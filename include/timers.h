/*
 * FreeRTOS Kernel <DEVELOPMENT BRANCH>
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#include "freertos.h"
#include "task.hpp"
#ifdef __cplusplus
extern "C" {
#endif
#define tmrCOMMAND_EXECUTE_CALLBACK_FROM_ISR ((BaseType_t)-2)
#define tmrCOMMAND_EXECUTE_CALLBACK ((BaseType_t)-1)
#define tmrCOMMAND_START_DONT_TRACE ((BaseType_t)0)
#define tmrCOMMAND_START ((BaseType_t)1)
#define tmrCOMMAND_RESET ((BaseType_t)2)
#define tmrCOMMAND_STOP ((BaseType_t)3)
#define tmrCOMMAND_CHANGE_PERIOD ((BaseType_t)4)
#define tmrCOMMAND_DELETE ((BaseType_t)5)
#define tmrFIRST_FROM_ISR_COMMAND ((BaseType_t)6)
#define tmrCOMMAND_START_FROM_ISR ((BaseType_t)6)
#define tmrCOMMAND_RESET_FROM_ISR ((BaseType_t)7)
#define tmrCOMMAND_STOP_FROM_ISR ((BaseType_t)8)
#define tmrCOMMAND_CHANGE_PERIOD_FROM_ISR ((BaseType_t)9)
struct Timer_t;
using TimerHandle_t = Timer_t*;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t Timer);
typedef void (*PendedFunction_t)(void* arg1, uint32_t arg2);
#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
TimerHandle_t TimerCreate(const char* const pcTimerName,
                          const TickType_t TimerPeriodInTicks,
                          const BaseType_t xAutoReload, void* const pvTimerID,
                          TimerCallbackFunction_t pxCallbackFunction);
#endif
#if (configSUPPORT_STATIC_ALLOCATION == 1)
TimerHandle_t TimerCreateStatic(const char* const pcTimerName,
                                const TickType_t TimerPeriodInTicks,
                                const BaseType_t xAutoReload,
                                void* const pvTimerID,
                                TimerCallbackFunction_t pxCallbackFunction,
                                StaticTimer_t* pTimerBuffer);
#endif
void* pvTimerGetTimerID(const TimerHandle_t Timer);
void SetTimerID(TimerHandle_t Timer, void* pvNewID);
BaseType_t TimerIsTimerActive(TimerHandle_t Timer);
TaskHandle_t TimerGetTimerDaemonTaskHandle(void);
#define TimerStart(Timer, xTicksToWait)                                       \
  TimerGenericCommand((Timer), tmrCOMMAND_START, (xTaskGetTickCount()), NULL, \
                      (xTicksToWait))
#define TimerStop(Timer, xTicksToWait) \
  TimerGenericCommand((Timer), tmrCOMMAND_STOP, 0U, NULL, (xTicksToWait))
#define TimerChangePeriod(Timer, xNewPeriod, xTicksToWait)                   \
  TimerGenericCommand((Timer), tmrCOMMAND_CHANGE_PERIOD, (xNewPeriod), NULL, \
                      (xTicksToWait))
#define TimerDelete(Timer, xTicksToWait) \
  TimerGenericCommand((Timer), tmrCOMMAND_DELETE, 0U, NULL, (xTicksToWait))
#define TimerReset(Timer, xTicksToWait)                                       \
  TimerGenericCommand((Timer), tmrCOMMAND_RESET, (xTaskGetTickCount()), NULL, \
                      (xTicksToWait))
#define TimerStartFromISR(Timer, pxHigherPriorityTaskWoken) \
  TimerGenericCommand((Timer), tmrCOMMAND_START_FROM_ISR,   \
                      (xTaskGetTickCountFromISR()),         \
                      (pxHigherPriorityTaskWoken), 0U)
#define TimerStopFromISR(Timer, pxHigherPriorityTaskWoken)  \
  TimerGenericCommand((Timer), tmrCOMMAND_STOP_FROM_ISR, 0, \
                      (pxHigherPriorityTaskWoken), 0U)
#define TimerChangePeriodFromISR(Timer, xNewPeriod, pxHigherPriorityTaskWoken) \
  TimerGenericCommand((Timer), tmrCOMMAND_CHANGE_PERIOD_FROM_ISR,              \
                      (xNewPeriod), (pxHigherPriorityTaskWoken), 0U)
#define TimerResetFromISR(Timer, pxHigherPriorityTaskWoken) \
  TimerGenericCommand((Timer), tmrCOMMAND_RESET_FROM_ISR,   \
                      (xTaskGetTickCountFromISR()),         \
                      (pxHigherPriorityTaskWoken), 0U)
BaseType_t TimerPendFunctionCallFromISR(PendedFunction_t xFunctionToPend,
                                        void* pvParameter1,
                                        uint32_t ulParameter2,
                                        BaseType_t* pxHigherPriorityTaskWoken);
BaseType_t TimerPendFunctionCall(PendedFunction_t xFunctionToPend,
                                 void* pvParameter1, uint32_t ulParameter2,
                                 TickType_t xTicksToWait);
const char* pcTimerGetName(TimerHandle_t Timer);
void vTimerSetReloadMode(TimerHandle_t Timer, const BaseType_t xAutoReload);
BaseType_t TimerGetReloadMode(TimerHandle_t Timer);
UBaseType_t uTimerGetReloadMode(TimerHandle_t Timer);
TickType_t TimerGetPeriod(TimerHandle_t Timer);
TickType_t TimerGetExpiryTime(TimerHandle_t Timer);
BaseType_t TimerGetStaticBuffer(TimerHandle_t Timer,
                                StaticTimer_t** ppTimerBuffer);
BaseType_t TimerCreateTimerTask(void);
BaseType_t TimerGenericCommandFromTask(
    TimerHandle_t Timer, const BaseType_t xCommandID,
    const TickType_t xOptionalValue,
    BaseType_t* const pxHigherPriorityTaskWoken, const TickType_t xTicksToWait);
BaseType_t TimerGenericCommandFromISR(
    TimerHandle_t Timer, const BaseType_t xCommandID,
    const TickType_t xOptionalValue,
    BaseType_t* const pxHigherPriorityTaskWoken, const TickType_t xTicksToWait);
#define TimerGenericCommand(Timer, xCommandID, xOptionalValue,                \
                            pxHigherPriorityTaskWoken, xTicksToWait)          \
  ((xCommandID) < tmrFIRST_FROM_ISR_COMMAND                                   \
       ? TimerGenericCommandFromTask(Timer, xCommandID, xOptionalValue,       \
                                     pxHigherPriorityTaskWoken, xTicksToWait) \
       : TimerGenericCommandFromISR(Timer, xCommandID, xOptionalValue,        \
                                    pxHigherPriorityTaskWoken, xTicksToWait))
void ApplicationGetTimerTaskMemory(
    StaticTask_t** ppTimerTaskTCBBuffer, StackType_t** ppTimerTaskStackBuffer,
    configSTACK_DEPTH_TYPE* puTimerTaskStackSize);
void TimerResetState(void);
#ifdef __cplusplus
}
#endif
