/* OS-A12 견고한 공유 메모리 IPC: 버전·백프레셔·상대 프로세스 장애 감지 (macOS)
 *
 * DS-A01 SPSC 링 축약판을 shm_open 메모리 위에 올리고, "실서비스용" 프로토콜 요소를 붙인다.
 *   Part A  헤더(매직·버전·레이아웃 크기) 검증 / 슬롯별 seqlock 마커 + 체크섬 (찢어진 메시지 거부) /
 *           메시지 seq 번호로 유실(gap) 감지
 *   Part B  가득 참 정책 3종: BLOCK(제한 시간 + 백오프 폴링), DROP_NEWEST, OVERWRITE_OLDEST(seqlock 으로
 *           덮어쓰인 슬롯 감지 → 최신 창으로 재동기화) + 스레드 스트레스 (TSan 대상)
 *   Part C  상대 장애 감지: 하트비트 카운터 정체 + kill(pid,0)==ESRCH / 세대(generation) /
 *           생산자 재시작 시 roll-forward 복구 / 크래시 주입(fork 한 자식이 정해진 지점에서 raise(SIGKILL))
 *
 * 체크는 "올바른 구현이면 매번 성립하는 것" 만 본다 (시간 측정값은 체크하지 않는다 — 정보용 출력만).
 * 모든 fork 는 기다리고(시간 제한 후 SIGKILL), 모든 대기는 제한 시간이 있으며, shm 이름은
 * /OS-A12.<pid>.<n> 이고 끝에서 전부 사라졌는지 확인한다.
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so   (starter.c 도 같은 명령)
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

_Static_assert(ATOMIC_LLONG_LOCK_FREE == 2, "64-bit atomics must be lock-free (address-free across processes)");

#if defined(__has_feature)
#  if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
#    define SAN_BUILD 1
#  endif
#endif
#ifndef SAN_BUILD
#  define SAN_BUILD 0
#endif

/* ======================= 프로토콜 정의 ======================= */
#define RB_MAGIC        0x52323141u                 /* 메모리 바이트 'A' '1' '2' 'R' (little-endian) */
#define RB_VERSION      1u
#define RB_SLOT_BYTES   256u
#define RB_SLOT_WORDS   (RB_SLOT_BYTES / 8u)        /* 32 워드 */
#define RB_META_WORDS   4u                          /* w0 마커, w1 gen<<32|len, w2 msg_seq, w3 체크섬 */
#define RB_MAX_PAYLOAD  ((RB_SLOT_WORDS - RB_META_WORDS) * 8u)   /* 224 B */
#define RB_MAX_CAP      4096u
#define CACHELINE       128                         /* Apple M: hw.cachelinesize = 128 */

enum { RB_BLOCK = 0, RB_DROP_NEWEST = 1, RB_OVERWRITE_OLDEST = 2 };
enum { RB_PRODUCER = 1, RB_CONSUMER = 2 };
enum { RB_OK = 0, RB_EMPTY, RB_FULL, RB_DROPPED, RB_TIMEOUT, RB_PEER_DEAD, RB_CORRUPT, RB_FENCED,
       RB_ENOTREADY, RB_EVERSION, RB_ELAYOUT, RB_ETOOBIG, RB_EXISTS, RB_ESYS };
enum { SLOT_OK = 0, SLOT_NOTREADY, SLOT_OVERRUN, SLOT_CORRUPT };
enum { PEER_NONE = 0, PEER_ALIVE, PEER_STALE, PEER_GONE };
static const char *const PEER_NAME[] = { "NONE", "ALIVE", "STALE", "GONE" };

/* 공유 헤더: 줄(128 B)마다 쓰는 쪽이 하나. 0번 줄 앞 8 바이트(magic, version, hdr_size)는
 * 어떤 버전에서도 위치가 안 바뀌는 "ABI 고정 접두사" — 낯선 버전도 여기까지는 읽을 수 있어야 한다. */
typedef struct {
    _Atomic uint32_t magic;          /* 초기화가 끝난 뒤 맨 마지막에 release 로 기록 */
    uint16_t version;
    uint16_t hdr_size;
    uint32_t slot_size;
    uint32_t capacity;               /* 슬롯 수, 2의 거듭제곱 */
    uint64_t total_size;             /* hdr_size + capacity * slot_size */
    uint32_t policy;
    uint32_t features;               /* v1 = 0 (연습 1 에서 사용) */
    _Alignas(CACHELINE) _Atomic uint64_t tail;   /* 생산자 줄: 게시된 위치의 "힌트" (진실은 슬롯 마커) */
    _Atomic uint64_t prod_hb;        /* 하트비트 카운터 */
    _Atomic uint32_t prod_pid;
    _Atomic uint32_t prod_gen;       /* 생산자 세대: (재)시작마다 +1 */
    _Atomic uint64_t dropped;        /* DROP_NEWEST 로 버린 수 */
    _Alignas(CACHELINE) _Atomic uint64_t head;   /* 소비자 줄: 커밋된 읽기 위치 */
    _Atomic uint64_t cons_hb;
    _Atomic uint32_t cons_pid;
    _Atomic uint32_t cons_gen;
} rb_hdr_t;
_Static_assert(sizeof(rb_hdr_t) == 3 * CACHELINE, "header = 3 lines");
_Static_assert(offsetof(rb_hdr_t, version) == 4 && offsetof(rb_hdr_t, hdr_size) == 6, "ABI prefix");

/* 슬롯 = 원자적 64비트 워드 32개. w0 마커: 0 = 안 씀, 2p+1 = 위치 p 쓰는 중, 2p+2 = 위치 p 게시됨 */
typedef struct { _Atomic uint64_t w[RB_SLOT_WORDS]; } rb_slot_t;
_Static_assert(sizeof(rb_slot_t) == RB_SLOT_BYTES, "slot size");

typedef struct { uint32_t gen, len; uint64_t msg_seq; uint8_t data[RB_MAX_PAYLOAD]; } rb_view_t;
typedef struct { uint32_t pid, gen; uint64_t last_hb, last_change_ns; bool seen; } peer_watch_t;

/* 프로세스 로컬 핸들 (공유 메모리에 두지 않는다) */
typedef struct {
    rb_hdr_t *h;
    rb_slot_t *slots;
    size_t map_len;
    uint32_t cap, mask, policy;
    int role;
    uint32_t gen;                 /* rb_start 에서 받은 내 세대 */
    uint64_t pos;                 /* 생산자: 다음 쓸 위치 / 소비자: 다음 읽을 위치 */
    uint64_t my_hb;
    uint64_t next_msg_hint;       /* 생산자 rb_start: 이어서 보낼 msg_seq */
    uint64_t rolled;              /* 생산자 rb_start: roll-forward 한 칸 수 */
    bool manual_commit;           /* 소비자: true 면 rb_recv 가 head 를 올리지 않음 → rb_commit */
    uint64_t stale_ns;            /* 하트비트가 이만큼 안 바뀌면 STALE */
    peer_watch_t pw;
    uint64_t last_check_ns;
    int last_peer;
    /* 소비자 통계 */
    bool have;
    uint32_t cur_gen;
    uint64_t next_msg, received, lost, dups, resyncs, overruns, skipped, torn_rejected, corrupt_pos;
} rb_t;

/* ======================= 주어진 도구 ======================= */
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static inline void cpu_relax(void) {
#if defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("pause" ::: "memory");
#endif
}
/* 백오프: 64번 스핀 → 64번 sched_yield → 1 µs 부터 두 배씩 최대 1 ms 잠 (macOS 에는 futex 가 없다) */
typedef struct { uint32_t n; } backoff_t;
static void backoff_wait(backoff_t *b) {
    if (b->n < 64u) cpu_relax();
    else if (b->n < 128u) sched_yield();
    else {
        uint32_t k = b->n - 128u;
        long ns = 1000L << (k < 10u ? k : 10u);
        if (ns > 1000000L) ns = 1000000L;
        struct timespec ts = { 0, ns };
        nanosleep(&ts, NULL);
    }
    b->n++;
}
static uint64_t fnv1a(uint64_t hsh, const void *p, size_t n) {
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) { hsh ^= b[i]; hsh *= 0x100000001b3ull; }
    return hsh;
}
/* 체크섬 = FNV-1a 64 (meta, msg_seq, payload). 마커는 포함하지 않는다 (위치가 바뀌어도 같은 내용) */
static uint64_t slot_csum(uint64_t meta, uint64_t msg_seq, const void *data, uint32_t len) {
    uint64_t hsh = 0xcbf29ce484222325ull;
    hsh = fnv1a(hsh, &meta, 8);
    hsh = fnv1a(hsh, &msg_seq, 8);
    return fnv1a(hsh, data, len);
}

/* 크래시 주입: 자식 프로세스에서만 켠다. 해당 msg_seq 의 해당 지점에서 raise(SIGKILL) = 전원 차단처럼 즉사 */
typedef enum { CP_NONE = 0, CP_HDR_MID, CP_SLOT_CLAIMED, CP_PAYLOAD_HALF, CP_SEQ_BEFORE_CSUM,
               CP_BEFORE_PUBLISH, CP_BEFORE_TAIL, CP_AFTER_SEND, CP_BUG_EARLY_PUBLISH } crash_point_t;
static const char *const CP_NAME[] = { "none", "hdr_mid", "slot_claimed", "payload_half",
    "seq_before_csum", "before_publish", "before_tail", "after_send", "BUG_early_pub" };
static crash_point_t g_crash_cp = CP_NONE;
static uint64_t g_crash_msg;
#define CRASH_POINT(cp, msg) do { if (g_crash_cp == (cp) && g_crash_msg == (uint64_t)(msg)) raise(SIGKILL); } while (0)

/* ======================= Part A — 헤더 검증, 슬롯 쓰기/읽기 ======================= */

/* TODO(A1): 매핑된 헤더를 검사한다. mapped = 실제로 매핑된 바이트 수 (macOS shm 은 페이지 단위로 올림됨).
 *   mapped < sizeof(rb_hdr_t) 또는 magic(acquire) != RB_MAGIC   → RB_ENOTREADY (만드는 중이거나 만들다 죽음)
 *   version != RB_VERSION                                         → RB_EVERSION
 *   hdr_size / slot_size 가 내 것과 다름, capacity 가 0·2의 거듭제곱 아님·RB_MAX_CAP 초과,
 *   total_size != hdr_size + capacity*slot_size, total_size > mapped, policy > 2   → RB_ELAYOUT
 *   다 맞으면 RB_OK */
static int rb_validate_header(const rb_hdr_t *h, size_t mapped) {
    (void)h;
    (void)mapped;
    return RB_ENOTREADY;          /* TODO(A1) */
}

/* TODO(A2): 위치 pos 에 메시지를 쓰고 게시한다 (단일 생산자 seqlock 쓰기).
 *   1) 마커 = 2*pos+1 (relaxed) → atomic_thread_fence(release)          CRASH_POINT(CP_SLOT_CLAIMED)
 *   2) payload 앞 절반 워드 → CRASH_POINT(CP_PAYLOAD_HALF) → 뒤 절반      (모두 relaxed store)
 *   3) w1 = gen<<32|len, w2 = msg_seq → CRASH_POINT(CP_SEQ_BEFORE_CSUM) → w3 = 체크섬
 *   4) CRASH_POINT(CP_BEFORE_PUBLISH) → 마커 = 2*pos+2 (release)  ← 이 store 하나가 "게시" */
static void slot_publish(rb_t *r, uint64_t pos, uint64_t msg_seq, const void *data, uint32_t len) {
    (void)r;
    (void)pos;
    (void)msg_seq;
    (void)data;
    (void)len;                    /* TODO(A2) */
}

/* TODO(A3): 위치 pos 의 슬롯을 읽는다 (seqlock 읽기 + 체크섬).
 *   m1 = 마커(acquire). m1 != 2*pos+2 이면: m1 > 2*pos+2 → SLOT_OVERRUN (다음 바퀴가 덮음), 아니면 SLOT_NOTREADY
 *   w1..w31 을 relaxed 로 로컬 배열에 복사 → atomic_thread_fence(acquire) → m2 = 마커(relaxed)
 *   m2 != m1 → 복사 도중 바뀜: m2 > 2*pos+2 면 OVERRUN, 아니면 NOTREADY
 *   len > RB_MAX_PAYLOAD 또는 체크섬 불일치 → SLOT_CORRUPT
 *   OK 면 v 를 채우고 SLOT_OK */
static int slot_read(rb_t *r, uint64_t pos, rb_view_t *v) {
    (void)r;
    (void)pos;
    (void)v;
    return SLOT_NOTREADY;         /* TODO(A3) */
}

/* ======================= Part C (먼저 필요) — 상대 생존 판단 ======================= */

/* TODO(C1): 상대(생산자면 소비자, 소비자면 생산자)의 상태를 now 기준으로 판단한다.
 *   pid == 0                                 → PEER_NONE (아직 안 붙었거나 정상 분리) — 감시 상태 초기화
 *   (pid, gen) 이 감시 중인 것과 다름(새 상대) → 감시 재시작: last_hb = 현재 hb, last_change = now → PEER_ALIVE
 *   kill(pid, 0) == -1 && errno == ESRCH      → PEER_GONE (확실히 죽음. EPERM 은 "살아 있음")
 *   hb 가 last_hb 와 다름                     → 갱신(last_hb, last_change = now) → PEER_ALIVE
 *   now - last_change >= r->stale_ns          → PEER_STALE (좀비·멈춤·pid 재사용 모두 여기서 잡힌다)
 *   그 밖                                      → PEER_ALIVE */
static int rb_peer_state(rb_t *r, uint64_t now) {
    (void)r;
    (void)now;
    return PEER_ALIVE;            /* TODO(C1) */
}

/* 주어짐: 대기 루프용. 1 ms 에 한 번만 실제로 판단(kill 시스템 콜 절약). 죽었으면 true */
static bool peer_dead_now(rb_t *r) {
    uint64_t t = now_ns();
    if (r->last_check_ns != 0u && t - r->last_check_ns < 1000000u) return false;
    r->last_check_ns = t;
    r->last_peer = rb_peer_state(r, t);
    return r->last_peer == PEER_STALE || r->last_peer == PEER_GONE;
}
static void beat(rb_t *r) {
    r->my_hb++;
    atomic_store_explicit(r->role == RB_PRODUCER ? &r->h->prod_hb : &r->h->cons_hb, r->my_hb, memory_order_relaxed);
}

/* ======================= Part B — 가득 참 정책과 수신 ======================= */

/* TODO(B1): 생산자 보내기. len > RB_MAX_PAYLOAD → RB_ETOOBIG.
 *   prod_gen != r->gen 이면 RB_FENCED (나보다 새 생산자가 시작됨 — 옛 생산자는 손 떼기).
 *   OVERWRITE_OLDEST 가 아니면 head(acquire) 를 보고 pos - head == cap 인 동안:
 *     DROP_NEWEST → dropped++ (relaxed) 후 RB_DROPPED
 *     BLOCK       → timeout_ms == 0 이면 RB_FULL / peer_dead_now → RB_PEER_DEAD / 마감 지남 → RB_TIMEOUT /
 *                   beat + backoff_wait 후 다시
 *   slot_publish → CRASH_POINT(CP_BEFORE_TAIL) → pos++, tail = pos (release) → CRASH_POINT(CP_AFTER_SEND)
 *   → beat → RB_OK */
static int rb_send(rb_t *r, const void *data, uint32_t len, uint64_t msg_seq, int timeout_ms) {
    (void)r;
    (void)data;
    (void)len;
    (void)msg_seq;
    (void)timeout_ms;
    return RB_FULL;               /* TODO(B1) */
}

/* TODO(B2): 소비자 받기. 루프:
 *   beat → k = slot_read(r, pos)
 *   SLOT_OK      : len > bufcap 이면 RB_ETOOBIG (소비하지 않음). 복사, info 채우기, 메시지 추적
 *                  (처음이거나 gen 이 바뀜 → 바뀐 경우 resyncs++ / 같은 gen 에서 msg_seq > next_msg → lost += 차이 /
 *                   msg_seq < next_msg → dups++ ; next_msg = msg_seq+1, received++), pos++,
 *                  manual_commit 이 아니면 head = pos (release). RB_OK
 *   SLOT_OVERRUN : 덮어쓰였다 → t = tail(acquire), np = max(t - cap + 1, pos + 1) 로 점프,
 *                  overruns++, skipped += np - pos, pos = np, head = pos (release), 곧바로 다시
 *   그 밖(NOTREADY/CORRUPT): CORRUPT 이고 이 위치를 처음 보면 torn_rejected++ (corrupt_pos = pos+1 로 기억)
 *                  timeout_ms == 0 → CORRUPT 면 RB_CORRUPT, 아니면 RB_EMPTY
 *                  peer_dead_now → 한 번 더 슬롯을 본 뒤(죽기 직전 게시했을 수 있음)에도 없으면 RB_PEER_DEAD
 *                  마감 지남 → RB_CORRUPT 또는 RB_TIMEOUT / backoff_wait */
static int rb_recv(rb_t *r, void *buf, size_t bufcap, rb_view_t *info, int timeout_ms) {
    (void)r;
    (void)buf;
    (void)bufcap;
    (void)info;
    (void)timeout_ms;
    return RB_EMPTY;              /* TODO(B2) */
}

/* 주어짐: manual_commit 소비자가 "처리 끝" 을 알린다 */
static void rb_commit(rb_t *r) { atomic_store_explicit(&r->h->head, r->pos, memory_order_release); }

/* ======================= Part C — (재)시작과 복구 ======================= */

/* TODO(C2): 역할을 맡는다.
 *   생산자: gen = ++prod_gen (fetch_add). t = tail(acquire) 에서 시작해, slot_read(t) == SLOT_OK 인 동안 t++
 *           (게시는 됐는데 tail 을 못 올리고 죽은 칸 = roll-forward; 최대 cap 칸). rolled = 올린 칸 수.
 *           next_msg_hint = (t > 0 이고 slot_read(t-1) OK) ? 그 msg_seq + 1 : 0. tail = t (release), pos = t,
 *           prod_pid = getpid() (release), beat.
 *   소비자: gen = ++cons_gen. pos = head(acquire). 추적 상태 초기화(have = false). cons_pid = getpid(), beat.
 *   RB_OK */
static int rb_start(rb_t *r) {
    (void)r;
    return RB_ESYS;               /* TODO(C2) */
}

/* ======================= 주어짐: 만들기 / 붙기 / 닫기 ======================= */
static void rb_fill_handle(rb_t *r, void *base, size_t map_len, int role) {
    memset(r, 0, sizeof *r);
    r->h = base;
    r->map_len = map_len;
    r->slots = (rb_slot_t *)((char *)base + r->h->hdr_size);
    r->cap = r->h->capacity;
    r->mask = r->cap - 1u;
    r->policy = r->h->policy;
    r->role = role;
    r->stale_ns = 500u * 1000000u;
}

/* 생산자 쪽에서 링을 만든다. 이미 있으면: 유효하면 RB_EXISTS (rb_attach 를 쓸 것),
 * 만들다 죽은 흔적(magic 없음)이면 다시 초기화한다. macOS 는 shm 에 ftruncate 를 두 번 못 한다(EINVAL). */
static int rb_create(rb_t *r, const char *name, uint32_t cap, uint32_t policy) {
    size_t need = sizeof(rb_hdr_t) + (size_t)cap * RB_SLOT_BYTES;
    bool reuse = false;
    int fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0 && errno == EEXIST) { fd = shm_open(name, O_RDWR, 0); reuse = true; }
    if (fd < 0) return RB_ESYS;
    if (!reuse) {
        if (ftruncate(fd, (off_t)need) != 0) { close(fd); return RB_ESYS; }
    } else {
        struct stat st;
        if (fstat(fd, &st) != 0 || (size_t)st.st_size < need) { close(fd); return RB_ELAYOUT; }
    }
    void *p = mmap(NULL, need, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return RB_ESYS;
    rb_hdr_t *h = p;
    if (reuse && rb_validate_header(h, need) == RB_OK) { munmap(p, need); return RB_EXISTS; }
    atomic_store_explicit(&h->magic, 0u, memory_order_relaxed);
    memset((char *)p + sizeof(rb_hdr_t), 0, need - sizeof(rb_hdr_t));     /* 모든 마커 0 */
    h->version = RB_VERSION;
    h->hdr_size = (uint16_t)sizeof(rb_hdr_t);
    CRASH_POINT(CP_HDR_MID, 0);
    h->slot_size = RB_SLOT_BYTES;
    h->capacity = cap;
    h->total_size = need;
    h->policy = policy;
    h->features = 0u;
    atomic_store(&h->tail, 0u); atomic_store(&h->head, 0u); atomic_store(&h->dropped, 0u);
    atomic_store(&h->prod_pid, 0u); atomic_store(&h->cons_pid, 0u);
    atomic_store(&h->prod_gen, 0u); atomic_store(&h->cons_gen, 0u);
    atomic_store(&h->prod_hb, 0u); atomic_store(&h->cons_hb, 0u);
    atomic_store_explicit(&h->magic, RB_MAGIC, memory_order_release);    /* 맨 마지막: "준비 끝" */
    rb_fill_handle(r, p, need, RB_PRODUCER);
    return RB_OK;
}

/* 이미 있는 링에 붙는다: 크기 확인 → mmap → rb_validate_header. 실패하면 매핑을 풀고 오류 코드 */
static int rb_attach(rb_t *r, const char *name, int role) {
    int fd = shm_open(name, O_RDWR, 0);
    if (fd < 0) return errno == ENOENT ? RB_ENOTREADY : RB_ESYS;
    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return RB_ESYS; }
    if ((size_t)st.st_size < sizeof(rb_hdr_t)) { close(fd); return RB_ENOTREADY; }  /* ftruncate 전 */
    size_t len = (size_t)st.st_size;                    /* macOS: 페이지 크기로 올림된 값 */
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return RB_ESYS;
    int e = rb_validate_header(p, len);
    if (e != RB_OK) { munmap(p, len); return e; }
    rb_fill_handle(r, p, len, role);
    return RB_OK;
}

/* 정상 분리: 내 pid 를 지워 상대가 "죽음" 이 아니라 "없음(NONE)" 으로 보게 한다 */
static void rb_close(rb_t *r) {
    if (!r->h) return;
    _Atomic uint32_t *pid = r->role == RB_PRODUCER ? &r->h->prod_pid : &r->h->cons_pid;
    _Atomic uint32_t *gen = r->role == RB_PRODUCER ? &r->h->prod_gen : &r->h->cons_gen;
    if (r->gen != 0u && atomic_load(pid) == (uint32_t)getpid() && atomic_load(gen) == r->gen)
        atomic_store_explicit(pid, 0u, memory_order_release);
    munmap(r->h, r->map_len);
    r->h = NULL;
}

/* ======================= 테스트 하네스 ======================= */
static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)
#define FAILS() (g_total - g_pass)

static char g_names[64][32];
static int g_nnames;
static const char *new_name(void) {
    char *n = g_names[g_nnames];
    snprintf(n, 32, "/OS-A12.%d.%d", (int)getpid(), g_nnames);
    g_nnames++;
    return n;
}
static uint64_t mix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
/* 메시지 msg_seq 의 내용: 길이 16..215, 바이트는 mix(seq) 에서 — 받는 쪽이 독립적으로 검증 가능 */
static uint32_t make_msg(uint64_t seq, uint8_t *buf) {
    uint32_t len = 16u + (uint32_t)(seq * 37u % 200u);
    uint64_t m = mix(seq);
    for (uint32_t i = 0; i < len; i++) buf[i] = (uint8_t)((m >> (8u * (i % 8u))) + i);
    return len;
}
static bool msg_ok(uint64_t seq, const uint8_t *buf, uint32_t len) {
    uint8_t want[RB_MAX_PAYLOAD];
    return make_msg(seq, want) == len && memcmp(want, buf, len) == 0;
}
static void sleep_us(long us) { struct timespec ts = { us / 1000000L, (us % 1000000L) * 1000L }; nanosleep(&ts, NULL); }

/* 자식 대기: WNOHANG 폴링, timeout_ms 가 지나면 SIGKILL 후 회수. 반환 = wait status (-1 = 오류) */
static int reap(pid_t pid, int timeout_ms) {
    int st = 0;
    uint64_t dl = now_ns() + (uint64_t)timeout_ms * 1000000u;
    for (;;) {
        pid_t w = waitpid(pid, &st, WNOHANG);
        if (w == pid) return st;
        if (w < 0 && errno != EINTR) return -1;
        if (now_ns() >= dl) { kill(pid, SIGKILL); waitpid(pid, &st, 0); return -2; }
        sleep_us(200);
    }
}
static bool killed_by(int st, int sig) { return st >= 0 && WIFSIGNALED(st) && WTERMSIG(st) == sig; }
static bool exited_ok(int st) { return st >= 0 && WIFEXITED(st) && WEXITSTATUS(st) == 0; }
static bool wait_flag(_Atomic int *f, int timeout_ms) {
    uint64_t dl = now_ns() + (uint64_t)timeout_ms * 1000000u;
    while (!atomic_load(f)) { if (now_ns() >= dl) return false; sleep_us(200); }
    return true;
}

/* 크래시 시나리오가 결과를 남기는 곳: fork 전에 만든 MAP_SHARED|MAP_ANON 영역 */
#define SC_N 40u
typedef struct {
    _Atomic uint32_t cnt[SC_N];          /* msg_seq 별 처리 횟수 */
    _Atomic uint32_t gen_of[SC_N];       /* 그 메시지를 쓴 생산자 세대 */
    _Atomic int dead_seen, dead_reason, order_err, torn_accepted;
    _Atomic uint64_t lost, dups, resyncs, torn_rejected, received;
    _Atomic uint64_t p2_gen, p2_hint, p2_rolled;
    _Atomic int prod_saw_dead, prod_dead_reason;
    _Atomic int64_t c2_first;
} report_t;
static report_t *new_report(void) {
    void *p = mmap(NULL, sizeof(report_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

/* 자식: 소비자. 메시지 0..SC_N-1 을 다 받을 때까지(최대 3 s). 상대 사망을 보면 기록하고 계속 기다린다 */
static int child_consumer(const char *name, report_t *rep, bool manual, uint64_t crash_msg, int crash_after_commit) {
    rb_t c = {0};
    if (rb_attach(&c, name, RB_CONSUMER) != RB_OK) return 10;
    c.manual_commit = manual;
    rb_start(&c);
    uint8_t buf[RB_MAX_PAYLOAD];
    rb_view_t v;
    int64_t last = -1;
    bool first = true;
    uint64_t dl = now_ns() + 3000000000ull;
    while (last != (int64_t)SC_N - 1 && now_ns() < dl) {
        int rc = rb_recv(&c, buf, sizeof buf, &v, 50);
        if (rc == RB_OK) {
            if (!msg_ok(v.msg_seq, buf, v.len)) atomic_fetch_add(&rep->torn_accepted, 1);
            if (v.msg_seq < SC_N) { atomic_fetch_add(&rep->cnt[v.msg_seq], 1u); atomic_store(&rep->gen_of[v.msg_seq], v.gen); }
            if (first) { atomic_store(&rep->c2_first, (int64_t)v.msg_seq); first = false; }
            if ((int64_t)v.msg_seq <= last && !manual) atomic_fetch_add(&rep->order_err, 1);
            last = (int64_t)v.msg_seq;
            if (manual) {                               /* 처리(위의 기록) 후 커밋 = at-least-once */
                if (v.msg_seq == crash_msg && !crash_after_commit) raise(SIGKILL);
                rb_commit(&c);
                if (v.msg_seq == crash_msg && crash_after_commit) raise(SIGKILL);
            }
        } else if (rc == RB_PEER_DEAD) {
            if (!atomic_load(&rep->dead_seen)) { atomic_store(&rep->dead_reason, c.last_peer); atomic_store(&rep->dead_seen, 1); }
            sleep_us(500);
        }
    }
    atomic_store(&rep->lost, c.lost); atomic_store(&rep->dups, c.dups);
    atomic_store(&rep->resyncs, c.resyncs); atomic_store(&rep->torn_rejected, c.torn_rejected);
    atomic_store(&rep->received, c.received);
    rb_close(&c);
    return last == (int64_t)SC_N - 1 ? 0 : 11;
}

/* 일부러 틀린 생산자 (하네스 전용): 마커를 "게시됨" 으로 먼저 쓰고 본문을 쓰다가 죽는다.
 * seqlock 순서를 어긴 쓰기 / 캐시가 coherent 하지 않은 코어 / DMA 가 이렇게 보인다 → 체크섬이 마지막 방어선 */
static void buggy_publish_and_die(rb_t *r, uint64_t msg_seq, const uint8_t *data, uint32_t len) {
    rb_slot_t *s = &r->slots[r->pos & r->mask];
    atomic_store_explicit(&s->w[0], 2u * r->pos + 2u, memory_order_release);   /* 너무 이른 게시 */
    uint64_t pw[RB_SLOT_WORDS - RB_META_WORDS] = { 0 };
    memcpy(pw, data, len);
    atomic_store_explicit(&s->w[1], ((uint64_t)r->gen << 32) | len, memory_order_relaxed);
    atomic_store_explicit(&s->w[2], msg_seq, memory_order_relaxed);
    for (uint32_t i = 0; i < (len + 7u) / 16u; i++)
        atomic_store_explicit(&s->w[RB_META_WORDS + i], pw[i], memory_order_relaxed);
    raise(SIGKILL);                                                            /* 본문 절반, 체크섬 없음 */
}

/* 자식: 생산자. from..SC_N-1 을 BLOCK 으로 보낸다. 소비자 사망(PEER_DEAD)이면 기록하고 같은 메시지를 재시도 */
static int child_producer(const char *name, report_t *rep, crash_point_t cp, uint64_t crash_msg, bool record_start) {
    rb_t p = {0};
    if (rb_attach(&p, name, RB_PRODUCER) != RB_OK) return 20;
    rb_start(&p);
    if (record_start) { atomic_store(&rep->p2_gen, p.gen); atomic_store(&rep->p2_hint, p.next_msg_hint); atomic_store(&rep->p2_rolled, p.rolled); }
    g_crash_cp = cp;
    g_crash_msg = crash_msg;
    uint8_t buf[RB_MAX_PAYLOAD];
    uint64_t dl = now_ns() + 3000000000ull;
    for (uint64_t s = p.next_msg_hint; s < SC_N; ) {
        uint32_t len = make_msg(s, buf);
        if (cp == CP_BUG_EARLY_PUBLISH && s == crash_msg) buggy_publish_and_die(&p, s, buf, len);
        int rc = rb_send(&p, buf, len, s, 50);
        if (rc == RB_OK) { s++; continue; }
        if (rc == RB_PEER_DEAD && !atomic_load(&rep->prod_saw_dead)) {
            atomic_store(&rep->prod_dead_reason, p.last_peer); atomic_store(&rep->prod_saw_dead, 1);
        }
        if (rc != RB_PEER_DEAD && rc != RB_TIMEOUT) return 21;
        if (now_ns() >= dl) return 22;
        sleep_us(500);
    }
    rb_close(&p);
    return 0;
}
static pid_t spawn_consumer(const char *n, report_t *rep, bool manual, uint64_t cm, int after) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) { alarm(5); _exit(child_consumer(n, rep, manual, cm, after)); }
    return pid;
}
static pid_t spawn_producer(const char *n, report_t *rep, crash_point_t cp, uint64_t cm, bool rec) {
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) { alarm(5); _exit(child_producer(n, rep, cp, cm, rec)); }
    return pid;
}

/* 시나리오 P: 생산자 P1 이 메시지 K 를 쓰다가 cp 에서 즉사 → 소비자 감지 → 감독자가 P2 재시작 → 0..N-1 정확히 한 번 */
#define SC_K 10u
static void scenario_producer_crash(crash_point_t cp) {
    const char *nm = new_name();
    rb_t sup = {0};
    report_t *rep = new_report();
    if (!rep || rb_create(&sup, nm, 32u, RB_BLOCK) != RB_OK) { CHECK(!"create"); return; }
    pid_t c = spawn_consumer(nm, rep, false, 0, 0);
    pid_t p1 = spawn_producer(nm, rep, cp, SC_K, false);
    int s1 = reap(p1, 3000);                       /* 감독자가 바로 회수 → 소비자의 kill(pid,0) 이 ESRCH */
    bool seen = wait_flag(&rep->dead_seen, 3000);  /* 소비자가 죽음을 감지할 때까지 재시작 보류 */
    pid_t p2 = spawn_producer(nm, rep, CP_NONE, 0, true);
    int s2 = reap(p2, 4000), sc = reap(c, 4000);
    bool once = true;
    for (uint32_t i = 0; i < SC_N; i++) once = once && atomic_load(&rep->cnt[i]) == 1u;
    bool k_from_p1 = (cp == CP_BEFORE_TAIL || cp == CP_AFTER_SEND);
    printf("  P-crash %-16s P1 %s, consumer saw %s (reason %s), P2 gen %llu hint %llu rolled %llu, torn_rejected %llu\n",
           CP_NAME[cp], killed_by(s1, SIGKILL) ? "SIGKILL" : "??", seen ? "death" : "nothing",
           PEER_NAME[atomic_load(&rep->dead_reason) & 3], (unsigned long long)atomic_load(&rep->p2_gen),
           (unsigned long long)atomic_load(&rep->p2_hint), (unsigned long long)atomic_load(&rep->p2_rolled),
           (unsigned long long)atomic_load(&rep->torn_rejected));
    CHECK(killed_by(s1, SIGKILL) && exited_ok(s2) && exited_ok(sc));
    CHECK(seen && (atomic_load(&rep->dead_reason) == PEER_GONE || atomic_load(&rep->dead_reason) == PEER_STALE));
    CHECK(once && atomic_load(&rep->order_err) == 0 && atomic_load(&rep->torn_accepted) == 0);   /* 정확히 한 번, 순서, 무결 */
    CHECK(atomic_load(&rep->resyncs) == 1u && atomic_load(&rep->lost) == 0u && atomic_load(&rep->p2_gen) == 2u);
    CHECK(atomic_load(&rep->p2_hint) == (k_from_p1 ? SC_K + 1u : SC_K)
          && atomic_load(&rep->p2_rolled) == (cp == CP_BEFORE_TAIL ? 1u : 0u)
          && atomic_load(&rep->gen_of[SC_K]) == (k_from_p1 ? 1u : 2u)
          && atomic_load(&rep->torn_rejected) == (cp == CP_BUG_EARLY_PUBLISH ? 1u : 0u));
    rb_close(&sup);
    shm_unlink(nm);
    munmap(rep, sizeof *rep);
}

/* 시나리오 Q: 소비자 C1 이 메시지 K 를 "처리" 한 뒤 커밋 전/후에 즉사 → 생산자(BLOCK) 가 감지 → C2 재시작 */
static void scenario_consumer_crash(int after_commit) {
    const char *nm = new_name();
    rb_t sup = {0};
    report_t *rep = new_report();
    if (!rep || rb_create(&sup, nm, 8u, RB_BLOCK) != RB_OK) { CHECK(!"create"); return; }
    pid_t p = spawn_producer(nm, rep, CP_NONE, 0, false);
    pid_t c1 = spawn_consumer(nm, rep, true, SC_K, after_commit);
    int s1 = reap(c1, 3000);
    bool seen = wait_flag(&rep->prod_saw_dead, 3000);   /* 링(8칸)이 차면 생산자가 대기 중에 감지 */
    atomic_store(&rep->c2_first, -1);
    pid_t c2 = spawn_consumer(nm, rep, true, SC_N + 1u, 0);
    int s2 = reap(c2, 4000), sp = reap(p, 4000);
    bool ok = true;
    for (uint32_t i = 0; i < SC_N; i++)
        ok = ok && atomic_load(&rep->cnt[i]) == ((i == SC_K && !after_commit) ? 2u : 1u);
    printf("  C-crash %-16s C1 %s, producer saw %s (reason %s), C2 first msg %lld, msg %u processed %u x\n",
           after_commit ? "after_commit" : "before_commit", killed_by(s1, SIGKILL) ? "SIGKILL" : "??",
           seen ? "death" : "nothing", PEER_NAME[atomic_load(&rep->prod_dead_reason) & 3],
           (long long)atomic_load(&rep->c2_first), SC_K, atomic_load(&rep->cnt[SC_K]));
    CHECK(killed_by(s1, SIGKILL) && exited_ok(s2) && exited_ok(sp));
    CHECK(seen);
    CHECK(ok && atomic_load(&rep->torn_accepted) == 0);      /* 커밋 전 사망 = 그 메시지만 두 번 (at-least-once) */
    CHECK(atomic_load(&rep->c2_first) == (int64_t)(after_commit ? SC_K + 1u : SC_K));
    rb_close(&sup);
    shm_unlink(nm);
    munmap(rep, sizeof *rep);
}

/* 스레드 스트레스: 같은 매핑을 생산자 핸들·소비자 핸들이 나눠 쓴다. 끝 표시 = msg_seq N (재시도로 반드시 전달) */
typedef struct { rb_t *r; uint64_t n; uint64_t first, last, bad, order, end_retries; bool timeout; } stress_t;
static void *stress_prod(void *arg) {
    stress_t *s = arg;
    uint8_t buf[RB_MAX_PAYLOAD];
    for (uint64_t i = 0; i <= s->n; ) {
        uint32_t len = make_msg(i, buf);
        int rc = rb_send(s->r, buf, len, i, 2000);
        if (rc == RB_OK || (rc == RB_DROPPED && i < s->n)) { i++; continue; }
        if (rc == RB_DROPPED) { s->end_retries++; sched_yield(); continue; }   /* 끝 표시는 버려지면 다시 */
        s->timeout = true;
        return NULL;
    }
    return NULL;
}
static void *stress_cons(void *arg) {
    stress_t *s = arg;
    uint8_t buf[RB_MAX_PAYLOAD];
    rb_view_t v;
    s->first = UINT64_MAX;
    for (;;) {
        int rc = rb_recv(s->r, buf, sizeof buf, &v, 2000);
        if (rc != RB_OK) { s->timeout = true; return NULL; }
        if (!msg_ok(v.msg_seq, buf, v.len)) s->bad++;
        if (s->first == UINT64_MAX) s->first = v.msg_seq;
        else if (v.msg_seq <= s->last) s->order++;
        s->last = v.msg_seq;
        if (v.msg_seq == s->n) return NULL;
        if ((v.msg_seq & 1023u) == 0u) for (int k = 0; k < 2000; k++) cpu_relax();   /* 가끔 느린 소비자 */
    }
}
static void stress_policy(uint32_t policy, uint64_t n) {
    const char *nm = new_name();
    rb_t p = {0}, c = {0};
    if (rb_create(&p, nm, 64u, policy) != RB_OK || rb_attach(&c, nm, RB_CONSUMER) != RB_OK) { CHECK(!"create"); return; }
    rb_start(&c);
    rb_start(&p);
    p.stale_ns = c.stale_ns = 2000000000ull;          /* 같은 프로세스 — 스케줄 지연을 죽음으로 오인하지 않게 */
    stress_t sp = { .r = &p, .n = n }, sc = { .r = &c, .n = n };
    pthread_t tp, tc;
    pthread_create(&tc, NULL, stress_cons, &sc);
    pthread_create(&tp, NULL, stress_prod, &sp);
    pthread_join(tp, NULL);
    pthread_join(tc, NULL);
    uint64_t dropped = atomic_load(&p.h->dropped) - sp.end_retries;   /* 끝 표시 재시도는 유실이 아님 */
    static const char *const PN[] = { "BLOCK", "DROP_NEWEST", "OVERWRITE_OLDEST" };
    printf("  stress %-16s n=%llu received %llu lost %llu dropped %llu overruns %llu skipped %llu (정보용)\n",
           PN[policy], (unsigned long long)n, (unsigned long long)c.received, (unsigned long long)c.lost,
           (unsigned long long)dropped, (unsigned long long)c.overruns, (unsigned long long)c.skipped);
    CHECK(!sp.timeout && !sc.timeout && sc.bad == 0u && sc.order == 0u && c.torn_rejected == 0u);
    if (policy == RB_BLOCK) CHECK(c.received == n + 1u && c.lost == 0u && sc.first == 0u);
    if (policy == RB_DROP_NEWEST) CHECK(sc.first == 0u && c.lost == dropped && c.received + dropped == n + 1u);
    if (policy == RB_OVERWRITE_OLDEST) CHECK(c.received + c.lost + sc.first == n + 1u && c.skipped == c.lost + sc.first);
    rb_close(&p);
    rb_close(&c);
    shm_unlink(nm);
}

/* v2 작성자 흉내: 같은 이름에 다른 버전/레이아웃 헤더를 쓴다 (magic 은 유효) */
static void forge(const char *nm, uint16_t ver, uint16_t hsz, uint32_t slot, uint32_t cap, uint64_t total) {
    int fd = shm_open(nm, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) return;
    if (ftruncate(fd, 16384) == 0) {
        rb_hdr_t *h = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (h != MAP_FAILED) {
            h->version = ver; h->hdr_size = hsz; h->slot_size = slot; h->capacity = cap; h->total_size = total;
            atomic_store(&h->magic, RB_MAGIC);
            munmap(h, 16384);
        }
    }
    close(fd);
}

int main(void) {
    (void)backoff_wait; (void)slot_csum; (void)slot_read; (void)peer_dead_now; (void)beat;   /* starter: 미사용 경고 방지 */
    uint64_t t_start = now_ns();
    printf("OS-A12 robust shm IPC (pid %d%s)\n", (int)getpid(), SAN_BUILD ? ", sanitizer build" : "");
    uint8_t buf[RB_MAX_PAYLOAD], out[RB_MAX_PAYLOAD];
    rb_view_t v;

    /* ---------------- Part A ---------------- */
    printf("Part A — header / slot seqlock / checksum / gap\n");
    int fa = FAILS();
    {   /* A1 순수 함수: 메모리 위 헤더 */
        static rb_hdr_t hh;
        rb_hdr_t *h = &hh;
        size_t big = 1u << 20;
        h->version = 1; h->hdr_size = sizeof(rb_hdr_t); h->slot_size = RB_SLOT_BYTES; h->capacity = 8;
        h->total_size = sizeof(rb_hdr_t) + 8u * RB_SLOT_BYTES; h->policy = RB_BLOCK;
        CHECK(rb_validate_header(h, big) == RB_ENOTREADY);                  /* magic 아직 없음 */
        atomic_store(&h->magic, RB_MAGIC);
        CHECK(rb_validate_header(h, big) == RB_OK);
        CHECK(rb_validate_header(h, 100) == RB_ENOTREADY);                  /* 헤더보다 작다 */
        CHECK(rb_validate_header(h, h->total_size - 1u) == RB_ELAYOUT);      /* 매핑이 링보다 작다 */
        h->version = 2;  CHECK(rb_validate_header(h, big) == RB_EVERSION);  h->version = 1;
        h->slot_size = 512; CHECK(rb_validate_header(h, big) == RB_ELAYOUT); h->slot_size = RB_SLOT_BYTES;
        h->capacity = 6; CHECK(rb_validate_header(h, big) == RB_ELAYOUT);
        h->capacity = 0; CHECK(rb_validate_header(h, big) == RB_ELAYOUT);    h->capacity = 8;
        h->total_size += 8; CHECK(rb_validate_header(h, big) == RB_ELAYOUT); h->total_size -= 8;
        h->policy = 7;   CHECK(rb_validate_header(h, big) == RB_ELAYOUT);    h->policy = RB_BLOCK;
        CHECK(rb_validate_header(h, big) == RB_OK);
    }
    {   /* A1 shm 위: v2 작성자, 레이아웃 불일치, 크기 0 객체 */
        rb_t r = {0};
        const char *n2 = new_name(); forge(n2, 2, 512, RB_SLOT_BYTES, 8, 512u + 8u * RB_SLOT_BYTES);
        CHECK(rb_attach(&r, n2, RB_CONSUMER) == RB_EVERSION);
        const char *n3 = new_name(); forge(n3, 1, sizeof(rb_hdr_t), 512, 8, sizeof(rb_hdr_t) + 8u * 512u);
        CHECK(rb_attach(&r, n3, RB_CONSUMER) == RB_ELAYOUT);
        const char *n4 = new_name(); forge(n4, 1, sizeof(rb_hdr_t), RB_SLOT_BYTES, 4096, sizeof(rb_hdr_t) + 4096u * RB_SLOT_BYTES);
        CHECK(rb_attach(&r, n4, RB_CONSUMER) == RB_ELAYOUT);                /* 16 KB 객체에 1 MB 링이라고 주장 */
        const char *n5 = new_name(); int fd = shm_open(n5, O_CREAT | O_EXCL | O_RDWR, 0600); if (fd >= 0) close(fd);
        CHECK(rb_attach(&r, n5, RB_CONSUMER) == RB_ENOTREADY);              /* ftruncate 전에 죽은 흔적 */
        shm_unlink(n2); shm_unlink(n3); shm_unlink(n4); shm_unlink(n5);
    }
    {   /* A2/A3 기본 송수신 — 생산자·소비자가 서로 다른 매핑 */
        const char *nm = new_name();
        rb_t p = {0}, c = {0};
        CHECK(rb_create(&p, nm, 4u, RB_DROP_NEWEST) == RB_OK);
        CHECK(rb_create(&c, nm, 4u, RB_DROP_NEWEST) == RB_EXISTS);
        CHECK(rb_attach(&c, nm, RB_CONSUMER) == RB_OK && c.h != p.h && c.cap == 4u);
        struct stat st; int fd = shm_open(nm, O_RDONLY, 0);
        CHECK(fd >= 0 && fstat(fd, &st) == 0 && st.st_size % getpagesize() == 0 && (size_t)st.st_size >= p.h->total_size);
        if (fd >= 0) close(fd);
        rb_start(&p); rb_start(&c);
        CHECK(p.gen == 1u && c.gen == 1u && atomic_load(&p.h->prod_pid) == (uint32_t)getpid());
        CHECK(rb_recv(&c, out, sizeof out, &v, 0) == RB_EMPTY);
        uint32_t l0 = make_msg(0, buf);
        CHECK(rb_send(&p, buf, l0, 0, 0) == RB_OK && atomic_load(&p.h->tail) == 1u);
        CHECK(rb_recv(&c, out, sizeof out, &v, 0) == RB_OK && v.msg_seq == 0u && v.gen == 1u && msg_ok(0, out, v.len));
        CHECK(atomic_load(&p.h->head) == 1u && rb_recv(&c, out, sizeof out, &v, 0) == RB_EMPTY);
        CHECK(rb_send(&p, buf, RB_MAX_PAYLOAD + 1u, 1, 0) == RB_ETOOBIG);
        memset(buf, 0x5A, RB_MAX_PAYLOAD);
        CHECK(rb_send(&p, buf, RB_MAX_PAYLOAD, 1, 0) == RB_OK && rb_send(&p, buf, 0, 2, 0) == RB_OK);
        CHECK(rb_recv(&c, out, sizeof out, &v, 0) == RB_OK && v.len == RB_MAX_PAYLOAD && memcmp(out, buf, RB_MAX_PAYLOAD) == 0);
        CHECK(rb_recv(&c, out, sizeof out, &v, 0) == RB_OK && v.len == 0u && v.msg_seq == 2u);
        /* 쓰는 중(홀수 마커)인 슬롯은 안 보인다 */
        rb_slot_t *s3 = &p.slots[3u & p.mask];
        uint32_t l3 = make_msg(3, buf);
        CHECK(rb_send(&p, buf, l3, 3, 0) == RB_OK);
        atomic_store(&s3->w[0], 2u * 3u + 1u);
        CHECK(rb_recv(&c, out, sizeof out, &v, 0) == RB_EMPTY);
        atomic_store(&s3->w[0], 2u * 3u + 2u);
        /* 게시된 슬롯의 바이트 하나가 깨지면 체크섬이 거부 — 같은 위치는 한 번만 센다 */
        atomic_fetch_xor(&s3->w[RB_META_WORDS + 1], 0x10u);
        CHECK(rb_recv(&c, out, sizeof out, &v, 0) == RB_CORRUPT && rb_recv(&c, out, sizeof out, &v, 0) == RB_CORRUPT);
        CHECK(c.torn_rejected == 1u && c.pos == 3u);
        atomic_fetch_xor(&s3->w[RB_META_WORDS + 1], 0x10u);
        CHECK(rb_recv(&c, out, sizeof out, &v, 0) == RB_OK && v.msg_seq == 3u && msg_ok(3, out, v.len));
        /* DROP_NEWEST + gap 으로 유실 감지: 4칸에 4..9 → 4개 OK, 2개 DROPPED */
        int ok = 0, dr = 0;
        for (uint64_t s = 4; s < 10; s++) { int rc = rb_send(&p, buf, make_msg(s, buf), s, 0); ok += rc == RB_OK; dr += rc == RB_DROPPED; }
        CHECK(ok == 4 && dr == 2 && atomic_load(&p.h->dropped) == 2u);
        bool seq_ok = true;
        for (uint64_t s = 4; s < 8; s++) seq_ok = seq_ok && rb_recv(&c, out, sizeof out, &v, 0) == RB_OK && v.msg_seq == s;
        CHECK(seq_ok && c.lost == 0u);
        CHECK(rb_send(&p, buf, make_msg(10, buf), 10, 0) == RB_OK);
        CHECK(rb_recv(&c, out, sizeof out, &v, 0) == RB_OK && v.msg_seq == 10u && c.lost == 2u);   /* 8, 9 유실 */
        CHECK(c.lost == atomic_load(&p.h->dropped) && c.received == 9u && c.dups == 0u);
        rb_close(&p); rb_close(&c);
        shm_unlink(nm);
    }
    bool a_ok = FAILS() == fa;

    /* ---------------- Part B ---------------- */
    printf("Part B — BLOCK / DROP_NEWEST / OVERWRITE_OLDEST\n");
    int fb = FAILS();
    {
        const char *nm = new_name();
        rb_t p = {0}, c = {0};
        CHECK(rb_create(&p, nm, 4u, RB_BLOCK) == RB_OK && rb_attach(&c, nm, RB_CONSUMER) == RB_OK);
        rb_start(&c); rb_start(&p);
        int ok = 0;
        for (uint64_t s = 0; s < 4; s++) ok += rb_send(&p, buf, make_msg(s, buf), s, 0) == RB_OK;
        CHECK(ok == 4);
        CHECK(rb_send(&p, buf, make_msg(4, buf), 4, 0) == RB_FULL);        /* 기다리지 않음 */
        CHECK(rb_send(&p, buf, make_msg(4, buf), 4, 20) == RB_TIMEOUT);   /* 상대는 살아 있음 → 시간 초과 */
        CHECK(rb_recv(&c, out, sizeof out, &v, 0) == RB_OK && v.msg_seq == 0u);
        CHECK(rb_send(&p, buf, make_msg(4, buf), 4, 0) == RB_OK && atomic_load(&p.h->tail) == 5u);
        /* 세대 울타리(fencing): 새 생산자가 시작하면 옛 핸들은 더 못 쓴다 */
        rb_t p2 = {0};
        CHECK(rb_attach(&p2, nm, RB_PRODUCER) == RB_OK && rb_start(&p2) == RB_OK && p2.gen == 2u && p2.pos == 5u);
        CHECK(rb_send(&p, buf, make_msg(5, buf), 5, 0) == RB_FENCED);
        CHECK(p2.next_msg_hint == 5u && p2.rolled == 0u);
        rb_close(&p2); rb_close(&p); rb_close(&c);
        shm_unlink(nm);
    }
    {   /* OVERWRITE_OLDEST: 생산자는 안 기다리고, 늦은 소비자는 최신 창으로 점프 */
        const char *nm = new_name();
        rb_t p = {0}, c = {0};
        CHECK(rb_create(&p, nm, 4u, RB_OVERWRITE_OLDEST) == RB_OK && rb_attach(&c, nm, RB_CONSUMER) == RB_OK);
        rb_start(&c); rb_start(&p);
        CHECK(rb_send(&p, buf, make_msg(0, buf), 0, 0) == RB_OK);
        CHECK(rb_recv(&c, out, sizeof out, &v, 0) == RB_OK && v.msg_seq == 0u);
        int ok = 0;
        for (uint64_t s = 1; s <= 10; s++) ok += rb_send(&p, buf, make_msg(s, buf), s, 0) == RB_OK;
        CHECK(ok == 10 && atomic_load(&p.h->tail) == 11u);
        CHECK(rb_recv(&c, out, sizeof out, &v, 0) == RB_OK && v.msg_seq == 8u && msg_ok(8, out, v.len));   /* 11-4+1 */
        CHECK(c.overruns == 1u && c.skipped == 7u && c.lost == 7u);
        CHECK(rb_recv(&c, out, sizeof out, &v, 0) == RB_OK && v.msg_seq == 9u);
        CHECK(rb_recv(&c, out, sizeof out, &v, 0) == RB_OK && v.msg_seq == 10u);
        CHECK(rb_recv(&c, out, sizeof out, &v, 0) == RB_EMPTY && c.torn_rejected == 0u);
        rb_close(&p); rb_close(&c);
        shm_unlink(nm);
    }
    if (a_ok && FAILS() == fb) {
        uint64_t n = SAN_BUILD ? 20000u : 200000u;
        stress_policy(RB_BLOCK, n);
        stress_policy(RB_DROP_NEWEST, n);
        stress_policy(RB_OVERWRITE_OLDEST, n);
    } else {
        printf("SKIP 6 stress checks (Part B): unit checks failed\n");
        g_total += 6;
    }
    bool b_ok = FAILS() == fb;

    /* ---------------- Part C ---------------- */
    printf("Part C — liveness, restart, crash injection\n");
    int fc = FAILS();
    {   /* C1 가짜 시계로: 하트비트 정체 → STALE, ESRCH → GONE, 좀비는 kill 로는 "살아 있음" */
        const char *nm = new_name();
        rb_t p = {0}, c = {0};
        CHECK(rb_create(&p, nm, 4u, RB_BLOCK) == RB_OK && rb_attach(&c, nm, RB_CONSUMER) == RB_OK);
        rb_hdr_t *h = p.h;
        const uint64_t S = 100u, T0 = 1000000u;
        c.stale_ns = S;
        CHECK(rb_peer_state(&c, T0) == PEER_NONE);                 /* 생산자 없음 */
        atomic_store(&h->prod_pid, (uint32_t)getpid()); atomic_store(&h->prod_gen, 1u);
        CHECK(rb_peer_state(&c, T0) == PEER_ALIVE);
        CHECK(rb_peer_state(&c, T0 + S - 1u) == PEER_ALIVE);
        CHECK(rb_peer_state(&c, T0 + S) == PEER_STALE);             /* 정확히 S 만큼 정체 */
        atomic_fetch_add(&h->prod_hb, 1u);
        CHECK(rb_peer_state(&c, T0 + S + 1u) == PEER_ALIVE);        /* 박동이 오면 회복 */
        CHECK(rb_peer_state(&c, T0 + 2u * S) == PEER_ALIVE && rb_peer_state(&c, T0 + 2u * S + 1u) == PEER_STALE);
        atomic_store(&h->prod_gen, 2u);
        CHECK(rb_peer_state(&c, T0 + 5u * S) == PEER_ALIVE);        /* 새 세대 = 새 감시 */
        fflush(stdout);
        pid_t z = fork();
        if (z == 0) _exit(0);
        siginfo_t si;
        memset(&si, 0, sizeof si);
        CHECK(waitid(P_PID, (id_t)z, &si, WEXITED | WNOWAIT) == 0);  /* 끝났지만 아직 회수 안 함 = 좀비 */
        atomic_store(&h->prod_pid, (uint32_t)z); atomic_store(&h->prod_gen, 3u);
        CHECK(rb_peer_state(&c, T0) == PEER_ALIVE && kill(z, 0) == 0);              /* 좀비는 kill 에 "있음" */
        CHECK(rb_peer_state(&c, T0 + S) == PEER_STALE);                              /* 하트비트가 잡아낸다 */
        int st; waitpid(z, &st, 0);
        CHECK(rb_peer_state(&c, T0 + S + 1u) == PEER_GONE);                          /* 회수 후 ESRCH */
        atomic_store(&h->prod_pid, 0u);
        CHECK(rb_peer_state(&c, T0) == PEER_NONE);
        rb_close(&c); rb_close(&p);
        shm_unlink(nm);
    }
    {   /* C2 복구: 게시됐지만 tail 이 안 오른 칸은 roll-forward, 쓰다 만 칸은 다시 쓴다 */
        const char *nm = new_name();
        rb_t p = {0}, c = {0}, q = {0};
        CHECK(rb_create(&p, nm, 8u, RB_BLOCK) == RB_OK && rb_attach(&c, nm, RB_CONSUMER) == RB_OK);
        rb_start(&c); rb_start(&p);
        for (uint64_t s = 0; s < 3; s++) (void)rb_send(&p, buf, make_msg(s, buf), s, 0);
        slot_publish(&p, 3, 3, buf, make_msg(3, buf));              /* "tail 올리기 직전에 죽음" */
        CHECK(atomic_load(&p.h->tail) == 3u);
        CHECK(rb_attach(&q, nm, RB_PRODUCER) == RB_OK && rb_start(&q) == RB_OK);
        CHECK(q.gen == 2u && q.rolled == 1u && q.pos == 4u && q.next_msg_hint == 4u && atomic_load(&p.h->tail) == 4u);
        rb_close(&q);
        atomic_store(&p.slots[4].w[0], 2u * 4u + 1u);              /* 위치 4 를 쓰다 죽음 */
        CHECK(rb_attach(&q, nm, RB_PRODUCER) == RB_OK && rb_start(&q) == RB_OK);
        CHECK(q.gen == 3u && q.rolled == 0u && q.pos == 4u && q.next_msg_hint == 4u);
        CHECK(rb_send(&q, buf, make_msg(4, buf), 4, 0) == RB_OK);
        bool all = true;
        for (uint64_t s = 0; s < 5; s++) all = all && rb_recv(&c, out, sizeof out, &v, 0) == RB_OK && v.msg_seq == s && msg_ok(s, out, v.len);
        CHECK(all && c.resyncs == 1u && c.lost == 0u);              /* 메시지 gen: 1,1,1,1,3 (gen 2 는 안 씀) */
        CHECK(rb_send(&p, buf, make_msg(5, buf), 5, 0) == RB_FENCED);
        rb_close(&q); rb_close(&c); rb_close(&p);
        shm_unlink(nm);
    }
    bool c_ok = FAILS() == fc;
    if (a_ok && b_ok && c_ok) {
        /* 헤더 쓰다 죽음 → 소비자는 ENOTREADY (멈추지도, 쓰레기를 읽지도 않음) → 재시작한 생산자가 다시 초기화 */
        const char *nm = new_name();
        fflush(stdout);
        pid_t p1 = fork();
        if (p1 == 0) { alarm(5); rb_t r = {0}; g_crash_cp = CP_HDR_MID; g_crash_msg = 0; (void)rb_create(&r, nm, 8u, RB_BLOCK); _exit(30); }
        int s1 = reap(p1, 3000);
        rb_t c = {0};
        CHECK(killed_by(s1, SIGKILL) && rb_attach(&c, nm, RB_CONSUMER) == RB_ENOTREADY);
        pid_t p2 = fork();
        if (p2 == 0) {
            alarm(5);
            rb_t r = {0};
            if (rb_create(&r, nm, 8u, RB_BLOCK) != RB_OK) _exit(31);
            rb_start(&r);
            for (uint64_t s = 0; s < 5; s++) if (rb_send(&r, buf, make_msg(s, buf), s, 0) != RB_OK) _exit(32);
            _exit(0);                                                /* rb_close 없이 끝: 데이터는 남는다 */
        }
        int s2 = reap(p2, 3000);
        CHECK(exited_ok(s2) && rb_attach(&c, nm, RB_CONSUMER) == RB_OK);
        rb_start(&c);
        bool all = true;
        for (uint64_t s = 0; s < 5; s++) all = all && rb_recv(&c, out, sizeof out, &v, 100) == RB_OK && v.msg_seq == s;
        CHECK(all && rb_recv(&c, out, sizeof out, &v, 0) == RB_EMPTY);   /* 죽은 생산자가 남긴 메시지도 받는다 */
        rb_close(&c);
        shm_unlink(nm);
        printf("  hdr_mid: P1 killed in header init → attach ENOTREADY → P2 re-init → 5 msgs delivered\n");

        static const crash_point_t cps[] = { CP_SLOT_CLAIMED, CP_PAYLOAD_HALF, CP_SEQ_BEFORE_CSUM,
                                             CP_BEFORE_PUBLISH, CP_BEFORE_TAIL, CP_AFTER_SEND,
                                             CP_BUG_EARLY_PUBLISH };
        for (size_t i = 0; i < sizeof cps / sizeof cps[0]; i++) scenario_producer_crash(cps[i]);
        scenario_consumer_crash(0);
        scenario_consumer_crash(1);
    } else {
        printf("SKIP 46 crash-injection checks (Part C): unit checks failed\n");
        g_total += 46;
    }

    /* 이름 누수 없음: 쓴 이름은 전부 사라졌어야 한다 */
    int leaked = 0;
    for (int i = 0; i < g_nnames; i++) {
        int fd = shm_open(g_names[i], O_RDONLY, 0);
        if (fd >= 0) { leaked++; close(fd); shm_unlink(g_names[i]); }
    }
    CHECK(leaked == 0);
    printf("(elapsed %.2f s, 정보용)\n", (double)(now_ns() - t_start) / 1e9);
    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
