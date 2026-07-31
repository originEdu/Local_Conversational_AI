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


class XttsEngine:
    """XTTS-v2 래퍼.

    fp32로 로드한다. `tts_model.half()`를 부르면 speaker encoder까지 fp16이 되는데
    입력 파형은 fp32로 들어오므로 conv1d에서 타입 불일치로 죽는다.
    ("Input type (torch.cuda.FloatTensor) and weight type (torch.cuda.HalfTensor)")
    VRAM 실측치는 설계 문서 7절 참조.
    """

    MODEL_NAME = "tts_models/multilingual/multi-dataset/xtts_v2"

    def __init__(self) -> None:
        from TTS.api import TTS

        from app.config import settings

        self._speaker_wav = settings.xtts_speaker_wav
        self._tts = TTS(self.MODEL_NAME).to(settings.xtts_device)
        self._sample_rate = self._tts.synthesizer.output_sample_rate

    def synthesize(self, text: str) -> SynthesisResult:
        try:
            samples = self._tts.tts(
                text=text,
                speaker_wav=self._speaker_wav,
                language="ko",
            )
        except Exception as error:
            raise TTSError(f"XTTS 합성 실패: {error}") from error

        waveform = np.asarray(samples, dtype=np.float32)
        if waveform.size == 0:
            raise TTSError("XTTS가 빈 파형을 반환했다")

        return SynthesisResult(waveform, self._sample_rate)
