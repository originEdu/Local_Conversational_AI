import base64
import io

import numpy as np
import soundfile

from app.tts import SynthesisResult, to_wav_base64


def test_duration_ms_from_sample_count():
    waveform = np.zeros(16000, dtype=np.float32)
    assert SynthesisResult(waveform, 16000).duration_ms == 1000


def test_duration_ms_non_integer_seconds():
    waveform = np.zeros(24000, dtype=np.float32)
    assert SynthesisResult(waveform, 16000).duration_ms == 1500


def test_wav_base64_round_trip():
    waveform = np.linspace(-0.5, 0.5, 16000, dtype=np.float32)
    encoded = to_wav_base64(SynthesisResult(waveform, 16000))

    decoded, sample_rate = soundfile.read(io.BytesIO(base64.b64decode(encoded)))

    assert sample_rate == 16000
    assert len(decoded) == 16000
    assert np.max(np.abs(decoded - waveform)) < 0.001
