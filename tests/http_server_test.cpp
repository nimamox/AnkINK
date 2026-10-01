#include "ankink/http_server.hpp"
#include "ankink/orientation.hpp"
#include "ankink/update_checker.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <future>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {
using namespace std::chrono_literals;

struct Response {
  int status{};
  std::string body;
};

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

Response request(std::uint16_t port, const std::string &method,
                 const std::string &target, const std::string &body = {}) {
  const int socket = ::socket(AF_INET, SOCK_STREAM, 0);
  if (socket < 0) throw std::runtime_error("test socket failed");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (::connect(socket, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
    ::close(socket);
    throw std::runtime_error(std::string("test connect failed: ") + std::strerror(errno));
  }
  const std::string wire = method + " " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n" +
    "Content-Type: application/x-www-form-urlencoded\r\nContent-Length: " +
    std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
  std::size_t sent = 0;
  while (sent < wire.size()) {
    const auto count = ::send(socket, wire.data() + sent, wire.size() - sent, 0);
    if (count <= 0) { ::close(socket); throw std::runtime_error("test send failed"); }
    sent += static_cast<std::size_t>(count);
  }
  std::string received;
  char buffer[4096];
  for (;;) {
    const auto count = ::recv(socket, buffer, sizeof(buffer), 0);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) break;
    received.append(buffer, static_cast<std::size_t>(count));
  }
  ::close(socket);
  Response response;
  const auto first_space = received.find(' ');
  if (first_space != std::string::npos) response.status = std::stoi(received.substr(first_space + 1, 3));
  const auto split = received.find("\r\n\r\n");
  if (split != std::string::npos) response.body = received.substr(split + 4);
  return response;
}

class RunningServer {
public:
  explicit RunningServer(ankink::ServerOptions options) : server_(std::move(options)) {
    thread_ = std::thread([this] {
      try { server_.run(); } catch (...) { failure_ = std::current_exception(); }
    });
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!server_.bound_port() && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(5ms);
    if (!server_.bound_port()) {
      shutdown();
      throw std::runtime_error("server did not start");
    }
  }

  ~RunningServer() {
    server_.stop();
    if (thread_.joinable()) thread_.join();
  }
  RunningServer(const RunningServer &) = delete;
  RunningServer &operator=(const RunningServer &) = delete;

  std::uint16_t port() const { return server_.bound_port(); }

  void shutdown() {
    server_.stop();
    if (thread_.joinable()) thread_.join();
    if (failure_) {
      const auto failure = failure_;
      failure_ = nullptr;
      std::rethrow_exception(failure);
    }
  }

private:
  ankink::HttpServer server_;
  std::thread thread_;
  std::exception_ptr failure_;
};

ankink::ServerOptions options() {
  static std::atomic<unsigned> sequence{0};
  ankink::ServerOptions result;
  result.port = 0;
  result.simulator = true;
  result.collection_path = "/tmp/ankink-http-server-test-missing/collection.anki2";
  result.data_dir = "/tmp/ankink-http-server-test-" +
    std::to_string(static_cast<long long>(::getpid())) + "-" +
    std::to_string(sequence.fetch_add(1));
  return result;
}

void remove_state_directory(const std::string &directory) {
  ::unlink((directory + "/state.conf").c_str());
  ::unlink((directory + "/state.conf.tmp").c_str());
  ::rmdir(directory.c_str());
}

void settings_are_daemon_persisted() {
  const auto configured = options();
  {
    RunningServer server(configured);
    const auto defaults = request(server.port(), "GET", "/api/settings");
    require(defaults.status == 200, "settings defaults status");
    require(defaults.body.find(R"("cardFont":"Bookerly")") != std::string::npos,
            "settings default card font");
    require(defaults.body.find(R"("pendingReviews":0)") != std::string::npos,
            "settings default pending reviews");
    require(defaults.body.find(R"("rotationMode":"auto")") != std::string::npos,
            "settings default auto rotation");
    require(request(server.port(), "POST", "/api/settings",
                    "key=cardFont&value=Amazon%20Ember").status == 200,
            "save card font setting");
    require(request(server.port(), "POST", "/api/settings",
                    "key=fontScale&value=1.25").status == 200,
            "save font scale setting");
    require(request(server.port(), "POST", "/api/settings",
                    "key=rotationMode&value=locked").status == 200,
            "save rotation mode setting");
    require(request(server.port(), "POST", "/api/settings",
                    "key=unknown&value=x").status == 400,
            "reject unknown setting");
  }
  {
    RunningServer server(configured);
    const auto restored = request(server.port(), "GET", "/api/settings");
    require(restored.body.find(R"("cardFont":"Amazon Ember")") != std::string::npos,
            "restore card font setting");
    require(restored.body.find(R"("fontScale":"1.25")") != std::string::npos,
            "restore font scale setting");
    require(restored.body.find(R"("rotationMode":"locked")") != std::string::npos,
            "restore rotation mode setting");
  }
  remove_state_directory(configured.data_dir);
}

void app_state_tracks_reviews() {
  const auto configured = options();
  {
    ankink::AppState state(configured.data_dir);
    state.record_answer();
    state.record_answer();
    require(state.pending_reviews() == 2, "record pending answers");
  }
  {
    ankink::AppState state(configured.data_dir);
    require(state.pending_reviews() == 2, "restore pending answers");
    state.record_undo();
    require(state.pending_reviews() == 1, "undo pending answer");
    state.record_sync();
    require(state.pending_reviews() == 0, "sync clears pending answers");
  }
  remove_state_directory(configured.data_dir);
}

void removed_input_endpoints_are_not_registered() {
  RunningServer server(options());
  require(request(server.port(), "GET", "/api/input").status == 404,
          "removed input endpoint must not be registered");
  require(request(server.port(), "POST", "/api/input/clear").status == 404,
          "removed input clear endpoint must not be registered");
  require(request(server.port(), "POST", "/api/simulator/input",
                  "action=forward").status == 404,
          "removed simulator input endpoint must not be registered");
}

void card_action_endpoint_is_registered() {
  RunningServer server(options());
  const auto response = request(server.port(), "POST", "/api/card-action",
                                "card=0&token=0&action=flag-red");
  require(response.status == 200, "card action endpoint status");
  require(response.body.find("No collection is open") != std::string::npos,
          "card action endpoint did not reach the collection handler");
}

void collection_requests_are_serialized() {
  auto configured = options();
  configured.collection_operation_delay = 150ms;
  RunningServer server(std::move(configured));
  std::promise<void> start;
  const auto gate = start.get_future().share();
  auto first = std::async(std::launch::async, [&] { gate.wait(); return request(server.port(), "GET", "/api/status"); });
  auto second = std::async(std::launch::async, [&] { gate.wait(); return request(server.port(), "GET", "/api/decks"); });
  const auto before = std::chrono::steady_clock::now();
  start.set_value();
  const auto first_response = first.get();
  const auto second_response = second.get();
  const auto elapsed = std::chrono::steady_clock::now() - before;
  require(first_response.status != 0 && second_response.status != 0,
          "serialized collection responses");
  require(elapsed >= 250ms, "collection operations must not overlap");
}

void update_status_is_non_blocking_and_dismissible() {
  auto configured = options();
  configured.update_checks_enabled = true;
  std::atomic<unsigned> checks{0};
  std::atomic<bool> payload_valid{true};
  configured.device_telemetry = [] {
    return ankink::DeviceTelemetry{
        "Test Kindle", "5.test", "3.test", "arm-test"};
  };
  configured.update_poster =
      [&](const std::string &endpoint, const std::string &body,
          std::string &response) {
        if (endpoint !=
                "https://telemetry.nimamo.workers.dev/api/v1/check" ||
            body.find(R"("app":"ankink")") == std::string::npos ||
            body.find(R"("appVersion":"0.4.2")") == std::string::npos ||
            body.find(R"("deviceModel":"Test Kindle")") ==
                std::string::npos ||
            body.find(R"("buildType":"development")") ==
                std::string::npos)
          payload_valid.store(false);
        checks.fetch_add(1);
        response = R"({"checked":true,"currentVersion":"0.4.2","latestVersion":"0.5.0"})";
        return true;
      };

  std::string first_id;
  {
    RunningServer server(configured);
    Response status;
    for (int i = 0; i < 100; ++i) {
      status = request(server.port(), "GET", "/api/update-status");
      if (status.body.find(R"("checked":true)") != std::string::npos) break;
      std::this_thread::sleep_for(5ms);
    }
    require(status.status == 200 &&
                status.body.find(R"("updateAvailable":true)") !=
                    std::string::npos &&
                status.body.find(R"("dismissed":false)") !=
                    std::string::npos,
            "newer update is exposed through the local API");
    require(request(server.port(), "POST",
                    "/api/update-status/dismiss").body.find(
                        R"("dismissed":true)") != std::string::npos,
            "newer version can be dismissed");
  }
  {
    std::ifstream input(configured.data_dir + "/install-id");
    std::getline(input, first_id);
  }
  require(first_id.size() == 36, "random installation UUID is persisted");
  {
    RunningServer server(configured);
    Response status;
    for (int i = 0; i < 100; ++i) {
      status = request(server.port(), "GET", "/api/update-status");
      if (status.body.find(R"("checked":true)") != std::string::npos) break;
      std::this_thread::sleep_for(5ms);
    }
    require(status.body.find(R"("dismissed":true)") != std::string::npos,
            "dismissal remains scoped to the reported target version");
  }
  std::string second_id;
  {
    std::ifstream input(configured.data_dir + "/install-id");
    std::getline(input, second_id);
  }
  require(payload_valid.load() && checks.load() == 2,
          "each backend start sends one valid asynchronous check");
  require(second_id == first_id,
          "the app-specific random installation UUID is reused");
  ::unlink((configured.data_dir + "/install-id").c_str());
  ::unlink((configured.data_dir + "/dismissed-update-version").c_str());
  remove_state_directory(configured.data_dir);
}

} // namespace

int main() {
#ifdef SIGPIPE
  std::signal(SIGPIPE, SIG_IGN);
#endif
  try {
    {
      std::vector<std::vector<std::string>> commands;
      const ankink::OrientationCommandRunner runner =
          [&commands](const std::vector<std::string> &arguments, std::string &) {
            commands.push_back(arguments);
            return true;
          };
      std::string error;
      require(ankink::apply_kindle_rotation("auto", runner, error),
              "apply Kindle auto rotation");
      require(commands.size() == 1 && commands[0].size() == 4 &&
                  commands[0][2] == "orientationLock" &&
                  commands[0][3] == "off",
              "auto rotation clears orientation lock");
      commands.clear();
      require(ankink::apply_kindle_rotation("locked", runner, error),
              "apply current-orientation lock");
      require(commands.size() == 1 && commands[0][3] == "current",
              "locked rotation captures current direction");
      commands.clear();
      require(!ankink::apply_kindle_rotation("sideways", runner, error) &&
                  commands.empty(),
              "invalid rotation does not invoke LIPC");
    }
    settings_are_daemon_persisted();
    app_state_tracks_reviews();
    removed_input_endpoints_are_not_registered();
    card_action_endpoint_is_registered();
    collection_requests_are_serialized();
    require(ankink::update_version_is_newer("0.4.0", "0.3.9") &&
                !ankink::update_version_is_newer("0.3.0", "0.3.0") &&
                !ankink::update_version_is_newer("0.4-beta", "0.3.0"),
            "strict semantic update version comparison");
    update_status_is_non_blocking_and_dismissible();
    std::cout << "http server tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "http server test failed: " << error.what() << '\n';
    return 1;
  }
}
