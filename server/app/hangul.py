"""한글 음절을 초성·중성·종성으로 분해한다.

비한글 문자(공백, 숫자, 영문, 기호)는 건너뛴다. 이 프로젝트는 한국어 전용이며
비한글 문자에는 입 모양을 할당하지 않는다.
"""

from dataclasses import dataclass

BASE = 0xAC00
LAST = 0xD7A3

CHOSUNG = [
    "ㄱ", "ㄲ", "ㄴ", "ㄷ", "ㄸ", "ㄹ", "ㅁ", "ㅂ", "ㅃ", "ㅅ",
    "ㅆ", "ㅇ", "ㅈ", "ㅉ", "ㅊ", "ㅋ", "ㅌ", "ㅍ", "ㅎ",
]
JUNGSUNG = [
    "ㅏ", "ㅐ", "ㅑ", "ㅒ", "ㅓ", "ㅔ", "ㅕ", "ㅖ", "ㅗ", "ㅘ",
    "ㅙ", "ㅚ", "ㅛ", "ㅜ", "ㅝ", "ㅞ", "ㅟ", "ㅠ", "ㅡ", "ㅢ", "ㅣ",
]
JONGSUNG = [
    "", "ㄱ", "ㄲ", "ㄳ", "ㄴ", "ㄵ", "ㄶ", "ㄷ", "ㄹ", "ㄺ",
    "ㄻ", "ㄼ", "ㄽ", "ㄾ", "ㄿ", "ㅀ", "ㅁ", "ㅂ", "ㅄ", "ㅅ",
    "ㅆ", "ㅇ", "ㅈ", "ㅊ", "ㅋ", "ㅌ", "ㅍ", "ㅎ",
]


@dataclass(frozen=True)
class Syllable:
    char: str
    onset: str
    nucleus: str
    coda: str


TimedSyllable = tuple[Syllable, int, int]


def decompose(text: str) -> list[Syllable]:
    result: list[Syllable] = []
    for ch in text:
        code = ord(ch)
        if not (BASE <= code <= LAST):
            continue
        offset = code - BASE
        result.append(
            Syllable(
                char=ch,
                onset=CHOSUNG[offset // 588],
                nucleus=JUNGSUNG[(offset % 588) // 28],
                coda=JONGSUNG[offset % 28],
            )
        )
    return result
