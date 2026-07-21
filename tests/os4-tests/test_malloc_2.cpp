/*
 * Tests for part 2 - basic malloc (malloc_2.cpp).
 *
 * What the spec pins down, and what these tests check:
 *   - smalloc / scalloc / sfree / srealloc, including every documented failure
 *     case (size 0, size > 10^8, sfree(NULL), double free, failed srealloc must
 *     not free oldp);
 *   - the six statistics functions, to the byte;
 *   - note 2: free blocks are searched in ascending address order (first fit);
 *   - notes 1 and 12: a block's size never changes, and a large block reused for
 *     a small request counts as fully used;
 *   - note 9: a "block" is the metadata plus the memory next to it, so
 *     _num_meta_data_bytes() == _num_allocated_blocks() * _size_meta_data().
 *
 * Every test runs in a fresh process (see test_utils.h), so the expected
 * statistics below are absolute, not deltas.
 */

#include <string.h>

#include "basic_model.h"
#include "stats_utils.h"
#include "test_utils.h"

using namespace ostest;

/* ------------------------------------------------------------------ */
/* the metadata itself                                                 */
/* ------------------------------------------------------------------ */

OS_TEST(size_meta_data_is_sane_and_stable) {
    size_t m = _size_meta_data();
    OS_ASSERT_MSG(m > 0, "_size_meta_data() returned 0");
    OS_ASSERT_MSG(m >= sizeof(size_t), "_size_meta_data() is %llu, too small to even hold the size",
                  (unsigned long long)m);
    OS_ASSERT_MSG(m < 1024, "_size_meta_data() is %llu, which is implausibly large",
                  (unsigned long long)m);
    OS_ASSERT_EQ(_size_meta_data(), m); /* it must not change between calls */

    if (m > 64)
        warn("_size_meta_data() is %llu; part 3 requires a metadata struct of at most 64 bytes",
             (unsigned long long)m);
}

OS_TEST(statistics_are_zero_before_the_first_allocation) {
    OS_ASSERT_STATS(0, 0, 0, 0);
}

/* ------------------------------------------------------------------ */
/* smalloc                                                             */
/* ------------------------------------------------------------------ */

OS_TEST(smalloc_rejects_size_zero) {
    OS_ASSERT_NULL(smalloc(0));
    OS_ASSERT_STATS(0, 0, 0, 0);
}

OS_TEST(smalloc_rejects_sizes_above_10_pow_8) {
    OS_ASSERT_NULL(smalloc(MAX_ALLOC + 1));
    OS_ASSERT_NULL(smalloc(2 * MAX_ALLOC));
    OS_ASSERT_NULL(smalloc((size_t)-1));
    OS_ASSERT_STATS(0, 0, 0, 0);
}

OS_TEST(smalloc_accepts_exactly_10_pow_8) {
    char* p = (char*)smalloc(MAX_ALLOC);
    OS_ASSERT_NOT_NULL(p);
    p[0] = 'a';
    p[MAX_ALLOC - 1] = 'z';
    OS_ASSERT_STATS(0, 0, 1, MAX_ALLOC);
}

OS_TEST(smalloc_returns_null_when_sbrk_fails) {
    limit_heap_growth(8 * 1024 * 1024);
    OS_ASSERT_NULL(smalloc(90 * 1024 * 1024));

    /* the failed request must not have been recorded as a block */
    OS_ASSERT_STATS(0, 0, 0, 0);

    /* and the allocator must still work afterwards */
    void* p = smalloc(64);
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_STATS(0, 0, 1, 64);
}

OS_TEST(one_allocation_updates_every_statistic) {
    void* p = smalloc(100);
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_STATS(0, 0, 1, 100);
    OS_ASSERT_EQ(_num_meta_data_bytes(), _size_meta_data());
    (void)p;
}

OS_TEST(three_allocations_update_every_statistic) {
    smalloc(100);
    smalloc(200);
    smalloc(300);
    OS_ASSERT_STATS(0, 0, 3, 600);
    OS_ASSERT_EQ(_num_meta_data_bytes(), 3 * _size_meta_data());
}

OS_TEST(blocks_are_laid_out_back_to_back_with_their_metadata) {
    /* Part 2 has no alignment (note 13) and grows the heap with sbrk() only, so
     * consecutive blocks are [metadata][data][metadata][data]... */
    size_t m = _size_meta_data();
    char* p1 = (char*)smalloc(100);
    char* p2 = (char*)smalloc(200);
    char* p3 = (char*)smalloc(64);
    OS_ASSERT_NOT_NULL(p1);
    OS_ASSERT_NOT_NULL(p2);
    OS_ASSERT_NOT_NULL(p3);

    OS_ASSERT_MSG(p2 == p1 + 100 + m,
                  "the second block should start %llu bytes (100 + metadata) after the first: "
                  "expected %p, got %p",
                  (unsigned long long)(100 + m), (void*)(p1 + 100 + m), (void*)p2);
    OS_ASSERT_MSG(p3 == p2 + 200 + m,
                  "the third block should start %llu bytes (200 + metadata) after the second: "
                  "expected %p, got %p",
                  (unsigned long long)(200 + m), (void*)(p2 + 200 + m), (void*)p3);
}

OS_TEST(allocations_do_not_overlap_and_keep_their_data) {
    const int N = 10;
    size_t sizes[N] = {1, 8, 100, 3, 1024, 17, 256, 5, 64, 4096};
    void* ptrs[N];
    for (int i = 0; i < N; ++i) {
        ptrs[i] = smalloc(sizes[i]);
        OS_ASSERT_NOT_NULL(ptrs[i]);
        fill_pattern(ptrs[i], sizes[i], (unsigned int)(i + 1));
    }
    for (int i = 0; i < N; ++i)
        for (int j = i + 1; j < N; ++j) OS_ASSERT_DISJOINT(ptrs[i], sizes[i], ptrs[j], sizes[j]);
    for (int i = 0; i < N; ++i) OS_ASSERT_PATTERN(ptrs[i], sizes[i], (unsigned int)(i + 1));
}

/* ------------------------------------------------------------------ */
/* sfree                                                               */
/* ------------------------------------------------------------------ */

OS_TEST(sfree_marks_the_block_as_free) {
    void* p = smalloc(100);
    OS_ASSERT_STATS(0, 0, 1, 100);
    sfree(p);
    /* the block is still allocated (the heap never shrinks), it is just free */
    OS_ASSERT_STATS(1, 100, 1, 100);
}

OS_TEST(sfree_of_null_does_nothing) {
    sfree(NULL);
    OS_ASSERT_STATS(0, 0, 0, 0);
    void* p = smalloc(100);
    sfree(NULL);
    OS_ASSERT_STATS(0, 0, 1, 100);
    sfree(p);
    sfree(NULL);
    OS_ASSERT_STATS(1, 100, 1, 100);
}

OS_TEST(freeing_an_already_freed_block_does_nothing) {
    void* p = smalloc(100);
    sfree(p);
    OS_ASSERT_STATS(1, 100, 1, 100);
    sfree(p);
    sfree(p);
    OS_ASSERT_STATS(1, 100, 1, 100);
}

OS_TEST(freeing_the_middle_block_leaves_the_others_alone) {
    char* p1 = (char*)smalloc(100);
    char* p2 = (char*)smalloc(200);
    char* p3 = (char*)smalloc(300);
    fill_pattern(p1, 100, 1);
    fill_pattern(p3, 300, 3);

    sfree(p2);
    OS_ASSERT_STATS(1, 200, 3, 600);
    OS_ASSERT_PATTERN(p1, 100, 1);
    OS_ASSERT_PATTERN(p3, 300, 3);
}

OS_TEST(freeing_everything_frees_every_block) {
    const int N = 20;
    void* ptrs[N];
    size_t total = 0;
    for (int i = 0; i < N; ++i) {
        size_t size = (size_t)(i + 1) * 10;
        ptrs[i] = smalloc(size);
        total += size;
    }
    OS_ASSERT_STATS(0, 0, N, total);
    for (int i = 0; i < N; ++i) sfree(ptrs[i]);
    OS_ASSERT_STATS(N, total, N, total);
}

/* ------------------------------------------------------------------ */
/* reuse of free blocks                                                */
/* ------------------------------------------------------------------ */

OS_TEST(a_freed_block_is_reused_for_the_same_size) {
    void* p1 = smalloc(100);
    sfree(p1);
    void* p2 = smalloc(100);
    OS_ASSERT_PTR_EQ(p2, p1);
    OS_ASSERT_STATS(0, 0, 1, 100);
}

OS_TEST(a_large_free_block_is_reused_for_a_small_request_and_counts_as_fully_used) {
    /* note 12: if a 1000 byte block is used for a 10 byte request, the whole
     * 1000 bytes stop being free. note 1: the block keeps its size of 1000. */
    void* p = smalloc(1000);
    sfree(p);
    OS_ASSERT_STATS(1, 1000, 1, 1000);

    void* q = smalloc(10);
    OS_ASSERT_PTR_EQ(q, p);
    OS_ASSERT_STATS(0, 0, 1, 1000);

    sfree(q);
    OS_ASSERT_STATS(1, 1000, 1, 1000);
}

OS_TEST(a_free_block_that_is_too_small_is_not_reused) {
    void* p1 = smalloc(100);
    sfree(p1);
    void* p2 = smalloc(200);
    OS_ASSERT_PTR_NE(p2, p1);
    OS_ASSERT_STATS(1, 100, 2, 300);
}

OS_TEST(free_blocks_are_searched_in_ascending_address_order) {
    /* note 2: with free blocks at 0x1000 and 0x2000, take the one at 0x1000. */
    void* p1 = smalloc(100);
    void* p2 = smalloc(200);
    void* p3 = smalloc(300);
    sfree(p1);
    sfree(p3);
    OS_ASSERT_STATS(2, 400, 3, 600);

    void* a = smalloc(50); /* fits in both free blocks -> take the lower one */
    OS_ASSERT_MSG(a == p1,
                  "there are free blocks of 100 bytes at %p and 300 bytes at %p; a 50 byte "
                  "request must reuse the one with the lower address (%p), but smalloc returned %p",
                  p1, p3, p1, a);
    OS_ASSERT_STATS(1, 300, 3, 600);

    void* b = smalloc(250); /* only the 300 byte block fits */
    OS_ASSERT_PTR_EQ(b, p3);
    OS_ASSERT_STATS(0, 0, 3, 600);
    (void)p2;
}

OS_TEST(a_used_block_is_never_handed_out_twice) {
    void* p1 = smalloc(100);
    void* p2 = smalloc(100);
    void* p3 = smalloc(100);
    OS_ASSERT_PTR_NE(p1, p2);
    OS_ASSERT_PTR_NE(p2, p3);
    OS_ASSERT_PTR_NE(p1, p3);

    sfree(p2);
    void* p4 = smalloc(100); /* must be p2's block, not p1's or p3's */
    OS_ASSERT_PTR_EQ(p4, p2);
    OS_ASSERT_STATS(0, 0, 3, 300);
}

OS_TEST(everything_is_reused_after_freeing_everything) {
    void* p1 = smalloc(100);
    void* p2 = smalloc(200);
    void* p3 = smalloc(300);
    sfree(p1);
    sfree(p2);
    sfree(p3);
    OS_ASSERT_STATS(3, 600, 3, 600);

    OS_ASSERT_PTR_EQ(smalloc(100), p1);
    OS_ASSERT_PTR_EQ(smalloc(200), p2);
    OS_ASSERT_PTR_EQ(smalloc(300), p3);
    OS_ASSERT_STATS(0, 0, 3, 600);
}

/* ------------------------------------------------------------------ */
/* scalloc                                                             */
/* ------------------------------------------------------------------ */

OS_TEST(scalloc_rejects_zero_arguments) {
    OS_ASSERT_NULL(scalloc(0, 10));
    OS_ASSERT_NULL(scalloc(10, 0));
    OS_ASSERT_NULL(scalloc(0, 0));
    OS_ASSERT_STATS(0, 0, 0, 0);
}

OS_TEST(scalloc_rejects_products_above_10_pow_8) {
    OS_ASSERT_NULL(scalloc(MAX_ALLOC + 1, 1));
    OS_ASSERT_NULL(scalloc(1, MAX_ALLOC + 1));
    OS_ASSERT_NULL(scalloc(2, MAX_ALLOC / 2 + 1)); /* 100000002 > 10^8 */
    OS_ASSERT_NULL(scalloc(50000, 2001));          /* 100050000 > 10^8 */
    OS_ASSERT_STATS(0, 0, 0, 0);
}

OS_TEST(scalloc_rejects_a_product_that_overflows) {
    /* The spec says: if 'size * num' is more than 10^8, return NULL. The real
     * product here is astronomically more than 10^8 - but computing num * size in
     * a size_t wraps around and can look tiny, sailing straight through a naive
     * guard. Check the product WITHOUT computing it (e.g. num > MAX / size). */
    OS_ASSERT_NULL(scalloc(((size_t)1 << 62) + 1, 4)); /* the product wraps to 4 */
    OS_ASSERT_NULL(scalloc(4, ((size_t)1 << 62) + 1));
    OS_ASSERT_NULL(scalloc(((size_t)1 << 61) + 1, 8)); /* wraps to 8 */
    OS_ASSERT_NULL(scalloc((size_t)-1, 2));
    OS_ASSERT_NULL(scalloc(2, (size_t)-1));
    OS_ASSERT_STATS(0, 0, 0, 0);
}

OS_TEST(scalloc_accepts_a_product_of_exactly_10_pow_8) {
    char* p = (char*)scalloc(MAX_ALLOC / 4, 4); /* exactly 10^8 bytes */
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_STATS(0, 0, 1, MAX_ALLOC);
    /* spot-check that the whole thing really was zeroed */
    OS_ASSERT_EQ(p[0], 0);
    OS_ASSERT_EQ(p[MAX_ALLOC / 2], 0);
    OS_ASSERT_EQ(p[MAX_ALLOC - 1], 0);
}

OS_TEST(scalloc_allocates_num_times_size_bytes_and_zeroes_them) {
    int* p = (int*)scalloc(25, sizeof(int)); /* 100 bytes */
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_STATS(0, 0, 1, 100);
    OS_ASSERT_ZEROED(p, 100);
    for (int i = 0; i < 25; ++i) p[i] = i;  /* all 25 ints must be ours */
    for (int i = 0; i < 25; ++i) OS_ASSERT_EQ(p[i], i);
}

OS_TEST(scalloc_zeroes_a_dirty_reused_block) {
    void* p = smalloc(100);
    memset(p, 0xFF, 100);
    sfree(p);

    void* q = scalloc(10, 10); /* same block, must come back zeroed */
    OS_ASSERT_PTR_EQ(q, p);
    OS_ASSERT_ZEROED(q, 100);
    OS_ASSERT_STATS(0, 0, 1, 100);
}

OS_TEST(scalloc_zeroes_the_requested_bytes_of_an_oversized_reused_block) {
    void* p = smalloc(1000);
    memset(p, 0xAB, 1000);
    sfree(p);

    void* q = scalloc(1, 40); /* reuses the 1000 byte block; 40 bytes must be 0 */
    OS_ASSERT_PTR_EQ(q, p);
    OS_ASSERT_ZEROED(q, 40);
    OS_ASSERT_STATS(0, 0, 1, 1000); /* note 12: the whole block is used now */
}

OS_TEST(scalloc_and_smalloc_share_the_same_free_blocks) {
    void* p1 = smalloc(100);
    void* p2 = smalloc(100);
    sfree(p1);
    void* q = scalloc(20, 5); /* 100 bytes -> reuse p1, the lowest free block */
    OS_ASSERT_PTR_EQ(q, p1);
    OS_ASSERT_STATS(0, 0, 2, 200);
    (void)p2;
}

/* ------------------------------------------------------------------ */
/* srealloc                                                            */
/* ------------------------------------------------------------------ */

OS_TEST(srealloc_with_null_behaves_like_smalloc) {
    void* p = srealloc(NULL, 100);
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_STATS(0, 0, 1, 100);
    fill_pattern(p, 100, 9);
    OS_ASSERT_PATTERN(p, 100, 9);
}

OS_TEST(srealloc_with_null_reuses_free_blocks_too) {
    void* p = smalloc(500);
    sfree(p);
    void* q = srealloc(NULL, 100);
    OS_ASSERT_PTR_EQ(q, p);
    OS_ASSERT_STATS(0, 0, 1, 500);
}

OS_TEST(srealloc_rejects_size_zero_and_keeps_the_old_block) {
    void* p = smalloc(100);
    fill_pattern(p, 100, 5);

    OS_ASSERT_NULL(srealloc(p, 0));
    OS_ASSERT_NULL(srealloc(NULL, 0));

    /* a failed srealloc() must not free oldp */
    OS_ASSERT_STATS(0, 0, 1, 100);
    OS_ASSERT_PATTERN(p, 100, 5);
}

OS_TEST(srealloc_rejects_sizes_above_10_pow_8_and_keeps_the_old_block) {
    void* p = smalloc(100);
    fill_pattern(p, 100, 5);

    OS_ASSERT_NULL(srealloc(p, MAX_ALLOC + 1));
    OS_ASSERT_NULL(srealloc(p, (size_t)-1));

    OS_ASSERT_STATS(0, 0, 1, 100); /* not freed */
    OS_ASSERT_PATTERN(p, 100, 5);  /* not touched */
}

OS_TEST(srealloc_to_a_smaller_size_reuses_the_same_block) {
    void* p = smalloc(100);
    fill_pattern(p, 100, 4);

    void* q = srealloc(p, 40);
    OS_ASSERT_PTR_EQ(q, p);
    OS_ASSERT_PATTERN(q, 40, 4);
    /* note 1: the block keeps its original size of 100 */
    OS_ASSERT_STATS(0, 0, 1, 100);
}

OS_TEST(srealloc_to_the_same_size_reuses_the_same_block) {
    void* p = smalloc(100);
    fill_pattern(p, 100, 4);
    void* q = srealloc(p, 100);
    OS_ASSERT_PTR_EQ(q, p);
    OS_ASSERT_PATTERN(q, 100, 4);
    OS_ASSERT_STATS(0, 0, 1, 100);
}

OS_TEST(srealloc_to_a_bigger_size_moves_the_data_and_frees_the_old_block) {
    size_t m = _size_meta_data();
    char* p1 = (char*)smalloc(100);
    char* p2 = (char*)smalloc(100); /* so p1 cannot simply grow in place */
    fill_pattern(p1, 100, 6);

    char* q = (char*)srealloc(p1, 200);
    OS_ASSERT_NOT_NULL(q);
    OS_ASSERT_PTR_NE(q, p1);
    OS_ASSERT_PATTERN(q, 100, 6); /* the old contents came along */

    /* the new block is a fresh one at the end of the heap */
    OS_ASSERT_PTR_EQ(q, p2 + 100 + m);
    /* p1's block is now free */
    OS_ASSERT_STATS(1, 100, 3, 400);
}

OS_TEST(srealloc_reuses_a_free_block_that_fits) {
    char* p1 = (char*)smalloc(100);
    char* p2 = (char*)smalloc(300);
    char* p3 = (char*)smalloc(100);
    fill_pattern(p1, 100, 8);

    sfree(p2); /* a 300 byte hole in the middle */
    OS_ASSERT_STATS(1, 300, 3, 500);

    char* q = (char*)srealloc(p1, 200); /* does not fit in 100, but fits in the hole */
    OS_ASSERT_MSG(q == p2,
                  "srealloc() should have reused the free 300 byte block at %p, but it returned %p",
                  (void*)p2, (void*)q);
    OS_ASSERT_PATTERN(q, 100, 8);
    OS_ASSERT_STATS(1, 100, 3, 500); /* p1's block is free now, no new block */
    (void)p3;
}

OS_TEST(srealloc_can_reuse_the_block_it_just_freed_on_a_later_call) {
    char* p = (char*)smalloc(100);
    char* guard = (char*)smalloc(50);
    fill_pattern(p, 100, 2);

    char* q = (char*)srealloc(p, 200); /* p's block becomes free */
    OS_ASSERT_STATS(1, 100, 3, 350);

    void* r = smalloc(80); /* the freed 100 byte block is the lowest fit */
    OS_ASSERT_PTR_EQ(r, p);
    OS_ASSERT_STATS(0, 0, 3, 350);
    OS_ASSERT_PATTERN(q, 100, 2);
    (void)guard;
}

OS_TEST(srealloc_keeps_the_data_across_a_chain_of_growing_reallocs) {
    size_t size = 8;
    char* p = (char*)smalloc(size);
    OS_ASSERT_NOT_NULL(p);
    fill_pattern(p, size, 1);

    for (int step = 0; step < 12; ++step) {
        size_t bigger = size * 2;
        char* q = (char*)srealloc(p, bigger);
        OS_ASSERT_NOT_NULL(q);
        OS_ASSERT_PATTERN(q, size, 1); /* everything that was there is still there */
        /* keep the prefix pattern and extend it over the new bytes */
        fill_pattern(q, bigger, 1);
        p = q;
        size = bigger;
    }
    OS_ASSERT_PATTERN(p, size, 1);
    OS_ASSERT_INVARIANTS();
}

OS_TEST(srealloc_of_a_block_that_was_reused_uses_the_block_size_not_the_request) {
    /* A 1000 byte block reused for a 10 byte request still *is* 1000 bytes
     * (note 1), so growing it to 500 must reuse it in place. */
    void* p = smalloc(1000);
    sfree(p);
    void* q = smalloc(10);
    OS_ASSERT_PTR_EQ(q, p);

    void* r = srealloc(q, 500);
    OS_ASSERT_MSG(r == q,
                  "the block is 1000 bytes (its size never changes), so growing the request to "
                  "500 bytes must reuse it in place: expected %p, got %p",
                  q, r);
    OS_ASSERT_STATS(0, 0, 1, 1000);
}

/* ------------------------------------------------------------------ */
/* the kernel's view, not the allocator's own bookkeeping              */
/* ------------------------------------------------------------------ */

OS_TEST(the_heap_grows_by_exactly_what_the_blocks_need) {
    /* The statistics are the allocator's own numbers, so they cannot show that
     * sbrk() was called for more (or fewer) bytes than the blocks actually take.
     * The program break can. */
    size_t m = _size_meta_data();
    char* before = (char*)sbrk(0);

    smalloc(100);
    smalloc(200);
    smalloc(64);
    size_t expected = (100 + m) + (200 + m) + (64 + m);

    char* after = (char*)sbrk(0);
    size_t grew = (size_t)(after - before);
    OS_ASSERT_MSG(grew == expected,
                  "three blocks of 100, 200 and 64 bytes need %llu bytes of heap (each one plus "
                  "%llu bytes of metadata), but the program break moved by %llu",
                  (unsigned long long)expected, (unsigned long long)m, (unsigned long long)grew);
}

OS_TEST(reusing_a_free_block_does_not_grow_the_heap) {
    void* p = smalloc(1000);
    sfree(p);

    char* before = (char*)sbrk(0);
    void* q = smalloc(500); /* must come out of the free block, not out of sbrk() */
    char* after = (char*)sbrk(0);

    OS_ASSERT_PTR_EQ(q, p);
    OS_ASSERT_MSG(before == after,
                  "there was a free 1000 byte block, but the program break still moved from %p to "
                  "%p: the block was not reused",
                  (void*)before, (void*)after);
}

OS_TEST(a_block_born_inside_srealloc_is_a_normal_block) {
    /* A block created by srealloc() must be as complete as one created by
     * smalloc(): freeable, reusable, and correctly sized. A missing field in the
     * metadata of a realloc-born block is invisible until you free it. */
    char* p = (char*)smalloc(100);
    char* guard = (char*)smalloc(50);
    fill_pattern(p, 100, 3);

    char* q = (char*)srealloc(p, 400); /* q is born inside srealloc() */
    OS_ASSERT_NOT_NULL(q);
    OS_ASSERT_PATTERN(q, 100, 3);
    OS_ASSERT_STATS(1, 100, 3, 550);

    sfree(q); /* freeing it must mark its 400 bytes free, not something else */
    OS_ASSERT_STATS(2, 500, 3, 550);

    void* r = smalloc(400); /* and its block must be reusable */
    OS_ASSERT_PTR_EQ(r, q);
    OS_ASSERT_STATS(1, 100, 3, 550);
    (void)guard;
}

/* ------------------------------------------------------------------ */
/* differential tests against the reference model                      */
/* ------------------------------------------------------------------ */

OS_TEST(model_alloc_free_cycles) {
    BasicSim sim;
    void* a = B_MALLOC(sim, 100);
    void* b = B_MALLOC(sim, 200);
    void* c = B_MALLOC(sim, 50);
    B_FREE(sim, b);
    void* d = B_MALLOC(sim, 150); /* reuses b */
    B_FREE(sim, a);
    void* e = B_CALLOC(sim, 10, 10); /* reuses a */
    B_REALLOC(sim, c, 20);           /* in place */
    B_REALLOC(sim, e, 400);          /* has to move */
    B_FREE(sim, d);
    B_VERIFY(sim);
}

OS_TEST(stress_random_operations_match_the_reference_model) {
    BasicSim sim;
    Rng rng(20240613);
    std::vector<void*> live;

    for (int step = 0; step < 4000; ++step) {
        unsigned int op = rng.next() % 100;

        if (live.empty() || op < 40) {
            size_t size = rng.range(1, 2000);
            void* p = B_MALLOC(sim, size);
            if (p) live.push_back(p);
        } else if (op < 55) {
            size_t num = rng.range(1, 32);
            size_t size = rng.range(1, 64);
            void* p = B_CALLOC(sim, num, size);
            if (p) live.push_back(p);
        } else if (op < 85) {
            size_t i = rng.range(0, live.size() - 1);
            B_FREE(sim, live[i]);
            live[i] = live.back();
            live.pop_back();
        } else {
            size_t i = rng.range(0, live.size() - 1);
            size_t size = rng.range(1, 3000);
            void* p = B_REALLOC(sim, live[i], size);
            if (p) live[i] = p;
        }

        if (step % 250 == 0) B_VERIFY(sim);
    }

    B_VERIFY(sim);

    /* free everything: every block must end up free, and nothing may vanish */
    size_t blocks_before = _num_allocated_blocks();
    size_t bytes_before = _num_allocated_bytes();
    for (size_t i = 0; i < live.size(); ++i) B_FREE(sim, live[i]);
    OS_ASSERT_STATS(blocks_before, bytes_before, blocks_before, bytes_before);
}

OS_TEST(stress_same_size_blocks_are_all_reused) {
    /* 200 blocks of the same size, freed and re-allocated: the allocator must
     * never grow the heap on the second round. */
    const int N = 200;
    void* ptrs[N];
    for (int i = 0; i < N; ++i) {
        ptrs[i] = smalloc(128);
        OS_ASSERT_NOT_NULL(ptrs[i]);
    }
    OS_ASSERT_STATS(0, 0, N, N * 128);

    for (int i = 0; i < N; ++i) sfree(ptrs[i]);
    OS_ASSERT_STATS(N, N * 128, N, N * 128);

    for (int i = 0; i < N; ++i) {
        void* p = smalloc(128);
        OS_ASSERT_MSG(p == ptrs[i], "block #%d should have been reused: expected %p, got %p", i,
                      ptrs[i], p);
    }
    OS_ASSERT_STATS(0, 0, N, N * 128);
}

OS_TEST_MAIN("malloc_2 - basic malloc")
