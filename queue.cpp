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
#define queueQUEUE_IS_MUTEX NULL
struct QueuePointers_t {
  int8_t *pcTail;
  int8_t *read;
};

struct SemaphoreData_t {
  TaskHandle_t MutHolder;
  UBaseType_t RecursiveCallCount;
};
#define queueSEMAPHORE_QUEUE_ITEM_LENGTH (0)
#define queueMUTEX_GIVE_BLOCK_TIME ((TickType_t)0U)
#define queueYIELD_IF_USING_PREEMPTION() portYIELD_WITHIN_API()
struct Queue_t;
static BaseType_t NotifyQueueSetContainer(const Queue_t *const Queue);

struct Queue_t {
  int8_t *Head;
  int8_t *write;
  union {
    QueuePointers_t q;
    SemaphoreData_t sema;
  } u;
  List_t<TCB_t> PendingTX;
  List_t<TCB_t> PendingRX;
  volatile UBaseType_t nWaiting;
  UBaseType_t length;
  UBaseType_t itemSize;
  volatile int8_t rxLock;
  volatile int8_t txLock;
  uint8_t StaticallyAllocated;
  struct Queue_t *set;

  void Lock() {
    if (rxLock == queueUNLOCKED) {
      rxLock = queueLOCKED_UNMODIFIED;
    }
    if (txLock == queueUNLOCKED) {
      txLock = queueLOCKED_UNMODIFIED;
    }
  }

  void Unlock() {
    {
      CriticalSection s;
      int8_t txLock = txLock;
      while (txLock > queueLOCKED_UNMODIFIED) {
        if (set != NULL) {
          if (NotifyQueueSetContainer(this)) {
            MissedYield();
          }
        } else {
          if (PendingRX.Length > 0) {
            if (RemoveFromEventList(&(PendingRX))) {
              MissedYield();
            }
          } else {
            break;
          }
        }
        --txLock;
      }
      txLock = queueUNLOCKED;
    }

    CriticalSection s;
    int8_t rxLock = rxLock;
    while (rxLock > queueLOCKED_UNMODIFIED) {
      if (PendingTX.Length > 0) {
        if (RemoveFromEventList(&(PendingTX))) {
          MissedYield();
        }
        --rxLock;
      } else {
        break;
      }
    }
    rxLock = queueUNLOCKED;
  }
};
static BaseType_t IsQueueEmpty(const Queue_t *Queue);
static BaseType_t IsQueueFull(const Queue_t *Queue);
static BaseType_t CopyDataToQueue(Queue_t *const Queue,
                                  const void *pvItemToQueue,
                                  const BaseType_t xPosition);
static void CopyDataFromQueue(Queue_t *const Queue, void *const pvBuffer);
static BaseType_t NotifyQueueSetContainer(const Queue_t *const Queue);
static void Initialiseq(const UBaseType_t uxQueueLength,
                        const UBaseType_t uxItemSize, uint8_t *qStorage,
                        const uint8_t ucQueueType, Queue_t *q);
static void InitialiseMutex(Queue_t *q);
static UBaseType_t GetHighestPriorityOfWaitToReceiveList(Queue_t *const Queue);

#define IncrementQueueTxLock(Queue, cTxLock)                    \
  do {                                                          \
    const UBaseType_t uxNumberOfTasks = TaskGetNumberOfTasks(); \
    if ((UBaseType_t)(txLock) < uxNumberOfTasks) {              \
      (Queue)->txLock = (int8_t)((txLock) + (int8_t)1);         \
    }                                                           \
  } while (0)

#define IncrementQueueRxLock(Queue, cRxLock)                    \
  do {                                                          \
    const UBaseType_t uxNumberOfTasks = TaskGetNumberOfTasks(); \
    if ((UBaseType_t)(rxLock) < uxNumberOfTasks) {              \
      (Queue)->rxLock = (int8_t)((rxLock) + (int8_t)1);         \
    }                                                           \
  } while (0)

BaseType_t xQueueGenericReset(QueueHandle_t q, BaseType_t xq) {
  BaseType_t Ret = true;
  if ((q != NULL) && (q->length >= 1U) &&

      ((SIZE_MAX / q->length) >= q->itemSize)) {
    ENTER_CRITICAL();
    {
      q->u.q.pcTail = q->Head + (q->length * q->itemSize);
      q->nWaiting = 0U;
      q->write = q->Head;
      q->u.q.read = q->Head + ((q->length - 1U) * q->itemSize);
      q->rxLock = queueUNLOCKED;
      q->txLock = queueUNLOCKED;
      if (xq == false) {
        if (q->PendingTX.Length > 0) {
          if (RemoveFromEventList(&(q->PendingTX))) {
            queueYIELD_IF_USING_PREEMPTION();
          }
        }
      } else {
        q->PendingTX.init();
        q->PendingRX.init();
      }
    }
    EXIT_CRITICAL();
  } else {
    Ret = false;
  }
  return Ret;
}

QueueHandle_t QueueCreateStatic(const UBaseType_t len,
                                const UBaseType_t itemSize, uint8_t *storage,
                                StaticQueue_t *staticQ, const uint8_t type) {
  Queue_t *q = NULL;

  if ((len > 0) && (staticQ != NULL) &&
      (!((storage != NULL) && (itemSize == 0U))) &&
      (!((storage == NULL) && (itemSize != 0U)))) {
    q = (Queue_t *)staticQ;
    q->StaticallyAllocated = true;
    Initialiseq(len, itemSize, storage, type, q);
  }
  return q;
}

BaseType_t GetStaticBuffers(QueueHandle_t xQueue, uint8_t **storage,
                            StaticQueue_t **staticQ) {
  BaseType_t Ret;
  Queue_t *const Queue = xQueue;
  if (Queue->StaticallyAllocated) {
    if (storage != NULL) {
      *storage = (uint8_t *)Queue->Head;
    }

    *staticQ = (StaticQueue_t *)Queue;
    return true;
  }
  return false;
}

QueueHandle_t xQueueGenericCreate(const UBaseType_t uxQueueLength,
                                  const UBaseType_t uxItemSize,
                                  const uint8_t ucQueueType) {
  Queue_t *q = NULL;
  size_t xQueueSizeInBytes;
  uint8_t *qStorage;
  if ((uxQueueLength > 0) &&

      ((SIZE_MAX / uxQueueLength) >= uxItemSize) &&

      ((UBaseType_t)(SIZE_MAX - sizeof(Queue_t)) >=
       (uxQueueLength * uxItemSize))) {
    xQueueSizeInBytes = (size_t)((size_t)uxQueueLength * (size_t)uxItemSize);
    q = (Queue_t *)pvPortMalloc(sizeof(Queue_t) + xQueueSizeInBytes);
    if (q != NULL) {
      qStorage = (uint8_t *)q;
      qStorage += sizeof(Queue_t);
      { q->StaticallyAllocated = false; }
      Initialiseq(uxQueueLength, uxItemSize, qStorage, ucQueueType, q);
    }
  }
  return q;
}

static void Initialiseq(const UBaseType_t length, const UBaseType_t itemSize,
                        uint8_t *storage, const uint8_t type, Queue_t *q) {
  (void)type;
  q->Head = (itemSize > 0 ? (int8_t *)storage : (int8_t *)q);
  q->length = length;
  q->itemSize = itemSize;
  (void)xQueueGenericReset(q, true);
  q->set = NULL;
}

static void InitialiseMutex(Queue_t *q) {
  if (q != NULL) {
    q->u.sema.MutHolder = NULL;
    q->Head = queueQUEUE_IS_MUTEX;
    q->u.sema.RecursiveCallCount = 0;
    (void)Send(q, NULL, (TickType_t)0U, queueSEND_TO_BACK);
  }
}

#if ((configUSE_MUTEXES == 1) && (configSUPPORT_DYNAMIC_ALLOCATION == 1))
QueueHandle_t xQueueCreateMutex(const uint8_t ucQueueType) {
  QueueHandle_t xq;
  const UBaseType_t uxMutexLength = (UBaseType_t)1, uxMutexSize = 0;
  xq = xQueueGenericCreate(uxMutexLength, uxMutexSize, ucQueueType);
  InitialiseMutex((Queue_t *)xq);
  return xq;
}
#endif

#if ((configUSE_MUTEXES == 1) && (configSUPPORT_STATIC_ALLOCATION == 1))
QueueHandle_t xQueueCreateMuteStatic(const uint8_t ucQueueType,
                                     StaticQueue_t *pStaticQueue) {
  QueueHandle_t q;
  const UBaseType_t uxMutexLength = (UBaseType_t)1, uxMutexSize = 0;

  (void)ucQueueType;
  q = QueueCreateStatic(uxMutexLength, uxMutexSize, NULL, pStaticQueue,
                        ucQueueType);
  InitialiseMutex((Queue_t *)q);
  return q;
}
#endif

TaskHandle_t xQueueGetMutexHolder(QueueHandle_t sema) {
  TaskHandle_t Return;
  Queue_t *const Semaphore = (Queue_t *)sema;

  ENTER_CRITICAL();
  {
    if (Semaphore->Head == queueQUEUE_IS_MUTEX) {
      Return = Semaphore->u.sema.MutHolder;
    } else {
      Return = NULL;
    }
  }
  EXIT_CRITICAL();
  return Return;
}

TaskHandle_t xQueueGetMutexHolderFromISR(QueueHandle_t xSemaphore) {
  TaskHandle_t Return;

  if (((Queue_t *)xSemaphore)->Head == queueQUEUE_IS_MUTEX) {
    Return = ((Queue_t *)xSemaphore)->u.sema.MutHolder;
  } else {
    Return = NULL;
  }
  return Return;
}

BaseType_t xQueueGiveMutexRecursive(QueueHandle_t xMutex) {
  BaseType_t Ret;
  Queue_t *const Mutex = (Queue_t *)xMutex;

  if (Mutex->u.sema.MutHolder == GetCurrentTaskHandle()) {
    (Mutex->u.sema.RecursiveCallCount)--;

    if (Mutex->u.sema.RecursiveCallCount == 0) {
      (void)Send(Mutex, NULL, queueMUTEX_GIVE_BLOCK_TIME, queueSEND_TO_BACK);
    }
    return true;
  }
  return false;
}

BaseType_t xQueueTakeMutexRecursive(QueueHandle_t xMutex, TickType_t ticks) {
  BaseType_t Ret;
  Queue_t *const Mutex = (Queue_t *)xMutex;
  if (Mutex->u.sema.MutHolder == GetCurrentTaskHandle()) {
    (Mutex->u.sema.RecursiveCallCount)++;
    Ret = true;
  } else {
    Ret = xQueueSemaphoreTake(Mutex, ticks);

    if (Ret) {
      (Mutex->u.sema.RecursiveCallCount)++;
    }
  }
  return Ret;
}

#if ((configUSE_COUNTING_SEMAPHORES == 1) && \
     (configSUPPORT_STATIC_ALLOCATION == 1))
QueueHandle_t xQueueCreateCountingSemaphoreStatic(
    const UBaseType_t uxMaxCount, const UBaseType_t uxInitialCount,
    StaticQueue_t *pStaticQueue) {
  QueueHandle_t xHandle = NULL;
  if ((uxMaxCount != 0U) && (uxInitialCount <= uxMaxCount)) {
    xHandle =
        QueueCreateStatic(uxMaxCount, queueSEMAPHORE_QUEUE_ITEM_LENGTH, NULL,
                          pStaticQueue, queueQUEUE_TYPE_COUNTING_SEMAPHORE);
    if (xHandle != NULL) {
      ((Queue_t *)xHandle)->nWaiting = uxInitialCount;
    }
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
  }
  return xHandle;
}
#endif

BaseType_t Send(QueueHandle_t xQueue, const void *const pvItemToQueue,
                TickType_t ticks, const BaseType_t copyPos) {
  BaseType_t xEntryTimeSet = false, xYieldRequired;
  TimeOut_t xTimeOut;
  Queue_t *const Queue = xQueue;
  for (;;) {
    ENTER_CRITICAL();
    {
      if ((Queue->nWaiting < Queue->length) || (copyPos == queueOVERWRITE)) {
        const UBaseType_t nPrevWaiting = Queue->nWaiting;
        xYieldRequired = CopyDataToQueue(Queue, pvItemToQueue, copyPos);
        if (Queue->set != NULL) {
          if ((copyPos == queueOVERWRITE) && (nPrevWaiting != 0)) {
          } else if (NotifyQueueSetContainer(Queue)) {
            queueYIELD_IF_USING_PREEMPTION();
          }
        } else {
          if (Queue->PendingRX.Length > 0) {
            if (RemoveFromEventList(&(Queue->PendingRX))) {
              queueYIELD_IF_USING_PREEMPTION();
            }
          } else if (xYieldRequired) {
            queueYIELD_IF_USING_PREEMPTION();
          }
        }
        EXIT_CRITICAL();
        return true;
      } else {
        if (ticks == (TickType_t)0) {
          EXIT_CRITICAL();
          return errQUEUE_FULL;
        } else if (xEntryTimeSet == false) {
          SetTimeOutState(&xTimeOut);
          xEntryTimeSet = true;
        }
      }
    }
    EXIT_CRITICAL();

    TaskSuspendAll();
    Queue->Lock();

    if (CheckForTimeOut(&xTimeOut, &ticks) == false) {
      if (IsQueueFull(Queue)) {
        PlaceOnEventList(&(Queue->PendingTX), ticks);

        Queue->Unlock();

        if (ResumeAll() == false) {
          taskYIELD_WITHIN_API();
        }
      } else {
        Queue->Unlock();
        (void)ResumeAll();
      }
    } else {
      Queue->Unlock();
      (void)ResumeAll();
      return errQUEUE_FULL;
    }
  }
}

BaseType_t SendFromISR(QueueHandle_t xQueue, const void *const pvItemToQueue,
                       BaseType_t *const woken, const BaseType_t copyPos) {
  BaseType_t Ret;
  UBaseType_t uxSavedInterruptStatus;
  Queue_t *const q = xQueue;
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();

  uxSavedInterruptStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
    if ((q->nWaiting < q->length) || (copyPos == queueOVERWRITE)) {
      const int8_t txLock = q->txLock;
      const UBaseType_t nPrevWaiting = q->nWaiting;

      (void)CopyDataToQueue(q, pvItemToQueue, copyPos);

      if (txLock == queueUNLOCKED) {
        if (q->set != NULL) {
          if ((copyPos == queueOVERWRITE) && (nPrevWaiting != 0)) {
          } else if (NotifyQueueSetContainer(q)) {
            if (woken != NULL) {
              *woken = true;
            }
          }
        } else {
          if (q->PendingRX.Length > 0) {
            if (RemoveFromEventList(&(q->PendingRX))) {
              if (woken != NULL) {
                *woken = true;
              }
            }
          }
        }
      } else {
        IncrementQueueTxLock(q, txLock);
      }
      Ret = true;
    } else {
      Ret = errQUEUE_FULL;
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return Ret;
}

BaseType_t GiveFromISR(QueueHandle_t q, BaseType_t *const woken) {
  BaseType_t Ret;
  UBaseType_t irqState;

  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  irqState = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
    const UBaseType_t nWaiting = q->nWaiting;
    if (nWaiting < q->length) {
      const int8_t txLock = q->txLock;
      q->nWaiting = (UBaseType_t)(nWaiting + (UBaseType_t)1);
      if (txLock == queueUNLOCKED) {
        {
          if (q->set != NULL) {
            if (NotifyQueueSetContainer(q)) {
              if (woken != NULL) {
                *woken = true;
              }
            }
          } else {
            if (q->PendingRX.Length > 0) {
              if (RemoveFromEventList(&(q->PendingRX))) {
                if (woken != NULL) {
                  *woken = true;
                }
              }
            }
          }
        }
      } else {
        IncrementQueueTxLock(q, txLock);
      }
      Ret = true;
    } else {
      Ret = errQUEUE_FULL;
    }
  }
  EXIT_CRITICAL_FROM_ISR(irqState);
  return Ret;
}

BaseType_t Recv(QueueHandle_t q, void *const pvBuffer, TickType_t ticks) {
  BaseType_t xEntryTimeSet = false;
  TimeOut_t xTimeOut;
  for (;;) {
    ENTER_CRITICAL();
    {
      const UBaseType_t nWaiting = q->nWaiting;

      if (nWaiting > 0) {
        CopyDataFromQueue(q, pvBuffer);
        q->nWaiting = (UBaseType_t)(nWaiting - (UBaseType_t)1);
        if (q->PendingTX.Length > 0) {
          if (RemoveFromEventList(&(q->PendingTX))) {
            queueYIELD_IF_USING_PREEMPTION();
          }
        }
        EXIT_CRITICAL();
        return true;
      } else {
        if (ticks == (TickType_t)0) {
          EXIT_CRITICAL();
          return errQUEUE_EMPTY;
        } else if (xEntryTimeSet == false) {
          SetTimeOutState(&xTimeOut);
          xEntryTimeSet = true;
        } else {
          mtCOVERAGE_TEST_MARKER();
        }
      }
    }
    EXIT_CRITICAL();

    TaskSuspendAll();
    q->Lock();

    if (CheckForTimeOut(&xTimeOut, &ticks) == false) {
      if (IsQueueEmpty(q)) {
        PlaceOnEventList(&(q->PendingRX), ticks);
        q->Unlock();
        if (ResumeAll() == false) {
          taskYIELD_WITHIN_API();
        }
      } else {
        q->Unlock();
        (void)ResumeAll();
      }
    } else {
      q->Unlock();
      (void)ResumeAll();
      if (IsQueueEmpty(q)) {
        return errQUEUE_EMPTY;
      }
    }
  }
}

BaseType_t xQueueSemaphoreTake(QueueHandle_t xQueue, TickType_t ticks) {
  BaseType_t xEntryTimeSet = false;
  TimeOut_t xTimeOut;
  Queue_t *const Queue = xQueue;
  BaseType_t xInheritanceOccurred = false;
  for (;;) {
    {
      CriticalSection s;
      const UBaseType_t uxSemaphoreCount = Queue->nWaiting;

      if (uxSemaphoreCount > 0) {
        Queue->nWaiting = (UBaseType_t)(uxSemaphoreCount - (UBaseType_t)1);
        if (Queue->Head == queueQUEUE_IS_MUTEX) {
          Queue->u.sema.MutHolder = IncrementMutexHeldCount();
        }
        if (Queue->PendingTX.Length > 0) {
          if (RemoveFromEventList(&(Queue->PendingTX))) {
            queueYIELD_IF_USING_PREEMPTION();
          }
        }
        return true;
      } else {
        if (ticks == (TickType_t)0) {
          return errQUEUE_EMPTY;
        } else if (xEntryTimeSet == false) {
          SetTimeOutState(&xTimeOut);
          xEntryTimeSet = true;
        }
      }
    }

    TaskSuspendAll();
    Queue->Lock();

    if (CheckForTimeOut(&xTimeOut, &ticks) == false) {
      if (IsQueueEmpty(Queue)) {
#if (configUSE_MUTEXES == 1)
        {
          if (Queue->Head == queueQUEUE_IS_MUTEX) {
            ENTER_CRITICAL();
            { xInheritanceOccurred = PriorityInherit(Queue->u.sema.MutHolder); }
            EXIT_CRITICAL();
          }
        }
#endif
        PlaceOnEventList(&(Queue->PendingRX), ticks);
        Queue->Unlock();
        if (ResumeAll() == false) {
          taskYIELD_WITHIN_API();
        }
      } else {
        Queue->Unlock();
        (void)ResumeAll();
      }
    } else {
      Queue->Unlock();
      (void)ResumeAll();

      if (IsQueueEmpty(Queue)) {
        {
          if (xInheritanceOccurred) {
            CriticalSection s;
            PriorityDisinheritAfterTimeout(
                Queue->u.sema.MutHolder,
                GetHighestPriorityOfWaitToReceiveList(Queue));
          }
        }
        return errQUEUE_EMPTY;
      }
    }
  }
}

BaseType_t xQueuePeek(QueueHandle_t xQueue, void *const pvBuffer,
                      TickType_t ticks) {
  BaseType_t xEntryTimeSet = false;
  TimeOut_t xTimeOut;
  int8_t *pcOriginalReadPosition;
  Queue_t *const Queue = xQueue;
  for (;;) {
    ENTER_CRITICAL();
    {
      const UBaseType_t nWaiting = Queue->nWaiting;

      if (nWaiting > 0) {
        pcOriginalReadPosition = Queue->u.q.read;
        CopyDataFromQueue(Queue, pvBuffer);

        Queue->u.q.read = pcOriginalReadPosition;

        if (Queue->PendingRX.Length > 0) {
          if (RemoveFromEventList(&(Queue->PendingRX))) {
            queueYIELD_IF_USING_PREEMPTION();
          }
        }
        EXIT_CRITICAL();
        return true;
      } else {
        if (ticks == (TickType_t)0) {
          EXIT_CRITICAL();
          return errQUEUE_EMPTY;
        } else if (xEntryTimeSet == false) {
          SetTimeOutState(&xTimeOut);
          xEntryTimeSet = true;
        }
      }
    }
    EXIT_CRITICAL();

    TaskSuspendAll();
    Queue->Lock();

    if (CheckForTimeOut(&xTimeOut, &ticks) == false) {
      if (IsQueueEmpty(Queue)) {
        PlaceOnEventList(&(Queue->PendingRX), ticks);
        Queue->Unlock();
        if (ResumeAll() == false) {
          taskYIELD_WITHIN_API();
        }
      } else {
        Queue->Unlock();
        (void)ResumeAll();
      }
    } else {
      Queue->Unlock();
      (void)ResumeAll();
      if (IsQueueEmpty(Queue)) {
        return errQUEUE_EMPTY;
      }
    }
  }
}
BaseType_t RecvFromISR(QueueHandle_t xQueue, void *const pvBuffer,
                       BaseType_t *const woken) {
  BaseType_t Ret;
  UBaseType_t uxSavedInterruptStatus;
  Queue_t *const Queue = xQueue;
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  uxSavedInterruptStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
    const UBaseType_t nWaiting = Queue->nWaiting;

    if (nWaiting > 0) {
      const int8_t rxLock = Queue->rxLock;
      CopyDataFromQueue(Queue, pvBuffer);
      Queue->nWaiting = (UBaseType_t)(nWaiting - (UBaseType_t)1);

      if (rxLock == queueUNLOCKED) {
        if (Queue->PendingTX.Length > 0) {
          if (RemoveFromEventList(&(Queue->PendingTX))) {
            if (woken != NULL) {
              *woken = true;
            }
          }
        }
      } else {
        IncrementQueueRxLock(Queue, rxLock);
      }
      Ret = true;
    } else {
      Ret = false;
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return Ret;
}

BaseType_t xQueuePeekFromISR(QueueHandle_t xQueue, void *const pvBuffer) {
  BaseType_t Ret;
  UBaseType_t uxSavedInterruptStatus;
  int8_t *pcOriginalReadPosition;
  Queue_t *const Queue = xQueue;
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  uxSavedInterruptStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
    if (Queue->nWaiting > 0) {
      pcOriginalReadPosition = Queue->u.q.read;
      CopyDataFromQueue(Queue, pvBuffer);
      Queue->u.q.read = pcOriginalReadPosition;
      Ret = true;
    } else {
      Ret = false;
    }
  }
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return Ret;
}
UBaseType_t uxQueueMessagesWaiting(const QueueHandle_t q) {
  CriticalSection s;
  return ((Queue_t *)q)->nWaiting;
}
UBaseType_t uxQueueSpacesAvailable(const QueueHandle_t q) {
  CriticalSection s;
  return (UBaseType_t)(q->length - q->nWaiting);
}

UBaseType_t uxQueueMessagesWaitingFromISR(const QueueHandle_t q) {
  return q->nWaiting;
}

void vQueueDelete(QueueHandle_t q) {
  if (!q->StaticallyAllocated) {
    vPortFree(q);
  }
}
UBaseType_t GetQueueItemSize(QueueHandle_t xQueue) {
  return ((Queue_t *)xQueue)->itemSize;
}

UBaseType_t GetQueueLength(QueueHandle_t xQueue) {
  return ((Queue_t *)xQueue)->length;
}

static UBaseType_t GetHighestPriorityOfWaitToReceiveList(Queue_t *const Queue) {
  if (Queue->PendingRX.Length > 0U) {
    return configMAX_PRIORITIES - (Queue->PendingRX.head()->Value);
  }
  return tskIDLE_PRIORITY;
}

static BaseType_t CopyDataToQueue(Queue_t *const q, const void *item,
                                  BaseType_t pos) {
  BaseType_t Ret = false;
  UBaseType_t nWaiting;

  nWaiting = q->nWaiting;
  if (q->itemSize == 0) {
    if (q->Head == queueQUEUE_IS_MUTEX) {
      Ret = PriorityDisinherit(q->u.sema.MutHolder);
      q->u.sema.MutHolder = NULL;
    }
  } else if (pos == queueSEND_TO_BACK) {
    (void)memcpy((void *)q->write, item, (size_t)q->itemSize);
    q->write += q->itemSize;
    if (q->write >= q->u.q.pcTail) {
      q->write = q->Head;
    }
  } else {
    (void)memcpy((void *)q->u.q.read, item, (size_t)q->itemSize);
    q->u.q.read -= q->itemSize;
    if (q->u.q.read < q->Head) {
      q->u.q.read = (q->u.q.pcTail - q->itemSize);
    }
    if (pos == queueOVERWRITE) {
      if (nWaiting > 0) {
        --nWaiting;
      }
    }
  }
  q->nWaiting = (UBaseType_t)(nWaiting + (UBaseType_t)1);
  return Ret;
}

static void CopyDataFromQueue(Queue_t *const q, void *const pvBuffer) {
  if (q->itemSize != 0) {
    q->u.q.read += q->itemSize;
    if (q->u.q.read >= q->u.q.pcTail) {
      q->u.q.read = q->Head;
    }
    (void)memcpy((void *)pvBuffer, (void *)q->u.q.read, (size_t)q->itemSize);
  }
}

static BaseType_t IsQueueEmpty(const Queue_t *Queue) {
  CriticalSection s;
  return Queue->nWaiting == 0;
}

BaseType_t xQueueIsQueueEmptyFromISR(const QueueHandle_t xQueue) {
  return xQueue->nWaiting == 0;
}

static BaseType_t IsQueueFull(const Queue_t *Queue) {
  CriticalSection s;
  return Queue->nWaiting == Queue->length;
}

BaseType_t xQueueIsQueueFullFromISR(const QueueHandle_t xQueue) {
  BaseType_t Ret;
  Queue_t *const Queue = xQueue;
  return Queue->nWaiting == Queue->length;
}

void vQueueWaitForMessageRestricted(QueueHandle_t Queue, TickType_t ticks,
                                    const BaseType_t xWaitIndefinitely) {
  Queue->Lock();
  if (Queue->nWaiting == 0U) {
    PlaceOnEventListRestricted(&(Queue->PendingRX), ticks, xWaitIndefinitely);
  }
  Queue->Unlock();
}
QueueSetHandle_t xQueueCreateSet(const UBaseType_t uxEventQueueLength) {
  return xQueueGenericCreate(uxEventQueueLength, (UBaseType_t)sizeof(Queue_t *),
                             queueQUEUE_TYPE_SET);
}
BaseType_t xQueueAddToSet(QueueSetMemberHandle_t xQueueOrSemaphore,
                          QueueSetHandle_t xQueueSet) {
  BaseType_t Ret;
  ENTER_CRITICAL();
  {
    if (((Queue_t *)xQueueOrSemaphore)->set != NULL) {
      Ret = false;
    } else if (((Queue_t *)xQueueOrSemaphore)->nWaiting != 0) {
      Ret = false;
    } else {
      ((Queue_t *)xQueueOrSemaphore)->set = xQueueSet;
      Ret = true;
    }
  }
  EXIT_CRITICAL();
  return Ret;
}
BaseType_t xQueueRemoveFromSet(QueueSetMemberHandle_t xQueueOrSemaphore,
                               QueueSetHandle_t xQueueSet) {
  BaseType_t Ret;
  Queue_t *const QueueOrSemaphore = (Queue_t *)xQueueOrSemaphore;
  if (QueueOrSemaphore->set != xQueueSet) {
    Ret = false;
  } else if (QueueOrSemaphore->nWaiting != 0) {
    Ret = false;
  } else {
    ENTER_CRITICAL();
    { QueueOrSemaphore->set = NULL; }
    EXIT_CRITICAL();
    Ret = true;
  }
  return Ret;
}

QueueSetMemberHandle_t xQueueSelectFromSet(QueueSetHandle_t set,
                                           TickType_t const ticks) {
  QueueSetMemberHandle_t Ret = NULL;
  (void)Recv((QueueHandle_t)set, &Ret, ticks);
  return Ret;
}

QueueSetMemberHandle_t xQueueSelectFromSetFromISR(QueueSetHandle_t set) {
  QueueSetMemberHandle_t Ret = NULL;
  (void)RecvFromISR((QueueHandle_t)set, &Ret, NULL);
  return Ret;
}

static BaseType_t NotifyQueueSetContainer(const Queue_t *const Queue) {
  Queue_t *container = Queue->set;
  BaseType_t Ret = false;

  if (container->nWaiting < container->length) {
    const int8_t txLock = container->txLock;

    Ret = CopyDataToQueue(container, &Queue, queueSEND_TO_BACK);
    if (txLock == queueUNLOCKED) {
      if (container->PendingRX.Length > 0) {
        if (RemoveFromEventList(&(container->PendingRX))) {
          Ret = true;
        }
      }
    } else {
      IncrementQueueTxLock(container, txLock);
    }
  }
  return Ret;
}
