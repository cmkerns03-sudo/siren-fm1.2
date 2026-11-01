# the palette the brain is told of (FM-1: SIREN's and Clipper's sounds only), for the identity test
import json, sys
P = '/home/claude/siren/proto/data/'
cur = json.load(open(P + 'curation.json')); kits = json.load(open(P + 'kits.json'))
STY = ['TECHNO', 'TRANCE', 'PSY', 'ACID', 'RAVE', 'HARD', 'DNB', 'DUBSTEP']; LAY = ['DRUMS', 'BASS', 'ARP', 'HOOK', 'PAD', 'STAB', 'FX', 'SUB']
fm1 = lambda k: '/' not in k or k.startswith('siren/')
n = {s: {L: len([k for k in cur[s].get(L, []) if fm1(k)]) for L in LAY} for s in STY}
o = ['static const uint16_t KN16[][2] = {']; first = []; i = 0
for s in STY:
    first.append(i)
    for k in kits[s]: o.append(f'{{{k["kickN16"]}, {k["kick2N16"]}}},'); i += 1
o.append('};')
o.append('static const sb_palette_t PAL = { {' + ', '.join('{' + ', '.join(str(n[s][L]) for L in LAY) + '}' for s in STY) + '}, {' + ', '.join(str(len(kits[s])) for s in STY) + '}, KN16, {' + ', '.join(map(str, first)) + '} };')
open('pal.h', 'w').write('\n'.join(o) + '\n')
json.dump({'n': n}, open('pal.json', 'w'))
