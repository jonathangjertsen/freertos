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

  bool EmptyFromISR() { return nWaiting == 0; }

  bool Empty() {
    CriticalSection s;
    return EmptyFromISR();
  }

  bool FullFromISR() { return nWaiting == length; }

  bool Full() {
    CriticalSection s;
    return FullFromISR();
  }

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

  TaskHandle_t MutexHolder() {
    CriticalSection s;
    return MutexHolderFromISR();
  }

  TaskHandle_t MutexHolderFromISR() { return Head == queueQUEUE_IS_MUTEX ? u.sema.MutHolder : nullptr; }

  Queue_t *InitAsMutex() {
    u.sema.MutHolder = NULL;
    Head = queueQUEUE_IS_MUTEX;
    u.sema.RecursiveCallCount = 0;
    (void)Send(this, NULL, 0U, queueSEND_TO_BACK);
    return this;
  }

  Queue_t *InitAsQueue(UBaseType_t length, UBaseType_t itemSIze, uint8_t *storage) {
    Head = (itemSize > 0 ? (int8_t *)storage : (int8_t *)this);
    length = length;
    itemSize = itemSize;
    (void)QueueGenericReset(this, true);
    set = NULL;
    return this;
  }

  void incTXLock() {
    if (txLock < TaskGetNumberOfTasks()) {
      txLock++;
    }
  }

  void incRXLock() {
    if (rxLock < TaskGetNumberOfTasks()) {
      rxLock++;
    }
  }

  void copyInto(void *buf) {
    if (itemSize != 0) {
      u.q.read += itemSize;
      if (u.q.read >= u.q.pcTail) {
        u.q.read = Head;
      }
      (void)memcpy((void *)buf, (void *)u.q.read, itemSize);
    }
  }

  bool copyFrom(void const *buf, UBaseType_t pos) {
    BaseType_t Ret = false;
    UBaseType_t nWaiting = nWaiting;
    if (itemSize == 0) {
      if (Head == queueQUEUE_IS_MUTEX) {
        Ret = PriorityDisinherit(u.sema.MutHolder);
        u.sema.MutHolder = NULL;
      }
    } else if (pos == queueSEND_TO_BACK) {
      memcpy(write, buf, (size_t)itemSize);
      write += itemSize;
      if (write >= u.q.pcTail) {
        write = Head;
      }
    } else {
      memcpy(u.q.read, buf, (size_t)itemSize);
      u.q.read -= itemSize;
      if (u.q.read < Head) {
        u.q.read = (u.q.pcTail - itemSize);
      }
      if (pos == queueOVERWRITE) {
        if (nWaiting > 0) {
          --nWaiting;
        }
      }
    }
    nWaiting++;
    return Ret;
  }
};
static BaseType_t NotifyQueueSetContainer(const Queue_t *const Queue);
static UBaseType_t GetHighestPriorityOfWaitToReceiveList(Queue_t *const Queue);

BaseType_t QueueGenericReset(Queue_t *q, BaseType_t xq) {
  BaseType_t Ret = true;
  if ((q != NULL) && (q->length >= 1U) && ((SIZE_MAX / q->length) >= q->itemSize)) {
    ENTER_CRITICAL();
    q->u.q.pcTail = q->Head + (q->length * q->itemSize);
    q->nWaiting = 0U;
    q->write = q->Head;
    q->u.q.read = q->Head + ((q->length - 1U) * q->itemSize);
    q->rxLock = queueUNLOCKED;
    q->txLock = queueUNLOCKED;
    if (xq == false) {
      if (q->PendingTX.Length > 0) {
        if (RemoveFromEventList(&(q->PendingTX))) {
          portYIELD_WITHIN_API();
        }
      }
    } else {
      q->PendingTX.init();
      q->PendingRX.init();
    }
    EXIT_CRITICAL();
  } else {
    Ret = false;
  }
  return Ret;
}

Queue_t *xQueueCreateStatic(const UBaseType_t len, const UBaseType_t itemSize, uint8_t *storage, StaticQueue_t *staticQ,
                            const uint8_t type) {
  if ((len > 0) && (staticQ != NULL) && (!((storage != NULL) && (itemSize == 0U))) &&
      (!((storage == NULL) && (itemSize != 0U)))) {
    Queue_t *q = (Queue_t *)staticQ;
    q->StaticallyAllocated = true;
    return q->InitAsQueue(len, itemSize, storage);
  }
  return nullptr;
}

BaseType_t GetStaticBuffers(Queue_t *const q, uint8_t **storage, StaticQueue_t **staticQ) {
  if (!q->StaticallyAllocated) {
    return false;
  }
  if (storage != NULL) {
    *storage = (uint8_t *)q->Head;
  }
  *staticQ = (StaticQueue_t *)q;
  return true;
}

Queue_t *QueueGenericCreate(const UBaseType_t len, const UBaseType_t itemSize, const uint8_t ucQueueType) {
  Queue_t *q = NULL;
  if ((len > 0) && ((SIZE_MAX / len) >= itemSize) && ((UBaseType_t)(SIZE_MAX - sizeof(Queue_t)) >= (len * itemSize))) {
    q = (Queue_t *)pvPortMalloc(sizeof(Queue_t) + (size_t)len * (size_t)itemSize);
    if (q != NULL) {
      q->StaticallyAllocated = false;
      q->InitAsQueue(len, itemSize, (uint8_t *)q + sizeof(Queue_t));
    }
  }
  return q;
}

Queue_t *QueueCreateMutex(const uint8_t type) { return QueueGenericCreate(1, 0, type)->InitAsMutex(); }

Queue_t *QueueCreateMutexStatic(const uint8_t type, StaticQueue_t *pStaticQueue) {
  return xQueueCreateStatic(1, 0, NULL, pStaticQueue, type)->InitAsMutex();
}

BaseType_t QueueGiveMutexRecursive(Queue_t *const Mutex) {
  if (Mutex->u.sema.MutHolder != CurrentTaskHandle()) {
    return false;
  }
  if (--Mutex->u.sema.RecursiveCallCount == 0) {
    Send(Mutex, NULL, queueMUTEX_GIVE_BLOCK_TIME, queueSEND_TO_BACK);
  }
  return true;
}

bool QueueTakeMutexRecursive(Queue_t *const mut, TickType_t ticks) {
  if (mut->u.sema.MutHolder == CurrentTaskHandle()) {
    mut->u.sema.RecursiveCallCount++;
    return true;
  }

  bool ret = QueueSemaphoreTake(mut, ticks);
  if (ret) {
    mut->u.sema.RecursiveCallCount++;
  }
  return ret;
}
Queue_t *QueueCreateCountingSemaphoreStatic(const UBaseType_t max, const UBaseType_t init,
                                            StaticQueue_t *pStaticQueue) {
  if ((max == 0U) || (init > max)) {
    return nullptr;
  }
  Queue_t *q =
      xQueueCreateStatic(max, queueSEMAPHORE_QUEUE_ITEM_LENGTH, NULL, pStaticQueue, queueQUEUE_TYPE_COUNTING_SEMAPHORE);
  if (q != NULL) {
    q->nWaiting = init;
  }
  return q;
}

Queue_t *QueueCreateCountingSemaphore(const UBaseType_t max, const UBaseType_t init) {
  if ((max == 0U) || (init > max)) {
    return nullptr;
  }
  Queue_t *q = QueueGenericCreate(max, queueSEMAPHORE_QUEUE_ITEM_LENGTH, queueQUEUE_TYPE_COUNTING_SEMAPHORE);
  if (q != NULL) {
    q->nWaiting = init;
  }
  return q;
}

BaseType_t Send(Queue_t *const q, const void *const pvItemToQueue, TickType_t ticks, const BaseType_t copyPos) {
  BaseType_t xEntryTimeSet = false, xYieldRequired;
  TimeOut_t timeout;
  for (;;) {
    ENTER_CRITICAL();
    if ((q->nWaiting < q->length) || (copyPos == queueOVERWRITE)) {
      const UBaseType_t nPrevWaiting = q->nWaiting;
      xYieldRequired = q->copyFrom(pvItemToQueue, copyPos);
      if (q->set != NULL) {
        if ((copyPos == queueOVERWRITE) && (nPrevWaiting != 0)) {
        } else if (NotifyQueueSetContainer(q)) {
          portYIELD_WITHIN_API();
        }
      } else {
        if (q->PendingRX.Length > 0) {
          if (RemoveFromEventList(&(q->PendingRX))) {
            portYIELD_WITHIN_API();
          }
        } else if (xYieldRequired) {
          portYIELD_WITHIN_API();
        }
      }
      EXIT_CRITICAL();
      return true;
    } else {
      if (ticks == (TickType_t)0) {
        EXIT_CRITICAL();
        return errQUEUE_FULL;
      } else if (xEntryTimeSet == false) {
        SetTimeOutState(&timeout);
        xEntryTimeSet = true;
      }
    }
    EXIT_CRITICAL();

    TaskSuspendAll();
    q->Lock();

    if (CheckForTimeOut(&timeout, &ticks) == false) {
      if (q->Full()) {
        PlaceOnEventList(&(q->PendingTX), ticks);
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
      return errQUEUE_FULL;
    }
  }
}

BaseType_t SendFromISR(Queue_t *Queue, const void *const pvItemToQueue, BaseType_t *const woken,
                       const BaseType_t copyPos) {
  BaseType_t Ret;
  UBaseType_t savedIrqStatus;
  Queue_t *const q = Queue;
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();

  savedIrqStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  {
    if ((q->nWaiting < q->length) || (copyPos == queueOVERWRITE)) {
      const int8_t txLock = q->txLock;
      const UBaseType_t nPrevWaiting = q->nWaiting;

      (void)q->copyFrom(pvItemToQueue, copyPos);

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
        q->incTXLock();
      }
      Ret = true;
    } else {
      Ret = errQUEUE_FULL;
    }
  }
  EXIT_CRITICAL_FROM_ISR(savedIrqStatus);
  return Ret;
}

BaseType_t GiveFromISR(Queue_t *q, BaseType_t *const woken) {
  BaseType_t Ret;
  UBaseType_t irqState;

  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  irqState = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  const UBaseType_t nWaiting = q->nWaiting;
  if (nWaiting < q->length) {
    const int8_t txLock = q->txLock;
    q->nWaiting++;
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
      q->incTXLock();
    }
    Ret = true;
  } else {
    Ret = errQUEUE_FULL;
  }
  EXIT_CRITICAL_FROM_ISR(irqState);
  return Ret;
}

BaseType_t Recv(Queue_t *q, void *const pvBuffer, TickType_t ticks) {
  BaseType_t xEntryTimeSet = false;
  TimeOut_t timeout;
  for (;;) {
    ENTER_CRITICAL();
    {
      if (q->nWaiting > 0) {
        q->copyInto(pvBuffer);
        q->nWaiting--;
        if (q->PendingTX.Length > 0) {
          if (RemoveFromEventList(&(q->PendingTX))) {
            portYIELD_WITHIN_API();
          }
        }
        EXIT_CRITICAL();
        return true;
      } else {
        if (ticks == (TickType_t)0) {
          EXIT_CRITICAL();
          return errQUEUE_EMPTY;
        } else if (xEntryTimeSet == false) {
          SetTimeOutState(&timeout);
          xEntryTimeSet = true;
        } else {
          mtCOVERAGE_TEST_MARKER();
        }
      }
    }
    EXIT_CRITICAL();

    TaskSuspendAll();
    q->Lock();

    if (CheckForTimeOut(&timeout, &ticks) == false) {
      if (q->Empty()) {
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
      if (q->Empty()) {
        return errQUEUE_EMPTY;
      }
    }
  }
}

BaseType_t QueueSemaphoreTake(Queue_t *const q, TickType_t ticks) {
  BaseType_t xEntryTimeSet = false;
  TimeOut_t timeout;
  bool didInherit = false;
  for (;;) {
    {
      CriticalSection s;
      auto n = q->nWaiting;
      if (n > 0) {
        q->nWaiting = n - 1;
        if (q->Head == queueQUEUE_IS_MUTEX) {
          q->u.sema.MutHolder = IncMutexHeldCount();
        }
        if (q->PendingTX.Length > 0) {
          if (RemoveFromEventList(&q->PendingTX)) {
            portYIELD_WITHIN_API();
          }
        }
        return true;
      }
      if (ticks == 0) {
        return errQUEUE_EMPTY;
      }
      if (!xEntryTimeSet) {
        SetTimeOutState(&timeout);
        xEntryTimeSet = true;
      }
    }

    TaskSuspendAll();
    q->Lock();

    if (!CheckForTimeOut(&timeout, &ticks)) {
      if (q->Empty()) {
        if (q->Head == queueQUEUE_IS_MUTEX) {
          CriticalSection s;
          didInherit = PriorityInherit(q->u.sema.MutHolder);
        }
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

      if (q->Empty()) {
        if (didInherit) {
          CriticalSection s;
          PriorityDisinheritAfterTimeout(q->u.sema.MutHolder, GetHighestPriorityOfWaitToReceiveList(q));
        }
        return errQUEUE_EMPTY;
      }
    }
  }
}

BaseType_t QueuePeek(Queue_t *const q, void *const pvBuffer, TickType_t ticks) {
  BaseType_t xEntryTimeSet = false;
  TimeOut_t timeout;
  for (;;) {
    {
      CriticalSection s;
      const UBaseType_t nWaiting = q->nWaiting;
      if (nWaiting > 0) {
        int8_t *origReadPos = q->u.q.read;
        q->copyInto(pvBuffer);
        q->u.q.read = origReadPos;
        if (q->PendingRX.Length > 0) {
          if (RemoveFromEventList(&(q->PendingRX))) {
            portYIELD_WITHIN_API();
          }
        }
        return true;
      } else {
        if (ticks == (TickType_t)0) {
          return errQUEUE_EMPTY;
        } else if (!xEntryTimeSet) {
          SetTimeOutState(&timeout);
          xEntryTimeSet = true;
        }
      }
    }

    TaskSuspendAll();
    q->Lock();

    if (CheckForTimeOut(&timeout, &ticks) == false) {
      if (q->Empty()) {
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
      if (q->Empty()) {
        return errQUEUE_EMPTY;
      }
    }
  }
}
BaseType_t RecvFromISR(Queue_t *q, void *const pvBuffer, BaseType_t *const woken) {
  BaseType_t Ret;
  UBaseType_t savedIrqStatus;
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  savedIrqStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  const UBaseType_t nWaiting = q->nWaiting;
  if (nWaiting > 0) {
    const int8_t rxLock = q->rxLock;
    q->copyInto(pvBuffer);
    q->nWaiting--;
    if (rxLock == queueUNLOCKED) {
      if (q->PendingTX.Length > 0) {
        if (RemoveFromEventList(&(q->PendingTX))) {
          if (woken != NULL) {
            *woken = true;
          }
        }
      }
    } else {
      q->incRXLock();
    }
    Ret = true;
  } else {
    Ret = false;
  }
  EXIT_CRITICAL_FROM_ISR(savedIrqStatus);
  return Ret;
}

BaseType_t QueuePeekFromISR(Queue_t *const q, void *const pvBuffer) {
  BaseType_t Ret;
  UBaseType_t savedIrqStatus;
  int8_t *origReadPos;
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();
  savedIrqStatus = (UBaseType_t)ENTER_CRITICAL_FROM_ISR();
  if (q->nWaiting > 0) {
    origReadPos = q->u.q.read;
    q->copyInto(pvBuffer);
    q->u.q.read = origReadPos;
    Ret = true;
  } else {
    Ret = false;
  }
  EXIT_CRITICAL_FROM_ISR(savedIrqStatus);
  return Ret;
}

UBaseType_t uxQueueMessagesWaiting(const Queue_t *q) {
  CriticalSection s;
  return ((Queue_t *)q)->nWaiting;
}

UBaseType_t uxQueueSpacesAvailable(const Queue_t *q) {
  CriticalSection s;
  return (UBaseType_t)(q->length - q->nWaiting);
}

UBaseType_t uxQueueMessagesWaitingFromISR(const Queue_t *q) { return q->nWaiting; }

void vQueueDelete(Queue_t *q) {
  if (!q->StaticallyAllocated) {
    vPortFree(q);
  }
}

static UBaseType_t GetHighestPriorityOfWaitToReceiveList(Queue_t *const Queue) {
  if (Queue->PendingRX.Length > 0U) {
    return configMAX_PRIORITIES - (Queue->PendingRX.head()->Value);
  }
  return tskIDLE_PRIORITY;
}

void vQueueWaitForMessageRestricted(Queue_t *Queue, TickType_t ticks, const BaseType_t xWaitIndefinitely) {
  Queue->Lock();
  if (Queue->nWaiting == 0U) {
    PlaceOnEventListRestricted(&(Queue->PendingRX), ticks, xWaitIndefinitely);
  }
  Queue->Unlock();
}
QueueSetHandle_t QueueCreateSet(const UBaseType_t uxEventQueueLength) {
  return QueueGenericCreate(uxEventQueueLength, (UBaseType_t)sizeof(Queue_t *), queueQUEUE_TYPE_SET);
}
BaseType_t QueueAddToSet(Queue_t *QueueOrSemaphore, QueueSetHandle_t QueueSet) {
  CriticalSection s;
  if ((QueueOrSemaphore->set != NULL) || (QueueOrSemaphore->nWaiting != 0)) {
    return false;
  }
  QueueOrSemaphore->set = QueueSet;
  return true;
}
BaseType_t QueueRemoveFromSet(Queue_t *const q, QueueSetHandle_t QueueSet) {
  if ((q->set != QueueSet) || (q->nWaiting != 0)) {
    return false;
  }
  CriticalSection s;
  q->set = NULL;
  return true;
}

Queue_t *QueueSelectFromSet(QueueSetHandle_t set, TickType_t const ticks) {
  Queue_t *Ret = NULL;
  (void)Recv((Queue_t *)set, &Ret, ticks);
  return Ret;
}

Queue_t *QueueSelectFromSetFromISR(QueueSetHandle_t set) {
  Queue_t *Ret = NULL;
  (void)RecvFromISR((Queue_t *)set, &Ret, NULL);
  return Ret;
}

static BaseType_t NotifyQueueSetContainer(const Queue_t *const q) {
  Queue_t *set = q->set;
  if (set->nWaiting >= set->length) {
    return false;
  }

  const int8_t txLock = set->txLock;
  bool ret = set->copyFrom(&q, queueSEND_TO_BACK);
  if (txLock == queueUNLOCKED) {
    if (set->PendingRX.Length > 0) {
      if (RemoveFromEventList(&(set->PendingRX))) {
        return true;
      }
    }
  } else {
    set->incTXLock();
  }
  return ret;
}
