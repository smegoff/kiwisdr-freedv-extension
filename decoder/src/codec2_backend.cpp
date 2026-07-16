#include "freedv/backend.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

#ifdef HAVE_CODEC2
#include <freedv_api.h>
#include <modem_stats.h>
#include <reliable_text.h>
#endif

namespace kfd {

#ifdef HAVE_CODEC2
class Codec2Backend final : public DecoderBackend {
 public:
  explicit Codec2Backend(int mode) : mode_(mode), ctx_(freedv_open(mode)) {
    if (!ctx_) throw std::runtime_error("freedv_open failed");
    reliable_text_ = reliable_text_create();
    if (!reliable_text_) throw std::runtime_error("reliable_text_create failed");
    reliable_text_use_with_freedv(reliable_text_, ctx_, &Codec2Backend::on_reliable_text, this);
  }
  ~Codec2Backend() override {
    reliable_text_unlink_from_freedv(reliable_text_);
    reliable_text_destroy(reliable_text_);
    freedv_close(ctx_);
  }
  const char* name() const override { return "codec2"; }
  uint32_t modem_sample_rate() const override { return 8000; }

  DecodeResult push(const int16_t* samples, std::size_t count) override {
    input_.insert(input_.end(), samples, samples + count);
    DecodeResult result;
    int nin = freedv_nin(ctx_);
    while (input_.size() >= static_cast<std::size_t>(nin)) {
      std::vector<int16_t> speech(freedv_get_n_max_speech_samples(ctx_));
      const int nout = freedv_rx(ctx_, speech.data(), input_.data());
      input_.erase(input_.begin(), input_.begin() + nin);
      result.pcm.insert(result.pcm.end(), speech.begin(), speech.begin() + nout);
      nin = freedv_nin(ctx_);
    }
    MODEM_STATS stats{};
    freedv_get_modem_extended_stats(ctx_, &stats);
    result.status.synced = freedv_get_sync(ctx_) != 0;
    if (result.status.synced && !was_synced_) {
      if (had_sync_) resyncs_++;
      had_sync_ = true;
    }
    if (!result.status.synced && was_synced_) {
      reliable_text_reset(reliable_text_);
      callsign_.clear();
    }
    was_synced_ = result.status.synced;
    result.status.snr_db = stats.snr_est;
    result.status.frequency_offset_hz = stats.foff;
    result.status.callsign = callsign_;
    result.status.bits = static_cast<uint64_t>(std::max(0, freedv_get_total_bits(ctx_)));
    result.status.bit_errors = static_cast<uint64_t>(
        std::max(0, freedv_get_total_bit_errors(ctx_)));
    result.status.packets = static_cast<uint64_t>(std::max(0, freedv_get_total_packets(ctx_)));
    result.status.packet_errors = static_cast<uint64_t>(
        std::max(0, freedv_get_total_packet_errors(ctx_)));
    result.status.resyncs = resyncs_;
    result.status.clock_offset_ppm = stats.clock_offset;
    result.status.timing_offset = stats.rx_timing;
    result.status.sync_metric = stats.sync_metric;
    return result;
  }
  void reset() override {
    input_.clear(); callsign_.clear(); was_synced_ = false; had_sync_ = false; resyncs_ = 0;
    reliable_text_reset(reliable_text_);
  }

 private:
  static void on_reliable_text(reliable_text_t, const char* text, int length, void* state) {
    auto* self = static_cast<Codec2Backend*>(state);
    if (text && length > 0) self->callsign_.assign(text, static_cast<std::size_t>(length));
  }

  int mode_;
  struct freedv* ctx_;
  reliable_text_t reliable_text_{};
  bool was_synced_ = false;
  bool had_sync_ = false;
  uint64_t resyncs_ = 0;
  std::string callsign_;
  std::vector<int16_t> input_;
};
#endif

std::unique_ptr<DecoderBackend> make_codec2_backend(const std::string& mode) {
#ifdef HAVE_CODEC2
  static const std::unordered_map<std::string, int> modes = {
      {"1600", FREEDV_MODE_1600}, {"700C", FREEDV_MODE_700C},
      {"700D", FREEDV_MODE_700D}, {"700E", FREEDV_MODE_700E},
      {"2400A", FREEDV_MODE_2400A}, {"2400B", FREEDV_MODE_2400B},
      {"800XA", FREEDV_MODE_800XA}};
  const auto it = modes.find(mode);
  if (it == modes.end()) throw std::invalid_argument("unsupported Codec2 mode");
  return std::make_unique<Codec2Backend>(it->second);
#else
  (void)mode;
  throw std::runtime_error("decoder built without libcodec2");
#endif
}

} // namespace kfd
