#include "ankink/frame.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char *message) {
  if (!condition)
    throw std::runtime_error(message);
}

void conversion_honors_stride_and_channel_order() {
  const std::vector<std::uint8_t> bgra{
      0,   0, 255, 255, 0,   255, 0,   255, 99, 99, 99, 99,
      255, 0, 0,   255, 255, 255, 255, 255, 99, 99, 99, 99,
  };
  const auto frame = ankink::to_grayscale(bgra.data(), 2, 2, 12,
                                          ankink::PixelFormat::bgra8888);
  require(frame.pixels.size() == 4, "wrong grayscale buffer size");
  require(frame.pixels[0] == 77, "red luma is wrong");
  require(frame.pixels[1] == 149, "green luma is wrong");
  require(frame.pixels[2] == 29, "blue luma is wrong");
  require(frame.pixels[3] == 255, "white luma is wrong");
}

void changes_are_merged_and_cropped() {
  ankink::GrayFrame before{4, 3, std::vector<std::uint8_t>(12, 255)};
  auto after = before;
  after.pixels[1] = 0;
  after.pixels[10] = 64;
  const auto bounds = ankink::changed_bounds(before, after);
  require(bounds == ankink::Rect{1, 0, 2, 3}, "changed bounds are wrong");
  const auto pixels = ankink::crop(after, *bounds);
  require(pixels == std::vector<std::uint8_t>({0, 255, 255, 255, 255, 64}),
          "cropped pixels are wrong");
  require(!ankink::changed_bounds(after, after),
          "identical frames should not refresh");
}

} // namespace

int main() {
  try {
    conversion_honors_stride_and_channel_order();
    changes_are_merged_and_cropped();
    std::cout << "AnkINK core tests passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception &error) {
    std::cerr << "Test failure: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
