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
// ลำดับ index 0..3 ต้องตรงกับลำดับ LED: ฟ้า, แดง, เหลือง, เขียว
// เลือกเป็นคอร์ด C เมเจอร์ ไล่สูง→ต่ำ ฟังแล้ว “แจ่มใส”
static const uint16_t tone_by_led[4] = {
    988, // ฟ้า   (B5)
    784, // แดง   (G5)
    659, // เหลือง(E5)
    523  // เขียว (C5)
};
uint8_t g_pattern[MAX_PATTERN_LENGTH] = {0};
uint8_t g_pattern_length = 0;
uint8_t g_pattern_index = 0;
uint8_t g_input_index = 0;
uint8_t g_input_correct = 1;

GameState_t g_last_state_logged = (GameState_t)-1;

typedef enum { PD_LED_ON, PD_LED_OFF } PatternPhase_t;
static PatternPhase_t s_phase = PD_LED_ON;
static uint32_t s_next_deadline = 0;


static void pattern_begin(void){
    g_pattern_index = 0;
    s_phase = PD_LED_ON;
    s_next_deadline = 0; // trigger ทันที
}

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
static void leds_clear(void) {
    LED_SetPattern(0);
    Buzzer_Stop();
}

static void set_game_state(GameState_t new_state) {
    leds_clear();
    g_game_state = new_state;
    g_state_entry_time = GetTick();
}

static void generate_pattern(uint8_t length) {
    for (uint8_t i = 0; i < length; i++)
        g_pattern[i] = rand() % 4;
    g_pattern_length = length;
}

static void leds_show(uint8_t idx) {
    uint8_t led = button_to_led_map[idx];
    LED_SetPattern(1 << led);
    Buzzer_Play(tone_by_led[led], 40);
}



/* ============================================================================
 * State Handler Functions
 * ============================================================================ */
static void handle_boot(void) {
    g_level = 1;
    g_score = 0;
    g_lives = INITIAL_LIVES;
    set_game_state(GAME_STATE_DIFFICULTY_SELECT);
    Buzzer_Play(800, 50);
    Delay_ms(100);
    Buzzer_Stop();
}

static uint16_t pot_avg = 0;

static uint8_t map_pot_to_speed(uint16_t v10bit) {
    // smooth: avg = avg*7/8 + new/8
    pot_avg = (pot_avg * 7 + v10bit) / 8;

    // map 0..1023 -> 1..5
    uint8_t s = (uint32_t)(pot_avg * 5) / 1024 + 1;

    // hysteresis: ถ้าต่างจาก g_difficulty น้อย ให้รอก่อน
    if (s > g_difficulty && (pot_avg % 205) < 20) return g_difficulty; // ขยับขึ้นเมื่อผ่านช่วง
    if (s < g_difficulty && (pot_avg % 205) > 185) return g_difficulty; // ขยับลงเมื่อผ่านช่วง
    return s;
}

static void handle_difficulty_select(void) {
    uint32_t current_time = GetTick();
    static uint32_t last_log_time = 0;
    static uint8_t last = 0;

    if (!g_difficulty_locked) {
        uint16_t pot_value = g_adc_values[0];
        g_difficulty = map_pot_to_speed(pot_value);
        SevenSeg_Display(g_difficulty);

        if (g_difficulty != last || (current_time - last_log_time) > 200) {
            last_log_time = current_time;
            last = g_difficulty;
            OLED_ShowStatus(); // อัปเดตจอน้อยลง
        }

        // long-press -> lock เหมือนเดิม...
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
    Debug_PrintGameState();
    OLED_ShowStatus();
    Delay_ms(800);

    // Back-and-forth LED animation only for first level
    if (g_level == 1) {
        // Forward: LED0 -> LED1 -> LED2 -> LED3
        for (int i = 0; i < 4; i++) {
            leds_show(i);
            Delay_ms(150);
        }
        // Backward: LED3 -> LED2 -> LED1 -> LED0
        for (int i = 2; i >= 0; i--) {
            leds_show(i);
            Delay_ms(150);
        }
        leds_clear();
        Delay_ms(200);
    }

    generate_pattern(g_level);
    g_pattern_index = 0;
    pattern_begin();  
    set_game_state(GAME_STATE_PATTERN_DISPLAY);
}

static void handle_pattern_display(void) {
    static uint8_t pattern_logged = 0;
    uint32_t now = GetTick();
    uint16_t t_on  = diff_on_ms(g_difficulty);
    uint16_t t_off = diff_off_ms(g_difficulty);

    // Log pattern once when starting display
    if (!pattern_logged) {
        Log_Print("[PATTERN] Displaying pattern: ");
        for (uint8_t i = 0; i < g_pattern_length; i++) {
            const char* led_names[4] = {"BLUE", "RED", "YELLOW", "GREEN"};
            Log_Print("%s", led_names[g_pattern[i]]);
            if (i < g_pattern_length - 1) Log_Print(", ");
        }
        Log_Print("\r\n");
        pattern_logged = 1;
    }

    if (g_pattern_index >= g_pattern_length) {
        // จบ pattern → ไป input
        g_pattern_index = 0;
        g_input_index = 0;
        g_input_correct = 1;
        pattern_logged = 0; // reset for next level
        Log_Print("[PATTERN] Display complete. Waiting for input...\r\n");
        set_game_state(GAME_STATE_INPUT_WAIT);
        return;
    }

    if (now >= s_next_deadline) {
        if (s_phase == PD_LED_ON) {
            leds_show(g_pattern[g_pattern_index]);     // จะเล่นเสียง/เปิดไฟ
            s_next_deadline = now + t_on;
            s_phase = PD_LED_OFF;
        } else { // PD_LED_OFF
            leds_clear();                            // จะหยุดเสียง/ดับไฟ
            s_next_deadline = now + t_off;
            s_phase = PD_LED_ON;
            g_pattern_index++;
        }
    }
}

static void handle_input_wait(void) {
    if (g_input_index < g_pattern_length) {
        for (int i = 0; i < 4; i++) {
            if (g_buttons[i].current_state == 1 && g_buttons[i].previous_state == 0) {
                const char* btn_names[4] = {"BLUE", "RED", "YELLOW", "GREEN"};
                Log_Print("[INPUT] Button %s pressed. Index: %u, Expected: %u, Correct: %s\r\n",
                          btn_names[i], g_input_index, g_pattern[g_input_index],
                          (i == g_pattern[g_input_index]) ? "YES" : "NO");
                leds_show(i);
                Delay_ms(diff_on_ms(g_difficulty) / 2);
                leds_clear();
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
        Log_Print("[RESULT] SUCCESS! Level %u completed.\r\n", g_level);
        Buzzer_Play(1200, 40);
        Delay_ms(80);
        Buzzer_Stop();
        g_score += 10 * g_level * g_difficulty;
        g_level++;
        Log_Print("[RESULT] Score: %lu, Next Level: %u\r\n", g_score, g_level);
        Debug_PrintGameState();
        OLED_ShowStatus();
        if (g_level > 9)
            set_game_state(GAME_STATE_VICTORY);
        else
            set_game_state(GAME_STATE_LEVEL_INTRO);
    } else {
        Log_Print("[RESULT] FAIL! Lost a life.\r\n");
        Buzzer_Play(300, 40);
        Delay_ms(150);
        Buzzer_Stop();
        if (g_lives > 0) g_lives--;
        Log_Print("[RESULT] Lives remaining: %u\r\n", g_lives);
        Debug_PrintGameState();
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
    static uint8_t played = 0;
    Log_Print("Congratulations! Final Score: %lu\r\n", g_score);
    OLED_ShowStatus();

    if (!played) {                      // <— เล่นครั้งเดียว
        Debug_PrintGameState();
        uint32_t melody[] = {523, 659, 784}; // C5, E5, G5
        for (int i = 0; i < 3; i++) {
            Buzzer_Play(melody[i], 40);
            Delay_ms(150);
            Buzzer_Stop();
            Delay_ms(50);
        }
        played = 1;
    }

    // รอกดปุ่มเพื่อรีสตาร์ท
    for (int i = 0; i < 4; i++) {
        if (g_buttons[i].current_state == 1 && g_buttons[i].previous_state == 0) {
            g_level = 1;
            g_score = 0;
            g_lives = INITIAL_LIVES;
            g_difficulty_locked = 0;
            played = 0;                 // <— รีเซ็ตสำหรับรอบถัดไป
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
        Debug_PrintGameState();

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
