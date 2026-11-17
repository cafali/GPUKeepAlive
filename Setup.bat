@echo off
setlocal EnableExtensions
net session >nul 2>&1
if not %errorlevel%==0 (echo Please right-click Setup.bat and choose Run as administrator.& pause& exit /b 1)

set "SRC=%~dp0"
set "APP_SRC=%SRC%GPUKeepAlive"
set "DEST=%ProgramData%\GPUKeepAlive"
set "LOCALCFG=%LOCALAPPDATA%\GPUKeepAlive"
set "STARTMENU=%APPDATA%\Microsoft\Windows\Start Menu\Programs\GPUKeepAlive Service.lnk"

if not exist "%APP_SRC%\GPUKeepAlive.exe" (
	echo GPUKeepAlive.exe is missing. The extracted package is incomplete.
	pause
	exit /b 1
)

if exist "%DEST%" (
	powershell -NoProfile -Command "try { $dest = [IO.Path]::GetFullPath($env:DEST).TrimEnd('\'); $config = [IO.Path]::GetFullPath($env:LOCALCFG).TrimEnd('\'); $source = [IO.Path]::GetFullPath($env:APP_SRC).TrimEnd('\'); foreach ($dir in @($dest, $config)) { if ($source.Equals($dir, [StringComparison]::OrdinalIgnoreCase) -or $source.StartsWith($dir + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'The setup package is inside a directory that must be removed.' } }; Get-CimInstance Win32_Process -Filter 'Name=''GPUKeepAlive.exe''' -ErrorAction Stop | ForEach-Object { if (-not $_.ExecutablePath) { throw 'Cannot determine the path of a GPUKeepAlive process.' }; if ($_.ExecutablePath.Equals(($dest + '\GPUKeepAlive.exe'), [StringComparison]::OrdinalIgnoreCase)) { $process = Stop-Process -Id $_.ProcessId -Force -PassThru -ErrorAction Stop; if (-not $process.WaitForExit(10000)) { throw 'The existing process did not stop.' } } } } catch { Write-Error $_; exit 1 }" >nul
	if errorlevel 1 (
		echo Failed to stop the existing installation or validate the setup package location.
		exit /b 1
	)

	schtasks /End /TN "GPUKeepAlive" >nul 2>&1
	schtasks /Query /TN "GPUKeepAlive" >nul 2>&1
	if not errorlevel 1 (
		schtasks /Delete /TN "GPUKeepAlive" /F >nul 2>&1
		if errorlevel 1 (
			echo Failed to remove the existing startup task.
			exit /b 1
		)
	)

	if exist "%STARTMENU%" del /F /Q "%STARTMENU%" >nul 2>&1
	if exist "%STARTMENU%" (
		echo Failed to remove the existing Start Menu shortcut.
		exit /b 1
	)

	rmdir /S /Q "%DEST%" >nul 2>&1
	if exist "%DEST%" (
		echo Failed to remove the existing installation directory.
		exit /b 1
	)
)

if not exist "%LOCALCFG%" mkdir "%LOCALCFG%" || exit /b 1

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

set "SHORTCUT_TARGET=%DEST%\GPUKeepAlive.exe"

powershell -NoProfile -Command "$WshShell = New-Object -ComObject WScript.Shell; $Shortcut = $WshShell.CreateShortcut('%STARTMENU%'); $Shortcut.TargetPath = '%SHORTCUT_TARGET%'; $Shortcut.WorkingDirectory = '%DEST%'; $Shortcut.IconLocation = '%SHORTCUT_TARGET%,0'; $Shortcut.Save()" >nul 2>&1
echo -----------------------------------
echo Installed to: %DEST%
echo Startup task created. It will run at the next user logon.
echo Start Menu shortcut created: GPUKeepAlive Service
color a
echo -----------------------------------
echo GPUKeepAlive installed
start "" "%DEST%\GPUKeepAlive.exe"
pause