#!/usr/bin/env bash
# Source this file to use SDKs installed inside this checkout.
A8_PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
A8_QT_ROOT="${A8_QT_ROOT:-$A8_PROJECT_DIR/.deps/qt/5.15.2/clang_64}"
A8_GST_ROOT="${A8_GST_ROOT:-$A8_PROJECT_DIR/.deps/GStreamer.framework/Versions/1.0}"
if [[ ! -d "$A8_GST_ROOT" ]]; then
    A8_GST_ROOT=/Library/Frameworks/GStreamer.framework/Versions/1.0
fi
export PATH="$A8_PROJECT_DIR/.tools/bin:$A8_QT_ROOT/bin:$A8_GST_ROOT/bin:$PATH"
export PKG_CONFIG_PATH="$A8_GST_ROOT/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export GST_PLUGIN_PATH_1_0="$A8_PROJECT_DIR/build/gst-plugins${GST_PLUGIN_PATH_1_0:+:$GST_PLUGIN_PATH_1_0}"
export GST_PLUGIN_SYSTEM_PATH_1_0="$A8_GST_ROOT/lib/gstreamer-1.0"
export GST_PLUGIN_SCANNER_1_0="$A8_GST_ROOT/libexec/gstreamer-1.0/gst-plugin-scanner"
export GST_REGISTRY="$A8_PROJECT_DIR/build/gst-registry-x86_64.bin"
export QML2_IMPORT_PATH="$A8_QT_ROOT/qml${QML2_IMPORT_PATH:+:$QML2_IMPORT_PATH}"
export QT_PLUGIN_PATH="$A8_QT_ROOT/plugins"
