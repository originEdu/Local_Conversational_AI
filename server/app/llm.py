"""Ollama를 스트리밍 호출한다.

/api/chat은 stream=true일 때 줄 단위 JSON을 흘려보낸다. 응답 전체를 기다리지 않고
토큰이 도착하는 대로 내보내야 첫 발화까지의 지연이 줄어든다.
"""

import json
from typing import AsyncIterator

import httpx

from app.config import settings


class LLMUnavailable(Exception):
    """Ollama 호출에 실패했을 때 발생한다."""


async def stream_chat(
    messages: list[dict],
    *,
    client: httpx.AsyncClient | None = None,
) -> AsyncIterator[str]:
    owns_client = client is None
    client = client or httpx.AsyncClient(timeout=120.0)

    payload = {
        "model": settings.llm_model,
        "messages": messages,
        "stream": True,
    }

    try:
        async with client.stream(
            "POST", f"{settings.llm_base_url}/api/chat", json=payload
        ) as response:
            if response.status_code != 200:
                await response.aread()
                raise LLMUnavailable(f"Ollama가 {response.status_code}를 반환했다")

            async for line in response.aiter_lines():
                if not line.strip():
                    continue
                data = json.loads(line)
                content = data.get("message", {}).get("content", "")
                if content:
                    yield content
                if data.get("done"):
                    return
    except httpx.HTTPError as error:
        raise LLMUnavailable(str(error)) from error
    finally:
        if owns_client:
            await client.aclose()
