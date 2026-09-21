# 04. Latest-value 공유 (seqlock) — writer는 절대 블록되지 않는다

> **이 노트를 다 읽으면**
> - seq를 홀수/짝수로 바꾸는 규약이 왜 "찢어진 스냅샷"을 걸러내는지 설명할 수 있다
> - 이 코드가 C 표준상 **data race** 이고 ThreadSanitizer가 잡아낸다는 것, 그리고 실무에서는 무엇으로 대체하는지 말할 수 있다
> - reader 재시도 루프를 `do/while + continue` 로 쓰면 왜 UB가 되는지 짚을 수 있다
>
> **선행**: `00_start_here` 의 §5 원자 연산과 메모리 순서, §6 최신값 패턴. `03_double_buffer` 를 먼저 읽으면 비교가 쉽다.
>
> **연습**: `make run N=04` (내 구현) · `make sol N=04` (모범답안) · `make tsan N=04`

---

## 0. 한 문장으로

**seqlock은 "쓰는 쪽이 절대 기다리지 않는" 최신값 공유 패턴이고, 읽는 쪽이 대신 재시도한다.**
Verkada 게이트웨이에서 모뎀 스레드가 1초마다 갱신하는 링크 상태(RSRP, RSRQ, uptime, SIM 슬롯)를
HTTP 핸들러·CLI·텔레메트리 업로더가 아무 때나 읽어가는 구조가 정확히 이 모양이다.

---

## 1. 왜 필요한가 — 비유로 시작

사무실 화이트보드에 당직자가 현재 상태를 적어둔다. 지나가는 사람은 아무 때나 그걸 본다.
당직자가 "지우고 다시 쓰는 중"인데 누가 보면 절반은 새 값, 절반은 옛날 값인 **찢어진(torn)**
내용을 읽는다. RSRP는 새 값인데 RSRQ는 이전 값 — 두 필드가 서로 다른 시점에서 온다.
해결책은 두 가지다. 화이트보드에 자물쇠를 달아 읽는 사람이 있으면 당직자가 기다리게 하거나(mutex),
아니면 **보드 옆에 "수정 횟수" 카운터**를 두고 읽는 사람이 스스로 확인하게 한다(seqlock).

```
 화이트보드 옆 카운터 seq

 seq=4 (짝수)  ── 안정. 지금 읽으면 된다
 seq=5 (홀수)  ── 당직자가 지우개 들고 있음. 읽어봐야 소용없다
 seq=6 (짝수)  ── 새 내용 확정

 reader: 읽기 전 seq=4  →  내용 베껴적기  →  읽은 뒤 seq=4
         앞뒤가 같고 짝수 → 베낀 내용은 한 시점의 스냅샷이다
```

베어메탈 대응: ISR이 DMA 완료 시각·상태 레지스터 묶음을 전역 구조체에 쓰고, 메인 루프가
그걸 읽는 상황. 인터럽트를 끄고 읽으면(= critical section) ISR 지연이 생긴다. seqlock은
**ISR을 한 번도 막지 않고** 메인 루프만 다시 읽게 만드는 방식이다.

---

## 2. 알아야 할 개념

| 용어 | 한 줄 정의 | 왜 존재하나 | Don이 아는 것과의 대응 |
| --- | --- | --- | --- |
| torn read | 여러 필드를 읽는 도중 값이 바뀌어 서로 다른 시점이 섞이는 것 | 구조체 복사는 원자적이지 않다 | 32비트 MCU에서 64비트 카운터 읽다 상위/하위가 갈리는 것 |
| sequence counter | 쓰기 시작/끝을 홀수/짝수로 표시하는 정수 | reader가 "내가 읽는 동안 바뀌었나"를 사후 판정 | 버전 레지스터 / 프레임 카운터 |
| `_Atomic uint32_t` | C11 원자 변수. 동시 접근해도 race가 아니다 | 일반 변수는 동시 접근 자체가 UB | 하드웨어 원자 명령(LDREX/STREX) |
| `memory_order_release` | 이 store 이전의 쓰기들이 이 store보다 먼저 보이게 한다 | 컴파일러/CPU의 재배치를 막는다 | DMB/DSB 배리어 |
| `memory_order_acquire` | 이 load 이후의 읽기들이 이 load보다 뒤로 가게 한다 | release와 짝을 이뤄 순서를 만든다 | 레지스터 읽고 나서 버퍼 읽기 순서 보장 |
| `atomic_thread_fence` | 특정 변수가 아니라 **그 지점**에 배리어를 친다 | 원자변수와 일반 변수 사이 순서를 잡을 때 | 명시적 DMB |
| POD | 포인터·소유권 없는 값만 든 구조체 | 찢어진 값을 잠깐 봐도 안전해야 하니까 | 레지스터 덤프 구조체 |

핵심은 마지막 줄이다. **seqlock reader는 버려질 쓰레기 값을 한 번은 읽는다.** 그 값에 포인터가
들어 있으면 쓰레기 포인터를 역참조하게 된다. 그래서 seqlock은 POD 스냅샷 전용이다.

---

## 3. 문제 읽기

요구사항은 세 줄이다.

1. reader는 여러 필드를 **같은 시점의 스냅샷**으로 읽어야 한다 → 필드별 원자성으로는 부족하다.
2. reader 때문에 writer가 지연되면 **안 된다** → writer는 lock을 잡거나 기다릴 수 없다.
3. writer는 1명, reader는 여러 명 → writer끼리의 경쟁은 고려하지 않아도 된다.

2번이 mutex를 탈락시킨다. mutex를 쓰면 reader 4개가 번갈아 잡고 있는 동안 writer가 밀린다.
3번이 seqlock을 가능하게 한다. writer가 여럿이면 seq 증가 자체를 또 보호해야 하고, 그 순간
"writer가 안 막힌다"는 장점이 사라진다.

구조체는 이렇게 생겼다.

```c
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
```

`data` 는 `_Atomic` 이 아니라 **평범한 구조체**다. 이게 이 패턴의 전부이자, 뒤에서 이야기할
문제의 근원이기도 하다.

---

## 4. 단계별로 만들기

### 4-1. writer — 홀수로 올리고, 쓰고, 짝수로 내린다

```c
void seq_write(SeqStatus *s, const LinkStatus *in)
{
    uint32_t v = atomic_load_explicit(&s->seq, memory_order_relaxed);
    /* 1) 홀수로 올려 "쓰는 중"을 알린다 */
    atomic_store_explicit(&s->seq, v + 1u, memory_order_relaxed);
    /* 2) seq 증가가 데이터 write보다 먼저 보이도록 */
    atomic_thread_fence(memory_order_release);

    s->data = *in;

    /* 3) 데이터 write가 seq 확정보다 먼저 보이도록 */
    atomic_store_explicit(&s->seq, v + 2u, memory_order_release);
}
```

첫 load가 `relaxed` 인 이유는 writer가 1명이라 이 값을 경쟁할 상대가 없기 때문이다.
그냥 "내가 마지막으로 쓴 값"을 다시 읽는 것뿐이다.

`v + 1u` 로 홀수를 만드는 순간부터 reader는 전부 재시도한다. 그 다음 `release` fence가
없으면 컴파일러나 CPU가 `s->data = *in;` 을 seq 증가보다 **앞으로** 옮길 수 있다. 그러면
reader가 짝수 seq를 보면서 이미 바뀌기 시작한 데이터를 읽고 "유효하다"고 판정한다.

마지막 store의 `release` 는 반대 방향이다. 데이터 쓰기가 전부 끝난 뒤에야 짝수 seq가 보이게
한다. 이 순서가 깨지면 reader는 "쓰기 끝났다"는 신호를 먼저 보고 아직 안 쓴 필드를 읽는다.

**writer는 어디서도 기다리지 않는다.** 이게 요구사항 2번의 답이다.

### 4-2. reader — 앞뒤로 seq를 읽어 사후 검증한다

```c
    for (;;) {
        uint32_t before = atomic_load_explicit(&s->seq, memory_order_acquire);
        if (before & 1u) continue;               /* 쓰기 진행 중 → 재시도 */

        *out = s->data;                          /* 찢어진 값일 수 있다 */

        atomic_thread_fence(memory_order_acquire);
        uint32_t after = atomic_load_explicit(&s->seq, memory_order_relaxed);
        if (before == after) return;             /* 스냅샷 유효 */
    }
```

읽는 순서가 전부다. `before` 가 홀수면 writer가 작업 중이니 볼 것도 없이 다시 돈다.
짝수면 일단 복사한다 — **이 복사본은 아직 못 믿는다.** 복사가 끝난 뒤 seq를 다시 읽어
`before` 와 같으면, 복사하는 동안 writer가 한 번도 끼어들지 않았다는 뜻이므로 유효하다.

중간의 `acquire` fence가 없으면 CPU가 `after` 읽기를 데이터 복사보다 먼저 해버릴 수 있다.
그러면 "복사 후 검사"가 "검사 후 복사"가 되어 검증 자체가 무의미해진다.

`seq` 가 32비트라 이론상 4,294,967,296번 wrap되어 `before == after` 가 우연히 맞을 수 있지만,
그 사이에 reader가 멈춰 있어야 하므로 실무에서는 무시한다(64비트를 쓰면 이마저 사라진다).

### 4-3. 절대 이렇게 쓰지 말 것 — 실제로 있었던 버그

이 파일에는 주석으로 함정이 박제되어 있다.

```c
    /* ★ 함정: do/while + continue 로 쓰면 안 된다.
     *   do { ... if (before & 1) continue; ... after = load(); } while (before != after);
     *   에서 continue 는 '조건식'으로 점프하므로, 첫 바퀴에 홀수를 만나면
     *   초기화되지 않은 after 와 비교하게 된다(UB). 쓰레기 값이 우연히 before 와
     *   같으면 *out 을 채우지도 않고 반환한다. for(;;) + 명시적 return 으로 쓴다. */
```

이 버그가 무서운 이유는 **읽기 쉽고, 거의 항상 동작하기 때문**이다. `do/while` 에서 `continue`
는 루프 처음이 아니라 `while (...)` 조건식으로 점프한다. 첫 바퀴에 홀수를 만나면 `after` 는
아직 아무 값도 안 들어간 스택 쓰레기이고, 그걸 `before` 와 비교한다. 스택에 우연히 같은 값이
남아 있으면 루프를 빠져나가고, 호출자는 **한 번도 안 채워진 `*out`** 을 유효한 스냅샷으로 믿는다.

증상은 "1만 번에 한 번 링크 상태가 0으로 보인다" 같은 형태로 나타난다. 재현이 안 되고,
디버그 빌드에서는 스택이 달라 사라진다. `for(;;)` + 명시적 `return` 으로 쓰면 구조적으로
불가능해진다. **재시도 루프에서 `continue` 를 쓸 때는 어디로 점프하는지 반드시 확인한다.**

---

## 5. 전체 흐름 따라가기

```
 시각   writer(W)                      reader(R)              seq   R이 보는 것
 ---------------------------------------------------------------------------
  t0                                                            4
  t1                                  before = 4 (짝수)         4   통과
  t2    v=4, store seq=5                                        5
  t3    fence(release)                                          5
  t4    data.rsrp = -70                                         5   (R은 복사 중)
  t5                                  *out = s->data            5   찢어진 값!
  t6    data.rsrq = +70                                         5
  t7    store seq=6 (release)                                   6
  t8                                  fence(acquire)            6
  t9                                  after = 6                 6
 t10                                  4 != 6 → 재시도           6   버린다
 t11                                  before = 6 (짝수)         6   통과
 t12                                  *out = s->data            6   일관된 값
 t13                                  after = 6 → return        6   성공
```

t5에서 reader가 실제로 **깨진 값을 읽는다**는 점을 눈으로 봐야 한다. seqlock은 깨진 값을
막는 게 아니라 **깨졌다는 걸 사후에 알아채고 버리는** 패턴이다. 그래서 그 값이 포인터면
안 된다 — 버리기 전에 이미 역참조했을 테니까. 또 writer가 빠르고 reader가 느리면 reader가
계속 재시도만 하다 굶을 수 있다(starvation). writer 30만 회 동안 reader 4개가 5만 회대밖에
못 읽는 것이 그 흔적이다.

---

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| `do/while + continue` 로 재시도 | 드물게 `*out` 이 안 채워짐 | `continue` 가 조건식으로 점프, `after` 미초기화 UB | `for(;;)` + 명시적 `return` |
| fence 생략 | 아주 드물게 필드가 섞임 | 컴파일러/CPU가 데이터와 seq 순서를 바꿈 | release/acquire fence 양쪽 다 유지 |
| `before` 홀수 검사 생략 | 재시도가 늘고 가끔 통과 | 쓰기 중 스냅샷을 유효로 오판 | `if (before & 1u) continue;` |
| 데이터에 포인터/핸들 저장 | 세그폴트, use-after-free | 찢어진 포인터를 역참조 | POD만 담는다, 포인터는 08의 refcount 패턴으로 |
| writer를 여러 개로 | seq가 홀수에 멈추고 reader 무한 재시도 | writer끼리 seq를 덮어씀 | writer는 1개로 고정, 필요하면 writer 측만 mutex |
| reader에서 재시도 횟수 무제한 | writer 폭주 시 reader 굶음 | seqlock은 reader starvation을 막지 않는다 | 재시도 상한 + 실패 시 이전 값 사용 |

---

## 7. 직접 확인하기

```sh
make sol N=04
```

```
writes=300000 reads=56417 inconsistent=0
04 PASS
```

`inconsistent=0` 이 핵심이다. 테스트의 불변식은 `rsrq_db == -rsrp_dbm` — 한 번의 `seq_write`
안에서 두 필드가 항상 이 관계로 만들어지므로, reader가 서로 다른 write의 조각을 섞어 읽으면
이 등식이 깨진다. 0이 아니면 스냅샷이 찢어졌다는 증거다.

`reads` 가 `writes` 보다 훨씬 적은 것도 정보다. reader 4개가 재시도로 상당 시간을 버린다는 뜻이고,
writer 우선 패턴의 대가가 그대로 드러난다.

이제 TSan을 돌린다.

```sh
make tsan N=04
```

```
WARNING: ThreadSanitizer: data race (pid=13903)
  Write of size 4 at 0x000100be8008 by thread T5:
    #0 writer 04_seqlock_latest.c:98
  Previous read of size 4 at 0x000100be8004 by thread T1:
    #0 reader 04_seqlock_latest.c:109
  Location is global 'g_status'
SUMMARY: ThreadSanitizer: data race 04_seqlock_latest.c:98 in writer
```

**이건 TSan의 오탐이 아니다. 진짜로 C 표준상 data race다.** `s->data` 는 평범한 구조체이고,
writer가 쓰는 동안 reader가 읽는다. C11 표준은 "동기화 없이 한쪽이 쓰고 한쪽이 읽으면
정의되지 않은 동작"이라고 못 박는다. 우리가 나중에 seq로 걸러낸다는 사실은 표준이 알 바 아니다.
Makefile의 `all-tsan` 이 04만 `SKIP 04_seqlock_latest (seqlock: 의도된 data race)` 로 건너뛰는 것도 그래서다.

**그럼 실무에서는 어떻게 쓰나.** 답은 "데이터도 원자적으로 만든다"이다.

- 필드마다 `_Atomic` 으로 선언하고 `memory_order_relaxed` 로 읽고 쓴다. 순서는 여전히 seq의 release/acquire가 잡아주고, 개별 접근은 race가 아니게 되어 TSan이 조용해진다.
- 또는 데이터를 `unsigned char` 버퍼에 두고 `memcpy` + fence로 다룬다. Linux 커널의 `READ_ONCE`/`WRITE_ONCE` 가 정확히 이 역할이고, 커널 seqlock은 이 위에 서 있다.
- 사용자 공간이라면 08의 refcount 스왑이나 03의 더블버퍼가 대개 더 안전하다. seqlock은 "읽기가 압도적으로 잦고 데이터가 작은 POD"일 때만 이긴다.

동작하는 코드를 쓰는 사람과 **왜 이 코드가 표준상 위험한지 아는 사람**의 차이가 여기서 갈린다.

---

## 8. 면접에서 말하기

- seqlock은 writer를 절대 블록시키지 않는 대신 reader가 재시도하는 구조다. 상태 텔레메트리처럼 읽기가 잦고 쓰기가 짧을 때 맞다.
- seq가 홀수면 쓰기 중이라 건너뛰고, 복사 전후의 seq가 같아야만 스냅샷이 유효하다고 판정한다.
- reader는 찢어진 값을 실제로 한 번 읽는다. 그래서 POD만 담고, 포인터를 넣으면 use-after-free가 된다.
- 엄밀히 말하면 이 구현은 C 표준상 data race라 TSan이 잡는다. 실무에서는 필드별 relaxed atomic이나 memcpy + fence로 바꾼다. 커널의 READ_ONCE/WRITE_ONCE가 그 역할이다.
- writer가 여러 개면 seqlock의 전제가 깨지므로 writer 측에만 mutex를 얹거나 다른 패턴으로 간다.

- A seqlock lets the writer proceed without ever blocking; readers retry instead.
- The writer bumps the sequence to odd before the update and to even after, so readers can detect that a write overlapped their copy.
- Readers do observe torn data briefly, so the payload must be plain old data with no pointers or ownership.
- Strictly speaking this is a data race in C, and ThreadSanitizer flags it. Production code uses per-field relaxed atomics or memcpy with fences, like the kernel's READ_ONCE and WRITE_ONCE.
- It assumes a single writer. With multiple writers I'd serialize the writers with a mutex or pick a different pattern such as an RCU-style pointer swap.

---

## 9. 요약 & 체크리스트

seqlock은 "writer 우선" 트레이드오프를 명시적으로 고른 패턴이다. writer는 seq를 홀수로 올리고,
데이터를 쓰고, 짝수로 내린다. reader는 앞뒤 seq가 같고 짝수일 때만 복사본을 신뢰한다.
순서 보장은 release/acquire fence가 담당하고, 유효성 판정은 seq 비교가 담당한다. 대가는 세 가지다 —
reader 재시도(starvation 가능), POD 전용 제약, 그리고 C 표준상 data race라는 점.
실무 코드는 필드별 relaxed atomic이나 `READ_ONCE`/`WRITE_ONCE` 스타일로 이 마지막 문제를 없앤다.

- [ ] 홀수 = 쓰기 중, 짝수 = 안정이라는 규약을 말로 설명할 수 있다
- [ ] writer의 release fence와 마지막 release store가 각각 무엇을 막는지 구분할 수 있다
- [ ] reader가 왜 복사 **후에** seq를 다시 읽어야 하는지 설명할 수 있다
- [ ] `do/while + continue` 가 왜 미초기화 변수 비교로 이어지는지 설명할 수 있다
- [ ] 데이터에 포인터를 넣으면 안 되는 이유를 한 문장으로 말할 수 있다
- [ ] TSan 경고가 오탐이 아니라는 것과, 실무 대체 구현을 두 가지 댈 수 있다
- [ ] writer가 여러 개일 때 이 패턴이 깨지는 지점을 지적할 수 있다
- [ ] `make sol N=04` 의 `inconsistent=0` 이 무슨 불변식을 검증하는지 안다
