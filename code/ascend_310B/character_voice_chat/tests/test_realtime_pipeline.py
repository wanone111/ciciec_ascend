import unittest

import numpy as np

from character_voice_chat.asr.asr_engine import ASREngine
from character_voice_chat.asr.doubao_asr_engine import _resample_float32
from character_voice_chat.audio.stream_buffer import AudioStreamBuffer
from character_voice_chat.core.dialogue_manager import DialogueManager
from character_voice_chat.core.pipeline_state import PipelineState
from character_voice_chat.core.realtime_pipeline import RealtimeDialoguePipeline
from character_voice_chat.output.speaker import Speaker


class StubASR(ASREngine):
    def __init__(self, text="熊二你好"):
        self.text = text
        self.calls = 0

    def transcribe(self, audio):
        self.calls += 1
        self.last_sample_count = audio.size
        return self.text


class StubSpeaker(Speaker):
    def __init__(self, keep_speaking=False):
        self.speaking = keep_speaking
        self.interrupt_count = 0
        self.messages = []

    def speak(self, message):
        self.messages.append(message)

    def interrupt(self):
        self.interrupt_count += 1
        self.speaking = False

    def is_speaking(self):
        return self.speaking


def make_pipeline(asr=None, speaker=None):
    manager = DialogueManager([], chat_fn=lambda _prompt: "你好呀！")
    return RealtimeDialoguePipeline(
        asr or StubASR(),
        manager,
        speaker or StubSpeaker(),
        silence_chunks_needed=2,
    )


class RealtimePipelineTests(unittest.TestCase):
    def test_offline_turn_reaches_listening_state(self):
        asr = StubASR()
        speaker = StubSpeaker()
        pipeline = make_pipeline(asr, speaker)

        speech = np.full(1600, 0.03, dtype=np.float32)
        silence = np.zeros(1600, dtype=np.float32)
        self.assertIsNone(pipeline.handle_audio_chunk(speech))
        self.assertIsNone(pipeline.handle_audio_chunk(silence))
        result = pipeline.handle_audio_chunk(silence)

        self.assertEqual(result["user_text"], "熊二你好")
        self.assertEqual(result["reply"]["text"], "你好呀！")
        self.assertEqual(result["state"], PipelineState.LISTENING)
        self.assertEqual(asr.calls, 1)
        self.assertEqual(len(speaker.messages), 1)

    def test_leading_noise_is_not_buffered(self):
        pipeline = make_pipeline()
        noise = np.full(1600, 0.005, dtype=np.float32)
        for _ in range(20):
            self.assertIsNone(pipeline.handle_audio_chunk(noise))
        self.assertFalse(pipeline.buffer.has_data())
        self.assertEqual(pipeline.state, PipelineState.LISTENING)

    def test_empty_asr_result_does_not_call_dialogue_or_speaker(self):
        speaker = StubSpeaker()
        pipeline = make_pipeline(StubASR("  "), speaker)
        speech = np.full(800, 0.03, dtype=np.float32)
        silence = np.zeros(800, dtype=np.float32)
        pipeline.handle_audio_chunk(speech)
        pipeline.handle_audio_chunk(silence)
        result = pipeline.handle_audio_chunk(silence)
        self.assertIsNone(result["reply"])
        self.assertEqual(result["state"], PipelineState.LISTENING)
        self.assertEqual(speaker.messages, [])

    def test_user_speech_interrupts_active_speaker(self):
        speaker = StubSpeaker(keep_speaking=True)
        pipeline = make_pipeline(speaker=speaker)
        pipeline.handle_audio_chunk(np.full(800, 0.04, dtype=np.float32))
        self.assertEqual(speaker.interrupt_count, 1)
        self.assertEqual(pipeline.state, PipelineState.USER_SPEAKING)


class AudioBufferTests(unittest.TestCase):
    def test_buffer_keeps_only_latest_samples(self):
        buffer = AudioStreamBuffer(max_samples=5)
        buffer.add_chunk(np.arange(4, dtype=np.float32))
        buffer.add_chunk(np.arange(4, 8, dtype=np.float32))
        np.testing.assert_array_equal(
            buffer.get_audio(), np.array([3, 4, 5, 6, 7], dtype=np.float32)
        )


class AudioResampleTests(unittest.TestCase):
    def test_resamples_48khz_chunk_to_16khz(self):
        source = np.linspace(-1, 1, 4800, dtype=np.float32)
        result = _resample_float32(source, 48_000, 16_000)
        self.assertEqual(result.dtype, np.float32)
        self.assertEqual(result.size, 1600)


if __name__ == "__main__":
    unittest.main()
