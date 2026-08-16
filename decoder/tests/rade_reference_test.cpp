#include "freedv/backend.hpp"
#include "freedv/audio_pacer.hpp"
#include "freedv/resampler.hpp"

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

struct CadenceResult {
  std::size_t returned_packets = 0;
  uint64_t underruns = 0;
  uint64_t reprimes = 0;
  std::size_t high_water_samples = 0;
};

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

void write_u16(std::ofstream& output, uint16_t value) {
  const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8)};
  output.write(bytes, sizeof(bytes));
}

void write_u32(std::ofstream& output, uint32_t value) {
  const char bytes[] = {static_cast<char>(value), static_cast<char>(value >> 8),
                        static_cast<char>(value >> 16), static_cast<char>(value >> 24)};
  output.write(bytes, sizeof(bytes));
}

void write_pcm16_mono_16k(const std::string& path, const std::vector<int16_t>& samples) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("unable to create RADE decoded WAV");
  const uint32_t data_bytes = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
  output.write("RIFF", 4);
  write_u32(output, 36 + data_bytes);
  output.write("WAVEfmt ", 8);
  write_u32(output, 16);
  write_u16(output, 1);
  write_u16(output, 1);
  write_u32(output, 16000);
  write_u32(output, 32000);
  write_u16(output, 2);
  write_u16(output, 16);
  output.write("data", 4);
  write_u32(output, data_bytes);
  for (const auto sample : samples) write_u16(output, static_cast<uint16_t>(sample));
  if (!output) throw std::runtime_error("unable to write RADE decoded WAV");
}

CadenceResult run_kiwi_cadence(const std::vector<int16_t>& modem_8k) {
  kfd::StreamingResampler source_resampler;
  const auto modem_12k = source_resampler.process(modem_8k, 8000, 12000);
  auto backend = kfd::make_rade_backend(true);
  kfd::StreamingResampler input_resampler;
  kfd::StreamingResampler output_resampler;
  kfd::AudioPacer pacer;
  pacer.configure(12000, 500, 280);

  CadenceResult result;
  constexpr std::size_t packet_samples = 512;
  for (std::size_t offset = 0; offset < modem_12k.size(); offset += packet_samples) {
    const std::size_t count = std::min(packet_samples, modem_12k.size() - offset);
    const std::vector<int16_t> packet(modem_12k.begin() + offset,
                                      modem_12k.begin() + offset + count);
    const auto& modem = input_resampler.process(packet, 12000, backend->modem_sample_rate());
    const auto decoded = backend->push(modem.data(), modem.size());
    if (decoded.status.synced && !decoded.pcm.empty()) {
      const auto& speech = output_resampler.process(decoded.pcm, decoded.sample_rate, 12000);
      pacer.push(speech);
    }
    if (!pacer.take(count).empty()) result.returned_packets++;
  }
  result.underruns = pacer.underruns();
  result.reprimes = pacer.reprimes();
  result.high_water_samples = pacer.high_water_samples();
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2 && argc != 3) {
    std::cerr << "usage: freedv-rade-reference-test reference.wav [decoded.wav]\n";
    return 2;
  }
  const auto samples = read_pcm16_mono_8k(argv[1]);
  auto backend = kfd::make_rade_backend(true);
  assert(std::string(backend->name()) == "rade-v1");
  assert(backend->modem_sample_rate() == 8000);

  bool sync_seen = false;
  bool streaming_state_checked = false;
  std::size_t pcm_samples = 0;
  std::vector<int16_t> pcm_output;
  const auto started = std::chrono::steady_clock::now();
  for (std::size_t offset = 0; offset < samples.size();) {
    const std::size_t count = std::min<std::size_t>(257, samples.size() - offset);
    const auto result = backend->push(samples.data() + offset, count);
    assert(result.sample_rate == 16000);
    assert(result.status.error.empty());
    assert(result.status.resyncs.has_value());
    sync_seen = sync_seen || result.status.synced;
    if (result.status.synced && !streaming_state_checked) {
      const auto between_frames = backend->push(nullptr, 0);
      assert(between_frames.status.synced);
      streaming_state_checked = true;
    }
    pcm_samples += result.pcm.size();
    pcm_output.insert(pcm_output.end(), result.pcm.begin(), result.pcm.end());
    offset += count;
  }
  const auto elapsed = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - started).count();
  const double signal_seconds = static_cast<double>(samples.size()) / 8000.0;
  const double realtime_factor = elapsed / signal_seconds;
  assert(sync_seen);
  assert(streaming_state_checked);
  const auto final_state = backend->push(nullptr, 0);
  assert(final_state.status.resyncs.has_value());
  assert(pcm_samples >= 16000);
  if (argc == 3) write_pcm16_mono_16k(argv[2], pcm_output);
  backend->reset();
  std::vector<int16_t> overload(4001);
  const auto overloaded = backend->push(overload.data(), overload.size());
  assert(!overloaded.status.error.empty());
  backend->reset();
  backend.reset();
  const auto cadence = run_kiwi_cadence(samples);
  assert(cadence.returned_packets > 0);
  assert(cadence.underruns == 0);
  assert(cadence.reprimes == 0);
  std::cout << "RADEV1 reference: sync=yes pcm_samples=" << pcm_samples
            << " elapsed_seconds=" << elapsed
            << " realtime_factor=" << realtime_factor
            << " cadence_packets=" << cadence.returned_packets
            << " cadence_underruns=" << cadence.underruns
            << " cadence_high_water=" << cadence.high_water_samples << '\n';
  return 0;
}
