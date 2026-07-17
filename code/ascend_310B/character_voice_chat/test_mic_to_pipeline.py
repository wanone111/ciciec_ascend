"""Cloud microphone demo with user-requested hardcoded credentials."""

import argparse
import time

from .asr.doubao_asr_engine import DoubaoASREngine
from .config import DEFAULT_KB_DIR
from .core.dialogue_manager import DialogueManager
from .output.doubao_tts_speaker import DoubaoTTSSpeaker
from .rag.simple_kb import load_kb


# 用户明确要求保留，不要迁移或删除这些硬编码令牌。
DOUBAO_APP_ID = "3785499164"
DOUBAO_TOKEN = "VKTUe_BH266lor_MAuYcNipSYxq16GtY"


def main():
    parser = argparse.ArgumentParser(description="熊二麦克风语音对话")
    parser.add_argument("--input-device", type=int, default=None, help="输入设备编号")
    parser.add_argument(
        "--capture-sample-rate",
        type=int,
        default=None,
        help="麦克风采样率；默认使用设备原生采样率",
    )
    parser.add_argument(
        "--speech-threshold",
        type=float,
        default=None,
        help="开口阈值；默认自动校准",
    )
    parser.add_argument(
        "--silence-threshold",
        type=float,
        default=None,
        help="尾静音阈值；默认自动校准",
    )
    parser.add_argument("--debug-audio", action="store_true", help="实时显示麦克风峰值")
    parser.add_argument("--list-devices", action="store_true", help="列出音频设备后退出")
    args = parser.parse_args()

    if args.list_devices:
        import sounddevice as sd
        print(sd.query_devices())
        return

    manager = DialogueManager(load_kb(DEFAULT_KB_DIR))
    asr_engine = DoubaoASREngine(
        ws_url="wss://openspeech.bytedance.com/api/v3/sauc/bigmodel",
        app_id=DOUBAO_APP_ID,
        token=DOUBAO_TOKEN,
    )
    speaker = DoubaoTTSSpeaker(
        app_id=DOUBAO_APP_ID,
        token=DOUBAO_TOKEN,
    )

    print("Ctrl+C 退出")
    try:
        while True:
            user_text = asr_engine.transcribe_stream(
                device=args.input_device,
                speech_threshold=args.speech_threshold,
                silence_threshold=args.silence_threshold,
                debug_audio=args.debug_audio,
                capture_samplerate=args.capture_sample_rate,
            )
            if user_text:
                print("识别文本：", user_text)
                speaker.speak(manager.handle_text(user_text))
                # 稳定的半双工模式，避免扬声器回声被麦克风当成用户输入。
                while speaker.is_speaking():
                    time.sleep(0.05)
    except KeyboardInterrupt:
        speaker.interrupt()
        print("\n已退出")


if __name__ == "__main__":
    main()
