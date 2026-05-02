import sys
import os
import argparse

sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), "../../")))

from ciciec_ascend.smart_ip_interaction.src.behavior.event_builder import build_action_event
import cv2
from ultralytics import YOLO
from ciciec_ascend.smart_ip_interaction.src.behavior.action_rules import is_wave_frame, is_cheer_frame, is_heart_frame
from ciciec_ascend.smart_ip_interaction.src.behavior.event_builder import build_action_event
from ciciec_ascend.smart_ip_interaction.src.interaction.action_mapper import map_event_to_command

parser = argparse.ArgumentParser()
parser.add_argument("--video", default=r"G:\yolo_8\smart_ip_interaction\test.mp4")
args = parser.parse_args()
VIDEO_PATH = args.video

model = YOLO("yolo26n-pose.pt")
cap = cv2.VideoCapture(VIDEO_PATH)

fps = cap.get(cv2.CAP_PROP_FPS)
frame_interval_ms = (1000 / fps) if fps > 0 else 33  # 每帧应占用的毫秒数

if not cap.isOpened():
    print(f"❌ 无法打开视频: {VIDEO_PATH}")
    raise SystemExit

import time

while True:
    t_start = time.time()

    ret, frame = cap.read()
    if not ret:
        break

    results = model(frame, verbose=False)
    annotated = results[0].plot()

    action_text = "NONE"
    debug_text = ""

    if results[0].keypoints is not None and len(results[0].keypoints.xy) > 0:
        keypoints = results[0].keypoints.xy
        boxes = results[0].boxes.xyxy

        if keypoints is not None and len(keypoints) > 0:
            max_area = 0
            best_idx = 0

            for i in range(len(boxes)):
                x1, y1, x2, y2 = boxes[i]
                area = (x2 - x1) * (y2 - y1)

                if area > max_area:
                    max_area = area
                    best_idx = i

            kp = keypoints[best_idx]

        wave_now = is_wave_frame(kp)
        heart_now = is_heart_frame(kp)
        cheer_now = is_cheer_frame(kp)

        # 单帧直接判断，按优先级输出
        if cheer_now:
            action_text = "CHEER"
        elif heart_now:
            action_text = "HEART"
        elif wave_now:
            action_text = "WAVE"

        debug_text = f"wave={int(wave_now)} heart={int(heart_now)} cheer={int(cheer_now)}"

    cv2.putText(
        annotated,
        f"ACTION: {action_text}",
        (30, 40),
        cv2.FONT_HERSHEY_SIMPLEX,
        1.0,
        (0, 0, 255),
        2
    )

    cv2.putText(
        annotated,
        debug_text,
        (30, 80),
        cv2.FONT_HERSHEY_SIMPLEX,
        0.8,
        (255, 0, 0),
        2
    )

    if action_text != "NONE":
        event = build_action_event(action_text)
        command = map_event_to_command(event)
        if command:
            print("Send command:", command)

    cv2.imshow("Action Rules Test", annotated)

    elapsed_ms = (time.time() - t_start) * 1000
    wait_ms = max(1, int(frame_interval_ms - elapsed_ms))
    key = cv2.waitKey(wait_ms) & 0xFF
    if key == 27:
        break

cap.release()
cv2.destroyAllWindows()
