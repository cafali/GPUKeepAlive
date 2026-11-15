@echo off
setlocal

:: Check for administrator privileges
net session >nul 2>&1
if not %errorlevel%==0 (
    echo Please run as administrator.
    pause
    exit /b 1
)

echo Stopping GpuKeepAlive process...

:: Kill the running process before deleting files
taskkill /F /IM GpuKeepAlive.exe >nul 2>&1

:: Stop the scheduled task
schtasks /End /TN "GpuKeepAlive" >nul 2>&1

:: Delete the scheduled task
schtasks /Delete /TN "GpuKeepAlive" /F >nul 2>&1

:: Remove installed files
if exist "%ProgramData%\GpuKeepAlive" (
    rmdir /S /Q "%ProgramData%\GpuKeepAlive"
)

echo.
echo GpuKeepAlive successfully uninstalled.
echo - Process terminated.
echo - Scheduled task stopped and deleted.
echo - Installed files removed.

pause