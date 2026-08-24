@echo off
cd /d "%~dp0"

REM Check if any arguments provided
if "%~1"=="" (
    echo.
    echo ========================================
    echo Firmware Packer - Usage
    echo ========================================
    echo.
    echo Usage: pack.bat [options] -o output_file.pkg
    echo.
    echo Examples:
    echo.
    echo   Pack all components:
    echo     pack.bat --bitstream bitstream.bin --os os.bin --app app.bin -o update.pkg
    echo.
    echo   Pack App only:
    echo     pack.bat --app app.bin -o app_only.pkg
    echo.
    echo   Pack Bitstream only:
    echo     pack.bat --bitstream bitstream.bin -o bitstream_only.pkg
    echo.
    echo   Pack OS only:
    echo     pack.bat --os os.bin -o os_only.pkg
    echo.
    echo For more options, run:
    echo   pack.bat --help
    echo.
    echo Or use the interactive menu:
    echo   Double-click: pack_examples.bat
    echo.
    pause
    exit /b 0
)

echo Checking Python...

REM Try python command
python --version >nul 2>&1
if not errorlevel 1 (
    echo Found Python: python
    set PYTHON_CMD=python
    goto :found
)

REM Try python3 command
python3 --version >nul 2>&1
if not errorlevel 1 (
    echo Found Python: python3
    set PYTHON_CMD=python3
    goto :found
)

REM Try py command (Python Launcher)
py --version >nul 2>&1
if not errorlevel 1 (
    echo Found Python: py
    set PYTHON_CMD=py
    goto :found
)

REM Not found
echo.
echo [ERROR] Python not found!
echo.
echo Please install Python 3.x first:
echo.
echo Method 1 (Recommended):
echo   1. Visit https://www.python.org/downloads/
echo   2. Download and install Python
echo   3. Make sure to check "Add Python to PATH"
echo.
echo Method 2 (Windows 10/11):
echo   Run in command line: winget install Python.Python.3.11
echo.
pause
exit /b 1

:found
echo.
echo Running: %PYTHON_CMD% firmware_packer.py %*
echo.
%PYTHON_CMD% firmware_packer.py %*

if errorlevel 1 (
    echo.
    echo [ERROR] Packing failed!
    echo.
    pause
    exit /b 1
)

echo.
echo [SUCCESS] Packing completed!
pause