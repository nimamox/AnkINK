#pragma once
#include "ankink/collection.hpp"
#include <cstdint>
#include <string>
namespace ankink {
struct ServerOptions {
  std::string collection_path{"/var/local/ankink/collection.anki2"};
  std::string asset_dir{"assets"};
  std::string simulator_asset_dir{"simulator"};
  std::uint16_t port{8765};
  bool simulator{};
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
