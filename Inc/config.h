/* ============================================================================
 * Hardware Configuration
 * Pin definitions and constants for the Simon Says Game
 * ============================================================================ */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

/* LED Pin Definitions */
#define LED1_PORT           GPIOA
#define LED1_PIN            5
#define LED2_PORT           GPIOA
#define LED2_PIN            6
#define LED3_PORT           GPIOA
#define LED3_PIN            7
#define LED4_PORT           GPIOB
#define LED4_PIN            6

/* Button Pin Definitions */
#define BTN0_PORT           GPIOA
#define BTN0_PIN            10
#define BTN1_PORT           GPIOB
#define BTN1_PIN            3
#define BTN2_PORT           GPIOB
#define BTN2_PIN            5
#define BTN3_PORT           GPIOB
#define BTN3_PIN            4

/* ADC Pin Definitions */
#define POT_PIN             4
#define TEMP_PIN            1
#define LIGHT_PIN           0

/* 7-Segment BCD Pin Definitions */
#define BCD_2_0_PORT        GPIOC
#define BCD_2_0_PIN         7
#define BCD_2_1_PORT        GPIOA
#define BCD_2_1_PIN         8
#define BCD_2_2_PORT        GPIOB
#define BCD_2_2_PIN         10
#define BCD_2_3_PORT        GPIOA
#define BCD_2_3_PIN         9

/* Game Configuration */
#define BUTTON_DEBOUNCE_MS      50
#define LONG_PRESS_DURATION_MS  2000
#define INITIAL_LIVES           4
#define MAX_PATTERN_LENGTH      32

/* Type Definitions */
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

#endif /* CONFIG_H */
