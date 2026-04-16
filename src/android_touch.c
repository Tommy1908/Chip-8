#include "android_touch.h"
#include "android_roms.h"

static const uint8_t chip8_keymap[4][4] = {
    {0x1, 0x2, 0x3, 0xC},
    {0x4, 0x5, 0x6, 0xD},
    {0x7, 0x8, 0x9, 0xE},
    {0xA, 0x0, 0xB, 0xF}};

static void get_keyboard_position(float x, float y, float keyboard_start, int *row, int *col)
{
    // So right now we have 0.45->1.00
    //  Normalize height, multiply and truncate
    float normalized_y = (y - keyboard_start) / (1.0f - keyboard_start);
    *row = (int)(normalized_y * 5.0f);
    *col = (int)(x * 4.0f);

    if (*col > 3)
        *col = 3;
    if (*row > 4)
        *row = 4;
}

// Updates acording to current state (moving the finger from one key to other is supported)
static void update_keypad_state(chip8_t *chip8, SDL_TouchID touch_id, float keyboard_start)
{
    // Clean current keypad info, if moved the finger we need to erase old data
    memset(chip8->keypad, 0, sizeof(chip8->keypad));
    int num_fingers = SDL_GetNumTouchFingers(touch_id);

    for (int i = 0; i < num_fingers; i++)
    {
        SDL_Finger *finger = SDL_GetTouchFinger(touch_id, i);

        // Only check for keyboard touch
        if (finger->y <= keyboard_start)
            continue;

        int row, col;
        get_keyboard_position(finger->x, finger->y, keyboard_start, &row, &col);

        // Avoid moving into special key (only want to move in input keys)
        if (row < 4)
        {
            chip8->keypad[chip8_keymap[row][col]] = true;
        }
    }
}

void handle_android_touch(SDL_Event *event, chip8_t *chip8, float keyboard_start, SDL_Haptic *vibrator)
{
    // Press once actions
    if (event->type == SDL_FINGERDOWN)
    {
        float x = event->tfinger.x;
        float y = event->tfinger.y;

        // Adjust to the keyboard sice (0-0.45 game 0.45-1 is keyboard)
        if (y < keyboard_start)
        {
            // Game screen
            chip8->state = (chip8->state == RUNNING) ? PAUSED : RUNNING;
        }
        else
        {
            if (vibrator != NULL)
            {
                // Fuerza (0.0 a 1.0) y duración en ms
                SDL_HapticRumblePlay(vibrator, 0.4, 50);
            }
            // Special buttons
            int row, col;
            get_keyboard_position(x, y, keyboard_start, &row, &col);

            if (row == 4)
            {
                if (col < 2)
                    init_chip8(chip8, chip8->rom_name); // Restart rom
                else
                    load_next_rom(chip8); // Change rom
            }
        }
    }

    // Handle state continuously for keypad, gets checked when pressing down, when releasing it, and when the finger moves
    // Doing this many checks doesn’t seem to impact performance much
    // Feels nicer this way to use the keyboard
    update_keypad_state(chip8, event->tfinger.touchId, keyboard_start);
}