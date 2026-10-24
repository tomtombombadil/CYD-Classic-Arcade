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
- Memory (Tom, 2026-10-20: "Stop shooting blindly in the dark. Do the
  math."): `docs/MEMORY.md` is the RAM budget - each game's exact needs
  (Xtensa sizeof + the runner's buffers), the always-running baseline and
  the optional parts, each number with its source. Measure before
  changing anything about memory; keep the file current. The board's
  numbers come from `main/memlog.*` (Log: "Mem <step>", "Free blocks",
  "Stacks used / size").
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
  for landscape games (Asteroids, Rally-X, Star Wars, Defender, Tempest). The
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
  too, v0.3.0: the card is read at start-up and its VSPI bus freed; since
  v0.13.0 the 2.8"'s touch controller takes VSPI after that - its own pins
  25 / 32 / 39 / 33, as in CYD Classic Games). Or over USB from the web
  page.
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
  decryption); for Missile Command it may set `indexed_io` (the (zp,X)
  instruction just fetched makes its data access through the callbacks,
  whatever the page table says) and a callback may `stall(n)` the
  instruction running. PC: ~800 emulated MHz (the loop and its memory helpers
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
- Touch menus (v0.13.0; Tom, 2026-10-16: "you can't do anything with it
  unless you have a controller paired", the game list clipped into the
  help text on some boards - "focus on user-friendliness and a clean
  attractive UI with touch based controls (as well as the controller)",
  plus an on-screen sign of which controllers are paired). `main/ui.*`
  (look, layout, widgets - LovyanGFX only), `main/menu_model.*` (the rows
  and texts), `main/menu.cpp` (driving them), `main/touch.*`. Every page:
  a header bar (< back, title, gear = Settings), content, a gold news line
  (game speed after a game, ROMs loaded / receiving), and at the very
  bottom the controllers - "1 8BitDo Ultimate" / "2 Xbox Wireless" with a
  teal pad icon, or "No controller: put one in pairing mode". Home lists
  the games as two-line rows (title; maker and year, from `games::Info`
  `maker` / `year`), playable ones first, games without ROMs muted ("No
  ROMs yet - tap for help" opens which zip it needs); a sideways-board
  icon marks landscape games; with no ROMs at all, "How to Add Games"
  comes first. Settings: Volume [-] [+], Swap A/B and X/Y and Rotate
  Screen 180 switches, Sound Output DAC / PWM (DAC boards), Controller
  Test, Forget Controllers, Calibrate Touch (resistive), How to Add Games,
  USB Transfer (v0.16.0), Last Game Speed, Log, About (version, board, games ready, ROM storage,
  game area, RAM / PSRAM); v0.15.0 added Demo Sounds (a switch, NVS
  demo_snd, default on - the cabinets' factory setting; Tom, 2026-10-17: leave a game in its attract mode
  "and get all those classic arcade sounds" - the cabinet's demo sounds
  switch where it has one, `games::Machine::set_demo_sounds()`) and Log.
  The game list is A to Z by title, playable games first (Tom,
  2026-10-17: the order games were added in "isn't suitable to a finished
  product"). Select (either controller) opens Settings from the game list
  and goes back (Tom: there was no way to Settings with a controller). No
  scrolling or dragging: rows per page come from the panel size (240x320:
  5 rows of 43 px, 320x480: 6 of 57) with < > page keys - nothing can
  clip. Fonts: FreeSansBold 9 / 12 pt titles, DejaVu 9 / 12 second lines,
  DejaVu 12 / 18 text pages. Touch: tap = act on release at the press
  point (two agreeing readings start a press, two empty ones end it - from
  CYD Classic Games); a touched row lights, the gold frame follows the
  finger. Controller: up / down move the gold frame (pages follow), A /
  Start choose, B back, left / right change the focused setting or the
  page, L / R page. Starting a game with no controller shows a note
  first; touch and hold the screen 1 s leaves any game (read every 4th
  frame). Resistive panels are calibrated once at the first start (tap
  the four corner arrows - LovyanGFX's calibrateTouch; BOOT held at power-
  on or Settings > Calibrate Touch redoes it), saved in NVS ("tcal" + a
  board-name hash). PC preview: `tools/menu_preview/build.sh <LovyanGFX
  dir>` draws every page at 240x320 and 320x480 with the real code onto
  LovyanGFX sprites (PNGs) - render it after any menu change.
- Start-up hidden (v0.14.0; Tom, 2026-10-16: show "Loading <game>"
  instead of "those horrible looking test patterns"): `games::Info::
  boot_frames` = frames from power-on to the attract mode, found frame by
  frame in PC runs with Tom's ROMs (pacman / mspacman 300 - the grid test
  ends there, rallyx 770, galaga 870, digdug 700, robotron 760, defender
  1625 - its Williams logo, qix 190, starwars 90,
  bzone 70 - black until its first picture at 67, galaxian 216 - after
  its cross-hatch test, amidar 193, gyruss 170 - after its grid test and
  its stars' fade, 1942 12, the rest 30 for the
  first black / garbage frame). The runner runs them before the game loop
  with nothing drawn and the sound thrown away, as fast as the board goes
  (bar, back check and a vTaskDelay(1) for the watchdog only every 32
  frames), behind `ui::loading()` (game title, gold progress bar). Tom:
  a landscape game's card is drawn in landscape (the panel turns first) -
  that is the sign to turn the board; no "turn the board" text. Back or a
  held touch cancels. The speed
  stats count only the game loop. A new game: find its attract mode's
  first frame the same way (arcade_host --png-every 10).
- Interrupted draws (v0.6.2, Tom: Galaga's start tune crackled at its end,
  where the whole screen changes at once): a draw stops at a line boundary
  when less than a frame of sound is queued, and the next draw goes on
  from that line (unsent lines keep their old hash). The sound never runs
  dry for drawing.
- Game start (v0.10.1; Tom: v0.10.0 said "can't load the ROMs" for every
  new game on every board): the ROMs go into RAM, then the machine is made
  (`new (std::nothrow)` - on the board a failed plain new aborts); while
  the machine, or then a 16 KB block for the runner's buffers, doesn't
  fit, the biggest ROM region still in RAM moves to the scratch flash
  (`flash_store::scratch_put`: the last 128 KB of the `roms` partition,
  kept out of the store - unless an old store already reaches into it -
  written only when its bytes differ) and is read in place, memory-mapped.
  A failed load names the missing ROM or the RAM shortfall on screen
  (`catalog::last_error()`); the menu shows free RAM and the largest
  block under its title. Machine + ROM RAM by game (PC sizes, about the
  board's): Robotron 108 KB, Q*bert 94, Mr. Do! 93, Dig Dug 90, Defender
  85, Burger Time 75, Ms. Pac-Man 72, Galaga 64, Tempest 56, Donkey Kong
  56, Rally-X 41, Pac-Man 41, Asteroids 36, Invaders 18. The USB loader's
  UART RX buffer is 4 KB (was 16; the page waits for an ACK every 2 KB).
  Screen strips are 8 lines (7.5 KB at 480 wide).
- Pairings kept, v0.14.2 (Tom, v0.14.0: a reboot drops the controller's
  pairing - woken, it did NOT come back; my claim that it would was
  wrong): Bluepad32 deletes a controller's link key whenever one L2CAP
  connection attempt fails (uni_bt_bredr.c, "so that the connection can
  start again"), so a controller paging the board while it starts or
  searches lost its pairing. `pad_bt.c` wraps BTstack's NVS key store
  (`hci_set_link_key_db`) and ignores those deletes; a key goes only when
  the controller refuses it (authentication complete with PIN or key
  missing / authentication failure - it was paired elsewhere since) or with
  Forget Controllers. About shows "Paired with this board: N" (BR/EDR keys;
  not on the BLE-only S3) so a kept pairing can be checked after a reset.
  Not confirmed on a board yet. If pads still don't come back: host-
  initiated paging of the stored keys at boot, and/or Tom's idea of
  asking whether to re-pair.
- Game start, v0.14.1 (Tom, v0.14.0 on the 4.0" and 3.2": Dig Dug, Mr. Do!
  - before its loading card -, Q*bert, Robotron, Defender, Qix, Star Wars
  reset the board; Ms. Pac-Man, Burger Time, Galaga ran; a reset also
  lost the paired controller): v0.10.1's order (every
  ROM into RAM, then the machine, moving regions to flash only when the
  machine didn't fit) left Bluetooth no RAM for a moment - all the
  failing games need more than ~80 KB. Now: the machine first, then
  `catalog::load(game, roms, kKeepFree = 40 KB)`: PSRAM first where there
  is any; internal RAM for a region only while 40 KB stays free (biggest
  regions go to flash first); the rest streams into the scratch flash ROM
  by ROM through `romset::Sink` (unpacked into one buffer the size of the
  biggest ROM, written at its offset; gaps zeroed after), so a big game's
  ROMs never all sit in RAM. The scratch area starts with a header sector
  ("CSC1": game, variant, the regions' places); the same game started
  again is checked byte for byte and nothing is written. `crash_note`
  keeps the game and step ("making its machine", "loading its ROMs",
  "starting its machine", "running its start-up", "playing") and the free
  RAM in RTC memory; after a crash / watchdog / brownout reset the menu
  shows "The board restarted: Dig Dug crashed while loading its ROMs (31
  KB free, block 20 KB)" and keeps it on the news line - ask Tom for that
  line when something resets.
- Speed on the board (v0.14.5; Tom, v0.14.3 on the 4.0": Galaga "much
  improved" but "57.3 fps, 49 shown/s", bogging down where enemies fire as
  they enter): Settings > Last Game Speed shows the last game's frames and
  pictures a second, the slowest whole second, and a frame's time split
  into emulation (average, worst after the first 2 s), picture (render +
  hash + send) and sending (with pixels a picture) - the same in the log
  line when a game ends. Ask Tom for this page when a game is slow. PC
  times don't carry over: the board was ~140x slower than the PC on
  Pac-Man's emulation (0.041 vs 5.7 ms), so PC numbers only compare
  versions. An ESP32 VM was considered (Tom, 2026-10-17): Espressif's QEMU
  translates Xtensa code and models no clock, flash cache or SPI timing,
  so it can't show where frames drop; the board's own page is the
  measurement.
- Two cores (v0.14.6, the next step after the speed page; Galaga's busy
  waves on the 4.0"): the tile games (Pac-Man, Ms. Pac-Man, Galaga, Dig
  Dug, Donkey Kong, Rally-X, Q*bert, Galaxian, Amidar, Dig Dug II, Gyruss, 1942) copy their
  VideoState after a frame
  (`Machine::snapshot()`, a `Shot<>` in games.cpp, ~2-5 KB allocated at the
  first one) and a "draw" task on core 0 (priority 1, as BTstack's loop;
  5 KB stack; 1 ms pause after each picture for the idle task's watchdog)
  renders, hashes and sends that copy while core 1 emulates the next
  frame. A snapshot is taken whenever the task is free, so slow drawing
  skips pictures, never frames; four frames of sound are queued. Since
  2026-10-24 also while core 1 waits for the sound before its next frame
  (the machine holds the last frame's picture until then; core 1 looks
  every 1 ms tick): before, a picture a little longer than a frame waited
  for the end of the next frame, so pictures came every 2 frames (1942 on
  Tom's S3: 22 ms pictures, 31 a second); a timing model (frames every
  16.8 ms, emulation 7.4 ms, +-15 % jitter) gives 22 ms -> 39 a second,
  18 ms -> 48 (no change below 14 ms). Same copy, same core, only taken
  sooner; nothing else changes. Their
  render_line(vs, ...) read only the copy and tables made at init
  (checked: every member they read is written only in init). Touch shares
  the display's bus on the ESP32-32E boards, so the game loop reads the
  held-touch exit only when the bus mutex is free. Asteroids and Tempest
  too, from a copy of the segment list (2026-10-17, see Vector games), and
  Star Wars where the game area has room for its 34 KB copy (the S3; on
  the ESP32 boards snapshot() fails and it draws between frames). Others
  (Williams 38 KB and Qix 64 KB video RAM, Mr. Do!, Burger Time, Invaders)
  still draw between frames on core 1. `arcade_host --panel`
  draws the tile games from a snapshot one or two frames old (as the other
  core would) and checks it against that frame's full picture: 0 wrong.
- Game area (v0.15.0; Tom, v0.14.6 on the 3.2" and 4.0": Qix crashed
  "while making its machine" (77 KB free, block 28 KB; once 5 KB), Robotron
  crashed (77 / 29 KB), a game that crashed said "Not enough memory" right
  after, yet another one then played - "Exiting a game should be similar
  almost to a reboot ... start each game from a unified ready state ...
  Memory management on these boards is critical"): `main/arena.*` - one
  block of internal RAM set aside at start-up once Bluetooth has started
  (`pad_bt_ready()`, waited up to 5 s), as big as leaves `kSystemReserve`
  (40 KB) to Bluetooth, the USB loader and the system. A game takes
  everything from it, in this order: the machine (games::Machine's own
  operator new -> `emu::alloc`, `emu/util/alloc.h`; the firmware points it
  at the area, the PC tools at malloc), the runner's buffers, the tile
  games' snapshot, then ROM regions while they fit (the rest into the
  scratch flash; PSRAM first on the S3); `arena::reset()` empties it when
  the game ends. v0.15.2 (Tom, v0.15.1 on the 3.2": after Donkey Kong,
  Galaga said "Machine 23 KB + buffers don't fit the 37 KB game area" - the
  area had come out at 37 KB at that start-up and stayed so): the area is
  given back and reserved again at every game start (`arena::renew`), from
  what is free then, keeping `kSystemReserve`; the game's start line logs
  the size and the size before. v0.15.3 (Tom, v0.15.2 on the 3.2": still a 32 KB area,
  Donkey Kong and Galaga "don't fit", Pac-Man plays): the ESP32's RAM is
  several regions and Bluetooth's blocks split the biggest, so one block
  can be far less than what's free (v0.14 games took pieces from all of
  them). The area is now up to 4 blocks (`arena::kMaxChunks`), biggest
  first, while `kSystemReserve` stays free; an allocation goes into the
  block with the least room that takes it; the loader's ROM budget is the
  room in all of them (a region that fits no block goes to flash). Log and
  messages give the blocks: "70 KB (48 + 14 + 8)", and a failed start the
  RAM free outside the area. v0.15.5 (Tom, v0.15.4 on the 3.5": "31 KB (31) game area;
  39 KB free outside" - every game but Pac-Man and Space Invaders "Not
  enough memory"; "these games were playing fine a couple revisions ago"):
  the area was capped at free RAM minus the 40 KB reserve (70 - 40), not
  by fragmentation. Before v0.15 a game took what it needed from the free
  RAM (Star Wars played on the 3.2" with ~15 KB left, Bluetooth fine), so
  `kSystemReserve` was 12 KB on the ESP32 (40 on the S3); since v0.16.2
  `system_reserve()` keeps 7 KB + 6 KB per free controller slot (a
  controller connecting takes 5.1 KB, measured) - never raise it without
  checking every game still starts on an ESP32 board (docs/MEMORY.md). The ESP32
  boards have ~70-77 KB free at a game's start (tasks: ui 12 KB, main /
  BTstack 8, usb 6 + 4 KB UART buffer, draw 5 - kept since v0.15.0 -,
  audio 3; static DRAM 105.6 KB at v0.3.1, 121 KB now). The PWM sound
  buffer (4 KB) is taken only when PWM is used. Machine + runner buffers
  (PC, 64-bit - a little over the board's): Invaders 25 KB, Pac-Man 32,
  Rally-X 34, Donkey Kong 37, Burger Time 37, Q*bert 39, Galaga 42,
  Asteroids 43, Tempest 47, Dig Dug 50, Mr. Do! 50, Ms. Pac-Man 52,
  Robotron 68, Defender 68, Star Wars 69, Qix 89. Allocation goes up from the bottom; freeing gives room
  back only for the newest block. The draw task (core 0) is made once at
  start-up, outside the area. The Log page borrows the area while it's
  open (no game then). About shows the area's size and the most a game
  used; the log line at each game start gives machine / area / ROM in RAM
  vs flash. Emulator code allocates only through emu::alloc (Qix's two 32
  KB video halves, Shot<>); nothing in emu/ may use new / malloc directly.
  v0.15.7-v0.15.8 (Tom's 3.5" Log, v0.15.7: the baseline in
  docs/MEMORY.md section 3 - an 81 KB area, every game but Qix's 88.2 KB
  fits): parts used only now and then hold no RAM between uses - the
  Controller Test's 14 KB strips borrow the area, the SD import's 4 KB
  buffer the heap at start-up, and the USB ROM loader (11.7 KB: task +
  UART driver) runs only while Settings > USB Transfer is open (v0.16.0,
  Tom, 2026-10-20: ROMs go onto a board about once - "let's not load it
  all the time. That's just silly."): the page gives the empty game area
  back (`game_runner::release_area()`), starts the loader, and on leaving
  stops it (`serial_link::stop()`; not while a file is coming in - the
  page stays with "Wait: ROMs are still coming in") and takes the area
  again - ~92 KB for games. The page is saved as open (NVS usb_xfer): a
  board reset by the port opening (CH340 on Windows) comes back to it with
  the loader running. The Load ROMs web page's steps and its no-answer
  message tell the user to open it. v0.16.1 (Tom, v0.16.0 on the 3.5":
  Star Wars "Can't load the ROMs - 136021.214.1f missing"): the ROM
  budget (`catalog::load`) keeps room in the area for the unpacking buffer
  (the biggest ROM going to flash), and a buffer that can't be had says
  "Out of memory", not a missing ROM. v0.16.2 (all 16 games measured on
  Tom's 3.5"): ESP32 task stacks cut to the most used + ~40 % (ui 12 -> 9
  KB, draw 5 -> 3, audio 3 -> 2; `memlog::kStack*`, the S3 keeps its
  sizes), and the reserve outside the area counts free controller slots
  (above).
- ROMs live in flash, not RAM (Tom, 2026-10-17, asked why zips are "in
  memory all the time"): the zips are copied from the SD card / USB into
  the `roms` flash partition (storage, read in place, no RAM); only the
  running game's chips are unpacked, into the game area or the scratch
  flash, and gone when it ends. They can't be emulated from the SD card:
  the CPUs read their program at random every few microseconds, which the
  ESP32 can do from its own (memory-mapped) flash but not from a card.
- Log (v0.15.0; Tom: crash messages went by too fast and the news line cut
  them off - "add a log button ... see game speeds, crash info, etc"):
  `main/devlog.*` over `core/log_ring.h` (host-tested) - 4 KB of lines in
  RTC memory, kept through crashes, watchdog and software resets (not
  power-off), oldest dropped first; each line "[boot seconds] text", also
  to serial. Settings > Log shows them newest first, paged with < > (also
  d-pad / L / R). Logged: each start (version, commit, board, reset
  reason), a crash's details, Bluetooth (started + pairings, connected,
  ready, disconnected, a pairing kept after a failed attempt or forgotten
  because the pad refused it, Forget Controllers), the SD import (copied /
  couldn't copy with the store's free room / skipped zips), games ready
  and which have no ROMs, the game area at start-up, each game's start
  (area, RAM outside it) and setup (machine, area used, ROM in RAM /
  flash, drawn on which core), its end (speed numbers as on Last Game
  Speed), a failed start's message. Crashes: ESP-IDF's panic handler is
  linked through `crash_note.cpp` (`-Wl,--wrap=esp_panic_handler`,
  main/CMakeLists.txt), which keeps the exception (or abort()'s / an
  assert's text, `g_panic_abort_details`), the task, the address and 8
  backtrace PCs in RTC memory; the next start logs them ("Crash: ...",
  "Backtrace: ...") - decode with the build's firmware.elf (CI artifact
  `elf-<env>`, 90 days). A crash's message and every failed-start message
  stay until a tap or a controller button (`touch::wait_tap_or_button()`);
  the news line then says "The board restarted: see Settings > Log". Ask
  Tom for Send Log (or the Log page, a photo) when something goes wrong.
- Send Log (v0.18.0; Tom, 2026-10-20: "we need a better way to transfer
  the logs" - as CYD Classic Games' Send Log): Settings > Send Log shows
  the newest log lines that fit (a binary search on the count), behind a
  header (board, version + commit, "Lines: newest N of M"), oldest first,
  as one QR code: byte segment `logpack::kUrl`
  (`https://tomtombombadil.github.io/CYD-Classic-Arcade/l/#`) + an
  alphanumeric segment of `core/log_pack.*` (from Classic Games, host-
  tested in core_tests against our own inflate: raw DEFLATE, fixed
  Huffman, 4 KB window, ~5x on logs, the 16 KB match tables passed in) as
  base43; ECC L, modules >= 2 px, a 2-module white margin, as large as the
  page allows (`ui::draw_qr` / `qr_room`; 5.4 KB of log text fit a 97 x
  97 code). Under it the line count and "Point your phone's camera at the
  code to get a link that sends the log to the developer." The encoder is
  Nayuki's qrcodegen as BTstack ships and builds it (`#include
  "qrcodegen.h"`, no copy of our own). All its buffers come from the game
  area (reset first, as the Log page; ~30 KB). `web/l/index.html` decodes
  it (DecompressionStream "deflate-raw") and offers Email (mailto
  **cyd.classic.games.logs@gmail.com** - Tom's address for logs, shared
  with Classic Games), Copy, Download log.txt. The menu preview renders the
  page with a made-up code of the largest size (`7e_send_log`).
- Download Log (v0.18.1; Tom, 2026-10-20: "if we can transfer files via
  the web-browser to serial/usb, let's use that and get all the log files
  in their natural form"): a button on the Load ROMs page (web/roms/)
  that works while Settings > USB Transfer is open. Protocol `CYDA LOG`
  -> `CYDA LOG <line>` ... `CYDA LOGEND <n>` (core/usb_import.*,
  `Session::set_log`); the firmware (serial_link.cpp `log_line`) sends 4
  lines about the board (name, version + commit, uptime, RAM, line count),
  a blank line, then the whole RTC log, oldest first. The page
  (`loader.js readLog`, sharing `waitHello` with `transfer`) saves it as
  `cyd-arcade-log-<board>-<date>.txt` (CRLF, for Notepad). The board has
  one log (4 KB in RTC memory): there are no other log files. Tested in
  core_tests and loader_test (fake_board answers LOG). Saving (Tom, v0.18.2: "doesn't work a second time without refreshing
  the whole page"): Chrome saves a page's first file by itself but blocks
  later downloads no click started (silently, an icon in the address bar)
  until a reload - so only the first log is saved at once; after that the
  page shows "Read <file>: click Save Log File" and a Save Log File button
  (a click always may download). Checked in headless Chromium with a fake
  Web Serial board: three reads in a row, then the button.
- Log lines (v0.18.2; Tom's 4.0" log: the end-of-game speed line stopped
  at "picture 5.0 (worst 14.9"): `devlog::kLineMax` 256 (was 112); the
  ring keeps lines at their own length, the readers (Log, Send Log, LOG)
  size their buffers from it.
- Port opening (v0.18.2; Tom: the 4.0" S3 restarted at every Download
  Log - "is there a way NOT to reboot every time serial connects?"): RTS
  on with DTR off resets the board (CH340 auto-reset transistors and the
  S3's USB-Serial-JTAG alike), and the page's one setSignals({DTR off,
  RTS off}) passed through that pair (Chrome clears DTR first). Now RTS
  is cleared first, then DTR. Unconfirmed on a board; if a reset remains
  it comes from the port opening itself (the driver), which the page
  can't control - the board then reopens USB Transfer by itself (NVS
  usb_xfer) and the log survives the reset.
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
- Space Invaders' march and invader hit (v0.17.1; Tom, 2026-10-20: no
  "thrum-thrum-thrum" heard as the fleet moves, and the shot "confused
  with the hit-the-alien sound"): the march notes (~58-76 Hz 555s) were
  filtered at 339 + 160 Hz - a small speaker plays nothing that low, so
  they were all but silent. Now both filters 339 Hz (the harmonics carry
  the note) and level 20,000: through a 400 Hz high-pass (a small
  speaker's) a note is ~1/5 as loud as the shot, as in the board's
  recording (v0.17.0: 1/16). The invader hit warbled 1-2.5 kHz, right over
  the shot's 1840 -> 580 Hz sweeps; the recording is a steady ~2.6 kHz
  tone falling ~900 Hz a second, full 160 ms, then tau 0.1 s - now that
  (square through a 4 kHz low-pass, 3 % jitter). A soft knee above 16,000
  (play peaked 26,400 with a note under the base explosion; now ~21,600).
  The colour overlay (since v0.1, MAME's bands for the Midway upright:
  red rows 32-61 where the UFO flies, green 184-239 over the shields and
  cannon, green under the reserve cannons) is drawn on the board too.
- Space Invaders' shot (Tom, 2026-10-20: "isn't right"; not the ROM - the
  board's sounds are analog circuits the program only switches): fitted to
  a recording of the board (classicgaming.cc's 1999 shoot.wav, 11 kHz -
  used on the PC for comparison only, never in the repo or firmware): a
  pulse a third of each cycle high, pitch falling in a straight line
  1840 -> ~580 Hz in 143 ms and starting again every 153 ms, faded out
  over 0.41 s (full for 60 ms, then 1 - x^1.2), 8 % pitch jitter from the
  noise; the bit's 3 ms / 56 ms envelope stays (the game holds the bit ~61
  frames = ~1 s while the shot flies). v0.1's model was one slow 50 %
  square slide 1.8 kHz -> 300 Hz. Two boards existed: Midway's (US) and
  Taito's (Japan) shot circuits differ (C12 1 uF vs 0.22 uF, R13 3.3M vs
  2.2M); which one the recording is from isn't known. The recording seems
  to start ~62 ms into the first sweep; the model starts at its top.
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
  list of clipped segments in output pixels (1/16 px fixed point; 14-byte
  segments + 2 bytes in the sort list + 4 bytes an active-list entry
  (number << 16 | last row); capacity and active-list size are template
  parameters - Asteroids / Tempest 1024, 20 KB), sorted by first row, and
  draws one output line at a time (1-pixel lines, dots 2 x 2, later lines
  on top) - no frame buffer. The machine builds a whole picture inside one
  run_frame (begin / add / finish). Drawing (2026-10-17, exact): a row's
  new segments merge into the active list from the back in one pass (it
  was an insertion each), ended ones go by their stored last row, x by a
  32-bit multiply (slope x t <= |dx| x 65536 fits), shifts for / 16 (the
  clipper clamps to the screen, so nothing is negative). `Copy` = a copy
  of the last picture, 20 bytes a segment, taken once at the first take()
  for the display's whole capacity from emu::alloc (the game area; a copy
  grown in steps would leave dead blocks there) - take() false if it can't
  be had - and drawn the same way: Asteroids and Tempest draw on core 0
  from it (`snapshot()`, 20 KB each; max seen 498 / 696 segments), Star
  Wars where its 34 KB fits; `--panel` 0 wrong.
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
  buttons and switches, read directly as old MAME did; fire, Superzapper
  and the starts read HIGH when pressed - v0.13.x read them low and Tom's
  ship fired on its own: the game debounces that byte and takes 1 = held,
  a 0 -> 1 change = a press), RANDOM (Tempest
  checks that a read 4 cycles later shows the high nibble in the low
  one), EAROM in RAM. The AVG loops on its list on the real board; here
  the list is drawn from its start to its jump back to word 0 every other
  frame (the game makes ~27 pictures a second). Landscape (Tom,
  2026-10-17: the portrait 240 x 320 stretch of the 580 x 570 window
  looked tall - "make it landscape, it was 4:3"): 320 x 240, the window
  at its true shape (square units, a round web), 570 units over the 240
  rows, 244 px wide in the middle, black sides; lines cut at the window's
  sides. On 320x480 panels the runner shows it 427 x 320. Controls: A =
  fire (held = autofire, as the cabinet's button), B = Superzapper, d-pad
  / left stick = the spinner, right = counter-clockwise on the screen
  (Tom, v0.14.4: the player sits at the bottom, where that is to the
  right; v0.14.4 had it the other way), Select = coin, Start = 1P, L = 2P. Spinner:
  clockwise on the screen counts the knob down; d-pad 1.5 counts a frame
  (a lane every ~5 frames), the stick up to 4 a frame in proportion
  (`set_spinner()`; the game reads the 4-bit count's change, so faster
  would alias). Idle skipping: the main loop's wait for the IRQ count
  (LDA zp; CMP #n; BCC, found by its code) jumps to the next IRQ. PC ~0.06
  ms a frame emulated (the game keeps its CPU busy: ~6800 instructions a
  frame), ~0.025 ms rendered. Sound: the two POKEYs x 4, AC-coupled; game
  peaks ~20,000.
- Battlezone (`emu/machines/bzone.*`, set `bzone` (Rev 2) or `bzonea`
  (Rev 1, Tom's; older MAME's `bzone`, files `036414.01` ...) as romset
  variants - only the first program chip differs; no AVG / math box
  PROMs; landscape 320 x 221). Emulated: 6502 at 1.512 MHz, NMI every
  6144 cycles (off while the self-test switch is on), A15 not decoded,
  IN0 (coins, slam, self-test, 3 kHz clock, AVG halted), the switches
  (`set_dips` = M10, the game options, default as the manual: 3 tanks,
  missile at 10,000, bonus 15,000 and 100,000, English; P10 pricing fixed
  at 1 coin 1 credit), the math box (Tempest's), the POKEY (sound;
  ALLPOT = the treads, fire and the starts, high when pressed), and the
  AVG's Battlezone variant (`atari_avg`, Variant::Battlezone): STAT sets
  only the intensity, and its clip window - a STAT with bit 10 clear
  latches the beam's position as a corner (bit 9 set: top right, clear:
  bottom left), lines after it are cut to the window, which is off again
  at each picture's start (the game cuts its 3-D world off below the radar
  and scores this way; with no clipping, "ENEMY IN RANGE" showed in the
  attract mode). The NMI handler starts the AVG every 6th NMI (41
  pictures a second) on one of two list buffers (the list's first word
  jumps to it); each picture is drawn whole at its AVG GO. The picture:
  the 580 x 400 window at square units (the radar round), 580 units over
  320 columns = 221 rows; the cabinet's overlay (MAME's artwork: red over
  the top fifth, green below, 1 / 0.125 tints) - rows 0-43 red, the rest
  green, lines cut in two at the edge; intensity as Tempest's (a floor so
  dim lines show). Drawn on core 0 from a copy (`snapshot()`, 1024
  segments, 20 KB; most seen 525 in the attract mode's pages, 449 in
  play). Modelled (`emu/sound/bzone_sound.*`, part values from the
  board's parts list and Al Kossow's notes on the 1840 latch: 0
  explosion, 1 explosion loud, 2 shell, 3 shell loud, 4 engine rev, 5
  sound on - every sound, the POKEY's too -, 6 start lamp, 7 engine on):
  a 16-bit noise register at 6 kHz (12 kHz halved; XNOR of bits 3 and 14
  in); shell / explosion = a 4.7 / 10 uF cap charged through a CD4066
  while its bit is on and discharged through 23k while its noise bit is
  high (shell: the output halved; explosion: the NAND of bits 11-14
  halved - slower, rumbling), the noise chopping its charge into an
  op-amp (x 33 loud, x 8.7 soft, 330k / 4.7 nF = 103 Hz); engine = the
  rev bit's control voltage (4.12 / 2.5 V from R5-R7) through 100k / 10
  uF (1 s revving up, 0.18 s down through the diode) into a 555 (100k,
  R11, 15 nF: ~355 Hz idle, ~800 Hz revved), two counters dividing it by
  12 and 10 (outputs QD and ripple), summed through 33k each onto 0.47 uF
  (the "integrated square wave"); the POKEY through 10k / 0.015 uF; the
  mixer's resistors; the output's 0.1 uF / 22k coupling (72 Hz). Levels
  set for a small speaker as Asteroids': explosion ~17,000, shell
  ~14,000, engine ~7,000 (constant while playing), POKEY ~8,000, a soft
  knee above 16,000 (play peaks ~21,000). Assumed: R11 (the engine pitch
  pot, 250k on the board) at 36k, MAME's default setting; the shell's
  loud bit as Kossow's notes say (D3; MAME wires the explosion's); the
  watchdog (131072 cycles, as Asteroids). Unsure: the engine and shell
  levels against each other (the board's mixer would make the explosion
  ~3x the others). Controls (Tom's rule, by position): Select = coin,
  Start = 1P, A, B, L and R = fire (v0.17.1, Tom: thumbs on both sticks reach only the shoulder buttons; a one-player game, so no 2P start); the d-pad drives: up = both treads
  forward, down = both back, left = left back + right forward (turn left
  on the spot), right the mirror; a diagonal adds both and a tread pressed
  both ways stops (`Machine::in3`), so up + left = the right tread alone
  (forward, turning left), down + left = the left tread back; or the
  sticks, tank style (`bzone::treads`): once either stick's Y is past a
  third, left Y = left tread, right Y = right tread (up = forward), and
  the d-pad's tread bits (which the left stick sets too) are dropped.
  Idle skipping, exact (checked frame by frame - picture, audio, PC, A -
  over 20,000 played frames with and without it; `bzone_tests` too): the
  main loop's waits LSR zp / BCC (for the NMI handler's count, every 16th
  NMI) and LDX zp / BEQ (for the buffer swap), whole turns to the next
  NMI - ~25 % of the cycles (the game keeps its CPU busy). Attract mode
  silent (see Demo sounds). PC, in play: emu ~0.05 ms, render ~0.011 ms
  a frame (Pac-Man 0.035 / 0.070, Tempest 0.059 / 0.021 in the same run).
  Memory (docs/MEMORY.md): machine 27.7 KB, ROM 16 KB in 2 regions.
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
- Bubbles: removed in v0.17.1 (Tom, 2026-10-20: "not the game I thought it
  was. That game stinks."). It ran on Robotron's board with an 8-bit CMOS
  RAM and the starts swapped in IN0 (git history has it). Don't add it back.
- Freenove ESP32-S3 Display (Tom bought an FNK0104, 2026-10-16; v0.11.0):
  envs `fnk28_ili9341_cap` (FNK0104B, 2.8" ILI9341) and `fnk40_st7796_cap`
  (FNK0104S, 4.0" ST7796), `custom_chip = esp32s3` (CI's merge puts the
  S3 bootloader at 0x0; the flasher's chipFamily "ESP32-S3"),
  `main/boards/freenove_s3_display.hpp`. Pins from Freenove's schematics
  (their repo is CC BY-NC-SA: facts only). Display SPI2 (IOMUX pins),
  inversion on; touch FT6336 through LovyanGFX's Touch_FT5x06 (I2C 0x38,
  panel portrait coordinates, polled). Sound: I2S into the ES8311 codec
  (`BOARD_AUDIO_I2S`; our own register setup, written through LovyanGFX's
  I2C - the codec shares the touch's bus, one driver owns it), no Sound
  Output menu item there. SD by 4-bit SDMMC (`BOARD_SD_SDMMC`). Native USB only: the
  ROM loader uses USB-Serial-JTAG (`BOARD_USB_SERIAL_JTAG`), the console
  stays on UART0 and the log is copied to USB (dropped when nobody reads).
  8 MB octal PSRAM (`sdkconfig.defaults.esp32s3`): ROM regions go there
  first, machines (< 128 KB) internal RAM. BLE controllers only on the S3.
  Tom owns the 4.0" (FNK0104S, tested since v0.14); he has no 2.8"
  (FNK0104B), so `fnk28_ili9341_cap` stays `custom_board_tested = no`.
- Qix (`emu/machines/qix.*`, set `qix` = Rev 2; portrait 240x256 =
  the CRTC's lines 8-247 shown as columns (MAME's ROT270; everything the
  game draws lies in lines 16-240, and 256 scaled down to 240 would drop
  the one-pixel lines), 55.84 Hz). Emulated: the data and video 6809s at
  1.25 MHz with the shared RAM and the FIRQs between them (8C00 raise the
  other's, 8C01 clear own), the 6845's timing (82 cycles x 273 lines;
  VSYNC lines 256-271 on the sound PIA's CB1 = the data CPU's IRQ), the
  scanline register (the shown line, 255 in the blank), the video RAM in
  two 32 KB heap blocks (nothrow in the constructor; `make_qix` returns
  null if either fails) seen through the 0000-7FFF window (bit 7 of the
  latch) and the 9400 latched byte, palette RAM with its bank, the CMOS,
  the PIAs (inputs active low; A = Fast Draw = button 1, B = Slow Draw),
  and the sound board (`emu/sound/qix_sound.*`: 6802 at 921.6 kHz + PIA +
  DAC running U27 - exact; the volume attenuators (data PIA port B, 4
  bits a side, 22k / 10k / 5.6k / 3.3k against 10k) averaged to mono, the
  1.5k / 1 uF output high-pass; play peaks ~15,000-21,000). Our own
  guess: the RRGGBBII levels (intensity scales the colour 50-100 % and
  adds a little grey). Not drawn: flip screen. First boot: the CMOS is
  empty, the game writes its defaults and waits on the operator's setup
  pages (Advance x5); the machine restarts the board 30 frames after the
  CMOS checks valid (55AA + checksum), as a power cycle would, and the
  game comes up. The CPUs run in step (32-cycle slices, the one behind
  runs; a FIRQ ahead of the other's time waits for it); the game is
  sensitive to this - other slices give another attract-mode Qix. Idle
  loops sleep (pure loops, scanline waits until the line changes, and
  the data CPU's counting wait "INC <25 ; LDA 8520 ; BNE" whose count is
  added on waking) and wake on the other's shared RAM writes (trapped
  while one sleeps; a counting wait only for its two bytes), FIRQs, IRQ,
  VSYNC. The video CPU never sleeps in play: its task loop polls the
  scanline. PC: emu ~1.4x Pac-Man's host instructions a frame (~650K by
  callgrind; wall time ~0.09 ms vs Pac-Man's 0.05 in the same run),
  render ~0.05 ms. RAM (32-bit): machine ~8.5 KB + video RAM 2 x 32 KB +
  ROM regions 16 + 14 + 2 KB = ~104 KB; the biggest single block 32 KB.
- 6800 core fix (2026-10-16, found with Qix): an IRQ waits one more
  instruction after CLI (with I set) or TAP, as on the chip - Qix's sound
  handler "LDS ; CLI ; LDAA <PIA>" re-entered itself forever without it;
  a branch to itself doesn't idle past an IRQ already due.
- Star Wars (`emu/machines/starwars.*`, set `starwars` (set 1); math box
  PROMs as today's dumps or old MAME's (14 bytes with stray high nibbles -
  only the low 4 bits count) as romset variants; no AVG PROM; landscape
  320x240, 61.5 Hz). Emulated: main 6809E at 1.512 MHz (IRQ every 6144
  cycles, acked by 4660; ROM bank at 6000 = LS259 bit 4; watchdog 65536
  cycles), the AVG's Star Wars variant (`atari_avg`, Variant::StarWars:
  big-endian words, STAT = colour bits 8-10 + 8-bit intensity, vector
  brightness = intensity x the 12k/24k/47k DAC of z), run at each AVG GO
  (the game waits for VG_HALT, so its drawing time is kept exact); the
  250 x 280 window stretched over 320 x 240 as the 4:3 tube does. The
  matrix processor (`starwars_mathbox.*`) runs its microcode from the
  user's PROMs (the four nibbles put together once at init: 2 KB), with its
  timing (5 clocks a step, +33 a multiply) on MATH_RUN; the divider; the 23-bit LFSR (3 MHz,
  jumped ahead in closed form at each read). X2212 NOVRAM in RAM only
  (store / recall). Sound board: 6809E, 6532 RIOT (`riot6532.*`: timer IRQ,
  PA7 edge IRQ on a command), latches both ways, 4 POKEYs, and our own
  TMS5220 (`emu/sound/tms5220.*`: Speak External FIFO, status, /RS /WS
  /READY timing, the chip's tables, interpolation, chirp / LFSR
  excitation, 10-stage lattice, 8-bit clipped output at 8.4 kHz). Mix:
  POKEYs x 0.85 AC-coupled, speech x 0.44 through a 4 kHz low-pass (our
  addition against the 8.4 kHz images); peaks ~21,000. Controls: the yoke
  = left stick (yaw / pitch, up = sights up) plus where the d-pad has
  moved the sights (4 ADC counts a frame - v0.15.4, Tom: 2 was too slow; stays put); A / B / X / Y fire
  (the four yoke buttons), Select = coin, Start = fire (the game has no
  start button: a trigger starts it). The game calibrates its yoke itself
  (minimum + gain per axis, learnt in play, kept in the NOVRAM); a new
  NOVRAM starts at minimum 0x40, gain 1.25, so the pots read 0x40 / 0xA6
  (centred) / 0xFF - the sights centre and reach the edges at once. Idle
  skipping, exact (checked frame by frame against runs without it over
  8000 frames incl. play): the main frame wait (6005), a flag wait (6032),
  the matrix processor wait (CDBD, the CPU stops after each start), the
  sound CPU's timer wait (7D4C); about half the cycles. The attract mode
  is silent but for one random tune / speech about every 7 minutes (see
  Demo sounds; the first ~7 minutes are silent). Display
  cap 1700 segments (the flight instructions page needs ~1660, high scores
  ~1070, play up to ~900 - 1,084 the most in a 12,000-frame played run),
  at most 256 crossing a row (173 seen). Memory: machine object ~54 KB
  (20 KB RAM, 28.3 KB display, 2 KB microprogram) + 72 KB of ROM regions
  (9). Unsure: the speech filter, the TMS5220's start delay (no "fast
  start").
- Demo sounds (Tom, 2026-10-17: an option for sound in the attract mode,
  "so you could leave it in demo mode and get all those classic arcade
  sounds"): `games::Machine::set_demo_sounds(bool)`, after init(), before
  the first frame; it returns false where the cabinet has no such switch.
  Only the cabinet's own switch is set - no sounds the real game doesn't
  make. Not called = the game's factory setting (on for all five that
  have one). Switches (facts from MAME's DIP lists, the games' operator
  pages and their code): Galaga DSWA bit 3 (SWB:4; the demo play's shots,
  explosions, tunes); Dig Dug DSWB bit 4 (SWB:4); Q*bert DSW bit 0 (the
  demo's hops, speech, music); Qix the operator's ATTRACT SOUND, CMOS
  87E9 (BCD 01 / 00) - written with the checksum (8700 = sum of
  8701-87FF, the game's f56d) at the first boot's restart; with it off
  the attract play itself differs (the CPUs' timing changes the game's
  random draws); Star Wars 10D switch 7 (DSW0 bit 6): the game copies the
  switches to the NOVRAM (4590-4593) at power-on, and with sound on the
  attract's logo starts one of nine tunes / speeches picked at random
  (table at 6759, gate NOVRAM 4592 bit 2) - only when the hundreds of a
  BCD time count (4B04-4B07, a step every 1000 IRQs = 4.1 s) changed
  since the last time: about every 7 minutes, never the first time round
  (the old note "no sound command whatever the switch" came from runs
  under 7 minutes); Amidar IN2 bit 1 (clear = on; the demo play's tunes
  and sounds, ~50 s into the attract cycle); Gyruss DSW2 bit 7 (Demo
  Sounds) and DSW3 bit 0 (Demo Music), both set by set_demo_sounds (0 =
  on; the attract's play sounds and tunes - 6000 frames: peak 18,600 vs
  0). No switch, attract silent on the cabinet: Galaxian, Pac-Man,
  Ms. Pac-Man, Space Invaders, Rally-X, Donkey Kong (and Jr.), Mr. Do!, Burger Time,
  Asteroids, Tempest, Dig Dug II, 1942, Missile Command (MAME's DIP lists have none for these
  sets; Missile Command's attract measured silent, peak 0 over 10,000
  frames),
  Battlezone (no switch either; its program sets the sound-on bit of the
  1840 latch only during play - checked: 20,000 frames of attract mode
  wrote only the start lamp),
  Zaxxon (its SW1 "Sound" switch silences all sound; the attract mode is
  silent either way - the game's own code),
  Robotron (its Game Adjustment page: no sound item - "Fancy Attract
  Mode" is pictures), Defender (28 audit / adjust items, none about sound;
  Game Adjust 1-10 are unnamed on screen). Not attract sound: Q*bert's
  power-on speech and Burger Time's power-on sound (~2 s in) come after
  their 30 boot frames, either way. Measured on the PC (`arcade_host
  --demo-sounds on|off`, 4000 frames after start-up, peak / RMS): Galaga
  16,335 / 1,085 vs 0; Dig Dug 17,816 / 1,085 vs 0; Q*bert 18,827 / 2,188
  vs power-on only; Qix 12,071 / 1,342 vs 0; Star Wars over 16 minutes
  9,975 / 274 (24 s of sound) vs 0; Amidar (6000 frames) 20,343 / 679 vs
  0; Galaxian silent after its power-on blip (in the boot frames). Play after a coin sounds the same
  either way.
- Star Wars speed (2026-10-17; Tom, v0.14.x: 47.4 frames and 32 pictures a
  second on the 3.2", "completely unplayable", the S3 too). Measured with a
  played run (`--input`: coin, start, yoke moves, fire; 9,000-12,000
  frames). Before: 1.04M host instructions a frame emulated (callgrind,
  6,000 frames attract + play; Pac-Man 483K) - the two 6809s ~505K (main
  ~3,600 instructions a frame of real work spread over many routines, sound
  ~1,700, half of it its timer wait), the AVG + picture build ~185K a frame
  (~280K a picture, 0.66 pictures a frame), POKEYs ~107K, matrix processor
  ~100K, TMS5220 ~37K, speech / DC filters ~32K, sound glue ~55K - and 0.32M
  rendered. Changes, all exact (`arcade_host --hash`: every frame's picture
  and audio CRC as before over 12,000 played frames and 10,000 of attract;
  Robotron, Defender, Qix (6809 / 6800 core), Asteroids, Tempest (display,
  AVG, POKEY) 5,000-6,000 frames each, the same): the sound board is no
  longer run after every main CPU slice, only where the boards meet (4400 /
  4401 / 46E0) and at the frame's end (same instructions, the same first
  boundary past each target - see run_frame); the sound CPU's wait at 7D4C
  is done by `sound_idle` instruction by instruction without the CPU
  (anywhere in the loop, partial turns too, stopping before a branch that
  leaves it) and its LDA <$00 ends the CPU's run, so the wait never runs
  to a slice's end (sound CPU 1,714 -> 885 instructions a frame;
  `starwars_tests` checks the loop against the CPU, with and without a
  timeout); 6809: the A / B / D / X / U groups (0x80-0xFF) as one template
  per opcode (`group<op>`, mode and operation known at compile time) and
  one test of the interrupt / wait state per instruction - ~13 % (x86 -O2
  object 35 -> 61 KB; force-inlining ea_indexed / fetch16 / the ALU helpers
  or a one-lookup rd16 gained nothing and were dropped); AVG
  (`run_lines<Variant, Sink>`): the machine's line code inlined into the
  loop, its state in locals, a STAT's 8 brightnesses worked out once (not a
  division a vector), the normalisation by count-redundant-sign-bits (NSA
  on the ESP32; equal for all 8192 values); the display's on-screen check
  and store inline; matrix processor: the decoded table, the strobe
  combinations the microprogram uses as straight code, registers in
  locals, the multiply in 32 bits (its low 32 bits are all that's kept);
  POKEY: its level and next underflow kept (`refresh()` after writes), so a
  sample without an underflow is no search (also Tempest's); TMS5220: the
  20 noise LFSR steps a sample as two table lookups (linear over GF(2),
  576 bytes of flash); the speech low-pass and the DC blocker skipped while
  silent and settled. After: ~0.73M emulated a frame (6809s ~375K, AVG +
  build ~130K, matrix processor ~60K, POKEYs ~55K, TMS5220 ~27K), 0.22M
  rendered. PC wall time in play, best of 3 runs per frame (ms): emu avg
  0.127 -> 0.086, p99 0.225 -> 0.162, worst 0.290 -> 0.212; render avg
  0.043 -> 0.031, p99 0.093 -> 0.065, worst 0.121 -> 0.089. Left: the main
  6809's real work (~85 host instructions a 6809 instruction), the picture
  build when the AVG runs, and drawing on core 1 between frames. Drawing on
  core 0 (`snapshot()`, `vector_display::Copy`) works (`--panel 320x480`:
  4,331 pictures, 0 wrong) and on (`kDrawFromSnapshot`) where it fits: the copy is 20
  bytes a segment, taken once at the display's capacity from the game area
  (emu::alloc - a copy grown in steps would leave dead blocks there): 34 KB
  for Star Wars' 1,700, ~20 KB for Asteroids' / Tempest's 1,024. With
  Star Wars' 55 KB machine + 18 KB of buffers it doesn't fit an ESP32's
  ~100 KB game area: snapshot() fails there and the runner draws between
  frames (as before); the S3's bigger area takes it.
- Missile Command (`emu/machines/missile.*`, set `missile` (rev 3), or
  `missile2` (rev 2, Tom's: his zip's `035820.02` ... `035825.02` are
  today's `missile2`, CRCs 7a62ce6a df3bd57f a1cd384a 82e552bb 606e42e0
  f752eaeb) or `missile1` (rev 1) as romset variants - CRCs from MAME's
  driver; the write-enable PROM 035826-01 isn't needed; landscape 256x231,
  61.04 Hz). Emulated: the 6502 at 1.25 MHz (10 MHz / 8), half speed on
  lines 224-255 (time is kept in 1.25 MHz ticks: 80 a line, 20,480 a
  frame; `Info::cpu_clock` / `cycles_per_frame` are ticks); 16 KB of RAM
  (A15 ignored) that is the picture: line y at 64 x y, 4 pixels a byte
  (pixel x & 3: low nibble bit = colour bit 1, high nibble bit = colour
  bit 2), lines 25-255 shown; the third colour bit of lines 224-255 at
  0200-05FF (MAME's get_bit3_addr: bit X & 7 of every other byte, Y bit 3
  picks 0200 / 0400, Y bit 4 the odd bytes); MADSEL - an opcode with low
  bits 00001 (the (zp,X) instructions) fetched while /IRQH is inactive
  sends its data access to the pixel logic (address = Y << 8 | X, data
  bits 7 / 6 / 5 = colour bits 2 / 1 / 0; a read gives them back in those
  bits, the rest 1), an extra clock for the third bit (via the 6502 core's
  `indexed_io` / `stall()`); the IRQ = the 32V flip-flop (set at lines 0,
  64, 128, 192, cleared at 32, 96, 160, 224 or by a write to 4D00),
  latched into /IRQ at each opcode fetch, the latch before it = /IRQH;
  VBLANK (4900 bit 7, lines 0-23); the 8-entry colour RAM (bits 3 / 2 / 1
  low = red / green / blue); one POKEY at 1.25 MHz, ALLPOT = the game
  options (R8: MAME's defaults 0x73 - 6 cities, mini trackball, a bonus
  city every 10,000, upright); set_dips = the pricing switches (R10,
  default 0x80: 1 coin 1 credit, English); the trackball's two 4-bit
  counters (4800 with CTRLD = 1: Y in bits 4-7, X in 0-3); the watchdog
  (8 frames). Assumed: the write-enable PROM enables exactly the bits the
  read path reads (the PROM isn't in Tom's set; MAME uses it as data);
  the sound is the POKEY's four channels summed x 5, AC-coupled (15 Hz) -
  play peaks 19,100-20,400 over three 98 s played runs (the game uses
  volumes up to 8); MAME's POKEY output RC isn't modelled. Not drawn: the
  cocktail flip (and its other IRQ lines). Controls: the left stick rolls
  the trackball at a speed in proportion (full = 8 counts = 8 pixels a
  frame), the d-pad 1 a frame when pressed, rising in a straight line to
  9 after half a second held the same way (v0.17.1, Tom: the steady 3 of
  v0.17.0 "doesn't work well with a dpad"; a tap now moves one pixel, a
  hold crosses the screen in ~1 s) (a count moves the crosshair a
  pixel with the mini trackball setting; the game reads the counters at
  each of the 4 IRQs a frame and takes the 4-bit change, so reads see the
  frame's speed spread over the frame - up to ~28 counts a frame would
  read right); X / A / B = the left / centre / right base (by position),
  Select = coin, Start = 1P, L = 2P. The game polls Start about every 8
  frames (a press shorter than that can be missed, as on the cabinet).
  boot_frames 48 (a white frame, black while it clears the RAM, the title
  at frame 48). Idle skipping, exact (`arcade_host --hash`: every frame's
  picture and sound the same over 10,000 attract and 3 x 8,000 played
  frames; `missile_tests` too): the main loop's LSR $9F / BCC wait (500F)
  skips all but its last pass to the stretch's end, the IRQ handler's BIT
  4900 / BMI wait for VBLANK's end (7C70) the passes that still read
  VBLANK; found by their code; ~5,300 -> ~2,800 instructions a frame. PC
  in play (best of 3, 6,000 frames): emu 0.045 ms, render 0.045 ms (Pac-
  Man 0.037 / 0.068 in the same run). Drawn on core 0 from a snapshot (the
  picture's RAM 0200-3FFF + pens, 15.5 KB); `--panel` 240x320 / 320x480: 0
  wrong. Memory (Xtensa): machine 18.3 KB, + buffers 14.4 / 16.0 KB, ROM
  12 KB (six 2 KB chips).
- Zaxxon (`emu/machines/zaxxon.*`, set `zaxxon` = set 1 rev D - Tom's
  zip has the old names `zaxxon.1` ... `zaxxon.15`, `zaxxon.u72` / `.u98`;
  `zaxxon2` (set 2) as a romset variant; portrait 224x256 = the board's
  256 x 224 (lines 16-239) turned like Pac-Man's (MAME's ROT90), 60.0 Hz).
  Emulated: Z80 at 3.041 MHz (48.66 MHz / 16, 264 lines of 192 cycles),
  the VBLANK IRQ (IM 1) raised when U56 bit 0 is set and held until the
  game writes that bit 0 (its handler does), the coin latches (set by a
  coin only while U55's enable bit is on, cleared by writing it 0), the
  inputs (active high), the service switch's NMI, no watchdog; the
  background: a 256 x 4096 picture of 8 x 8 tiles (32 x 512, codes and
  colours from the 32 KB tile map ROMs, 3 bits a pixel) at row y - 2 x the
  11-bit position, column x - y / 2 + 63 (the skew that makes the
  isometric view), its palette half (U56 bit 6) and switch (bit 7, else
  black); 32 sprites of 32 x 32 x 3 bits (code & 63, flips in bits 6-7,
  colour, top line 0xEF - Y, left column X - 16, wrapping at 256; sprite 0
  on top); the characters (32 x 32, 2 bits, colour from the second PROM at
  column + 32 x (row / 4), their palette half U56 bit 1) on top; the
  palette PROM through 1K / 470 / 220 (blue 470 / 220) with 470 ohm
  pull-downs. Flip screen (cocktail) not drawn. Drawing: a display line is
  a board column; pixel pairs share a picture column, so the line walks
  the picture two rows and one column a pair, a run per tile (~45 a line);
  background tiles are decoded as drawn into a 256-slot cache (8 KB, ~50
  misses a frame in play) - never the whole 32 KB set, nor a pre-drawn
  background; the characters' fully opaque tiles at a line's ends (the
  score / fuel lines are all opaque black or lettered characters, the
  altitude meter) leave out the background and sprites under them;
  sprites skip columns that draw nothing. `snapshot()` (1.2 KB) for core
  0. Idle skipping, exact (picture and audio CRC of every frame the same
  with and without it over 9,000 frames incl. a two-player game): the
  main loop's idle passes (01F4: an empty task queue, 600D unchanged;
  206 / 274 / 279 T-states and 22 / 30 / 31 opcode fetches by its path)
  are skipped but the last, R and the pass count at 600C added; only when
  the loop's bytes have set 1's CRC; ~42 % of the cycles in play. PC
  (best of 3, 3,000 frames with play): emu 0.030 ms (Pac-Man 0.034), render
  0.080 ms (Pac-Man 0.061); callgrind: render 1.04M host instructions a
  frame (Pac-Man 0.78M), emulation 0.22M (Pac-Man 0.32M; 0.45M before idle
  skipping). `--panel` 240x320 / 320x480: 0 wrong; ~19K pixels sent a
  picture on 320x480 (the background scrolls every frame). Sound: the 8255
  (E03C-E03F, the game writes FF3C) drives discrete circuits, its outputs
  active low (facts, MAME's driver): A0-1 engine level, A2 / A3 engines
  (fortress / space), A4 homing missile, A6 force field, A7 battleship -
  all held while low; one shots on a falling edge: A5 base missile, B4
  small explosion, B5 big explosion (not while sounding), B7 cannon, C0
  enemy shot, C2 alarm 2, C3 alarm 3 (not while sounding). Modelled
  (`emu/sound/zaxxon_sound.*`), all our own design - the board's part
  values weren't to be had (no schematic text online; the manual's
  schematics are scans): two shift-register noise sources (~15 / ~9 kHz,
  the board has two 555 noise generators), the engines as low-passed /
  band-passed noise at 0.5 + 0.157 x the level bits (MAME's figure), 555
  style square VCOs for the missiles, cannon, shot and alarms, a buzzing
  saw for the force field, a throbbing low square for the battleship,
  RC-decayed low-passed noise for the explosions; a 20 Hz DC blocker and a
  soft knee above 16,000 (peaks ~20,000 in play). The 8255's mode write
  (outputs low for a few microseconds before the game writes FF) isn't
  passed to the models (it would start every one shot at power-on).
  Controls: the stick as the cabinet's flight stick - up = pushed forward
  = dive, down = climb (the arcade's own; the ship moves down the screen
  when diving), A or B fire, Select coin, Start 1P, L 2P. SW1 switch 7
  "Sound" is all sound, not an attract switch: the IRQ handler (00DE)
  outputs sound only with it on and outside the attract mode (6004 bit 0),
  so `set_demo_sounds` answers false (attract silent either way, checked).
  `boot_frames` 30: no start-up test, the title page is up at frame 5.
  Memory: machine 20.8 KB (xtensa), ROM 104.5 KB (biggest 8 KB) - the
  tile map / graphics regions mostly go to the scratch flash. Unsure: the
  sound models (character only), the 63-pixel skew offset and sprite
  offsets (MAME's figures; the picture lines up), flip screen.
- Galaxian (`emu/machines/galaxian.*`, set `galaxian` = Namco set 1,
  program chips with Midway's names; portrait 224x256 = lines 16-239 of
  the board's 256-pixel scanlines, the monitor turned like Pac-Man's;
  60.606 Hz). Emulated: Z80 at 3.072 MHz, NMI at line 240 while 7001 is
  on, RAM / tiles / object RAM and their mirrors, inputs (active high;
  set_dips = IN2: bonus, lives; coinage fixed 1 coin 1 credit, upright),
  the watchdog (read 7800, 8 frames), and the video shared with Amidar
  (`emu/machines/galaxian_video.*`): 32 x 32 tiles with a scroll and a
  colour per tile column (= per display row), 8 sprites (sprites 0-2 one
  line lower, none in a scanline's first 17 pixels), the bullets (7 white
  shells - one a line, the last wins - and the yellow missile, 4 pixels on
  one line), the star field (the 17-bit LFSR clocked twice a pixel; the
  256 states that light are found once at init and placed each frame -
  `capture()` buckets them by display line, ~1.5 KB of the 2.1 KB
  VideoState; checked against a brute-force table of the whole period;
  the field moves one state a frame; a pixel shows the later of its two
  states, the board's thirds of a pixel are not drawn), the 32-byte
  palette PROM through 1K / 470 / 220 ohm (blue 470 / 220) into 470 ohm,
  scaled to 224 as MAME does. Flip screen (cocktail) not drawn. Modelled
  (`emu/sound/galaxian_sound.*`, part values from MAME's discrete model,
  read for facts): the tone (1.536 MHz / (256 - pitch) into a 4-bit
  counter, QA 33k / QC 22k (+10k with VOL1) / QD 15k (VOL2) - integrated
  exactly per sample in 16.16 fixed point, so pitch FF is silent), the
  swarm (FS1-3: 555s 100k + 470k / 330k / 220k, 0.01 uF, reset while off,
  their control voltage a sawtooth from a slow 555: 1 uF charged by (DAC -
  0.7 V) / 100k from the 6004-6007 DAC (1M / 470k / 220k / 100k, 15k to
  4.4 V, 330k to ground) - 16-21 Hz - through x 1.43 - 1.06 V = 1.3-3.7
  V), HIT (noise from a 17-bit register at 122.88 kHz sampled at 7.92 kHz,
  held 0.38 s by 172k / 2.2 uF, band-pass 168 Hz Q 2.5), FIRE (a 555 10k +
  22k / 0.01 uF whose control voltage falls with a 2.2k / 47 uF RC while
  FIRE is on - the pitch rises - plus noise, ringing out 0.1 s), the
  mixer (5.1k / 1.32k / 2.2k into 10k). `Astable` = a 555 with a moving
  control voltage run exactly per sample (logs only at a switch). Assumed /
  unsure: the slow 555's current formula and the DAC's 4 V high level (so
  the swarm's 16-21 Hz), the noise taps (the stars'), a 20 Hz output
  coupling, and the levels: by the resistors the tunes would be a sixth
  of the shot / explosion (which would drive the op-amp to its rails), so
  each path has a level for a small speaker (tone x 4.2, swarm x 0.85, HIT
  x 0.9, FIRE x 0.45) and a soft knee 15,000 -> 22,000: play peaks
  ~19,800 (tunes ~12,000, swarm ~6,000, shot ~11,000, explosions
  ~15,000). Silent parts cost nothing (noise, HIT filter and the shot's
  555 run only while heard). No idle skipping: the game's CPU is busy all
  frame (no address takes over ~2 % of its instructions in play). Controls: left / right,
  A (or B) fire, Select coin, Start 1P, L 2P. PC (in play, best of 3, the
  same run): emu 0.061 ms (Pac-Man 0.035; callgrind 465K host
  instructions a frame vs Pac-Man's 352K: sound ~107K of it), render 0.035
  (Pac-Man 0.060). `--panel`: ~8,400 pixels a picture at 240x320, 15,000
  at 320x480 (the stars change ~13 % of the panel every frame), 0 wrong.
- Amidar (`emu/machines/amidar.*`, set `amidar` = Konami; portrait
  224x256, the Galaxian video with no stars or bullets and the background
  colour of Konami's boards: latch bits red 0x55 / green 0x47 / blue 0x55).
  Main board (MAME's "turtles" map): Z80 3.072 MHz, NMI at line 240
  (A008), RAM 8000, tiles 9000, object RAM 9800 (A14 not decoded above
  8000), A000-A03F latch (background, NMI, flips, coin counters),
  watchdog read A800 (8 frames), two 8255s at B000 / B800 (register = A4-
  A5; only their port data is modelled: PPI 1 A / B / C = IN0 / IN1 /
  IN2, PPI 2 A = the sound command, B = sound control - bit 3 falling
  sets the IRQ flip-flop, bit 4 mutes -, C = IN3 coinage). Inputs active
  low; set_dips = lives (IN1 bits 0-1) + bonus (IN2 bit 2), demo sounds =
  IN2 bit 1 (set_demo_sounds), coinage fixed, upright. Sound board,
  exact: Z80 at 14.318 MHz / 8, ROM 0000-1FFF, RAM 8000-83FF (mirrors to
  FFFF), two AY-3-8910 at the same clock (I/O: 10 / 20 = AY 2 address /
  data, 40 / 80 = AY 1; reads AND both when both are picked), AY 1 port A
  = the command, port B = the counter chain (16 x 16 x 2 x 8 x 5 x 2
  crystal clocks: B7 the last /2, B6-B5 the /5's top bits, B4 the /8's
  top bit, B3-B1 high, B0 low), the IRQ (IM 1) held until the CPU takes it
  (stepped an instruction at a time while it waits, as Mr. Do!'s), the
  filters at 9000-9FFF (address bits: two a channel, 0.22 / 0.047 uF; AY
  1 = A0-A5). The sound CPU runs to the main CPU's time at each command /
  control write and at the frame's end (29,531.3 cycles a frame, the
  remainder carried). Assumed: the filters' resistance (1k || 5.1k, from
  MAME's older model of this board), a 20 Hz output coupling, all six
  channels mixed equally; level x 5 (the loudest - coin, start and death
  tunes - peak ~20,350). Idle skipping, exact (`galaxian_tests` against
  runs without it; 8000 frames of the real game compared frame by frame -
  picture, audio, CPU registers): the sound program's timer waits (01E2 /
  01E9, through RST 08 = OUT (40),A / IN A,(80)), 69 cycles and 7 fetches
  a pass; after a read that keeps it waiting the passes that would read
  the same bit 7 are skipped (time, R, A = the last read, F = AND 80's),
  the CPU runs the last one - ~19,700 of 29,531 sound cycles a frame.
  Controls: 4-way stick, B (and A) = Jump (stuns the enemies; the cabinet
  has one button), Select coin, Start 1P, L 2P. PC (in play, best of 3,
  same run as above): emu 0.068 ms (Pac-Man 0.035; callgrind 579K host
  instructions a frame vs 352K), render 0.040. `--panel`: ~2,600 pixels a
  picture at 240x320, 4,700 at 320x480, 0 wrong.
- Dig Dug II (`emu/machines/mappy.*`, set `digdug2` = New Ver., Tom's;
  `digdug2o` (Old Ver.: other program ROMs and sprite lookup PROM) as a
  romset variant; portrait 224x288 = the board's 288 x 224 turned like
  Pac-Man's (MAME's ROT90), 60.606 Hz). Namco's Mappy board, written as a
  reusable machine: a game is a `mappy::Board` (set, main ROM base, which
  I/O chip is where, watchdog, sprite colours, DSW1 default) - Mappy, Motos
  and The Tower of Druaga could follow. Emulated: two 6809s at 1.536 MHz
  (main + sound CPU, 264 lines of 96 cycles), run in 2-line slices, main
  first; the 1 KB shared RAM (main 4000, mirrored 4400; sound CPU 0000,
  mirrored to 1FFF); the LS259 latch both CPUs write (A1-A3 = bit, A0 =
  data: sound / main IRQ enable - off clears a pending IRQ -, flip, sound
  on, I/O chips run, sound CPU runs - its reset restarts it from its
  vector); both IRQs at VBLANK (line 224), held until acknowledged; the
  scroll (writes to 3800-3FFF, address bits 3-10); the tile map: 36 board
  columns, 2-33 a 32 x 60 map scrolled down by the scroll, 0-1 / 34-35
  fixed (score / round; MAME's address formula for them); 8 x 8 2-bit
  characters (ROM inverted), colour lookup PROM into the palette's upper
  16, attribute bit 6 = in front of sprites (its lookup-15 pens let them
  through); 64 sprites of 16 x 16 4-bit pixels (2 x 16 KB ROMs, planes 0-1
  / 2-3), 16 or 32 wide / tall, flips, X bit 8, the one-line delay, lookup
  15 = transparent, later ones on top; the palette through Pac-Man's
  resistor weights; the 15XX (8 voices of the shared `namco_wsg`, 24 kHz -
  the board's own sample rate, so the counter is exact; its registers are
  the shared RAM's first 64 bytes, applied after each slice; register 2
  sets the waveform position, `Wsg::set_position`); Dig Dug II has no
  watchdog (MAME's count 0). Modelled (`namco_io::Chip5x`, no dumps of the
  MB8843 programs): the 56XX / 58XX - 16 nibbles shared with the CPU,
  commands run 1 line after VBLANK: ports, coinage, coin mode (credits
  BCD, coins / service / starts on their edges, starts only while nibble
  9 = 0), DIP reads through the pin-13 multiplexer, the start-up checks
  (58XX's LFSR XORs, 56XX's sum) - checked against the answers Mappy,
  Grobda, Phozon and Super Pac-Man expect (`mappy_tests`). Dig Dug II's
  chip 0 (58XX) = coins, stick, button 1 (pump) and starts; chip 1 (56XX)
  = DSW1 / DSW0 (bit 0 = button 2, the jackhammer). Controls (MAME's
  ports): 4-way stick, A = pump, B = jackhammer, Select coin, Start 1P, L
  2P. No demo sounds switch; attract silent (6,000 frames measured).
  boot_frames 286 (RAM / ROM checks show "0 0 1", a settings page flashes
  4 frames at 274, the title at 286). Idle skipping, exact (`--hash` the
  same over 9,000 frames with two games; `mappy_tests` too): a CPU in
  "LDA addr ; BEQ back" (extended or direct) on a RAM byte that is 0 with
  no interrupt it would take skips the passes that end before the slice
  does (A = 0, flags as LDA left them) - the main CPU's frame wait (80C1)
  and the sound CPU's (E381), ~60 % of the cycles. Drawing: per display
  line (a board column), tile runs unrolled 8 at a time, sprite columns
  decoded 8 lines at once (one 64-bit read a plane pair). PC (callgrind,
  2,000 frames incl. play): emulation 507K host instructions a frame
  (Pac-Man 349K - the 6809s' real work), drawing 781K (Pac-Man 786K);
  wall time emu ~0.04-0.05 ms, render ~0.05 ms (Pac-Man ~0.03 / 0.05).
  `--panel` 240x320 / 320x480: 0 wrong. Sound peaks ~20,300 (WSG gain 26).
  Memory: wrapper 24.4 KB (xtensa), snapshot 4.7 KB, ROM 76.8 KB (7
  regions, biggest 16 KB). Unsure: the 1-line delay of the I/O chips'
  commands (MAME: 50 us), the "0 0 1" on the start-up screen (unknown
  whether it is a result of the checks), Old Ver.'s lookup PROM (MAME
  calls it possibly a bad dump).
- Donkey Kong Jr. (`emu/machines/dkongjr.*`, set `dkongjr` = US set F-2;
  Tom's zip has the old names `dkj.5b` / `.5c` / `.5e`, `c_3h.bin`, ...,
  same CRCs; portrait 224x256, 4-way, A / B jump, Select coin, Start 1P,
  L 2P). Donkey Kong's board, not a copy: `dkongjr::Machine` is
  `dkong::Machine` with the Junior's wiring switched on
  (`init_board()`: its sound model + room for the second tile bank;
  everything else is the same code, branches on `jr_`). Donkey Kong's
  pictures and sound checked unchanged frame for frame (`--hash`, 6,000
  played frames of `dkongo`). What differs, all emulated: the program -
  three 8 KB chips loaded whole, their 2 KB pieces mapped where the board
  puts them (`kJrRomMap`: 5B = 0000 0800 3000 3800, 5C = 2000 4800 1000
  5800, 5E = 4000 2800 5000 1800); latch 4H (7C80-7C87: bit 0 the tile
  bank - codes + 256 -, bit 1 to the 8035's P2 bit 6); latch 5H (7D80
  the 8035's INT as on DK, 7D81 the falling sound, 7D82-7D87 as DK); IN2
  bit 6 not connected (1); the 8035's 4 KB program (3H), MOVX = the tune
  latch's bits 0-4 (not inverted, no data ROM), P2 inputs bit 6 / 5 / 4 =
  4H bit 1 / 6H bit 3 / 6H bit 6, inverted (the port reads latch AND
  inputs); T0 / T1 as DK; DSW0 as DK's but the bonus 10,000 - 25,000.
  P2 bit 7 = the DAC's full level (set) / fade (clear), as our DK: MAME's
  discrete wiring reads the other way, but then the intro tune and the
  play music (the program holds the bit set) were silent - checked in a
  played run. Modelled (`emu/sound/dkongjr_sound.*`, part values and the
  74LS624 VCOs' breadboard measurements from MAME's discrete description,
  our own solution of the circuits): the one-shots (74LS123, 0.25 R C (1 +
  700 / R): walk 56 ms, jump / climb 0.26 s) fire on a latch bit's 1 -> 0
  write and retrigger (the game pulses the bits ~3 frames); walk (6H bit
  0; bit 7 the variant) = a NAND of the one-shot and (3.1 kHz VCO xor
  counter 6L bit 3) - or counter bit 6 with bit 7 set -, 6L clocked by a
  VCO at 14.45 / 59.4 kHz as its bit 11 / 12 picks; jump (bit 1) = a
  313-1378 Hz VCO whose control voltage the one-shot (47k) and counter bit
  11 (10k) set through 47 uF, under the RC envelope (10 uF via 120 + 10k
  into 100k, a diode to -0.6 V, a transistor shorting it while the tone is
  high); climb (bit 2) = a 16-bit noise register at 710 Hz under the same
  envelope (4.7k), high-passed 20k / 0.47 uF and low-passed 120 ohm / 1
  uF; fall (5H bit 1) = 570 Hz while set; the DAC 0-5 V, fade 10k x 10
  uF, Sallen-Key 1.9 kHz; the resistor mixer (DAC 47k, walk 100k, jump
  47k, climb 20k, fall 150k), its 10 nF, the 1k / 4.7 uF coupling. Levels
  for a small speaker (by the resistors the climb noise would be 2.3x the
  music): intro tune ~16,500, play music ~6,000, walk ~10,000, jump /
  climb ~14,000, fall ~8,000; soft knee 16,000 -> 22,000. Unsure: the
  effects' character (no recording compared), the trigger edge (falling,
  by MAME's inverted inputs), jump / climb leave a quiet ~1 s tail at -0.6
  V (as the circuit does). Attract mode silent (no demo sounds switch in
  MAME's DIP list, measured 0). `boot_frames` 30 (the attract page is up
  at frame 6). PC (in play, best of 3): emu 0.056 ms (Pac-Man 0.027, DK
  0.070 in the same run), render 0.062 (Pac-Man 0.050); `--panel`: ~1,600
  pixels a picture at 240x320, 2,800 at 320x480, 0 wrong. Memory: machine
  26,136 bytes (xtensa; DK's 21,800 + 4 KB second tile bank + 240 B sound
  model), snapshot 1.8 KB, ROM 44.8 KB (biggest 8 KB).
  `tools/host_tests/dkongjr_tests.cpp`: the map, tile banks, the 8035
  wiring, the sounds on hand-made programs.
- Elevator Action (`emu/machines/elevator.*`, game id `elevator`; MAME's
  `elevator` (BA3, 4 boards, 2764s) or `elevatora` (EA, 5 boards, 2732s -
  Tom's zip: `ea-ic69.bin` ... `ea-ic8.bin`, `ba3.11`, `eb16.22`, the same
  CRCs) as romset variants; the MCU dump `ba3.11` is needed; the bootleg
  `elevatorb` isn't read; landscape 256x224 = lines 16-239 of 264, 384 x
  264 pixel clocks at 6 MHz = 59.19 Hz). Taito's SJ board. Emulated: the
  Z80 at 4 MHz (IM 1, the VBLANK IRQ at line 240 held until taken - a
  frame runs VBLANK to VBLANK, so the picture is the RAM as VBLANK
  starts), RAM, the character RAM (9000-BFFF, write only, two banks of 256
  characters / 64 sprites, 3 bit planes 0x800 apart, bit 0 = the left
  pixel) that the program fills from the 32 KB graphics ROMs through the
  D404 port (D509 / D50A pointer, counting up), three 32 x 32 playfields
  with a horizontal scroll each (MAME's measured offsets 3 / 1 / -1 and 8
  / 10 / 12) and a vertical scroll per column, 24 sprites (slots 10-17
  absent; drawn 1E ... 0 then 1F on top; x 3-254 shown, wrapping), the
  palette RAM (9 bits, inverted, 1k / 470 / 270 ohm), the plane priority
  PROM pixel by pixel (the mask of blank planes picks the plane; when the
  PROM row is a plain order, as all of this game's are, the planes are
  laid on each other 8 pixels at a time - same pixels), the plane enables,
  the sprite bank, the DIPs (A by set_dips, B / C on AY 0's ports: 1 coin
  1 credit, MAME's defaults), the watchdog (128 VBLANKs, resets the board),
  the sound board (Z80 3 MHz, IRQ at 36.62 Hz held until taken, NMI =
  command waiting AND AY 3 port B bit 0 low, OR semaphore 2; the command
  latch, its flags, AY 2 port A = IN4's high nibble), four AY-3-8910s at
  1.5 MHz (`emu/sound/ay8910.*`; AY 0 on the main board plays the music)
  and the DAC (AY 1 port A) with its volume ladder (AY 1 port B,
  inverted, 680k ... 4.7k). The 68705P3 MCU (`emu/cpu/m68705.*`, our own
  6805 core: every documented opcode with the HMOS cycle table, 11-bit
  addresses, SP 60-7F, ports with DDRs (C four bits), the timer with its
  7-bit prescaler, /INT edge-latched, BIL / BIH; undocumented opcodes =
  counted 2-cycle no-ops) at 3 MHz / 4 on taitosj's security interface:
  Z80 8800 = data (write: ZREADY + the MCU's /INT; read: ZACCEPT), 8801 =
  status; MCU port A = the bus (Z80 byte AND the byte read from the Z80's
  memory AND its outputs), port B = 68LRD / 68LWR strobes, BUSRQ, 68WRITE
  / 68READ with the address counter (low byte counts), LAL / UAL; port C =
  ZREADY, ZACCEPT, BUSAK. The program checksums its EPROM at power-on,
  answers every command (~3 a frame, a rolling code) and would take the
  Z80's bus to corrupt RAM if the protocol failed - with ours it never
  does (0 bus grants in 8,000 played frames). Sync: one 12 MHz tick count
  (main 3, sound 4, MCU 16, AY 8 ticks a cycle); the main CPU leads, the
  MCU catches up at every 88xx access and after each slice; while the MCU
  sits in an idle loop only the Z80 or its timer can end (the main loop at
  0180, waits on its latches) the Z80 runs to the timer's interrupt, else
  the two step an instruction at a time; a bus request stops the Z80 at
  once (BUSAK at once) and it loses the time. Modelled / assumed: the AYs'
  mix by MAME's routes (AY 0's channels x 0.15 each, AYs 1-3 with outputs
  tied - taken as the average - x 0.5 / 0.5 / 1.0, the DAC x 0.15), a 20
  Hz output coupling, gain for ~20,000 peaks (a soft knee above 16,000;
  play peaks 13,700-19,100), AY 3's RC filter switch (port A bits 0-1)
  not modelled (MAME doesn't either); unmapped reads 0 (as MAME's map,
  which the game runs on); collision registers read 0 (the program never
  reads D400-D403); flip screen (cocktail) not drawn. No demo sounds
  switch (attract silent). Controls: 8-way stick, A = shoot (button 1), B
  = jump (button 2), Select coin, Start 1P, L 2P. boot_frames 41 (white
  until the title page). Idle skipping, exact (per-frame picture, audio
  and all three CPUs' registers / the MCU's RAM the same with and without
  it over 8,000 frames incl. play; `elevator_tests` too): the MCU's loop
  at 0180 (27 cycles a pass, a random-number counter at $23 - run in
  closed form to the timer's next interrupt) and branch-to-self waits on
  its latches; the main CPU's frame wait at 73F2 and the sound CPU's main
  loop at 0164 - after one pass run, every register they set is as each
  pass leaves it, so the rest are time and R (main ~49.5K, sound ~39K
  cycles a frame skipped). PC (same run, best of 3): emu ~0.050 ms
  (Pac-Man 0.028-0.032); callgrind: emulation ~0.66M host instructions a
  frame, the four AYs ~0.3M of it. Drawing (2026-10-23; Tom, v0.23.0 on
  the 4.0" S3: "59.2 fps, 24 shown/s ... picture 33.6 (send 1.8)" - ~31
  ms of rendering on core 0, ~140 us a line, Dig Dug II 8.0): v0.23's
  render was 64-bit words with an 8-byte memcpy to an unaligned place a
  column (a newlib call on the Xtensa, 96 a line) and a 1.6 KB stack
  frame that put spilled variables out of the short load offsets - ~14K
  Xtensa instructions a line. Now, exact (`--hash` the same over 12,000
  frames of attract and 3 x 12,000 played; `elevator_tests` against the
  per-pixel model on dense and sparse characters, one and many column
  scrolls): bit planes - `gather_line` (its own function, for registers)
  copies a playfield line's 32 characters' three plane bytes into 3 x 32
  bytes (12 Xtensa instructions a column when all 32 column scrolls are
  the same - playfields 1 and 2 always are; the VideoState keeps a count
  per playfield of columns unlike column 0, `cs_diff`, updated on writes;
  23 otherwise); a playfield line with nothing in it stops there; the scroll turns
  each plane into 8 screen-aligned words (a funnel shift each: `ssr` +
  `src` by inline asm on the Xtensa, a 64-bit shift elsewhere); the
  layers are laid on each other 32 pixels at a time in the PROM's order -
  a playfield blank there costs three loads - else 4 pixels at a time
  from each plane's nibble through 16-entry tables (`spread_`, 256 B in
  the machine, made at init) into words of four pens; sprites (the 24
  slots in a fixed order, one unsigned compare a slot) as pens into a byte
  line, cleared only when one is on it; colours go out 4 at a time, looked
  up only when the 4 pens differ from the 4 before (74 % don't). No
  library calls, 32-bit words only, stack frame 768 B. Per line (83
  sampled frames of play): 2.2 one-scroll + 0.8 per-column gathers, 9.7
  live spans, 67 nibbles, 17 of 64 words looked up - ~4.5K Xtensa
  instructions by the code's count (v0.23 ~14K): the S3's render should
  be ~3x faster, ~6-10 ms (picture ~9-13 ms with hash and send). Tom's
  4.0" S3, v0.24.1: "59.2 fps, 48 pics/s, slowest second 59 / 59, emu 6.3
  ms (worst 18.3), picture 11.1 (worst 45.7), sending 0.8 ms, 3.6K
  pixels" (v0.23.0: 24 a second, picture 33.6). PC: render 0.114 -> 0.099 ms (Pac-Man 0.061, the
  same run); callgrind 1.55M -> 1.46M host instructions a frame (x86 has
  64-bit words; Pac-Man ~0.79M). `--panel` 240x320 / 320x480: 0 wrong
  (~2,000 / 3,600 pixels a picture). Memory (xtensa): machine 22.0 KB
  (the VideoState 15.6 KB - character RAM 12 KB), the snapshot 15.6 KB,
  ROM 74.3 KB (biggest 4 KB; 8 KB in `elevator`).
- Xevious: removed in v0.23.1 (Tom, 2026-10-21: "It's just not going to
  work" - on the 4.0" S3 it drew 21 pictures a second: its background
  scrolls all the time, ~98,000 pixels to send a picture). It ran on the
  Galaga board with a 50XX model and the 51XX's Xevious mode (git history
  has it, the v0.22.0 merge). Don't add it back.
- Gyruss (`emu/machines/gyruss.*`, set `gyruss` = Konami; Tom's zip is
  exactly it; `gyrussce` (Centuri) has other main ROMs, not read).
  Portrait 224x256: the board's 256-pixel lines 16-239 turned like
  Galaxian's (display x = 239 - line, display y = pixel). 256 lines of 198
  main cycles (396 pixel clocks at 6.144 MHz): 60.606 Hz. Emulated: the
  main Z80 at 3.072 MHz (ROM 0000-5FFF, colour / video RAM 8000-87FF, RAM
  9000-9FFF, A000-A7FF shared with the sub CPU's 6000-67FF, C000 DSW2 /
  C080 SYSTEM + the sound IRQ write / C0A0 P1 / C0E0 DSW1 / C100 DSW3 + the
  sound command, LS259 at C180: Q0 NMI enable - the NMI line stays high
  until it's cleared, so a frame's NMI needs the handler to clear and set
  it, as the game does), inputs active low, no watchdog (MAME has none);
  the sub CPU, a Konami-1 at 1.536 MHz = our 6809 whose opcode fetches
  (the opcode, and a 10 / 11 prefix's second byte - not operands or data)
  go through `op_page` / `map_opcodes` / `op_read` (added to
  `emu/cpu/m6809.*`: map_read keeps op_page = read_page, so the other
  6809 games cost the same - Robotron, Defender, Qix, Star Wars `--hash`
  identical over 5,000 frames each); the bytes XOR 22 / 82 / 28 / 88 by
  address bits 1 and 3 (`konami1_decrypt`; from address 0), the program
  decrypted once into an 8 KB copy for its fetches; scanline read at 0000,
  IRQ enable at 2000 (level, dropped when cleared), RAM 4000-47FF with the
  sprites at 4040-40FF; the main and sub CPUs take turns every 2 lines.
  Video: 32x32 tiles of 8x8 2 bits (code bit 8 = colour RAM bit 5, colour
  bits 0-3 through the tile colour PROM to palette 16-31, flips bits 6 /
  7, bit 4 = in front of the sprites, whole tile), 48 sprites of 8x16 4
  bits (x = byte 0, top line 241 - byte 3, code byte 1 bits 1-7 + byte 2
  bit 5, bank byte 1 bit 0 = the second half of each 64-byte sprite,
  colour byte 2 bits 0-3, flip x = byte 2 bit 6 CLEAR, flip y bit 7, pen
  0 clear, slot 0 on top), the palette PROM through 1K / 470 / 220 (blue
  470 / 220) into 470. The sub CPU rewrites sprite slots while the beam
  is in the picture (MAME draws partial screens at each write): each
  slot's value is kept with the lines it was shown on (a write at line L
  closes the old value at L; VBLANK writes are the next frame's), up to
  192 pieces a frame, built into the VideoState at VBLANK; the renderer
  clips each piece to its lines. Tiles are decoded by column at init (8
  KB), sprites drawn from the ROM. Flip screen (cocktail) not drawn.
  Sound board, its programs exact: Z80 at 3.58 MHz (ROM 0000-3FFF, RAM
  6000-63FF, 8000 the command; I/O 00 / 04 / 08 / 0C / 10 = AY 1-5
  address, +1 read, +2 data, 14 = the i8039's interrupt, 18 = its
  command), IRQ held until taken (stepped as Amidar's); five AY-3-8910 at
  1.79 MHz; AY 3 port A = the timer (the CPU clock / 1024 into an LS90:
  0 1 2 3 4 9 A B A D); AY 1 / 2 port B switch 0.047 (bit 0) / 0.22 uF
  (bit 1) per channel into an RC low-pass with the chip's ~356 ohm + 1k
  (MAME's figures); the i8039 (`emu/cpu/i8035`, now with `ram_mask` for
  the 8039's 128 bytes - Donkey Kong `--hash` identical) at 8 MHz, 8,800
  machine cycles a frame: every MOVX reads its command, P2 drops its
  interrupt, P1 = the DAC; its DAC changes are queued with their times
  (they don't make the AYs catch up). Mix (mono = left +
  right, MAME's resistor mixer: AY 1 2.2k a channel, AY 3 / AY 4 3.3k
  each tied, right; AY 2, AY 5, the DAC 4.7k, left), an 8 kHz output RC
  (200 ohm / 0.1 uF), a 20 Hz coupling (assumed), x 9 with a soft knee
  above 16,000 (play peaks ~18,000-19,000). Assumed: the DAC's swing
  against the AYs' (4 V against ~1 V an AY channel). The AY core
  (`emu/sound/ay8910.*`) got faster for this, exact (Burger Time, Amidar
  `--hash` identical): a tone's square wave between noise / envelope
  steps is counted in closed form (its time high from the counter and
  period), and a run in which no output can change only adds the levels,
  inline, the ticks put on the counters later (`settle()`; at most 4096
  ticks ahead). Idle skipping, exact (`gyruss_tests`; and 6,000 attract +
  3 x 8,000 played frames of the real game compared frame by frame with
  it off - picture, audio, the four CPUs' registers): the main CPU's task
  wait at 05F4 (JP / LD A,(5865) / LD H,A / LD A,(9433) / LD L,A / LD
  A,(HL) / RLCA / JP C, 65 cycles 8 fetches, its own RAM only) from any of
  its instructions at a slice's start to where the CPU would stop at the
  slice's end (whole passes, then instruction by instruction: PC, R, A H L
  F as the loop leaves them); the sub CPU's BRA * at F0B6 (in its long
  put-off runs the 6809 core idles to the run's end by itself); the sound
  CPU's timer waits at 00FE / 0105 (through RST 10, 69 cycles 7 fetches;
  the IN stops the CPU, the passes that read the same bit 2 skipped, and
  when the pass that reads the change ends before the CPU's target that
  one too - the CPU left after the loop with A = the read AND 04, F, R, SP
  as it leaves them); the i8039's NOP / JMP at 0019 and any DJNZ Rr to
  itself (its delay after each DAC write, which stops it), the timer's
  prescaler counted. Left in play a frame: main ~23K cycles, sub ~15K,
  sound ~12K, i8039 ~2K (all real work). Speed (2026-10-23; Tom's 4.0" S3,
  v0.23.0: "43.0 fps (game 60.6), emu 22.0 ms (worst 50.0)"), all exact
  (`arcade_host --hash` of every frame the same as v0.23.0 over 6,000
  attract, 3 x 8,000 played, 20,000 attract with demo sounds off and
  20,000 played frames; the CPUs frame by frame as with idle skipping
  off): the sub CPU's turns (2 lines each, after the main CPU's) are put
  off until the main CPU touches the shared RAM (its pages unmapped while
  the sub CPU is behind; the first access runs it up to the slice before),
  VBLANK or the frame's end, and then run in one go - 128 6809 runs a
  frame -> ~19, the same instructions at the same times (nothing it reads
  changed meanwhile); the output samples' sums go to rows (`rows_`, 32
  samples, 2 KB): an AY write brings only that AY up to date, the other
  AYs, the i8039 (run only then and at its own port writes) and the mix
  wait until the rows fill, a filter switch (AY 1 / 2 port B) or the DAC
  queue needs them - ~16 such passes a frame instead of one at every AY
  write; the main CPU's sound writes stop it after the instruction and
  run_frame() runs the sound board then, a sound CPU write that needs the
  i8039 or the samples stops it and sound_to() does that (not nested
  deep inside the interpreters - each call that deep spills the ESP32's
  register windows); `ay8910::Chip::run_pieces` (a row of pieces: the
  quick path's stretch between output changes at once, silent channels
  add nothing), the noise register's long catch-ups by matrix powers (17
  x 17 bits over GF(2), M^2^i, 1.2 KB of flash: `step_noise`); the mix's
  filter states below the smallest normal float set to 0 (silent channels'
  tails sat in denormals, which the PC ran 4x slower; such a state never
  reaches an output sample); no 64-bit division (the sound clock in 32
  bits, the timer's round kept mod 10240); the samples' ends stepped
  along; the capture's sprite order by a counting sort; the i8035 core
  keeps whether an interrupt can be taken (`irq_ready_`; Donkey Kong /
  Jr. `--hash` identical). A single interrupt / HALT test in the Z80 loop
  was tried and dropped (more host instructions). PC, in play (best of 5,
  the same runs): emu 0.168 -> 0.093 ms (Pac-Man 0.034), render ~0.047
  (Pac-Man ~0.062); callgrind 1.02M -> 0.875M host instructions a frame
  emulated in play (Pac-Man 0.345M; attract 0.90M -> 0.76M): main + sound
  Z80 ~260K, the 6809 ~260K, the AYs ~100K, mix ~70K, i8039 ~80K, glue
  ~100K. With the x86's denormal flush on (FTZ + DAZ, as a CPU without a
  denormal penalty) base ~0.12, now ~0.11 ms: the gain on a board whose
  FPU has no denormal penalty is ~10-15 % plus what the flatter calls and
  fewer CPU switches save there (unmeasured; ESP32's denormal speed not
  known). The 4.0" S3 ran other games' emulation at ~140x the PC's wall
  time (Elevator Action 7.2 ms / 0.050, Donkey Kong Jr. 8.0 / 0.056,
  Gyruss 22.0 / 0.16): ~13 ms expected if the board tracks the PC, ~19
  ms if its FPU never paid for denormals. Tom's 4.0" S3, v0.24.1: "46.8
  fps (game 60.6), 47 shown/s, slowest second 36 / 36, emulation 19.5 ms
  (worst 53.8), picture 9.9 (worst 25.5), sending 2.1 ms, 5.8K pixels" -
  the ~19 ms case: the S3 gained ~11 %, not the PC's 45 %. Controls: the 8-way stick as on
  the cabinet (it moves the ship round the circle), A (or B) fire, Select
  coin, Start 1P, L 2P. Snapshot for core 0: the VideoState, 4.4 KB.
  Memory: machine 39.6 KB (Xtensa; 37.6 before the 2 KB of sample rows),
  ROM 92.5 KB (biggest 8 KB). `boot_frames` 170 (the grid test, then the
  stars fade, the score lines at 170). `--panel` 240x320 / 320x480: 0
  wrong.
- 1942 (`emu/machines/c1942.*`, game id and zip `1942`, C++ namespace
  `c1942`; MAME's `1942` (Revision B), `1942a` (Revision A - Tom's zip,
  old names `1-n3a.bin` ... `01m_sb-9.bin`, same CRCs) and `1942b` as
  romset variants - only the five main program chips differ; portrait
  224x256 = the board's 256-pixel lines 16-239 turned as MAME's ROT270:
  display x = line - 16, display y = 255 - pixel; 262 lines of 192 cycles
  = 59.64 Hz). Tom: Galagino did it, so it's possible - Galagino (GPL)
  was read for facts and shortcuts only, nothing taken. Emulated: the main
  Z80 at 3 MHz (12 MHz / 4; ROM 0000-7FFF, the bank at 8000 = M5 / M6 (8
  KB, A000-BFFF empty) / M7 / empty by C806, inputs C000-C004 active low,
  set_dips = DSWA's bonus / lives, coinage 1 coin 1 credit, upright, DSWB
  defaults; C800 / C801 the two sound commands, C802-C803 scroll, C804 bit
  4 = sound CPU held in reset (starts at 0 when let go), C805 palette
  bank, CC00-CC7F sprites, D000 characters, D800 background, E000 RAM; no
  watchdog), interrupt mode 0 with the RST the timing PROM sb-1 gives
  (RST 08 at line 109, RST 10 at 240) and the sound CPU's IRQ (lines 44,
  109, 175, 240), all read from the PROM at init and held until taken
  (stepped an instruction at a time while raised; a later RST overtakes
  an untaken one, as MAME's HOLD_LINE), the sound Z80 at 3 MHz (ROM, RAM
  4000, latches 6000 / 6001, AY 1 8000 / 8001, AY 2 C000 / C001) run to
  the main CPU's time at each command / reset write and each PROM line,
  two AY-3-8910s at 1.5 MHz; the six channels mixed evenly (220k each
  into the pot - MAME's netlist values), a 20 Hz coupling (assumed), x 1.6
  with a soft knee above 16,000 (play peaks ~18,000-19,500). Video:
  characters 32 x 32 of 8 x 8 2 bits (code bit 8 = attribute bit 7,
  colour bits 0-5 through sb-0 to palette 128-143, pen 0 clear) on top;
  the background 32 x 16 tiles of 16 x 16 3 bits (planes 16 KB apart),
  per column 16 codes + 16 attributes (bit 7 code bit 8, bit 6 flip Y, 5
  flip X, 0-4 colour through sb-4), palette bank C805 (x 16), the 9-bit
  scroll along the board's lines (down the turned screen; 1 pixel every
  other frame in play); 32 sprites of 16 x 16 4 bits, 16 / 32 / 64 / 256
  lines tall (byte 1 bits 6-7), X bit 8 = byte 1 bit 4, code bit 7 =
  byte 1 bit 5, bit 8 = byte 0 bit 7, colour through sb-8, pen 15 clear,
  slot 0 on top, slots 16-23 only on lines up to 128 and 24-31 from 129
  (MAME's reading of the board's sprite counter), no wrap past line 255;
  three colour PROMs through 2.2k / 1k / 470 / 220. Not drawn: flip screen
  (cocktail). Drawing: a display line is one board pixel column; the
  half background column under it (8 display lines) is decoded once into
  a 3.5 KB strip of RGB565 pens (8 pixel columns x 224 lines; repeated
  tiles down the column - open sea - copied as words), valid while that
  column's 32 bytes (compared as 8 words; VideoState is `alignas(4)`) and
  the bank are the same, and a line's background is one 448-byte memcpy
  of a strip row; characters skip pixel columns that are blank (a byte a
  character, made at init). Render cut 2026-10-24, exact (`--hash` of
  12,000 played, 12,000 played (other inputs) and 6,000 attract frames the
  same; `c1942_tests`' per-pixel model): v0.24 decoded palette indices
  through 64-bit spread tables + bswap with scattered byte stores (~13
  Xtensa instructions a pixel), 16 memcpy calls per repeated tile, a
  memcmp call and a 224-pixel palette lookup (~6 a pixel) every line; now
  the tile's 8 pens are looked up once, 4 pixels' 3-bit values come from
  three 16-entry nibble tables (kNib / kNibRev for flipped tiles, a
  template per flip, fy by pointer step) - ~9.4 a pixel with the pen
  lookup (the Xtensa listing). Per picture by the code's count (77 tiles
  decoded, 158 copied, 256 lines; sprites / characters unchanged ~0.23M):
  background ~0.73M -> ~0.29M Xtensa instructions. PC render 0.12 -> 0.06
  ms a frame. Idle skipping, exact (`--hash` identical over 4,000
  played frames with and without it; `c1942_tests` compares both CPUs'
  registers, RAM and audio frame by frame): the main task loop at 0087
  (EI, scan 16 slots at EFC0 for one >= 20, 825 cycles a pass, writes
  nothing - only while every slot is below 20 and no interrupt is raised)
  and the sound CPU's EI / JR at 0064 (16 cycles, while no IRQ is
  raised): from any instruction in the loop the CPU runs a pass itself
  (two at most); a pass that leaves every register as it found them
  repeats, so the whole passes to the run's end are skipped (time and R
  counted); the CPUs run in 1 KB chunks while their loop is known, to be
  caught coming back into it; ~72K of 100K cycles a frame skipped in the
  attract mode, ~56K in play. PC (in play, best of 3, the same runs): emu
  ~0.07 ms (Pac-Man ~0.04), render ~0.08 (Pac-Man ~0.08); callgrind 0.43M
  host instructions a frame emulated, 0.98M rendered (0.28M of it the
  strips). The picture's weak spot: the scroll moves the whole background
  every other frame, so `--panel` 320x480 sends ~71,000 pixels a picture
  in steady play (61 % of the panel; 48,000 averaged over 6,000 frames
  with deaths and READY pages), 240x320 ~40,000 - fewer than 60 pictures
  a second on the boards (by Xevious' 21 a second at 98,000 on the S3,
  ~25-30 on 320x480); the game itself keeps full speed. Tom's 4.0" S3,
  v0.24.1: "59.7 fps (game 59.6), 31 pictures a second, slowest second
  60 / 60, emulation 7.4 ms (worst 15.2), picture 22.2 (worst 51.8),
  sending 12.5 ms, 46.7K pixels" - sending is over half the picture.
  With the render cut and line_diff's (see Drawing) a heavy picture is
  ~1.93M -> ~1.03M Xtensa instructions (light ~1.23M -> ~0.64M), so render
  + hash ~9.7 -> ~5.2 ms on the S3 (estimate, not measured), picture
  ~22 -> ~18 ms; with the sooner snapshots (Two cores) ~48 pictures a
  second by the timing model (~32 without them). Other boards, same
  model (ESP32 emulation assumed 11 ms; send at the S3's 0.27 us a pixel,
  Tom's mix of 46.7K pixels on 320x480 = ~26K on 240x320; render +20 % for
  ROMs read from flash): 3.5" / 4.0" ESP32 ST7796 ~30 -> ~41; 2.8" / 3.2"
  ST7789 (80 MHz) ~34 -> ~60; 2.8" ILI9341 (40 MHz, ~13 ms sending) ~30
  -> ~41. Assessed, not built (2026-10-24): (1) the panels' hardware
  vertical scroll (VSCRDEF 33h / VSCRSADD 37h on all three controllers):
  in portrait it moves the panel's rows, which is 1942's scroll direction;
  a model of it (best shift each picture, segments compared row by row
  against the shifted panel) cuts play pictures 2 frames apart from ~59K
  to ~9K pixels on 240x320 (shown 240 x 274: 18 doubled rows break the
  one-row shift) and ~105K to ~39K on 320x480 (366 rows: the 110 doubled
  rows can't line up with a one-row shift) - the score / lives
  characters, sprites and those rows resent. Not built: whether VSCRSADD
  counts in memory rows with LovyanGFX's rotation 2 (MY | ML set; the
  ILI9341 datasheet counts the fixed areas from the bottom when ML = 1,
  ST7789 wants ML = 0 in scroll mode) is a hardware fact per controller;
  every push's rows would be mapped and split at the wrap; RST is tied to
  EN and the init lists send no SWRESET, so a crash mid-game would leave
  the menu scrolled until a boot-time reset of the scroll registers; the
  shifted score / sprites show for the few ms until resent. Tom would
  need to test it on all four panels in both Rotate Screen 180 settings,
  and after a crash. (2) Sending by DMA while the next lines render (S3
  only; LovyanGFX already DMAs pushes under 1 KB through its flip buffer
  on every board, bigger strips go 64 bytes at a time by the CPU):
  picture ~max(render, send) - ~13 ms on the S3, 60 a second when under
  ~14 ms; needs two strips (+5 KB) and pushImageDMA / waitDMA, and a test
  on the S3 (pictures and the touch's I2C, the codec) - never on the
  ESP32-32E boards (v0.2.0). Controls: 8-way
  stick, A fire, B loop (the roll), Select coin, Start 1P, L 2P. No demo
  sounds switch (attract silent, measured 0). `boot_frames` 12 (garbage,
  black, then the title). Snapshot for core 0: the VideoState, 3.1 KB.
  Memory: machine 19.6 KB (Xtensa), ROM 209.8 KB in 10 regions (biggest
  16 KB) - more than RAM + the 128 KB scratch area, so the loader got a
  way to borrow the ROM store's free sectors just below the area
  (`core/scratch_layout.h`, `flash_store::scratch_below()`, negative
  scratch offsets; `rom_store::Store::forget_erased()` makes the store
  erase them again before storing a zip there): ~64 KB for 1942 on an
  ESP32 board (the sprites); every other game keeps its places and
  header. Needs the store to have that much free (else "Not enough flash
  room for its ROMs"); the S3 holds it all in PSRAM. **Not yet run on a
  board** (the loader change included).
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
  Exact passes (`Speedup::pass_cycles` / `pass_fetches`, 2026-10-17): for
  a fixed loop the CPU skips whole passes but the last (time and R
  counted as if run) and runs the rest itself, so it ends the slice where
  running would have - bit-exact, unlike the whole-slice skips above.
- Galaga / Dig Dug speed (2026-10-17; Tom, v0.14.0: Galaga dropped many
  frames when a wave swoops in). Measured with a bot-played 20,000-frame
  run (coins, stages 1-2, deaths) on the PC: the busy play frames are real
  work - main ~25K, sub ~27K, sound ~12K Z80 cycles a frame, little left to
  idle-skip - and rendering cost twice the emulation. Changes, all checked
  bit-exact (`arcade_host --hash`: every frame's picture and audio CRC the
  same as before over 20,000 frames of Galaga and Dig Dug, 6,000 of
  Pac-Man, Ms. Pac-Man, Rally-X, Donkey Kong, Mr. Do!): rendering - a
  per-frame list of which sprites touch each 16-line band
  (`sprite_bands`, sprites with only blank tiles left out) instead of 64
  tries a line, per tile row a mask of the characters that draw anything
  (blank ones skipped unseen), unrolled opaque tile rows, Dig Dug's
  sprite wrap found once; Z80 core - LDIR / LDDR between direct pages
  run in one go (`block_copy`: exact - R, T-states, flags, stops where
  run() would: slice end, an interrupt, I/O, its own bytes; the boards
  copy their sprite tables with LDIR every frame, ~15 % of the cycles),
  the unprefixed decoder inlined into run(), small helpers forced inline
  (GCC had left fetch16 / wr16 / cp8 out of line); `steady()` compares
  its inputs with memcmp; Galaga main-CPU waits 0540, 01BF, 03D8 as exact
  passes. Host instructions a frame (callgrind, first 8,000 frames):
  Galaga emulation 601K -> 442K, rendering 1,272K -> 217K; Dig Dug
  emulation 522K -> 414K, rendering 3,272K -> 745K. PC wall time in play
  (best of 5, us a frame): Galaga emu 68 -> 59 (p99 104 -> 95), render
  107 -> 22 (p99 131 -> 34), emu + render p99 230 -> 126, worst 273 ->
  150; Dig Dug emu 56 -> 52, render 204 -> 47, p99 303 -> 137. The busy
  frames' emulation is now most of the cost and only ~10 % cheaper: the
  Z80 interpreter itself (~45 host instructions a Z80 instruction on the
  PC) is what's left. On the board the bigger cost was sending: Galaga's
  stars change ~140 of 288 lines every frame but ~10 % of the pixels, so
  v0.14.3 sends changed segments, not whole lines (see Drawing).
- Drawing (v0.14.3, `main/line_diff.h`, used by `game_runner` and by
  `arcade_host --panel WxH`): each game line is cut into up to 8 segments
  (even widths, 28 pixels for 224-wide games), each with a 32-bit hash of
  what the panel shows (rotate-multiply per pixel pair + a final mix; 0 =
  never sent); only changed segments go, runs one unchanged segment apart
  joined, and lines whose changed part is the same columns are packed into
  strips of 8 rows. 16-bit segment hashes (+ a line hash) were tried and
  missed a change within 10 frames of Galaga - never go back to short
  hashes. RAM: height x segments x 4 (Galaga 9 KB; v0.14's line hashes 2
  KB). `arcade_host --panel 320x480` draws exactly as the firmware does
  (scaling, skipped pictures, pictures cut short at random) onto a panel in
  memory and checks every finished picture against a full redraw: all 16
  games x 240x320 / 320x480, 6,000 frames each, 0 wrong pixels. Pixels
  sent a picture on 320x480, segments vs whole changed lines: Galaga 13.5K
  vs 62.9K (~100 blocks a picture), Dig Dug 1.7K vs 7.8K, Pac-Man 3.0K vs
  12.3K, Rally-X 14.8K vs 29.4K, Star Wars 9.1K vs 36.1K, Defender 6.8K vs
  26.4K, Robotron 5.1K vs 25.7K. Run it after any change to the drawing.
  Faster, same pushes (2026-10-24): `diff` hashes two segments side by
  side (two multiply chains) with a pixel pair as one 32-bit load when the
  line is word-aligned (the same hash values - `seg_hash` is the plain
  form, used for an odd tail); a scaled row is built 4 pixels an
  iteration and the line's further rows (scaled up) are memcpy'd from the
  one above. Xtensa: hash ~4 -> ~2.75 instructions a pixel, the scaled
  copy ~6 -> ~4.5 a panel pixel, the second row of a doubled line ~6 ->
  ~0.6. Checked with a copy of arcade_host running main's drawer beside
  it on the same lines: every push (place, size, pixels' CRC) the same,
  2.55M pushes over all 26 games x 240x320 / 320x480, 3,000 frames each;
  `--panel` 0 wrong.
- Galaga: emulated CPUs, WSG, tiles / sprites / 05XX stars (the 256 stars
  of the LFSR period found once, placed per frame); 54XX explosions are our
  own noise + band-pass model (`emu/sound/namco54.*`). Speedups: sub / sound
  main loops, seven RAM-poll loops, and the main CPU's background loop
  (skipped once its inputs stay unchanged 6000 cycles - its steady count
  restarts at every VBLANK IRQ, so ~6K cycles of it run each frame in
  play; skipping it sooner would change R and so the game), plus three
  exact-pass waits (0540, 01BF, 03D8). Cycles a frame 152K -> ~31K
  attract, ~55K play (busy wave entries ~64K).
- Dig Dug: same board; playfield from the map ROM, text layer, sprites
  (x wraps at 256, kept off the score lines), EAROM high scores (not kept
  over power-off). Speedups: the three main loops. ~22K attract, ~52K play.
- `core/`: plain C++ firmware logic, host-tested: `rom_store` (the user's
  zips as they are in the `roms` partition, append-only, directory sector)
  and `usb_import` (protocol of the web page's ROM loader, "CYDA ..."
  lines on UART0 at 115200). `components/arcade_core` builds emu/ + core/.
- Firmware (`main/`): `menu` (touch + controller, see Touch menus), `game_runner`
  (sound ring paces the game; changed line segments only; skips drawing when
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
  Flash is read QIO at 80 MHz on every board (v0.15.2; until then the
  ESP32 boards ran DIO at 40 MHz - esp32dev's default, and CI's merge forced
  40m into the bootloader). Code that misses the cache and ROM regions kept
  in flash come through it, so it sets every game's speed. Set in
  sdkconfig.defaults (bootloader, QIO switch) AND platformio.ini
  `board_build.flash_mode` / `f_flash` (the app image header: the ESP32
  takes its flash clock from there at start-up); CI merges with `keep` and
  prints both headers. The log's "Flash: QIO, 80 MHz" line at each start
  shows what a board got.
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
  a MAME sample zip, `--demo-sounds on|off` = set_demo_sounds, `--hash FILE` = each frame's picture and audio CRC,
  to show a speed-up changed nothing: compare the files of the old and the
  new build; `--panel WxH` = the firmware's drawing checked, see Drawing). Run them after any
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
  WAI, the IRQ wait after CLI / TAP). Undocumented opcodes run as 2-cycle
  no-ops.
- `qix_tests.cpp`: the Qix board on hand-assembled programs (video RAM
  latch / window, palette and picture, both FIRQs, the scanline, the
  VSYNC IRQ, the sound command / DAC / volume, the first boot's restart)
  and that idle skipping leaves their results as they are without it.
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
- Pushing (Tom, 2026-10-20: "Why does it take you so long to post the
  binaries?" - a work-branch run and then the same run on main took
  30-40 minutes): push firmware changes straight to main; CI deploys the
  flasher only when the host tests AND every board build pass, so a broken
  commit never reaches it - fix it at once. The board builds run alongside
  the host tests. Still ~11 minutes (Tom: "WAY too long ... my other
  repos post super quick"): the host tests compiled all of emu/ ~26 times
  on one core. Now emu/ is built once per flavour (-O2, sanitizers) as a
  library on every core and each test links it; each board keeps its last
  `.pio/build` in the Actions cache (fresh when sdkconfig / platformio.ini
  / a CMake file / a dependency pin changes), so a push recompiles only
  what changed. Never go back to compiling emu/ per test program.
- Exception (Tom, 2026-10-20, the new-games batch: Bubbles, Galaxian,
  Amidar, Missile Command, Battlezone, Zaxxon - "Don't bother updating the
  online web flasher until you're done"): that batch is built on the
  branch `work/newgames` and goes to main (and so the flasher) once, when
  every game is done - v0.17.0 (with the Space Invaders shot). Spy Hunter and Rolling Thunder are dropped (Tom,
  2026-10-20: too big for the 4 MB ESP32 boards - Spy Hunter's 68000 music
  board, Rolling Thunder's 1.2 MB of ROM).
- Every push to main: CI builds every env with `custom_firmware_name`,
  merges each into one image at 0x0 (esptool merge_bin) and publishes
  `web/index.html` + `tools/make_site.py`'s manifests on GitHub Pages:
  https://tomtombombadil.github.io/CYD-Classic-Arcade/

## Versions and releases
- Semantic versioning from `VERSION` (started 0.1.0, 2026-10-14). A fix
  bumps Z, a feature (a game, a setting, a screen) bumps Y. Bump VERSION in
  every push to main that changes the firmware.
- Don't publish a release unless Tom asks for one.
