#pragma once

#include "ankink/fbink_presenter.hpp"

#include <memory>

struct wpe_view_backend;

namespace ankink {

class InputManager {
public:
  InputManager(wpe_view_backend *backend, DisplayInfo display);
  ~InputManager();

  InputManager(const InputManager &) = delete;
  InputManager &operator=(const InputManager &) = delete;

  [[nodiscard]] bool has_touchscreen() const noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ankink
