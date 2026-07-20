#include <unistd.h>
#include <sys/mman.h>
#include <linux/mman.h>
#include <cstddef>
#include <cstring>
#include <cstdint>

static constexpr size_t INITIAL_BLOCKS = 32;
static constexpr size_t MIN_BLOCK_SIZE = 128;
static constexpr int MAX_ORDER = 10;
static constexpr size_t MAX_BLOCK_SIZE = MIN_BLOCK_SIZE << MAX_ORDER; // 128KB
static constexpr size_t BUDDY_POOL_SIZE = INITIAL_BLOCKS * MAX_BLOCK_SIZE; // 4MB
static constexpr size_t MMAP_THRESHOLD = MAX_BLOCK_SIZE;
static constexpr size_t MAX_MALLOC_SIZE = 100000000;

static constexpr size_t HUGE_PAGE_SIZE = 2UL * 1024UL * 1024UL;
static constexpr size_t SMALLOC_HUGE_THRESHOLD = 4UL * 1024UL * 1024UL;
static constexpr size_t SCALLOC_ELEMENT_HUGE_THRESHOLD = 2UL * 1024UL * 1024UL;

struct MallocMetadata {
    // Buddy block: total block size, including metadata.
    // mmap block: requested user size, excluding metadata.
    size_t size;

    // Actual mmap length. Zero for Buddy blocks.
    size_t mapped_size;

    bool is_free;
    bool is_mmaped;
    bool is_hugepage;

    MallocMetadata* next;
    MallocMetadata* prev;
};

static_assert(sizeof(MallocMetadata) <= 64,
              "MallocMetadata must not exceed 64 bytes");

static MallocMetadata* free_lists[MAX_ORDER + 1] = {nullptr};
static MallocMetadata* mmap_head = nullptr;
static void* heap_start = nullptr;
static bool is_initialized = false;

static size_t block_size_by_order(int order)
{
    return MIN_BLOCK_SIZE << order;
}

static uintptr_t address_of(const void* pointer)
{
    return reinterpret_cast<uintptr_t>(pointer);
}

static bool is_in_buddy_pool(const MallocMetadata* block)
{
    if (!is_initialized || heap_start == nullptr) {
        return false;
    }

    const uintptr_t address = address_of(block);
    const uintptr_t start = address_of(heap_start);
    return address >= start && address < start + BUDDY_POOL_SIZE;
}

static int get_order(size_t total_size)
{
    for (int order = 0; order <= MAX_ORDER; ++order) {
        if (total_size <= block_size_by_order(order)) {
            return order;
        }
    }
    return -1;
}

static size_t round_up_to_huge_page(size_t size)
{
    const size_t remainder = size % HUGE_PAGE_SIZE;
    return remainder == 0 ? size : size + (HUGE_PAGE_SIZE - remainder);
}

static void remove_from_free_list(MallocMetadata* block, int order)
{
    if (block->prev != nullptr) {
        block->prev->next = block->next;
    } else {
        free_lists[order] = block->next;
    }

    if (block->next != nullptr) {
        block->next->prev = block->prev;
    }

    block->next = nullptr;
    block->prev = nullptr;
}

static void insert_into_free_list(MallocMetadata* block, int order)
{
    block->is_free = true;
    block->is_mmaped = false;
    block->is_hugepage = false;
    block->mapped_size = 0;
    block->next = nullptr;
    block->prev = nullptr;

    MallocMetadata* current = free_lists[order];
    if (current == nullptr) {
        free_lists[order] = block;
        return;
    }

    if (address_of(block) < address_of(current)) {
        block->next = current;
        current->prev = block;
        free_lists[order] = block;
        return;
    }

    while (current->next != nullptr &&
           address_of(current->next) < address_of(block)) {
        current = current->next;
    }

    block->next = current->next;
    block->prev = current;

    if (current->next != nullptr) {
        current->next->prev = block;
    }
    current->next = block;
}

static bool initialize_buddy_allocator()
{
    if (is_initialized) {
        return true;
    }

    void* current_break = sbrk(0);
    if (current_break == reinterpret_cast<void*>(-1)) {
        return false;
    }

    const uintptr_t current_address = address_of(current_break);
    const size_t misalignment = current_address % BUDDY_POOL_SIZE;
    const size_t padding =
        misalignment == 0 ? 0 : BUDDY_POOL_SIZE - misalignment;

    if (padding != 0) {
        void* alignment_result = sbrk(static_cast<intptr_t>(padding));
        if (alignment_result == reinterpret_cast<void*>(-1)) {
            return false;
        }
    }

    void* pool = sbrk(static_cast<intptr_t>(BUDDY_POOL_SIZE));
    if (pool == reinterpret_cast<void*>(-1)) {
        return false;
    }

    heap_start = pool;

    for (size_t i = 0; i < INITIAL_BLOCKS; ++i) {
        char* block_address =
            static_cast<char*>(heap_start) + i * MAX_BLOCK_SIZE;
        MallocMetadata* block =
            reinterpret_cast<MallocMetadata*>(block_address);

        block->size = MAX_BLOCK_SIZE;
        block->mapped_size = 0;
        block->is_free = true;
        block->is_mmaped = false;
        block->is_hugepage = false;
        block->next = nullptr;
        block->prev = nullptr;

        // The blocks are created in ascending address order.
        if (free_lists[MAX_ORDER] == nullptr) {
            free_lists[MAX_ORDER] = block;
        } else {
            MallocMetadata* tail = free_lists[MAX_ORDER];
            while (tail->next != nullptr) {
                tail = tail->next;
            }
            tail->next = block;
            block->prev = tail;
        }
    }

    is_initialized = true;
    return true;
}

static MallocMetadata* split_block(MallocMetadata* block,
                                   int current_order,
                                   int target_order)
{
    remove_from_free_list(block, current_order);

    while (current_order > target_order) {
        const int next_order = current_order - 1;
        const size_t half_size = block_size_by_order(next_order);

        MallocMetadata* buddy = reinterpret_cast<MallocMetadata*>(
            reinterpret_cast<char*>(block) + half_size);

        block->size = half_size;
        block->mapped_size = 0;
        block->is_free = true;
        block->is_mmaped = false;
        block->is_hugepage = false;
        block->next = nullptr;
        block->prev = nullptr;

        buddy->size = half_size;
        buddy->mapped_size = 0;
        buddy->is_free = true;
        buddy->is_mmaped = false;
        buddy->is_hugepage = false;
        buddy->next = nullptr;
        buddy->prev = nullptr;

        // Continue splitting the lower-address half; keep the other half free.
        insert_into_free_list(buddy, next_order);
        current_order = next_order;
    }

    block->is_free = false;
    return block;
}

static void add_to_mmap_list(MallocMetadata* block)
{
    block->prev = nullptr;
    block->next = mmap_head;
    if (mmap_head != nullptr) {
        mmap_head->prev = block;
    }
    mmap_head = block;
}

static void remove_from_mmap_list(MallocMetadata* block)
{
    if (block->prev != nullptr) {
        block->prev->next = block->next;
    } else {
        mmap_head = block->next;
    }

    if (block->next != nullptr) {
        block->next->prev = block->prev;
    }
}

static void* allocate_mmap_block(size_t requested_size, bool use_hugepage)
{
    const size_t total_size = requested_size + sizeof(MallocMetadata);
    const size_t mapping_size =
        use_hugepage ? round_up_to_huge_page(total_size) : total_size;

    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
    if (use_hugepage) {
        flags |= MAP_HUGETLB;
        flags |= MAP_HUGE_2MB;
    }

    void* raw_address = mmap(nullptr,
                             mapping_size,
                             PROT_READ | PROT_WRITE,
                             flags,
                             -1,
                             0);
    if (raw_address == MAP_FAILED) {
        return nullptr;
    }

    MallocMetadata* block = static_cast<MallocMetadata*>(raw_address);
    block->size = requested_size;
    block->mapped_size = mapping_size;
    block->is_free = false;
    block->is_mmaped = true;
    block->is_hugepage = use_hugepage;
    block->next = nullptr;
    block->prev = nullptr;

    add_to_mmap_list(block);
    return static_cast<void*>(block + 1);
}

static void* allocate_internal(size_t size, bool use_hugepage)
{
    if (size == 0 || size > MAX_MALLOC_SIZE) {
        return nullptr;
    }

    if (!initialize_buddy_allocator()) {
        return nullptr;
    }

    const size_t total_size = size + sizeof(MallocMetadata);

    if (use_hugepage) {
        return allocate_mmap_block(size, true);
    }

    if (total_size > MMAP_THRESHOLD) {
        return allocate_mmap_block(size, false);
    }

    const int target_order = get_order(total_size);
    if (target_order < 0) {
        return nullptr;
    }

    int found_order = -1;
    for (int order = target_order; order <= MAX_ORDER; ++order) {
        if (free_lists[order] != nullptr) {
            found_order = order;
            break;
        }
    }

    if (found_order < 0) {
        return nullptr;
    }

    MallocMetadata* block = free_lists[found_order];
    if (found_order > target_order) {
        block = split_block(block, found_order, target_order);
    } else {
        remove_from_free_list(block, target_order);
        block->is_free = false;
        block->next = nullptr;
        block->prev = nullptr;
    }

    return static_cast<void*>(block + 1);
}

void* smalloc(size_t size)
{
    return allocate_internal(size, size >= SMALLOC_HUGE_THRESHOLD);
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

    if (metadata->is_mmaped) {
        remove_from_mmap_list(metadata);
        const size_t mapping_size = metadata->mapped_size;
        munmap(metadata, mapping_size);
        return;
    }

    metadata->is_free = true;
    MallocMetadata* current = metadata;
    int order = get_order(current->size);

    while (order >= 0 && order < MAX_ORDER) {
        const uintptr_t buddy_address =
            address_of(current) ^ block_size_by_order(order);
        MallocMetadata* buddy =
            reinterpret_cast<MallocMetadata*>(buddy_address);

        if (!is_in_buddy_pool(buddy) ||
            !buddy->is_free ||
            buddy->is_mmaped ||
            buddy->size != current->size) {
            break;
        }

        remove_from_free_list(buddy, order);

        if (address_of(buddy) < address_of(current)) {
            current = buddy;
        }

        current->size = block_size_by_order(order + 1);
        current->mapped_size = 0;
        current->is_free = true;
        current->is_mmaped = false;
        current->is_hugepage = false;
        current->next = nullptr;
        current->prev = nullptr;
        ++order;
    }

    insert_into_free_list(current, order);
}

void* scalloc(size_t num, size_t size)
{
    if (num == 0 || size == 0 || num > MAX_MALLOC_SIZE / size) {
        return nullptr;
    }

    const size_t total_size = num * size;
    const bool use_hugepage = size > SCALLOC_ELEMENT_HUGE_THRESHOLD;

    // Do not call smalloc here: scalloc has a different HugePage policy.
    void* pointer = allocate_internal(total_size, use_hugepage);
    if (pointer == nullptr) {
        return nullptr;
    }

    std::memset(pointer, 0, total_size);
    return pointer;
}

void* srealloc(void* oldp, size_t size)
{
    if (size == 0 || size > MAX_MALLOC_SIZE) {
        return nullptr;
    }

    if (oldp == nullptr) {
        return smalloc(size);
    }

    MallocMetadata* old_metadata =
        static_cast<MallocMetadata*>(oldp) - 1;

    if (old_metadata->is_mmaped) {
        if (size == old_metadata->size) {
            return oldp;
        }

        void* new_pointer = smalloc(size);
        if (new_pointer == nullptr) {
            return nullptr;
        }

        const size_t copy_size =
            old_metadata->size < size ? old_metadata->size : size;
        std::memmove(new_pointer, oldp, copy_size);
        sfree(oldp);
        return new_pointer;
    }

    const size_t old_block_size = old_metadata->size;
    const size_t old_usable_size = old_block_size - sizeof(MallocMetadata);

    // Highest priority: reuse the current block without merging.
    if (size <= old_usable_size) {
        return oldp;
    }

    const int old_order = get_order(old_block_size);
    const int target_order = get_order(size + sizeof(MallocMetadata));

    if (old_order < 0 || target_order < 0) {
        return nullptr;
    }

    // First check whether the required chain of buddy merges exists.
    MallocMetadata* candidate = old_metadata;
    int candidate_order = old_order;

    while (candidate_order < target_order) {
        const uintptr_t buddy_address =
            address_of(candidate) ^ block_size_by_order(candidate_order);
        MallocMetadata* buddy =
            reinterpret_cast<MallocMetadata*>(buddy_address);

        if (!is_in_buddy_pool(buddy) ||
            !buddy->is_free ||
            buddy->is_mmaped ||
            buddy->size != block_size_by_order(candidate_order)) {
            break;
        }

        if (address_of(buddy) < address_of(candidate)) {
            candidate = buddy;
        }
        ++candidate_order;
    }

    if (candidate_order >= target_order) {
        MallocMetadata* merged = old_metadata;
        int merged_order = old_order;

        while (merged_order < target_order) {
            const uintptr_t buddy_address =
                address_of(merged) ^ block_size_by_order(merged_order);
            MallocMetadata* buddy =
                reinterpret_cast<MallocMetadata*>(buddy_address);

            remove_from_free_list(buddy, merged_order);

            if (address_of(buddy) < address_of(merged)) {
                merged = buddy;
            }

            ++merged_order;
            merged->size = block_size_by_order(merged_order);
            merged->mapped_size = 0;
            merged->is_free = false;
            merged->is_mmaped = false;
            merged->is_hugepage = false;
            merged->next = nullptr;
            merged->prev = nullptr;
        }

        void* new_user_pointer = static_cast<void*>(merged + 1);
        if (merged != old_metadata) {
            std::memmove(new_user_pointer, oldp, old_usable_size);
        }
        return new_user_pointer;
    }

    // Could not grow in place: allocate elsewhere, then free the old block.
    void* new_pointer = smalloc(size);
    if (new_pointer == nullptr) {
        return nullptr;
    }

    std::memmove(new_pointer, oldp, old_usable_size);
    sfree(oldp);
    return new_pointer;
}

size_t _num_free_blocks()
{
    size_t count = 0;
    for (int order = 0; order <= MAX_ORDER; ++order) {
        for (MallocMetadata* current = free_lists[order];
             current != nullptr;
             current = current->next) {
            ++count;
        }
    }
    return count;
}

size_t _num_free_bytes()
{
    size_t total = 0;
    for (int order = 0; order <= MAX_ORDER; ++order) {
        for (MallocMetadata* current = free_lists[order];
             current != nullptr;
             current = current->next) {
            total += current->size - sizeof(MallocMetadata);
        }
    }
    return total;
}

size_t _num_allocated_blocks()
{
    size_t count = 0;

    if (is_initialized) {
        char* current = static_cast<char*>(heap_start);
        char* heap_end = current + BUDDY_POOL_SIZE;

        while (current < heap_end) {
            MallocMetadata* block =
                reinterpret_cast<MallocMetadata*>(current);
            ++count;
            current += block->size;
        }
    }

    for (MallocMetadata* current = mmap_head;
         current != nullptr;
         current = current->next) {
        ++count;
    }

    return count;
}

size_t _num_allocated_bytes()
{
    size_t total = 0;

    if (is_initialized) {
        char* current = static_cast<char*>(heap_start);
        char* heap_end = current + BUDDY_POOL_SIZE;

        while (current < heap_end) {
            MallocMetadata* block =
                reinterpret_cast<MallocMetadata*>(current);
            total += block->size - sizeof(MallocMetadata);
            current += block->size;
        }
    }

    for (MallocMetadata* current = mmap_head;
         current != nullptr;
         current = current->next) {
        total += current->size;
    }

    return total;
}

size_t _num_meta_data_bytes()
{
    return _num_allocated_blocks() * sizeof(MallocMetadata);
}

size_t _size_meta_data()
{
    return sizeof(MallocMetadata);
}