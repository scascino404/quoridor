/*
 * check.h - what the tests share. No framework: each CHECK failure is
 * printed, and main returns check_report(), which is nonzero if any failed.
 *
 * Defines static data and functions, so it is included by the one source
 * file of each test program.
 */
#ifndef CHECK_H
#define CHECK_H

#include <stdio.h>

#include "quoridor.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond) \
    do { \
        checks++; \
        if (!(cond)) { \
            failures++; \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

/* The square written as s, e.g. "e4". */
static qr_pos sq(const char *s)
{
    qr_pos p;
    p.col = s[0] - 'a';
    p.row = s[1] - '1';
    return p;
}

/* Prints the outcome of the checks and returns the process exit status. */
static int check_report(void)
{
    if (failures) {
        fprintf(stderr, "%d of %d checks FAILED\n", failures, checks);
        return 1;
    }
    printf("all %d checks passed\n", checks);
    return 0;
}

#endif /* CHECK_H */
