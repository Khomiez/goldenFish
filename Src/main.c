#define STM32F411xE
#include "stm32f4xx.h"
#include <stdint.h>

uint32_t SystemCoreClock = 84000000; // ต้องกำหนดเองถ้าไม่มี system_stm32f4xx.c

/* delay */
static inline void dly(uint32_t ms){
    SysTick->LOAD = (SystemCoreClock/1000)-1; SysTick->VAL=0;
    SysTick->CTRL = SysTick_CTRL_CLKSOURCE_Msk|SysTick_CTRL_ENABLE_Msk;
    while(ms--) { while(!(SysTick->CTRL & SysTick_CTRL_COUNTFLAG_Msk)); }
    SysTick->CTRL = 0;
}

/* I2C1 init */
static void I2C1_Init(void){
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    GPIOB->MODER &= ~((3<<(8*2))|(3<<(9*2)));
    GPIOB->MODER |=  (2<<(8*2))|(2<<(9*2));
    GPIOB->OTYPER |= (1<<8)|(1<<9);
    GPIOB->OSPEEDR|= (3<<(8*2))|(3<<(9*2));
    GPIOB->PUPDR  |= (1<<(8*2))|(1<<(9*2));
    GPIOB->AFR[1] |= (4<<(0))|(4<<(4));

    RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
    I2C1->CR1 = 0;
    I2C1->CR2 = 42;
    I2C1->CCR = 210;
    I2C1->TRISE = 43;
    I2C1->CR1 = I2C_CR1_PE;
}

/* I2C low-level */
static void i2c_start(uint8_t addr){
    I2C1->CR1 |= I2C_CR1_START;
    while(!(I2C1->SR1 & I2C_SR1_SB));
    (void)I2C1->SR1;
    I2C1->DR = addr<<1;
    while(!(I2C1->SR1 & I2C_SR1_ADDR));
    (void)I2C1->SR1; (void)I2C1->SR2;
}
static void i2c_w(uint8_t b){
    while(!(I2C1->SR1 & I2C_SR1_TXE));
    I2C1->DR = b;
    while(!(I2C1->SR1 & I2C_SR1_BTF));
}
static void i2c_stop(void){ I2C1->CR1 |= I2C_CR1_STOP; }

/* SSD1306 basic */
#define SSD1306_ADDR 0x3C
static void ssd_cmd(uint8_t c){
    i2c_start(SSD1306_ADDR);
    i2c_w(0x00); i2c_w(c);
    i2c_stop();
}
static void ssd_init(void){
    dly(100);
    ssd_cmd(0xAE);
    ssd_cmd(0xA6);
    ssd_cmd(0xAF);
}

/* fill zebra */
static void ssd_fill_zebra(void){
    for(uint8_t page=0; page<8; page++){
        ssd_cmd(0xB0 | page);
        ssd_cmd(0x00);
        ssd_cmd(0x10);
        i2c_start(SSD1306_ADDR);
        i2c_w(0x40);
        for(uint8_t col=0; col<129; col++){
            i2c_w((page%2)?0xFF:0x00);
        }
        i2c_stop();
    }
}

/* main */
int main(void){
    I2C1_Init();
    ssd_init();

    while(1){
        ssd_fill_zebra();
        dly(1000);
    }
}
