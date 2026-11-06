@echo off
title Clipper editor
cd /d "%~dp0"
rem Clipper's editor. Under SIREN, the FM-1's editor is SIREN STUDIO (OPEN-SIREN-STUDIO.bat): opened here when Clipper is not built.
if not exist build\clipper-site\webapp\editor\index.html (
  echo SIREN's editor is SIREN STUDIO: opening it ^(OPEN-SIREN-STUDIO.bat^).
  echo ^(Clipper's own editor: run INSTALL-CLIPPER.bat first.^)
  call "%~dp0OPEN-SIREN-STUDIO.bat"
  exit /b
)
start "" http://localhost:8767/webapp/editor/
echo Clipper editor: http://localhost:8767/webapp/editor/  (keep this window open while you use it)
python -m http.server 8767 --bind 127.0.0.1 --directory build/clipper-site
pause
