/* 04. Latest-value 공유 (seqlock) — writer는 절대 블록되지 않는다
 * ------------------------------------------------------------------
 * 게이트웨이 상태(신호세기, SIM, WAN, 온도)를 1초마다 갱신하는 writer 1개와,
 * 아무 때나 읽는 reader 여러 개(HTTP 핸들러, CLI, 텔레메트리 업로더).
 * 요구: reader가 여러 필드를 '같은 시점의 스냅샷'으로 읽어야 하고(찢어지면 안 됨),
 *       reader 때문에 writer가 지연되면 안 된다.
 *
 * seqlock:
 *   writer: seq를 홀수로(쓰기 시작) → 데이터 갱신 → seq를 짝수로(쓰기 끝)
 *   reader: seq 읽고(홀수면 재시도) → 데이터 복사 → seq 다시 읽어 같으면 성공
 * mutex와 달리 reader는 writer를 막지 않는다. 대신 reader가 재시도할 수 있다.
 * 주의: reader가 읽는 데이터에 포인터/리소스가 있으면 위험(찢어진 값을 잠깐 본다)
 *       → POD 스냅샷에만 사용.
 * 빌드: cc -std=c11 -Wall -Wextra -pthread 04_seqlock_latest.c
 */
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    int32_t  rsrp_dbm;      /* 불변식: rsrq_db == -rsrp_dbm 로 테스트에서 검증 */
    int32_t  rsrq_db;
    uint32_t uptime_s;
    uint8_t  sim_slot;
} LinkStatus;

typedef struct {
    _Atomic uint32_t seq;   /* 짝수 = 안정, 홀수 = 쓰기 중 */
    LinkStatus       data;  /* seq로 보호되는 평범한 구조체 */
} SeqStatus;

void seq_init(SeqStatus *s);
void seq_write(SeqStatus *s, const LinkStatus *in);   /* writer 1명 전용 */
void seq_read(const SeqStatus *s, LinkStatus *out);   /* reader 여러 명 */

/* ------------------------------------------------------------------
 * 여기부터 직접 구현한다. 위의 선언부와 아래 self-test는 그대로 두고,
 * 아래 함수 목록을 채운 뒤 `make run N=04` 로 검증한다.
 *
 * 구현할 함수:
 *   - seq_init()
 *   - seq_write()
 *   - seq_read()
 *
 * 막히면 ../solutions/04_seqlock_latest.c 를 열되, 먼저 15분은 스스로 해볼 것.
 * ------------------------------------------------------------------ */

/* ------------------------------- self test ------------------------------- */
#define NREADERS  4
#define NWRITES   300000

static SeqStatus g_status;
static _Atomic bool g_stop;
static _Atomic unsigned long g_reads, g_bad;

static void *writer(void *arg)
{
    (void)arg;
    for (uint32_t i = 1; i <= NWRITES; i++) {
        LinkStatus s = {
            .rsrp_dbm = -(int32_t)(i % 120),
            .rsrq_db  =  (int32_t)(i % 120),     /* 불변식: rsrq == -rsrp */
            .uptime_s = i,
            .sim_slot = (uint8_t)(i & 1u),
        };
        seq_write(&g_status, &s);
    }
    g_stop = true;
    return NULL;
}

static void *reader(void *arg)
{
    (void)arg;
    while (!g_stop) {
        LinkStatus s;
        seq_read(&g_status, &s);
        /* 스냅샷이 일관적이면 두 필드가 같은 write에서 나온다 */
        if (s.uptime_s != 0 && s.rsrq_db != -s.rsrp_dbm)
            atomic_fetch_add(&g_bad, 1u);
        atomic_fetch_add(&g_reads, 1u);
    }
    return NULL;
}

int main(void)
{
    pthread_t w, r[NREADERS];
    seq_init(&g_status);

    for (int i = 0; i < NREADERS; i++) pthread_create(&r[i], NULL, reader, NULL);
    pthread_create(&w, NULL, writer, NULL);
    pthread_join(w, NULL);
    for (int i = 0; i < NREADERS; i++) pthread_join(r[i], NULL);

    printf("writes=%d reads=%lu inconsistent=%lu\n",
           NWRITES, (unsigned long)g_reads, (unsigned long)g_bad);
    assert(g_bad == 0);
    assert(g_status.data.uptime_s == NWRITES);
    puts("04 PASS");
    return 0;
}
