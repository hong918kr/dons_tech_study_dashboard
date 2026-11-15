/* ARCH-M15 demo — solution.c 의 모델로 계산기 표, RTOS 구성의 RBAR/RASR, 접근 로그를 찍는다 (체크 없음)
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_region_map.c -o demo && ./demo
 */
#define main lesson_main                 /* solution.c 의 테스트 main 은 이름만 바꿔 둔다 */
#include "solution.c"
#undef main

static const char *const why_name[] = { "OK_REGION", "OK_DEFAULT", "OK_MPU_OFF", "OK_PPB", "F_NOMATCH",
                                        "F_AP", "F_XN", "F_DEFAULT_XN", "F_PPB_UNPRIV" };
static const char *const ap_name[8] = { "--/--", "RW/--", "RW/RO", "RW/RW", "rsvd", "RO/--", "RO/RO", "RO/RO" };

static void size_str(uint64_t b, char *s) {
    if (b >= (1u << 30)) sprintf(s, "%lluG", (unsigned long long)(b >> 30));
    else if (b >= (1u << 20)) sprintf(s, "%lluM", (unsigned long long)(b >> 20));
    else if (b >= 1024) sprintf(s, "%lluK", (unsigned long long)(b >> 10));
    else sprintf(s, "%lluB", (unsigned long long)b);
}

static void calc_row(uint32_t start, uint32_t len, const char *note) {
    region_t r; uint32_t over; char s1[16], s2[16];
    if (mpu_calc(start, len, &r, &over) != 0) { printf("  0x%08X +%-8u → error\n", (unsigned)start, (unsigned)len); return; }
    size_str(region_bytes(r.size_f), s1);
    size_str(region_bytes(r.size_f) >= 256 ? region_bytes(r.size_f) / 8 : 0, s2);
    printf("  0x%08X +0x%-6X → base 0x%08X SIZE=%2u (%4s) sub %4s SRD=0x%02X  over=0x%X  %s\n",
           (unsigned)start, (unsigned)len, (unsigned)r.base, r.size_f, s1, region_bytes(r.size_f) >= 256 ? s2 : "-",
           r.srd, (unsigned)over, note);
}

static void acc_row(const mpu_t *m, const char *what, uint32_t a, unsigned size, acc_t acc, bool priv, bool hf) {
    static const char accc[3] = { 'R', 'W', 'X' };
    mm_report_t rp = mpu_access(m, a, size, acc, priv, hf);
    printf("  %s%s %c%u @0x%08X → %-12s r%-2d", priv ? "priv" : "user", hf ? "+HF" : "   ",
           accc[acc], size, (unsigned)a, why_name[rp.why], rp.region);
    if (!rp.allowed) {
        if (!rp.memmanage) printf("  [BusFault]");
        else printf("  [MemManage %s%s]", rp.iaccviol ? "IACCVIOL" : "DACCVIOL",
                    rp.mmarvalid ? "" : " MMFAR 무효");
        if (rp.mmarvalid) printf(" MMFAR=0x%08X", (unsigned)rp.mmfar);
    }
    printf("  ; %s\n", what);
}

int main(void) {
    printf("영역 계산기 (mpu_calc)\n");
    calc_row(0x20000000u, 0x6000, "24 KB, 맨 앞 정렬");
    calc_row(0x20001000u, 0x6000, "24 KB, 가운데");
    calc_row(0x20007000u, 0x2000, "8 KB 가 32 KB 경계를 걸침");
    calc_row(0x20000020u, 64, "128 B 대신 256 B + 서브리전");
    calc_row(0x20004000u, 32, "스택 가드");
    calc_row(0x20001234u, 1000, "정렬 안 된 1000 B");
    calc_row(0x7FFFFFF0u, 0x20, "2 GB 경계를 걸친 32 B");

    region_t want[MPU_NREG];
    uint32_t over, rbar, rasr;
    memset(want, 0, sizeof want);
    want[0] = mk(0x08000000u, 18, 6, false, 0); want[0].texscb = 0x02;
    want[1] = mk(0x20000000u, 16, 1, true, 0);  want[1].texscb = 0x06;
    want[2] = mk(0x40000000u, 28, 1, true, 0);  want[2].texscb = 0x01;
    mpu_calc(0x20004000u, 0x1800, &want[3], &over); want[3].ap = 3; want[3].xn = true; want[3].texscb = 0x06;
    want[4] = mk(0x20008000u, 9, 2, true, 0);
    want[5] = mk(0x40004400u, 9, 3, true, 0);   want[5].texscb = 0x01;
    mpu_calc(0x20004000u, 32, &want[7], &over); want[7].ap = 0; want[7].xn = true;

    static const char *const nm[MPU_NREG] = { "Flash", "SRAM", "Periph", "TaskA RAM", "shared tbl", "TaskA UART", "-", "stack guard" };
    mpu_t m = mpu_new();
    printf("\nRTOS 구성 (영역 번호가 클수록 우선)\n  #  %-11s %-10s %-5s %-5s %-2s SRD   RBAR       RASR\n", "name", "base", "size", "AP", "XN");
    for (unsigned i = 0; i < MPU_NREG; i++) {
        char s1[16];
        mpu_encode(&want[i], i, &rbar, &rasr);
        m.r[i] = mpu_decode(rbar, rasr);
        size_str(region_bytes(want[i].size_f), s1);
        if (!want[i].en) { printf("  %u  %-11s (disabled)  RBAR 0x%08X RASR 0x%08X\n", i, nm[i], (unsigned)rbar, (unsigned)rasr); continue; }
        printf("  %u  %-11s 0x%08X %-5s %-5s %-2s 0x%02X  0x%08X 0x%08X\n", i, nm[i], (unsigned)want[i].base, s1,
               ap_name[want[i].ap], want[i].xn ? "XN" : "", want[i].srd, (unsigned)rbar, (unsigned)rasr);
    }

    printf("\n접근 로그 (PRIVDEFENA=1, HFNMIENA=0)\n");
    acc_row(&m, "태스크가 Flash 코드 실행", 0x08001234u, 2, ACC_X, false, false);
    acc_row(&m, "태스크가 Flash 에 쓰기", 0x08001234u, 4, ACC_W, false, false);
    acc_row(&m, "태스크 스택 push", 0x20004100u, 4, ACC_W, false, false);
    acc_row(&m, "스택 넘침 → 가드", 0x20004010u, 4, ACC_W, false, false);
    acc_row(&m, "ISR 이 가드에 쓰기", 0x20004010u, 4, ACC_W, true, false);
    acc_row(&m, "STRD 뒤 절반이 서브리전 구멍", 0x200057FCu, 8, ACC_W, false, false);
    acc_row(&m, "큰 프레임이 가드 건너뜀", 0x20003FF0u, 4, ACC_W, true, false);
    acc_row(&m, "SRAM 에서 코드 실행", 0x20004100u, 2, ACC_X, false, false);
    acc_row(&m, "태스크 UART 창", 0x40004404u, 4, ACC_W, false, false);
    acc_row(&m, "창 밖 주변장치", 0x40004800u, 4, ACC_W, false, false);
    acc_row(&m, "영역 없는 주소 (특권)", 0x60000000u, 4, ACC_R, true, false);
    acc_row(&m, "영역 없는 주소 (비특권)", 0x60000000u, 4, ACC_R, false, false);
    acc_row(&m, "HardFault 핸들러가 가드 덤프", 0x20004010u, 4, ACC_R, true, true);
    acc_row(&m, "태스크가 SCB->CFSR 읽기", 0xE000ED28u, 4, ACC_R, false, false);
    return 0;
}
