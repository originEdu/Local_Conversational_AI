"""LLM 서버를 스트리밍 호출한다.

응답 전체를 기다리지 않고 토큰이 도착하는 대로 내보내야 첫 발화까지의 지연이 줄어든다.

두 가지 API 방식을 지원한다. `LLM_API`로 고른다.

- `ollama`  — Ollama. `POST /api/chat`, 줄 단위 JSON, `message.content`, `done: true`로 종료
- `openai`  — llama.cpp server, vLLM 등. `POST /v1/chat/completions`,
              SSE(`data: {...}`), `choices[0].delta.content`, `data: [DONE]`으로 종료

두 방식은 경로도 응답 형식도 다르므로 주소만 바꿔서는 서로를 대체할 수 없다.
"""

import json
from typing import AsyncIterator

import httpx

from app.config import settings


class LLMUnavailable(Exception):
    """LLM 호출에 실패했을 때 발생한다."""


def _parse_ollama_line(line: str) -> tuple[str, bool]:
    """(내보낼 내용, 스트림 종료 여부)를 돌려준다."""
    if not line.strip():
        return "", False

    data = json.loads(line)
    return data.get("message", {}).get("content", ""), bool(data.get("done"))


def _parse_openai_line(line: str) -> tuple[str, bool]:
    line = line.strip()

    # 빈 줄과 주석(`: keep-alive`)은 SSE 프로토콜의 일부다
    if not line or not line.startswith("data:"):
        return "", False

    payload = line[len("data:") :].strip()
    if payload == "[DONE]":
        return "", True

    data = json.loads(payload)
    choices = data.get("choices") or [{}]
    first = choices[0]

    # `reasoning_content`는 사고 과정이라 읽어주면 안 된다. `content`만 본다
    content = first.get("delta", {}).get("content") or ""
    # llama.cpp는 [DONE] 없이 finish_reason만 보내고 끊는 경우가 있다
    done = first.get("finish_reason") is not None

    return content, done


# 추론형 모델은 답변 전에 사고 토큰을 수백 개 만든다. `delta.reasoning_content`로
# 오므로 발화에는 안 쓰이지만 생성 시간은 그대로 든다. 실측 24.2초 중 23.3초가
# 사고였다. 템플릿이 이 값을 안 쓰는 모델은 그냥 무시한다.
_NO_THINKING = {"chat_template_kwargs": {"enable_thinking": False}}

_APIS = {
    "ollama": ("/api/chat", _parse_ollama_line, {}),
    "openai": ("/v1/chat/completions", _parse_openai_line, _NO_THINKING),
}


async def stream_chat(
    messages: list[dict],
    *,
    client: httpx.AsyncClient | None = None,
) -> AsyncIterator[str]:
    api = settings.llm_api.lower()
    if api not in _APIS:
        raise LLMUnavailable(
            f"알 수 없는 LLM API: {settings.llm_api} (ollama 또는 openai)"
        )

    path, parse, extra = _APIS[api]

    owns_client = client is None
    client = client or httpx.AsyncClient(timeout=120.0)

    payload = {
        "model": settings.llm_model,
        "messages": messages,
        "stream": True,
        **extra,
    }

    try:
        async with client.stream(
            "POST", f"{settings.llm_base_url}{path}", json=payload
        ) as response:
            if response.status_code != 200:
                await response.aread()
                raise LLMUnavailable(
                    f"LLM 서버가 {response.status_code}를 반환했다"
                )

            async for line in response.aiter_lines():
                content, done = parse(line)
                if content:
                    yield content
                if done:
                    return
    except httpx.HTTPError as error:
        raise LLMUnavailable(str(error)) from error
    finally:
        if owns_client:
            await client.aclose()
