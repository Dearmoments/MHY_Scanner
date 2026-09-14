# B 站直播流初始化失败修复

最后更新：2026-09-15 00:26:00 +08:00

## 原因与改动

直播间 `1826344983` 正常开播。原程序能拿到 FLV 地址，但交给 FFmpeg 时没有携带直播 CDN 要求的 `Referer` 和浏览器 `User-Agent`，因此打开视频流时收到 HTTP 403。

`LiveStreamInfo` 现在同时返回视频地址和 FFmpeg 请求选项；B 站解析器提供 `referer=https://live.bilibili.com/`、浏览器 `user_agent`，主窗口把它们传给原有 `QRCodeForStream::setUrl()`。保留 qn=80、16 KiB 探测大小、200 ms 分析时长和无缓冲设置，抖音分支仍返回空选项。

## 实测结果

- 使用原安装目录中的 FFmpeg DLL，对同一个实时视频地址测试：不带请求头、只带 Referer、只带 User-Agent 均收到 403；两项都带上后，打开和流信息探测均成功，识别出 1920×1080、60 fps 的 H.264 视频。
- 使用本次构建的实际 `LiveStreamLink` 和 `QRCodeForStream` 代码测试：`--no-headers` 返回 `scanner_init=0`、退出码 5；默认模式返回 `scanner_init=1`、退出码 0。
- MSVC Release 构建通过。已替换原 `MHY_Scanner_1.2-lowlatency-final` 目录中的 EXE 及配套 Qt 6.10.1 运行库。启动检查通过，进程响应正常，Qt、FFmpeg、qwindows 均从该安装目录加载。测试实例已关闭。
- 账号配置未参与替换。本次验证覆盖直播初始化和程序启动，未执行真实游戏二维码的扫码确认。

本机证据保存在工作区 `artifacts` 下：`bili-1826344983-header-recheck.log`、`livefix-native-original.log`、`livefix-native-fixed.log`、`livefix-build.log`、`livefix-launch.json`、`livefix-loaded-modules.json`。

原运行文件备份：`E:\200SourceSoft\米哈游扫码\artifacts\backup-before-bili-fix-20260915-002230`。备份包含原 EXE、PDB、DLL 和 plugins，不包含账号配置。新程序使用 `qt.conf` 明确指定 `plugins` 路径。

## 重跑初始化验证

这是手动联网测试，需要房间正在直播。验证程序使用临时配置目录，不启动账号扫码线程。将以下参数加入正常项目的 CMake 配置命令，启用额外验证目标：

```text
-DCMAKE_PROJECT_MHY_Scanner_INCLUDE=<源码绝对路径>/tests/live_stream_probe.cmake
```

随后构建 `live_stream_probe`，将它放入具有对应 Qt、FFmpeg DLL 的目录：

```powershell
.\live_stream_probe.exe 1826344983 --no-headers
.\live_stream_probe.exe 1826344983
```

退出码：0 为初始化成功，2 为参数错误，3 为临时目录不可用，4 为房间未开播或解析失败，5 为视频流初始化失败。是否要求这两个请求头由当前 CDN 节点决定，不能保证所有房间或未来节点的无请求头对照都返回 403。
