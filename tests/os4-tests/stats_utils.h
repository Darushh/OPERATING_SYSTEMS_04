#ifndef OS_STATS_UTILS_H
#define OS_STATS_UTILS_H

/*
 * Helpers for the six statistics functions (parts 2, 3 and 4).
 *
 * OS_ASSERT_STATS(free_blocks, free_bytes, allocated_blocks, allocated_bytes)
 * checks those four numbers *and* _num_meta_data_bytes(), which must always be
 * allocated_blocks * _size_meta_data(). On a mismatch it prints the whole table
 * so you can see at a glance which counter drifted.
 *
 * Not included by test_malloc_1.cpp: part 1 only has to define smalloc().
 */

#include "test_utils.h"

namespace ostest {

inline size_t meta_size() { return _size_meta_data(); }

struct Stats {
    size_t free_blocks;
    size_t free_bytes;
    size_t allocated_blocks;
    size_t allocated_bytes;
    size_t meta_data_bytes;
};

inline Stats read_stats() {
    Stats s;
    s.free_blocks = _num_free_blocks();
    s.free_bytes = _num_free_bytes();
    s.allocated_blocks = _num_allocated_blocks();
    s.allocated_bytes = _num_allocated_bytes();
    s.meta_data_bytes = _num_meta_data_bytes();
    return s;
}

inline void check_stats(const char* file, int line, size_t exp_free_blocks,
                        size_t exp_free_bytes, size_t exp_alloc_blocks,
                        size_t exp_alloc_bytes) {
    Stats s = read_stats();
    size_t exp_meta = exp_alloc_blocks * meta_size();

    if (s.free_blocks == exp_free_blocks && s.free_bytes == exp_free_bytes &&
        s.allocated_blocks == exp_alloc_blocks && s.allocated_bytes == exp_alloc_bytes &&
        s.meta_data_bytes == exp_meta) {
        return;
    }

    char buf[2048];
    size_t n = 0;
    int k = ::snprintf(buf, sizeof(buf),
                       "\n    %sFAILED%s at %s:%d\n      the statistics do not match\n\n"
                       "        %-24s %14s %14s\n",
                       C_RED(), C_OFF(), file, line, "", "expected", "actual");
    if (k > 0) n = (size_t)k;

#define OS_STAT_ROW(label, got, exp)                                                     \
    do {                                                                                 \
        if (n < sizeof(buf) - 1) {                                                       \
            int k2 = ::snprintf(buf + n, sizeof(buf) - n, "        %-24s %14llu %14llu%s\n", \
                                label, (unsigned long long)(exp), (unsigned long long)(got), \
                                ((got) == (exp)) ? "" : "   <-- wrong");                  \
            if (k2 > 0) n += (size_t)k2;                                                 \
        }                                                                                \
    } while (0)

    OS_STAT_ROW("_num_free_blocks()", s.free_blocks, exp_free_blocks);
    OS_STAT_ROW("_num_free_bytes()", s.free_bytes, exp_free_bytes);
    OS_STAT_ROW("_num_allocated_blocks()", s.allocated_blocks, exp_alloc_blocks);
    OS_STAT_ROW("_num_allocated_bytes()", s.allocated_bytes, exp_alloc_bytes);
    OS_STAT_ROW("_num_meta_data_bytes()", s.meta_data_bytes, exp_meta);
#undef OS_STAT_ROW

    if (n < sizeof(buf) - 1) {
        int k3 = ::snprintf(buf + n, sizeof(buf) - n,
                            "\n      (_size_meta_data() = %llu; _num_meta_data_bytes() must "
                            "always be\n       _num_allocated_blocks() * _size_meta_data())\n",
                            (unsigned long long)meta_size());
        if (k3 > 0) n += (size_t)k3;
    }

    emit(buf);
    _exit(1);
}

/* Invariants that must hold after *every* operation, whatever the workload. */
inline void check_invariants(const char* file, int line) {
    Stats s = read_stats();
    if (s.free_blocks > s.allocated_blocks)
        fail(file, line, "_num_free_blocks() <= _num_allocated_blocks()",
             "%llu free blocks but only %llu allocated blocks",
             (unsigned long long)s.free_blocks, (unsigned long long)s.allocated_blocks);
    if (s.free_bytes > s.allocated_bytes)
        fail(file, line, "_num_free_bytes() <= _num_allocated_bytes()",
             "%llu free bytes but only %llu allocated bytes",
             (unsigned long long)s.free_bytes, (unsigned long long)s.allocated_bytes);
    if (s.meta_data_bytes != s.allocated_blocks * meta_size())
        fail(file, line, "_num_meta_data_bytes() == _num_allocated_blocks() * _size_meta_data()",
             "%llu != %llu * %llu", (unsigned long long)s.meta_data_bytes,
             (unsigned long long)s.allocated_blocks, (unsigned long long)meta_size());
}

}  // namespace ostest

#define OS_ASSERT_STATS(free_blocks, free_bytes, alloc_blocks, alloc_bytes)              \
    ::ostest::check_stats(__FILE__, __LINE__, (size_t)(free_blocks), (size_t)(free_bytes), \
                          (size_t)(alloc_blocks), (size_t)(alloc_bytes))

#define OS_ASSERT_INVARIANTS() ::ostest::check_invariants(__FILE__, __LINE__)

#endif /* OS_STATS_UTILS_H */
