#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kfd {

struct DecodeStatus {
  bool synced = false;
  float snr_db = 0;
  float frequency_offset_hz = 0;
  std::string callsign;
  std::string text;
  std::string error;
  std::optional<uint64_t> bits;
  std::optional<uint64_t> bit_errors;
  std::optional<uint64_t> packets;
  std::optional<uint64_t> packet_errors;
  std::optional<uint64_t> resyncs;
  std::optional<float> clock_offset_ppm;
  std::optional<float> timing_offset;
  std::optional<float> sync_metric;
  std::optional<float> codec_variance;
};

struct DecodeResult {
  uint32_t sample_rate = 8000;
  std::vector<int16_t> pcm;
  DecodeStatus status;
};

class DecoderBackend {
 public:
  virtual ~DecoderBackend() = default;
  virtual const char* name() const = 0;
  virtual uint32_t modem_sample_rate() const = 0;
  virtual DecodeResult push(const int16_t* samples, std::size_t count) = 0;
  virtual void reset() = 0;
};

std::unique_ptr<DecoderBackend> make_codec2_backend(const std::string& mode);
std::unique_ptr<DecoderBackend> make_rade_backend(bool enabled);

} // namespace kfd
