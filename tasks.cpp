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

#define taskYIELD_ANY_CORE_IF_USING_PREEMPTION(pxTCB) \
  do {                                                \
    if (CurrentTCB->Priority < (pxTCB)->Priority) {   \
      portYIELD_WITHIN_API();                         \
    }                                                 \
  } while (0)

 
#define taskNOT_WAITING_NOTIFICATION \
  ((uint8_t)0)  
#define taskWAITING_NOTIFICATION ((uint8_t)1)
#define taskNOTIFICATION_RECEIVED ((uint8_t)2)
 
#define tskSTACK_FILL_BYTE (0xa5U)
 
#define tskDYNAMICALLY_ALLOCATED_STACK_AND_TCB ((uint8_t)0)
#define tskSTATICALLY_ALLOCATED_STACK_ONLY ((uint8_t)1)
#define tskSTATICALLY_ALLOCATED_STACK_AND_TCB ((uint8_t)2)
 
#if ((configCHECK_FOR_STACK_OVERFLOW > 1) ||       \
     (configUSE_TRACE_FACILITY == 1) ||            \
     (INCLUDE_uxTaskGetStackHighWaterMark == 1) || \
     (INCLUDE_uxTaskGetStackHighWaterMark2 == 1))
#define tskSET_NEW_STACKS_TO_KNOWN_VALUE 1
#else
#define tskSET_NEW_STACKS_TO_KNOWN_VALUE 0
#endif
 
#ifdef portREMOVE_STATIC_QUALIFIER
#define static
#endif
 
#ifndef configIDLE_TASK_NAME
#define configIDLE_TASK_NAME "IDLE"
#endif
#if (configUSE_PORT_OPTIMISED_TASK_SELECTION == 0)
 
 
#define RECORD_READY_PRIORITY(Priority)  \
  do {                                   \
    if ((Priority) > TopReadyPriority) { \
      TopReadyPriority = (Priority);     \
    }                                    \
  } while (0)  

#define taskSELECT_HIGHEST_PRIORITY_TASK()                                   \
  do {                                                                       \
    UBaseType_t uxTopPriority = TopReadyPriority;                            \
                                                                             \
              \
    while (LIST_IS_EMPTY(&(ReadyTasksLists[uxTopPriority])) != false) {      \
      configASSERT(uxTopPriority);                                           \
      --uxTopPriority;                                                       \
    }                                                                        \
                                                                             \
    CurrentTCB = GET_OWNER_OF_NEXT_ENTRY(&(ReadyTasksLists[uxTopPriority])); \
    TopReadyPriority = uxTopPriority;                                        \
  } while (0)  

 
#define taskRESET_READY_PRIORITY(Priority)
#define portRESET_READY_PRIORITY(Priority, TopReadyPriority)
#else  
 
 
#define RECORD_READY_PRIORITY(Priority) \
  portRECORD_READY_PRIORITY((Priority), TopReadyPriority)

 
#define taskRESET_READY_PRIORITY(Priority)                      \
  do {                                                          \
    if (ReadyTasksLists[(Priority)].Length == (UBaseType_t)0) { \
      portRESET_READY_PRIORITY((Priority), (TopReadyPriority)); \
    }                                                           \
  } while (0)
#endif  

 
#define AddTaskToReadyList(pxTCB)                                     \
  do {                                                                \
    RECORD_READY_PRIORITY((pxTCB)->Priority);                         \
    ReadyTasksLists[pxTCB->Priority].append(&(pxTCB)->StateListItem); \
  } while (0)

 
#define GetTCBFromHandle(pxHandle) \
  (((pxHandle) == NULL) ? CurrentTCB : (pxHandle))
 
#if (configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_16_BITS)
#define taskEVENT_LIST_ITEM_VALUE_IN_USE ((uint16_t)0x8000U)
#elif (configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_32_BITS)
#define taskEVENT_LIST_ITEM_VALUE_IN_USE ((uint32_t)0x80000000U)
#elif (configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_64_BITS)
#define taskEVENT_LIST_ITEM_VALUE_IN_USE ((uint64_t)0x8000000000000000U)
#endif
 
#define taskTASK_NOT_RUNNING ((BaseType_t)(-1))
 
#define taskTASK_SCHEDULED_TO_YIELD ((BaseType_t)(-2))
 
#define taskTASK_IS_RUNNING(pxTCB) (((pxTCB) == CurrentTCB) ? (true) : (false))
#define taskTASK_IS_RUNNING_OR_SCHEDULED_TO_YIELD(pxTCB) \
  (((pxTCB) == CurrentTCB) ? (true) : (false))
 
#define taskATTRIBUTE_IS_IDLE (UBaseType_t)(1U << 0U)
#define taskBITS_PER_BYTE ((size_t)8)

 
typedef struct TCB_t {
  volatile StackType_t
      *pxTopOfStack;  
  Item_t<TCB_t> StateListItem;  
  Item_t<TCB_t>
      xEventListItem;  
  UBaseType_t
      Priority;  
  StackType_t *pxStack;  
  char pcTaskName[configMAX_TASK_NAME_LEN];  
  UBaseType_t uxCriticalNesting;  
  UBaseType_t uxBasePriority;  
  UBaseType_t uxMutexesHeld;
  volatile uint32_t NotifiedValue[configTASK_NOTIFICATION_ARRAY_ENTRIES];
  volatile uint8_t ucNotifyState[configTASK_NOTIFICATION_ARRAY_ENTRIES];
  uint8_t StaticallyAllocated;  
  uint8_t DelayAborted;
} TCB_t;

TCB_t *volatile CurrentTCB = nullptr;

 
static List_t<TCB_t>
    ReadyTasksLists[configMAX_PRIORITIES];  
static List_t<TCB_t> DelayedTaskList1;      
static List_t<TCB_t>
    DelayedTaskList2;  
static List_t<TCB_t> *volatile DelayedTaskList;  
static List_t<TCB_t>
    *volatile OverflowDelayedTaskList;  
static List_t<TCB_t>
    PendingReadyList;  
static List_t<TCB_t>
    xTasksWaitingTermination;  
static volatile UBaseType_t DeletedTasksWaitingCleanUp = (UBaseType_t)0U;
static List_t<TCB_t>
    SuspendedTaskList;  
 
static volatile UBaseType_t CurrentNumberOfTasks = (UBaseType_t)0U;
static volatile TickType_t TickCount = (TickType_t)configINITIAL_TICK_COUNT;
static volatile UBaseType_t TopReadyPriority = tskIDLE_PRIORITY;
static volatile BaseType_t SchedulerRunning = false;
static volatile TickType_t PendedTicks = (TickType_t)0U;
static volatile BaseType_t YieldPendings[configNUMBER_OF_CORES] = {false};
static volatile BaseType_t NumOfOverflows = (BaseType_t)0;
static UBaseType_t TaskNumber = (UBaseType_t)0U;
static volatile TickType_t NextTaskUnblockTime =
    (TickType_t)0U;  
static TaskHandle_t
    IdleTaskHandles[configNUMBER_OF_CORES];  
 
static const volatile UBaseType_t TopUsedPriority = configMAX_PRIORITIES - 1U;
 
static volatile bool SchedulerSuspended = false;

static void ResetNextTaskUnblockTime();
static inline void taskSWITCH_DELAYED_LISTS() {
  List_t<TCB_t> *pxTemp;
  configASSERT(DelayedTaskList->empty());
  pxTemp = DelayedTaskList;
  DelayedTaskList = OverflowDelayedTaskList;
  OverflowDelayedTaskList = pxTemp;
  NumOfOverflows++;
  ResetNextTaskUnblockTime();
}

static inline void taskSELECT_HIGHEST_PRIORITY_TASK() {
  UBaseType_t uxTopPriority;
  portGET_HIGHEST_PRIORITY(uxTopPriority, TopReadyPriority);
  configASSERT(ReadyTasksLists[uxTopPriority].Length > 0);
  CurrentTCB = ReadyTasksLists[uxTopPriority].advance()->Owner;
}

 
 
static BaseType_t CreateIdleTasks(void);
#if (configNUMBER_OF_CORES > 1)
 
static void CheckForRunStateChange(void);
#endif  
#if (configNUMBER_OF_CORES > 1)
 
static void YieldForTask(const TCB_t *pxTCB);
#endif  
#if (configNUMBER_OF_CORES > 1)
 
static void SelectHighestPriorityTask(BaseType_t xCoreID);
#endif  
 
#if (INCLUDE_vTaskSuspend == 1)
static BaseType_t TaskIsTaskSuspended(const TaskHandle_t xTask);
#endif  
 
static void InitialiseTaskLists(void);
 
static portTASK_FUNCTION_PROTO(IdleTask, Parameters);
#if (configNUMBER_OF_CORES > 1)
static portTASK_FUNCTION_PROTO(PassiveIdleTask, Parameters);
#endif
 
#if (INCLUDE_vTaskDelete == 1)
static void DeleteTCB(TCB_t *pxTCB);
#endif
 
static void CheckTasksWaitingTermination(void);
 
static void AddCurrentTaskToDelayedList(TickType_t TicksToWait,
                                        const BaseType_t CanBlockIndefinitely);
 
#if (INCLUDE_xTaskGetHandle == 1)
static TCB_t *SearchForNameWithinSingleList(List_t *pxList,
                                            const char NameToQuery[]);
#endif
 
#if ((configUSE_TRACE_FACILITY == 1) ||            \
     (INCLUDE_uxTaskGetStackHighWaterMark == 1) || \
     (INCLUDE_uxTaskGetStackHighWaterMark2 == 1))
static configSTACK_DEPTH_TYPE TaskCheckFreeStackSpace(
    const uint8_t *pucStackByte);
#endif
 
#if (configUSE_TICKLESS_IDLE != 0)
static TickType_t GetExpectedIdleTime(void);
#endif
 
static void ResetNextTaskUnblockTime(void);
#if (configUSE_STATS_FORMATTING_FUNCTIONS > 0)
 
static char *WriteNameToBuffer(char *pcBuffer, const char *pcTaskName);
#endif
 
static void InitialiseNewTask(TaskFunction_t TaskCode, const char *const Name,
                              const configSTACK_DEPTH_TYPE StackDepth,
                              void *const Parameters, UBaseType_t Priority,
                              TaskHandle_t *const CreatedTask, TCB_t *pxNewTCB,
                              const MemoryRegion_t *const xRegions);
 
static void AddNewTaskToReadyList(TCB_t *pxNewTCB);
 
#if (configSUPPORT_STATIC_ALLOCATION == 1)
static TCB_t *CreateStaticTask(TaskFunction_t TaskCode, const char *const Name,
                               const configSTACK_DEPTH_TYPE StackDepth,
                               void *const Parameters, UBaseType_t Priority,
                               StackType_t *const StackBuffer,
                               StaticTask_t *const TaskBuffer,
                               TaskHandle_t *const CreatedTask);
#endif  

 
#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
static TCB_t *CreateTask(TaskFunction_t TaskCode, const char *const Name,
                         const configSTACK_DEPTH_TYPE StackDepth,
                         void *const Parameters, UBaseType_t Priority,
                         TaskHandle_t *const CreatedTask);
#endif  
 
#ifdef FREERTOS_TASKS_C_ADDITIONS_INIT
static void freertos_tasks_c_additions_init(void);
#endif
#if (configUSE_PASSIVE_IDLE_HOOK == 1)
extern void vApplicationPassiveIdleHook(void);
#endif  

static TCB_t *CreateStaticTask(TaskFunction_t TaskCode, const char *const Name,
                               const configSTACK_DEPTH_TYPE StackDepth,
                               void *const Parameters, UBaseType_t Priority,
                               StackType_t *const StackBuffer,
                               StaticTask_t *const TaskBuffer,
                               TaskHandle_t *const CreatedTask) {
  TCB_t *pxNewTCB;
  configASSERT(StackBuffer != NULL);
  configASSERT(TaskBuffer != NULL);
#if (configASSERT_DEFINED == 1)
  {
     
    volatile size_t xSize = sizeof(StaticTask_t);
    configASSERT(xSize == sizeof(TCB_t));
    (void)xSize;  
  }
#endif  
  if ((TaskBuffer != NULL) && (StackBuffer != NULL)) {
     

    pxNewTCB = (TCB_t *)TaskBuffer;
    (void)memset((void *)pxNewTCB, 0x00, sizeof(TCB_t));
    pxNewTCB->pxStack = (StackType_t *)StackBuffer;
#if (tskSTATIC_AND_DYNAMIC_ALLOCATION_POSSIBLE != 0)
    {
       
      pxNewTCB->StaticallyAllocated = tskSTATICALLY_ALLOCATED_STACK_AND_TCB;
    }
#endif  
    InitialiseNewTask(TaskCode, Name, StackDepth, Parameters, Priority,
                      CreatedTask, pxNewTCB, NULL);
  } else {
    pxNewTCB = NULL;
  }
  return pxNewTCB;
}

TaskHandle_t xTaskCreateStatic(TaskFunction_t TaskCode, const char *const Name,
                               const configSTACK_DEPTH_TYPE StackDepth,
                               void *const Parameters, UBaseType_t Priority,
                               StackType_t *const StackBuffer,
                               StaticTask_t *const TaskBuffer) {
  TaskHandle_t xReturn = NULL;
  TCB_t *pxNewTCB;
  pxNewTCB = CreateStaticTask(TaskCode, Name, StackDepth, Parameters, Priority,
                              StackBuffer, TaskBuffer, &xReturn);
  if (pxNewTCB != NULL) {
    AddNewTaskToReadyList(pxNewTCB);
  }
  return xReturn;
}

static TCB_t *CreateTask(TaskFunction_t TaskCode, const char *const Name,
                         const configSTACK_DEPTH_TYPE StackDepth,
                         void *const Parameters, UBaseType_t Priority,
                         TaskHandle_t *const CreatedTask) {
  TCB_t *pxNewTCB;
 
#if (portSTACK_GROWTH > 0)
  {
     

    pxNewTCB = (TCB_t *)pvPortMalloc(sizeof(TCB_t));
    if (pxNewTCB != NULL) {
      (void)memset((void *)pxNewTCB, 0x00, sizeof(TCB_t));
       

      pxNewTCB->pxStack = (StackType_t *)pvPortMallocStack(
          (((size_t)StackDepth) * sizeof(StackType_t)));
      if (pxNewTCB->pxStack == NULL) {
         
        vPortFree(pxNewTCB);
        pxNewTCB = NULL;
      }
    }
  }
#else   
  {
    StackType_t *pxStack = (StackType_t *)pvPortMallocStack(
        (((size_t)StackDepth) * sizeof(StackType_t)));
    if (pxStack != NULL) {
      pxNewTCB = (TCB_t *)pvPortMalloc(sizeof(TCB_t));
      if (pxNewTCB != NULL) {
        (void)memset((void *)pxNewTCB, 0x00, sizeof(TCB_t));
        pxNewTCB->pxStack = pxStack;
      } else {
        vPortFreeStack(pxStack);
      }
    } else {
      pxNewTCB = NULL;
    }
  }
#endif  
  if (pxNewTCB != NULL) {
#if (tskSTATIC_AND_DYNAMIC_ALLOCATION_POSSIBLE != 0)
    {
       
      pxNewTCB->StaticallyAllocated = tskDYNAMICALLY_ALLOCATED_STACK_AND_TCB;
    }
#endif  
    InitialiseNewTask(TaskCode, Name, StackDepth, Parameters, Priority,
                      CreatedTask, pxNewTCB, NULL);
  }
  return pxNewTCB;
}

BaseType_t xTaskCreate(TaskFunction_t TaskCode, const char *const Name,
                       const configSTACK_DEPTH_TYPE StackDepth,
                       void *const Parameters, UBaseType_t Priority,
                       TaskHandle_t *const CreatedTask) {
  TCB_t *pxNewTCB;
  BaseType_t xReturn;
  pxNewTCB =
      CreateTask(TaskCode, Name, StackDepth, Parameters, Priority, CreatedTask);
  if (pxNewTCB != NULL) {
#if ((configNUMBER_OF_CORES > 1) && (configUSE_CORE_AFFINITY == 1))
    {
       
      pxNewTCB->uxCoreAffinityMask = configTASK_DEFAULT_CORE_AFFINITY;
    }
#endif
    AddNewTaskToReadyList(pxNewTCB);
    xReturn = true;
  } else {
    xReturn = errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
  }
  return xReturn;
}

static void InitialiseNewTask(TaskFunction_t TaskCode, const char *const Name,
                              const configSTACK_DEPTH_TYPE StackDepth,
                              void *const Parameters, UBaseType_t Priority,
                              TaskHandle_t *const CreatedTask, TCB_t *pxNewTCB,
                              const MemoryRegion_t *const xRegions) {
  StackType_t *pxTopOfStack;
  UBaseType_t x;

 
#if (tskSET_NEW_STACKS_TO_KNOWN_VALUE == 1)
  {
     
    (void)memset(pxNewTCB->pxStack, (int)tskSTACK_FILL_BYTE,
                 (size_t)StackDepth * sizeof(StackType_t));
  }
#endif  
 
#if (portSTACK_GROWTH < 0)
  {
    pxTopOfStack = &(pxNewTCB->pxStack[StackDepth - (configSTACK_DEPTH_TYPE)1]);
    pxTopOfStack =
        (StackType_t *)(((portPOINTER_SIZE_TYPE)pxTopOfStack) &
                        (~((portPOINTER_SIZE_TYPE)portBYTE_ALIGNMENT_MASK)));
     
    configASSERT((((portPOINTER_SIZE_TYPE)pxTopOfStack &
                   (portPOINTER_SIZE_TYPE)portBYTE_ALIGNMENT_MASK) == 0U));
#if (configRECORD_STACK_HIGH_ADDRESS == 1)
    {
       
      pxNewTCB->pxEndOfStack = pxTopOfStack;
    }
#endif  
  }
#else   
  {
    pxTopOfStack = pxNewTCB->pxStack;
    pxTopOfStack =
        (StackType_t *)((((portPOINTER_SIZE_TYPE)pxTopOfStack) +
                         portBYTE_ALIGNMENT_MASK) &
                        (~((portPOINTER_SIZE_TYPE)portBYTE_ALIGNMENT_MASK)));
     
    configASSERT((((portPOINTER_SIZE_TYPE)pxTopOfStack &
                   (portPOINTER_SIZE_TYPE)portBYTE_ALIGNMENT_MASK) == 0U));
     
    pxNewTCB->pxEndOfStack =
        pxNewTCB->pxStack + (StackDepth - (configSTACK_DEPTH_TYPE)1);
  }
#endif  
   
  if (Name != NULL) {
    for (x = (UBaseType_t)0; x < (UBaseType_t)configMAX_TASK_NAME_LEN; x++) {
      pxNewTCB->pcTaskName[x] = Name[x];
       
      if (Name[x] == (char)0x00) {
        break;
      }
    }
     
    pxNewTCB->pcTaskName[configMAX_TASK_NAME_LEN - 1U] = '\0';
  }
   
  configASSERT(Priority < configMAX_PRIORITIES);
  if (Priority >= (UBaseType_t)configMAX_PRIORITIES) {
    Priority = (UBaseType_t)configMAX_PRIORITIES - (UBaseType_t)1U;
  }
  pxNewTCB->Priority = Priority;
#if (configUSE_MUTEXES == 1)
  { pxNewTCB->uxBasePriority = Priority; }
#endif  
  pxNewTCB->StateListItem.init();
  pxNewTCB->xEventListItem.init();
  pxNewTCB->StateListItem.Owner = pxNewTCB;
  pxNewTCB->xEventListItem.Value =
      (TickType_t)configMAX_PRIORITIES - (TickType_t)Priority;
  pxNewTCB->xEventListItem.Owner = pxNewTCB;
  (void)xRegions;
  pxNewTCB->pxTopOfStack =
      pxPortInitialiseStack(pxTopOfStack, TaskCode, Parameters);
  if (CreatedTask != NULL) {
     
    *CreatedTask = (TaskHandle_t)pxNewTCB;
  }
}

static void AddNewTaskToReadyList(TCB_t *pxNewTCB) {
   
  ENTER_CRITICAL();
  {
    CurrentNumberOfTasks = (UBaseType_t)(CurrentNumberOfTasks + 1U);
    if (CurrentTCB == NULL) {
       
      CurrentTCB = pxNewTCB;
      if (CurrentNumberOfTasks == (UBaseType_t)1) {
         
        InitialiseTaskLists();
      }
    } else {
       
      if (SchedulerRunning == false) {
        if (CurrentTCB->Priority <= pxNewTCB->Priority) {
          CurrentTCB = pxNewTCB;
        }
      }
    }
    TaskNumber++;
    AddTaskToReadyList(pxNewTCB);
    portSETUP_TCB(pxNewTCB);
  }
  EXIT_CRITICAL();
  if (SchedulerRunning != false) {
     
    taskYIELD_ANY_CORE_IF_USING_PREEMPTION(pxNewTCB);
  }
}

void vTaskDelete(TaskHandle_t xTaskToDelete) {
  TCB_t *pxTCB;
  BaseType_t xDeleteTCBInIdleTask = false;
  BaseType_t xTaskIsRunningOrYielding;
  ENTER_CRITICAL();
  {
     
    pxTCB = GetTCBFromHandle(xTaskToDelete);
    if (pxTCB->StateListItem.remove() == 0) {
      taskRESET_READY_PRIORITY(pxTCB->Priority);
    }
    pxTCB->xEventListItem.ensureRemoved();
    TaskNumber++;
    xTaskIsRunningOrYielding = taskTASK_IS_RUNNING_OR_SCHEDULED_TO_YIELD(pxTCB);
    if ((SchedulerRunning != false) && (xTaskIsRunningOrYielding != false)) {
      xTasksWaitingTermination.append(&pxTCB->StateListItem);
      ++DeletedTasksWaitingCleanUp;
      xDeleteTCBInIdleTask = true;
      portPRE_TASK_DELETE_HOOK(pxTCB, &(YieldPendings[0]));
    } else {
      --CurrentNumberOfTasks;
      ResetNextTaskUnblockTime();
    }
  }
  EXIT_CRITICAL();
  if (xDeleteTCBInIdleTask != true) {
    DeleteTCB(pxTCB);
  }
  if (SchedulerRunning) {
    if (pxTCB == CurrentTCB) {
      configASSERT(SchedulerSuspended == 0);
      taskYIELD_WITHIN_API();
    }
  }
}

BaseType_t xTaskDelayUntil(TickType_t *const pxPreviousWakeTime,
                           const TickType_t xTimeIncrement) {
  TickType_t TimeToWake;
  BaseType_t xAlreadyYielded, xShouldDelay = false;
  configASSERT(pxPreviousWakeTime);
  configASSERT((xTimeIncrement > 0U));
  vTaskSuspendAll();
  {
     
    const TickType_t ConstTickCount = TickCount;
    configASSERT(SchedulerSuspended == 1U);
     
    TimeToWake = *pxPreviousWakeTime + xTimeIncrement;
    if (ConstTickCount < *pxPreviousWakeTime) {
       
      if ((TimeToWake < *pxPreviousWakeTime) && (TimeToWake > ConstTickCount)) {
        xShouldDelay = true;
      }
    } else {
       
      if ((TimeToWake < *pxPreviousWakeTime) || (TimeToWake > ConstTickCount)) {
        xShouldDelay = true;
      }
    }
     
    *pxPreviousWakeTime = TimeToWake;
    if (xShouldDelay != false) {
       
      AddCurrentTaskToDelayedList(TimeToWake - ConstTickCount, false);
    }
  }
  if (!TaskResumeAll()) {
    taskYIELD_WITHIN_API();
  }
  return xShouldDelay;
}

void vTaskDelay(const TickType_t xTicksToDelay) {
  if (xTicksToDelay > (TickType_t)0U) {
    vTaskSuspendAll();
    configASSERT(SchedulerSuspended);
    AddCurrentTaskToDelayedList(xTicksToDelay, false);
    if (!TaskResumeAll()) {
      taskYIELD_WITHIN_API();
    }
  }
}

#if ((INCLUDE_eTaskGetState == 1) || (configUSE_TRACE_FACILITY == 1) || \
     (INCLUDE_xTaskAbortDelay == 1))
eTaskState eTaskGetState(TaskHandle_t xTask) {
  eTaskState eReturn;
  List_t<TCB_t> *pxStateList;
  List_t<TCB_t> *pxEventList;
  List_t<TCB_t> *DelayedList;
  List_t<TCB_t> *pxOverflowedDelayedList;
  TCB_t *pxTCB = xTask;
  configASSERT(pxTCB);
  if (pxTCB == CurrentTCB) {
     
    eReturn = eRunning;
  } else {
    ENTER_CRITICAL();
    {
      pxStateList = pxTCB->StateListItem.Container;
      pxEventList = pxTCB->xEventListItem.Container;
      DelayedList = DelayedTaskList;
      pxOverflowedDelayedList = OverflowDelayedTaskList;
    }
    EXIT_CRITICAL();
    if (pxEventList == &PendingReadyList) {
       
      eReturn = eReady;
    } else if ((pxStateList == DelayedList) ||
               (pxStateList == pxOverflowedDelayedList)) {
       
      eReturn = eBlocked;
    } else if (pxStateList == &SuspendedTaskList) {
      if (pxTCB->xEventListItem.Container == NULL) {
        BaseType_t x;
        eReturn = eSuspended;
        for (x = (BaseType_t)0;
             x < (BaseType_t)configTASK_NOTIFICATION_ARRAY_ENTRIES; x++) {
          if (pxTCB->ucNotifyState[x] == taskWAITING_NOTIFICATION) {
            eReturn = eBlocked;
            break;
          }
        }
      } else {
        eReturn = eBlocked;
      }
    } else if ((pxStateList == &xTasksWaitingTermination) ||
               (pxStateList == NULL)) {
       
      eReturn = eDeleted;
    } else {
#if (configNUMBER_OF_CORES == 1)
      {
         
        eReturn = eReady;
      }
#else   
      {
        if (taskTASK_IS_RUNNING(pxTCB)) {
           
          eReturn = eRunning;
        } else {
           
          eReturn = eReady;
        }
      }
#endif  
    }
  }
  return eReturn;
}
#endif  

UBaseType_t uxTaskPriorityGet(const TaskHandle_t xTask) {
  TCB_t const *pxTCB;
  UBaseType_t uxReturn;
  ENTER_CRITICAL();
  {
     
    pxTCB = GetTCBFromHandle(xTask);
    uxReturn = pxTCB->Priority;
  }
  EXIT_CRITICAL();
  return uxReturn;
}

UBaseType_t uxTaskPriorityGetFromISR(const TaskHandle_t xTask) {
  TCB_t const *pxTCB;
  UBaseType_t uxReturn;
  UBaseType_t uxSavedInterruptStatus;
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  uxSavedInterruptStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
     
    pxTCB = GetTCBFromHandle(xTask);
    uxReturn = pxTCB->Priority;
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return uxReturn;
}

UBaseType_t uxTaskBasePriorityGet(const TaskHandle_t xTask) {
  TCB_t const *pxTCB;
  UBaseType_t uxReturn;
  ENTER_CRITICAL();
  {
     
    pxTCB = GetTCBFromHandle(xTask);
    uxReturn = pxTCB->uxBasePriority;
  }
  EXIT_CRITICAL();
  return uxReturn;
}

UBaseType_t uxTaskBasePriorityGetFromISR(const TaskHandle_t xTask) {
  TCB_t const *pxTCB;
  UBaseType_t uxReturn;
  UBaseType_t uxSavedInterruptStatus;
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  uxSavedInterruptStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
     
    pxTCB = GetTCBFromHandle(xTask);
    uxReturn = pxTCB->uxBasePriority;
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return uxReturn;
}

void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority) {
  TCB_t *pxTCB;
  UBaseType_t uxCurrentBasePriority, PriorityUsedOnEntry;
  BaseType_t xYieldRequired = false;
  configASSERT(uxNewPriority < configMAX_PRIORITIES);
   
  if (uxNewPriority >= (UBaseType_t)configMAX_PRIORITIES) {
    uxNewPriority = (UBaseType_t)configMAX_PRIORITIES - (UBaseType_t)1U;
  }

  ENTER_CRITICAL();
  {
     
    pxTCB = GetTCBFromHandle(xTask);
    uxCurrentBasePriority = pxTCB->uxBasePriority;
    if (uxCurrentBasePriority != uxNewPriority) {
       
      if (uxNewPriority > uxCurrentBasePriority) {
        if (pxTCB != CurrentTCB) {
           
          if (uxNewPriority > CurrentTCB->Priority) {
            xYieldRequired = true;
          }
        } else {
           
        }
      } else if (taskTASK_IS_RUNNING(pxTCB)) {
         
        xYieldRequired = true;
      }
      PriorityUsedOnEntry = pxTCB->Priority;
      if ((pxTCB->uxBasePriority == pxTCB->Priority) ||
          (uxNewPriority > pxTCB->Priority)) {
        pxTCB->Priority = uxNewPriority;
      }
      pxTCB->uxBasePriority = uxNewPriority;
      if ((pxTCB->xEventListItem.Value & taskEVENT_LIST_ITEM_VALUE_IN_USE) ==
          ((TickType_t)0U)) {
        pxTCB->xEventListItem.Value =
            (TickType_t)configMAX_PRIORITIES - (TickType_t)uxNewPriority;
      }
      if (pxTCB->StateListItem.Container ==
          &ReadyTasksLists[PriorityUsedOnEntry]) {
        if (pxTCB->StateListItem.remove() == 0) {
          portRESET_READY_PRIORITY(PriorityUsedOnEntry, TopReadyPriority);
        }
        AddTaskToReadyList(pxTCB);
      }
      if (xYieldRequired != false) {
        portYIELD_WITHIN_API();
      }
      (void)PriorityUsedOnEntry;
    }
  }
  EXIT_CRITICAL();
}

void vTaskSuspend(TaskHandle_t xTaskToSuspend) {
  TCB_t *pxTCB;
  ENTER_CRITICAL();
  {
    pxTCB = GetTCBFromHandle(xTaskToSuspend);
    if (pxTCB->StateListItem.remove() == 0) {
      taskRESET_READY_PRIORITY(pxTCB->Priority);
    }
    pxTCB->xEventListItem.ensureRemoved();

    SuspendedTaskList.append(&pxTCB->StateListItem);
    BaseType_t x;
    for (x = (BaseType_t)0;
         x < (BaseType_t)configTASK_NOTIFICATION_ARRAY_ENTRIES; x++) {
      if (pxTCB->ucNotifyState[x] == taskWAITING_NOTIFICATION) {
         
        pxTCB->ucNotifyState[x] = taskNOT_WAITING_NOTIFICATION;
      }
    }
  }
  EXIT_CRITICAL();
  {
    UBaseType_t uxCurrentListLength;
    if (SchedulerRunning != false) {
       
      ENTER_CRITICAL();
      { ResetNextTaskUnblockTime(); }
      EXIT_CRITICAL();
    }

    if (pxTCB == CurrentTCB) {
      if (SchedulerRunning != false) {
         
        configASSERT(SchedulerSuspended == 0);
        portYIELD_WITHIN_API();
      } else {
        uxCurrentListLength = SuspendedTaskList.Length;
        if (uxCurrentListLength == CurrentNumberOfTasks) {
          CurrentTCB = NULL;
        } else {
          vTaskSwitchContext();
        }
      }
    }
  }
}

static BaseType_t TaskIsTaskSuspended(const TaskHandle_t xTask) {
  BaseType_t xReturn = false;
  TCB_t *pxTCB = xTask;
  configASSERT(xTask);
  if (pxTCB->StateListItem.Container == &SuspendedTaskList) {
    if (pxTCB->xEventListItem.Container != &PendingReadyList) {
      if (pxTCB->xEventListItem.Container == nullptr) {
        xReturn = true;
        for (BaseType_t x = (BaseType_t)0;
             x < (BaseType_t)configTASK_NOTIFICATION_ARRAY_ENTRIES; x++) {
          if (pxTCB->ucNotifyState[x] == taskWAITING_NOTIFICATION) {
            xReturn = false;
            break;
          }
        }
      }
    }
  }
  return xReturn;
}

void vTaskResume(TaskHandle_t xTaskToResume) {
  TCB_t *const pxTCB = xTaskToResume;
   
  configASSERT(xTaskToResume);
   
  if ((pxTCB != CurrentTCB) && (pxTCB != NULL)) {
    ENTER_CRITICAL();
    {
      if (TaskIsTaskSuspended(pxTCB) != false) {
        pxTCB->StateListItem.remove();
        AddTaskToReadyList(pxTCB);
        taskYIELD_ANY_CORE_IF_USING_PREEMPTION(pxTCB);
      }
    }
    EXIT_CRITICAL();
  }
}

BaseType_t xTaskResumeFromISR(TaskHandle_t xTaskToResume) {
  BaseType_t xYieldRequired = false;
  TCB_t *const pxTCB = xTaskToResume;
  UBaseType_t uxSavedInterruptStatus;
  configASSERT(xTaskToResume);
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  uxSavedInterruptStatus = ENTER_CRITICAL_FROM_ISR();
  {
    if (TaskIsTaskSuspended(pxTCB) != false) {
       
      if (SchedulerSuspended == (UBaseType_t)0U) {
         
        if (pxTCB->Priority > CurrentTCB->Priority) {
          xYieldRequired = true;
          YieldPendings[0] = true;
        }
        pxTCB->StateListItem.remove();
        AddTaskToReadyList(pxTCB);
      } else {
        PendingReadyList.append(&pxTCB->xEventListItem);
      }
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return xYieldRequired;
}

static BaseType_t CreateIdleTasks(void) {
  BaseType_t xReturn = true;
  BaseType_t xCoreID;
  char cIdleName[configMAX_TASK_NAME_LEN];
  TaskFunction_t pxIdleTaskFunction = NULL;
  BaseType_t xIdleTaskNameIndex;
  for (xIdleTaskNameIndex = (BaseType_t)0;
       xIdleTaskNameIndex < (BaseType_t)configMAX_TASK_NAME_LEN;
       xIdleTaskNameIndex++) {
    cIdleName[xIdleTaskNameIndex] = configIDLE_TASK_NAME[xIdleTaskNameIndex];
     
    if (cIdleName[xIdleTaskNameIndex] == (char)0x00) {
      break;
    }
  }
   
  for (xCoreID = (BaseType_t)0; xCoreID < (BaseType_t)configNUMBER_OF_CORES;
       xCoreID++) {
    pxIdleTaskFunction = IdleTask;
    StaticTask_t *pIdleTaskTCBBuffer = NULL;
    StackType_t *pxIdleTaskStackBuffer = NULL;
    configSTACK_DEPTH_TYPE IdleTaskStackSize;
     
    ApplicationGetIdleTaskMemory(&pIdleTaskTCBBuffer, &pxIdleTaskStackBuffer,
                                 &IdleTaskStackSize);
    IdleTaskHandles[xCoreID] = xTaskCreateStatic(
        pxIdleTaskFunction, cIdleName, IdleTaskStackSize, (void *)NULL,
        portPRIVILEGE_BIT,  
        pxIdleTaskStackBuffer, pIdleTaskTCBBuffer);
    if (IdleTaskHandles[xCoreID] != NULL) {
      xReturn = true;
    } else {
      xReturn = false;
    }
     
    if (xReturn == false) {
      break;
    }
  }
  return xReturn;
}

void vTaskStartScheduler(void) {
  BaseType_t xReturn;
  xReturn = CreateIdleTasks();
  if (xReturn) {
    xReturn = TimerCreateTimerTask();
  }
  if (xReturn) {
 
#ifdef FREERTOS_TASKS_C_ADDITIONS_INIT
    { freertos_tasks_c_additions_init(); }
#endif
     
    portDISABLE_INTERRUPTS();
#if (configUSE_C_RUNTIME_TLS_SUPPORT == 1)
    {
       
      configSET_TLS_BLOCK(CurrentTCB->xTLSBlock);
    }
#endif
    NextTaskUnblockTime = portMAX_DELAY;
    SchedulerRunning = true;
    TickCount = (TickType_t)configINITIAL_TICK_COUNT;
     
    portCONFIGURE_TIMER_FOR_RUN_TIME_STATS();
     
     
    (void)xPortStartScheduler();
     
  } else {
     
    configASSERT(xReturn != errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY);
  }
   
  (void)IdleTaskHandles;
   
  (void)TopUsedPriority;
}

void vTaskEndScheduler(void) {
#if (INCLUDE_vTaskDelete == 1)
  {
    BaseType_t xCoreID;
#if (configUSE_TIMERS == 1)
    {
       
      vTaskDelete(TimerGetTimerDaemonTaskHandle());
    }
#endif  
     
    for (xCoreID = 0; xCoreID < (BaseType_t)configNUMBER_OF_CORES; xCoreID++) {
      vTaskDelete(IdleTaskHandles[xCoreID]);
    }
     
    CheckTasksWaitingTermination();
  }
#endif  
   
  portDISABLE_INTERRUPTS();
  SchedulerRunning = false;
   
  vPortEndScheduler();
}
 
void vTaskSuspendAll(void) {
#if (configNUMBER_OF_CORES == 1)
  {
     
     
    portSOFTWARE_BARRIER();
     
    SchedulerSuspended = (UBaseType_t)(SchedulerSuspended + 1U);
     
    portMEMORY_BARRIER();
  }
#else   
  {
    UBaseType_t ulState;
     
    portASSERT_IF_IN_ISR();
    if (SchedulerRunning != false) {
       
      ulState = portSET_INTERRUPT_MASK();
       
      configASSERT(portGET_CRITICAL_NESTING_COUNT() == 0);
       
      portSOFTWARE_BARRIER();
      portGET_TASK_LOCK();
       
      if (SchedulerSuspended == 0U) {
        CheckForRunStateChange();
      }

      portGET_ISR_LOCK();
       
      ++SchedulerSuspended;
      portRELEASE_ISR_LOCK();
      portCLEAR_INTERRUPT_MASK(ulState);
    }
  }
#endif  
}
 
#if (configUSE_TICKLESS_IDLE != 0)
static TickType_t GetExpectedIdleTime(void) {
  TickType_t xReturn;
  BaseType_t xHigherPriorityReadyTasks = false;
 
#if (configUSE_PORT_OPTIMISED_TASK_SELECTION == 0)
  {
    if (TopReadyPriority > tskIDLE_PRIORITY) {
      xHigherPriorityReadyTasks = true;
    }
  }
#else
  {
    const UBaseType_t uxLeastSignificantBit = (UBaseType_t)0x01;
     
    if (TopReadyPriority > uxLeastSignificantBit) {
      xHigherPriorityReadyTasks = true;
    }
  }
#endif  
  if (CurrentTCB->Priority > tskIDLE_PRIORITY) {
    xReturn = 0;
  } else if (CURRENT_LIST_LENGTH(&(ReadyTasksLists[tskIDLE_PRIORITY])) > 1U) {
     
    xReturn = 0;
  } else if (xHigherPriorityReadyTasks != false) {
     
    xReturn = 0;
  } else {
    xReturn = NextTaskUnblockTime;
    xReturn -= TickCount;
  }
  return xReturn;
}
#endif  
 
BaseType_t TaskResumeAll(void) {
  TCB_t *pxTCB = NULL;
  BaseType_t xAlreadyYielded = false;
#if (configNUMBER_OF_CORES > 1)
  if (SchedulerRunning != false)
#endif
  {
     
    ENTER_CRITICAL();
    {
      BaseType_t xCoreID;
      xCoreID = (BaseType_t)portGET_CORE_ID();
       
      configASSERT(SchedulerSuspended != 0U);
      SchedulerSuspended = (UBaseType_t)(SchedulerSuspended - 1U);
      portRELEASE_TASK_LOCK();
      if (SchedulerSuspended == (UBaseType_t)0U) {
        if (CurrentNumberOfTasks > (UBaseType_t)0U) {
          while (PendingReadyList.Length > 0) {
            pxTCB = PendingReadyList.head()->Owner;
            pxTCB->xEventListItem.remove();
            portMEMORY_BARRIER();
            pxTCB->StateListItem.remove();
            AddTaskToReadyList(pxTCB);
#if (configNUMBER_OF_CORES == 1)
            {
               
              if (pxTCB->Priority > CurrentTCB->Priority) {
                YieldPendings[xCoreID] = true;
              }
            }
#else   
            {
               
            }
#endif  
          }
          if (pxTCB != NULL) {
             
            ResetNextTaskUnblockTime();
          }
           
          {
            TickType_t xPendedCounts = PendedTicks;  
            if (xPendedCounts > (TickType_t)0U) {
              do {
                if (xTaskIncrementTick() != false) {
                   
                  YieldPendings[xCoreID] = true;
                }
                --xPendedCounts;
              } while (xPendedCounts > (TickType_t)0U);
              PendedTicks = 0;
            }
          }
          if (YieldPendings[xCoreID] != false) {
#if (configUSE_PREEMPTION != 0)
            { xAlreadyYielded = true; }
#endif  
            portYIELD_WITHIN_API();
          }
        }
      }
    }
    EXIT_CRITICAL();
  }
  return xAlreadyYielded;
}

TickType_t xTaskGetTickCount(void) {
  portTICK_TYPE_ENTER_CRITICAL();
  TickType_t xTicks = TickCount;
  portTICK_TYPE_EXIT_CRITICAL();
  return xTicks;
}

TickType_t xTaskGetTickCountFromISR(void) {
  TickType_t xReturn;
  UBaseType_t uxSavedInterruptStatus;
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  uxSavedInterruptStatus = portTICK_TYPE_SET_INTERRUPT_MASK_FROM_ISR();
  { xReturn = TickCount; }
  portTICK_TYPE_CLEAR_INTERRUPT_MASK_FROM_ISR(uxSavedInterruptStatus);
  return xReturn;
}

UBaseType_t TaskGetNumberOfTasks(void) { return CurrentNumberOfTasks; }

char *pcTaskGetName(TaskHandle_t xTaskToQuery) {
  TCB_t *pxTCB;
  pxTCB = GetTCBFromHandle(xTaskToQuery);
  configASSERT(pxTCB);
  return &(pxTCB->pcTaskName[0]);
}

BaseType_t xTaskGetStaticBuffers(TaskHandle_t xTask, StackType_t **pStackBuffer,
                                 StaticTask_t **pTaskBuffer) {
  TCB_t *pxTCB;
  configASSERT(pStackBuffer != NULL);
  configASSERT(pTaskBuffer != NULL);
  pxTCB = GetTCBFromHandle(xTask);
  if (pxTCB->StaticallyAllocated == tskSTATICALLY_ALLOCATED_STACK_AND_TCB) {
    *pStackBuffer = pxTCB->pxStack;
    *pTaskBuffer = (StaticTask_t *)pxTCB;
    return true;
  }
  if (pxTCB->StaticallyAllocated == tskSTATICALLY_ALLOCATED_STACK_ONLY) {
    *pStackBuffer = pxTCB->pxStack;
    *pTaskBuffer = NULL;
    return true;
  }
  return true;
}
TaskHandle_t xTaskGetIdleTaskHandle(void) {
   
  configASSERT((IdleTaskHandles[0] != NULL));
  return IdleTaskHandles[0];
}
TaskHandle_t xTaskGetIdleTaskHandleForCore(BaseType_t xCoreID) {
   
  configASSERT(taskVALID_CORE_ID(xCoreID));
   
  configASSERT((IdleTaskHandles[xCoreID] != NULL));
  return IdleTaskHandles[xCoreID];
}

BaseType_t xTaskCatchUpTicks(TickType_t xTicksToCatchUp) {
  BaseType_t xYieldOccurred;
  configASSERT(SchedulerSuspended == (UBaseType_t)0U);
  vTaskSuspendAll();
  ENTER_CRITICAL();
  PendedTicks += xTicksToCatchUp;
  EXIT_CRITICAL();
  return TaskResumeAll();
}

BaseType_t xTaskAbortDelay(TaskHandle_t xTask) {
  TCB_t *pxTCB = xTask;
  BaseType_t xReturn;
  configASSERT(pxTCB);
  vTaskSuspendAll();
  {
    if (eTaskGetState(xTask) == eBlocked) {
      xReturn = true;
      pxTCB->StateListItem.remove();
      ENTER_CRITICAL();
      {
        if (pxTCB->xEventListItem.Container != NULL) {
          pxTCB->xEventListItem.remove();
          pxTCB->DelayAborted = (uint8_t) true;
        }
      }
      EXIT_CRITICAL();
      AddTaskToReadyList(pxTCB);
      if (pxTCB->Priority > CurrentTCB->Priority) {
        YieldPendings[0] = true;
      }
    } else {
      xReturn = false;
    }
  }
  (void)TaskResumeAll();
  return xReturn;
}

BaseType_t xTaskIncrementTick(void) {
  TCB_t *pxTCB;
  TickType_t Value;
  BaseType_t xSwitchRequired = false;
  if (SchedulerSuspended == (UBaseType_t)0U) {
    const TickType_t ConstTickCount = TickCount + (TickType_t)1;
    TickCount = ConstTickCount;
    if (ConstTickCount == (TickType_t)0U) {
      taskSWITCH_DELAYED_LISTS();
    }
    if (ConstTickCount >= NextTaskUnblockTime) {
      for (;;) {
        if (DelayedTaskList->empty()) {
          NextTaskUnblockTime = portMAX_DELAY;
          break;
        } else {
          pxTCB = DelayedTaskList->head()->Owner;
          Value = pxTCB->StateListItem.Value;
          if (ConstTickCount < Value) {
            NextTaskUnblockTime = Value;
            break;
          }
          pxTCB->StateListItem.remove();
          pxTCB->xEventListItem.ensureRemoved();
          AddTaskToReadyList(pxTCB);
          if (pxTCB->Priority > CurrentTCB->Priority) {
            xSwitchRequired = true;
          }
        }
      }
    }
    if (ReadyTasksLists[CurrentTCB->Priority].Length > 1U) {
      xSwitchRequired = true;
    }
    if (PendedTicks == (TickType_t)0) {
      vApplicationTickHook();
    }
    if (YieldPendings[0] != false) {
      xSwitchRequired = true;
    }
  } else {
    PendedTicks += 1U;
 
#if (configUSE_TICK_HOOK == 1)
    { vApplicationTickHook(); }
#endif
  }

  return xSwitchRequired;
}

void vTaskSwitchContext(void) {
  if (SchedulerSuspended != (UBaseType_t)0U) {
    YieldPendings[0] = true;
  } else {
    YieldPendings[0] = false;
    taskSELECT_HIGHEST_PRIORITY_TASK();
    portTASK_SWITCH_HOOK(CurrentTCB);
  }
}

void vTaskPlaceOnEventList(List_t<TCB_t> *const pxEventList,
                           const TickType_t TicksToWait) {
  configASSERT(pxEventList);
  pxEventList->insert(&(CurrentTCB->xEventListItem));
  AddCurrentTaskToDelayedList(TicksToWait, true);
}

void vTaskPlaceOnUnorderedEventList(List_t<TCB_t> *pxEventList,
                                    const TickType_t Value,
                                    const TickType_t TicksToWait) {
  configASSERT(pxEventList);
  configASSERT(SchedulerSuspended != (UBaseType_t)0U);
  CurrentTCB->xEventListItem.Value = Value | taskEVENT_LIST_ITEM_VALUE_IN_USE;
  pxEventList->append(&(CurrentTCB->xEventListItem));
  AddCurrentTaskToDelayedList(TicksToWait, true);
}

void vTaskPlaceOnEventListRestricted(List_t<TCB_t> *const pxEventList,
                                     TickType_t TicksToWait,
                                     const BaseType_t xWaitIndefinitely) {
  configASSERT(pxEventList);
  pxEventList->append(&(CurrentTCB->xEventListItem));
  AddCurrentTaskToDelayedList(xWaitIndefinitely ? portMAX_DELAY : TicksToWait,
                              xWaitIndefinitely);
}

BaseType_t xTaskRemoveFromEventList(List_t<TCB_t> *const pxEventList) {
  TCB_t *UnblockedTCB = pxEventList->head()->Owner;
  configASSERT(UnblockedTCB);
  UnblockedTCB->xEventListItem.remove();
  if (SchedulerSuspended == (UBaseType_t)0U) {
    UnblockedTCB->StateListItem.remove();
    AddTaskToReadyList(UnblockedTCB);
  } else {
    PendingReadyList.append(&UnblockedTCB->xEventListItem);
  }
  if (UnblockedTCB->Priority > CurrentTCB->Priority) {
    YieldPendings[0] = true;
    return true;
  }
  return false;
}

void vTaskRemoveFromUnorderedEventList(Item_t<TCB_t> *EventListItem,
                                       const TickType_t ItemValue) {
  configASSERT(SchedulerSuspended != (UBaseType_t)0U);
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
void vTaskSetTimeOutState(TimeOut_t *const TimeOut) {
  configASSERT(TimeOut);
  ENTER_CRITICAL();
  TimeOut->xOverflowCount = NumOfOverflows;
  TimeOut->xTimeOnEntering = TickCount;
  EXIT_CRITICAL();
}
void vTaskInternalSetTimeOutState(TimeOut_t *const pxTimeOut) {
   
  pxTimeOut->xOverflowCount = NumOfOverflows;
  pxTimeOut->xTimeOnEntering = TickCount;
}
BaseType_t xTaskCheckForTimeOut(TimeOut_t *const pxTimeOut,
                                TickType_t *const pTicksToWait) {
  BaseType_t xReturn;
  configASSERT(pxTimeOut);
  configASSERT(pTicksToWait);
  CriticalSection s;
  const TickType_t ConstTickCount = TickCount;
  const TickType_t xElapsedTime = ConstTickCount - pxTimeOut->xTimeOnEntering;
  if (CurrentTCB->DelayAborted != (uint8_t) false) {
    CurrentTCB->DelayAborted = (uint8_t) false;
    return true;
  }

  if (*pTicksToWait == portMAX_DELAY) {
    return false;
  }

  if ((NumOfOverflows != pxTimeOut->xOverflowCount) &&
      (ConstTickCount >= pxTimeOut->xTimeOnEntering)) {
    *pTicksToWait = (TickType_t)0;
    return true;
  }

  if (xElapsedTime < *pTicksToWait) {
    *pTicksToWait -= xElapsedTime;
    vTaskInternalSetTimeOutState(pxTimeOut);
    return false;
  }

  *pTicksToWait = (TickType_t)0;
  return true;
}

void vTaskMissedYield(void) { YieldPendings[portGET_CORE_ID()] = true; }

static void IdleTask(void *) {
  portALLOCATE_SECURE_CONTEXT(configMINIMAL_SECURE_STACK_SIZE);
  for (; configCONTROL_INFINITE_LOOP();) {
    CheckTasksWaitingTermination();
    if (ReadyTasksLists[tskIDLE_PRIORITY].Length >
        (UBaseType_t)configNUMBER_OF_CORES) {
      taskYIELD();
    }
    vApplicationIdleHook();
  }
}

static void InitialiseTaskLists(void) {
  for (int pri = 0; pri < configMAX_PRIORITIES; pri++) {
    (ReadyTasksLists[pri]).init();
  }
  DelayedTaskList1.init();
  DelayedTaskList2.init();
  PendingReadyList.init();
  xTasksWaitingTermination.init();
  SuspendedTaskList.init();
  DelayedTaskList = &DelayedTaskList1;
  OverflowDelayedTaskList = &DelayedTaskList2;
}

static void CheckTasksWaitingTermination(void) {
  while (DeletedTasksWaitingCleanUp > 0) {
    TCB_t *pxTCB;
    {
      CriticalSection s;
      pxTCB = xTasksWaitingTermination.head()->Owner;
      pxTCB->StateListItem.remove();
      --CurrentNumberOfTasks;
      --DeletedTasksWaitingCleanUp;
    }
    DeleteTCB(pxTCB);
  }
}

static void DeleteTCB(TCB_t *pxTCB) {
  portCLEAN_UP_TCB(pxTCB);
  if (pxTCB->StaticallyAllocated == tskDYNAMICALLY_ALLOCATED_STACK_AND_TCB) {
    vPortFreeStack(pxTCB->pxStack);
    vPortFree(pxTCB);
  } else if (pxTCB->StaticallyAllocated == tskSTATICALLY_ALLOCATED_STACK_ONLY) {
    vPortFree(pxTCB);
  } else {
    configASSERT(pxTCB->StaticallyAllocated ==
                 tskSTATICALLY_ALLOCATED_STACK_AND_TCB);
  }
}

static void ResetNextTaskUnblockTime(void) {
  NextTaskUnblockTime =
      DelayedTaskList->empty() ? portMAX_DELAY : DelayedTaskList->head()->Value;
}

TaskHandle_t xTaskGetCurrentTaskHandle(void) { return CurrentTCB; }

BaseType_t xTaskGetSchedulerState(void) {
  return SchedulerRunning ? SchedulerSuspended ? taskSCHEDULER_SUSPENDED
                                               : taskSCHEDULER_RUNNING
                          : taskSCHEDULER_NOT_STARTED;
}

BaseType_t xTaskPriorityInherit(TaskHandle_t const pMutexHolder) {
  TCB_t *const pMutexHolderTCB = pMutexHolder;
  if (pMutexHolder == NULL) {
    return false;
  }
  if (pMutexHolderTCB->Priority >= CurrentTCB->Priority) {
    return pMutexHolderTCB->uxBasePriority < CurrentTCB->Priority;
  }
  if ((pMutexHolderTCB->xEventListItem.Value &
       taskEVENT_LIST_ITEM_VALUE_IN_USE) == ((TickType_t)0U)) {
    pMutexHolderTCB->xEventListItem.Value =
        (TickType_t)configMAX_PRIORITIES - (TickType_t)CurrentTCB->Priority;
  }
  if (pMutexHolderTCB->StateListItem.Container ==
      &(ReadyTasksLists[pMutexHolderTCB->Priority])) {
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

BaseType_t xTaskPriorityDisinherit(TaskHandle_t const pMutexHolder) {
  TCB_t *const pxTCB = pMutexHolder;
  BaseType_t xReturn = false;
  if (pMutexHolder == NULL) {
    return false;
  }
  configASSERT(pxTCB == CurrentTCB);
  configASSERT(pxTCB->uxMutexesHeld);
  pxTCB->uxMutexesHeld--;
  if (pxTCB->Priority == pxTCB->uxBasePriority) {
    return false;
  }
  if (pxTCB->uxMutexesHeld > 0) {
    return false;
  }
  if (pxTCB->StateListItem.remove() == 0) {
    portRESET_READY_PRIORITY(pxTCB->Priority, TopReadyPriority);
  }
  pxTCB->Priority = pxTCB->uxBasePriority;
  pxTCB->xEventListItem.Value =
      (TickType_t)configMAX_PRIORITIES - (TickType_t)pxTCB->Priority;
  AddTaskToReadyList(pxTCB);
  return true;
}

void vTaskPriorityDisinheritAfterTimeout(
    TaskHandle_t const pMutexHolder, UBaseType_t uxHighestPriorityWaitingTask) {
  TCB_t *const pxTCB = pMutexHolder;
  UBaseType_t PriorityUsedOnEntry, PriorityToUse;
  const UBaseType_t uxOnlyOneMutexHeld = (UBaseType_t)1;
  if (pMutexHolder != NULL) {
    configASSERT(pxTCB->uxMutexesHeld);
    if (pxTCB->uxBasePriority < uxHighestPriorityWaitingTask) {
      PriorityToUse = uxHighestPriorityWaitingTask;
    } else {
      PriorityToUse = pxTCB->uxBasePriority;
    }
    if (pxTCB->Priority != PriorityToUse) {
      if (pxTCB->uxMutexesHeld == uxOnlyOneMutexHeld) {
        configASSERT(pxTCB != CurrentTCB);
        PriorityUsedOnEntry = pxTCB->Priority;
        pxTCB->Priority = PriorityToUse;
        if ((pxTCB->xEventListItem.Value & taskEVENT_LIST_ITEM_VALUE_IN_USE) ==
            ((TickType_t)0U)) {
          pxTCB->xEventListItem.Value =
              (TickType_t)configMAX_PRIORITIES - (TickType_t)PriorityToUse;
        }
        if (pxTCB->StateListItem.Container ==
            &ReadyTasksLists[PriorityUsedOnEntry]) {
          if (pxTCB->StateListItem.remove() == 0) {
            portRESET_READY_PRIORITY(pxTCB->Priority, TopReadyPriority);
          }
          AddTaskToReadyList(pxTCB);
        }
      }
    }
  }
}

void TaskEnterCritical(void) {
  portDISABLE_INTERRUPTS();
  if (SchedulerRunning != false) {
    (CurrentTCB->uxCriticalNesting)++;
    if (CurrentTCB->uxCriticalNesting == 1U) {
      portASSERT_IF_IN_ISR();
    }
  }
}

void TaskExitCritical(void) {
  if (!SchedulerRunning) {
    return;
  }
  configASSERT(CurrentTCB->uxCriticalNesting > 0U);
  portASSERT_IF_IN_ISR();
  if (CurrentTCB->uxCriticalNesting > 0U) {
    (CurrentTCB->uxCriticalNesting)--;
    if (CurrentTCB->uxCriticalNesting == 0U) {
      portENABLE_INTERRUPTS();
    }
  }
}

TickType_t uxTaskResetEventItemValue(void) {
  TickType_t uxReturn = CurrentTCB->xEventListItem.Value;
  CurrentTCB->xEventListItem.Value =
      (TickType_t)configMAX_PRIORITIES - (TickType_t)CurrentTCB->Priority;
  return uxReturn;
}

TaskHandle_t pvTaskIncrementMutexHeldCount(void) {
  TCB_t *pxTCB = CurrentTCB;
  if (pxTCB != NULL) {
    (pxTCB->uxMutexesHeld)++;
  }
  return pxTCB;
}

uint32_t ulTaskGenericNotifyTake(UBaseType_t uxIndexToWaitOn,
                                 BaseType_t xClearCountOnExit,
                                 TickType_t TicksToWait) {
  uint32_t ulReturn;
  BaseType_t xAlreadyYielded, xShouldBlock = false;
  configASSERT(uxIndexToWaitOn < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  vTaskSuspendAll();
  {
    CriticalSection s;
    if (CurrentTCB->NotifiedValue[uxIndexToWaitOn] == 0U) {
      CurrentTCB->ucNotifyState[uxIndexToWaitOn] = taskWAITING_NOTIFICATION;
      if (TicksToWait > (TickType_t)0) {
        xShouldBlock = true;
      }
    }
  }
  if (xShouldBlock) {
    AddCurrentTaskToDelayedList(TicksToWait, true);
  }
  xAlreadyYielded = TaskResumeAll();
  if ((xShouldBlock) && (xAlreadyYielded == false)) {
    taskYIELD_WITHIN_API();
  }

  ENTER_CRITICAL();
  {
    ulReturn = CurrentTCB->NotifiedValue[uxIndexToWaitOn];
    if (ulReturn != 0U) {
      if (xClearCountOnExit != false) {
        CurrentTCB->NotifiedValue[uxIndexToWaitOn] = (uint32_t)0U;
      } else {
        CurrentTCB->NotifiedValue[uxIndexToWaitOn] = ulReturn - (uint32_t)1;
      }
    }

    CurrentTCB->ucNotifyState[uxIndexToWaitOn] = taskNOT_WAITING_NOTIFICATION;
  }
  EXIT_CRITICAL();
  return ulReturn;
}

BaseType_t TaskGenericNotifyWait(UBaseType_t uxIndexToWaitOn,
                                 uint32_t ulBitsToClearOnEntry,
                                 uint32_t ulBitsToClearOnExit,
                                 uint32_t *pulNotificationValue,
                                 TickType_t TicksToWait) {
  BaseType_t xReturn, xAlreadyYielded, xShouldBlock = false;
  configASSERT(uxIndexToWaitOn < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  vTaskSuspendAll();
  {
    CriticalSection s;
    if (CurrentTCB->ucNotifyState[uxIndexToWaitOn] !=
        taskNOTIFICATION_RECEIVED) {
      CurrentTCB->NotifiedValue[uxIndexToWaitOn] &= ~ulBitsToClearOnEntry;
      CurrentTCB->ucNotifyState[uxIndexToWaitOn] = taskWAITING_NOTIFICATION;
      if (TicksToWait > (TickType_t)0) {
        xShouldBlock = true;
      }
    }
  }
  if (xShouldBlock) {
    AddCurrentTaskToDelayedList(TicksToWait, true);
  }
  xAlreadyYielded = TaskResumeAll();
  if ((xShouldBlock) && (xAlreadyYielded == false)) {
    taskYIELD_WITHIN_API();
  }

  CriticalSection s;
  if (pulNotificationValue != NULL) {
    *pulNotificationValue = CurrentTCB->NotifiedValue[uxIndexToWaitOn];
  }
  if (CurrentTCB->ucNotifyState[uxIndexToWaitOn] != taskNOTIFICATION_RECEIVED) {
    xReturn = false;
  } else {
    CurrentTCB->NotifiedValue[uxIndexToWaitOn] &= ~ulBitsToClearOnExit;
    xReturn = true;
  }
  CurrentTCB->ucNotifyState[uxIndexToWaitOn] = taskNOT_WAITING_NOTIFICATION;
  return xReturn;
}

BaseType_t TaskGenericNotify(TaskHandle_t xTaskToNotify,
                             UBaseType_t uxIndexToNotify, uint32_t ulValue,
                             eNotifyAction eAction,
                             uint32_t *pulPreviousNotificationValue) {
  TCB_t *pxTCB;
  BaseType_t xReturn = true;
  uint8_t ucOriginalNotifyState;
  configASSERT(uxIndexToNotify < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  configASSERT(xTaskToNotify);
  pxTCB = xTaskToNotify;
  CriticalSection s;
  if (pulPreviousNotificationValue != NULL) {
    *pulPreviousNotificationValue = pxTCB->NotifiedValue[uxIndexToNotify];
  }
  ucOriginalNotifyState = pxTCB->ucNotifyState[uxIndexToNotify];
  pxTCB->ucNotifyState[uxIndexToNotify] = taskNOTIFICATION_RECEIVED;
  switch (eAction) {
    case eSetBits:
      pxTCB->NotifiedValue[uxIndexToNotify] |= ulValue;
      break;
    case eIncrement:
      (pxTCB->NotifiedValue[uxIndexToNotify])++;
      break;
    case eSetValueWithOverwrite:
      pxTCB->NotifiedValue[uxIndexToNotify] = ulValue;
      break;
    case eSetValueWithoutOverwrite:
      if (ucOriginalNotifyState != taskNOTIFICATION_RECEIVED) {
        pxTCB->NotifiedValue[uxIndexToNotify] = ulValue;
      } else {
        xReturn = false;
      }
      break;
    case eNoAction:
      break;
    default:
      configASSERT(TickCount == (TickType_t)0);
      break;
  }
  if (ucOriginalNotifyState == taskWAITING_NOTIFICATION) {
    pxTCB->StateListItem.remove();
    AddTaskToReadyList(pxTCB);
    configASSERT(pxTCB->xEventListItem.Container == NULL);
    taskYIELD_ANY_CORE_IF_USING_PREEMPTION(pxTCB);
  }
  return xReturn;
}

BaseType_t TaskGenericNotifyFromISR(TaskHandle_t xTaskToNotify,
                                    UBaseType_t uxIndexToNotify,
                                    uint32_t ulValue, eNotifyAction eAction,
                                    uint32_t *pulPreviousNotificationValue,
                                    BaseType_t *pxHigherPriorityTaskWoken) {
  TCB_t *pxTCB;
  uint8_t ucOriginalNotifyState;
  BaseType_t xReturn = true;
  UBaseType_t uxSavedInterruptStatus;
  configASSERT(xTaskToNotify);
  configASSERT(uxIndexToNotify < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  pxTCB = xTaskToNotify;
  uxSavedInterruptStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
    if (pulPreviousNotificationValue != NULL) {
      *pulPreviousNotificationValue = pxTCB->NotifiedValue[uxIndexToNotify];
    }
    ucOriginalNotifyState = pxTCB->ucNotifyState[uxIndexToNotify];
    pxTCB->ucNotifyState[uxIndexToNotify] = taskNOTIFICATION_RECEIVED;
    switch (eAction) {
      case eSetBits:
        pxTCB->NotifiedValue[uxIndexToNotify] |= ulValue;
        break;
      case eIncrement:
        (pxTCB->NotifiedValue[uxIndexToNotify])++;
        break;
      case eSetValueWithOverwrite:
        pxTCB->NotifiedValue[uxIndexToNotify] = ulValue;
        break;
      case eSetValueWithoutOverwrite:
        if (ucOriginalNotifyState != taskNOTIFICATION_RECEIVED) {
          pxTCB->NotifiedValue[uxIndexToNotify] = ulValue;
        } else {
          xReturn = false;
        }
        break;
      case eNoAction:
        break;
      default:
        configASSERT(TickCount == (TickType_t)0);
        break;
    }
    if (ucOriginalNotifyState == taskWAITING_NOTIFICATION) {
      configASSERT(pxTCB->xEventListItem.Container == NULL);
      if (SchedulerSuspended == (UBaseType_t)0U) {
        pxTCB->StateListItem.remove();
        AddTaskToReadyList(pxTCB);
      } else {
        PendingReadyList.append(&pxTCB->xEventListItem);
      }
      if (pxTCB->Priority > CurrentTCB->Priority) {
        if (pxHigherPriorityTaskWoken != NULL) {
          *pxHigherPriorityTaskWoken = true;
        }
        YieldPendings[0] = true;
      }
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return xReturn;
}

void vTaskGenericNotifyGiveFromISR(TaskHandle_t xTaskToNotify,
                                   UBaseType_t uxIndexToNotify,
                                   BaseType_t *pxHigherPriorityTaskWoken) {
  TCB_t *pxTCB;
  uint8_t ucOriginalNotifyState;
  UBaseType_t uxSavedInterruptStatus;
  configASSERT(xTaskToNotify);
  configASSERT(uxIndexToNotify < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  pxTCB = xTaskToNotify;
  uxSavedInterruptStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
    ucOriginalNotifyState = pxTCB->ucNotifyState[uxIndexToNotify];
    pxTCB->ucNotifyState[uxIndexToNotify] = taskNOTIFICATION_RECEIVED;
    (pxTCB->NotifiedValue[uxIndexToNotify])++;
    if (ucOriginalNotifyState == taskWAITING_NOTIFICATION) {
      configASSERT(pxTCB->xEventListItem.Container == NULL);
      if (SchedulerSuspended == (UBaseType_t)0U) {
        pxTCB->StateListItem.remove();
        AddTaskToReadyList(pxTCB);
      } else {
        PendingReadyList.append(&pxTCB->xEventListItem);
      }
      if (pxTCB->Priority > CurrentTCB->Priority) {
        if (pxHigherPriorityTaskWoken != NULL) {
          *pxHigherPriorityTaskWoken = true;
        }
        YieldPendings[0] = true;
      }
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
}

BaseType_t xTaskGenericNotifyStateClear(TaskHandle_t xTask,
                                        UBaseType_t IndexToClear) {
  TCB_t *pxTCB;
  BaseType_t xReturn;
  configASSERT(IndexToClear < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  pxTCB = GetTCBFromHandle(xTask);
  CriticalSection s;
  bool received =
      pxTCB->ucNotifyState[IndexToClear] == taskNOTIFICATION_RECEIVED;
  if (received) {
    pxTCB->ucNotifyState[IndexToClear] = taskNOT_WAITING_NOTIFICATION;
  }
  return received;
}

uint32_t ulTaskGenericNotifyValueClear(TaskHandle_t xTask,
                                       UBaseType_t IndexToClear,
                                       uint32_t ulBitsToClear) {
  configASSERT(IndexToClear < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  TCB_t *pxTCB = GetTCBFromHandle(xTask);
  CriticalSection s;
  uint32_t ulReturn = pxTCB->NotifiedValue[IndexToClear];
  pxTCB->NotifiedValue[IndexToClear] &= ~ulBitsToClear;
  return ulReturn;
}

static void AddCurrentTaskToDelayedList(TickType_t TicksToWait,
                                        const BaseType_t CanBlockIndefinitely) {
  TickType_t TimeToWake;
  const TickType_t ConstTickCount = TickCount;
  auto *DelayedList = DelayedTaskList;
  auto *OverflowDelayedList = OverflowDelayedTaskList;
  CurrentTCB->DelayAborted = (uint8_t) false;
  if (CurrentTCB->StateListItem.remove() == (UBaseType_t)0) {
    portRESET_READY_PRIORITY(CurrentTCB->Priority, TopReadyPriority);
  }
  if ((TicksToWait == portMAX_DELAY) && (CanBlockIndefinitely != false)) {
    SuspendedTaskList.append(&CurrentTCB->StateListItem);
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

void ApplicationGetIdleTaskMemory(StaticTask_t **TCBBuffer,
                                  StackType_t **StackBuffer,
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
  DeletedTasksWaitingCleanUp = (UBaseType_t)0U;
  CurrentNumberOfTasks = (UBaseType_t)0U;
  TickCount = (TickType_t)configINITIAL_TICK_COUNT;
  TopReadyPriority = tskIDLE_PRIORITY;
  SchedulerRunning = false;
  PendedTicks = (TickType_t)0U;
  for (xCoreID = 0; xCoreID < configNUMBER_OF_CORES; xCoreID++) {
    YieldPendings[xCoreID] = false;
  }
  NumOfOverflows = (BaseType_t)0;
  TaskNumber = (UBaseType_t)0U;
  NextTaskUnblockTime = (TickType_t)0U;
  SchedulerSuspended = (UBaseType_t)0U;
}
