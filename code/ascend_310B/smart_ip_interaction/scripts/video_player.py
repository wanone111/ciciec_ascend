"""
视频切换展示程序
================
连接后端 WebSocket，根据收到的指令切换播放对应视频。

运行：
  python scripts/video_player.py

逻辑：
  - 默认循环播放 idle 视频（待机画面）
  - 收到指令 → 立即切换播放对应视频 → 播完自动回 idle
  - 视频映射在 configs/video_config.json 中配置，改配置不改代码
  - idle 视频不存在时，显示黑色待机画面

按 ESC 退出
"""

import asyncio
import json
import os
import queue
import sys
import threading
import time

import cv2
import numpy as np
import websockets

# ─────────────────────────── 路径 ────────────────────────────
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CONFIG_PATH  = os.path.join(PROJECT_ROOT, "configs", "video_config.json")

WS_URL       = "ws://127.0.0.1:8000/ws"
WS_RETRY_SEC = 3.0

WINDOW_NAME  = "IP Interaction Display"
# ─────────────────────────────────────────────────────────────


def load_config():
    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def abs_video(cfg, rel_path):
    """把配置里的相对路径转为绝对路径。"""
    return os.path.join(PROJECT_ROOT, rel_path) if rel_path else None


# ──────────────────── WebSocket 监听线程 ─────────────────────
_cmd_queue: queue.Queue = queue.Queue()


def _ws_thread(url: str, retry_sec: float):
    """后台线程：持续监听 WebSocket，收到消息放入队列。"""

    async def _listen():
        while True:
            try:
                async with websockets.connect(url) as ws:
                    print(f"[WS] 已连接 {url}")
                    async for raw in ws:
                        try:
                            msg = json.loads(raw)
                            # 提取触发键：优先 dialogue_id，其次 scene
                            key = msg.get("dialogue_id") or msg.get("scene")
                            if key:
                                _cmd_queue.put(key)
                                print(f"[WS] 收到指令: {key}  原始: {msg}")
                        except json.JSONDecodeError:
                            pass
            except Exception as e:
                print(f"[WS] 连接断开 ({e})，{retry_sec:.0f}s 后重试...")
                await asyncio.sleep(retry_sec)

    asyncio.run(_listen())


# ──────────────────────── 视频播放主循环 ─────────────────────
def _blank_frame(w=1280, h=720):
    """idle 视频缺失时的黑色占位帧。"""
    frame = np.zeros((h, w, 3), dtype=np.uint8)
    cv2.putText(frame, "Waiting for action...",
                (w // 2 - 200, h // 2),
                cv2.FONT_HERSHEY_SIMPLEX, 1.2, (80, 80, 80), 2)
    return frame


def main():
    cfg = load_config()
    idle_path = abs_video(cfg, cfg.get("idle", ""))
    cmd_map   = {k: abs_video(cfg, v) for k, v in cfg.get("commands", {}).items()}

    # 启动 WS 监听线程
    t = threading.Thread(target=_ws_thread, args=(WS_URL, WS_RETRY_SEC), daemon=True)
    t.start()

    cv2.namedWindow(WINDOW_NAME, cv2.WINDOW_NORMAL)

    idle_exists = idle_path and os.path.exists(idle_path)
    if not idle_exists:
        print(f"[WARN] idle 视频不存在: {idle_path}，显示黑色待机画面")

    current_path = idle_path if idle_exists else None
    is_idle      = True
    cap          = None
    frame_delay  = 33  # ms，默认 30fps

    def open_cap(path):
        c = cv2.VideoCapture(path)
        if not c.isOpened():
            print(f"[ERROR] 无法打开视频: {path}")
            return None, 33
        fps = c.get(cv2.CAP_PROP_FPS)
        delay = max(1, int(1000 / fps)) if fps > 0 else 33
        return c, delay

    if current_path:
        cap, frame_delay = open_cap(current_path)

    print("[INFO] 视频展示程序已启动，按 ESC 退出")

    while True:
        # ── 从队列取指令（非阻塞）──
        try:
            key = _cmd_queue.get_nowait()
            video_path = cmd_map.get(key)
            if video_path and os.path.exists(video_path):
                print(f"[SWITCH] {key} → {os.path.basename(video_path)}")
                if cap:
                    cap.release()
                cap, frame_delay = open_cap(video_path)
                is_idle  = False
            else:
                if video_path:
                    print(f"[WARN] 视频文件不存在: {video_path}，指令 {key} 被忽略")
        except queue.Empty:
            pass

        # ── 读帧 ──
        frame = None
        if cap and cap.isOpened():
            ret, frame = cap.read()
            if not ret:
                # 视频播完
                frame = None
                cap.release()
                cap = None
                if not is_idle:
                    # 动作视频播完 → 回 idle
                    print("[INFO] 动作视频播完，回到待机")
                    is_idle = True
                    if idle_exists:
                        cap, frame_delay = open_cap(idle_path)
                else:
                    # idle 视频循环
                    if idle_exists:
                        cap, frame_delay = open_cap(idle_path)

        # frame 为 None（读帧失败/视频切换瞬间）→ 显示黑帧兜底
        if frame is None or frame.size == 0:
            frame = _blank_frame()

        # ── 按视频实际帧率显示 ──
        cv2.imshow(WINDOW_NAME, frame)
        if cv2.waitKey(frame_delay) & 0xFF == 27:
            break

    if cap:
        cap.release()
    cv2.destroyAllWindows()
    print("[INFO] 退出")


if __name__ == "__main__":
    main()
