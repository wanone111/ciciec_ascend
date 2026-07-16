# Ascend 310B USB 直连推理链实施报告

- 报告时间：2026-07-15 18:43:21 +08:00
- 当前结论：**USB 采集与三模型真实 NPU 推理路径已通过；完整互动验收尚未通过。** 未完成项是 Windows 防火墙许可、真人动作/表情事件、动画事件后回 idle、USB 实体拔插重连。
- 判定原则：未用 mock、模拟事件、CPU/ONNX/PTH fallback 代替正式验收。

## 1. 工程与版本

1. 本地工程路径：`C:\Users\32035\Desktop\JC\ciciec_ascend-main\code\ascend_310B\smart_ip_interaction`（PC 后端与动画）；改造基线取自远端实际工程快照。
2. Ascend 实际工程路径：`/home/HwHiAiUser/Desktop/ciciec_ascend-main/code/ascend_310B/smart_ip_interaction`。
3. Git 分支：不适用。本地两个候选副本和远端实际工程均不是 Git 工作树，因此没有伪造 `test/ascend-usb-camera-direct` 分支。
4. 用户指定的两份 20260715 交接文档在本地候选路径和远端工程中均未找到：`PYNQ-Z2_310B_工程交接与启动报告_20260715.md`、`AGENT_HANDOFF_PYNQ-Z2_310B_20260715.md`。实施以远端真实代码和运行状态为准。
5. 修改前远端备份：`/home/HwHiAiUser/Desktop/ciciec_ascend-main/.codex_backups/usb_direct_20260715_095125`。

## 2. SSH 与网络

6. Ascend SSH Host：`ascend310b`。
7. Ascend 实际 IP：`192.168.2.2`。
8. Ascend 实际用户名：`HwHiAiUser`；主机名：`davinci-mini`。
9. PC 与 Ascend 互通接口：PC 以太网 `192.168.2.5/24`。
10. SSH 公钥免密：成功，仅上传已有 `id_ed25519.pub`，未复制私钥。
11. BatchMode 验证：成功。`ssh -o BatchMode=yes ascend310b "echo SSH_KEY_OK && hostname && whoami"` 输出 `SSH_KEY_OK`、`davinci-mini`、`HwHiAiUser`，无密码提示。

## 3. USB 摄像头

12. 摄像头品牌/名称：`Web Camera Web Camera`。
13. USB VID:PID：`32e6:d412`；序列号 `202404120005`。
14. 实际图像节点：`/dev/video0`；`/dev/video1` 不是图像采集节点。
15. 实际像素格式：MJPG。
16. 实际配置：1280×720，请求/设备报告 30 FPS。
17. 独立 300 帧验证：300/300 成功、0 失败，BGR `uint8`、形状 `720×1280×3`；实际读取吞吐 14.718 FPS，平均读取 66.87 ms。
18. 实拍图检查：颜色、尺寸、亮度正常，无全黑、全绿或花屏。当前物理镜头横置 90°，最近实拍画面内无人。
19. 权限：`HwHiAiUser` 对 `/dev/video0` 有读写 ACL，无需 `chmod 777` 或长期提权。

## 4. 实现内容

20. 新增文件：
    - `src/input_sources/__init__.py`
    - `src/input_sources/base.py`
    - `src/input_sources/config.py`
    - `src/input_sources/udp_source.py`
    - `src/input_sources/usb_camera_source.py`
    - `scripts/start_usb_direct.sh`
    - `scripts/stop_usb_direct.sh`
    - `tests/test_input_sources.py`
21. 修改文件：
    - `run_pipeline.py`
    - `src/communication/tcp_sender.py`
    - `README.md`
22. `SMART_IP_INPUT_MODE`：允许 `usb`/`udp`，大小写归一化，非法值显式报错；默认 `udp` 保持向后兼容。
23. USB 输入类：V4L2 `cv2.VideoCapture(device, cv2.CAP_V4L2)`；设置 FOURCC、宽高、FPS、buffer size；首帧失败即退出；独立采集线程写入深度 1 的覆盖缓存；连续失败后只重连原设备路径；`close()` 释放 capture。
24. 最新帧策略：新帧覆盖未消费旧帧，无无限队列；线程安全；统计 capture FPS、processed、latest-frame drops、read failures、reconnects。
25. 原 UDP 模式：保留。`UDPInputSource` 继续包装原 `communication/udp_video_source.py`，不复制 NPU 推理/动作/表情逻辑，也不静默切 USB/mock。
26. UDP 恢复冒烟验证：原 `.so` 是 `video_stream_interface.cpython-39-aarch64-linux-gnu.so`，使用原 `/usr/local/miniconda3/bin/python3.9` 成功绑定/释放 9090，输出 `UDP_OPENED=True`、`UDP_CLOSED=True`。系统默认 Python 3.10 与该旧 ABI 不兼容，README 已写明解释器要求。

## 5. NPU 与 USB 主链实测

27. 启动日志后端：
    - `pose_backend=acl_om_npu`
    - `face_backend=acl_om_npu`
    - `expression_backend=acl_om_npu`
28. 三个实际 OM：`yolo26s-pose.om`、`face_detection_yunet_2023mar_640.om`、`emotion_mobilenetv3_6cls.om`。
29. 禁止 fallback：三分支 `require_npu=True`，PT/OpenCV/PTH fallback 均为 False。
30. USB 模式占用 UDP 9090：否。运行时 `ss -lunp` 显示 `NOT_BOUND`。
31. 稳定运行时长：1170.57 秒（19 分 30.57 秒），共记录 16,154 个真实帧。
32. 摄像头采集 FPS：稳定约 14.90 FPS；末次累计 captured=17,199、processed=16,148、latest-frame drops=1,050、read failures=0、reconnects=0。
33. 推理 FPS：运行统计约 13.6–14.1 FPS；逐帧字段均值 14.275 FPS（中位数 15.503，含启动/调度抖动）。
34. 姿态推理：均值 47.544 ms，中位数 47.197 ms，P95 49.669 ms。
35. 人脸检测：每 3 帧运行一次，均值 21.552 ms，中位数 21.291 ms，P95 23.040 ms。
36. 表情分类：由于实拍画面无人，未获得 face ROI，故 `expression_infer_ms` 无真人样本；没有伪造该数值。人脸+表情分支总耗时均值 21.557 ms，此时仅包含人脸检测。
37. 单帧总耗时：均值 61.422 ms，中位数 54.375 ms，P95 76.611 ms。
38. NPU：310B4 Health=OK；运行采样约 11.6 W、42°C、AICore 26%、内存 4171/7545 MB。
39. 进程资源：19 分钟采样 PID 129451，CPU 80.6%、RSS 483,240 KiB、VSZ 1,499,176 KiB。
40. 稳定性：19.5 分钟内摄像头失败 0、重连 0、进程无崩溃、USB 无历史帧累积；NPU 内存早晚采样约 4157→4171 MB，未见持续异常增长。TCP 在后端可达后自动建立，未导致推理进程退出。

## 6. PC 后端、WebSocket 与动画

41. PC 实际通信 IP：`192.168.2.5`。
42. FastAPI 8000：本机正常，`/status` 返回 `{"ws_clients":1,"current_scene":0,"cheer_count":0}`。
43. TCP 9000：后端监听 `0.0.0.0:9000`；曾真实接收来自 `192.168.2.2` 的连接，310B 日志显示 `[TCP] 已连接 192.168.2.5:9000`。停止主链后，新连接被 Windows 自动生成的 Python Public-profile 入站阻止规则覆盖；需用户授权删除/禁用该精确 TCP 阻止规则后才能继续正式脚本验收。
44. WebSocket：成功，`ws_clients=1`。
45. 本地动画窗口：成功，已用桌面可视检查确认 `IP Interaction Display` 窗口显示熊二 idle，不是 headless/隐藏模式。
46. WAVE 实测：未验证；摄像头最近实拍无人。
47. HEART 实测：未验证；摄像头最近实拍无人。
48. CHEER 实测：未验证；摄像头最近实拍无人。
49. 开心/惊讶表情实测：未验证；没有 face ROI。
50. 动画结束自动回 idle：初始 idle 已验证；真实事件后的恢复尚未验证。

## 7. 摄像头断开重连

51. 代码与单元测试已验证失败计数、释放与关闭路径；实机运行 19.5 分钟无自然读失败。
52. 实体拔出/重新插入：尚未执行，需要现场人员物理操作。预期日志关键字是 `camera read failed`、`camera reconnecting`、`camera reconnected`。
53. 节点变化策略：只重连显式配置的 `/dev/videoX`，不会静默切换到错误设备；若重插后节点变化，更新 `SMART_IP_CAMERA_DEVICE` 并重启专用脚本。

## 8. 质量验证

54. `python3 -m compileall -q .`：通过。
55. 单元测试：7/7 通过，覆盖模式解析、USB 配置覆盖、非法模式、摄像头打开失败、最新帧覆盖缓存、资源释放。
56. `bash -n scripts/start_usb_direct.sh` 与 `stop_usb_direct.sh`：通过。
57. 工程非 Git 仓库，无法直接执行工作树 `git diff --check`；对三份修改的既有文件执行等价 `git diff --no-index --check`，无 whitespace error（退出码 1 仅表示文件有差异）。
58. 未提交密钥、密码、Token、`.env`、日志大文件、视频缓存或构建缓存。

## 9. 正式 USB 启停命令

PC 后端与播放器：

```powershell
cd C:\Users\32035\Desktop\JC\ciciec_ascend-main\code\ascend_310B\smart_ip_interaction
.\.venv\Scripts\python.exe backend\server.py
.\.venv\Scripts\python.exe scripts\video_player.py
```

Ascend USB 启动：

```bash
ssh ascend310b
cd /home/HwHiAiUser/Desktop/ciciec_ascend-main/code/ascend_310B/smart_ip_interaction
export SMART_IP_BACKEND_HOST=192.168.2.5
export SMART_IP_BACKEND_PORT=9000
export SMART_IP_INPUT_MODE=usb
export SMART_IP_CAMERA_DEVICE=/dev/video0
export SMART_IP_CAMERA_WIDTH=1280
export SMART_IP_CAMERA_HEIGHT=720
export SMART_IP_CAMERA_FPS=30
export SMART_IP_CAMERA_FOURCC=MJPG
bash scripts/start_usb_direct.sh
tail -f logs/usb_direct_latest.log
```

Ascend USB 停止：

```bash
cd /home/HwHiAiUser/Desktop/ciciec_ascend-main/code/ascend_310B/smart_ip_interaction
bash scripts/stop_usb_direct.sh
```

## 10. 恢复 PYNQ UDP 模式

```bash
ssh ascend310b
cd /home/HwHiAiUser/Desktop/ciciec_ascend-main/code/ascend_310B/smart_ip_interaction
bash scripts/stop_usb_direct.sh
source /usr/local/Ascend/ascend-toolkit/set_env.sh
export SMART_IP_INPUT_MODE=udp
export SMART_IP_UDP_PORT=9090
export SMART_IP_UDP_MODULE_PATH=/home/HwHiAiUser/Desktop/ciciec_ascend-main/code/communication/build
export SMART_IP_BACKEND_HOST=192.168.2.5
export SMART_IP_BACKEND_PORT=9000
PYTHONPATH=.:src /usr/local/miniconda3/bin/python3.9 run_pipeline.py
```

之后按原流程启动 PYNQ 端 `pynq_udp`。USB 模式本身不要求、不启动 PYNQ、`pynq_udp` 或 310B UDP 接收进程。

## 11. 未解决问题与最终判定

1. Windows 自动生成了对后端所用 Python 的 TCP/UDP Public-profile 入站阻止规则。只需处理精确 TCP 阻止规则；等待用户明确授权，尚未擅自修改防火墙。
2. 摄像头当前横置且无人入镜，无法完成真人 WAVE/HEART/CHEER 和开心/惊讶表情验收。
3. USB 实体拔插需要现场人员执行。
4. 310B 系统时间未同步（日志 Unix 时间明显早于 PC 当前时间），未修改系统时钟；跨端绝对时延暂不具备可靠时钟基础。
5. 默认 Python 3.10 与旧 UDP CPython 3.9 `.so` ABI 不兼容；已确认原 Miniconda Python 3.9 路径可用并写入恢复命令。

因此本报告**不宣称“USB 直连测试路径成功”**。已通过的是 USB→BGR→三 OM ACL NPU、长期稳定运行、TCP 曾建立、FastAPI、WebSocket 和可见 idle 窗口；最终成功仍需完成至少一个真人动作、一个真人表情、动画恢复 idle 和实体拔插重连。

## 12. 2026-07-16 双栏 Web 前端与视频流优化补充

摄像头继续物理连接在 Ascend 310B 的 `/dev/video0`，PC 不直接打开摄像头。新增链路由 310B 推理进程复用最新采集帧，通过后台异步 JPEG 编码发布 `:8081/stream.mjpg`；浏览器左栏显示该实时画面，右栏通过原 WebSocket 事件播放动漫 MP4。

最终预览参数为 640×360、6 FPS、JPEG 质量 65。同场景 A/B 中，无流纯推理和开启异步流的六窗口平均值均为 9.96 FPS；旧同步编码版本为 9.44 FPS。最终流平均编码约 16.13–17.02 ms、实测带宽约 0.60 Mbps、编码错误 0，且没有可测的推理吞吐损失。

前端不再依赖持续 MJPEG 的 `img.onload`；先用 `/snapshot.jpg` 验证并移除遮罩，再切换持续流。完整实现、测试口径、资源采样和后续优化见 [WEB_DUAL_PANEL_PIPELINE_OPTIMIZATION_REPORT_20260716.md](WEB_DUAL_PANEL_PIPELINE_OPTIMIZATION_REPORT_20260716.md)。
