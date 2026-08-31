#pragma once
#include "ankink/app_state.hpp"
#include "ankink/collection.hpp"
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
  std::uint16_t port{8765};
  bool simulator{};
  std::size_t worker_count{4};
  // A zero-cost production default and a deterministic integration-test seam.
  std::chrono::milliseconds collection_operation_delay{};
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

  ServerOptions options_;
  AppState app_state_;
  Collection collection_;
  std::string collection_error_;
  std::mutex collection_mutex_;

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
