#include <stdatomic.h>

int test(int* ptr, atomic_int* aptr) {
    int tmp = *ptr;                    // normal load
    *ptr = tmp + 1;                    // normal store

    int atmp = atomic_load_explicit(aptr, memory_order_relaxed);  // atomic load
    atomic_store_explicit(aptr, atmp + 1, memory_order_relaxed);  // atomic store

    return tmp + atmp;
}
