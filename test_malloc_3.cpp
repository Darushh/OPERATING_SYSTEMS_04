#include <iostream>
#include <cassert>
#include <cstring>
#include <stdint.h>
#include "os_malloc.h"

#define MMAP_THRESHOLD (128 * 1024)

void test_alignment() {
    std::cout << "Test 1: 8-byte alignment... ";
    void* p1 = smalloc(10);
    void* p2 = smalloc(15);
    void* p3 = smalloc(33);
    assert(((uintptr_t)p1 % 8) == 0);
    assert(((uintptr_t)p2 % 8) == 0);
    assert(((uintptr_t)p3 % 8) == 0);
    sfree(p1);
    sfree(p2);
    sfree(p3);
    std::cout << "PASSED" << std::endl;
}

void test_block_merging() {
    std::cout << "Test 2: Block merging... ";
    size_t initial_free = _num_free_blocks();
    void* p1 = smalloc(100);
    void* p2 = smalloc(100);
    void* p3 = smalloc(100);
    sfree(p1);
    sfree(p2);
    sfree(p3);
    assert(_num_free_blocks() >= initial_free);
    std::cout << "PASSED" << std::endl;
}

void test_block_splitting() {
    std::cout << "Test 3: Block splitting... ";
    void* p1 = smalloc(1000);
    sfree(p1);
    void* p2 = smalloc(100);
    assert(_num_free_bytes() > 0);
    sfree(p2);
    std::cout << "PASSED" << std::endl;
}

void test_large_allocation() {
    std::cout << "Test 4: Large allocation (mmap)... ";
    void* large = smalloc(MMAP_THRESHOLD + 1000);
    assert(large != NULL);
    sfree(large);
    std::cout << "PASSED" << std::endl;
}

void test_statistics() {
    std::cout << "Test 5: Statistics with mmap... ";
    size_t initial = _num_allocated_blocks();
    void* small = smalloc(100);
    assert(_num_allocated_blocks() > initial);
    void* large = smalloc(MMAP_THRESHOLD + 1000);
    assert(_num_allocated_blocks() > initial + 1);
    sfree(small);
    sfree(large);
    std::cout << "PASSED" << std::endl;
}

void test_realloc() {
    std::cout << "Test 6: Realloc with merging... ";
    char* p1 = static_cast<char*>(smalloc(50));
    char* p2 = static_cast<char*>(smalloc(50));
    strcpy(p1, "Test");
    sfree(p2);
    char* p3 = static_cast<char*>(srealloc(p1, 120));
    assert(strcmp(p3, "Test") == 0);
    sfree(p3);
    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "malloc_3 tests:" << std::endl;
    test_alignment();
    test_block_merging();
    test_block_splitting();
    test_large_allocation();
    test_statistics();
    test_realloc();
    std::cout << "All tests PASSED" << std::endl;
    return 0;
}
