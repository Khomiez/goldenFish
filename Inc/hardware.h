/* ============================================================================
 * Hardware Control
 * Low-level hardware initialization and control functions
 * ============================================================================ */

#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdint.h>
#include "config.h"

/* Global Variables */
extern uint32_t SystemCoreClock;
extern ButtonState_t g_buttons[4];
extern uint16_t g_adc_values[3];
extern uint8_t g_current_adc_channel;

/* Function Prototypes */
void SystemClock_Config(void);
void GPIO_Init(void);
void ADC_Init(void);
void USART2_Init(void);
void NVIC_Init(void);
void ADC_StartConversion(void);

void Monitor_Buttons(void);
void Monitor_ADC(void);
void LED_SetPattern(uint8_t pattern);
void SevenSeg_Display(uint8_t digit);

#endif /* HARDWARE_H */
