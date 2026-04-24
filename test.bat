@echo off
echo [TEST] Starting Python Server...
start "Arcade Survivors Server" cmd /k "python server/server.py"

echo [TEST] Waiting for server to initialize...
timeout /t 2 /nobreak > nul

echo [TEST] Starting Client 1...
start "Client 1" bin/game.exe

echo [TEST] Starting Client 2...
start "Client 2" bin/game.exe

echo [TEST] All processes started.
