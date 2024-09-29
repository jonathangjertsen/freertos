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
 
 
#include <stdbool.h>
#include <string.h>

 
#include "FreeRTOS.h"
#include "stream_buffer.h"
#include "task.hpp"

 
#ifndef sbRECEIVE_COMPLETED
#define sbRECEIVE_COMPLETED(pxStreamBuffer)                             \
  do {                                                                  \
    vTaskSuspendAll();                                                  \
    {                                                                   \
      if ((pxStreamBuffer)->xTaskWaitingToSend != NULL) {               \
        (void)xTaskNotifyIndexed((pxStreamBuffer)->xTaskWaitingToSend,  \
                                 (pxStreamBuffer)->uxNotificationIndex, \
                                 (uint32_t)0, eNoAction);               \
        (pxStreamBuffer)->xTaskWaitingToSend = NULL;                    \
      }                                                                 \
    }                                                                   \
    (void)TaskResumeAll();                                              \
  } while (0)
#endif  
        
#if (configUSE_SB_COMPLETED_CALLBACK == 1)
#define RECEIVE_COMPLETED(pxStreamBuffer)                              \
  do {                                                                 \
    if ((pxStreamBuffer)->pxReceiveCompletedCallback != NULL) {        \
      (pxStreamBuffer)                                                 \
          ->pxReceiveCompletedCallback((pxStreamBuffer), false, NULL); \
    } else {                                                           \
      sbRECEIVE_COMPLETED((pxStreamBuffer));                           \
    }                                                                  \
  } while (0)
#else  
#define RECEIVE_COMPLETED(pxStreamBuffer) sbRECEIVE_COMPLETED((pxStreamBuffer))
#endif  
#ifndef sbRECEIVE_COMPLETED_FROM_ISR
#define sbRECEIVE_COMPLETED_FROM_ISR(pxStreamBuffer,                           \
                                     pxHigherPriorityTaskWoken)                \
  do {                                                                         \
    UBaseType_t uxSavedInterruptStatus;                                        \
                                                                               \
    uxSavedInterruptStatus = ENTER_CRITICAL_FROM_ISR();                        \
    {                                                                          \
      if ((pxStreamBuffer)->xTaskWaitingToSend != NULL) {                      \
        (void)xTaskNotifyIndexedFromISR((pxStreamBuffer)->xTaskWaitingToSend,  \
                                        (pxStreamBuffer)->uxNotificationIndex, \
                                        (uint32_t)0, eNoAction,                \
                                        (pxHigherPriorityTaskWoken));          \
        (pxStreamBuffer)->xTaskWaitingToSend = NULL;                           \
      }                                                                        \
    }                                                                          \
    EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);                            \
  } while (0)
#endif  
#if (configUSE_SB_COMPLETED_CALLBACK == 1)
#define RECEIVE_COMPLETED_FROM_ISR(pxStreamBuffer, pxHigherPriorityTaskWoken) \
  do {                                                                        \
    if ((pxStreamBuffer)->pxReceiveCompletedCallback != NULL) {               \
      (pxStreamBuffer)                                                        \
          ->pxReceiveCompletedCallback((pxStreamBuffer), true,                \
                                       (pxHigherPriorityTaskWoken));          \
    } else {                                                                  \
      sbRECEIVE_COMPLETED_FROM_ISR((pxStreamBuffer),                          \
                                   (pxHigherPriorityTaskWoken));              \
    }                                                                         \
  } while (0)
#else  
#define RECEIVE_COMPLETED_FROM_ISR(pxStreamBuffer, pxHigherPriorityTaskWoken) \
  sbRECEIVE_COMPLETED_FROM_ISR((pxStreamBuffer), (pxHigherPriorityTaskWoken))
#endif  
 
#ifndef sbSEND_COMPLETED
#define sbSEND_COMPLETED(pxStreamBuffer)                                \
  vTaskSuspendAll();                                                    \
  {                                                                     \
    if ((pxStreamBuffer)->xTaskWaitingToReceive != NULL) {              \
      (void)xTaskNotifyIndexed((pxStreamBuffer)->xTaskWaitingToReceive, \
                               (pxStreamBuffer)->uxNotificationIndex,   \
                               (uint32_t)0, eNoAction);                 \
      (pxStreamBuffer)->xTaskWaitingToReceive = NULL;                   \
    }                                                                   \
  }                                                                     \
  (void)TaskResumeAll()
#endif  
        
#if (configUSE_SB_COMPLETED_CALLBACK == 1)
#define SEND_COMPLETED(pxStreamBuffer)                              \
  do {                                                              \
    if ((pxStreamBuffer)->pxSendCompletedCallback != NULL) {        \
      (pxStreamBuffer)                                              \
          ->pxSendCompletedCallback((pxStreamBuffer), false, NULL); \
    } else {                                                        \
      sbSEND_COMPLETED((pxStreamBuffer));                           \
    }                                                               \
  } while (0)
#else  
#define SEND_COMPLETED(pxStreamBuffer) sbSEND_COMPLETED((pxStreamBuffer))
#endif  

#ifndef sbSEND_COMPLETE_FROM_ISR
#define sbSEND_COMPLETE_FROM_ISR(pxStreamBuffer, pxHigherPriorityTaskWoken) \
  do {                                                                      \
    UBaseType_t uxSavedInterruptStatus;                                     \
                                                                            \
    uxSavedInterruptStatus = ENTER_CRITICAL_FROM_ISR();                     \
    {                                                                       \
      if ((pxStreamBuffer)->xTaskWaitingToReceive != NULL) {                \
        (void)xTaskNotifyIndexedFromISR(                                    \
            (pxStreamBuffer)->xTaskWaitingToReceive,                        \
            (pxStreamBuffer)->uxNotificationIndex, (uint32_t)0, eNoAction,  \
            (pxHigherPriorityTaskWoken));                                   \
        (pxStreamBuffer)->xTaskWaitingToReceive = NULL;                     \
      }                                                                     \
    }                                                                       \
    EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);                         \
  } while (0)
#endif  

#if (configUSE_SB_COMPLETED_CALLBACK == 1)
#define SEND_COMPLETE_FROM_ISR(pxStreamBuffer, pxHigherPriorityTaskWoken)      \
  do {                                                                         \
    if ((pxStreamBuffer)->pxSendCompletedCallback != NULL) {                   \
      (pxStreamBuffer)                                                         \
          ->pxSendCompletedCallback((pxStreamBuffer), true,                    \
                                    (pxHigherPriorityTaskWoken));              \
    } else {                                                                   \
      sbSEND_COMPLETE_FROM_ISR((pxStreamBuffer), (pxHigherPriorityTaskWoken)); \
    }                                                                          \
  } while (0)
#else  
#define SEND_COMPLETE_FROM_ISR(pxStreamBuffer, pxHigherPriorityTaskWoken) \
  sbSEND_COMPLETE_FROM_ISR((pxStreamBuffer), (pxHigherPriorityTaskWoken))
#endif  
 
#define sbBYTES_TO_STORE_MESSAGE_LENGTH \
  (sizeof(configMESSAGE_BUFFER_LENGTH_TYPE))
 
#define sbFLAGS_IS_MESSAGE_BUFFER                                              \
  ((uint8_t)1)  
#define sbFLAGS_IS_STATICALLY_ALLOCATED                                 \
  ((uint8_t)2)  
#define sbFLAGS_IS_BATCHING_BUFFER                                             \
  ((uint8_t)4)  

 
typedef struct StreamBufferDef_t {
  volatile size_t xTail;  
  volatile size_t
      xHead;       
  size_t xLength;  
  size_t xTriggerLevelBytes;  
  volatile TaskHandle_t
      xTaskWaitingToReceive;  
  volatile TaskHandle_t
      xTaskWaitingToSend;  
  uint8_t* pucBuffer;  
  uint8_t ucFlags;
#if (configUSE_SB_COMPLETED_CALLBACK == 1)
  StreamBufferCallbackFunction_t
      pxSendCompletedCallback;  
  StreamBufferCallbackFunction_t
      pxReceiveCompletedCallback;  
#endif
  UBaseType_t uxNotificationIndex;  
} StreamBuffer_t;
 
static size_t BytesInBuffer(const StreamBuffer_t* const pxStreamBuffer);
 
static size_t WriteBytesToBuffer(StreamBuffer_t* const pxStreamBuffer,
                                 const uint8_t* pucData, size_t xCount,
                                 size_t xHead);
 
static size_t ReadMessageFromBuffer(StreamBuffer_t* pxStreamBuffer,
                                    void* pvRxData, size_t xBufferLengthBytes,
                                    size_t xBytesAvailable);
 
static size_t WriteMessageToBuffer(StreamBuffer_t* const pxStreamBuffer,
                                   const void* pvTxData,
                                   size_t xDataLengthBytes, size_t xSpace,
                                   size_t xRequiredSpace);
 
static size_t ReadBytesFromBuffer(StreamBuffer_t* pxStreamBuffer,
                                  uint8_t* pucData, size_t xCount,
                                  size_t xTail);
 
static void InitialiseNewStreamBuffer(
    StreamBuffer_t* const pxStreamBuffer, uint8_t* const pucBuffer,
    size_t xBufferSizeBytes, size_t xTriggerLevelBytes, uint8_t ucFlags,
    StreamBufferCallbackFunction_t pxSendCompletedCallback,
    StreamBufferCallbackFunction_t pxReceiveCompletedCallback);

#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
StreamBufferHandle_t xStreamBufferGenericCreate(
    size_t xBufferSizeBytes, size_t xTriggerLevelBytes,
    BaseType_t xStreamBufferType,
    StreamBufferCallbackFunction_t pxSendCompletedCallback,
    StreamBufferCallbackFunction_t pxReceiveCompletedCallback) {
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

        ((uint8_t*)pvAllocatedMemory) +
            sizeof(StreamBuffer_t),  
        xBufferSizeBytes, xTriggerLevelBytes, ucFlags, pxSendCompletedCallback,
        pxReceiveCompletedCallback);
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
    StreamBufferCallbackFunction_t pxSendCompletedCallback,
    StreamBufferCallbackFunction_t pxReceiveCompletedCallback) {
  StreamBuffer_t* const pxStreamBuffer = (StreamBuffer_t*)pStaticStreamBuffer;
  StreamBufferHandle_t xReturn;
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
    InitialiseNewStreamBuffer(pxStreamBuffer, pucStreamBufferStorageArea,
                              xBufferSizeBytes, xTriggerLevelBytes, ucFlags,
                              pxSendCompletedCallback,
                              pxReceiveCompletedCallback);
     
    pxStreamBuffer->ucFlags |= sbFLAGS_IS_STATICALLY_ALLOCATED;

    xReturn = (StreamBufferHandle_t)pStaticStreamBuffer;
  } else {
    xReturn = NULL;
  }

  return xReturn;
}
#endif  

#if (configSUPPORT_STATIC_ALLOCATION == 1)
BaseType_t xStreamBufferGetStaticBuffers(
    StreamBufferHandle_t xStreamBuffer, uint8_t** ppucStreamBufferStorageArea,
    StaticStreamBuffer_t** ppStaticStreamBuffer) {
  BaseType_t xReturn;
  StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;

  configASSERT(pxStreamBuffer);
  configASSERT(ppucStreamBufferStorageArea);
  configASSERT(ppStaticStreamBuffer);
  if ((pxStreamBuffer->ucFlags & sbFLAGS_IS_STATICALLY_ALLOCATED) !=
      (uint8_t)0) {
    *ppucStreamBufferStorageArea = pxStreamBuffer->pucBuffer;

    *ppStaticStreamBuffer = (StaticStreamBuffer_t*)pxStreamBuffer;
    xReturn = true;
  } else {
    xReturn = false;
  }

  return xReturn;
}
#endif  

void vStreamBufferDelete(StreamBufferHandle_t xStreamBuffer) {
  StreamBuffer_t* pxStreamBuffer = xStreamBuffer;
  configASSERT(pxStreamBuffer);
  if ((pxStreamBuffer->ucFlags & sbFLAGS_IS_STATICALLY_ALLOCATED) ==
      (uint8_t) false) {
#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
    {
       
      vPortFree((void*)pxStreamBuffer);
    }
#else
    {
       
      configASSERT(xStreamBuffer == (StreamBufferHandle_t)~0);
    }
#endif
  } else {
     
    (void)memset(pxStreamBuffer, 0x00, sizeof(StreamBuffer_t));
  }
}

BaseType_t xStreamBufferReset(StreamBufferHandle_t xStreamBuffer) {
  StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  BaseType_t xReturn = false;
  StreamBufferCallbackFunction_t pxSendCallback = NULL,
                                 pxReceiveCallback = NULL;
  configASSERT(pxStreamBuffer);
   
  ENTER_CRITICAL();
  {
    if ((pxStreamBuffer->xTaskWaitingToReceive == NULL) &&
        (pxStreamBuffer->xTaskWaitingToSend == NULL)) {
#if (configUSE_SB_COMPLETED_CALLBACK == 1)
      {
        pxSendCallback = pxStreamBuffer->pxSendCompletedCallback;
        pxReceiveCallback = pxStreamBuffer->pxReceiveCompletedCallback;
      }
#endif
      InitialiseNewStreamBuffer(
          pxStreamBuffer, pxStreamBuffer->pucBuffer, pxStreamBuffer->xLength,
          pxStreamBuffer->xTriggerLevelBytes, pxStreamBuffer->ucFlags,
          pxSendCallback, pxReceiveCallback);
      xReturn = true;
    }
  }
  EXIT_CRITICAL();
  return xReturn;
}

BaseType_t xStreamBufferResetFromISR(StreamBufferHandle_t xStreamBuffer) {
  StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  BaseType_t xReturn = false;
  StreamBufferCallbackFunction_t pxSendCallback = NULL,
                                 pxReceiveCallback = NULL;
  UBaseType_t uxSavedInterruptStatus;
  configASSERT(pxStreamBuffer);
   
  uxSavedInterruptStatus = ENTER_CRITICAL_FROM_ISR();
  {
    if ((pxStreamBuffer->xTaskWaitingToReceive == NULL) &&
        (pxStreamBuffer->xTaskWaitingToSend == NULL)) {
#if (configUSE_SB_COMPLETED_CALLBACK == 1)
      {
        pxSendCallback = pxStreamBuffer->pxSendCompletedCallback;
        pxReceiveCallback = pxStreamBuffer->pxReceiveCompletedCallback;
      }
#endif
      InitialiseNewStreamBuffer(
          pxStreamBuffer, pxStreamBuffer->pucBuffer, pxStreamBuffer->xLength,
          pxStreamBuffer->xTriggerLevelBytes, pxStreamBuffer->ucFlags,
          pxSendCallback, pxReceiveCallback);
      xReturn = true;
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return xReturn;
}

BaseType_t xStreamBufferSetTriggerLevel(StreamBufferHandle_t xStreamBuffer,
                                        size_t xTriggerLevel) {
  StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  BaseType_t xReturn;
  configASSERT(pxStreamBuffer);
   
  if (xTriggerLevel == (size_t)0) {
    xTriggerLevel = (size_t)1;
  }
   
  if (xTriggerLevel < pxStreamBuffer->xLength) {
    pxStreamBuffer->xTriggerLevelBytes = xTriggerLevel;
    xReturn = true;
  } else {
    xReturn = false;
  }
  return xReturn;
}

size_t xStreamBufferSpacesAvailable(StreamBufferHandle_t xStreamBuffer) {
  const StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  size_t xSpace;
  size_t xOriginalTail;
  configASSERT(pxStreamBuffer);
   
  do {
    xOriginalTail = pxStreamBuffer->xTail;
    xSpace = pxStreamBuffer->xLength + pxStreamBuffer->xTail;
    xSpace -= pxStreamBuffer->xHead;
  } while (xOriginalTail != pxStreamBuffer->xTail);
  xSpace -= (size_t)1;
  if (xSpace >= pxStreamBuffer->xLength) {
    xSpace -= pxStreamBuffer->xLength;
  }
  return xSpace;
}

size_t xStreamBufferBytesAvailable(StreamBufferHandle_t xStreamBuffer) {
  const StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  size_t xReturn;
  configASSERT(pxStreamBuffer);
  xReturn = BytesInBuffer(pxStreamBuffer);
  return xReturn;
}

size_t xStreamBufferSend(StreamBufferHandle_t xStreamBuffer,
                         const void* pvTxData, size_t xDataLengthBytes,
                         TickType_t xTicksToWait) {
  StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  size_t xReturn, xSpace = 0;
  size_t xRequiredSpace = xDataLengthBytes;
  TimeOut_t xTimeOut;
  size_t xMaxReportedSpace = 0;
  configASSERT(pvTxData);
  configASSERT(pxStreamBuffer);
   
  xMaxReportedSpace = pxStreamBuffer->xLength - (size_t)1;
   
  if ((pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
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
    vTaskSetTimeOutState(&xTimeOut);
    do {
       
      ENTER_CRITICAL();
      {
        xSpace = xStreamBufferSpacesAvailable(pxStreamBuffer);
        if (xSpace < xRequiredSpace) {
           
          (void)xTaskNotifyStateClearIndexed(
              NULL, pxStreamBuffer->uxNotificationIndex);
           
          configASSERT(pxStreamBuffer->xTaskWaitingToSend == NULL);
          pxStreamBuffer->xTaskWaitingToSend = xTaskGetCurrentTaskHandle();
        } else {
          EXIT_CRITICAL();
          break;
        }
      }
      EXIT_CRITICAL();
      (void)xTaskNotifyWaitIndexed(pxStreamBuffer->uxNotificationIndex,
                                   (uint32_t)0, (uint32_t)0, NULL,
                                   xTicksToWait);
      pxStreamBuffer->xTaskWaitingToSend = NULL;
    } while (xTaskCheckForTimeOut(&xTimeOut, &xTicksToWait) == false);
  }
  if (xSpace == (size_t)0) {
    xSpace = xStreamBufferSpacesAvailable(pxStreamBuffer);
  }
  xReturn = WriteMessageToBuffer(pxStreamBuffer, pvTxData, xDataLengthBytes,
                                 xSpace, xRequiredSpace);
  if (xReturn > (size_t)0) {
     
    if (BytesInBuffer(pxStreamBuffer) >= pxStreamBuffer->xTriggerLevelBytes) {
      SEND_COMPLETED(pxStreamBuffer);
    }
  }
  return xReturn;
}

size_t xStreamBufferSendFromISR(StreamBufferHandle_t xStreamBuffer,
                                const void* pvTxData, size_t xDataLengthBytes,
                                BaseType_t* const pxHigherPriorityTaskWoken) {
  StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  size_t xReturn, xSpace;
  size_t xRequiredSpace = xDataLengthBytes;
  configASSERT(pvTxData);
  configASSERT(pxStreamBuffer);
   
  if ((pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
    xRequiredSpace += sbBYTES_TO_STORE_MESSAGE_LENGTH;
  }
  xSpace = xStreamBufferSpacesAvailable(pxStreamBuffer);
  xReturn = WriteMessageToBuffer(pxStreamBuffer, pvTxData, xDataLengthBytes,
                                 xSpace, xRequiredSpace);
  if (xReturn > (size_t)0) {
     
    if (BytesInBuffer(pxStreamBuffer) >= pxStreamBuffer->xTriggerLevelBytes) {
      SEND_COMPLETE_FROM_ISR(pxStreamBuffer, pxHigherPriorityTaskWoken);
    }
  }
  return xReturn;
}

static size_t WriteMessageToBuffer(StreamBuffer_t* const pxStreamBuffer,
                                   const void* pvTxData,
                                   size_t xDataLengthBytes, size_t xSpace,
                                   size_t xRequiredSpace) {
  size_t xNextHead = pxStreamBuffer->xHead;
  configMESSAGE_BUFFER_LENGTH_TYPE xMessageLength;
  if ((pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
     
     
    xMessageLength = (configMESSAGE_BUFFER_LENGTH_TYPE)xDataLengthBytes;
     
    configASSERT((size_t)xMessageLength == xDataLengthBytes);
    if (xSpace >= xRequiredSpace) {
       
      xNextHead =
          WriteBytesToBuffer(pxStreamBuffer, (const uint8_t*)&(xMessageLength),
                             sbBYTES_TO_STORE_MESSAGE_LENGTH, xNextHead);
    } else {
       
      xDataLengthBytes = 0;
    }
  } else {
     
    xDataLengthBytes = configMIN(xDataLengthBytes, xSpace);
  }
  if (xDataLengthBytes != (size_t)0) {
     

    pxStreamBuffer->xHead = WriteBytesToBuffer(
        pxStreamBuffer, (const uint8_t*)pvTxData, xDataLengthBytes, xNextHead);
  }
  return xDataLengthBytes;
}

size_t xStreamBufferReceive(StreamBufferHandle_t xStreamBuffer, void* pvRxData,
                            size_t xBufferLengthBytes,
                            TickType_t xTicksToWait) {
  StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  size_t xReceivedLength = 0, xBytesAvailable, xBytesToStoreMessageLength;
  configASSERT(pvRxData);
  configASSERT(pxStreamBuffer);
   
  if ((pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
    xBytesToStoreMessageLength = sbBYTES_TO_STORE_MESSAGE_LENGTH;
  } else if ((pxStreamBuffer->ucFlags & sbFLAGS_IS_BATCHING_BUFFER) !=
             (uint8_t)0) {
     
    xBytesToStoreMessageLength = pxStreamBuffer->xTriggerLevelBytes;
  } else {
    xBytesToStoreMessageLength = 0;
  }
  if (xTicksToWait != (TickType_t)0) {
     
    ENTER_CRITICAL();
    {
      xBytesAvailable = BytesInBuffer(pxStreamBuffer);
       
      if (xBytesAvailable <= xBytesToStoreMessageLength) {
         
        (void)xTaskNotifyStateClearIndexed(NULL,
                                           pxStreamBuffer->uxNotificationIndex);
         
        configASSERT(pxStreamBuffer->xTaskWaitingToReceive == NULL);
        pxStreamBuffer->xTaskWaitingToReceive = xTaskGetCurrentTaskHandle();
      }
    }
    EXIT_CRITICAL();
    if (xBytesAvailable <= xBytesToStoreMessageLength) {
       
      (void)xTaskNotifyWaitIndexed(pxStreamBuffer->uxNotificationIndex,
                                   (uint32_t)0, (uint32_t)0, NULL,
                                   xTicksToWait);
      pxStreamBuffer->xTaskWaitingToReceive = NULL;
       
      xBytesAvailable = BytesInBuffer(pxStreamBuffer);
    }
  } else {
    xBytesAvailable = BytesInBuffer(pxStreamBuffer);
  }
   
  if (xBytesAvailable > xBytesToStoreMessageLength) {
    xReceivedLength = ReadMessageFromBuffer(
        pxStreamBuffer, pvRxData, xBufferLengthBytes, xBytesAvailable);
     
    if (xReceivedLength != (size_t)0) {
      RECEIVE_COMPLETED(xStreamBuffer);
    }
  }
  return xReceivedLength;
}

size_t xStreamBufferNextMessageLengthBytes(StreamBufferHandle_t xStreamBuffer) {
  StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  size_t xReturn, xBytesAvailable;
  configMESSAGE_BUFFER_LENGTH_TYPE xTempReturn;
  configASSERT(pxStreamBuffer);
   
  if ((pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
    xBytesAvailable = BytesInBuffer(pxStreamBuffer);
    if (xBytesAvailable > sbBYTES_TO_STORE_MESSAGE_LENGTH) {
       
      (void)ReadBytesFromBuffer(pxStreamBuffer, (uint8_t*)&xTempReturn,
                                sbBYTES_TO_STORE_MESSAGE_LENGTH,
                                pxStreamBuffer->xTail);
      xReturn = (size_t)xTempReturn;
    } else {
       
      configASSERT(xBytesAvailable == 0);
      xReturn = 0;
    }
  } else {
    xReturn = 0;
  }
  return xReturn;
}

size_t xStreamBufferReceiveFromISR(
    StreamBufferHandle_t xStreamBuffer, void* pvRxData,
    size_t xBufferLengthBytes, BaseType_t* const pxHigherPriorityTaskWoken) {
  StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  size_t xReceivedLength = 0, xBytesAvailable, xBytesToStoreMessageLength;
  configASSERT(pvRxData);
  configASSERT(pxStreamBuffer);
   
  if ((pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
    xBytesToStoreMessageLength = sbBYTES_TO_STORE_MESSAGE_LENGTH;
  } else {
    xBytesToStoreMessageLength = 0;
  }
  xBytesAvailable = BytesInBuffer(pxStreamBuffer);
   
  if (xBytesAvailable > xBytesToStoreMessageLength) {
    xReceivedLength = ReadMessageFromBuffer(
        pxStreamBuffer, pvRxData, xBufferLengthBytes, xBytesAvailable);
     
    if (xReceivedLength != (size_t)0) {
      RECEIVE_COMPLETED_FROM_ISR(pxStreamBuffer, pxHigherPriorityTaskWoken);
    }
  }
  return xReceivedLength;
}

static size_t ReadMessageFromBuffer(StreamBuffer_t* pxStreamBuffer,
                                    void* pvRxData, size_t xBufferLengthBytes,
                                    size_t xBytesAvailable) {
  size_t xCount, xNextMessageLength;
  configMESSAGE_BUFFER_LENGTH_TYPE xTempNextMessageLength;
  size_t xNextTail = pxStreamBuffer->xTail;
  if ((pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
     
    xNextTail =
        ReadBytesFromBuffer(pxStreamBuffer, (uint8_t*)&xTempNextMessageLength,
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
     

    pxStreamBuffer->xTail = ReadBytesFromBuffer(
        pxStreamBuffer, (uint8_t*)pvRxData, xCount, xNextTail);
  }
  return xCount;
}

BaseType_t xStreamBufferIsEmpty(StreamBufferHandle_t xStreamBuffer) {
  const StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  BaseType_t xReturn;
  size_t xTail;
  configASSERT(pxStreamBuffer);
   
  xTail = pxStreamBuffer->xTail;
  if (pxStreamBuffer->xHead == xTail) {
    xReturn = true;
  } else {
    xReturn = false;
  }
  return xReturn;
}

BaseType_t xStreamBufferIsFull(StreamBufferHandle_t xStreamBuffer) {
  BaseType_t xReturn;
  size_t xBytesToStoreMessageLength;
  const StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  configASSERT(pxStreamBuffer);
   
  if ((pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER) != (uint8_t)0) {
    xBytesToStoreMessageLength = sbBYTES_TO_STORE_MESSAGE_LENGTH;
  } else {
    xBytesToStoreMessageLength = 0;
  }
   
  if (xStreamBufferSpacesAvailable(xStreamBuffer) <=
      xBytesToStoreMessageLength) {
    xReturn = true;
  } else {
    xReturn = false;
  }
  return xReturn;
}

BaseType_t xStreamBufferSendCompletedFromISR(
    StreamBufferHandle_t xStreamBuffer, BaseType_t* pxHigherPriorityTaskWoken) {
  StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  BaseType_t xReturn;
  UBaseType_t uxSavedInterruptStatus;
  configASSERT(pxStreamBuffer);

  uxSavedInterruptStatus = ENTER_CRITICAL_FROM_ISR();
  {
    if ((pxStreamBuffer)->xTaskWaitingToReceive != NULL) {
      (void)xTaskNotifyIndexedFromISR((pxStreamBuffer)->xTaskWaitingToReceive,
                                      (pxStreamBuffer)->uxNotificationIndex,
                                      (uint32_t)0, eNoAction,
                                      pxHigherPriorityTaskWoken);
      (pxStreamBuffer)->xTaskWaitingToReceive = NULL;
      xReturn = true;
    } else {
      xReturn = false;
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return xReturn;
}

BaseType_t xStreamBufferReceiveCompletedFromISR(
    StreamBufferHandle_t xStreamBuffer, BaseType_t* pxHigherPriorityTaskWoken) {
  StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  BaseType_t xReturn;
  UBaseType_t uxSavedInterruptStatus;
  configASSERT(pxStreamBuffer);

  uxSavedInterruptStatus = ENTER_CRITICAL_FROM_ISR();
  {
    if ((pxStreamBuffer)->xTaskWaitingToSend != NULL) {
      (void)xTaskNotifyIndexedFromISR((pxStreamBuffer)->xTaskWaitingToSend,
                                      (pxStreamBuffer)->uxNotificationIndex,
                                      (uint32_t)0, eNoAction,
                                      pxHigherPriorityTaskWoken);
      (pxStreamBuffer)->xTaskWaitingToSend = NULL;
      xReturn = true;
    } else {
      xReturn = false;
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return xReturn;
}

static size_t WriteBytesToBuffer(StreamBuffer_t* const pxStreamBuffer,
                                 const uint8_t* pucData, size_t xCount,
                                 size_t xHead) {
  size_t xFirstLength;
  configASSERT(xCount > (size_t)0);
   
  xFirstLength = configMIN(pxStreamBuffer->xLength - xHead, xCount);
   
  configASSERT((xHead + xFirstLength) <= pxStreamBuffer->xLength);
  (void)memcpy((void*)(&(pxStreamBuffer->pucBuffer[xHead])),
               (const void*)pucData, xFirstLength);
   
  if (xCount > xFirstLength) {
     
    configASSERT((xCount - xFirstLength) <= pxStreamBuffer->xLength);
    (void)memcpy((void*)pxStreamBuffer->pucBuffer,
                 (const void*)&(pucData[xFirstLength]), xCount - xFirstLength);
  }
  xHead += xCount;
  if (xHead >= pxStreamBuffer->xLength) {
    xHead -= pxStreamBuffer->xLength;
  }
  return xHead;
}

static size_t ReadBytesFromBuffer(StreamBuffer_t* pxStreamBuffer,
                                  uint8_t* pucData, size_t xCount,
                                  size_t xTail) {
  size_t xFirstLength;
  configASSERT(xCount != (size_t)0);
   
  xFirstLength = configMIN(pxStreamBuffer->xLength - xTail, xCount);
   
  configASSERT(xFirstLength <= xCount);
  configASSERT((xTail + xFirstLength) <= pxStreamBuffer->xLength);
  (void)memcpy((void*)pucData, (const void*)&(pxStreamBuffer->pucBuffer[xTail]),
               xFirstLength);
   
  if (xCount > xFirstLength) {
     
    (void)memcpy((void*)&(pucData[xFirstLength]),
                 (void*)(pxStreamBuffer->pucBuffer), xCount - xFirstLength);
  }
   
  xTail += xCount;
  if (xTail >= pxStreamBuffer->xLength) {
    xTail -= pxStreamBuffer->xLength;
  }
  return xTail;
}

static size_t BytesInBuffer(const StreamBuffer_t* const pxStreamBuffer) {
   
  size_t xCount;
  xCount = pxStreamBuffer->xLength + pxStreamBuffer->xHead;
  xCount -= pxStreamBuffer->xTail;
  if (xCount >= pxStreamBuffer->xLength) {
    xCount -= pxStreamBuffer->xLength;
  }
  return xCount;
}

static void InitialiseNewStreamBuffer(
    StreamBuffer_t* const pxStreamBuffer, uint8_t* const pucBuffer,
    size_t xBufferSizeBytes, size_t xTriggerLevelBytes, uint8_t ucFlags,
    StreamBufferCallbackFunction_t pxSendCompletedCallback,
    StreamBufferCallbackFunction_t pxReceiveCompletedCallback) {
 
#if (configASSERT_DEFINED == 1)
  {
 
#define STREAM_BUFFER_BUFFER_WRITE_VALUE (0x55)
    configASSERT(memset(pucBuffer, (int)STREAM_BUFFER_BUFFER_WRITE_VALUE,
                        xBufferSizeBytes) == pucBuffer);
  }
#endif
  (void)memset((void*)pxStreamBuffer, 0x00, sizeof(StreamBuffer_t));
  pxStreamBuffer->pucBuffer = pucBuffer;
  pxStreamBuffer->xLength = xBufferSizeBytes;
  pxStreamBuffer->xTriggerLevelBytes = xTriggerLevelBytes;
  pxStreamBuffer->ucFlags = ucFlags;
  pxStreamBuffer->uxNotificationIndex = tskDEFAULT_INDEX_TO_NOTIFY;
#if (configUSE_SB_COMPLETED_CALLBACK == 1)
  {
    pxStreamBuffer->pxSendCompletedCallback = pxSendCompletedCallback;
    pxStreamBuffer->pxReceiveCompletedCallback = pxReceiveCompletedCallback;
  }
#else
  {
    (void)pxSendCompletedCallback;
    (void)pxReceiveCompletedCallback;
  }
#endif  
}

UBaseType_t uxStreamBufferGetStreamBufferNotificationIndex(
    StreamBufferHandle_t xStreamBuffer) {
  StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  configASSERT(pxStreamBuffer);
  return pxStreamBuffer->uxNotificationIndex;
}

void vStreamBufferSetStreamBufferNotificationIndex(
    StreamBufferHandle_t xStreamBuffer, UBaseType_t uxNotificationIndex) {
  StreamBuffer_t* const pxStreamBuffer = xStreamBuffer;
  configASSERT(pxStreamBuffer);
   
  configASSERT(pxStreamBuffer->xTaskWaitingToReceive == NULL);
  configASSERT(pxStreamBuffer->xTaskWaitingToSend == NULL);
   
  configASSERT(uxNotificationIndex < configTASK_NOTIFICATION_ARRAY_ENTRIES);
  pxStreamBuffer->uxNotificationIndex = uxNotificationIndex;
}
