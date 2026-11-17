@echo off
setlocal

set "APP_DIR=%ProgramData%\GPUKeepAlive"
set "STARTMENU=%APPDATA%\Microsoft\Windows\Start Menu\Programs\GPUKeepAlive Service.lnk"
set "CONFIG_DIR=%LOCALAPPDATA%\GPUKeepAlive"

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
taskkill /F /IM GPUKeepAlive.exe >nul 2>&1

:: Stop the scheduled task
schtasks /End /TN "GPUKeepAlive" >nul 2>&1

:: Delete the scheduled task
schtasks /Delete /TN "GPUKeepAlive" /F >nul 2>&1

if exist "%STARTMENU%" del /F /Q "%STARTMENU%" >nul 2>&1

:: Remove installed files
if exist "%APP_DIR%" (
    rmdir /S /Q "%APP_DIR%"
)

:: Remove user-specific configuration and metadata
if exist "%CONFIG_DIR%" (
    rmdir /S /Q "%CONFIG_DIR%"
)

if exist "%CONFIG_DIR%" (
    echo Failed to remove configuration files in "%CONFIG_DIR%".
    pause
    exit /b 1
)

echo.
echo GPUKeepAlive installation removed.
echo - Running process terminated.
echo - Scheduled task removed.
echo - Start menu shortcut removed.
echo - set_gpukeepalive.ini and app data in %CONFIG_DIR% removed.

echo.
echo GPUKeepAlive successfully uninstalled.

pause