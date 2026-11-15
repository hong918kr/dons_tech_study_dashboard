/* COMM-M07 실험: fault confinement 가 실제로 무엇을 지켜 주나
 *
 * 1) rule 2 (먼저 본 수신자 REC +8) 를 끄면 누가 먼저 쫓겨나나 — 고장 수신자 R vs 멀쩡한 송신자 T
 * 2) 시간표: 혼자 남은 노드 / 송신부 고장 노드 / 복구 (빈 버스, 꽉 찬 버스) 를 비트레이트별 µs·ms 로
 * 3) babbling idiot: 고장 + 최고 우선순위 노드가 있을 때 정책별 정상 노드의 처리량·대기
 * solution.c 의 모델을 그대로 쓴다 (같은 레슨 안의 #include, main 이름만 바꿈).
 * 빌드: cc -std=c11 -Wall -Wextra -O2 demo_confinement.c -o demo_confinement && ./demo_confinement
 */
#define main comm_m07_solution_main
#include "solution.c"
#undef main

/* 1) 이벤트 수준: 프레임마다 R 이 먼저 보고 active 플래그로 깨뜨린다 (R 이 passive 가 되면 R 혼자만 에러) */
static void ablation(bool rule2) {
    FcNode T = {0}, R = {0}, C = {0};
    int r_pas = 0, t_pas = 0, t_off = 0;
    unsigned tmax = 0;
    for (int fr = 1; fr <= 400; fr++) {
        if (R.state == ST_ACTIVE) {
            fc_rx_error(&R);
            if (rule2) fc_penalty8(&R, false);
            fc_tx_error(&T, ERR_BIT, false, false);
            fc_rx_error(&C);
        } else {
            fc_rx_error(&R);
            fc_tx_ok(&T);
            fc_rx_ok(&C);
        }
        if (T.tec > tmax && T.state != ST_BUSOFF) tmax = T.tec;
        if (!r_pas && R.state == ST_PASSIVE) r_pas = fr;
        if (!t_pas && T.state == ST_PASSIVE) t_pas = fr;
        if (!t_off && T.state == ST_BUSOFF) { t_off = fr; break; }
    }
    char rp[16] = "never", tp[16] = "never", to[16] = "never";
    if (r_pas) snprintf(rp, sizeof rp, "frame %d", r_pas);
    if (t_pas) snprintf(tp, sizeof tp, "frame %d", t_pas);
    if (t_off) snprintf(to, sizeof to, "frame %d", t_off);
    printf("| %s | %s | %u | %s | %s | %u |\n", rule2 ? "on" : "off", rp, tmax, tp, to, (unsigned)R.rec);
}

static uint64_t run_until(Bus *b, int x, CanState st, uint32_t max_slots) {
    while (b->nd[x].fc.state != st && b->slot < max_slots) bus_step(b);
    return b->bits;
}

int main(void) {
    printf("== 1) rule 2 ablation: faulty receiver R (sees an error in every frame), healthy T and C ==\n");
    printf("| rule 2  | R passive at  | T peak TEC  | T passive     | T bus-off     | R REC at end |\n");
    printf("|---|---|---|---|---|---|\n");
    ablation(true);
    ablation(false);

    printf("\n== 2) timeline (bit counts from the bit-level slot model; DLC 8 std frames, real stuff bits) ==\n");
    Bus b;
    bus_init(&b);
    int A = bus_add(&b, "A", 0x100);
    b.nd[A].babble = true;
    uint64_t lone_pas = run_until(&b, A, ST_PASSIVE, 1000);
    bus_init(&b);
    int X = bus_add(&b, "X", 0x010), B = bus_add(&b, "B", 0x100), C = bus_add(&b, "C", 0x200);
    b.nd[X].fault = F_TX_CORRUPT; b.nd[X].babble = true; b.nd[X].fc.auto_recover = true;
    uint64_t x_pas = run_until(&b, X, ST_PASSIVE, 1000);
    uint64_t x_off = run_until(&b, X, ST_BUSOFF, 1000);
    b.nd[X].babble = false; b.nd[X].qn = 0;
    while (b.nd[X].fc.n_recovered == 0 && b.slot < 5000) bus_step(&b);
    uint64_t idle_rec = b.nd[X].recovered_at - b.nd[X].busoff_at;
    b.nd[X].babble = true; b.nd[B].babble = true;
    while (b.nd[X].fc.n_busoff < 2 && b.slot < 5000) bus_step(&b);
    uint64_t off2 = b.nd[X].busoff_at;
    while (b.nd[X].fc.n_recovered < 2 && b.slot < 9000) bus_step(&b);
    uint64_t busy_rec = b.nd[X].recovered_at - off2;
    (void)C;
    printf("bits: lone->passive %llu, broken TX->passive %llu, ->bus-off %llu, recovery idle %llu, busy %llu\n",
           (unsigned long long)lone_pas, (unsigned long long)x_pas, (unsigned long long)x_off,
           (unsigned long long)idle_rec, (unsigned long long)busy_rec);
    printf("| bitrate | lone node -> passive | broken TX -> passive | broken TX -> bus-off | recovery, idle bus | recovery, 100%% bus |\n");
    printf("|---|---|---|---|---|---|\n");
    const uint32_t rates[] = { 125000, 250000, 500000, 1000000 };
    for (int i = 0; i < 4; i++) {
        double us = 1e6 / rates[i];
        printf("| %4u k | %8.1f us | %8.1f us | %8.1f us | %8.1f us | %8.1f us |\n", (unsigned)(rates[i] / 1000),
               lone_pas * us, x_pas * us, x_off * us, idle_rec * us, busy_rec * us);
    }

    printf("\n== 3) babbling idiot X (TX fault, ID 0x010, always pending) + B, C (period 5 slots), 40000 slots @500k ==\n");
    printf("| policy | X bus-offs | X restarts | bus bits used by X | B ok | B max wait | C ok | C max wait | B peak REC | B went passive |\n");
    printf("|---|---|---|---|---|---|---|---|---|---|\n");
    for (int pol = 0; pol < 4; pol++) {
        bus_init(&b);
        X = bus_add(&b, "X", 0x010); B = bus_add(&b, "B", 0x100); C = bus_add(&b, "C", 0x200);
        b.nd[X].fault = pol == 0 ? F_NONE : F_TX_CORRUPT;
        b.nd[X].babble = pol != 0;
        b.nd[X].fc.auto_recover = pol == 1;
        b.nd[B].period = 5; b.nd[C].period = 5;
        Drv dx;
        if (pol == 2) drv_init(&dx, 500000, 100, 1000000);   /* Linux 식: restart-ms 100, 무제한 */
        else if (pol == 3) drv_init(&dx, 500000, 2, 2);      /* 펌웨어 정책: 2 ms 백오프, 2번까지 */
        else drv_init(&dx, 500000, 0, 0);
        uint64_t xbits = 0;
        unsigned bpeak = 0;
        for (int s = 0; s < 40000; s++) {
            uint64_t before = b.bits;
            bus_step(&b);
            if (b.last_tx == X) xbits += b.bits - before;
            drv_poll(&dx, &b.nd[X], b.bits);
            if (b.nd[B].fc.rec > bpeak) bpeak = b.nd[B].fc.rec;
        }
        const char *name[] = { "no X (baseline)", "auto-recover (ABOM-like)", "restart-ms 100, unlimited",
                               "2 ms backoff, max 2 restarts" };
        printf("| %s | %u | %u | %.1f %% | %u | %u | %u | %u | %u | %u |\n", name[pol],
               (unsigned)b.nd[X].fc.n_busoff, (unsigned)dx.restarts, 100.0 * (double)xbits / (double)b.bits,
               (unsigned)b.nd[B].tx_ok, (unsigned)b.nd[B].max_wait, (unsigned)b.nd[C].tx_ok,
               (unsigned)b.nd[C].max_wait, bpeak, (unsigned)b.nd[B].fc.n_passive);
    }
    return 0;
}
