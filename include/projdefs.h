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

typedef void (*TaskFunction_t)(void* arg);

#ifndef pdMS_TO_TICKS
#define pdMS_TO_TICKS(xTimeInMs)                                         \
  ((TickType_t)(((uint64_t)(xTimeInMs) * (uint64_t)configTICK_RATE_HZ) / \
                (uint64_t)1000U))
#endif

#ifndef pdTICKS_TO_MS
#define pdTICKS_TO_MS(xTimeInTicks)                            \
  ((TickType_t)(((uint64_t)(xTimeInTicks) * (uint64_t)1000U) / \
                (uint64_t)configTICK_RATE_HZ))
#endif

#define errQUEUE_EMPTY ((BaseType_t)0)
#define errQUEUE_FULL ((BaseType_t)0)

#define errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY (-1)
#define errQUEUE_BLOCKED (-4)
#define errQUEUE_YIELD (-5)

#ifndef configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES
#define configUSE_LIST_DATA_INTEGRITY_CHECK_BYTES 0
#endif
#if (configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_16_BITS)
#define pdINTEGRITY_CHECK_VALUE 0x5a5a
#elif (configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_32_BITS)
#define pdINTEGRITY_CHECK_VALUE 0x5a5a5a5aUL
#elif (configTICK_TYPE_WIDTH_IN_BITS == TICK_TYPE_WIDTH_64_BITS)
#define pdINTEGRITY_CHECK_VALUE 0x5a5a5a5a5a5a5a5aULL
#else
#error configTICK_TYPE_WIDTH_IN_BITS set to unsupported tick type width.
#endif

#define pdFREERTOS_ERRNO_NONE 0
#define pdFREERTOS_ERRNO_ENOENT 2
#define pdFREERTOS_ERRNO_EINTR 4
#define pdFREERTOS_ERRNO_EIO 5
#define pdFREERTOS_ERRNO_ENXIO 6
#define pdFREERTOS_ERRNO_EBADF 9
#define pdFREERTOS_ERRNO_EAGAIN 11
#define pdFREERTOS_ERRNO_EWOULDBLOCK 11
#define pdFREERTOS_ERRNO_ENOMEM 12
#define pdFREERTOS_ERRNO_EACCES 13
#define pdFREERTOS_ERRNO_EFAULT 14
#define pdFREERTOS_ERRNO_EBUSY 16
#define pdFREERTOS_ERRNO_EEXIST 17
#define pdFREERTOS_ERRNO_EXDEV 18
#define pdFREERTOS_ERRNO_ENODEV 19
#define pdFREERTOS_ERRNO_ENOTDIR 20
#define pdFREERTOS_ERRNO_EISDIR 21
#define pdFREERTOS_ERRNO_EINVAL 22
#define pdFREERTOS_ERRNO_ENOSPC 28
#define pdFREERTOS_ERRNO_ESPIPE 29
#define pdFREERTOS_ERRNO_EROFS 30
#define pdFREERTOS_ERRNO_EUNATCH 42
#define pdFREERTOS_ERRNO_EBADE 50
#define pdFREERTOS_ERRNO_EFTYPE 79
#define pdFREERTOS_ERRNO_ENMFILE 89
#define pdFREERTOS_ERRNO_ENOTEMPTY 90
#define pdFREERTOS_ERRNO_ENAMETOOLONG 91
#define pdFREERTOS_ERRNO_EOPNOTSUPP 95
#define pdFREERTOS_ERRNO_EAFNOSUPPORT 97
#define pdFREERTOS_ERRNO_ENOBUFS 105
#define pdFREERTOS_ERRNO_ENOPROTOOPT 109
#define pdFREERTOS_ERRNO_EADDRINUSE 112
#define pdFREERTOS_ERRNO_ETIMEDOUT 116
#define pdFREERTOS_ERRNO_EINPROGRESS 119
#define pdFREERTOS_ERRNO_EALREADY 120
#define pdFREERTOS_ERRNO_EADDRNOTAVAIL 125
#define pdFREERTOS_ERRNO_EISCONN 127
#define pdFREERTOS_ERRNO_ENOTCONN 128
#define pdFREERTOS_ERRNO_ENOMEDIUM 135
#define pdFREERTOS_ERRNO_EILSEQ 138
#define pdFREERTOS_ERRNO_ECANCELED 140

#define pdFREERTOS_LITTLE_ENDIAN 0
#define pdFREERTOS_BIG_ENDIAN 1

#define pdLITTLE_ENDIAN pdFREERTOS_LITTLE_ENDIAN
#define pdBIG_ENDIAN pdFREERTOS_BIG_ENDIAN
