@echo off
REM 8000 포트를 잡고 있는 서버를 죽이고 다시 띄운다.
REM
REM 코드를 고친 뒤에 쓴다. uvicorn --reload는 못 쓴다 -- 파일이 바뀔 때마다
REM 모델을 처음부터 다시 올려서 2분씩 걸린다.
cd /d "%~dp0"

for /f "tokens=5" %%p in ('netstat -ano ^| findstr /r /c:":8000 .*LISTENING"') do (
    echo 기존 서버 종료: PID %%p
    taskkill /f /pid %%p >nul 2>&1
)

call run.bat
