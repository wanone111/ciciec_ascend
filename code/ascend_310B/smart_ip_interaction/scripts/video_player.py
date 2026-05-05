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
import socket
import threading
import json as _json

# ─────────────────────────── 路径 ────────────────────────────
PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC_ROOT = os.path.join(PROJECT_ROOT, "src")
if SRC_ROOT not in sys.path:
    sys.path.insert(0, SRC_ROOT)

from benchmark.logger import JsonlLogger

CONFIG_PATH  = os.path.join(PROJECT_ROOT, "configs", "video_config.json")

WS_URL       = "ws://127.0.0.1:8000/ws"
WS_RETRY_SEC = 3.0
PC_VIDEO_CTRL_PORT = 5567  # 从 communication/include/udp_common.h 对应

WINDOW_NAME  = "IP Interaction Display"
# ─────────────────────────────────────────────────────────────

bench_logger = JsonlLogger.from_env("player")


def _env_bool(name, default):
    raw = os.environ.get(name)
    if raw is None:
        return default
    return raw.strip().lower() not in ("0", "false", "no", "off")


PLAYER_ALLOW_INTERRUPT = _env_bool("SMART_IP_PLAYER_ALLOW_INTERRUPT", False)
PLAYER_FLUSH_ON_FINISH = _env_bool("SMART_IP_PLAYER_FLUSH_ON_FINISH", True)
MAX_COMMANDS_PER_FRAME = 50


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
                                received_ts = time.time()
                                _cmd_queue.put({
                                    "key": key,
                                    "raw": msg,
                                    "transport": "websocket",
                                    "received_ts": received_ts,
                                })
                                bench_logger.log(
                                    "player_command_received",
                                    event_id=msg.get("event_id"),
                                    key=key,
                                    transport="websocket",
                                    pipeline_ts=msg.get("pipeline_ts"),
                                    backend_broadcast_ts=msg.get("backend_broadcast_ts"),
                                    player_receive_ts=received_ts,
                                )
                                print(f"[WS] 收到指令: {key}  原始: {msg}")
                        except json.JSONDecodeError:
                            pass
            except Exception as e:
                print(f"[WS] 连接断开 ({e})，{retry_sec:.0f}s 后重试...")
                await asyncio.sleep(retry_sec)

    asyncio.run(_listen())


def _udp_ctrl_thread(listen_ip: str = "0.0.0.0", listen_port: int = PC_VIDEO_CTRL_PORT):
    """后台线程：监听 UDP 控制指令（JSON），收到后放入队列。
    作为 WebSocket 的回退通道，兼容通信模块中定义的控制端口。
    """
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((listen_ip, listen_port))
        s.settimeout(1.0)
        print(f"[UDPCTRL] listening on {listen_ip}:{listen_port}")
        while True:
            try:
                data, addr = s.recvfrom(4096)
            except socket.timeout:
                continue
            except Exception as e:
                print(f"[UDPCTRL] socket error: {e}")
                break

            try:
                msg = _json.loads(data.decode("utf-8"))
            except Exception:
                # 非 JSON 文本忽略
                continue

            key = msg.get("dialogue_id") or msg.get("scene")
            if key:
                received_ts = time.time()
                _cmd_queue.put({
                    "key": key,
                    "raw": msg,
                    "transport": "udp",
                    "received_ts": received_ts,
                })
                bench_logger.log(
                    "player_command_received",
                    event_id=msg.get("event_id"),
                    key=key,
                    transport="udp",
                    pipeline_ts=msg.get("pipeline_ts"),
                    backend_broadcast_ts=msg.get("backend_broadcast_ts"),
                    player_receive_ts=received_ts,
                )
                print(f"[UDPCTRL] 收到指令 {key} from {addr} 原始: {msg}")
    finally:
        s.close()


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
    bench_logger.log(
        "player_start",
        ws_url=WS_URL,
        udp_ctrl_port=PC_VIDEO_CTRL_PORT,
        idle_path=idle_path,
        allow_interrupt=PLAYER_ALLOW_INTERRUPT,
        flush_on_finish=PLAYER_FLUSH_ON_FINISH,
    )

    # 启动 WS 监听线程
    t = threading.Thread(target=_ws_thread, args=(WS_URL, WS_RETRY_SEC), daemon=True)
    t.start()

    # 启动 UDP 控制监听（作为 WS 回退/兼容通信模块）
    t2 = threading.Thread(target=_udp_ctrl_thread, args=("0.0.0.0", PC_VIDEO_CTRL_PORT), daemon=True)
    t2.start()

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

    def _log_command_suppressed(item, reason, key=None):
        raw = item.get("raw", {})
        bench_logger.log(
            "player_command_suppressed",
            event_id=raw.get("event_id"),
            key=key or item.get("key"),
            reason=reason,
            current_video=current_path,
            transport=item.get("transport"),
            pipeline_ts=raw.get("pipeline_ts"),
            backend_broadcast_ts=raw.get("backend_broadcast_ts"),
            player_receive_ts=item.get("received_ts"),
        )

    def flush_pending_commands(reason):
        flushed = 0
        while True:
            try:
                item = _cmd_queue.get_nowait()
            except queue.Empty:
                break
            flushed += 1
            _log_command_suppressed(item, reason)
        if flushed:
            bench_logger.log("player_command_queue_flushed", reason=reason, count=flushed)
        return flushed

    def handle_command(item):
        nonlocal cap, frame_delay, is_idle, current_path

        key = item["key"]
        raw = item.get("raw", {})

        if not is_idle and not PLAYER_ALLOW_INTERRUPT:
            _log_command_suppressed(item, "busy_playing_interaction", key)
            print(f"[BUSY] 正在播放互动视频，忽略指令: {key}")
            return False

        video_path = cmd_map.get(key)
        if video_path and os.path.exists(video_path):
            print(f"[SWITCH] {key} → {os.path.basename(video_path)}")
            if cap:
                cap.release()
            cap, frame_delay = open_cap(video_path)
            if cap is None:
                is_idle = True
                current_path = idle_path if idle_exists else None
                if idle_exists:
                    cap, frame_delay = open_cap(idle_path)
                return False

            current_path = video_path
            is_idle = False
            switch_ts = time.time()
            bench_logger.log(
                "video_switch",
                event_id=raw.get("event_id"),
                key=key,
                video_path=video_path,
                transport=item.get("transport"),
                pipeline_ts=raw.get("pipeline_ts"),
                model_event_ts=raw.get("model_event_ts"),
                backend_receive_ts=raw.get("backend_receive_ts"),
                backend_broadcast_ts=raw.get("backend_broadcast_ts"),
                player_receive_ts=item.get("received_ts"),
                player_switch_ts=switch_ts,
                frame_idx=raw.get("frame_idx"),
                source_frame_id=raw.get("source_frame_id"),
                source_timestamp_us=raw.get("source_timestamp_us"),
                source_type=raw.get("source_type"),
                source_value=raw.get("source_value"),
            )
            return True

        if video_path:
            print(f"[WARN] 视频文件不存在: {video_path}，指令 {key} 被忽略")
        bench_logger.log(
            "video_missing",
            event_id=raw.get("event_id"),
            key=key,
            video_path=video_path,
        )
        return False

    print("[INFO] 视频展示程序已启动，按 ESC 退出")

    while True:
        # ── 从队列取指令（非阻塞）──
        handled = 0
        while handled < MAX_COMMANDS_PER_FRAME:
            try:
                item = _cmd_queue.get_nowait()
            except queue.Empty:
                break
            handled += 1
            handle_command(item)

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
                    if PLAYER_FLUSH_ON_FINISH:
                        flushed = flush_pending_commands("interaction_finished")
                        if flushed:
                            print(f"[INFO] 已清理播放期间积压指令: {flushed} 条")
                    is_idle = True
                    current_path = idle_path if idle_exists else None
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
    bench_logger.log("player_stop")
    bench_logger.close()
    print("[INFO] 退出")


if __name__ == "__main__":
    main()
