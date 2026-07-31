"""문장 하나를 speech 프레임으로 조립한다.

실패해도 대화가 끊기지 않도록 단계별로 폴백한다. TTS가 실패하면 자막만 내보내고,
정렬이 실패하면 균등 분배로 대체한다.
"""

import logging
from typing import Protocol

from app.align import AlignError, uniform_align
from app.hangul import Syllable, TimedSyllable, decompose
from app.tts import SynthesisResult, TTSEngine, TTSError, to_wav_base64
from app.viseme import syllables_to_visemes

logger = logging.getLogger(__name__)


class Aligner(Protocol):
    def align(
        self, result: SynthesisResult, syllables: list[Syllable]
    ) -> list[TimedSyllable]:
        ...


def build_speech_frame(
    seq: int,
    text: str,
    engine: TTSEngine,
    aligner: Aligner | None,
) -> dict:
    try:
        result = engine.synthesize(text)
    except TTSError:
        logger.exception("TTS 실패, 자막만 전송한다: %s", text)
        return {
            "type": "speech",
            "seq": seq,
            "text": text,
            "audioBase64": None,
            "visemes": [],
        }

    syllables = decompose(text)

    timed: list[TimedSyllable] = []
    if syllables:
        if aligner is not None:
            try:
                timed = aligner.align(result, syllables)
            except AlignError:
                logger.exception("정렬 실패, 균등 분배로 대체한다: %s", text)
                timed = uniform_align(syllables, result.duration_ms)
        else:
            timed = uniform_align(syllables, result.duration_ms)

    return {
        "type": "speech",
        "seq": seq,
        "text": text,
        "audioBase64": to_wav_base64(result),
        "visemes": [span.to_dict() for span in syllables_to_visemes(timed)],
    }
