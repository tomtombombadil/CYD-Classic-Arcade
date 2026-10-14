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
  Defender, Stargate, Spy Hunter.
- Tom's ROM zips (in roms/, never committed): pacman, invaders, galaga,
  digdug (rev 2), dkongo (Donkey Kong US set 2), asteroid (= today's
  asteroid2, rev 2, no DVG PROM), qbert (no sc01a.bin - no Votrax ROM),
  defendg (= defenderg, Green label), qix (Rev 2), rallyx. No Namco MCU
  ROMs (51xx/53xx/54xx) and no sample zips yet. Support these revisions.
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
- Rendering is line by line into small DMA strips; no full frame buffer
  (a 224x288x2 frame is 129 KB - not with Bluetooth on).
- Sound: `main/audio_out.*`, 24 kHz, 8-bit DAC by DMA, volume
  (v/100)^2, default 50 %. Machines fill signed 16-bit mono.
- Flash: 4 MB = app 2.25 MB + `roms` data partition 1.7 MB (ROMs read in
  place, memory-mapped). No OTA - the web flasher is the update path.
- `version_gen.h` is written by `tools/version.py` (ESP-IDF components
  don't share -D flags).

## Testing
- Host: `tools/host_tests/` (Z80 exerciser etc.) and `tools/host/`
  (`arcade_host`: runs a game headless with scripted inputs, writes PNG
  frames and a WAV). Run them after any emulator change.
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
