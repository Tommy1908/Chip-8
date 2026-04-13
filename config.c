#include "config.h"

// Set up initial emulator config from arguments
bool set_config_from_args(config_t *config, const int argc, char **argv)
{
    // Defaults
    config->window_width = 64;  // Original X
    config->window_height = 32; // Original Y
    config->scale = 10;

    config->fg_color = 0xFFFFFFFF;
    config->bg_color = 0x00000000;
    config->pixel_outlines = false; // By default I disable the outline

    config->window_flags = 0;

    config->instructions_per_second = 700;

    config->increment_i_on_0xFX = true; // On the original and chip-48 was incremented, schip was left unmodified
    config->shift_from_vy = true;       // On the original it took vy and shift it into vx, later it was only from vx

    // TODO: AUDIO
    config->square_wave_freq = 440;    // Nota La (A4)
    config->audio_sample_rate = 44100; // 44.1Khz
    config->volume = 3000;             // Un volumen razonable para int16_t

    // TODO: OVERRIDE
    for (int i = 1; i < argc; i++)
    {
        (void)argv[i];
    }

    return true;
}