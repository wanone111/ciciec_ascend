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

PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
SRC_ROOT = os.path.join(PROJECT_ROOT, "src")

if PROJECT_ROOT not in sys.path:
    sys.path.insert(0, PROJECT_ROOT)
if SRC_ROOT not in sys.path:
    sys.path.insert(0, SRC_ROOT)

import cv2
from ultralytics import YOLO

from behavior.action_rules import (
    is_wave_frame, is_cheer_frame, is_heart_frame, valid_keypoints
)
from behavior.action_state_machine import ActionStateMachine
from behavior.event_builder import build_action_event, build_expression_event
from inference.expression_infer import ExpressionInfer
from inference.face_detector import FaceDetector
from interaction.action_mapper import map_event_to_command
from communication.tcp_sender import TCPSender
from inference.ar_overlay import AROverlay

# ─────────────────────────── 配置 ───────────────────────────
# ✅ 统一指向 models/om 目录，使用 PROJECT_ROOT 构建绝对路径，彻底解决路径解析问题
POSE_MODEL_PATH    = os.path.join(PROJECT_ROOT, "models", "om", "yolo26s-pose.om")
FACE_MODEL_PATH    = os.path.join(PROJECT_ROOT, "models", "pretrained", "face_detection_yunet_2023mar.onnx")
EMOTION_MODEL_PATH = os.path.join(PROJECT_ROOT, "models", "checkpoints", "emotion_mobilenetv3_6cls_best.pth")
NUM_EMOTION_CLASSES = 6

# ── 视频源选择 ────────────────────────────────────────────────
# USE_UDP_STREAM = False 时使用本地源（摄像头或视频文件）
# USE_UDP_STREAM = True  时使用硬件 UDP 视频流（PYNQ→310B→PC）
USE_UDP_STREAM = True

# 本地源：0 = 摄像头；填路径 = 视频文件（USE_UDP_STREAM=False 时生效）
VIDEO_SOURCE = 0

# UDP 流配置（USE_UDP_STREAM=True 时生效）
UDP_LISTEN_PORT = 9090          
UDP_MODULE_PATH = "/root/userdata/ciciec/communication/build" 
# ─────────────────────────────────────────────────────────────

# 后端地址
BACKEND_HOST = "192.168.137.1"
BACKEND_PORT = 9000

# 表情事件最短推送间隔（秒）
EXPR_SEND_INTERVAL = 1.5

# 每隔几帧做一次人脸检测+表情推理（表情变化慢，不需要每帧推理）
EXPR_SKIP = 3

# AR 道具
AR_PROP_PATH = os.path.join(PROJECT_ROOT,"assets", "bear_ears.png")
# ─────────────────────────────────────────────────────────────


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


def main():
    # ── 加载模型 ──
    print("[INFO] 加载姿态模型:", POSE_MODEL_PATH)
    pose_model = YOLO(POSE_MODEL_PATH)

    print("[INFO] 加载人脸检测模型:", FACE_MODEL_PATH)
    face_detector = FaceDetector(model_path=FACE_MODEL_PATH)

    print("[INFO] 加载表情模型:", EMOTION_MODEL_PATH)
    expr_infer = ExpressionInfer(
        model_path=EMOTION_MODEL_PATH,
        num_classes=NUM_EMOTION_CLASSES,
        smooth_window=7
    )

    # ── AR 道具 ──
    ar = AROverlay(prop_path=AR_PROP_PATH)

    # ── TCP 推送（后台自动重连）──
    print(f"[INFO] 启动 TCP 推送 → {BACKEND_HOST}:{BACKEND_PORT}")
    sender = TCPSender(host=BACKEND_HOST, port=BACKEND_PORT)

    # ── 打开视频源 ──
    cap = open_video_source()
    if cap is None:
        return

    action_sm    = ActionStateMachine()
    last_expr_time = 0.0
    expr_label, expr_conf = None, 0.0
    face_bbox    = None
    frame_idx    = 0

    prev_time = time.time()
    print("[INFO] 开始运行，按 ESC 退出")

    while True:
        ret, frame = cap.read()
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

        # ── Stage 1a：YOLO 姿态推理 → 动作识别 ──
        results = pose_model(frame, verbose=False)
        annotated = results[0].plot() if (
            results[0].boxes is not None and len(results[0].boxes) > 0
        ) else frame.copy()

        kp = find_main_person(results)
        action_label = "NONE"
        kp_list = None

        # ── AR 道具：每帧叠加到所有检测到的人 ──
        if results[0].keypoints is not None and len(results[0].keypoints.xy) > 0:
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
                    print(f"[ACTION] {cmd}")
                    sender.send(cmd)

        # ── Stage 1b + 2：每 EXPR_SKIP 帧做人脸检测 + 表情推理 ──
        if frame_idx % EXPR_SKIP == 0:
            face_roi, face_bbox = face_detector.detect_best(frame)
            if face_roi is not None:
                result = expr_infer.predict_smooth(face_roi)
                expr_label = result["expression"]
                expr_conf  = result["confidence"]

        if face_bbox is not None and expr_label is not None:
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
                    print(f"[EXPR ] {cmd}")
                    sender.send(cmd)
                    last_expr_time = now

        # ── FPS & 显示 ──
        cur_time = time.time()
        fps = 1.0 / max(cur_time - prev_time, 1e-6)
        prev_time = cur_time

        annotated = draw_overlay(
            annotated, action_label, expr_label, expr_conf, fps, sender.connected
        )
        cv2.imshow("IP Interaction Pipeline", annotated)

        if cv2.waitKey(1) & 0xFF == 27:
            break

    cap.release()
    cv2.destroyAllWindows()
    sender.close()
    print("[INFO] 退出")


if __name__ == "__main__":
    main()
