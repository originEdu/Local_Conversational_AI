import json

import httpx
import pytest

from app.llm import LLMUnavailable, stream_chat


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
