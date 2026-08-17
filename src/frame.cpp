#include "ankink/frame.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace ankink {

namespace {

bool pixel_count_fits(std::uint32_t width, std::uint32_t height) noexcept {
  return width != 0 && height != 0 &&
         static_cast<std::size_t>(width) <=
             std::numeric_limits<std::size_t>::max() / height;
}

} // namespace

bool GrayFrame::valid() const noexcept {
  return pixel_count_fits(width, height) &&
         pixels.size() == static_cast<std::size_t>(width) * height;
}

GrayFrame to_grayscale(const std::uint8_t *data, std::uint32_t width,
                       std::uint32_t height, std::size_t stride,
                       PixelFormat format) {
  if (!data || !pixel_count_fits(width, height) ||
      static_cast<std::size_t>(width) >
          std::numeric_limits<std::size_t>::max() / 4U)
    throw std::invalid_argument("invalid source frame");

  const auto row_bytes = static_cast<std::size_t>(width) * 4U;
  if (stride < row_bytes ||
      static_cast<std::size_t>(height - 1U) >
          (std::numeric_limits<std::size_t>::max() - row_bytes) / stride)
    throw std::overflow_error("frame is too large");

  const auto pixel_count = static_cast<std::size_t>(width) * height;
  GrayFrame result{width, height, std::vector<std::uint8_t>(pixel_count)};
  const bool blue_first =
      format == PixelFormat::bgra8888 || format == PixelFormat::bgrx8888;

  for (std::uint32_t y = 0; y < height; ++y) {
    const auto *source = data + static_cast<std::size_t>(y) * stride;
    auto *destination =
        result.pixels.data() + static_cast<std::size_t>(y) * width;
    for (std::uint32_t x = 0; x < width; ++x) {
      const auto *pixel = source + static_cast<std::size_t>(x) * 4;
      const std::uint8_t red = pixel[blue_first ? 2 : 0];
      const std::uint8_t green = pixel[1];
      const std::uint8_t blue = pixel[blue_first ? 0 : 2];
      // Integer Rec. 601 luma. The rounding term keeps white exactly white.
      destination[x] = static_cast<std::uint8_t>(
          (77U * red + 150U * green + 29U * blue + 128U) >> 8U);
    }
  }
  return result;
}

GrayFrame resize_nearest(const GrayFrame &frame, std::uint32_t width,
                         std::uint32_t height) {
  if (!frame.valid() || !pixel_count_fits(width, height))
    throw std::invalid_argument("invalid resize dimensions");
  if (frame.width == width && frame.height == height)
    return frame;

  GrayFrame result{width, height,
                   std::vector<std::uint8_t>(static_cast<std::size_t>(width) *
                                             height)};
  for (std::uint32_t y = 0; y < height; ++y) {
    const auto source_y = static_cast<std::uint64_t>(y) * frame.height / height;
    for (std::uint32_t x = 0; x < width; ++x) {
      const auto source_x =
          static_cast<std::uint64_t>(x) * frame.width / width;
      result.pixels[static_cast<std::size_t>(y) * width + x] =
          frame.pixels[static_cast<std::size_t>(source_y) * frame.width +
                       source_x];
    }
  }
  return result;
}

std::optional<Rect> changed_bounds(const GrayFrame &previous,
                                   const GrayFrame &current) {
  if (!current.valid())
    throw std::invalid_argument("invalid current frame");
  if (!previous.valid() || previous.width != current.width ||
      previous.height != current.height)
    return Rect{0, 0, current.width, current.height};

  std::uint32_t left = current.width;
  std::uint32_t top = current.height;
  std::uint32_t right = 0;
  std::uint32_t bottom = 0;
  bool changed = false;

  for (std::uint32_t y = 0; y < current.height; ++y) {
    const auto row = static_cast<std::size_t>(y) * current.width;
    for (std::uint32_t x = 0; x < current.width; ++x) {
      if (previous.pixels[row + x] == current.pixels[row + x])
        continue;
      changed = true;
      left = std::min(left, x);
      top = std::min(top, y);
      right = std::max(right, x);
      bottom = std::max(bottom, y);
    }
  }
  if (!changed)
    return std::nullopt;
  return Rect{left, top, right - left + 1, bottom - top + 1};
}

std::vector<std::uint8_t> crop(const GrayFrame &frame, Rect area) {
  if (!frame.valid() || area.empty() || area.x >= frame.width ||
      area.y >= frame.height || area.width > frame.width - area.x ||
      area.height > frame.height - area.y)
    throw std::invalid_argument("crop lies outside frame");

  std::vector<std::uint8_t> result(static_cast<std::size_t>(area.width) *
                                   area.height);
  for (std::uint32_t y = 0; y < area.height; ++y) {
    const auto source =
        frame.pixels.begin() +
        static_cast<std::ptrdiff_t>(static_cast<std::size_t>(area.y + y) *
                                    frame.width + area.x);
    auto destination =
        result.begin() + static_cast<std::ptrdiff_t>(
                             static_cast<std::size_t>(y) * area.width);
    std::copy_n(source, area.width, destination);
  }
  return result;
}

} // namespace ankink
