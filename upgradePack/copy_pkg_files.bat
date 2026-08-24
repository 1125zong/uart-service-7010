@echo off
setlocal enabledelayedexpansion

echo Running the pkg file copy script...
echo.

REM Check if PowerShell is available
powershell -Command "Get-Host" >nul 2>&1
if errorlevel 1 (
    echo Error: PowerShell is not available.
    pause
    exit /b 1
)

REM Run PowerShell script
powershell -ExecutionPolicy Bypass -File "%~dp0copy_pkg_files.ps1"

echo.
echo Script execution completed.
pause