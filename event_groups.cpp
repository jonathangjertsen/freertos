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

#include "event_groups.h"

#include <stdbool.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.hpp"
#include "timers.h"

typedef struct EventGroupDef_t {
  EventBits_t EventBits;
  List_t<TCB_t> TasksWaitingForBits;
  uint8_t StaticallyAllocated;
} EventGroup_t;

static BaseType_t TestWaitCondition(const EventBits_t uxCurrentEventBits,
                                    const EventBits_t uxBitsToWaitFor,
                                    const BaseType_t xWaitForAllBits);

EventGroupHandle_t xEventGroupCreateStatic(
    StaticEventGroup_t *EventGroupBuffer) {
  EventGroup_t *EventBits;
  configASSERT(EventGroupBuffer);
#if (configASSERT_DEFINED == 1)
  { configASSERT(sizeof(StaticEventGroup_t) == sizeof(EventGroup_t)); }
#endif
  EventBits = (EventGroup_t *)EventGroupBuffer;
  if (EventBits != NULL) {
    EventBits->EventBits = 0;
    EventBits->TasksWaitingForBits.init();
    EventBits->StaticallyAllocated = true;
  }
  return EventBits;
}

EventGroupHandle_t xEventGroupCreate(void) {
  EventGroup_t *EventBits;
  EventBits = (EventGroup_t *)pvPortMalloc(sizeof(EventGroup_t));
  if (EventBits != NULL) {
    EventBits->EventBits = 0;
    EventBits->TasksWaitingForBits.init();
#if (configSUPPORT_STATIC_ALLOCATION == 1)
    { EventBits->StaticallyAllocated = false; }
#endif
  }
  return EventBits;
}

EventBits_t xEventGroupSync(EventGroupHandle_t xEventGroup,
                            const EventBits_t uxBitsToSet,
                            const EventBits_t uxBitsToWaitFor,
                            TickType_t xTicksToWait) {
  EventBits_t uxOriginalBitValue, uxReturn;
  EventGroup_t *EventBits = xEventGroup;
  BaseType_t xAlreadyYielded;
  BaseType_t xTimeoutOccurred = false;
  configASSERT((uxBitsToWaitFor & EVENT_BITS_CONTROL_BYTES) == 0);
  configASSERT(uxBitsToWaitFor != 0);
#if ((INCLUDE_TaskGetSchedulerState == 1) || (configUSE_TIMERS == 1))
  {
    configASSERT(!((TaskGetSchedulerState() == taskSCHEDULER_SUSPENDED) &&
                   (xTicksToWait != 0)));
  }
#endif
  TaskSuspendAll();
  {
    uxOriginalBitValue = EventBits->EventBits;
    (void)xEventGroupSetBits(xEventGroup, uxBitsToSet);
    if (((uxOriginalBitValue | uxBitsToSet) & uxBitsToWaitFor) ==
        uxBitsToWaitFor) {
      uxReturn = (uxOriginalBitValue | uxBitsToSet);

      EventBits->EventBits &= ~uxBitsToWaitFor;
      xTicksToWait = 0;
    } else {
      if (xTicksToWait != (TickType_t)0) {
        TaskPlaceOnUnorderedEventList(
            &(EventBits->TasksWaitingForBits),
            (uxBitsToWaitFor | eventCLEAR_EVENTS_ON_EXIT_BIT |
             WAIT_FOR_ALL_BITS),
            xTicksToWait);

        uxReturn = 0;
      } else {
        uxReturn = EventBits->EventBits;
        xTimeoutOccurred = true;
      }
    }
  }
  xAlreadyYielded = TaskResumeAll();
  if (xTicksToWait != (TickType_t)0) {
    if (xAlreadyYielded == false) {
      taskYIELD_WITHIN_API();
    }

    uxReturn = TaskResetEventItemValue();
    if ((uxReturn & eventUNBLOCKED_DUE_TO_BIT_SET) == (EventBits_t)0) {
      ENTER_CRITICAL();
      {
        uxReturn = EventBits->EventBits;

        if ((uxReturn & uxBitsToWaitFor) == uxBitsToWaitFor) {
          EventBits->EventBits &= ~uxBitsToWaitFor;
        }
      }
      EXIT_CRITICAL();
      xTimeoutOccurred = true;
    }

    uxReturn &= ~EVENT_BITS_CONTROL_BYTES;
  }
  return uxReturn;
}

EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup,
                                const EventBits_t uxBitsToWaitFor,
                                const BaseType_t xClearOnExit,
                                const BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait) {
  EventGroup_t *EventBits = xEventGroup;
  EventBits_t uxReturn, uxControlBits = 0;
  BaseType_t xWaitConditionMet, xAlreadyYielded;
  BaseType_t xTimeoutOccurred = false;

  configASSERT(xEventGroup);
  configASSERT((uxBitsToWaitFor & EVENT_BITS_CONTROL_BYTES) == 0);
  configASSERT(uxBitsToWaitFor != 0);
#if ((INCLUDE_TaskGetSchedulerState == 1) || (configUSE_TIMERS == 1))
  {
    configASSERT(!((TaskGetSchedulerState() == taskSCHEDULER_SUSPENDED) &&
                   (xTicksToWait != 0)));
  }
#endif
  TaskSuspendAll();
  {
    const EventBits_t uxCurrentEventBits = EventBits->EventBits;

    xWaitConditionMet =
        TestWaitCondition(uxCurrentEventBits, uxBitsToWaitFor, xWaitForAllBits);
    if (xWaitConditionMet != false) {
      uxReturn = uxCurrentEventBits;
      xTicksToWait = (TickType_t)0;

      if (xClearOnExit != false) {
        EventBits->EventBits &= ~uxBitsToWaitFor;
      }
    } else if (xTicksToWait == (TickType_t)0) {
      uxReturn = uxCurrentEventBits;
      xTimeoutOccurred = true;
    } else {
      if (xClearOnExit != false) {
        uxControlBits |= eventCLEAR_EVENTS_ON_EXIT_BIT;
      }
      if (xWaitForAllBits != false) {
        uxControlBits |= WAIT_FOR_ALL_BITS;
      }

      TaskPlaceOnUnorderedEventList(&(EventBits->TasksWaitingForBits),
                                    (uxBitsToWaitFor | uxControlBits),
                                    xTicksToWait);

      uxReturn = 0;
    }
  }
  xAlreadyYielded = TaskResumeAll();
  if (xTicksToWait != (TickType_t)0) {
    if (xAlreadyYielded == false) {
      taskYIELD_WITHIN_API();
    }

    uxReturn = TaskResetEventItemValue();
    if ((uxReturn & eventUNBLOCKED_DUE_TO_BIT_SET) == (EventBits_t)0) {
      ENTER_CRITICAL();
      {
        uxReturn = EventBits->EventBits;

        if (TestWaitCondition(uxReturn, uxBitsToWaitFor, xWaitForAllBits) !=
            false) {
          if (xClearOnExit != false) {
            EventBits->EventBits &= ~uxBitsToWaitFor;
          }
        }
        xTimeoutOccurred = true;
      }
    }
    uxReturn &= ~EVENT_BITS_CONTROL_BYTES;
  }
  return uxReturn;
}

EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup,
                                 const EventBits_t uxBitsToClear) {
  EventGroup_t *EventBits = xEventGroup;
  EventBits_t uxReturn;

  configASSERT(xEventGroup);
  configASSERT((uxBitsToClear & EVENT_BITS_CONTROL_BYTES) == 0);
  ENTER_CRITICAL();
  {
    uxReturn = EventBits->EventBits;

    EventBits->EventBits &= ~uxBitsToClear;
  }
  EXIT_CRITICAL();
  return uxReturn;
}
EventBits_t xEventGroupGetBitsFromISR(EventGroupHandle_t xEventGroup) {
  UBaseType_t uxSavedInterruptStatus = ENTER_CRITICAL_FROM_ISR();
  EventBits_t uxReturn = xEventGroup->EventBits;
  EXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);
  return uxReturn;
}
EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup,
                               const EventBits_t uxBitsToSet) {
  Item_t<TCB_t> *ListItem;
  Item_t<TCB_t> *Next;
  EventBits_t uxBitsToClear = 0, uxBitsWaitedFor, uxControlBits, uxReturnBits;
  EventGroup_t *EventBits = xEventGroup;
  BaseType_t xMatchFound = false;

  configASSERT(xEventGroup);
  configASSERT((uxBitsToSet & EVENT_BITS_CONTROL_BYTES) == 0);
  List_t<TCB_t> *List = &(EventBits->TasksWaitingForBits);
  Item_t<TCB_t> const *ListEnd = &List->End;
  TaskSuspendAll();
  {
    ListItem = List->head();

    EventBits->EventBits |= uxBitsToSet;

    while (ListItem != ListEnd) {
      Next = ListItem->Next;
      uxBitsWaitedFor = ListItem->Value;
      xMatchFound = false;

      uxControlBits = uxBitsWaitedFor & EVENT_BITS_CONTROL_BYTES;
      uxBitsWaitedFor &= ~EVENT_BITS_CONTROL_BYTES;
      if ((uxControlBits & WAIT_FOR_ALL_BITS) == (EventBits_t)0) {
        if ((uxBitsWaitedFor & EventBits->EventBits) != (EventBits_t)0) {
          xMatchFound = true;
        }
      } else if ((uxBitsWaitedFor & EventBits->EventBits) ==
                 uxBitsWaitedFor) {
        xMatchFound = true;
      }
      if (!xMatchFound) {
        if ((uxControlBits & eventCLEAR_EVENTS_ON_EXIT_BIT) != (EventBits_t)0) {
          uxBitsToClear |= uxBitsWaitedFor;
        }

        TaskRemoveFromUnorderedEventList(
            ListItem, EventBits->EventBits | eventUNBLOCKED_DUE_TO_BIT_SET);
      }

      ListItem = Next;
    }

    EventBits->EventBits &= ~uxBitsToClear;

    uxReturnBits = EventBits->EventBits;
  }
  (void)TaskResumeAll();
  return uxReturnBits;
}

void vEventGroupDelete(EventGroupHandle_t xEventGroup) {
  EventGroup_t *EventBits = xEventGroup;
  configASSERT(EventBits);
  List_t<TCB_t> *TasksWaitingForBits = &(EventBits->TasksWaitingForBits);
  TaskSuspendAll();
  {
    while (TasksWaitingForBits->Length > (UBaseType_t)0) {
      TaskRemoveFromUnorderedEventList(TasksWaitingForBits->End.Next,
                                       eventUNBLOCKED_DUE_TO_BIT_SET);
    }
  }
  (void)TaskResumeAll();
#if ((configSUPPORT_DYNAMIC_ALLOCATION == 1) && \
     (configSUPPORT_STATIC_ALLOCATION == 0))
  { vPortFree(EventBits); }
#elif ((configSUPPORT_DYNAMIC_ALLOCATION == 1) && \
       (configSUPPORT_STATIC_ALLOCATION == 1))
  {
    if (EventBits->StaticallyAllocated == (uint8_t) false) {
      vPortFree(EventBits);
    }
  }
#endif
}

#if (configSUPPORT_STATIC_ALLOCATION == 1)
BaseType_t xEventGroupGetStaticBuffer(
    EventGroupHandle_t xEventGroup, StaticEventGroup_t **EventGroupBuffer) {
  BaseType_t xReturn;
  EventGroup_t *EventBits = xEventGroup;
  configASSERT(EventBits);
  configASSERT(EventGroupBuffer);

  if (EventBits->StaticallyAllocated == (uint8_t) true) {
    *EventGroupBuffer = (StaticEventGroup_t *)EventBits;
    xReturn = true;
  } else {
    xReturn = false;
  }
  return xReturn;
}
#endif

void vEventGroupSetBitsCallback(void *pvEventGroup, uint32_t ulBitsToSet) {
  (void)xEventGroupSetBits((EventGroupHandle_t)pvEventGroup,
                           (EventBits_t)ulBitsToSet);
}

void vEventGroupClearBitsCallback(void *pvEventGroup, uint32_t ulBitsToClear) {
  (void)xEventGroupClearBits((EventGroupHandle_t)pvEventGroup,
                             (EventBits_t)ulBitsToClear);
}

static BaseType_t TestWaitCondition(const EventBits_t uxCurrentEventBits,
                                    const EventBits_t uxBitsToWaitFor,
                                    const BaseType_t xWaitForAllBits) {
  BaseType_t xWaitConditionMet = false;
  if (xWaitForAllBits == false) {
    if ((uxCurrentEventBits & uxBitsToWaitFor) != (EventBits_t)0) {
      xWaitConditionMet = true;
    }
  } else {
    if ((uxCurrentEventBits & uxBitsToWaitFor) == uxBitsToWaitFor) {
      xWaitConditionMet = true;
    }
  }
  return xWaitConditionMet;
}
