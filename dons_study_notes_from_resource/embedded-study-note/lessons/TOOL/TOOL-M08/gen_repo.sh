#!/usr/bin/env bash
# TOOL-M08 — generate the deterministic 100-commit practice repo.
#   bash gen_repo.sh /tmp/TOOL-M08_work/repo
# Same input -> same commit hashes on any machine/git version: fixed names, e-mails, dates, TZ,
# no global/system git config, default branch forced to main.
#
# Planted history (subject tag [cNNN] = commit index, c000 = first):
#   c000  initial import (tag v1.0)          c020  tests/check.sh smoke test added
#   c040  (tag v1.1)                          c060  HAL rename -> main.c no longer compiles
#   c061  still broken (unrelated change)     c062  build fixed
#   c063  REGRESSION: ringbuf wrap off-by-one (header-only change; app aborts in assert)
#   c080  (tag v1.2)                          c085  in-repo test "temporarily" skips the ringbuf case
#   c088  PERF REGRESSION: table CRC replaced by bitwise CRC (same result, ~N x slower)
#   c099  HEAD of main
set -eu
DIR=${1:?usage: gen_repo.sh <new-dir>}
if [ -e "$DIR" ] && [ -n "$(ls -A "$DIR" 2>/dev/null)" ]; then echo "gen_repo: $DIR not empty" >&2; exit 2; fi
mkdir -p "$DIR"; cd "$DIR"

export GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_NOSYSTEM=1 TZ=UTC LC_ALL=C
git -c init.defaultBranch=main init -q .
git config core.autocrlf false
git config commit.gpgsign false

mkdir -p include src tests docs

# ---------------------------------------------------------------- initial sources
cat > Makefile <<'EOF'
# legacy firmware-host Makefile: objects depend on their .c only (no header deps)
CFLAGS ?= -std=c11 -O2 -Wall -Iinclude
SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:src/%.c=build/%.o)

app: build/app
build/app: $(OBJS)
	$(CC) $(OBJS) -o $@
build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@
build:
	mkdir -p build
clean:
	rm -rf build
.PHONY: app clean
EOF
printf 'build/\n' > .gitignore

cat > include/hal.h <<'EOF'
#ifndef HAL_H
#define HAL_H
void uart_puts(const char *s);   /* host stand-in for the board UART */
#endif
EOF
cat > src/uart.c <<'EOF'
#include <stdio.h>
#include "hal.h"

void uart_puts(const char *s) { fputs(s, stdout); }
EOF

cat > include/crc16.h <<'EOF'
#ifndef CRC16_H
#define CRC16_H
#include <stddef.h>
#include <stdint.h>
void crc16_init(void);
uint16_t crc16_ccitt(const uint8_t *p, size_t n);   /* CRC-16/CCITT-FALSE */
#endif
EOF
crc_table() { cat > src/crc16.c <<'EOF'
#include "crc16.h"

static uint16_t table[256];

void crc16_init(void)
{
    for (unsigned i = 0; i < 256; i++) {
        uint16_t c = (uint16_t)(i << 8);
        for (int b = 0; b < 8; b++)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
        table[i] = c;
    }
}

uint16_t crc16_ccitt(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--)
        crc = (uint16_t)((crc << 8) ^ table[((crc >> 8) ^ *p++) & 0xFF]);
    return crc;
}
EOF
}
crc_bitwise() { cat > src/crc16.c <<'EOF'
#include "crc16.h"

/* no lookup table: saves 512 B of flash on the MCU build */
void crc16_init(void) { }

uint16_t crc16_ccitt(const uint8_t *p, size_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint16_t)(*p++ << 8);
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}
EOF
}
crc_table

ringbuf_h() {  # $1 = wrap condition
cat > include/ringbuf.h <<EOF
#ifndef RINGBUF_H
#define RINGBUF_H
#define RB_CAP 8u

typedef struct {
    int buf[RB_CAP];
    unsigned head, tail, count;
} ringbuf_t;

static inline unsigned rb_next(unsigned i) { return ($1) ? 0u : i + 1; }

void rb_init(ringbuf_t *rb);
int rb_push(ringbuf_t *rb, int v);   /* -1 when full  */
int rb_pop(ringbuf_t *rb, int *v);   /* -1 when empty */
#endif
EOF
}
ringbuf_h 'i + 1 >= RB_CAP'
cat > src/ringbuf.c <<'EOF'
#include <assert.h>
#include "ringbuf.h"

void rb_init(ringbuf_t *rb) { rb->head = rb->tail = rb->count = 0; }

int rb_push(ringbuf_t *rb, int v)
{
    if (rb->count == RB_CAP) return -1;
    assert(rb->head < RB_CAP);
    rb->buf[rb->head] = v;
    rb->head = rb_next(rb->head);
    rb->count++;
    return 0;
}

int rb_pop(ringbuf_t *rb, int *v)
{
    if (rb->count == 0) return -1;
    assert(rb->tail < RB_CAP);
    *v = rb->buf[rb->tail];
    rb->tail = rb_next(rb->tail);
    rb->count--;
    return 0;
}
EOF

main_c() {  # $1 = name of the UART function main.c calls
cat > src/main.c <<EOF
#define _POSIX_C_SOURCE 200809L   /* clock_gettime under strict -std=c11 on glibc */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "crc16.h"
#include "hal.h"
#include "ringbuf.h"

extern const char fw_version[];

static void test_crc(void)
{
    char line[64];
    snprintf(line, sizeof line, "crc16 \"123456789\" = 0x%04X\n",
             (unsigned)crc16_ccitt((const uint8_t *)"123456789", 9));
    $1(line);
}

static void test_rb(void)
{
    ringbuf_t rb;
    char line[128];
    int v, n;
    rb_init(&rb);
    for (int i = 1; i <= 20; i++) (void)rb_push(&rb, i);   /* 9..20 dropped (full) */
    for (int i = 0; i < 3; i++) (void)rb_pop(&rb, &v);     /* drop 1 2 3 */
    for (int i = 21; i <= 23; i++) (void)rb_push(&rb, i);  /* wraps around */
    n = snprintf(line, sizeof line, "rb order =");
    while (rb_pop(&rb, &v) == 0)
        n += snprintf(line + n, sizeof line - (size_t)n, " %d", v);
    snprintf(line + n, sizeof line - (size_t)n, "\n");
    $1(line);
}

static void bench(void)
{
    static uint8_t buf[1u << 20];
    uint32_t x = 1;
    struct timespec t0, t1;
    uint16_t acc = 0;
    for (size_t i = 0; i < sizeof buf; i++) { x = x * 1664525u + 1013904223u; buf[i] = (uint8_t)(x >> 24); }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int r = 0; r < 16; r++) { buf[0] = (uint8_t)r; acc = (uint16_t)(acc + crc16_ccitt(buf, sizeof buf)); }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("bench: 16 MiB crc=0x%04X ns=%lld\n", (unsigned)acc,
           (long long)(t1.tv_sec - t0.tv_sec) * 1000000000LL + (t1.tv_nsec - t0.tv_nsec));
}

int main(int argc, char **argv)
{
    const char *what = argc > 1 ? argv[1] : "all";
    char line[64];
    crc16_init();
    if (strcmp(what, "bench") == 0) { bench(); return 0; }
    snprintf(line, sizeof line, "fw version %s\n", fw_version);
    $1(line);
    if (!strcmp(what, "all") || !strcmp(what, "crc")) test_crc();
    if (!strcmp(what, "all") || !strcmp(what, "rb"))  test_rb();
    $1("selftest done\n");
    return 0;
}
EOF
}
main_c uart_puts

version_c() { printf 'const char fw_version[] = "1.0.%d";\n' "$1" > src/version.c; }
version_c 0
printf '# notes\n\nHost build of the sensor-hub firmware core (crc16, ringbuf, uart shim).\n' > docs/notes.md
printf '# Changelog\n\n' > CHANGELOG.md

# ---------------------------------------------------------------- commit helper
AUTHORS=("Mina Park:mina@example.com" "Joel Ortiz:joel@example.com" "Priya Rao:priya@example.com")
commit() {  # $1 = index, $2 = subject (without tag)
    local i=$1 a=${AUTHORS[$(( $1 % 3 ))]}
    local subj; subj=$(printf '[c%03d] %s' "$i" "$2")
    printf -- '- %s\n' "$subj" >> CHANGELOG.md
    local when; when=$(( 1767603600 + i * 5400 ))           # 2026-01-05 09:00 UTC + 90 min per commit
    git add -A
    GIT_AUTHOR_NAME=${a%%:*} GIT_AUTHOR_EMAIL=${a##*:} GIT_AUTHOR_DATE="@$when +0000" \
    GIT_COMMITTER_NAME=${a%%:*} GIT_COMMITTER_EMAIL=${a##*:} GIT_COMMITTER_DATE="@$when +0000" \
        git commit -q -m "$subj"
}

ROUTINE=("docs: clarify uart shim" "build: tidy CFLAGS comment" "crc16: comment polynomial" \
         "docs: add bring-up notes" "ringbuf: document drop-when-full policy" "uart: comment line endings" \
         "chore: bump version" "docs: board pinout table" "ci: note host build steps" "app: comment selftest order" \
         "docs: fix typo" "chore: update changelog format")

for i in $(seq 0 99); do
    case $i in
    0)  commit 0 "initial import: crc16, ringbuf, uart shim, app"
        git tag v1.0 ;;
    20) cat > tests/check.sh <<'EOF'
#!/bin/sh
# project smoke test: run the app and compare with tests/expected.txt (version line ignored)
./build/app > build/out.txt || exit $?
grep -v '^fw version' build/out.txt | diff -u tests/expected.txt -
EOF
        printf 'crc16 "123456789" = 0x29B1\nrb order = 4 5 6 7 8 21 22 23\nselftest done\n' > tests/expected.txt
        commit 20 "tests: add smoke test (tests/check.sh)" ;;
    60) sed -i.bak 's/void uart_puts/void hal_uart_puts/' include/hal.h src/uart.c && rm -f include/hal.h.bak src/uart.c.bak
        commit 60 "hal: rename uart_puts -> hal_uart_puts" ;;
    61) printf '\n## HAL naming\nAll board calls are prefixed hal_.\n' >> docs/notes.md
        commit 61 "docs: describe hal_ prefix" ;;
    62) main_c hal_uart_puts
        commit 62 "app: fix build after HAL rename" ;;
    63) ringbuf_h 'i + 1 > RB_CAP'
        commit 63 "ringbuf: tidy wrap arithmetic" ;;
    85) cat > tests/check.sh <<'EOF'
#!/bin/sh
# project smoke test: run the app and compare with tests/expected.txt (version line ignored)
# ringbuf case skipped on CI for now (flaky, see #412) -- TODO re-enable
./build/app crc > build/out.txt || exit $?
grep -v '^fw version' build/out.txt | diff -u tests/expected.txt -
EOF
        printf 'crc16 "123456789" = 0x29B1\nselftest done\n' > tests/expected.txt
        commit 85 "tests: skip ringbuf case on CI (flaky, #412)" ;;
    88) crc_bitwise
        commit 88 "crc16: drop lookup table to save 512 B flash" ;;
    *)  version_c "$i"
        [ $(( i % 7 )) -eq 3 ] && printf -- '- note %d\n' "$i" >> docs/notes.md
        commit "$i" "${ROUTINE[$(( i % ${#ROUTINE[@]} ))]}" ;;
    esac
    case $i in 40) git tag v1.1 ;; 80) git tag v1.2 ;; esac
done
git gc -q --auto 2>/dev/null || true
echo "gen_repo: $(git rev-list --count main) commits, HEAD $(git rev-parse --short=12 HEAD), tags: $(git tag | tr '\n' ' ')"
