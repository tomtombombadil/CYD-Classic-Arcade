# CYD Classic Arcade - specification

## 1. What it is

An arcade emulator for ESP32 "Cheap Yellow Display" boards (the same five
boards as CYD Classic Games). The player uses a **Bluetooth game
controller**; the touch screen only drives menus and settings. ROMs come
from the user (`docs/ROMS.md`); none are in this repository.

Not a port of Galagino or MAME: every emulator here is our own code (MIT).
MAME's source and public hardware documents are read for facts (memory
maps, chip behaviour, timings, ROM checksums) - never copied.

## 2. Games

| Game | Main CPU | Other chips | Screen (as played) | Effort |
|---|---|---|---|---|
| Pac-Man | Z80 3.072 MHz | Namco 3-voice wavetable sound | 224x288 portrait | first |
| Space Invaders | 8080 2 MHz | analog sound boards | 224x256 portrait | easy |
| Galaga | 3x Z80 3.072 MHz | Namco 51xx/54xx customs, wavetable sound | 224x288 portrait | medium-hard |
| Dig Dug | 3x Z80 3.072 MHz | Namco 51xx/53xx customs, wavetable sound, playfield | 224x288 portrait | medium (after Galaga) |
| Donkey Kong | Z80 3.072 MHz | i8035 sound CPU + DAC, analog effects | 224x256 portrait | medium |
| Q\*bert | 8088 5 MHz | 6502 sound CPU + DAC, Votrax SC-01 speech | 240x256 portrait | medium-hard |
| Asteroids | 6502 1.5 MHz | vector display (DVG), analog sound | 4:3 landscape | medium |
| Rally-X | Z80 3.072 MHz | Namco wavetable sound (as Pac-Man), analog crash "bang", scrolling + radar | 288x224 landscape | easy (after Pac-Man) |
| Qix | 2x 6809 1.25 MHz | 6802 sound CPU + DAC, 256x256 8-bit bitmap (64 KB video RAM) | 240x256 portrait | medium: light CPUs, but 64 KB of RAM |
| Defender (stretch) | 6809 1 MHz | 6800 sound CPU + DAC | 292x240 landscape | medium-hard |
| Stargate (stretch) | 6809 1 MHz | 6800 sound CPU + DAC | 292x240 landscape | medium-hard |
| Spy Hunter (stretch) | Z80 | Z80 + 2 AY-8910, 68000 music board | 480x512 portrait | probably too big |

## 3. Sound - where it comes from

There is no recorded sound in these ROMs to "pull out". Each machine made
its sound one of three ways, and we do what the machine did:

1. **Sound chips driven by the game program** - Pac-Man, Galaga, Dig Dug
   (Namco wavetable: the waveforms are in a 256-byte PROM in the ROM set,
   the game program writes pitch and volume 60 times a second); Spy Hunter
   (AY-8910s). We emulate the chip; the sound is exact.
2. **A sound CPU with its own program and a DAC** - Donkey Kong's music
   (i8035), Q\*bert's effects (6502), Defender / Stargate (all of it,
   6800). We run that CPU's program from the ROM set; it writes the DAC
   and the sound is exact. Q\*bert's speech goes through a Votrax SC-01
   chip - emulated from its own internal ROM (in non-merged sets) if it
   fits the CPU budget, else the speech is left out.
3. **Analog circuits** - Space Invaders and Asteroids (all of it), Donkey
   Kong's walk / jump / stomp, Q\*bert's knocker (a solenoid hitting the
   cabinet!), Galaga's explosion filters. The ROM only switches these
   on and off. Two ways, in this order: the user's MAME **sample** zip
   (WAV recordings, optional, `docs/ROMS.md`), else our own small sound
   models (noise + tones + envelopes) that sound close.

Output: the ESP32's DAC on GPIO26 into the board's own amplifier and the
speaker connector (a plain 8-ohm speaker, nothing added). 24,000 samples a
second, 8-bit, streamed by DMA (`main/audio_out.*`); each machine mixes its
voices to 16-bit and the driver applies the volume.

## 4. Controls

Bluetooth controllers through Bluepad32 (PlayStation 3/4/5, Xbox, Switch
Pro, 8BitDo, most Android pads). Every controller maps to one layout
(`main/pad.h`): d-pad (the left stick counts too), A B X Y, L, R, Start,
Select, Home. Per game: Start = start, Select = coin, A = fire / jump,
others as the game needs (Defender's thrust / smart bomb / hyperspace,
Asteroids' thrust / hyperspace). Home = the emulator's menu (pause,
volume, quit). Two controllers can be paired (`CONFIG_BLUEPAD32_MAX_DEVICES`
= 2).

## 5. Screen

Games render line by line into small strips sent to the panel by DMA - a
whole 224x288 frame buffer (129 KB) doesn't fit beside Bluetooth. Portrait
games fill the width of a 240x320 panel with an 8-pixel border; the
320x480 panels scale up. Landscape games (Asteroids, Defender, Stargate)
turn the panel at game start.

## 6. ROM storage

ROMs are read in place from flash, never copied into RAM: the `roms`
partition (1.7 MB, `partitions.csv`) holds the recognised ROM chips and
samples, memory-mapped. Getting them there: from an SD card's `roms` folder
on boards with a usable slot; over USB from the web page (Web Serial) on
the 2.8" boards.

## 7. Milestones

1. **Controller test** (v0.1.0): pairing, buttons on screen, free RAM with
   Bluetooth on, frame push time, DAC tone. Proves the risky parts.
2. Pac-Man on the PC (host runner: PNG frames + WAV), then on the board.
3. ROM loading (SD / USB) and the game menu.
4. Space Invaders, Galaga, Dig Dug, Donkey Kong, Q\*bert, Asteroids.
5. Stretch: Defender, Stargate; Spy Hunter only if there's room.
