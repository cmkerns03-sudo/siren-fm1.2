@echo off
title SIREN STUDIO
cd /d "%~dp0"
rem SIREN STUDIO (web\siren_studio.html) sur http://localhost:8769 : rien a compiler. Chrome ou Edge, FM-1 sous SIREN 0.10 en USB.
rem (Port 8769 : la fenetre de INSTALL-SIREN.bat, sur 8768, peut rester ouverte a cote.)
if not exist web\siren_studio.html (
  echo web\siren_studio.html introuvable : refais PREPARER-SIREN-FM1.bat ^(SLOOP\siren\fm1-0.10^).
  pause
  exit /b 1
)
where python >nul 2>nul || (
  echo Python introuvable. C'est le meme Python que INSTALL-SIREN.bat utilise : installe-le, puis recommence.
  pause
  exit /b 1
)
start "" http://localhost:8769/siren_studio.html
echo SIREN STUDIO : http://localhost:8769/siren_studio.html
echo Garde cette fenetre ouverte pendant que tu l'utilises. Ferme les autres pages qui parlent a la FM-1 (l'editeur Clipper, un logiciel de musique).
python -m http.server 8769 --bind 127.0.0.1 --directory web
pause
