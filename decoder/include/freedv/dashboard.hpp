#pragma once

#include "freedv/backend.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace kfd {

struct DashboardConfig {
  bool enabled = true;
  std::string bind_address = "0.0.0.0";
  uint16_t port = 8076;
  std::string token_file = "/etc/freedv-decoder/dashboard.token";
  std::string asset_directory = "/usr/local/share/freedv-dashboard/current";
  unsigned history_seconds = 600;
  unsigned waterfall_fps = 10;
  unsigned session_lifetime_seconds = 8 * 60 * 60;
  unsigned websocket_heartbeat_seconds = 1;
};

DashboardConfig dashboard_config_from_environment();

// Versioned binary frame: "FDWF", version, flags, bin count, sample rate,
// sequence, then one unsigned -120..0 dBFS value per positive-frequency bin.
std::vector<uint8_t> dashboard_fft_frame(const int16_t* samples, std::size_t count,
                                         uint32_t sample_rate, uint32_t sequence,
                                         uint8_t flags);

bool dashboard_constant_time_equal(const std::string& left, const std::string& right);

class Dashboard {
 public:
  using StatusProvider = std::function<nlohmann::json()>;

  Dashboard(DashboardConfig config, StatusProvider provider);
  ~Dashboard();
  Dashboard(const Dashboard&) = delete;
  Dashboard& operator=(const Dashboard&) = delete;

  void start();
  void stop();
  bool enabled() const;

  void session_started(uint64_t generation, int rx_channel, const std::string& mode,
                       uint64_t frequency_hz, uint32_t input_rate, bool test,
                       const std::string& backend);
  void session_stopped();
  void update_status(const DecodeStatus& status, const std::string& backend,
                     uint64_t decoded_frames);
  void push_audio(const int16_t* samples, std::size_t count, uint32_t sample_rate);

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};

}  // namespace kfd
