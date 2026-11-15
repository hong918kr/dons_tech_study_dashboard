/* ARCH-B14 추가 연습 — 한 번에 검증
 * 연습 1: 최근 8 부팅 히스토리 비트맵으로 부트 루프 감지 (연속 streak 와 비교)
 * 연습 2: 부트로더 → 앱 리셋 원인 핸드오프 메일박스 (magic + raw + ~raw, 한 번만 소비)
 * 연습 3: ST 스타일 CSR → "POR BOR PIN" 문자열 (우선순위 순, snprintf 의미론)
 * 연습 4: 크래시 마커로 SW 리셋을 "의도한 리셋" 과 "크래시 후 리셋" 으로 나누기
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    CAUSE_NONE, CAUSE_POR, CAUSE_BOR, CAUSE_POWER, CAUSE_IWDG, CAUSE_WWDG, CAUSE_LOCKUP,
    CAUSE_SW, CAUSE_LPWR, CAUSE_WAKEOFF, CAUSE_PIN, CAUSE__N
} cause_t;

/* ---------------- 연습 1: 히스토리 비트맵 ---------------- */
static uint8_t hist_push(uint8_t h, bool abnormal) {
    return (uint8_t)((h << 1) | (abnormal ? 1u : 0u));      /* bit0 = 가장 최근 부팅 */
}
static bool hist_safe(uint8_t h, unsigned k) {
    unsigned n = 0;
    while (h) { h &= (uint8_t)(h - 1u); n++; }               /* Kernighan popcount (C-B01) */
    return n >= k;
}

/* ---------------- 연습 2: 핸드오프 메일박스 ---------------- */
#define HANDOFF_MAGIC 0xB007CA5Eu
typedef struct { uint32_t magic, raw, inv; } handoff_t;      /* .noinit 에 둔다 */

static void bl_handoff(handoff_t *mb, uint32_t raw) {
    mb->raw = raw;
    mb->inv = ~raw;
    mb->magic = HANDOFF_MAGIC;                               /* magic 은 마지막에 */
}
/* 메일박스가 유효하면 그 값을 쓰고 무효화(한 번만 소비). 아니면 하드웨어 레지스터 값. */
static bool app_take_cause(handoff_t *mb, uint32_t hw_reg, uint32_t *out) {
    if (mb->magic == HANDOFF_MAGIC && mb->inv == ~mb->raw) {
        *out = mb->raw;
        mb->magic = 0;
        return true;
    }
    *out = hw_reg;
    return false;
}

/* ---------------- 연습 3: CSR → 문자열 ---------------- */
#define ST_BORRSTF  (1u << 25)
#define ST_PINRSTF  (1u << 26)
#define ST_PORRSTF  (1u << 27)
#define ST_SFTRSTF  (1u << 28)
#define ST_IWDGRSTF (1u << 29)
#define ST_WWDGRSTF (1u << 30)
#define ST_LPWRRSTF (1u << 31)

/* snprintf 처럼: 필요한 길이를 돌려주고, n > 0 이면 항상 NUL 종료 */
static int st_flags_str(uint32_t csr, char *buf, size_t n) {
    static const struct { uint32_t bit; const char *name; } T[] = {   /* 우선순위 순 */
        { ST_PORRSTF, "POR" }, { ST_BORRSTF, "BOR" }, { ST_IWDGRSTF, "IWDG" },
        { ST_WWDGRSTF, "WWDG" }, { ST_SFTRSTF, "SW" }, { ST_LPWRRSTF, "LPWR" }, { ST_PINRSTF, "PIN" },
    };
    size_t len = 0;
    if (n) buf[0] = '\0';
    for (size_t i = 0; i < sizeof T / sizeof T[0]; i++) {
        if (!(csr & T[i].bit)) continue;
        int w = snprintf(len < n ? buf + len : NULL, len < n ? n - len : 0,
                         "%s%s", len ? " " : "", T[i].name);
        if (w > 0) len += (size_t)w;
    }
    return (int)len;
}

/* ---------------- 연습 4: 크래시 마커 ---------------- */
#define CRASH_MAGIC 0xDEADFA17u
typedef struct { uint32_t marker; uint32_t pc; } crash_rec_t;      /* .noinit */
typedef enum { KIND_COLD, KIND_USER, KIND_HANG, KIND_CRASH, KIND_INTENTIONAL, KIND_OTHER } kind_t;

/* HardFault 핸들러가 리셋 직전에 부른다 (그 다음 SYSRESETREQ) */
static void fault_record(crash_rec_t *cr, uint32_t pc) { cr->pc = pc; cr->marker = CRASH_MAGIC; }

static kind_t classify(cause_t c, crash_rec_t *cr, uint32_t *pc_out) {
    bool crashed = cr->marker == CRASH_MAGIC;
    cr->marker = 0;                                   /* 어떤 경우든 소비 */
    switch (c) {
    case CAUSE_POR: case CAUSE_BOR: case CAUSE_POWER: return KIND_COLD;   /* RAM 쓰레기: 마커 무시 */
    case CAUSE_PIN:                                   return KIND_USER;
    case CAUSE_IWDG: case CAUSE_WWDG: case CAUSE_LOCKUP: return KIND_HANG;
    case CAUSE_SW:
        if (crashed) { *pc_out = cr->pc; return KIND_CRASH; }
        return KIND_INTENTIONAL;
    default:                                          return KIND_OTHER;
    }
}

/* ---------------- 테스트 하네스 ---------------- */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

int main(void) {
    /* 연습 1: "크래시 → (healthy 표시) → 크래시 ..." 패턴. 연속 streak 는 못 잡는다. */
    {
        /* 1 = 워치독 리셋, 0 = 정상 부팅. 각 크래시 전에 앱이 healthy 를 찍었다고 가정 */
        static const bool ev[] = { 0, 1, 0, 1, 0, 1 };
        uint8_t h = 0; unsigned streak = 0, max_streak = 0; int first_safe = -1;
        for (int i = 0; i < 6; i++) {
            streak = ev[i] ? 1u : 0u;                  /* healthy 가 매번 0 으로 되돌림 */
            if (streak > max_streak) max_streak = streak;
            h = hist_push(h, ev[i]);
            if (first_safe < 0 && hist_safe(h, 3)) first_safe = i;
        }
        CHECK(max_streak == 1);                        /* streak 방식: safe mode 안 감 */
        CHECK(first_safe == 5);                        /* 창 방식: 세 번째 크래시에서 감지 */
        CHECK(h == 0x15);                              /* 0b010101 */
        for (int i = 0; i < 8; i++) h = hist_push(h, false);
        CHECK(!hist_safe(h, 3) && h == 0);             /* 8 번 정상 부팅하면 창에서 빠진다 */
        CHECK(hist_safe(0xFF, 8) && !hist_safe(0x7F, 8));
    }
    /* 연습 2 */
    {
        handoff_t mb; memset(&mb, 0x5A, sizeof mb);   /* 파워업 쓰레기 */
        uint32_t v = 0;
        CHECK(!app_take_cause(&mb, 0x0E000000u, &v) && v == 0x0E000000u);   /* 쓰레기 → HW 값 */
        bl_handoff(&mb, 0x24000000u);                  /* 부트로더가 IWDG|PIN 을 읽고 지움 */
        CHECK(app_take_cause(&mb, 0, &v) && v == 0x24000000u);
        CHECK(!app_take_cause(&mb, 0, &v) && v == 0);  /* 두 번째는 소비됨 */
        bl_handoff(&mb, 0x14000000u);
        mb.raw ^= 1u << 28;                            /* 비트 하나 깨짐 */
        CHECK(!app_take_cause(&mb, 0x04000000u, &v) && v == 0x04000000u);
    }
    /* 연습 3 */
    {
        char b[32];
        CHECK(st_flags_str(0x0E000000u, b, sizeof b) == 11 && strcmp(b, "POR BOR PIN") == 0);
        CHECK(st_flags_str(ST_IWDGRSTF | ST_PINRSTF, b, sizeof b) == 8 && strcmp(b, "IWDG PIN") == 0);
        CHECK(st_flags_str(ST_LPWRRSTF | ST_SFTRSTF, b, sizeof b) == 7 && strcmp(b, "SW LPWR") == 0);
        CHECK(st_flags_str(0x1u, b, sizeof b) == 0 && b[0] == '\0');      /* LSION 만 */
        char t[4];
        CHECK(st_flags_str(0x0E000000u, t, sizeof t) == 11 && strcmp(t, "POR") == 0);  /* 잘림 */
        char u[6];
        CHECK(st_flags_str(0x0E000000u, u, sizeof u) == 11 && strcmp(u, "POR B") == 0);
    }
    /* 연습 4 */
    {
        crash_rec_t cr; memset(&cr, 0, sizeof cr);
        uint32_t pc = 0;
        CHECK(classify(CAUSE_SW, &cr, &pc) == KIND_INTENTIONAL);           /* OTA 후 재부팅 */
        fault_record(&cr, 0x08001234u);
        CHECK(classify(CAUSE_SW, &cr, &pc) == KIND_CRASH && pc == 0x08001234u);
        CHECK(classify(CAUSE_SW, &cr, &pc) == KIND_INTENTIONAL);           /* 마커는 한 번만 */
        fault_record(&cr, 0x08005678u);
        CHECK(classify(CAUSE_POR, &cr, &pc) == KIND_COLD && cr.marker == 0); /* 전원이면 무시 */
        CHECK(classify(CAUSE_IWDG, &cr, &pc) == KIND_HANG);
        CHECK(classify(CAUSE_PIN, &cr, &pc) == KIND_USER);
        CHECK(classify(CAUSE_WAKEOFF, &cr, &pc) == KIND_OTHER);
    }

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
