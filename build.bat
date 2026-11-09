@echo off
chcp 65001 >nul
cd /d "%~dp0"

echo ======================================================
echo [1/3] Compiling Windows resources (windres)
echo ======================================================
if not exist "dist" mkdir "dist"

windres res\res.rc -O coff -o res.o
if errorlevel 1 (
    echo [ERROR] windres resource compilation failed!
    exit /b 1
)

echo ======================================================
echo [2/3] Compiling ff-compressor (native C, GPLv3)
echo ======================================================
gcc -std=c11 -O3 -s -flto ^
    -ffunction-sections -fdata-sections ^
    -Wl,--gc-sections ^
    -static -mwindows -municode ^
    -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 ^
    -Wall -Wextra -Wno-unused-parameter ^
    -Isrc ^
    src\main.c ^
    src\ui.c ^
    src\gb_ui.c ^
    src\engine.c ^
    src\probe.c ^
    src\hw.c ^
    src\presets.c ^
    src\estimate.c ^
    src\encode.c ^
    src\util.c ^
    src\strings.c ^
    src\cjson.c ^
    res.o ^
    -o dist\ff-compressor.exe ^
    -lole32 -luuid -ldxguid -lshell32 -ladvapi32 -lgdi32 -luser32

if errorlevel 1 (
    echo [ERROR] GCC compilation failed!
    exit /b 1
)

echo ======================================================
echo [3/3] Build complete - size verification
echo ======================================================
powershell -NoProfile -Command "$size = (Get-Item 'dist\ff-compressor.exe').Length; $kb = [math]::Round($size / 1KB, 2); Write-Host ('Executable: dist\ff-compressor.exe'); Write-Host ('File size: ' + $size + ' bytes (' + $kb + ' KB)'); if ($size -lt 1MB) { Write-Host '>> OK: the < 1 MB target is met!' -ForegroundColor Green } else { Write-Host '>> ERROR: the size exceeded 1 MB!' -ForegroundColor Red; exit 1 }"
