#include "ankink/input.hpp"

#include <wpe/wpe.h>

#include <glib-unix.h>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ankink {

namespace {

constexpr std::size_t bits_per_word = sizeof(unsigned long) * 8;

bool bit_set(const std::vector<unsigned long> &bits, unsigned int bit) {
  const auto word = bit / bits_per_word;
  return word < bits.size() && (bits[word] & (1UL << (bit % bits_per_word)));
}

std::vector<unsigned long> event_bits(int fd, int type, unsigned int maximum) {
  std::vector<unsigned long> result(maximum / bits_per_word + 1);
  if (ioctl(fd, EVIOCGBIT(type, result.size() * sizeof(unsigned long)),
            result.data()) < 0)
    result.clear();
  return result;
}

std::uint32_t event_time_ms(const input_event &event) {
  return static_cast<std::uint32_t>(event.time.tv_sec * 1000ULL +
                                    event.time.tv_usec / 1000ULL);
}

} // namespace

class InputManager::Impl {
public:
  struct Device {
    Impl *owner{};
    int fd{-1};
    guint source{};
    input_absinfo x_axis{};
    input_absinfo y_axis{};
    int raw_x{};
    int raw_y{};
    int tracking_id{-1};
    bool touch{};
    bool dirty{};
    bool down{};
    bool prior_down{};
  };

  Impl(wpe_view_backend *backend, DisplayInfo display)
      : backend_(backend), display_(display) {
    if (!backend_)
      return;
    std::error_code error;
    const std::filesystem::path directory{"/dev/input"};
    for (const auto &entry :
         std::filesystem::directory_iterator(directory, error)) {
      if (entry.path().filename().string().rfind("event", 0) != 0)
        continue;
      add_device(entry.path().string());
    }
    if (error)
      std::cerr << "AnkINK input: cannot scan /dev/input: " << error.message()
                << '\n';
  }

  ~Impl() {
    for (auto &device : devices_) {
      if (device->source)
        g_source_remove(device->source);
      if (device->fd >= 0)
        close(device->fd);
    }
  }

  void add_device(const std::string &path) {
    const int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0)
      return;
    const auto absolute = event_bits(fd, EV_ABS, ABS_MAX);
    const bool multitouch = bit_set(absolute, ABS_MT_POSITION_X) &&
                            bit_set(absolute, ABS_MT_POSITION_Y);
    const bool single_touch =
        bit_set(absolute, ABS_X) && bit_set(absolute, ABS_Y);
    const auto keys = event_bits(fd, EV_KEY, KEY_MAX);
    const bool page_keys = bit_set(keys, KEY_PAGEUP) ||
                           bit_set(keys, KEY_PAGEDOWN) ||
                           bit_set(keys, KEY_LEFT) || bit_set(keys, KEY_RIGHT);
    if (!multitouch && !single_touch && !page_keys) {
      close(fd);
      return;
    }

    auto device = std::make_unique<Device>();
    device->owner = this;
    device->fd = fd;
    device->touch = multitouch || single_touch;
    if (device->touch) {
      const int x_code = multitouch ? ABS_MT_POSITION_X : ABS_X;
      const int y_code = multitouch ? ABS_MT_POSITION_Y : ABS_Y;
      if (ioctl(fd, EVIOCGABS(x_code), &device->x_axis) < 0 ||
          ioctl(fd, EVIOCGABS(y_code), &device->y_axis) < 0) {
        close(fd);
        return;
      }
    }
    device->source = g_unix_fd_add(
        fd, static_cast<GIOCondition>(G_IO_IN | G_IO_HUP | G_IO_ERR),
        &Impl::on_fd, device.get());
    std::cerr << "AnkINK input: using " << path
              << (device->touch && page_keys ? " for touch and page keys\n"
                  : device->touch            ? " for touch\n"
                                             : " for page keys\n");
    devices_.push_back(std::move(device));
  }

  static gboolean on_fd(gint, GIOCondition condition, gpointer data) {
    auto &device = *static_cast<Device *>(data);
    if ((condition & (G_IO_HUP | G_IO_ERR)) != 0)
      return G_SOURCE_REMOVE;
    std::array<input_event, 32> events{};
    for (;;) {
      const ssize_t count = read(device.fd, events.data(), sizeof(events));
      if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        break;
      if (count <= 0)
        return G_SOURCE_REMOVE;
      const auto length = static_cast<std::size_t>(count) / sizeof(input_event);
      for (std::size_t index = 0; index < length; ++index)
        device.owner->handle(device, events[index]);
    }
    return G_SOURCE_CONTINUE;
  }

  void handle(Device &device, const input_event &event) {
    if (event.type == EV_ABS) {
      if (event.code == ABS_X || event.code == ABS_MT_POSITION_X) {
        device.raw_x = event.value;
        device.dirty = true;
      } else if (event.code == ABS_Y || event.code == ABS_MT_POSITION_Y) {
        device.raw_y = event.value;
        device.dirty = true;
      } else if (event.code == ABS_MT_TRACKING_ID) {
        device.tracking_id = event.value;
        device.down = event.value >= 0;
        device.dirty = true;
      }
    } else if (event.type == EV_KEY) {
      if (event.code == BTN_TOUCH) {
        device.down = event.value != 0;
        device.dirty = true;
      } else {
        dispatch_key(event);
      }
    } else if (event.type == EV_SYN && event.code == SYN_REPORT &&
               device.dirty) {
      dispatch(device, event_time_ms(event));
      device.dirty = false;
      device.prior_down = device.down;
    }
  }

  void dispatch_key(const input_event &event) {
    std::uint32_t key = 0;
    switch (event.code) {
    case KEY_PAGEUP:
    case KEY_UP:
    case KEY_LEFT:
      key = WPE_KEY_Page_Up;
      break;
    case KEY_PAGEDOWN:
    case KEY_DOWN:
    case KEY_RIGHT:
      key = WPE_KEY_Page_Down;
      break;
    default:
      return;
    }
    wpe_input_keyboard_event keyboard{event_time_ms(event), key, event.code,
                                      event.value != 0, 0};
    wpe_view_backend_dispatch_keyboard_event(backend_, &keyboard);
  }

  static int scale_axis(int value, const input_absinfo &axis,
                        std::uint32_t extent) {
    const int range = axis.maximum - axis.minimum;
    if (range <= 0 || extent == 0)
      return 0;
    const auto normalized = static_cast<std::int64_t>(
        std::clamp(value, axis.minimum, axis.maximum) - axis.minimum);
    return static_cast<int>(normalized * (extent - 1) / range);
  }

  void dispatch(Device &device, std::uint32_t time) {
    std::uint32_t logical_width = display_.width;
    std::uint32_t logical_height = display_.height;
    if (display_.touch_swap_axes)
      std::swap(logical_width, logical_height);
    int x = scale_axis(device.raw_x, device.x_axis, logical_width);
    int y = scale_axis(device.raw_y, device.y_axis, logical_height);
    if (display_.touch_swap_axes)
      std::swap(x, y);
    if (display_.touch_mirror_x)
      x = static_cast<int>(display_.width) - 1 - x;
    if (display_.touch_mirror_y)
      y = static_cast<int>(display_.height) - 1 - y;

    wpe_input_touch_event_type type = wpe_input_touch_event_type_motion;
    if (device.down && !device.prior_down)
      type = wpe_input_touch_event_type_down;
    else if (!device.down && device.prior_down)
      type = wpe_input_touch_event_type_up;
    else if (!device.down)
      return;

    const int id = std::max(0, device.tracking_id);
    const wpe_input_touch_event_raw point{type, time, id, x, y};
    wpe_input_touch_event touch{&point, 1, type, id, time, 0};
    wpe_view_backend_dispatch_touch_event(backend_, &touch);
  }

  wpe_view_backend *backend_{};
  DisplayInfo display_{};
  std::vector<std::unique_ptr<Device>> devices_;
};

InputManager::InputManager(wpe_view_backend *backend, DisplayInfo display)
    : impl_(std::make_unique<Impl>(backend, display)) {}
InputManager::~InputManager() = default;
bool InputManager::has_touchscreen() const noexcept {
  return std::any_of(impl_->devices_.begin(), impl_->devices_.end(),
                     [](const auto &device) { return device->touch; });
}

} // namespace ankink
