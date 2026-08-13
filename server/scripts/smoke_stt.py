"""faster-whisper가 한국어를 실제로 받아 적는지 확인한다.

TTS로 만든 소리를 그대로 되먹인다. 별도 녹음 파일이 필요 없고, 두 모델을 한 프로세스에
같이 올렸을 때 VRAM이 버티는지도 함께 본다. WAV 파일 경로를 인자로 주면 그 파일을 쓴다.

통과 기준:
- 예외 없이 문자열이 나온다
- 빈 문자열이 아니다
- 눈으로 봤을 때 원문과 대체로 같다 (whisper는 구두점과 띄어쓰기를 자기 식으로 쓴다)
"""

import io
import sys
import time
from pathlib import Path

import soundfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app.stt import WhisperEngine  # noqa: E402

TEXT = "안녕하세요. 오늘 하루는 어땠나요?"


def make_wav() -> bytes:
    from app.tts import create_engine

    result = create_engine().synthesize(TEXT)
    buffer = io.BytesIO()
    soundfile.write(
        buffer, result.waveform, result.sample_rate, format="WAV", subtype="PCM_16"
    )
    return buffer.getvalue()


def main() -> int:
    if len(sys.argv) > 1:
        wav = Path(sys.argv[1]).read_bytes()
        expected = "(직접 녹음)"
    else:
        print(f"TTS로 원본을 만든다: {TEXT}")
        wav = make_wav()
        expected = TEXT

    print("whisper 로딩 중...")
    started = time.monotonic()
    engine = WhisperEngine()
    print(f"로딩 {time.monotonic() - started:.1f}초")

    started = time.monotonic()
    text = engine.transcribe(wav)
    elapsed = time.monotonic() - started

    print(f"원문: {expected}")
    print(f"인식: {text}")
    print(f"인식 시간: {elapsed:.2f}초 (WAV {len(wav)}바이트)")

    import torch

    if torch.cuda.is_available():
        print(f"VRAM 최대 점유: {torch.cuda.max_memory_allocated() / 1024**2:.0f}MB")

    if not text:
        print("FAIL: 아무것도 못 알아들었다")
        return 1

    print("PASS: 원문과 대체로 같은지 눈으로 확인한다")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
