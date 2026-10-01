#!/bin/sh
# Benchmark controls change compilation, never the runtime package layout.
kindle_optimization_configure() {
  KINDLE_CPP_OPT_LEVEL=${KINDLE_CPP_OPT_LEVEL:-2}
  KINDLE_RUST_OPT_LEVEL=${KINDLE_RUST_OPT_LEVEL:-2}
  KINDLE_RUST_NEON=${KINDLE_RUST_NEON:-0}
  KINDLE_IPO=${KINDLE_IPO:-ON}
  KINDLE_VECTOR_REPORT=${KINDLE_VECTOR_REPORT:-OFF}
  case "$KINDLE_CPP_OPT_LEVEL:$KINDLE_RUST_OPT_LEVEL" in 2:2|2:3|3:2|3:3);;*)echo 'Optimization levels must be 2 or 3' >&2;return 2;;esac
  case "$KINDLE_RUST_NEON" in 0|1);;*)echo "KINDLE_RUST_NEON must be 0 or 1" >&2;return 2;;esac
  case "$KINDLE_IPO:$KINDLE_VECTOR_REPORT" in ON:ON|ON:OFF|OFF:ON|OFF:OFF);;*)echo "IPO and vector report must be ON or OFF" >&2;return 2;;esac
  KINDLE_OPT_PROFILE="generic-v1-cpp$KINDLE_CPP_OPT_LEVEL-rust$KINDLE_RUST_OPT_LEVEL-neon$KINDLE_RUST_NEON-ipo$KINDLE_IPO"
  export KINDLE_CPP_OPT_LEVEL KINDLE_RUST_OPT_LEVEL KINDLE_RUST_NEON KINDLE_IPO KINDLE_VECTOR_REPORT KINDLE_OPT_PROFILE
  export CARGO_PROFILE_RELEASE_OPT_LEVEL="$KINDLE_RUST_OPT_LEVEL"
  export CARGO_PROFILE_RELEASE_LTO=thin CARGO_PROFILE_RELEASE_CODEGEN_UNITS=1
}
kindle_rust_flags_configure() {
  # Global Cargo flags take precedence over target flags; keep this profile authoritative.
  unset RUSTFLAGS CARGO_ENCODED_RUSTFLAGS
  KINDLE_RUST_FLAGS=
  if [ "$KINDLE_RUST_NEON" = 1 ];then
    # The target's llvm_floatabi stays Soft. Disabling soft instruction
    # selection permits hardware FP/SIMD with the ARMEL calling convention,
    # exactly the softfp analogue documented by Rust's ABI feature checks.
    case "$KINDLE_ABI" in
      armel) KINDLE_RUST_FLAGS='-C target-feature=-soft-float,+vfp3,+neon';;
      armhf) KINDLE_RUST_FLAGS='-C target-feature=+neon';;
    esac
  fi
  RUST_TARGET_ENV=$(printf '%s' "$KINDLE_RUST_TARGET" | tr '[:lower:]-' '[:upper:]_')
  export "CARGO_TARGET_${RUST_TARGET_ENV}_RUSTFLAGS=$KINDLE_RUST_FLAGS"
}
