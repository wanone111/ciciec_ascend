"""Volcengine Ark chat client."""

from __future__ import annotations

from typing import Any

from ..config import VoiceChatSettings


def _extract_text(result: dict[str, Any]) -> str:
    if "error" in result:
        raise RuntimeError(str(result["error"]))

    for item in result.get("output", []):
        if item.get("type") != "message":
            continue
        for content in item.get("content", []):
            if content.get("type") == "output_text":
                text = content.get("text", "").strip()
                if text:
                    return text
    raise RuntimeError("模型响应中没有文本输出")


def chat(prompt: str, settings: VoiceChatSettings | None = None) -> str:
    """Send one prompt to Ark. Credentials are read only from the environment."""

    if not prompt.strip():
        raise ValueError("prompt 不能为空")

    settings = settings or VoiceChatSettings.from_env()
    settings.require_ark()

    try:
        import requests
    except ImportError as exc:
        raise RuntimeError("在线对话需要安装 requests") from exc

    response = requests.post(
        settings.ark_url,
        headers={
            "Authorization": f"Bearer {settings.ark_api_key}",
            "Content-Type": "application/json",
        },
        json={
            "model": settings.ark_model,
            "input": [
                {
                    "role": "user",
                    "content": [{"type": "input_text", "text": prompt}],
                }
            ],
        },
        timeout=(5, 60),
    )
    response.raise_for_status()
    return _extract_text(response.json())
