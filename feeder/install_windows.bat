@echo off
set PANEL=ai-panel.local

cd /d "%~dp0"
python -m pip install --user --quiet psutil

set STARTUP=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup
(
  echo @echo off
  echo start "" pythonw "%~dp0feeder.py" --panel %PANEL%
) > "%STARTUP%\ai-panel-feeder.bat"

start "" pythonw "%~dp0feeder.py" --panel %PANEL%
echo Done. Log: %USERPROFILE%\.ai-panel\feeder.log
pause
