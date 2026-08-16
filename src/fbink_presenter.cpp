#include "ankink/fbink_presenter.hpp"

#include <fbink.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace ankink {

namespace {

[[noreturn]] void fbink_error(const char *operation, int result) {
  const int code = result < 0 ? -result : result;
  throw std::runtime_error(std::string(operation) + ": " + std::strerror(code));
}

} // namespace

class FBInkPresenter::Impl {
public:
  explicit Impl(std::uint32_t full_refresh_every)
      : full_refresh_every_(std::max(1U, full_refresh_every)) {
    config_.is_quiet = true;
    config_.ignore_alpha = true;
    config_.wfm_mode = WFM_AUTO;
    config_.dithering_mode = HWD_PASSTHROUGH;

    fd_ = fbink_open();
    if (fd_ < 0)
      fbink_error("opening the framebuffer", fd_);
    const int initialized = fbink_init(fd_, &config_);
    if (initialized < 0) {
      fbink_close(fd_);
      fd_ = FBFD_AUTO;
      fbink_error("initializing FBInk", initialized);
    }

    FBInkState state{};
    fbink_get_state(&config_, &state);
    info_ = DisplayInfo{
        state.view_width,      state.view_height,    state.screen_dpi,
        state.touch_swap_axes, state.touch_mirror_x, state.touch_mirror_y,
    };
    if (info_.width == 0 || info_.height == 0)
      throw std::runtime_error("FBInk reported an empty viewport");
  }

  ~Impl() {
    if (fd_ != FBFD_AUTO)
      fbink_close(fd_);
  }

  void present(GrayFrame frame) {
    if (!frame.valid() || frame.width != info_.width ||
        frame.height != info_.height)
      throw std::invalid_argument(
          "WebKit frame size does not match the FBInk viewport");

    auto changed = changed_bounds(previous_, frame);
    if (!changed)
      return;

    ++partial_refreshes_;
    const auto changed_pixels =
        static_cast<std::uint64_t>(changed->width) * changed->height;
    const auto screen_pixels =
        static_cast<std::uint64_t>(info_.width) * info_.height;
    const bool large_change = changed_pixels * 100U >= screen_pixels * 60U;
    const bool maintenance_refresh = partial_refreshes_ >= full_refresh_every_;
    const bool full = previous_.pixels.empty() || large_change ||
                      maintenance_refresh || force_full_;
    const Rect area = full ? Rect{0, 0, info_.width, info_.height} : *changed;
    const auto pixels = full ? frame.pixels : crop(frame, area);

    FBInkConfig refresh = config_;
    refresh.is_flashing = full && (maintenance_refresh || force_full_);
    refresh.wfm_mode = full ? WFM_GC16 : WFM_AUTO;
    const int result = fbink_print_raw_data(
        fd_, pixels.data(), static_cast<int>(area.width),
        static_cast<int>(area.height), pixels.size(),
        static_cast<short>(area.x), static_cast<short>(area.y), &refresh);
    if (result < 0)
      fbink_error("presenting a WebKit frame through FBInk", result);

    if (full)
      partial_refreshes_ = 0;
    force_full_ = false;
    previous_ = std::move(frame);
  }

  int fd_{FBFD_AUTO};
  FBInkConfig config_{};
  DisplayInfo info_{};
  GrayFrame previous_{};
  std::uint32_t full_refresh_every_{};
  std::uint32_t partial_refreshes_{};
  bool force_full_{false};
};

FBInkPresenter::FBInkPresenter(std::uint32_t full_refresh_every)
    : impl_(std::make_unique<Impl>(full_refresh_every)) {}

FBInkPresenter::~FBInkPresenter() = default;

const DisplayInfo &FBInkPresenter::display_info() const noexcept {
  return impl_->info_;
}

void FBInkPresenter::present(GrayFrame frame) {
  impl_->present(std::move(frame));
}

void FBInkPresenter::force_full_refresh() { impl_->force_full_ = true; }

} // namespace ankink
