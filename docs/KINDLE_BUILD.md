# Building AnkINK for Kindle Oasis 8

The Kindle is not ABI-compatible with a generic Raspberry Pi or Debian ARMv7
system. Build the entire WPE stack against one sysroot captured from the target
firmware (or a demonstrably compatible toolchain). Before choosing hard-float,
verify the device rather than relying only on `uname -m`:

```sh
uname -a
readelf -A /bin/sh
readelf -l /bin/sh | grep interpreter
ldd --version
fbink -e
```

The supplied toolchain file assumes ARMv7-A, NEON, and hard-float. Change
`-mfloat-abi` and the compiler triplet if the device output says otherwise.

## 1. Build the runtime sysroot

Use a dedicated cross toolchain and build these for the same sysroot:

- libwpe 1.14.x
- WPEBackend-fdo 1.14.x
- a maintained WPE WebKit stable branch (minimum supported API: 2.38)
- Mesa with EGL, OpenGL ES 2, Wayland and surfaceless platforms, and Gallium
  `softpipe`
- GLib/GIO, Wayland, libdrm, SQLite, Fontconfig/FreeType/HarfBuzz, ICU, zlib,
  libjpeg/libpng/WebP, libxml2/libxslt, and the other dependencies selected by
  the WPE build
- FBInk built with `KINDLE=1 IMAGE=1` (a minimal build without `IMAGE` cannot
  accept AnkINK's raw Y8 frames)

For a 512 MiB class device, disable features the client does not use when your
WPE branch supports the corresponding switches: WebDriver, MiniBrowser,
developer tools, WebRTC, WebAudio, speech synthesis, gamepads, and the JIT. Keep
accelerated compositing enabled: WPEBackend-fdo still needs EGL even though Mesa
uses the CPU. A representative WebKit configuration starts with:

```sh
cmake -S WebKit -B webkit-build -GNinja \
  -DPORT=WPE \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DCMAKE_TOOLCHAIN_FILE=/absolute/path/to/cmake/kindle-armv7-toolchain.cmake \
  -DENABLE_MINIBROWSER=OFF \
  -DENABLE_WEBDRIVER=OFF \
  -DENABLE_WEB_RTC=OFF \
  -DENABLE_JIT=OFF
ninja -C webkit-build install
```

WPE options change between stable branches. Treat unknown-option warnings as a
configuration failure and inspect that checkout's `OptionsWPE.cmake`.

Cross-building WebKit is resource-intensive. Build on a Linux workstation; do
not attempt to compile it on the Kindle.

## 2. Build AnkINK

With the completed target sysroot:

```sh
export KINDLE_SYSROOT=/absolute/path/to/kindle-sysroot
cmake -S . -B build-kindle -GNinja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/kindle-armv7-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DANKINK_BUILD_TESTS=OFF
cmake --build build-kindle
```

Confirm the result before packaging:

```sh
file build-kindle/ankink
arm-linux-gnueabihf-readelf -A build-kindle/ankink
arm-linux-gnueabihf-readelf -d build-kindle/ankink
```

## 3. Create and deploy the bundle

The packaging script copies the executable, WPE subprocesses, backend, UI,
Mesa's software rasterizer, Fontconfig data, and recursively discovered
shared-library dependencies. It refuses to overwrite an existing output
directory.

```sh
scripts/package-kindle.sh "$KINDLE_SYSROOT" build-kindle AnkINK-kindle
cp /path/to/exported/collection.anki2 AnkINK-kindle/collection.anki2
```

Copy `AnkINK-kindle` to `/mnt/us/ankink` on the Kindle. Stop or hide the stock UI
using the launcher mechanism appropriate for the installed jailbreak package,
then run:

```sh
/mnt/us/ankink/ankink.sh
```

Do not replace system libraries or install the bundle into `/usr`. The launcher
uses an application-local library path so removal is recoverable.

## Runtime checks

If the screen stays unchanged, run from SSH and check stderr in this order:

1. FBInk recognizes the device and reports the expected viewport.
2. `libWPEBackend-fdo-1.0.so` loads from the bundle.
3. Mesa creates a software EGL context for WPEBackend-fdo's private Wayland
   display.
4. `WPEWebProcess` and `WPENetworkProcess` are found below `libexec`.
5. The backend reports SHM or a single-plane linear DMA-BUF. Tiled DMA-BUFs are
   rejected because safely reading them requires a device-specific GPU path.

If the process is killed without an error, inspect `dmesg` for the OOM killer.
The practical fixes are trimming WPE features, disabling JIT, using softpipe
instead of LLVMpipe, and reducing WebKit process count—not adding swap on Kindle
flash storage.

## E-ink policy

AnkINK sends only the bounding rectangle that differs from the previous frame.
Large changes use a full-quality GC16 update. After 20 partial updates it asks
FBInk for a flashing refresh to clear ghosting; adjust this with
`--full-refresh-every N`. The refresh button in the header forces the next frame
to flash.
