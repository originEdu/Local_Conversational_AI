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


class CtcAligner:
    """wav2vec2 CTC 로그 확률로 음절 시각을 구한다.

    GPU는 XTTS와 Ollama가 쓰므로 CPU에 둔다. 문장 하나에 100~200ms면 충분하다.
    """

    def __init__(self) -> None:
        import torch
        from transformers import Wav2Vec2ForCTC, Wav2Vec2Processor

        from app.config import settings

        self._torch = torch
        self._processor = Wav2Vec2Processor.from_pretrained(settings.aligner_model)
        self._model = Wav2Vec2ForCTC.from_pretrained(settings.aligner_model).eval()
        self._vocab = self._processor.tokenizer.get_vocab()
        self._sample_rate = 16000

    def align(self, result, syllables: list[Syllable]) -> list[TimedSyllable]:
        import torchaudio

        if not syllables:
            return []

        waveform = self._to_model_rate(result)
        tokens = self._tokenize(syllables)

        inputs = self._processor(
            waveform.numpy(), sampling_rate=self._sample_rate, return_tensors="pt"
        )

        with self._torch.inference_mode():
            logits = self._model(inputs.input_values).logits
            log_probs = self._torch.log_softmax(logits, dim=-1)

        try:
            aligned, scores = torchaudio.functional.forced_align(
                log_probs,
                self._torch.tensor([tokens], dtype=self._torch.int32),
                blank=self._vocab.get("<pad>", 0),
            )
            token_spans = torchaudio.functional.merge_tokens(aligned[0], scores[0])
        except Exception as error:
            raise AlignError(f"forced_align 실패: {error}") from error

        if len(token_spans) != len(syllables):
            raise AlignError(
                f"정렬 결과 {len(token_spans)}개가 음절 {len(syllables)}개와 다르다"
            )

        frame_ms = result.duration_ms / log_probs.shape[1]
        return [
            (syllable, int(span.start * frame_ms), int(span.end * frame_ms))
            for syllable, span in zip(syllables, token_spans)
        ]

    def _to_model_rate(self, result):
        import torchaudio

        waveform = self._torch.from_numpy(result.waveform)
        if result.sample_rate != self._sample_rate:
            waveform = torchaudio.functional.resample(
                waveform, result.sample_rate, self._sample_rate
            )
        return waveform

    def _tokenize(self, syllables: list[Syllable]) -> list[int]:
        """음절을 모델 어휘 토큰 하나씩으로 바꾼다."""
        tokens: list[int] = []
        for syllable in syllables:
            token = self._vocab.get(syllable.char)
            if token is None:
                raise AlignError(f"어휘에 없는 음절: {syllable.char}")
            tokens.append(token)
        return tokens
