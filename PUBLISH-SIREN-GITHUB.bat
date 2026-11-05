@echo off
title SIREN - publish on GitHub
rem Publishes SIREN 0.9.1 on your GitHub: the source (the repository YOUR-NAME/siren-fm1), the release (a pre-release:
rem SIREN is experimental) with the firmware and the launch video, the installer (GitHub Pages).
rem Run INSTALL-SIREN.bat first (it builds the firmware the release and the installer publish).
rem Sign-in: your own, in the browser (GitHub CLI). Nothing is forced on an existing repository.
rem Usage: PUBLISH-SIREN-GITHUB.bat                 (the repository YOUR-NAME/siren-fm1, a pre-release)
rem        PUBLISH-SIREN-GITHUB.bat -Repo another-name
rem        PUBLISH-SIREN-GITHUB.bat -Stable          (a normal release, the latest)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\publish_siren_github.ps1" %*
pause
