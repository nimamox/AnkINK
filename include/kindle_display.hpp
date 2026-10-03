#pragma once
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Kept identical in the two independent product repositories.
namespace kindle_display {
enum class Backend { framework, framebuffer };
struct FramebufferInfo {
  std::string driver;
  unsigned bits_per_pixel{}, grayscale{};
};
bool supports_inversion(const FramebufferInfo &info) noexcept;

// Device operations are injectable so host tests never need a framebuffer.
class Device {
public:
  virtual ~Device() = default;
  virtual bool read(Backend backend, bool &night, std::string &error) = 0;
  virtual bool write(Backend backend, bool night, std::string &error) = 0;
  virtual bool refresh(std::string &error) = 0;
  // True only for a completed, rejected framework request. Timeouts and
  // unknown/partial outcomes must not cause a second control mechanism.
  virtual bool fallback_safe() const { return false; }
};
using CommandRunner = std::function<bool(const std::vector<std::string> &,
                                        std::string &, std::string &)>;
bool run_command(const std::vector<std::string> &args, std::string &output,
                 std::string &error, bool *completed = nullptr);
struct RefreshGeometry {
  unsigned width{}, height{}, virtual_width{}, virtual_height{}, xoffset{}, yoffset{};
};
bool supports_eips_refresh(const std::string &usage) noexcept;
bool refresh_with_eips(const RefreshGeometry &geometry, const CommandRunner &runner,
                       std::string &error);
bool full_refresh(bool simulator, std::string &error);
std::shared_ptr<Device> make_device();

struct Options {
  bool simulator{};
  std::string journal{ "/var/local/kindledev-night-mode.restore" };
  std::shared_ptr<Device> device;
};
struct State {
  bool native{}, available{}, known{}, night{}, refresh_pending{};
  std::string backend, error;
};
class Controller {
public:
  explicit Controller(Options options);
  ~Controller();
  State state();
  bool set(bool night, std::string &error);
  bool refresh(std::string &error);
  bool restore(std::string &error);
  std::string settings_json(std::string settings);
private:
  void initialize();
  State read_locked();
  bool write_verified(Backend backend, bool night, std::string &error,
                      bool *accepted = nullptr);
  bool rollback_framework(bool previous, std::string &error);
  bool select_framebuffer(bool night, std::string &error);
  bool save_journal(bool original, bool last, std::string &error);
  bool recover(std::string &error);
  bool restore_locked(std::string &error);
  Options options_;
  std::shared_ptr<Device> device_;
  std::mutex mutex_;
  State state_;
  Backend backend_{Backend::framework};
  bool original_{}, last_{}, owned_{};
  int lock_{-1};
};
} // namespace kindle_display
