@echo off
rem Configure and build RoNDelirium (and UE4SS, which it links against) with the VS Build Tools' CMake + Ninja.
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"
cd /d "%~dp0"
if not exist build\build.ninja cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Game__Shipping__Win64 || exit /b 1
cmake --build build --target RoNDelirium || exit /b 1
