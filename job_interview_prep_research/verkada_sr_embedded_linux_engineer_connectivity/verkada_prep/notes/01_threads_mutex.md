# 🧵 스레드 & 뮤텍스 기초 (Threads & Mutex Fundamentals) — Q1~Q10

> 리크루터 메일의 1번 항목이 "thread safety in multi-threaded environments"다. 이 세트는
> 그 바닥을 깐다 — race를 **눈으로 보고**, mutex로 **불변식**을 지키고, 임계구역을 **짧게**
> 유지하고, 중첩 락·TOCTOU를 피하고, 마지막에 **깔끔하게 종료**하는 것까지. 뒤의
> condvar/atomic/더블버퍼 세트는 전부 여기 위에 얹힌다.

---

## 1. 핵심 아이디어

**Race condition은 "두 스레드가 동시에 접근"해서 생기는 게 아니다.**
정확히는 ① 둘 이상이 같은 메모리에 접근하고 ② 그중 하나 이상이 쓰기이며
③ 순서를 강제하는 동기화(happens-before)가 없을 때 생긴다. **셋 중 하나만 없애도 된다** —
쓰기를 없애거나(불변 데이터), 공유를 없애거나(스레드 로컬 후 병합 = Q8), 순서를
강제하거나(mutex/atomic/once/join = 나머지 전부).

`counter++` 는 원자적이지 않다. 세 단계다:

```
ldr x0,[counter]  /  add x0,x0,#1  /  str x0,[counter]     ; load → modify → store
```

두 스레드가 같은 값을 load하면 한쪽 증가가 통째로 사라진다(**lost update**).
Q1을 실제로 돌리면 80,000번 증가시켰는데 카운터는 4만대가 나온다.

> **`volatile`은 해결책이 아니다.** volatile은 "컴파일러가 이 접근을 최적화로
> 지우거나 합치지 말라"는 뜻일 뿐, **원자성도 메모리 순서도 보장하지 않는다.**
> MMIO 레지스터용이지 스레드 동기화용이 아니다. Q1이 volatile을 쓰고도 깨지는 게 증거다.

**mutex의 계약**: `lock`은 acquire, `unlock`은 release다. 임계구역은 불변식이 깨져 있어도
되는 유일한 구간이고, mutex는 상호배제뿐 아니라 **메모리 가시성(memory visibility)** 도
준다 — lock/unlock 쌍이 happens-before를 만들어 캐시·재정렬 문제까지 해결한다.
"mutex를 썼는데 값이 안 보인다"는 없다. 안 보이면 락을 안 잡은 경로가 있는 것이다.

---

## 2. 주제별 본문

### 2.1 불변식(invariant)을 단위로 잠가라 — Q3

락은 "변수"가 아니라 **불변식**을 보호한다. 링크 통계 `{count, sum, min, max}`는
네 필드가 **같은 순간의 값**이어야 의미가 있다.

```c
lock(&s->m);                                       // 네 필드를 한 임계구역에서 함께 갱신
s->count++;  s->sum_ms += rtt_ms;
if (rtt_ms < s->min_ms) s->min_ms = rtt_ms;
if (rtt_ms > s->max_ms) s->max_ms = rtt_ms;
unlock(&s->m);
```

필드마다 락을 따로 잡거나 snapshot을 나눠 읽으면 `count`는 새 값, `min`은 옛 값인
**찢어진(torn) 상태**가 나온다. 밖으로 들고 나갈 땐 한 번의 락에서 구조체를 통째로
복사한다(`ls_snapshot`). 필드별 getter 3개를 이어 부르거나 구조체 포인터를 락 밖으로
반환하면 같은 버그고, 필드별 mutex는 데드락까지 덤으로 준다.

### 2.2 임계구역은 짧게 — 락 안에서 I/O도 콜백도 금지 — Q4

```c
lock(&s->m);  update_state(s);  modem_write(s->last);  unlock(&s->m);  // ✗ 락 안에서 I/O

lock(&s->m);  update_state(s);  memcpy(copy, s->last, n);  unlock(&s->m);  // ✓ 복사만
modem_write(copy);                                          // 느린 일은 락 밖에서
```

락 안에서 **사용자 콜백**을 부르면 그 콜백이 같은 락을 다시 잡아 데드락이 난다. 임베디드는
여기에 **우선순위 역전(priority inversion)** 이 겹친다 — 낮은 우선순위 스레드가 락을 쥔 채
선점되면 높은 우선순위 스레드가 무한정 막힌다. 규칙: "임계구역 안에서는 상태 변경만".

### 2.3 `_locked` 접미사 규약 — 재귀 뮤텍스를 쓰지 않는 이유 — Q5

```c
static void poe_set_port_locked(PoeTable*, int i, int mw);  // 락 없음 — 호출자가 쥐고 있음
bool poe_set_port(PoeTable *t, int i, int mw)               // 공개 API 만 락을 잡는다
{ lock(&t->m);  poe_set_port_locked(t, i, mw);  unlock(&t->m); }

bool poe_set_both(PoeTable *t, int a, int b) {              // 락 1번으로 복합 연산
    lock(&t->m);
    poe_set_port_locked(t, 0, a);   // ← poe_set_port() 를 부르면 중첩 락!
    poe_set_port_locked(t, 1, b);
    unlock(&t->m);
}
```

**"재귀 뮤텍스 쓰면 되잖아요?"에 대한 답 3가지:** ① 재진입을 허용하면 **불변식이 깨진
중간 상태**에서 함수가 불릴 수 있고 "지금 일관된 상태인가?"라는 질문이 코드에서 사라진다.
② **condvar와 못 섞인다** — `pthread_cond_wait`은 락을 한 번만 놓으므로 재귀 깊이 2에서
wait하면 락을 쥔 채 잠든다. ③ 재귀가 필요하다는 건 보통 **락 경계를 잘못 그었다**는 신호다.

진단 팁: 디버그 빌드에서 `PTHREAD_MUTEX_ERRORCHECK`를 쓰면 중첩 락이 조용히 멈추는 대신
**`EDEADLK`를 반환**한다(Q5 테스트가 확인한다). 릴리스는 기본 mutex로.

### 2.4 TOCTOU — 검사와 행동은 같은 임계구역에 — Q6

```c
if (budget_has_room(b, mw))   // ✗ 락 잡고 확인 → 놓음
    budget_add(b, mw);        //   다시 락 잡고 증가 ← 그 사이 남이 다 써 버렸다

lock(&b->m);                                                          // ✓ check + act가
if (b->used_mw + mw <= b->total_mw) { b->used_mw += mw; ok = true; }  //   한 임계구역 안
unlock(&b->m);
```

지독한 이유: 각 연산이 **개별적으로는 완벽히 thread-safe**라서 TSan도 코드 리뷰도 못 잡는다.
60W 예산에 8개 스레드가 15W씩 요청하면 올바른 구현은 정확히 4개만 성공하지만, TOCTOU
버전은 최대 8개가 성공해 **120W를 배정**한다 — PoE면 실제로 전원이 죽는다.
일반화하면 **"thread-safe한 함수들을 조합해도 thread-safe하지 않다"**. 원자성이 필요한
단위가 함수 하나보다 크면, 그 단위를 감싸는 API를 만들어야 한다.

### 2.5 한 번만 초기화 — `pthread_once` — Q7

```c
static pthread_once_t once = PTHREAD_ONCE_INIT;
ModemHandle *modem_get(void) { pthread_once(&once, modem_init_once); return &g_modem; }
```

`if (!inited) { init(); inited = 1; }` 는 두 스레드가 동시에 `!inited`를 통과한다(포트를 두 번
열면 두 번째는 실패). **double-checked locking**을 직접 짜려면 `inited`를 atomic(acquire/
release)으로 둬야 하는데 `pthread_once`가 그걸 정확히 해 준다 — 초기화는 1회, 늦게 온
스레드는 끝날 때까지 **대기**하고 완성된 상태를 본다.

### 2.6 경합 줄이기 — 샤딩 후 병합 — Q8

```c
long local = 0;
for (long i = 0; i < iters; ++i) local++;                    // 공유 접근 0회
lock(&g_m);  g_total += local;  g_locks++;  unlock(&g_m);    // 스레드당 락 1회
```

Q2는 락을 `N*iters`번, Q8은 `N`번 잡는다(테스트가 실제로 락 횟수를 센다). 이득은 락 비용보다
**캐시 라인 핑퐁**에서 온다 — 여러 코어가 같은 64B 라인을 번갈아 독점하면 매 증가마다 코어
간 전송이 일어난다. 중간값이 필요 없는 집계(통계, drop 수)는 거의 항상 샤딩이 맞다.

### 2.7 rwlock — 언제 쓰고 언제 안 쓰나 — Q9

| 쓴다 | 쓰지 않는다 |
|---|---|
| 읽기:쓰기가 100:1 이상 | 임계구역이 몇 ns — rwlock 오버헤드가 더 크다 |
| 임계구역이 충분히 길다(문자열 복사 등) | 쓰기가 잦다 → writer starvation |
| 설정/라우팅 테이블처럼 거의 안 바뀌는 상태 | 값 하나면 `_Atomic`, 또는 RCU·더블버퍼·메시지 전달이 더 낫다 |

POSIX는 rwlock의 공정성을 **규정하지 않는다**. glibc 기본은 reader 우선이라 reader가
끊이지 않으면 writer가 굶는다(`..._PREFER_WRITER_NONRECURSIVE_NP`는 **Linux 전용** 확장 —
macOS엔 없다. 면접에서 말은 하되 이식 코드엔 쓰지 말 것). 그리고 rwlock은
**업그레이드(rdlock→wrlock)가 불가능**하다 — 시도하면 데드락이다.

### 2.8 깔끔한 종료 — stop 플래그 + join — Q10

```c
lock(&w->m);
bool need_join = w->running;  w->stop = true;  w->running = false;
unlock(&w->m);                        // ★ 락을 푼 뒤에 join (락 쥔 채 join = 데드락)
if (need_join) pthread_join(w->th, NULL);
```

`pthread_cancel`은 쓰지 않는다: 취소 지점이 불분명하고, 잡고 있던 락이 그대로 남고, fd가
샌다. 임베디드는 항상 **협조적 종료(cooperative shutdown)**. 깨우는 방법은 스레드가
무엇에 막혀 있느냐에 따라 다르다:

| 막혀 있는 곳 | 깨우는 법 |
|---|---|
| 계산 루프 | stop 플래그 폴링 (Q10) |
| `pthread_cond_wait` | 플래그 세운 뒤 **`broadcast`** (signal 아님 — 대기자가 여럿일 수 있다) |
| `poll`/`read`/`accept` (소켓·UART) | **self-pipe**: `pipe()` fd를 poll 집합에 넣고 한 바이트 쓴다 (또는 짧은 타임아웃 후 플래그 확인) |

> `eventfd`/`signalfd`/`epoll`은 **Linux 전용**이다. 실제 Verkada 게이트웨이는 Linux라
> `eventfd`가 정답이지만, 이 연습 코드는 macOS에서 도는 이식 가능한 `pipe()+poll()`로 쓴다.

---

## 3. 흔한 함정 (인터뷰 감점 포인트)

1. **`volatile`로 동기화하려 한다.** 즉시 탈락급. "원자성도 순서도 보장하지 않는다"고 말하라.
2. **읽기를 보호하지 않는다.** 락 없는 `sc_get()`은 stale/torn 값을 본다. 64비트 값을
   32비트 MCU에서 읽으면 진짜로 반쪽만 읽힌다.
3. **에러 경로에서 unlock을 빠뜨린다.** `if (err) return -1;`이 락을 쥔 채 나간다
   → 단일 exit point(`goto out;`) 또는 락 구간을 함수로 분리.
4. **락 안에서 blocking I/O / malloc / 콜백**(2.2), **재귀 뮤텍스로 중첩 락을 "해결"**(2.3),
   **thread-safe 함수를 조합하면 안전할 거라 착각**(TOCTOU, 2.4).
5. **락 순서를 정하지 않는다.** 락이 둘 이상이면 전역 순서(ID/주소 순) 고정 또는 `trylock`+백오프.
6. **detach만 하고 join하지 않는다**(스레드가 아직 쓰는 버퍼를 main이 free한다), **잠긴
   mutex를 destroy한다**(UB), **초기화 안 된 mutex를 쓴다**. 종료 순서: stop → join → destroy.

---

## 4. 면접에서 말할 것 (한국어 + 영어)

**① 락의 대상은 변수가 아니라 불변식**
- KO: "이 mutex가 무엇을 보호하는지 주석으로 적습니다 — 변수 하나가 아니라 count/sum/min/max가 같은 순간의 값이라는 불변식입니다."
- EN: *"I document what each mutex protects — not a variable, but an invariant: that count, sum, min and max all describe the same instant."*

**② 임계구역 최소화**
- KO: "락 안에서는 상태 변경만 하고, 느린 I/O나 콜백은 반드시 락을 놓고 호출합니다. 락을 쥔 채 모뎀에 쓰면 모뎀이 느려질 때 파이프라인 전체가 멈춥니다."
- EN: *"Inside the lock I only mutate state and copy out what I need. Blocking I/O and user callbacks always happen outside — otherwise a slow modem stalls every thread, and a callback that re-enters the lock deadlocks."*

**③ TOCTOU**
- KO: "검사와 행동은 같은 임계구역 안에 있어야 합니다. 각 함수가 thread-safe해도 둘을 이어 붙이면 thread-safe하지 않습니다 — PoE 예산이면 60W 한도에 120W를 배정하게 됩니다."
- EN: *"Check and act must be in the same critical section. Composing two thread-safe calls does not give you a thread-safe operation — with a PoE budget that means handing out 120 watts from a 60-watt supply."*

**④ 재귀 뮤텍스를 피하는 이유**
- KO: "재귀 뮤텍스 대신 `_locked` 접미사 규약을 씁니다. 재귀를 허용하면 불변식이 깨진 중간 상태에서 함수가 불릴 수 있고, condvar와도 섞이지 않습니다."
- EN: *"Instead of a recursive mutex I use a `_locked` suffix convention. Recursion lets a function run while the invariant is broken, and it doesn't compose with condition variables — cond_wait only drops the lock once."*

**⑤ 종료**
- KO: "`pthread_cancel`은 쓰지 않습니다. stop 플래그를 세우고, 블로킹 중이면 condvar broadcast나 self-pipe로 깨운 다음 join해서 실제로 끝난 걸 확인하고 자원을 해제합니다."
- EN: *"I never use pthread_cancel — it leaves locks held and fds leaked. I set a stop flag, wake the thread with a broadcast or a self-pipe write, then join before freeing anything."*

**⑥ 검증**
- KO: "동시성 코드는 눈으로 검증하지 않습니다. ThreadSanitizer로 돌리고, 불변식을 검사하는 checker 스레드를 테스트에 넣고, 결정적인 단정만 씁니다."
- EN: *"I don't eyeball concurrency. I run it under ThreadSanitizer, add a checker thread that asserts the invariant while writers run, and keep every assertion deterministic so the test never flakes."*

---

## 5. 체크리스트

- [ ] `counter++`가 왜 원자적이 아닌지 3단계(load/modify/store)로 설명할 수 있다
- [ ] `volatile`이 동기화에 쓸모없는 이유, mutex가 **메모리 가시성**도 준다는 점을 말할 수 있다
- [ ] 락이 보호하는 것을 "불변식" 단위로 말하고, snapshot 함수로 한 번에 복사해 나온다
- [ ] 락 안에서 I/O·malloc·콜백을 하지 않는다 (우선순위 역전까지 설명 가능)
- [ ] `_locked` 규약 + 재귀 뮤텍스를 피하는 이유 3가지 + `ERRORCHECK` 진단을 안다
- [ ] check-then-act를 한 임계구역으로 묶는다 (TOCTOU)
- [ ] `pthread_once` lazy init(DCLP 직접 금지) / 집계는 스레드 로컬 후 일괄 병합
- [ ] rwlock을 언제 쓰고 언제 안 쓰는지, upgrade가 왜 불가능한지 말할 수 있다
- [ ] `pthread_cancel` 대신 stop 플래그 + broadcast/self-pipe + join으로 종료한다
- [ ] 모든 에러 경로에서 unlock이 되는지 확인하고, 종료는 stop → join → destroy
- [ ] TSan으로 돌려봤고 의도된 race(Q1)가 어디인지 설명할 수 있다

---

## 6. 실행 메모 & Verkada 맥락

### ⚠️ ThreadSanitizer 실행 시 주의

Q1은 **일부러 race를 만드는 문제**라 TSan이 반드시 경고한다. 그 경고가 나머지 9문제의
결과를 가리지 않도록 `__has_feature(thread_sanitizer)`로 TSan 빌드를 감지해 **Q1 데모만
직렬 실행**으로 바꿔 두었다 — 그래서 기본 TSan 실행은 경고 0이다. race를 TSan 리포트로
직접 보려면 `-DVK_FORCE_RACE=1` 을 붙인다 → `race_worker` 에서 data race 정확히 1건.

### Verkada / 게이트웨이 맥락

GC31-E 게이트웨이 펌웨어에는 최소한 모뎀 제어(듀얼 SIM failover), 링크 모니터(WAN
RTT), PoE 컨트롤러(60W를 2포트에 배분), 이벤트 업로더, 로컬 설정 서버가 동시에 돈다.
이들이 **같은 상태**를 건드린다 — 링크 통계, 전력 예산, APN/MTU 설정, 모뎀 핸들.

- **Q3 링크 통계**는 Command 대시보드로 올라간다. 찢어지면 "평균 RTT가 min보다 작다"는 불가능한 그래프가 나온다.
- **Q6 전력 예산**은 안전 문제다. 802.3bt 60W를 초과 배정하면 PSE가 셧다운되고 카메라가 내려간다 — Perpetual PoE를 내세우는 제품에서 가장 나쁜 실패다.
- **Q4 I/O를 락 밖으로**는 셀룰러 링크가 느려질 때(RSRP 낮음, 재접속 중) 파이프라인 전체가 멈추지 않게 하는 핵심이다.
- **Q10 깔끔한 종료**는 GW31-E의 "오프라인 30분이면 자가 재부팅", 원격 power-cycle, OTA 직전 셧다운 경로 그대로다.
- 170개국 200만 대 fleet에서 "백만 번에 한 번" 나는 race는 **매일 여러 번** 난다 — TSan·불변식 checker·결정적 테스트를 말할 수 있느냐가 갈린다.
