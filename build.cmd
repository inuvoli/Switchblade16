@echo off
setlocal EnableExtensions

rem ============================================================
rem  Build script for sb16 (8051 / SDCC)
rem  Output: build\ = intermediate files, bin\ = final .bin
rem  Tip: set NO_PAUSE=1 before running to skip the final pause
rem       (useful when launching from a terminal or CI)
rem ============================================================

rem ---------- Configuration ----------
set "SDCC_BIN=D:\tools\sdcc\bin"
set "NAME=sb16_3.00"
set "SRC=src\%NAME%.c"
set "BUILD_DIR=build"
set "OUT_DIR=bin"

rem ---------- Move to the script folder ----------
cd /d "%~dp0"
if errorlevel 1 (
    echo [ERROR] Cannot change to the script folder.
    goto :fail
)

rem ---------- Check the toolchain ----------
if not exist "%SDCC_BIN%\sdcc.exe" (
    echo [ERROR] SDCC not found in "%SDCC_BIN%".
    goto :fail
)
set "PATH=%SDCC_BIN%;%PATH%"

rem ---------- Check the source file ----------
if not exist "%SRC%" (
    echo [ERROR] Source file not found: %SRC%
    goto :fail
)

rem ---------- Create output folders if missing ----------
for %%D in ("%BUILD_DIR%" "%OUT_DIR%") do (
    if not exist "%%~D\" mkdir "%%~D"
    if not exist "%%~D\" (
        echo [ERROR] Cannot create folder: %%~D
        goto :fail
    )
)

rem ---------- Remove stale artifacts from previous builds ----------
if exist "%BUILD_DIR%\%NAME%.ihx" del /q "%BUILD_DIR%\%NAME%.ihx"
if exist "%BUILD_DIR%\%NAME%.hex" del /q "%BUILD_DIR%\%NAME%.hex"
if exist "%BUILD_DIR%\%NAME%.bin" del /q "%BUILD_DIR%\%NAME%.bin"

rem ---------- 1) Compile (all generated files go to build\) ----------
echo [1/4] Compiling %SRC% ...
sdcc -mmcs51 --model-small --iram-size 128 --xram-loc 0x7E00 --xram-size 0x200 --code-size 0x8000 -o "%BUILD_DIR%/" "%SRC%"
if errorlevel 1 (
    echo [ERROR] Compilation failed.
    goto :fail
)
if not exist "%BUILD_DIR%\%NAME%.ihx" (
    echo [ERROR] Compiler did not produce %NAME%.ihx
    goto :fail
)

rem ---------- 2) Generate the packed HEX file ----------
echo [2/4] Generating HEX file ...
packihx "%BUILD_DIR%\%NAME%.ihx" > "%BUILD_DIR%\%NAME%.hex"
if errorlevel 1 (
    echo [ERROR] packihx failed.
    del /q "%BUILD_DIR%\%NAME%.hex" 2>nul
    goto :fail
)

rem ---------- 3) Generate the padded BIN file ----------
echo [3/4] Generating BIN file ...
makebin -p "%BUILD_DIR%\%NAME%.ihx" "%BUILD_DIR%\%NAME%.bin"
if errorlevel 1 (
    echo [ERROR] makebin failed.
    goto :fail
)
if not exist "%BUILD_DIR%\%NAME%.bin" (
    echo [ERROR] makebin did not produce %NAME%.bin
    goto :fail
)

rem ---------- 4) Move the BIN file to the output folder ----------
echo [4/4] Moving BIN to %OUT_DIR%\ ...
move /y "%BUILD_DIR%\%NAME%.bin" "%OUT_DIR%\%NAME%.bin" >nul
if errorlevel 1 (
    echo [ERROR] Cannot move the BIN file to %OUT_DIR%\
    goto :fail
)

rem ---------- Success ----------
echo.
echo Build completed successfully.
echo   - Intermediate files and HEX: %BUILD_DIR%\
echo   - Final BIN:                  %OUT_DIR%\%NAME%.bin
if not defined NO_PAUSE pause
endlocal
exit /b 0

rem ---------- Failure handler ----------
:fail
echo.
echo Build FAILED.
if not defined NO_PAUSE pause
endlocal
exit /b 1