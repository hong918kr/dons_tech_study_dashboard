/* C-A07 X-Macro로 에러코드·레지스터 테이블 생성
 *
 * 목록(list) 매크로 두 개 — FW_ERR_LIST (에러 코드), UART_REG_LIST (레지스터 맵) — 만이 "원본"이다.
 * enum, 이름/설명 표, 값→인덱스 switch, 정적 검사, 덤프·셀프테스트가 전부 이 목록에서 생성된다.
 *
 *   빌드:          cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
 *   항목 추가 실험: 같은 명령에 -DC_A07_EXTRA  → 에러 1개(목록 중간)와 레지스터 1개(끝)가
 *                  추가된 상태로 같은 테스트를 다시 돌린다. 코드는 한 줄도 안 고친다.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 벤더/드라이버 헤더에 흔히 있는 매크로. 목록의 에러 이름 TIMEOUT 과 철자가 같다 (Part B 함정). */
#define TIMEOUT 1000u

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* ======================= 단일 정의 목록 (single source of truth) ======================= */
enum fw_sev { SEV_INFO, SEV_WARN, SEV_ERROR, SEV_FATAL };
enum reg_acc { ACC_RO, ACC_RW, ACC_WO, ACC_RC, ACC_W1C };   /* RC = 읽으면 지워짐(read-clear) */

#ifdef C_A07_EXTRA
#define FW_ERR_EXTRA(X)   X(ECC, -13, SEV_ERROR, "uncorrectable ECC error")
#define UART_REG_EXTRA(X) X(DMACR, 0x20u, 0x00000000u, ACC_RW)
#define N_EXTRA 1u
#else
#define FW_ERR_EXTRA(X)
#define UART_REG_EXTRA(X)
#define N_EXTRA 0u
#endif

/*          이름        값    심각도      설명 (값은 와이어 프로토콜의 int8 필드로 나간다) */
#define FW_ERR_LIST(X)                                        \
    X(OK,          0,  SEV_INFO,  "success")                  \
    X(TIMEOUT,    -1,  SEV_WARN,  "operation timed out")      \
    X(BUSY,       -2,  SEV_WARN,  "resource busy")            \
    X(INVAL,      -3,  SEV_ERROR, "invalid argument")         \
    X(NOMEM,      -4,  SEV_ERROR, "out of memory")            \
    X(CRC,       -10,  SEV_ERROR, "CRC mismatch")             \
    X(NACK,      -11,  SEV_WARN,  "I2C address NACK")         \
    X(OVERRUN,   -12,  SEV_ERROR, "RX FIFO overrun")          \
    FW_ERR_EXTRA(X)                                           \
    X(HW_FAULT, -100,  SEV_FATAL, "hardware fault")

/*            이름     오프셋   리셋값        접근 */
#define UART_REG_LIST(X)                             \
    X(CR1,    0x00u, 0x00000000u, ACC_RW)            \
    X(CR2,    0x04u, 0x00000000u, ACC_RW)            \
    X(BRR,    0x08u, 0x00000000u, ACC_RW)            \
    X(SR,     0x0Cu, 0x000000C0u, ACC_W1C)           \
    X(RDR,    0x10u, 0x00000000u, ACC_RC)            \
    X(TDR,    0x14u, 0x00000000u, ACC_WO)            \
    X(ERRCNT, 0x18u, 0x00000000u, ACC_RC)            \
    X(ID,     0x1Cu, 0x00A70011u, ACC_RO)            \
    UART_REG_EXTRA(X)
#define UART_BLOCK_SIZE 0x400u

/* 예제 1 (주어짐): 에러 값 enum — FW_ERR_OK = (0), FW_ERR_TIMEOUT = (-1), ... */
#define X_ERR_ENUM(name, val, sev, desc) FW_ERR_##name = (val),
enum fw_err { FW_ERR_LIST(X_ERR_ENUM) };

/* 예제 2 (주어짐): 레지스터 오프셋 상수 — UART_CR1_OFS = (0x00u), ... (case 라벨·정적 검사에 쓰는 상수식) */
#define X_REG_OFS(name, ofs, rst, acc) UART_##name##_OFS = (ofs),
enum uart_ofs { UART_REG_LIST(X_REG_OFS) };
/* 타깃용 접근자: UART_REG(USART2_BASE, SR) = ...  (C-M01 의 volatile MMIO) */
#define UART_REG(base, name) (*(volatile uint32_t *)((uintptr_t)(base) + (uintptr_t)UART_##name##_OFS))
/* 예제 3 (주어짐): 촘촘한 레지스터 ID — UART_REG_CR1 = 0, ..., UART_REG_COUNT (표 인덱스·bad_mask 비트 번호) */
#define X_REG_ID(name, ofs, rst, acc) UART_REG_##name,
enum uart_reg_id { UART_REG_LIST(X_REG_ID) UART_REG_COUNT };

/* =============================== Part A — 에러 코드 =============================== */
typedef struct { int code; const char *name; enum fw_sev sev; const char *desc; } err_info_t;

/* TODO A1: 촘촘한(dense) 인덱스 enum FW_ERR_IDX_<name> = 0,1,2,... 와 마지막에 FW_ERR_COUNT */
#define X_ERR_IDX(name, val, sev, desc) FW_ERR_IDX_##name,
enum fw_err_idx { FW_ERR_LIST(X_ERR_IDX) FW_ERR_COUNT };

/* TODO A2: 목록 순서대로 { 값, "이름", 심각도, "설명" } 디스크립터 표 */
#define X_ERR_INFO(name, val, sev, desc) { (val), #name, (sev), (desc) },
static const err_info_t k_err_info[] = { FW_ERR_LIST(X_ERR_INFO) };

_Static_assert(ARRAY_LEN(k_err_info) == FW_ERR_COUNT, "err table out of sync with FW_ERR_COUNT");

/* TODO A3: 값 → 인덱스 (모르는 값은 -1). 값이 희소(sparse)하고 음수라 배열 첨자로 못 쓴다 → switch 생성.
 *          덤: 값이 겹치면 "duplicate case value" 컴파일 에러 → 중복 검사가 공짜. */
static int err_index(int code) {
#define X_ERR_CASE(name, val, sev, desc) case (val): return FW_ERR_IDX_##name;
    switch (code) {
        FW_ERR_LIST(X_ERR_CASE)
        default: return -1;
    }
#undef X_ERR_CASE
}

/* TODO A4: 이름·설명 조회. 모르는 값은 "UNKNOWN" / "unknown error" */
static const char *err_to_str(int code) {
    int i = err_index(code);
    return i < 0 ? "UNKNOWN" : k_err_info[i].name;
}
static const char *err_desc(int code) {
    int i = err_index(code);
    return i < 0 ? "unknown error" : k_err_info[i].desc;
}

/* TODO A5: 이름 → 값 (CLI·설정 파일 파싱용). 찾으면 *out 에 쓰고 true */
static bool err_from_str(const char *s, int *out) {
    for (size_t i = 0; i < ARRAY_LEN(k_err_info); i++) {
        if (strcmp(s, k_err_info[i].name) == 0) { *out = k_err_info[i].code; return true; }
    }
    return false;
}

/* ======================= Part B — 컴파일 타임 계약과 함정 ======================= */
/* TODO B1: 항목마다 _Static_assert 생성 — 값은 int8 와이어 필드에 들어가고 0 이하, 심각도는 범위 안 */
#define X_ERR_CHECK(name, val, sev, desc)                                                   \
    _Static_assert((val) <= 0 && (val) >= -128, "FW_ERR_" #name ": must fit int8 and be <= 0"); \
    _Static_assert((sev) >= SEV_INFO && (sev) <= SEV_FATAL, "FW_ERR_" #name ": bad severity");
FW_ERR_LIST(X_ERR_CHECK)

/* TODO B2: 가장 긴 이름의 크기(널 포함)를 컴파일 타임 상수로 — 로그 칸 폭·버퍼 크기용 */
#define X_ERR_NAMESZ(name, val, sev, desc) char n_##name[sizeof #name];
union err_name_sizer { FW_ERR_LIST(X_ERR_NAMESZ) };
#define ERR_NAME_MAX (sizeof(union err_name_sizer))

/* TODO B3: 에러 통계(텔레메트리) — 에러별 카운터. 목록에 없는 값은 g_err_unknown */
static uint32_t g_err_stats[FW_ERR_COUNT];
static uint32_t g_err_unknown;
static void err_record(int code) {
    int i = err_index(code);
    if (i < 0) g_err_unknown++;
    else g_err_stats[i]++;
}
/* "TIMEOUT=2 CRC=1 ?=1" — 0 이 아닌 카운터만 목록 순서로, 모르는 값은 맨 뒤 "?". 잘리면 -1 */
static int err_stats_format(char *buf, size_t cap) {
    size_t n = 0;
    buf[0] = '\0';
    for (size_t i = 0; i <= FW_ERR_COUNT; i++) {
        uint32_t c = i < FW_ERR_COUNT ? g_err_stats[i] : g_err_unknown;
        if (c == 0u) continue;
        int k = snprintf(buf + n, cap - n, "%s%s=%lu", n ? " " : "",
                         i < FW_ERR_COUNT ? k_err_info[i].name : "?", (unsigned long)c);
        if (k < 0 || (size_t)k >= cap - n) return -1;
        n += (size_t)k;
    }
    return (int)n;
}

/* ======================= Part C — 레지스터 맵 → 덤프·셀프테스트 ======================= */
typedef struct { const char *name; uint32_t ofs; uint32_t reset; enum reg_acc acc; } reg_desc_t;
typedef struct { uint32_t (*rd)(void *ctx, uint32_t ofs); void *ctx; } bus_t;   /* C-M06 함수 포인터 */

/* TODO C1: 목록 순서대로 { "이름", 오프셋, 리셋값, 접근 } 디스크립터 표 (인덱스 = UART_REG_<name>) */
#define X_REG_DESC(name, ofs, rst, acc) { #name, (ofs), (rst), (acc) },
static const reg_desc_t k_uart_regs[] = { UART_REG_LIST(X_REG_DESC) };

_Static_assert(ARRAY_LEN(k_uart_regs) == UART_REG_COUNT, "reg table out of sync with UART_REG_COUNT");
_Static_assert(UART_REG_COUNT <= 32, "bad_mask is uint32_t: one bit per register");

/* TODO C2: 항목마다 정적 검사 — 32비트 정렬, 블록 안 */
#define X_REG_CHECK(name, ofs, rst, acc)                                                  \
    _Static_assert(((ofs) & 3u) == 0u, "UART_" #name ": offset not 32-bit aligned");     \
    _Static_assert((ofs) < UART_BLOCK_SIZE, "UART_" #name ": offset outside the block");
UART_REG_LIST(X_REG_CHECK)

/* TODO C3: 오프셋 → 이름 (모르면 NULL). switch 라서 오프셋이 겹치면 컴파일 에러 */
static const char *reg_name_at(uint32_t ofs) {
#define X_REG_CASE(name, o, rst, acc) case (o): return #name;
    switch (ofs) {
        UART_REG_LIST(X_REG_CASE)
        default: return NULL;
    }
#undef X_REG_CASE
}

/* TODO C4: 가장 긴 레지스터 이름 크기 (덤프 칸 맞춤용) */
#define X_REG_NAMESZ(name, ofs, rst, acc) char n_##name[sizeof #name];
union reg_name_sizer { UART_REG_LIST(X_REG_NAMESZ) };
#define REG_NAME_MAX (sizeof(union reg_name_sizer))

/* TODO C5: 레지스터 덤프. 한 줄 형식 (이름은 REG_NAME_MAX-1 칸 왼쪽 정렬):
 *   읽을 수 있음(RO/RW/W1C): "SR     @0x0C = 0x000000E0 *"   (값 != 리셋값이면 " *")
 *   RC  : "RDR    @0x10 = (rc: not read)"   ← 읽으면 FIFO pop / 카운터 클리어 → 절대 읽지 않는다
 *   WO  : "TDR    @0x14 = (wo)"             ← 읽어 봐야 의미 없는 값
 * 각 줄 끝 '\n'. 반환: 쓴 글자 수, 버퍼가 모자라면 -1 */
static int reg_dump(const bus_t *bus, char *out, size_t cap) {
    size_t n = 0;
    out[0] = '\0';
    for (size_t i = 0; i < UART_REG_COUNT; i++) {
        const reg_desc_t *d = &k_uart_regs[i];
        int k;
        if (d->acc == ACC_RC) {
            k = snprintf(out + n, cap - n, "%-*s @0x%02lX = (rc: not read)\n",
                         (int)(REG_NAME_MAX - 1u), d->name, (unsigned long)d->ofs);
        } else if (d->acc == ACC_WO) {
            k = snprintf(out + n, cap - n, "%-*s @0x%02lX = (wo)\n",
                         (int)(REG_NAME_MAX - 1u), d->name, (unsigned long)d->ofs);
        } else {
            uint32_t v = bus->rd(bus->ctx, d->ofs);
            k = snprintf(out + n, cap - n, "%-*s @0x%02lX = 0x%08lX%s\n",
                         (int)(REG_NAME_MAX - 1u), d->name, (unsigned long)d->ofs,
                         (unsigned long)v, v != d->reset ? " *" : "");
        }
        if (k < 0 || (size_t)k >= cap - n) return -1;
        n += (size_t)k;
    }
    return (int)n;
}

/* TODO C6: 전원 투입 직후 셀프테스트 — 읽어도 되는(RC·WO 제외) 레지스터가 리셋값인지.
 *          틀린 레지스터는 *bad_mask 의 (1u << UART_REG_<name>) 비트. 하나라도 틀리면 FW_ERR_HW_FAULT */
static int reg_reset_check(const bus_t *bus, uint32_t *bad_mask) {
    uint32_t bad = 0u;
    for (size_t i = 0; i < UART_REG_COUNT; i++) {
        const reg_desc_t *d = &k_uart_regs[i];
        if (d->acc == ACC_RC || d->acc == ACC_WO) continue;
        if (bus->rd(bus->ctx, d->ofs) != d->reset) bad |= 1u << i;
    }
    *bad_mask = bad;
    return bad ? FW_ERR_HW_FAULT : FW_ERR_OK;
}

/* =================== 테스트용 mock 버스 (주어짐 — 목록과 독립적으로 오프셋을 직접 적는다) =================== */
typedef struct { uint32_t mem[16]; uint32_t read_mask; unsigned nreads; } mock_t;
static uint32_t mock_rd(void *ctx, uint32_t ofs) {
    mock_t *m = ctx;
    m->read_mask |= 1u << (ofs / 4u);
    m->nreads++;
    return m->mem[ofs / 4u];
}
static void mock_por(mock_t *m) {           /* 전원 투입 직후 상태 (데이터시트 값을 손으로 적음) */
    memset(m, 0, sizeof *m);
    m->mem[0x0C / 4] = 0x000000C0u;         /* SR: TXE | TC */
    m->mem[0x1C / 4] = 0x00A70011u;         /* ID */
}

/* ================================== 하네스 ================================== */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
#define STREQ(a, b) ((a) != NULL && strcmp((a), (b)) == 0)   /* NULL 이면 FAIL (starter 가 죽지 않게) */

int main(void) {
    char buf[512];
    int v;

    /* ---- Part A: enum·표·조회 ---- */
    CHECK(FW_ERR_TIMEOUT == -1 && FW_ERR_HW_FAULT == -100);          /* 주어진 예제 1 */
    CHECK(FW_ERR_COUNT == 9u + N_EXTRA);
    CHECK(STREQ(err_to_str(FW_ERR_OK), "OK"));
    CHECK(STREQ(err_to_str(-12), "OVERRUN"));
    CHECK(STREQ(err_to_str(FW_ERR_HW_FAULT), "HW_FAULT"));
    CHECK(STREQ(err_desc(FW_ERR_CRC), "CRC mismatch"));
    CHECK(STREQ(err_to_str(-5), "UNKNOWN") && STREQ(err_desc(-5), "unknown error"));
    CHECK(STREQ(err_to_str(1), "UNKNOWN") && err_index(-99) == -1);
    CHECK(err_index(FW_ERR_OK) == 0 && err_index(FW_ERR_HW_FAULT) == (int)FW_ERR_COUNT - 1);
    CHECK(err_from_str("NACK", &v) && v == -11);
    CHECK(!err_from_str("nack", &v) && !err_from_str("", &v) && !err_from_str("UNKNOWN", &v));
    v = err_index(FW_ERR_BUSY);
    CHECK(v == 2 && k_err_info[v].code == FW_ERR_BUSY && k_err_info[v].sev == SEV_WARN);

    /* ---- Part B: 동기화 — 목록이 바뀌어도 그대로 성립해야 하는 성질 (-DC_A07_EXTRA 로도 PASS) ---- */
    {
        bool roundtrip = true, unique = true;
        for (size_t i = 0; i < FW_ERR_COUNT; i++) {
            const err_info_t *e = &k_err_info[i];
            roundtrip = roundtrip && err_index(e->code) == (int)i && STREQ(err_to_str(e->code), e->name)
                        && STREQ(err_desc(e->code), e->desc) && err_from_str(e->name, &v) && v == e->code;
            for (size_t j = i + 1; j < FW_ERR_COUNT; j++)
                unique = unique && !STREQ(e->name, k_err_info[j].name) && e->code != k_err_info[j].code;
        }
        CHECK(roundtrip);
        CHECK(unique && FW_ERR_COUNT > 1u);
    }
    CHECK(STREQ(err_to_str(FW_ERR_TIMEOUT), "TIMEOUT") && TIMEOUT == 1000u);   /* 벤더 매크로에 안 먹힘 */
    CHECK(ERR_NAME_MAX == sizeof "HW_FAULT");
#ifdef C_A07_EXTRA
    CHECK(FW_ERR_ECC == -13 && STREQ(err_to_str(-13), "ECC") && err_index(-13) == 8);
    CHECK(err_index(FW_ERR_HW_FAULT) == 9);            /* 중간 삽입 → 뒤 항목의 인덱스가 밀렸다 */
#endif
    memset(g_err_stats, 0, sizeof g_err_stats);
    g_err_unknown = 0;
    err_record(FW_ERR_TIMEOUT); err_record(-10); err_record(FW_ERR_TIMEOUT); err_record(42);
    v = err_index(FW_ERR_TIMEOUT);
    CHECK(v >= 0 && g_err_stats[v] == 2u && g_err_unknown == 1u);
    CHECK(err_stats_format(buf, sizeof buf) == 19 && STREQ(buf, "TIMEOUT=2 CRC=1 ?=1"));
    CHECK(err_stats_format(buf, 10) == -1);
    err_record(FW_ERR_HW_FAULT);
    CHECK(err_stats_format(buf, sizeof buf) > 0 && STREQ(buf, "TIMEOUT=2 CRC=1 HW_FAULT=1 ?=1"));

    /* ---- Part C: 레지스터 맵 ---- */
    CHECK(UART_SR_OFS == 0x0Cu && UART_ID_OFS == 0x1Cu);            /* 주어진 예제 2 */
    CHECK(UART_REG_COUNT == 8u + N_EXTRA);
    CHECK(STREQ(k_uart_regs[UART_REG_SR].name, "SR") && k_uart_regs[UART_REG_SR].reset == 0xC0u);
    CHECK(k_uart_regs[UART_REG_RDR].acc == ACC_RC && k_uart_regs[UART_REG_TDR].acc == ACC_WO);
    CHECK(STREQ(reg_name_at(0x18u), "ERRCNT") && reg_name_at(0x02u) == NULL && reg_name_at(0x3FCu) == NULL);
    CHECK(REG_NAME_MAX == sizeof "ERRCNT");
    {
        bool sorted = true, names = true;
        for (size_t i = 0; i < UART_REG_COUNT; i++) {
            if (i > 0) sorted = sorted && k_uart_regs[i - 1].ofs < k_uart_regs[i].ofs;
            names = names && STREQ(reg_name_at(k_uart_regs[i].ofs), k_uart_regs[i].name);
        }
        CHECK(sorted && names);
    }
    {
        uint32_t fake_block[16] = {0};                       /* 호스트에서 UART_REG 매크로 확인 */
        UART_REG(fake_block, CR2) = 0x2000u;
        CHECK(fake_block[1] == 0x2000u);
    }
    {
        mock_t m;
        bus_t bus = { mock_rd, &m };
        uint32_t bad = 0xFFFFFFFFu;
        static const char expect_por[] =
            "CR1    @0x00 = 0x00000000\n"
            "CR2    @0x04 = 0x00000000\n"
            "BRR    @0x08 = 0x00000000\n"
            "SR     @0x0C = 0x000000C0\n"
            "RDR    @0x10 = (rc: not read)\n"
            "TDR    @0x14 = (wo)\n"
            "ERRCNT @0x18 = (rc: not read)\n"
            "ID     @0x1C = 0x00A70011\n"
#ifdef C_A07_EXTRA
            "DMACR  @0x20 = 0x00000000\n"
#endif
            ;
        mock_por(&m);
        CHECK(reg_dump(&bus, buf, sizeof buf) == (int)strlen(expect_por) && STREQ(buf, expect_por));
        CHECK((m.read_mask & ((1u << (0x10 / 4)) | (1u << (0x14 / 4)) | (1u << (0x18 / 4)))) == 0u);
        CHECK(m.nreads == 5u + N_EXTRA);
        CHECK(reg_dump(&bus, buf, 40) == -1);

        mock_por(&m);
        m.mem[0x04 / 4] = 0x00002000u;                     /* CR2: STOP=2 설정됨 */
        m.mem[0x0C / 4] = 0x000000E0u;                     /* SR: RXNE 도 켜짐 */
        CHECK(reg_dump(&bus, buf, sizeof buf) > 0
              && strstr(buf, "CR2    @0x04 = 0x00002000 *\n") != NULL
              && strstr(buf, "SR     @0x0C = 0x000000E0 *\n") != NULL
              && strstr(buf, "CR1    @0x00 = 0x00000000\n") != NULL);

        mock_por(&m);
        CHECK(reg_reset_check(&bus, &bad) == FW_ERR_OK && bad == 0u);
        CHECK((m.read_mask & ((1u << (0x10 / 4)) | (1u << (0x14 / 4)) | (1u << (0x18 / 4)))) == 0u);
        m.mem[0x1C / 4] = 0xDEAD0000u;                     /* ID 깨짐 */
        m.mem[0x08 / 4] = 0x0000008Bu;                     /* BRR 이 리셋값이 아님 */
        m.mem[0x10 / 4] = 0x55u;                           /* RDR(RC) 값은 검사 대상 아님 */
        CHECK(reg_reset_check(&bus, &bad) == FW_ERR_HW_FAULT
              && bad == ((1u << UART_REG_ID) | (1u << UART_REG_BRR)));
        CHECK(STREQ(err_to_str(reg_reset_check(&bus, &bad)), "HW_FAULT"));
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
