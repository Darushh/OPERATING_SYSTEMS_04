#include <iostream>
#include <cassert>
#include <cstring>
#include "os_malloc.h"

void test_basic_malloc() {
    std::cout << "Test 1: Basic malloc... ";
    void* p = smalloc(100);
    assert(p != NULL);
    sfree(p);
    std::cout << "PASSED" << std::endl;
}

void test_block_reuse() {
    std::cout << "Test 2: Block reuse... ";
    void* p1 = smalloc(100);
    void* original = p1;
    sfree(p1);
    void* p2 = smalloc(100);
    assert(p2 == original);
    sfree(p2);
    std::cout << "PASSED" << std::endl;
}

void test_free_block_statistics() {
    std::cout << "Test 3: Free block statistics... ";
    size_t initial_free = _num_free_blocks();
    void* p = smalloc(200);
    sfree(p);
    assert(_num_free_blocks() == initial_free + 1);
    std::cout << "PASSED" << std::endl;
}

void test_realloc_basic() {
    std::cout << "Test 4: Basic realloc... ";
    char* p1 = static_cast<char*>(smalloc(50));
    strcpy(p1, "Hello");
    char* p2 = static_cast<char*>(srealloc(p1, 100));
    assert(strcmp(p2, "Hello") == 0);
    sfree(p2);
    std::cout << "PASSED" << std::endl;
}

void test_calloc_initialization() {
    std::cout << "Test 5: Calloc initialization... ";
    char* p = static_cast<char*>(scalloc(100, 1));
    for (int i = 0; i < 100; i++) assert(p[i] == 0);
    sfree(p);
    std::cout << "PASSED" << std::endl;
}

void test_multiple_allocations() {
    std::cout << "Test 6: Multiple allocations... ";
    void* p1 = smalloc(64);
    void* p2 = smalloc(128);
    void* p3 = smalloc(256);
    assert(p1 != NULL && p2 != NULL && p3 != NULL);
    sfree(p2);
    void* p4 = smalloc(128);
    assert(p4 != NULL);
    sfree(p1);
    sfree(p3);
    sfree(p4);
    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "malloc_2 tests:" << std::endl;
    test_basic_malloc();
    test_block_reuse();
    test_free_block_statistics();
    test_realloc_basic();
    test_calloc_initialization();
    test_multiple_allocations();
    std::cout << "All tests PASSED" << std::endl;
    return 0;
}
