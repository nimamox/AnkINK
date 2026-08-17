#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace ankink {

struct Options {
  std::string collection_path{"/mnt/us/ankink/collection.anki2"};
  std::string assets_path;
  std::uint32_t full_refresh_every{20};
  std::uint32_t render_scale{1};
  bool input_enabled{true};
};

Options parse_options(int argc, char **argv);
std::string usage(const char *executable);

class Application {
public:
  explicit Application(Options options);
  ~Application();

  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;

  int run();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ankink
