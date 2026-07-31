import numpy as np

from app.llm import LLMUnavailable
from app.session import Session
from app.tts import SynthesisResult


class FakeTTS:
    def synthesize(self, text: str) -> SynthesisResult:
        return SynthesisResult(np.zeros(3200, dtype=np.float32), 16000)


def token_stream_from(tokens: list[str]):
    async def stream(messages, **kwargs):
        for token in tokens:
            yield token

    return stream


def failing_token_stream(messages, **kwargs):
    async def stream():
        raise LLMUnavailable("연결 거부")
        yield  # pragma: no cover

    return stream()


async def collect(session: Session, text: str) -> list[dict]:
    return [frame async for frame in session.handle(text)]


async def test_emits_speech_frames_then_turn_end():
    session = Session(
        FakeTTS(), None, token_stream=token_stream_from(["오늘은 날씨가 좋네요."])
    )

    frames = await collect(session, "안녕")

    assert [f["type"] for f in frames] == ["speech", "turn_end"]
    assert frames[0]["text"] == "오늘은 날씨가 좋네요."


async def test_seq_increments_per_sentence():
    session = Session(
        FakeTTS(),
        None,
        token_stream=token_stream_from(["가나다라마바사아자. ", "차카타파하가나다라!"]),
    )

    frames = await collect(session, "안녕")

    assert [f["seq"] for f in frames] == [0, 1, 2]
    assert frames[-1]["type"] == "turn_end"


async def test_trailing_text_without_boundary_is_flushed():
    session = Session(
        FakeTTS(), None, token_stream=token_stream_from(["마침표가 없는 문장"])
    )

    frames = await collect(session, "안녕")

    assert frames[0]["text"] == "마침표가 없는 문장"


async def test_history_accumulates_both_roles():
    session = Session(
        FakeTTS(), None, token_stream=token_stream_from(["반가워요 오늘도 좋네요."])
    )

    await collect(session, "안녕")

    assert session.history[0]["role"] == "system"
    assert session.history[1] == {"role": "user", "content": "안녕"}
    assert session.history[2]["role"] == "assistant"
    assert session.history[2]["content"] == "반가워요 오늘도 좋네요."


async def test_history_is_trimmed_but_keeps_system():
    session = Session(
        FakeTTS(), None, token_stream=token_stream_from(["짧은 답변입니다 여기까지"])
    )
    session.char_limit = 40

    for _ in range(5):
        await collect(session, "긴 질문을 반복해서 보냅니다")

    assert session.history[0]["role"] == "system"
    body = sum(len(m["content"]) for m in session.history[1:])
    assert body <= 40


async def test_llm_failure_emits_error_frame():
    session = Session(FakeTTS(), None, token_stream=failing_token_stream)

    frames = await collect(session, "안녕")

    assert frames[0]["type"] == "error"
    assert frames[0]["code"] == "LLM_UNAVAILABLE"
