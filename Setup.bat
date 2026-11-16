@echo off
setlocal EnableExtensions
net session >nul 2>&1
if not %errorlevel%==0 (echo Please right-click Setup.bat and choose Run as administrator.& pause& exit /b 1)

set "SRC=%~dp0"
set "APP_SRC=%SRC%GPUKeepAlive"
set "DEST=%ProgramData%\GPUKeepAlive"
set "LOCALCFG=%LOCALAPPDATA%\GPUKeepAlive"

if not exist "%APP_SRC%\GPUKeepAlive.exe" (
	echo GPUKeepAlive.exe is missing. The extracted package is incomplete.
	pause
	exit /b 1
)

if not exist "%LOCALCFG%" mkdir "%LOCALCFG%" || exit /b 1

if not exist "%LOCALCFG%\tray.ini" (
	> "%LOCALCFG%\tray.ini" echo [General]
	>> "%LOCALCFG%\tray.ini" echo Tray=1
)

if not exist "%LOCALCFG%\interval.cfg" (
	> "%LOCALCFG%\interval.cfg" echo ; GPUKeepAlive refresh interval
	>> "%LOCALCFG%\interval.cfg" echo ; Supported values: 30, 60, 120, 300, 600, or any positive number of seconds.
	>> "%LOCALCFG%\interval.cfg" echo ; Default: 120
	>> "%LOCALCFG%\interval.cfg" echo 120
)

if not exist "%DEST%" mkdir "%DEST%" || exit /b 1

echo GPUKeepAlive setup
echo Installation directory: %DEST%
echo Configuration directory: %LOCALCFG%

copy /Y "%APP_SRC%\GPUKeepAlive.exe" "%DEST%\GPUKeepAlive.exe" >nul || exit /b 1
copy /Y "%APP_SRC%\LICENSE" "%DEST%\LICENSE" >nul
copy /Y "%APP_SRC%\Uninstall.bat" "%DEST%\Uninstall.bat" >nul

schtasks /Delete /TN "GPUKeepAlive" /F >nul 2>&1
schtasks /Create /TN "GPUKeepAlive" /SC ONLOGON /RU "INTERACTIVE" /RL LIMITED /TR "%DEST%\GPUKeepAlive.exe" /F >nul
if errorlevel 1 (
	echo Task Scheduler registration failed.
	exit /b 1
)

set "STARTMENU=%APPDATA%\Microsoft\Windows\Start Menu\Programs\GPUKeepAlive Service.lnk"
set "SHORTCUT_TARGET=%DEST%\GPUKeepAlive.exe"

powershell -NoProfile -Command "$WshShell = New-Object -ComObject WScript.Shell; $Shortcut = $WshShell.CreateShortcut('%STARTMENU%'); $Shortcut.TargetPath = '%SHORTCUT_TARGET%'; $Shortcut.WorkingDirectory = '%DEST%'; $Shortcut.IconLocation = '%SHORTCUT_TARGET%,0'; $Shortcut.Save()" >nul 2>&1

echo Installed to: %DEST%
echo Startup task created. It will run at the next user logon.
echo Start Menu shortcut created: GPUKeepAlive Service

explorer "%DEST%"
pause