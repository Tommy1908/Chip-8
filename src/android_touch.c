#include "android_touch.h"

static const uint8_t chip8_keymap[4][4] = {
    {0x1, 0x2, 0x3, 0xC},
    {0x4, 0x5, 0x6, 0xD},
    {0x7, 0x8, 0x9, 0xE},
    {0xA, 0x0, 0xB, 0xF}};

void handle_android_touch(SDL_Event *event, chip8_t *chip8, sdl_t *sdl, float keyboard_start)
{
    SDL_Log("El que llega: %f", keyboard_start);
    int width, height;
    SDL_GetWindowSize(sdl->window, &width, &height);

    // Get coord
    float x = event->tfinger.x;
    float y = event->tfinger.y;

    bool is_down = (event->type == SDL_FINGERDOWN);

    // Adjust to the keyboard sice (0-0.45 game 0.45-1 is keyboard)
    if (y > keyboard_start)
    {
        // So right now we have 0.45->1.00
        //  Normalize height, multiply and truncate
        float normalized_y = (y - keyboard_start) / (1.0f - keyboard_start);
        int row = (int)(normalized_y * 5.0f);
        int col = (int)(x * 4.0f);

        if (col > 3)
            col = 3;
        if (row < 4)
        {
            // Activate chip8 keypad
            chip8->keypad[chip8_keymap[row][col]] = is_down;
        }
        else if (row == 4 && is_down)
        {
            // Special keys
            if (col < 2)
                // Restart rom
                init_chip8(chip8, chip8->rom_name);
            else
                // Change rom
                load_next_rom(chip8);
        }
    }
    else
    {
        if (is_down)
        {
            // Pause when touching the game screen
            if (chip8->state == RUNNING)
            {
                chip8->state = PAUSED;
                puts("-----PAUSED-----");
            }
            else
            {
                chip8->state = RUNNING;
            }
        }
    }
}