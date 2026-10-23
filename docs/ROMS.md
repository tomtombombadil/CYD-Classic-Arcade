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
| Ms. Pac-Man | `mspacman` | Ms. Pac-Man | - |
| Mr. Do! | `mrdo` | Mr. Do! | - |
| Tempest | `tempest` (or `tempest3`) | Tempest (rev 3, Revised Hardware) / (rev 3) | - |
| Qix | `qix` | Qix (Rev 2) | - |
| Burger Time | `btime` | Burger Time (Data East set 1) | - |
| Defender (stretch) | `defender` (or `defenderg`, Green label) | Defender (Red label) | `defender` for `defenderg` |
| Missile Command | `missile` (or `missile2`, `missile1`) | Missile Command (rev 3) / (rev 2) / (rev 1) | `missile` for `missile2` |
| Robotron: 2084 | `robotron` | Robotron: 2084 (Release 5, solid blue label) | - |
| Defender | `defender` or `defenderg` | Defender (Red label) / (Green label) | `defender` for `defenderg` |
| Stargate (stretch) | `stargate` | Stargate | - |
| Star Wars | `starwars` | Star Wars (Atari, set 1) | - |
| Battlezone | `bzone` (or `bzonea`, Rev 1) | Battlezone (Rev 2) / (Rev 1) | `bzone` for `bzonea` |
| Zaxxon | `zaxxon` (or `zaxxon2`) | Zaxxon (set 1, rev D) / (set 2, unknown rev) | `zaxxon` for `zaxxon2` |
| Galaxian | `galaxian` | Galaxian (Namco set 1) | - |
| Amidar | `amidar` | Amidar (Konami) | - |
| Dig Dug II | `digdug2` (or `digdug2o`) | Dig Dug II (New Ver.) / (Old Ver.) | `digdug2` for `digdug2o` |
| Donkey Kong Jr. | `dkongjr` | Donkey Kong Junior (US set F-2) | - |
| Elevator Action | `elevator` (or `elevatora`) | Elevator Action (BA3, 4 PCB version, 1.1) / (EA, 5 PCB version, 1.1) | `elevator` for `elevatora` |
| Gyruss | `gyruss` | Gyruss (Konami) | - |
| 1942 | `1942` (or `1942a`, `1942b`) | 1942 (Revision B) / (Revision A) / (First Version) | `1942` for `1942a` / `1942b` |
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
`cpu_pg5.11d` with its two halves swapped (the loader checks both orders).
An old `starwars` (files `136021.105` ... `136021.214`) has the math box
PROMs `136021.110` ... `.113` with stray bits above the 4 data bits in 14
bytes (other CRCs); the chips are 4 bits wide, so both dumps work (romset
variants). The AVG PROM (`136021-109.4b`) isn't needed. Only set 1's
program (`136021.214`) is recognised; `starwars1` / `starwarso` (program
`136021.114`) aren't.

Notes:
- **Battlezone:** the Rev 2 set (`bzone`, program `036414-02.e1` ...) or
  Rev 1 (`bzonea`; older MAME collections call it `bzone`, files
  `036414.01`, `036413.01` ... `036409.01`, `036421.01`, `036422.01`).
  Only the first program chip (`036414`) differs, so a split `bzonea.zip`
  needs `bzone.zip` beside it; with both sets present, Rev 2 is used. The
  vector generator's PROM (`036408-01.k7`) and the math box PROMs
  (`036174-01.b1`, `036175-01.m1` ... `036180-01.f1`) aren't needed. The
  cocktail set (`bzonec`) and Bradley Trainer have other programs and
  aren't read.
- **Galaga, Dig Dug:** non-merged sets also hold the Namco custom chips'
  own programs (`51xx.bin`, `53xx.bin`, `54xx.bin`). They aren't needed or
  used: the chips are modelled from their known behaviour (Galaga's
  explosions come from our own noise model). Older sets name Galaga's files
  `gg1-1b.3p` etc. and Dig Dug's partly `136007.1xx`; matching by CRC finds
  them all.
- **Rally-X:** older sets name the PROMs `im5623.*` / `m3-7603.11n`
  (same contents). The video layout and timing PROMs (`rx1-2.4n`,
  `rx1-3.7k`) aren't needed, and aren't in every set.
- **Burger Time:** the Data East set 1 (`aa04.9b` ... `ab9.15k`, 15
  files). It has no colour PROMs (the palette is in RAM). The other sets
  (`btime2`, `btime3`, `btimem`) have other program ROMs and aren't read.
- **Qix:** the Rev 2 set (MAME's `qix`, 16 files of 2 KB: `qq12_rev2.u12`
  ... or, in older collections, just `u4` ... `u19`, `u27`). The other Qix
  sets (`qixa`, `qixb`, `qixo`, `qix2`) have other program ROMs and aren't
  read.
- **Missile Command:** six 2 KB program chips. Today's `missile` is rev 3
  (`035822-03e.kl1`); rev 2 (`missile2`) differs in that one chip
  (`035822-02.kl1`), so a split `missile2.zip` holds only it and needs
  `missile.zip` beside it. Older collections call rev 2 `missile` with
  files `035820.02` ... `035825.02` (same contents). Rev 1 (`missile1`,
  `35820-01.h1` ...) works too. The write-enable PROM (`035826-01.l6`)
  isn't needed. The sets for the older boards (`missilea`, `missile2a`,
  `missile1a`, 1 KB chips) aren't recognised.
- **Robotron: 2084:** older sets name the files `robotron.sb1` ...
  `robotron.sbc` and `robotron.snd` (same contents). The two decoder PROMs
  (`decoder_rom_4.3g`, `decoder_rom_6.3c`, for the cocktail flip) aren't
  needed. Other releases (yellow label, the 1987 bug fix, ...) have other
  program ROMs and aren't supported.
- **Defender:** the Green label set (`defenderg`; older collections call
  it `defendg`, files `defeng01.bin` ...) shares its sound ROM with the
  Red label: a split `defenderg.zip` needs `defender.zip` beside it. With
  both sets present, the Red label is used. The decoder PROMs aren't
  needed.
- **Zaxxon:** set 1, rev D (MAME's `zaxxon`; its program ROMs carry a
  red "D"). Older collections name its 17 files `zaxxon.1` ... `zaxxon.15`,
  `zaxxon.u72`, `zaxxon.u98` (same contents; today `zaxxon_rom1d.u29` ...,
  `mro16.u76`). Set 2 (`zaxxon2`: program ROMs 3 and 1 and the character
  colour PROM `mro17.u41` differ) works too; a split `zaxxon2.zip` needs
  `zaxxon.zip` beside it. Set 3 and the Japanese set (encrypted) aren't
  read. The sound board's analog sounds are our own models - MAME's
  Zaxxon sample set isn't used.
- **Galaxian:** the Namco set (MAME's `galaxian`: `galmidw.u` `.v` `.w`
  `.y`, `7l`, `1h.bin`, `1k.bin`, `6l.bpr`; the program chips carry
  Midway's names). Midway's own set (`galmidw`) and the many bootlegs and
  clones have other program ROMs and aren't read. The `galaxian` sample
  set isn't needed: its sounds are modelled.
- **Amidar:** the Konami set (MAME's `amidar`: `amidar.2c` `.2e` `.2f`
  `.2h`, sound `amidar.5c` `.5d`, graphics `amidar.5f` `.5h`, the colour
  PROM `amidar.clr`). The Stern and bootleg sets (`amidaru`, `amidaro`,
  `amidarb`, ...) have other program ROMs and aren't read.
- **Dig Dug II:** the New Ver. (MAME's `digdug2`: program `d23_3.1d`
  cc155338, `d23_1.1b` 40e46af8; sound CPU `d21_4.1k` 737443b1;
  characters `d21_5.3b` afcb4509; sprites `d21_6.3m` df1f4ad8, `d21_7.3n`
  ccadb3ea; PROMs `d21-5.5b` 9b169db5 (palette), `d21-6.4c` 55a88695
  (character colours), `d21-7.5k` 9c55feda (sprite colours), `d21-3.3m`
  e0074ee2 (waveforms)) - Tom's set. The Old Ver. (`digdug2o`: program
  `d21_3.1d` be7ec80b, `d21_1.1b` 5c77c0d4 and sprite colours `d2x-7.5k`
  1525a4d1) works too; a split `digdug2o.zip` needs `digdug2.zip` beside
  it. The I/O chips' programs (56XX, 58XX) aren't needed: modelled.
- **Donkey Kong Jr.:** MAME's `dkongjr` (US set F-2; older names
  `dkj.5b` `dkj.5c` `dkj.5e` - 8 KB each, CRCs dea28158 6fb5faf6 d042b6a8 -,
  sound `c_3h.bin` 715da5f8, tiles `dkj.3n` `dkj.3p`, sprites `v_7c.bin` ...
  `v_7f.bin`, PROMs `c-2e.bpr` `c-2f.bpr` `v-2n.bpr`). The program chips
  are read whole; the board spreads their 2 KB pieces over 0000-5FFF.
  `dkongjr2` (the same code on 3 other chips), the Japanese sets
  (`dkongjrj`, `dkongjnrj`), the E kit and the bootlegs have other program
  ROMs and aren't read.
- **Elevator Action:** MAME's `elevator` (the 4-board set on 2764s:
  `ba3__01.2764.ic1` ... `ba3__04-1.2764.ic6`, graphics `ba3__05.2764.ic4`
  ... `ba3__08.2764.ic10`) or `elevatora` (the 5-board set on 2732s:
  program `ea_12.2732.ic69` ... `ea_19.2732.ic54` CRCs 24e277ef 13702e39
  46f52646 e22fe57e c10691d7 8913b293 1cabda08 f4647b4f, graphics
  `ea_20.2732.ic1` ... `ea_27.2732.ic8` CRCs bbbb3fba 639cc2fd 61317eea
  55446482 77895c0f 9a1b6901 839112ec db7ff692). Both have the sound ROMs
  `ea_9.2732.ic70` (6d5f57cb) and `ea_10.2732.ic71` (f0a769a1), the
  68705P3 MCU's program `ba3__11.mc68705p3` (9ce75afc, 2 KB - it is needed:
  the game talks to the MCU every frame) and the priority PROM `eb16.ic22`
  (b833b5ea). Older collections hold the 5-board set as `elevator.zip` with
  the files `ea-ic69.bin` ... `ea-ic54.bin`, `ea-ic70.bin`,
  `ea-ic71.bin`, `ea-ic1.bin` ... `ea-ic8.bin`, `ba3.11`, `eb16.22` (Tom's
  set): the same CRCs. The PAL dump (`ww15.pal16l8.ic24.jed.bin`) isn't
  needed. The bootleg `elevatorb` (protection patched out, no MCU) isn't
  read.
- **Gyruss:** the Konami set (MAME's `gyruss`, 15 files: main
  `gyrussk.1` `.2` `.3` (c673b43d a4ec03e4 27454a98), sub CPU `gyrussk.9`
  (822bf27e), sound `gyrussk.1a` `.2a` (f4ae1c17 ba498115), i8039
  `gyrussk.3a` (3f9b5dea), characters `gyrussk.4` (27d8329b), sprites
  `gyrussk.5` - `.8` (4f22411a c949db10 8e8d388c 47cd1fbc), PROMs
  `gyrussk.pr1` `.pr2` `.pr3` (7ed057de de823a81 98782db3)). Tom's zip is
  exactly this set. The Centuri set (`gyrussce`) has other main program
  ROMs (`gya-1.11j` ...) and isn't read; it would be a romset variant.
- **1942:** MAME's `1942` (Revision B), `1942a` (Revision A) or `1942b`
  (First Version) - only the five main program chips differ: Revision B
  `srb-03.m3` ... `srb-07.m7` (d9dafcc3 da0cf924 d102911c 466f8248
  0d31038c), Revision A `sra-03.m3` `sr-04.m4` ... `sr-07.m7` (40201bab
  a60ac644 835f7b24 821c6481 5df525e1), First Version `sr-03.m3`
  (612975f2) with Revision A's other four. The rest is the same in all:
  sound `sr-01.c11` (bd87f06b), characters `sr-02.f2` (6ebca191),
  tiles `sr-08.a1` ... `sr-13.a6` (3884d9eb 999cf6e0 8edb273a 3a2726c3
  1bd3d8bb 658f02c4), sprites `sr-14.l1` `sr-15.l2` `sr-16.n1`
  `sr-17.n2` (2528bec6 f89287aa 024418f8 e2c7e489), PROMs `sb-0.f1`
  (6047d91b, characters' lookup), `sb-1.k6` (712ac508, interrupt timing),
  `sb-4.d6` (4858968d, tiles' lookup), `sb-5.e8` `sb-6.e9` `sb-7.e10`
  (93ab8153 8ab44f7d f4ade9a4, red / green / blue), `sb-8.k3` (f6fad943,
  sprites' lookup); `sb-2`, `sb-3` and `sb-9` aren't needed. Tom's zip is
  an older `1942.zip` holding Revision A under the names `1-n3a.bin`
  `1-n4.bin` ... `1-n7.bin`, `1-c11.bin`, `1-f2.bin`, `2-a1.bin` ...
  `2-a6.bin`, `2-l1.bin` `2-l2.bin` `2-n1.bin` `2-n2.bin`, `f01_sb-0.bin`
  ... `01m_sb-9.bin` - the same CRCs. The bootleg `1942abl` (other chip
  sizes), the hack `1942h`, `1942w` and the Tecfri `1942p` aren't read.
- **Only the ROM zips** are needed. MAME also has separate "sample" sets
  (recordings of analog sounds); this emulator doesn't use them - those
  sounds are modelled in the firmware.
- **Ms. Pac-Man:** a parent set in today's MAME, so even a split
  `mspacman.zip` holds everything: the four Pac-Man program ROMs
  (`pacman.6e` ... `6j`), the aux board's `u5` `u6` `u7`, its own
  graphics `5e` `5f`, and the Pac-Man PROMs. A zip without the Pac-Man
  chips (from an old split collection) works with `pacman.zip` and
  `puckman.zip` beside it: chips are found by CRC in any zip. Bootleg sets
  (`mspacmab` and others, with the program unscrambled in other chips)
  aren't recognised.
- **Mr. Do!:** newer sets also have the protection PAL's dump
  (`u001_pal16r6cn.j2`); it isn't needed (the PAL's answers are modelled)
  and its absence is fine. The timing PROM `j10--4.bin` isn't needed either.
- **Q\*bert:** the speech chip (Votrax SC-01) has its own internal ROM,
  `sc01a.bin`; non-merged sets include it. Keep it in the zip.
- **Q\*bert:** non-merged sets also hold the speech chip's (Votrax SC-01)
  own ROM, `sc01a.bin`. It isn't needed or used: the speech is our own
  model. The sprite ROMs are the 8 KB `qb-fg*.bin` (older dumps of half
  that size, with other CRCs, aren't recognised).

## Getting them onto the board

Two ways, on every board:

- **SD card:** put the zips, as they are, in a `CYD-Classic-Arcade` folder
  at the root of the card:

    CYD-Classic-Arcade/pacman.zip
    CYD-Classic-Arcade/invaders.zip

  At power-on the firmware copies new zips it recognises into its own flash.
- **USB:** open Settings > USB Transfer on the board (the loader runs only
  while that page is open), then the install site's Load ROMs page
  (Chrome or Edge) sends them over the USB cable.
