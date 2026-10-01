/*
 * atomics.h - values that may be read and written from several threads.
 *
 * C89 has no atomics, so this is the one module that depends on compiler
 * extensions (see atomics.c); everything else stays ISO C89, apart from
 * the AI's use of POSIX threads and clocks. Each value is wrapped in a
 * struct so that it cannot be accessed by accident without going through
 * these functions.
 */
#ifndef ATOMICS_H
#define ATOMICS_H

typedef struct { int value; } qr_atomic_int;

int  qr_atomic_load(const qr_atomic_int *a);
void qr_atomic_store(qr_atomic_int *a, int value);

/* An unsigned long that is only ever read or written whole, with no
 * ordering against other memory accesses (relaxed). For data that is
 * checked after reading, such as the AI's shared transposition table. */
typedef struct { unsigned long value; } qr_atomic_ulong;

unsigned long qr_atomic_load_ulong(const qr_atomic_ulong *a);
void          qr_atomic_store_ulong(qr_atomic_ulong *a, unsigned long value);

#endif /* ATOMICS_H */
