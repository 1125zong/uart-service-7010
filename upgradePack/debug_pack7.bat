@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

REM Get script path using %0, then extract directory
for %%F in ("%~f0") do set "SCRIPT_PATH=%%~fF"
for %%D in ("%SCRIPT_PATH%") do set "SCRIPT_DIR=%%~dpD"
set "SCRIPT_DIR=!SCRIPT_DIR:~0,-1!"
pushd "!SCRIPT_DIR!"

echo Checking Python...
set PYTHON_CMD=

REM Try python command
python --version >nul 2>&1
if %errorlevel% equ 0 (
    echo Found Python: python
    set PYTHON_CMD=python
    goto :found
)

REM Try python3 command
python3 --version >nul 2>&1
if %errorlevel% equ 0 (
    echo Found Python: python3
    set PYTHON_CMD=python3
    goto :found
)

REM Try py command (Python Launcher)
py --version >nul 2>&1
if %errorlevel% equ 0 (
    echo Found Python: py
    set PYTHON_CMD=py
    goto :found
)

echo [ERROR] Python not found!
pause
exit /b 1

:found
echo Using Python: !PYTHON_CMD!

REM Simulate menu choice
cls
echo ========================================
echo Firmware Packer
echo ========================================
echo Current directory: %CD%
echo.
echo Available files:
if exist top_7015.bit echo   - top_7015.bit (Bitstream)
if exist os.bin echo   - os.bin (OS)
if exist app.bin echo   - app.bin (App)
if exist image.ub echo   - image.ub (Kernel)
if exist rootfs.jffs2 echo   - rootfs.jffs2 (RootFS)
echo.
echo Please select packing mode:
echo.
echo 1. Pack all components (Bitstream + OS + App + Kernel + RootFS)
echo 2. Pack Bitstream only
echo 3. Pack OS only
echo 4. Pack App only
echo 5. Pack Kernel only (image.ub)
echo 6. Pack RootFS only (rootfs.jffs2)
echo 7. Pack Bitstream + Kernel + RootFS (Generate upgrade.pkg)
echo 8. Pack Bitstream and OS
echo 9. Pack OS and App
echo 0. Exit
echo.

set choice=7
echo Simulating selection: !choice!
echo.

echo Checking if "!choice!" equals "7"...
if "!choice!"=="7" (
    echo Match found! Executing option 7...
    echo.
    echo Command: !PYTHON_CMD! firmware_packer.py --bitstream top_7015.bit --kernel image.ub --rootfs rootfs.jffs2 -o upgrade.pkg
    echo.
    !PYTHON_CMD! firmware_packer.py --bitstream top_7015.bit --kernel image.ub --rootfs rootfs.jffs2 -o upgrade.pkg
    set RESULT=!ERRORLEVEL!
    echo.
    echo Command result: !RESULT!
    if !RESULT! neq 0 (
        echo.
        echo [ERROR] Packing failed! Error code: !RESULT!
        echo.
        echo Please check the error messages above for details.
        echo.
        pause
        exit /b 1
    )
    echo.
    echo ========================================
    echo Packing completed!
    echo ========================================
    pause
    exit /b 0
)

echo No match found for option !choice!
pause
exit /b 1