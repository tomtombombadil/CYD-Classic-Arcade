# CYD Classic Arcade

Classic arcade games emulated on ESP32 "Cheap Yellow Display" (CYD) boards,
played with a Bluetooth game controller and heard through the board's
speaker connector.

**Work in progress.** Right now the firmware is a controller test: it pairs
Bluetooth controllers and shows their buttons. The emulators come next
(`docs/SPEC.md`).

## Games

Playable: Pac-Man, Space Invaders, Rally-X, Donkey Kong, Galaga, Dig Dug,
Robotron: 2084.

Planned: Q\*bert, Asteroids, Qix, Star Wars; later perhaps Defender,
Stargate, Spy Hunter and Rolling Thunder.

## ROMs - bring your own

**No game ROMs are included, and none ever will be.** You copy in the ROM
files of games you own, as standard MAME ROM sets (the `.zip` files as they
are). Which sets, and how they're found: [`docs/ROMS.md`](docs/ROMS.md).

## Boards

The same boards as [CYD Classic Games](https://github.com/tomtombombadil/CYD-Classic-Games):
2.8" ILI9341 / ST7789 (ESP32-2432S028), 3.2" ST7789, 3.5" ST7796 and 4.0"
ST7796 (ESP32-32E display boards), all resistive touch. Not yet tested on
hardware with this firmware.

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
