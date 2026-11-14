<img width="1920" height="600" alt="GPUKeepAlive" src="https://github.com/user-attachments/assets/07066416-0a87-42d0-81e8-18ce403517f1" />


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


Idea
------------------------------------
This software was created to address a frustrating and difficult-to-diagnose NVIDIA GPU stability issue, commonly associated with nvlddmkm.sys TDR errors. After extensive hardware replacements, driver changes, BIOS updates, and software troubleshooting failed to resolve random system freezes during low-power scenarios, a different approach was needed. The core idea is to develop a lightweight background application that keeps the GPU in a more stable power state by maintaining minimal GPU activity through a virtually invisible DirectX 3D workload. The application is designed to run automatically in the background, consume as few system resources as possible, and require virtually no user interaction. Ultimately, the goal is to provide a simple, portable, open-source solution that may prevent GPU driver resets caused by problematic low-power state transitions.

> [!CAUTION]
>This solution is not guaranteed to work for everyone, as **nvlddmkm.sys** TDR errors can have different causes and occur under different conditions, including gaming and high GPU loads. This approach is specifically designed for users experiencing the opposite scenario: GPU crashes during low-power states, idle usage, or lightweight tasks such as watching videos, browsing the web, or using the desktop normally. The software aims to address this particular behavior by preventing the GPU from entering potentially problematic lowest-power states.

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
folder. 
> [!NOTE]
>The portable executable can also be run directly without setup.

Requirements
------------------------------------
Windows 10/11 x64, a working Direct3D 11-capable graphics driver.
