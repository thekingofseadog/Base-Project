#include "stm32f10x.h"                  // Device header
#include "OLED.h"
#include "Key.h"

/* 最近一次动作值(保持显示用, 初始/无动作为 Waiting=0) */
KeyAction_e LastAction1 = Waiting;
KeyAction_e LastAction2 = Waiting;

/* 动作值对应的名称(与枚举一一对应, 不足6字符补空格以便整行覆盖刷新) */
static const char *ActionName[4] = {"Wait  ", "One   ", "Two   ", "Long  "};

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
	OLED_ShowString(1, 0, "KeyAct1: ");                 // 占 0~8 列
	OLED_ShowChar(1, 9, (char)('0' + Act1));            // 值 0~3
	OLED_ShowString(1, 10, (char *)ActionName[Act1]);   // 名称

	/* 第2行: KeyAction2 的值 */
	OLED_ShowString(2, 0, "KeyAct2: ");
	OLED_ShowChar(2, 9, (char)('0' + Act2));
	OLED_ShowString(2, 10, (char *)ActionName[Act2]);

	/* 第3/4行: 图例 */
	OLED_ShowString(3, 0, "0:Wait 1:One");
	OLED_ShowString(4, 0, "2:Two  3:Long");
}

/**
  * @brief  TIM4 更新中断服务函数
  * @note   TIM4 由 Key_Init() 配置为每 1ms 产生一次更新中断,
  *         本函数每 1ms 调用一次 Key_Tick() 完成按键扫描(消抖/单击/双击/长按)
  * @retval 无
  */
void TIM4_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)
	{
		TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
		Key_Tick();
	}
}

int main(void)
{
	uint8_t NewEvent = 0;

	OLED_Init();				// OLED 初始化(PB8/PB9 软件I2C)
	Key_Init();					// 按键初始化(PB1=Key1, PB11=Key2, TIM4 1ms时基)
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
