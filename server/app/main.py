"""FastAPI 앱과 WebSocket 엔드포인트.

연결 하나가 세션 하나다. 무거운 모델은 build_session이 처음 불릴 때 한 번만 만들어
이후 연결에서 재사용한다.
"""

import asyncio
import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI, WebSocket, WebSocketDisconnect

from app.session import Session

logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

_engine = None
_aligner = None


def _load_models() -> None:
    """엔진과 정렬기를 프로세스 수명 동안 한 번만 만든다."""
    global _engine, _aligner

    if _engine is None:
        from app.tts import create_engine

        _engine = create_engine()
    if _aligner is None:
        from app.align import CtcAligner

        _aligner = CtcAligner()


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
