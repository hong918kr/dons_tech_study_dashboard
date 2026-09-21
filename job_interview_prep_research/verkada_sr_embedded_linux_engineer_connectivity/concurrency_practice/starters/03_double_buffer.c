/* 03. Double buffering (ping-pong) — 리크루터 메일에 명시된 주제
 * ------------------------------------------------------------------
 * 센서/DMA가 한 버퍼를 채우는 동안 처리 스레드는 다른 버퍼를 읽는다.
 * 요구: (1) 읽는 쪽이 절반만 갱신된 프레임(tearing)을 절대 보지 않는다
 *       (2) 생산자는 소비자를 기다리지 않는다(실시간 — 블로킹 금지)
 *       (3) 소비자가 느리면 "가장 최근 프레임"만 보면 된다(중간 프레임은 버림)
 *
 * 핵심 아이디어: 락으로 '데이터'를 보호하지 말고 '버퍼 소유권'을 보호한다.
 *   각 버퍼는 항상 셋 중 하나의 상태: WRITER 소유 / READY(publish됨) / READER 소유
 *   임계구역은 인덱스 몇 개 바꾸는 것뿐 → 수 나노초. 데이터 복사 없음.
 *
 * 버퍼가 2개뿐이면: reader가 하나를 들고 있고 다른 하나가 READY인 순간
 * writer는 쓸 곳이 없다 → READY를 버린다(drop). 이것이 triple buffering을
 * 쓰는 이유다(문제 04 참고).
 * 빌드: cc -std=c11 -Wall -Wextra -pthread 03_double_buffer.c
 */
#include <assert.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define FRAME_LEN  512
#define NBUF       2            /* 3으로 바꾸면 그대로 triple buffering이 된다 */

typedef struct {
    unsigned      seq;
    unsigned char data[FRAME_LEN];
} Frame;

typedef struct {
    Frame           buf[NBUF];
    int             write_idx;      /* writer 소유 버퍼, 없으면 -1 */
    int             ready_idx;      /* publish된 최신 버퍼, 없으면 -1 */
    int             reader_idx;     /* reader 대여 중 버퍼, 없으면 -1 */
    unsigned long   dropped;        /* 소비되지 못하고 버려진 프레임 수 */
    pthread_mutex_t m;
} DoubleBuf;

void   db_init(DoubleBuf *db);
void   db_destroy(DoubleBuf *db);
Frame *db_write_begin(DoubleBuf *db);   /* writer: 채울 버퍼 확보(항상 성공) */
void   db_write_commit(DoubleBuf *db);  /* writer: publish */
Frame *db_read_acquire(DoubleBuf *db);  /* reader: 최신 프레임 대여, 없으면 NULL */
void   db_read_release(DoubleBuf *db);  /* reader: 반납 */

/* ------------------------------------------------------------------
 * 여기부터 직접 구현한다. 위의 선언부와 아래 self-test는 그대로 두고,
 * 아래 함수 목록을 채운 뒤 `make run N=03` 로 검증한다.
 *
 * 구현할 함수:
 *   - db_init()
 *   - db_destroy()
 *   - db_write_begin()
 *   - db_write_commit()
 *   - db_read_acquire()
 *   - db_read_release()
 *
 * 막히면 ../solutions/03_double_buffer.c 를 열되, 먼저 15분은 스스로 해볼 것.
 * ------------------------------------------------------------------ */

/* ------------------------------- self test ------------------------------- */
#define NFRAMES 20000

static DoubleBuf g_db;
static _Atomic bool g_done;
static unsigned long g_read_ok, g_torn, g_out_of_order;

static void *producer(void *arg)            /* 센서 / DMA 완료 콜백 역할 */
{
    (void)arg;
    for (unsigned s = 1; s <= NFRAMES; s++) {
        Frame *f = db_write_begin(&g_db);
        f->seq = s;
        /* 프레임 전체를 같은 패턴으로 채운다 → tearing이 나면 값이 섞인다 */
        memset(f->data, (int)(s & 0xFF), FRAME_LEN);
        db_write_commit(&g_db);
    }
    g_done = true;
    return NULL;
}

static void *consumer(void *arg)
{
    (void)arg;
    unsigned last = 0;
    for (;;) {
        Frame *f = db_read_acquire(&g_db);
        if (!f) {
            if (g_done) break;              /* 생산 종료 + 남은 프레임 없음 */
            continue;                        /* 실전이라면 condvar로 대기 */
        }
        unsigned char expect = (unsigned char)(f->seq & 0xFF);
        for (int i = 0; i < FRAME_LEN; i++) {
            if (f->data[i] != expect) { g_torn++; break; }
        }
        if (f->seq < last) g_out_of_order++; /* 항상 최신만 오므로 단조 증가여야 */
        last = f->seq;
        g_read_ok++;
        db_read_release(&g_db);
    }
    return NULL;
}

int main(void)
{
    pthread_t p, c;
    db_init(&g_db);
    pthread_create(&c, NULL, consumer, NULL);
    pthread_create(&p, NULL, producer, NULL);
    pthread_join(p, NULL);
    pthread_join(c, NULL);

    /* 마지막 프레임이 publish된 직후 소비자가 종료했을 수 있다 → 1개는 남을 수 있음 */
    unsigned long leftover = (g_db.ready_idx >= 0) ? 1u : 0u;
    printf("frames=%d read=%lu dropped=%lu leftover=%lu torn=%lu ooo=%lu\n",
           NFRAMES, g_read_ok, g_db.dropped, leftover, g_torn, g_out_of_order);
    assert(g_torn == 0);                     /* tearing 없어야 통과 */
    assert(g_out_of_order == 0);
    assert(g_read_ok > 0);
    /* 모든 프레임은 소비되었거나, 버려졌거나, 마지막으로 남아 있다 */
    assert(g_read_ok + g_db.dropped + leftover == NFRAMES);
    db_destroy(&g_db);
    puts("03 PASS");
    return 0;
}
