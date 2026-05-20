@echo off
setlocal

:: Settings
set BIN_DIR=bin
set CC=gcc
set CFLAGS=-Wall -Iraylib/src -Isource
set LDFLAGS=-Lraylib/src -lraylib -lws2_32 -lgdi32 -lwinmm

echo [BUILD] Creating bin directory...
if not exist %BIN_DIR% mkdir %BIN_DIR%

echo [BUILD] Copying assets to bin...
xcopy /E /I /Y assets "%BIN_DIR%\assets" >nul

echo [BUILD] Compiling game client...
%CC% source/main.c source/connection/connection.c -o %BIN_DIR%/game.exe %CFLAGS% %LDFLAGS%

if %errorlevel% neq 0 (
    echo [ERROR] Game build failed!
    pause
    exit /b %errorlevel%
) else (
    echo [SUCCESS] Game built at %BIN_DIR%/game.exe
)

echo [BUILD] Compiling Python server into executable...
python -m PyInstaller --onefile --distpath "%BIN_DIR%" --name server server/server.py >nul

if %errorlevel% neq 0 (
    echo [ERROR] Server build failed!
) else (
    echo [SUCCESS] Server built at %BIN_DIR%/server.exe
    :: Clean up PyInstaller build artifacts to keep repository clean
    if exist build rmdir /s /q build
    if exist server.spec del /q server.spec
)

pause
endlocal
