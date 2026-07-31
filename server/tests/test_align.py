from app.hangul import decompose
from app.align import uniform_align


def test_divides_duration_evenly():
    syllables = decompose("안녕")
    assert uniform_align(syllables, 200) == [
        (syllables[0], 0, 100),
        (syllables[1], 100, 200),
    ]


def test_last_syllable_ends_exactly_at_duration():
    syllables = decompose("가나다")
    timed = uniform_align(syllables, 100)
    assert timed[-1][2] == 100


def test_spans_are_contiguous():
    syllables = decompose("가나다라마")
    timed = uniform_align(syllables, 333)
    for (_, _, end), (_, start, _) in zip(timed, timed[1:]):
        assert end == start


def test_empty_syllables():
    assert uniform_align([], 500) == []
