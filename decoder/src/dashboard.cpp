#include "freedv/dashboard.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <fftw3.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cctype>
#include <deque>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using json = nlohmann::json;

namespace kfd {
namespace {

constexpr std::size_t kFftSize = 1024;
constexpr std::size_t kBins = kFftSize / 2;
constexpr std::size_t kAudioRingSize = 32768;
constexpr char kCookieName[] = "freedv_dashboard";

std::string env_or(const char* name, const char* fallback) {
  const char* value = std::getenv(name);
  return value ? value : fallback;
}

bool env_bool(const char* name, bool fallback) {
  const std::string value = env_or(name, fallback ? "1" : "0");
  return value == "1" || value == "true" || value == "yes" || value == "on";
}

unsigned env_unsigned(const char* name, unsigned fallback, unsigned low, unsigned high) {
  try {
    return std::clamp(static_cast<unsigned>(std::stoul(env_or(name, ""))), low, high);
  } catch (...) {
    return fallback;
  }
}

std::string trim(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    value.pop_back();
  return value;
}

std::string read_file(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("unable to read dashboard file: " + path);
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}

std::string random_hex(std::size_t bytes) {
  std::vector<unsigned char> raw(bytes);
  if (RAND_bytes(raw.data(), static_cast<int>(raw.size())) != 1)
    throw std::runtime_error("unable to generate dashboard session");
  std::ostringstream output;
  output << std::hex << std::setfill('0');
  for (const auto value : raw) output << std::setw(2) << static_cast<unsigned>(value);
  return output.str();
}

void put_u16(std::vector<uint8_t>& output, std::size_t offset, uint16_t value) {
  output[offset] = static_cast<uint8_t>(value & 0xff);
  output[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xff);
}

void put_u32(std::vector<uint8_t>& output, std::size_t offset, uint32_t value) {
  for (unsigned i = 0; i < 4; i++) output[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

template <typename T>
void json_optional(json& output, const char* name, const std::optional<T>& value) {
  if (value) output[name] = *value;
  else output[name] = nullptr;
}

std::string cookie_value(const http::request<http::string_body>& request) {
  const auto it = request.find(http::field::cookie);
  if (it == request.end()) return {};
  std::string cookies = it->value().to_string();
  std::size_t begin = 0;
  while (begin < cookies.size()) {
    const auto end = cookies.find(';', begin);
    const auto part = trim(cookies.substr(begin, end == std::string::npos ? end : end - begin));
    const auto equals = part.find('=');
    if (equals != std::string::npos && part.substr(0, equals) == kCookieName)
      return part.substr(equals + 1);
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return {};
}

http::response<http::string_body> response(http::status status, unsigned version,
                                           const std::string& content_type,
                                           std::string body) {
  http::response<http::string_body> result{status, version};
  result.set(http::field::server, "freedv-dashboard/0.1.20");
  result.set(http::field::content_type, content_type);
  result.set(http::field::cache_control, "no-store");
  result.set("X-Content-Type-Options", "nosniff");
  result.set("X-Frame-Options", "DENY");
  result.set("Content-Security-Policy",
             "default-src 'self'; script-src 'self'; style-src 'self'; connect-src 'self'");
  result.keep_alive(false);
  result.body() = std::move(body);
  result.prepare_payload();
  return result;
}

void write_response(tcp::socket& socket, http::response<http::string_body> result) {
  http::write(socket, result);
}

void lower_current_thread_priority() {
#ifdef __linux__
  (void)setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), 5);
#endif
}

class FftEncoder {
 public:
  FftEncoder() {
    constexpr float pi = 3.14159265358979323846f;
    for (std::size_t i = 0; i < kFftSize; i++)
      window_[i] = 0.5f - 0.5f * std::cos(2.0f * pi * i / (kFftSize - 1));
    plan_ = fftwf_plan_dft_r2c_1d(static_cast<int>(kFftSize), input_.data(),
                                   spectrum_.data(), FFTW_ESTIMATE);
    if (!plan_) throw std::runtime_error("unable to create dashboard FFT plan");
  }

  ~FftEncoder() { fftwf_destroy_plan(plan_); }
  FftEncoder(const FftEncoder&) = delete;
  FftEncoder& operator=(const FftEncoder&) = delete;

  std::vector<uint8_t> encode(const int16_t* samples, std::size_t count,
                              uint32_t sample_rate, uint32_t sequence,
                              uint8_t flags) {
    if (!samples || count < kFftSize || sample_rate == 0)
      throw std::invalid_argument("dashboard FFT requires 1024 samples and a sample rate");
    const int16_t* source = samples + count - kFftSize;
    for (std::size_t i = 0; i < kFftSize; i++)
      input_[i] = static_cast<float>(source[i]) * window_[i];
    fftwf_execute(plan_);

    std::vector<uint8_t> frame(16 + kBins);
    frame[0] = 'F'; frame[1] = 'D'; frame[2] = 'W'; frame[3] = 'F';
    frame[4] = 1;
    frame[5] = flags;
    put_u16(frame, 6, static_cast<uint16_t>(kBins));
    put_u32(frame, 8, sample_rate);
    put_u32(frame, 12, sequence);
    const float normalization = 2.0f / (kFftSize * 32768.0f * 0.5f);
    for (std::size_t i = 0; i < kBins; i++) {
      const float magnitude = std::hypot(spectrum_[i][0], spectrum_[i][1]) * normalization;
      const float db = std::clamp(20.0f * std::log10(std::max(magnitude, 1.0e-6f)),
                                  -120.0f, 0.0f);
      frame[16 + i] = static_cast<uint8_t>(
          std::lround((db + 120.0f) * 255.0f / 120.0f));
    }
    return frame;
  }

 private:
  std::array<float, kFftSize> window_{};
  std::array<float, kFftSize> input_{};
  std::array<fftwf_complex, kBins + 1> spectrum_{};
  fftwf_plan plan_ = nullptr;
};

}  // namespace

DashboardConfig dashboard_config_from_environment() {
  DashboardConfig config;
  config.enabled = env_bool("FREEDV_DASHBOARD_ENABLED", true);
  config.bind_address = env_or("FREEDV_DASHBOARD_BIND", "0.0.0.0");
  config.port = static_cast<uint16_t>(
      env_unsigned("FREEDV_DASHBOARD_PORT", 8076, 1024, 65535));
  config.token_file = env_or("FREEDV_DASHBOARD_TOKEN_FILE",
                             "/etc/freedv-decoder/dashboard.token");
  config.asset_directory = env_or("FREEDV_DASHBOARD_ASSET_DIR",
                                   "/usr/local/share/freedv-dashboard/current");
  config.history_seconds = env_unsigned("FREEDV_DASHBOARD_HISTORY_SECONDS", 600, 60, 3600);
  config.waterfall_fps = env_unsigned("FREEDV_DASHBOARD_WATERFALL_FPS", 10, 1, 10);
  return config;
}

bool dashboard_constant_time_equal(const std::string& left, const std::string& right) {
  return left.size() == right.size() && !left.empty() &&
         CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

std::vector<uint8_t> dashboard_fft_frame(const int16_t* samples, std::size_t count,
                                         uint32_t sample_rate, uint32_t sequence,
                                         uint8_t flags) {
  FftEncoder encoder;
  return encoder.encode(samples, count, sample_rate, sequence, flags);
}

struct Dashboard::Impl : std::enable_shared_from_this<Dashboard::Impl> {
  explicit Impl(DashboardConfig input, StatusProvider status)
      : config(std::move(input)), provider(std::move(status)), audio(kAudioRingSize) {}

  DashboardConfig config;
  StatusProvider provider;
  std::atomic<bool> stopping{false};
  std::atomic<bool> started{false};
  std::atomic<uint64_t> audio_write{0};
  std::atomic<uint64_t> audio_read{0};
  std::atomic<uint64_t> spectrum_drops{0};
  std::atomic<uint64_t> waterfall_frames{0};
  std::atomic<uint64_t> dashboard_clients{0};
  std::atomic<uint64_t> login_failures{0};
  std::vector<int16_t> audio;
  std::atomic<uint32_t> audio_rate{12000};
  std::thread server_thread;
  std::thread fft_thread;
  std::thread history_thread;
  asio::io_context server_ioc;
  std::shared_ptr<tcp::acceptor> acceptor;
  std::mutex frame_mutex;
  std::vector<uint8_t> latest_frame;
  uint32_t latest_sequence = 0;
  std::mutex state_mutex;
  json session = {{"active", false}};
  std::deque<json> history;
  std::mutex auth_mutex;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> sessions;
  std::unordered_map<std::string, std::deque<std::chrono::steady_clock::time_point>> failures;
  std::string access_token;
  std::string index_html;
  std::string app_js;
  std::string styles_css;

  json status_json() {
    json output = provider ? provider() : json::object();
    {
      std::lock_guard<std::mutex> lock(state_mutex);
      output["session"] = session;
    }
    const auto queued = audio_write.load(std::memory_order_acquire) -
                        audio_read.load(std::memory_order_acquire);
    output["dashboard"] = {{"enabled", config.enabled}, {"release", "0.1.20"},
                            {"clients", dashboard_clients.load()},
                            {"waterfall_frames", waterfall_frames.load()},
                            {"spectrum_drops", spectrum_drops.load()},
                            {"login_failures", login_failures.load()},
                            {"audio_queue_ms", audio_rate.load() ?
                                queued * 1000.0 / audio_rate.load() : 0.0}};
    return output;
  }

  json history_json() {
    std::lock_guard<std::mutex> lock(state_mutex);
    return history;
  }

  bool authenticated(const http::request<http::string_body>& request) {
    const auto value = cookie_value(request);
    if (value.empty()) return false;
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(auth_mutex);
    for (auto it = sessions.begin(); it != sessions.end();) {
      if (it->second <= now) it = sessions.erase(it); else ++it;
    }
    const auto it = sessions.find(value);
    return it != sessions.end() && it->second > now;
  }

  bool rate_limited(const std::string& address) {
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(auth_mutex);
    auto& values = failures[address];
    while (!values.empty() && now - values.front() > std::chrono::minutes(1)) values.pop_front();
    return values.size() >= 5;
  }

  void record_failure(const std::string& address) {
    std::lock_guard<std::mutex> lock(auth_mutex);
    failures[address].push_back(std::chrono::steady_clock::now());
    login_failures++;
  }

  void login(tcp::socket& socket, const http::request<http::string_body>& request) {
    const auto address = socket.remote_endpoint().address().to_string();
    if (rate_limited(address)) {
      auto result = response(http::status::too_many_requests, request.version(),
                             "application/json", "{\"error\":\"rate-limited\"}\n");
      result.set(http::field::retry_after, "60");
      http::write(socket, result);
      return;
    }
    std::string supplied;
    try { supplied = json::parse(request.body()).value("token", ""); } catch (...) {}
    if (!dashboard_constant_time_equal(supplied, access_token)) {
      record_failure(address);
      write_response(socket, response(http::status::unauthorized, request.version(),
                                      "application/json", "{\"error\":\"invalid-login\"}\n"));
      return;
    }
    const auto id = random_hex(32);
    {
      std::lock_guard<std::mutex> lock(auth_mutex);
      sessions[id] = std::chrono::steady_clock::now() +
                     std::chrono::seconds(config.session_lifetime_seconds);
      failures.erase(address);
    }
    auto result = response(http::status::ok, request.version(), "application/json",
                           "{\"status\":\"ok\"}\n");
    result.set(http::field::set_cookie, std::string(kCookieName) + "=" + id +
               "; HttpOnly; SameSite=Strict; Path=/; Max-Age=" +
               std::to_string(config.session_lifetime_seconds));
    http::write(socket, result);
  }

  void logout(tcp::socket& socket, const http::request<http::string_body>& request) {
    const auto value = cookie_value(request);
    {
      std::lock_guard<std::mutex> lock(auth_mutex);
      sessions.erase(value);
    }
    auto result = response(http::status::ok, request.version(), "application/json",
                           "{\"status\":\"ok\"}\n");
    result.set(http::field::set_cookie, std::string(kCookieName) +
               "=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0");
    http::write(socket, result);
  }

  void websocket_stream(tcp::socket socket, http::request<http::string_body> request) {
    if (!authenticated(request)) {
      write_response(socket, response(http::status::unauthorized, request.version(),
                                      "application/json", "{\"error\":\"unauthorized\"}\n"));
      return;
    }
    beast::tcp_stream stream(std::move(socket));
    websocket::stream<beast::tcp_stream> ws(std::move(stream));
    ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
    ws.accept(request);
    dashboard_clients++;
    uint32_t sent = 0;
    auto next_heartbeat = std::chrono::steady_clock::now();
    try {
      while (!stopping.load()) {
        std::vector<uint8_t> frame;
        uint32_t sequence = 0;
        {
          std::lock_guard<std::mutex> lock(frame_mutex);
          sequence = latest_sequence;
          if (sequence != sent) frame = latest_frame;
        }
        if (!frame.empty()) {
          ws.binary(true);
          ws.write(asio::buffer(frame));
          sent = sequence;
        } else if (std::chrono::steady_clock::now() >= next_heartbeat) {
          ws.text(true);
          ws.write(asio::buffer(std::string("ping")));
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          continue;
        }
        next_heartbeat = std::chrono::steady_clock::now() +
                         std::chrono::seconds(config.websocket_heartbeat_seconds);
        beast::flat_buffer acknowledgement;
        beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(2));
        ws.read(acknowledgement);
        beast::get_lowest_layer(ws).expires_never();
        if (!ws.got_text() || beast::buffers_to_string(acknowledgement.data()) != "ack")
          break;
      }
    } catch (...) {}
    dashboard_clients--;
  }

  void handle(tcp::socket socket) {
    try {
      beast::flat_buffer buffer;
      http::request<http::string_body> request;
      http::read(socket, buffer, request);
      const std::string target = request.target().to_string();
      if (target == "/api/v1/stream" && websocket::is_upgrade(request)) {
        websocket_stream(std::move(socket), std::move(request));
        return;
      }
      if (target == "/api/v1/login" && request.method() == http::verb::post) {
        login(socket, request);
      } else if (target == "/api/v1/logout" && request.method() == http::verb::post) {
        if (authenticated(request)) logout(socket, request);
        else write_response(socket, response(http::status::unauthorized, request.version(),
                                             "application/json", "{\"error\":\"unauthorized\"}\n"));
      } else if (target == "/" || target == "/index.html") {
        write_response(socket, response(http::status::ok, request.version(), "text/html; charset=utf-8", index_html));
      } else if (target == "/app.js") {
        write_response(socket, response(http::status::ok, request.version(), "text/javascript; charset=utf-8", app_js));
      } else if (target == "/styles.css") {
        write_response(socket, response(http::status::ok, request.version(), "text/css; charset=utf-8", styles_css));
      } else if (!authenticated(request)) {
        write_response(socket, response(http::status::unauthorized, request.version(),
                                        "application/json", "{\"error\":\"unauthorized\"}\n"));
      } else if (target == "/api/v1/status" && request.method() == http::verb::get) {
        write_response(socket, response(http::status::ok, request.version(), "application/json",
                                        status_json().dump() + "\n"));
      } else if (target == "/api/v1/history" && request.method() == http::verb::get) {
        write_response(socket, response(http::status::ok, request.version(), "application/json",
                                        history_json().dump() + "\n"));
      } else {
        write_response(socket, response(http::status::not_found, request.version(),
                                        "application/json", "{\"error\":\"not-found\"}\n"));
      }
      beast::error_code ignored;
      socket.shutdown(tcp::socket::shutdown_send, ignored);
    } catch (...) {}
  }

  void server_loop() {
    while (!stopping.load()) {
      try {
        tcp::socket socket(server_ioc);
        acceptor->accept(socket);
        auto self = shared_from_this();
        std::thread([self, socket = std::move(socket)]() mutable {
          self->handle(std::move(socket));
        }).detach();
      } catch (const boost::system::system_error& error) {
        if (!stopping.load()) throw;
      }
    }
  }

  void fft_loop() {
    lower_current_thread_priority();
    FftEncoder encoder;
    uint32_t sequence = 0;
    const auto delay = std::chrono::milliseconds(1000 / config.waterfall_fps);
    while (!stopping.load()) {
      const uint64_t write = audio_write.load(std::memory_order_acquire);
      const uint64_t read = audio_read.load(std::memory_order_relaxed);
      if (write - read >= kFftSize) {
        std::array<int16_t, kFftSize> samples{};
        const uint64_t start = write - kFftSize;
        for (std::size_t i = 0; i < kFftSize; i++) samples[i] = audio[(start + i) % audio.size()];
        audio_read.store(write, std::memory_order_release);
        uint8_t flags = 0;
        {
          std::lock_guard<std::mutex> lock(state_mutex);
          if (session.value("active", false)) flags |= 1;
          if (session.value("sync", false)) flags |= 2;
        }
        auto frame = encoder.encode(samples.data(), samples.size(), audio_rate.load(),
                                    ++sequence, flags);
        {
          std::lock_guard<std::mutex> lock(frame_mutex);
          latest_sequence = sequence;
          latest_frame = std::move(frame);
        }
        waterfall_frames++;
      }
      std::this_thread::sleep_for(delay);
    }
  }

  void history_loop() {
    while (!stopping.load()) {
      const auto status = status_json();
      json sample = {{"timestamp_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch()).count()},
                     {"sync", status["session"].value("sync", false)},
                     {"snr_db", status["session"].value("snr_db", 0.0)},
                     {"frequency_offset_hz", status["session"].value("frequency_offset_hz", 0.0)},
                     {"audio_queue_ms", status["dashboard"].value("audio_queue_ms", 0.0)},
                     {"decode_seconds_total", status.value("decode_seconds_total", 0.0)},
                     {"snd_frames_total", status.value("snd_frames_total", 0)},
                     {"decoded_frames_total", status.value("decoded_frames_total", 0)},
                     {"dropped_frames_total", status.value("dropped_frames_total", 0)}};
      {
        std::lock_guard<std::mutex> lock(state_mutex);
        history.push_back(std::move(sample));
        while (history.size() > config.history_seconds) history.pop_front();
      }
      for (unsigned i = 0; i < 10 && !stopping.load(); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  void start() {
    if (!config.enabled || started.exchange(true)) return;
    access_token = trim(read_file(config.token_file));
    if (access_token.size() != 64 || !std::all_of(access_token.begin(), access_token.end(),
        [](unsigned char c) { return std::isxdigit(c); }))
      throw std::runtime_error("dashboard token must contain 64 hexadecimal characters");
    index_html = read_file(config.asset_directory + "/index.html");
    app_js = read_file(config.asset_directory + "/app.js");
    styles_css = read_file(config.asset_directory + "/styles.css");
    const auto address = asio::ip::make_address(config.bind_address);
    acceptor = std::make_shared<tcp::acceptor>(server_ioc, tcp::endpoint(address, config.port));
    auto self = shared_from_this();
    server_thread = std::thread([self] { self->server_loop(); });
    fft_thread = std::thread([self] { self->fft_loop(); });
    history_thread = std::thread([self] { self->history_loop(); });
  }

  void stop() {
    if (!started.load() || stopping.exchange(true)) return;
    // A synchronous accept is not guaranteed to unblock when another thread
    // closes the acceptor. Wake it with a local connection before joining.
    try {
      asio::io_context wake_ioc;
      tcp::socket wake_socket(wake_ioc);
      const std::string wake_address = config.bind_address == "::" ? "::1" :
          config.bind_address == "0.0.0.0" ? "127.0.0.1" : config.bind_address;
      wake_socket.connect(tcp::endpoint(asio::ip::make_address(wake_address), config.port));
      beast::error_code wake_ignored;
      wake_socket.close(wake_ignored);
    } catch (...) {}
    beast::error_code ignored;
    if (acceptor) acceptor->close(ignored);
    if (server_thread.joinable()) server_thread.join();
    if (fft_thread.joinable()) fft_thread.join();
    if (history_thread.joinable()) history_thread.join();
  }
};

Dashboard::Dashboard(DashboardConfig config, StatusProvider provider)
    : impl_(std::make_shared<Impl>(std::move(config), std::move(provider))) {}
Dashboard::~Dashboard() { stop(); }
void Dashboard::start() { impl_->start(); }
void Dashboard::stop() { impl_->stop(); }
bool Dashboard::enabled() const { return impl_->config.enabled; }

void Dashboard::session_started(uint64_t generation, int rx_channel, const std::string& mode,
                                uint64_t frequency_hz, uint32_t input_rate, bool test,
                                const std::string& backend) {
  std::lock_guard<std::mutex> lock(impl_->state_mutex);
  impl_->session = {{"active", true}, {"generation", generation}, {"rx_channel", rx_channel},
                    {"mode", mode}, {"frequency_hz", frequency_hz},
                    {"input_rate", input_rate}, {"test", test}, {"backend", backend},
                    {"sync", false}, {"snr_db", 0.0}, {"frequency_offset_hz", 0.0},
                    {"callsign", ""}, {"text", ""}, {"decoded_frames", 0}};
}

void Dashboard::session_stopped() {
  std::lock_guard<std::mutex> lock(impl_->state_mutex);
  impl_->session = {{"active", false}};
  impl_->audio_read.store(impl_->audio_write.load());
}

void Dashboard::update_status(const DecodeStatus& status, const std::string& backend,
                              uint64_t decoded_frames) {
  std::lock_guard<std::mutex> lock(impl_->state_mutex);
  if (!impl_->session.value("active", false)) return;
  auto& session = impl_->session;
  session["backend"] = backend;
  session["sync"] = status.synced;
  session["snr_db"] = status.snr_db;
  session["frequency_offset_hz"] = status.frequency_offset_hz;
  session["callsign"] = status.callsign;
  session["text"] = status.text;
  session["error"] = status.error;
  session["decoded_frames"] = decoded_frames;
  json modem;
  json_optional(modem, "bits", status.bits);
  json_optional(modem, "bit_errors", status.bit_errors);
  json_optional(modem, "packets", status.packets);
  json_optional(modem, "packet_errors", status.packet_errors);
  json_optional(modem, "resyncs", status.resyncs);
  json_optional(modem, "clock_offset_ppm", status.clock_offset_ppm);
  json_optional(modem, "timing_offset", status.timing_offset);
  json_optional(modem, "sync_metric", status.sync_metric);
  json_optional(modem, "codec_variance", status.codec_variance);
  session["modem"] = std::move(modem);
}

void Dashboard::push_audio(const int16_t* samples, std::size_t count, uint32_t sample_rate) {
  if (!impl_->config.enabled || !samples || !count) return;
  const uint64_t write = impl_->audio_write.load(std::memory_order_relaxed);
  const uint64_t read = impl_->audio_read.load(std::memory_order_acquire);
  const std::size_t free = impl_->audio.size() -
      static_cast<std::size_t>(std::min<uint64_t>(impl_->audio.size(), write - read));
  if (count > free) {
    impl_->spectrum_drops += count;
    return;
  }
  for (std::size_t i = 0; i < count; i++) impl_->audio[(write + i) % impl_->audio.size()] = samples[i];
  impl_->audio_rate = sample_rate;
  impl_->audio_write.store(write + count, std::memory_order_release);
}

}  // namespace kfd
