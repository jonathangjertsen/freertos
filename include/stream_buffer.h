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
#include "freertos.h"
#include "portmacro.h"
#define sbTYPE_STREAM_BUFFER ((BaseType_t)0)
#define sbTYPE_MESSAGE_BUFFER ((BaseType_t)1)
#define sbTYPE_STREAM_BATCHING_BUFFER ((BaseType_t)2)
struct StreamBufferDef_t;
typedef struct StreamBufferDef_t* StreamBufferHandle_t;
typedef void (*StreamBufferCallbackFunction_t)(
    StreamBufferHandle_t xStreamBuffer, BaseType_t xIsInsideISR,
    BaseType_t* const HigherPriorityTaskWoken);
#define xStreamBufferCreate(xBufferSizeBytes, xTriggerLevelBytes)      \
  xStreamBufferGenericCreate((xBufferSizeBytes), (xTriggerLevelBytes), \
                             sbTYPE_STREAM_BUFFER, NULL, NULL)

#define xStreamBufferCreateStatic(xBufferSizeBytes, xTriggerLevelBytes, \
                                  pucStreamBufferStorageArea,           \
                                  pStaticStreamBuffer)                  \
  xStreamBufferGenericCreateStatic(                                     \
      (xBufferSizeBytes), (xTriggerLevelBytes), sbTYPE_STREAM_BUFFER,   \
      (pucStreamBufferStorageArea), (pStaticStreamBuffer), NULL, NULL)
#define xStreamBatchingBufferCreate(xBufferSizeBytes, xTriggerLevelBytes) \
  xStreamBufferGenericCreate((xBufferSizeBytes), (xTriggerLevelBytes),    \
                             sbTYPE_STREAM_BATCHING_BUFFER, NULL, NULL)

#define xStreamBatchingBufferCreateStatic(                                     \
    xBufferSizeBytes, xTriggerLevelBytes, pucStreamBufferStorageArea,          \
    pStaticStreamBuffer)                                                       \
  xStreamBufferGenericCreateStatic(                                            \
      (xBufferSizeBytes), (xTriggerLevelBytes), sbTYPE_STREAM_BATCHING_BUFFER, \
      (pucStreamBufferStorageArea), (pStaticStreamBuffer), NULL, NULL)

BaseType_t xStreamBufferGetStaticBuffers(
    StreamBufferHandle_t xStreamBuffer, uint8_t** ppucStreamBufferStorageArea,
    StaticStreamBuffer_t** ppStaticStreamBuffer);
size_t xStreamBufferSend(StreamBufferHandle_t xStreamBuffer,
                         const void* pvTxData, size_t xDataLengthBytes,
                         TickType_t xTicksToWait);
size_t xStreamBufferSendFromISR(StreamBufferHandle_t xStreamBuffer,
                                const void* pvTxData, size_t xDataLengthBytes,
                                BaseType_t* const HigherPriorityTaskWoken);
size_t xStreamBufferReceive(StreamBufferHandle_t xStreamBuffer, void* pvRxData,
                            size_t xBufferLengthBytes, TickType_t xTicksToWait);
size_t xStreamBufferReceiveFromISR(StreamBufferHandle_t xStreamBuffer,
                                   void* pvRxData, size_t xBufferLengthBytes,
                                   BaseType_t* const HigherPriorityTaskWoken);
void vStreamBufferDelete(StreamBufferHandle_t xStreamBuffer);
BaseType_t xStreamBufferIsFull(StreamBufferHandle_t xStreamBuffer);
BaseType_t xStreamBufferIsEmpty(StreamBufferHandle_t xStreamBuffer);
BaseType_t xStreamBufferReset(StreamBufferHandle_t xStreamBuffer);
BaseType_t xStreamBufferResetFromISR(StreamBufferHandle_t xStreamBuffer);
size_t xStreamBufferSpacesAvailable(StreamBufferHandle_t xStreamBuffer);
size_t xStreamBufferBytesAvailable(StreamBufferHandle_t xStreamBuffer);
BaseType_t xStreamBufferSetTriggerLevel(StreamBufferHandle_t xStreamBuffer,
                                        size_t xTriggerLevel);
BaseType_t xStreamBufferSendCompletedFromISR(
    StreamBufferHandle_t xStreamBuffer, BaseType_t* HigherPriorityTaskWoken);
BaseType_t xStreamBufferReceiveCompletedFromISR(
    StreamBufferHandle_t xStreamBuffer, BaseType_t* HigherPriorityTaskWoken);
UBaseType_t uxStreamBufferGetStreamBufferNotificationIndex(
    StreamBufferHandle_t xStreamBuffer);
void vStreamBufferSetStreamBufferNotificationIndex(
    StreamBufferHandle_t xStreamBuffer, UBaseType_t uxNotificationIndex);
StreamBufferHandle_t xStreamBufferGenericCreate(
    size_t xBufferSizeBytes, size_t xTriggerLevelBytes,
    BaseType_t xStreamBufferType,
    StreamBufferCallbackFunction_t SendCompletedCallback,
    StreamBufferCallbackFunction_t ReceiveCompletedCallback);
StreamBufferHandle_t xStreamBufferGenericCreateStatic(
    size_t xBufferSizeBytes, size_t xTriggerLevelBytes,
    BaseType_t xStreamBufferType, uint8_t* const pucStreamBufferStorageArea,
    StaticStreamBuffer_t* const pStaticStreamBuffer,
    StreamBufferCallbackFunction_t SendCompletedCallback,
    StreamBufferCallbackFunction_t ReceiveCompletedCallback);
size_t xStreamBufferNextMessageLengthBytes(StreamBufferHandle_t xStreamBuffer);
