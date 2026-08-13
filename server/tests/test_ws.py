import base64

import pytest
from fastapi.testclient import TestClient

from app import main
from app.stt import STTError


class FakeStt:
    def __init__(self, text: str = "", error: bool = False):
        self.text = text
        self.error = error
        self.received: list[bytes] = []

    def transcribe(self, wav: bytes) -> str:
        self.received.append(wav)
        if self.error:
            raise STTError("터졌다")
        return self.text


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


@pytest.fixture
def stt(monkeypatch):
    engine = FakeStt()
    monkeypatch.setattr(main, "build_stt", lambda: engine)
    return engine


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


def test_audio_message_returns_transcript_without_running_turn(client, stt):
    test_client, session = client
    stt.text = "안녕"

    with test_client.websocket_connect("/ws") as ws:
        ws.send_json(
            {
                "type": "audio_message",
                "audioBase64": base64.b64encode(b"fake wav").decode("ascii"),
            }
        )
        transcript = ws.receive_json()

        # 턴이 안 돌았는지 확인한다. 다음 메시지가 곧장 처리되면 사이에 낀 게 없다.
        ws.send_json({"type": "user_message", "text": "타자"})
        assert ws.receive_json()["type"] == "speech"

    assert stt.received == [b"fake wav"]
    assert transcript == {"type": "transcript", "text": "안녕"}
    assert session.received == ["타자"]


def test_silent_audio_returns_empty_transcript(client, stt):
    test_client, _ = client
    stt.text = ""

    with test_client.websocket_connect("/ws") as ws:
        ws.send_json(
            {
                "type": "audio_message",
                "audioBase64": base64.b64encode(b"silence").decode("ascii"),
            }
        )
        transcript = ws.receive_json()

    assert transcript == {"type": "transcript", "text": ""}


def test_stt_failure_returns_error(client, stt):
    test_client, session = client
    stt.error = True

    with test_client.websocket_connect("/ws") as ws:
        ws.send_json(
            {
                "type": "audio_message",
                "audioBase64": base64.b64encode(b"junk").decode("ascii"),
            }
        )
        frame = ws.receive_json()

    assert frame["type"] == "error"
    assert frame["code"] == "STT_FAILED"
    assert session.received == []


def test_bad_base64_returns_error(client, stt):
    test_client, _ = client

    with test_client.websocket_connect("/ws") as ws:
        ws.send_json({"type": "audio_message", "audioBase64": "not base64!!"})
        frame = ws.receive_json()

    assert frame["type"] == "error"
    assert frame["code"] == "BAD_REQUEST"
