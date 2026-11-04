#!/bin/sh
# ABYSS's brain for the web (tools/abyss/brain_wasm.c -> the site's engines/abyss-brain.wasm): the FM-1's abyss_brain.c,
# compiled to WebAssembly with clang (no library)
set -e
cd "$(dirname "$0")"
clang --target=wasm32 -O2 -nostdlib -ffreestanding -fvisibility=hidden -Wall -Wno-unused-function \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,--strip-all -o "${1:-abyss-brain.wasm}" brain_wasm.c
echo "abyss brain: ${1:-abyss-brain.wasm} $(wc -c < "${1:-abyss-brain.wasm}") B"
