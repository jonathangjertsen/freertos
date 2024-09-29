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
typedef struct Queue_t* QueueHandle_t;

typedef struct Queue_t* QueueSetHandle_t;

typedef struct Queue_t* QueueSetMemberHandle_t;

#define queueSEND_TO_BACK ((BaseType_t)0)
#define queueSEND_TO_FRONT ((BaseType_t)1)
#define queueOVERWRITE ((BaseType_t)2)

#define queueQUEUE_TYPE_BASE ((uint8_t)0U)
#define queueQUEUE_TYPE_MUTEX ((uint8_t)1U)
#define queueQUEUE_TYPE_COUNTING_SEMAPHORE ((uint8_t)2U)
#define queueQUEUE_TYPE_BINARY_SEMAPHORE ((uint8_t)3U)
#define queueQUEUE_TYPE_RECURSIVE_MUTEX ((uint8_t)4U)
#define queueQUEUE_TYPE_SET ((uint8_t)5U)

#define xQueueCreate(len, itemSize) \
  QueueGenericCreate(len, itemSize, (queueQUEUE_TYPE_BASE))
#define xQueueCreateStatic(len, itemSize, storage, buf) \
  QueueCreateStatic(len, itemSize, storage, buf, (queueQUEUE_TYPE_BASE))
#define xQueueGetStaticBuffers(q, storage, staticQ) \
  GetStaticBuffers(q, storage, (staticQ))
#define xQueueSendToFront(q, item, ticks) \
  Send(q, item, ticks, queueSEND_TO_FRONT)

#define xQueueSendToBack(q, item, ticks) Send(q, item, ticks, queueSEND_TO_BACK)

#define xQueueSend(q, item, ticks) Send(q, item, ticks, queueSEND_TO_BACK)

#define xQueueOverwrite(q, item) xQueueGenericSend(q, item, 0, queueOVERWRITE)

BaseType_t Send(QueueHandle_t q, const void* const item, TickType_t ticks,
                const BaseType_t copyPos);

BaseType_t xQueuePeek(QueueHandle_t q, void* const pvBuffer, TickType_t ticks);

BaseType_t xQueuePeekFromISR(QueueHandle_t q, void* const pvBuffer);

BaseType_t Recv(QueueHandle_t q, void* const pvBuffer, TickType_t ticks);

UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t q);

UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t q);

void vQueueDelete(QueueHandle_t q);

#define xQueueSendToFrontFromISR(q, item, woken) \
  SendFromISR(q, item, woken, queueSEND_TO_FRONT)

#define xQueueSendToBackFromISR(q, item, woken) \
  SendFromISR(q, item, woken, queueSEND_TO_BACK)

#define xQueueOverwriteFromISR(q, item, woken) \
  SendFromISR(q, item, woken, queueOVERWRITE)

#define xQueueSendFromISR(q, item, woken) \
  SendFromISR(q, item, woken, queueSEND_TO_BACK)

BaseType_t SendFromISR(QueueHandle_t q, const void* const item,
                       BaseType_t* const woken, const BaseType_t copyPos);
BaseType_t GiveFromISR(QueueHandle_t q, BaseType_t* const woken);

BaseType_t RecvFromISR(QueueHandle_t q, void* const pvBuffer,
                       BaseType_t* const woken);

BaseType_t xQueueIsQueueEmptyFromISR(const QueueHandle_t q);
BaseType_t xQueueIsQueueFullFromISR(const QueueHandle_t q);
UBaseType_t uxQueueMessagesWaitingFromISR(const QueueHandle_t q);

QueueHandle_t xQueueCreateMutex(const uint8_t type);
QueueHandle_t xQueueCreateMuteStatic(const uint8_t type,
                                     StaticQueue_t* pStaticQueue);
QueueHandle_t xQueueCreateCountingSemaphore(const UBaseType_t maxCount,
                                            const UBaseType_t initCount);
QueueHandle_t xQueueCreateCountingSemaphoreStatic(const UBaseType_t maxCount,
                                                  const UBaseType_t initCount,
                                                  StaticQueue_t* pStaticQueue);
BaseType_t xQueueSemaphoreTake(QueueHandle_t q, TickType_t ticks);
TaskHandle_t xQueueGetMutexHolder(QueueHandle_t xSemaphore);
TaskHandle_t xQueueGetMutexHolderFromISR(QueueHandle_t xSemaphore);

BaseType_t xQueueTakeMutexRecursive(QueueHandle_t xMutex, TickType_t ticks);
BaseType_t xQueueGiveMutexRecursive(QueueHandle_t xMutex);

#define xQueueReset(xQueue) xQueueGenericReset((xQueue), false)

QueueHandle_t QueueGenericCreate(const UBaseType_t len,
                                 const UBaseType_t itemSize,
                                 const uint8_t type);

QueueHandle_t QueueCreateStatic(const UBaseType_t len,
                                const UBaseType_t itemSize, uint8_t* storage,
                                StaticQueue_t* pStaticQueue,
                                const uint8_t type);

BaseType_t GetStaticBuffers(QueueHandle_t q, uint8_t** storage,
                            StaticQueue_t** staticQ);

QueueSetHandle_t xQueueCreateSet(const UBaseType_t uxEventQueueLength);

BaseType_t xQueueAddToSet(QueueSetMemberHandle_t xQueueOrSemaphore,
                          QueueSetHandle_t xQueueSet);

BaseType_t xQueueRemoveFromSet(QueueSetMemberHandle_t xQueueOrSemaphore,
                               QueueSetHandle_t xQueueSet);

QueueSetMemberHandle_t xQueueSelectFromSet(QueueSetHandle_t xQueueSet,
                                           const TickType_t ticks);

QueueSetMemberHandle_t xQueueSelectFromSetFromISR(QueueSetHandle_t xQueueSet);

void vQueueWaitForMessageRestricted(QueueHandle_t q, TickType_t ticks,
                                    const BaseType_t xWaitIndefinitely);
BaseType_t xQueueGenericReset(QueueHandle_t q, BaseType_t xNewQueue);
UBaseType_t GetQueueItemSize(QueueHandle_t q);
UBaseType_t GetQueueLength(QueueHandle_t q);
