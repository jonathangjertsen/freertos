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

void TaskDelay(const TickType_t xTicksToDelay);

BaseType_t TaskDelayUntil(TickType_t *const PreviousWakeTime,
                          const TickType_t xTimeIncrement);

BaseType_t TaskAbortDelay(TaskHandle_t Task);

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

BaseType_t TaskResumeAll(void);

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

void ApplicationGetIdlTaskMemory(StaticTask_t **IdlTaskTCBBuffer,
                                 StackType_t **IdlTaskStackBuffer,
                                 configSTACK_DEPTH_TYPE *puxIdlTaskStackSize);

#define TaskList(pcWriteBuffer) \
  TaskListTasks((pcWriteBuffer), configSTATS_BUFFER_MAX_LENGTH)

#define TaskGetRunTimeStats(pcWriteBuffer) \
  TaskGetRunTimeStatistics((pcWriteBuffer), configSTATS_BUFFER_MAX_LENGTH)

BaseType_t TaskGenericNotify(TaskHandle_t TaskToNotify,
                             UBaseType_t uxIndexToNotify, uint32_t ulValue,
                             eNotifyAction eAction,
                             uint32_t *PreviousNotificationValue);
#define TaskNotify(TaskToNotify, ulValue, eAction)                           \
  TaskGenericNotify((TaskToNotify), (tskDEFAULT_INDEX_TO_NOTIFY), (ulValue), \
                    (eAction), NULL)
#define TaskNotifyIndexed(TaskToNotify, uxIndexToNotify, ulValue, eAction)   \
  TaskGenericNotify((TaskToNotify), (uxIndexToNotify), (ulValue), (eAction), \
                    NULL)

#define TaskNotifyAndQuery(TaskToNotify, ulValue, eAction,                   \
                           PreviousNotifyValue)                           \
  TaskGenericNotify((TaskToNotify), (tskDEFAULT_INDEX_TO_NOTIFY), (ulValue), \
                    (eAction), (PreviousNotifyValue))
#define TaskNotifyAndQueryIndexed(TaskToNotify, uxIndexToNotify, ulValue,    \
                                  eAction, PreviousNotifyValue)           \
  TaskGenericNotify((TaskToNotify), (uxIndexToNotify), (ulValue), (eAction), \
                    (PreviousNotifyValue))

BaseType_t TaskGenericNotifyFromISR(TaskHandle_t TaskToNotify,
                                    UBaseType_t uxIndexToNotify,
                                    uint32_t ulValue, eNotifyAction eAction,
                                    uint32_t *PreviousNotificationValue,
                                    BaseType_t *HigherPriorityTaskWoken);
#define TaskNotifyFromISR(TaskToNotify, ulValue, eAction,                \
                          HigherPriorityTaskWoken)                     \
  TaskGenericNotifyFromISR((TaskToNotify), (tskDEFAULT_INDEX_TO_NOTIFY), \
                           (ulValue), (eAction), NULL,                   \
                           (HigherPriorityTaskWoken))
#define TaskNotifyIndexedFromISR(TaskToNotify, uxIndexToNotify, ulValue, \
                                 eAction, HigherPriorityTaskWoken)     \
  TaskGenericNotifyFromISR((TaskToNotify), (uxIndexToNotify), (ulValue), \
                           (eAction), NULL, (HigherPriorityTaskWoken))

#define TaskNotifyAndQueryIndexedFromISR(                                \
    TaskToNotify, uxIndexToNotify, ulValue, eAction,                     \
    PreviousNotificationValue, HigherPriorityTaskWoken)             \
  TaskGenericNotifyFromISR((TaskToNotify), (uxIndexToNotify), (ulValue), \
                           (eAction), (PreviousNotificationValue),    \
                           (HigherPriorityTaskWoken))
#define TaskNotifyAndQueryFromISR(TaskToNotify, ulValue, eAction,         \
                                  PreviousNotificationValue,           \
                                  HigherPriorityTaskWoken)              \
  TaskGenericNotifyFromISR(                                               \
      (TaskToNotify), (tskDEFAULT_INDEX_TO_NOTIFY), (ulValue), (eAction), \
      (PreviousNotificationValue), (HigherPriorityTaskWoken))

BaseType_t TaskGenericNotifyWait(UBaseType_t uxIndexToWaitOn,
                                 uint32_t ulBitsToClearOnEntry,
                                 uint32_t ulBitsToClearOnExit,
                                 uint32_t *NotificationValue,
                                 TickType_t xTicksToWait);
#define TaskNotifyWait(ulBitsToClearOnEntry, ulBitsToClearOnExit,           \
                       NotificationValue, xTicksToWait)                  \
  TaskGenericNotifyWait(tskDEFAULT_INDEX_TO_NOTIFY, (ulBitsToClearOnEntry), \
                        (ulBitsToClearOnExit), (NotificationValue),      \
                        (xTicksToWait))
#define TaskNotifyWaitIndexed(uxIndexToWaitOn, ulBitsToClearOnEntry,     \
                              ulBitsToClearOnExit, NotificationValue, \
                              xTicksToWait)                              \
  TaskGenericNotifyWait((uxIndexToWaitOn), (ulBitsToClearOnEntry),       \
                        (ulBitsToClearOnExit), (NotificationValue),   \
                        (xTicksToWait))

#define TaskNotifyGive(TaskToNotify)                                   \
  TaskGenericNotify((TaskToNotify), (tskDEFAULT_INDEX_TO_NOTIFY), (0), \
                    eIncrement, NULL)
#define TaskNotifyGiveIndexed(TaskToNotify, uxIndexToNotify) \
  TaskGenericNotify((TaskToNotify), (uxIndexToNotify), (0), eIncrement, NULL)

void TaskGenericNotifyGiveFromISR(TaskHandle_t TaskToNotify,
                                  UBaseType_t uxIndexToNotify,
                                  BaseType_t *HigherPriorityTaskWoken);
#define TaskNotifyGiveFromISR(TaskToNotify, HigherPriorityTaskWoken)       \
  TaskGenericNotifyGiveFromISR((TaskToNotify), (tskDEFAULT_INDEX_TO_NOTIFY), \
                               (HigherPriorityTaskWoken))
#define TaskNotifyGiveIndexedFromISR(TaskToNotify, uxIndexToNotify, \
                                     HigherPriorityTaskWoken)     \
  TaskGenericNotifyGiveFromISR((TaskToNotify), (uxIndexToNotify),   \
                               (HigherPriorityTaskWoken))

uint32_t ulTaskGenericNotifyTake(UBaseType_t uxIndexToWaitOn,
                                 BaseType_t xClearCountOnExit,
                                 TickType_t xTicksToWait);
#define ulTaskNotifyTake(xClearCountOnExit, xTicksToWait)                    \
  ulTaskGenericNotifyTake((tskDEFAULT_INDEX_TO_NOTIFY), (xClearCountOnExit), \
                          (xTicksToWait))
#define ulTaskNotifyTakeIndexed(uxIndexToWaitOn, xClearCountOnExit, \
                                xTicksToWait)                       \
  ulTaskGenericNotifyTake((uxIndexToWaitOn), (xClearCountOnExit),   \
                          (xTicksToWait))

BaseType_t TaskGenericNotifyStateClear(TaskHandle_t Task,
                                       UBaseType_t uxIndexToClear);
#define TaskNotifyStateClear(Task) \
  TaskGenericNotifyStateClear((Task), (tskDEFAULT_INDEX_TO_NOTIFY))
#define TaskNotifyStateClearIndexed(Task, uxIndexToClear) \
  TaskGenericNotifyStateClear((Task), (uxIndexToClear))

uint32_t ulTaskGenericNotifyValueClear(TaskHandle_t Task,
                                       UBaseType_t uxIndexToClear,
                                       uint32_t ulBitsToClear);
#define ulTaskNotifyValueClear(Task, ulBitsToClear)                   \
  ulTaskGenericNotifyValueClear((Task), (tskDEFAULT_INDEX_TO_NOTIFY), \
                                (ulBitsToClear))
#define ulTaskNotifyValueClearIndexed(Task, uxIndexToClear, ulBitsToClear) \
  ulTaskGenericNotifyValueClear((Task), (uxIndexToClear), (ulBitsToClear))

void TaskSetTimeOutState(TimeOut_t *const TimeOut);

BaseType_t TaskCheckForTimeOut(TimeOut_t *const TimeOut,
                               TickType_t *const TicksToWait);

BaseType_t TaskCatchUpTicks(TickType_t xTicksToCatchUp);

void TaskResetState(void);

#if (configNUMBER_OF_CORES == 1)
#define taskYIELD_WITHIN_API() portYIELD_WITHIN_API()
#else
#define taskYIELD_WITHIN_API() TaskYieldWithinAPI()
#endif

BaseType_t TaskIncrementTick(void);

void TaskPlaceOnEventList(List_t<TCB_t> *const EventList,
                          const TickType_t xTicksToWait);
void TaskPlaceOnUnorderedEventList(List_t<TCB_t> *EventList,
                                   const TickType_t Value,
                                   const TickType_t xTicksToWait);

void TaskPlaceOnEventListRestricted(List_t<TCB_t> *const EventList,
                                    TickType_t xTicksToWait,
                                    const BaseType_t xWaitIndefinitely);

BaseType_t TaskRemoveFromEventList(const List_t<TCB_t> *const EventList);
void TaskRemoveFromUnorderedEventList(Item_t<TCB_t> *EventListItem,
                                      const TickType_t Value);

portDONT_DISCARD void TaskSwitchContext(void);

TickType_t TaskResetEventItemValue(void);

TaskHandle_t TaskGetCurrentTaskHandle(void);

TaskHandle_t TaskGetCurrentTaskHandleForCore(BaseType_t xCoreID);

void TaskMissedYield(void);

BaseType_t TaskGetSchedulerState(void);

BaseType_t TaskPriorityInherit(TaskHandle_t const pMutexHolder);

BaseType_t TaskPriorityDisinherit(TaskHandle_t const pMutexHolder);

void TaskPriorityDisinheritAfterTimeout(
    TaskHandle_t const pMutexHolder, UBaseType_t uxHighestPriorityWaitingTask);

TaskHandle_t TaskIncrementMutexHeldCount(void);

void TaskInternalSetTimeOutState(TimeOut_t *const TimeOut);
void TaskEnterCritical(void);
void TaskExitCritical(void);
