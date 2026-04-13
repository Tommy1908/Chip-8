#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

#include <SDL2/SDL.h>

#include "config.h"

#define ENTRY_POINT 0x200 // Where roms are loaded on ram

// SDL Container
typedef struct
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_AudioSpec want, have;
    SDL_AudioDeviceID device;
} sdl_t;

// Instruction format
typedef struct
{
    uint16_t opcode;
    uint16_t NNN; // 12 bit constant
    uint8_t NN;   // 8 bit constant
    uint8_t N;    // 4 bit constant
    uint8_t X;    // 4 bit register
    uint8_t Y;    // 4 bit register
} instruction_t;

typedef enum
{
    QUIT,
    RUNNING,
    PAUSED
} emulator_state_t;

typedef struct
{
    emulator_state_t state;
    uint8_t ram[4096];
    bool display[64 * 32];     // Original, the uppermost 256 bytes (0xF00-0xFFF) are reserved for display refresh on ram
    uint16_t stack[12];        // Stack
    uint16_t *stack_ptr;       // Stack pointer
    uint8_t V[16];             // Data registers V0-VF
    uint16_t I;                // Index register
    uint16_t PC;               // PC register
    uint8_t delay_timer;       // Decrements at 60hz when > 0
    uint8_t sound_timer;       // Decrements at 60hz and play tone when > 0
    bool keypad[16];           // 0X0-0XF
    const char *rom_name;      // Currently running ROM
    instruction_t instruction; // Current instruction
} chip8_t;

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

    sdl->renderer = SDL_CreateRenderer(sdl->window, -1, SDL_RENDERER_ACCELERATED);
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

// Initialize CHIP8 (STATE) machine
bool init_chip8(chip8_t *chip8, const char *rom_name)
{
    memset(chip8, 0, sizeof(chip8_t)); // If coming back from reset

    const uint8_t font[] = {
        0xF0, 0x90, 0x90, 0x90, 0xF0, // 0      1110 0000
        0x20, 0x60, 0x20, 0x20, 0x70, // 1      10001 0000
        0xF0, 0x10, 0xF0, 0x80, 0xF0, // 2      1110 0000
        0xF0, 0x10, 0xF0, 0x10, 0xF0, // 3      10001 0000
        0x90, 0x90, 0xF0, 0x10, 0x10, // 4      1110 0000
        0xF0, 0x80, 0xF0, 0x10, 0xF0, // 5
        0xF0, 0x80, 0xF0, 0x90, 0xF0, // 6
        0xF0, 0x10, 0x20, 0x40, 0x40, // 7
        0xF0, 0x90, 0xF0, 0x90, 0xF0, // 8
        0xF0, 0x90, 0xF0, 0x10, 0xF0, // 9
        0xF0, 0x90, 0xF0, 0x90, 0x90, // A
        0xE0, 0x90, 0xE0, 0x90, 0xE0, // B
        0xF0, 0x80, 0x80, 0x80, 0xF0, // C
        0xE0, 0x90, 0x90, 0x90, 0xE0, // D
        0xF0, 0x80, 0xF0, 0x80, 0xF0, // E
        0xF0, 0x80, 0xF0, 0x80, 0x80  // F
    };
    // https://archive.org/details/bitsavers_rcacosmacCManual1978_6956559/page/n37/mode/2up
    //  Load Font
    memcpy(&chip8->ram[0], font, sizeof(font));

    // Get Rom
    FILE *rom = fopen(rom_name, "rb");
    if (!rom)
    {
        SDL_Log("ROM %s is not valid\n", rom_name);
        return false;
    }

    fseek(rom, 0, SEEK_END);
    const size_t rom_size = ftell(rom);
    const size_t max_size = sizeof(chip8->ram) - ENTRY_POINT;
    rewind(rom);

    if (rom_size > max_size)
    {
        SDL_Log("ROM %s is too big (size %zu) for RAM (size %zu)\n", rom_name, rom_size, max_size);
        return false;
    }

    // Load Rom
    if (fread(&chip8->ram[ENTRY_POINT], rom_size, 1, rom) != 1)
    {
        SDL_Log("Couldnt read ROM %s\n", rom_name);
        return false;
    }

    fclose(rom);

    // Set Chip8 machine defaults
    chip8->state = RUNNING;
    chip8->PC = ENTRY_POINT;
    chip8->rom_name = rom_name;
    chip8->stack_ptr = &chip8->stack[0];

    return true;
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
                SDL_Log("Tecla presionada: %s\n", SDL_GetKeyName(event.key.keysym.sym));
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

#ifdef DEBUG
void print_debug_info(const chip8_t *chip8, const config_t *config)
{
    printf("Addr: 0x%04X, Opcode: 0x%04X Desc: ",
           chip8->PC - 2, chip8->instruction.opcode);
    switch ((chip8->instruction.opcode >> 12) & 0x0F)
    {
    case 0x0:
    {
        if (chip8->instruction.NN == 0xE0)
        {
            // 0x00E0: Clear the screen
            printf("Clear screen\n");
        }
        else if (chip8->instruction.NN == 0xEE)
        {
            // 0x00EE: Return from subroutine
            // Set program counter to last address on subroutine stack ("pop" it off the stack)
            // so that next opcode will be gotten from that address.
            printf("Return from subroutine\n");
        }
        else
        {
            fprintf(stderr, "OPCODE %d NOT IMPLEMENTED\n", chip8->instruction.opcode);
            // Calls machine code routine (RCA 1802 for COSMAC VIP) at address NNN. Not necessary for most ROMs.
        }
        break;
    }
    case 0x1:
    {
        // 0x1NNN: jump to address NNN
        printf("Jump to 0x%03X\n", chip8->instruction.NNN);
        break;
    }
    case 0x2:
    {
        // 0x2NNN: Call subroutine at NNN
        // Store current address to return to on subroutine stack ("push" it on the stack)
        // and set program counter to subroutine address so that the next opcode
        // is gotten from there.
        printf("Call subroutine at 0x%03X\n", chip8->instruction.NNN);
        break;
    }
    case 0x3:
    {
        // 0x3XNN: if (Vx == NN) Skips the next instruction (usually the next instruction is a jump to skip a code block)
        printf("V%X = 0x%02X, if(0x%02X == 0x%02X) then: PC(next) = 0x%03X -> 0x%03X else: PC(next) = 0x%03X\n", chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->V[chip8->instruction.X], chip8->instruction.NN, chip8->PC, (chip8->PC) + 2, chip8->PC);
        break;
    }
    case 0x4:
    {
        // 0x4XNN: if (Vx != NN) Skips the next instruction (usually the next instruction is a jump to skip a code block)
        printf("V%X = 0x%02X, if(0x%02X != 0x%02X) then: PC(next) = 0x%03X -> 0x%03X else: PC(next) = 0x%03X\n", chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->V[chip8->instruction.X], chip8->instruction.NN, chip8->PC, (chip8->PC) + 2, chip8->PC);
        break;
    }
    case 0x5:
    {
        if (chip8->instruction.N != 0)
        {
            printf("0x%04X is an Invalid OPCODE last bit must be 0\n0", chip8->instruction.opcode);
            break;
        }
        // 0x5XY0: if (Vx == Vy) Skips the next instruction (usually the next instruction is a jump to skip a code block)
        printf("V%X = 0x%02X, V%X = 0x%02X , if(0x%02X == 0x%02X) then: PC(next) = 0x%03X -> 0x%03X else: PC(next) = 0x%03X\n", chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->instruction.Y, chip8->V[chip8->instruction.Y], chip8->V[chip8->instruction.X], chip8->V[chip8->instruction.Y], chip8->PC, (chip8->PC) + 2, chip8->PC);
        break;
    }
    case 0x6:
    {
        // 0x6XNN: Sets register VX to NN.
        printf("Set register V%X to 0x%02X\n", chip8->instruction.X, chip8->instruction.NN);
        break;
    }
    case 0x7:
    {
        // 0x7XNN: Sets register VX += NN.
        printf("Set register V%X to 0x%02X += 0x%02X -> 0x%02X\n", chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->instruction.NN, (chip8->V[chip8->instruction.X] + chip8->instruction.NN));
        break;
    }
    case 0x8:
    {
        switch (chip8->instruction.N)
        {
        case 0:
            // 0x8XY0: Sets register VX = VY.
            printf("Set register V%X to V%X(0x%02X)\n", chip8->instruction.X, chip8->instruction.Y, chip8->V[chip8->instruction.Y]);
            break;
        case 1:
            // 0x8XY1: Sets VX to VX or VY (bitwise)
            printf("Set register V%X to V%X(0x%02X) or V%X(0x%02X) (bitwise)\n", chip8->instruction.X, chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->instruction.Y, chip8->V[chip8->instruction.Y]);
            break;
        case 2:
            // 0x8XY2: Sets VX to VX and VY (bitwise)
            printf("Set register V%X to V%X(0x%02X) and V%X(0x%02X) (bitwise)\n", chip8->instruction.X, chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->instruction.Y, chip8->V[chip8->instruction.Y]);
            break;
        case 3:
            // 0x8XY3: Sets VX to VX xor VY (bitwise)
            printf("Set register V%X to V%X(0x%02X) xor V%X(0x%02X) (bitwise)\n", chip8->instruction.X, chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->instruction.Y, chip8->V[chip8->instruction.Y]);
            break;
        case 4:
            // 0x8XY4: Adds VY to VX. VF is set to 1 when there's an overflow, and to 0 when there is not.
            printf("Set register V%X to V%X(0x%02X) + V%X(0x%02X), overflow? -> VF(%d)\n", chip8->instruction.X, chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->instruction.Y, chip8->V[chip8->instruction.Y], (u_int16_t)chip8->V[chip8->instruction.X] + chip8->V[chip8->instruction.Y] > 255);
            break;
        case 5:
            // 0x8XY5: VY is subtracted from VX. VF is set to 0 when there's an underflow, and 1 when there is not. (i.e. VF set to 1 if VX >= VY and 0 if not)
            printf("Set register V%X to V%X(0x%02X) - V%X(0x%02X), not underflow? -> VF(%d)\n", chip8->instruction.X, chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->instruction.Y, chip8->V[chip8->instruction.Y], chip8->V[chip8->instruction.X] >= chip8->V[chip8->instruction.Y]);
            break;
        case 6:
            // 0x8XY6: Shifts VX to the right by 1, then stores the least significant bit of VX prior to the shift into VF.
            printf("Shift right 1 bit V%X(0x%02X)->(0x%02X) and store least significan bit on VF(0x%01X)\n", chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->V[chip8->instruction.X] >> 1, chip8->V[chip8->instruction.X] & 0x01);
            break;
        case 7:
            // 0x8XY7: Sets VX to VY minus VX. VF is set to 0 when there's an underflow, and 1 when there is not. (i.e. VF set to 1 if VY >= VX)..
            printf("Set register V%X to V%X(0x%02X) - V%X(0x%02X), not underflow? -> VF(%d)\n", chip8->instruction.X, chip8->instruction.Y, chip8->V[chip8->instruction.Y], chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->V[chip8->instruction.Y] >= chip8->V[chip8->instruction.X]);
            break;
        case 0XE:
            // 0x8XYE: Shifts VX to the left by 1, then sets VF to 1 if the most significant bit of VX prior to that shift was set, or to 0 if it was unset.
            printf("Shift left 1 bit V%X(0x%02X)->(0x%02X) and store most significan bit on VF(0x%01X)\n", chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->V[chip8->instruction.X] << 1, (chip8->V[chip8->instruction.X] & 0x80) >> 7);
            break;
        default:
            printf("0x%04X is an Invalid OPCODE\n", chip8->instruction.opcode);
            break;
        }
        break;
    }
    case 0x09:
    {
        if (chip8->instruction.N != 0)
        {
            printf("0x%04X is an Invalid OPCODE\n", chip8->instruction.opcode);
            break;
        }
        // 0x9XY0: Skips the next instruction if VX does not equal VY. (Usually the next instruction is a jump to skip a code block).
        printf("V%X = 0x%02X, V%X = 0x%02X , if(0x%02X != 0x%02X) then: PC(next) = 0x%03X -> 0x%03X else: PC(next) = 0x%03X\n", chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->instruction.Y, chip8->V[chip8->instruction.Y], chip8->V[chip8->instruction.X], chip8->V[chip8->instruction.Y], chip8->PC, (chip8->PC) + 2, chip8->PC);
        break;
    }
    case 0xA:
    {
        // 0xANNN: Set index register I to NNN
        printf("Set I to 0x%03X\n", chip8->instruction.NNN);
        break;
    }
    case 0xB:
    {
        // 0xBNNN: Jumps to the address NNN plus V0.
        printf("Set PC to 0x%03X + V0(0x%02X)\n", chip8->instruction.NNN, chip8->V[0]);
        break;
    }
    case 0xC:
    {
        // 0xCXNN: Sets VX to the result of a bitwise and operation on a random number (Typically: 0 to 255) and NN.
        printf("Set V%X to rand() & 0x%02X\n", chip8->instruction.X, chip8->instruction.NN);
        break;
    }
    case 0XD:
    {
        // 0xDXYN: Draw a sprite at (Vx, Vy) that is width = 8 and height = N pixels.
        printf("Draw N(%u) height sprite at (V%x ->%d,V%x ->%d) from I(0x%03X)\n", chip8->instruction.N, chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->instruction.Y, chip8->V[chip8->instruction.Y], chip8->I);
        break;
    }
    case 0xE:
    {
        if (chip8->instruction.NN == 0x9E)
        {
            // 0xEX9E: Skips the next instruction if the key stored in VX(only consider the lowest nibble) is pressed (usually the next instruction is a jump to skip a code block).
            printf("Stored key in V%X = 0x%01X, Keypad[0x%01X] = %d , if(Keypad[0x%01X]) then: PC(next) = 0x%03X -> 0x%03X else: PC(next) = 0x%03X\n",
                   chip8->instruction.X, chip8->V[chip8->instruction.X] & 0x0F, chip8->V[chip8->instruction.X] & 0x0F, chip8->keypad[chip8->V[chip8->instruction.X] & 0x0F], chip8->V[chip8->instruction.X] & 0x0F, chip8->PC, (chip8->PC) + 2, chip8->PC);
        }
        else if (chip8->instruction.NN == 0xA1)
        {
            // 0XEXA1: Skips the next instruction if the key stored in VX(only consider the lowest nibble) is not pressed (usually the next instruction is a jump to skip a code block).[
            printf("Stored key in V%X = 0x%01X, Keypad[0x%01X] = %d , if(!Keypad[0x%01X]) then: PC(next) = 0x%03X -> 0x%03X else: PC(next) = 0x%03X\n",
                   chip8->instruction.X, chip8->V[chip8->instruction.X] & 0x0F, chip8->V[chip8->instruction.X] & 0x0F, chip8->keypad[chip8->V[chip8->instruction.X] & 0x0F], chip8->V[chip8->instruction.X] & 0x0F, chip8->PC, (chip8->PC) + 2, chip8->PC);
        }
        else
        {
            printf("0x%04X is an Invalid OPCODE\n", chip8->instruction.opcode);
        }
        break;
    }
    case 0xF:
    {
        switch (chip8->instruction.NN)
        {
        case 0x07:
        {
            // 0xFX07: Sets VX to the value of the delay timer.
            printf("Set V%X to Delay Timer -> V%X = %d \n", chip8->instruction.X, chip8->instruction.X, chip8->delay_timer);
            break;
        }
        case 0x0A: // Check
        {
            // 0xFX0A: A key press is awaited, and then stored in VX (blocking operation, all instruction halted until next key event, delay and sound timers should continue processing).
            printf("Waiting for any key to be pressed, then store it in V%X\n", chip8->instruction.X);
            break;
        }
        case 0x15:
        {
            // 0xFX15: Sets the delay timer to VX.
            printf("Set Delay Timer to V%X -> Delay timer = %d \n", chip8->instruction.X, chip8->V[chip8->instruction.X]);
            break;
        }
        case 0x18:
        {
            // 0xFX18: Sets the sound timer to VX.
            printf("Set Sound Timer to V%X -> Sound timer = %d \n", chip8->instruction.X, chip8->V[chip8->instruction.X]);
            break;
        }
        case 0x1E:
        {
            // 0xFX1E: Adds VX to I. VF is not affected.
            // chip8->I += chip8->V[chip8->instruction.X];
            break;
        }
        case 0x29:
        {
            // 0xFX29: Sets I to the location of the sprite for the character in VX(only consider the lowest nibble). Characters 0-F (in hexadecimal) are represented by a 4x5 font.
            // The font starts in 0x0. Each row takes 8 bits, which only 4 columns are used. And there are 5 rows for each char.
            printf("Set I to the sprite location in memory for char in V%X (0x%01X), I = (0X%03X)", chip8->instruction.X, chip8->V[chip8->instruction.X], chip8->V[chip8->instruction.X] * 5);
            break;
        }
        case 0x33:
        {
            // 0xFX33: Stores the binary-coded decimal representation of VX, with the hundreds digit in memory at location in I, the tens digit at location I+1, and the ones digit at location I+2.
            uint8_t vx = chip8->V[chip8->instruction.X];
            uint8_t h = vx / 100;
            uint8_t t = (vx / 10) % 10;
            uint8_t d = vx % 10;

            printf("Set the binary-coded decimal of V%X(0x%02X=%d) into I=(0x%03X): RAM[0x%03X]=%d, RAM[0x%03X]=%d, RAM[0x%03X]=%d\n",
                   chip8->instruction.X, vx, vx,
                   chip8->I,
                   chip8->I, h,
                   chip8->I + 1, t,
                   chip8->I + 2, d);
            break;
        }
        case 0x55:
        {
            // 0xFX55: Stores from V0 to VX (including VX) in memory, starting at address I.
            printf("Store from V0 to V%X into I: 0x%03X, 0x%03X+1, ... + 0x%03X+(%X) (Is I incremented? -> %d)\n", chip8->instruction.X, chip8->I, chip8->I, chip8->I, chip8->instruction.X, config->increment_i_on_0xFX);
            break;
        }
        case 0x65:
        {
            // 0xFX65: Fills from V0 to VX (including VX) with values from memory, starting at address I. The offset from I is increased by 1 for each value read, but I itself is left unmodified.
            printf("Store from I: 0x%03X, 0x%03X+1 ... 0x%03X+(%X) into V0 - V%X (Is  I incremented? -> %d)\n", chip8->I, chip8->I, chip8->I, chip8->instruction.X, chip8->instruction.X, config->instructions_per_second);
            break;
        }
        default:
            printf("0x%04X is an Invalid OPCODE\n", chip8->instruction.opcode);
            break;
        }
        break;
    }
    default:
        printf("NOT IMPLEMENTED\n");
        break; // Unimplemented or invalid opcode
    }
}
#endif

void emulate_instruction(chip8_t *chip8, const config_t *config)
{
    // Next opcode from ram (chip8 is Big endian)
    chip8->instruction.opcode = chip8->ram[chip8->PC] << 8 | chip8->ram[chip8->PC + 1];
    chip8->PC += 2;

    // Fill instruction format
    chip8->instruction.NNN = chip8->instruction.opcode & 0X0FFF;
    chip8->instruction.NN = chip8->instruction.opcode & 0X00FF;
    chip8->instruction.N = chip8->instruction.opcode & 0X000F;
    chip8->instruction.X = chip8->instruction.opcode >> 8 & 0X000F;
    chip8->instruction.Y = chip8->instruction.opcode >> 4 & 0X000F;

#ifdef DEBUG
    print_debug_info(chip8, config);
#endif

    // Emulate opcode
    switch (chip8->instruction.opcode >> 12)
    {
    case 0X0:
    {
        if (chip8->instruction.NN == 0XE0)
        {
            // 0X00E0 : Clear screen
            memset(&chip8->display[0], false, sizeof(chip8->display));
        }
        else if (chip8->instruction.NN == 0XEE)
        {
            // 0X00EE : Return from subroutine
            // Pop from the stack (-- first cause we ++ after pushing)
            chip8->stack_ptr--;
            chip8->PC = *chip8->stack_ptr;
        }

        break;
    }
    case 0x1:
    {
        // 0x1NNN: jump to address NNN
        chip8->PC = chip8->instruction.NNN;
        break;
    }
    case 0x2:
    {
        // 0X2NNN : Call subroutine at NNN
        *chip8->stack_ptr = chip8->PC; // Store current  (next of this one) PC
        chip8->stack_ptr++;
        chip8->PC = chip8->instruction.NNN;
        break;
    }
    case 0x3:
    {
        // 0x3XNN: if (Vx == NN) Skips the next instruction (usually the next instruction is a jump to skip a code block)
        if (chip8->V[chip8->instruction.X] == chip8->instruction.NN)
            chip8->PC += 2;
        break;
    }
    case 0x4:
    {
        // 0x4XNN: if (Vx != NN) Skips the next instruction (usually the next instruction is a jump to skip a code block)
        if (chip8->V[chip8->instruction.X] != chip8->instruction.NN)
            chip8->PC += 2;
        break;
    }
    case 0x5:
    {
        if (chip8->instruction.N != 0)
            break;
        // 0x5XY0: if (Vx == NN) Skips the next instruction (usually the next instruction is a jump to skip a code block)
        if (chip8->V[chip8->instruction.X] == chip8->V[chip8->instruction.Y])
            chip8->PC += 2;
        break;
    }
    case 0x6:
    {
        // 0x6XNN: Sets register VX to NN.
        chip8->V[chip8->instruction.X] = chip8->instruction.NN;
        break;
    }
    case 0x7:
    {
        // 0x7XNN: Sets register VX += NN.
        chip8->V[chip8->instruction.X] += chip8->instruction.NN;
        break;
    }
    case 0x8:
    {
        switch (chip8->instruction.N)
        {
        case 0:
            // 0x8XY0: Sets register VX = VY.
            chip8->V[chip8->instruction.X] = chip8->V[chip8->instruction.Y];
            break;
        case 1:
            // 0x8XY1: Sets VX to VX or VY (bitwise)
            chip8->V[chip8->instruction.X] |= chip8->V[chip8->instruction.Y];
            break;
        case 2:
            // 0x8XY2: Sets VX to VX and VY (bitwise)
            chip8->V[chip8->instruction.X] &= chip8->V[chip8->instruction.Y];
            break;
        case 3:
            // 0x8XY3: Sets VX to VX xor VY (bitwise)
            chip8->V[chip8->instruction.X] ^= chip8->V[chip8->instruction.Y];
            break;
        case 4:
            // 0x8XY4: Adds VY to VX. VF is set to 1 when there's an overflow, and to 0 when there is not.
            if ((u_int16_t)chip8->V[chip8->instruction.X] + chip8->V[chip8->instruction.Y] > 255)
            {
                chip8->V[0xF] = 1;
            }
            else
            {
                chip8->V[0xF] = 0;
            }
            chip8->V[chip8->instruction.X] += chip8->V[chip8->instruction.Y];
            break;
        case 5:
            // 0x8XY5: VY is subtracted from VX. VF is set to 0 when there's an underflow, and 1 when there is not. (i.e. VF set to 1 if VX >= VY and 0 if not)
            if (chip8->V[chip8->instruction.X] >= chip8->V[chip8->instruction.Y])
            {
                chip8->V[0xF] = 1;
            }
            else
            {
                chip8->V[0xF] = 0;
            }
            chip8->V[chip8->instruction.X] -= chip8->V[chip8->instruction.Y];
            break;
        case 6:
            // 0x8XY6: Shifts VX to the right by 1, then stores the least significant bit of VX prior to the shift into VF.
            // CHIP-8's opcodes 8XY6 and 8XYE (the bit shift instructions), which were in fact undocumented opcodes in the original interpreter, shifted the value in the register VY and stored the result in VX. The CHIP-48 and SCHIP implementations instead ignored VY, and simply shifted VX.[18]
            // TODO: Add to config if needed
            chip8->V[0xF] = chip8->V[chip8->instruction.X] & 0x01;
            chip8->V[chip8->instruction.X] >>= 1;
            break;
        case 7:
            // 0x8XY7: Sets VX to VY minus VX. VF is set to 0 when there's an underflow, and 1 when there is not. (i.e. VF set to 1 if VY >= VX).
            if (chip8->V[chip8->instruction.Y] >= chip8->V[chip8->instruction.X])
            {
                chip8->V[0xF] = 1;
            }
            else
            {
                chip8->V[0xF] = 0;
            }
            chip8->V[chip8->instruction.X] = chip8->V[chip8->instruction.Y] - chip8->V[chip8->instruction.X];
            break;
        case 0XE:
            // 0x8XYE: Shifts VX to the left by 1, then sets VF to 1 if the most significant bit of VX prior to that shift was set, or to 0 if it was unset.
            // CHIP-8's opcodes 8XY6 and 8XYE (the bit shift instructions), which were in fact undocumented opcodes in the original interpreter, shifted the value in the register VY and stored the result in VX. The CHIP-48 and SCHIP implementations instead ignored VY, and simply shifted VX.[18]
            // TODO: Add to config if needed
            chip8->V[0xF] = (chip8->V[chip8->instruction.X] & 0x80) >> 7;
            chip8->V[chip8->instruction.X] <<= 1;
            break;
        default:
            break;
        }
        break;
    }
    case 0x09:
    {
        if (chip8->instruction.N != 0)
            break;
        // Skips the next instruction if VX does not equal VY.(Usually the next instruction is a jump to skip a code block).case 0XA:
        if (chip8->V[chip8->instruction.X] != chip8->V[chip8->instruction.Y])
            chip8->PC += 2;

        break;
    }
    case 0xA:
    {
        // 0xANNN: Set index register I to NNN
        chip8->I = chip8->instruction.NNN;
        break;
    }
    case 0xB:
    {
        // 0xBNNN: Jumps to the address NNN plus V0.
        chip8->PC = chip8->instruction.NNN + chip8->V[0];
        break;
    }
    case 0xC:
    {
        // 0xCXNN: Sets VX to the result of a bitwise and operation on a random number (Typically: 0 to 255) and NN.
        chip8->V[chip8->instruction.X] = rand() % 256 & chip8->instruction.NN;
        break;
    }
    case 0xD:
    {
        // 0xDXYN: Draw a sprite at (Vx, Vy) that is width = 8 and height = N pixels.
        // Read from memory location I
        // VF (Carry flag) set to 1 if any of the XOR pixeles with the sprite are set off. (For collision could be used)
        uint8_t X = chip8->V[chip8->instruction.X] % config->window_width; // If it goes out of screen bounds it does a wrapping...it works like that
        uint8_t Y = chip8->V[chip8->instruction.Y] % config->window_height;
        const uint8_t original_X = X;

        chip8->V[0xF] = 0; // Initialize carry flag

        // For each byte we have 1 row
        for (uint8_t i = 0; i < chip8->instruction.N; i++)
        {
            uint8_t sprite_data = chip8->ram[chip8->I + i];

            X = original_X;

            for (int8_t j = 7; j >= 0; j--)
            {
                // If sprite pixel and display pixel is on set carry flag
                uint8_t sprite_bit = sprite_data >> j & 0x01; // Starts with the one furthes to the left (MSB), we isolate that bit
                uint8_t display_bit = chip8->display[Y * config->window_width + X];
                if (sprite_bit && display_bit)
                {
                    chip8->V[0xF] = 1;
                }
                // XOR on display
                chip8->display[Y * config->window_width + X] ^= sprite_bit;

                // If right edge stop writing
                if (++X >= config->window_width)
                    break;
            }
            if (++Y >= config->window_height)
                break;
        }
        break;
    }
    case 0xE:
    {
        if (chip8->instruction.NN == 0x9E)
        {
            // 0xEX9E: Skips the next instruction if the key stored in VX(only consider the lowest nibble) is pressed (usually the next instruction is a jump to skip a code block).
            const uint8_t stored_key = chip8->V[chip8->instruction.X] & 0x0F;
            if (chip8->keypad[stored_key])
            {
                chip8->PC += 2;
            }
        }
        else if (chip8->instruction.NN == 0xA1)
        {
            // 0XEXA1: Skips the next instruction if the key stored in VX(only consider the lowest nibble) is not pressed (usually the next instruction is a jump to skip a code block).[
            const uint8_t stored_key = chip8->V[chip8->instruction.X] & 0x0F;
            if (!chip8->keypad[stored_key])
            {
                chip8->PC += 2;
            }
        }
        break;
    }
    case 0xF:
    {
        switch (chip8->instruction.NN)
        {
        case 0x07:
        {
            // 0xFX07: Sets VX to the value of the delay timer.
            chip8->V[chip8->instruction.X] = chip8->delay_timer;
            break;
        }
        case 0x0A: // Check
        {
            // 0xFX0A: A key press is awaited, and then stored in VX (blocking operation, all instruction halted until next key event, delay and sound timers should continue processing).
            bool any_key_pressed = false;
            for (uint8_t i = 0; i < sizeof chip8->keypad; i++)
            {
                if (chip8->keypad[i])
                {
                    chip8->V[chip8->instruction.X] = i;
                    any_key_pressed = true;
                    break;
                }
            }
            // If no key pressed, we fetch again this instrucction
            if (!any_key_pressed)
                chip8->PC -= 2;
            break;
        }
        case 0x15:
        {
            // 0xFX15: Sets the delay timer to VX.
            chip8->delay_timer = chip8->V[chip8->instruction.X];
            break;
        }
        case 0x18:
        {
            // 0xFX18: Sets the sound timer to VX.
            chip8->sound_timer = chip8->V[chip8->instruction.X];
            break;
        }
        case 0x1E:
        {
            // 0xFX1E: Adds VX to I. VF is not affected.
            chip8->I += chip8->V[chip8->instruction.X];
            break;
        }
        case 0x29:
        {
            // 0xFX29: Sets I to the location of the sprite for the character in VX(only consider the lowest nibble). Characters 0-F (in hexadecimal) are represented by a 4x5 font.
            // The font starts in 0x0. Each row takes 8 bits, which only 4 columns are used. And there are 5 rows for each char.
            const uint8_t character = chip8->V[chip8->instruction.X] & 0x0F;
            const uint16_t sprite_location = character * 5;
            chip8->I = sprite_location;
            break;
        }
        case 0x33:
        {
            // 0xFX33: Stores the binary-coded decimal representation of VX, with the hundreds digit in memory at location in I, the tens digit at location I+1, and the ones digit at location I+2.
            uint8_t vx = chip8->V[chip8->instruction.X];
            chip8->ram[chip8->I] = (vx / 100);
            chip8->ram[chip8->I + 1] = (vx / 10) % 10;
            chip8->ram[chip8->I + 2] = (vx % 10);

            break;
        }
        case 0x55:
        {
            // 0xFX55: Stores from V0 to VX (including VX) in memory, starting at address I.
            // The offset from I is increased by 1 for each value written, but I itself is left unmodified.
            // In the original CHIP-8 implementation, and also in CHIP-48, I is left incremented after this instruction had been executed. In SCHIP, I is left unmodified.
            for (uint8_t x = 0; x <= chip8->instruction.X; x++)
            {
                chip8->ram[chip8->I + x] = chip8->V[x];
            }
            if (config->increment_i_on_0xFX)
            {
                chip8->I += chip8->instruction.X + 1;
            }
            break;
        }
        case 0x65:
        {
            // 0xFX65: Fills from V0 to VX (including VX) with values from memory, starting at address I. The offset from I is increased by 1 for each value read, but I itself is left unmodified.
            // The offset from I is increased by 1 for each value written, but I itself is left unmodified.
            // In the original CHIP-8 implementation, and also in CHIP-48, I is left incremented after this instruction had been executed. In SCHIP, I is left unmodified.
            // TODO: Right now im using SCHIP, left unmodified, could add to config the other option in case is needed
            for (uint8_t x = 0; x <= chip8->instruction.X; x++)
            {
                chip8->V[x] = chip8->ram[chip8->I + x];
            }
            if (config->increment_i_on_0xFX)
            {
                chip8->I += chip8->instruction.X + 1;
            }
            break;
        }
        default:
            break;
        }
    }
    default:
        break;
    }
}

void update_timers(chip8_t *chip8, sdl_t *sdl)
{
    if (chip8->delay_timer)
        chip8->delay_timer--;
    if (chip8->sound_timer > 0)
    {
        chip8->sound_timer--;
        SDL_PauseAudioDevice(sdl->device, 0); // Play sound
    }
    else
    {
        SDL_PauseAudioDevice(sdl->device, 1); // Pause sound
    }
}

int main(int argc, char **argv)
{
    // Default use of args
    if (argc < 2)
    {
        fprintf(stderr, "Usage %s <rom>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    // Initialize emulator config
    config_t config = {0};
    if (!set_config_from_args(&config, argc, argv))
        exit(EXIT_FAILURE);

    // Initialize SDL
    sdl_t sdl = {0};
    if (!init_sdl(&sdl, &config))
        exit(EXIT_FAILURE);

    // Initializa CHIP8 MACHINE
    chip8_t chip8 = {0};
    const char *rom_name = argv[1];
    if (!init_chip8(&chip8, rom_name))
        exit(EXIT_FAILURE);

    // Initial screen clear
    clear_screen(&sdl, &config);

    // Prepare rand
    srand(time(NULL));

    // Main emulator loop
    while (chip8.state != QUIT)
    {
        // Handle User input
        handle_input(&chip8);

        if (chip8.state == PAUSED)
            continue;

        // Get time
        const uint64_t start_time = SDL_GetPerformanceCounter();

        // In this frame emulate config.instructions_per_second / 60 instructions (60hz)
        for (uint32_t i = 0; i < config.instructions_per_second / 60; i++)
        {
            emulate_instruction(&chip8, &config);
        }
        // Get time after peforming instructions
        const uint64_t finish_time = SDL_GetPerformanceCounter();

        // Delay fo aprox 60fs (1000ms/60 ~= 16)
        double time_elapsed_ms = (double)((finish_time - start_time) * 1000) / SDL_GetPerformanceFrequency();
        if (16.67f > time_elapsed_ms)
        {
            SDL_Delay((uint32_t)(16.67f - time_elapsed_ms));
        }
        // Update with changes
        update_screen(sdl, &config, &chip8);
        // Update timers (delay and sound)
        update_timers(&chip8, &sdl);

        if (chip8.PC > 4096)
        {
            fprintf(stderr, "Reach end PC:%d is out of bounds\n", chip8.PC);
            break;
        }
    }

    // Final cleanup
    final_cleanup(&sdl);

    exit(EXIT_SUCCESS);
}