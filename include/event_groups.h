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
#include "timers.h"

#if (configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_16_BITS)
#define eventCLEAR_EVENTS_ON_EXIT_BIT ((uint16_t)0x0100U)
#define eventUNBLOCKED_DUE_TO_BIT_SET ((uint16_t)0x0200U)
#define WAIT_FOR_ALL_BITS ((uint16_t)0x0400U)
#define EVENT_BITS_CONTROL_BYTES ((uint16_t)0xff00U)
#elif (configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_32_BITS)
#define eventCLEAR_EVENTS_ON_EXIT_BIT ((uint32_t)0x01000000U)
#define eventUNBLOCKED_DUE_TO_BIT_SET ((uint32_t)0x02000000U)
#define WAIT_FOR_ALL_BITS ((uint32_t)0x04000000U)
#define EVENT_BITS_CONTROL_BYTES ((uint32_t)0xff000000U)
#elif (configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_64_BITS)
#define eventCLEAR_EVENTS_ON_EXIT_BIT ((uint64_t)0x0100000000000000U)
#define eventUNBLOCKED_DUE_TO_BIT_SET ((uint64_t)0x0200000000000000U)
#define WAIT_FOR_ALL_BITS ((uint64_t)0x0400000000000000U)
#define EVENT_BITS_CONTROL_BYTES ((uint64_t)0xff00000000000000U)
#endif

struct EventGroup_t;
typedef struct EventGroup_t *EventGroupHandle_t;

typedef TickType_t EventBits_t;

EventGroupHandle_t xEventGroupCreate(void);
EventGroupHandle_t xEventGroupCreateStatic(StaticEventGroup_t *EventGroupBuffer);

EventBits_t xEventGroupWaitBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToWaitFor,
                                const BaseType_t xClearOnExit, const BaseType_t xWaitForAllBits,
                                TickType_t xTicksToWait);

EventBits_t xEventGroupClearBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToClear);

#define xEventGroupClearBitsFromISR(xEventGroup, uxBitsToClear) \
  TimerPendFunctionCallFromISR(vEventGroupClearBitsCallback, (void *)(xEventGroup), (uint32_t)(uxBitsToClear), NULL)

EventBits_t xEventGroupSetBits(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet);

#define xEventGroupSetBitsFromISR(xEventGroup, uxBitsToSet, HigherPriorityTaskWoken)                       \
  TimerPendFunctionCallFromISR(vEventGroupSetBitsCallback, (void *)(xEventGroup), (uint32_t)(uxBitsToSet), \
                               (HigherPriorityTaskWoken))

EventBits_t xEventGroupSync(EventGroupHandle_t xEventGroup, const EventBits_t uxBitsToSet,
                            const EventBits_t uxBitsToWaitFor, TickType_t xTicksToWait);

#define xEventGroupGetBits(xEventGroup) xEventGroupClearBits((xEventGroup), 0)

EventBits_t xEventGroupGetBitsFromISR(EventGroupHandle_t xEventGroup);

void vEventGroupDelete(EventGroupHandle_t xEventGroup);

BaseType_t xEventGroupGetStaticBuffer(EventGroupHandle_t xEventGroup, StaticEventGroup_t **EventGroupBuffer);

void vEventGroupSetBitsCallback(void *pvEventGroup, uint32_t ulBitsToSet);
void vEventGroupClearBitsCallback(void *pvEventGroup, uint32_t ulBitsToClear);
