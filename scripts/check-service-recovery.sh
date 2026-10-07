#!/usr/bin/env bash
set -euo pipefail
[[ $EUID -eq 0 ]] || exit 1
task_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
before_pid="$(systemctl show mediamtx-a8mini -p MainPID --value)"
systemctl restart mediamtx-a8mini
systemctl is-active --quiet mediamtx-a8mini
manual_pid="$(systemctl show mediamtx-a8mini -p MainPID --value)"
[[ "$before_pid" != "$manual_pid" ]]
before_restarts="$(systemctl show mediamtx-a8mini -p NRestarts --value)"
# Deliberate crash of only the newly installed proxy, with no active test readers.
systemctl kill --kill-who=main --signal=SIGKILL mediamtx-a8mini
sleep 5
systemctl is-active --quiet mediamtx-a8mini
after_pid="$(systemctl show mediamtx-a8mini -p MainPID --value)"
after_restarts="$(systemctl show mediamtx-a8mini -p NRestarts --value)"
[[ "$manual_pid" != "$after_pid" && "$after_restarts" -gt "$before_restarts" ]]
{
    date -Is
    echo "Manual restart: PID $before_pid -> $manual_pid"
    echo "Crash recovery: PID $manual_pid -> $after_pid"
    echo "Automatic restart counter: $before_restarts -> $after_restarts"
    systemctl is-active mediamtx-a8mini
    systemctl is-enabled mediamtx-a8mini
    stat -c '%a %U:%G %n' /etc/mediamtx/mediamtx.yml /etc/systemd/system/mediamtx-a8mini.service /usr/local/lib/mediamtx/v1.21.1/mediamtx
    getent passwd mediamtx-a8mini
    getent group mediamtx-a8mini
    passwd -S mediamtx-a8mini
} > "$task_dir/artifacts/service-recovery.txt"
cat "$task_dir/artifacts/service-recovery.txt"
