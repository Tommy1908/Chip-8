#pragma once

#include "chip8.h"

extern const char *rom_list[];
extern int current_rom_idx;
extern const int rom_count;

void load_next_rom(chip8_t *chip8);
const char *get_next_rom_name();