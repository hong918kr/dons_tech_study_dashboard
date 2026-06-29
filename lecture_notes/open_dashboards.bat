@echo off
REM ============================================================
REM  Open both dashboards in Chrome at logon.
REM  Waits for both servers to come up, then opens two tabs.
REM    5050 = Durumi lecture note
REM    5055 = Study dashboard
REM  Copy into the Windows Startup folder (shell:startup) to auto-run.
REM  NOTE: ASCII-only on purpose - boot console codepage is not guaranteed.
REM ============================================================
set "CHROME=C:\Program Files\Google\Chrome\Application\chrome.exe"
set "URL1=http://127.0.0.1:5050/"
set "URL2=http://127.0.0.1:5055/"

REM Fall back to PATH chrome if the pinned path is missing
if not exist "%CHROME%" set "CHROME=chrome.exe"

REM Wait up to ~40s for BOTH ports to be listening before opening Chrome.
set /a tries=0
:waitloop
set "UP1="
set "UP2="
netstat -ano | findstr ":5050" | findstr "LISTENING" >nul && set "UP1=1"
netstat -ano | findstr ":5055" | findstr "LISTENING" >nul && set "UP2=1"
if defined UP1 if defined UP2 goto :open
set /a tries+=1
if %tries% geq 20 goto :open
timeout /t 2 /nobreak >nul
goto :waitloop

:open
REM Open both URLs as tabs in one Chrome window.
start "" "%CHROME%" "%URL1%" "%URL2%"
