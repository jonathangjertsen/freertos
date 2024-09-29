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

#pragma once

#include "list.hpp"

#define tskKERNEL_VERSION_NUMBER "V11.1.0+"
#define tskKERNEL_VERSION_MAJOR 11
#define tskKERNEL_VERSION_MINOR 1
#define tskKERNEL_VERSION_BUILD 0

#define tskDEFAULT_INDEX_TO_NOTIFY (0)

struct TCB_t;
typedef struct TCB_t *TaskHandle_t;
typedef const struct TCB_t *ConstTaskHandle_t;

typedef BaseType_t (*TaskHookFunction_t)(void *arg);

typedef enum {
  eRunning = 0,
  eReady,
  eBlocked,
  eSuspended,
  eDeleted,
  eInvalid
} TaskState;

typedef enum {
  eNoAction = 0,
  eSetBits,
  eIncrement,
  eSetValueWithOverwrite,
  eSetValueWithoutOverwrite
} eNotifyAction;

typedef struct xTIME_OUT {
  BaseType_t OverflowCount;
  TickType_t TimeOnEntering;
} TimeOut_t;

typedef struct xMEMORY_REGION {
  void *Address;
  uint32_t NBytes;
  uint32_t Params;
} MemoryRegion_t;

struct TaskParameters_t {
  TaskFunction_t TaskCode;
  const char *Name;
  configSTACK_DEPTH_TYPE StackDepth;
  void *Params;
  UBaseType_t Priority;
  StackType_t *StackBuffer;
  MemoryRegion_t Regions[portNUM_CONFIGURABLE_REGIONS];
};

struct TaskStatus_t {
  TaskHandle_t Handle;
  const char *Name;
  UBaseType_t Number;
  TaskState State;
  UBaseType_t Priority;
  UBaseType_t BasePriority;
  configRUN_TIME_COUNTER_TYPE RTCounter;
  StackType_t *StackBase;
  configSTACK_DEPTH_TYPE StackHighWaterMark;
};

enum eSleepModeStatus {
  eAbortSleep = 0,
  eStandardSleep,
  eNoTasksWaitingTimeout
};

#define tskIDLE_PRIORITY ((UBaseType_t)0U)

#define tskNO_AFFINITY ((UBaseType_t)-1)

#define taskYIELD() portYIELD()

#define ENTER_CRITICAL() portENTER_CRITICAL()
#define ENTER_CRITICAL_FROM_ISR() portSET_INTERRUPT_MASK_FROM_ISR()

#define EXIT_CRITICAL() portEXIT_CRITICAL()
#define EXIT_CRITICAL_FROM_ISR(x) portCLEAR_INTERRUPT_MASK_FROM_ISR(x)

struct CriticalSection {
  CriticalSection() { ENTER_CRITICAL(); }
  ~CriticalSection() { EXIT_CRITICAL(); }
};

#define taskDISABLE_INTERRUPTS() portDISABLE_INTERRUPTS()

#define taskENABLE_INTERRUPTS() portENABLE_INTERRUPTS()

#define taskSCHEDULER_SUSPENDED ((BaseType_t)0)
#define taskSCHEDULER_NOT_STARTED ((BaseType_t)1)
#define taskSCHEDULER_RUNNING ((BaseType_t)2)

#define taskVALID_CORE_ID(xCoreID)                     \
  (((((BaseType_t)0 <= (xCoreID)) &&                   \
     ((xCoreID) < (BaseType_t)configNUMBER_OF_CORES))) \
       ? (true)                                        \
       : (false))

BaseType_t TaskCreate(TaskFunction_t Code, const char *const Name,
                      const configSTACK_DEPTH_TYPE StackDepth,
                      void *const Params, UBaseType_t Priority,
                      TaskHandle_t *const CreatedTask);

TaskHandle_t TaskCreateStatic(TaskFunction_t Code, const char *const Name,
                              const configSTACK_DEPTH_TYPE StackDepth,
                              void *const Params, UBaseType_t Priority,
                              StackType_t *const StackBuffer,
                              StaticTask_t *const TaskBuffer);

void TaskDelete(TaskHandle_t TaskToDelete);

void Delay(const TickType_t xTicksToDelay);

BaseType_t DelayUntil(TickType_t *const PreviousWakeTime,
                      const TickType_t xTimeIncrement);

BaseType_t AbortDelay(TaskHandle_t Task);

UBaseType_t TaskPriorityGet(const TaskHandle_t Task);

UBaseType_t TaskPriorityGetFromISR(const TaskHandle_t Task);

UBaseType_t TaskBasePriorityGet(const TaskHandle_t Task);

UBaseType_t TaskBasePriorityGetFromISR(const TaskHandle_t Task);

TaskState TaskGetState(TaskHandle_t Task);

void TaskPrioritySet(TaskHandle_t Task, UBaseType_t uxNewPriority);

void TaskSuspend(TaskHandle_t TaskToSuspend);

void TaskResume(TaskHandle_t TaskToResume);

BaseType_t TaskResumeFromISR(TaskHandle_t TaskToResume);

void TaskStartScheduler(void);

void TaskEndScheduler(void);

void TaskSuspendAll(void);

BaseType_t ResumeAll(void);

TickType_t TaskGetTickCount(void);

TickType_t TaskGetTickCountFromISR(void);

UBaseType_t TaskGetNumberOfTasks(void);

char *TaskGetName(TaskHandle_t TaskToQuery);

BaseType_t TaskGetStaticBuffers(TaskHandle_t Task, StackType_t **pStackBuffer,
                                StaticTask_t **TaskBuffer);

#if (configUSE_IDLE_HOOK == 1)

void ApplicationIdleHook(void);
#endif

#if (configUSE_TICK_HOOK != 0)

void ApplicationTickHook(void);
#endif

void GetIdleTaskMemory(StaticTask_t **IdleTaskTCBBuffer,
                       StackType_t **IdleTaskStackBuffer,
                       configSTACK_DEPTH_TYPE *puxIdleTaskStackSize);

BaseType_t GenericNotify(TaskHandle_t task, UBaseType_t idx, uint32_t value,
                         eNotifyAction action, uint32_t *prevValue);
#define TaskNotify(task, value, action) \
  GenericNotify(task, tskDEFAULT_INDEX_TO_NOTIFY, value, action, NULL)
#define TaskNotifyIndexed(task, idx, value, action) \
  GenericNotify(task, idx, value, action, NULL)

#define TaskNotifyAndQuery(task, value, action, PreviousNotifyValue) \
  GenericNotify(task, tskDEFAULT_INDEX_TO_NOTIFY, value, action,     \
                (PreviousNotifyValue))
#define TaskNotifyAndQueryIndexed(task, idx, value, action, \
                                  PreviousNotifyValue)      \
  GenericNotify(task, idx, value, action, (PreviousNotifyValue))

BaseType_t GenericNotifyFromISR(TaskHandle_t task, UBaseType_t idx,
                                uint32_t value, eNotifyAction action,
                                uint32_t *prevValue, BaseType_t *woken);
#define TaskNotifyFromISR(task, value, action, woken)                         \
  GenericNotifyFromISR(task, tskDEFAULT_INDEX_TO_NOTIFY, value, action, NULL, \
                       woken)
#define TaskNotifyIndexedFromISR(task, idx, value, action, woken) \
  GenericNotifyFromISR(task, idx, value, action, NULL, woken)

#define TaskNotifyAndQueryIndexedFromISR(task, idx, value, action, prevValue, \
                                         woken)                               \
  GenericNotifyFromISR(task, idx, value, action, prevValue, woken)
#define TaskNotifyAndQueryFromISR(task, value, action, prevValue, woken) \
  GenericNotifyFromISR(task, tskDEFAULT_INDEX_TO_NOTIFY, value, action,  \
                       prevValue, woken)

BaseType_t GenericNotifyWait(UBaseType_t idx, uint32_t clearBitsOnEntry,
                             uint32_t clearBitsOnExit,
                             uint32_t *NotificationValue, TickType_t xticks);
#define TaskNotifyWait(clearBitsOnEntry, clearBitsOnExit, NotificationValue, \
                       xticks)                                               \
  GenericNotifyWait(tskDEFAULT_INDEX_TO_NOTIFY, (clearBitsOnEntry),          \
                    (clearBitsOnExit), (NotificationValue), (xticks))
#define TaskNotifyWaitIndexed(idx, clearBitsOnEntry, clearBitsOnExit, \
                              NotificationValue, xticks)              \
  GenericNotifyWait((idx), (clearBitsOnEntry), (clearBitsOnExit),     \
                    (NotificationValue), (xticks))

#define TaskNotifyGivetask \
  GenericNotify(task, tskDEFAULT_INDEX_TO_NOTIFY, (0), eIncrement, NULL)
#define TaskNotifyGiveIndexed(task, idx) \
  GenericNotify(task, idx, (0), eIncrement, NULL)

void GenericNotifyGiveFromISR(TaskHandle_t task, UBaseType_t idx,
                              BaseType_t *woken);
#define TaskNotifyGiveFromISR(task, woken) \
  GenericNotifyGiveFromISR(task, tskDEFAULT_INDEX_TO_NOTIFY, woken)
#define TaskNotifyGiveIndexedFromISR(task, idx, woken) \
  GenericNotifyGiveFromISR(task, idx, woken)

uint32_t ulTaskGenericNotifyTake(UBaseType_t idx, BaseType_t clearOnExit,
                                 TickType_t xticks);
#define ulTaskNotifyTake(clearOnExit, xticks)                              \
  ulTaskGenericNotifyTake(task, tskDEFAULT_INDEX_TO_NOTIFY, (clearOnExit), \
                          (xticks))
#define ulTaskNotifyTakeIndexed(idx, clearOnExit, xticks) \
  ulTaskGenericNotifyTake((idx), (clearOnExit), (xticks))

BaseType_t TaskGenericNotifyStateClear(TaskHandle_t Task, UBaseType_t clearIdx);
#define TaskNotifyStateClear(Task) \
  TaskGenericNotifyStateClear((Task), tskDEFAULT_INDEX_TO_NOTIFY)
#define TaskNotifyStateClearIndexed(Task, clearIdx) \
  TaskGenericNotifyStateClear((Task), (clearIdx))

uint32_t ulTaskGenericNotifyValueClear(TaskHandle_t Task, UBaseType_t clearIdx,
                                       uint32_t clearBits);
#define ulTaskNotifyValueClear(Task, clearBits) \
  ulTaskGenericNotifyValueClear((Task), tskDEFAULT_INDEX_TO_NOTIFY, (clearBits))
#define ulTaskNotifyValueClearIndexed(Task, clearIdx, clearBits) \
  ulTaskGenericNotifyValueClear((Task), (clearIdx), (clearBits))

void TaskSetTimeOutState(TimeOut_t *const TimeOut);

BaseType_t CheckForTimeOut(TimeOut_t *const TimeOut, TickType_t *const ticks);

BaseType_t CatchUpTicks(TickType_t xTicksToCatchUp);

void TaskResetState(void);

#define taskYIELD_WITHIN_API() portYIELD_WITHIN_API()

BaseType_t TaskIncrementTick(void);

void PlaceOnEventList(List_t<TCB_t> *const EventList, const TickType_t xticks);
void PlaceOnUnorderedEventList(List_t<TCB_t> *EventList, const TickType_t Value,
                               const TickType_t xticks);

void PlaceOnEventListRestricted(List_t<TCB_t> *const EventList,
                                TickType_t xticks,
                                const BaseType_t xWaitIndefinitely);

BaseType_t RemoveFromEventList(const List_t<TCB_t> *const EventList);
void RemoveFromUnorderedEventList(Item_t<TCB_t> *EventListItem,
                                  const TickType_t Value);

portDONT_DISCARD void SwitchContext(void);

TickType_t ResetEventItemValue(void);

TaskHandle_t GetCurrentTaskHandle(void);

TaskHandle_t GetCurrentTaskHandleForCore(BaseType_t xCoreID);

void MissedYield(void);

BaseType_t GetSchedulerState(void);

BaseType_t PriorityInherit(TaskHandle_t const pMutexHolder);

BaseType_t PriorityDisinherit(TaskHandle_t const pMutexHolder);

void PriorityDisinheritAfterTimeout(
    TaskHandle_t const pMutexHolder, UBaseType_t uxHighestPriorityWaitingTask);

TaskHandle_t IncrementMutexHeldCount(void);

void SetTimeOutState(TimeOut_t *const TimeOut);
void TaskEnterCritical(void);
void TaskExitCritical(void);
