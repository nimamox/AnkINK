#!/bin/sh

# Shared Kindle userspace ABI mapping for the Docker, Rust, packaging, and
# validation scripts. Keep compiler/sysroot implementation details here so a
# future ARMHF SDK can replace Debian's cross toolchain without changing the
# package layout or runtime selection.
kindle_abi_configure() {
  KINDLE_ABI=$1
  case "$KINDLE_ABI" in
    armel)
      KINDLE_GNU_TRIPLET=arm-linux-gnueabi
      KINDLE_RUST_TARGET=armv7-unknown-linux-gnueabi
      KINDLE_FLOAT_ABI=softfp
      KINDLE_LOADER=ld-linux.so.3
      ;;
    armhf)
      KINDLE_GNU_TRIPLET=arm-linux-gnueabihf
      KINDLE_RUST_TARGET=armv7-unknown-linux-gnueabihf
      KINDLE_FLOAT_ABI=hard
      KINDLE_LOADER=ld-linux-armhf.so.3
      ;;
    *)
      echo "Unsupported Kindle ABI: $KINDLE_ABI (expected armel or armhf)" >&2
      return 2
      ;;
  esac
  KINDLE_FLOAT_FLAGS="-mfpu=neon -mfloat-abi=$KINDLE_FLOAT_ABI"
  KINDLE_ARCH_FLAGS="-march=armv7-a -mtune=generic-armv7-a $KINDLE_FLOAT_FLAGS"
  export KINDLE_ABI KINDLE_GNU_TRIPLET KINDLE_RUST_TARGET
  export KINDLE_FLOAT_ABI KINDLE_FLOAT_FLAGS KINDLE_ARCH_FLAGS KINDLE_LOADER
}
