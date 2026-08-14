# STT · TTS 시퀀스

서버의 음성 경로를 호출 순서대로 그린다. 전체 구조는 `docs/동작원리.md`에 있다.

두 경로는 **완전히 분리돼 있다.**

| | STT | TTS |
|---|---|---|
| 입력 프레임 | `audio_message` | `user_message` |
| 진입점 | `main._transcribe` | `session.Session.handle` |
| 대화 상태 | 없음 (무상태) | 히스토리에 쌓임 |
| 응답 | `transcript` 1개 | `speech` N개 + `turn_end` 1개 |
| LLM | 안 부름 | 부름 |

---

## 1. 모델 로딩 (기동 시 1회)

```mermaid
sequenceDiagram
    autonumber
    participant U as uvicorn
    participant M as main.lifespan
    participant L as main._load_models
    participant T as tts.create_engine
    participant A as align.CtcAligner
    participant S as stt.WhisperEngine

    U->>M: 앱 시작
    M->>L: await asyncio.to_thread(_load_models)
    Note over M,L: to_thread로 감싸야 로딩 중에도<br/>이벤트 루프가 안 막힌다
    L->>T: create_engine()
    T-->>L: MeloEngine (GPU 0.9GB)
    L->>A: CtcAligner()
    A-->>L: wav2vec2 (CPU)
    L->>S: WhisperEngine()
    S-->>L: large-v3-turbo, int8_float16 (GPU 0.9GB, 27초)
    L-->>M: 완료
    M-->>U: "모델 로딩 완료"
```

셋 다 모듈 전역(`_engine`, `_aligner`, `_stt`)에 담긴다. 프로세스 수명 동안 한 번만
만들고 모든 연결이 공유한다.

`_load_models()`는 멱등이다. `build_session()`과 `build_stt()`가 매번 부르지만 이미
만들어져 있으면 그냥 지나간다.

---

## 2. STT — 받아적기 한 번

```mermaid
sequenceDiagram
    autonumber
    participant C as UE 클라이언트
    participant W as main.websocket_endpoint
    participant TR as main._transcribe
    participant S as stt.WhisperEngine
    participant FW as faster_whisper

    C->>W: {"type":"audio_message", "audioBase64":"UklGRi..."}
    W->>TR: await _transcribe(ws, message)
    TR->>TR: base64.b64decode(validate=True)

    TR->>S: await to_thread(transcribe, wav)
    Note over TR,S: 여기가 GPU 작업이다.<br/>to_thread로 빼야 다른 연결이 안 막힌다
    S->>FW: model.transcribe(BytesIO(wav),<br/>language="ko",<br/>vad_filter=True,<br/>condition_on_previous_text=False)
    FW-->>S: segments (제너레이터)
    S->>S: "".join(seg.text for seg in segments).strip()
    Note over S: 제너레이터를 여기서 소비해야<br/>실제 디코딩이 돈다
    S-->>TR: "오늘 뭐 했어?"

    TR-->>C: {"type":"transcript", "text":"오늘 뭐 했어?"}
```

**턴을 돌리지 않는다.** 히스토리도 안 건드리고 LLM도 안 부른다. 받아적은 글자는
클라이언트 입력란에 들어가고, 보낼지는 사용자가 정한다.

그래서 이 경로는 완전히 무상태다. 연결마다 오디오 버퍼를 들고 있을 필요가 없다.

### 실패 분기

```mermaid
sequenceDiagram
    autonumber
    participant C as UE 클라이언트
    participant TR as main._transcribe
    participant S as stt.WhisperEngine

    alt base64가 깨졌다
        C->>TR: audioBase64: "!!!"
        TR->>TR: binascii.Error
        TR-->>C: {"type":"error", "code":"BAD_REQUEST"}
    else 인식기가 죽었다
        C->>TR: audioBase64: "UklGRi..."
        TR->>S: transcribe(wav)
        S-->>TR: STTError
        TR-->>C: {"type":"error", "code":"STT_FAILED"}
    else 아무 말도 없었다
        C->>TR: audioBase64: "UklGRi..." (무음)
        TR->>S: transcribe(wav)
        Note over S: vad_filter가 무음을 잘라내<br/>segments가 비어 나온다
        S-->>TR: ""
        TR-->>C: {"type":"transcript", "text":""}
    end
```

빈 문자열은 오류가 아니다. 이것도 답으로 보내야 클라이언트가 녹음 상태를 푼다.

`vad_filter`가 없으면 whisper는 무음에 학습 데이터의 자막 상투구를 지어낸다
("시청해주셔서 감사합니다").

### 말하는 중 실시간 갱신

```mermaid
sequenceDiagram
    autonumber
    participant U as 사용자
    participant W as ConversationWidget<br/>(NativeTick)
    participant R as MicRecorder
    participant SV as 서버

    U->>R: (말하기 시작)
    loop 1초마다, 앞 응답이 온 뒤에만
        W->>R: Snapshot()
        R-->>W: 녹음 전체 WAV
        W->>SV: audio_message (전체)
        Note over W,SV: 매번 처음부터 다시 보낸다.<br/>whisper 인코더가 30초로 패딩해<br/>인식 비용이 길이와 무관하다
        SV-->>W: transcript (전체 문장)
        W->>W: InputBox를 통째로 갈아친다
    end

    U->>R: (말 멈춤)
    R->>R: RMS < conv.SpeechLevel이<br/>conv.SilenceMs만큼 이어짐
    W->>R: Stop()
    R-->>W: 최종 WAV
    W->>SV: audio_message (최종)
    SV-->>W: transcript (최종)

    alt 실시간 대화 모드
        W->>W: 곧바로 user_message로 전송
    else 푸시 투 토크 모드
        W->>W: 입력란에 남긴다. 사용자가 엔터를 칠 때까지 안 보낸다
    end
```

앞 요청의 답이 오기 전에는 다음 것을 보내지 않는다(`PendingAudio > 0`이면 건너뛴다).
인식이 느려지면 갱신 간격이 알아서 벌어진다.

---

## 3. TTS — 한 턴 전체

```mermaid
sequenceDiagram
    autonumber
    participant C as UE 클라이언트
    participant W as main.websocket_endpoint
    participant SE as session.Session
    participant L as llm.stream_chat
    participant SP as sentence.split_stream
    participant P as pipeline.build_speech_frame

    C->>W: {"type":"user_message", "text":"오늘 뭐 했어?"}
    W->>SE: async for frame in session.handle(text)
    SE->>SE: history.append({"role":"user", ...})

    SE->>L: stream_chat(history)

    loop 토큰이 올 때마다
        L-->>SE: 토큰 조각
        SE->>SE: buffer += token
        SE->>SP: split_stream(buffer)
        SP-->>SE: (완성된 문장들, 남은 buffer)

        loop 완성된 문장마다
            SE->>P: await to_thread(build_speech_frame, seq, 문장, engine, aligner)
            Note over SE,P: 4장 참조. TTS와 정렬이 여기서 돈다
            P-->>SE: speech 프레임
            SE-->>W: yield frame
            W-->>C: {"type":"speech", "seq":0, ...}
        end
    end

    SE->>SP: split_stream(buffer, flush=True)
    Note over SE,SP: LLM이 마침표 없이 끝냈을 때<br/>남은 buffer를 마지막 문장으로 뱉는다
    SP-->>SE: 남은 문장
    SE->>P: build_speech_frame(...)
    P-->>SE: speech 프레임
    SE-->>C: {"type":"speech", "seq":N, ...}

    SE->>SE: history.append({"role":"assistant", ...})
    SE->>SE: _trim_history()
    SE-->>C: {"type":"turn_end", "seq":N+1}
```

**문장 단위로 자르는 게 체감 속도의 핵심이다.** 답변 전체를 기다리면 5~8초 침묵이
생긴다. 첫 문장만 나오면 바로 말을 시작하므로 2.6초 안에 입이 움직인다.

LLM이 죽으면 문장 루프 전체가 `LLMUnavailable`로 빠져나와
`{"type":"error", "code":"LLM_UNAVAILABLE"}` 하나만 나가고 턴이 끝난다. 서버는 안 죽는다.

---

## 4. TTS — 프레임 하나 만들기

`build_speech_frame`은 동기 함수다. `Session._speech_frame`이 `to_thread`로 감싸
이벤트 루프 밖에서 돌린다.

```mermaid
sequenceDiagram
    autonumber
    participant SE as session
    participant P as pipeline.build_speech_frame
    participant E as tts.MeloEngine
    participant H as hangul.decompose
    participant A as align.CtcAligner
    participant V as viseme.syllables_to_visemes
    participant WV as tts.to_wav_base64

    SE->>P: build_speech_frame(seq, "음, 그냥 책 읽었어!", engine, aligner)

    P->>E: synthesize(text)
    E-->>P: SynthesisResult(waveform, sample_rate)
    Note over E,P: duration_ms는 파형 길이에서 계산된다

    P->>H: decompose(text)
    H-->>P: [Syllable(음), Syllable(그), ...]
    Note over H: 한글이 아닌 글자는 빠진다.<br/>구두점·이모지는 음절이 아니다

    P->>A: align(result, syllables)
    A->>A: 48kHz -> 16kHz 리샘플
    A->>A: wav2vec2 로그 확률 -> forced_align
    A-->>P: [(음절, 시작ms, 끝ms), ...]

    P->>V: syllables_to_visemes(timed)
    V-->>P: [VisemeSpan(v="EU", 0, 340), ...]
    Note over V: 음절 하나가 최대 3구간이 된다<br/>(초성 앞 25% / 중성 / 종성 뒤 20%)

    P->>WV: to_wav_base64(result)
    WV-->>P: "UklGRi..."

    P-->>SE: {"type":"speech", "seq":..., "text":..., "audioBase64":..., "visemes":[...]}
```

### 폴백 두 단계

```mermaid
sequenceDiagram
    autonumber
    participant P as pipeline.build_speech_frame
    participant E as tts.MeloEngine
    participant A as align.CtcAligner
    participant UA as align.uniform_align

    alt TTS 실패
        P->>E: synthesize(text)
        E-->>P: TTSError
        Note over P: 자막만 내보낸다.<br/>입은 가만있지만 대화는 안 끊긴다
        P-->>P: audioBase64=None, visemes=[]
    else 정렬 실패
        P->>A: align(result, syllables)
        A-->>P: AlignError
        Note over A,P: 어휘에 없는 음절(팁·늑·콕 등)이나<br/>정렬 결과 개수 불일치
        P->>UA: uniform_align(syllables, duration_ms)
        UA-->>P: 오디오 길이를 음절 수로 균등 분배
        Note over UA: 입 모양이 소리보다 밀리지만<br/>타임라인은 나온다
    else 정렬기가 아예 없음
        Note over P: aligner=None (테스트, 또는 로딩 실패)
        P->>UA: uniform_align(...)
        UA-->>P: 균등 분배
    end
```

**텍스트가 최소 보장선이다.** 어느 단계가 죽어도 자막은 나간다.

---

## 5. 두 경로가 만나는 곳

```mermaid
sequenceDiagram
    autonumber
    participant C as UE 클라이언트
    participant W as main.websocket_endpoint

    Note over C,W: 연결 하나 = 세션 하나. build_session()이 한 번 불린다

    loop 연결이 살아 있는 동안
        C->>W: receive_json()
        alt type == "audio_message"
            W->>W: await _transcribe(...)
            W-->>C: transcript
        else type == "user_message" && text 있음
            W->>W: async for frame in session.handle(text)
            W-->>C: speech × N, turn_end
        else 그 외
            W-->>C: {"type":"error", "code":"BAD_REQUEST"}
        end
    end
```

한 연결 안에서는 **메시지가 순서대로 처리된다.** `await`로 하나를 끝내야 다음
`receive_json()`으로 간다. 그래서 부분 인식 응답이 최종 인식 응답보다 늦게 도착하는
일은 없다.

연결이 여러 개면 `to_thread`의 스레드풀에서 STT와 TTS가 동시에 GPU를 칠 수 있다.
지금은 클라이언트가 하나라 문제가 안 된다.

`WebSocketDisconnect`가 나면 루프를 빠져나오고 `Session` 객체는 버려진다.
**히스토리도 같이 사라진다** — 연결이 곧 세션이다.
