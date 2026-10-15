# A8 mini 云台与相机控制

双击 `Start_Video_Probe.command`，选择右侧“云台控制”，保留默认地址 `192.168.2.112:37264`，点击“连接控制”。视频和控制分别连接；启动程序不会自动转动、变焦、拍照或录像。

按住方向键转动，松手停止；速度默认 20，可调 5～100。支持回中、朝下、指定偏航/俯仰、按住变焦、指定变焦倍数、锁定/跟随/FPV、拍照和录像。窗口失焦、切换页签、断开和退出会发送停止。

## 通信分工

```text
Mac / Windows Qt 控制端
  → 192.168.2.112:37264 TCP / UDP
  → Orin a8mini-control 转发服务
  → 192.168.144.25:37260 TCP / UDP
  → A8 mini
```

| 操作 | SDK 命令 | 传输 |
| --- | --- | --- |
| 按住转动 | `0x07`，有符号偏航/俯仰速度 -100～100 | UDP，20 Hz |
| 按住变焦 | `0x05`，-1 / 1 | UDP，20 Hz |
| 松手或主动停止 | `0x07` 双零、`0x05` 零 | TCP；同时发送 UDP 零并重复 3 次 |
| 回中 | `0x08 01` | TCP |
| 朝下 / 指定角度 | `0x0E`，朝下为 yaw=0、pitch=-90° | TCP |
| 指定变焦倍数 | `0x0F`，整数和十分位 | TCP |
| 锁定 / 跟随 / FPV | `0x0C 03/04/05` | TCP |
| 拍照 / 录像切换 | `0x0C 00/02` | TCP |
| 姿态读取 | `0x0D` | UDP，5 Hz |
| 相机状态 / TCP 心跳 | `0x0A` / `0x00` | TCP，1 Hz |

TCP 命令由网关原样通过 TCP 送到相机，UDP 命令也保持 UDP。无 TCP→UDP 降级。网关拒绝通过 UDP 发送离散操作，也拒绝通过 TCP 发送非零连续速度。

协议依据本仓库 `docs/SDK - V0.1.1.pdf`：PDF 第 3 页为帧格式，第 6～13 页为基本控制，第 49 页为接口，第 50～51 页为样例，第 55～57 页为 CRC，第 85～92 页为 TCP 支持列表。`docs/云台.pdf` 实际是 Caddx Avatar GM 系列快速指南，与当前 SIYI A8 mini 不同，因此这里使用 SIYI SDK。Orin 上原有程序占用了 `37260`，新网关监听 `37264`，相机端口仍为 `37260`。

SDK 的 TCP 支持列表中 A8 mini 多处未标记；2026-10-15 在当前相机上实测 TCP `0x0A`、TCP/UDP `0x0D` 能返回带有效 CRC 的相机数据。TCP `0x18` 当前变焦查询未返回数据，因此不依赖它；变焦倍数从 `0x05` 回包读取。具体操作仍以当前固件的 ACK/反馈为准。

## 确认与停止

帧采用 `55 66`、小端长度/序号、CRC16-0x1021（初值 0，小端存储）。解析处理 TCP 拆包/粘包，拒绝坏 CRC 和超长数据。实测相机回包采用自身递增序号，而非请求序号；控制端同一时间只处理一个离散操作，按命令和数据格式匹配回包。

回中/绝对变焦检查状态 ACK，角度回包表示命令已接收，实际姿态单独显示。模式和录像通过 `0x0A` 相机状态确认。录像按钮只在最新状态为“未录像/录像中”时可用，TF 卡异常时禁用。拍照等待 `0x0B` 成功/失败反馈。超时显示“结果未知”并断开控制，避免迟到 ACK 确认后续操作；不会自动重发拍照或录像切换。

控制端每 50 ms 发送当前速度，松手清零并通过 TCP 发送停止。网关独立监视每个连续命令：超过 350 ms 未收到新样本时通过 TCP 和 UDP 发零；TCP 客户端退出或断联也停止。网关校验连续命令序号，处理 16 位回绕，丢弃重复和乱序包，包括落在 TCP 停止之前的旧 UDP 包。TCP 3.5 秒无有效客户端数据时释放控制权；客户端 3.5 秒无有效 TCP 回包时断开。

同一时刻只允许一个 TCP 控制端，UDP 需匹配它的 IP 和首次 UDP 源端口。网关默认仅允许回环；部署时显式指定允许的地面端 IP/CIDR。它使用局域网明文 SDK，无账号认证，应只绑定可信网络并按需要缩小允许的地址范围。相机网线断开、Orin 断电或进程被强制终止时，发出的停止包无法保证到达；这里的看门狗不等于相机硬件失联保护。

## Orin 安装

将 `scripts/gimbal-gateway.py` 和 `scripts/install-gimbal-gateway.sh` 放在 Orin 同一项目的 `scripts/` 下，以普通登录用户执行：

```bash
bash scripts/install-gimbal-gateway.sh 192.168.2.0/24 192.168.1.0/24
systemctl --user status a8mini-control --no-pager
journalctl --user -u a8mini-control -f
```

可以只传地面端单个 IP，例如 `192.168.2.108/32`。安装位置为 `~/.local/share/a8mini-control/`，服务为 `~/.config/systemd/user/a8mini-control.service`，仅依赖 Python 3.8+。不会修改路由、NetworkManager、MediaMTX 或检测程序。

用户服务在用户管理器运行时自启；若要求注销后及开机未登录时仍运行，需要在 Orin 执行一次 `sudo loginctl enable-linger "$USER"`。

2026-10-15 已在当前 Orin 用户 `ly` 下安装并启用服务，允许 `192.168.2.0/24` 和 `192.168.1.0/24`，`Linger=yes`，端口为 `37264`。Qt 实机只读验证已通过：TCP 读取模式/录像状态、UDP 读取姿态；没有自动执行非零转动、变焦、拍照或录像。

前台运行：

```bash
python3 scripts/gimbal-gateway.py --bind 0.0.0.0 --allow 192.168.2.0/24
```

客户端也可直接访问有路由的相机，或指定网关：

```bash
bash scripts/run-probe-macos.sh --controls --control-host 192.168.2.112 --control-port 37264
```

直连相机时指定 `--control-host 192.168.144.25 --control-port 37260`。直连时绕过 Orin 的看门狗和乱序保护，应优先使用网关。Windows 使用现有启动脚本传入相同的 `--controls --control-host ...` 参数；本次未做 Windows 真机验证。

卸载本次用户服务：

```bash
systemctl --user disable --now a8mini-control.service
rm ~/.config/systemd/user/a8mini-control.service
systemctl --user daemon-reload
rm -r ~/.local/share/a8mini-control
```

## 验证

```bash
bash scripts/build-probe-macos.sh
bash -c 'source scripts/probe-env.sh; cmake -S . -B build -DA8_BUILD_CONTROL_TESTS=ON; cmake --build build --parallel 6; ctest --test-dir build --output-on-failure'
python3 tests/a8mini/test-gateway.py -v
# 实机只查询状态/姿态；退出时仅发送停止，不自动转动或拍照
A8_LIVE_CONTROL_HOST=192.168.2.112 build/a8gimbal_tests liveStatusReadOnly
```

自动测试使用本地假相机验证手册字节样例、CRC、拆包、角度边界、UDP 连续控制/TCP 停止、ACK/状态确认、超时不重发、网关 UDP 看门狗、独占控制、坏包拒绝、序号回绕与旧 UDP 丢弃。真实运动、实际变焦范围、TF 卡拍照/录像以及不同固件的操作支持需要在设备现场验收。
