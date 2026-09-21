// 05_linux_io.c  —  REFERENCE SOLUTION
// 임베디드 Linux I/O & 이벤트 루프 (Embedded Linux I/O & Event Loops)  —  Q39~Q48
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra -O1 -g -pthread 05_linux_io.c -o /tmp/vk05 && /tmp/vk05
//
// Verkada Connectivity 팀의 게이트웨이 데몬(GC31-E/GW31-E)은 결국 "하나의 이벤트 루프"다.
// 모뎀 제어 채널(AT/UART), 클라우드(Command) 소켓, 재연결 타이머, 종료 시그널을
// 한 루프에서 멀티플렉싱한다. 이 세트는 그 루프를 구성하는 부품을 하나씩 드릴한다:
// 부분 I/O·EINTR → 논블로킹 → poll 루프 → self-pipe → 라인 프레이밍 → 백오프 →
// monotonic deadline → 원자적 저장 → 시그널 종료 → 에러 경로 정리.
//
// ⚠️ 이식성: 이 파일은 macOS(Apple clang, arm64)에서 컴파일/실행된다. 그래서 코드는
//    POSIX(poll/pipe/fcntl/clock_gettime/sigaction/rename)만 쓴다. Linux 전용
//    epoll / timerfd / signalfd / inotify / netlink / sd_notify 는 notes 와 JSON 에서
//    "이론"으로 다룬다 — 면접에서는 그쪽을 말해야 하므로 반드시 노트를 읽을 것.
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
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// 하네스 전용 시계 — 학생 코드(now_ms)와 독립적이어야 테스트가 서로 오염되지 않는다.
static uint64_t hms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

// ===========================================================================
// Q39. read_full / write_full  —  부분 읽기·쓰기 루프 + EINTR 재시도
// ---------------------------------------------------------------------------
// POSIX 에서 read()/write() 는 "요청한 만큼"을 보장하지 않는다. 파이프·소켓·tty 는
// 커널 버퍼 한도만큼만 처리하고 돌아온다(short read / short write). 시그널이 오면
// -1/EINTR 로 조기 복귀한다. 이 둘을 처리하지 않으면 모뎀 응답이 잘리고, 클라우드로
// 보낸 프레임이 반쪽만 나간다 — 게이트웨이 필드 버그 1순위.
//   read_full : n 바이트를 다 읽거나 EOF 를 만날 때까지 반복. 리턴 = 실제 읽은 수
//               (n 보다 작으면 EOF. 그 자체는 에러가 아니다.) 에러면 -1.
//   write_full: n 바이트를 전부 밀어넣는다. 리턴 = n, 에러면 -1.
// ===========================================================================
ssize_t read_full(int fd, void *buf, size_t n) {
    if (!buf && n) { errno = EINVAL; return -1; }
    unsigned char *p = (unsigned char *)buf;
    size_t got = 0;
    while (got < n) {
        ssize_t k = read(fd, p + got, n - got);
        if (k < 0) {
            if (errno == EINTR) continue;   // 시그널에 깨진 것뿐 — 재시도
            return -1;                      // 진짜 에러
        }
        if (k == 0) break;                  // EOF: 짧게 리턴하되 에러 아님
        got += (size_t)k;
    }
    return (ssize_t)got;
}

ssize_t write_full(int fd, const void *buf, size_t n) {
    if (!buf && n) { errno = EINVAL; return -1; }
    const unsigned char *p = (const unsigned char *)buf;
    size_t sent = 0;
    while (sent < n) {
        ssize_t k = write(fd, p + sent, n - sent);
        if (k < 0) {
            if (errno == EINTR) continue;   // 재시도
            return -1;
        }
        sent += (size_t)k;                  // 부분 쓰기 → 남은 만큼 다시
    }
    return (ssize_t)sent;
}

// ===========================================================================
// Q40. 논블로킹 fd (fcntl O_NONBLOCK) + EAGAIN/EWOULDBLOCK 처리
// ---------------------------------------------------------------------------
// 이벤트 루프의 모든 fd 는 논블로킹이어야 한다. poll 이 "읽을 수 있다"고 해도
// (스퓨리어스 wakeup, 다른 소비자가 먼저 읽음) 막상 read 가 블록될 수 있기 때문.
// 핵심은 F_GETFL → OR → F_SETFL 의 read-modify-write: 기존 플래그를 덮어쓰면 안 된다.
// EAGAIN 은 "지금은 데이터 없음"이지 에러가 아니다. EWOULDBLOCK 과 같은 값일 수도,
// 아닐 수도 있으므로 두 개를 모두 검사하는 게 이식성 있는 관용구다.
// ===========================================================================
typedef enum {
    IO_OK         =  0,   // out_n 바이트를 읽었다
    IO_WOULDBLOCK =  1,   // 지금은 데이터 없음 (EAGAIN/EWOULDBLOCK)
    IO_EOF        =  2,   // 상대가 닫음 (read == 0)
    IO_ERROR      = -1    // 진짜 에러
} io_res_t;

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (flags & O_NONBLOCK) return 0;               // 이미 설정 — 멱등
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) // '=' 아니고 '|=' 가 핵심
        return -1;
    return 0;
}

io_res_t read_nb(int fd, void *buf, size_t n, size_t *out_n) {
    if (out_n) *out_n = 0;
    if (!buf && n) { errno = EINVAL; return IO_ERROR; }
    for (;;) {
        ssize_t k = read(fd, buf, n);
        if (k > 0) { if (out_n) *out_n = (size_t)k; return IO_OK; }
        if (k == 0) return IO_EOF;
        if (errno == EINTR) continue;                       // 재시도
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return IO_WOULDBLOCK;                           // 정상 — 루프로 돌아가라
        return IO_ERROR;
    }
}

// ===========================================================================
// Q41. poll() 기반 이벤트 루프 — 두 fd 멀티플렉싱 + 타임아웃
// ---------------------------------------------------------------------------
// 게이트웨이 데몬의 심장. 모뎀 채널(fd_a)과 클라우드 소켓(fd_b)을 동시에 감시하고,
// 아무 일도 없으면 타임아웃으로 깨어나 하트비트/watchdog 을 친다.
// 규칙 3가지:
//   ① poll 이 -1/EINTR 이면 에러가 아니라 재시도.
//   ② EOF 가 된 fd 는 배열에서 빼야 한다 — 안 그러면 POLLHUP 이 계속 떠서 100% CPU.
//   ③ revents 는 요청 안 한 비트(POLLHUP/POLLERR/POLLNVAL)도 올라온다. 반드시 검사.
// 여기서는 두 fd 가 모두 EOF 이거나, 한 번 유휴 타임아웃이 나면 0 을 리턴한다.
// ===========================================================================
typedef struct {
    size_t bytes_a;     // fd_a 에서 읽은 총 바이트
    size_t bytes_b;     // fd_b 에서 읽은 총 바이트
    int    timeouts;    // poll 타임아웃 횟수
    int    eof_a;       // fd_a 가 EOF 인가
    int    eof_b;       // fd_b 가 EOF 인가
    int    iters;       // 루프 회전 수 (스핀 감지용)
} pump_t;

int pump_two(int fd_a, int fd_b, int timeout_ms, pump_t *st) {
    if (!st) { errno = EINVAL; return -1; }
    memset(st, 0, sizeof *st);

    while (!(st->eof_a && st->eof_b)) {
        struct pollfd pfd[2];
        int slot[2];                    // pfd[i] 가 a(0)인지 b(1)인지
        int nf = 0;
        if (!st->eof_a) { pfd[nf].fd = fd_a; pfd[nf].events = POLLIN; pfd[nf].revents = 0; slot[nf] = 0; nf++; }
        if (!st->eof_b) { pfd[nf].fd = fd_b; pfd[nf].events = POLLIN; pfd[nf].revents = 0; slot[nf] = 1; nf++; }

        st->iters++;
        int r = poll(pfd, (nfds_t)nf, timeout_ms);
        if (r < 0) {
            if (errno == EINTR) continue;        // ① 시그널 — 재시도
            return -1;
        }
        if (r == 0) { st->timeouts++; return 0; } // 유휴 타임아웃 → 상위에서 결정

        for (int i = 0; i < nf; ++i) {
            // ③ POLLHUP/POLLERR 도 "읽어봐라" 신호. read 가 0 또는 -1 로 알려준다.
            if (!(pfd[i].revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL))) continue;
            char buf[256];
            ssize_t k = read(pfd[i].fd, buf, sizeof buf);
            if (k < 0) {
                if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
                return -1;
            }
            if (k == 0) {                        // ② EOF → 배열에서 제외
                if (slot[i] == 0) st->eof_a = 1; else st->eof_b = 1;
                continue;
            }
            if (slot[i] == 0) st->bytes_a += (size_t)k;
            else              st->bytes_b += (size_t)k;
        }
    }
    return 0;
}

// ===========================================================================
// Q42. self-pipe trick — 다른 스레드/시그널 핸들러에서 이벤트 루프 깨우기
// ---------------------------------------------------------------------------
// poll() 안에서 자고 있는 루프를 "지금 당장" 깨우는 표준 기법. 파이프를 하나 만들어
// 읽기단을 항상 poll 집합에 넣어두고, 깨우고 싶은 쪽이 1바이트를 쓴다.
//   · write() 는 async-signal-safe → 시그널 핸들러에서 호출해도 된다.
//   · 두 끝 모두 O_NONBLOCK: 파이프가 차 있어도 notify 가 블록되면 안 된다
//     (이미 깨울 이벤트가 밀려 있다는 뜻이므로 EAGAIN 은 성공으로 취급).
//   · FD_CLOEXEC: fork/exec 하는 데몬에서 fd 가 자식으로 새지 않게.
// Linux 라면 eventfd(2) 로 fd 1개만 쓰지만, self-pipe 는 어디서나 동작한다.
// ===========================================================================
typedef struct { int rd; int wr; } selfpipe_t;

static int set_cloexec(int fd) {
    int f = fcntl(fd, F_GETFD, 0);
    if (f < 0) return -1;
    return fcntl(fd, F_SETFD, f | FD_CLOEXEC);
}

int sp_init(selfpipe_t *sp) {
    if (!sp) { errno = EINVAL; return -1; }
    sp->rd = sp->wr = -1;
    int fds[2];
    if (pipe(fds) != 0) return -1;
    sp->rd = fds[0];
    sp->wr = fds[1];
    if (set_nonblocking(sp->rd) != 0 || set_nonblocking(sp->wr) != 0 ||
        set_cloexec(sp->rd)    != 0 || set_cloexec(sp->wr)    != 0) {
        int save = errno;
        close(sp->rd); close(sp->wr);
        sp->rd = sp->wr = -1;
        errno = save;
        return -1;
    }
    return 0;
}

// 시그널 핸들러에서 호출해도 안전(write 만 쓴다). errno 를 보존하는 것도 중요.
int sp_notify(const selfpipe_t *sp) {
    if (!sp || sp->wr < 0) { errno = EINVAL; return -1; }
    const unsigned char byte = 'x';
    for (;;) {
        ssize_t k = write(sp->wr, &byte, 1);
        if (k == 1) return 0;
        if (k < 0 && errno == EINTR) continue;
        if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return 0;            // 파이프가 이미 꽉 참 = 이미 깨울 예정 → 성공
        return -1;
    }
}

// 루프가 깨어난 뒤 반드시 비워야 한다. 안 비우면 poll 이 영원히 즉시 리턴(busy loop).
int sp_drain(const selfpipe_t *sp) {
    if (!sp || sp->rd < 0) { errno = EINVAL; return -1; }
    int total = 0;
    unsigned char buf[64];
    for (;;) {
        ssize_t k = read(sp->rd, buf, sizeof buf);
        if (k > 0) { total += (int)k; continue; }
        if (k == 0) break;                                   // 쓰기단이 닫힘
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;   // 다 비웠다
        return -1;
    }
    return total;
}

void sp_close(selfpipe_t *sp) {
    if (!sp) return;
    if (sp->rd >= 0) close(sp->rd);
    if (sp->wr >= 0) close(sp->wr);
    sp->rd = sp->wr = -1;
}

// ===========================================================================
// Q43. 라인 프레이밍 리더 — 스트림에서 완전한 줄만 넘기는 상태 있는 파서
// ---------------------------------------------------------------------------
// TCP/UART 는 "메시지"가 아니라 "바이트 스트림"이다. 모뎀에 AT+CSQ 를 보내면 응답이
// "+CSQ: 22,"  / "99\r\nOK\r\n" 처럼 두 번에 나뉘어 도착할 수 있다. 그래서 리더는
// 반드시 상태를 들고 있어야 한다(누적 버퍼 + 길이). 완성된 줄만 콜백으로 넘긴다.
// 실전 요구사항 3가지: ①CRLF/LF 모두 지원 ②버퍼보다 긴 줄은 버리되 그 다음 줄부터
// 정상 복구(절대 크래시/오버런 금지) ③빈 줄도 유효한 줄.
// ===========================================================================
#define LR_CAP 128
typedef void (*lr_cb_t)(const char *line, void *user);

typedef struct {
    char   buf[LR_CAP];   // 누적 버퍼 ('\0' 자리 1칸 포함)
    size_t len;           // 현재 누적 길이
    int    overflow;      // 현재 줄이 이미 넘쳤는가 (줄 끝까지 버리는 중)
    int    dropped;       // 버린 줄 수 — 조용한 손실 금지, 반드시 센다
} linereader_t;

void lr_init(linereader_t *lr) {
    if (!lr) return;
    lr->len = 0;
    lr->overflow = 0;
    lr->dropped = 0;
    lr->buf[0] = '\0';
}

int lr_feed(linereader_t *lr, const char *data, size_t n, lr_cb_t cb, void *user) {
    if (!lr || (!data && n)) return 0;
    int lines = 0;
    for (size_t i = 0; i < n; ++i) {
        char c = data[i];
        if (c == '\n') {
            if (lr->overflow) {          // 넘친 줄은 통째로 버리고 여기서 복구
                lr->overflow = 0;
                lr->len = 0;
                continue;
            }
            size_t len = lr->len;
            if (len > 0 && lr->buf[len - 1] == '\r') len--;   // CRLF → CR 떼기
            lr->buf[len] = '\0';
            if (cb) cb(lr->buf, user);
            lr->len = 0;
            lines++;
            continue;
        }
        if (lr->len + 1 >= LR_CAP) {     // '\0' 자리 확보 — 오버런 방지
            if (!lr->overflow) { lr->overflow = 1; lr->dropped++; }
            continue;                    // 줄이 끝날 때까지 계속 버린다
        }
        lr->buf[lr->len++] = c;
    }
    return lines;
}

// ===========================================================================
// Q44. 지수 백오프 + 지터 (pure function)
// ---------------------------------------------------------------------------
// 200만 대의 게이트웨이가 클라우드 장애 후 동시에 재연결하면 thundering herd 로
// 복구를 방해한다. 그래서 ①지수적으로 늘리고 ②상한(cap)에서 멈추고 ③지터로 흩는다.
// 난수를 **주입**(rnd 파라미터)하는 게 핵심 설계 포인트 — 순수 함수가 되어
// 하드웨어/시간 없이 단위 테스트가 가능하다("modular, testable embedded software").
//   attempt : 0,1,2,...  delay = base * 2^attempt, cap 에서 클램프
//   jitter_pct : 결과를 ±jitter_pct % 범위로 흔든다
//   rnd : 0..9999 (테스트에서는 고정값, 실전에서는 rand()/dev/urandom)
// ===========================================================================
uint32_t backoff_delay_ms(uint32_t attempt, uint32_t base_ms, uint32_t cap_ms,
                          uint32_t jitter_pct, uint32_t rnd) {
    if (base_ms == 0) return 0;
    if (cap_ms < base_ms) cap_ms = base_ms;

    uint64_t d = base_ms;
    if (attempt >= 32) {
        d = cap_ms;                              // 시프트 오버플로 방지
    } else {
        for (uint32_t i = 0; i < attempt; ++i) {
            d <<= 1;
            if (d >= cap_ms) { d = cap_ms; break; }
        }
    }
    if (d > cap_ms) d = cap_ms;
    if (jitter_pct == 0) return (uint32_t)d;
    if (jitter_pct > 100) jitter_pct = 100;

    uint64_t span = d * jitter_pct / 100;        // 편차 반폭
    if (span == 0) return (uint32_t)d;
    uint64_t width = 2 * span + 1;               // [d-span, d+span]
    uint64_t off   = width * (uint64_t)(rnd % 10000u) / 10000u;
    return (uint32_t)(d - span + off);
}

// ===========================================================================
// Q45. monotonic deadline 유틸
// ---------------------------------------------------------------------------
// 타임아웃에 절대 CLOCK_REALTIME(gettimeofday/time())을 쓰지 말 것. NTP 가 시계를
// 뒤로 당기면 타임아웃이 몇 시간 늘어난다 — 게이트웨이가 "멈춘 것처럼" 보이는 전형적
// 필드 버그. CLOCK_MONOTONIC 은 부팅 이후 단조 증가라 안전하다.
// 또 하나: poll 은 "상대 시간(ms)"을 받으므로, 루프를 돌 때마다 남은 시간을 **다시
// 계산**해야 한다. 안 그러면 EINTR 로 깰 때마다 타임아웃이 리셋되어 무한정 늘어난다.
// ===========================================================================
uint64_t now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

// 남은 시간. 이미 지났으면 0 (음수 클램프 — poll 에 음수를 주면 '무한 대기'가 된다!)
int64_t ms_remaining(uint64_t deadline_ms) {
    uint64_t now = now_ms();
    if (deadline_ms <= now) return 0;
    return (int64_t)(deadline_ms - now);
}

// poll 에 넘길 타임아웃. cap_ms 로 상한을 걸어 주기적으로 깨어나게 한다
// (하트비트/watchdog kick 을 놓치지 않기 위해). deadline_ms == 0 = "마감 없음".
int poll_timeout_from(uint64_t deadline_ms, int cap_ms) {
    if (cap_ms < 0) cap_ms = 0;
    if (deadline_ms == 0) return cap_ms;
    int64_t left = ms_remaining(deadline_ms);
    if (left > (int64_t)cap_ms) left = cap_ms;
    if (left < 0) left = 0;
    return (int)left;
}

// ===========================================================================
// Q46. 원자적 설정 저장 — temp write + fsync + rename (전원 손실 안전)
// ---------------------------------------------------------------------------
// 게이트웨이는 야외에서 전원이 툭 끊긴다(PoE 재협상, 태양광 배터리 방전). 설정 파일을
// 제자리에서 덮어쓰면 절반만 쓰인 상태로 부팅해 벽돌이 된다. 표준 레시피:
//   ① 같은 디렉토리에 임시 파일 생성 (다른 파일시스템이면 rename 이 EXDEV 로 실패)
//   ② 내용 write_full  ③ fsync(파일) — 데이터가 실제 미디어에 도달
//   ④ close  ⑤ rename() — POSIX 가 원자성을 보장. 독자는 옛 파일 or 새 파일만 본다
//   ⑥ fsync(디렉토리) — 이걸 빼면 rename 자체가 전원 손실로 사라질 수 있다
// 실패하면 임시 파일을 반드시 unlink (쓰레기 누적 금지).
// ===========================================================================
// (제공됨) path 의 디렉토리 부분을 out 에 담는다. '/' 가 없으면 ".".
static const char *dir_of(const char *path, char *out, size_t cap) {
    const char *slash = strrchr(path, '/');
    if (!slash) { snprintf(out, cap, "."); return out; }
    size_t len = (size_t)(slash - path);
    if (len == 0) { snprintf(out, cap, "/"); return out; }
    if (len >= cap) len = cap - 1;
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

int atomic_save(const char *path, const void *data, size_t n) {
    if (!path || (!data && n)) { errno = EINVAL; return -1; }

    char tmp[512], dbuf[512];
    int  fd = -1, dfd = -1, rc = -1, have_tmp = 0;

    if (snprintf(tmp, sizeof tmp, "%s.tmpXXXXXX", path) >= (int)sizeof tmp) {
        errno = ENAMETOOLONG;
        return -1;
    }
    fd = mkstemp(tmp);                      // ① 같은 디렉토리 = 같은 파일시스템
    if (fd < 0) return -1;
    have_tmp = 1;

    if (write_full(fd, data, n) != (ssize_t)n) goto cleanup;   // ②
    if (fsync(fd) != 0)                       goto cleanup;   // ③
    if (close(fd) != 0) { fd = -1;            goto cleanup; } // ④
    fd = -1;
    if (chmod(tmp, 0644) != 0)                goto cleanup;   // mkstemp 는 0600
    if (rename(tmp, path) != 0)               goto cleanup;   // ⑤ 원자적 교체
    have_tmp = 0;                             // 이제 tmp 는 존재하지 않는다

    dfd = open(dir_of(path, dbuf, sizeof dbuf), O_RDONLY);    // ⑥
    if (dfd >= 0) fsync(dfd);
    rc = 0;

cleanup: {
        int save = errno;                     // 정리 중 errno 가 덮이지 않게 보존
        if (fd  >= 0) close(fd);
        if (dfd >= 0) close(dfd);
        if (have_tmp) unlink(tmp);            // 실패 시 쓰레기 제거
        errno = save;
    }
    return rc;
}

// ===========================================================================
// Q47. 시그널 안전 종료 — volatile sig_atomic_t + sigaction + EINTR 루프
// ---------------------------------------------------------------------------
// SIGTERM 을 받은 데몬은 "지금 하던 I/O 를 끝내고" 깨끗이 나가야 한다(설정 flush,
// 모뎀 세션 해제). 핸들러에서 할 수 있는 일은 극히 제한적이다:
//   · async-signal-safe 함수만 (write, _exit, sigaction ... printf/malloc 금지!)
//   · 쓸 수 있는 전역은 volatile sig_atomic_t 뿐 (원자적 읽기/쓰기 보장)
//   · 핸들러는 플래그만 세우고 즉시 리턴 — 실제 정리는 루프가 한다
// signal() 대신 sigaction() 을 쓰는 이유: 이식성 있는 시맨틱, 마스크 지정 가능,
// SA_RESTART 를 **끄면** poll 이 EINTR 로 즉시 깨어나 플래그를 볼 수 있다.
// (SA_RESTART 를 켜면 일부 syscall 이 자동 재시작되어 종료가 지연된다.)
// ===========================================================================
static volatile sig_atomic_t g_stop = 0;

void on_stop_signal(int signo) {
    (void)signo;
    g_stop = 1;        // 여기서 하는 일은 이것뿐. (실전에서는 + sp_notify(&g_sp))
}

int install_stop_handler(int signo) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_stop_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;                   // SA_RESTART 없음 → poll 이 EINTR 로 깬다
    return sigaction(signo, &sa, NULL);
}

// budget_ms 동안 fd 를 감시한다.
//   리턴 1 = 종료 시그널로 중단, 2 = fd 가 읽기 가능, 0 = 예산 소진, -1 = 에러
int wait_for_stop(int fd, int budget_ms, int *eintr_count) {
    if (eintr_count) *eintr_count = 0;
    uint64_t deadline = now_ms() + (uint64_t)(budget_ms < 0 ? 0 : budget_ms);

    for (;;) {
        if (g_stop) return 1;                       // 루프 진입 전에 먼저 확인
        int to = (int)ms_remaining(deadline);       // 매 회전마다 재계산
        if (to <= 0) return g_stop ? 1 : 0;

        struct pollfd pf = { fd, POLLIN, 0 };
        int r = poll(&pf, 1, to);
        if (r < 0) {
            if (errno == EINTR) {                   // 시그널로 깼다 — 에러 아님
                if (eintr_count) (*eintr_count)++;
                continue;                           // 루프 상단에서 g_stop 검사
            }
            return -1;
        }
        if (r == 0) return g_stop ? 1 : 0;          // 타임아웃
        if (pf.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) return 2;
    }
}

// ===========================================================================
// Q48. 에러 경로 정리 — fd 누수 없는 goto cleanup + close()/EINTR 주의
// ---------------------------------------------------------------------------
// 오래 도는 데몬에서 fd 누수는 며칠 뒤 EMFILE 로 터진다 — "왜 3일마다 죽지?"의 범인.
// C 에는 RAII 가 없으므로 관용구는 **단일 출구 + goto cleanup**:
//   자원 핸들을 무효값(-1/NULL)으로 먼저 선언 → 실패하면 goto → cleanup 에서
//   유효한 것만 해제. 조기 return 을 흩뿌리면 반드시 하나를 빠뜨린다.
// close() 와 EINTR: Linux 에서 close() 가 EINTR 를 리턴해도 **fd 는 이미 반납됐다**.
// 여기서 재시도하면 그 사이 다른 스레드가 같은 번호로 연 fd 를 닫아버린다(심각한 버그).
// 따라서 close 는 재시도하지 않고 EINTR 을 성공으로 취급한다.
// 반대로 데이터 무결성이 중요하면 close 전에 fsync 로 에러를 미리 확인해야 한다.
// ===========================================================================
int xclose(int fd) {
    if (fd < 0) return 0;                  // 이미 닫힘/미할당 — no-op
    int r = close(fd);
    if (r < 0 && errno == EINTR) return 0; // fd 는 이미 반납됨 → 재시도 금지
    return r;
}

// src 의 앞 max_bytes 바이트를 dst 로 복사. 어떤 단계에서 실패해도 fd/메모리 누수 없음.
// 리턴 = 복사한 바이트 수, 실패 시 -1.
long copy_file_prefix(const char *src, const char *dst, size_t max_bytes) {
    int            in = -1, out = -1;
    unsigned char *buf = NULL;
    long           rc = -1;
    long           done = 0;
    const size_t   CHUNK = 4096;

    if (!src || !dst) { errno = EINVAL; goto cleanup; }

    in = open(src, O_RDONLY);
    if (in < 0) goto cleanup;
    out = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) goto cleanup;
    buf = (unsigned char *)malloc(CHUNK);
    if (!buf) goto cleanup;

    size_t left = max_bytes;
    while (left > 0) {
        size_t want = left < CHUNK ? left : CHUNK;
        ssize_t k = read_full(in, buf, want);
        if (k < 0) goto cleanup;
        if (k == 0) break;                           // EOF — 정상 종료
        if (write_full(out, buf, (size_t)k) != k) goto cleanup;
        done += (long)k;
        left -= (size_t)k;
        if ((size_t)k < want) break;                 // 짧은 read = EOF 도달
    }
    rc = done;

cleanup: {
        int save = errno;
        free(buf);                 // free(NULL) 은 안전
        if (xclose(out) != 0) rc = -1;   // 쓰기 파일은 close 에러도 실패로 본다
        xclose(in);
        errno = save;
    }
    return rc;
}

// ===========================================================================
// 테스트 보조 (하네스 전용 — 문제 풀이 대상 아님)
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
