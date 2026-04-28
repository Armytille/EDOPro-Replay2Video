@echo off
setlocal EnableDelayedExpansion

echo ============================================
echo EDOPro Replay2Video - Deploy Package
echo ============================================
echo.

set ROOT=%~dp0
if "%ROOT:~-1%"=="\" set ROOT=%ROOT:~0,-1%

set EXE_SRC=%ROOT%\edopro\bin\x64\release\replay2video.exe
set DLL_SRC=%ROOT%\ffmpeg-dev\bin
set CFG_SRC=%ROOT%\config.ini.example
set DIST=%ROOT%\dist

REM --- Guard: exe must exist ---
if not exist "%EXE_SRC%" (
    echo ERROR: replay2video.exe not found at:
    echo   %EXE_SRC%
    echo Run build_windows.bat first.
    exit /b 1
)

REM --- Guard: all required DLLs must exist ---
set MISSING=0
for %%D in (avcodec-62.dll avfilter-11.dll avformat-62.dll avutil-60.dll swresample-6.dll swscale-9.dll) do (
    if not exist "%DLL_SRC%\%%D" (
        echo ERROR: Missing DLL: %DLL_SRC%\%%D
        set MISSING=1
    )
)
if !MISSING! neq 0 exit /b 1

REM --- Clean and recreate dist\ ---
if exist "%DIST%" (
    echo Removing existing dist\ ...
    rmdir /S /Q "%DIST%"
)
mkdir "%DIST%"
if !ERRORLEVEL! neq 0 (
    echo ERROR: Could not create %DIST%
    exit /b 1
)

REM --- Copy exe ---
echo Copying replay2video.exe...
copy /Y "%EXE_SRC%" "%DIST%\replay2video.exe" >nul
if !ERRORLEVEL! neq 0 ( echo ERROR: copy of replay2video.exe failed & exit /b 1 )

REM --- Copy DLLs ---
echo Copying FFmpeg DLLs...
for %%D in (avcodec-62.dll avfilter-11.dll avformat-62.dll avutil-60.dll swresample-6.dll swscale-9.dll) do (
    copy /Y "%DLL_SRC%\%%D" "%DIST%\%%D" >nul
    if !ERRORLEVEL! neq 0 ( echo ERROR: Failed to copy %%D & exit /b 1 )
    echo   %%D
)

REM --- Copy config example ---
echo Copying config.ini.example...
copy /Y "%CFG_SRC%" "%DIST%\config.ini.example" >nul
if !ERRORLEVEL! neq 0 ( echo ERROR: copy of config.ini.example failed & exit /b 1 )

REM --- Copy presets ---
echo Copying presets\...
xcopy /E /I /Y "%ROOT%\presets" "%DIST%\presets" >nul
if !ERRORLEVEL! neq 0 ( echo ERROR: copy of presets\ failed & exit /b 1 )

REM --- Summary ---
echo.
echo ============================================
echo Package assembled in: %DIST%
echo.
dir /b "%DIST%"
echo.
echo INSTALL: place dist\ contents alongside your EDOPro assets.
echo USE:     replay2video.exe --render-replay duel.yrpX --output duel.mp4 --workdir D:\path\to\EDOPro
echo ============================================
exit /b 0
