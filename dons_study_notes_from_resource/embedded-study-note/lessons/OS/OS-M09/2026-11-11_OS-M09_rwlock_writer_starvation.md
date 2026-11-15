---
id: OS-M09
time: 90분
updated: 2026-11-11
---
# OS-M09 reader-writer lock 구현과 writer starvation

## 학습 목표

- mutex 1개 + 조건 변수 2개로 **reader-preferring**과 **writer-preferring** rwlock을 직접 만든다. 두 정책은 딱 두 군데만 다르다는 것을 코드로 확인한다. 하나는 reader 입장 조건("writer가 *기다리기만* 해도 막는가")이고, 다른 하나는 writer가 나갈 때 누구를 먼저 깨우는가이다.
- **writer starvation**을 강제 스케줄로 **매번** 재현한다. reader 둘이 번갈아 겹치면 reader-pref의 writer는 K = 20번의 교대 동안 한 번도 못 들어간다. writer-pref에서는 바로 다음 차례에 들어간다. 판정은 시간이 아니라 카운터와 상태로 한다.
- 반대 문제(writer-pref의 **reader starvation**)도 재현한다. 이것을 **phase-fair** 정책으로 완화한다. writer가 나갈 때 기다리던 reader 전원을 한 묶음으로 직접 넘겨주므로, writer 사이에 reader 묶음이 반드시 한 번 끼고 양쪽 모두 굶지 않는다.
- 95/5 워크로드(8 스레드)에서 처리량, writer p99, writer max 대기를 정책별로 잰다. macOS `pthread_rwlock_t`와 mutex도 같이 잰다. reader가 쉬지 않고 겹치는 워크로드에서 reader-pref writer는 실측 **1초 동안 1번** 썼다.
- 펌웨어 설정 테이블 래퍼를 만든다. 같은 태스크의 중첩 읽기는 허용하고, R→W 승격은 기다리지 않고 `EDEADLK`로 거부한다. macOS/glibc의 실제 rwlock은 이 두 경우에 **멈춘다**(실측).

## 1. 문제

"rwlock을 짜 보세요. writer가 굶는 경우를 보여 주고 고쳐 보세요. 반대로 reader가 굶으면요?" 동시성 면접에서 자주 나오는 꼬리질문 사슬이다. 펌웨어에서는 **설정·캘리브레이션 테이블**이 전형적인 사례다. 제어 루프, 텔레메트리, 통신 태스크가 초당 수천 번 읽고, CLI나 OTA 태스크가 가끔 통째로 고친다. 정책을 잘못 고르면 "설정 변경 명령이 몇 초씩 안 먹는" 버그가 생긴다. 반대로 고르면 "설정을 연달아 바꾸는 동안 제어 루프가 멈추는" 버그가 생긴다.

앞 레슨에서 배운 것을 그대로 쓴다. mutex(OS-B05), 조건 변수의 while 재검사와 signal/broadcast(OS-B06), 스핀락과 공정성(OS-M07), 교착과 강제 스케줄(OS-M10), 락 순서(OS-M11)이다. OSTEP 31.5 *Reader-Writer Locks*는 세마포어 버전을 보여 주고 "writer가 굶을 수 있다"고만 말한다(연습 4에서 그 코드를 확인한다). 이 레슨은 굶는 것을 **결정적으로** 보이고, 정책을 바꿔 고친다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-weight="bold">강제 스케줄 ①: reader 둘이 번갈아 겹친다 (새 reader가 들어온 뒤에야 옛 reader가 나감)</text>
<text x="10" y="58" fill="currentColor">reader-pref</text>
<rect x="120" y="42" width="100" height="18" class="f-ok" stroke="currentColor"/><text x="125" y="56" fill="currentColor">RA</text>
<rect x="300" y="42" width="100" height="18" class="f-ok" stroke="currentColor"/><text x="305" y="56" fill="currentColor">RA</text>
<rect x="480" y="42" width="100" height="18" class="f-ok" stroke="currentColor"/><text x="485" y="56" fill="currentColor">RA</text>
<rect x="190" y="66" width="130" height="18" class="f-ok" stroke="currentColor"/><text x="195" y="80" fill="currentColor">RB</text>
<rect x="370" y="66" width="130" height="18" class="f-ok" stroke="currentColor"/><text x="375" y="80" fill="currentColor">RB</text>
<rect x="550" y="66" width="110" height="18" class="f-ok" stroke="currentColor"/><text x="555" y="80" fill="currentColor">RB</text>
<line x1="150" y1="104" x2="676" y2="104" stroke="currentColor" stroke-dasharray="5 4"/>
<text x="160" y="122" fill="currentColor">W: wrlock 안에서 대기 — active_readers가 한 번도 0이 안 됨 (K = 20 교대)</text>
<rect x="676" y="95" width="50" height="18" class="f-acc" stroke="currentColor"/><text x="688" y="109" fill="currentColor">W</text>
<line x1="10" y1="145" x2="730" y2="145" stroke="currentColor" stroke-opacity="0.4"/>
<text x="10" y="185" fill="currentColor">writer-pref</text>
<text x="10" y="201" fill="currentColor">phase-fair</text>
<rect x="120" y="170" width="140" height="18" class="f-ok" stroke="currentColor"/><text x="125" y="184" fill="currentColor">RA</text>
<line x1="200" y1="204" x2="400" y2="204" stroke="currentColor" stroke-dasharray="5 4"/>
<text x="205" y="220" class="f-warn" fill="currentColor">RB: writer가 기다리니 줄 선다</text>
<rect x="400" y="195" width="120" height="18" class="f-ok" stroke="currentColor"/><text x="405" y="209" fill="currentColor">RB</text>
<line x1="150" y1="244" x2="260" y2="244" stroke="currentColor" stroke-dasharray="5 4"/>
<rect x="260" y="235" width="140" height="18" class="f-acc" stroke="currentColor"/><text x="265" y="249" fill="currentColor">W (다음 차례)</text>
<text x="10" y="282" fill="currentColor">로그 실측: reader-pref "RA RB RA … RA W"(reader 21회 뒤 W) · writer-pref/phase-fair "RA W RB"</text>
</svg>
```

**세 정책 한눈에** (이 레슨의 `rwl_t` 기준):

| 정책 | reader가 기다리는 조건 | writer가 나갈 때 깨우는 순서 | 굶을 수 있는 쪽 |
|---|---|---|---|
| reader-pref (Courtois 외 1971의 1번 문제) | writer가 **안에 있을 때만** | 기다리는 reader 전원 → 없으면 writer 하나 | **writer** (reader가 끊임없이 겹치면) |
| writer-pref (같은 논문의 2번 문제) | writer가 안에 있거나 **기다리고 있으면** | 기다리는 writer 하나 → 없으면 reader 전원 | **reader** (writer가 줄줄이 오면) |
| phase-fair (reader 묶음 ↔ writer 교대) | writer-pref와 같음 | 기다리는 reader **전원을 한 묶음으로 직접 입장** → 없으면 writer | 없음: reader는 writer 한 판, writer는 reader 한 묶음 + writer 한 판까지만 기다림 |

### Part A — reader-pref와 writer-pref (TODO A1–A4)

- `rwl_t` = `pthread_mutex_t m` + `pthread_cond_t readers_ok, writer_ok` + `active_readers`, `writer_active`, `waiting_readers`, `waiting_writers`이다. **모든 정책이 `waiting_*`를 정확히 유지해야 한다.** 하네스가 이 값으로 "지금 줄을 섰다"를 판정한다.
- A1 `rw_basic_rdlock`, A2 `rw_basic_rdunlock`, A3 `rw_basic_wrlock`, A4 `rw_basic_wrunlock`을 만든다. 한 함수 안에서 `rw->policy`로 갈라진다.
- 테스트: 1스레드 카운터, reader 두 명 동시 입장, writer가 reader를 막음, reader가 writer를 막음, **강제 스케줄 ①**(K = 20)이다. 스트레스는 8 스레드 × 2만 연산(95/5)이다. 임계 구역 **안에서** 불변식(readers > 0 ⇒ writer 없음, writer ≤ 1)과 테이블 검사합을 확인하고, TSan으로도 돌린다.

### Part B — phase-fair (TODO B1–B4)

- `pf_rdlock`은 writer가 안에 있거나 기다리면 줄을 서고, `read_gen`이 바뀔 때까지 잔다. `pf_wrunlock`은 기다리는 reader가 있으면 `active_readers += waiting_readers; waiting_readers = 0; read_gen++; broadcast`로 **직접 넘긴다**.
- **강제 스케줄 ②**: R0이 읽는 중에 Wa가 줄을 서고, R1과 R2가 줄을 선다. 그다음 매 판 다음 writer가 줄을 선 상태에서 지금 writer가 나간다. writer-pref는 reader가 writer 20판을 내리 기다리고, phase-fair는 0판 만에 R1과 R2가 **함께** 들어와야 한다.

### Part C — 펌웨어 설정 테이블 래퍼 (TODO C1–C3)

- `cfg_read_lock`/`cfg_read_unlock`(C1): 태스크별(`_Thread_local`) 보유 기록으로 **중첩 읽기는 rwlock을 다시 잡지 않는다**. writer-pref/phase-fair에서 "R을 쥔 채 R을 다시 잡으면" 그사이에 writer가 줄을 선 순간 자기 교착이 되기 때문이다.
- `cfg_write_lock`(C2): R을 쥔 채 W를 잡거나(승격) W를 다시 잡으면 기다리지 않고 `EDEADLK`를 돌려준다. W 안에서 읽는 것은 허용한다. `cfg_write_unlock`(C3): 안에서 연 읽기가 아직 열려 있으면 `EPERM`을 돌려준다.
- 데모: macOS `pthread_rwlock_t`와 glibc(VM)가 같은 상황에서 실제로 어떻게 되는지 확인한다(`demo_pthread_rw.c`, `demo_glibc_rw.c`).

## 2. Problem (English)

> Implement a reader-writer lock from one `pthread_mutex_t` and two `pthread_cond_t` with three policies:
>
> ```c
> enum { RW_READER_PREF, RW_WRITER_PREF, RW_PHASE_FAIR };
> void rwl_init(rwl_t *, int policy);
> void rwl_rdlock(rwl_t *);  void rwl_rdunlock(rwl_t *);
> void rwl_wrlock(rwl_t *);  void rwl_wrunlock(rwl_t *);
> ```
>
> - *Reader-preferring*: a reader waits only while a writer **holds** the lock.
> - *Writer-preferring*: a reader also waits while a writer is **waiting**; on write-unlock wake one waiting writer first.
> - *Phase-fair*: on write-unlock, admit **all** currently waiting readers as one batch (direct hand-off), so a writer waits for at most one reader phase plus one writer phase and a reader waits for at most one writer phase.
>
> Keep `waiting_readers` / `waiting_writers` exact in every policy (the test harness observes them).
> Prove starvation **deterministically**: two readers overlap in a hand-over-hand pattern while a writer waits; the reader-preferring lock must not admit the writer within K = 20 hand-offs, the writer-preferring lock must admit it at the first opportunity. Then show the symmetric reader starvation of writer preference and that phase-fair removes it.
> Check mutual exclusion invariants inside the critical section (8 threads, 95 % reads), run under ThreadSanitizer.
>
> Firmware: a configuration table read by many tasks. Provide `cfg_read_lock/unlock` that are re-entrant per task, and `cfg_write_lock` that returns `EDEADLK` instead of blocking on an upgrade (read → write) or recursive write.
>
> Follow-ups: throughput and writer max/p99 wait for each policy vs `pthread_rwlock_t` at 95/5, 8 threads. What does macOS do on R→W upgrade? What is glibc's default policy?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
```

`/* BUILD: -pthread */`가 파일 첫 줄에 있다. 맥에서는 pthread가 libSystem 안에 있어서 `-pthread` 없이도 링크되지만, 리눅스와 같은 명령을 쓰려고 붙였다. 시작 코드의 TODO는 전부 빈 몸체(`(void)rw;`)라서 아무것도 막지 않는다. 그래서 **멈추지 않고** 0.14초 만에 FAIL로 끝난다. 스트레스 테스트는 단위 테스트가 통과한 정책만 돌린다. 시작 코드에서는 `SKIP`이 되므로, 락 없는 데이터 레이스(UB)를 실행하지 않는다. 실제 출력(PASS 줄 생략):

```text
== Part A/B: single-thread counters, basic schedules ==
  [reader-pref] single=0 basic_sched=0
FAIL starter.c:659  unit_ok[RW_READER_PREF]
…
== Part A: forced schedule 1 — readers overlap continuously, writer waits (K = 20) ==
  [reader-pref] handoffs while W waited=0 waited_all=0 W_in_next=1 blocked_reader_after_W=0
                order: RA W RB
FAIL starter.c:673  s[RW_READER_PREF].handoffs == K && s[RW_READER_PREF].writer_waited_all
…
SKIP stress reader-pref (unit tests failed)
…
FAIL starter.c:706  test_cfg_upgrade(RW_WRITER_PREF)
SKIP cfg_stress (unit tests failed)
2/21 passed
```

하네스 구성:

| 부품 | 하는 일 |
|---|---|
| `actor_t` | 명령(`CMD_RD`, `CMD_WRUN`, `CMD_CRD`…)을 하나씩 받아 실행하는 스레드. 상태 `A_IDLE`/`A_BUSY`(락 호출 안)/`A_IN_R`/`A_IN_W` |
| `rwl_snapshot` | `rw->m`을 쥐고 카운터 4개를 복사. "줄을 섰다" = `waiting_* ≥ n`(그 스레드가 `cond_wait` 안에서 mutex를 놓았다는 뜻) |
| `wait_until(pred)` | 조건이 참이 될 때까지 200 µs 간격으로 확인, 최대 1초. **판정은 조건으로만** 하고, 시간은 안전장치다 |
| 워치독 스레드 | 한 단계가 8초를 넘으면 `FAIL watchdog: …`를 찍고 `_exit(1)`. 틀린 구현(잃어버린 깨움 등)도 멈추지 않는다 |
| `cs_enter_*` | 임계 구역 안에서 seq_cst 카운터 `g_in_r`/`g_in_w`로 불변식 검사 + 입장 순서 로그 |

## 4. 힌트

<details><summary>힌트 1 — 조건 변수 루프의 모양 (OS-B06 그대로)</summary>

`lock(m); waiting++; while (못 들어감) cond_wait(cv, m); waiting--; 입장 표시; unlock(m);` 순서다. `waiting++`를 while **앞**에 두면, 하네스가 mutex를 쥐고 `waiting == 1`을 본 순간 그 스레드가 `cond_wait` 안에서 잠들어 있다는 것이 보장된다. mutex를 놓는 길은 `cond_wait`뿐이기 때문이다.
</details>

<details><summary>힌트 2 — 두 정책의 차이는 두 줄</summary>

reader 조건은 reader-pref가 `writer_active`, writer-pref가 `writer_active || waiting_writers > 0`이다. writer 조건은 두 정책 모두 `writer_active || active_readers > 0`이다. 나머지 차이는 `wrunlock`에서 누구를 먼저 깨우는지뿐이다.
</details>

<details><summary>힌트 3 — signal인가 broadcast인가</summary>

writer는 한 명만 들어갈 수 있으니 `signal(writer_ok)`이다. reader는 기다리던 전원이 같이 들어갈 수 있으니 `broadcast(readers_ok)`이다. reader를 `signal`로 하나만 깨우면, 나머지는 다음 writer가 나갈 때까지 잔다. 강제 스케줄 ②에서 R2가 영원히 안 깨어난다(§7 실측). 마지막 reader가 나갈 때(`active_readers == 0`) writer 하나를 깨우는 것도 잊지 않는다.
</details>

<details><summary>힌트 4 — phase-fair의 "직접 넘김"</summary>

writer가 나가면서 깨우기만 하면, reader가 mutex를 다시 잡기 전에 다음 writer나 새 reader가 먼저 끼어들 수 있다(barging). 그러면 "묶음"이 보장되지 않는다. 그래서 깨우는 쪽이 **mutex를 쥔 채** `active_readers`에 대기자 수를 미리 더한다. 깨어난 reader는 조건을 다시 볼 필요 없이 "내 세대(`read_gen`)가 지나갔나"만 확인한다. spurious wakeup에도 안전하다.
</details>

<details><summary>힌트 5 — 래퍼의 태스크별 기록</summary>

`_Thread_local` 배열에 `{lock, rdepth, wr}` 칸을 둔다(하네스의 `tl_slot`). 읽기: 이미 `rdepth > 0`이거나 `wr`이면 `rdepth++`만 한다. 아니면 진짜 `rwl_rdlock`. 쓰기: 칸이 있고 `rdepth > 0 || wr`이면 즉시 `EDEADLK`. 해제는 가장 바깥에서만 진짜 unlock을 한다.
</details>

## 5. 풀이

### Part A — 상태 네 개와 깨우기 규칙

**불변식.** mutex `m` 아래에서 항상 `writer_active ⇒ active_readers == 0`이고 writer는 최대 한 명이다. rwlock은 이 두 줄을 지키는 "입장 관리자"일 뿐이다. 임계 구역 자체는 `m` 밖에서 돈다. 그래서 reader 여럿이 동시에 테이블을 읽는다.

```c
/* A1 — reader 입장 */
pthread_mutex_lock(&rw->m);
rw->waiting_readers++;
while (rw->writer_active || (rw->policy == RW_WRITER_PREF && rw->waiting_writers > 0))
    pthread_cond_wait(&rw->readers_ok, &rw->m);
rw->waiting_readers--;
rw->active_readers++;
pthread_mutex_unlock(&rw->m);
```

| 사건 | reader-pref | writer-pref | 이유 |
|---|---|---|---|
| reader 입장 조건 | `!writer_active` | `!writer_active && waiting_writers == 0` | writer-pref는 "기다리는 writer" 뒤로 새 reader가 줄을 선다 |
| writer 입장 조건 | `!writer_active && active_readers == 0` | 같음 | |
| reader 퇴장 | 마지막 reader면 `signal(writer_ok)` | 같음 | writer는 한 명만 필요 |
| writer 퇴장 | reader 대기 있으면 `broadcast(readers_ok)`, 없으면 `signal(writer_ok)` | writer 대기 있으면 `signal(writer_ok)`, 없으면 `broadcast(readers_ok)` | 정책의 핵심 |

**잃어버린 깨움(lost wakeup)이 없는 이유.** writer가 잠드는 조건은 "누가 안에 있다"이다. 안에 있는 쪽이 나갈 때 반드시 깨운다. reader가 나가면 마지막 reader가, writer가 나가면 그 writer가 깨운다. writer-pref의 reader가 잠드는 조건 "writer가 기다린다"는 그 writer가 들어갔다 나가면서 `waiting_writers == 0`일 때 broadcast로 풀린다. `signal(writer_ok)`이 소비됐는데 그 writer가 다시 잠드는 경우도 문제가 없다. 예를 들어 reader-pref에서 새 reader가 먼저 끼어든 경우, 그 reader가 나갈 때 다시 signal한다. 연습 2는 이 논리가 깨지는 한 곳을 다룬다. writer가 **포기**하면 그 writer 때문에 잠든 reader를 누군가 깨워야 한다.

**강제 스케줄 ①이 결정적인 이유.** 하네스는 다음 순서로 진행한다.

1. RA가 `CMD_RD`로 들어간다.
2. W가 `CMD_WR`을 받고, 하네스는 `waiting_writers ≥ 1`이 될 때까지 기다린다. 이때 W는 `cond_wait` 안에 있다.
3. 매 판 새 reader(RB/RA 교대)에게 `CMD_RD`를 주고, **그 reader가 들어왔거나(`A_IN_R`) 줄을 설 때까지(`waiting_readers ≥ 1`)** 기다린다.
4. 들어왔으면 옛 reader를 내보낸다. 새 reader가 안에 있으므로 `active_readers`는 1 아래로 내려가지 않는다. 마지막 reader가 나갈 때만 writer를 깨우므로 W는 깨어날 일 자체가 없다.

어떤 스케줄링 순서에서도 같은 결과가 나온다. "몇 ms 안에 들어왔나" 같은 시간 판정은 하지 않는다.

```text
== Part A: forced schedule 1 — readers overlap continuously, writer waits (K = 20) ==
  [reader-pref] handoffs while W waited=20 waited_all=1 W_in_next=1 blocked_reader_after_W=0
                order: RA RB RA RB RA RB RA RB RA RB RA RB RA RB RA RB RA RB RA RB RA W
  [writer-pref] handoffs while W waited=0 waited_all=0 W_in_next=1 blocked_reader_after_W=1
                order: RA W RB
  [phase-fair ] handoffs while W waited=0 waited_all=0 W_in_next=1 blocked_reader_after_W=1
                order: RA W RB
```

reader-pref의 W는 RA와 RB가 21번 입장하는 동안 기다렸다. 하네스가 reader 공급을 멈추자(마지막 RA 퇴장) 그제야 들어갔다. 실제 시스템에서 reader 공급이 멈추지 않으면 W는 영원히 기다린다. 이것이 **writer starvation**이다. 아래 벤치마크 W2에서 1초 동안 1번이라는 숫자로 다시 나온다.

**스트레스와 불변식 검사.** `cs_enter_read`는 `g_in_r`을 올린 뒤 `g_in_w`를 확인하고, `cs_enter_write`는 `g_in_w`를 올린 뒤 `g_in_r`을 확인한다. 둘 다 seq_cst다. 이것은 Dekker/SB 리트머스 모양이라서, reader와 writer가 정말 겹쳤다면 **적어도 한쪽은 반드시 상대를 본다**(relaxed였다면 둘 다 0을 볼 수 있다, ARCH-A05/OS-M08 참고). 테이블은 일반 변수 16워드 + 검사합 + 버전이다. reader는 임계 구역 안에서 필드를 하나씩 읽고 검사합을 맞춰 본다.

```text
  [stress reader-pref] 8 threads x 20000 ops: reads=151857 writes=8143 violations=0 bad_sum=0 nonmono=0 version=8143
  [stress writer-pref] 8 threads x 20000 ops: reads=151857 writes=8143 violations=0 bad_sum=0 nonmono=0 version=8143
  [stress phase-fair ] 8 threads x 20000 ops: reads=151857 writes=8143 violations=0 bad_sum=0 nonmono=0 version=8143
```

(연산 종류는 스레드별 고정 seed의 xorshift32로 정하므로 reads/writes 수가 매번 같다. `version == writes`는 쓰기가 하나도 사라지지 않았다는 뜻이다.)

### Part B — phase-fair: reader 묶음과 writer의 교대

writer-pref는 writer starvation을 고치지만 대칭인 문제를 만든다. writer가 나가는 순간 다음 writer가 이미 줄에 있으면 reader는 또 기다린다. 강제 스케줄 ②가 이것을 20판 연속으로 만든다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="18" fill="currentColor" font-weight="bold">강제 스케줄 ②: R1·R2 줄 선 상태에서 writer가 연달아 온다</text>
<text x="10" y="56" fill="currentColor">writer-pref</text>
<rect x="110" y="42" width="60" height="18" class="f-ok" stroke="currentColor"/><text x="118" y="56" fill="currentColor">R0</text>
<rect x="170" y="42" width="70" height="18" class="f-acc" stroke="currentColor"/><text x="178" y="56" fill="currentColor">Wa</text>
<rect x="240" y="42" width="70" height="18" class="f-acc" stroke="currentColor"/><text x="248" y="56" fill="currentColor">Wb</text>
<rect x="310" y="42" width="70" height="18" class="f-acc" stroke="currentColor"/><text x="318" y="56" fill="currentColor">Wa</text>
<text x="390" y="56" fill="currentColor">… 20판 …</text>
<rect x="470" y="42" width="70" height="18" class="f-acc" stroke="currentColor"/><text x="478" y="56" fill="currentColor">Wa</text>
<rect x="540" y="42" width="90" height="18" class="f-ok" stroke="currentColor"/><text x="548" y="56" fill="currentColor">R1 R2</text>
<line x1="130" y1="80" x2="540" y2="80" stroke="currentColor" stroke-dasharray="5 4"/>
<text x="140" y="98" class="f-warn" fill="currentColor">R1, R2: writer 20판 내내 줄에 있음 = reader starvation</text>
<line x1="10" y1="118" x2="730" y2="118" stroke="currentColor" stroke-opacity="0.4"/>
<text x="10" y="156" fill="currentColor">phase-fair</text>
<rect x="110" y="142" width="60" height="18" class="f-ok" stroke="currentColor"/><text x="118" y="156" fill="currentColor">R0</text>
<rect x="170" y="142" width="70" height="18" class="f-acc" stroke="currentColor"/><text x="178" y="156" fill="currentColor">Wa</text>
<rect x="240" y="142" width="90" height="18" class="f-ok" stroke="currentColor"/><text x="248" y="156" fill="currentColor">R1 R2</text>
<rect x="330" y="142" width="70" height="18" class="f-acc" stroke="currentColor"/><text x="338" y="156" fill="currentColor">Wb</text>
<line x1="240" y1="184" x2="330" y2="184" stroke="currentColor"/>
<text x="110" y="202" fill="currentColor">Wa가 나가며: active_readers += 2, waiting_readers = 0, read_gen++, broadcast</text>
<text x="110" y="220" fill="currentColor">Wb는 깨우지 않는다 → 마지막 reader(R1 또는 R2)가 나가며 Wb를 깨운다</text>
<text x="110" y="240" fill="currentColor">로그 실측: "R0 Wa R1 R2 Wb"</text>
</svg>
```

```c
/* B1 — reader 입장 */
if (!rw->writer_active && rw->waiting_writers == 0) {
    rw->active_readers++;
} else {
    unsigned long gen = rw->read_gen;
    rw->waiting_readers++;
    while (rw->read_gen == gen)                  /* "내 묶음이 허락됐나" 만 본다 */
        pthread_cond_wait(&rw->readers_ok, &rw->m);
}
/* B4 — writer 퇴장 */
rw->writer_active = false;
if (rw->waiting_readers > 0) {                   /* reader 대기자를 writer 대기자보다 먼저 본다 */
    rw->active_readers += rw->waiting_readers;   /* 직접 넘김: 깨우는 쪽이 입장 처리까지 */
    rw->waiting_readers = 0;
    rw->read_gen++;
    pthread_cond_broadcast(&rw->readers_ok);
} else if (rw->waiting_writers > 0) {
    pthread_cond_signal(&rw->writer_ok);
}
```

**왜 상한이 생기나.** writer-pref처럼 reader는 writer가 기다리면 줄을 선다. 그래서 writer는 **지금 안에 있는 reader 묶음**만 기다리면 된다(writer 한 판 + reader 한 묶음). reader는 writer가 나갈 때 반드시 들어간다. writer 퇴장 코드가 reader 대기자를 writer 대기자보다 먼저 보기 때문이다. 그래서 reader는 writer 한 판만 기다린다. 이름과 상한의 모양은 Brandenburg–Anderson의 phase-fair rwlock(실시간 멀티코어 스핀 rwlock 연구)에서 빌렸다. 그 논문은 티켓 기반 스핀락이고, 여기는 mutex+cond로 만든 같은 정책이다(논문 세부는 미확인).

```text
== Part B: forced schedule 2 — writers arrive back-to-back, readers wait (K = 20) ==
  [writer-pref] writer phases R1/R2 waited=20 batch_together=0 next_writer_after_batch=0
                order: R0 Wa Wb Wa Wb Wa Wb Wa Wb Wa Wb Wa Wb Wa Wb Wa Wb Wa Wb Wa Wb Wa R1 R2
  [phase-fair ] writer phases R1/R2 waited=0 batch_together=1 next_writer_after_batch=1
                order: R0 Wa R1 R2 Wb
```

`batch_together=1`은 R2가 들어왔을 때 `active_readers == 2`, `waiting_writers == 1`, Wb가 아직 `A_BUSY`였다는 뜻이다. 즉 두 reader가 한 묶음으로 입장했고 Wb는 그 뒤로 밀렸다.

**정책별 대가** (아래 벤치마크와 같이 읽는다):

| | reader-pref | writer-pref | phase-fair |
|---|---|---|---|
| reader 처리량 | 가장 좋음: 겹치는 reader를 막지 않음 | writer가 기다리는 동안 새 reader가 멈춤 | writer-pref와 비슷 |
| writer 지연 상한 | **없음** | reader 한 묶음 | reader 한 묶음 + writer 한 판 |
| reader 지연 상한 | writer 한 판 | **없음** | writer 한 판 |
| 재귀 읽기 | 안전 | writer가 기다리면 자기 교착 | 자기 교착 |

### Part C — 설정 테이블 래퍼와 실제 pthread rwlock

**왜 래퍼가 필요한가.** 펌웨어 코드는 계층이 깊다. `ctrl_step()`이 설정을 읽는 동안 `filter_coeff()`라는 도우미를 부르고, 그 도우미도 설정을 읽는다. writer-pref나 phase-fair에서는 바깥 읽기와 안쪽 읽기 사이에 writer가 줄을 서면, 안쪽 `rdlock`이 그 writer 뒤에 서게 된다. writer는 바깥 읽기가 끝나기를 기다리므로 **자기 자신과의 교착**이 된다. 래퍼는 태스크별 깊이(`rdepth`)를 세어 안쪽 읽기에서는 rwlock에 손대지 않는다.

```c
/* C1 — cfg_read_lock */
tl_hold_t *h = tl_slot(&s->lock, true);
if (!h) return ENOMEM;
if (h->rdepth > 0 || h->wr) { h->rdepth++; return 0; }   /* 중첩: rwlock 은 건드리지 않음 */
rwl_rdlock(&s->lock);
h->rdepth = 1;
return 0;
/* C2 — cfg_write_lock */
tl_hold_t *h = tl_slot(&s->lock, false);
if (h && (h->rdepth > 0 || h->wr)) return EDEADLK;        /* 승격 · 재귀 쓰기: 기다리지 않고 거부 */
h = tl_slot(&s->lock, true);
if (!h) return ENOMEM;
rwl_wrlock(&s->lock);
h->wr = true;
return 0;
```

`test_cfg_nested`는 이 상황을 강제로 만든다. T가 읽는 중에 W를 줄 세우고(`waiting_writers == 1`), T가 다시 읽는다. 안쪽 읽기는 즉시 돌아와야 하고, rwlock 입장에서는 reader가 여전히 1명(`active_readers == 1`)이어야 한다. 바깥까지 다 풀면 W가 들어간다. `cfg_stress`는 7개 태스크가 중첩 읽기(`cfg_get_entry`가 안에서 또 잠금)를 6000번씩 하고, 1개 태스크가 300번 고친다.

```text
  [cfg writer-pref] 7 readers x 6000 (nested) + 1 writer x 300: done=42300 bad_sum=0 nonmono=0 err=0 version=300
  [cfg phase-fair ] 7 readers x 6000 (nested) + 1 writer x 300: done=42300 bad_sum=0 nonmono=0 err=0 version=300
```

**실제 rwlock은 어떻게 하나 — macOS** (`demo_pthread_rw.c`, 멈출 수 있는 실험은 fork한 자식에서 `alarm(1)`):

<!-- code: demo_pthread_rw.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_pthread_rw.c -o demo_rw && ./demo_rw
```

```text
1) R held by main, writer blocked in wrlock, same thread tryrdlock:
    tryrdlock while a writer is blocked: EBUSY
    releasing the read lock
    [writer] got the write lock
    => child exited 0 (did not hang)
1b) same, but tryrdlock from another thread:
    tryrdlock from ANOTHER thread while a writer is blocked: EBUSY
    [writer] got the write lock
    => child exited 0 (did not hang)
2) upgrade: holding R, call wrlock in the same thread:
    holding R; trywrlock -> EBUSY
    holding R; calling wrlock ...
    => child killed by signal 14 (SIGALRM) after 1 s: HUNG, no error returned
3) recursive read while a writer waits:
    holding R, writer blocked; calling rdlock again ...
    => child killed by signal 14 (SIGALRM) after 1 s: HUNG, no error returned
4) holding W, lock again:
    holding W; tryrdlock -> EDEADLK, trywrlock -> EDEADLK
    holding W; rdlock -> EDEADLK
    holding W; wrlock -> EDEADLK
    => child exited 0 (did not hang)
```

읽는 법:

- macOS `man pthread_rwlock_rdlock`의 IMPLEMENTATION NOTES에는 "To prevent writer starvation, writers are favored over readers"라고 적혀 있다. 1)과 1b)가 이것을 확인한다. writer가 기다리면 새 읽기는 `EBUSY`다. 즉 **writer 선호**다.
- 2) **R→W 승격은 에러 없이 영원히 멈춘다.** man 페이지에는 wrlock이 "may fail ... [EDEADLK] The calling thread already owns the read/write lock (for reading or writing)"라고 되어 있지만 읽기 보유는 추적하지 않는다. POSIX도 이 경우 "shall either deadlock or return [EDEADLK]"이므로 멈추는 것은 규격 위반이 아니다. STOR-M10이 B+tree 래치 승격에서 본 것과 같은 현상이다.
- 3) 재귀 읽기 + 대기 writer → 멈춘다. writer 선호의 대가이고, Part C 래퍼가 막는 상황이 정확히 이것이다.
- 4) W 보유 중 재잠금은 `EDEADLK`다. writer 소유자는 추적한다.

**리눅스 glibc** (Lima VM `elinux`: kernel 7.0.0-38-generic, gcc 15.2.0, GLIBC 2.43, `demo_glibc_rw.c`):

<!-- code: demo_glibc_rw.c -->

```bash
limactl shell elinux -- bash -lc 'mkdir -p /tmp/OS-M09 && cp <lesson dir>/demo_glibc_rw.c /tmp/OS-M09/ && cd /tmp/OS-M09 && gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_glibc_rw.c -o demo_glibc_rw && ./demo_glibc_rw'
```

```text
PTHREAD_RWLOCK_DEFAULT_NP (= 0):
    a) other-thread tryrdlock while writer blocked: 0 (ok)
    b) recursive rdlock while writer blocked: 0 (ok)
    c) holding R, wrlock: HUNG (child killed by SIGALRM after 1 s)
PTHREAD_RWLOCK_PREFER_READER_NP (= 0):
    …(DEFAULT 와 같음)
PTHREAD_RWLOCK_PREFER_WRITER_NP (= 1):
    a) other-thread tryrdlock while writer blocked: 0 (ok)
    b) recursive rdlock while writer blocked: 0 (ok)
    c) holding R, wrlock: HUNG (child killed by SIGALRM after 1 s)
PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP (= 2):
    a) other-thread tryrdlock while writer blocked: EBUSY
    b) recursive rdlock while writer blocked: HUNG (child killed by SIGALRM after 1 s)
    c) holding R, wrlock: HUNG (child killed by SIGALRM after 1 s)
```

glibc 기본값은 **reader 선호**다(DEFAULT = PREFER_READER = 0). 이름과 달리 `PREFER_WRITER_NP`도 이 실험에서는 reader 선호처럼 동작했다. 실제로 writer를 선호하는 것은 `PREFER_WRITER_NONRECURSIVE_NP`뿐이고, 그 대가로 재귀 읽기가 멈춘다. "NONRECURSIVE"라는 이름이 바로 이 대가를 가리킨다. 정리하면 같은 `pthread_rwlock_t`가 **macOS에서는 writer 선호, Linux 기본은 reader 선호**다. 이식할 코드에서 정책에 의존하면 안 되는 이유다. POSIX는 우선순위 스케줄링 옵션이 없을 때 "writer가 기다릴 때 reader를 들일지"를 구현 정의로 둔다.

### 벤치마크 — demo_bench.c

<!-- code: demo_bench.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_bench.c -o bench && ./bench 1000    # solution.c 를 include, 약 10 초
```

기계: Apple M6(`hw.perflevel0/1/2.physicalcpu` = 2 / 4 / 6), Apple clang version 21.0.0 (clang-2100.0.123.102). **`uptime` load average 18.70 → 22.96**(다른 세션의 빌드와 벤치가 동시에 돌던 공유 맥). 세 번 돌렸고, 아래는 두 번째 실행이다. 순위와 모양은 세 번 모두 같았다. **비율만 믿을 것.** 시간은 `clock_gettime_nsec_np(CLOCK_UPTIME_RAW)`로 쟀다(실측 눈금 42 ns). `CLOCK_MONOTONIC`은 이 맥에서 1 µs 눈금이어서 처음 버전은 p50이 0.0 / 15.0처럼 뭉개져 나왔다.

**W1 — 95/5 혼합, 8 스레드**(모든 스레드가 매 연산 5 % 확률로 쓰기, 임계 구역 = 16워드 검사합, 밖 = `spin_work(200)` ≈ 0.2 µs):

| lock | kops/s | writes/s | W p50 µs | W p99 µs | W max µs | R p99 µs |
|---|---|---|---|---|---|---|
| reader-pref | 1380 | 69105 | 0.08 | **309.0** | 12728.0 | 56.58 |
| writer-pref | 979 | 48904 | 8.96 | **51.4** | 1146.0 | 95.54 |
| phase-fair | 565 | 28089 | 18.00 | 186.0 | 5792.4 | 81.00 |
| pthread_rwlock (macOS) | 797 | 39776 | 14.42 | 113.9 | 8781.3 | 55.79 |
| mutex (기준선) | **3799** | 189933 | 0.04 | 26.2 | 916.2 | 26.04 |

**W2 — 겹침 강제**(reader 7개가 `spin_work(2000)` ≈ 2 µs짜리 임계 구역을 쉬지 않고 반복, writer 1개가 100 µs마다 쓰기):

| lock | kops/s | writes/s | W p50 µs | W p99 µs | W max µs | R p99 µs |
|---|---|---|---|---|---|---|
| reader-pref | 1216 | **1** | **1010044.83** | 1010044.8 | **1010044.8** | 11.46 |
| writer-pref | 709 | 3875 | 14.83 | 343.4 | 6038.4 | 70.25 |
| phase-fair | 783 | 4097 | 17.67 | 362.2 | 6517.8 | 110.04 |
| pthread_rwlock (macOS) | 865 | 4308 | 16.38 | 501.9 | 3443.8 | 168.88 |
| mutex | 184 | 2520 | 50.12 | 1860.0 | 12712.2 | 745.33 |

읽는 법:

1. **W2의 reader-pref writer는 1초 동안 1번 썼고, 그 한 번의 대기가 1.01초였다**(세 번 모두 1번, 1.002~1.010 s). reader가 끊임없이 겹쳐 `active_readers`가 0이 되지 않다가, 측정 끝에 reader들이 멈추자 그제야 들어간 것이다. 강제 스케줄 ①과 같은 현상을 실제 스케줄에서 본 것이다. 다른 세 rwlock은 writer가 초당 2700~4600번 썼다(세 실행 합쳐서)(100 µs 간격이므로 상한은 약 1만 번).
2. **W1에서는 mutex가 가장 빨랐다**(rwlock들의 2.7~6.7배). 임계 구역이 16워드뿐이라서, rwlock이 한 번에 mutex를 두 번 잡고 놓는 비용(입장·퇴장)과 cond 깨우기 비용이 병렬 읽기 이득보다 크다. OSTEP 31.5 끝의 Hill's Law 팁("simple and dumb can be better")이 숫자로 나온 것이다. rwlock은 **읽기 임계 구역이 길 때**(W2: mutex 184 vs rwlock 709~1216 kops/s) 이긴다.
3. **writer 꼬리 지연**: W1의 writer p99는 reader-pref 309 µs vs writer-pref 51 µs였다(세 번 각각 290~331 vs 50~52). phase-fair는 그 사이(100~186)였다. 양쪽 상한을 다 보장하는 대가로 처리량이 가장 낮았다(565~675 kops/s). 묶음 입장이 broadcast + 직접 넘김이라 깨어남이 몰리는 탓으로 보인다(추정).
4. max 값은 부하 18~24인 맥에서 스레드가 선점된 시간이 섞여 있어 잡음이 크다(W1 max는 실행마다 1~13 ms). 정책 비교에는 p99와 W2의 "1번"을 쓴다.

**TSan이 놓친 경로 — 실측.** 처음 스트레스 버전에서 reader는 테이블을 구조체 복사(`cfg_table_t snap = *tab;`)로 읽었다. reader가 writer를 무시하는 돌연변이(§7의 두 번째 줄)를 단위 테스트를 건너뛰고 스트레스까지 돌렸다. 불변식 카운터를 켠 빌드와 끈 빌드 모두 TSan 경고는 **0개**였고, 끈 빌드에서는 검사합 실패가 `bad_sum=764`(reader-pref)나 있었다. 그 복사는 `___tsan_memcpy` 호출로 컴파일됐다(`-S`로 확인). 필드를 제자리에서 하나씩 읽게 바꾸자(`___tsan_read4`) 같은 돌연변이에서 TSan이 `data race … Read of size 4 … Previous write of size 4`를 보고했다. 원인은 조사하지 않았다. LLAT-M02에도 같은 관찰이 있다. 그래서 정답 스트레스는 필드별로 읽는다. 교훈은 **TSan 0개 ≠ 레이스 없음**이고, 불변식 검사와 검사합을 같이 둔다는 것이다.

### 펌웨어 쪽 정리

- RTOS 커널은 대개 rwlock을 기본 제공하지 않는다(FreeRTOS 커널에는 rwlock 객체가 없는 것으로 알고 있다 — 기억 기반, 미확인). 필요하면 mutex + 카운터로 이 레슨처럼 만들거나, 아래 대안을 쓴다.
- 단일 코어 MCU에서 작은 테이블이라면 **인터럽트를 잠깐 막고 복사**(C-M14)나 **더블 버퍼 + 포인터 교체**가 rwlock보다 단순하고 빠르다. 포인터 교체 뒤 옛 버퍼를 언제 재사용하느냐가 RCU/EBR의 문제다(OS-A03). writer가 하나라면 **seqlock**(LLAT-M02)은 reader가 아무것도 쓰지 않아서 reader 확장성이 가장 좋다.
- rwlock을 쓴다면 writer 지연 상한이 필요한지 먼저 정한다. 설정 변경이 반드시 몇 ms 안에 반영돼야 하면 writer-pref나 phase-fair를 쓰고, 재귀 읽기는 래퍼로 막는다. ISR에서 rwlock을 잡는 것은 mutex와 같은 이유로 금지다(OS-M10 Part C, C-M14).
- 우선순위 역전도 남는다. 낮은 우선순위 reader 여럿이 쥔 rwlock을 높은 우선순위 writer가 기다리면, 누구의 우선순위를 올려야 하는지가 하나로 정해지지 않는다. reader가 여러 명이라 우선순위 상속이 mutex보다 어렵다(RTOS-M04 참고, 세부는 이 레슨 범위 밖).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution             # 21/21 passed (5회 반복 + 다른 cwd), ~0.4 초
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.c -o so_san && ./so_san    # 21/21, 보고 0
cc -std=c11 -g -O1 -fsanitize=thread -pthread solution.c -o so_tsan && ./so_tsan                                     # 21/21, 경고 0, ~4.3 초
```

시작 코드와 다른 줄은 TODO 열한 군데(A1–A4, B1–B4, C1–C3)의 몸체뿐이다. 시작 코드를 ASan/UBSan으로 돌려도 보고는 0개였다(2/21, exit 1).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| writer 퇴장 때 reader를 `signal`로 하나만 깨움 | 나머지 reader는 다음 writer 퇴장까지 잔다. 실측 돌연변이: 강제 스케줄 ②에서 R2가 영원히 안 깨어나 `FAIL watchdog: 'reader_starve_schedule: stopping actors' did not finish in 8000 ms` (9.2초 후 exit 1) | `pthread_cond_broadcast(&rw->readers_ok)` |
| reader 조건에서 `writer_active`를 빠뜨림 | writer가 쓰는 중에 reader가 들어간다. 실측 돌연변이(단위 테스트 우회): `violations=4704 bad_sum=1375`(reader-pref), TSan `data race` 3건. 단위 테스트는 `basic_sched=0`으로 먼저 잡았다 | `while (rw->writer_active \|\| …)` |
| 테이블을 구조체 복사로 읽고 TSan만 믿음 | 이 맥에서 `___tsan_memcpy` 경로 레이스는 경고 0이었다(위 실측, `bad_sum=764`) | 불변식 카운터 + 검사합을 함께, TSan용으로는 필드별 접근도 |
| `if (…) cond_wait` | spurious wakeup이나 barging 뒤에 조건이 다시 거짓일 수 있다(OS-B06) | 항상 `while` |
| `waiting_*`를 실제로 잠들 때만 올림(조건 안쪽에서) | 의미는 같지만 깨우는 쪽이 "누가 기다리나"를 잘못 볼 창이 생기기 쉽다 | 루프 앞에서 `++`, 입장할 때 `--`(같은 mutex 아래) |
| phase-fair에서 깨운 뒤 reader가 조건을 다시 봄 | 그사이 다음 writer가 먼저 잡으면 "묶음" 보장이 깨진다 = writer-pref로 돌아감 | 깨우는 writer가 `active_readers += waiting_readers`(직접 넘김), reader는 `read_gen`만 확인 |
| 시간 제한 writer가 포기하면서 아무도 안 깨움 | writer-pref reader는 "writer가 기다린다"는 이유로 잠들어 있었다. 실측(연습 2 돌연변이): `queued reader admitted=0` FAIL | 포기할 때 `waiting_writers == 0`이면 `broadcast(readers_ok)` |
| R을 쥔 채 `wrlock`으로 승격 | 다른 reader도 같은 시도를 하면 서로 상대 퇴장을 기다린다. 혼자여도 자기 R 때문에 못 들어간다. macOS·glibc 모두 **멈춤**(실측) | R을 놓고 W를 새로 잡은 뒤 **조건을 다시 확인**하거나, 처음부터 W로 잡기. 래퍼는 `EDEADLK` |
| writer-pref에서 같은 태스크의 재귀 `rdlock` | writer가 줄을 선 순간 자기 교착(macOS 실측 HUNG) | 태스크별 깊이로 안쪽은 rwlock을 건너뜀(Part C) |
| 읽기 임계 구역이 짧은데 rwlock | 입장·퇴장 비용이 병렬 이득보다 크다. W1 실측 mutex 3799 vs rwlock 565~1380 kops/s | mutex 먼저, 재 보고 바꾼다(Hill's Law). 아니면 seqlock/RCU |

## 8. 면접 Q&A

<details><summary>Q1. reader-preferring과 writer-preferring의 차이를 코드 수준에서 설명해 보세요.</summary>

차이는 두 곳이다. ① reader 입장 조건: reader-pref는 `writer_active`일 때만 기다리고, writer-pref는 `writer_active || waiting_writers > 0`일 때 기다린다. ② writer 퇴장 때 깨우는 순서: reader-pref는 reader 전원 먼저, writer-pref는 writer 하나 먼저. writer 입장 조건(`!writer_active && active_readers == 0`)과 reader 퇴장(마지막이면 writer 하나 깨움)은 같다. 꼬리질문: "reader-pref의 장점은?" → 읽기가 겹치는 한 reader를 절대 막지 않으므로 reader 처리량과 지연이 가장 좋고, 재귀 읽기가 안전하다(glibc 기본값이 이것이다. VM 실측). 단점은 writer 지연 상한이 없다는 것이다(W2 실측 1초에 1번).
</details>

<details><summary>Q2. writer starvation을 테스트로 어떻게 재현하나요? sleep을 넣어서요?</summary>

sleep으로는 "대개" 재현될 뿐이고 CI에서 흔들린다. 스케줄을 강제했다. reader 둘을 hand-over-hand로 교대시킨다. 새 reader가 **들어온 것을 확인한 뒤에** 옛 reader를 내보내므로 `active_readers`가 0이 되는 순간이 없다. writer는 `waiting_writers == 1`(= cond_wait 안에서 잠듦)을 확인한 뒤에 시작한다. 판정은 "K = 20번 교대 동안 writer 상태가 `A_IN_W`가 아니었다"이고, writer-pref에서는 "첫 새 reader가 `waiting_readers == 1`로 줄을 섰고 옛 reader가 나가자 writer가 들어갔다"이다. 어떤 스케줄링 순서에서도 같은 결과가 나온다. 꼬리질문: "틀린 구현이면 테스트가 멈추지 않나요?" → 모든 대기는 최대 1초이고, 그래도 막히면 워치독이 8초에 FAIL을 찍고 `_exit(1)` 한다(잃어버린 깨움 돌연변이로 실측).
</details>

<details><summary>Q3. writer-pref면 다 해결인가요? phase-fair는 무엇을 더 보장하나요?</summary>

writer-pref는 반대로 reader를 굶긴다. 강제 스케줄 ②에서 writer가 줄줄이 오면 reader가 writer 20판을 내리 기다렸다(실측). phase-fair는 writer가 나갈 때 기다리던 reader 전원을 한 묶음으로 직접 입장시킨다. 그래서 reader는 writer 한 판, writer는 reader 한 묶음 + writer 한 판 이상 기다리지 않는다. 대가로 처리량이 가장 낮았다(W1 565~675 kops/s). 꼬리질문: "왜 깨우기만 하지 않고 카운터를 미리 올려 주나요?" → 깨어난 reader가 mutex를 다시 잡기 전에 다음 writer가 끼어들면(barging) 묶음이 깨진다. 직접 넘김은 FIFO 핸드오프 락과 같은 생각이고, 공정성을 처리량과 바꾼다.
</details>

<details><summary>Q4. macOS pthread_rwlock은 어떤 정책인가요? 리눅스는요?</summary>

macOS man 페이지에는 "writers are favored over readers"라고 되어 있고, 실측에서 writer가 기다리면 다른 스레드의 `tryrdlock`이 `EBUSY`였다. glibc(2.43, VM 실측) 기본값은 reader 선호다. `PTHREAD_RWLOCK_PREFER_WRITER_NP`도 실측에서는 reader 선호처럼 동작했고, `PREFER_WRITER_NONRECURSIVE_NP`만 writer를 선호했다(그 대신 재귀 읽기가 멈춤). POSIX는 이 선택을 구현에 맡긴다(우선순위 스케줄링 옵션이 없을 때 구현 정의). 그래서 이식할 코드는 정책에 기대지 말고, 필요하면 직접 만든 락이나 다른 구조를 쓴다. 꼬리질문: "_NP는?" → non-portable, glibc 확장이다.
</details>

<details><summary>Q5. 읽기 락을 쥔 채 쓰기 락으로 올리면(upgrade) 어떻게 되나요?</summary>

POSIX `pthread_rwlock_wrlock`은 호출 스레드가 이미 읽기나 쓰기로 쥐고 있으면 "shall either deadlock or return [EDEADLK]"이다. macOS와 glibc 모두 실측에서 **에러 없이 멈췄다**. 근본 문제는 두 reader가 동시에 승격하려 하면 서로 상대가 R을 놓기를 기다린다는 것이다. 그래서 일반 rwlock은 원자적 승격을 줄 수 없다. 해법은 세 가지다. ① R을 놓고 W를 잡은 뒤 조건을 다시 확인한다(그사이 바뀌었을 수 있음). ② 처음부터 W로 잡는다. ③ "업그레이드 가능 읽기"(한 번에 한 명만 가질 수 있는 U 모드, 예: Boost `upgrade_mutex` — 이름만 언급)를 쓴다. 래퍼는 이 상황을 기다리지 않고 `EDEADLK`로 바꿔 버그를 바로 드러낸다. 반대 방향인 W→R 다운그레이드는 안전하다(연습 3).
</details>

<details><summary>Q6. rwlock이 mutex보다 느린 경우는? 측정해 보셨나요?</summary>

읽기 임계 구역이 짧을 때다. 95/5, 16워드 테이블에서 mutex가 3799 kops/s, rwlock들이 565~1380 kops/s였다(공유 맥, load 18~24, 비율만). rwlock은 입장과 퇴장 각각 내부 mutex를 잡고 놓는다. 또 reader 수 카운터가 모든 reader가 쓰는 캐시 라인이라 코어 사이를 오간다. 그래서 "읽기는 병렬"이라는 이득이 임계 구역이 길 때만 남는다(W2: 2 µs 구역에서는 rwlock들이 mutex의 약 3~10배). 꼬리질문: "reader가 정말 많고 짧으면?" → reader가 공유 상태를 쓰지 않는 구조로 간다. seqlock(LLAT-M02: 같은 맥에서 reader 4개 기준 seqlock 704 vs rwlock 9.1 Mreads/s), RCU/EBR(OS-A03), per-CPU 카운터를 쓰는 rwsem(Linux `percpu_rw_semaphore`, 이름만 언급)이다.
</details>

<details><summary>Q7. 왜 cond 변수를 두 개 쓰나요? 하나로 broadcast하면 안 되나요?</summary>

하나로도 정확하게는 만들 수 있다. 바뀔 때마다 모두 깨우고 각자 조건을 다시 보면 된다. 하지만 writer 하나만 깨우면 되는 순간에도 잠든 reader 전원이 깨어나 mutex를 두고 다투다가 다시 잔다(thundering herd). OS-B06의 "조건마다 cond 하나" 규칙과 같다. 두 개로 나누면 writer는 `signal`, reader는 `broadcast`로 필요한 만큼만 깨운다. 꼬리질문: "signal을 broadcast로 바꾸면 틀리나요?" → 틀리지는 않고 느려진다. 반대로 reader 쪽 broadcast를 signal로 바꾸면 **틀린다**(§7 실측: 워치독).
</details>

<details><summary>Q8. 펌웨어의 설정 테이블이라면 무엇을 쓰겠습니까?</summary>

먼저 크기와 코어 수를 묻는다. 단일 코어 MCU에 수십 바이트라면 인터럽트를 잠깐 막고 복사한다(C-M14). 이것이 가장 단순하고 결정적이다. 테이블이 크고 writer가 하나라면 더블 버퍼 + 버전 번호(seqlock, LLAT-M02)나 포인터 교체 + 유예 기간(RCU, OS-A03)을 쓴다. reader가 아무것도 쓰지 않으므로 제어 루프 지연이 흔들리지 않는다. 태스크가 많고 읽기 구간이 길어 rwlock을 쓴다면, 설정 변경 반영 시간 요구가 있으니 writer-pref나 phase-fair를 고른다. 재귀 읽기와 승격은 래퍼로 막고(Part C), ISR에서는 쓰지 않는다. 꼬리질문: "rwlock에서 우선순위 역전은?" → reader가 여럿이라 상속 대상을 하나로 정할 수 없어 mutex보다 어렵다. 그래서 RTOS에서는 rwlock 대신 위 구조를 더 자주 쓴다(일반론).
</details>

## 9. 추가 연습

### 연습 1 — `rwl_tryrdlock` / `rwl_trywrlock`

정책을 따르는 try를 만든다. writer-pref라면 writer가 **기다리기만** 해도 `tryrdlock`은 `EBUSY`여야 한다(macOS `pthread_rwlock_tryrdlock`의 man 페이지 설명과 같은 규칙). main이 R을 쥐고 writer 스레드를 줄 세운 뒤, 다른 스레드에서 `tryrdlock`을 해서 확인한다.

<details><summary>답</summary>

입장 조건 함수 `reader_blocked(rw)`를 rdlock과 공유하면 try는 "조건이 참이면 EBUSY, 아니면 그대로 입장"이 된다. 실측: reader-pref `0`, writer-pref `EBUSY`. try는 기다리지 않으므로 `waiting_*`를 건드리지 않는다. 주의할 점이 있다. try가 성공해도 정책을 어기면 안 된다. writer-pref에서 try로 새치기를 허용하면 그 경로로 writer가 굶는다.
</details>

### 연습 2 — `rwl_wrlock_timed`: 포기하는 writer

`pthread_cond_timedwait`(CLOCK_REALTIME 절대 시각)로 시간 제한 writer를 만든다. writer-pref에서 main이 R을 쥐고, writer가 100 ms 제한으로 줄을 서고, 그 뒤에 reader가 줄을 선다. writer가 `ETIMEDOUT`으로 포기하면 **그 reader가 들어와야** 한다.

<details><summary>답</summary>

포기할 때 `waiting_writers--`를 하고, 그 결과 `waiting_writers == 0 && !writer_active`이면 `broadcast(readers_ok)`를 한다. reader는 "writer가 기다린다"는 이유만으로 잠들어 있었는데 그 이유가 사라졌다. 이 broadcast를 빼면(실측 돌연변이) `queued reader admitted=0`로 FAIL이고, reader는 다음 writer가 나갈 때까지 잔다. 일반 규칙: **대기 조건에 쓰이는 값을 바꾸는 모든 경로가 깨울 책임을 진다.** 타임아웃·취소 경로가 가장 자주 빠진다.
</details>

### 연습 3 — `rwl_downgrade`: W를 쥔 채 R로

쓰기를 마치고 결과를 계속 읽어야 할 때, 놓았다가 다시 R을 잡으면 그사이 다른 writer가 끼어든다. 원자적으로 내려가는 함수를 만든다. W 보유 중에 reader 하나와 writer 하나가 줄을 선 상태에서 다운그레이드한다.

<details><summary>답</summary>

mutex 아래에서 `writer_active = false; active_readers++`를 **한 번에** 한다. 이 순간부터 `active_readers > 0`이므로 다른 writer는 못 들어간다. 그다음 정책대로 reader를 깨운다. 실측: reader-pref는 줄 선 reader도 같이 입장(`active_readers=2 waiting_writers=1`), writer-pref는 줄 선 writer가 우선이라 reader가 계속 대기(`active_readers=1 waiting_writers=1 waiting_readers=1`). Linux 커널 rw_semaphore의 `downgrade_write()`가 같은 생각이다(이름만 확인). 업그레이드와 달리 다운그레이드는 교착 위험이 없다.
</details>

### 연습 4 — OSTEP 그림 31.13: 세마포어 rwlock은 reader 선호

OSTEP의 `lock`·`writelock` 두 세마포어 버전을 옮긴다(맥에는 `sem_init`이 없어 mutex+cond 세마포어 `csem_t`로 만든다, OS-B06). 그리고 writer가 `writelock`에서 기다리는 동안(`csem_waiters == 1`) 새 reader가 들어오는지 확인한다.

<details><summary>답</summary>

첫 reader가 `writelock`을 잡고 마지막 reader가 놓는다. 그사이 새 reader는 `readers++`만 하므로 writer를 신경 쓰지 않는다. 즉 reader 선호이고, OSTEP이 "writer가 굶을 수 있다"고 하는 이유가 이것이다. 실측: `writer queued=1, new reader admitted while writer waits=1, readers=2`, R1이 나가도 writer는 계속 대기, R2까지 나가야 writer가 들어갔다. 주의: `writelock`을 **잡은 스레드와 놓는 스레드가 다를 수 있다**(첫 reader ≠ 마지막 reader). 세마포어에는 소유자가 없으니 가능하다. 같은 자리에 mutex를 쓰면 POSIX 기본 mutex에서는 미정의 동작이다. OSTEP 숙제 `threads-sema/reader-writer-nostarve.c`가 이 버전의 굶주림을 고치는 과제다(본문의 writer-pref/phase-fair가 그 답의 한 형태).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread practice.c -o pr_san && ./pr_san
cc -std=c11 -g -O1 -fsanitize=thread -pthread practice.c -o pr_tsan && ./pr_tsan
```

실제 출력: `22/22 passed`(세 빌드 모두, ASan/UBSan 보고 0, TSan 경고 0, 5회 반복 + 다른 cwd, 약 0.26초).

## 10. 핵심 정리

- rwlock = mutex + cond 2개(`readers_ok`, `writer_ok`) + `active_readers`, `writer_active`, `waiting_readers`, `waiting_writers`. 불변식은 `writer_active ⇒ active_readers == 0`, writer ≤ 1.
- reader-pref와 writer-pref의 차이는 두 줄이다. reader가 "기다리는 writer"에도 막히는가, writer 퇴장 때 누구를 먼저 깨우는가. writer는 `signal`, reader는 `broadcast`.
- reader-pref는 writer가 굶는다(강제 스케줄 20교대 실측, W2 실측 1초에 1번 쓰기·대기 1.01 s). writer-pref는 reader가 굶는다(writer 20판 실측). phase-fair는 reader 묶음을 직접 넘겨 양쪽 상한을 보장하지만 처리량이 가장 낮았다.
- 테스트는 강제 스케줄 + 카운터 판정 + 워치독으로 결정적으로 만든다. 불변식은 임계 구역 안에서 seq_cst 카운터로 검사한다. TSan 0개 ≠ 레이스 없음(이 맥에서 memcpy 경로 놓침, 실측).
- macOS `pthread_rwlock`은 writer 선호, glibc 기본은 reader 선호다(`PREFER_WRITER_NONRECURSIVE_NP`만 writer 선호). R→W 승격은 둘 다 멈췄다. writer 선호에서는 재귀 읽기도 멈춘다.
- 짧은 임계 구역이면 mutex가 rwlock보다 빨랐다(3799 vs 565~1380 kops/s). 먼저 재 보고 고른다.
- 펌웨어 설정 테이블: 작으면 인터럽트 금지 복사, writer 하나면 seqlock(LLAT-M02)이나 RCU(OS-A03). rwlock이면 writer-pref/phase-fair + 중첩 읽기·승격 래퍼, ISR 금지.

## 참고자료

- OSTEP 31.5 *Reader-Writer Locks*(그림 31.13, Hill's Law 팁), 30장 *Condition Variables*: <https://pages.cs.wisc.edu/~remzi/OSTEP/threads-sema.pdf> · <https://pages.cs.wisc.edu/~remzi/OSTEP/threads-cv.pdf> (장·절 번호는 로컬 추출본 `book-md/C31_semaphores.md`로 확인). 숙제 `ostep-homework/threads-sema/reader-writer.c`, `reader-writer-nostarve.c`
- P. J. Courtois, F. Heymans, D. L. Parnas, "Concurrent Control with Readers and Writers", *CACM* 14(10), 1971 — reader 우선(1번)·writer 우선(2번) 문제의 원전 (OSTEP 참고문헌 [CHP71]에서 서지 확인, 1·2번 구분은 기억 기반)
- B. B. Brandenburg, J. H. Anderson, phase-fair reader-writer locks (실시간 멀티프로세서 스핀 rwlock, 2009년 전후) — 이름·상한 개념 출처, 서지 세부 미확인
- POSIX.1-2017 `pthread_rwlock_rdlock`(writer가 기다릴 때 입장 여부는 구현 정의, 쓰기 보유 중이면 교착 가능), `pthread_rwlock_wrlock`("shall either deadlock or return [EDEADLK]"): <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_rwlock_rdlock.html> · <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_rwlock_wrlock.html>
- macOS `man pthread_rwlock_rdlock`, `man pthread_rwlock_wrlock` (이 맥에서 읽음: "writers are favored over readers", EDEADLK 항목)
- glibc `pthread_rwlockattr_setkind_np(3)` — `PTHREAD_RWLOCK_PREFER_READER_NP` / `_PREFER_WRITER_NP` / `_PREFER_WRITER_NONRECURSIVE_NP`: <https://man7.org/linux/man-pages/man3/pthread_rwlockattr_setkind_np.3.html> (동작은 VM의 GLIBC 2.43에서 실측)
- 관련 레슨: OS-B05 (mutex), OS-B06 (조건 변수 · 맥 `sem_init` 불가), OS-M07 (스핀락 공정성), OS-M08 (atomics), OS-M10 (교착 · 강제 스케줄), OS-M11 (lockdep), OS-A03 (EBR/RCU), LLAT-M02 (seqlock · TSan memcpy 관찰), STOR-M10 (래치 R→W 승격 교착), C-M14 (ISR vs main), RTOS-M04 (우선순위 상속), ARCH-A05 (SB 리트머스)
