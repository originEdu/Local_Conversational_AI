# 백엔드 서버

설계 문서: `../docs/superpowers/specs/2026-07-30-local-conversational-ai-design.md`
구현 계획: `../docs/superpowers/plans/2026-07-30-backend-server.md`

## 준비

1. Python 3.11 가상환경

   ```bash
   python -m venv .venv && .venv/Scripts/pip install -r requirements.txt
   ```

2. MeloTTS 본체는 `requirements.txt`로 설치되지 않는다. 따로 실행한다.

   ```bash
   .venv/Scripts/pip install --no-deps git+https://github.com/myshell-ai/MeloTTS.git
   ```

   `--no-deps`가 필수다. MeloTTS는 `transformers==4.27.4`를 못박는데 coqui-tts는
   그보다 높은 버전을 요구해서, 그냥 설치하면 XTTS가 깨진다.

3. Ollama에 모델 받기

   ```bash
   ollama pull gemma3n:e2b
   ```

4. XTTS를 쓸 때만 필요: 참조 음성을 `models/speaker.wav`에 둔다
   (6초 이상, 잡음 없는 한국어 발화). MeloTTS는 참조 음성을 쓰지 않는다.

## 실행

```bash
.venv/Scripts/python -m uvicorn app.main:app --port 8000
```

WebSocket 엔드포인트: `ws://localhost:8000/ws`

모델은 서버 기동 시점에 올라간다. `모델 로딩 완료` 로그가 찍힌 뒤에 접속한다.

## 검증

단위 테스트 (모델 불필요, 1초 이내):

```bash
.venv/Scripts/python -m pytest -v
```

TTS 스모크. 엔진 이름을 인자로 준다. 결과는 `out/smoke_<엔진>.wav`:

```bash
.venv/Scripts/python scripts/smoke_tts.py melo
```

```bash
.venv/Scripts/python scripts/smoke_tts.py xtts
```

XTTS 첫 실행은 CPML 동의 프롬프트에서 멈춘다. 미리 `COQUI_TOS_AGREED=1`을 설정한다.

정렬 스모크:

```bash
.venv/Scripts/python scripts/smoke_align.py
```

전체 왕복 (서버 + Ollama 실행 중이어야 함):

```bash
.venv/Scripts/python scripts/ws_client.py "오늘 뭐 했어?"
```

## TTS 엔진

`TTS_ENGINE` 환경변수로 고른다. 기본값은 `melo`.

| | MeloTTS (`melo`) | XTTS-v2 (`xtts`) |
|---|---|---|
| 한글 처리 | 직접 | 로마자 변환 후 다국어 모델 |
| 발음 | 자연스러움 | 외국인 억양 |
| 음성 복제 | 불가 | 가능 (`models/speaker.wav`) |
| 같은 문장 길이 | 4357ms | 8534ms |
| VRAM | 802MB | 1899MB |
| 샘플레이트 | 44100 | 24000 |
| 라이선스 | MIT | CPML (비상업 전용) |

## 설정

환경변수로 바꾼다. 기본값은 `app/config.py` 참조.

| 변수 | 기본값 |
|---|---|
| `LLM_BASE_URL` | `http://localhost:11434` |
| `LLM_MODEL` | `gemma3n:e2b` |
| `LLM_API` | `ollama` |
| `TTS_ENGINE` | `melo` |
| `XTTS_SPEAKER_WAV` | `models/speaker.wav` |
| `XTTS_DEVICE` | `cuda` |
| `ALIGNER_MODEL` | `kresnik/wav2vec2-large-xlsr-korean` |
| `HISTORY_CHAR_LIMIT` | `6000` |
| `SYSTEM_PROMPT` | `app/config.py` 참조 |

`XTTS_DEVICE`는 이름과 달리 MeloTTS에도 적용된다.

## LLM 서버 바꾸기

`LLM_API`로 호출 방식을 고른다. 주소만 바꿔서는 안 된다. 경로도 응답 형식도 다르다.

| | `ollama` | `openai` |
|---|---|---|
| 대상 | Ollama | llama.cpp server, vLLM |
| 경로 | `/api/chat` | `/v1/chat/completions` |
| 응답 형식 | 줄 단위 JSON | SSE (`data: {...}`) |
| 토큰 위치 | `message.content` | `choices[0].delta.content` |
| 종료 신호 | `done: true` | `data: [DONE]` 또는 `finish_reason` |

원격 llama.cpp에 붙일 때:

```bash
LLM_API=openai LLM_BASE_URL=http://192.168.3.26:8080 .venv/Scripts/python -m uvicorn app.main:app --port 8000
```

llama.cpp는 `LLM_MODEL`을 무시하고 기동 시 올린 모델을 쓴다. 그래도 필드는 보낸다.

llama.cpp가 원격에서 안 잡히면 `--host 0.0.0.0`으로 띄웠는지부터 본다.
기본값이 `127.0.0.1`이라 로컬 접속만 받는다. 확인은 원격에서 `ss -tlnp | grep 8080`.

`openai` 경로는 요청에 `chat_template_kwargs.enable_thinking = false`를 넣는다.
추론형 모델은 답변 전에 사고 토큰을 수백 개 만드는데 발화에 쓰이지 않아 전부 버려진다.
`gemma-4-E2B-it-Q8_0` 실측으로 298토큰 24.2초 중 사고가 278토큰이었다.
끄면 같은 질문이 13토큰 0.94초가 된다.

## 구조

```
app/
  main.py       FastAPI 앱, WebSocket 엔드포인트, 모델 사전 로딩
  session.py    대화 히스토리, 턴 오케스트레이션
  llm.py        Ollama 스트리밍 호출
  sentence.py   토큰 스트림을 문장으로 분할
  pipeline.py   speech 프레임 조립, 실패 폴백
  tts.py        TTS 프로토콜, MeloTTS·XTTS 구현체, WAV 인코딩
  align.py      균등 분배 폴백, wav2vec2 CTC 정렬
  viseme.py     음절 -> viseme 매핑
  hangul.py     한글 자모 분해
  config.py     환경변수 설정
```

의존 방향은 한 방향이다.
`main` -> `session` -> `pipeline` -> (`tts`, `align`, `viseme` -> `hangul`).
`hangul`, `viseme`, `sentence`는 아무것도 import하지 않는 순수 모듈이다.
