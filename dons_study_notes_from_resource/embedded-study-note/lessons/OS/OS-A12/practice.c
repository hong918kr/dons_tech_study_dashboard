/* OS-A12 추가 연습 — 한 번에 검증
 *
 * solution.c 의 링을 그대로 가져와(main 만 이름을 바꿔 포함) 5 개 연습의 답을 검사한다.
 *   연습 1  하위 호환: 같은 major 에서 헤더가 커지고(hdr_size) 기능 비트가 붙은 "v1.1 작성자" 를 받아들이기
 *           (필수 비트를 모르면 거절, 선택 비트는 무시)
 *   연습 2  pid 재사용에 강한 신원: (pid, 프로세스 시작 시각) — proc_pidinfo(PROC_PIDTBSDINFO)
 *   연습 3  폴링 없는 사망 통지: kqueue EVFILT_PROC + NOTE_EXIT (+ NOTE_EXITSTATUS)
 *   연습 4  BLOCK 대기를 폴링 대신 os_sync_wait_on_address(SHARED) 도어벨로 (macOS 14.4+ 공개 API)
 *   연습 5  정확히 한 번 처리: 결과 + 커서를 한 번의 원자적 전환으로 커밋하고, 재시작 때 중복을 건너뛰기
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr   (solution.c 와 같은 폴더에서)
 */
#define main solution_main
#include "solution.c"
#undef main

#include <libproc.h>
#include <sys/event.h>
#include <os/os_sync_wait_on_address.h>
#include <os/clock.h>

static int p_pass, p_total;
#define PCHECK(expr) do { p_total++; if (expr) { p_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

/* ---------------- 연습 1: major 같고 minor 가 올라간 작성자 받아들이기 ----------------
 * 규칙 (v1 계열): version = major (1). 헤더는 뒤로만 자란다: hdr_size ≥ sizeof(rb_hdr_t), 128 의 배수.
 * features 하위 16 비트 = 필수(모르면 거절), 상위 16 비트 = 선택(모르면 무시). 슬롯은 hdr_size 에서 시작. */
#define FEAT_REQ_MASK   0x0000FFFFu
#define FEAT_OPT_TS     (1u << 16)          /* 예: 헤더 뒤에 생산자 시작 시각 (연습 2 가 쓸 수 있는 칸) */
#define FEAT_REQ_ZSTD   (1u << 0)           /* 예: payload 압축 — 모르는 리더는 해석 불가 */
static int compat_validate(const rb_hdr_t *h, size_t mapped, uint32_t known_req) {
    if (mapped < sizeof(rb_hdr_t)) return RB_ENOTREADY;
    if (atomic_load_explicit(&((rb_hdr_t *)h)->magic, memory_order_acquire) != RB_MAGIC) return RB_ENOTREADY;
    if (h->version != RB_VERSION) return RB_EVERSION;                       /* major 가 다르면 끝 */
    if ((h->features & FEAT_REQ_MASK & ~known_req) != 0u) return RB_EVERSION; /* 모르는 필수 기능 */
    if (h->hdr_size < sizeof(rb_hdr_t) || h->hdr_size % CACHELINE != 0u || h->slot_size != RB_SLOT_BYTES)
        return RB_ELAYOUT;
    uint32_t cap = h->capacity;
    if (cap == 0u || (cap & (cap - 1u)) != 0u || cap > RB_MAX_CAP) return RB_ELAYOUT;
    if (h->total_size != (uint64_t)h->hdr_size + (uint64_t)cap * h->slot_size || h->total_size > mapped) return RB_ELAYOUT;
    if (h->policy > RB_OVERWRITE_OLDEST) return RB_ELAYOUT;
    return RB_OK;
}
static int compat_attach(rb_t *r, const char *name, int role, uint32_t known_req) {
    int fd = shm_open(name, O_RDWR, 0);
    if (fd < 0) return RB_ENOTREADY;
    struct stat st;
    if (fstat(fd, &st) != 0 || (size_t)st.st_size < sizeof(rb_hdr_t)) { close(fd); return RB_ENOTREADY; }
    void *p = mmap(NULL, (size_t)st.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return RB_ESYS;
    int e = compat_validate(p, (size_t)st.st_size, known_req);
    if (e != RB_OK) { munmap(p, (size_t)st.st_size); return e; }
    rb_fill_handle(r, p, (size_t)st.st_size, role);                         /* slots = base + hdr_size */
    return RB_OK;
}
/* "v1.1 작성자": 헤더 512 B (줄 하나 더 = 확장 필드), features 지정 */
static void forge_v11(const char *nm, uint32_t features) {
    int fd = shm_open(nm, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) return;
    size_t total = 512u + 8u * RB_SLOT_BYTES;
    if (ftruncate(fd, (off_t)total) == 0) {
        rb_hdr_t *h = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (h != MAP_FAILED) {
            h->version = RB_VERSION; h->hdr_size = 512; h->slot_size = RB_SLOT_BYTES; h->capacity = 8;
            h->total_size = total; h->policy = RB_BLOCK; h->features = features;
            ((uint64_t *)h)[384 / 8] = 0x1234;                                  /* 확장 필드 (v1.0 은 모름) */
            atomic_store(&h->magic, RB_MAGIC);
            munmap(h, total);
        }
    }
    close(fd);
}
static void ex1(void) {
    printf("연습 1 — minor 호환\n");
    uint8_t buf[RB_MAX_PAYLOAD], out[RB_MAX_PAYLOAD];
    rb_view_t v;
    const char *a = new_name(); forge_v11(a, FEAT_OPT_TS);
    const char *b = new_name(); forge_v11(b, FEAT_OPT_TS | FEAT_REQ_ZSTD);
    rb_t s = {0}, p = {0}, c = {0};
    PCHECK(rb_attach(&s, a, RB_CONSUMER) == RB_ELAYOUT);                 /* 엄격한 v1.0 리더: hdr_size 가 다르면 거절 */
    PCHECK(compat_attach(&c, a, RB_CONSUMER, 0) == RB_OK && c.slots == (rb_slot_t *)((char *)c.h + 512));
    PCHECK(compat_attach(&p, a, RB_PRODUCER, 0) == RB_OK);
    rb_start(&p); rb_start(&c);
    PCHECK(rb_send(&p, buf, make_msg(7, buf), 7, 0) == RB_OK);
    PCHECK(rb_recv(&c, out, sizeof out, &v, 0) == RB_OK && v.msg_seq == 7u && msg_ok(7, out, v.len));
    PCHECK(((uint64_t *)p.h)[384 / 8] == 0x1234);                         /* 확장 필드를 안 건드렸다 */
    rb_t d = {0};
    PCHECK(compat_attach(&d, b, RB_CONSUMER, 0) == RB_EVERSION);           /* 모르는 필수 기능 → 거절 */
    PCHECK(compat_attach(&d, b, RB_CONSUMER, FEAT_REQ_ZSTD) == RB_OK);     /* 아는 리더는 OK */
    rb_close(&d); rb_close(&p); rb_close(&c);
    shm_unlink(a); shm_unlink(b);
}

/* ---------------- 연습 2: (pid, 시작 시각) 신원 ----------------
 * kill(pid,0) 은 "그 번호의 프로세스가 있다" 만 말한다: 좀비도, 번호를 재사용한 남의 프로세스도 "있다".
 * 시작 시각을 같이 저장해 두면 재사용을 구분한다. proc_pidinfo 는 좀비에 대해 실패한다 (이 Mac 에서 확인). */
static bool proc_start_us(pid_t pid, uint64_t *us) {
    struct proc_bsdinfo bi;
    int n = proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bi, (int)sizeof bi);
    if (n != (int)sizeof bi) return false;
    *us = bi.pbi_start_tvsec * 1000000u + bi.pbi_start_tvusec;
    return true;
}
static int identity_state(pid_t pid, uint64_t start_us) {
    uint64_t now_start;
    if (!proc_start_us(pid, &now_start)) return PEER_GONE;      /* 없음 또는 좀비 */
    return now_start == start_us ? PEER_ALIVE : PEER_GONE;      /* 번호만 같은 다른 프로세스 = 재사용 */
}
static void ex2(void) {
    printf("연습 2 — pid 재사용에 강한 신원\n");
    uint64_t me = 0;
    PCHECK(proc_start_us(getpid(), &me) && me > 0u);
    PCHECK(identity_state(getpid(), me) == PEER_ALIVE);
    PCHECK(identity_state(getpid(), me - 1u) == PEER_GONE);     /* 같은 pid, 다른 시작 시각 = 재사용 흉내 */
    fflush(stdout);
    pid_t z = fork();
    if (z == 0) _exit(0);
    uint64_t zs = 0;
    bool got = proc_start_us(z, &zs);                            /* 살아 있을 때 읽었거나, 이미 좀비라 실패 */
    siginfo_t si;
    memset(&si, 0, sizeof si);
    PCHECK(waitid(P_PID, (id_t)z, &si, WEXITED | WNOWAIT) == 0);
    errno = 0;
    uint64_t dummy;
    bool zombie_info = proc_start_us(z, &dummy);
    printf("  zombie: kill(pid,0)=%d, proc_pidinfo %s (errno %d), start read before exit: %s\n",
           kill(z, 0), zombie_info ? "ok" : "fails", errno, got ? "yes" : "no");
    PCHECK(kill(z, 0) == 0 && !zombie_info);                     /* kill 은 "있음", 신원 확인은 "없음" */
    PCHECK(identity_state(z, got ? zs : 1u) == PEER_GONE);
    int st;
    waitpid(z, &st, 0);
    PCHECK(identity_state(z, got ? zs : 1u) == PEER_GONE && kill(z, 0) == -1 && errno == ESRCH);
}

/* ---------------- 연습 3: kqueue EVFILT_PROC NOTE_EXIT ---------------- */
static int watch_exit(int kq, pid_t pid) {
    struct kevent ev;
    EV_SET(&ev, (uintptr_t)pid, EVFILT_PROC, EV_ADD | EV_ONESHOT, NOTE_EXIT | NOTE_EXITSTATUS, 0, NULL);
    return kevent(kq, &ev, 1, NULL, 0, NULL);                   /* 이미 없는 pid 면 -1 / ESRCH */
}
/* 반환: 끝난 pid (status 채움), 시간 초과면 0 */
static pid_t wait_exit(int kq, int timeout_ms, int *status) {
    struct kevent out;
    struct timespec to = { timeout_ms / 1000, (long)(timeout_ms % 1000) * 1000000L };
    int n = kevent(kq, NULL, 0, &out, 1, &to);
    if (n != 1 || out.filter != EVFILT_PROC || !(out.fflags & NOTE_EXIT)) return 0;
    *status = (int)out.data;
    return (pid_t)out.ident;
}
static void ex3(void) {
    printf("연습 3 — kqueue NOTE_EXIT\n");
    int kq = kqueue();
    PCHECK(kq >= 0);
    fflush(stdout);
    pid_t c = fork();
    if (c == 0) { sleep_us(30000); _exit(3); }
    PCHECK(watch_exit(kq, c) == 0);
    int st = 0;
    uint64_t t0 = now_ns();
    pid_t who = wait_exit(kq, 2000, &st);
    double ms = (double)(now_ns() - t0) / 1e6;
    PCHECK(who == c && WIFEXITED(st) && WEXITSTATUS(st) == 3);   /* 회수 전에도 통지 + 종료 코드 */
    PCHECK(reap(c, 2000) >= 0);
    PCHECK(watch_exit(kq, c) == -1 && errno == ESRCH);           /* 등록 전에 이미 사라짐 → 경쟁은 호출자가 처리 */
    fflush(stdout);
    pid_t k = fork();
    if (k == 0) { for (;;) pause(); }
    PCHECK(watch_exit(kq, k) == 0 && wait_exit(kq, 20, &st) == 0);   /* 아직 살아 있음 → 시간 초과 */
    kill(k, SIGKILL);
    who = wait_exit(kq, 2000, &st);
    PCHECK(who == k && WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);
    reap(k, 2000);
    close(kq);
    printf("  exit notice after %.1f ms (child slept 30 ms, 정보용)\n", ms);
}

/* ---------------- 연습 4: os_sync_wait_on_address 도어벨 ----------------
 * waiters 표시 + seq 증가가 서로 다른 변수에 대한 store→load (SB 모양) 이라 둘 다 seq_cst 여야 깨움을 잃지 않는다.
 * 커널은 "값이 아직 expected 인가" 를 원자적으로 확인한 뒤 재운다 (futex 와 같은 계약). */
typedef struct { _Atomic uint64_t seq; _Atomic uint32_t waiters; _Atomic uint32_t wakes; } bell_t;
static void bell_ring(bell_t *b) {
    atomic_fetch_add(&b->seq, 1u);                                /* seq_cst */
    if (atomic_load(&b->waiters) != 0u) {                         /* seq_cst */
        atomic_fetch_add_explicit(&b->wakes, 1u, memory_order_relaxed);
        os_sync_wake_by_address_all(&b->seq, sizeof(uint64_t), OS_SYNC_WAKE_BY_ADDRESS_SHARED);
    }
}
/* seq 가 last 와 달라질 때까지 기다린다. 0 = 바뀜, ETIMEDOUT */
static int bell_wait(bell_t *b, uint64_t last, int timeout_ms) {
    uint64_t dl = now_ns() + (uint64_t)timeout_ms * 1000000u;
    for (;;) {
        if (atomic_load(&b->seq) != last) return 0;
        uint64_t now = now_ns();
        if (now >= dl) return ETIMEDOUT;
        atomic_fetch_add(&b->waiters, 1u);
        int rc = 0;
        if (atomic_load(&b->seq) == last)
            rc = os_sync_wait_on_address_with_timeout(&b->seq, last, sizeof(uint64_t),
                     OS_SYNC_WAIT_ON_ADDRESS_SHARED, OS_CLOCK_MACH_ABSOLUTE_TIME, dl - now);
        int e = errno;
        atomic_fetch_sub(&b->waiters, 1u);
        if (rc < 0 && e == ETIMEDOUT) return ETIMEDOUT;
    }
}
static void ex4(void) {
    printf("연습 4 — os_sync 도어벨\n");
    bell_t *bl = mmap(NULL, 2 * sizeof(bell_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    PCHECK(bl != MAP_FAILED);
    if (bl == MAP_FAILED) return;
    bell_t *ping = &bl[0], *pong = &bl[1];
    uint64_t t0 = now_ns();
    PCHECK(bell_wait(ping, 0, 20) == ETIMEDOUT && atomic_load(&ping->waiters) == 0u);
    double tmo_ms = (double)(now_ns() - t0) / 1e6;
    const int R = 2000;
    fflush(stdout);
    pid_t c = fork();
    if (c == 0) {
        alarm(5);
        for (int i = 0; i < R; i++) {
            if (bell_wait(ping, (uint64_t)i, 2000) != 0) _exit(1);
            bell_ring(pong);
        }
        _exit(0);
    }
    bool ok = true;
    t0 = now_ns();
    for (int i = 0; i < R && ok; i++) {
        bell_ring(ping);
        ok = bell_wait(pong, (uint64_t)i, 2000) == 0;
    }
    double rtt_us = (double)(now_ns() - t0) / 1e3 / R;
    PCHECK(ok && exited_ok(reap(c, 3000)));
    PCHECK(atomic_load(&ping->seq) == (uint64_t)R && atomic_load(&pong->seq) == (uint64_t)R);
    uint32_t wakes = atomic_load(&ping->wakes) + atomic_load(&pong->wakes);
    PCHECK(wakes <= 2u * (uint32_t)R);                            /* 깨움은 기다리는 쪽이 있을 때만 */
    printf("  timeout path %.1f ms (asked 20), %d round trips, %.1f us/RTT, wake syscalls %u of %d rings (정보용)\n",
           tmo_ms, R, rtt_us, wakes, 2 * R);
    munmap(bl, 2 * sizeof(bell_t));
}

/* ---------------- 연습 5: 정확히 한 번 처리 ----------------
 * 응용 상태(합계)와 커서(마지막으로 반영한 msg_seq)를 같은 레코드에 두고, 레코드 두 개 중 "현재" 인덱스를
 * 한 번의 release store 로 바꾼다(더블 버퍼 커밋). 링의 head 커밋은 그 뒤에 — 그래서 head 가 뒤처져도
 * 재시작한 소비자는 msg_seq ≤ 커서인 메시지를 건너뛴다. */
typedef struct { uint64_t sum, applied, last_msg; bool any; } app_rec_t;
typedef struct { app_rec_t rec[2]; _Atomic uint32_t cur; _Atomic int done; _Atomic uint32_t dup_skipped; } app_state_t;
enum { X_NONE, X_MID_RECORD, X_AFTER_SWITCH };
static uint64_t value_of(uint64_t seq) { return mix(seq) & 0xFFFFu; }
static void app_apply(app_state_t *a, uint64_t msg_seq, int crash_mode, uint64_t crash_msg) {
    uint32_t cur = atomic_load_explicit(&a->cur, memory_order_acquire);
    app_rec_t *o = &a->rec[cur], *n = &a->rec[cur ^ 1u];
    if (o->any && msg_seq <= o->last_msg) { atomic_fetch_add(&a->dup_skipped, 1u); return; }   /* 이미 반영됨 */
    n->sum = o->sum + value_of(msg_seq);
    if (crash_mode == X_MID_RECORD && msg_seq == crash_msg) raise(SIGKILL);   /* 새 레코드 반쯤 = 안 보임 */
    n->applied = o->applied + 1u;
    n->last_msg = msg_seq;
    n->any = true;
    atomic_store_explicit(&a->cur, cur ^ 1u, memory_order_release);           /* 커밋 지점 */
    if (crash_mode == X_AFTER_SWITCH && msg_seq == crash_msg) raise(SIGKILL); /* 반영됐지만 head 는 아직 */
}
static int ex5_consumer(const char *nm, app_state_t *a, int crash_mode, uint64_t crash_msg) {
    rb_t c = {0};
    if (rb_attach(&c, nm, RB_CONSUMER) != RB_OK) return 10;
    c.manual_commit = true;
    rb_start(&c);
    uint8_t buf[RB_MAX_PAYLOAD];
    rb_view_t v;
    uint64_t dl = now_ns() + 3000000000ull;
    while (now_ns() < dl) {
        int rc = rb_recv(&c, buf, sizeof buf, &v, 50);
        if (rc != RB_OK) continue;
        app_apply(a, v.msg_seq, crash_mode, crash_msg);
        rb_commit(&c);
        if (v.msg_seq == SC_N - 1u) { atomic_store(&a->done, 1); rb_close(&c); return 0; }
    }
    return 11;
}
static void ex5_run(int crash_mode) {
    const char *nm = new_name();
    rb_t sup = {0};
    report_t *rep = new_report();
    app_state_t *a = mmap(NULL, sizeof(app_state_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    if (!rep || a == MAP_FAILED || rb_create(&sup, nm, 8u, RB_BLOCK) != RB_OK) { PCHECK(!"setup"); return; }
    pid_t p = spawn_producer(nm, rep, CP_NONE, 0, false);
    fflush(stdout);
    pid_t c1 = fork();
    if (c1 == 0) { alarm(5); _exit(ex5_consumer(nm, a, crash_mode, SC_K)); }
    int s1 = reap(c1, 3000);
    pid_t c2 = 0;
    if (crash_mode != X_NONE) {
        fflush(stdout);
        c2 = fork();
        if (c2 == 0) { alarm(5); _exit(ex5_consumer(nm, a, X_NONE, 0)); }
    }
    int s2 = c2 ? reap(c2, 4000) : 0, sp = reap(p, 4000);
    uint64_t want = 0;
    for (uint64_t i = 0; i < SC_N; i++) want += value_of(i);
    app_rec_t *r = &a->rec[atomic_load(&a->cur)];
    static const char *const XN[] = { "no crash", "crash mid-record", "crash after switch" };
    printf("  %-20s sum %llu (want %llu), applied %llu, duplicates skipped %u\n", XN[crash_mode],
           (unsigned long long)r->sum, (unsigned long long)want, (unsigned long long)r->applied,
           atomic_load(&a->dup_skipped));
    PCHECK(crash_mode == X_NONE ? exited_ok(s1) : (killed_by(s1, SIGKILL) && exited_ok(s2)));
    PCHECK(exited_ok(sp) && atomic_load(&a->done) == 1);
    PCHECK(r->sum == want && r->applied == SC_N && r->last_msg == SC_N - 1u);   /* 정확히 한 번 */
    PCHECK(atomic_load(&a->dup_skipped) == (crash_mode == X_AFTER_SWITCH ? 1u : 0u)); /* 링은 다시 줬고 커서가 막음 */
    rb_close(&sup);
    shm_unlink(nm);
    munmap(rep, sizeof *rep);
    munmap(a, sizeof *a);
}
static void ex5(void) {
    printf("연습 5 — 정확히 한 번 처리\n");
    ex5_run(X_NONE);
    ex5_run(X_MID_RECORD);
    ex5_run(X_AFTER_SWITCH);
}

int main(void) {
    ex1();
    ex2();
    ex3();
    ex4();
    ex5();
    int leaked = 0;
    for (int i = 0; i < g_nnames; i++) {
        int fd = shm_open(g_names[i], O_RDONLY, 0);
        if (fd >= 0) { leaked++; close(fd); shm_unlink(g_names[i]); }
    }
    PCHECK(leaked == 0);
    printf("%d/%d passed\n", p_pass, p_total);
    return p_pass == p_total ? 0 : 1;
}
