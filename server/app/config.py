"""환경변수 기반 설정. 기본값은 단일 머신 개발 환경 기준이다."""

import os
from dataclasses import dataclass


@dataclass(frozen=True)
class Settings:
    llm_base_url: str = os.getenv("LLM_BASE_URL", "http://localhost:11434")
    llm_model: str = os.getenv("LLM_MODEL", "gemma3n:e2b")
    xtts_speaker_wav: str = os.getenv("XTTS_SPEAKER_WAV", "models/speaker.wav")
    xtts_device: str = os.getenv("XTTS_DEVICE", "cuda")
    aligner_model: str = os.getenv(
        "ALIGNER_MODEL", "kresnik/wav2vec2-large-xlsr-korean"
    )
    history_char_limit: int = int(os.getenv("HISTORY_CHAR_LIMIT", "6000"))
    system_prompt: str = os.getenv(
        "SYSTEM_PROMPT",
        "너는 사용자와 편하게 대화하는 친구다. 한국어로, 두세 문장 이내로 짧게 답한다.",
    )


settings = Settings()
