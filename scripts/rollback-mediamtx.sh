#!/usr/bin/env bash
set -euo pipefail
[[ $EUID -eq 0 ]] || { echo 'Run with sudo.' >&2; exit 1; }
task_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
# Stop and remove only this deployment; preserve copies for later inspection.
backup_dir="$task_dir/artifacts/rollback-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$backup_dir"
chmod 0700 "$backup_dir"
cp -a /etc/mediamtx/mediamtx.yml /etc/systemd/system/mediamtx-a8mini.service "$backup_dir/"
systemctl disable --now mediamtx-a8mini.service
[[ $(readlink /usr/local/bin/mediamtx) != /usr/local/lib/mediamtx/v1.21.1/mediamtx ]] || rm /usr/local/bin/mediamtx
rm /etc/systemd/system/mediamtx-a8mini.service /etc/mediamtx/mediamtx.yml
rmdir /etc/mediamtx
rm /usr/local/lib/mediamtx/v1.21.1/mediamtx /usr/local/lib/mediamtx/v1.21.1/LICENSE /usr/local/lib/mediamtx/v1.21.1/mediamtx.default.yml
rmdir /usr/local/lib/mediamtx/v1.21.1
rmdir /usr/local/lib/mediamtx
systemctl daemon-reload
userdel mediamtx-a8mini
if getent group mediamtx-a8mini >/dev/null; then groupdel mediamtx-a8mini; fi
systemctl reset-failed mediamtx-a8mini.service 2>/dev/null || true
echo "Removed this MediaMTX deployment; configuration copies: $backup_dir"
