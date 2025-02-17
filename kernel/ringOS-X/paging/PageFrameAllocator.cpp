#include "PageFrameAllocator.h"
#include <stddef.h>
#include "bitmap.h"
#include "../BasicRenderer.hpp"

PageFrameAllocator *GlobalAllocator;

void PageFrameAllocator::InitBitmap(size_t bitmapSize, void *bufferAddress)
{
    PageBitMap.Size = bitmapSize;
    PageBitMap.Buffer = (uint8_t *)bufferAddress;

    for (uint64_t i = 0; i < bitmapSize; i++)
    {
        *(uint8_t *)((uint64_t)PageBitMap.Buffer + i) = 0;
    }

    pageBitmapIndex = 0;
}

void PageFrameAllocator::ReservePage(void *address)
{
    uint64_t index = (uint64_t)address / 4096;

    if (PageBitMap[index])
    {
        return;
    }

    PageBitMap.Set(index, true);
    freeMemory -= 4096;
    reservedMemory += 4096;
}

void PageFrameAllocator::UnreservePage(void *address)
{
    uint64_t index = (uint64_t)address / 4096;

    if (!PageBitMap[index])
    {
        return;
    }

    if (!PageBitMap.Set(index, false))
    {
        return;
    }

    if (pageBitmapIndex > index)
    {
        pageBitmapIndex = index;
    }

    freeMemory += 4096;
    reservedMemory -= 4096;
}

void PageFrameAllocator::ReservePages(void *address, uint64_t pageCount)
{
    for (int i = 0; i < pageCount; i++)
    {
        ReservePage((void *)((uint64_t)address + (i * 4096)));
    }
}

void PageFrameAllocator::UnreservePages(void *address, uint64_t pageCount)
{
    for (int i = 0; i < pageCount; i++)
    {
        UnreservePage((void *)((uint64_t)address + (i * 4096)));
    }
}

uint64_t PageFrameAllocator::GetFreeRAM()
{
    return freeMemory;
}

uint64_t PageFrameAllocator::GetUsedRAM()
{
    return usedMemory;
}

uint64_t PageFrameAllocator::GetReservedRAM()
{
    return reservedMemory;
}

uint64_t PageFrameAllocator::GetFreePageCount()
{
    uint64_t count = 0;
    for (uint64_t index = 0; index < PageBitMap.Size * 8; index++)
    {
        if (!PageBitMap[index])
        {
            count++;
        }
    }

    return count;
}

void PageFrameAllocator::ReadEFIMemoryMap(void *start, uint64_t size)
{
    if (Initialized)
    {
        return;
    }

    Initialized = true;

    void *largestFreeMemSeg = start;
    size_t largestFreeMemSegSize = size;

    uint64_t memorySize = size;
    freeMemory = memorySize;
    reservedMemory = 0;
    usedMemory = 0;
    uint64_t bitmapSize = (memorySize / 4096 / 8) + 1;

    InitBitmap(bitmapSize, largestFreeMemSeg);

    ReservePages(0, 0x100);
    LockPages(PageBitMap.Buffer, bitmapSize / 4096 + 1);
}

int reqCount = 0;

void *PageFrameAllocator::RequestPage()
{
    reqCount++;
    for (; pageBitmapIndex < PageBitMap.Size * 8; pageBitmapIndex++)
    {
        if (PageBitMap[pageBitmapIndex])
        {
            continue;

        }

        LockPage((void *)(pageBitmapIndex * 4096));
        return (void *)(pageBitmapIndex * 4096);
    }

    for (pageBitmapIndex = 0; pageBitmapIndex < PageBitMap.Size * 8; pageBitmapIndex++)
    {
        if (PageBitMap[pageBitmapIndex])
        {
            continue;
        }

        LockPage((void *)(pageBitmapIndex * 4096));
        return (void *)(pageBitmapIndex * 4096);
    }

    GlobalRenderer->Clear(Colors.red, true);
    GlobalRenderer->Print("ERROR: NO MORE RAM AVAIABLE!");
    asm("hlt");

    return NULL; // Page Frame Swap to file
}

void PageFrameAllocator::FreePage(void *address)
{
    uint64_t index = (uint64_t)address / 4096;

    if (!PageBitMap[index])
    {
        return;
    }

    if (!PageBitMap.Set(index, false))
    {
        return;
    }


    if (pageBitmapIndex > index)
    {
        pageBitmapIndex = index;
    }

    freeMemory += 4096;
    usedMemory -= 4096;
}

void PageFrameAllocator::LockPage(void *address)
{
    uint64_t index = (uint64_t)address / 4096;

    if (PageBitMap[index])
    {
        return;
    }

    if (!PageBitMap.Set(index, true))
    {
        return;
    }

    freeMemory -= 4096;
    usedMemory += 4096;
}

void PageFrameAllocator::FreePages(void *address, uint64_t pageCount)
{
    for (int i = 0; i < pageCount; i++)
    {
        FreePage((void *)((uint64_t)address + (i * 4096)));
    }
}

void PageFrameAllocator::LockPages(void *address, uint64_t pageCount)
{
    for (int i = 0; i < pageCount; i++)
    {
        LockPage((void *)((uint64_t)address + (i * 4096)));
    }
}
