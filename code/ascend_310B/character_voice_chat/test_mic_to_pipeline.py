"""Cloud microphone demo. Credentials must be supplied through environment variables."""

import time

from .asr.doubao_asr_engine import DoubaoASREngine
from .config import DEFAULT_KB_DIR, VoiceChatSettings
from .core.dialogue_manager import DialogueManager
from .output.doubao_tts_speaker import DoubaoTTSSpeaker
from .rag.simple_kb import load_kb


def main():
    settings = VoiceChatSettings.from_env()
    settings.require_doubao()
    settings.require_ark()

    manager = DialogueManager(load_kb(DEFAULT_KB_DIR))
    asr_engine = DoubaoASREngine(
        ws_url=settings.doubao_asr_url,
        app_id=settings.doubao_app_id,
        token=settings.doubao_token,
    )
    speaker = DoubaoTTSSpeaker(
        app_id=settings.doubao_app_id,
        token=settings.doubao_token,
    )

    print("Ctrl+C 退出")
    while True:
        user_text = asr_engine.transcribe_stream()
        if user_text:
            print("识别文本：", user_text)
            speaker.speak(manager.handle_text(user_text))
            # 当前入口采用稳定的半双工模式，避免扬声器回声被麦克风当成用户输入。
            while speaker.is_speaking():
                time.sleep(0.05)


if __name__ == "__main__":
    main()
