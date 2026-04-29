"""
全流程启动器（真实推理版）
==========================
一键启动完整链路：
  run_pipeline  →(TCP:9000)→  backend  →(WebSocket:8000)→  video_player

启动顺序：
  1. backend/server.py        等待 2s
  2. scripts/video_player.py  等待 1.5s
  3. run_pipeline.py          （主推理，前两个就绪后启动）

视频配置：
  configs/video_config.json — 修改此文件即可更换熊二互动视频，无需改代码

按 Ctrl+C 同时关闭所有进程。

运行：
  python scripts/launch_pipeline.py
"""

import os
import subprocess
import sys
import time

PROJECT_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
PYTHON = r"G:\Anaconda\envs\torch_env\python.exe"

PROCS = [
    {
        "name": "backend",
        "cmd": [PYTHON, os.path.join(PROJECT_ROOT, "backend", "server.py")],
        "wait_after": 2.0,
    },
    {
        "name": "video_player",
        "cmd": [PYTHON, os.path.join(PROJECT_ROOT, "scripts", "video_player.py")],
        "wait_after": 1.5,
    },
    {
        "name": "run_pipeline",
        "cmd": [PYTHON, os.path.join(PROJECT_ROOT, "run_pipeline.py")],
        "wait_after": 0,
    },
]


def main():
    print("=" * 50)
    print("  智能 IP 互动 — 全流程启动器")
    print("=" * 50)

    procs = []
    for cfg in PROCS:
        print(f"\n[{cfg['name']}] 启动中...")
        p = subprocess.Popen(
            cfg["cmd"],
            cwd=PROJECT_ROOT,
            creationflags=subprocess.CREATE_NEW_CONSOLE,
        )
        procs.append((cfg["name"], p))
        print(f"[{cfg['name']}] PID={p.pid}")
        if cfg["wait_after"] > 0:
            time.sleep(cfg["wait_after"])

    print("\n" + "=" * 50)
    print("  所有进程已启动，按 Ctrl+C 全部关闭")
    print("=" * 50)
    print("\n  backend      → http://127.0.0.1:8000/status  (状态/调试)")
    print("  video_player → 熊二互动视频窗口")
    print("  run_pipeline → 姿态+表情推理窗口\n")

    try:
        while True:
            for name, p in procs:
                if p.poll() is not None:
                    print(f"\n[WARN] {name} 已退出 (code={p.returncode})")
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n\n[INFO] 正在关闭所有进程...")

    for name, p in procs:
        if p.poll() is None:
            p.terminate()
            print(f"  [STOP] {name}")

    time.sleep(1)
    for name, p in procs:
        if p.poll() is None:
            p.kill()

    print("[INFO] 全部已关闭")


if __name__ == "__main__":
    main()
