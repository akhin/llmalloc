@echo off

:: Use vswhere to find the latest installed Visual Studio
for /f "usebackq tokens=*" %%a in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VS_INSTALL_DIR=%%a"
)

:: Check if the variable was set
if not defined VS_INSTALL_DIR (
    echo Visual Studio installation not found.
    exit /b 1
)

:: Call the developer command prompt
call "%VS_INSTALL_DIR%\Common7\Tools\VsDevCmd.bat" -arch=x64

REM Set the console color to yellow
color 0E

copy /Y ".\mimalloc\mimalloc.lib" "."
copy /Y ".\intelonetbb\tbbmalloc.lib" "."
copy /Y ".\intelonetbb\tbbmalloc_proxy.lib" "."
copy /Y ".\intelonetbb\tbbmalloc.dll" "."
copy /Y ".\intelonetbb\tbbmalloc_proxy.dll" "."

set "TRANSLATION_UNIT_NAME=benchmark_llmalloc"
del %TRANSLATION_UNIT_NAME%.exe
REM Build the C++ file using MSVC, no O3 in MSVC
cl.exe /EHsc /MD /I"../" /std:c++17 /D NDEBUG /O2 %TRANSLATION_UNIT_NAME%.cpp /Fe:%TRANSLATION_UNIT_NAME%.exe /link /subsystem:console /DEFAULTLIB:Advapi32.lib
REM Delete the object file generated during compilation
del %TRANSLATION_UNIT_NAME%.obj

set "TRANSLATION_UNIT_NAME=benchmark_mimalloc"
del %TRANSLATION_UNIT_NAME%.exe
REM Build the C++ file using MSVC, no O3 in MSVC
cl.exe /EHsc /I"./mimalloc/include/" /MD /I"../" /std:c++17 /D NDEBUG /O2 %TRANSLATION_UNIT_NAME%.cpp /Fe:%TRANSLATION_UNIT_NAME%.exe /link /subsystem:console /DEFAULTLIB:Advapi32.lib
REM Delete the object file generated during compilation
del %TRANSLATION_UNIT_NAME%.obj

set "TRANSLATION_UNIT_NAME=benchmark_ucrt"
del %TRANSLATION_UNIT_NAME%.exe
REM Build the C++ file using MSVC, no O3 in MSVC
cl.exe /EHsc /I"../" /std:c++17 /D NDEBUG /O2 %TRANSLATION_UNIT_NAME%.cpp /Fe:%TRANSLATION_UNIT_NAME%.exe /link /subsystem:console /DEFAULTLIB:Advapi32.lib
REM Delete the object file generated during compilation
del %TRANSLATION_UNIT_NAME%.obj

set "TRANSLATION_UNIT_NAME=benchmark_inteltbb"
del %TRANSLATION_UNIT_NAME%.exe
REM Build the C++ file using MSVC, no O3 in MSVC
cl.exe /EHsc /I".\intelonetbb\include" /I"./" /I"../" /std:c++17 /D NDEBUG /O2 %TRANSLATION_UNIT_NAME%.cpp /Fe:%TRANSLATION_UNIT_NAME%.exe /link /subsystem:console /DEFAULTLIB:Advapi32.lib
REM Delete the object file generated during compilation
del %TRANSLATION_UNIT_NAME%.obj

REM Pause the script so you can see the build output
pause