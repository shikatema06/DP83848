#include "ads8354.h"
#include "spi.h"
#include "usart.h"
extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern SPI_HandleTypeDef hspi3;
//extern I2C_HandleTypeDef hi2c1;

extern uint8_t AdcDate_ready;
extern int16_t valChan2;
extern uint8_t SendData_en;
extern uint16_t Package_index;
extern uint8_t clean_data_flag;

//extern uint8_t uart1RxBuf[RX_SIZE];
//extern uint8_t uart1TxBuf[RX_SIZE];
//extern uint8_t uart2RxBuf[RX_SIZE];
//extern uint8_t uart2TxBuf[RX_SIZE];
//extern uint8_t uart3RxBuf[RX_SIZE];
//extern uint8_t uart3TxBuf[RX_SIZE];
extern int16_t  Adc_avg[3];
extern int8_t  Adcavg_en[3];

HAL_StatusTypeDef  spi_state;

uint8_t  data_flag=0;
uint8_t spi_error=0;
uint16_t Tx_cnt = 0;
uint16_t CoilTemp;    /* Coil temperature */
uint16_t CoilHumi;    /* Coil relative humidity */
uint16_t ADC_cyc_cnt = 0;
uint16_t ADC_tx_cyc = UART_TX_FREQ;
uint16_t Tx_cnt1=0;
uint16_t Tx_cnt2=0;
uint16_t Tx_cnt3=0;


//
//uint16_t Ads8354ReadReg2(uint8_t addr, uint8_t *rxBuf)
//{
//    uint8_t txBuf[6] = {0};
//
//    txBuf[0] = (addr << 4) & 0xf0;
//    Spi2Write(SPI_CHAN2, txBuf, 6);
//
//    txBuf[0] = 0;
//    Spi2WriteRead(SPI_CHAN2, txBuf, rxBuf, 6);
//
//    return 0;
//}
//
//
//uint16_t Ads8354ReadChanA()
//{
//
//    uint8_t rxBuf[6];
//    uint8_t txBuf[6] = {0};
//
//    Spi2WriteRead(SPI_CHAN2, txBuf, rxBuf, 6);
//    return (rxBuf[2] << 8) | rxBuf[3];
//}
//
//uint16_t Ads8354ReadChanB()
//{
//
//    uint8_t rxBuf[6];
//    uint8_t txBuf[6] = {0};
//
//    Spi2WriteRead(SPI_CHAN2, txBuf, rxBuf, 6);
//    return (rxBuf[4] << 8) | rxBuf[5];
//}
//
//
//uint32_t Ads8354ReadConvers()
//{
//    uint32_t val;
//    uint8_t rxBuf[6];
//    uint8_t txBuf[6] = {0};
//
//    Spi2WriteRead(SPI_CHAN2, txBuf, rxBuf, 6);
//    val = (rxBuf[2] << 24) + (rxBuf[3] << 16) +(rxBuf[4] << 8) + rxBuf[5];
//    return val;
//}
//
//int GetChanBVoltage(int a, int b)
//{
//    int16_t valChanB;
//    int voltage; 
//    valChanB = Ads8354ReadConvers() & 0xffff;
//    voltage = valChanB * 2500 / 32768; // mv
//
//    rt_kprintf("ChanB reg val: 0x%x, voltage %d\r\n", valChanB, voltage);
//    return voltage * a / b; //  分压比
//}

void Ads8354Write(uint8_t func, uint16_t data)
{
    uint8_t cmd[6] = {0};

    cmd[0] = ((func << 4) & 0xf0) | ((data >> 8) & 0xf);
    cmd[1] = data & 0xff;
    
    Spi1Enable;
      
  //  HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size, uint32_t Timeout)
    spi_state=HAL_OK;
    spi_state=HAL_SPI_Transmit(&hspi1, cmd, 6, 10); 
    if (spi_state != HAL_OK) 
    {
        spi_error=1;
    }
    
    Spi1Disable;
    
}

void Ads8354Init()
{
    // single sdo mode
    Ads8354Write(ADC_CFR_W, 0x400);
}

//void Ads8354ReadReg()
//{
//    //uint8_t spirxBuf[6] = {0};
//     Spi1Enable;
//     spi_state=HAL_OK;
//     spi_state=HAL_SPI_Receive(&hspi1, (uint8_t*) &uart1TxBuf, 6, 10);
//     if (spi_state != HAL_OK) 
//    {
//        spi_error=1;
//    }
//    Spi1Disable;
//
//}

uint32_t Ads8354ReadConvers(SPI_HandleTypeDef *hspi)
{
    uint16_t val;
    uint8_t rxBuf[6];
    uint8_t txBuf[6] = {0};
    if(hspi==&hspi1)
      Spi1Enable;
     if(hspi==&hspi2)
      Spi2Enable;
     if(hspi==&hspi3)
      Spi3Enable;
    
    spi_state=HAL_OK;
    spi_state=HAL_SPI_TransmitReceive(hspi,(uint8_t*)&txBuf, (uint8_t*)&rxBuf, 6,10);
    if (spi_state != HAL_OK) 
    {
        spi_error=1;
    }
    if(hspi==&hspi1)
      Spi1Disable;
    if(hspi==&hspi2)
      Spi2Disable;
    if(hspi==&hspi3)
      Spi3Disable;

    //Spi2WriteRead(SPI_CHAN2, txBuf, rxBuf, 6);
    //val = (rxBuf[2] << 24) + (rxBuf[3] << 16) +(rxBuf[4] << 8) + rxBuf[5];
    val = (rxBuf[2] << 8) + rxBuf[3] ;
    return val;
}




uint16_t Send_Adcdata(void)
{
    return 0;
}





//uint16_t Init_Hdc1010(void)
//{
//#define Hdc1010_CfgPointer ((uint16_t)0x2)
//#define Hdc1010_CfgReg ((uint16_t)(1 << 12 | 0 << 10 | 0 << 8))
//    uint16_t data;
//    data = (uint16_t)Hdc1010_CfgReg;
//    data = swap_word(data);
//    return HAL_I2C_Mem_Write(&hi2c1, I2C_HDC_ADDRESS,
//                             Hdc1010_CfgPointer, 1, (uint8_t *)&data, 2, I2C_BLOCKING_DELAY);
//}

//void Start_Hdc1010(void)
//{
//    uint16_t data = 0;
//    HAL_StatusTypeDef status;
//    status = HAL_I2C_Master_Transmit(&hi2c1, I2C_HDC_ADDRESS, (uint8_t *)&data, 1,
//                                     I2C_BLOCKING_DELAY);
//    if (status != HAL_OK)
//    {
//
//    }
//}

//uint16_t Read_Hdc1010_a(void)
//{
//    static uint16_t Hdc_temp_queue[SMOOTH_TEMP_NUMBER];
//    static uint16_t Hdc_humi_queue[SMOOTH_TEMP_NUMBER];
//    static uint16_t index = 0;
//    static uint16_t elements = 0;
//    int32_t iTemp, iHumi, tmp;
//    uint16_t data[2];
//    HAL_StatusTypeDef status;
//    status = HAL_I2C_Master_Receive(&hi2c1, I2C_HDC_ADDRESS, (uint8_t *)&data, 4, 0x100);
//    if (status != HAL_OK)
//        return status;
//
//#define HDC_TEMP_CONSTANT (uint32_t)(40 * 25 * 2 ^ 16 - 8192)
//    iTemp = swap_word(data[0]) * 4125; /* 4125 = 165*25 */
//    tmp = iTemp - 65527808;
//    //  iTemp += 1 << 13;
//    iTemp = tmp >> 14;
//
//    iHumi = swap_word(data[1]) * 10000;
//    iHumi >>= 16;
//
//    Hdc_temp_queue[index] = (uint16_t)iTemp;
//    Hdc_humi_queue[index] = (uint16_t)iHumi;
//
//    if (elements < SMOOTH_TEMP_NUMBER)
//    {
//        elements++;
//    }
//
//    iTemp = 0;
//    iHumi = 0;
//    for (uint16_t i = 0; i < elements; i++)
//    {
//        iTemp += Hdc_temp_queue[i];
//        iHumi += Hdc_humi_queue[i];
//    }
//
//    iTemp /= elements;
//    iHumi /= elements;
//    CoilTemp = iTemp;
//    CoilHumi = iHumi;
//
//    if (index < SMOOTH_TEMP_NUMBER - 1)
//        index++;
//    else
//        index = 0;
//
//    return 0;
//}
uint8_t IAP_DataCheck(uint8_t *data,uint16_t len)
{
  uint8_t sum=0;
  for(int i=0;i < len;i++)
  {
    sum +=*(data+i);
  }
  sum=sum&0xff; 
  return sum;

}

uint16_t swap_word(uint16_t word)
{
	asm("rev16 r0,r0");
	return word;
}




