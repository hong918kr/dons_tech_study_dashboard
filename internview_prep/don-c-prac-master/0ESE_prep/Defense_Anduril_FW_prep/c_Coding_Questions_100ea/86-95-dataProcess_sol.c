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
    // TODO: implement
}

/*
87. Implement a function to calculate an XOR checksum for a data buffer.
   Input: xor_checksum({0x01, 0x02, 0x03}, 3)
   Output: 0x00
*/
uint8_t xor_checksum(const uint8_t *data, size_t len) { // 87
    // TODO: implement
}

/*
88. Implement a function to calculate an 8-bit additive checksum (sum-to-8).
   Input: sum8_checksum({0x01, 0x02, 0x03}, 3)
   Output: 0x06
*/
uint8_t sum8_checksum(const uint8_t *data, size_t len) { // 88
    // TODO: implement
}

/*
89. Calculate CRC-16 using a lookup table.
   Input: crc16_lut({0x01, 0x02, 0x03}, 3)
   Output: 0xE5CC (example, depends on polynomial)
*/
uint16_t crc16_lut(const uint8_t *data, size_t len) { // 89
    // TODO: implement
}

/*
90. Implement a simple binary packet parsing function.
   Input: parse_packet({0xAA, 0x01, 0x02, 0x03, 0xBB}, 5)
   Output: true (if valid packet)
*/
bool parse_packet(const uint8_t *data, size_t len) { // 90
    // TODO: implement
}

/*
91. Implement fixed-point arithmetic operations (add/multiply).
   Input: fixed_add(256, 128, 8)
   Output: 384
   Input: fixed_mul(256, 128, 8)
   Output: 128
*/
int32_t fixed_add(int32_t a, int32_t b, uint8_t q) { // 91
    // TODO: implement
}
int32_t fixed_mul(int32_t a, int32_t b, uint8_t q) { // 91
    // TODO: implement
}

/*
92. Implement a Moving Average Filter.
   Input: moving_average({1.0, 2.0, 3.0, 4.0, 5.0}, 5, 3)
   Output: 3.0
*/
float moving_average(const float *data, size_t len, size_t window) { // 92
    // TODO: implement
}

/*
93. Implement Run-Length Encoding (RLE) compression/decompression.
   Input: rle_compress({0xAA, 0xAA, 0xAA, 0xBB}, 4, out, 10)
   Output: out = {0xAA, 3, 0xBB, 1}, returns 4
   Input: rle_decompress({0xAA, 3, 0xBB, 1}, 4, out, 10)
   Output: out = {0xAA, 0xAA, 0xAA, 0xBB}, returns 4
*/
size_t rle_compress(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max) { // 93
    // TODO: implement
}
size_t rle_decompress(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_max) { // 93
    // TODO: implement
}

/*
94. Implement Base64 encoding/decoding functions.
   Input: base64_encode("Man", 3, out, 10)
   Output: out = "TWFu", returns 4
   Input: base64_decode("TWFu", 4, out, 10)
   Output: out = "Man", returns 3
*/
size_t base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_max) { // 94
    // TODO: implement
}
size_t base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_max) { // 94
    // TODO: implement
}

/*
95. Search for a specific byte sequence (pattern) in a data stream.
   Input: search_pattern({0x01, 0x02, 0x03, 0x04}, 4, {0x02, 0x03}, 2)
   Output: 1
*/
ssize_t search_pattern(const uint8_t *data, size_t len, const uint8_t *pattern, size_t pat_len) { // 95
    // TODO: implement
}

/*
96. Implement a simple parser (e.g., for NMEA GPS sentences).
*/
// 96

//예: NMEA GPS sentence를
int main(void) {
    // 86
    printf("fsm_handle_timeout() => (see implementation)\n");
    fsm_handle_timeout();

    // 87
    uint8_t data1[] = {0x01, 0x02, 0x03};
    printf("xor_checksum({0x01,0x02,0x03}, 3) = 0x%X\n", xor_checksum(data1, 3)); // 0x00

    // 88
    printf("sum8_checksum({0x01,0x02,0x03}, 3) = 0x%X\n", sum8_checksum(data1, 3)); // 0x06

    // 89
    printf("crc16_lut({0x01,0x02,0x03}, 3) = 0x%X\n", crc16_lut(data1, 3)); // 0xE5CC (example)

    // 90
    uint8_t packet[] = {0xAA, 0x01, 0x02, 0x03, 0xBB};
    printf("parse_packet({0xAA,0x01,0x02,0x03,0xBB}, 5) = %d\n", parse_packet(packet, 5)); // true

    // 91
    printf("fixed_add(256, 128, 8) = %d\n", fixed_add(256, 128, 8)); // 384
    printf("fixed_mul(256, 128, 8) = %d\n", fixed_mul(256, 128, 8)); // 128

    // 92
    float arr[] = {1.0, 2.0, 3.0, 4.0, 5.0};
    printf("moving_average({1.0,2.0,3.0,4.0,5.0}, 5, 3) = %.1f\n", moving_average(arr, 5, 3)); // 3.0

    // 93
    uint8_t rle_in[] = {0xAA, 0xAA, 0xAA, 0xBB};
    uint8_t rle_out[10] = {0};
    size_t rle_out_len = rle_compress(rle_in, 4, rle_out, 10);
    printf("rle_compress({0xAA,0xAA,0xAA,0xBB}, 4, out, 10) = ");
    for (size_t i = 0; i < rle_out_len; ++i) printf("0x%X ", rle_out[i]);
    printf(", returns %zu\n", rle_out_len); // out = {0xAA,3,0xBB,1}, returns 4

    uint8_t rle_dec_out[10] = {0};
    size_t rle_dec_len = rle_decompress(rle_out, rle_out_len, rle_dec_out, 10);
    printf("rle_decompress({0xAA,3,0xBB,1}, 4, out, 10) = ");
    for (size_t i = 0; i < rle_dec_len; ++i) printf("0x%X ", rle_dec_out[i]);
    printf(", returns %zu\n", rle_dec_len); // out = {0xAA,0xAA,0xAA,0xBB}, returns 4

    // 94
    char b64_out[10] = {0};
    size_t b64_len = base64_encode((const uint8_t *)"Man", 3, b64_out, 10);
    printf("base64_encode(\"Man\", 3, out, 10) = %s, returns %zu\n", b64_out, b64_len); // "TWFu", 4

    uint8_t b64_dec_out[10] = {0};
    size_t b64_dec_len = base64_decode("TWFu", 4, b64_dec_out, 10);
    printf("base64_decode(\"TWFu\", 4, out, 10) = %.*s, returns %zu\n", (int)b64_dec_len, b64_dec_out, b64_dec_len); // "Man", 3

    // 95
    uint8_t stream[] = {0x01, 0x02, 0x03, 0x04};
    uint8_t pattern[] = {0x02, 0x03};
    printf("search_pattern({0x01,0x02,0x03,0x04}, 4, {0x02,0x03}, 2) = %zd\n", search_pattern(stream, 4, pattern, 2)); // 1

    return 0;
}

/*
------------------ Expected Result ------------------

fsm_handle_timeout() => (see implementation)
xor_checksum({0x01,0x02,0x03}, 3) = 0x0
sum8_checksum({0x01,0x02,0x03}, 3) = 0x6
crc16_lut({0x01,0x02,0x03}, 3) = 0xE5CC
parse_packet({0xAA,0x01,0x02,0x03,0xBB}, 5) = 1
fixed_add(256, 128, 8) = 384
fixed_mul(256, 128, 8) = 128
moving_average({1.0,2.0,3.0,4.0,5.0}, 5, 3) = 3.0
rle_compress({0xAA,0xAA,0xAA,0xBB}, 4, out, 10) = 0xAA 0x3 0xBB 0x1 , returns 4
rle_decompress({0xAA,3,0xBB,1}, 4, out, 10) = 0xAA 0xAA 0xAA 0xBB , returns 4
base64_encode("Man", 3, out, 10) = TWFu, returns 4
base64_decode("TWFu", 4, out, 10) = Man, returns 3
search_pattern({0x01,0x02,0x03,0x04}, 4, {0x02,0x03}, 2) = 1

-----------------------------------------------------
*/