#include "md_Uart.h"
#include "usart.h"
#include "ads8354.h"
#include "tcp_task.h"


uint16_t Rx_len = 0;
uint8_t Flag_time6=0;
uint8_t Time_cnt=0;

uint8_t Uart1tx_finsh_en=0;
uint8_t Uart2tx_finsh_en=0;
uint8_t Uart3tx_finsh_en=0;
uint8_t send_data_flag=0;

uint8_t send_data2_flag=0;
uint8_t send_data3_flag=0;
uint8_t uart1RxBuf[RX_SIZE]={0};
uint8_t uart1TxBuf[RX_SIZE]={0};
uint8_t uart2RxBuf[RX_SIZE]={0};
uint8_t uart2TxBuf[RX_SIZE]={0};
uint8_t uart3RxBuf[RX_SIZE]={0};
uint8_t uart3TxBuf[RX_SIZE]={0};

extern uint8_t SendData_en;
int voltage=0;
uint16_t Package_index=0;
uint8_t clean_data_flag=0;


int16_t  Adc_avg[3] = {0} ;
int16_t  Adc_Chan[3] = {0} ;
int32_t  Adc_sum[3] = {0} ;
int8_t  Adc_cnt[3] = {0} ;
int8_t  Adcavg_en[3] = {0} ;


extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern SPI_HandleTypeDef hspi3;

extern TIM_HandleTypeDef htim6;
extern TIM_HandleTypeDef htim7;
extern TIM_HandleTypeDef htim10;

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
extern DMA_HandleTypeDef hdma_usart1_rx;
extern DMA_HandleTypeDef hdma_usart1_tx;
extern DMA_HandleTypeDef hdma_usart2_rx;
extern DMA_HandleTypeDef hdma_usart2_tx;
extern DMA_HandleTypeDef hdma_usart3_rx;
extern DMA_HandleTypeDef hdma_usart3_tx;

extern uint8_t recv_flag;



void UART_RxIdleCallback(UART_HandleTypeDef *huart)
{
  if(huart==&huart1)
  {
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_IDLE))
    {
        __HAL_UART_CLEAR_IDLEFLAG(huart);
        HAL_UART_DMAStop(huart);
        Rx_len = __HAL_DMA_GET_COUNTER(&hdma_usart1_rx);
        Rx_len = RX_SIZE - Rx_len;
 

        if ((Rx_len > 3) && (Rx_len < 10))
        {
          if(uart1RxBuf[2]==0x05 && uart1RxBuf[1]==0x01)
          {
            send_data_flag=1;
            SendData_en=1;
          }
            
          else
          {
            send_data_flag=0;
            SendData_en=0;
          }
            
        
        }
        

     UART485_RevEnable;
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_IDLE);
    HAL_UART_Receive_DMA(&huart1, uart1RxBuf, RX_SIZE);

    }
  }
  if(huart==&huart2)
  {
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_IDLE))
    {
        __HAL_UART_CLEAR_IDLEFLAG(huart);
        HAL_UART_DMAStop(huart);
        Rx_len = __HAL_DMA_GET_COUNTER(&hdma_usart2_rx);
        Rx_len = RX_SIZE - Rx_len;
 

        if ((Rx_len > 3) && (Rx_len < 10))
        {
          if(uart2RxBuf[2]==0x05 && uart2RxBuf[1]==0x02)
            send_data2_flag=1;
          else
            send_data2_flag=0;
        
        }
        

     UART485_RevEnable2;
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
    HAL_UART_Receive_DMA(&huart2, uart2RxBuf, RX_SIZE);

    }
  }
  
   if(huart==&huart3)
  {
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_IDLE))
    {
        __HAL_UART_CLEAR_IDLEFLAG(huart);
        HAL_UART_DMAStop(huart);
        Rx_len = __HAL_DMA_GET_COUNTER(&hdma_usart3_rx);
        Rx_len = RX_SIZE - Rx_len;
 

        if ((Rx_len > 3) && (Rx_len < 10))
        {
          if(uart3RxBuf[2]==0x05 && uart3RxBuf[1]==0x03)
            send_data3_flag=1;
          else
            send_data3_flag=0;
        
        }
        

     UART485_RevEnable3;
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE);
    HAL_UART_Receive_DMA(&huart3, uart3RxBuf, RX_SIZE);

    }
  }
}

void md_Uart1SendcmpCallback()
{
  Uart1tx_finsh_en=1;	
}

void md_Uart2SendcmpCallback()
{
  Uart2tx_finsh_en=1; 	
}

void md_Uart3SendcmpCallback()
{
  Uart3tx_finsh_en=1;    	
}

//Calculate the average value
int32_t Cal_ADC_Aveg( int32_t Adc_sum)
{
      int32_t Adc_Value = Adc_sum/ADC_AVEG;
      return Adc_Value ;
}

int32_t Cal_ADC_Sum( int32_t Adc_sum, int16_t Adc_Value)
{
    Adc_sum += Adc_Value;
    return Adc_sum ;
}

// void Aveg( uint8_t index )
//{
//    Adc_cnt[index]=Adc_cnt[index]+1;
//    if( Adc_cnt[index] <=  ADC_AVEG )
//    {
//       Adc_sum[index] = Cal_ADC_Sum( Adc_sum[index], Adc_Chan[index])
//    }
//    if( Adc_cnt[index] == ADC_AVEG)
//    {
//       Adc_avg[index] = Cal_ADC_Aveg( Adc_sum[index]) ;
//       Adcavg_en[index]=1;
//       Adc_cnt[index]=0;
//       Adc_sum[index]=0;
//    }
//}
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{

  if(htim==&htim6)
  {
      //
      Adc_Chan[0]= Ads8354ReadConvers(&hspi1) & 0xffff; 
      Adc_cnt[0] = Adc_cnt[0]+1;
        
      if( Adc_cnt[0] <=  ADC_AVEG )
      {
         Adc_sum[0] = Cal_ADC_Sum( Adc_sum[0], Adc_Chan[0]) ;
         Adcavg_en[0]=0;
      }
      if( Adc_cnt[0] == ADC_AVEG)
      {
         Adc_avg[0] = Cal_ADC_Aveg( Adc_sum[0]) ;
         
         Adcavg_en[0]=1;
         Adc_cnt[0]=0;
         Adc_sum[0]=0;
      }
    //  
    Adc_Chan[1] = Ads8354ReadConvers(&hspi2) & 0xffff;
    Adc_cnt[1] = Adc_cnt[1]+1;
        
    if( Adc_cnt[1] <=  ADC_AVEG )
    {
       Adc_sum[1] = Cal_ADC_Sum( Adc_sum[1], Adc_Chan[1]) ;
       Adcavg_en[1]=0;
    }
    if( Adc_cnt[1] == ADC_AVEG)
    {
       Adc_avg[1] = Cal_ADC_Aveg( Adc_sum[1]) ;
       
       Adcavg_en[1]=1;
       Adc_cnt[1]=0;
       Adc_sum[1]=0;
    }
    //
    Adc_Chan[2]= Ads8354ReadConvers(&hspi3) & 0xffff;
    Adc_cnt[2] = Adc_cnt[2]+1;
    
    if( Adc_cnt[2] <=  ADC_AVEG )
    {
       Adc_sum[2] = Cal_ADC_Sum( Adc_sum[2], Adc_Chan[2]) ;
       Adcavg_en[2]=0;
    }
    if( Adc_cnt[2] == ADC_AVEG)
    {
       Adc_avg[2] = Cal_ADC_Aveg( Adc_sum[2]) ;
       
       Adcavg_en[2]=1;
       Adc_cnt[2]=0;
       Adc_sum[2]=0;
    }  
      
  }

  if(htim==&htim7  )
  {
//    Adc_Chan[1] = Ads8354ReadConvers(&hspi2) & 0xffff;
//    Adc_cnt[1] = Adc_cnt[1]+1;
//        
//    if( Adc_cnt[1] <=  ADC_AVEG )
//    {
//       Adc_sum[1] = Cal_ADC_Sum( Adc_sum[1], Adc_Chan[1]) ;
//       Adcavg_en[1]=0;
//    }
//    if( Adc_cnt[1] == ADC_AVEG)
//    {
//       Adc_avg[1] = Cal_ADC_Aveg( Adc_sum[1]) ;
//       
//       Adcavg_en[1]=1;
//       Adc_cnt[1]=0;
//       Adc_sum[1]=0;
//    }
  }
 
 if(htim==&htim10)
  {
//    Adc_Chan[2]= Ads8354ReadConvers(&hspi3) & 0xffff;
//    Adc_cnt[2] = Adc_cnt[2]+1;
//    
//    if( Adc_cnt[2] <=  ADC_AVEG )
//    {
//       Adc_sum[2] = Cal_ADC_Sum( Adc_sum[2], Adc_Chan[2]) ;
//       Adcavg_en[2]=0;
//    }
//    if( Adc_cnt[2] == ADC_AVEG)
//    {
//       Adc_avg[2] = Cal_ADC_Aveg( Adc_sum[2]) ;
//       
//       Adcavg_en[2]=1;
//       Adc_cnt[2]=0;
//       Adc_sum[2]=0;
//    }
  }
  
}

int fputc(int c,FILE * f)
{
  UART_SendEnable;
  HAL_UART_Transmit(&huart1,(uint8_t *)&c,1,20);
 return c;
}