#include "SDL.h"
#include "chip8.h"
#include "android_roms.h"

const char *rom_list[] = {
    "ibm.ch8",
    "tetris.ch8",
    "brix.ch8",
    "space_invaders.ch8",
};
int current_rom_idx = 0;
const int rom_count = 4;

void load_next_rom(chip8_t *chip8)
{
    current_rom_idx = (current_rom_idx + 1) % rom_count;
    // Reiniciamos el chip8 con la nueva ROM de la lista
    init_chip8(chip8, rom_list[current_rom_idx]);
    SDL_Log("Chom: Cambiando a ROM: %s", rom_list[current_rom_idx]);
}