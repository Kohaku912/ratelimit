/*
 * token_bucket.h — a small, dependency-free token-bucket rate limiter.
 *
 * The clock is injected as a `double` (seconds, monotonic) instead of being
 * read internally. That keeps the implementation pure and makes every
 * refill boundary testable without sleeping.
 */
#ifndef TOKEN_BUCKET_H
#define TOKEN_BUCKET_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A single rate-limiting bucket. Treat the fields as read-only. */
typedef struct {
    double capacity;           /* maximum number of tokens held            */
    double refill_per_second;  /* tokens added per second (>= 0)           */
    double tokens;             /* tokens currently available               */
    double last_refill;        /* last timestamp the bucket was refilled   */
    bool   initialised;        /* guards against use before tb_init()      */
} tb_bucket;

/* Outcome of a tb_consume() call. */
typedef struct {
    bool   allowed;      /* true if the request was admitted              */
    double remaining;    /* tokens left after the decision                */
    double retry_after;  /* seconds until `cost` tokens exist; 0 if allowed,
                            INFINITY if the bucket can never refill         */
} tb_result;

/*
 * Initialise a bucket. It starts *full*.
 *
 * Returns false on invalid arguments. The check fails closed: on failure *b
 * is zeroed and left permanently unusable, so ignoring the return value can
 * never expose indeterminate memory. Rejected inputs are:
 *   - b is NULL
 *   - capacity is not finite, or <= 0
 *   - refill_per_second is not finite, or < 0
 *   - now is not finite, or < 0
 */
bool tb_init(tb_bucket *b, double capacity, double refill_per_second, double now);

/*
 * Try to take `cost` tokens at time `now`.
 *
 * `cost` is clamped to >= 0; a cost of 0 is always admitted and changes
 * nothing. A non-finite cost is treated as +INFINITY and is denied.
 *
 * If `now` is earlier than the last refill (a non-monotonic clock), the
 * bucket is *not* refilled and `last_refill` is not moved backwards, so a
 * clock jump cannot create or destroy tokens.
 */
tb_result tb_consume(tb_bucket *b, double cost, double now);

/*
 * Tokens that would be available at `now`, without consuming any.
 * Returns 0.0 for an uninitialised bucket.
 */
double tb_peek(const tb_bucket *b, double now);

#ifdef __cplusplus
}
#endif

#endif /* TOKEN_BUCKET_H */
