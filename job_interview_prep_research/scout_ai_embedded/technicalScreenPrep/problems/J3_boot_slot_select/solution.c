/*
 * J3  Pick a Boot Slot (A/B images with rollback)
 *
 * A bootloader keeps two firmware slots (A and B) in flash. Each slot starts with a
 * 16-byte little-endian header, followed by the image:
 *
 *   off 0  magic       u32  0x544F4F42 ("BOOT" in memory)
 *   off 4  version     u32
 *   off 8  image_size  u32  bytes of image after the header, 8 .. slot_size-16
 *   off 12 crc32       u32  CRC-32/IEEE over the image bytes (given: crc32())
 *   off 16 image ...   starts with a Cortex-M vector table:
 *                        word0 = initial SP, word1 = reset handler
 *
 * Part A - image_check(): validate one slot, in this order, and fill *out on IMG_OK.
 *   IMG_BAD_MAGIC  : magic mismatch (erased flash 0xFF.. also lands here)
 *   IMG_BAD_SIZE   : slot_size < 16, or image_size < 8 (no room for SP + reset),
 *                    or image_size > slot_size - 16
 *   IMG_BAD_CRC    : crc32(image, image_size) != header crc32
 *   IMG_BAD_VECTOR : SP not in [RAM_START, RAM_END] or not 4-byte aligned, or the reset
 *                    handler has no Thumb bit (bit0) or (addr & ~1) is outside
 *                    [image_base, image_base + image_size), image_base = slot_base + 16
 *   Parse with explicit byte assembly - do not cast the byte array to a struct.
 *
 * Part B - boot_select(): persisted BootState (small EEPROM record) decides the slot.
 *   - A slot is usable if it is not in bad_mask and image_check() == IMG_OK.
 *   - If pending != NO_PENDING (an update that was installed but not yet confirmed):
 *       * pending slot not usable          -> mark it bad, clear pending, attempts = 0
 *       * attempts >= MAX_ATTEMPTS         -> same (rollback)
 *       * otherwise attempts++ and return the pending slot (trial boot)
 *   - Otherwise return the usable slot with the highest version (tie -> active),
 *     and set active to it. No usable slot -> return -1 (stay in bootloader / DFU).
 *   - A corrupt record (active > 1, pending not 0/1/NO_PENDING) is sanitized first.
 * Part C - boot_confirm(): the app calls this once it is healthy:
 *   pending becomes active, pending = NO_PENDING, attempts = 0.
 *
 * Reference solution.  Build & run:  ./run.sh J3 sol
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

#define SLOT_SIZE     4096u
#define HDR_SIZE      16u
#define IMG_MAGIC     0x544F4F42u          /* bytes 42 4F 4F 54 = "BOOT" */
#define RAM_START     0x20000000u
#define RAM_END       0x20020000u          /* 128 KiB; initial SP may equal RAM_END */
#define MAX_ATTEMPTS  3u
#define NO_PENDING    0xFFu

static const uint32_t SLOT_BASE[2] = { 0x08004000u, 0x08024000u };

typedef enum { IMG_OK, IMG_BAD_MAGIC, IMG_BAD_SIZE, IMG_BAD_CRC, IMG_BAD_VECTOR } ImgStatus;
typedef struct { uint32_t version, image_size, sp, reset; } ImgInfo;
typedef struct { uint8_t active; uint8_t pending; uint8_t attempts; uint8_t bad_mask; } BootState;

/* given library: CRC-32/IEEE (reflected, poly 0xEDB88320, init/xorout 0xFFFFFFFF) */
uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

/* little-endian u32 from any byte address (no alignment needed) */
uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

ImgStatus image_check(const uint8_t *slot, size_t slot_size, uint32_t slot_base, ImgInfo *out)
{
    if (slot_size < HDR_SIZE) return IMG_BAD_SIZE;
    if (rd32le(slot) != IMG_MAGIC) return IMG_BAD_MAGIC;

    uint32_t ver  = rd32le(slot + 4);
    uint32_t size = rd32le(slot + 8);
    uint32_t crc  = rd32le(slot + 12);
    if (size < 8u || size > slot_size - HDR_SIZE) return IMG_BAD_SIZE;  /* before touching image */

    const uint8_t *img = slot + HDR_SIZE;
    if (crc32(img, size) != crc) return IMG_BAD_CRC;

    uint32_t sp = rd32le(img), reset = rd32le(img + 4);
    uint32_t img_base = slot_base + HDR_SIZE;
    uint32_t entry = reset & ~1u;
    if (sp < RAM_START || sp > RAM_END || (sp & 3u) != 0) return IMG_BAD_VECTOR;
    if ((reset & 1u) == 0 || entry - img_base >= size) return IMG_BAD_VECTOR; /* one unsigned compare */

    if (out) *out = (ImgInfo){ ver, size, sp, reset };
    return IMG_OK;
}

int boot_select(BootState *st, const uint8_t *slots[2], size_t slot_size)
{
    ImgInfo info[2];
    bool ok[2];

    if (st->active > 1) st->active = 0;                         /* sanitize persisted record */
    if (st->pending > 1 && st->pending != NO_PENDING) st->pending = NO_PENDING;

    for (int i = 0; i < 2; i++)
        ok[i] = !(st->bad_mask & (1u << i)) &&
                image_check(slots[i], slot_size, SLOT_BASE[i], &info[i]) == IMG_OK;

    if (st->pending != NO_PENDING) {
        uint8_t p = st->pending;
        if (ok[p] && st->attempts < MAX_ATTEMPTS) {
            st->attempts++;                    /* persist BEFORE jumping: a crash still counts */
            return p;                          /* trial boot */
        }
        st->bad_mask |= (uint8_t)(1u << p);    /* rollback: never try this image again */
        ok[p] = false;
        st->pending = NO_PENDING;
        st->attempts = 0;
    }

    int pick;
    if (ok[0] && ok[1]) {
        if (info[0].version == info[1].version) pick = st->active;
        else pick = info[1].version > info[0].version ? 1 : 0;
    } else if (ok[0]) pick = 0;
    else if (ok[1])   pick = 1;
    else return -1;                            /* nothing bootable: stay in DFU */

    st->active = (uint8_t)pick;
    return pick;
}

void boot_confirm(BootState *st)
{
    if (st->pending == NO_PENDING) return;     /* already confirmed: no flash write */
    st->active = st->pending;
    st->pending = NO_PENDING;
    st->attempts = 0;
}

/* ---------------- test helpers (not part of the answer) ---------------- */
static void wr32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* Build a valid image for `slot_base`: SP = RAM_END, reset = image_base + 0x40 | 1. */
static void make_image(uint8_t *slot, uint32_t slot_base, uint32_t version, uint32_t size)
{
    memset(slot, 0xFF, SLOT_SIZE);                       /* erased flash */
    uint8_t *img = slot + HDR_SIZE;
    for (uint32_t i = 0; i < size; i++) img[i] = (uint8_t)(i * 7u + version);
    wr32le(img, RAM_END);
    wr32le(img + 4, (slot_base + HDR_SIZE + 0x40u) | 1u);
    wr32le(slot, IMG_MAGIC);
    wr32le(slot + 4, version);
    wr32le(slot + 8, size);
    wr32le(slot + 12, crc32(img, size));
}

static void reseal(uint8_t *slot, uint32_t size)          /* recompute CRC after editing */
{
    wr32le(slot + 12, crc32(slot + HDR_SIZE, size));
}

static uint8_t flash[2][SLOT_SIZE];

int main(void)
{
    const uint8_t *slots[2] = { flash[0], flash[1] };
    ImgInfo inf = {0};
    BootState st;

    /* 1. normal: A v1, B v2 both valid -> newer B; equal versions -> keep active */
    make_image(flash[0], SLOT_BASE[0], 1, 512);
    make_image(flash[1], SLOT_BASE[1], 2, 512);
    CHECK(image_check(flash[0], SLOT_SIZE, SLOT_BASE[0], &inf) == IMG_OK);
    CHECK(inf.version == 1 && inf.image_size == 512 && inf.sp == 0x20020000u && inf.reset == 0x08004051u);
    CHECK(image_check(flash[1], SLOT_SIZE, SLOT_BASE[1], &inf) == IMG_OK && inf.reset == 0x08024051u);
    st = (BootState){ 0, NO_PENDING, 0, 0 };
    CHECK(boot_select(&st, slots, SLOT_SIZE) == 1 && st.active == 1);
    make_image(flash[1], SLOT_BASE[1], 1, 512);
    st = (BootState){ 1, NO_PENDING, 0, 0 };
    CHECK(boot_select(&st, slots, SLOT_SIZE) == 1);       /* tie -> stay on active B */
    st = (BootState){ 0, NO_PENDING, 0, 0 };
    CHECK(boot_select(&st, slots, SLOT_SIZE) == 0);       /* tie -> stay on active A */

    /* 2. one flipped bit in the newer image -> BAD_CRC -> boot the older slot */
    make_image(flash[1], SLOT_BASE[1], 2, 512);
    flash[1][HDR_SIZE + 300] ^= 0x10;
    CHECK(image_check(flash[1], SLOT_SIZE, SLOT_BASE[1], &inf) == IMG_BAD_CRC);
    st = (BootState){ 0, NO_PENDING, 0, 0 };
    CHECK(boot_select(&st, slots, SLOT_SIZE) == 0 && st.active == 0);

    /* 3. bad magic: erased slot, and a big-endian header written by mistake */
    memset(flash[1], 0xFF, SLOT_SIZE);
    CHECK(image_check(flash[1], SLOT_SIZE, SLOT_BASE[1], &inf) == IMG_BAD_MAGIC);
    make_image(flash[1], SLOT_BASE[1], 2, 512);
    flash[1][0] = 0x54; flash[1][1] = 0x4F; flash[1][2] = 0x4F; flash[1][3] = 0x42;
    CHECK(image_check(flash[1], SLOT_SIZE, SLOT_BASE[1], &inf) == IMG_BAD_MAGIC);

    /* 4. size: 0, 7, slot_size-15 are rejected; exactly slot_size-16 is fine */
    make_image(flash[1], SLOT_BASE[1], 2, 512);
    wr32le(flash[1] + 8, 0);
    CHECK(image_check(flash[1], SLOT_SIZE, SLOT_BASE[1], &inf) == IMG_BAD_SIZE);
    wr32le(flash[1] + 8, 7);
    CHECK(image_check(flash[1], SLOT_SIZE, SLOT_BASE[1], &inf) == IMG_BAD_SIZE);
    wr32le(flash[1] + 8, SLOT_SIZE - HDR_SIZE + 1);
    CHECK(image_check(flash[1], SLOT_SIZE, SLOT_BASE[1], &inf) == IMG_BAD_SIZE);
    wr32le(flash[1] + 8, 0xFFFFFFFFu);
    CHECK(image_check(flash[1], SLOT_SIZE, SLOT_BASE[1], &inf) == IMG_BAD_SIZE);
    make_image(flash[1], SLOT_BASE[1], 2, SLOT_SIZE - HDR_SIZE);
    CHECK(image_check(flash[1], SLOT_SIZE, SLOT_BASE[1], &inf) == IMG_OK && inf.image_size == 4080);
    CHECK(image_check(flash[1], 8, SLOT_BASE[1], &inf) == IMG_BAD_SIZE);

    /* 5. vector table: no Thumb bit, image linked for slot A flashed into B, bad SP */
    make_image(flash[1], SLOT_BASE[1], 2, 512);
    wr32le(flash[1] + HDR_SIZE + 4, 0x08024050u); reseal(flash[1], 512);       /* bit0 clear */
    CHECK(image_check(flash[1], SLOT_SIZE, SLOT_BASE[1], &inf) == IMG_BAD_VECTOR);
    wr32le(flash[1] + HDR_SIZE + 4, 0x08004051u); reseal(flash[1], 512);       /* points into A */
    CHECK(image_check(flash[1], SLOT_SIZE, SLOT_BASE[1], &inf) == IMG_BAD_VECTOR);
    wr32le(flash[1] + HDR_SIZE + 4, 0x08024211u); reseal(flash[1], 512);       /* past image end */
    CHECK(image_check(flash[1], SLOT_SIZE, SLOT_BASE[1], &inf) == IMG_BAD_VECTOR);
    wr32le(flash[1] + HDR_SIZE + 4, 0x0802420Fu); reseal(flash[1], 512);       /* last halfword ok */
    CHECK(image_check(flash[1], SLOT_SIZE, SLOT_BASE[1], &inf) == IMG_OK);
    wr32le(flash[1] + HDR_SIZE, 0x20020004u); reseal(flash[1], 512);           /* SP above RAM */
    CHECK(image_check(flash[1], SLOT_SIZE, SLOT_BASE[1], &inf) == IMG_BAD_VECTOR);
    wr32le(flash[1] + HDR_SIZE, 0x2001FFFEu); reseal(flash[1], 512);           /* SP misaligned */
    CHECK(image_check(flash[1], SLOT_SIZE, SLOT_BASE[1], &inf) == IMG_BAD_VECTOR);
    st = (BootState){ 0, NO_PENDING, 0, 0 };
    CHECK(boot_select(&st, slots, SLOT_SIZE) == 0);

    /* 6. pending update B v3 crashes every time: 3 trials, then rollback to A, B marked bad */
    make_image(flash[0], SLOT_BASE[0], 2, 512);
    make_image(flash[1], SLOT_BASE[1], 3, 512);
    st = (BootState){ 0, 1, 0, 0 };
    CHECK(boot_select(&st, slots, SLOT_SIZE) == 1 && st.attempts == 1);   /* no confirm = crash */
    CHECK(boot_select(&st, slots, SLOT_SIZE) == 1 && st.attempts == 2);
    CHECK(boot_select(&st, slots, SLOT_SIZE) == 1 && st.attempts == 3);
    CHECK(boot_select(&st, slots, SLOT_SIZE) == 0);
    CHECK(st.active == 0 && st.pending == NO_PENDING && st.attempts == 0 && st.bad_mask == 0x02);
    CHECK(boot_select(&st, slots, SLOT_SIZE) == 0);       /* newer but bad: stays on A */

    /* 7. pending update confirmed on the 2nd try -> B becomes active and stays */
    st = (BootState){ 0, 1, 0, 0 };
    CHECK(boot_select(&st, slots, SLOT_SIZE) == 1 && st.attempts == 1);   /* crash */
    CHECK(boot_select(&st, slots, SLOT_SIZE) == 1 && st.attempts == 2);
    boot_confirm(&st);
    CHECK(st.active == 1 && st.pending == NO_PENDING && st.attempts == 0 && st.bad_mask == 0);
    CHECK(boot_select(&st, slots, SLOT_SIZE) == 1 && st.attempts == 0);
    boot_confirm(&st);                                    /* idempotent */
    CHECK(st.active == 1 && st.pending == NO_PENDING);

    /* 8. pending slot corrupt -> rolled back at once; both invalid -> -1 (DFU) */
    flash[1][HDR_SIZE + 100] ^= 0x01;
    st = (BootState){ 0, 1, 0, 0 };
    CHECK(boot_select(&st, slots, SLOT_SIZE) == 0 && st.bad_mask == 0x02 && st.pending == NO_PENDING);
    memset(flash[0], 0xFF, SLOT_SIZE);
    st = (BootState){ 1, NO_PENDING, 0, 0 };
    CHECK(boot_select(&st, slots, SLOT_SIZE) == -1);
    st = (BootState){ 7, 9, 0, 0 };                       /* garbage record + no image */
    CHECK(boot_select(&st, slots, SLOT_SIZE) == -1 && st.active <= 1 && st.pending == NO_PENDING);

    printf(g_fail ? "%d FAILED\n" : "ALL PASS\n", g_fail);
    return g_fail != 0;
}
