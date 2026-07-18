# 海康 GigE 相机直连 Ascend 310B 优化与验证报告

> 2026-07-18 更新：相机已迁移到 `192.168.2.253/24`，310B 从
> `192.168.2.2` 直连访问。下方 `192.168.1.200/24` 与 yolo26s 数据保留为
> 2026-07-17 历史基线，当前生产首选姿态模型已经切换为
> `yolo26n-pose-640.om`，当前海康推荐预处理为 packed-BGR AIPP。

## 1. 现场链路

- 相机：Hikrobot `MV-CS060-10GC`，序列号 `DA9807825`
- 相机地址：`192.168.1.200/24`
- 310B 接口：`eth1`，地址 `192.168.1.252/24`
- SDK：`/opt/MVS`，MVS 3.0.1，SDK API 报告 4.7.0.3
- 数据链：GigE 相机 -> 310B MVS SDK -> ACL/OM 三模型 -> TCP 事件 + MJPEG
- 不使用 PYNQ/UDP 视频输入。

启动：

```bash
cd /home/HwHiAiUser/Desktop/ciciec_ascend-main/code/ascend_310B/smart_ip_interaction
bash scripts/start_hik_direct.sh
```

## 2. 已完成的远端改动

1. 新增 `src/capture/hik_gige_source.py`，按 IP/序列号选择相机，自动设置 8164 字节 GigE 包长，暴露帧号、丢包和采集统计。
2. 采集使用深度 1 的 latest-frame 缓存；后台只复制原始 RGB，实际消费时才转 BGR。默认把相机帧率限制为 12 FPS，避免无效转换和 CPU 争用。
3. 三个 `AclOmSession` 共享一个 ACL device context，最后一个 session 关闭时统一 destroy/reset/finalize。
4. 增加姿态、人脸、表情的 preprocess/H2D/execute/D2H/postprocess 分段计时和单调时钟 trace。
5. 修复表情模型 uint8/AIPP 分支在 `rgb` 定义前使用变量的问题，并增加 NCHW/NHWC uint8 测试。
6. JSONL 改为有界异步写盘；TCP 发送增加队列上限、TTL、溢出/过期丢弃、发送时间和排队年龄统计。
7. 增加 OM 契约审计、单模型微基准、`msprof` 采集及算子聚合脚本。
8. 保留原 MJPEG 深度 1 异步编码；验证 `/health`、`/snapshot.jpg` 和 `/stream.mjpg`。

## 3. OM 契约

| 模型 | 输入 | 输出摘要 | SHA256 |
|---|---|---|---|
| yolo26s-pose | float32 NCHW 1x3x640x640 | 1x300x57 | `ef0a2127...4592d3` |
| YuNet 640 | float32 NCHW 1x3x640x640 | 12 路 head | `2525766b...090a6d` |
| emotion 6cls | float32 NCHW 1x3x112x112 | 1x6 | `b2a412df...fe5bf` |

三模型同时加载时共享 runtime 引用计数为 3。完整结果：
`artifacts/contracts/20260717_hik_gige/contracts.json`。

## 4. 300 帧同口径 A/B

统计均排除前 30 帧；overlay、AR、ACL profile 和 640x360@6FPS/Q65 MJPEG 均开启。

| 指标 | 原始同步采集 | latest-frame + 相机 12 FPS | 变化 |
|---|---:|---:|---:|
| read_ms 平均 | 3.392 ms | 2.663 ms | -21.5% |
| pose_ms 平均 | 49.279 ms | 48.619 ms | -1.3% |
| expression branch 平均 | 22.724 ms | 21.317 ms | -6.2% |
| frame_total_ms 平均 | 59.448 ms | 58.607 ms | -1.4% |
| frame_total_ms P95 | 88.910 ms | 86.708 ms | -2.5% |
| 处理 FPS 字段平均 | 12.180 | 12.435 | +2.1% |

最终采集：309 帧，交付 300 帧，覆盖旧帧 8，timeout 0，GigE lost packet 0；采集到整帧处理完成延迟平均 77.478 ms、P95 123.188 ms。日志写入 310 条、丢弃 0。

最终记录：`artifacts/optimized/20260717_hik_gige_300_v3/`。

## 5. Web/MJPEG 验证

- `/health`：HTTP 200，状态 `ok`
- `/snapshot.jpg`：HTTP 200，有效 JPEG，8438 字节（现场帧）
- `/stream.mjpg`：HTTP 200，5 秒接收 186585 字节
- 300 帧：提交 300，编码 115，覆盖 185，编码错误 0

覆盖是预期的低延迟策略，不是丢流故障。

## 6. 单模型和算子证据

| 模型 | ACL execute 平均 | msprof 设备 task 平均 | AICPU calls | 首要热点 |
|---|---:|---:|---:|---|
| pose s640 | 32.896 ms | 30.703 ms | 0 | GatherElements，17.57% |
| YuNet 640 | 7.532 ms | 6.298 ms | 0 | 首层 Conv2D，20.39% |
| expression | 1.346 ms | 0.900 ms | 0 | MatMulV2，10.91% |

原始 trace、环境、微基准和 Top-K：`artifacts/profiling/20260717/`。

## 7. 其他验证

- 板端单元测试：10/10 通过。
- TCP 本机回环：3/3 JSON 按序接收，发送错误 0，队列丢弃 0。
- JSONL 2000 条热路径：同步版 36.895 us/条，异步版 13.924 us/条，下降 62.3%；关闭时会完整 drain。
- NPU 健康状态：`OK`；测试进程结束后模型正常释放。

## 8. 尚不能宣称完成的验收

现场画面很暗且失焦，画面中没有可用于统计的人脸或动作；因此本次只能确认采集、三模型加载/执行能力、传输和 Web 链路，不宣称 WAVE/HEART/CHEER 或 6 类表情准确率达标。需要人工调焦、补光并让测试人员入镜后，按每类至少 10 次完成准确率回归，才可替换默认姿态模型或调整表情阈值。

## 9. 回退

- 本轮运行时改动备份：`backups/codex-20260717-2145-runtime-opt/`
- 海康接入前主入口备份：`backups/codex-20260717-2055-hik-gige/run_pipeline.py`
- `SMART_IP_HIK_LATEST_FRAME=0` 可关闭 latest-frame；`SMART_IP_HIK_ACQUISITION_FPS=0` 可取消相机帧率限制。

## 10. 2026-07-18 yolo26n 与 AIPP 追加验证

当前相机 `192.168.2.253` 的 10 帧采集检查为 10/10 成功，帧尺寸
`1928x1078`，timeout 和 lost packet 均为 0。`yolo26n-pose-640.onnx`
与现有 n640 float OM 的输出经新转换的 audit-float OM 逐项确认一致。

| 姿态链路（100 帧） | frame mean / P95 | pose mean / P95 | 结论 |
|---|---:|---:|---|
| n640 CPU float32 | 42.012 / 75.639 ms | 34.108 / 49.549 ms | 回退基线 |
| n640 packed-BGR AIPP | 31.083 / 50.129 ms | 23.803 / 29.361 ms | 当前海康推荐 |
| n640 BGR→NV12 + DVPP+AIPP | 229.644 / 312.706 ms | 221.803 / 305.121 ms | 当前海康不推荐 |

packed-BGR AIPP 将姿态 H2D 有效输入从 4,915,200 字节降到 1,228,800
字节。标准人物图的主 bbox IoU 为 1.0，score 差为 0，关键点最大误差
0.5 px，动作规则输出一致。NV12 候选的主 bbox IoU 为 0.99545，动作输出
同样一致，但海康 SDK 当前交付 BGR；实测主机 BGR→NV12 转换平均
194.783 ms，抵消了 DVPP/AIPP 收益。因此 NV12 变体仅保留给未来原生
NV12 输入，不作为当前网口相机启动默认值。

本轮切换前备份：
`backups/codex-20260718-yolo26n-pose-pre/changed-files.tar.gz`，SHA-256
`b00d703fa29357f9d1f0645b8e93855f7f73407b1bd2f576d45ccd56605a2db5`。

