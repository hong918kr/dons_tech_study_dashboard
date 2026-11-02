/* ==== answer ==== */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define IMU_MAGIC     0xA5u
#define IMU_WIRE_LEN  22u       /* fixed wire size, independent of sizeof(imu_sample_t) */

typedef struct {
    uint8_t  sensor_id;
    uint16_t flags;             /* bit0 valid, bit1 saturated, ... */
    uint32_t seq;
    uint64_t timestamp_ns;
    int16_t  accel[3];          /* raw counts x, y, z */
} imu_sample_t;                 /* in-memory layout: padding is the compiler's choice */

static void put_be16(uint8_t *b, uint16_t v) { b[0] = (uint8_t)(v >> 8); b[1] = (uint8_t)v; }
static void put_be32(uint8_t *b, uint32_t v) { put_be16(b, (uint16_t)(v >> 16)); put_be16(b + 2, (uint16_t)v); }
static void put_be64(uint8_t *b, uint64_t v) { put_be32(b, (uint32_t)(v >> 32)); put_be32(b + 4, (uint32_t)v); }
static uint16_t get_be16(const uint8_t *b) { return (uint16_t)((b[0] << 8) | b[1]); }
static uint32_t get_be32(const uint8_t *b) { return ((uint32_t)get_be16(b) << 16) | get_be16(b + 2); }
static uint64_t get_be64(const uint8_t *b) { return ((uint64_t)get_be32(b) << 32) | get_be32(b + 4); }

size_t imu_serialize(const imu_sample_t *s, uint8_t *buf, size_t cap)
{
    if (cap < IMU_WIRE_LEN)
        return 0;
    buf[0] = IMU_MAGIC;
    buf[1] = s->sensor_id;
    put_be16(&buf[2], s->flags);
    put_be32(&buf[4], s->seq);
    put_be64(&buf[8], s->timestamp_ns);
    for (int i = 0; i < 3; i++)                  /* int16 -> uint16 is well-defined (mod 2^16) */
        put_be16(&buf[16 + 2 * i], (uint16_t)s->accel[i]);
    return IMU_WIRE_LEN;
}

bool imu_deserialize(const uint8_t *buf, size_t len, imu_sample_t *s)
{
    if (len < IMU_WIRE_LEN || buf[0] != IMU_MAGIC)
        return false;
    s->sensor_id    = buf[1];
    s->flags        = get_be16(&buf[2]);
    s->seq          = get_be32(&buf[4]);
    s->timestamp_ns = get_be64(&buf[8]);
    for (int i = 0; i < 3; i++) {                /* portable uint16 -> int16 */
        uint16_t u = get_be16(&buf[16 + 2 * i]);
        s->accel[i] = (int16_t)(u >= 0x8000u ? (int32_t)u - 0x10000 : (int32_t)u);
    }
    return true;
}
/* ==== tests ==== */
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool same(const imu_sample_t *a, const imu_sample_t *b)
{
    return a->sensor_id == b->sensor_id && a->flags == b->flags && a->seq == b->seq &&
           a->timestamp_ns == b->timestamp_ns && a->accel[0] == b->accel[0] &&
           a->accel[1] == b->accel[1] && a->accel[2] == b->accel[2];
}

int main(void)
{
    const imu_sample_t s = { .sensor_id = 3, .flags = 0x0001, .seq = 0x01020304u,
                             .timestamp_ns = 0x0000000123456789ull, .accel = { -1, 256, -32768 } };
    const uint8_t expect[IMU_WIRE_LEN] = { 0xA5, 0x03, 0x00, 0x01, 0x01, 0x02, 0x03, 0x04,
                                           0x00, 0x00, 0x00, 0x01, 0x23, 0x45, 0x67, 0x89,
                                           0xFF, 0xFF, 0x01, 0x00, 0x80, 0x00 };
    uint8_t raw[IMU_WIRE_LEN + 1];
    uint8_t *buf = &raw[1];                              /* deliberately unaligned */
    assert(imu_serialize(&s, buf, IMU_WIRE_LEN) == IMU_WIRE_LEN);
    assert(memcmp(buf, expect, IMU_WIRE_LEN) == 0);
    imu_sample_t d;
    memset(&d, 0, sizeof d);
    assert(imu_deserialize(buf, IMU_WIRE_LEN, &d) && same(&s, &d));

    /* extremes round-trip */
    const imu_sample_t m = { .sensor_id = 0xFF, .flags = 0xFFFF, .seq = 0xFFFFFFFFu,
                             .timestamp_ns = ~0ull, .accel = { 32767, -32768, 0 } };
    assert(imu_serialize(&m, buf, sizeof raw - 1) == IMU_WIRE_LEN);
    assert(imu_deserialize(buf, IMU_WIRE_LEN, &d) && same(&m, &d));

    /* errors */
    assert(imu_serialize(&s, buf, IMU_WIRE_LEN - 1) == 0);
    assert(!imu_deserialize(buf, IMU_WIRE_LEN - 1, &d));
    buf[0] = 0x5A;
    assert(!imu_deserialize(buf, IMU_WIRE_LEN, &d));

    /* why not memcpy the struct: in-memory size != wire size */
    printf("sizeof(imu_sample_t) = %zu, wire = %u\n", sizeof(imu_sample_t), IMU_WIRE_LEN);
    assert(sizeof(imu_sample_t) != IMU_WIRE_LEN);
    printf("B3 PASS\n");
    return 0;
}
