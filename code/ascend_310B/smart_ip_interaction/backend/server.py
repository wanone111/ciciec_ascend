"""
后端业务分发服务
================
职责：
  1. 监听 TCP 9000 端口 ← 接收边缘端 AI 推理结果
  2. 维护剧情状态机（记录 CHEER 累计次数，管理三种联动形式）
  3. 通过 WebSocket 广播控制指令 → Unity / 前端大屏

三种联动形式：
  A. 虚拟角色对话  — 动作(WAVE/HEART) 或表情触发
  B. 剧情分支切换  — CHEER 累计达阈值推进剧情线
  C. AR 合影特效   — HEART + 关键点/人脸坐标实时推送

运行：
  python backend/server.py

WebSocket 地址：ws://0.0.0.0:8000/ws
"""

import asyncio
import json
import os
import threading
import socket
import sys
import time
import uvicorn
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC_ROOT = os.path.join(PROJECT_ROOT, "src")
FRONTEND_ROOT = os.path.join(PROJECT_ROOT, "frontend")
MEDIA_ROOT = os.path.join(PROJECT_ROOT, "datasets", "interaction_videos")
VIDEO_CONFIG_PATH = os.path.join(PROJECT_ROOT, "configs", "video_config.json")
if SRC_ROOT not in sys.path:
    sys.path.insert(0, SRC_ROOT)

from benchmark.logger import JsonlLogger

# ─────────────────────────── 配置 ───────────────────────────
TCP_HOST = "0.0.0.0"
TCP_PORT = 9000          # 接收边缘端推送

WS_HOST  = "0.0.0.0"
WS_PORT  = 8000          # Unity / 前端连接

# 剧情分支：CHEER 每累计多少次推进一次
CHEER_SCENE_THRESHOLD = 3
SCENE_COUNT = 3          # 总剧情线数量
# ─────────────────────────────────────────────────────────────

app = FastAPI()
bench_logger = JsonlLogger.from_env("backend")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)
app.mount("/frontend", StaticFiles(directory=FRONTEND_ROOT), name="frontend")
app.mount("/media", StaticFiles(directory=MEDIA_ROOT), name="media")


@app.get("/", include_in_schema=False)
def frontend_index():
    return FileResponse(os.path.join(FRONTEND_ROOT, "index.html"))


@app.get("/frontend-config")
def frontend_config():
    with open(VIDEO_CONFIG_PATH, "r", encoding="utf-8") as config_file:
        video_config = json.load(config_file)

    def media_url(path):
        prefix = "datasets/interaction_videos/"
        relative = path[len(prefix):] if path.startswith(prefix) else path
        return "/media/" + relative.lstrip("/")

    camera_stream_url = os.environ.get(
        "SMART_IP_CAMERA_STREAM_URL", "http://192.168.2.2:8081/stream.mjpg"
    )
    camera_base_url = camera_stream_url.rsplit("/", 1)[0]
    return {
        "camera_stream_url": camera_stream_url,
        "camera_snapshot_url": camera_base_url + "/snapshot.jpg",
        "camera_health_url": camera_base_url + "/health",
        "idle": media_url(video_config["idle"]),
        "commands": {
            key: media_url(path)
            for key, path in video_config.get("commands", {}).items()
        },
    }

# ── WebSocket 连接池 ──────────────────────────────────────────
class ConnectionManager:
    def __init__(self):
        self._clients: list[WebSocket] = []
        self._lock = asyncio.Lock()

    async def connect(self, ws: WebSocket):
        await ws.accept()
        async with self._lock:
            self._clients.append(ws)
        print(f"[WS] 客户端接入，当前连接数: {len(self._clients)}")
        bench_logger.log("ws_client_connect", client_count=len(self._clients))

    async def disconnect(self, ws: WebSocket):
        async with self._lock:
            if ws in self._clients:
                self._clients.remove(ws)
        print(f"[WS] 客户端断开，当前连接数: {len(self._clients)}")
        bench_logger.log("ws_client_disconnect", client_count=len(self._clients))

    async def broadcast(self, data: dict):
        msg = json.dumps(data, ensure_ascii=False)
        async with self._lock:
            dead = []
            for ws in self._clients:
                try:
                    await ws.send_text(msg)
                    bench_logger.log(
                        "ws_sent",
                        event_id=data.get("event_id"),
                        scene=data.get("scene"),
                        dialogue_id=data.get("dialogue_id"),
                        source_type=data.get("source_type"),
                        source_value=data.get("source_value"),
                        client_count=len(self._clients),
                    )
                except Exception:
                    dead.append(ws)
            for ws in dead:
                self._clients.remove(ws)

    @property
    def client_count(self):
        return len(self._clients)

manager = ConnectionManager()

# ── 剧情状态机 ───────────────────────────────────────────────
class SceneStateMachine:
    def __init__(self):
        self.current_scene = 0          # 当前剧情线 (0-based)
        self.cheer_count   = 0          # CHEER 累计次数
        self.last_action_time = {}      # 各动作最后触发时间，防止重复
        self.cooldown = 2.0             # 同一动作触发冷却（秒）

    def on_event(self, cmd: dict) -> list[dict]:
        """
        接收一条 AI 指令，返回需要广播给前端的指令列表。
        """
        outputs = []
        now = time.time()
        t = cmd.get("type")
        v = cmd.get("value", "")

        # ── 冷却检查 ──
        key = f"{t}:{v}"
        if now - self.last_action_time.get(key, 0) < self.cooldown:
            return outputs
        self.last_action_time[key] = now

        # ── A. 虚拟角色对话 ──────────────────────────────────
        if t == "action" and v == "wave_hand":
            outputs.append({
                "scene": "dialogue",
                "character": "bear",
                "dialogue_id": "wave_response",
                "tts": "你好呀！欢迎来到熊乐园！",
            })

        elif t == "action" and v == "heart_particles":
            outputs.append({
                "scene": "dialogue",
                "character": "bear",
                "dialogue_id": "heart_response",
                "tts": "比心！你真可爱～",
            })
            # ── C. AR 合影特效（附带关键点）────────────────
            if "keypoints" in cmd:
                outputs.append({
                    "scene": "ar_overlay",
                    "effect": "bear_hat",
                    "face_bbox": cmd.get("face_bbox"),
                    "keypoints": cmd["keypoints"],
                })

        elif t == "expression":
            expr = cmd.get("value", "")
            tts_map = {
                "smile_feedback":  ("smile_response",  "你笑得真甜！"),
                "wow_feedback":    ("wow_response",     "哇！你惊到啦！"),
                "angry_feedback":  ("angry_response",   "别生气嘛，熊熊陪你～"),
                "fear_feedback":   ("fear_response",    "不要怕，我在呢！"),
                "sad_feedback":    ("sad_response",     "怎么啦？要抱抱吗？"),
                "idle_feedback":   None,
            }
            entry = tts_map.get(expr)
            if entry:
                dialogue_id, tts_text = entry
                outputs.append({
                    "scene": "dialogue",
                    "character": "bear",
                    "dialogue_id": dialogue_id,
                    "tts": tts_text,
                    "face_bbox": cmd.get("face_bbox"),
                })

        # ── B. 剧情分支切换（CHEER 累计）────────────────────
        elif t == "action" and v == "cheer_light_sound":
            self.cheer_count += 1
            outputs.append({
                "scene": "cheer_effect",
                "cheer_total": self.cheer_count,
            })
            if self.cheer_count % CHEER_SCENE_THRESHOLD == 0:
                self.current_scene = (self.current_scene + 1) % SCENE_COUNT
                outputs.append({
                    "scene": "switch_scene",
                    "scene_id": self.current_scene,
                    "trigger": "cheer",
                    "cheer_total": self.cheer_count,
                })
                print(f"[SCENE] 剧情切换 → 场景 {self.current_scene}")

        return outputs

scene_sm = SceneStateMachine()

# ── 全局 asyncio loop（供 TCP 线程回调）────────────────────
_loop: asyncio.AbstractEventLoop = None

def _dispatch(cmd: dict, received_ts: float | None = None):
    """在 TCP 线程中调用，把事件投递到 asyncio loop。"""
    if _loop is None:
        return
    if "event_id" not in cmd:
        cmd["event_id"] = f"backend-{time.time_ns()}"
    if received_ts is None:
        received_ts = time.time()

    outputs = scene_sm.on_event(cmd)
    if not outputs:
        bench_logger.log(
            "event_suppressed",
            event_id=cmd.get("event_id"),
            source_type=cmd.get("type"),
            source_value=cmd.get("value"),
            backend_receive_ts=received_ts,
        )

    for out in outputs:
        broadcast_ts = time.time()
        out.update({
            "event_id": cmd.get("event_id"),
            "pipeline_ts": cmd.get("pipeline_ts"),
            "model_event_ts": cmd.get("model_event_ts"),
            "backend_receive_ts": received_ts,
            "backend_broadcast_ts": broadcast_ts,
            "frame_idx": cmd.get("frame_idx"),
            "source_frame_id": cmd.get("source_frame_id"),
            "source_timestamp_us": cmd.get("source_timestamp_us"),
            "source_type": cmd.get("type"),
            "source_value": cmd.get("value"),
        })
        print(f"[BROADCAST] {out}")
        bench_logger.log(
            "broadcast_queued",
            event_id=out.get("event_id"),
            scene=out.get("scene"),
            dialogue_id=out.get("dialogue_id"),
            source_type=out.get("source_type"),
            source_value=out.get("source_value"),
            backend_receive_ts=received_ts,
            backend_broadcast_ts=broadcast_ts,
            ws_clients=manager.client_count,
        )
        asyncio.run_coroutine_threadsafe(manager.broadcast(out), _loop)

# ── TCP 服务（独立线程）──────────────────────────────────────
def _handle_client(conn: socket.socket, addr):
    print(f"[TCP] 边缘端接入: {addr}")
    buf = b""
    try:
        while True:
            data = conn.recv(4096)
            if not data:
                break
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.strip()
                if not line:
                    continue
                try:
                    cmd = json.loads(line.decode("utf-8"))
                    received_ts = time.time()
                    bench_logger.log(
                        "tcp_event_received",
                        event_id=cmd.get("event_id"),
                        source_type=cmd.get("type"),
                        source_value=cmd.get("value"),
                        pipeline_ts=cmd.get("pipeline_ts"),
                        frame_idx=cmd.get("frame_idx"),
                        source_frame_id=cmd.get("source_frame_id"),
                        backend_receive_ts=received_ts,
                    )
                    _dispatch(cmd, received_ts)
                except json.JSONDecodeError:
                    pass
    except Exception as e:
        print(f"[TCP] 连接异常: {e}")
    finally:
        conn.close()
        print(f"[TCP] 边缘端断开: {addr}")

def _tcp_server():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((TCP_HOST, TCP_PORT))
    srv.listen(5)
    print(f"[TCP] 监听 {TCP_HOST}:{TCP_PORT}")
    while True:
        conn, addr = srv.accept()
        t = threading.Thread(target=_handle_client, args=(conn, addr), daemon=True)
        t.start()

# ── WebSocket 端点 ───────────────────────────────────────────
@app.websocket("/ws")
async def ws_endpoint(ws: WebSocket):
    await manager.connect(ws)
    try:
        while True:
            # 保持连接存活；Unity 也可发消息过来（暂不处理）
            await ws.receive_text()
    except WebSocketDisconnect:
        pass
    finally:
        await manager.disconnect(ws)

# ── 状态查询接口（调试用）───────────────────────────────────
@app.get("/status")
def status():
    return {
        "ws_clients": manager.client_count,
        "current_scene": scene_sm.current_scene,
        "cheer_count": scene_sm.cheer_count,
    }

# ── 启动 ─────────────────────────────────────────────────────
@app.on_event("startup")
async def startup():
    global _loop
    _loop = asyncio.get_event_loop()
    bench_logger.log(
        "backend_start",
        tcp_host=TCP_HOST,
        tcp_port=TCP_PORT,
        ws_host=WS_HOST,
        ws_port=WS_PORT,
    )
    t = threading.Thread(target=_tcp_server, daemon=True)
    t.start()

if __name__ == "__main__":
    uvicorn.run(app, host=WS_HOST, port=WS_PORT)
