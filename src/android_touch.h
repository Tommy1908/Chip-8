#pragma once
#include "SDL.h"
#include "chip8.h"
#include "media.h"

void handle_android_touch(SDL_Event *event, chip8_t *chip8, float keyboard_start);
