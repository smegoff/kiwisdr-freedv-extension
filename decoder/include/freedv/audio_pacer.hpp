#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace kfd {

// Converts modem-sized decoded speech bursts into receiver-sized packets.
// Each take() returns either no packet or exactly the requested number of
// samples so the Kiwi can replace one normal SND packet without changing the
// browser's long-term audio rate.
class AudioPacer {
 public:
  void configure(uint32_t sample_rate, uint32_t maximum_queue_ms = 500,
                 uint32_t target_queue_ms = 0);
  void clear();
  void push(const int16_t* samples, std::size_t count);
  void push(const std::vector<int16_t>& samples) { push(samples.data(), samples.size()); }
  std::vector<int16_t> take(std::size_t packet_samples);

  std::size_t queued_samples() const { return size_; }
  uint64_t dropped_samples() const { return dropped_samples_; }
  uint64_t underruns() const { return underruns_; }
  uint64_t reprimes() const { return reprimes_; }
  std::size_t high_water_samples() const { return high_water_samples_; }
  bool primed() const { return primed_; }

 private:
  void discard_oldest(std::size_t count);
  void resize_queue(std::size_t maximum_samples);

  std::vector<int16_t> queue_;
  std::size_t head_ = 0;
  std::size_t size_ = 0;
  std::size_t maximum_samples_ = 6000;
  std::size_t target_samples_ = 0;
  std::size_t high_water_samples_ = 0;
  bool primed_ = true;
  bool starvation_pending_ = false;
  uint64_t dropped_samples_ = 0;
  uint64_t underruns_ = 0;
  uint64_t reprimes_ = 0;
};

}  // namespace kfd
