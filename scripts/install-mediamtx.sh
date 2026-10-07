#!/usr/bin/env bash
set -euo pipefail
task_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
[[ $EUID -eq 0 ]] || { echo 'Run with sudo.' >&2; exit 1; }
[[ $(uname -m) == aarch64 ]] || { echo 'ARM64 host required.' >&2; exit 1; }
# Refuse to overwrite an unrelated deployment.
for path in /usr/local/lib/mediamtx /usr/local/bin/mediamtx /etc/mediamtx /etc/systemd/system/mediamtx-a8mini.service; do
    [[ ! -e "$path" && ! -L "$path" ]] || { echo "Already exists: $path" >&2; exit 1; }
done
sha256sum --check --status <<< '6a3aa635fb60ea9b8d566ec306f0a42ff1b6b52a3942bc2baffbe55880d4c3dd  '"$task_dir/vendor/mediamtx_v1.21.1_linux_arm64.tar.gz"
[[ $("$task_dir/vendor/mediamtx" --version) == v1.21.1 ]]
if getent passwd mediamtx-a8mini || getent group mediamtx-a8mini; then
    echo 'Service account already exists; inspect before installation.' >&2
    exit 1
fi
python3 - "$task_dir" <<'PY'
import json, os, pathlib, secrets, sys
root = pathlib.Path(sys.argv[1])
password = secrets.token_urlsafe(24)
credentials = root / 'artifacts/client-credentials.json'
config = root / 'artifacts/mediamtx.installed.yml'
for path, text in (
    (credentials, json.dumps({'username': 'a8viewer', 'password': password}, indent=2) + '\n'),
    (config, (root / 'configs/mediamtx.yml').read_text().replace('__GENERATED_PASSWORD__', password)),
):
    fd = os.open(str(path), os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'w') as stream:
        stream.write(text)
    workspace_owner = root.stat()
    os.chown(str(path), workspace_owner.st_uid, workspace_owner.st_gid)
PY
useradd --system --user-group --no-create-home --home-dir /nonexistent --shell /usr/sbin/nologin mediamtx-a8mini
install -d -m 0755 /usr/local/lib/mediamtx/v1.21.1 /etc/mediamtx
install -m 0755 "$task_dir/vendor/mediamtx" /usr/local/lib/mediamtx/v1.21.1/mediamtx
install -m 0644 "$task_dir/vendor/LICENSE" /usr/local/lib/mediamtx/v1.21.1/LICENSE
install -m 0644 "$task_dir/vendor/mediamtx.yml" /usr/local/lib/mediamtx/v1.21.1/mediamtx.default.yml
install -m 0640 -o root -g mediamtx-a8mini "$task_dir/artifacts/mediamtx.installed.yml" /etc/mediamtx/mediamtx.yml
install -m 0644 "$task_dir/configs/mediamtx-a8mini.service" /etc/systemd/system/mediamtx-a8mini.service
ln -s /usr/local/lib/mediamtx/v1.21.1/mediamtx /usr/local/bin/mediamtx
systemd-analyze verify /etc/systemd/system/mediamtx-a8mini.service
systemctl daemon-reload
systemctl enable --now mediamtx-a8mini.service
systemctl --no-pager --full status mediamtx-a8mini.service
