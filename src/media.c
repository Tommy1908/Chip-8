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

    // Conditional window creation for Android and Linux
#ifdef __ANDROID__
    sdl->window = SDL_CreateWindow("Chip8",
                                   0, 0, 0, 0,
                                   SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);

    int w, h;
    SDL_GetWindowSize(sdl->window, &w, &h);

    int vertical_margin = 400;
    int horizontal_margin = 10;

    // Use diferent scale might lead to some deformation or sparation of pixels, TODO: make this option, but seems easier to play games like this on small displays
    int available_w = w - (horizontal_margin * 2);
    config->scale_x = available_w / config->window_width;
    config->scale_y = config->scale_x;

    config->offset_x = (w - (config->window_width * config->scale)) / 2;
    config->offset_y = vertical_margin;

    // Init vibration
    sdl->vibrator = NULL;
    if (SDL_InitSubSystem(SDL_INIT_HAPTIC) >= 0)
    {
        sdl->vibrator = SDL_HapticOpen(0);
        if (sdl->vibrator)
        {
            if (SDL_HapticRumbleInit(sdl->vibrator) != 0) // 0 is simple vibration, checking its avalaible
            {
                SDL_HapticClose(sdl->vibrator);
                sdl->vibrator = NULL;
            }
        }
    }

#else
    sdl->window = SDL_CreateWindow("Chip8",
                                   SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED,
                                   config->window_width * config->scale,
                                   config->window_height * config->scale,
                                   config->window_flags);
#endif
    if (!sdl->window)
    {
        SDL_Log("Couldn't create window %s\n", SDL_GetError());
        return false;
    }

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!sdl->renderer)
    {
        SDL_Log("Couldn't create Renderer %s\n", SDL_GetError());
        return false;
    }

    sdl->texture = SDL_CreateTexture(
        sdl->renderer,
        SDL_PIXELFORMAT_RGBA8888, // 32 -> R G B A
        SDL_TEXTUREACCESS_STREAMING,
        config->window_width, config->window_height);
    if (!sdl->texture)
    {
        SDL_Log("Couldn't create  textura: %s", SDL_GetError());
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

void final_cleanup(sdl_t *sdl)
{
#ifdef __ANDROID__
    if (sdl->vibrator != NULL)
    {
        SDL_HapticClose(sdl->vibrator);
        sdl->vibrator = NULL;
    }
#endif

    SDL_CloseAudioDevice(sdl->device);
    SDL_DestroyTexture(sdl->texture);
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

void update_screen(const sdl_t *sdl, const config_t *config, const chip8_t *chip8)
{

    int screen_size = (int)(sizeof chip8->display);
    uint32_t pixels[screen_size];
    static bool ghost_pixel[sizeof(chip8->display)] = {0}; // Some games do constant update of graphics, we can make everything last 1 more frame. Avoids flickering on some cases

    for (int i = 0; i < screen_size; i++)
    {
        if (chip8->display[i])
        {
            pixels[i] = config->fg_color;
            // Refresh ghost pixel
            ghost_pixel[i] = 1;
        }
        else
        {
            if (ghost_pixel[i])
            {
                pixels[i] = config->fg_color;
                // Consume ghost pixel
                ghost_pixel[i] = 0;
            }
            else
            {
                pixels[i] = config->bg_color;
            }
        }
    }

    SDL_UpdateTexture(sdl->texture, NULL, pixels, config->window_width * sizeof(uint32_t));

    // Here we create the window size rectangle, to scale the texture
    SDL_Rect dest_rect = {
        .x = config->offset_x,
        .y = config->offset_y,
        .w = 64 * config->scale,
        .h = 32 * config->scale};

    // Estiramos el renderer con la textura
    SDL_RenderCopy(sdl->renderer, sdl->texture, NULL, &dest_rect);

#ifdef __ANDROID__
    draw_android_ui(sdl, chip8, config->keyboard_start);
#endif

    SDL_RenderPresent(sdl->renderer);
}

// Chip8                 Linux
// 1  2	 3  C     ->     1  2  3  4
// 4  5	 6  D     ->     Q  W  E  R
// 7  8	 9  E     ->     A  S  D  F
// A  0	 B  F     ->     Z  X  C  V
// Android
// Show a virtual keyboard, just like Chip8
static int map_key(SDL_Keycode key)
{
    switch (key)
    {
    case SDLK_x:
        return 0x0;
    case SDLK_1:
        return 0x1;
    case SDLK_2:
        return 0x2;
    case SDLK_3:
        return 0x3;
    case SDLK_q:
        return 0x4;
    case SDLK_w:
        return 0x5;
    case SDLK_e:
        return 0x6;
    case SDLK_a:
        return 0x7;
    case SDLK_s:
        return 0x8;
    case SDLK_d:
        return 0x9;
    case SDLK_z:
        return 0xA;
    case SDLK_c:
        return 0xB;
    case SDLK_4:
        return 0xC;
    case SDLK_r:
        return 0xD;
    case SDLK_f:
        return 0xE;
    case SDLK_v:
        return 0xF;
    default:
        return -1;
    }
}
void handle_input(chip8_t *chip8, sdl_t *sdl, const config_t *config)
{
    (void)sdl;    // Only used on android, make compiler okey with not using it
    (void)config; // Only used on android, make compiler okey with not using it
    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
#ifdef __ANDROID__
        case SDL_FINGERDOWN:
        case SDL_FINGERUP:
        case SDL_FINGERMOTION:
            SDL_Log("El valor antes: %f", config->keyboard_start);
            handle_android_touch(&event, chip8, config->keyboard_start, sdl->vibrator);
            break;
#endif
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
            default:
            {
                int key = map_key(event.key.keysym.sym);
                if (key != -1)
                    chip8->keypad[key] = 1;
                break;
            }
            }
            break;
        case SDL_KEYUP:
        {
            int key = map_key(event.key.keysym.sym);
            if (key != -1)
                chip8->keypad[key] = 0;
            break;
        }
        default:
            break;
        }
    }
}
