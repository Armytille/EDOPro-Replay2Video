@echo off
setlocal EnableDelayedExpansion

echo ============================================
echo EDOPro Replay2Video - Windows Build Script
echo ============================================
echo.

REM Configuration
set EDOPro_REPO=https://github.com/edo9300/edopro.git
set FFMPEG_URL=https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-full-shared.7z
set REPLAY_URL=https://www.quicksendfile.com/download/tcrtv
set PREMAKE_URL=https://github.com/premake/premake-core/releases/download/v5.0.0-beta2/premake-5.0.0-beta2-windows.zip

set ROOT=%CD%
set EDOPro_DIR=%ROOT%\edopro
set FFMPEG_DEV=%ROOT%\ffmpeg-dev
set PREMAKE_DIR=%ROOT%\premake5

REM Step 1: Clone EDOPro with submodules
if not exist "%EDOPro_DIR%\.git" (
    echo [1/6] Cloning EDOPro repository with submodules...
    git clone --recursive %EDOPro_REPO% "%EDOPro_DIR%"
    if !ERRORLEVEL! neq 0 (
        echo ERROR: git clone failed. Ensure git is installed and network is available.
        exit /b 1
    )
) else (
    echo [1/6] EDOPro already cloned, updating submodules...
    cd "%EDOPro_DIR%"
    git submodule update --init --recursive
    if !ERRORLEVEL! neq 0 (
        echo ERROR: git submodule update failed.
        exit /b 1
    )
    cd "%ROOT%"
)

REM Step 2: Download and extract FFmpeg dev libraries
if not exist "%FFMPEG_DEV%\include\libavcodec\avcodec.h" (
    echo [2/6] Downloading FFmpeg development libraries...
    if not exist "%ROOT%\ffmpeg.7z" (
        powershell -Command "Invoke-WebRequest -Uri '%FFMPEG_URL%' -OutFile '%ROOT%\ffmpeg.7z' -UseBasicParsing"
        if !ERRORLEVEL! neq 0 (
            echo ERROR: Failed to download FFmpeg libraries from %FFMPEG_URL%
            echo Please manually download FFmpeg full shared build and extract to %FFMPEG_DEV%
            exit /b 1
        )
    )
    echo [2/6] Extracting FFmpeg libraries...
    if exist "%ProgramFiles%\7-Zip\7z.exe" (
        "%ProgramFiles%\7-Zip\7z.exe" x "%ROOT%\ffmpeg.7z" -o"%ROOT%\ffmpeg-temp" -y
    ) else if exist "%ProgramFiles(x86)%\7-Zip\7z.exe" (
        "%ProgramFiles(x86)%\7-Zip\7z.exe" x "%ROOT%\ffmpeg.7z" -o"%ROOT%\ffmpeg-temp" -y
    ) else (
        echo ERROR: 7-Zip not found. Please install 7-Zip or manually extract ffmpeg.7z to %FFMPEG_DEV%
        exit /b 1
    )
    if !ERRORLEVEL! neq 0 (
        echo ERROR: 7-Zip extraction failed.
        exit /b 1
    )
    REM The extracted folder name varies by version, so we move contents to a stable path
    for /d %%D in ("%ROOT%\ffmpeg-temp\*") do (
        if exist "%%D\include" (
            xcopy /E /I /Y "%%D\*" "%FFMPEG_DEV%\" >nul
        )
    )
    if not exist "%FFMPEG_DEV%\include" (
        echo ERROR: Could not locate extracted FFmpeg include folder.
        exit /b 1
    )
    rmdir /S /Q "%ROOT%\ffmpeg-temp" 2>nul
) else (
    echo [2/6] FFmpeg libraries already present.
)

REM Step 3: Download premake5 if not present
if not exist "%PREMAKE_DIR%\premake5.exe" (
    echo [3/6] Downloading premake5...
    if not exist "%ROOT%\premake.zip" (
        powershell -Command "Invoke-WebRequest -Uri '%PREMAKE_URL%' -OutFile '%ROOT%\premake.zip' -UseBasicParsing"
        if !ERRORLEVEL! neq 0 (
            echo ERROR: Failed to download premake5.
            exit /b 1
        )
    )
    mkdir "%PREMAKE_DIR%" 2>nul
    powershell -Command "Expand-Archive -Path '%ROOT%\premake.zip' -DestinationPath '%PREMAKE_DIR%' -Force"
    if !ERRORLEVEL! neq 0 (
        echo ERROR: Failed to extract premake5.
        exit /b 1
    )
) else (
    echo [3/6] premake5 already present.
)

REM Step 4: Apply patches
echo [4/6] Applying replay2video patches...
cd "%EDOPro_DIR%"

REM Check if patches are already applied by looking for a marker
git log --oneline -1 | findstr "replay2video" >nul
if !ERRORLEVEL! equ 0 (
    echo [4/6] Patches already applied.
) else (
    git apply --check "%ROOT%\patches\game.cpp.patch" 2>nul
    if !ERRORLEVEL! neq 0 (
        echo WARNING: game.cpp patch check failed. It may already be applied or the source has diverged.
    ) else (
        git apply "%ROOT%\patches\game.cpp.patch"
        if !ERRORLEVEL! neq 0 (
            echo ERROR: Failed to apply game.cpp.patch
            exit /b 1
        )
    )

    git apply --check "%ROOT%\patches\replay_mode.cpp.patch" 2>nul
    if !ERRORLEVEL! neq 0 (
        echo WARNING: replay_mode.cpp patch check failed.
    ) else (
        git apply "%ROOT%\patches\replay_mode.cpp.patch"
        if !ERRORLEVEL! neq 0 (
            echo ERROR: Failed to apply replay_mode.cpp.patch
            exit /b 1
        )
    )

    git apply --check "%ROOT%\patches\replay_mode.h.patch" 2>nul
    if !ERRORLEVEL! neq 0 (
        echo WARNING: replay_mode.h patch check failed.
    ) else (
        git apply "%ROOT%\patches\replay_mode.h.patch"
        if !ERRORLEVEL! neq 0 (
            echo ERROR: Failed to apply replay_mode.h.patch
            exit /b 1
        )
    )

    git apply --check "%ROOT%\patches\edopro_main.cpp.patch" 2>nul
    if !ERRORLEVEL! neq 0 (
        echo WARNING: edopro_main.cpp patch check failed.
    ) else (
        git apply "%ROOT%\patches\edopro_main.cpp.patch"
        if !ERRORLEVEL! neq 0 (
            echo ERROR: Failed to apply edopro_main.cpp.patch
            exit /b 1
        )
    )

    git apply --check "%ROOT%\patches\premake5.lua.patch" 2>nul
    if !ERRORLEVEL! neq 0 (
        echo WARNING: premake5.lua patch check failed.
    ) else (
        git apply "%ROOT%\patches\premake5.lua.patch"
        if !ERRORLEVEL! neq 0 (
            echo ERROR: Failed to apply premake5.lua.patch
            exit /b 1
        )
    )

    REM Create a marker commit so we can detect reapplication
    git config user.email "replay2video@local" >nul 2>&1
    git config user.name "replay2video" >nul 2>&1
    git add -A
    git commit -m "replay2video: apply integration patches" --no-verify >nul 2>&1
)

REM Step 5: Copy new source files into edopro tree
echo [5/6] Copying replay2video source files...
if not exist "%EDOPro_DIR%\gframe\replay2video" (
    xcopy /E /I /Y "%ROOT%\gframe\replay2video" "%EDOPro_DIR%\gframe\replay2video"
    if !ERRORLEVEL! neq 0 (
        echo ERROR: Failed to copy replay2video source files.
        exit /b 1
    )
) else (
    echo [5/6] replay2video source files already present.
)

REM Step 6: Build with premake5 + MSBuild or MinGW
echo [6/6] Generating build files and compiling...
cd "%EDOPro_DIR%\gframe"

set PATH=%PREMAKE_DIR%;%PATH%

REM Try VS2022, VS2019, then fallback to gmake2 (MinGW)
premake5 vs2022 --with-replay2video
if !ERRORLEVEL! neq 0 (
    premake5 vs2019 --with-replay2video
    if !ERRORLEVEL! neq 0 (
        premake5 gmake2 --with-replay2video
        if !ERRORLEVEL! neq 0 (
            echo ERROR: premake5 failed to generate build files.
            exit /b 1
        )
    )
)

REM Build using MSBuild if Visual Studio project was generated
if exist "%EDOPro_DIR%\build\ygopro.sln" (
    where msbuild >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        msbuild "%EDOPro_DIR%\build\ygopro.sln" /p:Configuration=Release /p:Platform=x64 /m
        if !ERRORLEVEL! neq 0 (
            echo ERROR: MSBuild compilation failed.
            exit /b 1
        )
        echo.
        echo ============================================
        echo Build completed successfully!
        echo Output: %EDOPro_DIR%\build\bin\Release\ygopro.exe
        echo ============================================
    ) else (
        echo WARNING: msbuild not found in PATH. Please build manually:
        echo   Open %EDOPro_DIR%\build\ygopro.sln in Visual Studio and build Release x64
    )
) else if exist "%EDOPro_DIR%\build\Makefile" (
    where mingw32-make >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        cd "%EDOPro_DIR%\build"
        mingw32-make config=release64
        if !ERRORLEVEL! neq 0 (
            echo ERROR: MinGW compilation failed.
            exit /b 1
        )
        echo.
        echo ============================================
        echo Build completed successfully!
        echo Output: %EDOPro_DIR%\build\bin\Release\ygopro.exe
        echo ============================================
    ) else (
        echo WARNING: mingw32-make not found. Please ensure MinGW-w64 is installed.
    )
) else (
    echo ERROR: No build files generated. Check premake5 output above.
    exit /b 1
)

echo.
echo To assemble a distributable package, run: deploy_package.bat

cd "%ROOT%"
exit /b 0
