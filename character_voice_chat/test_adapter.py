from ciciec_ascend.character_voice_chat.audio.fake_stream import FakeAudioStream
from ciciec_ascend.character_voice_chat.audio.audio_adapter import AudioStreamAdapter
from ciciec_ascend.character_voice_chat.rag.simple_kb import load_kb
from ciciec_ascend.character_voice_chat.core.dialogue_manager import DialogueManager
from ciciec_ascend.character_voice_chat.asr.doubao_asr_engine import DoubaoASREngine
from ciciec_ascend.character_voice_chat.output.console_speaker import ConsoleSpeaker
from ciciec_ascend.character_voice_chat.core.realtime_pipeline import RealtimeDialoguePipeline


def main():
    print("=== Audio Adapter 测试 ===")

    kb = load_kb("data/character_kb")
    manager = DialogueManager(kb)
    asr_engine = DoubaoASREngine(
        app_id="3659409540",
        token="hm5k63hRlAmI1p-b6k7QhEXyS2WGkw1b",
        ws_url="wss://openspeech.bytedance.com/api/v3/sauc/bigmodel"
    )
    
    speaker = ConsoleSpeaker()

    pipeline = RealtimeDialoguePipeline(
        asr_engine=asr_engine,
        dialogue_manager=manager,
        speaker=speaker,
        silence_threshold=0.002,
        silence_chunks_needed=3,
        speech_start_threshold=0.006
    )

    adapter = AudioStreamAdapter(pipeline)

    stream = FakeAudioStream(interval=0.05)

    for idx, chunk in enumerate(stream, start=1):
        print(f"[外部] 推送 chunk {idx}")

        result = adapter.on_chunk(chunk)

        if result:
            print("识别文本：", result["user_text"])
            print("回复结构：", result["reply"])
            print("----")

    print("测试结束")


if __name__ == "__main__":
    main()