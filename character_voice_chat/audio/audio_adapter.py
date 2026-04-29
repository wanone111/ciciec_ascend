import numpy as np


class AudioStreamAdapter:
    """
    把外部音频流适配成 pipeline 可用格式
    """

    def __init__(self, pipeline, sample_rate=16000):
        self.pipeline = pipeline
        self.sample_rate = sample_rate

    def on_chunk(self, chunk):
        """
        接收一段音频 chunk
        chunk 可以是：
        - bytes（16bit PCM）
        - numpy.ndarray（float32 / int16）
        """
        audio = self._to_float32(chunk)

        result = self.pipeline.handle_audio_chunk(audio)

        return result

    def _to_float32(self, chunk):
        # numpy float32 直接用
        if isinstance(chunk, np.ndarray):
            if chunk.dtype == np.float32:
                return chunk

            if chunk.dtype == np.int16:
                return chunk.astype(np.float32) / 32768.0

        # bytes → int16 → float32
        if isinstance(chunk, (bytes, bytearray)):
            audio = np.frombuffer(chunk, dtype=np.int16)
            return audio.astype(np.float32) / 32768.0

        raise ValueError(f"不支持的音频类型: {type(chunk)}")