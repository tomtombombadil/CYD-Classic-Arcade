# ROMs

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

| Game | MAME set | MAME title | Also give (split sets) |
|---|---|---|---|
| Pac-Man | `pacman` | Pac-Man (Midway) | `puckman` |
| Space Invaders | `invaders` | Space Invaders / Space Invaders M | - |
| Galaga | `galaga` | Galaga (Namco rev. B) | - |
| Dig Dug | `digdug` | Dig Dug (rev 2) | - |
| Donkey Kong | `dkong` (or `dkongo`, US set 2) | Donkey Kong (US set 1) | `dkong` for `dkongo` |
| Q\*bert | `qbert` | Q\*bert (US set 1) | - |
| Asteroids | `asteroid` (or `asteroid2`, rev 2) | Asteroids (rev 4) | - |
| Rally-X | `rallyx` | Rally-X (32k Ver.?) | - |
| Qix | `qix` | Qix (Rev 2) | - |
| Defender (stretch) | `defender` (or `defenderg`, Green label) | Defender (Red label) | `defender` for `defenderg` |
| Stargate (stretch) | `stargate` | Stargate | - |
| Star Wars | `starwars` | Star Wars (Atari, set 1) | - |
| Spy Hunter (stretch) | `spyhunt` | Spy Hunter | - |
| Rolling Thunder (stretch) | `rthunder` | Rolling Thunder (rev 3) | - |

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
- **Only the ROM zips** are needed. MAME also has separate "sample" sets
  (recordings of analog sounds); this emulator doesn't use them - those
  sounds are modelled in the firmware.
- **Q\*bert:** the speech chip (Votrax SC-01) has its own internal ROM,
  `sc01a.bin`; non-merged sets include it. Keep it in the zip.

## Getting them onto the board

(Planned - see SPEC.) Boards with a working SD slot (the ESP32-32E boards:
3.2", 3.5", 4.0"): put the zips, as they are, in a `CYD-Classic-Arcade`
folder at the root of the card:

    CYD-Classic-Arcade/pacman.zip
    CYD-Classic-Arcade/invaders.zip

The firmware copies what it recognises into its own flash. The 2.8" boards
(no usable SD slot) get the same zips over USB from the web page.
