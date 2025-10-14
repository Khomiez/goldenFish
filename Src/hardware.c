/* ============================================================================
 * Hardware Control Implementation
 * ============================================================================ */

#include "hardware.h"
#include "utils.h"

#define STM32F411xE
#include "stm32f4xx.h"

/* Global Variables */
uint32_t SystemCoreClock = 84000000;
ButtonState_t g_buttons[4];
uint16_t g_adc_values[3] = {0};
uint8_t g_current_adc_channel = 0;

/* ============================================================================
 * System Initialization
 * ============================================================================ */
void SystemClock_Config(void) {
    RCC->CR |= RCC_CR_HSION;
    while(!(RCC->CR & RCC_CR_HSIRDY));

    RCC->PLLCFGR = (RCC_PLLCFGR_PLLSRC_HSI) |
                   (16 << RCC_PLLCFGR_PLLM_Pos) |
                   (168 << RCC_PLLCFGR_PLLN_Pos) |
                   (0 << RCC_PLLCFGR_PLLP_Pos);

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

    // LEDs as outputs
    GPIOA->MODER |= (1 << (LED1_PIN*2)) | (1 << (LED2_PIN*2)) | (1 << (LED3_PIN*2));
    GPIOB->MODER |= (1 << (LED4_PIN*2));

    // Buttons with pull-ups
    GPIOA->PUPDR |= (1 << (BTN0_PIN*2));
    GPIOB->PUPDR |= (1 << (BTN1_PIN*2)) | (1 << (BTN2_PIN*2)) | (1 << (BTN3_PIN*2));

    // ADC pins as analog
    GPIOA->MODER |= (3 << (POT_PIN*2)) | (3 << (TEMP_PIN*2)) | (3 << (LIGHT_PIN*2));

    // UART2: PA2, PA3 as AF7
    GPIOA->MODER |= (2 << (2*2)) | (2 << (3*2));
    GPIOA->AFR[0] |= (7 << (2*4)) | (7 << (3*4));

    // 7-Segment BCD outputs
    GPIOC->MODER = (GPIOC->MODER & ~(3U << (BCD_2_0_PIN*2))) | (1U << (BCD_2_0_PIN*2));
    GPIOA->MODER = (GPIOA->MODER & ~((3U << (BCD_2_1_PIN*2)) | (3U << (BCD_2_3_PIN*2)))) |
                   (1U << (BCD_2_1_PIN*2)) | (1U << (BCD_2_3_PIN*2));
    GPIOB->MODER = (GPIOB->MODER & ~(3U << (BCD_2_2_PIN*2))) | (1U << (BCD_2_2_PIN*2));
}

void ADC_Init(void) {
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;
    ADC1->CR2 |= ADC_CR2_ADON;
    ADC1->CR1 |= ADC_CR1_EOCIE;
    ADC1->CR1 |= (1 << ADC_CR1_RES_Pos); // 10-bit resolution
    ADC1->SMPR2 |= (7 << ADC_SMPR2_SMP0_Pos) |
                   (7 << ADC_SMPR2_SMP1_Pos) |
                   (7 << ADC_SMPR2_SMP4_Pos);
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
 * Hardware Monitoring
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

void Monitor_ADC(void) {
    /* ADC handled via interrupt */
}

/* ============================================================================
 * Hardware Control
 * ============================================================================ */
void LED_SetPattern(uint8_t pattern) {
    (pattern & 0x01) ? (LED1_PORT->BSRR = (1 << LED1_PIN)) :
                       (LED1_PORT->BSRR = (1 << (LED1_PIN + 16)));
    (pattern & 0x02) ? (LED2_PORT->BSRR = (1 << LED2_PIN)) :
                       (LED2_PORT->BSRR = (1 << (LED2_PIN + 16)));
    (pattern & 0x04) ? (LED3_PORT->BSRR = (1 << LED3_PIN)) :
                       (LED3_PORT->BSRR = (1 << (LED3_PIN + 16)));
    (pattern & 0x08) ? (LED4_PORT->BSRR = (1 << LED4_PIN)) :
                       (LED4_PORT->BSRR = (1 << (LED4_PIN + 16)));
}

void SevenSeg_Display(uint8_t digit) {
    if(digit > 9) return;
    (digit & 0x01) ? (BCD_2_0_PORT->BSRR = (1 << BCD_2_0_PIN)) :
                     (BCD_2_0_PORT->BSRR = (1 << (BCD_2_0_PIN + 16)));
    (digit & 0x02) ? (BCD_2_1_PORT->BSRR = (1 << BCD_2_1_PIN)) :
                     (BCD_2_1_PORT->BSRR = (1 << (BCD_2_1_PIN + 16)));
    (digit & 0x04) ? (BCD_2_2_PORT->BSRR = (1 << BCD_2_2_PIN)) :
                     (BCD_2_2_PORT->BSRR = (1 << (BCD_2_2_PIN + 16)));
    (digit & 0x08) ? (BCD_2_3_PORT->BSRR = (1 << BCD_2_3_PIN)) :
                     (BCD_2_3_PORT->BSRR = (1 << (BCD_2_3_PIN + 16)));
}

/* ============================================================================
 * Interrupt Handler
 * ============================================================================ */
void ADC_IRQHandler(void) {
    if(ADC1->SR & ADC_SR_EOC) {
        g_adc_values[g_current_adc_channel] = ADC1->DR;
        g_current_adc_channel = (g_current_adc_channel + 1) % 3;
        ADC1->SQR3 = (ADC1->SQR3 & ~ADC_SQR3_SQ1) |
                     (g_current_adc_channel == 0 ? POT_PIN :
                     (g_current_adc_channel == 1 ? TEMP_PIN : LIGHT_PIN));
        ADC1->CR2 |= ADC_CR2_SWSTART;
    }
}
