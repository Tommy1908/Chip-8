#include "media.h"

// TODO: Audio stuff
// SDL Audio callback
// Fill out stream/audio buffer with data
void audio_callback(void *userdata, uint8_t *stream, int len)
{
    config_t *config = (config_t *)userdata;

    int16_t *audio_data = (int16_t *)stream;
    static uint32_t running_sample_index = 0;
    const int32_t square_wave_period = config->audio_sample_rate / config->square_wave_freq;
    const int32_t half_square_wave_period = square_wave_period / 2;

    // We are filling out 2 bytes at a time (int16_t), len is in bytes,
    //   so divide by 2
    // If the current chunk of audio for the square wave is the crest of the wave,
    //   this will add the volume, otherwise it is the trough of the wave, and will add
    //   "negative" volume
    for (int i = 0; i < len / 2; i++)
        audio_data[i] = ((running_sample_index++ / half_square_wave_period) % 2) ? config->volume : -config->volume;
}

bool init_sdl(sdl_t *sdl, config_t *config)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0)
    {
        SDL_Log("Couldnt initialize SDL subsystems %s\n", SDL_GetError());
        return false;
    }

    sdl->window = SDL_CreateWindow("Tommy's Chip8",
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   config->window_width * config->scale,
                                   config->window_height * config->scale,
                                   config->window_flags);
    if (!sdl->window)
    {
        SDL_Log("Couldn't create window %s\n", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!sdl->renderer)
    {
        SDL_Log("Couldn't create Renderer %s\n", SDL_GetError());
        return false;
    }

    // TODO: Audio
    // Init Audio stuff
    sdl->want = (SDL_AudioSpec){
        .freq = 44100,          // 44100hz "CD" quality
        .format = AUDIO_S16SYS, // Signed 16 bit little endian
        .channels = 1,          // Mono, 1 channel
        .samples = 256,
        .callback = audio_callback,
        .userdata = config, // Userdata passed to audio callback
    };

    sdl->device = SDL_OpenAudioDevice(NULL, 0, &sdl->want, &sdl->have, 0);

    if (sdl->device == 0)
    {
        SDL_Log("Could not get an Audio Device %s\n", SDL_GetError());
        return false;
    }

    if ((sdl->want.format != sdl->have.format) ||
        (sdl->want.channels != sdl->have.channels))
    {

        SDL_Log("Could not get desired Audio Spec\n");
        return false;
    }

    return true;
}

void final_cleanup(const sdl_t *sdl)
{
    SDL_DestroyRenderer(sdl->renderer);
    SDL_DestroyWindow(sdl->window);
    SDL_Quit();
}

// Clear screen to bg color
void clear_screen(const sdl_t *sdl, const config_t *config)
{
    const uint8_t r = config->bg_color >> 24 & 0XFF;
    const uint8_t g = config->bg_color >> 16 & 0XFF;
    const uint8_t b = config->bg_color >> 8 & 0XFF;
    const uint8_t a = config->bg_color & 0XFF;

    SDL_SetRenderDrawColor(sdl->renderer, r, g, b, a);
    SDL_RenderClear(sdl->renderer);
}

void update_screen(const sdl_t sdl, const config_t *config, const chip8_t *chip8)
{
    SDL_Rect rect = {.x = 0, .y = 0, .w = config->scale, .h = config->scale};

    const uint8_t bg_r = config->bg_color >> 24 & 0XFF;
    const uint8_t bg_g = config->bg_color >> 16 & 0XFF;
    const uint8_t bg_b = config->bg_color >> 8 & 0XFF;
    const uint8_t bg_a = config->bg_color & 0XFF;

    const uint8_t fg_r = config->fg_color >> 24 & 0XFF;
    const uint8_t fg_g = config->fg_color >> 16 & 0XFF;
    const uint8_t fg_b = config->fg_color >> 8 & 0XFF;
    const uint8_t fg_a = config->fg_color & 0XFF;

    // Loop through display pixels
    for (uint32_t i = 0; i < sizeof chip8->display; i++)
    {
        // 1D index i to 2D X,Y
        rect.x = (i % config->window_width) * config->scale;
        rect.y = (i / config->window_width) * config->scale;

        if (chip8->display[i])
        {
            // If on draw foreground
            SDL_SetRenderDrawColor(sdl.renderer, fg_r, fg_g, fg_b, fg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);

            // If drawing pixel outlines
            if (config->pixel_outlines)
            {
                SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
                SDL_RenderDrawRect(sdl.renderer, &rect);
            }
        }
        else
        {
            // If off draw foreground
            SDL_SetRenderDrawColor(sdl.renderer, bg_r, bg_g, bg_b, bg_a);
            SDL_RenderFillRect(sdl.renderer, &rect);
        }
    }

    SDL_RenderPresent(sdl.renderer);
}

// 1  2	 3  C     ->     1  2  3  4
// 4  5	 6  D     ->     Q  W  E  R
// 7  8	 9  E     ->     A  S  D  F
// A  0	 B  F     ->     Z  X  C  V
void handle_input(chip8_t *chip8)
{
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
        case SDL_QUIT:
            chip8->state = QUIT;
            return;
        case SDL_KEYDOWN:
            switch (event.key.keysym.sym)
            {
            case SDLK_ESCAPE:
                chip8->state = QUIT;
                return;
            case SDLK_SPACE:
                // PAUSE and RESUME
                if (chip8->state == RUNNING)
                {
                    chip8->state = PAUSED;
                    puts("-----PAUSED-----");
                }
                else
                {
                    chip8->state = RUNNING;
                }
                return;
            case SDLK_TAB:
                // Reset Rom
                init_chip8(chip8, chip8->rom_name);
                break;

            case SDLK_1:
                chip8->keypad[0x1] = true;
                break;
            case SDLK_2:
                chip8->keypad[0x2] = true;
                break;
            case SDLK_3:
                chip8->keypad[0x3] = true;
                break;
            case SDLK_4:
                chip8->keypad[0xC] = true;
                break;

            case SDLK_q:
                chip8->keypad[0x4] = true;
                break;
            case SDLK_w:
                chip8->keypad[0x5] = true;
                break;
            case SDLK_e:
                chip8->keypad[0x6] = true;
                break;
            case SDLK_r:
                chip8->keypad[0xD] = true;
                break;

            case SDLK_a:
                chip8->keypad[0x7] = true;
                break;
            case SDLK_s:
                chip8->keypad[0x8] = true;
                break;
            case SDLK_d:
                chip8->keypad[0x9] = true;
                break;
            case SDLK_f:
                chip8->keypad[0xE] = true;
                break;

            case SDLK_z:
                chip8->keypad[0xA] = true;
                break;
            case SDLK_x:
                chip8->keypad[0x0] = true;
                break;
            case SDLK_c:
                chip8->keypad[0xB] = true;
                break;
            case SDLK_v:
                chip8->keypad[0xF] = true;
                break;

            default:
#ifdef DEBUG
                SDL_Log("Tecla presionada: %s\n", SDL_GetKeyName(event.key.keysym.sym));
#endif
                break;
            }
            break;
        case SDL_KEYUP:
            switch (event.key.keysym.sym)
            {
            case SDLK_1:
                chip8->keypad[0x1] = false;
                break;
            case SDLK_2:
                chip8->keypad[0x2] = false;
                break;
            case SDLK_3:
                chip8->keypad[0x3] = false;
                break;
            case SDLK_4:
                chip8->keypad[0xC] = false;
                break;
            case SDLK_q:
                chip8->keypad[0x4] = false;
                break;
            case SDLK_w:
                chip8->keypad[0x5] = false;
                break;
            case SDLK_e:
                chip8->keypad[0x6] = false;
                break;
            case SDLK_r:
                chip8->keypad[0xD] = false;
                break;
            case SDLK_a:
                chip8->keypad[0x7] = false;
                break;
            case SDLK_s:
                chip8->keypad[0x8] = false;
                break;
            case SDLK_d:
                chip8->keypad[0x9] = false;
                break;
            case SDLK_f:
                chip8->keypad[0xE] = false;
                break;
            case SDLK_z:
                chip8->keypad[0xA] = false;
                break;
            case SDLK_x:
                chip8->keypad[0x0] = false;
                break;
            case SDLK_c:
                chip8->keypad[0xB] = false;
                break;
            case SDLK_v:
                chip8->keypad[0xF] = false;
                break;
            default:
                // SDL_Log("Tecla soltada: %s\n", SDL_GetKeyName(event.key.keysym.sym));
                break;
            }
            break;
        default:
            break;
        }
    }
}
