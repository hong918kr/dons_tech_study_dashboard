# 08. 무중단 설정 교체 — 읽는 중에 바꿔치기해도 안 터지게

> **이 노트를 다 읽으면**:
> - 설정을 immutable 객체로 만들고 포인터만 원자적으로 갈아끼우는 패턴을 설명할 수 있다
> - "포인터를 읽고 나서 refcount를 올린다" 사이의 use-after-free 창을 그림으로 설명하고, 그걸 닫는 세 가지 방법(짧은 락 / hazard pointer / RCU)을 비교할 수 있다
> - 마지막 refcount 감소에만 release + acquire fence가 필요한 이유를 말할 수 있다
>
> **선행**: `00_start_here` 의 §3 뮤텍스, §5 원자 연산과 메모리 순서 · `03_double_buffer`
>
> **연습**: `make run N=08` (내 구현) · `make sol N=08` (모범답안) · `make tsan N=08`

---

## 0. 한 문장으로

읽기는 초당 수만 번, 쓰기는 하루에 몇 번인 **설정 객체**를, 읽는 쪽을 한 번도 멈추지 않고 통째로 교체하고 헌 객체는 아무도 안 보게 된 순간에만 free하는 문제다.

Verkada 게이트웨이에서 클라우드가 APN, WAN 우선순위, 업로드 주기를 내려보내면 업로더·모뎀 감시·로그 수집 워커가 전부 그 설정을 읽는 중이고, 여기서 헌 설정을 잘못 free하면 현장 장비가 재부팅된다.

## 1. 왜 필요한가 — 비유로 시작

펌웨어의 **A/B 뱅크 스왑**과 똑같다. 새 이미지를 B 뱅크에 통째로 다 굽고 마지막에 부트 포인터 한 워드만 B로 바꾼다. 절반만 바뀐 상태가 세상에 보이는 순간이 없다. 차이는 하나다 — 펌웨어는 재부팅하면 아무도 A 뱅크를 안 보지만, 여기서는 **스왑 순간에도 누군가 헌 객체를 읽고 있다**. 그래서 "헌 뱅크를 언제 지워도 되나"를 따로 추적해야 한다. 그게 refcount다.

```
              s->cur ──────────────┐
                                   v
  [Config v1 refs=3]        [Config v2 refs=1]
     ^   ^   ^
    W1  W2  W3   ← 아직 v1 을 읽는 중인 워커들

  publish(v2) 직후: s->cur 은 v2. v1 은 "도달 불가"지만 아직 살아 있다.
  W1,W2,W3 이 차례로 release 하면 refs 3→2→1→0, 0이 되는 순간 free.
```

`refs` 는 "이 객체를 가리키는 손가락 개수"다. 0이 되면 지운다.

## 2. 알아야 할 개념

### immutable object (불변 객체)

한 번 만들어지면 내용이 절대 안 바뀌는 객체. 바꾸고 싶으면 **새로 만들어서 통째로 교체**한다. 내용이 안 바뀌면 읽는 쪽은 락이 필요 없다 — 락은 "남이 바꾸는 중"을 막는 도구인데 바꾸는 사람이 없으니까. 아는 것과의 대응: OTP 영역이나 read-only 매핑된 파라미터 블록.

### refcount (참조 카운트)

객체를 쓰는 사람이 들어올 때 +1, 나갈 때 -1. 0이 되면 해제한다. "언제 free해도 되는가"를 중앙에서 알 수 없으니 각자 자기 사용 구간을 신고하게 만드는 것이다. 아는 것과의 대응: DMA 디스크립터를 in-flight 카운트로 관리하다가 0이 될 때 버퍼를 풀에 반납하는 것.

### atomic pointer

포인터 변수를 `_Atomic(Config *)` 으로 선언하면 읽기·쓰기가 **찢어지지 않는다**(torn read 없음). 다른 스레드는 옛 포인터 아니면 새 포인터를 보지, 반쯤 섞인 값을 보지 않는다.

### acquire / release

`release` store는 이 store **이전에 내가 한 모든 쓰기**를 acquire로 읽은 쪽에게 보이게 하고, `acquire` load는 이 load **이후의 내 읽기**가 앞당겨지지 않게 한다. 한 문장으로: **release로 내보내고 acquire로 받으면 내용이 포인터보다 먼저 보인다.** 이게 없으면 "포인터는 새 것인데 그 안의 apn 필드는 아직 쓰레기"인 상태를 볼 수 있다.

### use-after-free 와 reclamation

use-after-free는 이미 free된 메모리를 읽거나 쓰는 것. 이 문제의 진짜 적이고, 대부분 조용히 지나가다가 현장에서 몇 주에 한 번 터진다.

reclamation(회수)은 "아무도 안 보는 게 확실해진 객체를 실제로 지우는 일". lock-free 자료구조가 어려운 이유의 90%가 자료구조 자체가 아니라 이 회수 문제다.

## 3. 문제 읽기

| 요구 | 설계에 주는 제약 |
|---|---|
| 읽기가 아주 빈번 | 읽기 경로에 긴 락·할당·시스템콜 금지 |
| 쓰기는 아주 드묾 | 쓰기는 비싸도 된다. 객체 새로 malloc해도 된다 |
| 읽는 중 교체 가능 | 읽는 쪽이 잡은 객체는 그 스레드가 놓을 때까지 살아 있어야 한다 |
| 메모리 누수 금지 | 교체된 객체는 **정확히 한 번** free되어야 한다 |
| 필드 일관성 | `version` 과 `upload_period_ms` 가 서로 다른 세대에서 섞이면 안 됨 |

마지막 줄이 self test의 불변식이다 — `period_ms == version * 10` 을 깨는 조합을 본 리더가 하나라도 있으면 실패다.

## 4. 단계별로 만들기

### 4-1. 설정을 통짜 객체로 만든다

목표: 필드를 하나씩 바꾸는 길을 아예 없앤다.

```c
typedef struct {
    _Atomic int refs;         /* 이 객체를 붙잡고 있는 스레드 수 */
    uint32_t    version;
    uint32_t    upload_period_ms;
    char        apn[32];
} Config;
```

```c
Config *config_new(uint32_t version, uint32_t period_ms, const char *apn)
{
    Config *c = calloc(1, sizeof *c);
    if (!c) return NULL;
    atomic_init(&c->refs, 1);                   /* store가 들고 있는 참조 1 */
    c->version = version;
    c->upload_period_ms = period_ms;
    snprintf(c->apn, sizeof c->apn, "%s", apn);
    return c;
}
```

`refs` 만 `_Atomic` 이다. 나머지 필드는 publish 이후 **아무도 안 바꾸므로** atomic일 필요가 없다. 초기값이 1인 게 핵심인데, 이 1은 "store 자신이 들고 있는 참조"다. 0으로 시작하면 publish 직후 아무 리더도 안 잡은 찰나에 카운트가 0이라 free 대상처럼 보인다.

### 4-2. 감소와 해제를 한 곳에 모은다

목표: free는 오직 여기서만 일어나게 한다.

```c
static void cfg_drop(ConfigStore *s, Config *c)
{
    /* release: 내가 이 객체에 한 모든 접근이 감소보다 먼저 보이게 */
    if (atomic_fetch_sub_explicit(&c->refs, 1, memory_order_release) == 1) {
        /* 마지막 참조였다 → 다른 스레드의 접근을 모두 본 뒤 해제 */
        atomic_thread_fence(memory_order_acquire);
        atomic_fetch_add(&s->frees, 1ul);
        free(c);
    }
}
```

`atomic_fetch_sub` 는 **감소 전 값**을 돌려준다. 그래서 `== 1` 이면 내가 마지막 손가락이었다는 뜻이다. `== 0` 으로 쓰면 아무도 free를 안 한다.

메모리 순서가 왜 비대칭인가 — 이 부분이 면접 질문이다.

- **release 로 감소**: 내가 `c->apn` 을 읽은 행위가 감소보다 먼저 일어난 것으로 보여야 한다. 안 그러면 마지막 스레드가 free한 뒤로 내 읽기가 재배치될 수 있다.
- **0이 됐을 때만 acquire fence**: 나는 이제 `free(c)` 를 하려 한다. 그러려면 **다른 스레드들이 c에 한 모든 접근이 나에게 보여야** 한다. 그들은 release로 감소했고 나는 acquire로 그걸 받는다. release/acquire 짝이 여기서 맞춰진다. 빼면 x86에서는 통과하고 약한 메모리 모델(ARM)에서 깨진다.

### 4-3. 진짜 함정 — acquire 경로

목표: 리더가 안전하게 손가락을 하나 올린다.

순진하게 쓰면 이렇게 된다. **이건 틀린 코드다.**

```
Config *c = atomic_load(&s->cur);      // (1)
atomic_fetch_add(&c->refs, 1);         // (2)
```

(1)과 (2) 사이에 publisher가 끼어들면:

```
T1(reader)                        T0(publisher)
 (1) c = load(cur)  -> v1
                                  publish(v2): cur = v2
                                  cfg_drop(v1): refs 1→0 → free(v1)
 (2) fetch_add(&c->refs, 1)
       ↑ 이미 free된 메모리를 건드린다 = use-after-free
```

두 줄이 각각은 atomic인데 **묶어서는 atomic이 아니다**. 이게 이 문제의 심장이다. 이 창을 닫는 방법은 실질적으로 셋뿐이다.

1. **아주 짧은 락**으로 (1)+(2)를 하나로 묶는다 — 이 모범답안이 채택한 방법
2. **hazard pointer** — 리더가 "나 지금 이 주소 본다"를 전역 슬롯에 먼저 게시하고, free하려는 쪽이 모든 슬롯을 스캔해서 걸리면 미룬다
3. **RCU / epoch-based reclamation** — 리더는 아무 표시도 안 하고, "모든 리더가 최소 한 번 쉬어간 시점(grace period)"이 지난 뒤에 free한다. Linux 커널의 `rcu_read_lock` / `synchronize_rcu` 가 이것이다.

```c
Config *cfg_acquire(ConfigStore *s)
{
    pthread_mutex_lock(&s->writer_m);
    Config *c = atomic_load_explicit(&s->cur, memory_order_acquire);
    atomic_fetch_add_explicit(&c->refs, 1, memory_order_acquire);
    pthread_mutex_unlock(&s->writer_m);
    return c;
}
```

락 구간이 **두 줄**, 몇 나노초다. 그리고 리더가 실제로 설정을 **쓰는** 시간은 전부 락 밖이다. "읽기에 락 쓰지 마라"는 원칙을 어긴 게 아니라, 락을 포인터 획득에만 쓰고 데이터 사용에는 안 쓴 것이다.

**면접에서 이 말을 꼭 해야 한다**: 임베디드 코드 대부분에서 정답은 hazard pointer도 RCU도 아니고 이 짧은 락이다. hazard pointer는 리더마다 슬롯과 스캔 비용이 들고, RCU는 grace period를 관리할 인프라가 필요하다. 코어 4개짜리 게이트웨이에서 나노초짜리 uncontended mutex의 경합보다 직접 구현한 lock-free 회수 로직의 버그가 훨씬 비싸다. **lock-free는 측정으로 필요가 증명됐을 때만 간다.**

### 4-4. 게시(publish)

목표: 새 설정을 내걸고, 헌 설정의 store 참조를 내려놓는다.

```c
void cfg_publish(ConfigStore *s, Config *fresh)
{
    pthread_mutex_lock(&s->writer_m);           /* acquire와 같은 락 → 경쟁 창 제거 */
    Config *old = atomic_load_explicit(&s->cur, memory_order_relaxed);
    /* release: fresh의 필드 초기화가 포인터 publish보다 먼저 보이게 */
    atomic_store_explicit(&s->cur, fresh, memory_order_release);
    pthread_mutex_unlock(&s->writer_m);

    /* 이 시점부터 old는 store에서 도달 불가 → 남은 참조가 0이 되면 안전하게 free */
    cfg_drop(s, old);
}
```

줄별로 보면:

- `cfg_acquire` 와 **같은 락**을 쓴다. 다른 락이면 4-3의 창이 그대로 열려 있다.
- `old` 를 읽을 때는 `relaxed` 로 충분하다. 이미 락이 순서를 보장한다.
- 새 포인터 store는 `release`. `config_new` 안에서 쓴 `version`, `apn` 이 포인터보다 먼저 보이게 만든다. 이게 없으면 리더가 새 포인터를 보고 그 안에서 쓰레기를 읽는다.
- `cfg_drop(s, old)` 는 **락 밖**이다. free가 락 안에 있으면 리더가 malloc 락 경합까지 떠안는다.

락을 풀고 drop해도 안전한 이유: 락을 푼 순간 `s->cur` 은 이미 `fresh` 라 그 뒤에 들어오는 리더는 `old` 를 잡을 길이 없고, `old` 를 이미 잡고 있던 리더들은 각자 refs를 들고 있으므로 내 감소로 0이 되지 않는다.

### 4-5. 종료 — 초기 참조 갚기

```c
void cfg_store_destroy(ConfigStore *s)
{
    Config *c = atomic_load(&s->cur);
    if (c) cfg_drop(s, c);
    pthread_mutex_destroy(&s->writer_m);
}
```

`config_new` 가 걸어둔 초기 참조 1을 여기서 갚는다. 그래서 self test가 `frees == NPUBLISH`(2000)로 끝난다. destroy 전에는 1999다 — 마지막 설정은 아직 store가 들고 있으니까.

## 5. 전체 흐름 따라가기

```
 T0 (publisher)                      T1 (reader)            v1.refs  v2.refs
 ─────────────────────────────────────────────────────────────────────────
 init: cur=v1                                                  1        -
                                     acquire: lock             1
                                       load cur -> v1
                                       refs 1→2                2
                                     unlock
                                     c->version 읽는 중         2
 publish(v2): lock
   old = v1
   cur = v2 (release)
 unlock                                                        2        1
 cfg_drop(v1): refs 2→1  (아직 0 아님)                          1        1
                                     release: refs 1→0         0
                                       마지막! acquire fence
                                       free(v1)                -        1
                                     acquire: 이제 v2 를 잡는다           2
```

핵심은 **T0이 free를 하지 않았다는 것**이다. 마지막 손가락을 놓은 리더가 free했다. 누가 free할지는 실행마다 달라지고 그래야 맞는 설계다.

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
|---|---|---|---|
| `load` 후 락 없이 `fetch_add` | 드물게 SIGSEGV, ASan heap-use-after-free | 두 연산 사이에 free가 끼어든다 | 짧은 락으로 묶거나 hazard pointer / RCU |
| `atomic_init(&c->refs, 0)` | publish 직후 즉시 free, 이중 해제 | store 자신의 참조를 안 셈 | 1로 시작 |
| `fetch_sub(...) == 0` 으로 판정 | 메모리 무한 증가 (누수) | fetch_sub는 **감소 전** 값을 반환 | `== 1` 로 판정 |
| 마지막 감소 후 acquire fence 없음 | x86에서는 통과, ARM에서 랜덤 크래시 | 다른 스레드의 접근이 free보다 뒤로 재배치 가능 | `atomic_thread_fence(memory_order_acquire)` |
| `cur` store를 `relaxed` 로 | 새 버전인데 `apn` 이 빈 문자열 | 필드 초기화가 포인터 publish보다 늦게 보임 | `memory_order_release` |
| `cfg_release` 를 안 부름 | `frees` 가 안 늘고 메모리 증가 | 손가락이 안 내려옴 | acquire/release를 같은 함수 안에서 짝맞춤 |
| free를 락 안에서 | 리더 지연 튐(tail latency) | 락 구간에 allocator 경합 유입 | drop은 항상 락 밖 |

## 7. 직접 확인하기

모범답안부터.

```sh
make sol N=08
```

```
publishes=2000 reads=15802 inconsistent=0 freed=1999 last_ver=2000 refs=1
08 PASS
```

읽을 것은 세 숫자다. `inconsistent=0` 은 세대가 섞인 설정을 본 리더가 없다는 뜻, `freed=1999` 는 교체된 것들이 정확히 한 번씩 해제됐다는 뜻(마지막 하나는 아직 store 소유), `refs=1` 은 종료 시점에 남은 손가락이 store 것 하나뿐이라는 뜻이다.

`make tsan N=08` 은 `starters/08_config_swap_refcount.c` 를 빌드하므로 내가 구현을 채워 넣기 전에는 링크 에러가 나는 게 정상이다. 채운 뒤 돌리면 경고 없이 `08 PASS` 가 나와야 한다(모범답안은 TSan 아래에서 깨끗하다). 버그를 눈으로 보고 싶으면 파일을 복사해서 `cfg_acquire` 의 `pthread_mutex_lock` / `unlock` 두 줄만 지우고 AddressSanitizer로 빌드해 보면 된다.

```sh
cc -std=c11 -Wall -Wextra -O1 -g -pthread -fsanitize=address 08_bad.c -o /tmp/08bad && /tmp/08bad
```

```
ERROR: AddressSanitizer: heap-use-after-free on address 0x604000001898
READ of size 4 at 0x604000001898 thread T4
    #0 ... in reader 08_bad.c:130
0x604000001898 is located 8 bytes inside of 44-byte region
```

`reader` 에서 터졌고, 44바이트 영역(= `Config` 크기) 안쪽 8바이트 위치를 읽었다고 나온다. `refs` 가 offset 0, `version` 이 4, `upload_period_ms` 가 8이다. 즉 이미 해제된 설정의 필드를 읽은 것이다. 이게 현장에서는 "한 달에 한 번 리부트"로 나타난다.

## 8. 면접에서 말하기

- 설정을 immutable 객체로 만들고 포인터만 원자적으로 교체한다. 부분 갱신 상태가 보이는 순간이 없다 — 펌웨어 A/B 뱅크 스왑과 같은 아이디어다. 진짜 어려운 부분은 교체가 아니라 회수다. 포인터를 읽는 것과 refcount를 올리는 것이 함께 원자적이지 않아서 그 사이에 헌 객체가 free될 수 있다.
- 나는 그 두 연산만 아주 짧은 mutex로 묶었다. lock-free 대안은 hazard pointer와 RCU인데, 임베디드 게이트웨이에서는 나노초짜리 uncontended mutex보다 직접 만든 회수 로직의 버그가 훨씬 비싸다.
- 마지막 감소는 release로 하고, 0이 됐을 때만 acquire fence를 건 뒤 free한다. 그래야 다른 스레드가 그 객체에 한 접근을 모두 본 뒤에 해제된다. 이걸 빼면 x86에서는 통과하고 ARM에서 깨진다.
- MMU 없는 타깃이면 malloc/free 대신 고정 개수 static slot pool을 쓴다. 설정 슬롯 4개를 미리 잡아두고 refs가 0인 슬롯을 재사용하면, 같은 refcount 논리를 그대로 쓰면서 heap fragmentation과 할당 실패 경로가 사라진다.

English:

- The config is an immutable object; publishing is a single atomic pointer store, so readers never see a half-updated config. The hard part is reclamation, not the swap: loading the pointer and incrementing its refcount are not atomic together, so the object can be freed in that window.
- I close the window with a very short mutex around just those two operations. The lock-free alternatives are hazard pointers and RCU or epoch-based reclamation, but for most embedded code a tiny lock is the right answer.
- The final decrement uses release ordering, and only the thread that drops the count to zero takes an acquire fence before freeing, so it observes every other thread's accesses to that object.
- On a target without dynamic allocation I'd back this with a static pool of config slots and reuse the slot whose refcount is zero, keeping the same discipline without malloc.

## 9. 요약 & 체크리스트

교체는 쉽고 회수가 어렵다. immutable 객체 + atomic 포인터 store(release)로 게시하고, 리더는 refcount로 자기 사용 구간을 신고한다. 포인터 로드와 refcount 증가 사이의 창이 이 문제 전체의 핵심이고, 짧은 락·hazard pointer·RCU 셋 중 하나로 닫아야 한다. 임베디드에서는 짧은 락이 거의 항상 정답이다. 마지막 감소는 release, 0일 때만 acquire fence 후 free.

- [ ] `refs` 를 1로 초기화하는 이유(store 자신의 참조)를 설명할 수 있다
- [ ] `fetch_sub(...) == 1` 이 마지막 참조라는 뜻임을 안다
- [ ] 락 없는 `load` + `fetch_add` 가 왜 use-after-free인지 타임라인으로 그릴 수 있다
- [ ] hazard pointer와 RCU의 차이를 말하고, "그래도 임베디드에선 짧은 락이 낫다"는 근거를 댈 수 있다
- [ ] publish의 store가 `release` 여야 하는 이유를 필드 초기화와 연결해 설명할 수 있다
- [ ] 마지막 감소의 release + acquire fence 조합을 설명할 수 있다
- [ ] malloc 없는 타깃에서 static slot pool로 바꾸는 방법을 말할 수 있다
