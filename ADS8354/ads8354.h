#ifndef ADS8345_DRIVER_H
#define ADS8345_DRIVER_H

#include "stm32f4xx_hal.h"
#define ADC_CFR_W 0x8
#define ADC_AVEG  5
//#define MODE_MB 0
#define UART_TX_FREQ 80

#define  SPI_csPin1   GPIO_PIN_6
#define  Spi1Enable    HAL_GPIO_WritePin(GPIOB, SPI_csPin1, GPIO_PIN_RESET)//片选低有效
#define  Spi1Disable   HAL_GPIO_WritePin(GPIOB, SPI_csPin1, GPIO_PIN_SET)

#define  SPI_csPin2   GPIO_PIN_8
#define  Spi2Enable    HAL_GPIO_WritePin(GPIOD, SPI_csPin2, GPIO_PIN_RESET)//片选低有效
#define  Spi2Disable   HAL_GPIO_WritePin(GPIOD, SPI_csPin2, GPIO_PIN_SET)

#define  SPI_csPin3   GPIO_PIN_0
#define  Spi3Enable    HAL_GPIO_WritePin(GPIOD, SPI_csPin3, GPIO_PIN_RESET)//片选低有效
#define  Spi3Disable   HAL_GPIO_WritePin(GPIOD, SPI_csPin3, GPIO_PIN_SET)

#define I2C_HDC_ADDRESS ((uint16_t)(0x40 << 1))
#define SMOOTH_TEMP_NUMBER (uint16_t)4
#define I2C_BLOCKING_DELAY 5
//uint16_t CoilTemp;    /* Coil temperature */
//uint16_t CoilHumi;    /* Coil relative humidity */

void Ads8354ReadReg();
void Clean_Txdata(void);
void Ads8354Write(uint8_t func, uint16_t data);
void Ads8354Init();
uint32_t Ads8354ReadConvers();
int GetChanBVoltage(int a, int b);
uint16_t Ads8354ReadChanA();
uint16_t Ads8354ReadReg2(uint8_t addr, uint8_t *rxBuf);
uint16_t Send_Adcdata(void);
uint16_t Send_MBdata1(void);
void Start_Hdc1010(void);
uint16_t Init_Hdc1010(void);
uint16_t Read_Hdc1010_a(void);
uint8_t IAP_DataCheck(uint8_t *data,uint16_t len);
uint16_t swap_word(uint16_t word);
//void Cal_CRC16(uint8_t *buf, int len);

#endif
