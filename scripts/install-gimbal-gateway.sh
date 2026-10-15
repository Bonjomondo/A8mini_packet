#!/usr/bin/env bash
# Run on the Orin as its normal login user. No sudo or system network changes.
set -euo pipefail
project_dir="$(cd "$(dirname "$0")/.." && pwd)"
control_dir="$HOME/.local/share/a8mini-control"
unit_dir="$HOME/.config/systemd/user"
mkdir -p "$control_dir" "$unit_dir"
install -m 0755 "$project_dir/scripts/gimbal-gateway.py" "$control_dir/gimbal-gateway.py"
python3 - "$control_dir" "$unit_dir" "$@" <<'PY'
import ipaddress
import pathlib
import sys
control_dir, unit_dir = map(pathlib.Path, sys.argv[1:3])
networks = [str(ipaddress.ip_network(value)) for value in sys.argv[3:]]
if not networks:
    raise SystemExit('Pass allowed controller IPs/CIDRs, e.g. 192.168.2.0/24')
allow = ' '.join('--allow ' + value for value in networks)
unit = f'''[Unit]
Description=A8 mini gimbal TCP/UDP control gateway
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/usr/bin/python3 "{control_dir}/gimbal-gateway.py" --bind 0.0.0.0 --port 37264 {allow}
Restart=on-failure
RestartSec=2
TimeoutStopSec=3
NoNewPrivileges=yes
PrivateTmp=yes
UMask=0077

[Install]
WantedBy=default.target
'''
(unit_dir / 'a8mini-control.service').write_text(unit)
PY
systemctl --user daemon-reload
systemctl --user enable --now a8mini-control.service
systemctl --user restart a8mini-control.service
systemctl --user --no-pager status a8mini-control.service
