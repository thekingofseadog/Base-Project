#include "stm32f10x.h"                  // Device header
#include "OLED.h"
#include "Key.h"

/* ---- 临时诊断用: 引用 Key.c 里的内部状态读取接口(定位后删除) ---- */
extern uint8_t  Key_DbgLevel(uint8_t Idx);
extern uint8_t  Key_DbgPhase(uint8_t Idx);
extern uint16_t Key_DbgWaitMs(uint8_t Idx);
extern uint16_t Key_DbgPressMs(uint8_t Idx);

/* TIM2 中断累计毫秒(诊断用, 顺便验证 1ms 时基是否真实) */
volatile uint32_t g_TickMs = 0;

/* 动作值对应的名称 */
static const char *ActionName[4] = {"Wait ", "One  ", "Two  ", "Long "};

/**
  * @brief  TIM2 更新中断服务函数: 每 1ms 调用一次 Key_Tick()
  * @retval 无
  */
void TIM2_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM2, TIM_IT_Update) != RESET)
	{
		TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
		g_TickMs++;
		Key_Tick();
	}
}

/**
  * @brief  诊断显示刷新(约每100ms一次, 临时版)
  * @retval 无
  */
static void OLED_DbgShow(void)
{
	char Buf[17];

	/* 第1行: KeyAction1 的值; 第2行: KeyAction2 的值 */
	OLED_ShowString(1, 0, "A1:");
	OLED_ShowChar(1, 3, (char)('0' + KeyAction1));
	OLED_ShowString(1, 5, (char *)ActionName[KeyAction1]);
	OLED_ShowString(2, 0, "A2:");
	OLED_ShowChar(2, 3, (char)('0' + KeyAction2));
	OLED_ShowString(2, 5, (char *)ActionName[KeyAction2]);

	/* 第3行: Key1 内部状态: 电平/阶段/窗口倒计时/按住计时 */
	Buf[0] = '0' + Key_DbgLevel(0);
	Buf[1] = ' ';
	Buf[2] = 'P';
	Buf[3] = 'h';
	Buf[4] = ':';
	Buf[5] = '0' + Key_DbgPhase(0);
	Buf[6] = ' ';
	Buf[7] = 'W';
	Buf[8] = ':';
	Buf[9] = '0' + (uint8_t)(Key_DbgWaitMs(0) / 100);
	Buf[10] = '0' + (uint8_t)((Key_DbgWaitMs(0) / 10) % 10);
	Buf[11] = '0' + (uint8_t)(Key_DbgWaitMs(0) % 10);
	Buf[12] = ' ';
	Buf[13] = 'C';
	Buf[14] = ':';
	Buf[15] = (uint8_t)((Key_DbgPressMs(0) / 100) % 10) + '0';
	Buf[16] = '\0';
	OLED_ShowString(3, 0, Buf);

	/* 第4行: 毫秒计数器(验证时基快慢) */
	Buf[0] = 'T';
	Buf[1] = ':';
	Buf[2] = '0' + (uint8_t)((g_TickMs / 1000) % 10);
	Buf[3] = '0' + (uint8_t)((g_TickMs / 100) % 10);
	Buf[4] = '0' + (uint8_t)((g_TickMs / 10) % 10);
	Buf[5] = '0' + (uint8_t)(g_TickMs % 10);
	Buf[6] = '\0';
	OLED_ShowString(4, 0, Buf);
}

int main(void)
{
	uint32_t LastMs = 0;

	OLED_Init();				// OLED 初始化(PB8/PB9 软件I2C)
	Key_Init();					// 按键初始化(PB1=Key1, PB11=Key2, TIM2 1ms时基)
	OLED_Clear();

	while (1)
	{
		/* 诊断说明: 故意不做 Key_Clear, 首次动作值会停在屏幕; 每轮实验前按一下复位键 */

		/* 诊断显示: 每 100ms 刷新一次 */
		if (g_TickMs - LastMs >= 100)
		{
			LastMs = g_TickMs;
			OLED_DbgShow();
		}
	}
}
