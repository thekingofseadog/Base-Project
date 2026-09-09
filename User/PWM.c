#include "stm32f10x.h"                  // Device header
#include "PWM.h"

/* ============================== 输出参数 ============================== */
/* TIM3 输入时钟: APB1=36MHz, 定时器时钟自动 x2 = 72MHz(系统时钟 72MHz 前提) */
#define PWM_TIM_CLK         72000000UL

/* 自动重装值 ARR=99 => 每周期 100 个计数点, CCR 数值 = 占空比百分比(0~100)
   (CCR=0 恒低, CCR=100 > ARR 恒高), 占空比精度 1% */
#define PWM_ARR             99u

/* 预分频: 使计数频率 = (ARR+1) x 输出频率
   当前 72MHz / (100 x 1000) - 1 = 719 => 计数 100kHz, 输出 1kHz
   修改输出频率只需改本宏: PSC = 72MHz / ((ARR+1) x 目标频率Hz) - 1
   例: 10kHz 时 PSC = 72 / (100 x 10) - 1 = 71(以千为单位: 72000/1000-1=71) */
#define PWM_PSC             (PWM_TIM_CLK / ((uint32_t)(PWM_ARR + 1u) * 1000u) - 1u)

/**
  * @brief  TIM3 双通道 PWM 输出初始化(PWM1=PA6, PWM2=PA7)
  * @note   初始占空比 50%: 上电即可在 PA6/PA7 测得 1kHz 方波
  * @retval 无
  */
void PWM_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	TIM_OCInitTypeDef TIM_OCInitStructure;

	/* 1. 开启时钟: GPIOA(APB2) + TIM3(APB1) */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);

	/* 2. PA6/PA7 配置为复用推挽输出(50MHz) */
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	/* 3. 时基单元: 计数 100kHz, 周期 100 点 => 更新频率 1kHz */
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInitStructure.TIM_Period = PWM_ARR;
	TIM_TimeBaseInitStructure.TIM_Prescaler = PWM_PSC;
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;    /* 高级定时器才用到 */
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);

	/* 4. CH1/CH2 输出比较: PWM1 模式, 默认脉冲 50 */
	TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
	TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
	TIM_OCInitStructure.TIM_Pulse = 50u;                    /* CCR=50 => 50% */
	TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
	TIM_OC1Init(TIM3, &TIM_OCInitStructure);
	TIM_OC2Init(TIM3, &TIM_OCInitStructure);

	/* 5. 预装载使能(更新事件时才生效, 避免输出跳变), 启动 TIM3 */
	TIM_OC1PreloadConfig(TIM3, TIM_OCPreload_Enable);
	TIM_OC2PreloadConfig(TIM3, TIM_OCPreload_Enable);
	TIM_ARRPreloadConfig(TIM3, ENABLE);
	TIM_Cmd(TIM3, ENABLE);
}

/**
  * @brief  设置 PWM1(PA6/TIM3_CH1) 占空比
  * @param  Percent : 0~100(0=恒低, 100=恒高)
  * @retval 无
  */
void PWM1_SetDuty(uint8_t Percent)
{
	if (Percent > 100u)
	{
		Percent = 100u;
	}
	TIM_SetCompare1(TIM3, Percent);     /* ARR=99 时 CCR 数值即百分比 */
}

/**
  * @brief  设置 PWM2(PA7/TIM3_CH2) 占空比
  * @param  Percent : 0~100(0=恒低, 100=恒高)
  * @retval 无
  */
void PWM2_SetDuty(uint8_t Percent)
{
	if (Percent > 100u)
	{
		Percent = 100u;
	}
	TIM_SetCompare2(TIM3, Percent);
}
