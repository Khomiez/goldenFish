/* ============================================================================
 * Utility Functions Implementation
 * ============================================================================ */

#include "utils.h"
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
 * Interrupt Handler
 * ============================================================================ */
void SysTick_Handler(void) {
    g_tick_counter++;
}
