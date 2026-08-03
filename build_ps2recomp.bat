@echo off
set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist %VCVARS% (
    call %VCVARS%
    cd third_party\PS2Recomp
    echo Running CMake configuration...
    cmake -S . -B build
    if %errorlevel% neq 0 exit /b %errorlevel%
    echo Running CMake build...
    cmake --build build --config Release
    if %errorlevel% neq 0 exit /b %errorlevel%
    cd ..\..
    echo Copying binaries to tools/
    copy third_party\PS2Recomp\build\Release\ps2xAnalyzer.exe tools\
    copy third_party\PS2Recomp\build\Release\ps2xRecomp.exe tools\
    echo Build successful.
) else (
    echo Error: vcvars64.bat not found at %VCVARS%
    exit /b 1
)
