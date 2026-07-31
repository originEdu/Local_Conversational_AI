from app.hangul import Syllable, decompose


def test_decompose_syllable_with_coda():
    assert decompose("밤") == [Syllable("밤", "ㅂ", "ㅏ", "ㅁ")]


def test_decompose_syllable_without_coda():
    assert decompose("가") == [Syllable("가", "ㄱ", "ㅏ", "")]


def test_decompose_multiple_syllables():
    assert decompose("안녕") == [
        Syllable("안", "ㅇ", "ㅏ", "ㄴ"),
        Syllable("녕", "ㄴ", "ㅕ", "ㅇ"),
    ]


def test_skips_non_hangul():
    assert decompose("안 녕! abc 3") == [
        Syllable("안", "ㅇ", "ㅏ", "ㄴ"),
        Syllable("녕", "ㄴ", "ㅕ", "ㅇ"),
    ]


def test_empty_string():
    assert decompose("") == []
