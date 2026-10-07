# Ubuntu / Jetson A8 mini 视频代理配置变更记录

日期：2026-10-07，Asia/Shanghai（UTC+08:00）。
工作目录：`/home/ly/Documents/A8mini_packet`。
本次范围：按用户确认，只部署视频代理；不部署 TCP 9001 / UDP 9002 控制网关。

## 1. 配置前状态

| 项目 | 实测结果 |
| --- | --- |
| 系统 | Ubuntu 20.04.6 LTS / aarch64 |
| 硬件 | NVIDIA Orin NX Developer Kit |
| 内核 | 5.10.216-tegra |
| NVIDIA L4T | 35.6.5-20260701022834 |
| GStreamer | 1.16.3；已有 `nvv4l2decoder` |
| 本机 Qt 开发包 | 5.12.8，不是附件地面端要求的 5.15 |
| 本机 `qmlglsink` | 未安装；此次 Ubuntu 作为代理端不需要它 |
| 相机 RTSP | `rtsp://192.168.144.25:8554/main.264` |
| 相机实际编码 | HEVC / H.265，1920×1080，25 FPS |
| 原 MediaMTX 服务 | 没有发现；8554 / 9997 / 9998 未被占用 |

已有网络连接：

| 网卡 | NetworkManager 配置 | IPv4 | 方法 |
| --- | --- | --- | --- |
| rtl8168 | a8mini-camera | 192.168.144.30/24 | manual，静态，相机网段 |
| rtl8125 | lidar-nic | 192.168.1.5/24 | manual，静态 |
| wlan0 | TP-LINK_5G_C5A4 | 192.168.2.113/24 | auto，DHCP |

默认路由为 `192.168.2.1` 经 wlan0。已有有线 IP 确实是静态，但 Wi-Fi 配置实测为 DHCP。本次没有修改它；因此 Wi-Fi 播放地址可能在租约或网络变化时改变。

依据：`artifacts/system-before.json`、`package-baseline.txt`、`camera-source-probe.json`。

## 2. 新增程序及版本固定

从 [MediaMTX 官方 v1.21.1 发布页](https://github.com/bluenviron/mediamtx/releases/tag/v1.21.1) 下载 ARM64 独立程序。没有通过 apt/pip 安装、升级或替换系统依赖，没有更换 NVIDIA 插件或 GStreamer。

- 发布版本：v1.21.1，发布元数据时间为 2026-09-20T16:07:47Z。
- 下载文件：`mediamtx_v1.21.1_linux_arm64.tar.gz`。
- 下载地址：`https://github.com/bluenviron/mediamtx/releases/download/v1.21.1/mediamtx_v1.21.1_linux_arm64.tar.gz`。
- 压缩包 SHA256：`6a3aa635fb60ea9b8d566ec306f0a42ff1b6b52a3942bc2baffbe55880d4c3dd`。
- 同时与 GitHub release asset 的 digest 及官方 `checksums.sha256` 核对，均一致。
- 系统安装路径：`/usr/local/lib/mediamtx/v1.21.1/mediamtx`。
- 命令入口：`/usr/local/bin/mediamtx`，符号链接到上述固定版本。
- 保留官方 LICENSE 和默认配置，运行时使用自定义 `/etc/mediamtx/mediamtx.yml`。

依据：`artifacts/mediamtx-release.json`、`download-verification.txt`、`binary-sha256.txt`、`vendor/checksums.sha256`。

## 3. 系统文件新增清单

以下文件/目录在部署前不存在，故没有覆盖旧服务或旧配置。

| 系统路径 | 内容/作用 | 权限与属主 |
| --- | --- | --- |
| `/usr/local/lib/mediamtx/` | 新建版本目录 | 0755 root:root |
| `/usr/local/lib/mediamtx/v1.21.1/` | 固定版本目录 | 0755 root:root |
| `/usr/local/lib/mediamtx/v1.21.1/mediamtx` | ARM64 程序 | 0755 root:root |
| `/usr/local/lib/mediamtx/v1.21.1/LICENSE` | 官方许可证 | 0644 root:root |
| `/usr/local/lib/mediamtx/v1.21.1/mediamtx.default.yml` | 官方默认配置副本，未用于启动 | 0644 root:root |
| `/usr/local/bin/mediamtx` | 程序入口符号链接 | root:root |
| `/etc/mediamtx/` | 新建配置目录 | 0755 root:root |
| `/etc/mediamtx/mediamtx.yml` | 实际运行配置，含播放密码 | 0640 root:mediamtx-a8mini |
| `/etc/systemd/system/mediamtx-a8mini.service` | 新建服务单元 | 0644 root:root |
| `/etc/systemd/system/multi-user.target.wants/mediamtx-a8mini.service` | enable 操作新增的自启符号链接 | root:root |

`systemctl daemon-reload` 重新加载了服务定义；`systemctl enable --now mediamtx-a8mini.service` 启动并启用了自启。整个 Ubuntu 没有重启。

安装时 `systemd-analyze verify` 还输出了已有 `/lib/systemd/system/snapd.service` 的 `RestartMode` 字段兼容提示。这不是本次新增服务的字段，未修改 snapd 文件。

## 4. 系统账号数据库变更

新建专用系统用户/同名组 `mediamtx-a8mini`：

- UID：999；GID：993。
- home 字段：`/nonexistent`，没有创建家目录。
- shell：`/usr/sbin/nologin`。
- 密码状态：锁定（`passwd -S` 输出 `L`），不能用该账号登录。
- 仅供 MediaMTX 运行并读取 root:mediamtx-a8mini 的配置文件；不以 root 运行视频服务。

`useradd --system --user-group ...` 更新了 `/etc/passwd`、`/etc/shadow`、`/etc/group`、`/etc/gshadow`，系统账号工具也可能维护这些文件的常规备份。没有修改 ly 的密码、sudo 权限、sudoers 或任何既有用户组成员关系。

依据：`artifacts/service-recovery.txt`。用户提供的 sudo 密码仅用于 sudo 交互输入，没有写入脚本、配置或 changelog。

## 5. 实际视频代理配置

```text
相机 192.168.144.25:8554/main.264
  → RTSP/TCP 拉流
  → MediaMTX 路径 a8mini
  → RTSP/TCP 客户端
```

| 参数 | 实际值/含义 |
| --- | --- |
| RTSP 监听 | `0.0.0.0:8554`，IPv4 TCP；没有 IPv6 RTSP 监听 |
| 客户端传输 | `rtspTransports: [tcp]` |
| 相机上游传输 | `rtspTransport: tcp` |
| 上游地址 | `rtsp://192.168.144.25:8554/main.264` |
| 按需拉流 | `sourceOnDemand: true` |
| 首次等待上限 | 20 秒 |
| 最后一个客户端离开后的释放时间 | 10 秒 |
| 读/写超时 | 10 秒 / 10 秒 |
| 写队列 | 512 |
| 录像 | 关闭 |
| 转码/缩放/相机编码修改 | 均未设置或执行 |
| RTMP/HLS/WebRTC/SRT/MOQ/playback/pprof | 关闭 |
| API | `127.0.0.1:9997`，仅本机 |
| metrics | `127.0.0.1:9998`，仅本机 |
| 日志 | stdout → systemd journal，没有新增专用日志文件或 logrotate 配置 |

MediaMTX 日志观察到相机 RTP 包较大，代理把包拆分为较小 RTP 包：`8180 > 1440`。这是 RTP 重新封包，不是 H.265 解码或重新编码，视频分辨率和帧率不因此改变。

客户端测试 `latency=300` 是 GStreamer 接收端参数，没有修改原检测程序的 200ms 参数。只有验证用 Probe 运行了 `nvv4l2decoder`；常驻 MediaMTX 本身不运行 GPU 解码。

参数依据：[MediaMTX 官方配置参考](https://mediamtx.org/docs/references/configuration-file)。

## 6. 访问控制和审批调整

最初准备的匿名全接口 RTSP 配置被自动审批拒绝。拒绝理由是用户授权了视频代理部署和自启，但没有授权全接口无密码暴露相机视频。该次安装没有执行，没有留下匿名服务。

随后修改为带随机密码的配置，通过审批后才执行安装：

- 播放用户名 `a8viewer`；使用 `secrets.token_urlsafe(24)` 生成独立随机密码。
- 只授予 `read` 权限，且仅路径 `a8mini`；不授予发布、录像回放或管理权限。
- 客户端来源仅允许 `127.0.0.1`、`192.168.1.0/24`、`192.168.2.0/24`。
- 相机网段 `192.168.144.0/24` 无读流权限，正确密码也会被拒绝。
- 本机 API/metrics 管理权限仅为回环地址 `127.0.0.1` / `::1` 配置，监听地址实际是 IPv4 回环。
- 实际系统配置权限为 0640，非服务组的普通账号不能直接读取。
- 工作目录凭据文件 `artifacts/client-credentials.json` 和实际配置副本 `artifacts/mediamtx.installed.yml` 为 0600，归 ly 所有，并已写入 `.gitignore`。
- RTSP 使用附件局域网方案的普通 TCP，没有启用 TLS；连接密码只应供受信任局域网使用。

服务限制包括 `NoNewPrivileges`、只读系统/家目录保护、私有临时目录/设备、空 capability 集合及受限网络地址族。没有改系统防火墙、端口转发、NetworkManager、DNS、路由或 sysctl。

## 7. 验证结果和失败记录

### 基础功能与权限

| 验证 | 实测结果 |
| --- | --- |
| 官方包 SHA256 | 通过，两份官方依据一致 |
| 相机 ffprobe | H.265，1920×1080，25/1 FPS |
| service 状态 | active / running |
| 开机自启设置 | enabled；未通过整机重启实测 |
| 匿名 DESCRIBE | 401 Unauthorized |
| 错误密码 DESCRIBE | 401 Unauthorized |
| 正确凭据 DESCRIBE | 200 OK |
| 正确凭据但源地址为相机网口 | 401 Unauthorized |
| API/metrics | 本机回环 HTTP 可访问 |
| 原检测配置/采集器 SHA256 | 与配置前一致 |
| 三份 NetworkManager IPv4 profile | 与配置前一致 |
| GStreamer/L4T/Qt 软件包版本 | 与配置前一致 |

依据：`artifacts/auth-checks.json`、`system-after.json`、`package-after.txt`、`metrics-after.txt`。

### 120 秒真实视频测试

使用 `rtspsrc protocols=tcp latency=300 → rtph265depay → h265parse → nvv4l2decoder → fakesink`，带播放账号密码。本次不打开画面窗口，依据解码帧推进判断。

- 测试时段起点：2026-10-07 16:25:25 +08:00。
- 实际运行：120.019 秒。
- 解码帧数：2973。
- 取得首帧：1.247 秒。
- 首帧后的平均帧率：25.031 FPS。
- 最大相邻帧间隔：约 0.070 秒。
- caps：NVMM / NV12 / 1920×1080 / 25 FPS。
- GStreamer error / warning：均为空。

依据：`artifacts/proxy-decode.json`、`proxy-decode.log`。

### 服务重启和异常退出恢复

16:30:55 +08:00 完成验证：

- 手动 `systemctl restart`：主进程 PID 13098 → 13709。
- 向本次新服务主进程发送 SIGKILL，模拟异常退出；没有终止旧检测或飞行进程。
- 5 秒检查窗口内自动恢复：PID 13709 → 13744。
- `NRestarts`：0 → 1；恢复后仍为 active / enabled。
- 服务配置 `Restart=on-failure`、`RestartSec=3`，启动限流为 60 秒最多 10 次。

依据：`artifacts/service-recovery.txt`、`service-after-recovery.log`。

### 并发测试

初次在当前工具执行上下文同时启动相机直连、有线代理、Wi-Fi 代理三路读流：Wi-Fi 代理完成 60 秒，另外两路在约 10 秒时发生 RTSP EOF，未取得帧。这轮整体判定失败，原始结果保留，没有删除或覆盖。

依据：`artifacts/concurrent-tests.json`、`direct-concurrent.json`、`wired-proxy-concurrent.json`、`wifi-proxy-concurrent.json`。有线地址的单独初次重试也保存在 `wired-proxy-retry.json`。

之后执行本机系统权限上下文并发复测，在已验证重启恢复的服务上运行，同样同时读取原相机和有线/Wi-Fi 两个代理地址，没有改网络或相机参数。完整结果见下方自动汇总。

首次握手失败的具体根因未定位。服务日志在成功复测时可用于核对实际客户端来源；不将工具上下文的差异当作已查明的网络根因。

## 8. 本工作目录新增内容

| 路径 | 内容 |
| --- | --- |
| `README.md` | 地址、账号、地面端命令、维护和回退说明 |
| `CHANGELOG.md` | 本变更记录 |
| `configs/mediamtx.yml` | 不含真实密码的配置模板 |
| `configs/mediamtx-a8mini.service` | 服务定义副本 |
| `scripts/install-mediamtx.sh` | 固定版本安装、生成凭据、创建账号、启用服务；拒绝覆盖已有部署 |
| `scripts/rollback-mediamtx.sh` | 停止并移除本次部署，先保留配置副本 |
| `scripts/probe-video.py` | 真实视频解码和帧推进统计 |
| `scripts/check-service-recovery.sh` | 手动重启与异常退出恢复检查，需 sudo |
| `scripts/check-concurrent.py` | 三路并发真实视频测试 |
| `vendor/` | 官方压缩包、校验文件、解压出的程序、默认配置和许可证 |
| `artifacts/` | 部署前后快照、下载证据、凭据、测试结果、服务日志 |
| `.gitignore` | 排除真实密码、回退备份和 Python 缓存 |

运行语法检查产生了 `scripts/__pycache__/`。测试结果由不同权限上下文写入，部分日志归 root 所有且对普通用户可读；含密码的两份本地文件始终保持 0600。

## 9. 保持原样及待验收范围

- 没有更改 `/home/ly/Documents/A8mini_Detction/` 的检测程序、模型或文档。
- 没有更改 `diff-planner-A8miniV2` 的 GStreamer/NVDEC/TensorRT 检测链路或飞行栈。
- 没有修改相机固件、编码设置、云台姿态；没有发送拍照、录像或控制命令。
- 没有升级 Ubuntu、Qt、GStreamer、CUDA、TensorRT、NVIDIA 驱动或内核。
- 没有安装 libVLC，也没有在 Jetson 上构建地面端 Qt5 qmlglsink。
- TCP 9001 / UDP 9002 未监听、控制网关未部署，符合用户本轮选择。
- 没有整机重启、断 Wi-Fi、重启相机或更改功耗模式。

已完成的是 Ubuntu 视频代理部署和短时帧推进验证。Mac/Windows 的 qmlglsink、QML 叠加、主观花屏观察、30/60 分钟实机播放、断 Wi-Fi 恢复、相机重启恢复及全飞行栈资源负载仍需现场验收。三路解码测试没有运行 TensorRT 推理，因此不能当作全检测负载验证。

用户提供的参考文档位置：

- `/home/ly/Documents/A8mini_Detction/A8 mini云台相机用户手册.pdf`
- `/home/ly/Documents/A8mini_Detction/SDK - V0.1.1.pdf`

本轮确认了文件存在；视频源参数以实际探测为准，没有执行 SDK 控制开发。

## 10. 回退步骤

```bash
cd /home/ly/Documents/A8mini_packet
sudo bash scripts/rollback-mediamtx.sh
```

该脚本：先复制系统实际配置和 service 到 `artifacts/rollback-时间/`（目录 0700），再取消自启并停止服务，删除本次安装的入口、程序、配置、service 和账号/组，执行 daemon-reload。不会递归删除工作目录，不会修改原检测项目、网络连接或软件包。密码副本随回退备份保留在受限目录。

回退脚本仅做了 shell 语法检查，没有实际运行，因为当前目标是保留已工作的代理服务。Ubuntu systemd journal 中的历史验证日志及账号工具常规备份不会被回退脚本清除。


## 11. 系统权限上下文并发复测最终汇总

三路近乎同时开始；原相机直连的记录起点为 `2026-10-07T16:31:48.743871+08:00`，均使用 300ms RTSP/TCP 接收缓冲和 NVIDIA H.265 硬解；三个消费者全部完成 60 秒测试。此时原相机额外直连一路，MediaMTX 只向相机拉取一路，并把该代理流分发给两个客户端。

| 路径 | 时长 s | 帧数 | 首帧后平均 FPS | 首帧 s | 最大相邻帧间隔 s | error / warning |
| --- | --- | --- | --- | --- | --- | --- |
| direct-host | 60.075 | 1499 | 25.067 | 0.276 | 0.079 | 0 / 0 |
| wired-proxy-host | 60.067 | 1474 | 25.054 | 1.234 | 0.066 | 0 / 0 |
| wifi-proxy-host | 60.075 | 1474 | 25.053 | 1.240 | 0.073 | 0 / 0 |

依据：`artifacts/concurrent-host-tests.json`、`direct-host.json`、`wired-proxy-host.json`、`wifi-proxy-host.json` 及各自 `.log`。该测试同时验证了服务重启后的实际读流恢复。两个代理地址的系统权限测试从本机发起；不替代 Mac/Windows 外部客户端验收。


最终常驻服务快照：`MainPID=13744`、`NRestarts=1`（来自本次主动异常恢复测试）、`ActiveState=active`、`SubState=running`、`UnitFileState=enabled`；`MemoryCurrent=28684288` 字节（约 27.36 MiB）。该内存值是单次 systemd 快照，不是长期峰值。`CPUUsageNSec=[not set]`，本轮没有取得可信的常驻 CPU 占用统计，也没有执行长期资源压力测试。依据：`artifacts/service-final-status.txt`、`service-final-resources.txt`、`service-final.log`、`paths-final.json`。
