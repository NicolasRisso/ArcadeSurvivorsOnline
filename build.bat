@echo off
setlocal

:: Settings
set BIN_DIR=bin
set CC=gcc
set CFLAGS=-Wall -Iraylib/src -Isource
set LDFLAGS=-Lraylib/src -lraylib -lws2_32 -lgdi32 -lwinmm

echo [BUILD] Creating bin directory...
if not exist %BIN_DIR% mkdir %BIN_DIR%

echo [BUILD] Compiling...
%CC% source/main.c source/connection/connection.c -o %BIN_DIR%/game.exe %CFLAGS% %LDFLAGS%

if %errorlevel% neq 0 (
    echo [ERROR] Build failed!
    exit /b %errorlevel%
)

echo [SUCCESS] Game built at %BIN_DIR%/game.exe
endlocal
