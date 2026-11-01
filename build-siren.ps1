# SIREN 0.3 (experimental): build the firmware, then open the browser installer.
# The same tools as Clipper and SLOOP (WSL MinUI-Build, the JieLi toolchain) and the three AC79 SDK files in
# build\deps\ac79 (copied from the SLOOP or CLIPPER folder the first time if they are not here yet).
# Port 8768 (SLOOP 8766, Clipper 8767): a window of theirs left open does not get mixed up with this one.
param([string]$Voices = "8")
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
Write-Host ""
Write-Host "  S I R E N   0.3   EXPERIMENTAL   ($Voices voices)" -ForegroundColor White
Write-Host "  ---- ---- ---- ----" -ForegroundColor DarkGray
if ($Voices -notin @("8", "12", "16")) { throw "Voices: 8, 12 or 16" }
$sdk = "build\deps\ac79"
if (-not (Test-Path "$sdk\cpu\wl82\tools\uboot.boot")) {
  $from = @($env:SIREN_SDK, "..\CLIPPER\build\deps\ac79", "..\SLOOP\build\deps\ac79", "..\sloop-fm1\build\deps\ac79", "..\clipper-pro\build\deps\ac79", "..\sloop-pro\build\deps\ac79") |
    Where-Object { $_ -and (Test-Path "$_\cpu\wl82\tools\uboot.boot") } | Select-Object -First 1
  if (-not $from) { throw "No AC79 SDK files: copy build\deps\ac79 from your SLOOP or CLIPPER folder into this folder (or set SIREN_SDK)" }
  Write-Host "== Copying the SDK files from $from" -ForegroundColor Cyan
  New-Item -ItemType Directory -Force -Path "build\deps" | Out-Null
  Copy-Item -Recurse -Force $from "build\deps\"
  $src = Split-Path (Split-Path (Split-Path $from))
  foreach ($c in @("genwav", "hiphop-src")) {
    if ((Test-Path "$src\build\$c") -and -not (Test-Path "build\$c")) { Copy-Item -Recurse "$src\build\$c" "build\" }
  }
  if ((Test-Path "$src\build\gen_samples.cache") -and -not (Test-Path "build\gen_samples.cache")) { Copy-Item "$src\build\gen_samples.cache" "build\" }
}
Write-Host "== Building the firmware (WSL MinUI-Build)" -ForegroundColor Cyan
Remove-Item Env:FELUCCA_SLICE -ErrorAction SilentlyContinue
$env:SIREN = "1"
python tools/build_windows.py --distro MinUI-Build --toolchain /root/fm1-codex-deps/jieli/toolchain --sdk $sdk --slice 0 --nvoice $Voices --siren 1
if ($LASTEXITCODE -ne 0) { throw "The build failed: send the messages above to Claude" }
Write-Host "== Making the installer site" -ForegroundColor Cyan
python web/make_site.py build/felucca.fwsc siren-0.3 build/siren-site
if ($LASTEXITCODE -ne 0) { throw "make_site failed" }
Write-Host ""
Write-Host "Installer: http://localhost:8768/webapp/installer/  (Chrome or Edge, FM-1 on USB)" -ForegroundColor Green
Write-Host "Keep this window open during the install. Ctrl+C stops the server."
Start-Process "http://localhost:8768/webapp/installer/"
python -m http.server 8768 --bind 127.0.0.1 --directory build/siren-site
