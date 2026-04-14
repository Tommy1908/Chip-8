#include "android_ui.h"
#include "media.h"
#include "chip8.h"

#define keyboard_start 0.45f // 0-0.45f is reserved game, rest is keyboard

extern void load_next_rom(chip8_t *chip8);
extern const char *get_next_rom_name();

static const uint8_t extended_font_set[] = {
    // 0-9 (0 - 9)
    0xF0, 0x90, 0x90, 0x90, 0xF0, // 0
    0x20, 0x60, 0x20, 0x20, 0x70, // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3
    0x90, 0x90, 0xF0, 0x10, 0x10, // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
    0xF0, 0x10, 0x20, 0x40, 0x40, // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9

    // A-Z (10 - 35)
    0xF0, 0x90, 0xF0, 0x90, 0x90, // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
    0xF0, 0x80, 0x80, 0x80, 0xF0, // C
    0xE0, 0x90, 0x90, 0x90, 0xE0, // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
    0xF0, 0x80, 0xF0, 0x80, 0x80, // F
    0xF0, 0x80, 0xB0, 0x90, 0xF0, // G
    0x90, 0x90, 0xF0, 0x90, 0x90, // H
    0x70, 0x20, 0x20, 0x20, 0x70, // I
    0x10, 0x10, 0x10, 0x90, 0x70, // J
    0x90, 0xA0, 0xC0, 0xA0, 0x90, // K
    0x80, 0x80, 0x80, 0x80, 0xF0, // L
    0x90, 0xF0, 0xF0, 0x90, 0x90, // M
    0x90, 0xD0, 0xB0, 0x90, 0x90, // N
    0xF0, 0x90, 0x90, 0x90, 0xF0, // O
    0xF0, 0x90, 0xF0, 0x80, 0x80, // P
    0xF0, 0x90, 0x90, 0xA0, 0xD0, // Q
    0xF0, 0x90, 0xF0, 0xA0, 0x90, // R
    0xF0, 0x80, 0xF0, 0x10, 0xF0, // S
    0xE0, 0x40, 0x40, 0x40, 0x40, // T
    0x90, 0x90, 0x90, 0x90, 0xF0, // U
    0x90, 0x90, 0x90, 0x90, 0x60, // V
    0x90, 0x90, 0xF0, 0xF0, 0x90, // W
    0x90, 0x90, 0x60, 0x90, 0x90, // X
    0x90, 0x90, 0x70, 0x20, 0x20, // Y
    0xF0, 0x10, 0x20, 0x40, 0xF0, // Z

    // Extra (36 - 39)
    0x00, 0x00, 0x00, 0x00, 0x00, // 36: Espacio
    0x00, 0x00, 0x00, 0x00, 0x40, // 37: Punto
    0x00, 0x00, 0xF0, 0x00, 0x00, // 38: Guion
    0x00, 0x00, 0x00, 0x00, 0xF0, // 39: Guion bajo
    0x00, 0x40, 0x00, 0x40, 0x00  // 40: Dos puntos
};

static const uint8_t chip8_keymap[4][4] = {
    {0x1, 0x2, 0x3, 0xC},
    {0x4, 0x5, 0x6, 0xD},
    {0x7, 0x8, 0x9, 0xE},
    {0xA, 0x0, 0xB, 0xF}};

// Index ascii to or font index
static int get_char_index(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    // No hay mayus y min
    if (c >= 'A' && c <= 'Z')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 10;
    if (c == '.')
        return 37;
    if (c == '-')
        return 38;
    if (c == '_')
        return 39;
    if (c == ':')
        return 40;
    // default
    return 36;
}

static void draw_text_char_ascii(SDL_Renderer *renderer, char c, int x, int y, int scale)
{
    int index = get_char_index(c);

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    for (int row = 0; row < 5; row++)
    {
        uint8_t byte = extended_font_set[index * 5 + row];
        for (int col = 0; col < 8; col++)
        {
            if (byte & (0x80 >> col))
            {
                SDL_Rect r = {x + col * scale, y + row * scale, scale, scale};
                SDL_RenderFillRect(renderer, &r);
            }
        }
    }
}

static void draw_text_string(SDL_Renderer *renderer, const char *str, int x, int y, int scale)
{
    int i = 0;
    int step = 5 * scale;
    while (str[i] != '\0')
    {
        draw_text_char_ascii(renderer, str[i], x + (i * step), y, scale);
        i++;
    }
}

static void draw_centered_text_in_rect(SDL_Renderer *renderer, const char *text, SDL_Rect rect, int base_scale, bool auto_scale)
{
    int scale = base_scale;
    int step = 5 * scale;
    int text_w = strlen(text) * step;

    // Adjust margins
    if (auto_scale)
    {
        while (text_w > rect.w - 10 && scale > 1)
        {
            scale--;
            step = 5 * scale;
            text_w = strlen(text) * step;
        }
    }

    int text_h = 5 * scale;
    int x = rect.x + (rect.w / 2) - (text_w / 2);
    int y = rect.y + (rect.h / 2) - (text_h / 2);

    draw_text_string(renderer, text, x, y, scale);
}

static void draw_keyboard(SDL_Renderer *renderer, const chip8_t *chip8, uint32_t ui_top, uint32_t key_w, uint32_t key_h)
{
    const char hex_chars[] = "0123456789ABCDEF";

    for (uint8_t row = 0; row < 4; row++)
    {
        for (uint8_t col = 0; col < 4; col++)
        {
            uint8_t key_val = chip8_keymap[row][col];
            SDL_Rect rect = {col * key_w + 2, ui_top + (row * key_h) + 2, key_w - 4, key_h - 4};

            if (chip8->keypad[key_val])
            {
                SDL_SetRenderDrawColor(renderer, 50, 150, 50, 255);
            }
            else
            {
                SDL_SetRenderDrawColor(renderer, 35, 35, 35, 255);
            }
            SDL_RenderFillRect(renderer, &rect);

            int scale = 6;
            int char_w = 4 * scale;
            int char_h = 5 * scale;

            draw_text_char_ascii(renderer, hex_chars[key_val],
                                 rect.x + (rect.w / 2) - (char_w / 2),
                                 rect.y + (rect.h / 2) - (char_h / 2),
                                 scale);
        }
    }
}

static void draw_special_buttons(SDL_Renderer *renderer, uint32_t width, uint32_t height, uint32_t ui_top, uint32_t key_h)
{
    int special_y = ui_top + (4 * key_h);
    SDL_Rect btn_left = {2, special_y + 2, (width / 2) - 4, (height - special_y) - 4};
    SDL_Rect btn_right = {(width / 2) + 2, special_y + 2, (width / 2) - 4, (height - special_y) - 4};

    // Background
    SDL_SetRenderDrawColor(renderer, 150, 50, 50, 255);
    SDL_RenderFillRect(renderer, &btn_left);
    SDL_SetRenderDrawColor(renderer, 50, 50, 150, 255);
    SDL_RenderFillRect(renderer, &btn_right);

    int text_sacale = 5;
    // RESTART text
    draw_centered_text_in_rect(renderer, "RESTART", btn_left, text_sacale, false);

    // NEXT ROM text
    char next_txt_buffer[64];
    const char *next_rom_name = get_next_rom_name();
    snprintf(next_txt_buffer, sizeof(next_txt_buffer), "NEXT ROM: %s", next_rom_name);
    draw_centered_text_in_rect(renderer, next_txt_buffer, btn_right, text_sacale, true);
}

void draw_android_ui(const sdl_t *sdl, const chip8_t *chip8)
{
    uint32_t width, height;
    SDL_GetWindowSize(sdl->window, &width, &height);

    // 45% percent is where the ui can start drawing
    uint32_t ui_top = height * keyboard_start;
    // Divide spacer for amount of keys, it will have original 4x4 keyboard + 1 row 1x2
    uint32_t key_width = width / 4;
    uint32_t key_height = (height - ui_top) / 5;

    // 4x4 keyboard
    draw_keyboard(sdl->renderer, chip8, ui_top, key_width, key_height);

    // Special buttoms
    draw_special_buttons(sdl->renderer, width, height, ui_top, key_height);
}

void handle_android_touch(SDL_Event *event, chip8_t *chip8, sdl_t *sdl)
{
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
