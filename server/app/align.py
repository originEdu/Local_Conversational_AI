"""오디오와 텍스트를 정렬해 음절별 시각을 구한다.

uniform_align은 폴백이다. 오디오 길이를 음절 수로 균등 분배할 뿐 실제 발음 길이를
반영하지 않지만, CTC 정렬이 실패해도 대화가 멈추지 않게 한다.
"""

from app.hangul import Syllable, TimedSyllable


class AlignError(Exception):
    """강제 정렬에 실패했을 때 발생한다."""


def uniform_align(syllables: list[Syllable], duration_ms: int) -> list[TimedSyllable]:
    if not syllables:
        return []

    count = len(syllables)
    result: list[TimedSyllable] = []
    for index, syllable in enumerate(syllables):
        start = duration_ms * index // count
        end = duration_ms * (index + 1) // count
        result.append((syllable, start, end))
    return result
