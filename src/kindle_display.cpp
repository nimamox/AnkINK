#include "kindle_display.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <linux/fb.h>
#include <sys/ioctl.h>
#endif

namespace kindle_display {
namespace {
std::string trim(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  return first == std::string::npos ? "" :
    value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
std::string boot_id() {
  std::ifstream input("/proc/sys/kernel/random/boot_id");
  std::string id; input >> id; return id.empty() ? "host" : id;
}
std::string quoted(const std::string &s) {
  std::string out = "\"";
  for (const unsigned char c : s) {
    if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if (c >= 32) out += static_cast<char>(c);
  }
  return out + '"';
}
const char *name(Backend backend) {
  return backend == Backend::framework ? "epdcMode" : "framebuffer";
}
bool kindle_host() {
#ifdef __linux__
  std::ifstream input("/etc/prettyversion.txt");
  std::string value; std::getline(input, value);
  return value.find("Kindle") != std::string::npos;
#else
  return false;
#endif
}
class NativeDevice final : public Device {
public:
  bool read(Backend backend, bool &night, std::string &error) override {
    if (!kindle_host()) { error = "Native display control requires Kindle firmware"; return false; }
    if (backend == Backend::framework) {
      std::string value;
      if (!run_command({"/usr/bin/lipc-get-prop", "com.lab126.winmgr", "epdcMode"}, value, error)) return false;
      value = trim(value);
      if (value != "Y8" && value != "Y8INV") { error = "Unsupported epdcMode: " + value; return false; }
      night = value == "Y8INV"; return true;
    }
    return framebuffer(false, night, error);
  }
  bool write(Backend backend, bool night, std::string &error) override {
    if (!kindle_host()) { error = "Native display control requires Kindle firmware"; return false; }
    if (backend == Backend::framework) {
      std::string output;
      bool completed = false;
      const bool ok = run_command({"/usr/bin/lipc-set-prop", "com.lab126.winmgr", "epdcMode", night ? "Y8INV" : "Y8"}, output, error, &completed);
      framework_rejected_ = !ok && completed;
      return ok;
    }
    return framebuffer(true, night, error);
  }
  bool refresh(std::string &error) override { return full_refresh(false, error); }
  bool fallback_safe() const override { return framework_rejected_; }
private:
  bool framework_rejected_{};
  bool framebuffer(bool write, bool &night, std::string &error) {
#ifdef __linux__
    const int fd = ::open("/dev/fb0", (write ? O_RDWR : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) { error = "Open framebuffer: " + std::string(std::strerror(errno)); return false; }
    struct fb_var_screeninfo var{};
    struct fb_fix_screeninfo fix{};
    bool ok = ::ioctl(fd, FBIOGET_FSCREENINFO, &fix) == 0 &&
              ::ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0;
    if (!ok) error = "Read framebuffer: " + std::string(std::strerror(errno));
    else if (!supports_inversion({std::string(fix.id, strnlen(fix.id, sizeof(fix.id))), var.bits_per_pixel, var.grayscale}) ||
             fix.type != FB_TYPE_PACKED_PIXELS || fix.visual != FB_VISUAL_STATIC_PSEUDOCOLOR) {
      ok = false; error = "Framebuffer is not a supported 8-bit grayscale EPDC display";
    } else if (write) {
      // Read fresh geometry for every write, including after rotation. Only
      // grayscale changes: no bit depth, offsets, activation or timing changes.
      var.grayscale = night ? 2U : 1U;
      if (::ioctl(fd, FBIOPUT_VSCREENINFO, &var) != 0) {
        ok = false; error = "Set framebuffer grayscale: " + std::string(std::strerror(errno));
      }
    } else night = var.grayscale == 2U;
    ::close(fd); return ok;
#else
    (void)write; (void)night;
    error = "Framebuffer inversion is unavailable on this host"; return false;
#endif
  }
};
} // namespace

bool supports_inversion(const FramebufferInfo &info) noexcept {
  return info.driver == "mxc_epdc_fb" && info.bits_per_pixel == 8 &&
         (info.grayscale == 1 || info.grayscale == 2);
}

// Bounded, event-only subprocesses for firmware LIPC and stock eips. No shell, scheduler, timer thread, or idle polling is involved.
bool run_command(const std::vector<std::string> &args, std::string &output,
                 std::string &error, bool *completed) {
  output.clear(); error.clear();
  if (completed) *completed = false;
  if (args.empty()) { error = "Empty display command"; return false; }
  std::vector<char *> argv;
  for (const auto &arg : args) argv.push_back(const_cast<char *>(arg.c_str()));
  argv.push_back(nullptr);
  int pipefd[2];
  if (::pipe(pipefd) != 0) { error = std::strerror(errno); return false; }
  ::fcntl(pipefd[0], F_SETFD, FD_CLOEXEC); ::fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
  const pid_t child = ::fork();
  if (child < 0) { ::close(pipefd[0]); ::close(pipefd[1]); error = std::strerror(errno); return false; }
  if (child == 0) {
    ::close(pipefd[0]);
    ::dup2(pipefd[1], STDOUT_FILENO); ::dup2(pipefd[1], STDERR_FILENO);
    ::close(pipefd[1]);
    const int nullfd = ::open("/dev/null", O_RDONLY);
    if (nullfd >= 0) { ::dup2(nullfd, STDIN_FILENO); ::close(nullfd); }
    ::execv(argv[0], argv.data()); ::_exit(127);
  }
  ::close(pipefd[1]);
  ::fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  bool eof = false, timed_out = false;
  int status = 0;
  for (;;) {
    char buffer[512];
    const auto count = ::read(pipefd[0], buffer, sizeof(buffer));
    if (count > 0 && output.size() < 4096) output.append(buffer, std::min<std::size_t>(count, 4096 - output.size()));
    else if (count == 0) eof = true;
    const auto waited = ::waitpid(child, &status, WNOHANG);
    if (waited == child) break;
    if (waited < 0 && errno != EINTR) { error = std::strerror(errno); break; }
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    if (left <= 0) { timed_out = true; ::kill(child, SIGKILL); while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {} break; }
    // EOF without exit is possible for a hung utility; wait without spinning.
    struct pollfd descriptor{pipefd[0], POLLIN, 0};
    if (eof) ::poll(nullptr, 0, static_cast<int>(std::min<long long>(left, 20)));
    else ::poll(&descriptor, 1, static_cast<int>(left));
  }
  char tail[512]; ssize_t count;
  while ((count = ::read(pipefd[0], tail, sizeof(tail))) > 0)
    if (output.size() < 4096) output.append(tail, std::min<std::size_t>(count, 4096 - output.size()));
  ::close(pipefd[0]);
  if (timed_out) { error = args[0] + " timed out"; return false; }
  if (!error.empty()) return false;
  if (completed) *completed = WIFEXITED(status);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    error = args[0] + " failed: " + trim(output); return false;
  }
  return true;
}
bool supports_eips_refresh(const std::string &usage) noexcept {
  // Do not run --help: on some firmware it enters an invalid-option loop.
  // Check the executable's own documented refresh-only command instead.
  return usage.find("to flash display with current fb content:") != std::string::npos &&
         usage.find("eips -s w=") != std::string::npos;
}
bool refresh_with_eips(const RefreshGeometry &g, const CommandRunner &runner,
                       std::string &error) {
  if (!g.width || !g.height || g.width > 16384 || g.height > 16384 ||
      g.xoffset > g.virtual_width || g.width > g.virtual_width - g.xoffset ||
      g.yoffset > g.virtual_height || g.height > g.virtual_height - g.yoffset) {
    error = "Invalid visible framebuffer geometry for full refresh"; return false;
  }
  // Use visible xres/yres, never stride or virtual dimensions. Firmware eips
  // handles the device-specific update ABI, waveform and completion wait.
  std::string output;
  return runner({"/usr/sbin/eips", "-s", "w=" + std::to_string(g.width) +
                 ",h=" + std::to_string(g.height), "-f"}, output, error);
}
bool full_refresh(bool simulator, std::string &error) {
  if (simulator) return true;
  if (!kindle_host()) { error = "Full refresh requires Kindle firmware"; return false; }
#ifdef __linux__
  if (::access("/usr/sbin/eips", X_OK) != 0) {
    error = "Stock Kindle eips refresh tool is unavailable"; return false;
  }
  std::ifstream executable("/usr/sbin/eips", std::ios::binary);
  // The tested tools are ~62 KiB. Bound capability inspection, with no shell
  // or execution of firmware options that might clear or paint the display.
  std::string usage(1024 * 1024, '\0');
  executable.read(&usage[0], static_cast<std::streamsize>(usage.size()));
  usage.resize(static_cast<std::size_t>(executable.gcount()));
  if (!supports_eips_refresh(usage)) {
    error = "Stock eips does not document the supported refresh-only operation"; return false;
  }
  const int fd = ::open("/dev/fb0", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (fd < 0) { error = "Open framebuffer for refresh: " + std::string(std::strerror(errno)); return false; }
  struct fb_var_screeninfo var{};
  const bool ok = ::ioctl(fd, FBIOGET_VSCREENINFO, &var) == 0;
  if (!ok) error = "Read refresh geometry: " + std::string(std::strerror(errno));
  ::close(fd);
  if (!ok) return false;
  return refresh_with_eips({var.xres, var.yres, var.xres_virtual, var.yres_virtual,
                            var.xoffset, var.yoffset},
    [](const std::vector<std::string> &args, std::string &output, std::string &failure) {
      return run_command(args, output, failure);
    }, error);
#else
  error = "Native refresh is unavailable on this host"; return false;
#endif
}
std::shared_ptr<Device> make_device() { return std::make_shared<NativeDevice>(); }

Controller::Controller(Options options) : options_(std::move(options)), device_(options_.device) {
  state_.native = !options_.simulator;
  if (options_.simulator) {
    state_.backend = "simulator";
    state_.error = "Native Night Mode is unavailable in the host simulator";
    return;
  }
  if (!device_) {
    // Host builds and QEMU-user checks must not acquire a global display lock
    // or touch /dev/fb0. Only actual Kindle firmware (or injected tests) does.
    if (!kindle_host()) {
      state_.backend = "unavailable";
      state_.error = "Native Night Mode requires Kindle firmware";
      return;
    }
    device_ = make_device();
  }
  initialize();
}
void Controller::initialize() {
  lock_ = ::open((options_.journal + ".lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (lock_ < 0 || ::flock(lock_, LOCK_EX | LOCK_NB) != 0) {
    state_.error = "Another app owns native Night Mode, or the display lock is unavailable";
    if (lock_ >= 0) ::close(lock_);
    lock_ = -1; return;
  }
  std::string error;
  if (!recover(error)) { state_.error = "Night Mode recovery: " + error; return; }
  bool value = false, framebuffer_value = false;
  const bool framework = device_->read(Backend::framework, value, error);
  std::string fb_error;
  const bool framebuffer = device_->read(Backend::framebuffer, framebuffer_value, fb_error);
  // A framework value that disagrees with the actual framebuffer is not an
  // authority for this session (e.g. externally enabled fbdepth inversion).
  if (framework && framebuffer && value == framebuffer_value) backend_ = Backend::framework;
  else if (framebuffer) { backend_ = Backend::framebuffer; value = framebuffer_value; }
  else { state_.error = error + "; " + fb_error; return; }
  original_ = last_ = value;
  state_.available = true; state_.known = true; state_.night = value;
  state_.error.clear();
  state_.backend = name(backend_);
  std::cerr << "Native Night Mode: " << state_.backend << ", initial=" << (value ? "night" : "day") << '\n';
}
Controller::~Controller() {
  std::string error;
  if (!restore(error)) std::cerr << "Native Night Mode restore: " << error << '\n';
  if (lock_ >= 0) ::close(lock_);
}
State Controller::read_locked() {
  // A previous daemon can still be finishing shutdown on a quick relaunch.
  // Retry only when the UI explicitly asks, never with an idle timer.
  if (device_ && state_.native && !state_.available && lock_ < 0) initialize();
  if (state_.native && state_.available) {
    bool value; std::string error;
    bool ok = device_->read(backend_, value, error);
    if (ok && backend_ == Backend::framework) {
      bool framebuffer_value;
      ok = device_->read(Backend::framebuffer, framebuffer_value, error);
      if (ok && framebuffer_value != value) {
        ok = false; error = "Framework inversion disagrees with actual framebuffer state";
      }
    }
    if (ok) { state_.known = true; state_.night = value; }
    else { state_.known = false; state_.error = error; }
  }
  return state_;
}
State Controller::state() { std::lock_guard<std::mutex> guard(mutex_); return read_locked(); }
bool Controller::write_verified(Backend backend, bool night, std::string &error, bool *accepted) {
  if (!device_->write(backend, night, error)) return false;
  if (accepted) *accepted = true;
  bool value;
  if (!device_->read(backend, value, error)) return false;
  if (value != night) { error = "Display control did not retain the requested inversion state"; return false; }
  if (backend == Backend::framework) {
    bool framebuffer_value;
    if (!device_->read(Backend::framebuffer, framebuffer_value, error)) return false;
    if (framebuffer_value != night) {
      error = "Framework control did not change actual framebuffer inversion";
      return false;
    }
  }
  return true;
}
bool Controller::rollback_framework(bool previous, std::string &error) {
  bool framebuffer_current, framework_current;
  if (!device_->read(Backend::framework, framework_current, error) ||
      !device_->read(Backend::framebuffer, framebuffer_current, error)) return false;
  if (framebuffer_current != previous) {
    error = "Cannot safely fall back: framebuffer changed during native request"; return false;
  }
  // A completed setter may be reset by the window manager before the first
  // getter (KOA1). Never classify that as an ambiguous write: explicitly
  // restore and verify the original framework state AND unchanged framebuffer.
  if (!write_verified(Backend::framework, previous, error) ||
      !device_->read(Backend::framebuffer, framebuffer_current, error)) return false;
  if (framebuffer_current != previous) {
    error = "Cannot safely roll back ineffective epdcMode"; return false;
  }
  return true;
}
bool Controller::select_framebuffer(bool night, std::string &error) {
  backend_ = Backend::framebuffer; state_.backend = name(backend_);
  error.clear();
  if (!save_journal(original_, night, error) || !write_verified(backend_, night, error)) return false;
  std::cerr << "Native Night Mode: ineffective epdcMode, verified original state, using framebuffer\n";
  return true;
}
bool Controller::save_journal(bool original, bool last, std::string &error) {
  const auto temporary = options_.journal + ".tmp";
  const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) { error = "Cannot write Night Mode recovery journal"; return false; }
  const std::string body = "1 " + std::string(name(backend_)) + " " + (original ? "1" : "0") + " " + (last ? "1" : "0") + " " + boot_id() + "\n";
  const bool ok = ::write(fd, body.data(), body.size()) == static_cast<ssize_t>(body.size()) && ::fsync(fd) == 0;
  ::close(fd);
  if (!ok || ::rename(temporary.c_str(), options_.journal.c_str()) != 0) { error = "Cannot commit Night Mode recovery journal"; ::unlink(temporary.c_str()); return false; }
  // Persist the rename before touching globally visible hardware state.
  const auto slash = options_.journal.find_last_of('/');
  const auto directory = slash == std::string::npos ? "." : options_.journal.substr(0, slash);
  const int parent = ::open(directory.c_str(), O_RDONLY | O_CLOEXEC);
  if (parent < 0 || ::fsync(parent) != 0) { if (parent >= 0) ::close(parent); error = "Cannot sync Night Mode journal directory"; return false; }
  ::close(parent); return true;
}
bool Controller::recover(std::string &error) {
  std::ifstream input(options_.journal);
  if (!input) return true;
  std::string version, backend, original, last, boot, extra;
  if (!(input >> version >> backend >> original >> last >> boot) || input >> extra ||
      version != "1" || (backend != "epdcMode" && backend != "framebuffer") ||
      (original != "0" && original != "1") || (last != "0" && last != "1")) {
    error = "Invalid recovery journal; hardware left unchanged"; return false;
  }
  if (boot != boot_id()) { ::unlink(options_.journal.c_str()); return true; }
  backend_ = backend == "epdcMode" ? Backend::framework : Backend::framebuffer;
  bool value;
  if (!device_->read(backend_, value, error)) return false;
  if (value != (last == "1") && value != (original == "1")) {
    ::unlink(options_.journal.c_str()); return true;
  }
  if (value == (last == "1") && value != (original == "1") &&
      !write_verified(backend_, original == "1", error)) return false;
  // Also refresh a restoration that completed just before a crash.
  if (!device_->refresh(error)) return false;
  ::unlink(options_.journal.c_str());
  std::cerr << "Native Night Mode: recovered previous session\n";
  return true;
}
bool Controller::set(bool night, std::string &error) {
  std::lock_guard<std::mutex> guard(mutex_);
  if (!state_.native || !state_.available) { error = state_.error.empty() ? "Native Night Mode is unavailable" : state_.error; return false; }
  if (!read_locked().known) { error = state_.error; return false; }
  // Respect an outside owner that changed state since our last request.
  if (owned_ && state_.night != last_) { owned_ = false; ::unlink(options_.journal.c_str()); original_ = state_.night; }
  if (state_.night == night) {
    if (state_.refresh_pending && !device_->refresh(error)) { state_.error = error; return false; }
    state_.refresh_pending = false; state_.error.clear(); return true;
  }
  if (!owned_) original_ = state_.night;
  const bool previous = state_.night;
  bool fb_before = false; std::string fb_check;
  const bool check_native_framebuffer = backend_ == Backend::framework &&
    device_->read(Backend::framebuffer, fb_before, fb_check) && fb_before == previous;
  if (!save_journal(original_, night, error)) { state_.error = error; return false; }
  owned_ = true; last_ = night;
  bool accepted = false;
  if (!write_verified(backend_, night, error, &accepted)) {
    bool framework_current, fb_current; std::string check;
    const bool rejected_unchanged = !accepted && device_->fallback_safe() &&
      backend_ == Backend::framework &&
      device_->read(backend_, framework_current, check) && framework_current == previous &&
      device_->read(Backend::framebuffer, fb_current, check) && fb_current == previous;
    // An acknowledged but reverted property is safe only after an explicit,
    // verified rollback. Timeouts, unreadable states and changed framebuffers
    // never qualify. No waiting or retry loop is needed for the firmware race.
    const bool rolled_back = accepted && check_native_framebuffer &&
      backend_ == Backend::framework && rollback_framework(previous, error);
    if ((!rejected_unchanged && !rolled_back) || !select_framebuffer(night, error)) {
      state_.error = error; read_locked(); return false;
    }
  }
  if (backend_ == Backend::framework && check_native_framebuffer) {
    bool fb_after = false;
    if (!device_->read(Backend::framebuffer, fb_after, error)) {
      state_.error = error; read_locked(); return false;
    }
    if (fb_after != night) {
      // The property retained its value but did not change global inversion.
      // Apply exactly the same rollback checks as the earlier-reversion case.
      if (!rollback_framework(previous, error) || !select_framebuffer(night, error)) {
        state_.error = error; read_locked(); return false;
      }
    }
  }
  state_.night = night; state_.known = true; state_.refresh_pending = true;
  std::cerr << "Native Night Mode: " << state_.backend << " -> " << (night ? "night" : "day") << '\n';
  if (!device_->refresh(error)) { state_.error = error; return false; }
  state_.refresh_pending = false;
  if (!read_locked().known || state_.night != night) {
    error = state_.known ? "Display inversion did not remain active after full refresh" : state_.error;
    state_.error = error; return false;
  }
  state_.error.clear();
  if (night == original_) { owned_ = false; ::unlink(options_.journal.c_str()); }
  return true;
}
bool Controller::refresh(std::string &error) {
  std::lock_guard<std::mutex> guard(mutex_);
  const bool ok = state_.native ? (device_ && device_->refresh(error)) : full_refresh(options_.simulator, error);
  if (ok) { state_.refresh_pending = false; state_.error.clear(); }
  return ok;
}
bool Controller::restore_locked(std::string &error) {
  if (!owned_) return true;
  bool current;
  if (!device_->read(backend_, current, error)) return false;
  if (current != last_) { owned_ = false; ::unlink(options_.journal.c_str()); return true; }
  if (current != original_ && !write_verified(backend_, original_, error)) return false;
  if (!device_->refresh(error)) return false;
  owned_ = false; state_.night = original_; state_.refresh_pending = false;
  ::unlink(options_.journal.c_str()); return true;
}
bool Controller::restore(std::string &error) { std::lock_guard<std::mutex> guard(mutex_); return restore_locked(error); }
std::string Controller::settings_json(std::string settings) {
  const auto value = state();
  {
    const auto at = settings.find("\"nightMode\":");
    if (at != std::string::npos) {
      const auto start = at + 12;
      const auto end = settings.find_first_of(",}", start);
      settings.replace(start, end - start, value.known && value.night ? "true" : "false");
    }
  }
  if (!settings.empty() && settings.back() == '}') settings.pop_back();
  return settings + ",\"nightNative\":" + (value.native ? "true" : "false") +
    ",\"nightAvailable\":" + (value.available ? "true" : "false") +
    ",\"nightKnown\":" + (value.known ? "true" : "false") +
    ",\"nightBackend\":" + quoted(value.backend) +
    ",\"nightRefreshPending\":" + (value.refresh_pending ? "true" : "false") +
    ",\"nightError\":" + quoted(value.error) + "}";
}
} // namespace kindle_display
