/* ============================================================================
 * Simon Says Game + OLED HUD (SSD1306/SH1106 via I2C1)
 * ============================================================================ */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define STM32F411xE
#include "stm32f4xx.h"

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
// Button 2: PB5 (D5) → LED PA7 (Yellow/D11)
#define BTN2_PORT           GPIOB
#define BTN2_PIN            5
// Button 3: PB4 (D7) → LED PB6 (Green/D10)
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
uint32_t SystemCoreClock = 84000000;

ButtonState_t g_buttons[4];
uint8_t g_system_initialized = 0;
uint16_t g_adc_values[3] = {0};
volatile uint32_t g_tick_counter = 0;
uint8_t g_current_adc_channel = 0;

GameState_t g_game_state;
uint8_t  g_difficulty;
uint8_t  g_level;
uint32_t g_score;
uint8_t  g_lives;
uint32_t g_state_entry_time;
uint8_t g_difficulty_locked = 0;

#define MAX_PATTERN_LENGTH 32
const uint8_t button_to_led_map[4] = {0, 1, 2, 3};
uint8_t g_pattern[MAX_PATTERN_LENGTH] = {0};
uint8_t g_pattern_length = 0;
uint8_t g_pattern_index = 0;
uint8_t g_input_index = 0;
uint8_t g_input_correct = 1;

GameState_t g_last_state_logged = (GameState_t)-1;

/* ============================================================================
 * OLED (SH1106/SSD1306 over I2C1 PB8=SCL, PB9=SDA)
 * ============================================================================ */
#define OLED_ADDR       0x3C
#define OLED_COL_OFFSET 2   /* SH1106 = 2, SSD1306 = 0 */

static void I2C1_Init_OLED(void){
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    // PB8, PB9 AF4, OD, PU, High speed
    GPIOB->MODER &= ~((3u<<(8*2))|(3u<<(9*2)));
    GPIOB->MODER |=  ((2u<<(8*2))|(2u<<(9*2)));
    GPIOB->OTYPER |=  (1u<<8)|(1u<<9);
    GPIOB->OSPEEDR|=  (3u<<(8*2))|(3u<<(9*2));
    GPIOB->PUPDR  &= ~((3u<<(8*2))|(3u<<(9*2)));
    GPIOB->PUPDR  |=  ((1u<<(8*2))|(1u<<(9*2)));
    GPIOB->AFR[1] &= ~((0xFu<<0)|(0xFu<<4));
    GPIOB->AFR[1] |=  ((4u<<0) |(4u<<4));

    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    RCC->APB1RSTR |= RCC_APB1RSTR_I2C1RST; RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C1RST;

    I2C1->CR1 = 0;
    I2C1->CR2 = 42;          // APB1=42MHz
    I2C1->CCR = 210;         // 100kHz
    I2C1->TRISE = 43;
    I2C1->CR1 = I2C_CR1_PE;
}

static void i2c_start(uint8_t addr){
    I2C1->CR1 |= I2C_CR1_START;
    while(!(I2C1->SR1 & I2C_SR1_SB));
    (void)I2C1->SR1;
    I2C1->DR = addr<<1;                // write
    while(!(I2C1->SR1 & I2C_SR1_ADDR));
    (void)I2C1->SR1; (void)I2C1->SR2;
}
static void i2c_w(uint8_t b){
    while(!(I2C1->SR1 & I2C_SR1_TXE));
    I2C1->DR = b;
    while(!(I2C1->SR1 & I2C_SR1_BTF));
}
static void i2c_stop(void){ I2C1->CR1 |= I2C_CR1_STOP; }

static void oled_cmd(uint8_t c){ i2c_start(OLED_ADDR); i2c_w(0x00); i2c_w(c); i2c_stop(); }
static void oled_data(const uint8_t* p, uint16_t n){ i2c_start(OLED_ADDR); i2c_w(0x40); while(n--) i2c_w(*p++); i2c_stop(); }

static void oled_setpos(uint8_t page, uint8_t col){
    col += OLED_COL_OFFSET;
    oled_cmd(0xB0 | (page & 7));
    oled_cmd(0x00 | (col & 0x0F));
    oled_cmd(0x10 | (col >> 4));
}
static void oled_clear(void){
    uint8_t z[128] = {0};
    for(uint8_t p=0;p<8;p++){ oled_setpos(p,0); oled_data(z,128); }
}
static void oled_init(void){
    I2C1_Init_OLED();
    // init sequence
    oled_cmd(0xAE); oled_cmd(0xD5); oled_cmd(0x80);
    oled_cmd(0xA8); oled_cmd(0x3F); oled_cmd(0xD3); oled_cmd(0x00);
    oled_cmd(0x40); oled_cmd(0x8D); oled_cmd(0x14);
    oled_cmd(0x20); oled_cmd(0x00); oled_cmd(0xA1); oled_cmd(0xC8);
    oled_cmd(0xDA); oled_cmd(0x12); oled_cmd(0x81); oled_cmd(0x7F);
    oled_cmd(0xD9); oled_cmd(0xF1); oled_cmd(0xDB); oled_cmd(0x40);
    oled_cmd(0xA4); oled_cmd(0xA6); oled_cmd(0xAF);
    oled_clear();
}

/* ----- 5x7 tiny font: digits + uppercase A-Z + space + '-' (6 bytes/glyph) ----- */
static const uint8_t FONT5x7_DIGIT[10][6] = {
 {0x3E,0x51,0x49,0x45,0x3E,0x00},{0x00,0x42,0x7F,0x40,0x00,0x00},
 {0x42,0x61,0x51,0x49,0x46,0x00},{0x21,0x41,0x45,0x4B,0x31,0x00},
 {0x18,0x14,0x12,0x7F,0x10,0x00},{0x27,0x45,0x45,0x45,0x39,0x00},
 {0x3C,0x4A,0x49,0x49,0x30,0x00},{0x01,0x71,0x09,0x05,0x03,0x00},
 {0x36,0x49,0x49,0x49,0x36,0x00},{0x06,0x49,0x49,0x29,0x1E,0x00}
};
static const uint8_t FONT5x7_LET[26][6] = {
 /*A*/{0x7E,0x11,0x11,0x11,0x7E,0x00},/*B*/{0x7F,0x49,0x49,0x49,0x36,0x00},
 /*C*/{0x3E,0x41,0x41,0x41,0x22,0x00},/*D*/{0x7F,0x41,0x41,0x22,0x1C,0x00},
 /*E*/{0x7F,0x49,0x49,0x49,0x41,0x00},/*F*/{0x7F,0x09,0x09,0x09,0x01,0x00},
 /*G*/{0x3E,0x41,0x49,0x49,0x7A,0x00},/*H*/{0x7F,0x08,0x08,0x08,0x7F,0x00},
 /*I*/{0x00,0x41,0x7F,0x41,0x00,0x00},/*J*/{0x20,0x40,0x41,0x3F,0x01,0x00},
 /*K*/{0x7F,0x08,0x14,0x22,0x41,0x00},/*L*/{0x7F,0x40,0x40,0x40,0x40,0x00},
 /*M*/{0x7F,0x02,0x0C,0x02,0x7F,0x00},/*N*/{0x7F,0x04,0x08,0x10,0x7F,0x00},
 /*O*/{0x3E,0x41,0x41,0x41,0x3E,0x00},/*P*/{0x7F,0x09,0x09,0x09,0x06,0x00},
 /*Q*/{0x3E,0x41,0x51,0x21,0x5E,0x00},/*R*/{0x7F,0x09,0x19,0x29,0x46,0x00},
 /*S*/{0x46,0x49,0x49,0x49,0x31,0x00},/*T*/{0x01,0x01,0x7F,0x01,0x01,0x00},
 /*U*/{0x3F,0x40,0x40,0x40,0x3F,0x00},/*V*/{0x1F,0x20,0x40,0x20,0x1F,0x00},
 /*W*/{0x7F,0x20,0x18,0x20,0x7F,0x00},/*X*/{0x63,0x14,0x08,0x14,0x63,0x00},
 /*Y*/{0x07,0x08,0x70,0x08,0x07,0x00},/*Z*/{0x61,0x51,0x49,0x45,0x43,0x00}
};
static const uint8_t FONT5x7_SPACE[6] = {0,0,0,0,0,0};
static const uint8_t FONT5x7_MINUS[6] = {0x08,0x08,0x08,0x08,0x08,0x00};

static void oled_draw_digit(uint8_t x,uint8_t page,int d){
    if(d>=0 && d<=9){ oled_setpos(page,x); oled_data(FONT5x7_DIGIT[d],6); }
}
static void oled_draw_letter(uint8_t x,uint8_t page,char c){
    const uint8_t* g = FONT5x7_SPACE;
    if(c>='A' && c<='Z') g = FONT5x7_LET[c-'A'];
    else if(c>='0' && c<='9') g = FONT5x7_DIGIT[c-'0'];
    else if(c=='-') g = FONT5x7_MINUS;
    oled_setpos(page,x); oled_data(g,6);
}
static void oled_print_text(uint8_t x,uint8_t page,const char* s){
    while(*s){ oled_draw_letter(x,page, (*s>='a'&&*s<='z')?(*s-32):*s ); x+=6; s++; }
}
static void oled_print_uint(uint8_t x,uint8_t page,unsigned v){
    char buf[10]; int n=0;
    if(v==0){ oled_draw_digit(x,page,0); return; }
    while(v && n<10){ buf[n++] = '0' + (v%10); v/=10; }
    for(int i=n-1;i>=0;i--,x+=6) oled_draw_digit(x,page, buf[i]-'0');
}

/* แสดงสถานะเกมบน OLED */
static void OLED_ShowStatus(void){
    oled_clear();
    // LEVEL
    oled_print_text(0, 0, "LEVEL");
    oled_print_uint(6*6, 0, g_level);

    // LIVES
    oled_print_text(0, 2, "LIVES");
    oled_print_uint(6*6, 2, g_lives);

    // SCORE
    oled_print_text(0, 4, "SCORE");
    oled_print_uint(6*6, 4, g_score);

    // STATE
    switch(g_game_state){
        case GAME_STATE_VICTORY:      oled_print_text(0, 6, "VICTORY"); break;
        case GAME_STATE_GAME_DEATH:   oled_print_text(0, 6, "GAME-OVER"); break;
        case GAME_STATE_PATTERN_DISPLAY: oled_print_text(0, 6, "SHOW"); break;
        case GAME_STATE_INPUT_WAIT:   oled_print_text(0, 6, "INPUT"); break;
        case GAME_STATE_DIFFICULTY_SELECT: oled_print_text(0,6,"DIFF"); break;
        default:                      oled_print_text(0, 6, "PLAY"); break;
    }
}

/* ============================================================================
 * FUNCTION PROTOTYPES (GAME)
 * ============================================================================ */
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

/* Hardware & System */
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
 * MAIN
 * ============================================================================ */
int main(void) {
    SystemClock_Config();
    GPIO_Init();
    USART2_Init();
    SysTick_Config(SystemCoreClock / 1000); // 1ms ticks
    NVIC_Init();
    ADC_Init();

    // OLED HUD
    oled_init();
    oled_clear();

    g_system_initialized = 1;

    ADC_StartConversion();
    Delay_ms(10);

    Game_Init();

    while(1) {
        Monitor_Buttons();
        Monitor_ADC();
        Game_Run();
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
        OLED_ShowStatus(); // อัปเดตจอทุกครั้งที่เข้าสถานะใหม่
    }
    switch(g_game_state) {
        case GAME_STATE_BOOT:              handle_boot();              break;
        case GAME_STATE_DIFFICULTY_SELECT: handle_difficulty_select(); break;
        case GAME_STATE_LEVEL_INTRO:       handle_level_intro();       break;
        case GAME_STATE_PATTERN_DISPLAY:   handle_pattern_display();   break;
        case GAME_STATE_INPUT_WAIT:        handle_input_wait();        break;
        case GAME_STATE_RESULT_PROCESS:    handle_result_process();    break;
        case GAME_STATE_VICTORY:           handle_victory();           break;
        case GAME_STATE_GAME_DEATH:        handle_game_death();        break;
        default:
            set_game_state(GAME_STATE_DIFFICULTY_SELECT);
            Delay_ms(1000);
            break;
    }
}

static void generate_pattern(uint8_t length) {
    for (uint8_t i = 0; i < length; i++) g_pattern[i] = rand() % 4;
    g_pattern_length = length;
}

static void show_led(uint8_t idx) { LED_SetPattern(1 << button_to_led_map[idx]); }
static void clear_leds(void) { LED_SetPattern(0); }

static void handle_level_intro(void) {
    Log_Print("Level %u. Lives: %u. Score: %lu\r\n", g_level, g_lives, g_score);
    OLED_ShowStatus();
    Delay_ms(800);
    generate_pattern(g_level + g_difficulty - 1);
    g_pattern_index = 0;
    set_game_state(GAME_STATE_PATTERN_DISPLAY);
}

static void handle_pattern_display(void) {
    if (g_pattern_index < g_pattern_length) {
        show_led(g_pattern[g_pattern_index]); Delay_ms(500);
        clear_leds();                          Delay_ms(250);
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
                show_led(i); Delay_ms(200); clear_leds();
                if (i != g_pattern[g_input_index]) g_input_correct = 0;
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
        if (g_level > 5) set_game_state(GAME_STATE_VICTORY);
        else             set_game_state(GAME_STATE_LEVEL_INTRO);
    } else {
        if (g_lives > 0) g_lives--;
        OLED_ShowStatus();
        if (g_lives == 0) set_game_state(GAME_STATE_GAME_DEATH);
        else { Log_Print("Try again!\r\n"); set_game_state(GAME_STATE_LEVEL_INTRO); }
    }
}

static void handle_victory(void) {
    Log_Print("Congratulations! Final Score: %lu\r\n", g_score);
    OLED_ShowStatus();
    for (int i = 0; i < 4; i++) {
        if (g_buttons[i].current_state == 1 && g_buttons[i].previous_state == 0) {
            g_level = 1; g_score = 0; g_lives = INITIAL_LIVES; g_difficulty_locked = 0;
            set_game_state(GAME_STATE_DIFFICULTY_SELECT);
            break;
        }
    }
}

static void handle_game_death(void) {
    Log_Print("Game Over! Final Score: %lu\r\n", g_score);
    OLED_ShowStatus();
    for (int i = 0; i < 4; i++) {
        if (g_buttons[i].current_state == 1 && g_buttons[i].previous_state == 0) {
            g_level = 1; g_score = 0; g_lives = INITIAL_LIVES; g_difficulty_locked = 0;
            set_game_state(GAME_STATE_DIFFICULTY_SELECT);
            break;
        }
    }
}

static void set_game_state(GameState_t new_state) {
    g_game_state = new_state;
    g_state_entry_time = GetTick();
}

static void handle_boot(void) {
    g_level = 1; g_score = 0; g_lives = INITIAL_LIVES;
    set_game_state(GAME_STATE_DIFFICULTY_SELECT);
}

static void handle_difficulty_select(void) {
    uint32_t current_time = GetTick();
    static uint32_t last_log_time = 0;
    static uint8_t last_difficulty = 0;

    if (!g_difficulty_locked) {
        uint16_t pot_value = g_adc_values[0]; // 0..1023
        // map เป็น 1..5 (แบ่งเท่า ๆ กัน)
        g_difficulty = (uint32_t)(pot_value * 5) / 1024 + 1;
        SevenSeg_Display(g_difficulty);

        if (g_difficulty != last_difficulty || (current_time - last_log_time) > 1000) {
            Log_Print("[DIFFICULTY] Pot:%u -> Diff:%u\r\n", pot_value, g_difficulty);
            last_log_time = current_time; last_difficulty = g_difficulty;
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

/* ============================================================================
 * INTERRUPT HANDLERS
 * ============================================================================ */
void SysTick_Handler(void) { g_tick_counter++; }

void ADC_IRQHandler(void) {
    if(ADC1->SR & ADC_SR_EOC) {
        g_adc_values[g_current_adc_channel] = ADC1->DR;
        g_current_adc_channel = (g_current_adc_channel + 1) % 3;
        ADC1->SQR3 = (ADC1->SQR3 & ~ADC_SQR3_SQ1) |
                     (g_current_adc_channel == 0 ? POT_PIN : (g_current_adc_channel == 1 ? TEMP_PIN : LIGHT_PIN));
        ADC1->CR2 |= ADC_CR2_SWSTART;
    }
}

/* ============================================================================
 * HARDWARE MONITORING
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
            }
        }
    }
}

void Monitor_ADC(void) { /* ADC via IRQ */ }

/* ============================================================================
 * HARDWARE CONTROL
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
    RCC->CR |= RCC_CR_HSION; while(!(RCC->CR & RCC_CR_HSIRDY));
    RCC->PLLCFGR = (RCC_PLLCFGR_PLLSRC_HSI) | (16 << RCC_PLLCFGR_PLLM_Pos) | (168 << RCC_PLLCFGR_PLLN_Pos) | (0 << RCC_PLLCFGR_PLLP_Pos);
    RCC->CR |= RCC_CR_PLLON; while(!(RCC->CR & RCC_CR_PLLRDY));
    FLASH->ACR = FLASH_ACR_LATENCY_2WS;
    RCC->CFGR |= RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1;
    RCC->CFGR |= RCC_CFGR_SW_PLL; while((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL);
    SystemCoreClock = 84000000;
}

void GPIO_Init(void) {
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN;
    // LEDs
    GPIOA->MODER |= (1 << (LED1_PIN*2)) | (1 << (LED2_PIN*2)) | (1 << (LED3_PIN*2));
    GPIOB->MODER |= (1 << (LED4_PIN*2));
    // Buttons (pull-up)
    GPIOA->PUPDR |= (1 << (BTN0_PIN*2));
    GPIOB->PUPDR |= (1 << (BTN1_PIN*2)) | (1 << (BTN2_PIN*2)) | (1 << (BTN3_PIN*2));
    // ADC
    GPIOA->MODER |= (3 << (POT_PIN*2)) | (3 << (TEMP_PIN*2)) | (3 << (LIGHT_PIN*2));
    // UART2: PA2,PA3 AF7
    GPIOA->MODER |= (2 << (2*2)) | (2 << (3*2));
    GPIOA->AFR[0] |= (7 << (2*4)) | (7 << (3*4));
    // 7-Segment (BCD inputs)
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
    USART2->BRR = 0x16C; // 115200 @ 42MHz
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
 * UTILITY
 * ============================================================================ */
void Delay_ms(uint32_t ms) {
    uint32_t start = GetTick();
    while((GetTick() - start) < ms);
}
uint32_t GetTick(void) { return g_tick_counter; }

void Log_Print(const char* format, ...) {
    if(!g_system_initialized) return;
    char buffer[256];
    va_list args; va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format,args);
    va_end(args);
    for(char* p = buffer; *p; p++) { while(!(USART2->SR & USART_SR_TXE)); USART2->DR = *p; }
}
