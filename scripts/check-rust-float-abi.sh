#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
: "${KINDLE_ABI:?}" "${KINDLE_SDK_ROOT:?}"
. "$ROOT/scripts/kindle-abi.sh"
. "$ROOT/scripts/kindle-optimization.sh"
kindle_optimization_configure
kindle_abi_configure "$KINDLE_ABI"
kindle_rust_flags_configure
OUTPUT=$(mktemp -d)
trap 'rm -rf "$OUTPUT"' EXIT HUP INT TERM
# Word splitting is intentional for our validated, fixed-value compiler flags.
rustc +1.92.0 --edition=2024 --crate-type staticlib -C opt-level="$KINDLE_RUST_OPT_LEVEL" \
 --target "$KINDLE_RUST_TARGET" $KINDLE_RUST_FLAGS "$ROOT/tests/rust_float_abi.rs" -o "$OUTPUT/probe.a"
"$KINDLE_SDK_ROOT/bin/$KINDLE_GNU_TRIPLET-g++" -O"$KINDLE_CPP_OPT_LEVEL" \
 --sysroot="$KINDLE_SDK_ROOT/$KINDLE_ABI" $KINDLE_ARCH_FLAGS \
 "$ROOT/tests/rust_float_abi.cpp" "$OUTPUT/probe.a" -ldl -lpthread -lm -o "$OUTPUT/probe"
ATTRIBUTES=$("$KINDLE_SDK_ROOT/bin/$KINDLE_GNU_TRIPLET-readelf" -A "$OUTPUT/probe")
printf '%s\n' "$ATTRIBUTES"
case "$KINDLE_ABI" in
 armhf) printf '%s\n' "$ATTRIBUTES" | grep -q 'Tag_ABI_VFP_args: VFP registers';;
 armel) if printf '%s\n' "$ATTRIBUTES" | grep -q 'Tag_ABI_VFP_args: VFP registers';then exit 1;fi;;
esac
for cpu in cortex-a8 cortex-a9;do
 qemu-arm -r 3.0.35 -cpu "$cpu" -L "$KINDLE_SDK_ROOT/$KINDLE_ABI" "$OUTPUT/probe"
done
