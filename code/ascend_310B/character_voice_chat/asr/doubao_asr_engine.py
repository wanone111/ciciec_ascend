import gzip
import json
import struct
import threading
import queue
import uuid
import websocket
import numpy as np

from .asr_engine import ASREngine

# Volcengine BigASR binary protocol constants
_PROTOCOL_VERSION = 1
_HEADER_SIZE = 1  # 4 bytes (units of 4)

_MSG_FULL_CLIENT   = 1   # full client request (JSON payload)
_MSG_AUDIO_ONLY    = 2   # audio-only client request

_SER_JSON          = 1
_COMPRESS_NONE     = 0


def _resample_float32(audio, source_rate: int, target_rate: int):
    """Resample a mono float32 chunk while keeping the streaming chunk duration."""
    audio = np.asarray(audio, dtype=np.float32).reshape(-1)
    if source_rate == target_rate or audio.size == 0:
        return audio

    target_size = max(1, int(round(audio.size * target_rate / source_rate)))
    source_positions = np.arange(audio.size, dtype=np.float64)
    target_positions = np.arange(target_size, dtype=np.float64) * source_rate / target_rate
    target_positions = np.minimum(target_positions, audio.size - 1)
    return np.interp(target_positions, source_positions, audio).astype(np.float32)


def _build_header(msg_type, msg_flags=0, serialization=_SER_JSON, compression=_COMPRESS_NONE):
    b0 = (_PROTOCOL_VERSION << 4) | _HEADER_SIZE
    b1 = (msg_type << 4) | msg_flags
    b2 = (serialization << 4) | compression
    b3 = 0x00
    return bytes([b0, b1, b2, b3])


def _pack_json_frame(msg_type, data: dict, msg_flags=0) -> bytes:
    payload = json.dumps(data).encode("utf-8")
    header = _build_header(msg_type, msg_flags)
    size = struct.pack(">I", len(payload))
    return header + size + payload


def _pack_audio_frame(chunk: bytes, is_last: bool) -> bytes:
    flags = 2 if is_last else 0
    header = _build_header(_MSG_AUDIO_ONLY, flags, serialization=0, compression=_COMPRESS_NONE)
    size = struct.pack(">I", len(chunk))
    return header + size + chunk


def _parse_server_frame(data: bytes) -> dict:
    # Frame: 4B header | 4B sequence | 4B payload_size | payload
    if len(data) < 12:
        return {}
    payload_size = struct.unpack(">I", data[8:12])[0]
    payload = data[12:12 + payload_size]
    b2 = data[2]
    serialization = (b2 >> 4) & 0xF
    compression   = b2 & 0xF
    if compression == 1:
        payload = gzip.decompress(payload)
    if serialization == _SER_JSON:
        return json.loads(payload.decode("utf-8"))
    return {}


class DoubaoASREngine(ASREngine):
    def __init__(self, ws_url, app_id, token, resource_id="volc.bigasr.sauc.duration",
                 request_timeout=20.0):
        self.ws_url = ws_url
        self.app_id = app_id
        self.token = token
        self.resource_id = resource_id
        self.request_timeout = float(request_timeout)

    def transcribe(self, audio):
        audio_bytes = self._float32_to_pcm16(audio)
        result_queue = queue.Queue()

        ws = websocket.WebSocketApp(
            self.ws_url,
            header=[
                f"X-Api-App-Key: {self.app_id}",
                f"X-Api-Access-Key: {self.token}",
                f"X-Api-Resource-Id: {self.resource_id}",
                f"X-Api-Connect-Id: {uuid.uuid4()}",
            ],
            on_open=lambda ws: self._on_open(ws, audio_bytes),
            on_message=lambda ws, msg: self._on_message(ws, msg, result_queue),
            on_error=lambda ws, err: None if "opcode=8" in str(err) else print("ASR错误:", err),

        )

        t = threading.Thread(target=ws.run_forever, daemon=True)
        t.start()
        t.join(timeout=self.request_timeout)
        if t.is_alive():
            ws.close()
            t.join(timeout=1)

        return result_queue.get() if not result_queue.empty() else ""

    def _on_open(self, ws, audio_bytes):
        start_msg = {
            "app": {
                "appid": self.app_id,
                "token": self.token,
                "cluster": "volcano_bigasr",
            },
            "user": {
                "uid": "user"
            },
            "audio": {
                "format": "pcm",
                "sample_rate": 16000,
                "channel": 1,
                "bit_depth": 16,
                "codec": "raw",
            },
            "request": {
                "reqid": str(uuid.uuid4()),
                "model_name": "bigmodel",
                "enable_punc": True,
            }
        }
        ws.send(_pack_json_frame(_MSG_FULL_CLIENT, start_msg), opcode=websocket.ABNF.OPCODE_BINARY)

        chunk_size = 3200
        chunks = [audio_bytes[i:i+chunk_size] for i in range(0, len(audio_bytes), chunk_size)]
        for idx, chunk in enumerate(chunks):
            is_last = (idx == len(chunks) - 1)
            ws.send(_pack_audio_frame(chunk, is_last), opcode=websocket.ABNF.OPCODE_BINARY)

    def _on_message(self, ws, message, result_queue):
        try:
            if isinstance(message, bytes):
                data = _parse_server_frame(message)
            else:
                data = json.loads(message)

            if data.get("code", 0) not in (0, 20000000):
                print("ASR服务错误:", data)
                result_queue.put("")
                ws.close()
                return

            result = data.get("result", {})
            text = result.get("text", "") if isinstance(result, dict) else ""
            if text:
                # 始终用最新结果覆盖，确保取到最完整的识别文本
                while not result_queue.empty():
                    result_queue.get_nowait()
                result_queue.put(text)
        except Exception as e:
            print("ASR消息解析错误:", e)

    def transcribe_stream(
        self,
        device=None,
        samplerate=16000,
        speech_threshold=None,
        silence_threshold=None,
        silence_duration=1.8,
        max_duration=30.0,
        chunk_ms=100,
        calibration_duration=0.8,
        warmup_duration=0.5,
        debug_audio=False,
        capture_samplerate=None,
    ) -> str:
        """边录音边实时发送到 ASR，流式显示识别结果，静音结束后返回完整文本。"""
        import sounddevice as sd

        silence_chunks_needed = int(silence_duration * 1000 / chunk_ms)
        max_chunks = int(max_duration * 1000 / chunk_ms)

        audio_queue = queue.Queue()   # 录音块 → ASR 线程
        last_text = [""]

        ws = websocket.WebSocketApp(
            self.ws_url,
            header=[
                f"X-Api-App-Key: {self.app_id}",
                f"X-Api-Access-Key: {self.token}",
                f"X-Api-Resource-Id: {self.resource_id}",
                f"X-Api-Connect-Id: {uuid.uuid4()}",
            ],
            on_open=lambda ws: self._stream_sender(ws, audio_queue, samplerate),
            on_message=lambda ws, msg: self._on_stream_message(ws, msg, last_text),
            on_error=lambda ws, err: None if "opcode=8" in str(err) else print("ASR错误:", err),
        )

        ws_thread = threading.Thread(target=ws.run_forever, daemon=True)

        speech_started = False
        silence_count = 0
        selected_device = sd.query_devices(device, "input")
        capture_samplerate = int(round(
            capture_samplerate or selected_device.get("default_samplerate") or samplerate
        ))
        chunk_size = int(capture_samplerate * chunk_ms / 1000)
        print(
            f"🎤 输入设备: {selected_device['name']} "
            f"({capture_samplerate} Hz → ASR {samplerate} Hz)"
        )

        with sd.InputStream(device=device, samplerate=capture_samplerate, channels=1,
                            dtype="float32", blocksize=chunk_size) as stream:
            if speech_threshold is None or silence_threshold is None:
                # PulseAudio/ALSA 刚启动时可能返回较大的瞬态脉冲，不能用于估计底噪。
                warmup_chunks = max(1, int(warmup_duration * 1000 / chunk_ms))
                for _ in range(warmup_chunks):
                    stream.read(chunk_size)

                calibration_chunks = max(1, int(calibration_duration * 1000 / chunk_ms))
                noise_peaks = []
                print("🔧 正在校准环境噪声，请暂时保持安静...")
                for _ in range(calibration_chunks):
                    chunk, overflowed = stream.read(chunk_size)
                    if overflowed and debug_audio:
                        print("\n[音频] 输入缓冲发生溢出")
                    noise_peaks.append(float(np.max(np.abs(chunk))))

                # 中位数不易受偶发爆音影响；自动开口阈值限制在可讲话触发的范围内。
                noise_floor = float(np.median(noise_peaks))
                if speech_threshold is None:
                    speech_threshold = float(np.clip(noise_floor * 3.0, 0.002, 0.02))
                if silence_threshold is None:
                    silence_threshold = float(np.clip(
                        noise_floor * 1.5,
                        0.0008,
                        speech_threshold * 0.6,
                    ))
                print(
                    f"✅ 校准完成: noise={noise_floor:.5f}, "
                    f"start={speech_threshold:.5f}, silence={silence_threshold:.5f}"
                )

            # 麦克风成功打开后才建立云端连接，避免设备错误时遗留连接线程。
            ws_thread.start()
            print("🎤 请说话...")
            for _ in range(max_chunks):
                chunk, overflowed = stream.read(chunk_size)
                chunk = chunk.flatten()
                amplitude = float(np.max(np.abs(chunk)))

                if debug_audio and not speech_started:
                    print(
                        f"\r等待开口: peak={amplitude:.5f} / "
                        f"threshold={speech_threshold:.5f}      ",
                        end="",
                        flush=True,
                    )
                if overflowed and debug_audio:
                    print("\n[音频] 输入缓冲发生溢出")

                if not speech_started:
                    if amplitude >= speech_threshold:
                        speech_started = True
                        print("\n🔴 检测到开口，识别中...", end="\r")
                    else:
                        continue

                asr_chunk = _resample_float32(chunk, capture_samplerate, samplerate)
                pcm = (np.clip(asr_chunk, -1, 1) * 32767).astype(np.int16).tobytes()
                audio_queue.put(pcm)

                if amplitude < silence_threshold:
                    silence_count += 1
                    if silence_count >= silence_chunks_needed:
                        break
                else:
                    silence_count = 0

        audio_queue.put(None)  # 通知发送线程结束
        ws_thread.join(timeout=10)
        if ws_thread.is_alive():
            ws.close()
            ws_thread.join(timeout=1)
        print()  # 换行
        return last_text[0]

    def _stream_sender(self, ws, audio_queue: queue.Queue, samplerate=16000):
        start_msg = {
            "app": {"appid": self.app_id, "token": self.token, "cluster": "volcano_bigasr"},
            "user": {"uid": "user"},
            "audio": {"format": "pcm", "sample_rate": samplerate, "channel": 1, "bit_depth": 16, "codec": "raw"},
            "request": {"reqid": str(uuid.uuid4()), "model_name": "bigmodel", "enable_punc": True},
        }
        ws.send(_pack_json_frame(_MSG_FULL_CLIENT, start_msg), opcode=websocket.ABNF.OPCODE_BINARY)

        while True:
            chunk = audio_queue.get()
            is_last = chunk is None
            if is_last:
                ws.send(_pack_audio_frame(b"", is_last=True), opcode=websocket.ABNF.OPCODE_BINARY)
                break
            ws.send(_pack_audio_frame(chunk, is_last=False), opcode=websocket.ABNF.OPCODE_BINARY)

    def _on_stream_message(self, _ws, message, last_text: list):
        try:
            data = _parse_server_frame(message) if isinstance(message, bytes) else json.loads(message)
            if data.get("code", 0) not in (0, 20000000):
                print("\nASR服务错误:", data)
                return
            text = (data.get("result") or {}).get("text", "")
            if text:
                last_text[0] = text
                print(f"\r识别中: {text}          ", end="", flush=True)
        except Exception as e:
            print("ASR消息解析错误:", e)

    def _float32_to_pcm16(self, audio):
        return (np.clip(audio, -1, 1) * 32767).astype(np.int16).tobytes()
