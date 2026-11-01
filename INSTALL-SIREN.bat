@echo off
title SIREN installer
rem Compile SIREN (8 voices) et ouvre l installateur local (http://localhost:8768). Rien n est publie.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-siren.ps1" %1
pause
