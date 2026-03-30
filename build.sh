#!/usr/bin/env bash

# Usage:
#   ./build.sh            (Release build)
#   ./build.sh debug      (Debug build with ASan/UBSan)
#   ./build.sh clean      (Remove build directory)

set -euo pipefail

BUILD_TYPE="${1:-release}"
BUILD_DIR="build"

case "${BUILD_TYPE,,}" in
  clean)
    echo "[orbit] Removing ${BUILD_DIR}/"
    rm -rf "${BUILD_DIR}"
    exit 0
    ;;
  debug)
    CMAKE_BUILD_TYPE="Debug"
    ;;
  release|*)
    CMAKE_BUILD_TYPE="Release"
    ;;
esac

echo "[orbit] Configuring (${CMAKE_BUILD_TYPE})…"
cmake -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      "$@"

echo "[orbit] Building…"
cmake --build "${BUILD_DIR}" --parallel "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo ""
echo "Build succeeded → ./${BUILD_DIR}/orbit"
echo ""
echo "Run: ./${BUILD_DIR}/orbit"
echo "     ORBIT_MIN_SPREAD_PCT=0.05 ./${BUILD_DIR}/orbit"
echo "     ORBIT_SYMBOLS=BTCUSDT,ETHUSDT ORBIT_LOG_LEVEL=DEBUG ./${BUILD_DIR}/orbit"