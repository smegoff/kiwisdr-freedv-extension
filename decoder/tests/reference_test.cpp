#include "freedv/backend.hpp"

#include <samplerate.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

uint32_t be32(const std::vector<uint8_t>& bytes, std::size_t offset) {
  return (static_cast<uint32_t>(bytes.at(offset)) << 24) |
         (static_cast<uint32_t>(bytes.at(offset + 1)) << 16) |
         (static_cast<uint32_t>(bytes.at(offset + 2)) << 8) |
         static_cast<uint32_t>(bytes.at(offset + 3));
}

std::vector<int16_t> read_au(const std::string& path, uint32_t& sample_rate) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("unable to open reference recording");
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
  if (bytes.size() < 28 || std::string(bytes.begin(), bytes.begin() + 4) != ".snd")
    throw std::runtime_error("invalid AU header");
  const uint32_t offset = be32(bytes, 4);
  const uint32_t encoding = be32(bytes, 12);
  sample_rate = be32(bytes, 16);
  const uint32_t channels = be32(bytes, 20);
  if (offset < 24 || offset >= bytes.size() || (offset & 1) || encoding != 3 || channels != 1)
    throw std::runtime_error("reference recording must be mono 16-bit big-endian PCM");
  std::vector<int16_t> samples;
  samples.reserve((bytes.size() - offset) / 2);
  for (std::size_t i = offset; i + 1 < bytes.size(); i += 2) {
    samples.push_back(static_cast<int16_t>(
        (static_cast<uint16_t>(bytes[i]) << 8) | bytes[i + 1]));
  }
  return samples;
}

std::vector<int16_t> resample(const std::vector<int16_t>& input, uint32_t input_rate,
                              uint32_t output_rate) {
  if (input_rate == output_rate) return input;
  std::vector<float> source(input.size());
  src_short_to_float_array(input.data(), source.data(), static_cast<int>(source.size()));
  const double ratio = static_cast<double>(output_rate) / input_rate;
  std::vector<float> destination(static_cast<std::size_t>(input.size() * ratio) + 1024);
  SRC_DATA data{};
  data.data_in = source.data();
  data.input_frames = static_cast<long>(source.size());
  data.data_out = destination.data();
  data.output_frames = static_cast<long>(destination.size());
  data.src_ratio = ratio;
  data.end_of_input = 1;
  const int error = src_simple(&data, SRC_SINC_FASTEST, 1);
  if (error) throw std::runtime_error(src_strerror(error));
  std::vector<int16_t> output(static_cast<std::size_t>(data.output_frames_gen));
  src_float_to_short_array(destination.data(), output.data(), static_cast<int>(output.size()));
  return output;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: freedv-reference-test MODE FreeDV.test.au\n";
    return 2;
  }
  try {
    uint32_t input_rate = 0;
    const auto input = read_au(argv[2], input_rate);
    auto backend = kfd::make_codec2_backend(argv[1]);
    const auto modem = resample(input, input_rate, backend->modem_sample_rate());
    bool synchronized = false;
    uint64_t decoded_samples = 0;
    constexpr std::size_t chunk = 320;
    for (std::size_t offset = 0; offset < modem.size(); offset += chunk) {
      const std::size_t count = std::min(chunk, modem.size() - offset);
      const auto result = backend->push(modem.data() + offset, count);
      synchronized = synchronized || result.status.synced;
      if (result.status.synced) decoded_samples += result.pcm.size();
    }
    std::cout << "mode=" << argv[1] << " input_rate=" << input_rate
              << " sync=" << (synchronized ? 1 : 0)
              << " decoded_samples=" << decoded_samples << '\n';
    return synchronized && decoded_samples ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "reference test: " << error.what() << '\n';
    return 3;
  }
}
