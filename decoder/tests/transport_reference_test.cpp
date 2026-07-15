#include "freedv/backend.hpp"
#include "freedv/kiwi_protocol.hpp"
#include "freedv/resampler.hpp"

#include <algorithm>
#include <array>
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
  if (offset < 24 || offset >= bytes.size() || (offset & 1) ||
      encoding != 3 || channels != 1)
    throw std::runtime_error("reference recording must be mono 16-bit big-endian PCM");
  std::vector<int16_t> samples;
  samples.reserve((bytes.size() - offset) / 2);
  for (std::size_t i = offset; i + 1 < bytes.size(); i += 2) {
    samples.push_back(static_cast<int16_t>(
        (static_cast<uint16_t>(bytes[i]) << 8) | bytes[i + 1]));
  }
  return samples;
}

std::vector<uint8_t> snd_packet(const int16_t* samples, std::size_t count,
                                uint32_t sequence, bool little_endian) {
  std::vector<uint8_t> packet{'S', 'N', 'D',
      little_endian ? kfd::kSndFlagLittleEndian : uint8_t{0},
      static_cast<uint8_t>(sequence), static_cast<uint8_t>(sequence >> 8),
      static_cast<uint8_t>(sequence >> 16), static_cast<uint8_t>(sequence >> 24),
      0x04, 0xf6};
  packet.reserve(packet.size() + count * 2);
  for (std::size_t i = 0; i < count; ++i) {
    const auto value = static_cast<uint16_t>(samples[i]);
    if (little_endian) {
      packet.push_back(static_cast<uint8_t>(value));
      packet.push_back(static_cast<uint8_t>(value >> 8));
    } else {
      packet.push_back(static_cast<uint8_t>(value >> 8));
      packet.push_back(static_cast<uint8_t>(value));
    }
  }
  return packet;
}

bool run_case(const std::vector<int16_t>& input, uint32_t reported_rate,
              bool little_endian, bool skip_first_packet) {
  auto backend = kfd::make_codec2_backend("700D");
  kfd::StreamingResampler resampler;
  kfd::AdpcmState adpcm;
  bool synchronized = false;
  uint64_t decoded_samples = 0;
  uint32_t sequence = 1;
  constexpr std::size_t packet_samples = 1024;
  std::size_t offset = skip_first_packet ? packet_samples : 0;
  for (; offset < input.size(); offset += packet_samples) {
    const std::size_t count = std::min(packet_samples, input.size() - offset);
    const auto bytes = snd_packet(input.data() + offset, count, sequence++, little_endian);
    const auto packet = kfd::parse_kiwi_snd(bytes.data(), bytes.size());
    const auto pcm = kfd::decode_kiwi_audio(packet, adpcm, true);
    const auto modem = resampler.process(pcm, reported_rate, backend->modem_sample_rate());
    const auto result = backend->push(modem.data(), modem.size());
    synchronized = synchronized || result.status.synced;
    if (result.status.synced) decoded_samples += result.pcm.size();
  }
  std::cout << "endian=" << (little_endian ? "little" : "big")
            << " rate=" << reported_rate
            << " skip_first=" << (skip_first_packet ? 1 : 0)
            << " sync=" << (synchronized ? 1 : 0)
            << " decoded_samples=" << decoded_samples << '\n';
  return synchronized && decoded_samples;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: freedv-transport-reference-test FreeDV.test.au\n";
    return 2;
  }
  try {
    uint32_t sample_rate = 0;
    const auto input = read_au(argv[1], sample_rate);
    bool passed = true;
    for (bool little_endian : {false, true}) {
      for (uint32_t reported_rate : {sample_rate - 1, sample_rate, sample_rate + 1}) {
        for (bool skip_first : {false, true})
          passed = run_case(input, reported_rate, little_endian, skip_first) && passed;
      }
    }
    return passed ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << "transport reference test: " << error.what() << '\n';
    return 3;
  }
}
