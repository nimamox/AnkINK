#pragma once

#include "ankink/frame.hpp"

#include <cstdint>
#include <memory>

namespace ankink {

struct DisplayInfo {
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint16_t dpi{};
  bool touch_swap_axes{};
  bool touch_mirror_x{};
  bool touch_mirror_y{};
};

class FBInkPresenter {
public:
  explicit FBInkPresenter(std::uint32_t full_refresh_every = 20);
  ~FBInkPresenter();

  FBInkPresenter(const FBInkPresenter &) = delete;
  FBInkPresenter &operator=(const FBInkPresenter &) = delete;

  [[nodiscard]] const DisplayInfo &display_info() const noexcept;
  void present(GrayFrame frame);
  void force_full_refresh();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace ankink
