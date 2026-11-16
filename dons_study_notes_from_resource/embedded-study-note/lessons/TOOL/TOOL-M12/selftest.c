/* selftest.c - run-time checks for the fixed module (proves fw_clean.c is real,
 * working code and not just "code the analyzers happen to like").
 *
 *   cc -std=c11 -Wall -Wextra -Werror -Wthread-safety -Iinclude -Ithird_party selftest.c -o /tmp/TOOL-M12_st
 *
 * It #includes the module so it can reach the static state (ring, link state).
 * -DFW_SRC='"fw_bugs.c"' builds the same tests against the buggy file
 * (demo_runtime.sh) to compare static findings with what testing sees.
 * malloc/free are counted so a leak on an error path shows up as live != 0. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int live_allocs;
static void *count_malloc(size_t n)
{
    void *p = malloc(n);
    if (p != NULL) {
        live_allocs++;
    }
    return p;
}
static void count_free(void *p)
{
    if (p != NULL) {
        live_allocs--;
    }
    free(p);
}
#define malloc(n) count_malloc(n)
#define free(p) count_free(p)

#ifndef FW_SRC
#define FW_SRC "fw_clean.c"
#endif
#include FW_SRC

#undef malloc
#undef free

/* flash stubs: the first write can be made to fail */
static int flash_fail_next;
static int verify_calls;
static char flash_img[64];

int flash_write(uint32_t addr, const void *src, size_t n)
{
    (void)addr;
    if (flash_fail_next) {
        flash_fail_next = 0;
        return -5; /* -EIO */
    }
    memcpy(flash_img, src, n < sizeof flash_img ? n : sizeof flash_img - 1);
    return 0;
}

int flash_verify(uint32_t addr, const void *src, size_t n)
{
    (void)addr;
    verify_calls++;
    return memcmp(flash_img, src, n) == 0 ? 0 : -1;
}

static int passed, total;
#define CHECK(cond)                                                    \
    do {                                                               \
        total++;                                                       \
        if (cond) {                                                    \
            passed++;                                                  \
        } else {                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #cond);             \
        }                                                              \
    } while (0)

/* frame: [A5][type][crc][0][n_rec LE32][records] */
static size_t make_frame(uint8_t *f, uint8_t type, uint32_t n_rec, size_t body_len)
{
    f[0] = 0xA5u;
    f[1] = type;
    f[3] = 0;
    f[4] = (uint8_t)n_rec;
    f[5] = (uint8_t)(n_rec >> 8);
    f[6] = (uint8_t)(n_rec >> 16);
    f[7] = (uint8_t)(n_rec >> 24);
    for (size_t i = 0; i < body_len; i++) {
        f[8 + i] = (uint8_t)(0x10u + i);
    }
    f[2] = crc8(f + 8, body_len);
    return 8 + body_len;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); /* see progress even if the buggy build crashes */
    uint8_t f[64];
    size_t n;

    /* uart_init: valid code programs BRR, bad code is rejected (FIX04) */
    CHECK(uart_init(2) == 0 && UART1->BRR == 729u);
    CHECK(uart_init(9) == -1 && UART1->BRR == 729u);

    /* ring: 64 fit, the 65th fails AND the lock is released (FIX07) */
    int ok = 0;
    for (int i = 0; i < 64; i++) {
        ok += rb_push(&rx_rb, (uint8_t)i) == 0;
    }
    CHECK(ok == 64);
    CHECK(rb_push(&rx_rb, 0xEE) == -1);
    int lock_free = pthread_mutex_trylock(&rx_rb.lock.m) == 0;
    CHECK(lock_free);
    if (lock_free) {
        pthread_mutex_unlock(&rx_rb.lock.m);
        uint8_t b = 0, last = 0;
        int popped = 0;
        while (rb_pop(&rx_rb, &b) == 0) {
            last = b;
            popped++;
        }
        CHECK(popped == 64 && last == 63u);
    }

    /* parser: good frame, bad CRC without leak (FIX02), wrapped size (FIX08) */
    n = make_frame(f, 0, 2, 12);
    packet_t *p = parse_frame(f, n);
    CHECK(p != NULL && p->n_rec == 2u && p->recs[11] == 0x1Bu);
    packet_free(p);
    f[2] ^= 0xFFu;
    CHECK(parse_frame(f, n) == NULL && live_allocs == 0);
    /* 0x2AAAAAAB * 6 = 0x1_0000_0002: wraps to 2 in 32-bit arithmetic */
    n = make_frame(f, 0, 0x2AAAAAABu, 2);
    CHECK(parse_frame(f, n) == NULL);

    /* dispatch: unknown id 3 (FIX01/FIX03), failing command (FIX09) */
    n = make_frame(f, 3, 0, 0);
    CHECK(dispatch(parse_frame(f, n)) == -1 && cmd_hit_count(3) == 0u);
    n = make_frame(f, 1, 1, 6); /* reset with a record -> -1 */
    CHECK(dispatch(parse_frame(f, n)) == -1 && cmd_hit_count(1) == 1u);
    n = make_frame(f, 0, 3, 18); /* ping returns n_rec */
    CHECK(dispatch(parse_frame(f, n)) == 3 && live_allocs == 0);

    /* link FSM: -95 dBm is below the -90 floor (FIX05) */
    CHECK(link_step(-60) == LINK_UP);
    CHECK(link_step(-95) == LINK_DEGRADED);

    /* stats: format (FIX10), write error is not overwritten (FIX06) */
    for (int i = 0; i < 5; i++) {
        link_tick_1ms();
    }
    CHECK(link_save_stats(0x08060000u) == 0 && strcmp(flash_img, "up 5 ms state 2\n") == 0);
    flash_fail_next = 1;
    verify_calls = 0;
    CHECK(link_save_stats(0x08060000u) == -5 && verify_calls == 0);

    printf("selftest(%s): %d/%d OK\n", FW_SRC, passed, total);
    return passed == total ? 0 : 1;
}
