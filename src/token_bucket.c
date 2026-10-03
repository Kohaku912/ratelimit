#include "token_bucket.h"

#include <math.h>

/*
 * Bring the bucket up to date as of `now`.
 *
 * The monotonic-clock guard lives here: if `now` is earlier than the last
 * refill we return the current token count untouched and deliberately do
 * NOT rewind `last_refill`. Rewinding would let a single backwards clock
 * step hand out the same elapsed time twice.
 */
static double tb_refill(tb_bucket *b, double now)
{
    if (!(now >= b->last_refill)) {
        return b->tokens;
    }

    const double elapsed = now - b->last_refill;
    if (elapsed > 0.0 && b->refill_per_second > 0.0) {
        double tokens = b->tokens + elapsed * b->refill_per_second;
        if (!isfinite(tokens) || tokens > b->capacity) {
            tokens = b->capacity;
        }
        b->tokens = tokens;
    }

    b->last_refill = now;
    return b->tokens;
}

bool tb_init(tb_bucket *b, double capacity, double refill_per_second, double now)
{
    if (b == NULL) {
        return false;
    }

    /*
     * Fail closed. A rejected tb_init() must leave the bucket definitively
     * unusable, otherwise a caller that ignores the return value would be
     * reading indeterminate stack memory. tb_consume()/tb_peek() gate on
     * `initialised`, so clearing it here is enough to make them refuse.
     */
    b->initialised       = false;
    b->capacity          = 0.0;
    b->refill_per_second = 0.0;
    b->tokens            = 0.0;
    b->last_refill       = 0.0;

    if (!isfinite(capacity) || capacity <= 0.0) {
        return false;
    }
    if (!isfinite(refill_per_second) || refill_per_second < 0.0) {
        return false;
    }
    if (!isfinite(now) || now < 0.0) {
        return false;
    }

    b->capacity          = capacity;
    b->refill_per_second = refill_per_second;
    b->tokens            = capacity;   /* a fresh bucket is full */
    b->last_refill       = now;
    b->initialised       = true;
    return true;
}

tb_result tb_consume(tb_bucket *b, double cost, double now)
{
    tb_result r = { false, 0.0, 0.0 };

    if (b == NULL || !b->initialised) {
        r.retry_after = HUGE_VAL;
        return r;
    }
    if (!isfinite(now)) {
        r.remaining   = b->tokens;
        r.retry_after = HUGE_VAL;
        return r;
    }

    if (!isfinite(cost)) {
        cost = HUGE_VAL;          /* NaN / +inf / -inf: never satisfiable */
    } else if (cost < 0.0) {
        cost = 0.0;
    }

    r.remaining = tb_refill(b, now);

    if (cost == 0.0) {
        r.allowed     = true;
        r.retry_after = 0.0;
        return r;
    }

    if (cost <= b->tokens) {
        b->tokens     -= cost;
        r.allowed      = true;
        r.remaining    = b->tokens;
        r.retry_after  = 0.0;
        return r;
    }

    /* Denied: report how long until `cost` tokens are available. */
    r.allowed     = false;
    r.remaining   = b->tokens;
    r.retry_after = (b->refill_per_second > 0.0 && isfinite(cost))
                    ? (cost - b->tokens) / b->refill_per_second
                    : HUGE_VAL;
    return r;
}

double tb_peek(const tb_bucket *b, double now)
{
    if (b == NULL || !b->initialised) {
        return 0.0;
    }
    if (!isfinite(now) || !(now >= b->last_refill)) {
        return b->tokens;
    }

    const double elapsed = now - b->last_refill;
    if (elapsed > 0.0 && b->refill_per_second > 0.0) {
        double tokens = b->tokens + elapsed * b->refill_per_second;
        if (!isfinite(tokens) || tokens > b->capacity) {
            tokens = b->capacity;
        }
        return tokens;
    }
    return b->tokens;
}
