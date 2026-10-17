// Game controllers over Bluetooth (Bluepad32 + BTstack), seen by the rest of
// the firmware as plain button bits. Any controller Bluepad32 knows maps to
// the same layout: d-pad (or the left stick pushed past a third), A B X Y,
// shoulders, Start, Select, Home.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PAD_UP = 1 << 0,
    PAD_DOWN = 1 << 1,
    PAD_LEFT = 1 << 2,
    PAD_RIGHT = 1 << 3,
    PAD_A = 1 << 4,
    PAD_B = 1 << 5,
    PAD_X = 1 << 6,
    PAD_Y = 1 << 7,
    PAD_L = 1 << 8,       // shoulder or trigger
    PAD_R = 1 << 9,
    PAD_START = 1 << 10,  // Start / Options / +
    PAD_SELECT = 1 << 11, // Select / Share / Create / -
    PAD_HOME = 1 << 12,   // PS / Xbox / Home
    PAD_RUP = 1 << 13,    // the right stick pushed past a third
    PAD_RDOWN = 1 << 14,
    PAD_RLEFT = 1 << 15,
    PAD_RRIGHT = 1 << 16,
};

#define PAD_MAX 2

typedef struct {
    uint32_t buttons;  // PAD_* bits
    int16_t lx, ly;    // left stick, -512..511 (down / right positive)
    int16_t rx, ry;    // right stick
    uint8_t connected;
    char name[32];     // controller model, shown on this board only
    uint32_t reports;  // reports received since it connected
} pad_state_t;

// Starts Bluetooth and never returns (BTstack's run loop): call it last in
// app_main, which runs on core 0. Controllers in pairing mode are found and
// remembered (link keys in NVS); a paired controller reconnects when it
// pages the board (most do when woken). A pairing is kept when a connection
// attempt fails; it goes only when the controller refuses the key or with
// pad_forget_all().
void pad_bt_run(void);

// A copy of controller `idx` (0 .. PAD_MAX-1); safe from any task.
void pad_get(int idx, pad_state_t* out);

// Buttons are named by position (Xbox layout: A = bottom, B = right,
// X = left, Y = top) on every controller. true swaps A<->B and X<->Y, for
// players who'd rather follow Nintendo-style printed letters.
void pad_set_swap_ab(bool swap);

// Drops every connection and forgets every paired controller (their keys in
// NVS); controllers in pairing mode are found again. Safe from any task.
void pad_forget_all(void);

// Controllers paired with this board (BR/EDR link keys kept); -1 on BLE-only
// chips, whose bonds aren't counted.
int pad_paired_count(void);

#ifdef __cplusplus
}
#endif
