#include "freedv/kiwi_protocol.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

template <typename Callback>
bool throws(Callback callback) {
  try {
    callback();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

int main() {
  const std::string hmac = kfd::hmac_sha256_hex(
      "key", "The quick brown fox jumps over the lazy dog");
  assert(hmac == "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
  assert(kfd::make_poll_command("key", 123, "abcd").find("SET freedv_poll=2,123,abcd,") == 0);

  std::string pair;
  assert(kfd::parse_message_pair("camp=1,4", "camp", pair) && pair == "1,4");
  assert(kfd::parse_message_pair("foo=1 camp=1,4 state=ok", "camp", pair) &&
         pair == "1,4");
  assert(!kfd::parse_message_pair("audio_camp=1,0", "camp", pair));
  assert(kfd::parse_message_pair("audio_camp=1,0", "audio_camp", pair) && pair == "1,0");

  const std::string raw = R"({"protocol":2,"generation":9,"running":true,"rx_chan":3,"mode":"700D","input_rate":12000,"frequency_hz":7177000,"test":true})";
  const auto job = kfd::parse_decoder_job(kfd::url_encode(raw));
  assert(job.running && job.generation == 9 && job.rx_channel == 3 &&
         job.mode == "700D" && job.test);
  assert(kfd::url_decode(kfd::url_encode(raw)) == raw);
  const auto stopped = kfd::parse_decoder_job(kfd::url_encode(
      R"({"protocol":2,"generation":10,"running":false})"));
  assert(!stopped.running && stopped.generation == 10);
  assert(kfd::classify_job(job, job) == kfd::JobDisposition::unchanged);
  auto older = job;
  older.generation = 8;
  assert(kfd::classify_job(job, older) == kfd::JobDisposition::stale);
  auto newer = job;
  newer.generation = 10;
  assert(kfd::classify_job(job, newer) == kfd::JobDisposition::changed);
  auto conflict = job;
  conflict.mode = "1600";
  assert(kfd::classify_job(job, conflict) == kfd::JobDisposition::conflict);
  conflict = job;
  conflict.test = false;
  assert(kfd::classify_job(job, conflict) == kfd::JobDisposition::conflict);
  assert(throws([] { kfd::parse_decoder_job("%7Bbad-json%7D"); }));
  assert(throws([] { kfd::parse_decoder_job(kfd::url_encode(
      R"({"protocol":99,"generation":1,"running":false})")); }));
  assert(throws([] { kfd::parse_decoder_job(kfd::url_encode(
      R"({"protocol":2,"generation":1,"running":true,"rx_chan":0,"mode":"BAD","input_rate":12000,"frequency_hz":1})")); }));
  assert(throws([] { kfd::url_decode("%Q0"); }));

  const std::vector<uint8_t> snd = {'S','N','D',kfd::kSndFlagLittleEndian,
      0x78,0x56,0x34,0x12,0x04,0xf6,0x34,0x12,0xcc,0xed};
  const auto packet = kfd::parse_kiwi_snd(snd.data(), snd.size());
  assert(packet.sequence == 0x12345678 && packet.payload.size() == 4);
  kfd::AdpcmState state;
  const auto pcm = kfd::decode_kiwi_audio(packet, state, true);
  assert(pcm.size() == 2 && pcm[0] == 0x1234 && pcm[1] == static_cast<int16_t>(0xedcc));
  const auto encoded = kfd::encode_kiwi_audio(pcm, packet.flags, state);
  assert(encoded == packet.payload);

  const std::vector<uint8_t> snd_big = {'S','N','D',0,
      0x01,0x00,0x00,0x00,0x04,0xf6,0x12,0x34,0xed,0xcc};
  const auto packet_big = kfd::parse_kiwi_snd(snd_big.data(), snd_big.size());
  kfd::AdpcmState big_state;
  const auto pcm_big = kfd::decode_kiwi_audio(packet_big, big_state, true);
  assert(pcm_big.size() == 2 && pcm_big[0] == 0x1234 &&
         pcm_big[1] == static_cast<int16_t>(0xedcc));
  assert(kfd::encode_kiwi_audio(pcm_big, packet_big.flags, big_state) == packet_big.payload);
  assert(throws([] { const std::vector<uint8_t> short_snd = {'S','N','D'};
                     kfd::parse_kiwi_snd(short_snd.data(), short_snd.size()); }));
  assert(throws([] { const std::vector<uint8_t> wrong = {'B','A','D',0,0,0,0,0,0,0};
                     kfd::parse_kiwi_snd(wrong.data(), wrong.size()); }));
  assert(throws([] { kfd::KiwiAudioPacket odd; odd.payload = {0}; kfd::AdpcmState s;
                     kfd::decode_kiwi_audio(odd, s, true); }));
  assert(throws([] { kfd::KiwiAudioPacket stereo; stereo.flags = kfd::kSndFlagStereo;
                     kfd::AdpcmState s; kfd::decode_kiwi_audio(stereo, s, true); }));

  kfd::KiwiAudioPacket compressed;
  compressed.flags = kfd::kSndFlagCompressed;
  compressed.payload = {0x00, 0x11, 0x72, 0x8f};
  kfd::AdpcmState decoder;
  assert(throws([&] { kfd::AdpcmState missing;
                      kfd::decode_kiwi_audio(compressed, missing, false); }));
  const auto decoded = kfd::decode_kiwi_audio(compressed, decoder, true);
  kfd::AdpcmState encoder;
  const auto recompressed = kfd::encode_kiwi_audio(decoded, compressed.flags, encoder);
  assert(recompressed == compressed.payload);

  std::cout << "FreeDV Kiwi protocol tests passed\n";
  return 0;
}
