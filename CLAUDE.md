# CLAUDE.md - working rules for this repo

CYD Classic Arcade: classic arcade games emulated on ESP32 "Cheap Yellow
Display" boards, played with a Bluetooth game controller. Sister project of
[CYD Classic Games](https://github.com/tomtombombadil/CYD-Classic-Games)
(same owner, MIT): copy from it freely, don't link it as a submodule.
Started 2026-10-14. Design: `docs/SPEC.md`; ROM sets: `docs/ROMS.md`.

## The developer
- Tom works on **Windows** with VS Code + PlatformIO. Never give Linux/bash
  commands in instructions for him; use the PlatformIO GUI or PowerShell.
- If a Python tool is ever needed on his side: always a venv, and
  `python -m pip install`, never bare `pip install`.
- He does not want to repeat instructions. Anything decided goes in this
  file, `docs/SPEC.md`, `docs/ROMS.md` or `THIRD_PARTY_NOTICES.md`.
- Tom touches the screen with a Nintendo DS Lite stylus, firm presses.

## Decisions (Tom, 2026-10-14)
- Games: Space Invaders, Asteroids, Pac-Man, Donkey Kong, Galaga, Dig Dug,
  Q*bert, Rally-X and Qix (Tom added the last two, 2026-10-14). Stretch:
  Defender, Stargate, Spy Hunter. Star Wars added to the games (Tom,
  2026-10-14); Rolling Thunder a stretch goal (Tom's old favourite).
- Tom's ROM zips (in roms/, never committed): pacman, invaders, galaga,
  digdug (rev 2), dkongo (Donkey Kong US set 2), asteroid (= today's
  asteroid2, rev 2, no DVG PROM), qbert (no sc01a.bin - no Votrax ROM),
  defendg (= defenderg, Green label), qix (Rev 2), rallyx, starwars (set
  1 program; old math box PROM dumps, no AVG PROM), spyhunt (old dump:
  cpu_pg5.11d halves swapped vs today's 88aa1e99), rthunder (rev 3).
  Stargate: Tom is still looking. No Namco MCU
  ROMs (51xx/53xx/54xx). Support these revisions.
  Added 2026-10-15 (Tom: "here's five more games"): mspacman (non-merged,
  with the aux board's u5 u6 u7), btime (Burger Time, aa/ab names),
  tempest (136002.xxx), robotron (old robotron.sb* names, no decoder
  PROMs), mrdo (a4-01.bin ... u8-10.bin).
- **No ROMs in the repo, ever** (nor sample WAVs, nor ROM-derived data such
  as converted headers). Users add their own standard MAME sets. `roms/`
  and `samples/` are git-ignored for local testing; never commit from them.
  The loader matches chips by CRC-32, not file names (`docs/ROMS.md`).
- Controls = a Bluetooth controller via **Bluepad32** (Tom: non-commercial
  use, so BTstack's terms are fine). No on-screen game controls; touch is
  for menus and settings only.
- Sound: the board's speaker connector with a plain 8-ohm speaker - the
  ESP32 DAC (GPIO26) into the board's own amp. No extra hardware.
- License: as free as possible - MIT for our code. BTstack (in the built
  firmware) is free for non-commercial use only; say so in the README and
  THIRD_PARTY_NOTICES.
- Clean-room emulators: MAME, Galagino and other GPL / non-commercial
  emulators are read for hardware facts only, never copied.
- Firmware file prefix `TTB-CYD-CA_` (Classic Arcade).
- Orientation (Tom, 2026-10-14): each game runs in its cabinet's own
  orientation and fills the screen; the player turns the CYD on its side
  for landscape games (Asteroids, Rally-X, Star Wars, Defender). The
  firmware rotates the panel when a game starts and back for the menus.
  A game is shown as large as fits with its shape kept (borders only
  where the shapes differ).
- Pac-Man's sound and screens confirmed right by Tom (2026-10-14, PC run).
- New games (Tom, 2026-10-14): send him the PC screenshots (and a sound
  clip), then push the game straight into a build - no waiting for his OK
  on the shots; he tests on the boards when the shots look good.
- Rally-X (`emu/machines/rallyx.*`, landscape 288x224). Emulated: Z80,
  the 32x32-tile playfield with its scroll X / Y registers (3 pixels left
  of the registers, as on the board), the fixed 8x32-tile score / radar
  area right of it (its 64 pixels show tile columns 4-7 then 0-3), 6
  sprites, tiles that cover sprites (attribute bit 5), the radar dots from
  the dots PROM (in the palette's second bank), the WSG (this set uses the
  sound-on latch bit, so it's honoured), the watchdog (16 frames, assumed
  as on Pac-Man). Modelled: the crash "BANG" (`emu/sound/rallyx_bang.*`:
  2 kHz shift-register noise chopping a 1 uF capacitor's charge, 840 Hz
  low-pass + 34 Hz high-pass, part values from the board). Flip screen
  (cocktail) isn't drawn: the cabinet DIP reads upright. Costs about what
  Pac-Man does (PC: emu 0.05, render 0.08 ms a frame), but the scrolling
  playfield changes nearly every line, so the changed-lines-only push
  saves little in play.
- Controller test v0.1.0 on Tom's 2.8" ST7789 (2026-10-14): 8BitDo
  Ultimate Bluetooth and Vilcorn D8 pair and work (two at once), every
  button, both sticks' directions, DAC tones OK. ShanWan Q36 (two units)
  not found; an 8BitDo "80EL" unclear (charge?). Bluepad32 names buttons
  by POSITION (Xbox layout: A = bottom), so on Nintendo-labelled pads A/B
  and X/Y read swapped. L1+L2 -> L and R1+R2 -> R by design; the right
  stick is unused. ShanWan Q36 works with its switch on "D" (shows as
  "Q36 for Android"; two of them pair as P1 and P2) - except its Home
  button, which never arrives.
- Controller test also confirmed by Tom on the 2.8" ILI9341, 3.2" ST7789,
  3.5" and 4.0" ST7796 (2026-10-14): every supported board is tested.
- SPI 80 MHz on the ST7796 boards too (v0.6.3; Tom: "almost positive all
  the ST7789 and ST7796 will do the increased display rates"). The ILI9341
  stays at 40 MHz.
- Right stick and analog (v0.6.3): PAD_RUP..PAD_RRIGHT (bits 13-16,
  `games::kPadRUp`...) for twin-stick games (Robotron), and
  `Machine::set_analog(games::Analog)` - both sticks -512..511, both
  controllers added - every frame for spinners / trackballs (Tempest).
- Measured on Tom's 2.8" ST7789 (v0.1.0, Bluetooth on): RAM 142 KB free,
  biggest block 108 KB, lowest 142 KB. A 224x288 frame takes 25.9 ms at
  40 MHz SPI (38 fps max) - too slow for 60 fps, so: SPI at 80 MHz where the
  panel takes it (HSPI's own IOMUX pins allow it) and send only the lines
  that changed since the last frame (a hash per line).
- Buttons by POSITION (Tom, 2026-10-14): bottom face button = the main
  one (fire / jump), right = second. A setting "Swap A/B and X/Y" in the
  menu for players who want the printed letters (`pad_set_swap_ab()`).
- SD card layout (Tom, 2026-10-14): ROM zips as they are in
  `/CYD-Classic-Arcade/` at the card's root, on every board (the 2.8"
  too, v0.3.0: touch isn't attached there, so VSPI is the SD card's - port
  CYD-BYOG-PoolRadSeries' software touch driver when touch is needed).
  Or over USB from the web page.
- v0.2.0 on the ESP32-32E boards (Tom): no picture in games, then none in
  the menu - the display's DMA broke once the SD card's SPI bus (IDF
  sdspi, DMA) had run. Fix (v0.3.0): the SD card is read and its bus
  freed BEFORE the display starts, and the screen is drawn with blocking
  pushImage, never DMA - as Tom's CYD-BYOG-PoolRadSeries does on every
  board. Don't bring display DMA back without testing on a 32E board.
- Exit a game (Tom, 2026-10-14): hold Select and press Start twice, like
  most retro handhelds - or Home. While Select is held, Start goes to the
  gesture, not the game; the coin drops when Select is let go unused
  (`main/input.*`). Replaces v0.2.0's Start + Select held 1 s.
  After the second Start, Start does nothing more until Select is let go
  (Tom, v0.3.0: a third press often started the game again), and the menu
  ignores every button until all are let go (`Input::wait_for_release()`;
  games too, so the press that opened one doesn't reach it).
- Web flasher caching (v0.3.0: Tom's browser flashed a cached v0.2.0 file
  for the 4.0" from the v0.3.0 page): every manifest / firmware link
  carries the build's stamp (`?v=<version>-<time>`, boards.json `stamp`).
- v0.3.0 / v0.3.1 confirmed by Tom on the 3.5" and 4.0": games play and
  show (the no-DMA + SD-first fix works).
- Scaling (Tom, 2026-10-14: games were 1:1 and small on the 3.5"): games
  are scaled up to fill the panel's width with their shape kept, nearest
  pixel, no blending (224 wide -> 320 on the 320x480 panels; Pac-Man shows
  as 320x411). 240x320 panels stay 1:1.
- Landscape games (Rally-X first): the panel turns a quarter clockwise
  from the menu's portrait for the game and back after (`screen::landscape()`); scaling as above.
- 6502 core (`emu/cpu/m6502.*`, for the Atari games and Burger Time):
  NMOS, documented opcodes with NMOS decimal mode flags, the JMP ($xxFF)
  wrap, cycle timing with page-cross / branch extras, IRQ delayed one
  instruction after CLI / PLP, edge NMI. Undocumented opcodes run as NOPs
  of their length and are counted (`illegal_count`). 256-byte pages
  (boards bank RAM at that grain). `opcode_hook` sees each opcode fetch
  and whether the last instruction wrote (Burger Time's DECO CPU-7
  decryption). PC: ~800 emulated MHz (the loop and its memory helpers
  inlined, one flag to look at the interrupt lines).
- 8088 core (`emu/cpu/i8088.*`, for Q*bert): real mode, every 8086 opcode
  with the 8088's quirks (60-6F = Jcc, C0/C1/C8/C9 = RET/RETF, SALC,
  SETMO, POP CS, PUSH SP = new SP, word wrap at offset FFFF, divide error
  returns past the instruction, REP before IDIV flips the quotient). Passes
  the SingleStepTests 8088 suite (defined flags; undefined ones masked).
  Timing = documented cycles + 4 a word transfer, at least 4 a fetched
  byte (no prefetch queue): ~1.04x the suite's measured cycles. REP string
  ops run to their end in one run() unless an interrupt is pending between
  elements (then it's taken and the IRET repeats the rest) - so run budget
  slicing never changes timing. 20-bit bus with
  `addr_mask`; the low 64 KB through 256-byte pages. PC: ~1,400 emulated
  MHz on a simple loop.
- No room in internal RAM (IRAM) for the CPU cores: v0.3.3 put the Z80 /
  8080 interpreters there (`emu/cpu/hot.h`, EMU_HOT_IRAM) and the link
  overflowed IRAM by 48,428 bytes (Bluetooth uses most of it). EMU_HOT
  stays a no-op hook; speed has to come from the code (Pac-Man's one Z80
  takes ~5.7 ms a frame from flash, ~9 emulated MHz - Galaga has three).
- Pacing (v0.5.0, Tom: Rally-X crawled and surged on the 4.0", slowed on
  the 3.2"): a frame is drawn only when the sound queue holds enough to
  last through the draw (`draw_est`, measured); else drawing is skipped.
  The queue grows to cover the slowest recent draw (max ~128 ms). A
  scrolling Rally-X shown 411x320 on the 4.0" is ~50 ms a full picture at
  40 MHz SPI, so it shows ~12 pictures a second at full game speed.
- Menu help text at the bottom wraps to the screen (`screen::text_box`),
  in the small font on every panel (Tom: it ran off the right edge).
- Menu fonts (v0.6.2, Tom: the list ran into the help text on the 2.8";
  "it could all be a little smaller"): DejaVu12 items / DejaVu9 help on
  240-wide panels, DejaVu18 / DejaVu12 on 320-wide; the list scrolls (with
  arrows) when it doesn't fit above the news line.
- Interrupted draws (v0.6.2, Tom: Galaga's start tune crackled at its end,
  where the whole screen changes at once): a draw stops at a line boundary
  when less than a frame of sound is queued, and the next draw goes on
  from that line (unsent lines keep their old hash). The sound never runs
  dry for drawing.
- Game start (v0.10.0, for Robotron's ~104 KB): the machine is made first
  (its object is the biggest block), then the ROMs are loaded; machines
  are made with `new (std::nothrow)` - on the board a failed plain new
  aborts (no exceptions) - so a game that doesn't fit says "Not enough
  memory". Screen strips are 8 lines (7.5 KB at 480 wide).
- Game count: the catalog holds `games::kMaxGames` (64; v0.6 held 16),
  checked by `arcade_host --check-table`; the USB loader's GAMES reply has
  room for ~40 ids (600 chars; the usb task's stack is 6 KB for it).
- ROM regions: the firmware's loader holds `romset::kMaxRegions` (12);
  v0.6.0's Dig Dug had 9 of the old 8 and "Can't load the ROMs" on the
  boards. `arcade_host --check-table` (run by the host tests) checks
  every game and prints its ROM RAM.
- Screen (Tom, 2026-10-14): portrait with the USB port at the TOP by
  default (the board stands up, cable out the top; rotation 2). The 2.8"
  ILI9341 is the other way up (Tom, v0.5.0), so the menu setting is
  "Rotate Screen 180: On / Off" (NVS rot180; v0.3-v0.5's "USB Port"
  usb_top carries over) - Tom: no per-board code for which end is up.
- 2.8" ILI9341 (Tom, v0.5.0): no sound at all, and his 8BitDo Ultimate
  only connected after a reboot each time. v0.5.1: sound 4x louder (it
  was ~a fifth of CYD Classic Games' loudness at the same setting; v0.5.2
  reworked it, see Sound) and a
  menu item "Forget Controllers" (drops connections and pairing keys - a
  controller moved between Tom's boards). Pairing fix unconfirmed.
  v0.6.1 on the ILI9341: the DAC starts (no "didn't start" note) but not
  even the Controller Test tone is heard. v0.6.2: menu setting "Sound
  Output: DAC / PWM" (NVS snd_pwm, restarts the board): PWM = LEDC on
  GPIO26 (high-speed channel 0 / timer 0; the backlight has channel 7 /
  timer 3), 10 bits at 78,125 Hz, duty set per sample by a 24,024 Hz
  gptimer interrupt from an 85 ms FIFO; at rest the pin slides to low (as
  Classic Games' tones leave it). Tom, 2026-10-15: his 2.8" ILI9341
  has no sound in ANY firmware (his PoolRad too) - the board's own fault
  (he had no speaker for it before). Don't chase it further; the PWM
  setting stays for boards that need it.
- 4-way games (Pac-Man, Donkey Kong; `games::Info::four_way`): a d-pad
  diagonal counts as the direction pressed last (Tom found Pac-Man hard
  to steer).
- Tom, v0.2.0 on the 2.8" ST7789: ROMs over USB worked, Pac-Man and Space
  Invaders "played perfectly". 4.0" log: Pac-Man emu ~5.7 ms + draw ~6-8
  ms a frame (of 16.5), every frame drawn.
- **ROM zips only, no sample files** (Tom, 2026-10-14: on handhelds the
  zip is all a player ever needs). Analog sounds are always our own
  models in the firmware. `emu/sound/samples.*` stays for the PC tool
  only (`arcade_host --samples`, to compare a model with a recording).
- Home isn't on every controller (Q36): leaving a game also works with
  Select + Start twice.
- Donkey Kong (`emu/machines/dkong.*`, set `dkong` or `dkongo` as romset
  variants): emulated = Z80 + tiles / sprites (16 a line) / palette PROMs,
  the 8257 sprite DMA (an instant copy, the Z80 stalled 8 cycles a byte),
  and the 8035 sound CPU (`emu/cpu/i8035.*`) with its DAC - music, death,
  roar, coin, spring, falling, prize are exact. Modelled
  (`emu/sound/dkong_sound.*`): the discrete walk, jump and stomp circuits
  (555 VCOs, CMOS wobble oscillators, RC envelopes, the noise counter) and
  the DAC's fade (P2.7) and 1.9 kHz Sallen-Key filter; the mixer's ~300 Hz
  output filter is left out (too dull on a small speaker). The upright
  game runs with flip screen ON (that is its normal picture); flip = the
  whole picture turned 180 degrees. Attract mode is silent on the real game.
- Ms. Pac-Man (`emu/machines/mspacman.*`, a subclass of the Pac-Man
  machine): emulated = Midway's aux board - U5 / U6 / U7 read through its
  rewired address and data lines (decoded once at init into 20 KB: U5 + U6
  at 8000-97FF, U7 at 3000-3FFF, the ten Pac-Man pages that hold the forty
  8-byte patch windows), and the decode latch with its trap windows (any
  access to 3FF8-3FFF sets it - every VBLANK's IM 2 vector fetch from 3FFC
  does; 0038 03B0 1600 3FF0 8000 97F0 clear it). Pages with a window go
  through the read callback. Left out: the 2120 window (its page runs
  ~7,000 reads a frame and nothing reaches it with the latch set - logged
  over a 90 s game: only the boot ROM check, latch already clear, and
  blue-time random reads below 2000 touch windows). Latch at power-on:
  set (unknown on the board; the game sets it at its first VBLANK).
  PC: emu ~0.048 ms (Pac-Man 0.041), render as Pac-Man. Its tunes peak
  ~13,000 (same WSG scale as Pac-Man, which peaks ~19,700).
- Mr. Do! (`emu/machines/mrdo.*`, portrait 192x240 - the board's 240x192
  turned): emulated = Z80 at 4.1 MHz, the VBLANK IRQ held until taken
  (stepped an instruction at a time while interrupts are off, so it is
  taken exactly once), two SN76489s (`emu/sound/sn76489.*`, our own: each
  output sample the average over its time, so tones above 12 kHz fade
  instead of aliasing; powered up silent - a real chip may beep), both
  tile layers (back layer scroll X / Y, attribute bit 6 = pixel 0 drawn
  too), 64 sprites, the palette from the U2 / T2 PROMs through the
  resistor + diode network (MAME's model of it). Modelled: the protection
  PAL at 9803 - the game writes a table byte to the front layer, steps HL
  to the next one and compares bits 1-6 of 9803 with (HL), so 9803 reads
  the program ROM at HL (the PAL's equations aren't known well enough).
  Assumed: a ~15 Hz high-pass on the output (the chips only go positive);
  flip screen (cocktail) not drawn. Idle skip: the game waits in JR $ at
  0154 after its frame's work; a CPU found there (512-cycle slices) skips
  to the frame's end, R counted - checked identical (frames, sound, R)
  with and without it. Attract mode is silent (sound starts with a coin).
  PC: emu ~0.050 ms, render ~0.067 ms (two layers; tile columns unpacked
  once per 8 lines); sound peaks ~17,000.
- Z80 core fix (2026-10-15, found with Mr. Do!'s idle skip): JR e took 17
  T-states, not 12 (the table had the taken time and the macro added 5
  more). Every Z80 game ran a little fewer instructions a frame than the
  real board; `mspacman_mrdo_tests` now checks every unprefixed opcode's
  time against Zilog's table.
- Burger Time (`emu/machines/btime.*`, set `btime` = Data East set 1;
  portrait 240x240, 57.44 Hz, 4-way, A = pepper; set_dips = DSW2, DSW1
  fixed at 1 coin 1 credit, upright). Emulated: the DECO CPU-7 = our 6502
  + `deco7_decrypt` in the opcode hook (an opcode fetched right after a
  write, at an address with bits 8 and 2 set, has its bits swapped 6 5 3 4
  2 7 1 0 - no undocumented opcode runs in 4000 frames of play), VBLANK
  polled in DSW1 bit 7, the coin IRQ (held until the hook sees the
  handler's first fetch), the sound 6502 (latch IRQ, NMI every 16 lines
  gated by C000), two AY-3-8910s (`emu/sound/ay8910.*`, our own: counters
  exact; ones that can't be heard are carried along lazily; tones above
  ~23 kHz count as their average), characters, 8 sprites, the background
  map page (4004), palette RAM. Modelled: channel 2A's band-pass (187 Hz,
  Q 1.88, gain 4.7 - MAME's R51 = 5k, itself a guess) and the speaker's
  coupling high-pass (~530 Hz, MAME's 3 ohm guess). With gain 4.7 the
  music in play peaked ~2,500 against the thump's ~19,000 - too quiet on a
  small speaker - so kBandGain is 2.0 and kOutGain 55,000: music ~5,000,
  the loudest (coin / start tunes, thump) ~21,000. Graphics are
  decoded as drawn into a 255-glyph cache (~0.35 decodes a frame), not
  30 KB of tables: machine ~22 KB + 52 KB of ROM. Speed: the VBLANK waits
  (LDA 4003 / BPL or BMI) skip to the read that sees the change, the sound
  CPU's jump-to-self to its next interrupt (PC in play: emu ~0.05, render
  ~0.065 ms a frame). Flip screen = the picture turned 180 degrees
  (unseen: the upright game never sets it). No watchdog on this board.
- Q*bert (`emu/machines/qbert.*`, set `qbert` US set 1, portrait 240x256:
  the monitor is turned like Donkey Kong's, MAME's ROT270). Emulated: the
  8088 (5 MHz, 318 x 256 clocks a frame = 61.42 Hz, NMI at line 240), RAM
  (0000-0FFF battery RAM starts as FF; not kept over power-off), tiles
  from ROM, 64 sprites (MAME's measured -4 / -13 offsets, none in the
  first 8 columns), 16-colour palette RAM (2K/1K/470/240 ohm weights),
  background-over-sprites bit, flips, the watchdog (16 frames), and the
  sound board: 6502 + 6532 RIOT (RAM, ports, PA7 edge IRQ, timer) + DAC,
  its program from the set - the effects and music are exact. Modelled
  (`emu/sound/qbert_sound.*`): the Votrax SC-01 speech (Tom's set has no
  sc01a.bin; the 6502 still sends every phoneme and gets its A/R NMI
  after about the datasheet's phoneme length): a pitch sawtooth / noise through two
  formant resonators per phoneme (textbook English formants) - it babbles
  like Q*bert, but it isn't the chip's voice; the speech clock DAC -> 0.6x
  to 2x speed and pitch is our own guess. The knocker (5803 bit 5 rising):
  a click + 60 Hz / 150 Hz damped booms, 0.45 s. Sound mix: DAC +-14K,
  speech <= 12K, knocker <= 20K, soft knee above 16K (peaks ~19.5K seen).
  Controls (`games::Info::diagonal`, `diagonal_pad()`): the cabinet's
  stick is turned 45 degrees, so the d-pad turns a step clockwise - up =
  up-right, right = down-right, down = down-left, left = up-left (the
  usual handheld layout) - and a held diagonal (or the stick) moves on that diagonal: up +
  right = up-right etc.; opposites cancel. Select = coin, Start = 1P, L =
  2P. Idle skipping, exact (checked frame by frame against runs without
  it: RAM, picture, sound): the 8088's frame wait at B36D (CMP AX,[0080]
  / JZ, 35 cycles a turn) and the 6502's command wait at 7035-704C (29
  cycles a turn), whole turns only, never with an interrupt pending. PC:
  ~7-12K 8088 + 0-10K 6502 cycles a frame run, emu ~0.02-0.04 ms, render
  ~0.065 ms a frame. RAM: 68 KB of ROM regions (the sprite ROMs' blank
  lower halves included - the loader takes whole files) + ~20 KB machine.
  Unsure: the sprite offsets / clip (MAME calls them a guess), the speech
  clock mapping, the 8088 timing (no prefetch queue).
- Vector games: `emu/machines/vector_display.*` keeps each picture as a
  list of clipped segments in output pixels (1/16 px fixed point, at most
  1024, ~20 KB with the sort and active lists), sorted by first row, and
  draws one output line at a time (1-pixel lines, dots 2 x 2, later lines
  on top) - no frame buffer. The machine builds a whole picture inside one
  run_frame (begin / add / finish).
- Asteroids (`emu/machines/asteroids.*`, set `asteroid` (rev 4) or
  `asteroid2` (rev 2, Tom's; older MAME's `asteroid`) as romset variants,
  no DVG PROM): emulated = 6502 at 1.512 MHz, NMI every 6144 cycles (off
  in self-test), RAMSEL page swap, A15 mirror, inputs / DIPs, watchdog, the
  DVG (`emu/machines/atari_dvg.*`: its instructions VCTR / LABS / HALT /
  JSRL / RTSL / JMPL / SVEC run directly - no state machine PROM - with the
  bit-rate multipliers' exact step counts, run at VGGO; busy for its
  drawing time). The picture: Y 88-935 of the DVG square fills 240 rows,
  X at the same scale (rocks round), centred - 320 x 240. Idle skipping:
  the main loop's NMI wait and DVG wait (recognised by their code) jump
  ahead; PC ~0.04 ms a frame emulated, ~0.01 ms rendered. Modelled
  (`emu/sound/asteroids_sound.*`): the 12 kHz noise register, explosion
  (noise held at the pitch clock x volume, 52 Hz RC), thump (555 VCO
  frequency from the resistor DAC and current source: 207 Hz at 0, 170 Hz
  at 4 - the two the game alternates - 65 Hz at 15), saucer (triangle VCO
  swept 8.25 / 5.75 Hz), ship / saucer fire (pitch ramps, RC decay), thrust
  (noise, RC, 89.5 Hz band-pass, 160 Hz), extra life 3 kHz. Levels are set
  for a small speaker, not the board's mixer weights (explosion / thrust
  would be ~20x the fire sounds but sit below 100 Hz). Unsure: the thump's
  exact pitch (the 555 current source estimated from part values) and the
  DVG's per-instruction time (2 clocks a word). Attract mode is silent, as
  on the real game.
- POKEY (`emu/sound/pokey.*`, our own): event-driven - each sample is
  the exact average of the level between underflows; dividers (64 / 15
  kHz, full clock, joined pairs), 4 / 5 / 9 / 17-bit polys (tables
  generated at compile time, flash), high-pass flip-flops, volume-only,
  RANDOM timed by the CPU clock passed in. Silent channels' events are
  skipped. No serial / keyboard / timer IRQs; pot / ALLPOT values are
  set by the machine.
- Tempest (`emu/machines/tempest.*`, set `tempest` (rev 3, 4 KB chips)
  or `tempest3` (Tom's; the same program on 2 KB chips, older MAME's
  `tempest`) as romset variants; no AVG / math box PROMs): emulated = 6502
  at 1.512 MHz, IRQ every 6144 cycles cleared by the watchdog write, the
  AVG (`emu/machines/atari_avg.*`: VCTR / HALT / SVEC / STAT / SCAL /
  CNTR / JSRL / RTSL / JMPL run directly, normalisation and timing as the
  hardware's counters, closed form checked against the step-by-step one),
  the colour RAM, the math box (`emu/machines/atari_mathbox.*`: the
  microprogram's commands as arithmetic, the same rounding and division
  loop), 2 POKEYs (sound; ALLPOT = the spinner's 4-bit count / the
  buttons and switches, read directly as old MAME did), RANDOM (Tempest
  checks that a read 4 cycles later shows the high nibble in the low
  one), EAROM in RAM. The AVG loops on its list on the real board; here
  the list is drawn from its start to its jump back to word 0 every other
  frame (the game makes ~27 pictures a second). Shown 240 x 320, the AVG's
  580 x 570 window stretched to fill it as the portrait tube does (as
  MAME shows it; unsure if a real cabinet's web looks this tall). Spinner:
  clockwise on the screen counts the knob down; d-pad 1.5 counts a frame
  (a lane every ~5 frames), the stick up to 4 a frame in proportion
  (`set_spinner()`; the game reads the 4-bit count's change, so faster
  would alias). Idle skipping: the main loop's wait for the IRQ count
  (LDA zp; CMP #n; BCC, found by its code) jumps to the next IRQ. PC ~0.06
  ms a frame emulated (the game keeps its CPU busy: ~6800 instructions a
  frame), ~0.025 ms rendered. Sound: the two POKEYs x 4, AC-coupled; game
  peaks ~20,000.
- Williams boards (`emu/machines/williams.*`; Robotron: 2084 and
  Defender). Emulated: the 6809 at 1 MHz (260 lines of 64 cycles, 60.10
  Hz); the 304x256 bitmap of 4-bit pixels in 38 KB of RAM (columns of
  pixel pairs, 256 bytes apart), shown as MAME's visible 292x240; the 16
  palette registers (BBGGGRRR, resistor weights); ROM banked over the video
  RAM for reads (Robotron, C900) or at C000 (Defender, D000); two 6821 PIAs
  (`pia6821.*`) with the interrupts from the line counter (CB1 = line bit
  5, CA1 = line >= 240); the video counter; the 4-bit CMOS RAM; the
  watchdog (60 frames - assumed, the real length isn't documented); and
  the sound board (`emu/sound/williams_sound.*`: 6808 at 894.9 kHz + PIA
  + DAC running the sound ROM, so the sound is exact; a 10 Hz DC blocker
  as the output coupling; full DAC swing = +-10,900, loudest peaks
  ~21,000). Not drawn: flip screen (cocktail). The picture is the video
  RAM as the frame ends; frames run from line 128 to line 128 (a test of
  one-frame dropouts found no difference from line 0 or 240). The CMOS
  RAM is RAM only (empty at every start): the first boot restores the
  factory settings and then waits for the operator's Advance button, so
  the machine presses it once the CMOS writes stop (Robotron: shows
  "FACTORY SETTINGS RESTORED" ~1 s). Idle loops (Robotron waits for the
  next interrupt in "LDA <10 ; CMPA #2 ; BCS"): between 128-cycle slices
  the CPU is followed for up to 12 instructions; a round trip that leaves
  every register as it was, writes nothing and touches no I/O is skipped
  to the next line event in whole rounds - checked exact (same PNGs and
  WAV with and without it); a probe that finds nothing doubles the wait
  before the next (up to 16 slices), so a busy CPU isn't slowed. Memory: the machine object ~52 KB (48 KB of
  RAM) + Robotron's ROM in RAM 52 KB (blocks of 36 / 12 / 4 KB) = ~104 KB,
  a little over the ~100 KB aimed at (140 KB free, largest block 108 KB
  with Bluetooth) - not yet run on a board.
- Robotron: 2084 (set `robotron`, Release 5 / solid blue label; old dumps
  name the files robotron.sb1 ... sbc, robotron.snd). The SC1 blitter
  (`williams_blitter.*`): instant, the CPU held ~1 cycle a byte (2 when
  slow), width / height XOR 4 (the SC1 bug), source reads through the
  CPU's map (ROM when banked), the old destination byte from RAM. Twin
  stick: left stick / d-pad moves, the right stick fires
  (`games::kPadRUp`...), and Y / A / X / B fire up / down / left / right
  for pads without a usable right stick. Boot: ~7.5 s of RAM test noise,
  "INITIAL TESTS INDICATE: OPERATIONAL", then the attract mode (silent, as
  on the real game). PC: emu ~0.035-0.04 ms a frame in play (393K host
  instructions a frame by callgrind, Pac-Man 483K), render ~0.035 ms.
- Defender (set `defender` Red label or `defenderg` Green label - romset
  variants; Tom's `defendg` is the Green label). The first Williams board:
  no blitter, ROM banks 1 / 2 / 3 / 7 at C000 (D000 picks), I/O in bank 0.
  Its watchdog is C3FC with any data (the game writes 38; MAME maps C3FF /
  39 and never times it out). Boot: ~21 s of RAM test noise (the CPU tests
  the RAM itself - assumed as on the board), "UNIT OK", then with the
  empty CMOS "PRESS ADVANCE WITH SWITCH SET FOR AUTO FOR GAME OVER" for
  ~1 s before the bookkeeping pages: the machine presses Advance with Auto
  Up 10 frames after the CMOS writes stop, and the attract mode follows.
  Controls: up / down move; A fire, B thrust, X smart bomb, Y hyperspace,
  R reverse (the cabinet's buttons); left / right on the stick do what a
  player means: the way the ship faces = thrust, the other way = Reverse
  (pressed 3 frames on / 3 off until the ship has turned), then thrust.
  The facing comes from RAM A098 (03 right, FD left, other values = not
  playing: the stick does nothing then), found by watching the Green
  label; the Red label is assumed to keep it there (not checked - no ROMs).
  PC: emu ~0.04-0.065 ms a frame in play (648K host instructions a frame
  by callgrind: 1.3x Pac-Man's 483K; the sound CPU is half of it - the
  game's waveform loops write the DAC ~40,000 times a second), render
  ~0.035 ms.
- Idea, later (Tom): Spy Hunter / Rolling Thunder only on ESP32-S3 or -C5
  boards (more RAM / PSRAM). Note: Bluepad32 on S3 / C5 is BLE only, so
  PlayStation / Switch Pro controllers don't work there - Xbox and BLE
  pads do.

## Board naming (Tom's rule, same as CYD Classic Games)
- Name boards by what a user can identify: **screen size, display driver,
  touch type**. Never by an information site (LCDwiki) or vendor codes.
- Firmware files: `TTB-CYD-CA_<size>in_<DRIVER>_<Resistive|Capacitive>.bin`.
  No version in the file name.
- PlatformIO envs are short (`cyd32_st7789_res`) and carry
  `custom_firmware_name`, `custom_board_title`, `custom_board_hint`, and
  `custom_board_tested = no` until Tom has run THIS firmware on that board.
  platformio.ini is the single source for CI and the flasher.
- `main/boards/board_select.h` is the only file that tests `CYD_BOARD_*`.

## Architecture
- PlatformIO with **ESP-IDF** (`framework = espidf`, no Arduino), pioarduino
  platform 55.03.31-2 (ESP-IDF 5.5). `src_dir = main` (the ESP-IDF main
  component). Root `CMakeLists.txt` adds BTstack's ESP32 port
  (`BTstack_project.cmake`) and external components.
- Outside code is not vendored: `tools/fetch_deps.py` (a PlatformIO pre-
  script, also runnable by hand) downloads pinned GitHub zips into
  `external/` - Bluepad32 5.0.0-beta0, BTstack v1.8.2, LovyanGFX 1.2.31. No
  git or submodules needed on Tom's PC. Change a pin = edit `DEPS` there.
- Cores: Bluetooth (BTstack run loop in app_main) on core 0; screen, sound
  and the emulators on core 1.
- `emu/` - the emulators, plain C++17 (no ESP-IDF / Arduino), host-built:
  CPUs (`emu/cpu/`), sound chips (`emu/sound/`), machines
  (`emu/machines/`), ROM sets / zip / CRC (`emu/rom/`), `emu/util/`.
  Hot paths allocate nothing; big tables `const` (flash); machine RAM small.
- ROM sets may have variants (`romset::Set::variants`, a ROM's `variant`):
  alternative program-ROM sets of one game; the first complete one loads.
- `emu/games.*`: the game table shared by firmware and PC tools (machine
  face, screen, orientation, controller -> panel buttons: Select = coin,
  Start = 1P start, L = 2P start, A = fire). Adding a game = machine +
  entry there.
- Namco Galaga-family board (`emu/machines/namco_galaga_common.*`): three
  Z80s sharing RAM, run in 512-cycle slices (main first; interrupts on
  slice edges), latch / 06XX / WSG / watchdog shared by Galaga and Dig Dug.
  The custom MCUs' programs aren't in the sets, so the 51XX (coins,
  credits, sticks) and 53XX (DIPs) are behaviour models (`namco_io.*`, after
  the old MAME HLE's behaviour). Idle skipping: each game lists its wait
  loops (`Speedup`: PC range + a check on RAM at points with no decision
  pending); a CPU that would spin the whole slice skips it, a jump-to-self
  sleeps to its next interrupt. No Z80 core change. Checked against runs
  without it: the same picture and WSG state frame for frame (in Galaga's
  play a score digit can show a frame late now and then; R differs).
- Galaga: emulated CPUs, WSG, tiles / sprites / 05XX stars (the 256 stars
  of the LFSR period found once, placed per frame); 54XX explosions are our
  own noise + band-pass model (`emu/sound/namco54.*`). Speedups: sub / sound
  main loops, seven RAM-poll loops, and the main CPU's background loop
  (skipped once its inputs stay unchanged 6000 cycles). Cycles a frame
  152K -> ~31K attract, ~55K play.
- Dig Dug: same board; playfield from the map ROM, text layer, sprites
  (x wraps at 256, kept off the score lines), EAROM high scores (not kept
  over power-off). Speedups: the three main loops. ~22K attract, ~52K play.
- `core/`: plain C++ firmware logic, host-tested: `rom_store` (the user's
  zips as they are in the `roms` partition, append-only, directory sector)
  and `usb_import` (protocol of the web page's ROM loader, "CYDA ..."
  lines on UART0 at 115200). `components/arcade_core` builds emu/ + core/.
- Firmware (`main/`): `menu` (controller-driven; no touch yet), `game_runner`
  (sound ring paces the game; changed lines only; skips drawing when
  behind; back = hold Select + Start twice, or Home), `catalog` (which games
  the stored zips complete; ROM regions unpacked into RAM at game start),
  `sd_import` (new zips from /CYD-Classic-Arcade/ at boot, 32E boards),
  `serial_link`, `settings` (NVS: swap_ab, volume), `pad_test`.
- ROM page `web/roms/` (+ `loader.js`, tested in Node by
  `tools/host_tests/loader_test.mjs` against `fake_board`): reads zip
  directories in the browser, sends only the zips a game needs.
- Rendering is line by line into small DMA strips; no full frame buffer
  (a 224x288x2 frame is 129 KB - not with Bluetooth on).
- Sound: `main/audio_out.*`, games at 24 kHz; the 8-bit DAC runs at 4x
  (96 kHz) by DMA, each game sample joined to the next by a straight line,
  first-order noise shaping (v0.5.2, Tom: Donkey Kong "distorted and
  noisy", Pac-Man a little: the sound sat on ~2 DAC steps - in-band noise
  now ~24 dB lower). Volume linear, 100 % = the loudest game peaks
  (~21,000) just under full scale - never clips; default 50 %.
- After a game the menu's status line shows its measured speed, "Rally-X:
  60.6 fps, 28 shown/s" (game frames / pictures drawn a second, by the
  clock; v0.6.1, Tom: Rally-X "too fast" on the 2.8" ST7789 while the PC
  shows one game tick a frame at 60.6 Hz). "No sound: the DAC didn't
  start" if the DAC failed.
- ESP32-32E amp (BOARD_PIN_AUDIO_EN) off after ~0.2 s of silence, on
  with the first sound (v0.6.2, Tom: a faint low crackle from the 3.5"
  at idle) - as CYD Classic Games does.
- Backlight PWM at 44.1 kHz (v0.5.2): at 12 kHz it whined faintly in the
  2.8" ST7789's speaker (Tom; silent while flashing - backlight off). Machines fill signed 16-bit mono.
- Flash: 4 MB = app 2.25 MB + `roms` data partition 1.7 MB (ROMs read in
  place, memory-mapped). No OTA - the web flasher is the update path.
- `version_gen.h` is written by `tools/version.py` (ESP-IDF components
  don't share -D flags).

## Testing
- Host: `tools/host_tests/` (Z80 exerciser, 8080 tests 8080PRE / 8080EXM /
  TST8080 / CPUTEST, Klaus Dormann's 6502 functional test and the 6502
  decimal test (its source assembled at test time by `asm6502.py`, with the
  N / V / Z checks on), the SingleStepTests 8088 suite (the first 128 KB
  of each opcode's file, ~600 tests each, `fetch_i8088_tests.py` with
  SHA-256s in `i8088_tests.sha256`), unit tests, `namco_tests` for the
  Galaga-family parts, `atari_tests` for the 6502 boards)
  and `tools/host/` (`arcade_host`: runs a game headless with scripted
  inputs, writes PNG frames and a WAV; a game table in it, `--samples` for
  a MAME sample zip). Run them after any
  emulator change. Test programs that aren't ours (ZEX, the 8080 tests) are
  downloaded into `build/` with pinned SHA-256s, never committed.
- 6809 core (`emu/cpu/m6809.*`): no public 6809 test program was found,
  so `tools/host_tests/m6809_tests.cpp` checks it on its own: every 8-bit
  ALU result / flag against a plain signed / unsigned model for all
  operands, 16-bit ALU on random operands, DAA by decimal sums, every
  branch, every indexed mode (address, side effect, cycles), the
  datasheet's cycle counts, TFR / EXG, the stack, IRQ / FIRQ / NMI (armed
  by the first LDS), CWAI, SYNC, SWI 1-3 and small programs.
- 6800 / 6802 / 6808 core (`emu/cpu/m6800.*`, the Williams and Qix sound
  CPUs): the same kind of own tests in `m6800_tests.cpp` (ALU vs a plain
  model, shifts' V = N xor C, CPX's 6800 flags, cycles, IRQ / NMI / SWI /
  WAI). Undocumented opcodes run as 2-cycle no-ops.
- `williams_tests.cpp`: the 6821 PIA, the SC1 blitter on synthetic data,
  the sound board on a hand-made 6808 program, and the Robotron board
  running hand-assembled 6809 programs (line interrupts, video counter,
  ROM banking, CMOS, palette / video, sound command, watchdog), and the
  Defender board (banks, CMOS, its watchdog, the stick's reverse / thrust).
- Claude's container can't reach the PlatformIO registry, so firmware
  builds are checked by CI (GitHub Actions) on every push.
- CI pins PlatformIO 6.1.18: 6.2.0 brings SCons 4.11, which breaks
  pioarduino's ESP-IDF build ("No module named SCons.Tool.FortranCommon").

## Web flasher
- Every push to main: CI builds every env with `custom_firmware_name`,
  merges each into one image at 0x0 (esptool merge_bin) and publishes
  `web/index.html` + `tools/make_site.py`'s manifests on GitHub Pages:
  https://tomtombombadil.github.io/CYD-Classic-Arcade/

## Versions and releases
- Semantic versioning from `VERSION` (started 0.1.0, 2026-10-14). A fix
  bumps Z, a feature (a game, a setting, a screen) bumps Y. Bump VERSION in
  every push to main that changes the firmware.
- Don't publish a release unless Tom asks for one.
