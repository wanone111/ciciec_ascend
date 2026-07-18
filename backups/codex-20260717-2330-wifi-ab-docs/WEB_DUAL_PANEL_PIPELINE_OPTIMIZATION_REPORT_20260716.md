# 双栏 Web 前端与 310B 视频流优化报告

- 实施日期：2026-07-16
- PC 工程：`code/ascend_310B/smart_ip_interaction`
- 310B 工程：`/home/HwHiAiUser/Desktop/ciciec_ascend-main/code/ascend_310B/smart_ip_interaction`
- 摄像头归属：摄像头物理连接在 Ascend 310B，设备为 `/dev/video0`；PC 不直接打开摄像头。
- 当前结论：双栏页面、动漫事件联动、310B MJPEG 视频发布和健康检查已实现；最终异步流在本次同场景 A/B 中未产生可测的推理 FPS 损失。

## 1. 最终数据链

```text
Ascend 310B /dev/video0
  ├─ 最新帧覆盖缓存
  ├─ ACL + OM 三模型 NPU 推理
  ├─ TCP JSON 事件 ───────────────> PC FastAPI :9000
  │                                  └─ WebSocket /ws
  │                                      └─ 浏览器右栏动漫切换
  └─ 后台缩放/JPEG 编码
      └─ MJPEG :8081/stream.mjpg ───> 浏览器左栏实时镜头

PC FastAPI :8000
  ├─ /                         双栏页面
  ├─ /frontend/*               HTML/CSS/JS
  ├─ /media/*                  动漫 MP4，支持 Range 请求
  ├─ /frontend-config          流地址和视频映射
  └─ /ws                       互动事件
```

视频帧不经过 TCP JSON/WebSocket，也不做 Base64 封装。事件与视频使用独立通道，避免大帧阻塞动作/表情消息。

## 2. “视频流没有显示”的根因

1. 310B 的 MJPEG 接口曾能返回有效 multipart JPEG，说明摄像头归属和网络方向正确。
2. 初版前端使用持续 MJPEG `<img>` 的 `onload` 隐藏加载遮罩。部分 Chromium 环境不会为永不结束的 multipart 响应可靠触发 `onload`，导致实际画面被遮罩覆盖。
3. 初版 JPEG 在推理主线程同步执行；1280×720、10 FPS、质量 75 会直接增加单帧路径耗时，并且旧进程在约 4,185 个采集帧后无 Python traceback 退出，缺少可观测性。

修复后，浏览器先请求 `/snapshot.jpg`。单帧 JPEG 解码成功后立即移除遮罩，再切换到 `/stream.mjpg`。失败时每 2 秒重新探测，不再用持续流的 `onload` 判断健康。

## 3. 已实施优化

### 3.1 推理线程零编码

- `publish(frame)` 只提交最新帧引用，不执行 `resize` 或 `imencode`。
- 后台 `mjpeg-encoder` 线程独立完成缩放和 JPEG 编码。
- 待编码槽深度固定为 1；新帧覆盖旧帧，不形成延迟不断增长的队列。
- 一个编码结果由所有浏览器客户端共享，多客户端不会重复编码。

### 3.2 面向预览的流参数

最终参数：

```text
输出尺寸：640×360
流帧率上限：6 FPS
JPEG 质量：65
端口：8081
```

推理仍使用摄像头原始 1280×720 MJPG 输入；降采样只影响浏览器预览，不影响 NPU 输入或识别结果。

### 3.3 可观测性

新增接口：

- `GET http://192.168.2.2:8081/health`
- `GET http://192.168.2.2:8081/snapshot.jpg`
- `GET http://192.168.2.2:8081/stream.mjpg`

`/health` 返回提交帧、覆盖帧、编码帧、编码错误、最近/平均编码耗时、最新帧年龄及流参数。

### 3.4 浏览器动漫播放

- FastAPI 直接提供 MP4，浏览器以 HTTP Range 请求读取，不由 Python 解码视频。
- WebSocket 只传切换事件。
- 互动视频播放期间忽略新事件，避免频繁重载文件。
- 互动视频 `ended` 后自动恢复 idle 循环。

## 4. 实测性能

### 4.1 测试口径

- 同一台 Ascend 310B4、同一 `/dev/video0`、同一 PC 后端。
- 三模型强制 `acl_om_npu`，没有 CPU/ONNX/PTH fallback。
- 关闭流与开启最终异步流各取连续 6 个 10 秒统计窗口。
- 旧同步版本取稳定阶段连续 16 个统计窗口，不使用启动瞬时峰值。

### 4.2 推理吞吐对比

| 配置 | 平均推理 FPS | 相对旧同步版本 | 相对无流纯推理 |
|---|---:|---:|---:|
| 旧同步 JPEG，1280×720、10 FPS、Q75 | 9.44 | 基线 | -5.24% |
| 完全关闭浏览器视频流 | 9.96 | +5.53% | 基线 |
| 最终异步 JPEG，640×360、6 FPS、Q65 | 9.96 | +5.53% | 0.00% |

最终异步流的六个窗口为 `9.95、9.95、9.98、9.96、9.94、9.98 FPS`；无流对照为 `9.93、9.97、9.98、9.96、9.94、9.98 FPS`。在本次测试分辨率内，流媒体开销低于当前统计精度。

### 4.3 视频流指标

| 指标 | 960×540、8 FPS、Q70 | 最终 640×360、6 FPS、Q65 |
|---|---:|---:|
| 平均缩放+JPEG 编码 | 79.43 ms | 16.13–17.02 ms |
| 实测网络带宽 | 1.26 Mbps | 0.60 Mbps |
| 代表性快照大小 | 32,772 bytes | 14,095 bytes |
| 编码错误 | 0 | 0 |

最终版本的健康采样：

```json
{
  "status": "ok",
  "frames_submitted": 1299,
  "frames_overwritten": 637,
  "frames_encoded": 660,
  "encode_errors": 0,
  "average_encode_ms": 16.127,
  "latest_frame_age_seconds": 0.065,
  "stream_max_fps": 6.0,
  "stream_max_width": 640,
  "jpeg_quality": 65
}
```

覆盖帧是预期行为：推理产生的新帧比 6 FPS 预览编码更快，系统主动保留最新画面，避免累计延迟。

### 4.4 稳定性与资源

- 异步版本曾连续运行 16 分 41 秒后由测试人员主动停止；期间视频 `encode_errors=0`。
- A/B 最终窗口中 `camera_read_failures=0`、`camera_reconnects=0`、`tcp_reconnects=0`。
- 310B 推理进程采样：CPU 74.4%，RSS 489,776 KiB。
- NPU 采样：Health=OK，11.1 W，45°C，AICore 34%，内存 2028/7545 MB。
- PC FastAPI 采样：CPU 0.2%，RSS 48,988 KiB；3 个 WebSocket 客户端在线。

## 5. 当前启动与检查

310B 正式启动脚本已内置视频流默认参数：

```bash
cd /home/HwHiAiUser/Desktop/ciciec_ascend-main/code/ascend_310B/smart_ip_interaction
export SMART_IP_BACKEND_HOST=192.168.2.126
bash scripts/start_usb_direct.sh
```

PC：

```bash
cd /home/edison/AI-infra/ciciec_ascend/code/ascend_310B/smart_ip_interaction
SMART_IP_PC_BENCH_DIR=/home/edison/AI-infra/ciciec_ascend/log/web_frontend \
  /home/edison/miniconda3/envs/ai_infra/bin/python -u backend/server.py
```

浏览器打开：

```text
http://127.0.0.1:8000/
```

检查：

```bash
curl http://192.168.2.2:8081/health
curl -o /tmp/ascend_snapshot.jpg http://192.168.2.2:8081/snapshot.jpg
curl http://127.0.0.1:8000/status
```

## 6. 进一步优化建议

1. **优先修正 310B 系统时间。** 当前 310B 与 PC 的 Unix 时间相差明显，跨端绝对延迟仍不可可靠统计。
2. **治理表情事件偏置。** 现场多次连续输出 `sad`/`surprised`；应使用带标注样本重新校准表情阈值、平滑窗口和发送间隔，避免动漫频繁切换。
3. **多客户端时切换 H.264/WebRTC。** MJPEG 简单稳定，但每个客户端都消耗独立网络带宽；超过 2–3 个同时观看端时，建议使用硬件 H.264 + WebRTC/LL-HLS。
4. **将 FastAPI `on_event` 改为 lifespan。** 当前启动钩子可用但已被 FastAPI 标记弃用，后续应迁移以便统一关闭 TCP 线程和日志资源。
5. **增加端到端浏览器指标。** 浏览器可上报首帧时间、视频切换时间、丢失事件和播放错误，补齐从模型事件到用户实际看到画面的闭环。

## 7. 判定

已验证：310B 摄像头单一占用、真实 NPU 推理、PC TCP/WebSocket、浏览器动漫播放、单帧快照、持续 MJPEG、最新帧覆盖、互动结束回 idle，以及开启/关闭视频流的同场景性能对照。

未宣称：浏览器视频达到硬实时、表情准确率达标或跨端绝对延迟达标。上述结论仍需要时钟同步和带标注真人试验。

## 8. 2026-07-17 海康 GigE 输入复测

视频输入已从 `/dev/video0` 切换为海康 `MV-CS060-10GC`（`192.168.1.200`）通过 310B `eth1` 的 MVS SDK 取流，不使用 PYNQ。310B 新增 `scripts/start_hik_direct.sh`，MJPEG 仍复用推理主链路帧，不会二次打开相机。

实测：

- `GET /health`：HTTP 200，状态 `ok`。
- `GET /snapshot.jpg`：HTTP 200，有效 JPEG，现场样本 8438 字节。
- `GET /stream.mjpg`：HTTP 200，5 秒接收 186585 字节。
- 300 帧运行：提交 300，后台编码 115，覆盖 185，编码错误 0。
- 同口径基线对比：`frame_total_ms` 平均 59.448 -> 58.607 ms，P95 88.910 -> 86.708 ms。
- 相机侧：采集 309、交付 300、覆盖 8、timeout 0、GigE lost packet 0。

最新帧覆盖仍是预期的低延迟策略。完整报告位于 310B 工程 `docs/HIK_GIGE_OPTIMIZATION_REPORT_20260717.md`，性能证据位于 `artifacts/optimized/20260717_hik_gige_300_v3/`。

当前快照显示现场画面很暗且失焦，且没有真人入镜；因此仅确认 Web/视频/NPU/通信工程链路，暂不把动作和表情准确率列为已验收。
