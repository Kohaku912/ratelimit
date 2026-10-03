/*
 * Unit tests for token_bucket.c.
 *
 * Deliberately framework-free: a reviewer can read the whole harness in a
 * few lines, and CI needs nothing but a C compiler.
 */
#include "token_bucket.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_checks   = 0;
static int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        g_checks++;                                                     \
        if (!(cond)) {                                                  \
            g_failures++;                                               \
            printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                               \
    } while (0)

#define CHECK_CLOSE(actual, expected, eps)                                  \
    do {                                                                    \
        g_checks++;                                                         \
        const double a_ = (actual);                                         \
        const double e_ = (expected);                                       \
        if (!(fabs(a_ - e_) <= (eps))) {                                    \
            g_failures++;                                                   \
            printf("    FAIL %s:%d  %s == %.9g, expected %.9g\n",           \
                   __FILE__, __LINE__, #actual, a_, e_);                    \
        }                                                                   \
    } while (0)

static const double EPS = 1e-9;

/* ---------------------------------------------------------------- init -- */

static void test_init_rejects_invalid_arguments(void)
{
    tb_bucket b;

    CHECK(tb_init(NULL, 10.0, 1.0, 0.0) == false);
    CHECK(tb_init(&b, 0.0, 1.0, 0.0) == false);
    CHECK(tb_init(&b, -1.0, 1.0, 0.0) == false);
    CHECK(tb_init(&b, NAN, 1.0, 0.0) == false);
    CHECK(tb_init(&b, INFINITY, 1.0, 0.0) == false);
    CHECK(tb_init(&b, 10.0, -0.5, 0.0) == false);
    CHECK(tb_init(&b, 10.0, NAN, 0.0) == false);
    CHECK(tb_init(&b, 10.0, INFINITY, 0.0) == false);
    CHECK(tb_init(&b, 10.0, 1.0, -1.0) == false);
    CHECK(tb_init(&b, 10.0, 1.0, NAN) == false);
    CHECK(b.initialised == false);
}

static void test_init_starts_full(void)
{
    tb_bucket b;
    CHECK(tb_init(&b, 7.0, 1.0, 0.0) == true);
    CHECK(b.initialised == true);
    CHECK_CLOSE(b.tokens, 7.0, EPS);
    CHECK_CLOSE(tb_peek(&b, 0.0), 7.0, EPS);
}

/* ------------------------------------------------------------- consume -- */

static void test_consume_within_capacity(void)
{
    tb_bucket b;
    tb_init(&b, 5.0, 1.0, 0.0);

    tb_result r = tb_consume(&b, 2.0, 0.0);
    CHECK(r.allowed == true);
    CHECK_CLOSE(r.remaining, 3.0, EPS);
    CHECK_CLOSE(r.retry_after, 0.0, EPS);

    r = tb_consume(&b, 3.0, 0.0);          /* exact depletion is allowed */
    CHECK(r.allowed == true);
    CHECK_CLOSE(r.remaining, 0.0, EPS);

    r = tb_consume(&b, 1.0, 0.0);
    CHECK(r.allowed == false);
    CHECK_CLOSE(r.remaining, 0.0, EPS);
}

static void test_denied_reports_retry_after(void)
{
    tb_bucket b;
    tb_init(&b, 5.0, 2.0, 0.0);

    tb_consume(&b, 5.0, 0.0);
    const tb_result r = tb_consume(&b, 1.0, 0.0);

    CHECK(r.allowed == false);
    CHECK_CLOSE(r.retry_after, 0.5, EPS);   /* 1 token at 2 tokens/second */
}

static void test_retry_after_matches_actual_wait(void)
{
    tb_bucket b;
    tb_init(&b, 5.0, 2.0, 0.0);
    tb_consume(&b, 5.0, 0.0);

    const tb_result r = tb_consume(&b, 3.0, 0.0);
    CHECK(r.allowed == false);
    CHECK_CLOSE(r.retry_after, 1.5, EPS);   /* 3 tokens at 2 tokens/second */

    /* Waiting exactly retry_after seconds must be enough. */
    const tb_result again = tb_consume(&b, 3.0, r.retry_after);
    CHECK(again.allowed == true);
}

static void test_cost_above_capacity_is_never_admitted(void)
{
    tb_bucket b;
    tb_init(&b, 5.0, 1.0, 0.0);

    tb_result r = tb_consume(&b, 6.0, 0.0);
    CHECK(r.allowed == false);

    /* Even a full bucket cannot satisfy a request larger than capacity. */
    r = tb_consume(&b, 6.0, 1000.0);
    CHECK(r.allowed == false);
    CHECK_CLOSE(r.remaining, 5.0, EPS);
}

static void test_zero_cost_is_a_noop(void)
{
    tb_bucket b;
    tb_init(&b, 3.0, 0.0, 0.0);

    for (int i = 0; i < 5; i++) {
        const tb_result r = tb_consume(&b, 0.0, (double)i);
        CHECK(r.allowed == true);
        CHECK_CLOSE(r.remaining, 3.0, EPS);
    }
}

static void test_negative_cost_is_clamped(void)
{
    tb_bucket b;
    tb_init(&b, 3.0, 1.0, 0.0);
    tb_consume(&b, 1.0, 0.0);               /* 2 left */

    const tb_result r = tb_consume(&b, -100.0, 0.0);
    CHECK(r.allowed == true);
    CHECK_CLOSE(r.remaining, 2.0, EPS);     /* unchanged */
}

static void test_non_finite_inputs_are_denied(void)
{
    tb_bucket b;
    tb_init(&b, 5.0, 1.0, 0.0);

    CHECK(tb_consume(&b, NAN, 0.0).allowed == false);
    CHECK(tb_consume(&b, INFINITY, 0.0).allowed == false);

    const tb_result r = tb_consume(&b, 1.0, NAN);
    CHECK(r.allowed == false);
    CHECK_CLOSE(r.remaining, 5.0, EPS);     /* untouched */
}

/* -------------------------------------------------------------- refill -- */

static void test_refill_over_time(void)
{
    tb_bucket b;
    tb_init(&b, 10.0, 1.0, 0.0);
    tb_consume(&b, 10.0, 0.0);

    CHECK_CLOSE(tb_peek(&b, 3.0), 3.0, EPS);

    const tb_result r = tb_consume(&b, 2.0, 3.0);
    CHECK(r.allowed == true);
    CHECK_CLOSE(r.remaining, 1.0, EPS);
}

static void test_refill_is_capped_at_capacity(void)
{
    tb_bucket b;
    tb_init(&b, 4.0, 1.0, 0.0);
    tb_consume(&b, 4.0, 0.0);

    CHECK_CLOSE(tb_peek(&b, 1e6), 4.0, EPS);   /* not 1e6 */

    const tb_result r = tb_consume(&b, 4.0, 1e6);
    CHECK(r.allowed == true);
    CHECK_CLOSE(r.remaining, 0.0, EPS);
}

static void test_fractional_refill_accumulates(void)
{
    tb_bucket b;
    tb_init(&b, 10.0, 0.5, 0.0);
    tb_consume(&b, 10.0, 0.0);

    CHECK_CLOSE(tb_peek(&b, 1.0), 0.5, EPS);

    const tb_result r = tb_consume(&b, 0.5, 1.0);
    CHECK(r.allowed == true);
    CHECK_CLOSE(r.remaining, 0.0, EPS);
}

static void test_zero_rate_never_refills(void)
{
    tb_bucket b;
    tb_init(&b, 2.0, 0.0, 0.0);
    tb_consume(&b, 2.0, 0.0);

    const tb_result r = tb_consume(&b, 1.0, 1e6);
    CHECK(r.allowed == false);
    CHECK(r.retry_after >= 1e30);           /* reported as INFINITY */
}

static void test_peek_does_not_consume(void)
{
    tb_bucket b;
    tb_init(&b, 5.0, 1.0, 0.0);
    tb_consume(&b, 5.0, 0.0);

    CHECK_CLOSE(tb_peek(&b, 3.0), 3.0, EPS);
    CHECK_CLOSE(tb_peek(&b, 3.0), 3.0, EPS);   /* idempotent */

    const tb_result r = tb_consume(&b, 3.0, 3.0);
    CHECK(r.allowed == true);
    CHECK_CLOSE(r.remaining, 0.0, EPS);
}

/* ------------------------------------------------- monotonic clock ----- */

static void test_backwards_clock_does_not_refill_or_rewind(void)
{
    tb_bucket b;
    tb_init(&b, 10.0, 1.0, 100.0);
    tb_consume(&b, 10.0, 100.0);            /* empty at t=100 */

    const tb_result r = tb_consume(&b, 1.0, 50.0);   /* clock jumped back */
    CHECK(r.allowed == false);
    CHECK_CLOSE(r.remaining, 0.0, EPS);

    /* last_refill must still be 100, not 50. */
    CHECK_CLOSE(b.last_refill, 100.0, EPS);

    /*
     * The discriminating assertion: 1 second after t=100 we have exactly 1
     * token. Had last_refill been rewound to 50, elapsed would be 51 and
     * the bucket would be full (capped at 10).
     */
    CHECK_CLOSE(tb_peek(&b, 101.0), 1.0, EPS);
}

/* -------------------------------------------------------- robustness ---- */

static void test_uninitialised_bucket_is_safe(void)
{
    tb_bucket b;
    memset(&b, 0, sizeof b);                /* initialised == false */

    const tb_result r = tb_consume(&b, 1.0, 0.0);
    CHECK(r.allowed == false);
    CHECK(r.retry_after >= 1e30);
    CHECK_CLOSE(tb_peek(&b, 0.0), 0.0, EPS);
}

static void test_null_bucket_is_safe(void)
{
    const tb_result r = tb_consume(NULL, 1.0, 0.0);
    CHECK(r.allowed == false);
    CHECK_CLOSE(tb_peek(NULL, 0.0), 0.0, EPS);
}

/* --------------------------------------------------------------- main --- */

int main(void)
{
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "init rejects invalid arguments",        test_init_rejects_invalid_arguments },
        { "init starts full",                      test_init_starts_full },
        { "consume within capacity",               test_consume_within_capacity },
        { "denied reports retry_after",            test_denied_reports_retry_after },
        { "retry_after matches actual wait",       test_retry_after_matches_actual_wait },
        { "cost above capacity never admitted",    test_cost_above_capacity_is_never_admitted },
        { "zero cost is a no-op",                  test_zero_cost_is_a_noop },
        { "negative cost is clamped",              test_negative_cost_is_clamped },
        { "non-finite inputs are denied",          test_non_finite_inputs_are_denied },
        { "refill over time",                      test_refill_over_time },
        { "refill is capped at capacity",          test_refill_is_capped_at_capacity },
        { "fractional refill accumulates",         test_fractional_refill_accumulates },
        { "zero rate never refills",               test_zero_rate_never_refills },
        { "peek does not consume",                 test_peek_does_not_consume },
        { "backwards clock neither refills nor rewinds", test_backwards_clock_does_not_refill_or_rewind },
        { "uninitialised bucket is safe",          test_uninitialised_bucket_is_safe },
        { "null bucket is safe",                   test_null_bucket_is_safe },
    };

    const size_t n = sizeof tests / sizeof tests[0];

    for (size_t i = 0; i < n; i++) {
        printf("[%2zu/%2zu] %s\n", i + 1, n, tests[i].name);
        tests[i].fn();
    }

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if (g_failures == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return g_failures == 0 ? 0 : 1;
}
