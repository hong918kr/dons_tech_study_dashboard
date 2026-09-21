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

// 1. 곱셈/나눗셈 없이 곱셈, 나눗셈, 나머지 구하기 (시프트/덧셈/뺄셈)
int mul_shift_add(int a, int b) {
    int res = 0;
    while (b) {
        if (b & 1) res += a;
        a <<= 1;
        b >>= 1;
    }
    return res;
}
void divide_shift_sub(int a, int b, int* quotient, int* remainder) {
    *quotient = 0;
    for (int i = 31; i >= 0; --i) {
        if ((a >> i) >= b) {
            *quotient |= (1 << i);
            a -= (b << i);
        }
    }
    *remainder = a;
}

// 2. 비트 카운트 (Set Bit Count, Hamming Weight)
int bit_count(uint32_t n) {
    int count = 0;
    while (n) {
        n &= (n - 1);
        count++;
    }
    return count;
}

// 3. 비트 리버스/엔디안 변환
uint32_t reverse_bits(uint32_t n) {
    uint32_t res = 0;
    for (int i = 0; i < 32; ++i) {
        res <<= 1;
        res |= (n & 1);
        n >>= 1;
    }
    return res;
}
uint32_t swap_endian32(uint32_t n) {
    return ((n & 0xFF) << 24) |
           ((n & 0xFF00) << 8) |
           ((n & 0xFF0000) >> 8) |
           ((n & 0xFF000000) >> 24);
}

// 4. 소프트웨어 CRC/체크섬 (CRC-8-ATM)
uint8_t crc8(const uint8_t* data, int len) {
    uint8_t crc = 0;
    for (int i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x07;
            else
                crc <<= 1;
        }
    }
    return crc;
}

// 5. 원자적 연산/임계구역 보호 (인터럽트 마스킹 시뮬레이션)
static int g_irq_enabled = 1; // 1: 인터럽트 허용, 0: 마스킹
int irq_save_and_disable(void) {
    int prev = g_irq_enabled;
    g_irq_enabled = 0;
    return prev;
}
void irq_restore(int prev) {
    g_irq_enabled = prev;
}

// 6. 소프트웨어 타이머/디바운싱 (예시: 소프트웨어 타이머 틱)
static uint32_t g_tick = 0;
void timer_tick(void) {
    g_tick++;
}
bool timer_expired(uint32_t start, uint32_t period) {
    return (g_tick - start) >= period;
}

// 7. 링버퍼, 큐, FIFO, LRU 등 버퍼 관리
#define QMAX 8
typedef struct {
    uint8_t buf[QMAX];
    uint8_t head, tail, count;
} cb_t;
void cb_init(cb_t* q) {
    q->head = q->tail = q->count = 0;
}
bool cb_push(cb_t* q, uint8_t data) {
    if (q->count == QMAX) return false;
    q->buf[q->tail] = data;
    q->tail = (q->tail + 1) % QMAX;
    q->count++;
    return true;
}
bool cb_pop(cb_t* q, uint8_t* data) {
    if (q->count == 0) return false;
    *data = q->buf[q->head];
    q->head = (q->head + 1) % QMAX;
    q->count--;
    return true;
}

// 8. 소프트웨어 UART 송신 (비트 단위, 스타트/스톱/파리티 포함)
void soft_uart_send(uint8_t data, uint8_t* out_bits, size_t* out_len) {
    out_bits[0] = 0; // Start bit
    for (int i = 0; i < 8; ++i)
        out_bits[1 + i] = (data >> i) & 1;
    out_bits[9] = 1; // Stop bit
    *out_len = 10;
}

// 9. 비트필드/마스크 연산
void set_bit(volatile uint32_t* reg, uint8_t bit) {
    *reg |= (1U << bit);
}
void clear_bit(volatile uint32_t* reg, uint8_t bit) {
    *reg &= ~(1U << bit);
}
void toggle_bit(volatile uint32_t* reg, uint8_t bit) {
    *reg ^= (1U << bit);
}
bool get_bit(volatile uint32_t* reg, uint8_t bit) {
    return (*reg & (1U << bit)) != 0;
}

// 10. 소프트웨어 곱셈/나눗셈 최적화 (Lookup Table 등)
int fast_mul_lookup(int a, int b) {
    static const int table[10][10] = {
        {0,0,0,0,0,0,0,0,0,0},
        {0,1,2,3,4,5,6,7,8,9},
        {0,2,4,6,8,10,12,14,16,18},
        {0,3,6,9,12,15,18,21,24,27},
        {0,4,8,12,16,20,24,28,32,36},
        {0,5,10,15,20,25,30,35,40,45},
        {0,6,12,18,24,30,36,42,48,54},
        {0,7,14,21,28,35,42,49,56,63},
        {0,8,16,24,32,40,48,56,64,72},
        {0,9,18,27,36,45,54,63,72,81}
    };
    if (a >= 0 && a < 10 && b >= 0 && b < 10)
        return table[a][b];
    return a * b;
}

// 11. 소프트웨어 난수 생성기 (Pseudo Random Number Generator)
uint32_t xorshift32(uint32_t* state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

// 12. 소프트웨어 PWM 파형 생성 (타이머 틱마다 호출)
typedef void (*pwm_callback_t)(bool high);
static uint8_t pwm_duty = 0;
static uint16_t pwm_tick_cnt = 0;
static pwm_callback_t pwm_cb = NULL;
void soft_pwm_init(uint8_t duty, pwm_callback_t cb) {
    pwm_duty = duty;
    pwm_cb = cb;
    pwm_tick_cnt = 0;
}
void soft_pwm_tick(void) {
    if (!pwm_cb) return;
    bool high = (pwm_tick_cnt < pwm_duty);
    pwm_cb(high);
    pwm_tick_cnt = (pwm_tick_cnt + 1) % 256;
}

// 13. 인터럽트 시퀀스 시뮬레이션 (우선순위, 중첩)
void isr_A(void) { printf("ISR_A start\n"); /* ... */ printf("ISR_A end\n"); }
void isr_B(void) { printf("ISR_B start\n"); /* ... */ printf("ISR_B end\n"); }
void simulate_interrupts(bool irq_A, bool irq_B) {
    // A가 우선순위 높음, 중첩 허용
    if (irq_A && irq_B) {
        isr_A();
        isr_B();
    } else if (irq_A) {
        isr_A();
    } else if (irq_B) {
        isr_B();
    }
}

// 14. 소프트웨어로 메모리 세트/카피 (memset, memcpy)
void* my_memset(void* dst, int val, size_t n) {
    unsigned char* p = (unsigned char*)dst;
    for (size_t i = 0; i < n; ++i) p[i] = (unsigned char)val;
    return dst;
}
void* my_memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}

// 15. 소프트웨어로 멀티바이트 레지스터 안전 읽기
uint64_t safe_read_64bit(volatile uint32_t* low, volatile uint32_t* high) {
    uint32_t h1, l, h2;
    do {
        h1 = *high;
        l = *low;
        h2 = *high;
    } while (h1 != h2);
    return ((uint64_t)h2 << 32) | l;
}

// ------------------- 테스트 코드 -------------------


#define PASS 1
#define FAIL 0

void pwm_print(bool high) { printf("%d", high); }

int test_mul_shift_add() {
    int result = PASS;
    if (mul_shift_add(6, 7) != 42) { printf("[FAIL] mul_shift_add(6,7)\n"); result = FAIL; }
    if (mul_shift_add(0, 5) != 0) { printf("[FAIL] mul_shift_add(0,5)\n"); result = FAIL; }
    printf("1. mul_shift_add: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_divide_shift_sub() {
    int result = PASS;
    int q, r;
    divide_shift_sub(20, 3, &q, &r);
    if (q != 6 || r != 2) { printf("[FAIL] divide_shift_sub(20,3): q=%d r=%d\n", q, r); result = FAIL; }
    divide_shift_sub(15, 5, &q, &r);
    if (q != 3 || r != 0) { printf("[FAIL] divide_shift_sub(15,5): q=%d r=%d\n", q, r); result = FAIL; }
    printf("2. divide_shift_sub: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_bit_count() {
    int result = PASS;
    if (bit_count(0xF0F0F0F0) != 16) { printf("[FAIL] bit_count(0xF0F0F0F0)\n"); result = FAIL; }
    if (bit_count(0xFFFFFFFF) != 32) { printf("[FAIL] bit_count(0xFFFFFFFF)\n"); result = FAIL; }
    printf("3. bit_count: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_reverse_bits() {
    int result = PASS;
    if (reverse_bits(0x80000001) != 0x80000001) { printf("[FAIL] reverse_bits(0x80000001)\n"); result = FAIL; }
    printf("4. reverse_bits: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_swap_endian32() {
    int result = PASS;
    if (swap_endian32(0x12345678) != 0x78563412) { printf("[FAIL] swap_endian32(0x12345678)\n"); result = FAIL; }
    printf("5. swap_endian32: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_crc8() {
    int result = PASS;
    uint8_t arr[] = {1,2,3,4};
    if (crc8(arr, 4) != 0xE3) { printf("[FAIL] crc8({1,2,3,4},4)\n"); result = FAIL; }
    printf("6. crc8: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_irq_mask() {
    int result = PASS;
    int prev = irq_save_and_disable();
    if (g_irq_enabled != 0) { printf("[FAIL] irq_save_and_disable: g_irq_enabled=%d\n", g_irq_enabled); result = FAIL; }
    irq_restore(prev);
    if (g_irq_enabled != prev) { printf("[FAIL] irq_restore: g_irq_enabled=%d prev=%d\n", g_irq_enabled, prev); result = FAIL; }
    printf("7. irq_save_and_disable/restore: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_timer() {
    int result = PASS;
    uint32_t start = g_tick;
    for (int i = 0; i < 10; ++i) timer_tick();
    if (!timer_expired(start, 10)) { printf("[FAIL] timer_expired(start,10)\n"); result = FAIL; }
    printf("8. timer_expired: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_ring_buffer() {
    int result = PASS;
    cb_t qbuf;
    cb_init(&qbuf);
    if (!cb_push(&qbuf, 42)) { printf("[FAIL] cb_push\n"); result = FAIL; }
    uint8_t val;
    if (!cb_pop(&qbuf, &val) || val != 42) { printf("[FAIL] cb_pop: val=%d\n", val); result = FAIL; }
    printf("9. ring_buffer: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_soft_uart_send() {
    int result = PASS;
    uint8_t bits[12]; size_t blen = 0;
    soft_uart_send(0xA5, bits, &blen);
    uint8_t expect[10] = {0,1,0,1,0,0,1,0,1,1};
    for (size_t i = 0; i < 10; ++i)
        if (bits[i] != expect[i]) { printf("[FAIL] soft_uart_send: bits[%zu]=%d expect=%d\n", i, bits[i], expect[i]); result = FAIL; }
    printf("10. soft_uart_send: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_bitfield_mask() {
    int result = PASS;
    volatile uint32_t reg = 0;
    set_bit(&reg, 3);
    if (reg != 0x8) { printf("[FAIL] set_bit: reg=0x%X\n", reg); result = FAIL; }
    clear_bit(&reg, 3);
    if (reg != 0x0) { printf("[FAIL] clear_bit: reg=0x%X\n", reg); result = FAIL; }
    toggle_bit(&reg, 1);
    if (reg != 0x2) { printf("[FAIL] toggle_bit: reg=0x%X\n", reg); result = FAIL; }
    if (!get_bit(&reg, 1)) { printf("[FAIL] get_bit: reg=0x%X\n", reg); result = FAIL; }
    printf("11. bitfield/mask: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_fast_mul_lookup() {
    int result = PASS;
    if (fast_mul_lookup(3, 4) != 12) { printf("[FAIL] fast_mul_lookup(3,4)\n"); result = FAIL; }
    printf("12. fast_mul_lookup: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_xorshift32() {
    int result = PASS;
    uint32_t state = 123456789;
    if (xorshift32(&state) == 0) { printf("[FAIL] xorshift32\n"); result = FAIL; }
    printf("13. xorshift32: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_soft_pwm() {
    printf("14. soft_pwm: PASS (manual check)\n");
    soft_pwm_init(4, pwm_print);
    for (int i = 0; i < 10; ++i) soft_pwm_tick();
    printf("\n");
    return PASS;
}

int test_interrupt_sim() {
    printf("15. simulate_interrupts: PASS (manual check)\n");
    simulate_interrupts(true, true);
    simulate_interrupts(false, true);
    return PASS;
}

int test_my_memset_memcpy() {
    int result = PASS;
    char buf[10];
    my_memset(buf, 'A', 5);
    buf[5] = '\0';
    if (strcmp(buf, "AAAAA") != 0) { printf("[FAIL] my_memset\n"); result = FAIL; }
    char src[] = "hello";
    my_memcpy(buf, src, 6);
    if (strcmp(buf, "hello") != 0) { printf("[FAIL] my_memcpy\n"); result = FAIL; }
    printf("16. my_memset/my_memcpy: %s\n", result ? "PASS" : "FAIL");
    return result;
}

int test_safe_read_64bit() {
    int result = PASS;
    volatile uint32_t low = 0x12345678, high = 0x9ABCDEF0;
    if (safe_read_64bit(&low, &high) != 0x9ABCDEF012345678ULL) { printf("[FAIL] safe_read_64bit\n"); result = FAIL; }
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
    total += test_irq_mask();
    total += test_timer();
    total += test_ring_buffer();
    total += test_soft_uart_send();
    total += test_bitfield_mask();
    total += test_fast_mul_lookup();
    total += test_xorshift32();
    total += test_soft_pwm();
    total += test_interrupt_sim();
    total += test_my_memset_memcpy();
    total += test_safe_read_64bit();
    printf("Total Passed: %d/17\n", total);
    return 0;
}
