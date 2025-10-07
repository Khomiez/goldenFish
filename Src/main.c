/* ============================================================================
 * Simon Says Game - chat stupid version
 * ============================================================================ */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h> // For vsnprintf

#define STM32F411xE
#include "stm32f4xx.h"
#include <stdio.h> // For vsnprintf

/* ============================================================================
 * HARDWARE PIN DEFINITIONS
 * ============================================================================ */
#define LED1_PORT           GPIOA
#define LED1_PIN            5
#define LED2_PORT           GPIOA
#define LED2_PIN            6
#define LED3_PORT           GPIOA
#define LED3_PIN            7
#define LED4_PORT           GPIOB
#define LED4_PIN            6

// Button 0: PA10 (D2) → LED PA5 (Blue/D13)
#define BTN0_PORT           GPIOA
#define BTN0_PIN            10
// Button 1: PB3 (D3) → LED PA6 (Red/D12)
#define BTN1_PORT           GPIOB
#define BTN1_PIN            3
// Button 2: PB4 (D5) → LED PA7 (Yellow/D11)
#define BTN2_PORT           GPIOB
#define BTN2_PIN            5
// Button 3: PA8 (D7) → LED PB6 (Green/D10)
#define BTN3_PORT           GPIOB
#define BTN3_PIN            4

#define POT_PIN             4
#define TEMP_PIN            1
#define LIGHT_PIN           0

#define BCD_2_0_PORT        GPIOC
#define BCD_2_0_PIN         7
#define BCD_2_1_PORT        GPIOA
#define BCD_2_1_PIN         8
#define BCD_2_2_PORT        GPIOB
#define BCD_2_2_PIN         10
#define BCD_2_3_PORT        GPIOA
#define BCD_2_3_PIN         9

/* ============================================================================
 * GAME AND MONITORING CONFIGURATION
 * ============================================================================ */
#define BUTTON_DEBOUNCE_MS      50
#define LONG_PRESS_DURATION_MS  2000
#define INITIAL_LIVES           4

/* ============================================================================
 * TYPE DEFINITIONS
 * ============================================================================ */

typedef struct {
    uint8_t current_state;
    uint8_t previous_state;
    uint32_t last_change_time;
} ButtonState_t;

typedef enum {
    GAME_STATE_BOOT,
    GAME_STATE_DIFFICULTY_SELECT,
    GAME_STATE_LEVEL_INTRO,
    GAME_STATE_PATTERN_DISPLAY,
    GAME_STATE_INPUT_WAIT,
    GAME_STATE_RESULT_PROCESS,
    GAME_STATE_VICTORY,
    GAME_STATE_GAME_DEATH
} GameState_t;

/* ============================================================================
 * GLOBAL VARIABLES
 * ============================================================================ */

// System & Hardware State
uint32_t SystemCoreClock = 84000000;
// Use ButtonState_t for button states
ButtonState_t g_buttons[4];
// System initialization flag
uint8_t g_system_initialized = 0;
// ADC values for POT, TEMP, LIGHT
uint16_t g_adc_values[3] = {0};
// Tick counter for SysTick
volatile uint32_t g_tick_counter = 0;
// Current ADC channel index
uint8_t g_current_adc_channel = 0;

// Game State
GameState_t g_game_state;
uint8_t  g_difficulty;
uint8_t  g_level;
uint32_t g_score;
uint8_t  g_lives;
uint32_t g_state_entry_time;
// Difficulty lock flag
uint8_t g_difficulty_locked = 0;
// Pattern and game logic variables
#define MAX_PATTERN_LENGTH 32
// Button-to-LED mapping: index = button, value = LED bit
// Button index: 0=PA10, 1=PB3, 2=PB4, 3=PA8
// LED index:    0=PA5,  1=PA6, 2=PA7, 3=PB6
const uint8_t button_to_led_map[4] = {0, 1, 2, 3};
uint8_t g_pattern[MAX_PATTERN_LENGTH] = {0};
uint8_t g_pattern_length = 0;
uint8_t g_pattern_index = 0;
uint8_t g_input_index = 0;
uint8_t g_input_correct = 1;
// For logging only once per state
GameState_t g_last_state_logged = (GameState_t)-1;

/* ============================================================================
 * FUNCTION PROTOTYPES
 * ============================================================================ */

// Game Logic Functions
void Game_Init(void);
void Game_Run(void);
static void set_game_state(GameState_t new_state);
static void handle_boot(void);
static void handle_difficulty_select(void);
static void handle_level_intro(void);
static void handle_pattern_display(void);
static void handle_input_wait(void);
static void handle_result_process(void);
static void handle_victory(void);
static void handle_game_death(void);

// Hardware & System Functions
void SystemClock_Config(void);
void GPIO_Init(void);
void ADC_Init(void);
void USART2_Init(void);
void NVIC_Init(void);
void Monitor_Buttons(void);
void Monitor_ADC(void);
void LED_SetPattern(uint8_t pattern);
void SevenSeg_Display(uint8_t digit);
void ADC_StartConversion(void);
void Delay_ms(uint32_t ms);
uint32_t GetTick(void);
void Log_Print(const char* format, ...);

/* ============================================================================
 * MAIN ENTRY POINT
 * ============================================================================ */

int main(void) {
    // 1. Initialize all microcontroller peripherals
    SystemClock_Config();
    GPIO_Init();
    USART2_Init();
    SysTick_Config(SystemCoreClock / 1000); // 1ms ticks
    NVIC_Init();
    ADC_Init();

    // Enable Log_Print() now that UART is ready
    g_system_initialized = 1;

    // Start the first ADC conversion
    ADC_StartConversion();
    Delay_ms(10); // Allow ADC to stabilize

    // 2. Initialize the game logic
    Game_Init();

    // 3. Main application loop
    while(1) {
        // Continuously monitor hardware inputs
        Monitor_Buttons();
        Monitor_ADC();

        // Run the game's state machine
        Game_Run();

        // Small delay to keep loop from running too fast
        Delay_ms(5);
    }
}

/* ============================================================================
 * GAME LOGIC IMPLEMENTATION
 * ============================================================================ */

void Game_Init(void) {
    Log_Print("\r\n[GAME] Initializing Simon Game...\r\n");
    uint32_t seed = g_adc_values[1] + g_adc_values[2] + GetTick();
    srand(seed);
    Log_Print("[GAME] Random seed set to: %lu\r\n", seed);
    set_game_state(GAME_STATE_BOOT);
}

void Game_Run(void) {
    if (g_last_state_logged != g_game_state) {
        switch(g_game_state) {
            case GAME_STATE_BOOT:
                Log_Print("[STATE] -> BOOT\r\n");
                Log_Print("Welcome! Use the potentiometer to select difficulty, then press and hold any button to confirm.\r\n");
                break;
            case GAME_STATE_DIFFICULTY_SELECT:
                Log_Print("[STATE] -> DIFFICULTY_SELECT\r\n");
                Log_Print("Select difficulty with potentiometer. Press and hold any button to confirm.\r\n");
                break;
            case GAME_STATE_LEVEL_INTRO:
                Log_Print("[STATE] -> LEVEL_INTRO\r\n");
                Log_Print("Get ready! Watch the LED pattern.\r\n");
                break;
            case GAME_STATE_PATTERN_DISPLAY:
                Log_Print("[STATE] -> PATTERN_DISPLAY\r\n");
                Log_Print("Memorize the pattern!\r\n");
                break;
            case GAME_STATE_INPUT_WAIT:
                Log_Print("[STATE] -> INPUT_WAIT\r\n");
                Log_Print("Repeat the pattern using the buttons.\r\n");
                break;
            case GAME_STATE_RESULT_PROCESS:
                Log_Print("[STATE] -> RESULT_PROCESS\r\n");
                Log_Print("Checking your answer...\r\n");
                break;
            case GAME_STATE_VICTORY:
                Log_Print("[STATE] -> VICTORY\r\n");
                Log_Print("Congratulations! You won!\r\n");
                break;
            case GAME_STATE_GAME_DEATH:
                Log_Print("[STATE] -> GAME_DEATH\r\n");
                Log_Print("Game over! Try again.\r\n");
                break;
        }
        g_last_state_logged = g_game_state;
    }
    switch(g_game_state) {
        case GAME_STATE_BOOT:              handle_boot();              break;
        case GAME_STATE_DIFFICULTY_SELECT: handle_difficulty_select(); break;
        case GAME_STATE_LEVEL_INTRO:       handle_level_intro();       break;
        case GAME_STATE_PATTERN_DISPLAY:   handle_pattern_display();   break;
        case GAME_STATE_INPUT_WAIT:        handle_input_wait();        break;
        case GAME_STATE_RESULT_PROCESS:    handle_result_process();    break;
        case GAME_STATE_VICTORY:           handle_victory();          break;
        case GAME_STATE_GAME_DEATH:        handle_game_death();       break;
        default:
            Log_Print("[TODO] State %d not yet implemented!\r\n", g_game_state);
            set_game_state(GAME_STATE_DIFFICULTY_SELECT); // Go back for now
            Delay_ms(1000);
            break;
    }
}

// Helper: Generate random pattern
static void generate_pattern(uint8_t length) {
    for (uint8_t i = 0; i < length; i++) {
        g_pattern[i] = rand() % 4; // 0-3 for 4 buttons/LEDs
    }
    g_pattern_length = length;
}

// Helper: Show one LED
static void show_led(uint8_t idx) {
    // Map button index to correct LED bit
    LED_SetPattern(1 << button_to_led_map[idx]);
}

// Helper: Turn off all LEDs
static void clear_leds(void) {
    LED_SetPattern(0);
}

static void handle_level_intro(void) {
    Log_Print("Level %u. Lives: %u. Score: %lu\r\n", g_level, g_lives, g_score);
    Log_Print("Memorize the pattern!\r\n");
    Delay_ms(1000);
    // Generate new pattern for this level
    generate_pattern(g_level + g_difficulty - 1); // pattern length increases
    g_pattern_index = 0;
    set_game_state(GAME_STATE_PATTERN_DISPLAY);
}

static void handle_pattern_display(void) {
    // Show each LED in pattern sequence
    if (g_pattern_index < g_pattern_length) {
        show_led(g_pattern[g_pattern_index]);
        Delay_ms(500);
        clear_leds();
        Delay_ms(250);
        g_pattern_index++;
    } else {
        g_pattern_index = 0;
        g_input_index = 0;
        g_input_correct = 1;
        set_game_state(GAME_STATE_INPUT_WAIT);
    }
}

static void handle_input_wait(void) {
//    Log_Print("Repeat the pattern by pressing the buttons!\r\n");
    // Wait for user input, one button at a time
    if (g_input_index < g_pattern_length) {
        for (int i = 0; i < 4; i++) {
            if (g_buttons[i].current_state == 1 && g_buttons[i].previous_state == 0) {
                // Button pressed
                show_led(i);
                Delay_ms(200);
                clear_leds();
                if (i != g_pattern[g_input_index]) {
                    g_input_correct = 0;
                }
                g_input_index++;
                break;
            }
        }
    } else {
        set_game_state(GAME_STATE_RESULT_PROCESS);
    }
}

static void handle_result_process(void) {
    if (g_input_correct) {
        Log_Print("Correct!\r\n");
        g_score += 10 * g_level * g_difficulty;
        g_level++;
        if (g_level > 5) {
            set_game_state(GAME_STATE_VICTORY);
        } else {
            set_game_state(GAME_STATE_LEVEL_INTRO);
        }
    } else {
        Log_Print("Incorrect!\r\n");
        if (g_lives > 0) g_lives--;
        if (g_lives == 0) {
            set_game_state(GAME_STATE_GAME_DEATH);
        } else {
            Log_Print("Try again!\r\n");
            set_game_state(GAME_STATE_LEVEL_INTRO);
        }
    }
}

static void handle_victory(void) {
    Log_Print("Congratulations! You completed all levels! Final Score: %lu\r\n", g_score);
    Log_Print("Press any button to restart.\r\n");
    for (int i = 0; i < 4; i++) {
        if (g_buttons[i].current_state == 1 && g_buttons[i].previous_state == 0) {
            g_level = 1;
            g_score = 0;
            g_lives = INITIAL_LIVES;
            g_difficulty_locked = 0;
            set_game_state(GAME_STATE_DIFFICULTY_SELECT);
            break;
        }
    }
}

static void handle_game_death(void) {
    Log_Print("Game Over! Final Score: %lu\r\n", g_score);
    Log_Print("Press any button to restart.\r\n");
    for (int i = 0; i < 4; i++) {
        if (g_buttons[i].current_state == 1 && g_buttons[i].previous_state == 0) {
            g_level = 1;
            g_score = 0;
            g_lives = INITIAL_LIVES;
            g_difficulty_locked = 0;
            set_game_state(GAME_STATE_DIFFICULTY_SELECT);
            break;
        }
    }
}


static void set_game_state(GameState_t new_state) {
    g_game_state = new_state;
    g_state_entry_time = GetTick();
    switch(new_state) {
        case GAME_STATE_BOOT:
            Log_Print("[STATE] -> BOOT\r\n");
            Log_Print("Welcome! Use the potentiometer to select difficulty, then press and hold any button to confirm.\r\n");
            break;
        case GAME_STATE_DIFFICULTY_SELECT:
            Log_Print("[STATE] -> DIFFICULTY_SELECT\r\n");
            Log_Print("Select difficulty with potentiometer. Press and hold any button to confirm.\r\n");
            break;
        case GAME_STATE_LEVEL_INTRO:
            Log_Print("[STATE] -> LEVEL_INTRO\r\n");
            Log_Print("Get ready! Watch the LED pattern.\r\n");
            break;
        case GAME_STATE_PATTERN_DISPLAY:
            Log_Print("[STATE] -> PATTERN_DISPLAY\r\n");
            Log_Print("Memorize the pattern!\r\n");
            break;
        case GAME_STATE_INPUT_WAIT:
            Log_Print("[STATE] -> INPUT_WAIT\r\n");
            Log_Print("Repeat the pattern using the buttons.\r\n");
            break;
        case GAME_STATE_RESULT_PROCESS:
            Log_Print("[STATE] -> RESULT_PROCESS\r\n");
            Log_Print("Checking your answer...\r\n");
            break;
        case GAME_STATE_VICTORY:
            Log_Print("[STATE] -> VICTORY\r\n");
            Log_Print("Congratulations! You won!\r\n");
            break;
        case GAME_STATE_GAME_DEATH:
            Log_Print("[STATE] -> GAME_DEATH\r\n");
            Log_Print("Game over! Try again.\r\n");
            break;
    }
}

static void handle_boot(void) {
    Log_Print("[BOOT] Welcome to Simon!\r\n");
    g_level = 1;
    g_score = 0;
    g_lives = INITIAL_LIVES;
    Log_Print("[GAME] Level:%u, Lives:%u, Score:%lu\r\n", g_level, g_lives, g_score);
    set_game_state(GAME_STATE_DIFFICULTY_SELECT);
}

static void handle_difficulty_select(void) {
    uint32_t current_time = GetTick();
    static uint32_t last_log_time = 0;
    static uint8_t last_difficulty = 0;

    if (!g_difficulty_locked) {
        uint16_t pot_value = g_adc_values[0];
        if (pot_value < 256) {
            g_difficulty = 1;
        } else if (pot_value < 512) {
            g_difficulty = 2;
        } else if (pot_value < 768) {
            g_difficulty = 3;
        } else {
            g_difficulty = 4;
        }
        SevenSeg_Display(g_difficulty);
        if (g_difficulty != last_difficulty || (current_time - last_log_time) > 1000) {
            Log_Print("[DIFFICULTY] Pot value: %u -> Difficulty: %u\r\n", pot_value, g_difficulty);
            last_log_time = current_time;
            last_difficulty = g_difficulty;
        }
        for (int i = 0; i < 4; i++) {
            if (g_buttons[i].current_state == 1) {
                if ((current_time - g_buttons[i].last_change_time) >= LONG_PRESS_DURATION_MS) {
                    g_difficulty_locked = 1;
                    Log_Print("[GAME] Difficulty %u confirmed by holding BTN%d!\r\n", g_difficulty, i + 1);
                    Log_Print("Difficulty locked. Get ready for the game!\r\n");
                    set_game_state(GAME_STATE_LEVEL_INTRO);
                    return;
                }
            }
        }
    } else {
        // Difficulty is locked, do not read or log potentiometer
        SevenSeg_Display(g_difficulty); // Still show locked difficulty
    }
}

/* ============================================================================
 * INTERRUPT HANDLERS
 * ============================================================================ */

void SysTick_Handler(void) {
    g_tick_counter++;
}

void ADC_IRQHandler(void) {
    if(ADC1->SR & ADC_SR_EOC) {
        g_adc_values[g_current_adc_channel] = ADC1->DR;
        g_current_adc_channel = (g_current_adc_channel + 1) % 3;
        ADC1->SQR3 = (ADC1->SQR3 & ~ADC_SQR3_SQ1) | (g_current_adc_channel == 0 ? POT_PIN : (g_current_adc_channel == 1 ? TEMP_PIN : LIGHT_PIN));
        ADC1->CR2 |= ADC_CR2_SWSTART;
    }
}

/* ============================================================================
 * HARDWARE MONITORING FUNCTIONS
 * ============================================================================ */

void Monitor_Buttons(void) {
    uint32_t current_time = GetTick();
    uint8_t readings[4] = {
        !(BTN0_PORT->IDR & (1 << BTN0_PIN)),
        !(BTN1_PORT->IDR & (1 << BTN1_PIN)),
        !(BTN2_PORT->IDR & (1 << BTN2_PIN)),
        !(BTN3_PORT->IDR & (1 << BTN3_PIN))
    };

    for(int i = 0; i < 4; i++) {
        g_buttons[i].previous_state = g_buttons[i].current_state;
        g_buttons[i].current_state = readings[i];

        if(g_buttons[i].current_state != g_buttons[i].previous_state) {
            if((current_time - g_buttons[i].last_change_time) >= BUTTON_DEBOUNCE_MS) {
                g_buttons[i].last_change_time = current_time;
                if(g_buttons[i].current_state == 1) {
                    Log_Print("[BTN%d] PRESSED\r\n", i + 1);
                } else {
                    Log_Print("[BTN%d] RELEASED\r\n", i + 1);
                }
            }
        }
    }
}

void Monitor_ADC(void) { /* ADC is handled by interrupt, this can be a placeholder */ }

/* ============================================================================
 * HARDWARE CONTROL FUNCTIONS
 * ============================================================================ */

void LED_SetPattern(uint8_t pattern) {
    (pattern & 0x01) ? (LED1_PORT->BSRR = (1 << LED1_PIN)) : (LED1_PORT->BSRR = (1 << (LED1_PIN + 16)));
    (pattern & 0x02) ? (LED2_PORT->BSRR = (1 << LED2_PIN)) : (LED2_PORT->BSRR = (1 << (LED2_PIN + 16)));
    (pattern & 0x04) ? (LED3_PORT->BSRR = (1 << LED3_PIN)) : (LED3_PORT->BSRR = (1 << (LED3_PIN + 16)));
    (pattern & 0x08) ? (LED4_PORT->BSRR = (1 << LED4_PIN)) : (LED4_PORT->BSRR = (1 << (LED4_PIN + 16)));
}

void SevenSeg_Display(uint8_t digit) {
    if(digit > 9) return;
    (digit & 0x01) ? (BCD_2_0_PORT->BSRR = (1 << BCD_2_0_PIN)) : (BCD_2_0_PORT->BSRR = (1 << (BCD_2_0_PIN + 16)));
    (digit & 0x02) ? (BCD_2_1_PORT->BSRR = (1 << BCD_2_1_PIN)) : (BCD_2_1_PORT->BSRR = (1 << (BCD_2_1_PIN + 16)));
    (digit & 0x04) ? (BCD_2_2_PORT->BSRR = (1 << BCD_2_2_PIN)) : (BCD_2_2_PORT->BSRR = (1 << (BCD_2_2_PIN + 16)));
    (digit & 0x08) ? (BCD_2_3_PORT->BSRR = (1 << BCD_2_3_PIN)) : (BCD_2_3_PORT->BSRR = (1 << (BCD_2_3_PIN + 16)));
}

/* ============================================================================
 * SYSTEM INITIALIZATION
 * ============================================================================ */

void SystemClock_Config(void) {
    RCC->CR |= RCC_CR_HSION;
    while(!(RCC->CR & RCC_CR_HSIRDY));
    RCC->PLLCFGR = (RCC_PLLCFGR_PLLSRC_HSI) | (16 << RCC_PLLCFGR_PLLM_Pos) | (168 << RCC_PLLCFGR_PLLN_Pos) | (0 << RCC_PLLCFGR_PLLP_Pos);
    RCC->CR |= RCC_CR_PLLON;
    while(!(RCC->CR & RCC_CR_PLLRDY));
    FLASH->ACR = FLASH_ACR_LATENCY_2WS;
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1;
    RCC->CFGR |= RCC_CFGR_SW_PLL;
    while((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
    SystemCoreClock = 84000000;
}

void GPIO_Init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    // LEDs
    GPIOA->MODER |= (1 << (LED1_PIN*2)) | (1 << (LED2_PIN*2)) | (1 << (LED3_PIN*2));
    GPIOB->MODER |= (1 << (LED4_PIN*2));
    // Buttons
    GPIOA->PUPDR |= (1 << (BTN0_PIN*2)) | (1 << (BTN3_PIN*2));
    GPIOB->PUPDR |= (1 << (BTN1_PIN*2)) | (1 << (BTN2_PIN*2));
    // ADC
    GPIOA->MODER |= (3 << (POT_PIN*2)) | (3 << (TEMP_PIN*2)) | (3 << (LIGHT_PIN*2));
    // UART
    GPIOA->MODER |= (2 << (2*2)) | (2 << (3*2));
    GPIOA->AFR[0] |= (7 << (2*4)) | (7 << (3*4));
    // 7-Segment (BCD inputs) — ตั้งตามพอร์ตจริง
    GPIOC->MODER = (GPIOC->MODER & ~(3U << (BCD_2_0_PIN*2))) | (1U << (BCD_2_0_PIN*2));   // PC7
    GPIOA->MODER = (GPIOA->MODER & ~((3U << (BCD_2_1_PIN*2)) | (3U << (BCD_2_3_PIN*2))))
                                 |  (1U << (BCD_2_1_PIN*2)) | (1U << (BCD_2_3_PIN*2));     // PA8, PA9
    GPIOB->MODER = (GPIOB->MODER & ~(3U << (BCD_2_2_PIN*2))) | (1U << (BCD_2_2_PIN*2));   // PB10
}

void ADC_Init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    ADC1->CR2 |= ADC_CR2_ADON;
    ADC1->CR1 |= ADC_CR1_EOCIE;
    ADC1->CR1 |= (1 << ADC_CR1_RES_Pos); // 10-bit
    ADC1->SMPR2 |= (7 << ADC_SMPR2_SMP0_Pos) | (7 << ADC_SMPR2_SMP1_Pos) | (7 << ADC_SMPR2_SMP4_Pos);
    Delay_ms(2);
}

void USART2_Init(void) {
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    USART2->BRR = 0x16C; // 115200 baud @ 42MHz
    USART2->CR1 |= USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

void NVIC_Init(void) {
    NVIC_EnableIRQ(ADC_IRQn);
    NVIC_SetPriority(ADC_IRQn, 1);
    NVIC_SetPriority(SysTick_IRQn, 0);
}

void ADC_StartConversion(void) {
    ADC1->SQR3 = (ADC1->SQR3 & ~ADC_SQR3_SQ1) | POT_PIN;
    ADC1->CR2 |= ADC_CR2_SWSTART;
}

/* ============================================================================
 * UTILITY FUNCTIONS
 * ============================================================================ */

void Delay_ms(uint32_t ms) {
    uint32_t start = GetTick();
    while((GetTick() - start) < ms);
}

uint32_t GetTick(void) {
    return g_tick_counter;
}

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
