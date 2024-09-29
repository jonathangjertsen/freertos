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
#include "FreeRTOS.h"
#include "task.hpp"
#include "queue.h"
#include "timers.h"


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
struct Timer_t                                               /* The old naming convention is used to prevent breaking kernel aware debuggers. */
{
    const char * pcTimerName;                                                /**< Text name.  This is not used by the kernel, it is included simply to make debugging easier. */
    Item_t<Timer_t> TimerListItem;                                               /**< Standard linked list item as used by all kernel features for event management. */
    TickType_t TimerPeriodInTicks;                                          /**< How quickly and often the timer expires. */
    void * pvTimerID;                                                        /**< An ID to identify the timer.  This allows the timer to be identified when the same callback is used for multiple timers. */
    portTIMER_CALLBACK_ATTRIBUTE TimerCallbackFunction_t pxCallbackFunction; /**< The function that will be called when the timer expires. */
    uint8_t ucStatus;                                                        /**< Holds bits to say if the timer was statically allocated or not, and if it is active or not. */
};

/* The definition of messages that can be sent and received on the timer queue.
* Two types of message can be queued - messages that manipulate a software timer,
* and messages that request the execution of a non-timer related callback.  The
* two message types are defined in two separate structures, TimerParametersType
* and xCallbackParametersType respectively. */
typedef struct tmrTimerParameters
{
    TickType_t xMessageValue; /**< An optional value used by a subset of commands, for example, when changing the period of a timer. */
    Timer_t * pTimer;        /**< The timer to which the command will be applied. */
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
        TimerParameter_t TimerParameters;
        /* Don't include xCallbackParameters if it is not going to be used as
            * it makes the structure (and therefore the timer queue) larger. */
        #if ( INCLUDE_TimerPendFunctionCall == 1 )
            CallbackParameters_t xCallbackParameters;
        #endif /* INCLUDE_TimerPendFunctionCall */
    } u;
} DaemonTaskMessage_t;
/* The list in which active timers are stored.  Timers are referenced in expire
* time order, with the nearest expiry time at the front of the list.  Only the
* timer service task is allowed to access these lists.
* ActiveTimerList1 and ActiveTimerList2 could be at function scope but that
* breaks some kernel aware debuggers, and debuggers that reply on removing the
* static qualifier. */
 static List_t<Timer_t> ActiveTimerList1;
 static List_t<Timer_t> ActiveTimerList2;
 static List_t<Timer_t> *CurrentTimerList;
 static List_t<Timer_t> *OverflowTimerList;
/* A queue that is used to send commands to the timer service task. */
 static QueueHandle_t TimerQueue = NULL;
 static TaskHandle_t TimerTaskHandle = NULL;

/*
* Initialise the infrastructure used by the timer service task if it has not
* been initialised already.
*/
static void CheckForValidListAndQueue( void ) ;
/*
* The timer service task (daemon).  Timer functionality is controlled by this
* task.  Other tasks communicate with the timer service task using the
* TimerQueue queue.
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
static BaseType_t InsertTimerInActiveList( Timer_t * const pTimer,
                                                const TickType_t xNextExpiryTime,
                                                const TickType_t TimeNow,
                                                const TickType_t xCommandTime ) ;
/*
* Reload the specified auto-reload timer.  If the reloading is backlogged,
* clear the backlog, calling the callback for each additional reload.  When
* this function returns, the next expiry time is after TimeNow.
*/
static void ReloadTimer( Timer_t * const pTimer,
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
                                    const TickType_t TimerPeriodInTicks,
                                    const BaseType_t xAutoReload,
                                    void * const pvTimerID,
                                    TimerCallbackFunction_t pxCallbackFunction,
                                    Timer_t * pxNewTimer ) ;

BaseType_t TimerCreateTimerTask( void )
{
    BaseType_t Return = false;
    
    /* This function is called when the scheduler is started if
        * configUSE_TIMERS is set to 1.  Check that the infrastructure used by the
        * timer service task has been created/initialised.  If timers have already
        * been created then the initialisation will already have been performed. */
    CheckForValidListAndQueue();
    if( TimerQueue != NULL )
    {
        StaticTask_t * pTimerTaskTCBBuffer = NULL;
        StackType_t * pTimerTaskStackBuffer = NULL;
        configSTACK_DEPTH_TYPE uTimerTaskStackSize;
        vApplicationGetTimerTaskMemory( &pTimerTaskTCBBuffer, &pTimerTaskStackBuffer, &uTimerTaskStackSize );
        TimerTaskHandle = xTaskCreateStatic( TimerTask,
                                                configTIMER_SERVICE_TASK_NAME,
                                                uTimerTaskStackSize,
                                                NULL,
                                                ( ( UBaseType_t ) configTIMER_TASK_PRIORITY ) | portPRIVILEGE_BIT,
                                                pTimerTaskStackBuffer,
                                                pTimerTaskTCBBuffer );
        if( TimerTaskHandle != NULL )
        {
            Return = true;
        }
    }
    configASSERT( Return );
    return Return;
}

#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
    TimerHandle_t TimerCreate( const char * const pcTimerName,
                                const TickType_t TimerPeriodInTicks,
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
            InitialiseNewTimer( pcTimerName, TimerPeriodInTicks, xAutoReload, pvTimerID, pxCallbackFunction, pxNewTimer );
        }
        
        return pxNewTimer;
    }
#endif /* configSUPPORT_DYNAMIC_ALLOCATION */

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
    TimerHandle_t TimerCreateStatic( const char * const pcTimerName,
                                        const TickType_t TimerPeriodInTicks,
                                        const BaseType_t xAutoReload,
                                        void * const pvTimerID,
                                        TimerCallbackFunction_t pxCallbackFunction,
                                        StaticTimer_t * pTimerBuffer )
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
        configASSERT( pTimerBuffer );

        pxNewTimer = ( Timer_t * ) pTimerBuffer;
        if( pxNewTimer != NULL )
        {
            /* Timers can be created statically or dynamically so note this
                * timer was created statically in case it is later deleted.  The
                * auto-reload bit may get set in InitialiseNewTimer(). */
            pxNewTimer->ucStatus = ( uint8_t ) tmrSTATUS_IS_STATICALLY_ALLOCATED;
            InitialiseNewTimer( pcTimerName, TimerPeriodInTicks, xAutoReload, pvTimerID, pxCallbackFunction, pxNewTimer );
        }
        
        return pxNewTimer;
    }
#endif /* configSUPPORT_STATIC_ALLOCATION */

static void InitialiseNewTimer( const char * const pcTimerName,
                                    const TickType_t TimerPeriodInTicks,
                                    const BaseType_t xAutoReload,
                                    void * const pvTimerID,
                                    TimerCallbackFunction_t pxCallbackFunction,
                                    Timer_t * pxNewTimer )
{
    configASSERT( ( TimerPeriodInTicks > 0 ) );
    CheckForValidListAndQueue();
    pxNewTimer->pcTimerName = pcTimerName;
    pxNewTimer->TimerPeriodInTicks = TimerPeriodInTicks;
    pxNewTimer->pvTimerID = pvTimerID;
    pxNewTimer->pxCallbackFunction = pxCallbackFunction;
    pxNewTimer->TimerListItem.init();
    if( xAutoReload != false )
    {
        pxNewTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_AUTORELOAD;
    }
}

BaseType_t TimerGenericCommandFromTask( TimerHandle_t Timer,
                                            const BaseType_t xCommandID,
                                            const TickType_t xOptionalValue,
                                            BaseType_t * const pxHigherPriorityTaskWoken,
                                            const TickType_t xTicksToWait )
{
    BaseType_t Return = false;
    DaemonTaskMessage_t xMessage;
    ( void ) pxHigherPriorityTaskWoken;
    
    configASSERT( Timer );
    /* Send a message to the timer service task to perform a particular action
        * on a particular timer definition. */
    if( TimerQueue != NULL )
    {
        /* Send a command to the timer service task to start the Timer timer. */
        xMessage.xMessageID = xCommandID;
        xMessage.u.TimerParameters.xMessageValue = xOptionalValue;
        xMessage.u.TimerParameters.pTimer = Timer;
        configASSERT( xCommandID < tmrFIRST_FROM_ISR_COMMAND );
        if( xCommandID < tmrFIRST_FROM_ISR_COMMAND )
        {
            if( xTaskGetSchedulerState() == taskSCHEDULER_RUNNING )
            {
                Return = xQueueSendToBack( TimerQueue, &xMessage, xTicksToWait );
            }
            else
            {
                Return = xQueueSendToBack( TimerQueue, &xMessage, tmrNO_DELAY );
            }
        }
    }

    
    return Return;
}

BaseType_t TimerGenericCommandFromISR( TimerHandle_t Timer,
                                        const BaseType_t xCommandID,
                                        const TickType_t xOptionalValue,
                                        BaseType_t * const pxHigherPriorityTaskWoken,
                                        const TickType_t xTicksToWait )
{
    BaseType_t Return = false;
    DaemonTaskMessage_t xMessage;
    ( void ) xTicksToWait;
    
    configASSERT( Timer );
    /* Send a message to the timer service task to perform a particular action
        * on a particular timer definition. */
    if( TimerQueue != NULL )
    {
        /* Send a command to the timer service task to start the Timer timer. */
        xMessage.xMessageID = xCommandID;
        xMessage.u.TimerParameters.xMessageValue = xOptionalValue;
        xMessage.u.TimerParameters.pTimer = Timer;
        configASSERT( xCommandID >= tmrFIRST_FROM_ISR_COMMAND );
        if( xCommandID >= tmrFIRST_FROM_ISR_COMMAND )
        {
            Return = xQueueSendToBackFromISR( TimerQueue, &xMessage, pxHigherPriorityTaskWoken );
        }
    }

    
    return Return;
}

TaskHandle_t TimerGetTimerDaemonTaskHandle( void )
{
    
    /* If TimerGetTimerDaemonTaskHandle() is called before the scheduler has been
        * started, then TimerTaskHandle will be NULL. */
    configASSERT( ( TimerTaskHandle != NULL ) );
    
    return TimerTaskHandle;
}

TickType_t TimerGetPeriod( TimerHandle_t Timer )
{
    Timer_t * pTimer = Timer;
    
    configASSERT( Timer );
    
    return pTimer->TimerPeriodInTicks;
}

void vTimerSetReloadMode( TimerHandle_t Timer,
                            const BaseType_t xAutoReload )
{
    Timer_t * pTimer = Timer;
    
    configASSERT( Timer );
    ENTER_CRITICAL();
    {
        if( xAutoReload != false )
        {
            pTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_AUTORELOAD;
        } else {
            pTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_AUTORELOAD );
        }
    }
    EXIT_CRITICAL();
    
}

BaseType_t TimerGetReloadMode( TimerHandle_t Timer )
{
    Timer_t * pTimer = Timer;
    BaseType_t Return;
    
    configASSERT( Timer );
    ENTER_CRITICAL();
    {
        if( ( pTimer->ucStatus & tmrSTATUS_IS_AUTORELOAD ) == 0U )
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
UBaseType_t uTimerGetReloadMode( TimerHandle_t Timer )
{
    UBaseType_t uReturn;
    
    uReturn = ( UBaseType_t ) TimerGetReloadMode( Timer );
    
    return uReturn;
}

TickType_t TimerGetExpiryTime( TimerHandle_t Timer )
{
    Timer_t * pTimer = Timer;
    TickType_t Return;
    
    configASSERT( Timer );
    return pTimer->TimerListItem.Value;
}

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
    BaseType_t TimerGetStaticBuffer( TimerHandle_t Timer,
                                        StaticTimer_t ** ppTimerBuffer )
    {
        BaseType_t Return;
        Timer_t * pTimer = Timer;
        
        configASSERT( ppTimerBuffer != NULL );
        if( ( pTimer->ucStatus & tmrSTATUS_IS_STATICALLY_ALLOCATED ) != 0U )
        {

            *ppTimerBuffer = ( StaticTimer_t * ) pTimer;
            Return = true;
        } else {
            Return = false;
        }
        
        return Return;
    }
#endif /* configSUPPORT_STATIC_ALLOCATION */

const char * pcTimerGetName( TimerHandle_t Timer )
{
    Timer_t * pTimer = Timer;
    
    configASSERT( Timer );
    
    return pTimer->pcTimerName;
}

static void ReloadTimer( Timer_t * const pTimer,
                            TickType_t xExpiredTime,
                            const TickType_t TimeNow )
{
    /* Insert the timer into the appropriate list for the next expiry time.
        * If the next expiry time has already passed, advance the expiry time,
        * call the callback function, and try again. */
    while( InsertTimerInActiveList( pTimer, ( xExpiredTime + pTimer->TimerPeriodInTicks ), TimeNow, xExpiredTime ) != false )
    {
        /* Advance the expiry time. */
        xExpiredTime += pTimer->TimerPeriodInTicks;
        /* Call the timer callback. */
        pTimer->pxCallbackFunction( ( TimerHandle_t ) pTimer );
    }
}

static void ProcessExpiredTimer( const TickType_t NextExpireTime, const TickType_t TimeNow )
{

    Timer_t * const pTimer = CurrentTimerList->head()->Owner;
    pTimer->TimerListItem.remove();
    if( ( pTimer->ucStatus & tmrSTATUS_IS_AUTORELOAD ) != 0U )
    {
        ReloadTimer( pTimer, NextExpireTime, TimeNow );
    }
    else
    {
        pTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
    }
    /* Call the timer callback. */
    pTimer->pxCallbackFunction( ( TimerHandle_t ) pTimer );
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
                ListWasEmpty |= OverflowTimerList->empty();
                vQueueWaitForMessageRestricted( TimerQueue, ( NextExpireTime - TimeNow ), ListWasEmpty );
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
    *ListWasEmpty = CurrentTimerList->empty();
    if( !*ListWasEmpty )
    {
        NextExpireTime = CurrentTimerList->head()->Value;
    }
    else
    {
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

static BaseType_t InsertTimerInActiveList( Timer_t * const pTimer,
                                                const TickType_t xNextExpiryTime,
                                                const TickType_t TimeNow,
                                                const TickType_t xCommandTime )
{
    BaseType_t xProcessTimerNow = false;
    pTimer->TimerListItem.Value = xNextExpiryTime;
    pTimer->TimerListItem.Owner = pTimer;
    if( xNextExpiryTime <= TimeNow )
    {
        /* Has the expiry time elapsed between the command to start/reset a
            * timer was issued, and the time the command was processed? */
        if( ( ( TickType_t ) ( TimeNow - xCommandTime ) ) >= pTimer->TimerPeriodInTicks )
        {
            /* The time between a command being issued and the command being
                * processed actually exceeds the timers period.  */
            xProcessTimerNow = true;
        } else {
            OverflowTimerList->insert(&( pTimer->TimerListItem ) );
        }
    } else {
        if( ( TimeNow < xCommandTime ) && ( xNextExpiryTime >= xCommandTime ) ) {
            /* If, since the command was issued, the tick count has overflowed
                * but the expiry time has not, then the timer must have already passed
                * its expiry time and should be processed immediately. */
            xProcessTimerNow = true;
        } else {
            CurrentTimerList->insert(&( pTimer->TimerListItem ) );
        }
    }
    return xProcessTimerNow;
}

static void ProcessReceivedCommands( void )
{
    DaemonTaskMessage_t xMessage = { 0 };
    Timer_t * pTimer;
    BaseType_t TimerListsWereSwitched;
    TickType_t TimeNow;
    while( xQueueReceive( TimerQueue, &xMessage, tmrNO_DELAY ) != false )
    {
        #if ( INCLUDE_TimerPendFunctionCall == 1 )
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
        #endif /* INCLUDE_TimerPendFunctionCall */
        if( xMessage.xMessageID >= ( BaseType_t ) 0 )
        {
            pTimer = xMessage.u.TimerParameters.pTimer;
            if(pTimer->TimerListItem.Container != nullptr)
            {
                pTimer->TimerListItem.remove();
            }
            TimeNow = SampleTimeNow( &TimerListsWereSwitched );
            switch( xMessage.xMessageID )
            {
                case tmrCOMMAND_START:
                case tmrCOMMAND_START_FROM_ISR:
                case tmrCOMMAND_RESET:
                case tmrCOMMAND_RESET_FROM_ISR:
                    /* Start or restart a timer. */
                    pTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_ACTIVE;
                    if( InsertTimerInActiveList( pTimer, xMessage.u.TimerParameters.xMessageValue + pTimer->TimerPeriodInTicks, TimeNow, xMessage.u.TimerParameters.xMessageValue ) != false )
                    {
                        /* The timer expired before it was added to the active
                            * timer list.  Process it now. */
                        if( ( pTimer->ucStatus & tmrSTATUS_IS_AUTORELOAD ) != 0U )
                        {
                            ReloadTimer( pTimer, xMessage.u.TimerParameters.xMessageValue + pTimer->TimerPeriodInTicks, TimeNow );
                        }
                        else
                        {
                            pTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
                        }
                        /* Call the timer callback. */
                        pTimer->pxCallbackFunction( ( TimerHandle_t ) pTimer );
                    }
                    break;
                case tmrCOMMAND_STOP:
                case tmrCOMMAND_STOP_FROM_ISR:
                    /* The timer has already been removed from the active list. */
                    pTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
                    break;
                case tmrCOMMAND_CHANGE_PERIOD:
                case tmrCOMMAND_CHANGE_PERIOD_FROM_ISR:
                    pTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_ACTIVE;
                    pTimer->TimerPeriodInTicks = xMessage.u.TimerParameters.xMessageValue;
                    configASSERT( ( pTimer->TimerPeriodInTicks > 0 ) );
                    /* The new period does not really have a reference, and can
                        * be longer or shorter than the old one.  The command time is
                        * therefore set to the current time, and as the period cannot
                        * be zero the next expiry time can only be in the future,
                        * meaning (unlike for the TimerStart() case above) there is
                        * no fail case that needs to be handled here. */
                    ( void ) InsertTimerInActiveList( pTimer, ( TimeNow + pTimer->TimerPeriodInTicks ), TimeNow, TimeNow );
                    break;
                case tmrCOMMAND_DELETE:
                    #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
                    {
                        /* The timer has already been removed from the active list,
                            * just free up the memory if the memory was dynamically
                            * allocated. */
                        if( ( pTimer->ucStatus & tmrSTATUS_IS_STATICALLY_ALLOCATED ) == ( uint8_t ) 0 )
                        {
                            vPortFree( pTimer );
                        }
                        else
                        {
                            pTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
                        }
                    }
                    #else /* if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) */
                    {
                        /* If dynamic allocation is not enabled, the memory
                            * could not have been dynamically allocated. So there is
                            * no need to free the memory - just mark the timer as
                            * "not active". */
                        pTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
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
    List_t<Timer_t> * Temp;
    while(CurrentTimerList->Length > 0) {
        NextExpireTime = CurrentTimerList->head()->Value;
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
    if( TimerQueue == NULL )
    {
        ActiveTimerList1.init();
        ActiveTimerList2.init();
        CurrentTimerList = &ActiveTimerList1;
        OverflowTimerList = &ActiveTimerList2;
        static StaticQueue_t StaticTimerQueue;
        static uint8_t StaticTimerQueueStorage[ ( size_t ) configTIMER_QUEUE_LENGTH * sizeof( DaemonTaskMessage_t ) ];
        TimerQueue = xQueueCreateStatic( ( UBaseType_t ) configTIMER_QUEUE_LENGTH, ( UBaseType_t ) sizeof( DaemonTaskMessage_t ), &( StaticTimerQueueStorage[ 0 ] ), &StaticTimerQueue );

    }
    EXIT_CRITICAL();
}

BaseType_t TimerIsTimerActive( TimerHandle_t Timer )
{
    configASSERT( Timer );
    ENTER_CRITICAL();
    BaseType_t Return = (Timer->ucStatus & tmrSTATUS_IS_ACTIVE ) != 0U;
    EXIT_CRITICAL();
    return Return;
}

void * pvTimerGetTimerID( const TimerHandle_t Timer )
{
    configASSERT( Timer );
    ENTER_CRITICAL();
    void *pvReturn = Timer->pvTimerID;
    EXIT_CRITICAL();
    return pvReturn;
}

void SetTimerID( TimerHandle_t Timer, void * pvNewID )
{
    configASSERT( Timer );
    ENTER_CRITICAL();
    Timer->pvTimerID = pvNewID;
    EXIT_CRITICAL();
    
}

BaseType_t TimerPendFunctionCallFromISR( PendedFunction_t xFunctionToPend,
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
    return xQueueSendFromISR( TimerQueue, &xMessage, pxHigherPriorityTaskWoken );
}

BaseType_t TimerPendFunctionCall( PendedFunction_t xFunctionToPend,
                                    void * pvParameter1,
                                    uint32_t ulParameter2,
                                    TickType_t xTicksToWait )
{
    DaemonTaskMessage_t xMessage;
    
    /* This function can only be called after a timer has been created or
        * after the scheduler has been started because, until then, the timer
        * queue does not exist. */
    configASSERT( TimerQueue );
    /* Complete the message with the function parameters and post it to the
        * daemon task. */
    xMessage.xMessageID = tmrCOMMAND_EXECUTE_CALLBACK;
    xMessage.u.xCallbackParameters.pxCallbackFunction = xFunctionToPend;
    xMessage.u.xCallbackParameters.pvParameter1 = pvParameter1;
    xMessage.u.xCallbackParameters.ulParameter2 = ulParameter2;
    return xQueueSendToBack( TimerQueue, &xMessage, xTicksToWait );
}

/*
 * Reset the state in this file. This state is normally initialized at start up.
 * This function must be called by the application before restarting the
 * scheduler.
 */
void TimerResetState( void )
{
    TimerQueue = NULL;
    TimerTaskHandle = NULL;
}
