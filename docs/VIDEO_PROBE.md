# A8 mini 视频 Probe

本阶段交付独立视频窗口和可复用 `A8VideoPipeline`。默认连接 `rtsp://192.168.2.113:8554/a8mini`，读取 `artifacts/client-credentials.json` 中的 `username` / `password`，并自动播放。凭据文件已被 Git 忽略，密码不会出现在报告中。

## 当前 Mac 直接运行

双击项目根目录的 **`启动视频Probe.command`**，或在终端执行：

```bash
bash scripts/run-probe-macos.sh
```

右侧可修改地址、账号密码和缓冲，点击「连接」生效。「断开」停止自动重连；「彩条」切换到内置测试源。红色准星和左上状态标记都是覆盖视频的 QML 元素。

当前工作区的 `.deps/` 已放入 Qt 5.15.2、GStreamer 1.22.12 Runtime/Development 和匹配的 gst-plugins-good 源码；`.tools/` 包含构建工具。这些 SDK 不提交到 Git。Qt5 官方 Mac SDK 为 x86_64，本次在 Apple Silicon 上通过现有 Rosetta 运行。

重新编译：

```bash
bash scripts/build-probe-macos.sh
```

不要直接双击 `build/` 下的 `.app`：当前是开发版，由启动脚本设置 Qt 和 GStreamer 插件路径。移动工作区或更换电脑后需要重新安装 SDK、重新编译。

其他运行方式：

```bash
bash scripts/run-probe-macos.sh --test
bash scripts/run-probe-macos.sh --file /absolute/path/video.mp4
bash scripts/run-probe-macos.sh --url rtsp://192.168.1.5:8554/a8mini
bash scripts/run-probe-macos.sh --software
```

`--software` 只加载界面，显示「软件渲染模式不支持实时 GL 视频」，不会创建 qmlglsink。

## 视频底层

`src/a8mini/A8VideoPipeline.{h,cpp}` 不依赖 Probe 的 UI。它负责：

- RTSP/TCP、H.265 解码、300 ms 默认缓冲。
- `videoconvert → glupload → glcolorconvert → qmlglsink`。
- 错误、EOS 和 5 秒无帧推进后的重建；重试间隔 1 / 2 / 5 秒。按需拉流的首帧超时为 30 秒。
- 状态、错误、渲染/丢帧总数、帧率、分辨率、实际解码器和重连次数。
- 在 Qt 的渲染线程上先让 sink 进入 READY，再启动其余管线，保持 GL display 一致。

帧率按实际渲染帧数的时间差计算。GstBaseSink 的 `average-rate` 是处理时序比值，不直接作为 FPS 显示。

`qmlglsink` 必须在 QML 加载前创建以注册 `GstGLVideoItem`。初始化顺序和渲染线程启动参考 [GStreamer 官方示例](https://github.com/GStreamer/gstreamer/blob/main/subprojects/gst-plugins-good/tests/examples/qt/qmlsink/main.cpp) 和 [qmlglsink 文档](https://gstreamer.freedesktop.org/documentation/qmlgl/qmlglsink.html)。

后续接地面站只需链接 `a8video` 库，将对象提供给 QML，在加载 QML 后调用 `attach(videoItem)`，再调用 `play(url, username, password, 300)`。软件模式使用 `A8VideoPipeline(false)`，通过 Loader 避免加载 GL 视频 QML。机载检测程序不需要变更。

## 其他电脑的构建

需要 CMake ≥ 3.16、C++17、Qt 5.15.x（Core / Gui / Qml / Quick / QuickControls2）以及 GStreamer ≥ 1.18 的 Runtime 和 Development。Qt、GStreamer 和程序的架构必须一致。

macOS 官方 Framework 的 `bin/pkg-config` 可用于 CMake。`scripts/probe-env.sh` 默认优先使用项目内 SDK，可通过 `A8_QT_ROOT` 和 `A8_GST_ROOT` 指定其他安装。当前 Mac 构建脚本固定使用 x86_64。

Windows 使用 **Qt MSVC x64** 和 **GStreamer MSVC x86_64 Runtime + Development**，安装 Visual Studio C++ 工具和 CMake。在 PowerShell 中：

```powershell
.\scripts\run-probe-windows.ps1 -QtRoot C:\Qt\5.15.2\msvc2019_64 -GStreamerRoot C:\gstreamer\1.0\msvc_x86_64 -Build
```

如安装包缺少 `qmlglsink`，下载与安装的 GStreamer 版本一致的 gst-plugins-good 发布源码，再加 `-QmlglSourceDir C:\deps\gst-plugins-good-版本`。CMake 会编译上游 Qt5 插件，启动脚本会加载它。无需自行编写视频渲染器。Windows 脚本已提供，本次没有 Windows 真机，尚未运行验证。

## 本次必要验证

只做构建、短时实际渲染和一次客户端断线恢复；未做 30/60 分钟压测。报告在 `artifacts/probe-macos-*.json`，截图留在本地（不提交相机画面）。

2026-10-07 本机结果：

| 检查 | 结果 |
| --- | --- |
| C++17 / Qt5 / GStreamer / 自编译 qmlglsink | 构建通过 |
| 彩条 + QML 准星 | 1280×720，正常显示 |
| A8 mini → Orin → Mac RTSP/TCP | 1920×1080，约 25 fps，`vtdec_hw`，通过 |
| 本地 H.265 文件 | 正常播放至 FINISHED，正常退出 |
| 客户端连接中断 4 秒 | 2 次重连后回到 PLAYING，正常退出 |
| 软件模式 | 界面正常、GL 视频禁用，通过 |
| Windows / 长时间稳定性 | 尚未验证 |

复现短时检查：

```bash
bash scripts/run-probe-macos.sh --test --seconds 6 --report artifacts/probe-macos-test.json
bash scripts/run-probe-macos.sh --seconds 15 --report artifacts/probe-macos-rtsp.json
python3 tests/a8mini/check-reconnect.py
```

重连检查只关闭本地 TCP 转发连接 4 秒，不重启 MediaMTX、不修改 Orin 网络。它会确认最终回到 PLAYING、重连次数增加、进程正常退出。`--seconds` 到期会写报告并退出；无视频帧或仍处在错误/连接状态时返回非零。
