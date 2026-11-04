@echo off
title ABYSS installer
rem Compile ABYSS (12 voix) et ouvre l installateur local (http://localhost:8769). Rien n est publie.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-abyss.ps1" %1
pause
