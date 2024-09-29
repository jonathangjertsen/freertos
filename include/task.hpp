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
 
#define tskMPU_REGION_READ_ONLY (1U << 0U)
#define tskMPU_REGION_READ_WRITE (1U << 1U)
#define tskMPU_REGION_EXECUTE_NEVER (1U << 2U)
#define tskMPU_REGION_NORMAL_MEMORY (1U << 3U)
#define tskMPU_REGION_DEVICE_MEMORY (1U << 4U)
#if defined(portARMV8M_MINOR_VERSION) && (portARMV8M_MINOR_VERSION >= 1)
#define tskMPU_REGION_PRIVILEGED_EXECUTE_NEVER (1U << 5U)
#endif  
 
#define tskMPU_READ_PERMISSION (1U << 0U)
#define tskMPU_WRITE_PERMISSION (1U << 1U)
 
#define tskDEFAULT_INDEX_TO_NOTIFY (0)
 
struct TCB_t;  
typedef struct TCB_t *TaskHandle_t;
typedef const struct TCB_t *ConstTaskHandle_t;
 
typedef BaseType_t (*TaskHookFunction_t)(void *arg);
 
typedef enum {
  eRunning =
      0,       
  eReady,      
  eBlocked,    
  eSuspended,  
  eDeleted,  
  eInvalid   
} eTaskState;
 
typedef enum {
  eNoAction = 0,  
  eSetBits,       
  eIncrement,     
  eSetValueWithOverwrite,    
  eSetValueWithoutOverwrite  
} eNotifyAction;
 
typedef struct xTIME_OUT {
  BaseType_t xOverflowCount;
  TickType_t xTimeOnEntering;
} TimeOut_t;
 
typedef struct xMEMORY_REGION {
  void *pvBaseAddress;
  uint32_t ulLengthInBytes;
  uint32_t ulParameters;
} MemoryRegion_t;
 
typedef struct xTASK_PARAMETERS {
  TaskFunction_t pvTaskCode;
  const char *pcName;
  configSTACK_DEPTH_TYPE usStackDepth;
  void *pvParameters;
  UBaseType_t uxPriority;
  StackType_t *puxStackBuffer;
  MemoryRegion_t xRegions[portNUM_CONFIGURABLE_REGIONS];
#if ((portUSING_MPU_WRAPPERS == 1) && (configSUPPORT_STATIC_ALLOCATION == 1))
  StaticTask_t *const pxTaskBuffer;
#endif
} TaskParameters_t;
 
typedef struct xTASK_STATUS {
  TaskHandle_t xHandle;     
  const char *pcTaskName;   
  UBaseType_t xTaskNumber;  
  eTaskState eCurrentState;  
  UBaseType_t
      uxCurrentPriority;  
  UBaseType_t
      uxBasePriority;  
  configRUN_TIME_COUNTER_TYPE
      ulRunTimeCounter;  
  StackType_t
      *pxStackBase;  
#if ((portSTACK_GROWTH > 0) || (configRECORD_STACK_HIGH_ADDRESS == 1))
  StackType_t
      *pxTopOfStack;  
  StackType_t
      *pxEndOfStack;  
#endif
  configSTACK_DEPTH_TYPE
      usStackHighWaterMark;  
#if ((configUSE_CORE_AFFINITY == 1) && (configNUMBER_OF_CORES > 1))
  UBaseType_t uxCoreAffinityMask;  
#endif
} TaskStatus_t;
 
typedef enum {
  eAbortSleep = 0,  
  eStandardSleep    
#if (INCLUDE_vTaskSuspend == 1)
  ,
  eNoTasksWaitingTimeout  
#endif                    
} eSleepModeStatus;
 
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
 
 
#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
BaseType_t xTaskCreate(TaskFunction_t pxTaskCode, const char *const pcName,
                       const configSTACK_DEPTH_TYPE uxStackDepth,
                       void *const pvParameters, UBaseType_t uxPriority,
                       TaskHandle_t *const pxCreatedTask);
#endif
#if ((configSUPPORT_DYNAMIC_ALLOCATION == 1) && (configNUMBER_OF_CORES > 1) && \
     (configUSE_CORE_AFFINITY == 1))
BaseType_t xTaskCreateAffinitySet(TaskFunction_t pxTaskCode,
                                  const char *const pcName,
                                  const configSTACK_DEPTH_TYPE uxStackDepth,
                                  void *const pvParameters,
                                  UBaseType_t uxPriority,
                                  UBaseType_t uxCoreAffinityMask,
                                  TaskHandle_t *const pxCreatedTask);
#endif
 
#if (configSUPPORT_STATIC_ALLOCATION == 1)
TaskHandle_t xTaskCreateStatic(TaskFunction_t pxTaskCode,
                               const char *const pcName,
                               const configSTACK_DEPTH_TYPE uxStackDepth,
                               void *const pvParameters, UBaseType_t uxPriority,
                               StackType_t *const puxStackBuffer,
                               StaticTask_t *const pxTaskBuffer);
#endif  
#if ((configSUPPORT_STATIC_ALLOCATION == 1) && (configNUMBER_OF_CORES > 1) && \
     (configUSE_CORE_AFFINITY == 1))
TaskHandle_t xTaskCreateStaticAffinitySet(
    TaskFunction_t pxTaskCode, const char *const pcName,
    const configSTACK_DEPTH_TYPE uxStackDepth, void *const pvParameters,
    UBaseType_t uxPriority, StackType_t *const puxStackBuffer,
    StaticTask_t *const pxTaskBuffer, UBaseType_t uxCoreAffinityMask);
#endif
 
#if (portUSING_MPU_WRAPPERS == 1)
BaseType_t xTaskCreateRestricted(const TaskParameters_t *const pxTaskDefinition,
                                 TaskHandle_t *pxCreatedTask);
#endif
#if ((portUSING_MPU_WRAPPERS == 1) && (configNUMBER_OF_CORES > 1) && \
     (configUSE_CORE_AFFINITY == 1))
BaseType_t xTaskCreateRestrictedAffinitySet(
    const TaskParameters_t *const pxTaskDefinition,
    UBaseType_t uxCoreAffinityMask, TaskHandle_t *pxCreatedTask);
#endif
 
#if ((portUSING_MPU_WRAPPERS == 1) && (configSUPPORT_STATIC_ALLOCATION == 1))
BaseType_t xTaskCreateRestrictedStatic(
    const TaskParameters_t *const pxTaskDefinition,
    TaskHandle_t *pxCreatedTask);
#endif
#if ((portUSING_MPU_WRAPPERS == 1) &&                                         \
     (configSUPPORT_STATIC_ALLOCATION == 1) && (configNUMBER_OF_CORES > 1) && \
     (configUSE_CORE_AFFINITY == 1))
BaseType_t xTaskCreateRestrictedStaticAffinitySet(
    const TaskParameters_t *const pxTaskDefinition,
    UBaseType_t uxCoreAffinityMask, TaskHandle_t *pxCreatedTask);
#endif
 
#if (portUSING_MPU_WRAPPERS == 1)
void vTaskAllocateMPURegions(TaskHandle_t xTaskToModify,
                             const MemoryRegion_t *const pxRegions);
#endif
 
void vTaskDelete(TaskHandle_t xTaskToDelete);
 
 
void vTaskDelay(const TickType_t xTicksToDelay);
 
BaseType_t xTaskDelayUntil(TickType_t *const pxPreviousWakeTime,
                           const TickType_t xTimeIncrement);
 
#define vTaskDelayUntil(pxPreviousWakeTime, xTimeIncrement)        \
  do {                                                             \
    (void)xTaskDelayUntil((pxPreviousWakeTime), (xTimeIncrement)); \
  } while (0)

 
#if (INCLUDE_xTaskAbortDelay == 1)
BaseType_t xTaskAbortDelay(TaskHandle_t xTask);
#endif
 
UBaseType_t uxTaskPriorityGet(const TaskHandle_t xTask);
 
UBaseType_t uxTaskPriorityGetFromISR(const TaskHandle_t xTask);
 
UBaseType_t uxTaskBasePriorityGet(const TaskHandle_t xTask);
 
UBaseType_t uxTaskBasePriorityGetFromISR(const TaskHandle_t xTask);
 
#if ((INCLUDE_eTaskGetState == 1) || (configUSE_TRACE_FACILITY == 1) || \
     (INCLUDE_xTaskAbortDelay == 1))
eTaskState eTaskGetState(TaskHandle_t xTask);
#endif
 
#if (configUSE_TRACE_FACILITY == 1)
void vTaskGetInfo(TaskHandle_t xTask, TaskStatus_t *pxTaskStatus,
                  BaseType_t xGetFreeStackSpace, eTaskState eState);
#endif
 
void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority);
 
void vTaskSuspend(TaskHandle_t xTaskToSuspend);
 
void vTaskResume(TaskHandle_t xTaskToResume);
 
BaseType_t xTaskResumeFromISR(TaskHandle_t xTaskToResume);
#if (configUSE_CORE_AFFINITY == 1)
 
void vTaskCoreAffinitySet(const TaskHandle_t xTask,
                          UBaseType_t uxCoreAffinityMask);
#endif
#if ((configNUMBER_OF_CORES > 1) && (configUSE_CORE_AFFINITY == 1))
 
UBaseType_t vTaskCoreAffinityGet(ConstTaskHandle_t xTask);
#endif
#if (configUSE_TASK_PREEMPTION_DISABLE == 1)
 
void vTaskPreemptionDisable(const TaskHandle_t xTask);
#endif
#if (configUSE_TASK_PREEMPTION_DISABLE == 1)
 
void vTaskPreemptionEnable(const TaskHandle_t xTask);
#endif
 
 
void vTaskStartScheduler(void);
 
void vTaskEndScheduler(void);
 
void vTaskSuspendAll(void);
 
BaseType_t TaskResumeAll(void);
 
 
TickType_t xTaskGetTickCount(void);
 
TickType_t xTaskGetTickCountFromISR(void);
 
UBaseType_t TaskGetNumberOfTasks(void);
 
char *pcTaskGetName(TaskHandle_t xTaskToQuery);
 
#if (INCLUDE_xTaskGetHandle == 1)
TaskHandle_t xTaskGetHandle(const char *pcNameToQuery);
#endif
 
#if (configSUPPORT_STATIC_ALLOCATION == 1)
BaseType_t xTaskGetStaticBuffers(TaskHandle_t xTask,
                                 StackType_t **ppuxStackBuffer,
                                 StaticTask_t **ppxTaskBuffer);
#endif  
 
#if (INCLUDE_uxTaskGetStackHighWaterMark == 1)
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t xTask);
#endif
 
#if (INCLUDE_uxTaskGetStackHighWaterMark2 == 1)
configSTACK_DEPTH_TYPE uxTaskGetStackHighWaterMark2(TaskHandle_t xTask);
#endif
 
#ifdef configUSE_APPLICATION_TASK_TAG
#if configUSE_APPLICATION_TASK_TAG == 1
 
void vTaskSetApplicationTaskTag(TaskHandle_t xTask,
                                TaskHookFunction_t pxHookFunction);
 
TaskHookFunction_t xTaskGetApplicationTaskTag(TaskHandle_t xTask);
 
TaskHookFunction_t xTaskGetApplicationTaskTagFromISR(TaskHandle_t xTask);
#endif  
#endif  
#if (configNUM_THREAD_LOCAL_STORAGE_POINTERS > 0)
 
void vTaskSetThreadLocalStoragePointer(TaskHandle_t xTaskToSet,
                                       BaseType_t xIndex, void *pvValue);
void *pvTaskGetThreadLocalStoragePointer(TaskHandle_t xTaskToQuery,
                                         BaseType_t xIndex);
#endif
#if (configCHECK_FOR_STACK_OVERFLOW > 0)
 

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName);
#endif
#if (configUSE_IDLE_HOOK == 1)
 

void vApplicationIdleHook(void);
#endif

#if (configUSE_TICK_HOOK != 0)
 

void vApplicationTickHook(void);
#endif
#if (configSUPPORT_STATIC_ALLOCATION == 1)
 
void ApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                  StackType_t **ppxIdleTaskStackBuffer,
                                  configSTACK_DEPTH_TYPE *puxIdleTaskStackSize);
 
#if (configNUMBER_OF_CORES > 1)
void vApplicationGetPassiveIdleTaskMemory(
    StaticTask_t **ppxIdleTaskTCBBuffer, StackType_t **ppxIdleTaskStackBuffer,
    configSTACK_DEPTH_TYPE *puxIdleTaskStackSize,
    BaseType_t xPassiveIdleTaskIndex);
#endif  
#endif  
 
#if (configUSE_APPLICATION_TASK_TAG == 1)
BaseType_t xTaskCallApplicationTaskHook(TaskHandle_t xTask, void *pvParameter);
#endif
 
#if (INCLUDE_xTaskGetIdleTaskHandle == 1)
#if (configNUMBER_OF_CORES == 1)
TaskHandle_t xTaskGetIdleTaskHandle(void);
#endif  
TaskHandle_t xTaskGetIdleTaskHandleForCore(BaseType_t xCoreID);
#endif  
 
#if (configUSE_TRACE_FACILITY == 1)
UBaseType_t uxTaskGetSystemState(
    TaskStatus_t *const pxTaskStatusArray, const UBaseType_t uxArraySize,
    configRUN_TIME_COUNTER_TYPE *const pulTotalRunTime);
#endif
 
#if ((configUSE_TRACE_FACILITY == 1) && \
     (configUSE_STATS_FORMATTING_FUNCTIONS > 0))
void vTaskListTasks(char *pcWriteBuffer, size_t uxBufferLength);
#endif
 
#define vTaskList(pcWriteBuffer) \
  vTaskListTasks((pcWriteBuffer), configSTATS_BUFFER_MAX_LENGTH)
 
#if ((configGENERATE_RUN_TIME_STATS == 1) &&       \
     (configUSE_STATS_FORMATTING_FUNCTIONS > 0) && \
     (configUSE_TRACE_FACILITY == 1))
void vTaskGetRunTimeStatistics(char *pcWriteBuffer, size_t uxBufferLength);
#endif
 
#define vTaskGetRunTimeStats(pcWriteBuffer) \
  vTaskGetRunTimeStatistics((pcWriteBuffer), configSTATS_BUFFER_MAX_LENGTH)
 
#if (configGENERATE_RUN_TIME_STATS == 1)
configRUN_TIME_COUNTER_TYPE ulTaskGetRunTimeCounter(const TaskHandle_t xTask);
configRUN_TIME_COUNTER_TYPE ulTaskGetRunTimePercent(const TaskHandle_t xTask);
#endif
 
#if ((configGENERATE_RUN_TIME_STATS == 1) && \
     (INCLUDE_xTaskGetIdleTaskHandle == 1))
configRUN_TIME_COUNTER_TYPE ulTaskGetIdleRunTimeCounter(void);
configRUN_TIME_COUNTER_TYPE ulTaskGetIdleRunTimePercent(void);
#endif
 
BaseType_t TaskGenericNotify(TaskHandle_t xTaskToNotify,
                             UBaseType_t uxIndexToNotify, uint32_t ulValue,
                             eNotifyAction eAction,
                             uint32_t *pulPreviousNotificationValue);
#define xTaskNotify(xTaskToNotify, ulValue, eAction)                          \
  TaskGenericNotify((xTaskToNotify), (tskDEFAULT_INDEX_TO_NOTIFY), (ulValue), \
                    (eAction), NULL)
#define xTaskNotifyIndexed(xTaskToNotify, uxIndexToNotify, ulValue, eAction)  \
  TaskGenericNotify((xTaskToNotify), (uxIndexToNotify), (ulValue), (eAction), \
                    NULL)
 
#define xTaskNotifyAndQuery(xTaskToNotify, ulValue, eAction,                  \
                            pulPreviousNotifyValue)                           \
  TaskGenericNotify((xTaskToNotify), (tskDEFAULT_INDEX_TO_NOTIFY), (ulValue), \
                    (eAction), (pulPreviousNotifyValue))
#define xTaskNotifyAndQueryIndexed(xTaskToNotify, uxIndexToNotify, ulValue,   \
                                   eAction, pulPreviousNotifyValue)           \
  TaskGenericNotify((xTaskToNotify), (uxIndexToNotify), (ulValue), (eAction), \
                    (pulPreviousNotifyValue))
 
BaseType_t TaskGenericNotifyFromISR(TaskHandle_t xTaskToNotify,
                                    UBaseType_t uxIndexToNotify,
                                    uint32_t ulValue, eNotifyAction eAction,
                                    uint32_t *pulPreviousNotificationValue,
                                    BaseType_t *pxHigherPriorityTaskWoken);
#define xTaskNotifyFromISR(xTaskToNotify, ulValue, eAction,               \
                           pxHigherPriorityTaskWoken)                     \
  TaskGenericNotifyFromISR((xTaskToNotify), (tskDEFAULT_INDEX_TO_NOTIFY), \
                           (ulValue), (eAction), NULL,                    \
                           (pxHigherPriorityTaskWoken))
#define xTaskNotifyIndexedFromISR(xTaskToNotify, uxIndexToNotify, ulValue, \
                                  eAction, pxHigherPriorityTaskWoken)      \
  TaskGenericNotifyFromISR((xTaskToNotify), (uxIndexToNotify), (ulValue),  \
                           (eAction), NULL, (pxHigherPriorityTaskWoken))
 
#define xTaskNotifyAndQueryIndexedFromISR(                                \
    xTaskToNotify, uxIndexToNotify, ulValue, eAction,                     \
    pulPreviousNotificationValue, pxHigherPriorityTaskWoken)              \
  TaskGenericNotifyFromISR((xTaskToNotify), (uxIndexToNotify), (ulValue), \
                           (eAction), (pulPreviousNotificationValue),     \
                           (pxHigherPriorityTaskWoken))
#define xTaskNotifyAndQueryFromISR(xTaskToNotify, ulValue, eAction,        \
                                   pulPreviousNotificationValue,           \
                                   pxHigherPriorityTaskWoken)              \
  TaskGenericNotifyFromISR(                                                \
      (xTaskToNotify), (tskDEFAULT_INDEX_TO_NOTIFY), (ulValue), (eAction), \
      (pulPreviousNotificationValue), (pxHigherPriorityTaskWoken))
 
BaseType_t TaskGenericNotifyWait(UBaseType_t uxIndexToWaitOn,
                                 uint32_t ulBitsToClearOnEntry,
                                 uint32_t ulBitsToClearOnExit,
                                 uint32_t *pulNotificationValue,
                                 TickType_t xTicksToWait);
#define xTaskNotifyWait(ulBitsToClearOnEntry, ulBitsToClearOnExit,          \
                        pulNotificationValue, xTicksToWait)                 \
  TaskGenericNotifyWait(tskDEFAULT_INDEX_TO_NOTIFY, (ulBitsToClearOnEntry), \
                        (ulBitsToClearOnExit), (pulNotificationValue),      \
                        (xTicksToWait))
#define xTaskNotifyWaitIndexed(uxIndexToWaitOn, ulBitsToClearOnEntry,     \
                               ulBitsToClearOnExit, pulNotificationValue, \
                               xTicksToWait)                              \
  TaskGenericNotifyWait((uxIndexToWaitOn), (ulBitsToClearOnEntry),        \
                        (ulBitsToClearOnExit), (pulNotificationValue),    \
                        (xTicksToWait))
 
#define xTaskNotifyGive(xTaskToNotify)                                  \
  TaskGenericNotify((xTaskToNotify), (tskDEFAULT_INDEX_TO_NOTIFY), (0), \
                    eIncrement, NULL)
#define xTaskNotifyGiveIndexed(xTaskToNotify, uxIndexToNotify) \
  TaskGenericNotify((xTaskToNotify), (uxIndexToNotify), (0), eIncrement, NULL)
 
void vTaskGenericNotifyGiveFromISR(TaskHandle_t xTaskToNotify,
                                   UBaseType_t uxIndexToNotify,
                                   BaseType_t *pxHigherPriorityTaskWoken);
#define vTaskNotifyGiveFromISR(xTaskToNotify, pxHigherPriorityTaskWoken)       \
  vTaskGenericNotifyGiveFromISR((xTaskToNotify), (tskDEFAULT_INDEX_TO_NOTIFY), \
                                (pxHigherPriorityTaskWoken))
#define vTaskNotifyGiveIndexedFromISR(xTaskToNotify, uxIndexToNotify, \
                                      pxHigherPriorityTaskWoken)      \
  vTaskGenericNotifyGiveFromISR((xTaskToNotify), (uxIndexToNotify),   \
                                (pxHigherPriorityTaskWoken))
 
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
 
BaseType_t xTaskGenericNotifyStateClear(TaskHandle_t xTask,
                                        UBaseType_t uxIndexToClear);
#define xTaskNotifyStateClear(xTask) \
  xTaskGenericNotifyStateClear((xTask), (tskDEFAULT_INDEX_TO_NOTIFY))
#define xTaskNotifyStateClearIndexed(xTask, uxIndexToClear) \
  xTaskGenericNotifyStateClear((xTask), (uxIndexToClear))
 
uint32_t ulTaskGenericNotifyValueClear(TaskHandle_t xTask,
                                       UBaseType_t uxIndexToClear,
                                       uint32_t ulBitsToClear);
#define ulTaskNotifyValueClear(xTask, ulBitsToClear)                   \
  ulTaskGenericNotifyValueClear((xTask), (tskDEFAULT_INDEX_TO_NOTIFY), \
                                (ulBitsToClear))
#define ulTaskNotifyValueClearIndexed(xTask, uxIndexToClear, ulBitsToClear) \
  ulTaskGenericNotifyValueClear((xTask), (uxIndexToClear), (ulBitsToClear))
 
void vTaskSetTimeOutState(TimeOut_t *const pxTimeOut);
 
BaseType_t xTaskCheckForTimeOut(TimeOut_t *const pxTimeOut,
                                TickType_t *const pxTicksToWait);
 
BaseType_t xTaskCatchUpTicks(TickType_t xTicksToCatchUp);
 
void TaskResetState(void);

 
#if (configNUMBER_OF_CORES == 1)
#define taskYIELD_WITHIN_API() portYIELD_WITHIN_API()
#else  
#define taskYIELD_WITHIN_API() vTaskYieldWithinAPI()
#endif  
 
BaseType_t xTaskIncrementTick(void);
 
void vTaskPlaceOnEventList(List_t<TCB_t> *const pxEventList,
                           const TickType_t xTicksToWait);
void vTaskPlaceOnUnorderedEventList(List_t<TCB_t> *pxEventList,
                                    const TickType_t Value,
                                    const TickType_t xTicksToWait);
 
void vTaskPlaceOnEventListRestricted(List_t<TCB_t> *const pxEventList,
                                     TickType_t xTicksToWait,
                                     const BaseType_t xWaitIndefinitely);

BaseType_t xTaskRemoveFromEventList(const List_t<TCB_t> *const pxEventList);
void vTaskRemoveFromUnorderedEventList(Item_t<TCB_t> *pxEventListItem,
                                       const TickType_t Value);
 
portDONT_DISCARD void vTaskSwitchContext(void);

 
TickType_t uxTaskResetEventItemValue(void);
 
TaskHandle_t xTaskGetCurrentTaskHandle(void);
 
TaskHandle_t xTaskGetCurrentTaskHandleForCore(BaseType_t xCoreID);
 
void vTaskMissedYield(void);
 
BaseType_t xTaskGetSchedulerState(void);
 
BaseType_t xTaskPriorityInherit(TaskHandle_t const pMutexHolder);
 
BaseType_t xTaskPriorityDisinherit(TaskHandle_t const pMutexHolder);
 
void vTaskPriorityDisinheritAfterTimeout(
    TaskHandle_t const pMutexHolder, UBaseType_t uxHighestPriorityWaitingTask);

 
TaskHandle_t pvTaskIncrementMutexHeldCount(void);
 
void vTaskInternalSetTimeOutState(TimeOut_t *const pxTimeOut);
void vTaskEnterCritical(void);
void vTaskExitCritical(void);
