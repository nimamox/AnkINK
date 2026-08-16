#pragma once

#include "ankink/fbink_presenter.hpp"

#include <cstdint>
#include <memory>

struct wpe_view_backend;

namespace ankink {

class WPEView {
public:
  WPEView(FBInkPresenter &presenter, std::uint32_t width, std::uint32_t height);
  ~WPEView();

  WPEView(const WPEView &) = delete;
  WPEView &operator=(const WPEView &) = delete;

  [[nodiscard]] wpe_view_backend *backend() const noexcept;
  void activate(float device_scale_factor);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ankink
