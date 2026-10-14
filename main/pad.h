// Game controllers over Bluetooth (Bluepad32 + BTstack), seen by the rest of
// the firmware as plain button bits. Any controller Bluepad32 knows maps to
// the same layout: d-pad (or the left stick pushed past a third), A B X Y,
// shoulders, Start, Select, Home.
#pragma once

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
};

#define PAD_MAX 2

typedef struct {
    uint32_t buttons;  // PAD_* bits
    int16_t lx, ly;    // left stick, -512..511 (down / right positive)
    uint8_t connected;
    char name[32];     // controller model, shown on this board only
    uint32_t reports;  // reports received since it connected
} pad_state_t;

// Starts Bluetooth and never returns (BTstack's run loop): call it last in
// app_main, which runs on core 0. Controllers in pairing mode are found and
// remembered; known ones reconnect by themselves.
void pad_bt_run(void);

// A copy of controller `idx` (0 .. PAD_MAX-1); safe from any task.
void pad_get(int idx, pad_state_t* out);

#ifdef __cplusplus
}
#endif
