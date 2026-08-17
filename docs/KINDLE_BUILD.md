# Building AnkINK for PW2-compatible ARMEL Kindles

The Kindle package contains a small C++ daemon and Mesquite web assets. It does
not contain a browser engine or graphics stack.

On the Linux build host:

```sh
export KINDLE_SDK_ROOT=/home/nima/ankink-sdk
export KINDLE_ABI=armel
cmake -S . -B cmake-build-kindle-armel -GNinja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/kindle-debian-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=MinSizeRel \
  -DANKINK_BUILD_APP=ON -DANKINK_BUILD_TESTS=OFF
cmake --build cmake-build-kindle-armel --parallel
```

Validate that the result is 32-bit ARM EABI5 using `/lib/ld-linux.so.3`, then
package its minimal shared-library closure:

```sh
export CROSS_COMPILE=arm-linux-gnueabi-
scripts/package-kindle.sh \
  "$KINDLE_SDK_ROOT/armel" \
  cmake-build-kindle-armel \
  AnkINK-kindle-armel
```

Put `collection.anki2` in the bundle root. Copy the bundle to
`/mnt/us/ankink` and copy `kual-extension/AnkINK` to
`/mnt/us/extensions/AnkINK`. Launch it from KUAL.

For CLion, use the remote Linux toolchain and set the two environment variables
above in the Kindle ARMEL CMake profile. The executable is
`cmake-build-kindle-armel/ankinkd`; it is not directly runnable on the host.
