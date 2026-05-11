@echo off
echo [TEST] Starting Server...
start "Arcade Survivors Server" python server/server.py

echo [TEST] Waiting for server to initialize...
timeout /t 2 /nobreak > nul

echo [TEST] Starting Player 1...
start "Player 1" bin\game.exe

echo [TEST] Starting Player 2...
start "Player 2" bin\game.exe

echo.
echo [TEST] Both clients started. 
echo [TEST] Press any key in this window to kill the server and exit.
pause > nul

echo [TEST] Cleaning up...
taskkill /FI "WINDOWTITLE eq Arcade Survivors Server*" /T /F > nul 2>&1
echo [TEST] Done.
