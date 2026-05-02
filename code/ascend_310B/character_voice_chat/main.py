from ciciec_ascend.character_voice_chat.rag.simple_kb import load_kb
from ciciec_ascend.character_voice_chat.core.dialogue_manager import DialogueManager


def main():
    print("=== 熊二角色对话系统（测试版）===")

    kb = load_kb("data/character_kb")
    manager = DialogueManager(kb)

    while True:
        user_input = input("\n你：")

        if user_input.lower() in ["exit", "quit"]:
            break

        reply = manager.handle_text(user_input)
        print("熊二：", reply)


if __name__ == "__main__":
    main()