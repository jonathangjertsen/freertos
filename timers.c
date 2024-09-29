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
#include <stdbool.h>
/* Defining MPU_WRAPPERS_INCLUDED_FROM_API_FILE prevents task.h from redefining
 * all the API functions to use the MPU wrappers.  That should only be done when
 * task.h is included from an application file. */
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"
/* The MPU ports require MPU_WRAPPERS_INCLUDED_FROM_API_FILE to be defined
 * for the header files above, but not in this file, in order to generate the
 * correct privileged Vs unprivileged linkage and placement. */
#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE

#if ( configUSE_TIMERS == 1 )
/* Misc definitions. */
#define tmrNO_DELAY                    ( ( TickType_t ) 0U )
#define tmrMAX_TIME_BEFORE_OVERFLOW    ( ( TickType_t ) -1 )
/* The name assigned to the timer service task. This can be overridden by
* defining configTIMER_SERVICE_TASK_NAME in FreeRTOSConfig.h. */
#ifndef configTIMER_SERVICE_TASK_NAME
    #define configTIMER_SERVICE_TASK_NAME    "Tmr Svc"
#endif
/* Bit definitions used in the ucStatus member of a timer structure. */
#define tmrSTATUS_IS_ACTIVE                  ( 0x01U )
#define tmrSTATUS_IS_STATICALLY_ALLOCATED    ( 0x02U )
#define tmrSTATUS_IS_AUTORELOAD              ( 0x04U )
/* The definition of the timers themselves. */
typedef struct tmrTimerControl                                               /* The old naming convention is used to prevent breaking kernel aware debuggers. */
{
    const char * pcTimerName;                                                /**< Text name.  This is not used by the kernel, it is included simply to make debugging easier. */
    ListItem_t xTimerListItem;                                               /**< Standard linked list item as used by all kernel features for event management. */
    TickType_t xTimerPeriodInTicks;                                          /**< How quickly and often the timer expires. */
    void * pvTimerID;                                                        /**< An ID to identify the timer.  This allows the timer to be identified when the same callback is used for multiple timers. */
    portTIMER_CALLBACK_ATTRIBUTE TimerCallbackFunction_t pxCallbackFunction; /**< The function that will be called when the timer expires. */
    uint8_t ucStatus;                                                        /**< Holds bits to say if the timer was statically allocated or not, and if it is active or not. */
} xTIMER;
/* The old xTIMER name is maintained above then typedefed to the new Timer_t
* name below to enable the use of older kernel aware debuggers. */
typedef xTIMER Timer_t;
/* The definition of messages that can be sent and received on the timer queue.
* Two types of message can be queued - messages that manipulate a software timer,
* and messages that request the execution of a non-timer related callback.  The
* two message types are defined in two separate structures, xTimerParametersType
* and xCallbackParametersType respectively. */
typedef struct tmrTimerParameters
{
    TickType_t xMessageValue; /**< An optional value used by a subset of commands, for example, when changing the period of a timer. */
    Timer_t * pxTimer;        /**< The timer to which the command will be applied. */
} TimerParameter_t;

typedef struct tmrCallbackParameters
{
    portTIMER_CALLBACK_ATTRIBUTE
    PendedFunction_t pxCallbackFunction; /* << The callback function to execute. */
    void * pvParameter1;                 /* << The value that will be used as the callback functions first parameter. */
    uint32_t ulParameter2;               /* << The value that will be used as the callback functions second parameter. */
} CallbackParameters_t;
/* The structure that contains the two message types, along with an identifier
* that is used to determine which message type is valid. */
typedef struct tmrTimerQueueMessage
{
    BaseType_t xMessageID; /**< The command being sent to the timer service task. */
    union
    {
        TimerParameter_t xTimerParameters;
        /* Don't include xCallbackParameters if it is not going to be used as
            * it makes the structure (and therefore the timer queue) larger. */
        #if ( INCLUDE_xTimerPendFunctionCall == 1 )
            CallbackParameters_t xCallbackParameters;
        #endif /* INCLUDE_xTimerPendFunctionCall */
    } u;
} DaemonTaskMessage_t;
/* The list in which active timers are stored.  Timers are referenced in expire
* time order, with the nearest expiry time at the front of the list.  Only the
* timer service task is allowed to access these lists.
* ActiveTimerList1 and ActiveTimerList2 could be at function scope but that
* breaks some kernel aware debuggers, and debuggers that reply on removing the
* static qualifier. */
 static List_t ActiveTimerList1;
 static List_t ActiveTimerList2;
 static List_t * CurrentTimerList;
 static List_t * OverflowTimerList;
/* A queue that is used to send commands to the timer service task. */
 static QueueHandle_t xTimerQueue = NULL;
 static TaskHandle_t xTimerTaskHandle = NULL;

/*
* Initialise the infrastructure used by the timer service task if it has not
* been initialised already.
*/
static void CheckForValidListAndQueue( void ) ;
/*
* The timer service task (daemon).  Timer functionality is controlled by this
* task.  Other tasks communicate with the timer service task using the
* xTimerQueue queue.
*/
static portTASK_FUNCTION_PROTO( TimerTask, pvParameters ) ;
/*
* Called by the timer service task to interpret and process a command it
* received on the timer queue.
*/
static void ProcessReceivedCommands( void ) ;
/*
* Insert the timer into either ActiveTimerList1, or ActiveTimerList2,
* depending on if the expire time causes a timer counter overflow.
*/
static BaseType_t InsertTimerInActiveList( Timer_t * const pxTimer,
                                                const TickType_t xNextExpiryTime,
                                                const TickType_t TimeNow,
                                                const TickType_t xCommandTime ) ;
/*
* Reload the specified auto-reload timer.  If the reloading is backlogged,
* clear the backlog, calling the callback for each additional reload.  When
* this function returns, the next expiry time is after TimeNow.
*/
static void ReloadTimer( Timer_t * const pxTimer,
                            TickType_t xExpiredTime,
                            const TickType_t TimeNow ) ;
/*
* An active timer has reached its expire time.  Reload the timer if it is an
* auto-reload timer, then call its callback.
*/
static void ProcessExpiredTimer( const TickType_t NextExpireTime,
                                    const TickType_t TimeNow ) ;
/*
* The tick count has overflowed.  Switch the timer lists after ensuring the
* current timer list does not still reference some timers.
*/
static void SwitchTimerLists( void ) ;
/*
* Obtain the current tick count, setting *TimerListsWereSwitched to true
* if a tick count overflow occurred since SampleTimeNow() was last called.
*/
static TickType_t SampleTimeNow( BaseType_t * const TimerListsWereSwitched ) ;
/*
* If the timer list contains any active timers then return the expire time of
* the timer that will expire first and set *ListWasEmpty to false.  If the
* timer list does not contain any timers then return 0 and set *ListWasEmpty
* to true.
*/
static TickType_t GetNextExpireTime( BaseType_t * const ListWasEmpty ) ;
/*
* If a timer has expired, process it.  Otherwise, block the timer service task
* until either a timer does expire or a command is received.
*/
static void ProcessTimerOrBlockTask( const TickType_t NextExpireTime,
                                        BaseType_t ListWasEmpty ) ;
/*
* Called after a Timer_t structure has been allocated either statically or
* dynamically to fill in the structure's members.
*/
static void InitialiseNewTimer( const char * const pcTimerName,
                                    const TickType_t xTimerPeriodInTicks,
                                    const BaseType_t xAutoReload,
                                    void * const pvTimerID,
                                    TimerCallbackFunction_t pxCallbackFunction,
                                    Timer_t * pxNewTimer ) ;

BaseType_t xTimerCreateTimerTask( void )
{
    BaseType_t Return = false;
    
    /* This function is called when the scheduler is started if
        * configUSE_TIMERS is set to 1.  Check that the infrastructure used by the
        * timer service task has been created/initialised.  If timers have already
        * been created then the initialisation will already have been performed. */
    CheckForValidListAndQueue();
    if( xTimerQueue != NULL )
    {
        StaticTask_t * pxTimerTaskTCBBuffer = NULL;
        StackType_t * pxTimerTaskStackBuffer = NULL;
        configSTACK_DEPTH_TYPE uxTimerTaskStackSize;
        vApplicationGetTimerTaskMemory( &pxTimerTaskTCBBuffer, &pxTimerTaskStackBuffer, &uxTimerTaskStackSize );
        xTimerTaskHandle = xTaskCreateStatic( TimerTask,
                                                configTIMER_SERVICE_TASK_NAME,
                                                uxTimerTaskStackSize,
                                                NULL,
                                                ( ( UBaseType_t ) configTIMER_TASK_PRIORITY ) | portPRIVILEGE_BIT,
                                                pxTimerTaskStackBuffer,
                                                pxTimerTaskTCBBuffer );
        if( xTimerTaskHandle != NULL )
        {
            Return = true;
        }
    }
    configASSERT( Return );
    return Return;
}

#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
    TimerHandle_t xTimerCreate( const char * const pcTimerName,
                                const TickType_t xTimerPeriodInTicks,
                                const BaseType_t xAutoReload,
                                void * const pvTimerID,
                                TimerCallbackFunction_t pxCallbackFunction )
    {
        Timer_t * pxNewTimer;
        


        pxNewTimer = ( Timer_t * ) pvPortMalloc( sizeof( Timer_t ) );
        if( pxNewTimer != NULL )
        {
            /* Status is thus far zero as the timer is not created statically
                * and has not been started.  The auto-reload bit may get set in
                * InitialiseNewTimer. */
            pxNewTimer->ucStatus = 0x00;
            InitialiseNewTimer( pcTimerName, xTimerPeriodInTicks, xAutoReload, pvTimerID, pxCallbackFunction, pxNewTimer );
        }
        
        return pxNewTimer;
    }
#endif /* configSUPPORT_DYNAMIC_ALLOCATION */

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
    TimerHandle_t xTimerCreateStatic( const char * const pcTimerName,
                                        const TickType_t xTimerPeriodInTicks,
                                        const BaseType_t xAutoReload,
                                        void * const pvTimerID,
                                        TimerCallbackFunction_t pxCallbackFunction,
                                        StaticTimer_t * pxTimerBuffer )
    {
        Timer_t * pxNewTimer;
        
        #if ( configASSERT_DEFINED == 1 )
        {
            /* Sanity check that the size of the structure used to declare a
                * variable of type StaticTimer_t equals the size of the real timer
                * structure. */
            volatile size_t xSize = sizeof( StaticTimer_t );
            configASSERT( xSize == sizeof( Timer_t ) );
            ( void ) xSize; /* Prevent unused variable warning when configASSERT() is not defined. */
        }
        #endif /* configASSERT_DEFINED */
        /* A pointer to a StaticTimer_t structure MUST be provided, use it. */
        configASSERT( pxTimerBuffer );

        pxNewTimer = ( Timer_t * ) pxTimerBuffer;
        if( pxNewTimer != NULL )
        {
            /* Timers can be created statically or dynamically so note this
                * timer was created statically in case it is later deleted.  The
                * auto-reload bit may get set in InitialiseNewTimer(). */
            pxNewTimer->ucStatus = ( uint8_t ) tmrSTATUS_IS_STATICALLY_ALLOCATED;
            InitialiseNewTimer( pcTimerName, xTimerPeriodInTicks, xAutoReload, pvTimerID, pxCallbackFunction, pxNewTimer );
        }
        
        return pxNewTimer;
    }
#endif /* configSUPPORT_STATIC_ALLOCATION */

static void InitialiseNewTimer( const char * const pcTimerName,
                                    const TickType_t xTimerPeriodInTicks,
                                    const BaseType_t xAutoReload,
                                    void * const pvTimerID,
                                    TimerCallbackFunction_t pxCallbackFunction,
                                    Timer_t * pxNewTimer )
{
    /* 0 is not a valid value for xTimerPeriodInTicks. */
    configASSERT( ( xTimerPeriodInTicks > 0 ) );
    /* Ensure the infrastructure used by the timer service task has been
        * created/initialised. */
    CheckForValidListAndQueue();
    /* Initialise the timer structure members using the function
        * parameters. */
    pxNewTimer->pcTimerName = pcTimerName;
    pxNewTimer->xTimerPeriodInTicks = xTimerPeriodInTicks;
    pxNewTimer->pvTimerID = pvTimerID;
    pxNewTimer->pxCallbackFunction = pxCallbackFunction;
    ListInitialiseItem( &( pxNewTimer->xTimerListItem ) );
    if( xAutoReload != false )
    {
        pxNewTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_AUTORELOAD;
    }
}

BaseType_t xTimerGenericCommandFromTask( TimerHandle_t xTimer,
                                            const BaseType_t xCommandID,
                                            const TickType_t xOptionalValue,
                                            BaseType_t * const pxHigherPriorityTaskWoken,
                                            const TickType_t xTicksToWait )
{
    BaseType_t Return = false;
    DaemonTaskMessage_t xMessage;
    ( void ) pxHigherPriorityTaskWoken;
    
    configASSERT( xTimer );
    /* Send a message to the timer service task to perform a particular action
        * on a particular timer definition. */
    if( xTimerQueue != NULL )
    {
        /* Send a command to the timer service task to start the xTimer timer. */
        xMessage.xMessageID = xCommandID;
        xMessage.u.xTimerParameters.xMessageValue = xOptionalValue;
        xMessage.u.xTimerParameters.pxTimer = xTimer;
        configASSERT( xCommandID < tmrFIRST_FROM_ISR_COMMAND );
        if( xCommandID < tmrFIRST_FROM_ISR_COMMAND )
        {
            if( xTaskGetSchedulerState() == taskSCHEDULER_RUNNING )
            {
                Return = xQueueSendToBack( xTimerQueue, &xMessage, xTicksToWait );
            }
            else
            {
                Return = xQueueSendToBack( xTimerQueue, &xMessage, tmrNO_DELAY );
            }
        }
    }

    
    return Return;
}

BaseType_t xTimerGenericCommandFromISR( TimerHandle_t xTimer,
                                        const BaseType_t xCommandID,
                                        const TickType_t xOptionalValue,
                                        BaseType_t * const pxHigherPriorityTaskWoken,
                                        const TickType_t xTicksToWait )
{
    BaseType_t Return = false;
    DaemonTaskMessage_t xMessage;
    ( void ) xTicksToWait;
    
    configASSERT( xTimer );
    /* Send a message to the timer service task to perform a particular action
        * on a particular timer definition. */
    if( xTimerQueue != NULL )
    {
        /* Send a command to the timer service task to start the xTimer timer. */
        xMessage.xMessageID = xCommandID;
        xMessage.u.xTimerParameters.xMessageValue = xOptionalValue;
        xMessage.u.xTimerParameters.pxTimer = xTimer;
        configASSERT( xCommandID >= tmrFIRST_FROM_ISR_COMMAND );
        if( xCommandID >= tmrFIRST_FROM_ISR_COMMAND )
        {
            Return = xQueueSendToBackFromISR( xTimerQueue, &xMessage, pxHigherPriorityTaskWoken );
        }
    }

    
    return Return;
}

TaskHandle_t xTimerGetTimerDaemonTaskHandle( void )
{
    
    /* If xTimerGetTimerDaemonTaskHandle() is called before the scheduler has been
        * started, then xTimerTaskHandle will be NULL. */
    configASSERT( ( xTimerTaskHandle != NULL ) );
    
    return xTimerTaskHandle;
}

TickType_t xTimerGetPeriod( TimerHandle_t xTimer )
{
    Timer_t * pxTimer = xTimer;
    
    configASSERT( xTimer );
    
    return pxTimer->xTimerPeriodInTicks;
}

void vTimerSetReloadMode( TimerHandle_t xTimer,
                            const BaseType_t xAutoReload )
{
    Timer_t * pxTimer = xTimer;
    
    configASSERT( xTimer );
    ENTER_CRITICAL();
    {
        if( xAutoReload != false )
        {
            pxTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_AUTORELOAD;
        } else {
            pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_AUTORELOAD );
        }
    }
    EXIT_CRITICAL();
    
}

BaseType_t xTimerGetReloadMode( TimerHandle_t xTimer )
{
    Timer_t * pxTimer = xTimer;
    BaseType_t Return;
    
    configASSERT( xTimer );
    ENTER_CRITICAL();
    {
        if( ( pxTimer->ucStatus & tmrSTATUS_IS_AUTORELOAD ) == 0U )
        {
            /* Not an auto-reload timer. */
            Return = false;
        } else {
            /* Is an auto-reload timer. */
            Return = true;
        }
    }
    EXIT_CRITICAL();
    
    return Return;
}
UBaseType_t uxTimerGetReloadMode( TimerHandle_t xTimer )
{
    UBaseType_t uReturn;
    
    uReturn = ( UBaseType_t ) xTimerGetReloadMode( xTimer );
    
    return uReturn;
}

TickType_t xTimerGetExpiryTime( TimerHandle_t xTimer )
{
    Timer_t * pxTimer = xTimer;
    TickType_t Return;
    
    configASSERT( xTimer );
    Return = GET_LIST_ITEM_VALUE( &( pxTimer->xTimerListItem ) );
    
    return Return;
}

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
    BaseType_t xTimerGetStaticBuffer( TimerHandle_t xTimer,
                                        StaticTimer_t ** ppxTimerBuffer )
    {
        BaseType_t Return;
        Timer_t * pxTimer = xTimer;
        
        configASSERT( ppxTimerBuffer != NULL );
        if( ( pxTimer->ucStatus & tmrSTATUS_IS_STATICALLY_ALLOCATED ) != 0U )
        {

            *ppxTimerBuffer = ( StaticTimer_t * ) pxTimer;
            Return = true;
        } else {
            Return = false;
        }
        
        return Return;
    }
#endif /* configSUPPORT_STATIC_ALLOCATION */

const char * pcTimerGetName( TimerHandle_t xTimer )
{
    Timer_t * pxTimer = xTimer;
    
    configASSERT( xTimer );
    
    return pxTimer->pcTimerName;
}

static void ReloadTimer( Timer_t * const pxTimer,
                            TickType_t xExpiredTime,
                            const TickType_t TimeNow )
{
    /* Insert the timer into the appropriate list for the next expiry time.
        * If the next expiry time has already passed, advance the expiry time,
        * call the callback function, and try again. */
    while( InsertTimerInActiveList( pxTimer, ( xExpiredTime + pxTimer->xTimerPeriodInTicks ), TimeNow, xExpiredTime ) != false )
    {
        /* Advance the expiry time. */
        xExpiredTime += pxTimer->xTimerPeriodInTicks;
        /* Call the timer callback. */
        pxTimer->pxCallbackFunction( ( TimerHandle_t ) pxTimer );
    }
}

static void ProcessExpiredTimer( const TickType_t NextExpireTime,
                                    const TickType_t TimeNow )
{

    Timer_t * const pxTimer = ( Timer_t * ) GET_OWNER_OF_HEAD_ENTRY( CurrentTimerList );
    /* Remove the timer from the list of active timers.  A check has already
        * been performed to ensure the list is not empty. */
    ( void ) ListRemove( &( pxTimer->xTimerListItem ) );
    /* If the timer is an auto-reload timer then calculate the next
        * expiry time and re-insert the timer in the list of active timers. */
    if( ( pxTimer->ucStatus & tmrSTATUS_IS_AUTORELOAD ) != 0U )
    {
        ReloadTimer( pxTimer, NextExpireTime, TimeNow );
    }
    else
    {
        pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
    }
    /* Call the timer callback. */
    pxTimer->pxCallbackFunction( ( TimerHandle_t ) pxTimer );
}

static portTASK_FUNCTION( TimerTask, pvParameters )
{
    TickType_t NextExpireTime;
    BaseType_t ListWasEmpty;
    /* Just to avoid compiler warnings. */
    ( void ) pvParameters;
    for( ; configCONTROL_INFINITE_LOOP(); )
    {
        /* Query the timers list to see if it contains any timers, and if so,
            * obtain the time at which the next timer will expire. */
        NextExpireTime = GetNextExpireTime( &ListWasEmpty );
        /* If a timer has expired, process it.  Otherwise, block this task
            * until either a timer does expire, or a command is received. */
        ProcessTimerOrBlockTask( NextExpireTime, ListWasEmpty );
        /* Empty the command queue. */
        ProcessReceivedCommands();
    }
}

static void ProcessTimerOrBlockTask( const TickType_t NextExpireTime,
                                        BaseType_t ListWasEmpty )
{
    TickType_t TimeNow;
    BaseType_t TimerListsWereSwitched;
    vTaskSuspendAll();
    {
        /* Obtain the time now to make an assessment as to whether the timer
            * has expired or not.  If obtaining the time causes the lists to switch
            * then don't process this timer as any timers that remained in the list
            * when the lists were switched will have been processed within the
            * SampleTimeNow() function. */
        TimeNow = SampleTimeNow( &TimerListsWereSwitched );
        if( TimerListsWereSwitched == false )
        {
            /* The tick count has not overflowed, has the timer expired? */
            if( ( ListWasEmpty == false ) && ( NextExpireTime <= TimeNow ) )
            {
                ( void ) TaskResumeAll();
                ProcessExpiredTimer( NextExpireTime, TimeNow );
            }
            else
            {
                /* The tick count has not overflowed, and the next expire
                    * time has not been reached yet.  This task should therefore
                    * block to wait for the next expire time or a command to be
                    * received - whichever comes first.  The following line cannot
                    * be reached unless NextExpireTime > TimeNow, except in the
                    * case when the current timer list is empty. */
                ListWasEmpty |= LIST_IS_EMPTY( OverflowTimerList );
                vQueueWaitForMessageRestricted( xTimerQueue, ( NextExpireTime - TimeNow ), ListWasEmpty );
                if( TaskResumeAll() == false )
                {
                    /* Yield to wait for either a command to arrive, or the
                        * block time to expire.  If a command arrived between the
                        * critical section being exited and this yield then the yield
                        * will not cause the task to block. */
                    taskYIELD_WITHIN_API();
                }
            }
        } else {
            ( void ) TaskResumeAll();
        }
    }
}

static TickType_t GetNextExpireTime( BaseType_t * const ListWasEmpty )
{
    TickType_t NextExpireTime;
    /* Timers are listed in expiry time order, with the head of the list
        * referencing the task that will expire first.  Obtain the time at which
        * the timer with the nearest expiry time will expire.  If there are no
        * active timers then just set the next expire time to 0.  That will cause
        * this task to unblock when the tick count overflows, at which point the
        * timer lists will be switched and the next expiry time can be
        * re-assessed.  */
    *ListWasEmpty = LIST_IS_EMPTY( CurrentTimerList );
    if( !*ListWasEmpty )
    {
        NextExpireTime = GET_ITEM_VALUE_OF_HEAD_ENTRY( CurrentTimerList );
    }
    else
    {
        /* Ensure the task unblocks when the tick count rolls over. */
        NextExpireTime = ( TickType_t ) 0U;
    }
    return NextExpireTime;
}

static TickType_t SampleTimeNow( BaseType_t * const TimerListsWereSwitched )
{
    static TickType_t xLastTime = ( TickType_t ) 0U;
    TickType_t TimeNow = GetTickCount();
    if( TimeNow < xLastTime )
    {
        SwitchTimerLists();
        *TimerListsWereSwitched = true;
    }
    else
    {
        *TimerListsWereSwitched = false;
    }
    xLastTime = TimeNow;
    return TimeNow;
}

static BaseType_t InsertTimerInActiveList( Timer_t * const pxTimer,
                                                const TickType_t xNextExpiryTime,
                                                const TickType_t TimeNow,
                                                const TickType_t xCommandTime )
{
    BaseType_t xProcessTimerNow = false;
    SET_LIST_ITEM_VALUE( &( pxTimer->xTimerListItem ), xNextExpiryTime );
    SET_LIST_ITEM_OWNER( &( pxTimer->xTimerListItem ), pxTimer );
    if( xNextExpiryTime <= TimeNow )
    {
        /* Has the expiry time elapsed between the command to start/reset a
            * timer was issued, and the time the command was processed? */
        if( ( ( TickType_t ) ( TimeNow - xCommandTime ) ) >= pxTimer->xTimerPeriodInTicks )
        {
            /* The time between a command being issued and the command being
                * processed actually exceeds the timers period.  */
            xProcessTimerNow = true;
        } else {
            ListInsert( OverflowTimerList, &( pxTimer->xTimerListItem ) );
        }
    } else {
        if( ( TimeNow < xCommandTime ) && ( xNextExpiryTime >= xCommandTime ) ) {
            /* If, since the command was issued, the tick count has overflowed
                * but the expiry time has not, then the timer must have already passed
                * its expiry time and should be processed immediately. */
            xProcessTimerNow = true;
        } else {
            ListInsert( CurrentTimerList, &( pxTimer->xTimerListItem ) );
        }
    }
    return xProcessTimerNow;
}

static void ProcessReceivedCommands( void )
{
    DaemonTaskMessage_t xMessage = { 0 };
    Timer_t * pxTimer;
    BaseType_t TimerListsWereSwitched;
    TickType_t TimeNow;
    while( xQueueReceive( xTimerQueue, &xMessage, tmrNO_DELAY ) != false )
    {
        #if ( INCLUDE_xTimerPendFunctionCall == 1 )
        {
            /* Negative commands are pended function calls rather than timer
                * commands. */
            if( xMessage.xMessageID < ( BaseType_t ) 0 )
            {
                const CallbackParameters_t * const pxCallback = &( xMessage.u.xCallbackParameters );
                /* The timer uses the xCallbackParameters member to request a
                    * callback be executed.  Check the callback is not NULL. */
                configASSERT( pxCallback );
                /* Call the function. */
                pxCallback->pxCallbackFunction( pxCallback->pvParameter1, pxCallback->ulParameter2 );
            }
        }
        #endif /* INCLUDE_xTimerPendFunctionCall */
        /* Commands that are positive are timer commands rather than pended
            * function calls. */
        if( xMessage.xMessageID >= ( BaseType_t ) 0 )
        {
            /* The messages uses the xTimerParameters member to work on a
                * software timer. */
            pxTimer = xMessage.u.xTimerParameters.pxTimer;
            if( IS_CONTAINED_WITHIN( NULL, &( pxTimer->xTimerListItem ) ) == false )
            {
                /* The timer is in a list, remove it. */
                ( void ) ListRemove( &( pxTimer->xTimerListItem ) );
            }
            /* In this case the TimerListsWereSwitched parameter is not used, but
                *  it must be present in the function call.  SampleTimeNow() must be
                *  called after the message is received from xTimerQueue so there is no
                *  possibility of a higher priority task adding a message to the message
                *  queue with a time that is ahead of the timer daemon task (because it
                *  pre-empted the timer daemon task after the TimeNow value was set). */
            TimeNow = SampleTimeNow( &TimerListsWereSwitched );
            switch( xMessage.xMessageID )
            {
                case tmrCOMMAND_START:
                case tmrCOMMAND_START_FROM_ISR:
                case tmrCOMMAND_RESET:
                case tmrCOMMAND_RESET_FROM_ISR:
                    /* Start or restart a timer. */
                    pxTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_ACTIVE;
                    if( InsertTimerInActiveList( pxTimer, xMessage.u.xTimerParameters.xMessageValue + pxTimer->xTimerPeriodInTicks, TimeNow, xMessage.u.xTimerParameters.xMessageValue ) != false )
                    {
                        /* The timer expired before it was added to the active
                            * timer list.  Process it now. */
                        if( ( pxTimer->ucStatus & tmrSTATUS_IS_AUTORELOAD ) != 0U )
                        {
                            ReloadTimer( pxTimer, xMessage.u.xTimerParameters.xMessageValue + pxTimer->xTimerPeriodInTicks, TimeNow );
                        }
                        else
                        {
                            pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
                        }
                        /* Call the timer callback. */
                        pxTimer->pxCallbackFunction( ( TimerHandle_t ) pxTimer );
                    }
                    break;
                case tmrCOMMAND_STOP:
                case tmrCOMMAND_STOP_FROM_ISR:
                    /* The timer has already been removed from the active list. */
                    pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
                    break;
                case tmrCOMMAND_CHANGE_PERIOD:
                case tmrCOMMAND_CHANGE_PERIOD_FROM_ISR:
                    pxTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_ACTIVE;
                    pxTimer->xTimerPeriodInTicks = xMessage.u.xTimerParameters.xMessageValue;
                    configASSERT( ( pxTimer->xTimerPeriodInTicks > 0 ) );
                    /* The new period does not really have a reference, and can
                        * be longer or shorter than the old one.  The command time is
                        * therefore set to the current time, and as the period cannot
                        * be zero the next expiry time can only be in the future,
                        * meaning (unlike for the xTimerStart() case above) there is
                        * no fail case that needs to be handled here. */
                    ( void ) InsertTimerInActiveList( pxTimer, ( TimeNow + pxTimer->xTimerPeriodInTicks ), TimeNow, TimeNow );
                    break;
                case tmrCOMMAND_DELETE:
                    #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
                    {
                        /* The timer has already been removed from the active list,
                            * just free up the memory if the memory was dynamically
                            * allocated. */
                        if( ( pxTimer->ucStatus & tmrSTATUS_IS_STATICALLY_ALLOCATED ) == ( uint8_t ) 0 )
                        {
                            vPortFree( pxTimer );
                        }
                        else
                        {
                            pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
                        }
                    }
                    #else /* if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) */
                    {
                        /* If dynamic allocation is not enabled, the memory
                            * could not have been dynamically allocated. So there is
                            * no need to free the memory - just mark the timer as
                            * "not active". */
                        pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
                    }
                    #endif /* configSUPPORT_DYNAMIC_ALLOCATION */
                    break;
                default:
                    /* Don't expect to get here. */
                    break;
            }
        }
    }
}

static void SwitchTimerLists( void )
{
    TickType_t NextExpireTime;
    List_t * Temp;
    while( LIST_IS_EMPTY( CurrentTimerList ) == false ) {
        NextExpireTime = GET_ITEM_VALUE_OF_HEAD_ENTRY( CurrentTimerList );
        ProcessExpiredTimer( NextExpireTime, tmrMAX_TIME_BEFORE_OVERFLOW );
    }
    Temp = CurrentTimerList;
    CurrentTimerList = OverflowTimerList;
    OverflowTimerList = Temp;
}

static void CheckForValidListAndQueue( void )
{
    /* Check that the list from which active timers are referenced, and the
        * queue used to communicate with the timer service, have been
        * initialised. */
    ENTER_CRITICAL();
    if( xTimerQueue == NULL )
    {
        ListInitialise( &ActiveTimerList1 );
        ListInitialise( &ActiveTimerList2 );
        CurrentTimerList = &ActiveTimerList1;
        OverflowTimerList = &ActiveTimerList2;
        static StaticQueue_t xStaticTimerQueue;
        static uint8_t ucStaticTimerQueueStorage[ ( size_t ) configTIMER_QUEUE_LENGTH * sizeof( DaemonTaskMessage_t ) ];
        xTimerQueue = xQueueCreateStatic( ( UBaseType_t ) configTIMER_QUEUE_LENGTH, ( UBaseType_t ) sizeof( DaemonTaskMessage_t ), &( ucStaticTimerQueueStorage[ 0 ] ), &xStaticTimerQueue );

    }
    EXIT_CRITICAL();
}

BaseType_t xTimerIsTimerActive( TimerHandle_t xTimer )
{
    BaseType_t Return;
    Timer_t * pxTimer = xTimer;
    
    configASSERT( xTimer );
    /* Is the timer in the list of active timers? */
    ENTER_CRITICAL();
    {
        if( ( pxTimer->ucStatus & tmrSTATUS_IS_ACTIVE ) == 0U )
        {
            Return = false;
        } else {
            Return = true;
        }
    }
    EXIT_CRITICAL();
    
    return Return;
}

void * pvTimerGetTimerID( const TimerHandle_t xTimer )
{
    Timer_t * const pxTimer = xTimer;
    void * pvReturn;
    
    configASSERT( xTimer );
    ENTER_CRITICAL();
    {
        pvReturn = pxTimer->pvTimerID;
    }
    EXIT_CRITICAL();
    
    return pvReturn;
}

void vTimerSetTimerID( TimerHandle_t xTimer,
                        void * pvNewID )
{
    Timer_t * const pxTimer = xTimer;
    
    configASSERT( xTimer );
    ENTER_CRITICAL();
    {
        pxTimer->pvTimerID = pvNewID;
    }
    EXIT_CRITICAL();
    
}

#if ( INCLUDE_xTimerPendFunctionCall == 1 )
    BaseType_t xTimerPendFunctionCallFromISR( PendedFunction_t xFunctionToPend,
                                                void * pvParameter1,
                                                uint32_t ulParameter2,
                                                BaseType_t * pxHigherPriorityTaskWoken )
    {
        DaemonTaskMessage_t xMessage;
        
        /* Complete the message with the function parameters and post it to the
            * daemon task. */
        xMessage.xMessageID = tmrCOMMAND_EXECUTE_CALLBACK_FROM_ISR;
        xMessage.u.xCallbackParameters.pxCallbackFunction = xFunctionToPend;
        xMessage.u.xCallbackParameters.pvParameter1 = pvParameter1;
        xMessage.u.xCallbackParameters.ulParameter2 = ulParameter2;
        return xQueueSendFromISR( xTimerQueue, &xMessage, pxHigherPriorityTaskWoken );
    }
#endif /* INCLUDE_xTimerPendFunctionCall */

#if ( INCLUDE_xTimerPendFunctionCall == 1 )
    BaseType_t xTimerPendFunctionCall( PendedFunction_t xFunctionToPend,
                                        void * pvParameter1,
                                        uint32_t ulParameter2,
                                        TickType_t xTicksToWait )
    {
        DaemonTaskMessage_t xMessage;
        BaseType_t Return;
        
        /* This function can only be called after a timer has been created or
            * after the scheduler has been started because, until then, the timer
            * queue does not exist. */
        configASSERT( xTimerQueue );
        /* Complete the message with the function parameters and post it to the
            * daemon task. */
        xMessage.xMessageID = tmrCOMMAND_EXECUTE_CALLBACK;
        xMessage.u.xCallbackParameters.pxCallbackFunction = xFunctionToPend;
        xMessage.u.xCallbackParameters.pvParameter1 = pvParameter1;
        xMessage.u.xCallbackParameters.ulParameter2 = ulParameter2;
        Return = xQueueSendToBack( xTimerQueue, &xMessage, xTicksToWait );
        return Return;
    }
#endif /* INCLUDE_xTimerPendFunctionCall */

/*
 * Reset the state in this file. This state is normally initialized at start up.
 * This function must be called by the application before restarting the
 * scheduler.
 */
void vTimerResetState( void )
{
    xTimerQueue = NULL;
    xTimerTaskHandle = NULL;
}

#endif /* configUSE_TIMERS == 1 */
