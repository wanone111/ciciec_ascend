"""Offline smoke test: fake audio -> fake ASR -> local reply -> console."""

from .asr.fake_asr import FakeASREngine
from .audio.fake_stream import FakeAudioStream
from .config import DEFAULT_KB_DIR
from .core.dialogue_manager import DialogueManager
from .core.realtime_pipeline import RealtimeDialoguePipeline
from .output.console_speaker import ConsoleSpeaker
from .rag.simple_kb import load_kb


def main():
    print("=== 实时语音角色流水线离线自检 ===")
    manager = DialogueManager(
        load_kb(DEFAULT_KB_DIR),
        chat_fn=lambda _prompt: "俺最喜欢蜂蜜啦！离线语音闭环正常。",
    )
    pipeline = RealtimeDialoguePipeline(
        asr_engine=FakeASREngine(),
        dialogue_manager=manager,
        speaker=ConsoleSpeaker(),
    )

    result = None
    for idx, chunk in enumerate(FakeAudioStream(interval=0), start=1):
        print(f"chunk {idx}: energy={abs(chunk).mean():.6f}")
        result = pipeline.handle_audio_chunk(chunk)
        if result is not None:
            break

    if result is None:
        raise RuntimeError("流水线未检测到完整语句")
    print("识别文本：", result["user_text"])
    print("回复：", result["reply"]["text"])
    print("最终状态：", result["state"])


if __name__ == "__main__":
    main()
