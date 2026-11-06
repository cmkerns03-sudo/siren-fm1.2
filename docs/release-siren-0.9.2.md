# SIREN 0.9.2 (experimental)

**A rave that writes itself: a free, open-source firmware for the M-VAVE FM-1.** Press PLAY and SIREN writes the
whole track, then the next one; you play it like a DJ. Built on [Clipper](https://github.com/isod89/clipper-fm1) and
[SLOOP](https://github.com/isod89/sloop-fm1), based on [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita /
Hügelton Instruments.

**[Install it from the browser](https://isod89.github.io/siren-fm1/webapp/installer/)** (Chrome or Edge, the FM-1 on
USB). The installer page also takes you back to the official firmware (V16 or V15).

> **Experimental.** SIREN passes its whole test suite on a computer and is being tested on the FM-1. If something
> crackles or sounds wrong, open an issue with the style and what the screen shows (the CPU % at the bottom, ECO at the
> top).

## What it is

- **8 styles**: TECHNO, TRANCE, PSY, ACID, RAVE, HARD, DRUM & BASS, HOUSE.
- **Seven layers** written live (drums, bass, arp, hook, pad, stabs, FX) and the track's form: intro, builds, breaks,
  drops, a new track when one is done.
- **You play it**: the black keys shape the track (BREAK, BUILD, DROP, FILL, NEW MELODY, NEXT TRACK...), the white
  keys take any layer, 16 DJ effects, a mixer, four DJ knobs, 16 seeds to keep the tracks you love.

## New in 0.9.2: your seeds stay, the battery in %

- **The 16 seeds of the SAVE page are kept over a restart.** Writing the FM-1's memory stops the sound for a moment,
  so SIREN writes them when the music is stopped: a seed kept while it plays shows KEPT: SAVED AT STOP, and SEEDS
  SAVED once it is written. Turn the FM-1 off only after a stop if you kept seeds while playing.
- **The battery in %** at the bottom of the deck, in the middle (USB when a computer powers the FM-1; orange under
  15 %). It is worked out from the battery's voltage, so it reads a few % lower while the music plays.
- SIREN no longer loads a tuning saved by Clipper (it has no tuning page: equal temperament). Its seeds sit where
  Clipper keeps its tuning: after SIREN saves its seeds, Clipper starts in equal temperament until you set the tuning
  again.

## New in 0.9.1: more sounds

- **201 sounds instead of 107**: 74 more of the FM-1's own (basses, mallets and bells for the arps, SUPERSAW, brass,
  pads, organs and stabs), each in the styles it suits, each level-matched in its layer.
- **The community's OberVave voice**: [OberVave](https://github.com/zvenson/obervave) by zvenson, the 8-voice
  analog-style synth for the FM-1, ported as SIREN's OBERVAVE engine, with 27 of its programs (basses, arps, leads,
  stabs).
- **Still light**: no new sound costs more than the dearest sound already in its layer.

## New in 0.9

- **Lighter**: the two cores share the work better, the room reverb and the pad run at half the rate: the longest
  block 16 to 35 % shorter, depending on the style.
- **Every sound in every style**: PRESET goes through every bank; **ALL SOUNDS** (SEL page, key 9) lets the next
  tracks pick from every bank.
- **Pro risers**: five kinds (AIR RISE, UPLIFTER, SHIMMER UP, SAW SWEEP, WASH RISE) that climb to the drop and fall
  away after it.
- **A readable screen**: the layers, the seed, the knobs and the messages in the big font.
- **Its own installer**: three steps, in English and French.

M-VAVE, FM-1 and the other product names here are trademarks of their respective owners. SIREN is an independent
firmware, not affiliated with them.
