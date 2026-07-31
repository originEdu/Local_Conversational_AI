import pytest
from fastapi.testclient import TestClient

from app import main


class FakeSession:
    def __init__(self, frames: list[dict]):
        self.frames = frames
        self.received: list[str] = []

    async def handle(self, text: str):
        self.received.append(text)
        for frame in self.frames:
            yield frame


@pytest.fixture
def client(monkeypatch):
    session = FakeSession(
        [
            {
                "type": "speech",
                "seq": 0,
                "text": "안녕하세요.",
                "audioBase64": "AAAA",
                "visemes": [{"v": "AA", "start": 0, "end": 100}],
            },
            {"type": "turn_end", "seq": 1},
        ]
    )
    monkeypatch.setattr(main, "build_session", lambda: session)
    return TestClient(main.app), session


def test_round_trip(client):
    test_client, session = client

    with test_client.websocket_connect("/ws") as ws:
        ws.send_json({"type": "user_message", "text": "안녕"})
        first = ws.receive_json()
        second = ws.receive_json()

    assert session.received == ["안녕"]
    assert first["type"] == "speech"
    assert first["visemes"] == [{"v": "AA", "start": 0, "end": 100}]
    assert second == {"type": "turn_end", "seq": 1}


def test_unknown_message_type_returns_error(client):
    test_client, _ = client

    with test_client.websocket_connect("/ws") as ws:
        ws.send_json({"type": "ping"})
        frame = ws.receive_json()

    assert frame["type"] == "error"
    assert frame["code"] == "BAD_REQUEST"


def test_missing_text_returns_error(client):
    test_client, _ = client

    with test_client.websocket_connect("/ws") as ws:
        ws.send_json({"type": "user_message"})
        frame = ws.receive_json()

    assert frame["type"] == "error"
    assert frame["code"] == "BAD_REQUEST"
