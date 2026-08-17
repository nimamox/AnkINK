#include <fbink.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void glyph(char character, uint8_t rows[7]) {
  static const uint8_t blank[7] = {0};
  const uint8_t *source = blank;
  static const uint8_t a[7] = {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11};
  static const uint8_t d[7] = {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e};
  static const uint8_t g[7] = {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0f};
  static const uint8_t i[7] = {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f};
  static const uint8_t k[7] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
  static const uint8_t l[7] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f};
  static const uint8_t n[7] = {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11};
  static const uint8_t o[7] = {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e};
  static const uint8_t dot[7] = {0, 0, 0, 0, 0, 0x0c, 0x0c};
  switch (character) {
  case 'A': source = a; break;
  case 'D': source = d; break;
  case 'G': source = g; break;
  case 'I': source = i; break;
  case 'K': source = k; break;
  case 'L': source = l; break;
  case 'N': source = n; break;
  case 'O': source = o; break;
  case '.': source = dot; break;
  }
  memcpy(rows, source, 7);
}

static void draw_text(uint8_t *pixels, uint32_t width, uint32_t height,
                      const char *text, uint32_t y, uint32_t scale) {
  const size_t length = strlen(text);
  const uint32_t cell = 6U * scale;
  const uint32_t text_width = length ? (uint32_t)length * cell - scale : 0;
  const uint32_t start_x = width > text_width ? (width - text_width) / 2U : 0;
  for (size_t index = 0; index < length; ++index) {
    uint8_t rows[7];
    glyph(text[index], rows);
    for (uint32_t row = 0; row < 7; ++row) {
      for (uint32_t column = 0; column < 5; ++column) {
        if (!(rows[row] & (1U << (4U - column))))
          continue;
        for (uint32_t dy = 0; dy < scale; ++dy) {
          for (uint32_t dx = 0; dx < scale; ++dx) {
            const uint32_t x = start_x + (uint32_t)index * cell +
                               column * scale + dx;
            const uint32_t py = y + row * scale + dy;
            if (x < width && py < height)
              pixels[(size_t)py * width + x] = 0;
          }
        }
      }
    }
  }
}

int main(void) {
  FBInkConfig config = {0};
  config.is_quiet = true;
  config.ignore_alpha = true;
  config.wfm_mode = WFM_GC16;
  config.is_flashing = true;

  int fd = fbink_open();
  if (fd < 0 || fbink_init(fd, &config) < 0)
    return EXIT_FAILURE;
  FBInkState state = {0};
  fbink_get_state(&config, &state);
  const size_t size = (size_t)state.view_width * state.view_height;
  uint8_t *pixels = malloc(size);
  if (!pixels) {
    fbink_close(fd);
    return EXIT_FAILURE;
  }
  memset(pixels, 255, size);
  const uint32_t center = state.view_height / 2U;
  draw_text(pixels, state.view_width, state.view_height, "ANKINK",
            center > 100U ? center - 100U : 0U, 12U);
  draw_text(pixels, state.view_width, state.view_height, "LOADING...",
            center + 35U, 6U);
  const int result = fbink_print_raw_data(
      fd, pixels, (int)state.view_width, (int)state.view_height, size, 0, 0,
      &config);
  free(pixels);
  fbink_close(fd);
  return result < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
