@echo off
setlocal enabledelayedexpansion

cd /d "%~dp0"

echo Calling pack_examples.bat menu...
echo.

REM Call the menu section directly
REM We'll set up the environment and jump to the menu label

echo Checking Python...
set PYTHON_CMD=

REM Try python command
python --version >nul 2>&1
if %errorlevel% equ 0 (
    echo Found Python: python
    set PYTHON_CMD=python
    goto :menu
)

REM Try python3 command
python3 --version >nul 2>&1
if %errorlevel% equ 0 (
    echo Found Python: python3
    set PYTHON_CMD=python3
    goto :menu
)

REM Try py command (Python Launcher)
py --version >nul 2>&1
if %errorlevel% equ 0 (
    echo Found Python: py
    set PYTHON_CMD=py
    goto :menu
)

echo [ERROR] Python not found!
pause
exit /b 1

:menu
echo Using Python: !PYTHON_CMD!
echo.

REM Now simulate the menu
cls
echo ========================================
echo Firmware Packer (Menu Simulation)
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
set /p choice="Enter option (0-9): "

REM Remove all spaces from input
set choice=%choice: =%

echo.
echo You selected: %choice%
echo.

REM Now process the choice
if "%choice%"=="7" (
    echo Processing option 7...
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

echo Invalid option selected: %choice%
pause
exit /b 1