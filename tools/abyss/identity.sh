#!/bin/sh
# ABYSS: the site's brain (tools/abyss/brain_wasm.c compiled to WebAssembly) plays the FM-1's notes, event for event:
# the host test's dump (tests/abyss/brain_test.c, gcc) against the WebAssembly's (clang, run by node), every world, a few
# seeds, two pieces each. Needs clang (wasm32) and node; without them it says so and passes.
#   tools/abyss/identity.sh HOST_BRAIN_TEST OUT_DIR
set -e
T="$1"; O="${2:-build/host}"
command -v clang >/dev/null && command -v node >/dev/null || { echo "identity: no clang or node here, skipped"; exit 0; }
sh "$(dirname "$0")/build_brain_wasm.sh" "$(cd "$O" && pwd)/abyss-brain.wasm" >/dev/null 2>&1 || { echo "identity: no wasm32 target, skipped"; exit 0; }
for w in 0 1 2; do for s in S1R-3N0 AB0-0X5 7KQ-ZZ1 000-0001; do
  "$T" $w $s 2 dump | grep -E '^[0-9]+\.[0-9]+ ' > "$O/id_c.txt" || true
  node "$(dirname "$0")/brain_identity.mjs" "$O/abyss-brain.wasm" $w $s 2 > "$O/id_w.txt"
  [ -s "$O/id_c.txt" ] || { echo "identity: no dump from $T"; exit 1; }
  cmp -s "$O/id_c.txt" "$O/id_w.txt" || { echo "identity: world $w seed $s differs"; diff "$O/id_c.txt" "$O/id_w.txt" | head -5; exit 1; }
done; done
echo "identity: 3 worlds x 4 seeds, the same notes ($(wc -l < "$O/id_c.txt") in the last)"
