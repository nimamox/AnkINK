#include "kindle_display.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

using namespace kindle_display;
namespace {
void require(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
class Fake final : public Device {
public:
  bool framework{true}, framebuffer{true}, night{}, fb_night{};
  bool framework_affects_framebuffer{true}, fail_rollback{}, lose_on_refresh{};
  bool framework_reverts{}, unreadable_after_write{};
  bool fail_framework{}, fail_framebuffer{}, ignore_write{}, fail_read{}, fail_refresh{}, ambiguous{};
  int reads{}, writes{}, refreshes{};
  Backend last_backend{Backend::framework};
  bool read(Backend backend, bool &value, std::string &error) override {
    ++reads;
    if (fail_read || (unreadable_after_write && writes > 0 && backend == Backend::framework) ||
        !(backend == Backend::framework ? framework : framebuffer)) {
      error = "read unsupported"; return false;
    }
    value = backend == Backend::framework ? night : fb_night; return true;
  }
  bool write(Backend backend, bool value, std::string &error) override {
    ++writes; last_backend = backend;
    if ((backend == Backend::framework ? fail_framework : fail_framebuffer) ||
        (backend == Backend::framework && !value && fail_rollback)) {
      error = "write failed"; return false;
    }
    if (!ignore_write) {
      if (backend == Backend::framework) { night = framework_reverts ? false : value; if (framework_affects_framebuffer) fb_night = value; }
      else fb_night = value;
    }
    return true;
  }
  bool refresh(std::string &error) override {
    ++refreshes;
    if (fail_refresh) { error = "refresh failed"; return false; }
    if (lose_on_refresh) night = fb_night = false;
    return true;
  }
  bool fallback_safe() const override { return fail_framework && !ambiguous; }
};
}
int main() {
  char directory[] = "/tmp/kindle-display-test-XXXXXX";
  require(::mkdtemp(directory), "temporary directory");
  const std::string journal = std::string(directory) + "/restore";
  auto fake = std::make_shared<Fake>();
  Options options{false, journal, fake};
  std::string error;
  try {
    require(supports_eips_refresh("to flash display with current fb content: eips -s w=758,h=1024 -f"), "stock refresh-only capability");
    require(!supports_eips_refresh("to clear display: eips -c"), "reject paint-only stock command");
    int refresh_commands = 0;
    CommandRunner runner = [&](const std::vector<std::string> &args, std::string &, std::string &) {
      ++refresh_commands;
      require(args == std::vector<std::string>{"/usr/sbin/eips", "-s", "w=1072,h=1448", "-f"}, "one stock full refresh with visible dimensions and no inversion/painting flags");
      return true;
    };
    require(refresh_with_eips({1072, 1448, 1088, 6144, 0, 0}, runner, error) && refresh_commands == 1, "KOA1 visible size rather than stride/virtual size");
    CommandRunner landscape = [&](const std::vector<std::string> &args, std::string &, std::string &) {
      ++refresh_commands; require(args[2] == "w=1448,h=1072", "rotated refresh geometry"); return true;
    };
    require(refresh_with_eips({1448, 1072, 1448, 6144, 0, 0}, landscape, error), "rotation read fresh");
    for (auto g : {RefreshGeometry{0, 1448, 1088, 6144, 0, 0}, {1072, 0, 1088, 6144, 0, 0},
                   {1072, 1448, 1000, 6144, 0, 0}, {1072, 1448, 1088, 1000, 0, 0},
                   {1072, 1448, 1088, 6144, 2000, 0}, {1072, 1448, 1088, 6144, 0, 7000},
                   {16385, 1448, 20000, 6144, 0, 0}})
      require(!refresh_with_eips(g, runner, error), "invalid geometry never invokes firmware");
    require(refresh_commands == 2, "exactly one update per valid request");
    CommandRunner failed = [](const std::vector<std::string> &, std::string &, std::string &failure) {
      failure = "stock refresh failed"; return false;
    };
    require(!refresh_with_eips({1072, 1448, 1088, 6144, 0, 0}, failed, error) && error == "stock refresh failed", "stock failure propagated without another refresh mechanism");
    require(full_refresh(true, error), "host simulator refresh safe without stock tools");
    require(supports_inversion({"mxc_epdc_fb", 8, 1}), "normal EPDC capability");
    require(supports_inversion({"mxc_epdc_fb", 8, 2}), "inverted EPDC capability");
    for (auto invalid : {FramebufferInfo{"mxc_epdc_fb", 16, 1}, {"mxc_epdc_fb", 8, 0}, {"mxc_epdc_fb", 8, 3}, {"other", 8, 1}})
      require(!supports_inversion(invalid), "reject unsupported formats/drivers");
    {
      Controller controller(options);
      auto state = controller.state();
      require(state.available && state.known && !state.night && state.backend == "epdcMode", "prefer recognized native control");
      require(fake->writes == 0 && fake->refreshes == 0, "startup does not apply stale preference or refresh");
      const auto json = controller.settings_json("{\"nightMode\":true}");
      require(json.find("\"nightMode\":false") != std::string::npos, "actual hardware overrides stale persisted night");
      require(controller.set(true, error) && fake->writes == 1 && fake->refreshes == 1, "night: set then one refresh");
      require(controller.set(true, error) && fake->writes == 1 && fake->refreshes == 1, "night no-op");
      require(controller.set(false, error) && fake->writes == 2 && fake->refreshes == 2, "day: set then one refresh");
      require(controller.set(false, error) && fake->refreshes == 2, "day no-op");
    }
    fake = std::make_shared<Fake>(); options.device = fake;
    fake->night = fake->fb_night = true;
    {
      Controller controller(options);
      require(controller.state().night && fake->writes == 0, "preexisting verified framework inversion stays active");
      require(controller.set(false, error), "turn off preexisting night during app session");
    }
    require(fake->night && fake->writes == 2 && fake->refreshes == 2, "exit restores preexisting night rather than forcing day");
    fake = std::make_shared<Fake>(); options.device = fake; fake->night = true;
    {
      Controller controller(options);
      require(!controller.state().night && controller.state().backend == "framebuffer", "ineffective Y8INV at startup cannot override actual day framebuffer");
      require(fake->writes == 0 && fake->refreshes == 0, "mismatched startup state is read-only");
    }
    fake = std::make_shared<Fake>(); options.device = fake; fake->fb_night = true;
    {
      Controller controller(options);
      require(controller.state().backend == "framebuffer" && controller.state().night, "external fbdepth inversion takes precedence over stale Y8");
    }
    require(fake->fb_night && fake->writes == 0, "untouched outside state survives exit");
    fake = std::make_shared<Fake>(); options.device = fake; fake->framework = false;
    {
      Controller controller(options);
      require(controller.state().backend == "framebuffer", "missing framework -> validated framebuffer");
      require(controller.set(true, error) && fake->fb_night, "framebuffer inversion");
      fake->fb_night = false;
    }
    require(fake->writes == 1 && !fake->fb_night, "exit respects a subsequent outside state change");
    fake = std::make_shared<Fake>(); options.device = fake; fake->fail_framework = true;
    {
      Controller controller(options);
      require(controller.set(true, error) && fake->last_backend == Backend::framebuffer, "unchanged rejected framework request safely falls back");
    }
    fake = std::make_shared<Fake>(); options.device = fake; fake->framework_affects_framebuffer = false;
    {
      Controller controller(options);
      require(controller.set(true, error) && controller.state().backend == "framebuffer" && fake->fb_night && !fake->night, "KOA1 acknowledged property without global inversion: rollback both states before ioctl fallback");
      require(fake->writes == 3 && fake->refreshes == 1, "ineffective native path rolls back without redundant full refreshes");
    }
    require(!fake->fb_night, "framebuffer fallback restores baseline on exit");
    fake = std::make_shared<Fake>(); options.device = fake;
    fake->framework_affects_framebuffer = false; fake->framework_reverts = true;
    {
      Controller controller(options);
      require(controller.set(true, error) && controller.state().backend == "framebuffer" && fake->fb_night && !fake->night, "KOA1 property reverts before first verification: safely roll back and use framebuffer");
      require(fake->writes == 3 && fake->refreshes == 1, "early native reversion still gets exactly one final refresh");
      require(controller.set(false, error) && !fake->fb_night && fake->writes == 4 && fake->refreshes == 2, "day uses selected framebuffer without retrying ineffective native control");
    }
    fake = std::make_shared<Fake>(); options.device = fake;
    fake->framework_affects_framebuffer = false; fake->framework_reverts = fake->fail_rollback = true;
    {
      Controller controller(options);
      require(!controller.set(true, error) && fake->writes == 2 && fake->last_backend == Backend::framework && fake->refreshes == 0, "early reversion with failed rollback cannot fall back");
      fake->fail_rollback = false;
    }
    fake = std::make_shared<Fake>(); options.device = fake;
    fake->framework_affects_framebuffer = false; fake->framework_reverts = true; fake->framebuffer = false;
    {
      Controller controller(options);
      require(!controller.set(true, error) && fake->writes == 0 && fake->refreshes == 0, "unsupported framebuffer prevents any native request");
    }
    fake = std::make_shared<Fake>(); options.device = fake; fake->unreadable_after_write = true;
    {
      Controller controller(options);
      require(!controller.set(true, error) && fake->writes == 1 && fake->refreshes == 0, "acknowledged request with unknown state never triggers fallback");
      fake->unreadable_after_write = false;
    }
    fake = std::make_shared<Fake>(); options.device = fake; fake->framework_affects_framebuffer = false; fake->fail_rollback = true;
    {
      Controller controller(options);
      require(!controller.set(true, error) && fake->last_backend == Backend::framework && fake->refreshes == 0, "failed native rollback prevents fallback and success refresh");
      fake->fail_rollback = false;
    }
    fake = std::make_shared<Fake>(); options.device = fake; fake->lose_on_refresh = true;
    {
      Controller controller(options);
      require(!controller.set(true, error) && !controller.state().night && !error.empty(), "post-refresh state loss is visible as failure");
    }
    fake = std::make_shared<Fake>(); options.device = fake; fake->ignore_write = true;
    {
      Controller controller(options);
      require(!controller.set(true, error) && fake->writes == 3 && fake->refreshes == 0, "ignored framebuffer write after verified native rollback never reports success or refreshes");
      require(!controller.state().night, "failed ioctl/readback does not report requested state");
    }
    fake = std::make_shared<Fake>(); options.device = fake; fake->fail_framework = fake->ambiguous = true;
    {
      Controller controller(options);
      require(!controller.set(true, error) && fake->writes == 1 && fake->refreshes == 0, "ambiguous/timeout native failure never triggers framebuffer fallback");
    }
    fake = std::make_shared<Fake>(); options.device = fake; fake->fail_framework = fake->fail_framebuffer = true;
    {
      Controller controller(options);
      require(!controller.set(true, error) && fake->refreshes == 0, "failed native/ioctl has no success refresh");
    }
    fake = std::make_shared<Fake>(); options.device = fake; fake->fail_refresh = true;
    {
      Controller controller(options);
      require(!controller.set(true, error), "failed full refresh is reported");
      const auto state = controller.state();
      require(state.night && state.refresh_pending && !state.error.empty(), "refresh failure still reports actual changed hardware");
      fake->fail_refresh = false;
      require(controller.set(true, error) && fake->writes == 1 && fake->refreshes == 2, "retry only pending refresh");
    }
    fake = std::make_shared<Fake>(); options.device = fake;
    {
      Controller first(options);
      Controller second(options);
      require(!second.state().available && !second.set(true, error), "two apps cannot concurrently own global inversion");
    }
    {
      auto first = std::make_unique<Controller>(options);
      Controller second(options);
      first.reset();
      require(second.state().available, "event-driven retry after previous owner finishes shutdown");
    }
    fake = std::make_shared<Fake>(); options.device = fake;
    const auto child = ::fork(); require(child >= 0, "fork crash fixture");
    if (child == 0) { Controller controller(options); std::string child_error; ::_exit(controller.set(true, child_error) ? 0 : 1); }
    int status; ::waitpid(child, &status, 0);
    require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "unclean child left journal");
    fake->night = fake->fb_night = true;
    {
      Controller controller(options);
      require(!controller.state().night && fake->writes == 1 && fake->refreshes == 1, "next launch recovers interrupted session before taking baseline");
    }
    fake = std::make_shared<Fake>(); options.device = fake; options.simulator = true;
    {
      Controller controller(options);
      require(!controller.state().native && controller.refresh(error), "simulator reports unavailable display control and safe refresh");
      require(!controller.state().known && !controller.set(true, error), "simulator cannot pretend inversion succeeded");
      require(controller.settings_json("{\"nightMode\":true}").find("\"nightMode\":false") != std::string::npos, "simulator does not expose stale saved inversion");
      require(fake->reads == 0 && fake->writes == 0 && fake->refreshes == 0, "simulator never accesses display device");
    }
    options.simulator = false; fake->framework = fake->framebuffer = false;
    {
      Controller controller(options);
      require(!controller.state().available && !controller.set(true, error) && fake->writes == 0, "capability detection fails safely");
    }
    std::ofstream(journal) << "invalid data\n";
    {
      Controller controller(options);
      require(!controller.state().available && fake->writes == 0, "malformed journal cannot trigger hardware changes");
    }
    ::unlink(journal.c_str()); ::unlink((journal + ".lock").c_str()); ::rmdir(directory);
    std::cout << "Display capability, transitions, failures, simulator, ownership and recovery passed\n";
    return 0;
  } catch (const std::exception &failure) { std::cerr << failure.what() << '\n'; return 1; }
}
