"""Environment-backed configuration for the voice interaction package."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path


PACKAGE_DIR = Path(__file__).resolve().parent
DEFAULT_KB_DIR = PACKAGE_DIR / "data" / "character_kb"


def _env_float(name: str, default: float) -> float:
    value = os.getenv(name)
    return default if value in (None, "") else float(value)


def _env_int(name: str, default: int) -> int:
    value = os.getenv(name)
    return default if value in (None, "") else int(value)


@dataclass(frozen=True)
class VoiceChatSettings:
    """Configuration with safe defaults and no credentials in source control."""

    sample_rate: int = 16_000
    speech_start_threshold: float = 0.01
    silence_threshold: float = 0.003
    silence_chunks_needed: int = 4
    max_utterance_seconds: float = 30.0
    ark_api_key: str = ""
    ark_model: str = "doubao-seed-2-0-pro-260215"
    ark_url: str = "https://ark.cn-beijing.volces.com/api/v3/responses"
    doubao_app_id: str = ""
    doubao_token: str = ""
    doubao_asr_url: str = "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel"

    @classmethod
    def from_env(cls) -> "VoiceChatSettings":
        return cls(
            sample_rate=_env_int("VOICE_SAMPLE_RATE", 16_000),
            speech_start_threshold=_env_float("VOICE_SPEECH_START_THRESHOLD", 0.01),
            silence_threshold=_env_float("VOICE_SILENCE_THRESHOLD", 0.003),
            silence_chunks_needed=_env_int("VOICE_SILENCE_CHUNKS", 4),
            max_utterance_seconds=_env_float("VOICE_MAX_UTTERANCE_SECONDS", 30.0),
            ark_api_key=os.getenv("VOLCENGINE_API_KEY", ""),
            ark_model=os.getenv("VOLCENGINE_MODEL", "doubao-seed-2-0-pro-260215"),
            ark_url=os.getenv(
                "VOLCENGINE_RESPONSES_URL",
                "https://ark.cn-beijing.volces.com/api/v3/responses",
            ),
            doubao_app_id=os.getenv("DOUBAO_APP_ID", ""),
            doubao_token=os.getenv("DOUBAO_TOKEN", ""),
            doubao_asr_url=os.getenv(
                "DOUBAO_ASR_URL",
                "wss://openspeech.bytedance.com/api/v3/sauc/bigmodel",
            ),
        )

    def require_ark(self) -> None:
        if not self.ark_api_key:
            raise RuntimeError("缺少 VOLCENGINE_API_KEY，无法调用在线大模型")

    def require_doubao(self) -> None:
        missing = [
            name
            for name, value in (
                ("DOUBAO_APP_ID", self.doubao_app_id),
                ("DOUBAO_TOKEN", self.doubao_token),
            )
            if not value
        ]
        if missing:
            raise RuntimeError(f"缺少环境变量：{', '.join(missing)}")
