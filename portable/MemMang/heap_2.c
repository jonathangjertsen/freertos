/*
 * FreeRTOS Kernel <DEVELOPMENT BRANCH>
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */
/*
 * A sample implementation of pvPortMalloc() and vPortFree() that permits
 * allocated blocks to be freed, but does not combine adjacent free blocks
 * into a single larger block (and so will fragment memory).  See heap_4.c for
 * an equivalent that does combine adjacent blocks into single larger blocks.
 *
 * See heap_1.c, heap_3.c and heap_4.c for alternative implementations, and the
 * memory management pages of https://www.FreeRTOS.org for more information.
 */
#include <stdlib.h>
#include <string.h>
/* Defining MPU_WRAPPERS_INCLUDED_FROM_API_FILE prevents task.h from redefining
 * all the API functions to use the MPU wrappers.  That should only be done when
 * task.h is included from an application file. */
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE
#include "FreeRTOS.h"
#include "task.h"
#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE
#if ( configSUPPORT_DYNAMIC_ALLOCATION == 0 )
    #error This file must not be used if configSUPPORT_DYNAMIC_ALLOCATION is 0
#endif
#ifndef configHEAP_CLEAR_MEMORY_ON_FREE
    #define configHEAP_CLEAR_MEMORY_ON_FREE    0
#endif
/* A few bytes might be lost to byte aligning the heap start address. */
#define configADJUSTED_HEAP_SIZE    ( configTOTAL_HEAP_SIZE - portBYTE_ALIGNMENT )
/* Assumes 8bit bytes! */
#define heapBITS_PER_BYTE           ( ( size_t ) 8 )
/* Max value that fits in a size_t type. */
#define heapSIZE_MAX                ( ~( ( size_t ) 0 ) )
/* Check if multiplying a and b will result in overflow. */
#define heapMULTIPLY_WILL_OVERFLOW( a, b )    ( ( ( a ) > 0 ) && ( ( b ) > ( heapSIZE_MAX / ( a ) ) ) )
/* Check if adding a and b will result in overflow. */
#define heapADD_WILL_OVERFLOW( a, b )         ( ( a ) > ( heapSIZE_MAX - ( b ) ) )
/* MSB of the xBlockSize member of an BlockLink_t structure is used to track
 * the allocation status of a block.  When MSB of the xBlockSize member of
 * an BlockLink_t structure is set then the block belongs to the application.
 * When the bit is free the block is still part of the free heap space. */
#define heapBLOCK_ALLOCATED_BITMASK    ( ( ( size_t ) 1 ) << ( ( sizeof( size_t ) * heapBITS_PER_BYTE ) - 1 ) )
#define heapBLOCK_SIZE_IS_VALID( xBlockSize )    ( ( ( xBlockSize ) & heapBLOCK_ALLOCATED_BITMASK ) == 0 )
#define heapBLOCK_IS_ALLOCATED( Block )        ( ( ( Block->xBlockSize ) & heapBLOCK_ALLOCATED_BITMASK ) != 0 )
#define heapALLOCATE_BLOCK( Block )            ( ( Block->xBlockSize ) |= heapBLOCK_ALLOCATED_BITMASK )
#define heapFREE_BLOCK( Block )                ( ( Block->xBlockSize ) &= ~heapBLOCK_ALLOCATED_BITMASK )

/* Allocate the memory for the heap. */
#if ( configAPPLICATION_ALLOCATED_HEAP == 1 )
/* The application writer has already defined the array used for the RTOS
 * heap - probably so it can be placed in a special segment or address. */
    extern uint8_t ucHeap[ configTOTAL_HEAP_SIZE ];
#else
     static uint8_t ucHeap[ configTOTAL_HEAP_SIZE ];
#endif /* configAPPLICATION_ALLOCATED_HEAP */

/* Define the linked list structure.  This is used to link free blocks in order
 * of their size. */
typedef struct A_BLOCK_LINK
{
    struct A_BLOCK_LINK * NextFreeBlock; /*<< The next free block in the list. */
    size_t xBlockSize;                     /*<< The size of the free block. */
} BlockLink_t;

static const size_t xHeapStructSize = ( ( sizeof( BlockLink_t ) + ( size_t ) ( portBYTE_ALIGNMENT - 1 ) ) & ~( ( size_t ) portBYTE_ALIGNMENT_MASK ) );
#define heapMINIMUM_BLOCK_SIZE    ( ( size_t ) ( xHeapStructSize * 2 ) )
/* Create a couple of list links to mark the start and end of the list. */
 static BlockLink_t xStart, xEnd;
/* Keeps track of the number of free bytes remaining, but says nothing about
 * fragmentation. */
 static size_t xFreeBytesRemaining = configADJUSTED_HEAP_SIZE;
/* Indicates whether the heap has been initialised or not. */
 static BaseType_t xHeapHasBeenInitialised = false;

/*
 * Initialises the heap structures before their first use.
 */
static void HeapInit( void ) ;

/* STATIC FUNCTIONS ARE DEFINED AS MACROS TO MINIMIZE THE FUNCTION CALL DEPTH. */
/*
 * Insert a block into the list of free blocks - which is ordered by size of
 * the block.  Small blocks at the start of the list and large blocks at the end
 * of the list.
 */
#define InsertBlockIntoFreeList( BlockToInsert )                                                                               \
    {                                                                                                                               \
        BlockLink_t * Iterator;                                                                                                   \
        size_t xBlockSize;                                                                                                          \
                                                                                                                                    \
        xBlockSize = BlockToInsert->xBlockSize;                                                                                   \
                                                                                                                                    \
        /* Iterate through the list until a block is found that has a larger size */                                                \
        /* than the block we are inserting. */                                                                                      \
        for( Iterator = &xStart; Iterator->NextFreeBlock->xBlockSize < xBlockSize; Iterator = Iterator->NextFreeBlock ) \
        {                                                                                                                           \
            /* There is nothing to do here - just iterate to the correct position. */                                               \
        }                                                                                                                           \
                                                                                                                                    \
        /* Update the list to include the block being inserted in the correct */                                                    \
        /* position. */                                                                                                             \
        BlockToInsert->NextFreeBlock = Iterator->NextFreeBlock;                                                             \
        Iterator->NextFreeBlock = BlockToInsert;                                                                              \
    }

void * pvPortMalloc( size_t xWantedSize )
{
    BlockLink_t * Block;
    BlockLink_t * PreviousBlock;
    BlockLink_t * NewBlockLink;
    void * pvReturn = NULL;
    size_t xAdditionalRequiredSize;
    size_t xAllocatedBlockSize = 0;
    if( xWantedSize > 0 )
    {
        /* The wanted size must be increased so it can contain a BlockLink_t
         * structure in addition to the requested amount of bytes. */
        if( heapADD_WILL_OVERFLOW( xWantedSize, xHeapStructSize ) == 0 )
        {
            xWantedSize += xHeapStructSize;
            /* Ensure that blocks are always aligned to the required number
             * of bytes. */
            if( ( xWantedSize & portBYTE_ALIGNMENT_MASK ) != 0x00 )
            {
                /* Byte alignment required. */
                xAdditionalRequiredSize = portBYTE_ALIGNMENT - ( xWantedSize & portBYTE_ALIGNMENT_MASK );
                if( heapADD_WILL_OVERFLOW( xWantedSize, xAdditionalRequiredSize ) == 0 )
                {
                    xWantedSize += xAdditionalRequiredSize;
                }
                else
                {
                    xWantedSize = 0;
                }
            }
            
        }
        else
        {
            xWantedSize = 0;
        }
    }
    TaskSuspendAll();
    {
        /* If this is the first call to malloc then the heap will require
         * initialisation to setup the list of free blocks. */
        if( xHeapHasBeenInitialised == false )
        {
            HeapInit();
            xHeapHasBeenInitialised = true;
        }
        /* Check the block size we are trying to allocate is not so large that the
         * top bit is set.  The top bit of the block size member of the BlockLink_t
         * structure is used to determine who owns the block - the application or
         * the kernel, so it must be free. */
        if( heapBLOCK_SIZE_IS_VALID( xWantedSize ) != 0 )
        {
            if( ( xWantedSize > 0 ) && ( xWantedSize <= xFreeBytesRemaining ) )
            {
                /* Blocks are stored in byte order - traverse the list from the start
                 * (smallest) block until one of adequate size is found. */
                PreviousBlock = &xStart;
                Block = xStart.NextFreeBlock;
                while( ( Block->xBlockSize < xWantedSize ) && ( Block->NextFreeBlock != NULL ) )
                {
                    PreviousBlock = Block;
                    Block = Block->NextFreeBlock;
                }
                /* If we found the end marker then a block of adequate size was not found. */
                if( Block != &xEnd )
                {
                    /* Return the memory space - jumping over the BlockLink_t structure
                     * at its start. */
                    pvReturn = ( void * ) ( ( ( uint8_t * ) PreviousBlock->NextFreeBlock ) + xHeapStructSize );
                    /* This block is being returned for use so must be taken out of the
                     * list of free blocks. */
                    PreviousBlock->NextFreeBlock = Block->NextFreeBlock;
                    /* If the block is larger than required it can be split into two. */
                    if( ( Block->xBlockSize - xWantedSize ) > heapMINIMUM_BLOCK_SIZE )
                    {
                        /* This block is to be split into two.  Create a new block
                         * following the number of bytes requested. The void cast is
                         * used to prevent byte alignment warnings from the compiler. */
                        NewBlockLink = ( void * ) ( ( ( uint8_t * ) Block ) + xWantedSize );
                        /* Calculate the sizes of two blocks split from the single
                         * block. */
                        NewBlockLink->xBlockSize = Block->xBlockSize - xWantedSize;
                        Block->xBlockSize = xWantedSize;
                        /* Insert the new block into the list of free blocks.
                         * The list of free blocks is sorted by their size, we have to
                         * iterate to find the right place to insert new block. */
                        InsertBlockIntoFreeList( ( NewBlockLink ) );
                    }
                    xFreeBytesRemaining -= Block->xBlockSize;
                    xAllocatedBlockSize = Block->xBlockSize;
                    /* The block is being returned - it is allocated and owned
                     * by the application and has no "next" block. */
                    heapALLOCATE_BLOCK( Block );
                    Block->NextFreeBlock = NULL;
                }
            }
        }
        traceMALLOC( pvReturn, xAllocatedBlockSize );
        /* Prevent compiler warnings when trace macros are not used. */
        ( void ) xAllocatedBlockSize;
    }
    ( void ) TaskResumeAll();
    #if ( configUSE_MALLOC_FAILED_HOOK == 1 )
    {
        if( pvReturn == NULL )
        {
            ApplicationMallocFailedHook();
        }
    }
    #endif
    return pvReturn;
}

void vPortFree( void * pv )
{
    uint8_t * puc = ( uint8_t * ) pv;
    BlockLink_t * Link;
    if( pv != NULL )
    {
        /* The memory being freed will have an BlockLink_t structure immediately
         * before it. */
        puc -= xHeapStructSize;
        /* This unexpected casting is to keep some compilers from issuing
         * byte alignment warnings. */
        Link = ( void * ) puc;
                        if( heapBLOCK_IS_ALLOCATED( Link ) != 0 )
        {
            if( Link->NextFreeBlock == NULL )
            {
                /* The block is being returned to the heap - it is no longer
                 * allocated. */
                heapFREE_BLOCK( Link );
                #if ( configHEAP_CLEAR_MEMORY_ON_FREE == 1 )
                {
                    ( void ) memset( puc + xHeapStructSize, 0, Link->xBlockSize - xHeapStructSize );
                }
                #endif
                TaskSuspendAll();
                {
                    /* Add this block to the list of free blocks. */
                    InsertBlockIntoFreeList( ( ( BlockLink_t * ) Link ) );
                    xFreeBytesRemaining += Link->xBlockSize;
                    traceFREE( pv, Link->xBlockSize );
                }
                ( void ) TaskResumeAll();
            }
        }
    }
}

size_t xPortGetFreeHeapSize( void )
{
    return xFreeBytesRemaining;
}

void vPortInitialiseBlocks( void )
{
    /* This just exists to keep the linker quiet. */
}

void * pvPortCalloc( size_t xNum,
                     size_t xSize )
{
    void * pv = NULL;
    if( heapMULTIPLY_WILL_OVERFLOW( xNum, xSize ) == 0 )
    {
        pv = pvPortMalloc( xNum * xSize );
        if( pv != NULL )
        {
            ( void ) memset( pv, 0, xNum * xSize );
        }
    }
    return pv;
}

static void HeapInit( void ) /*  */
{
    BlockLink_t * FirstFreeBlock;
    uint8_t * pucAlignedHeap;
    /* Ensure the heap starts on a correctly aligned boundary. */
    pucAlignedHeap = ( uint8_t * ) ( ( ( portPOINTER_SIZE_TYPE ) & ucHeap[ portBYTE_ALIGNMENT - 1 ] ) & ( ~( ( portPOINTER_SIZE_TYPE ) portBYTE_ALIGNMENT_MASK ) ) );
    /* xStart is used to hold a pointer to the first item in the list of free
     * blocks.  The void cast is used to prevent compiler warnings. */
    xStart.NextFreeBlock = ( void * ) pucAlignedHeap;
    xStart.xBlockSize = ( size_t ) 0;
    /* xEnd is used to mark the end of the list of free blocks. */
    xEnd.xBlockSize = configADJUSTED_HEAP_SIZE;
    xEnd.NextFreeBlock = NULL;
    /* To start with there is a single free block that is sized to take up the
     * entire heap space. */
    FirstFreeBlock = ( BlockLink_t * ) pucAlignedHeap;
    FirstFreeBlock->xBlockSize = configADJUSTED_HEAP_SIZE;
    FirstFreeBlock->NextFreeBlock = &xEnd;
}

/*
 * Reset the state in this file. This state is normally initialized at start up.
 * This function must be called by the application before restarting the
 * scheduler.
 */
void vPortHeapResetState( void )
{
    xFreeBytesRemaining = configADJUSTED_HEAP_SIZE;
    xHeapHasBeenInitialised = false;
}
