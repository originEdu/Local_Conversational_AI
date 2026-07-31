import json
from dataclasses import replace

import httpx
import pytest

from app import llm as llm_module
from app.config import settings
from app.llm import LLMUnavailable, stream_chat


@pytest.fixture
def openai_api(monkeypatch):
    """llama.cpp 등 OpenAI 호환 서버를 쓰도록 설정을 바꾼다."""
    monkeypatch.setattr(llm_module, "settings", replace(settings, llm_api="openai"))


def make_client(lines: list[str]) -> httpx.AsyncClient:
    def handler(request: httpx.Request) -> httpx.Response:
        return httpx.Response(200, content="\n".join(lines).encode())

    return httpx.AsyncClient(transport=httpx.MockTransport(handler))


def chunk(content: str, done: bool = False) -> str:
    return json.dumps({"message": {"content": content}, "done": done})


async def test_yields_tokens_in_order():
    client = make_client([chunk("안"), chunk("녕"), chunk("", done=True)])

    tokens = [token async for token in stream_chat([], client=client)]

    assert tokens == ["안", "녕"]


async def test_stops_at_done():
    client = make_client([chunk("가"), chunk("", done=True), chunk("무시됨")])

    tokens = [token async for token in stream_chat([], client=client)]

    assert tokens == ["가"]


async def test_ignores_blank_lines():
    client = make_client([chunk("가"), "", chunk("", done=True)])

    tokens = [token async for token in stream_chat([], client=client)]

    assert tokens == ["가"]


async def test_connection_error_raises_llm_unavailable():
    def handler(request: httpx.Request) -> httpx.Response:
        raise httpx.ConnectError("연결 거부")

    client = httpx.AsyncClient(transport=httpx.MockTransport(handler))

    with pytest.raises(LLMUnavailable):
        [token async for token in stream_chat([], client=client)]


async def test_http_error_raises_llm_unavailable():
    def handler(request: httpx.Request) -> httpx.Response:
        return httpx.Response(500, content=b"boom")

    client = httpx.AsyncClient(transport=httpx.MockTransport(handler))

    with pytest.raises(LLMUnavailable):
        [token async for token in stream_chat([], client=client)]


def sse(content: str) -> str:
    return "data: " + json.dumps({"choices": [{"delta": {"content": content}}]})


async def test_ollama_uses_api_chat_path():
    seen = {}

    def handler(request: httpx.Request) -> httpx.Response:
        seen["path"] = request.url.path
        return httpx.Response(200, content=chunk("", done=True).encode())

    client = httpx.AsyncClient(transport=httpx.MockTransport(handler))
    [token async for token in stream_chat([], client=client)]

    assert seen["path"] == "/api/chat"


async def test_openai_uses_chat_completions_path(openai_api):
    seen = {}

    def handler(request: httpx.Request) -> httpx.Response:
        seen["path"] = request.url.path
        return httpx.Response(200, content=b"data: [DONE]")

    client = httpx.AsyncClient(transport=httpx.MockTransport(handler))
    [token async for token in stream_chat([], client=client)]

    assert seen["path"] == "/v1/chat/completions"


async def test_openai_yields_tokens_in_order(openai_api):
    client = make_client([sse("안"), sse("녕"), "data: [DONE]"])

    tokens = [token async for token in stream_chat([], client=client)]

    assert tokens == ["안", "녕"]


async def test_openai_stops_at_done_marker(openai_api):
    client = make_client([sse("가"), "data: [DONE]", sse("무시됨")])

    tokens = [token async for token in stream_chat([], client=client)]

    assert tokens == ["가"]


async def test_openai_ignores_blank_and_comment_lines(openai_api):
    client = make_client(["", ": keep-alive", sse("가"), "", "data: [DONE]"])

    tokens = [token async for token in stream_chat([], client=client)]

    assert tokens == ["가"]


async def test_openai_stops_at_finish_reason(openai_api):
    # llama.cpp는 [DONE] 없이 finish_reason만 보내고 끊는 경우가 있다
    last = "data: " + json.dumps(
        {"choices": [{"delta": {}, "finish_reason": "stop"}]}
    )
    client = make_client([sse("가"), last, sse("무시됨")])

    tokens = [token async for token in stream_chat([], client=client)]

    assert tokens == ["가"]


async def test_openai_disables_thinking(openai_api):
    # 추론형 모델은 버려질 사고 토큰을 수백 개 만든다. 첫 발화가 그만큼 늦어진다
    seen = {}

    def handler(request: httpx.Request) -> httpx.Response:
        seen["body"] = json.loads(request.content)
        return httpx.Response(200, content=b"data: [DONE]")

    client = httpx.AsyncClient(transport=httpx.MockTransport(handler))
    [token async for token in stream_chat([], client=client)]

    assert seen["body"]["chat_template_kwargs"] == {"enable_thinking": False}


async def test_ollama_payload_has_no_thinking_flag():
    seen = {}

    def handler(request: httpx.Request) -> httpx.Response:
        seen["body"] = json.loads(request.content)
        return httpx.Response(200, content=chunk("", done=True).encode())

    client = httpx.AsyncClient(transport=httpx.MockTransport(handler))
    [token async for token in stream_chat([], client=client)]

    assert "chat_template_kwargs" not in seen["body"]


async def test_openai_ignores_reasoning_content(openai_api):
    # 사고 토큰이 와도 발화 내용으로 흘려보내면 안 된다
    thinking = "data: " + json.dumps(
        {"choices": [{"delta": {"reasoning_content": "생각 중"}}]}
    )
    client = make_client([thinking, sse("가"), "data: [DONE]"])

    tokens = [token async for token in stream_chat([], client=client)]

    assert tokens == ["가"]


async def test_unknown_api_raises(monkeypatch):
    monkeypatch.setattr(llm_module, "settings", replace(settings, llm_api="grpc"))
    client = make_client([])

    with pytest.raises(LLMUnavailable):
        [token async for token in stream_chat([], client=client)]
