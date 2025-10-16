/* ============================================================================
 * Utility Functions Implementation
 * ============================================================================ */

#include "utils.h"
#include "game.h"
#include "hardware.h"
#include <stdarg.h>
#include <stdio.h>

#define STM32F411xE
#include "stm32f4xx.h"

/* Global Variables */
volatile uint32_t g_tick_counter = 0;
uint8_t g_system_initialized = 0;

/* ============================================================================
 * Timing Functions
 * ============================================================================ */
void Delay_ms(uint32_t ms) {
    uint32_t start = GetTick();
    while((GetTick() - start) < ms);
}

uint32_t GetTick(void) {
    return g_tick_counter;
}

/* ============================================================================
 * Logging Functions
 * ============================================================================ */
void Log_Print(const char* format, ...) {
    if(!g_system_initialized) return;
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    for(char* p = buffer; *p; p++) {
        while(!(USART2->SR & USART_SR_TXE));
        USART2->DR = *p;
    }
}

/* ============================================================================
 * Debug Functions
 * ============================================================================ */
void Debug_PrintGameState(void) {
    if(!g_system_initialized) return;

    Log_Print("\r\n");
    Log_Print("========================================\r\n");
    Log_Print("        GAME DEBUG INFO\r\n");
    Log_Print("========================================\r\n");
    Log_Print("Time: %lu ms\r\n", GetTick());
    Log_Print("\r\n");

    // Game State
    const char* state_name = "UNKNOWN";
    switch(g_game_state) {
        case GAME_STATE_BOOT:              state_name = "BOOT"; break;
        case GAME_STATE_DIFFICULTY_SELECT: state_name = "DIFFICULTY_SELECT"; break;
        case GAME_STATE_LEVEL_INTRO:       state_name = "LEVEL_INTRO"; break;
        case GAME_STATE_PATTERN_DISPLAY:   state_name = "PATTERN_DISPLAY"; break;
        case GAME_STATE_INPUT_WAIT:        state_name = "INPUT_WAIT"; break;
        case GAME_STATE_RESULT_PROCESS:    state_name = "RESULT_PROCESS"; break;
        case GAME_STATE_VICTORY:           state_name = "VICTORY"; break;
        case GAME_STATE_GAME_DEATH:        state_name = "GAME_DEATH"; break;
    }
    Log_Print("State: %s\r\n", state_name);
    Log_Print("\r\n");

    // Game Progress
    Log_Print("--- Game Progress ---\r\n");
    Log_Print("Level:      %u / 9\r\n", g_level);
    Log_Print("Lives:      %u / %u\r\n", g_lives, INITIAL_LIVES);
    Log_Print("Score:      %lu\r\n", g_score);
    Log_Print("Difficulty: %u (1-5)\r\n", g_difficulty);
    Log_Print("\r\n");

    // Pattern Info
    Log_Print("--- Pattern Info ---\r\n");
    Log_Print("Pattern Length: %u\r\n", g_pattern_length);
    Log_Print("Pattern Index:  %u\r\n", g_pattern_index);
    Log_Print("Pattern: [");
    for(uint8_t i = 0; i < g_pattern_length && i < MAX_PATTERN_LENGTH; i++) {
        Log_Print("%u", g_pattern[i]);
        if(i < g_pattern_length - 1) Log_Print(", ");
    }
    Log_Print("]\r\n");
    Log_Print("Input Index:    %u\r\n", g_input_index);
    Log_Print("\r\n");

    // Button States
    Log_Print("--- Button States ---\r\n");
    const char* btn_names[4] = {"BLUE", "RED", "YELLOW", "GREEN"};
    for(int i = 0; i < 4; i++) {
        Log_Print("BTN %s: %s (prev: %s)\r\n",
                  btn_names[i],
                  g_buttons[i].current_state ? "PRESSED" : "RELEASED",
                  g_buttons[i].previous_state ? "PRESSED" : "RELEASED");
    }
    Log_Print("\r\n");

    // ADC Values
    Log_Print("--- ADC Values ---\r\n");
    Log_Print("POT (Speed):   %u / 1023\r\n", g_adc_values[0]);
    Log_Print("ADC Channel 1: %u / 1023\r\n", g_adc_values[1]);
    Log_Print("ADC Channel 2: %u / 1023\r\n", g_adc_values[2]);
    Log_Print("Current Chan:  %u\r\n", g_current_adc_channel);
    Log_Print("\r\n");

    Log_Print("========================================\r\n");
    Log_Print("\r\n");
}

/* ============================================================================
 * Interrupt Handler
 * ============================================================================ */
void SysTick_Handler(void) {
    g_tick_counter++;
}
