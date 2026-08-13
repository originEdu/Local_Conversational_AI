"""음성을 문장으로 받아 적는다.

faster-whisper를 쓴다. CTranslate2 런타임이라 transformers와 무관하다. MeloTTS가
transformers==4.27.4를 못박아 --no-deps로 설치돼 있는 환경이라, transformers 기반
whisper를 얹으면 그 핀 충돌에 같이 묶인다.
"""

import io
import logging

logger = logging.getLogger(__name__)


class STTError(Exception):
    """음성 인식에 실패했을 때 발생한다."""


class WhisperEngine:
    """faster-whisper 래퍼.

    무음 구간에서 whisper는 학습 데이터의 자막 상투구를 지어낸다("시청해주셔서
    감사합니다" 등). vad_filter가 무음을 먼저 잘라내 이걸 막는다.

    condition_on_previous_text는 끈다. 켜두면 앞 문장을 조건으로 삼아 짧은 발화에서
    같은 말을 반복하는 루프에 빠진다.
    """

    def __init__(self) -> None:
        from faster_whisper import WhisperModel

        from app.config import settings

        self._model = WhisperModel(
            settings.stt_model,
            device=settings.stt_device,
            compute_type=settings.stt_compute_type,
        )

    def transcribe(self, wav: bytes) -> str:
        """WAV 바이트를 받아 문장을 돌려준다. 말이 없으면 빈 문자열."""
        try:
            segments, _ = self._model.transcribe(
                io.BytesIO(wav),
                language="ko",
                vad_filter=True,
                condition_on_previous_text=False,
            )
            # segments는 제너레이터다. 여기서 소비해야 실제 디코딩이 돈다.
            return "".join(segment.text for segment in segments).strip()
        except Exception as error:
            raise STTError(f"음성 인식 실패: {error}") from error
