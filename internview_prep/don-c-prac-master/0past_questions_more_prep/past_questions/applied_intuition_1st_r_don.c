/*
Got this question from the first ronud of Applied Intuition (30m) - Sep 23 2025

When passing data between two embedded systems, we want to minimize the amount of data being sent on the bus. While the integer values may often be small, we need to retain the ability to send large values as well. To accommodate both requirements, implement a variable length encoding as described below. 

Your task is to implement encoding and decoding functions for a unsigned 64-bit integer (uint64_t).

Variable Length Encoding Scheme:

An integer will be encoded as an array of bytes where the total number of bytes is not specified. For each byte in the array:

The most significant bit (MSB) of each byte is used as a continuation bit:
If the MSB is 1, it means there are more bytes following.
If the MSB is 0, it indicates that the current byte is the last one.

The remaining 7 bits are the data bits:
The least significant 7 bits of the integer are added to the first byte
Each subsequent byte will contain the next least significant 7 bits

The encoding shall minimize the number of bytes needed to encode the integer given this scheme.

Use the examples below to understand the encoding scheme and implement the functions:

Encoding 127:

Binary representation: 0b0111 1111
Split into 7-bit chunks: 0b0111 1111
Applied MSB Rule: 0b0111 1111 (no continuation byte, MSB = 0)
Encoded bytes as hex: [ 0x7f ]


Encoding 128:

Binary representation: 0b1000 0000
Split into 7-bit chunks: 0b0000 0000 (lower 7 bits) and 0b000 00001 (upper bits)
Applied MSB Rule: 0b1000 0000 (MSB = 1, continuation byte exists) and 0b000 00001 (MSB = 0, last byte)
Encoded bytes as hex: [ 0xff, 0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x01 ] (10 bytes)

Implement the functions int encode(uint64_t encode_me, uint8_t* out) and int decode(uint8_t* decode_me, uint64_t* out) to satisfy the above encoding scheme.

*/


/* C code below */
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>

// Encode a uint64_t as a varint into the provide buffer.
// Returns the number of bytes used for encoding
// You may assume that "out" has enough memory allocated for the encoding
int encode(uint64_t encode_me, uint8_t* out) {
    
    
}

// Decode a varint from the provided buffer into a uint64_t.
// Returns the number of bytes read.
// You may assume that decode_me is well-formed.
int decode(const uint8_t* decode_me, uint64_t* out)
{
    
}

int main ()
{
    
    uint8_t buffer[10];
    uint64_t decoded_value;
    int encoded_length, decoded_length;

    // Test Case 1: Encoding 1
    printf("Testing 1... \n");
    memset(buffer, 0, sizeof(buffer));
    encoded_length = encode(1, buffer);
    assert(encoded_length == 1);
    assert(buffer[0] == 0x01);
    decoded_length = decode(buffer, &decoded_value);
    assert(decoded_length == 1);
    assert(decoded_value == 1);
    printf("Test Passed\n");


    // Test Case 2: Encoding 127
    printf("Testing 127... \n");
    memset(buffer, 0, sizeof(buffer));
    encoded_length = encode(127, buffer);
    assert(encoded_length == 1);
    assert(buffer[0] == 0x7f);
    decoded_length = decode(buffer, &decoded_value);
    assert(decoded_length == 1);
    assert(decoded_value == 127);
    printf("Test Passed\n");

    
    // Test Case 3: Encoding 128
    printf("Testing 128... \n");
    memset(buffer, 0, sizeof(buffer));
    encoded_length = encode(128, buffer);
    assert(encoded_length == 2);
    assert(buffer[0] == 0x80);
    assert(buffer[1] == 0x01);
    decoded_length = decode(buffer, &decoded_value);
    assert(decoded_length == 2);
    assert(decoded_value == 128);
    printf("Test Passed\n");

    // Test Case 4: Encoding 300
    printf("Testing 300... \n");
    memset(buffer, 0, sizeof(buffer));
    encoded_length = encode(300, buffer);
    assert(encoded_length == 2);
    assert(buffer[0] == 0xac);
    assert(buffer[1] == 0x02);
    decoded_length = decode(buffer, &decoded_value);
    assert(decoded_length == 2);
    assert(decoded_value == 300);
    printf("Test Passed\n");

    

    // Test Case 5: Encoding 16384
    printf("Testing 16384... \n");
    memset(buffer, 0, sizeof(buffer));
    encoded_length = encode(16384, buffer);
    assert(encoded_length == 3);
    assert(buffer[0] == 0x80);
    assert(buffer[1] == 0x80);
    assert(buffer[2] == 0x01);
    decoded_length = decode(buffer, &decoded_value);
    assert(decoded_length == 3);
    assert(decoded_value == 16384);
    printf("Test Passed\n");

    

    // Test Case 6: Encoding 123456789
    printf("Testing 123456789... \n");
    memset(buffer, 0, sizeof(buffer));
    encoded_length = encode(123456789, buffer);
    assert(encoded_length == 4);
    assert(buffer[0] == 0x95);
    assert(buffer[1] == 0x9a);
    assert(buffer[2] == 0xef);
    assert(buffer[3] == 0x3a);
    decoded_length = decode(buffer, &decoded_value);
    assert(decoded_length == 4);
    assert(decoded_value == 123456789);
    printf("Test Passed\n");

    // Test Case 7: Encoding 0xFFFFFFFFFFFFFFFF (Max uint64_t value)
    printf("Testing max int... \n");
    memset(buffer, 0, sizeof(buffer));
    encoded_length = encode(0xFFFFFFFFFFFFFFFF, buffer);
    assert(encoded_length == 10);
    assert(buffer[0] == 0xff);
    assert(buffer[1] == 0xff);
    assert(buffer[2] == 0xff);
    assert(buffer[3] == 0xff);
    assert(buffer[4] == 0xff);
    assert(buffer[5] == 0xff);
    assert(buffer[6] == 0xff);
    assert(buffer[7] == 0xff);
    assert(buffer[8] == 0xff);
    assert(buffer[9] == 0x01);

    decoded_length = decode(buffer, &decoded_value);
    assert(decoded_length == 10);
    assert(decoded_value == 0xFFFFFFFFFFFFFFFF);
    printf("Test Passed\n");


    // Test Case 8: Encoding 1
    printf("Testing encoding 0xABCDABCDABCDABCD... \n");
    memset(buffer, 0, sizeof(buffer));
    encoded_length = encode(0xABCDABCDABCDABCD, buffer);
    assert(encoded_length == 10);
    assert(buffer[0] == 0xcd);
    assert(buffer[1] == 0xd7);
    assert(buffer[2] == 0xb6);
    assert(buffer[3] == 0xde);
    assert(buffer[4] == 0xda);
    assert(buffer[5] == 0xf9);
    assert(buffer[6] == 0xea);
    assert(buffer[7] == 0xe6);
    assert(buffer[8] == 0xab);
    assert(buffer[9] == 0x1);
    decoded_length = decode(buffer, &decoded_value);
    assert(decoded_length == 10);
    assert(decoded_value == 0xABCDABCDABCDABCD);
    printf("Test Passed\n");


    printf("All test cases passed\n");

    return 0;
}