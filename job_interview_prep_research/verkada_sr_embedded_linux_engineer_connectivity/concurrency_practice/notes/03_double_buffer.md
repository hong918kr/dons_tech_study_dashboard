# 03. Double buffering — 락으로 데이터가 아니라 '소유권'을 지킨다

> **이 노트를 다 읽으면**
> - 생산자를 한 번도 블록시키지 않으면서 소비자가 tearing 없는 프레임만 보게 만들 수 있다
> - "락은 데이터가 아니라 소유권을 보호한다"는 설계를 코드로 옮길 수 있다
> - 버퍼 개수와 drop 정책의 관계를 설명하고, triple buffering 이 왜 필요한지 말할 수 있다
>
> **선행**: `00_start_here` 의 §3 (mutex), §6 (실시간 데이터 전달 패턴), `01_bounded_queue`
> **연습**: `make run N=03` (내 구현) · `make sol N=03` (모범답안) · `make tsan N=03`

---

## 0. 한 문장으로

**버퍼 두 개를 번갈아 쓰면서, 생산자가 한쪽을 채우는 동안 소비자는 다른 쪽을 읽게 하는 패턴(ping-pong)** 이다. 리크루터 메일에 주제가 직접 적혀 있고, 센서 프레임이나 카메라 프레임처럼 "최신값만 맞으면 되는 데이터"를 실시간 경로에서 넘길 때 쓴다.

---

## 1. 왜 필요한가 — 비유로 시작

칠판이 두 개 있다. 선생님(생산자)은 한 칠판에 글을 쓰고 학생(소비자)은 다른 칠판을 읽는다. 선생님이 다 쓰면 두 칠판을 **바꾼다**. 학생이 읽는 도중에 선생님이 그 칠판을 지우고 새로 쓰는 일은 절대 없으므로, 학생은 **반쯤 지워진 글**을 볼 일이 없다.

```
   상태 A                          swap             상태 B
  ┌──────────┐ ┌──────────┐               ┌──────────┐ ┌──────────┐
  │ WRITER   │ │ READER   │   ────────▶   │ READER   │ │ WRITER   │
  │ buf0 채움 │ │ buf1 읽음 │               │ buf0 읽음 │ │ buf1 채움 │
  └──────────┘ └──────────┘               └──────────┘ └──────────┘
```

베어메탈에서 이미 해 본 것이다. **ping-pong DMA** — DMA 가 buf[0] 을 채우는 동안 CPU 가 buf[1] 을 처리하고, 완료 인터럽트에서 디스크립터를 바꿔 끼운다. 여기서 달라지는 건 "완료 인터럽트" 자리에 **mutex 로 보호되는 인덱스 교체**가 들어간다는 것뿐이다.

**왜 그냥 큐(문제 01)를 안 쓰나?** 프레임이 512바이트~수 MB 라 큐에 넣으려면 **복사**를 해야 하는데 그 비용이 크고, 소비자가 느려서 큐가 차면 01은 생산자를 **블록**시킨다. 센서/DMA 는 기다릴 수 없다 — 프레임이 밀리면 하드웨어 오버런이 난다. 그래서 요구가 이렇게 바뀐다. **복사하지 않는다. 생산자는 절대 기다리지 않는다. 소비자가 못 따라오면 중간 프레임은 버린다 — 어차피 필요한 건 최신 프레임이다.**

---

## 2. 알아야 할 개념

| 용어 | 한 줄 정의 | 왜 존재하나 / 내가 아는 것과의 대응 |
|---|---|---|
| tearing | 절반만 갱신된 데이터를 읽는 것 | 프레임 앞은 새 값, 뒤는 옛 값. 화면 찢김의 그 tearing |
| 소유권(ownership) | 이 버퍼를 지금 만질 자격이 누구에게 있는가 | 락이 보호하는 진짜 대상. DMA 디스크립터 소유 비트와 같은 개념 |
| publish | 다 채운 버퍼를 "이제 읽어도 된다"고 공개하는 것 | 02의 release store 와 같은 역할, 여기서는 mutex 가 대신한다 |
| drop | 소비되지 못한 프레임을 버리는 것 | 반드시 **센다**. 조용히 버리면 현장에서 원인 추적이 불가능 |
| latest-only | 최신값만 의미 있는 데이터 | RSRP 같은 신호 품질, 온도, 카메라 프레임 |
| ping-pong / triple buffering | 버퍼 2개를 번갈아 씀 / 3개로 늘려 writer 가 막히지 않게 함 | DMA ping-pong 과 동일. `NBUF` 를 3으로 바꾸면 이 코드가 그대로 된다 |

### 핵심 아이디어: 락은 데이터가 아니라 소유권을 지킨다

보통 배우는 방식은 "공유 데이터를 락으로 감싸라"다. 그대로 하면 `lock(); memcpy(dst, src, 512); unlock();` 이 되고, 생산자와 소비자가 서로를 **복사 시간만큼** 기다린다. 프레임이 커질수록 나빠진다.

이 문제의 답은 다르다. **데이터는 락 밖에서 만지고, 락 안에서는 인덱스 몇 개만 바꾼다.** 락은 "지금 이 버퍼를 누가 소유하는가"만 지킨다. 소유권이 겹치지 않으면 데이터 접근은 애초에 동시에 일어나지 않으므로 보호할 필요가 없다. 임계구역은 인덱스 두세 개를 바꾸는 것뿐이라 수 나노초다. 각 버퍼는 항상 셋 중 하나의 상태다.

```
              write_begin          commit         read_acquire
   ┌────────┐ ──────────▶ ┌────────┐ ────────▶ ┌───────┐ ──────────▶ ┌────────┐
   │  FREE  │             │ WRITER │           │ READY │             │ READER │
   └────────┘             └────────┘           └───────┘             └────────┘
       ▲  ▲                 채우는 중           publish됨              읽는 중
       │  └──── write_begin 이 훔침 (drop) ────────┘                      │
       └──────────────────── read_release ────────────────────────────────┘
```

---

## 3. 문제 읽기

| 요구 | 설계에 주는 제약 |
|---|---|
| tearing 절대 금지 | reader 가 들고 있는 버퍼는 writer 가 **절대** 건드리지 않는다 |
| 생산자 블로킹 금지 | `db_write_begin` 은 **항상 성공** → 쓸 곳이 없으면 뭔가를 버린다 |
| 복사 금지 | 버퍼 **포인터**를 넘긴다. 데이터 write/read 는 락 밖에서 |
| 최신 프레임만 필요 | 오래된 READY 는 덮어써도 된다. 대신 카운트한다. 소비할 게 없으면 `db_read_acquire` 는 블로킹 없이 `NULL` 반환 |

```c
typedef struct {
    Frame           buf[NBUF];
    int             write_idx;      /* writer 소유 버퍼, 없으면 -1 */
    int             ready_idx;      /* publish된 최신 버퍼, 없으면 -1 */
    int             reader_idx;     /* reader 대여 중 버퍼, 없으면 -1 */
    unsigned long   dropped;        /* 소비되지 못하고 버려진 프레임 수 */
    pthread_mutex_t m;
} DoubleBuf;
```

인덱스 세 개가 그대로 세 가지 소유 상태다(`-1` 은 "해당 없음"). `dropped` 를 구조체 필드로 들고 있는 것도 설계의 일부다 — **버릴 거면 반드시 센다.** **불변식**은 이렇다.

```
  write_idx, ready_idx, reader_idx 는 서로 같을 수 없다 (-1 제외)
  reader_idx 가 가리키는 버퍼는 writer 가 절대 쓰지 않는다   ← tearing 방지의 전부
  세 인덱스와 dropped 는 오직 m 을 잡은 상태에서만 읽고 쓴다
  생산자는 하나뿐이다 (write_idx 가 하나이므로)
```

API 가 **획득/반납 쌍**으로 되어 있는 것 자체가 소유권 이전을 표현한다.

```c
Frame *db_write_begin(DoubleBuf *db);   /* writer: 채울 버퍼 확보(항상 성공) */
void   db_write_commit(DoubleBuf *db);  /* writer: publish */
Frame *db_read_acquire(DoubleBuf *db);  /* reader: 최신 프레임 대여, 없으면 NULL */
void   db_read_release(DoubleBuf *db);  /* reader: 반납 */
```

---

## 4. 단계별로 만들기

### 4.1 초기화 — `-1` 이 기본값

```c
void db_init(DoubleBuf *db)
{
    memset(db, 0, sizeof *db);
    db->write_idx = db->ready_idx = db->reader_idx = -1;
    pthread_mutex_init(&db->m, NULL);
}
```

`memset` 으로 0 을 채운 **다음** 세 인덱스를 `-1` 로 덮는다. 0 은 유효한 버퍼 번호라서 "없음"을 뜻할 수 없기 때문이다. `memset` 뒤에 `pthread_mutex_init` 을 부르는 순서도 중요하다 — 반대로 하면 초기화된 mutex 를 0 으로 밀어 버린다.

### 4.2 write_begin — 빈 버퍼를 찾고, 없으면 READY 를 희생시킨다

```c
Frame *db_write_begin(DoubleBuf *db)
{
    pthread_mutex_lock(&db->m);
    int idx = -1;
    for (int i = 0; i < NBUF; i++) {              /* 아무도 소유하지 않은 버퍼 */
        if (i != db->ready_idx && i != db->reader_idx) { idx = i; break; }
    }
    if (idx < 0) {
        /* reader가 하나, READY가 하나를 차지 → READY를 희생시킨다.
         * (절대 reader 버퍼는 건드리지 않는다: 그게 tearing의 원인) */
        idx = db->ready_idx;
        db->ready_idx = -1;
        db->dropped++;
    }
    db->write_idx = idx;
    pthread_mutex_unlock(&db->m);
    return &db->buf[idx];                          /* 데이터 write는 락 밖에서 */
}
```

먼저 `ready_idx` 도 `reader_idx` 도 아닌 버퍼, 즉 **FREE 버퍼**를 찾는다. `write_idx` 를 검사하지 않아도 되는 이유는 생산자가 하나뿐이라 이 함수에 들어온 시점에 `write_idx == -1` 이 보장되기 때문이다(직전 commit 이 `-1` 로 되돌린다). `NBUF == 2` 에서 FREE 가 없다는 건 **reader 가 하나, READY 가 하나**를 차지했다는 뜻이다. 생산자는 기다릴 수 없으므로 둘 중 하나를 빼앗아야 하는데 선택지는 하나뿐이다. reader 버퍼를 빼앗으면 소비자가 읽는 중에 데이터가 바뀐다 = **tearing**, 절대 금지. READY 버퍼를 빼앗으면 아직 아무도 안 읽은 프레임 하나가 사라진다 = **drop**, 허용. 요구사항이 *최신 프레임만 필요*이므로 정확히 옳은 선택이고, 대신 `dropped++` 로 **반드시 센다**.

마지막 줄이 이 설계의 요점이다. **버퍼 포인터를 반환하고 락을 놓는다.** 호출자는 락 밖에서 느긋하게 512바이트를 채우고, 그동안 소비자는 자유롭게 다른 버퍼를 읽는다. **`db->write_idx = idx;` 를 빼먹으면** commit 이 `-1` 을 publish 해서 소비자가 영원히 프레임을 못 받는다.

### 4.3 write_commit — 인덱스 두 개 옮기는 게 전부

```c
void db_write_commit(DoubleBuf *db)
{
    pthread_mutex_lock(&db->m);
    if (db->ready_idx >= 0)
        db->dropped++;                             /* 소비 전에 더 새 프레임이 나옴 */
    db->ready_idx  = db->write_idx;                /* publish */
    db->write_idx  = -1;
    pthread_mutex_unlock(&db->m);
}
```

이 줄 이후로 그 버퍼는 writer 소유가 아니다. `if (db->ready_idx >= 0) db->dropped++;` 는 **다른 종류의 drop** 이다 — 소비자가 가져가기 전에 더 새로운 프레임이 완성돼 옛 READY 가 밀려난 경우다. 4.2의 drop 이 "쓸 곳이 없어서 빼앗은 것"이라면 이건 "더 새 게 나와서 밀려난 것"이고, 둘 다 세야 `read + dropped + leftover == 생산한 프레임 수` 가 맞는다.

여기서 조용히 일어나는 중요한 일이 하나 있다. 생산자는 **락 밖에서** 512바이트를 썼는데 소비자가 그 데이터를 제대로 볼까? 본다. 생산자의 데이터 write 는 이 함수의 `pthread_mutex_lock` 보다 앞에 있고, `unlock` 은 release 로 동작하며, 소비자는 `db_read_acquire` 에서 **같은 mutex 를** lock(acquire) 한다. 그래서 "unlock 이전에 쓴 모든 것"이 "lock 이후"에 보인다(`00_start_here` §3). 02에서 손으로 짜야 했던 release/acquire 짝을 여기서는 **mutex 가 공짜로 해 준다.**

### 4.4 read_acquire — READY 를 reader 소유로 이전

```c
Frame *db_read_acquire(DoubleBuf *db)
{
    Frame *f = NULL;
    pthread_mutex_lock(&db->m);
    if (db->reader_idx < 0 && db->ready_idx >= 0) {
        db->reader_idx = db->ready_idx;            /* READY를 reader 소유로 이전 */
        db->ready_idx  = -1;
        f = &db->buf[db->reader_idx];
    }
    pthread_mutex_unlock(&db->m);
    return f;
}
```

조건이 둘이다. `reader_idx < 0` 은 "이미 하나 대여 중이면 또 빌려주지 않는다"(안 그러면 반납 짝이 안 맞는다), `ready_idx >= 0` 은 "publish 된 게 있어야 한다"이다. 그리고 `db->ready_idx = -1;` 이 핵심이다. READY 에서 빼내는 순간 writer 가 이 버퍼를 빼앗을 후보에서 사라진다. **이 한 줄이 tearing 을 막는다.** 가져갈 게 없으면 `NULL` 을 반환하고 **블로킹하지 않는다**. 그다음은 호출자 몫이다 — 테스트는 폴링하지만 실제 코드라면 condvar 로 재우는 게 맞다(테스트 주석도 그렇게 적혀 있다).

### 4.5 read_release — 반납

```c
void db_read_release(DoubleBuf *db)
{
    pthread_mutex_lock(&db->m);
    db->reader_idx = -1;                           /* 다시 free 상태로 */
    pthread_mutex_unlock(&db->m);
}
```

한 줄이지만 **빼먹으면 곧바로 망가진다.** `reader_idx` 가 계속 잡혀 있으면 writer 는 매 프레임 READY 를 빼앗아야 하고(`dropped` 폭증), `db_read_acquire` 는 `reader_idx < 0` 조건 때문에 영원히 `NULL` 을 반환한다 — **완전한 stall** 이다. 락 없이 대입만 해도 될 것 같지만 안 된다. 그 대입이 writer 에게 보이는 시점이 보장되지 않고, TSan 은 정확히 여기에 data race 를 보고한다.

---

## 5. 전체 흐름 따라가기

`NBUF = 2` 에서 소비자가 느릴 때다. 표기는 `W`=writer 소유, `R`=ready, `C`=reader 소유.

```
 시간 ↓  생산자                         소비자              buf0   buf1   dropped
  t0     write_begin → idx 0            (없음)              W      -        0
  t1     seq=1, memset (락 밖)                              W      -        0
  t2     write_commit → ready=0                             R      -        0
  t3                                    read_acquire → 0    C      -        0
  t4     write_begin → idx 1 (FREE)                         C      W        0
  t5     seq=2, memset (락 밖)                              C      W        0
  t6     write_commit → ready=1                             C      R        0
  t7     write_begin: FREE 없음!                            C      R        0
  t8       → ready(1) 을 빼앗음         (아직 seq=1 읽는 중) C      W        1
  t9     seq=3, memset (락 밖)                              C      W        1
  t10    write_commit → ready=1                             C      R        1
  t11                                   read_release        -      R        1
  t12                                   read_acquire → 1    -      C        1
```

t8 이 이 문제의 전부다. 생산자는 **기다리지 않았고**, 소비자가 읽던 buf0 은 **건드리지 않았고**, 대신 아직 아무도 안 읽은 seq=2 프레임을 버리고 카운트했다. 소비자는 t12 에서 seq=1 다음에 seq=3 을 받는다 — 프레임은 건너뛰었지만 **찢어지지는 않았다.** `NBUF` 를 3으로 바꾸면 t7 에서 세 번째 FREE 버퍼가 있어 drop 이 사라진다. 이게 **triple buffering** 이고, 이 코드는 상수 하나만 바꾸면 그대로 동작한다(`#define NBUF 2 /* 3으로 바꾸면 그대로 triple buffering이 된다 */`). 트레이드오프는 명확하다 — 메모리 한 프레임분을 더 쓰는 대신 drop 을 줄이고, 대신 latency 가 한 프레임 늘어날 수 있다. "메모리 vs 최신성"의 선택이고, 면접에서 이렇게 정리해 말하면 좋다.

---

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
|---|---|---|---|
| 락 안에서 프레임을 복사 | 동작은 맞지만 느리고 생산자가 밀림 | 임계구역에 512바이트 memcpy 가 들어감 | 포인터만 넘기고 데이터는 락 밖에서 |
| writer 가 reader 버퍼를 빼앗음 | `torn > 0`, 프레임 앞뒤 값이 다름 | 읽는 중에 덮어씀 = tearing | FREE 가 없으면 **READY** 를 희생 |
| `read_acquire` 에서 `ready_idx = -1` 생략 | 간헐적 tearing | writer 가 여전히 그 버퍼를 후보로 봄 | 대여 시 READY 에서 반드시 뺀다 |
| `db_read_release` 누락 | `dropped` 폭증 후 완전 stall | `reader_idx` 가 영원히 잡혀 있음 | acquire/release 를 쌍으로 |
| 인덱스를 락 없이 수정 | TSan data race, 드물게 두 인덱스가 같아짐 | 가시성·원자성 없음 | 세 인덱스 전부 락 안에서만 |
| drop 을 세지 않음 | 현장에서 "프레임이 튄다"를 설명 못 함 | 증거가 없음 | `dropped` 카운터 + 텔레메트리 |
| `db_write_begin` 이 실패 가능 | 생산자가 재시도/블로킹 → 하드웨어 오버런 | 실시간 제약 위반 | 항상 성공하도록 drop 정책 내장 |

---

## 7. 직접 확인하기

```sh
make sol N=03
```

```
frames=20000 read=103 dropped=19897 leftover=0 torn=0 ooo=0
03 PASS
```

숫자를 읽는 법은 이렇다.

- `torn=0` — **가장 중요한 값.** 프레임이 하나도 찢어지지 않았다. 0 이 아니면 설계가 틀렸다.
- `ooo=0` — out of order 가 없다. 항상 최신 프레임만 오므로 seq 는 단조 증가해야 한다.
- `read=103`, `dropped=19897` — **실행할 때마다 크게 달라진다.** 소비자가 프레임마다 512바이트를 전부 검사하느라 생산자보다 훨씬 느려서 대부분이 버려진다. **정상이고 의도된 동작**이다(TSan 빌드에서는 상대 속도가 달라져 `read=226` 같은 값이 나온다). `leftover` 는 마지막 프레임이 publish 된 직후 소비자가 종료했으면 1 이다.

검증의 핵심은 self test 가 프레임 전체를 같은 값으로 채운다는 데 있다(`memset(f->data, (int)(s & 0xFF), FRAME_LEN);`). 소비자는 512바이트가 전부 기대값과 같은지 확인하므로, 중간에 writer 가 끼어들어 덮어썼다면 앞뒤 값이 달라져 **tearing 이 반드시 검출된다.**

```c
unsigned char expect = (unsigned char)(f->seq & 0xFF);
for (int i = 0; i < FRAME_LEN; i++) {
    if (f->data[i] != expect) { g_torn++; break; }
}
```

마지막 `assert(g_read_ok + g_db.dropped + leftover == NFRAMES);` 는 회계가 맞는지 본다. **생산한 모든 프레임은 읽혔거나, 버려졌거나, 마지막으로 남아 있다** — 셋 중 하나여야 하고 합이 정확히 맞아야 한다. drop 카운터가 한 군데라도 빠지면 이 assert 가 잡아낸다.

```sh
make tsan N=03
```

모범답안은 깨끗하다. 인덱스 수정을 락 밖으로 빼거나 `db_read_release` 의 락을 지우면 `WARNING: ThreadSanitizer: data race` 가 `reader_idx` 나 `ready_idx` 를 지목하며 뜬다.

---

## 8. 면접에서 말하기

- "락으로 데이터를 보호하지 않고 **버퍼 소유권**을 보호합니다. 임계구역은 인덱스 몇 개를 바꾸는 것뿐이고, 실제 프레임 read/write 는 락 밖에서 일어납니다. 복사가 없습니다."
- "각 버퍼는 항상 writer 소유, READY, reader 소유 셋 중 하나입니다. reader 가 들고 있는 버퍼는 writer 가 절대 건드리지 않습니다. 그 규칙 하나가 tearing 을 막습니다."
- "생산자는 절대 블록하지 않습니다. 쓸 버퍼가 없으면 아직 소비되지 않은 READY 를 버리고 카운터를 올립니다. 최신 프레임만 의미 있는 데이터라 옳은 선택이고, drop 은 반드시 세서 텔레메트리로 올립니다."
- "버퍼가 2개면 reader 가 하나를 들고 있을 때 drop 이 생기고, 3개로 늘리면 writer 가 막히지 않습니다 — 메모리 한 프레임 대 drop 의 트레이드오프고 이 코드는 상수 하나로 바뀝니다. 데이터를 락 밖에서 쓰는데도 안전한 이유는 commit 의 unlock 과 acquire 의 lock 이 같은 mutex 라 happens-before 가 성립하기 때문입니다."

- "The lock protects buffer **ownership**, not the frame data. The critical section is just a couple of index assignments, so it's nanoseconds and there is no copy."
- "Every buffer is in exactly one of three states: owned by the writer, ready, or owned by the reader. The writer never touches the buffer the reader holds — that's the whole tearing guarantee."
- "The writer never blocks. If no free buffer exists it sacrifices the un-consumed ready buffer and increments a drop counter, which we export as telemetry."
- "With two buffers a slow reader forces drops; a third buffer removes them at the cost of one more frame of memory. Writing the payload outside the lock is still safe because commit's unlock and acquire's lock are on the same mutex, so there's a happens-before edge."

---

## 9. 요약 & 체크리스트

double buffering 은 "실시간 생산자는 기다릴 수 없다"는 제약에서 출발한 설계다. 데이터를 락으로 감싸는 대신 **소유권**을 락으로 관리하고, 버퍼 포인터를 넘겨 복사를 없애고, 쓸 곳이 없으면 reader 버퍼가 아니라 READY 버퍼를 희생하고, 버린 개수를 반드시 센다. 세 인덱스(`write_idx` / `ready_idx` / `reader_idx`)가 곧 세 가지 소유 상태이고, `NBUF` 를 3으로 올리면 그대로 triple buffering 이 된다.

- [ ] 세 가지 소유 상태와 각 상태 전이 함수를 그림으로 그릴 수 있다
- [ ] "락은 데이터가 아니라 소유권을 보호한다"를 한 문장으로 설명할 수 있다
- [ ] FREE 가 없을 때 READY 를 버리고 reader 버퍼는 안 건드리는 이유를 말할 수 있다
- [ ] drop 이 두 군데(`write_begin`, `write_commit`)에서 발생하는 이유를 각각 설명할 수 있다
- [ ] 락 밖에서 쓴 데이터가 소비자에게 보이는 이유를 mutex 의 happens-before 로 설명할 수 있다
- [ ] double 과 triple buffering 의 트레이드오프를 말할 수 있다
- [ ] `make sol N=03` 에서 `torn=0 ooo=0` 을 직접 확인했다
