"""환경변수 기반 설정. 기본값은 단일 머신 개발 환경 기준이다."""

import os
from dataclasses import dataclass


@dataclass(frozen=True)
class Settings:
    llm_base_url: str = os.getenv("LLM_BASE_URL", "http://localhost:11434")
    llm_model: str = os.getenv("LLM_MODEL", "gemma3n:e2b")
    # "ollama" 또는 "openai". llama.cpp·vLLM처럼 OpenAI 호환 서버는 "openai".
    llm_api: str = os.getenv("LLM_API", "ollama")
    tts_engine: str = os.getenv("TTS_ENGINE", "melo")
    xtts_speaker_wav: str = os.getenv("XTTS_SPEAKER_WAV", "models/speaker.wav")
    xtts_device: str = os.getenv("XTTS_DEVICE", "cuda")
    aligner_model: str = os.getenv(
        "ALIGNER_MODEL", "kresnik/wav2vec2-large-xlsr-korean"
    )
    stt_model: str = os.getenv("STT_MODEL", "large-v3-turbo")
    stt_device: str = os.getenv("STT_DEVICE", "cuda")
    # int8_float16은 VRAM을 절반 가까이 줄이면서 한국어 정확도 차이가 거의 없다.
    # CPU로 돌릴 거면 "int8"로 바꿔야 한다.
    stt_compute_type: str = os.getenv("STT_COMPUTE_TYPE", "int8_float16")
    history_char_limit: int = int(os.getenv("HISTORY_CHAR_LIMIT", "6000"))
    # "친구다"만으로는 부족했다. 사용자가 존댓말로 말하면 모델이 따라가고, 한번
    # 넘어가면 "어떻게 도와드릴까요"로 굳는다. 반말과 상담원 말투 금지를 명시한다.
    system_prompt: str = os.getenv(
        "SYSTEM_PROMPT",
        "너는 사용자와 편하게 대화하는 친구다. 한국어 반말로만 답한다. "
        "사용자가 존댓말을 써도 반말을 유지한다. "
        "'도와드릴까요', '말씀해 주세요' 같은 상담원 말투를 쓰지 않는다. "
        "두세 문장 이내로 짧게 답한다.",
    )


settings = Settings()
