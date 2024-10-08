/*
 * FreeRTOS Kernel <DEVELOPMENT BRANCH>
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

#pragma once

#include "task.hpp"

struct Queue_t;
typedef struct Queue_t *QueueHandle_t;

typedef struct Queue_t *QueueSetHandle_t;

typedef struct Queue_t *QueueSetMemberHandle_t;

enum class QueuePos_t {
  Back = 0,
  Front = 1,
  Overwrite = 2,
};

#define QueueGetStaticBuffers(q, storage, staticQ) GetStaticBuffers(q, storage, (staticQ))
#define QueueSendToFront(q, item, ticks) Send(q, item, ticks, QueuePos_t::Front)

#define QueueSendToBack(q, item, ticks) Send(q, item, ticks, QueuePos_t::Back)

#define QueueSend(q, item, ticks) Send(q, item, ticks, QueuePos_t::Back)

#define QueueOverwrite(q, item) QueueGenericSend(q, item, 0, QueuePos_t::Overwrite)

BaseType_t Send(QueueHandle_t q, const void *const item, TickType_t ticks, QueuePos_t pos);

BaseType_t QueuePeek(QueueHandle_t q, void *const pvBuffer, TickType_t ticks);

BaseType_t QueuePeekFromISR(QueueHandle_t q, void *const pvBuffer);

BaseType_t Recv(QueueHandle_t q, void *const pvBuffer, TickType_t ticks);

UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t q);

UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t q);

void vQueueDelete(QueueHandle_t q);

#define QueueSendToFrontFromISR(q, item, woken) SendFromISR(q, item, woken, QueuePos_t::Front)

#define QueueSendToBackFromISR(q, item, woken) SendFromISR(q, item, woken, QueuePos_t::Back)

#define QueueOverwriteFromISR(q, item, woken) SendFromISR(q, item, woken, QueuePos_t::Overwrite)

#define QueueSendFromISR(q, item, woken) SendFromISR(q, item, woken, QueuePos_t::Back)

BaseType_t SendFromISR(QueueHandle_t q, const void *const item, BaseType_t *const woken, QueuePos_t pos);
BaseType_t GiveFromISR(QueueHandle_t q, BaseType_t *const woken);

BaseType_t RecvFromISR(QueueHandle_t q, void *const pvBuffer, BaseType_t *const woken);

BaseType_t QueueIsQueueEmptyFromISR(const QueueHandle_t q);
BaseType_t QueueIsQueueFullFromISR(const QueueHandle_t q);
UBaseType_t uxQueueMessagesWaitingFromISR(const QueueHandle_t q);

QueueHandle_t QueueCreateMutex(const uint8_t type);
QueueHandle_t QueueCreateMuteStatic(const uint8_t type, StaticQueue_t *pStaticQueue);
QueueHandle_t QueueCreateCountingSemaphore(const UBaseType_t maxCount, const UBaseType_t initCount);
QueueHandle_t QueueCreateCountingSemaphoreStatic(const UBaseType_t maxCount, const UBaseType_t initCount,
                                                 StaticQueue_t *pStaticQueue);
BaseType_t QueueSemaphoreTake(QueueHandle_t q, TickType_t ticks);
TaskHandle_t QueueGetMutexHolder(QueueHandle_t xSemaphore);
TaskHandle_t QueueGetMutexHolderFromISR(QueueHandle_t xSemaphore);

bool QueueTakeMutexRecursive(QueueHandle_t xMutex, TickType_t ticks);
BaseType_t QueueGiveMutexRecursive(QueueHandle_t xMutex);

#define QueueReset(xQueue) QueueGenericReset((xQueue), false)

QueueHandle_t QueueCreate(const UBaseType_t len, const UBaseType_t itemSize);

QueueHandle_t QueueCreateStatic(const UBaseType_t len, const UBaseType_t itemSize, uint8_t *storage,
                                StaticQueue_t *pStaticQueue);

BaseType_t GetStaticBuffers(QueueHandle_t q, uint8_t **storage, StaticQueue_t **staticQ);

QueueSetHandle_t QueueCreateSet(const UBaseType_t uxEventQueueLength);

BaseType_t QueueAddToSet(QueueSetMemberHandle_t QueueOrSemaphore, QueueSetHandle_t QueueSet);

BaseType_t QueueRemoveFromSet(QueueSetMemberHandle_t QueueOrSemaphore, QueueSetHandle_t QueueSet);

QueueSetMemberHandle_t QueueSelectFromSet(QueueSetHandle_t QueueSet, const TickType_t ticks);

QueueSetMemberHandle_t QueueSelectFromSetFromISR(QueueSetHandle_t QueueSet);

void vQueueWaitForMessageRestricted(QueueHandle_t q, TickType_t ticks, const BaseType_t xWaitIndefinitely);
