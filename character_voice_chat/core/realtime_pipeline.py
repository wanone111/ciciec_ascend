from ciciec_ascend.character_voice_chat.audio.stream_buffer import AudioStreamBuffer
from ciciec_ascend.character_voice_chat.audio.utterance_detector import UtteranceDetector
from ciciec_ascend.character_voice_chat.core.pipeline_state import PipelineState
import numpy as np


class RealtimeDialoguePipeline:
    def __init__(self, asr_engine, dialogue_manager, speaker,
                 silence_threshold=0.003, silence_chunks_needed=4,
                 speech_start_threshold=0.01):
        self.asr_engine = asr_engine
        self.dialogue_manager = dialogue_manager
        self.speaker = speaker

        self.buffer = AudioStreamBuffer()
        self.detector = UtteranceDetector(
            silence_threshold=silence_threshold,
            silence_chunks_needed=silence_chunks_needed
        )

        self.state = PipelineState.LISTENING
        self.speech_start_threshold = speech_start_threshold

    def handle_audio_chunk(self, chunk):
        energy = float(np.mean(np.abs(chunk)))

        # 如果 AI 正在说话，同时用户新开口，触发打断
        if self.speaker.is_speaking() and energy > self.speech_start_threshold:
            self.speaker.interrupt()
            self.state = PipelineState.INTERRUPTED

            # 打断后准备接收新的用户发言
            self.buffer.clear()
            self.detector.reset()

        # 检测到用户开始说话
        if energy > self.speech_start_threshold:
            self.state = PipelineState.USER_SPEAKING

        self.buffer.add_chunk(chunk)

        # 检测到一句话结束
        if self.detector.update(chunk):
            full_audio = self.buffer.get_audio()
            if full_audio is None:
                self.state = PipelineState.LISTENING
                return None

            self.state = PipelineState.THINKING

            user_text = self.asr_engine.transcribe(full_audio)
            message = self.dialogue_manager.handle_text(user_text)

            self.state = PipelineState.AI_SPEAKING
            self.speaker.speak(message)

            self.buffer.clear()

            return {
                "user_text": user_text,
                "reply": message,
                "state": self.state
            }

        return None