#ifndef AD7767_DRIVER_H
#define AD7767_DRIVER_H

#include "stm32f4xx_hal.h"

#define AD7767_CH_COUNT   3
#define AD7767_CH1        0
#define AD7767_CH2        1
#define AD7767_CH3        2

typedef enum {
    AD7767_OK = 0,
    AD7767_ERR_NOT_READY,   /* DRDY 未就绪 → 本次跳过（"宁少不错"，D-05） */
    AD7767_ERR_TIMEOUT,     /* SPI 等待超时（DWT 计数） */
    AD7767_ERR_PARAM,       /* ch 越界 */
} AD7767_Status_t;


typedef struct {
    SPI_HandleTypeDef *spi;        /* &hspi1 / &hspi2 / &hspi3 */
    GPIO_TypeDef      *cs_port;    /* CS  = SPIx_NSS 引脚 */
    uint16_t           cs_pin;
    GPIO_TypeDef      *drdy_port;  /* PG15 / PH12 / PA15 */
    uint16_t           drdy_pin;
    GPIO_TypeDef      *sync_port;  /* PB7 / PD9 / PD1 */
    uint16_t           sync_pin;
} AD7767_Device_t;

void AD7767_InitAll(void);
void AD7767_PowerDownAll(void);
void AD7767_Sync(uint8_t ch);
AD7767_Status_t AD7767_ReadRaw24(uint8_t ch, uint32_t *raw);
int32_t AD7767_Sign24(uint32_t raw);
int32_t AD7767_ToMicroVolt(uint32_t raw);
uint8_t AD7767_DrdyIsLow(uint8_t ch);

#endif