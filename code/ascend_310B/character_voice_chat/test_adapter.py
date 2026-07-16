"""Offline smoke test for byte/ndarray audio adaptation."""

from .asr.fake_asr import FakeASREngine
from .audio.audio_adapter import AudioStreamAdapter
from .audio.fake_stream import FakeAudioStream
from .config import DEFAULT_KB_DIR
from .core.dialogue_manager import DialogueManager
from .core.realtime_pipeline import RealtimeDialoguePipeline
from .output.console_speaker import ConsoleSpeaker
from .rag.simple_kb import load_kb


def main():
    manager = DialogueManager(
        load_kb(DEFAULT_KB_DIR),
        chat_fn=lambda _prompt: "音频适配器运行正常。",
    )
    adapter = AudioStreamAdapter(
        RealtimeDialoguePipeline(FakeASREngine(), manager, ConsoleSpeaker())
    )
    for chunk in FakeAudioStream(interval=0):
        result = adapter.on_chunk(chunk)
        if result:
            print(result)
            return
    raise RuntimeError("适配器未输出对话结果")


if __name__ == "__main__":
    main()
