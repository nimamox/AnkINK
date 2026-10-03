#ifdef DISPLAY_TEST_POTION
#include "potion/http_server.hpp"
using Server = potion::HttpServer;
using Options = potion::ServerOptions;
#else
#include "ankink/http_server.hpp"
using Server = ankink::HttpServer;
using Options = ankink::ServerOptions;
#endif
#include <arpa/inet.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {
void check(bool condition, const char *message) { if (!condition) throw std::runtime_error(message); }
struct Fake final : kindle_display::Device {
  bool night{}, fb_night{}, framework_reverts{}, fail_refresh{}; int writes{}, refreshes{};
  bool read(kindle_display::Backend backend, bool &value, std::string &) override {
    value = backend == kindle_display::Backend::framework ? night : fb_night; return true;
  }
  bool write(kindle_display::Backend backend, bool value, std::string &) override {
    ++writes;
    if (backend == kindle_display::Backend::framebuffer) fb_night = value;
    else if (framework_reverts) night = false;
    else night = fb_night = value;
    return true;
  }
  bool refresh(std::string &error) override { ++refreshes; if (fail_refresh) { error = "test refresh failed"; return false; } return true; }
};
std::string request(unsigned port, const std::string &path, const std::string &body = "") {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0); check(fd >= 0, "socket");
  sockaddr_in address{}; address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(port);
  check(::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0, "connect");
  const auto wire = std::string(body.empty() ? "GET " : "POST ") + path +
    " HTTP/1.1\r\nHost: localhost\r\nContent-Length: " + std::to_string(body.size()) +
    "\r\nConnection: close\r\n\r\n" + body;
  check(::send(fd, wire.data(), wire.size(), 0) == static_cast<ssize_t>(wire.size()), "send");
  std::string result; char data[1024]; ssize_t n;
  while ((n = ::recv(fd, data, sizeof(data), 0)) > 0) result.append(data, n);
  ::close(fd); return result;
}
struct Running {
  Server server; std::thread thread; std::exception_ptr failure;
  explicit Running(Options options) : server(std::move(options)), thread([this] {
    try { server.run(); } catch (...) { failure = std::current_exception(); }
  }) {
    for (int i = 0; i < 200 && server.bound_port() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (!server.bound_port()) { server.stop(); thread.join(); if (failure) std::rethrow_exception(failure); throw std::runtime_error("listen unavailable"); }
  }
  ~Running() { server.stop(); thread.join(); }
  std::string get() { return request(server.bound_port(), "/api/settings"); }
  std::string set(const std::string &value) { return request(server.bound_port(), "/api/night-mode", "value=" + value); }
};
}
int main() {
  try {
    char directory[] = "/tmp/native-night-api-XXXXXX";
    check(::mkdtemp(directory), "temporary state");
    Options options; options.port = 0; options.data_dir = directory;
    options.display_journal = std::string(directory) + "/display.restore";
    auto fake = std::make_shared<Fake>(); options.display_device = fake;
#ifdef DISPLAY_TEST_POTION
    options.remote_setup_port = 0;
    options.image_cache_dir = std::string(directory) + "/images";
    options.page_cache_dir = std::string(directory) + "/pages";
#else
    options.collection_path = std::string(directory) + "/missing/collection.anki2";
#endif
    {
      std::ofstream config(std::string(directory) + "/state.conf");
      config << "nightMode=1\nnightPageMode=palette-images\nnightCardMode=palette-images\nfutureSetting=ignored\n";
    }
    {
      Running running(options);
      const auto initial = running.get();
      check(initial.find("\"nightMode\":false") != std::string::npos &&
            initial.find("\"nightNative\":true") != std::string::npos, "startup represents hardware instead of persisted night");
      check(initial.find("nightPageMode") == std::string::npos && initial.find("nightCardMode") == std::string::npos && fake->writes == 0, "obsolete config modes ignored and absent from API");
      check(running.set("1").find("200 OK") != std::string::npos && fake->night && fake->refreshes == 1, "explicit API transition");
      check(running.set("1").find("200 OK") != std::string::npos && fake->writes == 1, "API no-op");
      check(running.set("toggle").find("503") != std::string::npos && fake->writes == 1, "reject blind/invalid toggles");
      fake->fail_refresh = true;
      const auto failed = running.set("0");
      check(failed.find("503") != std::string::npos && failed.find("test refresh failed") != std::string::npos, "refresh failure visible to frontend");
      check(failed.find("\"nightMode\":false") != std::string::npos && failed.find("\"nightRefreshPending\":true") != std::string::npos, "failure response includes actual hardware state");
      fake->fail_refresh = false;
      check(running.set("0").find("200 OK") != std::string::npos && fake->writes == 2, "retry refresh without rewriting hardware");
      const auto legacy = request(running.server.bound_port(), "/api/settings", "key=nightMode&value=1");
      check(legacy.find("200 OK") != std::string::npos && fake->night, "legacy night setting requests use the native controller too");
    }
    check(!fake->night, "clean server destruction restores initial day");
    fake->night = fake->fb_night = true;
    {
      Running running(options);
      check(running.get().find("\"nightMode\":true") != std::string::npos, "already inverted startup");
    }
    check(fake->night, "untouched external inversion survives close");
    fake = std::make_shared<Fake>(); options.display_device = fake; fake->framework_reverts = true;
    {
      Running running(options);
      const auto response = running.set("1");
      check(response.find("200 OK") != std::string::npos &&
            response.find("\"nightMode\":true") != std::string::npos &&
            response.find("\"nightBackend\":\"framebuffer\"") != std::string::npos &&
            response.find("\"nightError\":\"\"") != std::string::npos &&
            fake->fb_night && fake->writes == 3 && fake->refreshes == 1,
            "frontend receives successful effective framebuffer state after early native reversion");
      check(running.set("1").find("200 OK") != std::string::npos && fake->writes == 3 && fake->refreshes == 1, "fallback API no-op does not retry native control");
      check(running.set("0").find("200 OK") != std::string::npos && !fake->fb_night, "day API uses selected framebuffer");
      check(running.set("1").find("200 OK") != std::string::npos, "repeated API toggle after native reversion");
    }
    check(!fake->fb_night, "fallback API cleanup restores actual initial framebuffer");
    options.simulator = true;
    const int writes = fake->writes;
    {
      Running running(options);
      check(running.get().find("\"nightNative\":false") != std::string::npos, "simulator reports no native display capability");
      check(running.get().find("\"nightKnown\":false") != std::string::npos, "simulator cannot claim saved state is hardware state");
      check(running.set("0").find("503") != std::string::npos && fake->writes == writes, "simulator rejects display requests without I/O");
    }
    options.simulator = false;
    for (const auto &old_mode : {"standard", "palette", "palette-images", "malformed-legacy-value"}) {
      { std::ofstream config(std::string(directory) + "/state.conf");
        config << "nightMode=1\nnightPageMode=" << old_mode << "\nnightCardMode=" << old_mode << "\npageButtonMode=reversed\n"; }
      {
        Running running(options);
        const auto json = running.get();
        check(json.find("\"nightMode\":false") != std::string::npos && json.find("nightPageMode") == std::string::npos && json.find("nightCardMode") == std::string::npos, "every old mode including malformed value is ignored on upgrade");
        check(json.find("\"pageButtonMode\":\"reversed\"") != std::string::npos, "unrelated settings survive upgrade");
        check(running.set("0").find("200 OK") != std::string::npos, "upgrade accepts explicit effective state");
        const std::string old_key =
#ifdef DISPLAY_TEST_POTION
          "nightPageMode";
#else
          "nightCardMode";
#endif
        check(request(running.server.bound_port(), "/api/settings", "key=" + old_key + "&value=palette").find("200 OK") == std::string::npos, "removed setting cannot be reintroduced by API");
      }
      std::ifstream saved(std::string(directory) + "/state.conf");
      const std::string body((std::istreambuf_iterator<char>(saved)), std::istreambuf_iterator<char>());
      check(body.find("nightPageMode") == std::string::npos && body.find("nightCardMode") == std::string::npos && body.find("nightMode=0") != std::string::npos, "resaving drops obsolete modes and keeps actual preference");
    }
    ::unlink((std::string(directory) + "/state.conf").c_str());
    ::unlink((options.display_journal + ".lock").c_str());
    ::rmdir((std::string(directory) + "/images").c_str());
    ::rmdir((std::string(directory) + "/pages").c_str());
    ::rmdir(directory);
    std::cout << "Native Night Mode API, legacy persistence and simulator tests passed\n";
    return 0;
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
