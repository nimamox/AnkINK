#include "ankink/http_server.hpp"
#include "ankink/orientation.hpp"

#include <arpa/inet.h>
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>
namespace ankink {
namespace {
volatile std::sig_atomic_t stop_requested = 0;
volatile std::sig_atomic_t stop_wake_fd = -1;
struct Request { std::string method, target, body; };

std::string trim(const std::string &value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  return first == std::string::npos ? std::string{} :
    value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
std::string url_decode(const std::string &value) {
  std::string result;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '+') result.push_back(' ');
    else if (value[i] == '%' && i + 2 < value.size() &&
             hex_value(value[i + 1]) >= 0 && hex_value(value[i + 2]) >= 0) {
      result.push_back(static_cast<char>((hex_value(value[i + 1]) << 4) |
                                         hex_value(value[i + 2])));
      i += 2;
    } else result.push_back(value[i]);
  }
  return result;
}
std::string form_value(const std::string &body, const std::string &name) {
  std::size_t start = 0;
  while (start <= body.size()) {
    const auto end = body.find('&', start);
    const std::string pair = body.substr(start, end == std::string::npos ? end : end - start);
    const auto equals = pair.find('=');
    if (url_decode(pair.substr(0, equals)) == name)
      return equals == std::string::npos ? std::string{} : url_decode(pair.substr(equals + 1));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return {};
}
bool send_all(int fd, const std::string &data) {
#ifdef MSG_NOSIGNAL
  constexpr int send_flags = MSG_NOSIGNAL;
#else
  constexpr int send_flags = 0;
#endif
  std::size_t sent = 0;
  while (sent < data.size()) {
    const auto count =
        ::send(fd, data.data() + sent, data.size() - sent, send_flags);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    sent += static_cast<std::size_t>(count);
  }
  return true;
}
void respond(int fd, int status, const char *reason, const std::string &type,
             const std::string &body) {
  std::ostringstream out;
  out << "HTTP/1.1 " << status << ' ' << reason << "\r\n"
      << "Content-Type: " << type << "\r\nContent-Length: " << body.size()
      << "\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *"
      << "\r\nAccess-Control-Allow-Methods: GET, POST, OPTIONS"
      << "\r\nAccess-Control-Allow-Headers: Content-Type"
      << "\r\nConnection: close\r\n\r\n";
  send_all(fd, out.str()); send_all(fd, body);
}
bool read_request(int fd, Request &request) {
  std::string input;
  char buffer[4096];
  std::size_t split = std::string::npos;
  while (input.size() < 65536 && split == std::string::npos) {
    const auto count = ::recv(fd, buffer, sizeof(buffer), 0);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    input.append(buffer, static_cast<std::size_t>(count));
    split = input.find("\r\n\r\n");
  }
  if (split == std::string::npos) return false;
  std::istringstream stream(input.substr(0, split));
  std::string line, version;
  if (!std::getline(stream, line)) return false;
  std::istringstream first(line);
  if (!(first >> request.method >> request.target >> version)) return false;
  std::size_t content_length = 0;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const auto colon = line.find(':');
    if (colon == std::string::npos) continue;
    std::string name = line.substr(0, colon);
    for (char &c : name) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    if (name == "content-length") {
      const auto parsed = std::stoul(trim(line.substr(colon + 1)));
      if (parsed > 65536) return false;
      content_length = parsed;
    }
  }
  const std::size_t body_start = split + 4;
  while (input.size() - body_start < content_length) {
    const auto count = ::recv(fd, buffer, sizeof(buffer), 0);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return false;
    input.append(buffer, static_cast<std::size_t>(count));
  }
  request.body = input.substr(body_start, content_length);
  const auto query = request.target.find('?');
  if (query != std::string::npos) request.target.resize(query);
  return true;
}
std::string read_file(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return {};
  std::ostringstream out; out << file.rdbuf(); return out.str();
}
std::string mime_type(const std::string &path) {
  if (path.size() >= 5 && path.substr(path.size() - 5) == ".html") return "text/html; charset=utf-8";
  if (path.size() >= 4 && path.substr(path.size() - 4) == ".css") return "text/css; charset=utf-8";
  if (path.size() >= 3 && path.substr(path.size() - 3) == ".js") return "application/javascript; charset=utf-8";
  if (path.size() >= 4 && path.substr(path.size() - 4) == ".png") return "image/png";
  if (path.size() >= 4 && path.substr(path.size() - 4) == ".jpg") return "image/jpeg";
  if (path.size() >= 5 && path.substr(path.size() - 5) == ".jpeg") return "image/jpeg";
  if (path.size() >= 4 && path.substr(path.size() - 4) == ".gif") return "image/gif";
  if (path.size() >= 5 && path.substr(path.size() - 5) == ".webp") return "image/webp";
  if (path.size() >= 4 && path.substr(path.size() - 4) == ".svg") return "image/svg+xml";
  return "application/octet-stream";
}
std::string base64(const std::string &value) {
  static const char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string output;
  output.reserve(((value.size() + 2) / 3) * 4);
  for (std::size_t i = 0; i < value.size(); i += 3) {
    const unsigned a = static_cast<unsigned char>(value[i]);
    const unsigned b = i + 1 < value.size() ? static_cast<unsigned char>(value[i + 1]) : 0;
    const unsigned c = i + 2 < value.size() ? static_cast<unsigned char>(value[i + 2]) : 0;
    const unsigned bits = (a << 16) | (b << 8) | c;
    output.push_back(alphabet[(bits >> 18) & 63]);
    output.push_back(alphabet[(bits >> 12) & 63]);
    output.push_back(i + 1 < value.size() ? alphabet[(bits >> 6) & 63] : '=');
    output.push_back(i + 2 < value.size() ? alphabet[bits & 63] : '=');
  }
  return output;
}
std::int64_t integer(const std::string &value, const char *name) {
  if (value.empty()) throw std::runtime_error(std::string("missing ") + name);
  std::size_t used = 0; const auto result = std::stoll(value, &used);
  if (used != value.size()) throw std::runtime_error(std::string("invalid ") + name);
  return result;
}
bool json_type(const std::string &body, const char *type) {
  return body.find(std::string(R"("type":")") + type + '"') !=
         std::string::npos;
}
std::string with_app_state(std::string body, std::uint64_t count,
                           bool state_persisted = true) {
  const auto closing = body.rfind('}');
  if (closing != std::string::npos)
    body.insert(closing, R"(,"pendingReviews":)" + std::to_string(count) +
                             R"(,"statePersisted":)" +
                             (state_persisted ? "true" : "false"));
  return body;
}
} // namespace

HttpServer::HttpServer(ServerOptions options)
    : options_(std::move(options)), app_state_(options_.data_dir) {
  collection_.open(options_.collection_path, collection_error_);
  if (!options_.simulator) {
    std::string rotation_error;
    const std::string mode = app_state_.setting("rotationMode");
    if (!apply_kindle_rotation(mode, rotation_error))
      std::cerr << "Rotation restore: " << rotation_error << '\n';
  }
}

HttpServer::~HttpServer() {
  stop();
  if (!options_.simulator) {
    std::string ignored;
    apply_kindle_rotation("auto", ignored);
  }
}

void HttpServer::request_stop() noexcept {
  stop_requested = 1;
  const int fd = stop_wake_fd;
  if (fd >= 0) {
    const char byte = 1;
    const auto ignored = ::write(fd, &byte, 1);
    (void)ignored;
  }
}

std::uint16_t HttpServer::bound_port() const noexcept { return bound_port_.load(); }

void HttpServer::wake_listener() noexcept {
  if (wake_write_ < 0) return;
  const char byte = 1;
  const auto ignored = ::write(wake_write_, &byte, 1);
  (void)ignored;
}

void HttpServer::close_wakeup_pipe() noexcept {
  stop_wake_fd = -1;
  if (wake_read_ >= 0) ::close(wake_read_);
  if (wake_write_ >= 0) ::close(wake_write_);
  wake_read_ = wake_write_ = -1;
}

void HttpServer::stop() noexcept {
  if (stopping_.exchange(true)) return;
  pending_clients_condition_.notify_all();
  wake_listener();
  std::lock_guard<std::mutex> lock(clients_mutex_);
  for (const int client : active_clients_) ::shutdown(client, SHUT_RDWR);
}

void HttpServer::worker_loop() noexcept {
  for (;;) {
    int client = -1;
    {
      std::unique_lock<std::mutex> lock(pending_clients_mutex_);
      pending_clients_condition_.wait(lock, [this] {
        return stopping_.load() || !pending_clients_.empty();
      });
      if (pending_clients_.empty()) return;
      client = pending_clients_.front();
      pending_clients_.pop_front();
    }
    if (stopping_.load()) {
      {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        active_clients_.erase(client);
      }
      ::close(client);
    } else {
      handle_client(client);
    }
  }
}

void HttpServer::handle_client(int client) noexcept {
  const auto collection_call = [this](auto operation) {
    std::lock_guard<std::mutex> lock(collection_mutex_);
    if (options_.collection_operation_delay.count() > 0)
      std::this_thread::sleep_for(options_.collection_operation_delay);
    return operation();
  };

  try {
    Request request;
    if (!read_request(client, request)) {
      respond(client, 400, "Bad Request", "application/json",
              R"({"type":"error","message":"Invalid request"})");
    } else if (request.method == "OPTIONS") {
      respond(client, 204, "No Content", "text/plain", "");
    } else if (request.method == "GET" &&
               (request.target == "/api/status" || request.target == "/health")) {
      const std::string body = collection_call([this] {
        return std::string(R"({"type":"status","version":")") + ANKINK_VERSION +
          R"(","collectionOpen":)" + (collection_.is_open() ? "true" : "false") +
          R"(,"authenticated":)" + (collection_.is_authenticated() ? "true" : "false") +
          R"(,"collection":)" + json_string(options_.collection_path) +
          R"(,"pendingReviews":)" + std::to_string(app_state_.pending_reviews()) +
          R"(,"error":)" + json_string(collection_error_) + "}";
      });
      respond(client, 200, "OK", "application/json; charset=utf-8", body);
    } else if (request.method == "GET" && request.target == "/api/settings") {
      respond(client, 200, "OK", "application/json; charset=utf-8",
              app_state_.settings_json());
    } else if (request.method == "POST" && request.target == "/api/settings") {
      const std::string key = form_value(request.body, "key");
      const std::string value = form_value(request.body, "value");
      const std::string previous_rotation = app_state_.setting("rotationMode");
      std::string error;
      if (!app_state_.set_setting(key, value, error))
        throw std::runtime_error(error);
      if (key == "rotationMode" && !options_.simulator &&
          !apply_kindle_rotation(value, error)) {
        std::string rollback_error;
        app_state_.set_setting("rotationMode", previous_rotation,
                               rollback_error);
        throw std::runtime_error(error);
      }
      respond(client, 200, "OK", "application/json; charset=utf-8",
              app_state_.settings_json());
    } else if (request.method == "GET" && request.target == "/api/decks") {
      const auto result = collection_call([this] {
        const bool open = collection_.is_open();
        const std::string body = open ? collection_.decks_json() :
          std::string(R"({"type":"decks","path":)") + json_string(options_.collection_path) +
          R"(,"decks":[],"error":)" + json_string(collection_error_) + "}";
        return std::make_pair(open, body);
      });
      respond(client, result.first ? 200 : 503,
              result.first ? "OK" : "Service Unavailable",
              "application/json; charset=utf-8", result.second);
    } else if (request.method == "GET" &&
               request.target == "/api/review-activity") {
      const auto result = collection_call([this] {
        const bool open = collection_.is_open();
        return std::make_pair(
            open, open ? collection_.review_activity_json()
                       : R"({"type":"error","message":"No collection is open"})");
      });
      respond(client, result.first ? 200 : 503,
              result.first ? "OK" : "Service Unavailable",
              "application/json; charset=utf-8", result.second);
    } else if (request.method == "GET" &&
               request.target.compare(0, 11, "/api/decks/") == 0 &&
               request.target.size() > 16 &&
               request.target.substr(request.target.size() - 5) == "/next") {
      const auto id = integer(request.target.substr(11, request.target.size() - 16), "deck id");
      const std::string body = collection_call([this, id] { return collection_.next_card_json(id); });
      respond(client, 200, "OK", "application/json; charset=utf-8", body);
    } else if (request.method == "POST" && request.target == "/api/answer") {
      const auto card = integer(form_value(request.body, "card"), "card");
      const auto token = integer(form_value(request.body, "token"), "review token");
      const auto rating = integer(form_value(request.body, "rating"), "rating");
      const std::string body = collection_call([this, card, token, rating] {
        std::string result = collection_.answer_json(
            card, static_cast<std::uint64_t>(token), static_cast<int>(rating));
        const bool persisted = !json_type(result, "answered") ||
                               app_state_.record_answer();
        return with_app_state(std::move(result), app_state_.pending_reviews(),
                              persisted);
      });
      respond(client, 200, "OK", "application/json; charset=utf-8", body);
    } else if (request.method == "POST" && request.target == "/api/undo") {
      const std::string body = collection_call([this] {
        std::string result = collection_.undo_json();
        const bool persisted = !json_type(result, "undone") ||
                               app_state_.record_undo();
        return with_app_state(std::move(result), app_state_.pending_reviews(),
                              persisted);
      });
      respond(client, 200, "OK", "application/json; charset=utf-8", body);
    } else if (request.method == "POST" && request.target == "/api/quit") {
      respond(client, 200, "OK", "application/json; charset=utf-8", R"({"type":"quitting"})");
      stop();
    } else if (request.method == "POST" && request.target == "/api/refresh") {
      if (options_.simulator) {
        respond(client, 200, "OK", "application/json; charset=utf-8",
                R"({"type":"refreshed","simulated":true})");
      } else {
        const int result = std::system(
            "if [ -x /usr/bin/fbink ]; then /usr/bin/fbink -q -f -s; "
            "elif [ -x /mnt/us/extensions/MRInstaller/bin/PW2/fbink ]; then "
            "/mnt/us/extensions/MRInstaller/bin/PW2/fbink -q -f -s; "
            "else exit 1; fi >/dev/null 2>&1");
        if (result != 0)
          throw std::runtime_error("Full refresh requires a working /usr/bin/fbink command");
        respond(client, 200, "OK", "application/json; charset=utf-8", R"({"type":"refreshed"})");
      }
    } else if (request.method == "POST" && request.target == "/api/auth/login") {
      const std::string username = form_value(request.body, "username");
      const std::string password = form_value(request.body, "password");
      if (username.empty() || password.empty())
        throw std::runtime_error("username and password are required");
      const std::string body = collection_call([this, &username, &password] {
        return collection_.login_json(username, password);
      });
      respond(client, 200, "OK", "application/json; charset=utf-8", body);
    } else if (request.method == "POST" && request.target == "/api/auth/logout") {
      const std::string body = collection_call([this] { return collection_.logout_json(); });
      respond(client, 200, "OK", "application/json; charset=utf-8", body);
    } else if (request.method == "POST" && request.target == "/api/sync") {
      const std::string body = collection_call([this] {
        std::string result = collection_.sync_json();
        const bool completed = json_type(result, "sync") &&
          result.find(R"("required":"none")") != std::string::npos;
        const bool persisted = !completed || app_state_.record_sync();
        return with_app_state(std::move(result), app_state_.pending_reviews(),
                              persisted);
      });
      respond(client, 200, "OK", "application/json; charset=utf-8", body);
    } else if (request.method == "POST" && request.target == "/api/sync/full-download") {
      const std::string body = collection_call([this] {
        std::string result = collection_.full_download_json();
        const bool completed = json_type(result, "sync");
        const bool persisted = !completed || app_state_.record_sync();
        return with_app_state(std::move(result), app_state_.pending_reviews(),
                              persisted);
      });
      respond(client, 200, "OK", "application/json; charset=utf-8", body);
    } else if (request.method == "GET" &&
               (request.target.compare(0, 11, "/api/media/") == 0 ||
                request.target.compare(0, 16, "/api/media-data/") == 0)) {
      const bool as_data = request.target.compare(0, 16, "/api/media-data/") == 0;
      const std::string name = url_decode(request.target.substr(as_data ? 16 : 11));
      if (name.empty() || name.find("..") != std::string::npos ||
          name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        throw std::runtime_error("invalid media filename");
      std::string media_dir = options_.collection_path;
      const auto dot = media_dir.rfind('.');
      if (dot != std::string::npos) media_dir.resize(dot);
      media_dir += ".media/";
      const std::string body = read_file(media_dir + name);
      if (body.empty()) respond(client, 404, "Not Found", "text/plain", "Not found\n");
      else if (as_data)
        respond(client, 200, "OK", "application/json; charset=utf-8",
                "{\"data\":\"data:" + mime_type(name) + ";base64," + base64(body) + "\"}");
      else respond(client, 200, "OK", mime_type(name), body);
    } else if (request.method == "GET" && options_.simulator &&
               (request.target == "/simulator" ||
                request.target.compare(0, 11, "/simulator/") == 0)) {
      const std::string relative =
          request.target == "/simulator" || request.target == "/simulator/"
              ? "index.html" : request.target.substr(11);
      if (relative.find("..") != std::string::npos)
        throw std::runtime_error("invalid simulator asset path");
      const std::string body = read_file(options_.simulator_asset_dir + "/" + relative);
      if (body.empty()) respond(client, 404, "Not Found", "text/plain", "Not found\n");
      else respond(client, 200, "OK", mime_type(relative), body);
    } else if (request.method == "GET" && request.target.find("..") == std::string::npos) {
      const std::string relative = request.target == "/" ? "index.html" : request.target.substr(1);
      const std::string body = read_file(options_.asset_dir + "/" + relative);
      if (body.empty()) respond(client, 404, "Not Found", "text/plain", "Not found\n");
      else respond(client, 200, "OK", mime_type(relative), body);
    } else {
      respond(client, 404, "Not Found", "application/json",
              R"({"type":"error","message":"Not found"})");
    }
  } catch (const std::exception &exception) {
    respond(client, 400, "Bad Request", "application/json; charset=utf-8",
            std::string(R"({"type":"error","message":)") +
                json_string(exception.what()) + "}");
  }

  {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    active_clients_.erase(client);
  }
  ::close(client);
}

int HttpServer::run() {
  stop_requested = 0;
  const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
  int wake_pipe[2];
  if (::pipe(wake_pipe) != 0) {
    ::close(listener);
    throw std::runtime_error(std::string("pipe: ") + std::strerror(errno));
  }
  wake_read_ = wake_pipe[0]; wake_write_ = wake_pipe[1];
  stop_wake_fd = wake_write_;
  ::fcntl(wake_read_, F_SETFL, ::fcntl(wake_read_, F_GETFL) | O_NONBLOCK);
  ::fcntl(wake_write_, F_SETFL, ::fcntl(wake_write_, F_GETFL) | O_NONBLOCK);

  int reuse = 1; setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address{}; address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(options_.port);
  if (::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
    const std::string error = std::strerror(errno); ::close(listener); close_wakeup_pipe();
    throw std::runtime_error("bind: " + error);
  }
  if (::listen(listener, 16) != 0) {
    ::close(listener); close_wakeup_pipe(); throw std::runtime_error("listen failed");
  }
  socklen_t address_size = sizeof(address);
  if (::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &address_size) == 0)
    bound_port_.store(ntohs(address.sin_port));
  std::cout << "AnkINK daemon listening at http://127.0.0.1:" << bound_port() << "/\n";
  {
    std::lock_guard<std::mutex> lock(collection_mutex_);
    if (!collection_.is_open()) std::cerr << "AnkINK collection: " << collection_error_ << '\n';
  }

  try {
    const std::size_t worker_count = std::max<std::size_t>(2, options_.worker_count);
    workers_.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i)
      workers_.emplace_back(&HttpServer::worker_loop, this);
  } catch (...) {
    stop();
    for (auto &worker : workers_) if (worker.joinable()) worker.join();
    ::close(listener); close_wakeup_pipe();
    throw;
  }

  while (!stopping_.load() && !stop_requested) {
    fd_set set; FD_ZERO(&set); FD_SET(listener, &set); FD_SET(wake_read_, &set);
    int highest = std::max(listener, wake_read_);
    const int ready = select(highest + 1, &set, nullptr, nullptr, nullptr);
    if (ready < 0 && errno == EINTR) continue;
    if (ready <= 0) continue;
    if (FD_ISSET(wake_read_, &set)) {
      char bytes[32]; while (::read(wake_read_, bytes, sizeof(bytes)) > 0) {}
    }
    if (stopping_.load() || stop_requested || !FD_ISSET(listener, &set)) continue;
    const int client = ::accept(listener, nullptr, nullptr);
    if (client < 0) continue;
#ifdef SO_NOSIGPIPE
    int no_sigpipe = 1;
    setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe,
               sizeof(no_sigpipe));
#endif
    timeval io_timeout{5, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &io_timeout, sizeof(io_timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &io_timeout, sizeof(io_timeout));
    {
      std::lock_guard<std::mutex> clients_lock(clients_mutex_);
      if (stopping_.load()) { ::close(client); continue; }
      active_clients_.insert(client);
      std::lock_guard<std::mutex> pending_lock(pending_clients_mutex_);
      pending_clients_.push_back(client);
    }
    pending_clients_condition_.notify_one();
  }

  stop();
  ::close(listener);
  for (auto &worker : workers_) if (worker.joinable()) worker.join();
  workers_.clear();
  stop_wake_fd = -1;
  close_wakeup_pipe();
  bound_port_.store(0);
  return 0;
}
} // namespace ankink
