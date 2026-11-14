<img width="1920" height="600" alt="GPUKeepAlive" src="https://github.com/user-attachments/assets/07066416-0a87-42d0-81e8-18ce403517f1" />

TLDR
------------------------------------
Struggling with **NVLDDMKM.SYS** TDR errors that cause your PC to freeze or restart when you're just browsing the web, watching videos, or doing nothing at all? This project aims to solve that problem with a small, lightweight tool that runs quietly in the background and helps keep your graphics card active enough to avoid problematic low-power states.

<img width="1920" height="400" alt="nvlk" src="https://github.com/user-attachments/assets/c99a5010-96e0-43f8-8b94-74e51f8ca832" />


WHEN TO USE?
------------------------------------

✅**CRASHING DURING IDLE & WATCHING VIDEOS:** 
>Yes! This is exactly what the tool is designed for. Use it if your PC crashes while browsing, watching videos, or performing other lightweight tasks.

❌**CRASHING DURING GAMING AND HIGH LOADS:** 
>Not intended for this scenario. This tool is unlikely to help if your crashes happen mainly during gaming or high GPU loads.


Why GpuKeepAlive?
------------------------------------
GpuKeepAlive is a small Windows background utility. It creates a tiny off-screen
Direct3D 11 rendering workload twice per second. There is no visible render
window. The intention is to discourage some GPUs/drivers from entering their
lowest idle states.

Download GPUKeepAlive from GitHub: https://github.com/cafali/GpuKeepAlive/releases

> [!NOTE]
> This is an experiment/workaround, NOT a repair for a faulty GPU,
power supply, motherboard, driver, firmware, or PCIe connection. It cannot
promise to prevent TDRs, freezes, or restarts. It may increase idle power use.
Stop it if temperatures, power use, noise, or stability get worse. Do not use
this tool instead of diagnosing a recurring hardware fault.

<img width="1770" height="582" alt="image" src="https://github.com/user-attachments/assets/d811fc98-6f6b-4121-bcc3-3305d5d648ec" />


Idea
------------------------------------
This software was created to address a frustrating and difficult-to-diagnose NVIDIA GPU stability issue, commonly associated with **NVLDDMKM.SYS** TDR errors. After extensive hardware replacements, driver changes, BIOS updates, and software troubleshooting failed to resolve random system freezes during low-power scenarios, a different approach was needed. The core idea is to develop a lightweight background application that keeps the GPU in a more stable power state by maintaining minimal GPU activity through a virtually invisible DirectX 3D workload. The application is designed to run automatically in the background, consume as few system resources as possible, and require virtually no user interaction. Ultimately, the goal is to provide a simple, portable, open-source solution that may prevent GPU driver resets caused by problematic low-power state transitions.

> [!CAUTION]
>This solution is not guaranteed to work for everyone, as **NVLDDMKM.SYS** TDR errors can have different causes and occur under different conditions, including gaming and high GPU loads. This approach is specifically designed for users experiencing the opposite scenario: GPU crashes during low-power states, idle usage, or lightweight tasks such as watching videos, browsing the web, or using the desktop normally. The software aims to address this particular behavior by preventing the GPU from entering potentially problematic lowest-power states.

Run
------------------------------------
Run GpuKeepAlive.exe. It starts in the background. If tray.ini says Tray=1,
a notification-area icon is shown. Right-click it for Info or Exit. Info opens
the README file. If Tray=0, it runs without a tray icon; use Task Manager to exit it.

tray.ini
------------------------------------
[General]
Tray=1

Set Tray=1 to show the icon, or Tray=0 to hide it. Restart the app after edits.

<img width="306" height="247" alt="image" src="https://github.com/user-attachments/assets/5e74071c-be36-4dc4-a39d-022c19d3037a" />


Startup setup
------------------------------------
1. Build the application (or obtain a prebuilt GpuKeepAlive.exe).
2. Right-click Setup.bat and choose Run as administrator.
3. Choose 1 for tray icon or 0 for hidden background operation.
4. Setup copies the app to %ProgramData%\GpuKeepAlive and creates a Task
   Scheduler logon task using the interactive logged-in user (not SYSTEM).
5. setup.log records installation path, time, and tray selection.

<img width="1282" height="112" alt="image" src="https://github.com/user-attachments/assets/814b778d-b8c0-4e17-a091-e923aec0fb2e" />

Run Uninstall.bat as administrator to remove the scheduled task and installed
folder. 
> [!NOTE]
>The portable executable can also be run directly without setup.

Requirements
------------------------------------
Windows 10/11 x64, a working Direct3D 11-capable graphics driver.

PERSONAL OBSERVATION – ON MY PREVIOUS HARDWARE
------------------------------------
My problems started after upgrading to a newer GPU. Since then, I've been experiencing random system freezes and restarts related to NVLDDMKM.SYS TDR errors. What makes this issue particularly strange is that it almost never happens while gaming or running GPU-intensive applications. Instead, it happens during everyday activities such as watching YouTube or Twitch, browsing the internet, or simply leaving the PC idle.

I've spent countless hours troubleshooting, replacing hardware, testing different GPUs and power supplies, reinstalling Windows, changing drivers, updating BIOS versions, and trying various software-based fixes. Nothing has permanently solved the problem.

Based on my personal observations and my NVLDDMKM.SYS scenario, I suspect the problem might be related to how NVIDIA GPUs handle certain low-power states on my particular hardware configuration. The system appears to be stable under higher GPU loads but becomes unpredictable when the GPU is barely being used.

This observation became the starting point for this project: instead of continuing to troubleshoot drivers and hardware endlessly, I wanted to try a different approach – keeping the GPU slightly active in the background to potentially avoid problematic low-power transitions.

**My hope is that this project can help others experiencing the same frustrating issue. If you're dealing with similar crashes, I hope this little tool makes a difference in your case and saves you some of the time, frustration, and endless troubleshooting I've been through.**
