#include "ankink/http_server.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <stdexcept>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace ankink {
namespace {
volatile std::sig_atomic_t stop_requested = 0;
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
  std::size_t sent = 0;
  while (sent < data.size()) {
    const auto count = ::send(fd, data.data() + sent, data.size() - sent, 0);
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
std::int64_t integer(const std::string &value, const char *name) {
  if (value.empty()) throw std::runtime_error(std::string("missing ") + name);
  std::size_t used = 0; const auto result = std::stoll(value, &used);
  if (used != value.size()) throw std::runtime_error(std::string("invalid ") + name);
  return result;
}
} // namespace

HttpServer::HttpServer(ServerOptions options) : options_(std::move(options)) {
  collection_.open(options_.collection_path, collection_error_);
}
void HttpServer::request_stop() noexcept { stop_requested = 1; }

int HttpServer::run() {
  const int listener = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listener < 0) throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
  int reuse = 1; setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address{}; address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(options_.port);
  if (::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) != 0) {
    const std::string error = std::strerror(errno); ::close(listener);
    throw std::runtime_error("bind: " + error);
  }
  if (::listen(listener, 8) != 0) { ::close(listener); throw std::runtime_error("listen failed"); }
  std::cout << "AnkINK daemon listening at http://127.0.0.1:" << options_.port << "/\n";
  if (!collection_.is_open()) std::cerr << "AnkINK collection: " << collection_error_ << '\n';
  while (!stop_requested) {
    fd_set set; FD_ZERO(&set); FD_SET(listener, &set); timeval timeout{1, 0};
    const int ready = select(listener + 1, &set, nullptr, nullptr, &timeout);
    if (ready < 0 && errno == EINTR) continue;
    if (ready <= 0) continue;
    const int client = ::accept(listener, nullptr, nullptr);
    if (client < 0) continue;
    timeval io_timeout{5, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &io_timeout, sizeof(io_timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &io_timeout, sizeof(io_timeout));
    try {
      Request request;
      if (!read_request(client, request))
        respond(client, 400, "Bad Request", "application/json", R"({"type":"error","message":"Invalid request"})");
      else if (request.method == "OPTIONS") respond(client, 204, "No Content", "text/plain", "");
      else if (request.method == "GET" && (request.target == "/api/status" || request.target == "/health")) {
        const std::string body = std::string(R"({"type":"status","version":")") + ANKINK_VERSION +
          R"(","collectionOpen":)" + (collection_.is_open() ? "true" : "false") +
          R"(,"authenticated":)" + (collection_.is_authenticated() ? "true" : "false") +
          R"(,"collection":)" + json_string(options_.collection_path) +
          R"(,"error":)" + json_string(collection_error_) + "}";
        respond(client, 200, "OK", "application/json; charset=utf-8", body);
      } else if (request.method == "GET" && request.target == "/api/decks") {
        const std::string body = collection_.is_open() ? collection_.decks_json() :
          std::string(R"({"type":"decks","path":)") + json_string(options_.collection_path) +
          R"(,"decks":[],"error":)" + json_string(collection_error_) + "}";
        respond(client, collection_.is_open() ? 200 : 503,
                collection_.is_open() ? "OK" : "Service Unavailable",
                "application/json; charset=utf-8", body);
      } else if (request.method == "GET" && request.target.compare(0, 11, "/api/decks/") == 0 &&
                 request.target.size() > 16 && request.target.substr(request.target.size() - 5) == "/next") {
        const auto id = integer(request.target.substr(11, request.target.size() - 16), "deck id");
        respond(client, 200, "OK", "application/json; charset=utf-8", collection_.next_card_json(id));
      } else if (request.method == "POST" && request.target == "/api/answer") {
        const auto card = integer(form_value(request.body, "card"), "card");
        const auto rating = integer(form_value(request.body, "rating"), "rating");
        respond(client, 200, "OK", "application/json; charset=utf-8",
                collection_.answer_json(card, static_cast<int>(rating)));
      } else if (request.method == "POST" && request.target == "/api/auth/login") {
        const std::string username = form_value(request.body, "username");
        const std::string password = form_value(request.body, "password");
        if (username.empty() || password.empty())
          throw std::runtime_error("username and password are required");
        respond(client, 200, "OK", "application/json; charset=utf-8",
                collection_.login_json(username, password));
      } else if (request.method == "POST" && request.target == "/api/auth/logout") {
        respond(client, 200, "OK", "application/json; charset=utf-8",
                collection_.logout_json());
      } else if (request.method == "POST" && request.target == "/api/sync") {
        respond(client, 200, "OK", "application/json; charset=utf-8",
                collection_.sync_json());
      } else if (request.method == "POST" &&
                 request.target == "/api/sync/full-download") {
        respond(client, 200, "OK", "application/json; charset=utf-8",
                collection_.full_download_json());
      } else if (request.method == "GET" &&
                 request.target.compare(0, 11, "/api/media/") == 0) {
        const std::string name = url_decode(request.target.substr(11));
        if (name.empty() || name.find("..") != std::string::npos ||
            name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
          throw std::runtime_error("invalid media filename");
        std::string media_dir = options_.collection_path;
        const auto dot = media_dir.rfind('.');
        if (dot != std::string::npos) media_dir.resize(dot);
        media_dir += ".media/";
        const std::string body = read_file(media_dir + name);
        if (body.empty()) respond(client, 404, "Not Found", "text/plain", "Not found\n");
        else respond(client, 200, "OK", mime_type(name), body);
      } else if (request.method == "GET" && request.target.find("..") == std::string::npos) {
        const std::string relative = request.target == "/" ? "index.html" : request.target.substr(1);
        const std::string body = read_file(options_.asset_dir + "/" + relative);
        if (body.empty()) respond(client, 404, "Not Found", "text/plain", "Not found\n");
        else respond(client, 200, "OK", mime_type(relative), body);
      } else respond(client, 404, "Not Found", "application/json", R"({"type":"error","message":"Not found"})");
    } catch (const std::exception &exception) {
      respond(client, 400, "Bad Request", "application/json; charset=utf-8",
              std::string(R"({"type":"error","message":)") + json_string(exception.what()) + "}");
    }
    ::close(client);
  }
  ::close(listener); return 0;
}
} // namespace ankink
