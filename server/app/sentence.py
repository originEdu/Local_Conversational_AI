"""LLM 토큰 스트림 버퍼를 문장 단위로 자른다.

너무 짧은 조각을 개별 TTS로 돌리면 오디오가 부자연스럽게 끊기므로 최소 길이를 둔다.
LLM이 마침표를 찍지 않는 경우에 대비해 최대 길이도 둔다.
"""

BOUNDARY_CHARS = ".?!…\n"
MIN_LENGTH = 10
MAX_LENGTH = 120


def split_stream(buffer: str, *, flush: bool = False) -> tuple[list[str], str]:
    sentences: list[str] = []
    start = 0

    for index, char in enumerate(buffer):
        length = index - start + 1

        is_boundary = char in BOUNDARY_CHARS and length >= MIN_LENGTH
        is_overflow = length >= MAX_LENGTH

        if is_boundary or is_overflow:
            sentences.append(buffer[start : index + 1].strip())
            start = index + 1

    rest = buffer[start:]

    if flush:
        if rest.strip():
            sentences.append(rest.strip())
        rest = ""

    return [s for s in sentences if s], rest
