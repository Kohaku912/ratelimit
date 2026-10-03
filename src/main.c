/*
 * ratelimit — a tiny CLI that drives the token-bucket library.
 *
 * It exists so the library can be exercised end-to-end from a shell, and so
 * a reviewer can see the behaviour without reading the tests. All time is
 * simulated, so a 60-second window is evaluated instantly.
 */
#include "token_bucket.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    double capacity;
    double rate;
    double cost;
    long   requests;
    double interval;
} options;

static void usage(const char *prog)
{
    printf(
        "usage: %s [options]\n"
        "\n"
        "  --capacity N    bucket size in tokens            (default 10)\n"
        "  --rate R        tokens refilled per second       (default 2)\n"
        "  --cost C        tokens charged per request       (default 1)\n"
        "  --requests N    how many requests to simulate    (default 20)\n"
        "  --interval S    seconds between requests         (default 0.5)\n"
        "  --help          show this message\n"
        "\n"
        "example: %s --capacity 5 --rate 1 --cost 2 --requests 10 --interval 0.25\n",
        prog, prog);
}

static int parse_double(const char *s, double *out)
{
    char *end = NULL;
    const double v = strtod(s, &end);
    if (end == s || *end != '\0') {
        return 0;
    }
    *out = v;
    return 1;
}

static int parse_long(const char *s, long *out)
{
    char *end = NULL;
    const long v = strtol(s, &end, 10);
    if (end == s || *end != '\0') {
        return 0;
    }
    *out = v;
    return 1;
}

int main(int argc, char **argv)
{
    options o = { 10.0, 2.0, 1.0, 20L, 0.5 };

    int i = 1;
    while (i < argc) {
        const char *flag = argv[i];

        if (strcmp(flag, "--help") == 0 || strcmp(flag, "-h") == 0) {
            usage(argv[0]);
            return 0;
        }

        const char *val = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (val == NULL) {
            fprintf(stderr, "error: %s requires a value\n", flag);
            return 2;
        }

        int ok = 0;
        if      (strcmp(flag, "--capacity") == 0) ok = parse_double(val, &o.capacity);
        else if (strcmp(flag, "--rate")     == 0) ok = parse_double(val, &o.rate);
        else if (strcmp(flag, "--cost")     == 0) ok = parse_double(val, &o.cost);
        else if (strcmp(flag, "--interval") == 0) ok = parse_double(val, &o.interval);
        else if (strcmp(flag, "--requests") == 0) ok = parse_long(val, &o.requests);
        else {
            fprintf(stderr, "error: unknown option '%s'\n", flag);
            usage(argv[0]);
            return 2;
        }

        if (!ok) {
            fprintf(stderr, "error: invalid value for %s: '%s'\n", flag, val);
            return 2;
        }
        i += 2;   /* flag + value */
    }

    if (o.requests < 0 || o.interval < 0.0) {
        fprintf(stderr, "error: --requests and --interval must not be negative\n");
        return 2;
    }

    tb_bucket bucket;
    if (!tb_init(&bucket, o.capacity, o.rate, 0.0)) {
        fprintf(stderr,
                "error: invalid bucket (capacity=%.6g, rate=%.6g)\n"
                "       capacity must be > 0 and rate must be >= 0\n",
                o.capacity, o.rate);
        return 2;
    }

    printf("capacity=%.6g rate=%.6g/s cost=%.6g interval=%.6gs requests=%ld\n\n",
           o.capacity, o.rate, o.cost, o.interval, o.requests);
    printf("%-6s %-9s %-8s %-11s %s\n", "#", "t (s)", "allowed", "remaining", "retry_after (s)");

    long allowed_count = 0;
    for (long n = 0; n < o.requests; n++) {
        const double t = (double)n * o.interval;
        const tb_result r = tb_consume(&bucket, o.cost, t);
        if (r.allowed) {
            allowed_count++;
        }

        char retry[32];
        if (r.allowed) {
            snprintf(retry, sizeof retry, "-");
        } else if (r.retry_after >= 1e30) {
            snprintf(retry, sizeof retry, "never");
        } else {
            snprintf(retry, sizeof retry, "%.3f", r.retry_after);
        }

        printf("%-6ld %-9.3f %-8s %-11.3f %s\n",
               n + 1, t, r.allowed ? "yes" : "no", r.remaining, retry);
    }

    printf("\nallowed %ld / %ld\n", allowed_count, o.requests);
    return 0;
}
