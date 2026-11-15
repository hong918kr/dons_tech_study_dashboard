/* main.c — first translation unit: uses a macro (LOG), a header API and a libc call */
#include <stdio.h>
#include "util.h"

static const uint8_t boot_block[8] = { 0xEB, 0x3C, 0x90, 'F', 'W', '2', '7', 0x00 };

int main(void)
{
    LOG("boot");
    printf("fw %s: 1500 bytes -> %u sectors, csum=0x%02x\n",
           fw_version, (unsigned)sectors_for(1500u),
           (unsigned)checksum8(boot_block, sizeof boot_block));
    return 0;
}
