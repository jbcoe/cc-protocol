#!/bin/bash

set -eu -o pipefail

MANUAL_VTABLE=OFF
PRESET="Release"
BUILD_DIR=""
CLEAN=OFF

while [[ $# -gt 0 ]]; do
  case $1 in
    --manual-vtable)
      MANUAL_VTABLE=ON
      shift
      ;;
    -B|--build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --clean)
      CLEAN=ON
      shift
      ;;
    *)
      PRESET="$1"
      shift
      ;;
  esac
done

# Generate build system specified in build directory with cmake
CONFIGURE_ARGS=(--preset "$PRESET" -DXYZ_PROTOCOL_GENERATE_MANUAL_VTABLE="$MANUAL_VTABLE")
if [[ -n "$BUILD_DIR" ]]; then
  CONFIGURE_ARGS+=(-B "$BUILD_DIR")
fi
if [[ "$CLEAN" == "ON" ]]; then
  CONFIGURE_ARGS+=(--fresh)
fi
cmake "${CONFIGURE_ARGS[@]}"

# Build the underlying build system via CMake
BUILD_ARGS=(--build)
if [[ -n "$BUILD_DIR" ]]; then
  BUILD_ARGS+=("$BUILD_DIR" --config "$PRESET")
else
  BUILD_ARGS+=(--preset "$PRESET")
fi
if [[ "$CLEAN" == "ON" ]]; then
  BUILD_ARGS+=(--clean-first)
fi
cmake "${BUILD_ARGS[@]}"

# Run the tests
TEST_ARGS=()
if [[ -n "$BUILD_DIR" ]]; then
  TEST_ARGS+=(--test-dir "$BUILD_DIR" -C "$PRESET")
else
  TEST_ARGS+=(--preset "$PRESET")
fi
ctest "${TEST_ARGS[@]}"
