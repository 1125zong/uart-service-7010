@echo off
setlocal enabledelayedexpansion

echo ========================================
echo Python Detection Tool
echo ========================================
echo.

echo Checking for Python installations...
echo.

REM Check common Python installation paths
set PYTHON_PATHS[0]=C:\Python311\python.exe
set PYTHON_PATHS[1]=C:\Python310\python.exe
set PYTHON_PATHS[2]=C:\Python39\python.exe
set PYTHON_PATHS[3]=C:\Python38\python.exe
set PYTHON_PATHS[4]=C:\Python312\python.exe
set PYTHON_PATHS[5]=C:\Program Files\Python311\python.exe
set PYTHON_PATHS[6]=C:\Program Files\Python310\python.exe
set PYTHON_PATHS[7]=C:\Program Files\Python39\python.exe
set PYTHON_PATHS[8]=C:\Program Files\Python38\python.exe
set PYTHON_PATHS[9]=C:\Program Files\Python312\python.exe
set PYTHON_PATHS[10]=C:\Users\%USERNAME%\AppData\Local\Programs\Python\Python311\python.exe
set PYTHON_PATHS[11]=C:\Users\%USERNAME%\AppData\Local\Programs\Python\Python310\python.exe
set PYTHON_PATHS[12]=C:\Users\%USERNAME%\AppData\Local\Programs\Python\Python39\python.exe
set PYTHON_PATHS[13]=C:\Users\%USERNAME%\AppData\Local\Programs\Python\Python312\python.exe

set FOUND=0
for /L %%i in (0,1,13) do (
    if !FOUND! equ 0 (
        set P=!PYTHON_PATHS[%%i]!
        if exist "!P!" (
            echo Found Python at: !P!
            "!P!" --version
            if !errorlevel! equ 0 (
                set FOUND=1
                set PYTHON_CMD=!P!
            )
        )
    )
)

if %FOUND% equ 0 (
    echo.
    echo Python not found in common locations.
    echo.
    echo Please manually specify Python path:
    echo Example: C:\Python311\python.exe
    echo.
    set /p PYTHON_PATH="Enter Python path (or press Enter to skip): "
    if defined PYTHON_PATH (
        if exist "%PYTHON_PATH%" (
            echo Found Python at: %PYTHON_PATH%
            "%PYTHON_PATH%" --version
            if !errorlevel! equ 0 (
                set FOUND=1
                set PYTHON_CMD=%PYTHON_PATH%
            ) else (
                echo [ERROR] Failed to run Python
            )
        ) else (
            echo [ERROR] File not found: %PYTHON_PATH%
        )
    )
)

echo.
if %FOUND% equ 1 (
    echo ========================================
    echo Python found: %PYTHON_CMD%
    echo ========================================
    echo.
    echo Please update pack_examples.bat to use this path:
    echo set PYTHON_CMD=%PYTHON_CMD%
    echo.
) else (
    echo ========================================
    echo Python not found!
    echo ========================================
    echo.
    echo Please install Python 3.x from:
    echo https://www.python.org/downloads/
    echo.
    echo Make sure to check "Add Python to PATH" during installation.
    echo.
)

pause