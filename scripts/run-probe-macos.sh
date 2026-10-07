#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/probe-env.sh"
cd "$A8_PROJECT_DIR"
exec "$A8_PROJECT_DIR/build/tools/a8mini_probe/a8mini_probe.app/Contents/MacOS/a8mini_probe" "$@"
