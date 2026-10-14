// Sound out of the CYD's speaker connector: a continuous stream of 8-bit
// samples from the ESP32's built-in DAC (GPIO26, through the board's amp),
// fed by DMA. The game side fills 16-bit signed samples; this scales them
// by the volume and converts.
#pragma once

#include <cstdint>

namespace audio {

constexpr int kRate = 24000;  // samples per second

// Fills `n` samples (signed 16-bit, mono). Runs on the audio task.
using Fill = void (*)(int16_t* out, int n, void* ctx);

// Starts the DAC and the audio task (core 1). false = no audio (logged).
bool start(Fill fill, void* ctx);

// 0..100 %, amplitude in proportion; 100 % = the loudest games just under
// the DAC's full scale.
void set_volume(int percent);

}  // namespace audio
