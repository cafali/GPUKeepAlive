@echo off
setlocal
where cmake >nul 2>nul || (echo CMake not found. Install Visual Studio 2022 with Desktop development with C++ and CMake. & exit /b 1)
cmake -S . -B build -A x64 -DCMAKE_BUILD_TYPE=Release || exit /b 1
cmake --build build --config Release || exit /b 1
copy /Y build\Release\GPUKeepAlive.exe .\GPUKeepAlive.exe >nul || exit /b 1
echo Build complete: GPUKeepAlive.exe
