#!/bin/sh
# Portable build + test driver.
#
# Works on Linux, macOS and Git Bash on Windows. The Makefile is the primary
# interface; this script exists so the project can be verified on a machine
# without `make`.
set -eu

CC="${CC:-}"
if [ -z "$CC" ]; then
    for candidate in cc gcc clang; do
        if command -v "$candidate" >/dev/null 2>&1; then
            CC="$candidate"
            break
        fi
    done
fi
if [ -z "$CC" ]; then
    echo "error: no C compiler found (tried cc, gcc, clang)" >&2
    exit 1
fi

CFLAGS="-std=c17 -Wall -Wextra -Werror -pedantic"

# libm is a separate library on Linux/macOS, but on Windows (MinGW/MSYS) the
# maths functions live in the C runtime and `-lm` does not resolve.
LDLIBS="-lm"
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) LDLIBS="" ;;
esac

BUILD=build
mkdir -p "$BUILD"

echo "== compiler =="
"$CC" --version 2>&1 | head -1

echo
echo "== unit tests =="
"$CC" $CFLAGS -O2 -Iinclude src/token_bucket.c tests/test_token_bucket.c \
    -o "$BUILD/tests" $LDLIBS
"./$BUILD/tests"

echo
echo "== CLI smoke test =="
"$CC" $CFLAGS -O2 -Iinclude src/token_bucket.c src/main.c \
    -o "$BUILD/ratelimit" $LDLIBS
"./$BUILD/ratelimit" --capacity 5 --rate 1 --cost 2 --requests 8 --interval 0.5

if "./$BUILD/ratelimit" --capacity 0 >/dev/null 2>&1; then
    echo "error: invalid input should have produced a non-zero exit status" >&2
    exit 1
fi
echo "  invalid input rejected with a non-zero exit status: OK"

echo
echo "== sanitizers (best effort) =="
if "$CC" $CFLAGS -g -O1 -fsanitize=address,undefined -Iinclude \
        src/token_bucket.c tests/test_token_bucket.c \
        -o "$BUILD/tests-asan" $LDLIBS 2>/dev/null; then

    # clang on Windows links the sanitizer runtime as a DLL; put it on PATH.
    case "$(uname -s)" in
        MINGW*|MSYS*|CYGWIN*)
            RESOURCE_DIR=$("$CC" -print-resource-dir 2>/dev/null || true)
            if [ -n "$RESOURCE_DIR" ] && [ -d "$RESOURCE_DIR/lib/windows" ]; then
                PATH="$RESOURCE_DIR/lib/windows:$PATH"
                export PATH
            fi
            ;;
    esac

    if "./$BUILD/tests-asan"; then
        echo "  ASan + UBSan: clean"
    else
        echo "  ASan + UBSan reported a problem" >&2
        exit 1
    fi
else
    echo "  (sanitizers not available with $CC — skipped)"
fi

echo
echo "ALL CHECKS PASSED"
