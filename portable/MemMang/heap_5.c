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
 * A sample implementation of pvPortMalloc() that allows the heap to be defined
 * across multiple non-contiguous blocks and combines (coalescences) adjacent
 * memory blocks as they are freed.
 *
 * See heap_1.c, heap_2.c, heap_3.c and heap_4.c for alternative
 * implementations, and the memory management pages of https://www.FreeRTOS.org
 * for more information.
 *
 * Usage notes:
 *
 * vPortDefineHeapRegions() ***must*** be called before pvPortMalloc().
 * pvPortMalloc() will be called if any task objects (tasks, queues, event
 * groups, etc.) are created, therefore vPortDefineHeapRegions() ***must*** be
 * called before any other objects are defined.
 *
 * vPortDefineHeapRegions() takes a single parameter.  The parameter is an array
 * of HeapRegion_t structures.  HeapRegion_t is defined in portable.h as
 *
 * typedef struct HeapRegion
 * {
 *  uint8_t *pucStartAddress; << Start address of a block of memory that will be part of the heap.
 *  size_t xSizeInBytes;      << Size of the block of memory.
 * } HeapRegion_t;
 *
 * The array is terminated using a NULL zero sized region definition, and the
 * memory regions defined in the array ***must*** appear in address order from
 * low address to high address.  So the following is a valid example of how
 * to use the function.
 *
 * HeapRegion_t xHeapRegions[] =
 * {
 *  { ( uint8_t * ) 0x80000000UL, 0x10000 }, << Defines a block of 0x10000 bytes starting at address 0x80000000
 *  { ( uint8_t * ) 0x90000000UL, 0xa0000 }, << Defines a block of 0xa0000 bytes starting at address of 0x90000000
 *  { NULL, 0 }                << Terminates the array.
 * };
 *
 * vPortDefineHeapRegions( xHeapRegions ); << Pass the array into vPortDefineHeapRegions().
 *
 * Note 0x80000000 is the lower address so appears in the array first.
 *
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
/* Block sizes must not get too small. */
#define heapMINIMUM_BLOCK_SIZE    ( ( size_t ) ( xHeapStructSize << 1 ) )
/* Assumes 8bit bytes! */
#define heapBITS_PER_BYTE         ( ( size_t ) 8 )
/* Max value that fits in a size_t type. */
#define heapSIZE_MAX              ( ~( ( size_t ) 0 ) )
/* Check if multiplying a and b will result in overflow. */
#define heapMULTIPLY_WILL_OVERFLOW( a, b )     ( ( ( a ) > 0 ) && ( ( b ) > ( heapSIZE_MAX / ( a ) ) ) )
/* Check if adding a and b will result in overflow. */
#define heapADD_WILL_OVERFLOW( a, b )          ( ( a ) > ( heapSIZE_MAX - ( b ) ) )
/* Check if the subtraction operation ( a - b ) will result in underflow. */
#define heapSUBTRACT_WILL_UNDERFLOW( a, b )    ( ( a ) < ( b ) )
/* MSB of the xBlockSize member of an BlockLink_t structure is used to track
 * the allocation status of a block.  When MSB of the xBlockSize member of
 * an BlockLink_t structure is set then the block belongs to the application.
 * When the bit is free the block is still part of the free heap space. */
#define heapBLOCK_ALLOCATED_BITMASK    ( ( ( size_t ) 1 ) << ( ( sizeof( size_t ) * heapBITS_PER_BYTE ) - 1 ) )
#define heapBLOCK_SIZE_IS_VALID( xBlockSize )    ( ( ( xBlockSize ) & heapBLOCK_ALLOCATED_BITMASK ) == 0 )
#define heapBLOCK_IS_ALLOCATED( Block )        ( ( ( Block->xBlockSize ) & heapBLOCK_ALLOCATED_BITMASK ) != 0 )
#define heapALLOCATE_BLOCK( Block )            ( ( Block->xBlockSize ) |= heapBLOCK_ALLOCATED_BITMASK )
#define heapFREE_BLOCK( Block )                ( ( Block->xBlockSize ) &= ~heapBLOCK_ALLOCATED_BITMASK )
/* Setting configENABLE_HEAP_PROTECTOR to 1 enables heap block pointers
 * protection using an application supplied canary value to catch heap
 * corruption should a heap buffer overflow occur.
 */
#if ( configENABLE_HEAP_PROTECTOR == 1 )
/* Macro to load/store BlockLink_t pointers to memory. By XORing the
 * pointers with a random canary value, heap overflows will result
 * in randomly unpredictable pointer values which will be caught by
 * heapVALIDATE_BLOCK_POINTER assert. */
    #define heapPROTECT_BLOCK_POINTER( Block )    ( ( BlockLink_t * ) ( ( ( portPOINTER_SIZE_TYPE ) ( Block ) ) ^ xHeapCanary ) )
/* Assert that a heap block pointer is within the heap bounds.
 * Setting configVALIDATE_HEAP_BLOCK_POINTER to 1 enables customized heap block pointers
 * protection on heap_5. */
    #ifndef configVALIDATE_HEAP_BLOCK_POINTER
        #define heapVALIDATE_BLOCK_POINTER( Block )                           \
            configASSERT( ( pucHeapHighAddress != NULL ) &&                     \
                          ( pucHeapLowAddress != NULL ) &&                      \
                          ( ( uint8_t * ) ( Block ) >= pucHeapLowAddress ) && \
                          ( ( uint8_t * ) ( Block ) < pucHeapHighAddress ) )
    #else /* ifndef configVALIDATE_HEAP_BLOCK_POINTER */
        #define heapVALIDATE_BLOCK_POINTER( Block )                           \
            configVALIDATE_HEAP_BLOCK_POINTER( Block )
    #endif /* configVALIDATE_HEAP_BLOCK_POINTER */
#else /* if ( configENABLE_HEAP_PROTECTOR == 1 ) */
    #define heapPROTECT_BLOCK_POINTER( Block )    ( Block )
    #define heapVALIDATE_BLOCK_POINTER( Block )
#endif /* configENABLE_HEAP_PROTECTOR */

/* Define the linked list structure.  This is used to link free blocks in order
 * of their memory address. */
typedef struct A_BLOCK_LINK
{
    struct A_BLOCK_LINK * NextFreeBlock; /**< The next free block in the list. */
    size_t xBlockSize;                     /**< The size of the free block. */
} BlockLink_t;

/*
 * Inserts a block of memory that is being freed into the correct position in
 * the list of free memory blocks.  The block being freed will be merged with
 * the block in front it and/or the block behind it if the memory blocks are
 * adjacent to each other.
 */
static void InsertBlockIntoFreeList( BlockLink_t * BlockToInsert ) ;
void vPortDefineHeapRegions( const HeapRegion_t * const HeapRegions ) ;
#if ( configENABLE_HEAP_PROTECTOR == 1 )
/**
 * @brief Application provided function to get a random value to be used as canary.
 *
 * @param HeapCanary [out] Output parameter to return the canary value.
 */
    extern void ApplicationGetRandomHeapCanary( portPOINTER_SIZE_TYPE * HeapCanary );
#endif /* configENABLE_HEAP_PROTECTOR */

/* The size of the structure placed at the beginning of each allocated memory
 * block must by correctly byte aligned. */
static const size_t xHeapStructSize = ( sizeof( BlockLink_t ) + ( ( size_t ) ( portBYTE_ALIGNMENT - 1 ) ) ) & ~( ( size_t ) portBYTE_ALIGNMENT_MASK );
/* Create a couple of list links to mark the start and end of the list. */
 static BlockLink_t xStart;
 static BlockLink_t * End = NULL;
/* Keeps track of the number of calls to allocate and free memory as well as the
 * number of free bytes remaining, but says nothing about fragmentation. */
 static size_t xFreeBytesRemaining = ( size_t ) 0U;
 static size_t xMinimumEverFreeBytesRemaining = ( size_t ) 0U;
 static size_t xNumberOfSuccessfulAllocations = ( size_t ) 0U;
 static size_t xNumberOfSuccessfulFrees = ( size_t ) 0U;
#if ( configENABLE_HEAP_PROTECTOR == 1 )
/* Canary value for protecting internal heap pointers. */
     static portPOINTER_SIZE_TYPE xHeapCanary;
/* Highest and lowest heap addresses used for heap block bounds checking. */
     static uint8_t * pucHeapHighAddress = NULL;
     static uint8_t * pucHeapLowAddress = NULL;
#endif /* configENABLE_HEAP_PROTECTOR */

void * pvPortMalloc( size_t xWantedSize )
{
    BlockLink_t * Block;
    BlockLink_t * PreviousBlock;
    BlockLink_t * NewBlockLink;
    void * pvReturn = NULL;
    size_t xAdditionalRequiredSize;
    size_t xAllocatedBlockSize = 0;
    /* The heap must be initialised before the first call to
     * pvPortMalloc(). */
    configASSERT( End );
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
        /* Check the block size we are trying to allocate is not so large that the
         * top bit is set.  The top bit of the block size member of the BlockLink_t
         * structure is used to determine who owns the block - the application or
         * the kernel, so it must be free. */
        if( heapBLOCK_SIZE_IS_VALID( xWantedSize ) != 0 )
        {
            if( ( xWantedSize > 0 ) && ( xWantedSize <= xFreeBytesRemaining ) )
            {
                /* Traverse the list from the start (lowest address) block until
                 * one of adequate size is found. */
                PreviousBlock = &xStart;
                Block = heapPROTECT_BLOCK_POINTER( xStart.NextFreeBlock );
                heapVALIDATE_BLOCK_POINTER( Block );
                while( ( Block->xBlockSize < xWantedSize ) && ( Block->NextFreeBlock != heapPROTECT_BLOCK_POINTER( NULL ) ) )
                {
                    PreviousBlock = Block;
                    Block = heapPROTECT_BLOCK_POINTER( Block->NextFreeBlock );
                    heapVALIDATE_BLOCK_POINTER( Block );
                }
                /* If the end marker was reached then a block of adequate size
                 * was not found. */
                if( Block != End )
                {
                    /* Return the memory space pointed to - jumping over the
                     * BlockLink_t structure at its start. */
                    pvReturn = ( void * ) ( ( ( uint8_t * ) heapPROTECT_BLOCK_POINTER( PreviousBlock->NextFreeBlock ) ) + xHeapStructSize );
                    heapVALIDATE_BLOCK_POINTER( pvReturn );
                    /* This block is being returned for use so must be taken out
                     * of the list of free blocks. */
                    PreviousBlock->NextFreeBlock = Block->NextFreeBlock;
                    /* If the block is larger than required it can be split into
                     * two. */
                    configASSERT( heapSUBTRACT_WILL_UNDERFLOW( Block->xBlockSize, xWantedSize ) == 0 );
                    if( ( Block->xBlockSize - xWantedSize ) > heapMINIMUM_BLOCK_SIZE )
                    {
                        /* This block is to be split into two.  Create a new
                         * block following the number of bytes requested. The void
                         * cast is used to prevent byte alignment warnings from the
                         * compiler. */
                        NewBlockLink = ( void * ) ( ( ( uint8_t * ) Block ) + xWantedSize );
                        configASSERT( ( ( ( size_t ) NewBlockLink ) & portBYTE_ALIGNMENT_MASK ) == 0 );
                        /* Calculate the sizes of two blocks split from the
                         * single block. */
                        NewBlockLink->xBlockSize = Block->xBlockSize - xWantedSize;
                        Block->xBlockSize = xWantedSize;
                        /* Insert the new block into the list of free blocks. */
                        NewBlockLink->NextFreeBlock = PreviousBlock->NextFreeBlock;
                        PreviousBlock->NextFreeBlock = heapPROTECT_BLOCK_POINTER( NewBlockLink );
                    }
                    xFreeBytesRemaining -= Block->xBlockSize;
                    if( xFreeBytesRemaining < xMinimumEverFreeBytesRemaining )
                    {
                        xMinimumEverFreeBytesRemaining = xFreeBytesRemaining;
                    }
                    xAllocatedBlockSize = Block->xBlockSize;
                    /* The block is being returned - it is allocated and owned
                     * by the application and has no "next" block. */
                    heapALLOCATE_BLOCK( Block );
                    Block->NextFreeBlock = heapPROTECT_BLOCK_POINTER( NULL );
                    xNumberOfSuccessfulAllocations++;
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
    #endif /* if ( configUSE_MALLOC_FAILED_HOOK == 1 ) */
    configASSERT( ( ( ( size_t ) pvReturn ) & ( size_t ) portBYTE_ALIGNMENT_MASK ) == 0 );
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
        /* This casting is to keep the compiler from issuing warnings. */
        Link = ( void * ) puc;
        heapVALIDATE_BLOCK_POINTER( Link );
        configASSERT( heapBLOCK_IS_ALLOCATED( Link ) != 0 );
        configASSERT( Link->NextFreeBlock == heapPROTECT_BLOCK_POINTER( NULL ) );
        if( heapBLOCK_IS_ALLOCATED( Link ) != 0 )
        {
            if( Link->NextFreeBlock == heapPROTECT_BLOCK_POINTER( NULL ) )
            {
                /* The block is being returned to the heap - it is no longer
                 * allocated. */
                heapFREE_BLOCK( Link );
                #if ( configHEAP_CLEAR_MEMORY_ON_FREE == 1 )
                {
                    /* Check for underflow as this can occur if xBlockSize is
                     * overwritten in a heap block. */
                    if( heapSUBTRACT_WILL_UNDERFLOW( Link->xBlockSize, xHeapStructSize ) == 0 )
                    {
                        ( void ) memset( puc + xHeapStructSize, 0, Link->xBlockSize - xHeapStructSize );
                    }
                }
                #endif
                TaskSuspendAll();
                {
                    /* Add this block to the list of free blocks. */
                    xFreeBytesRemaining += Link->xBlockSize;
                    traceFREE( pv, Link->xBlockSize );
                    InsertBlockIntoFreeList( ( ( BlockLink_t * ) Link ) );
                    xNumberOfSuccessfulFrees++;
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

size_t xPortGetMinimumEverFreeHeapSize( void )
{
    return xMinimumEverFreeBytesRemaining;
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

static void InsertBlockIntoFreeList( BlockLink_t * BlockToInsert ) /*  */
{
    BlockLink_t * Iterator;
    uint8_t * puc;
    /* Iterate through the list until a block is found that has a higher address
     * than the block being inserted. */
    for( Iterator = &xStart; heapPROTECT_BLOCK_POINTER( Iterator->NextFreeBlock ) < BlockToInsert; Iterator = heapPROTECT_BLOCK_POINTER( Iterator->NextFreeBlock ) )
    {
        /* Nothing to do here, just iterate to the right position. */
    }
    if( Iterator != &xStart )
    {
        heapVALIDATE_BLOCK_POINTER( Iterator );
    }
    /* Do the block being inserted, and the block it is being inserted after
     * make a contiguous block of memory? */
    puc = ( uint8_t * ) Iterator;
    if( ( puc + Iterator->xBlockSize ) == ( uint8_t * ) BlockToInsert )
    {
        Iterator->xBlockSize += BlockToInsert->xBlockSize;
        BlockToInsert = Iterator;
    }
    /* Do the block being inserted, and the block it is being inserted before
     * make a contiguous block of memory? */
    puc = ( uint8_t * ) BlockToInsert;
    if( ( puc + BlockToInsert->xBlockSize ) == ( uint8_t * ) heapPROTECT_BLOCK_POINTER( Iterator->NextFreeBlock ) )
    {
        if( heapPROTECT_BLOCK_POINTER( Iterator->NextFreeBlock ) != End )
        {
            /* Form one big block from the two blocks. */
            BlockToInsert->xBlockSize += heapPROTECT_BLOCK_POINTER( Iterator->NextFreeBlock )->xBlockSize;
            BlockToInsert->NextFreeBlock = heapPROTECT_BLOCK_POINTER( Iterator->NextFreeBlock )->NextFreeBlock;
        }
        else
        {
            BlockToInsert->NextFreeBlock = heapPROTECT_BLOCK_POINTER( End );
        }
    }
    else
    {
        BlockToInsert->NextFreeBlock = Iterator->NextFreeBlock;
    }
    /* If the block being inserted plugged a gap, so was merged with the block
     * before and the block after, then it's NextFreeBlock pointer will have
     * already been set, and should not be set here as that would make it point
     * to itself. */
    if( Iterator != BlockToInsert )
    {
        Iterator->NextFreeBlock = heapPROTECT_BLOCK_POINTER( BlockToInsert );
    }
}

void vPortDefineHeapRegions( const HeapRegion_t * const HeapRegions ) /*  */
{
    BlockLink_t * FirstFreeBlockInRegion = NULL;
    BlockLink_t * PreviousFreeBlock;
    portPOINTER_SIZE_TYPE xAlignedHeap;
    size_t xTotalRegionSize, xTotalHeapSize = 0;
    BaseType_t xDefinedRegions = 0;
    portPOINTER_SIZE_TYPE xAddress;
    const HeapRegion_t * HeapRegion;
    /* Can only call once! */
    configASSERT( End == NULL );
    #if ( configENABLE_HEAP_PROTECTOR == 1 )
    {
        ApplicationGetRandomHeapCanary( &( xHeapCanary ) );
    }
    #endif
    HeapRegion = &( HeapRegions[ xDefinedRegions ] );
    while( HeapRegion->xSizeInBytes > 0 )
    {
        xTotalRegionSize = HeapRegion->xSizeInBytes;
        /* Ensure the heap region starts on a correctly aligned boundary. */
        xAddress = ( portPOINTER_SIZE_TYPE ) HeapRegion->pucStartAddress;
        if( ( xAddress & portBYTE_ALIGNMENT_MASK ) != 0 )
        {
            xAddress += ( portBYTE_ALIGNMENT - 1 );
            xAddress &= ~( portPOINTER_SIZE_TYPE ) portBYTE_ALIGNMENT_MASK;
            /* Adjust the size for the bytes lost to alignment. */
            xTotalRegionSize -= ( size_t ) ( xAddress - ( portPOINTER_SIZE_TYPE ) HeapRegion->pucStartAddress );
        }
        xAlignedHeap = xAddress;
        /* Set xStart if it has not already been set. */
        if( xDefinedRegions == 0 )
        {
            /* xStart is used to hold a pointer to the first item in the list of
             *  free blocks.  The void cast is used to prevent compiler warnings. */
            xStart.NextFreeBlock = ( BlockLink_t * ) heapPROTECT_BLOCK_POINTER( xAlignedHeap );
            xStart.xBlockSize = ( size_t ) 0;
        }
        else
        {
            /* Should only get here if one region has already been added to the
             * heap. */
            configASSERT( End != heapPROTECT_BLOCK_POINTER( NULL ) );
            /* Check blocks are passed in with increasing start addresses. */
            configASSERT( ( size_t ) xAddress > ( size_t ) End );
        }
        #if ( configENABLE_HEAP_PROTECTOR == 1 )
        {
            if( ( pucHeapLowAddress == NULL ) ||
                ( ( uint8_t * ) xAlignedHeap < pucHeapLowAddress ) )
            {
                pucHeapLowAddress = ( uint8_t * ) xAlignedHeap;
            }
        }
        #endif /* configENABLE_HEAP_PROTECTOR */
        /* Remember the location of the end marker in the previous region, if
         * any. */
        PreviousFreeBlock = End;
        /* End is used to mark the end of the list of free blocks and is
         * inserted at the end of the region space. */
        xAddress = xAlignedHeap + ( portPOINTER_SIZE_TYPE ) xTotalRegionSize;
        xAddress -= ( portPOINTER_SIZE_TYPE ) xHeapStructSize;
        xAddress &= ~( ( portPOINTER_SIZE_TYPE ) portBYTE_ALIGNMENT_MASK );
        End = ( BlockLink_t * ) xAddress;
        End->xBlockSize = 0;
        End->NextFreeBlock = heapPROTECT_BLOCK_POINTER( NULL );
        /* To start with there is a single free block in this region that is
         * sized to take up the entire heap region minus the space taken by the
         * free block structure. */
        FirstFreeBlockInRegion = ( BlockLink_t * ) xAlignedHeap;
        FirstFreeBlockInRegion->xBlockSize = ( size_t ) ( xAddress - ( portPOINTER_SIZE_TYPE ) FirstFreeBlockInRegion );
        FirstFreeBlockInRegion->NextFreeBlock = heapPROTECT_BLOCK_POINTER( End );
        /* If this is not the first region that makes up the entire heap space
         * then link the previous region to this region. */
        if( PreviousFreeBlock != NULL )
        {
            PreviousFreeBlock->NextFreeBlock = heapPROTECT_BLOCK_POINTER( FirstFreeBlockInRegion );
        }
        xTotalHeapSize += FirstFreeBlockInRegion->xBlockSize;
        #if ( configENABLE_HEAP_PROTECTOR == 1 )
        {
            if( ( pucHeapHighAddress == NULL ) ||
                ( ( ( ( uint8_t * ) FirstFreeBlockInRegion ) + FirstFreeBlockInRegion->xBlockSize ) > pucHeapHighAddress ) )
            {
                pucHeapHighAddress = ( ( uint8_t * ) FirstFreeBlockInRegion ) + FirstFreeBlockInRegion->xBlockSize;
            }
        }
        #endif
        /* Move onto the next HeapRegion_t structure. */
        xDefinedRegions++;
        HeapRegion = &( HeapRegions[ xDefinedRegions ] );
    }
    xMinimumEverFreeBytesRemaining = xTotalHeapSize;
    xFreeBytesRemaining = xTotalHeapSize;
    /* Check something was actually defined before it is accessed. */
    configASSERT( xTotalHeapSize );
}

void vPortGetHeapStats( HeapStats_t * HeapStats )
{
    BlockLink_t * Block;
    size_t xBlocks = 0, xMaxSize = 0, xMinSize = portMAX_DELAY; /* portMAX_DELAY used as a portable way of getting the maximum value. */
    TaskSuspendAll();
    {
        Block = heapPROTECT_BLOCK_POINTER( xStart.NextFreeBlock );
        /* Block will be NULL if the heap has not been initialised.  The heap
         * is initialised automatically when the first allocation is made. */
        if( Block != NULL )
        {
            while( Block != End )
            {
                /* Increment the number of blocks and record the largest block seen
                 * so far. */
                xBlocks++;
                if( Block->xBlockSize > xMaxSize )
                {
                    xMaxSize = Block->xBlockSize;
                }
                /* Heap five will have a zero sized block at the end of each
                 * each region - the block is only used to link to the next
                 * heap region so it not a real block. */
                if( Block->xBlockSize != 0 )
                {
                    if( Block->xBlockSize < xMinSize )
                    {
                        xMinSize = Block->xBlockSize;
                    }
                }
                /* Move to the next block in the chain until the last block is
                 * reached. */
                Block = heapPROTECT_BLOCK_POINTER( Block->NextFreeBlock );
            }
        }
    }
    ( void ) TaskResumeAll();
    HeapStats->xSizeOfLargestFreeBlockInBytes = xMaxSize;
    HeapStats->xSizeOfSmallestFreeBlockInBytes = xMinSize;
    HeapStats->xNumberOfFreeBlocks = xBlocks;
    ENTER_CRITICAL();
    {
        HeapStats->xAvailableHeapSpaceInBytes = xFreeBytesRemaining;
        HeapStats->xNumberOfSuccessfulAllocations = xNumberOfSuccessfulAllocations;
        HeapStats->xNumberOfSuccessfulFrees = xNumberOfSuccessfulFrees;
        HeapStats->xMinimumEverFreeBytesRemaining = xMinimumEverFreeBytesRemaining;
    }
    EXIT_CRITICAL();
}

/*
 * Reset the state in this file. This state is normally initialized at start up.
 * This function must be called by the application before restarting the
 * scheduler.
 */
void vPortHeapResetState( void )
{
    End = NULL;
    xFreeBytesRemaining = ( size_t ) 0U;
    xMinimumEverFreeBytesRemaining = ( size_t ) 0U;
    xNumberOfSuccessfulAllocations = ( size_t ) 0U;
    xNumberOfSuccessfulFrees = ( size_t ) 0U;
    #if ( configENABLE_HEAP_PROTECTOR == 1 )
        pucHeapHighAddress = NULL;
        pucHeapLowAddress = NULL;
    #endif /* #if ( configENABLE_HEAP_PROTECTOR == 1 ) */
}
