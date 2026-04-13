#pragma once
#include <SDL2/SDL.h>
#include "config.h"
#include "chip8.h"

// SDL Container
typedef struct
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID device;
} sdl_t;

bool init_sdl(sdl_t *sdl, config_t *config);
void audio_callback(void *userdata, uint8_t *stream, int len);
void final_cleanup(const sdl_t *sdl);
void clear_screen(const sdl_t *sdl, const config_t *config);
void update_screen(const sdl_t sdl, const config_t *config, const chip8_t *chip8);
void handle_input(chip8_t *chip8);