"""
假前端调试客户端
================
连接后端 WebSocket，把收到的所有消息打印出来。
用于测试完整链路：推理端 → 后端 → 本客户端（替代 Unity/视频展示程序）

运行：
  python scripts/fake_frontend_client.py
"""

import asyncio
import json
import websockets

WS_URL = "ws://127.0.0.1:8000/ws"


async def main():
    print(f"[INFO] 连接 {WS_URL} ...")
    async with websockets.connect(WS_URL) as ws:
        print("[INFO] 已连接，等待消息（Ctrl+C 退出）\n")
        async for raw in ws:
            try:
                msg = json.loads(raw)
                print(f"[RECV] {json.dumps(msg, ensure_ascii=False, indent=2)}")
            except json.JSONDecodeError:
                print(f"[RECV] (非JSON) {raw}")


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[INFO] 退出")
