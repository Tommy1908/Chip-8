#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <SDL2/SDL.h>

#include "config.h"
#include "chip8.h"
#include "media.h"

int main(int argc, char **argv)
{
    // Default use of args
    if (argc < 2)
    {
        fprintf(stderr, "Usage %s <rom>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    // Initialize emulator config
    config_t config = {0};
    if (!set_config_from_args(&config, argc, argv))
        exit(EXIT_FAILURE);

    // Initialize SDL
    sdl_t sdl = {0};
    if (!init_sdl(&sdl, &config))
        exit(EXIT_FAILURE);

    // Initializa CHIP8 MACHINE
    chip8_t chip8 = {0};
    const char *rom_name = argv[1];
    if (!init_chip8(&chip8, rom_name))
        exit(EXIT_FAILURE);

    // Initial screen clear
    clear_screen(&sdl, &config);

    // Prepare rand
    srand(time(NULL));

    // Main emulator loop
    while (chip8.state != QUIT)
    {

        if (chip8.state == PAUSED)
            continue;

        // Get time
        const uint64_t start_time = SDL_GetPerformanceCounter();

        // In this frame emulate config.instructions_per_second / 60 instructions (60hz)
        for (uint32_t i = 0; i < config.instructions_per_second / 60; i++)
        {
            // Handle User input
            handle_input(&chip8);

            emulate_instruction(&chip8, &config);
        }
        // Get time after peforming instructions
        const uint64_t finish_time = SDL_GetPerformanceCounter();

        // Delay fo aprox 60fs (1000ms/60 ~= 16)
        double time_elapsed_ms = (double)((finish_time - start_time) * 1000) / SDL_GetPerformanceFrequency();
        if (16.67f > time_elapsed_ms)
        {
            SDL_Delay((uint32_t)(16.67f - time_elapsed_ms));
        }
        // Update with changes
        update_screen(sdl, &config, &chip8);
        // Update timers (delay and sound)
        update_timers(&chip8);
        // Play sound
        if (chip8.sound_timer > 0)
            SDL_PauseAudioDevice(sdl.device, 0); // Play sound
        else
            SDL_PauseAudioDevice(sdl.device, 1); // Pause sound

        if (chip8.PC > 4096)
        {
            fprintf(stderr, "Reach end PC:%d is out of bounds\n", chip8.PC);
            break;
        }
    }

    // Final cleanup
    final_cleanup(&sdl);

    exit(EXIT_SUCCESS);
}