/* demo_bugs.c — 소유권을 어기는 두 가지 버그를 실제 스레드로 재현한다. (solution.c 를 include)
 *   ok          : 올바른 드라이버 + 올바른 앱
 *   torn        : 드라이버 버그 — 다음 버퍼가 없는데 완료를 알리고 HW 는 같은 버퍼에 계속 쓴다
 *   after_qbuf  : 앱 버그 — qbuf 한 뒤에 들고 있던 포인터로 버퍼에 쓴다
 * 빌드: cc -std=c11 -Wall -Wextra -O2 -pthread demo_bugs.c -o demo_bugs
 * TSan: cc -std=c11 -g -O1 -fsanitize=thread -pthread demo_bugs.c -o demo_bugs_tsan
 * 실행: ./demo_bugs <ok|torn|after_qbuf> [frames]
 */
#define CAM_NO_MAIN
#include "solution.c"

enum { M_OK, M_TORN, M_AFTER_QBUF };
static int g_mode;
static unsigned g_frames = 4000;

/* 버그 드라이버의 EOF: inq 가 비었는데도 DONE 으로 내보내고 active 를 그대로 둔다 */
static void buggy_eof(cap_t *c) {
    int x = c->active;
    if (!fifo_empty(&c->inq)) {
        if (x >= 0 && c->buf[x].state == B_ACTIVE) complete_active(c);
        c->active = -1;
        arm_next(c);
        return;
    }
    if (x >= 0 && c->buf[x].state == B_ACTIVE) {
        complete_active(c);
        c->active = x;                 /* BUG: 앱에게 넘겼는데 HW 는 계속 이 버퍼에 쓴다 */
    }
}

static void buggy_frame(cap_t *c) {
    pthread_mutex_lock(&c->lock);
    if (!c->streaming) { pthread_mutex_unlock(&c->lock); return; }
    uint32_t seq = c->hw_seq++;
    c->frames++;
    int idx = c->active;
    if (idx >= 0) c->buf[idx].seq = seq;
    c->dma_busy = idx >= 0;
    audit(c);
    pthread_mutex_unlock(&c->lock);
    if (idx >= 0) for (unsigned i = 0; i < FRAME_WORDS; i++) c->buf[idx].mem[i] = frame_word(seq, i);
    pthread_mutex_lock(&c->lock);
    c->dma_busy = false;
    if (idx >= 0) buggy_eof(c);
    audit(c);
    pthread_cond_broadcast(&c->done_cv);
    pthread_mutex_unlock(&c->lock);
}

typedef struct { cap_t *c; int done; pthread_mutex_t m; } arg_t;

static void *prod(void *a) {
    arg_t *p = a;
    for (unsigned i = 0; i < g_frames; i++) {
        if (g_mode == M_TORN) buggy_frame(p->c); else cap_hw_frame(p->c);
        sleep_ns(10000);
    }
    pthread_mutex_lock(&p->m); p->done = 1; pthread_mutex_unlock(&p->m);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s ok|torn|after_qbuf [frames]\n", argv[0]); return 2; }
    g_mode = !strcmp(argv[1], "torn") ? M_TORN : !strcmp(argv[1], "after_qbuf") ? M_AFTER_QBUF : M_OK;
    if (argc > 2) g_frames = (unsigned)strtoul(argv[2], NULL, 10);
    cap_t c; arg_t a; pthread_t th; cap_info_t info;
    memset(&a, 0, sizeof a);
    a.c = &c; pthread_mutex_init(&a.m, NULL);
    cap_init(&c, POLICY_OVERWRITE_OLDEST, 4);
    for (int i = 0; i < 4; i++) (void)cap_qbuf(&c, i);
    (void)cap_streamon(&c);
    pthread_create(&th, NULL, prod, &a);

    uint64_t delivered = 0, torn = 0, stale_writes = 0;
    int idle = 0;
    for (long budget = 0; budget < 2000000 && idle < 3; budget++) {
        int idx = cap_dqbuf(&c, &info, 2);
        int d;
        pthread_mutex_lock(&a.m); d = a.done; pthread_mutex_unlock(&a.m);
        if (idx < 0) { if (d) idle++; continue; }
        idle = 0;
        uint32_t *p = c.buf[idx].mem;  /* 데모라서 cap_ptr 없이 직접 */
        bool bad = false;
        for (unsigned i = 0; i < FRAME_WORDS / 2; i++) if (p[i] != frame_word(info.seq, i)) bad = true;
        sleep_ns(40000);               /* 처리 중: 이 사이에 버그 HW 가 덮어쓴다 */
        for (unsigned i = FRAME_WORDS / 2; i < FRAME_WORDS; i++) if (p[i] != frame_word(info.seq, i)) bad = true;
        if (bad) torn++;
        delivered++;
        (void)cap_qbuf(&c, idx);
        if (g_mode == M_AFTER_QBUF) { p[9] = 0xBADC0DEu; stale_writes++; }   /* BUG: 이미 넘긴 버퍼 */
    }
    pthread_join(th, NULL);
    (void)cap_streamoff(&c, 500);
    printf("mode=%s frames=%llu delivered=%llu dropped=%llu flushed=%llu\n", argv[1],
           (unsigned long long)c.frames, (unsigned long long)delivered,
           (unsigned long long)c.dropped, (unsigned long long)c.flushed);
    printf("  검출 도구: payload 찢어짐=%llu  poison_hits=%llu  소유권 불변식 위반=%llu  stale 쓰기 시도=%llu\n",
           (unsigned long long)torn, (unsigned long long)c.poison_hits,
           (unsigned long long)c.inv_fail, (unsigned long long)stale_writes);
    return 0;
}
