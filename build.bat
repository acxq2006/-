@echo off
rem ============================================================
rem  02_demo - build from command line (no Qt Creator needed)
rem  NOTE: keep this file pure ASCII, or cmd will garble it.
rem ============================================================

set "CMAKE=E:\QTT\Tools\CMake_64\bin\cmake.exe"
set "GXX=E:\QTT\Tools\mingw1310_64\bin\g++.exe"
set "GCC=E:\QTT\Tools\mingw1310_64\bin\gcc.exe"
set "NINJA=C:\mingw64\bin\ninja.exe"
set "QTDIR=E:\QTT\6.11.2\mingw_64"
set "MINGW_BIN=E:\QTT\Tools\mingw1310_64\bin"

set "PATH=%QTDIR%\bin;%MINGW_BIN%;C:\mingw64\bin;%PATH%"

rem %~dp0 ends with a backslash, which would escape the closing quote
rem of "%SRC%". Append a dot so the path never ends with a backslash.
set "HERE=%~dp0"
set "SRC=%HERE%."
set "BUILD=%HERE%build\Desktop_Qt_6_11_2_MinGW_64_bit_Debug"

echo ========== Configure ==========
"%CMAKE%" -S "%SRC%" -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER="%GCC%" -DCMAKE_CXX_COMPILER="%GXX%" -DCMAKE_MAKE_PROGRAM="%NINJA%" -DCMAKE_PREFIX_PATH="%QTDIR%"
if errorlevel 1 goto fail

echo.
echo ========== Build ==========
"%CMAKE%" --build "%BUILD%"
if errorlevel 1 goto fail

echo.
echo [OK] Build finished. Double-click run.bat to launch.
echo.
pause
exit /b 0

:fail
echo.
echo [FAILED] See the error messages above.
echo.
pause
exit /b 1
