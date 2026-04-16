#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    // SDL Window size
    uint32_t window_width;
    uint32_t window_height;
    uint32_t offset_x;
    uint32_t offset_y;
    uint32_t scale_x;
    uint32_t scale_y;
    uint32_t scale;
    float keyboard_start;
    uint32_t window_flags; // https://wiki.libsdl.org/SDL2/SDL_WindowFlags
    uint32_t fg_color;     // Foreground RGBA8888
    uint32_t bg_color;     // Background RGBA8888
    bool pixel_outlines;
    uint32_t instructions_per_second;

    bool increment_i_on_0xFX; // https://en.wikipediaokl.org/wiki/CHIP-8#cite_note-increment-28
    bool shift_from_vy;       // https://en.wikipedia.org/wiki/CHIP-8#cite_note-bitshift-25
    bool reset_vf_on_bitwise_ops;

    // TODO: Audio stuff should check later
    uint32_t square_wave_freq; // Frequency of square wave sound e.g. 440hz for middle A
    uint32_t audio_sample_rate;
    int16_t volume; // How loud or not is the sound

} config_t;

bool set_config_from_args(config_t *config, int argc, char **argv);