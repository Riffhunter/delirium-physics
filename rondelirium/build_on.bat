@echo off
rem Build RoNDelirium against another UE4SS commit, for players whose other mods pin it: build_on.bat <commit>
rem A checkout of that commit in RE-UE4SS-<commit> (a git worktree of RE-UE4SS, with its submodules), built in build-<commit>.
rem Result: build-<commit>\RoNDelirium\RoNDelirium.dll (main.dll). delirium_physics.dll doesn't depend on UE4SS.
if "%~1"=="" (echo usage: build_on.bat ^<UE4SS commit^> & exit /b 1)
call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set "PATH=%USERPROFILE%\.cargo\bin;%PATH%"
cd /d "%~dp0"
if not exist "RE-UE4SS-%~1\" (
    git -C RE-UE4SS worktree add --detach "..\RE-UE4SS-%~1" %~1 || exit /b 1
    git -C "RE-UE4SS-%~1" submodule update --init --reference "%~dp0RE-UE4SS\deps\first\Unreal" deps/first/Unreal || exit /b 1
    git -C "RE-UE4SS-%~1" submodule update --init --reference "%~dp0RE-UE4SS\deps\first\patternsleuth" deps/first/patternsleuth || exit /b 1
)
if not exist "build-%~1\build.ninja" cmake -B "build-%~1" -G Ninja -DCMAKE_BUILD_TYPE=Game__Shipping__Win64 -DUE4SS_SOURCE=RE-UE4SS-%~1 || exit /b 1
cmake --build "build-%~1" --target RoNDelirium || exit /b 1
