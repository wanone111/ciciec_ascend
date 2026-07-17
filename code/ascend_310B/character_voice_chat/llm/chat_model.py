"""Volcengine Ark chat client."""

from __future__ import annotations

from typing import Any

from ..config import VoiceChatSettings


# 用户明确要求保留硬编码令牌；环境变量仍可临时覆盖它。
API_KEY = "ark-d75e387a-250c-44ad-aa00-a7afd271c206-af86e"


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
    """Send one prompt to Ark, preferring an environment override when present."""

    if not prompt.strip():
        raise ValueError("prompt 不能为空")

    settings = settings or VoiceChatSettings.from_env()
    api_key = settings.ark_api_key or API_KEY

    try:
        import requests
    except ImportError as exc:
        raise RuntimeError("在线对话需要安装 requests") from exc

    response = requests.post(
        settings.ark_url,
        headers={
            "Authorization": f"Bearer {api_key}",
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
