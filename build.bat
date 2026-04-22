@echo off
setlocal

set NINJA=C:\Users\Nyu\AppData\Local\Programs\CLion\bin\ninja\win\x64\ninja.exe
set CL=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64\cl.exe
set RC=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\rc.exe
set MT=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\mt.exe

if not exist cmake-build-debug (
    cmake -B cmake-build-debug -G Ninja ^
        -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
        -DCMAKE_CXX_COMPILER="%CL%" ^
        -DCMAKE_RC_COMPILER="%RC%" ^
        -DCMAKE_MT="%MT%"
)

cmake --build cmake-build-debug
if %ERRORLEVEL% == 0 (
    echo.
    echo Build successful: cmake-build-debug\2026_wowhc_launcher.exe
) else (
    echo Build FAILED
)
endlocal
