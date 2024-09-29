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
struct Item_t 
{
    TickType_t Value;  
    struct Item_t<T> *Next;     
    struct Item_t<T> *Prev; 
    T *Owner;                     
    List_t<T> *Container;

    void init() {
        Container = nullptr;
    }

    UBaseType_t remove() {
        auto * const List = Container;
        Next->Prev = Prev;
        Prev->Next = Next;
        if(List->Index == this)
        {
            List->Index = Prev;
        }
        Container = NULL;
        List->Length--;
        return List->Length;
    }

    void ensureRemoved() {
        if (Container != nullptr) {
            remove();
        }
    }
};

/*
 * Definition of the type of queue used by the scheduler.
 */
template<class T>
struct List_t
{
    UBaseType_t Length;
    Item_t<T> *Index;
    Item_t<T> End;

    void init() {
        Index = &( End );
        End.Value = portMAX_DELAY;
        End.Next = &( End );
        End.Prev = &( End );
        Length = ( UBaseType_t ) 0U;
    }

    bool empty() {
        return Length == 0;
    }

    Item_t<T> *head() {
        return End.Next;
    }

    Item_t<T> *advance() {
        Index = Index->Next;
        if(Index == &End)
        {
            Index = End.Next;
        }
        return Index;
    }

    void append(Item_t<T> *item) {
        Item_t<T> * const Index = Index;
        item->Next = Index;
        item->Prev = Index->Prev;
        Index->Prev->Next = item;
        Index->Prev = item;
        item->Container = this;
        Length++;
    }

    void insert(Item_t<T> *item) {
        Item_t<T> *prev;
        const TickType_t value = item->Value;
        if(value == portMAX_DELAY)
        {
            prev = End.Prev;
        }
        else
        {
            for( prev = &End; prev->Next->Value <= value; prev = prev->Next )
            {
            }
        }
        item->Next = prev->Next;
        item->Next->Prev = item;
        item->Prev = prev;
        prev->Next = item;
        item->Container = this;
        Length++;
    }
};
