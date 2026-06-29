@echo off
REM ============================================================
REM  강의노트 - 서버 실행 (더블클릭하면 서버 + 브라우저)
REM ============================================================
chcp 65001 >nul
setlocal

set "PROJ=%~dp0"
set "PY=C:\Users\haydn\AppData\Local\Programs\Python\Python313\python.exe"
set "URL=http://127.0.0.1:5055"

cd /d "%PROJ%"
if not exist "%PY%" set "PY=python"

netstat -ano | findstr ":5055" | findstr "LISTENING" >nul
if %errorlevel%==0 (
    echo [info] 서버가 이미 실행 중입니다. 브라우저만 엽니다.
    start "" "%URL%"
    goto :eof
)

echo [info] 강의노트 서버를 시작합니다...
if not exist "%PROJ%logs" mkdir "%PROJ%logs"
start "Lecture Notes Server" /min cmd /c ""%PY%" scripts\server.py >> "%PROJ%logs\server.log" 2>&1"
timeout /t 3 /nobreak >nul
start "" "%URL%"

endlocal
