/* TOOL-B14 추가 연습 — 본문 풀이(Part A~C 의 헤더 읽기)를 그대로 가져와 그 위에 연습 1~5 를 얹는다.
 *
 * 빌드: export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
 *       cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/TOOL-B14_p && /tmp/TOOL-B14_p
 * 기대 문자열·값 = Lima VM readelf -h (cross_readelf_h.txt), Mac otool -hv (lesson md) 실제 출력.
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
#define EF_RISCV_RVE          0x0008u   /* 연습 4·5 */
#define EF_RISCV_TSO          0x0010u   /* 연습 4 */

enum { TI_ERR_SHORT = -1, TI_ERR_MAGIC = -2, TI_ERR_MACHO = -3, TI_ERR_CLASS = -4, TI_ERR_DATA = -5 };
enum float_abi { FABI_NONE, FABI_UNKNOWN, FABI_SOFT, FABI_HARD, FABI_SINGLE, FABI_DOUBLE, FABI_QUAD };

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

/* ===================== Part C — e_flags 해석 ===================== */
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


/* ===================== 연습 1 — 엔트리 주소와 Thumb 비트 ===================== */
static uint64_t rd64(const uint8_t *p, int big) {
    uint64_t hi = rd32(p + (big ? 0 : 4), big), lo = rd32(p + (big ? 4 : 0), big);
    return hi << 32 | lo;
}
/* e_entry 를 읽고, ARM 이면 비트 0(Thumb 표시)을 떼어 낸다. 성공 0, 실패는 elf_target_info 의 오류 */
static int elf_entry(const uint8_t *buf, size_t n, uint64_t *addr, int *thumb) {
    struct target_info t;
    int rc = elf_target_info(buf, n, &t);
    if (rc != 0) return rc;
    uint64_t e = t.bits == 64 ? rd64(buf + 24, t.big_endian) : rd32(buf + 24, t.big_endian);
    *thumb = t.machine == EM_ARM && (e & 1u);
    *addr  = *thumb ? e & ~(uint64_t)1 : e;
    return 0;
}

/* ===================== 연습 2 — Mach-O 라면 어떤 CPU 인가 ===================== */
#define CPU_ARCH_ABI64   0x01000000u
#define CPU_TYPE_X86_64  (7u | CPU_ARCH_ABI64)    /* 0x01000007 */
#define CPU_TYPE_ARM64   (12u | CPU_ARCH_ABI64)   /* 0x0100000C */
#define MH_OBJECT        1u
static const char *macho_cpu(const uint8_t *buf, size_t n, uint32_t *filetype) {
    if (buf == NULL || n < 16 || rd32(buf, 0) != 0xFEEDFACFu) return NULL;   /* 64-bit, 리틀 엔디언만 */
    uint32_t cpu = rd32(buf + 4, 0);
    *filetype = rd32(buf + 12, 0);
    return cpu == CPU_TYPE_ARM64 ? "arm64" : cpu == CPU_TYPE_X86_64 ? "x86_64" : "other";
}

/* ===================== 연습 3 — readelf -h 의 Flags 줄을 그대로 재현 ===================== */
static void readelf_flags(const struct target_info *t, char *out, size_t sz) {
    int k = snprintf(out, sz, "0x%x", (unsigned)t->flags);       /* "%#x" 는 0 일 때 "0" → 안 됨 */
    if (t->flags == 0 || k < 0) return;                           /* readelf: 0 이면 해석 안 함 */
#define ADD(s) do { size_t L = strlen(out); snprintf(out + L, sz - L, "%s", (s)); } while (0)
    if (t->machine == EM_ARM) {
        if (t->arm_eabi == 5) ADD(", Version5 EABI");
        if (t->flags & EF_ARM_ABI_FLOAT_HARD) ADD(", hard-float ABI");
        if (t->flags & EF_ARM_ABI_FLOAT_SOFT) ADD(", soft-float ABI");
    } else if (t->machine == EM_RISCV) {
        static const char *const fl[4] = {", soft-float ABI", ", single-float ABI", ", double-float ABI", ", quad-float ABI"};
        if (t->flags & EF_RISCV_RVC) ADD(", RVC");
        if (t->flags & EF_RISCV_RVE) ADD(", RVE");
        if (t->flags & EF_RISCV_TSO) ADD(", TSO");
        ADD(fl[(t->flags & EF_RISCV_FLOAT_ABI) >> 1]);
    }
#undef ADD
}

/* ===================== 연습 4 — 헤더에서 -mabi 되짚기 (RISC-V) ===================== */
static const char *riscv_mabi(const struct target_info *t) {
    if (t->machine != EM_RISCV) return NULL;
    if (t->flags & EF_RISCV_RVE) return t->bits == 64 ? "lp64e" : "ilp32e";
    static const char *const m32[4] = {"ilp32", "ilp32f", "ilp32d", "ilp32q"};
    static const char *const m64[4] = {"lp64", "lp64f", "lp64d", "lp64q"};
    unsigned i = (t->flags & EF_RISCV_FLOAT_ABI) >> 1;
    return t->bits == 64 ? m64[i] : m32[i];
}

/* ===================== 연습 5 — 헤더 자기 일관성 검사 ===================== */
/* EI_VERSION == 1, e_version == 1, e_ehsize == 52/64 (클래스와 일치). 0 = OK, -1 = 깨짐, 그 외 = 파싱 오류 */
static int elf_sanity(const uint8_t *buf, size_t n) {
    struct target_info t;
    int rc = elf_target_info(buf, n, &t);
    if (rc != 0) return rc;
    uint16_t ehsize = rd16(buf + (t.bits == 64 ? 52 : 40), t.big_endian);
    if (buf[EI_VERSION] != 1 || rd32(buf + 20, t.big_endian) != 1u) return -1;
    return ehsize == (t.bits == 64 ? 64 : 52) ? 0 : -1;
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
/* m4_hard.o — readelf: Flags 0x5000000, Version5 EABI */
static const uint8_t H_m4_hard_o[64] = {
    0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x28, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xd8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x0a, 0x00, 0x01, 0x00, 0x01, 0x88, 0x90, 0xed, 0x01, 0x1a, 0x00, 0xee, 0x10, 0x1a, 0xb8, 0xee,
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
/* rv32ec.o — readelf: Flags 0x9, RVC, RVE, soft-float ABI */
static const uint8_t H_rv32ec_o[64] = {
    0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0xf3, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x1c, 0x02, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x09, 0x00, 0x01, 0x00, 0x61, 0x11, 0x06, 0xc2, 0x22, 0xc0, 0x2a, 0x84, 0x03, 0x55, 0x05, 0x00,
};
/* rv32imac_tso.o — readelf: Flags 0x11, RVC, TSO, soft-float ABI */
static const uint8_t H_rv32imac_tso_o[64] = {
    0x7f, 0x45, 0x4c, 0x46, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0xf3, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x48, 0x02, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x34, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x09, 0x00, 0x01, 0x00, 0x41, 0x11, 0x06, 0xc6, 0x22, 0xc4, 0x2a, 0x84, 0x03, 0x55, 0x05, 0x00,
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

static int flags_str_is(const uint8_t *h, size_t n, const char *want) {
    struct target_info t; char s[96];
    if (elf_target_info(h, n, &t) != 0) return 0;
    readelf_flags(&t, s, sizeof s);
    if (strcmp(s, want) != 0) { printf("  got \"%s\" want \"%s\"\n", s, want); return 0; }
    return 1;
}
#define FLAGS_IS(arr, s) flags_str_is((arr), sizeof (arr), (s))

int main(void) {
    struct target_info t;
    uint64_t addr = 1; int thumb = -1; uint32_t ft = 0;
    uint8_t tmp[64];

    /* 연습 1: readelf "Entry point address: 0x80b1" */
    CHECK(elf_entry(H_m4_hard_elf, sizeof H_m4_hard_elf, &addr, &thumb) == 0 && addr == 0x80b0u && thumb == 1);
    CHECK(elf_entry(H_m4_soft_elf, sizeof H_m4_soft_elf, &addr, &thumb) == 0 && addr == 0x80b0u && thumb == 1);
    CHECK(elf_entry(H_aarch64_linux_o, sizeof H_aarch64_linux_o, &addr, &thumb) == 0 && addr == 0 && thumb == 0);
    CHECK(elf_entry(H_rv64gc_o, sizeof H_rv64gc_o, &addr, &thumb) == 0 && addr == 0 && thumb == 0);
    CHECK(elf_entry(H_arm64_macho_o, sizeof H_arm64_macho_o, &addr, &thumb) == TI_ERR_MACHO);

    /* 연습 2: otool -hv → "MH_MAGIC_64 ARM64 ... OBJECT", "MH_MAGIC_64 X86_64 ... OBJECT" */
    CHECK(macho_cpu(H_arm64_macho_o, sizeof H_arm64_macho_o, &ft) && strcmp(macho_cpu(H_arm64_macho_o, 32, &ft), "arm64") == 0 && ft == MH_OBJECT);
    CHECK(macho_cpu(H_x86_64_macho_o, 32, &ft) && strcmp(macho_cpu(H_x86_64_macho_o, 32, &ft), "x86_64") == 0 && ft == MH_OBJECT);
    CHECK(macho_cpu(H_m4_hard_o, sizeof H_m4_hard_o, &ft) == NULL);
    CHECK(macho_cpu(H_arm64_macho_o, 15, &ft) == NULL);

    /* 연습 3: readelf -h 의 Flags 문자열과 바이트 단위로 같게 */
    CHECK(FLAGS_IS(H_aarch64_linux_o, "0x0"));
    CHECK(FLAGS_IS(H_rv32ima_o, "0x0"));
    CHECK(FLAGS_IS(H_m4_hard_o, "0x5000000, Version5 EABI"));
    CHECK(FLAGS_IS(H_m4_hard_elf, "0x5000400, Version5 EABI, hard-float ABI"));
    CHECK(FLAGS_IS(H_m4_soft_elf, "0x5000200, Version5 EABI, soft-float ABI"));
    CHECK(FLAGS_IS(H_rv32imac_o, "0x1, RVC, soft-float ABI"));
    CHECK(FLAGS_IS(H_rv32imafc_o, "0x3, RVC, single-float ABI"));
    CHECK(FLAGS_IS(H_rv64gc_o, "0x5, RVC, double-float ABI"));
    CHECK(FLAGS_IS(H_rv32ec_o, "0x9, RVC, RVE, soft-float ABI"));
    CHECK(FLAGS_IS(H_rv32imac_tso_o, "0x11, RVC, TSO, soft-float ABI"));

    /* 연습 4: 컴파일할 때 준 -mabi 를 헤더에서 되짚기 (demo_cross.sh 의 명령과 대조) */
    CHECK(INFO(H_rv32imac_o, &t) == 0 && strcmp(riscv_mabi(&t), "ilp32") == 0);
    CHECK(INFO(H_rv32imafc_o, &t) == 0 && strcmp(riscv_mabi(&t), "ilp32f") == 0);
    CHECK(INFO(H_rv64gc_o, &t) == 0 && strcmp(riscv_mabi(&t), "lp64d") == 0);
    CHECK(INFO(H_rv32ec_o, &t) == 0 && strcmp(riscv_mabi(&t), "ilp32e") == 0);
    CHECK(INFO(H_m4_hard_o, &t) == 0 && riscv_mabi(&t) == NULL);

    /* 연습 5: 헤더 자기 일관성 */
    CHECK(elf_sanity(H_m4_hard_o, sizeof H_m4_hard_o) == 0);
    CHECK(elf_sanity(H_rv64gc_o, sizeof H_rv64gc_o) == 0);
    CHECK(elf_sanity(H_aarch64_be_o, sizeof H_aarch64_be_o) == 0);     /* 큰 엔디언도 ehsize 64 */
    memcpy(tmp, H_m4_hard_o, 64); tmp[40] = 64;                          /* ELF32 인데 ehsize 64 */
    CHECK(elf_sanity(tmp, 64) == -1);
    memcpy(tmp, H_rv64gc_o, 64); tmp[20] = 2;                            /* e_version 2 */
    CHECK(elf_sanity(tmp, 64) == -1);
    memcpy(tmp, H_rv64gc_o, 64); tmp[EI_VERSION] = 0;
    CHECK(elf_sanity(tmp, 64) == -1);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
