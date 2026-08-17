#include "ankink/http_server.hpp"
#include <csignal>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
void stop_server(int) { ankink::HttpServer::request_stop(); }
void usage(const char *program) {
  std::cout << "Usage: " << program
            << " [--collection FILE] [--assets DIRECTORY] [--port PORT]\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    ankink::ServerOptions options;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      if (argument == "--help" || argument == "-h") { usage(argv[0]); return 0; }
      if (i + 1 >= argc) throw std::runtime_error("missing value for " + argument);
      const std::string value = argv[++i];
      if (argument == "--collection") options.collection_path = value;
      else if (argument == "--assets") options.asset_dir = value;
      else if (argument == "--port") {
        const long port = std::stol(value);
        if (port < 1 || port > 65535) throw std::runtime_error("invalid port");
        options.port = static_cast<std::uint16_t>(port);
      } else throw std::runtime_error("unknown option: " + argument);
    }
    std::signal(SIGINT, stop_server);
    std::signal(SIGTERM, stop_server);
#ifdef SIGPIPE
    std::signal(SIGPIPE, SIG_IGN);
#endif
    return ankink::HttpServer(std::move(options)).run();
  } catch (const std::exception &exception) {
    std::cerr << "ankinkd: " << exception.what() << '\n';
    return 1;
  }
}
