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
#include "stream_buffer.h"

typedef StreamBufferHandle_t MessageBufferHandle_t;

#define xMessageBufferCreate(xBufferSizeBytes) \
  xStreamBufferGenericCreate((xBufferSizeBytes), (size_t)0, sbTYPE_MESSAGE_BUFFER, NULL, NULL)

#define xMessageBufferCreateStatic(xBufferSizeBytes, pucMessageBufferStorageArea, pStaticMessageBuffer)         \
  xStreamBufferGenericCreateStatic((xBufferSizeBytes), 0, sbTYPE_MESSAGE_BUFFER, (pucMessageBufferStorageArea), \
                                   (pStaticMessageBuffer), NULL, NULL)

#define xMessageBufferSend(xMessageBuffer, pvTxData, xDataLengthBytes, xTicksToWait) \
  xStreamBufferSend((xMessageBuffer), (pvTxData), (xDataLengthBytes), (xTicksToWait))

#define xMessageBufferSendFromISR(xMessageBuffer, pvTxData, xDataLengthBytes, HigherPriorityTaskWoken) \
  xStreamBufferSendFromISR((xMessageBuffer), (pvTxData), (xDataLengthBytes), (HigherPriorityTaskWoken))

#define xMessageBufferReceive(xMessageBuffer, pvRxData, xBufferLengthBytes, xTicksToWait) \
  xStreamBufferReceive((xMessageBuffer), (pvRxData), (xBufferLengthBytes), (xTicksToWait))

#define xMessageBufferReceiveFromISR(xMessageBuffer, pvRxData, xBufferLengthBytes, HigherPriorityTaskWoken) \
  xStreamBufferReceiveFromISR((xMessageBuffer), (pvRxData), (xBufferLengthBytes), (HigherPriorityTaskWoken))

#define vMessageBufferDelete(xMessageBuffer) vStreamBufferDelete(xMessageBuffer)

#define xMessageBufferIsFull(xMessageBuffer) xStreamBufferIsFull(xMessageBuffer)

#define xMessageBufferIsEmpty(xMessageBuffer) xStreamBufferIsEmpty(xMessageBuffer)

#define xMessageBufferReset(xMessageBuffer) xStreamBufferReset(xMessageBuffer)

#define xMessageBufferResetFromISR(xMessageBuffer) xStreamBufferResetFromISR(xMessageBuffer)

#define xMessageBufferSpaceAvailable(xMessageBuffer) xStreamBufferSpacesAvailable(xMessageBuffer)
#define xMessageBufferSpacesAvailable(xMessageBuffer) xStreamBufferSpacesAvailable(xMessageBuffer)

#define xMessageBufferNextLengthBytes(xMessageBuffer) xStreamBufferNextMessageLengthBytes(xMessageBuffer)

#define xMessageBufferSendCompletedFromISR(xMessageBuffer, HigherPriorityTaskWoken) \
  xStreamBufferSendCompletedFromISR((xMessageBuffer), (HigherPriorityTaskWoken))

#define xMessageBufferReceiveCompletedFromISR(xMessageBuffer, HigherPriorityTaskWoken) \
  xStreamBufferReceiveCompletedFromISR((xMessageBuffer), (HigherPriorityTaskWoken))
