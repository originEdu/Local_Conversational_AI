# Local Conversational AI — 백엔드 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 사용자 텍스트를 받아 로컬 LLM 응답을 문장 단위로 생성하고, 각 문장의 음성과 viseme 타임라인을 WebSocket으로 스트리밍하는 FastAPI 서버를 만든다.

**Architecture:** 단일 FastAPI 프로세스. WebSocket 핸들러가 세션 히스토리를 들고 Ollama를 스트리밍 호출하며, 토큰을 문장 단위로 잘라 TTS → 강제 정렬 → viseme 매핑 파이프라인에 넘겨 프레임을 조립해 내보낸다. 순수 함수(한글 분해, viseme 매핑, 문장 분할)를 모델 의존 코드와 분리해 대부분의 로직을 모델 없이 테스트한다.

**Tech Stack:** Python 3.11, FastAPI, uvicorn, httpx, Ollama(`gemma3n:e2b`), coqui-tts(XTTS-v2), torch/torchaudio(wav2vec2 CTC), pytest, pytest-asyncio

## Global Constraints

- Python **3.11** 고정. coqui-tts가 3.13을 지원하지 않는다.
- 대화 언어는 **한국어 전용**. 비한글 문자(영문, 숫자, 기호)는 viseme을 생성하지 않고 건너뛴다.
- GPU VRAM **8GB**. XTTS는 반드시 `half=True`로 로드하고, wav2vec2 정렬기는 **CPU**에 둔다.
- LLM 모델은 **`gemma3n:e2b`**, LLM은 **로컬 Ollama** (`http://localhost:11434`, 환경변수로 교체 가능).
- WebSocket 프레임 스키마는 설계 문서 5절 계약을 그대로 따른다. 필드명 변경 금지:
  - 수신: `{"type": "user_message", "text": str}`
  - 송신(발화): `{"type": "speech", "seq": int, "text": str, "audioBase64": str|None, "visemes": [{"v": str, "start": int, "end": int}]}`
  - 송신(턴 종료): `{"type": "turn_end", "seq": int}`
  - 송신(오류): `{"type": "error", "code": str, "message": str}`
- `start`/`end`는 **해당 문장 오디오 시작 기준 밀리초 정수**.
- 어떤 실패에서도 대화는 끊기지 않는다. 텍스트가 최소 보장선이다.
- 모든 작업 디렉터리는 `server/`. UE 클라이언트(`UE5_Client/`)는 이번 계획의 범위 밖이다.

## LLM 서버 주소

기본은 **같은 머신의 Ollama** (`http://localhost:11434`)다.

주소는 코드에 하드코딩하지 않는다. `app/config.py`의 `llm_base_url` 한 곳만 보며, 환경변수 `LLM_BASE_URL`로 언제든 덮어쓸 수 있다.

```bash
# 로컬 Ollama (기본값)
set LLM_BASE_URL=http://localhost:11434

# 다른 머신의 Ollama로 전환
set LLM_BASE_URL=http://192.168.3.26:11434
```

또는 `server/.env`에 적어둔다. `.env`는 `.gitignore`에 이미 들어 있다.

**원격으로 바꿀 때 확인할 것:** 방화벽에서 포트가 열려 있어야 하고, Ollama가 `OLLAMA_HOST=0.0.0.0`으로 떠 있어야 외부에서 접속된다.

**주의 — 주소만 바꿔서는 안 되는 경우가 있다.** `192.168.3.26:8080`에 떠 있는 llama.cpp 서버처럼 **OpenAI 호환 API**를 쓰는 대상으로 옮기려면 주소뿐 아니라 `app/llm.py` 구현을 바꿔야 한다. 경로가 `/api/chat`이 아니라 `/v1/chat/completions`이고, 응답도 줄 단위 JSON이 아니라 SSE(`data: {...}`)이며 토큰이 `choices[0].delta.content`에 들어 있다. 이 계획은 Ollama 형식만 구현한다.

## 명세에서 확정한 사항

설계 문서 6.3절에 모호하거나 빠진 부분이 있어 아래와 같이 확정한다.

1. **받침 ㅇ** — 6.3절 표는 `KK` 트리거에 "받침 ㅇ"을 넣었으나 추가 규칙에는 종성 덮어쓰기가 ㅁ/ㅂ/ㅍ뿐이다. 충돌이므로 **받침 ㅇ은 덮어쓰지 않는다**. [ŋ]은 입 모양을 바꾸지 않는다.
2. **복합 모음** — 6.3절에 ㅘ ㅙ ㅚ ㅝ ㅞ ㅟ가 없다. 지배 모음 기준으로 매핑한다: ㅘ→`AA`, ㅙ→`EH`, ㅚ→`OU`, ㅝ→`EH`, ㅞ→`EH`, ㅟ→`OU`.
3. **히스토리 상한** — "토큰 초과 시 오래된 턴 제거"를 토큰 대신 **문자 수 6000자**로 근사한다. 토크나이저 의존을 추가하지 않기 위함이다.
4. **모듈 분리** — 설계 문서 4.1절은 문장 분할을 `session.py`에 뒀으나, 순수 함수라 단독 테스트가 쉬우므로 `sentence.py`로 분리한다. 같은 이유로 한글 자모 분해를 `hangul.py`로, 프레임 조립을 `pipeline.py`로 분리한다.

---

## File Structure

| 파일 | 책임 | 모델 의존 |
|---|---|---|
| `server/app/config.py` | 환경변수 기반 설정값 | 없음 |
| `server/app/hangul.py` | 한글 음절 → 초성/중성/종성 분해 | 없음 |
| `server/app/viseme.py` | 시간이 붙은 음절 → viseme 타임라인 | 없음 |
| `server/app/sentence.py` | 토큰 스트림 버퍼 → 완성된 문장 목록 | 없음 |
| `server/app/align.py` | 균등 분배 폴백 + wav2vec2 CTC 정렬 | torch (CPU) |
| `server/app/tts.py` | 문장 → 파형. XTTS 래퍼 + 프로토콜 정의 | XTTS (GPU) |
| `server/app/llm.py` | Ollama 스트리밍 호출 | 없음 (HTTP) |
| `server/app/pipeline.py` | 문장 → speech 프레임 조립, 실패 폴백 | 주입받음 |
| `server/app/session.py` | 히스토리 관리, 턴 오케스트레이션 | 주입받음 |
| `server/app/main.py` | FastAPI 앱, WebSocket 엔드포인트 | 주입받음 |
| `server/scripts/ws_client.py` | UE 없이 프레임을 검증하는 수동 클라이언트 | 없음 |

의존 방향은 한 방향이다. `main` → `session` → `pipeline` → (`tts`, `align`, `viseme` → `hangul`). 순수 모듈은 아무것도 import하지 않는다.

---

### Task 1: 프로젝트 스캐폴딩 + 한글 음절 분해

**Files:**
- Create: `server/requirements.txt`
- Create: `server/pytest.ini`
- Create: `server/app/__init__.py`
- Create: `server/app/hangul.py`
- Create: `server/tests/__init__.py`
- Test: `server/tests/test_hangul.py`

**Interfaces:**
- Consumes: 없음 (첫 작업)
- Produces:
  - `hangul.Syllable` — frozen dataclass, 필드 `char: str`, `onset: str`, `nucleus: str`, `coda: str`
  - `hangul.TimedSyllable` — 타입 별칭 `tuple[Syllable, int, int]` (음절, 시작ms, 끝ms)
  - `hangul.decompose(text: str) -> list[Syllable]`

- [ ] **Step 1: 의존성 파일 작성**

`server/requirements.txt`:

```
fastapi==0.115.6
uvicorn[standard]==0.34.0
httpx==0.28.1
numpy==1.26.4
soundfile==0.12.1
pytest==8.3.4
pytest-asyncio==0.25.0
```

torch, torchaudio, coqui-tts는 Task 9·10에서 추가한다. 무거워서 먼저 넣으면 초반 반복이 느려진다.

`server/pytest.ini`:

```ini
[pytest]
testpaths = tests
asyncio_mode = auto
```

- [ ] **Step 2: 가상환경 생성 및 설치**

```bash
cd server && python -m venv .venv && .venv/Scripts/pip install -r requirements.txt
```

- [ ] **Step 3: 실패하는 테스트 작성**

`server/tests/test_hangul.py`:

```python
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
```

- [ ] **Step 4: 테스트 실패 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_hangul.py -v
```

Expected: FAIL — `ModuleNotFoundError: No module named 'app.hangul'`

- [ ] **Step 5: 구현**

`server/app/__init__.py`와 `server/tests/__init__.py`는 빈 파일로 만든다.

`server/app/hangul.py`:

```python
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
```

- [ ] **Step 6: 테스트 통과 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_hangul.py -v
```

Expected: PASS (5개)

- [ ] **Step 7: 커밋**

```bash
git add server/requirements.txt server/pytest.ini server/app server/tests && git commit -m "feat(server): 한글 음절 분해 모듈 추가"
```

---

### Task 2: viseme 매핑

**Files:**
- Create: `server/app/viseme.py`
- Test: `server/tests/test_viseme.py`

**Interfaces:**
- Consumes: `hangul.Syllable`, `hangul.TimedSyllable`
- Produces:
  - `viseme.VisemeSpan` — frozen dataclass, 필드 `v: str`, `start: int`, `end: int`, 메서드 `to_dict() -> dict`
  - `viseme.syllables_to_visemes(timed: list[TimedSyllable]) -> list[VisemeSpan]`

**규칙 요약** (설계 문서 6.3절 + 위 "확정한 사항"):

- 중성이 음절 전체의 기본 viseme을 정한다.
- 초성 ㅁㅂㅃㅍ → 앞 **25%**를 `PP`로 덮는다.
- 초성 ㄴㄷㄸㅌㄹ → 앞 **15%**를 `NN`으로. ㄱㄲㅋ → `KK`. ㅅㅆㅈㅉㅊ → `SS`.
- 초성 ㅇ, ㅎ → 덮어쓰지 않는다.
- 종성 ㅁㅂㅍ → 뒤 **20%**를 `PP`로 덮는다.
- 음절 사이 간격이 **200ms 초과**면 그 사이를 `sil`로 채운다.
- 덮어쓰기로 길이가 0이 되는 구간은 버린다(아주 짧은 음절에서 발생).

- [ ] **Step 1: 실패하는 테스트 작성**

`server/tests/test_viseme.py`:

```python
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
```

- [ ] **Step 2: 테스트 실패 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_viseme.py -v
```

Expected: FAIL — `ModuleNotFoundError: No module named 'app.viseme'`

- [ ] **Step 3: 구현**

`server/app/viseme.py`:

```python
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
```

- [ ] **Step 4: 테스트 통과 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_viseme.py -v
```

Expected: PASS (11개)

- [ ] **Step 5: 커밋**

```bash
git add server/app/viseme.py server/tests/test_viseme.py && git commit -m "feat(server): 음절 기반 viseme 매핑 추가"
```

---

### Task 3: 문장 경계 분할

**Files:**
- Create: `server/app/sentence.py`
- Test: `server/tests/test_sentence.py`

**Interfaces:**
- Consumes: 없음
- Produces: `sentence.split_stream(buffer: str, *, flush: bool = False) -> tuple[list[str], str]` — 완성된 문장 목록과 남은 버퍼를 돌려준다. `flush=True`면 남은 버퍼를 마지막 문장으로 확정한다.

**규칙** (설계 문서 6.2절):

- 경계 문자: `.` `?` `!` `…` 줄바꿈
- 최소 길이 **10자**. 그보다 짧으면 다음 경계까지 합친다.
- 최대 길이 **120자**. 경계 없이 길어지면 강제로 자른다.

- [ ] **Step 1: 실패하는 테스트 작성**

`server/tests/test_sentence.py`:

```python
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
```

- [ ] **Step 2: 테스트 실패 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_sentence.py -v
```

Expected: FAIL — `ModuleNotFoundError: No module named 'app.sentence'`

- [ ] **Step 3: 구현**

`server/app/sentence.py`:

```python
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

    if flush and rest.strip():
        sentences.append(rest.strip())
        rest = ""

    return [s for s in sentences if s], rest
```

- [ ] **Step 4: 테스트 통과 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_sentence.py -v
```

Expected: PASS (10개)

줄바꿈 경계 테스트는 `.strip()` 덕분에 `\n`이 제거된 결과가 나온다.

- [ ] **Step 5: 커밋**

```bash
git add server/app/sentence.py server/tests/test_sentence.py && git commit -m "feat(server): 문장 경계 분할 추가"
```

---

### Task 4: 균등 분배 정렬 폴백

**Files:**
- Create: `server/app/align.py`
- Test: `server/tests/test_align.py`

**Interfaces:**
- Consumes: `hangul.Syllable`, `hangul.TimedSyllable`
- Produces:
  - `align.AlignError` — 정렬 실패 예외
  - `align.uniform_align(syllables: list[Syllable], duration_ms: int) -> list[TimedSyllable]`

CTC 정렬기는 Task 10에서 같은 파일에 추가한다. 폴백을 먼저 만들면 무거운 모델 없이 파이프라인 전체를 돌려볼 수 있다.

- [ ] **Step 1: 실패하는 테스트 작성**

`server/tests/test_align.py`:

```python
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
```

- [ ] **Step 2: 테스트 실패 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_align.py -v
```

Expected: FAIL — `ImportError: cannot import name 'uniform_align'`

- [ ] **Step 3: 구현**

`server/app/align.py`:

```python
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
```

- [ ] **Step 4: 테스트 통과 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_align.py -v
```

Expected: PASS (4개)

- [ ] **Step 5: 커밋**

```bash
git add server/app/align.py server/tests/test_align.py && git commit -m "feat(server): 균등 분배 정렬 폴백 추가"
```

---

### Task 5: TTS 프로토콜 + 설정

**Files:**
- Create: `server/app/tts.py`
- Create: `server/app/config.py`
- Test: `server/tests/test_tts.py`

**Interfaces:**
- Consumes: 없음
- Produces:
  - `tts.TTSError` — 합성 실패 예외
  - `tts.SynthesisResult` — frozen dataclass, 필드 `waveform: numpy.ndarray` (float32 모노), `sample_rate: int`, 프로퍼티 `duration_ms: int`
  - `tts.TTSEngine` — Protocol, 메서드 `synthesize(self, text: str) -> SynthesisResult`
  - `tts.to_wav_base64(result: SynthesisResult) -> str` — PCM16 WAV 컨테이너로 인코딩한 base64 문자열
  - `config.Settings` — 필드는 아래 구현 참조. `config.settings` 싱글턴 인스턴스

실제 XTTS 구현체는 Task 9에서 같은 파일에 추가한다. 지금은 프로토콜과 인코딩만 만들어 파이프라인이 가짜 엔진으로 돌아가게 한다.

- [ ] **Step 1: 실패하는 테스트 작성**

`server/tests/test_tts.py`:

```python
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
```

- [ ] **Step 2: 테스트 실패 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_tts.py -v
```

Expected: FAIL — `ModuleNotFoundError: No module named 'app.tts'`

- [ ] **Step 3: 구현**

`server/app/config.py`:

```python
"""환경변수 기반 설정. 기본값은 단일 머신 개발 환경 기준이다."""

import os
from dataclasses import dataclass


@dataclass(frozen=True)
class Settings:
    llm_base_url: str = os.getenv("LLM_BASE_URL", "http://localhost:11434")
    llm_model: str = os.getenv("LLM_MODEL", "gemma3n:e2b")
    xtts_speaker_wav: str = os.getenv("XTTS_SPEAKER_WAV", "models/speaker.wav")
    xtts_device: str = os.getenv("XTTS_DEVICE", "cuda")
    aligner_model: str = os.getenv(
        "ALIGNER_MODEL", "kresnik/wav2vec2-large-xlsr-korean"
    )
    history_char_limit: int = int(os.getenv("HISTORY_CHAR_LIMIT", "6000"))
    system_prompt: str = os.getenv(
        "SYSTEM_PROMPT",
        "너는 사용자와 편하게 대화하는 친구다. 한국어로, 두세 문장 이내로 짧게 답한다.",
    )


settings = Settings()
```

`server/app/tts.py`:

```python
"""문장을 파형으로 합성한다.

TTSEngine은 프로토콜이다. 파이프라인은 이 인터페이스에만 의존하므로 엔진을 바꿔도
상위 코드는 수정되지 않는다.
"""

import base64
import io
from dataclasses import dataclass
from typing import Protocol

import numpy as np
import soundfile


class TTSError(Exception):
    """음성 합성에 실패했을 때 발생한다."""


@dataclass(frozen=True)
class SynthesisResult:
    waveform: np.ndarray
    sample_rate: int

    @property
    def duration_ms(self) -> int:
        return int(len(self.waveform) * 1000 / self.sample_rate)


class TTSEngine(Protocol):
    def synthesize(self, text: str) -> SynthesisResult:
        ...


def to_wav_base64(result: SynthesisResult) -> str:
    buffer = io.BytesIO()
    soundfile.write(
        buffer,
        result.waveform,
        result.sample_rate,
        format="WAV",
        subtype="PCM_16",
    )
    return base64.b64encode(buffer.getvalue()).decode("ascii")
```

- [ ] **Step 4: 테스트 통과 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_tts.py -v
```

Expected: PASS (3개)

- [ ] **Step 5: 커밋**

```bash
git add server/app/tts.py server/app/config.py server/tests/test_tts.py && git commit -m "feat(server): TTS 프로토콜과 WAV 인코딩 추가"
```

---

### Task 6: 프레임 조립 파이프라인 + 실패 폴백

**Files:**
- Create: `server/app/pipeline.py`
- Test: `server/tests/test_pipeline.py`

**Interfaces:**
- Consumes: `tts.TTSEngine`, `tts.TTSError`, `tts.SynthesisResult`, `tts.to_wav_base64`, `align.uniform_align`, `align.AlignError`, `viseme.syllables_to_visemes`, `hangul.decompose`
- Produces:
  - `pipeline.Aligner` — Protocol, 메서드 `align(self, result: SynthesisResult, syllables: list[Syllable]) -> list[TimedSyllable]`
  - `pipeline.build_speech_frame(seq: int, text: str, engine: TTSEngine, aligner: Aligner | None) -> dict`

**폴백 규칙** (설계 문서 8절):

| 상황 | 결과 |
|---|---|
| 정상 | `audioBase64` 채워짐, `visemes` 채워짐 |
| TTS 실패 (`TTSError`) | `audioBase64: None`, `visemes: []` — 자막만 나감 |
| 정렬 실패 (`AlignError`) 또는 `aligner=None` | `audioBase64` 채워짐, `visemes`는 균등 분배 결과 |

- [ ] **Step 1: 실패하는 테스트 작성**

`server/tests/test_pipeline.py`:

```python
import numpy as np

from app.align import AlignError, uniform_align
from app.hangul import Syllable, TimedSyllable
from app.pipeline import build_speech_frame
from app.tts import SynthesisResult, TTSError


class FakeTTS:
    def __init__(self, duration_ms: int = 200):
        self.sample_rate = 16000
        self.samples = duration_ms * self.sample_rate // 1000

    def synthesize(self, text: str) -> SynthesisResult:
        return SynthesisResult(
            np.zeros(self.samples, dtype=np.float32), self.sample_rate
        )


class FailingTTS:
    def synthesize(self, text: str) -> SynthesisResult:
        raise TTSError("모델 로드 실패")


class FailingAligner:
    def align(self, result, syllables) -> list[TimedSyllable]:
        raise AlignError("CTC 정렬 실패")


class WorkingAligner:
    """정상 동작하는 가짜 정렬기. 균등 분배 결과를 그대로 돌려준다."""

    def align(self, result, syllables) -> list[TimedSyllable]:
        return uniform_align(syllables, result.duration_ms)


def test_frame_matches_contract():
    frame = build_speech_frame(0, "안녕", FakeTTS(), WorkingAligner())

    assert frame["type"] == "speech"
    assert frame["seq"] == 0
    assert frame["text"] == "안녕"
    assert isinstance(frame["audioBase64"], str)
    assert frame["visemes"][0].keys() == {"v", "start", "end"}


def test_visemes_span_full_audio():
    frame = build_speech_frame(0, "안녕", FakeTTS(duration_ms=200), WorkingAligner())
    assert frame["visemes"][-1]["end"] == 200


def test_tts_failure_keeps_text_only():
    frame = build_speech_frame(3, "안녕", FailingTTS(), WorkingAligner())

    assert frame["type"] == "speech"
    assert frame["seq"] == 3
    assert frame["text"] == "안녕"
    assert frame["audioBase64"] is None
    assert frame["visemes"] == []


def test_align_failure_falls_back_to_uniform():
    frame = build_speech_frame(0, "안녕", FakeTTS(duration_ms=200), FailingAligner())

    assert frame["audioBase64"] is not None
    assert frame["visemes"][-1]["end"] == 200


def test_no_aligner_uses_uniform():
    frame = build_speech_frame(0, "안녕", FakeTTS(duration_ms=200), None)
    assert frame["visemes"][-1]["end"] == 200


def test_text_without_hangul_produces_no_visemes():
    frame = build_speech_frame(0, "abc 123", FakeTTS(), WorkingAligner())

    assert frame["audioBase64"] is not None
    assert frame["visemes"] == []
```

- [ ] **Step 2: 테스트 실패 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_pipeline.py -v
```

Expected: FAIL — `ModuleNotFoundError: No module named 'app.pipeline'`

- [ ] **Step 3: 구현**

`server/app/pipeline.py`:

```python
"""문장 하나를 speech 프레임으로 조립한다.

실패해도 대화가 끊기지 않도록 단계별로 폴백한다. TTS가 실패하면 자막만 내보내고,
정렬이 실패하면 균등 분배로 대체한다.
"""

import logging
from typing import Protocol

from app.align import AlignError, uniform_align
from app.hangul import Syllable, TimedSyllable, decompose
from app.tts import SynthesisResult, TTSEngine, TTSError, to_wav_base64
from app.viseme import syllables_to_visemes

logger = logging.getLogger(__name__)


class Aligner(Protocol):
    def align(
        self, result: SynthesisResult, syllables: list[Syllable]
    ) -> list[TimedSyllable]:
        ...


def build_speech_frame(
    seq: int,
    text: str,
    engine: TTSEngine,
    aligner: Aligner | None,
) -> dict:
    try:
        result = engine.synthesize(text)
    except TTSError:
        logger.exception("TTS 실패, 자막만 전송한다: %s", text)
        return {
            "type": "speech",
            "seq": seq,
            "text": text,
            "audioBase64": None,
            "visemes": [],
        }

    syllables = decompose(text)

    timed: list[TimedSyllable] = []
    if syllables:
        if aligner is not None:
            try:
                timed = aligner.align(result, syllables)
            except AlignError:
                logger.exception("정렬 실패, 균등 분배로 대체한다: %s", text)
                timed = uniform_align(syllables, result.duration_ms)
        else:
            timed = uniform_align(syllables, result.duration_ms)

    return {
        "type": "speech",
        "seq": seq,
        "text": text,
        "audioBase64": to_wav_base64(result),
        "visemes": [span.to_dict() for span in syllables_to_visemes(timed)],
    }
```

- [ ] **Step 4: 테스트 통과 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_pipeline.py -v
```

Expected: PASS (6개)

- [ ] **Step 5: 커밋**

```bash
git add server/app/pipeline.py server/tests/test_pipeline.py && git commit -m "feat(server): speech 프레임 조립과 실패 폴백 추가"
```

---

### Task 7: Ollama 스트리밍 클라이언트

**Files:**
- Create: `server/app/llm.py`
- Test: `server/tests/test_llm.py`

**Interfaces:**
- Consumes: `config.settings`
- Produces:
  - `llm.LLMUnavailable` — Ollama 호출 실패 예외
  - `llm.stream_chat(messages: list[dict], *, client: httpx.AsyncClient | None = None) -> AsyncIterator[str]` — 토큰 문자열을 순서대로 내보낸다

Ollama `/api/chat`은 `stream=true`일 때 줄 단위 JSON을 흘려보낸다. 각 줄은 `{"message": {"content": "..."}, "done": false}` 형태이고 마지막 줄은 `"done": true`다.

테스트는 `httpx.MockTransport`로 가짜 응답을 만든다. 실제 Ollama가 없어도 돌아간다.

- [ ] **Step 1: 실패하는 테스트 작성**

`server/tests/test_llm.py`:

```python
import json

import httpx
import pytest

from app.llm import LLMUnavailable, stream_chat


def make_client(lines: list[str]) -> httpx.AsyncClient:
    def handler(request: httpx.Request) -> httpx.Response:
        return httpx.Response(200, content="\n".join(lines).encode())

    return httpx.AsyncClient(transport=httpx.MockTransport(handler))


def chunk(content: str, done: bool = False) -> str:
    return json.dumps({"message": {"content": content}, "done": done})


async def test_yields_tokens_in_order():
    client = make_client([chunk("안"), chunk("녕"), chunk("", done=True)])

    tokens = [token async for token in stream_chat([], client=client)]

    assert tokens == ["안", "녕"]


async def test_stops_at_done():
    client = make_client([chunk("가"), chunk("", done=True), chunk("무시됨")])

    tokens = [token async for token in stream_chat([], client=client)]

    assert tokens == ["가"]


async def test_ignores_blank_lines():
    client = make_client([chunk("가"), "", chunk("", done=True)])

    tokens = [token async for token in stream_chat([], client=client)]

    assert tokens == ["가"]


async def test_connection_error_raises_llm_unavailable():
    def handler(request: httpx.Request) -> httpx.Response:
        raise httpx.ConnectError("연결 거부")

    client = httpx.AsyncClient(transport=httpx.MockTransport(handler))

    with pytest.raises(LLMUnavailable):
        [token async for token in stream_chat([], client=client)]


async def test_http_error_raises_llm_unavailable():
    def handler(request: httpx.Request) -> httpx.Response:
        return httpx.Response(500, content=b"boom")

    client = httpx.AsyncClient(transport=httpx.MockTransport(handler))

    with pytest.raises(LLMUnavailable):
        [token async for token in stream_chat([], client=client)]
```

- [ ] **Step 2: 테스트 실패 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_llm.py -v
```

Expected: FAIL — `ModuleNotFoundError: No module named 'app.llm'`

- [ ] **Step 3: 구현**

`server/app/llm.py`:

```python
"""Ollama를 스트리밍 호출한다.

/api/chat은 stream=true일 때 줄 단위 JSON을 흘려보낸다. 응답 전체를 기다리지 않고
토큰이 도착하는 대로 내보내야 첫 발화까지의 지연이 줄어든다.
"""

import json
from typing import AsyncIterator

import httpx

from app.config import settings


class LLMUnavailable(Exception):
    """Ollama 호출에 실패했을 때 발생한다."""


async def stream_chat(
    messages: list[dict],
    *,
    client: httpx.AsyncClient | None = None,
) -> AsyncIterator[str]:
    owns_client = client is None
    client = client or httpx.AsyncClient(timeout=120.0)

    payload = {
        "model": settings.llm_model,
        "messages": messages,
        "stream": True,
    }

    try:
        async with client.stream(
            "POST", f"{settings.llm_base_url}/api/chat", json=payload
        ) as response:
            if response.status_code != 200:
                await response.aread()
                raise LLMUnavailable(f"Ollama가 {response.status_code}를 반환했다")

            async for line in response.aiter_lines():
                if not line.strip():
                    continue
                data = json.loads(line)
                content = data.get("message", {}).get("content", "")
                if content:
                    yield content
                if data.get("done"):
                    return
    except httpx.HTTPError as error:
        raise LLMUnavailable(str(error)) from error
    finally:
        if owns_client:
            await client.aclose()
```

- [ ] **Step 4: 테스트 통과 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_llm.py -v
```

Expected: PASS (5개)

- [ ] **Step 5: 커밋**

```bash
git add server/app/llm.py server/tests/test_llm.py && git commit -m "feat(server): Ollama 스트리밍 클라이언트 추가"
```

---

### Task 8: 세션 오케스트레이션

**Files:**
- Create: `server/app/session.py`
- Test: `server/tests/test_session.py`

**Interfaces:**
- Consumes: `llm.stream_chat`, `llm.LLMUnavailable`, `sentence.split_stream`, `pipeline.build_speech_frame`, `config.settings`
- Produces:
  - `session.Session(engine: TTSEngine, aligner: Aligner | None, *, token_stream=stream_chat)` — `token_stream`은 테스트에서 교체할 수 있게 주입한다
  - `session.Session.handle(text: str) -> AsyncIterator[dict]` — speech 프레임들을 내보내고 마지막에 turn_end를 내보낸다
  - `session.Session.history` — `list[dict]`, system 메시지를 항상 맨 앞에 유지한다

TTS 호출은 CPU/GPU를 점유하는 동기 함수다. 이벤트 루프를 막지 않도록 `asyncio.to_thread`로 감싼다.

- [ ] **Step 1: 실패하는 테스트 작성**

`server/tests/test_session.py`:

```python
import numpy as np

from app.llm import LLMUnavailable
from app.session import Session
from app.tts import SynthesisResult


class FakeTTS:
    def synthesize(self, text: str) -> SynthesisResult:
        return SynthesisResult(np.zeros(3200, dtype=np.float32), 16000)


def token_stream_from(tokens: list[str]):
    async def stream(messages, **kwargs):
        for token in tokens:
            yield token

    return stream


def failing_token_stream(messages, **kwargs):
    async def stream():
        raise LLMUnavailable("연결 거부")
        yield  # pragma: no cover

    return stream()


async def collect(session: Session, text: str) -> list[dict]:
    return [frame async for frame in session.handle(text)]


async def test_emits_speech_frames_then_turn_end():
    session = Session(
        FakeTTS(), None, token_stream=token_stream_from(["오늘은 날씨가 좋네요."])
    )

    frames = await collect(session, "안녕")

    assert [f["type"] for f in frames] == ["speech", "turn_end"]
    assert frames[0]["text"] == "오늘은 날씨가 좋네요."


async def test_seq_increments_per_sentence():
    session = Session(
        FakeTTS(),
        None,
        token_stream=token_stream_from(["가나다라마바사아자. ", "차카타파하가나다라!"]),
    )

    frames = await collect(session, "안녕")

    assert [f["seq"] for f in frames] == [0, 1, 2]
    assert frames[-1]["type"] == "turn_end"


async def test_trailing_text_without_boundary_is_flushed():
    session = Session(
        FakeTTS(), None, token_stream=token_stream_from(["마침표가 없는 문장"])
    )

    frames = await collect(session, "안녕")

    assert frames[0]["text"] == "마침표가 없는 문장"


async def test_history_accumulates_both_roles():
    session = Session(
        FakeTTS(), None, token_stream=token_stream_from(["반가워요 오늘도 좋네요."])
    )

    await collect(session, "안녕")

    assert session.history[0]["role"] == "system"
    assert session.history[1] == {"role": "user", "content": "안녕"}
    assert session.history[2]["role"] == "assistant"
    assert session.history[2]["content"] == "반가워요 오늘도 좋네요."


async def test_history_is_trimmed_but_keeps_system():
    session = Session(
        FakeTTS(), None, token_stream=token_stream_from(["짧은 답변입니다 여기까지"])
    )
    session.char_limit = 40

    for _ in range(5):
        await collect(session, "긴 질문을 반복해서 보냅니다")

    assert session.history[0]["role"] == "system"
    body = sum(len(m["content"]) for m in session.history[1:])
    assert body <= 40


async def test_llm_failure_emits_error_frame():
    session = Session(FakeTTS(), None, token_stream=failing_token_stream)

    frames = await collect(session, "안녕")

    assert frames[0]["type"] == "error"
    assert frames[0]["code"] == "LLM_UNAVAILABLE"
```

- [ ] **Step 2: 테스트 실패 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_session.py -v
```

Expected: FAIL — `ModuleNotFoundError: No module named 'app.session'`

- [ ] **Step 3: 구현**

`server/app/session.py`:

```python
"""한 WebSocket 연결의 대화 상태와 턴 처리를 담당한다.

연결이 곧 세션이다. 재연결하면 히스토리는 초기화된다.
"""

import asyncio
import logging
from typing import AsyncIterator

from app.config import settings
from app.llm import LLMUnavailable, stream_chat
from app.pipeline import Aligner, build_speech_frame
from app.sentence import split_stream
from app.tts import TTSEngine

logger = logging.getLogger(__name__)


class Session:
    def __init__(
        self,
        engine: TTSEngine,
        aligner: Aligner | None,
        *,
        token_stream=stream_chat,
    ):
        self.engine = engine
        self.aligner = aligner
        self.token_stream = token_stream
        self.char_limit = settings.history_char_limit
        self.history: list[dict] = [
            {"role": "system", "content": settings.system_prompt}
        ]

    async def handle(self, text: str) -> AsyncIterator[dict]:
        self.history.append({"role": "user", "content": text})

        buffer = ""
        reply = ""
        seq = 0

        try:
            async for token in self.token_stream(self.history):
                buffer += token
                sentences, buffer = split_stream(buffer)
                for content in sentences:
                    reply += content
                    yield await self._speech_frame(seq, content)
                    seq += 1

            sentences, buffer = split_stream(buffer, flush=True)
            for content in sentences:
                reply += content
                yield await self._speech_frame(seq, content)
                seq += 1
        except LLMUnavailable as error:
            logger.exception("LLM 호출 실패")
            yield {
                "type": "error",
                "code": "LLM_UNAVAILABLE",
                "message": str(error),
            }
            return

        self.history.append({"role": "assistant", "content": reply})
        self._trim_history()

        yield {"type": "turn_end", "seq": seq}

    async def _speech_frame(self, seq: int, content: str) -> dict:
        return await asyncio.to_thread(
            build_speech_frame, seq, content, self.engine, self.aligner
        )

    def _trim_history(self) -> None:
        system, body = self.history[0], self.history[1:]
        while sum(len(m["content"]) for m in body) > self.char_limit and body:
            body.pop(0)
        self.history = [system, *body]
```

- [ ] **Step 4: 테스트 통과 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_session.py -v
```

Expected: PASS (6개)

- [ ] **Step 5: 커밋**

```bash
git add server/app/session.py server/tests/test_session.py && git commit -m "feat(server): 세션 히스토리와 턴 오케스트레이션 추가"
```

---

### Task 9: WebSocket 엔드포인트

**Files:**
- Create: `server/app/main.py`
- Test: `server/tests/test_ws.py`

**Interfaces:**
- Consumes: `session.Session`
- Produces:
  - `main.app` — FastAPI 인스턴스
  - `main.build_session()` — 세션 팩토리. 테스트에서 `app.dependency_overrides` 대신 모듈 속성을 교체해 가짜 세션을 주입한다
  - WebSocket 엔드포인트 `/ws`

모델 로딩은 첫 연결이 아니라 lifespan에서 한 번만 한다. 다만 Task 10·11 전까지는 실제 엔진이 없으므로 `build_session`이 `None` 엔진을 쓰지 않도록 지연 import한다.

- [ ] **Step 1: 실패하는 테스트 작성**

`server/tests/test_ws.py`:

```python
import pytest
from fastapi.testclient import TestClient

from app import main


class FakeSession:
    def __init__(self, frames: list[dict]):
        self.frames = frames
        self.received: list[str] = []

    async def handle(self, text: str):
        self.received.append(text)
        for frame in self.frames:
            yield frame


@pytest.fixture
def client(monkeypatch):
    session = FakeSession(
        [
            {
                "type": "speech",
                "seq": 0,
                "text": "안녕하세요.",
                "audioBase64": "AAAA",
                "visemes": [{"v": "AA", "start": 0, "end": 100}],
            },
            {"type": "turn_end", "seq": 1},
        ]
    )
    monkeypatch.setattr(main, "build_session", lambda: session)
    return TestClient(main.app), session


def test_round_trip(client):
    test_client, session = client

    with test_client.websocket_connect("/ws") as ws:
        ws.send_json({"type": "user_message", "text": "안녕"})
        first = ws.receive_json()
        second = ws.receive_json()

    assert session.received == ["안녕"]
    assert first["type"] == "speech"
    assert first["visemes"] == [{"v": "AA", "start": 0, "end": 100}]
    assert second == {"type": "turn_end", "seq": 1}


def test_unknown_message_type_returns_error(client):
    test_client, _ = client

    with test_client.websocket_connect("/ws") as ws:
        ws.send_json({"type": "ping"})
        frame = ws.receive_json()

    assert frame["type"] == "error"
    assert frame["code"] == "BAD_REQUEST"


def test_missing_text_returns_error(client):
    test_client, _ = client

    with test_client.websocket_connect("/ws") as ws:
        ws.send_json({"type": "user_message"})
        frame = ws.receive_json()

    assert frame["type"] == "error"
    assert frame["code"] == "BAD_REQUEST"
```

- [ ] **Step 2: 테스트 실패 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_ws.py -v
```

Expected: FAIL — `ModuleNotFoundError: No module named 'app.main'`

- [ ] **Step 3: 구현**

`server/app/main.py`:

```python
"""FastAPI 앱과 WebSocket 엔드포인트.

연결 하나가 세션 하나다. 무거운 모델은 build_session이 처음 불릴 때 한 번만 만들어
이후 연결에서 재사용한다.
"""

import logging

from fastapi import FastAPI, WebSocket, WebSocketDisconnect

from app.session import Session

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

app = FastAPI(title="Local Conversational AI")

_engine = None
_aligner = None


def build_session() -> Session:
    """세션을 만든다. 엔진과 정렬기는 프로세스 수명 동안 한 번만 만든다."""
    global _engine, _aligner

    if _engine is None:
        from app.tts import XttsEngine

        _engine = XttsEngine()
    if _aligner is None:
        from app.align import CtcAligner

        _aligner = CtcAligner()

    return Session(_engine, _aligner)


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await websocket.accept()
    session = build_session()

    try:
        while True:
            message = await websocket.receive_json()

            if message.get("type") != "user_message" or not message.get("text"):
                await websocket.send_json(
                    {
                        "type": "error",
                        "code": "BAD_REQUEST",
                        "message": "user_message 타입과 text 필드가 필요하다",
                    }
                )
                continue

            async for frame in session.handle(message["text"]):
                await websocket.send_json(frame)
    except WebSocketDisconnect:
        logger.info("클라이언트 연결 종료")
```

`XttsEngine`과 `CtcAligner`는 Task 10·11에서 만든다. 지연 import이므로 테스트는 이들을 부르지 않아 지금 통과한다.

- [ ] **Step 4: 테스트 통과 확인**

```bash
cd server && .venv/Scripts/python -m pytest tests/test_ws.py -v
```

Expected: PASS (3개)

- [ ] **Step 5: 전체 테스트 실행**

```bash
cd server && .venv/Scripts/python -m pytest -v
```

Expected: PASS (53개). 여기까지가 모델 없이 검증 가능한 전부다.

- [ ] **Step 6: 커밋**

```bash
git add server/app/main.py server/tests/test_ws.py && git commit -m "feat(server): WebSocket 엔드포인트 추가"
```

---

### Task 10: XTTS-v2 엔진 연동

**Files:**
- Modify: `server/app/tts.py` (파일 끝에 추가)
- Modify: `server/requirements.txt`
- Create: `server/scripts/smoke_tts.py`

**Interfaces:**
- Consumes: `tts.SynthesisResult`, `tts.TTSError`, `config.settings`
- Produces: `tts.XttsEngine` — `TTSEngine` 프로토콜 구현체. 생성자에서 모델을 로드한다

여기부터는 GPU와 모델 파일이 필요하다. 단위 테스트로 검증할 수 없으므로 명시적 통과 기준을 가진 스모크 스크립트로 확인한다.

**선행 조건:** 참조 음성 wav 파일(6초 이상, 16kHz 이상, 잡음 없음)을 `server/models/speaker.wav`에 둔다. `server/models/`는 `.gitignore`에 이미 들어 있다.

- [ ] **Step 1: 의존성 추가**

`server/requirements.txt` 끝에 추가:

```
torch==2.5.1
torchaudio==2.5.1
coqui-tts==0.25.1
```

Windows + CUDA면 CUDA 빌드로 설치한다:

```bash
cd server && .venv/Scripts/pip install torch==2.5.1 torchaudio==2.5.1 --index-url https://download.pytorch.org/whl/cu121
```

그다음 나머지를 설치한다:

```bash
cd server && .venv/Scripts/pip install -r requirements.txt
```

- [ ] **Step 2: 스모크 스크립트 작성**

`server/scripts/smoke_tts.py`:

```python
"""XTTS 엔진이 실제로 소리를 만드는지 확인한다.

통과 기준:
- 예외 없이 SynthesisResult가 나온다
- duration_ms가 500 이상이다 (한 문장이 0.5초 미만일 수 없다)
- 파형에 무음이 아닌 구간이 있다
- out/smoke.wav를 귀로 들었을 때 한국어로 들린다
"""

import sys
from pathlib import Path

import numpy as np
import soundfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app.tts import XttsEngine  # noqa: E402

TEXT = "안녕하세요. 오늘 하루는 어땠나요?"


def main() -> int:
    engine = XttsEngine()
    result = engine.synthesize(TEXT)

    print(f"길이: {result.duration_ms}ms, 샘플레이트: {result.sample_rate}")
    print(f"최대 진폭: {np.max(np.abs(result.waveform)):.4f}")

    if result.duration_ms < 500:
        print("FAIL: 오디오가 너무 짧다")
        return 1
    if np.max(np.abs(result.waveform)) < 0.01:
        print("FAIL: 무음이다")
        return 1

    out = Path(__file__).resolve().parents[1] / "out"
    out.mkdir(exist_ok=True)
    soundfile.write(out / "smoke.wav", result.waveform, result.sample_rate)

    print(f"PASS: {out / 'smoke.wav'} 를 들어보고 한국어로 들리는지 확인한다")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 3: XttsEngine 구현**

`server/app/tts.py` 끝에 추가:

```python
class XttsEngine:
    """XTTS-v2 래퍼.

    8GB VRAM에서 다른 모델과 공존해야 하므로 fp16으로 로드한다. fp32로 로드하면
    4.5GB를 점유해 Ollama와 함께 올릴 여유가 없다.
    """

    MODEL_NAME = "tts_models/multilingual/multi-dataset/xtts_v2"

    def __init__(self) -> None:
        from TTS.api import TTS

        from app.config import settings

        self._speaker_wav = settings.xtts_speaker_wav
        self._tts = TTS(self.MODEL_NAME).to(settings.xtts_device)

        if settings.xtts_device.startswith("cuda"):
            self._tts.synthesizer.tts_model.half()

        self._sample_rate = self._tts.synthesizer.output_sample_rate

    def synthesize(self, text: str) -> SynthesisResult:
        try:
            samples = self._tts.tts(
                text=text,
                speaker_wav=self._speaker_wav,
                language="ko",
            )
        except Exception as error:
            raise TTSError(f"XTTS 합성 실패: {error}") from error

        waveform = np.asarray(samples, dtype=np.float32)
        if waveform.size == 0:
            raise TTSError("XTTS가 빈 파형을 반환했다")

        return SynthesisResult(waveform, self._sample_rate)
```

- [ ] **Step 4: 스모크 실행**

```bash
cd server && .venv/Scripts/python scripts/smoke_tts.py
```

Expected: `PASS: .../out/smoke.wav 를 들어보고...` 출력. 그 파일을 재생해 한국어로 들리는지 확인한다.

`.half()` 호출에서 예외가 나면 fp16을 끄고(`settings.xtts_device`를 그대로 두되 `.half()` 줄을 주석) 다시 시도한 뒤, VRAM 사용량을 `nvidia-smi`로 확인해 설계 문서 7절 예산과 비교한다. 예산을 넘으면 설계 문서의 리스크 항목으로 기록한다.

- [ ] **Step 5: 기존 테스트가 여전히 통과하는지 확인**

```bash
cd server && .venv/Scripts/python -m pytest -v
```

Expected: PASS (53개). XttsEngine은 지연 import이므로 단위 테스트에 영향이 없다.

- [ ] **Step 6: 커밋**

```bash
git add server/requirements.txt server/app/tts.py server/scripts/smoke_tts.py && git commit -m "feat(server): XTTS-v2 엔진 연동"
```

---

### Task 11: wav2vec2 CTC 강제 정렬

**Files:**
- Modify: `server/app/align.py` (파일 끝에 추가)
- Modify: `server/requirements.txt`
- Create: `server/scripts/smoke_align.py`

**Interfaces:**
- Consumes: `align.AlignError`, `hangul.Syllable`, `hangul.TimedSyllable`, `tts.SynthesisResult`, `config.settings`
- Produces: `align.CtcAligner` — `pipeline.Aligner` 프로토콜 구현체. 메서드 `align(result: SynthesisResult, syllables: list[Syllable]) -> list[TimedSyllable]`

`torchaudio.functional.forced_align`은 CTC 로그 확률과 토큰 시퀀스를 받아 프레임 단위 정렬을 돌려준다. 한국어 wav2vec2 모델의 어휘는 음절이 아니라 자모 또는 문자 단위이므로, 음절을 모델 어휘 토큰으로 바꾼 뒤 정렬 결과를 다시 음절 단위로 합친다.

**설계 문서 9절 통과 기준:** 마지막 음절의 `end`가 wav 길이의 ±10% 이내.

- [ ] **Step 1: 의존성 추가**

`server/requirements.txt` 끝에 추가:

```
transformers==4.47.1
```

```bash
cd server && .venv/Scripts/pip install -r requirements.txt
```

- [ ] **Step 2: 스모크 스크립트 작성**

`server/scripts/smoke_align.py`:

```python
"""CTC 정렬기가 실제 오디오에 대해 타당한 음절 시각을 내는지 확인한다.

통과 기준 (설계 문서 9절):
- 마지막 음절의 end가 오디오 길이의 ±10% 이내
- 시각이 단조 증가하고 구간이 겹치지 않는다
- 음절 수가 입력 텍스트의 한글 음절 수와 같다
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from app.align import CtcAligner  # noqa: E402
from app.hangul import decompose  # noqa: E402
from app.tts import XttsEngine  # noqa: E402

TEXT = "안녕하세요. 오늘 하루는 어땠나요?"


def main() -> int:
    result = XttsEngine().synthesize(TEXT)
    syllables = decompose(TEXT)
    timed = CtcAligner().align(result, syllables)

    print(f"오디오 길이: {result.duration_ms}ms, 음절 수: {len(syllables)}")
    for syllable, start, end in timed:
        print(f"  {syllable.char}  {start:>5} ~ {end:>5}ms")

    if len(timed) != len(syllables):
        print(f"FAIL: 음절 수 불일치 {len(timed)} != {len(syllables)}")
        return 1

    for (_, _, end), (_, start, _) in zip(timed, timed[1:]):
        if end > start:
            print("FAIL: 구간이 겹친다")
            return 1

    last_end = timed[-1][2]
    tolerance = result.duration_ms * 0.10
    if abs(last_end - result.duration_ms) > tolerance:
        print(f"FAIL: 마지막 end {last_end}ms 가 {result.duration_ms}ms 에서 10% 초과 벗어남")
        return 1

    print("PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 3: CtcAligner 구현**

`server/app/align.py` 끝에 추가:

```python
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
```

- [ ] **Step 4: 스모크 실행**

```bash
cd server && .venv/Scripts/python scripts/smoke_align.py
```

Expected: 음절별 시각이 출력되고 마지막 줄에 `PASS`.

FAIL이면 원인별로 대응한다.

| 실패 | 원인 | 대응 |
|---|---|---|
| `어휘에 없는 음절` | 모델 어휘가 음절이 아니라 자모 단위 | `_tokenize`가 음절 대신 자모(`onset`+`nucleus`+`coda`)를 토큰으로 내보내고, 음절당 자모 수를 함께 반환해 `token_spans`를 그 개수만큼 묶는다 |
| `정렬 결과 N개가 음절 M개와 다르다` | `merge_tokens`는 연속된 동일 토큰을 하나로 합친다. `"가가"`처럼 같은 음절이 붙으면 개수가 줄어든다 | 같은 음절이 연속될 때 사이에 blank를 강제로 넣거나, 이 문장만 균등 분배로 폴백한다 |
| 그 외 예외 | 모델·API 불일치 | `ALIGNER_MODEL` 환경변수로 다른 체크포인트를 시도한다 |

정렬 자체가 계속 실패하면 **Task 11을 건너뛰어도 된다.** 파이프라인은 `aligner=None`으로 균등 분배 폴백을 쓰며 정상 동작한다. 이 경우 `main.build_session`에서 `_aligner`를 `None`으로 두고, 설계 문서 11절에 미해결 항목으로 기록한다.

- [ ] **Step 5: 전체 테스트 확인**

```bash
cd server && .venv/Scripts/python -m pytest -v
```

Expected: PASS (53개)

- [ ] **Step 6: 커밋**

```bash
git add server/requirements.txt server/app/align.py server/scripts/smoke_align.py && git commit -m "feat(server): wav2vec2 CTC 강제 정렬 추가"
```

---

### Task 12: 통합 검증 클라이언트 + 실행 문서

**Files:**
- Create: `server/scripts/ws_client.py`
- Create: `server/README.md`

**Interfaces:**
- Consumes: 실행 중인 서버의 `/ws` 엔드포인트
- Produces: 없음 (검증 도구)

설계 문서 9절이 요구하는 "UE 없이 프레임을 검증하는 클라이언트"다. UE 클라이언트를 붙이기 전에 백엔드를 여기서 완결한다.

- [ ] **Step 1: 검증 클라이언트 작성**

`server/scripts/ws_client.py`:

```python
"""UE 없이 서버를 검증한다.

통과 기준:
- speech 프레임이 하나 이상 오고 마지막에 turn_end가 온다
- seq가 0부터 1씩 증가한다
- 각 speech 프레임의 viseme 구간이 겹치지 않고 단조 증가한다
- audioBase64가 있으면 디코딩되고 WAV로 읽힌다

사용법:
    python scripts/ws_client.py "오늘 뭐 했어?"
"""

import asyncio
import base64
import io
import json
import sys

import soundfile
from websockets.asyncio.client import connect

URL = "ws://localhost:8000/ws"


def check_frame(frame: dict, expected_seq: int) -> list[str]:
    problems: list[str] = []

    if frame["seq"] != expected_seq:
        problems.append(f"seq가 {expected_seq}가 아니라 {frame['seq']}")

    previous_end = None
    for span in frame["visemes"]:
        if span["start"] >= span["end"]:
            problems.append(f"빈 구간: {span}")
        if previous_end is not None and span["start"] < previous_end:
            problems.append(f"구간 겹침: {span}")
        previous_end = span["end"]

    if frame["audioBase64"] is not None:
        try:
            data, rate = soundfile.read(io.BytesIO(base64.b64decode(frame["audioBase64"])))
            print(f"    오디오 {len(data) * 1000 // rate}ms @ {rate}Hz")
        except Exception as error:
            problems.append(f"오디오 디코딩 실패: {error}")
    else:
        print("    오디오 없음 (TTS 폴백)")

    return problems


async def main(text: str) -> int:
    problems: list[str] = []
    speech_count = 0
    saw_turn_end = False

    async with connect(URL) as ws:
        await ws.send(json.dumps({"type": "user_message", "text": text}))

        while True:
            frame = json.loads(await ws.recv())

            if frame["type"] == "error":
                print(f"  error: {frame['code']} — {frame['message']}")
                problems.append(f"error 프레임: {frame['code']}")
                break

            if frame["type"] == "speech":
                print(f"  [{frame['seq']}] {frame['text']}")
                print(f"    viseme {len(frame['visemes'])}개")
                problems.extend(check_frame(frame, speech_count))
                speech_count += 1
                continue

            if frame["type"] == "turn_end":
                print(f"  turn_end (seq={frame['seq']})")
                saw_turn_end = True
                break

    if speech_count == 0:
        problems.append("speech 프레임이 하나도 없다")
    if not saw_turn_end:
        problems.append("turn_end가 오지 않았다")

    if problems:
        print("\nFAIL:")
        for problem in problems:
            print(f"  - {problem}")
        return 1

    print(f"\nPASS: speech {speech_count}개 + turn_end")
    return 0


if __name__ == "__main__":
    question = sys.argv[1] if len(sys.argv) > 1 else "오늘 뭐 했어?"
    raise SystemExit(asyncio.run(main(question)))
```

`websockets`를 `requirements.txt`에 추가한다:

```
websockets==14.1
```

```bash
cd server && .venv/Scripts/pip install -r requirements.txt
```

- [ ] **Step 2: 서버 실행**

별도 터미널에서:

```bash
cd server && .venv/Scripts/python -m uvicorn app.main:app --port 8000
```

Ollama도 떠 있어야 한다:

```bash
ollama run gemma3n:e2b
```

- [ ] **Step 3: 검증 실행**

```bash
cd server && .venv/Scripts/python scripts/ws_client.py "오늘 뭐 했어?"
```

Expected: 문장별 프레임이 출력되고 마지막에 `PASS: speech N개 + turn_end`.

- [ ] **Step 4: README 작성**

`server/README.md`:

```markdown
# 백엔드 서버

설계 문서: `../docs/superpowers/specs/2026-07-30-local-conversational-ai-design.md`

## 준비

1. Python 3.11 가상환경

   ```bash
   python -m venv .venv && .venv/Scripts/pip install -r requirements.txt
   ```

2. Ollama에 모델 받기

   ```bash
   ollama pull gemma3n:e2b
   ```

3. 참조 음성을 `models/speaker.wav`에 둔다 (6초 이상, 잡음 없는 한국어 발화)

## 실행

```bash
.venv/Scripts/python -m uvicorn app.main:app --port 8000
```

WebSocket 엔드포인트: `ws://localhost:8000/ws`

## 검증

단위 테스트 (모델 불필요):

```bash
.venv/Scripts/python -m pytest -v
```

TTS 스모크 (GPU 필요):

```bash
.venv/Scripts/python scripts/smoke_tts.py
```

정렬 스모크 (GPU 필요):

```bash
.venv/Scripts/python scripts/smoke_align.py
```

전체 왕복 (서버 + Ollama 실행 중이어야 함):

```bash
.venv/Scripts/python scripts/ws_client.py "오늘 뭐 했어?"
```

## 설정

환경변수로 바꾼다. 기본값은 `app/config.py` 참조.

| 변수 | 기본값 |
|---|---|
| `LLM_BASE_URL` | `http://localhost:11434` |
| `LLM_MODEL` | `gemma3n:e2b` |
| `XTTS_SPEAKER_WAV` | `models/speaker.wav` |
| `XTTS_DEVICE` | `cuda` |
| `ALIGNER_MODEL` | `kresnik/wav2vec2-large-xlsr-korean` |
| `HISTORY_CHAR_LIMIT` | `6000` |
| `SYSTEM_PROMPT` | `app/config.py` 참조 |
```

- [ ] **Step 5: 커밋**

```bash
git add server/scripts/ws_client.py server/README.md server/requirements.txt && git commit -m "feat(server): 통합 검증 클라이언트와 실행 문서 추가"
```

---

## 완료 기준

전부 통과하면 백엔드가 완결된 것이다. UE 클라이언트는 이 계약에 붙이기만 하면 된다.

- [ ] `pytest -v` 53개 통과 (모델 없이)
- [ ] `smoke_tts.py` PASS, `out/smoke.wav`가 한국어로 들림
- [ ] `smoke_align.py` PASS 또는 균등 분배 폴백으로 진행 결정 기록
- [ ] `ws_client.py` PASS
- [ ] Ollama를 끈 상태에서 `ws_client.py`를 돌리면 `error` 프레임이 오고 서버가 죽지 않음
- [ ] `models/speaker.wav`를 지운 상태에서 돌리면 `audioBase64: null` 프레임이 오고 자막은 나옴
