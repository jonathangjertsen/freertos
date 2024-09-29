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

#include "timers.h"

#include <stdbool.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.hpp"

#define tmrNO_DELAY ((TickType_t)0U)
#define tmrMAX_TIME_BEFORE_OVERFLOW ((TickType_t)-1)

#ifndef configTIMER_SERVICE_TASK_NAME
#define configTIMER_SERVICE_TASK_NAME "Tmr Svc"
#endif

#define tmrSTATUS_IS_ACTIVE (0x01U)
#define tmrSTATUS_IS_STATICALLY_ALLOCATED (0x02U)
#define tmrSTATUS_IS_AUTORELOAD (0x04U)
struct Timer_t {
  const char *pcTimerName;
  Item_t<Timer_t> TimerListItem;
  TickType_t TimerPeriodInTicks;
  void *pvTimerID;
  portTIMER_CALLBACK_ATTRIBUTE TimerCallbackFunction_t CallbackFunction;
  uint8_t ucStatus;
};

typedef struct tmrTimerParameters {
  TickType_t xMessageValue;
  Timer_t *pTimer;
} TimerParameter_t;

typedef struct tmrCallbackParameters {
  portTIMER_CALLBACK_ATTRIBUTE PendedFunction_t CallbackFunction;
  void *pvParameter1;
  uint32_t ulParameter2;
} CallbackParameters_t;

typedef struct tmrTimerQueueMessage {
  BaseType_t xMessageID;
  union {
    TimerParameter_t TimerParameters;

#if (INCLUDE_TimerPendFunctionCall == 1)
    CallbackParameters_t xCallbackParameters;
#endif
  } u;
} DaemonTaskMessage_t;

static List_t<Timer_t> ActiveTimerList1;
static List_t<Timer_t> ActiveTimerList2;
static List_t<Timer_t> *CurrentTimerList;
static List_t<Timer_t> *OverflowTimerList;

static QueueHandle_t TimerQueue = NULL;
static TaskHandle_t TimerTaskHandle = NULL;

static void CheckForValidListAndQueue(void);

static portTASK_FUNCTION_PROTO(TimerTask, Params);

static void ProcessReceivedCommands(void);

static BaseType_t InsertTimerInActiveList(Timer_t *const pTimer,
                                          const TickType_t xNextExpiryTime,
                                          const TickType_t TimeNow,
                                          const TickType_t xCommandTime);

static void ReloadTimer(Timer_t *const pTimer, TickType_t xExpiredTime,
                        const TickType_t TimeNow);

static void ProcessExpiredTimer(const TickType_t NextExpireTime,
                                const TickType_t TimeNow);

static void SwitchTimerLists(void);

static TickType_t SampleTimeNow(BaseType_t *const TimerListsWereSwitched);

static TickType_t GetNextExpireTime(BaseType_t *const ListWasEmpty);

static void ProcessTimerOrBlockTask(const TickType_t NextExpireTime,
                                    BaseType_t ListWasEmpty);

static void InitialiseNewTimer(const char *const pcTimerName,
                               const TickType_t TimerPeriodInTicks,
                               const BaseType_t xAutoReload,
                               void *const pvTimerID,
                               TimerCallbackFunction_t CallbackFunction,
                               Timer_t *NewTimer);

BaseType_t TimerCreateTimerTask(void) {
  BaseType_t Return = false;
  CheckForValidListAndQueue();
  if (TimerQueue != NULL) {
    StaticTask_t *pTimerTaskTCBBuffer = NULL;
    StackType_t *pTimerTaskStackBuffer = NULL;
    configSTACK_DEPTH_TYPE uTimerTaskStackSize;
    ApplicationGetTimerTaskMemory(&pTimerTaskTCBBuffer, &pTimerTaskStackBuffer,
                                  &uTimerTaskStackSize);
    TimerTaskHandle = TaskCreateStatic(
        TimerTask, configTIMER_SERVICE_TASK_NAME, uTimerTaskStackSize, NULL,
        ((UBaseType_t)configTIMER_TASK_PRIORITY) | portPRIVILEGE_BIT,
        pTimerTaskStackBuffer, pTimerTaskTCBBuffer);
    if (TimerTaskHandle != NULL) {
      Return = true;
    }
  }
  return Return;
}

TimerHandle_t TimerCreate(const char *const pcTimerName,
                          const TickType_t TimerPeriodInTicks,
                          const BaseType_t xAutoReload, void *const pvTimerID,
                          TimerCallbackFunction_t CallbackFunction) {
  Timer_t *NewTimer;
  NewTimer = (Timer_t *)pvPortMalloc(sizeof(Timer_t));
  if (NewTimer != NULL) {
    NewTimer->ucStatus = 0x00;
    InitialiseNewTimer(pcTimerName, TimerPeriodInTicks, xAutoReload, pvTimerID,
                       CallbackFunction, NewTimer);
  }

  return NewTimer;
}

TimerHandle_t TimerCreateStatic(const char *const pcTimerName,
                                const TickType_t TimerPeriodInTicks,
                                const BaseType_t xAutoReload,
                                void *const pvTimerID,
                                TimerCallbackFunction_t CallbackFunction,
                                StaticTimer_t *pTimerBuffer) {
  Timer_t *NewTimer;

  NewTimer = (Timer_t *)pTimerBuffer;
  if (NewTimer != NULL) {
    NewTimer->ucStatus = (uint8_t)tmrSTATUS_IS_STATICALLY_ALLOCATED;
    InitialiseNewTimer(pcTimerName, TimerPeriodInTicks, xAutoReload, pvTimerID,
                       CallbackFunction, NewTimer);
  }
  return NewTimer;
}

static void InitialiseNewTimer(const char *const pcTimerName,
                               const TickType_t TimerPeriodInTicks,
                               const BaseType_t xAutoReload,
                               void *const pvTimerID,
                               TimerCallbackFunction_t CallbackFunction,
                               Timer_t *NewTimer) {
  CheckForValidListAndQueue();
  NewTimer->pcTimerName = pcTimerName;
  NewTimer->TimerPeriodInTicks = TimerPeriodInTicks;
  NewTimer->pvTimerID = pvTimerID;
  NewTimer->CallbackFunction = CallbackFunction;
  NewTimer->TimerListItem.init();
  if (xAutoReload != false) {
    NewTimer->ucStatus |= (uint8_t)tmrSTATUS_IS_AUTORELOAD;
  }
}

BaseType_t TimerGenericCommandFromTask(
    TimerHandle_t Timer, const BaseType_t xCommandID,
    const TickType_t xOptionalValue, BaseType_t *const HigherPriorityTaskWoken,
    const TickType_t xTicksToWait) {
  DaemonTaskMessage_t xMessage;
  (void)HigherPriorityTaskWoken;
  if (TimerQueue == NULL) {
    return false;
  }
  xMessage.xMessageID = xCommandID;
  xMessage.u.TimerParameters.xMessageValue = xOptionalValue;
  xMessage.u.TimerParameters.pTimer = Timer;
  if (xCommandID < tmrFIRST_FROM_ISR_COMMAND) {
    return xQueueSendToBack(TimerQueue, &xMessage,
                            GetSchedulerState() == taskSCHEDULER_RUNNING
                                ? xTicksToWait
                                : tmrNO_DELAY);
  }
  return false;
}

BaseType_t TimerGenericCommandFromISR(TimerHandle_t Timer,
                                      const BaseType_t xCommandID,
                                      const TickType_t xOptionalValue,
                                      BaseType_t *const HigherPriorityTaskWoken,
                                      const TickType_t xTicksToWait) {
  DaemonTaskMessage_t xMessage;
  (void)xTicksToWait;
  if (TimerQueue == NULL) {
    return false;
  }
  xMessage.xMessageID = xCommandID;
  xMessage.u.TimerParameters.xMessageValue = xOptionalValue;
  xMessage.u.TimerParameters.pTimer = Timer;
  if (xCommandID >= tmrFIRST_FROM_ISR_COMMAND) {
    return xQueueSendToBackFromISR(TimerQueue, &xMessage,
                                   HigherPriorityTaskWoken);
  }
  return false;
}

TaskHandle_t TimerGetTimerDaemonTaskHandle(void) { return TimerTaskHandle; }

TickType_t TimerGetPeriod(TimerHandle_t Timer) {
  return Timer->TimerPeriodInTicks;
}

void vTimerSetReloadMode(TimerHandle_t Timer, const BaseType_t xAutoReload) {
  ENTER_CRITICAL();
  {
    if (xAutoReload != false) {
      Timer->ucStatus |= (uint8_t)tmrSTATUS_IS_AUTORELOAD;
    } else {
      Timer->ucStatus &= ((uint8_t)~tmrSTATUS_IS_AUTORELOAD);
    }
  }
  EXIT_CRITICAL();
}

BaseType_t TimerGetReloadMode(TimerHandle_t Timer) {
  BaseType_t Return;

  ENTER_CRITICAL();
  Return = (Timer->ucStatus & tmrSTATUS_IS_AUTORELOAD) != 0;
  EXIT_CRITICAL();
  return Return;
}
UBaseType_t uTimerGetReloadMode(TimerHandle_t Timer) {
  UBaseType_t uReturn;
  uReturn = (UBaseType_t)TimerGetReloadMode(Timer);
  return uReturn;
}

TickType_t TimerGetExpiryTime(TimerHandle_t Timer) {
  Timer_t *pTimer = Timer;
  TickType_t Return;

  return pTimer->TimerListItem.Value;
}

BaseType_t TimerGetStaticBuffer(TimerHandle_t Timer,
                                StaticTimer_t **ppTimerBuffer) {
  BaseType_t Return;
  Timer_t *pTimer = Timer;

  if ((pTimer->ucStatus & tmrSTATUS_IS_STATICALLY_ALLOCATED) != 0U) {
    *ppTimerBuffer = (StaticTimer_t *)pTimer;
    Return = true;
  } else {
    Return = false;
  }

  return Return;
}

const char *pcTimerGetName(TimerHandle_t Timer) {
  Timer_t *pTimer = Timer;
  return pTimer->pcTimerName;
}

static void ReloadTimer(Timer_t *const pTimer, TickType_t xExpiredTime,
                        const TickType_t TimeNow) {
  while (InsertTimerInActiveList(pTimer,
                                 (xExpiredTime + pTimer->TimerPeriodInTicks),
                                 TimeNow, xExpiredTime) != false) {
    xExpiredTime += pTimer->TimerPeriodInTicks;
    pTimer->CallbackFunction((TimerHandle_t)pTimer);
  }
}

static void ProcessExpiredTimer(const TickType_t NextExpireTime,
                                const TickType_t TimeNow) {
  Timer_t *const pTimer = CurrentTimerList->head()->Owner;
  pTimer->TimerListItem.remove();
  if ((pTimer->ucStatus & tmrSTATUS_IS_AUTORELOAD) != 0U) {
    ReloadTimer(pTimer, NextExpireTime, TimeNow);
  } else {
    pTimer->ucStatus &= ((uint8_t)~tmrSTATUS_IS_ACTIVE);
  }
  pTimer->CallbackFunction((TimerHandle_t)pTimer);
}

static portTASK_FUNCTION(TimerTask, Params) {
  TickType_t NextExpireTime;
  BaseType_t ListWasEmpty;
  (void)Params;
  for (; configCONTROL_INFINITE_LOOP();) {
    NextExpireTime = GetNextExpireTime(&ListWasEmpty);
    ProcessTimerOrBlockTask(NextExpireTime, ListWasEmpty);
    ProcessReceivedCommands();
  }
}

static void ProcessTimerOrBlockTask(const TickType_t NextExpireTime,
                                    BaseType_t ListWasEmpty) {
  TickType_t TimeNow;
  BaseType_t TimerListsWereSwitched;
  TaskSuspendAll();
  TimeNow = SampleTimeNow(&TimerListsWereSwitched);
  if (TimerListsWereSwitched) {
    ResumeAll();
    return;
  }
  if ((ListWasEmpty == false) && (NextExpireTime <= TimeNow)) {
    (void)ResumeAll();
    ProcessExpiredTimer(NextExpireTime, TimeNow);
  } else {
    ListWasEmpty |= OverflowTimerList->empty();
    vQueueWaitForMessageRestricted(TimerQueue, (NextExpireTime - TimeNow),
                                   ListWasEmpty);
    if (ResumeAll() == false) {
      taskYIELD_WITHIN_API();
    }
  }
}

static TickType_t GetNextExpireTime(BaseType_t *const ListWasEmpty) {
  TickType_t NextExpireTime;
  *ListWasEmpty = CurrentTimerList->empty();
  return *ListWasEmpty ? 0 : CurrentTimerList->head()->Value;
}

static TickType_t SampleTimeNow(BaseType_t *const TimerListsWereSwitched) {
  static TickType_t xLastTime = (TickType_t)0U;
  TickType_t TimeNow = GetTickCount();
  *TimerListsWereSwitched = TimeNow < xLastTime;
  if (*TimerListsWereSwitched) {
    SwitchTimerLists();
  }
  xLastTime = TimeNow;
  return TimeNow;
}

static BaseType_t InsertTimerInActiveList(Timer_t *const pTimer,
                                          const TickType_t xNextExpiryTime,
                                          const TickType_t TimeNow,
                                          const TickType_t xCommandTime) {
  BaseType_t xProcessTimerNow = false;
  pTimer->TimerListItem.Value = xNextExpiryTime;
  pTimer->TimerListItem.Owner = pTimer;
  if (xNextExpiryTime <= TimeNow) {
    if (((TickType_t)(TimeNow - xCommandTime)) >= pTimer->TimerPeriodInTicks) {
      xProcessTimerNow = true;
    } else {
      OverflowTimerList->insert(&(pTimer->TimerListItem));
    }
  } else {
    if ((TimeNow < xCommandTime) && (xNextExpiryTime >= xCommandTime)) {
      xProcessTimerNow = true;
    } else {
      CurrentTimerList->insert(&(pTimer->TimerListItem));
    }
  }
  return xProcessTimerNow;
}

static void ProcessReceivedCommands(void) {
  DaemonTaskMessage_t xMessage = {0};
  Timer_t *pTimer;
  BaseType_t TimerListsWereSwitched;
  TickType_t TimeNow;
  while (Recv(TimerQueue, &xMessage, tmrNO_DELAY) != false) {
    if (xMessage.xMessageID < (BaseType_t)0) {
      const CallbackParameters_t *const Callback =
          &(xMessage.u.xCallbackParameters);
      Callback->CallbackFunction(Callback->pvParameter1,
                                 Callback->ulParameter2);
    }
    if (xMessage.xMessageID >= (BaseType_t)0) {
      pTimer = xMessage.u.TimerParameters.pTimer;
      if (pTimer->TimerListItem.Container != nullptr) {
        pTimer->TimerListItem.remove();
      }
      TimeNow = SampleTimeNow(&TimerListsWereSwitched);
      switch (xMessage.xMessageID) {
        case tmrCOMMAND_START:
        case tmrCOMMAND_START_FROM_ISR:
        case tmrCOMMAND_RESET:
        case tmrCOMMAND_RESET_FROM_ISR:
          pTimer->ucStatus |= (uint8_t)tmrSTATUS_IS_ACTIVE;
          if (InsertTimerInActiveList(
                  pTimer,
                  xMessage.u.TimerParameters.xMessageValue +
                      pTimer->TimerPeriodInTicks,
                  TimeNow, xMessage.u.TimerParameters.xMessageValue) != false) {
            if ((pTimer->ucStatus & tmrSTATUS_IS_AUTORELOAD) != 0U) {
              ReloadTimer(pTimer,
                          xMessage.u.TimerParameters.xMessageValue +
                              pTimer->TimerPeriodInTicks,
                          TimeNow);
            } else {
              pTimer->ucStatus &= ((uint8_t)~tmrSTATUS_IS_ACTIVE);
            }
            pTimer->CallbackFunction((TimerHandle_t)pTimer);
          }
          break;
        case tmrCOMMAND_STOP:
        case tmrCOMMAND_STOP_FROM_ISR:

          pTimer->ucStatus &= ((uint8_t)~tmrSTATUS_IS_ACTIVE);
          break;
        case tmrCOMMAND_CHANGE_PERIOD:
        case tmrCOMMAND_CHANGE_PERIOD_FROM_ISR:
          pTimer->ucStatus |= (uint8_t)tmrSTATUS_IS_ACTIVE;
          pTimer->TimerPeriodInTicks = xMessage.u.TimerParameters.xMessageValue;
          (void)InsertTimerInActiveList(
              pTimer, (TimeNow + pTimer->TimerPeriodInTicks), TimeNow, TimeNow);
          break;
        case tmrCOMMAND_DELETE:
          if ((pTimer->ucStatus & tmrSTATUS_IS_STATICALLY_ALLOCATED) ==
              (uint8_t)0) {
            vPortFree(pTimer);
          } else {
            pTimer->ucStatus &= ((uint8_t)~tmrSTATUS_IS_ACTIVE);
          }
          break;
        default:
          break;
      }
    }
  }
}

static void SwitchTimerLists(void) {
  TickType_t NextExpireTime;
  List_t<Timer_t> *Temp;
  while (CurrentTimerList->Length > 0) {
    NextExpireTime = CurrentTimerList->head()->Value;
    ProcessExpiredTimer(NextExpireTime, tmrMAX_TIME_BEFORE_OVERFLOW);
  }
  Temp = CurrentTimerList;
  CurrentTimerList = OverflowTimerList;
  OverflowTimerList = Temp;
}

static void CheckForValidListAndQueue(void) {
  ENTER_CRITICAL();
  if (TimerQueue == NULL) {
    ActiveTimerList1.init();
    ActiveTimerList2.init();
    CurrentTimerList = &ActiveTimerList1;
    OverflowTimerList = &ActiveTimerList2;
    static StaticQueue_t StaticTimerQueue;
    static uint8_t StaticTimerQueueStorage[(size_t)configTIMER_QUEUE_LENGTH *
                                           sizeof(DaemonTaskMessage_t)];
    TimerQueue =
        xQueueCreateStatic((UBaseType_t)configTIMER_QUEUE_LENGTH,
                           (UBaseType_t)sizeof(DaemonTaskMessage_t),
                           &(StaticTimerQueueStorage[0]), &StaticTimerQueue);
  }
  EXIT_CRITICAL();
}

BaseType_t TimerIsTimerActive(TimerHandle_t Timer) {
  ENTER_CRITICAL();
  BaseType_t Return = (Timer->ucStatus & tmrSTATUS_IS_ACTIVE) != 0U;
  EXIT_CRITICAL();
  return Return;
}

void *pvTimerGetTimerID(const TimerHandle_t Timer) {
  ENTER_CRITICAL();
  void *pvReturn = Timer->pvTimerID;
  EXIT_CRITICAL();
  return pvReturn;
}

void SetTimerID(TimerHandle_t Timer, void *pvNewID) {
  ENTER_CRITICAL();
  Timer->pvTimerID = pvNewID;
  EXIT_CRITICAL();
}

BaseType_t TimerPendFunctionCallFromISR(PendedFunction_t xFunctionToPend,
                                        void *pvParameter1,
                                        uint32_t ulParameter2,
                                        BaseType_t *HigherPriorityTaskWoken) {
  DaemonTaskMessage_t xMessage;
  xMessage.xMessageID = tmrCOMMAND_EXECUTE_CALLBACK_FROM_ISR;
  xMessage.u.xCallbackParameters.CallbackFunction = xFunctionToPend;
  xMessage.u.xCallbackParameters.pvParameter1 = pvParameter1;
  xMessage.u.xCallbackParameters.ulParameter2 = ulParameter2;
  return xQueueSendFromISR(TimerQueue, &xMessage, HigherPriorityTaskWoken);
}

BaseType_t TimerPendFunctionCall(PendedFunction_t xFunctionToPend,
                                 void *pvParameter1, uint32_t ulParameter2,
                                 TickType_t xTicksToWait) {
  DaemonTaskMessage_t xMessage;
  xMessage.xMessageID = tmrCOMMAND_EXECUTE_CALLBACK;
  xMessage.u.xCallbackParameters.CallbackFunction = xFunctionToPend;
  xMessage.u.xCallbackParameters.pvParameter1 = pvParameter1;
  xMessage.u.xCallbackParameters.ulParameter2 = ulParameter2;
  return xQueueSendToBack(TimerQueue, &xMessage, xTicksToWait);
}

void TimerResetState(void) {
  TimerQueue = NULL;
  TimerTaskHandle = NULL;
}
