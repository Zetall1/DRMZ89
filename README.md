# KNULLI Drumkit

Een drumcomputer voor Anbernic-handhelds die [KNULLI](https://knulli.org) draaien:

- **TR-808 bank** – kick, snare, closed/open hat, clap, cowbell, tom hi/lo (volledig gesynthetiseerd, geen copyright-samples)
- **TR-909 bank** – kick (met punch-click), snare, closed/open hat, clap, rimshot, tom hi/lo (gesynthetiseerd)
- **SAMPLER bank** – 8 pads; laadt je eigen 16-bit WAV-bestanden uit de map `samples/`
- **Sequencer** – 16 stappen × 8 sporen per bank, 30–300 BPM, patroon wordt opgeslagen als tekstbestand
- **Besturing** – werkt direct met de KNULLI-gamepad (SDL GameController) én joystick-fallback

De 808/909-stemmen worden bij opstart zelf berekend (sine-sweeps, gefilterde
ruis, exponentiële envelopes) — daarom is de binary klein en vrij van
auteursrechtelijke samples.

## Besturing

| Knop | Functie |
|---|---|
| D-pad | cursor (spoor/stap) |
| **A** | zet stap aan/uit + preview |
| **B** | preview geluid |
| **Y** | vorige bank (808 → 909 → sampler) |
| **X** | volgende bank |
| **L1** / **R1** | BPM −2 / +2 |
| **Select** | play / stop |
| **Start** | patroon opslaan |
| **Select + Start** | opslaan + stoppen |

## Installeren op KNULLI

1. Kopieer de complete map `drumkit/` (met de binary `drumkit`, `drumkit.sh` en
   de map `samples/`) naar je SD-kaart in `/roms/ports/drumkit/`.
2. Zorg dat `drumkit.sh` uitvoerbaar is: `chmod +x drumkit.sh`.
3. Herstart KNULLI of ververs de gamelist; de app staat onder **Ports**.

Je patroon wordt opgeslagen in `drumkit.pattern` (zelfde map) en automatisch
weer geladen bij de volgende start.

## Bouwen

### Op de handheld zelf (SSH)

Vereist een rootfs met dev-tools; praktischer is cross-compileren op je PC:

```sh
make
```

### Cross-compileren vanaf je PC (aanbevolen)

```sh
sudo apt install gcc-aarch64-linux-gnu pkg-config libsdl2-dev
make knulli CROSS=aarch64-linux-gnu- SDL_CFLAGS="-I/usr/include/SDL2"
```

De linker gebruikt `pkg-config --libs sdl2`; op de handheld levert KNULLI de
runtime-SDL2 zelf. Zonder pkg-config vallen beide terug op
`-I/usr/include/SDL2` / `-lSDL2`.

## Eigen samples gebruiken (sampler-bank)

Leg 16-bit PCM-WAV-bestanden in `drumkit/samples/` als `01.wav` t/m `08.wav`.
Mono of stereo; elke samplefrequentie is toegestaan (wordt automatisch naar
44,1 kHz geresampled). Ontbrekende bestanden blijven lege pads.

## Zelftest

```sh
make selftest
```

Test de WAV-parser (mono + stereo + resampling), het patroon opslaan/laden en
alle gesynthetiseerde stemmen. Vereist een werkende SDL2-toolchain; de tests
draaien headless.

## Projectstructuur

```
drumkit/
├── Makefile
├── drumkit.sh        # KNULLI Ports-launcher
├── src/main.c        # alles: DSP, sequencer, UI, invoer
└── samples/          # hier je eigen WAVs (01.wav..08.wav)
```

## Licentie

Vrij te gebruiken en aan te passen (MIT). De 808/909-klanken zijn hier zelf
gereconstrueerd, dus geen sample-rechtenproblemen.
