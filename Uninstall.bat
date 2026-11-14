@echo off
setlocal
net session >nul 2>&1
if not %errorlevel%==0 (echo Please run as administrator.& pause& exit /b 1)
schtasks /End /TN "GpuKeepAlive" >nul 2>&1
schtasks /Delete /TN "GpuKeepAlive" /F >nul 2>&1
if exist "%ProgramData%\GpuKeepAlive" rmdir /S /Q "%ProgramData%\GpuKeepAlive"
echo GpuKeepAlive startup task and installed files removed.
pause
