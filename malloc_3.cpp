#include <unistd.h>
#include <cstddef>
#include <cstring>
#include <cstdint>

#define INITIAL_BLOCKS 32
#define MAX_ORDER_SIZE (128 * 1024) // 128KB
#define ALLOCATION_SIZE (INITIAL_BLOCKS * MAX_ORDER_SIZE) // 4MB
#define MAX_ORDER 10
#define MIN_ORDER_SIZE 128
struct MallocMetadata {
    size_t size;    //size of block including METADATA
    bool is_free;   //if block is free or not
    bool is_mmaped; //we want to know wether the block was was allocated with mmap or regularly
    MallocMetadata* next;
    MallocMetadata* prev;
};

static MallocMetadata* head = nullptr;
static MallocMetadata* tail = nullptr;

static constexpr size_t MAX_MALLOC = 100000000;

//array of doubly linked lists of free blocks sorted by address
//free_lists[i] has free blocks of order i
extern MallocMetadata* free_lists[11];

void* heap_start = nullptr; //global variable for the start of heap (aligned so we can use XOR)
bool is_initialized = false; //first time calling malloc - need to initialize buddy

void initialize_buddy_allocator() {
    void* current_break = sbrk(0);
    if (current_break == (void*)-1) return; 
    uintptr_t addr = reinterpret_cast<uintptr_t>(current_break);
    uintptr_t alignment = ALLOCATION_SIZE; // 4MB
    //check how much for aligment:
    uintptr_t offset = 0;
    if (addr % alignment != 0) offset = alignment - (addr % alignment);
    //allign:
    if (offset > 0) sbrk(offset);
   //second call to sbrk for buddy_allocator
    heap_start = sbrk(ALLOCATION_SIZE);
    if (heap_start == (void*)-1) return; 

    //initializing blocks (MAX size at first):
    MallocMetadata* prev_block = nullptr;
    for (int i = 0; i < INITIAL_BLOCKS; ++i) {
        //phisical adress:
        uintptr_t block_address = reinterpret_cast<uintptr_t>(heap_start) + (i * MAX_ORDER_SIZE);
        MallocMetadata* current_block = reinterpret_cast<MallocMetadata*>(block_address);

        current_block->size = MAX_ORDER_SIZE; 
        current_block->is_free = true;
        current_block->is_mmaped = false;
        
        current_block->prev = prev_block;
        current_block->next = nullptr;
        if (prev_block != nullptr) {
            prev_block->next = current_block;
        } else {
            //first block in list
            free_lists[10] = current_block;
        }
        prev_block = current_block;
    }

}




//function for data aligment
size_t align_8(size_t size) {
    return (size + 7) & ~7;
}

//function to get the right order for the block
int get_order(size_t total_size) {
    size_t block_size = MIN_ORDER_SIZE; //min size in level 0
    int order = 0;
    while (block_size < total_size && order < MAX_ORDER) {
        block_size *= 2;
        order++;
    }
    return order;
}

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
    if (rawAddress == reinterpret_cast<void*>(-1))  return nullptr;
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

//here we want to add splitting blocks by size - as requested



void* smalloc(size_t size)
{
    if (size == 0 || size > MAX_MALLOC) { return nullptr; }
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