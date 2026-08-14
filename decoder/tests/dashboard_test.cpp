#include "freedv/dashboard.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

#include <unistd.h>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;

namespace {

uint16_t get_u16(const std::vector<uint8_t>& value, std::size_t offset) {
  return static_cast<uint16_t>(value[offset] | (value[offset + 1] << 8));
}

uint32_t get_u32(const std::vector<uint8_t>& value, std::size_t offset) {
  uint32_t result = 0;
  for (unsigned i = 0; i < 4; i++) result |= static_cast<uint32_t>(value[offset + i]) << (8 * i);
  return result;
}

http::response<http::string_body> request(uint16_t port, http::verb method,
                                          const std::string& target,
                                          const std::string& body = {},
                                          const std::string& cookie = {}) {
  asio::io_context ioc;
  tcp::socket socket(ioc);
  socket.connect({asio::ip::make_address("127.0.0.1"), port});
  http::request<http::string_body> value{method, target, 11};
  value.set(http::field::host, "127.0.0.1");
  if (!cookie.empty()) value.set(http::field::cookie, cookie);
  if (!body.empty()) {
    value.set(http::field::content_type, "application/json");
    value.body() = body;
    value.prepare_payload();
  }
  http::write(socket, value);
  beast::flat_buffer buffer;
  http::response<http::string_body> result;
  http::read(socket, buffer, result);
  return result;
}

}  // namespace

int main() {
  constexpr uint32_t sample_rate = 12000;
  constexpr float pi = 3.14159265358979323846f;
  std::array<int16_t, 1024> tone{};
  for (std::size_t i = 0; i < tone.size(); i++)
    tone[i] = static_cast<int16_t>(20000.0f * std::sin(2.0f * pi * 1500.0f * i / sample_rate));

  const auto frame = kfd::dashboard_fft_frame(tone.data(), tone.size(), sample_rate, 42, 3);
  assert(frame.size() == 16 + 512);
  assert(frame[0] == 'F' && frame[1] == 'D' && frame[2] == 'W' && frame[3] == 'F');
  assert(frame[4] == 1 && frame[5] == 3);
  assert(get_u16(frame, 6) == 512);
  assert(get_u32(frame, 8) == sample_rate);
  assert(get_u32(frame, 12) == 42);
  const auto peak = std::max_element(frame.begin() + 16, frame.end());
  assert(static_cast<std::size_t>(peak - (frame.begin() + 16)) == 128);
  assert(*peak > 220);

  std::array<int16_t, 1024> multi_tone{};
  for (std::size_t i = 0; i < multi_tone.size(); i++) {
    const float first = std::sin(2.0f * pi * 750.0f * i / sample_rate);
    const float second = std::sin(2.0f * pi * 2250.0f * i / sample_rate);
    multi_tone[i] = static_cast<int16_t>(12000.0f * (first + second));
  }
  const auto multi_frame = kfd::dashboard_fft_frame(
      multi_tone.data(), multi_tone.size(), sample_rate, 43, 0);
  assert(multi_frame[16 + 64] > 205);
  assert(multi_frame[16 + 192] > 205);

  bool rejected = false;
  try { (void)kfd::dashboard_fft_frame(tone.data(), 100, sample_rate, 1, 0); }
  catch (const std::invalid_argument&) { rejected = true; }
  assert(rejected);

  const auto root = std::filesystem::temp_directory_path() /
                    ("freedv-dashboard-test-" + std::to_string(getpid()));
  std::filesystem::create_directories(root);
  std::ofstream(root / "index.html") << "<!doctype html><title>FreeDV Decoder Diagnostics</title>";
  std::ofstream(root / "app.js") << "'use strict';";
  std::ofstream(root / "styles.css") << "body{color:white}";
  const auto public_root = root / "public";
  std::filesystem::create_directories(public_root);
  std::ofstream(public_root / "index.html") << "<!doctype html><title>FreeDV Live Signal Monitor</title>";
  std::ofstream(public_root / "app.js") << "'use strict';";
  std::ofstream(public_root / "styles.css") << "body{color:white}";

  kfd::DashboardConfig config;
  config.bind_address = "127.0.0.1";
  config.port = static_cast<uint16_t>(19000 + getpid() % 1000);
  config.asset_directory = root.string();
  config.history_seconds = 60;
  config.waterfall_fps = 10;
  config.websocket_heartbeat_seconds = 1;
  config.capture_seconds = 1;
  config.public_enabled = true;
  config.public_bind_address = "127.0.0.1";
  config.public_port = static_cast<uint16_t>(config.port + 1);
  config.public_asset_directory = public_root.string();
  config.public_max_clients = 1;
  kfd::Dashboard dashboard(config, [] {
    return nlohmann::json{{"release", "test"}, {"kiwi_connected", true},
                          {"decode_seconds_total", 1.25}};
  });
  dashboard.start();
  dashboard.session_started(7, 2, "700D", 7177000, sample_rate, false, "codec2");
  kfd::DecodeStatus status;
  status.synced = true;
  status.snr_db = -1.5f;
  status.frequency_offset_hz = 4.0f;
  status.bits = 100;
  status.bit_errors = 2;
  status.resyncs = 1;
  dashboard.update_status(status, "codec2", 3);
  dashboard.push_audio(tone.data(), tone.size(), sample_rate);
  dashboard.push_modem_audio(tone.data(), tone.size(), 8000);
  std::this_thread::sleep_for(std::chrono::milliseconds(250));

  auto current = request(config.port, http::verb::get, "/api/v1/status");
  assert(current.result() == http::status::ok);
  const auto current_json = nlohmann::json::parse(current.body());
  assert(current_json["session"]["mode"] == "700D");
  assert(current_json["session"]["modem"]["bits"] == 100);
  assert(current_json["dashboard"]["waterfall_frames"].get<uint64_t>() > 0);
  assert(current_json["dashboard"]["capture"]["available"] == true);
  assert(current_json["dashboard"]["capture"]["sample_rate"] == 8000);
  assert(current_json["dashboard"]["public_enabled"] == true);
  auto capture = request(config.port, http::verb::get, "/api/v1/capture.wav");
  assert(capture.result() == http::status::ok);
  assert(capture[http::field::content_type] == "audio/wav");
  assert(capture.body().size() == 44 + tone.size() * sizeof(int16_t));
  assert(capture.body().substr(0, 4) == "RIFF");
  assert(capture.body().substr(8, 8) == "WAVEfmt ");
  assert(static_cast<unsigned char>(capture.body()[24]) == 0x40);
  assert(static_cast<unsigned char>(capture.body()[25]) == 0x1f);
  std::vector<int16_t> overflow(40000, 0);
  dashboard.push_audio(overflow.data(), overflow.size(), sample_rate);
  auto overflow_status = request(config.port, http::verb::get, "/api/v1/status");
  assert(nlohmann::json::parse(overflow_status.body())["dashboard"]["spectrum_drops"]
             .get<uint64_t>() >= overflow.size());
  auto traversal = request(config.port, http::verb::get, "/../../etc/passwd");
  assert(traversal.result() == http::status::not_found);
  auto removed_login = request(config.port, http::verb::post, "/api/v1/login",
                               "{\"token\":\"unused\"}");
  assert(removed_login.result() == http::status::not_found);

  auto public_root_page = request(config.public_port, http::verb::get, "/");
  assert(public_root_page.result() == http::status::ok);
  assert(public_root_page.body().find("FreeDV Live Signal Monitor") != std::string::npos);
  auto public_current = request(config.public_port, http::verb::get, "/api/v1/status");
  assert(public_current.result() == http::status::ok);
  const auto public_json = nlohmann::json::parse(public_current.body());
  assert(public_json.size() == 4);
  assert(public_json["version"] == 1);
  assert(public_json["kiwi_connected"] == true);
  assert(public_json["session"]["mode"] == "700D");
  assert(public_json["session"]["sync"] == true);
  assert(!public_json.contains("dashboard"));
  assert(!public_json.contains("decode_seconds_total"));
  assert(!public_json["session"].contains("backend"));
  assert(!public_json["session"].contains("callsign"));
  assert(!public_json["session"].contains("text"));
  auto public_history = request(config.public_port, http::verb::get, "/api/v1/history");
  assert(public_history.result() == http::status::ok);
  for (const auto& sample : nlohmann::json::parse(public_history.body())) {
    assert(sample.size() <= 4);
    assert(!sample.contains("audio_queue_ms"));
    assert(!sample.contains("decode_seconds_total"));
    assert(!sample.contains("dropped_frames_total"));
  }
  auto public_capture = request(config.public_port, http::verb::get, "/api/v1/capture.wav");
  assert(public_capture.result() == http::status::not_found);
  auto public_post = request(config.public_port, http::verb::post, "/");
  assert(public_post.result() == http::status::not_found);
  auto public_traversal = request(config.public_port, http::verb::get, "/../../etc/passwd");
  assert(public_traversal.result() == http::status::not_found);

  asio::io_context public_ws_ioc;
  websocket::stream<tcp::socket> public_ws(public_ws_ioc);
  public_ws.next_layer().connect({asio::ip::make_address("127.0.0.1"), config.public_port});
  public_ws.handshake("127.0.0.1", "/api/v1/stream");
  beast::flat_buffer public_ws_buffer;
  public_ws.read(public_ws_buffer);
  assert(public_ws.got_binary());
  public_ws.text(true);
  public_ws.write(asio::buffer(std::string("ack")));

  tcp::socket excess_socket(public_ws_ioc);
  excess_socket.connect({asio::ip::make_address("127.0.0.1"), config.public_port});
  http::request<http::empty_body> excess_request{http::verb::get, "/api/v1/stream", 11};
  excess_request.set(http::field::host, "127.0.0.1");
  excess_request.set(http::field::upgrade, "websocket");
  excess_request.set(http::field::connection, "upgrade");
  excess_request.set(http::field::sec_websocket_version, "13");
  excess_request.set(http::field::sec_websocket_key, "dGhlIHNhbXBsZSBub25jZQ==");
  http::write(excess_socket, excess_request);
  beast::flat_buffer excess_buffer;
  http::response<http::string_body> excess_response;
  http::read(excess_socket, excess_buffer, excess_response);
  assert(excess_response.result() == http::status::too_many_requests);
  beast::get_lowest_layer(public_ws).close();

  asio::io_context ws_ioc;
  websocket::stream<tcp::socket> ws(ws_ioc);
  ws.next_layer().connect({asio::ip::make_address("127.0.0.1"), config.port});
  ws.handshake("127.0.0.1", "/api/v1/stream");
  beast::flat_buffer ws_buffer;
  ws.read(ws_buffer);
  assert(ws.got_binary());
  assert(ws_buffer.size() == 16 + 512);
  ws.text(true);
  ws.write(asio::buffer(std::string("ack")));
  beast::get_lowest_layer(ws).close();
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  auto after_disconnect = request(config.port, http::verb::get, "/api/v1/status");
  assert(nlohmann::json::parse(after_disconnect.body())["dashboard"]["clients"] == 0);
  assert(nlohmann::json::parse(after_disconnect.body())["dashboard"]["public_clients"] == 0);
  dashboard.stop();
  std::filesystem::remove_all(root);
  return 0;
}
