/* ============================================================================
 * Utility Functions
 * Timing and logging utilities
 * ============================================================================ */

#ifndef UTILS_H
#define UTILS_H

#include <stdint.h>

/* Global Variables */
extern volatile uint32_t g_tick_counter;
extern uint8_t g_system_initialized;

/* Function Prototypes */
void Delay_ms(uint32_t ms);
uint32_t GetTick(void);
void Log_Print(const char* format, ...);

#endif /* UTILS_H */
