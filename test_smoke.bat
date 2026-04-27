@echo off
setlocal EnableDelayedExpansion

echo ============================================
echo EDOPro Replay2Video - Smoke Test
echo ============================================
echo.

set ROOT=%CD%
set EDOPro_DIR=%ROOT%\edopro
set EXE=%EDOPro_DIR%\build\bin\Release\ygopro.exe
set TEST_REPLAY=%ROOT%\test_replay.yrpX
set FFMPEG_DIR=%ROOT%\ffmpeg-dev\bin

REM Find ffprobe and ffmpeg
set FFPROBE=%FFMPEG_DIR%\ffprobe.exe
set FFMPEG=%FFMPEG_DIR%\ffmpeg.exe
if not exist "%FFPROBE%" (
    where ffprobe >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        for /f "tokens=*" %%i in ('where ffprobe') do set FFPROBE=%%i
    ) else (
        echo ERROR: ffprobe not found. Please ensure FFmpeg binaries are in PATH or in %FFMPEG_DIR%
        exit /b 1
    )
)
if not exist "%FFMPEG%" (
    where ffmpeg >nul 2>&1
    if !ERRORLEVEL! equ 0 (
        for /f "tokens=*" %%i in ('where ffmpeg') do set FFMPEG=%%i
    ) else (
        echo ERROR: ffmpeg not found. Please ensure FFmpeg binaries are in PATH or in %FFMPEG_DIR%
        exit /b 1
    )
)

REM Check executable exists
if not exist "%EXE%" (
    echo ERROR: Executable not found at %EXE%
    echo Please run build_windows.bat first.
    exit /b 1
)

REM Download test replay if not present
if not exist "%TEST_REPLAY%" (
    echo [TEST] Downloading test replay file...
    powershell -Command "Invoke-WebRequest -Uri 'https://www.quicksendfile.com/download/tcrtv' -OutFile '%TEST_REPLAY%' -UseBasicParsing"
    if !ERRORLEVEL! neq 0 (
        echo ERROR: Failed to download test replay. Please download manually from:
        echo   https://www.quicksendfile.com/download/tcrtv
        echo and save as %TEST_REPLAY%
        exit /b 1
    )
)

set OUT_MP4=%ROOT%\smoke_test_output.mp4

REM Clean previous output
if exist "%OUT_MP4%" del "%OUT_MP4%"

REM Test 1: Full render at 720p 30fps for ~10 seconds
echo [TEST 1] Rendering test replay (720p 30fps)...
"%EXE%" --render-replay "%TEST_REPLAY%" --output "%OUT_MP4%" --resolution 1280x720 --fps 30 --workdir "%EDOPro_DIR%"
if !ERRORLEVEL! neq 0 (
    echo ERROR: Rendering failed with exit code !ERRORLEVEL!
    exit /b 1
)

REM Verify output file exists and size > 10KB
if not exist "%OUT_MP4%" (
    echo ERROR: Output file %OUT_MP4% was not created.
    exit /b 1
)
for %%F in ("%OUT_MP4%") do set SIZE=%%~zF
if !SIZE! lss 10240 (
    echo ERROR: Output file is too small ^(!SIZE! bytes, expected ^> 10KB^).
    exit /b 1
)
echo [TEST 1] Output file size: !SIZE! bytes

REM ffprobe checks
for /f "tokens=*" %%i in ('"%FFPROBE%" -v error -select_streams v:0 -show_entries stream^=width^,height^,codec_name -of csv^=s^=p:p^=0 "%OUT_MP4%"') do set STREAM_INFO=%%i
echo [TEST 1] Stream info: !STREAM_INFO!
echo !STREAM_INFO! | findstr "h264" >nul
if !ERRORLEVEL! neq 0 (
    echo ERROR: Output is not H.264. Got: !STREAM_INFO!
    exit /b 1
)

for /f "tokens=*" %%i in ('"%FFPROBE%" -v error -show_entries format^=duration -of default^=noprint_wrappers^=1:nokey^=1 "%OUT_MP4%"') do set DURATION=%%i
echo [TEST 1] Duration: !DURATION! seconds

REM Simple duration check - must be at least 5 seconds (allowing for floating point string)
powershell -Command "if ([double]'!DURATION!' -lt 5.0) { exit 1 } else { exit 0 }"
if !ERRORLEVEL! neq 0 (
    echo ERROR: Video duration too short ^(!DURATION!s, expected ^>= 5s^).
    exit /b 1
)

REM Black frame detection
echo [TEST 1] Checking for black frames ^(luminance analysis^)...
"%FFMPEG%" -i "%OUT_MP4%" -vf "blackdetect=d=0.1:pix_th=0.00" -an -f null - 2>"%ROOT%\blackdetect.log"
findstr "black_start" "%ROOT%\blackdetect.log" >nul
if !ERRORLEVEL! equ 0 (
    REM blackdetect found black segments - let's see if it's the entire video
    for /f "tokens=*" %%i in ('findstr /C:"black_start:0" "%ROOT%\blackdetect.log"') do (
        echo WARNING: Video may start with black frames. Full line: %%i
    )
    echo NOTE: blackdetect output logged to %ROOT%\blackdetect.log
    REM We do not fail here; some intros may legitimately be dark. Instead we do a frame grab.
)

REM More robust check: extract one frame and check mean luminance
"%FFMPEG%" -y -i "%OUT_MP4%" -ss 2 -vframes 1 -f rawvideo -pix_fmt rgb24 "%ROOT%\test_frame.raw" 2>nul
if exist "%ROOT%\test_frame.raw" (
    REM Calculate average luminance using PowerShell
    powershell -Command "
        $bytes = [System.IO.File]::ReadAllBytes('%ROOT%\test_frame.raw');
        $sum = 0;
        for ($i = 0; $i -lt $bytes.Length; $i += 3) {
            $r = $bytes[$i];
            $g = $bytes[$i+1];
            $b = $bytes[$i+2];
            $lum = 0.299*$r + 0.587*$g + 0.114*$b;
            $sum += $lum;
        }
        $avg = $sum / ($bytes.Length / 3);
        if ($avg -lt 5) { Write-Host 'FAIL: Average luminance too low (' $avg ')'; exit 1 }
        else { Write-Host 'PASS: Average luminance (' $avg ')' }
    "
    if !ERRORLEVEL! neq 0 (
        echo ERROR: Extracted frame appears entirely black or near-black.
        exit /b 1
    )
    del "%ROOT%\test_frame.raw"
)

echo [TEST 1] PASSED
echo.

REM Test 2: Dry-run mode
echo [TEST 2] Dry-run mode ^(10 frames, save frame 5 as PNG^)...
if exist "%ROOT%\dry_run_frame.png" del "%ROOT%\dry_run_frame.png"
"%EXE%" --render-replay "%TEST_REPLAY%" --output "%ROOT%\dry_run_output.mp4" --resolution 1280x720 --fps 30 --workdir "%EDOPro_DIR%" --dry-run
if !ERRORLEVEL! neq 0 (
    echo ERROR: Dry-run failed.
    exit /b 1
)
if not exist "%ROOT%\dry_run_frame.png" (
    echo ERROR: Dry-run did not produce dry_run_frame.png.
    exit /b 1
)
for %%F in ("%ROOT%\dry_run_frame.png") do set PNG_SIZE=%%~zF
if !PNG_SIZE! lss 1000 (
    echo ERROR: Dry-run frame PNG is too small ^(!PNG_SIZE! bytes^), possibly blank.
    exit /b 1
)
echo [TEST 2] Dry-run frame size: !PNG_SIZE! bytes
echo [TEST 2] PASSED
echo.

echo ============================================
echo All smoke tests PASSED.
echo ============================================
exit /b 0
