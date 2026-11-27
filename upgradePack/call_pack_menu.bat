@echo off
cd /d %~dp0

echo ========================================
echo Firmware Packer (App + RootFS)
echo ========================================
echo.

echo Checking Python...
py --version >nul 2>&1
if errorlevel 1 (
    echo ERROR: Python not found
    echo Please install Python 3.x from https://www.python.org/downloads/
    pause
    exit /b 1
)

echo Found Python: py
echo.

set "APP_FILE=uart_service"
set "ROOTFS_FILE=rootfs.ext2"

:menu
cls
echo ========================================
echo Firmware Packer (App + RootFS)
echo ========================================
echo Current directory: %CD%
echo.
echo Available files:
if exist %APP_FILE% echo   - %APP_FILE% (App)
if exist %ROOTFS_FILE% echo   - %ROOTFS_FILE% (RootFS)
echo.
echo Please select packing mode:
echo.
echo 1. Pack App + RootFS (%APP_FILE% + %ROOTFS_FILE%)
echo 2. Pack App only (%APP_FILE%)
echo 3. Pack RootFS only (%ROOTFS_FILE%)
echo 4. Custom packing (any file names / paths)
echo 0. Exit
echo.
set /p choice="Enter option: "

set choice=%choice: =%

if "%choice%"=="1" goto option1
if "%choice%"=="2" goto option2
if "%choice%"=="3" goto option3
if "%choice%"=="4" goto option4
if "%choice%"=="0" goto exit
goto invalid

:option1
echo.
echo Packing App + RootFS...
py firmware_packer.py --app "%APP_FILE%" --rootfs "%ROOTFS_FILE%" -o app_rootfs.pkg
if errorlevel 1 (
    echo.
    echo ERROR: Packing failed
    pause
    goto menu
)
echo.
echo SUCCESS: Packing completed
pause
goto menu

:option2
echo.
echo Packing App only...
py firmware_packer.py --app "%APP_FILE%" -o app_only.pkg
if errorlevel 1 (
    echo.
    echo ERROR: Packing failed
    pause
    goto menu
)
echo.
echo SUCCESS: Packing completed
pause
goto menu

:option3
echo.
echo Packing RootFS only...
py firmware_packer.py --rootfs "%ROOTFS_FILE%" -o rootfs_only.pkg
if errorlevel 1 (
    echo.
    echo ERROR: Packing failed
    pause
    goto menu
)
echo.
echo SUCCESS: Packing completed
pause
goto menu

:option4
echo.
echo Custom packing
echo Enter file path for App and/or RootFS, press Enter to skip.
echo.
set "APP_INPUT="
set "ROOTFS_INPUT="
set "OUTPUT_INPUT="
set /p "APP_INPUT=App file             : "
set /p "ROOTFS_INPUT=RootFS file       : "
set /p "OUTPUT_INPUT=Output package [custom.pkg]: "
if "%OUTPUT_INPUT%"=="" set "OUTPUT_INPUT=custom.pkg"

set "ARGS="
if defined APP_INPUT set "ARGS=%ARGS% --app "%APP_INPUT%""
if defined ROOTFS_INPUT set "ARGS=%ARGS% --rootfs "%ROOTFS_INPUT%""

if not defined ARGS (
    echo.
    echo ERROR: Please specify at least one component.
    pause
    goto menu
)

echo.
py firmware_packer.py %ARGS% -o "%OUTPUT_INPUT%"
if errorlevel 1 (
    echo.
    echo ERROR: Packing failed
    pause
    goto menu
)
echo.
echo SUCCESS: Packing completed
pause
goto menu

:invalid
echo.
echo ERROR: Invalid option! Please enter 0, 1, 2, 3 or 4.
pause
goto menu

:exit
echo Exit
pause
exit /b 0
