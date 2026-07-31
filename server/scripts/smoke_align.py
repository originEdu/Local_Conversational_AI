"""CTC 정렬기가 실제 오디오에 대해 타당한 음절 시각을 내는지 확인한다.

통과 기준 (설계 문서 9절):
- 마지막 음절의 end가 오디오 길이의 ±10% 이내
- 시각이 단조 증가하고 구간이 겹치지 않는다
- 음절 수가 입력 텍스트의 한글 음절 수와 같다
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app.align import CtcAligner  # noqa: E402
from app.hangul import decompose  # noqa: E402
from app.tts import create_engine  # noqa: E402

TEXT = "안녕하세요. 오늘 하루는 어땠나요?"


def main() -> int:
    engine = create_engine(sys.argv[1] if len(sys.argv) > 1 else None)
    result = engine.synthesize(TEXT)
    syllables = decompose(TEXT)
    timed = CtcAligner().align(result, syllables)

    print(f"엔진: {type(engine).__name__}")
    print(f"오디오 길이: {result.duration_ms}ms, 음절 수: {len(syllables)}")
    for syllable, start, end in timed:
        print(f"  {syllable.char}  {start:>5} ~ {end:>5}ms")

    if len(timed) != len(syllables):
        print(f"FAIL: 음절 수 불일치 {len(timed)} != {len(syllables)}")
        return 1

    for (_, _, end), (_, start, _) in zip(timed, timed[1:]):
        if end > start:
            print("FAIL: 구간이 겹친다")
            return 1

    last_end = timed[-1][2]
    tolerance = result.duration_ms * 0.10
    if abs(last_end - result.duration_ms) > tolerance:
        print(f"FAIL: 마지막 end {last_end}ms 가 {result.duration_ms}ms 에서 10% 초과 벗어남")
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
