"""XTTS 엔진이 실제로 소리를 만드는지 확인한다.

통과 기준:
- 예외 없이 SynthesisResult가 나온다
- duration_ms가 500 이상이다 (한 문장이 0.5초 미만일 수 없다)
- 파형에 무음이 아닌 구간이 있다
- out/smoke.wav를 귀로 들었을 때 한국어로 들린다
"""

import sys
from pathlib import Path

import numpy as np
import soundfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app.tts import create_engine  # noqa: E402

TEXT = "안녕하세요. 오늘 하루는 어땠나요?"


def main() -> int:
    name = sys.argv[1] if len(sys.argv) > 1 else None
    engine = create_engine(name)
    label = type(engine).__name__
    print(f"엔진: {label}")

    result = engine.synthesize(TEXT)

    print(f"길이: {result.duration_ms}ms, 샘플레이트: {result.sample_rate}")
    print(f"최대 진폭: {np.max(np.abs(result.waveform)):.4f}")

    import torch

    if torch.cuda.is_available():
        peak_mb = torch.cuda.max_memory_allocated() / 1024**2
        print(f"VRAM 최대 점유: {peak_mb:.0f}MB")

    if result.duration_ms < 500:
        print("FAIL: 오디오가 너무 짧다")
        return 1
    if np.max(np.abs(result.waveform)) < 0.01:
        print("FAIL: 무음이다")
        return 1

    out = Path(__file__).resolve().parents[1] / "out"
    out.mkdir(exist_ok=True)
    path = out / f"smoke_{label.replace('Engine', '').lower()}.wav"
    soundfile.write(path, result.waveform, result.sample_rate)

    print(f"PASS: {path} 를 들어보고 한국어로 들리는지 확인한다")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
