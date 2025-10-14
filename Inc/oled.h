/* ============================================================================
 * OLED Display Driver
 * SH1106/SSD1306 over I2C1 (PB8=SCL, PB9=SDA)
 * ============================================================================ */

#ifndef OLED_H
#define OLED_H

#include <stdint.h>

#define OLED_ADDR       0x3C
#define OLED_COL_OFFSET 2   /* SH1106 = 2, SSD1306 = 0 */

/* Function Prototypes */
void oled_init(void);
void oled_clear(void);
void OLED_ShowStatus(void);

#endif /* OLED_H */
