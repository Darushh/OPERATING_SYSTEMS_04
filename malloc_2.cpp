#include <unistd.h>
#include <cstddef>
#include <cstring>
#include <cstdint>

struct MallocMetadata {
    size_t size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
};

static MallocMetadata* head = nullptr;
static MallocMetadata* tail = nullptr;

static constexpr size_t MAX_MALLOC = 100000000;

// internal function to find a free block of memory that is large enough to satisfy the requested size.
static MallocMetadata* findFreeBlock(size_t size)
{
    MallocMetadata* current = head;

    while (current != nullptr) {
        if (current->is_free && current->size >= size) {
            return current;
        }

        current = current->next;
    }

    return nullptr;
}

// internal function to allocate a new block of memory and add it to the linked list of blocks.
static MallocMetadata* allocateBlock(size_t size)
{
    size_t totalSize = sizeof(MallocMetadata) + size;

    void* rawAddress = sbrk(static_cast<intptr_t>(totalSize));

    if (rawAddress == reinterpret_cast<void*>(-1)) 
    {
        return nullptr;
    }

    MallocMetadata* newBlock = static_cast<MallocMetadata*>(rawAddress);

    newBlock->size = size;
    newBlock->is_free = false;
    newBlock->next = nullptr;
    newBlock->prev = tail;

    if (tail != nullptr) {
        tail->next = newBlock;
    } else {
        head = newBlock;
    }

    tail = newBlock;
    return newBlock;
}

void* smalloc(size_t size)
{
    if (size == 0 || size > MAX_MALLOC) {
        return nullptr;
    }

    MallocMetadata* metadata = findFreeBlock(size);

    if (metadata != nullptr) {
        metadata->is_free = false;
        return static_cast<void*>(metadata + 1);
    }

    metadata = allocateBlock(size);

    if (metadata == nullptr) {
        return nullptr;
    }
    return static_cast<void*>(metadata + 1);
}

void sfree(void* p)
{
    if (p == nullptr) {
        return;
    }

    MallocMetadata* metadata = static_cast<MallocMetadata*>(p) - 1;

    if (metadata->is_free) {
        return;
    }

    metadata->is_free = true;
}


void* scalloc(size_t num, size_t size)
{
    if (num == 0 || size == 0) {
        return nullptr;
    }

    if (num > MAX_MALLOC / size) {
        return nullptr;
    }

    size_t totalSize = num * size;

    void* ptr = smalloc(totalSize);

    if (ptr == nullptr) {
        return nullptr;
    }

    std::memset(ptr, 0, totalSize);

    return ptr;
}


void* srealloc(void* oldp, size_t size)
{
    if (size == 0 || size > MAX_MALLOC) {
        return nullptr;
    }

    if (oldp == nullptr) {
        return smalloc(size);
    }

    MallocMetadata* oldMetadata =
        static_cast<MallocMetadata*>(oldp) - 1;

    if (size <= oldMetadata->size) {
        return oldp;
    }

    void* newPtr = smalloc(size);

    if (newPtr == nullptr) {
        return nullptr;
    }

    std::memmove(newPtr, oldp, oldMetadata->size);

    sfree(oldp);

    return newPtr;
}


size_t _num_free_blocks()
{
    size_t count = 0;
    MallocMetadata* current = head;

    while (current != nullptr) {
        if (current->is_free) {
            count++;
        }

        current = current->next;
    }

    return count;
}


size_t _num_free_bytes()
{
    size_t totalFreeBytes = 0;
    MallocMetadata* current = head;

    while (current != nullptr) {
        if (current->is_free) {
            totalFreeBytes += current->size;
        }

        current = current->next;
    }

    return totalFreeBytes;
}


size_t _num_allocated_blocks()
{
    size_t count = 0;
    MallocMetadata* current = head;

    while (current != nullptr) {
        count++;
        current = current->next;
    }

    return count;
}


size_t _num_allocated_bytes()
{
    size_t totalAllocatedBytes = 0;
    MallocMetadata* current = head;

    while (current != nullptr) {
        totalAllocatedBytes += current->size;
        current = current->next;
    }

    return totalAllocatedBytes;
}


size_t _num_meta_data_bytes()
{
    return _num_allocated_blocks() * sizeof(MallocMetadata);
}


size_t _size_meta_data()
{
    return sizeof(MallocMetadata);
}