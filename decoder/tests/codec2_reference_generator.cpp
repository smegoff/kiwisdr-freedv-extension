#include <freedv_api.h>
#include <samplerate.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

uint16_t le16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | static_cast<uint16_t>(p[1] << 8);
}

uint32_t le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void write_be32(std::ofstream& output, uint32_t value) {
  const uint8_t bytes[] = {static_cast<uint8_t>(value >> 24),
                           static_cast<uint8_t>(value >> 16),
                           static_cast<uint8_t>(value >> 8),
                           static_cast<uint8_t>(value)};
  output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
}

std::vector<int16_t> read_wav(const std::string& path, uint32_t& sample_rate) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("unable to open input WAV");
  const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
  if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
      std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
    throw std::runtime_error("input is not a RIFF/WAVE file");

  uint16_t format = 0;
  uint16_t channels = 0;
  uint16_t bits = 0;
  const uint8_t* pcm = nullptr;
  std::size_t pcm_bytes = 0;
  for (std::size_t offset = 12; offset + 8 <= bytes.size();) {
    const uint32_t length = le32(bytes.data() + offset + 4);
    const std::size_t data = offset + 8;
    if (data + length > bytes.size()) throw std::runtime_error("truncated WAV chunk");
    if (std::memcmp(bytes.data() + offset, "fmt ", 4) == 0 && length >= 16) {
      format = le16(bytes.data() + data);
      channels = le16(bytes.data() + data + 2);
      sample_rate = le32(bytes.data() + data + 4);
      bits = le16(bytes.data() + data + 14);
    } else if (std::memcmp(bytes.data() + offset, "data", 4) == 0) {
      pcm = bytes.data() + data;
      pcm_bytes = length;
    }
    offset = data + length + (length & 1);
  }
  if (format != 1 || channels != 1 || bits != 16 || sample_rate == 0 || !pcm ||
      (pcm_bytes & 1))
    throw std::runtime_error("input WAV must be mono 16-bit PCM");

  std::vector<int16_t> samples(pcm_bytes / 2);
  for (std::size_t i = 0; i < samples.size(); ++i)
    samples[i] = static_cast<int16_t>(le16(pcm + i * 2));
  return samples;
}

std::vector<int16_t> resample(const std::vector<int16_t>& input, uint32_t input_rate,
                              uint32_t output_rate) {
  if (input_rate == output_rate) return input;
  std::vector<float> source(input.size());
  src_short_to_float_array(input.data(), source.data(), static_cast<int>(source.size()));
  const double ratio = static_cast<double>(output_rate) / input_rate;
  std::vector<float> destination(
      static_cast<std::size_t>(input.size() * ratio) + 4096);
  SRC_DATA data{};
  data.data_in = source.data();
  data.input_frames = static_cast<long>(source.size());
  data.data_out = destination.data();
  data.output_frames = static_cast<long>(destination.size());
  data.src_ratio = ratio;
  data.end_of_input = 1;
  const int error = src_simple(&data, SRC_SINC_BEST_QUALITY, 1);
  if (error) throw std::runtime_error(src_strerror(error));
  std::vector<int16_t> output(static_cast<std::size_t>(data.output_frames_gen));
  src_float_to_short_array(destination.data(), output.data(), static_cast<int>(output.size()));
  return output;
}

void write_au(const std::string& path, const std::vector<int16_t>& samples,
              uint32_t sample_rate) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("unable to open output AU");
  output.write(".snd", 4);
  write_be32(output, 24);
  write_be32(output, static_cast<uint32_t>(samples.size() * 2));
  write_be32(output, 3);
  write_be32(output, sample_rate);
  write_be32(output, 1);
  for (int16_t sample : samples) {
    const auto value = static_cast<uint16_t>(sample);
    const uint8_t bytes[] = {static_cast<uint8_t>(value >> 8),
                             static_cast<uint8_t>(value)};
    output.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
  }
  if (!output) throw std::runtime_error("failed writing output AU");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: freedv-codec2-reference-generator input.wav output.au\n";
    return 2;
  }
  try {
    uint32_t input_rate = 0;
    const auto input = read_wav(argv[1], input_rate);
    auto speech = resample(input, input_rate, 8000);
    // Give the receiver a valid 700D waveform before and after the spoken
    // sample. These are encoded silence frames, not an unmodulated gap.
    speech.insert(speech.begin(), 8000, 0);
    speech.insert(speech.end(), 8000, 0);

    struct freedv* modem = freedv_open(FREEDV_MODE_700D);
    if (!modem) throw std::runtime_error("freedv_open(700D) failed");
    const int speech_per_frame = freedv_get_n_speech_samples(modem);
    const int modem_per_frame = freedv_get_n_tx_modem_samples(modem);
    if (speech_per_frame <= 0 || modem_per_frame <= 0) {
      freedv_close(modem);
      throw std::runtime_error("invalid FreeDV frame dimensions");
    }
    const std::size_t complete =
        (speech.size() + speech_per_frame - 1) / speech_per_frame * speech_per_frame;
    speech.resize(complete, 0);
    std::vector<int16_t> waveform;
    waveform.reserve(speech.size() / speech_per_frame * modem_per_frame);
    std::vector<int16_t> frame(static_cast<std::size_t>(modem_per_frame));
    for (std::size_t offset = 0; offset < speech.size(); offset += speech_per_frame) {
      freedv_tx(modem, frame.data(), speech.data() + offset);
      waveform.insert(waveform.end(), frame.begin(), frame.end());
    }
    freedv_close(modem);

    const auto output = resample(waveform, 8000, 12000);
    write_au(argv[2], output, 12000);
    std::cout << "generated clean 700D reference: speech_seconds="
              << static_cast<double>(speech.size()) / 8000.0
              << " modem_seconds=" << static_cast<double>(output.size()) / 12000.0
              << " output_samples=" << output.size() << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "reference generator: " << error.what() << '\n';
    return 3;
  }
}
