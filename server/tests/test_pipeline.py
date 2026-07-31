import numpy as np

from app.align import AlignError, uniform_align
from app.hangul import TimedSyllable
from app.pipeline import build_speech_frame
from app.tts import SynthesisResult, TTSError


class FakeTTS:
    def __init__(self, duration_ms: int = 200):
        self.sample_rate = 16000
        self.samples = duration_ms * self.sample_rate // 1000

    def synthesize(self, text: str) -> SynthesisResult:
        return SynthesisResult(
            np.zeros(self.samples, dtype=np.float32), self.sample_rate
        )


class FailingTTS:
    def synthesize(self, text: str) -> SynthesisResult:
        raise TTSError("모델 로드 실패")


class FailingAligner:
    def align(self, result, syllables) -> list[TimedSyllable]:
        raise AlignError("CTC 정렬 실패")


class WorkingAligner:
    """정상 동작하는 가짜 정렬기. 균등 분배 결과를 그대로 돌려준다."""

    def align(self, result, syllables) -> list[TimedSyllable]:
        return uniform_align(syllables, result.duration_ms)


def test_frame_matches_contract():
    frame = build_speech_frame(0, "안녕", FakeTTS(), WorkingAligner())

    assert frame["type"] == "speech"
    assert frame["seq"] == 0
    assert frame["text"] == "안녕"
    assert isinstance(frame["audioBase64"], str)
    assert frame["visemes"][0].keys() == {"v", "start", "end"}


def test_visemes_span_full_audio():
    frame = build_speech_frame(0, "안녕", FakeTTS(duration_ms=200), WorkingAligner())
    assert frame["visemes"][-1]["end"] == 200


def test_tts_failure_keeps_text_only():
    frame = build_speech_frame(3, "안녕", FailingTTS(), WorkingAligner())

    assert frame["type"] == "speech"
    assert frame["seq"] == 3
    assert frame["text"] == "안녕"
    assert frame["audioBase64"] is None
    assert frame["visemes"] == []


def test_align_failure_falls_back_to_uniform():
    frame = build_speech_frame(0, "안녕", FakeTTS(duration_ms=200), FailingAligner())

    assert frame["audioBase64"] is not None
    assert frame["visemes"][-1]["end"] == 200


def test_no_aligner_uses_uniform():
    frame = build_speech_frame(0, "안녕", FakeTTS(duration_ms=200), None)
    assert frame["visemes"][-1]["end"] == 200


def test_text_without_hangul_produces_no_visemes():
    frame = build_speech_frame(0, "abc 123", FakeTTS(), WorkingAligner())

    assert frame["audioBase64"] is not None
    assert frame["visemes"] == []
