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

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.hpp"
#include "timers.h"
#include "event_groups.h"

typedef struct EventGroupDef_t
{
    EventBits_t uxEventBits;
    List_t<TCB_t> xTasksWaitingForBits; /**< List of tasks waiting for a bit to be set. */
    #if ( ( configSUPPORT_STATIC_ALLOCATION == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
        uint8_t StaticallyAllocated; /**< Set to true if the event group is statically allocated to ensure no attempt is made to free the memory. */
    #endif
} EventGroup_t;

/*
* Test the bits set in uxCurrentEventBits to see if the wait condition is met.
* The wait condition is defined by xWaitForAllBits.  If xWaitForAllBits is
* true then the wait condition is met if all the bits set in uxBitsToWaitFor
* are also set in uxCurrentEventBits.  If xWaitForAllBits is false then the
* wait condition is met if any of the bits set in uxBitsToWait for are also set
* in uxCurrentEventBits.
*/
static BaseType_t TestWaitCondition( const EventBits_t uxCurrentEventBits,
                                        const EventBits_t uxBitsToWaitFor,
                                        const BaseType_t xWaitForAllBits ) ;

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
    EventGroupHandle_t xEventGroupCreateStatic( StaticEventGroup_t * pxEventGroupBuffer )
    {
        EventGroup_t * pxEventBits;
        /* A StaticEventGroup_t object must be provided. */
        configASSERT( pxEventGroupBuffer );
        #if ( configASSERT_DEFINED == 1 )
        {
            /* Sanity check that the size of the structure used to declare a
                * variable of type StaticEventGroup_t equals the size of the real
                * event group structure. */
            volatile size_t xSize = sizeof( StaticEventGroup_t );
            configASSERT( xSize == sizeof( EventGroup_t ) );
        }
        #endif /* configASSERT_DEFINED */
        pxEventBits = ( EventGroup_t * ) pxEventGroupBuffer;
        if( pxEventBits != NULL )
        {
            pxEventBits->uxEventBits = 0;
            ListInitialise( &( pxEventBits->xTasksWaitingForBits ) );
            #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
            {
                /* Both static and dynamic allocation can be used, so note that
                    * this event group was created statically in case the event group
                    * is later deleted. */
                pxEventBits->StaticallyAllocated = true;
            }
            #endif /* configSUPPORT_DYNAMIC_ALLOCATION */
        }
        return pxEventBits;
    }
#endif /* configSUPPORT_STATIC_ALLOCATION */

#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
    EventGroupHandle_t xEventGroupCreate( void )
    {
        EventGroup_t * pxEventBits;
        pxEventBits = ( EventGroup_t * ) pvPortMalloc( sizeof( EventGroup_t ) );
        if( pxEventBits != NULL )
        {
            pxEventBits->uxEventBits = 0;
            ListInitialise( &( pxEventBits->xTasksWaitingForBits ) );
            #if ( configSUPPORT_STATIC_ALLOCATION == 1 )
            {
                /* Both static and dynamic allocation can be used, so note this
                    * event group was allocated statically in case the event group is
                    * later deleted. */
                pxEventBits->StaticallyAllocated = false;
            }
            #endif /* configSUPPORT_STATIC_ALLOCATION */
        }
        return pxEventBits;
    }
#endif /* configSUPPORT_DYNAMIC_ALLOCATION */

EventBits_t xEventGroupSync( EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToSet,
                                const EventBits_t uxBitsToWaitFor,
                                TickType_t xTicksToWait )
{
    EventBits_t uxOriginalBitValue, uxReturn;
    EventGroup_t * pxEventBits = xEventGroup;
    BaseType_t xAlreadyYielded;
    BaseType_t xTimeoutOccurred = false;
    configASSERT( ( uxBitsToWaitFor & EVENT_BITS_CONTROL_BYTES ) == 0 );
    configASSERT( uxBitsToWaitFor != 0 );
    #if ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) )
    {
        configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) );
    }
    #endif
    vTaskSuspendAll();
    {
        uxOriginalBitValue = pxEventBits->uxEventBits;
        ( void ) xEventGroupSetBits( xEventGroup, uxBitsToSet );
        if( ( ( uxOriginalBitValue | uxBitsToSet ) & uxBitsToWaitFor ) == uxBitsToWaitFor )
        {
            /* All the rendezvous bits are now set - no need to block. */
            uxReturn = ( uxOriginalBitValue | uxBitsToSet );
            /* Rendezvous always clear the bits.  They will have been cleared
                * already unless this is the only task in the rendezvous. */
            pxEventBits->uxEventBits &= ~uxBitsToWaitFor;
            xTicksToWait = 0;
        }
        else
        {
            if( xTicksToWait != ( TickType_t ) 0 )
            {
                /* Store the bits that the calling task is waiting for in the
                    * task's event list item so the kernel knows when a match is
                    * found.  Then enter the blocked state. */
                vTaskPlaceOnUnorderedEventList( &( pxEventBits->xTasksWaitingForBits ), ( uxBitsToWaitFor | eventCLEAR_EVENTS_ON_EXIT_BIT | WAIT_FOR_ALL_BITS ), xTicksToWait );
                /* This assignment is obsolete as uxReturn will get set after
                    * the task unblocks, but some compilers mistakenly generate a
                    * warning about uxReturn being returned without being set if the
                    * assignment is omitted. */
                uxReturn = 0;
            }
            else
            {
                /* The rendezvous bits were not set, but no block time was
                    * specified - just return the current event bit value. */
                uxReturn = pxEventBits->uxEventBits;
                xTimeoutOccurred = true;
            }
        }
    }
    xAlreadyYielded = TaskResumeAll();
    if( xTicksToWait != ( TickType_t ) 0 )
    {
        if( xAlreadyYielded == false )
        {
            taskYIELD_WITHIN_API();
        }
        /* The task blocked to wait for its required bits to be set - at this
            * point either the required bits were set or the block time expired.  If
            * the required bits were set they will have been stored in the task's
            * event list item, and they should now be retrieved then cleared. */
        uxReturn = uxTaskResetEventItemValue();
        if( ( uxReturn & eventUNBLOCKED_DUE_TO_BIT_SET ) == ( EventBits_t ) 0 )
        {
            /* The task timed out, just return the current event bit value. */
            ENTER_CRITICAL();
            {
                uxReturn = pxEventBits->uxEventBits;
                /* Although the task got here because it timed out before the
                    * bits it was waiting for were set, it is possible that since it
                    * unblocked another task has set the bits.  If this is the case
                    * then it needs to clear the bits before exiting. */
                if( ( uxReturn & uxBitsToWaitFor ) == uxBitsToWaitFor )
                {
                    pxEventBits->uxEventBits &= ~uxBitsToWaitFor;
                }
            }
            EXIT_CRITICAL();
            xTimeoutOccurred = true;
        }
        /* Control bits might be set as the task had blocked should not be
            * returned. */
        uxReturn &= ~EVENT_BITS_CONTROL_BYTES;
    }
    return uxReturn;
}

EventBits_t xEventGroupWaitBits( EventGroupHandle_t xEventGroup,
                                    const EventBits_t uxBitsToWaitFor,
                                    const BaseType_t xClearOnExit,
                                    const BaseType_t xWaitForAllBits,
                                    TickType_t xTicksToWait )
{
    EventGroup_t * pxEventBits = xEventGroup;
    EventBits_t uxReturn, uxControlBits = 0;
    BaseType_t xWaitConditionMet, xAlreadyYielded;
    BaseType_t xTimeoutOccurred = false;
    /* Check the user is not attempting to wait on the bits used by the kernel
        * itself, and that at least one bit is being requested. */
    configASSERT( xEventGroup );
    configASSERT( ( uxBitsToWaitFor & EVENT_BITS_CONTROL_BYTES ) == 0 );
    configASSERT( uxBitsToWaitFor != 0 );
    #if ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) )
    {
        configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) );
    }
    #endif
    vTaskSuspendAll();
    {
        const EventBits_t uxCurrentEventBits = pxEventBits->uxEventBits;
        /* Check to see if the wait condition is already met or not. */
        xWaitConditionMet = TestWaitCondition( uxCurrentEventBits, uxBitsToWaitFor, xWaitForAllBits );
        if( xWaitConditionMet != false )
        {
            /* The wait condition has already been met so there is no need to
                * block. */
            uxReturn = uxCurrentEventBits;
            xTicksToWait = ( TickType_t ) 0;
            /* Clear the wait bits if requested to do so. */
            if( xClearOnExit != false )
            {
                pxEventBits->uxEventBits &= ~uxBitsToWaitFor;
            }
        }
        else if( xTicksToWait == ( TickType_t ) 0 )
        {
            /* The wait condition has not been met, but no block time was
                * specified, so just return the current value. */
            uxReturn = uxCurrentEventBits;
            xTimeoutOccurred = true;
        }
        else
        {
            /* The task is going to block to wait for its required bits to be
                * set.  uxControlBits are used to remember the specified behaviour of
                * this call to xEventGroupWaitBits() - for use when the event bits
                * unblock the task. */
            if( xClearOnExit != false )
            {
                uxControlBits |= eventCLEAR_EVENTS_ON_EXIT_BIT;
            }
            if( xWaitForAllBits != false )
            {
                uxControlBits |= WAIT_FOR_ALL_BITS;
            }
            /* Store the bits that the calling task is waiting for in the
                * task's event list item so the kernel knows when a match is
                * found.  Then enter the blocked state. */
            vTaskPlaceOnUnorderedEventList( &( pxEventBits->xTasksWaitingForBits ), ( uxBitsToWaitFor | uxControlBits ), xTicksToWait );
            /* This is obsolete as it will get set after the task unblocks, but
                * some compilers mistakenly generate a warning about the variable
                * being returned without being set if it is not done. */
            uxReturn = 0;
        }
    }
    xAlreadyYielded = TaskResumeAll();
    if( xTicksToWait != ( TickType_t ) 0 )
    {
        if( xAlreadyYielded == false )
        {
            taskYIELD_WITHIN_API();
        }
        /* The task blocked to wait for its required bits to be set - at this
            * point either the required bits were set or the block time expired.  If
            * the required bits were set they will have been stored in the task's
            * event list item, and they should now be retrieved then cleared. */
        uxReturn = uxTaskResetEventItemValue();
        if( ( uxReturn & eventUNBLOCKED_DUE_TO_BIT_SET ) == ( EventBits_t ) 0 )
        {
            ENTER_CRITICAL();
            {
                /* The task timed out, just return the current event bit value. */
                uxReturn = pxEventBits->uxEventBits;
                /* It is possible that the event bits were updated between this
                    * task leaving the Blocked state and running again. */
                if( TestWaitCondition( uxReturn, uxBitsToWaitFor, xWaitForAllBits ) != false )
                {
                    if( xClearOnExit != false )
                    {
                        pxEventBits->uxEventBits &= ~uxBitsToWaitFor;
                    }
                }
                xTimeoutOccurred = true;
            }
        }
        uxReturn &= ~EVENT_BITS_CONTROL_BYTES;
    }
    return uxReturn;
}

EventBits_t xEventGroupClearBits( EventGroupHandle_t xEventGroup,
                                    const EventBits_t uxBitsToClear )
{
    EventGroup_t * pxEventBits = xEventGroup;
    EventBits_t uxReturn;
    /* Check the user is not attempting to clear the bits used by the kernel
        * itself. */
    configASSERT( xEventGroup );
    configASSERT( ( uxBitsToClear & EVENT_BITS_CONTROL_BYTES ) == 0 );
    ENTER_CRITICAL();
    {
        /* The value returned is the event group value prior to the bits being
            * cleared. */
        uxReturn = pxEventBits->uxEventBits;
        /* Clear the bits. */
        pxEventBits->uxEventBits &= ~uxBitsToClear;
    }
    EXIT_CRITICAL();
    return uxReturn;
}
EventBits_t xEventGroupGetBitsFromISR( EventGroupHandle_t xEventGroup )
{
    UBaseType_t uxSavedInterruptStatus = ENTER_CRITICAL_FROM_ISR();
    EventBits_t uxReturn = xEventGroup->uxEventBits;
    EXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );
    return uxReturn;
}
EventBits_t xEventGroupSetBits( EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet )
{
    ListItem_t<TCB_t> * pxListItem;
    ListItem_t<TCB_t> * pxNext;
    EventBits_t uxBitsToClear = 0, uxBitsWaitedFor, uxControlBits, uxReturnBits;
    EventGroup_t * pxEventBits = xEventGroup;
    BaseType_t xMatchFound = false;
    /* Check the user is not attempting to set the bits used by the kernel
        * itself. */
    configASSERT( xEventGroup );
    configASSERT( ( uxBitsToSet & EVENT_BITS_CONTROL_BYTES ) == 0 );
    List_t<TCB_t> * pxList = &( pxEventBits->xTasksWaitingForBits );
    ListItem_t<TCB_t> const * pxListEnd = GET_END_MARKER( pxList );
    vTaskSuspendAll();
    {
        pxListItem = GET_HEAD_ENTRY( pxList );
        /* Set the bits. */
        pxEventBits->uxEventBits |= uxBitsToSet;
        /* See if the new bit value should unblock any tasks. */
        while( pxListItem != pxListEnd )
        {
            pxNext = GET_NEXT( pxListItem );
            uxBitsWaitedFor = GET_LIST_ITEM_VALUE( pxListItem );
            xMatchFound = false;
            /* Split the bits waited for from the control bits. */
            uxControlBits = uxBitsWaitedFor & EVENT_BITS_CONTROL_BYTES;
            uxBitsWaitedFor &= ~EVENT_BITS_CONTROL_BYTES;
            if( ( uxControlBits & WAIT_FOR_ALL_BITS ) == ( EventBits_t ) 0 )
            {
                /* Just looking for single bit being set. */
                if( ( uxBitsWaitedFor & pxEventBits->uxEventBits ) != ( EventBits_t ) 0 )
                {
                    xMatchFound = true;
                }
            }
            else if( ( uxBitsWaitedFor & pxEventBits->uxEventBits ) == uxBitsWaitedFor )
            {
                /* All bits are set. */
                xMatchFound = true;
            }
            if( !xMatchFound)
            {
                /* The bits match.  Should the bits be cleared on exit? */
                if( ( uxControlBits & eventCLEAR_EVENTS_ON_EXIT_BIT ) != ( EventBits_t ) 0 )
                {
                    uxBitsToClear |= uxBitsWaitedFor;
                }
                /* Store the actual event flag value in the task's event list
                    * item before removing the task from the event list.  The
                    * eventUNBLOCKED_DUE_TO_BIT_SET bit is set so the task knows
                    * that is was unblocked due to its required bits matching, rather
                    * than because it timed out. */
                vTaskRemoveFromUnorderedEventList( pxListItem, pxEventBits->uxEventBits | eventUNBLOCKED_DUE_TO_BIT_SET );
            }
            /* Move onto the next list item.  Note pxListItem->pxNext is not
                * used here as the list item may have been removed from the event list
                * and inserted into the ready/pending reading list. */
            pxListItem = pxNext;
        }
        /* Clear any bits that matched when the eventCLEAR_EVENTS_ON_EXIT_BIT
            * bit was set in the control word. */
        pxEventBits->uxEventBits &= ~uxBitsToClear;
        /* Snapshot resulting bits. */
        uxReturnBits = pxEventBits->uxEventBits;
    }
    ( void ) TaskResumeAll();
    return uxReturnBits;
}

void vEventGroupDelete( EventGroupHandle_t xEventGroup )
{
    EventGroup_t * pxEventBits = xEventGroup;
    configASSERT( pxEventBits );
    List_t<TCB_t> *pxTasksWaitingForBits = &( pxEventBits->xTasksWaitingForBits );
    vTaskSuspendAll();
    {
        while( CURRENT_LIST_LENGTH( pxTasksWaitingForBits ) > ( UBaseType_t ) 0 )
        {
            vTaskRemoveFromUnorderedEventList( pxTasksWaitingForBits->xListEnd.pxNext, eventUNBLOCKED_DUE_TO_BIT_SET );
        }
    }
    ( void ) TaskResumeAll();
    #if ( ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) && ( configSUPPORT_STATIC_ALLOCATION == 0 ) )
    {
        /* The event group can only have been allocated dynamically - free
            * it again. */
        vPortFree( pxEventBits );
    }
    #elif ( ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) && ( configSUPPORT_STATIC_ALLOCATION == 1 ) )
    {
        /* The event group could have been allocated statically or
            * dynamically, so check before attempting to free the memory. */
        if( pxEventBits->StaticallyAllocated == ( uint8_t ) false )
        {
            vPortFree( pxEventBits );
        }
    }
    #endif /* configSUPPORT_DYNAMIC_ALLOCATION */
}

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
    BaseType_t xEventGroupGetStaticBuffer( EventGroupHandle_t xEventGroup,
                                            StaticEventGroup_t ** ppxEventGroupBuffer )
    {
        BaseType_t xReturn;
        EventGroup_t * pxEventBits = xEventGroup;
        configASSERT( pxEventBits );
        configASSERT( ppxEventGroupBuffer );
        /* Check if the event group was statically allocated. */
        if( pxEventBits->StaticallyAllocated == ( uint8_t ) true )
        {
            *ppxEventGroupBuffer = ( StaticEventGroup_t * ) pxEventBits;
            xReturn = true;
        }
        else
        {
            xReturn = false;
        }
        return xReturn;
    }
#endif /* configSUPPORT_STATIC_ALLOCATION */

/* For internal use only - execute a 'set bits' command that was pended from
* an interrupt. */
void vEventGroupSetBitsCallback( void * pvEventGroup,
                                    uint32_t ulBitsToSet )
{
    ( void ) xEventGroupSetBits( (EventGroupHandle_t)pvEventGroup, ( EventBits_t ) ulBitsToSet );
}

/* For internal use only - execute a 'clear bits' command that was pended from
* an interrupt. */
void vEventGroupClearBitsCallback( void * pvEventGroup,
                                    uint32_t ulBitsToClear )
{
    ( void ) xEventGroupClearBits( (EventGroupHandle_t)pvEventGroup, ( EventBits_t ) ulBitsToClear );
}

static BaseType_t TestWaitCondition( const EventBits_t uxCurrentEventBits,
                                        const EventBits_t uxBitsToWaitFor,
                                        const BaseType_t xWaitForAllBits )
{
    BaseType_t xWaitConditionMet = false;
    if( xWaitForAllBits == false )
    {
        /* Task only has to wait for one bit within uxBitsToWaitFor to be
            * set.  Is one already set? */
        if( ( uxCurrentEventBits & uxBitsToWaitFor ) != ( EventBits_t ) 0 )
        {
            xWaitConditionMet = true;
        }
    }
    else
    {
        /* Task has to wait for all the bits in uxBitsToWaitFor to be set.
            * Are they set already? */
        if( ( uxCurrentEventBits & uxBitsToWaitFor ) == uxBitsToWaitFor )
        {
            xWaitConditionMet = true;
        }
    }
    return xWaitConditionMet;
}
