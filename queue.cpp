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
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.hpp"
#include "queue.h"

/* Constants used with the cRxLock and cTxLock structure members. */
#define queueUNLOCKED             ( ( int8_t ) -1 )
#define queueLOCKED_UNMODIFIED    ( ( int8_t ) 0 )
#define queueINT8_MAX             ( ( int8_t ) 127 )
/* When the Queue_t structure is used to represent a base queue its pcHead and
 * pcTail members are used as pointers into the queue storage area.  When the
 * Queue_t structure is used to represent a mutex pcHead and pcTail pointers are
 * not necessary, and the pcHead pointer is set to NULL to indicate that the
 * structure instead holds a pointer to the mutex holder (if any).  Map alternative
 * names to the pcHead and structure member to ensure the readability of the code
 * is maintained.  The QueuePointers_t and SemaphoreData_t types are used to form
 * a union as their usage is mutually exclusive dependent on what the queue is
 * being used for. */
#define uxQueueType               pcHead
#define queueQUEUE_IS_MUTEX       NULL
typedef struct QueuePointers
{
    int8_t * pcTail;     /**< Points to the byte at the end of the queue storage area.  Once more byte is allocated than necessary to store the queue items, this is used as a marker. */
    int8_t * pcReadFrom; /**< Points to the last place that a queued item was read from when the structure is used as a queue. */
} QueuePointers_t;
typedef struct SemaphoreData
{
    TaskHandle_t xMutexHolder;        /**< The handle of the task that holds the mutex. */
    UBaseType_t uxRecursiveCallCount; /**< Maintains a count of the number of times a recursive mutex has been recursively 'taken' when the structure is used as a mutex. */
} SemaphoreData_t;
/* Semaphores do not actually store or copy data, so have an item size of
 * zero. */
#define queueSEMAPHORE_QUEUE_ITEM_LENGTH    ( ( UBaseType_t ) 0 )
#define queueMUTEX_GIVE_BLOCK_TIME          ( ( TickType_t ) 0U )
#if ( configUSE_PREEMPTION == 0 )
/* If the cooperative scheduler is being used then a yield should not be
 * performed just because a higher priority task has been woken. */
    #define queueYIELD_IF_USING_PREEMPTION()
#else
    #if ( configNUMBER_OF_CORES == 1 )
        #define queueYIELD_IF_USING_PREEMPTION()    portYIELD_WITHIN_API()
    #else /* #if ( configNUMBER_OF_CORES == 1 ) */
        #define queueYIELD_IF_USING_PREEMPTION()    vTaskYieldWithinAPI()
    #endif /* #if ( configNUMBER_OF_CORES == 1 ) */
#endif
/*
 * Definition of the queue used by the scheduler.
 * Items are queued by copy, not reference.  See the following link for the
 * rationale: https://www.FreeRTOS.org/Embedded-RTOS-Queues.html
 */
typedef struct QueueDefinition /* The old naming convention is used to prevent breaking kernel aware debuggers. */
{
    int8_t * pcHead;           /**< Points to the beginning of the queue storage area. */
    int8_t * pcWriteTo;        /**< Points to the free next place in the storage area. */
    union
    {
        QueuePointers_t xQueue;     /**< Data required exclusively when this structure is used as a queue. */
        SemaphoreData_t xSemaphore; /**< Data required exclusively when this structure is used as a semaphore. */
    } u;
    List_t<TCB_t> xTasksWaitingToSend;             /**< List of tasks that are blocked waiting to post onto this queue.  Stored in priority order. */
    List_t<TCB_t> xTasksWaitingToReceive;          /**< List of tasks that are blocked waiting to read from this queue.  Stored in priority order. */
    volatile UBaseType_t uxMessagesWaiting; /**< The number of items currently in the queue. */
    UBaseType_t uxLength;                   /**< The length of the queue defined as the number of items it will hold, not the number of bytes. */
    UBaseType_t uxItemSize;                 /**< The size of each items that the queue will hold. */
    volatile int8_t cRxLock;                /**< Stores the number of items received from the queue (removed from the queue) while the queue was locked.  Set to queueUNLOCKED when the queue is not locked. */
    volatile int8_t cTxLock;                /**< Stores the number of items transmitted to the queue (added to the queue) while the queue was locked.  Set to queueUNLOCKED when the queue is not locked. */
    uint8_t StaticallyAllocated; /**< Set to true if the memory used by the queue was statically allocated to ensure no attempt is made to free the memory. */
    struct QueueDefinition * pxQueueSetContainer;
} xQUEUE;
/* The old xQUEUE name is maintained above then typedefed to the new Queue_t
 * name below to enable the use of older kernel aware debuggers. */
typedef xQUEUE Queue_t;

/*
 * The queue registry is just a means for kernel aware debuggers to locate
 * queue structures.  It has no other purpose so is an optional component.
 */
#if ( configQUEUE_REGISTRY_SIZE > 0 )
/* The type stored within the queue registry array.  This allows a name
 * to be assigned to each queue making kernel aware debugging a little
 * more user friendly. */
    typedef struct QUEUE_REGISTRY_ITEM
    {
        const char * pcQueueName;
        QueueHandle_t xHandle;
    } xQueueRegistryItem;
/* The old xQueueRegistryItem name is maintained above then typedefed to the
 * new xQueueRegistryItem name below to enable the use of older kernel aware
 * debuggers. */
    typedef xQueueRegistryItem QueueRegistryItem_t;
/* The queue registry is simply an array of QueueRegistryItem_t structures.
 * The pcQueueName member of a structure being NULL is indicative of the
 * array position being vacant. */


     QueueRegistryItem_t xQueueRegistry[ configQUEUE_REGISTRY_SIZE ];
#endif /* configQUEUE_REGISTRY_SIZE */
/*
 * Unlocks a queue locked by a call to LockQueue.  Locking a queue does not
 * prevent an ISR from adding or removing items to the queue, but does prevent
 * an ISR from removing tasks from the queue event lists.  If an ISR finds a
 * queue is locked it will instead increment the appropriate queue lock count
 * to indicate that a task may require unblocking.  When the queue in unlocked
 * these lock counts are inspected, and the appropriate action taken.
 */
static void UnlockQueue( Queue_t * const pxQueue ) ;
/*
 * Uses a critical section to determine if there is any data in a queue.
 *
 * @return true if the queue contains no items, otherwise false.
 */
static BaseType_t IsQueueEmpty( const Queue_t * pxQueue ) ;
/*
 * Uses a critical section to determine if there is any space in a queue.
 *
 * @return true if there is no space, otherwise false;
 */
static BaseType_t IsQueueFull( const Queue_t * pxQueue ) ;
/*
 * Copies an item into the queue, either at the front of the queue or the
 * back of the queue.
 */
static BaseType_t CopyDataToQueue( Queue_t * const pxQueue,
                                      const void * pvItemToQueue,
                                      const BaseType_t xPosition ) ;
/*
 * Copies an item out of a queue.
 */
static void CopyDataFromQueue( Queue_t * const pxQueue,
                                  void * const pvBuffer ) ;
#if ( configUSE_QUEUE_SETS == 1 )
/*
 * Checks to see if a queue is a member of a queue set, and if so, notifies
 * the queue set that the queue contains data.
 */
    static BaseType_t NotifyQueueSetContainer( const Queue_t * const pxQueue ) ;
#endif
/*
 * Called after a Queue_t structure has been allocated either statically or
 * dynamically to fill in the structure's members.
 */
static void InitialiseNewQueue( const UBaseType_t uxQueueLength,
                                   const UBaseType_t uxItemSize,
                                   uint8_t * pucQueueStorage,
                                   const uint8_t ucQueueType,
                                   Queue_t * pxNewQueue ) ;
/*
 * Mutexes are a special type of queue.  When a mutex is created, first the
 * queue is created, then InitialiseMutex() is called to configure the queue
 * as a mutex.
 */
#if ( configUSE_MUTEXES == 1 )
    static void InitialiseMutex( Queue_t * pxNewQueue ) ;
#endif
#if ( configUSE_MUTEXES == 1 )
/*
 * If a task waiting for a mutex causes the mutex holder to inherit a
 * priority, but the waiting task times out, then the holder should
 * disinherit the priority - but only down to the highest priority of any
 * other tasks that are waiting for the same mutex.  This function returns
 * that priority.
 */
    static UBaseType_t GetHighestPriorityOfWaitToReceiveList(Queue_t * const pxQueue ) ;
#endif

/*
 * Macro to mark a queue as locked.  Locking a queue prevents an ISR from
 * accessing the queue event lists.
 */
#define LockQueue( pxQueue )                            \
    ENTER_CRITICAL();                                  \
    {                                                      \
        if( ( pxQueue )->cRxLock == queueUNLOCKED )        \
        {                                                  \
            ( pxQueue )->cRxLock = queueLOCKED_UNMODIFIED; \
        }                                                  \
        if( ( pxQueue )->cTxLock == queueUNLOCKED )        \
        {                                                  \
            ( pxQueue )->cTxLock = queueLOCKED_UNMODIFIED; \
        }                                                  \
    }                                                      \
    EXIT_CRITICAL()
/*
 * Macro to increment cTxLock member of the queue data structure. It is
 * capped at the number of tasks in the system as we cannot unblock more
 * tasks than the number of tasks in the system.
 */
#define IncrementQueueTxLock( pxQueue, cTxLock )                           \
    do {                                                                      \
        const UBaseType_t uxNumberOfTasks = uxTaskGetNumberOfTasks();         \
        if( ( UBaseType_t ) ( cTxLock ) < uxNumberOfTasks )                   \
        {                                                                     \
            configASSERT( ( cTxLock ) != queueINT8_MAX );                     \
            ( pxQueue )->cTxLock = ( int8_t ) ( ( cTxLock ) + ( int8_t ) 1 ); \
        }                                                                     \
    } while( 0 )
/*
 * Macro to increment cRxLock member of the queue data structure. It is
 * capped at the number of tasks in the system as we cannot unblock more
 * tasks than the number of tasks in the system.
 */
#define IncrementQueueRxLock( pxQueue, cRxLock )                           \
    do {                                                                      \
        const UBaseType_t uxNumberOfTasks = uxTaskGetNumberOfTasks();         \
        if( ( UBaseType_t ) ( cRxLock ) < uxNumberOfTasks )                   \
        {                                                                     \
            configASSERT( ( cRxLock ) != queueINT8_MAX );                     \
            ( pxQueue )->cRxLock = ( int8_t ) ( ( cRxLock ) + ( int8_t ) 1 ); \
        }                                                                     \
    } while( 0 )

BaseType_t xQueueGenericReset( QueueHandle_t xQueue,
                               BaseType_t xNewQueue )
{
    BaseType_t xReturn = true;
    Queue_t * const pxQueue = xQueue;
    configASSERT( pxQueue );
    if( ( pxQueue != NULL ) &&
        ( pxQueue->uxLength >= 1U ) &&
        /* Check for multiplication overflow. */
        ( ( SIZE_MAX / pxQueue->uxLength ) >= pxQueue->uxItemSize ) )
    {
        ENTER_CRITICAL();
        {
            pxQueue->u.xQueue.pcTail = pxQueue->pcHead + ( pxQueue->uxLength * pxQueue->uxItemSize );
            pxQueue->uxMessagesWaiting = ( UBaseType_t ) 0U;
            pxQueue->pcWriteTo = pxQueue->pcHead;
            pxQueue->u.xQueue.pcReadFrom = pxQueue->pcHead + ( ( pxQueue->uxLength - 1U ) * pxQueue->uxItemSize );
            pxQueue->cRxLock = queueUNLOCKED;
            pxQueue->cTxLock = queueUNLOCKED;
            if( xNewQueue == false )
            {
                if(pxQueue->xTasksWaitingToSend.Length > 0)
                {
                    if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToSend ) ) != false )
                    {
                        queueYIELD_IF_USING_PREEMPTION();
                    }
                }
            }
            else
            {
                /* Ensure the event queues start in the correct state. */
                pxQueue->xTasksWaitingToSend.init();
                pxQueue->xTasksWaitingToReceive.init();
            }
        }
        EXIT_CRITICAL();
    }
    else
    {
        xReturn = false;
    }
    configASSERT( xReturn != false );
    return xReturn;
}

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
    QueueHandle_t xQueueGenericCreateStatic( const UBaseType_t uxQueueLength,
                                             const UBaseType_t uxItemSize,
                                             uint8_t * pucQueueStorage,
                                             StaticQueue_t * pStaticQueue,
                                             const uint8_t ucQueueType )
    {
        Queue_t * pxNewQueue = NULL;
        /* The StaticQueue_t structure and the queue storage area must be
         * supplied. */
        configASSERT( pStaticQueue );
        if( ( uxQueueLength > ( UBaseType_t ) 0 ) &&
            ( pStaticQueue != NULL ) &&
            /* A queue storage area should be provided if the item size is not 0, and
             * should not be provided if the item size is 0. */
            ( !( ( pucQueueStorage != NULL ) && ( uxItemSize == 0U ) ) ) &&
            ( !( ( pucQueueStorage == NULL ) && ( uxItemSize != 0U ) ) ) )
        {
            #if ( configASSERT_DEFINED == 1 )
            {
                /* Sanity check that the size of the structure used to declare a
                 * variable of type StaticQueue_t or StaticSemaphore_t equals the size of
                 * the real queue and semaphore structures. */
                volatile size_t xSize = sizeof( StaticQueue_t );
                /* This assertion cannot be branch covered in unit tests */
                configASSERT( xSize == sizeof( Queue_t ) ); /* LCOV_EXCL_BR_LINE */
                ( void ) xSize;                             /* Prevent unused variable warning when configASSERT() is not defined. */
            }
            #endif /* configASSERT_DEFINED */
            /* The address of a statically allocated queue was passed in, use it.
             * The address of a statically allocated storage area was also passed in
             * but is already set. */
            pxNewQueue = ( Queue_t * ) pStaticQueue;
            #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
            {
                /* Queues can be allocated wither statically or dynamically, so
                 * note this queue was allocated statically in case the queue is
                 * later deleted. */
                pxNewQueue->StaticallyAllocated = true;
            }
            #endif /* configSUPPORT_DYNAMIC_ALLOCATION */
            InitialiseNewQueue( uxQueueLength, uxItemSize, pucQueueStorage, ucQueueType, pxNewQueue );
        }
        else
        {
            configASSERT( pxNewQueue );
        }
        return pxNewQueue;
    }
#endif /* configSUPPORT_STATIC_ALLOCATION */

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
    BaseType_t xQueueGenericGetStaticBuffers( QueueHandle_t xQueue,
                                              uint8_t ** ppucQueueStorage,
                                              StaticQueue_t ** ppStaticQueue )
    {
        BaseType_t xReturn;
        Queue_t * const pxQueue = xQueue;
        configASSERT( pxQueue );
        configASSERT( ppStaticQueue );
        #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
        {
            /* Check if the queue was statically allocated. */
            if( pxQueue->StaticallyAllocated == ( uint8_t ) true )
            {
                if( ppucQueueStorage != NULL )
                {
                    *ppucQueueStorage = ( uint8_t * ) pxQueue->pcHead;
                }


                *ppStaticQueue = ( StaticQueue_t * ) pxQueue;
                xReturn = true;
            }
            else
            {
                xReturn = false;
            }
        }
        #else /* configSUPPORT_DYNAMIC_ALLOCATION */
        {
            /* Queue must have been statically allocated. */
            if( ppucQueueStorage != NULL )
            {
                *ppucQueueStorage = ( uint8_t * ) pxQueue->pcHead;
            }
            *ppStaticQueue = ( StaticQueue_t * ) pxQueue;
            xReturn = true;
        }
        #endif /* configSUPPORT_DYNAMIC_ALLOCATION */
        return xReturn;
    }
#endif /* configSUPPORT_STATIC_ALLOCATION */

#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
    QueueHandle_t xQueueGenericCreate( const UBaseType_t uxQueueLength,
                                       const UBaseType_t uxItemSize,
                                       const uint8_t ucQueueType )
    {
        Queue_t * pxNewQueue = NULL;
        size_t xQueueSizeInBytes;
        uint8_t * pucQueueStorage;
        if( ( uxQueueLength > ( UBaseType_t ) 0 ) &&
            /* Check for multiplication overflow. */
            ( ( SIZE_MAX / uxQueueLength ) >= uxItemSize ) &&
            /* Check for addition overflow. */
            ( ( UBaseType_t ) ( SIZE_MAX - sizeof( Queue_t ) ) >= ( uxQueueLength * uxItemSize ) ) )
        {
            /* Allocate enough space to hold the maximum number of items that
             * can be in the queue at any time.  It is valid for uxItemSize to be
             * zero in the case the queue is used as a semaphore. */
            xQueueSizeInBytes = ( size_t ) ( ( size_t ) uxQueueLength * ( size_t ) uxItemSize );
            pxNewQueue = ( Queue_t * ) pvPortMalloc( sizeof( Queue_t ) + xQueueSizeInBytes );
            if( pxNewQueue != NULL )
            {
                /* Jump past the queue structure to find the location of the queue
                 * storage area. */
                pucQueueStorage = ( uint8_t * ) pxNewQueue;
                pucQueueStorage += sizeof( Queue_t );
                #if ( configSUPPORT_STATIC_ALLOCATION == 1 )
                {
                    /* Queues can be created either statically or dynamically, so
                     * note this task was created dynamically in case it is later
                     * deleted. */
                    pxNewQueue->StaticallyAllocated = false;
                }
                #endif /* configSUPPORT_STATIC_ALLOCATION */
                InitialiseNewQueue( uxQueueLength, uxItemSize, pucQueueStorage, ucQueueType, pxNewQueue );
            }
        }
        else
        {
            configASSERT( pxNewQueue );
        }
        return pxNewQueue;
    }
#endif /* configSUPPORT_STATIC_ALLOCATION */

static void InitialiseNewQueue( const UBaseType_t uxQueueLength,
                                   const UBaseType_t uxItemSize,
                                   uint8_t * pucQueueStorage,
                                   const uint8_t ucQueueType,
                                   Queue_t * pxNewQueue )
{
    /* Remove compiler warnings about unused parameters should
     * configUSE_TRACE_FACILITY not be set to 1. */
    ( void ) ucQueueType;
    if( uxItemSize == ( UBaseType_t ) 0 )
    {
        /* No RAM was allocated for the queue storage area, but PC head cannot
         * be set to NULL because NULL is used as a key to say the queue is used as
         * a mutex.  Therefore just set pcHead to point to the queue as a benign
         * value that is known to be within the memory map. */
        pxNewQueue->pcHead = ( int8_t * ) pxNewQueue;
    }
    else
    {
        /* Set the head to the start of the queue storage area. */
        pxNewQueue->pcHead = ( int8_t * ) pucQueueStorage;
    }
    /* Initialise the queue members as described where the queue type is
     * defined. */
    pxNewQueue->uxLength = uxQueueLength;
    pxNewQueue->uxItemSize = uxItemSize;
    ( void ) xQueueGenericReset( pxNewQueue, true );
    #if ( configUSE_QUEUE_SETS == 1 )
    {
        pxNewQueue->pxQueueSetContainer = NULL;
    }
    #endif /* configUSE_QUEUE_SETS */
}

#if ( configUSE_MUTEXES == 1 )
    static void InitialiseMutex( Queue_t * pxNewQueue )
    {
        if( pxNewQueue != NULL )
        {
            /* The queue create function will set all the queue structure members
            * correctly for a generic queue, but this function is creating a
            * mutex.  Overwrite those members that need to be set differently -
            * in particular the information required for priority inheritance. */
            pxNewQueue->u.xSemaphore.xMutexHolder = NULL;
            pxNewQueue->uxQueueType = queueQUEUE_IS_MUTEX;
            /* In case this is a recursive mutex. */
            pxNewQueue->u.xSemaphore.uxRecursiveCallCount = 0;
            /* Start with the semaphore in the expected state. */
            ( void ) xQueueGenericSend( pxNewQueue, NULL, ( TickType_t ) 0U, queueSEND_TO_BACK );
        }
    }
#endif /* configUSE_MUTEXES */

#if ( ( configUSE_MUTEXES == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
    QueueHandle_t xQueueCreateMutex( const uint8_t ucQueueType )
    {
        QueueHandle_t xNewQueue;
        const UBaseType_t uxMutexLength = ( UBaseType_t ) 1, uxMutexSize = ( UBaseType_t ) 0;
        xNewQueue = xQueueGenericCreate( uxMutexLength, uxMutexSize, ucQueueType );
        InitialiseMutex( ( Queue_t * ) xNewQueue );
        return xNewQueue;
    }
#endif /* configUSE_MUTEXES */

#if ( ( configUSE_MUTEXES == 1 ) && ( configSUPPORT_STATIC_ALLOCATION == 1 ) )
    QueueHandle_t xQueueCreateMuteStatic( const uint8_t ucQueueType,
                                           StaticQueue_t * pStaticQueue )
    {
        QueueHandle_t xNewQueue;
        const UBaseType_t uxMutexLength = ( UBaseType_t ) 1, uxMutexSize = ( UBaseType_t ) 0;
        /* Prevent compiler warnings about unused parameters if
         * configUSE_TRACE_FACILITY does not equal 1. */
        ( void ) ucQueueType;
        xNewQueue = xQueueGenericCreateStatic( uxMutexLength, uxMutexSize, NULL, pStaticQueue, ucQueueType );
        InitialiseMutex( ( Queue_t * ) xNewQueue );
        return xNewQueue;
    }
#endif /* configUSE_MUTEXES */

#if ( ( configUSE_MUTEXES == 1 ) && ( INCLUDE_xSemaphoreGetMutexHolder == 1 ) )
    TaskHandle_t xQueueGetMutexHolder( QueueHandle_t xSemaphore )
    {
        TaskHandle_t pxReturn;
        Queue_t * const pxSemaphore = ( Queue_t * ) xSemaphore;
        configASSERT( xSemaphore );
        /* This function is called by xSemaphoreGetMutexHolder(), and should not
         * be called directly.  Note:  This is a good way of determining if the
         * calling task is the mutex holder, but not a good way of determining the
         * identity of the mutex holder, as the holder may change between the
         * following critical section exiting and the function returning. */
        ENTER_CRITICAL();
        {
            if( pxSemaphore->uxQueueType == queueQUEUE_IS_MUTEX )
            {
                pxReturn = pxSemaphore->u.xSemaphore.xMutexHolder;
            }
            else
            {
                pxReturn = NULL;
            }
        }
        EXIT_CRITICAL();
        return pxReturn;
    }
#endif /* if ( ( configUSE_MUTEXES == 1 ) && ( INCLUDE_xSemaphoreGetMutexHolder == 1 ) ) */

#if ( ( configUSE_MUTEXES == 1 ) && ( INCLUDE_xSemaphoreGetMutexHolder == 1 ) )
    TaskHandle_t xQueueGetMutexHolderFromISR( QueueHandle_t xSemaphore )
    {
        TaskHandle_t pxReturn;
        configASSERT( xSemaphore );
        /* Mutexes cannot be used in interrupt service routines, so the mutex
         * holder should not change in an ISR, and therefore a critical section is
         * not required here. */
        if( ( ( Queue_t * ) xSemaphore )->uxQueueType == queueQUEUE_IS_MUTEX )
        {
            pxReturn = ( ( Queue_t * ) xSemaphore )->u.xSemaphore.xMutexHolder;
        }
        else
        {
            pxReturn = NULL;
        }
        return pxReturn;
    }
#endif /* if ( ( configUSE_MUTEXES == 1 ) && ( INCLUDE_xSemaphoreGetMutexHolder == 1 ) ) */

#if ( configUSE_RECURSIVE_MUTEXES == 1 )
    BaseType_t xQueueGiveMutexRecursive( QueueHandle_t xMutex )
    {
        BaseType_t xReturn;
        Queue_t * const pxMutex = ( Queue_t * ) xMutex;
        configASSERT( pxMutex );
        /* If this is the task that holds the mutex then xMutexHolder will not
         * change outside of this task.  If this task does not hold the mutex then
         * pxMutexHolder can never coincidentally equal the tasks handle, and as
         * this is the only condition we are interested in it does not matter if
         * pxMutexHolder is accessed simultaneously by another task.  Therefore no
         * mutual exclusion is required to test the pxMutexHolder variable. */
        if( pxMutex->u.xSemaphore.xMutexHolder == xTaskGetCurrentTaskHandle() )
        {
            /* uxRecursiveCallCount cannot be zero if xMutexHolder is equal to
             * the task handle, therefore no underflow check is required.  Also,
             * uxRecursiveCallCount is only modified by the mutex holder, and as
             * there can only be one, no mutual exclusion is required to modify the
             * uxRecursiveCallCount member. */
            ( pxMutex->u.xSemaphore.uxRecursiveCallCount )--;
            /* Has the recursive call count unwound to 0? */
            if( pxMutex->u.xSemaphore.uxRecursiveCallCount == ( UBaseType_t ) 0 )
            {
                /* Return the mutex.  This will automatically unblock any other
                 * task that might be waiting to access the mutex. */
                ( void ) xQueueGenericSend( pxMutex, NULL, queueMUTEX_GIVE_BLOCK_TIME, queueSEND_TO_BACK );
            }
            xReturn = true;
        }
        else
        {
            xReturn = false;
        }
        return xReturn;
    }
#endif /* configUSE_RECURSIVE_MUTEXES */

#if ( configUSE_RECURSIVE_MUTEXES == 1 )
    BaseType_t xQueueTakeMutexRecursive( QueueHandle_t xMutex,
                                         TickType_t xTicksToWait )
    {
        BaseType_t xReturn;
        Queue_t * const pxMutex = ( Queue_t * ) xMutex;
        configASSERT( pxMutex );
        if( pxMutex->u.xSemaphore.xMutexHolder == xTaskGetCurrentTaskHandle() )
        {
            ( pxMutex->u.xSemaphore.uxRecursiveCallCount )++;
            xReturn = true;
        }
        else
        {
            xReturn = xQueueSemaphoreTake( pxMutex, xTicksToWait );
            /* true will only be returned if the mutex was successfully
             * obtained.  The calling task may have entered the Blocked state
             * before reaching here. */
            if( xReturn != false )
            {
                ( pxMutex->u.xSemaphore.uxRecursiveCallCount )++;
            }
        }
        return xReturn;
    }
#endif /* configUSE_RECURSIVE_MUTEXES */

#if ( ( configUSE_COUNTING_SEMAPHORES == 1 ) && ( configSUPPORT_STATIC_ALLOCATION == 1 ) )
    QueueHandle_t xQueueCreateCountingSemaphoreStatic( const UBaseType_t uxMaxCount,
                                                       const UBaseType_t uxInitialCount,
                                                       StaticQueue_t * pStaticQueue )
    {
        QueueHandle_t xHandle = NULL;
        if( ( uxMaxCount != 0U ) &&
            ( uxInitialCount <= uxMaxCount ) )
        {
            xHandle = xQueueGenericCreateStatic( uxMaxCount, queueSEMAPHORE_QUEUE_ITEM_LENGTH, NULL, pStaticQueue, queueQUEUE_TYPE_COUNTING_SEMAPHORE );
            if( xHandle != NULL )
            {
                ( ( Queue_t * ) xHandle )->uxMessagesWaiting = uxInitialCount;
            }
        }
        else
        {
            configASSERT( xHandle );
        }
        return xHandle;
    }
#endif /* ( ( configUSE_COUNTING_SEMAPHORES == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) ) */

#if ( ( configUSE_COUNTING_SEMAPHORES == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
    QueueHandle_t xQueueCreateCountingSemaphore( const UBaseType_t uxMaxCount,
                                                 const UBaseType_t uxInitialCount )
    {
        QueueHandle_t xHandle = NULL;
        if( ( uxMaxCount != 0U ) &&
            ( uxInitialCount <= uxMaxCount ) )
        {
            xHandle = xQueueGenericCreate( uxMaxCount, queueSEMAPHORE_QUEUE_ITEM_LENGTH, queueQUEUE_TYPE_COUNTING_SEMAPHORE );
            if( xHandle != NULL )
            {
                ( ( Queue_t * ) xHandle )->uxMessagesWaiting = uxInitialCount;
            }
        }
        else
        {
            configASSERT( xHandle );
        }
        return xHandle;
    }
#endif /* ( ( configUSE_COUNTING_SEMAPHORES == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) ) */

BaseType_t xQueueGenericSend( QueueHandle_t xQueue,
                              const void * const pvItemToQueue,
                              TickType_t xTicksToWait,
                              const BaseType_t xCopyPosition )
{
    BaseType_t xEntryTimeSet = false, xYieldRequired;
    TimeOut_t xTimeOut;
    Queue_t * const pxQueue = xQueue;
    configASSERT( pxQueue );
    configASSERT( !( ( pvItemToQueue == NULL ) && ( pxQueue->uxItemSize != ( UBaseType_t ) 0U ) ) );
    configASSERT( !( ( xCopyPosition == queueOVERWRITE ) && ( pxQueue->uxLength != 1 ) ) );
    #if ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) )
    {
        configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) );
    }
    #endif
    for( ; ; )
    {
        ENTER_CRITICAL();
        {
            /* Is there room on the queue now?  The running task must be the
             * highest priority task wanting to access the queue.  If the head item
             * in the queue is to be overwritten then it does not matter if the
             * queue is full. */
            if( ( pxQueue->uxMessagesWaiting < pxQueue->uxLength ) || ( xCopyPosition == queueOVERWRITE ) )
            {
                const UBaseType_t uxPreviousMessagesWaiting = pxQueue->uxMessagesWaiting;
                xYieldRequired = CopyDataToQueue( pxQueue, pvItemToQueue, xCopyPosition );
                if( pxQueue->pxQueueSetContainer != NULL )
                {
                    if( ( xCopyPosition == queueOVERWRITE ) && ( uxPreviousMessagesWaiting != ( UBaseType_t ) 0 ) )
                    {
                        /* Do not notify the queue set as an existing item
                            * was overwritten in the queue so the number of items
                            * in the queue has not changed. */
                    }
                    else if( NotifyQueueSetContainer( pxQueue ) != false )
                    {
                        /* The queue is a member of a queue set, and posting
                            * to the queue set caused a higher priority task to
                            * unblock. A context switch is required. */
                        queueYIELD_IF_USING_PREEMPTION();
                    }
                }
                else
                {
                    if(pxQueue->xTasksWaitingToReceive.Length > 0)
                    {
                        if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != false )
                        {
                            queueYIELD_IF_USING_PREEMPTION();
                        }
                    }
                    else if( xYieldRequired != false )
                    {
                        queueYIELD_IF_USING_PREEMPTION();
                    }
                }
                EXIT_CRITICAL();
                return true;
            }
            else
            {
                if( xTicksToWait == ( TickType_t ) 0 )
                {
                    /* The queue was full and no block time is specified (or
                     * the block time has expired) so leave now. */
                    EXIT_CRITICAL();
                    return errQUEUE_FULL;
                }
                else if( xEntryTimeSet == false )
                {
                    /* The queue was full and a block time was specified so
                     * configure the timeout structure. */
                    vTaskInternalSetTimeOutState( &xTimeOut );
                    xEntryTimeSet = true;
                }
            }
        }
        EXIT_CRITICAL();
        /* Interrupts and other tasks can send to and receive from the queue
         * now the critical section has been exited. */
        vTaskSuspendAll();
        LockQueue( pxQueue );
        /* Update the timeout state to see if it has expired yet. */
        if( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == false )
        {
            if( IsQueueFull( pxQueue ) != false )
            {
                vTaskPlaceOnEventList( &( pxQueue->xTasksWaitingToSend ), xTicksToWait );
                /* Unlocking the queue means queue events can effect the
                 * event list. It is possible that interrupts occurring now
                 * remove this task from the event list again - but as the
                 * scheduler is suspended the task will go onto the pending
                 * ready list instead of the actual ready list. */
                UnlockQueue( pxQueue );
                /* Resuming the scheduler will move tasks from the pending
                 * ready list into the ready list - so it is feasible that this
                 * task is already in the ready list before it yields - in which
                 * case the yield will not cause a context switch unless there
                 * is also a higher priority task in the pending ready list. */
                if( TaskResumeAll() == false )
                {
                    taskYIELD_WITHIN_API();
                }
            }
            else
            {
                /* Try again. */
                UnlockQueue( pxQueue );
                ( void ) TaskResumeAll();
            }
        }
        else
        {
            /* The timeout has expired. */
            UnlockQueue( pxQueue );
            ( void ) TaskResumeAll();
            return errQUEUE_FULL;
        }
    }
}

BaseType_t xQueueGenericSendFromISR( QueueHandle_t xQueue,
                                     const void * const pvItemToQueue,
                                     BaseType_t * const pxHigherPriorityTaskWoken,
                                     const BaseType_t xCopyPosition )
{
    BaseType_t xReturn;
    UBaseType_t uxSavedInterruptStatus;
    Queue_t * const pxQueue = xQueue;
    configASSERT( pxQueue );
    configASSERT( !( ( pvItemToQueue == NULL ) && ( pxQueue->uxItemSize != ( UBaseType_t ) 0U ) ) );
    configASSERT( !( ( xCopyPosition == queueOVERWRITE ) && ( pxQueue->uxLength != 1 ) ) );

    portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
    /* Similar to xQueueGenericSend, except without blocking if there is no room
     * in the queue.  Also don't directly wake a task that was blocked on a queue
     * read, instead return a flag to say whether a context switch is required or
     * not (i.e. has a task with a higher priority than us been woken by this
     * post). */
    uxSavedInterruptStatus = ( UBaseType_t ) ENTER_CRITICAL_FROM_ISR();
    {
        if( ( pxQueue->uxMessagesWaiting < pxQueue->uxLength ) || ( xCopyPosition == queueOVERWRITE ) )
        {
            const int8_t cTxLock = pxQueue->cTxLock;
            const UBaseType_t uxPreviousMessagesWaiting = pxQueue->uxMessagesWaiting;
            /* Semaphores use xQueueGiveFromISR(), so pxQueue will not be a
             *  semaphore or mutex.  That means CopyDataToQueue() cannot result
             *  in a task disinheriting a priority and CopyDataToQueue() can be
             *  called here even though the disinherit function does not check if
             *  the scheduler is suspended before accessing the ready lists. */
            ( void ) CopyDataToQueue( pxQueue, pvItemToQueue, xCopyPosition );
            /* The event list is not altered if the queue is locked.  This will
             * be done when the queue is unlocked later. */
            if( cTxLock == queueUNLOCKED )
            {
                #if ( configUSE_QUEUE_SETS == 1 )
                {
                    if( pxQueue->pxQueueSetContainer != NULL )
                    {
                        if( ( xCopyPosition == queueOVERWRITE ) && ( uxPreviousMessagesWaiting != ( UBaseType_t ) 0 ) )
                        {
                            /* Do not notify the queue set as an existing item
                             * was overwritten in the queue so the number of items
                             * in the queue has not changed. */
                        }
                        else if( NotifyQueueSetContainer( pxQueue ) != false )
                        {
                            /* The queue is a member of a queue set, and posting
                             * to the queue set caused a higher priority task to
                             * unblock.  A context switch is required. */
                            if( pxHigherPriorityTaskWoken != NULL )
                            {
                                *pxHigherPriorityTaskWoken = true;
                            }
                        }
                    }
                    else
                    {
                        if(pxQueue->xTasksWaitingToReceive.Length > 0)
                        {
                            if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != false )
                            {
                                /* The task waiting has a higher priority so
                                 *  record that a context switch is required. */
                                if( pxHigherPriorityTaskWoken != NULL )
                                {
                                    *pxHigherPriorityTaskWoken = true;
                                }
                            }
                        }
                    }
                }
                #else /* configUSE_QUEUE_SETS */
                {
                    if( LIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == false )
                    {
                        if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != false )
                        {
                            /* The task waiting has a higher priority so record that a
                             * context switch is required. */
                            if( pxHigherPriorityTaskWoken != NULL )
                            {
                                *pxHigherPriorityTaskWoken = true;
                            }
                        }
                    }
                    /* Not used in this path. */
                    ( void ) uxPreviousMessagesWaiting;
                }
                #endif /* configUSE_QUEUE_SETS */
            }
            else
            {
                /* Increment the lock count so the task that unlocks the queue
                 * knows that data was posted while it was locked. */
                IncrementQueueTxLock( pxQueue, cTxLock );
            }
            xReturn = true;
        }
        else
        {
            xReturn = errQUEUE_FULL;
        }
    }
    EXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );
    return xReturn;
}

BaseType_t xQueueGiveFromISR( QueueHandle_t xQueue,
                              BaseType_t * const pxHigherPriorityTaskWoken )
{
    BaseType_t xReturn;
    UBaseType_t uxSavedInterruptStatus;
    Queue_t * const pxQueue = xQueue;
    /* Similar to xQueueGenericSendFromISR() but used with semaphores where the
     * item size is 0.  Don't directly wake a task that was blocked on a queue
     * read, instead return a flag to say whether a context switch is required or
     * not (i.e. has a task with a higher priority than us been woken by this
     * post). */
    configASSERT( pxQueue );
    /* xQueueGenericSendFromISR() should be used instead of xQueueGiveFromISR()
     * if the item size is not 0. */
    configASSERT( pxQueue->uxItemSize == 0 );
    /* Normally a mutex would not be given from an interrupt, especially if
     * there is a mutex holder, as priority inheritance makes no sense for an
     * interrupts, only tasks. */
    configASSERT( !( ( pxQueue->uxQueueType == queueQUEUE_IS_MUTEX ) && ( pxQueue->u.xSemaphore.xMutexHolder != NULL ) ) );

    portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
    uxSavedInterruptStatus = ( UBaseType_t ) ENTER_CRITICAL_FROM_ISR();
    {
        const UBaseType_t uxMessagesWaiting = pxQueue->uxMessagesWaiting;
        /* When the queue is used to implement a semaphore no data is ever
         * moved through the queue but it is still valid to see if the queue 'has
         * space'. */
        if( uxMessagesWaiting < pxQueue->uxLength )
        {
            const int8_t cTxLock = pxQueue->cTxLock;
            /* A task can only have an inherited priority if it is a mutex
             * holder - and if there is a mutex holder then the mutex cannot be
             * given from an ISR.  As this is the ISR version of the function it
             * can be assumed there is no mutex holder and no need to determine if
             * priority disinheritance is needed.  Simply increase the count of
             * messages (semaphores) available. */
            pxQueue->uxMessagesWaiting = ( UBaseType_t ) ( uxMessagesWaiting + ( UBaseType_t ) 1 );
            /* The event list is not altered if the queue is locked.  This will
             * be done when the queue is unlocked later. */
            if( cTxLock == queueUNLOCKED )
            {
                #if ( configUSE_QUEUE_SETS == 1 )
                {
                    if( pxQueue->pxQueueSetContainer != NULL )
                    {
                        if( NotifyQueueSetContainer( pxQueue ) != false )
                        {
                            /* The semaphore is a member of a queue set, and
                             * posting to the queue set caused a higher priority
                             * task to unblock.  A context switch is required. */
                            if( pxHigherPriorityTaskWoken != NULL )
                            {
                                *pxHigherPriorityTaskWoken = true;
                            }
                        }
                    }
                    else
                    {
                        if(pxQueue->xTasksWaitingToReceive.Length > 0)
                        {
                            if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != false )
                            {
                                /* The task waiting has a higher priority so
                                 *  record that a context switch is required. */
                                if( pxHigherPriorityTaskWoken != NULL )
                                {
                                    *pxHigherPriorityTaskWoken = true;
                                }
                            }
                        }
                    }
                }
                #else /* configUSE_QUEUE_SETS */
                {
                    if( LIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == false )
                    {
                        if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != false )
                        {
                            /* The task waiting has a higher priority so record that a
                             * context switch is required. */
                            if( pxHigherPriorityTaskWoken != NULL )
                            {
                                *pxHigherPriorityTaskWoken = true;
                            }
                        }
                    }
                }
                #endif /* configUSE_QUEUE_SETS */
            }
            else
            {
                /* Increment the lock count so the task that unlocks the queue
                 * knows that data was posted while it was locked. */
                IncrementQueueTxLock( pxQueue, cTxLock );
            }
            xReturn = true;
        }
        else
        {
            xReturn = errQUEUE_FULL;
        }
    }
    EXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );
    return xReturn;
}

BaseType_t xQueueReceive( QueueHandle_t xQueue,
                          void * const pvBuffer,
                          TickType_t xTicksToWait )
{
    BaseType_t xEntryTimeSet = false;
    TimeOut_t xTimeOut;
    Queue_t * const pxQueue = xQueue;
    /* Check the pointer is not NULL. */
    configASSERT( ( pxQueue ) );
    /* The buffer into which data is received can only be NULL if the data size
     * is zero (so no data is copied into the buffer). */
    configASSERT( !( ( ( pvBuffer ) == NULL ) && ( ( pxQueue )->uxItemSize != ( UBaseType_t ) 0U ) ) );
    /* Cannot block if the scheduler is suspended. */
    #if ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) )
    {
        configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) );
    }
    #endif
    for( ; ; )
    {
        ENTER_CRITICAL();
        {
            const UBaseType_t uxMessagesWaiting = pxQueue->uxMessagesWaiting;
            /* Is there data in the queue now?  To be running the calling task
             * must be the highest priority task wanting to access the queue. */
            if( uxMessagesWaiting > ( UBaseType_t ) 0 )
            {
                /* Data available, remove one item. */
                CopyDataFromQueue( pxQueue, pvBuffer );
                pxQueue->uxMessagesWaiting = ( UBaseType_t ) ( uxMessagesWaiting - ( UBaseType_t ) 1 );
                if(pxQueue->xTasksWaitingToSend.Length > 0)
                {
                    if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToSend ) ) != false )
                    {
                        queueYIELD_IF_USING_PREEMPTION();
                    }
                }
                EXIT_CRITICAL();
                return true;
            }
            else
            {
                if( xTicksToWait == ( TickType_t ) 0 )
                {
                    /* The queue was empty and no block time is specified (or
                     * the block time has expired) so leave now. */
                    EXIT_CRITICAL();
                    return errQUEUE_EMPTY;
                }
                else if( xEntryTimeSet == false )
                {
                    /* The queue was empty and a block time was specified so
                     * configure the timeout structure. */
                    vTaskInternalSetTimeOutState( &xTimeOut );
                    xEntryTimeSet = true;
                }
                else
                {
                    /* Entry time was already set. */
                    mtCOVERAGE_TEST_MARKER();
                }
            }
        }
        EXIT_CRITICAL();
        /* Interrupts and other tasks can send to and receive from the queue
         * now the critical section has been exited. */
        vTaskSuspendAll();
        LockQueue( pxQueue );
        /* Update the timeout state to see if it has expired yet. */
        if( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == false )
        {
            /* The timeout has not expired.  If the queue is still empty place
             * the task on the list of tasks waiting to receive from the queue. */
            if( IsQueueEmpty( pxQueue ) != false )
            {
                vTaskPlaceOnEventList( &( pxQueue->xTasksWaitingToReceive ), xTicksToWait );
                UnlockQueue( pxQueue );
                if( TaskResumeAll() == false )
                {
                    taskYIELD_WITHIN_API();
                }
            }
            else
            {
                /* The queue contains data again.  Loop back to try and read the
                 * data. */
                UnlockQueue( pxQueue );
                ( void ) TaskResumeAll();
            }
        }
        else
        {
            /* Timed out.  If there is no data in the queue exit, otherwise loop
             * back and attempt to read the data. */
            UnlockQueue( pxQueue );
            ( void ) TaskResumeAll();
            if( IsQueueEmpty( pxQueue ) != false )
            {
                return errQUEUE_EMPTY;
            }
        }
    }
}

BaseType_t xQueueSemaphoreTake( QueueHandle_t xQueue,
                                TickType_t xTicksToWait )
{
    BaseType_t xEntryTimeSet = false;
    TimeOut_t xTimeOut;
    Queue_t * const pxQueue = xQueue;
    #if ( configUSE_MUTEXES == 1 )
        BaseType_t xInheritanceOccurred = false;
    #endif
    /* Check the queue pointer is not NULL. */
    configASSERT( ( pxQueue ) );
    /* Check this really is a semaphore, in which case the item size will be
     * 0. */
    configASSERT( pxQueue->uxItemSize == 0 );
    /* Cannot block if the scheduler is suspended. */
    #if ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) )
    {
        configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) );
    }
    #endif
    for( ; ; )
    {
        ENTER_CRITICAL();
        {
            /* Semaphores are queues with an item size of 0, and where the
             * number of messages in the queue is the semaphore's count value. */
            const UBaseType_t uxSemaphoreCount = pxQueue->uxMessagesWaiting;
            /* Is there data in the queue now?  To be running the calling task
             * must be the highest priority task wanting to access the queue. */
            if( uxSemaphoreCount > ( UBaseType_t ) 0 )
            {
                /* Semaphores are queues with a data size of zero and where the
                 * messages waiting is the semaphore's count.  Reduce the count. */
                pxQueue->uxMessagesWaiting = ( UBaseType_t ) ( uxSemaphoreCount - ( UBaseType_t ) 1 );
                #if ( configUSE_MUTEXES == 1 )
                {
                    if( pxQueue->uxQueueType == queueQUEUE_IS_MUTEX )
                    {
                        /* Record the information required to implement
                         * priority inheritance should it become necessary. */
                        pxQueue->u.xSemaphore.xMutexHolder = pvTaskIncrementMutexHeldCount();
                    }
                }
                #endif /* configUSE_MUTEXES */
                /* Check to see if other tasks are blocked waiting to give the
                 * semaphore, and if so, unblock the highest priority such task. */
                if(pxQueue->xTasksWaitingToSend.Length > 0)
                {
                    if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToSend ) ) != false )
                    {
                        queueYIELD_IF_USING_PREEMPTION();
                    }
                }
                EXIT_CRITICAL();
                return true;
            }
            else
            {
                if( xTicksToWait == ( TickType_t ) 0 )
                {
                    /* The semaphore count was 0 and no block time is specified
                     * (or the block time has expired) so exit now. */
                    EXIT_CRITICAL();
                    return errQUEUE_EMPTY;
                }
                else if( xEntryTimeSet == false )
                {
                    /* The semaphore count was 0 and a block time was specified
                     * so configure the timeout structure ready to block. */
                    vTaskInternalSetTimeOutState( &xTimeOut );
                    xEntryTimeSet = true;
                }
            }
        }
        EXIT_CRITICAL();
        /* Interrupts and other tasks can give to and take from the semaphore
         * now the critical section has been exited. */
        vTaskSuspendAll();
        LockQueue( pxQueue );
        /* Update the timeout state to see if it has expired yet. */
        if( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == false )
        {
            /* A block time is specified and not expired.  If the semaphore
             * count is 0 then enter the Blocked state to wait for a semaphore to
             * become available.  As semaphores are implemented with queues the
             * queue being empty is equivalent to the semaphore count being 0. */
            if( IsQueueEmpty( pxQueue ) != false )
            {
                #if ( configUSE_MUTEXES == 1 )
                {
                    if( pxQueue->uxQueueType == queueQUEUE_IS_MUTEX )
                    {
                        ENTER_CRITICAL();
                        {
                            xInheritanceOccurred = xTaskPriorityInherit( pxQueue->u.xSemaphore.xMutexHolder );
                        }
                        EXIT_CRITICAL();
                    }
                }
                #endif /* if ( configUSE_MUTEXES == 1 ) */
                vTaskPlaceOnEventList( &( pxQueue->xTasksWaitingToReceive ), xTicksToWait );
                UnlockQueue( pxQueue );
                if( TaskResumeAll() == false )
                {
                    taskYIELD_WITHIN_API();
                }
            }
            else
            {
                /* There was no timeout and the semaphore count was not 0, so
                 * attempt to take the semaphore again. */
                UnlockQueue( pxQueue );
                ( void ) TaskResumeAll();
            }
        }
        else
        {
            /* Timed out. */
            UnlockQueue( pxQueue );
            ( void ) TaskResumeAll();
            /* If the semaphore count is 0 exit now as the timeout has
             * expired.  Otherwise return to attempt to take the semaphore that is
             * known to be available.  As semaphores are implemented by queues the
             * queue being empty is equivalent to the semaphore count being 0. */
            if( IsQueueEmpty( pxQueue ) != false )
            {
                #if ( configUSE_MUTEXES == 1 )
                {
                    /* xInheritanceOccurred could only have be set if
                     * pxQueue->uxQueueType == queueQUEUE_IS_MUTEX so no need to
                     * test the mutex type again to check it is actually a mutex. */
                    if( xInheritanceOccurred != false )
                    {
                        ENTER_CRITICAL();
                        {
                            UBaseType_t uxHighestWaitingPriority;
                            /* This task blocking on the mutex caused another
                             * task to inherit this task's priority.  Now this task
                             * has timed out the priority should be disinherited
                             * again, but only as low as the next highest priority
                             * task that is waiting for the same mutex. */
                            uxHighestWaitingPriority = GetHighestPriorityOfWaitToReceiveList( pxQueue );
                            vTaskPriorityDisinheritAfterTimeout( pxQueue->u.xSemaphore.xMutexHolder, uxHighestWaitingPriority );
                        }
                        EXIT_CRITICAL();
                    }
                }
                #endif /* configUSE_MUTEXES */
                return errQUEUE_EMPTY;
            }
        }
    }
}

BaseType_t xQueuePeek( QueueHandle_t xQueue,
                       void * const pvBuffer,
                       TickType_t xTicksToWait )
{
    BaseType_t xEntryTimeSet = false;
    TimeOut_t xTimeOut;
    int8_t * pcOriginalReadPosition;
    Queue_t * const pxQueue = xQueue;
    /* Check the pointer is not NULL. */
    configASSERT( ( pxQueue ) );
    /* The buffer into which data is received can only be NULL if the data size
     * is zero (so no data is copied into the buffer. */
    configASSERT( !( ( ( pvBuffer ) == NULL ) && ( ( pxQueue )->uxItemSize != ( UBaseType_t ) 0U ) ) );
    /* Cannot block if the scheduler is suspended. */
    #if ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) )
    {
        configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) );
    }
    #endif
    for( ; ; )
    {
        ENTER_CRITICAL();
        {
            const UBaseType_t uxMessagesWaiting = pxQueue->uxMessagesWaiting;
            /* Is there data in the queue now?  To be running the calling task
             * must be the highest priority task wanting to access the queue. */
            if( uxMessagesWaiting > ( UBaseType_t ) 0 )
            {
                /* Remember the read position so it can be reset after the data
                 * is read from the queue as this function is only peeking the
                 * data, not removing it. */
                pcOriginalReadPosition = pxQueue->u.xQueue.pcReadFrom;
                CopyDataFromQueue( pxQueue, pvBuffer );
                /* The data is not being removed, so reset the read pointer. */
                pxQueue->u.xQueue.pcReadFrom = pcOriginalReadPosition;
                /* The data is being left in the queue, so see if there are
                 * any other tasks waiting for the data. */
                if(pxQueue->xTasksWaitingToReceive.Length > 0)
                {
                    if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != false )
                    {
                        /* The task waiting has a higher priority than this task. */
                        queueYIELD_IF_USING_PREEMPTION();
                    }
                }
                EXIT_CRITICAL();
                return true;
            }
            else
            {
                if( xTicksToWait == ( TickType_t ) 0 )
                {
                    /* The queue was empty and no block time is specified (or
                     * the block time has expired) so leave now. */
                    EXIT_CRITICAL();
                    return errQUEUE_EMPTY;
                }
                else if( xEntryTimeSet == false )
                {
                    /* The queue was empty and a block time was specified so
                     * configure the timeout structure ready to enter the blocked
                     * state. */
                    vTaskInternalSetTimeOutState( &xTimeOut );
                    xEntryTimeSet = true;
                }
            }
        }
        EXIT_CRITICAL();
        /* Interrupts and other tasks can send to and receive from the queue
         * now that the critical section has been exited. */
        vTaskSuspendAll();
        LockQueue( pxQueue );
        /* Update the timeout state to see if it has expired yet. */
        if( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == false )
        {
            /* Timeout has not expired yet, check to see if there is data in the
            * queue now, and if not enter the Blocked state to wait for data. */
            if( IsQueueEmpty( pxQueue ) != false )
            {
                vTaskPlaceOnEventList( &( pxQueue->xTasksWaitingToReceive ), xTicksToWait );
                UnlockQueue( pxQueue );
                if( TaskResumeAll() == false )
                {
                    taskYIELD_WITHIN_API();
                }
            }
            else
            {
                /* There is data in the queue now, so don't enter the blocked
                 * state, instead return to try and obtain the data. */
                UnlockQueue( pxQueue );
                ( void ) TaskResumeAll();
            }
        }
        else
        {
            /* The timeout has expired.  If there is still no data in the queue
             * exit, otherwise go back and try to read the data again. */
            UnlockQueue( pxQueue );
            ( void ) TaskResumeAll();
            if( IsQueueEmpty( pxQueue ) != false )
            {
                return errQUEUE_EMPTY;
            }
        }
    }
}
BaseType_t xQueueReceiveFromISR( QueueHandle_t xQueue,
                                 void * const pvBuffer,
                                 BaseType_t * const pxHigherPriorityTaskWoken )
{
    BaseType_t xReturn;
    UBaseType_t uxSavedInterruptStatus;
    Queue_t * const pxQueue = xQueue;
    configASSERT( pxQueue );
    configASSERT( !( ( pvBuffer == NULL ) && ( pxQueue->uxItemSize != ( UBaseType_t ) 0U ) ) );
    portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
    uxSavedInterruptStatus = ( UBaseType_t ) ENTER_CRITICAL_FROM_ISR();
    {
        const UBaseType_t uxMessagesWaiting = pxQueue->uxMessagesWaiting;
        /* Cannot block in an ISR, so check there is data available. */
        if( uxMessagesWaiting > ( UBaseType_t ) 0 )
        {
            const int8_t cRxLock = pxQueue->cRxLock;
            CopyDataFromQueue( pxQueue, pvBuffer );
            pxQueue->uxMessagesWaiting = ( UBaseType_t ) ( uxMessagesWaiting - ( UBaseType_t ) 1 );
            /* If the queue is locked the event list will not be modified.
             * Instead update the lock count so the task that unlocks the queue
             * will know that an ISR has removed data while the queue was
             * locked. */
            if( cRxLock == queueUNLOCKED )
            {
                if(pxQueue->xTasksWaitingToSend.Length > 0)
                {
                    if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToSend ) ) != false )
                    {
                        /* The task waiting has a higher priority than us so
                         * force a context switch. */
                        if( pxHigherPriorityTaskWoken != NULL )
                        {
                            *pxHigherPriorityTaskWoken = true;
                        }
                    }
                }
            }
            else
            {
                /* Increment the lock count so the task that unlocks the queue
                 * knows that data was removed while it was locked. */
                IncrementQueueRxLock( pxQueue, cRxLock );
            }
            xReturn = true;
        }
        else
        {
            xReturn = false;
        }
    }
    EXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );
    return xReturn;
}

BaseType_t xQueuePeekFromISR( QueueHandle_t xQueue,
                              void * const pvBuffer )
{
    BaseType_t xReturn;
    UBaseType_t uxSavedInterruptStatus;
    int8_t * pcOriginalReadPosition;
    Queue_t * const pxQueue = xQueue;
    configASSERT( pxQueue );
    configASSERT( !( ( pvBuffer == NULL ) && ( pxQueue->uxItemSize != ( UBaseType_t ) 0U ) ) );
    configASSERT( pxQueue->uxItemSize != 0 ); /* Can't peek a semaphore. */

    portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
    uxSavedInterruptStatus = ( UBaseType_t ) ENTER_CRITICAL_FROM_ISR();
    {
        /* Cannot block in an ISR, so check there is data available. */
        if( pxQueue->uxMessagesWaiting > ( UBaseType_t ) 0 )
        {
            /* Remember the read position so it can be reset as nothing is
             * actually being removed from the queue. */
            pcOriginalReadPosition = pxQueue->u.xQueue.pcReadFrom;
            CopyDataFromQueue( pxQueue, pvBuffer );
            pxQueue->u.xQueue.pcReadFrom = pcOriginalReadPosition;
            xReturn = true;
        }
        else
        {
            xReturn = false;
        }
    }
    EXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );
    return xReturn;
}
UBaseType_t uxQueueMessagesWaiting( const QueueHandle_t xQueue )
{
    UBaseType_t uxReturn;
    configASSERT( xQueue );
    ENTER_CRITICAL();
    {
        uxReturn = ( ( Queue_t * ) xQueue )->uxMessagesWaiting;
    }
    EXIT_CRITICAL();
    return uxReturn;
}
UBaseType_t uxQueueSpacesAvailable( const QueueHandle_t xQueue )
{
    UBaseType_t uxReturn;
    Queue_t * const pxQueue = xQueue;
    configASSERT( pxQueue );
    ENTER_CRITICAL();
    {
        uxReturn = ( UBaseType_t ) ( pxQueue->uxLength - pxQueue->uxMessagesWaiting );
    }
    EXIT_CRITICAL();
    return uxReturn;
}

UBaseType_t uxQueueMessagesWaitingFromISR( const QueueHandle_t xQueue )
{
    Queue_t * const pxQueue = xQueue;
    configASSERT( pxQueue );
    return pxQueue->uxMessagesWaiting;
}

void vQueueDelete( QueueHandle_t xQueue )
{
    Queue_t * const pxQueue = xQueue;
    configASSERT( pxQueue );
    #if ( configQUEUE_REGISTRY_SIZE > 0 )
    {
        vQueueUnregisterQueue( pxQueue );
    }
    #endif
    #if ( ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) && ( configSUPPORT_STATIC_ALLOCATION == 0 ) )
    {
        /* The queue can only have been allocated dynamically - free it
         * again. */
        vPortFree( pxQueue );
    }
    #elif ( ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) && ( configSUPPORT_STATIC_ALLOCATION == 1 ) )
    {
        /* The queue could have been allocated statically or dynamically, so
         * check before attempting to free the memory. */
        if( pxQueue->StaticallyAllocated == ( uint8_t ) false )
        {
            vPortFree( pxQueue );
        }
    }
    #else /* if ( ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) && ( configSUPPORT_STATIC_ALLOCATION == 0 ) ) */
    {
        /* The queue must have been statically allocated, so is not going to be
         * deleted.  Avoid compiler warnings about the unused parameter. */
        ( void ) pxQueue;
    }
    #endif /* configSUPPORT_DYNAMIC_ALLOCATION */
}
UBaseType_t uxQueueGetQueueItemSize( QueueHandle_t xQueue ) /*  */
{
    return ( ( Queue_t * ) xQueue )->uxItemSize;
}

UBaseType_t uxQueueGetQueueLength( QueueHandle_t xQueue ) /*  */
{
    return ( ( Queue_t * ) xQueue )->uxLength;
}

#if ( configUSE_MUTEXES == 1 )
static UBaseType_t GetHighestPriorityOfWaitToReceiveList( Queue_t * const pxQueue )
{
    UBaseType_t uxHighestPriorityOfWaitingTasks;
    /* If a task waiting for a mutex causes the mutex holder to inherit a
        * priority, but the waiting task times out, then the holder should
        * disinherit the priority - but only down to the highest priority of any
        * other tasks that are waiting for the same mutex.  For this purpose,
        * return the priority of the highest priority task that is waiting for the
        * mutex. */
    if(pxQueue->xTasksWaitingToReceive.Length > 0U )
    {
        uxHighestPriorityOfWaitingTasks = ( UBaseType_t ) ( ( UBaseType_t ) configMAX_PRIORITIES - ( UBaseType_t )(pxQueue->xTasksWaitingToReceive.head()->Value));
    }
    else
    {
        uxHighestPriorityOfWaitingTasks = tskIDLE_PRIORITY;
    }
    return uxHighestPriorityOfWaitingTasks;
}
#endif /* configUSE_MUTEXES */

static BaseType_t CopyDataToQueue( Queue_t * const pxQueue,
                                      const void * pvItemToQueue,
                                      const BaseType_t xPosition )
{
    BaseType_t xReturn = false;
    UBaseType_t uxMessagesWaiting;
    /* This function is called from a critical section. */
    uxMessagesWaiting = pxQueue->uxMessagesWaiting;
    if( pxQueue->uxItemSize == ( UBaseType_t ) 0 )
    {
        #if ( configUSE_MUTEXES == 1 )
        {
            if( pxQueue->uxQueueType == queueQUEUE_IS_MUTEX )
            {
                /* The mutex is no longer being held. */
                xReturn = xTaskPriorityDisinherit( pxQueue->u.xSemaphore.xMutexHolder );
                pxQueue->u.xSemaphore.xMutexHolder = NULL;
            }
        }
        #endif /* configUSE_MUTEXES */
    }
    else if( xPosition == queueSEND_TO_BACK )
    {
        ( void ) memcpy( ( void * ) pxQueue->pcWriteTo, pvItemToQueue, ( size_t ) pxQueue->uxItemSize );
        pxQueue->pcWriteTo += pxQueue->uxItemSize;
        if( pxQueue->pcWriteTo >= pxQueue->u.xQueue.pcTail )
        {
            pxQueue->pcWriteTo = pxQueue->pcHead;
        }
    }
    else
    {
        ( void ) memcpy( ( void * ) pxQueue->u.xQueue.pcReadFrom, pvItemToQueue, ( size_t ) pxQueue->uxItemSize );
        pxQueue->u.xQueue.pcReadFrom -= pxQueue->uxItemSize;
        if( pxQueue->u.xQueue.pcReadFrom < pxQueue->pcHead )
        {
            pxQueue->u.xQueue.pcReadFrom = ( pxQueue->u.xQueue.pcTail - pxQueue->uxItemSize );
        }
        if( xPosition == queueOVERWRITE )
        {
            if( uxMessagesWaiting > ( UBaseType_t ) 0 )
            {
                /* An item is not being added but overwritten, so subtract
                 * one from the recorded number of items in the queue so when
                 * one is added again below the number of recorded items remains
                 * correct. */
                --uxMessagesWaiting;
            }
        }
    }
    pxQueue->uxMessagesWaiting = ( UBaseType_t ) ( uxMessagesWaiting + ( UBaseType_t ) 1 );
    return xReturn;
}

static void CopyDataFromQueue( Queue_t * const pxQueue,
                                  void * const pvBuffer )
{
    if( pxQueue->uxItemSize != ( UBaseType_t ) 0 )
    {
        pxQueue->u.xQueue.pcReadFrom += pxQueue->uxItemSize;
        if( pxQueue->u.xQueue.pcReadFrom >= pxQueue->u.xQueue.pcTail )
        {
            pxQueue->u.xQueue.pcReadFrom = pxQueue->pcHead;
        }
        ( void ) memcpy( ( void * ) pvBuffer, ( void * ) pxQueue->u.xQueue.pcReadFrom, ( size_t ) pxQueue->uxItemSize );
    }
}

static void UnlockQueue( Queue_t * const pxQueue )
{
    /* THIS FUNCTION MUST BE CALLED WITH THE SCHEDULER SUSPENDED. */
    /* The lock counts contains the number of extra data items placed or
     * removed from the queue while the queue was locked.  When a queue is
     * locked items can be added or removed, but the event lists cannot be
     * updated. */
    ENTER_CRITICAL();
    {
        int8_t cTxLock = pxQueue->cTxLock;
        /* See if data was added to the queue while it was locked. */
        while( cTxLock > queueLOCKED_UNMODIFIED )
        {
            /* Data was posted while the queue was locked.  Are any tasks
             * blocked waiting for data to become available? */
            #if ( configUSE_QUEUE_SETS == 1 )
            {
                if( pxQueue->pxQueueSetContainer != NULL )
                {
                    if( NotifyQueueSetContainer( pxQueue ) != false )
                    {
                        /* The queue is a member of a queue set, and posting to
                         * the queue set caused a higher priority task to unblock.
                         * A context switch is required. */
                        vTaskMissedYield();
                    }
                }
                else
                {
                    /* Tasks that are removed from the event list will get
                     * added to the pending ready list as the scheduler is still
                     * suspended. */
                    if(pxQueue->xTasksWaitingToReceive.Length > 0)
                    {
                        if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != false )
                        {
                            /* The task waiting has a higher priority so record that a
                             * context switch is required. */
                            vTaskMissedYield();
                        }
                    }
                    else
                    {
                        break;
                    }
                }
            }
            #else /* configUSE_QUEUE_SETS */
            {
                /* Tasks that are removed from the event list will get added to
                 * the pending ready list as the scheduler is still suspended. */
                if( LIST_IS_EMPTY( &( pxQueue->xTasksWaitingToReceive ) ) == false )
                {
                    if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToReceive ) ) != false )
                    {
                        /* The task waiting has a higher priority so record that
                         * a context switch is required. */
                        vTaskMissedYield();
                    }
                }
                else
                {
                    break;
                }
            }
            #endif /* configUSE_QUEUE_SETS */
            --cTxLock;
        }
        pxQueue->cTxLock = queueUNLOCKED;
    }
    EXIT_CRITICAL();
    /* Do the same for the Rx lock. */
    ENTER_CRITICAL();
    {
        int8_t cRxLock = pxQueue->cRxLock;
        while( cRxLock > queueLOCKED_UNMODIFIED )
        {
            if(pxQueue->xTasksWaitingToSend.Length > 0)
            {
                if( xTaskRemoveFromEventList( &( pxQueue->xTasksWaitingToSend ) ) != false )
                {
                    vTaskMissedYield();
                }
                --cRxLock;
            }
            else
            {
                break;
            }
        }
        pxQueue->cRxLock = queueUNLOCKED;
    }
    EXIT_CRITICAL();
}

static BaseType_t IsQueueEmpty( const Queue_t * pxQueue )
{
    BaseType_t xReturn;
    ENTER_CRITICAL();
    {
        if( pxQueue->uxMessagesWaiting == ( UBaseType_t ) 0 )
        {
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

BaseType_t xQueueIsQueueEmptyFromISR( const QueueHandle_t xQueue )
{
    BaseType_t xReturn;
    Queue_t * const pxQueue = xQueue;
    configASSERT( pxQueue );
    if( pxQueue->uxMessagesWaiting == ( UBaseType_t ) 0 )
    {
        xReturn = true;
    }
    else
    {
        xReturn = false;
    }
    return xReturn;
}

static BaseType_t IsQueueFull( const Queue_t * pxQueue )
{
    ENTER_CRITICAL();
    BaseType_t xReturn = pxQueue->uxMessagesWaiting == pxQueue->uxLength;
    EXIT_CRITICAL();
    return xReturn;
}

BaseType_t xQueueIsQueueFullFromISR( const QueueHandle_t xQueue )
{
    BaseType_t xReturn;
    Queue_t * const pxQueue = xQueue;
    configASSERT( pxQueue );
    return pxQueue->uxMessagesWaiting == pxQueue->uxLength;
}

#if ( configUSE_TIMERS == 1 )
    void vQueueWaitForMessageRestricted( QueueHandle_t xQueue,
                                         TickType_t xTicksToWait,
                                         const BaseType_t xWaitIndefinitely )
    {
        Queue_t * const pxQueue = xQueue;
        /* This function should not be called by application code hence the
         * 'Restricted' in its name.  It is not part of the public API.  It is
         * designed for use by kernel code, and has special calling requirements.
         * It can result in ListInsert() being called on a list that can only
         * possibly ever have one item in it, so the list will be fast, but even
         * so it should be called with the scheduler locked and not from a critical
         * section. */
        /* Only do anything if there are no messages in the queue.  This function
         *  will not actually cause the task to block, just place it on a blocked
         *  list.  It will not block until the scheduler is unlocked - at which
         *  time a yield will be performed.  If an item is added to the queue while
         *  the queue is locked, and the calling task blocks on the queue, then the
         *  calling task will be immediately unblocked when the queue is unlocked. */
        LockQueue( pxQueue );
        if( pxQueue->uxMessagesWaiting == ( UBaseType_t ) 0U )
        {
            /* There is nothing in the queue, block for the specified period. */
            vTaskPlaceOnEventListRestricted( &( pxQueue->xTasksWaitingToReceive ), xTicksToWait, xWaitIndefinitely );
        }
        UnlockQueue( pxQueue );
    }
#endif /* configUSE_TIMERS */
#if ( ( configUSE_QUEUE_SETS == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
QueueSetHandle_t xQueueCreateSet( const UBaseType_t uxEventQueueLength )
{
    return xQueueGenericCreate( uxEventQueueLength, ( UBaseType_t ) sizeof( Queue_t * ), queueQUEUE_TYPE_SET );
}
#endif /* configUSE_QUEUE_SETS */
#if ( configUSE_QUEUE_SETS == 1 )
BaseType_t xQueueAddToSet( QueueSetMemberHandle_t xQueueOrSemaphore,
                            QueueSetHandle_t xQueueSet )
{
    BaseType_t xReturn;
    ENTER_CRITICAL();
    {
        if( ( ( Queue_t * ) xQueueOrSemaphore )->pxQueueSetContainer != NULL )
        {
            /* Cannot add a queue/semaphore to more than one queue set. */
            xReturn = false;
        }
        else if( ( ( Queue_t * ) xQueueOrSemaphore )->uxMessagesWaiting != ( UBaseType_t ) 0 )
        {
            /* Cannot add a queue/semaphore to a queue set if there are already
                * items in the queue/semaphore. */
            xReturn = false;
        }
        else
        {
            ( ( Queue_t * ) xQueueOrSemaphore )->pxQueueSetContainer = xQueueSet;
            xReturn = true;
        }
    }
    EXIT_CRITICAL();
    return xReturn;
}
#endif /* configUSE_QUEUE_SETS */
#if ( configUSE_QUEUE_SETS == 1 )
    BaseType_t xQueueRemoveFromSet( QueueSetMemberHandle_t xQueueOrSemaphore,
                                    QueueSetHandle_t xQueueSet )
    {
        BaseType_t xReturn;
        Queue_t * const pxQueueOrSemaphore = ( Queue_t * ) xQueueOrSemaphore;
        if( pxQueueOrSemaphore->pxQueueSetContainer != xQueueSet )
        {
            /* The queue was not a member of the set. */
            xReturn = false;
        }
        else if( pxQueueOrSemaphore->uxMessagesWaiting != ( UBaseType_t ) 0 )
        {
            /* It is dangerous to remove a queue from a set when the queue is
             * not empty because the queue set will still hold pending events for
             * the queue. */
            xReturn = false;
        }
        else
        {
            ENTER_CRITICAL();
            {
                /* The queue is no longer contained in the set. */
                pxQueueOrSemaphore->pxQueueSetContainer = NULL;
            }
            EXIT_CRITICAL();
            xReturn = true;
        }
        return xReturn;
    }
#endif /* configUSE_QUEUE_SETS */

#if ( configUSE_QUEUE_SETS == 1 )
    QueueSetMemberHandle_t xQueueSelectFromSet( QueueSetHandle_t xQueueSet,
                                                TickType_t const xTicksToWait )
    {
        QueueSetMemberHandle_t xReturn = NULL;
        ( void ) xQueueReceive( ( QueueHandle_t ) xQueueSet, &xReturn, xTicksToWait );
        return xReturn;
    }
#endif /* configUSE_QUEUE_SETS */

#if ( configUSE_QUEUE_SETS == 1 )
    QueueSetMemberHandle_t xQueueSelectFromSetFromISR( QueueSetHandle_t xQueueSet )
    {
        QueueSetMemberHandle_t xReturn = NULL;
        ( void ) xQueueReceiveFromISR( ( QueueHandle_t ) xQueueSet, &xReturn, NULL );
        return xReturn;
    }
#endif /* configUSE_QUEUE_SETS */

#if ( configUSE_QUEUE_SETS == 1 )
    static BaseType_t NotifyQueueSetContainer( const Queue_t * const pxQueue )
    {
        Queue_t * pxQueueSetContainer = pxQueue->pxQueueSetContainer;
        BaseType_t xReturn = false;
        /* This function must be called form a critical section. */
        /* The following line is not reachable in unit tests because every call
         * to NotifyQueueSetContainer is preceded by a check that
         * pxQueueSetContainer != NULL */
        configASSERT( pxQueueSetContainer ); /* LCOV_EXCL_BR_LINE */
        configASSERT( pxQueueSetContainer->uxMessagesWaiting < pxQueueSetContainer->uxLength );
        if( pxQueueSetContainer->uxMessagesWaiting < pxQueueSetContainer->uxLength )
        {
            const int8_t cTxLock = pxQueueSetContainer->cTxLock;
            /* The data copied is the handle of the queue that contains data. */
            xReturn = CopyDataToQueue( pxQueueSetContainer, &pxQueue, queueSEND_TO_BACK );
            if( cTxLock == queueUNLOCKED )
            {
                if(pxQueueSetContainer->xTasksWaitingToReceive.Length > 0)
                {
                    if( xTaskRemoveFromEventList( &( pxQueueSetContainer->xTasksWaitingToReceive ) ) != false )
                    {
                        /* The task waiting has a higher priority. */
                        xReturn = true;
                    }
                }
            }
            else
            {
                IncrementQueueTxLock( pxQueueSetContainer, cTxLock );
            }
        }
        return xReturn;
    }
#endif /* configUSE_QUEUE_SETS */
