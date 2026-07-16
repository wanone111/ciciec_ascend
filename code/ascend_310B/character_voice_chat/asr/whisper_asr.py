import sounddevice as sd

_model = None


def get_model():
    global _model
    if _model is None:
        try:
            from faster_whisper import WhisperModel
        except ImportError as exc:
            raise RuntimeError("本地 Whisper 需要安装 faster-whisper") from exc
        print("正在加载 Whisper 模型...")
        _model = WhisperModel("small", device="cpu", compute_type="int8")
        print("Whisper 模型加载完成")
    return _model


def record_audio(duration=4, samplerate=16000):
    print("🎤 录音中...")
    audio = sd.rec(
        int(duration * samplerate),
        samplerate=samplerate,
        channels=1,
        dtype="float32"
    )
    sd.wait()
    print("✅ 录音结束")
    return audio.flatten()


def transcribe(audio, samplerate=16000):
    model = get_model()
    segments, _ = model.transcribe(audio, language="zh")
    text = "".join(seg.text for seg in segments)
    return text.strip()
