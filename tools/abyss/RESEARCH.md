# ABYSS: the research behind the brain

ABYSS writes ambient music in three worlds. This file explains where its rules come from. Each rule is marked
**[src]** when a source states it, or **[set]** when it is a starting value tuned by ear. In
`firmware/src/abyss_brain.c`, each one is a table or a constant.

## Chills (frisson): the moment a piece is built toward
- [src] **Sloboda 1991** (83 listeners): tears came with melodic appoggiaturas and sequences. Shivers came with new or
  unprepared harmony, sudden dynamic or textural change, and events that arrive earlier than expected.
- [src] **Grewe et al. 2007**: chills began at loudness peaks, modulations, a new voice or instrument entering, the start
  of a section, and melodies in the vocal register. The 920–4400 Hz band got louder.
- [src] **Guhn et al. 2007**: chills came with a wider frequency range and with a solo against an accompaniment.
  **Panksepp 1995**: sudden soft-to-loud leaps, and sad music more than happy.
- [src] **Bannister & Eerola 2018**: when the features above were removed, chills fell from 30 to 21. The same features
  without a build-up gave no chills. Chills correlate with loudness (r 0.47–0.78), roughness, brightness and spectral
  flux.
- [src] **Goodchild**: adding instruments raises the rated intensity, and it stays high after the texture thins.
  **Salimpoor 2011**: the anticipation response comes before the peak.

What ABYSS does with this:
- [set] The climax combines at least three of these: a crescendo of about 9 dB, then a bar of near-silence (a
  breath), then everything back at once (subito); a new voice entering (the SWELL layer: choir, brass or organ); one
  unexpected chord (CINEMATIC ♭VI or IV major, COSMIC ♭VI maj7, DARK ♭II cluster); an appoggiatura on the melody's
  peak; the register opening at both ends (the sub, and the bells an octave up); the filters opening; the theme
  coming back augmented or an octave up; a rising sequence into it.
- The climax comes after minutes of build, at 65–80 % of the piece.

## CINEMATIC (Max Richter, Hans Zimmer's calm work, Hammock, Stars of the Lid, Jóhann Jóhannsson, Ólafur Arnalds)
- [src] Hooktheory tempos: *On the Nature of Daylight* 61 BPM, *No Time for Caution* 63, the *Interstellar* theme 96
  in 6/8. Melodies hold each note 3.6–4 beats or more.
- [src] Progressions:
  - *Daylight*: i–v6–III6–VI–VII, a descending ground bass looped for the whole piece.
  - *Interstellar*: VI7–v6–i–v6/5 over a dominant pedal.
  - *No Time for Caution*: IV–V–vi(add9)–Vsus4.
  - *Dona Nobis Pacem*: i–v(add11)–VI(add9)–III(add11)–iv(add9).
  - *One Day*: i–VI–III–VIIsus4.

  `AB_CINEMA` holds these, plus a lament, a plagal loop, a dorian lift and a "hope" loop.
- [src] Melodies are stepwise and 100 % diatonic, 59–94 % on chord tones, over a range of 7–11 semitones in the vocal
  register. *Daylight* builds from the low end up and is palindromic. Arnalds' *Stratus* plays a held note in a set
  rhythm with random octave jumps; ABYSS's felt piano (the BELLS layer) does the same.
- [set]
  - Chords: one every 1–2 bars, sometimes 4.
  - Theme: cells of 2–4 notes, with one leap answered by a step back.
  - Melody: absent in the first half of the THEME section; afterwards each phrase plays with a chance of 45–95 %
    (the DENSITY knob).
  - Climax at 65–80 %; tape hiss throughout.
  - Sometimes a clock's tick (*Interstellar*'s "Mountains", where each tick is a day on Earth [src]).

## COSMIC (Steve Roach, Carbon Based Lifeforms, Michael Stearns, Eno's *Apollo*, Vangelis, Tangerine Dream)
- [src] Carbon Based Lifeforms tempos (AcousticBrainz) are 78–107 BPM, mostly 90–100, and their drums come in only
  "if the energy feels right". Roach slowed his sequences a few points "to make them sweeter". In *Structures from
  Silence*, a second synth with the same patch enters about 15 minutes in. *Phaedra* is half sequencer, half cosmic
  texture, and its Moog drifted out of tune. Eno: "simple, major chords".
- [set]
  - Piece kinds: beatless half the time, a Berlin sequencer 30 %, a slow beat 20 % (86–100 BPM, entering late and
    leaving before the end).
  - Harmony: lydian, ionian, mixolydian, dorian or aeolian; sus and add9 chords, one every 20–60 s.
  - Drones on root and fifth; the sub at 40–65 Hz.
  - The *lift*: the key moves a chromatic mediant and a new voice comes in.
  - Textures: wind (band-passed noise, its colour moving over two minutes).
  - The whole set's pitch drifts by up to 5 cents.

## DARK (Lustmord, Tim Hecker, William Basinski, Thomas Köner)
- [src]
  - Lustmord: "really low-end-heavy", with bass most systems won't register; long tracks.
  - Sunn O))) tune down to A (55 Hz); Köner's *Nunatak* is "almost subsonic rumbles".
  - Rough sounds (amplitude modulation at 30–150 Hz) are the scream and alarm range and activate the amygdala
    (Arnal 2015). A minor second in the 440–1800 Hz register beats in that range.
  - Basinski's loops decay non-linearly: the strongest notes hold on longest, the sustains fall away first, and the
    decay speeds up.
  - Hecker's arc moves from tension toward a near-romantic finish.
  - The *Soundless Music* infrasound concert (2003): 22 % of listeners felt unease or chills, a result its organisers
    call "tentative and inconclusive". ABYSS does not use infrasound; its sub stays above 27 Hz.
- [set]
  - No pulse.
  - Drones on root and octave, changing by a semitone or a tritone every 1–2 minutes.
  - The sub at 27–45 Hz, swelling every 5 s; a rumble (brown noise) breathing every 20 s.
  - Rough clusters only at the peak, a semitone apart between A4 and G#5.
  - One looming crescendo of minutes, then the VOID (silence and the reverb's tail), then the decay.
  - A quarter of the pieces are a Basinski loop instead: a consonant phrase played again and again. Its notes' ends
    fall away first, then the notes themselves, faster and faster (quadratic), while its filter closes.
  - Reverb about 25 s.

## Sources
- Hooktheory (Max Richter: *On the Nature of Daylight*, *November*, *Dona Nobis Pacem*; Hans Zimmer: *Interstellar*
  theme, *No Time for Caution*, *One Day*)
- https://en.wikipedia.org/wiki/On_the_Nature_of_Daylight
- https://en.wikipedia.org/wiki/Sleep_(album)
- https://www.guitarworld.com/features/hammock-marc-byrd-andrew-thompson
- https://www.spitfireaudio.com/cdn/shop/files/Stratus_Manual_V1.1.pdf
- https://www.frontiersin.org/article/10.3389/fpsyg.2018.02046/full (Bannister & Eerola 2018)
- https://www.frontiersin.org/articles/10.3389/fpsyg.2014.00790 (frisson review)
- https://sonicfield.org/library/psychoacoustical-correlates-of-musically-induced-chills (Grewe, Nagel)
- https://sonicfield.org/library/exploring-emotional-responses-to-orchestral-gestures (Goodchild)
- https://reverb.com/uk/news/steve-roach-interview
- https://www.releasemagazine.net/carbon-based-lifeforms-the-ambient-electronic-duo-interviewed/
- https://acousticbrainz.org (Carbon Based Lifeforms tempos)
- https://www.udiscovermusic.com/stories/tangerine-dream-phaedra-making-of-feature/
- https://www.rollingstone.com/music/music-features/brian-eno-apollo-interview-858237/
- https://daily.redbullmusicacademy.com/2015/10/lustmord-feature
- https://www.soundonsound.com/techniques/classic-tracks-william-basinski-disintegration-loops
- https://en.wikipedia.org/wiki/Ravedeath,_1972
- https://neurosciencenews.com/acoustic-properties-fear-scream-2257/ (Arnal 2015)
- https://www.sarahangliss.com/?p=27293 (*Soundless Music*)
- https://sonicfield.org/library/a-cortical-network-underpinning-the-perceptual-priority-for-rising-intensity-and (looming sounds)
