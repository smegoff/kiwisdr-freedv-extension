#include "freedv/kiwi_protocol.hpp"

#include <nlohmann/json.hpp>
#include <openssl/hmac.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace kfd {
namespace {

constexpr std::array<int, 16> kIndexAdjust = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
constexpr std::array<int, 89> kStepSize = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,    19,
    21,    23,    25,    28,    31,    34,    37,    41,    45,    50,    55,
    60,    66,    73,    80,    88,    97,    107,   118,   130,   143,   157,
    173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,
    494,   544,   598,   658,   724,   796,   876,   963,   1060,  1166,  1282,
    1411,  1552,  1707,  1878,  2066,  2272,  2499,  2749,  3024,  3327,  3660,
    4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845,  8630,  9493,  10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767};

int16_t decode_nibble(uint8_t code, AdpcmState& state) {
  const int step = kStepSize.at(static_cast<std::size_t>(state.index));
  int difference = step >> 3;
  if (code & 1) difference += step >> 2;
  if (code & 2) difference += step >> 1;
  if (code & 4) difference += step;
  if (code & 8) difference = -difference;
  state.previous = std::clamp(state.previous + difference, -32768, 32767);
  state.index = std::clamp(state.index + kIndexAdjust.at(code & 0x0f), 0, 88);
  return static_cast<int16_t>(state.previous);
}

uint8_t encode_nibble(int16_t sample, AdpcmState& state) {
  const int step = kStepSize.at(static_cast<std::size_t>(state.index));
  int difference = static_cast<int>(sample) - state.previous;
  uint8_t code = 0;
  if (difference < 0) {
    code = 8;
    difference = -difference;
  }
  int delta = step >> 3;
  if (difference >= step) {
    code |= 4;
    difference -= step;
    delta += step;
  }
  if (difference >= (step >> 1)) {
    code |= 2;
    difference -= step >> 1;
    delta += step >> 1;
  }
  if (difference >= (step >> 2)) {
    code |= 1;
    delta += step >> 2;
  }
  state.previous += (code & 8) ? -delta : delta;
  state.previous = std::clamp(state.previous, -32768, 32767);
  state.index = std::clamp(state.index + kIndexAdjust.at(code), 0, 88);
  return code;
}

uint32_t read_le32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

}  // namespace

KiwiAudioPacket parse_kiwi_snd(const void* data, std::size_t size) {
  if (size < 10) throw std::runtime_error("short Kiwi SND packet");
  const auto* p = static_cast<const uint8_t*>(data);
  if (p[0] != 'S' || p[1] != 'N' || p[2] != 'D')
    throw std::runtime_error("not a Kiwi SND packet");
  KiwiAudioPacket packet;
  packet.flags = p[3];
  packet.sequence = read_le32(p + 4);
  const uint16_t smeter = static_cast<uint16_t>((p[8] << 8) | p[9]);
  packet.rssi_dbm = static_cast<float>(smeter) / 10.0f - 127.0f;
  packet.payload.assign(p + 10, p + size);
  return packet;
}

std::vector<int16_t> decode_kiwi_audio(const KiwiAudioPacket& packet, AdpcmState& state,
                                       bool state_valid) {
  std::vector<int16_t> samples;
  if (packet.flags & kSndFlagStereo)
    throw std::runtime_error("stereo/IQ Kiwi audio is not accepted by FreeDV");
  if (packet.flags & kSndFlagCompressed) {
    if (!state_valid) throw std::runtime_error("ADPCM state has not been received");
    samples.reserve(packet.payload.size() * 2);
    for (uint8_t byte : packet.payload) {
      samples.push_back(decode_nibble(byte & 0x0f, state));
      samples.push_back(decode_nibble(byte >> 4, state));
    }
    return samples;
  }
  if (packet.payload.size() % 2) throw std::runtime_error("odd PCM payload size");
  samples.reserve(packet.payload.size() / 2);
  const bool little = (packet.flags & kSndFlagLittleEndian) != 0;
  for (std::size_t i = 0; i < packet.payload.size(); i += 2) {
    const uint16_t value = little
        ? static_cast<uint16_t>(packet.payload[i] | (packet.payload[i + 1] << 8))
        : static_cast<uint16_t>((packet.payload[i] << 8) | packet.payload[i + 1]);
    samples.push_back(static_cast<int16_t>(value));
  }
  return samples;
}

std::vector<uint8_t> encode_kiwi_audio(const std::vector<int16_t>& samples, uint8_t flags,
                                       AdpcmState& state) {
  std::vector<uint8_t> output;
  if (flags & kSndFlagCompressed) {
    output.reserve((samples.size() + 1) / 2);
    for (std::size_t i = 0; i < samples.size(); i += 2) {
      const uint8_t low = encode_nibble(samples[i], state);
      const uint8_t high = encode_nibble(samples[std::min(i + 1, samples.size() - 1)], state);
      output.push_back(static_cast<uint8_t>(low | (high << 4)));
    }
    return output;
  }
  const bool little = (flags & kSndFlagLittleEndian) != 0;
  output.reserve(samples.size() * 2);
  for (int16_t sample : samples) {
    const auto value = static_cast<uint16_t>(sample);
    if (little) {
      output.push_back(static_cast<uint8_t>(value));
      output.push_back(static_cast<uint8_t>(value >> 8));
    } else {
      output.push_back(static_cast<uint8_t>(value >> 8));
      output.push_back(static_cast<uint8_t>(value));
    }
  }
  return output;
}

std::string hmac_sha256_hex(const std::string& key, const std::string& message) {
  unsigned int length = 0;
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  if (!HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
            reinterpret_cast<const unsigned char*>(message.data()), message.size(), digest.data(),
            &length) || length != 32)
    throw std::runtime_error("HMAC-SHA256 failed");
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (unsigned i = 0; i < length; ++i) out << std::setw(2) << static_cast<unsigned>(digest[i]);
  return out.str();
}

std::string make_poll_command(const std::string& secret, int64_t unix_seconds,
                              const std::string& nonce) {
  const std::string signed_value = "2|" + std::to_string(unix_seconds) + "|" + nonce;
  return "SET freedv_poll=2," + std::to_string(unix_seconds) + "," + nonce + "," +
         hmac_sha256_hex(secret, signed_value);
}

std::string url_encode(const std::string& value) {
  std::ostringstream out;
  out << std::uppercase << std::hex << std::setfill('0');
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out << c;
    else out << '%' << std::setw(2) << static_cast<unsigned>(c);
  }
  return out.str();
}

std::string url_decode(const std::string& value) {
  std::string out;
  out.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const int high = hex_value(value[i + 1]);
      const int low = hex_value(value[i + 2]);
      if (high < 0 || low < 0) throw std::runtime_error("invalid URL escape");
      out.push_back(static_cast<char>((high << 4) | low));
      i += 2;
    } else if (value[i] == '+') out.push_back(' ');
    else out.push_back(value[i]);
  }
  return out;
}

bool parse_message_pair(const std::string& message, const std::string& name,
                        std::string& value) {
  if (name.empty()) return false;
  const std::string key = name + "=";
  std::size_t search_from = 0;
  std::size_t position = std::string::npos;
  while ((position = message.find(key, search_from)) != std::string::npos) {
    if (position == 0 ||
        std::isspace(static_cast<unsigned char>(message[position - 1]))) break;
    search_from = position + key.size();
  }
  if (position == std::string::npos) return false;
  const auto begin = position + key.size();
  const auto end = message.find(' ', begin);
  value = message.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
  return true;
}

DecoderJob parse_decoder_job(const std::string& encoded_json) {
  const auto value = nlohmann::json::parse(url_decode(encoded_json));
  DecoderJob job;
  job.protocol = value.at("protocol").get<uint32_t>();
  job.generation = value.at("generation").get<uint64_t>();
  job.running = value.at("running").get<bool>();
  if (job.protocol != 2) throw std::runtime_error("unsupported job protocol");
  if (job.running) {
    job.rx_channel = value.at("rx_chan").get<int>();
    job.mode = value.at("mode").get<std::string>();
    job.input_rate = value.at("input_rate").get<uint32_t>();
    job.frequency_hz = value.at("frequency_hz").get<uint64_t>();
    job.test = value.value("test", false);
    const auto reporter = value.value("reporter", nlohmann::json::object());
    job.reporter_enabled = reporter.value("enabled", false);
    job.reporter_callsign = reporter.value("callsign", std::string{});
    job.reporter_grid = reporter.value("grid", std::string{});
    job.reporter_message = reporter.value("message", std::string{});
    static const std::array<const char*, 8> modes = {
        "1600", "700C", "700D", "700E", "2400A", "2400B", "800XA", "RADEV1"};
    const bool mode_valid = std::any_of(modes.begin(), modes.end(), [&](const char* mode) {
      return job.mode == mode;
    });
    if (job.rx_channel < 0 || job.rx_channel > 31 || !mode_valid ||
        job.input_rate < 8000 || job.input_rate > 192000)
      throw std::runtime_error("invalid decoder job");
  }
  return job;
}

JobDisposition classify_job(const DecoderJob& current, const DecoderJob& incoming) {
  if (incoming.generation < current.generation) return JobDisposition::stale;
  const bool equal = incoming.protocol == current.protocol &&
                     incoming.generation == current.generation &&
                     incoming.running == current.running &&
                     incoming.rx_channel == current.rx_channel &&
                     incoming.mode == current.mode &&
                     incoming.input_rate == current.input_rate &&
                     incoming.frequency_hz == current.frequency_hz &&
                     incoming.test == current.test &&
                     incoming.reporter_enabled == current.reporter_enabled &&
                     incoming.reporter_callsign == current.reporter_callsign &&
                     incoming.reporter_grid == current.reporter_grid &&
                     incoming.reporter_message == current.reporter_message;
  if (equal) return JobDisposition::unchanged;
  if (incoming.generation == current.generation) return JobDisposition::conflict;
  return JobDisposition::changed;
}

}  // namespace kfd
