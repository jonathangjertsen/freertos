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

struct QueuePointers_t {
  int8_t *pcTail;
  int8_t *read;
};

struct SemaphoreData_t {
  TaskHandle_t MutHolder;
  UBaseType_t RecursiveCallCount;
};
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
  uint8_t StaticAlloc;
  struct Queue_t *set;

  bool IsMutex() const { return Head == nullptr; }

  void MakeMutex() { Head = nullptr; }

  bool EmptyFromISR() const { return nWaiting == 0; }

  bool Empty() const {
    CriticalSection s;
    return EmptyFromISR();
  }

  bool FullFromISR() const { return nWaiting == length; }

  bool Full() const {
    CriticalSection s;
    return FullFromISR();
  }

  void Lock() {
    if (rxLock == -1) {
      rxLock = 0;
    }
    if (txLock == -1) {
      txLock = 0;
    }
  }

  void Unlock() {
    {
      CriticalSection s;
      while (txLock > 0) {
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
      txLock = -1;
    }

    CriticalSection s;
    while (rxLock > 0) {
      if (PendingTX.Length > 0) {
        if (RemoveFromEventList(&(PendingTX))) {
          MissedYield();
        }
        --rxLock;
      } else {
        break;
      }
    }
    rxLock = -1;
  }

  TaskHandle_t MutexHolder() const {
    CriticalSection s;
    return MutexHolderFromISR();
  }

  TaskHandle_t MutexHolderFromISR() const { return IsMutex() ? u.sema.MutHolder : nullptr; }

  Queue_t *InitAsMutex() {
    MakeMutex();
    u.sema.MutHolder = NULL;
    u.sema.RecursiveCallCount = 0;
    Send(NULL, 0U, QueuePos_t::Back);
    return this;
  }

  Queue_t *InitAsQueue(UBaseType_t length, UBaseType_t itemSIze, uint8_t *storage) {
    Head = (itemSize > 0 ? (int8_t *)storage : (int8_t *)this);
    length = length;
    itemSize = itemSize;
    Reset(true);
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

  bool copyFrom(void const *buf, QueuePos_t pos) {
    BaseType_t Ret = false;
    UBaseType_t nWaiting = nWaiting;
    if (itemSize == 0) {
      if (IsMutex()) {
        Ret = PriorityDisinherit(u.sema.MutHolder);
        u.sema.MutHolder = NULL;
      }
    } else if (pos == QueuePos_t::Back) {
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
      if (pos == QueuePos_t::Overwrite) {
        if (nWaiting > 0) {
          --nWaiting;
        }
      }
    }
    nWaiting++;
    return Ret;
  }

  bool Reset(bool isQueue) {
    if ((length >= 1U) && ((SIZE_MAX / length) >= itemSize)) {
      CriticalSection s;
      u.q.pcTail = Head + (length * itemSize);
      nWaiting = 0U;
      write = Head;
      u.q.read = Head + ((length - 1U) * itemSize);
      rxLock = -1;
      txLock = -1;
      if (isQueue) {
        PendingTX.init();
        PendingRX.init();
        return true;
      }
      if (PendingTX.Length > 0 && RemoveFromEventList(&(PendingTX))) {
        portYIELD_WITHIN_API();
      }
      return true;
    }
    return false;
  }

  bool UnlockAndResume() {
    Unlock();
    return ResumeAll();
  }

  bool Send(const void *const item, TickType_t ticks, QueuePos_t pos) {
    bool entryTimeSet = false;
    bool needYield;
    TimeOut_t timeout;
    for (;;) {
      {
        CriticalSection s;
        bool canWriteNow = (nWaiting < length) || (pos == QueuePos_t::Overwrite);
        if (canWriteNow) {
          const UBaseType_t nPrevWaiting = nWaiting;
          needYield = copyFrom(item, pos);
          if (set != NULL) {
            if ((pos == QueuePos_t::Overwrite) && (nPrevWaiting != 0)) {
              return true;
            }
            if (NotifyQueueSetContainer(this)) {
              portYIELD_WITHIN_API();
            }
            return true;
          }

          if (PendingRX.Length > 0) {
            if (RemoveFromEventList(&(PendingRX))) {
              portYIELD_WITHIN_API();
            }
            return true;
          }
          if (needYield) {
            portYIELD_WITHIN_API();
          }
          return true;
        }

        if (ticks == 0) {
          return false;
        }

        if (!entryTimeSet) {
          SetTimeOutState(&timeout);
          entryTimeSet = true;
        }
      }

      TaskSuspendAll();
      Lock();

      if (CheckForTimeOut(&timeout, &ticks)) {
        UnlockAndResume();
        return false;
      }

      if (Full()) {
        PlaceOnEventList(&PendingTX, ticks);
        if (!UnlockAndResume()) {
          taskYIELD_WITHIN_API();
        }
        continue;
      }

      UnlockAndResume();
    }
  }
};
static BaseType_t NotifyQueueSetContainer(const Queue_t *const Queue);
static UBaseType_t GetHighestPriorityOfWaitToReceiveList(Queue_t *const Queue);

Queue_t *QueueCreateStatic(const UBaseType_t len, const UBaseType_t itemSize, uint8_t *storage,
                           StaticQueue_t *staticQ) {
  if ((len > 0) && (staticQ != NULL) && (!((storage != NULL) && (itemSize == 0U))) &&
      (!((storage == NULL) && (itemSize != 0U)))) {
    Queue_t *q = (Queue_t *)staticQ;
    q->StaticAlloc = true;
    return q->InitAsQueue(len, itemSize, storage);
  }
  return nullptr;
}

BaseType_t GetStaticBuffers(Queue_t *const q, uint8_t **storage, StaticQueue_t **staticQ) {
  if (!q->StaticAlloc) {
    return false;
  }
  if (storage != NULL) {
    *storage = (uint8_t *)q->Head;
  }
  *staticQ = (StaticQueue_t *)q;
  return true;
}

Queue_t *QueueCreate(const UBaseType_t len, const UBaseType_t itemSize) {
  if ((len <= 0) || ((SIZE_MAX / len) < itemSize) || (SIZE_MAX - sizeof(Queue_t) < (len * itemSize))) {
    return nullptr;
  }
  Queue_t *q = (Queue_t *)pvPortMalloc(sizeof(Queue_t) + len * itemSize);
  if (q == nullptr) {
    return q;
  }
  q->StaticAlloc = false;
  q->InitAsQueue(len, itemSize, (uint8_t *)q + sizeof(Queue_t));
  return q;
}

Queue_t *QueueCreateMutex(const uint8_t type) { return QueueCreate(1, 0)->InitAsMutex(); }

Queue_t *QueueCreateMutexStatic(const uint8_t type, StaticQueue_t *pStaticQueue) {
  return QueueCreateStatic(1, 0, nullptr, pStaticQueue)->InitAsMutex();
}

BaseType_t QueueGiveMutexRecursive(Queue_t *const Mutex) {
  if (Mutex->u.sema.MutHolder != CurrentTaskHandle()) {
    return false;
  }
  if (--Mutex->u.sema.RecursiveCallCount == 0) {
    Send(Mutex, nullptr, 0, QueuePos_t::Back);
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
  Queue_t *q = QueueCreateStatic(max, 0, NULL, pStaticQueue);
  if (q != NULL) {
    q->nWaiting = init;
  }
  return q;
}

Queue_t *QueueCreateCountingSemaphore(const UBaseType_t max, const UBaseType_t init) {
  if ((max == 0U) || (init > max)) {
    return nullptr;
  }
  Queue_t *q = QueueCreate(max, 0);
  if (q != NULL) {
    q->nWaiting = init;
  }
  return q;
}

BaseType_t SendFromISR(Queue_t *q, const void *const item, BaseType_t *const woken, QueuePos_t pos) {
  portASSERT_IF_INTERRUPT_PRIORITY_INVALID();

  CriticalSectionISR s;
  if ((q->nWaiting >= q->length) && (pos != QueuePos_t::Overwrite)) {
    return false;
  }
  const UBaseType_t nPrevWaiting = q->nWaiting;
  q->copyFrom(item, pos);
  if (q->txLock != -1) {
    q->incTXLock();
    return true;
  }
  if (q->set != NULL) {
    if ((pos == QueuePos_t::Overwrite) && (nPrevWaiting != 0)) {
    } else if (NotifyQueueSetContainer(q) && (woken != nullptr)) {
      *woken = true;
    }
    return true;
  }
  if ((q->PendingRX.Length > 0) && RemoveFromEventList(&(q->PendingRX)) && woken != nullptr) {
    *woken = true;
  }
  return true;
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
    if (txLock == -1) {
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
  BaseType_t entryTimeSet = false;
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
        } else if (entryTimeSet == false) {
          SetTimeOutState(&timeout);
          entryTimeSet = true;
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
  BaseType_t entryTimeSet = false;
  TimeOut_t timeout;
  bool didInherit = false;
  for (;;) {
    {
      CriticalSection s;
      auto n = q->nWaiting;
      if (n > 0) {
        q->nWaiting = n - 1;
        if (q->IsMutex()) {
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
      if (!entryTimeSet) {
        SetTimeOutState(&timeout);
        entryTimeSet = true;
      }
    }

    TaskSuspendAll();
    q->Lock();

    if (!CheckForTimeOut(&timeout, &ticks)) {
      if (q->Empty()) {
        if (q->IsMutex()) {
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
  BaseType_t entryTimeSet = false;
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
        } else if (!entryTimeSet) {
          SetTimeOutState(&timeout);
          entryTimeSet = true;
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
    if (rxLock == -1) {
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
  if (!q->StaticAlloc) {
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
  return QueueCreate(uxEventQueueLength, (UBaseType_t)sizeof(Queue_t *));
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
  bool ret = set->copyFrom(&q, QueuePos_t::Back);
  if (txLock == -1) {
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
