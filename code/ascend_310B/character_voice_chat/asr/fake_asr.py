from .asr_engine import ASREngine


class FakeASREngine(ASREngine):
    def transcribe(self, audio):
        # 这里只是占位，后面换成真实 ASR
        return "熊二你喜欢吃什么"
