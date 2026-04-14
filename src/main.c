#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include "SDL.h"

#include "config.h"
#include "chip8.h"
#include "media.h"

int main(int argc, char **argv);
int main_loop(int argc, char **argv);

int main(int argc, char **argv)
{
#ifdef __ANDROID__
#include "android_roms.h"
    // Android uses ROMS from Assets
    // To be able to use it just like Linux we adapt it
    char *rom_name = (char *)rom_list[current_rom_idx];

    char *mock_argv[] = {argv[0], rom_name, NULL};
    int mock_argc = 2;
    return main_loop(mock_argc, mock_argv);
#else
    // Default use of args
    if (argc < 2)
    {
        fprintf(stderr, "Usage %s <rom>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    return main_loop(argc, argv);

#endif
}

int main_loop(int argc, char **argv)
{
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
    char *rom_name = argv[1];
    if (!init_chip8(&chip8, rom_name))
        exit(EXIT_FAILURE);

    // Initial screen clear
    clear_screen(&sdl, &config);

    // Prepare rand
    srand(time(NULL));

    // Main emulator loop
    while (chip8.state != QUIT)
    {
        // Handle User input
        handle_input(&chip8, &sdl);

        if (chip8.state == PAUSED)
            continue;

        // Get time
        const uint64_t start_time = SDL_GetPerformanceCounter();

        // In this frame emulate config.instructions_per_second / 60 instructions (60hz)
        for (uint32_t i = 0; i < config.instructions_per_second / 60; i++)
        {

            emulate_instruction(&chip8, &config);
        }
        // Get time after peforming instructions
        const uint64_t finish_time = SDL_GetPerformanceCounter();
        double time_elapsed_ms = (double)((finish_time - start_time) * 1000) / SDL_GetPerformanceFrequency();

        // Update with changes
        update_screen(&sdl, &config, &chip8);
        // Update timers (delay and sound)
        update_timers(&chip8);
        // Play sound
        if (chip8.sound_timer > 0)
            SDL_PauseAudioDevice(sdl.device, 0); // Play sound
        else
            SDL_PauseAudioDevice(sdl.device, 1); // Pause sound

        if (16.67f > time_elapsed_ms)
        {
            SDL_Delay((uint32_t)(16.67f - time_elapsed_ms));
        }
        if (chip8.PC > 4096)
        {
            fprintf(stderr, "Reach end PC:%d is out of bounds\n", chip8.PC);
            exit(EXIT_FAILURE);
        }
    }

    // Final cleanup
    final_cleanup(&sdl);

    exit(EXIT_SUCCESS);
}