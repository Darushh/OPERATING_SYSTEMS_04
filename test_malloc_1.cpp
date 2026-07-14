#include <iostream>
#include <cassert>
#include "os_malloc.h"

#define MAX_MALLOC 100000001

void test_basic_allocation() {
    std::cout << "Test 1: Basic allocation... ";
    void* p = smalloc(100);
    assert(p != NULL);
    std::cout << "PASSED" << std::endl;
}

void test_integer_array() {
    std::cout << "Test 2: Integer array... ";
    int* arr = static_cast<int*>(smalloc(10 * sizeof(int)));
    assert(arr != NULL);
    for (int i = 0; i < 10; i++) arr[i] = i * 100;
    for (int i = 0; i < 10; i++) assert(arr[i] == i * 100);
    std::cout << "PASSED" << std::endl;
}

void test_large_allocation() {
    std::cout << "Test 3: Large allocation rejection... ";
    void* large = smalloc(MAX_MALLOC);
    assert(large == NULL);
    void* large_ok = smalloc(MAX_MALLOC - 2);
    assert(large_ok != NULL);
    std::cout << "PASSED" << std::endl;
}

void test_zero_allocation() {
    std::cout << "Test 4: Zero allocation rejection... ";
    void* zero = smalloc(0);
    assert(zero == NULL);
    std::cout << "PASSED" << std::endl;
}

void test_multiple_allocations() {
    std::cout << "Test 5: Multiple allocations... ";
    void* p1 = smalloc(50);
    void* p2 = smalloc(100);
    void* p3 = smalloc(200);
    assert(p1 != NULL && p2 != NULL && p3 != NULL);
    assert(p1 != p2 && p2 != p3 && p1 != p3);
    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "malloc_1 tests:" << std::endl;
    test_basic_allocation();
    test_integer_array();
    test_large_allocation();
    test_zero_allocation();
    test_multiple_allocations();
    std::cout << "All tests PASSED" << std::endl;
    return 0;
}
