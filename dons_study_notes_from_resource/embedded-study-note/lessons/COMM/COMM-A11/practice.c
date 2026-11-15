/* COMM-A11 추가 연습 — 한 번에 검증
 *   연습 1: 협력형 루프의 워치독 감시자 — "살아 있음" 체크인 vs "진전" 체크인 (NOR 장애로 MCU 가 리셋되면 안 된다)
 *   연습 2: 누수 버킷(leaky bucket) 건강 판정 — 연속 실패 규칙이 놓치는 간헐 에러를 잡는다 (+ 히스테리시스)
 *   연습 3: 부팅 때 NOR 로그 훑기 → 다음 쓰기 위치와 다음 id 복원 (회전, 태운 슬롯, 찢어진 마지막 쓰기)
 *   연습 4: 재초기화 백오프의 회복 지연 상한 — 닫힌 식 vs 전수 시뮬레이션
 * 빌드: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)
static uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }

/* ================= 연습 1: 워치독 감시자 ================= */
enum { T_SENSOR, T_LOGGER, T_TLM, NTASK };
#define HW_WDT_TIMEOUT 200u      /* tick: 독립 워치독 (IWDG 같은 것) — 이 시간 안에 kick 이 없으면 리셋 */
typedef struct {
    uint32_t last_checkin[NTASK], deadline[NTASK];
    uint32_t last_kick, resets, reset_at;
} Sup;
static void sup_init(Sup *s, uint32_t now) {
    memset(s, 0, sizeof *s);
    for (int k = 0; k < NTASK; k++) { s->last_checkin[k] = now; s->deadline[k] = 50; }
    s->last_kick = now;
}
static void sup_checkin(Sup *s, int task, uint32_t now) { s->last_checkin[task] = now; }
/* 메인 루프 끝에서 매 tick: 모든 태스크가 마감 안에 체크인했을 때만 kick */
static void sup_tick(Sup *s, uint32_t now) {
    bool all = true;
    for (int k = 0; k < NTASK; k++) if (now - s->last_checkin[k] > s->deadline[k]) all = false;
    if (all) s->last_kick = now;
}
/* 하드웨어 워치독: 루프와 상관없이 시간이 흐른다 */
static bool hw_wdt_expired(Sup *s, uint32_t now) {
    if (now - s->last_kick < HW_WDT_TIMEOUT) return false;
    s->resets++; s->reset_at = now;
    return true;
}
typedef enum { POLICY_PROGRESS, POLICY_ALIVE } CheckinPolicy;
/* 시나리오: hang_at 부터 로거가 while(WIP){} 로 영원히 돈다 (루프 전체 정지),
 *           nor_out = [a, b) 동안 NOR 가 바쁨 — 올바른 로거는 타임아웃/백오프로 처리하며 매 tick 돌려준다 */
static uint32_t run_wdt(CheckinPolicy pol, uint32_t hang_at, uint32_t nor_a, uint32_t nor_b, uint32_t T, Sup *s) {
    sup_init(s, 0);
    for (uint32_t now = 0; now < T; now++) {
        if (now >= hang_at) {                                 /* 루프가 멈춤: 감시자도 못 돈다, HW 만 */
            if (hw_wdt_expired(s, now)) return now;
            continue;
        }
        sup_checkin(s, T_SENSOR, now);
        bool nor_busy = now >= nor_a && now < nor_b;
        bool logger_progress = !nor_busy;                     /* 바쁘면 진전이 없다 (기다리는 중) */
        if (pol == POLICY_ALIVE || logger_progress) sup_checkin(s, T_LOGGER, now);
        sup_checkin(s, T_TLM, now);
        sup_tick(s, now);
        if (hw_wdt_expired(s, now)) return now;
    }
    return 0;
}
static void test_p1_watchdog(void) {
    printf("-- 연습 1: watchdog liveness vs progress --\n");
    Sup s;
    CHECK(run_wdt(POLICY_ALIVE, 100000, 0, 0, 5000, &s) == 0 && s.resets == 0);
    uint32_t r = run_wdt(POLICY_ALIVE, 1234, 0, 0, 5000, &s);
    printf("hang at 1234 -> reset at %u (last kick %u + %u)\n", r, s.last_kick, HW_WDT_TIMEOUT);
    CHECK(r == 1233 + HW_WDT_TIMEOUT && s.resets == 1);       /* 마지막 kick = 1233 */
    uint32_t rp = run_wdt(POLICY_PROGRESS, 100000, 1000, 1600, 5000, &s);
    printf("NOR busy [1000,1600): progress-checkin resets at %u, ", rp);
    uint32_t ra = run_wdt(POLICY_ALIVE, 100000, 1000, 1600, 5000, &s);
    printf("alive-checkin resets at %u (0 = never)\n", ra);
    CHECK(rp == 999 + 50 + HW_WDT_TIMEOUT);                   /* 마지막 진전 999 + 마감 50 = 마지막 kick 1049 → +200 */
    CHECK(ra == 0);
}

/* ================= 연습 2: 누수 버킷 건강 판정 ================= */
typedef enum { H_OK, H_DEGRADED, H_FAULT } HState;
#define LB_W      4u     /* 에러 하나 = +4, 성공 하나 = −1 → 에러율 1/(W+1) = 20% 넘으면 계속 오른다 */
#define LB_FAULT  12u    /* 이 이상이면 FAULT */
#define LB_CLEAR  4u     /* FAULT 는 이 이하로 내려와야 DEGRADED (히스테리시스) */
#define LB_MAX    32u
typedef struct { uint32_t level; HState st; uint32_t faults; } Bucket;
static void lb_update(Bucket *b, bool ok) {
    if (ok) { if (b->level) b->level--; }
    else b->level = b->level + LB_W > LB_MAX ? LB_MAX : b->level + LB_W;
    HState old = b->st;
    if (b->level >= LB_FAULT) b->st = H_FAULT;
    else if (b->st == H_FAULT && b->level > LB_CLEAR) b->st = H_FAULT;
    else b->st = b->level ? H_DEGRADED : H_OK;
    if (b->st == H_FAULT && old != H_FAULT) b->faults++;
}
/* 본문 규칙 (연속 실패 3번이면 FAULT, 연속 성공 2번이면 OK) 의 최소판 — 비교용 */
typedef struct { uint8_t err_run, ok_run; HState st; uint32_t faults; } Consec;
static void cs_update(Consec *c, bool ok) {
    HState old = c->st;
    if (ok) { c->err_run = 0; if (c->st != H_OK && ++c->ok_run >= 2) c->st = H_OK; }
    else { c->ok_run = 0; c->err_run++; HState w = c->err_run >= 3 ? H_FAULT : H_DEGRADED; if (w > c->st) c->st = w; }
    if (c->st == H_FAULT && old != H_FAULT) c->faults++;
}
static void test_p2_bucket(void) {
    printf("-- 연습 2: leaky bucket vs consecutive-run health --\n");
    Bucket b = { 0, H_OK, 0 };
    Consec c = { 0, 0, H_OK, 0 };
    int first_fault = -1;
    for (int i = 0; i < 200; i++) {                           /* 에러, 성공, 에러, 성공 ... (50 %) */
        bool ok = i & 1;
        lb_update(&b, ok); cs_update(&c, ok);
        if (b.st == H_FAULT && first_fault < 0) first_fault = i;
    }
    printf("alternating 50%%: bucket FAULT at event %d (level %u), consecutive-rule faults=%u state=%d\n",
           first_fault, b.level, c.faults, (int)c.st);
    CHECK(first_fault == 6 && c.faults == 0);                 /* 4,3,7,6,10,9,13 → 7번째 사건(인덱스 6) */
    Bucket b2 = { 0, H_OK, 0 };
    uint32_t max_level = 0;
    for (int i = 0; i < 1000; i++) { lb_update(&b2, i % 10 != 0); if (b2.level > max_level) max_level = b2.level; }
    CHECK(b2.faults == 0 && max_level == LB_W && b2.st == H_OK);   /* 10 % 에러는 흡수 */
    Bucket b3 = { 0, H_OK, 0 };
    for (int i = 0; i < 3; i++) lb_update(&b3, false);        /* 12 → FAULT */
    int steps = 0;
    while (b3.st == H_FAULT && steps < 100) { lb_update(&b3, true); steps++; }
    CHECK(b3.faults == 1 && steps == (int)(LB_FAULT - LB_CLEAR) && b3.st == H_DEGRADED);   /* 히스테리시스 */
    uint32_t seed = 0xA11u, flaps_b = 0, flaps_c = 0;         /* 25 % 무작위: 버킷은 FAULT 에 머물고, 연속 규칙은 들락날락 */
    Bucket b4 = { 0, H_OK, 0 };
    Consec c4 = { 0, 0, H_OK, 0 };
    for (int i = 0; i < 5000; i++) {
        bool ok = xs32(&seed) % 4 != 0;
        HState pb = b4.st, pc = c4.st;
        lb_update(&b4, ok); cs_update(&c4, ok);
        if (pb != b4.st) flaps_b++;
        if (pc != c4.st) flaps_c++;
    }
    printf("random 25%% errors: state changes bucket=%u consecutive=%u (bucket faults=%u, consecutive faults=%u)\n",
           flaps_b, flaps_c, b4.faults, c4.faults);
    CHECK(b4.faults >= 1 && flaps_b < flaps_c);
}

/* ================= 연습 3: 부팅 때 NOR 로그 훑기 ================= */
#define NOR_SECTOR 4096u
#define NOR_NSECT  4u
#define REC_SIZE   20u
#define SLOTS      (NOR_SECTOR / REC_SIZE)
static uint8_t g_img[NOR_SECTOR * NOR_NSECT];
static uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t c = 0xFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= (uint16_t)(p[i] << 8);
        for (int k = 0; k < 8; k++) c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
    }
    return c;
}
static void rec_write(uint32_t sector, uint32_t slot, uint32_t id, size_t keep) {   /* 본문 레코드 형식, keep = 앞 몇 바이트만 */
    uint8_t r[REC_SIZE];
    memset(r, 0xFF, sizeof r);
    r[0] = 0xA5; r[1] = (uint8_t)(id & 1);
    r[2] = (uint8_t)id; r[3] = (uint8_t)(id >> 8); r[4] = (uint8_t)(id >> 16); r[5] = (uint8_t)(id >> 24);
    uint16_t c = crc16(r, REC_SIZE - 2);
    r[18] = (uint8_t)(c >> 8); r[19] = (uint8_t)c;
    uint8_t *dst = &g_img[sector * NOR_SECTOR + slot * REC_SIZE];
    for (size_t i = 0; i < keep && i < REC_SIZE; i++) dst[i] &= r[i];
}
static bool rec_valid(const uint8_t *r, uint32_t *id) {
    if (r[0] != 0xA5 || crc16(r, REC_SIZE - 2) != (uint16_t)(r[18] << 8 | r[19])) return false;
    *id = (uint32_t)r[2] | (uint32_t)r[3] << 8 | (uint32_t)r[4] << 16 | (uint32_t)r[5] << 24;
    return true;
}
static bool slot_blank(const uint8_t *r) { for (uint32_t i = 0; i < REC_SIZE; i++) if (r[i] != 0xFF) return false; return true; }
typedef struct { uint32_t sector, slot, next_id, valid, dirty; bool need_erase, empty; } Resume;
/* 가장 큰 id 레코드 바로 뒤가 쓰기 위치. 그 자리가 비어 있지 않으면(찢어진 쓰기/태운 슬롯) 빈 슬롯까지 건너뛴다.
 * 섹터 끝을 넘으면 다음 섹터, need_erase = 그 섹터가 비어 있지 않음. 레코드가 하나도 없으면 (0,0), id 0. */
static Resume scan_resume(const uint8_t *img) {
    Resume r = { 0, 0, 0, 0, 0, false, true };
    uint32_t best = 0, bs = 0, bl = 0;
    for (uint32_t s = 0; s < NOR_NSECT; s++)
        for (uint32_t l = 0; l < SLOTS; l++) {
            const uint8_t *p = &img[s * NOR_SECTOR + l * REC_SIZE];
            uint32_t id;
            if (rec_valid(p, &id)) {
                r.valid++;
                if (r.empty || id > best) { best = id; bs = s; bl = l; r.empty = false; }
            } else if (!slot_blank(p)) r.dirty++;
        }
    if (r.empty) { r.need_erase = !slot_blank(&img[0]); return r; }
    r.next_id = best + 1;
    uint32_t s = bs, l = bl + 1;
    while (l < SLOTS && !slot_blank(&img[s * NOR_SECTOR + l * REC_SIZE])) l++;
    if (l >= SLOTS) {
        s = (s + 1) % NOR_NSECT; l = 0;
        bool blank = true;
        for (uint32_t i = 0; i < NOR_SECTOR && blank; i++) if (img[s * NOR_SECTOR + i] != 0xFF) blank = false;
        r.need_erase = !blank;
    }
    r.sector = s; r.slot = l;
    return r;
}
static void test_p3_scan(void) {
    printf("-- 연습 3: boot scan -> resume position --\n");
    memset(g_img, 0xFF, sizeof g_img);
    Resume r = scan_resume(g_img);
    CHECK(r.empty && r.sector == 0 && r.slot == 0 && r.next_id == 0 && !r.need_erase);
    for (uint32_t i = 0; i < 50; i++) rec_write(0, i, i, REC_SIZE);
    r = scan_resume(g_img);
    CHECK(!r.empty && r.sector == 0 && r.slot == 50 && r.next_id == 50 && r.valid == 50);
    rec_write(0, 50, 50, 9);                                  /* 전원이 끊긴 찢어진 쓰기 */
    rec_write(0, 51, 50, REC_SIZE);                           /* 같은 샘플을 다음 슬롯에 다시 씀 (본문 burn 경로) */
    r = scan_resume(g_img);
    CHECK(r.sector == 0 && r.slot == 52 && r.next_id == 51 && r.dirty == 1);
    rec_write(0, 52, 51, 9);                                  /* 마지막이 찢어짐: 그 슬롯은 건너뛴다 */
    r = scan_resume(g_img);
    CHECK(r.slot == 53 && r.next_id == 51);
    /* 회전: 섹터 0..3 을 다 쓰고 섹터 0 을 지우고 다시 쓰는 중 */
    memset(g_img, 0xFF, sizeof g_img);
    uint32_t id = 0;
    for (uint32_t s = 0; s < NOR_NSECT; s++) for (uint32_t l = 0; l < SLOTS; l++) rec_write(s, l, id++, REC_SIZE);
    r = scan_resume(g_img);
    CHECK(r.sector == 0 && r.slot == 0 && r.need_erase && r.next_id == id);   /* 꽉 참 → 가장 오래된 섹터 0 을 지워야 */
    memset(g_img, 0xFF, NOR_SECTOR);
    for (uint32_t l = 0; l < 17; l++) rec_write(0, l, id++, REC_SIZE);
    r = scan_resume(g_img);
    CHECK(r.sector == 0 && r.slot == 17 && r.next_id == id && !r.need_erase);
    /* 무작위: 위치 하나를 정해 거기까지 쓰고 훑으면 그 위치가 나온다 */
    uint32_t seed = 0x5CA9u, bad = 0;
    for (int t = 0; t < 300; t++) {
        memset(g_img, 0xFF, sizeof g_img);
        uint32_t n = 1 + xs32(&seed) % (SLOTS * NOR_NSECT - 1), base = xs32(&seed) % 100000u;
        for (uint32_t k = 0; k < n; k++) rec_write(k / SLOTS, k % SLOTS, base + k, REC_SIZE);
        Resume q = scan_resume(g_img);
        if (q.next_id != base + n || q.sector != n / SLOTS || q.slot != n % SLOTS) bad++;
    }
    CHECK(bad == 0);
}

/* ================= 연습 4: 백오프 회복 지연 상한 ================= */
/* 본문 규칙: t0 에 실패 → 시도 a1 = t0 + P, 이후 a_{k+1} = a_k + min(2^k·P, C·P).
 * 센서가 t_end 에 돌아오면 첫 a_k ≥ t_end 에서 성공. 지연 = a_k − t_end. */
static uint32_t first_attempt_at_or_after(uint32_t t0, uint32_t t_end, uint32_t P, uint32_t C) {
    uint32_t a = t0 + P, step = P;
    while (a < t_end) { step = step * 2 > C * P ? C * P : step * 2; a += step; }
    return a;
}
/* 시뮬레이션: 슬롯마다 next_init 을 보는 본문 코드 그대로의 축약판 */
static uint32_t simulate(uint32_t t0, uint32_t t_end, uint32_t P, uint32_t C, uint32_t *attempts) {
    uint32_t next_init = t0 + P, backoff = 2 * P;
    *attempts = 0;
    for (uint32_t t = t0 + P; t < t_end + 2 * C * P; t += P) {
        if (t < next_init) continue;
        (*attempts)++;
        if (t >= t_end) return t;
        next_init = t + backoff;
        backoff = backoff * 2 > C * P ? C * P : backoff * 2;
    }
    return 0;
}
static void test_p4_backoff(void) {
    printf("-- 연습 4: re-init backoff recovery bound --\n");
    uint32_t att;
    CHECK(first_attempt_at_or_after(305, 2300, 50, 8) == 2655 && simulate(305, 2300, 50, 8, &att) == 2655 && att == 8);  /* 본문 실측 */
    const uint32_t Ps[2] = { 20, 50 };
    for (int i = 0; i < 2; i++) {
        uint32_t P = Ps[i], C = 8, worst = 0, mism = 0, worst_L = 0;
        for (uint32_t L = 1; L <= 5000; L++) {
            uint32_t a = first_attempt_at_or_after(1000, 1000 + L, P, C), b = simulate(1000, 1000 + L, P, C, &att);
            if (a != b) mism++;
            if (a - (1000 + L) > worst) { worst = a - (1000 + L); worst_L = L; }
        }
        printf("P=%u C=%u: fault lengths 1..5000 -> worst recovery delay %u ticks (L=%u), bound C*P-1 = %u\n",
               P, C, worst, worst_L, C * P - 1);
        CHECK(mism == 0 && worst == C * P - 1);
    }
}

int main(void) {
    test_p1_watchdog();
    test_p2_bucket();
    test_p3_scan();
    test_p4_backoff();
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
