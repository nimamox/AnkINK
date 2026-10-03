#pragma once
#include "kindle_display.hpp"
#include "ankink/app_state.hpp"
#include "ankink/collection.hpp"
#include "ankink/update_checker.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
namespace ankink {
struct ServerOptions {
  std::string collection_path{"/var/local/ankink/collection.anki2"};
  std::string data_dir{"/var/local/ankink"};
  std::string asset_dir{"assets"};
  std::string simulator_asset_dir{"simulator"};
  std::string ca_bundle_path;
  std::uint16_t port{9257};
  bool simulator{};
  std::string display_journal{"/var/local/kindledev-night-mode.restore"};
  std::shared_ptr<kindle_display::Device> display_device;
  bool update_checks_enabled{};
  std::size_t worker_count{4};
  // A zero-cost production default and a deterministic integration-test seam.
  std::chrono::milliseconds collection_operation_delay{};
  UpdatePostFunction update_poster;
  DeviceTelemetryFunction device_telemetry;
};
class HttpServer {
public:
  explicit HttpServer(ServerOptions options);
  ~HttpServer();
  int run();
  void stop() noexcept;
  static void request_stop() noexcept;
  [[nodiscard]] std::uint16_t bound_port() const noexcept;
private:
  void handle_client(int client) noexcept;
  void worker_loop() noexcept;
  void wake_listener() noexcept;
  void close_wakeup_pipe() noexcept;

  std::string effective_settings_json();
  std::string set_night_mode(const std::string &value, bool &ok);
  ServerOptions options_;
  kindle_display::Controller display_;
  std::mutex display_settings_mutex_;
  AppState app_state_;
  Collection collection_;
  std::string collection_error_;
  std::mutex collection_mutex_;
  UpdateChecker update_checker_;

  std::atomic<bool> stopping_{false};
  std::atomic<std::uint16_t> bound_port_{0};
  std::mutex clients_mutex_;
  std::set<int> active_clients_;
  std::mutex pending_clients_mutex_;
  std::condition_variable pending_clients_condition_;
  std::deque<int> pending_clients_;
  std::vector<std::thread> workers_;
  int wake_read_{-1};
  int wake_write_{-1};
};
} // namespace ankink
