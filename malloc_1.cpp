#include <unistd.h>
#include <cstddef>

void* smalloc(size_t size) {
    size_t MAX_MALLOC = 100000000; 
    if (size == 0 || size > MAX_MALLOC) {
        return NULL;
    }
    void* ptr = sbrk(size);
    if (ptr == (void*)-1) {
        return NULL;
    }
    return ptr;
}