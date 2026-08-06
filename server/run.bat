@echo off
REM 백엔드 서버를 개발용 설정으로 띄운다. 종료는 Ctrl+C.
REM LLM은 원격 PC의 llama.cpp를 쓴다. OpenAI 호환이므로 LLM_API=openai.
cd /d "%~dp0"

if not defined LLM_API set LLM_API=openai
if not defined LLM_BASE_URL set LLM_BASE_URL=http://192.168.3.26:8080

echo LLM_API=%LLM_API%  LLM_BASE_URL=%LLM_BASE_URL%
.venv\Scripts\python -m uvicorn app.main:app --port 8000

REM 포트가 이미 잡혀 있으면 uvicorn이 바로 죽는다. 창이 닫히기 전에 이유를 보여준다.
if errorlevel 1 pause
