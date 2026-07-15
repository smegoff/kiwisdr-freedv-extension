#include "freedv/backend.hpp"
#include "freedv/kiwi_protocol.hpp"
#include "freedv/resampler.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using udp = asio::ip::udp;
using json = nlohmann::json;

namespace {

constexpr char kRelease[] = "0.1.16";
constexpr uint64_t kStalledMainLoopSeconds = 15;

struct Metrics {
  std::atomic<uint64_t> kiwi_connected{0};
  std::atomic<uint64_t> camper_connected{0};
  std::atomic<uint64_t> sessions{0};
  std::atomic<uint64_t> snd_frames{0};
  std::atomic<uint64_t> decoded_frames{0};
  std::atomic<uint64_t> dropped_frames{0};
  std::atomic<uint64_t> reconnects{0};
  std::atomic<uint64_t> auth_failures{0};
  std::atomic<uint64_t> auth_successes{0};
  std::atomic<uint64_t> malformed_jobs{0};
  std::atomic<uint64_t> stale_jobs{0};
  std::atomic<uint64_t> decode_nanoseconds{0};
  std::atomic<uint64_t> generation{0};
  std::atomic<uint64_t> synced{0};
  std::atomic<uint64_t> status_updates{0};
  std::atomic<uint64_t> main_loop_heartbeat{0};
};

Metrics metrics;

uint64_t monotonic_seconds() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

void mark_main_loop_progress() { metrics.main_loop_heartbeat = monotonic_seconds(); }

uint64_t main_loop_age_seconds() {
  const uint64_t heartbeat = metrics.main_loop_heartbeat.load();
  const uint64_t now = monotonic_seconds();
  return heartbeat == 0 || heartbeat > now ? UINT64_MAX : now - heartbeat;
}

std::string env_or(const char* name, const char* fallback) {
  const char* value = std::getenv(name);
  return value ? value : fallback;
}

std::string reporter_state() {
  std::ifstream input(env_or("FREEDV_REPORTER_STATE_FILE", "/tmp/freedv-reporter-state"));
  std::string state;
  if (input) std::getline(input, state);
  if (state == "connecting" || state == "online" || state == "rate-limited" ||
      state == "error" || state == "disabled") return state;
  return "disabled";
}

void reporter_send(const json& event) {
  try {
    asio::io_context ioc;
    udp::socket socket(ioc, udp::v4());
    const auto payload = event.dump();
    socket.send_to(asio::buffer(payload), {asio::ip::make_address("127.0.0.1"), 8075});
  } catch (...) {
    // Reporter is best-effort and may never interfere with decoding.
  }
}

std::string metrics_text() {
  std::ostringstream out;
  out << "freedv_build_info{version=\"" << kRelease << "\"} 1\n"
      << "freedv_kiwi_connected " << metrics.kiwi_connected.load() << '\n'
      << "freedv_camper_connected " << metrics.camper_connected.load() << '\n'
      << "freedv_sessions " << metrics.sessions.load() << '\n'
      << "freedv_snd_frames_total " << metrics.snd_frames.load() << '\n'
      << "freedv_decoded_frames_total " << metrics.decoded_frames.load() << '\n'
      << "freedv_dropped_frames_total " << metrics.dropped_frames.load() << '\n'
      << "freedv_reconnects_total " << metrics.reconnects.load() << '\n'
      << "freedv_auth_failures_total " << metrics.auth_failures.load() << '\n'
      << "freedv_auth_successes_total " << metrics.auth_successes.load() << '\n'
      << "freedv_malformed_jobs_total " << metrics.malformed_jobs.load() << '\n'
      << "freedv_stale_jobs_total " << metrics.stale_jobs.load() << '\n'
      << "freedv_decode_seconds_total " << metrics.decode_nanoseconds.load() / 1.0e9 << '\n'
      << "freedv_generation " << metrics.generation.load() << '\n'
      << "freedv_sync " << metrics.synced.load() << '\n'
      << "freedv_status_updates_total " << metrics.status_updates.load() << '\n'
      << "freedv_main_loop_age_seconds " << main_loop_age_seconds() << '\n'
      << "freedv_audio_queue_milliseconds 0\n"
      << "freedv_reporter_state{state=\"" << reporter_state() << "\"} 1\n";
  return out.str();
}

void health_server(unsigned short port) {
  try {
    asio::io_context ioc;
    tcp::acceptor acceptor(ioc, {asio::ip::make_address("127.0.0.1"), port});
    for (;;) {
      tcp::socket socket(ioc);
      acceptor.accept(socket);
      beast::flat_buffer buffer;
      http::request<http::string_body> request;
      http::read(socket, buffer, request);
      const bool is_metrics = request.target() == "/metrics";
      const bool is_health = request.target() == "/healthz" || request.target() == "/";
      const bool live = main_loop_age_seconds() <= kStalledMainLoopSeconds;
      const bool healthy = live && metrics.kiwi_connected.load() != 0;
      http::response<http::string_body> response{
          is_metrics ? http::status::ok :
          is_health ? (healthy ? http::status::ok : http::status::service_unavailable) :
                      http::status::not_found,
          request.version()};
      response.set(http::field::server, std::string("freedv-decoder/") + kRelease);
      response.set(http::field::content_type,
                   is_metrics ? "text/plain; version=0.0.4" : "application/json");
      if (is_metrics) response.body() = metrics_text();
      else if (is_health) {
        response.body() = json{{"status", healthy ? "ok" : "degraded"},
                               {"release", kRelease},
                               {"kiwi_connected", metrics.kiwi_connected.load() != 0},
                               {"camper_connected", metrics.camper_connected.load() != 0},
                               {"sessions", metrics.sessions.load()},
                               {"main_loop_age_seconds", main_loop_age_seconds()},
                               {"reporter", reporter_state()}}.dump() + "\n";
      } else response.body() = "{\"status\":\"not-found\"}\n";
      response.keep_alive(false);
      response.prepare_payload();
      http::write(socket, response);
      beast::error_code ignored;
      socket.shutdown(tcp::socket::shutdown_send, ignored);
    }
  } catch (const std::exception& error) {
    std::cerr << "health server: " << error.what() << '\n';
    std::exit(3);
  }
}

void systemd_notify(const char* state) {
  const char* path = std::getenv("NOTIFY_SOCKET");
  if (!path || !*path) return;
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  const std::size_t length = std::min(std::strlen(path), sizeof(address.sun_path) - 1);
  std::memcpy(address.sun_path, path, length);
  if (address.sun_path[0] == '@') address.sun_path[0] = '\0';
  const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
  if (fd < 0) return;
  const socklen_t address_length = static_cast<socklen_t>(
      offsetof(sockaddr_un, sun_path) + length + 1);
  (void) sendto(fd, state, std::strlen(state), MSG_NOSIGNAL,
                reinterpret_cast<const sockaddr*>(&address), address_length);
  close(fd);
}

void systemd_watchdog() {
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    const uint64_t age = main_loop_age_seconds();
    if (age > kStalledMainLoopSeconds) {
      std::cerr << "main loop stalled for " << age
                << " seconds; exiting for systemd recovery\n";
      _exit(4);
    }
    systemd_notify("WATCHDOG=1");
  }
}

std::string random_nonce() {
  static std::mt19937_64 engine(std::random_device{}());
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << engine();
  return out.str();
}

std::string trim(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    value.pop_back();
  return value;
}

class KiwiCamper {
 public:
  KiwiCamper(std::string host, std::string port, std::string password, std::string secret,
             bool rade_enabled)
      : host_(std::move(host)), port_(std::move(port)), password_(std::move(password)),
        secret_(std::move(secret)), rade_enabled_(rade_enabled), resolver_(ioc_), ws_(ioc_) {}

  void run() {
    mark_main_loop_progress();
    const auto endpoints = resolver_.resolve(host_, port_);
    asio::connect(ws_.next_layer(), endpoints);
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    ws_.read_message_max(1024 * 1024);
    const auto stamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    ws_.handshake(host_ + ":" + port_, "/" + std::to_string(stamp) + "/SND?camp");
    mark_main_loop_progress();
    metrics.kiwi_connected = 1;
    send_text("SET auth t=kiwi p=" + password_);

    for (;;) {
      beast::flat_buffer buffer;
      ws_.read(buffer);
      mark_main_loop_progress();
      const std::string frame = beast::buffers_to_string(buffer.data());
      if (frame.size() < 3) continue;
      const std::string tag = frame.substr(0, 3);
      if (tag == "MSG") process_message(trim(frame.substr(3)));
      else if (tag == "SND") process_audio(frame);
      maybe_poll();
      maybe_keepalive();
    }
  }

  void shutdown() {
    if (job_.running) reporter_send({{"type", "stop"}, {"session_id", job_.generation}});
    metrics.kiwi_connected = 0;
    metrics.camper_connected = 0;
    metrics.sessions = 0;
    metrics.synced = 0;
  }

 private:
  void send_text(const std::string& message) {
    ws_.text(true);
    ws_.write(asio::buffer(message));
  }

  void send_binary(const std::vector<uint8_t>& message) {
    ws_.binary(true);
    ws_.write(asio::buffer(message));
  }

  void maybe_poll(bool force = false) {
    if (!control_ready_) return;
    const auto now = std::chrono::steady_clock::now();
    if (!force && now < next_poll_) return;
    const auto unix_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    send_text(kfd::make_poll_command(secret_, unix_seconds, random_nonce()));
    next_poll_ = now + (job_.test && !job_.test_ready ?
        std::chrono::milliseconds(100) : std::chrono::milliseconds(1000));
  }

  void maybe_keepalive() {
    const auto now = std::chrono::steady_clock::now();
    if (now < next_keepalive_) return;
    send_text("SET keepalive");
    next_keepalive_ = now + std::chrono::seconds(5);
  }

  void process_message(const std::string& message) {
    std::string auth_value;
    if (message.find("badp=1") != std::string::npos ||
        (kfd::parse_message_pair(message, "freedv_auth", auth_value) && auth_value != "ok")) {
      metrics.auth_failures++;
      throw std::runtime_error("Kiwi authentication rejected");
    }
    if (message == "monitor" || message.find("monitor ") == 0) {
      control_ready_ = true;
      maybe_poll(true);
      return;
    }
    if (message.find("camp_disconnect") != std::string::npos) {
      camped_channel_ = -1;
      metrics.camper_connected = 0;
      reset_decoder();
      return;
    }

    std::string value;
    if (kfd::parse_message_pair(message, "freedv_job", value)) {
      try {
        apply_job(kfd::parse_decoder_job(value));
        metrics.auth_successes++;
      } catch (...) {
        metrics.malformed_jobs++;
        throw;
      }
      if (!job_.running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        maybe_poll(true);
      }
      return;
    }
    if (kfd::parse_message_pair(message, "camp", value)) {
      const auto comma = value.find(',');
      if (comma != std::string::npos && value.substr(0, comma) == "1") {
        camped_channel_ = std::stoi(value.substr(comma + 1));
        metrics.camper_connected = 1;
        // The pre-camp status cannot be routed to a receiver. Announce the
        // running camper immediately so Kiwi can arm a reference test before
        // this process is allowed to consume any test audio.
        send_status({});
        if (job_.test && !job_.test_ready) maybe_poll(true);
      }
      return;
    }
    if (kfd::parse_message_pair(message, "audio_rate", value)) {
      audio_rate_ = static_cast<uint32_t>(std::stoul(value));
      return;
    }
    if (kfd::parse_message_pair(message, "sample_rate", value)) {
      const auto rate = static_cast<uint32_t>(std::llround(std::stod(value)));
      if (rate >= 8000 && rate <= 192000) input_rate_ = rate;
      return;
    }
    if (kfd::parse_message_pair(message, "audio_adpcm_state", value)) {
      const auto comma = value.find(',');
      if (comma != std::string::npos) {
        input_adpcm_.index = std::clamp(std::stoi(value.substr(0, comma)), 0, 88);
        input_adpcm_.previous = std::clamp(std::stoi(value.substr(comma + 1)), -32768, 32767);
        input_adpcm_valid_ = true;
      }
    }
  }

  void apply_job(const kfd::DecoderJob& incoming) {
    if (job_.running && incoming.running && job_.generation == incoming.generation &&
        job_.test && incoming.test && !job_.test_ready && incoming.test_ready &&
        job_.rx_channel == incoming.rx_channel && job_.mode == incoming.mode &&
        job_.input_rate == incoming.input_rate && job_.frequency_hz == incoming.frequency_hz) {
      job_.test_ready = true;
      backend_->reset();
      input_resampler_.reset();
      output_resampler_.reset();
      expected_sequence_valid_ = false;
      return_adpcm_valid_ = false;
      std::cerr << "test reference armed: generation=" << job_.generation << '\n';
      return;
    }
    const auto disposition = kfd::classify_job(job_, incoming);
    if (disposition == kfd::JobDisposition::stale) {
      metrics.stale_jobs++;
      return;
    }
    if (disposition == kfd::JobDisposition::unchanged) return;
    if (disposition == kfd::JobDisposition::conflict)
      throw std::runtime_error("conflicting job state for current generation");
    if (job_.running) reporter_send({{"type", "stop"}, {"session_id", job_.generation}});
    if (!incoming.running) {
      if (camped_channel_ >= 0) send_text("SET MON_CAMP=-1");
      camped_channel_ = -1;
      metrics.camper_connected = 0;
      reset_decoder();
      job_ = incoming;
      metrics.generation = job_.generation;
      return;
    }

    job_ = incoming;
    metrics.generation = job_.generation;
    input_rate_ = job_.input_rate;
    backend_ = job_.mode == "RADEV1" ? kfd::make_rade_backend(rade_enabled_)
                                      : kfd::make_codec2_backend(job_.mode);
    metrics.sessions = 1;
    expected_sequence_valid_ = false;
    input_resampler_.reset();
    output_resampler_.reset();
    return_adpcm_valid_ = false;
    first_audio_packet_ = true;
    job_decoded_frames_ = 0;
    std::cerr << "job start: generation=" << job_.generation
              << " mode=" << job_.mode << " input_rate=" << job_.input_rate
              << " test=" << (job_.test ? 1 : 0) << '\n';
    reporter_send({{"type", "start"}, {"session_id", job_.generation},
                   {"mode", job_.mode}, {"frequency", job_.frequency_hz},
                   {"sync", false}, {"enabled", job_.reporter_enabled && !job_.test},
                   {"station_callsign", job_.reporter_callsign},
                   {"grid_square", job_.reporter_grid},
                   {"message", job_.reporter_message}});
    if (camped_channel_ != job_.rx_channel)
      send_text("SET MON_CAMP=" + std::to_string(job_.rx_channel));
    send_status({});
  }

  void reset_decoder() {
    if (backend_) backend_->reset();
    backend_.reset();
    input_resampler_.reset();
    output_resampler_.reset();
    input_adpcm_valid_ = false;
    return_adpcm_valid_ = false;
    expected_sequence_valid_ = false;
    first_audio_packet_ = true;
    job_decoded_frames_ = 0;
    metrics.sessions = 0;
    metrics.synced = 0;
  }

  void process_audio(const std::string& frame) {
    if (!backend_ || !job_.running || camped_channel_ != job_.rx_channel) return;
    const auto packet = kfd::parse_kiwi_snd(frame.data(), frame.size());
    metrics.snd_frames++;
    if (job_.test && !job_.test_ready) {
      // Never decode live receiver noise as part of the deterministic test.
      // The next authenticated job poll carries Kiwi's test_ready handshake.
      maybe_poll();
      return;
    }
    if (expected_sequence_valid_ && packet.sequence != expected_sequence_) {
      metrics.dropped_frames += static_cast<uint32_t>(packet.sequence - expected_sequence_);
      backend_->reset();
      input_resampler_.reset();
      return_adpcm_valid_ = false;
    }
    expected_sequence_ = packet.sequence + 1;
    expected_sequence_valid_ = true;

    const bool compressed = (packet.flags & kfd::kSndFlagCompressed) != 0;
    const auto input = kfd::decode_kiwi_audio(packet, input_adpcm_,
                                               !compressed || input_adpcm_valid_);
    if (compressed) input_adpcm_valid_ = true;
    if (first_audio_packet_) {
      std::cerr << "audio format: flags=" << static_cast<unsigned>(packet.flags)
                << " payload_bytes=" << packet.payload.size()
                << " samples=" << input.size() << " input_rate=" << input_rate_
                << " audio_rate=" << audio_rate_ << '\n';
      first_audio_packet_ = false;
    }
    const auto modem = input_resampler_.process(input, input_rate_, backend_->modem_sample_rate());
    const auto started = std::chrono::steady_clock::now();
    auto decoded = backend_->push(modem.data(), modem.size());
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started).count();
    metrics.decode_nanoseconds += static_cast<uint64_t>(elapsed);
    if (elapsed > 500000000) {
      metrics.dropped_frames++;
      backend_->reset();
      input_resampler_.reset();
      output_resampler_.reset();
      return_adpcm_valid_ = false;
      return;
    }
    metrics.synced = decoded.status.synced ? 1 : 0;

    // Never return modem noise or partial speech while the FreeDV modem is
    // unsynchronized. The Kiwi-side return-audio gate supplies silence.
    if (decoded.status.synced && !decoded.pcm.empty()) {
      const auto speech = output_resampler_.process(decoded.pcm, decoded.sample_rate, audio_rate_);
      if (compressed && !return_adpcm_valid_) {
        return_adpcm_ = input_adpcm_;
        return_adpcm_valid_ = true;
      }
      auto payload = kfd::encode_kiwi_audio(speech, packet.flags, return_adpcm_);
      static constexpr char prefix[] = "SET rev_bin=";
      std::vector<uint8_t> message(prefix, prefix + sizeof(prefix) - 1);
      message.insert(message.end(), payload.begin(), payload.end());
      send_binary(message);
      metrics.decoded_frames++;
      job_decoded_frames_++;
    } else if (!decoded.status.synced) {
      output_resampler_.reset();
      return_adpcm_valid_ = false;
    }

    const auto now = std::chrono::steady_clock::now();
    if (now >= next_status_) {
      send_status(decoded.status);
      if (!job_.test) {
        reporter_send({{"type", "status"}, {"session_id", job_.generation},
                       {"mode", job_.mode}, {"frequency", job_.frequency_hz},
                       {"sync", decoded.status.synced}, {"snr", decoded.status.snr_db},
                       {"callsign", decoded.status.callsign}});
      }
      next_status_ = now + std::chrono::milliseconds(250);
    }
  }

  void send_status(const kfd::DecodeStatus& status) {
    const json value{{"type", "status"},
                     {"release", kRelease},
                     {"generation", job_.generation},
                     {"state", camped_channel_ == job_.rx_channel ? "running" : "connecting"},
                     {"backend", backend_ ? backend_->name() : "none"},
                     {"test", job_.test},
                     {"sync", status.synced},
                     {"snr", status.snr_db},
                     {"frequency_offset", status.frequency_offset_hz},
                     {"callsign", status.callsign},
                     {"text", status.text},
                     {"dropped", metrics.dropped_frames.load()},
                     {"decoded_frames", job_decoded_frames_},
                     {"reporter", reporter_state()},
                     {"error", status.error}};
    send_text("SET rev_txt=" + std::to_string(job_.generation) + "," +
              kfd::url_encode(value.dump()));
    metrics.status_updates++;
  }

  std::string host_;
  std::string port_;
  std::string password_;
  std::string secret_;
  bool rade_enabled_ = false;
  asio::io_context ioc_;
  tcp::resolver resolver_;
  websocket::stream<tcp::socket> ws_;
  bool control_ready_ = false;
  int camped_channel_ = -1;
  kfd::DecoderJob job_;
  std::unique_ptr<kfd::DecoderBackend> backend_;
  uint32_t input_rate_ = 12000;
  uint32_t audio_rate_ = 12000;
  uint32_t expected_sequence_ = 0;
  bool expected_sequence_valid_ = false;
  kfd::AdpcmState input_adpcm_;
  kfd::AdpcmState return_adpcm_;
  bool input_adpcm_valid_ = false;
  bool return_adpcm_valid_ = false;
  bool first_audio_packet_ = true;
  uint64_t job_decoded_frames_ = 0;
  kfd::StreamingResampler input_resampler_;
  kfd::StreamingResampler output_resampler_;
  std::chrono::steady_clock::time_point next_poll_{};
  std::chrono::steady_clock::time_point next_keepalive_{};
  std::chrono::steady_clock::time_point next_status_{};
};

}  // namespace

int main() {
  const char* secret = std::getenv("FREEDV_SHARED_SECRET");
  if (!secret || std::string(secret).size() < 32) {
    std::cerr << "FREEDV_SHARED_SECRET must contain at least 32 characters\n";
    return 2;
  }
  mark_main_loop_progress();
  const auto health_port = static_cast<unsigned short>(
      std::stoul(env_or("FREEDV_HEALTH_PORT", "8074")));
  std::thread(health_server, health_port).detach();
  std::thread(systemd_watchdog).detach();
  systemd_notify("READY=1");

  unsigned backoff = 1;
  std::mt19937 jitter(std::random_device{}());
  for (;;) {
    try {
      mark_main_loop_progress();
      KiwiCamper camper(env_or("FREEDV_KIWI_HOST", "192.168.10.238"),
                        env_or("FREEDV_KIWI_PORT", "8073"),
                        env_or("FREEDV_KIWI_PASSWORD", ""), secret,
                        env_or("FREEDV_ENABLE_RADE", "0") == "1");
      std::cout << "connecting to Kiwi camper transport\n";
      camper.run();
      camper.shutdown();
      backoff = 1;
    } catch (const std::exception& error) {
      mark_main_loop_progress();
      metrics.kiwi_connected = 0;
      metrics.camper_connected = 0;
      metrics.sessions = 0;
      metrics.synced = 0;
      metrics.reconnects++;
      std::cerr << "camper transport: " << error.what() << '\n';
      const unsigned delay_ms = backoff * 1000 + (jitter() % 500);
      unsigned remaining_ms = delay_ms;
      while (remaining_ms) {
        const unsigned slice_ms = std::min(remaining_ms, 1000u);
        std::this_thread::sleep_for(std::chrono::milliseconds(slice_ms));
        remaining_ms -= slice_ms;
        mark_main_loop_progress();
      }
      backoff = std::min(backoff * 2, 30u);
    }
  }
}
