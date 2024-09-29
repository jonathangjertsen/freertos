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
/* Defining MPU_WRAPPERS_INCLUDED_FROM_API_FILE prevents task.h from redefining
 * all the API functions to use the MPU wrappers.  That should only be done when
 * task.h is included from an application file. */
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
#include "FreeRTOS.h"
#include "list.h"
/* The MPU ports require MPU_WRAPPERS_INCLUDED_FROM_API_FILE to be
 * defined for the header files above, but not in this file, in order to
 * generate the correct privileged Vs unprivileged linkage and placement. */
#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE
/*-----------------------------------------------------------
* PUBLIC LIST API documented in list.h
*----------------------------------------------------------*/
void ListInitialise( List_t * const List )
{
    /* The list structure contains a list item which is used to mark the
     * end of the list.  To initialise the list the list end is inserted
     * as the only list entry. */
    List->Index = ( ListItem_t * ) &( List->xListEnd );
    SET_FIRST_LIST_ITEM_INTEGRITY_CHECK_VALUE( &( List->xListEnd ) );
    /* The list end value is the highest possible value in the list to
     * ensure it remains at the end of the list. */
    List->xListEnd.xItemValue = portMAX_DELAY;
    /* The list end next and previous pointers point to itself so we know
     * when the list is empty. */
    List->xListEnd.pxNext = ( ListItem_t * ) &( List->xListEnd );
    List->xListEnd.pxPrevious = ( ListItem_t * ) &( List->xListEnd );
    /* Initialize the remaining fields of xListEnd when it is a proper ListItem_t */
    #if ( configUSE_MINI_LIST_ITEM == 0 )
    {
        List->xListEnd.pvOwner = NULL;
        List->xListEnd.pxContainer = NULL;
        SET_SECOND_LIST_ITEM_INTEGRITY_CHECK_VALUE( &( List->xListEnd ) );
    }
    #endif
    List->uxNumberOfItems = ( UBaseType_t ) 0U;
    /* Write known values into the list if
     * configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES is set to 1. */
    SET_LIST_INTEGRITY_CHECK_1_VALUE( List );
    SET_LIST_INTEGRITY_CHECK_2_VALUE( List );
}

void ListInitialiseItem( ListItem_t * const Item )
{
    /* Make sure the list item is not recorded as being on a list. */
    Item->pxContainer = NULL;
    /* Write known values into the list item if
     * configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES is set to 1. */
    SET_FIRST_LIST_ITEM_INTEGRITY_CHECK_VALUE( Item );
    SET_SECOND_LIST_ITEM_INTEGRITY_CHECK_VALUE( Item );
}

void ListInsertEnd( List_t * const List,
                     ListItem_t * const NewListItem )
{
    ListItem_t * const Index = List->Index;
    /* Only effective when configASSERT() is also defined, these tests may catch
     * the list data structures being overwritten in memory.  They will not catch
     * data errors caused by incorrect configuration or use of FreeRTOS. */
    TEST_LIST_INTEGRITY( List );
    TEST_LIST_ITEM_INTEGRITY( NewListItem );
    /* Insert a new list item into List, but rather than sort the list,
     * makes the new list item the last item to be removed by a call to
     * listGET_OWNER_OF_NEXT_ENTRY(). */
    NewListItem->pxNext = Index;
    NewListItem->pxPrevious = Index->pxPrevious;
    Index->pxPrevious->pxNext = NewListItem;
    Index->pxPrevious = NewListItem;
    /* Remember which list the item is in. */
    NewListItem->pxContainer = List;
    ( List->uxNumberOfItems ) = ( UBaseType_t ) ( List->uxNumberOfItems + 1U );
}

void ListInsert( List_t * const List,
                  ListItem_t * const NewListItem )
{
    ListItem_t * pxIterator;
    const TickType_t xValueOfInsertion = NewListItem->xItemValue;
    /* Only effective when configASSERT() is also defined, these tests may catch
     * the list data structures being overwritten in memory.  They will not catch
     * data errors caused by incorrect configuration or use of FreeRTOS. */
    TEST_LIST_INTEGRITY( List );
    TEST_LIST_ITEM_INTEGRITY( NewListItem );
    /* Insert the new list item into the list, sorted in xItemValue order.
     *
     * If the list already contains a list item with the same item value then the
     * new list item should be placed after it.  This ensures that TCBs which are
     * stored in ready lists (all of which have the same xItemValue value) get a
     * share of the CPU.  However, if the xItemValue is the same as the back marker
     * the iteration loop below will not end.  Therefore the value is checked
     * first, and the algorithm slightly modified if necessary. */
    if( xValueOfInsertion == portMAX_DELAY )
    {
        pxIterator = List->xListEnd.pxPrevious;
    }
    else
    {
        /* *** NOTE ***********************************************************
        *  If you find your application is crashing here then likely causes are
        *  listed below.  In addition see https://www.freertos.org/Why-FreeRTOS/FAQs for
        *  more tips, and ensure configASSERT() is defined!
        *  https://www.FreeRTOS.org/a00110.html#configASSERT
        *
        *   1) Stack overflow -
        *      see https://www.FreeRTOS.org/Stacks-and-stack-overflow-checking.html
        *   2) Incorrect interrupt priority assignment, especially on Cortex-M
        *      parts where numerically high priority values denote low actual
        *      interrupt priorities, which can seem counter intuitive.  See
        *      https://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html and the definition
        *      of configMAX_SYSCALL_INTERRUPT_PRIORITY on
        *      https://www.FreeRTOS.org/a00110.html
        *   3) Calling an API function from within a critical section or when
        *      the scheduler is suspended, or calling an API function that does
        *      not end in "FromISR" from an interrupt.
        *   4) Using a queue or semaphore before it has been initialised or
        *      before the scheduler has been started (are interrupts firing
        *      before vTaskStartScheduler() has been called?).
        *   5) If the FreeRTOS port supports interrupt nesting then ensure that
        *      the priority of the tick interrupt is at or below
        *      configMAX_SYSCALL_INTERRUPT_PRIORITY.
        **********************************************************************/
        for( pxIterator = ( ListItem_t * ) &( List->xListEnd ); pxIterator->pxNext->xItemValue <= xValueOfInsertion; pxIterator = pxIterator->pxNext )
        {
            /* There is nothing to do here, just iterating to the wanted
             * insertion position.
             * IF YOU FIND YOUR CODE STUCK HERE, SEE THE NOTE JUST ABOVE.
             */
        }
    }
    NewListItem->pxNext = pxIterator->pxNext;
    NewListItem->pxNext->pxPrevious = NewListItem;
    NewListItem->pxPrevious = pxIterator;
    pxIterator->pxNext = NewListItem;
    /* Remember which list the item is in.  This allows fast removal of the
     * item later. */
    NewListItem->pxContainer = List;
    ( List->uxNumberOfItems ) = ( UBaseType_t ) ( List->uxNumberOfItems + 1U );
}

UBaseType_t uxListRemove( ListItem_t * const ItemToRemove )
{
    /* The list item knows which list it is in.  Obtain the list from the list
     * item. */
    List_t * const List = ItemToRemove->pxContainer;
    ItemToRemove->pxNext->pxPrevious = ItemToRemove->pxPrevious;
    ItemToRemove->pxPrevious->pxNext = ItemToRemove->pxNext;
    /* Make sure the index is left pointing to a valid item. */
    if( List->Index == ItemToRemove )
    {
        List->Index = ItemToRemove->pxPrevious;
    }
    ItemToRemove->pxContainer = NULL;
    ( List->uxNumberOfItems ) = ( UBaseType_t ) ( List->uxNumberOfItems - 1U );
    return List->uxNumberOfItems;
}
