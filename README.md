# A8 mini Ubuntu / Jetson 视频代理

已部署 MediaMTX v1.21.1，提供 A8 mini 原始 H.265 视频的 RTSP/TCP 代理。当前阶段只做视频，控制网关未部署。

```text
A8 mini 192.168.144.25:8554/main.264（H.265 / 1080p25）
  → Jetson 相机网口 192.168.144.30
  → MediaMTX / RTSP TCP / 按需拉流
  → 地面端 GStreamer / Qt5 qmlglsink
```

## 连接地址和账号

| 连接网络 | 播放地址 | 地址配置 |
| --- | --- | --- |
| 地面端有线 192.168.1.x | `rtsp://192.168.1.5:8554/a8mini` | 本机已有静态 IP |
| 地面端 Wi-Fi 192.168.2.x | `rtsp://192.168.2.113:8554/a8mini` | 当前 DHCP 地址，后续可能变化 |
| Jetson 本机 | `rtsp://127.0.0.1:8554/a8mini` | 本机回环 |

播放用户名为 `a8viewer`。随机密码在本目录的 `artifacts/client-credentials.json`，权限为 0600。请将账号密码填入地面端播放器或 Probe 配置。凭据文件及 `artifacts/mediamtx.installed.yml` 含密码，不要随诊断日志提交到仓库。sudo 密码没有写入任何文件。

仅允许本机、192.168.1.0/24、192.168.2.0/24 读流，并要求正确凭据。没有发布权限。API 和 metrics 只监听本机回环。RTSP 是当前局域网方案使用的非 TLS TCP 连接。

现有有线静态 IP 保持原样；Wi-Fi 实测是 DHCP，本次没有修改 NetworkManager 配置。方案中 `192.168.0.108` 是示例，不能用于当前本机。地面端需要能够路由到上述地址；路由器的无线客户端隔离也可能影响外部播放。

## Mac / Windows 播放基线

安装好 GStreamer 后，在同一局域网运行以下命令，将密码占位文字替换成凭据文件中的密码。Wi-Fi 地址若变化，替换 `location`。

```bash
gst-launch-1.0 rtspsrc location=rtsp://192.168.2.113:8554/a8mini user-id=a8viewer user-pw="填入随机密码" protocols=tcp latency=300 ! rtph265depay ! h265parse ! decodebin ! videoconvert ! autovideosink
```

Windows PowerShell 可使用同一行命令。有线连接时使用 `rtsp://192.168.1.5:8554/a8mini`。Qt5 Probe 应把用户名/密码传给 `rtspsrc` 的 `user-id`、`user-pw` 属性。`qmlglsink` 的环境和 QML 渲染测试仍需在 Mac / Windows 各自验证。

## 日常管理

```bash
systemctl status mediamtx-a8mini --no-pager
systemctl is-enabled mediamtx-a8mini
journalctl -u mediamtx-a8mini -f
sudo systemctl restart mediamtx-a8mini
curl -fsS http://127.0.0.1:9997/v3/paths/list
curl -fsS http://127.0.0.1:9998/metrics
```

没有客户端时 `ready/available=false` 是按需拉流的正常状态。客户端连接后代理建立到相机的 TCP 流；最后一个客户端离开 10 秒后释放相机连接。首次打开最多等待 20 秒；相机离线时服务仍运行，但不能提供画面。

## Jetson 本机验证

以下只解码统计帧进度，不打开窗口、运行检测或发送云台命令。

```bash
cd /home/ly/Documents/A8mini_packet
/usr/bin/python3 scripts/probe-video.py --seconds 120
# 按需做更长的 Ubuntu 帧推进测试
/usr/bin/python3 scripts/probe-video.py --seconds 1800 --output artifacts/proxy-decode-30min.json
```

120 秒自动测试通过只能证明本机 TCP 代理和 NVIDIA 解码帧推进。它不证明 Mac/Windows 画面观感、QML 叠加、30/60 分钟稳定性或断 Wi-Fi 恢复已经验收。

## 部署和回退

本次安装已执行；安装脚本会拒绝覆盖已有部署。文件、版本和验证证据见 [CHANGELOG.md](CHANGELOG.md)。需要移除本次部署时执行：

```bash
cd /home/ly/Documents/A8mini_packet
sudo bash scripts/rollback-mediamtx.sh
```

回退脚本停止并取消自启，移除本次服务、程序、配置和专用系统账号，在 `artifacts/rollback-时间/` 保留配置副本。不会恢复或修改网络、检测程序及系统软件包，因为本次没有改这些内容。工作目录中的下载包、日志和凭据文件会保留。

## 本机相关文档

- 相机手册：`/home/ly/Documents/A8mini_Detction/A8 mini云台相机用户手册.pdf`
- 控制 SDK：`/home/ly/Documents/A8mini_Detction/SDK - V0.1.1.pdf`
- 原独立检测代码：`/home/ly/Documents/A8mini_Detction/`
- 当前 ROS 检测配置：`/home/ly/Documents/diff-planner-A8miniV2/src/user_command/multipoint/config/a8mini_detection.yaml`

MediaMTX 配置字段依据 [官方配置参考](https://mediamtx.org/docs/references/configuration-file)，独立 ARM64 发布包依据 [官方安装文档](https://mediamtx.org/docs/kickoff/install)。源流为 H.265 的判断来自本机实际 ffprobe 和解码结果。
