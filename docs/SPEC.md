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
| Galaga | 3x Z80 3.072 MHz | Namco 51xx (modelled), 54xx (our noise model), 05xx stars, wavetable sound | 224x288 portrait | done: idle-loop skipping brings 152K CPU cycles a frame to ~55K in play |
| Dig Dug | 3x Z80 3.072 MHz | Namco 51xx/53xx (modelled), wavetable sound, playfield from a map ROM, EAROM | 224x288 portrait | done: ~52K cycles a frame in play |
| Donkey Kong | Z80 3.072 MHz | i8035 sound CPU + DAC, analog effects | 224x256 portrait | medium |
| Q\*bert | 8088 5 MHz | 6502 sound CPU + RIOT + DAC, Votrax SC-01 speech (our model), knocker (our model) | 240x256 portrait | done: idle-loop skipping brings 81K 8088 cycles a frame to ~7-12K |
| Asteroids | 6502 1.5 MHz | vector display (DVG), analog sound (our own models) | 320x240 landscape (lines drawn row by row from a segment list) | done: idle-loop skipping, ~0.04 ms a frame on the PC |
| Tempest | 6502 1.5 MHz | colour vector display (AVG), math box, 2 POKEY (sound, spinner, buttons), EAROM | 320x240 landscape, the nearly square AVG window at its true shape in the middle (lines drawn row by row from a segment list) | done: ~0.06 ms a frame on the PC (the game keeps its CPU busy) |
| Battlezone | 6502 1.5 MHz | monochrome vector display (AVG with a clip window), math box, POKEY (sound, treads, buttons), analog sound (our own models of the engine, shell and explosion circuits) | 320x221 landscape, the 580 x 400 AVG window at its true shape, red over the top fifth and green below (the cabinet's overlay) | done: waits skipped (exact), ~0.05 ms a frame on the PC |
| Rally-X | Z80 3.072 MHz | Namco wavetable sound (as Pac-Man), analog crash "bang", scrolling + radar | 288x224 landscape | easy (after Pac-Man) |
| Ms. Pac-Man | Z80 3.072 MHz | the Pac-Man board + Midway's aux board (scrambled ROMs laid over the Pac-Man program, a latch set / cleared by trap addresses) | 224x288 portrait | done: as Pac-Man |
| Mr. Do! | Z80 4.1 MHz | 2 x SN76489, two tile layers (one scrolls) + sprites, a protection PAL | 192x240 portrait | done: ~Pac-Man's cost |
| Qix | 2x 6809 1.25 MHz | 6802 sound CPU + DAC, 256x256 8-bit bitmap (64 KB video RAM, two 32 KB blocks), 6845 timing | 240x256 portrait (lines 8-247 of 256) | done: the video CPU polls the scanline all the time (~1.4x Pac-Man's host instructions a frame); ~104 KB of RAM with the ROMs |
| Burger Time | DECO CPU-7 (6502 with scrambled opcodes) 1.5 MHz | 6502 sound CPU + 2 AY-3-8910, a band-pass on one channel; characters, 8 sprites, background map from ROM, palette RAM | 240x240 portrait | done: VBLANK waits and the sound CPU's idle loop skipped (~6-9K main CPU cycles a frame in play) |
| Star Wars | 6809 1.5 MHz + matrix processor (PROM microcode) | colour vector generator (AVG), 6809 sound CPU + RIOT, 4 POKEY, TMS5220 speech (our LPC synthesiser; the voices are LPC data in the sound ROMs) | 320x240 landscape (lines drawn row by row from a segment list) | done: ~1.0M host instructions a frame (2x Pac-Man); machine ~51 KB + 72 KB of ROM |
| Robotron: 2084 | 6809 1 MHz | SC1 blitter, 6808 sound CPU + DAC, 304x256 4-bit bitmap (38 KB) | 292x240 landscape | done: idle loops skipped (exact), ~104 KB of RAM with the ROMs |
| Defender | 6809 1 MHz | 6808 sound CPU + DAC, ROM banked at C000 | 292x240 landscape | done: the stick reverses / thrusts by the ship's facing |
| Stargate (stretch) | 6809 1 MHz | 6800 sound CPU + DAC | 292x240 landscape | medium-hard |
| Spy Hunter (stretch) | Z80 | Z80 + 2 AY-8910, 68000 music board | 480x512 portrait | probably too big |
| Rolling Thunder (stretch) | 2x 6809 1.5 MHz + HD63701 MCU | YM2151 FM + Namco CUS30 + sampled voices, 3 tile layers + sprites, 1.2 MB of ROMs | 288x224 landscape | very hard: ROMs bigger than the ROM partition, FM synthesis |

## 3. Sound - where it comes from

There is no recorded sound in these ROMs to "pull out". Each machine made
its sound one of three ways, and we do what the machine did:

1. **Sound chips driven by the game program** - Pac-Man, Ms. Pac-Man,
   Galaga, Dig Dug (Namco wavetable: the waveforms are in a 256-byte PROM
   in the ROM set, the game program writes pitch and volume 60 times a
   second); Mr. Do! (two SN76489s: square waves and noise); Burger Time
   (two AY-3-8910s, programmed by a sound 6502); Spy Hunter
   (AY-8910s). We emulate the chip; the sound is exact.
2. **A sound CPU with its own program and a DAC** - Donkey Kong's music
   (i8035), Q\*bert's effects (6502), Defender / Stargate (all of it,
   6800), Qix (all of it, 6802). We run that CPU's program from the ROM
   set; it writes the DAC and the sound is exact. Q\*bert's speech goes through a Votrax SC-01
   chip whose own ROM isn't in the common sets: the sound CPU's program
   (and so the phoneme timing) runs as on the board, and our own small
   source-filter model voices the phonemes (Q\*bert's swearing is random
   phonemes anyway).
3. **Analog circuits** - Space Invaders and Asteroids (all of it), Donkey
   Kong's walk / jump / stomp, Q\*bert's knocker (a solenoid hitting the
   cabinet!), Galaga's explosion filters. The ROM only switches these
   on and off. We play our own small sound models of those circuits
   (noise + tones + envelopes), built into the firmware. No sample files
   (Tom: the ROM zip is all a player ever needs).

Output: the ESP32's DAC on GPIO26 into the board's own amplifier and the
speaker connector (a plain 8-ohm speaker, nothing added). 24,000 samples a
second, 8-bit, streamed by DMA (`main/audio_out.*`); each machine mixes its
voices to 16-bit and the driver applies the volume.

ESP32-S3 boards (Freenove ESP32-S3 Display, v0.11.0): no DAC on the S3, so
16-bit samples go by I2S to the board's ES8311 codec at 24,000 a second
(MCLK 256 x, codec in slave mode, its DAC volume at 0 dB - our volume
setting scales the samples, same scale as the DAC path), then its own amp
(enable GPIO1, low = on, switched off after 0.2 s of silence as on the
ESP32-32E boards).

## 4. Controls

Bluetooth controllers through Bluepad32 (PlayStation 3/4/5, Xbox, Switch
Pro, 8BitDo, most Android pads). Every controller maps to one layout
(`main/pad.h`): d-pad (the left stick counts too), A B X Y, L, R, Start,
Select, Home. Per game: Start = start, Select = coin, A = fire / jump,
others as the game needs (Defender's thrust / smart bomb / hyperspace,
Asteroids' thrust / hyperspace; Tempest: A fires, B = Superzapper, the
spinner = the left stick's X at a speed in proportion or the d-pad at a
steady speed; Robotron's right stick fires; Battlezone: the two sticks'
Y are the two treads, or the d-pad drives - up / down both treads,
left / right turn on the spot). Home = the emulator's menu
(pause, volume, quit). Two controllers can be paired (`CONFIG_BLUEPAD32_MAX_DEVICES`
= 2).

## 5. Screen

Games render line by line into small strips sent to the panel by DMA - a
whole 224x288 frame buffer (129 KB) doesn't fit beside Bluetooth. Portrait
games fill the width of a 240x320 panel with an 8-pixel border; the
320x480 panels scale up. Landscape games (Asteroids, Defender, Stargate,
Tempest, Battlezone) turn the panel at game start.

## 6. ROM storage

ROMs are read in place from flash, never copied into RAM: the `roms`
partition (1.7 MB, `partitions.csv`) holds the recognised ROM chips,
memory-mapped. Getting them there: from the SD card's `CYD-Classic-Arcade`
folder at power-on, or over USB from the web page (Web Serial), on every
board.

## 7. Milestones

1. **Controller test** (v0.1.0): pairing, buttons on screen, free RAM with
   Bluetooth on, frame push time, DAC tone. Proves the risky parts.
2. Pac-Man on the PC (host runner: PNG frames + WAV), then on the board.
3. ROM loading (SD / USB) and the game menu.
4. Space Invaders, Galaga, Dig Dug, Donkey Kong, Q\*bert, Asteroids.
5. Stretch: Defender, Stargate; Spy Hunter only if there's room.
