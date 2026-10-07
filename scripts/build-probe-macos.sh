#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/probe-env.sh"
cd "$A8_PROJECT_DIR"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=x86_64 \
    -DCMAKE_PREFIX_PATH="$A8_QT_ROOT" \
    -DPKG_CONFIG_EXECUTABLE="$A8_GST_ROOT/bin/pkg-config" \
    -DA8_QMLGL_SOURCE_DIR="$A8_PROJECT_DIR/.deps/gst-plugins-good-1.22.12"
cmake --build build --parallel 6
