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
#ifndef LIST_H
#define LIST_H

#include "FreeRTOS.h"

#define FIRST_LIST_ITEM_INTEGRITY_CHECK_VALUE
#define SECOND_LIST_ITEM_INTEGRITY_CHECK_VALUE
#define FIRST_LIST_INTEGRITY_CHECK_VALUE
#define SECOND_LIST_INTEGRITY_CHECK_VALUE
#define SET_FIRST_LIST_ITEM_INTEGRITY_CHECK_VALUE( pxItem )
#define SET_SECOND_LIST_ITEM_INTEGRITY_CHECK_VALUE( pxItem )
#define SET_LIST_INTEGRITY_CHECK_1_VALUE( pxList )
#define SET_LIST_INTEGRITY_CHECK_2_VALUE( pxList )
#define TEST_LIST_ITEM_INTEGRITY( pxItem )
#define TEST_LIST_INTEGRITY( pxList )

/*
 * Definition of the only type of object that a list can contain.
 */
struct xLIST;
struct xLIST_ITEM
{
    FIRST_LIST_ITEM_INTEGRITY_CHECK_VALUE           /**< Set to a known value if configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES is set to 1. */
    TickType_t xItemValue;          /**< The value being listed.  In most cases this is used to sort the list in ascending order. */
    struct xLIST_ITEM *pxNext;     /**< Pointer to the next ListItem_t in the list. */
    struct xLIST_ITEM *pxPrevious; /**< Pointer to the previous ListItem_t in the list. */
    void * pvOwner;                                     /**< Pointer to the object (normally a TCB) that contains the list item.  There is therefore a two way link between the object containing the list item and the list item itself. */
    struct xLIST *pxContainer;     /**< Pointer to the list in which this list item is placed (if any). */
    SECOND_LIST_ITEM_INTEGRITY_CHECK_VALUE          /**< Set to a known value if configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES is set to 1. */
};
typedef struct xLIST_ITEM ListItem_t;
typedef struct xLIST_ITEM MiniListItem_t;
/*
 * Definition of the type of queue used by the scheduler.
 */
typedef struct xLIST
{
    FIRST_LIST_INTEGRITY_CHECK_VALUE      /**< Set to a known value if configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES is set to 1. */
    UBaseType_t uxNumberOfItems;
    ListItem_t *  Index; /**< Used to walk through the list.  Points to the last item returned by a call to listGET_OWNER_OF_NEXT_ENTRY (). */
    MiniListItem_t xListEnd;                  /**< List item that contains the maximum possible item value meaning it is always at the end of the list and is therefore used as a marker. */
    SECOND_LIST_INTEGRITY_CHECK_VALUE     /**< Set to a known value if configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES is set to 1. */
} List_t;
/*
 * Access macro to set the owner of a list item.  The owner of a list item
 * is the object (usually a TCB) that contains the list item.
 *
 * \page SET_LIST_ITEM_OWNER SET_LIST_ITEM_OWNER
 * \ingroup LinkedList
 */
template<class T>
void SET_LIST_ITEM_OWNER(ListItem_t *pxListItem, T *pxOwner) {
    pxListItem->pvOwner = (void*)pxOwner;
}

void* GET_LIST_ITEM_OWNER(ListItem_t *pxListItem) {
    return (pxListItem->pvOwner);
}

static inline void SET_LIST_ITEM_VALUE(ListItem_t *pxListItem, TickType_t xValue ) {
    pxListItem->xItemValue = xValue;
}

static inline TickType_t GET_LIST_ITEM_VALUE(ListItem_t *pxListItem) {
    return pxListItem->xItemValue;
}

static inline TickType_t GET_ITEM_VALUE_OF_HEAD_ENTRY(List_t *pxList) {
    return pxList->xListEnd.pxNext->xItemValue;
}

static inline ListItem_t *GET_HEAD_ENTRY(List_t *pxList ) {
    return pxList->xListEnd.pxNext;
}

static inline ListItem_t *GET_NEXT(ListItem_t *pxListItem) {
    return pxListItem->pxNext;
}

static inline ListItem_t const *GET_END_MARKER(List_t *pxList){
    return &pxList->xListEnd;
}

static inline bool LIST_IS_EMPTY(List_t *pxList) {
    return pxList->uxNumberOfItems == 0;
}

static inline UBaseType_t CURRENT_LIST_LENGTH(List_t *pxList) {
    return pxList->uxNumberOfItems;
}

#define GET_OWNER_OF_NEXT_ENTRY( pxTCB, pxList )                                       \
do {                                                                                       \
    List_t * const pxConstList = ( pxList );                                               \
    /* Increment the index to the next item and return the item, ensuring */               \
    /* we don't return the marker used at the end of the list.  */                         \
    ( pxConstList )->Index = ( pxConstList )->Index->pxNext;                           \
    if( ( void * ) ( pxConstList )->Index == ( void * ) &( ( pxConstList )->xListEnd ) ) \
    {                                                                                      \
        ( pxConstList )->Index = ( pxConstList )->xListEnd.pxNext;                       \
    }                                                                                      \
    ( pxTCB ) = ( pxConstList )->Index->pvOwner;                                         \
} while( 0 )

#define REMOVE_ITEM( pxItemToRemove ) \
    do {                                  \
        /* The list item knows which list it is in.  Obtain the list from the list \
         * item. */                                                                                 \
        List_t * const pxList = ( pxItemToRemove )->pxContainer;                                    \
                                                                                                    \
        ( pxItemToRemove )->pxNext->pxPrevious = ( pxItemToRemove )->pxPrevious;                    \
        ( pxItemToRemove )->pxPrevious->pxNext = ( pxItemToRemove )->pxNext;                        \
        /* Make sure the index is left pointing to a valid item. */                                 \
        if( pxList->Index == ( pxItemToRemove ) )                                                 \
        {                                                                                           \
            pxList->Index = ( pxItemToRemove )->pxPrevious;                                       \
        }                                                                                           \
                                                                                                    \
        ( pxItemToRemove )->pxContainer = NULL;                                                     \
        ( ( pxList )->uxNumberOfItems ) = ( UBaseType_t ) ( ( ( pxList )->uxNumberOfItems ) - 1U ); \
    } while( 0 )
#define INSERT_END( pxList, pxNewListItem )           \
    do {                                                  \
        ListItem_t * const Index = ( pxList )->Index; \
                                                          \
        /* Only effective when configASSERT() is also defined, these tests may catch \
         * the list data structures being overwritten in memory.  They will not catch \
         * data errors caused by incorrect configuration or use of FreeRTOS. */ \
        TEST_LIST_INTEGRITY( ( pxList ) );                                  \
        TEST_LIST_ITEM_INTEGRITY( ( pxNewListItem ) );                      \
                                                                                \
        /* Insert a new list item into ( pxList ), but rather than sort the list, \
         * makes the new list item the last item to be removed by a call to \
         * listGET_OWNER_OF_NEXT_ENTRY(). */                                                        \
        ( pxNewListItem )->pxNext = Index;                                                        \
        ( pxNewListItem )->pxPrevious = Index->pxPrevious;                                        \
                                                                                                    \
        Index->pxPrevious->pxNext = ( pxNewListItem );                                            \
        Index->pxPrevious = ( pxNewListItem );                                                    \
                                                                                                    \
        /* Remember which list the item is in. */                                                   \
        ( pxNewListItem )->pxContainer = ( pxList );                                                \
                                                                                                    \
        ( ( pxList )->uxNumberOfItems ) = ( UBaseType_t ) ( ( ( pxList )->uxNumberOfItems ) + 1U ); \
    } while( 0 )

static inline void *GET_OWNER_OF_HEAD_ENTRY(List_t *pxList) {
    return pxList->xListEnd.pxNext->pvOwner;
}

static inline bool IS_CONTAINED_WITHIN(List_t *pxList, ListItem_t *pxListItem ) {
    return pxListItem->pxContainer == pxList;
}

static inline List_t *LIST_ITEM_CONTAINER(ListItem_t *pxListItem) {
    return pxListItem->pxContainer;
}

static inline bool LIST_IS_INITIALISED(List_t *pxList) {
    return pxList->xListEnd.xItemValue == portMAX_DELAY;
}

void ListInitialise(List_t *const pxList) ;
void ListInitialiseItem( ListItem_t *const pxItem) ;
void ListInsert(List_t *const pxList, ListItem_t *const pxNewListItem);
void vListInsertEnd(List_t *const pxList, ListItem_t *const pxNewListItem);
UBaseType_t ListRemove(ListItem_t *const pxItemToRemove) ;
#endif /* ifndef LIST_H */
