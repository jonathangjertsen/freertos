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

#ifndef TIMERS_H
#define TIMERS_H
#ifndef INC_FREERTOS_H
    #error "include FreeRTOS.h must appear in source files before include timers.h"
#endif
#include "task.h"

/* *INDENT-OFF* */
#ifdef __cplusplus
    extern "C" {
#endif
/* *INDENT-ON* */
/*-----------------------------------------------------------
* MACROS AND DEFINITIONS
*----------------------------------------------------------*/
/* IDs for commands that can be sent/received on the timer queue.  These are to
 * be used solely through the macros that make up the public software timer API,
 * as defined below.  The commands that are sent from interrupts must use the
 * highest numbers as tmrFIRST_FROM_ISR_COMMAND is used to determine if the task
 * or interrupt version of the queue send function should be used. */
#define tmrCOMMAND_EXECUTE_CALLBACK_FROM_ISR    ( ( BaseType_t ) -2 )
#define tmrCOMMAND_EXECUTE_CALLBACK             ( ( BaseType_t ) -1 )
#define tmrCOMMAND_START_DONT_TRACE             ( ( BaseType_t ) 0 )
#define tmrCOMMAND_START                        ( ( BaseType_t ) 1 )
#define tmrCOMMAND_RESET                        ( ( BaseType_t ) 2 )
#define tmrCOMMAND_STOP                         ( ( BaseType_t ) 3 )
#define tmrCOMMAND_CHANGE_PERIOD                ( ( BaseType_t ) 4 )
#define tmrCOMMAND_DELETE                       ( ( BaseType_t ) 5 )
#define tmrFIRST_FROM_ISR_COMMAND               ( ( BaseType_t ) 6 )
#define tmrCOMMAND_START_FROM_ISR               ( ( BaseType_t ) 6 )
#define tmrCOMMAND_RESET_FROM_ISR               ( ( BaseType_t ) 7 )
#define tmrCOMMAND_STOP_FROM_ISR                ( ( BaseType_t ) 8 )
#define tmrCOMMAND_CHANGE_PERIOD_FROM_ISR       ( ( BaseType_t ) 9 )

/**
 * Type by which software timers are referenced.  For example, a call to
 * TimerCreate() returns an TimerHandle_t variable that can then be used to
 * reference the subject timer in calls to other software timer API functions
 * (for example, TimerStart(), TimerReset(), etc.).
 */
struct tmrTimerControl; /* The old naming convention is used to prevent breaking kernel aware debuggers. */
typedef struct tmrTimerControl * TimerHandle_t;
/*
 * Defines the prototype to which timer callback functions must conform.
 */
typedef void (* TimerCallbackFunction_t)( TimerHandle_t Timer );
/*
 * Defines the prototype to which functions used with the
 * TimerPendFunctionCallFromISR() function must conform.
 */
typedef void (* PendedFunction_t)( void * arg1,
                                   uint32_t arg2 );
/**
 * TimerHandle_t TimerCreate(  const char * const pcTimerName,
 *                              TickType_t TimerPeriodInTicks,
 *                              BaseType_t xAutoReload,
 *                              void * pvTimerID,
 *                              TimerCallbackFunction_t pxCallbackFunction );
 *
 * Creates a new software timer instance, and returns a handle by which the
 * created software timer can be referenced.
 *
 * Internally, within the FreeRTOS implementation, software timers use a block
 * of memory, in which the timer data structure is stored.  If a software timer
 * is created using TimerCreate() then the required memory is automatically
 * dynamically allocated inside the TimerCreate() function.  (see
 * https://www.FreeRTOS.org/a00111.html).  If a software timer is created using
 * TimerCreateStatic() then the application writer must provide the memory that
 * will get used by the software timer.  TimerCreateStatic() therefore allows a
 * software timer to be created without using any dynamic memory allocation.
 *
 * Timers are created in the dormant state.  The TimerStart(), TimerReset(),
 * TimerStartFromISR(), TimerResetFromISR(), TimerChangePeriod() and
 * TimerChangePeriodFromISR() API functions can all be used to transition a
 * timer into the active state.
 *
 * @param pcTimerName A text name that is assigned to the timer.  This is done
 * purely to assist debugging.  The kernel itself only ever references a timer
 * by its handle, and never by its name.
 *
 * @param TimerPeriodInTicks The timer period.  The time is defined in tick
 * periods so the constant portTICK_PERIOD_MS can be used to convert a time that
 * has been specified in milliseconds.  For example, if the timer must expire
 * after 100 ticks, then TimerPeriodInTicks should be set to 100.
 * Alternatively, if the timer must expire after 500ms, then xPeriod can be set
 * to ( 500 / portTICK_PERIOD_MS ) provided configTICK_RATE_HZ is less than or
 * equal to 1000.  Time timer period must be greater than 0.
 *
 * @param xAutoReload If xAutoReload is set to true then the timer will
 * expire repeatedly with a frequency set by the TimerPeriodInTicks parameter.
 * If xAutoReload is set to false then the timer will be a one-shot timer and
 * enter the dormant state after it expires.
 *
 * @param pvTimerID An identifier that is assigned to the timer being created.
 * Typically this would be used in the timer callback function to identify which
 * timer expired when the same callback function is assigned to more than one
 * timer.
 *
 * @param pxCallbackFunction The function to call when the timer expires.
 * Callback functions must have the prototype defined by TimerCallbackFunction_t,
 * which is "void vCallbackFunction( TimerHandle_t Timer );".
 *
 * @return If the timer is successfully created then a handle to the newly
 * created timer is returned.  If the timer cannot be created because there is
 * insufficient FreeRTOS heap remaining to allocate the timer
 * structures then NULL is returned.
 *
 * Example usage:
 * @verbatim
 * #define NUM_TIMERS 5
 *
 * // An array to hold handles to the created timers.
 * TimerHandle_t Timers[ NUM_TIMERS ];
 *
 * // An array to hold a count of the number of times each timer expires.
 * int32_t lExpireCounters[ NUM_TIMERS ] = { 0 };
 *
 * // Define a callback function that will be used by multiple timer instances.
 * // The callback function does nothing but count the number of times the
 * // associated timer expires, and stop the timer once the timer has expired
 * // 10 times.
 * void vTimerCallback( TimerHandle_t pTimer )
 * {
 * int32_t lArrayIndex;
 * const int32_t xMaxExpiryCountBeforeStopping = 10;
 *
 *     // Optionally do something if the pTimer parameter is NULL.
 *     configASSERT( pTimer );
 *
 *     // Which timer expired?
 *     lArrayIndex = ( int32_t ) pvTimerGetTimerID( pTimer );
 *
 *     // Increment the number of times that pTimer has expired.
 *     lExpireCounters[ lArrayIndex ] += 1;
 *
 *     // If the timer has expired 10 times then stop it from running.
 *     if( lExpireCounters[ lArrayIndex ] == xMaxExpiryCountBeforeStopping )
 *     {
 *         // Do not use a block time if calling a timer API function from a
 *         // timer callback function, as doing so could cause a deadlock!
 *         TimerStop( pTimer, 0 );
 *     }
 * }
 *
 * void main( void )
 * {
 * int32_t x;
 *
 *     // Create then start some timers.  Starting the timers before the scheduler
 *     // has been started means the timers will start running immediately that
 *     // the scheduler starts.
 *     for( x = 0; x < NUM_TIMERS; x++ )
 *     {
 *         Timers[ x ] = TimerCreate(    "Timer",             // Just a text name, not used by the kernel.
 *                                         ( 100 * ( x + 1 ) ), // The timer period in ticks.
 *                                         true,              // The timers will auto-reload themselves when they expire.
 *                                         ( void * ) x,        // Assign each timer a unique id equal to its array index.
 *                                         vTimerCallback       // Each timer calls the same callback when it expires.
 *                                     );
 *
 *         if( Timers[ x ] == NULL )
 *         {
 *             // The timer was not created.
 *         }
 *         else
 *         {
 *             // Start the timer.  No block time is specified, and even if one was
 *             // it would be ignored because the scheduler has not yet been
 *             // started.
 *             if( TimerStart( Timers[ x ], 0 ) != true )
 *             {
 *                 // The timer could not be set into the Active state.
 *             }
 *         }
 *     }
 *
 *     // ...
 *     // Create tasks here.
 *     // ...
 *
 *     // Starting the scheduler will start the timers running as they have already
 *     // been set into the active state.
 *     vTaskStartScheduler();
 *
 *     // Should not reach here.
 *     for( ;; );
 * }
 * @endverbatim
 */
#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
    TimerHandle_t TimerCreate( const char * const pcTimerName,
                                const TickType_t TimerPeriodInTicks,
                                const BaseType_t xAutoReload,
                                void * const pvTimerID,
                                TimerCallbackFunction_t pxCallbackFunction ) ;
#endif
/**
 * TimerHandle_t TimerCreateStatic(const char * const pcTimerName,
 *                                  TickType_t TimerPeriodInTicks,
 *                                  BaseType_t xAutoReload,
 *                                  void * pvTimerID,
 *                                  TimerCallbackFunction_t pxCallbackFunction,
 *                                  StaticTimer_t *pTimerBuffer );
 *
 * Creates a new software timer instance, and returns a handle by which the
 * created software timer can be referenced.
 *
 * Internally, within the FreeRTOS implementation, software timers use a block
 * of memory, in which the timer data structure is stored.  If a software timer
 * is created using TimerCreate() then the required memory is automatically
 * dynamically allocated inside the TimerCreate() function.  (see
 * https://www.FreeRTOS.org/a00111.html).  If a software timer is created using
 * TimerCreateStatic() then the application writer must provide the memory that
 * will get used by the software timer.  TimerCreateStatic() therefore allows a
 * software timer to be created without using any dynamic memory allocation.
 *
 * Timers are created in the dormant state.  The TimerStart(), TimerReset(),
 * TimerStartFromISR(), TimerResetFromISR(), TimerChangePeriod() and
 * TimerChangePeriodFromISR() API functions can all be used to transition a
 * timer into the active state.
 *
 * @param pcTimerName A text name that is assigned to the timer.  This is done
 * purely to assist debugging.  The kernel itself only ever references a timer
 * by its handle, and never by its name.
 *
 * @param TimerPeriodInTicks The timer period.  The time is defined in tick
 * periods so the constant portTICK_PERIOD_MS can be used to convert a time that
 * has been specified in milliseconds.  For example, if the timer must expire
 * after 100 ticks, then TimerPeriodInTicks should be set to 100.
 * Alternatively, if the timer must expire after 500ms, then xPeriod can be set
 * to ( 500 / portTICK_PERIOD_MS ) provided configTICK_RATE_HZ is less than or
 * equal to 1000.  The timer period must be greater than 0.
 *
 * @param xAutoReload If xAutoReload is set to true then the timer will
 * expire repeatedly with a frequency set by the TimerPeriodInTicks parameter.
 * If xAutoReload is set to false then the timer will be a one-shot timer and
 * enter the dormant state after it expires.
 *
 * @param pvTimerID An identifier that is assigned to the timer being created.
 * Typically this would be used in the timer callback function to identify which
 * timer expired when the same callback function is assigned to more than one
 * timer.
 *
 * @param pxCallbackFunction The function to call when the timer expires.
 * Callback functions must have the prototype defined by TimerCallbackFunction_t,
 * which is "void vCallbackFunction( TimerHandle_t Timer );".
 *
 * @param pTimerBuffer Must point to a variable of type StaticTimer_t, which
 * will be then be used to hold the software timer's data structures, removing
 * the need for the memory to be allocated dynamically.
 *
 * @return If the timer is created then a handle to the created timer is
 * returned.  If pTimerBuffer was NULL then NULL is returned.
 *
 * Example usage:
 * @verbatim
 *
 * // The buffer used to hold the software timer's data structure.
 * static StaticTimer_t TimerBuffer;
 *
 * // A variable that will be incremented by the software timer's callback
 * // function.
 * UBaseType_t uxVariableToIncrement = 0;
 *
 * // A software timer callback function that increments a variable passed to
 * // it when the software timer was created.  After the 5th increment the
 * // callback function stops the software timer.
 * static void TimerCallback( TimerHandle_t xExpiredTimer )
 * {
 * UBaseType_t *puxVariableToIncrement;
 * BaseType_t xReturned;
 *
 *     // Obtain the address of the variable to increment from the timer ID.
 *     puxVariableToIncrement = ( UBaseType_t * ) pvTimerGetTimerID( xExpiredTimer );
 *
 *     // Increment the variable to show the timer callback has executed.
 *     ( *puxVariableToIncrement )++;
 *
 *     // If this callback has executed the required number of times, stop the
 *     // timer.
 *     if( *puxVariableToIncrement == 5 )
 *     {
 *         // This is called from a timer callback so must not block.
 *         TimerStop( xExpiredTimer, staticDONT_BLOCK );
 *     }
 * }
 *
 *
 * void main( void )
 * {
 *     // Create the software time.  TimerCreateStatic() has an extra parameter
 *     // than the normal TimerCreate() API function.  The parameter is a pointer
 *     // to the StaticTimer_t structure that will hold the software timer
 *     // structure.  If the parameter is passed as NULL then the structure will be
 *     // allocated dynamically, just as if TimerCreate() had been called.
 *     Timer = TimerCreateStatic( "T1",             // Text name for the task.  Helps debugging only.  Not used by FreeRTOS.
 *                                  TimerPeriod,     // The period of the timer in ticks.
 *                                  true,           // This is an auto-reload timer.
 *                                  ( void * ) &uxVariableToIncrement,    // A variable incremented by the software timer's callback function
 *                                  TimerCallback, // The function to execute when the timer expires.
 *                                  &TimerBuffer );  // The buffer that will hold the software timer structure.
 *
 *     // The scheduler has not started yet so a block time is not used.
 *     xReturned = TimerStart( Timer, 0 );
 *
 *     // ...
 *     // Create tasks here.
 *     // ...
 *
 *     // Starting the scheduler will start the timers running as they have already
 *     // been set into the active state.
 *     vTaskStartScheduler();
 *
 *     // Should not reach here.
 *     for( ;; );
 * }
 * @endverbatim
 */
#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
    TimerHandle_t TimerCreateStatic( const char * const pcTimerName,
                                      const TickType_t TimerPeriodInTicks,
                                      const BaseType_t xAutoReload,
                                      void * const pvTimerID,
                                      TimerCallbackFunction_t pxCallbackFunction,
                                      StaticTimer_t * pTimerBuffer ) ;
#endif /* configSUPPORT_STATIC_ALLOCATION */
/**
 * void *pvTimerGetTimerID( TimerHandle_t Timer );
 *
 * Returns the ID assigned to the timer.
 *
 * IDs are assigned to timers using the pvTimerID parameter of the call to
 * TimerCreated() that was used to create the timer, and by calling the
 * SetTimerID() API function.
 *
 * If the same callback function is assigned to multiple timers then the timer
 * ID can be used as time specific (timer local) storage.
 *
 * @param Timer The timer being queried.
 *
 * @return The ID assigned to the timer being queried.
 *
 * Example usage:
 *
 * See the TimerCreate() API function example usage scenario.
 */
void * pvTimerGetTimerID( const TimerHandle_t Timer ) ;
/**
 * void SetTimerID( TimerHandle_t Timer, void *pvNewID );
 *
 * Sets the ID assigned to the timer.
 *
 * IDs are assigned to timers using the pvTimerID parameter of the call to
 * TimerCreated() that was used to create the timer.
 *
 * If the same callback function is assigned to multiple timers then the timer
 * ID can be used as time specific (timer local) storage.
 *
 * @param Timer The timer being updated.
 *
 * @param pvNewID The ID to assign to the timer.
 *
 * Example usage:
 *
 * See the TimerCreate() API function example usage scenario.
 */
void SetTimerID( TimerHandle_t Timer,
                       void * pvNewID ) ;
/**
 * BaseType_t TimerIsTimerActive( TimerHandle_t Timer );
 *
 * Queries a timer to see if it is active or dormant.
 *
 * A timer will be dormant if:
 *     1) It has been created but not started, or
 *     2) It is an expired one-shot timer that has not been restarted.
 *
 * Timers are created in the dormant state.  The TimerStart(), TimerReset(),
 * TimerStartFromISR(), TimerResetFromISR(), TimerChangePeriod() and
 * TimerChangePeriodFromISR() API functions can all be used to transition a timer into the
 * active state.
 *
 * @param Timer The timer being queried.
 *
 * @return false will be returned if the timer is dormant.  A value other than
 * false will be returned if the timer is active.
 */
BaseType_t TimerIsTimerActive( TimerHandle_t Timer ) ;
/**
 * TaskHandle_t TimerGetTimerDaemonTaskHandle( void );
 *
 * Simply returns the handle of the timer service/daemon task.  It it not valid
 * to call TimerGetTimerDaemonTaskHandle() before the scheduler has been started.
 */
TaskHandle_t TimerGetTimerDaemonTaskHandle( void ) ;
/**
 * BaseType_t TimerStart( TimerHandle_t Timer, TickType_t xTicksToWait );
 *
 * Timer functionality is provided by a timer service/daemon task.  Many of the
 * public FreeRTOS timer API functions send commands to the timer service task
 * through a queue called the timer command queue.  The timer command queue is
 * private to the kernel itself and is not directly accessible to application
 * code.  The length of the timer command queue is set by the
 * configTIMER_QUEUE_LENGTH configuration constant.
 *
 * TimerStart() starts a timer that was previously created using the
 * TimerCreate() API function.  If the timer had already been started and was
 * already in the active state, then TimerStart() has equivalent functionality
 * to the TimerReset() API function.
 *
 * Starting a timer ensures the timer is in the active state.  If the timer
 * is not stopped, deleted, or reset in the mean time, the callback function
 * associated with the timer will get called 'n' ticks after TimerStart() was
 * called, where 'n' is the timers defined period.
 *
 * It is valid to call TimerStart() before the scheduler has been started, but
 * when this is done the timer will not actually start until the scheduler is
 * started, and the timers expiry time will be relative to when the scheduler is
 * started, not relative to when TimerStart() was called.
 *
 * The configUSE_TIMERS configuration constant must be set to 1 for TimerStart()
 * to be available.
 *
 * @param Timer The handle of the timer being started/restarted.
 *
 * @param xTicksToWait Specifies the time, in ticks, that the calling task should
 * be held in the Blocked state to wait for the start command to be successfully
 * sent to the timer command queue, should the queue already be full when
 * TimerStart() was called.  xTicksToWait is ignored if TimerStart() is called
 * before the scheduler is started.
 *
 * @return false will be returned if the start command could not be sent to
 * the timer command queue even after xTicksToWait ticks had passed.  true will
 * be returned if the command was successfully sent to the timer command queue.
 * When the command is actually processed will depend on the priority of the
 * timer service/daemon task relative to other tasks in the system, although the
 * timers expiry time is relative to when TimerStart() is actually called.  The
 * timer service/daemon task priority is set by the configTIMER_TASK_PRIORITY
 * configuration constant.
 *
 */
#define TimerStart( Timer, xTicksToWait ) \
    TimerGenericCommand( ( Timer ), tmrCOMMAND_START, ( xTaskGetTickCount() ), NULL, ( xTicksToWait ) )
/**
 * BaseType_t TimerStop( TimerHandle_t Timer, TickType_t xTicksToWait );
 *
 * Timer functionality is provided by a timer service/daemon task.  Many of the
 * public FreeRTOS timer API functions send commands to the timer service task
 * through a queue called the timer command queue.  The timer command queue is
 * private to the kernel itself and is not directly accessible to application
 * code.  The length of the timer command queue is set by the
 * configTIMER_QUEUE_LENGTH configuration constant.
 *
 * TimerStop() stops a timer that was previously started using either of the
 * The TimerStart(), TimerReset(), TimerStartFromISR(), TimerResetFromISR(),
 * TimerChangePeriod() or TimerChangePeriodFromISR() API functions.
 *
 * Stopping a timer ensures the timer is not in the active state.
 *
 * The configUSE_TIMERS configuration constant must be set to 1 for TimerStop()
 * to be available.
 *
 * @param Timer The handle of the timer being stopped.
 *
 * @param xTicksToWait Specifies the time, in ticks, that the calling task should
 * be held in the Blocked state to wait for the stop command to be successfully
 * sent to the timer command queue, should the queue already be full when
 * TimerStop() was called.  xTicksToWait is ignored if TimerStop() is called
 * before the scheduler is started.
 *
 * @return false will be returned if the stop command could not be sent to
 * the timer command queue even after xTicksToWait ticks had passed.  true will
 * be returned if the command was successfully sent to the timer command queue.
 * When the command is actually processed will depend on the priority of the
 * timer service/daemon task relative to other tasks in the system.  The timer
 * service/daemon task priority is set by the configTIMER_TASK_PRIORITY
 * configuration constant.
 */
#define TimerStop( Timer, xTicksToWait ) \
    TimerGenericCommand( ( Timer ), tmrCOMMAND_STOP, 0U, NULL, ( xTicksToWait ) )
/**
 * BaseType_t TimerChangePeriod(   TimerHandle_t Timer,
 *                                  TickType_t xNewPeriod,
 *                                  TickType_t xTicksToWait );
 *
 * Timer functionality is provided by a timer service/daemon task.  Many of the
 * public FreeRTOS timer API functions send commands to the timer service task
 * through a queue called the timer command queue.  The timer command queue is
 * private to the kernel itself and is not directly accessible to application
 * code.  The length of the timer command queue is set by the
 * configTIMER_QUEUE_LENGTH configuration constant.
 *
 * TimerChangePeriod() changes the period of a timer that was previously
 * created using the TimerCreate() API function.
 *
 * TimerChangePeriod() can be called to change the period of an active or
 * dormant state timer.
 *
 * The configUSE_TIMERS configuration constant must be set to 1 for
 * TimerChangePeriod() to be available.
 *
 * @param Timer The handle of the timer that is having its period changed.
 *
 * @param xNewPeriod The new period for Timer. Timer periods are specified in
 * tick periods, so the constant portTICK_PERIOD_MS can be used to convert a time
 * that has been specified in milliseconds.  For example, if the timer must
 * expire after 100 ticks, then xNewPeriod should be set to 100.  Alternatively,
 * if the timer must expire after 500ms, then xNewPeriod can be set to
 * ( 500 / portTICK_PERIOD_MS ) provided configTICK_RATE_HZ is less than
 * or equal to 1000.
 *
 * @param xTicksToWait Specifies the time, in ticks, that the calling task should
 * be held in the Blocked state to wait for the change period command to be
 * successfully sent to the timer command queue, should the queue already be
 * full when TimerChangePeriod() was called.  xTicksToWait is ignored if
 * TimerChangePeriod() is called before the scheduler is started.
 *
 * @return false will be returned if the change period command could not be
 * sent to the timer command queue even after xTicksToWait ticks had passed.
 * true will be returned if the command was successfully sent to the timer
 * command queue.  When the command is actually processed will depend on the
 * priority of the timer service/daemon task relative to other tasks in the
 * system.  The timer service/daemon task priority is set by the
 * configTIMER_TASK_PRIORITY configuration constant.
 */
#define TimerChangePeriod( Timer, xNewPeriod, xTicksToWait ) \
    TimerGenericCommand( ( Timer ), tmrCOMMAND_CHANGE_PERIOD, ( xNewPeriod ), NULL, ( xTicksToWait ) )
/**
 * BaseType_t TimerDelete( TimerHandle_t Timer, TickType_t xTicksToWait );
 *
 * Timer functionality is provided by a timer service/daemon task.  Many of the
 * public FreeRTOS timer API functions send commands to the timer service task
 * through a queue called the timer command queue.  The timer command queue is
 * private to the kernel itself and is not directly accessible to application
 * code.  The length of the timer command queue is set by the
 * configTIMER_QUEUE_LENGTH configuration constant.
 *
 * TimerDelete() deletes a timer that was previously created using the
 * TimerCreate() API function.
 *
 * The configUSE_TIMERS configuration constant must be set to 1 for
 * TimerDelete() to be available.
 *
 * @param Timer The handle of the timer being deleted.
 *
 * @param xTicksToWait Specifies the time, in ticks, that the calling task should
 * be held in the Blocked state to wait for the delete command to be
 * successfully sent to the timer command queue, should the queue already be
 * full when TimerDelete() was called.  xTicksToWait is ignored if TimerDelete()
 * is called before the scheduler is started.
 *
 * @return false will be returned if the delete command could not be sent to
 * the timer command queue even after xTicksToWait ticks had passed.  true will
 * be returned if the command was successfully sent to the timer command queue.
 * When the command is actually processed will depend on the priority of the
 * timer service/daemon task relative to other tasks in the system.  The timer
 * service/daemon task priority is set by the configTIMER_TASK_PRIORITY
 * configuration constant.
 *
 * Example usage:
 *
 * See the TimerChangePeriod() API function example usage scenario.
 */
#define TimerDelete( Timer, xTicksToWait ) \
    TimerGenericCommand( ( Timer ), tmrCOMMAND_DELETE, 0U, NULL, ( xTicksToWait ) )
/**
 * BaseType_t TimerReset( TimerHandle_t Timer, TickType_t xTicksToWait );
 *
 * Timer functionality is provided by a timer service/daemon task.  Many of the
 * public FreeRTOS timer API functions send commands to the timer service task
 * through a queue called the timer command queue.  The timer command queue is
 * private to the kernel itself and is not directly accessible to application
 * code.  The length of the timer command queue is set by the
 * configTIMER_QUEUE_LENGTH configuration constant.
 *
 * TimerReset() re-starts a timer that was previously created using the
 * TimerCreate() API function.  If the timer had already been started and was
 * already in the active state, then TimerReset() will cause the timer to
 * re-evaluate its expiry time so that it is relative to when TimerReset() was
 * called.  If the timer was in the dormant state then TimerReset() has
 * equivalent functionality to the TimerStart() API function.
 *
 * Resetting a timer ensures the timer is in the active state.  If the timer
 * is not stopped, deleted, or reset in the mean time, the callback function
 * associated with the timer will get called 'n' ticks after TimerReset() was
 * called, where 'n' is the timers defined period.
 *
 * It is valid to call TimerReset() before the scheduler has been started, but
 * when this is done the timer will not actually start until the scheduler is
 * started, and the timers expiry time will be relative to when the scheduler is
 * started, not relative to when TimerReset() was called.
 *
 * The configUSE_TIMERS configuration constant must be set to 1 for TimerReset()
 * to be available.
 *
 * @param Timer The handle of the timer being reset/started/restarted.
 *
 * @param xTicksToWait Specifies the time, in ticks, that the calling task should
 * be held in the Blocked state to wait for the reset command to be successfully
 * sent to the timer command queue, should the queue already be full when
 * TimerReset() was called.  xTicksToWait is ignored if TimerReset() is called
 * before the scheduler is started.
 *
 * @return false will be returned if the reset command could not be sent to
 * the timer command queue even after xTicksToWait ticks had passed.  true will
 * be returned if the command was successfully sent to the timer command queue.
 * When the command is actually processed will depend on the priority of the
 * timer service/daemon task relative to other tasks in the system, although the
 * timers expiry time is relative to when TimerStart() is actually called.  The
 * timer service/daemon task priority is set by the configTIMER_TASK_PRIORITY
 * configuration constant.
 */
#define TimerReset( Timer, xTicksToWait ) \
    TimerGenericCommand( ( Timer ), tmrCOMMAND_RESET, ( xTaskGetTickCount() ), NULL, ( xTicksToWait ) )
/**
 * BaseType_t TimerStartFromISR(   TimerHandle_t Timer,
 *                                  BaseType_t *pxHigherPriorityTaskWoken );
 *
 * A version of TimerStart() that can be called from an interrupt service
 * routine.
 *
 * @param Timer The handle of the timer being started/restarted.
 *
 * @param pxHigherPriorityTaskWoken The timer service/daemon task spends most
 * of its time in the Blocked state, waiting for messages to arrive on the timer
 * command queue.  Calling TimerStartFromISR() writes a message to the timer
 * command queue, so has the potential to transition the timer service/daemon
 * task out of the Blocked state.  If calling TimerStartFromISR() causes the
 * timer service/daemon task to leave the Blocked state, and the timer service/
 * daemon task has a priority equal to or greater than the currently executing
 * task (the task that was interrupted), then *pxHigherPriorityTaskWoken will
 * get set to true internally within the TimerStartFromISR() function.  If
 * TimerStartFromISR() sets this value to true then a context switch should
 * be performed before the interrupt exits.
 *
 * @return false will be returned if the start command could not be sent to
 * the timer command queue.  true will be returned if the command was
 * successfully sent to the timer command queue.  When the command is actually
 * processed will depend on the priority of the timer service/daemon task
 * relative to other tasks in the system, although the timers expiry time is
 * relative to when TimerStartFromISR() is actually called.  The timer
 * service/daemon task priority is set by the configTIMER_TASK_PRIORITY
 * configuration constant.
 *
 */
#define TimerStartFromISR( Timer, pxHigherPriorityTaskWoken ) \
    TimerGenericCommand( ( Timer ), tmrCOMMAND_START_FROM_ISR, ( xTaskGetTickCountFromISR() ), ( pxHigherPriorityTaskWoken ), 0U )
/**
 * BaseType_t TimerStopFromISR(    TimerHandle_t Timer,
 *                                  BaseType_t *pxHigherPriorityTaskWoken );
 *
 * A version of TimerStop() that can be called from an interrupt service
 * routine.
 *
 * @param Timer The handle of the timer being stopped.
 *
 * @param pxHigherPriorityTaskWoken The timer service/daemon task spends most
 * of its time in the Blocked state, waiting for messages to arrive on the timer
 * command queue.  Calling TimerStopFromISR() writes a message to the timer
 * command queue, so has the potential to transition the timer service/daemon
 * task out of the Blocked state.  If calling TimerStopFromISR() causes the
 * timer service/daemon task to leave the Blocked state, and the timer service/
 * daemon task has a priority equal to or greater than the currently executing
 * task (the task that was interrupted), then *pxHigherPriorityTaskWoken will
 * get set to true internally within the TimerStopFromISR() function.  If
 * TimerStopFromISR() sets this value to true then a context switch should
 * be performed before the interrupt exits.
 *
 * @return false will be returned if the stop command could not be sent to
 * the timer command queue.  true will be returned if the command was
 * successfully sent to the timer command queue.  When the command is actually
 * processed will depend on the priority of the timer service/daemon task
 * relative to other tasks in the system.  The timer service/daemon task
 * priority is set by the configTIMER_TASK_PRIORITY configuration constant.
 */
#define TimerStopFromISR( Timer, pxHigherPriorityTaskWoken ) \
    TimerGenericCommand( ( Timer ), tmrCOMMAND_STOP_FROM_ISR, 0, ( pxHigherPriorityTaskWoken ), 0U )
/**
 * BaseType_t TimerChangePeriodFromISR( TimerHandle_t Timer,
 *                                       TickType_t xNewPeriod,
 *                                       BaseType_t *pxHigherPriorityTaskWoken );
 *
 * A version of TimerChangePeriod() that can be called from an interrupt
 * service routine.
 *
 * @param Timer The handle of the timer that is having its period changed.
 *
 * @param xNewPeriod The new period for Timer. Timer periods are specified in
 * tick periods, so the constant portTICK_PERIOD_MS can be used to convert a time
 * that has been specified in milliseconds.  For example, if the timer must
 * expire after 100 ticks, then xNewPeriod should be set to 100.  Alternatively,
 * if the timer must expire after 500ms, then xNewPeriod can be set to
 * ( 500 / portTICK_PERIOD_MS ) provided configTICK_RATE_HZ is less than
 * or equal to 1000.
 *
 * @param pxHigherPriorityTaskWoken The timer service/daemon task spends most
 * of its time in the Blocked state, waiting for messages to arrive on the timer
 * command queue.  Calling TimerChangePeriodFromISR() writes a message to the
 * timer command queue, so has the potential to transition the timer service/
 * daemon task out of the Blocked state.  If calling TimerChangePeriodFromISR()
 * causes the timer service/daemon task to leave the Blocked state, and the
 * timer service/daemon task has a priority equal to or greater than the
 * currently executing task (the task that was interrupted), then
 * *pxHigherPriorityTaskWoken will get set to true internally within the
 * TimerChangePeriodFromISR() function.  If TimerChangePeriodFromISR() sets
 * this value to true then a context switch should be performed before the
 * interrupt exits.
 *
 * @return false will be returned if the command to change the timers period
 * could not be sent to the timer command queue.  true will be returned if the
 * command was successfully sent to the timer command queue.  When the command
 * is actually processed will depend on the priority of the timer service/daemon
 * task relative to other tasks in the system.  The timer service/daemon task
 * priority is set by the configTIMER_TASK_PRIORITY configuration constant.
 */
#define TimerChangePeriodFromISR( Timer, xNewPeriod, pxHigherPriorityTaskWoken ) \
    TimerGenericCommand( ( Timer ), tmrCOMMAND_CHANGE_PERIOD_FROM_ISR, ( xNewPeriod ), ( pxHigherPriorityTaskWoken ), 0U )
/**
 * BaseType_t TimerResetFromISR(   TimerHandle_t Timer,
 *                                  BaseType_t *pxHigherPriorityTaskWoken );
 *
 * A version of TimerReset() that can be called from an interrupt service
 * routine.
 *
 * @param Timer The handle of the timer that is to be started, reset, or
 * restarted.
 *
 * @param pxHigherPriorityTaskWoken The timer service/daemon task spends most
 * of its time in the Blocked state, waiting for messages to arrive on the timer
 * command queue.  Calling TimerResetFromISR() writes a message to the timer
 * command queue, so has the potential to transition the timer service/daemon
 * task out of the Blocked state.  If calling TimerResetFromISR() causes the
 * timer service/daemon task to leave the Blocked state, and the timer service/
 * daemon task has a priority equal to or greater than the currently executing
 * task (the task that was interrupted), then *pxHigherPriorityTaskWoken will
 * get set to true internally within the TimerResetFromISR() function.  If
 * TimerResetFromISR() sets this value to true then a context switch should
 * be performed before the interrupt exits.
 *
 * @return false will be returned if the reset command could not be sent to
 * the timer command queue.  true will be returned if the command was
 * successfully sent to the timer command queue.  When the command is actually
 * processed will depend on the priority of the timer service/daemon task
 * relative to other tasks in the system, although the timers expiry time is
 * relative to when TimerResetFromISR() is actually called.  The timer service/daemon
 * task priority is set by the configTIMER_TASK_PRIORITY configuration constant.
 */
#define TimerResetFromISR( Timer, pxHigherPriorityTaskWoken ) \
    TimerGenericCommand( ( Timer ), tmrCOMMAND_RESET_FROM_ISR, ( xTaskGetTickCountFromISR() ), ( pxHigherPriorityTaskWoken ), 0U )

/**
 * BaseType_t TimerPendFunctionCallFromISR( PendedFunction_t xFunctionToPend,
 *                                          void *pvParameter1,
 *                                          uint32_t ulParameter2,
 *                                          BaseType_t *pxHigherPriorityTaskWoken );
 *
 *
 * Used from application interrupt service routines to defer the execution of a
 * function to the RTOS daemon task (the timer service task, hence this function
 * is implemented in timers.c and is prefixed with 'Timer').
 *
 * Ideally an interrupt service routine (ISR) is kept as short as possible, but
 * sometimes an ISR either has a lot of processing to do, or needs to perform
 * processing that is not deterministic.  In these cases
 * TimerPendFunctionCallFromISR() can be used to defer processing of a function
 * to the RTOS daemon task.
 *
 * A mechanism is provided that allows the interrupt to return directly to the
 * task that will subsequently execute the pended callback function.  This
 * allows the callback function to execute contiguously in time with the
 * interrupt - just as if the callback had executed in the interrupt itself.
 *
 * @param xFunctionToPend The function to execute from the timer service/
 * daemon task.  The function must conform to the PendedFunction_t
 * prototype.
 *
 * @param pvParameter1 The value of the callback function's first parameter.
 * The parameter has a void * type to allow it to be used to pass any type.
 * For example, unsigned longs can be cast to a void *, or the void * can be
 * used to point to a structure.
 *
 * @param ulParameter2 The value of the callback function's second parameter.
 *
 * @param pxHigherPriorityTaskWoken As mentioned above, calling this function
 * will result in a message being sent to the timer daemon task.  If the
 * priority of the timer daemon task (which is set using
 * configTIMER_TASK_PRIORITY in FreeRTOSConfig.h) is higher than the priority of
 * the currently running task (the task the interrupt interrupted) then
 * *pxHigherPriorityTaskWoken will be set to true within
 * TimerPendFunctionCallFromISR(), indicating that a context switch should be
 * requested before the interrupt exits.  For that reason
 * *pxHigherPriorityTaskWoken must be initialised to false.  See the
 * example code below.
 *
 * @return true is returned if the message was successfully sent to the
 * timer daemon task, otherwise false is returned.
 */
#if ( INCLUDE_TimerPendFunctionCall == 1 )
    BaseType_t TimerPendFunctionCallFromISR( PendedFunction_t xFunctionToPend,
                                              void * pvParameter1,
                                              uint32_t ulParameter2,
                                              BaseType_t * pxHigherPriorityTaskWoken ) ;
#endif
/**
 * BaseType_t TimerPendFunctionCall( PendedFunction_t xFunctionToPend,
 *                                    void *pvParameter1,
 *                                    uint32_t ulParameter2,
 *                                    TickType_t xTicksToWait );
 *
 *
 * Used to defer the execution of a function to the RTOS daemon task (the timer
 * service task, hence this function is implemented in timers.c and is prefixed
 * with 'Timer').
 *
 * @param xFunctionToPend The function to execute from the timer service/
 * daemon task.  The function must conform to the PendedFunction_t
 * prototype.
 *
 * @param pvParameter1 The value of the callback function's first parameter.
 * The parameter has a void * type to allow it to be used to pass any type.
 * For example, unsigned longs can be cast to a void *, or the void * can be
 * used to point to a structure.
 *
 * @param ulParameter2 The value of the callback function's second parameter.
 *
 * @param xTicksToWait Calling this function will result in a message being
 * sent to the timer daemon task on a queue.  xTicksToWait is the amount of
 * time the calling task should remain in the Blocked state (so not using any
 * processing time) for space to become available on the timer queue if the
 * queue is found to be full.
 *
 * @return true is returned if the message was successfully sent to the
 * timer daemon task, otherwise false is returned.
 *
 */
#if ( INCLUDE_TimerPendFunctionCall == 1 )
    BaseType_t TimerPendFunctionCall( PendedFunction_t xFunctionToPend,
                                       void * pvParameter1,
                                       uint32_t ulParameter2,
                                       TickType_t xTicksToWait ) ;
#endif
/**
 * const char * const pcTimerGetName( TimerHandle_t Timer );
 *
 * Returns the name that was assigned to a timer when the timer was created.
 *
 * @param Timer The handle of the timer being queried.
 *
 * @return The name assigned to the timer specified by the Timer parameter.
 */
const char * pcTimerGetName( TimerHandle_t Timer ) ;
/**
 * void vTimerSetReloadMode( TimerHandle_t Timer, const BaseType_t xAutoReload );
 *
 * Updates a timer to be either an auto-reload timer, in which case the timer
 * automatically resets itself each time it expires, or a one-shot timer, in
 * which case the timer will only expire once unless it is manually restarted.
 *
 * @param Timer The handle of the timer being updated.
 *
 * @param xAutoReload If xAutoReload is set to true then the timer will
 * expire repeatedly with a frequency set by the timer's period (see the
 * TimerPeriodInTicks parameter of the TimerCreate() API function).  If
 * xAutoReload is set to false then the timer will be a one-shot timer and
 * enter the dormant state after it expires.
 */
void vTimerSetReloadMode( TimerHandle_t Timer,
                          const BaseType_t xAutoReload ) ;
/**
 * BaseType_t TimerGetReloadMode( TimerHandle_t Timer );
 *
 * Queries a timer to determine if it is an auto-reload timer, in which case the timer
 * automatically resets itself each time it expires, or a one-shot timer, in
 * which case the timer will only expire once unless it is manually restarted.
 *
 * @param Timer The handle of the timer being queried.
 *
 * @return If the timer is an auto-reload timer then true is returned, otherwise
 * false is returned.
 */
BaseType_t TimerGetReloadMode( TimerHandle_t Timer ) ;
/**
 * UBaseType_t uTimerGetReloadMode( TimerHandle_t Timer );
 *
 * Queries a timer to determine if it is an auto-reload timer, in which case the timer
 * automatically resets itself each time it expires, or a one-shot timer, in
 * which case the timer will only expire once unless it is manually restarted.
 *
 * @param Timer The handle of the timer being queried.
 *
 * @return If the timer is an auto-reload timer then true is returned, otherwise
 * false is returned.
 */
UBaseType_t uTimerGetReloadMode( TimerHandle_t Timer ) ;
/**
 * TickType_t TimerGetPeriod( TimerHandle_t Timer );
 *
 * Returns the period of a timer.
 *
 * @param Timer The handle of the timer being queried.
 *
 * @return The period of the timer in ticks.
 */
TickType_t TimerGetPeriod( TimerHandle_t Timer ) ;
/**
 * TickType_t TimerGetExpiryTime( TimerHandle_t Timer );
 *
 * Returns the time in ticks at which the timer will expire.  If this is less
 * than the current tick count then the expiry time has overflowed from the
 * current time.
 *
 * @param Timer The handle of the timer being queried.
 *
 * @return If the timer is running then the time in ticks at which the timer
 * will next expire is returned.  If the timer is not running then the return
 * value is undefined.
 */
TickType_t TimerGetExpiryTime( TimerHandle_t Timer ) ;
/**
 * BaseType_t TimerGetStaticBuffer( TimerHandle_t Timer,
 *                                   StaticTimer_t ** ppTimerBuffer );
 *
 * Retrieve pointer to a statically created timer's data structure
 * buffer. This is the same buffer that is supplied at the time of
 * creation.
 *
 * @param Timer The timer for which to retrieve the buffer.
 *
 * @param ppxTaskBuffer Used to return a pointer to the timers's data
 * structure buffer.
 *
 * @return true if the buffer was retrieved, false otherwise.
 */
#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
    BaseType_t TimerGetStaticBuffer( TimerHandle_t Timer,
                                      StaticTimer_t ** ppTimerBuffer ) ;
#endif /* configSUPPORT_STATIC_ALLOCATION */
/*
 * Functions beyond this part are not part of the public API and are intended
 * for use by the kernel only.
 */
BaseType_t TimerCreateTimerTask( void ) ;
/*
 * Splitting the TimerGenericCommand into two sub functions and making it a macro
 * removes a recursion path when called from ISRs. This is primarily for the XCore
 * XCC port which detects the recursion path and throws an error during compilation
 * when this is not split.
 */
BaseType_t TimerGenericCommandFromTask( TimerHandle_t Timer,
                                         const BaseType_t xCommandID,
                                         const TickType_t xOptionalValue,
                                         BaseType_t * const pxHigherPriorityTaskWoken,
                                         const TickType_t xTicksToWait ) ;
BaseType_t TimerGenericCommandFromISR( TimerHandle_t Timer,
                                        const BaseType_t xCommandID,
                                        const TickType_t xOptionalValue,
                                        BaseType_t * const pxHigherPriorityTaskWoken,
                                        const TickType_t xTicksToWait ) ;
#define TimerGenericCommand( Timer, xCommandID, xOptionalValue, pxHigherPriorityTaskWoken, xTicksToWait )         \
    ( ( xCommandID ) < tmrFIRST_FROM_ISR_COMMAND ?                                                                  \
      TimerGenericCommandFromTask( Timer, xCommandID, xOptionalValue, pxHigherPriorityTaskWoken, xTicksToWait ) : \
      TimerGenericCommandFromISR( Timer, xCommandID, xOptionalValue, pxHigherPriorityTaskWoken, xTicksToWait ) )
#if ( configUSE_TRACE_FACILITY == 1 )
    void vTimerSetTimerNumber( TimerHandle_t Timer,
                               UBaseType_t uTimerNumber ) ;
    UBaseType_t uTimerGetTimerNumber( TimerHandle_t Timer ) ;
#endif

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
/**
 * task.h
 * @code{c}
 * void vApplicationGetTimerTaskMemory( StaticTask_t ** ppTimerTaskTCBBuffer, StackType_t ** ppTimerTaskStackBuffer, configSTACK_DEPTH_TYPE * puTimerTaskStackSize )
 * @endcode
 *
 * This function is used to provide a statically allocated block of memory to FreeRTOS to hold the Timer Task TCB.  This function is required when
 * configSUPPORT_STATIC_ALLOCATION is set.  For more information see this URI: https://www.FreeRTOS.org/a00110.html#configSUPPORT_STATIC_ALLOCATION
 *
 * @param ppTimerTaskTCBBuffer   A handle to a statically allocated TCB buffer
 * @param ppTimerTaskStackBuffer A handle to a statically allocated Stack buffer for the idle task
 * @param puTimerTaskStackSize   A pointer to the number of elements that will fit in the allocated stack buffer
 */
    void vApplicationGetTimerTaskMemory( StaticTask_t ** ppTimerTaskTCBBuffer,
                                         StackType_t ** ppTimerTaskStackBuffer,
                                         configSTACK_DEPTH_TYPE * puTimerTaskStackSize );
#endif

#if ( configUSE_DAEMON_TASK_STARTUP_HOOK != 0 )
/**
 *  timers.h
 *
 * This hook function is called form the timer task once when the task starts running.
 */
    void vApplicationDaemonTaskStartupHook( void );
#endif
/*
 * This function resets the internal state of the timer module. It must be called
 * by the application before restarting the scheduler.
 */
void TimerResetState( void ) ;
/* *INDENT-OFF* */
#ifdef __cplusplus
    }
#endif
/* *INDENT-ON* */
#endif /* TIMERS_H */
