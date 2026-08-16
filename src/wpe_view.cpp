#include "ankink/wpe_view.hpp"

#include "ankink/frame.hpp"

#include <wpe/fdo.h>
#include <wpe/wpe.h>

#include <drm_fourcc.h>
#include <wayland-server-protocol.h>
#include <wayland-server.h>

#include <sys/mman.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace ankink {

namespace {

PixelFormat wayland_format(std::uint32_t format) {
  switch (format) {
  case WL_SHM_FORMAT_ARGB8888:
    return PixelFormat::bgra8888;
  case WL_SHM_FORMAT_XRGB8888:
    return PixelFormat::bgrx8888;
  case WL_SHM_FORMAT_ABGR8888:
    return PixelFormat::rgba8888;
  case WL_SHM_FORMAT_XBGR8888:
    return PixelFormat::rgbx8888;
  default:
    throw std::runtime_error("unsupported WPE shared-memory pixel format " +
                             std::to_string(format));
  }
}

PixelFormat drm_format(std::uint32_t format) {
  switch (format) {
  case DRM_FORMAT_ARGB8888:
    return PixelFormat::bgra8888;
  case DRM_FORMAT_XRGB8888:
    return PixelFormat::bgrx8888;
  case DRM_FORMAT_ABGR8888:
    return PixelFormat::rgba8888;
  case DRM_FORMAT_XBGR8888:
    return PixelFormat::rgbx8888;
  default:
    throw std::runtime_error("unsupported WPE DMA-BUF pixel format " +
                             std::to_string(format));
  }
}

} // namespace

class WPEView::Impl {
public:
  Impl(FBInkPresenter &presenter, std::uint32_t width, std::uint32_t height)
      : presenter_(presenter) {
    static const wpe_view_backend_exportable_fdo_client client{
        &Impl::export_buffer_resource,
        &Impl::export_dmabuf_resource,
        &Impl::export_shm_buffer,
        nullptr,
        nullptr,
    };
    exportable_ =
        wpe_view_backend_exportable_fdo_create(&client, this, width, height);
    if (!exportable_)
      throw std::runtime_error(
          "WPEBackend-fdo could not create an exportable view backend");
    backend_ = wpe_view_backend_exportable_fdo_get_view_backend(exportable_);
    if (!backend_) {
      wpe_view_backend_exportable_fdo_destroy(exportable_);
      exportable_ = nullptr;
      throw std::runtime_error("WPEBackend-fdo returned an empty view backend");
    }
  }

  ~Impl() {
    if (exportable_)
      wpe_view_backend_exportable_fdo_destroy(exportable_);
  }

  void present_shm(wl_shm_buffer *buffer) {
    if (!buffer)
      throw std::runtime_error("WPE exported an invalid shared-memory buffer");
    wl_shm_buffer_begin_access(buffer);
    try {
      const auto width =
          static_cast<std::uint32_t>(wl_shm_buffer_get_width(buffer));
      const auto height =
          static_cast<std::uint32_t>(wl_shm_buffer_get_height(buffer));
      const auto stride =
          static_cast<std::size_t>(wl_shm_buffer_get_stride(buffer));
      const auto format = wayland_format(wl_shm_buffer_get_format(buffer));
      const auto *pixels =
          static_cast<const std::uint8_t *>(wl_shm_buffer_get_data(buffer));
      presenter_.present(to_grayscale(pixels, width, height, stride, format));
      wl_shm_buffer_end_access(buffer);
    } catch (...) {
      wl_shm_buffer_end_access(buffer);
      throw;
    }
  }

  void present_dmabuf(
      const wpe_view_backend_exportable_fdo_dmabuf_resource &buffer) {
    if (buffer.n_planes != 1 || buffer.fds[0] < 0)
      throw std::runtime_error(
          "only single-plane WPE DMA-BUF frames are supported");
    if (buffer.width == 0 || buffer.height == 0 ||
        buffer.strides[0] < static_cast<std::uint64_t>(buffer.width) * 4U)
      throw std::runtime_error("WPE exported an invalid DMA-BUF layout");
    if (buffer.modifiers[0] != DRM_FORMAT_MOD_LINEAR &&
        buffer.modifiers[0] != DRM_FORMAT_MOD_INVALID)
      throw std::runtime_error(
          "WPE exported a tiled DMA-BUF that cannot be read by the CPU");
    if (buffer.height >
        (std::numeric_limits<std::size_t>::max() - buffer.offsets[0]) /
            buffer.strides[0])
      throw std::overflow_error("WPE DMA-BUF size overflow");
    const std::size_t size =
        buffer.offsets[0] +
        static_cast<std::size_t>(buffer.strides[0]) * buffer.height;
    void *mapping =
        mmap(nullptr, size, PROT_READ, MAP_SHARED, buffer.fds[0], 0);
    if (mapping == MAP_FAILED)
      throw std::runtime_error(std::string("mapping WPE DMA-BUF: ") +
                               std::strerror(errno));
    try {
      const auto *pixels =
          static_cast<const std::uint8_t *>(mapping) + buffer.offsets[0];
      presenter_.present(to_grayscale(pixels, buffer.width, buffer.height,
                                      buffer.strides[0],
                                      drm_format(buffer.format)));
      munmap(mapping, size);
    } catch (...) {
      munmap(mapping, size);
      throw;
    }
  }

  void complete() {
    wpe_view_backend_exportable_fdo_dispatch_frame_complete(exportable_);
  }

  static void export_buffer_resource(void *data, wl_resource *resource) {
    auto &self = *static_cast<Impl *>(data);
    try {
      self.present_shm(wl_shm_buffer_get(resource));
    } catch (const std::exception &error) {
      std::cerr << "AnkINK frame error: " << error.what() << '\n';
    }
    wpe_view_backend_exportable_fdo_dispatch_release_buffer(self.exportable_,
                                                            resource);
    self.complete();
  }

  static void export_dmabuf_resource(
      void *data, wpe_view_backend_exportable_fdo_dmabuf_resource *resource) {
    auto &self = *static_cast<Impl *>(data);
    try {
      if (!resource)
        throw std::runtime_error("WPE exported an empty DMA-BUF resource");
      self.present_dmabuf(*resource);
    } catch (const std::exception &error) {
      std::cerr << "AnkINK frame error: " << error.what() << '\n';
    }
    if (resource)
      wpe_view_backend_exportable_fdo_dispatch_release_buffer(
          self.exportable_, resource->buffer_resource);
    self.complete();
  }

  static void export_shm_buffer(void *data,
                                wpe_fdo_shm_exported_buffer *exported) {
    auto &self = *static_cast<Impl *>(data);
    try {
      if (!exported)
        throw std::runtime_error(
            "WPE exported an empty shared-memory resource");
      self.present_shm(wpe_fdo_shm_exported_buffer_get_shm_buffer(exported));
    } catch (const std::exception &error) {
      std::cerr << "AnkINK frame error: " << error.what() << '\n';
    }
    if (exported)
      wpe_view_backend_exportable_fdo_dispatch_release_shm_exported_buffer(
          self.exportable_, exported);
    self.complete();
  }

  FBInkPresenter &presenter_;
  wpe_view_backend_exportable_fdo *exportable_{};
  wpe_view_backend *backend_{};
};

WPEView::WPEView(FBInkPresenter &presenter, std::uint32_t width,
                 std::uint32_t height)
    : impl_(std::make_unique<Impl>(presenter, width, height)) {}
WPEView::~WPEView() = default;
wpe_view_backend *WPEView::backend() const noexcept { return impl_->backend_; }

void WPEView::activate(float device_scale_factor) {
  wpe_view_backend_dispatch_set_device_scale_factor(impl_->backend_,
                                                    device_scale_factor);
  wpe_view_backend_add_activity_state(impl_->backend_,
                                      wpe_view_activity_state_visible |
                                          wpe_view_activity_state_focused |
                                          wpe_view_activity_state_in_window);
}

} // namespace ankink
