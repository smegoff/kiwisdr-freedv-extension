#include "freedv/audio_pacer.hpp"

#include <algorithm>
#include <stdexcept>

namespace kfd {

void AudioPacer::configure(uint32_t sample_rate, uint32_t maximum_queue_ms,
                           uint32_t target_queue_ms) {
  const std::size_t maximum_samples = std::max<std::size_t>(
      1, static_cast<std::size_t>(sample_rate) * maximum_queue_ms / 1000);
  const std::size_t target_samples = std::min(
      maximum_samples, static_cast<std::size_t>(sample_rate) * target_queue_ms / 1000);
  if (maximum_samples != maximum_samples_) resize_queue(maximum_samples);
  if (target_samples != target_samples_) {
    target_samples_ = target_samples;
    primed_ = target_samples_ == 0 || size_ >= target_samples_;
  }
}

void AudioPacer::clear() {
  head_ = 0;
  size_ = 0;
  high_water_samples_ = 0;
  primed_ = target_samples_ == 0;
  starvation_pending_ = false;
  dropped_samples_ = 0;
  underruns_ = 0;
  reprimes_ = 0;
}

void AudioPacer::push(const int16_t* samples, std::size_t count) {
  if (!samples || count == 0) return;
  if (queue_.size() != maximum_samples_) resize_queue(maximum_samples_);

  if (count >= maximum_samples_) {
    dropped_samples_ += size_ + count - maximum_samples_;
    samples += count - maximum_samples_;
    count = maximum_samples_;
    head_ = 0;
    size_ = 0;
  } else if (size_ + count > maximum_samples_) {
    const std::size_t discard = size_ + count - maximum_samples_;
    discard_oldest(discard);
    dropped_samples_ += discard;
  }

  const std::size_t tail = (head_ + size_) % maximum_samples_;
  const std::size_t first = std::min(count, maximum_samples_ - tail);
  std::copy_n(samples, first, queue_.begin() + tail);
  std::copy_n(samples + first, count - first, queue_.begin());
  size_ += count;
  high_water_samples_ = std::max(high_water_samples_, size_);

  if (!primed_ && size_ >= target_samples_) {
    if (starvation_pending_) {
      underruns_++;
      reprimes_++;
      starvation_pending_ = false;
    }
    primed_ = true;
  }
}

std::vector<int16_t> AudioPacer::take(std::size_t packet_samples) {
  if (packet_samples == 0 || size_ == 0 || !primed_) return {};
  if (target_samples_ && size_ < packet_samples) {
    primed_ = false;
    starvation_pending_ = true;
    return {};
  }

  std::vector<int16_t> packet(packet_samples, 0);
  const std::size_t available = std::min(packet_samples, size_);
  const std::size_t first = std::min(available, maximum_samples_ - head_);
  std::copy_n(queue_.begin() + head_, first, packet.begin());
  std::copy_n(queue_.begin(), available - first, packet.begin() + first);
  discard_oldest(available);
  return packet;
}

void AudioPacer::discard_oldest(std::size_t count) {
  count = std::min(count, size_);
  if (count == 0) return;
  head_ = (head_ + count) % maximum_samples_;
  size_ -= count;
  if (size_ == 0) head_ = 0;
}

void AudioPacer::resize_queue(std::size_t maximum_samples) {
  if (maximum_samples == 0) throw std::invalid_argument("audio queue cannot be empty");
  std::vector<int16_t> replacement(maximum_samples);
  const std::size_t keep = std::min(size_, maximum_samples);
  const std::size_t discard = size_ - keep;
  for (std::size_t i = 0; i < keep; ++i) {
    const std::size_t source = (head_ + discard + i) % std::max<std::size_t>(1, queue_.size());
    replacement[i] = queue_.empty() ? 0 : queue_[source];
  }
  dropped_samples_ += discard;
  queue_.swap(replacement);
  maximum_samples_ = maximum_samples;
  head_ = 0;
  size_ = keep;
  high_water_samples_ = std::max(high_water_samples_, size_);
}

}  // namespace kfd
