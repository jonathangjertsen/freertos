/*
 * FreeRTOS Kernel <DEVELOPMENT BRANCH>
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */
/* Standard includes. */
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.hpp"
#include "timers.h"
#include "stack_macros.h"

#define taskYIELD_ANY_CORE_IF_USING_PREEMPTION( pxTCB ) \
    do {                                                        \
        if( CurrentTCB->Priority < ( pxTCB )->Priority )  \
        {                                                       \
            portYIELD_WITHIN_API();                             \
        }                                                       \
    } while( 0 )

/* Values that can be assigned to the ucNotifyState member of the TCB. */
#define taskNOT_WAITING_NOTIFICATION              ( ( uint8_t ) 0 ) /* Must be zero as it is the initialised value. */
#define taskWAITING_NOTIFICATION                  ( ( uint8_t ) 1 )
#define taskNOTIFICATION_RECEIVED                 ( ( uint8_t ) 2 )
/*
 * The value used to fill the stack of a task when the task is created.  This
 * is used purely for checking the high water mark for tasks.
 */
#define tskSTACK_FILL_BYTE                        ( 0xa5U )
/* Bits used to record how a task's stack and TCB were allocated. */
#define tskDYNAMICALLY_ALLOCATED_STACK_AND_TCB    ( ( uint8_t ) 0 )
#define tskSTATICALLY_ALLOCATED_STACK_ONLY        ( ( uint8_t ) 1 )
#define tskSTATICALLY_ALLOCATED_STACK_AND_TCB     ( ( uint8_t ) 2 )
/* If any of the following are set then task stacks are filled with a known
 * value so the high water mark can be determined.  If none of the following are
 * set then don't fill the stack so there is no unnecessary dependency on memset. */
#if ( ( configCHECK_FOR_STACK_OVERFLOW > 1 ) || ( configUSE_TRACE_FACILITY == 1 ) || ( INCLUDE_uxTaskGetStackHighWaterMark == 1 ) || ( INCLUDE_uxTaskGetStackHighWaterMark2 == 1 ) )
    #define tskSET_NEW_STACKS_TO_KNOWN_VALUE    1
#else
    #define tskSET_NEW_STACKS_TO_KNOWN_VALUE    0
#endif
/*
 * Some kernel aware debuggers require the data the debugger needs access to be
 * global, rather than file scope.
 */
#ifdef portREMOVE_STATIC_QUALIFIER
    #define static
#endif
/* The name allocated to the Idle task.  This can be overridden by defining
 * configIDLE_TASK_NAME in FreeRTOSConfig.h. */
#ifndef configIDLE_TASK_NAME
    #define configIDLE_TASK_NAME    "IDLE"
#endif
#if ( configUSE_PORT_OPTIMISED_TASK_SELECTION == 0 )
/* If configUSE_PORT_OPTIMISED_TASK_SELECTION is 0 then task selection is
 * performed in a generic way that is not optimised to any particular
 * microcontroller architecture. */
/* TopReadyPriority holds the priority of the highest priority ready
 * state task. */
    #define RECORD_READY_PRIORITY( Priority ) \
    do {                                            \
        if( ( Priority ) > TopReadyPriority )   \
        {                                           \
            TopReadyPriority = ( Priority );    \
        }                                           \
    } while( 0 ) /* RECORD_READY_PRIORITY */

    #define taskSELECT_HIGHEST_PRIORITY_TASK()                                           \
    do {                                                                                 \
        UBaseType_t uxTopPriority = TopReadyPriority;                                  \
                                                                                         \
        /* Find the highest priority queue that contains ready tasks. */                 \
        while( LIST_IS_EMPTY( &( ReadyTasksLists[ uxTopPriority ] ) ) != false ) \
        {                                                                                \
            configASSERT( uxTopPriority );                                               \
            --uxTopPriority;                                                             \
        }                                                                                \
                                                                                         \
        CurrentTCB = GET_OWNER_OF_NEXT_ENTRY(&( ReadyTasksLists[ uxTopPriority ] ) ); \
        TopReadyPriority = uxTopPriority;                                                   \
    } while( 0 ) /* taskSELECT_HIGHEST_PRIORITY_TASK */

/* Define away taskRESET_READY_PRIORITY() and portRESET_READY_PRIORITY() as
 * they are only required when a port optimised method of task selection is
 * being used. */
    #define taskRESET_READY_PRIORITY( Priority )
    #define portRESET_READY_PRIORITY( Priority, TopReadyPriority )
#else /* configUSE_PORT_OPTIMISED_TASK_SELECTION */
/* If configUSE_PORT_OPTIMISED_TASK_SELECTION is 1 then task selection is
 * performed in a way that is tailored to the particular microcontroller
 * architecture being used. */
/* A port optimised version is provided.  Call the port defined macros. */
    #define RECORD_READY_PRIORITY( Priority )    portRECORD_READY_PRIORITY( ( Priority ), TopReadyPriority )

/* A port optimised version is provided, call it only if the TCB being reset
 * is being referenced from a ready list.  If it is referenced from a delayed
 * or suspended list then it won't be in a ready list. */
    #define taskRESET_READY_PRIORITY( Priority )                                                     \
    do {                                                                                               \
        if(ReadyTasksLists[ ( Priority ) ].Length == ( UBaseType_t ) 0 ) \
        {                                                                                              \
            portRESET_READY_PRIORITY( ( Priority ), ( TopReadyPriority ) );                        \
        }                                                                                              \
    } while( 0 )
#endif /* configUSE_PORT_OPTIMISED_TASK_SELECTION */

/*
 * Place the task represented by pxTCB into the appropriate ready list for
 * the task.  It is inserted at the end of the list.
 */
#define AddTaskToReadyList( pxTCB )                                                                     \
    do {                                                                                                   \
        RECORD_READY_PRIORITY( ( pxTCB )->Priority );                                                \
        ReadyTasksLists[pxTCB->Priority].append(&(pxTCB)->xStateListItem); \
    } while( 0 )

/*
 * Several functions take a TaskHandle_t parameter that can optionally be NULL,
 * where NULL is used to indicate that the handle of the currently executing
 * task should be used in place of the parameter.  This macro simply checks to
 * see if the parameter is NULL and returns a pointer to the appropriate TCB.
 */
#define GetTCBFromHandle( pxHandle )    ( ( ( pxHandle ) == NULL ) ? CurrentTCB : ( pxHandle ) )
/* The item value of the event list item is normally used to hold the priority
 * of the task to which it belongs (coded to allow it to be held in reverse
 * priority order).  However, it is occasionally borrowed for other purposes.  It
 * is important its value is not updated due to a task priority change while it is
 * being used for another purpose.  The following bit definition is used to inform
 * the scheduler that the value should not be changed - in which case it is the
 * responsibility of whichever module is using the value to ensure it gets set back
 * to its original value when it is released. */
#if ( configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_16_BITS )
    #define taskEVENT_LIST_ITEM_VALUE_IN_USE    ( ( uint16_t ) 0x8000U )
#elif ( configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_32_BITS )
    #define taskEVENT_LIST_ITEM_VALUE_IN_USE    ( ( uint32_t ) 0x80000000U )
#elif ( configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_64_BITS )
    #define taskEVENT_LIST_ITEM_VALUE_IN_USE    ( ( uint64_t ) 0x8000000000000000U )
#endif
/* Indicates that the task is not actively running on any core. */
#define taskTASK_NOT_RUNNING           ( ( BaseType_t ) ( -1 ) )
/* Indicates that the task is actively running but scheduled to yield. */
#define taskTASK_SCHEDULED_TO_YIELD    ( ( BaseType_t ) ( -2 ) )
/* Returns true if the task is actively running and not scheduled to yield. */
#define taskTASK_IS_RUNNING( pxTCB )                          ( ( ( pxTCB ) == CurrentTCB ) ? ( true ) : ( false ) )
#define taskTASK_IS_RUNNING_OR_SCHEDULED_TO_YIELD( pxTCB )    ( ( ( pxTCB ) == CurrentTCB ) ? ( true ) : ( false ) )
/* Indicates that the task is an Idle task. */
#define taskATTRIBUTE_IS_IDLE    ( UBaseType_t ) ( 1U << 0U )
#define taskBITS_PER_BYTE    ( ( size_t ) 8 )

/*
 * Task control block.  A task control block (TCB) is allocated for each task,
 * and stores task state information, including a pointer to the task's context
 * (the task's run time environment, including register values)
 */
typedef struct TCB_t {
    volatile StackType_t * pxTopOfStack; /**< Points to the location of the last item placed on the tasks stack.  THIS MUST BE THE FIRST MEMBER OF THE TCB STRUCT. */
    Item_t<TCB_t> xStateListItem;                  /**< The list that the state list item of a task is reference from denotes the state of that task (Ready, Blocked, Suspended ). */
    Item_t<TCB_t> xEventListItem;                  /**< Used to reference a task from an event list. */
    UBaseType_t Priority;                     /**< The priority of the task.  0 is the lowest priority. */
    StackType_t * pxStack;                      /**< Points to the start of the stack. */
    char pcTaskName[ configMAX_TASK_NAME_LEN ]; /**< Descriptive name given to the task when created.  Facilitates debugging only. */
    UBaseType_t uxCriticalNesting; /**< Holds the critical section nesting depth for ports that do not maintain their own count in the port layer. */
    UBaseType_t uxBasePriority; /**< The priority last assigned to the task - used by the priority inheritance mechanism. */
    UBaseType_t uxMutexesHeld;
    volatile uint32_t ulNotifiedValue[ configTASK_NOTIFICATION_ARRAY_ENTRIES ];
    volatile uint8_t ucNotifyState[ configTASK_NOTIFICATION_ARRAY_ENTRIES ];
    uint8_t StaticallyAllocated; /**< Set to true if the task is a statically allocated to ensure no attempt is made to free the memory. */
    uint8_t ucDelayAborted;
} TCB_t;

TCB_t * volatile CurrentTCB = nullptr;

/* Lists for ready and blocked tasks. --------------------
 * DelayedTaskList1 and DelayedTaskList2 could be moved to function scope but
 * doing so breaks some kernel aware debuggers and debuggers that rely on removing
 * the static qualifier. */
static List_t<TCB_t> ReadyTasksLists[ configMAX_PRIORITIES ]; /**< Prioritised ready tasks. */
static List_t<TCB_t> DelayedTaskList1;                         /**< Delayed tasks. */
static List_t<TCB_t> DelayedTaskList2;                         /**< Delayed tasks (two lists are used - one for delays that have overflowed the current tick count. */
static List_t<TCB_t> * volatile DelayedTaskList;              /**< Points to the delayed task list currently being used. */
static List_t<TCB_t> * volatile OverflowDelayedTaskList;      /**< Points to the delayed task list currently being used to hold tasks that have overflowed the current tick count. */
static List_t<TCB_t> PendingReadyList;                         /**< Tasks that have been readied while the scheduler was suspended.  They will be moved to the ready list when the scheduler is resumed. */
static List_t<TCB_t> xTasksWaitingTermination; /**< Tasks that have been deleted - but their memory not yet freed. */
static volatile UBaseType_t uxDeletedTasksWaitingCleanUp = ( UBaseType_t ) 0U;
static List_t<TCB_t> SuspendedTaskList; /**< Tasks that are currently suspended. */
/* Other file private variables. --------------------------------*/
static volatile UBaseType_t CurrentNumberOfTasks = ( UBaseType_t ) 0U;
static volatile TickType_t TickCount = (TickType_t) configINITIAL_TICK_COUNT;
static volatile UBaseType_t TopReadyPriority = tskIDLE_PRIORITY;
static volatile BaseType_t SchedulerRunning = false;
static volatile TickType_t PendedTicks = (TickType_t) 0U;
static volatile BaseType_t YieldPendings[ configNUMBER_OF_CORES ] = { false };
static volatile BaseType_t NumOfOverflows = ( BaseType_t ) 0;
static UBaseType_t TaskNumber = ( UBaseType_t ) 0U;
static volatile TickType_t NextTaskUnblockTime = (TickType_t) 0U; /* Initialised to portMAX_DELAY before the scheduler starts. */
static TaskHandle_t IdleTaskHandles[ configNUMBER_OF_CORES ];       /**< Holds the handles of the idle tasks.  The idle tasks are created automatically when the scheduler is started. */
/* Improve support for OpenOCD. The kernel tracks Ready tasks via priority lists.
 * For tracking the state of remote threads, OpenOCD uses TopUsedPriority
 * to determine the number of priority lists to read back from the remote target. */
static const volatile UBaseType_t TopUsedPriority = configMAX_PRIORITIES - 1U;
/* Context switches are held pending while the scheduler is suspended.  Also,
 * interrupts must not manipulate the xStateListItem of a TCB, or any of the
 * lists the xStateListItem can be referenced from, if the scheduler is suspended.
 * If an interrupt needs to unblock a task while the scheduler is suspended then it
 * moves the task's event list item into the PendingReadyList, ready for the
 * kernel to move the task from the pending ready list into the real ready list
 * when the scheduler is unsuspended.  The pending ready list itself can only be
 * accessed from a critical section.
 *
 * Updates to SchedulerSuspended must be protected by both the task lock and the ISR lock
 * and must not be done from an ISR. Reads must be protected by either lock and may be done
 * from either an ISR or a task. */
 static volatile bool SchedulerSuspended = false;

static void ResetNextTaskUnblockTime();
static inline void taskSWITCH_DELAYED_LISTS() {                                            
    List_t<TCB_t> * pxTemp;                                                          
    configASSERT(DelayedTaskList->empty());               
    pxTemp = DelayedTaskList;                                               
    DelayedTaskList = OverflowDelayedTaskList;                            
    OverflowDelayedTaskList = pxTemp;                                       
    NumOfOverflows++;                 
    ResetNextTaskUnblockTime();                                            
}

static inline void taskSELECT_HIGHEST_PRIORITY_TASK() {
    UBaseType_t uxTopPriority;                                                           
    portGET_HIGHEST_PRIORITY( uxTopPriority, TopReadyPriority );                       
    configASSERT( ReadyTasksLists[ uxTopPriority ].Length > 0 ); 
    CurrentTCB = ReadyTasksLists[uxTopPriority].advance()->Owner;
}

/* File private functions. --------------------------------*/
/*
 * Creates the idle tasks during scheduler start.
 */
static BaseType_t CreateIdleTasks( void );
#if ( configNUMBER_OF_CORES > 1 )
/*
 * Checks to see if another task moved the current task out of the ready
 * list while it was waiting to enter a critical section and yields, if so.
 */
    static void CheckForRunStateChange( void );
#endif /* #if ( configNUMBER_OF_CORES > 1 ) */
#if ( configNUMBER_OF_CORES > 1 )
/*
 * Yields a core, or cores if multiple priorities are not allowed to run
 * simultaneously, to allow the task pxTCB to run.
 */
    static void YieldForTask( const TCB_t * pxTCB );
#endif /* #if ( configNUMBER_OF_CORES > 1 ) */
#if ( configNUMBER_OF_CORES > 1 )
/*
 * Selects the highest priority available task for the given core.
 */
    static void SelectHighestPriorityTask( BaseType_t xCoreID );
#endif /* #if ( configNUMBER_OF_CORES > 1 ) */
/**
 * Utility task that simply returns true if the task referenced by xTask is
 * currently in the Suspended state, or false if the task referenced by xTask
 * is in any other state.
 */
#if ( INCLUDE_vTaskSuspend == 1 )
    static BaseType_t TaskIsTaskSuspended( const TaskHandle_t xTask ) ;
#endif /* INCLUDE_vTaskSuspend */
/*
 * Utility to ready all the lists used by the scheduler.  This is called
 * automatically upon the creation of the first task.
 */
static void InitialiseTaskLists( void ) ;
/*
 * The idle task, which as all tasks is implemented as a never ending loop.
 * The idle task is automatically created and added to the ready lists upon
 * creation of the first user task.
 *
 * In the FreeRTOS SMP, configNUMBER_OF_CORES - 1 passive idle tasks are also
 * created to ensure that each core has an idle task to run when no other
 * task is available to run.
 *
 * The portTASK_FUNCTION_PROTO() macro is used to allow port/compiler specific
 * language extensions.  The equivalent prototype for these functions are:
 *
 * void IdleTask( void *Parameters );
 * void PassiveIdleTask( void *Parameters );
 *
 */
static portTASK_FUNCTION_PROTO( IdleTask, Parameters ) ;
#if ( configNUMBER_OF_CORES > 1 )
    static portTASK_FUNCTION_PROTO( PassiveIdleTask, Parameters ) ;
#endif
/*
 * Utility to free all memory allocated by the scheduler to hold a TCB,
 * including the stack pointed to by the TCB.
 *
 * This does not free memory allocated by the task itself (i.e. memory
 * allocated by calls to pvPortMalloc from within the tasks application code).
 */
#if ( INCLUDE_vTaskDelete == 1 )
    static void DeleteTCB( TCB_t * pxTCB ) ;
#endif
/*
 * Used only by the idle task.  This checks to see if anything has been placed
 * in the list of tasks waiting to be deleted.  If so the task is cleaned up
 * and its TCB deleted.
 */
static void CheckTasksWaitingTermination( void ) ;
/*
 * The currently executing task is entering the Blocked state.  Add the task to
 * either the current or the overflow delayed task list.
 */
static void AddCurrentTaskToDelayedList( TickType_t TicksToWait,
                                            const BaseType_t CanBlockIndefinitely ) ;
/*
 * Searches pxList for a task with name NameToQuery - returning a handle to
 * the task if it is found, or NULL if the task is not found.
 */
#if ( INCLUDE_xTaskGetHandle == 1 )
    static TCB_t * SearchForNameWithinSingleList( List_t * pxList,
                                                     const char NameToQuery[] ) ;
#endif
/*
 * When a task is created, the stack of the task is filled with a known value.
 * This function determines the 'high water mark' of the task stack by
 * determining how much of the stack remains at the original preset value.
 */
#if ( ( configUSE_TRACE_FACILITY == 1 ) || ( INCLUDE_uxTaskGetStackHighWaterMark == 1 ) || ( INCLUDE_uxTaskGetStackHighWaterMark2 == 1 ) )
    static configSTACK_DEPTH_TYPE TaskCheckFreeStackSpace( const uint8_t * pucStackByte ) ;
#endif
/*
 * Return the amount of time, in ticks, that will pass before the kernel will
 * next move a task from the Blocked state to the Running state or before the
 * tick count overflows (whichever is earlier).
 *
 * This conditional compilation should use inequality to 0, not equality to 1.
 * This is to ensure portSUPPRESS_TICKS_AND_SLEEP() can be called when user
 * defined low power mode implementations require configUSE_TICKLESS_IDLE to be
 * set to a value other than 1.
 */
#if ( configUSE_TICKLESS_IDLE != 0 )
    static TickType_t GetExpectedIdleTime( void ) ;
#endif
/*
 * Set NextTaskUnblockTime to the time at which the next Blocked state task
 * will exit the Blocked state.
 */
static void ResetNextTaskUnblockTime( void ) ;
#if ( configUSE_STATS_FORMATTING_FUNCTIONS > 0 )
/*
 * Helper function used to pad task names with spaces when printing out
 * human readable tables of task information.
 */
    static char * WriteNameToBuffer( char * pcBuffer,
                                        const char * pcTaskName ) ;
#endif
/*
 * Called after a Task_t structure has been allocated either statically or
 * dynamically to fill in the structure's members.
 */
static void InitialiseNewTask( TaskFunction_t TaskCode,
                                  const char * const Name,
                                  const configSTACK_DEPTH_TYPE StackDepth,
                                  void * const Parameters,
                                  UBaseType_t Priority,
                                  TaskHandle_t * const CreatedTask,
                                  TCB_t * pxNewTCB,
                                  const MemoryRegion_t * const xRegions ) ;
/*
 * Called after a new task has been created and initialised to place the task
 * under the control of the scheduler.
 */
static void AddNewTaskToReadyList( TCB_t * pxNewTCB ) ;
/*
 * Create a task with static buffer for both TCB and stack. Returns a handle to
 * the task if it is created successfully. Otherwise, returns NULL.
 */
#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
    static TCB_t * CreateStaticTask( TaskFunction_t TaskCode,
                                        const char * const Name,
                                        const configSTACK_DEPTH_TYPE StackDepth,
                                        void * const Parameters,
                                        UBaseType_t Priority,
                                        StackType_t * const StackBuffer,
                                        StaticTask_t * const TaskBuffer,
                                        TaskHandle_t * const CreatedTask ) ;
#endif /* #if ( configSUPPORT_STATIC_ALLOCATION == 1 ) */

/*
 * Create a task with allocated buffer for both TCB and stack. Returns a handle to
 * the task if it is created successfully. Otherwise, returns NULL.
 */
#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
    static TCB_t * CreateTask( TaskFunction_t TaskCode,
                                  const char * const Name,
                                  const configSTACK_DEPTH_TYPE StackDepth,
                                  void * const Parameters,
                                  UBaseType_t Priority,
                                  TaskHandle_t * const CreatedTask ) ;
#endif /* #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) */
/*
 * freertos_tasks_c_additions_init() should only be called if the user definable
 * macro FREERTOS_TASKS_C_ADDITIONS_INIT() is defined, as that is the only macro
 * called by the function.
 */
#ifdef FREERTOS_TASKS_C_ADDITIONS_INIT
    static void freertos_tasks_c_additions_init( void ) ;
#endif
#if ( configUSE_PASSIVE_IDLE_HOOK == 1 )
    extern void vApplicationPassiveIdleHook( void );
#endif /* #if ( configUSE_PASSIVE_IDLE_HOOK == 1 ) */

static TCB_t * CreateStaticTask( TaskFunction_t TaskCode,
                                    const char * const Name,
                                    const configSTACK_DEPTH_TYPE StackDepth,
                                    void * const Parameters,
                                    UBaseType_t Priority,
                                    StackType_t * const StackBuffer,
                                    StaticTask_t * const TaskBuffer,
                                    TaskHandle_t * const CreatedTask )
{
    TCB_t * pxNewTCB;
    configASSERT( StackBuffer != NULL );
    configASSERT( TaskBuffer != NULL );
    #if ( configASSERT_DEFINED == 1 )
    {
        /* Sanity check that the size of the structure used to declare a
            * variable of type StaticTask_t equals the size of the real task
            * structure. */
        volatile size_t xSize = sizeof( StaticTask_t );
        configASSERT( xSize == sizeof( TCB_t ) );
        ( void ) xSize; /* Prevent unused variable warning when configASSERT() is not used. */
    }
    #endif /* configASSERT_DEFINED */
    if( ( TaskBuffer != NULL ) && ( StackBuffer != NULL ) )
    {
        /* The memory used for the task's TCB and stack are passed into this
            * function - use them. */

        pxNewTCB = ( TCB_t * ) TaskBuffer;
        ( void ) memset( ( void * ) pxNewTCB, 0x00, sizeof( TCB_t ) );
        pxNewTCB->pxStack = ( StackType_t * ) StackBuffer;
        #if ( tskSTATIC_AND_DYNAMIC_ALLOCATION_POSSIBLE != 0 )
        {
            /* Tasks can be created statically or dynamically, so note this
                * task was created statically in case the task is later deleted. */
            pxNewTCB->StaticallyAllocated = tskSTATICALLY_ALLOCATED_STACK_AND_TCB;
        }
        #endif /* tskSTATIC_AND_DYNAMIC_ALLOCATION_POSSIBLE */
        InitialiseNewTask( TaskCode, Name, StackDepth, Parameters, Priority, CreatedTask, pxNewTCB, NULL );
    }
    else
    {
        pxNewTCB = NULL;
    }
    return pxNewTCB;
}

TaskHandle_t xTaskCreateStatic( TaskFunction_t TaskCode,
                                const char * const Name,
                                const configSTACK_DEPTH_TYPE StackDepth,
                                void * const Parameters,
                                UBaseType_t Priority,
                                StackType_t * const StackBuffer,
                                StaticTask_t * const TaskBuffer )
{
    TaskHandle_t xReturn = NULL;
    TCB_t * pxNewTCB;
    pxNewTCB = CreateStaticTask( TaskCode, Name, StackDepth, Parameters, Priority, StackBuffer, TaskBuffer, &xReturn );
    if( pxNewTCB != NULL )
    {
        AddNewTaskToReadyList( pxNewTCB );
    }
    return xReturn;
}

static TCB_t * CreateTask( TaskFunction_t TaskCode,
                                const char * const Name,
                                const configSTACK_DEPTH_TYPE StackDepth,
                                void * const Parameters,
                                UBaseType_t Priority,
                                TaskHandle_t * const CreatedTask )
{
    TCB_t * pxNewTCB;
    /* If the stack grows down then allocate the stack then the TCB so the stack
        * does not grow into the TCB.  Likewise if the stack grows up then allocate
        * the TCB then the stack. */
    #if ( portSTACK_GROWTH > 0 )
    {
        /* Allocate space for the TCB.  Where the memory comes from depends on
            * the implementation of the port malloc function and whether or not static
            * allocation is being used. */

        pxNewTCB = ( TCB_t * ) pvPortMalloc( sizeof( TCB_t ) );
        if( pxNewTCB != NULL )
        {
            ( void ) memset( ( void * ) pxNewTCB, 0x00, sizeof( TCB_t ) );
            /* Allocate space for the stack used by the task being created.
                * The base of the stack memory stored in the TCB so the task can
                * be deleted later if required. */

            pxNewTCB->pxStack = ( StackType_t * ) pvPortMallocStack( ( ( ( size_t ) StackDepth ) * sizeof( StackType_t ) ) );
            if( pxNewTCB->pxStack == NULL )
            {
                /* Could not allocate the stack.  Delete the allocated TCB. */
                vPortFree( pxNewTCB );
                pxNewTCB = NULL;
            }
        }
    }
    #else /* portSTACK_GROWTH */
    {
        StackType_t *pxStack = (StackType_t *)pvPortMallocStack( ( ( ( size_t ) StackDepth ) * sizeof( StackType_t ) ) );
        if( pxStack != NULL )
        {
            pxNewTCB = ( TCB_t * ) pvPortMalloc( sizeof( TCB_t ) );
            if( pxNewTCB != NULL )
            {
                ( void ) memset( ( void * ) pxNewTCB, 0x00, sizeof( TCB_t ) );
                pxNewTCB->pxStack = pxStack;
            }
            else
            {
                vPortFreeStack( pxStack );
            }
        }
        else
        {
            pxNewTCB = NULL;
        }
    }
    #endif /* portSTACK_GROWTH */
    if( pxNewTCB != NULL )
    {
        #if ( tskSTATIC_AND_DYNAMIC_ALLOCATION_POSSIBLE != 0 )
        {
            /* Tasks can be created statically or dynamically, so note this
                * task was created dynamically in case it is later deleted. */
            pxNewTCB->StaticallyAllocated = tskDYNAMICALLY_ALLOCATED_STACK_AND_TCB;
        }
        #endif /* tskSTATIC_AND_DYNAMIC_ALLOCATION_POSSIBLE */
        InitialiseNewTask( TaskCode, Name, StackDepth, Parameters, Priority, CreatedTask, pxNewTCB, NULL );
    }
    return pxNewTCB;
}

BaseType_t xTaskCreate( TaskFunction_t TaskCode,
                        const char * const Name,
                        const configSTACK_DEPTH_TYPE StackDepth,
                        void * const Parameters,
                        UBaseType_t Priority,
                        TaskHandle_t * const CreatedTask )
{
    TCB_t * pxNewTCB;
    BaseType_t xReturn;
    pxNewTCB = CreateTask( TaskCode, Name, StackDepth, Parameters, Priority, CreatedTask );
    if( pxNewTCB != NULL )
    {
        #if ( ( configNUMBER_OF_CORES > 1 ) && ( configUSE_CORE_AFFINITY == 1 ) )
        {
            /* Set the task's affinity before scheduling it. */
            pxNewTCB->uxCoreAffinityMask = configTASK_DEFAULT_CORE_AFFINITY;
        }
        #endif
        AddNewTaskToReadyList( pxNewTCB );
        xReturn = true;
    }
    else
    {
        xReturn = errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
    }
    return xReturn;
}

static void InitialiseNewTask( TaskFunction_t TaskCode,
                                  const char * const Name,
                                  const configSTACK_DEPTH_TYPE StackDepth,
                                  void * const Parameters,
                                  UBaseType_t Priority,
                                  TaskHandle_t * const CreatedTask,
                                  TCB_t * pxNewTCB,
                                  const MemoryRegion_t * const xRegions )
{
    StackType_t * pxTopOfStack;
    UBaseType_t x;
    
    /* Avoid dependency on memset() if it is not required. */
    #if ( tskSET_NEW_STACKS_TO_KNOWN_VALUE == 1 )
    {
        /* Fill the stack with a known value to assist debugging. */
        ( void ) memset( pxNewTCB->pxStack, ( int ) tskSTACK_FILL_BYTE, ( size_t ) StackDepth * sizeof( StackType_t ) );
    }
    #endif /* tskSET_NEW_STACKS_TO_KNOWN_VALUE */
    /* Calculate the top of stack address.  This depends on whether the stack
     * grows from high memory to low (as per the 80x86) or vice versa.
     * portSTACK_GROWTH is used to make the result positive or negative as required
     * by the port. */
    #if ( portSTACK_GROWTH < 0 )
    {
        pxTopOfStack = &( pxNewTCB->pxStack[ StackDepth - ( configSTACK_DEPTH_TYPE ) 1 ] );
        pxTopOfStack = ( StackType_t * ) ( ( ( portPOINTER_SIZE_TYPE ) pxTopOfStack ) & ( ~( ( portPOINTER_SIZE_TYPE ) portBYTE_ALIGNMENT_MASK ) ) );
        /* Check the alignment of the calculated top of stack is correct. */
        configASSERT( ( ( ( portPOINTER_SIZE_TYPE ) pxTopOfStack & ( portPOINTER_SIZE_TYPE ) portBYTE_ALIGNMENT_MASK ) == 0U ) );
        #if ( configRECORD_STACK_HIGH_ADDRESS == 1 )
        {
            /* Also record the stack's high address, which may assist
             * debugging. */
            pxNewTCB->pxEndOfStack = pxTopOfStack;
        }
        #endif /* configRECORD_STACK_HIGH_ADDRESS */
    }
    #else /* portSTACK_GROWTH */
    {
        pxTopOfStack = pxNewTCB->pxStack;
        pxTopOfStack = ( StackType_t * ) ( ( ( ( portPOINTER_SIZE_TYPE ) pxTopOfStack ) + portBYTE_ALIGNMENT_MASK ) & ( ~( ( portPOINTER_SIZE_TYPE ) portBYTE_ALIGNMENT_MASK ) ) );
        /* Check the alignment of the calculated top of stack is correct. */
        configASSERT( ( ( ( portPOINTER_SIZE_TYPE ) pxTopOfStack & ( portPOINTER_SIZE_TYPE ) portBYTE_ALIGNMENT_MASK ) == 0U ) );
        /* The other extreme of the stack space is required if stack checking is
         * performed. */
        pxNewTCB->pxEndOfStack = pxNewTCB->pxStack + ( StackDepth - ( configSTACK_DEPTH_TYPE ) 1 );
    }
    #endif /* portSTACK_GROWTH */
    /* Store the task name in the TCB. */
    if( Name != NULL )
    {
        for( x = ( UBaseType_t ) 0; x < ( UBaseType_t ) configMAX_TASK_NAME_LEN; x++ )
        {
            pxNewTCB->pcTaskName[ x ] = Name[ x ];
            /* Don't copy all configMAX_TASK_NAME_LEN if the string is shorter than
             * configMAX_TASK_NAME_LEN characters just in case the memory after the
             * string is not accessible (extremely unlikely). */
            if( Name[ x ] == ( char ) 0x00 )
            {
                break;
            }
            
        }
        /* Ensure the name string is terminated in the case that the string length
         * was greater or equal to configMAX_TASK_NAME_LEN. */
        pxNewTCB->pcTaskName[ configMAX_TASK_NAME_LEN - 1U ] = '\0';
    }
    /* This is used as an array index so must ensure it's not too large. */
    configASSERT( Priority < configMAX_PRIORITIES );
    if( Priority >= ( UBaseType_t ) configMAX_PRIORITIES )
    {
        Priority = ( UBaseType_t ) configMAX_PRIORITIES - ( UBaseType_t ) 1U;
    }
    pxNewTCB->Priority = Priority;
    #if ( configUSE_MUTEXES == 1 )
    {
        pxNewTCB->uxBasePriority = Priority;
    }
    #endif /* configUSE_MUTEXES */
    pxNewTCB->xStateListItem.init();
    pxNewTCB->xEventListItem.init();
    pxNewTCB->xStateListItem.Owner = pxNewTCB;
    pxNewTCB->xEventListItem.Value = (TickType_t) configMAX_PRIORITIES - (TickType_t) Priority;
    pxNewTCB->xEventListItem.Owner = pxNewTCB;
    ( void ) xRegions;
    pxNewTCB->pxTopOfStack = pxPortInitialiseStack( pxTopOfStack, TaskCode, Parameters );
    if( CreatedTask != NULL )
    {
        /* Pass the handle out in an anonymous way.  The handle can be used to
         * change the created task's priority, delete the created task, etc.*/
        *CreatedTask = ( TaskHandle_t ) pxNewTCB;
    }
}

static void AddNewTaskToReadyList( TCB_t * pxNewTCB )
{
    /* Ensure interrupts don't access the task lists while the lists are being
        * updated. */
    ENTER_CRITICAL();
    {
        CurrentNumberOfTasks = ( UBaseType_t ) ( CurrentNumberOfTasks + 1U );
        if( CurrentTCB == NULL )
        {
            /* There are no other tasks, or all the other tasks are in
                * the suspended state - make this the current task. */
            CurrentTCB = pxNewTCB;
            if( CurrentNumberOfTasks == ( UBaseType_t ) 1 )
            {
                /* This is the first task to be created so do the preliminary
                    * initialisation required.  We will not recover if this call
                    * fails, but we will report the failure. */
                InitialiseTaskLists();
            }
        }
        else
        {
            /* If the scheduler is not already running, make this task the
                * current task if it is the highest priority task to be created
                * so far. */
            if( SchedulerRunning == false )
            {
                if( CurrentTCB->Priority <= pxNewTCB->Priority )
                {
                    CurrentTCB = pxNewTCB;
                }
            }
        }
        TaskNumber++;
        AddTaskToReadyList( pxNewTCB );
        portSETUP_TCB( pxNewTCB );
    }
    EXIT_CRITICAL();
    if( SchedulerRunning != false )
    {
        /* If the created task is of a higher priority than the current task
            * then it should run now. */
        taskYIELD_ANY_CORE_IF_USING_PREEMPTION( pxNewTCB );
    }
}

void vTaskDelete( TaskHandle_t xTaskToDelete )
{
    TCB_t * pxTCB;
    BaseType_t xDeleteTCBInIdleTask = false;
    BaseType_t xTaskIsRunningOrYielding;
    ENTER_CRITICAL();
    {
        /* If null is passed in here then it is the calling task that is
            * being deleted. */
        pxTCB = GetTCBFromHandle( xTaskToDelete );
        if(pxTCB->xStateListItem.remove() == 0)
        {
            taskRESET_READY_PRIORITY( pxTCB->Priority );
        }
        pxTCB->xEventListItem.ensureRemoved();
        TaskNumber++;
        xTaskIsRunningOrYielding = taskTASK_IS_RUNNING_OR_SCHEDULED_TO_YIELD( pxTCB );
        if( ( SchedulerRunning != false ) && ( xTaskIsRunningOrYielding != false ) )
        {
            xTasksWaitingTermination.append(&pxTCB->xStateListItem);
            ++uxDeletedTasksWaitingCleanUp;
            xDeleteTCBInIdleTask = true;
            portPRE_TASK_DELETE_HOOK( pxTCB, &( YieldPendings[ 0 ] ) );
        }
        else
        {
            --CurrentNumberOfTasks;
            ResetNextTaskUnblockTime();
        }
    }
    EXIT_CRITICAL();
    if( xDeleteTCBInIdleTask != true )
    {
        DeleteTCB( pxTCB );
    }
    if( SchedulerRunning)
    {
        if( pxTCB == CurrentTCB )
        {
            configASSERT( SchedulerSuspended == 0 );
            taskYIELD_WITHIN_API();
        }
    }
}

BaseType_t xTaskDelayUntil( TickType_t * const pxPreviousWakeTime,
                            const TickType_t xTimeIncrement )
{
    TickType_t TimeToWake;
    BaseType_t xAlreadyYielded, xShouldDelay = false;
    configASSERT( pxPreviousWakeTime );
    configASSERT( ( xTimeIncrement > 0U ) );
    vTaskSuspendAll();
    {
        /* Minor optimisation.  The tick count cannot change in this
            * block. */
        const TickType_t ConstTickCount = TickCount;
        configASSERT( SchedulerSuspended == 1U );
        /* Generate the tick time at which the task wants to wake. */
        TimeToWake = *pxPreviousWakeTime + xTimeIncrement;
        if( ConstTickCount < *pxPreviousWakeTime )
        {
            /* The tick count has overflowed since this function was
                * lasted called.  In this case the only time we should ever
                * actually delay is if the wake time has also  overflowed,
                * and the wake time is greater than the tick time.  When this
                * is the case it is as if neither time had overflowed. */
            if( ( TimeToWake < *pxPreviousWakeTime ) && ( TimeToWake > ConstTickCount ) )
            {
                xShouldDelay = true;
            }
        }
        else
        {
            /* The tick time has not overflowed.  In this case we will
                * delay if either the wake time has overflowed, and/or the
                * tick time is less than the wake time. */
            if( ( TimeToWake < *pxPreviousWakeTime ) || ( TimeToWake > ConstTickCount ) )
            {
                xShouldDelay = true;
            }
        }
        /* Update the wake time ready for the next call. */
        *pxPreviousWakeTime = TimeToWake;
        if( xShouldDelay != false )
        {
            /* AddCurrentTaskToDelayedList() needs the block time, not
                * the time to wake, so subtract the current tick count. */
            AddCurrentTaskToDelayedList( TimeToWake - ConstTickCount, false );
        }
        
    }
    if(!TaskResumeAll()) {
        taskYIELD_WITHIN_API();
    }
    return xShouldDelay;
}

void vTaskDelay( const TickType_t xTicksToDelay ) {
    if( xTicksToDelay > (TickType_t) 0U ) {
        vTaskSuspendAll();
        configASSERT(SchedulerSuspended);
        AddCurrentTaskToDelayedList( xTicksToDelay, false );
        if (!TaskResumeAll()) {
            taskYIELD_WITHIN_API();
        }
    }
}

#if ( ( INCLUDE_eTaskGetState == 1 ) || ( configUSE_TRACE_FACILITY == 1 ) || ( INCLUDE_xTaskAbortDelay == 1 ) )
    eTaskState eTaskGetState( TaskHandle_t xTask )
    {
        eTaskState eReturn;
        List_t<TCB_t> * pxStateList;
        List_t<TCB_t> * pxEventList;
        List_t<TCB_t> * pxDelayedList;
        List_t<TCB_t> * pxOverflowedDelayedList;
        TCB_t * pxTCB = xTask;
        configASSERT( pxTCB );
        if( pxTCB == CurrentTCB )
        {
            /* The task calling this function is querying its own state. */
            eReturn = eRunning;
        }
        else
        {
            ENTER_CRITICAL();
            {
                pxStateList = pxTCB->xStateListItem.Container;
                pxEventList = pxTCB->xEventListItem.Container;
                pxDelayedList = DelayedTaskList;
                pxOverflowedDelayedList = OverflowDelayedTaskList;
            }
            EXIT_CRITICAL();
            if( pxEventList == &PendingReadyList )
            {
                /* The task has been placed on the pending ready list, so its
                 * state is eReady regardless of what list the task's state list
                 * item is currently placed on. */
                eReturn = eReady;
            }
            else if( ( pxStateList == pxDelayedList ) || ( pxStateList == pxOverflowedDelayedList ) )
            {
                /* The task being queried is referenced from one of the Blocked
                 * lists. */
                eReturn = eBlocked;
            }
            else if( pxStateList == &SuspendedTaskList )
            {
                if(pxTCB->xEventListItem.Container == NULL)
                {
                    BaseType_t x;
                    eReturn = eSuspended;
                    for( x = ( BaseType_t ) 0; x < ( BaseType_t ) configTASK_NOTIFICATION_ARRAY_ENTRIES; x++ )
                    {
                        if( pxTCB->ucNotifyState[ x ] == taskWAITING_NOTIFICATION )
                        {
                            eReturn = eBlocked;
                            break;
                        }
                    }
                }
                else
                {
                    eReturn = eBlocked;
                }
            }
            else if( ( pxStateList == &xTasksWaitingTermination ) || ( pxStateList == NULL ) )
            {
                /* The task being queried is referenced from the deleted
                    * tasks list, or it is not referenced from any lists at
                    * all. */
                eReturn = eDeleted;
            }
            else
            {
                #if ( configNUMBER_OF_CORES == 1 )
                {
                    /* If the task is not in any other state, it must be in the
                     * Ready (including pending ready) state. */
                    eReturn = eReady;
                }
                #else /* #if ( configNUMBER_OF_CORES == 1 ) */
                {
                    if( taskTASK_IS_RUNNING( pxTCB )  )
                    {
                        /* Is it actively running on a core? */
                        eReturn = eRunning;
                    }
                    else
                    {
                        /* If the task is not in any other state, it must be in the
                         * Ready (including pending ready) state. */
                        eReturn = eReady;
                    }
                }
                #endif /* #if ( configNUMBER_OF_CORES == 1 ) */
            }
        }
        return eReturn;
    }
#endif /* INCLUDE_eTaskGetState */

UBaseType_t uxTaskPriorityGet( const TaskHandle_t xTask )
{
    TCB_t const * pxTCB;
    UBaseType_t uxReturn;
    ENTER_CRITICAL();
    {
        /* If null is passed in here then it is the priority of the task
            * that called uxTaskPriorityGet() that is being queried. */
        pxTCB = GetTCBFromHandle( xTask );
        uxReturn = pxTCB->Priority;
    }
    EXIT_CRITICAL();
    return uxReturn;
}

UBaseType_t uxTaskPriorityGetFromISR( const TaskHandle_t xTask )
{
    TCB_t const * pxTCB;
    UBaseType_t uxReturn;
    UBaseType_t uxSavedInterruptStatus;
    portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
    uxSavedInterruptStatus = ( UBaseType_t ) ENTER_CRITICAL_FROM_ISR();
    {
        /* If null is passed in here then it is the priority of the calling
            * task that is being queried. */
        pxTCB = GetTCBFromHandle( xTask );
        uxReturn = pxTCB->Priority;
    }
    EXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );
    return uxReturn;
}

UBaseType_t uxTaskBasePriorityGet( const TaskHandle_t xTask )
{
    TCB_t const * pxTCB;
    UBaseType_t uxReturn;
    ENTER_CRITICAL();
    {
        /* If null is passed in here then it is the base priority of the task
            * that called uxTaskBasePriorityGet() that is being queried. */
        pxTCB = GetTCBFromHandle( xTask );
        uxReturn = pxTCB->uxBasePriority;
    }
    EXIT_CRITICAL();
    return uxReturn;
}

UBaseType_t uxTaskBasePriorityGetFromISR( const TaskHandle_t xTask )
{
    TCB_t const * pxTCB;
    UBaseType_t uxReturn;
    UBaseType_t uxSavedInterruptStatus;
    portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
    uxSavedInterruptStatus = ( UBaseType_t ) ENTER_CRITICAL_FROM_ISR();
    {
        /* If null is passed in here then it is the base priority of the calling
            * task that is being queried. */
        pxTCB = GetTCBFromHandle( xTask );
        uxReturn = pxTCB->uxBasePriority;
    }
    EXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );
    return uxReturn;
}

void vTaskPrioritySet( TaskHandle_t xTask,
                        UBaseType_t uxNewPriority )
{
    TCB_t * pxTCB;
    UBaseType_t uxCurrentBasePriority, PriorityUsedOnEntry;
    BaseType_t xYieldRequired = false;
    configASSERT( uxNewPriority < configMAX_PRIORITIES );
    /* Ensure the new priority is valid. */
    if( uxNewPriority >= ( UBaseType_t ) configMAX_PRIORITIES )
    {
        uxNewPriority = ( UBaseType_t ) configMAX_PRIORITIES - ( UBaseType_t ) 1U;
    }

    ENTER_CRITICAL();
    {
        /* If null is passed in here then it is the priority of the calling
            * task that is being changed. */
        pxTCB = GetTCBFromHandle( xTask );
        uxCurrentBasePriority = pxTCB->uxBasePriority;
        if( uxCurrentBasePriority != uxNewPriority )
        {
            /* The priority change may have readied a task of higher
                * priority than a running task. */
            if( uxNewPriority > uxCurrentBasePriority )
            {
                if( pxTCB != CurrentTCB )
                {
                    /* The priority of a task other than the currently
                        * running task is being raised.  Is the priority being
                        * raised above that of the running task? */
                    if( uxNewPriority > CurrentTCB->Priority )
                    {
                        xYieldRequired = true;
                    }
                }
                else
                {
                    /* The priority of the running task is being raised,
                        * but the running task must already be the highest
                        * priority task able to run so no yield is required. */
                }
            }
            else if( taskTASK_IS_RUNNING( pxTCB )){
                /* Setting the priority of a running task down means
                    * there may now be another task of higher priority that
                    * is ready to execute. */
                xYieldRequired = true;
            }
            PriorityUsedOnEntry = pxTCB->Priority;
            if( ( pxTCB->uxBasePriority == pxTCB->Priority ) || ( uxNewPriority > pxTCB->Priority ) ){
                pxTCB->Priority = uxNewPriority;
            }
            pxTCB->uxBasePriority = uxNewPriority;
            if((pxTCB->xEventListItem.Value & taskEVENT_LIST_ITEM_VALUE_IN_USE ) == ( (TickType_t) 0U ) ) {
                pxTCB->xEventListItem.Value = (TickType_t) configMAX_PRIORITIES - (TickType_t) uxNewPriority;
            }
            if( pxTCB->xStateListItem.Container == &ReadyTasksLists[PriorityUsedOnEntry]) {
                if(pxTCB->xStateListItem.remove() == 0) {
                    portRESET_READY_PRIORITY( PriorityUsedOnEntry, TopReadyPriority );
                }
                AddTaskToReadyList( pxTCB );
            }
            if( xYieldRequired != false ) {
                portYIELD_WITHIN_API( );
            }
            ( void ) PriorityUsedOnEntry;
        }
    }
    EXIT_CRITICAL();
}

void vTaskSuspend( TaskHandle_t xTaskToSuspend )
{
    TCB_t * pxTCB;
    ENTER_CRITICAL();
    {
        pxTCB = GetTCBFromHandle( xTaskToSuspend );
        if(pxTCB->xStateListItem.remove() == 0 )
        {
            taskRESET_READY_PRIORITY( pxTCB->Priority );
        }
        pxTCB->xEventListItem.ensureRemoved();
        
        SuspendedTaskList.append(&pxTCB->xStateListItem);
        BaseType_t x;
        for( x = ( BaseType_t ) 0; x < ( BaseType_t ) configTASK_NOTIFICATION_ARRAY_ENTRIES; x++ )
        {
            if( pxTCB->ucNotifyState[ x ] == taskWAITING_NOTIFICATION )
            {
                /* The task was blocked to wait for a notification, but is
                    * now suspended, so no notification was received. */
                pxTCB->ucNotifyState[ x ] = taskNOT_WAITING_NOTIFICATION;
            }
        }
    }
    EXIT_CRITICAL();
    {
        UBaseType_t uxCurrentListLength;
        if( SchedulerRunning != false )
        {
            /* Reset the next expected unblock time in case it referred to the
                * task that is now in the Suspended state. */
            ENTER_CRITICAL();
            {
                ResetNextTaskUnblockTime();
            }
            EXIT_CRITICAL();
        }
        
        if( pxTCB == CurrentTCB )
        {
            if( SchedulerRunning != false )
            {
                /* The current task has just been suspended. */
                configASSERT( SchedulerSuspended == 0 );
                portYIELD_WITHIN_API();
            }
            else
            {
                uxCurrentListLength = SuspendedTaskList.Length;
                if( uxCurrentListLength == CurrentNumberOfTasks )
                {
                    CurrentTCB = NULL;
                }
                else
                {
                    vTaskSwitchContext();
                }
            }
        }
        
    }
}

static BaseType_t TaskIsTaskSuspended( const TaskHandle_t xTask )
{
    BaseType_t xReturn = false;
    TCB_t * pxTCB = xTask;
    configASSERT( xTask );
    if(pxTCB->xStateListItem.Container == &SuspendedTaskList)
    {
        if(pxTCB->xEventListItem.Container != &PendingReadyList)
        {
            if(pxTCB->xEventListItem.Container == nullptr)
            {
                xReturn = true;
                for( BaseType_t x = ( BaseType_t ) 0; x < ( BaseType_t ) configTASK_NOTIFICATION_ARRAY_ENTRIES; x++ )
                {
                    if( pxTCB->ucNotifyState[ x ] == taskWAITING_NOTIFICATION )
                    {
                        xReturn = false;
                        break;
                    }
                }
            }
        }
        
    }
    return xReturn;
}

void vTaskResume( TaskHandle_t xTaskToResume )
{
    TCB_t * const pxTCB = xTaskToResume;
    /* It does not make sense to resume the calling task. */
    configASSERT( xTaskToResume );
    /* The parameter cannot be NULL as it is impossible to resume the
        * currently executing task. */
    if( ( pxTCB != CurrentTCB ) && ( pxTCB != NULL ) )
    {
        ENTER_CRITICAL();
        {
            if( TaskIsTaskSuspended( pxTCB ) != false )
            {
                pxTCB->xStateListItem.remove();
                AddTaskToReadyList( pxTCB );
                taskYIELD_ANY_CORE_IF_USING_PREEMPTION( pxTCB );
            }
        }
        EXIT_CRITICAL();
    }
}

BaseType_t xTaskResumeFromISR( TaskHandle_t xTaskToResume )
{
    BaseType_t xYieldRequired = false;
    TCB_t * const pxTCB = xTaskToResume;
    UBaseType_t uxSavedInterruptStatus;
    configASSERT( xTaskToResume );
    portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
    uxSavedInterruptStatus = ENTER_CRITICAL_FROM_ISR();
    {
        if( TaskIsTaskSuspended( pxTCB ) != false )
        {
            /* Check the ready lists can be accessed. */
            if( SchedulerSuspended == ( UBaseType_t ) 0U )
            {
                /* Ready lists can be accessed so move the task from the
                    * suspended list to the ready list directly. */
                if( pxTCB->Priority > CurrentTCB->Priority )
                {
                    xYieldRequired = true;
                    YieldPendings[ 0 ] = true;
                }
                pxTCB->xStateListItem.remove();
                AddTaskToReadyList( pxTCB );
            }
            else
            {
                PendingReadyList.append(&pxTCB->xEventListItem);
            }
        }
    }
    EXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );
    return xYieldRequired;
}

static BaseType_t CreateIdleTasks( void )
{
    BaseType_t xReturn = true;
    BaseType_t xCoreID;
    char cIdleName[ configMAX_TASK_NAME_LEN ];
    TaskFunction_t pxIdleTaskFunction = NULL;
    BaseType_t xIdleTaskNameIndex;
    for( xIdleTaskNameIndex = ( BaseType_t ) 0; xIdleTaskNameIndex < ( BaseType_t ) configMAX_TASK_NAME_LEN; xIdleTaskNameIndex++ )
    {
        cIdleName[ xIdleTaskNameIndex ] = configIDLE_TASK_NAME[ xIdleTaskNameIndex ];
        /* Don't copy all configMAX_TASK_NAME_LEN if the string is shorter than
         * configMAX_TASK_NAME_LEN characters just in case the memory after the
         * string is not accessible (extremely unlikely). */
        if( cIdleName[ xIdleTaskNameIndex ] == ( char ) 0x00 )
        {
            break;
        }
    }
    /* Add each idle task at the lowest priority. */
    for( xCoreID = ( BaseType_t ) 0; xCoreID < ( BaseType_t ) configNUMBER_OF_CORES; xCoreID++ )
    {
        pxIdleTaskFunction = IdleTask;
        StaticTask_t * pIdleTaskTCBBuffer = NULL;
        StackType_t * pxIdleTaskStackBuffer = NULL;
        configSTACK_DEPTH_TYPE IdleTaskStackSize;
        /* The Idle task is created using user provided RAM - obtain the
            * address of the RAM then create the idle task. */
        ApplicationGetIdleTaskMemory( &pIdleTaskTCBBuffer, &pxIdleTaskStackBuffer, &IdleTaskStackSize );
        IdleTaskHandles[ xCoreID ] = xTaskCreateStatic( pxIdleTaskFunction,
                                                            cIdleName,
                                                            IdleTaskStackSize,
                                                            ( void * ) NULL,
                                                            portPRIVILEGE_BIT, /* In effect ( tskIDLE_PRIORITY | portPRIVILEGE_BIT ), but tskIDLE_PRIORITY is zero. */
                                                            pxIdleTaskStackBuffer,
                                                            pIdleTaskTCBBuffer );
        if( IdleTaskHandles[ xCoreID ] != NULL )
        {
            xReturn = true;
        }
        else
        {
            xReturn = false;
        }
        /* Break the loop if any of the idle task is failed to be created. */
        if( xReturn == false )
        {
            break;
        }
    }
    return xReturn;
}

void vTaskStartScheduler( void )
{
    BaseType_t xReturn;
    xReturn = CreateIdleTasks();
    if( xReturn  )
    {
        xReturn = TimerCreateTimerTask();
    }
    if( xReturn  )
    {
        /* freertos_tasks_c_additions_init() should only be called if the user
         * definable macro FREERTOS_TASKS_C_ADDITIONS_INIT() is defined, as that is
         * the only macro called by the function. */
        #ifdef FREERTOS_TASKS_C_ADDITIONS_INIT
        {
            freertos_tasks_c_additions_init();
        }
        #endif
        /* Interrupts are turned off here, to ensure a tick does not occur
         * before or during the call to xPortStartScheduler().  The stacks of
         * the created tasks contain a status word with interrupts switched on
         * so interrupts will automatically get re-enabled when the first task
         * starts to run. */
        portDISABLE_INTERRUPTS();
        #if ( configUSE_C_RUNTIME_TLS_SUPPORT == 1 )
        {
            /* Switch C-Runtime's TLS Block to point to the TLS
             * block specific to the task that will run first. */
            configSET_TLS_BLOCK( CurrentTCB->xTLSBlock );
        }
        #endif
        NextTaskUnblockTime = portMAX_DELAY;
        SchedulerRunning = true;
        TickCount = (TickType_t) configINITIAL_TICK_COUNT;
        /* If configGENERATE_RUN_TIME_STATS is defined then the following
         * macro must be defined to configure the timer/counter used to generate
         * the run time counter time base.   NOTE:  If configGENERATE_RUN_TIME_STATS
         * is set to 0 and the following line fails to build then ensure you do not
         * have portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() defined in your
         * FreeRTOSConfig.h file. */
        portCONFIGURE_TIMER_FOR_RUN_TIME_STATS();
        /* Setting up the timer tick is hardware specific and thus in the
         * portable interface. */
        /* The return value for xPortStartScheduler is not required
         * hence using a void datatype. */
        ( void ) xPortStartScheduler();
        /* In most cases, xPortStartScheduler() will not return. If it
         * returns true then there was not enough heap memory available
         * to create either the Idle or the Timer task. If it returned
         * false, then the application called xTaskEndScheduler().
         * Most ports don't implement xTaskEndScheduler() as there is
         * nothing to return to. */
    }
    else
    {
        /* This line will only be reached if the kernel could not be started,
         * because there was not enough FreeRTOS heap to create the idle task
         * or the timer task. */
        configASSERT( xReturn != errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY );
    }
    /* Prevent compiler warnings if INCLUDE_xTaskGetIdleTaskHandle is set to 0,
     * meaning IdleTaskHandles are not used anywhere else. */
    ( void ) IdleTaskHandles;
    /* OpenOCD makes use of TopUsedPriority for thread debugging. Prevent TopUsedPriority
     * from getting optimized out as it is no longer used by the kernel. */
    ( void ) TopUsedPriority;
}

void vTaskEndScheduler( void )
{
    #if ( INCLUDE_vTaskDelete == 1 )
    {
        BaseType_t xCoreID;
        #if ( configUSE_TIMERS == 1 )
        {
            /* Delete the timer task created by the kernel. */
            vTaskDelete( TimerGetTimerDaemonTaskHandle() );
        }
        #endif /* #if ( configUSE_TIMERS == 1 ) */
        /* Delete Idle tasks created by the kernel.*/
        for( xCoreID = 0; xCoreID < ( BaseType_t ) configNUMBER_OF_CORES; xCoreID++ )
        {
            vTaskDelete( IdleTaskHandles[ xCoreID ] );
        }
        /* Idle task is responsible for reclaiming the resources of the tasks in
         * xTasksWaitingTermination list. Since the idle task is now deleted and
         * no longer going to run, we need to reclaim resources of all the tasks
         * in the xTasksWaitingTermination list. */
        CheckTasksWaitingTermination();
    }
    #endif /* #if ( INCLUDE_vTaskDelete == 1 ) */
    /* Stop the scheduler interrupts and call the portable scheduler end
     * routine so the original ISRs can be restored if necessary.  The port
     * layer must ensure interrupts enable  bit is left in the correct state. */
    portDISABLE_INTERRUPTS();
    SchedulerRunning = false;
    /* This function must be called from a task and the application is
     * responsible for deleting that task after the scheduler is stopped. */
    vPortEndScheduler();
}
/*----------------------------------------------------------*/
void vTaskSuspendAll( void )
{
    #if ( configNUMBER_OF_CORES == 1 )
    {
        /* A critical section is not required as the variable is of type
         * BaseType_t. Each task maintains its own context, and a context switch
         * cannot occur if the variable is non zero. So, as long as the writing
         * from the register back into the memory is atomic, it is not a
         * problem.
         *
         * Consider the following scenario, which starts with
         * SchedulerSuspended at zero.
         *
         * 1. load SchedulerSuspended into register.
         * 2. Now a context switch causes another task to run, and the other
         *    task uses the same variable. The other task will see the variable
         *    as zero because the variable has not yet been updated by the
         *    original task. Eventually the original task runs again. **That can
         *    only happen when SchedulerSuspended is once again zero**. When
         *    the original task runs again, the contents of the CPU registers
         *    are restored to exactly how they were when it was switched out -
         *    therefore the value it read into the register still matches the
         *    value of the SchedulerSuspended variable.
         *
         * 3. increment register.
         * 4. store register into SchedulerSuspended. The value restored to
         *    SchedulerSuspended will be the correct value of 1, even though
         *    the variable was used by other tasks in the mean time.
         */
        /* portSOFTWARE_BARRIER() is only implemented for emulated/simulated ports that
         * do not otherwise exhibit real time behaviour. */
        portSOFTWARE_BARRIER();
        /* The scheduler is suspended if SchedulerSuspended is non-zero.  An increment
         * is used to allow calls to vTaskSuspendAll() to nest. */
        SchedulerSuspended = ( UBaseType_t ) ( SchedulerSuspended + 1U );
        /* Enforces ordering for ports and optimised compilers that may otherwise place
         * the above increment elsewhere. */
        portMEMORY_BARRIER();
    }
    #else /* #if ( configNUMBER_OF_CORES == 1 ) */
    {
        UBaseType_t ulState;
        /* This must only be called from within a task. */
        portASSERT_IF_IN_ISR();
        if( SchedulerRunning != false )
        {
            /* Writes to SchedulerSuspended must be protected by both the task AND ISR locks.
             * We must disable interrupts before we grab the locks in the event that this task is
             * interrupted and switches context before incrementing SchedulerSuspended.
             * It is safe to re-enable interrupts after releasing the ISR lock and incrementing
             * SchedulerSuspended since that will prevent context switches. */
            ulState = portSET_INTERRUPT_MASK();
            /* This must never be called from inside a critical section. */
            configASSERT( portGET_CRITICAL_NESTING_COUNT() == 0 );
            /* portSOFRWARE_BARRIER() is only implemented for emulated/simulated ports that
             * do not otherwise exhibit real time behaviour. */
            portSOFTWARE_BARRIER();
            portGET_TASK_LOCK();
            /* SchedulerSuspended is increased after CheckForRunStateChange. The
             * purpose is to prevent altering the variable when fromISR APIs are readying
             * it. */
            if( SchedulerSuspended == 0U )
            {
                CheckForRunStateChange();
            }
            
            portGET_ISR_LOCK();
            /* The scheduler is suspended if SchedulerSuspended is non-zero. An increment
             * is used to allow calls to vTaskSuspendAll() to nest. */
            ++SchedulerSuspended;
            portRELEASE_ISR_LOCK();
            portCLEAR_INTERRUPT_MASK( ulState );
        }
    }
    #endif /* #if ( configNUMBER_OF_CORES == 1 ) */
}
/*----------------------------------------------------------*/
#if ( configUSE_TICKLESS_IDLE != 0 )
    static TickType_t GetExpectedIdleTime( void )
    {
        TickType_t xReturn;
        BaseType_t xHigherPriorityReadyTasks = false;
        /* xHigherPriorityReadyTasks takes care of the case where
         * configUSE_PREEMPTION is 0, so there may be tasks above the idle priority
         * task that are in the Ready state, even though the idle task is
         * running. */
        #if ( configUSE_PORT_OPTIMISED_TASK_SELECTION == 0 )
        {
            if( TopReadyPriority > tskIDLE_PRIORITY )
            {
                xHigherPriorityReadyTasks = true;
            }
        }
        #else
        {
            const UBaseType_t uxLeastSignificantBit = ( UBaseType_t ) 0x01;
            /* When port optimised task selection is used the TopReadyPriority
             * variable is used as a bit map.  If bits other than the least
             * significant bit are set then there are tasks that have a priority
             * above the idle priority that are in the Ready state.  This takes
             * care of the case where the co-operative scheduler is in use. */
            if( TopReadyPriority > uxLeastSignificantBit )
            {
                xHigherPriorityReadyTasks = true;
            }
        }
        #endif /* if ( configUSE_PORT_OPTIMISED_TASK_SELECTION == 0 ) */
        if( CurrentTCB->Priority > tskIDLE_PRIORITY )
        {
            xReturn = 0;
        }
        else if( CURRENT_LIST_LENGTH( &( ReadyTasksLists[ tskIDLE_PRIORITY ] ) ) > 1U )
        {
            /* There are other idle priority tasks in the ready state.  If
             * time slicing is used then the very next tick interrupt must be
             * processed. */
            xReturn = 0;
        }
        else if( xHigherPriorityReadyTasks != false )
        {
            /* There are tasks in the Ready state that have a priority above the
             * idle priority.  This path can only be reached if
             * configUSE_PREEMPTION is 0. */
            xReturn = 0;
        }
        else
        {
            xReturn = NextTaskUnblockTime;
            xReturn -= TickCount;
        }
        return xReturn;
    }
#endif /* configUSE_TICKLESS_IDLE */
/*----------------------------------------------------------*/
BaseType_t TaskResumeAll( void )
{
    TCB_t * pxTCB = NULL;
    BaseType_t xAlreadyYielded = false;
    #if ( configNUMBER_OF_CORES > 1 )
        if( SchedulerRunning != false )
    #endif
    {
        /* It is possible that an ISR caused a task to be removed from an event
         * list while the scheduler was suspended.  If this was the case then the
         * removed task will have been added to the PendingReadyList.  Once the
         * scheduler has been resumed it is safe to move all the pending ready
         * tasks from this list into their appropriate ready list. */
        ENTER_CRITICAL();
        {
            BaseType_t xCoreID;
            xCoreID = ( BaseType_t ) portGET_CORE_ID();
            /* If SchedulerSuspended is zero then this function does not match a
             * previous call to vTaskSuspendAll(). */
            configASSERT( SchedulerSuspended != 0U );
            SchedulerSuspended = ( UBaseType_t ) ( SchedulerSuspended - 1U );
            portRELEASE_TASK_LOCK();
            if( SchedulerSuspended == ( UBaseType_t ) 0U )
            {
                if( CurrentNumberOfTasks > ( UBaseType_t ) 0U )
                {
                    while(PendingReadyList.Length > 0)
                    {
                        pxTCB = PendingReadyList.head()->Owner;
                        pxTCB->xEventListItem.remove();
                        portMEMORY_BARRIER();
                        pxTCB->xStateListItem.remove();
                        AddTaskToReadyList( pxTCB );
                        #if ( configNUMBER_OF_CORES == 1 )
                        {
                            /* If the moved task has a priority higher than the current
                             * task then a yield must be performed. */
                            if( pxTCB->Priority > CurrentTCB->Priority )
                            {
                                YieldPendings[ xCoreID ] = true;
                            }
                        }
                        #else /* #if ( configNUMBER_OF_CORES == 1 ) */
                        {
                            /* All appropriate tasks yield at the moment a task is added to PendingReadyList.
                             * If the current core yielded then vTaskSwitchContext() has already been called
                             * which sets YieldPendings for the current core to true. */
                        }
                        #endif /* #if ( configNUMBER_OF_CORES == 1 ) */
                    }
                    if( pxTCB != NULL )
                    {
                        /* A task was unblocked while the scheduler was suspended,
                         * which may have prevented the next unblock time from being
                         * re-calculated, in which case re-calculate it now.  Mainly
                         * important for low power tickless implementations, where
                         * this can prevent an unnecessary exit from low power
                         * state. */
                        ResetNextTaskUnblockTime();
                    }
                    /* If any ticks occurred while the scheduler was suspended then
                     * they should be processed now.  This ensures the tick count does
                     * not  slip, and that any delayed tasks are resumed at the correct
                     * time.
                     *
                     * It should be safe to call xTaskIncrementTick here from any core
                     * since we are in a critical section and xTaskIncrementTick itself
                     * protects itself within a critical section. Suspending the scheduler
                     * from any core causes xTaskIncrementTick to increment uxPendedCounts. */
                    {
                        TickType_t xPendedCounts = PendedTicks; /* Non-volatile copy. */
                        if( xPendedCounts > (TickType_t) 0U )
                        {
                            do
                            {
                                if( xTaskIncrementTick() != false )
                                {
                                    /* Other cores are interrupted from
                                     * within xTaskIncrementTick(). */
                                    YieldPendings[ xCoreID ] = true;
                                }
                                --xPendedCounts;
                            } while( xPendedCounts > (TickType_t) 0U );
                            PendedTicks = 0;
                        }
                    }
                    if( YieldPendings[ xCoreID ] != false )
                    {
                        #if ( configUSE_PREEMPTION != 0 )
                        {
                            xAlreadyYielded = true;
                        }
                        #endif /* #if ( configUSE_PREEMPTION != 0 ) */
                        portYIELD_WITHIN_API( );
                    }
                }
            }
            
        }
        EXIT_CRITICAL();
    }
    return xAlreadyYielded;
}

TickType_t xTaskGetTickCount( void )
{
    portTICK_TYPE_ENTER_CRITICAL();
    TickType_t xTicks = TickCount;
    portTICK_TYPE_EXIT_CRITICAL();
    return xTicks;
}

TickType_t xTaskGetTickCountFromISR( void )
{
    TickType_t xReturn;
    UBaseType_t uxSavedInterruptStatus;
    portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
    uxSavedInterruptStatus = portTICK_TYPE_SET_INTERRUPT_MASK_FROM_ISR();
    {
        xReturn = TickCount;
    }
    portTICK_TYPE_CLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus );
    return xReturn;
}

UBaseType_t TaskGetNumberOfTasks( void )
{
    return CurrentNumberOfTasks;
}

char * pcTaskGetName( TaskHandle_t xTaskToQuery )
{
    TCB_t * pxTCB;
    pxTCB = GetTCBFromHandle( xTaskToQuery );
    configASSERT( pxTCB );
    return &( pxTCB->pcTaskName[ 0 ] );
}

BaseType_t xTaskGetStaticBuffers( TaskHandle_t xTask,
                                    StackType_t ** pStackBuffer,
                                    StaticTask_t ** pTaskBuffer )
{
    TCB_t * pxTCB;
    configASSERT( pStackBuffer != NULL );
    configASSERT( pTaskBuffer != NULL );
    pxTCB = GetTCBFromHandle( xTask );
    if( pxTCB->StaticallyAllocated == tskSTATICALLY_ALLOCATED_STACK_AND_TCB )
    {
        *pStackBuffer = pxTCB->pxStack;
        *pTaskBuffer = ( StaticTask_t * ) pxTCB;
        return true;
    }
    if( pxTCB->StaticallyAllocated == tskSTATICALLY_ALLOCATED_STACK_ONLY )
    {
        *pStackBuffer = pxTCB->pxStack;
        *pTaskBuffer = NULL;
        return true;
    }
    return true;
}
TaskHandle_t xTaskGetIdleTaskHandle( void )
{
    /* If xTaskGetIdleTaskHandle() is called before the scheduler has been
        * started, then IdleTaskHandles will be NULL. */
    configASSERT( ( IdleTaskHandles[ 0 ] != NULL ) );
    return IdleTaskHandles[ 0 ];
}
TaskHandle_t xTaskGetIdleTaskHandleForCore( BaseType_t xCoreID )
{
    /* Ensure the core ID is valid. */
    configASSERT( taskVALID_CORE_ID( xCoreID )  );
    /* If xTaskGetIdleTaskHandle() is called before the scheduler has been
        * started, then IdleTaskHandles will be NULL. */
    configASSERT( ( IdleTaskHandles[ xCoreID ] != NULL ) );
    return IdleTaskHandles[ xCoreID ];
}

BaseType_t xTaskCatchUpTicks( TickType_t xTicksToCatchUp ) {
    BaseType_t xYieldOccurred;
    configASSERT( SchedulerSuspended == ( UBaseType_t ) 0U );
    vTaskSuspendAll();
    ENTER_CRITICAL();
    PendedTicks += xTicksToCatchUp;
    EXIT_CRITICAL();
    return TaskResumeAll();
}

BaseType_t xTaskAbortDelay( TaskHandle_t xTask ) {
    TCB_t * pxTCB = xTask;
    BaseType_t xReturn;
    configASSERT( pxTCB );
    vTaskSuspendAll();
    {
        if( eTaskGetState( xTask ) == eBlocked )
        {
            xReturn = true;
            pxTCB->xStateListItem.remove();
            ENTER_CRITICAL();
            {
                if(pxTCB->xEventListItem.Container != NULL)
                {
                    pxTCB->xEventListItem.remove();
                    pxTCB->ucDelayAborted = ( uint8_t ) true;
                }
            }
            EXIT_CRITICAL();
            AddTaskToReadyList( pxTCB );
            if( pxTCB->Priority > CurrentTCB->Priority )
            {
                YieldPendings[ 0 ] = true;
            }
        }
        else
        {
            xReturn = false;
        }
    }
    ( void ) TaskResumeAll();
    return xReturn;
}

BaseType_t xTaskIncrementTick( void )
{
    TCB_t * pxTCB;
    TickType_t Value;
    BaseType_t xSwitchRequired = false;
    if( SchedulerSuspended == ( UBaseType_t ) 0U )
    {
        const TickType_t ConstTickCount = TickCount + (TickType_t) 1;
        TickCount = ConstTickCount;
        if( ConstTickCount == (TickType_t) 0U )
        {
            taskSWITCH_DELAYED_LISTS();
        }
        if( ConstTickCount >= NextTaskUnblockTime )
        {
            for( ; ; )
            {
                if(DelayedTaskList->empty()) {
                    NextTaskUnblockTime = portMAX_DELAY;
                    break;
                } else {
                    pxTCB = DelayedTaskList->head()->Owner;
                    Value = pxTCB->xStateListItem.Value;
                    if( ConstTickCount < Value ) {
                        NextTaskUnblockTime = Value;
                        break;
                    }
                    pxTCB->xStateListItem.remove();
                    pxTCB->xEventListItem.ensureRemoved();
                    AddTaskToReadyList( pxTCB );
                    if( pxTCB->Priority > CurrentTCB->Priority )
                    {
                        xSwitchRequired = true;
                    }
                }
            }
        }
        if(ReadyTasksLists[ CurrentTCB->Priority ].Length > 1U )
        {
            xSwitchRequired = true;
        }
        if( PendedTicks == (TickType_t) 0 )
        {
            vApplicationTickHook();
        }
        if( YieldPendings[ 0 ] != false )
        {
            xSwitchRequired = true;
        }
    }
    else
    {
        PendedTicks += 1U;
        /* The tick hook gets called at regular intervals, even if the
         * scheduler is locked. */
        #if ( configUSE_TICK_HOOK == 1 )
        {
            vApplicationTickHook();
        }
        #endif
    }
    
    return xSwitchRequired;
}


void vTaskSwitchContext( void )
{
    if( SchedulerSuspended != ( UBaseType_t ) 0U )
    {
        YieldPendings[ 0 ] = true;
    }
    else
    {
        YieldPendings[ 0 ] = false;
        taskCHECK_FOR_STACK_OVERFLOW();
        taskSELECT_HIGHEST_PRIORITY_TASK();
        portTASK_SWITCH_HOOK( CurrentTCB );
    }
}

void vTaskPlaceOnEventList( List_t<TCB_t> * const pxEventList,
                            const TickType_t TicksToWait )
{
    configASSERT( pxEventList );
    pxEventList->insert(&( CurrentTCB->xEventListItem ) );
    AddCurrentTaskToDelayedList( TicksToWait, true );   
}

void vTaskPlaceOnUnorderedEventList( List_t<TCB_t> * pxEventList,
                                     const TickType_t Value,
                                     const TickType_t TicksToWait )
{
    configASSERT( pxEventList );
    configASSERT( SchedulerSuspended != ( UBaseType_t ) 0U );
    CurrentTCB->xEventListItem.Value = Value | taskEVENT_LIST_ITEM_VALUE_IN_USE;
    pxEventList->append(&(CurrentTCB->xEventListItem));
    AddCurrentTaskToDelayedList( TicksToWait, true );
}

void vTaskPlaceOnEventListRestricted( List_t<TCB_t> * const pxEventList,
                                        TickType_t TicksToWait,
                                        const BaseType_t xWaitIndefinitely )
{
    configASSERT( pxEventList );
    pxEventList->append(&(CurrentTCB->xEventListItem));
    AddCurrentTaskToDelayedList( xWaitIndefinitely ? portMAX_DELAY : TicksToWait, xWaitIndefinitely );
}

BaseType_t xTaskRemoveFromEventList(List_t<TCB_t> * const pxEventList )
{
    TCB_t *UnblockedTCB = pxEventList->head()->Owner;
    configASSERT( UnblockedTCB );
    UnblockedTCB->xEventListItem.remove();
    if( SchedulerSuspended == ( UBaseType_t ) 0U ) {
        UnblockedTCB->xStateListItem.remove();
        AddTaskToReadyList( UnblockedTCB );
    } else {
        PendingReadyList.append(&UnblockedTCB->xEventListItem);
    }
    if( UnblockedTCB->Priority > CurrentTCB->Priority ) {
        YieldPendings[ 0 ] = true;
        return true;
    }
    return false;
}

void vTaskRemoveFromUnorderedEventList( Item_t<TCB_t> * EventListItem,
                                        const TickType_t ItemValue )
{
    configASSERT( SchedulerSuspended != ( UBaseType_t ) 0U );
    EventListItem->Value = ItemValue | taskEVENT_LIST_ITEM_VALUE_IN_USE;
    TCB_t *UnblockedTCB = EventListItem->Owner;
    configASSERT( UnblockedTCB );
    EventListItem->remove();
    UnblockedTCB->xStateListItem.remove();
    AddTaskToReadyList( UnblockedTCB );
    if( UnblockedTCB->Priority > CurrentTCB->Priority )
    {
        YieldPendings[ 0 ] = true;
    }
}
void vTaskSetTimeOutState( TimeOut_t * const TimeOut )
{
    configASSERT( TimeOut );
    ENTER_CRITICAL();
    TimeOut->xOverflowCount = NumOfOverflows;
    TimeOut->xTimeOnEntering = TickCount;
    EXIT_CRITICAL();
}
void vTaskInternalSetTimeOutState( TimeOut_t * const pxTimeOut )
{
    /* For internal use only as it does not use a critical section. */
    pxTimeOut->xOverflowCount = NumOfOverflows;
    pxTimeOut->xTimeOnEntering = TickCount;
}
BaseType_t xTaskCheckForTimeOut( TimeOut_t * const pxTimeOut,
                                 TickType_t * const pTicksToWait )
{
    BaseType_t xReturn;
    configASSERT( pxTimeOut );
    configASSERT( pTicksToWait );
    ENTER_CRITICAL();
    {
        /* Minor optimisation.  The tick count cannot change in this block. */
        const TickType_t ConstTickCount = TickCount;
        const TickType_t xElapsedTime = ConstTickCount - pxTimeOut->xTimeOnEntering;
        if( CurrentTCB->ucDelayAborted != ( uint8_t ) false )
        {
            /* The delay was aborted, which is not the same as a time out,
                * but has the same result. */
            CurrentTCB->ucDelayAborted = ( uint8_t ) false;
            xReturn = true;
        }
        else
        if( *pTicksToWait == portMAX_DELAY )
        {
            /* If INCLUDE_vTaskSuspend is set to 1 and the block time
                * specified is the maximum block time then the task should block
                * indefinitely, and therefore never time out. */
            xReturn = false;
        }
        else if( ( NumOfOverflows != pxTimeOut->xOverflowCount ) && ( ConstTickCount >= pxTimeOut->xTimeOnEntering ) )
        {
            /* The tick count is greater than the time at which
             * vTaskSetTimeout() was called, but has also overflowed since
             * vTaskSetTimeOut() was called.  It must have wrapped all the way
             * around and gone past again. This passed since vTaskSetTimeout()
             * was called. */
            xReturn = true;
            *pTicksToWait = (TickType_t) 0;
        }
        else if( xElapsedTime < *pTicksToWait )
        {
            /* Not a genuine timeout. Adjust parameters for time remaining. */
            *pTicksToWait -= xElapsedTime;
            vTaskInternalSetTimeOutState( pxTimeOut );
            xReturn = false;
        }
        else
        {
            *pTicksToWait = (TickType_t) 0;
            xReturn = true;
        }
    }
    EXIT_CRITICAL();
    return xReturn;
}

void vTaskMissedYield( void )
{
    /* Must be called from within a critical section. */
    YieldPendings[ portGET_CORE_ID() ] = true;
}
/*
 * -----------------------------------------------------------
 * The idle task.
 * ----------------------------------------------------------
 *
 * The portTASK_FUNCTION() macro is used to allow port/compiler specific
 * language extensions.  The equivalent prototype for this function is:
 *
 * void IdleTask( void *Parameters );
 */
static portTASK_FUNCTION( IdleTask, Parameters )
{
    /* Stop warnings. */
    ( void ) Parameters;
    /** THIS IS THE RTOS IDLE TASK - WHICH IS CREATED AUTOMATICALLY WHEN THE
     * SCHEDULER IS STARTED. **/
    /* In case a task that has a secure context deletes itself, in which case
     * the idle task is responsible for deleting the task's secure context, if
     * any. */
    portALLOCATE_SECURE_CONTEXT( configMINIMAL_SECURE_STACK_SIZE );
    for( ; configCONTROL_INFINITE_LOOP(); )
    {
        /* See if any tasks have deleted themselves - if so then the idle task
         * is responsible for freeing the deleted task's TCB and stack. */
        CheckTasksWaitingTermination();
        #if ( ( configUSE_PREEMPTION == 1 ) && ( configIDLE_SHOULD_YIELD == 1 ) )
        {
            /* When using preemption tasks of equal priority will be
             * timesliced.  If a task that is sharing the idle priority is ready
             * to run then the idle task should yield before the end of the
             * timeslice.
             *
             * A critical region is not required here as we are just reading from
             * the list, and an occasional incorrect value will not matter.  If
             * the ready list at the idle priority contains one more task than the
             * number of idle tasks, which is equal to the configured numbers of cores
             * then a task other than the idle task is ready to execute. */
            if( ReadyTasksLists[ tskIDLE_PRIORITY ].Length > ( UBaseType_t ) configNUMBER_OF_CORES )
            {
                taskYIELD();
            }
            
        }
        #endif /* ( ( configUSE_PREEMPTION == 1 ) && ( configIDLE_SHOULD_YIELD == 1 ) ) */
        /* Call the user defined function from within the idle task. */
        vApplicationIdleHook();
    }
}

static void InitialiseTaskLists( void )
{
    UBaseType_t Priority;
    for( Priority = ( UBaseType_t ) 0U; Priority < ( UBaseType_t ) configMAX_PRIORITIES; Priority++ )
    {
        (ReadyTasksLists[Priority]).init();
    }
    DelayedTaskList1.init();
    DelayedTaskList2.init();
    PendingReadyList.init();
    xTasksWaitingTermination.init();
    SuspendedTaskList.init();
    /* Start with pxDelayedTaskList using list1 and the pxOverflowDelayedTaskList
     * using list2. */
    DelayedTaskList = &DelayedTaskList1;
    OverflowDelayedTaskList = &DelayedTaskList2;
}

static void CheckTasksWaitingTermination( void )
{
    /** THIS FUNCTION IS CALLED FROM THE RTOS IDLE TASK **/
    TCB_t * pxTCB;
    /* uxDeletedTasksWaitingCleanUp is used to prevent ENTER_CRITICAL()
        * being called too often in the idle task. */
    while( uxDeletedTasksWaitingCleanUp > ( UBaseType_t ) 0U )
    {
        ENTER_CRITICAL();
        {
            pxTCB = xTasksWaitingTermination.head()->Owner;
            pxTCB->xStateListItem.remove();
            --CurrentNumberOfTasks;
            --uxDeletedTasksWaitingCleanUp;
        }
        EXIT_CRITICAL();
        DeleteTCB( pxTCB );
    }
}

#if ( INCLUDE_vTaskDelete == 1 )
    static void DeleteTCB( TCB_t * pxTCB )
    {
        /* This call is required specifically for the TriCore port.  It must be
         * above the vPortFree() calls.  The call is also used by ports/demos that
         * want to allocate and clean RAM statically. */
        portCLEAN_UP_TCB( pxTCB );
        /* The task could have been allocated statically or dynamically, so
            * check what was statically allocated before trying to free the
            * memory. */
        if( pxTCB->StaticallyAllocated == tskDYNAMICALLY_ALLOCATED_STACK_AND_TCB )
        {
            /* Both the stack and TCB were allocated dynamically, so both
                * must be freed. */
            vPortFreeStack( pxTCB->pxStack );
            vPortFree( pxTCB );
        }
        else if( pxTCB->StaticallyAllocated == tskSTATICALLY_ALLOCATED_STACK_ONLY )
        {
            /* Only the stack was statically allocated, so the TCB is the
                * only memory that must be freed. */
            vPortFree( pxTCB );
        }
        else
        {
            /* Neither the stack nor the TCB were allocated dynamically, so
                * nothing needs to be freed. */
            configASSERT( pxTCB->StaticallyAllocated == tskSTATICALLY_ALLOCATED_STACK_AND_TCB );
            mtCOVERAGE_TEST_MARKER();
        }
    }
#endif /* INCLUDE_vTaskDelete */

static void ResetNextTaskUnblockTime( void )
{
    if(DelayedTaskList->empty())
    {
        NextTaskUnblockTime = portMAX_DELAY;
    }
    else
    {
        NextTaskUnblockTime = DelayedTaskList->head()->Value;
    }
}

TaskHandle_t xTaskGetCurrentTaskHandle( void )
{
    return CurrentTCB;
}

#if ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) )
    BaseType_t xTaskGetSchedulerState( void )
    {
        BaseType_t xReturn;
        if( SchedulerRunning == false )
        {
            xReturn = taskSCHEDULER_NOT_STARTED;
        }
        else
        {
            if( SchedulerSuspended == ( UBaseType_t ) 0U )
            {
                xReturn = taskSCHEDULER_RUNNING;
            }
            else
            {
                xReturn = taskSCHEDULER_SUSPENDED;
            }
        }
        return xReturn;
    }
#endif /* ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) ) */

#if ( configUSE_MUTEXES == 1 )
    BaseType_t xTaskPriorityInherit( TaskHandle_t const pxMutexHolder )
    {
        TCB_t * const pxMutexHolderTCB = pxMutexHolder;
        BaseType_t xReturn = false;
        /* If the mutex is taken by an interrupt, the mutex holder is NULL. Priority
         * inheritance is not applied in this scenario. */
        if( pxMutexHolder != NULL )
        {
            /* If the holder of the mutex has a priority below the priority of
             * the task attempting to obtain the mutex then it will temporarily
             * inherit the priority of the task attempting to obtain the mutex. */
            if( pxMutexHolderTCB->Priority < CurrentTCB->Priority )
            {
                /* Adjust the mutex holder state to account for its new
                 * priority.  Only reset the event list item value if the value is
                 * not being used for anything else. */
                if((pxMutexHolderTCB->xEventListItem.Value & taskEVENT_LIST_ITEM_VALUE_IN_USE ) == ( (TickType_t) 0U ) )
                {
                    pxMutexHolderTCB->xEventListItem.Value = (TickType_t) configMAX_PRIORITIES - (TickType_t) CurrentTCB->Priority;
                }
                if( pxMutexHolderTCB->xStateListItem.Container == &(ReadyTasksLists[pxMutexHolderTCB->Priority]))
                {
                    if(pxMutexHolderTCB->xStateListItem.remove() == 0)
                    {
                        portRESET_READY_PRIORITY( pxMutexHolderTCB->Priority, TopReadyPriority );
                    }
                    pxMutexHolderTCB->Priority = CurrentTCB->Priority;
                    AddTaskToReadyList( pxMutexHolderTCB );
                }
                else
                {
                    /* Just inherit the priority. */
                    pxMutexHolderTCB->Priority = CurrentTCB->Priority;
                }
                /* Inheritance occurred. */
                xReturn = true;
            }
            else
            {
                if( pxMutexHolderTCB->uxBasePriority < CurrentTCB->Priority )
                {
                    /* The base priority of the mutex holder is lower than the
                     * priority of the task attempting to take the mutex, but the
                     * current priority of the mutex holder is not lower than the
                     * priority of the task attempting to take the mutex.
                     * Therefore the mutex holder must have already inherited a
                     * priority, but inheritance would have occurred if that had
                     * not been the case. */
                    xReturn = true;
                }
            }
        }

        return xReturn;
    }
#endif /* configUSE_MUTEXES */

#if ( configUSE_MUTEXES == 1 )
    BaseType_t xTaskPriorityDisinherit( TaskHandle_t const pxMutexHolder )
    {
        TCB_t * const pxTCB = pxMutexHolder;
        BaseType_t xReturn = false;
        if( pxMutexHolder != NULL )
        {
            /* A task can only have an inherited priority if it holds the mutex.
             * If the mutex is held by a task then it cannot be given from an
             * interrupt, and if a mutex is given by the holding task then it must
             * be the running state task. */
            configASSERT( pxTCB == CurrentTCB );
            configASSERT( pxTCB->uxMutexesHeld );
            ( pxTCB->uxMutexesHeld )--;
            /* Has the holder of the mutex inherited the priority of another
             * task? */
            if( pxTCB->Priority != pxTCB->uxBasePriority )
            {
                /* Only disinherit if no other mutexes are held. */
                if( pxTCB->uxMutexesHeld == ( UBaseType_t ) 0 )
                {
                    if(pxTCB->xStateListItem.remove() == 0)
                    {
                        portRESET_READY_PRIORITY( pxTCB->Priority, TopReadyPriority );
                    }
                    /* Disinherit the priority before adding the task into the
                     * new  ready list. */                    pxTCB->Priority = pxTCB->uxBasePriority;
                    /* Reset the event list item value.  It cannot be in use for
                     * any other purpose if this task is running, and it must be
                     * running to give back the mutex. */
                    pxTCB->xEventListItem.Value = (TickType_t) configMAX_PRIORITIES - (TickType_t) pxTCB->Priority;
                    AddTaskToReadyList( pxTCB );
                    /* Return true to indicate that a context switch is required.
                     * This is only actually required in the corner case whereby
                     * multiple mutexes were held and the mutexes were given back
                     * in an order different to that in which they were taken.
                     * If a context switch did not occur when the first mutex was
                     * returned, even if a task was waiting on it, then a context
                     * switch should occur when the last mutex is returned whether
                     * a task is waiting on it or not. */
                    xReturn = true;
                }
            }
        }
        return xReturn;
    }
#endif /* configUSE_MUTEXES */

#if ( configUSE_MUTEXES == 1 )
    void vTaskPriorityDisinheritAfterTimeout( TaskHandle_t const pxMutexHolder,
                                              UBaseType_t uxHighestPriorityWaitingTask )
    {
        TCB_t * const pxTCB = pxMutexHolder;
        UBaseType_t PriorityUsedOnEntry, PriorityToUse;
        const UBaseType_t uxOnlyOneMutexHeld = ( UBaseType_t ) 1;
        if( pxMutexHolder != NULL )
        {
            /* If pxMutexHolder is not NULL then the holder must hold at least
             * one mutex. */
            configASSERT( pxTCB->uxMutexesHeld );
            /* Determine the priority to which the priority of the task that
             * holds the mutex should be set.  This will be the greater of the
             * holding task's base priority and the priority of the highest
             * priority task that is waiting to obtain the mutex. */
            if( pxTCB->uxBasePriority < uxHighestPriorityWaitingTask )
            {
                PriorityToUse = uxHighestPriorityWaitingTask;
            }
            else
            {
                PriorityToUse = pxTCB->uxBasePriority;
            }
            /* Does the priority need to change? */
            if( pxTCB->Priority != PriorityToUse )
            {
                /* Only disinherit if no other mutexes are held.  This is a
                 * simplification in the priority inheritance implementation.  If
                 * the task that holds the mutex is also holding other mutexes then
                 * the other mutexes may have caused the priority inheritance. */
                if( pxTCB->uxMutexesHeld == uxOnlyOneMutexHeld )
                {
                    /* If a task has timed out because it already holds the
                     * mutex it was trying to obtain then it cannot of inherited
                     * its own priority. */
                    configASSERT( pxTCB != CurrentTCB );
                    /* Disinherit the priority, remembering the previous
                     * priority to facilitate determining the subject task's
                     * state. */                    PriorityUsedOnEntry = pxTCB->Priority;
                    pxTCB->Priority = PriorityToUse;
                    /* Only reset the event list item value if the value is not
                     * being used for anything else. */
                    if((pxTCB->xEventListItem.Value & taskEVENT_LIST_ITEM_VALUE_IN_USE ) == ( (TickType_t) 0U ) )
                    {
                        pxTCB->xEventListItem.Value = (TickType_t)configMAX_PRIORITIES-(TickType_t)PriorityToUse;
                    }
                    if(pxTCB->xStateListItem.Container == &ReadyTasksLists[PriorityUsedOnEntry])
                    {
                        if(pxTCB->xStateListItem.remove() == 0)
                        {
                            portRESET_READY_PRIORITY( pxTCB->Priority, TopReadyPriority );
                        }
                        AddTaskToReadyList( pxTCB );
                    }
                }
            }
        }
    }
#endif /* configUSE_MUTEXES */

#if ( ( portCRITICAL_NESTING_IN_TCB == 1 ) && ( configNUMBER_OF_CORES == 1 ) )
    void vTaskEnterCritical( void )
    {
        portDISABLE_INTERRUPTS();
        if( SchedulerRunning != false )
        {
            ( CurrentTCB->uxCriticalNesting )++;
            /* This is not the interrupt safe version of the enter critical
             * function so  assert() if it is being called from an interrupt
             * context.  Only API functions that end in "FromISR" can be used in an
             * interrupt.  Only assert if the critical nesting count is 1 to
             * protect against recursive calls if the assert function also uses a
             * critical section. */
            if( CurrentTCB->uxCriticalNesting == 1U )
            {
                portASSERT_IF_IN_ISR();
            }
        }
    }
#endif /* #if ( ( portCRITICAL_NESTING_IN_TCB == 1 ) && ( configNUMBER_OF_CORES == 1 ) ) */

#if ( ( portCRITICAL_NESTING_IN_TCB == 1 ) && ( configNUMBER_OF_CORES == 1 ) )
    void vTaskExitCritical( void )
    {
        if( SchedulerRunning != false )
        {
            /* If CurrentTCB->uxCriticalNesting is zero then this function
             * does not match a previous call to vTaskEnterCritical(). */
            configASSERT( CurrentTCB->uxCriticalNesting > 0U );
            /* This function should not be called in ISR. Use vTaskExitCriticalFromISR
             * to exit critical section from ISR. */
            portASSERT_IF_IN_ISR();
            if( CurrentTCB->uxCriticalNesting > 0U )
            {
                ( CurrentTCB->uxCriticalNesting )--;
                if( CurrentTCB->uxCriticalNesting == 0U )
                {
                    portENABLE_INTERRUPTS();
                }
            }
            
        }
    }
#endif /* #if ( ( portCRITICAL_NESTING_IN_TCB == 1 ) && ( configNUMBER_OF_CORES == 1 ) ) */

TickType_t uxTaskResetEventItemValue( void )
{
    TickType_t uxReturn = CurrentTCB->xEventListItem.Value;
    /* Reset the event list item to its normal value - so it can be used with
     * queues and semaphores. */
    CurrentTCB->xEventListItem.Value = (TickType_t)configMAX_PRIORITIES-(TickType_t)CurrentTCB->Priority; 
    return uxReturn;
}

#if ( configUSE_MUTEXES == 1 )
    TaskHandle_t pvTaskIncrementMutexHeldCount( void )
    {
        TCB_t * pxTCB;
        pxTCB = CurrentTCB;
        /* If xSemaphoreCreateMutex() is called before any tasks have been created
         * then CurrentTCB will be NULL. */
        if( pxTCB != NULL )
        {
            ( pxTCB->uxMutexesHeld )++;
        }
        return pxTCB;
    }
#endif /* configUSE_MUTEXES */

#if ( configUSE_TASK_NOTIFICATIONS == 1 )
    uint32_t ulTaskGenericNotifyTake( UBaseType_t uxIndexToWaitOn,
                                      BaseType_t xClearCountOnExit,
                                      TickType_t TicksToWait )
    {
        uint32_t ulReturn;
        BaseType_t xAlreadyYielded, xShouldBlock = false;
        configASSERT( uxIndexToWaitOn < configTASK_NOTIFICATION_ARRAY_ENTRIES );
        /* We suspend the scheduler here as AddCurrentTaskToDelayedList is a
         * non-deterministic operation. */
        vTaskSuspendAll();
        {
            /* We MUST enter a critical section to atomically check if a notification
             * has occurred and set the flag to indicate that we are waiting for
             * a notification. If we do not do so, a notification sent from an ISR
             * will get lost. */
            ENTER_CRITICAL();
            {
                /* Only block if the notification count is not already non-zero. */
                if( CurrentTCB->ulNotifiedValue[ uxIndexToWaitOn ] == 0U )
                {
                    /* Mark this task as waiting for a notification. */
                    CurrentTCB->ucNotifyState[ uxIndexToWaitOn ] = taskWAITING_NOTIFICATION;
                    if( TicksToWait > (TickType_t) 0 )
                    {
                        xShouldBlock = true;
                    }
                }
            }
            EXIT_CRITICAL();
            /* We are now out of the critical section but the scheduler is still
             * suspended, so we are safe to do non-deterministic operations such
             * as AddCurrentTaskToDelayedList. */
            if( xShouldBlock  )
            {                AddCurrentTaskToDelayedList( TicksToWait, true );
            }
            
        }
        xAlreadyYielded = TaskResumeAll();
        /* Force a reschedule if TaskResumeAll has not already done so. */
        if( ( xShouldBlock  ) && ( xAlreadyYielded == false ) )
        {
            taskYIELD_WITHIN_API();
        }

        ENTER_CRITICAL();
        {            ulReturn = CurrentTCB->ulNotifiedValue[ uxIndexToWaitOn ];
            if( ulReturn != 0U )
            {
                if( xClearCountOnExit != false )
                {
                    CurrentTCB->ulNotifiedValue[ uxIndexToWaitOn ] = ( uint32_t ) 0U;
                }
                else
                {
                    CurrentTCB->ulNotifiedValue[ uxIndexToWaitOn ] = ulReturn - ( uint32_t ) 1;
                }
            }
            
            CurrentTCB->ucNotifyState[ uxIndexToWaitOn ] = taskNOT_WAITING_NOTIFICATION;
        }
        EXIT_CRITICAL();
        return ulReturn;
    }
#endif /* configUSE_TASK_NOTIFICATIONS */

#if ( configUSE_TASK_NOTIFICATIONS == 1 )
    BaseType_t xTaskGenericNotifyWait( UBaseType_t uxIndexToWaitOn,
                                       uint32_t ulBitsToClearOnEntry,
                                       uint32_t ulBitsToClearOnExit,
                                       uint32_t * pulNotificationValue,
                                       TickType_t TicksToWait )
    {
        BaseType_t xReturn, xAlreadyYielded, xShouldBlock = false;
        configASSERT( uxIndexToWaitOn < configTASK_NOTIFICATION_ARRAY_ENTRIES );
        /* We suspend the scheduler here as AddCurrentTaskToDelayedList is a
         * non-deterministic operation. */
        vTaskSuspendAll();
        {
            /* We MUST enter a critical section to atomically check and update the
             * task notification value. If we do not do so, a notification from
             * an ISR will get lost. */
            ENTER_CRITICAL();
            {
                /* Only block if a notification is not already pending. */
                if( CurrentTCB->ucNotifyState[ uxIndexToWaitOn ] != taskNOTIFICATION_RECEIVED )
                {
                    /* Clear bits in the task's notification value as bits may get
                     * set by the notifying task or interrupt. This can be used
                     * to clear the value to zero. */
                    CurrentTCB->ulNotifiedValue[ uxIndexToWaitOn ] &= ~ulBitsToClearOnEntry;
                    /* Mark this task as waiting for a notification. */
                    CurrentTCB->ucNotifyState[ uxIndexToWaitOn ] = taskWAITING_NOTIFICATION;
                    if( TicksToWait > (TickType_t) 0 )
                    {
                        xShouldBlock = true;
                    }
                }
            }
            EXIT_CRITICAL();
            /* We are now out of the critical section but the scheduler is still
             * suspended, so we are safe to do non-deterministic operations such
             * as AddCurrentTaskToDelayedList. */
            if( xShouldBlock  )
            {                AddCurrentTaskToDelayedList( TicksToWait, true );
            }
            
        }
        xAlreadyYielded = TaskResumeAll();
        /* Force a reschedule if TaskResumeAll has not already done so. */
        if( ( xShouldBlock  ) && ( xAlreadyYielded == false ) )
        {
            taskYIELD_WITHIN_API();
        }

        ENTER_CRITICAL();
        {
            if( pulNotificationValue != NULL )
            {
                /* Output the current notification value, which may or may not
                 * have changed. */
                *pulNotificationValue = CurrentTCB->ulNotifiedValue[ uxIndexToWaitOn ];
            }
            /* If ucNotifyValue is set then either the task never entered the
             * blocked state (because a notification was already pending) or the
             * task unblocked because of a notification.  Otherwise the task
             * unblocked because of a timeout. */
            if( CurrentTCB->ucNotifyState[ uxIndexToWaitOn ] != taskNOTIFICATION_RECEIVED )
            {
                /* A notification was not received. */
                xReturn = false;
            }
            else
            {
                /* A notification was already pending or a notification was
                 * received while the task was waiting. */
                CurrentTCB->ulNotifiedValue[ uxIndexToWaitOn ] &= ~ulBitsToClearOnExit;
                xReturn = true;
            }
            CurrentTCB->ucNotifyState[ uxIndexToWaitOn ] = taskNOT_WAITING_NOTIFICATION;
        }
        EXIT_CRITICAL();
        return xReturn;
    }
#endif /* configUSE_TASK_NOTIFICATIONS */

#if ( configUSE_TASK_NOTIFICATIONS == 1 )
    BaseType_t xTaskGenericNotify( TaskHandle_t xTaskToNotify,
                                   UBaseType_t uxIndexToNotify,
                                   uint32_t ulValue,
                                   eNotifyAction eAction,
                                   uint32_t * pulPreviousNotificationValue )
    {
        TCB_t * pxTCB;
        BaseType_t xReturn = true;
        uint8_t ucOriginalNotifyState;
        configASSERT( uxIndexToNotify < configTASK_NOTIFICATION_ARRAY_ENTRIES );
        configASSERT( xTaskToNotify );
        pxTCB = xTaskToNotify;
        ENTER_CRITICAL();
        {
            if( pulPreviousNotificationValue != NULL )
            {
                *pulPreviousNotificationValue = pxTCB->ulNotifiedValue[ uxIndexToNotify ];
            }
            ucOriginalNotifyState = pxTCB->ucNotifyState[ uxIndexToNotify ];
            pxTCB->ucNotifyState[ uxIndexToNotify ] = taskNOTIFICATION_RECEIVED;
            switch( eAction )
            {
                case eSetBits:
                    pxTCB->ulNotifiedValue[ uxIndexToNotify ] |= ulValue;
                    break;
                case eIncrement:
                    ( pxTCB->ulNotifiedValue[ uxIndexToNotify ] )++;
                    break;
                case eSetValueWithOverwrite:
                    pxTCB->ulNotifiedValue[ uxIndexToNotify ] = ulValue;
                    break;
                case eSetValueWithoutOverwrite:
                    if( ucOriginalNotifyState != taskNOTIFICATION_RECEIVED )
                    {
                        pxTCB->ulNotifiedValue[ uxIndexToNotify ] = ulValue;
                    }
                    else
                    {
                        xReturn = false;
                    }
                    break;
                case eNoAction:
                    break;
                default:
                    configASSERT( TickCount == (TickType_t) 0 );
                    break;
            }
            if( ucOriginalNotifyState == taskWAITING_NOTIFICATION )
            {
                pxTCB->xStateListItem.remove();
                AddTaskToReadyList( pxTCB );
                configASSERT(pxTCB->xEventListItem.Container == NULL);
                taskYIELD_ANY_CORE_IF_USING_PREEMPTION( pxTCB );
            }
            
        }
        EXIT_CRITICAL();
        return xReturn;
    }
#endif /* configUSE_TASK_NOTIFICATIONS */

#if ( configUSE_TASK_NOTIFICATIONS == 1 )
    BaseType_t xTaskGenericNotifyFromISR( TaskHandle_t xTaskToNotify,
                                          UBaseType_t uxIndexToNotify,
                                          uint32_t ulValue,
                                          eNotifyAction eAction,
                                          uint32_t * pulPreviousNotificationValue,
                                          BaseType_t * pxHigherPriorityTaskWoken )
    {
        TCB_t * pxTCB;
        uint8_t ucOriginalNotifyState;
        BaseType_t xReturn = true;
        UBaseType_t uxSavedInterruptStatus;
        configASSERT( xTaskToNotify );
        configASSERT( uxIndexToNotify < configTASK_NOTIFICATION_ARRAY_ENTRIES );
        portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
        pxTCB = xTaskToNotify;
        uxSavedInterruptStatus = ( UBaseType_t ) ENTER_CRITICAL_FROM_ISR();
        {
            if( pulPreviousNotificationValue != NULL )
            {
                *pulPreviousNotificationValue = pxTCB->ulNotifiedValue[ uxIndexToNotify ];
            }
            ucOriginalNotifyState = pxTCB->ucNotifyState[ uxIndexToNotify ];
            pxTCB->ucNotifyState[ uxIndexToNotify ] = taskNOTIFICATION_RECEIVED;
            switch( eAction )
            {
                case eSetBits:
                    pxTCB->ulNotifiedValue[ uxIndexToNotify ] |= ulValue;
                    break;
                case eIncrement:
                    ( pxTCB->ulNotifiedValue[ uxIndexToNotify ] )++;
                    break;
                case eSetValueWithOverwrite:
                    pxTCB->ulNotifiedValue[ uxIndexToNotify ] = ulValue;
                    break;
                case eSetValueWithoutOverwrite:
                    if( ucOriginalNotifyState != taskNOTIFICATION_RECEIVED )
                    {
                        pxTCB->ulNotifiedValue[ uxIndexToNotify ] = ulValue;
                    }
                    else
                    {
                        /* The value could not be written to the task. */
                        xReturn = false;
                    }
                    break;
                case eNoAction:
                    /* The task is being notified without its notify value being
                     * updated. */
                    break;
                default:
                    /* Should not get here if all enums are handled.
                     * Artificially force an assert by testing a value the
                     * compiler can't assume is const. */
                    configASSERT( TickCount == (TickType_t) 0 );
                    break;
            }
            /* If the task is in the blocked state specifically to wait for a
             * notification then unblock it now. */
            if( ucOriginalNotifyState == taskWAITING_NOTIFICATION )
            {
                /* The task should not have been on an event list. */
                configASSERT( pxTCB->xEventListItem.Container == NULL );
                if( SchedulerSuspended == ( UBaseType_t ) 0U )
                {
                    pxTCB->xStateListItem.remove();
                    AddTaskToReadyList( pxTCB );
                }
                else
                {
                    PendingReadyList.append(&pxTCB->xEventListItem);
                }
                if( pxTCB->Priority > CurrentTCB->Priority )
                {
                    /* The notified task has a priority above the currently
                        * executing task so a yield is required. */
                    if( pxHigherPriorityTaskWoken != NULL )
                    {
                        *pxHigherPriorityTaskWoken = true;
                    }
                    /* Mark that a yield is pending in case the user is not
                        * using the "xHigherPriorityTaskWoken" parameter to an ISR
                        * safe FreeRTOS function. */
                    YieldPendings[ 0 ] = true;
                }
            }
        }
        EXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );
        return xReturn;
    }
#endif /* configUSE_TASK_NOTIFICATIONS */

void vTaskGenericNotifyGiveFromISR( TaskHandle_t xTaskToNotify,
                                    UBaseType_t uxIndexToNotify,
                                    BaseType_t * pxHigherPriorityTaskWoken )
{
    TCB_t * pxTCB;
    uint8_t ucOriginalNotifyState;
    UBaseType_t uxSavedInterruptStatus;
    configASSERT( xTaskToNotify );
    configASSERT( uxIndexToNotify < configTASK_NOTIFICATION_ARRAY_ENTRIES );
    portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
    pxTCB = xTaskToNotify;
    uxSavedInterruptStatus = ( UBaseType_t ) ENTER_CRITICAL_FROM_ISR();
    {
        ucOriginalNotifyState = pxTCB->ucNotifyState[ uxIndexToNotify ];
        pxTCB->ucNotifyState[ uxIndexToNotify ] = taskNOTIFICATION_RECEIVED;
        ( pxTCB->ulNotifiedValue[ uxIndexToNotify ] )++;
        if( ucOriginalNotifyState == taskWAITING_NOTIFICATION )
        {
            configASSERT(pxTCB->xEventListItem.Container == NULL );
            if( SchedulerSuspended == ( UBaseType_t ) 0U )
            {
                pxTCB->xStateListItem.remove();
                AddTaskToReadyList( pxTCB );
            }
            else
            {
                PendingReadyList.append(&pxTCB->xEventListItem);
            }
            if( pxTCB->Priority > CurrentTCB->Priority )
            {
                if( pxHigherPriorityTaskWoken != NULL )
                {
                    *pxHigherPriorityTaskWoken = true;
                }
                YieldPendings[ 0 ] = true;
            }
        }
    }
    EXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );
}

BaseType_t xTaskGenericNotifyStateClear( TaskHandle_t xTask,
                                            UBaseType_t uxIndexToClear )
{
    TCB_t * pxTCB;
    BaseType_t xReturn;
    configASSERT( uxIndexToClear < configTASK_NOTIFICATION_ARRAY_ENTRIES );
    /* If null is passed in here then it is the calling task that is having
        * its notification state cleared. */
    pxTCB = GetTCBFromHandle( xTask );
    ENTER_CRITICAL();
    {
        if( pxTCB->ucNotifyState[ uxIndexToClear ] == taskNOTIFICATION_RECEIVED )
        {
            pxTCB->ucNotifyState[ uxIndexToClear ] = taskNOT_WAITING_NOTIFICATION;
            xReturn = true;
        }
        else
        {
            xReturn = false;
        }
    }
    EXIT_CRITICAL();
    return xReturn;
}

uint32_t ulTaskGenericNotifyValueClear( TaskHandle_t xTask,
                                        UBaseType_t uxIndexToClear,
                                        uint32_t ulBitsToClear )
{
    configASSERT( uxIndexToClear < configTASK_NOTIFICATION_ARRAY_ENTRIES );
    /* If null is passed in here then it is the calling task that is having
        * its notification state cleared. */
    TCB_t *pxTCB = GetTCBFromHandle( xTask );
    ENTER_CRITICAL();
    /* Return the notification as it was before the bits were cleared,
        * then clear the bit mask. */
    uint32_t ulReturn = pxTCB->ulNotifiedValue[ uxIndexToClear ];
    pxTCB->ulNotifiedValue[ uxIndexToClear ] &= ~ulBitsToClear;
    EXIT_CRITICAL();
    return ulReturn;
}

static void AddCurrentTaskToDelayedList( TickType_t TicksToWait,
                                            const BaseType_t CanBlockIndefinitely )
{
    TickType_t TimeToWake;
    const TickType_t ConstTickCount = TickCount;
    auto *pxDelayedList = DelayedTaskList;
    auto *pxOverflowDelayedList = OverflowDelayedTaskList;
    CurrentTCB->ucDelayAborted = ( uint8_t ) false;
    if(CurrentTCB->xStateListItem.remove() == ( UBaseType_t ) 0 )
    {
        portRESET_READY_PRIORITY( CurrentTCB->Priority, TopReadyPriority );
    }
    if( ( TicksToWait == portMAX_DELAY ) && ( CanBlockIndefinitely != false ) )
    {
        SuspendedTaskList.append(&CurrentTCB->xStateListItem);
    }
    else
    {
        TimeToWake = ConstTickCount + TicksToWait;
        CurrentTCB->xStateListItem.Value = TimeToWake;
        if( TimeToWake < ConstTickCount )
        {
            pxOverflowDelayedList->insert(&( CurrentTCB->xStateListItem ) );
        }
        else
        {
            pxDelayedList->insert(&( CurrentTCB->xStateListItem ) );
            /* If the task entering the blocked state was placed at the
                * head of the list of blocked tasks then NextTaskUnblockTime
                * needs to be updated too. */
            if( TimeToWake < NextTaskUnblockTime )
            {
                NextTaskUnblockTime = TimeToWake;
            }
        }
    }
}

/*
 * This is the kernel provided implementation of ApplicationGetIdleTaskMemory()
 * to provide the memory that is used by the Idle task. It is used when
 * configKERNEL_PROVIDED_STATIC_MEMORY is set to 1. The application can provide
 * it's own implementation of ApplicationGetIdleTaskMemory by setting
 * configKERNEL_PROVIDED_STATIC_MEMORY to 0 or leaving it undefined.
 */
void ApplicationGetIdleTaskMemory( StaticTask_t ** IdleTaskTCBBuffer,
                                    StackType_t ** IdleTaskStackBuffer,
                                    configSTACK_DEPTH_TYPE * IdleTaskStackSize )
{
    static StaticTask_t IdleTaskTCB;
    static StackType_t IdleTaskStack[ configMINIMAL_STACK_SIZE ];
    *IdleTaskTCBBuffer = &( IdleTaskTCB );
    *IdleTaskStackBuffer = &( IdleTaskStack[ 0 ] );
    *IdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/*
 * This is the kernel provided implementation of vApplicationGetTimerTaskMemory()
 * to provide the memory that is used by the Timer service task. It is used when
 * configKERNEL_PROVIDED_STATIC_MEMORY is set to 1. The application can provide
 * it's own implementation of vApplicationGetTimerTaskMemory by setting
 * configKERNEL_PROVIDED_STATIC_MEMORY to 0 or leaving it undefined.
 */
void vApplicationGetTimerTaskMemory( StaticTask_t ** TimerTaskTCBBuffer,
                                        StackType_t ** TimerTaskStackBuffer,
                                        configSTACK_DEPTH_TYPE * TimerTaskStackSize )
{
    static StaticTask_t TimerTaskTCB;
    static StackType_t uTimerTaskStack[ configTIMER_TASK_STACK_DEPTH ];
    *TimerTaskTCBBuffer = &( TimerTaskTCB );
    *TimerTaskStackBuffer = &( uTimerTaskStack[ 0 ] );
    *TimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}

/*
 * Reset the state in this file. This state is normally initialized at start up.
 * This function must be called by the application before restarting the
 * scheduler.
 */
void vTaskResetState( void )
{
    BaseType_t xCoreID;
    /* Task control block. */
    CurrentTCB = NULL;
    uxDeletedTasksWaitingCleanUp = ( UBaseType_t ) 0U;
    /* Other file private variables. */
    CurrentNumberOfTasks = ( UBaseType_t ) 0U;
    TickCount = (TickType_t) configINITIAL_TICK_COUNT;
    TopReadyPriority = tskIDLE_PRIORITY;
    SchedulerRunning = false;
    PendedTicks = (TickType_t) 0U;
    for( xCoreID = 0; xCoreID < configNUMBER_OF_CORES; xCoreID++ )
    {
        YieldPendings[ xCoreID ] = false;
    }
    NumOfOverflows = ( BaseType_t ) 0;
    TaskNumber = ( UBaseType_t ) 0U;
    NextTaskUnblockTime = (TickType_t) 0U;
    SchedulerSuspended = ( UBaseType_t ) 0U;
}
