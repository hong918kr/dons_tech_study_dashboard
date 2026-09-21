// 05_linux_io.c  —  PRACTICE STUB (직접 채워넣기)
// 임베디드 Linux I/O & 이벤트 루프 (Embedded Linux I/O & Event Loops)  —  Q39~Q48
// ---------------------------------------------------------------------------
// 빌드/실행:  make prob N=05_linux_io
//   또는:     cc -std=c11 -Wall -Wextra -O1 -g -pthread 05_linux_io.c -o /tmp/vk05p && /tmp/vk05p
//
// 각 함수의 '// TODO' 를 구현하고 다시 실행 -> [FAIL] 이 [PASS] 로 바뀌면 성공.
// (미구현 상태에서도 컴파일/실행은 되며 대부분 FAIL 로 뜬다. 모든 poll 은 타임아웃이
//  걸려 있어 절대 멈추지 않는다.)
//
// Verkada Connectivity 팀의 게이트웨이 데몬(GC31-E/GW31-E)은 결국 "하나의 이벤트 루프"다.
// 모뎀 제어 채널(AT/UART), 클라우드(Command) 소켓, 재연결 타이머, 종료 시그널을
// 한 루프에서 멀티플렉싱한다. 이 세트는 그 루프의 부품을 하나씩 드릴한다.
//
// ⚠️ 이식성: macOS(Apple clang, arm64)에서 돌아야 한다. POSIX 만 쓸 것
//    (poll/pipe/fcntl/clock_gettime/sigaction/rename). epoll/timerfd/signalfd 는
//    notes/05_linux_io.md 에서 "이론"으로 공부한다 — 면접에서는 그쪽을 말해야 한다.
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

// ===========================================================================
// 테스트 하네스 (건드리지 말 것)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// 하네스 전용 시계 (건드리지 말 것) — 학생 코드(now_ms)와 독립적이어야 한다.
static uint64_t hms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

/* ---------------------------------------------------------------------------
 * Q39.  read_full / write_full  —  부분 I/O 루프 + EINTR 재시도
 *   KO: read()/write() 는 요청한 바이트 수를 보장하지 않는다(short read/write).
 *       read_full 은 n 바이트를 다 읽거나 EOF 를 만날 때까지 반복하고 **실제로 읽은
 *       바이트 수**를 리턴한다(n 보다 작아도 에러 아님). write_full 은 n 바이트를
 *       전부 밀어넣고 n 을 리턴한다. 둘 다 -1/EINTR 은 재시도, 그 외 -1 은 실패.
 *   EN: Loop until all n bytes are transferred; retry on EINTR; a short read is
 *       EOF, not an error. Return bytes moved, or -1 on a real error.
 *   ex: 파이프에 10바이트만 있고 쓰기단이 닫힘 -> read_full(fd, buf, 100) == 10
 * ------------------------------------------------------------------------- */
ssize_t read_full(int fd, void *buf, size_t n) {
    (void)fd; (void)buf; (void)n;
    // TODO: implement
    return -1;
}

ssize_t write_full(int fd, const void *buf, size_t n) {
    (void)fd; (void)buf; (void)n;
    // TODO: implement
    return -1;
}

// Q40 의 리턴 타입 (건드리지 말 것)
typedef enum {
    IO_OK         =  0,   // out_n 바이트를 읽었다
    IO_WOULDBLOCK =  1,   // 지금은 데이터 없음 (EAGAIN/EWOULDBLOCK)
    IO_EOF        =  2,   // 상대가 닫음 (read == 0)
    IO_ERROR      = -1    // 진짜 에러
} io_res_t;

/* ---------------------------------------------------------------------------
 * Q40.  논블로킹 fd 설정 + EAGAIN/EWOULDBLOCK 처리
 *   KO: set_nonblocking 은 fcntl(F_GETFL) 로 **현재 플래그를 읽어** O_NONBLOCK 을
 *       OR 한 뒤 F_SETFL 한다(덮어쓰면 O_RDONLY 등이 날아간다). 성공 0, 실패 -1.
 *       read_nb 는 read 결과를 4가지 상태로 분류한다: 데이터(IO_OK, *out_n 세팅),
 *       지금은 없음(IO_WOULDBLOCK), 상대 종료(IO_EOF), 진짜 에러(IO_ERROR).
 *       EINTR 은 재시도. EAGAIN 과 EWOULDBLOCK 을 **둘 다** 검사할 것.
 *   EN: Set O_NONBLOCK with a read-modify-write fcntl; classify read() results
 *       into OK / WOULDBLOCK / EOF / ERROR, retrying on EINTR.
 *   ex: 빈 논블로킹 파이프 -> read_nb(...) == IO_WOULDBLOCK, *out_n == 0
 * ------------------------------------------------------------------------- */
int set_nonblocking(int fd) {
    (void)fd;
    // TODO: implement
    return -1;
}

io_res_t read_nb(int fd, void *buf, size_t n, size_t *out_n) {
    (void)fd; (void)buf; (void)n;
    if (out_n) *out_n = 0;
    // TODO: implement
    return IO_ERROR;
}

// Q41 의 통계 구조체 (건드리지 말 것)
typedef struct {
    size_t bytes_a;     // fd_a 에서 읽은 총 바이트
    size_t bytes_b;     // fd_b 에서 읽은 총 바이트
    int    timeouts;    // poll 타임아웃 횟수
    int    eof_a;       // fd_a 가 EOF 인가
    int    eof_b;       // fd_b 가 EOF 인가
    int    iters;       // 루프 회전 수 (스핀 감지용)
} pump_t;

/* ---------------------------------------------------------------------------
 * Q41.  poll() 기반 이벤트 루프 — 두 fd 멀티플렉싱 + 타임아웃
 *   KO: st 를 **직접 0 으로 초기화**한 뒤, fd_a/fd_b 를 poll 로 동시에 감시한다.
 *       읽은 바이트는 bytes_a/bytes_b 에 누적, read==0 이면 해당 eof_* 를 세우고
 *       **다음 회전의 pollfd 배열에서 제외**한다(안 빼면 POLLHUP 으로 100% CPU).
 *       poll 이 -1/EINTR 이면 재시도, 0(타임아웃)이면 timeouts++ 후 0 리턴.
 *       두 fd 모두 EOF 가 되면 0 리턴. 진짜 에러는 -1. 매 회전마다 iters++.
 *   EN: Multiplex two fds with poll(); accumulate bytes, drop fds that hit EOF,
 *       retry on EINTR, and return 0 on idle timeout or when both are closed.
 *   ex: a에 "hello", b에 "worldzz" 쓰고 두 쓰기단 close -> bytes_a==5, bytes_b==7
 * ------------------------------------------------------------------------- */
int pump_two(int fd_a, int fd_b, int timeout_ms, pump_t *st) {
    (void)fd_a; (void)fd_b; (void)timeout_ms; (void)st;
    // TODO: implement
    return -1;
}

// Q42 의 self-pipe 핸들 (건드리지 말 것)
typedef struct { int rd; int wr; } selfpipe_t;

// (제공됨) fd 에 FD_CLOEXEC 를 세운다 — exec 할 때 fd 가 자식으로 새지 않게.
int set_cloexec(int fd) {
    int f = fcntl(fd, F_GETFD, 0);
    if (f < 0) return -1;
    return fcntl(fd, F_SETFD, f | FD_CLOEXEC);
}

/* ---------------------------------------------------------------------------
 * Q42.  self-pipe trick — 다른 스레드/시그널 핸들러에서 이벤트 루프 깨우기
 *   KO: sp_init 은 pipe() 를 만들고 **양쪽 끝을 O_NONBLOCK + FD_CLOEXEC** 로 만든다
 *       (실패 시 이미 연 fd 를 닫고 rd/wr 를 -1 로). sp_notify 는 1바이트를 write 해
 *       poll 을 깨운다 — EAGAIN(파이프가 이미 참)은 "이미 깨울 예정"이므로 **성공**으로
 *       취급하고 0 을 리턴한다. sp_drain 은 EAGAIN 이 날 때까지 다 읽고 읽은 바이트
 *       수를 리턴한다(안 비우면 poll 이 계속 즉시 리턴 = busy loop). sp_close 는 두
 *       fd 를 닫고 -1 로 되돌린다.
 *   EN: Build a non-blocking, close-on-exec pipe; notify by writing one byte
 *       (EAGAIN means a wakeup is already pending), and drain until EAGAIN.
 *   ex: sp_notify x3 -> sp_drain() == 3, 그 후 poll(rd, 0ms) == 0
 * ------------------------------------------------------------------------- */
int sp_init(selfpipe_t *sp) {
    if (sp) { sp->rd = -1; sp->wr = -1; }
    // TODO: implement
    return -1;
}

int sp_notify(const selfpipe_t *sp) {
    (void)sp;
    // TODO: implement  (시그널 핸들러에서도 안전해야 한다 — write 만 쓸 것)
    return -1;
}

int sp_drain(const selfpipe_t *sp) {
    (void)sp;
    // TODO: implement
    return -1;
}

void sp_close(selfpipe_t *sp) {
    (void)sp;
    // TODO: implement
}

// Q43 의 리더 상태 (건드리지 말 것)
#define LR_CAP 128
typedef void (*lr_cb_t)(const char *line, void *user);

typedef struct {
    char   buf[LR_CAP];   // 누적 버퍼 ('\0' 자리 1칸 포함)
    size_t len;           // 현재 누적 길이
    int    overflow;      // 현재 줄이 이미 넘쳤는가 (줄 끝까지 버리는 중)
    int    dropped;       // 버린 줄 수
} linereader_t;

/* ---------------------------------------------------------------------------
 * Q43.  라인 프레이밍 리더 — 부분 수신을 누적해 완전한 줄만 넘긴다
 *   KO: TCP/UART 는 메시지가 아니라 바이트 스트림이다. lr_feed 는 data[0..n) 를
 *       누적하다가 '\n' 을 만나면 그 줄을 '\0' 종료해 cb(line, user) 로 넘기고
 *       버퍼를 비운다. 끝의 '\r' 은 떼고(CRLF), 빈 줄도 유효한 줄이다.
 *       줄이 LR_CAP-1 보다 길면 overflow 를 세우고 dropped++ 한 뒤 **그 줄 끝까지
 *       버리고** 다음 줄부터 정상 복구한다(절대 버퍼 오버런 금지).
 *       리턴 = 이번 호출에서 넘긴 줄 수. lr_init 은 모든 필드를 리셋한다.
 *   EN: Stateful line framer: accumulate bytes, emit one callback per complete
 *       line, strip CRLF, and drop (but count) lines longer than the buffer.
 *   ex: feed("AT+CS") -> 0줄, 이어서 feed("Q\r\nOK\r\n") -> 2줄 "AT+CSQ","OK"
 * ------------------------------------------------------------------------- */
void lr_init(linereader_t *lr) {
    (void)lr;
    // TODO: implement
}

int lr_feed(linereader_t *lr, const char *data, size_t n, lr_cb_t cb, void *user) {
    (void)lr; (void)data; (void)n; (void)cb; (void)user;
    // TODO: implement
    return 0;
}

/* ---------------------------------------------------------------------------
 * Q44.  지수 백오프 + 지터 (순수 함수)
 *   KO: delay = base_ms * 2^attempt 를 cap_ms 에서 클램프한다(오버플로 주의 —
 *       attempt 가 커도 절대 랩어라운드하면 안 된다). jitter_pct 가 0 이 아니면
 *       결과를 [d-span, d+span] (span = d*jitter_pct/100) 로 흔든다. 난수는
 *       **주입**된다: rnd 는 0..9999, rnd=0 이면 하한, rnd=9999 면 상한.
 *       (난수를 안에서 뽑지 않는 이유 = 순수 함수라야 단위 테스트가 된다.)
 *   EN: Pure exponential backoff with cap and injected jitter, no overflow.
 *   ex: (attempt=3, base=500, cap=30000, jitter=20, rnd=0) -> 3200
 * ------------------------------------------------------------------------- */
uint32_t backoff_delay_ms(uint32_t attempt, uint32_t base_ms, uint32_t cap_ms,
                          uint32_t jitter_pct, uint32_t rnd) {
    (void)attempt; (void)base_ms; (void)cap_ms; (void)jitter_pct; (void)rnd;
    // TODO: implement
    return 0;
}

/* ---------------------------------------------------------------------------
 * Q45.  monotonic deadline 유틸
 *   KO: now_ms 는 CLOCK_MONOTONIC 기반 밀리초를 리턴한다(CLOCK_REALTIME 금지 —
 *       NTP 가 시계를 되돌리면 타임아웃이 몇 시간 늘어난다). ms_remaining 은
 *       deadline 까지 남은 ms 를 리턴하되 **이미 지났으면 0** 으로 클램프한다
 *       (poll 에 음수를 주면 무한 대기가 된다!). poll_timeout_from 은 남은 시간을
 *       cap_ms 로 상한 클램프해 poll 에 넘길 int 를 만든다. deadline==0 은
 *       "마감 없음"이라 cap_ms 를 그대로 리턴한다.
 *   EN: Monotonic now/remaining/poll-timeout helpers; clamp the past to 0 and
 *       cap long waits so the loop still wakes to kick the watchdog.
 *   ex: poll_timeout_from(now_ms()+100000, 1000) == 1000
 * ------------------------------------------------------------------------- */
uint64_t now_ms(void) {
    // TODO: implement
    return 0;
}

int64_t ms_remaining(uint64_t deadline_ms) {
    (void)deadline_ms;
    // TODO: implement
    return 0;
}

int poll_timeout_from(uint64_t deadline_ms, int cap_ms) {
    (void)deadline_ms; (void)cap_ms;
    // TODO: implement
    return 0;
}

// (제공됨) path 의 디렉토리 부분을 out 에 담는다. '/' 가 없으면 ".".
const char *dir_of(const char *path, char *out, size_t cap) {
    const char *slash = strrchr(path, '/');
    if (!slash) { snprintf(out, cap, "."); return out; }
    size_t len = (size_t)(slash - path);
    if (len == 0) { snprintf(out, cap, "/"); return out; }
    if (len >= cap) len = cap - 1;
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

/* ---------------------------------------------------------------------------
 * Q46.  원자적 설정 저장 — temp write + fsync + rename
 *   KO: 야외 게이트웨이는 전원이 툭 끊긴다. 제자리 덮어쓰기는 반쪽 파일을 남긴다.
 *       레시피: ① 같은 디렉토리에 "<path>.tmpXXXXXX" 를 mkstemp 로 만들고
 *       ② write_full 로 n 바이트 기록 ③ fsync(파일) ④ close
 *       ⑤ chmod 0644 (mkstemp 는 0600) ⑥ rename(tmp, path) — POSIX 원자성
 *       ⑦ dir_of() 로 디렉토리를 열어 fsync (안 하면 rename 이 날아갈 수 있다).
 *       어느 단계에서 실패하든 **임시 파일을 unlink** 하고 fd 를 닫을 것(goto cleanup).
 *       성공 0, 실패 -1.
 *   EN: Write to a temp file in the same directory, fsync, rename, then fsync
 *       the directory; clean up the temp file on any failure.
 *   ex: atomic_save(p,"{\"v\":1}",7)==0, 그 후 디렉토리에 .tmp 파일이 남지 않음
 * ------------------------------------------------------------------------- */
int atomic_save(const char *path, const void *data, size_t n) {
    (void)path; (void)data; (void)n;
    // TODO: implement
    return -1;
}

// Q47 의 종료 플래그 (건드리지 말 것) — 시그널 핸들러가 쓸 수 있는 유일한 타입
static volatile sig_atomic_t g_stop = 0;

/* ---------------------------------------------------------------------------
 * Q47.  시그널 안전 종료 — volatile sig_atomic_t + sigaction + EINTR
 *   KO: on_stop_signal 은 **g_stop = 1 만** 한다(핸들러에서는 async-signal-safe
 *       함수만 호출 가능 — printf/malloc 금지). install_stop_handler 는 signal()
 *       대신 sigaction() 으로 핸들러를 등록하고, **SA_RESTART 를 켜지 않는다**
 *       (그래야 poll 이 EINTR 로 즉시 깨어난다). 성공 0, 실패 -1.
 *       wait_for_stop 은 budget_ms 예산 안에서 fd 를 poll 한다: 루프 상단에서
 *       g_stop 을 먼저 보고 1 리턴, poll 이 -1/EINTR 이면 (*eintr_count)++ 후 재시도,
 *       타임아웃/예산 소진이면 0, fd 가 읽기 가능하면 2, 진짜 에러면 -1.
 *       남은 시간은 **매 회전마다 재계산**해야 한다(EINTR 마다 리셋되면 안 됨).
 *   EN: Handler only sets a volatile sig_atomic_t; sigaction without SA_RESTART;
 *       the poll loop treats EINTR as "check the flag and recompute the timeout".
 *   ex: 80ms 뒤 SIGALRM -> wait_for_stop(fd, 3000, &ec) == 1, ec >= 1
 * ------------------------------------------------------------------------- */
void on_stop_signal(int signo) {
    (void)signo;
    // TODO: implement  (여기서 할 수 있는 일은 플래그 세팅뿐)
}

int install_stop_handler(int signo) {
    (void)signo;
    // TODO: implement  (sigaction 사용, SA_RESTART 금지)
    return -1;
}

int wait_for_stop(int fd, int budget_ms, int *eintr_count) {
    (void)fd; (void)budget_ms;
    if (eintr_count) *eintr_count = 0;
    // TODO: implement
    return -1;
}

/* ---------------------------------------------------------------------------
 * Q48.  에러 경로 정리 — fd 누수 없는 goto cleanup + close()/EINTR
 *   KO: xclose 는 fd<0 이면 0(no-op), close 가 EINTR 이면 **재시도하지 않고** 0 을
 *       리턴한다 — Linux 에서 close 는 EINTR 여도 fd 를 이미 반납했으므로 재시도하면
 *       다른 스레드가 방금 연 같은 번호의 fd 를 닫아버린다. 그 외 실패는 -1.
 *       copy_file_prefix 는 src 를 열고 dst 를 O_WRONLY|O_CREAT|O_TRUNC 로 연 뒤
 *       앞 max_bytes 바이트를 read_full/write_full 로 복사한다. **모든 실패는
 *       goto cleanup 으로 모아** fd/메모리를 정확히 한 번씩 해제한다.
 *       리턴 = 복사한 바이트 수(EOF 면 더 적을 수 있음), 실패 -1.
 *   EN: Single-exit goto cleanup so no path leaks an fd; never retry close() on
 *       EINTR (the descriptor is already gone).
 *   ex: 존재하지 않는 src 로 400번 호출해도 열린 fd 개수가 늘지 않아야 한다
 * ------------------------------------------------------------------------- */
int xclose(int fd) {
    (void)fd;
    // TODO: implement
    return -1;
}

long copy_file_prefix(const char *src, const char *dst, size_t max_bytes) {
    (void)src; (void)dst; (void)max_bytes;
    // TODO: implement
    return -1;
}

// ===========================================================================
// 테스트 보조 + main (건드리지 말 것 — 정답 파일과 동일)
// ===========================================================================
typedef struct { int fd; size_t got; int ok; } drain_arg_t;

// Q39 쓰기 테스트용 소비자 스레드. 1초 유휴면 스스로 빠져나온다(스텁에서도 안 멈춤).
static void *drain_thread(void *p) {
    drain_arg_t *a = (drain_arg_t *)p;
    unsigned char buf[4096];
    a->ok = 1;
    for (;;) {
        struct pollfd pf = { a->fd, POLLIN, 0 };
        int r = poll(&pf, 1, 1000);
        if (r <= 0) break;                        // 타임아웃/에러 → 탈출
        ssize_t k = read(a->fd, buf, sizeof buf);
        if (k < 0) { if (errno == EINTR) continue; a->ok = 0; break; }
        if (k == 0) break;                        // EOF
        for (ssize_t i = 0; i < k; ++i)
            if (buf[i] != (unsigned char)((a->got + (size_t)i) & 0xFF)) a->ok = 0;
        a->got += (size_t)k;
    }
    return NULL;
}

// Q42 깨우기 테스트용 스레드
static void *notify_thread(void *p) {
    selfpipe_t *sp = (selfpipe_t *)p;
    sp_notify(sp);
    return NULL;
}

// Q43 콜백: 받은 줄을 모은다
#define LINE_MAX_N 8
typedef struct { char line[LINE_MAX_N][LR_CAP]; int n; } collect_t;
static void collect_cb(const char *line, void *user) {
    collect_t *c = (collect_t *)user;
    if (c->n < LINE_MAX_N) {
        snprintf(c->line[c->n], LR_CAP, "%s", line);
        c->n++;
    }
}

// 디렉토리에 이름이 sub 를 포함하는 엔트리가 몇 개인가 (Q46 임시파일 잔존 검사)
static int count_entries_with(const char *dir, const char *sub) {
    DIR *d = opendir(dir);
    if (!d) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL)
        if (strstr(e->d_name, sub)) n++;
    closedir(d);
    return n;
}

static long read_file_into(const char *path, char *out, size_t cap) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t k = read_full(fd, out, cap - 1);
    close(fd);
    if (k < 0) return -1;
    out[k] = '\0';
    return (long)k;
}

// ===========================================================================
// main
// ===========================================================================
int main(void) {
    // 데몬 관례: 파이프 상대가 사라져도 프로세스가 죽지 않게 SIGPIPE 무시 → EPIPE 로 받는다.
    signal(SIGPIPE, SIG_IGN);

    char dirtpl[] = "/tmp/vk05io_XXXXXX";
    const char *tdir = mkdtemp(dirtpl);
    if (!tdir) { perror("mkdtemp"); return 1; }
    char pathA[256], pathB[256], pathC[256];
    snprintf(pathA, sizeof pathA, "%s/config.json", tdir);
    snprintf(pathB, sizeof pathB, "%s/blob.bin",    tdir);
    snprintf(pathC, sizeof pathC, "%s/copy.bin",    tdir);

    // ---------------------------------------------------------------- Q39
    printf("== Q39 read_full / write_full (부분 I/O + EINTR) ==\n");
    {
        // (1) 파일 왕복
        int fd = open(pathB, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        unsigned char *src = (unsigned char *)malloc(4096);
        for (int i = 0; i < 4096; ++i) src[i] = (unsigned char)(i & 0xFF);
        ssize_t w = write_full(fd, src, 4096);
        close(fd);
        T("write_full writes all 4096 bytes", w == 4096);

        unsigned char *back = (unsigned char *)calloc(1, 4096);
        fd = open(pathB, O_RDONLY);
        ssize_t r = read_full(fd, back, 4096);
        close(fd);
        T("read_full reads all 4096 bytes", r == 4096);
        T("read_full round-trip content matches", memcmp(src, back, 4096) == 0);
        free(src); free(back);

        // (2) 짧은 read 는 에러가 아니다 — EOF 에서 부분 반환
        int p[2];
        if (pipe(p) == 0) {
            ssize_t k = write(p[1], "hello12345", 10);
            (void)k;
            close(p[1]);
            char b[100];
            ssize_t got = read_full(p[0], b, sizeof b);
            close(p[0]);
            T("read_full returns short count at EOF (not an error)", got == 10);
        } else {
            T("read_full returns short count at EOF (not an error)", 0);
        }

        // (3) 진짜 에러는 -1
        char b2[4];
        T("read_full returns -1 on bad fd", read_full(-1, b2, sizeof b2) == -1);

        // (4) 파이프 용량(64KB)보다 큰 전송 → 진짜 partial write 루프를 강제
        int q[2];
        if (pipe(q) == 0) {
            const size_t BIG = 256 * 1024;
            unsigned char *big = (unsigned char *)malloc(BIG);
            for (size_t i = 0; i < BIG; ++i) big[i] = (unsigned char)(i & 0xFF);
            drain_arg_t arg = { q[0], 0, 0 };
            pthread_t th;
            pthread_create(&th, NULL, drain_thread, &arg);
            ssize_t sent = write_full(q[1], big, BIG);
            close(q[1]);
            pthread_join(th, NULL);
            close(q[0]);
            free(big);
            T("write_full loops over partial writes (256KB thru pipe)",
              sent == (ssize_t)BIG && arg.got == BIG && arg.ok);
        } else {
            T("write_full loops over partial writes (256KB thru pipe)", 0);
        }
    }

    // ---------------------------------------------------------------- Q40
    printf("\n== Q40 O_NONBLOCK + EAGAIN 처리 ==\n");
    {
        int p[2];
        if (pipe(p) != 0) { perror("pipe"); return 1; }
        T("set_nonblocking returns 0", set_nonblocking(p[0]) == 0);
        int fl = fcntl(p[0], F_GETFL, 0);
        T("O_NONBLOCK is set on the fd", (fl & O_NONBLOCK) != 0);
        T("access mode preserved (read-modify-write, not overwrite)",
          (fl & O_ACCMODE) == O_RDONLY);
        T("set_nonblocking is idempotent", set_nonblocking(p[0]) == 0);

        size_t got = 12345;
        char b[16];
        T("read_nb on empty pipe -> IO_WOULDBLOCK",
          read_nb(p[0], b, sizeof b, &got) == IO_WOULDBLOCK && got == 0);

        ssize_t k = write(p[1], "hi", 2); (void)k;
        io_res_t rr = read_nb(p[0], b, sizeof b, &got);
        T("read_nb with data -> IO_OK and byte count",
          rr == IO_OK && got == 2 && b[0] == 'h' && b[1] == 'i');

        close(p[1]);
        T("read_nb after peer close -> IO_EOF",
          read_nb(p[0], b, sizeof b, &got) == IO_EOF);
        close(p[0]);
        T("set_nonblocking(-1) fails", set_nonblocking(-1) == -1);
    }

    // ---------------------------------------------------------------- Q41
    printf("\n== Q41 poll() 이벤트 루프 (2 fd 멀티플렉싱) ==\n");
    {
        int a[2], b[2];
        if (pipe(a) != 0 || pipe(b) != 0) { perror("pipe"); return 1; }
        ssize_t k1 = write(a[1], "hello", 5);
        ssize_t k2 = write(b[1], "worldzz", 7);
        (void)k1; (void)k2;
        close(a[1]); close(b[1]);                 // 둘 다 EOF 예정

        pump_t st;
        memset(&st, 0xAB, sizeof st);       // 포이즌: pump_two 가 직접 초기화해야 한다
        int r = pump_two(a[0], b[0], 500, &st);
        close(a[0]); close(b[0]);
        T("pump_two returns 0 when both fds hit EOF", r == 0);
        T("pump_two counts bytes from fd_a", st.bytes_a == 5);
        T("pump_two counts bytes from fd_b", st.bytes_b == 7);
        T("pump_two saw no timeout when data was ready", st.timeouts == 0);
        T("pump_two removed EOF fds (no spin)", st.iters > 0 && st.iters < 20);
    }
    {
        // 유휴 → 타임아웃 경로
        int a[2], b[2];
        if (pipe(a) != 0 || pipe(b) != 0) { perror("pipe"); return 1; }
        pump_t st;
        memset(&st, 0xAB, sizeof st);
        uint64_t t0 = hms();
        int r = pump_two(a[0], b[0], 60, &st);
        uint64_t dt = hms() - t0;
        close(a[0]); close(a[1]); close(b[0]); close(b[1]);
        T("pump_two returns 0 on idle timeout", r == 0 && st.timeouts == 1);
        T("pump_two actually waited the timeout", dt >= 40 && dt < 3000);
    }

    // ---------------------------------------------------------------- Q42
    printf("\n== Q42 self-pipe trick (루프 깨우기) ==\n");
    {
        selfpipe_t sp;
        T("sp_init succeeds with valid fds",
          sp_init(&sp) == 0 && sp.rd >= 0 && sp.wr >= 0);
        if (sp.rd >= 0) {
            int fl = fcntl(sp.rd, F_GETFL, 0);
            T("self-pipe read end is non-blocking", (fl & O_NONBLOCK) != 0);

            struct pollfd pf = { sp.rd, POLLIN, 0 };
            T("no event before notify", poll(&pf, 1, 0) == 0);

            T("sp_notify succeeds", sp_notify(&sp) == 0);
            pf.revents = 0;
            T("poll wakes immediately after notify", poll(&pf, 1, 0) == 1);

            T("sp_drain consumes 1 byte", sp_drain(&sp) == 1);
            pf.revents = 0;
            T("no event after drain", poll(&pf, 1, 0) == 0);

            sp_notify(&sp); sp_notify(&sp); sp_notify(&sp);
            T("sp_drain coalesces 3 notifies", sp_drain(&sp) == 3);

            // 다른 스레드가 블로킹 poll 을 깨운다
            pthread_t th;
            pthread_create(&th, NULL, notify_thread, &sp);
            pf.revents = 0;
            int r = poll(&pf, 1, 3000);
            pthread_join(th, NULL);
            T("another thread wakes a blocking poll", r == 1);
            sp_drain(&sp);
            sp_close(&sp);
            T("sp_close invalidates fds", sp.rd == -1 && sp.wr == -1);
        } else {
            T("self-pipe read end is non-blocking", 0);
            T("no event before notify", 0);
            T("sp_notify succeeds", 0);
            T("poll wakes immediately after notify", 0);
            T("sp_drain consumes 1 byte", 0);
            T("no event after drain", 0);
            T("sp_drain coalesces 3 notifies", 0);
            T("another thread wakes a blocking poll", 0);
            T("sp_close invalidates fds", 0);
        }
    }

    // ---------------------------------------------------------------- Q43
    printf("\n== Q43 라인 프레이밍 리더 (모뎀 AT 응답) ==\n");
    {
        linereader_t lr;
        memset(&lr, 0xAB, sizeof lr);      // 포이즌: lr_init 이 전부 리셋해야 한다
        collect_t c = { {{0}}, 0 };
        lr_init(&lr);
        int n1 = lr_feed(&lr, "AT+CS", 5, collect_cb, &c);
        T("partial chunk yields no line yet", n1 == 0 && c.n == 0);
        int n2 = lr_feed(&lr, "Q\r\nOK\r\n", 7, collect_cb, &c);
        T("split line is reassembled across chunks",
          n2 == 2 && c.n == 2 && strcmp(c.line[0], "AT+CSQ") == 0);
        T("CRLF is stripped", strcmp(c.line[1], "OK") == 0);
    }
    {
        linereader_t lr;
        memset(&lr, 0xAB, sizeof lr);      // 포이즌: lr_init 이 전부 리셋해야 한다
        collect_t c = { {{0}}, 0 };
        lr_init(&lr);
        const char *s = "+CSQ: 22,99\r\n";
        int total = 0;
        for (size_t i = 0; s[i]; ++i) total += lr_feed(&lr, &s[i], 1, collect_cb, &c);
        T("byte-at-a-time feed still frames one line",
          total == 1 && c.n == 1 && strcmp(c.line[0], "+CSQ: 22,99") == 0);
    }
    {
        linereader_t lr;
        memset(&lr, 0xAB, sizeof lr);      // 포이즌: lr_init 이 전부 리셋해야 한다
        collect_t c = { {{0}}, 0 };
        lr_init(&lr);
        char huge[200];
        memset(huge, 'x', sizeof huge);
        lr_feed(&lr, huge, sizeof huge, collect_cb, &c);
        int n = lr_feed(&lr, "\r\nRING\r\n", 8, collect_cb, &c);
        T("over-long line is dropped and counted",
          lr.dropped == 1 && n == 1 && c.n == 1);
        T("reader recovers on the next line", strcmp(c.line[0], "RING") == 0);
    }
    {
        linereader_t lr;
        memset(&lr, 0xAB, sizeof lr);      // 포이즌: lr_init 이 전부 리셋해야 한다
        collect_t c = { {{0}}, 0 };
        lr_init(&lr);
        int n = lr_feed(&lr, "\r\n", 2, collect_cb, &c);
        T("empty line is still a line", n == 1 && c.n == 1 && c.line[0][0] == '\0');
    }

    // ---------------------------------------------------------------- Q44
    printf("\n== Q44 지수 백오프 + 지터 (pure function) ==\n");
    {
        T("attempt 0 -> base delay",
          backoff_delay_ms(0, 500, 30000, 0, 0) == 500);
        T("attempt 3 -> base * 2^3",
          backoff_delay_ms(3, 500, 30000, 0, 0) == 4000);
        T("clamped at cap",
          backoff_delay_ms(10, 500, 30000, 0, 0) == 30000);
        T("no overflow at huge attempt",
          backoff_delay_ms(64, 1000, 30000, 0, 0) == 30000);
        T("jitter rnd=0 -> lower bound (delay*0.8)",
          backoff_delay_ms(3, 500, 30000, 20, 0) == 3200);
        T("jitter rnd=5000 -> centre",
          backoff_delay_ms(3, 500, 30000, 20, 5000) == 4000);
        T("jitter rnd=9999 -> upper bound (delay*1.2)",
          backoff_delay_ms(3, 500, 30000, 20, 9999) == 4800);
        int in_range = 1;
        for (uint32_t r = 0; r < 10000; r += 37) {
            uint32_t d = backoff_delay_ms(5, 200, 60000, 30, r);
            if (d < 4480 || d > 8320) in_range = 0;   // 6400 ± 30%
        }
        T("jitter always stays inside ±pct band", in_range);
    }

    // ---------------------------------------------------------------- Q45
    printf("\n== Q45 monotonic deadline 유틸 ==\n");
    {
        uint64_t t0 = now_ms();
        T("now_ms returns a plausible monotonic value", t0 > 0);
        uint64_t t1 = now_ms();
        T("now_ms is non-decreasing", t1 >= t0);
        T("now_ms agrees with harness clock (±2s)",
          (t1 > hms() ? t1 - hms() : hms() - t1) < 2000);

        uint64_t dl = now_ms() + 50;
        int64_t left = ms_remaining(dl);
        T("ms_remaining on a future deadline is (0, 50]", left > 0 && left <= 50);
        T("ms_remaining clamps a past deadline to 0",
          ms_remaining(now_ms() - 1000) == 0);
        T("poll_timeout_from caps long waits",
          poll_timeout_from(now_ms() + 100000, 1000) == 1000);
        T("poll_timeout_from returns 0 for an expired deadline",
          poll_timeout_from(now_ms() - 5, 1000) == 0);
        T("poll_timeout_from(0, cap) means 'no deadline' -> cap",
          poll_timeout_from(0, 250) == 250);

        // 실제 경과 측정: 하네스 시계로 30ms 스핀하는 동안 now_ms 도 같이 움직여야 한다
        uint64_t g0 = hms(), n0 = now_ms();
        while (hms() - g0 < 30) { /* spin */ }
        uint64_t dn = now_ms() - n0;
        T("now_ms advances with real time (30ms spin)", dn >= 20 && dn <= 500);
    }

    // ---------------------------------------------------------------- Q46
    printf("\n== Q46 원자적 설정 저장 (write+fsync+rename) ==\n");
    {
        char back[256];
        T("atomic_save creates the file",
          atomic_save(pathA, "{\"v\":1}", 7) == 0);
        T("content is exactly what was written",
          read_file_into(pathA, back, sizeof back) == 7 && strcmp(back, "{\"v\":1}") == 0);
        T("atomic_save overwrites in place",
          atomic_save(pathA, "{\"v\":22}", 8) == 0 &&
          read_file_into(pathA, back, sizeof back) == 8 &&
          strcmp(back, "{\"v\":22}") == 0);
        T("no temp files left behind", count_entries_with(tdir, ".tmp") == 0);
        struct stat stb;
        T("saved file is world-readable (0644), not mkstemp's 0600",
          stat(pathA, &stb) == 0 && (stb.st_mode & 0777) == 0644);
        T("atomic_save fails on an unwritable directory",
          atomic_save("/nonexistent-dir-vk05/x.json", "a", 1) == -1);
    }

    // ---------------------------------------------------------------- Q47
    printf("\n== Q47 시그널 안전 종료 (sig_atomic_t + EINTR) ==\n");
    {
        int p[2];
        if (pipe(p) != 0) { perror("pipe"); return 1; }

        int ok1 = (install_stop_handler(SIGUSR1) == 0);
        int ok2 = (install_stop_handler(SIGALRM) == 0);
        struct sigaction cur;
        memset(&cur, 0, sizeof cur);
        int armed = ok1 && ok2 &&
                    sigaction(SIGUSR1, NULL, &cur) == 0 &&
                    cur.sa_handler == on_stop_signal;
        T("install_stop_handler registers the handler via sigaction", armed);
        T("handler installed without SA_RESTART (so poll returns EINTR)",
          armed && (cur.sa_flags & SA_RESTART) == 0);

        if (armed) {
            // (1) 이미 플래그가 서 있으면 즉시 종료
            g_stop = 0;
            raise(SIGUSR1);
            int ec = -1;
            uint64_t t0 = hms();
            int r = wait_for_stop(p[0], 3000, &ec);
            uint64_t dt = hms() - t0;
            T("flag set before entering loop -> immediate stop", r == 1 && dt < 500);

            // (2) poll 안에서 자다가 시그널로 깨어남 (EINTR 경로)
            g_stop = 0;
            struct itimerval it;
            memset(&it, 0, sizeof it);
            it.it_value.tv_usec = 80000;      // 80ms 후 1회 SIGALRM
            ec = -1;
            t0 = hms();
            setitimer(ITIMER_REAL, &it, NULL);
            r = wait_for_stop(p[0], 3000, &ec);
            dt = hms() - t0;
            memset(&it, 0, sizeof it);
            setitimer(ITIMER_REAL, &it, NULL);   // 타이머 해제
            T("signal during poll stops the loop", r == 1 && dt >= 40 && dt < 2500);
            T("EINTR from poll was counted, not treated as an error", ec >= 1);

            // (3) 시그널이 없으면 예산 소진 후 0
            g_stop = 0;
            t0 = hms();
            r = wait_for_stop(p[0], 80, &ec);
            dt = hms() - t0;
            T("no signal -> budget expires and returns 0",
              r == 0 && dt >= 50 && dt < 2500);

            // (4) fd 가 읽기 가능하면 2
            g_stop = 0;
            ssize_t k = write(p[1], "z", 1); (void)k;
            T("readable fd -> returns 2", wait_for_stop(p[0], 500, &ec) == 2);
        } else {
            T("flag set before entering loop -> immediate stop", 0);
            T("signal during poll stops the loop", 0);
            T("EINTR from poll was counted, not treated as an error", 0);
            T("no signal -> budget expires and returns 0", 0);
            T("readable fd -> returns 2", 0);
        }
        g_stop = 0;
        close(p[0]); close(p[1]);
    }

    // ---------------------------------------------------------------- Q48
    printf("\n== Q48 에러 경로 정리 (goto cleanup, fd 누수 0) ==\n");
    {
        T("xclose(-1) is a no-op success", xclose(-1) == 0);
        int fd = open("/dev/null", O_RDONLY);
        T("xclose closes a real fd", fd >= 0 && xclose(fd) == 0);
        T("xclose on an already-closed fd reports EBADF", xclose(fd) == -1);

        char back[256];
        T("copy_file_prefix copies the requested prefix",
          copy_file_prefix(pathB, pathC, 100) == 100);
        T("copy_file_prefix stops at EOF (short source)",
          copy_file_prefix(pathA, pathC, 1u << 20) == 8 &&
          read_file_into(pathC, back, sizeof back) == 8);
        T("copy_file_prefix fails on a missing source",
          copy_file_prefix("/nonexistent-vk05/src", pathC, 16) == -1);

        // fd 누수 검사: 실패 경로를 400번 돌려도 fd 번호가 크게 자라면 안 된다
        int base = open("/dev/null", O_RDONLY);
        close(base);
        for (int i = 0; i < 200; ++i) {
            copy_file_prefix("/nonexistent-vk05/src", pathC, 16);        // open(src) 실패
            copy_file_prefix(pathB, "/nonexistent-vk05/dst", 16);        // open(dst) 실패
        }
        int after = open("/dev/null", O_RDONLY);
        int leaked = (base >= 0 && after >= 0) ? (after - base) : 999;
        close(after);
        T("400 failed copies leak zero fds", base >= 0 && leaked <= 2);
    }

    // -------- 정리 --------
    unlink(pathA); unlink(pathB); unlink(pathC);
    rmdir(tdir);

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
