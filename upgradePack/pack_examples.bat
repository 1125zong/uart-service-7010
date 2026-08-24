@echo off
cd /d %~dp0

echo ========================================
echo Firmware Packer
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

:menu
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
set /p choice="Enter option: "

set choice=%choice: =%

if "%choice%"=="1" goto option1
if "%choice%"=="2" goto option2
if "%choice%"=="3" goto option3
if "%choice%"=="4" goto option4
if "%choice%"=="5" goto option5
if "%choice%"=="6" goto option6
if "%choice%"=="7" goto option7
if "%choice%"=="8" goto option8
if "%choice%"=="9" goto option9
if "%choice%"=="0" goto exit
goto invalid

:option1
echo.
echo Packing all components...
py firmware_packer.py --bitstream top_7015.bit --os os.bin --app app.bin --kernel image.ub --rootfs rootfs.jffs2 -o update_full.pkg
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
echo Packing Bitstream only...
py firmware_packer.py --bitstream top_7015.bit -o bitstream_only.pkg
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
echo Packing OS only...
py firmware_packer.py --os os.bin -o os_only.pkg
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
echo Packing App only...
py firmware_packer.py --app app.bin -o app_only.pkg
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

:option5
echo.
echo Packing Kernel only (image.ub)...
py firmware_packer.py --kernel image.ub -o kernel_only.pkg
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

:option6
echo.
echo Packing RootFS only (rootfs.jffs2)...
py firmware_packer.py --rootfs rootfs.jffs2 -o rootfs_only.pkg
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

:option7
echo.
echo Packing Bitstream + Kernel + RootFS (Generating upgrade.pkg)...
py firmware_packer.py --bitstream top_7015.bit --kernel image.ub --rootfs rootfs.jffs2 -o upgrade.pkg
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

:option8
echo.
echo Packing Bitstream and OS...
py firmware_packer.py --bitstream top_7015.bit --os os.bin -o bit_os.pkg
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

:option9
echo.
echo Packing OS and App...
py firmware_packer.py --os os.bin --app app.bin -o os_app.pkg
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
echo ERROR: Invalid option! Please enter a number between 0 and 9.
pause
goto menu

:exit
echo Exit
pause
exit /b 0