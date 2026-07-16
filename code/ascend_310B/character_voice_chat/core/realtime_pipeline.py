import numpy as np

from ..audio.stream_buffer import AudioStreamBuffer
from ..audio.utterance_detector import UtteranceDetector
from .pipeline_state import PipelineState


class RealtimeDialoguePipeline:
    def __init__(self, asr_engine, dialogue_manager, speaker,
                 silence_threshold=0.003, silence_chunks_needed=4,
                 speech_start_threshold=0.01, sample_rate=16_000,
                 max_utterance_seconds=30.0):
        if silence_threshold >= speech_start_threshold:
            raise ValueError("silence_threshold 必须小于 speech_start_threshold")

        self.asr_engine = asr_engine
        self.dialogue_manager = dialogue_manager
        self.speaker = speaker

        max_samples = max(1, int(sample_rate * max_utterance_seconds))
        self.buffer = AudioStreamBuffer(max_samples=max_samples)
        self.detector = UtteranceDetector(
            silence_threshold=silence_threshold,
            silence_chunks_needed=silence_chunks_needed
        )

        self.state = PipelineState.LISTENING
        self.speech_start_threshold = speech_start_threshold

    def handle_audio_chunk(self, chunk):
        chunk = np.asarray(chunk, dtype=np.float32).reshape(-1)
        if chunk.size == 0:
            return None
        energy = float(np.mean(np.abs(chunk)))

        # 如果 AI 正在说话，同时用户新开口，触发打断
        if self.speaker.is_speaking() and energy > self.speech_start_threshold:
            self.speaker.interrupt()
            self.state = PipelineState.INTERRUPTED

            # 打断后准备接收新的用户发言
            self.buffer.clear()
            self.detector.reset()

        is_speech_start = energy > self.speech_start_threshold
        was_in_utterance = self.detector.has_speech

        # 检测到用户开始说话
        if is_speech_start:
            self.state = PipelineState.USER_SPEAKING

        # 不把用户开口之前的环境静音无限写入缓冲区。
        if is_speech_start or was_in_utterance:
            self.buffer.add_chunk(chunk)

        # 只有跨过开口阈值后才启动尾静音检测，避免中等强度底噪误触发。
        utterance_ended = False
        if is_speech_start or was_in_utterance:
            utterance_ended = self.detector.update(chunk)

        # 检测到一句话结束
        if utterance_ended:
            full_audio = self.buffer.get_audio()
            self.buffer.clear()
            if full_audio is None:
                self.state = PipelineState.LISTENING
                return None

            self.state = PipelineState.THINKING

            user_text = self.asr_engine.transcribe(full_audio)
            user_text = (user_text or "").strip()
            if not user_text:
                self.state = PipelineState.LISTENING
                return {
                    "user_text": "",
                    "reply": None,
                    "state": self.state,
                }

            message = self.dialogue_manager.handle_text(user_text)

            self.state = PipelineState.AI_SPEAKING
            self.speaker.speak(message)
            if not self.speaker.is_speaking():
                self.state = PipelineState.LISTENING

            return {
                "user_text": user_text,
                "reply": message,
                "state": self.state
            }

        return None
