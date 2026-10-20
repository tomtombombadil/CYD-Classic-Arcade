# CYD Classic Arcade

Classic arcade games emulated on ESP32 "Cheap Yellow Display" (CYD) boards,
played with a Bluetooth game controller and heard through the board's
speaker connector.

**Work in progress.** Right now the firmware is a controller test: it pairs
Bluetooth controllers and shows their buttons. The emulators come next
(`docs/SPEC.md`).

## Games

Playable: Pac-Man, Space Invaders, Rally-X, Donkey Kong, Galaga, Dig Dug,
Ms. Pac-Man, Mr. Do!, Burger Time, Q\*bert, Asteroids, Tempest, Robotron:
2084, Defender, Bubbles, Qix, Star Wars.

Later perhaps: Stargate, Spy Hunter and Rolling Thunder.

## ROMs - bring your own

**No game ROMs are included, and none ever will be.** You copy in the ROM
files of games you own, as standard MAME ROM sets (the `.zip` files as they
are). Which sets, and how they're found: [`docs/ROMS.md`](docs/ROMS.md).

## Boards

The same boards as [CYD Classic Games](https://github.com/tomtombombadil/CYD-Classic-Games):
2.8" ILI9341 / ST7789 (ESP32-2432S028), 3.2" ST7789, 3.5" ST7796 and 4.0"
ST7796 (ESP32-32E display boards), all resistive touch - all tested.

Also the Freenove ESP32-S3 Display boards (2.8" ILI9341 FNK0104B and 4.0"
ST7796 FNK0104S, capacitive touch, 8 MB PSRAM) - not yet tested. The
ESP32-S3 has Bluetooth LE only: Xbox Wireless and other BLE controllers
pair; classic-Bluetooth ones (PlayStation, Switch Pro, many 8BitDo modes)
don't. ROM zips go in over USB from the web page (with the board's Settings > USB
Transfer page open), or from an SD card.

## HOW TO INSTALL

Go the the web flasher at:
https://tomtombombadil.github.io/CYD-Classic-Arcade/
and follow the onscreen instructions. It's just like many of the other web-flashers for ESP32-based devices.

## Using it

Touch the screen or use a controller. Tap a game to play it; the gear at
the top right opens Settings (volume, button layout, screen rotation,
Controller Test, touch calibration, how to add games). The line at the
bottom shows which controllers are connected. In a game: hold Select and
press Start twice (or press Home, or touch and hold the screen for a
second) to come back. Resistive screens ask you to tap four corner arrows
once, the first time.

## Controllers

Anything [Bluepad32](https://github.com/ricardoquesada/bluepad32) supports:
PlayStation 3/4/5, Xbox Wireless, Switch Pro, 8BitDo, most Android
controllers. Put the controller in pairing mode near the board; it is
remembered and reconnects by itself after that.

## Building

VS Code + PlatformIO: open the folder, pick your board's `env:` in the
status bar, Build / Upload. The first build downloads Bluepad32, BTstack
and LovyanGFX (pinned versions) into `external/`.

## License

Our code is MIT (`LICENSE`). The built firmware includes BTstack, which is
free for **non-commercial use only** - see
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md). Game titles are
trademarks of their owners; this project is not affiliated with them.
