"""음절 타임라인을 viseme 타임라인으로 바꾼다.

한국어 입 모양은 중성(모음)이 지배한다. 초성·종성은 기본 viseme을 대체하지 않고
음절 구간의 앞뒤 일부만 덮어쓴다.
"""

from dataclasses import dataclass

from app.hangul import TimedSyllable

SILENCE_GAP_MS = 200
BILABIAL_ONSET_RATIO = 0.25
OTHER_ONSET_RATIO = 0.15
BILABIAL_CODA_RATIO = 0.20

VOWEL_TO_VISEME = {
    "ㅏ": "AA", "ㅐ": "AA", "ㅑ": "AA", "ㅒ": "AA", "ㅘ": "AA",
    "ㅓ": "EH", "ㅔ": "EH", "ㅕ": "EH", "ㅖ": "EH", "ㅙ": "EH",
    "ㅝ": "EH", "ㅞ": "EH",
    "ㅗ": "OH", "ㅛ": "OH",
    "ㅜ": "OU", "ㅠ": "OU", "ㅚ": "OU", "ㅟ": "OU",
    "ㅡ": "EU", "ㅢ": "EU",
    "ㅣ": "IH",
}

ONSET_TO_VISEME = {
    "ㅁ": "PP", "ㅂ": "PP", "ㅃ": "PP", "ㅍ": "PP",
    "ㄴ": "NN", "ㄷ": "NN", "ㄸ": "NN", "ㅌ": "NN", "ㄹ": "NN",
    "ㄱ": "KK", "ㄲ": "KK", "ㅋ": "KK",
    "ㅅ": "SS", "ㅆ": "SS", "ㅈ": "SS", "ㅉ": "SS", "ㅊ": "SS",
}

BILABIAL_CODAS = {"ㅁ", "ㅂ", "ㅍ"}


@dataclass(frozen=True)
class VisemeSpan:
    v: str
    start: int
    end: int

    def to_dict(self) -> dict:
        return {"v": self.v, "start": self.start, "end": self.end}


def _syllable_spans(syllable, start: int, end: int) -> list[VisemeSpan]:
    base = VOWEL_TO_VISEME.get(syllable.nucleus, "AA")
    duration = end - start

    onset_viseme = ONSET_TO_VISEME.get(syllable.onset)
    if onset_viseme == "PP":
        onset_end = start + int(duration * BILABIAL_ONSET_RATIO)
    elif onset_viseme is not None:
        onset_end = start + int(duration * OTHER_ONSET_RATIO)
    else:
        onset_end = start

    if syllable.coda in BILABIAL_CODAS:
        coda_start = end - int(duration * BILABIAL_CODA_RATIO)
    else:
        coda_start = end

    spans = [
        VisemeSpan(onset_viseme, start, onset_end) if onset_viseme else None,
        VisemeSpan(base, onset_end, coda_start),
        VisemeSpan("PP", coda_start, end) if coda_start < end else None,
    ]
    return [s for s in spans if s is not None and s.start < s.end]


def syllables_to_visemes(timed: list[TimedSyllable]) -> list[VisemeSpan]:
    result: list[VisemeSpan] = []
    previous_end: int | None = None

    for syllable, start, end in timed:
        if previous_end is not None and start - previous_end > SILENCE_GAP_MS:
            result.append(VisemeSpan("sil", previous_end, start))
        result.extend(_syllable_spans(syllable, start, end))
        previous_end = end

    return result
