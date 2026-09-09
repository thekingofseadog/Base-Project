#include "stm32f10x.h"                  // Device header
#include "Key.h"

/* ============================== 引脚与时基配置 ============================== */
/* Key1 = PB1, Key2 = PB11: 上拉输入, 按下为低电平, 松开为高电平 */
#define KEY1_PORT               GPIOB
#define KEY1_PIN                GPIO_Pin_1
#define KEY2_PORT               GPIOB
#define KEY2_PIN                GPIO_Pin_11

/* 按键时基: TIM4 更新中断, 每 1ms 调用一次 Key_Tick() */
#define KEY_TIM                 TIM4
#define KEY_TIM_RCC             RCC_APB1Periph_TIM4
#define KEY_TIM_IRQn            TIM4_IRQn
#define KEY_TIM_IRQHandler      TIM4_IRQHandler

/* ============================== 时序参数(单位 ms) ============================== */
#define KEY_DEBOUNCE_MS         10      /* 消抖时间: 电平需连续稳定 10ms 才确认翻转 */
#define KEY_DOUBLE_WIN_MS       300     /* 双击判定窗口: 短按松开后 300ms 内再按下为双击 */
#define KEY_LONG_MS             1000    /* 长按阈值: 按下持续 1000ms 上报一次长按 */

/* ============================== 内部电平/阶段定义 ============================== */
#define KEY_RELEASED            0x01    /* 去抖后电平: 松开(高) */
#define KEY_PRESSED             0x00    /* 去抖后电平: 按下(低) */

enum
{
	P_IDLE = 0,     /* 空闲, 等待按下 */
	P_PRESS,        /* 按下中(计时判定长按) */
	P_WAIT2,        /* 短按松开, 等待窗口内的第二次按下(超时则判为单击) */
	P_2NDPRESS      /* 双击已确认, 等待第二次按下松开 */
};

/* 单个按键的内部状态 */
typedef struct
{
	GPIO_TypeDef *Port;     /* 按键端口 */
	uint16_t      Pin;      /* 按键引脚 */
	uint8_t       Level;    /* 当前去抖后电平: KEY_RELEASED / KEY_PRESSED */
	uint8_t       StableCnt;/* 电平稳定计数(消抖用, 单位节拍) */
	uint8_t       Phase;    /* 内部阶段: P_IDLE / P_PRESS / P_WAIT2 / P_2NDPRESS */
	uint8_t       LongDone; /* 本次按下是否已上报过长按 */
	uint16_t      PressMs;  /* 本次按下已持续的时间(ms) */
	uint16_t      WaitMs;   /* 双击等待窗口倒计时(ms) */
} Key_Dev_t;

/* 两个按键的外部动作状态(见 Key.h) */
KeyAction_e KeyAction1 = Waiting;
KeyAction_e KeyAction2 = Waiting;

/* 两个按键的内部状态(Key1 = PB1, Key2 = PB11) */
static Key_Dev_t KeyDev[2] = {0};

/* 上报动作: 上一次动作尚未被清除时不覆盖(丢弃), 保证不漏报 */
static void Key_Report(KeyAction_e *Action, KeyAction_e Value)
{
	if (*Action == Waiting)
	{
		*Action = Value;
	}
}

/**
  * @brief  单个按键的 1ms 节拍扫描: 消抖 + 单击/双击/长按状态机
  * @param  Dev    : 按键内部状态指针
  * @param  Action : 对应按键的外部动作状态指针
  * @retval 无
  */
static void Key_Scan(Key_Dev_t *Dev, KeyAction_e *Action)
{
	uint8_t Raw;    /* 原始电平 */
	uint8_t Pre;    /* 消抖前的电平(用于沿检测) */

	Raw = (GPIO_ReadInputDataBit(Dev->Port, Dev->Pin) == Bit_RESET) ?
	       KEY_PRESSED : KEY_RELEASED;
	Pre = Dev->Level;

	/* ---- 消抖: 电平需连续稳定 KEY_DEBOUNCE_MS 个节拍才翻转 ---- */
	if (Raw != Dev->Level)
	{
		Dev->StableCnt++;
		if (Dev->StableCnt >= KEY_DEBOUNCE_MS)
		{
			Dev->Level = Raw;           /* 电平稳定翻转(产生沿) */
			Dev->StableCnt = 0;
		}
	}
	else
	{
		Dev->StableCnt = 0;
	}

	/* ---- 沿事件处理(本拍去抖电平发生变化才执行) ---- */
	if (Dev->Level != Pre)
	{
		if (Dev->Level == KEY_PRESSED)  /* 按下沿 */
		{
			switch (Dev->Phase)
			{
				case P_IDLE:            /* 第一次按下: 开始计时, 用于长按判定 */
					Dev->Phase = P_PRESS;
					Dev->PressMs = 0;
					Dev->LongDone = 0;
					break;

				case P_WAIT2:           /* 双击窗口内第二次按下: 上报双击 */
					Key_Report(Action, TwoButton);
					Dev->Phase = P_2NDPRESS;
					break;

				default:                /* P_PRESS / P_2NDPRESS 期间不会出现新的按下沿 */
					break;
			}
		}
		else                            /* 松开沿 */
		{
			switch (Dev->Phase)
			{
				case P_PRESS:
					if (Dev->LongDone)  /* 长按后的松开: 不产生单击/双击 */
					{
						Dev->Phase = P_IDLE;
					}
					else                /* 短按松开: 进入双击等待窗口 */
					{
						Dev->WaitMs = KEY_DOUBLE_WIN_MS;
						Dev->Phase = P_WAIT2;
					}
					break;

				case P_2NDPRESS:        /* 双击的第二次松开 */
					Dev->Phase = P_IDLE;
					break;

				default:
					break;
			}
		}
	}

	/* ---- 每节拍的计时处理 ---- */
	switch (Dev->Phase)
	{
		case P_PRESS:
			Dev->PressMs++;
			if ((Dev->PressMs >= KEY_LONG_MS) && (Dev->LongDone == 0))
			{
				Key_Report(Action, LongButton);     /* 达到长按阈值, 只上报一次 */
				Dev->LongDone = 1;
			}
			break;

		case P_WAIT2:
			if (Dev->WaitMs > 0)
			{
				Dev->WaitMs--;
			}
			if (Dev->WaitMs == 0)       /* 窗口内没有第二次按下: 上报单击 */
			{
				Key_Report(Action, OneButton);
				Dev->Phase = P_IDLE;
			}
			break;

		default:
			break;
	}
}

/**
  * @brief  1ms 节拍扫描(两个按键)
  * @note   已由 TIM4 更新中断自动每 1ms 调用一次, 一般无需手动调用;
  *         若自行更换时基, 请保证按 1ms 周期调用本函数
  * @retval 无
  */
void Key_Tick(void)
{
	Key_Scan(&KeyDev[0], &KeyAction1);
	Key_Scan(&KeyDev[1], &KeyAction2);
}

/**
  * @brief  按键 GPIO 与 TIM4 1ms 时基初始化(按键为非阻塞方式扫描)
  * @note   Key1 = PB1, Key2 = PB11, 上拉输入;
  *         TIM4 每 1ms 产生更新中断, 中断内自动调用 Key_Tick()
  * @retval 无
  */
void Key_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	uint8_t i;

	/* 1. 使能 GPIOB 与 TIM4 时钟 */
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
	RCC_APB1PeriphClockCmd(KEY_TIM_RCC, ENABLE);

	/* 2. 按键引脚配置: PB1、PB11 上拉输入 */
	GPIO_InitStructure.GPIO_Pin = KEY1_PIN | KEY2_PIN;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);

	/* 3. 按键内部状态复位(可重复调用 Key_Init) */
	KeyDev[0].Port = KEY1_PORT;
	KeyDev[0].Pin  = KEY1_PIN;
	KeyDev[1].Port = KEY2_PORT;
	KeyDev[1].Pin  = KEY2_PIN;
	for (i = 0; i < 2; i++)
	{
		KeyDev[i].Level = KEY_RELEASED;
		KeyDev[i].StableCnt = 0;
		KeyDev[i].Phase = P_IDLE;
		KeyDev[i].LongDone = 0;
		KeyDev[i].PressMs = 0;
		KeyDev[i].WaitMs = 0;
	}
	Key_Clear();

	/* 4. TIM4 配置: 1ms 更新中断
	       注意: APB1 分频不为 1 时定时器时钟 = 2 x APB1 = SystemCoreClock(默认72MHz),
	       预分频到 1MHz 计数, 自动重装 1000 即为 1ms; 若系统时钟不是 72MHz,
	       SystemCoreClock 会自动换算预分频系数 */
	TIM_TimeBaseStructure.TIM_Period = 1000 - 1;        /* 计数 1000 次 */
	TIM_TimeBaseStructure.TIM_Prescaler =
		(uint16_t)(SystemCoreClock / 1000000U - 1U);    /* 计数频率 1MHz */
	TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInit(KEY_TIM, &TIM_TimeBaseStructure);

	/* 5. 使能 TIM4 更新中断并启动 */
	TIM_ITConfig(KEY_TIM, TIM_IT_Update, ENABLE);

	NVIC_InitStructure.NVIC_IRQChannel = KEY_TIM_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	TIM_Cmd(KEY_TIM, ENABLE);
}

/**
  * @brief  清除按键动作状态(KeyAction1 / KeyAction2 恢复为 Waiting)
  * @note   建议在主循环取出动作并处理完毕后再调用
  * @retval 无
  */
void Key_Clear(void)
{
	KeyAction1 = Waiting;
	KeyAction2 = Waiting;
}

/**
  * @brief  TIM4 更新中断服务函数: 每 1ms 调用一次 Key_Tick()
  * @retval 无
  */
void KEY_TIM_IRQHandler(void)
{
	if (TIM_GetITStatus(KEY_TIM, TIM_IT_Update) != RESET)
	{
		TIM_ClearITPendingBit(KEY_TIM, TIM_IT_Update);
		Key_Tick();
	}
}
