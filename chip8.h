#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    QUIT,
    RUNNING,
    PAUSED
} emulator_state_t;

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

bool init_chip8(chip8_t *chip8, const char *rom_name);