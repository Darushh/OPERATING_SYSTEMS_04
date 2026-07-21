/*
 * Tests for part 4 (optional) - huge pages (malloc_4.cpp).
 *
 * Part 4 keeps everything from part 3 and changes only how big allocations are
 * backed, so this program runs the whole part 3 suite (malloc3_tests.h) and then
 * adds the huge page rules:
 *
 *   - smalloc(size) with size >= 4MB          -> huge page
 *   - scalloc(num, size) with size > 2MB      -> huge page  (size is ONE block!)
 *   - anything else                           -> exactly as in part 3
 *
 * Note that the scalloc rule looks at the size of a single element, not at the
 * product: scalloc(4, 1MB) allocates 4MB but must NOT use a huge page.
 *
 * Whether a mapping really sits on huge pages is read back from
 * /proc/self/smaps: a hugetlb mapping reports "KernelPageSize: 2048 kB", and a
 * transparent huge page reports a non-zero "AnonHugePages". If the machine has
 * no huge pages available at all (vm.nr_hugepages = 0 and THP off) the check
 * cannot mean anything, so it degrades to a warning - reserve some with
 *     sudo sysctl -w vm.nr_hugepages=64
 * to get the real check.
 */

#include "malloc3_tests.h"
#include "proc_utils.h"

static const size_t MB = 1024 * 1024;

/* ------------------------------------------------------------------ */
/* the huge page rules                                                 */
/* ------------------------------------------------------------------ */

OS_TEST(report_the_huge_page_setup_of_this_machine) {
    note("HugePages_Total > 0: %s, transparent huge pages: %s",
         proc::hugetlb_pages_reserved() ? "yes" : "no", proc::thp_enabled() ? "on" : "off");
    if (!proc::hugetlb_pages_reserved() && !proc::thp_enabled())
        warn("this machine cannot serve huge pages, so the huge page tests below can only check "
             "that the allocations work - not that they are huge. Reserve some with "
             "'sudo sysctl -w vm.nr_hugepages=128'.");
}

OS_TEST(smalloc_of_exactly_4MB_uses_a_huge_page) {
    size_t size = 4 * MB;
    char* p = (char*)smalloc(size);
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_HUGE(p, "smalloc(4MB)");

    fill_pattern(p, size, 41);
    OS_ASSERT_PATTERN(p, size, 41);

    size_t heap = 32 * usable_size(BUDDY_MAX_ORDER);
    OS_ASSERT_STATS(32, heap, 33, heap + size);

    sfree(p);
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(smalloc_of_more_than_4MB_uses_a_huge_page) {
    size_t size = 9 * MB + 1234;
    char* p = (char*)smalloc(size);
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_HUGE(p, "smalloc(9MB)");

    fill_pattern(p, size, 42);
    OS_ASSERT_PATTERN(p, size, 42);

    size_t heap = 32 * usable_size(BUDDY_MAX_ORDER);
    OS_ASSERT_STATS(32, heap, 33, heap + size);

    sfree(p);
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(smalloc_just_below_4MB_is_a_normal_allocation) {
    size_t size = 4 * MB - 1;
    char* p = (char*)smalloc(size);
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_NOT_HUGE(p, "smalloc(4MB - 1)");

    fill_pattern(p, size, 43);
    OS_ASSERT_PATTERN(p, size, 43);

    size_t heap = 32 * usable_size(BUDDY_MAX_ORDER);
    OS_ASSERT_STATS(32, heap, 33, heap + size);
}

OS_TEST(scalloc_with_elements_larger_than_2MB_uses_a_huge_page) {
    size_t size = 3 * MB; /* one element is bigger than 2MB */
    char* p = (char*)scalloc(2, size);
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_HUGE(p, "scalloc(2, 3MB)");
    OS_ASSERT_ZEROED(p, 2 * size);

    size_t heap = 32 * usable_size(BUDDY_MAX_ORDER);
    OS_ASSERT_STATS(32, heap, 33, heap + 2 * size);

    sfree(p);
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(scalloc_with_elements_of_exactly_2MB_is_a_normal_allocation) {
    /* "only do this if the size of one block is LARGER than 2MB" - 2MB is not */
    size_t size = 2 * MB;
    char* p = (char*)scalloc(2, size); /* 4MB in total, but that does not matter */
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_NOT_HUGE(p, "scalloc(2, 2MB)");
    OS_ASSERT_ZEROED(p, 2 * size);
}

OS_TEST(scalloc_of_many_small_elements_is_a_normal_allocation) {
    /* 8MB in total, but each element is only 1MB: the smalloc rule (>= 4MB) must
     * not be applied to a scalloc */
    size_t size = 1 * MB;
    char* p = (char*)scalloc(8, size);
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_NOT_HUGE(p, "scalloc(8, 1MB)");
    OS_ASSERT_ZEROED(p, 8 * size);

    size_t heap = 32 * usable_size(BUDDY_MAX_ORDER);
    OS_ASSERT_STATS(32, heap, 33, heap + 8 * size);
}

OS_TEST(huge_page_allocations_are_freed_properly) {
    char* a = (char*)smalloc(4 * MB);   /* huge page */
    char* b = (char*)scalloc(1, 5 * MB); /* huge page */
    char* c = (char*)smalloc(100);      /* an ordinary buddy allocation */
    OS_ASSERT_NOT_NULL(a);
    OS_ASSERT_NOT_NULL(b);
    OS_ASSERT_NOT_NULL(c);
    fill_pattern(a, 4 * MB, 44);
    OS_ASSERT_ZEROED(b, 5 * MB);

    /* the heap holds one allocation (42 - o blocks), plus the two mapped blocks */
    int o = order_for(100);
    size_t free_bytes = free_bytes_after_single(o);
    OS_ASSERT_STATS(41 - o, free_bytes, 42 - o + 2,
                    free_bytes + usable_size(o) + 4 * MB + 5 * MB);

    OS_ASSERT_PATTERN(a, 4 * MB, 44);

    sfree(a);
    sfree(b);
    OS_ASSERT_SINGLE_ALLOCATION(o);

    sfree(c);
    OS_ASSERT_FRESH_HEAP();
}

OS_TEST(srealloc_of_a_huge_allocation_still_works) {
    size_t old_size = 4 * MB;
    size_t new_size = 6 * MB;
    char* p = (char*)smalloc(old_size);
    OS_ASSERT_NOT_NULL(p);
    fill_pattern(p, old_size, 45);

    char* q = (char*)srealloc(p, new_size);
    OS_ASSERT_NOT_NULL(q);
    OS_ASSERT_PTR_NE(q, p); /* an mmap()ed block is never resized in place */
    OS_ASSERT_PATTERN(q, old_size, 45);
    OS_ASSERT_HUGE(q, "srealloc(4MB block, 6MB)");

    size_t heap = 32 * usable_size(BUDDY_MAX_ORDER);
    OS_ASSERT_STATS(32, heap, 33, heap + new_size);

    /* the old huge mapping must be gone, and the new one must be freeable too */
    OS_ASSERT_UNMAPPED(p, "the old 4MB huge block, after srealloc() moved it");
    sfree(q);
    OS_ASSERT_UNMAPPED(q, "a 6MB huge block created by srealloc()");
    OS_ASSERT_FRESH_HEAP();
}

/* ------------------------------------------------------------------ */
/* the two things the statistics cannot show                           */
/* ------------------------------------------------------------------ */

OS_TEST(freeing_a_huge_allocation_really_unmaps_it) {
    /* A huge page mapping is rounded up to a multiple of the huge page size. If
     * sfree() hands munmap() the un-rounded length (or forgets that this block
     * was a huge one at all), munmap() fails with EINVAL - and nothing tells you:
     * the block is already unlinked, the statistics say it is gone, and the
     * memory stays mapped for the lifetime of the process. */
    char* p = (char*)smalloc(4 * MB);
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_HUGE(p, "smalloc(4MB)");

    sfree(p);
    OS_ASSERT_FRESH_HEAP(); /* the allocator says it is gone... */
    OS_ASSERT_UNMAPPED(p, "a 4MB huge block"); /* ...but is it? */
}

OS_TEST(repeatedly_allocating_and_freeing_huge_blocks_does_not_leak) {
    if (!proc::available()) {
        warn("/proc is not available, skipping");
        return;
    }
    void* warmup = smalloc(1);
    sfree(warmup);

    unsigned long before = proc::vm_size_kb();
    OS_ASSERT_MSG(before > 0, "could not read VmSize from /proc/self/status");

    for (int i = 0; i < 20; ++i) {
        char* p = (char*)smalloc(4 * MB);
        OS_ASSERT_MSG(p != NULL, "huge allocation #%d failed", i);
        p[0] = 1;
        p[4 * MB - 1] = 1;
        sfree(p);
    }

    unsigned long after = proc::vm_size_kb();
    unsigned long grew = after > before ? after - before : 0;
    OS_ASSERT_MSG(grew <= 8 * 1024,
                  "the process grew by %lu kB after allocating and freeing a 4MB huge block twenty "
                  "times. Those mappings are being leaked: sfree() is not unmapping them (the "
                  "length passed to munmap() has to match the one that was mmap()ed, including the "
                  "huge page rounding).",
                  grew);
}

OS_TEST(srealloc_does_not_forget_that_a_block_came_from_smalloc) {
    /* The huge page threshold depends on where the block was ORIGINALLY allocated:
     * 4MB for smalloc, but only 2MB per element for scalloc. srealloc() creates a
     * new block, so it has to carry that origin over. If it does not, the new
     * block silently looks scalloc-originated, and the NEXT srealloc applies the
     * 2MB gate instead of the 4MB one - handing out a huge page for a 3.5MB
     * request that must not have one.
     *
     * Note this only shows up on the second realloc: the first one still reads the
     * (correct) flag of the original block. */
    size_t size = 3 * MB;
    char* p = (char*)smalloc(size); /* 3MB < 4MB -> a normal allocation */
    OS_ASSERT_NOT_NULL(p);
    OS_ASSERT_NOT_HUGE(p, "smalloc(3MB)");
    fill_pattern(p, size, 46);

    char* q = (char*)srealloc(p, 3 * MB + 256 * 1024); /* q is born inside srealloc() */
    OS_ASSERT_NOT_NULL(q);
    OS_ASSERT_PATTERN(q, size, 46);
    OS_ASSERT_NOT_HUGE(q, "srealloc(smalloc'd 3MB block, 3.25MB)");

    char* r = (char*)srealloc(q, 3 * MB + 512 * 1024); /* still under 4MB */
    OS_ASSERT_NOT_NULL(r);
    OS_ASSERT_PATTERN(r, size, 46);
    OS_ASSERT_NOT_HUGE(r, "srealloc(a realloc'd smalloc block, 3.5MB - still under 4MB)");

    sfree(r);
    OS_ASSERT_UNMAPPED(r, "a 3.5MB block created by srealloc()");
}

OS_TEST_MAIN("malloc_4 - huge pages (optional)")
