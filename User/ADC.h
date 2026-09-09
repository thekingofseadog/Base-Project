#ifndef __ADC_H
#define __ADC_H

/**
  * @brief  双路 ADC 测频模块(方案1: 过零计数法检测方波 PWM 频率/占空比)
  * @note   Sel0 = PA4(ADC1_IN4): 测 PWM1 回环输入(自测时接 PA6)
  *         Sel1 = PA5(ADC2_IN5): 测 PWM2 回环输入(自测时接 PA7)
  *
  *         原理: 对输入引脚高速连续 ADC 采样(约每秒数十万点), 电压超过阈值判
  *         高电平、低于阈值判低电平(带迟滞防抖), 在 1s 窗口内统计:
  *           - 上升沿个数  => 频率(分辨率 1Hz)
  *           - 高电平采样点占比 => 占空比(分辨率 1%)
  *         适用信号: 0~3.3V 干净方波, 频率建议 10Hz~10kHz(每周期需足够采样点)。
  *
  *         依赖: main.c 中定义的 volatile uint32_t g_msTick(TIM2 每 1ms 累加)
  *         提供 1s 测频窗口时基; ADCx_Poll() 需在主循环每轮持续调用。
  *
  *         引脚/占用: PA4/PA5 为纯 ADC 输入脚, 与按键(PB1/PB11)、OLED(PB8/PB9)、
  *         PWM 输出(PA6/PA7)、SWD(PA13/14) 均无冲突。
  *
  *         典型用法(见 main.c):
  *         @code
  *         ADCx_Init();
  *         while (1)
  *         {
  *             ADCx_Poll();                        // 主循环每轮采样+窗口计时
  *             if (ADCx_ResultReady())             // 每 1s 出一组结果
  *             {
  *                 f = ADCx_GetFreqHz(0);          // 通道0(PA4)频率
  *                 d = ADCx_GetDutyPct(0);         // 通道0(PA4)占空比
  *                 ADCx_ResultClear();
  *             }
  *         }
  *         @endcode
  */

#include "stm32f10x.h"

/* 函数声明 */
void ADCx_Init(void);                       /* ADC1(PA4)/ADC2(PA5) 初始化 */
void ADCx_Poll(void);                       /* 采样两路并累计, 主循环每轮调用 */
uint8_t ADCx_ResultReady(void);             /* 1s 窗口完成有新结果返回 1 */
void ADCx_ResultClear(void);                /* 清除新结果标志 */
uint32_t ADCx_GetFreqHz(uint8_t Sel);       /* 取通道频率(Hz), Sel: 0=PA4 1=PA5 */
uint8_t ADCx_GetDutyPct(uint8_t Sel);       /* 取通道占空比(%), Sel: 0=PA4 1=PA5 */

#endif
