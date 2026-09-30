/*
 * atomics.h - an int that may be read and written from several threads.
 *
 * C89 has no atomics, so this is the one module that depends on compiler
 * extensions (see atomics.c); everything else stays ISO C89. The value is
 * wrapped in a struct so that it cannot be accessed by accident without
 * going through these functions.
 */
#ifndef QR_ATOMICS_H
#define QR_ATOMICS_H

typedef struct { int value; } qr_atomic_int;

int  qr_atomic_load(const qr_atomic_int *a);
void qr_atomic_store(qr_atomic_int *a, int value);

#endif /* QR_ATOMICS_H */
