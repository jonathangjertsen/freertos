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
#pragma once

#include "FreeRTOS.h"

template<class T>
struct List_t;

template<class T>
struct ListItem_t 
{
    TickType_t xItemValue;          /**< The value being listed.  In most cases this is used to sort the list in ascending order. */
    struct ListItem_t<T> *pxNext;     /**< Pointer to the next ListItem_t in the list. */
    struct ListItem_t<T> *pxPrevious; /**< Pointer to the previous ListItem_t in the list. */
    T *pvOwner;                                     /**< Pointer to the object (normally a TCB) that contains the list item.  There is therefore a two way link between the object containing the list item and the list item itself. */
    List_t<T> *pvContainer;     /**< Pointer to the list in which this list item is placed (if any). */
};

/*
 * Definition of the type of queue used by the scheduler.
 */
template<class T>
struct List_t
{
    UBaseType_t uxNumberOfItems;
    ListItem_t<T> *  Index; /**< Used to walk through the list.  Points to the last item returned by a call to listGET_OWNER_OF_NEXT_ENTRY (). */
    ListItem_t<T> xListEnd;                  /**< List item that contains the maximum possible item value meaning it is always at the end of the list and is therefore used as a marker. */
};

/*
 * Access macro to set the owner of a list item.  The owner of a list item
 * is the object (usually a TCB) that contains the list item.
 *
 * \page SET_LIST_ITEM_OWNER SET_LIST_ITEM_OWNER
 * \ingroup LinkedList
 */
template<class T>
void SET_LIST_ITEM_OWNER(ListItem_t<T> *pxListItem, T *pxOwner) {
    pxListItem->pvOwner = pxOwner;
}

template<class T>
T* GET_LIST_ITEM_OWNER(ListItem_t<T> *pxListItem) {
    return pxListItem->pvOwner;
}

template<class T>
static inline void SET_LIST_ITEM_VALUE(ListItem_t<T> *pxListItem, TickType_t xValue ) {
    pxListItem->xItemValue = xValue;
}

template<class T>
static inline TickType_t GET_LIST_ITEM_VALUE(ListItem_t<T> *pxListItem) {
    return pxListItem->xItemValue;
}

template<class T>
static inline TickType_t GET_ITEM_VALUE_OF_HEAD_ENTRY(List_t<T> const *pxList) {
    return pxList->xListEnd.pxNext->xItemValue;
}

template<class T>
static inline ListItem_t<T> *GET_HEAD_ENTRY(List_t<T> *pxList ) {
    return pxList->xListEnd.pxNext;
}

template<class T>
static inline ListItem_t<T> *GET_NEXT(ListItem_t<T> *pxListItem) {
    return pxListItem->pxNext;
}

template<class T>
static inline ListItem_t<T> const *GET_END_MARKER(List_t<T> *pxList){
    return &pxList->xListEnd;
}

template<class T>
static inline bool LIST_IS_EMPTY(List_t<T> *pxList) {
    return pxList->uxNumberOfItems == 0;
}

template<class T>
static inline UBaseType_t CURRENT_LIST_LENGTH(List_t<T> const *pxList) {
    return pxList->uxNumberOfItems;
}

template<class T>
static inline T*GET_OWNER_OF_NEXT_ENTRY(List_t<T> *list) {
    list->Index = list->Index->pxNext;
    if((void *)list->Index == (void *)&(list->xListEnd))
    {
        list->Index = list->xListEnd.pxNext;
    }
    return list->Index->pvOwner;
}

template<class T>
static inline void REMOVE_ITEM(ListItem_t<T> *item) {
    List_t<T> * const pxList = item->pvContainer;                                    
    item->pxNext->pxPrevious = item->pxPrevious;                    
    item->pxPrevious->pxNext = item->pxNext;                        
    if( pxList->Index == item )                                                 
    {                                                                                           
        pxList->Index = item->pxPrevious;                                       
    }                                                                                           
    item->pvContainer = NULL;                                                     
    ( pxList->uxNumberOfItems ) = ( UBaseType_t ) ( ( pxList->uxNumberOfItems ) - 1U ); 
}

template<class T>
static inline void INSERT_END(List_t<T> *list, ListItem_t<T> *item) {
    ListItem_t<T> * const Index = list->Index;
    item->pxNext = Index;
    item->pxPrevious = Index->pxPrevious;
    Index->pxPrevious->pxNext = item;
    Index->pxPrevious = item;
    item->pvContainer = list;
    list->uxNumberOfItems = (UBaseType_t) ( ( list->uxNumberOfItems ) + 1U );
}

template<class T>
static inline T *GET_OWNER_OF_HEAD_ENTRY(List_t<T> *pxList) {
    return (T*)(pxList->xListEnd.pxNext->pvOwner);
}

template<class T>
static inline bool IS_CONTAINED_WITHIN(List_t<T> *pxList, ListItem_t<T> *pxListItem ) {
    return pxListItem->pvContainer == pxList;
}

template<class T>
static inline List_t<T> *LIST_ITEM_CONTAINER(ListItem_t<T> *pxListItem) {
    return pxListItem->pvContainer;
}

template<class T>
static inline bool LIST_IS_INITIALISED(List_t<T> *pxList) {
    return pxList->xListEnd.xItemValue == portMAX_DELAY;
}


template<class T>
void ListInitialise(List_t<T> *const pxList) ;

template<class T>
void ListInitialiseItem( ListItem_t<T> *const pxItem) ;

template<class T>
void ListInsert(List_t<T> *const pxList, ListItem_t<T> *const pxNewListItem);

template<class T>
void vListInsertEnd(List_t<T> *const pxList, ListItem_t<T> *const pxNewListItem);

template<class T>
UBaseType_t ListRemove(ListItem_t<T> *const pxItemToRemove) ;

template<class T>
UBaseType_t ListRemove(ListItem_t<T> * const ItemToRemove)
{
    /* The list item knows which list it is in.  Obtain the list from the list
     * item. */
    auto * const List = ItemToRemove->pvContainer;
    ItemToRemove->pxNext->pxPrevious = ItemToRemove->pxPrevious;
    ItemToRemove->pxPrevious->pxNext = ItemToRemove->pxNext;
    /* Make sure the index is left pointing to a valid item. */
    if( List->Index == ItemToRemove )
    {
        List->Index = ItemToRemove->pxPrevious;
    }
    ItemToRemove->pvContainer = NULL;
    ( List->uxNumberOfItems ) = ( UBaseType_t ) ( List->uxNumberOfItems - 1U );
    return List->uxNumberOfItems;
}

template<class T>
void ListInsert( List_t<T> * const List, ListItem_t<T> * const NewListItem )
{
    ListItem_t<T> *pxIterator;
    const TickType_t xValueOfInsertion = NewListItem->xItemValue;
    if( xValueOfInsertion == portMAX_DELAY )
    {
        pxIterator = List->xListEnd.pxPrevious;
    }
    else
    {
        for( pxIterator = ( ListItem_t<T>* ) &( List->xListEnd ); pxIterator->pxNext->xItemValue <= xValueOfInsertion; pxIterator = pxIterator->pxNext )
        {
        }
    }
    NewListItem->pxNext = pxIterator->pxNext;
    NewListItem->pxNext->pxPrevious = NewListItem;
    NewListItem->pxPrevious = pxIterator;
    pxIterator->pxNext = NewListItem;
    NewListItem->pvContainer = List;
    ( List->uxNumberOfItems ) = ( UBaseType_t ) ( List->uxNumberOfItems + 1U );
}

template<class T>
void ListInsertEnd( List_t<T> * const List,
                     ListItem_t<T> * const NewListItem )
{
    auto * const Index = List->Index;
    NewListItem->pxNext = Index;
    NewListItem->pxPrevious = Index->pxPrevious;
    Index->pxPrevious->pxNext = NewListItem;
    Index->pxPrevious = NewListItem;
    NewListItem->pvContainer = List;
    ( List->uxNumberOfItems ) = ( UBaseType_t ) ( List->uxNumberOfItems + 1U );
}

template<class T>
void ListInitialiseItem( ListItem_t<T> * const Item )
{
    Item->pvContainer = NULL;
}

template<class T>
void ListInitialise(List_t<T> * const List )
{
    List->Index = &( List->xListEnd );
    List->xListEnd.xItemValue = portMAX_DELAY;
    List->xListEnd.pxNext = &( List->xListEnd );
    List->xListEnd.pxPrevious = &( List->xListEnd );
    List->uxNumberOfItems = ( UBaseType_t ) 0U;
}
