#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""SIREN 0.10: SIREN STUDIO (web/siren_studio.html) in a real browser against the firmware on the host: the page's
WebMIDI goes through a shim to tests/siren/studio_test.c's bridge (the firmware's own siren.c, siren_ui.c,
editor_siren.c and audio path). Connect, LIVE (PLAY, a style, an effect held, a fader), SEEDS (codes imported, kept
in the FM-1), SOUNDS (a bank narrowed and sent). Screens in OUT_DIR.
  tests/siren/studio_e2e.py BUILD/siren_studio_o2 OUT_DIR"""
import subprocess, sys, os, re, json
from pathlib import Path
from playwright.sync_api import sync_playwright

exe, out = sys.argv[1], Path(sys.argv[2]); out.mkdir(parents=True, exist_ok=True)
br = subprocess.Popen([exe, 'bridge'], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
def wire(hexline):
    br.stdin.write(hexline + '\n'); br.stdin.flush()
    return br.stdout.readline().strip()
SHIM = """
(() => {
  const input = {name: 'Clipper', id: 'in', onmidimessage: null};
  const output = {name: 'Clipper', id: 'out', send(d) {
    const hex = Array.from(d).map(b => b.toString(16).padStart(2, '0').toUpperCase()).join(' ');
    window.__wire(hex).then(r => { if (r && input.onmidimessage) input.onmidimessage({data: Uint8Array.from(r.split(' ').map(x => parseInt(x, 16)))}); });
  }};
  navigator.requestMIDIAccess = async () => ({inputs: new Map([['in', input]]), outputs: new Map([['out', output]])});
})();
"""
fails = []
def check(c, m):
    print(('ok    ' if c else 'FAIL  ') + m); (None if c else fails.append(m))
with sync_playwright() as p:
    b = p.chromium.launch()
    pg = b.new_page(viewport={'width': 1280, 'height': 900}, device_scale_factor=1)
    errs = []
    pg.on('pageerror', lambda e: errs.append(str(e)))
    pg.on('console', lambda m: errs.append(m.text) if m.type == 'error' else None)
    pg.expose_function('__wire', wire)
    pg.add_init_script(SHIM)
    pg.add_init_script("try { localStorage.setItem('siren-studio-lang', 'en'); } catch (e) {}")
    pg.goto('file://' + os.path.abspath('web/siren_studio.html'))
    pg.click('#conn')
    pg.wait_for_function("document.getElementById('status').textContent.startsWith('Connected')", timeout=30000)
    check(True, 'connect: ' + pg.text_content('#status'))
    pg.wait_for_function("document.getElementById('snd1').textContent.length > 2", timeout=10000)
    check('TECHNO' in pg.text_content('#dStyle'), 'LIVE: the style shown (%s)' % pg.text_content('#dStyle'))
    pg.click('#play'); pg.wait_for_function("document.getElementById('play').textContent === 'STOP'", timeout=10000)
    check(True, 'LIVE: PLAY -> STOP')
    pg.click('#styles button:nth-child(8)')
    pg.wait_for_function("document.querySelector('#styles button:nth-child(8)').classList.contains('wait') || document.querySelector('#styles button:nth-child(8)').classList.contains('on')", timeout=10000)
    check(True, 'LIVE: HOUSE queued')
    pad = pg.locator('#fx button').nth(4); bx = pad.bounding_box()
    pg.mouse.move(bx['x'] + 5, bx['y'] + 5); pg.mouse.down()
    pg.wait_for_timeout(600)
    check('LOW PASS' in pg.text_content('#fx button:nth-child(5)'), 'LIVE: the effects named (%s)' % pg.text_content('#fx button:nth-child(5)'))
    pg.screenshot(path=str(out / 'studio_live.png'), full_page=True)
    pg.mouse.up()
    s = pg.locator('#s4_0'); s.fill('-10'); s.dispatch_event('input')
    pg.wait_for_timeout(1500)
    v = pg.evaluate("state.L[4].vol")
    check(v <= -6, 'LIVE: the pad volume fader reached the FM-1 (%s)' % v)
    # SEEDS
    pg.click('nav button[data-tab=seeds]')
    pg.fill('#codes', 'PSY 11R-3N07\nTECHNO 2FQ-9KD1 ALL\nnonsense')
    pg.click('#addCodes'); pg.wait_for_timeout(800)
    t0 = pg.text_content('#slot0 .code'); t1 = pg.text_content('#slot1 .code')
    check(t0.strip() == 'PSY 11R-3N07' and t1.strip() == 'TECHNO 2FQ-9KD1 ALL', 'SEEDS: imported (%s | %s)' % (t0, t1))
    check('1 not understood' in pg.text_content('#impMsg'), 'SEEDS: the bad line counted (%s)' % pg.text_content('#impMsg'))
    pg.click('#slot0 .btns button:first-child'); pg.wait_for_timeout(800)
    check(pg.evaluate("state.style") == 2 and pg.evaluate("seedCode(state.seed)") == '11R-3N07', 'SEEDS: slot 1 plays PSY 11R-3N07')
    pg.screenshot(path=str(out / 'studio_seeds.png'), full_page=True)
    # SOUNDS
    pg.click('nav button[data-tab=sounds]'); pg.wait_for_timeout(500)
    pg.click('#bStyles button:nth-child(1)'); pg.wait_for_timeout(300); pg.click('#bLayers button:nth-child(2)'); pg.wait_for_timeout(500)
    n = pg.locator('#bList .item').count()
    check(n >= 50, 'SOUNDS: the bass universe listed (%d)' % n)
    pg.click('#bNone'); pg.locator('#bList .item input').nth(n - 1).check()
    pg.click('#bSave'); pg.wait_for_timeout(600)
    check('Sent' in pg.text_content('#bMsg'), 'SOUNDS: sent (%s)' % pg.text_content('#bMsg'))
    pg.screenshot(path=str(out / 'studio_sounds.png'), full_page=True)
    pg.click('#bResetAll'); pg.wait_for_timeout(500)
    check(not errs, 'no error in the page: %s' % errs[:3])
    b.close()
br.stdin.close(); br.wait(timeout=10)
print('studio e2e: %s' % ('ok' if not fails else '%d FAILED' % len(fails)))
sys.exit(1 if fails else 0)
