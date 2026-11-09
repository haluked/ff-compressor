@echo off
chcp 65001 >nul
cd /d "%~dp0"

if not exist "tests\build" mkdir "tests\build"

gcc -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter ^
    -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 ^
    -Isrc ^
    tests\test_core.c src\probe.c src\util.c src\strings.c src\cjson.c ^
    -o tests\build\test_core.exe ^
    -lshell32 -ladvapi32 -lgdi32 -luser32

if errorlevel 1 (
    echo [ERROR] Test compilation failed!
    exit /b 1
)

tests\build\test_core.exe
if errorlevel 1 exit /b 1
