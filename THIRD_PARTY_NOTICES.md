# Third-party notices

CYD Classic Arcade's own code is MIT (see LICENSE). The built firmware also
contains the following, downloaded at build time by `tools/fetch_deps.py`
(not stored in this repository):

| Component | Use | License |
|---|---|---|
| [Bluepad32](https://github.com/ricardoquesada/bluepad32) | Bluetooth game controller support | Apache 2.0 |
| [BTstack](https://github.com/bluekitchen/btstack) | Bluetooth host stack under Bluepad32 | BlueKitchen license: free for non-commercial use; commercial use needs a license from BlueKitchen GmbH |
| [LovyanGFX](https://github.com/lovyan03/LovyanGFX) | display and touch driver | FreeBSD (2-clause BSD) |
| [ESP-IDF](https://github.com/espressif/esp-idf) | SDK | Apache 2.0 |

**Because of BTstack, the firmware files built from this project may be
used for non-commercial purposes only.** The source code in this
repository stays MIT.

From [CYD Classic Games](https://github.com/tomtombombadil/CYD-Classic-Games)
(same author, MIT): the board files in `main/boards/` and the DEFLATE
decoder in `emu/util/`.

Freenove ESP32-S3 Display boards (`main/boards/freenove_s3_display.hpp`):
the pins come from Freenove's published schematics and sample setups
(CC BY-NC-SA) - facts only, no code taken. The ES8311 codec's start-up
register values (`main/audio_out.cpp`) follow the ES8311 user guide, the
same values Espressif's Apache 2.0 driver writes; the code is our own.

## Game ROMs

None are included. Arcade game ROMs are copyrighted by their makers; users
supply their own. Game titles are named only to say which ROM sets the
emulator reads; they are trademarks of their owners and this project is not
affiliated with them.
