@echo off

echo ============================================
echo  replay2video - Build GUI Launcher
echo ============================================
echo.

set GUI_DIR=%~dp0
if "%GUI_DIR:~-1%"=="\" set GUI_DIR=%GUI_DIR:~0,-1%

set ROOT=%GUI_DIR%\..
set OUT=%GUI_DIR%\dist
set DIST=%ROOT%\dist

REM --- Check Python 3.11 ---
py -3.11 --version >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo ERROR: Python 3.11 n'est pas trouve.
    echo Installez Python 3.11 depuis https://python.org
    exit /b 1
)

REM --- Install dependencies ---
echo [1/3] Installation des dependances...
py -3.11 -m pip install --quiet --prefer-binary pywebview pyinstaller
if %ERRORLEVEL% neq 0 (
    echo ERROR: pip install a echoue.
    exit /b 1
)

REM --- Build with PyInstaller ---
echo [2/3] Compilation avec PyInstaller...
cd /D "%GUI_DIR%"
py -3.11 -m PyInstaller ^
    --onefile ^
    --noconsole ^
    --name replay2videoGUI ^
    --add-data "ui.html;." ^
    --hidden-import webview ^
    --hidden-import webview.platforms.winforms ^
    launcher.py
if %ERRORLEVEL% neq 0 (
    echo ERROR: PyInstaller a echoue.
    exit /b 1
)

REM --- Copy to dist/ ---
echo [3/3] Copie vers dist\...
if not exist "%DIST%" mkdir "%DIST%"
if exist "%OUT%\replay2videoGUI.exe" (
    copy /Y "%OUT%\replay2videoGUI.exe" "%DIST%\replay2videoGUI.exe" >nul
    echo.
    echo OK: %DIST%\replay2videoGUI.exe
) else (
    echo ERROR: replay2videoGUI.exe introuvable dans %OUT%
    exit /b 1
)

echo.
echo ============================================
echo  Launcher construit avec succes !
echo  Lancez deploy_package.bat pour assembler
echo  le package complet de distribution.
echo ============================================
exit /b 0
