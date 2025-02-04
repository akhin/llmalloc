@echo off

REM Change vars accordingly to your MSVC installation
set "VS_PATH=C:\Program Files\Microsoft Visual Studio"
set "VS_VERSION=2022"
set "VS_EDITION=Community"

if not exist "%VS_PATH%\%VS_VERSION%\%VS_EDITION%\VC\Auxiliary\Build\vcvarsall.bat" (
    echo Can't find VS%VS_VERSION% command prompt in %VS_PATH%.
    echo Please check your VS installation and update the script accordingly.
    pause
    exit /b 1
)

call "%VS_PATH%\%VS_VERSION%\%VS_EDITION%\VC\Auxiliary\Build\vcvarsall.bat" x64

REM Set the console color to yellow
color 0E

set "TRANSLATION_UNIT_NAME=benchmark_llmalloc"

REM Build the C++ file using MSVC, no O3 in MSVC
cl.exe /EHsc /I"./" /I"../" /I"../../" /std:c++17 /D NDEBUG /O2 %TRANSLATION_UNIT_NAME%.cpp /Fe:%TRANSLATION_UNIT_NAME%.exe /link /subsystem:console /DEFAULTLIB:Advapi32.lib user32.lib

REM Delete the object file generated during compilation
del %TRANSLATION_UNIT_NAME%.obj

set "TRANSLATION_UNIT_NAME=benchmark_intelonetbb"

REM Build the C++ file using MSVC, no O3 in MSVC
cl.exe /EHsc /I"./" /I"../" /I"./intelonetbb/include/" /std:c++17 /D NDEBUG /O2 %TRANSLATION_UNIT_NAME%.cpp /Fe:%TRANSLATION_UNIT_NAME%.exe /link /subsystem:console /DEFAULTLIB:Advapi32.lib user32.lib /LIBPATH:"./intelonetbb/lib/windows11/"

REM Delete the object file generated during compilation
del %TRANSLATION_UNIT_NAME%.obj

copy /Y ".\intelonetbb\tbbmalloc.dll" "."

REM Pause the script so you can see the build output
pause