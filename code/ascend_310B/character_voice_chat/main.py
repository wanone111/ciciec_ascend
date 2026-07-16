import argparse

from .config import DEFAULT_KB_DIR
from .core.dialogue_manager import DialogueManager
from .rag.simple_kb import load_kb


def _offline_chat(_prompt: str) -> str:
    return "俺听见啦！本地闭环运行正常，配置云端密钥后就能生成更自然的回答。"


def main():
    parser = argparse.ArgumentParser(description="熊二角色文本对话入口")
    parser.add_argument(
        "--online",
        action="store_true",
        help="调用在线大模型（需要 VOLCENGINE_API_KEY）",
    )
    args = parser.parse_args()

    print("=== 熊二角色对话系统 ===")

    kb = load_kb(DEFAULT_KB_DIR)
    manager = DialogueManager(kb, chat_fn=None if args.online else _offline_chat)
    print("模式：", "在线大模型" if args.online else "离线自检")

    while True:
        user_input = input("\n你：")

        if user_input.lower() in ["exit", "quit"]:
            break

        reply = manager.handle_text(user_input)
        print("熊二：", reply["text"])


if __name__ == "__main__":
    main()
