/* COMM-M12 demo: bus_ops 간접 호출이 Cortex-M4 에서 실제로 무엇이 되는가 (freestanding, 링크하지 않음)
 * 빌드: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_indirect.c -o -
 *   who_direct   : 드라이버가 i2c_read_regs 를 직접 부름 (버스 고정 드라이버)
 *   who_indirect : bus_ops 테이블을 거쳐 부름 (이 레슨의 구조)
 *   who_known    : ops 테이블이 컴파일 시점에 보이는 경우 — 컴파일러가 간접 호출을 직접 호출로 바꾸는가?
 */
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int (*read_regs)(void *ctx, uint8_t reg, uint8_t *buf, size_t n);
    int (*write_regs)(void *ctx, uint8_t reg, const uint8_t *buf, size_t n);
} bus_ops;
typedef struct { const bus_ops *ops; void *ctx; } bus_t;

int i2c_read_regs(void *ctx, uint8_t reg, uint8_t *buf, size_t n);   /* 다른 파일에 있다고 가정 */
int i2c_write_regs(void *ctx, uint8_t reg, const uint8_t *buf, size_t n);

int who_direct(void *ctx) {
    uint8_t id = 0;
    int rc = i2c_read_regs(ctx, 0x0F, &id, 1);
    return rc ? rc : id;
}

int who_indirect(const bus_t *b) {
    uint8_t id = 0;
    int rc = b->ops->read_regs(b->ctx, 0x0F, &id, 1);
    return rc ? rc : id;
}

static const bus_ops I2C_OPS = { i2c_read_regs, i2c_write_regs };
int who_known(void *ctx) {
    bus_t b = { &I2C_OPS, ctx };
    return who_indirect(&b);
}

#if !defined(__arm__)
/* 호스트(맥)에서 빌드·실행할 때만: 가짜 i2c_read_regs 와 main. Cortex-M 타깃 asm 에는 들어가지 않는다.
 * 호스트: cc -std=c11 -Wall -Wextra -O2 demo_indirect.c -o demo_indirect && ./demo_indirect */
#include <stdio.h>
int i2c_read_regs(void *ctx, uint8_t reg, uint8_t *buf, size_t n) {
    (void)ctx;
    if (reg == 0x0F && n == 1) { buf[0] = 0xB5; return 0; }
    return -3;
}
int i2c_write_regs(void *ctx, uint8_t reg, const uint8_t *buf, size_t n) { (void)ctx; (void)reg; (void)buf; (void)n; return 0; }
int main(void) {
    bus_t b = { &I2C_OPS, NULL };
    printf("who_direct=0x%02X who_indirect=0x%02X who_known=0x%02X (all three must be 0xB5)\n",
           (unsigned)who_direct(NULL), (unsigned)who_indirect(&b), (unsigned)who_known(NULL));
    return 0;
}
#endif
