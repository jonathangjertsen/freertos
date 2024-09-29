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
#include <stddef.h>
#include <stdint.h>

#define TICK_TYPE_WIDTH_16_BITS 0
#define TICK_TYPE_WIDTH_32_BITS 1
#define TICK_TYPE_WIDTH_64_BITS 2

#include "FreeRTOSConfig.h"
#if !defined(configUSE_16_BIT_TICKS) && !defined(configTICK_TYPE_WIDTH_IN_BITS)
#error Missing definition:  One of configUSE_16_BIT_TICKS and configTICK_TYPE_WIDTH_IN_BITS must be defined in FreeRTOSConfig.h.  See the Configuration section of the FreeRTOS API documentation for details.
#endif
#if defined(configUSE_16_BIT_TICKS) && defined(configTICK_TYPE_WIDTH_IN_BITS)
#error Only one of configUSE_16_BIT_TICKS and configTICK_TYPE_WIDTH_IN_BITS must be defined in FreeRTOSConfig.h.  See the Configuration section of the FreeRTOS API documentation for details.
#endif

#ifndef configTICK_TYPE_WIDTH_IN_BITS
#if (configUSE_16_BIT_TICKS == 1)
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_16_BITS
#else
#define configTICK_TYPE_WIDTH_IN_BITS TICK_TYPE_WIDTH_32_BITS
#endif
#endif

#ifndef configENABLE_ACCESS_CONTROL_LIST
#define configENABLE_ACCESS_CONTROL_LIST 0
#endif

#ifndef configNUMBER_OF_CORES
#define configNUMBER_OF_CORES 1
#endif
#ifndef configUSE_MALLOC_FAILED_HOOK
#define configUSE_MALLOC_FAILED_HOOK 0
#endif

#include "portable.h"
#include "projdefs.h"

#ifndef configUSE_NEWLIB_REENTRANT
#define configUSE_NEWLIB_REENTRANT 0
#endif

#if (configUSE_NEWLIB_REENTRANT == 1)
#include "newlib-freertos.h"
#endif

#ifndef configUSE_PICOLIBC_TLS
#define configUSE_PICOLIBC_TLS 0
#endif
#if (configUSE_PICOLIBC_TLS == 1)
#include "picolibc-freertos.h"
#endif

#ifndef configUSE_C_RUNTIME_TLS_SUPPORT
#define configUSE_C_RUNTIME_TLS_SUPPORT 0
#endif
#if (configUSE_C_RUNTIME_TLS_SUPPORT == 1)
#ifndef configTLS_BLOCK_TYPE
#error Missing definition:  configTLS_BLOCK_TYPE must be defined in FreeRTOSConfig.h when configUSE_C_RUNTIME_TLS_SUPPORT is set to 1.
#endif
#ifndef configINIT_TLS_BLOCK
#error Missing definition:  configINIT_TLS_BLOCK must be defined in FreeRTOSConfig.h when configUSE_C_RUNTIME_TLS_SUPPORT is set to 1.
#endif
#ifndef configSET_TLS_BLOCK
#error Missing definition:  configSET_TLS_BLOCK must be defined in FreeRTOSConfig.h when configUSE_C_RUNTIME_TLS_SUPPORT is set to 1.
#endif
#ifndef configDEINIT_TLS_BLOCK
#error Missing definition:  configDEINIT_TLS_BLOCK must be defined in FreeRTOSConfig.h when configUSE_C_RUNTIME_TLS_SUPPORT is set to 1.
#endif
#endif

#ifndef configMINIMAL_STACK_SIZE
#error Missing definition:  configMINIMAL_STACK_SIZE must be defined in FreeRTOSConfig.h.  configMINIMAL_STACK_SIZE defines the size (in words) of the stack allocated to the idle task.  Refer to the demo project provided for your port for a suitable value.
#endif
#ifndef configMAX_PRIORITIES
#error Missing definition:  configMAX_PRIORITIES must be defined in FreeRTOSConfig.h.  See the Configuration section of the FreeRTOS API documentation for details.
#endif
#if configMAX_PRIORITIES < 1
#error configMAX_PRIORITIES must be defined to be greater than or equal to 1.
#endif
#ifndef configUSE_PREEMPTION
#error Missing definition:  configUSE_PREEMPTION must be defined in FreeRTOSConfig.h as either 1 or 0.  See the Configuration section of the FreeRTOS API documentation for details.
#endif
#ifndef configUSE_IDLE_HOOK
#error Missing definition:  configUSE_IDLE_HOOK must be defined in FreeRTOSConfig.h as either 1 or 0.  See the Configuration section of the FreeRTOS API documentation for details.
#endif
#if (configNUMBER_OF_CORES > 1)
#ifndef configUSE_PASSIVE_IDLE_HOOK
#error Missing definition:  configUSE_PASSIVE_IDLE_HOOK must be defined in FreeRTOSConfig.h as either 1 or 0.  See the Configuration section of the FreeRTOS API documentation for details.
#endif
#endif
#ifndef configUSE_TICK_HOOK
#error Missing definition:  configUSE_TICK_HOOK must be defined in FreeRTOSConfig.h as either 1 or 0.  See the Configuration section of the FreeRTOS API documentation for details.
#endif
#if ((configTICK_TYPE_WIDTH_IN_BITS != TICK_TYPE_WIDTH_16_BITS) && \
     (configTICK_TYPE_WIDTH_IN_BITS != TICK_TYPE_WIDTH_32_BITS) && \
     (configTICK_TYPE_WIDTH_IN_BITS != TICK_TYPE_WIDTH_64_BITS))
#error Macro configTICK_TYPE_WIDTH_IN_BITS is defined to incorrect value.  See the Configuration section of the FreeRTOS API documentation for details.
#endif
#ifndef configUSE_CO_ROUTINES
#define configUSE_CO_ROUTINES 0
#endif
#ifndef INCLUDE_TaskPrioritySet
#define INCLUDE_TaskPrioritySet 0
#endif
#ifndef INCLUDE_uTaskPriorityGet
#define INCLUDE_uTaskPriorityGet 0
#endif
#ifndef INCLUDE_TaskDelete
#define INCLUDE_TaskDelete 0
#endif
#ifndef INCLUDE_TaskSuspend
#define INCLUDE_TaskSuspend 0
#endif
#ifndef INCLUDE_TaskDelayUntil
#ifdef INCLUDE_TaskDelayUntil

#define INCLUDE_TaskDelayUntil INCLUDE_TaskDelayUntil
#endif
#endif
#ifndef INCLUDE_TaskDelayUntil
#define INCLUDE_TaskDelayUntil 0
#endif
#ifndef INCLUDE_TaskDelay
#define INCLUDE_TaskDelay 0
#endif
#ifndef INCLUDE_TaskGetIdleTaskHandle
#define INCLUDE_TaskGetIdleTaskHandle 0
#endif
#ifndef INCLUDE_TaskAbortDelay
#define INCLUDE_TaskAbortDelay 0
#endif
#ifndef INCLUDE_xQueueGetMutexHolder
#define INCLUDE_xQueueGetMutexHolder 0
#endif
#ifndef INCLUDE_xSemaphoreGetMutexHolder
#define INCLUDE_xSemaphoreGetMutexHolder INCLUDE_xQueueGetMutexHolder
#endif
#ifndef INCLUDE_TaskGetHandle
#define INCLUDE_TaskGetHandle 0
#endif
#ifndef INCLUDE_uTaskGetStackHighWaterMark
#define INCLUDE_uTaskGetStackHighWaterMark 0
#endif
#ifndef INCLUDE_uTaskGetStackHighWaterMark2
#define INCLUDE_uTaskGetStackHighWaterMark2 0
#endif
#ifndef INCLUDE_eTaskGetState
#define INCLUDE_eTaskGetState 0
#endif
#ifndef INCLUDE_TaskResumeFromISR
#define INCLUDE_TaskResumeFromISR 1
#endif
#ifndef INCLUDE_TimerPendFunctionCall
#define INCLUDE_TimerPendFunctionCall 0
#endif
#ifndef INCLUDE_TaskGetSchedulerState
#define INCLUDE_TaskGetSchedulerState 0
#endif
#ifndef INCLUDE_TaskGetCurrentTaskHandle
#define INCLUDE_TaskGetCurrentTaskHandle 1
#endif
#if configUSE_CO_ROUTINES != 0
#ifndef configMAX_CO_ROUTINE_PRIORITIES
#error configMAX_CO_ROUTINE_PRIORITIES must be greater than or equal to 1.
#endif
#endif
#ifndef configUSE_APPLICATION_TASK_TAG
#define configUSE_APPLICATION_TASK_TAG 0
#endif
#ifndef configNUM_THREAD_LOCAL_STORAGE_POINTERS
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0
#endif
#ifndef configUSE_RECURSIVE_MUTEXES
#define configUSE_RECURSIVE_MUTEXES 0
#endif
#ifndef configUSE_MUTEXES
#define configUSE_MUTEXES 0
#endif
#ifndef configUSE_TIMERS
#define configUSE_TIMERS 0
#endif
#ifndef configUSE_EVENT_GROUPS
#define configUSE_EVENT_GROUPS 1
#endif
#ifndef configUSE_STREAM_BUFFERS
#define configUSE_STREAM_BUFFERS 1
#endif
#ifndef configUSE_DAEMON_TASK_STARTUP_HOOK
#define configUSE_DAEMON_TASK_STARTUP_HOOK 0
#endif
#if (configUSE_DAEMON_TASK_STARTUP_HOOK != 0)
#if (configUSE_TIMERS == 0)
#error configUSE_DAEMON_TASK_STARTUP_HOOK is set, but the daemon task is not created because configUSE_TIMERS is 0.
#endif
#endif
#ifndef configUSE_COUNTING_SEMAPHORES
#define configUSE_COUNTING_SEMAPHORES 0
#endif
#ifndef configUSE_TASK_PREEMPTION_DISABLE
#define configUSE_TASK_PREEMPTION_DISABLE 0
#endif
#ifndef configUSE_ALTERNATIVE_API
#define configUSE_ALTERNATIVE_API 0
#endif
#ifndef portCRITICAL_NESTING_IN_TCB
#define portCRITICAL_NESTING_IN_TCB 0
#endif
#ifndef configMAX_TASK_NAME_LEN
#define configMAX_TASK_NAME_LEN 16
#endif
#ifndef configIDLE_SHOULD_YIELD
#define configIDLE_SHOULD_YIELD 1
#endif
#if configMAX_TASK_NAME_LEN < 1
#error configMAX_TASK_NAME_LEN must be set to a minimum of 1 in FreeRTOSConfig.h
#endif

#ifndef portMEMORY_BARRIER
#define portMEMORY_BARRIER()
#endif
#ifndef portSOFTWARE_BARRIER
#define portSOFTWARE_BARRIER()
#endif
#ifndef configRUN_MULTIPLE_PRIORITIES
#define configRUN_MULTIPLE_PRIORITIES 0
#endif
#ifndef portGET_CORE_ID
#if (configNUMBER_OF_CORES == 1)
#define portGET_CORE_ID() 0
#else
#error configNUMBER_OF_CORES is set to more than 1 then portGET_CORE_ID must also be defined.
#endif
#endif
#ifndef portYIELD_CORE
#if (configNUMBER_OF_CORES == 1)
#define portYIELD_CORE(x) portYIELD()
#else
#error configNUMBER_OF_CORES is set to more than 1 then portYIELD_CORE must also be defined.
#endif
#endif
#ifndef portSET_INTERRUPT_MASK
#if (configNUMBER_OF_CORES > 1)
#error portSET_INTERRUPT_MASK is required in SMP
#endif
#endif
#ifndef portCLEAR_INTERRUPT_MASK
#if (configNUMBER_OF_CORES > 1)
#error portCLEAR_INTERRUPT_MASK is required in SMP
#endif
#endif
#ifndef portRELEASE_TASK_LOCK
#define portRELEASE_TASK_LOCK()
#endif
#ifndef portGET_TASK_LOCK
#define portGET_TASK_LOCK()
#endif
#ifndef portRELEASE_ISR_LOCK
#define portRELEASE_ISR_LOCK()
#endif
#ifndef portGET_ISR_LOCK
#define portGET_ISR_LOCK()
#endif
#ifndef portENTER_CRITICAL_FROM_ISR

#endif
#ifndef portEXIT_CRITICAL_FROM_ISR

#endif
#ifndef configUSE_CORE_AFFINITY
#define configUSE_CORE_AFFINITY 0
#endif
#ifndef configUSE_PASSIVE_IDLE_HOOK
#define configUSE_PASSIVE_IDLE_HOOK 0
#endif

#if configUSE_TIMERS == 1
#ifndef portTIMER_CALLBACK_ATTRIBUTE
#define portTIMER_CALLBACK_ATTRIBUTE
#endif
#endif
#define portHAS_NESTED_INTERRUPTS 0
#define portSET_INTERRUPT_MASK_FROM_ISR() 0
#define portCLEAR_INTERRUPT_MASK_FROM_ISR(uxSavedStatusValue) \
  (void)(uxSavedStatusValue)
#ifndef portSETUP_TCB
#define portSETUP_TCB(pxTCB) (void)(pxTCB)
#endif
#ifndef portTASK_SWITCH_HOOK
#define portTASK_SWITCH_HOOK(pxTCB) (void)(pxTCB)
#endif
#ifndef configQUEUE_REGISTRY_SIZE
#define configQUEUE_REGISTRY_SIZE 0U
#endif
#if (configQUEUE_REGISTRY_SIZE < 1)
#define vQueueAddToRegistry(xQueue, pcName)
#define vQueueUnregisterQueue(xQueue)
#define pcQueueGetName(xQueue)
#endif
#ifndef configUSE_MINI_LIST_ITEM
#define configUSE_MINI_LIST_ITEM 1
#endif
#ifndef configCHECK_FOR_STACK_OVERFLOW
#define configCHECK_FOR_STACK_OVERFLOW 0
#endif
#ifndef configRECORD_STACK_HIGH_ADDRESS
#define configRECORD_STACK_HIGH_ADDRESS 0
#endif
#ifndef configINCLUDE_FREERTOS_TASK_C_ADDITIONS_H
#define configINCLUDE_FREERTOS_TASK_C_ADDITIONS_H 0
#endif
#ifndef configGENERATE_RUN_TIME_STATS
#define configGENERATE_RUN_TIME_STATS 0
#endif
#ifndef portCONFIGURE_TIMER_FOR_RUN_TIME_STATS
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()
#endif
#ifndef portPRIVILEGE_BIT
#define portPRIVILEGE_BIT ((UBaseType_t)0x00)
#endif
#ifndef portYIELD_WITHIN_API
#define portYIELD_WITHIN_API portYIELD
#endif
#ifndef portSUPPRESS_TICKS_AND_SLEEP
#define portSUPPRESS_TICKS_AND_SLEEP(xExpectedIdleTime)
#endif
#ifndef configEXPECTED_IDLE_TIME_BEFORE_SLEEP
#define configEXPECTED_IDLE_TIME_BEFORE_SLEEP 2
#endif
#ifndef configUSE_TICKLESS_IDLE
#define configUSE_TICKLESS_IDLE 0
#endif
#ifndef configPRE_SUPPRESS_TICKS_AND_SLEEP_PROCESSING
#define configPRE_SUPPRESS_TICKS_AND_SLEEP_PROCESSING(x)
#endif
#ifndef configPRE_SLEEP_PROCESSING
#define configPRE_SLEEP_PROCESSING(x)
#endif
#ifndef configPOST_SLEEP_PROCESSING
#define configPOST_SLEEP_PROCESSING(x)
#endif
#ifndef portTASK_USES_FLOATING_POINT
#define portTASK_USES_FLOATING_POINT()
#endif
#ifndef portALLOCATE_SECURE_CONTEXT
#define portALLOCATE_SECURE_CONTEXT(ulSecureStackSize)
#endif
#ifndef portDONT_DISCARD
#define portDONT_DISCARD
#endif
#ifndef configUSE_TIME_SLICING
#define configUSE_TIME_SLICING 1
#endif
#ifndef configINCLUDE_APPLICATION_DEFINED_S
#define configINCLUDE_APPLICATION_DEFINED_S 0
#endif
#ifndef configUSE_STATS_FORMATTING_FUNCTIONS
#define configUSE_STATS_FORMATTING_FUNCTIONS 0
#endif

#ifndef portASSERT_IF_INTERRUPT_PRIORITY_INVALID
#define portASSERT_IF_INTERRUPT_PRIORITY_INVALID()
#endif
#ifndef configUSE_TRACE_FACILITY
#define configUSE_TRACE_FACILITY 0
#endif
#ifndef mtCOVERAGE_TEST_MARKER
#define mtCOVERAGE_TEST_MARKER()
#endif
#ifndef mtCOVERAGE_TEST_DELAY
#define mtCOVERAGE_TEST_DELAY()
#endif
#ifndef portASSERT_IF_IN_ISR
#define portASSERT_IF_IN_ISR()
#endif
#ifndef configAPPLICATION_ALLOCATED_HEAP
#define configAPPLICATION_ALLOCATED_HEAP 0
#endif
#ifndef configENABLE_HEAP_PROTECTOR
#define configENABLE_HEAP_PROTECTOR 0
#endif
#ifndef configUSE_TASK_NOTIFICATIONS
#define configUSE_TASK_NOTIFICATIONS 1
#endif
#ifndef configTASK_NOTIFICATION_ARRAY_ENTRIES
#define configTASK_NOTIFICATION_ARRAY_ENTRIES 1
#endif
#ifndef configUSE_POSIX_ERRNO
#define configUSE_POSIX_ERRNO 0
#endif
#ifndef configUSE_SB_COMPLETED_CALLBACK

#define configUSE_SB_COMPLETED_CALLBACK 0
#endif
#ifndef configSUPPORT_DYNAMIC_ALLOCATION
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#endif

#ifndef configSTATS_BUFFER_MAX_LENGTH
#define configSTATS_BUFFER_MAX_LENGTH 0xFFFF
#endif

#ifndef configMESSAGE_BUFFER_LENGTH_TYPE

#define configMESSAGE_BUFFER_LENGTH_TYPE size_t
#endif

#ifndef configINITIAL_TICK_COUNT
#define configINITIAL_TICK_COUNT 0
#endif

#define portTICK_TYPE_ENTER_CRITICAL()
#define portTICK_TYPE_EXIT_CRITICAL()
#define portTICK_TYPE_SET_INTERRUPT_MASK_FROM_ISR() 0
#define portTICK_TYPE_CLEAR_INTERRUPT_MASK_FROM_ISR(x) (void)(x)

#ifndef configENABLE_BACKWARD_COMPATIBILITY
#define configENABLE_BACKWARD_COMPATIBILITY 1
#endif
#ifndef configPRINTF

#define configPRINTF(X)
#endif
#ifndef configMAX

#define configMAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef configMIN

#define configMIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#if configENABLE_BACKWARD_COMPATIBILITY == 1
#define eTaskStateGet eTaskGetState
#define portTickType TickType_t
#define TaskHandle TaskHandle_t
#define xQueueHandle QueueHandle_t
#define xSemaphoreHandle SemaphoreHandle_t
#define xQueueSetHandle QueueSetHandle_t
#define xQueueSetMemberHandle QueueSetMemberHandle_t
#define xTimeOutType TimeOut_t
#define xMemoryRegion MemoryRegion_t
#define TaskParameters TaskParameters_t
#define TaskStatusType TaskStatus_t
#define TimerHandle TimerHandle_t
#define xCoRoutineHandle CoRoutineHandle_t
#define pdTASK_HOOK_CODE TaskHookFunction_t
#define portTICK_RATE_MS portTICK_PERIOD_MS
#define pcTaskGetTaskName pcTaskGetName
#define pcTimerGetTimerName pcTimerGetName
#define pcQueueGetQueueName pcQueueGetName
#define TaskGetTaskInfo TaskGetInfo
#define TaskGetIdleRunTimeCounter ulTaskGetIdleRunTimeCounter

#define tmrTIMER_CALLBACK TimerCallbackFunction_t
#define pdTASK_CODE TaskFunction_t
#define xListItem ListItem_t
#define xList List_t
#endif

#ifndef configUSE_TASK_FPU_SUPPORT
#define configUSE_TASK_FPU_SUPPORT 1
#endif

#ifndef configENABLE_MPU
#define configENABLE_MPU 0
#endif

#ifndef configENABLE_FPU
#define configENABLE_FPU 1
#endif

#ifndef configENABLE_MVE
#define configENABLE_MVE 0
#endif

#ifndef configENABLE_TRUSTZONE
#define configENABLE_TRUSTZONE 1
#endif

#ifndef configRUN_FREERTOS_SECURE_ONLY
#define configRUN_FREERTOS_SECURE_ONLY 0
#endif
#ifndef configRUN_ADDITIONAL_TESTS
#define configRUN_ADDITIONAL_TESTS 0
#endif

#ifndef configCONTROL_INFINITE_LOOP
#define configCONTROL_INFINITE_LOOP()
#endif

#define tskSTATIC_AND_DYNAMIC_ALLOCATION_POSSIBLE \
  (((portUSING_MPU_WRAPPERS == 0) &&              \
    (configSUPPORT_DYNAMIC_ALLOCATION == 1) &&    \
    (configSUPPORT_STATIC_ALLOCATION == 1)) ||    \
   ((portUSING_MPU_WRAPPERS == 1) && (configSUPPORT_DYNAMIC_ALLOCATION == 1)))

struct xSTATIC_LIST_ITEM {
  TickType_t xDummy2;
  void* pvDummy3[4];
};
typedef struct xSTATIC_LIST_ITEM StaticListItem_t;
struct xSTATIC_MINI_LIST_ITEM {
  TickType_t xDummy2;
  void* pvDummy3[2];
};
typedef struct xSTATIC_MINI_LIST_ITEM StaticMiniListItem_t;

typedef struct xSTATIC_LIST {
  UBaseType_t uxDummy2;
  void* pvDummy3;
  StaticMiniListItem_t xDummy4;
} StaticList_t;

typedef struct xSTATIC_TCB {
  void* pxDummy1;
  StaticListItem_t xDummy3[2];
  UBaseType_t uxDummy5;
  void* pxDummy6;
  uint8_t ucDummy7[configMAX_TASK_NAME_LEN];
  UBaseType_t uxDummy9;
  UBaseType_t uxDummy12[2];
  uint32_t ulDummy18[configTASK_NOTIFICATION_ARRAY_ENTRIES];
  uint8_t ucDummy19[configTASK_NOTIFICATION_ARRAY_ENTRIES];
  uint8_t uxDummy20;
  uint8_t ucDummy21;
} StaticTask_t;

typedef struct xSTATIC_QUEUE {
  void* pvDummy1[3];
  union {
    void* pvDummy2;
    UBaseType_t uxDummy2;
  } u;
  StaticList_t xDummy3[2];
  UBaseType_t uxDummy4[3];
  uint8_t ucDummy5[2];
  uint8_t ucDummy6;
  void* pvDummy7;
} StaticQueue_t;
typedef StaticQueue_t StaticSemaphore_t;

typedef struct xSTATIC_EVENT_GROUP {
  TickType_t xDummy1;
  StaticList_t xDummy2;
  uint8_t ucDummy4;
} StaticEventGroup_t;

typedef struct xSTATIC_TIMER {
  void* pvDummy1;
  StaticListItem_t xDummy2;
  TickType_t xDummy3;
  void* pvDummy5;
  TaskFunction_t pvDummy6;
  uint8_t ucDummy8;
} StaticTimer_t;

typedef struct xSTATIC_STREAM_BUFFER {
  size_t uxDummy1[4];
  void* pvDummy2[3];
  uint8_t ucDummy3;
  UBaseType_t uxDummy6;
} StaticStreamBuffer_t;

typedef StaticStreamBuffer_t StaticMessageBuffer_t;
