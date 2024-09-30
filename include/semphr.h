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
#include "queue.h"
typedef QueueHandle_t SemaphoreHandle_t;
#define semBINARY_SEMAPHORE_QUEUE_LENGTH ((uint8_t)1U)
#define semSEMAPHORE_QUEUE_ITEM_LENGTH ((uint8_t)0U)
#define semGIVE_BLOCK_TIME ((TickType_t)0U)
#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
#define vSemaphoreCreateBinary(xSemaphore)                                                                     \
  do {                                                                                                         \
    (xSemaphore) =                                                                                             \
        xQueueGenericCreate((UBaseType_t)1, semSEMAPHORE_QUEUE_ITEM_LENGTH, queueQUEUE_TYPE_BINARY_SEMAPHORE); \
    if ((xSemaphore) != NULL) {                                                                                \
      (void)xSemaphoreGive((xSemaphore));                                                                      \
    }                                                                                                          \
  } while (0)
#endif
#define xSemaphoreCreateBinary() \
  xQueueGenericCreate((UBaseType_t)1, semSEMAPHORE_QUEUE_ITEM_LENGTH, queueQUEUE_TYPE_BINARY_SEMAPHORE)
#define xSemaphoreCreateBinaryStatic(pStaticSemaphore)                                        \
  QueueCreateStatic((UBaseType_t)1, semSEMAPHORE_QUEUE_ITEM_LENGTH, NULL, (pStaticSemaphore), \
                    queueQUEUE_TYPE_BINARY_SEMAPHORE)
#define xSemaphoreTake(xSemaphore, xBlockTime) xQueueSemaphoreTake((xSemaphore), (xBlockTime))
#define xSemaphoreTakeRecursive(xMutex, xBlockTime) xQueueTakeMutexRecursive((xMutex), (xBlockTime))
#define xSemaphoreGive(xSemaphore) Send((QueueHandle_t)(xSemaphore), NULL, semGIVE_BLOCK_TIME, queueSEND_TO_BACK)
#define xSemaphoreGiveRecursive(xMutex) xQueueGiveMutexRecursive((xMutex))
#define xSemaphoreGiveFromISR(xSemaphore, HigherPriorityTaskWoken) \
  GiveFromISR((QueueHandle_t)(xSemaphore), (HigherPriorityTaskWoken))
#define xSemaphoreTakeFromISR(xSemaphore, HigherPriorityTaskWoken) \
  RecvFromISR((QueueHandle_t)(xSemaphore), NULL, (HigherPriorityTaskWoken))
#define xSemaphoreCreateMutex() xQueueCreateMutex(queueQUEUE_TYPE_MUTEX)
#define xSemaphoreCreateMuteStatic(MutexBuffer) xQueueCreateMuteStatic(queueQUEUE_TYPE_MUTEX, (MutexBuffer))
#define xSemaphoreCreateRecursiveMutex() xQueueCreateMutex(queueQUEUE_TYPE_RECURSIVE_MUTEX)
#define xSemaphoreCreateRecursiveMuteStatic(pStaticSemaphore) \
  xQueueCreateMuteStatic(queueQUEUE_TYPE_RECURSIVE_MUTEX, (pStaticSemaphore))
#define xSemaphoreCreateCounting(uxMaxCount, uxInitialCount) \
  xQueueCreateCountingSemaphore((uxMaxCount), (uxInitialCount))
#define xSemaphoreCreateCountingStatic(uxMaxCount, uxInitialCount, SemaphoreBuffer) \
  xQueueCreateCountingSemaphoreStatic((uxMaxCount), (uxInitialCount), (SemaphoreBuffer))
#define vSemaphoreDelete(xSemaphore) vQueueDelete((QueueHandle_t)(xSemaphore))
#define xSemaphoreGetMutexHolder(xSemaphore) xQueueGetMutexHolder((xSemaphore))
#define xSemaphoreGetMutexHolderFromISR(xSemaphore) xQueueGetMutexHolderFromISR((xSemaphore))
#define uxSemaphoreGetCount(xSemaphore) uxQueueMessagesWaiting((QueueHandle_t)(xSemaphore))
#define uxSemaphoreGetCountFromISR(xSemaphore) uxQueueMessagesWaitingFromISR((QueueHandle_t)(xSemaphore))
#define xSemaphoreGetStaticBuffer(xSemaphore, SemaphoreBuffer) \
  xGetStaticBuffers((QueueHandle_t)(xSemaphore), NULL, (SemaphoreBuffer))
