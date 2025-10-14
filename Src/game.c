/* ============================================================================
 * Game Logic Implementation
 * Simon Says Game State Machine
 * ============================================================================ */

#include "game.h"
#include "hardware.h"
#include "utils.h"
#include "oled.h"
#include <stdlib.h>

/* Global Variables */
GameState_t g_game_state;
uint8_t g_difficulty;
uint8_t g_level;
uint32_t g_score;
uint8_t g_lives;
uint32_t g_state_entry_time;
uint8_t g_difficulty_locked = 0;

const uint8_t button_to_led_map[4] = {0, 1, 2, 3};
uint8_t g_pattern[MAX_PATTERN_LENGTH] = {0};
uint8_t g_pattern_length = 0;
uint8_t g_pattern_index = 0;
uint8_t g_input_index = 0;
uint8_t g_input_correct = 1;

GameState_t g_last_state_logged = (GameState_t)-1;

/* ============================================================================
 * Difficulty Timing Functions
 * ============================================================================ */
uint8_t clamp_u8(uint8_t v, uint8_t lo, uint8_t hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

uint16_t diff_on_ms(uint8_t diff) {
    static const uint16_t T[5] = {500, 400, 300, 220, 150}; // DIFF 1..5
    diff = clamp_u8(diff, 1, 5);
    return T[diff-1];
}

uint16_t diff_off_ms(uint8_t diff) {
    static const uint16_t T[5] = {250, 200, 150, 110, 80};  // DIFF 1..5
    diff = clamp_u8(diff, 1, 5);
    return T[diff-1];
}

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */
static void set_game_state(GameState_t new_state) {
    g_game_state = new_state;
    g_state_entry_time = GetTick();
}

static void generate_pattern(uint8_t length) {
    for (uint8_t i = 0; i < length; i++)
        g_pattern[i] = rand() % 4;
    g_pattern_length = length;
}

static void show_led(uint8_t idx) {
    LED_SetPattern(1 << button_to_led_map[idx]);
}

static void clear_leds(void) {
    LED_SetPattern(0);
}

/* ============================================================================
 * State Handler Functions
 * ============================================================================ */
static void handle_boot(void) {
    g_level = 1;
    g_score = 0;
    g_lives = INITIAL_LIVES;
    set_game_state(GAME_STATE_DIFFICULTY_SELECT);
}

static void handle_difficulty_select(void) {
    uint32_t current_time = GetTick();
    static uint32_t last_log_time = 0;
    static uint8_t last_difficulty = 0;

    if (!g_difficulty_locked) {
        uint16_t pot_value = g_adc_values[0];
        g_difficulty = (uint32_t)(pot_value * 5) / 1024 + 1;  // 1..5
        SevenSeg_Display(g_difficulty);

        if (g_difficulty != last_difficulty || (current_time - last_log_time) > 1000) {
            Log_Print("[DIFFICULTY] Pot:%u -> Diff:%u\r\n", pot_value, g_difficulty);
            last_log_time = current_time;
            last_difficulty = g_difficulty;
            OLED_ShowStatus();
        }

        for (int i = 0; i < 4; i++) {
            if (g_buttons[i].current_state == 1 &&
               (current_time - g_buttons[i].last_change_time) >= LONG_PRESS_DURATION_MS) {
                g_difficulty_locked = 1;
                set_game_state(GAME_STATE_LEVEL_INTRO);
                return;
            }
        }
    } else {
        SevenSeg_Display(g_difficulty);
    }
}

static void handle_level_intro(void) {
    Log_Print("Level %u. Lives: %u. Score: %lu\r\n", g_level, g_lives, g_score);
    OLED_ShowStatus();
    Delay_ms(800);

    // Back-and-forth LED animation only for first level
    if (g_level == 1) {
        // Forward: LED0 -> LED1 -> LED2 -> LED3
        for (int i = 0; i < 4; i++) {
            show_led(i);
            Delay_ms(150);
        }
        // Backward: LED3 -> LED2 -> LED1 -> LED0
        for (int i = 2; i >= 0; i--) {
            show_led(i);
            Delay_ms(150);
        }
        clear_leds();
        Delay_ms(200);
    }

    generate_pattern(g_level);
    g_pattern_index = 0;
    set_game_state(GAME_STATE_PATTERN_DISPLAY);
}

static void handle_pattern_display(void) {
    uint16_t t_on  = diff_on_ms(g_difficulty);
    uint16_t t_off = diff_off_ms(g_difficulty);

    if (g_pattern_index < g_pattern_length) {
        show_led(g_pattern[g_pattern_index]);
        Delay_ms(t_on);
        clear_leds();
        Delay_ms(t_off);
        g_pattern_index++;
    } else {
        g_pattern_index = 0;
        g_input_index = 0;
        g_input_correct = 1;
        set_game_state(GAME_STATE_INPUT_WAIT);
    }
}

static void handle_input_wait(void) {
    if (g_input_index < g_pattern_length) {
        for (int i = 0; i < 4; i++) {
            if (g_buttons[i].current_state == 1 && g_buttons[i].previous_state == 0) {
                show_led(i);
                Delay_ms(diff_on_ms(g_difficulty) / 2);
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
        g_score += 10 * g_level * g_difficulty;
        g_level++;
        OLED_ShowStatus();
        if (g_level > 9)
            set_game_state(GAME_STATE_VICTORY);
        else
            set_game_state(GAME_STATE_LEVEL_INTRO);
    } else {
        if (g_lives > 0) g_lives--;
        OLED_ShowStatus();
        if (g_lives == 0)
            set_game_state(GAME_STATE_GAME_DEATH);
        else {
            Log_Print("Try again!\r\n");
            set_game_state(GAME_STATE_LEVEL_INTRO);
        }
    }
}

static void handle_victory(void) {
    Log_Print("Congratulations! Final Score: %lu\r\n", g_score);
    OLED_ShowStatus();

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
    static uint8_t animation_played = 0;

    // Play game over animation once upon entering this state
    if (!animation_played) {
        Log_Print("Game Over! Final Score: %lu\r\n", g_score);

        // Rapid blink: 3 cycles
        for (int cycle = 0; cycle < 3; cycle++) {
            LED_SetPattern(0x0F);  // All LEDs on
            Delay_ms(150);
            LED_SetPattern(0x00);  // All LEDs off
            Delay_ms(150);
        }

        // Gradual fade out simulation
        for (int brightness = 10; brightness > 0; brightness--) {
            for (int pulse = 0; pulse < 20; pulse++) {
                LED_SetPattern(0x0F);
                Delay_ms(brightness);
                LED_SetPattern(0x00);
                Delay_ms(11 - brightness);
            }
        }

        LED_SetPattern(0x00);  // Ensure all off
        OLED_ShowStatus();
        animation_played = 1;
    }

    // Wait for button press to restart
    for (int i = 0; i < 4; i++) {
        if (g_buttons[i].current_state == 1 && g_buttons[i].previous_state == 0) {
            g_level = 1;
            g_score = 0;
            g_lives = INITIAL_LIVES;
            g_difficulty_locked = 0;
            animation_played = 0;  // Reset for next game over
            set_game_state(GAME_STATE_DIFFICULTY_SELECT);
            break;
        }
    }
}

/* ============================================================================
 * Public Functions
 * ============================================================================ */
void Game_Init(void) {
    Log_Print("\r\n[GAME] Initializing Simon Game...\r\n");
    uint32_t seed = g_adc_values[1] + g_adc_values[2] + GetTick();
    srand(seed);
    Log_Print("[GAME] Random seed set to: %lu\r\n", seed);
    set_game_state(GAME_STATE_BOOT);
}

void Game_Run(void) {
    // Log state transitions
    if (g_last_state_logged != g_game_state) {
        switch(g_game_state) {
            case GAME_STATE_BOOT:
                Log_Print("[STATE] -> BOOT\r\n");
                break;
            case GAME_STATE_DIFFICULTY_SELECT:
                Log_Print("[STATE] -> DIFFICULTY_SELECT\r\n");
                break;
            case GAME_STATE_LEVEL_INTRO:
                Log_Print("[STATE] -> LEVEL_INTRO\r\n");
                break;
            case GAME_STATE_PATTERN_DISPLAY:
                Log_Print("[STATE] -> PATTERN_DISPLAY\r\n");
                break;
            case GAME_STATE_INPUT_WAIT:
                Log_Print("[STATE] -> INPUT_WAIT\r\n");
                break;
            case GAME_STATE_RESULT_PROCESS:
                Log_Print("[STATE] -> RESULT_PROCESS\r\n");
                break;
            case GAME_STATE_VICTORY:
                Log_Print("[STATE] -> VICTORY\r\n");
                break;
            case GAME_STATE_GAME_DEATH:
                Log_Print("[STATE] -> GAME_DEATH\r\n");
                break;
        }
        g_last_state_logged = g_game_state;
        OLED_ShowStatus();
    }

    // Execute current state handler
    switch(g_game_state) {
        case GAME_STATE_BOOT:
            handle_boot();
            break;
        case GAME_STATE_DIFFICULTY_SELECT:
            handle_difficulty_select();
            break;
        case GAME_STATE_LEVEL_INTRO:
            handle_level_intro();
            break;
        case GAME_STATE_PATTERN_DISPLAY:
            handle_pattern_display();
            break;
        case GAME_STATE_INPUT_WAIT:
            handle_input_wait();
            break;
        case GAME_STATE_RESULT_PROCESS:
            handle_result_process();
            break;
        case GAME_STATE_VICTORY:
            handle_victory();
            break;
        case GAME_STATE_GAME_DEATH:
            handle_game_death();
            break;
        default:
            set_game_state(GAME_STATE_DIFFICULTY_SELECT);
            Delay_ms(1000);
            break;
    }
}
