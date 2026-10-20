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
| Rally-X | 18.6 | - | 15.0 / 16.8 | **33.7 / 35.5** | 2.6 | 21.1 | 4 |
| Q*bert | 19.5 | - | 14.7 / 16.1 | **34.2 / 35.6** | 1.4 | 68.0 | 8 |
| Donkey Kong | 21.3 | - | 14.7 / 16.1 | **36.0 / 37.4** | 1.8 | 32.8 | 4 |
| Galaga | 23.8 | - | 15.7 / 17.1 | **39.5 / 40.9** | 3.6 | 37.0 | 4 |
| Asteroids | 25.5 | - | 15.8 / 17.7 | **41.3 / 43.2** | 20.0 | 8.0 | 2 |
| Tempest | 29.2 | - | 15.8 / 17.7 | **45.0 / 46.9** | 20.0 | 24.0 | 4 |
| Dig Dug | 31.9 | - | 15.7 / 17.1 | **47.6 / 49.0** | 1.8 | 55.0 | 4 |
| Mr. Do! | 34.3 | - | 14.1 / 15.5 | **48.4 / 49.8** | - | 56.1 | 8 |
| Ms. Pac-Man | 34.8 | - | 15.7 / 17.1 | **50.5 / 51.9** | 2.0 | 34.8 | 4 |
| Robotron | 51.9 | - | 15.2 / 17.0 | **67.2 / 68.9** | - | 52.0 | 4 |
| Defender | 51.9 | - | 15.2 / 17.0 | **67.2 / 68.9** | - | 30.0 | 4 |
| Star Wars | 52.6 | - | 15.8 / 17.7 | **68.4 / 70.2** | 27.6 | 72.0 | 16 |
| Qix | 8.1 | 64.0 (2 x 32) | 14.7 / 16.1 | **86.8 / 88.2** | - | 32.0 | 2 |

The largest single block a game needs is its machine (Star Wars 52.6 KB,
Robotron / Defender 51.9) or one of Qix's 32 KB halves.

## 2. The ESP32 boards' RAM

- Internal RAM usable by the heap: **(board)** "Heap: N KB internal in
  all" at each start.
- Static RAM (.data + .bss, fixed at build): v0.3.1 105.6 KB, v0.15.x
  121 KB **(build)**; the biggest static symbols are listed in every CI
  build log ("Biggest static RAM symbols").
- v0.1.0 (controller test, Bluetooth on): 142 KB free, largest block
  108 KB **(board, Tom)**.
- v0.14.6, 3.2" at a game's start: ~77 KB free; v0.15.4, 3.5": 70 KB
  free (31 in the area + 39 outside) **(board)**.

## 3. What always runs (the baseline)

| Part | RAM | Source |
|---|---|---|
| Static .data + .bss | 121 KB | build |
| Bluetooth controller + BTstack + Bluepad32 (heap) | "Mem Bluetooth started" step | board |
| A controller connected | "Mem controller connected" step | board |
| main task (app_main, then BTstack's run loop) stack | 8 KB (used: "Stacks" line) | code / board |
| ui task stack (menu, game runner) | 12 KB (used: "Stacks") | code / board |
| audio task stack | 3 KB | code |
| Sound DMA (DAC: 4 x 1 KB) | 4 KB | code |
| Display (LovyanGFX, no frame buffer) + touch | "Mem after screen + touch" step | board |
| ROM catalog | "Mem after ROM catalog" step | board |
| ESP-IDF system tasks (IPC x2, esp_timer, idle x2), NVS | "Mem at start" / "after NVS" | board |

## 4. What is optional (only while needed)

| Part | RAM | Needed when |
|---|---|---|
| USB ROM loader: usb task 6 KB + UART driver (4 KB receive buffer) | ~10.5 KB | the web page sends ROMs - the menu only, never in a game |
| draw task (core 0) stack | 5 KB | a game that draws from a snapshot |
| PWM sound buffer | 4 KB | Sound Output = PWM only (v0.15.5: taken on demand) |
| SD import buffers | while copying | start-up only |
| Log page lines | in the game area | while the page is open |

## 5. The plan

1. **Measure** (v0.15.6): one start on an ESP32 board and one game
   start / end fill in sections 2-3 from the Log.
2. **Budget**: free RAM when a game starts = heap - baseline. With the
   optional parts stopped for a game, the game area on an ESP32 board is
   that minus a small margin for Bluetooth's own allocations while
   playing (measured: a controller connecting, the "Stacks" line).
3. **Fit every game**: the area must hold each game's minimum (section 1),
   and its biggest single block. Where it can't (Qix's 88 KB, the 52 KB
   machines if Bluetooth splits the big region), cut the baseline by the
   measured amounts - task stacks to what they use, the USB loader and the
   draw task out of a game's way - or shrink the machines (Qix's video RAM
   halves, the Williams 48 KB RAM) until the numbers add up. Each game's
   need is checked against the area before it starts, and the menu says
   plainly which games a board can't hold.
