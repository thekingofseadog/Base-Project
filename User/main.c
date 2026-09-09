#include "stm32f10x.h"                  // Device header
#include "OLED.h"
#include "Key.h"

/* 最近一次动作值(保持显示用, 初始/无动作为 Waiting=0) */
KeyAction_e LastAction1 = Waiting;
KeyAction_e LastAction2 = Waiting;

/* 动作值对应的名称(与枚举一一对应, 不足5字符补空格以便整行覆盖刷新) */
static const char *ActionName[4] = {"Wait ", "One  ", "Two  ", "Long "};

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
  * @brief  TIM2 更新中断服务函数: 每 1ms 调用一次 Key_Tick()
  * @retval 无
  */
void TIM2_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
	{
		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
		Key_Tick();                             // 1ms 节拍, 驱动按键消抖与动作识别
	}
}

/**
  * @brief  刷新 OLED: 第1行显示 KeyAction1 的值, 第2行显示 KeyAction2 的值
  * @note   第3/4行为图例: 0=等待 1=单击 2=双击 3=长按
  *         值 1/2/3 在动作发生后一直保持显示, 直到下一次动作才更新,
  *         便于肉眼观察(原始状态在 Key_Clear() 后会立即回到 0)
  * @param  Act1 : Key1 最近一次动作值
  * @param  Act2 : Key2 最近一次动作值
  * @retval 无
  */
static void OLED_ShowActions(KeyAction_e Act1, KeyAction_e Act2)
{
	/* 第1行: KeyAction1 的值 */
	OLED_ShowString(1, 1, "KeyAct1: ");                 // 占 1~9 列
	OLED_ShowChar(1, 10, (char)('0' + Act1));           // 值 0~3
	OLED_ShowString(1, 11, (char *)ActionName[Act1]);   // 名称 11~15 列

	/* 第2行: KeyAction2 的值 */
	OLED_ShowString(2, 1, "KeyAct2: ");
	OLED_ShowChar(2, 10, (char)('0' + Act2));
	OLED_ShowString(2, 11, (char *)ActionName[Act2]);

	/* 第3/4行: 图例 */
	OLED_ShowString(3, 1, "0:Wait 1:One");
	OLED_ShowString(4, 1, "2:Two  3:Long");
}

int main(void)
{
	uint8_t NewEvent = 0;

	OLED_Init();				// OLED 初始化(PB8/PB9 软件I2C)
	Key_Init();					// 按键 GPIO 初始化(PB1=Key1, PB11=Key2, 下拉输入)
	Timer_Init();				// TIM2 1ms 时基(参考写法), 中断内每 1ms 调 Key_Tick()
	OLED_Clear();
	OLED_ShowActions(Waiting, Waiting);

	while (1)
	{
		/* 动作出现时记下 KeyAction1 / KeyAction2 的值(保持显示, 直到下次动作) */
		if (KeyAction1 != Waiting)
		{
			LastAction1 = KeyAction1;
			NewEvent = 1;
		}
		if (KeyAction2 != Waiting)
		{
			LastAction2 = KeyAction2;
			NewEvent = 1;
		}

		/* 动作已记录, 统一清除(未清除前产生的新动作会被丢弃) */
		Key_Clear();

		/* 仅在出现动作时刷新 OLED, 避免频繁占用 I2C 影响按键响应 */
		if (NewEvent)
		{
			OLED_ShowActions(LastAction1, LastAction2);
			NewEvent = 0;
		}
	}
}
