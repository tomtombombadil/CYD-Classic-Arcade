# Memory budget

Tom, 2026-10-20: "Memory allocation has been the source of almost ALL of
our problems ... Calculate it. That's our baseline. Anything that is
optional is ... only loaded when it is needed ... the memory requirements
for each game ... are fixed. We should know exactly what they are ... Do
the math."

This file is that math. Every number says where it comes from. Numbers
marked **(board)** are read from the Log of a board running v0.15.6 or
later (`main/memlog.*`); **(build)** from CI's build log; **(xtensa)** from
the ESP32 compiler (`sizeof`); **(code)** from the source.

## 1. What a game needs (fixed, exact)

A game takes, from the game area (`main/arena.*`), in this order:

1. its machine object - `sizeof` the machine class **(xtensa)**, plus
   Qix's two 32 KB video RAM halves (two separate blocks);
2. the runner's buffers **(code, game_runner.cpp)**: a strip of 8 output
   lines (out_w x 8 x 2 bytes), one game line (width x 2), the x map
   (out_w x 2), the segment hashes (height x segments x 4), the sound
   buffer (2 KB);
3. optional: the snapshot for drawing on core 0 (tile games' VideoState,
   vector games' segment copy) - without it the game draws between frames
   on core 1, slower but working;
4. optional: ROM regions in RAM - without room they are read from flash
   (memory-mapped scratch area), slower but working; the unpacking buffer
   (the biggest ROM) is taken only while loading.

Each block costs an 8-byte head and rounds up to 8 bytes. Measured with
`tools/host_tests` style code: machine sizes from the Xtensa compiler
(they match the board's "Machine 23 KB" for Galaga, 21 KB for Donkey
Kong), buffers from the runner's own arithmetic. KB = 1024 bytes.

| Game | Machine | Extra | Buffers 240x320 / 320x480 | **Minimum 240x320 / 320x480** | Snapshot (opt.) | ROM (opt.) | Biggest ROM |
|---|---|---|---|---|---|---|---|
| Space Invaders | 9.2 | - | 14.7 / 16.1 | **23.9 / 25.3** | - | 8.0 | 2 |
| Pac-Man | 14.4 | - | 15.7 / 17.1 | **30.1 / 31.5** | 2.0 | 24.8 | 4 |
| Burger Time | 17.6 | - | 14.2 / 15.6 | **31.9 / 33.3** | - | 52.0 | 4 |
| Missile Command | 18.3 | - | 14.4 / 16.0 | **32.8 / 34.3** | 15.5 | 12.0 | 2 |
| Rally-X | 18.6 | - | 15.0 / 16.8 | **33.7 / 35.5** | 2.6 | 21.1 | 4 |
| Q*bert | 19.5 | - | 14.7 / 16.1 | **34.2 / 35.6** | 1.4 | 68.0 | 8 |
| Donkey Kong | 21.3 | - | 14.7 / 16.1 | **36.0 / 37.4** | 1.8 | 32.8 | 4 |
| Galaga | 23.8 | - | 15.7 / 17.1 | **39.5 / 40.9** | 3.6 | 37.0 | 4 |
| Asteroids | 25.5 | - | 15.8 / 17.7 | **41.3 / 43.2** | 20.0 | 8.0 | 2 |
| Battlezone | 27.7 | - | 15.2 / 17.7 | **42.9 / 45.4** | 20.0 | 16.0 | 2 |
| Tempest | 29.2 | - | 15.8 / 17.7 | **45.0 / 46.9** | 20.0 | 24.0 | 4 |
| Dig Dug | 31.9 | - | 15.7 / 17.1 | **47.6 / 49.0** | 1.8 | 55.0 | 4 |
| Mr. Do! | 34.3 | - | 14.1 / 15.5 | **48.4 / 49.8** | - | 56.1 | 8 |
| Ms. Pac-Man | 34.8 | - | 15.7 / 17.1 | **50.5 / 51.9** | 2.0 | 34.8 | 4 |
| Robotron | 51.9 | - | 15.2 / 17.0 | **67.2 / 68.9** | - | 52.0 | 4 |
| Defender | 51.9 | - | 15.2 / 17.0 | **67.2 / 68.9** | - | 30.0 | 4 |
| Bubbles | 51.9 | - | 15.2 / 17.0 | **67.2 / 68.9** | - | 52.0 | 4 |
| Star Wars | 52.6 | - | 15.8 / 17.7 | **68.4 / 70.2** | 27.6 | 72.0 | 16 |
| Zaxxon | 20.8 | - | 14.7 / 16.1 | **35.5 / 36.9** | 1.2 | 104.5 | 8 |
| Qix | 8.1 | 64.0 (2 x 32) | 14.7 / 16.1 | **86.8 / 88.2** | - | 32.0 | 2 |

Missile Command (2026-10-20, Xtensa sizeof 18,752 bytes): the machine is
its 16 KB of RAM (work RAM and the picture) + the 6502's page tables (2
KB) + the POKEY; shown 266 x 240 on the 240x320 panels and 355 x 320 on
the 320x480 ones (strip 4,256 / 5,680 bytes, segment hashes 231 x 8 x 4 =
7,392); the snapshot copies its picture's RAM (0200-3FFF) + 8 pens,
15,888 bytes.

Battlezone (320 x 221) **(xtensa / code)**: machine 28,368 bytes (the
vector display 20,536 of it - 1,024 segments; the sound board 104, the
POKEY 132); buffers at 240x320: strip 320 x 8 x 2, line 640, x map 640,
segment hashes 221 x 8 x 4 = 7,072, sound 2,048 - 15,560 bytes with the
heads; at 320x480 (shown 463 x 320): strip 7,408, x map 928 - 18,136
bytes. Snapshot: 1,024 x 20 + 8. Most segments seen: 525 (attract mode),
449 (play). With everything in RAM: 42.9 + 20.0 + 16.0 = 78.9 KB at
240x320, 81.4 KB at 320x480 - inside the ~92 KB area.

The largest single block a game needs is its machine (Star Wars 52.6 KB,
Robotron / Defender / Bubbles 51.9) or one of Qix's 32 KB halves.

Zaxxon (2026-10-20, xtensa-esp32-elf-g++ `sizeof`): the games.cpp wrapper
21,344 bytes (zaxxon::Machine 21,328: Z80, RAM 4 KB + characters 1 KB +
sprites 256 B, the VideoState 1,192 B, characters decoded by column 4 KB,
the background tile cache 8 KB + 512 B of tags, palette 512 B); buffers
for 224 x 256 as Pac-Man's but 256 lines of hashes; snapshot 1,192 B. Its
ROM (104.5 KB in 6 regions: program 20, characters 4, background tiles
24, sprites 24, tile map 32, PROMs 0.5) can't all be in RAM: what doesn't
fit is read from the scratch flash (the renderer reads the tile map,
background and sprite ROMs every line).

## 2. The ESP32 boards' RAM

- Internal RAM usable by the heap: **(board)** "Heap: N KB internal in
  all" at each start.
- Static RAM (.data + .bss, fixed at build): v0.3.1 105.6 KB, v0.15.x
  121 KB **(build)**; symbols at DRAM addresses, v0.15.7: 88.8 KB in 895
  symbols (CI's list; the rest of PlatformIO's "RAM used" is IRAM and
  alignment); the biggest static symbols are listed in every CI
  build log ("Biggest static RAM symbols").
- Static RAM's biggest users **(build, v0.15.6, 3.5")**: BTstack's HCI
  ring buffer 22.3 KB, Bluepad32's g_devices 11.3 KB, the sound ring
  (`s_ring`, audio_out) 8.2 KB, the menu object (`menu::run` m) 7.9 KB,
  Bluepad32's arrControllers 6.8 KB, the log ring 4.1 KB, ROM store
  directory 3.9 KB, port_IntStack 3 KB, the USB loader session 2.2 KB.
  Moved out in v0.15.7 (used only now and then, borrowed while in use):
  the controller test's 14 KB screen-speed strips (game area) and the SD
  import's 4 KB copy buffer (heap, at start-up) - 18 KB more for games.
  Until v0.15.6 CI's list counted const tables in flash (fonts, POKEY's
  17-bit poly) as RAM; it now lists only symbols at DRAM addresses.
- v0.1.0 (controller test, Bluetooth on): 142 KB free, largest block
  108 KB **(board, Tom)**.
- v0.14.6, 3.2" at a game's start: ~77 KB free; v0.15.4, 3.5": 70 KB
  free (31 in the area + 39 outside) **(board)**.

## 3. What always runs (the baseline)

Measured on Tom's 3.5" ST7796 running v0.15.7 (2026-10-20, Settings > Log
photos; one Q36 controller; Galaga started and left). Heap = internal RAM
the allocator manages, in KB **(board)**.

| Step | Free | Biggest block | Cost |
|---|---|---|---|
| Heap in all | 190.5 | | |
| At app_main (ESP-IDF's own tasks and drivers before us) | 164.7 | 108.0 | 25.8 |
| After NVS | 163.9 | 108.0 | 0.8 |
| ui task (12 KB stack + TCB) | 151.6 | 108.0 | 12.3 |
| Bluetooth started (controller, BTstack, Bluepad32) | 123.6 | 108.0 | 28.0 |
| A controller connected | 118.5 | 108.0 | 5.1 |
| Screen + touch, ROM catalog | 118.5 | 108.0 | 0 |
| USB ROM loader (task 6 KB + UART driver, 4 KB RX) | 106.8 | 104.0 | 11.7 |
| Sound (audio task 3 KB + DAC DMA) | 98.9 | 96.0 | 7.9 |
| draw task (5 KB stack) | ~93.5 | | ~5.4 |
| Kept back for the system (`kSystemReserve`) | | | 12 |
| **Game area** | **81** (one block) | | |

Stack use, worst seen since start-up **(board)**: v0.15.7 after Galaga:
main / BTstack 3.5 of 8 KB, ui 5.6 of 12, usb 0.7 of 6, draw 1.0 of 5,
audio 0.7 of 3. v0.15.8 after Ms. Pac-Man, Mr. Do!, Galaga, Donkey Kong,
Dig Dug, Defender, Burger Time, Asteroids: main 3.5, ui 5.6, draw 1.2,
audio 0.7 - unchanged by seven more games. v0.16.0 after Ms. Pac-Man,
Pac-Man, Q*bert, Qix, Rally-X, Robotron, Space Invaders, Tempest: the
same (draw 1.0-1.2). v0.16.1 after Star Wars: main 3.5, ui 5.6, draw 0.8, audio
0.7. All 16 games measured: no game moved any stack's high-water mark.

v0.16.1, Star Wars on the 3.5" **(board)**: area 98 KB at the menu (no
controller yet), 93 KB at the game's start (one connected); machine 52
KB, area used 74, ROM 4 KB in RAM / 68 KB from flash; 61.4 frames, 57
pictures a second (emu 8.7 ms, worst 13.4). A controller connecting at
the menu took 5.1 KB and left 5.2 KB free outside the area - not enough
for a second one.

v0.16.2: ESP32 stacks cut to the most used + ~40 %: ui 12 -> 9 KB, draw 5
-> 3, audio 3 -> 2 (6 KB more for games; main / BTstack stays 8 - pairing
a new controller wasn't measured; the S3 keeps its sizes). Kept outside
the area: 7 KB + 6 KB for each controller slot still free (two slots), so
a controller connecting at the menu or in a game always has its 5.1 KB.
Expected on the 3.5" at a game's start with one controller: ~96 KB area
(Qix needs 88.2), 13 KB outside.

v0.16.0 on the 3.5" (USB loader only on its page) **(board)**: game area
92 KB in one block, 8 KB free outside (block 6.3). Area used / ROM in RAM
/ ROM from flash, KB: Qix 90 / 2 / 30 (machine with its video RAM 72 KB;
55.9 frames a second = its own 55.8 Hz), Tempest 90 / 24 / 0, Ms. Pac-Man
88 / 34 / 0, Robotron 84 / 16 / 36, Q*bert 73 / 36 / 32, Rally-X 59 / 21 /
0, Pac-Man 58 / 24 / 0, Space Invaders 33 / 8 / 0. Star Wars: "Can't load
the ROMs - 136021.214.1f missing" - not a missing ROM: its regions going
to flash are unpacked through a buffer the size of their biggest ROM (16
KB), taken from the area after the regions kept in RAM had filled it,
else from the ~8 KB outside - it fit neither, and the loader reported the
untouched status list (Missing = 0). v0.16.1: the ROM budget keeps room in
the area for that buffer, and a failed buffer says "Out of memory".

v0.15.8 on the 3.5" (USB loader stopped during games) **(board)**: game
area 92 KB in two blocks (88 + 4; 80 KB at the menu with the loader
running), 11 KB free outside it (blocks 5.4 + 2.8 KB) - enough for
Bluetooth, every game ran. Area used / ROM in RAM / ROM from flash, KB:
Ms. Pac-Man 88 / 34 / 0, Dig Dug 89 / 39 / 16, Galaga 81 / 37 / 0, Mr.
Do! 73 / 24 / 32, Donkey Kong 71 / 32 / 0, Asteroids 71 / 8 / 0. Speeds:
all 59.5-64 frames and 58-61 pictures a second.

With the 81 KB area every game's minimum (section 1, 320x480) fits but
Qix's 88.2 KB. Galaga used 65 of 81 KB (machine 23, ROM 21 KB in RAM, 16
from flash) and ran 60.9 fps, 60 pictures a second, with 9.5 KB free
outside the area.

## 4. What is optional (only while needed)

| Part | RAM | Needed when |
|---|---|---|
| USB ROM loader: usb task 6 KB + UART driver (4 KB receive buffer) | 11.7 KB (board) | only while Settings > USB Transfer is open (v0.16.0; about once per board) |
| draw task (core 0) stack | 5 KB | a game that draws from a snapshot |
| PWM sound buffer | 4 KB | Sound Output = PWM only (v0.15.5: taken on demand) |
| SD import buffers | while copying | start-up only |
| Log page lines | in the game area | while the page is open |

## 5. The plan

1. **Measure**: done on the 3.5" (section 3).
2. **Budget** (3.5"): area = free at the menu - draw task - 12 KB reserve =
   81 KB with the USB loader running (v0.15.7); ~92 KB since it runs only
   on the USB Transfer page (v0.16.0).
3. **Fit every game**: ~92 KB holds every game's minimum, Qix's 88.2 KB
   included (its two 32 KB halves fit the one block); the rest of the
   area takes ROM regions. Next, as numbers come in: every game's
   "Stacks" line, then stacks cut to what they use plus a margin (ui 12
   -> 8 KB would give 4 KB more); the other boards' "Mem" lines (the
   2.8" has no SD card, panels differ in buffer sizes); a check of each
   game's need against the area before it starts, so the menu can say
   plainly which games a board can't hold.
