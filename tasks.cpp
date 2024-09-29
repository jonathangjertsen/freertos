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

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.hpp"
#include "timers.h"

#define taskYIELD_ANY_CORE_IF_USING_PREEMPTION(TCB) \
  do {                                              \
    if (CurrentTCB->Priority < (TCB)->Priority) {   \
      portYIELD_WITHIN_API();                       \
    }                                               \
  } while (0)

#define taskNOT_WAITING_NOTIFICATION ((uint8_t)0)
#define taskWAITING_NOTIFICATION ((uint8_t)1)
#define taskNOTIFICATION_RECEIVED ((uint8_t)2)

#define tskSTACK_FILL_BYTE (0xa5U)

#define tskDYNAMICALLY_ALLOCATED_STACK_AND_TCB ((uint8_t)0)
#define tskSTATICALLY_ALLOCATED_STACK_ONLY ((uint8_t)1)
#define tskSTATICALLY_ALLOCATED_STACK_AND_TCB ((uint8_t)2)

#define tskSET_NEW_STACKS_TO_KNOWN_VALUE 0

#ifndef configIDLE_TASK_NAME
#define configIDLE_TASK_NAME "IDLE"
#endif

#define RECORD_READY_PRIORITY(Priority) \
  portRECORD_READY_PRIORITY((Priority), TopReadyPriority)

#define taskRESET_READY_PRIORITY(Priority)                      \
  do {                                                          \
    if (ReadyTasks[(Priority)].Length == (UBaseType_t)0) {      \
      portRESET_READY_PRIORITY((Priority), (TopReadyPriority)); \
    }                                                           \
  } while (0)

#define AddTaskToReadyList(TCB)                              \
  do {                                                       \
    RECORD_READY_PRIORITY((TCB)->Priority);                  \
    ReadyTasks[TCB->Priority].append(&(TCB)->StateListItem); \
  } while (0)

#define GetTCBFromHandle(Handle) (((Handle) == NULL) ? CurrentTCB : (Handle))

#if (configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_16_BITS)
#define taskEVENT_LIST_ITEM_VALUE_IN_USE ((uint16_t)0x8000U)
#elif (configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_32_BITS)
#define taskEVENT_LIST_ITEM_VALUE_IN_USE ((uint32_t)0x80000000U)
#elif (configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_64_BITS)
#define taskEVENT_LIST_ITEM_VALUE_IN_USE ((uint64_t)0x8000000000000000U)
#endif

#define taskTASK_NOT_RUNNING ((BaseType_t)(-1))

#define taskTASK_SCHEDULED_TO_YIELD ((BaseType_t)(-2))

#define taskTASK_IS_RUNNING(TCB) (((TCB) == CurrentTCB) ? (true) : (false))
#define taskTASK_IS_RUNNING_OR_SCHEDULED_TO_YIELD(TCB) \
  (((TCB) == CurrentTCB) ? (true) : (false))

#define taskATTRIBUTE_IS_IDLE (UBaseType_t)(1U << 0U)
#define taskBITS_PER_BYTE ((size_t)8)

struct TCB_t;
TCB_t *volatile CurrentTCB = nullptr;

static List_t<TCB_t> ReadyTasks[configMAX_PRIORITIES];
static List_t<TCB_t> DelayedTasks1;
static List_t<TCB_t> DelayedTasks2;
static List_t<TCB_t> *volatile DelayedTasks;
static List_t<TCB_t> *volatile OverflowDelayed;
static List_t<TCB_t> PendingReady;
static List_t<TCB_t> TasksWaitingTermination;
static volatile UBaseType_t DeletedTasksWaitingCleanUp = 0U;
static List_t<TCB_t> SuspendedTasks;

static volatile UBaseType_t CurrentNumberOfTasks = 0U;
static volatile TickType_t TickCount = (TickType_t)configINITIAL_TICK_COUNT;
static volatile UBaseType_t TopReadyPriority = tskIDLE_PRIORITY;
static volatile BaseType_t SchedulerRunning = false;
static volatile TickType_t PendedTicks = 0U;
static volatile BaseType_t YieldPendings[configNUMBER_OF_CORES] = {false};
static volatile BaseType_t NOverflows = 0;
static UBaseType_t TaskNumber = 0U;
static volatile TickType_t NextTaskUnblockTime = 0U;
static TaskHandle_t IdleTasks[configNUMBER_OF_CORES];

static const volatile UBaseType_t TopUsedPriority = configMAX_PRIORITIES - 1U;

static volatile bool SchedulerSuspended = false;
struct TCB_t {
  volatile StackType_t *StackTop;
  Item_t<TCB_t> StateListItem;
  Item_t<TCB_t> EventListItem;
  UBaseType_t Priority;
  StackType_t *Stack;
  char Name[configMAX_TASK_NAME_LEN];
  UBaseType_t CriticalNesting;
  UBaseType_t BasePriority;
  UBaseType_t MutexesHeld;
  volatile uint32_t NotifiedValue[configTASK_NOTIFICATION_ARRAY_ENTRIES];
  volatile uint8_t NotifyState[configTASK_NOTIFICATION_ARRAY_ENTRIES];
  uint8_t StaticallyAllocated;
  uint8_t DelayAborted;

  bool Suspended() const {
    if (StateListItem.Container != &SuspendedTasks) {
      return false;
    }
    if (EventListItem.Container == &PendingReady) {
      return false;
    }
    if (EventListItem.Container == nullptr) {
      for (int x = 0; x < configTASK_NOTIFICATION_ARRAY_ENTRIES; x++) {
        if (NotifyState[x] == taskWAITING_NOTIFICATION) {
          return false;
        }
      }
      return true;
    }
    return false;
  }
};

static void ResetNextTaskUnblockTime();
static inline void taskSWITCH_DELAYED_LISTS() {
  List_t<TCB_t> *Temp;
  configASSERT(DelayedTasks->empty());
  Temp = DelayedTasks;
  DelayedTasks = OverflowDelayed;
  OverflowDelayed = Temp;
  NOverflows++;
  ResetNextTaskUnblockTime();
}

static inline void taskSELECT_HIGHEST_PRIORITY_TASK() {
  UBaseType_t uxTopPriority;
  portGET_HIGHEST_PRIORITY(uxTopPriority, TopReadyPriority);
  configASSERT(ReadyTasks[uxTopPriority].Length > 0);
  CurrentTCB = ReadyTasks[uxTopPriority].advance()->Owner;
}

static BaseType_t CreateIdleTasks(void);
static void InitialiseTaskLists(void);
static portTASK_FUNCTION_PROTO(IdleTask, Parameters);

static void DeleteTCB(TCB_t *TCB);

static void CheckTasksWaitingTermination(void);

static void AddCurrentTaskToDelayedList(TickType_t TicksToWait,
                                        const BaseType_t CanBlockIndefinitely);

static void ResetNextTaskUnblockTime(void);

static void InitialiseNewTask(TaskFunction_t TaskCode, const char *const Name,
                              const configSTACK_DEPTH_TYPE StackDepth,
                              void *const Parameters, UBaseType_t Priority,
                              TaskHandle_t *const CreatedTask, TCB_t *NewTCB,
                              const MemoryRegion_t *const xRegions);

static void AddNewTaskToReadyList(TCB_t *NewTCB);

static TCB_t *CreateStaticTask(TaskFunction_t TaskCode, const char *const Name,
                               const configSTACK_DEPTH_TYPE StackDepth,
                               void *const Parameters, UBaseType_t Priority,
                               StackType_t *const StackBuffer,
                               StaticTask_t *const TaskBuffer,
                               TaskHandle_t *const CreatedTask);

static TCB_t *CreateTask(TaskFunction_t TaskCode, const char *const Name,
                         const configSTACK_DEPTH_TYPE StackDepth,
                         void *const Parameters, UBaseType_t Priority,
                         TaskHandle_t *const CreatedTask);

static TCB_t *CreateStaticTask(TaskFunction_t TaskCode, const char *const Name,
                               const configSTACK_DEPTH_TYPE StackDepth,
                               void *const Parameters, UBaseType_t Priority,
                               StackType_t *const StackBuffer,
                               StaticTask_t *const TaskBuffer,
                               TaskHandle_t *const CreatedTask) {
  TCB_t *NewTCB;
  configASSERT(StackBuffer != NULL);
  configASSERT(TaskBuffer != NULL);
  configASSERT(sizeof(StaticTask_t) == sizeof(TCB_t));
  if ((TaskBuffer != NULL) && (StackBuffer != NULL)) {
    NewTCB = (TCB_t *)TaskBuffer;
    (void)memset((void *)NewTCB, 0x00, sizeof(TCB_t));
    NewTCB->Stack = (StackType_t *)StackBuffer;
    { NewTCB->StaticallyAllocated = tskSTATICALLY_ALLOCATED_STACK_AND_TCB; }
    InitialiseNewTask(TaskCode, Name, StackDepth, Parameters, Priority,
                      CreatedTask, NewTCB, NULL);
  } else {
    NewTCB = NULL;
  }
  return NewTCB;
}

TaskHandle_t TaskCreateStatic(TaskFunction_t TaskCode, const char *const Name,
                              const configSTACK_DEPTH_TYPE StackDepth,
                              void *const Parameters, UBaseType_t Priority,
                              StackType_t *const StackBuffer,
                              StaticTask_t *const TaskBuffer) {
  TaskHandle_t Ret = NULL;
  TCB_t *NewTCB;
  NewTCB = CreateStaticTask(TaskCode, Name, StackDepth, Parameters, Priority,
                            StackBuffer, TaskBuffer, &Ret);
  if (NewTCB != NULL) {
    AddNewTaskToReadyList(NewTCB);
  }
  return Ret;
}

static TCB_t *CreateTask(TaskFunction_t TaskCode, const char *const Name,
                         const configSTACK_DEPTH_TYPE StackDepth,
                         void *const Parameters, UBaseType_t Priority,
                         TaskHandle_t *const CreatedTask) {
  TCB_t *NewTCB;

  {
    StackType_t *Stack = (StackType_t *)pvPortMallocStack(
        (((size_t)StackDepth) * sizeof(StackType_t)));
    if (Stack != NULL) {
      NewTCB = (TCB_t *)pvPortMalloc(sizeof(TCB_t));
      if (NewTCB != NULL) {
        (void)memset((void *)NewTCB, 0x00, sizeof(TCB_t));
        NewTCB->Stack = Stack;
      } else {
        vPortFreeStack(Stack);
      }
    } else {
      NewTCB = NULL;
    }
  }
  if (NewTCB != NULL) {
    {
      NewTCB->StaticallyAllocated = tskDYNAMICALLY_ALLOCATED_STACK_AND_TCB;
    }
    InitialiseNewTask(TaskCode, Name, StackDepth, Parameters, Priority,
                      CreatedTask, NewTCB, NULL);
  }
  return NewTCB;
}

BaseType_t TaskCreate(TaskFunction_t TaskCode, const char *const Name,
                      const configSTACK_DEPTH_TYPE StackDepth,
                      void *const Parameters, UBaseType_t Priority,
                      TaskHandle_t *const CreatedTask) {
  TCB_t *NewTCB =
      CreateTask(TaskCode, Name, StackDepth, Parameters, Priority, CreatedTask);
  if (NewTCB != NULL) {
    AddNewTaskToReadyList(NewTCB);
    return true;
  }
  return errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
}

static void InitialiseNewTask(TaskFunction_t TaskCode, const char *const Name,
                              const configSTACK_DEPTH_TYPE StackDepth,
                              void *const Parameters, UBaseType_t Priority,
                              TaskHandle_t *const CreatedTask, TCB_t *NewTCB,
                              const MemoryRegion_t *const xRegions) {
  StackType_t *StackTop;
  UBaseType_t x;

#if (tskSET_NEW_STACKS_TO_KNOWN_VALUE == 1)
  {
    (void)memset(NewTCB->Stack, (int)tskSTACK_FILL_BYTE,
                 (size_t)StackDepth * sizeof(StackType_t));
  }
#endif

#if (portSTACK_GROWTH < 0)
  {
    StackTop = &(NewTCB->Stack[StackDepth - (configSTACK_DEPTH_TYPE)1]);
    StackTop =
        (StackType_t *)(((portPOINTER_SIZE_TYPE)StackTop) &
                        (~((portPOINTER_SIZE_TYPE)portBYTE_ALIGNMENT_MASK)));

    configASSERT((((portPOINTER_SIZE_TYPE)StackTop &
                   (portPOINTER_SIZE_TYPE)portBYTE_ALIGNMENT_MASK) == 0U));
#if (configRECORD_STACK_HIGH_ADDRESS == 1)
    { NewTCB->EndOfStack = StackTop; }
#endif
  }
#else
  {
    StackTop = NewTCB->Stack;
    StackTop =
        (StackType_t *)((((portPOINTER_SIZE_TYPE)StackTop) +
                         portBYTE_ALIGNMENT_MASK) &
                        (~((portPOINTER_SIZE_TYPE)portBYTE_ALIGNMENT_MASK)));

    configASSERT((((portPOINTER_SIZE_TYPE)StackTop &
                   (portPOINTER_SIZE_TYPE)portBYTE_ALIGNMENT_MASK) == 0U));

    NewTCB->EndOfStack =
        NewTCB->Stack + (StackDepth - (configSTACK_DEPTH_TYPE)1);
  }
#endif

  if (Name != NULL) {
    for (x = (UBaseType_t)0; x < (UBaseType_t)configMAX_TASK_NAME_LEN; x++) {
      NewTCB->Name[x] = Name[x];

      if (Name[x] == (char)0x00) {
        break;
      }
    }

    NewTCB->Name[configMAX_TASK_NAME_LEN - 1U] = '\0';
  }

  configASSERT(Priority < configMAX_PRIORITIES);
  if (Priority >= (UBaseType_t)configMAX_PRIORITIES) {
    Priority = (UBaseType_t)configMAX_PRIORITIES - (UBaseType_t)1U;
  }
  NewTCB->Priority = Priority;
#if (configUSE_MUTEXES == 1)
  { NewTCB->BasePriority = Priority; }
#endif
  NewTCB->StateListItem.init();
  NewTCB->EventListItem.init();
  NewTCB->StateListItem.Owner = NewTCB;
  NewTCB->EventListItem.Value =
      (TickType_t)configMAX_PRIORITIES - (TickType_t)Priority;
  NewTCB->EventListItem.Owner = NewTCB;
  (void)xRegions;
  NewTCB->StackTop = PortInitialiseStack(StackTop, TaskCode, Parameters);
  if (CreatedTask != NULL) {
    *CreatedTask = (TaskHandle_t)NewTCB;
  }
}

static void AddNewTaskToReadyList(TCB_t *NewTCB) {
  ENTER_CRITICAL();
  {
    CurrentNumberOfTasks = (UBaseType_t)(CurrentNumberOfTasks + 1U);
    if (CurrentTCB == NULL) {
      CurrentTCB = NewTCB;
      if (CurrentNumberOfTasks == (UBaseType_t)1) {
        InitialiseTaskLists();
      }
    } else {
      if (SchedulerRunning == false) {
        if (CurrentTCB->Priority <= NewTCB->Priority) {
          CurrentTCB = NewTCB;
        }
      }
    }
    TaskNumber++;
    AddTaskToReadyList(NewTCB);
    portSETUP_TCB(NewTCB);
  }
  EXIT_CRITICAL();
  if (SchedulerRunning) {
    taskYIELD_ANY_CORE_IF_USING_PREEMPTION(NewTCB);
  }
}

void TaskDelete(TaskHandle_t TaskToDelete) {
  TCB_t *TCB;
  BaseType_t xDeleteTCBInIdleTask = false;
  BaseType_t TaskIsRunningOrYielding;
  ENTER_CRITICAL();
  {
    TCB = GetTCBFromHandle(TaskToDelete);
    if (TCB->StateListItem.remove() == 0) {
      taskRESET_READY_PRIORITY(TCB->Priority);
    }
    TCB->EventListItem.ensureRemoved();
    TaskNumber++;
    TaskIsRunningOrYielding = taskTASK_IS_RUNNING_OR_SCHEDULED_TO_YIELD(TCB);
    if ((SchedulerRunning) && (TaskIsRunningOrYielding)) {
      TasksWaitingTermination.append(&TCB->StateListItem);
      ++DeletedTasksWaitingCleanUp;
      xDeleteTCBInIdleTask = true;
      portPRE_TASK_DELETE_HOOK(TCB, &(YieldPendings[0]));
    } else {
      --CurrentNumberOfTasks;
      ResetNextTaskUnblockTime();
    }
  }
  EXIT_CRITICAL();
  if (xDeleteTCBInIdleTask != true) {
    DeleteTCB(TCB);
  }
  if (SchedulerRunning) {
    if (TCB == CurrentTCB) {
      configASSERT(SchedulerSuspended == 0);
      taskYIELD_WITHIN_API();
    }
  }
}

BaseType_t DelayUntil(TickType_t *const PreviousWakeTime,
                      const TickType_t xTimeIncrement) {
  TickType_t TimeToWake;
  BaseType_t xAlreadyYielded, xShouldDelay = false;
  configASSERT(PreviousWakeTime);
  configASSERT((xTimeIncrement > 0U));
  TaskSuspendAll();
  {
    const TickType_t ConstTickCount = TickCount;
    configASSERT(SchedulerSuspended == 1U);

    TimeToWake = *PreviousWakeTime + xTimeIncrement;
    if (ConstTickCount < *PreviousWakeTime) {
      if ((TimeToWake < *PreviousWakeTime) && (TimeToWake > ConstTickCount)) {
        xShouldDelay = true;
      }
    } else {
      if ((TimeToWake < *PreviousWakeTime) || (TimeToWake > ConstTickCount)) {
        xShouldDelay = true;
      }
    }

    *PreviousWakeTime = TimeToWake;
    if (xShouldDelay) {
      AddCurrentTaskToDelayedList(TimeToWake - ConstTickCount, false);
    }
  }
  if (!ResumeAll()) {
    taskYIELD_WITHIN_API();
  }
  return xShouldDelay;
}

void Delay(const TickType_t xTicksToDelay) {
  if (xTicksToDelay > 0U) {
    TaskSuspendAll();
    configASSERT(SchedulerSuspended);
    AddCurrentTaskToDelayedList(xTicksToDelay, false);
    if (!ResumeAll()) {
      taskYIELD_WITHIN_API();
    }
  }
}

TaskState TaskGetState(TaskHandle_t Task) {
  TaskState eReturn;
  List_t<TCB_t> *StateList;
  List_t<TCB_t> *EventList;
  List_t<TCB_t> *DelayedList;
  List_t<TCB_t> *OverflowedDelayedList;
  TCB_t *TCB = Task;
  configASSERT(TCB);
  if (TCB == CurrentTCB) {
    eReturn = eRunning;
  } else {
    ENTER_CRITICAL();
    {
      StateList = TCB->StateListItem.Container;
      EventList = TCB->EventListItem.Container;
      DelayedList = DelayedTasks;
      OverflowedDelayedList = OverflowDelayed;
    }
    EXIT_CRITICAL();
    if (EventList == &PendingReady) {
      eReturn = eReady;
    } else if ((StateList == DelayedList) ||
               (StateList == OverflowedDelayedList)) {
      eReturn = eBlocked;
    } else if (StateList == &SuspendedTasks) {
      if (TCB->EventListItem.Container == NULL) {
        BaseType_t x;
        eReturn = eSuspended;
        for (x = 0; x < (BaseType_t)configTASK_NOTIFICATION_ARRAY_ENTRIES;
             x++) {
          if (TCB->NotifyState[x] == taskWAITING_NOTIFICATION) {
            eReturn = eBlocked;
            break;
          }
        }
      } else {
        eReturn = eBlocked;
      }
    } else if ((StateList == &TasksWaitingTermination) || (StateList == NULL)) {
      eReturn = eDeleted;
    } else {
      { eReturn = eReady; }
    }
  }
  return eReturn;
}

UBaseType_t TaskPriorityGet(const TaskHandle_t Task) {
  TCB_t const *TCB;
  UBaseType_t uRet;
  ENTER_CRITICAL();
  {
    TCB = GetTCBFromHandle(Task);
    uRet = TCB->Priority;
  }
  EXIT_CRITICAL();
  return uRet;
}

UBaseType_t TaskPriorityGetFromISR(const TaskHandle_t Task) {
  TCB_t const *TCB;
  UBaseType_t uRet;
  UBaseType_t uxSavedInterruptStatus;
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  uxSavedInterruptStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
    TCB = GetTCBFromHandle(Task);
    uRet = TCB->Priority;
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return uRet;
}

UBaseType_t TaskBasePriorityGet(const TaskHandle_t Task) {
  TCB_t const *TCB;
  UBaseType_t uRet;
  ENTER_CRITICAL();
  {
    TCB = GetTCBFromHandle(Task);
    uRet = TCB->BasePriority;
  }
  EXIT_CRITICAL();
  return uRet;
}

UBaseType_t TaskBasePriorityGetFromISR(const TaskHandle_t Task) {
  TCB_t const *TCB;
  UBaseType_t uRet;
  UBaseType_t uxSavedInterruptStatus;
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  uxSavedInterruptStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
    TCB = GetTCBFromHandle(Task);
    uRet = TCB->BasePriority;
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return uRet;
}

void TaskPrioritySet(TaskHandle_t Task, UBaseType_t uxNewPriority) {
  TCB_t *TCB;
  UBaseType_t uxCurrentBasePriority, PriorityUsedOnEntry;
  BaseType_t xYieldRequired = false;
  configASSERT(uxNewPriority < configMAX_PRIORITIES);

  if (uxNewPriority >= (UBaseType_t)configMAX_PRIORITIES) {
    uxNewPriority = (UBaseType_t)configMAX_PRIORITIES - (UBaseType_t)1U;
  }

  ENTER_CRITICAL();
  {
    TCB = GetTCBFromHandle(Task);
    uxCurrentBasePriority = TCB->BasePriority;
    if (uxCurrentBasePriority != uxNewPriority) {
      if (uxNewPriority > uxCurrentBasePriority) {
        if (TCB != CurrentTCB) {
          if (uxNewPriority > CurrentTCB->Priority) {
            xYieldRequired = true;
          }
        } else {
        }
      } else if (taskTASK_IS_RUNNING(TCB)) {
        xYieldRequired = true;
      }
      PriorityUsedOnEntry = TCB->Priority;
      if ((TCB->BasePriority == TCB->Priority) ||
          (uxNewPriority > TCB->Priority)) {
        TCB->Priority = uxNewPriority;
      }
      TCB->BasePriority = uxNewPriority;
      if ((TCB->EventListItem.Value & taskEVENT_LIST_ITEM_VALUE_IN_USE) == 0U) {
        TCB->EventListItem.Value =
            (TickType_t)configMAX_PRIORITIES - (TickType_t)uxNewPriority;
      }
      if (TCB->StateListItem.Container == &ReadyTasks[PriorityUsedOnEntry]) {
        if (TCB->StateListItem.remove() == 0) {
          portRESET_READY_PRIORITY(PriorityUsedOnEntry, TopReadyPriority);
        }
        AddTaskToReadyList(TCB);
      }
      if (xYieldRequired) {
        portYIELD_WITHIN_API();
      }
      (void)PriorityUsedOnEntry;
    }
  }
  EXIT_CRITICAL();
}

void TaskSuspend(TaskHandle_t TaskToSuspend) {
  TCB_t *TCB;
  ENTER_CRITICAL();
  {
    TCB = GetTCBFromHandle(TaskToSuspend);
    if (TCB->StateListItem.remove() == 0) {
      taskRESET_READY_PRIORITY(TCB->Priority);
    }
    TCB->EventListItem.ensureRemoved();

    SuspendedTasks.append(&TCB->StateListItem);
    BaseType_t x;
    for (x = 0; x < (BaseType_t)configTASK_NOTIFICATION_ARRAY_ENTRIES; x++) {
      if (TCB->NotifyState[x] == taskWAITING_NOTIFICATION) {
        TCB->NotifyState[x] = taskNOT_WAITING_NOTIFICATION;
      }
    }
  }
  EXIT_CRITICAL();
  {
    UBaseType_t uxCurrentListLength;
    if (SchedulerRunning) {
      ENTER_CRITICAL();
      { ResetNextTaskUnblockTime(); }
      EXIT_CRITICAL();
    }

    if (TCB == CurrentTCB) {
      if (SchedulerRunning) {
        configASSERT(SchedulerSuspended == 0);
        portYIELD_WITHIN_API();
      } else {
        uxCurrentListLength = SuspendedTasks.Length;
        if (uxCurrentListLength == CurrentNumberOfTasks) {
          CurrentTCB = NULL;
        } else {
          SwitchContext();
        }
      }
    }
  }
}

void TaskResume(TaskHandle_t TaskToResume) {
  TCB_t *const TCB = TaskToResume;

  configASSERT(TaskToResume);

  if ((TCB != CurrentTCB) && (TCB != NULL)) {
    CriticalSection s;
    if (TCB->Suspended()) {
      TCB->StateListItem.remove();
      AddTaskToReadyList(TCB);
      taskYIELD_ANY_CORE_IF_USING_PREEMPTION(TCB);
    }
  }
}

BaseType_t TaskResumeFromISR(TaskHandle_t TaskToResume) {
  BaseType_t xYieldRequired = false;
  TCB_t *const TCB = TaskToResume;
  UBaseType_t uxSavedInterruptStatus;
  configASSERT(TaskToResume);
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  uxSavedInterruptStatus = ENTER_CRITICAL_FROM_ISR();
  {
    if (TCB->Suspended()) {
      if (SchedulerSuspended == 0U) {
        if (TCB->Priority > CurrentTCB->Priority) {
          xYieldRequired = true;
          YieldPendings[0] = true;
        }
        TCB->StateListItem.remove();
        AddTaskToReadyList(TCB);
      } else {
        PendingReady.append(&TCB->EventListItem);
      }
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return xYieldRequired;
}

static BaseType_t CreateIdleTasks(void) {
  BaseType_t Ret = true;
  BaseType_t xCoreID;
  char cIdleName[configMAX_TASK_NAME_LEN];
  TaskFunction_t IdleTaskFunction = NULL;
  BaseType_t xIdleTaskNameIndex;
  for (xIdleTaskNameIndex = 0;
       xIdleTaskNameIndex < (BaseType_t)configMAX_TASK_NAME_LEN;
       xIdleTaskNameIndex++) {
    cIdleName[xIdleTaskNameIndex] = configIDLE_TASK_NAME[xIdleTaskNameIndex];

    if (cIdleName[xIdleTaskNameIndex] == (char)0x00) {
      break;
    }
  }

  for (xCoreID = 0; xCoreID < (BaseType_t)configNUMBER_OF_CORES; xCoreID++) {
    IdleTaskFunction = IdleTask;
    StaticTask_t *pIdleTaskTCBBuffer = NULL;
    StackType_t *IdleTaskStackBuffer = NULL;
    configSTACK_DEPTH_TYPE IdleTaskStackSize;

    GetIdleTaskMemory(&pIdleTaskTCBBuffer, &IdleTaskStackBuffer,
                      &IdleTaskStackSize);
    IdleTasks[xCoreID] = TaskCreateStatic(
        IdleTaskFunction, cIdleName, IdleTaskStackSize, (void *)NULL,
        portPRIVILEGE_BIT, IdleTaskStackBuffer, pIdleTaskTCBBuffer);
    if (IdleTasks[xCoreID] != NULL) {
      Ret = true;
    } else {
      Ret = false;
    }

    if (Ret == false) {
      break;
    }
  }
  return Ret;
}

void TaskStartScheduler(void) {
  BaseType_t Ret;
  Ret = CreateIdleTasks();
  if (Ret) {
    Ret = TimerCreateTimerTask();
  }
  if (Ret) {

    portDISABLE_INTERRUPTS();

    NextTaskUnblockTime = portMAX_DELAY;
    SchedulerRunning = true;
    TickCount = (TickType_t)configINITIAL_TICK_COUNT;

    portCONFIGURE_TIMER_FOR_RUN_TIME_STATS();

    (void)xPortStartScheduler();

  } else {
    configASSERT(Ret != errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY);
  }

  (void)IdleTasks;

  (void)TopUsedPriority;
}

void TaskEndScheduler(void) {
  {
    BaseType_t xCoreID;
    { TaskDelete(TimerGetTimerDaemonTaskHandle()); }

    for (xCoreID = 0; xCoreID < (BaseType_t)configNUMBER_OF_CORES; xCoreID++) {
      TaskDelete(IdleTasks[xCoreID]);
    }

    CheckTasksWaitingTermination();
  }

  portDISABLE_INTERRUPTS();
  SchedulerRunning = false;

  vPortEndScheduler();
}

void TaskSuspendAll(void) {
    portSOFTWARE_BARRIER();
    SchedulerSuspended = (UBaseType_t)(SchedulerSuspended + 1U);
    portMEMORY_BARRIER();
}

BaseType_t ResumeAll(void) {
  TCB_t *TCB = NULL;
  BaseType_t xAlreadyYielded = false;
  {

    ENTER_CRITICAL();
    {
      BaseType_t xCoreID;
      xCoreID = (BaseType_t)portGET_CORE_ID();

      configASSERT(SchedulerSuspended != 0U);
      SchedulerSuspended = (UBaseType_t)(SchedulerSuspended - 1U);
      portRELEASE_TASK_LOCK();
      if (SchedulerSuspended == 0U) {
        if (CurrentNumberOfTasks > 0U) {
          while (PendingReady.Length > 0) {
            TCB = PendingReady.head()->Owner;
            TCB->EventListItem.remove();
            portMEMORY_BARRIER();
            TCB->StateListItem.remove();
            AddTaskToReadyList(TCB);
            {
              if (TCB->Priority > CurrentTCB->Priority) {
                YieldPendings[xCoreID] = true;
              }
            }
          }
          if (TCB != NULL) {
            ResetNextTaskUnblockTime();
          }

          {
            TickType_t xPendedCounts = PendedTicks;
            if (xPendedCounts > 0U) {
              do {
                if (TaskIncrementTick()) {
                  YieldPendings[xCoreID] = true;
                }
                --xPendedCounts;
              } while (xPendedCounts > 0U);
              PendedTicks = 0;
            }
          }
          if (YieldPendings[xCoreID]) {
            { xAlreadyYielded = true; }
            portYIELD_WITHIN_API();
          }
        }
      }
    }
    EXIT_CRITICAL();
  }
  return xAlreadyYielded;
}

TickType_t TaskGetTickCount(void) {
  portTICK_TYPE_ENTER_CRITICAL();
  TickType_t xTicks = TickCount;
  portTICK_TYPE_EXIT_CRITICAL();
  return xTicks;
}

TickType_t TaskGetTickCountFromISR(void) {
  TickType_t Ret;
  UBaseType_t uxSavedInterruptStatus;
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  uxSavedInterruptStatus = portTICK_TYPE_SET_INTERRUPT_MASK_FROM_ISR();
  { Ret = TickCount; }
  portTICK_TYPE_CLEAR_INTERRUPT_MASK_FROM_ISR(uxSavedInterruptStatus);
  return Ret;
}

UBaseType_t TaskGetNumberOfTasks(void) { return CurrentNumberOfTasks; }

char *TaskGetName(TaskHandle_t TaskToQuery) {
  TCB_t *TCB;
  TCB = GetTCBFromHandle(TaskToQuery);
  configASSERT(TCB);
  return &(TCB->Name[0]);
}

BaseType_t TaskGetStaticBuffers(TaskHandle_t Task, StackType_t **pStackBuffer,
                                StaticTask_t **TaskBuffer) {
  TCB_t *TCB;
  configASSERT(pStackBuffer != NULL);
  configASSERT(TaskBuffer != NULL);
  TCB = GetTCBFromHandle(Task);
  if (TCB->StaticallyAllocated == tskSTATICALLY_ALLOCATED_STACK_AND_TCB) {
    *pStackBuffer = TCB->Stack;
    *TaskBuffer = (StaticTask_t *)TCB;
    return true;
  }
  if (TCB->StaticallyAllocated == tskSTATICALLY_ALLOCATED_STACK_ONLY) {
    *pStackBuffer = TCB->Stack;
    *TaskBuffer = NULL;
    return true;
  }
  return true;
}
TaskHandle_t TaskGetIdleTaskHandle(void) {
  configASSERT((IdleTasks[0] != NULL));
  return IdleTasks[0];
}
TaskHandle_t TaskGetIdleTaskHandleForCore(BaseType_t xCoreID) {
  configASSERT(taskVALID_CORE_ID(xCoreID));

  configASSERT((IdleTasks[xCoreID] != NULL));
  return IdleTasks[xCoreID];
}

BaseType_t CatchUpTicks(TickType_t xTicksToCatchUp) {
  BaseType_t xYieldOccurred;
  configASSERT(SchedulerSuspended == 0U);
  TaskSuspendAll();
  ENTER_CRITICAL();
  PendedTicks += xTicksToCatchUp;
  EXIT_CRITICAL();
  return ResumeAll();
}

BaseType_t AbortDelay(TaskHandle_t Task) {
  TCB_t *TCB = Task;
  BaseType_t Ret;
  configASSERT(TCB);
  TaskSuspendAll();
  {
    if (TaskGetState(Task) == eBlocked) {
      Ret = true;
      TCB->StateListItem.remove();
      ENTER_CRITICAL();
      {
        if (TCB->EventListItem.Container != NULL) {
          TCB->EventListItem.remove();
          TCB->DelayAborted = (uint8_t) true;
        }
      }
      EXIT_CRITICAL();
      AddTaskToReadyList(TCB);
      if (TCB->Priority > CurrentTCB->Priority) {
        YieldPendings[0] = true;
      }
    } else {
      Ret = false;
    }
  }
  (void)ResumeAll();
  return Ret;
}

BaseType_t TaskIncrementTick(void) {
  TCB_t *TCB;
  TickType_t Value;
  BaseType_t xSwitchRequired = false;
  if (SchedulerSuspended == 0U) {
    const TickType_t ConstTickCount = TickCount + (TickType_t)1;
    TickCount = ConstTickCount;
    if (ConstTickCount == 0U) {
      taskSWITCH_DELAYED_LISTS();
    }
    if (ConstTickCount >= NextTaskUnblockTime) {
      for (;;) {
        if (DelayedTasks->empty()) {
          NextTaskUnblockTime = portMAX_DELAY;
          break;
        } else {
          TCB = DelayedTasks->head()->Owner;
          Value = TCB->StateListItem.Value;
          if (ConstTickCount < Value) {
            NextTaskUnblockTime = Value;
            break;
          }
          TCB->StateListItem.remove();
          TCB->EventListItem.ensureRemoved();
          AddTaskToReadyList(TCB);
          if (TCB->Priority > CurrentTCB->Priority) {
            xSwitchRequired = true;
          }
        }
      }
    }
    if (ReadyTasks[CurrentTCB->Priority].Length > 1U) {
      xSwitchRequired = true;
    }
    if (PendedTicks == (TickType_t)0) {
      ApplicationTickHook();
    }
    if (YieldPendings[0]) {
      xSwitchRequired = true;
    }
  } else {
    PendedTicks += 1U;

#if (configUSE_TICK_HOOK == 1)
    { ApplicationTickHook(); }
#endif
  }

  return xSwitchRequired;
}

void SwitchContext(void) {
  if (SchedulerSuspended != 0U) {
    YieldPendings[0] = true;
  } else {
    YieldPendings[0] = false;
    taskSELECT_HIGHEST_PRIORITY_TASK();
    portTASK_SWITCH_HOOK(CurrentTCB);
  }
}

void PlaceOnEventList(List_t<TCB_t> *const EventList,
                      const TickType_t TicksToWait) {
  configASSERT(EventList);
  EventList->insert(&(CurrentTCB->EventListItem));
  AddCurrentTaskToDelayedList(TicksToWait, true);
}

void PlaceOnUnorderedEventList(List_t<TCB_t> *EventList, const TickType_t Value,
                               const TickType_t TicksToWait) {
  configASSERT(EventList);
  configASSERT(SchedulerSuspended != 0U);
  CurrentTCB->EventListItem.Value = Value | taskEVENT_LIST_ITEM_VALUE_IN_USE;
  EventList->append(&(CurrentTCB->EventListItem));
  AddCurrentTaskToDelayedList(TicksToWait, true);
}

void PlaceOnEventListRestricted(List_t<TCB_t> *const EventList,
                                TickType_t TicksToWait,
                                const BaseType_t xWaitIndefinitely) {
  configASSERT(EventList);
  EventList->append(&(CurrentTCB->EventListItem));
  AddCurrentTaskToDelayedList(xWaitIndefinitely ? portMAX_DELAY : TicksToWait,
                              xWaitIndefinitely);
}

BaseType_t TaskRemoveFromEventList(List_t<TCB_t> *const EventList) {
  TCB_t *UnblockedTCB = EventList->head()->Owner;
  configASSERT(UnblockedTCB);
  UnblockedTCB->EventListItem.remove();
  if (SchedulerSuspended == 0U) {
    UnblockedTCB->StateListItem.remove();
    AddTaskToReadyList(UnblockedTCB);
  } else {
    PendingReady.append(&UnblockedTCB->EventListItem);
  }
  if (UnblockedTCB->Priority > CurrentTCB->Priority) {
    YieldPendings[0] = true;
    return true;
  }
  return false;
}

void RemoveFromUnorderedEventList(Item_t<TCB_t> *EventListItem,
                                  const TickType_t ItemValue) {
  configASSERT(SchedulerSuspended != 0U);
  EventListItem->Value = ItemValue | taskEVENT_LIST_ITEM_VALUE_IN_USE;
  TCB_t *UnblockedTCB = EventListItem->Owner;
  configASSERT(UnblockedTCB);
  EventListItem->remove();
  UnblockedTCB->StateListItem.remove();
  AddTaskToReadyList(UnblockedTCB);
  if (UnblockedTCB->Priority > CurrentTCB->Priority) {
    YieldPendings[0] = true;
  }
}
void TaskSetTimeOutState(TimeOut_t *const TimeOut) {
  configASSERT(TimeOut);
  ENTER_CRITICAL();
  TimeOut->OverflowCount = NOverflows;
  TimeOut->TimeOnEntering = TickCount;
  EXIT_CRITICAL();
}
void SetTimeOutState(TimeOut_t *const TimeOut) {
  TimeOut->OverflowCount = NOverflows;
  TimeOut->TimeOnEntering = TickCount;
}
BaseType_t CheckForTimeOut(TimeOut_t *const TimeOut,
                           TickType_t *const pTicksToWait) {
  BaseType_t Ret;
  configASSERT(TimeOut);
  configASSERT(pTicksToWait);
  CriticalSection s;
  const TickType_t ConstTickCount = TickCount;
  const TickType_t xElapsedTime = ConstTickCount - TimeOut->TimeOnEntering;
  if (CurrentTCB->DelayAborted != (uint8_t) false) {
    CurrentTCB->DelayAborted = (uint8_t) false;
    return true;
  }

  if (*pTicksToWait == portMAX_DELAY) {
    return false;
  }

  if ((NOverflows != TimeOut->OverflowCount) &&
      (ConstTickCount >= TimeOut->TimeOnEntering)) {
    *pTicksToWait = (TickType_t)0;
    return true;
  }

  if (xElapsedTime < *pTicksToWait) {
    *pTicksToWait -= xElapsedTime;
    SetTimeOutState(TimeOut);
    return false;
  }

  *pTicksToWait = (TickType_t)0;
  return true;
}

void MissedYield(void) { YieldPendings[portGET_CORE_ID()] = true; }

static void IdleTask(void *) {
  portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE);
  for (; configCONTROL_INFINITE_LOOP();) {
    CheckTasksWaitingTermination();
    if (ReadyTasks[tskIDLE_PRIORITY].Length >
        (UBaseType_t)configNUMBER_OF_CORES) {
      taskYIELD();
    }
    ApplicationIdleHook();
  }
}

static void InitialiseTaskLists(void) {
  for (int pri = 0; pri < configMAX_PRIORITIES; pri++) {
    (ReadyTasks[pri]).init();
  }
  DelayedTasks1.init();
  DelayedTasks2.init();
  PendingReady.init();
  TasksWaitingTermination.init();
  SuspendedTasks.init();
  DelayedTasks = &DelayedTasks1;
  OverflowDelayed = &DelayedTasks2;
}

static void CheckTasksWaitingTermination(void) {
  while (DeletedTasksWaitingCleanUp > 0) {
    TCB_t *TCB;
    {
      CriticalSection s;
      TCB = TasksWaitingTermination.head()->Owner;
      TCB->StateListItem.remove();
      --CurrentNumberOfTasks;
      --DeletedTasksWaitingCleanUp;
    }
    DeleteTCB(TCB);
  }
}

static void DeleteTCB(TCB_t *TCB) {
  portCLEAN_UP_TCB(TCB);
  if (TCB->StaticallyAllocated == tskDYNAMICALLY_ALLOCATED_STACK_AND_TCB) {
    vPortFreeStack(TCB->Stack);
    vPortFree(TCB);
  } else if (TCB->StaticallyAllocated == tskSTATICALLY_ALLOCATED_STACK_ONLY) {
    vPortFree(TCB);
  } else {
    configASSERT(TCB->StaticallyAllocated ==
                 tskSTATICALLY_ALLOCATED_STACK_AND_TCB);
  }
}

static void ResetNextTaskUnblockTime(void) {
  NextTaskUnblockTime =
      DelayedTasks->empty() ? portMAX_DELAY : DelayedTasks->head()->Value;
}

TaskHandle_t GetCurrentTaskHandle(void) { return CurrentTCB; }

BaseType_t GetSchedulerState(void) {
  return SchedulerRunning ? SchedulerSuspended ? taskSCHEDULER_SUSPENDED
                                               : taskSCHEDULER_RUNNING
                          : taskSCHEDULER_NOT_STARTED;
}

BaseType_t PriorityInherit(TaskHandle_t const pMutexHolder) {
  TCB_t *const pMutexHolderTCB = pMutexHolder;
  if (pMutexHolder == NULL) {
    return false;
  }
  if (pMutexHolderTCB->Priority >= CurrentTCB->Priority) {
    return pMutexHolderTCB->BasePriority < CurrentTCB->Priority;
  }
  if ((pMutexHolderTCB->EventListItem.Value &
       taskEVENT_LIST_ITEM_VALUE_IN_USE) == 0U) {
    pMutexHolderTCB->EventListItem.Value =
        (TickType_t)configMAX_PRIORITIES - (TickType_t)CurrentTCB->Priority;
  }
  if (pMutexHolderTCB->StateListItem.Container ==
      &(ReadyTasks[pMutexHolderTCB->Priority])) {
    if (pMutexHolderTCB->StateListItem.remove() == 0) {
      portRESET_READY_PRIORITY(pMutexHolderTCB->Priority, TopReadyPriority);
    }
    pMutexHolderTCB->Priority = CurrentTCB->Priority;
    AddTaskToReadyList(pMutexHolderTCB);
  } else {
    pMutexHolderTCB->Priority = CurrentTCB->Priority;
  }
  return true;
}

BaseType_t PriorityDisinherit(TaskHandle_t const pMutexHolder) {
  TCB_t *const TCB = pMutexHolder;
  BaseType_t Ret = false;
  if (pMutexHolder == NULL) {
    return false;
  }
  configASSERT(TCB == CurrentTCB);
  configASSERT(TCB->MutexesHeld);
  TCB->MutexesHeld--;
  if (TCB->Priority == TCB->BasePriority) {
    return false;
  }
  if (TCB->MutexesHeld > 0) {
    return false;
  }
  if (TCB->StateListItem.remove() == 0) {
    portRESET_READY_PRIORITY(TCB->Priority, TopReadyPriority);
  }
  TCB->Priority = TCB->BasePriority;
  TCB->EventListItem.Value =
      (TickType_t)configMAX_PRIORITIES - (TickType_t)TCB->Priority;
  AddTaskToReadyList(TCB);
  return true;
}

void PriorityDisinheritAfterTimeout(TaskHandle_t const pMutexHolder,
                                    UBaseType_t uxHighestPriorityWaitingTask) {
  TCB_t *const TCB = pMutexHolder;
  UBaseType_t PriorityUsedOnEntry, PriorityToUse;
  const UBaseType_t uxOnlyOneMutexHeld = (UBaseType_t)1;
  if (pMutexHolder != NULL) {
    configASSERT(TCB->MutexesHeld);
    if (TCB->BasePriority < uxHighestPriorityWaitingTask) {
      PriorityToUse = uxHighestPriorityWaitingTask;
    } else {
      PriorityToUse = TCB->BasePriority;
    }
    if (TCB->Priority != PriorityToUse) {
      if (TCB->MutexesHeld == uxOnlyOneMutexHeld) {
        configASSERT(TCB != CurrentTCB);
        PriorityUsedOnEntry = TCB->Priority;
        TCB->Priority = PriorityToUse;
        if ((TCB->EventListItem.Value & taskEVENT_LIST_ITEM_VALUE_IN_USE) ==
            0U) {
          TCB->EventListItem.Value =
              (TickType_t)configMAX_PRIORITIES - (TickType_t)PriorityToUse;
        }
        if (TCB->StateListItem.Container == &ReadyTasks[PriorityUsedOnEntry]) {
          if (TCB->StateListItem.remove() == 0) {
            portRESET_READY_PRIORITY(TCB->Priority, TopReadyPriority);
          }
          AddTaskToReadyList(TCB);
        }
      }
    }
  }
}

void TaskEnterCritical(void) {
  portDISABLE_INTERRUPTS();
  if (SchedulerRunning) {
    (CurrentTCB->CriticalNesting)++;
    if (CurrentTCB->CriticalNesting == 1U) {
      portASSERT_IF_IN_ISR();
    }
  }
}

void TaskExitCritical(void) {
  if (!SchedulerRunning) {
    return;
  }
  configASSERT(CurrentTCB->CriticalNesting > 0U);
  portASSERT_IF_IN_ISR();
  if (CurrentTCB->CriticalNesting > 0U) {
    (CurrentTCB->CriticalNesting)--;
    if (CurrentTCB->CriticalNesting == 0U) {
      portENABLE_INTERRUPTS();
    }
  }
}

TickType_t ResetEventItemValue(void) {
  TickType_t uRet = CurrentTCB->EventListItem.Value;
  CurrentTCB->EventListItem.Value =
      (TickType_t)configMAX_PRIORITIES - (TickType_t)CurrentTCB->Priority;
  return uRet;
}

TaskHandle_t IncrementMutexHeldCount(void) {
  TCB_t *TCB = CurrentTCB;
  if (TCB != NULL) {
    (TCB->MutexesHeld)++;
  }
  return TCB;
}

uint32_t ulTaskGenericNotifyTake(UBaseType_t uxIndexToWaitOn,
                                 BaseType_t xClearCountOnExit,
                                 TickType_t TicksToWait) {
  uint32_t ulReturn;
  BaseType_t xAlreadyYielded, ShouldBlock = false;
  configASSERT(uxIndexToWaitOn < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  TaskSuspendAll();
  {
    CriticalSection s;
    if (CurrentTCB->NotifiedValue[uxIndexToWaitOn] == 0U) {
      CurrentTCB->NotifyState[uxIndexToWaitOn] = taskWAITING_NOTIFICATION;
      if (TicksToWait > (TickType_t)0) {
        ShouldBlock = true;
      }
    }
  }
  if (ShouldBlock) {
    AddCurrentTaskToDelayedList(TicksToWait, true);
  }
  xAlreadyYielded = ResumeAll();
  if ((ShouldBlock) && (xAlreadyYielded == false)) {
    taskYIELD_WITHIN_API();
  }

  ENTER_CRITICAL();
  {
    ulReturn = CurrentTCB->NotifiedValue[uxIndexToWaitOn];
    if (ulReturn != 0U) {
      if (xClearCountOnExit) {
        CurrentTCB->NotifiedValue[uxIndexToWaitOn] = (uint32_t)0U;
      } else {
        CurrentTCB->NotifiedValue[uxIndexToWaitOn] = ulReturn - (uint32_t)1;
      }
    }

    CurrentTCB->NotifyState[uxIndexToWaitOn] = taskNOT_WAITING_NOTIFICATION;
  }
  EXIT_CRITICAL();
  return ulReturn;
}

BaseType_t GenericNotifyWait(UBaseType_t uxIndexToWaitOn,
                             uint32_t ulBitsToClearOnEntry,
                             uint32_t ulBitsToClearOnExit,
                             uint32_t *NotificationValue,
                             TickType_t TicksToWait) {
  BaseType_t Ret, xAlreadyYielded, ShouldBlock = false;
  configASSERT(uxIndexToWaitOn < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  TaskSuspendAll();
  {
    CriticalSection s;
    if (CurrentTCB->NotifyState[uxIndexToWaitOn] != taskNOTIFICATION_RECEIVED) {
      CurrentTCB->NotifiedValue[uxIndexToWaitOn] &= ~ulBitsToClearOnEntry;
      CurrentTCB->NotifyState[uxIndexToWaitOn] = taskWAITING_NOTIFICATION;
      if (TicksToWait > (TickType_t)0) {
        ShouldBlock = true;
      }
    }
  }
  if (ShouldBlock) {
    AddCurrentTaskToDelayedList(TicksToWait, true);
  }
  xAlreadyYielded = ResumeAll();
  if ((ShouldBlock) && (xAlreadyYielded == false)) {
    taskYIELD_WITHIN_API();
  }

  CriticalSection s;
  if (NotificationValue != NULL) {
    *NotificationValue = CurrentTCB->NotifiedValue[uxIndexToWaitOn];
  }
  if (CurrentTCB->NotifyState[uxIndexToWaitOn] != taskNOTIFICATION_RECEIVED) {
    Ret = false;
  } else {
    CurrentTCB->NotifiedValue[uxIndexToWaitOn] &= ~ulBitsToClearOnExit;
    Ret = true;
  }
  CurrentTCB->NotifyState[uxIndexToWaitOn] = taskNOT_WAITING_NOTIFICATION;
  return Ret;
}

BaseType_t GenericNotify(TaskHandle_t TaskToNotify, UBaseType_t uxIndexToNotify,
                         uint32_t ulValue, eNotifyAction eAction,
                         uint32_t *PreviousNotificationValue) {
  TCB_t *TCB;
  BaseType_t Ret = true;
  uint8_t ucOriginalNotifyState;
  configASSERT(uxIndexToNotify < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  configASSERT(TaskToNotify);
  TCB = TaskToNotify;
  CriticalSection s;
  if (PreviousNotificationValue != NULL) {
    *PreviousNotificationValue = TCB->NotifiedValue[uxIndexToNotify];
  }
  ucOriginalNotifyState = TCB->NotifyState[uxIndexToNotify];
  TCB->NotifyState[uxIndexToNotify] = taskNOTIFICATION_RECEIVED;
  switch (eAction) {
    case eSetBits:
      TCB->NotifiedValue[uxIndexToNotify] |= ulValue;
      break;
    case eIncrement:
      (TCB->NotifiedValue[uxIndexToNotify])++;
      break;
    case eSetValueWithOverwrite:
      TCB->NotifiedValue[uxIndexToNotify] = ulValue;
      break;
    case eSetValueWithoutOverwrite:
      if (ucOriginalNotifyState != taskNOTIFICATION_RECEIVED) {
        TCB->NotifiedValue[uxIndexToNotify] = ulValue;
      } else {
        Ret = false;
      }
      break;
    case eNoAction:
      break;
    default:
      configASSERT(TickCount == (TickType_t)0);
      break;
  }
  if (ucOriginalNotifyState == taskWAITING_NOTIFICATION) {
    TCB->StateListItem.remove();
    AddTaskToReadyList(TCB);
    configASSERT(TCB->EventListItem.Container == NULL);
    taskYIELD_ANY_CORE_IF_USING_PREEMPTION(TCB);
  }
  return Ret;
}

BaseType_t GenericNotifyFromISR(TaskHandle_t TaskToNotify,
                                UBaseType_t uxIndexToNotify, uint32_t ulValue,
                                eNotifyAction eAction,
                                uint32_t *PreviousNotificationValue,
                                BaseType_t *HigherPriorityTaskWoken) {
  TCB_t *TCB;
  uint8_t ucOriginalNotifyState;
  BaseType_t Ret = true;
  UBaseType_t uxSavedInterruptStatus;
  configASSERT(TaskToNotify);
  configASSERT(uxIndexToNotify < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  TCB = TaskToNotify;
  uxSavedInterruptStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
    if (PreviousNotificationValue != NULL) {
      *PreviousNotificationValue = TCB->NotifiedValue[uxIndexToNotify];
    }
    ucOriginalNotifyState = TCB->NotifyState[uxIndexToNotify];
    TCB->NotifyState[uxIndexToNotify] = taskNOTIFICATION_RECEIVED;
    switch (eAction) {
      case eSetBits:
        TCB->NotifiedValue[uxIndexToNotify] |= ulValue;
        break;
      case eIncrement:
        (TCB->NotifiedValue[uxIndexToNotify])++;
        break;
      case eSetValueWithOverwrite:
        TCB->NotifiedValue[uxIndexToNotify] = ulValue;
        break;
      case eSetValueWithoutOverwrite:
        if (ucOriginalNotifyState != taskNOTIFICATION_RECEIVED) {
          TCB->NotifiedValue[uxIndexToNotify] = ulValue;
        } else {
          Ret = false;
        }
        break;
      case eNoAction:
        break;
      default:
        configASSERT(TickCount == (TickType_t)0);
        break;
    }
    if (ucOriginalNotifyState == taskWAITING_NOTIFICATION) {
      configASSERT(TCB->EventListItem.Container == NULL);
      if (SchedulerSuspended == 0U) {
        TCB->StateListItem.remove();
        AddTaskToReadyList(TCB);
      } else {
        PendingReady.append(&TCB->EventListItem);
      }
      if (TCB->Priority > CurrentTCB->Priority) {
        if (HigherPriorityTaskWoken != NULL) {
          *HigherPriorityTaskWoken = true;
        }
        YieldPendings[0] = true;
      }
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return Ret;
}

void GenericNotifyGiveFromISR(TaskHandle_t TaskToNotify,
                              UBaseType_t uxIndexToNotify,
                              BaseType_t *HigherPriorityTaskWoken) {
  TCB_t *TCB;
  uint8_t ucOriginalNotifyState;
  UBaseType_t uxSavedInterruptStatus;
  configASSERT(TaskToNotify);
  configASSERT(uxIndexToNotify < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  TCB = TaskToNotify;
  uxSavedInterruptStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
    ucOriginalNotifyState = TCB->NotifyState[uxIndexToNotify];
    TCB->NotifyState[uxIndexToNotify] = taskNOTIFICATION_RECEIVED;
    (TCB->NotifiedValue[uxIndexToNotify])++;
    if (ucOriginalNotifyState == taskWAITING_NOTIFICATION) {
      configASSERT(TCB->EventListItem.Container == NULL);
      if (SchedulerSuspended == 0U) {
        TCB->StateListItem.remove();
        AddTaskToReadyList(TCB);
      } else {
        PendingReady.append(&TCB->EventListItem);
      }
      if (TCB->Priority > CurrentTCB->Priority) {
        if (HigherPriorityTaskWoken != NULL) {
          *HigherPriorityTaskWoken = true;
        }
        YieldPendings[0] = true;
      }
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
}

BaseType_t TaskGenericNotifyStateClear(TaskHandle_t Task,
                                       UBaseType_t IndexToClear) {
  TCB_t *TCB;
  BaseType_t Ret;
  configASSERT(IndexToClear < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  TCB = GetTCBFromHandle(Task);
  CriticalSection s;
  bool received = TCB->NotifyState[IndexToClear] == taskNOTIFICATION_RECEIVED;
  if (received) {
    TCB->NotifyState[IndexToClear] = taskNOT_WAITING_NOTIFICATION;
  }
  return received;
}

uint32_t ulTaskGenericNotifyValueClear(TaskHandle_t Task,
                                       UBaseType_t IndexToClear,
                                       uint32_t ulBitsToClear) {
  configASSERT(IndexToClear < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  TCB_t *TCB = GetTCBFromHandle(Task);
  CriticalSection s;
  uint32_t ulReturn = TCB->NotifiedValue[IndexToClear];
  TCB->NotifiedValue[IndexToClear] &= ~ulBitsToClear;
  return ulReturn;
}

static void AddCurrentTaskToDelayedList(TickType_t TicksToWait,
                                        const BaseType_t CanBlockIndefinitely) {
  TickType_t TimeToWake;
  const TickType_t ConstTickCount = TickCount;
  auto *DelayedList = DelayedTasks;
  auto *OverflowDelayedList = OverflowDelayed;
  CurrentTCB->DelayAborted = (uint8_t) false;
  if (CurrentTCB->StateListItem.remove() == (UBaseType_t)0) {
    portRESET_READY_PRIORITY(CurrentTCB->Priority, TopReadyPriority);
  }
  if ((TicksToWait == portMAX_DELAY) && (CanBlockIndefinitely)) {
    SuspendedTasks.append(&CurrentTCB->StateListItem);
  } else {
    TimeToWake = ConstTickCount + TicksToWait;
    CurrentTCB->StateListItem.Value = TimeToWake;
    if (TimeToWake < ConstTickCount) {
      OverflowDelayedList->insert(&(CurrentTCB->StateListItem));
    } else {
      DelayedList->insert(&(CurrentTCB->StateListItem));
      if (TimeToWake < NextTaskUnblockTime) {
        NextTaskUnblockTime = TimeToWake;
      }
    }
  }
}

void GetIdleTaskMemory(StaticTask_t **TCBBuffer, StackType_t **StackBuffer,
                       configSTACK_DEPTH_TYPE *StackSize) {
  static StaticTask_t TCB;
  static StackType_t Stack[configMINIMAL_STACK_SIZE];
  *TCBBuffer = &TCB;
  *StackBuffer = &Stack[0];
  *StackSize = configMINIMAL_STACK_SIZE;
}

void ApplicationGetTimerTaskMemory(StaticTask_t **TCBBuffer,
                                   StackType_t **StackBuffer,
                                   configSTACK_DEPTH_TYPE *StackSize) {
  static StaticTask_t TCB;
  static StackType_t Stack[configTIMER_TASK_STACK_DEPTH];
  *TCBBuffer = &TCB;
  *StackBuffer = &Stack[0];
  *StackSize = configTIMER_TASK_STACK_DEPTH;
}

void TaskResetState(void) {
  BaseType_t xCoreID;
  CurrentTCB = NULL;
  DeletedTasksWaitingCleanUp = 0U;
  CurrentNumberOfTasks = 0U;
  TickCount = (TickType_t)configINITIAL_TICK_COUNT;
  TopReadyPriority = tskIDLE_PRIORITY;
  SchedulerRunning = false;
  PendedTicks = 0U;
  for (xCoreID = 0; xCoreID < configNUMBER_OF_CORES; xCoreID++) {
    YieldPendings[xCoreID] = false;
  }
  NOverflows = 0;
  TaskNumber = 0U;
  NextTaskUnblockTime = 0U;
  SchedulerSuspended = 0U;
}
