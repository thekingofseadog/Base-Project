#ifndef __PWM_H
#define __PWM_H

/**
  * @brief  双通道 PWM 输出模块(方案1: TIM3)
  * @note   PWM1 = PA6 (TIM3_CH1), PWM2 = PA7 (TIM3_CH2)
  *         输出频率默认 1kHz(修改方法见 PWM.c 顶部宏), 占空比 0~100% 可随时更新,
  *         占空比精度 1% (默认 ARR=99, CCR 数值即占空比百分数)。
  *
  *         引脚占用情况(与工程其他模块无冲突):
  *           PA6/PA7   -> TIM3_CH1/CH2 输出(推挽复用), 本模块专用;
  *           不占用 PB1/PB11(按键)、PB8/PB9(OLED)、PA13/14(SWD) 等任何引脚。
  *
  *         典型用法:
  *         @code
  *         PWM_Init();                  // 初始化后两路均为 50%
  *         PWM1_SetDuty(30);            // PWM1 占空比 30%
  *         PWM2_SetDuty(100);           // PWM2 恒高(100%)
  *         @endcode
  *
  *         @note 按键联动(main.c): Key1 调 PWM1、Key2 调 PWM2,
  *               单击 +10%、双击 -10%、长按恢复 50%。
  */

#include "stm32f10x.h"

/* 函数声明 */
void PWM_Init(void);            /* TIM3 双通道 PWM 初始化(初始占空比 50%) */
void PWM1_SetDuty(uint8_t Percent); /* 设置 PWM1(PA6) 占空比, 0~100 */
void PWM2_SetDuty(uint8_t Percent); /* 设置 PWM2(PA7) 占空比, 0~100 */

#endif
