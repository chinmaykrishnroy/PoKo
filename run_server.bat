@echo off
setlocal

cd /d "%~dp0"

if "%~1"=="--log" (
  if not exist "server\logs" mkdir "server\logs"
  echo Starting Poko backend with logging...
  echo Log: server\logs\server.log
  python -m server.poko_server --config server\config.yml >> server\logs\server.log 2>&1
  exit /b %ERRORLEVEL%
)

echo Starting Poko backend...
echo Working directory: %CD%
echo URL: http://127.0.0.1:8765
echo.

python -m server.poko_server --config server\config.yml

echo.
echo Poko backend stopped.
pause

