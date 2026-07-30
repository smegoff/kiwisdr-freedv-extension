#include "freedv/audio_pacer.hpp"

#include <algorithm>

namespace kfd {

void AudioPacer::configure(uint32_t sample_rate, uint32_t maximum_queue_ms) {
  maximum_samples_ = std::max<std::size_t>(
      1, static_cast<std::size_t>(sample_rate) * maximum_queue_ms / 1000);
  trim_to_limit();
}

void AudioPacer::clear() {
  queue_.clear();
  dropped_samples_ = 0;
}

void AudioPacer::push(const int16_t* samples, std::size_t count) {
  if (!samples || count == 0) return;
  queue_.insert(queue_.end(), samples, samples + count);
  trim_to_limit();
}

std::vector<int16_t> AudioPacer::take(std::size_t packet_samples) {
  if (packet_samples == 0 || queue_.empty()) return {};
  std::vector<int16_t> packet(packet_samples, 0);
  const std::size_t available = std::min(packet_samples, queue_.size());
  for (std::size_t i = 0; i < available; ++i) {
    packet[i] = queue_.front();
    queue_.pop_front();
  }
  return packet;
}

void AudioPacer::trim_to_limit() {
  while (queue_.size() > maximum_samples_) {
    queue_.pop_front();
    dropped_samples_++;
  }
}

}  // namespace kfd
