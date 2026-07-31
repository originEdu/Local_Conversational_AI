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
