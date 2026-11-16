/* ENV: qemu */
/* BM-A11 종합: 베어메탈 데이터 로거 (qemu-system-arm -M mps2-an385, Cortex-M3)
 *
 * 한 파일 = startup (BM-B08 축약판) + 이미지 헤더 자기검사 (BM-A04 헤더 축약판, CRC 만)
 *         + timer0 100 Hz 샘플링 + 샘플 링 → 로그 링 → UART0 TX 인터럽트 + 워치독 (CMSDK, NMI 경고 + 리셋)
 *         + HardFault 덤프 (.noinit, BM-A09 아이디어 축약판) + 5 부팅 시나리오 + 마지막 부팅의 자체 검사.
 *   부팅 1: 콜드 → 12 샘플 로그 → 주입: main 멈춤        → 워치독 NMI(빵부스러기) → 워치독 리셋
 *   부팅 2: 원인=WATCHDOG 보고 → 12 샘플 → 주입: 틱 정지 → 워치독 리셋 (main 은 살아 있는데도)
 *   부팅 3: 원인=WATCHDOG → 12 샘플 → 주입: BusFault (main)   → HardFault 덤프 → SYSRESETREQ
 *   부팅 4: 원인=FAULT, 덤프 보고 → 12 샘플 → 주입: ISR 안 UDF → 덤프 → SYSRESETREQ
 *   부팅 5: 원인=FAULT, 덤프 보고 → 12 샘플 → DONE → 검사 표 → SYS_EXIT 0/1
 * UART0 출력(로그)은 run_qemu.sh 가 파일로 받아 logtool.py 로 expected_uart.txt 와 비교한다.
 * 세미호스팅은 "시험 하네스" 채널 (진행 표시, CHECK, 종료 코드) 이다.
 * 빌드/실행 (Lima VM elinux): bash run_qemu.sh solution|starter|practice|probe|bugs|corrupt|mem|syms
 */
#include <stddef.h>
#include <stdint.h>

extern uint32_t _sidata[], _sdata[], _edata[], _sbss[], _ebss[], _estack[], _snoinit[], _enoinit[];
extern const uint8_t _img_hdr[];

/* ---------------- 세미호스팅 (BM-B08 축약판) ---------------- */
static int semi(int op, const void *arg) {
    register int r0 __asm__("r0") = op;
    register const void *r1 __asm__("r1") = arg;
    __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}
static void sh_puts(const char *s) { semi(0x04, s); }
static void sh_hex(uint32_t v) {
    char b[11]; b[0] = '0'; b[1] = 'x'; b[10] = 0;
    for (int i = 9; i >= 2; --i, v >>= 4) b[i] = "0123456789abcdef"[v & 0xFu];
    sh_puts(b);
}
static void sh_dec(uint32_t v) {
    char b[11]; int i = 10; b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    sh_puts(&b[i]);
}
__attribute__((noreturn)) static void sh_exit(int code) {
    uint32_t blk[2] = { 0x20026u, (uint32_t)code };          /* SYS_EXIT_EXTENDED, ADP_Stopped_ApplicationExit */
    semi(0x20, blk);
    for (;;) { }
}

/* ---------------- 레지스터 (QEMU v10.2.0 소스에서 확인 / ARMv7-M ARM — 확인 권장) ---------------- */
#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG8(a)  (*(volatile uint8_t *)(uintptr_t)(a))
/* CMSDK APB timer: CTRL 0x0 (EN 0, IRQEN 3), VALUE 0x4, RELOAD 0x8, INTSTATUS 0xC (W1C) */
#define T0_BASE 0x40000000u                 /* timer0, NVIC IRQ 8 */
#define T1_BASE 0x40001000u                 /* timer1: 자유 카운트 (부하 측정용 시계, 인터럽트 없음) */
#define TM_CTRL(b)   REG32((b) + 0x00u)
#define TM_VALUE(b)  REG32((b) + 0x04u)
#define TM_RELOAD(b) REG32((b) + 0x08u)
#define TM_INT(b)    REG32((b) + 0x0Cu)
#define TM_EN    1u
#define TM_IRQEN 8u
/* CMSDK APB UART0 @0x40004000: DATA 0, STATE 4, CTRL 8 (TX_EN 0, TX_INTEN 2), INTSTATUS 0xC (W1C), BAUDDIV 0x10.
 * TX 인터럽트 = NVIC IRQ 1 (RX 는 0). TX_INTEN 을 켜도 빈 버퍼에선 인터럽트가 안 난다 (probe P2) → 첫 바이트를 직접 쓴다. */
#define U0_DATA    REG32(0x40004000u)
#define U0_CTRL    REG32(0x40004008u)
#define U0_INT     REG32(0x4000400Cu)
#define U0_BAUDDIV REG32(0x40004010u)
#define U_TX_EN    1u
#define U_TX_INTEN 4u
/* CMSDK APB watchdog @0x40008000: LOAD 0, VALUE 4, CONTROL 8 (INTEN 0, RESEN 1), INTCLR 0xC (= 재장전), LOCK 0xC00.
 * 1차 만료 → 인터럽트 (mps2: NMI 에 연결), 2차 만료 → 리셋 (RESEN). */
#define WD_LOAD    REG32(0x40008000u)
#define WD_CTRL    REG32(0x40008008u)
#define WD_INTCLR  REG32(0x4000800Cu)
#define WD_LOCK    REG32(0x40008C00u)
#define WD_KEY     0x1ACCE551u
#define WD_INTEN   1u
#define WD_RESEN   2u
#define NVIC_ISER0 REG32(0xE000E100u)
#define NVIC_ICER0 REG32(0xE000E180u)
#define NVIC_ICPR0 REG32(0xE000E280u)
#define NVIC_IPR(n) REG8(0xE000E400u + (n))
#define SCB_AIRCR  REG32(0xE000ED0Cu)
#define SCB_CFSR   REG32(0xE000ED28u)
#define SCB_HFSR   REG32(0xE000ED2Cu)
#define SCB_MMFAR  REG32(0xE000ED34u)
#define SCB_BFAR   REG32(0xE000ED38u)
#define CFSR_MMARVALID (1u << 7)
#define CFSR_BFARVALID (1u << 15)
#define IRQ_UART0_TX 1u
#define IRQ_TIMER0   8u

#define PCLK_HZ    25000000u                /* mps2 SYSCLK (hw/arm/mps2.c) */
#define TICK_HZ    100u
#define WDOG_LOAD  (PCLK_HZ / 10u)          /* 100 ms: NMI 경고, 다시 100 ms: 리셋 */
#define N_SAMPLES  12u
#define N_BOOTS    5u
#define MAX_BOOTS  8u
#define LOG_DEADLINE_TICKS  60u             /* 로그 단계가 이 틱 수 안에 끝나야 (멈춤 방지) */
#define HANG_DEADLINE_TICKS 60u             /* 주입한 멈춤을 워치독이 600 ms 안에 못 잡으면 FAIL */

#define dsb() __asm__ volatile("dsb" ::: "memory")
#define dmb() __asm__ volatile("dmb" ::: "memory")
#define isb() __asm__ volatile("isb" ::: "memory")
static inline uint32_t irq_save(void) {
    uint32_t p;
    __asm__ volatile("mrs %0, primask\n cpsid i" : "=r"(p) :: "memory");
    return p;
}
static inline void irq_restore(uint32_t p) { __asm__ volatile("msr primask, %0" :: "r"(p) : "memory"); }

/* ---------------- CRC-32/ISO-HDLC (zlib 과 같은 값, 비트 단위) ---------------- */
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n) {
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* ---------------- 이미지 헤더 (BM-A04 헤더 축약판: CRC 만, flags bit0 = 0) ---------------- */
typedef struct {
    uint32_t magic;          /* 0x474D4942 "BIMG" */
    uint16_t hdr_version;    /* 1 */
    uint16_t hdr_size;       /* 64 */
    uint32_t img_version;    /* major<<24 | minor<<16 | patch */
    uint32_t payload_size;   /* 이 레이아웃에선 [0, _img_hdr) 의 크기 */
    uint32_t load_addr;      /* payload(= 벡터 테이블) 주소 = 0 */
    uint32_t flags;          /* bit0 = SHA-256 있음 → 축약판은 0 */
    uint32_t payload_crc32;
    uint8_t  sha256[32];     /* 축약판에선 0 */
    uint32_t hdr_crc32;      /* [0, 60) 의 CRC-32 */
} img_hdr_t;
_Static_assert(sizeof(img_hdr_t) == 64, "img_hdr_t must be 64 bytes");
#define IMG_MAGIC 0x474D4942u
/* 자리만 잡는다 (전부 0). 빌드 뒤 mkimg.py 가 ELF 안의 이 64 B 를 진짜 헤더로 바꾼다. */
__attribute__((section(".img_hdr"), used)) const img_hdr_t g_img_hdr_slot = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, { 0u }, 0u };

typedef enum { IMG_OK = 0, IMG_BAD_MAGIC, IMG_BAD_HDR_VERSION, IMG_BAD_HDR_CRC, IMG_BAD_SIZE, IMG_BAD_LOAD_ADDR,
               IMG_BAD_CRC } img_rc_t;
static const char *const k_img_rc[] = { "OK", "BAD_MAGIC", "BAD_HDR_VERSION", "BAD_HDR_CRC", "BAD_SIZE",
                                         "BAD_LOAD_ADDR", "BAD_CRC" };
/* 순서가 중요: 헤더 자체를 믿을 수 있게 된 뒤에야(hdr CRC) 그 안의 크기·주소로 메모리를 읽는다 */
static img_rc_t img_verify(const img_hdr_t *h, uint32_t slot_base, uint32_t slot_len, uint32_t expect_size) {
    if (h->magic != IMG_MAGIC) return IMG_BAD_MAGIC;
    if (h->hdr_version != 1u || h->hdr_size != sizeof(img_hdr_t)) return IMG_BAD_HDR_VERSION;
    if (crc32_update(0u, (const uint8_t *)h, (uint32_t)offsetof(img_hdr_t, hdr_crc32)) != h->hdr_crc32)
        return IMG_BAD_HDR_CRC;
    if (h->payload_size == 0u || h->payload_size > slot_len - (uint32_t)sizeof(img_hdr_t) ||
        h->payload_size != expect_size) return IMG_BAD_SIZE;
    if (h->load_addr != slot_base) return IMG_BAD_LOAD_ADDR;
    if (crc32_update(0u, (const uint8_t *)(uintptr_t)h->load_addr, h->payload_size) != h->payload_crc32)
        return IMG_BAD_CRC;
    return IMG_OK;
}

/* ---------------- 가짜 센서: 틱의 결정적 함수 (logtool.py 가 같은 식으로 기대 로그를 만든다) ---------------- */
static uint32_t sensor_value(uint32_t tick) {
    uint32_t x = tick * 2654435761u;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12;
    uint32_t ph = tick % 50u;
    uint32_t tri = ph < 25u ? ph : 50u - ph;
    return 2000u + tri * 8u + (x & 7u);
}

/* ---------------- 링 두 개 (SPSC, 2 의 거듭제곱, 자유 증가 인덱스) ---------------- */
typedef struct { uint32_t tick, value; } sample_t;
#define SRING_N 8u                           /* 생산자 timer0 ISR → 소비자 main */
static sample_t s_ring[SRING_N];
static volatile uint32_t s_head, s_tail;
static int sring_push(sample_t s) {
    uint32_t h = s_head;
    if (h - s_tail == SRING_N) return 0;
    s_ring[h & (SRING_N - 1u)] = s;
    dmb();
    s_head = h + 1u;
    return 1;
}
static int sring_pop(sample_t *o) {
    uint32_t t = s_tail;
    if (t == s_head) return 0;
    *o = s_ring[t & (SRING_N - 1u)];
    dmb();
    s_tail = t + 1u;
    return 1;
}
#define TXRING_N 512u                        /* 생산자 main(log_*) → 소비자 UART0 TX ISR */
static uint8_t tx_ring[TXRING_N];
static volatile uint32_t tx_head, tx_tail, tx_busy;

/* ---------------- 공유 상태 ---------------- */
static volatile uint32_t g_tick;             /* TODO 1 이 올리는 샘플 틱 */
static volatile uint32_t g_isr_ticks;        /* 프레임워크 카운터 (ISR 진입 수, TODO 와 무관) */
static volatile uint32_t g_drops;            /* 샘플 링이 가득 차서 버린 수 */
static volatile uint32_t g_kick_tick;        /* 마지막 킥 때의 g_tick (NMI 빵부스러기가 읽는다) */
static volatile uint32_t g_kicks, g_tx_bytes, g_tx_drops, g_nmi_count;
static volatile uint32_t g_stage, g_stage_tick0, g_inject_isr_fault;
enum { ST_LOG = 0, ST_HANG, ST_STALL, ST_BUS, ST_ISRF, ST_DONE };

static void wdog_kick(void) {                /* CMSDK: INTCLR 쓰기 = 인터럽트 지우기 + 카운터 재장전 */
    WD_LOCK = WD_KEY;
    WD_INTCLR = 1u;
    WD_LOCK = 0u;
    g_kicks++;
}

/* ================= TODO 1: 틱 ISR 의 일 =================
 * g_tick 을 1 올리고, 그 틱의 샘플 {tick, sensor_value(tick)} 을 샘플 링에 넣는다.
 * 링이 가득 차 있으면 버리고 g_drops 를 센다 (ISR 은 절대 기다리지 않는다). */
static void tick_isr(void) {
    uint32_t t = g_tick + 1u;
    g_tick = t;
    sample_t s = { t, sensor_value(t) };
    if (!sring_push(s)) g_drops++;
}

/* ================= TODO 2: UART0 TX 인터럽트의 일 =================
 * (INTSTATUS.TX 는 이미 W1C 로 지웠다.) 로그 링에 바이트가 있으면 하나 꺼내 DATA 에 쓰고 g_tx_bytes++,
 * 비었으면 tx_busy = 0 ("송신기 쉼" — 다음 바이트는 log_kick 이 직접 밀어 넣어야 한다). */
static void uart_tx_isr(void) {
    uint32_t t = tx_tail;
    if (t != tx_head) {
        U0_DATA = tx_ring[t & (TXRING_N - 1u)];
        tx_tail = t + 1u;
        g_tx_bytes++;
    } else {
        tx_busy = 0u;
    }
}

/* ================= TODO 3: 송신기 깨우기 (prime) =================
 * CMSDK TX 인터럽트는 "TXFULL 1→0" 때만 선다 → 쉬고 있는 송신기는 첫 바이트를 main 이 써 줘야 돈다.
 * 임계 구역 안에서: tx_busy == 0 이고 링에 바이트가 있으면 tx_busy = 1, 한 바이트 꺼내 DATA 에 쓴다. */
static void log_kick(void) {
    uint32_t pm = irq_save();
    if (!tx_busy && tx_tail != tx_head) {
        uint32_t t = tx_tail;
        tx_busy = 1u;
        tx_tail = t + 1u;                    /* 꼬리를 먼저 옮긴다: DATA 쓰기 직후 TX ISR 이 와도 같은 바이트를 또 안 보낸다 */
        U0_DATA = tx_ring[t & (TXRING_N - 1u)];
        g_tx_bytes++;
    }
    irq_restore(pm);
}

/* ================= TODO 4: 워치독 킥 정책 =================
 * main 루프에서만 킥한다. 조건 둘 다: (a) 틱 ISR 이 살아 있다 — g_tick 이 마지막 킥 이후 바뀌었다,
 * (b) main 이 따라잡고 있다 — 샘플 링이 비었다. 킥하면 g_kick_tick = 지금 틱. */
static void wdog_service(void) {
    uint32_t t = g_tick;
#ifdef BUG_UNCOND_KICK
    wdog_kick(); g_kick_tick = t; return;           /* 결함 주입: 조건 없이 킥 → 틱이 죽어도 개는 배부르다 */
#endif
    if (t != g_kick_tick && s_head == s_tail) {
        wdog_kick();
        g_kick_tick = t;
    }
}

/* ---------------- 로그 출력 (main 전용 생산자) ---------------- */
static void log_putc(char c) {
    uint32_t spins = 0;
    while (tx_head - tx_tail == TXRING_N) {          /* 가득: 송신기를 깨우고 제한된 시간만 기다린다 */
        log_kick();
        if (++spins > 200000u) { g_tx_drops++; return; }
    }
    tx_ring[tx_head & (TXRING_N - 1u)] = (uint8_t)c;
    dmb();
    tx_head = tx_head + 1u;
}
static void log_puts(const char *s) { while (*s) log_putc(*s++); log_kick(); }
static void log_hex(uint32_t v) {
    char b[11]; b[0] = '0'; b[1] = 'x'; b[10] = 0;
    for (int i = 9; i >= 2; --i, v >>= 4) b[i] = "0123456789abcdef"[v & 0xFu];
    log_puts(b);
}
static void log_dec_w(uint32_t v, int width) {        /* width 자리 0 채움 (0 = 그대로) */
    char b[11]; int i = 10; b[i] = 0;
    do { b[--i] = (char)('0' + v % 10u); v /= 10u; } while (v);
    while (10 - i < width) b[--i] = '0';
    log_puts(&b[i]);
}
static void log_flush(void) {                        /* 링이 비고 송신기가 쉴 때까지 (제한 있음) */
    for (uint32_t spins = 0; spins < 2000000u; ++spins) {
        if (tx_head == tx_tail && !tx_busy) return;
        log_kick();
    }
}

/* ---------------- .noinit: 리셋을 건너는 기록 세 가지 ---------------- */
#define CRUMB_MAGIC 0x57444F47u      /* "WDOG" 워치독 NMI 빵부스러기 */
#define DUMP_MAGIC  0xD0D0FA17u      /* HardFault 덤프 */
#define SCEN_MAGIC  0x5343454Eu      /* "SCEN" 시나리오 진행 */
#define DUMP_VERSION 1u
typedef struct {
    uint32_t magic, boot, stage, tick, kick_tick, isr_ticks, drops, pc, exc_return;
    uint32_t crc;                    /* CRC-32 over [boot, crc) */
} crumb_t;
typedef struct {
    uint32_t magic, version, boot, exc, exc_return;
    uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;      /* 하드웨어가 쌓은 프레임 */
    uint32_t cfsr, hfsr, mmfar, bfar;
    uint32_t crc;                    /* CRC-32 over [version, crc) */
} dump_t;
typedef struct { uint32_t cause, img_rc, logged, sum, drops, kicks, tx_bytes, busy, total; } boot_res_t;
typedef struct {
    uint32_t magic, boots, n_crumbs, n_dumps;
    boot_res_t res[MAX_BOOTS];
    crumb_t crumbs[2];
    dump_t dumps[2];
    uint32_t crc;                    /* CRC-32 over [boots, crc) */
} scen_t;
__attribute__((section(".noinit"))) crumb_t g_crumb;
__attribute__((section(".noinit"))) dump_t g_dump;
__attribute__((section(".noinit"))) scen_t g_scen;

#define CRC_FROM(p, first, last) crc32_update(0u, (const uint8_t *)&(p)->first, \
        (uint32_t)(offsetof(__typeof__(*(p)), last) - offsetof(__typeof__(*(p)), first)))
static int crumb_valid(const crumb_t *c) { return c->magic == CRUMB_MAGIC && c->crc == CRC_FROM(c, boot, crc); }
static int dump_valid(const dump_t *d) {
    return d->magic == DUMP_MAGIC && d->version == DUMP_VERSION && d->crc == CRC_FROM(d, version, crc);
}
static int scen_valid(void) {
    return g_scen.magic == SCEN_MAGIC && g_scen.crc == CRC_FROM(&g_scen, boots, crc) &&
           g_scen.n_crumbs <= 2u && g_scen.n_dumps <= 2u;
}
static void copy_words(void *dst, const void *src, uint32_t bytes) {   /* 구조체 대입은 memcpy 호출이 된다 (-nostdlib) */
    uint32_t *d = dst;
    const uint32_t *s = src;
    for (uint32_t i = 0; i < bytes / 4u; ++i) d[i] = s[i];
}
static void scen_seal(void) { g_scen.crc = CRC_FROM(&g_scen, boots, crc); dsb(); }

__attribute__((noreturn)) static void system_reset(void) {
    dsb();
    SCB_AIRCR = (0x05FAu << 16) | (SCB_AIRCR & (7u << 8)) | (1u << 2);   /* VECTKEY | PRIGROUP | SYSRESETREQ */
    dsb();
    for (;;) { }
}

/* ---------------- NMI = 워치독 1차 만료: 빵부스러기만 남기고 킥은 하지 않는다 ---------------- */
__attribute__((used)) void wdog_nmi(const uint32_t *frame, uint32_t exc_return) {
    crumb_t *c = &g_crumb;
    c->magic = 0u;
    c->boot = g_scen.boots; c->stage = g_stage; c->tick = g_tick; c->kick_tick = g_kick_tick;
    c->isr_ticks = g_isr_ticks; c->drops = g_drops; c->pc = frame[6]; c->exc_return = exc_return;
    c->crc = CRC_FROM(c, boot, crc);
    dsb();
    c->magic = CRUMB_MAGIC;                         /* magic 은 맨 마지막 (BM-M13) */
    dsb();
    sh_puts("  [nmi] watchdog early warning: breadcrumb sealed, not kicking\n");
    if (++g_nmi_count >= 3u) {                      /* 정책 버그(아픈데도 킥)면 리셋이 영영 안 온다 */
        sh_puts("FAIL: 3 watchdog NMIs without a reset (dog kept being fed while sick)\n");
        sh_exit(1);
    }
}
__attribute__((naked)) void NMI_Handler(void) {
    __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n mov r1, lr\n b wdog_nmi\n");
}

/* ---------------- HardFault → 덤프 (BM-A09 아이디어 축약판) → SYSRESETREQ ---------------- */
static int frame_ok(const uint32_t *f) {
    uintptr_t a = (uintptr_t)f;
    return a >= 0x20000000u && a + 32u <= (uintptr_t)_estack && (a & 3u) == 0u;
}
__attribute__((used, noreturn)) void fault_capture(const uint32_t *frame, uint32_t exc_return, uint32_t ipsr) {
    dump_t *d = &g_dump;
    d->magic = 0u;
    d->version = DUMP_VERSION; d->boot = g_scen.boots; d->exc = ipsr & 0x1FFu; d->exc_return = exc_return;
    if (frame_ok(frame)) {
        d->r0 = frame[0]; d->r1 = frame[1]; d->r2 = frame[2]; d->r3 = frame[3];
        d->r12 = frame[4]; d->lr = frame[5]; d->pc = frame[6]; d->xpsr = frame[7];
    } else {
        d->r0 = d->r1 = d->r2 = d->r3 = d->r12 = d->lr = d->pc = d->xpsr = 0xFFFFFFFFu;
    }
    d->cfsr = SCB_CFSR; d->hfsr = SCB_HFSR;
    d->mmfar = (d->cfsr & CFSR_MMARVALID) ? SCB_MMFAR : 0u;
    d->bfar = (d->cfsr & CFSR_BFARVALID) ? SCB_BFAR : 0u;
    d->crc = CRC_FROM(d, version, crc);
    dsb();
    d->magic = DUMP_MAGIC;
    dsb();
    sh_puts("  [hardfault] dump sealed in .noinit, SYSRESETREQ\n");
    system_reset();
}
__attribute__((naked)) void HardFault_Handler(void) {
    __asm__ volatile("tst lr, #4\n ite eq\n mrseq r0, msp\n mrsne r0, psp\n mov r1, lr\n mrs r2, ipsr\n"
                     " b fault_capture\n");
}

/* ---------------- 인터럽트 껍데기 (플래그 W1C 는 껍데기가 먼저; 일은 TODO 1/2) ---------------- */
void Timer0_Handler(void) {
    TM_INT(T0_BASE) = 1u;                           /* W1C 를 먼저 (BM-M01 "늦은 플래그 지우기") */
    g_isr_ticks++;
#ifdef BUG_KICK_IN_ISR
    wdog_kick();                                    /* 결함 주입: ISR 에서 킥 → main 이 죽어도 개는 배부르다 */
#endif
    tick_isr();                                     /* TODO 1 */
    if (g_inject_isr_fault) __asm__ volatile(".global pc_isr\npc_isr: udf #1");
    if ((g_stage == ST_HANG || g_stage == ST_ISRF) && g_isr_ticks - g_stage_tick0 > HANG_DEADLINE_TICKS) {
        sh_puts("FAIL: injected failure was not caught within the deadline (stage ");
        sh_dec(g_stage); sh_puts(")\n");
        sh_exit(1);
    }
}
void Uart0Tx_Handler(void) {
    U0_INT = 1u;                                    /* INTSTATUS.TX W1C */
    uart_tx_isr();                                  /* TODO 2 */
}

/* ---------------- startup 벡터 (BM-B08 축약판) ---------------- */
void Reset_Handler(void);
void Default_Handler(void) {
    uint32_t ipsr;
    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));
    sh_puts("Default_Handler: unexpected exception "); sh_dec(ipsr); sh_puts("\n");
    sh_exit(2);
}
typedef void (*vector_t)(void);
#define D Default_Handler
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + 32] = {
    (vector_t)_estack, Reset_Handler, NMI_Handler, HardFault_Handler, D, D, D, 0, 0, 0, 0, D, D, 0, D, D,
    D, Uart0Tx_Handler, D, D, D, D, D, D, Timer0_Handler, D, D, D, D, D, D, D,      /* IRQ 0..15 */
    D, D, D, D, D, D, D, D, D, D, D, D, D, D, D, D,                                 /* IRQ 16..31 */
};

/* ---------------- 주변장치 시작 ---------------- */
static void uart_init(void) {
    U0_BAUDDIV = PCLK_HZ / 115200u;                 /* 217: QEMU 는 >= 16 만 확인 (BM-B09) */
    U0_CTRL = U_TX_EN | U_TX_INTEN;
    U0_INT = 0xFu;                                  /* 남은 플래그 W1C */
    NVIC_ICPR0 = 1u << IRQ_UART0_TX;                /* NVIC pending 은 래치 → 따로 지운다 (probe P2) */
    NVIC_IPR(IRQ_UART0_TX) = 0x80u;                 /* 로그는 샘플링보다 낮게 */
    NVIC_ISER0 = 1u << IRQ_UART0_TX;
}
static void timers_start(void) {
    TM_RELOAD(T1_BASE) = 0xFFFFFFFFu;               /* timer1: 자유 카운트 시계 (25 MHz, 내림) */
    TM_CTRL(T1_BASE) = TM_EN;
    TM_RELOAD(T0_BASE) = PCLK_HZ / TICK_HZ - 1u;    /* 주기 = RELOAD+1 클럭 (CMSDK — TRM 확인 권장) */
    TM_INT(T0_BASE) = 1u;
    NVIC_ICPR0 = 1u << IRQ_TIMER0;
    NVIC_IPR(IRQ_TIMER0) = 0x40u;                   /* 샘플링이 로그(UART)보다 우선 */
    NVIC_ISER0 = 1u << IRQ_TIMER0;
    TM_CTRL(T0_BASE) = TM_EN | TM_IRQEN;
}
static void wdog_start(void) {
    WD_LOCK = WD_KEY;
    WD_LOAD = WDOG_LOAD;
    WD_CTRL = WD_INTEN | WD_RESEN;                  /* INTEN 이 카운터를 출발시킨다 (QEMU 모델) */
    WD_LOCK = 0u;                                   /* 잠금: 이후 쓰기는 무시 (probe P4) */
}

/* ---------------- main 루프: 샘플을 꺼내 로그 → 킥 정책 → 잠들기 ---------------- */
static uint32_t g_logged, g_sum, g_busy, g_total;
static void log_sample(const sample_t *s) {
    log_puts("S "); log_dec_w(g_scen.boots, 0); log_puts(" ");
    log_dec_w(g_logged + 1u, 2); log_puts(" t="); log_dec_w(s->tick, 4);
    log_puts(" v="); log_dec_w(s->value, 0); log_puts("\n");
    g_logged++;
    g_sum += s->value;
}
static void run_loop(uint32_t until_logged) {
    uint32_t t_start = TM_VALUE(T1_BASE), sleep = 0u;
    while (g_logged < until_logged) {
        sample_t s;
        if (sring_pop(&s)) {
            if (g_logged < N_SAMPLES) log_sample(&s);
        } else {
            uint32_t pm = irq_save();               /* "비었나 확인 → 잠들기" 사이에 온 인터럽트를 놓치지 않게 */
            if (s_head == s_tail) {
                uint32_t a = TM_VALUE(T1_BASE);
                __asm__ volatile("wfi");            /* PRIMASK=1 이어도 pending 이면 깬다 */
                sleep += a - TM_VALUE(T1_BASE);
            }
            irq_restore(pm);                        /* 여기서 ISR 이 돈다 */
        }
        wdog_service();                             /* TODO 4 */
        if (g_stage == ST_LOG && g_isr_ticks > LOG_DEADLINE_TICKS) break;
    }
    g_total = t_start - TM_VALUE(T1_BASE);
    g_busy = g_total - sleep;
}

/* ---------------- 시나리오 ---------------- */
enum { RC_POWER_ON = 0, RC_WATCHDOG, RC_FAULT, RC_UNKNOWN };
static const char *const k_cause[] = { "POWER_ON", "WATCHDOG", "FAULT", "UNKNOWN" };
static const uint32_t k_plan[N_BOOTS] = { ST_HANG, ST_STALL, ST_BUS, ST_ISRF, ST_DONE };
static const uint32_t k_expect_cause[N_BOOTS] = { RC_POWER_ON, RC_WATCHDOG, RC_WATCHDOG, RC_FAULT, RC_FAULT };
#define BUS_HOLE 0x30000000u                        /* QEMU 에서 BusFault 나는 빈 주소 (BM-M13 probe) */
extern const char pc_hang[], pc_bus[], pc_isr[];
__attribute__((noinline, noclone)) static uint32_t inject_bus(uint32_t addr) {
    uint32_t v;
    __asm__ volatile(".global pc_bus\npc_bus: ldr %0, [%1]" : "=r"(v) : "r"(addr) : "memory");
    return v;
}

static void report_crumb(const crumb_t *c) {
    int tick_dead = (c->tick == c->kick_tick);      /* 마지막 킥 뒤로 틱이 하나도 안 늘었다 = 틱 ISR 이 죽었다 */
    log_puts("WDOG diag="); log_puts(tick_dead ? "TICK_DEAD" : "MAIN_STUCK");
    log_puts(" boot="); log_dec_w(c->boot, 0);
    log_puts(" since_kick="); log_dec_w(c->tick - c->kick_tick, 0);
    log_puts(" pc="); log_hex(c->pc); log_puts(" excret="); log_hex(c->exc_return); log_puts("\n");
}
static void report_dump(const dump_t *d) {
    log_puts("DUMP v="); log_dec_w(d->version, 0); log_puts(" boot="); log_dec_w(d->boot, 0);
    log_puts(" exc="); log_dec_w(d->exc, 0); log_puts(" excret="); log_hex(d->exc_return);
    log_puts(" pc="); log_hex(d->pc); log_puts(" lr="); log_hex(d->lr); log_puts(" xpsr="); log_hex(d->xpsr);
    log_puts(" cfsr="); log_hex(d->cfsr); log_puts(" hfsr="); log_hex(d->hfsr);
    log_puts(" bfar="); log_hex(d->bfar); log_puts(" crc="); log_hex(d->crc); log_puts("\n");
}

/* ---------------- 마지막 부팅: 자체 검사 (세미호스팅) ---------------- */
#define CHECK(expr) do { total++; if (expr) { pass++; } else { \
    sh_puts("FAIL " __FILE__ ":"); sh_dec(__LINE__); sh_puts("  " #expr "\n"); } } while (0)
static int final_report(void) {
    int pass = 0, total = 0;
    sh_puts("== BM-A11 summary ==\nboot cause     img logged sum   drops kicks tx_bytes busy/total(timer1 counts)\n");
    for (uint32_t b = 0; b < g_scen.boots && b < MAX_BOOTS; ++b) {
        const boot_res_t *r = &g_scen.res[b];
        sh_dec(b + 1u); sh_puts("    "); sh_puts(k_cause[r->cause & 3u]); sh_puts(" ");
        sh_puts(k_img_rc[r->img_rc < 7u ? r->img_rc : 0u]); sh_puts(" "); sh_dec(r->logged); sh_puts(" ");
        sh_dec(r->sum); sh_puts(" "); sh_dec(r->drops); sh_puts(" "); sh_dec(r->kicks); sh_puts(" ");
        sh_dec(r->tx_bytes); sh_puts(" "); sh_dec(r->busy); sh_puts("/"); sh_dec(r->total); sh_puts("\n");
    }
    CHECK(g_scen.boots == N_BOOTS);
    for (uint32_t b = 0; b < N_BOOTS; ++b) {
        const boot_res_t *r = &g_scen.res[b];
        CHECK(r->cause == k_expect_cause[b]);
        CHECK(r->img_rc == IMG_OK);
        CHECK(r->logged == N_SAMPLES);
        CHECK(r->drops == 0u);
        CHECK(r->kicks >= N_SAMPLES - 1u);          /* 틱마다 한 번 (첫 틱 직전 상태 차이로 ±1) */
        CHECK(r->busy * 2u < r->total);             /* 로그 단계 CPU 부하 < 50 % (icount 기준) */
    }
    const crumb_t *c0 = &g_scen.crumbs[0], *c1 = &g_scen.crumbs[1];
    CHECK(g_scen.n_crumbs == 2u);
    CHECK(crumb_valid(c0) && crumb_valid(c1));
    CHECK(c0->boot == 1u && c0->stage == ST_HANG);
    CHECK(c0->tick - c0->kick_tick >= 9u && c0->tick - c0->kick_tick <= 11u);   /* 100 ms / 10 ms 틱 */
    CHECK(c0->pc == (uint32_t)(uintptr_t)pc_hang || c0->exc_return == 0xFFFFFFF1u);
    CHECK(c0->drops >= 1u);                         /* main 이 멈춘 동안 링(8)이 넘쳤다 */
    CHECK(c1->boot == 2u && c1->stage == ST_STALL);
    CHECK(c1->tick == c1->kick_tick);               /* 틱이 죽었다 → since_kick 0 */
    CHECK(c1->exc_return == 0xFFFFFFF9u);           /* main(Thread, MSP) 이 WFI 근처에서 살아 있었다 */
    const dump_t *d0 = &g_scen.dumps[0], *d1 = &g_scen.dumps[1];
    CHECK(g_scen.n_dumps == 2u);
    CHECK(dump_valid(d0) && dump_valid(d1));
    CHECK(d0->boot == 3u && d0->exc == 3u && d0->exc_return == 0xFFFFFFF9u);
    CHECK(d0->cfsr == 0x00008200u && d0->hfsr == 0x40000000u && d0->bfar == BUS_HOLE);
    CHECK(d0->pc == (uint32_t)(uintptr_t)pc_bus);
    CHECK(d1->boot == 4u && d1->exc == 3u && d1->exc_return == 0xFFFFFFF1u);
    CHECK(d1->cfsr == 0x00010000u && d1->hfsr == 0x40000000u && d1->bfar == 0u);
    CHECK(d1->pc == (uint32_t)(uintptr_t)pc_isr);
    CHECK((d1->xpsr & 0x1FFu) == 16u + IRQ_TIMER0);  /* 쌓인 xPSR 의 IPSR = 24 → timer0 ISR 안에서 */
    CHECK(crc32_update(0u, (const uint8_t *)"123456789", 9u) == 0xCBF43926u);
    CHECK((uintptr_t)&g_scen >= (uintptr_t)_snoinit && (uintptr_t)(&g_scen + 1) <= (uintptr_t)_enoinit);
    CHECK(g_tx_drops == 0u);
    sh_dec((uint32_t)pass); sh_puts("/"); sh_dec((uint32_t)total); sh_puts(" passed\n");
    return pass == total ? 0 : 1;
}

int main(void) {
    uart_init();
    const img_hdr_t *h = (const img_hdr_t *)(const void *)_img_hdr;
    img_rc_t img = img_verify(h, (uint32_t)(uintptr_t)g_vectors, 256u * 1024u, (uint32_t)(uintptr_t)_img_hdr);

    uint32_t cause;
    if (!scen_valid()) {                            /* 콜드 부팅 (QEMU RAM 은 0 으로 시작; 실물은 쓰레기) */
        g_scen.magic = SCEN_MAGIC; g_scen.boots = 0u; g_scen.n_crumbs = 0u; g_scen.n_dumps = 0u;
        g_crumb.magic = 0u; g_dump.magic = 0u;
        cause = RC_POWER_ON;
    } else if (dump_valid(&g_dump)) cause = RC_FAULT;
    else if (crumb_valid(&g_crumb)) cause = RC_WATCHDOG;
    else cause = RC_UNKNOWN;
    uint32_t boot = ++g_scen.boots;
    scen_seal();
    sh_puts("== boot "); sh_dec(boot); sh_puts(" cause="); sh_puts(k_cause[cause]);
    sh_puts(" img="); sh_puts(k_img_rc[img]); sh_puts(" ver="); sh_hex(h->img_version);
    sh_puts(" payload="); sh_dec(h->payload_size); sh_puts(" crc="); sh_hex(h->payload_crc32); sh_puts("\n");
    if (boot > MAX_BOOTS) { sh_puts("boot-loop guard: too many resets\n"); return final_report() | 2; }
    boot_res_t *r = &g_scen.res[boot - 1u];
    r->cause = cause; r->img_rc = img;              /* 나머지 칸은 로그 단계가 끝난 뒤 */

    log_puts("BOOT "); log_dec_w(boot, 0); log_puts(" cause="); log_puts(k_cause[cause]); log_puts("\n");
    if (img != IMG_OK) {
        log_puts("IMG BAD code="); log_puts(k_img_rc[img]); log_puts(" -> SAFE MODE, logger not started\n");
        log_flush();
        return 3;
    }
    log_puts("IMG OK ver="); log_dec_w(h->img_version >> 24, 0); log_puts(".");
    log_dec_w((h->img_version >> 16) & 0xFFu, 0); log_puts("."); log_dec_w(h->img_version & 0xFFFFu, 0);
    log_puts("\n");
    if (cause == RC_FAULT) {
        report_dump(&g_dump);
        if (g_scen.n_dumps < 2u) copy_words(&g_scen.dumps[g_scen.n_dumps++], &g_dump, sizeof g_dump);
        g_dump.magic = 0u;                          /* 소비 → 무효화 */
    }
    if (cause == RC_WATCHDOG) {
        report_crumb(&g_crumb);
        if (g_scen.n_crumbs < 2u) copy_words(&g_scen.crumbs[g_scen.n_crumbs++], &g_crumb, sizeof g_crumb);
    }
    g_crumb.magic = 0u;
    scen_seal();

    g_stage = ST_LOG;
    timers_start();
    wdog_start();
    run_loop(N_SAMPLES);
    log_puts("SUM "); log_dec_w(boot, 0); log_puts(" n="); log_dec_w(g_logged, 0);
    log_puts(" sum="); log_dec_w(g_sum, 0); log_puts(" drops="); log_dec_w(g_drops, 0); log_puts("\n");

    r->logged = g_logged; r->sum = g_sum; r->drops = g_drops;
    r->kicks = g_kicks; r->busy = g_busy; r->total = g_total;
    uint32_t stage = boot <= N_BOOTS ? k_plan[boot - 1u] : ST_DONE;
    static const char *const k_inject[] = { "", "main-hang", "tick-stall", "bus-fault", "isr-fault", "" };
    if (stage != ST_DONE) { log_puts("INJECT "); log_puts(k_inject[stage]); log_puts("\n"); }
    else log_puts("DONE boots=5\n");
    log_flush();
    r->tx_bytes = g_tx_bytes;
    scen_seal();
    sh_puts("  logged="); sh_dec(g_logged); sh_puts(" sum="); sh_dec(g_sum); sh_puts(" busy/total=");
    sh_dec(g_busy); sh_puts("/"); sh_dec(g_total); sh_puts(" tx_bytes="); sh_dec(g_tx_bytes);
    sh_puts(" -> stage "); sh_puts(stage == ST_DONE ? "DONE" : k_inject[stage]); sh_puts("\n");

    g_stage_tick0 = g_isr_ticks;
    g_stage = stage;
    switch (stage) {
    case ST_HANG:                                   /* main 이 무한 루프에 갇힘. 인터럽트는 계속 돈다 */
        __asm__ volatile(".global pc_hang\npc_hang: b .");
        break;
    case ST_STALL:                                  /* 누군가 틱 IRQ 를 꺼 버림. main 은 계속 돈다 */
        NVIC_ICER0 = 1u << IRQ_TIMER0;
        run_loop(0xFFFFFFFFu);
        break;
    case ST_BUS:
        (void)inject_bus(BUS_HOLE);
        break;
    case ST_ISRF:
        g_inject_isr_fault = 1u;
        run_loop(0xFFFFFFFFu);
        break;
    default:
        wdog_kick();
        return final_report();
    }
    sh_puts("FAIL: injected stage returned\n");
    return 1;
}

__attribute__((noreturn)) void Reset_Handler(void) {
    const uint32_t *src = _sidata;
    for (uint32_t *dst = _sdata; dst < _edata;) *dst++ = *src++;
    for (uint32_t *dst = _sbss; dst < _ebss;) *dst++ = 0u;      /* .noinit 은 건드리지 않는다 */
    sh_exit(main());
}
