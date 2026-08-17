#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ankink {

enum class PixelFormat {
  bgra8888,
  bgrx8888,
  rgba8888,
  rgbx8888,
};

struct Rect {
  std::uint32_t x{};
  std::uint32_t y{};
  std::uint32_t width{};
  std::uint32_t height{};

  [[nodiscard]] bool empty() const noexcept {
    return width == 0 || height == 0;
  }
  friend bool operator==(const Rect &left, const Rect &right) noexcept {
    return left.x == right.x && left.y == right.y &&
           left.width == right.width && left.height == right.height;
  }
};

struct GrayFrame {
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<std::uint8_t> pixels;

  [[nodiscard]] bool valid() const noexcept;
};

// Converts a strided four-byte WebKit frame to the Y8 layout preferred by FBInk
// on Kindle.
GrayFrame to_grayscale(const std::uint8_t *data, std::uint32_t width,
                       std::uint32_t height, std::size_t stride,
                       PixelFormat format);

// Scales a grayscale frame without introducing intermediate gray levels. This
// is intentionally nearest-neighbour: it is cheap on the Kindle and keeps text
// edges crisp for the e-ink waveform.
GrayFrame resize_nearest(const GrayFrame &frame, std::uint32_t width,
                         std::uint32_t height);

// Finds one conservative rectangle containing all changed pixels. E-ink
// benefits more from one merged update than from a large number of tiny refresh
// ioctls.
std::optional<Rect> changed_bounds(const GrayFrame &previous,
                                   const GrayFrame &current);

std::vector<std::uint8_t> crop(const GrayFrame &frame, Rect area);

} // namespace ankink
