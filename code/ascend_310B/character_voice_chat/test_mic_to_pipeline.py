
from ciciec_ascend.character_voice_chat.rag.simple_kb import load_kb
from ciciec_ascend.character_voice_chat.core.dialogue_manager import DialogueManager
from ciciec_ascend.character_voice_chat.asr.doubao_asr_engine import DoubaoASREngine
from ciciec_ascend.character_voice_chat.output.doubao_tts_speaker import DoubaoTTSSpeaker


def main():
    print("=== 麦克风 → 熊二 对话测试 ===")

    kb = load_kb("data/character_kb")
    manager = DialogueManager(kb)

    # ========== 最重要：ws_url 修复 ==========
    asr_engine = DoubaoASREngine(
        ws_url="wss://openspeech.bytedance.com/api/v3/sauc/bigmodel",
        app_id="3785499164",
        token="VKTUe_BH266lor_MAuYcNipSYxq16GtY",
    )

    speaker = DoubaoTTSSpeaker(
        app_id="3785499164",
        token="VKTUe_BH266lor_MAuYcNipSYxq16GtY",
    )
    print("Ctrl+C 退出")
    while True:
        user_text = asr_engine.transcribe_stream(device=1)
        print("识别文本：", user_text)

        if not user_text:
            continue

        message = manager.handle_text(user_text)
        speaker.speak(message)


if __name__ == "__main__":
    main()