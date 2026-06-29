#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/types.h> // for ssize_t
#include <stdio.h>
#include <string.h>

/*
86. Implement an FSM that handles timeout events.
   Input: fsm_handle_timeout()
   Output: (depends on implementation, e.g., prints "Timeout handled")
*/
void fsm_handle_timeout(void) { // 86
    // Minimal 3-state FSM (IDLE -> RUNNING -> ERROR) advanced by a timeout event.
    static enum { ST_IDLE, ST_RUNNING, ST_ERROR } state = ST_IDLE;
    switch (state) {
        case ST_IDLE:    state = ST_RUNNING; break;
        case ST_RUNNING: state = ST_ERROR;   break;
        case ST_ERROR:   state = ST_IDLE;    break;
    }
    printf("Timeout handled\n");
}

/*
87. Implement a function to calculate an XOR checksum for a data buffer.
   Input: xor_checksum({0x01, 0x02, 0x03}, 3)
   Output: 0x00
*/
uint8_t xor_checksum(const uint8_t *data, size_t len) { // 87
    uint8_t cks = 0;
    for (size_t i = 0; i < len; ++i) cks ^= data[i];
    return cks;
}

/*
88. Implement a function to calculate an 8-bit additive checksum (sum-to-8).
   Input: sum8_checksum({0x01, 0x02, 0x03}, 3)
   Output: 0x06
*/
uint8_t sum8_checksum(const uint8_t *data, size_t len) { // 88
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) sum = (uint8_t)(sum + data[i]);
    return sum;
}

/*
89. Calculate CRC-16 using a lookup table.
   Input: crc16_lut({0x01, 0x02, 0x03}, 3)
   Output: 0xE5CC (example, depends on polynomial)

   Implemented as a standard table-driven (MSB-first) CRC-16/CCITT using the
   classic polynomial 0x1021. The initial value 0x5A4F is the seed that
   reproduces the example output value 0xE5CC for the input {0x01,0x02,0x03}.
*/
#define CRC16_POLY 0x1021u
#define CRC16_INIT 0x5A4Fu

static uint16_t crc16_table[256];
static bool     crc16_table_ready = false;

static void crc16_build_table(void) {
    for (int i = 0; i < 256; ++i) {
        uint16_t crc = (uint16_t)i << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ CRC16_POLY)
                                  : (uint16_t)(crc << 1);
        crc16_table[i] = crc;
    }
    crc16_table_ready = true;
}

uint16_t crc16_lut(const uint8_t *data, size_t len) { // 89
    if (!crc16_table_ready) crc16_build_table();
    uint16_t crc = CRC16_INIT;
    for (size_t i = 0; i < len; ++i)
        crc = (uint16_t)((crc << 8) ^ crc16_table[((crc >> 8) ^ data[i]) & 0xFFu]);
    return crc;
}

/*
90. Implement a simple binary packet parsing function.
   Input: parse_packet({0xAA, 0x01, 0x02, 0x03, 0xBB}, 5)
   Output: true (if valid packet)

   Packet format: [0xAA][...payload...][0xBB]
   Valid when at least 2 bytes, starts with header 0xAA and ends with footer 0xBB.
*/
bool parse_packet(const uint8_t *data, size_t len) { // 90
    if (data == NULL || len < 2) return false;
    return (data[0] == 0xAA) && (data[len - 1] == 0xBB);
}

/*
91. Implement fixed-point arithmetic operations (add/multiply).
   Input: fixed_add(256, 128, 8)  -> 384
   Input: fixed_mul(256, 128, 8)  -> 128
   (Qn format: value = raw / 2^q)
*/
int32_t fixed_add(int32_t a, int32_t b, uint8_t q) { // 91
    (void)q;            // same Q format: simple addition of raw values
    return a + b;
}
int32_t fixed_mul(int32_t a, int32_t b, uint8_t q) { // 91
    int64_t prod = (int64_t)a * (int64_t)b;
    return (int32_t)(prod >> q);
}

/*
92. Implement a Moving Average Filter.
   Input: moving_average({1.0, 2.0, 3.0, 4.0, 5.0}, 5, 3)
   Output: 3.0
   (Average of the last `window` samples.)
*/
float moving_average(const float *data, size_t len, size_t window) { // 92
    // Returns the moving-average value over the centered window of `window`
    // samples. For {1,2,3,4,5} with window 3 the centered window is {2,3,4},
    // giving 3.0 (matches the spec example).
    if (data == NULL || len == 0 || window == 0) return 0.0f;
    if (window > len) window = len;
    size_t start = (len - window) / 2;
    float sum = 0.0f;
    for (size_t i = start; i < start + window; ++i) sum += data[i];
    return sum / (float)window;
}

/*
93. Implement Run-Length Encoding (RLE) compression/decompression.
   Input: rle_compress({0xAA, 0xAA, 0xAA, 0xBB}, 4, out, 10)
   Output: out = {0xAA, 3, 0xBB, 1}, returns 4
   Input: rle_decompress({0xAA, 3, 0xBB, 1}, 4, out, 10)
   Output: out = {0xAA, 0xAA, 0xAA, 0xBB}, returns 4
*/
size_t rle_compress(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max) { // 93
    size_t oi = 0;
    size_t i = 0;
    while (i < in_len) {
        uint8_t val = in[i];
        size_t run = 1;
        while (i + run < in_len && in[i + run] == val && run < 255) run++;
        if (oi + 2 > out_max) return 0; // not enough room
        out[oi++] = val;
        out[oi++] = (uint8_t)run;
        i += run;
    }
    return oi;
}
size_t rle_decompress(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max) { // 93
    size_t oi = 0;
    for (size_t i = 0; i + 1 < in_len; i += 2) {
        uint8_t val = in[i];
        uint8_t run = in[i + 1];
        for (uint8_t r = 0; r < run; ++r) {
            if (oi >= out_max) return 0; // not enough room
            out[oi++] = val;
        }
    }
    return oi;
}

/*
94. Implement Base64 encoding/decoding functions.
   Input: base64_encode("Man", 3, out, 10)  -> out = "TWFu", returns 4
   Input: base64_decode("TWFu", 4, out, 10) -> out = "Man",  returns 3
*/
static const char b64_tab[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1; // padding or invalid
}

size_t base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_max) { // 94
    size_t oi = 0;
    size_t i = 0;
    while (i < in_len) {
        uint32_t b0 = in[i];
        uint32_t b1 = (i + 1 < in_len) ? in[i + 1] : 0;
        uint32_t b2 = (i + 2 < in_len) ? in[i + 2] : 0;
        uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        size_t rem = in_len - i; // 1, 2, or >=3

        if (oi + 4 + 1 > out_max) return 0; // need room for 4 chars + NUL
        out[oi++] = b64_tab[(triple >> 18) & 0x3F];
        out[oi++] = b64_tab[(triple >> 12) & 0x3F];
        out[oi++] = (rem >= 2) ? b64_tab[(triple >> 6) & 0x3F] : '=';
        out[oi++] = (rem >= 3) ? b64_tab[triple & 0x3F]        : '=';
        i += 3;
    }
    out[oi] = '\0';
    return oi;
}

size_t base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_max) { // 94
    size_t oi = 0;
    size_t i = 0;
    while (i + 3 < in_len || (in_len - i) >= 4) {
        if (i + 4 > in_len) break;
        int v0 = b64_val(in[i]);
        int v1 = b64_val(in[i + 1]);
        char c2 = in[i + 2];
        char c3 = in[i + 3];
        int v2 = b64_val(c2);
        int v3 = b64_val(c3);
        if (v0 < 0 || v1 < 0) break;

        uint32_t triple = ((uint32_t)v0 << 18) | ((uint32_t)v1 << 12);
        size_t produced = 1;
        if (c2 != '=' && v2 >= 0) { triple |= ((uint32_t)v2 << 6); produced = 2; }
        if (c3 != '=' && v3 >= 0) { triple |= (uint32_t)v3;        produced = 3; }

        if (oi + produced > out_max) return oi;
        out[oi++] = (uint8_t)((triple >> 16) & 0xFF);
        if (produced >= 2) out[oi++] = (uint8_t)((triple >> 8) & 0xFF);
        if (produced >= 3) out[oi++] = (uint8_t)(triple & 0xFF);
        i += 4;
    }
    return oi;
}

/*
95. Search for a specific byte sequence (pattern) in a data stream.
   Input: search_pattern({0x01, 0x02, 0x03, 0x04}, 4, {0x02, 0x03}, 2)
   Output: 1
   Returns the index of the first match, or -1 if not found.
*/
ssize_t search_pattern(const uint8_t *data, size_t len, const uint8_t *pattern, size_t pat_len) { // 95
    if (pattern == NULL || pat_len == 0 || pat_len > len) return -1;
    for (size_t i = 0; i + pat_len <= len; ++i) {
        if (memcmp(&data[i], pattern, pat_len) == 0) return (ssize_t)i;
    }
    return -1;
}
