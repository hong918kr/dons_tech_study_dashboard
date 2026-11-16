/* BM-A11 추가 연습 — 호스트 C (맥에서 바로; VM 에선 run_qemu.sh practice)
 *   연습 1: 이미지 헤더 검증(펌웨어와 같은 규칙)의 호스트 쌍둥이 — 모든 단일 비트 플립, 크기 거짓말, 잘린 슬롯
 *   연습 2: .noinit 덤프 유효성 — 쓰레기 200,000 개 오수락 0, "magic 먼저 쓰기" 의 찢어진 기록
 *   연습 3: log_kick 경쟁 — QEMU 처럼 "DATA 쓰기 즉시 TX 인터럽트" 모델에서 세 가지 순서 비교
 *   연습 4: 워치독 킥 정책 3 가지 × 장애 4 가지 표
 *   연습 5: 예산 계산 — 워치독 LOAD, UART 대역폭, CPU 부하 (QEMU icount 실측 상수)
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-A11_pr && /tmp/BM-A11_pr
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) g_pass++; else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static uint32_t xs_state = 0xA11C0DEu;
static uint32_t xorshift32(void) {
    uint32_t x = xs_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return xs_state = x;
}
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n) {   /* 펌웨어와 같은 코드 */
    crc = ~crc;
    while (n--) {
        crc ^= *p++;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

/* ===================== 연습 1: 이미지 헤더 (BM-A04 헤더 축약판) ===================== */
typedef struct {
    uint32_t magic; uint16_t hdr_version, hdr_size; uint32_t img_version, payload_size, load_addr, flags,
    payload_crc32; uint8_t sha256[32]; uint32_t hdr_crc32;
} img_hdr_t;
_Static_assert(sizeof(img_hdr_t) == 64, "64 B header");
#define IMG_MAGIC 0x474D4942u
typedef enum { IMG_OK = 0, IMG_BAD_MAGIC, IMG_BAD_HDR_VERSION, IMG_BAD_HDR_CRC, IMG_BAD_SIZE, IMG_BAD_LOAD_ADDR,
               IMG_BAD_CRC } img_rc_t;
/* 호스트 쌍둥이: payload 를 "주소" 대신 slot 메모리 안의 오프셋으로 읽는다. 레이아웃 = [payload][header]. */
static img_rc_t img_verify_buf(const uint8_t *slot, uint32_t slot_base, uint32_t slot_len, const img_hdr_t *h,
                               uint32_t expect_size) {
    if (h->magic != IMG_MAGIC) return IMG_BAD_MAGIC;
    if (h->hdr_version != 1u || h->hdr_size != sizeof(img_hdr_t)) return IMG_BAD_HDR_VERSION;
    if (crc32_update(0u, (const uint8_t *)h, (uint32_t)offsetof(img_hdr_t, hdr_crc32)) != h->hdr_crc32)
        return IMG_BAD_HDR_CRC;
    if (slot_len < sizeof(img_hdr_t) || h->payload_size == 0u ||
        h->payload_size > slot_len - (uint32_t)sizeof(img_hdr_t) || h->payload_size != expect_size)
        return IMG_BAD_SIZE;                     /* 뺄셈은 slot_len >= 64 를 확인한 뒤에만: 언더플로 없음 */
    if (h->load_addr != slot_base) return IMG_BAD_LOAD_ADDR;
    if (crc32_update(0u, slot, h->payload_size) != h->payload_crc32) return IMG_BAD_CRC;
    return IMG_OK;
}
#define PAY 448u
static uint8_t g_slot[PAY + 64u];
static void make_image(uint32_t payload_size_field) {              /* C 판 mkimg */
    for (uint32_t i = 0; i < PAY; ++i) g_slot[i] = (uint8_t)xorshift32();
    img_hdr_t h;
    memset(&h, 0, sizeof h);
    h.magic = IMG_MAGIC; h.hdr_version = 1u; h.hdr_size = 64u; h.img_version = 0x01010000u;
    h.payload_size = payload_size_field; h.load_addr = 0u; h.flags = 0u;
    h.payload_crc32 = crc32_update(0u, g_slot, PAY);
    h.hdr_crc32 = crc32_update(0u, (const uint8_t *)&h, 60u);
    memcpy(g_slot + PAY, &h, 64u);
}
static img_rc_t verify_slot(void) {
    img_hdr_t h;
    memcpy(&h, g_slot + PAY, sizeof h);
    return img_verify_buf(g_slot, 0u, sizeof g_slot, &h, PAY);
}
static void ex1_image(void) {
    make_image(PAY);
    CHECK(verify_slot() == IMG_OK);
    uint32_t wrong_code = 0, accepted = 0;
    for (uint32_t bit = 0; bit < (PAY + 64u) * 8u; ++bit) {         /* 모든 단일 비트 플립 (3,584 개) */
        g_slot[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
        img_rc_t rc = verify_slot();
        uint32_t off = bit / 8u;
        img_rc_t want = off < PAY ? IMG_BAD_CRC
                      : off < PAY + 4u ? IMG_BAD_MAGIC
                      : off < PAY + 8u ? IMG_BAD_HDR_VERSION : IMG_BAD_HDR_CRC;
        if (rc == IMG_OK) accepted++;
        if (rc != want) wrong_code++;
        g_slot[bit / 8u] ^= (uint8_t)(1u << (bit % 8u));
    }
    printf("ex1: %u single-bit flips -> accepted %u, wrong error code %u\n", (PAY + 64u) * 8u, accepted, wrong_code);
    CHECK(accepted == 0u);
    CHECK(wrong_code == 0u);
    /* 헤더 CRC 까지 맞춘 거짓 크기 — 정직하게 만든 "나쁜 헤더" 는 크기 검사가 잡는다 */
    static const uint32_t lies[] = { 0u, PAY - 4u, PAY + 4u, 0x7FFFFFFFu, 0xFFFFFFC0u, 0xFFFFFFFFu };
    for (size_t i = 0; i < sizeof lies / sizeof lies[0]; ++i) {
        make_image(lies[i]);
        CHECK(verify_slot() == IMG_BAD_SIZE);
    }
    /* 슬롯이 헤더보다 작다고 주장 → 언더플로 없이 BAD_SIZE */
    make_image(PAY);
    img_hdr_t h;
    memcpy(&h, g_slot + PAY, sizeof h);
    CHECK(img_verify_buf(g_slot, 0u, 40u, &h, PAY) == IMG_BAD_SIZE);
    CHECK(img_verify_buf(g_slot, 0x4000u, sizeof g_slot, &h, PAY) == IMG_BAD_LOAD_ADDR);
    CHECK(crc32_update(0u, (const uint8_t *)"123456789", 9u) == 0xCBF43926u);
}

/* ===================== 연습 2: 덤프 유효성 (펌웨어 dump_t 와 같은 모양) ===================== */
typedef struct {
    uint32_t magic, version, boot, exc, exc_return, r0, r1, r2, r3, r12, lr, pc, xpsr, cfsr, hfsr, mmfar, bfar, crc;
} dump_t;
#define DUMP_MAGIC 0xD0D0FA17u
static uint32_t dump_crc(const dump_t *d) {
    return crc32_update(0u, (const uint8_t *)&d->version, (uint32_t)(offsetof(dump_t, crc) - offsetof(dump_t, version)));
}
static int dump_valid(const dump_t *d) { return d->magic == DUMP_MAGIC && d->version == 1u && d->crc == dump_crc(d); }
static void ex2_dump(void) {
    dump_t d;
    uint32_t acc_garbage = 0, acc_magic = 0;
    for (uint32_t n = 0; n < 200000u; ++n) {                        /* 실물 콜드 부팅의 SRAM = 쓰레기 */
        uint32_t *w = (uint32_t *)&d;
        for (size_t i = 0; i < sizeof d / 4u; ++i) w[i] = xorshift32();
        acc_garbage += (uint32_t)dump_valid(&d);
        d.magic = DUMP_MAGIC; d.version = 1u;                       /* 최악: magic·version 은 우연히 맞았다 */
        acc_magic += (uint32_t)dump_valid(&d);
    }
    printf("ex2: 200000 garbage fills -> false accept %u; with magic+version forced -> %u\n", acc_garbage, acc_magic);
    CHECK(acc_garbage == 0u);
    CHECK(acc_magic == 0u);
    /* 정상 기록 / 옛 버전 / 찢어진 기록 */
    memset(&d, 0, sizeof d);
    d.version = 1u; d.boot = 3u; d.exc = 3u; d.exc_return = 0xFFFFFFF9u; d.pc = 0x1ECu; d.cfsr = 0x8200u;
    d.hfsr = 0x40000000u; d.bfar = 0x30000000u; d.crc = dump_crc(&d); d.magic = DUMP_MAGIC;
    CHECK(dump_valid(&d));
    dump_t stale = d;
    stale.version = 0u; stale.crc = dump_crc(&stale);                /* CRC 는 맞지만 옛 형식 */
    CHECK(!dump_valid(&stale));
    /* 찢어진 쓰기: 이전 덤프(유효)가 남아 있는 자리에 새 덤프를 쓰다가 k 번째 쓰기 뒤에 끊김 (중첩 fault / 전원).
     * 좋은 순서 = magic=0 → 내용 17 워드(crc 포함) → magic,  나쁜 순서 = 내용만 덮어쓰기 (magic 은 이전 값 그대로).
     * "잘못 수락" = 유효로 보이는데 이전 것도 새 것도 아닌 섞인 기록. CRC 있는 검사 / magic 만 보는 검사 둘 다 센다. */
    dump_t old = d;
    old.boot = 2u; old.pc = 0x111u; old.cfsr = 0x10000u; old.crc = dump_crc(&old); old.magic = DUMP_MAGIC;
    uint32_t mix[2][2] = { { 0, 0 }, { 0, 0 } };                    /* [order good/bad][check crc/magic-only] */
    for (int order = 0; order < 2; ++order) {
        uint32_t nw = order == 0 ? 19u : 17u;
        for (uint32_t k = 0; k < nw; ++k) {                          /* k 번 쓰고 끊김 (k == nw 는 완료) */
            dump_t t = old;
            uint32_t *tw = (uint32_t *)&t;
            const uint32_t *nwds = (const uint32_t *)&d;
            for (uint32_t i = 0; i < k; ++i) {
                if (order == 0) { if (i == 0u) tw[0] = 0u; else if (i <= 17u) tw[i] = nwds[i]; }
                else tw[i + 1u] = nwds[i + 1u];
            }
            int is_old = memcmp(&t, &old, sizeof t) == 0;
            int is_new = memcmp(&t, &d, sizeof t) == 0;
            if (is_old || is_new) continue;
            mix[order][0] += (uint32_t)dump_valid(&t);
            mix[order][1] += (uint32_t)(t.magic == DUMP_MAGIC && t.version == 1u);
        }
    }
    printf("ex2: torn writes accepted as valid (mixed old/new): good order crc=%u magic-only=%u | "
           "no-clear order crc=%u magic-only=%u\n", mix[0][0], mix[0][1], mix[1][0], mix[1][1]);
    CHECK(mix[0][0] == 0u && mix[0][1] == 0u);                      /* magic 을 먼저 지우면 CRC 없이도 안전 */
    CHECK(mix[1][0] == 0u);                                         /* CRC 가 섞인 기록을 잡는다 */
    CHECK(mix[1][1] > 0u);                                          /* magic 만 보면 섞인 기록을 믿는다 */
}

/* ===================== 연습 3: log_kick 경쟁 (QEMU: DATA 쓰기 = 즉시 TX 완료 = 즉시 인터럽트) ===================== */
#define RN 16u
typedef struct { uint8_t ring[RN]; uint32_t head, tail, busy, primask, pending; uint8_t out[256]; uint32_t nout; } txm_t;
static void m_isr(txm_t *m);
static void m_data_write(txm_t *m, uint8_t b) {
    m->out[m->nout++ & 255u] = b;
    m->pending = 1u;                                                /* CMSDK: TXFULL 1→0 → INTSTATUS.TX → NVIC */
    if (!m->primask) m_isr(m);                                      /* 마스크가 없으면 바로 다음 명령 경계에서 */
}
static void m_isr(txm_t *m) {
    while (m->pending) {
        m->pending = 0u;
        if (m->tail != m->head) { uint8_t b = m->ring[m->tail % RN]; m->tail++; m_data_write(m, b); }
        else m->busy = 0u;
    }
}
static void m_kick(txm_t *m, int variant) {
    if (variant == 0) {                                             /* 정답: PRIMASK + 꼬리 먼저 */
        m->primask = 1u;
        if (!m->busy && m->tail != m->head) { uint32_t t = m->tail; m->busy = 1u; m->tail = t + 1u; m_data_write(m, m->ring[t % RN]); }
        m->primask = 0u;
        m_isr(m);                                                   /* 마스크 해제 → 펜딩 처리 */
    } else if (variant == 1) {                                      /* 마스크 없음, 그러나 꼬리 먼저 */
        if (!m->busy && m->tail != m->head) { uint32_t t = m->tail; m->busy = 1u; m->tail = t + 1u; m_data_write(m, m->ring[t % RN]); }
    } else {                                                        /* 마스크 없음 + DATA 먼저, 꼬리 나중 → 중복 */
        if (!m->busy && m->tail != m->head) { uint32_t t = m->tail; m->busy = 1u; m_data_write(m, m->ring[t % RN]); m->tail = t + 1u; }
    }
}
static int m_send(int variant, const char *msg, uint32_t start) {  /* 1 = 출력이 입력과 같다 */
    txm_t m;
    memset(&m, 0, sizeof m);
    m.head = m.tail = start;
    uint32_t n = (uint32_t)strlen(msg);
    for (uint32_t i = 0; i < n; ++i) { m.ring[m.head % RN] = (uint8_t)msg[i]; m.head++; }
    m_kick(&m, variant);
    return m.nout == n && memcmp(m.out, msg, n) == 0 && m.busy == 0u && m.tail == m.head;
}
static void ex3_race(void) {
    static const char *const msgs[] = { "A", "OK\n", "S 1 01 t=0001\n", "IMG OK ver=1.1." };
    uint32_t good[3] = { 0, 0, 0 }, cases = 0;
    for (size_t k = 0; k < 4; ++k)
        for (uint32_t start = 0xFFFFFFF0u; start != 0x10u; ++start) {  /* 인덱스 랩어라운드 포함 */
            cases++;
            for (int v = 0; v < 3; ++v) good[v] += (uint32_t)m_send(v, msgs[k], start);
        }
    printf("ex3: %u cases: correct output  primask+tail-first %u, tail-first %u, data-first %u\n",
           cases, good[0], good[1], good[2]);
    CHECK(good[0] == cases);
    CHECK(good[1] == cases);
    CHECK(good[2] == 0u);
    txm_t m;                                                        /* data-first 가 실제로 뭘 내보내나 */
    memset(&m, 0, sizeof m);
    memcpy(m.ring, "BOOT", 4); m.head = 4u;
    m_kick(&m, 2);
    m.out[m.nout & 255u] = 0;
    printf("ex3: data-first sends \"%s\" for \"BOOT\" and leaves tail=%u head=%u busy=%u\n",
           (char *)m.out, m.tail, m.head, m.busy);
    CHECK(m.nout == 5u && memcmp(m.out, "BBOOT", 5) == 0);
    CHECK(m.tail == 1u && m.head == 4u && m.busy == 0u);            /* main 의 tail = t+1 이 ISR 의 진행을 되감았다 */
    m.ring[m.head % RN] = '1'; m.head++;                            /* 다음 log_puts("1") */
    m_kick(&m, 2);
    m.out[m.nout & 255u] = 0;
    printf("ex3: next kick after pushing \"1\" -> total output \"%s\" (QEMU 실측 첫 줄: \"BBOOT OOOT 1OOT 1 ...\")\n",
           (char *)m.out);
    CHECK(m.nout == 10u && memcmp(m.out, "BBOOTOOOT1", 10) == 0);
}

/* ===================== 연습 4: 킥 정책 × 장애 ===================== */
enum { P_ISR = 0, P_MAIN_ALWAYS, P_HEARTBEAT };
enum { F_NONE = 0, F_MAIN_STUCK, F_TICK_DEAD, F_MAIN_SLOW };
/* 1 ms 단위 시뮬: 틱 10 ms, 워치독 100 ms 경고 + 100 ms 리셋. 장애는 t = 200 ms 에 시작. 1000 ms 안에 리셋 나나? */
static int sim_policy(int policy, int fault) {
    uint32_t wd = 100u, stage = 0u, tick = 0u, kick_tick = 0u, backlog = 0u, main_budget = 0u;
    for (uint32_t ms = 1; ms <= 1000u; ++ms) {
        int faulty = ms >= 200u;
        int tick_alive = !(faulty && fault == F_TICK_DEAD);
        int main_alive = !(faulty && fault == F_MAIN_STUCK);
        if (ms % 10u == 0u && tick_alive) {
            tick++; backlog++;
            if (policy == P_ISR) { wd = 100u; stage = 0u; }
        }
        if (main_alive) {
            int slow = faulty && fault == F_MAIN_SLOW;
            if (backlog && (!slow || (++main_budget % 20u) == 0u)) backlog--;   /* 느리면 20 ms 에 한 샘플 */
            if (policy == P_MAIN_ALWAYS) { wd = 100u; stage = 0u; }
            if (policy == P_HEARTBEAT && tick != kick_tick && backlog == 0u) { wd = 100u; stage = 0u; kick_tick = tick; }
        }
        if (--wd == 0u) { if (stage++) return 1; wd = 100u; }      /* 두 번째 만료 = 리셋 */
    }
    return 0;
}
static void ex4_policy(void) {
    static const char *const pn[] = { "kick in tick ISR      ", "kick every main loop  ", "heartbeat + drained   " };
    static const int want[3][4] = { { 0, 0, 1, 0 }, { 0, 1, 0, 0 }, { 0, 1, 1, 1 } };
    printf("ex4: reset within 1 s?   none main_stuck tick_dead main_slow\n");
    for (int p = 0; p < 3; ++p) {
        printf("     %s", pn[p]);
        for (int f = 0; f < 4; ++f) {
            int r = sim_policy(p, f);
            printf("  %-9s", r ? "RESET" : "-");
            CHECK(r == want[p][f]);
        }
        printf("\n");
    }
}

/* ===================== 연습 5: 예산 ===================== */
static uint32_t wdog_load(uint32_t pclk_hz, uint32_t warn_ms) { return (uint32_t)((uint64_t)pclk_hz * warn_ms / 1000u); }
static uint32_t uart_bytes_per_s(uint32_t baud) { return baud / 10u; }              /* 8N1 = 10 비트/바이트 */
static uint32_t load_ppm(uint32_t busy, uint32_t total) { return (uint32_t)((uint64_t)busy * 1000000u / total); }
static void ex5_budget(void) {
    CHECK(wdog_load(25000000u, 100u) == 2500000u);
    CHECK(wdog_load(25000000u, 171798u) == 4294950000u);
    CHECK((uint64_t)25000000u * 171799u / 1000u > 0xFFFFFFFFull);   /* 25 MHz 에서 1 단계 최대 ≈ 171.8 s */
    uint32_t line = 22u;                                            /* "S 1 01 t=0001 v=2011\n" = 21 B (+여유) */
    CHECK(strlen("S 1 01 t=0001 v=2011\n") == 21u);
    CHECK(uart_bytes_per_s(115200u) == 11520u);
    printf("ex5: UART 115200 8N1 = %u B/s; 100 Hz x %u B = %u B/s (%u %%); max rate for this line ~ %u Hz\n",
           uart_bytes_per_s(115200u), line, 100u * line, 100u * line * 100u / uart_bytes_per_s(115200u),
           uart_bytes_per_s(115200u) / line);
    CHECK(uart_bytes_per_s(115200u) / line == 523u);
    /* QEMU icount 실측 (run_qemu.sh solution 의 busy/total): 5974 / 3000489 timer1 카운트 */
    uint32_t ppm = load_ppm(5974u, 3000489u);
    uint32_t instr = 5974u * 40u / 16u;                             /* 1 카운트 = 40 ns, 1 명령 = 16 ns (shift=4) */
    printf("ex5: logging-phase load %u ppm (%u.%02u %%), ~%u instructions over 12 ticks = %u per tick\n",
           ppm, ppm / 10000u, ppm / 100u % 100u, instr, instr / 12u);
    CHECK(ppm == 1991u);
    CHECK(instr / 12u == 1244u);
}

/* 센서 모델 쌍둥이: 펌웨어·logtool.py 와 같은 값 (첫 12 틱 합 = 24677, UART 로그 SUM 줄) */
static uint32_t sensor_value(uint32_t tick) {
    uint32_t x = tick * 2654435761u;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12;
    uint32_t ph = tick % 50u;
    uint32_t tri = ph < 25u ? ph : 50u - ph;
    return 2000u + tri * 8u + (x & 7u);
}

int main(void) {
    ex1_image();
    ex2_dump();
    ex3_race();
    ex4_policy();
    ex5_budget();
    uint32_t sum = 0;
    for (uint32_t t = 1; t <= 12u; ++t) sum += sensor_value(t);
    CHECK(sum == 24677u);
    CHECK(sensor_value(1) == 2011u && sensor_value(12) == 2103u);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
