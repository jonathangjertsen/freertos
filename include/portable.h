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
#include "projdefs.h"
#ifndef portENTER_CRITICAL
#include "portmacro.h"
#endif
#if portBYTE_ALIGNMENT == 32
#define portBYTE_ALIGNMENT_MASK (0x001f)
#elif portBYTE_ALIGNMENT == 16
#define portBYTE_ALIGNMENT_MASK (0x000f)
#elif portBYTE_ALIGNMENT == 8
#define portBYTE_ALIGNMENT_MASK (0x0007)
#elif portBYTE_ALIGNMENT == 4
#define portBYTE_ALIGNMENT_MASK (0x0003)
#elif portBYTE_ALIGNMENT == 2
#define portBYTE_ALIGNMENT_MASK (0x0001)
#elif portBYTE_ALIGNMENT == 1
#define portBYTE_ALIGNMENT_MASK (0x0000)
#else
#error "Invalid portBYTE_ALIGNMENT definition"
#endif
#ifndef portUSING_MPU_WRAPPERS
#define portUSING_MPU_WRAPPERS 0
#endif
#ifndef portNUM_CONFIGURABLE_REGIONS
#define portNUM_CONFIGURABLE_REGIONS 1
#endif
#ifndef portHAS_STACK_OVERFLOW_CHECKING
#define portHAS_STACK_OVERFLOW_CHECKING 0
#endif
#ifndef portARCH_NAME
#define portARCH_NAME NULL
#endif
#ifndef configSTACK_DEPTH_TYPE
#define configSTACK_DEPTH_TYPE StackType_t
#endif
#ifndef configSTACK_ALLOCATION_FROM_SEPARATE_HEAP

#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP 0
#endif

StackType_t* PortInitialiseStack(StackType_t* StackTop, TaskFunction_t Code,
                                   void* Params);

typedef struct HeapRegion {
  uint8_t* pucStartAddress;
  size_t xSizeInBytes;
} HeapRegion_t;

typedef struct xHeapStats {
  size_t xAvailableHeapSpaceInBytes;
  size_t xSizeOfLargestFreeBlockInBytes;
  size_t xSizeOfSmallestFreeBlockInBytes;
  size_t xNumberOfFreeBlocks;
  size_t xMinimumEverFreeBytesRemaining;
  size_t xNumberOfSuccessfulAllocations;
  size_t xNumberOfSuccessfulFrees;
} HeapStats_t;

void vPortDefineHeapRegions(const HeapRegion_t* const HeapRegions);
void vPortGetHeapStats(HeapStats_t* HeapStats);
void* pvPortMalloc(size_t xWantedSize);
void* pvPortCalloc(size_t xNum, size_t xSize);
void vPortFree(void* pv);
void vPortInitialiseBlocks(void);
size_t xPortGetFreeHeapSize(void);
size_t xPortGetMinimumEverFreeHeapSize(void);
#define pvPortMallocStack pvPortMalloc
#define vPortFreeStack vPortFree
void vPortHeapResetState(void);
BaseType_t xPortStartScheduler(void);
void vPortEndScheduler(void);
