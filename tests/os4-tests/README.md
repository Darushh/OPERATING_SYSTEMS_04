# HW4 test suite

Exhaustive tests for the four parts of the malloc assignment. **Run them on the
Ubuntu VM** — they use `sbrk()`, `mmap()`, `setrlimit()` and `/proc`.

```
make            # build and run parts 1, 2 and 3 against ../hw4/malloc_*.cpp
make 2          # only part 2
make 4          # the optional huge page part
make 3 FILTER=srealloc      # only the part 3 tests with "srealloc" in the name
make 3 ARGS=--list          # list the test names
make 3 ARGS=--no-fork       # run in-process, so gdb/valgrind can see the failure
make SRC_DIR=/somewhere     # if your malloc_*.cpp are not in ../hw4
```

The exit status is non-zero if anything failed, so `make` fails loudly.

## How it works

Each test runs in its **own forked process**. The allocator's state (the block
list, the buddy heap) is global and cannot be reset, so without this every test
would have to reason about whatever the previous ones left behind. A fresh child
per test means every test starts with an untouched program break and can assert
**absolute** numbers — `_num_allocated_blocks() == 42`, not "one more than
before". It also means a segfault or an infinite loop in one test is reported
(`CRASH`, `TIMEOUT`) instead of taking the whole run down.

A failure prints the file, the line, and what it expected. Statistics mismatches
print the whole table:

```
   8. three_allocations_update_every_statistic                        FAIL

    FAILED at test_malloc_2.cpp:98
      the statistics do not match

                                       expected         actual
        _num_free_blocks()                    0              0
        _num_free_bytes()                     0              0
        _num_allocated_blocks()               3              0   <-- wrong
        _num_allocated_bytes()              600              0   <-- wrong
        _num_meta_data_bytes()               96              0   <-- wrong
```

## What is tested

| file | tests | covers |
|---|---|---|
| `test_malloc_1.cpp` | 14 | `smalloc` only: the three failure cases (0, > 10⁸, `sbrk` fails), that the memory is real, that blocks don't overlap, that the break really moved |
| `test_malloc_2.cpp` | 42 | every function and every documented failure case, the six statistics to the byte, first-fit in ascending address order (note 2), "a reused block keeps its size and counts as fully used" (notes 1 and 12), block layout (note 13: no alignment) |
| `test_malloc_3.cpp` | 53 | the 32×128KB heap, tightest-fit/lowest-address (challenge 0), splitting (challenge 1), iterative merging and the no-merge-at-order-10 rule (challenge 2), the `size + M > 128KB` mmap threshold (challenge 3), every `srealloc` priority (reuse → merge buddies → move), heap exhaustion |
| `test_malloc_4.cpp` | 62 | all 53 part 3 tests (part 4 must not break them) plus the huge page rules: `smalloc` ≥ 4MB and `scalloc` with elements > 2MB get huge pages, everything else does not |

Sizes are computed from `_size_meta_data()` at runtime, so the tests work
whatever your metadata struct looks like (as long as it is ≤ 64 bytes, which
part 3 requires and the suite checks).

### Differential testing

`stress_random_operations_match_the_reference_model` (and friends) run thousands
of random `smalloc`/`scalloc`/`sfree`/`srealloc` calls against a reference model
of the allocator that the spec describes (`basic_model.h`, `buddy_model.h`).
After **every single operation** they check:

* all five statistics, and
* the exact address the call must have returned — the spec pins this down
  completely (tightest fit, lowest address, the lower half serves the request),
  and
* that no live block's contents were corrupted by a later operation.

When one of these fails, the message says which rule was broken, e.g.

```
      expected: the tightest fitting free block with the lowest address to be used
      smalloc(300) needs a block of order 2 (512 bytes); the one it must use is at
      offset 2048 of the region, i.e. 0x7f...820, but it returned 0x7f...c20
```

## Huge pages (part 4)

The huge page checks read `/proc/self/smaps` back and look at `KernelPageSize` /
`AnonHugePages`. If the machine has no huge pages available at all
(`HugePages_Total` is 0 and THP is off) the check cannot prove anything and
degrades to a warning. To test it for real:

```
sudo sysctl -w vm.nr_hugepages=64
```

Also note: if your huge page path uses `MAP_HUGETLB` with no fallback, it will
fail on a machine with no reserved huge pages — a few part 3 tests (the ones
that allocate ≥ 4MB) will start failing under `make 4`. Falling back to a normal
`mmap()` when the huge page mapping fails is the sane thing to do.

## Files

```
test_utils.h     the fork-per-test harness, assertions, patterns
stats_utils.h    OS_ASSERT_STATS - the five statistics, checked together
basic_model.h    reference model of the part 2 allocator
buddy_model.h    reference model of the part 3 buddy allocator
malloc3_tests.h  the part 3 test bodies (shared by parts 3 and 4)
```
