@echo off
if exist raylib (
    echo [INFO] raylib folder already exists.
    exit /b 0
)

echo [DOWNLOAD] Cloning raylib from GitHub...
git clone --depth 1 https://github.com/raysan5/raylib.git raylib

if %errorlevel% neq 0 (
    echo [ERROR] Failed to clone raylib.
    exit /b %errorlevel%
)

echo [SUCCESS] raylib downloaded successfully.
