@echo off
setlocal EnableExtensions
net session >nul 2>&1
if not %errorlevel%==0 (echo Please right-click Setup.bat and choose Run as administrator.& pause& exit /b 1)
set "SRC=%~dp0"
set "DEST=%ProgramData%\GpuKeepAlive"
if not exist "%SRC%GpuKeepAlive.exe" (echo GpuKeepAlive.exe is missing. Run Build.bat first.& pause& exit /b 1)
echo GpuKeepAlive setup
echo 1 = show tray icon, 0 = run hidden
choice /C 10 /N /M "Choose tray visibility (1 or 0): "
if errorlevel 2 (set "TRAY=0") else set "TRAY=1"
if not exist "%DEST%" mkdir "%DEST%" || exit /b 1
copy /Y "%SRC%GpuKeepAlive.exe" "%DEST%\GpuKeepAlive.exe" >nul || exit /b 1
copy /Y "%SRC%README.txt" "%DEST%\README.txt" >nul
copy /Y "%SRC%LICENSE" "%DEST%\LICENSE" >nul
copy /Y "%SRC%Uninstall.bat" "%DEST%\Uninstall.bat" >nul
>"%DEST%\tray.ini" echo [General]
>>"%DEST%\tray.ini" echo Tray=%TRAY%
schtasks /Delete /TN "GpuKeepAlive" /F >nul 2>&1
schtasks /Create /TN "GpuKeepAlive" /SC ONLOGON /RU "INTERACTIVE" /RL LIMITED /TR "%DEST%\GpuKeepAlive.exe" /F >nul
if errorlevel 1 (echo Task Scheduler registration failed.& exit /b 1)
set "LOG=%DEST%\setup.log"
>>"%LOG%" echo --------------------------------------------------
>>"%LOG%" echo Installed: %DATE% %TIME%
>>"%LOG%" echo Path: %DEST%
>>"%LOG%" echo Tray visible: %TRAY%
>>"%LOG%" echo Startup: Task Scheduler - interactive user logon
>>"%LOG%" echo Result: success
 echo Installed to: %DEST%
echo Tray visible: %TRAY%
echo Startup task created. It will run at the next user logon.

explorer "%DEST%"

pause