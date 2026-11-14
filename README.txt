GpuKeepAlive
https://github.com/cafali/GpuKeepAlive

What it does
------------------------------------
GpuKeepAlive is a small Windows background utility. It creates a tiny off-screen
Direct3D 11 rendering workload twice per second. There is no visible render
window. The intention is to discourage some GPUs/drivers from entering their
lowest idle states.

IMPORTANT: This is an experiment/workaround, NOT a repair for a faulty GPU,
power supply, motherboard, driver, firmware, or PCIe connection. It cannot
promise to prevent TDRs, freezes, or restarts. It may increase idle power use.
Stop it if temperatures, power use, noise, or stability get worse. Do not use
this tool instead of diagnosing a recurring hardware fault.

Run
------------------------------------
Run GpuKeepAlive.exe. It starts in the background. If tray.ini says Tray=1,
a notification-area icon is shown. Right-click it for Info or Exit. Info opens
this file. If Tray=0, it runs without a tray icon; use Task Manager to exit it.

tray.ini
------------------------------------
[General]
Tray=1

Set Tray=1 to show the icon, or Tray=0 to hide it. Restart the app after edits.

Startup setup
------------------------------------
1. Build the application (or obtain a prebuilt GpuKeepAlive.exe).
2. Right-click Setup.bat and choose Run as administrator.
3. Choose 1 for tray icon or 0 for hidden background operation.
4. Setup copies the app to %ProgramData%\GpuKeepAlive and creates a Task
   Scheduler logon task using the interactive logged-in user (not SYSTEM).
5. setup.log records installation path, time, and tray selection.

Run Uninstall.bat as administrator to remove the scheduled task and installed
folder. The portable executable can also be run directly without setup.

Requirements
------------------------------------
Windows 10/11 x64, a working Direct3D 11-capable graphics driver.
