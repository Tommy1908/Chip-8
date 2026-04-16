#pragma once
#include "SDL.h"
#include "chip8.h"
#include "media.h"

void draw_android_ui(const sdl_t *sdl, const chip8_t *chip8, float keyboard_start);
