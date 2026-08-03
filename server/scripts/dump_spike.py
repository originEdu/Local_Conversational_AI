"""UE 스파이크용 오디오와 viseme 타임라인을 파일로 뽑는다.

LLM도 WebSocket도 거치지 않는다. 문장을 직접 주고 TTS와 정렬만 돌린다.
UE 블루프린트가 읽을 수 있게 wav와 CSV(DataTable용)를 남긴다.

사용법:
    python scripts/dump_spike.py "오늘은 맑고 따뜻한 날씨네요"
"""

import base64
import sys
from pathlib import Path

from app.align import CtcAligner
from app.pipeline import build_speech_frame
from app.tts import create_engine

OUT_DIR = Path(__file__).resolve().parents[1] / "out"


def main(text: str) -> int:
    frame = build_speech_frame(0, text, create_engine(), CtcAligner())

    if frame["audioBase64"] is None:
        print("TTS 실패. 오디오가 없다")
        return 1

    OUT_DIR.mkdir(exist_ok=True)

    wav_path = OUT_DIR / "spike.wav"
    wav_path.write_bytes(base64.b64decode(frame["audioBase64"]))

    # UE DataTable은 첫 열을 행 이름으로 쓴다. 순서가 유지되도록 0부터 번호를 준다
    csv_path = OUT_DIR / "spike_visemes.csv"
    lines = ["Name,Viseme,StartMs,EndMs"]
    for index, span in enumerate(frame["visemes"]):
        lines.append(f"{index},{span['v']},{span['start']},{span['end']}")
    csv_path.write_text("\n".join(lines), encoding="utf-8")

    last_end = frame["visemes"][-1]["end"] if frame["visemes"] else 0
    print(f"문장: {text}")
    print(f"  {wav_path}")
    print(f"  {csv_path}  (viseme {len(frame['visemes'])}개)")
    print(f"  마지막 viseme 끝: {last_end}ms  <- UE 재생 길이와 비교할 값")
    return 0


if __name__ == "__main__":
    sentence = sys.argv[1] if len(sys.argv) > 1 else "오늘은 맑고 따뜻한 날씨네요"
    raise SystemExit(main(sentence))
