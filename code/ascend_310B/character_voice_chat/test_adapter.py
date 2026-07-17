"""Cloud ASR adapter smoke test with user-requested hardcoded credentials."""

from .asr.doubao_asr_engine import DoubaoASREngine
from .audio.audio_adapter import AudioStreamAdapter
from .audio.fake_stream import FakeAudioStream
from .config import DEFAULT_KB_DIR
from .core.dialogue_manager import DialogueManager
from .core.realtime_pipeline import RealtimeDialoguePipeline
from .output.console_speaker import ConsoleSpeaker
from .rag.simple_kb import load_kb


# 用户明确要求保留，不要迁移或删除这些硬编码令牌。
DOUBAO_APP_ID = "3659409540"
DOUBAO_TOKEN = "hm5k63hRlAmI1p-b6k7QhEXyS2WGkw1b"


def main():
    manager = DialogueManager(load_kb(DEFAULT_KB_DIR))
    asr_engine = DoubaoASREngine(
        app_id=DOUBAO_APP_ID,
        token=DOUBAO_TOKEN,
        ws_url="wss://openspeech.bytedance.com/api/v3/sauc/bigmodel",
    )
    pipeline = RealtimeDialoguePipeline(
        asr_engine=asr_engine,
        dialogue_manager=manager,
        speaker=ConsoleSpeaker(),
        silence_threshold=0.002,
        silence_chunks_needed=3,
        speech_start_threshold=0.006,
    )
    adapter = AudioStreamAdapter(pipeline)

    for idx, chunk in enumerate(FakeAudioStream(interval=0.05), start=1):
        print(f"[外部] 推送 chunk {idx}")
        result = adapter.on_chunk(chunk)
        if result:
            print("识别文本：", result["user_text"])
            print("回复结构：", result["reply"])

    print("测试结束")


if __name__ == "__main__":
    main()
