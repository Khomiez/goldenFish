/* ============================================================================
 * OLED Display Driver Implementation
 * SH1106/SSD1306 over I2C1
 * ============================================================================
 */

#include "oled.h"
#include "game.h"

#define STM32F411xE
#include "stm32f4xx.h"

/* ============================================================================
 * I2C Low-Level Functions
 * ============================================================================
 */
static void I2C1_Init_OLED(void) {
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;

  // PB8, PB9 AF4, Open-Drain, Pull-Up, High speed
  GPIOB->MODER &= ~((3u << (8 * 2)) | (3u << (9 * 2)));
  GPIOB->MODER |= ((2u << (8 * 2)) | (2u << (9 * 2)));
  GPIOB->OTYPER |= (1u << 8) | (1u << 9);
  GPIOB->OSPEEDR |= (3u << (8 * 2)) | (3u << (9 * 2));
  GPIOB->PUPDR &= ~((3u << (8 * 2)) | (3u << (9 * 2)));
  GPIOB->PUPDR |= ((1u << (8 * 2)) | (1u << (9 * 2)));
  GPIOB->AFR[1] &= ~((0xFu << 0) | (0xFu << 4));
  GPIOB->AFR[1] |= ((4u << 0) | (4u << 4));

  RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
  RCC->APB1RSTR |= RCC_APB1RSTR_I2C1RST;
  RCC->APB1RSTR &= ~RCC_APB1RSTR_I2C1RST;

  I2C1->CR1 = 0;
  I2C1->CR2 = 42;  // APB1=42MHz
  I2C1->CCR = 210; // 100kHz
  I2C1->TRISE = 43;
  I2C1->CR1 = I2C_CR1_PE;
}

static void i2c_start(uint8_t addr) {
  I2C1->CR1 |= I2C_CR1_START;
  while (!(I2C1->SR1 & I2C_SR1_SB))
    ;
  (void)I2C1->SR1;
  I2C1->DR = addr << 1;
  while (!(I2C1->SR1 & I2C_SR1_ADDR))
    ;
  (void)I2C1->SR1;
  (void)I2C1->SR2;
}

static void i2c_w(uint8_t b) {
  while (!(I2C1->SR1 & I2C_SR1_TXE))
    ;
  I2C1->DR = b;
  while (!(I2C1->SR1 & I2C_SR1_BTF))
    ;
}

static void i2c_stop(void) { I2C1->CR1 |= I2C_CR1_STOP; }

/* ============================================================================
 * OLED Command/Data Functions
 * ============================================================================
 */
static void oled_cmd(uint8_t c) {
  i2c_start(OLED_ADDR);
  i2c_w(0x00);
  i2c_w(c);
  i2c_stop();
}

static void oled_data(const uint8_t *p, uint16_t n) {
  i2c_start(OLED_ADDR);
  i2c_w(0x40);
  while (n--)
    i2c_w(*p++);
  i2c_stop();
}

static void oled_setpos(uint8_t page, uint8_t col) {
  col += OLED_COL_OFFSET;
  oled_cmd(0xB0 | (page & 7));
  oled_cmd(0x00 | (col & 0x0F));
  oled_cmd(0x10 | (col >> 4));
}

/* ============================================================================
 * Font Data
 * ============================================================================
 */
static const uint8_t FONT5x7_DIGIT[10][6] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00}, {0x00, 0x42, 0x7F, 0x40, 0x00, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46, 0x00}, {0x21, 0x41, 0x45, 0x4B, 0x31, 0x00},
    {0x18, 0x14, 0x12, 0x7F, 0x10, 0x00}, {0x27, 0x45, 0x45, 0x45, 0x39, 0x00},
    {0x3C, 0x4A, 0x49, 0x49, 0x30, 0x00}, {0x01, 0x71, 0x09, 0x05, 0x03, 0x00},
    {0x36, 0x49, 0x49, 0x49, 0x36, 0x00}, {0x06, 0x49, 0x49, 0x29, 0x1E, 0x00}};

static const uint8_t FONT5x7_LET[26][6] = {
    /*A*/ {0x7E, 0x11, 0x11, 0x11, 0x7E, 0x00},
    /*B*/ {0x7F, 0x49, 0x49, 0x49, 0x36, 0x00},
    /*C*/ {0x3E, 0x41, 0x41, 0x41, 0x22, 0x00},
    /*D*/ {0x7F, 0x41, 0x41, 0x22, 0x1C, 0x00},
    /*E*/ {0x7F, 0x49, 0x49, 0x49, 0x41, 0x00},
    /*F*/ {0x7F, 0x09, 0x09, 0x09, 0x01, 0x00},
    /*G*/ {0x3E, 0x41, 0x49, 0x49, 0x7A, 0x00},
    /*H*/ {0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00},
    /*I*/ {0x00, 0x41, 0x7F, 0x41, 0x00, 0x00},
    /*J*/ {0x20, 0x40, 0x41, 0x3F, 0x01, 0x00},
    /*K*/ {0x7F, 0x08, 0x14, 0x22, 0x41, 0x00},
    /*L*/ {0x7F, 0x40, 0x40, 0x40, 0x40, 0x00},
    /*M*/ {0x7F, 0x02, 0x0C, 0x02, 0x7F, 0x00},
    /*N*/ {0x7F, 0x04, 0x08, 0x10, 0x7F, 0x00},
    /*O*/ {0x3E, 0x41, 0x41, 0x41, 0x3E, 0x00},
    /*P*/ {0x7F, 0x09, 0x09, 0x09, 0x06, 0x00},
    /*Q*/ {0x3E, 0x41, 0x51, 0x21, 0x5E, 0x00},
    /*R*/ {0x7F, 0x09, 0x19, 0x29, 0x46, 0x00},
    /*S*/ {0x46, 0x49, 0x49, 0x49, 0x31, 0x00},
    /*T*/ {0x01, 0x01, 0x7F, 0x01, 0x01, 0x00},
    /*U*/ {0x3F, 0x40, 0x40, 0x40, 0x3F, 0x00},
    /*V*/ {0x1F, 0x20, 0x40, 0x20, 0x1F, 0x00},
    /*W*/ {0x7F, 0x20, 0x18, 0x20, 0x7F, 0x00},
    /*X*/ {0x63, 0x14, 0x08, 0x14, 0x63, 0x00},
    /*Y*/ {0x07, 0x08, 0x70, 0x08, 0x07, 0x00},
    /*Z*/ {0x61, 0x51, 0x49, 0x45, 0x43, 0x00}};

static const uint8_t FONT5x7_SPACE[6] = {0, 0, 0, 0, 0, 0};
static const uint8_t FONT5x7_MINUS[6] = {0x08, 0x08, 0x08, 0x08, 0x08, 0x00};
static const uint8_t FONT5x7_COLON[6] = {0x00, 0x00, 0x36, 0x36, 0x00, 0x00};

/* Custom Icons */
static const uint8_t ICON_HEART[6] = {0x10, 0x38, 0x7C, 0x7E, 0x7C, 0x38};
static const uint8_t ICON_HEART_EMPTY[6] = {0x10, 0x28, 0x44, 0x42, 0x44, 0x28};
static const uint8_t ICON_BLOCK_FULL[6] = {0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x00};
static const uint8_t ICON_BLOCK_EMPTY[6] = {0x7F, 0x41, 0x41, 0x41, 0x7F, 0x00};

/* ============================================================================
 * Text & Drawing Functions
 * ============================================================================
 */
static void oled_draw_digit(uint8_t x, uint8_t page, int d) {
  if (d >= 0 && d <= 9) {
    oled_setpos(page, x);
    oled_data(FONT5x7_DIGIT[d], 6);
  }
}

static void oled_draw_letter(uint8_t x, uint8_t page, char c) {
  const uint8_t *g = FONT5x7_SPACE;
  if (c >= 'A' && c <= 'Z')
    g = FONT5x7_LET[c - 'A'];
  else if (c >= '0' && c <= '9')
    g = FONT5x7_DIGIT[c - '0'];
  else if (c == '-')
    g = FONT5x7_MINUS;
  else if (c == ':')
    g = FONT5x7_COLON;
  oled_setpos(page, x);
  oled_data(g, 6);
}

static void oled_draw_icon(uint8_t x, uint8_t page, const uint8_t *icon) {
  oled_setpos(page, x);
  oled_data(icon, 6);
}

static void oled_print_text(uint8_t x, uint8_t page, const char *s) {
  while (*s) {
    char c = (*s >= 'a' && *s <= 'z') ? (*s - 32) : *s;
    oled_draw_letter(x, page, c);
    x += 6;
    s++;
  }
}

static void oled_print_uint(uint8_t x, uint8_t page, unsigned v) {
  char buf[10];
  int n = 0;

  if (v == 0) {
    oled_draw_digit(x, page, 0);
    return;
  }

  while (v && n < 10) {
    buf[n++] = '0' + (v % 10);
    v /= 10;
  }

  for (int i = n - 1; i >= 0; i--, x += 6) {
    oled_draw_digit(x, page, buf[i] - '0');
  }
}

static void oled_draw_progress_bar(uint8_t x, uint8_t page, uint8_t current,
                                   uint8_t max, uint8_t width) {
  uint8_t filled = (width * current) / max;
  if (filled > width)
    filled = width;

  for (uint8_t i = 0; i < width; i++) {
    if (i < filled) {
      oled_draw_icon(x + i * 6, page, ICON_BLOCK_FULL);
    } else {
      oled_draw_icon(x + i * 6, page, ICON_BLOCK_EMPTY);
    }
  }
}

/* ---------- New: Bordered proportional progress bar (1 page tall) ----------
 */
// width_cols: bar width in columns (pixels) on this page (>=4 recommended).
// value/max: filled proportion from 0..max; clamps applied.
static void oled_draw_bordered_progress(uint8_t x, uint8_t page,
                                        uint8_t width_cols, uint8_t value,
                                        uint8_t max) {
  if (width_cols < 4)
    return; // not enough room for borders
  if (max == 0)
    max = 1;
  if (value > max)
    value = max;

  static uint8_t colbuf[128];
  uint8_t w = width_cols;
  if (w > sizeof(colbuf))
    w = sizeof(colbuf);

  // Interior width (between vertical borders)
  uint8_t interior = (w >= 2) ? (uint8_t)(w - 2) : 0;
  uint8_t fill_cols = (uint8_t)((uint16_t)interior * value / max);

  // Left border (vertical line = 7px high)
  colbuf[0] = 0x7F;

  // Interior columns
  for (uint8_t i = 0; i < interior; i++) {
    if (i < fill_cols) {
      // Filled interior (no top/bottom stroke) => solid area
      colbuf[1 + i] = 0x7E;
    } else {
      // Empty interior: keep top & bottom strokes (thin outline feeling)
      colbuf[1 + i] = 0x41;
    }
  }

  // Right border
  if (w >= 2)
    colbuf[w - 1] = 0x7F;

  // Blit one scanline
  oled_setpos(page, x);
  oled_data(colbuf, w);
}

/* ============================================================================
 * Public Functions
 * ============================================================================
 */
void oled_clear(void) {
  uint8_t z[128] = {0};
  for (uint8_t p = 0; p < 8; p++) {
    oled_setpos(p, 0);
    oled_data(z, 128);
  }
}

void oled_init(void) {
  I2C1_Init_OLED();

  // Initialization sequence
  oled_cmd(0xAE);
  oled_cmd(0xD5);
  oled_cmd(0x80);
  oled_cmd(0xA8);
  oled_cmd(0x3F);
  oled_cmd(0xD3);
  oled_cmd(0x00);
  oled_cmd(0x40);
  oled_cmd(0x8D);
  oled_cmd(0x14);
  oled_cmd(0x20);
  oled_cmd(0x00);
  oled_cmd(0xA1);
  oled_cmd(0xC8);
  oled_cmd(0xDA);
  oled_cmd(0x12);
  oled_cmd(0x81);
  oled_cmd(0x7F);
  oled_cmd(0xD9);
  oled_cmd(0xF1);
  oled_cmd(0xDB);
  oled_cmd(0x40);
  oled_cmd(0xA4);
  oled_cmd(0xA6);
  oled_cmd(0xAF);

  oled_clear();
}

void OLED_ShowStatus(void) {
  oled_clear();

  // Line 0: LEVEL <num>  <hearts>
  oled_print_text(0, 0, "LEVEL ");
  oled_print_uint(6 * 7, 0, g_level);

  // Draw hearts for lives (right side)
  uint8_t heart_x = 84;
  for (uint8_t i = 0; i < INITIAL_LIVES; i++) {
    if (i < g_lives) {
      oled_draw_icon(heart_x + i * 7, 0, ICON_HEART);
    } else {
      oled_draw_icon(heart_x + i * 7, 0, ICON_HEART_EMPTY);
    }
  }

  // Line 2: SCORE: <number>
  oled_print_text(0, 2, "SCORE:");
  oled_print_uint(6 * 7, 2, g_score);

  // Line 4: SPD:<num>  [bordered proportional bar]
  oled_print_text(0, 4, "SPD:");
  oled_print_uint(6 * 5, 4, g_difficulty);

  // Proportional bar (5 => 100%)
  uint8_t spd_val =
      (g_difficulty < 1) ? 1 : (g_difficulty > 5 ? 5 : g_difficulty);
  uint8_t spd_bar_x = 6 * 8; // position after text/number
  uint8_t spd_bar_w =
      60; // width in columns; tweak as desired (40–80 looks good)
  oled_draw_bordered_progress(spd_bar_x, 4, spd_bar_w, spd_val, 5);

  // Line 4 (continued): Pattern progress (kept as your original block-style
  // bar)
  if (g_pattern_length > 0) {
    oled_draw_progress_bar(60, 4, g_pattern_index, g_pattern_length, 10);
  }

  // Line 6-7: Status message (larger, more descriptive)
  oled_print_text(0, 6, ">");
  switch (g_game_state) {
  case GAME_STATE_VICTORY:
    oled_print_text(12, 6, "VICTORY");
    break;
  case GAME_STATE_GAME_DEATH:
    oled_print_text(12, 6, "GAME OVER");
    break;
  case GAME_STATE_PATTERN_DISPLAY:
    oled_print_text(12, 6, "WATCH");
    break;
  case GAME_STATE_INPUT_WAIT:
    oled_print_text(12, 6, "YOUR TURN");
    break;
  case GAME_STATE_DIFFICULTY_SELECT:
    oled_print_text(12, 6, "SELECT SPEED");
    break;
  case GAME_STATE_LEVEL_INTRO:
    oled_print_text(12, 6, "GET READY");
    break;
  default:
    oled_print_text(12, 6, "READY");
    break;
  }
}