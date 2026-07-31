"""UE 없이 서버를 검증한다.

통과 기준:
- speech 프레임이 하나 이상 오고 마지막에 turn_end가 온다
- seq가 0부터 1씩 증가한다
- 각 speech 프레임의 viseme 구간이 겹치지 않고 단조 증가한다
- audioBase64가 있으면 디코딩되고 WAV로 읽힌다

사용법:
    python scripts/ws_client.py "오늘 뭐 했어?"
"""

import asyncio
import base64
import io
import json
import sys

import soundfile
from websockets.asyncio.client import connect

URL = "ws://localhost:8000/ws"


def check_frame(frame: dict, expected_seq: int) -> list[str]:
    problems: list[str] = []

    if frame["seq"] != expected_seq:
        problems.append(f"seq가 {expected_seq}가 아니라 {frame['seq']}")

    previous_end = None
    for span in frame["visemes"]:
        if span["start"] >= span["end"]:
            problems.append(f"빈 구간: {span}")
        if previous_end is not None and span["start"] < previous_end:
            problems.append(f"구간 겹침: {span}")
        previous_end = span["end"]

    if frame["audioBase64"] is not None:
        try:
            data, rate = soundfile.read(
                io.BytesIO(base64.b64decode(frame["audioBase64"]))
            )
            print(f"    오디오 {len(data) * 1000 // rate}ms @ {rate}Hz")
        except Exception as error:
            problems.append(f"오디오 디코딩 실패: {error}")
    else:
        print("    오디오 없음 (TTS 폴백)")

    return problems


async def main(text: str) -> int:
    problems: list[str] = []
    speech_count = 0
    saw_turn_end = False

    async with connect(URL) as ws:
        await ws.send(json.dumps({"type": "user_message", "text": text}))

        while True:
            frame = json.loads(await ws.recv())

            if frame["type"] == "error":
                print(f"  error: {frame['code']} — {frame['message']}")
                problems.append(f"error 프레임: {frame['code']}")
                break

            if frame["type"] == "speech":
                print(f"  [{frame['seq']}] {frame['text']}")
                print(f"    viseme {len(frame['visemes'])}개")
                problems.extend(check_frame(frame, speech_count))
                speech_count += 1
                continue

            if frame["type"] == "turn_end":
                print(f"  turn_end (seq={frame['seq']})")
                saw_turn_end = True
                break

    if speech_count == 0:
        problems.append("speech 프레임이 하나도 없다")
    if not saw_turn_end:
        problems.append("turn_end가 오지 않았다")

    if problems:
        print("\nFAIL:")
        for problem in problems:
            print(f"  - {problem}")
        return 1

    print(f"\nPASS: speech {speech_count}개 + turn_end")
    return 0


if __name__ == "__main__":
    question = sys.argv[1] if len(sys.argv) > 1 else "오늘 뭐 했어?"
    raise SystemExit(asyncio.run(main(question)))
