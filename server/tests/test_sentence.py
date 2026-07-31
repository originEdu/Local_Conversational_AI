from app.sentence import split_stream


def test_no_boundary_yet():
    assert split_stream("오늘은 날씨가") == ([], "오늘은 날씨가")


def test_single_complete_sentence():
    assert split_stream("오늘은 날씨가 좋네요. 남은") == (
        ["오늘은 날씨가 좋네요."],
        " 남은",
    )


def test_two_sentences_at_once():
    # 각 조각이 최소 길이 10자를 넘어야 개별 문장으로 확정된다
    sentences, rest = split_stream("가나다라마바사아자. 차카타파하가나다라!")
    assert sentences == ["가나다라마바사아자.", "차카타파하가나다라!"]
    assert rest == ""


def test_short_segment_merges_with_next():
    # "응." 은 10자 미만이라 다음 경계까지 합쳐진다
    sentences, rest = split_stream("응. 그래서 어제 뭐 했는데?")
    assert sentences == ["응. 그래서 어제 뭐 했는데?"]
    assert rest == ""


def test_short_segment_alone_is_not_emitted():
    assert split_stream("응. ") == ([], "응. ")


def test_flush_emits_remainder():
    assert split_stream("응. ", flush=True) == (["응."], "")


def test_flush_on_empty_buffer():
    assert split_stream("", flush=True) == ([], "")


def test_flush_ignores_whitespace_only():
    assert split_stream("   ", flush=True) == ([], "")


def test_newline_is_a_boundary():
    sentences, rest = split_stream("첫째 줄입니다 여기까지\n둘째")
    assert sentences == ["첫째 줄입니다 여기까지"]
    assert rest == "둘째"


def test_force_split_at_max_length():
    text = "가" * 130
    sentences, rest = split_stream(text)
    assert len(sentences) == 1
    assert len(sentences[0]) == 120
    assert rest == "가" * 10
