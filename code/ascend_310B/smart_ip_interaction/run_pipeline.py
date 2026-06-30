"""
主推理流水线
============
输入：摄像头（默认）、视频文件，或硬件 UDP 视频流（PYNQ→310B→PC）

流程（每帧）：
  ┌─ YOLO-Pose ────────────────────────────────────────────────────┐
  │   找面积最大的人 → 关键点 → ActionStateMachine → 动作标签       │
  └────────────────────────────────────────────────────────────────┘
  ┌─ 两段式级联人脸表情 ───────────────────────────────────────────┐
  │  Stage 1：YuNet 人脸检测 → 最佳人脸 bbox                       │
  │  Stage 2：MobileNetV3 表情分类 + 滑动窗口7帧投票平滑           │
  └────────────────────────────────────────────────────────────────┘
  推理结果 → TCP 推送到后端（连不上时自动重连，不阻塞推理）
  叠加标注 → cv2.imshow 显示
"""

import sys
import os
import time

import numpy as np

PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
SRC_ROOT = os.path.join(PROJECT_ROOT, "src")

if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)
if SRC_ROOT not in sys.path:
    sys.path.insert(0, SRC_ROOT)

import cv2

from behavior.action_rules import (
    is_wave_frame, is_cheer_frame, is_heart_frame, valid_keypoints
)
from behavior.action_state_machine import ActionStateMachine
from behavior.event_builder import build_action_event, build_expression_event
from inference.pose_infer import PoseInfer
from inference.expression_infer import ExpressionInfer
from inference.face_detector import FaceDetector
from interaction.action_mapper import map_event_to_command
from communication.tcp_sender import TCPSender
from inference.ar_overlay import AROverlay
from benchmark.logger import JsonlLogger, time_ms


def _env_bool(name, default):
    raw = os.environ.get(name)
    if raw is None:
        return default
    return raw.strip().lower() not in ("0", "false", "no", "off")


def _env_video_source(name, default):
    raw = os.environ.get(name)
    if raw is None:
        return default
    try:
        return int(raw)
    except ValueError:
        return raw


def _env_int(name, default, min_value=None):
    raw = os.environ.get(name)
    if raw is None:
        return default
    try:
        value = int(raw)
    except ValueError:
        print(f"[WARN] invalid {name}={raw!r}, fallback to {default}")
        return default
    if min_value is not None and value < min_value:
        print(f"[WARN] invalid {name}={raw!r}, fallback to {default}")
        return default
    return value


def _env_float(name, default, min_value=None):
    raw = os.environ.get(name)
    if raw is None:
        return default
    try:
        value = float(raw)
    except ValueError:
        print(f"[WARN] invalid {name}={raw!r}, fallback to {default}")
        return default
    if min_value is not None and value < min_value:
        print(f"[WARN] invalid {name}={raw!r}, fallback to {default}")
        return default
    return value


# ─────────────────────────── 配置 ───────────────────────────
# 310B 生产路径：默认强制使用 ACL + OM，避免静默回退到 CPU/PT。
POSE_REQUIRE_NPU   = _env_bool("SMART_IP_POSE_REQUIRE_NPU", True)
POSE_ALLOW_PT_FALLBACK = _env_bool("SMART_IP_POSE_ALLOW_PT_FALLBACK", False)
POSE_DEFAULT_MODEL_PATH = (
    os.path.join(PROJECT_ROOT, "models", "om", "yolo26s-pose.om")
    if POSE_REQUIRE_NPU or not POSE_ALLOW_PT_FALLBACK
    else os.path.join(PROJECT_ROOT, "yolo26n-pose.pt")
)
POSE_MODEL_PATH    = os.environ.get("SMART_IP_POSE_MODEL", POSE_DEFAULT_MODEL_PATH)
FACE_REQUIRE_NPU = _env_bool("SMART_IP_FACE_REQUIRE_NPU", True)
FACE_ALLOW_OPENCV_FALLBACK = _env_bool("SMART_IP_FACE_ALLOW_OPENCV_FALLBACK", False)
FACE_DEFAULT_MODEL_PATH = (
    os.path.join(PROJECT_ROOT, "models", "om", "face_detection_yunet_2023mar_640.om")
    if FACE_REQUIRE_NPU or not FACE_ALLOW_OPENCV_FALLBACK
    else os.path.join(PROJECT_ROOT, "models", "pretrained", "face_detection_yunet_2023mar.onnx")
)
FACE_MODEL_PATH    = os.environ.get("SMART_IP_FACE_MODEL", FACE_DEFAULT_MODEL_PATH)
EXPR_REQUIRE_NPU = _env_bool("SMART_IP_EXPR_REQUIRE_NPU", True)
EXPR_ALLOW_TORCH_FALLBACK = _env_bool("SMART_IP_EXPR_ALLOW_TORCH_FALLBACK", False)
EMOTION_DEFAULT_MODEL_PATH = (
    os.path.join(PROJECT_ROOT, "models", "om", "emotion_mobilenetv3_6cls.om")
    if EXPR_REQUIRE_NPU or not EXPR_ALLOW_TORCH_FALLBACK
    else os.path.join(PROJECT_ROOT, "models", "checkpoints", "emotion_mobilenetv3_6cls_best.pth")
)
EMOTION_MODEL_PATH = (
    os.environ.get("SMART_IP_EMOTION_MODEL")
    or os.environ.get("SMART_IP_EXPRESSION_MODEL")
    or EMOTION_DEFAULT_MODEL_PATH
)
NUM_EMOTION_CLASSES = 6

# ── 视频源选择 ────────────────────────────────────────────────
# USE_UDP_STREAM = False 时使用本地源（摄像头或视频文件）
# USE_UDP_STREAM = True  时使用硬件 UDP 视频流（PYNQ→310B→PC）
USE_UDP_STREAM = _env_bool("SMART_IP_USE_UDP", True)

# 本地源：0 = 摄像头；填路径 = 视频文件（USE_UDP_STREAM=False 时生效）
VIDEO_SOURCE = _env_video_source("SMART_IP_VIDEO_SOURCE", 0)

# UDP 流配置（USE_UDP_STREAM=True 时生效）
UDP_LISTEN_PORT = int(os.environ.get("SMART_IP_UDP_PORT", "9090"))
UDP_MODULE_PATH = os.environ.get("SMART_IP_UDP_MODULE_PATH", "/root/userdata/ciciec/communication/build")
# ─────────────────────────────────────────────────────────────

# 后端地址
BACKEND_HOST = os.environ.get("SMART_IP_BACKEND_HOST", "192.168.137.1")
BACKEND_PORT = int(os.environ.get("SMART_IP_BACKEND_PORT", "9000"))

# 表情事件最短推送间隔（秒）
EXPR_SEND_INTERVAL = _env_float("SMART_IP_EXPR_SEND_INTERVAL", 1.5, min_value=0.0)

# 每隔几帧做一次人脸检测+表情推理（表情变化慢，不需要每帧推理）
EXPR_SKIP = _env_int("SMART_IP_EXPR_SKIP", 3, min_value=1)

# 压测时可关闭绘制和 AR，减少 CPU 可视化开销对推理指标的干扰。
ENABLE_OVERLAY = _env_bool("SMART_IP_ENABLE_OVERLAY", True)
ENABLE_AR = _env_bool("SMART_IP_ENABLE_AR", True)
ACL_PROFILE = _env_bool("SMART_IP_ACL_PROFILE", False)

# AR 道具
AR_PROP_PATH = os.path.join(PROJECT_ROOT,"assets", "bear_ears.png")
# ─────────────────────────────────────────────────────────────


def can_use_gui() -> bool:
    """Return True if a GUI display is likely available."""
    if not (os.environ.get("DISPLAY") or os.environ.get("WAYLAND_DISPLAY")):
        return False

    try:
        probe_name = "__opencv_gui_probe__"
        cv2.namedWindow(probe_name, cv2.WINDOW_NORMAL)
        cv2.imshow(probe_name, np.zeros((1, 1, 3), dtype=np.uint8))
        cv2.waitKey(1)
        cv2.destroyWindow(probe_name)
        return True
    except cv2.error:
        return False


def find_main_person(results):
    """返回面积最大的那个人的关键点，没人则返回 None。"""
    if results[0].keypoints is None:
        return None
    kps = results[0].keypoints.xy
    boxes = results[0].boxes.xyxy
    if len(kps) == 0:
        return None

    best_idx, max_area = 0, 0
    for i, box in enumerate(boxes):
        x1, y1, x2, y2 = box
        area = float((x2 - x1) * (y2 - y1))
        if area > max_area:
            max_area = area
            best_idx = i

    return kps[best_idx]


def kp_to_list(kp):
    """关键点 tensor 转可序列化的列表 [[x,y], ...]。"""
    return [[round(float(p[0]), 1), round(float(p[1]), 1)] for p in kp]


def draw_overlay(frame, action_label, expr_label, expr_conf, fps, tcp_ok):
    _, w = frame.shape[:2]

    overlay = frame.copy()
    cv2.rectangle(overlay, (0, 0), (w, 100), (0, 0, 0), -1)
    cv2.addWeighted(overlay, 0.45, frame, 0.55, 0, frame)

    action_color = (0, 80, 255) if action_label != "NONE" else (120, 120, 120)
    cv2.putText(frame, f"ACTION: {action_label}", (20, 40),
                cv2.FONT_HERSHEY_SIMPLEX, 1.1, action_color, 2)

    expr_color = (0, 220, 80) if expr_label else (120, 120, 120)
    expr_str = f"EXPR: {expr_label} ({expr_conf:.2f})" if expr_label else "EXPR: --"
    cv2.putText(frame, expr_str, (20, 80),
                cv2.FONT_HERSHEY_SIMPLEX, 1.0, expr_color, 2)

    cv2.putText(frame, f"FPS:{fps:.1f}", (w - 130, 30),
                cv2.FONT_HERSHEY_SIMPLEX, 0.8, (200, 200, 200), 1)

    # TCP 连接状态指示
    dot_color = (0, 220, 0) if tcp_ok else (0, 0, 200)
    cv2.circle(frame, (w - 20, 60), 8, dot_color, -1)

    return frame


def acl_profile_fields(prefix, owner):
    session = getattr(owner, "_om_session", None)
    profile = getattr(session, "last_profile", None)
    if not profile:
        return {}
    return {f"{prefix}_{key}": value for key, value in profile.items()}


def open_video_source():
    """根据配置打开本地视频源或硬件 UDP 流，返回 cap 对象。"""
    if USE_UDP_STREAM:
        try:
            from communication.udp_video_source import UDPVideoSource
            print(f"[INFO] 使用硬件 UDP 视频流，监听端口 {UDP_LISTEN_PORT}")
            cap = UDPVideoSource(port=UDP_LISTEN_PORT, module_path=UDP_MODULE_PATH)
            if not cap.isOpened():
                print("[WARN] UDP 视频源启动失败，尝试回退到本地摄像头")
                cap = cv2.VideoCapture(0)
                if not cap.isOpened():
                    print("[ERROR] 本地摄像头也无法打开")
                    return None
                return cap
            return cap
        except ImportError as e:
            print(f"[ERROR] 无法导入 UDPVideoSource: {e}")
            print("[INFO] 确保已设置 PYTHONPATH 指向 communication/build 目录")
            print("[INFO] 回退到本地摄像头")
            cap = cv2.VideoCapture(0)
            if not cap.isOpened():
                print("[ERROR] 本地摄像头也无法打开")
                return None
            return cap
        except Exception as e:
            print(f"[ERROR] UDP 启动异常: {e}")
            print("[INFO] 回退到本地摄像头")
            cap = cv2.VideoCapture(0)
            if not cap.isOpened():
                print("[ERROR] 本地摄像头也无法打开")
                return None
            return cap
    else:
        print(f"[INFO] 使用本地视频源: {VIDEO_SOURCE}")
        cap = cv2.VideoCapture(VIDEO_SOURCE)
        if not cap.isOpened():
            print(f"[ERROR] 无法打开视频源: {VIDEO_SOURCE}")
            return None
        return cap


def read_frame_with_info(cap):
    """Read a frame and keep transport metadata when the source supports it."""
    if hasattr(cap, "read_with_info"):
        return cap.read_with_info()
    ret, frame = cap.read()
    return ret, frame, {}


def _event_trace(prefix, frame_idx, frame_info, model_event):
    now_ts = time.time()
    return {
        "event_id": f"{prefix}-{frame_idx}-{time.time_ns()}",
        "pipeline_ts": now_ts,
        "model_event_ts": model_event.get("timestamp", now_ts) if isinstance(model_event, dict) else now_ts,
        "frame_idx": frame_idx,
        "source_frame_id": frame_info.get("frame_id"),
        "source_timestamp_us": frame_info.get("timestamp_us"),
    }


def main():
    bench_logger = JsonlLogger.from_env("pipeline")
    bench_frame_stride = _env_int("SMART_IP_BENCH_FRAME_STRIDE", 1, min_value=0)
    bench_logger.log(
        "pipeline_start",
        use_udp_stream=USE_UDP_STREAM,
        video_source=str(VIDEO_SOURCE),
        udp_listen_port=UDP_LISTEN_PORT,
        udp_module_path=UDP_MODULE_PATH,
        backend_host=BACKEND_HOST,
        backend_port=BACKEND_PORT,
        expr_skip=EXPR_SKIP,
        expr_send_interval=EXPR_SEND_INTERVAL,
        enable_overlay=ENABLE_OVERLAY,
        enable_ar=ENABLE_AR,
        acl_profile=ACL_PROFILE,
        pose_model_path=POSE_MODEL_PATH,
        pose_require_npu=POSE_REQUIRE_NPU,
        pose_allow_pt_fallback=POSE_ALLOW_PT_FALLBACK,
        face_model_path=FACE_MODEL_PATH,
        face_require_npu=FACE_REQUIRE_NPU,
        face_allow_opencv_fallback=FACE_ALLOW_OPENCV_FALLBACK,
        expression_model_path=EMOTION_MODEL_PATH,
        expression_require_npu=EXPR_REQUIRE_NPU,
        expression_allow_torch_fallback=EXPR_ALLOW_TORCH_FALLBACK,
    )

    # ── 加载模型 ──
    print("[INFO] 加载姿态模型:", POSE_MODEL_PATH)
    pose_model = PoseInfer(
        model_path=POSE_MODEL_PATH,
        imgsz=640,
        conf=0.25,
        iou=0.45,
        use_npu=True,
        require_npu=POSE_REQUIRE_NPU,
        allow_pt_fallback=POSE_ALLOW_PT_FALLBACK,
    )
    pose_runtime = pose_model.runtime_info()
    print("[INFO] 姿态推理运行时:", pose_runtime)
    bench_logger.log("pose_runtime", **pose_runtime)

    print("[INFO] 加载人脸检测模型:", FACE_MODEL_PATH)
    face_detector = FaceDetector(
        model_path=FACE_MODEL_PATH,
        use_npu=True,
        require_npu=FACE_REQUIRE_NPU,
        allow_opencv_fallback=FACE_ALLOW_OPENCV_FALLBACK,
    )
    face_runtime = face_detector.runtime_info()
    print("[INFO] 人脸检测运行时:", face_runtime)
    bench_logger.log("face_runtime", **face_runtime)

    print("[INFO] 加载表情模型:", EMOTION_MODEL_PATH)
    expr_infer = ExpressionInfer(
        model_path=EMOTION_MODEL_PATH,
        num_classes=NUM_EMOTION_CLASSES,
        smooth_window=7,
        use_npu=True,
        require_npu=EXPR_REQUIRE_NPU,
        allow_torch_fallback=EXPR_ALLOW_TORCH_FALLBACK,
    )
    expression_runtime = expr_infer.runtime_info()
    print("[INFO] 表情推理运行时:", expression_runtime)
    bench_logger.log("expression_runtime", **expression_runtime)

    # ── AR 道具 ──
    ar = AROverlay(prop_path=AR_PROP_PATH) if ENABLE_AR else None

    # ── TCP 推送（后台自动重连）──
    print(f"[INFO] 启动 TCP 推送 → {BACKEND_HOST}:{BACKEND_PORT}")
    sender = TCPSender(host=BACKEND_HOST, port=BACKEND_PORT)

    # ── 打开视频源 ──
    cap = open_video_source()
    if cap is None:
        face_detector.close()
        expr_infer.close()
        pose_model.close()
        sender.close()
        bench_logger.log("pipeline_stop", frame_idx=0, reason="video_source_open_failed")
        bench_logger.close()
        return

    action_sm    = ActionStateMachine()
    last_expr_time = 0.0
    expr_label, expr_conf = None, 0.0
    face_bbox    = None
    frame_idx    = 0

    prev_time = time.time()
    gui_enabled = can_use_gui()
    if not gui_enabled:
        print("[WARN] 未检测到可用 GUI 环境，已禁用 cv2.imshow；推理将继续在无界面模式运行")
    stats_last_print = time.time()
    STATS_PRINT_INTERVAL = 10.0  # seconds
    print("[INFO] 开始运行，按 ESC 退出")

    while True:
        read_start = time.perf_counter()
        ret, frame, frame_info = read_frame_with_info(cap)
        read_ms = time_ms(read_start)
        if not ret:
            # 本地视频文件循环播放；UDP 流超时则重试
            if not USE_UDP_STREAM and isinstance(VIDEO_SOURCE, str):
                cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
                expr_infer.reset_smooth()
                continue
            if USE_UDP_STREAM:
                # UDP 超时：等待硬件重新发送，不退出
                continue
            break

        frame_idx += 1
        frame_start = time.perf_counter()

        # ── Stage 1a：YOLO 姿态推理 → 动作识别 ──
        pose_start = time.perf_counter()
        results = pose_model(frame, verbose=False)
        pose_ms = time_ms(pose_start)
        if ENABLE_OVERLAY:
            annotated = results[0].plot() if (
                results[0].boxes is not None and len(results[0].boxes) > 0
            ) else frame.copy()
        else:
            annotated = frame

        kp = find_main_person(results)
        action_label = "NONE"
        kp_list = None

        # ── AR 道具：每帧叠加到所有检测到的人 ──
        if ar is not None and results[0].keypoints is not None and len(results[0].keypoints.xy) > 0:
            ar.apply(annotated, results[0].keypoints.xy)

        if kp is not None and valid_keypoints(kp):
            kp_list = kp_to_list(kp)
            wave  = is_wave_frame(kp)
            heart = is_heart_frame(kp)
            cheer = is_cheer_frame(kp)
            action_label = action_sm.update(wave, heart, cheer)

            if action_label != "NONE":
                event = build_action_event(action_label)
                cmd = map_event_to_command(event)
                if cmd:
                    # 附带关键点供 AR 特效使用
                    cmd["keypoints"] = kp_list
                    trace = _event_trace("action", frame_idx, frame_info, event)
                    cmd.update(trace)
                    print(f"[ACTION] {cmd}")
                    bench_logger.log(
                        "action_event_sent",
                        event_id=trace["event_id"],
                        frame_idx=frame_idx,
                        source_frame_id=trace["source_frame_id"],
                        source_timestamp_us=trace["source_timestamp_us"],
                        action_label=action_label,
                        command_value=cmd.get("value"),
                        tcp_connected=sender.connected,
                    )
                    sender.send(cmd)

        # ── Stage 1b + 2：每 EXPR_SKIP 帧做人脸检测 + 表情推理 ──
        expr_ms = None
        if frame_idx % EXPR_SKIP == 0:
            expr_start = time.perf_counter()
            face_roi, face_bbox = face_detector.detect_best(frame)
            if face_roi is not None:
                result = expr_infer.predict_smooth(face_roi)
                expr_label = result["expression"]
                expr_conf  = result["confidence"]
            expr_ms = time_ms(expr_start)

        if face_bbox is not None and expr_label is not None:
            if ENABLE_OVERLAY:
                x1, y1, x2, y2 = face_bbox
                cv2.rectangle(annotated, (x1, y1), (x2, y2), (0, 220, 80), 2)
                cv2.putText(annotated, f"{expr_label}", (x1, max(y1 - 8, 12)),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 220, 80), 2)

            now = time.time()
            if now - last_expr_time > EXPR_SEND_INTERVAL:
                event = build_expression_event(expr_label, expr_conf)
                cmd = map_event_to_command(event)
                if cmd:
                    # 附带人脸 bbox 供 AR 特效定位
                    cmd["face_bbox"] = list(face_bbox)
                    if kp_list:
                        cmd["keypoints"] = kp_list
                    trace = _event_trace("expression", frame_idx, frame_info, event)
                    cmd.update(trace)
                    print(f"[EXPR ] {cmd}")
                    bench_logger.log(
                        "expression_event_sent",
                        event_id=trace["event_id"],
                        frame_idx=frame_idx,
                        source_frame_id=trace["source_frame_id"],
                        source_timestamp_us=trace["source_timestamp_us"],
                        expression_label=expr_label,
                        expression_confidence=expr_conf,
                        command_value=cmd.get("value"),
                        tcp_connected=sender.connected,
                    )
                    sender.send(cmd)
                    last_expr_time = now

        # ── FPS & 显示 ──
        cur_time = time.time()
        fps = 1.0 / max(cur_time - prev_time, 1e-6)
        prev_time = cur_time
        frame_total_ms = time_ms(frame_start)

        if bench_frame_stride > 0 and frame_idx % bench_frame_stride == 0:
            profile_fields = {}
            if ACL_PROFILE:
                profile_fields.update(acl_profile_fields("pose", pose_model))
                profile_fields.update(acl_profile_fields("face", face_detector))
                profile_fields.update(acl_profile_fields("expression", expr_infer))
            bench_logger.log(
                "frame",
                frame_idx=frame_idx,
                frame_info=frame_info,
                read_ms=read_ms,
                pose_ms=pose_ms,
                expression_ms=expr_ms,
                frame_total_ms=frame_total_ms,
                fps=fps,
                action_label=action_label,
                expression_label=expr_label,
                expression_confidence=expr_conf,
                tcp_connected=sender.connected,
                pose_backend=pose_model.backend,
                face_backend=face_detector.backend,
                expression_backend=expr_infer.backend,
                **profile_fields,
            )

        if ENABLE_OVERLAY:
            annotated = draw_overlay(
                annotated, action_label, expr_label, expr_conf, fps, sender.connected
            )

        # 周期性打印底层 pybind 接收统计（仅当使用 UDPVideoSource 并且底层 interface 可用）
        try:
            now = time.time()
            if now - stats_last_print > STATS_PRINT_INTERVAL:
                stats_last_print = now
                if USE_UDP_STREAM and hasattr(cap, "get_detailed_stats"):
                    stats = cap.get_detailed_stats()
                    if stats:
                        bench_logger.log("udp_stats", **stats)
                        loss = stats.get("packet_loss_rate_per_mille", 0) / 10.0
                        print(
                            "[STATS] recv_pkt={recv_pkt} valid_pkt={valid_pkt} drop_pkt={drop_pkt} "
                            "drop_frag={drop_frag} drop_frame={drop_frame} loss={loss:.1f}% "
                            "latency_asm(avg/min/max)={asm_avg}/{asm_min}/{asm_max}us "
                            "latency_e2e(avg/min/max)={e2e_avg}/{e2e_min}/{e2e_max}us".format(
                                recv_pkt=stats.get("recv_pkt_cnt", 0),
                                valid_pkt=stats.get("pkt_cnt", 0),
                                drop_pkt=stats.get("dropped_pkt_cnt", 0),
                                drop_frag=stats.get("dropped_frag_cnt", 0),
                                drop_frame=stats.get("dropped_frame_cnt", 0),
                                loss=loss,
                                asm_avg=stats.get("assembly_latency_avg_us", 0),
                                asm_min=stats.get("assembly_latency_min_us", 0),
                                asm_max=stats.get("assembly_latency_max_us", 0),
                                e2e_avg=stats.get("sender_latency_avg_us", 0),
                                e2e_min=stats.get("sender_latency_min_us", 0),
                                e2e_max=stats.get("sender_latency_max_us", 0),
                            )
                        )
        except Exception:
            pass

        if gui_enabled:
            try:
                cv2.imshow("IP Interaction Pipeline", annotated)
                if cv2.waitKey(1) & 0xFF == 27:
                    break
            except cv2.error as e:
                print(f"[WARN] cv2.imshow 不可用，切换为无界面模式: {e}")
                gui_enabled = False

    cap.release()
    face_detector.close()
    expr_infer.close()
    pose_model.close()
    if gui_enabled:
        try:
            cv2.destroyAllWindows()
        except cv2.error:
            pass
    sender.close()
    bench_logger.log("pipeline_stop", frame_idx=frame_idx)
    bench_logger.close()
    print("[INFO] 退出")


if __name__ == "__main__":
    main()
