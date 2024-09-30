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
  const char *Name;
  Item_t<Timer_t> TimerListItem;
  TickType_t Period;
  void *ID;
  portTIMER_CALLBACK_ATTRIBUTE TimerCallbackFunction_t Callback;
  uint8_t status;

  bool StaticallyAllocated() { return status & tmrSTATUS_IS_STATICALLY_ALLOCATED; }

  bool Active() { return status & tmrSTATUS_IS_ACTIVE; }

  void Activate() { status |= tmrSTATUS_IS_ACTIVE; }

  void Deactivate() { status &= ~tmrSTATUS_IS_ACTIVE; }

  void SetActiveState(bool active) {
    if (active) {
      Activate();
    } else {
      Deactivate();
    }
  }

  bool Autoreload() { return status & tmrSTATUS_IS_AUTORELOAD; }
};

typedef struct tmrTimerParameters {
  TickType_t Value;
  Timer_t *Timer;
} TimerParameter_t;

typedef struct tmrCallbackParameters {
  portTIMER_CALLBACK_ATTRIBUTE PendedFunction_t Func;
  void *Param1;
  uint32_t Param2;
} CallbackParameters_t;

typedef struct tmrTimerQueueMessage {
  BaseType_t ID;
  union {
    TimerParameter_t TimerParams;
    CallbackParameters_t CBParams;
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

static BaseType_t InsertTimerInActiveList(Timer_t *const pTimer, const TickType_t xNextExpiryTime, const TickType_t Now,
                                          const TickType_t xCommandTime);

static void ReloadTimer(Timer_t *const pTimer, TickType_t Expired, const TickType_t Now);

static void ProcessExpiredTimer(const TickType_t NextExpire, const TickType_t Now);

static void SwitchTimerLists(void);

static TickType_t SampleTimeNow(BaseType_t *const switched);

static TickType_t GetNextExpireTime(BaseType_t *const ListWasEmpty);

static void ProcessTimerOrBlockTask(const TickType_t NextExpire, BaseType_t ListWasEmpty);

static void InitialiseNewTimer(const char *const name, const TickType_t period, const BaseType_t autoReload,
                               void *const id, TimerCallbackFunction_t f, Timer_t *t);

BaseType_t TimerCreateTimerTask(void) {
  CheckForValidListAndQueue();
  if (TimerQueue == nullptr) {
    return false;
  }
  StaticTask_t *tcbBuf = NULL;
  StackType_t *stackBuf = NULL;
  configSTACK_DEPTH_TYPE stackSize;
  ApplicationGetTimerTaskMemory(&tcbBuf, &stackBuf, &stackSize);
  TimerTaskHandle = TaskCreateStatic(TimerTask, configTIMER_SERVICE_TASK_NAME, stackSize, NULL,
                                     ((UBaseType_t)configTIMER_TASK_PRIORITY) | portPRIVILEGE_BIT, stackBuf, tcbBuf);
  return TimerTaskHandle != nullptr;
}

Timer_t *TimerCreate(const char *const name, const TickType_t period, const BaseType_t autoReload, void *const id,
                     TimerCallbackFunction_t f) {
  Timer_t *t;
  t = (Timer_t *)pvPortMalloc(sizeof(Timer_t));
  if (t != NULL) {
    t->status = 0x00;
    InitialiseNewTimer(name, period, autoReload, id, f, t);
  }

  return t;
}

Timer_t *TimerCreateStatic(const char *const name, const TickType_t period, const BaseType_t autoReload, void *const id,
                           TimerCallbackFunction_t f, StaticTimer_t *buf) {
  if (buf == nullptr) {
    return nullptr;
  }
  Timer_t *t = (Timer_t *)buf;
  t->status = (uint8_t)tmrSTATUS_IS_STATICALLY_ALLOCATED;
  InitialiseNewTimer(name, period, autoReload, id, f, t);
  return t;
}

static void InitialiseNewTimer(const char *const name, const TickType_t period, const BaseType_t autoReload,
                               void *const id, TimerCallbackFunction_t f, Timer_t *t) {
  CheckForValidListAndQueue();
  t->Name = name;
  t->Period = period;
  t->ID = id;
  t->Callback = f;
  t->TimerListItem.init();
  if (autoReload) {
    t->status |= (uint8_t)tmrSTATUS_IS_AUTORELOAD;
  }
}

BaseType_t TimerGenericCommandFromTask(Timer_t *Timer, const BaseType_t ID, const TickType_t xOptionalValue,
                                       BaseType_t *const woken, const TickType_t xTicksToWait) {
  DaemonTaskMessage_t xMessage;
  (void)woken;
  if (TimerQueue == NULL) {
    return false;
  }
  xMessage.ID = ID;
  xMessage.u.TimerParams.Value = xOptionalValue;
  xMessage.u.TimerParams.Timer = Timer;
  if (ID < tmrFIRST_FROM_ISR_COMMAND) {
    return QueueSendToBack(TimerQueue, &xMessage,
                           GetSchedulerState() == taskSCHEDULER_RUNNING ? xTicksToWait : tmrNO_DELAY);
  }
  return false;
}

BaseType_t TimerGenericCommandFromISR(Timer_t *t, const BaseType_t ID, const TickType_t xOptionalValue,
                                      BaseType_t *const woken, const TickType_t xTicksToWait) {
  DaemonTaskMessage_t xMessage;
  (void)xTicksToWait;
  if (TimerQueue == NULL) {
    return false;
  }
  xMessage.ID = ID;
  xMessage.u.TimerParams.Value = xOptionalValue;
  xMessage.u.TimerParams.Timer = t;
  if (ID >= tmrFIRST_FROM_ISR_COMMAND) {
    return QueueSendToBackFromISR(TimerQueue, &xMessage, woken);
  }
  return false;
}

TaskHandle_t TimerGetTimerDaemonTaskHandle(void) { return TimerTaskHandle; }

TickType_t TimerGetPeriod(Timer_t *t) { return t->Period; }

void vTimerSetReloadMode(Timer_t *t, const BaseType_t autoReload) {
  CriticalSection s;
  if (autoReload) {
    t->status |= (uint8_t)tmrSTATUS_IS_AUTORELOAD;
  } else {
    t->status &= ((uint8_t)~tmrSTATUS_IS_AUTORELOAD);
  }
}

BaseType_t TimerGetReloadMode(Timer_t *Timer) {
  CriticalSection s;
  return (Timer->status & tmrSTATUS_IS_AUTORELOAD) != 0;
}
UBaseType_t uTimerGetReloadMode(Timer_t *Timer) { return (UBaseType_t)TimerGetReloadMode(Timer); }

TickType_t TimerGetExpiryTime(Timer_t *Timer) { return Timer->TimerListItem.Value; }

BaseType_t TimerGetStaticBuffer(Timer_t *t, StaticTimer_t **pbuf) {
  if (t->StaticallyAllocated()) {
    *pbuf = (StaticTimer_t *)t;
  }
  return t->StaticallyAllocated();
}

const char *pcTimerGetName(Timer_t *t) { return t->Name; }

static void ReloadTimer(Timer_t *const t, TickType_t Expired, const TickType_t Now) {
  while (InsertTimerInActiveList(t, (Expired + t->Period), Now, Expired)) {
    Expired += t->Period;
    t->Callback((Timer_t *)t);
  }
}

static void ProcessExpiredTimer(const TickType_t NextExpire, const TickType_t Now) {
  Timer_t *const t = CurrentTimerList->head()->Owner;
  t->TimerListItem.remove();
  if (t->Autoreload()) {
    ReloadTimer(t, NextExpire, Now);
  } else {
    t->Deactivate();
  }
  t->Callback((Timer_t *)t);
}

static portTASK_FUNCTION(TimerTask, Params) {
  TickType_t NextExpire;
  BaseType_t ListWasEmpty;
  (void)Params;
  for (; configCONTROL_INFINITE_LOOP();) {
    NextExpire = GetNextExpireTime(&ListWasEmpty);
    ProcessTimerOrBlockTask(NextExpire, ListWasEmpty);
    ProcessReceivedCommands();
  }
}

static void ProcessTimerOrBlockTask(const TickType_t NextExpire, BaseType_t ListWasEmpty) {
  TickType_t Now;
  BaseType_t switched;
  TaskSuspendAll();
  Now = SampleTimeNow(&switched);
  if (switched) {
    ResumeAll();
    return;
  }
  if ((ListWasEmpty == false) && (NextExpire <= Now)) {
    ResumeAll();
    ProcessExpiredTimer(NextExpire, Now);
  } else {
    ListWasEmpty |= OverflowTimerList->empty();
    vQueueWaitForMessageRestricted(TimerQueue, (NextExpire - Now), ListWasEmpty);
    if (!ResumeAll()) {
      taskYIELD_WITHIN_API();
    }
  }
}

static TickType_t GetNextExpireTime(BaseType_t *const ListWasEmpty) {
  TickType_t NextExpire;
  *ListWasEmpty = CurrentTimerList->empty();
  return *ListWasEmpty ? 0 : CurrentTimerList->head()->Value;
}

static TickType_t SampleTimeNow(BaseType_t *const switched) {
  static TickType_t xLastTime = (TickType_t)0U;
  TickType_t Now = GetTickCount();
  *switched = Now < xLastTime;
  if (*switched) {
    SwitchTimerLists();
  }
  xLastTime = Now;
  return Now;
}

static BaseType_t InsertTimerInActiveList(Timer_t *const pTimer, const TickType_t xNextExpiryTime, const TickType_t Now,
                                          const TickType_t xCommandTime) {
  BaseType_t xProcessTimerNow = false;
  pTimer->TimerListItem.Value = xNextExpiryTime;
  pTimer->TimerListItem.Owner = pTimer;
  if (xNextExpiryTime <= Now) {
    if (((TickType_t)(Now - xCommandTime)) >= pTimer->Period) {
      xProcessTimerNow = true;
    } else {
      OverflowTimerList->insert(&(pTimer->TimerListItem));
    }
  } else {
    if ((Now < xCommandTime) && (xNextExpiryTime >= xCommandTime)) {
      xProcessTimerNow = true;
    } else {
      CurrentTimerList->insert(&(pTimer->TimerListItem));
    }
  }
  return xProcessTimerNow;
}

static void ProcessReceivedCommands(void) {
  DaemonTaskMessage_t msg = {0};
  Timer_t *pTimer;
  BaseType_t switched;
  TickType_t now;
  while (Recv(TimerQueue, &msg, 0)) {
    if (msg.ID < (BaseType_t)0) {
      const CallbackParameters_t *const Callback = &(msg.u.CBParams);
      Callback->Func(Callback->Param1, Callback->Param2);
    }
    if (msg.ID >= 0) {
      pTimer = msg.u.TimerParams.Timer;
      if (pTimer->TimerListItem.Container != nullptr) {
        pTimer->TimerListItem.remove();
      }
      now = SampleTimeNow(&switched);
      switch (msg.ID) {
        case tmrCOMMAND_START:
        case tmrCOMMAND_START_FROM_ISR:
        case tmrCOMMAND_RESET:
        case tmrCOMMAND_RESET_FROM_ISR:
          pTimer->Activate();
          if (InsertTimerInActiveList(pTimer, msg.u.TimerParams.Value + pTimer->Period, now, msg.u.TimerParams.Value) !=
              false) {
            if (pTimer->Autoreload()) {
              ReloadTimer(pTimer, msg.u.TimerParams.Value + pTimer->Period, now);
            } else {
              pTimer->Deactivate();
            }
            pTimer->Callback((Timer_t *)pTimer);
          }
          break;
        case tmrCOMMAND_STOP:
        case tmrCOMMAND_STOP_FROM_ISR:
          pTimer->Deactivate();
          break;
        case tmrCOMMAND_CHANGE_PERIOD:
        case tmrCOMMAND_CHANGE_PERIOD_FROM_ISR:
          pTimer->Activate();
          pTimer->Period = msg.u.TimerParams.Value;
          (void)InsertTimerInActiveList(pTimer, (now + pTimer->Period), now, now);
          break;
        case tmrCOMMAND_DELETE:
          if (!pTimer->StaticallyAllocated()) {
            vPortFree(pTimer);
          } else {
            pTimer->Deactivate();
          }
          break;
        default:
          break;
      }
    }
  }
}

static void SwitchTimerLists(void) {
  TickType_t NextExpire;
  List_t<Timer_t> *Temp;
  while (CurrentTimerList->Length > 0) {
    NextExpire = CurrentTimerList->head()->Value;
    ProcessExpiredTimer(NextExpire, tmrMAX_TIME_BEFORE_OVERFLOW);
  }
  Temp = CurrentTimerList;
  CurrentTimerList = OverflowTimerList;
  OverflowTimerList = Temp;
}

static void CheckForValidListAndQueue(void) {
  CriticalSection s;
  if (TimerQueue != NULL) {
    return;
  }
  ActiveTimerList1.init();
  ActiveTimerList2.init();
  CurrentTimerList = &ActiveTimerList1;
  OverflowTimerList = &ActiveTimerList2;
  static StaticQueue_t StaticTimerQueue;
  static uint8_t StaticTimerQueueStorage[(size_t)configTIMER_QUEUE_LENGTH * sizeof(DaemonTaskMessage_t)];
  TimerQueue = QueueCreateStatic((UBaseType_t)configTIMER_QUEUE_LENGTH, (UBaseType_t)sizeof(DaemonTaskMessage_t),
                                 &(StaticTimerQueueStorage[0]), &StaticTimerQueue);
}

bool TimerIsTimerActive(Timer_t *Timer) {
  CriticalSection s;
  return Timer->Active();
}

void *pvTimerGetTimerID(const Timer_t *Timer) {
  CriticalSection s;
  return Timer->ID;
}

void SetTimerID(Timer_t *Timer, void *pvNewID) {
  CriticalSection s;
  Timer->ID = pvNewID;
}

BaseType_t TimerPendFunctionCallFromISR(PendedFunction_t f, void *param1, uint32_t param2, BaseType_t *woken) {
  DaemonTaskMessage_t xMessage;
  xMessage.ID = tmrCOMMAND_EXECUTE_CALLBACK_FROM_ISR;
  xMessage.u.CBParams.Func = f;
  xMessage.u.CBParams.Param1 = param1;
  xMessage.u.CBParams.Param2 = param2;
  return QueueSendFromISR(TimerQueue, &xMessage, woken);
}

BaseType_t TimerPendFunctionCall(PendedFunction_t f, void *param1, uint32_t param2, TickType_t xTicksToWait) {
  DaemonTaskMessage_t xMessage;
  xMessage.ID = tmrCOMMAND_EXECUTE_CALLBACK;
  xMessage.u.CBParams.Func = f;
  xMessage.u.CBParams.Param1 = param1;
  xMessage.u.CBParams.Param2 = param2;
  return QueueSendToBack(TimerQueue, &xMessage, xTicksToWait);
}

void TimerResetState(void) {
  TimerQueue = NULL;
  TimerTaskHandle = NULL;
}
