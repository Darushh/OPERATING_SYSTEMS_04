#ifndef OS_PROC_UTILS_H
#define OS_PROC_UTILS_H

/*
 * The kernel's view of the process, read back from /proc.
 *
 * The statistics functions are the allocator's own bookkeeping, so they cannot
 * be trusted to reveal a leak: an sfree() whose munmap() silently failed still
 * decrements the counters, and the memory quietly stays mapped forever. These
 * helpers ask the kernel instead:
 *
 *   is_mapped(p)     is this address still part of any mapping?
 *   vm_size_kb()     total size of every mapping the process has
 *   query(p)         page size of the mapping holding p (huge pages, part 4)
 *
 * Everything degrades to a warning when /proc is unavailable (e.g. macOS).
 */

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test_utils.h"

namespace ostest {
namespace proc {

/* read(2) based: the C++ streams allocate, and we want to stay out of the way of
 * the allocator under test */
inline size_t read_file(const char* path, char* buf, size_t cap) {
    int fd = ::open(path, O_RDONLY);
    if (fd < 0) return 0;
    size_t used = 0;
    while (used + 1 < cap) {
        ssize_t r = ::read(fd, buf + used, cap - 1 - used);
        if (r <= 0) break;
        used += (size_t)r;
    }
    ::close(fd);
    buf[used] = '\0';
    return used;
}

inline bool available() {
    static char buf[64];
    return read_file("/proc/self/statm", buf, sizeof(buf)) > 0;
}

struct Mapping {
    bool found;
    unsigned long start;
    unsigned long end;
    unsigned long kernel_page_kb;
    unsigned long anon_huge_kb;
};

inline Mapping query(const void* addr) {
    static char buf[1 << 21];
    Mapping m;
    m.found = false;
    m.start = m.end = 0;
    m.kernel_page_kb = 0;
    m.anon_huge_kb = 0;

    if (read_file("/proc/self/smaps", buf, sizeof(buf)) == 0) return m;

    unsigned long target = (unsigned long)(uintptr_t)addr;
    bool inside = false;
    char* line = buf;

    while (line && *line) {
        char* eol = ::strchr(line, '\n');
        if (eol) *eol = '\0';

        /* a mapping header looks like "7f0e1c000000-7f0e1c200000 rw-p ..." */
        bool is_header = false;
        char* dash = ::strchr(line, '-');
        if (dash) {
            char* end_of_start = NULL;
            unsigned long start = ::strtoul(line, &end_of_start, 16);
            if (end_of_start == dash) {
                unsigned long end = ::strtoul(dash + 1, NULL, 16);
                is_header = true;
                inside = (target >= start && target < end);
                if (inside) {
                    m.found = true;
                    m.start = start;
                    m.end = end;
                }
            }
        }

        if (!is_header && inside) {
            if (::strncmp(line, "KernelPageSize:", 15) == 0)
                m.kernel_page_kb = ::strtoul(line + 15, NULL, 10);
            else if (::strncmp(line, "AnonHugePages:", 14) == 0)
                m.anon_huge_kb = ::strtoul(line + 14, NULL, 10);
        }

        line = eol ? eol + 1 : NULL;
    }
    return m;
}

/* Is this address still backed by a mapping? After sfree() of an mmap()ed block
 * the answer must be no. */
inline bool is_mapped(const void* addr) { return query(addr).found; }

/* The total size of every mapping in the process, in kB (VmSize). A correct
 * allocate/free cycle leaves this unchanged; a failed munmap() makes it grow. */
inline unsigned long vm_size_kb() {
    static char buf[8192];
    if (read_file("/proc/self/status", buf, sizeof(buf)) == 0) return 0;
    const char* p = ::strstr(buf, "VmSize:");
    if (!p) return 0;
    return ::strtoul(p + 7, NULL, 10);
}

inline bool hugetlb_pages_reserved() {
    static char buf[8192];
    if (read_file("/proc/meminfo", buf, sizeof(buf)) == 0) return false;
    const char* p = ::strstr(buf, "HugePages_Total:");
    if (!p) return false;
    return ::strtoul(p + 16, NULL, 10) > 0;
}

inline bool thp_enabled() {
    static char buf[256];
    if (read_file("/sys/kernel/mm/transparent_hugepage/enabled", buf, sizeof(buf)) == 0)
        return false;
    return ::strstr(buf, "[always]") != NULL || ::strstr(buf, "[madvise]") != NULL;
}

/* part 4: is this allocation on a huge page (or must it not be)? */
inline void expect_huge(const void* p, bool want_huge, const char* what, const char* file,
                        int line) {
    Mapping m = query(p);
    if (!m.found) {
        warn("could not find the mapping for %p in /proc/self/smaps, skipping the huge page check "
             "for %s",
             p, what);
        return;
    }
    bool is_huge = (m.kernel_page_kb >= 2048) || (m.anon_huge_kb > 0);

    if (want_huge && !is_huge) {
        if (!hugetlb_pages_reserved() && !thp_enabled()) {
            warn("%s is not on a huge page (KernelPageSize %lu kB), but this machine has no huge "
                 "pages at all (HugePages_Total = 0, THP off), so the check cannot prove anything. "
                 "Run 'sudo sysctl -w vm.nr_hugepages=128' to test it for real.",
                 what, m.kernel_page_kb);
            return;
        }
        fail(file, line, "the allocation to be backed by huge pages",
             "%s must use a huge page, but the mapping at %p reports KernelPageSize %lu kB and "
             "AnonHugePages %lu kB",
             what, p, m.kernel_page_kb, m.anon_huge_kb);
    }

    if (!want_huge && m.kernel_page_kb >= 2048) {
        fail(file, line, "the allocation NOT to be backed by huge pages",
             "%s is below the threshold and must be allocated normally, but the mapping at %p uses "
             "%lu kB pages",
             what, p, m.kernel_page_kb);
    }
}

}  // namespace proc
}  // namespace ostest

#define OS_ASSERT_HUGE(p, what) ::ostest::proc::expect_huge((p), true, (what), __FILE__, __LINE__)
#define OS_ASSERT_NOT_HUGE(p, what) \
    ::ostest::proc::expect_huge((p), false, (what), __FILE__, __LINE__)

/* the address must no longer be backed by any mapping */
#define OS_ASSERT_UNMAPPED(p, what)                                                          \
    do {                                                                                     \
        const void* p__ = (const void*)(p);                                                  \
        if (!::ostest::proc::available()) {                                                  \
            ::ostest::warn("/proc is not available, skipping the unmap check");              \
        } else if (::ostest::proc::is_mapped(p__)) {                                         \
            ::ostest::fail(__FILE__, __LINE__, "the memory to be given back to the kernel",  \
                           "%s: the block at %p is STILL MAPPED after sfree(). munmap() was " \
                           "never called, or it failed (EINVAL) - check that munmap() gets "  \
                           "exactly the address and the length that mmap() was given. The "   \
                           "statistics cannot show this: they say the block is gone, but the " \
                           "memory is leaked for the lifetime of the process.",               \
                           (what), p__);                                                      \
        }                                                                                    \
    } while (0)

#endif /* OS_PROC_UTILS_H */
