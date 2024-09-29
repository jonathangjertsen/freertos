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

#include "stream_buffer.h"

#include <stdbool.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.hpp"

#ifndef sbRECEIVE_COMPLETED
#define sbRECEIVE_COMPLETED(StreamBuffer)                            \
  do {                                                               \
    TaskSuspendAll();                                                \
    {                                                                \
      if ((StreamBuffer)->TaskWaitingToSend != NULL) {               \
        (void)TaskNotifyIndexed((StreamBuffer)->TaskWaitingToSend,   \
                                (StreamBuffer)->uxNotificationIndex, \
                                (uint32_t)0, eNoAction);             \
        (StreamBuffer)->TaskWaitingToSend = NULL;                    \
      }                                                              \
    }                                                                \
    (void)ResumeAll();                                               \
  } while (0)
#endif

#if (configUSE_SB_COMPLETED_CALLBACK == 1)
#define RECEIVE_COMPLETED(StreamBuffer)                                      \
  do {                                                                       \
    if ((StreamBuffer)->ReceiveCompletedCallback != NULL) {                  \
      (StreamBuffer)->ReceiveCompletedCallback((StreamBuffer), false, NULL); \
    } else {                                                                 \
      sbRECEIVE_COMPLETED((StreamBuffer));                                   \
    }                                                                        \
  } while (0)
#else
#define RECEIVE_COMPLETED(StreamBuffer) sbRECEIVE_COMPLETED((StreamBuffer))
#endif
#ifndef sbRECEIVE_COMPLETED_FROM_ISR
#define sbRECEIVE_COMPLETED_FROM_ISR(StreamBuffer, HigherPriorityTaskWoken) \
  do {                                                                      \
    UBaseType_t uxSavedInterruptStatus;                                     \
                                                                            \
    uxSavedInterruptStatus = ENTER_CRITICAL_FROM_ISR();                     \
    {                                                                       \
      if ((StreamBuffer)->TaskWaitingToSend != NULL) {                      \
        (void)TaskNotifyIndexedFromISR((StreamBuffer)->TaskWaitingToSend,   \
                                       (StreamBuffer)->uxNotificationIndex, \
                                       (uint32_t)0, eNoAction,              \
                                       (HigherPriorityTaskWoken));          \
        (StreamBuffer)->TaskWaitingToSend = NULL;                           \
      }                                                                     \
    }                                                                       \
    EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);                         \
  } while (0)
#endif
#if (configUSE_SB_COMPLETED_CALLBACK == 1)
#define RECEIVE_COMPLETED_FROM_ISR(StreamBuffer, HigherPriorityTaskWoken)      \
  do {                                                                         \
    if ((StreamBuffer)->ReceiveCompletedCallback != NULL) {                    \
      (StreamBuffer)                                                           \
          ->ReceiveCompletedCallback((StreamBuffer), true,                     \
                                     (HigherPriorityTaskWoken));               \
    } else {                                                                   \
      sbRECEIVE_COMPLETED_FROM_ISR((StreamBuffer), (HigherPriorityTaskWoken)); \
    }                                                                          \
  } while (0)
#else
#define RECEIVE_COMPLETED_FROM_ISR(StreamBuffer, HigherPriorityTaskWoken) \
  sbRECEIVE_COMPLETED_FROM_ISR((StreamBuffer), (HigherPriorityTaskWoken))
#endif

#ifndef sbSEND_COMPLETED
#define sbSEND_COMPLETED(StreamBuffer)                              \
  TaskSuspendAll();                                                 \
  {                                                                 \
    if ((StreamBuffer)->TaskWaitingToReceive != NULL) {             \
      (void)TaskNotifyIndexed((StreamBuffer)->TaskWaitingToReceive, \
                              (StreamBuffer)->uxNotificationIndex,  \
                              (uint32_t)0, eNoAction);              \
      (StreamBuffer)->TaskWaitingToReceive = NULL;                  \
    }                                                               \
  }                                                                 \
  (void)ResumeAll()
#endif

#if (configUSE_SB_COMPLETED_CALLBACK == 1)
#define SEND_COMPLETED(StreamBuffer)                                      \
  do {                                                                    \
    if ((StreamBuffer)->SendCompletedCallback != NULL) {                  \
      (StreamBuffer)->SendCompletedCallback((StreamBuffer), false, NULL); \
    } else {                                                              \
      sbSEND_COMPLETED((StreamBuffer));                                   \
    }                                                                     \
  } while (0)
#else
#define SEND_COMPLETED(StreamBuffer) sbSEND_COMPLETED((StreamBuffer))
#endif

#ifndef sbSEND_COMPLETE_FROM_ISR
#define sbSEND_COMPLETE_FROM_ISR(StreamBuffer, HigherPriorityTaskWoken)      \
  do {                                                                       \
    UBaseType_t uxSavedInterruptStatus;                                      \
                                                                             \
    uxSavedInterruptStatus = ENTER_CRITICAL_FROM_ISR();                      \
    {                                                                        \
      if ((StreamBuffer)->TaskWaitingToReceive != NULL) {                    \
        (void)TaskNotifyIndexedFromISR((StreamBuffer)->TaskWaitingToReceive, \
                                       (StreamBuffer)->uxNotificationIndex,  \
                                       (uint32_t)0, eNoAction,               \
                                       (HigherPriorityTaskWoken));           \
        (StreamBuffer)->TaskWaitingToReceive = NULL;                         \
      }                                                                      \
    }                                                                        \
    EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);                          \
  } while (0)
#endif

#if (configUSE_SB_COMPLETED_CALLBACK == 1)
#define SEND_COMPLETE_FROM_ISR(StreamBuffer, HigherPriorityTaskWoken)      \
  do {                                                                     \
    if ((StreamBuffer)->SendCompletedCallback != NULL) {                   \
      (StreamBuffer)                                                       \
          ->SendCompletedCallback((StreamBuffer), true,                    \
                                  (HigherPriorityTaskWoken));              \
    } else {                                                               \
      sbSEND_COMPLETE_FROM_ISR((StreamBuffer), (HigherPriorityTaskWoken)); \
    }                                                                      \
  } while (0)
#else
#define SEND_COMPLETE_FROM_ISR(StreamBuffer, HigherPriorityTaskWoken) \
  sbSEND_COMPLETE_FROM_ISR((StreamBuffer), (HigherPriorityTaskWoken))
#endif

#define sbBYTES_TO_STORE_MESSAGE_LENGTH \
  (sizeof(configMESSAGE_BUFFER_LENGTH_TYPE))

#define sbFLAGS_IS_MESSAGE_BUFFER ((uint8_t)1)
#define sbFLAGS_IS_STATICALLY_ALLOCATED ((uint8_t)2)
#define sbFLAGS_IS_BATCHING_BUFFER ((uint8_t)4)

typedef struct StreamBufferDef_t {
  volatile size_t xTail;
  volatile size_t xHead;
  size_t xLength;
  size_t xTriggerLevelBytes;
  volatile TaskHandle_t TaskWaitingToReceive;
  volatile TaskHandle_t TaskWaitingToSend;
  uint8_t* pucBuffer;
  uint8_t ucFlags;
#if (configUSE_SB_COMPLETED_CALLBACK == 1)
  StreamBufferCallbackFunction_t SendCompletedCallback;
  StreamBufferCallbackFunction_t ReceiveCompletedCallback;
#endif
  UBaseType_t uxNotificationIndex;
} StreamBuffer_t;

static size_t BytesInBuffer(const StreamBuffer_t* const StreamBuffer);

static size_t WriteBytesToBuffer(StreamBuffer_t* const StreamBuffer,
                                 const uint8_t* pucData, size_t xCount,
                                 size_t xHead);

static size_t ReadMessageFromBuffer(StreamBuffer_t* StreamBuffer,
                                    void* pvRxData, size_t xBufferLengthBytes,
                                    size_t xBytesAvailable);

static size_t WriteMessageToBuffer(StreamBuffer_t* const StreamBuffer,
                                   const void* pvTxData,
                                   size_t xDataLengthBytes, size_t xSpace,
                                   size_t xRequiredSpace);

static size_t ReadBytesFromBuffer(StreamBuffer_t* StreamBuffer,
                                  uint8_t* pucData, size_t xCount,
                                  size_t xTail);

static void InitialiseNewStreamBuffer(
    StreamBuffer_t* const StreamBuffer, uint8_t* const pucBuffer,
    size_t xBufferSizeBytes, size_t xTriggerLevelBytes, uint8_t ucFlags,
    StreamBufferCallbackFunction_t SendCompletedCallback,
    StreamBufferCallbackFunction_t ReceiveCompletedCallback);

#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
StreamBufferHandle_t xStreamBufferGenericCreate(
    size_t xBufferSizeBytes, size_t xTriggerLevelBytes,
    BaseType_t xStreamBufferType,
    StreamBufferCallbackFunction_t SendCompletedCallback,
    StreamBufferCallbackFunction_t ReceiveCompletedCallback) {
  void* pvAllocatedMemory;
  uint8_t ucFlags;

  if (xStreamBufferType == sbTYPE_MESSAGE_BUFFER) {
    ucFlags = sbFLAGS_IS_MESSAGE_BUFFER;
    configASSERT(xBufferSizeBytes > sbBYTES_TO_STORE_MESSAGE_LENGTH);
  } else if (xStreamBufferType == sbTYPE_STREAM_BATCHING_BUFFER) {
    ucFlags = sbFLAGS_IS_BATCHING_BUFFER;
    configASSERT(xBufferSizeBytes > 0);
  } else {
    ucFlags = 0;
    configASSERT(xBufferSizeBytes > 0);
  }
  configASSERT(xTriggerLevelBytes <= xBufferSizeBytes);

  if (xTriggerLevelBytes == (size_t)0) {
    xTriggerLevelBytes = (size_t)1;
  }

  if (xBufferSizeBytes < (xBufferSizeBytes + 1U + sizeof(StreamBuffer_t))) {
    xBufferSizeBytes++;
    pvAllocatedMemory = pvPortMalloc(xBufferSizeBytes + sizeof(StreamBuffer_t));
  } else {
    pvAllocatedMemory = NULL;
  }
  if (pvAllocatedMemory != NULL) {
    InitialiseNewStreamBuffer(
        (StreamBuffer_t*)pvAllocatedMemory,

        ((uint8_t*)pvAllocatedMemory) + sizeof(StreamBuffer_t),
        xBufferSizeBytes, xTriggerLevelBytes, ucFlags, SendCompletedCallback,
        ReceiveCompletedCallback);
  } else {
  }

  return (StreamBufferHandle_t)pvAllocatedMemory;
}
#endif

#if (configSUPPORT_STATIC_ALLOCATION == 1)
StreamBufferHandle_t xStreamBufferGenericCreateStatic(
    size_t xBufferSizeBytes, size_t xTriggerLevelBytes,
    BaseType_t xStreamBufferType, uint8_t* const pucStreamBufferStorageArea,
    StaticStreamBuffer_t* const pStaticStreamBuffer,
    StreamBufferCallbackFunction_t SendCompletedCallback,
    StreamBufferCallbackFunction_t ReceiveCompletedCallback) {
  StreamBuffer_t* const StreamBuffer = (StreamBuffer_t*)pStaticStreamBuffer;
  StreamBufferHandle_t Ret;
  uint8_t ucFlags;

  configASSERT(pucStreamBufferStorageArea);
  configASSERT(pStaticStreamBuffer);
  configASSERT(xTriggerLevelBytes <= xBufferSizeBytes);

  if (xTriggerLevelBytes == (size_t)0) {
    xTriggerLevelBytes = (size_t)1;
  }

  if (xStreamBufferType == sbTYPE_MESSAGE_BUFFER) {
    ucFlags = sbFLAGS_IS_MESSAGE_BUFFER | sbFLAGS_IS_STATICALLY_ALLOCATED;
    configASSERT(xBufferSizeBytes > sbBYTES_TO_STORE_MESSAGE_LENGTH);
  } else if (xStreamBufferType == sbTYPE_STREAM_BATCHING_BUFFER) {
    ucFlags = sbFLAGS_IS_BATCHING_BUFFER | sbFLAGS_IS_STATICALLY_ALLOCATED;
    configASSERT(xBufferSizeBytes > 0);
  } else {
    ucFlags = sbFLAGS_IS_STATICALLY_ALLOCATED;
  }
#if (configASSERT_DEFINED == 1)
  {
    volatile size_t xSize = sizeof(StaticStreamBuffer_t);
    configASSERT(xSize == sizeof(StreamBuffer_t));
  }
#endif
  if ((pucStreamBufferStorageArea != NULL) && (pStaticStreamBuffer != NULL)) {
    InitialiseNewStreamBuffer(StreamBuffer, pucStreamBufferStorageArea,
                              xBufferSizeBytes, xTriggerLevelBytes, ucFlags,
                              SendCompletedCallback, ReceiveCompletedCallback);

    StreamBuffer->ucFlags |= sbFLAGS_IS_STATICALLY_ALLOCATED;

    Ret = (StreamBufferHandle_t)pStaticStreamBuffer;
  } else {
    Ret = NULL;
  }

  return Ret;
}
#endif

#if (configSUPPORT_STATIC_ALLOCATION == 1)
BaseType_t xStreamBufferGetStaticBuffers(
    StreamBufferHandle_t xStreamBuffer, uint8_t** ppucStreamBufferStorageArea,
    StaticStreamBuffer_t** ppStaticStreamBuffer) {
  BaseType_t Ret;
  StreamBuffer_t* const StreamBuffer = xStreamBuffer;

  configASSERT(StreamBuffer);
  configASSERT(ppucStreamBufferStorageArea);
  configASSERT(ppStaticStreamBuffer);
  if ((StreamBuffer->ucFlags & sbFLAGS_IS_STATICALLY_ALLOCATED) != (uint8_t)0) {
    *ppucStreamBufferStorageArea = StreamBuffer->pucBuffer;

    *ppStaticStreamBuffer = (StaticStreamBuffer_t*)StreamBuffer;
    Ret = true;
  } else {
    Ret = false;
  }

  return Ret;
}
#endif

void vStreamBufferDelete(StreamBufferHandle_t xStreamBuffer) {
  StreamBuffer_t* StreamBuffer = xStreamBuffer;
  configASSERT(StreamBuffer);
  if ((StreamBuffer->ucFlags & sbFLAGS_IS_STATICALLY_ALLOCATED) ==
      (uint8_t) false) {
#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
    { vPortFree((void*)StreamBuffer); }
#else
    {
      configASSERT(xStreamBuffer == (StreamBufferHandle_t)~0);
    }
#endif
  } else {
    (void)memset(StreamBuffer, 0x00, sizeof(StreamBuffer_t));
  }
}

BaseType_t xStreamBufferReset(StreamBufferHandle_t xStreamBuffer) {
  StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  BaseType_t Ret = false;
  StreamBufferCallbackFunction_t SendCallback = NULL, ReceiveCallback = NULL;
  configASSERT(StreamBuffer);

  ENTER_CRITICAL();
  {
    if ((StreamBuffer->TaskWaitingToReceive == NULL) &&
        (StreamBuffer->TaskWaitingToSend == NULL)) {
#if (configUSE_SB_COMPLETED_CALLBACK == 1)
      {
        SendCallback = StreamBuffer->SendCompletedCallback;
        ReceiveCallback = StreamBuffer->ReceiveCompletedCallback;
      }
#endif
      InitialiseNewStreamBuffer(
          StreamBuffer, StreamBuffer->pucBuffer, StreamBuffer->xLength,
          StreamBuffer->xTriggerLevelBytes, StreamBuffer->ucFlags, SendCallback,
          ReceiveCallback);
      Ret = true;
    }
  }
  EXIT_CRITICAL();
  return Ret;
}

BaseType_t xStreamBufferResetFromISR(StreamBufferHandle_t xStreamBuffer) {
  StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  BaseType_t Ret = false;
  StreamBufferCallbackFunction_t SendCallback = NULL, ReceiveCallback = NULL;
  UBaseType_t uxSavedInterruptStatus;
  configASSERT(StreamBuffer);

  uxSavedInterruptStatus = ENTER_CRITICAL_FROM_ISR();
  {
    if ((StreamBuffer->TaskWaitingToReceive == NULL) &&
        (StreamBuffer->TaskWaitingToSend == NULL)) {
#if (configUSE_SB_COMPLETED_CALLBACK == 1)
      {
        SendCallback = StreamBuffer->SendCompletedCallback;
        ReceiveCallback = StreamBuffer->ReceiveCompletedCallback;
      }
#endif
      InitialiseNewStreamBuffer(
          StreamBuffer, StreamBuffer->pucBuffer, StreamBuffer->xLength,
          StreamBuffer->xTriggerLevelBytes, StreamBuffer->ucFlags, SendCallback,
          ReceiveCallback);
      Ret = true;
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return Ret;
}

BaseType_t xStreamBufferSetTriggerLevel(StreamBufferHandle_t xStreamBuffer,
                                        size_t xTriggerLevel) {
  StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  BaseType_t Ret;
  configASSERT(StreamBuffer);

  if (xTriggerLevel == (size_t)0) {
    xTriggerLevel = (size_t)1;
  }

  if (xTriggerLevel < StreamBuffer->xLength) {
    StreamBuffer->xTriggerLevelBytes = xTriggerLevel;
    Ret = true;
  } else {
    Ret = false;
  }
  return Ret;
}

size_t xStreamBufferSpacesAvailable(StreamBufferHandle_t xStreamBuffer) {
  const StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  size_t xSpace;
  size_t xOriginalTail;
  configASSERT(StreamBuffer);

  do {
    xOriginalTail = StreamBuffer->xTail;
    xSpace = StreamBuffer->xLength + StreamBuffer->xTail;
    xSpace -= StreamBuffer->xHead;
  } while (xOriginalTail != StreamBuffer->xTail);
  xSpace -= (size_t)1;
  if (xSpace >= StreamBuffer->xLength) {
    xSpace -= StreamBuffer->xLength;
  }
  return xSpace;
}

size_t xStreamBufferBytesAvailable(StreamBufferHandle_t xStreamBuffer) {
  const StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  size_t Ret;
  configASSERT(StreamBuffer);
  Ret = BytesInBuffer(StreamBuffer);
  return Ret;
}

size_t xStreamBufferSend(StreamBufferHandle_t xStreamBuffer,
                         const void* pvTxData, size_t xDataLengthBytes,
                         TickType_t xTicksToWait) {
  StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  size_t Ret, xSpace = 0;
  size_t xRequiredSpace = xDataLengthBytes;
  TimeOut_t xTimeOut;
  size_t xMaxReportedSpace = 0;
  configASSERT(pvTxData);
  configASSERT(StreamBuffer);

  xMaxReportedSpace = StreamBuffer->xLength - (size_t)1;

  if ((StreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
    xRequiredSpace += sbBYTES_TO_STORE_MESSAGE_LENGTH;

    configASSERT(xRequiredSpace > xDataLengthBytes);

    if (xRequiredSpace > xMaxReportedSpace) {
      xTicksToWait = (TickType_t)0;
    }
  } else {
    if (xRequiredSpace > xMaxReportedSpace) {
      xRequiredSpace = xMaxReportedSpace;
    }
  }
  if (xTicksToWait != (TickType_t)0) {
    TaskSetTimeOutState(&xTimeOut);
    do {
      ENTER_CRITICAL();
      {
        xSpace = xStreamBufferSpacesAvailable(StreamBuffer);
        if (xSpace < xRequiredSpace) {
          (void)TaskNotifyStateClearIndexed(NULL,
                                            StreamBuffer->uxNotificationIndex);

          configASSERT(StreamBuffer->TaskWaitingToSend == NULL);
          StreamBuffer->TaskWaitingToSend = GetCurrentTaskHandle();
        } else {
          EXIT_CRITICAL();
          break;
        }
      }
      EXIT_CRITICAL();
      (void)TaskNotifyWaitIndexed(StreamBuffer->uxNotificationIndex,
                                  (uint32_t)0, (uint32_t)0, NULL, xTicksToWait);
      StreamBuffer->TaskWaitingToSend = NULL;
    } while (CheckForTimeOut(&xTimeOut, &xTicksToWait) == false);
  }
  if (xSpace == (size_t)0) {
    xSpace = xStreamBufferSpacesAvailable(StreamBuffer);
  }
  Ret = WriteMessageToBuffer(StreamBuffer, pvTxData, xDataLengthBytes, xSpace,
                             xRequiredSpace);
  if (Ret > (size_t)0) {
    if (BytesInBuffer(StreamBuffer) >= StreamBuffer->xTriggerLevelBytes) {
      SEND_COMPLETED(StreamBuffer);
    }
  }
  return Ret;
}

size_t xStreamBufferSendFromISR(StreamBufferHandle_t xStreamBuffer,
                                const void* pvTxData, size_t xDataLengthBytes,
                                BaseType_t* const HigherPriorityTaskWoken) {
  StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  size_t Ret, xSpace;
  size_t xRequiredSpace = xDataLengthBytes;
  configASSERT(pvTxData);
  configASSERT(StreamBuffer);

  if ((StreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
    xRequiredSpace += sbBYTES_TO_STORE_MESSAGE_LENGTH;
  }
  xSpace = xStreamBufferSpacesAvailable(StreamBuffer);
  Ret = WriteMessageToBuffer(StreamBuffer, pvTxData, xDataLengthBytes, xSpace,
                             xRequiredSpace);
  if (Ret > (size_t)0) {
    if (BytesInBuffer(StreamBuffer) >= StreamBuffer->xTriggerLevelBytes) {
      SEND_COMPLETE_FROM_ISR(StreamBuffer, HigherPriorityTaskWoken);
    }
  }
  return Ret;
}

static size_t WriteMessageToBuffer(StreamBuffer_t* const StreamBuffer,
                                   const void* pvTxData,
                                   size_t xDataLengthBytes, size_t xSpace,
                                   size_t xRequiredSpace) {
  size_t xNextHead = StreamBuffer->xHead;
  configMESSAGE_BUFFER_LENGTH_TYPE xMessageLength;
  if ((StreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
    xMessageLength = (configMESSAGE_BUFFER_LENGTH_TYPE)xDataLengthBytes;

    configASSERT((size_t)xMessageLength == xDataLengthBytes);
    if (xSpace >= xRequiredSpace) {
      xNextHead =
          WriteBytesToBuffer(StreamBuffer, (const uint8_t*)&(xMessageLength),
                             sbBYTES_TO_STORE_MESSAGE_LENGTH, xNextHead);
    } else {
      xDataLengthBytes = 0;
    }
  } else {
    xDataLengthBytes = configMIN(xDataLengthBytes, xSpace);
  }
  if (xDataLengthBytes != (size_t)0) {
    StreamBuffer->xHead = WriteBytesToBuffer(
        StreamBuffer, (const uint8_t*)pvTxData, xDataLengthBytes, xNextHead);
  }
  return xDataLengthBytes;
}

size_t xStreamBufferReceive(StreamBufferHandle_t xStreamBuffer, void* pvRxData,
                            size_t xBufferLengthBytes,
                            TickType_t xTicksToWait) {
  StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  size_t xReceivedLength = 0, xBytesAvailable, xBytesToStoreMessageLength;
  configASSERT(pvRxData);
  configASSERT(StreamBuffer);

  if ((StreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
    xBytesToStoreMessageLength = sbBYTES_TO_STORE_MESSAGE_LENGTH;
  } else if ((StreamBuffer->ucFlags & sbFLAGS_IS_BATCHING_BUFFER) !=
             (uint8_t)0) {
    xBytesToStoreMessageLength = StreamBuffer->xTriggerLevelBytes;
  } else {
    xBytesToStoreMessageLength = 0;
  }
  if (xTicksToWait != (TickType_t)0) {
    ENTER_CRITICAL();
    {
      xBytesAvailable = BytesInBuffer(StreamBuffer);

      if (xBytesAvailable <= xBytesToStoreMessageLength) {
        (void)TaskNotifyStateClearIndexed(NULL,
                                          StreamBuffer->uxNotificationIndex);

        configASSERT(StreamBuffer->TaskWaitingToReceive == NULL);
        StreamBuffer->TaskWaitingToReceive = GetCurrentTaskHandle();
      }
    }
    EXIT_CRITICAL();
    if (xBytesAvailable <= xBytesToStoreMessageLength) {
      (void)TaskNotifyWaitIndexed(StreamBuffer->uxNotificationIndex,
                                  (uint32_t)0, (uint32_t)0, NULL, xTicksToWait);
      StreamBuffer->TaskWaitingToReceive = NULL;

      xBytesAvailable = BytesInBuffer(StreamBuffer);
    }
  } else {
    xBytesAvailable = BytesInBuffer(StreamBuffer);
  }

  if (xBytesAvailable > xBytesToStoreMessageLength) {
    xReceivedLength = ReadMessageFromBuffer(
        StreamBuffer, pvRxData, xBufferLengthBytes, xBytesAvailable);

    if (xReceivedLength != (size_t)0) {
      RECEIVE_COMPLETED(xStreamBuffer);
    }
  }
  return xReceivedLength;
}

size_t xStreamBufferNextMessageLengthBytes(StreamBufferHandle_t xStreamBuffer) {
  StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  size_t Ret, xBytesAvailable;
  configMESSAGE_BUFFER_LENGTH_TYPE xTempReturn;
  configASSERT(StreamBuffer);

  if ((StreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
    xBytesAvailable = BytesInBuffer(StreamBuffer);
    if (xBytesAvailable > sbBYTES_TO_STORE_MESSAGE_LENGTH) {
      (void)ReadBytesFromBuffer(StreamBuffer, (uint8_t*)&xTempReturn,
                                sbBYTES_TO_STORE_MESSAGE_LENGTH,
                                StreamBuffer->xTail);
      Ret = (size_t)xTempReturn;
    } else {
      configASSERT(xBytesAvailable == 0);
      Ret = 0;
    }
  } else {
    Ret = 0;
  }
  return Ret;
}

size_t xStreamBufferReceiveFromISR(StreamBufferHandle_t xStreamBuffer,
                                   void* pvRxData, size_t xBufferLengthBytes,
                                   BaseType_t* const HigherPriorityTaskWoken) {
  StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  size_t xReceivedLength = 0, xBytesAvailable, xBytesToStoreMessageLength;
  configASSERT(pvRxData);
  configASSERT(StreamBuffer);

  if ((StreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
    xBytesToStoreMessageLength = sbBYTES_TO_STORE_MESSAGE_LENGTH;
  } else {
    xBytesToStoreMessageLength = 0;
  }
  xBytesAvailable = BytesInBuffer(StreamBuffer);

  if (xBytesAvailable > xBytesToStoreMessageLength) {
    xReceivedLength = ReadMessageFromBuffer(
        StreamBuffer, pvRxData, xBufferLengthBytes, xBytesAvailable);

    if (xReceivedLength != (size_t)0) {
      RECEIVE_COMPLETED_FROM_ISR(StreamBuffer, HigherPriorityTaskWoken);
    }
  }
  return xReceivedLength;
}

static size_t ReadMessageFromBuffer(StreamBuffer_t* StreamBuffer,
                                    void* pvRxData, size_t xBufferLengthBytes,
                                    size_t xBytesAvailable) {
  size_t xCount, xNextMessageLength;
  configMESSAGE_BUFFER_LENGTH_TYPE xTempNextMessageLength;
  size_t xNextTail = StreamBuffer->xTail;
  if ((StreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
    xNextTail =
        ReadBytesFromBuffer(StreamBuffer, (uint8_t*)&xTempNextMessageLength,
                            sbBYTES_TO_STORE_MESSAGE_LENGTH, xNextTail);
    xNextMessageLength = (size_t)xTempNextMessageLength;

    xBytesAvailable -= sbBYTES_TO_STORE_MESSAGE_LENGTH;

    if (xNextMessageLength > xBufferLengthBytes) {
      xNextMessageLength = 0;
    }
  } else {
    xNextMessageLength = xBufferLengthBytes;
  }

  xCount = configMIN(xNextMessageLength, xBytesAvailable);
  if (xCount != (size_t)0) {
    StreamBuffer->xTail = ReadBytesFromBuffer(StreamBuffer, (uint8_t*)pvRxData,
                                              xCount, xNextTail);
  }
  return xCount;
}

BaseType_t xStreamBufferIsEmpty(StreamBufferHandle_t xStreamBuffer) {
  const StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  BaseType_t Ret;
  size_t xTail;
  configASSERT(StreamBuffer);

  xTail = StreamBuffer->xTail;
  if (StreamBuffer->xHead == xTail) {
    Ret = true;
  } else {
    Ret = false;
  }
  return Ret;
}

BaseType_t xStreamBufferIsFull(StreamBufferHandle_t xStreamBuffer) {
  BaseType_t Ret;
  size_t xBytesToStoreMessageLength;
  const StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  configASSERT(StreamBuffer);

  if ((StreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
    xBytesToStoreMessageLength = sbBYTES_TO_STORE_MESSAGE_LENGTH;
  } else {
    xBytesToStoreMessageLength = 0;
  }

  if (xStreamBufferSpacesAvailable(xStreamBuffer) <=
      xBytesToStoreMessageLength) {
    Ret = true;
  } else {
    Ret = false;
  }
  return Ret;
}

BaseType_t xStreamBufferSendCompletedFromISR(
    StreamBufferHandle_t xStreamBuffer, BaseType_t* HigherPriorityTaskWoken) {
  StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  BaseType_t Ret;
  UBaseType_t uxSavedInterruptStatus;
  configASSERT(StreamBuffer);

  uxSavedInterruptStatus = ENTER_CRITICAL_FROM_ISR();
  {
    if ((StreamBuffer)->TaskWaitingToReceive != NULL) {
      (void)TaskNotifyIndexedFromISR((StreamBuffer)->TaskWaitingToReceive,
                                     (StreamBuffer)->uxNotificationIndex,
                                     (uint32_t)0, eNoAction,
                                     HigherPriorityTaskWoken);
      (StreamBuffer)->TaskWaitingToReceive = NULL;
      Ret = true;
    } else {
      Ret = false;
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return Ret;
}

BaseType_t xStreamBufferReceiveCompletedFromISR(
    StreamBufferHandle_t xStreamBuffer, BaseType_t* HigherPriorityTaskWoken) {
  StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  BaseType_t Ret;
  UBaseType_t uxSavedInterruptStatus;
  configASSERT(StreamBuffer);

  uxSavedInterruptStatus = ENTER_CRITICAL_FROM_ISR();
  {
    if ((StreamBuffer)->TaskWaitingToSend != NULL) {
      (void)TaskNotifyIndexedFromISR((StreamBuffer)->TaskWaitingToSend,
                                     (StreamBuffer)->uxNotificationIndex,
                                     (uint32_t)0, eNoAction,
                                     HigherPriorityTaskWoken);
      (StreamBuffer)->TaskWaitingToSend = NULL;
      Ret = true;
    } else {
      Ret = false;
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return Ret;
}

static size_t WriteBytesToBuffer(StreamBuffer_t* const StreamBuffer,
                                 const uint8_t* pucData, size_t xCount,
                                 size_t xHead) {
  size_t xFirstLength;
  configASSERT(xCount > (size_t)0);

  xFirstLength = configMIN(StreamBuffer->xLength - xHead, xCount);

  configASSERT((xHead + xFirstLength) <= StreamBuffer->xLength);
  (void)memcpy((void*)(&(StreamBuffer->pucBuffer[xHead])), (const void*)pucData,
               xFirstLength);

  if (xCount > xFirstLength) {
    configASSERT((xCount - xFirstLength) <= StreamBuffer->xLength);
    (void)memcpy((void*)StreamBuffer->pucBuffer,
                 (const void*)&(pucData[xFirstLength]), xCount - xFirstLength);
  }
  xHead += xCount;
  if (xHead >= StreamBuffer->xLength) {
    xHead -= StreamBuffer->xLength;
  }
  return xHead;
}

static size_t ReadBytesFromBuffer(StreamBuffer_t* StreamBuffer,
                                  uint8_t* pucData, size_t xCount,
                                  size_t xTail) {
  size_t xFirstLength;
  configASSERT(xCount != (size_t)0);

  xFirstLength = configMIN(StreamBuffer->xLength - xTail, xCount);

  configASSERT(xFirstLength <= xCount);
  configASSERT((xTail + xFirstLength) <= StreamBuffer->xLength);
  (void)memcpy((void*)pucData, (const void*)&(StreamBuffer->pucBuffer[xTail]),
               xFirstLength);

  if (xCount > xFirstLength) {
    (void)memcpy((void*)&(pucData[xFirstLength]),
                 (void*)(StreamBuffer->pucBuffer), xCount - xFirstLength);
  }

  xTail += xCount;
  if (xTail >= StreamBuffer->xLength) {
    xTail -= StreamBuffer->xLength;
  }
  return xTail;
}

static size_t BytesInBuffer(const StreamBuffer_t* const StreamBuffer) {
  size_t xCount;
  xCount = StreamBuffer->xLength + StreamBuffer->xHead;
  xCount -= StreamBuffer->xTail;
  if (xCount >= StreamBuffer->xLength) {
    xCount -= StreamBuffer->xLength;
  }
  return xCount;
}

static void InitialiseNewStreamBuffer(
    StreamBuffer_t* const StreamBuffer, uint8_t* const pucBuffer,
    size_t xBufferSizeBytes, size_t xTriggerLevelBytes, uint8_t ucFlags,
    StreamBufferCallbackFunction_t SendCompletedCallback,
    StreamBufferCallbackFunction_t ReceiveCompletedCallback) {
#if (configASSERT_DEFINED == 1)
  {
#define STREAM_BUFFER_BUFFER_WRITE_VALUE (0x55)
    configASSERT(memset(pucBuffer, (int)STREAM_BUFFER_BUFFER_WRITE_VALUE,
                        xBufferSizeBytes) == pucBuffer);
  }
#endif
  (void)memset((void*)StreamBuffer, 0x00, sizeof(StreamBuffer_t));
  StreamBuffer->pucBuffer = pucBuffer;
  StreamBuffer->xLength = xBufferSizeBytes;
  StreamBuffer->xTriggerLevelBytes = xTriggerLevelBytes;
  StreamBuffer->ucFlags = ucFlags;
  StreamBuffer->uxNotificationIndex = tskDEFAULT_INDEX_TO_NOTIFY;
#if (configUSE_SB_COMPLETED_CALLBACK == 1)
  {
    StreamBuffer->SendCompletedCallback = SendCompletedCallback;
    StreamBuffer->ReceiveCompletedCallback = ReceiveCompletedCallback;
  }
#else
  {
    (void)SendCompletedCallback;
    (void)ReceiveCompletedCallback;
  }
#endif
}

UBaseType_t uxStreamBufferGetStreamBufferNotificationIndex(
    StreamBufferHandle_t xStreamBuffer) {
  StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  configASSERT(StreamBuffer);
  return StreamBuffer->uxNotificationIndex;
}

void vStreamBufferSetStreamBufferNotificationIndex(
    StreamBufferHandle_t xStreamBuffer, UBaseType_t uxNotificationIndex) {
  StreamBuffer_t* const StreamBuffer = xStreamBuffer;
  configASSERT(StreamBuffer);

  configASSERT(StreamBuffer->TaskWaitingToReceive == NULL);
  configASSERT(StreamBuffer->TaskWaitingToSend == NULL);

  configASSERT(uxNotificationIndex < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  StreamBuffer->uxNotificationIndex = uxNotificationIndex;
}
