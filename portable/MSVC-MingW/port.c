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
#include <stdio.h>
#include <stdbool.h>
/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.hpp"
#ifdef __GNUC__
    #include "mmsystem.h"
#else
    #pragma comment(lib, "winmm.lib")
#endif
#define portMAX_INTERRUPTS                          ( ( uint32_t ) sizeof( uint32_t ) * 8UL ) /* The number of bits in an uint32_t. */
#define portNO_CRITICAL_NESTING                     ( ( uint32_t ) 0 )
/* The priorities at which the various components of the simulation execute. */
#define portDELETE_SELF_THREAD_PRIORITY             THREAD_PRIORITY_TIME_CRITICAL /* Must be highest. */
#define portSIMULATED_INTERRUPTS_THREAD_PRIORITY    THREAD_PRIORITY_TIME_CRITICAL
#define portSIMULATED_TIMER_THREAD_PRIORITY         THREAD_PRIORITY_HIGHEST
#define portTASK_THREAD_PRIORITY                    THREAD_PRIORITY_ABOVE_NORMAL
/*
 * Created as a high priority thread, this function uses a timer to simulate
 * a tick interrupt being generated on an embedded target.  In this Windows
 * environment the timer does not achieve anything approaching real time
 * performance though.
 */
static DWORD WINAPI SimulatedPeripheralTimer( LPVOID lpParameter );
/*
 * Process all the simulated interrupts - each represented by a bit in
 * ulPendingInterrupts variable.
 */
static void ProcessSimulatedInterrupts( void );
/*
 * Interrupt handlers used by the kernel itself.  These are executed from the
 * simulated interrupt handler thread.
 */
static uint32_t ProcessYieldInterrupt( void );
static uint32_t ProcessTickInterrupt( void );
/*
 * Exiting a critical section will cause the calling task to block on yield
 * event to wait for an interrupt to process if an interrupt was pended while
 * inside the critical section.  This variable protects against a recursive
 * attempt to obtain pvInterruptEventMutex if a critical section is used inside
 * an interrupt handler itself.
 */
volatile BaseType_t xInsideInterrupt = false;
/*
 * Called when the process exits to let Windows know the high timer resolution
 * is no longer required.
 */
static BOOL WINAPI EndProcess( DWORD dwCtrlType );

/* The WIN32 simulator runs each task in a thread.  The context switching is
 * managed by the threads, so the task stack does not have to be managed directly,
 * although the task stack is still used to hold an xThreadState structure this is
 * the only thing it will ever hold.  The structure indirectly maps the task handle
 * to a thread handle. */
typedef struct
{
    /* Handle of the thread that executes the task. */
    void * pvThread;
    /* Event used to make sure the thread does not execute past a yield point
     * between the call to SuspendThread() to suspend the thread and the
     * asynchronous SuspendThread() operation actually being performed. */
    void * pvYieldEvent;
} ThreadState_t;
/* Simulated interrupts waiting to be processed.  This is a bit mask where each
 * bit represents one interrupt, so a maximum of 32 interrupts can be simulated. */
static volatile uint32_t ulPendingInterrupts = 0UL;
/* An event used to inform the simulated interrupt processing thread (a high
 * priority thread that simulated interrupt processing) that an interrupt is
 * pending. */
static void * pvInterruptEvent = NULL;
/* Mutex used to protect all the simulated interrupt variables that are accessed
 * by multiple threads. */
static void * pvInterruptEventMutex = NULL;
/* The critical nesting count for the currently executing task.  This is
 * initialised to a non-zero value so interrupts do not become enabled during
 * the initialisation phase.  As each task has its own critical nesting value
 * ulCriticalNesting will get set to zero when the first task runs.  This
 * initialisation is probably not critical in this simulated environment as the
 * simulated interrupt handlers do not get created until the FreeRTOS scheduler is
 * started anyway. */
static volatile uint32_t ulCriticalNesting = 9999UL;
/* Handlers for all the simulated software interrupts.  The first two positions
 * are used for the Yield and Tick interrupts so are handled slightly differently,
 * all the other interrupts can be user defined. */
static uint32_t (* ulIsrHandler[ portMAX_INTERRUPTS ])( void ) = { 0 };
/* Pointer to the TCB of the currently executing task. */
extern void * volatile CurrentTCB;
/* Used to ensure nothing is processed during the startup sequence. */
static BaseType_t xPortRunning = false;

static DWORD WINAPI SimulatedPeripheralTimer( LPVOID lpParameter )
{
    TickType_t xMinimumWindowsBlockTime;
    TIMECAPS xTimeCaps;
    /* Set the timer resolution to the maximum possible. */
    if( timeGetDevCaps( &xTimeCaps, sizeof( xTimeCaps ) ) == MMSYSERR_NOERROR )
    {
        xMinimumWindowsBlockTime = ( TickType_t ) xTimeCaps.wPeriodMin;
        timeBeginPeriod( xTimeCaps.wPeriodMin );
        /* Register an exit handler so the timeBeginPeriod() function can be
         * matched with a timeEndPeriod() when the application exits. */
        SetConsoleCtrlHandler( EndProcess, TRUE );
    }
    else
    {
        xMinimumWindowsBlockTime = ( TickType_t ) 20;
    }
    /* Just to prevent compiler warnings. */
    ( void ) lpParameter;
    while( xPortRunning  )
    {
        /* Wait until the timer expires and we can access the simulated interrupt
         * variables.  *NOTE* this is not a 'real time' way of generating tick
         * events as the next wake time should be relative to the previous wake
         * time, not the time that Sleep() is called.  It is done this way to
         * prevent overruns in this very non real time simulated/emulated
         * environment. */
        if( portTICK_PERIOD_MS < xMinimumWindowsBlockTime )
        {
            Sleep( xMinimumWindowsBlockTime );
        }
        else
        {
            Sleep( portTICK_PERIOD_MS );
        }
        vPortGenerateSimulatedInterruptFromWindowsThread( portINTERRUPT_TICK );
    }
    return 0;
}

static BOOL WINAPI EndProcess( DWORD dwCtrlType )
{
    TIMECAPS xTimeCaps;
    ( void ) dwCtrlType;
    if( timeGetDevCaps( &xTimeCaps, sizeof( xTimeCaps ) ) == MMSYSERR_NOERROR )
    {
        /* Match the call to timeBeginPeriod( xTimeCaps.wPeriodMin ) made when
         * the process started with a timeEndPeriod() as the process exits. */
        timeEndPeriod( xTimeCaps.wPeriodMin );
    }
    return false;
}

StackType_t * PortInitialiseStack( StackType_t * StackTop,
                                     TaskFunction_t Code,
                                     void * Params )
{
    ThreadState_t * ThreadState = NULL;
    int8_t * pcTopOfStack = ( int8_t * ) StackTop;
    const SIZE_T xStackSize = 1024; /* Set the size to a small number which will get rounded up to the minimum possible. */
    /* In this simulated case a stack is not initialised, but instead a thread
     * is created that will execute the task being created.  The thread handles
     * the context switching itself.  The ThreadState_t object is placed onto
     * the stack that was created for the task - so the stack buffer is still
     * used, just not in the conventional way.  It will not be used for anything
     * other than holding this structure. */
    ThreadState = ( ThreadState_t * ) ( pcTopOfStack - sizeof( ThreadState_t ) );
    /* Create the event used to prevent the thread from executing past its yield
     * point if the SuspendThread() call that suspends the thread does not take
     * effect immediately (it is an asynchronous call). */
    ThreadState->pvYieldEvent = CreateEvent( NULL,   /* Default security attributes. */
                                               FALSE,  /* Auto reset. */
                                               FALSE,  /* Start not signalled. */
                                               NULL ); /* No name. */

#ifdef __GNUC__
    /* GCC reports the warning for the cast operation from TaskFunction_t to LPTHREAD_START_ROUTINE. */
    /* Disable this warning here by the #pragma option. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
    /* Create the thread itself. */
    ThreadState->pvThread = CreateThread( NULL, xStackSize, ( LPTHREAD_START_ROUTINE ) Code, Params, CREATE_SUSPENDED | STACK_SIZE_PARAM_IS_A_RESERVATION, NULL );
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
    configASSERT( ThreadState->pvThread ); /* See comment where TerminateThread() is called. */
    SetThreadAffinityMask( ThreadState->pvThread, 0x01 );
    SetThreadPriorityBoost( ThreadState->pvThread, TRUE );
    SetThreadPriority( ThreadState->pvThread, portTASK_THREAD_PRIORITY );
    return ( StackType_t * ) ThreadState;
}

BaseType_t xPortStartScheduler( void )
{
    void * pvHandle = NULL;
    int32_t lSuccess;
    ThreadState_t * ThreadState = NULL;
    SYSTEM_INFO xSystemInfo;
    /* This port runs windows threads with extremely high priority.  All the
     * threads execute on the same core - to prevent locking up the host only start
     * if the host has multiple cores. */
    GetSystemInfo( &xSystemInfo );
    if( xSystemInfo.dwNumberOfProcessors <= 1 )
    {
        printf( "This version of the FreeRTOS Windows port can only be used on multi-core hosts.\r\n" );
        lSuccess = false;
    }
    else
    {
        lSuccess = true;
        /* The highest priority class is used to [try to] prevent other Windows
         * activity interfering with FreeRTOS timing too much. */
        if( SetPriorityClass( GetCurrentProcess(), REALTIME_PRIORITY_CLASS ) == 0 )
        {
            printf( "SetPriorityClass() failed\r\n" );
        }
        /* Install the interrupt handlers used by the scheduler itself. */
        vPortSetInterruptHandler( portINTERRUPT_YIELD, ProcessYieldInterrupt );
        vPortSetInterruptHandler( portINTERRUPT_TICK, ProcessTickInterrupt );
        /* Create the events and mutexes that are used to synchronise all the
         * threads. */
        pvInterruptEventMutex = CreateMutex( NULL, FALSE, NULL );
        pvInterruptEvent = CreateEvent( NULL, FALSE, FALSE, NULL );
        if( ( pvInterruptEventMutex == NULL ) || ( pvInterruptEvent == NULL ) )
        {
            lSuccess = false;
        }
        /* Set the priority of this thread such that it is above the priority of
         * the threads that run tasks.  This higher priority is required to ensure
         * simulated interrupts take priority over tasks. */
        pvHandle = GetCurrentThread();
        if( pvHandle == NULL )
        {
            lSuccess = false;
        }
    }
    if( lSuccess  )
    {
        if( SetThreadPriority( pvHandle, portSIMULATED_INTERRUPTS_THREAD_PRIORITY ) == 0 )
        {
            lSuccess = false;
        }
        SetThreadPriorityBoost( pvHandle, TRUE );
        SetThreadAffinityMask( pvHandle, 0x01 );
    }
    if( lSuccess  )
    {
        /* Start the thread that simulates the timer peripheral to generate
         * tick interrupts.  The priority is set below that of the simulated
         * interrupt handler so the interrupt event mutex is used for the
         * handshake / overrun protection. */
        pvHandle = CreateThread( NULL, 0, SimulatedPeripheralTimer, NULL, CREATE_SUSPENDED, NULL );
        if( pvHandle != NULL )
        {
            SetThreadPriority( pvHandle, portSIMULATED_TIMER_THREAD_PRIORITY );
            SetThreadPriorityBoost( pvHandle, TRUE );
            SetThreadAffinityMask( pvHandle, 0x01 );
            ResumeThread( pvHandle );
        }
        /* Start the highest priority task by obtaining its associated thread
         * state structure, in which is stored the thread handle. */
        ThreadState = ( ThreadState_t * ) *( ( size_t * ) CurrentTCB );
        ulCriticalNesting = portNO_CRITICAL_NESTING;
        /* The scheduler is now running. */
        xPortRunning = true;
        /* Start the first task. */
        ResumeThread( ThreadState->pvThread );
        /* Handle all simulated interrupts - including yield requests and
         * simulated ticks. */
        ProcessSimulatedInterrupts();
    }
    /* Would not expect to return from ProcessSimulatedInterrupts(), so should
     * not get here. */
    return 0;
}

static uint32_t ProcessYieldInterrupt( void )
{
    /* Always return true as this is a yield. */
    return true;
}

static uint32_t ProcessTickInterrupt( void )
{
    uint32_t ulSwitchRequired;
    /* Process the tick itself. */
        ulSwitchRequired = ( uint32_t ) TaskIncrementTick();
    return ulSwitchRequired;
}

static void ProcessSimulatedInterrupts( void )
{
    uint32_t ulSwitchRequired, i;
    ThreadState_t * ThreadState;
    void * pvObjectList[ 2 ];
    CONTEXT xContext;
    DWORD xWinApiResult;
    const DWORD xTimeoutMilliseconds = 1000;
    /* Going to block on the mutex that ensured exclusive access to the simulated
     * interrupt objects, and the event that signals that a simulated interrupt
     * should be processed. */
    pvObjectList[ 0 ] = pvInterruptEventMutex;
    pvObjectList[ 1 ] = pvInterruptEvent;
    /* Create a pending tick to ensure the first task is started as soon as
     * this thread pends. */
    ulPendingInterrupts |= ( 1 << portINTERRUPT_TICK );
    SetEvent( pvInterruptEvent );
    while( xPortRunning  )
    {
        xInsideInterrupt = false;
        /* Wait with timeout so that we can exit from this loop when
         * the scheduler is stopped by calling vPortEndScheduler. */
        xWinApiResult = WaitForMultipleObjects( sizeof( pvObjectList ) / sizeof( void * ), pvObjectList, TRUE, xTimeoutMilliseconds );
        if( xWinApiResult != WAIT_TIMEOUT )
        {
            /* Cannot be in a critical section to get here.  Tasks that exit a
             * critical section will block on a yield mutex to wait for an interrupt to
             * process if an interrupt was set pending while the task was inside the
             * critical section.  xInsideInterrupt prevents interrupts that contain
             * critical sections from doing the same. */
            xInsideInterrupt = true;
            /* Used to indicate whether the simulated interrupt processing has
             * necessitated a context switch to another task/thread. */
            ulSwitchRequired = false;
            /* For each interrupt we are interested in processing, each of which is
             * represented by a bit in the 32bit ulPendingInterrupts variable. */
            for( i = 0; i < portMAX_INTERRUPTS; i++ )
            {
                /* Is the simulated interrupt pending? */
                if( ( ulPendingInterrupts & ( 1UL << i ) ) != 0 )
                {
                    /* Is a handler installed? */
                    if( ulIsrHandler[ i ] != NULL )
                    {
                        /* Run the actual handler.  Handlers return true if they
                         * necessitate a context switch. */
                        if( ulIsrHandler[ i ]() != false )
                        {
                            /* A bit mask is used purely to help debugging. */
                            ulSwitchRequired |= ( 1 << i );
                        }
                    }
                    /* Clear the interrupt pending bit. */
                    ulPendingInterrupts &= ~( 1UL << i );
                }
            }
            if( ulSwitchRequired != false )
            {
                /* Suspend the old thread. */
                ThreadState = ( ThreadState_t * ) *( ( size_t * ) CurrentTCB );
                SuspendThread( ThreadState->pvThread );
                /* Ensure the thread is actually suspended by performing a
                 * synchronous operation that can only complete when the thread
                 * is actually suspended. The below code asks for dummy register
                 * data. Experimentation shows that these two lines don't appear
                 * to do anything now, but according to
                 * https://devblogs.microsoft.com/oldnewthing/20150205-00/?p=44743
                 * they do - so as they do not harm (slight run-time hit). */
                xContext.ContextFlags = CONTEXT_INTEGER;
                ( void ) GetThreadContext( ThreadState->pvThread, &xContext );
                /* Select the next task to run. */
                TaskSwitchContext();
                /* Obtain the state of the task now selected to enter the
                 * Running state. */
                ThreadState = ( ThreadState_t * ) ( *( size_t * ) CurrentTCB );
                /* ThreadState->pvThread can be NULL if the task deleted
                 * itself - but a deleted task should never be resumed here. */
                                ResumeThread( ThreadState->pvThread );
            }
            /* If the thread that is about to be resumed stopped running
             * because it yielded then it will wait on an event when it resumed
             * (to ensure it does not continue running after the call to
             * SuspendThread() above as SuspendThread() is asynchronous).
             * Signal the event to ensure the thread can proceed now it is
             * valid for it to do so.  Signaling the event is benign in the case that
             * the task was switched out asynchronously by an interrupt as the event
             * is reset before the task blocks on it. */
            ThreadState = ( ThreadState_t * ) ( *( size_t * ) CurrentTCB );
            SetEvent( ThreadState->pvYieldEvent );
            ReleaseMutex( pvInterruptEventMutex );
        }
    }
}

void vPortDeleteThread( void * TaskToDelete )
{
    ThreadState_t * ThreadState;
    uint32_t ulErrorCode;
    /* Remove compiler warnings if configASSERT() is not defined. */
    ( void ) ulErrorCode;
    /* Find the handle of the thread being deleted. */
    ThreadState = ( ThreadState_t * ) ( *( size_t * ) TaskToDelete );
    /* Check that the thread is still valid, it might have been closed by
     * vPortCloseRunningThread() - which will be the case if the task associated
     * with the thread originally deleted itself rather than being deleted by a
     * different task. */
    if( ThreadState->pvThread != NULL )
    {
        WaitForSingleObject( pvInterruptEventMutex, INFINITE );
        /* !!! This is not a nice way to terminate a thread, and will eventually
         * result in resources being depleted if tasks frequently delete other
         * tasks (rather than deleting themselves) as the task stacks will not be
         * freed. */
        ulErrorCode = TerminateThread( ThreadState->pvThread, 0 );
                ulErrorCode = CloseHandle( ThreadState->pvThread );
                ReleaseMutex( pvInterruptEventMutex );
    }
}

void vPortCloseRunningThread( void * TaskToDelete,
                              volatile BaseType_t * PendYield )
{
    ThreadState_t * ThreadState;
    void * pvThread;
    uint32_t ulErrorCode;
    /* Remove compiler warnings if configASSERT() is not defined. */
    ( void ) ulErrorCode;
    /* Find the handle of the thread being deleted. */
    ThreadState = ( ThreadState_t * ) ( *( size_t * ) TaskToDelete );
    pvThread = ThreadState->pvThread;
    /* Raise the Windows priority of the thread to ensure the FreeRTOS scheduler
     * does not run and swap it out before it is closed.  If that were to happen
     * the thread would never run again and effectively be a thread handle and
     * memory leak. */
    SetThreadPriority( pvThread, portDELETE_SELF_THREAD_PRIORITY );
    /* This function will not return, therefore a yield is set as pending to
     * ensure a context switch occurs away from this thread on the next tick. */
    *PendYield = true;
    /* Mark the thread associated with this task as invalid so
     * vPortDeleteThread() does not try to terminate it. */
    ThreadState->pvThread = NULL;
    /* Close the thread. */
    ulErrorCode = CloseHandle( pvThread );
        /* This is called from a critical section, which must be exited before the
     * thread stops. */
    EXIT_CRITICAL();
    /* Record that a yield is pending so that the next tick interrupt switches
     * out this thread regardless of the value of configUSE_PREEMPTION. This is
     * needed when a task deletes itself - the taskYIELD_WITHIN_API within
     * TaskDelete does not get called because this function never returns. If
     * we do not pend portINTERRUPT_YIELD here, the next task is not scheduled
     * when configUSE_PREEMPTION is set to 0. */
    if( pvInterruptEventMutex != NULL )
    {
        WaitForSingleObject( pvInterruptEventMutex, INFINITE );
        ulPendingInterrupts |= ( 1 << portINTERRUPT_YIELD );
        ReleaseMutex( pvInterruptEventMutex );
    }
    CloseHandle( ThreadState->pvYieldEvent );
    ExitThread( 0 );
}

void vPortEndScheduler( void )
{
    xPortRunning = false;
}

void vPortGenerateSimulatedInterrupt( uint32_t ulInterruptNumber )
{
    ThreadState_t * ThreadState = ( ThreadState_t * ) *( ( size_t * ) CurrentTCB );
        if( ( ulInterruptNumber < portMAX_INTERRUPTS ) && ( pvInterruptEventMutex != NULL ) )
    {
        WaitForSingleObject( pvInterruptEventMutex, INFINITE );
        ulPendingInterrupts |= ( 1 << ulInterruptNumber );
        /* The simulated interrupt is now held pending, but don't actually
         * process it yet if this call is within a critical section.  It is
         * possible for this to be in a critical section as calls to wait for
         * mutexes are accumulative.  If in a critical section then the event
         * will get set when the critical section nesting count is wound back
         * down to zero. */
        if( ulCriticalNesting == portNO_CRITICAL_NESTING )
        {
            SetEvent( pvInterruptEvent );
            /* Going to wait for an event - make sure the event is not already
             * signaled. */
            ResetEvent( ThreadState->pvYieldEvent );
        }
        ReleaseMutex( pvInterruptEventMutex );
        if( ulCriticalNesting == portNO_CRITICAL_NESTING )
        {
            /* An interrupt was pended so ensure to block to allow it to
             * execute.  In most cases the (simulated) interrupt will have
             * executed before the next line is reached - so this is just to make
             * sure. */
            WaitForSingleObject( ThreadState->pvYieldEvent, INFINITE );
        }
    }
}

void vPortGenerateSimulatedInterruptFromWindowsThread( uint32_t ulInterruptNumber )
{
    if( xPortRunning  )
    {
        /* Can't proceed if in a critical section as pvInterruptEventMutex won't
         * be available. */
        WaitForSingleObject( pvInterruptEventMutex, INFINITE );
        /* Pending a user defined interrupt to be handled in simulated interrupt
         * handler thread. */
        ulPendingInterrupts |= ( 1 << ulInterruptNumber );
        /* The interrupt is now pending - notify the simulated interrupt
         * handler thread.  Must be outside of a critical section to get here so
         * the handler thread can execute immediately pvInterruptEventMutex is
         * released. */
                SetEvent( pvInterruptEvent );
        /* Give back the mutex so the simulated interrupt handler unblocks
         * and can access the interrupt handler variables. */
        ReleaseMutex( pvInterruptEventMutex );
    }
}

void vPortSetInterruptHandler( uint32_t ulInterruptNumber,
                               uint32_t ( * pvHandler )( void ) )
{
    if( ulInterruptNumber < portMAX_INTERRUPTS )
    {
        if( pvInterruptEventMutex != NULL )
        {
            WaitForSingleObject( pvInterruptEventMutex, INFINITE );
            ulIsrHandler[ ulInterruptNumber ] = pvHandler;
            ReleaseMutex( pvInterruptEventMutex );
        }
        else
        {
            ulIsrHandler[ ulInterruptNumber ] = pvHandler;
        }
    }
}

void vPortEnterCritical( void )
{
    if( xPortRunning  )
    {
        /* The interrupt event mutex is held for the entire critical section,
         * effectively disabling (simulated) interrupts. */
        WaitForSingleObject( pvInterruptEventMutex, INFINITE );
    }
    ulCriticalNesting++;
}

void vPortExitCritical( void )
{
    int32_t lMutexNeedsReleasing;
    /* The interrupt event mutex should already be held by this thread as it was
     * obtained on entry to the critical section. */
    lMutexNeedsReleasing = true;
    if( ulCriticalNesting > portNO_CRITICAL_NESTING )
    {
        ulCriticalNesting--;
        /* Don't need to wait for any pending interrupts to execute if the
         * critical section was exited from inside an interrupt. */
        if( ( ulCriticalNesting == portNO_CRITICAL_NESTING ) && ( xInsideInterrupt == false ) )
        {
            /* Were any interrupts set to pending while interrupts were
             * (simulated) disabled? */
            if( ulPendingInterrupts != 0UL )
            {
                ThreadState_t * ThreadState = ( ThreadState_t * ) *( ( size_t * ) CurrentTCB );
                                /* The interrupt won't actually executed until
                 * pvInterruptEventMutex is released as it waits on both
                 * pvInterruptEventMutex and pvInterruptEvent.
                 * pvInterruptEvent is only set when the simulated
                 * interrupt is pended if the interrupt is pended
                 * from outside a critical section - hence it is set
                 * here. */
                SetEvent( pvInterruptEvent );
                /* The calling task is going to wait for an event to ensure the
                 * interrupt that is pending executes immediately after the
                 * critical section is exited - so make sure the event is not
                 * already signaled. */
                ResetEvent( ThreadState->pvYieldEvent );
                /* Mutex will be released now so the (simulated) interrupt can
                 * execute, so does not require releasing on function exit. */
                lMutexNeedsReleasing = false;
                ReleaseMutex( pvInterruptEventMutex );
                WaitForSingleObject( ThreadState->pvYieldEvent, INFINITE );
            }
        }
    }
    if( pvInterruptEventMutex != NULL )
    {
        if( lMutexNeedsReleasing  )
        {
                        ReleaseMutex( pvInterruptEventMutex );
        }
    }
}
