from app.hangul import Syllable
from app.viseme import VisemeSpan, syllables_to_visemes


def test_simple_vowel_covers_whole_span():
    timed = [(Syllable("아", "ㅇ", "ㅏ", ""), 0, 200)]
    assert syllables_to_visemes(timed) == [VisemeSpan("AA", 0, 200)]


def test_bilabial_onset_covers_first_quarter():
    timed = [(Syllable("바", "ㅂ", "ㅏ", ""), 0, 200)]
    assert syllables_to_visemes(timed) == [
        VisemeSpan("PP", 0, 50),
        VisemeSpan("AA", 50, 200),
    ]


def test_bilabial_coda_covers_last_fifth():
    timed = [(Syllable("밤", "ㅂ", "ㅏ", "ㅁ"), 0, 200)]
    assert syllables_to_visemes(timed) == [
        VisemeSpan("PP", 0, 50),
        VisemeSpan("AA", 50, 160),
        VisemeSpan("PP", 160, 200),
    ]


def test_alveolar_onset_covers_first_fifteen_percent():
    timed = [(Syllable("나", "ㄴ", "ㅏ", ""), 0, 200)]
    assert syllables_to_visemes(timed) == [
        VisemeSpan("NN", 0, 30),
        VisemeSpan("AA", 30, 200),
    ]


def test_velar_coda_is_not_overlaid():
    # 받침 ㅇ은 입 모양을 바꾸지 않으므로 덮어쓰지 않는다
    timed = [(Syllable("앙", "ㅇ", "ㅏ", "ㅇ"), 0, 200)]
    assert syllables_to_visemes(timed) == [VisemeSpan("AA", 0, 200)]


def test_gap_over_threshold_becomes_silence():
    timed = [
        (Syllable("아", "ㅇ", "ㅏ", ""), 0, 200),
        (Syllable("이", "ㅇ", "ㅣ", ""), 500, 700),
    ]
    assert syllables_to_visemes(timed) == [
        VisemeSpan("AA", 0, 200),
        VisemeSpan("sil", 200, 500),
        VisemeSpan("IH", 500, 700),
    ]


def test_small_gap_is_not_filled():
    timed = [
        (Syllable("아", "ㅇ", "ㅏ", ""), 0, 200),
        (Syllable("이", "ㅇ", "ㅣ", ""), 300, 500),
    ]
    assert syllables_to_visemes(timed) == [
        VisemeSpan("AA", 0, 200),
        VisemeSpan("IH", 300, 500),
    ]


def test_compound_vowel_maps_to_dominant_shape():
    timed = [(Syllable("와", "ㅇ", "ㅘ", ""), 0, 100)]
    assert syllables_to_visemes(timed) == [VisemeSpan("AA", 0, 100)]


def test_spans_are_contiguous_and_monotonic():
    timed = [
        (Syllable("밤", "ㅂ", "ㅏ", "ㅁ"), 0, 200),
        (Syllable("나", "ㄴ", "ㅏ", ""), 200, 400),
    ]
    spans = syllables_to_visemes(timed)
    for a, b in zip(spans, spans[1:]):
        assert a.end <= b.start
        assert a.start < a.end


def test_empty_input():
    assert syllables_to_visemes([]) == []


def test_to_dict_matches_contract():
    assert VisemeSpan("AA", 0, 140).to_dict() == {"v": "AA", "start": 0, "end": 140}
