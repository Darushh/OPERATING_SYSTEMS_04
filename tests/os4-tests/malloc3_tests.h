#ifndef OS_MALLOC3_TESTS_H
#define OS_MALLOC3_TESTS_H

/*
 * The part 3 (buddy allocator) test bodies.
 *
 * This header is included by test_malloc_3.cpp *and* by test_malloc_4.cpp:
 * part 4 only changes how allocations of 4MB and up are backed, so every rule
 * tested here must still hold for malloc_4.cpp.
 *
 * Notation used throughout:
 *   M      = _size_meta_data()
 *   U(o)   = usable_size(o) = (128 << o) - M, the bytes of an order-o block that
 *            the statistics count (metadata excluded)
 *   the heap = the 32 blocks of 128KB created on the first allocation
 *
 * Handy identity, used a lot below: after a single allocation served by a block
 * of order o (starting from a fresh heap), the heap holds
 *      42 - o  blocks, of which 41 - o are free,
 * because splitting one 128KB block down to order o leaves exactly one free
 * block of each order o..9 behind.
 */

#include <string.h>

#include "buddy_model.h"
#include "proc_utils.h"
#include "stats_utils.h"
#include "test_utils.h"

using namespace ostest;

/* free bytes after a single allocation served by an order-o block */
static size_t free_bytes_after_single(int o) {
    size_t bytes = (NUM_INITIAL_BLOCKS - 1) * usable_size(BUDDY_MAX_ORDER);
    for (int k = o; k < BUDDY_MAX_ORDER; ++k) bytes += usable_size(k);
    return bytes;
}

/* the full expected statistics after a single allocation of order o */
#define OS_ASSERT_SINGLE_ALLOCATION(o)                                                   \
    OS_ASSERT_STATS(41 - (o), free_bytes_after_single(o), 42 - (o),                      \
                    free_bytes_after_single(o) + usable_size(o))

/* ------------------------------------------------------------------ */
/* the metadata and the initial heap                                   */
/* ------------------------------------------------------------------ */

OS_TEST(size_meta_data_is_at_most_64_bytes) {
    size_t m = _size_meta_data();
    OS_ASSERT_MSG(m > 0, "_size_meta_data() returned 0");
    OS_ASSERT_MSG(m <= 64,
                  "_size_meta_data() is %llu; the spec requires a metadata struct of at most 64 "
                  "bytes",
                  (unsigned long long)m);
    OS_ASSERT_MSG(m < MIN_BLOCK_SIZE,
                  "_size_meta_data() is %llu, so nothing would fit in a 128 byte block",
                  (unsigned long long)m);
    OS_ASSERT_EQ(_size_meta_data(), m);
}

OS_TEST(statistics_are_zero_before_the_first_allocation) {
    /* the 32 blocks are created on the first allocation, not before */
    OS_ASSERT_STATS(0, 0, 0, 0);
}

OS_TEST(the_first_allocation_creates_32_blocks_of_128KB) {
    /* a request that fills a whole 128KB block: no splitting, so the only thing
     * this can show is the initial heap */
    void* p = smalloc(max_heap_request());
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_STATS(31, 31 * usable_size(BUDDY_MAX_ORDER), 32,
                    32 * usable_size(BUDDY_MAX_ORDER));

    sfree(p);
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(the_heap_is_4MB_and_never_grows) {
    void* p = smalloc(100);
    OS_ASSERT_NOT_NULL(p);
    /* every byte of the region is either usable or metadata */
    OS_ASSERT_MSG(_num_allocated_bytes() + _num_meta_data_bytes() == HEAP_TOTAL_BYTES,
                  "_num_allocated_bytes() (%llu) + _num_meta_data_bytes() (%llu) should be exactly "
                  "32 * 128KB = %llu",
                  (unsigned long long)_num_allocated_bytes(),
                  (unsigned long long)_num_meta_data_bytes(),
                  (unsigned long long)HEAP_TOTAL_BYTES);

    void* before = sbrk(0);
    for (int i = 0; i < 100; ++i) OS_ASSERT_NOT_NULL(smalloc(64));
    void* after = sbrk(0);
    OS_ASSERT_MSG(before == after,
                  "the program break moved from %p to %p: after the initial 32 blocks the buddy "
                  "allocator must never call sbrk() again",
                  before, after);
    OS_ASSERT_MSG(_num_allocated_bytes() + _num_meta_data_bytes() == HEAP_TOTAL_BYTES,
                  "the heap is no longer exactly 4MB after 100 allocations");
}

OS_TEST(the_region_is_aligned_for_the_xor_buddy_trick) {
    char* p = (char*)smalloc(1);
    OS_ASSERT_NOT_NULL(p);
    uintptr_t base = (uintptr_t)(p - meta_size());
    if (base % (NUM_INITIAL_BLOCKS * MAX_BLOCK_SIZE) != 0)
        warn("the 4MB region starts at %p, which is not a multiple of 32*128KB. That is allowed, "
             "but the recommended 'buddy = address XOR size' trick only works when it is aligned.",
             (void*)base);
}

OS_TEST(returned_pointers_are_8_byte_aligned) {
    size_t m = meta_size();
    if (m % 8 != 0) {
        warn("_size_meta_data() is %llu, which is not a multiple of 8, so the pointers you hand "
             "out cannot be 8 byte aligned",
             (unsigned long long)m);
        return;
    }
    void* p1 = smalloc(10);
    void* p2 = smalloc(1);
    void* p3 = smalloc(1000);
    void* p4 = smalloc(200 * 1024); /* mmap()ed */
    OS_ASSERT_EQ((uintptr_t)p1 % 8, 0);
    OS_ASSERT_EQ((uintptr_t)p2 % 8, 0);
    OS_ASSERT_EQ((uintptr_t)p3 % 8, 0);
    OS_ASSERT_EQ((uintptr_t)p4 % 8, 0);
}

/* ------------------------------------------------------------------ */
/* challenge 1 - splitting                                             */
/* ------------------------------------------------------------------ */

OS_TEST(a_small_request_is_split_down_to_the_smallest_block) {
    void* p = smalloc(1);
    OS_ASSERT_NOT_NULL(p);
    /* one 128KB block was split all the way down to 128 bytes, leaving one free
     * block of each order 0..9 behind: 31 + 1 + 10 = 42 blocks */
    OS_ASSERT_SINGLE_ALLOCATION(0);
}

OS_TEST(every_order_is_served_by_a_block_of_exactly_that_order) {
    /* U(o) bytes fit exactly in an order-o block */
    for (int o = 0; o <= BUDDY_MAX_ORDER; ++o) {
        void* p = smalloc(usable_size(o));
        OS_ASSERT_MSG(p != NULL, "smalloc(%llu) (exactly one order-%d block) returned NULL",
                      (unsigned long long)usable_size(o), o);
        OS_ASSERT_SINGLE_ALLOCATION(o);
        sfree(p);
        OS_ASSERT_FRESH_HEAP(); /* everything merges back */
    }
}

OS_TEST(one_byte_more_than_a_block_holds_moves_up_one_order) {
    for (int o = 0; o < BUDDY_MAX_ORDER; ++o) {
        void* p = smalloc(usable_size(o) + 1);
        OS_ASSERT_MSG(p != NULL, "smalloc(%llu) returned NULL",
                      (unsigned long long)(usable_size(o) + 1));
        OS_ASSERT_SINGLE_ALLOCATION(o + 1);
        sfree(p);
        OS_ASSERT_FRESH_HEAP();
    }
}

OS_TEST(splitting_stops_as_soon_as_the_request_no_longer_fits_in_half) {
    /* U(5) + 1 bytes do not fit in an order-5 block, so the order-6 block that
     * serves them must NOT be split any further */
    void* p = smalloc(usable_size(5) + 1);
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_SINGLE_ALLOCATION(6);
}

OS_TEST(two_smallest_allocations_are_buddies) {
    char* p1 = (char*)smalloc(1);
    char* p2 = (char*)smalloc(1);
    OS_ASSERT_NOT_NULL(p1);
    OS_ASSERT_NOT_NULL(p2);
    OS_ASSERT_MSG(p2 == p1 + MIN_BLOCK_SIZE,
                  "the second 128 byte block must be the buddy of the first, i.e. 128 bytes above "
                  "it (%p), but it is at %p",
                  (void*)(p1 + MIN_BLOCK_SIZE), (void*)p2);
    /* the second allocation reuses the free buddy: no new split */
    OS_ASSERT_STATS(40, free_bytes_after_single(0) - usable_size(0), 42,
                    free_bytes_after_single(0) + usable_size(0));
}

/* ------------------------------------------------------------------ */
/* challenge 0 - tightest fit, lowest address                          */
/* ------------------------------------------------------------------ */

OS_TEST(the_tightest_free_block_is_used) {
    size_t m = meta_size();
    char* p = (char*)smalloc(1); /* splits 128KB down to order 0 */
    OS_ASSERT_NOT_NULL(p);
    char* base = p - m;
    /* the split left one free block of each order: order k sits at offset (128 << k) */

    char* q = (char*)smalloc(usable_size(3)); /* needs exactly an order-3 block */
    char* expected = base + block_size(3) + m;
    OS_ASSERT_MSG(q == expected,
                  "there is a free order-3 block at %p and free blocks of every bigger order; the "
                  "tightest one (order 3) must be used, but smalloc returned %p",
                  (void*)expected, (void*)q);

    /* the order-3 block was used as is: no block was created or split */
    OS_ASSERT_STATS(40, free_bytes_after_single(0) - usable_size(3), 42,
                    free_bytes_after_single(0) + usable_size(0));
}

OS_TEST(among_equal_blocks_the_lowest_address_is_used) {
    char* p0 = (char*)smalloc(1);
    char* p1 = (char*)smalloc(1);
    char* p2 = (char*)smalloc(1);
    char* p3 = (char*)smalloc(1);
    OS_ASSERT_PTR_EQ(p1, p0 + 128);
    OS_ASSERT_PTR_EQ(p2, p0 + 256);
    OS_ASSERT_PTR_EQ(p3, p0 + 384);

    /* free two order-0 blocks that cannot merge (their buddies are in use) */
    sfree(p1);
    sfree(p3);

    void* q = smalloc(1);
    OS_ASSERT_MSG(q == p1,
                  "there are free 128 byte blocks at %p and %p; the one with the lower address "
                  "must be used, but smalloc returned %p",
                  (void*)p1, (void*)p3, q);
}

OS_TEST(a_big_free_block_is_not_used_when_a_tight_one_exists) {
    size_t m = meta_size();
    char* big = (char*)smalloc(usable_size(4)); /* order 4 at offset 0 */
    OS_ASSERT_NOT_NULL(big);
    char* base = big - m;
    OS_ASSERT_SINGLE_ALLOCATION(4);

    /* Splitting a 128KB block down to order 4 left one free block of each order
     * 4..9 behind. The tightest fit for an order-2 request is the free order-4
     * block right above `big` (at offset 2048) - not one of the 31 untouched
     * 128KB blocks - and it has to be split down to order 2. */
    char* small = (char*)smalloc(usable_size(2));
    char* expected = base + block_size(4) + m;
    OS_ASSERT_MSG(small == expected,
                  "the smallest free block that fits is the order-4 block at %p (the free 128KB "
                  "blocks are much bigger); it must be split down to order 2, so smalloc should "
                  "return %p, but it returned %p",
                  (void*)(base + block_size(4)), (void*)expected, (void*)small);

    /* the order-4 block became: used order 2, free order 2, free order 3 */
    size_t free_bytes =
        free_bytes_after_single(4) - usable_size(4) + usable_size(2) + usable_size(3);
    OS_ASSERT_STATS(38, free_bytes, 40, free_bytes + usable_size(4) + usable_size(2));
}

/* ------------------------------------------------------------------ */
/* challenge 2 - merging                                               */
/* ------------------------------------------------------------------ */

OS_TEST(freeing_the_only_block_merges_all_the_way_back) {
    void* p = smalloc(1);
    OS_ASSERT_SINGLE_ALLOCATION(0);
    sfree(p);
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(a_block_is_not_merged_while_its_buddy_is_used) {
    void* p1 = smalloc(1);
    void* p2 = smalloc(1); /* the buddy of p1 */

    sfree(p1);
    /* p1 cannot merge (its buddy p2 is in use): the block count does not change */
    OS_ASSERT_STATS(41, free_bytes_after_single(0), 42,
                    free_bytes_after_single(0) + usable_size(0));

    sfree(p2);
    /* now everything merges back into 32 blocks of 128KB */
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(merging_is_iterative) {
    /* fill the whole heap with the smallest blocks, then free them all: the
     * merge has to cascade 10 levels up, 32768 times */
    static void* ptrs[32768];
    const size_t count = HEAP_TOTAL_BYTES / MIN_BLOCK_SIZE; /* 32768 */

    for (size_t i = 0; i < count; ++i) {
        ptrs[i] = smalloc(1);
        OS_ASSERT_MSG(ptrs[i] != NULL, "the heap should fit %llu blocks of 128 bytes, but "
                                       "allocation #%llu failed",
                      (unsigned long long)count, (unsigned long long)i);
    }
    OS_ASSERT_STATS(0, 0, count, count * usable_size(0));

    for (size_t i = 0; i < count; ++i) sfree(ptrs[i]);
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(merging_is_iterative_when_freed_in_reverse) {
    static void* ptrs[32768];
    const size_t count = HEAP_TOTAL_BYTES / MIN_BLOCK_SIZE;

    for (size_t i = 0; i < count; ++i) {
        ptrs[i] = smalloc(1);
        OS_ASSERT_NOT_NULL(ptrs[i]);
    }
    for (size_t i = count; i > 0; --i) sfree(ptrs[i - 1]);
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(blocks_of_max_order_are_never_merged) {
    /* two neighbouring 128KB blocks are buddies on paper, but order 10 is the
     * biggest order there is: they must stay two separate blocks */
    void* p1 = smalloc(max_heap_request());
    void* p2 = smalloc(max_heap_request());
    OS_ASSERT_NOT_NULL(p1);
    OS_ASSERT_NOT_NULL(p2);
    OS_ASSERT_STATS(30, 30 * usable_size(BUDDY_MAX_ORDER), 32,
                    32 * usable_size(BUDDY_MAX_ORDER));

    sfree(p1);
    sfree(p2);
    /* still 32 blocks - if they had been merged there would be 31 */
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(sfree_of_null_does_nothing) {
    sfree(NULL);
    OS_ASSERT_STATS(0, 0, 0, 0); /* not even the heap is created */

    void* p = smalloc(1);
    sfree(NULL);
    OS_ASSERT_SINGLE_ALLOCATION(0);
    sfree(p);
    sfree(NULL);
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(freeing_an_already_freed_block_does_nothing) {
    void* p = smalloc(1);
    sfree(p);
    OS_ASSERT_FRESH_HEAP();
    sfree(p); /* must be ignored, not merged again */
    sfree(p);
    OS_ASSERT_FRESH_HEAP();
}

/* ------------------------------------------------------------------ */
/* the heap runs out                                                   */
/* ------------------------------------------------------------------ */

OS_TEST(smalloc_returns_null_when_the_heap_is_full) {
    void* ptrs[NUM_INITIAL_BLOCKS];
    for (size_t i = 0; i < NUM_INITIAL_BLOCKS; ++i) {
        ptrs[i] = smalloc(max_heap_request());
        OS_ASSERT_MSG(ptrs[i] != NULL, "the heap has 32 blocks of 128KB, but allocation #%llu of "
                                       "one whole block failed",
                      (unsigned long long)i);
    }
    OS_ASSERT_STATS(0, 0, 32, 32 * usable_size(BUDDY_MAX_ORDER));

    /* not one byte is left */
    OS_ASSERT_NULL(smalloc(1));
    OS_ASSERT_NULL(smalloc(max_heap_request()));
    OS_ASSERT_NULL(scalloc(1, 1));

    /* give one block back and it works again */
    sfree(ptrs[7]);
    void* p = smalloc(1);
    OS_ASSERT_NOT_NULL(p);
}

OS_TEST(mmap_still_works_when_the_heap_is_full) {
    void* ptrs[NUM_INITIAL_BLOCKS];
    for (size_t i = 0; i < NUM_INITIAL_BLOCKS; ++i) {
        ptrs[i] = smalloc(max_heap_request());
        OS_ASSERT_NOT_NULL(ptrs[i]);
    }
    OS_ASSERT_NULL(smalloc(1));

    /* a big allocation does not come from the heap, so it must still succeed */
    char* big = (char*)smalloc(200 * 1024);
    OS_ASSERT_NOT_NULL(big);
    fill_pattern(big, 200 * 1024, 3);
    OS_ASSERT_PATTERN(big, 200 * 1024, 3);
    OS_ASSERT_STATS(0, 0, 33, 32 * usable_size(BUDDY_MAX_ORDER) + 200 * 1024);
}

/* ------------------------------------------------------------------ */
/* challenge 3 - mmap                                                  */
/* ------------------------------------------------------------------ */

OS_TEST(the_largest_heap_request_is_128KB_minus_metadata) {
    size_t m = meta_size();
    char* small = (char*)smalloc(1);
    char* base = small - m;
    sfree(small);

    /* size + M == 128KB exactly: still the heap */
    char* p = (char*)smalloc(max_heap_request());
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_MSG((uintptr_t)p >= (uintptr_t)base &&
                      (uintptr_t)p < (uintptr_t)base + HEAP_TOTAL_BYTES,
                  "a request of %llu bytes (+ %llu metadata = exactly 128KB) must be served from "
                  "the heap, but it landed at %p, outside the region [%p, %p)",
                  (unsigned long long)max_heap_request(), (unsigned long long)m, (void*)p,
                  (void*)base, (void*)(base + HEAP_TOTAL_BYTES));
    OS_ASSERT_STATS(31, 31 * usable_size(BUDDY_MAX_ORDER), 32,
                    32 * usable_size(BUDDY_MAX_ORDER));
}

OS_TEST(one_byte_more_than_128KB_minus_metadata_is_mmapped) {
    size_t m = meta_size();
    char* small = (char*)smalloc(1);
    char* base = small - m;
    sfree(small);
    OS_ASSERT_FRESH_HEAP();

    size_t size = max_heap_request() + 1; /* size + M > 128KB */
    char* p = (char*)smalloc(size);
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_MSG((uintptr_t)p < (uintptr_t)base ||
                      (uintptr_t)p >= (uintptr_t)base + HEAP_TOTAL_BYTES,
                  "a request of %llu bytes (+ %llu metadata > 128KB) must be served by mmap(), but "
                  "it landed at %p, inside the heap region [%p, %p)",
                  (unsigned long long)size, (unsigned long long)m, (void*)p, (void*)base,
                  (void*)(base + HEAP_TOTAL_BYTES));

    /* the heap is untouched; the mmap()ed block is one more allocated block */
    OS_ASSERT_STATS(32, 32 * usable_size(BUDDY_MAX_ORDER), 33,
                    32 * usable_size(BUDDY_MAX_ORDER) + size);

    fill_pattern(p, size, 11);
    OS_ASSERT_PATTERN(p, size, 11);
}

OS_TEST(the_heap_is_created_even_when_the_first_request_is_mmapped) {
    /* "you should allocate the initial 32 free blocks the first time malloc()
     * is called" - even if that first call is a big one */
    size_t size = 200 * 1024;
    void* p = smalloc(size);
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_STATS(32, 32 * usable_size(BUDDY_MAX_ORDER), 33,
                    32 * usable_size(BUDDY_MAX_ORDER) + size);
    (void)p;
}

OS_TEST(freeing_an_mmapped_block_gives_the_memory_back) {
    void* small = smalloc(1); /* create the heap first */
    sfree(small);

    size_t size = 200 * 1024;
    void* p = smalloc(size);
    OS_ASSERT_STATS(32, 32 * usable_size(BUDDY_MAX_ORDER), 33,
                    32 * usable_size(BUDDY_MAX_ORDER) + size);

    sfree(p);
    /* munmap()ed memory is gone: it is neither allocated nor free (note 3) */
    OS_ASSERT_FRESH_HEAP();
}

/* The statistics are the allocator's own bookkeeping, so they happily report a
 * block as gone while the memory is still mapped. These two tests ask the kernel
 * instead, and are the only thing standing between you and a silent leak. */

OS_TEST(freed_mmap_blocks_are_really_given_back_to_the_kernel) {
    void* p = smalloc(200 * 1024);
    OS_ASSERT_NOT_NULL(p);
    if (!proc::available()) {
        warn("/proc is not available, skipping");
        return;
    }
    OS_ASSERT_MSG(proc::is_mapped(p), "the block at %p is not mapped at all?", p);

    sfree(p);
    OS_ASSERT_UNMAPPED(p, "a 200KB mmap()ed block");
}

OS_TEST(allocating_and_freeing_large_blocks_does_not_leak) {
    /* If munmap() is given a different length than mmap() got, it fails with
     * EINVAL, the statistics still say the block was freed, and the memory is
     * gone for good. After 30 cycles that is well over 100MB, which shows up in
     * the size of the process. */
    if (!proc::available()) {
        warn("/proc is not available, skipping");
        return;
    }
    void* warmup = smalloc(1); /* create the 4MB heap before we measure */
    sfree(warmup);

    unsigned long before = proc::vm_size_kb();
    OS_ASSERT_MSG(before > 0, "could not read VmSize from /proc/self/status");

    const size_t size = 5 * 1024 * 1024;
    for (int i = 0; i < 30; ++i) {
        char* p = (char*)smalloc(size);
        OS_ASSERT_MSG(p != NULL, "allocation #%d of 5MB failed", i);
        p[0] = 1;
        p[size - 1] = 1;
        sfree(p);
    }

    unsigned long after = proc::vm_size_kb();
    unsigned long grew = after > before ? after - before : 0;
    OS_ASSERT_MSG(grew <= 8 * 1024,
                  "the process grew by %lu kB after allocating and freeing 5MB thirty times. "
                  "sfree() is not really unmapping those blocks - the statistics say they are "
                  "gone, but the kernel still has them. Check that munmap() is given exactly the "
                  "address and the length that mmap() returned.",
                  grew);
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(scalloc_rejects_a_product_that_overflows) {
    /* The spec says: if 'size * num' is more than 10^8, return NULL. The real
     * product here is astronomically more than 10^8 - but computing num * size in
     * a size_t wraps around and can look tiny, sailing straight through a naive
     * guard. Check the product WITHOUT computing it (e.g. num > MAX / size). */
    OS_ASSERT_NULL(scalloc(((size_t)1 << 62) + 1, 4)); /* wraps to 4 */
    OS_ASSERT_NULL(scalloc(4, ((size_t)1 << 62) + 1));
    OS_ASSERT_NULL(scalloc(((size_t)1 << 61) + 1, 8)); /* wraps to 8 */
    OS_ASSERT_NULL(scalloc((size_t)-1, 2));
    OS_ASSERT_NULL(scalloc(2, (size_t)-1));
    OS_ASSERT_STATS(0, 0, 0, 0);
}

OS_TEST(several_mmapped_blocks_are_tracked_independently) {
    void* small = smalloc(1);
    sfree(small);

    size_t s1 = 130 * 1024, s2 = 500 * 1024, s3 = 1024 * 1024;
    char* p1 = (char*)smalloc(s1);
    char* p2 = (char*)smalloc(s2);
    char* p3 = (char*)smalloc(s3);
    OS_ASSERT_NOT_NULL(p1);
    OS_ASSERT_NOT_NULL(p2);
    OS_ASSERT_NOT_NULL(p3);
    fill_pattern(p1, s1, 1);
    fill_pattern(p2, s2, 2);
    fill_pattern(p3, s3, 3);

    size_t heap_bytes = 32 * usable_size(BUDDY_MAX_ORDER);
    OS_ASSERT_STATS(32, heap_bytes, 35, heap_bytes + s1 + s2 + s3);

    OS_ASSERT_PATTERN(p1, s1, 1);
    OS_ASSERT_PATTERN(p2, s2, 2);
    OS_ASSERT_PATTERN(p3, s3, 3);

    sfree(p2);
    OS_ASSERT_STATS(32, heap_bytes, 34, heap_bytes + s1 + s3);
    OS_ASSERT_PATTERN(p1, s1, 1);
    OS_ASSERT_PATTERN(p3, s3, 3);

    sfree(p1);
    sfree(p3);
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(a_very_large_allocation_is_served) {
    char* p = (char*)smalloc(MAX_ALLOC); /* 10^8 bytes, way past the mmap threshold */
    OS_ASSERT_NOT_NULL(p);

    void* small = smalloc(1); /* the heap exists as well */
    OS_ASSERT_NOT_NULL(small);

    p[0] = 'a';
    p[MAX_ALLOC / 2] = 'b';
    p[MAX_ALLOC - 1] = 'c';
    OS_ASSERT_EQ(p[0], 'a');
    OS_ASSERT_EQ(p[MAX_ALLOC / 2], 'b');
    OS_ASSERT_EQ(p[MAX_ALLOC - 1], 'c');

    OS_ASSERT_MSG(_num_allocated_bytes() >= MAX_ALLOC,
                  "_num_allocated_bytes() is %llu, it must include the %llu byte mmap()ed block",
                  (unsigned long long)_num_allocated_bytes(), (unsigned long long)MAX_ALLOC);

    sfree(p);
    OS_ASSERT_SINGLE_ALLOCATION(0);
}

OS_TEST(mmap_sized_requests_still_respect_the_10_pow_8_limit) {
    OS_ASSERT_NULL(smalloc(MAX_ALLOC + 1));
    OS_ASSERT_NULL(scalloc(MAX_ALLOC + 1, 1));
    OS_ASSERT_NULL(smalloc((size_t)-1));
    OS_ASSERT_STATS(0, 0, 0, 0);
}

/* ------------------------------------------------------------------ */
/* smalloc / scalloc failure cases                                     */
/* ------------------------------------------------------------------ */

OS_TEST(smalloc_rejects_size_zero) {
    OS_ASSERT_NULL(smalloc(0));
    OS_ASSERT_STATS(0, 0, 0, 0);
}

OS_TEST(scalloc_rejects_zero_arguments) {
    OS_ASSERT_NULL(scalloc(0, 10));
    OS_ASSERT_NULL(scalloc(10, 0));
    OS_ASSERT_NULL(scalloc(0, 0));
    OS_ASSERT_STATS(0, 0, 0, 0);
}

OS_TEST(scalloc_zeroes_the_memory_it_returns) {
    int* p = (int*)scalloc(25, sizeof(int));
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_ZEROED(p, 100);
    OS_ASSERT_SINGLE_ALLOCATION(order_for(100));
}

OS_TEST(scalloc_zeroes_a_dirty_block_it_reuses) {
    size_t size = usable_size(3);
    char* p = (char*)smalloc(size);
    OS_ASSERT_NOT_NULL(p);
    memset(p, 0xFF, size);
    sfree(p);
    OS_ASSERT_FRESH_HEAP();

    char* q = (char*)scalloc(1, size); /* the same block, still full of 0xFF */
    OS_ASSERT_PTR_EQ(q, p);
    OS_ASSERT_ZEROED(q, size);
}

OS_TEST(scalloc_of_an_mmapped_block_is_zeroed) {
    size_t size = 300 * 1024;
    char* p = (char*)scalloc(1, size);
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_ZEROED(p, size);
    OS_ASSERT_STATS(32, 32 * usable_size(BUDDY_MAX_ORDER), 33,
                    32 * usable_size(BUDDY_MAX_ORDER) + size);
}

/* ------------------------------------------------------------------ */
/* srealloc                                                            */
/* ------------------------------------------------------------------ */

OS_TEST(srealloc_with_null_behaves_like_smalloc) {
    void* p = srealloc(NULL, 1);
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_SINGLE_ALLOCATION(0);
    (void)p;
}

OS_TEST(srealloc_rejects_bad_sizes_and_keeps_the_old_block) {
    size_t size = usable_size(2);
    char* p = (char*)smalloc(size);
    fill_pattern(p, size, 21);

    OS_ASSERT_NULL(srealloc(p, 0));
    OS_ASSERT_NULL(srealloc(p, MAX_ALLOC + 1));
    OS_ASSERT_NULL(srealloc(NULL, 0));

    /* the block was not freed and not touched */
    OS_ASSERT_SINGLE_ALLOCATION(2);
    OS_ASSERT_PATTERN(p, size, 21);
}

OS_TEST(srealloc_reuses_the_current_block_when_the_request_still_fits) {
    /* priority (a): no merging, no moving, not even when a smaller block would do */
    size_t size = usable_size(4);
    char* p = (char*)smalloc(size);
    fill_pattern(p, size, 22);

    void* q = srealloc(p, usable_size(4)); /* exactly fills the block */
    OS_ASSERT_PTR_EQ(q, p);
    OS_ASSERT_SINGLE_ALLOCATION(4);

    void* r = srealloc(p, 1); /* much smaller: still the same block */
    OS_ASSERT_PTR_EQ(r, p);
    OS_ASSERT_SINGLE_ALLOCATION(4);

    OS_ASSERT_PATTERN(p, 1, 22);
}

OS_TEST(srealloc_merges_with_its_free_buddy) {
    size_t m = meta_size();
    size_t size = usable_size(0);
    char* p1 = (char*)smalloc(size); /* order 0, offset 0 */
    char* p2 = (char*)smalloc(size); /* order 0, its buddy */
    OS_ASSERT_PTR_EQ(p2, p1 + MIN_BLOCK_SIZE);
    fill_pattern(p1, size, 23);

    sfree(p2); /* the buddy is free now */

    char* q = (char*)srealloc(p1, usable_size(1)); /* needs order 1 */
    OS_ASSERT_MSG(q == p1,
                  "the block at %p and its free buddy at %p must be merged into one order-1 block, "
                  "so srealloc() must return %p, but it returned %p",
                  (void*)p1, (void*)p2, (void*)p1, (void*)q);
    OS_ASSERT_PATTERN(q, size, 23);

    /* the heap now looks exactly as if one order-1 block had been allocated */
    OS_ASSERT_SINGLE_ALLOCATION(1);
    (void)m;
}

OS_TEST(srealloc_merges_buddies_iteratively) {
    size_t size = usable_size(0);
    char* p = (char*)smalloc(size); /* order 0 at offset 0 */
    fill_pattern(p, size, 24);
    /* the split left free blocks of order 0 (offset 128) and order 1 (offset 256)
     * right above it, so two merges in a row can reach order 2 */

    char* q = (char*)srealloc(p, usable_size(2));
    OS_ASSERT_MSG(q == p,
                  "merging the block with its free buddy (order 0) and then with the free order-1 "
                  "block above it yields the order-2 block the request needs, so srealloc() must "
                  "return %p, but it returned %p",
                  (void*)p, (void*)q);
    OS_ASSERT_PATTERN(q, size, 24);
    OS_ASSERT_SINGLE_ALLOCATION(2);
}

OS_TEST(srealloc_does_not_merge_when_the_current_block_is_enough) {
    size_t size = usable_size(0);
    char* p1 = (char*)smalloc(size);
    char* p2 = (char*)smalloc(size);
    sfree(p2); /* a free buddy is available... */

    void* q = srealloc(p1, 1); /* ...but the request fits as is */
    OS_ASSERT_PTR_EQ(q, p1);
    /* the buddy must still be a separate free block */
    OS_ASSERT_STATS(41, free_bytes_after_single(0), 42,
                    free_bytes_after_single(0) + usable_size(0));
}

OS_TEST(srealloc_moves_the_block_when_merging_is_not_enough) {
    size_t m = meta_size();
    size_t size = usable_size(0);
    char* p1 = (char*)smalloc(size); /* order 0, offset 0 */
    char* p2 = (char*)smalloc(size); /* order 0, offset 128 - p1's buddy, in use */
    char* base = p1 - m;
    fill_pattern(p1, size, 25);

    /* p1 cannot grow: its buddy is used. The tightest free block for an order-2
     * request is the order-2 block the first split left at offset 512. */
    char* q = (char*)srealloc(p1, usable_size(2));
    char* expected = base + block_size(2) + m;
    OS_ASSERT_MSG(q == expected,
                  "p1's buddy is in use, so srealloc() has to move the data into the free order-2 "
                  "block at %p, but it returned %p",
                  (void*)expected, (void*)q);
    OS_ASSERT_PATTERN(q, size, 25);

    /* p1's block was freed (it could not merge: its buddy p2 is still used) */
    size_t free_bytes = free_bytes_after_single(0) - usable_size(2);
    OS_ASSERT_STATS(40, free_bytes, 42, free_bytes + usable_size(0) + usable_size(2));
    (void)p2;
}

OS_TEST(srealloc_merges_several_orders_up_before_it_gives_up) {
    size_t m = meta_size();
    size_t size = usable_size(1);
    char* p = (char*)smalloc(size); /* order 1 at offset 0 */
    char* base = p - m;
    fill_pattern(p, size, 26);

    /* The first split left free blocks of order 1 (offset 256), order 2 (offset
     * 512) and order 3 (offset 1024) right above p, so three merges in a row
     * turn p's block into the order-4 block this request needs. srealloc() must
     * take that route instead of moving the data somewhere else. */
    char* q = (char*)srealloc(p, usable_size(4));
    OS_ASSERT_MSG(q == base + m,
                  "p's block can be merged with its free buddies up to order 4, so srealloc() "
                  "must reuse it and return %p, but it returned %p",
                  (void*)(base + m), (void*)q);
    OS_ASSERT_PATTERN(q, size, 26);
    /* the heap now looks exactly as if one order-4 block had been allocated */
    OS_ASSERT_SINGLE_ALLOCATION(4);
}

OS_TEST(srealloc_fails_when_nothing_can_serve_the_request) {
    static void* ptrs[32768];
    const size_t count = HEAP_TOTAL_BYTES / MIN_BLOCK_SIZE;
    for (size_t i = 0; i < count; ++i) {
        ptrs[i] = smalloc(1);
        OS_ASSERT_NOT_NULL(ptrs[i]);
    }
    fill_pattern(ptrs[0], usable_size(0), 27);

    /* every block is used, so ptrs[0] can neither grow, nor merge, nor move */
    void* q = srealloc(ptrs[0], usable_size(1));
    OS_ASSERT_MSG(q == NULL, "the heap is completely full, so srealloc() must fail, but it "
                             "returned %p",
                  q);
    /* and it must not have freed the old block */
    OS_ASSERT_STATS(0, 0, count, count * usable_size(0));
    OS_ASSERT_PATTERN(ptrs[0], usable_size(0), 27);
}

OS_TEST(srealloc_of_an_mmapped_block_to_the_same_size_reuses_it) {
    size_t size = 200 * 1024;
    char* p = (char*)smalloc(size);
    fill_pattern(p, size, 28);

    void* q = srealloc(p, size);
    OS_ASSERT_MSG(q == p,
                  "srealloc() of an mmap()ed block to its own size must reuse it (old_size == "
                  "new_size): expected %p, got %p",
                  (void*)p, q);
    OS_ASSERT_PATTERN(p, size, 28);
    OS_ASSERT_STATS(32, 32 * usable_size(BUDDY_MAX_ORDER), 33,
                    32 * usable_size(BUDDY_MAX_ORDER) + size);
}

OS_TEST(srealloc_of_an_mmapped_block_to_a_bigger_size_allocates_a_new_one) {
    size_t old_size = 200 * 1024;
    size_t new_size = 300 * 1024;
    char* p = (char*)smalloc(old_size);
    fill_pattern(p, old_size, 29);

    char* q = (char*)srealloc(p, new_size);
    OS_ASSERT_NOT_NULL(q);
    OS_ASSERT_MSG(q != p, "a resized mmap()ed block must never be reused, but srealloc() returned "
                          "the old pointer %p",
                  (void*)p);
    OS_ASSERT_PATTERN(q, old_size, 29); /* the old contents came along */

    size_t heap_bytes = 32 * usable_size(BUDDY_MAX_ORDER);
    /* the old mapping is gone, the new one took its place */
    OS_ASSERT_STATS(32, heap_bytes, 33, heap_bytes + new_size);
}

OS_TEST(srealloc_of_an_mmapped_block_to_a_smaller_size_allocates_a_new_one) {
    size_t old_size = 400 * 1024;
    size_t new_size = 200 * 1024; /* still above the mmap threshold */
    char* p = (char*)smalloc(old_size);
    fill_pattern(p, old_size, 30);

    char* q = (char*)srealloc(p, new_size);
    OS_ASSERT_NOT_NULL(q);
    OS_ASSERT_PTR_NE(q, p);
    OS_ASSERT_PATTERN(q, new_size, 30);

    size_t heap_bytes = 32 * usable_size(BUDDY_MAX_ORDER);
    OS_ASSERT_STATS(32, heap_bytes, 33, heap_bytes + new_size);
}

OS_TEST(an_mmapped_block_born_inside_srealloc_is_unmapped_when_freed) {
    /* srealloc() creates the new block itself, so it has to set up the metadata
     * as completely as smalloc() does. If the "this one came from mmap()" flag is
     * only set by smalloc() and not on this path, sfree() will treat the block as
     * a heap block: the statistics still look right, and the memory is leaked. */
    char* p = (char*)smalloc(200 * 1024);
    OS_ASSERT_NOT_NULL(p);
    fill_pattern(p, 200 * 1024, 33);

    char* q = (char*)srealloc(p, 300 * 1024); /* q is born inside srealloc() */
    OS_ASSERT_NOT_NULL(q);
    OS_ASSERT_PATTERN(q, 200 * 1024, 33);

    size_t heap_bytes = 32 * usable_size(BUDDY_MAX_ORDER);
    OS_ASSERT_STATS(32, heap_bytes, 33, heap_bytes + 300 * 1024);

    sfree(q);
    OS_ASSERT_FRESH_HEAP();                        /* the statistics say it is gone... */
    OS_ASSERT_UNMAPPED(q, "an mmap()ed block created by srealloc()"); /* ...and the kernel agrees */
}

OS_TEST(srealloc_of_the_same_block_twice_keeps_it_consistent) {
    /* Realloc a block that was itself born in a realloc. Any field that srealloc
     * forgets to carry over shows up on the second round, not the first. */
    size_t size = usable_size(2);
    char* p = (char*)smalloc(size);
    OS_ASSERT_NOT_NULL(p);
    fill_pattern(p, size, 34);

    char* q = (char*)srealloc(p, usable_size(4));
    OS_ASSERT_NOT_NULL(q);
    OS_ASSERT_PATTERN(q, size, 34);

    char* r = (char*)srealloc(q, usable_size(6));
    OS_ASSERT_NOT_NULL(r);
    OS_ASSERT_PATTERN(r, size, 34);
    OS_ASSERT_SINGLE_ALLOCATION(6);

    sfree(r);
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(srealloc_keeps_the_data_across_a_chain_of_growing_reallocs) {
    size_t size = 1;
    char* p = (char*)smalloc(size);
    OS_ASSERT_NOT_NULL(p);
    fill_pattern(p, size, 31);

    while (size * 2 <= max_heap_request()) {
        size_t bigger = size * 2;
        char* q = (char*)srealloc(p, bigger);
        OS_ASSERT_MSG(q != NULL, "srealloc() to %llu bytes failed", (unsigned long long)bigger);
        OS_ASSERT_PATTERN(q, size, 31);
        fill_pattern(q, bigger, 31);
        p = q;
        size = bigger;
    }
    OS_ASSERT_INVARIANTS();

    sfree(p);
    OS_ASSERT_FRESH_HEAP();
}

/* ------------------------------------------------------------------ */
/* failures of the underlying system calls                             */
/* ------------------------------------------------------------------ */

OS_TEST(the_allocator_reports_failure_instead_of_crashing) {
    /* the heap needs 4MB up front; with a 1MB cap it cannot be created */
    limit_heap_growth(1024 * 1024);

    void* p = smalloc(100);
    if (p != NULL)
        warn("smalloc() succeeded although RLIMIT_DATA (1MB) is far below the 4MB the buddy "
             "allocator needs - is the initial region really taken from the heap?");
    else
        OS_ASSERT_STATS(0, 0, 0, 0);
}

/* ------------------------------------------------------------------ */
/* differential tests against the reference model                      */
/* ------------------------------------------------------------------ */

OS_TEST(model_split_and_merge_sequence) {
    BuddySim sim;
    void* a = D_MALLOC(sim, 1);              /* order 0 */
    void* b = D_MALLOC(sim, usable_size(0)); /* order 0, the buddy */
    void* c = D_MALLOC(sim, usable_size(3)); /* order 3 */
    D_FREE(sim, a);
    void* d = D_CALLOC(sim, 1, 10); /* takes a's block back */
    D_FREE(sim, b);
    D_FREE(sim, d);
    void* e = D_MALLOC(sim, usable_size(5));
    D_REALLOC(sim, c, usable_size(4));
    D_FREE(sim, e);
    D_VERIFY(sim);
}

OS_TEST(model_mmap_sequence) {
    BuddySim sim;
    void* a = D_MALLOC(sim, 100);
    void* big1 = D_MALLOC(sim, 200 * 1024);
    void* big2 = D_CALLOC(sim, 2, 100 * 1024);
    void* b = D_MALLOC(sim, usable_size(6));
    D_FREE(sim, big1);
    void* big3 = D_REALLOC(sim, big2, 400 * 1024);
    D_FREE(sim, a);
    D_FREE(sim, b);
    D_FREE(sim, big3);
    D_VERIFY(sim);
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(stress_random_operations_match_the_reference_model) {
    BuddySim sim;
    Rng rng(987654321);
    std::vector<void*> live;

    for (int step = 0; step < 3000; ++step) {
        unsigned int op = rng.next() % 100;

        if (live.empty() || op < 40) {
            /* mostly small requests, a few big ones, and now and then an mmap */
            size_t size;
            unsigned int kind = rng.next() % 100;
            if (kind < 70)
                size = rng.range(1, 500);
            else if (kind < 95)
                size = rng.range(500, max_heap_request());
            else
                size = rng.range(max_heap_request() + 1, 400 * 1024);
            void* p = D_MALLOC(sim, size);
            if (p) live.push_back(p);
        } else if (op < 52) {
            size_t num = rng.range(1, 16);
            size_t size = rng.range(1, 300);
            void* p = D_CALLOC(sim, num, size);
            if (p) live.push_back(p);
        } else if (op < 85) {
            size_t i = rng.range(0, live.size() - 1);
            D_FREE(sim, live[i]);
            live[i] = live.back();
            live.pop_back();
        } else {
            size_t i = rng.range(0, live.size() - 1);
            void* p = live[i];
            /* the spec says an mmap()ed block is never resized below the
             * threshold, and a heap block never above it */
            size_t size;
            if (sim.is_mmapped(p))
                size = rng.range(max_heap_request() + 1, 400 * 1024);
            else
                size = rng.range(1, max_heap_request());
            void* q = D_REALLOC(sim, p, size);
            if (q) live[i] = q;
        }

        if (step % 200 == 0) D_VERIFY(sim);
    }

    D_VERIFY(sim);
    while (!live.empty()) {
        D_FREE(sim, live.back());
        live.pop_back();
    }
    /* after everything is freed, the heap must be whole again */
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(stress_small_allocations_match_the_reference_model) {
    /* only the smallest orders: maximum splitting and merging pressure */
    BuddySim sim;
    Rng rng(24680);
    std::vector<void*> live;

    for (int step = 0; step < 4000; ++step) {
        unsigned int op = rng.next() % 100;
        if (live.empty() || op < 55) {
            void* p = D_MALLOC(sim, rng.range(1, usable_size(2)));
            if (p) live.push_back(p);
        } else {
            size_t i = rng.range(0, live.size() - 1);
            D_FREE(sim, live[i]);
            live[i] = live.back();
            live.pop_back();
        }
    }
    D_VERIFY(sim);
    while (!live.empty()) {
        D_FREE(sim, live.back());
        live.pop_back();
    }
    OS_ASSERT_FRESH_HEAP();
}

#endif /* OS_MALLOC3_TESTS_H */
