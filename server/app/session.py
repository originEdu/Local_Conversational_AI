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
