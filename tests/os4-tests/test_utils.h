#ifndef OS_TEST_UTILS_H
#define OS_TEST_UTILS_H

/*
 * Minimal fork-per-test harness for the HW4 malloc assignment.
 *
 * Every test runs in its own child process. That matters a lot here: the
 * allocator keeps global state (the block list / the buddy heap) that cannot be
 * reset, so without forking each test would have to reason about whatever the
 * previous tests left behind. With a fresh child per test, every test starts
 * with an untouched program break and can assert *absolute* values.
 *
 * Diagnostics are written with write(2) rather than the C++ streams, so tests
 * that deliberately exhaust memory (setrlimit) can still report failures.
 *
 * Usage:
 *     OS_TEST(name_of_test) { ... asserts ... }
 *     OS_TEST_MAIN("title")
 *
 * Command line:
 *     ./test2                 run everything
 *     ./test2 realloc         run only tests whose name contains "realloc"
 *     ./test2 --list          list test names
 *     ./test2 --no-fork xyz   run in-process (for gdb / valgrind)
 */

#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "os_malloc.h"

namespace ostest {

/* The largest size the allocators are required to serve (10^8). */
const size_t MAX_ALLOC = 100000000;

const int TEST_TIMEOUT_SECONDS = 60;

/* ------------------------------------------------------------------------- */
/* output                                                                     */
/* ------------------------------------------------------------------------- */

inline bool& color_enabled() {
    static bool enabled = false;
    return enabled;
}
inline const char* C_RED() { return color_enabled() ? "\033[1;31m" : ""; }
inline const char* C_GREEN() { return color_enabled() ? "\033[1;32m" : ""; }
inline const char* C_YELLOW() { return color_enabled() ? "\033[1;33m" : ""; }
inline const char* C_DIM() { return color_enabled() ? "\033[2m" : ""; }
inline const char* C_OFF() { return color_enabled() ? "\033[0m" : ""; }

inline void emit(const char* s) {
    ssize_t written = ::write(1, s, ::strlen(s));
    (void)written;
}

inline void fail_v(const char* file, int line, const char* what, const char* fmt,
                   va_list ap) __attribute__((noreturn));

inline void fail_v(const char* file, int line, const char* what, const char* fmt,
                   va_list ap) {
    char buf[4096];
    size_t n = 0;
    int k = ::snprintf(buf, sizeof(buf), "\n    %sFAILED%s at %s:%d\n      expected: %s\n      ",
                       C_RED(), C_OFF(), file, line, what);
    if (k > 0) n = (size_t)k;
    if (n < sizeof(buf) - 2) {
        k = ::vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, ap);
        if (k > 0) n += (size_t)k;
    }
    if (n > sizeof(buf) - 2) n = sizeof(buf) - 2;
    buf[n] = '\n';
    buf[n + 1] = '\0';
    emit(buf);
    _exit(1);
}

inline void fail(const char* file, int line, const char* what, const char* fmt, ...)
    __attribute__((noreturn, format(printf, 4, 5)));

inline void fail(const char* file, int line, const char* what, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fail_v(file, line, what, fmt, ap);
}

inline void warn(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

inline void warn(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int k = ::snprintf(buf, sizeof(buf), "\n    %sWARNING%s  ", C_YELLOW(), C_OFF());
    size_t n = k > 0 ? (size_t)k : 0;
    if (n < sizeof(buf) - 2) {
        k = ::vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, ap);
        if (k > 0) n += (size_t)k;
    }
    va_end(ap);
    if (n > sizeof(buf) - 2) n = sizeof(buf) - 2;
    buf[n] = '\n';
    buf[n + 1] = '\0';
    emit(buf);
}

inline void note(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

inline void note(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int k = ::snprintf(buf, sizeof(buf), "\n    %snote:%s ", C_DIM(), C_OFF());
    size_t n = k > 0 ? (size_t)k : 0;
    if (n < sizeof(buf) - 2) {
        k = ::vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, ap);
        if (k > 0) n += (size_t)k;
    }
    va_end(ap);
    if (n > sizeof(buf) - 2) n = sizeof(buf) - 2;
    buf[n] = '\n';
    buf[n + 1] = '\0';
    emit(buf);
}

/* ------------------------------------------------------------------------- */
/* assertions                                                                 */
/* ------------------------------------------------------------------------- */

#define OS_ASSERT(cond)                                                                  \
    do {                                                                                 \
        if (!(cond)) ::ostest::fail(__FILE__, __LINE__, #cond, "the expression is false"); \
    } while (0)

#define OS_ASSERT_MSG(cond, ...)                                                         \
    do {                                                                                 \
        if (!(cond)) ::ostest::fail(__FILE__, __LINE__, #cond, __VA_ARGS__);             \
    } while (0)

#define OS_ASSERT_EQ(actual, expected)                                                   \
    do {                                                                                 \
        unsigned long long a__ = (unsigned long long)(actual);                           \
        unsigned long long e__ = (unsigned long long)(expected);                         \
        if (a__ != e__)                                                                  \
            ::ostest::fail(__FILE__, __LINE__, #actual " == " #expected,                 \
                           "expected %llu, got %llu", e__, a__);                         \
    } while (0)

#define OS_ASSERT_NE(actual, unexpected)                                                 \
    do {                                                                                 \
        unsigned long long a__ = (unsigned long long)(actual);                           \
        unsigned long long u__ = (unsigned long long)(unexpected);                       \
        if (a__ == u__)                                                                  \
            ::ostest::fail(__FILE__, __LINE__, #actual " != " #unexpected,               \
                           "got %llu", a__);                                             \
    } while (0)

#define OS_ASSERT_LE(a, b)                                                               \
    do {                                                                                 \
        unsigned long long a__ = (unsigned long long)(a);                                \
        unsigned long long b__ = (unsigned long long)(b);                                \
        if (!(a__ <= b__))                                                               \
            ::ostest::fail(__FILE__, __LINE__, #a " <= " #b, "%llu is greater than %llu",\
                           a__, b__);                                                    \
    } while (0)

#define OS_ASSERT_GE(a, b)                                                               \
    do {                                                                                 \
        unsigned long long a__ = (unsigned long long)(a);                                \
        unsigned long long b__ = (unsigned long long)(b);                                \
        if (!(a__ >= b__))                                                               \
            ::ostest::fail(__FILE__, __LINE__, #a " >= " #b, "%llu is smaller than %llu",\
                           a__, b__);                                                    \
    } while (0)

#define OS_ASSERT_PTR_EQ(actual, expected)                                               \
    do {                                                                                 \
        const void* a__ = (const void*)(actual);                                         \
        const void* e__ = (const void*)(expected);                                       \
        if (a__ != e__)                                                                  \
            ::ostest::fail(__FILE__, __LINE__, #actual " == " #expected,                 \
                           "expected pointer %p, got %p", e__, a__);                     \
    } while (0)

#define OS_ASSERT_PTR_NE(actual, unexpected)                                             \
    do {                                                                                 \
        const void* a__ = (const void*)(actual);                                         \
        const void* u__ = (const void*)(unexpected);                                     \
        if (a__ == u__)                                                                  \
            ::ostest::fail(__FILE__, __LINE__, #actual " != " #unexpected,               \
                           "both are %p", a__);                                          \
    } while (0)

#define OS_ASSERT_NULL(p)                                                                \
    do {                                                                                 \
        const void* p__ = (const void*)(p);                                              \
        if (p__ != NULL)                                                                 \
            ::ostest::fail(__FILE__, __LINE__, #p " == NULL", "got %p", p__);            \
    } while (0)

#define OS_ASSERT_NOT_NULL(p)                                                            \
    do {                                                                                 \
        if ((const void*)(p) == NULL)                                                    \
            ::ostest::fail(__FILE__, __LINE__, #p " != NULL", "the allocation failed");  \
    } while (0)

/* ------------------------------------------------------------------------- */
/* memory content helpers                                                     */
/* ------------------------------------------------------------------------- */

/* A cheap, position dependent byte pattern: a block filled with pattern `seed`
 * cannot be confused with a block filled with any other seed, and an overlap
 * between two blocks shows up as a mismatch. */
inline unsigned char pattern_byte(unsigned int seed, size_t i) {
    unsigned int v = seed * 2654435761u + (unsigned int)i * 40503u + (unsigned int)(i >> 16) * 7u;
    return (unsigned char)((v >> 13) ^ v ^ 0x5A);
}

inline void fill_pattern(void* p, size_t n, unsigned int seed) {
    unsigned char* c = (unsigned char*)p;
    for (size_t i = 0; i < n; ++i) c[i] = pattern_byte(seed, i);
}

/* index of the first corrupted byte, or -1 if the whole range is intact */
inline long check_pattern(const void* p, size_t n, unsigned int seed) {
    const unsigned char* c = (const unsigned char*)p;
    for (size_t i = 0; i < n; ++i)
        if (c[i] != pattern_byte(seed, i)) return (long)i;
    return -1;
}

inline long first_nonzero(const void* p, size_t n) {
    const unsigned char* c = (const unsigned char*)p;
    for (size_t i = 0; i < n; ++i)
        if (c[i] != 0) return (long)i;
    return -1;
}

#define OS_ASSERT_PATTERN(p, n, seed)                                                    \
    do {                                                                                 \
        const void* p__ = (const void*)(p);                                              \
        size_t n__ = (size_t)(n);                                                        \
        unsigned int s__ = (unsigned int)(seed);                                         \
        long bad__ = ::ostest::check_pattern(p__, n__, s__);                             \
        if (bad__ >= 0)                                                                  \
            ::ostest::fail(__FILE__, __LINE__, "the contents of " #p " to be intact",    \
                           "byte %ld of %llu was corrupted (expected 0x%02x, found 0x%02x)", \
                           bad__, (unsigned long long)n__,                               \
                           ::ostest::pattern_byte(s__, (size_t)bad__),                   \
                           ((const unsigned char*)p__)[bad__]);                          \
    } while (0)

#define OS_ASSERT_ZEROED(p, n)                                                           \
    do {                                                                                 \
        const void* p__ = (const void*)(p);                                              \
        size_t n__ = (size_t)(n);                                                        \
        long bad__ = ::ostest::first_nonzero(p__, n__);                                  \
        if (bad__ >= 0)                                                                  \
            ::ostest::fail(__FILE__, __LINE__, "the " #n " bytes at " #p " to be zeroed",\
                           "byte %ld is 0x%02x, not 0", bad__,                           \
                           ((const unsigned char*)p__)[bad__]);                          \
    } while (0)

/* [a, a+na) and [b, b+nb) must not intersect */
#define OS_ASSERT_DISJOINT(a, na, b, nb)                                                 \
    do {                                                                                 \
        uintptr_t a__ = (uintptr_t)(a), b__ = (uintptr_t)(b);                            \
        uintptr_t ae__ = a__ + (uintptr_t)(na), be__ = b__ + (uintptr_t)(nb);            \
        if (a__ < be__ && b__ < ae__)                                                    \
            ::ostest::fail(__FILE__, __LINE__, "two live allocations not to overlap",    \
                           "[%p, %p) overlaps [%p, %p)", (void*)a__, (void*)ae__,        \
                           (void*)b__, (void*)be__);                                     \
    } while (0)

/* ------------------------------------------------------------------------- */
/* misc                                                                       */
/* ------------------------------------------------------------------------- */

/* Cap how far the program break may travel, so that sbrk() (and, on Linux >=
 * 4.7, private anonymous mmap()) starts failing. Used to check that the
 * allocators report failure instead of crashing. */
inline void limit_heap_growth(size_t bytes) {
    struct rlimit rl;
    rl.rlim_cur = (rlim_t)bytes;
    rl.rlim_max = (rlim_t)bytes;
    if (::setrlimit(RLIMIT_DATA, &rl) != 0) {
        warn("SKIPPED: RLIMIT_DATA cannot be lowered on this system, so sbrk() cannot be made to "
             "fail. Run this on the Linux VM to test the failure path.");
        _exit(0);
    }
}

/* Deterministic RNG, so a failing stress test can be reproduced exactly. */
struct Rng {
    unsigned int state;
    explicit Rng(unsigned int seed) : state(seed) {}
    unsigned int next() {
        state = state * 1103515245u + 12345u;
        return (state >> 16) & 0x7FFF;
    }
    /* uniform in [lo, hi] */
    size_t range(size_t lo, size_t hi) {
        unsigned int r = (next() << 15) ^ next();
        return lo + (size_t)(r % (unsigned int)(hi - lo + 1));
    }
};

/* ------------------------------------------------------------------------- */
/* registry + runner                                                          */
/* ------------------------------------------------------------------------- */

typedef void (*TestFn)();

struct TestCase {
    const char* name;
    TestFn fn;
};

const int MAX_TESTS = 256;

inline TestCase* registry() {
    static TestCase tests[MAX_TESTS];
    return tests;
}
inline int& test_count() {
    static int n = 0;
    return n;
}

struct Registrar {
    Registrar(const char* name, TestFn fn) {
        int& n = test_count();
        if (n < MAX_TESTS) {
            registry()[n].name = name;
            registry()[n].fn = fn;
            ++n;
        }
    }
};

inline int run_all(const char* title, int argc, char** argv) {
    const char* filter = NULL;
    bool nofork = false;

    for (int i = 1; i < argc; ++i) {
        if (::strcmp(argv[i], "--list") == 0) {
            for (int t = 0; t < test_count(); ++t) ::printf("%s\n", registry()[t].name);
            return 0;
        } else if (::strcmp(argv[i], "--no-fork") == 0) {
            nofork = true;
        } else if (argv[i][0] == '-') {
            ::printf("usage: %s [--list] [--no-fork] [name-substring]\n", argv[0]);
            return 2;
        } else {
            filter = argv[i];
        }
    }

    color_enabled() = ::isatty(1) != 0;
    ::printf("\n===== %s =====\n\n", title);

    int total = 0, passed = 0, failed = 0;

    for (int t = 0; t < test_count(); ++t) {
        const TestCase& tc = registry()[t];
        if (filter && ::strstr(tc.name, filter) == NULL) continue;

        ++total;
        ::printf("  %3d. %-62s", total, tc.name);
        ::fflush(stdout);

        if (nofork) {
            tc.fn();
            ::printf(" %sPASS%s\n", C_GREEN(), C_OFF());
            ++passed;
            continue;
        }

        int fds[2];
        if (::pipe(fds) != 0) {
            ::perror("pipe");
            return 2;
        }

        ::fflush(stdout);
        ::fflush(stderr);
        pid_t pid = ::fork();
        if (pid < 0) {
            ::perror("fork");
            return 2;
        }
        if (pid == 0) {
            /* child: everything the test prints is captured, so that the parent
             * can print it *after* the verdict line. */
            ::close(fds[0]);
            ::dup2(fds[1], 1);
            ::dup2(fds[1], 2);
            ::close(fds[1]);
            ::alarm(TEST_TIMEOUT_SECONDS);
            tc.fn();
            _exit(0);
        }

        ::close(fds[1]);
        static char captured[65536];
        size_t used = 0;
        for (;;) {
            if (used >= sizeof(captured) - 1) {
                char sink[4096];
                while (::read(fds[0], sink, sizeof(sink)) > 0) {
                }
                break;
            }
            ssize_t r = ::read(fds[0], captured + used, sizeof(captured) - 1 - used);
            if (r <= 0) break;
            used += (size_t)r;
        }
        captured[used] = '\0';
        ::close(fds[0]);

        int status = 0;
        ::waitpid(pid, &status, 0);

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            ::printf(" %sPASS%s\n", C_GREEN(), C_OFF());
            ++passed;
        } else if (WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM) {
            ::printf(" %sTIMEOUT%s (still running after %d s - infinite loop?)\n", C_RED(),
                     C_OFF(), TEST_TIMEOUT_SECONDS);
            ++failed;
        } else if (WIFSIGNALED(status)) {
            ::printf(" %sCRASH%s (%s)\n", C_RED(), C_OFF(), ::strsignal(WTERMSIG(status)));
            ++failed;
        } else {
            ::printf(" %sFAIL%s\n", C_RED(), C_OFF());
            ++failed;
        }

        ::fflush(stdout);
        if (used) emit(captured);
    }

    ::fflush(stdout);
    if (failed == 0)
        ::printf("\n  %sall %d tests passed%s\n\n", C_GREEN(), passed, C_OFF());
    else
        ::printf("\n  %d/%d passed, %s%d failed%s\n\n", passed, total, C_RED(), failed, C_OFF());
    ::fflush(stdout);

    return failed ? 1 : 0;
}

}  // namespace ostest

#define OS_TEST(name)                                                                    \
    static void name();                                                                  \
    static ::ostest::Registrar os_registrar_##name(#name, name);                         \
    static void name()

#define OS_TEST_MAIN(title)                                                              \
    int main(int argc, char** argv) { return ::ostest::run_all(title, argc, argv); }

#endif /* OS_TEST_UTILS_H */
