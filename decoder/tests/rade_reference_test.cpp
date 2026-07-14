#include "freedv/backend.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

uint16_t read_u16(const std::vector<uint8_t>& bytes, std::size_t offset) {
  if (offset + 2 > bytes.size()) throw std::runtime_error("truncated WAV");
  return static_cast<uint16_t>(bytes[offset] | (bytes[offset + 1] << 8));
}

uint32_t read_u32(const std::vector<uint8_t>& bytes, std::size_t offset) {
  if (offset + 4 > bytes.size()) throw std::runtime_error("truncated WAV");
  return static_cast<uint32_t>(bytes[offset]) |
         (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
         (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
         (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

std::vector<int16_t> read_pcm16_mono_8k(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("unable to open RADE reference WAV");
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
  if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
      std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
    throw std::runtime_error("invalid RADE reference WAV");

  bool format_ok = false;
  std::size_t data_offset = 0;
  std::size_t data_size = 0;
  for (std::size_t offset = 12; offset + 8 <= bytes.size();) {
    const uint32_t size = read_u32(bytes, offset + 4);
    const std::size_t body = offset + 8;
    if (body + size > bytes.size()) throw std::runtime_error("truncated WAV chunk");
    if (std::memcmp(bytes.data() + offset, "fmt ", 4) == 0) {
      format_ok = size >= 16 && read_u16(bytes, body) == 1 &&
                  read_u16(bytes, body + 2) == 1 && read_u32(bytes, body + 4) == 8000 &&
                  read_u16(bytes, body + 14) == 16;
    } else if (std::memcmp(bytes.data() + offset, "data", 4) == 0) {
      data_offset = body;
      data_size = size;
    }
    offset = body + size + (size & 1u);
  }
  if (!format_ok || !data_offset || data_size < 1600 || (data_size & 1u))
    throw std::runtime_error("RADE reference must be mono 16-bit PCM at 8 kHz");
  std::vector<int16_t> samples(data_size / 2);
  for (std::size_t i = 0; i < samples.size(); i++)
    samples[i] = static_cast<int16_t>(read_u16(bytes, data_offset + 2 * i));
  return samples;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: freedv-rade-reference-test reference.wav\n";
    return 2;
  }
  const auto samples = read_pcm16_mono_8k(argv[1]);
  auto backend = kfd::make_rade_backend(true);
  assert(std::string(backend->name()) == "rade-v1");
  assert(backend->modem_sample_rate() == 8000);

  bool sync_seen = false;
  bool streaming_state_checked = false;
  std::size_t pcm_samples = 0;
  const auto started = std::chrono::steady_clock::now();
  for (std::size_t offset = 0; offset < samples.size();) {
    const std::size_t count = std::min<std::size_t>(257, samples.size() - offset);
    const auto result = backend->push(samples.data() + offset, count);
    assert(result.sample_rate == 16000);
    assert(result.status.error.empty());
    sync_seen = sync_seen || result.status.synced;
    if (result.status.synced && !streaming_state_checked) {
      const auto between_frames = backend->push(nullptr, 0);
      assert(between_frames.status.synced);
      streaming_state_checked = true;
    }
    pcm_samples += result.pcm.size();
    offset += count;
  }
  const auto elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - started).count();
  const double signal_seconds = static_cast<double>(samples.size()) / 8000.0;
  const double realtime_factor = elapsed / signal_seconds;
  assert(sync_seen);
  assert(streaming_state_checked);
  assert(pcm_samples >= 16000);
  backend->reset();
  std::vector<int16_t> overload(4001);
  const auto overloaded = backend->push(overload.data(), overload.size());
  assert(!overloaded.status.error.empty());
  backend->reset();
  std::cout << "RADEV1 reference: sync=yes pcm_samples=" << pcm_samples
            << " elapsed_seconds=" << elapsed
            << " realtime_factor=" << realtime_factor << '\n';
  return 0;
}
