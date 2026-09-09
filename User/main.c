#include "stm32f10x.h"                  // Device header
#include "OLED.h"
#include "Key.h"
#include "PWM.h"
#include "ADC.h"

/* 按键单击/双击的占空比步进(%), 长按恢复 50% */
#define DUTY_STEP_PCT           10u

/* 两路 PWM 当前设定占空比(上电默认 50%, 按键修改, 立即写入 CCR) */
static uint8_t Duty1 = 50u;
static uint8_t Duty2 = 50u;

/* 系统时基计数(ms): TIM2 更新中断每 1ms 累加一次;
   供 Key_Tick 节拍与 ADCx 模块 1s 测频窗口使用(见 ADC.c) */
volatile uint32_t g_msTick = 0u;

/**
  * @brief  TIM2 定时中断初始化(1ms), 仿照工程内已验证成功的写法
  * @param  无
  * @retval 无
  */
static void Timer_Init(void)
{
	/* 开启时钟: 使能 TIM2 时钟 */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

	/* 配置时钟源: TIM2 选择内部时钟(不调用此函数时默认也为内部时钟) */
	TIM_InternalClockConfig(TIM2);

	/* 时基单元初始化 */
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInitStructure.TIM_Period = 100 - 1;         /* ARR=99 */
	TIM_TimeBaseInitStructure.TIM_Prescaler = 720 - 1;      /* PSC=719, 72MHz下计数100kHz */
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;    /* 高级定时器才用到 */
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);     /* 100kHz/100 => 更新周期 1ms */

	/* 清除时基初始化产生的更新事件标志(否则开中断后会立刻进一次中断) */
	TIM_ClearFlag(TIM2, TIM_FLAG_Update);

	/* 开启 TIM2 更新中断 */
	TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);

	/* NVIC 中断分组(整个工程仅调用一次): 分组2, 抢占0~3 / 响应0~3 */
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

	/* NVIC 配置 TIM2 中断通道 */
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_Init(&NVIC_InitStructure);

	/* 使能 TIM2, 定时器开始运行 */
	TIM_Cmd(TIM2, ENABLE);
}

/**
  * @brief  TIM2 更新中断服务函数: 每 1ms 驱动按键扫描并累加系统时基
  * @retval 无
  */
void TIM2_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
	{
		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
		Key_Tick();                 // 1ms 节拍, 驱动按键消抖与动作识别
		g_msTick++;                 // 1ms 时基计数, 供 ADCx 测频窗口计时
	}
}

/**
  * @brief  按键服务: Key1 调 PWM1 占空比, Key2 调 PWM2 占空比
  * @note   单击(One)= +10%, 双击(Two)= -10%, 长按(Long)= 恢复 50%;
  *         修改立即写入 CCR, OLED 上 D1/D2 为 ADC 实测占空比(回环),
  *         约 1s 内跟随新设定值
  * @retval 无
  */
static void Key_Service(void)
{
	uint8_t Chg1 = 0, Chg2 = 0;

	/* Key1 -> PWM1 */
	switch (KeyAction1)
	{
		case OneButton:                                 /* +10%, 100%时回绕到0% */
			Duty1 = (Duty1 > (100u - DUTY_STEP_PCT)) ? 0u :
			        (uint8_t)(Duty1 + DUTY_STEP_PCT);
			Chg1 = 1;
			break;
		case TwoButton:                                 /* -10%, 0%时回绕到100% */
			Duty1 = (Duty1 < DUTY_STEP_PCT) ? 100u :
			        (uint8_t)(Duty1 - DUTY_STEP_PCT);
			Chg1 = 1;
			break;
		case LongButton:                                /* 长按恢复 50% */
			Duty1 = 50u;
			Chg1 = 1;
			break;
		default:
			break;
	}

	/* Key2 -> PWM2 */
	switch (KeyAction2)
	{
		case OneButton:
			Duty2 = (Duty2 > (100u - DUTY_STEP_PCT)) ? 0u :
			        (uint8_t)(Duty2 + DUTY_STEP_PCT);
			Chg2 = 1;
			break;
		case TwoButton:
			Duty2 = (Duty2 < DUTY_STEP_PCT) ? 100u :
			        (uint8_t)(Duty2 - DUTY_STEP_PCT);
			Chg2 = 1;
			break;
		case LongButton:
			Duty2 = 50u;
			Chg2 = 1;
			break;
		default:
			break;
	}

	if (Chg1)
	{
		PWM1_SetDuty(Duty1);
	}
	if (Chg2)
	{
		PWM2_SetDuty(Duty2);
	}
	Key_Clear();
}

/**
  * @brief  刷新 OLED 某一行: "F?:xxxxx D?:xxx%"(频率5位/占空比3位, 实测值)
  * @note   第1行 = PWM1 通道(PA4), 第2行 = PWM2 通道(PA5);
  *         值只在 ADCx 每 1s 出新结果时更新, 平时不占用 I2C
  * @retval 无
  */
static void OLED_ShowChannel(uint8_t Line, uint8_t Sel, uint32_t FreqHz, uint8_t DutyPct)
{
	OLED_ShowString(Line, 1, (Sel == 0u) ? "F1:" : "F2:");          /* 1~3 列 */
	OLED_ShowNum(Line, 4, FreqHz, 5);                                /* 4~8 列: 频率(Hz) */
	OLED_ShowString(Line, 10, (Sel == 0u) ? "D1:" : "D2:");          /* 10~12 列 */
	OLED_ShowNum(Line, 13, DutyPct, 3);                              /* 13~15 列: 占空比 */
	OLED_ShowChar(Line, 16, '%');                                    /* 16 列 */
}

int main(void)
{
	uint32_t F1 = 0u, F2 = 0u;              /* ADC 实测频率(Hz) */
	uint8_t  D1 = 0u, D2 = 0u;              /* ADC 实测占空比(%) */
	uint8_t  FirstDraw = 1u;                /* 首帧强制刷新 */
	uint32_t PreF1 = 0u, PreF2 = 0u;        /* 上次显示值(变化才刷新) */
	uint8_t  PreD1 = 0u, PreD2 = 0u;

	OLED_Init();                // OLED 初始化(PB8/PB9 软件I2C)
	Key_Init();                 // 按键 GPIO 初始化(PB1=Key1, PB11=Key2, 下拉输入)
	PWM_Init();                 // PWM 输出初始化(PA6/PA7, TIM3_CH1/CH2, 1kHz, 默认50%)
	ADCx_Init();                // 双路 ADC 初始化(PA4/PA5, ADC1/ADC2)
	Timer_Init();               // TIM2 1ms 时基: 中断内调 Key_Tick 并累加 g_msTick

	OLED_Clear();
	/* 第1/2行: 频率+占空比(实测值, 每 1s 窗口更新); 第3/4行: 按键图例 */
	OLED_ShowChannel(1, 0u, F1, D1);
	OLED_ShowChannel(2, 1u, F2, D2);
	OLED_ShowString(3, 1, "1:+10% 2:-10%");
	OLED_ShowString(4, 1, "3(Long)=50%");

	while (1)
	{
		Key_Service();          /* 按键动作 -> 改两路 PWM 占空比 */
		ADCx_Poll();            /* 两路 ADC 各采一点, 满 1s 结算一次 */

		/* 1s 窗口出结果: 仅在数值变化时刷新 OLED(避免周期性打断采样影响读数) */
		if (ADCx_ResultReady())
		{
			F1 = ADCx_GetFreqHz(0);
			D1 = ADCx_GetDutyPct(0);
			F2 = ADCx_GetFreqHz(1);
			D2 = ADCx_GetDutyPct(1);
			ADCx_ResultClear();

			if (FirstDraw || F1 != PreF1 || D1 != PreD1 ||
			    F2 != PreF2 || D2 != PreD2)
			{
				OLED_ShowChannel(1, 0u, F1, D1);
				OLED_ShowChannel(2, 1u, F2, D2);
				PreF1 = F1; PreD1 = D1;
				PreF2 = F2; PreD2 = D2;
				FirstDraw = 0u;
			}
		}
	}
}
