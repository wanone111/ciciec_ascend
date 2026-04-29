"""
310B 端模拟发送测试脚本
========================
模拟昇腾310B边缘端，向 PC 后端 TCP 9000 端口发送推理结果。
用于在没有硬件的情况下验证完整链路：
  本脚本 → backend/server.py → video_player.py / fake_frontend_client.py

运行前请先启动后端：
  python backend/server.py

运行：
  python scripts/simulate_310b.py [--mode auto|interactive]

  auto        : 自动循环发送所有动作和表情（默认）
  interactive : 手动输入选择发送内容
"""

import argparse
import json
import socket
import time
import random

HOST = "127.0.0.1"
PORT = 9000

# ── 预设的测试数据包 ────────────────────────────────────────

FAKE_KEYPOINTS = [
    [320.0, 150.0], [310.0, 140.0], [330.0, 140.0],
    [300.0, 138.0], [340.0, 138.0], [290.0, 200.0],
    [350.0, 200.0], [270.0, 260.0], [370.0, 260.0],
    [260.0, 310.0], [380.0, 310.0], [295.0, 340.0],
    [345.0, 340.0], [290.0, 420.0], [350.0, 420.0],
    [285.0, 500.0], [355.0, 500.0],
]

FAKE_FACE_BBOX = [295, 120, 365, 200]

ACTION_PACKETS = {
    "1": {
        "label": "挥手 (WAVE)",
        "data": {
            "type": "action",
            "value": "wave_hand",
            "keypoints": FAKE_KEYPOINTS,
            "timestamp": 0,
        }
    },
    "2": {
        "label": "比心 (HEART)",
        "data": {
            "type": "action",
            "value": "heart_particles",
            "keypoints": FAKE_KEYPOINTS,
            "face_bbox": FAKE_FACE_BBOX,
            "timestamp": 0,
        }
    },
    "3": {
        "label": "欢呼 (CHEER)",
        "data": {
            "type": "action",
            "value": "cheer_light_sound",
            "keypoints": FAKE_KEYPOINTS,
            "timestamp": 0,
        }
    },
    "4": {
        "label": "表情-开心 (happy)",
        "data": {
            "type": "expression",
            "value": "smile_feedback",
            "face_bbox": FAKE_FACE_BBOX,
            "keypoints": FAKE_KEYPOINTS,
            "timestamp": 0,
        }
    },
    "5": {
        "label": "表情-惊讶 (surprised)",
        "data": {
            "type": "expression",
            "value": "wow_feedback",
            "face_bbox": FAKE_FACE_BBOX,
            "keypoints": FAKE_KEYPOINTS,
            "timestamp": 0,
        }
    },
    "6": {
        "label": "表情-生气 (angry)",
        "data": {
            "type": "expression",
            "value": "angry_feedback",
            "face_bbox": FAKE_FACE_BBOX,
            "keypoints": FAKE_KEYPOINTS,
            "timestamp": 0,
        }
    },
    "7": {
        "label": "表情-悲伤 (sad)",
        "data": {
            "type": "expression",
            "value": "sad_feedback",
            "face_bbox": FAKE_FACE_BBOX,
            "keypoints": FAKE_KEYPOINTS,
            "timestamp": 0,
        }
    },
}


def send_packet(sock, data: dict):
    data["timestamp"] = time.time()
    msg = (json.dumps(data, ensure_ascii=False) + "\n").encode("utf-8")
    sock.sendall(msg)
    print(f"  [SENT] type={data['type']} value={data['value']}")


def connect(host, port, retries=5):
    for i in range(retries):
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((host, port))
            print(f"[OK] 已连接 {host}:{port}")
            return s
        except ConnectionRefusedError:
            print(f"[WARN] 连接失败，{2}s 后重试 ({i+1}/{retries})...")
            time.sleep(2)
    print("[ERROR] 无法连接后端，请先运行 backend/server.py")
    return None


def mode_auto(sock, interval=2.0):
    """自动循环发送所有事件，每次间隔 interval 秒。"""
    keys = list(ACTION_PACKETS.keys())
    print(f"\n[AUTO] 自动模式，间隔 {interval}s，Ctrl+C 停止\n")
    try:
        while True:
            key = random.choice(keys)
            item = ACTION_PACKETS[key]
            print(f">>> {item['label']}")
            send_packet(sock, dict(item["data"]))
            time.sleep(interval)
    except KeyboardInterrupt:
        print("\n[INFO] 已停止")


def mode_interactive(sock):
    """手动选择发送内容。"""
    print("\n[INTERACTIVE] 手动模式，输入编号发送，q 退出\n")
    while True:
        print("可发送的事件：")
        for k, v in ACTION_PACKETS.items():
            print(f"  {k}. {v['label']}")
        choice = input("输入编号（或 q 退出）: ").strip()
        if choice.lower() == "q":
            break
        item = ACTION_PACKETS.get(choice)
        if item:
            send_packet(sock, dict(item["data"]))
        else:
            print("  无效输入")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=["auto", "interactive"], default="auto")
    parser.add_argument("--interval", type=float, default=2.0, help="auto模式发送间隔（秒）")
    args = parser.parse_args()

    sock = connect(HOST, PORT)
    if sock is None:
        exit(1)

    try:
        if args.mode == "auto":
            mode_auto(sock, interval=args.interval)
        else:
            mode_interactive(sock)
    finally:
        sock.close()
        print("[INFO] 连接已关闭")
