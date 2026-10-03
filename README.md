# ratelimit

A small, dependency-free **token-bucket rate limiter** written in C17.

[![CI](https://github.com/Kohaku912/ratelimit/actions/workflows/ci.yml/badge.svg)](https://github.com/Kohaku912/ratelimit/actions/workflows/ci.yml)

Roughly 120 lines of library code, a CLI to drive it, and a test suite that
runs under AddressSanitizer, UndefinedBehaviorSanitizer and valgrind.

---

## Why this exists

Rate limiting is the kind of component that looks trivial and then quietly
breaks in production: it drifts when the clock jumps, it leaks tokens when a
request is rejected, it hands out more than the configured burst. The point of
this library is to make those edge cases **explicit and testable** rather than
incidental.

Three decisions follow from that:

1. **The clock is injected, not read.** Every call takes `now` as a parameter.
   The library never calls `time()` or `clock_gettime()`. A 60-second window is
   therefore evaluated instantly in a test, with no sleeping and no flakes.
2. **A backwards clock cannot corrupt the bucket.** If `now` is earlier than the
   last refill, the bucket is *not* refilled and `last_refill` is *not* rewound.
   Rewinding would let a single clock step hand out the same elapsed time twice.
3. **Initialisation fails closed.** A rejected `tb_init()` zeroes the bucket, so
   a caller that ignores the return value can never read indeterminate memory.

There are no allocations, no globals and no I/O in the library.

## Layout

```
include/token_bucket.h        public API
src/token_bucket.c            implementation (~120 lines)
src/main.c                    CLI that drives the library
tests/test_token_bucket.c     unit tests, no external framework
Makefile                      build, test, asan, valgrind
CMakeLists.txt                alternative build (library + CLI + ctest)
scripts/run_tests.sh          portable driver for machines without make
.github/workflows/ci.yml      gcc + clang, ASan/UBSan, valgrind, cmake
```

## Build and test

With `make`:

```sh
make test        # build and run the unit tests
make all         # build build/ratelimit
make asan        # rebuild the tests with ASan + UBSan and run them
make valgrind    # run the tests under valgrind memcheck
```

With CMake:

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --parallel
ctest --test-dir build-cmake --output-on-failure
```

Or, if you have neither `make` nor CMake, use the portable driver — it picks a
compiler, runs the tests, smoke-tests the CLI and then re-runs the tests under
ASan/UBSan where the toolchain supports it:

```sh
sh scripts/run_tests.sh
```

Only a C17 compiler and `libm` are required.

## API

```c
#include "token_bucket.h"

tb_bucket bucket;
tb_init(&bucket, 10.0, 2.0, 0.0);      /* capacity 10, 2 tokens/second */

tb_result r = tb_consume(&bucket, 3.0, 0.0);
if (r.allowed) {
    /* serve the request */
} else {
    /* too many requests; retry in r.retry_after seconds */
}

double available = tb_peek(&bucket, 0.0);   /* inspect without consuming */
```

`tb_result` carries `allowed`, `remaining` and `retry_after`, so the caller can
emit a `Retry-After` header without recomputing anything.

## Semantics

The behaviour is defined for the awkward inputs, not just the happy path.

| Situation | Behaviour |
|---|---|
| `cost` is negative | clamped to `0` |
| `cost` is `0` | admitted; no state change |
| `cost == tokens` | admitted (exact depletion is allowed) |
| `cost > capacity` | **always denied**, even from a full bucket |
| denied | `retry_after = (cost - tokens) / refill_per_second` |
| `refill_per_second == 0` | denied requests report `retry_after = INFINITY` |
| `now < last_refill` | no refill; `last_refill` is not rewound |
| `now` or `cost` is NaN/Inf | denied; the bucket is left untouched |
| `tb_init` rejected its arguments | bucket zeroed and permanently unusable |
| bucket never initialised / `NULL` | safe: denied, `tb_peek` returns `0.0` |

A freshly initialised bucket starts **full**.

## CLI

The CLI simulates a stream of requests with a synthetic clock, so you can see
the refill boundary without waiting for it.

```console
$ ./build/ratelimit --capacity 5 --rate 1 --cost 2 --requests 8 --interval 0.5
capacity=5 rate=1/s cost=2 interval=0.5s requests=8

#      t (s)     allowed  remaining   retry_after (s)
1      0.000     yes      3.000       -
2      0.500     yes      1.500       -
3      1.000     yes      0.000       -
4      1.500     no       0.500       1.500
5      2.000     no       1.000       1.000
6      2.500     no       1.500       0.500
7      3.000     yes      0.000       -
8      3.500     no       0.500       1.500

allowed 4 / 8
```

Run `./build/ratelimit --help` for the flags.

## Tests

17 test cases / 70 assertions, all passing:

```console
$ make test
...
[17/17] null bucket is safe

70 checks, 0 failures
ALL TESTS PASSED
```

Coverage includes: argument validation, exact depletion, refill accumulation and
capping, fractional refill, `retry_after` cross-checked against an actual wait,
`cost > capacity`, zero and negative cost, NaN/Inf inputs, `tb_peek` idempotence,
zero refill rate, the backwards-clock guard, and `NULL` / uninitialised buckets.

The clock-injection design pays off most in
`test_retry_after_matches_actual_wait`: the reported `retry_after` is fed back in
as the next timestamp and the request must then be admitted.

Two defects were found and fixed while writing these tests:

- `tb_init` originally returned `false` on bad arguments **without touching the
  struct**, so a caller ignoring the return value read indeterminate memory.
  It now fails closed.
- The CLI incremented its loop counter inside the loop body, which
  `-Wfor-loop-analysis` rejects. The parser is now a `while` loop.

## Requirements

- C17 compiler (verified with clang 22 and, in CI, gcc and clang)
- `libm` (`-lm`) on Linux/macOS

The tests are run under AddressSanitizer, UndefinedBehaviorSanitizer and
valgrind in CI, and locally with clang 22.1.8 on Windows.

## License

MIT — see [LICENSE](LICENSE).

---

## 日本語

C17で書いた、依存ライブラリなしの**トークンバケット方式レートリミッタ**です。

- **時刻を引数で受け取る設計**（内部で `time()` を呼ばない）ため、
  60秒のウィンドウでもテストは一瞬で終わり、フレークしません。
- **時計が逆行しても壊れません。** `now` が前回の補充時刻より前の場合、
  補充も `last_refill` の巻き戻しも行いません。
- **初期化はフェイルクローズド。** 引数が不正な場合、構造体をゼロにして
  恒久的に使用不能にします。戻り値を無視しても不定値は読まれません。
- アロケーション・グローバル変数・I/O はライブラリ内に一切ありません。

`make test` で単体テスト、`make asan` で ASan/UBSan、
`make valgrind` でメモリ検査を実行できます。
