#ifndef OS_BASIC_MODEL_H
#define OS_BASIC_MODEL_H

/*
 * A reference model of the part 2 allocator, plus a "simulator" that drives the
 * real allocator and the model side by side and compares them after *every*
 * operation.
 *
 * The model is a direct reading of the spec:
 *   - the blocks form one list, in ascending address order (= creation order,
 *     since blocks are only ever appended with sbrk() and never split, merged
 *     or moved);
 *   - smalloc() reuses the FIRST free block that is big enough (note 2), and
 *     only appends a new block when no free block fits;
 *   - a reused block keeps its original size, and the whole block counts as
 *     used (notes 1 and 12);
 *   - srealloc() reuses the current block when the request fits in it,
 *     otherwise it finds/allocates another block, copies, and frees the old one.
 *
 * The model deliberately does not predict absolute addresses. It remembers the
 * address the real allocator gave each block, and checks that a *reused* block
 * comes back at exactly that address. (Predicting addresses would mean assuming
 * the blocks are contiguous in the heap, which breaks the moment glibc's own
 * malloc - used by the test's STL containers - moves the program break.)
 */

#include <map>
#include <vector>

#include "stats_utils.h"

namespace ostest {

class BasicSim {
   public:
    BasicSim() : next_seed_(1) {}

    void* do_smalloc(size_t size, const char* file, int line) {
        void* real = ::smalloc(size);
        if (size == 0 || size > MAX_ALLOC) {
            if (real != NULL)
                fail(file, line, "smalloc() to reject the request",
                     "smalloc(%llu) returned %p instead of NULL", (unsigned long long)size, real);
            check(file, line);
            return NULL;
        }
        size_t index = place(real, size, file, line, "smalloc");
        start_life(real, index, size);
        check(file, line);
        return real;
    }

    void* do_scalloc(size_t num, size_t size, const char* file, int line) {
        void* real = ::scalloc(num, size);
        unsigned long long total = (unsigned long long)num * (unsigned long long)size;
        if (num == 0 || size == 0 || total > MAX_ALLOC) {
            if (real != NULL)
                fail(file, line, "scalloc() to reject the request",
                     "scalloc(%llu, %llu) returned %p instead of NULL", (unsigned long long)num,
                     (unsigned long long)size, real);
            check(file, line);
            return NULL;
        }
        size_t index = place(real, (size_t)total, file, line, "scalloc");
        long bad = first_nonzero(real, (size_t)total);
        if (bad >= 0)
            fail(file, line, "scalloc() to zero the memory it returns",
                 "byte %ld of the %llu bytes returned by scalloc(%llu, %llu) is 0x%02x", bad, total,
                 (unsigned long long)num, (unsigned long long)size,
                 ((unsigned char*)real)[bad]);
        start_life(real, index, (size_t)total);
        check(file, line);
        return real;
    }

    void do_sfree(void* p, const char* file, int line) {
        if (p == NULL) {
            ::sfree(NULL);
            check(file, line);
            return;
        }
        Live live = lookup(p, file, line);
        long bad = check_pattern(p, live.size, live.seed);
        if (bad >= 0)
            fail(file, line, "the block to still hold its data when it is freed",
                 "byte %ld of the block at %p was corrupted while it was alive", bad, p);

        ::sfree(p);
        blocks_[live.index].is_free = true;
        live_.erase(p);
        check(file, line);
    }

    void* do_srealloc(void* oldp, size_t size, const char* file, int line) {
        if (oldp == NULL) {
            void* real = ::srealloc(NULL, size);
            if (size == 0 || size > MAX_ALLOC) {
                if (real != NULL)
                    fail(file, line, "srealloc(NULL, bad size) to return NULL", "got %p", real);
                check(file, line);
                return NULL;
            }
            size_t index = place(real, size, file, line, "srealloc(NULL, ...)");
            start_life(real, index, size);
            check(file, line);
            return real;
        }

        Live live = lookup(oldp, file, line);

        if (size == 0 || size > MAX_ALLOC) {
            /* a failing srealloc() must not touch oldp at all */
            void* real = ::srealloc(oldp, size);
            if (real != NULL)
                fail(file, line, "srealloc() to reject the request",
                     "srealloc(%p, %llu) returned %p instead of NULL", oldp,
                     (unsigned long long)size, real);
            long bad = check_pattern(oldp, live.size, live.seed);
            if (bad >= 0)
                fail(file, line, "a failed srealloc() to leave oldp untouched",
                     "byte %ld of the old block at %p was corrupted", bad, oldp);
            check(file, line); /* in particular: oldp must NOT have been freed */
            return NULL;
        }

        size_t old_block_size = blocks_[live.index].size;

        if (size <= old_block_size) {
            /* "If size is smaller than or equal to the current block's size,
             * reuse the same block" - same pointer, and no statistic changes. */
            void* real = ::srealloc(oldp, size);
            if (real != oldp)
                fail(file, line, "srealloc() to reuse the current block",
                     "the request (%llu bytes) fits in the current block (%llu bytes), so "
                     "srealloc() must return oldp (%p), but it returned %p",
                     (unsigned long long)size, (unsigned long long)old_block_size, oldp, real);
            long bad = check_pattern(real, size < live.size ? size : live.size, live.seed);
            if (bad >= 0)
                fail(file, line, "srealloc() to preserve the data of the block it reuses",
                     "byte %ld of the block at %p was corrupted", bad, real);
            live_.erase(oldp);
            start_life(real, live.index, size);
            check(file, line);
            return real;
        }

        /* The request does not fit: srealloc() has to find another block. The
         * old block is still in use while the search happens, so it can never be
         * the one that is picked. */
        size_t target = first_fit(size);
        void* expected = (target != NPOS) ? blocks_[target].addr : NULL;

        void* real = ::srealloc(oldp, size);
        if (real == NULL)
            fail(file, line, "srealloc() to succeed", "srealloc(%p, %llu) returned NULL", oldp,
                 (unsigned long long)size);

        if (target != NPOS) {
            if (real != expected)
                fail(file, line, "srealloc() to reuse the first free block that is big enough",
                     "there is a free block of %llu bytes at %p, but srealloc(%p, %llu) returned %p",
                     (unsigned long long)blocks_[target].size, expected, oldp,
                     (unsigned long long)size, real);
            blocks_[target].is_free = false;
        } else {
            target = append(real, size, file, line, "srealloc");
        }

        long bad = check_pattern(real, live.size, live.seed);
        if (bad >= 0)
            fail(file, line, "srealloc() to copy the old contents into the new block",
                 "byte %ld (of the %llu bytes that were in the old block) is wrong in the new "
                 "block at %p",
                 bad, (unsigned long long)live.size, real);

        blocks_[live.index].is_free = true; /* the old block is released */
        live_.erase(oldp);
        start_life(real, target, size);
        check(file, line);
        return real;
    }

    /* every live block still holds the pattern it was filled with */
    void verify_all(const char* file, int line) {
        for (std::map<void*, Live>::iterator it = live_.begin(); it != live_.end(); ++it) {
            long bad = check_pattern(it->first, it->second.size, it->second.seed);
            if (bad >= 0)
                fail(file, line, "every live allocation to keep its contents",
                     "byte %ld of the %llu byte block at %p was corrupted by another allocation",
                     bad, (unsigned long long)it->second.size, it->first);
        }
    }

    void check(const char* file, int line) {
        size_t free_blocks = 0, free_bytes = 0, alloc_bytes = 0;
        for (size_t i = 0; i < blocks_.size(); ++i) {
            alloc_bytes += blocks_[i].size;
            if (blocks_[i].is_free) {
                ++free_blocks;
                free_bytes += blocks_[i].size;
            }
        }
        check_stats(file, line, free_blocks, free_bytes, blocks_.size(), alloc_bytes);
    }

    size_t num_live() const { return live_.size(); }

    void* live_at(size_t n) const {
        std::map<void*, Live>::const_iterator it = live_.begin();
        for (size_t i = 0; i < n && it != live_.end(); ++i) ++it;
        return it == live_.end() ? NULL : it->first;
    }

   private:
    static const size_t NPOS = (size_t)-1;

    struct Block {
        size_t size;
        bool is_free;
        void* addr;
    };
    struct Live {
        size_t index;
        size_t size;
        unsigned int seed;
    };

    size_t first_fit(size_t size) const {
        for (size_t i = 0; i < blocks_.size(); ++i)
            if (blocks_[i].is_free && blocks_[i].size >= size) return i;
        return NPOS;
    }

    Live lookup(void* p, const char* file, int line) {
        std::map<void*, Live>::iterator it = live_.find(p);
        if (it == live_.end())
            fail(file, line, "the simulator to know this pointer",
                 "%p is not a live allocation (test bug)", p);
        return it->second;
    }

    /* Decide where this allocation must have landed, and check it did. */
    size_t place(void* real, size_t size, const char* file, int line, const char* who) {
        size_t index = first_fit(size);
        if (index == NPOS) return append(real, size, file, line, who);

        if (real == NULL)
            fail(file, line, "the allocation to succeed",
                 "%s(%llu) returned NULL although there is a free block of %llu bytes at %p", who,
                 (unsigned long long)size, (unsigned long long)blocks_[index].size,
                 blocks_[index].addr);
        if (real != blocks_[index].addr)
            fail(file, line, "the first free block that is big enough to be reused",
                 "%s(%llu) should have reused the free %llu byte block at %p (the lowest address "
                 "that fits), but it returned %p",
                 who, (unsigned long long)size, (unsigned long long)blocks_[index].size,
                 blocks_[index].addr, real);
        blocks_[index].is_free = false;
        return index;
    }

    size_t append(void* real, size_t size, const char* file, int line, const char* who) {
        if (real == NULL)
            fail(file, line, "the allocation to succeed", "%s(%llu) returned NULL", who,
                 (unsigned long long)size);
        for (size_t i = 0; i < blocks_.size(); ++i)
            if (blocks_[i].addr == real)
                fail(file, line, "a brand new block to be handed out",
                     "%s(%llu) returned %p, which is block #%llu - it is still in use", who,
                     (unsigned long long)size, real, (unsigned long long)i);
        Block b;
        b.size = size;
        b.is_free = false;
        b.addr = real;
        blocks_.push_back(b);
        return blocks_.size() - 1;
    }

    void start_life(void* p, size_t index, size_t size) {
        Live l;
        l.index = index;
        l.size = size;
        l.seed = next_seed_++;
        fill_pattern(p, size, l.seed);
        live_[p] = l;
    }

    std::vector<Block> blocks_;
    std::map<void*, Live> live_;
    unsigned int next_seed_;
};

}  // namespace ostest

#define B_MALLOC(sim, size) (sim).do_smalloc((size), __FILE__, __LINE__)
#define B_CALLOC(sim, num, size) (sim).do_scalloc((num), (size), __FILE__, __LINE__)
#define B_FREE(sim, p) (sim).do_sfree((p), __FILE__, __LINE__)
#define B_REALLOC(sim, p, size) (sim).do_srealloc((p), (size), __FILE__, __LINE__)
#define B_VERIFY(sim) (sim).verify_all(__FILE__, __LINE__)

#endif /* OS_BASIC_MODEL_H */
