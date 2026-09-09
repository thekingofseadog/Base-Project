#include "stm32f10x.h"                  // Device header
#include "OLED.h"
#include "Key.h"

/* 两个按键的各动作累计计数(单击/双击/长按) */
uint32_t Key1Single = 0, Key1Double = 0, Key1Long = 0;
uint32_t Key2Single = 0, Key2Double = 0, Key2Long = 0;

/**
  * @brief  刷新 OLED 计数显示
  * @note   布局: 第1行 Key1 单击/双击次数, 第2行 Key1 长按次数
  *              第3行 Key2 单击/双击次数, 第4行 Key2 长按次数
  * @retval 无
  */
static void OLED_ShowCounts(void)
{
	OLED_ShowString(1, 0, "K1 1:");
	OLED_ShowNum(1, 6, Key1Single, 3);
	OLED_ShowString(1, 9, "2:");
	OLED_ShowNum(1, 11, Key1Double, 3);
	OLED_ShowString(2, 0, "K1 L:");
	OLED_ShowNum(2, 5, Key1Long, 3);

	OLED_ShowString(3, 0, "K2 1:");
	OLED_ShowNum(3, 6, Key2Single, 3);
	OLED_ShowString(3, 9, "2:");
	OLED_ShowNum(3, 11, Key2Double, 3);
	OLED_ShowString(4, 0, "K2 L:");
	OLED_ShowNum(4, 5, Key2Long, 3);
}

int main(void)
{
	uint8_t NewEvent = 0;

	OLED_Init();				// OLED 初始化(PB8/PB9 软件I2C)
	Key_Init();					// 按键初始化(PB1=Key1, PB11=Key2, TIM4 1ms时基)
	OLED_Clear();
	OLED_ShowCounts();

	while (1)
	{
		/* 查询 Key1(PB1) 动作并计数 */
		switch (KeyAction1)
		{
			case OneButton:    Key1Single++;  NewEvent = 1; break;
			case TwoButton:    Key1Double++;  NewEvent = 1; break;
			case LongButton:   Key1Long++;    NewEvent = 1; break;
			default: break;
		}

		/* 查询 Key2(PB11) 动作并计数 */
		switch (KeyAction2)
		{
			case OneButton:    Key2Single++;  NewEvent = 1; break;
			case TwoButton:    Key2Double++;  NewEvent = 1; break;
			case LongButton:   Key2Long++;    NewEvent = 1; break;
			default: break;
		}

		/* 动作已处理, 统一清除(未清除前产生的新动作会被丢弃) */
		Key_Clear();

		/* 仅在出现动作时刷新 OLED, 避免频繁占用 I2C 影响按键响应 */
		if (NewEvent)
		{
			OLED_ShowCounts();
			NewEvent = 0;
		}
	}
}
