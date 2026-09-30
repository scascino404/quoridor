/*
 * atomics.c - qr_atomic_int on top of the GCC/Clang __atomic builtins,
 * which are available in every language mode, including -std=c89.
 */
#include "atomics.h"

#if !defined(__GNUC__)
#error "atomics.c needs porting: no atomic builtins known for this compiler"
#endif

int qr_atomic_load(const qr_atomic_int *a)
{
    return __atomic_load_n(&a->value, __ATOMIC_SEQ_CST);
}

void qr_atomic_store(qr_atomic_int *a, int value)
{
    __atomic_store_n(&a->value, value, __ATOMIC_SEQ_CST);
}
