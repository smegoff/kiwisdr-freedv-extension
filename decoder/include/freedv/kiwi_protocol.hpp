#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace kfd {

constexpr uint8_t kSndFlagStereo = 0x08;
constexpr uint8_t kSndFlagCompressed = 0x10;
constexpr uint8_t kSndFlagLittleEndian = 0x80;

struct AdpcmState {
  int index = 0;
  int previous = 0;
};

struct KiwiAudioPacket {
  uint8_t flags = 0;
  uint32_t sequence = 0;
  float rssi_dbm = -127.0f;
  std::vector<uint8_t> payload;
};

struct DecoderJob {
  uint32_t protocol = 2;
  uint64_t generation = 0;
  bool running = false;
  int rx_channel = -1;
  std::string mode;
  uint32_t input_rate = 12000;
  uint64_t frequency_hz = 0;
  bool test = false;
  bool reporter_enabled = false;
  std::string reporter_callsign;
  std::string reporter_grid;
  std::string reporter_message;
};

enum class JobDisposition { stale, unchanged, changed, conflict };

KiwiAudioPacket parse_kiwi_snd(const void* data, std::size_t size);
std::vector<int16_t> decode_kiwi_audio(const KiwiAudioPacket& packet, AdpcmState& state,
                                       bool state_valid);
std::vector<uint8_t> encode_kiwi_audio(const std::vector<int16_t>& samples, uint8_t flags,
                                       AdpcmState& state);

std::string hmac_sha256_hex(const std::string& key, const std::string& message);
std::string make_poll_command(const std::string& secret, int64_t unix_seconds,
                              const std::string& nonce);
std::string url_encode(const std::string& value);
std::string url_decode(const std::string& value);
bool parse_message_pair(const std::string& message, const std::string& name,
                        std::string& value);
DecoderJob parse_decoder_job(const std::string& encoded_json);
JobDisposition classify_job(const DecoderJob& current, const DecoderJob& incoming);

}  // namespace kfd
