#ifndef OS_BUDDY_MODEL_H
#define OS_BUDDY_MODEL_H

/*
 * Constants, helpers and a reference model for the part 3 buddy allocator.
 *
 * The spec makes the allocator fully deterministic, so the model can predict
 * both the statistics and the exact address of every allocation:
 *
 *   - the heap is 32 blocks of 128KB (orders 0..10, order i = 128 << i bytes),
 *     created on the first call to an allocation function;
 *   - smalloc() takes the free block with the SMALLEST size that fits, and
 *     among those the one with the LOWEST address (challenge 0);
 *   - a block is split in half while the request still fits in the lower half,
 *     the lower half serves the request and the upper half stays free
 *     (challenge 1);
 *   - sfree() merges a block with its buddy while the buddy is free, but never
 *     past order 10 (challenge 2);
 *   - size + _size_meta_data() > 128KB is served by mmap() instead, and given
 *     back with munmap() (challenge 3).
 *
 * Buddies are computed on offsets from the start of the region (offset XOR
 * block size), which is exactly the recommended trick and does not depend on
 * the region's absolute alignment.
 */

#include <map>
#include <set>
#include <vector>

#include "stats_utils.h"

namespace ostest {

const size_t MIN_BLOCK_SIZE = 128;
const int BUDDY_MAX_ORDER = 10;
const int NUM_ORDERS = BUDDY_MAX_ORDER + 1;
const size_t MAX_BLOCK_SIZE = MIN_BLOCK_SIZE << BUDDY_MAX_ORDER; /* 128KB */
const size_t NUM_INITIAL_BLOCKS = 32;
const size_t HEAP_TOTAL_BYTES = NUM_INITIAL_BLOCKS * MAX_BLOCK_SIZE; /* 4MB */

inline size_t block_size(int order) { return MIN_BLOCK_SIZE << order; }

/* usable bytes of a block of this order, i.e. what the statistics count */
inline size_t usable_size(int order) { return block_size(order) - meta_size(); }

/* the biggest request that is still served from the heap */
inline size_t max_heap_request() { return MAX_BLOCK_SIZE - meta_size(); }

inline bool needs_mmap(size_t size) { return size + meta_size() > MAX_BLOCK_SIZE; }

/* the order of the block that must serve `size`, or -1 if it needs mmap() */
inline int order_for(size_t size) {
    for (int order = 0; order <= BUDDY_MAX_ORDER; ++order)
        if (block_size(order) >= size + meta_size()) return order;
    return -1;
}

/* statistics of an untouched heap (32 free blocks of order 10) */
inline size_t fresh_blocks() { return NUM_INITIAL_BLOCKS; }
inline size_t fresh_bytes() { return NUM_INITIAL_BLOCKS * usable_size(BUDDY_MAX_ORDER); }

#define OS_ASSERT_FRESH_HEAP() \
    OS_ASSERT_STATS(::ostest::fresh_blocks(), ::ostest::fresh_bytes(), ::ostest::fresh_blocks(), \
                    ::ostest::fresh_bytes())

/* ------------------------------------------------------------------------- */
/* the model                                                                  */
/* ------------------------------------------------------------------------- */

class BuddyRef {
   public:
    BuddyRef() : initialised_(false) {}

    void ensure_init() {
        if (initialised_) return;
        for (size_t i = 0; i < NUM_INITIAL_BLOCKS; ++i)
            free_[BUDDY_MAX_ORDER].insert(i * MAX_BLOCK_SIZE);
        initialised_ = true;
    }

    /* tightest fit, lowest address; false if no block is big enough */
    bool alloc_heap(int need, size_t* out_offset) {
        ensure_init();
        int order = -1;
        for (int o = need; o <= BUDDY_MAX_ORDER; ++o) {
            if (!free_[o].empty()) {
                order = o;
                break;
            }
        }
        if (order < 0) return false;

        size_t offset = *free_[order].begin();
        free_[order].erase(free_[order].begin());
        while (order > need) { /* split: lower half is used, upper half is freed */
            --order;
            free_[order].insert(offset + block_size(order));
        }
        used_[offset] = need;
        *out_offset = offset;
        return true;
    }

    void free_heap(size_t offset) {
        std::map<size_t, int>::iterator it = used_.find(offset);
        int order = it->second;
        used_.erase(it);

        while (order < BUDDY_MAX_ORDER) {
            size_t buddy = offset ^ block_size(order);
            std::set<size_t>::iterator b = free_[order].find(buddy);
            if (b == free_[order].end()) break;
            free_[order].erase(b);
            if (buddy < offset) offset = buddy;
            ++order;
        }
        free_[order].insert(offset);
    }

    /* Can this block reach `need` by repeatedly merging with free buddies?
     * Merging stops as soon as the block is big enough. */
    bool can_merge_to(size_t offset, int order, int need, size_t* out_offset) const {
        while (order < need) {
            if (order >= BUDDY_MAX_ORDER) return false;
            size_t buddy = offset ^ block_size(order);
            if (free_[order].find(buddy) == free_[order].end()) return false;
            if (buddy < offset) offset = buddy;
            ++order;
        }
        *out_offset = offset;
        return true;
    }

    void merge_to(size_t offset, int order, int need) {
        used_.erase(offset);
        while (order < need) {
            size_t buddy = offset ^ block_size(order);
            free_[order].erase(buddy);
            if (buddy < offset) offset = buddy;
            ++order;
        }
        used_[offset] = need;
    }

    int order_of(size_t offset) const {
        std::map<size_t, int>::const_iterator it = used_.find(offset);
        return it == used_.end() ? -1 : it->second;
    }

    void add_mmap(size_t size) {
        ensure_init();
        mmaps_.insert(std::make_pair(next_mmap_id(), size));
    }
    void remove_mmap(size_t size) {
        for (std::map<int, size_t>::iterator it = mmaps_.begin(); it != mmaps_.end(); ++it) {
            if (it->second == size) {
                mmaps_.erase(it);
                return;
            }
        }
    }

    size_t num_free_blocks() const {
        size_t n = 0;
        for (int o = 0; o <= BUDDY_MAX_ORDER; ++o) n += free_[o].size();
        return n;
    }
    size_t num_free_bytes() const {
        size_t n = 0;
        for (int o = 0; o <= BUDDY_MAX_ORDER; ++o) n += free_[o].size() * usable_size(o);
        return n;
    }
    size_t num_allocated_blocks() const {
        return num_free_blocks() + used_.size() + mmaps_.size();
    }
    size_t num_allocated_bytes() const {
        size_t n = num_free_bytes();
        for (std::map<size_t, int>::const_iterator it = used_.begin(); it != used_.end(); ++it)
            n += usable_size(it->second);
        for (std::map<int, size_t>::const_iterator it = mmaps_.begin(); it != mmaps_.end(); ++it)
            n += it->second;
        return n;
    }

   private:
    int next_mmap_id() {
        static int id = 0;
        return ++id;
    }

    std::set<size_t> free_[NUM_ORDERS]; /* offsets from the start of the region */
    std::map<size_t, int> used_;        /* offset -> order */
    std::map<int, size_t> mmaps_;       /* id -> requested size */
    bool initialised_;
};

/* ------------------------------------------------------------------------- */
/* the simulator: drives the real allocator and the model together            */
/* ------------------------------------------------------------------------- */

class BuddySim {
   public:
    BuddySim() : base_(0), base_known_(false), next_seed_(1) {}

    void* do_smalloc(size_t size, const char* file, int line) {
        void* real = ::smalloc(size);

        if (size == 0 || size > MAX_ALLOC) {
            if (real != NULL)
                fail(file, line, "smalloc() to reject the request",
                     "smalloc(%llu) returned %p instead of NULL", (unsigned long long)size, real);
            check(file, line);
            return NULL;
        }

        int order = order_for(size);
        if (order < 0) {
            if (real == NULL)
                fail(file, line, "the mmap() allocation to succeed", "smalloc(%llu) returned NULL",
                     (unsigned long long)size);
            ref_.add_mmap(size);
            begin_life(real, true, 0, -1, size);
        } else {
            size_t offset = 0;
            bool ok = ref_.alloc_heap(order, &offset);
            if (!ok) {
                if (real != NULL)
                    fail(file, line, "smalloc() to fail when the heap is full",
                         "no free block of order %d (or bigger) is left, but smalloc(%llu) "
                         "returned %p",
                         order, (unsigned long long)size, real);
            } else {
                if (real == NULL)
                    fail(file, line, "the allocation to succeed",
                         "smalloc(%llu) returned NULL although a free block of order %d exists",
                         (unsigned long long)size, order);
                learn_base(real);
                expect_address(real, offset, order, size, file, line, "smalloc");
                begin_life(real, false, offset, order, size);
            }
        }
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

        size_t bytes = (size_t)total;
        int order = order_for(bytes);
        if (order < 0) {
            if (real == NULL)
                fail(file, line, "the mmap() allocation to succeed",
                     "scalloc(%llu, %llu) returned NULL", (unsigned long long)num,
                     (unsigned long long)size);
            ref_.add_mmap(bytes);
        } else {
            size_t offset = 0;
            bool ok = ref_.alloc_heap(order, &offset);
            if (!ok) {
                if (real != NULL)
                    fail(file, line, "scalloc() to fail when the heap is full",
                         "no free block of order %d is left, but scalloc returned %p", order, real);
                check(file, line);
                return NULL;
            }
            if (real == NULL)
                fail(file, line, "the allocation to succeed",
                     "scalloc(%llu, %llu) returned NULL although a free block of order %d exists",
                     (unsigned long long)num, (unsigned long long)size, order);
            learn_base(real);
            expect_address(real, offset, order, bytes, file, line, "scalloc");
            long bad = first_nonzero(real, bytes);
            if (bad >= 0)
                fail(file, line, "scalloc() to zero the memory it returns",
                     "byte %ld of the %llu bytes at %p is 0x%02x", bad, total, real,
                     ((unsigned char*)real)[bad]);
            begin_life(real, false, offset, order, bytes);
            check(file, line);
            return real;
        }

        long bad = first_nonzero(real, bytes);
        if (bad >= 0)
            fail(file, line, "scalloc() to zero the memory it returns",
                 "byte %ld of the %llu bytes at %p is 0x%02x", bad, total, real,
                 ((unsigned char*)real)[bad]);
        begin_life(real, true, 0, -1, bytes);
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
                 "byte %ld of the %llu byte block at %p was corrupted while it was alive", bad,
                 (unsigned long long)live.size, p);

        ::sfree(p);
        if (live.mmapped)
            ref_.remove_mmap(live.size);
        else
            ref_.free_heap(live.offset);
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
            int order = order_for(size);
            if (order < 0) {
                if (real == NULL)
                    fail(file, line, "the allocation to succeed", "srealloc(NULL, %llu) failed",
                         (unsigned long long)size);
                ref_.add_mmap(size);
                begin_life(real, true, 0, -1, size);
            } else {
                size_t offset = 0;
                if (!ref_.alloc_heap(order, &offset)) {
                    if (real != NULL)
                        fail(file, line, "srealloc() to fail when the heap is full", "got %p",
                             real);
                    check(file, line);
                    return NULL;
                }
                if (real == NULL)
                    fail(file, line, "the allocation to succeed", "srealloc(NULL, %llu) failed",
                         (unsigned long long)size);
                learn_base(real);
                expect_address(real, offset, order, size, file, line, "srealloc(NULL, ...)");
                begin_life(real, false, offset, order, size);
            }
            check(file, line);
            return real;
        }

        Live live = lookup(oldp, file, line);

        if (size == 0 || size > MAX_ALLOC) {
            void* real = ::srealloc(oldp, size);
            if (real != NULL)
                fail(file, line, "srealloc() to reject the request",
                     "srealloc(%p, %llu) returned %p instead of NULL", oldp,
                     (unsigned long long)size, real);
            long bad = check_pattern(oldp, live.size, live.seed);
            if (bad >= 0)
                fail(file, line, "a failed srealloc() to leave oldp untouched",
                     "byte %ld of the old block at %p was corrupted", bad, oldp);
            check(file, line); /* oldp must NOT have been freed */
            return NULL;
        }

        if (live.mmapped) return realloc_mmapped(oldp, live, size, file, line);
        return realloc_heap(oldp, live, size, file, line);
    }

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
        check_stats(file, line, ref_.num_free_blocks(), ref_.num_free_bytes(),
                    ref_.num_allocated_blocks(), ref_.num_allocated_bytes());
    }

    size_t num_live() const { return live_.size(); }
    bool is_mmapped(void* p) { return live_[p].mmapped; }
    size_t size_of(void* p) { return live_[p].size; }

   private:
    struct Live {
        bool mmapped;
        size_t offset; /* heap offset, only if !mmapped */
        int order;     /* only if !mmapped */
        size_t size;   /* what the user asked for */
        unsigned int seed;
    };

    void* realloc_mmapped(void* oldp, const Live& live, size_t size, const char* file, int line) {
        if (size == live.size) {
            /* "unless old_size == new_size" - the only case where the block is reused */
            void* real = ::srealloc(oldp, size);
            if (real != oldp)
                fail(file, line, "srealloc() of an mmap()ed block to the same size to reuse it",
                     "expected %p, got %p", oldp, real);
            check(file, line);
            return real;
        }

        void* real = ::srealloc(oldp, size);
        if (real == NULL)
            fail(file, line, "the reallocation to succeed", "srealloc(%p, %llu) returned NULL",
                 oldp, (unsigned long long)size);
        if (real == oldp)
            fail(file, line, "srealloc() of an mmap()ed block to allocate a new block",
                 "a resized mmap()ed block must never be reused, but srealloc() returned the old "
                 "pointer %p",
                 oldp);

        size_t keep = size < live.size ? size : live.size;
        long bad = check_pattern(real, keep, live.seed);
        if (bad >= 0)
            fail(file, line, "srealloc() to copy the old contents",
                 "byte %ld of the %llu copied bytes is wrong", bad, (unsigned long long)keep);

        ref_.remove_mmap(live.size);
        ref_.add_mmap(size);
        live_.erase(oldp);
        begin_life(real, true, 0, -1, size);
        check(file, line);
        return real;
    }

    void* realloc_heap(void* oldp, const Live& live, size_t size, const char* file, int line) {
        int need = order_for(size);
        if (need < 0)
            fail(file, line, "the test not to grow a heap block past 128KB",
                 "test bug: the spec says this case is not tested");

        /* (a) the current block is big enough */
        if (need <= live.order) {
            void* real = ::srealloc(oldp, size);
            if (real != oldp)
                fail(file, line, "srealloc() to reuse the current block",
                     "%llu bytes still fit in the current block of order %d, so srealloc() must "
                     "return oldp (%p), but it returned %p",
                     (unsigned long long)size, live.order, oldp, real);
            size_t keep = size < live.size ? size : live.size;
            long bad = check_pattern(real, keep, live.seed);
            if (bad >= 0)
                fail(file, line, "srealloc() to preserve the data of the block it reuses",
                     "byte %ld was corrupted", bad);
            live_.erase(oldp);
            begin_life(real, false, live.offset, live.order, size);
            check(file, line);
            return real;
        }

        /* (b) merging with free buddies is enough */
        size_t merged_offset = 0;
        if (ref_.can_merge_to(live.offset, live.order, need, &merged_offset)) {
            ref_.merge_to(live.offset, live.order, need);
            void* expected = (void*)(base_ + merged_offset + meta_size());
            void* real = ::srealloc(oldp, size);
            if (real != expected)
                fail(file, line, "srealloc() to merge the block with its free buddies",
                     "the block at %p and its free buddies form a block of order %d that fits "
                     "%llu bytes, so srealloc() must return %p, but it returned %p",
                     oldp, need, (unsigned long long)size, expected, real);
            long bad = check_pattern(real, live.size, live.seed);
            if (bad >= 0)
                fail(file, line, "srealloc() to keep the data when it merges buddies",
                     "byte %ld of the %llu bytes that were in the old block is wrong", bad,
                     (unsigned long long)live.size);
            live_.erase(oldp);
            begin_life(real, false, merged_offset, need, size);
            check(file, line);
            return real;
        }

        /* (c) another block has to serve the request; oldp is only freed if that
         *     succeeds, and it is still in use while the search happens */
        size_t offset = 0;
        bool ok = ref_.alloc_heap(need, &offset);
        if (!ok) {
            void* real = ::srealloc(oldp, size);
            if (real != NULL)
                fail(file, line, "srealloc() to fail when nothing can serve the request",
                     "the heap has no free block of order %d and the block cannot be merged into "
                     "one, but srealloc() returned %p",
                     need, real);
            long bad = check_pattern(oldp, live.size, live.seed);
            if (bad >= 0)
                fail(file, line, "a failed srealloc() to leave oldp untouched",
                     "byte %ld of the old block was corrupted", bad);
            check(file, line); /* and oldp must not have been freed */
            return NULL;
        }

        void* expected = (void*)(base_ + offset + meta_size());
        void* real = ::srealloc(oldp, size);
        if (real != expected)
            fail(file, line, "srealloc() to move the block into the tightest free block",
                 "expected %p (order %d), got %p", expected, need, real);
        long bad = check_pattern(real, live.size, live.seed);
        if (bad >= 0)
            fail(file, line, "srealloc() to copy the old contents into the new block",
                 "byte %ld of the %llu copied bytes is wrong", bad, (unsigned long long)live.size);

        ref_.free_heap(live.offset); /* the old block is released (and merged) */
        live_.erase(oldp);
        begin_life(real, false, offset, need, size);
        check(file, line);
        return real;
    }

    Live lookup(void* p, const char* file, int line) {
        std::map<void*, Live>::iterator it = live_.find(p);
        if (it == live_.end())
            fail(file, line, "the simulator to know this pointer",
                 "%p is not a live allocation (test bug)", p);
        return it->second;
    }

    void learn_base(void* first_heap_pointer) {
        if (base_known_) return;
        /* The very first heap allocation always comes out of the first (lowest)
         * 128KB block, so it starts exactly at the beginning of the region. */
        base_ = (uintptr_t)first_heap_pointer - meta_size();
        base_known_ = true;
    }

    void expect_address(void* real, size_t offset, int order, size_t size, const char* file,
                        int line, const char* who) {
        void* expected = (void*)(base_ + offset + meta_size());
        if (real != expected)
            fail(file, line, "the tightest fitting free block with the lowest address to be used",
                 "%s(%llu) needs a block of order %d (%llu bytes); the one it must use is at "
                 "offset %llu of the region, i.e. %p, but it returned %p",
                 who, (unsigned long long)size, order, (unsigned long long)block_size(order),
                 (unsigned long long)offset, expected, real);
    }

    void begin_life(void* p, bool mmapped, size_t offset, int order, size_t size) {
        Live l;
        l.mmapped = mmapped;
        l.offset = offset;
        l.order = order;
        l.size = size;
        l.seed = next_seed_++;
        fill_pattern(p, size, l.seed);
        live_[p] = l;
    }

    BuddyRef ref_;
    std::map<void*, Live> live_;
    uintptr_t base_;
    bool base_known_;
    unsigned int next_seed_;
};

}  // namespace ostest

#define D_MALLOC(sim, size) (sim).do_smalloc((size), __FILE__, __LINE__)
#define D_CALLOC(sim, num, size) (sim).do_scalloc((num), (size), __FILE__, __LINE__)
#define D_FREE(sim, p) (sim).do_sfree((p), __FILE__, __LINE__)
#define D_REALLOC(sim, p, size) (sim).do_srealloc((p), (size), __FILE__, __LINE__)
#define D_VERIFY(sim) (sim).verify_all(__FILE__, __LINE__)

#endif /* OS_BUDDY_MODEL_H */
