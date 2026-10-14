# ROMs and sound samples

CYD Classic Arcade ships **no game ROMs**. You copy in the ROM files of
games you own. It reads standard **MAME ROM sets** (the `.zip` files, as
they are - no unzipping or renaming).

## How files are found

Every ROM chip is recognised by its **CRC-32 checksum**, not its file name.
File names change between MAME versions (Space Invaders' `invaders.h` is
`9316b-0869_m739h.h1` in newer sets); the contents don't. So:

- Any MAME version's zip of the right set works.
- Split, merged and non-merged sets all work. A clone set (e.g. `pacman`)
  in a **split** collection holds only the files that differ from its
  parent - give the parent zip too (`puckman.zip` for `pacman`). Non-merged
  sets have everything in one zip.
- Extra files in a zip are ignored. A missing or bad chip is named in the
  error, with its checksum.

## The sets

| Game | MAME set | MAME title | Also give (split sets) | Optional sound samples |
|---|---|---|---|---|
| Pac-Man | `pacman` | Pac-Man (Midway) | `puckman` | - |
| Space Invaders | `invaders` | Space Invaders / Space Invaders M | - | `invaders` samples |
| Galaga | `galaga` | Galaga (Namco rev. B) | - | - |
| Dig Dug | `digdug` | Dig Dug (rev 2) | - | - |
| Donkey Kong | `dkong` (or `dkongo`, US set 2) | Donkey Kong (US set 1) | `dkong` for `dkongo` | `dkong` samples |
| Q\*bert | `qbert` | Q\*bert (US set 1) | - | `qbert` samples (the knocker) |
| Asteroids | `asteroid` (or `asteroid2`, rev 2) | Asteroids (rev 4) | - | `asteroid` samples |
| Rally-X | `rallyx` | Rally-X (32k Ver.?) | - | - |
| Qix | `qix` | Qix (Rev 2) | - | - |
| Defender (stretch) | `defender` (or `defenderg`, Green label) | Defender (Red label) | `defender` for `defenderg` | - |
| Stargate (stretch) | `stargate` | Stargate | - | - |
| Star Wars | `starwars` | Star Wars (Atari, set 1) | - | - |
| Spy Hunter (stretch) | `spyhunt` | Spy Hunter | - | - |
| Rolling Thunder (stretch) | `rthunder` | Rolling Thunder (rev 3) | - | - |

Every set above is the parent ("main") set in MAME except `pacman`, which is
the Midway version and a clone of the Japanese `puckman` (Puck Man).

Older MAME versions named the sets differently: their `asteroid` is today's
`asteroid2` (rev 2), and they leave out the vector generator's PROM
(`034602-01.c8`), which our emulator doesn't need. Both work.

Older dumps also work where the data is the same: an old `spyhunt` has
`cpu_pg5.11d` with its two halves swapped (the loader checks both orders),
and an old `starwars` has the math box PROMs in another layout, and no
AVG PROM - our emulator needs neither.

Notes:
- **Galaga, Dig Dug:** non-merged sets also hold the Namco custom chips'
  own programs (`51xx.bin`, `53xx.bin`, `54xx.bin`). They are optional:
  without them the chips are modelled from their known behaviour (Galaga's
  explosions then come from our own noise model).
- **Q\*bert:** the speech chip (Votrax SC-01) has its own internal ROM,
  `sc01a.bin`; non-merged sets include it. Keep it in the zip.
- **Sample zips** are MAME's separate sound-sample sets (WAV files in a zip
  named after the game). They are optional: without them those sounds are
  made by the firmware's own simple sound models (see SPEC, Sound). Files
  are found by name; any missing one is modelled instead. WAVs: PCM, 8 or
  16 bit, mono or stereo, any rate.
- **Space Invaders samples:** `0.wav` UFO (older sets only - newer ones
  leave it to the model), `1.wav` shot, `2.wav` player hit, `3.wav` invader
  hit, `4.wav`-`7.wav` the four fleet notes, `8.wav` UFO hit, `9.wav`
  extra ship.

## Getting them onto the board

(Planned - see SPEC.) Boards with a working SD slot (the ESP32-32E boards:
3.2", 3.5", 4.0"): put the zips in a `roms` folder on the card; the
firmware copies what it recognises into its own flash once. The 2.8" boards
(no usable SD slot) get them over USB from the web page.
