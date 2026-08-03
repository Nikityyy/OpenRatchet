@echo off
set VCVARS="C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist %VCVARS% (
    call %VCVARS%
    echo Building OpenRatchet via build.py
    python tools/build.py --ps2recomp-dir third_party/PS2Recomp
) else (
    echo Error: vcvars64.bat not found at %VCVARS%
    exit /b 1
)
