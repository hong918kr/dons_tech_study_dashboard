@echo off
REM ============================================================
REM  Study dashboard - boot launcher (runs server silently)
REM  Copy this into the Windows Startup folder to auto-run at logon:
REM    %APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup
REM  Server only (no browser).  URL: http://127.0.0.1:5055
REM  NOTE: ASCII-only on purpose - boot console codepage is not guaranteed.
REM ============================================================
set "PROJ=C:\workspace\don_tech_study\lecture_notes"
set "PYW=C:\Users\haydn\AppData\Local\Programs\Python\Python313\pythonw.exe"

cd /d "%PROJ%"

REM Fall back to PATH pythonw if the pinned path is missing
if not exist "%PYW%" set "PYW=pythonw.exe"

REM Skip if port 5055 is already listening (avoid double launch)
netstat -ano | findstr ":5055" | findstr "LISTENING" >nul
if %errorlevel%==0 goto :eof

REM Make sure log folder exists
if not exist "%PROJ%\logs" mkdir "%PROJ%\logs"

REM Launch server windowless with pythonw, append output to log
start "" /b "%PYW%" scripts\server.py >> "%PROJ%\logs\server.log" 2>&1
