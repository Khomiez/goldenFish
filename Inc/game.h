/* ============================================================================
 * Game Logic
 * Simon Says game state machine and logic
 * ============================================================================ */

#ifndef GAME_H
#define GAME_H

#include <stdint.h>
#include "config.h"

/* Global Variables */
extern GameState_t g_game_state;
extern uint8_t g_difficulty;
extern uint8_t g_level;
extern uint32_t g_score;
extern uint8_t g_lives;
extern uint32_t g_state_entry_time;
extern uint8_t g_difficulty_locked;
extern uint8_t g_pattern[MAX_PATTERN_LENGTH];
extern uint8_t g_pattern_length;
extern uint8_t g_pattern_index;
extern uint8_t g_input_index;
extern uint8_t g_input_correct;
extern GameState_t g_last_state_logged;

/* Function Prototypes */
void Game_Init(void);
void Game_Run(void);

/* Difficulty Timing Functions */
uint8_t clamp_u8(uint8_t v, uint8_t lo, uint8_t hi);
uint16_t diff_on_ms(uint8_t diff);
uint16_t diff_off_ms(uint8_t diff);

#endif /* GAME_H */
