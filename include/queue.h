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

#ifdef __cplusplus
extern "C" {
#endif

struct QueueDefinition;
typedef struct QueueDefinition* QueueHandle_t;

typedef struct QueueDefinition* QueueSetHandle_t;

typedef struct QueueDefinition* QueueSetMemberHandle_t;

#define queueSEND_TO_BACK ((BaseType_t)0)
#define queueSEND_TO_FRONT ((BaseType_t)1)
#define queueOVERWRITE ((BaseType_t)2)

#define queueQUEUE_TYPE_BASE ((uint8_t)0U)
#define queueQUEUE_TYPE_MUTEX ((uint8_t)1U)
#define queueQUEUE_TYPE_COUNTING_SEMAPHORE ((uint8_t)2U)
#define queueQUEUE_TYPE_BINARY_SEMAPHORE ((uint8_t)3U)
#define queueQUEUE_TYPE_RECURSIVE_MUTEX ((uint8_t)4U)
#define queueQUEUE_TYPE_SET ((uint8_t)5U)

#define xQueueCreate(uxQueueLength, uxItemSize) \
  xQueueGenericCreate((uxQueueLength), (uxItemSize), (queueQUEUE_TYPE_BASE))
#define xQueueCreateStatic(uxQueueLength, uxItemSize, pucQueueStorage,        \
                           QueueBuffer)                                     \
  xQueueGenericCreateStatic((uxQueueLength), (uxItemSize), (pucQueueStorage), \
                            (QueueBuffer), (queueQUEUE_TYPE_BASE))
#define xQueueGetStaticBuffers(xQueue, ppucQueueStorage, ppStaticQueue) \
  xQueueGenericGetStaticBuffers((xQueue), (ppucQueueStorage), (ppStaticQueue))
#define xQueueSendToFront(xQueue, pvItemToQueue, xTicksToWait) \
  xQueueGenericSend((xQueue), (pvItemToQueue), (xTicksToWait), \
                    queueSEND_TO_FRONT)

#define xQueueSendToBack(xQueue, pvItemToQueue, xTicksToWait)  \
  xQueueGenericSend((xQueue), (pvItemToQueue), (xTicksToWait), \
                    queueSEND_TO_BACK)

#define xQueueSend(xQueue, pvItemToQueue, xTicksToWait)        \
  xQueueGenericSend((xQueue), (pvItemToQueue), (xTicksToWait), \
                    queueSEND_TO_BACK)

#define xQueueOverwrite(xQueue, pvItemToQueue) \
  xQueueGenericSend((xQueue), (pvItemToQueue), 0, queueOVERWRITE)

BaseType_t xQueueGenericSend(QueueHandle_t xQueue,
                             const void* const pvItemToQueue,
                             TickType_t xTicksToWait,
                             const BaseType_t xCopyPosition);

BaseType_t xQueuePeek(QueueHandle_t xQueue, void* const pvBuffer,
                      TickType_t xTicksToWait);

BaseType_t xQueuePeekFromISR(QueueHandle_t xQueue, void* const pvBuffer);

BaseType_t xQueueReceive(QueueHandle_t xQueue, void* const pvBuffer,
                         TickType_t xTicksToWait);

UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t xQueue);

UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t xQueue);

void vQueueDelete(QueueHandle_t xQueue);

#define xQueueSendToFrontFromISR(xQueue, pvItemToQueue,     \
                                 HigherPriorityTaskWoken) \
  xQueueGenericSendFromISR((xQueue), (pvItemToQueue),       \
                           (HigherPriorityTaskWoken), queueSEND_TO_FRONT)

#define xQueueSendToBackFromISR(xQueue, pvItemToQueue,     \
                                HigherPriorityTaskWoken) \
  xQueueGenericSendFromISR((xQueue), (pvItemToQueue),      \
                           (HigherPriorityTaskWoken), queueSEND_TO_BACK)

#define xQueueOverwriteFromISR(xQueue, pvItemToQueue,     \
                               HigherPriorityTaskWoken) \
  xQueueGenericSendFromISR((xQueue), (pvItemToQueue),     \
                           (HigherPriorityTaskWoken), queueOVERWRITE)

#define xQueueSendFromISR(xQueue, pvItemToQueue, HigherPriorityTaskWoken) \
  xQueueGenericSendFromISR((xQueue), (pvItemToQueue),                       \
                           (HigherPriorityTaskWoken), queueSEND_TO_BACK)

BaseType_t xQueueGenericSendFromISR(QueueHandle_t xQueue,
                                    const void* const pvItemToQueue,
                                    BaseType_t* const HigherPriorityTaskWoken,
                                    const BaseType_t xCopyPosition);
BaseType_t xQueueGiveFromISR(QueueHandle_t xQueue,
                             BaseType_t* const HigherPriorityTaskWoken);

BaseType_t xQueueReceiveFromISR(QueueHandle_t xQueue, void* const pvBuffer,
                                BaseType_t* const HigherPriorityTaskWoken);

BaseType_t xQueueIsQueueEmptyFromISR(const QueueHandle_t xQueue);
BaseType_t xQueueIsQueueFullFromISR(const QueueHandle_t xQueue);
UBaseType_t uxQueueMessagesWaitingFromISR(const QueueHandle_t xQueue);
#if (configUSE_CO_ROUTINES == 1)

BaseType_t xQueueCRSendFromISR(QueueHandle_t xQueue, const void* pvItemToQueue,
                               BaseType_t xCoRoutinePreviouslyWoken);
BaseType_t xQueueCRReceiveFromISR(QueueHandle_t xQueue, void* pvBuffer,
                                  BaseType_t* TaskWoken);
BaseType_t xQueueCRSend(QueueHandle_t xQueue, const void* pvItemToQueue,
                        TickType_t xTicksToWait);
BaseType_t xQueueCRReceive(QueueHandle_t xQueue, void* pvBuffer,
                           TickType_t xTicksToWait);
#endif

QueueHandle_t xQueueCreateMutex(const uint8_t ucQueueType);
#if (configSUPPORT_STATIC_ALLOCATION == 1)
QueueHandle_t xQueueCreateMuteStatic(const uint8_t ucQueueType,
                                     StaticQueue_t* pStaticQueue);
#endif
#if (configUSE_COUNTING_SEMAPHORES == 1)
QueueHandle_t xQueueCreateCountingSemaphore(const UBaseType_t uxMaxCount,
                                            const UBaseType_t uxInitialCount);
#endif
#if ((configUSE_COUNTING_SEMAPHORES == 1) && \
     (configSUPPORT_STATIC_ALLOCATION == 1))
QueueHandle_t xQueueCreateCountingSemaphoreStatic(
    const UBaseType_t uxMaxCount, const UBaseType_t uxInitialCount,
    StaticQueue_t* pStaticQueue);
#endif
BaseType_t xQueueSemaphoreTake(QueueHandle_t xQueue, TickType_t xTicksToWait);
#if ((configUSE_MUTEXES == 1) && (INCLUDE_xSemaphoreGetMutexHolder == 1))
TaskHandle_t xQueueGetMutexHolder(QueueHandle_t xSemaphore);
TaskHandle_t xQueueGetMutexHolderFromISR(QueueHandle_t xSemaphore);
#endif

BaseType_t xQueueTakeMutexRecursive(QueueHandle_t xMutex,
                                    TickType_t xTicksToWait);
BaseType_t xQueueGiveMutexRecursive(QueueHandle_t xMutex);

#define xQueueReset(xQueue) xQueueGenericReset((xQueue), false)

#if (configQUEUE_REGISTRY_SIZE > 0)
void vQueueAddToRegistry(QueueHandle_t xQueue, const char* pcQueueName);
#endif

#if (configQUEUE_REGISTRY_SIZE > 0)
void vQueueUnregisterQueue(QueueHandle_t xQueue);
#endif

#if (configQUEUE_REGISTRY_SIZE > 0)
const char* pcQueueGetName(QueueHandle_t xQueue);
#endif

#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
QueueHandle_t xQueueGenericCreate(const UBaseType_t uxQueueLength,
                                  const UBaseType_t uxItemSize,
                                  const uint8_t ucQueueType);
#endif

#if (configSUPPORT_STATIC_ALLOCATION == 1)
QueueHandle_t xQueueGenericCreateStatic(const UBaseType_t uxQueueLength,
                                        const UBaseType_t uxItemSize,
                                        uint8_t* pucQueueStorage,
                                        StaticQueue_t* pStaticQueue,
                                        const uint8_t ucQueueType);
#endif

#if (configSUPPORT_STATIC_ALLOCATION == 1)
BaseType_t xQueueGenericGetStaticBuffers(QueueHandle_t xQueue,
                                         uint8_t** ppucQueueStorage,
                                         StaticQueue_t** ppStaticQueue);
#endif

#if ((configUSE_QUEUE_SETS == 1) && (configSUPPORT_DYNAMIC_ALLOCATION == 1))
QueueSetHandle_t xQueueCreateSet(const UBaseType_t uxEventQueueLength);
#endif

#if (configUSE_QUEUE_SETS == 1)
BaseType_t xQueueAddToSet(QueueSetMemberHandle_t xQueueOrSemaphore,
                          QueueSetHandle_t xQueueSet);
#endif

#if (configUSE_QUEUE_SETS == 1)
BaseType_t xQueueRemoveFromSet(QueueSetMemberHandle_t xQueueOrSemaphore,
                               QueueSetHandle_t xQueueSet);
#endif

#if (configUSE_QUEUE_SETS == 1)
QueueSetMemberHandle_t xQueueSelectFromSet(QueueSetHandle_t xQueueSet,
                                           const TickType_t xTicksToWait);
#endif

#if (configUSE_QUEUE_SETS == 1)
QueueSetMemberHandle_t xQueueSelectFromSetFromISR(QueueSetHandle_t xQueueSet);
#endif

void vQueueWaitForMessageRestricted(QueueHandle_t xQueue,
                                    TickType_t xTicksToWait,
                                    const BaseType_t xWaitIndefinitely);
BaseType_t xQueueGenericReset(QueueHandle_t xQueue, BaseType_t xNewQueue);
UBaseType_t uxQueueGetQueueItemSize(QueueHandle_t xQueue);
UBaseType_t uxQueueGetQueueLength(QueueHandle_t xQueue);

#ifdef __cplusplus
}
#endif
