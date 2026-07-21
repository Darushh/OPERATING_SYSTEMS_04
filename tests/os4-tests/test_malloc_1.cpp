/*
 * Tests for part 1 - naive malloc (malloc_1.cpp).
 *
 * The only required function is smalloc(). The spec says:
 *   - success: a pointer to the first allocated byte
 *   - failure: NULL if size == 0, NULL if size > 10^8, NULL if sbrk() fails
 *
 * Nothing about metadata, alignment or reuse is required here, so the tests
 * only check what the spec actually promises. The two places where we look at
 * *how* the memory was obtained (contiguity / the program break) are warnings,
 * not failures.
 */

#include "test_utils.h"

using namespace ostest;

/* ------------------------------------------------------------------ */
/* the three documented failure cases                                  */
/* ------------------------------------------------------------------ */

OS_TEST(smalloc_rejects_size_zero) {
    OS_ASSERT_NULL(smalloc(0));
}

OS_TEST(smalloc_rejects_sizes_above_10_pow_8) {
    OS_ASSERT_NULL(smalloc(MAX_ALLOC + 1));
    OS_ASSERT_NULL(smalloc(MAX_ALLOC + 2));
    OS_ASSERT_NULL(smalloc(2 * MAX_ALLOC));
    OS_ASSERT_NULL(smalloc((size_t)-1));
    OS_ASSERT_NULL(smalloc((size_t)-1 / 2));
}

OS_TEST(smalloc_accepts_exactly_10_pow_8) {
    /* 10^8 is *not* "more than 10^8": it must still be served. */
    char* p = (char*)smalloc(MAX_ALLOC);
    OS_ASSERT_NOT_NULL(p);

    /* the whole range has to be ours: touch both ends and the middle */
    p[0] = 'a';
    p[MAX_ALLOC / 2] = 'b';
    p[MAX_ALLOC - 1] = 'c';
    OS_ASSERT_EQ(p[0], 'a');
    OS_ASSERT_EQ(p[MAX_ALLOC / 2], 'b');
    OS_ASSERT_EQ(p[MAX_ALLOC - 1], 'c');
}

OS_TEST(smalloc_returns_null_when_sbrk_fails) {
    /* Cap the data segment, then ask for far more than the cap: sbrk() must
     * fail and smalloc() must report it as NULL instead of returning garbage. */
    limit_heap_growth(8 * 1024 * 1024);

    OS_ASSERT_NULL(smalloc(90 * 1024 * 1024));
    OS_ASSERT_NULL(smalloc(MAX_ALLOC));

    /* and a small request that still fits must keep working */
    void* p = smalloc(64);
    OS_ASSERT_NOT_NULL(p);
    fill_pattern(p, 64, 7);
    OS_ASSERT_PATTERN(p, 64, 7);
}

OS_TEST(rejected_requests_do_not_move_the_program_break) {
    void* before = sbrk(0);
    OS_ASSERT_NULL(smalloc(0));
    OS_ASSERT_NULL(smalloc(MAX_ALLOC + 1));
    void* after = sbrk(0);
    OS_ASSERT_PTR_EQ(after, before);
}

/* ------------------------------------------------------------------ */
/* the memory we get back is real memory                               */
/* ------------------------------------------------------------------ */

OS_TEST(smalloc_returns_writable_memory) {
    void* p = smalloc(100);
    OS_ASSERT_NOT_NULL(p);
    fill_pattern(p, 100, 1);
    OS_ASSERT_PATTERN(p, 100, 1);
}

OS_TEST(smalloc_of_one_byte) {
    char* p = (char*)smalloc(1);
    OS_ASSERT_NOT_NULL(p);
    *p = 0x42;
    OS_ASSERT_EQ(*p, 0x42);
}

OS_TEST(smalloc_serves_an_int_array) {
    const int N = 1000;
    int* arr = (int*)smalloc(N * sizeof(int));
    OS_ASSERT_NOT_NULL(arr);
    for (int i = 0; i < N; ++i) arr[i] = i * 7 - 3;
    for (int i = 0; i < N; ++i) OS_ASSERT_EQ(arr[i], i * 7 - 3);
}

OS_TEST(every_requested_byte_is_usable) {
    /* sizes 1..256: each allocation must really own `size` bytes */
    for (size_t size = 1; size <= 256; ++size) {
        void* p = smalloc(size);
        OS_ASSERT_MSG(p != NULL, "smalloc(%llu) returned NULL", (unsigned long long)size);
        fill_pattern(p, size, (unsigned int)size);
        OS_ASSERT_PATTERN(p, size, (unsigned int)size);
    }
}

/* ------------------------------------------------------------------ */
/* distinct allocations are really distinct                            */
/* ------------------------------------------------------------------ */

OS_TEST(allocations_do_not_overlap) {
    const int N = 8;
    size_t sizes[N] = {1, 17, 100, 3, 4096, 33, 7, 512};
    void* ptrs[N];

    for (int i = 0; i < N; ++i) {
        ptrs[i] = smalloc(sizes[i]);
        OS_ASSERT_NOT_NULL(ptrs[i]);
        fill_pattern(ptrs[i], sizes[i], (unsigned int)(i + 1));
    }

    for (int i = 0; i < N; ++i)
        for (int j = i + 1; j < N; ++j)
            OS_ASSERT_DISJOINT(ptrs[i], sizes[i], ptrs[j], sizes[j]);

    /* nothing was clobbered by a later allocation */
    for (int i = 0; i < N; ++i) OS_ASSERT_PATTERN(ptrs[i], sizes[i], (unsigned int)(i + 1));
}

OS_TEST(many_small_allocations_stay_intact) {
    const int N = 2000;
    void* ptrs[N];
    for (int i = 0; i < N; ++i) {
        ptrs[i] = smalloc(16);
        OS_ASSERT_MSG(ptrs[i] != NULL, "allocation #%d of 16 bytes returned NULL", i);
        fill_pattern(ptrs[i], 16, (unsigned int)i);
    }
    for (int i = 0; i < N; ++i) OS_ASSERT_PATTERN(ptrs[i], 16, (unsigned int)i);
}

OS_TEST(addresses_grow_with_the_heap) {
    /* sbrk() grows upwards, so a naive allocator hands out increasing addresses */
    void* prev = smalloc(64);
    OS_ASSERT_NOT_NULL(prev);
    for (int i = 0; i < 20; ++i) {
        void* p = smalloc(64);
        OS_ASSERT_NOT_NULL(p);
        OS_ASSERT_MSG((uintptr_t)p > (uintptr_t)prev,
                      "allocation #%d is at %p, below the previous one at %p", i, p, prev);
        prev = p;
    }
}

/* ------------------------------------------------------------------ */
/* how the memory was obtained                                         */
/* ------------------------------------------------------------------ */

OS_TEST(allocation_lives_inside_the_heap_it_created) {
    void* before = sbrk(0);
    void* p = smalloc(1000);
    void* after = sbrk(0);

    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_MSG((uintptr_t)after - (uintptr_t)before >= 1000,
                  "the program break only moved by %llu bytes for a 1000 byte request",
                  (unsigned long long)((uintptr_t)after - (uintptr_t)before));
    OS_ASSERT_MSG((uintptr_t)p >= (uintptr_t)before && (uintptr_t)p + 1000 <= (uintptr_t)after,
                  "the returned block [%p, %p) is not inside the heap area [%p, %p) that "
                  "smalloc() added",
                  p, (void*)((char*)p + 1000), before, after);
}

OS_TEST(no_memory_is_wasted_between_allocations) {
    /* Part 1 has no metadata and no alignment, so a straight sbrk(size) puts the
     * blocks back to back. A gap is not a spec violation - only wasteful - so
     * this only warns. */
    char* p1 = (char*)smalloc(100);
    char* p2 = (char*)smalloc(200);
    char* p3 = (char*)smalloc(50);
    OS_ASSERT_NOT_NULL(p1);
    OS_ASSERT_NOT_NULL(p2);
    OS_ASSERT_NOT_NULL(p3);

    if (p2 != p1 + 100 || p3 != p2 + 200)
        warn("the blocks are not contiguous (%p, %p, %p for sizes 100, 200, 50): the naive "
             "allocator is expected to hand out exactly what sbrk() returned",
             p1, p2, p3);
}

OS_TEST_MAIN("malloc_1 - naive malloc")
