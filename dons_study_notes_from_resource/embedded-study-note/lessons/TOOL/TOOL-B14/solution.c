/* TOOL-B14 맥에서 크로스 컴파일 첫걸음 — ELF 헤더만 읽고 "어느 타깃용 오브젝트인가" 판정
 *
 * 빌드: export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
 *       cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/TOOL-B14_s && /tmp/TOOL-B14_s
 * 테스트 바이트 = demo_cross.sh 가 만든 실제 오브젝트의 첫 64 바이트 (cross_hdr64.txt),
 * 기대값       = Lima VM 의 readelf -h 출력 (cross_readelf_h.txt) 과 실제 링커 결과 (cross_link.txt).
 * 범위: ELF 헤더(52/64 B)만. 섹션·심볼·프로그램 헤더 파싱은 TOOL-A11.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- ELF 상수 (System V gABI + ARM / RISC-V psABI) ---- */
enum { EI_CLASS = 4, EI_DATA = 5, EI_VERSION = 6, EI_OSABI = 7, EI_NIDENT = 16 };
enum { ELFCLASS32 = 1, ELFCLASS64 = 2, ELFDATA2LSB = 1, ELFDATA2MSB = 2 };
enum { ET_REL = 1, ET_EXEC = 2, ET_DYN = 3, ET_CORE = 4 };
enum { EM_ARM = 40, EM_X86_64 = 62, EM_AARCH64 = 183, EM_RISCV = 243 };
#define EF_ARM_EABIMASK       0xFF000000u
#define EF_ARM_ABI_FLOAT_SOFT 0x00000200u
#define EF_ARM_ABI_FLOAT_HARD 0x00000400u
#define EF_RISCV_RVC          0x0001u
#define EF_RISCV_FLOAT_ABI    0x0006u   /* 0 soft / 2 single / 4 double / 6 quad */

enum { TI_ERR_SHORT = -1, TI_ERR_MAGIC = -2, TI_ERR_MACHO = -3, TI_ERR_CLASS = -4, TI_ERR_DATA = -5 };
enum float_abi { FABI_NONE, FABI_UNKNOWN, FABI_SOFT, FABI_HARD, FABI_SINGLE, FABI_DOUBLE, FABI_QUAD };
enum link_verdict { LINK_OK, LINK_ERR_MACHINE, LINK_ERR_CLASS, LINK_ERR_ENDIAN, LINK_ERR_FLOAT, LINK_UNKNOWN };

struct target_info {
    int            bits;         /* 32 / 64           (e_ident[EI_CLASS]) */
    int            big_endian;   /* 1 = MSB           (e_ident[EI_DATA])  */
    uint8_t        osabi;        /* 0 = System V      (e_ident[EI_OSABI]) */
    uint16_t       type;         /* e_type   : ET_REL / ET_EXEC / ...      */
    uint16_t       machine;      /* e_machine: EM_ARM / EM_RISCV / ...     */
    uint32_t       flags;        /* e_flags  : 아키텍처별 의미             */
    const char    *machine_name; /* "ARM" "AArch64" "RISC-V" "X86-64" "other" */
    int            arm_eabi;     /* ARM: EABI 버전 = e_flags >> 24, 그 외 0 */
    int            rvc;          /* RISC-V: C(압축) 확장 사용 1, 그 외 0   */
    enum float_abi fabi;         /* 헤더가 말해 주는 float ABI             */
};

/* ===================== Part A — 엔디언을 지키며 읽기 + 필드 위치 ===================== */
static uint16_t rd16(const uint8_t *p, int big) {
    return big ? (uint16_t)(p[0] << 8 | p[1])
               : (uint16_t)(p[1] << 8 | p[0]);
}
static uint32_t rd32(const uint8_t *p, int big) {
    return big ? (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]
               : (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}
static const char *machine_name(uint16_t m) {
    switch (m) {
    case EM_ARM:     return "ARM";
    case EM_X86_64:  return "X86-64";
    case EM_AARCH64: return "AArch64";
    case EM_RISCV:   return "RISC-V";
    default:         return "other";
    }
}

/* ===================== Part B — 입력 검증: 헤더 크기(52/64) 또는 음수 오류 ===================== */
static int elf_check_ident(const uint8_t *buf, size_t n) {
    static const uint8_t macho64[4] = {0xcf, 0xfa, 0xed, 0xfe}, macho32[4] = {0xce, 0xfa, 0xed, 0xfe};
    if (buf == NULL || n < 4) return TI_ERR_SHORT;
    if (memcmp(buf, macho64, 4) == 0 || memcmp(buf, macho32, 4) == 0) return TI_ERR_MACHO;
    if (buf[0] != 0x7f || buf[1] != 'E' || buf[2] != 'L' || buf[3] != 'F') return TI_ERR_MAGIC;
    if (n < EI_NIDENT) return TI_ERR_SHORT;
    if (buf[EI_CLASS] != ELFCLASS32 && buf[EI_CLASS] != ELFCLASS64) return TI_ERR_CLASS;
    if (buf[EI_DATA] != ELFDATA2LSB && buf[EI_DATA] != ELFDATA2MSB) return TI_ERR_DATA;
    int hsz = buf[EI_CLASS] == ELFCLASS64 ? 64 : 52;
    return n < (size_t)hsz ? TI_ERR_SHORT : hsz;
}

/* ===================== Part C — e_flags 해석 + 링크 호환성 + CI 게이트 ===================== */
static void decode_flags(struct target_info *t) {
    t->arm_eabi = 0; t->rvc = 0; t->fabi = FABI_NONE;
    if (t->machine == EM_ARM) {
        t->arm_eabi = (int)((t->flags & EF_ARM_EABIMASK) >> 24);
        if (t->flags & EF_ARM_ABI_FLOAT_HARD)      t->fabi = FABI_HARD;
        else if (t->flags & EF_ARM_ABI_FLOAT_SOFT) t->fabi = FABI_SOFT;
        else                                       t->fabi = FABI_UNKNOWN;   /* .o: .ARM.attributes 를 봐야 함 */
    } else if (t->machine == EM_RISCV) {
        static const enum float_abi rv[4] = {FABI_SOFT, FABI_SINGLE, FABI_DOUBLE, FABI_QUAD};
        t->rvc  = (t->flags & EF_RISCV_RVC) != 0;
        t->fabi = rv[(t->flags & EF_RISCV_FLOAT_ABI) >> 1];
    }
}
static enum link_verdict link_check(const struct target_info *a, const struct target_info *b) {
    if (a->machine != b->machine)       return LINK_ERR_MACHINE;
    if (a->bits != b->bits)             return LINK_ERR_CLASS;
    if (a->big_endian != b->big_endian) return LINK_ERR_ENDIAN;
    if (a->fabi == FABI_UNKNOWN || b->fabi == FABI_UNKNOWN) return LINK_UNKNOWN;
    if (a->fabi != b->fabi)             return LINK_ERR_FLOAT;
    return LINK_OK;                     /* RVC 차이는 OK: 링커가 플래그를 OR 한다 */
}

/* ----- 주어진 코드 (TODO 아님): Part A/B/C 함수를 엮는 공개 API ----- */
int elf_target_info(const uint8_t *buf, size_t n, struct target_info *out) {
    memset(out, 0, sizeof *out);            /* 실패해도 out 은 깨끗한 0 상태 */
    int hsz = elf_check_ident(buf, n);
    if (hsz < 0) return hsz;
    out->bits       = hsz == 64 ? 64 : 32;
    out->big_endian = buf[EI_DATA] == ELFDATA2MSB;
    out->osabi      = buf[EI_OSABI];
    out->type       = rd16(buf + 16, out->big_endian);
    out->machine    = rd16(buf + 18, out->big_endian);
    out->flags      = rd32(buf + (out->bits == 64 ? 48 : 36), out->big_endian);  /* e_entry/phoff/shoff 폭 차이 */
    out->machine_name = machine_name(out->machine);
    decode_flags(out);
    return 0;
}

struct blob { const char *name; const uint8_t *p; size_t n; };

/* CI 게이트: 이미지(want)와 같이 링크될 오브젝트들을 헤더로 검사.
 * 반환 = 확실히 깨지는 개수 (파싱 실패 포함), *unknown = 헤더만으로는 판정 못 하는 개수 */
static int ci_gate(const struct blob *objs, int n, const struct target_info *want, int *unknown) {
    int bad = 0;
    *unknown = 0;
    for (int i = 0; i < n; i++) {
        struct target_info t;
        if (elf_target_info(objs[i].p, objs[i].n, &t) != 0) { bad++; continue; }
        enum link_verdict v = link_check(want, &t);
        if (v == LINK_UNKNOWN) (*unknown)++;
        else if (v != LINK_OK) bad++;
    }
    return bad;
}

/* ===================== 실제 오브젝트 헤더 (cross_hdr64.txt) ===================== */
/* aarch64_linux.o — readelf: Flags 0x0 */
static const uint8_t H_aarch64_linux_o[64] = {
    0x7f, 0x45, 0x4c, 0x46, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0xb7, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x07, 0x00, 0x01, 0x00,
};
/* aarch64_be.o — readelf: Flags 0x0 */
static const uint8_t H_aarch64_be_o[64] = {
    0x7f, 0x45, 0x4c, 0x46, 0x02, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0xb7, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0xa8,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x07, 0x00, 0x01,
};
/* x86_64_linux.o — readelf: Flags 0x0 */
static const uint8_t H_x86_64_linux_o[64] = {
    0x7f, 0x45, 0x4c, 0x46, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x3e, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x58, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x07, 0x00, 0x01, 0x00,
};
/* m4_hard.o — readelf: Flags 0x5000000, Version5 EABI */
static const uint8_t H_m4_hard_o[64] = {
    0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x28, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xd8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x0a, 0x00, 0x01, 0x00, 0x01, 0x88, 0x90, 0xed, 0x01, 0x1a, 0x00, 0xee, 0x10, 0x1a, 0xb8, 0xee,
};
/* m4_soft.o — readelf: Flags 0x5000000, Version5 EABI */
static const uint8_t H_m4_soft_o[64] = {
    0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x28, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x24, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x0b, 0x00, 0x01, 0x00, 0x10, 0xb5, 0x04, 0x46, 0x00, 0x88, 0xff, 0xf7, 0xfe, 0xff, 0x61, 0x68,
};
/* gcc_m4_hard.o — readelf: Flags 0x5000000, Version5 EABI */
static const uint8_t H_gcc_m4_hard_o[64] = {
    0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x28, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xc0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x09, 0x00, 0x08, 0x00, 0x03, 0x88, 0xd0, 0xed, 0x01, 0x7a, 0x00, 0xee, 0x10, 0x3a, 0xb8, 0xee,
};
/* m4_hard.elf — readelf: Flags 0x5000400, Version5 EABI, hard-float ABI */
static const uint8_t H_m4_hard_elf[64] = {
    0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x28, 0x00, 0x01, 0x00, 0x00, 0x00, 0xb1, 0x80, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00,
    0x7c, 0xb2, 0x01, 0x00, 0x00, 0x04, 0x00, 0x05, 0x34, 0x00, 0x20, 0x00, 0x03, 0x00, 0x28, 0x00,
    0x1b, 0x00, 0x1a, 0x00, 0x01, 0x00, 0x00, 0x70, 0x10, 0x8d, 0x00, 0x00, 0x10, 0x8d, 0x00, 0x00,
};
/* m4_soft.elf — readelf: Flags 0x5000200, Version5 EABI, soft-float ABI */
static const uint8_t H_m4_soft_elf[64] = {
    0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x28, 0x00, 0x01, 0x00, 0x00, 0x00, 0xb1, 0x80, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00,
    0x94, 0xbd, 0x01, 0x00, 0x00, 0x02, 0x00, 0x05, 0x34, 0x00, 0x20, 0x00, 0x03, 0x00, 0x28, 0x00,
    0x1b, 0x00, 0x1a, 0x00, 0x01, 0x00, 0x00, 0x70, 0x80, 0x90, 0x00, 0x00, 0x80, 0x90, 0x00, 0x00,
};
/* rv32imac.o — readelf: Flags 0x1, RVC, soft-float ABI */
static const uint8_t H_rv32imac_o[64] = {
    0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0xf3, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x02, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x09, 0x00, 0x01, 0x00, 0x41, 0x11, 0x06, 0xc6, 0x22, 0xc4, 0x2a, 0x84, 0x03, 0x55, 0x05, 0x00,
};
/* rv32imafc.o — readelf: Flags 0x3, RVC, single-float ABI */
static const uint8_t H_rv32imafc_o[64] = {
    0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0xf3, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xd0, 0x01, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x08, 0x00, 0x01, 0x00, 0x83, 0x55, 0x05, 0x00, 0x5c, 0x61, 0x53, 0xf7, 0x15, 0xd0, 0x53, 0xf5,
};
/* rv32ima.o — readelf: Flags 0x0 */
static const uint8_t H_rv32ima_o[64] = {
    0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0xf3, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x60, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x09, 0x00, 0x01, 0x00, 0x13, 0x01, 0x01, 0xff, 0x23, 0x26, 0x11, 0x00, 0x23, 0x24, 0x81, 0x00,
};
/* rv64gc.o — readelf: Flags 0x5, RVC, double-float ABI */
static const uint8_t H_rv64gc_o[64] = {
    0x7f, 0x45, 0x4c, 0x46, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0xf3, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x05, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x08, 0x00, 0x01, 0x00,
};
/* arm64_macho.o — Mach-O, 첫 32 바이트 */
static const uint8_t H_arm64_macho_o[32] = {
    0xcf, 0xfa, 0xed, 0xfe, 0x0c, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x18, 0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
/* x86_64_macho.o — Mach-O, 첫 32 바이트 */
static const uint8_t H_x86_64_macho_o[32] = {
    0xcf, 0xfa, 0xed, 0xfe, 0x07, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x18, 0x01, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* ===================== 테스트 하네스 ===================== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define INFO(arr, ti) elf_target_info((arr), sizeof (arr), (ti))

static int pair(const uint8_t *a, size_t na, const uint8_t *b, size_t nb) {
    struct target_info x, y;
    if (elf_target_info(a, na, &x) != 0 || elf_target_info(b, nb, &y) != 0) return -1;
    return (int)link_check(&x, &y);
}
#define PAIR(a, b) pair((a), sizeof (a), (b), sizeof (b))

int main(void) {
    struct target_info t;
    uint8_t tmp[64];

    /* ---- Part A: 엔디언 읽기, 이름표, 실제 헤더의 기본 필드 ---- */
    CHECK(rd16((const uint8_t[]){0x34, 0x12}, 0) == 0x1234 && rd16((const uint8_t[]){0x34, 0x12}, 1) == 0x3412);
    CHECK(rd32((const uint8_t[]){0x00, 0x00, 0x00, 0x05}, 0) == 0x05000000u);
    CHECK(rd32((const uint8_t[]){0x00, 0x00, 0x00, 0x05}, 1) == 0x5u);
    CHECK(strcmp(machine_name(40), "ARM") == 0 && strcmp(machine_name(183), "AArch64") == 0);
    CHECK(strcmp(machine_name(243), "RISC-V") == 0 && strcmp(machine_name(62), "X86-64") == 0);
    CHECK(strcmp(machine_name(3), "other") == 0);                       /* EM_386 은 표에 없음 */
    CHECK(INFO(H_aarch64_linux_o, &t) == 0 && t.bits == 64 && !t.big_endian && t.type == ET_REL);
    CHECK(t.machine == EM_AARCH64 && t.machine_name && strcmp(t.machine_name, "AArch64") == 0 && t.flags == 0 && t.osabi == 0);
    CHECK(INFO(H_x86_64_linux_o, &t) == 0 && t.machine == EM_X86_64 && t.bits == 64);
    CHECK(INFO(H_m4_hard_o, &t) == 0 && t.bits == 32 && t.machine == EM_ARM && t.flags == 0x05000000u);
    CHECK(INFO(H_rv32imac_o, &t) == 0 && t.bits == 32 && t.machine == EM_RISCV && t.flags == 0x1u);
    CHECK(INFO(H_rv64gc_o, &t) == 0 && t.bits == 64 && t.flags == 0x5u);   /* ELF64: e_flags 는 48 바이트 위치 */
    CHECK(INFO(H_m4_hard_elf, &t) == 0 && t.type == ET_EXEC && t.flags == 0x05000400u);

    /* ---- Part B: 큰 엔디언, 잘린 입력, 엉뚱한 매직, Mach-O ---- */
    CHECK(INFO(H_aarch64_be_o, &t) == 0 && t.big_endian && t.machine == EM_AARCH64 && t.type == ET_REL);
    CHECK(elf_target_info(H_m4_hard_o, 0, &t) == TI_ERR_SHORT);
    CHECK(elf_target_info(H_m4_hard_o, 3, &t) == TI_ERR_SHORT);
    CHECK(elf_target_info(H_m4_hard_o, 8, &t) == TI_ERR_SHORT);         /* 매직은 맞지만 e_ident 16 B 미만 */
    CHECK(elf_target_info(H_rv32imac_o, 51, &t) == TI_ERR_SHORT);       /* ELF32 헤더 = 52 B */
    CHECK(elf_target_info(H_rv32imac_o, 52, &t) == 0 && t.flags == 0x1u);
    CHECK(elf_target_info(H_rv64gc_o, 63, &t) == TI_ERR_SHORT);         /* ELF64 헤더 = 64 B */
    CHECK(elf_target_info(NULL, 64, &t) == TI_ERR_SHORT);
    CHECK(INFO(H_arm64_macho_o, &t) == TI_ERR_MACHO);
    CHECK(INFO(H_x86_64_macho_o, &t) == TI_ERR_MACHO);
    memcpy(tmp, H_m4_hard_o, 64); tmp[1] = 'e';
    CHECK(elf_target_info(tmp, 64, &t) == TI_ERR_MAGIC);
    memcpy(tmp, H_m4_hard_o, 64); tmp[EI_CLASS] = 3;
    CHECK(elf_target_info(tmp, 64, &t) == TI_ERR_CLASS);
    memcpy(tmp, H_m4_hard_o, 64); tmp[EI_DATA] = 0;
    CHECK(elf_target_info(tmp, 64, &t) == TI_ERR_DATA);

    /* ---- Part C: e_flags 해석 (readelf 와 대조) ---- */
    CHECK(INFO(H_m4_hard_o, &t) == 0 && t.arm_eabi == 5 && t.fabi == FABI_UNKNOWN);   /* .o 에는 float 비트 없음 */
    CHECK(INFO(H_gcc_m4_hard_o, &t) == 0 && t.arm_eabi == 5 && t.fabi == FABI_UNKNOWN);
    CHECK(INFO(H_m4_hard_elf, &t) == 0 && t.arm_eabi == 5 && t.fabi == FABI_HARD);
    CHECK(INFO(H_m4_soft_elf, &t) == 0 && t.arm_eabi == 5 && t.fabi == FABI_SOFT);
    CHECK(INFO(H_rv32imac_o, &t) == 0 && t.rvc == 1 && t.fabi == FABI_SOFT);
    CHECK(INFO(H_rv32imafc_o, &t) == 0 && t.rvc == 1 && t.fabi == FABI_SINGLE);
    CHECK(INFO(H_rv32ima_o, &t) == 0 && t.rvc == 0 && t.fabi == FABI_SOFT);
    CHECK(INFO(H_rv64gc_o, &t) == 0 && t.rvc == 1 && t.fabi == FABI_DOUBLE);
    CHECK(INFO(H_aarch64_linux_o, &t) == 0 && t.fabi == FABI_NONE && t.arm_eabi == 0 && t.rvc == 0);

    /* ---- Part C: 링크 호환성 (실제 ld.bfd / ld.lld 결과와 대조, cross_link.txt) ---- */
    CHECK(PAIR(H_rv32imac_o, H_rv32imafc_o) == LINK_ERR_FLOAT);   /* bfd: can't link soft-float modules with single-float */
    CHECK(PAIR(H_rv32ima_o, H_rv32imac_o) == LINK_OK);            /* RVC 차이는 링크됨 (결과 Flags 0x1) */
    CHECK(PAIR(H_gcc_m4_hard_o, H_m4_soft_o) == LINK_UNKNOWN);    /* 헤더로는 모름 — bfd 는 attributes 로 거부 */
    CHECK(PAIR(H_m4_hard_elf, H_m4_soft_elf) == LINK_ERR_FLOAT);
    CHECK(PAIR(H_m4_hard_elf, H_m4_hard_elf) == LINK_OK);
    CHECK(PAIR(H_aarch64_linux_o, H_aarch64_be_o) == LINK_ERR_ENDIAN);
    CHECK(PAIR(H_rv32imac_o, H_rv64gc_o) == LINK_ERR_CLASS);
    CHECK(PAIR(H_m4_hard_o, H_rv32imac_o) == LINK_ERR_MACHINE);
    CHECK(PAIR(H_aarch64_linux_o, H_x86_64_linux_o) == LINK_ERR_MACHINE);

    /* ---- Part C: CI 게이트 — 벤더 라이브러리 오브젝트가 이미지 ABI 와 맞나 ---- */
    {
        struct target_info want; int unk = -1;
        const struct blob rv_libs[] = {
            {"rv32imafc.o", H_rv32imafc_o, sizeof H_rv32imafc_o}, {"rv32imac.o", H_rv32imac_o, sizeof H_rv32imac_o},
            {"rv64gc.o", H_rv64gc_o, sizeof H_rv64gc_o},          {"m4_hard.o", H_m4_hard_o, sizeof H_m4_hard_o},
            {"arm64_macho.o", H_arm64_macho_o, sizeof H_arm64_macho_o},
        };
        CHECK(INFO(H_rv32imafc_o, &want) == 0 && ci_gate(rv_libs, 5, &want, &unk) == 4 && unk == 0);
        const struct blob m4_libs[] = {
            {"m4_hard.o", H_m4_hard_o, sizeof H_m4_hard_o},       {"gcc_m4_hard.o", H_gcc_m4_hard_o, sizeof H_gcc_m4_hard_o},
            {"m4_soft.elf", H_m4_soft_elf, sizeof H_m4_soft_elf}, {"aarch64_linux.o", H_aarch64_linux_o, sizeof H_aarch64_linux_o},
            {"rv32imac.o", H_rv32imac_o, sizeof H_rv32imac_o},
        };
        CHECK(INFO(H_m4_hard_elf, &want) == 0 && ci_gate(m4_libs, 5, &want, &unk) == 3 && unk == 2);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
