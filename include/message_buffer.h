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
 

 
#ifndef FREERTOS_MESSAGE_BUFFER_H
#define FREERTOS_MESSAGE_BUFFER_H

 
#include "stream_buffer.h"
 
#if defined(__cplusplus)
extern "C" {
#endif
 
 
typedef StreamBufferHandle_t MessageBufferHandle_t;

 
#define xMessageBufferCreate(xBufferSizeBytes)              \
  xStreamBufferGenericCreate((xBufferSizeBytes), (size_t)0, \
                             sbTYPE_MESSAGE_BUFFER, NULL, NULL)
#if (configUSE_SB_COMPLETED_CALLBACK == 1)
#define xMessageBufferCreateWithCallback(                                      \
    xBufferSizeBytes, pxSendCompletedCallback, pxReceiveCompletedCallback)     \
  xStreamBufferGenericCreate((xBufferSizeBytes), (size_t)0,                    \
                             sbTYPE_MESSAGE_BUFFER, (pxSendCompletedCallback), \
                             (pxReceiveCompletedCallback))
#endif
 
#define xMessageBufferCreateStatic(                                      \
    xBufferSizeBytes, pucMessageBufferStorageArea, pStaticMessageBuffer) \
  xStreamBufferGenericCreateStatic(                                      \
      (xBufferSizeBytes), 0, sbTYPE_MESSAGE_BUFFER,                      \
      (pucMessageBufferStorageArea), (pStaticMessageBuffer), NULL, NULL)
#if (configUSE_SB_COMPLETED_CALLBACK == 1)
#define xMessageBufferCreateStaticWithCallback(                          \
    xBufferSizeBytes, pucMessageBufferStorageArea, pStaticMessageBuffer, \
    pxSendCompletedCallback, pxReceiveCompletedCallback)                 \
  xStreamBufferGenericCreateStatic(                                      \
      (xBufferSizeBytes), 0, sbTYPE_MESSAGE_BUFFER,                      \
      (pucMessageBufferStorageArea), (pStaticMessageBuffer),             \
      (pxSendCompletedCallback), (pxReceiveCompletedCallback))
#endif
 
#if (configSUPPORT_STATIC_ALLOCATION == 1)
#define xMessageBufferGetStaticBuffers(                                  \
    xMessageBuffer, ppucMessageBufferStorageArea, ppStaticMessageBuffer) \
  xStreamBufferGetStaticBuffers((xMessageBuffer),                        \
                                (ppucMessageBufferStorageArea),          \
                                (ppStaticMessageBuffer))
#endif  
 
#define xMessageBufferSend(xMessageBuffer, pvTxData, xDataLengthBytes, \
                           xTicksToWait)                               \
  xStreamBufferSend((xMessageBuffer), (pvTxData), (xDataLengthBytes),  \
                    (xTicksToWait))
 
#define xMessageBufferSendFromISR(xMessageBuffer, pvTxData, xDataLengthBytes, \
                                  pxHigherPriorityTaskWoken)                  \
  xStreamBufferSendFromISR((xMessageBuffer), (pvTxData), (xDataLengthBytes),  \
                           (pxHigherPriorityTaskWoken))
 
#define xMessageBufferReceive(xMessageBuffer, pvRxData, xBufferLengthBytes, \
                              xTicksToWait)                                 \
  xStreamBufferReceive((xMessageBuffer), (pvRxData), (xBufferLengthBytes),  \
                       (xTicksToWait))

 
#define xMessageBufferReceiveFromISR(                                        \
    xMessageBuffer, pvRxData, xBufferLengthBytes, pxHigherPriorityTaskWoken) \
  xStreamBufferReceiveFromISR((xMessageBuffer), (pvRxData),                  \
                              (xBufferLengthBytes),                          \
                              (pxHigherPriorityTaskWoken))
 
#define vMessageBufferDelete(xMessageBuffer) vStreamBufferDelete(xMessageBuffer)
 
#define xMessageBufferIsFull(xMessageBuffer) xStreamBufferIsFull(xMessageBuffer)
 
#define xMessageBufferIsEmpty(xMessageBuffer) \
  xStreamBufferIsEmpty(xMessageBuffer)
 
#define xMessageBufferReset(xMessageBuffer) xStreamBufferReset(xMessageBuffer)

 
#define xMessageBufferResetFromISR(xMessageBuffer) \
  xStreamBufferResetFromISR(xMessageBuffer)
 
#define xMessageBufferSpaceAvailable(xMessageBuffer) \
  xStreamBufferSpacesAvailable(xMessageBuffer)
#define xMessageBufferSpacesAvailable(xMessageBuffer) \
  xStreamBufferSpacesAvailable(                       \
      xMessageBuffer)  
 
#define xMessageBufferNextLengthBytes(xMessageBuffer) \
  xStreamBufferNextMessageLengthBytes(xMessageBuffer)
 
#define xMessageBufferSendCompletedFromISR(xMessageBuffer,            \
                                           pxHigherPriorityTaskWoken) \
  xStreamBufferSendCompletedFromISR((xMessageBuffer),                 \
                                    (pxHigherPriorityTaskWoken))
 
#define xMessageBufferReceiveCompletedFromISR(xMessageBuffer,            \
                                              pxHigherPriorityTaskWoken) \
  xStreamBufferReceiveCompletedFromISR((xMessageBuffer),                 \
                                       (pxHigherPriorityTaskWoken))
 
#if defined(__cplusplus)
}  
#endif
 
#endif  
