#ifndef __KEY_H
#define __KEY_H

/**
  * @brief  非阻塞按键模块(单击/双击/长按)
  * @note   Key1 = PB1, Key2 = PB11, 均为下拉输入, 按下为高电平
  *         TIM4 由 Key_Init() 配置为每 1ms 产生更新中断, 中断服务函数
  *         TIM4_IRQHandler 需在 main.c 中实现并每 1ms 调用一次 Key_Tick(),
  *         由此驱动消抖与按键动作识别, 全程无阻塞等待;
  *         使用时只需周期查询 KeyAction1 / KeyAction2。
  *
  *         动作识别规则(时间参数见 Key.c 顶部宏, 可按需修改):
  *           1. 单击: 短按松开后, 在双击窗口(200ms)内没有第二次按下才上报
  *           2. 双击: 短按松开后, 双击窗口内第二次按下, 在按下瞬间上报
  *           3. 长按: 按下持续达到长按阈值(600ms)上报一次, 松开不产生单击/双击
  *
  *         典型使用示例:
  *         @code
  *         #include "Key.h"
  *
  *         int main(void)
  *         {
  *             Key_Init();                        初始化 GPIO 与 TIM4 时基
  *             while (1)
  *             {
  *                 if (KeyAction1 == OneButton)        Key1 单击处理
  *                 else if (KeyAction1 == TwoButton)   Key1 双击处理
  *                 else if (KeyAction1 == LongButton)  Key1 长按处理
  *                 if (KeyAction2 == OneButton)        Key2 单击处理
  *                 else if (KeyAction2 == TwoButton)   Key2 双击处理
  *                 else if (KeyAction2 == LongButton)  Key2 长按处理
  *                 Key_Clear();                    处理完毕统一清除
  *             }
  *         }
  *
  *         TIM4 更新中断服务函数(写在 main.c 中, 每 1ms 调 Key_Tick):
  *         void TIM4_IRQHandler(void)
  *         {
  *             if (TIM_GetITStatus(TIM4, TIM_IT_Update) != RESET)
  *             {
  *                 TIM_ClearITPendingBit(TIM4, TIM_IT_Update);
  *                 Key_Tick();
  *             }
  *         }
  *         @endcode
  *
  *         @warning 上报动作后请尽快处理并调用 Key_Clear(); 动作尚未被清除期间
  *                  产生的新动作将被丢弃(不覆盖), 以保证不会漏掉任何一次上报。
  */

/* 按键动作状态枚举 */
typedef enum
{
	Waiting      = 0,   /* 等待: 空闲无动作(初始/清除后的状态) */
	OneButton    = 1,   /* 单击 */
	TwoButton    = 2,   /* 双击 */
	LongButton   = 3    /* 长按 */
} KeyAction_e;

extern KeyAction_e KeyAction1;   /* Key1(PB1)  的动作状态 */
extern KeyAction_e KeyAction2;   /* Key2(PB11) 的动作状态 */

/* 函数声明 */
void Key_Init(void);    /* 按键 GPIO 初始化 + TIM4 1ms 时基启动(TIM4_IRQHandler 见 main.c) */
void Key_Clear(void);   /* 将 KeyAction1 / KeyAction2 清除为 Waiting */
void Key_Tick(void);    /* 1ms 节拍扫描函数, 由 main.c 中 TIM4_IRQHandler 每 1ms 调用 */

#endif
