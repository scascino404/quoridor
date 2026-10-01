/*
 * atomics.c - qr_atomic_int and qr_atomic_ulong on top of the GCC/Clang
 * __atomic builtins, which are available in every language mode, including
 * -std=c89.
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

unsigned long qr_atomic_load_ulong(const qr_atomic_ulong *a)
{
    return __atomic_load_n(&a->value, __ATOMIC_RELAXED);
}

void qr_atomic_store_ulong(qr_atomic_ulong *a, unsigned long value)
{
    __atomic_store_n(&a->value, value, __ATOMIC_RELAXED);
}
