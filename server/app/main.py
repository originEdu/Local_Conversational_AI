"""FastAPI 앱과 WebSocket 엔드포인트.

연결 하나가 세션 하나다. 무거운 모델은 build_session이 처음 불릴 때 한 번만 만들어
이후 연결에서 재사용한다.
"""

import asyncio
import base64
import binascii
import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI, WebSocket, WebSocketDisconnect

from app.session import Session
from app.stt import STTError

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

_engine = None
_aligner = None
_stt = None


def _load_models() -> None:
    """엔진과 정렬기와 인식기를 프로세스 수명 동안 한 번만 만든다."""
    global _engine, _aligner, _stt

    if _engine is None:
        from app.tts import create_engine

        _engine = create_engine()
    if _aligner is None:
        from app.align import CtcAligner

        _aligner = CtcAligner()
    if _stt is None:
        from app.stt import WhisperEngine

        _stt = WhisperEngine()


@asynccontextmanager
async def lifespan(app: FastAPI):
    """모델을 서버 기동 시점에 올린다.

    첫 연결에서 로딩하면 수십 초간 이벤트 루프가 막혀 WebSocket 핸드셰이크가
    타임아웃된다. to_thread로 감싸 기동 중에도 루프를 막지 않는다.
    """
    logger.info("모델 로딩 시작")
    await asyncio.to_thread(_load_models)
    logger.info("모델 로딩 완료")
    yield


app = FastAPI(title="Local Conversational AI", lifespan=lifespan)


def build_session() -> Session:
    _load_models()
    return Session(_engine, _aligner)


def build_stt():
    _load_models()
    return _stt


async def _transcribe(websocket: WebSocket, message: dict) -> None:
    """audio_message를 받아 적어 transcript 프레임으로 돌려준다.

    턴은 돌리지 않는다. 클라이언트가 이 글자를 입력란에 채워 넣고, 사용자가 고친 뒤
    직접 보낸다. 잘못 알아들은 문장이 그대로 LLM에 가는 것보다 낫다.

    아무 말도 없었으면 빈 문자열을 보낸다. 클라이언트가 "못 알아들었다"를 구분해야
    녹음 상태를 풀 수 있다.
    """
    try:
        wav = base64.b64decode(message.get("audioBase64", ""), validate=True)
    except (binascii.Error, ValueError):
        await websocket.send_json(
            {
                "type": "error",
                "code": "BAD_REQUEST",
                "message": "audioBase64를 디코딩할 수 없다",
            }
        )
        return

    try:
        text = await asyncio.to_thread(build_stt().transcribe, wav)
    except STTError as error:
        logger.exception("음성 인식 실패")
        await websocket.send_json(
            {"type": "error", "code": "STT_FAILED", "message": str(error)}
        )
        return

    logger.info("받아적음 (%d바이트): %s", len(wav), text)
    await websocket.send_json({"type": "transcript", "text": text})


@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket) -> None:
    await websocket.accept()
    session = build_session()

    try:
        while True:
            message = await websocket.receive_json()
            kind = message.get("type")

            if kind == "audio_message":
                await _transcribe(websocket, message)
                continue

            if kind != "user_message" or not message.get("text"):
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
