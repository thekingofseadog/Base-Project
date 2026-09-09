#include "stm32f10x.h"                  // Device header
#include "ADC.h"

/* ============================== 引脚与时基配置 ============================== */
/* 两路被测通道: Sel0 = PA4(ADC1_IN4), Sel1 = PA5(ADC2_IN5), 两片 ADC 并行独立采样 */
#define ADC_CH_SEL0              ADC_Channel_4
#define ADC_CH_SEL1              ADC_Channel_5
#define ADC_SAMPLE_TIME          ADC_SampleTime_1Cycles5   /* 最快采样: 14周期/次 */

/* 测频窗口(ms): 1s => 频率分辨率 1Hz; 若改小, 分辨率同步变为 1000/窗口ms */
#define ADC_WIN_MS               1000u

/* 迟滞阈值(12位): 高于 ADC_LEVEL_HI 判高, 低于 ADC_LEVEL_LO 判低,
   中间区间保持原电平, 消除临界电压附近的抖动误判(约 1.86V / 1.44V @3.3V) */
#define ADC_LEVEL_HI             2304u
#define ADC_LEVEL_LO             1792u

/* ============================== 内部数据结构 ============================== */
typedef struct
{
	ADC_TypeDef *Adc;       /* ADC1 / ADC2 */
	uint8_t       Level;    /* 上一采样点判定电平: 0=低 1=高 */
	uint32_t      SampCnt;  /* 窗口内总采样点数 */
	uint32_t      HighCnt;  /* 窗口内高电平采样点数(占空比用) */
	uint32_t      EdgeCnt;  /* 窗口内上升沿个数(频率用) */
} ADC_Meter_t;

/* 两路被测通道状态 */
static ADC_Meter_t Meter[2];

/* 测量结果缓存与标志 */
static uint32_t FreqHz[2];
static uint8_t  DutyPct[2];
static uint32_t WinStartMs = 0;     /* 当前窗口起始时刻(ms, 以 g_msTick 计) */
static uint8_t  Ready = 0;          /* 1s 窗口完成标志 */

/* main.c 中定义: TIM2 中断每 1ms 累加一次的系统时基 */
extern volatile uint32_t g_msTick;

/**
  * @brief  单路 ADC 采集一点: 启动转换 -> 等待 EOC -> 读值并判电平、累计统计
  * @param  M : 通道状态指针
  * @retval 无
  */
static void ADC_MeterTick(ADC_Meter_t *M)
{
	uint16_t V;
	uint8_t  Lv;

	/* 软件触发单次转换(读数据寄存器后 EOC 自动清除) */
	ADC_SoftwareStartConvCmd(M->Adc, ENABLE);
	while (ADC_GetFlagStatus(M->Adc, ADC_FLAG_EOC) == RESET);
	V = ADC_GetConversionValue(M->Adc);

	/* 带迟滞的电平判定 */
	if (V >= ADC_LEVEL_HI)
	{
		Lv = 1;
	}
	else if (V <= ADC_LEVEL_LO)
	{
		Lv = 0;
	}
	else
	{
		Lv = M->Level;      /* 迟滞区间: 保持原电平 */
	}

	M->SampCnt++;
	if (Lv)
	{
		M->HighCnt++;
	}
	if ((Lv == 1) && (M->Level == 0))
	{
		M->EdgeCnt++;       /* 上升沿 */
	}
	M->Level = Lv;
}

/**
  * @brief  ADC1(PA4)/ADC2(PA5) 初始化
  * @note   两片 ADC 各固定一个通道, 软件触发单次转换; 无中断、无 DMA
  * @retval 无
  */
void ADCx_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	ADC_InitTypeDef ADC_InitStructure;

	/* 1. 开启时钟: ADC1/ADC2 + GPIOA(APB2), ADC 预分频 = PCLK2/6 = 12MHz */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_ADC2 |
	                       RCC_APB2Periph_GPIOA, ENABLE);
	RCC_ADCCLKConfig(RCC_PCLK2_Div6);

	/* 2. PA4/PA5 模拟输入 */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4 | GPIO_Pin_5;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	/* 3. ADC 工作参数: 独立模式, 单次转换, 右对齐, 规则组 1 个通道 */
	ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
	ADC_InitStructure.ADC_ScanConvMode = DISABLE;
	ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
	ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
	ADC_InitStructure.ADC_NbrOfChannel = 1;
	ADC_Init(ADC1, &ADC_InitStructure);
	ADC_Init(ADC2, &ADC_InitStructure);

	/* 4. 固定规则通道: ADC1=IN4(PA4), ADC2=IN5(PA5), 采样时间 1.5 周期 */
	ADC_RegularChannelConfig(ADC1, ADC_CH_SEL0, 1, ADC_SAMPLE_TIME);
	ADC_RegularChannelConfig(ADC2, ADC_CH_SEL1, 1, ADC_SAMPLE_TIME);

	/* 5. 使能 ADC 并校准(与工程 AD.c 已验证写法一致) */
	ADC_Cmd(ADC1, ENABLE);
	ADC_Cmd(ADC2, ENABLE);

	ADC_ResetCalibration(ADC1);
	while (ADC_GetResetCalibrationStatus(ADC1) == SET);
	ADC_StartCalibration(ADC1);
	while (ADC_GetCalibrationStatus(ADC1) == SET);

	ADC_ResetCalibration(ADC2);
	while (ADC_GetResetCalibrationStatus(ADC2) == SET);
	ADC_StartCalibration(ADC2);
	while (ADC_GetCalibrationStatus(ADC2) == SET);

	/* 6. 内部状态复位 */
	Meter[0].Adc = ADC1;
	Meter[1].Adc = ADC2;
	Meter[0].Level = 0;
	Meter[1].Level = 0;
	Meter[0].SampCnt = 0;
	Meter[1].SampCnt = 0;
	Meter[0].HighCnt = 0;
	Meter[1].HighCnt = 0;
	Meter[0].EdgeCnt = 0;
	Meter[1].EdgeCnt = 0;
	WinStartMs = g_msTick;
	Ready = 0;
}

/**
  * @brief  主循环每轮调用: 两路各采样一点, 窗口满 1s 后结算频率/占空比
  * @retval 无
  */
void ADCx_Poll(void)
{
	uint32_t Elapsed;
	uint8_t  i;

	/* 两路各采一点(顺序采样, 每路采样率 = 主循环频率) */
	ADC_MeterTick(&Meter[0]);
	ADC_MeterTick(&Meter[1]);

	/* 窗口未到 1s 直接返回 */
	Elapsed = g_msTick - WinStartMs;
	if (Elapsed < ADC_WIN_MS)
	{
		return;
	}

	/* 结算: 频率 = 上升沿数 / 实际窗口时长; 占空比 = 高电平采样点占比 */
	for (i = 0; i < 2; i++)
	{
		FreqHz[i] = Meter[i].EdgeCnt * 1000u / Elapsed;
		if (FreqHz[i] > 99999u)             /* OLED 5 位显示上限 */
		{
			FreqHz[i] = 99999u;
		}
		DutyPct[i] = (uint8_t)((Meter[i].HighCnt * 100u +
		                        Meter[i].SampCnt / 2u) / Meter[i].SampCnt);

		/* 开下一窗口 */
		Meter[i].SampCnt = 0;
		Meter[i].HighCnt = 0;
		Meter[i].EdgeCnt = 0;
	}
	WinStartMs = g_msTick;
	Ready = 1;
}

/**
  * @brief  查询是否有新测量结果(每 1s 一次)
  * @retval 1=有新结果(用 ADCx_GetFreqHz/DutyPct 读取后调 ResultClear)
  */
uint8_t ADCx_ResultReady(void)
{
	return Ready;
}

/**
  * @brief  清除新结果标志
  * @retval 无
  */
void ADCx_ResultClear(void)
{
	Ready = 0;
}

/**
  * @brief  取某通道频率结果
  * @param  Sel : 0=PA4(测 PWM1 回环), 1=PA5(测 PWM2 回环)
  * @retval 频率(Hz), 窗口结算后才更新, 未接线/无信号时为环境噪声读数
  */
uint32_t ADCx_GetFreqHz(uint8_t Sel)
{
	if (Sel > 1u)
	{
		return 0u;
	}
	return FreqHz[Sel];
}

/**
  * @brief  取某通道占空比结果
  * @param  Sel : 0=PA4(测 PWM1 回环), 1=PA5(测 PWM2 回环)
  * @retval 占空比(0~100%), 窗口结算后才更新
  */
uint8_t ADCx_GetDutyPct(uint8_t Sel)
{
	if (Sel > 1u)
	{
		return 0u;
	}
	return DutyPct[Sel];
}
