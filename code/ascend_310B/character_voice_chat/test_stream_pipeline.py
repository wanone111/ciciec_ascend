import numpy as np
from ciciec_ascend.character_voice_chat.audio.fake_stream import FakeAudioStream
from ciciec_ascend.character_voice_chat.rag.simple_kb import load_kb
from ciciec_ascend.character_voice_chat.core.dialogue_manager import DialogueManager
from ciciec_ascend.character_voice_chat.asr.fake_asr import FakeASREngine
from ciciec_ascend.character_voice_chat.output.console_speaker import ConsoleSpeaker
from ciciec_ascend.character_voice_chat.core.realtime_pipeline import RealtimeDialoguePipeline


def main():
    print("=== 实时语音角色流水线 + 打断测试 ===")

    kb = load_kb("data/character_kb")
    manager = DialogueManager(kb)
    asr_engine = FakeASREngine()
    speaker = ConsoleSpeaker()

    pipeline = RealtimeDialoguePipeline(
        asr_engine=asr_engine,
        dialogue_manager=manager,
        speaker=speaker,
        silence_threshold=0.003,
        silence_chunks_needed=4,
        speech_start_threshold=0.01
    )

    stream = FakeAudioStream(interval=0.05)

    # 第一轮：正常说一句话
    for idx, chunk in enumerate(stream, start=1):
        energy = float(abs(chunk).mean())
        print(f"收到 chunk {idx}, energy={energy:.6f}")

        result = pipeline.handle_audio_chunk(chunk)

        if result is not None:
            print("识别文本：", result["user_text"])
            print("回复结构：", result["reply"])
            print("状态：", result["state"])
            print("本轮处理完成")
            break

    # 第二轮：模拟熊二还在说话时，用户突然插话
    print("\n=== 模拟用户打断熊二 ===")
    interrupt_chunk = np.random.randn(1600).astype(np.float32) * 0.05
    pipeline.handle_audio_chunk(interrupt_chunk)

    # 手动模拟“熊二说完了”
    speaker.finish()

    print("测试结束")


if __name__ == "__main__":
    main()