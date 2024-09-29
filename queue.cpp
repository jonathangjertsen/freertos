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

#include "queue.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.hpp"

#define queueUNLOCKED ((int8_t)-1)
#define queueLOCKED_UNMODIFIED ((int8_t)0)
#define queueINT8_MAX ((int8_t)127)
#define uxQueueType pcHead
#define queueQUEUE_IS_MUTEX NULL
struct QueuePointers_t {
  int8_t *pcTail;
  int8_t *pcReadFrom;
};

struct SemaphoreData_t {
  TaskHandle_t MutexHolder;
  UBaseType_t RecursiveCallCount;
};
#define queueSEMAPHORE_QUEUE_ITEM_LENGTH ((UBaseType_t)0)
#define queueMUTEX_GIVE_BLOCK_TIME ((TickType_t)0U)
#define queueYIELD_IF_USING_PREEMPTION() portYIELD_WITHIN_API()

typedef struct QueueDefinition {
  int8_t *pcHead;
  int8_t *pcWriteTo;
  union {
    QueuePointers_t xQueue;
    SemaphoreData_t xSemaphore;
  } u;
  List_t<TCB_t> TasksWaitingToSend;
  List_t<TCB_t> TasksWaitingToReceive;
  volatile UBaseType_t nWaiting;
  UBaseType_t length;
  UBaseType_t uxItemSize;
  volatile int8_t cRxLock;
  volatile int8_t cTxLock;
  uint8_t StaticallyAllocated;
  struct QueueDefinition *set;
} xQUEUE;
typedef xQUEUE Queue_t;
static void UnlockQueue(Queue_t *const Queue);
static BaseType_t IsQueueEmpty(const Queue_t *Queue);
static BaseType_t IsQueueFull(const Queue_t *Queue);
static BaseType_t CopyDataToQueue(Queue_t *const Queue,
                                  const void *pvItemToQueue,
                                  const BaseType_t xPosition);
static void CopyDataFromQueue(Queue_t *const Queue, void *const pvBuffer);
static BaseType_t NotifyQueueSetContainer(const Queue_t *const Queue);
static void InitialiseNewQueue(const UBaseType_t uxQueueLength,
                               const UBaseType_t uxItemSize,
                               uint8_t *pucQueueStorage,
                               const uint8_t ucQueueType, Queue_t *NewQueue);
static void InitialiseMutex(Queue_t *NewQueue);
static UBaseType_t GetHighestPriorityOfWaitToReceiveList(
    Queue_t *const Queue);

#define LockQueue(Queue)                         \
  ENTER_CRITICAL();                                \
  {                                                \
    if ((Queue)->cRxLock == queueUNLOCKED) {     \
      (Queue)->cRxLock = queueLOCKED_UNMODIFIED; \
    }                                              \
    if ((Queue)->cTxLock == queueUNLOCKED) {     \
      (Queue)->cTxLock = queueLOCKED_UNMODIFIED; \
    }                                              \
  }                                                \
  EXIT_CRITICAL()

#define IncrementQueueTxLock(Queue, cTxLock)                  \
  do {                                                          \
    const UBaseType_t uxNumberOfTasks = TaskGetNumberOfTasks(); \
    if ((UBaseType_t)(cTxLock) < uxNumberOfTasks) {             \
      configASSERT((cTxLock) != queueINT8_MAX);                 \
      (Queue)->cTxLock = (int8_t)((cTxLock) + (int8_t)1);     \
    }                                                           \
  } while (0)

#define IncrementQueueRxLock(Queue, cRxLock)                  \
  do {                                                          \
    const UBaseType_t uxNumberOfTasks = TaskGetNumberOfTasks(); \
    if ((UBaseType_t)(cRxLock) < uxNumberOfTasks) {             \
      configASSERT((cRxLock) != queueINT8_MAX);                 \
      (Queue)->cRxLock = (int8_t)((cRxLock) + (int8_t)1);     \
    }                                                           \
  } while (0)

BaseType_t xQueueGenericReset(QueueHandle_t xQueue, BaseType_t xNewQueue) {
  BaseType_t xReturn = true;
  Queue_t *const Queue = xQueue;
  configASSERT(Queue);
  if ((Queue != NULL) && (Queue->length >= 1U) &&

      ((SIZE_MAX / Queue->length) >= Queue->uxItemSize)) {
    ENTER_CRITICAL();
    {
      Queue->u.xQueue.pcTail =
          Queue->pcHead + (Queue->length * Queue->uxItemSize);
      Queue->nWaiting = (UBaseType_t)0U;
      Queue->pcWriteTo = Queue->pcHead;
      Queue->u.xQueue.pcReadFrom =
          Queue->pcHead + ((Queue->length - 1U) * Queue->uxItemSize);
      Queue->cRxLock = queueUNLOCKED;
      Queue->cTxLock = queueUNLOCKED;
      if (xNewQueue == false) {
        if (Queue->TasksWaitingToSend.Length > 0) {
          if (TaskRemoveFromEventList(&(Queue->TasksWaitingToSend)) !=
              false) {
            queueYIELD_IF_USING_PREEMPTION();
          }
        }
      } else {
        Queue->TasksWaitingToSend.init();
        Queue->TasksWaitingToReceive.init();
      }
    }
    EXIT_CRITICAL();
  } else {
    xReturn = false;
  }
  configASSERT(xReturn != false);
  return xReturn;
}

#if (configSUPPORT_STATIC_ALLOCATION == 1)
QueueHandle_t xQueueGenericCreateStatic(const UBaseType_t uxQueueLength,
                                        const UBaseType_t uxItemSize,
                                        uint8_t *pucQueueStorage,
                                        StaticQueue_t *pStaticQueue,
                                        const uint8_t ucQueueType) {
  Queue_t *NewQueue = NULL;

  configASSERT(pStaticQueue);
  if ((uxQueueLength > (UBaseType_t)0) && (pStaticQueue != NULL) &&

      (!((pucQueueStorage != NULL) && (uxItemSize == 0U))) &&
      (!((pucQueueStorage == NULL) && (uxItemSize != 0U)))) {
#if (configASSERT_DEFINED == 1)
    {
      volatile size_t xSize = sizeof(StaticQueue_t);

      configASSERT(xSize == sizeof(Queue_t));
      (void)xSize;
    }
#endif

    NewQueue = (Queue_t *)pStaticQueue;
#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
    { NewQueue->StaticallyAllocated = true; }
#endif
    InitialiseNewQueue(uxQueueLength, uxItemSize, pucQueueStorage, ucQueueType,
                       NewQueue);
  } else {
    configASSERT(NewQueue);
  }
  return NewQueue;
}
#endif

#if (configSUPPORT_STATIC_ALLOCATION == 1)
BaseType_t xQueueGenericGetStaticBuffers(QueueHandle_t xQueue,
                                         uint8_t **ppucQueueStorage,
                                         StaticQueue_t **ppStaticQueue) {
  BaseType_t xReturn;
  Queue_t *const Queue = xQueue;
  configASSERT(Queue);
  configASSERT(ppStaticQueue);
#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
  {
    if (Queue->StaticallyAllocated == (uint8_t) true) {
      if (ppucQueueStorage != NULL) {
        *ppucQueueStorage = (uint8_t *)Queue->pcHead;
      }

      *ppStaticQueue = (StaticQueue_t *)Queue;
      xReturn = true;
    } else {
      xReturn = false;
    }
  }
#else
  {
    if (ppucQueueStorage != NULL) {
      *ppucQueueStorage = (uint8_t *)Queue->pcHead;
    }
    *ppStaticQueue = (StaticQueue_t *)Queue;
    xReturn = true;
  }
#endif
  return xReturn;
}
#endif

#if (configSUPPORT_DYNAMIC_ALLOCATION == 1)
QueueHandle_t xQueueGenericCreate(const UBaseType_t uxQueueLength,
                                  const UBaseType_t uxItemSize,
                                  const uint8_t ucQueueType) {
  Queue_t *NewQueue = NULL;
  size_t xQueueSizeInBytes;
  uint8_t *pucQueueStorage;
  if ((uxQueueLength > (UBaseType_t)0) &&

      ((SIZE_MAX / uxQueueLength) >= uxItemSize) &&

      ((UBaseType_t)(SIZE_MAX - sizeof(Queue_t)) >=
       (uxQueueLength * uxItemSize))) {
    xQueueSizeInBytes = (size_t)((size_t)uxQueueLength * (size_t)uxItemSize);
    NewQueue = (Queue_t *)pvPortMalloc(sizeof(Queue_t) + xQueueSizeInBytes);
    if (NewQueue != NULL) {
      pucQueueStorage = (uint8_t *)NewQueue;
      pucQueueStorage += sizeof(Queue_t);
#if (configSUPPORT_STATIC_ALLOCATION == 1)
      { NewQueue->StaticallyAllocated = false; }
#endif
      InitialiseNewQueue(uxQueueLength, uxItemSize, pucQueueStorage,
                         ucQueueType, NewQueue);
    }
  } else {
    configASSERT(NewQueue);
  }
  return NewQueue;
}
#endif

static void InitialiseNewQueue(const UBaseType_t uxQueueLength,
                               const UBaseType_t uxItemSize,
                               uint8_t *pucQueueStorage,
                               const uint8_t ucQueueType, Queue_t *NewQueue) {
  (void)ucQueueType;
  if (uxItemSize == (UBaseType_t)0) {
    NewQueue->pcHead = (int8_t *)NewQueue;
  } else {
    NewQueue->pcHead = (int8_t *)pucQueueStorage;
  }

  NewQueue->length = uxQueueLength;
  NewQueue->uxItemSize = uxItemSize;
  (void)xQueueGenericReset(NewQueue, true);
#if (configUSE_QUEUE_SETS == 1)
  { NewQueue->set = NULL; }
#endif
}

#if (configUSE_MUTEXES == 1)
static void InitialiseMutex(Queue_t *NewQueue) {
  if (NewQueue != NULL) {
    NewQueue->u.xSemaphore.MutexHolder = NULL;
    NewQueue->uxQueueType = queueQUEUE_IS_MUTEX;

    NewQueue->u.xSemaphore.RecursiveCallCount = 0;

    (void)xQueueGenericSend(NewQueue, NULL, (TickType_t)0U,
                            queueSEND_TO_BACK);
  }
}
#endif

#if ((configUSE_MUTEXES == 1) && (configSUPPORT_DYNAMIC_ALLOCATION == 1))
QueueHandle_t xQueueCreateMutex(const uint8_t ucQueueType) {
  QueueHandle_t xNewQueue;
  const UBaseType_t uxMutexLength = (UBaseType_t)1,
                    uxMutexSize = (UBaseType_t)0;
  xNewQueue = xQueueGenericCreate(uxMutexLength, uxMutexSize, ucQueueType);
  InitialiseMutex((Queue_t *)xNewQueue);
  return xNewQueue;
}
#endif

#if ((configUSE_MUTEXES == 1) && (configSUPPORT_STATIC_ALLOCATION == 1))
QueueHandle_t xQueueCreateMuteStatic(const uint8_t ucQueueType,
                                     StaticQueue_t *pStaticQueue) {
  QueueHandle_t xNewQueue;
  const UBaseType_t uxMutexLength = (UBaseType_t)1,
                    uxMutexSize = (UBaseType_t)0;

  (void)ucQueueType;
  xNewQueue = xQueueGenericCreateStatic(uxMutexLength, uxMutexSize, NULL,
                                        pStaticQueue, ucQueueType);
  InitialiseMutex((Queue_t *)xNewQueue);
  return xNewQueue;
}
#endif

#if ((configUSE_MUTEXES == 1) && (INCLUDE_xSemaphoreGetMutexHolder == 1))
TaskHandle_t xQueueGetMutexHolder(QueueHandle_t xSemaphore) {
  TaskHandle_t Return;
  Queue_t *const Semaphore = (Queue_t *)xSemaphore;
  configASSERT(xSemaphore);

  ENTER_CRITICAL();
  {
    if (Semaphore->uxQueueType == queueQUEUE_IS_MUTEX) {
      Return = Semaphore->u.xSemaphore.MutexHolder;
    } else {
      Return = NULL;
    }
  }
  EXIT_CRITICAL();
  return Return;
}
#endif

#if ((configUSE_MUTEXES == 1) && (INCLUDE_xSemaphoreGetMutexHolder == 1))
TaskHandle_t xQueueGetMutexHolderFromISR(QueueHandle_t xSemaphore) {
  TaskHandle_t Return;
  configASSERT(xSemaphore);

  if (((Queue_t *)xSemaphore)->uxQueueType == queueQUEUE_IS_MUTEX) {
    Return = ((Queue_t *)xSemaphore)->u.xSemaphore.MutexHolder;
  } else {
    Return = NULL;
  }
  return Return;
}
#endif

#if (configUSE_RECURSIVE_MUTEXES == 1)
BaseType_t xQueueGiveMutexRecursive(QueueHandle_t xMutex) {
  BaseType_t xReturn;
  Queue_t *const Mutex = (Queue_t *)xMutex;
  configASSERT(Mutex);

  if (Mutex->u.xSemaphore.MutexHolder == TaskGetCurrentTaskHandle()) {
    (Mutex->u.xSemaphore.RecursiveCallCount)--;

    if (Mutex->u.xSemaphore.RecursiveCallCount == (UBaseType_t)0) {
      (void)xQueueGenericSend(Mutex, NULL, queueMUTEX_GIVE_BLOCK_TIME,
                              queueSEND_TO_BACK);
    }
    xReturn = true;
  } else {
    xReturn = false;
  }
  return xReturn;
}
#endif

#if (configUSE_RECURSIVE_MUTEXES == 1)
BaseType_t xQueueTakeMutexRecursive(QueueHandle_t xMutex,
                                    TickType_t xTicksToWait) {
  BaseType_t xReturn;
  Queue_t *const Mutex = (Queue_t *)xMutex;
  configASSERT(Mutex);
  if (Mutex->u.xSemaphore.MutexHolder == TaskGetCurrentTaskHandle()) {
    (Mutex->u.xSemaphore.RecursiveCallCount)++;
    xReturn = true;
  } else {
    xReturn = xQueueSemaphoreTake(Mutex, xTicksToWait);

    if (xReturn != false) {
      (Mutex->u.xSemaphore.RecursiveCallCount)++;
    }
  }
  return xReturn;
}
#endif

#if ((configUSE_COUNTING_SEMAPHORES == 1) && \
     (configSUPPORT_STATIC_ALLOCATION == 1))
QueueHandle_t xQueueCreateCountingSemaphoreStatic(
    const UBaseType_t uxMaxCount, const UBaseType_t uxInitialCount,
    StaticQueue_t *pStaticQueue) {
  QueueHandle_t xHandle = NULL;
  if ((uxMaxCount != 0U) && (uxInitialCount <= uxMaxCount)) {
    xHandle = xQueueGenericCreateStatic(
        uxMaxCount, queueSEMAPHORE_QUEUE_ITEM_LENGTH, NULL, pStaticQueue,
        queueQUEUE_TYPE_COUNTING_SEMAPHORE);
    if (xHandle != NULL) {
      ((Queue_t *)xHandle)->nWaiting = uxInitialCount;
    }
  } else {
    configASSERT(xHandle);
  }
  return xHandle;
}
#endif

#if ((configUSE_COUNTING_SEMAPHORES == 1) && \
     (configSUPPORT_DYNAMIC_ALLOCATION == 1))
QueueHandle_t xQueueCreateCountingSemaphore(const UBaseType_t uxMaxCount,
                                            const UBaseType_t uxInitialCount) {
  QueueHandle_t xHandle = NULL;
  if ((uxMaxCount != 0U) && (uxInitialCount <= uxMaxCount)) {
    xHandle = xQueueGenericCreate(uxMaxCount, queueSEMAPHORE_QUEUE_ITEM_LENGTH,
                                  queueQUEUE_TYPE_COUNTING_SEMAPHORE);
    if (xHandle != NULL) {
      ((Queue_t *)xHandle)->nWaiting = uxInitialCount;
    }
  } else {
    configASSERT(xHandle);
  }
  return xHandle;
}
#endif

BaseType_t xQueueGenericSend(QueueHandle_t xQueue,
                             const void *const pvItemToQueue,
                             TickType_t xTicksToWait,
                             const BaseType_t xCopyPosition) {
  BaseType_t xEntryTimeSet = false, xYieldRequired;
  TimeOut_t xTimeOut;
  Queue_t *const Queue = xQueue;
  configASSERT(Queue);
  configASSERT(
      !((pvItemToQueue == NULL) && (Queue->uxItemSize != (UBaseType_t)0U)));
  configASSERT(!((xCopyPosition == queueOVERWRITE) && (Queue->length != 1)));
#if ((INCLUDE_TaskGetSchedulerState == 1) || (configUSE_TIMERS == 1))
  {
    configASSERT(!((TaskGetSchedulerState() == taskSCHEDULER_SUSPENDED) &&
                   (xTicksToWait != 0)));
  }
#endif
  for (;;) {
    ENTER_CRITICAL();
    {
      if ((Queue->nWaiting < Queue->length) ||
          (xCopyPosition == queueOVERWRITE)) {
        const UBaseType_t uxPreviousMessagesWaiting = Queue->nWaiting;
        xYieldRequired = CopyDataToQueue(Queue, pvItemToQueue, xCopyPosition);
        if (Queue->set != NULL) {
          if ((xCopyPosition == queueOVERWRITE) &&
              (uxPreviousMessagesWaiting != (UBaseType_t)0)) {
          } else if (NotifyQueueSetContainer(Queue) != false) {
            queueYIELD_IF_USING_PREEMPTION();
          }
        } else {
          if (Queue->TasksWaitingToReceive.Length > 0) {
            if (TaskRemoveFromEventList(&(Queue->TasksWaitingToReceive)) !=
                false) {
              queueYIELD_IF_USING_PREEMPTION();
            }
          } else if (xYieldRequired != false) {
            queueYIELD_IF_USING_PREEMPTION();
          }
        }
        EXIT_CRITICAL();
        return true;
      } else {
        if (xTicksToWait == (TickType_t)0) {
          EXIT_CRITICAL();
          return errQUEUE_FULL;
        } else if (xEntryTimeSet == false) {
          TaskInternalSetTimeOutState(&xTimeOut);
          xEntryTimeSet = true;
        }
      }
    }
    EXIT_CRITICAL();

    TaskSuspendAll();
    LockQueue(Queue);

    if (TaskCheckForTimeOut(&xTimeOut, &xTicksToWait) == false) {
      if (IsQueueFull(Queue) != false) {
        TaskPlaceOnEventList(&(Queue->TasksWaitingToSend), xTicksToWait);

        UnlockQueue(Queue);

        if (TaskResumeAll() == false) {
          taskYIELD_WITHIN_API();
        }
      } else {
        UnlockQueue(Queue);
        (void)TaskResumeAll();
      }
    } else {
      UnlockQueue(Queue);
      (void)TaskResumeAll();
      return errQUEUE_FULL;
    }
  }
}

BaseType_t xQueueGenericSendFromISR(QueueHandle_t xQueue,
                                    const void *const pvItemToQueue,
                                    BaseType_t *const HigherPriorityTaskWoken,
                                    const BaseType_t xCopyPosition) {
  BaseType_t xReturn;
  UBaseType_t uxSavedInterruptStatus;
  Queue_t *const Queue = xQueue;
  configASSERT(Queue);
  configASSERT(
      !((pvItemToQueue == NULL) && (Queue->uxItemSize != (UBaseType_t)0U)));
  configASSERT(!((xCopyPosition == queueOVERWRITE) && (Queue->length != 1)));

  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();

  uxSavedInterruptStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
    if ((Queue->nWaiting < Queue->length) ||
        (xCopyPosition == queueOVERWRITE)) {
      const int8_t cTxLock = Queue->cTxLock;
      const UBaseType_t uxPreviousMessagesWaiting = Queue->nWaiting;

      (void)CopyDataToQueue(Queue, pvItemToQueue, xCopyPosition);

      if (cTxLock == queueUNLOCKED) {
#if (configUSE_QUEUE_SETS == 1)
        {
          if (Queue->set != NULL) {
            if ((xCopyPosition == queueOVERWRITE) &&
                (uxPreviousMessagesWaiting != (UBaseType_t)0)) {
            } else if (NotifyQueueSetContainer(Queue) != false) {
              if (HigherPriorityTaskWoken != NULL) {
                *HigherPriorityTaskWoken = true;
              }
            }
          } else {
            if (Queue->TasksWaitingToReceive.Length > 0) {
              if (TaskRemoveFromEventList(&(Queue->TasksWaitingToReceive)) !=
                  false) {
                if (HigherPriorityTaskWoken != NULL) {
                  *HigherPriorityTaskWoken = true;
                }
              }
            }
          }
        }
#else
        {
          if (LIST_IS_EMPTY(&(Queue->TasksWaitingToReceive)) == false) {
            if (TaskRemoveFromEventList(&(Queue->TasksWaitingToReceive)) !=
                false) {
              if (HigherPriorityTaskWoken != NULL) {
                *HigherPriorityTaskWoken = true;
              }
            }
          }

          (void)uxPreviousMessagesWaiting;
        }
#endif
      } else {
        IncrementQueueTxLock(Queue, cTxLock);
      }
      xReturn = true;
    } else {
      xReturn = errQUEUE_FULL;
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return xReturn;
}

BaseType_t xQueueGiveFromISR(QueueHandle_t xQueue,
                             BaseType_t *const HigherPriorityTaskWoken) {
  BaseType_t xReturn;
  UBaseType_t uxSavedInterruptStatus;
  Queue_t *const Queue = xQueue;

  configASSERT(Queue);

  configASSERT(Queue->uxItemSize == 0);

  configASSERT(!((Queue->uxQueueType == queueQUEUE_IS_MUTEX) &&
                 (Queue->u.xSemaphore.MutexHolder != NULL)));

  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  uxSavedInterruptStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
    const UBaseType_t nWaiting = Queue->nWaiting;

    if (nWaiting < Queue->length) {
      const int8_t cTxLock = Queue->cTxLock;

      Queue->nWaiting = (UBaseType_t)(nWaiting + (UBaseType_t)1);

      if (cTxLock == queueUNLOCKED) {
#if (configUSE_QUEUE_SETS == 1)
        {
          if (Queue->set != NULL) {
            if (NotifyQueueSetContainer(Queue) != false) {
              if (HigherPriorityTaskWoken != NULL) {
                *HigherPriorityTaskWoken = true;
              }
            }
          } else {
            if (Queue->TasksWaitingToReceive.Length > 0) {
              if (TaskRemoveFromEventList(&(Queue->TasksWaitingToReceive)) !=
                  false) {
                if (HigherPriorityTaskWoken != NULL) {
                  *HigherPriorityTaskWoken = true;
                }
              }
            }
          }
        }
#else
        {
          if (LIST_IS_EMPTY(&(Queue->TasksWaitingToReceive)) == false) {
            if (TaskRemoveFromEventList(&(Queue->TasksWaitingToReceive)) !=
                false) {
              if (HigherPriorityTaskWoken != NULL) {
                *HigherPriorityTaskWoken = true;
              }
            }
          }
        }
#endif
      } else {
        IncrementQueueTxLock(Queue, cTxLock);
      }
      xReturn = true;
    } else {
      xReturn = errQUEUE_FULL;
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return xReturn;
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *const pvBuffer,
                         TickType_t xTicksToWait) {
  BaseType_t xEntryTimeSet = false;
  TimeOut_t xTimeOut;
  Queue_t *const Queue = xQueue;

  configASSERT((Queue));

  configASSERT(
      !(((pvBuffer) == NULL) && ((Queue)->uxItemSize != (UBaseType_t)0U)));

#if ((INCLUDE_TaskGetSchedulerState == 1) || (configUSE_TIMERS == 1))
  {
    configASSERT(!((TaskGetSchedulerState() == taskSCHEDULER_SUSPENDED) &&
                   (xTicksToWait != 0)));
  }
#endif
  for (;;) {
    ENTER_CRITICAL();
    {
      const UBaseType_t nWaiting = Queue->nWaiting;

      if (nWaiting > (UBaseType_t)0) {
        CopyDataFromQueue(Queue, pvBuffer);
        Queue->nWaiting = (UBaseType_t)(nWaiting - (UBaseType_t)1);
        if (Queue->TasksWaitingToSend.Length > 0) {
          if (TaskRemoveFromEventList(&(Queue->TasksWaitingToSend)) !=
              false) {
            queueYIELD_IF_USING_PREEMPTION();
          }
        }
        EXIT_CRITICAL();
        return true;
      } else {
        if (xTicksToWait == (TickType_t)0) {
          EXIT_CRITICAL();
          return errQUEUE_EMPTY;
        } else if (xEntryTimeSet == false) {
          TaskInternalSetTimeOutState(&xTimeOut);
          xEntryTimeSet = true;
        } else {
          mtCOVERAGE_TEST_MARKER();
        }
      }
    }
    EXIT_CRITICAL();

    TaskSuspendAll();
    LockQueue(Queue);

    if (TaskCheckForTimeOut(&xTimeOut, &xTicksToWait) == false) {
      if (IsQueueEmpty(Queue) != false) {
        TaskPlaceOnEventList(&(Queue->TasksWaitingToReceive), xTicksToWait);
        UnlockQueue(Queue);
        if (TaskResumeAll() == false) {
          taskYIELD_WITHIN_API();
        }
      } else {
        UnlockQueue(Queue);
        (void)TaskResumeAll();
      }
    } else {
      UnlockQueue(Queue);
      (void)TaskResumeAll();
      if (IsQueueEmpty(Queue) != false) {
        return errQUEUE_EMPTY;
      }
    }
  }
}

BaseType_t xQueueSemaphoreTake(QueueHandle_t xQueue, TickType_t xTicksToWait) {
  BaseType_t xEntryTimeSet = false;
  TimeOut_t xTimeOut;
  Queue_t *const Queue = xQueue;
#if (configUSE_MUTEXES == 1)
  BaseType_t xInheritanceOccurred = false;
#endif

  configASSERT((Queue));

  configASSERT(Queue->uxItemSize == 0);

#if ((INCLUDE_TaskGetSchedulerState == 1) || (configUSE_TIMERS == 1))
  {
    configASSERT(!((TaskGetSchedulerState() == taskSCHEDULER_SUSPENDED) &&
                   (xTicksToWait != 0)));
  }
#endif
  for (;;) {
    ENTER_CRITICAL();
    {
      const UBaseType_t uxSemaphoreCount = Queue->nWaiting;

      if (uxSemaphoreCount > (UBaseType_t)0) {
        Queue->nWaiting = (UBaseType_t)(uxSemaphoreCount - (UBaseType_t)1);
#if (configUSE_MUTEXES == 1)
        {
          if (Queue->uxQueueType == queueQUEUE_IS_MUTEX) {
            Queue->u.xSemaphore.MutexHolder = TaskIncrementMutexHeldCount();
          }
        }
#endif

        if (Queue->TasksWaitingToSend.Length > 0) {
          if (TaskRemoveFromEventList(&(Queue->TasksWaitingToSend)) !=
              false) {
            queueYIELD_IF_USING_PREEMPTION();
          }
        }
        EXIT_CRITICAL();
        return true;
      } else {
        if (xTicksToWait == (TickType_t)0) {
          EXIT_CRITICAL();
          return errQUEUE_EMPTY;
        } else if (xEntryTimeSet == false) {
          TaskInternalSetTimeOutState(&xTimeOut);
          xEntryTimeSet = true;
        }
      }
    }
    EXIT_CRITICAL();

    TaskSuspendAll();
    LockQueue(Queue);

    if (TaskCheckForTimeOut(&xTimeOut, &xTicksToWait) == false) {
      if (IsQueueEmpty(Queue) != false) {
#if (configUSE_MUTEXES == 1)
        {
          if (Queue->uxQueueType == queueQUEUE_IS_MUTEX) {
            ENTER_CRITICAL();
            {
              xInheritanceOccurred =
                  TaskPriorityInherit(Queue->u.xSemaphore.MutexHolder);
            }
            EXIT_CRITICAL();
          }
        }
#endif
        TaskPlaceOnEventList(&(Queue->TasksWaitingToReceive), xTicksToWait);
        UnlockQueue(Queue);
        if (TaskResumeAll() == false) {
          taskYIELD_WITHIN_API();
        }
      } else {
        UnlockQueue(Queue);
        (void)TaskResumeAll();
      }
    } else {
      UnlockQueue(Queue);
      (void)TaskResumeAll();

      if (IsQueueEmpty(Queue) != false) {
#if (configUSE_MUTEXES == 1)
        {
          if (xInheritanceOccurred != false) {
            ENTER_CRITICAL();
            {
              UBaseType_t uxHighestWaitingPriority;

              uxHighestWaitingPriority =
                  GetHighestPriorityOfWaitToReceiveList(Queue);
              TaskPriorityDisinheritAfterTimeout(
                  Queue->u.xSemaphore.MutexHolder, uxHighestWaitingPriority);
            }
            EXIT_CRITICAL();
          }
        }
#endif
        return errQUEUE_EMPTY;
      }
    }
  }
}

BaseType_t xQueuePeek(QueueHandle_t xQueue, void *const pvBuffer,
                      TickType_t xTicksToWait) {
  BaseType_t xEntryTimeSet = false;
  TimeOut_t xTimeOut;
  int8_t *pcOriginalReadPosition;
  Queue_t *const Queue = xQueue;

  configASSERT((Queue));

  configASSERT(
      !(((pvBuffer) == NULL) && ((Queue)->uxItemSize != (UBaseType_t)0U)));

#if ((INCLUDE_TaskGetSchedulerState == 1) || (configUSE_TIMERS == 1))
  {
    configASSERT(!((TaskGetSchedulerState() == taskSCHEDULER_SUSPENDED) &&
                   (xTicksToWait != 0)));
  }
#endif
  for (;;) {
    ENTER_CRITICAL();
    {
      const UBaseType_t nWaiting = Queue->nWaiting;

      if (nWaiting > (UBaseType_t)0) {
        pcOriginalReadPosition = Queue->u.xQueue.pcReadFrom;
        CopyDataFromQueue(Queue, pvBuffer);

        Queue->u.xQueue.pcReadFrom = pcOriginalReadPosition;

        if (Queue->TasksWaitingToReceive.Length > 0) {
          if (TaskRemoveFromEventList(&(Queue->TasksWaitingToReceive)) !=
              false) {
            queueYIELD_IF_USING_PREEMPTION();
          }
        }
        EXIT_CRITICAL();
        return true;
      } else {
        if (xTicksToWait == (TickType_t)0) {
          EXIT_CRITICAL();
          return errQUEUE_EMPTY;
        } else if (xEntryTimeSet == false) {
          TaskInternalSetTimeOutState(&xTimeOut);
          xEntryTimeSet = true;
        }
      }
    }
    EXIT_CRITICAL();

    TaskSuspendAll();
    LockQueue(Queue);

    if (TaskCheckForTimeOut(&xTimeOut, &xTicksToWait) == false) {
      if (IsQueueEmpty(Queue) != false) {
        TaskPlaceOnEventList(&(Queue->TasksWaitingToReceive), xTicksToWait);
        UnlockQueue(Queue);
        if (TaskResumeAll() == false) {
          taskYIELD_WITHIN_API();
        }
      } else {
        UnlockQueue(Queue);
        (void)TaskResumeAll();
      }
    } else {
      UnlockQueue(Queue);
      (void)TaskResumeAll();
      if (IsQueueEmpty(Queue) != false) {
        return errQUEUE_EMPTY;
      }
    }
  }
}
BaseType_t xQueueReceiveFromISR(QueueHandle_t xQueue, void *const pvBuffer,
                                BaseType_t *const HigherPriorityTaskWoken) {
  BaseType_t xReturn;
  UBaseType_t uxSavedInterruptStatus;
  Queue_t *const Queue = xQueue;
  configASSERT(Queue);
  configASSERT(
      !((pvBuffer == NULL) && (Queue->uxItemSize != (UBaseType_t)0U)));
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  uxSavedInterruptStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
    const UBaseType_t nWaiting = Queue->nWaiting;

    if (nWaiting > (UBaseType_t)0) {
      const int8_t cRxLock = Queue->cRxLock;
      CopyDataFromQueue(Queue, pvBuffer);
      Queue->nWaiting = (UBaseType_t)(nWaiting - (UBaseType_t)1);

      if (cRxLock == queueUNLOCKED) {
        if (Queue->TasksWaitingToSend.Length > 0) {
          if (TaskRemoveFromEventList(&(Queue->TasksWaitingToSend)) !=
              false) {
            if (HigherPriorityTaskWoken != NULL) {
              *HigherPriorityTaskWoken = true;
            }
          }
        }
      } else {
        IncrementQueueRxLock(Queue, cRxLock);
      }
      xReturn = true;
    } else {
      xReturn = false;
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return xReturn;
}

BaseType_t xQueuePeekFromISR(QueueHandle_t xQueue, void *const pvBuffer) {
  BaseType_t xReturn;
  UBaseType_t uxSavedInterruptStatus;
  int8_t *pcOriginalReadPosition;
  Queue_t *const Queue = xQueue;
  configASSERT(Queue);
  configASSERT(
      !((pvBuffer == NULL) && (Queue->uxItemSize != (UBaseType_t)0U)));
  configASSERT(Queue->uxItemSize != 0);

  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  uxSavedInterruptStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
    if (Queue->nWaiting > (UBaseType_t)0) {
      pcOriginalReadPosition = Queue->u.xQueue.pcReadFrom;
      CopyDataFromQueue(Queue, pvBuffer);
      Queue->u.xQueue.pcReadFrom = pcOriginalReadPosition;
      xReturn = true;
    } else {
      xReturn = false;
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return xReturn;
}
UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t xQueue) {
  UBaseType_t uxReturn;
  configASSERT(xQueue);
  ENTER_CRITICAL();
  { uxReturn = ((Queue_t *)xQueue)->nWaiting; }
  EXIT_CRITICAL();
  return uxReturn;
}
UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t xQueue) {
  UBaseType_t uxReturn;
  Queue_t *const Queue = xQueue;
  configASSERT(Queue);
  ENTER_CRITICAL();
  { uxReturn = (UBaseType_t)(Queue->length - Queue->nWaiting); }
  EXIT_CRITICAL();
  return uxReturn;
}

UBaseType_t uxQueueMessagesWaitingFromISR(const QueueHandle_t xQueue) {
  Queue_t *const Queue = xQueue;
  configASSERT(Queue);
  return Queue->nWaiting;
}

void vQueueDelete(QueueHandle_t xQueue) {
  Queue_t *const Queue = xQueue;
  configASSERT(Queue);
#if (configQUEUE_REGISTRY_SIZE > 0)
  { vQueueUnregisterQueue(Queue); }
#endif
#if ((configSUPPORT_DYNAMIC_ALLOCATION == 1) && \
     (configSUPPORT_STATIC_ALLOCATION == 0))
  { vPortFree(Queue); }
#elif ((configSUPPORT_DYNAMIC_ALLOCATION == 1) && \
       (configSUPPORT_STATIC_ALLOCATION == 1))
  {
    if (Queue->StaticallyAllocated == (uint8_t) false) {
      vPortFree(Queue);
    }
  }
#else
  {
    (void)Queue;
  }
#endif
}
UBaseType_t uxQueueGetQueueItemSize(QueueHandle_t xQueue) {
  return ((Queue_t *)xQueue)->uxItemSize;
}

UBaseType_t uxQueueGetQueueLength(QueueHandle_t xQueue) {
  return ((Queue_t *)xQueue)->length;
}

#if (configUSE_MUTEXES == 1)
static UBaseType_t GetHighestPriorityOfWaitToReceiveList(
    Queue_t *const Queue) {
  UBaseType_t uxHighestPriorityOfWaitingTasks;

  if (Queue->TasksWaitingToReceive.Length > 0U) {
    uxHighestPriorityOfWaitingTasks =
        (UBaseType_t)((UBaseType_t)configMAX_PRIORITIES -
                      (UBaseType_t)(Queue->TasksWaitingToReceive.head()
                                        ->Value));
  } else {
    uxHighestPriorityOfWaitingTasks = tskIDLE_PRIORITY;
  }
  return uxHighestPriorityOfWaitingTasks;
}
#endif

static BaseType_t CopyDataToQueue(Queue_t *const q, const void *pvItemToQueue,
                                  const BaseType_t xPosition) {
  BaseType_t xReturn = false;
  UBaseType_t nWaiting;

  nWaiting = q->nWaiting;
  if (q->uxItemSize == (UBaseType_t)0) {
#if (configUSE_MUTEXES == 1)
    {
      if (q->uxQueueType == queueQUEUE_IS_MUTEX) {
        xReturn = TaskPriorityDisinherit(q->u.xSemaphore.MutexHolder);
        q->u.xSemaphore.MutexHolder = NULL;
      }
    }
#endif
  } else if (xPosition == queueSEND_TO_BACK) {
    (void)memcpy((void *)q->pcWriteTo, pvItemToQueue, (size_t)q->uxItemSize);
    q->pcWriteTo += q->uxItemSize;
    if (q->pcWriteTo >= q->u.xQueue.pcTail) {
      q->pcWriteTo = q->pcHead;
    }
  } else {
    (void)memcpy((void *)q->u.xQueue.pcReadFrom, pvItemToQueue,
                 (size_t)q->uxItemSize);
    q->u.xQueue.pcReadFrom -= q->uxItemSize;
    if (q->u.xQueue.pcReadFrom < q->pcHead) {
      q->u.xQueue.pcReadFrom = (q->u.xQueue.pcTail - q->uxItemSize);
    }
    if (xPosition == queueOVERWRITE) {
      if (nWaiting > (UBaseType_t)0) {
        --nWaiting;
      }
    }
  }
  q->nWaiting = (UBaseType_t)(nWaiting + (UBaseType_t)1);
  return xReturn;
}

static void CopyDataFromQueue(Queue_t *const Queue, void *const pvBuffer) {
  if (Queue->uxItemSize != (UBaseType_t)0) {
    Queue->u.xQueue.pcReadFrom += Queue->uxItemSize;
    if (Queue->u.xQueue.pcReadFrom >= Queue->u.xQueue.pcTail) {
      Queue->u.xQueue.pcReadFrom = Queue->pcHead;
    }
    (void)memcpy((void *)pvBuffer, (void *)Queue->u.xQueue.pcReadFrom,
                 (size_t)Queue->uxItemSize);
  }
}

static void UnlockQueue(Queue_t *const Queue) {
  {
    CriticalSection s;
    int8_t cTxLock = Queue->cTxLock;
    while (cTxLock > queueLOCKED_UNMODIFIED) {
      if (Queue->set != NULL) {
        if (NotifyQueueSetContainer(Queue) != false) {
          TaskMissedYield();
        }
      } else {
        if (Queue->TasksWaitingToReceive.Length > 0) {
          if (TaskRemoveFromEventList(&(Queue->TasksWaitingToReceive)) !=
              false) {
            TaskMissedYield();
          }
        } else {
          break;
        }
      }
      --cTxLock;
    }
    Queue->cTxLock = queueUNLOCKED;
  }

  CriticalSection s;
  int8_t cRxLock = Queue->cRxLock;
  while (cRxLock > queueLOCKED_UNMODIFIED) {
    if (Queue->TasksWaitingToSend.Length > 0) {
      if (TaskRemoveFromEventList(&(Queue->TasksWaitingToSend)) != false) {
        TaskMissedYield();
      }
      --cRxLock;
    } else {
      break;
    }
  }
  Queue->cRxLock = queueUNLOCKED;
}

static BaseType_t IsQueueEmpty(const Queue_t *Queue) {
  CriticalSection s;
  return Queue->nWaiting == 0;
}

BaseType_t xQueueIsQueueEmptyFromISR(const QueueHandle_t xQueue) {
  configASSERT(xQueue);
  return xQueue->nWaiting == 0;
}

static BaseType_t IsQueueFull(const Queue_t *Queue) {
  CriticalSection s;
  return Queue->nWaiting == Queue->length;
}

BaseType_t xQueueIsQueueFullFromISR(const QueueHandle_t xQueue) {
  BaseType_t xReturn;
  Queue_t *const Queue = xQueue;
  configASSERT(Queue);
  return Queue->nWaiting == Queue->length;
}

void vQueueWaitForMessageRestricted(QueueHandle_t xQueue,
                                    TickType_t xTicksToWait,
                                    const BaseType_t xWaitIndefinitely) {
  Queue_t *const Queue = xQueue;

  LockQueue(Queue);
  if (Queue->nWaiting == (UBaseType_t)0U) {
    TaskPlaceOnEventListRestricted(&(Queue->TasksWaitingToReceive),
                                   xTicksToWait, xWaitIndefinitely);
  }
  UnlockQueue(Queue);
}
QueueSetHandle_t xQueueCreateSet(const UBaseType_t uxEventQueueLength) {
  return xQueueGenericCreate(uxEventQueueLength, (UBaseType_t)sizeof(Queue_t *),
                             queueQUEUE_TYPE_SET);
}
BaseType_t xQueueAddToSet(QueueSetMemberHandle_t xQueueOrSemaphore,
                          QueueSetHandle_t xQueueSet) {
  BaseType_t xReturn;
  ENTER_CRITICAL();
  {
    if (((Queue_t *)xQueueOrSemaphore)->set != NULL) {
      xReturn = false;
    } else if (((Queue_t *)xQueueOrSemaphore)->nWaiting != (UBaseType_t)0) {
      xReturn = false;
    } else {
      ((Queue_t *)xQueueOrSemaphore)->set = xQueueSet;
      xReturn = true;
    }
  }
  EXIT_CRITICAL();
  return xReturn;
}
BaseType_t xQueueRemoveFromSet(QueueSetMemberHandle_t xQueueOrSemaphore,
                               QueueSetHandle_t xQueueSet) {
  BaseType_t xReturn;
  Queue_t *const QueueOrSemaphore = (Queue_t *)xQueueOrSemaphore;
  if (QueueOrSemaphore->set != xQueueSet) {
    xReturn = false;
  } else if (QueueOrSemaphore->nWaiting != (UBaseType_t)0) {
    xReturn = false;
  } else {
    ENTER_CRITICAL();
    { QueueOrSemaphore->set = NULL; }
    EXIT_CRITICAL();
    xReturn = true;
  }
  return xReturn;
}

QueueSetMemberHandle_t xQueueSelectFromSet(QueueSetHandle_t xQueueSet,
                                           TickType_t const xTicksToWait) {
  QueueSetMemberHandle_t xReturn = NULL;
  (void)xQueueReceive((QueueHandle_t)xQueueSet, &xReturn, xTicksToWait);
  return xReturn;
}

QueueSetMemberHandle_t xQueueSelectFromSetFromISR(QueueSetHandle_t xQueueSet) {
  QueueSetMemberHandle_t xReturn = NULL;
  (void)xQueueReceiveFromISR((QueueHandle_t)xQueueSet, &xReturn, NULL);
  return xReturn;
}

static BaseType_t NotifyQueueSetContainer(const Queue_t *const Queue) {
  Queue_t *container = Queue->set;
  BaseType_t xReturn = false;

  configASSERT(container);
  configASSERT(container->nWaiting < container->length);
  if (container->nWaiting < container->length) {
    const int8_t cTxLock = container->cTxLock;

    xReturn = CopyDataToQueue(container, &Queue, queueSEND_TO_BACK);
    if (cTxLock == queueUNLOCKED) {
      if (container->TasksWaitingToReceive.Length > 0) {
        if (TaskRemoveFromEventList(&(container->TasksWaitingToReceive)) !=
            false) {
          xReturn = true;
        }
      }
    } else {
      IncrementQueueTxLock(container, cTxLock);
    }
  }
  return xReturn;
}
