#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SIREN 0.9.1: what each sound of the selection costs, in exact instructions (valgrind's callgrind over
tests/siren/sound_ir.c), against one HYBRID voice: one line "LAYER|ENGINE:PRESET x" a (sound, layer) of
build/gen/siren_data.h.   tools/siren/cost_ir.py BUILD/sound_ir OUT.json [JOBS]"""
import json, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
exe, out = sys.argv[1], sys.argv[2]; jobs = int(sys.argv[3]) if len(sys.argv) > 3 else 2
h = Path('build/gen/siren_data.h').read_text()
snd = re.findall(r'\{"([^"]+)", "([^"]+)"\}', re.search(r'SD_SND\[SD_NSND\] = \{(.*?)\};', h, re.S).group(1))
lst = [int(x) for x in re.search(r'SD_LIST\[SD_NLIST\] = \{(.*?)\};', h, re.S).group(1).split(',')]
cur = re.search(r'SD_CUR\[\d+\]\[\d+\] = \{(.*?)\};\n', h, re.S).group(1)
spans = [tuple(map(int, m)) for m in re.findall(r'\{(\d+), (\d+)\}', cur)]
LN = ['DRUMS', 'BASS', 'ARP', 'HOOK', 'PAD', 'STAB', 'FX']
pairs = set()
for i, (first, n) in enumerate(spans):
    L = i % 7
    if 1 <= L <= 5:
        for k in range(first, first + n): pairs.add((lst[k], L))
def ir(a):
    r = subprocess.run(['valgrind', '--tool=callgrind', '--callgrind-out-file=/dev/null', exe] + [str(x) for x in a],
                       capture_output=True, text=True)
    return int(re.search(r'Collected : (\d+)', r.stderr).group(1))
with ThreadPoolExecutor(jobs) as ex:
    base, hy = ex.submit(ir, (-1, 0)).result(), ex.submit(ir, (-2, 0)).result()
    keys = sorted(pairs); res = list(ex.map(lambda p: ir(p), keys))
hy -= base
d = {f'{LN[L]}|{snd[s][0]}:{snd[s][1]}': round((r - base) / hy, 3) for (s, L), r in zip(keys, res)}
Path(out).write_text(json.dumps(d, indent=0, sort_keys=True))
print(f'cost: {len(d)} sounds, one HYBRID voice {hy} instructions')
