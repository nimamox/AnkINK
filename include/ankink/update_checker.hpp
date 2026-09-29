#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace ankink {

struct DeviceTelemetry {
  std::string model{"Unknown"};
  std::string firmware{"Unknown"};
  std::string kernel{"Unknown"};
  std::string architecture{"Unknown"};
};

using UpdatePostFunction = std::function<bool(
    const std::string &, const std::string &, std::string &)>;
using DeviceTelemetryFunction = std::function<DeviceTelemetry()>;

struct UpdateCheckerOptions {
  std::string data_dir;
  std::string endpoint;
  std::string ca_bundle;
  std::string app;
  std::string current_version;
  std::string build_commit;
  std::string build_type;
  bool simulator{};
  UpdatePostFunction post;
  DeviceTelemetryFunction device_telemetry;
};

bool update_version_is_newer(const std::string &candidate,
                             const std::string &current) noexcept;

class UpdateChecker {
public:
  explicit UpdateChecker(UpdateCheckerOptions options);
  ~UpdateChecker();
  UpdateChecker(const UpdateChecker &) = delete;
  UpdateChecker &operator=(const UpdateChecker &) = delete;

  void start() noexcept;
  void stop() noexcept;
  [[nodiscard]] std::string status_json() const;
  bool dismiss_latest(std::string &error);

private:
  void run() noexcept;
  bool post(const std::string &body, std::string &response) noexcept;

  UpdateCheckerOptions options_;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> started_{false};
  std::thread thread_;
  mutable std::mutex mutex_;
  bool checked_{};
  std::string latest_version_;
  std::string dismissed_version_;
};

} // namespace ankink
