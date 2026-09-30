/*
 * perft_cli.c - command-line perft for the Quoridor library: counts the
 * move tree to a given depth, to check move generation against known totals
 * and to time it.
 *
 * Times are CPU time from clock(), the only timer C89 offers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "perft.h"

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s [-d] [-m MOVES] DEPTH\n"
            "\n"
            "Counts the move sequences of each length from 1 to DEPTH.\n"
            "\n"
            "  -d        divide: list each move with the number of sequences\n"
            "            of length DEPTH that start with it\n"
            "  -m MOVES  play these moves from the starting position first,\n"
            "            e.g. -m \"e2 e8 e3h\"\n",
            prog);
}

static double seconds_since(clock_t start)
{
    return (double)(clock() - start) / CLOCKS_PER_SEC;
}

/* Parses a depth of at least 1. Returns 0 on success, -1 otherwise. */
static int parse_depth(const char *s, int *depth)
{
    char *end;
    long v;

    v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 1 || v > 1000)
        return -1;
    *depth = (int)v;
    return 0;
}

/* Applies a whitespace-separated move list to g. Returns 0 on success; on
 * failure prints the offending move and returns -1. */
static int play_moves(qr_game *g, const char *moves)
{
    const char *p = moves;
    char tok[4];
    size_t len;
    qr_move m;
    qr_status st;

    for (;;) {
        p += strspn(p, " \t\n,");
        if (*p == '\0')
            return 0;
        len = strcspn(p, " \t\n,");
        if (len >= sizeof tok) {
            fprintf(stderr, "bad move: %.*s\n", (int)len, p);
            return -1;
        }
        memcpy(tok, p, len);
        tok[len] = '\0';
        p += len;

        if (qr_move_from_str(tok, &m) != 0) {
            fprintf(stderr, "bad move: %s\n", tok);
            return -1;
        }
        st = qr_apply_move(g, &m);
        if (st != QR_OK) {
            fprintf(stderr, "cannot play %s: %s\n", tok, qr_status_str(st));
            return -1;
        }
    }
}

/* Writes nodes per second into buf, or "-" if no time was measured. */
static void rate_str(char buf[32], unsigned long nodes, double secs)
{
    if (secs > 0.0)
        sprintf(buf, "%.0f", (double)nodes / secs);
    else
        strcpy(buf, "-");
}

/* One row per depth from 1 to max_depth. */
static void run_table(qr_game *g, int max_depth)
{
    unsigned long nodes;
    clock_t start;
    double secs;
    char rate[32];
    int d;

    printf("%5s %16s %10s %14s\n", "depth", "nodes", "time(s)", "nodes/s");
    for (d = 1; d <= max_depth; d++) {
        start = clock();
        nodes = perft(g, d);
        secs = seconds_since(start);
        rate_str(rate, nodes, secs);
        printf("%5d %16lu %10.3f %14s\n", d, nodes, secs, rate);
        fflush(stdout);
    }
}

/* Each move with its share of the depth-ply total, in generation order. */
static void run_divide(qr_game *g, int depth)
{
    qr_move moves[QR_MAX_MOVES];
    qr_pos from;
    char buf[4], rate[32];
    unsigned long nodes, total = 0;
    clock_t start;
    double secs;
    int n, i;

    start = clock();
    n = qr_legal_moves(g, moves);
    for (i = 0; i < n; i++) {
        from = g->pawn[g->to_move];
        qr_move_to_str(&moves[i], buf);
        if (qr_apply_move(g, &moves[i]) != QR_OK) {
            printf("%-3s  rejected by qr_apply_move\n", buf);
            continue;
        }
        nodes = perft(g, depth - 1);
        qr_undo_move(g, &moves[i], from);
        total += nodes;
        printf("%-3s %16lu\n", buf, nodes);
        fflush(stdout);
    }
    secs = seconds_since(start);

    rate_str(rate, total, secs);
    printf("\nmoves   %d\n", n);
    printf("nodes   %lu\n", total);
    printf("time    %.3f\n", secs);
    printf("nodes/s %s\n", rate);
}

int main(int argc, char *argv[])
{
    qr_game g;
    const char *moves = NULL, *depth_arg = NULL;
    int divide = 0, depth, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0) {
            divide = 1;
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            moves = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-' && depth_arg == NULL) {
            depth_arg = argv[i];
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (depth_arg == NULL) {
        usage(argv[0]);
        return 2;
    }
    if (parse_depth(depth_arg, &depth) != 0) {
        fprintf(stderr, "bad depth: %s\n", depth_arg);
        return 2;
    }

    qr_game_init(&g);
    if (moves != NULL && play_moves(&g, moves) != 0)
        return 2;

    if (divide)
        run_divide(&g, depth);
    else
        run_table(&g, depth);
    return 0;
}
