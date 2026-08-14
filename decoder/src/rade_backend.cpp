#include "freedv/backend.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <stdexcept>
#include <vector>

#ifdef HAVE_RADE
#include "freedv/rade_v1_wrapper.h"
#endif

namespace kfd {

#ifdef HAVE_RADE
namespace {

constexpr std::size_t kMaxQueuedSamples = 4000;  // 500 ms at 8 kHz.
// The official rade_c real-valued receiver path uses imag=0 and compensates
// for the discarded negative-frequency image with 2 / RADE_INT16_SCALE.
constexpr float kRadeRealInt16Scale = 2.0f / 16384.0f;

bool valid_eoo_callsign(const char* value) {
  if (!value || !*value) return false;
  const std::size_t length = std::strlen(value);
  if (length > 8) return false;
  return std::all_of(value, value + length, [](unsigned char c) {
    return std::isalnum(c) || c == '/';
  });
}

class RadeBackend final : public DecoderBackend {
 public:
  RadeBackend() {
    char error[160] = {0};
    decoder_ = kfd_rade_create(error, sizeof(error));
    if (!decoder_) throw std::runtime_error(error[0] ? error : "unable to open RADEv1");
    pcm_scratch_.resize(kfd_rade_pcm_max(decoder_));
    iq_.reserve(kMaxQueuedSamples);
    if (pcm_scratch_.empty() || kfd_rade_input_max(decoder_) > kMaxQueuedSamples) {
      kfd_rade_destroy(decoder_);
      decoder_ = nullptr;
      throw std::runtime_error("unsupported RADEv1 frame dimensions");
    }
  }

  ~RadeBackend() override { kfd_rade_destroy(decoder_); }
  const char* name() const override { return "rade-v1"; }
  uint32_t modem_sample_rate() const override { return 8000; }

  DecodeResult push(const int16_t* samples, std::size_t count) override {
    DecodeResult result;
    result.sample_rate = 16000;
    result.pcm.reserve(pcm_scratch_.size());
    if (count && !samples) throw std::invalid_argument("null RADE input");
    if (iq_.size() + count > kMaxQueuedSamples) {
      reset();
      result.status.error = "RADE input queue overflow";
      return result;
    }

    for (std::size_t i = 0; i < count; i++) {
      iq_.push_back({static_cast<float>(samples[i]) * kRadeRealInt16Scale, 0.0f});
    }

    std::size_t consumed = 0;
    while (true) {
      const std::size_t needed = kfd_rade_input_needed(decoder_);
      if (!needed || iq_.size() - consumed < needed) break;
      kfd_rade_output output{};
      char error[160] = {0};
      if (kfd_rade_decode(decoder_, iq_.data() + consumed, needed,
                          pcm_scratch_.data(), pcm_scratch_.size(), &output,
                          error, sizeof(error)) != 0) {
        reset();
        throw std::runtime_error(error[0] ? error : "RADEv1 decode failed");
      }
      consumed += needed;
      last_status_.synced = output.synced != 0;
      last_status_.snr_db = output.snr_db;
      last_status_.frequency_offset_hz = output.frequency_offset_hz;
      if (valid_eoo_callsign(output.eoo_callsign)) callsign_ = output.eoo_callsign;
      result.pcm.insert(result.pcm.end(), pcm_scratch_.begin(),
                        pcm_scratch_.begin() + output.pcm_samples);
    }
    if (consumed) iq_.erase(iq_.begin(), iq_.begin() + consumed);
    if (!last_status_.synced) callsign_.clear();
    last_status_.callsign = callsign_;
    result.status = last_status_;
    // A valid final speech frame can coincide with the modem dropping sync at
    // End of Over. Allow that decoded frame through the Kiwi audio gate.
    if (!result.pcm.empty()) result.status.synced = true;
    return result;
  }

  void reset() override {
    iq_.clear();
    callsign_.clear();
    last_status_ = {};
    char error[160] = {0};
    if (decoder_ && kfd_rade_reset(decoder_, error, sizeof(error)) != 0)
      throw std::runtime_error(error[0] ? error : "unable to reset RADEv1");
  }

 private:
  kfd_rade_decoder* decoder_ = nullptr;
  std::vector<kfd_rade_complex> iq_;
  std::vector<int16_t> pcm_scratch_;
  std::string callsign_;
  DecodeStatus last_status_;
};

}  // namespace
#endif

std::unique_ptr<DecoderBackend> make_rade_backend(bool enabled) {
  if (!enabled) throw std::runtime_error("RADE backend is disabled");
#ifdef HAVE_RADE
  return std::make_unique<RadeBackend>();
#else
  throw std::runtime_error("RADEv1 adapter is not compiled into this decoder");
#endif
}

} // namespace kfd
