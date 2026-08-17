#pragma once
#include "ankink/collection.hpp"
#include <cstdint>
#include <string>
namespace ankink {
struct ServerOptions {
  std::string collection_path{"/var/local/ankink/collection.anki2"};
  std::string asset_dir{"assets"};
  std::uint16_t port{8765};
};
class HttpServer {
public:
  explicit HttpServer(ServerOptions options);
  int run();
  static void request_stop() noexcept;
private:
  ServerOptions options_;
  Collection collection_;
  std::string collection_error_;
};
} // namespace ankink
