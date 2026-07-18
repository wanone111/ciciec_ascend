# DVPP + AIPP integration report

Date: 2026-07-18  
Board: Ascend 310B4 at `192.168.2.2` (`HwHiAiUser`)  
Current source: Hikrobot GigE camera `MV-CS060-10GC`, `192.168.2.253`  
Status vocabulary: only commands actually run on the board are labelled measured/passed.

## 1. Repository and baseline audit

The formal local workspace was older than the live board and initially had no `tests/`, conversion script, TODO or Hik report. Before feature work, the live board versions were matched by SHA-256 to the saved `tmp/remote-opt`/`tmp/live-fix` snapshots and promoted into the formal directory. The board baseline was then run before deployment: 12 tests passed.

Audited preprocessing semantics:

- Pose: CPU centered letterbox, BGR to RGB, divide by 255, NCHW float32.
- Face: CPU aspect-ratio resize, top-left paste into a black 640x640 canvas, BGR NCHW float32, no division by 255.
- Expression: CPU face ROI crop and resize, BGR to RGB, divide by 255, NCHW float32. Seven-frame probability smoothing and class order are unchanged.
- UDP NV12 was immediately converted to BGR in the old adapter.
- The board baseline `AclOmSession` already shared one reference-counted ACL runtime/context; this design was retained.
- The PYNQ C++ sender requests RGB24 and only falls back to MJPEG. Protocol support for NV12 does not mean the sender currently emits NV12.

Real OM contracts measured on the board:

| Model | Input name | Dims | Bytes | Contract |
|---|---|---:|---:|---|
| `yolo26n-pose-640.om` | `images` | 1x3x640x640 | 4,915,200 | float32 NCHW, current default |
| `yolo26n-pose-640_aipp_bgr_u8.om` | `images` | 1x640x640x3 | 1,228,800 | packed BGR uint8 AIPP |
| `yolo26n-pose-640_aipp_nv12.om` | `images` | 1x640x640x3 | 614,400 | NV12 AIPP, byte-audited |
| `face_detection_yunet_2023mar_640.om` | `input` | 1x3x640x640 | 4,915,200 | float32 NCHW |
| `emotion_mobilenetv3_6cls.om` | `image` | 1x3x112x112 | 150,528 | float32 NCHW |
| `emotion_mobilenetv3_6cls_aipp_bgr_u8.om` | `image` | 1x112x112x3 | 37,632 | packed BGR uint8 AIPP |
| `emotion_mobilenetv3_6cls_aipp_nv12.om` | `image` | 1x112x112x3 | 18,816 | NV12 AIPP, byte-audited |

Checksums measured on the board: n640 ONNX `8e8f081f...f722a86`, existing n640 float OM `fa86f389...f04f15b`, fresh n640 audit-float OM `ec65cc8c...fa1dbe`, n640 BGR-AIPP `28ba7dd8...64bd6`, n640 NV12-AIPP `005b03b4...b7786`, face `2525766b...0090a6d`, expression `b2a412df...9cfe5bf`, expression BGR-AIPP `14f269ec...54a43d1`, expression NV12-AIPP `d6613798...48cac`. The existing n640 float OM and fresh ONNX conversion produced bit-exact tensors across the validation set, proving that the selected ONNX matches the deployed n640 model contract.

## 2. Data flows and rollback

```text
Default, unchanged:
Hik GigE -> BGR host -> CPU resize/crop/color/float NCHW -> H2D -> old OM

Phase A, opt-in:
Hik GigE -> BGR host -> CPU geometry only -> packed BGR uint8 -> H2D -> AIPP OM

Phase B, opt-in:
Hik GigE -> BGR host -> transitional full-range NV12 conversion -> one H2D upload
         -> DVPP pose/face/expression geometry -> D2D -> NV12 AIPP OM

Prepared Phase C (not hardware-validated in this run):
PYNQ NV12 -> owned UDP raw bytes -> one H2D upload -> shared DVPP derivatives
          -> D2D -> AIPP OM
```

All new paths are controlled by `SMART_IP_PREPROCESS_BACKEND`, `SMART_IP_DVPP_ENABLE`, `SMART_IP_RAW_NV12_ENABLE` and explicit per-model input modes. Unset variables select the legacy CPU path. `external` input is deliberately rejected until dataset lifetime is validated; safe D2D is implemented.

Board rollback archive:

`code/ascend_310B/smart_ip_interaction/backups/codex-20260718-dvpp-aipp-pre/changed-files.tar.gz`  
SHA-256: `d523aa44667cdcf40ed6542ef6e40bbf99d0feb7e5ae3642e9461a9179becc45`.

The yolo26n switch also has an independent pre-change rollback archive:

`code/ascend_310B/smart_ip_interaction/backups/codex-20260718-yolo26n-pose-pre/changed-files.tar.gz`  
SHA-256: `b00d703fa29357f9d1f0645b8e93855f7f73407b1bd2f576d45ccd56605a2db5`.

## 3. AIPP color and normalization

Packed BGR AIPP uses `RGB888_U8` plus `rbuv_swap_switch:true`. Pose/expression use reciprocal variance `1/255`; face remains on the old float model by default. The n640 pose BGR-AIPP configuration passed decoded-output parity, including red/blue channel tests and a real-person pose sample.

PYNQ HLS implements full-range-style BT.601 without a Y+16 offset:

```text
Y = 0.299R + 0.587G + 0.114B
U = -0.169R - 0.331G + 0.500B + 128
V = 0.500R - 0.419G - 0.081B + 128
```

The inverse Q8 matrix is RGB rows `(256,0,359)`, `(256,-88,-183)`, `(256,454,0)`, with input biases `(0,128,128)`. Face swaps output rows to BGR and uses reciprocal variance 1.0. Packed-BGR and NV12 variants for both n640 pose and expression passed board comparisons. Face remains experimental because its model-specific bbox/score parity is still unavailable.

## 4. DVPP design

- `DvppProcessor` receives `AclOmSession.shared_runtime`; it never calls `acl.init`, `set_device` or `create_context` independently.
- One stream/channel and resolution-keyed source/model buffers are reused. Allocations change only when geometry changes.
- NV12 size is always `width_stride * height_stride * 3 // 2`; default width/height alignments are 16 and 2.
- Inclusive YUV420 ROI uses even left/top and odd right/bottom. Expression metadata records original bbox, aligned bbox and scales.
- Pose reproduces the CPU `round` and `+/-0.1` border rules before YUV alignment. Canvas Y is 114 and UV is 128, initialized separately every frame. For the measured 1928x1078 camera frame, the CPU requests a 141-pixel top pad; YUV420 requires an even top ROI, so the real DVPP mapping is top 140/bottom 142 and the returned postprocess pad is `(0,140)`. This one-pixel constraint is recorded rather than hidden.
- Face paste stays at the top left. Full-range black is Y=0, UV=128.
- 640 and 112 outputs are tightly aligned already, so their NV12 sizes match 614,400 and 18,816 bytes. Other strides are exposed in `DvppImage` and audit output.

## 5. Resource lifecycle

`AclOmSession` retains the board's shared runtime refcount and reusable pinned output views. Device input validates model path, dims, configured mode and exact bytes before D2D. DVPP close is idempotent and releases buffers, channel descriptors and stream. Pipeline emergency cleanup stops capture first, then DVPP/model resources; the last model session releases/finalizes the shared runtime.

## 6. Tests and measured board baseline

Local and board command:

```bash
cd code/ascend_310B/smart_ip_interaction
PYTHONPATH=src python3 -m unittest discover -s tests -v
```

- Original live-board baseline: 12/12 passed.
- Updated local suite: 28/28 passed.
- Updated board suite: 28/28 passed.
- Tests cover packed BGR contracts, D2D bytes/kind, pose/face geometry, Y/UV canvas values, stride/ROI alignment, full-range primaries, DVPP reuse/close, UDP ownership/no `cvtColor`, legacy buffer reuse and logger/stream regressions.

Board environment probe: driver/NPU reports 310B4 healthy; CANN toolkit is `7.0.RC1`; all required resize/crop/paste APIs exist. Python ACL exposes no pixel enum, while the installed `acl_dvpp.h` defines `PIXEL_FORMAT_YUV_SEMIPLANAR_420 = 1`.

Measured Hik CPU baseline, 100 frames:

| Metric | Mean | P95 |
|---|---:|---:|
| frame total | 51.40 ms | 66.50 ms |
| pose branch | 44.26 ms | 46.81 ms |
| expression branch when run | 20.27 ms | 22.06 ms |
| reported instantaneous FPS | 12.48 | 16.07 |

Camera source result: 100 delivered frames, zero overwritten frames, zero timeout, zero lost packets; measured delivery FPS 10.45. Pose ACL breakdown: H2D 1.92 ms average, execute 32.12 ms, D2H 0.10 ms. No accuracy claim is made from this scene.

The original table above used the retained yolo26s rollback model. After switching the default to the contract-matched yolo26n 640 model, three additional 100-frame Hik runs at camera address `192.168.2.253` measured:

| Pose path | Frame mean / P95 | Pose mean / P95 | Input/geometry note | Camera result |
|---|---:|---:|---|---|
| n640 CPU float32 | 42.012 / 75.639 ms | 34.108 / 49.549 ms | 4,915,200-byte H2D | 100/100 delivered, timeout/loss 0 |
| n640 packed-BGR AIPP | 31.083 / 50.129 ms | 23.803 / 29.361 ms | 1,228,800-byte H2D | 100/100 delivered, timeout/loss 0 |
| n640 BGR-to-NV12 + DVPP+AIPP | 229.644 / 312.706 ms | 221.803 / 305.121 ms | host conversion 194.783 ms mean | 100 delivered; 175 overwritten, 482 lost packets |

The packed-BGR AIPP path is therefore the production recommendation for the current Hik BGR source. The NV12 model itself is valid, but this camera path has no native NV12 frame: host conversion dominates latency and causes the latest-frame queue to discard frames. The DVPP+NV12 path remains useful for a future native-NV12 source, not for current Hik production.

Measured n640 pose parity on the standard person image (`zidane.jpg`):

| Candidate | Detections | Main bbox IoU | Score abs delta | Keypoint XY error | Action output | ACL total |
|---|---:|---:|---:|---:|---|---:|
| fresh float audit vs existing n640 float | 2/2 | 1.0 | 0 | 0 px | identical | baseline |
| packed-BGR AIPP vs float | 2/2 | 1.0 | 0 | max 0.5 px | identical | 18.491 ms vs 21.627 ms |
| DVPP+NV12-AIPP vs float | 2/2 | 0.99545 | 0 | mean 0.581 px, max 6.5 px | identical | 18.288 ms |

The saved Hik frame contained no accepted person detections, so it was used only for camera and geometry checks. The person image supplies the decoded bbox/keypoint/action parity evidence. Low-confidence raw anchors can differ after 4:2:0 interpolation; acceptance is based on decoded detections, coordinates and action rules rather than claiming bit-exact NV12 tensors.

Measured static expression AIPP-BGR parity:

| Input | Raw tensor max abs | Top-1 | Probability max abs |
|---|---:|---|---:|
| red/green/blue/gray/color bars | 0 | same | 0 |
| saved Hik frame | 0.0078125 | same | 0.000200 |

On the saved Hik frame, the legacy expression input copied 150,528 bytes and measured H2D/execute/total `0.085/1.086/1.335 ms`; packed-BGR AIPP copied 37,632 bytes and measured `0.017/0.912/0.998 ms`. AIPP is included in execute. A 100-frame mixed pipeline run with only expression switched to AIPP also exited normally with zero camera timeout/loss; its frame mean/P95 was `51.90/70.07 ms`. The scene did not reliably contain a face, so standalone parity is the accuracy evidence.

Measured expression DVPP + D2D + NV12-AIPP parity:

| Input | Raw tensor max abs | Top-1 | Probability max abs |
|---|---:|---|---:|
| solid red | 0.01367 | same | 0.000110 |
| solid green | 0.03125 | same | 0.000026 |
| solid blue | 0.01172 | same | 0.000001 |
| gray 114 | 0 | same | 0 |
| color bars | 0.02246 | same | 0.000043 |
| saved Hik frame | 0.53906 | same | 0.007077 |

For the saved Hik sample, the NV12 candidate used 18,816 D2D bytes, `acl_d2d_ms=0.020`, `acl_execute_ms=0.983`, `acl_total_ms=1.049`; the same validation step measured DVPP H2D `0.045 ms` and VPC `0.659 ms`. The larger raw-logit delta reflects BGR-to-4:2:0 subsampling/interpolation, while the probability result stays below the proposed 0.02 threshold. All primary-color and color-bar top-1 results match, providing direct evidence against an R/B swap or narrow/full-range mismatch.

Measured DVPP geometry on the same `1928x1078` Hik frame:

- Source upload: 3,130,512 bytes including source stride; measured H2D about 2.34 ms.
- Pose: 640x358 paste into 640x640, borders `(0,140,0,142)`, Y padding 114; requested-versus-aligned top delta `-1`.
- Face: 640x358 top-left paste; bottom-right full-range black is Y=0, UV=128/128.
- Expression: original ROI `(482,215.6,1446,862.4)` aligned to `(482,214,1445,861)`, output 112x112/18,816 bytes.
- Last measured VPC operation was about 0.69 ms. The installed CANN 7 binding uses the six-argument crop-and-paste signature, which the adapter detects and logs as `cann_7_six_args`.

The first mixed 100-frame run exposed an unconditional full-frame BGR-to-NV12 conversion and measured 295.31 ms/frame. Source upload is now lazy: it occurs at most once per frame and only when an enabled NV12 branch actually runs. After the fix, the same pose/face-legacy plus expression-NV12 configuration measured 49.96 ms mean / 70.50 ms P95, pose 42.85 ms mean, reported FPS mean 12.55, and 100 delivered frames with zero overwrite, timeout or loss. This scene had no accepted face, so it validates lifecycle/fallback performance rather than expression accuracy.

## 7. Board conversion and validation commands

The conversion script audits the ONNX input automatically when Python `onnx` is installed. On this offline board it requires `SMART_IP_ONNX_INPUT_NAME` from an audited contract. Outputs never overwrite existing OM files.

```bash
cd /home/HwHiAiUser/Desktop/ciciec_ascend-main
source /usr/local/Ascend/ascend-toolkit/set_env.sh
SMART_IP_ONNX_INPUT_NAME=image \
  bash Scripts/ascend_310b/convert_onnx_to_om.sh emotion aipp_bgr
SMART_IP_ONNX_INPUT_NAME=image \
  bash Scripts/ascend_310b/convert_onnx_to_om.sh emotion aipp_nv12

SMART_IP_ONNX_INPUT_NAME=images \
  bash Scripts/ascend_310b/convert_onnx_to_om.sh pose_n audit_float
SMART_IP_ONNX_INPUT_NAME=images \
  bash Scripts/ascend_310b/convert_onnx_to_om.sh pose_n aipp_bgr
SMART_IP_ONNX_INPUT_NAME=images \
  bash Scripts/ascend_310b/convert_onnx_to_om.sh pose_n aipp_nv12
```

Parity and DVPP scripts:

```bash
cd code/ascend_310B/smart_ip_interaction
PYTHONPATH=src:$PYTHONPATH python3 scripts/benchmark/validate_aipp_parity.py \
  --task expression --baseline models/om/emotion_mobilenetv3_6cls.om \
  --candidate models/om/emotion_mobilenetv3_6cls_aipp_bgr_u8.om \
  --candidate-mode aipp_bgr_u8 --input artifacts/aipp/hik_frame.jpg \
  --output artifacts/aipp/expression_bgr_profiled.json

PYTHONPATH=src:$PYTHONPATH SMART_IP_ACL_PROFILE=1 \
  python3 scripts/benchmark/validate_aipp_parity.py \
  --task expression --baseline models/om/emotion_mobilenetv3_6cls.om \
  --candidate models/om/emotion_mobilenetv3_6cls_aipp_nv12.om \
  --candidate-mode aipp_nv12 --input artifacts/aipp/hik_frame.jpg \
  --output artifacts/aipp/expression_nv12_profiled.json

PYTHONPATH=src:$PYTHONPATH python3 scripts/benchmark/validate_dvpp_geometry.py \
  --runtime-model models/om/yolo26n-pose-640.om --image artifacts/aipp/hik_frame.jpg \
  --output artifacts/dvpp/hik_geometry.json

PYTHONPATH=src:$PYTHONPATH SMART_IP_ACL_PROFILE=1 \
  python3 scripts/benchmark/validate_aipp_parity.py \
  --task pose --baseline models/om/yolo26n-pose-640.om \
  --candidate models/om/yolo26n-pose-640_aipp_bgr_u8.om \
  --candidate-mode aipp_bgr_u8 --input artifacts/aipp/zidane.jpg \
  --output artifacts/aipp/pose_n640_bgr_zidane.json

PYTHONPATH=src:$PYTHONPATH SMART_IP_ACL_PROFILE=1 \
  python3 scripts/benchmark/validate_aipp_parity.py \
  --task pose --baseline models/om/yolo26n-pose-640.om \
  --candidate models/om/yolo26n-pose-640_aipp_nv12.om \
  --candidate-mode aipp_nv12 --input artifacts/aipp/zidane.jpg \
  --visualize-dir artifacts/aipp/pose_n640_nv12_visuals \
  --output artifacts/aipp/pose_n640_nv12_zidane.json
```

## 8. One-command starts for the current Hik camera

CPU baseline:

```bash
SMART_IP_USE_UDP=0 SMART_IP_VIDEO_BACKEND=hik_gige \
SMART_IP_HIK_CAMERA_IP=192.168.2.253 SMART_IP_PREPROCESS_BACKEND=cpu \
SMART_IP_POSE_MODEL=models/om/yolo26n-pose-640.om \
python3 run_pipeline.py
```

AIPP BGR uint8, the recommended current Hik path. Pose and expression use their validated packed-BGR models; face remains on the production float model:

```bash
SMART_IP_USE_UDP=0 SMART_IP_VIDEO_BACKEND=hik_gige \
SMART_IP_HIK_CAMERA_IP=192.168.2.253 \
SMART_IP_PREPROCESS_BACKEND=aipp SMART_IP_POSE_INPUT_MODE=aipp_bgr_u8 \
SMART_IP_EXPR_INPUT_MODE=aipp_bgr_u8 SMART_IP_FACE_INPUT_MODE=float_nchw \
SMART_IP_POSE_MODEL=models/om/yolo26n-pose-640_aipp_bgr_u8.om \
SMART_IP_EMOTION_MODEL=models/om/emotion_mobilenetv3_6cls_aipp_bgr_u8.om \
python3 run_pipeline.py
```

Experimental Hik BGR-to-NV12 + pose DVPP+AIPP path. This is numerically validated but is deliberately not recommended for current production because host BGR-to-NV12 conversion dominates latency:

```bash
SMART_IP_USE_UDP=0 SMART_IP_VIDEO_BACKEND=hik_gige \
SMART_IP_HIK_CAMERA_IP=192.168.2.253 \
SMART_IP_PREPROCESS_BACKEND=dvpp_aipp SMART_IP_DVPP_ENABLE=1 \
SMART_IP_POSE_INPUT_MODE=aipp_nv12 SMART_IP_FACE_INPUT_MODE=float_nchw \
SMART_IP_EXPR_INPUT_MODE=float_nchw SMART_IP_KEEP_BGR_FOR_DISPLAY=0 \
SMART_IP_POSE_MODEL=models/om/yolo26n-pose-640_aipp_nv12.om \
python3 run_pipeline.py
```

PYNQ NV12 direct is intentionally not presented as runnable in this hardware session. When the sender is actually wired to NV12, add `SMART_IP_USE_UDP=1 SMART_IP_RAW_NV12_ENABLE=1` to the verified DVPP command.

## 9. Incomplete or blocked items

- The old `yolo26s-pose.onnx` remains absent, but this no longer blocks pose acceleration: the default was intentionally changed to the contract-matched `yolo26n-pose-640.om`, whose ONNX, float audit build, BGR-AIPP and NV12-AIPP variants were audited and compared on the board. The yolo26s OM remains available only for rollback.
- Full three-model AIPP/DVPP production switching is now blocked by unvalidated face AIPP output, not by pose. Pose and expression have independent BGR and NV12 parity evidence.
- Face NV12 is experimental and remains off by default.
- The current real input is Hik GigE, not PYNQ. PYNQ sender wiring remains deferred.
- Native-NV12 end-to-end performance remains unmeasured. The current Hik source delivers BGR, and its measured host conversion cost makes DVPP+NV12 slower despite valid device-side geometry and parity.
- AIPP time is part of OM execute; reports compare old/new execute and never invent a separate AIPP duration.
- Six labelled expression-class samples and an action video were not available in this board session, so class-wide/action acceptance remains pending; no result is fabricated from the no-face camera scene.

## 10. Modified and added files

- Runtime/integration: `run_pipeline.py`, `src/inference/acl_om.py`, `src/inference/dvpp.py`, `src/inference/pose_infer.py`, `src/inference/face_detector.py`, `src/inference/expression_infer.py`, `src/communication/udp_video_source.py`.
- Restored live baseline support: `src/capture/hik_gige_source.py`, `src/benchmark/logger.py`, `src/communication/tcp_sender.py`, `src/inference/ar_overlay.py`, `src/streaming/*`.
- AIPP/ATC: `Scripts/ascend_310b/aipp/*.cfg`, `Scripts/ascend_310b/convert_onnx_to_om.sh`.
- Tests: original six test modules plus `test_aipp_contracts.py`, `test_dvpp_geometry.py`, `test_dvpp_lifecycle.py`, `test_udp_raw_nv12.py`, `test_device_input.py`.
- Board tools: `audit_om_contracts.py`, `probe_ascend_dvpp.py`, `validate_aipp_parity.py`, `validate_dvpp_geometry.py`, `benchmark_preprocess_backends.py`, `hik_camera_smoke.py`.
- Documentation: application `README.md`, this report, the acceleration TODO and the restored Hik optimization report.
