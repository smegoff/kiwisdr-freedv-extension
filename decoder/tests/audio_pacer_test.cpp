#include "freedv/audio_pacer.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
  // With no target configured the pacer retains the legacy behaviour used by
  // the packet-framing tests, including zero-padding a final short packet.
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

  // RADE and Codec2 emit speech in modem-sized bursts. A target queue keeps
  // the receiver cadence from draining the first burst before the next one is
  // available. No partial/zero-padded packet is released in primed mode.
  kfd::AudioPacer buffered;
  buffered.configure(12000, 500, 200);
  std::vector<int16_t> first_burst(1440);
  std::vector<int16_t> second_burst(1440);
  for (std::size_t i = 0; i < first_burst.size(); ++i) {
    first_burst[i] = static_cast<int16_t>(i + 1);
    second_burst[i] = static_cast<int16_t>(i + first_burst.size() + 1);
  }
  buffered.push(first_burst);
  assert(!buffered.primed());
  assert(buffered.take(512).empty());
  buffered.push(second_burst);
  assert(buffered.primed());
  const auto buffered_packet = buffered.take(512);
  assert(buffered_packet.size() == 512);
  assert(buffered_packet.front() == 1);
  assert(buffered_packet.back() == 512);
  assert(buffered.high_water_samples() == 2880);

  // An underrun retains the short tail and re-primes before resuming. This
  // avoids the old repeating speech/zero-fill cycle.
  kfd::AudioPacer reprime;
  reprime.configure(1000, 100, 100);
  std::vector<int16_t> sequence(180);
  for (std::size_t i = 0; i < sequence.size(); ++i)
    sequence[i] = static_cast<int16_t>(i + 1);
  reprime.push(sequence.data(), 100);
  const auto first = reprime.take(80);
  assert(first.front() == 1 && first.back() == 80);
  assert(reprime.take(80).empty());
  assert(reprime.queued_samples() == 20);
  // A queue drain at end-of-over is not an audible interruption. It is only
  // confirmed as an underrun if later speech arrives and playback re-primes.
  assert(reprime.underruns() == 0);
  assert(reprime.reprimes() == 0);
  reprime.push(sequence.data() + 100, 79);
  assert(reprime.take(80).empty());
  reprime.push(sequence.data() + 179, 1);
  const auto resumed = reprime.take(80);
  assert(resumed.front() == 81 && resumed.back() == 160);
  assert(reprime.underruns() == 1);
  assert(reprime.reprimes() == 1);

  // Exercise ring wrap and overflow. The newest bounded window is preserved.
  kfd::AudioPacer ring;
  ring.configure(1000, 10);
  std::vector<int16_t> twenty(20);
  for (std::size_t i = 0; i < twenty.size(); ++i)
    twenty[i] = static_cast<int16_t>(i + 1);
  ring.push(twenty);
  assert(ring.queued_samples() == 10);
  assert(ring.dropped_samples() == 10);
  const auto newest = ring.take(10);
  assert(newest.front() == 11 && newest.back() == 20);

  // Simulate a 12 kHz Kiwi returning 512-sample packets while the modem emits
  // 120 ms decoded bursts. Once primed, equal long-term production and
  // consumption must not create periodic underruns.
  kfd::AudioPacer cadence;
  cadence.configure(12000, 500, 200);
  std::vector<int16_t> modem_burst(1440, 1234);
  std::size_t modem_credit = 0;
  std::size_t returned_packets = 0;
  for (int packet = 0; packet < 600; ++packet) {
    modem_credit += 512;
    while (modem_credit >= modem_burst.size()) {
      cadence.push(modem_burst);
      modem_credit -= modem_burst.size();
    }
    const auto output = cadence.take(512);
    if (!output.empty()) {
      assert(output.size() == 512);
      assert(output.front() == 1234 && output.back() == 1234);
      returned_packets++;
    }
  }
  assert(returned_packets > 590);
  assert(cadence.underruns() == 0);
  assert(cadence.dropped_samples() == 0);

  std::cout << "audio pacer: ok\n";
  return 0;
}
