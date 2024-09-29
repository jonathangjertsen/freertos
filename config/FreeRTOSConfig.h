#pragma once

#undef NDEBUG
#include <assert.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
    extern "C" {
#endif

#define configMINIMAL_STACK_SIZE 1000
#define configMAX_PRIORITIES 32
#define configUSE_PREEMPTION 1
#define configUSE_IDLE_HOOK 1
#define configUSE_TICK_HOOK 1
#define configUSE_16_BIT_TICKS 0
#define configTICK_RATE_HZ 1000
#define configTOTAL_HEAP_SIZE 10000
#define configUSE_MUTEXES 1
#define configUSE_TIMERS 1
#define configTIMER_TASK_STACK_DEPTH 1000
#define configTIMER_QUEUE_LENGTH 10
#define configTIMER_TASK_PRIORITY 5
#define configUSE_COUNTING_SEMAPHORES 1
#define configRUN_TIME_COUNTER_TYPE uint64_t
#define configSUPPORT_STATIC_ALLOCATION 1
#define INCLUDE_TimerPendFunctionCall 1
#define configUSE_TASK_PREEMPTION_DISABLE 0
#define portCRITICAL_NESTING_IN_TCB 1
#define INCLUDE_TaskSuspend 1
#define INCLUDE_TaskPrioritySet 1
#define configUSE_MUTEXES 1
#define INCLUDE_TaskPriorityGet 1
#define configKERNEL_PROVIDED_STATIC_MEMORY 1
#define INCLUDE_TaskAbortDelay 1
#define configUSE_QUEUE_SETS 1
#define INCLUDE_TaskDelayUntil 1

#define INCLUDE_TaskDelete 1
#define INCLUDE_TaskGetSchedulerState 1

#define pvPortMalloc malloc
#define vPortFree free

#define configNUM_THREAD_LOCAL_STORAGE_POINTERS 0

#define FREERTOS_CONFIGURE_INTERRUPTS()

#ifdef __cplusplus
    }
#endif
