#include <iostream>
#include <cassert>
#include "os_malloc.h"

void test_basic() {
    std::cout << "Test 1: Basic malloc_4... ";
    void* p = smalloc(1000);
    assert(p != NULL);
    sfree(p);
    std::cout << "PASSED" << std::endl;
}

int main() {
    std::cout << "malloc_4 tests (optional):" << std::endl;
    test_basic();
    std::cout << "All tests PASSED" << std::endl;
    return 0;
}
