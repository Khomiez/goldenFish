/* ============================================================================
 * Simon Says Game + OLED HUD (SSD1306/SH1106 via I2C1)
 * Main Entry Point
 * ============================================================================ */

#include <stdint.h>

#define STM32F411xE
#include "stm32f4xx.h"

#include "config.h"
#include "hardware.h"
#include "oled.h"
#include "game.h"
#include "utils.h"

/* ============================================================================
 * Main Function
 * ============================================================================ */
int main(void) {
    // Initialize hardware
    SystemClock_Config();
    GPIO_Init();
    USART2_Init();
    SysTick_Config(SystemCoreClock / 1000); // 1ms ticks
    NVIC_Init();
    ADC_Init();
    Buzzer_Init();

    // Initialize OLED display
    oled_init();
    oled_clear();

    // Mark system as initialized
    g_system_initialized = 1;

    // Start ADC conversions
    ADC_StartConversion();
    Delay_ms(10);

    // Initialize game
    Game_Init();

    // Main loop
    while(1) {
        Monitor_Buttons();
        Monitor_ADC();
        Game_Run();
        Delay_ms(5);
    }
}
