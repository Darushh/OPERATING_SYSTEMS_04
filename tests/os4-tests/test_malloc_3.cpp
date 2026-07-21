/*
 * Tests for part 3 - the buddy allocator (malloc_3.cpp).
 *
 * All the test bodies live in malloc3_tests.h, because test_malloc_4.cpp has to
 * run exactly the same ones: part 4 only changes how allocations of 4MB and up
 * are backed, so every rule of part 3 must still hold there.
 */

#include "malloc3_tests.h"

OS_TEST_MAIN("malloc_3 - buddy allocator")
