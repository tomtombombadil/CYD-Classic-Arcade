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
- Some games take either of two sets that differ only in their program
  chips (Donkey Kong: `dkong` or `dkongo`). Either one works; with both,
  the first in the table below is used. The chips of the other set are
  then never mixed in.

## The sets

| Game | MAME set | MAME title | Also give (split sets) |
|---|---|---|---|
| Pac-Man | `pacman` | Pac-Man (Midway) | `puckman` |
| Space Invaders | `invaders` | Space Invaders / Space Invaders M | - |
| Galaga | `galaga` | Galaga (Namco rev. B) | - |
| Dig Dug | `digdug` | Dig Dug (rev 2) | - |
| Donkey Kong | `dkong` or `dkongo` | Donkey Kong (US set 1) / (US set 2) | `dkong` for `dkongo` |
| Q\*bert | `qbert` | Q\*bert (US set 1) | - |
| Asteroids | `asteroid` (or `asteroid2`, rev 2) | Asteroids (rev 4) | - |
| Rally-X | `rallyx` | Rally-X (32k Ver.?) | - |
| Tempest | `tempest` (or `tempest3`) | Tempest (rev 3, Revised Hardware) / (rev 3) | - |
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
(`034602-01.c8`), which our emulator doesn't need. Both work. Their
`tempest` (twelve 2 KB chips, `136002.113` ... `136002.316`) is today's
`tempest3`; today's `tempest` holds the same program on 4 KB chips
(`136002-133.d1` ...). Either works. The vector generator's PROM
(`136002-125.d7`) and the math box PROMs (`136002-126.a1`, `-127` ...
`-132`) aren't needed: the emulator does what they do.

Older dumps also work where the data is the same: an old `spyhunt` has
`cpu_pg5.11d` with its two halves swapped (the loader checks both orders),
and an old `starwars` has the math box PROMs in another layout, and no
AVG PROM - our emulator needs neither.

Notes:
- **Galaga, Dig Dug:** non-merged sets also hold the Namco custom chips'
  own programs (`51xx.bin`, `53xx.bin`, `54xx.bin`). They aren't needed or
  used: the chips are modelled from their known behaviour (Galaga's
  explosions come from our own noise model). Older sets name Galaga's files
  `gg1-1b.3p` etc. and Dig Dug's partly `136007.1xx`; matching by CRC finds
  them all.
- **Rally-X:** older sets name the PROMs `im5623.*` / `m3-7603.11n`
  (same contents). The video layout and timing PROMs (`rx1-2.4n`,
  `rx1-3.7k`) aren't needed, and aren't in every set.
- **Only the ROM zips** are needed. MAME also has separate "sample" sets
  (recordings of analog sounds); this emulator doesn't use them - those
  sounds are modelled in the firmware.
- **Q\*bert:** the speech chip (Votrax SC-01) has its own internal ROM,
  `sc01a.bin`; non-merged sets include it. Keep it in the zip.

## Getting them onto the board

Two ways, on every board:

- **SD card:** put the zips, as they are, in a `CYD-Classic-Arcade` folder
  at the root of the card:

    CYD-Classic-Arcade/pacman.zip
    CYD-Classic-Arcade/invaders.zip

  At power-on the firmware copies new zips it recognises into its own flash.
- **USB:** the install site's Load ROMs page (Chrome or Edge) sends them
  over the USB cable.
