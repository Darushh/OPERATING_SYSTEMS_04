#include <unistd.h>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <sys/mman.h>

#define INITIAL_BLOCKS 32
#define MAX_ORDER_SIZE (128 * 1024) //128KB
#define ALLOCATION_SIZE (INITIAL_BLOCKS * MAX_ORDER_SIZE) //4MB
#define MAX_ORDER 10
#define MIN_ORDER_SIZE 128
#define MMAP_THRESHOLD (128 * 1024) //128KB

struct MallocMetadata {
    size_t size;    //size of block including METADATA
    bool is_free;   //if block is free or not
    bool is_mmaped; //we want to know wether the block was was allocated with mmap or regularly
    MallocMetadata* next;
    MallocMetadata* prev;
};


static constexpr size_t MAX_MALLOC = 100000000;


//array of double linked lists of free blocks sorted by address
//free_lists[i] has free blocks of order i
MallocMetadata* free_lists[11] = { nullptr };

MallocMetadata* mmap_head = nullptr; //head for mmap blocks
void* heap_start = nullptr; //global variable for the start of heap (aligned so we can use XOR)
bool is_initialized = false; //first time calling malloc - need to initialize buddy

bool initialize_buddy_allocator() {
    void* current_break = sbrk(0);
    if (current_break == (void*)-1) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(current_break);
    uintptr_t alignment = ALLOCATION_SIZE; // 4MB
    //check how much for aligment:
    uintptr_t offset = 0;
    if (addr % alignment != 0) offset = alignment - (addr % alignment);
    //allign:
    if (offset > 0) {
        if (sbrk(offset) == (void*)-1) return false;
    }
   //second call to sbrk for buddy_allocator
    heap_start = sbrk(ALLOCATION_SIZE);
    if (heap_start == (void*)-1) return false;

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

    return true;
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

//removing block from list:
void remove_from_free_list(MallocMetadata* block, int order) {
    if (free_lists[order] == block) {
        free_lists[order] = block->next;
    }
    if (block->prev != nullptr) {
        block->prev->next = block->next;
    }
    if (block->next != nullptr) {
        block->next->prev = block->prev;
    }
    block->next = nullptr;
    block->prev = nullptr;
}

//adding block to list:
void insert_into_free_list(MallocMetadata* block, int order) {
    block->is_free = true;
    block->next = nullptr;
    block->prev = nullptr;
    //if list is empty:
    if (free_lists[order] == nullptr) {
        free_lists[order] = block;
        return;
    }
    //block needs to be first in list (lowest address)
    if (block < free_lists[order]) {
        block->next = free_lists[order];
        free_lists[order]->prev = block;
        free_lists[order] = block;
        return;
    }
    //insert the block sorted!!
    MallocMetadata* curr = free_lists[order];
    while (curr->next != nullptr && curr->next < block) {
        curr = curr->next;
    }
    block->next = curr->next;
    block->prev = curr;
    if (curr->next != nullptr) {
        curr->next->prev = block;
    }
    curr->next = block;
}


//spliting for already allocated
void split_allocated_block(MallocMetadata* block, int current_order, int target_order) {
    while (current_order > target_order) {
        size_t block_size = static_cast<size_t>(MIN_ORDER_SIZE) << current_order;
        size_t half_size = block_size / 2;
        block->size = half_size;
        char* buddy_address = reinterpret_cast<char*>(block) + half_size;
        MallocMetadata* buddy = reinterpret_cast<MallocMetadata*>(buddy_address);
        buddy->size = half_size;
        buddy->is_free = true;
        buddy->is_mmaped = false;
        insert_into_free_list(buddy, current_order - 1);
        current_order--;
    }
}


//here we want to add splitting blocks by size - as requested (free block)
MallocMetadata* split_block(MallocMetadata* block, int current_order, int target_order) {
    while (current_order > target_order) {
       //removing the block from current order
        remove_from_free_list(block, current_order);
        //calculating half (spliting):
        size_t block_size = static_cast<size_t>(MIN_ORDER_SIZE) << current_order;
        size_t half_size = block_size / 2;
        //same address, size changes
        block->size = half_size;
        //second half - need to change address
        char* buddy_address = reinterpret_cast<char*>(block) + half_size;
        MallocMetadata* buddy_block = reinterpret_cast<MallocMetadata*>(buddy_address);
        buddy_block->size = half_size;
        buddy_block->is_free = true;
        buddy_block->is_mmaped = false;
        //entering to new order
        insert_into_free_list(buddy_block, current_order - 1);
        insert_into_free_list(block, current_order - 1);
        //repeating until getting to desired order
        current_order--;
    }
    
    //removing from free list
    remove_from_free_list(block, target_order);
    block->is_free = false;
    return block;
}


void* smalloc(size_t size)
{
    if (size == 0 || size > MAX_MALLOC) { return nullptr; }
    //if first time call - initialize
    if (!is_initialized) {
        if (!initialize_buddy_allocator()) {
            return nullptr;
        }
        is_initialized = true;
    }
    size_t aligned_size = align_8(size);
    //mmap threshold check uses the raw requested size, per spec ("size + _size_meta_data() > 128KB")
    size_t raw_total_size = size + sizeof(MallocMetadata);

    if (raw_total_size > MMAP_THRESHOLD) {
        //memory from OS:
        void* raw_address = mmap(NULL, raw_total_size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
        if (raw_address == MAP_FAILED) {
            return nullptr;
        }
        MallocMetadata* block = static_cast<MallocMetadata*>(raw_address);
        block->size = size; //store the raw requested size, not the 8-byte-aligned one
        block->is_free = false;
        block->is_mmaped = true;
        //inserting to mmap block list
        block->next = mmap_head;
        block->prev = nullptr;
        if (mmap_head != nullptr) {
            mmap_head->prev = block;
        }
        mmap_head = block;
        return static_cast<void*>(block + 1);
    }
    size_t total_size = aligned_size + sizeof(MallocMetadata);
    int target_order = get_order(total_size);
    int found_order = -1;
    for (int o = target_order; o <= 10; ++o) {
        if (free_lists[o] != nullptr) {
            found_order = o;
            break;
        }
    }
    if (found_order == -1) { return nullptr;} //no available blocks
    MallocMetadata* block = free_lists[found_order];
    if (found_order > target_order) { //spliting if needed
        block = split_block(block, found_order, target_order);
    } else {
        remove_from_free_list(block, target_order);
        block->is_free = false;
    }
    return static_cast<void*>(block + 1);
}

void sfree(void* p)
{
    if (p == nullptr) return;
    MallocMetadata* metadata = static_cast<MallocMetadata*>(p) - 1;
    if (metadata->is_free)  return; //already free
    //if allocated with mmap;
    if (metadata->is_mmaped) {
        if (metadata->prev != nullptr) {
            metadata->prev->next = metadata->next;
        } else {
            mmap_head = metadata->next;
        }
        if (metadata->next != nullptr) {
            metadata->next->prev = metadata->prev;
        }
        munmap(metadata, metadata->size + sizeof(MallocMetadata));
        return;
    }
    //merging:
    metadata->is_free = true;
    int order = get_order(metadata->size);
    MallocMetadata* curr = metadata;
    while (order < MAX_ORDER) { //merging until possible
        uintptr_t curr_addr = reinterpret_cast<uintptr_t>(curr);
        uintptr_t buddy_addr = curr_addr ^ curr->size;
        MallocMetadata* buddy = reinterpret_cast<MallocMetadata*>(buddy_addr);
        //conditions for merging:
        if (buddy->is_free && buddy->size == curr->size && !buddy->is_mmaped) {
            remove_from_free_list(buddy, order);
            if (buddy < curr) {curr = buddy;}
            curr->size *= 2;
            order++;
        } else { break;}
    }
    insert_into_free_list(curr, order);
}


void* scalloc(size_t num, size_t size)
{
    if (num == 0 || size == 0) return nullptr;

    if (num > MAX_MALLOC / size) return nullptr;
    size_t totalSize = num * size;
    void* ptr = smalloc(totalSize);

    if (ptr == nullptr)  return nullptr;

    std::memset(ptr, 0, totalSize);

    return ptr;
}


void* srealloc(void* oldp, size_t size)
{
    if (size == 0 || size > MAX_MALLOC) return nullptr;
    if (oldp == nullptr) return smalloc(size);
    MallocMetadata* oldMetadata = static_cast<MallocMetadata*>(oldp) - 1;
    size_t aligned_size = align_8(size);
    size_t total_size = aligned_size + sizeof(MallocMetadata);
    //handle mmap
    if (oldMetadata->is_mmaped) {
        if (size == oldMetadata->size) {
            return oldp;
        }
        void* newPtr = smalloc(size);
        if (newPtr == nullptr) {
            return nullptr;
        }
        size_t copy_size = oldMetadata->size < size ? oldMetadata->size : size;
        std::memmove(newPtr, oldp, copy_size);
        sfree(oldp);
        return newPtr;
    }
    //handle buddies:
    //(a) reuse the current block as-is when it already fits - no splitting/merging
    size_t old_usable_size = oldMetadata->size - sizeof(MallocMetadata);
    if (size <= old_usable_size) {
        return oldp;
    }
    int target_order = get_order(total_size);
    int old_order = get_order(oldMetadata->size);
    //maybe merging
    int order = old_order;
    MallocMetadata* curr = oldMetadata;
    bool can_merge = false;
    while (order < target_order) {
        uintptr_t curr_addr = reinterpret_cast<uintptr_t>(curr);
        uintptr_t buddy_addr = curr_addr ^ (static_cast<size_t>(MIN_ORDER_SIZE) << order);
        MallocMetadata* buddy = reinterpret_cast<MallocMetadata*>(buddy_addr);
        if (buddy->is_free && buddy->size == (static_cast<size_t>(MIN_ORDER_SIZE) << order) && !buddy->is_mmaped) {
            if (buddy < curr) {
                curr = buddy;
            }
            order++;
            if (order >= target_order) {
                can_merge = true;
                break;
            }
        } else {
            break;
        }
    }
    if (can_merge) {
        order = old_order;
        curr = oldMetadata;
        while (order < target_order) {
            uintptr_t curr_addr = reinterpret_cast<uintptr_t>(curr);
            uintptr_t buddy_addr = curr_addr ^ (static_cast<size_t>(MIN_ORDER_SIZE) << order);
            MallocMetadata* buddy = reinterpret_cast<MallocMetadata*>(buddy_addr);
            remove_from_free_list(buddy, order);
            if (buddy < curr) {
                curr = buddy;
            }
            curr->size *= 2;
            order++;
        }
        curr->is_free = false;
        //moving data if needed
        if (curr != oldMetadata) {
            std::memmove(static_cast<void*>(curr + 1), oldp, (static_cast<size_t>(MIN_ORDER_SIZE) << old_order) - sizeof(MallocMetadata));
        }
        //spliting if too big
        split_allocated_block(curr, order, target_order);
        return static_cast<void*>(curr + 1);
    }
    //new allocation - copy
    void* newPtr = smalloc(size);
    if (newPtr == nullptr) {
        return nullptr;
    }
    std::memmove(newPtr, oldp, oldMetadata->size - sizeof(MallocMetadata));
    sfree(oldp);
    return newPtr;
}


size_t _num_free_blocks()
{
    size_t count = 0;
    for (int i = 0; i <= MAX_ORDER; ++i) {
        MallocMetadata* current = free_lists[i];
        while (current != nullptr) {
            count++;
            current = current->next;
        }
    }
    return count;
}
size_t _num_free_bytes()
{
    size_t totalFreeBytes = 0;
    for (int i = 0; i <= MAX_ORDER; ++i) {
        MallocMetadata* current = free_lists[i];
        while (current != nullptr) {
            totalFreeBytes += (current->size - sizeof(MallocMetadata));
            current = current->next;
        }
    }
    return totalFreeBytes;
}


size_t _num_allocated_blocks()
{
    if (!is_initialized) return 0;
    size_t count = 0;
    char* current = static_cast<char*>(heap_start);
    char* heap_end = current + 32 * 128 * 1024; 
    while (current < heap_end) {
        MallocMetadata* block = reinterpret_cast<MallocMetadata*>(current);
        count++;
        current += block->size; 
    }
    //check mmap blocks:
    MallocMetadata* mmap_current = mmap_head;
    while (mmap_current != nullptr) {
        count++;
        mmap_current = mmap_current->next;
    }
    return count;
}
size_t _num_allocated_bytes()
{
    if (!is_initialized) return 0;
    size_t allocated_bytes = 0;
    char* current = static_cast<char*>(heap_start);
    char* heap_end = current + 32 * 128 * 1024;
    while (current < heap_end) {
        MallocMetadata* block = reinterpret_cast<MallocMetadata*>(current);
        allocated_bytes += (block->size - sizeof(MallocMetadata));
        current += block->size;
    }
    MallocMetadata* mmap_current = mmap_head;
    while (mmap_current != nullptr) {
        allocated_bytes += mmap_current->size;
        mmap_current = mmap_current->next;
    }
    return allocated_bytes;
}

size_t _num_meta_data_bytes(){ return _num_allocated_blocks() * sizeof(MallocMetadata);}


size_t _size_meta_data(){  return sizeof(MallocMetadata);}