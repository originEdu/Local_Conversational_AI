"""문장을 파형으로 합성한다.

TTSEngine은 프로토콜이다. 파이프라인은 이 인터페이스에만 의존하므로 엔진을 바꿔도
상위 코드는 수정되지 않는다.
"""

import base64
import io
from dataclasses import dataclass
from typing import Protocol

import numpy as np
import soundfile


class TTSError(Exception):
    """음성 합성에 실패했을 때 발생한다."""


@dataclass(frozen=True)
class SynthesisResult:
    waveform: np.ndarray
    sample_rate: int

    @property
    def duration_ms(self) -> int:
        return int(len(self.waveform) * 1000 / self.sample_rate)


class TTSEngine(Protocol):
    def synthesize(self, text: str) -> SynthesisResult:
        ...


def to_wav_base64(result: SynthesisResult) -> str:
    buffer = io.BytesIO()
    soundfile.write(
        buffer,
        result.waveform,
        result.sample_rate,
        format="WAV",
        subtype="PCM_16",
    )
    return base64.b64encode(buffer.getvalue()).decode("ascii")
