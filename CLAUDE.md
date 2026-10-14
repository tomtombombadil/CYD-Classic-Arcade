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
- Menu help text at the bottom wraps to the screen (`screen::text_box`),
  in the small font on every panel (Tom: it ran off the right edge).
- Screen (Tom, 2026-10-14): portrait with the USB port at the TOP by
  default (the board stands up, cable out the top; rotation 2); menu
  setting "USB Port: Top / Bottom" (NVS usb_top).
- 4-way games (Pac-Man; `games::Info::four_way`): a d-pad diagonal counts
  as the direction pressed last (Tom found Pac-Man hard to steer).
- Tom, v0.2.0 on the 2.8" ST7789: ROMs over USB worked, Pac-Man and Space
  Invaders "played perfectly". 4.0" log: Pac-Man emu ~5.7 ms + draw ~6-8
  ms a frame (of 16.5), every frame drawn.
- **ROM zips only, no sample files** (Tom, 2026-10-14: on handhelds the
  zip is all a player ever needs). Analog sounds are always our own
  models in the firmware. `emu/sound/samples.*` stays for the PC tool
  only (`arcade_host --samples`, to compare a model with a recording).
- Home isn't on every controller (Q36): leaving a game also works with
  Select + Start twice.
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
- `emu/games.*`: the game table shared by firmware and PC tools (machine
  face, screen, orientation, controller -> panel buttons: Select = coin,
  Start = 1P start, L = 2P start, A = fire). Adding a game = machine +
  entry there.
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
- Sound: `main/audio_out.*`, 24 kHz, 8-bit DAC by DMA, volume
  (v/100)^2, default 50 %. Machines fill signed 16-bit mono.
- Flash: 4 MB = app 2.25 MB + `roms` data partition 1.7 MB (ROMs read in
  place, memory-mapped). No OTA - the web flasher is the update path.
- `version_gen.h` is written by `tools/version.py` (ESP-IDF components
  don't share -D flags).

## Testing
- Host: `tools/host_tests/` (Z80 exerciser, 8080 tests 8080PRE / 8080EXM /
  TST8080 / CPUTEST, unit tests) and `tools/host/` (`arcade_host`: runs a
  game headless with scripted inputs, writes PNG frames and a WAV; a game
  table in it, `--samples` for a MAME sample zip). Run them after any
  emulator change. Test programs that aren't ours (ZEX, the 8080 tests) are
  downloaded into `build/` with pinned SHA-256s, never committed.
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
