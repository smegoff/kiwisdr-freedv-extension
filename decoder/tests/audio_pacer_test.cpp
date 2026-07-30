#include "freedv/audio_pacer.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
  kfd::AudioPacer pacer;
  pacer.configure(12000, 500);

  // Codec2 700D commonly returns 160 ms (1920 samples at 12 kHz) at once.
  // The Kiwi receiver supplies 512-sample packets. Never forward the whole
  // modem burst in a single receiver cadence.
  std::vector<int16_t> decoded(1920);
  for (std::size_t i = 0; i < decoded.size(); ++i)
    decoded[i] = static_cast<int16_t>(i + 1);
  pacer.push(decoded);
  for (int packet = 0; packet < 3; ++packet) {
    const auto output = pacer.take(512);
    assert(output.size() == 512);
    assert(output.front() == packet * 512 + 1);
    assert(output.back() == (packet + 1) * 512);
  }
  const auto tail = pacer.take(512);
  assert(tail.size() == 512);
  assert(tail.front() == 1537);
  assert(tail[383] == 1920);
  assert(tail[384] == 0);
  assert(pacer.queued_samples() == 0);
  assert(pacer.take(512).empty());

  // The 500 ms cap discards oldest stale audio instead of increasing latency.
  std::vector<int16_t> overload(7000, 7);
  pacer.push(overload);
  assert(pacer.queued_samples() == 6000);
  assert(pacer.dropped_samples() == 1000);
  pacer.clear();
  assert(pacer.queued_samples() == 0);
  assert(pacer.dropped_samples() == 0);

  std::cout << "audio pacer: ok\n";
  return 0;
}
