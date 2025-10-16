/* ============================================================================
 * OLED Display Driver Implementation
 * SF1106/SSD1306 over I2C1
 * Layout: [section1 left]|[section2 right]
 * Footer centered at bottom
 * ============================================================================
 */

#include "oled.h"
#include "game.h"
#include <stdint.h>
#include <string.h> // for memset

#define STM32F411xE
#include "stm32f4xx.h"

/* ---------------- External game-side globals (expected) ------------------- */
// Provided by your game code:
extern uint8_t g_level;
extern uint8_t g_lives;
extern uint32_t g_score;
extern uint8_t g_difficulty; // 1..5
extern uint8_t g_pattern_index;
extern uint8_t g_pattern_length;
extern GameState_t g_game_state;

// NEW: countdown value for section2 big number (10..0)
volatile uint8_t g_countdown = 10;

/* ---------------- OLED addressing & constants (expected) ------------------ */
#ifndef OLED_ADDR
#define OLED_ADDR 0x3C
#endif

#ifndef OLED_COL_OFFSET
#define OLED_COL_OFFSET 0
#endif

#ifndef INITIAL_LIVES
#define INITIAL_LIVES 3
#endif

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
  I2C1->CR2 = 42;  // APB1 = 42 MHz
  I2C1->CCR = 210; // 100 kHz
  I2C1->TRISE = 43;
  I2C1->CR1 = I2C_CR1_PE;
}

static void i2c_start(uint8_t addr) {
  I2C1->CR1 |= I2C_CR1_START;
  while (!(I2C1->SR1 & I2C_SR1_SB)) {
  }
  (void)I2C1->SR1;
  I2C1->DR = (uint8_t)(addr << 1);
  while (!(I2C1->SR1 & I2C_SR1_ADDR)) {
  }
  (void)I2C1->SR1;
  (void)I2C1->SR2;
}

static void i2c_w(uint8_t b) {
  while (!(I2C1->SR1 & I2C_SR1_TXE)) {
  }
  I2C1->DR = b;
  while (!(I2C1->SR1 & I2C_SR1_BTF)) {
  }
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
 * Font Data (5x7)
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
    /*P*/ {0x7F, 0x09, 0x09, 0x09, 0x06, 0x00}, /* <-- fixed trailing 0x00 */
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

/* Custom Icons (upright) */
static const uint8_t ICON_HEART[6] = {0x36, 0x7F, 0x7F, 0x3E, 0x1C, 0x08};
static const uint8_t ICON_HEART_EMPTY[6] = {0x22, 0x41, 0x41, 0x22, 0x14, 0x08};
static const uint8_t ICON_BLOCK_FULL[6] = {0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x00};
static const uint8_t ICON_BLOCK_EMPTY[6] = {0x7F, 0x41, 0x41, 0x41, 0x7F, 0x00};

/* Rotated 90° CW heart icons (8 columns wide) */
static const uint8_t ICON_HEART_ROT90[8] = {0x18, 0x3C, 0x3E, 0x1F,
                                            0x3E, 0x3C, 0x18, 0x00};
static const uint8_t ICON_HEART_EMPTY_ROT90[8] = {0x18, 0x24, 0x02, 0x01,
                                                  0x02, 0x24, 0x18, 0x00};

/* ============================================================================
 * Text & Drawing Helpers
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

/* Draw an 8-column icon (used for rotated hearts) */
static void oled_draw_icon8(uint8_t x, uint8_t page, const uint8_t *icon8) {
  oled_setpos(page, x);
  oled_data(icon8, 8);
}

/* Horizontal flip for 8-col icons (fix mirrored hearts) */
static void oled_draw_icon8_hflip(uint8_t x, uint8_t page,
                                  const uint8_t *icon8) {
  uint8_t buf[8];
  for (int i = 0; i < 8; ++i)
    buf[i] = icon8[7 - i];
  oled_setpos(page, x);
  oled_data(buf, 8);
}

/* Bordered proportional progress (1 page tall) */
static void oled_draw_bordered_progress(uint8_t x, uint8_t page,
                                        uint8_t width_cols, uint8_t value,
                                        uint8_t max) {
  if (width_cols < 4)
    return;
  if (max == 0)
    max = 1;
  if (value > max)
    value = max;

  static uint8_t colbuf[128];
  uint8_t w = width_cols;
  if (w > sizeof(colbuf))
    w = sizeof(colbuf);

  uint8_t interior = (uint8_t)(w - 2);
  uint8_t fill_cols = (uint8_t)((uint16_t)interior * value / max);

  colbuf[0] = 0x7F; // left border
  for (uint8_t i = 0; i < interior; i++)
    colbuf[1 + i] = (i < fill_cols) ? 0x7E : 0x41; // filled vs empty
  colbuf[w - 1] = 0x7F;                            // right border

  oled_setpos(page, x);
  oled_data(colbuf, w);
}

/* Clear a specific region (one page: [col_start, col_end) ) */
static void oled_clear_region(uint8_t page, uint8_t col_start,
                              uint8_t col_end) {
  if (col_end <= col_start)
    return;
  uint8_t w = (uint8_t)(col_end - col_start);
  if (w > 128)
    w = 128;
  uint8_t z[128];
  memset(z, 0, w);
  oled_setpos(page, col_start);
  oled_data(z, w);
}

/* Print ASCII (5x7) starting at (x,page) */
static void oled_print_text(uint8_t x, uint8_t page, const char *s) {
  uint8_t cx = x;
  while (*s) {
    char c = *s++;
    if (c == ' ') {
      oled_setpos(page, cx);
      oled_data(FONT5x7_SPACE, 6);
      cx += 6;
      continue;
    }
    if (c >= 'A' && c <= 'Z') {
      oled_setpos(page, cx);
      oled_data(FONT5x7_LET[c - 'A'], 6);
    } else if (c >= '0' && c <= '9') {
      oled_setpos(page, cx);
      oled_data(FONT5x7_DIGIT[c - '0'], 6);
    } else if (c == '-') {
      oled_setpos(page, cx);
      oled_data(FONT5x7_MINUS, 6);
    } else if (c == ':') {
      oled_setpos(page, cx);
      oled_data(FONT5x7_COLON, 6);
    } else {
      oled_setpos(page, cx);
      oled_data(FONT5x7_SPACE, 6);
    }
    cx += 6;
  }
}

/* Print unsigned integer with 5x7 digits */
static void oled_print_uint(uint8_t x, uint8_t page, uint32_t v) {
  char buf[12]; // enough for 32-bit
  int idx = 11;
  buf[idx] = 0;
  idx--;
  if (v == 0) {
    buf[idx] = '0';
    idx--;
  }
  while (v > 0 && idx >= 0) {
    buf[idx] = (char)('0' + (v % 10));
    v /= 10;
    idx--;
  }
  oled_print_text(x, page, &buf[idx + 1]);
}

/* -------- Centering helpers (within a column range) -------- */
static uint8_t text_width_5x7(const char *s) {
  uint16_t n = 0;
  while (*s++)
    n++;
  return (uint8_t)(n * 6);
}

static void oled_print_centered(uint8_t page, uint8_t col_l, uint8_t col_r,
                                const char *s) {
  uint8_t W = (uint8_t)(col_r - col_l);
  uint8_t tw = text_width_5x7(s);
  uint8_t x = col_l + (uint8_t)((W > tw) ? ((W - tw) / 2) : 0);
  oled_print_text(x, page, s);
}

/* ----------------- 2x scaled big digit (from 5x7) -----------------
 * Renders one digit as ~10x14 pixels (2 pages tall)
 * Each original column is duplicated horizontally; each row is doubled
 * vertically. Drawn across pages (page_top) and (page_top+1).
 */
static void oled_draw_big_digit2x(uint8_t x, uint8_t page_top, int d) {
  if (d < 0 || d > 9)
    return;
  const uint8_t *src = FONT5x7_DIGIT[d];

  // Build two page buffers of width 10 (5 cols doubled) + optional 2-col
  // spacing
  uint8_t w = 10;
  uint8_t top[12];    // <=12 safety
  uint8_t bottom[12]; // <=12 safety
  memset(top, 0, sizeof(top));
  memset(bottom, 0, sizeof(bottom));

  uint8_t outc = 0;
  for (uint8_t c = 0; c < 6; c++) {
    if (c == 5)
      break;            // last column in font is blank spacing
    uint8_t b = src[c]; // bit0..bit6 used

    // Build doubled vertical mapping into two pages:
    // rows 0..3 (doubled -> 0..7) go to top page
    // rows 4..6 (doubled -> 8..13) go to bottom page positions 0..5
    uint8_t top_byte = 0, bot_byte = 0;
    for (uint8_t row = 0; row < 7; row++) {
      if (b & (1u << row)) {
        uint8_t y0 = (uint8_t)(2 * row);
        uint8_t y1 = (uint8_t)(y0 + 1);
        if (y1 <= 7) {
          // stays on top page
          top_byte |= (uint8_t)(1u << y0);
          top_byte |= (uint8_t)(1u << y1);
        } else {
          // goes to bottom page (shifted by -8)
          uint8_t yb0 = (uint8_t)(y0 - 8);
          uint8_t yb1 = (uint8_t)(y1 - 8);
          if (yb0 < 8)
            bot_byte |= (uint8_t)(1u << yb0);
          if (yb1 < 8)
            bot_byte |= (uint8_t)(1u << yb1);
        }
      }
    }

    // duplicate horizontally
    if (outc < sizeof(top)) {
      top[outc] = top_byte;
      bottom[outc] = bot_byte;
      outc++;
    }
    if (outc < sizeof(top)) {
      top[outc] = top_byte;
      bottom[outc] = bot_byte;
      outc++;
    }
  }

  // Write to OLED: top page then bottom page
  oled_setpos(page_top, x);
  oled_data(top, outc);
  oled_setpos((uint8_t)(page_top + 1), x);
  oled_data(bottom, outc);
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
  oled_cmd(0xA1); // segment remap
  oled_cmd(0xC8); // COM scan dir
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

/* ============================================================================
 * New Layout Constants
 * ============================================================================
 */
// Screen split
#define COL_SPLIT 64 // [0..63] section1 | [64..127] section2

// Section1: rows/pages used
#define S1_COL_L 0
#define S1_COL_R COL_SPLIT
#define S1_PAGE_LABEL 0 // "LEVEL", hearts same page
#define S1_PAGE_SPEED 2 // "SPD" + number + bar

// Section2
#define S2_COL_L COL_SPLIT
#define S2_COL_R 128
#define S2_PAGE_TITLE 0   // "TIME"
#define S2_PAGE_BIG_TOP 2 // big number uses page 2 and 3

// Footer
#define FOOTER_PAGE 7

// Speed bar width (inside section1)
#define W_SPD_BAR 54

/* ----------------------------- Rendering ---------------------------------- */
static void draw_section1(void) {
  // Clear region pages we use
  oled_clear_region(S1_PAGE_LABEL, S1_COL_L, S1_COL_R);
  oled_clear_region(S1_PAGE_SPEED, S1_COL_L, S1_COL_R);
  oled_clear_region((uint8_t)(S1_PAGE_SPEED + 1), S1_COL_L, S1_COL_R); // safety

  // LEVEL label + number (left)
  oled_print_text(S1_COL_L + 0, S1_PAGE_LABEL, "LEVEL");
  oled_print_uint((uint8_t)(S1_COL_L + 6 * 6), S1_PAGE_LABEL, g_level);

  // LIVES (hearts) right-aligned within section1
  {
    // each heart drawn with 8 columns + 1 spacing
    uint8_t total_w = (uint8_t)(INITIAL_LIVES * 9);
    uint8_t start_x =
        (uint8_t)((S1_COL_R - S1_COL_L > total_w) ? (S1_COL_R - total_w - 2)
                                                  : (S1_COL_L + 2));
    for (uint8_t i = 0; i < INITIAL_LIVES; i++) {
      uint8_t x = (uint8_t)(start_x + i * 9);
      if (i < g_lives)
        oled_draw_icon8_hflip(x, S1_PAGE_LABEL, ICON_HEART_ROT90);
      else
        oled_draw_icon8_hflip(x, S1_PAGE_LABEL, ICON_HEART_EMPTY_ROT90);
    }
  }

  // SPEED line
  oled_print_text(S1_COL_L + 0, S1_PAGE_SPEED, "SPD");
  // numeric value
  oled_print_uint((uint8_t)(S1_COL_L + 6 * 4), S1_PAGE_SPEED, g_difficulty);

  // draw speed bar within section1 width
  {
    uint8_t bar_x = (uint8_t)(S1_COL_R - W_SPD_BAR - 2);
    if (bar_x < (S1_COL_L + 6 * 8))
      bar_x = (uint8_t)(S1_COL_L + 6 * 8); // keep some gap from number
    uint8_t spd = g_difficulty;
    if (spd > 5)
      spd = 5;
    oled_draw_bordered_progress(bar_x, S1_PAGE_SPEED, W_SPD_BAR, spd, 5);
  }
}

static void draw_section2(void) {
  // Title "TIME" centered on S2
  oled_clear_region(S2_PAGE_TITLE, S2_COL_L, S2_COL_R);
  oled_print_centered(S2_PAGE_TITLE, S2_COL_L, S2_COL_R, "TIME");

  // Big countdown number centered (uses pages 2 and 3)
  oled_clear_region(S2_PAGE_BIG_TOP, S2_COL_L, S2_COL_R);
  oled_clear_region((uint8_t)(S2_PAGE_BIG_TOP + 1), S2_COL_L, S2_COL_R);

  // Determine digits to display (10 -> "10", else '0'..'9')
  char buf[3] = {0};
  if (g_countdown == 10) {
    buf[0] = '1';
    buf[1] = '0';
  } else {
    buf[0] = (char)('0' + (g_countdown % 10));
    buf[1] = 0;
  }

  // Big digit width ≈ 10 columns each, plus 2 cols spacing between
  uint8_t digits = (uint8_t)((buf[1] == 0) ? 1 : 2);
  uint8_t total_w = (uint8_t)(digits * 10 + ((digits > 1) ? 2 : 0));

  // Center horizontally within section2
  uint8_t s2w = (uint8_t)(S2_COL_R - S2_COL_L);
  uint8_t x0 =
      (uint8_t)(S2_COL_L + ((s2w > total_w) ? ((s2w - total_w) / 2) : 0));

  // Draw
  uint8_t x = x0;
  for (uint8_t i = 0; i < digits; i++) {
    int d = buf[i] - '0';
    oled_draw_big_digit2x(x, S2_PAGE_BIG_TOP, d);
    x = (uint8_t)(x + 10 + 2); // 2 col spacing
  }
}

static const char *state_text(GameState_t s) {
  switch (s) {
  case GAME_STATE_VICTORY:
    return "VICTORY";
  case GAME_STATE_GAME_DEATH:
    return "GAME OVER";
  case GAME_STATE_PATTERN_DISPLAY:
    return "WATCH";
  case GAME_STATE_INPUT_WAIT:
    return "YOUR TURN";
  case GAME_STATE_DIFFICULTY_SELECT:
    return "SELECT SPEED";
  case GAME_STATE_LEVEL_INTRO:
    return "GET READY";
  default:
    return "READY";
  }
}

static void draw_footer(void) {
  oled_clear_region(FOOTER_PAGE, 0, 128);
  oled_print_centered(FOOTER_PAGE, 0, 128, state_text(g_game_state));
}

/* ============================================================================
 * Single entry to render the whole screen with the new layout
 * ============================================================================
 */
void OLED_ShowStatus(void) {
  // You can keep selective updates with prev_* if you like; for clarity we
  // redraw the three zones.
  draw_section1();
  draw_section2();
  draw_footer();
}
