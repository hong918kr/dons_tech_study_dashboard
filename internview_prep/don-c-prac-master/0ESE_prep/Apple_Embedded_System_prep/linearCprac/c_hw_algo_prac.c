/*
아래는 대표적인 하드웨어 최적화/저수준 알고리즘 유형과 예시입니다.

1. 곱셈/나눗셈 없이 곱셈, 나눗셈, 나머지 구하기
시프트와 덧셈/뺄셈만으로 곱셈/나눗셈/나머지 구현
예: a * b를 시프트와 덧셈으로, a / b를 시프트와 뺄셈으로
2. 비트 카운트 (Set Bit Count, Hamming Weight)
하드웨어 명령 없이 1의 개수 세기
예: Brian Kernighan’s Algorithm, Lookup Table 등
3. 비트 리버스/엔디안 변환
하드웨어 없이 비트 순서 뒤집기, 바이트 순서 변환
4. 소프트웨어 CRC/체크섬
하드웨어 CRC 유닛 없이 소프트웨어로 CRC, 체크섬 계산
5. 원자적 연산/임계구역 보호
하드웨어 락 없이 소프트웨어적으로 임계구역 보호 (예: 인터럽트 마스킹, CAS, Spinlock)
6. 소프트웨어 타이머/디바운싱
하드웨어 타이머 없이 소프트웨어로 타임아웃, 디바운싱 구현
7. 링버퍼, 큐, FIFO, LRU 등 버퍼 관리
하드웨어 FIFO 없이 소프트웨어로 버퍼 관리
8. 소프트웨어 시리얼/통신 프로토콜
하드웨어 UART/SPI/I2C 없이 C로 시리얼 통신 시뮬레이션
9. 비트필드/마스크 연산
하드웨어 레지스터의 특정 비트만 조작(set/clear/toggle/get)
10. 소프트웨어 곱셈/나눗셈 최적화
곱셈/나눗셈이 느린 MCU에서 시프트, 덧셈, 뺄셈, Lookup Table 등으로 대체
11. 소프트웨어 난수 생성기 (Pseudo Random Number Generator)
하드웨어 RNG 없이 LFSR, Xorshift 등으로 난수 생성
12. 소프트웨어 PWM, ADC 샘플링
하드웨어 PWM/ADC 없이 타이머와 비교 연산으로 파형 생성/샘플링
13. 하드웨어 인터럽트 시퀀스 시뮬레이션
인터럽트 발생/처리 플로우를 함수로 구현
14. 소프트웨어로 메모리 세트/카피 (memset, memcpy)
하드웨어 가속 없이 최적화된 루프, word 단위 복사 등
15. 소프트웨어로 멀티바이트 레지스터 안전 읽기
8/16/32비트 레지스터 여러 개를 조합해 64비트 값 읽기 (레이스 컨디션 방지)
이런 문제들은 하드웨어의 한계나 특성을 이해하고, 소프트웨어적으로 극복하는 능력을 평가합니다.
임베디드/펌웨어/저수준 시스템 개발자라면 반드시 연습해두는 것이 좋습니다! 

*/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// 1. 곱셈/나눗셈 없이 곱셈, 나눗셈, 나머지 구하기
int mul_shift_add(int a, int b) {
    // TODO: implement
    return 0;
}
void divide_shift_sub(int a, int b, int* quotient, int* remainder) {
    // TODO: implement
}

// 2. 비트 카운트 (Set Bit Count, Hamming Weight)
int bit_count(uint32_t n) {
    // TODO: implement
    return 0;
}

// 3. 비트 리버스/엔디안 변환
uint32_t reverse_bits(uint32_t n) {
    // TODO: implement
    return 0;
}
uint32_t swap_endian32(uint32_t n) {
    // TODO: implement
    return 0;
}

// 4. 소프트웨어 CRC/체크섬
uint8_t crc8(const uint8_t* data, int len) {
    // TODO: implement
    return 0;
}

// 5. 원자적 연산/임계구역 보호 (예시: 소프트웨어 스핀락)
void lock_acquire(volatile int* lock) {
    // TODO: implement
}
void lock_release(volatile int* lock) {
    // TODO: implement
}

// 6. 소프트웨어 타이머/디바운싱 (예시: 소프트웨어 타이머 틱)
void timer_tick(void) {
    // TODO: implement
}
bool timer_expired(uint32_t start, uint32_t period) {
    // TODO: implement
    return false;
}

// 7. 링버퍼, 큐, FIFO, LRU 등 버퍼 관리
#define QMAX 8
typedef struct {
    uint8_t buf[QMAX];
    uint8_t head, tail, count;
} cb_t;
void cb_init(cb_t* q) {
    // TODO: implement
}
bool cb_push(cb_t* q, uint8_t data) {
    // TODO: implement
    return false;
}
bool cb_pop(cb_t* q, uint8_t* data) {
    // TODO: implement
    return false;
}

// 8. 소프트웨어 시리얼/통신 프로토콜 (예시: UART 송신)
void soft_uart_send(uint8_t data) {
    // TODO: implement
}

// 9. 비트필드/마스크 연산
void set_bit(volatile uint32_t* reg, uint8_t bit) {
    // TODO: implement
}
void clear_bit(volatile uint32_t* reg, uint8_t bit) {
    // TODO: implement
}
bool get_bit(uint32_t reg, uint8_t bit) {
    // TODO: implement
    return false;
}

// 10. 소프트웨어 곱셈/나눗셈 최적화 (Lookup Table 등)
int fast_mul_lookup(int a, int b) {
    // TODO: implement
    return 0;
}

// 11. 소프트웨어 난수 생성기 (Pseudo Random Number Generator)
uint32_t xorshift32(uint32_t* state) {
    // TODO: implement
    return 0;
}

// 12. 소프트웨어 PWM, ADC 샘플링 (예시)
void soft_pwm_set(uint8_t duty) {
    // TODO: implement
}
uint8_t soft_adc_sample(void) {
    // TODO: implement
    return 0;
}

// 13. 하드웨어 인터럽트 시퀀스 시뮬레이션
void simulate_interrupt_sequence(void) {
    // TODO: implement
}

// 14. 소프트웨어로 메모리 세트/카피 (memset, memcpy)
void* my_memset(void* dst, int val, size_t n) {
    // TODO: implement
    return dst;
}
void* my_memcpy(void* dst, const void* src, size_t n) {
    // TODO: implement
    return dst;
}

// 15. 소프트웨어로 멀티바이트 레지스터 안전 읽기
uint64_t safe_read_64bit(volatile uint32_t* low, volatile uint32_t* high) {
    // TODO: implement
    return 0;
}

// ------------------- 테스트 코드 -------------------

#define PASS 1
#define FAIL 0

int test_mul_shift_add() {
    int result = PASS;
    if (mul_shift_add(6, 7) != 42) result = FAIL;
    if (mul_shift_add(0, 5) != 0) result = FAIL;
    printf("1. mul_shift_add: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_divide_shift_sub() {
    int result = PASS;
    int q, r;
    divide_shift_sub(20, 3, &q, &r);
    if (q != 6 || r != 2) result = FAIL;
    divide_shift_sub(15, 5, &q, &r);
    if (q != 3 || r != 0) result = FAIL;
    printf("2. divide_shift_sub: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_bit_count() {
    int result = PASS;
    if (bit_count(0xF0F0F0F0) != 16) result = FAIL;
    if (bit_count(0xFFFFFFFF) != 32) result = FAIL;
    printf("3. bit_count: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_reverse_bits() {
    int result = PASS;
    if (reverse_bits(0x80000001) != 0x80000001) result = FAIL; // 실제 구현에 따라 다름
    printf("4. reverse_bits: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_swap_endian32() {
    int result = PASS;
    if (swap_endian32(0x12345678) != 0x78563412) result = FAIL;
    printf("5. swap_endian32: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_crc8() {
    int result = PASS;
    uint8_t arr[] = {1,2,3,4};
    if (crc8(arr, 4) != 0) result = FAIL; // 실제 CRC 값은 구현에 따라 다름
    printf("6. crc8: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_lock() {
    int result = PASS;
    volatile int lock = 0;
    lock_acquire(&lock);
    lock_release(&lock);
    printf("7. lock_acquire/release: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_timer() {
    int result = PASS;
    if (timer_expired(100, 10) != false) result = FAIL; // 구현에 따라 다름
    printf("8. timer_expired: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_ring_buffer() {
    int result = PASS;
    cb_t qbuf;
    cb_init(&qbuf);
    if (!cb_push(&qbuf, 42)) result = FAIL;
    uint8_t val;
    if (!cb_pop(&qbuf, &val) || val != 42) result = FAIL;
    printf("9. ring_buffer: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_soft_uart_send() {
    printf("10. soft_uart_send: PASS (manual check)\n");
    return PASS;
}

int test_bitfield_mask() {
    int result = PASS;
    volatile uint32_t reg = 0;
    set_bit(&reg, 3);
    if (reg != 0x8) result = FAIL;
    clear_bit(&reg, 3);
    if (reg != 0x0) result = FAIL;
    if (get_bit(reg, 3) != 0) result = FAIL;
    printf("11. bitfield/mask: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_fast_mul_lookup() {
    int result = PASS;
    if (fast_mul_lookup(3, 4) != 12) result = FAIL;
    printf("12. fast_mul_lookup: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_xorshift32() {
    int result = PASS;
    uint32_t state = 123456789;
    if (xorshift32(&state) == 0) result = FAIL; // 실제 값은 구현에 따라 다름
    printf("13. xorshift32: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_soft_pwm_adc() {
    printf("14. soft_pwm_set/soft_adc_sample: PASS (manual check)\n");
    return PASS;
}

int test_interrupt_sim() {
    printf("15. simulate_interrupt_sequence: PASS (manual check)\n");
    return PASS;
}

int test_my_memset_memcpy() {
    int result = PASS;
    char buf[10];
    my_memset(buf, 'A', 5);
    buf[5] = '\0';
    if (strcmp(buf, "AAAAA") != 0) result = FAIL;
    char src[] = "hello";
    my_memcpy(buf, src, 6);
    if (strcmp(buf, "hello") != 0) result = FAIL;
    printf("16. my_memset/my_memcpy: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_safe_read_64bit() {
    int result = PASS;
    volatile uint32_t low = 0x12345678, high = 0x9ABCDEF0;
    if (safe_read_64bit(&low, &high) != 0x9ABCDEF012345678ULL) result = FAIL;
    printf("17. safe_read_64bit: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int main() {
    int total = 0;
    total += test_mul_shift_add();
    total += test_divide_shift_sub();
    total += test_bit_count();
    total += test_reverse_bits();
    total += test_swap_endian32();
    total += test_crc8();
    total += test_lock();
    total += test_timer();
    total += test_ring_buffer();
    total += test_soft_uart_send();
    total += test_bitfield_mask();
    total += test_fast_mul_lookup();
    total += test_xorshift32();
    total += test_soft_pwm_adc();
    total += test_interrupt_sim();
    total += test_my_memset_memcpy();
    total += test_safe_read_64bit();
    printf("Total Passed: %d/17\n", total);
    return 0;
}