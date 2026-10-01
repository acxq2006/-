@echo off
rem ============================================================
rem  02_demo - launch the app with Qt DLLs on PATH
rem  NOTE: keep this file pure ASCII, or cmd will garble it.
rem ============================================================

set "QTDIR=E:\QTT\6.11.2\mingw_64"
set "MINGW_BIN=E:\QTT\Tools\mingw1310_64\bin"
set "PATH=%QTDIR%\bin;%MINGW_BIN%;%PATH%"

set "HERE=%~dp0"
set "EXE=%HERE%build\Desktop_Qt_6_11_2_MinGW_64_bit_Debug\02_demo.exe"

if not exist "%EXE%" (
    echo [ERROR] Executable not found:
    echo         %EXE%
    echo.
    echo Please build first: double-click build.bat
    echo.
    pause
    exit /b 1
)

start "" "%EXE%"
exit /b 0
