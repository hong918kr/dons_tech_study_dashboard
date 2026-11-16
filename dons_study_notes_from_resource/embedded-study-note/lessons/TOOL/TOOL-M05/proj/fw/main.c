/* ENV: cross — arm-none-eabi-gcc only (CMake target build). Never built for the host. */
/* main.c — "firmware": uses the same lib/ as the host tests. */
#include <stdint.h>
#include "dsp.h"

#ifndef BOARD_REV
#define BOARD_REV 1                      /* -DBOARD_REV=n from CMAKE_C_FLAGS / presets */
#endif

const volatile uint32_t fw_board_rev = BOARD_REV; /* .rodata; volatile read keeps it */
static const uint8_t boot_msg[] = "123456789";
volatile uint16_t g_crc;                 /* .bss */
volatile int32_t g_avg;                  /* .bss */
uint32_t g_boot_count = 0x5A5A0001u;     /* .data: initial value lives in Flash */
static mavg_t filt;

int main(void)
{
    g_crc = crc16_ccitt(boot_msg, sizeof boot_msg - 1u);
    mavg_init(&filt);
    for (int32_t i = 0; i < 16; i++)
        g_avg = mavg_push(&filt, i * 10);
    g_boot_count += fw_board_rev;      /* uses the define -> survives --gc-sections */
    for (;;) {
    }
}
