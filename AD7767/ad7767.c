#include "main.h"
#include "ad7767.h"
#include "spi.h"
#include <string.h>

/* ============================================================================
 *  AD7767 三通道驱动  (W1.2 ~ W1.6, "上午可演示"版本)
 *
 *  本版本取舍:
 *    - 读取用 HAL_SPI_TransmitReceive() 阻塞式: 只在主循环 / TCP 命令路径
 *      调用, 不进 ISR, 因此阻塞可接受。
 *    - 正式版 (W1.5) 将替换为 "寄存器直驱 + DWT 超时", 函数接口不变,
 *      上层调用代码无需改动。
 *    - AD7767_Sync() 内的 HAL_Delay() 属阻塞操作, 严禁在 ISR 中调用。
 * ========================================================================== */

static const AD7767_Device_t s_dev[AD7767_CH_COUNT] =
{
    [AD7767_CH1] = { &hspi1, GPIOB, SPI1_NSS_Pin, GPIOG, DRDY_AD7767_1_Pin, GPIOB, SYNC_PD_AD7767_1_Pin },
    [AD7767_CH2] = { &hspi2, GPIOD, SPI2_NSS_Pin, GPIOH, DRDY_AD7767_2_Pin, GPIOD, SYNC_PD_AD7767_2_Pin },
    [AD7767_CH3] = { &hspi3, GPIOD, SPI3_NSS_Pin, GPIOA, DRDY_AD7767_3_Pin, GPIOD, SYNC_PD_AD7767_3_Pin },
};

/* 编译期校验: 实例表项数必须与通道数一致 (漏写元素会被零初始化成 NULL, 静默出错) */
_Static_assert(sizeof(s_dev) / sizeof(s_dev[0]) == AD7767_CH_COUNT,
               "s_dev[] size must match AD7767_CH_COUNT");


/* -------------------------------------------------------------------------- */
/*  断电 / 同步                                                                */
/* -------------------------------------------------------------------------- */

void AD7767_PowerDownAll(void)
{
    uint8_t ch;

    for (ch = 0; ch < AD7767_CH_COUNT; ch++)
    {
        HAL_GPIO_WritePin(s_dev[ch].sync_port, s_dev[ch].sync_pin, GPIO_PIN_RESET);
    }
}

void AD7767_Sync(uint8_t ch)
{
    const AD7767_Device_t *dev;

    if (ch >= AD7767_CH_COUNT)
    {
        return;
    }

    dev = &s_dev[ch];

    /* 手册 Figure 5: 负脉冲复位 -> 拉高退出掉电 -> 等滤波器建立 */
    HAL_GPIO_WritePin(dev->sync_port, dev->sync_pin, GPIO_PIN_RESET);
    /* 低电平只需被一个 MCLK 上升沿采到(周期约 1us), 1ms 余量极大 */
    HAL_Delay(1);
    HAL_GPIO_WritePin(dev->sync_port, dev->sync_pin, GPIO_PIN_SET);
    /* tSETTLING 从 SYNC/PD 上升沿之后起算: 74/ODR = 2.31ms @32kSPS, 取 4ms 留余量 */
    HAL_Delay(4);
}

void AD7767_InitAll(void)
{
    uint8_t ch;

    AD7767_PowerDownAll();                      /* 先按住三片, 上电静默 */
    for (ch = 0; ch < AD7767_CH_COUNT; ch++)
    {
        AD7767_Sync(ch);                        /* 逐片复位 + 等待建立 */
    }
}


/* -------------------------------------------------------------------------- */
/*  读取                                                                       */
/* -------------------------------------------------------------------------- */

uint8_t AD7767_DrdyIsLow(uint8_t ch)
{
    const AD7767_Device_t *dev;

    if (ch >= AD7767_CH_COUNT)
    {
        return 0U;
    }

    dev = &s_dev[ch];

    /* 1 = DRDY 为低 = 数据已就绪; 0 = 未就绪 -> 上层跳过本次 */
    return (HAL_GPIO_ReadPin(dev->drdy_port, dev->drdy_pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

AD7767_Status_t AD7767_ReadRaw24(uint8_t ch, uint32_t *raw)
{
    const AD7767_Device_t *dev;
    HAL_StatusTypeDef st;
    uint8_t tx[3] = { 0, 0, 0 };
    uint8_t rx[3] = { 0, 0, 0 };

    if (raw == NULL || ch >= AD7767_CH_COUNT)
    {
        return AD7767_ERR_PARAM;
    }

    dev = &s_dev[ch];

    /* "宁少不错" (D-05): DRDY 未就绪直接跳过, 不自旋等待。
     * 需要"等到下一个数据点"的调用者, 自行在外部轮询 AD7767_DrdyIsLow()。 */
    if (AD7767_DrdyIsLow(ch) == 0U)
    {
        return AD7767_ERR_NOT_READY;
    }

    /* CS 拉低(MSB 即刻出现在 SDO) -> 时钟 24bit, MSB first -> CS 拉高 */
    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_RESET);

    /* AD7767 的 SDI 仅用于菊花链, 单读模式下发送内容无关, 填 0 即可 */
    st = HAL_SPI_TransmitReceive(dev->spi, tx, rx, 3, 10);

    HAL_GPIO_WritePin(dev->cs_port, dev->cs_pin, GPIO_PIN_SET);

    if (st != HAL_OK)
    {
        return AD7767_ERR_TIMEOUT;
    }

    *raw = ((uint32_t)rx[0] << 16) | ((uint32_t)rx[1] << 8) | (uint32_t)rx[2];

    return AD7767_OK;
}


/* -------------------------------------------------------------------------- */
/*  换算                                                                       */
/* -------------------------------------------------------------------------- */

int32_t AD7767_Sign24(uint32_t raw)
{
    /* 24bit 补码 -> int32_t: 先把符号位(bit23)左移到 bit31, 再算术右移回来,
     * 即完成符号扩展。直接 (int32_t)raw 会把 bit23 当数据位, 负值全变正。 */
    return (int32_t)(raw << 8) >> 8;
}

int32_t AD7767_ToMicroVolt(uint32_t raw)
{
    /* Vdiff = code * VREF / 2^23, 以 uV 为单位 => VREF 取 5000000 uV
     * 中间量必须用 int64: 8388608 * 5000000 = 4.19e13, 远超 int32 范围。
     * 且不能用 >>23 替代除法 —— 右移对负数向 -inf 取整, 小信号偏差明显。
     *
     * NOTE: 满量程按 +-VREF 计(手册 Table 2 Differential Input Voltage),
     *       单板前端 ADA4941-1 的增益尚待按原理图核实 (任务清单 W1.6)。 */
    return (int32_t)(((int64_t)AD7767_Sign24(raw) * 5000000LL) / 8388608LL);
}
