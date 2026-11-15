---
id: ELX-M04
time: 90분
updated: 2026-11-08
---
# ELX-M04 드라이버 동기화: spinlock vs mutex vs atomic

## 학습 목표

- 드라이버의 공유 상태를 **프로세스 컨텍스트**(ioctl)와 **하드 IRQ 컨텍스트**(hrtimer 콜백)가 같이 만질 때, 어떤 락을 어디에 써야 하는지 표로 고른다.
- 진짜 커널(6.18.55-dbg, QEMU TCG cortex-a57 -smp 4)에서 lost update를 재현하고, 잘못된 락을 일부러 써서 **lockdep · DEBUG_ATOMIC_SLEEP 경고의 실물**을 읽는다.
- `spin_lock_irqsave` 와 `atomic_long_t` 로 고친 뒤 **같은 도구**로 효과를 확인한다: 카운트 정확, dmesg에 `BUG:/WARNING:/lockdep` 줄 0개.
- `atomic_t` 는 왜 카운터는 지키고 리스트는 못 지키는지 숫자로 보인다.
- lockdep이 실제 교착 없이 **순서 위반과 컨텍스트 위반**을 잡는 원리(클래스 vs 인스턴스), `raw_spinlock_t` 와 PREEMPT_RT를 면접에서 설명한다.

## 1. 문제

드라이버 코드의 동기화 버그는 펌웨어 검증과 bring-up에서 "가끔 멈춤 · 가끔 카운트가 틀림"으로 나타난다. 그래서 면접에서는 **"이 공유 변수를 ISR과 프로세스가 같이 쓰면 무슨 락을 쓰나?"** 를 묻는다. RTOS에서는 ISR과 태스크(C-M14, RTOS-B10)였고, 리눅스에서는 그 답이 `spin_lock_irqsave`, `mutex`, `atomic_t` 중 하나다. 이 레슨은 그 선택을 말로 외우는 대신 **잘못 고르면 커널이 뭐라고 하는지**를 직접 본다.

> 환경: 모든 커널 출력은 **QEMU TCG cortex-a57 -smp 4 위의 Linux 6.18.55-dbg** (PROVE_LOCKING + LOCK_STAT + DEBUG_ATOMIC_SLEEP + PROVE_RAW_LOCK_NESTING) 실측이다. 모듈은 게스트 안에서만 올린다. TCG는 실제 CPU가 아니므로 **횟수는 사실이지만 시간 수치는 비율로만** 본다.

준비물은 misc 문자 장치 `/dev/syncdrv` 하나다. 공유 상태는 **카운터 + 연결 리스트**이고, 이것을 두 경로가 건드린다.

- **프로세스 경로**: N개의 스트레스 스레드가 `ioctl(SD_IOC_UPDATE)`.
- **하드 IRQ 경로**: 주기적 `hrtimer` 콜백. 콜백 안에서 `in_hardirq()` 를 검사해서 하드 IRQ인지 매번 스스로 확인한다 (`not_hardirq` 카운터, 전 실행에서 0).

`mode` 하나로 보호 방식을 바꾸므로 같은 코드 경로에서 버그와 수정을 나란히 볼 수 있다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">어떤 락을 어디에: 컨텍스트 → 락 선택 (실측 결과)</text>
<rect x="10" y="35" width="200" height="50" class="f-acc" stroke="currentColor"/>
<text x="20" y="55" fill="currentColor">프로세스 경로만</text>
<text x="20" y="75" fill="currentColor" font-size="12">(sleep 가능)</text>
<rect x="10" y="100" width="200" height="50" class="f-acc" stroke="currentColor"/>
<text x="20" y="120" fill="currentColor">프로세스 + 하드 IRQ 경로</text>
<text x="20" y="140" fill="currentColor" font-size="12">(같은 상태 공유)</text>
<rect x="10" y="165" width="200" height="50" class="f-acc" stroke="currentColor"/>
<text x="20" y="185" fill="currentColor">단일 카운터/플래그</text>
<text x="20" y="205" fill="currentColor" font-size="12">(read-modify-write 하나)</text>
<rect x="10" y="230" width="200" height="50" class="f-acc" stroke="currentColor"/>
<text x="20" y="250" fill="currentColor">진짜 하드 IRQ 콜백</text>
<text x="20" y="270" fill="currentColor" font-size="12">(HRTIMER_MODE_REL_HARD)</text>
<line x1="210" y1="60" x2="290" y2="60" stroke="currentColor"/>
<line x1="210" y1="125" x2="290" y2="125" stroke="currentColor"/>
<line x1="210" y1="190" x2="290" y2="190" stroke="currentColor"/>
<line x1="210" y1="255" x2="290" y2="255" stroke="currentColor"/>
<rect x="290" y="35" width="430" height="50" class="f-ok" stroke="currentColor"/>
<text x="300" y="55" fill="currentColor">mutex (다른 경로 없을 때만): 정확, 경고 0</text>
<text x="300" y="75" fill="currentColor" font-size="12">IRQ 경로에서 부르면 → BUG: sleeping function called ...</text>
<rect x="290" y="100" width="430" height="50" class="f-ok" stroke="currentColor"/>
<text x="300" y="120" fill="currentColor">spin_lock_irqsave: 정확, 경고 0</text>
<text x="300" y="140" fill="currentColor" font-size="12">plain spin_lock → inconsistent {IN-HARDIRQ-W} -&gt; {HARDIRQ-ON-W}</text>
<rect x="290" y="165" width="430" height="50" class="f-ok" stroke="currentColor"/>
<text x="300" y="185" fill="currentColor">atomic_long_t: 카운터는 정확</text>
<text x="300" y="205" fill="currentColor" font-size="12">리스트는 못 지킴: list_add 는 여러 번의 쓰기 → 노드 유실</text>
<rect x="290" y="230" width="430" height="50" class="f-warn" stroke="currentColor"/>
<text x="300" y="250" fill="currentColor">raw_spinlock_t (Part C / 추가 연습 P1)</text>
<text x="300" y="270" fill="currentColor" font-size="12">spinlock_t 는 lockdep이 [ BUG: Invalid wait context ]</text>
<text x="10" y="310" fill="currentColor" font-size="12">일반 hrtimer(HRTIMER_MODE_REL)도 이 커널(비 RT)에서는 하드 IRQ에서 돈다. 이 레슨 표의 "IRQ 경로"는 그것을 뜻한다.</text>
</svg>
```

### Part A — lost update 재현과 두 가지 수정

- A1. `sd_irqsave_lock()` / `sd_irqsave_unlock()` — `sd.lock` 을 **로컬 IRQ를 끄고** 잡는다. 이전 IRQ 상태는 `flags` 에 넣는다.
- A2. `sd_atomic_touch()` — 카운터를 `sd.acounter` 로 원자적으로 증가시킨다.
- 공유 상태를 건드리는 나머지 코드는 이미 있다. `mode=none` 은 보호 없음, `mode=mutex` 는 프로세스 경로 전용.
- `selftest` ioctl이 irqsave 헬퍼가 정말 락을 쥐고 IRQ를 껐는지, atomic이 1000번 정확히 세는지 **타이밍 없이** 검사한다.

### Part B — 일부러 잘못 쓰고 경고 읽기

`check.sh demo <시나리오>` 로 부팅마다 하나씩 재현하고 리포트를 읽는다 (lockdep은 첫 경고 뒤 스스로 꺼지므로 시나리오마다 새로 부팅한다).

| 시나리오 | 무엇을 잘못 썼나 | 기대 |
|---|---|---|
| `nolock` | 보호 없음 | `counter ≠ N×K + irq_fires`, 경고 0개 (lockdep은 락 없는 race를 못 본다) |
| `mutex_irq` | IRQ 경로에서 `mutex_lock` | `BUG: sleeping function called from invalid context` |
| `spin_irq` | 양쪽에서 `spin_lock` | lockdep `inconsistent {IN-HARDIRQ-W} -> {HARDIRQ-ON-W} usage` |
| `spin_deadlock` | 위 + 같은 CPU에 고정 | 실제 멈춤 (`timeout` 이 끊는다) |
| `abba` | A→B 후 B→A | `possible circular locking dependency` (교착 없이!) |
| `recursive` | 같은 클래스 두 인스턴스 | `possible recursive locking detected` (OS-M11의 클래스 vs 인스턴스) |
| `sleep_in_spin` | 스핀락 쥔 채 `msleep` | `BUG: scheduling while atomic` |

### Part C — atomic의 한계와 REL_HARD

- `atomic` 모드에 리스트를 켜면(`with_list=1`) 카운터는 정확한데 리스트 길이는 짧아진다. 왜인가?
- 타이머를 `HRTIMER_MODE_REL_HARD` 로 바꾸면(`500h`) 같은 `spin_lock_irqsave` 코드가 lockdep에 걸린다. 왜인가? 고치는 건 `raw_spinlock_t` (추가 연습 P1).

## 2. Problem (English)

> Implement the two correct protections in a misc-char driver whose state (a counter and a linked list) is updated from both process context (`ioctl` from N threads) and hard-IRQ context (a periodic `hrtimer`).
>
> ```c
> static void sd_irqsave_lock(unsigned long *flags);   /* take sd.lock, IRQs off, save old state */
> static void sd_irqsave_unlock(unsigned long flags);  /* release, restore IRQ state */
> static void sd_atomic_touch(void);                   /* counter via sd.acounter, no lost increments */
> ```
>
> Constraints: the mutex path must never be reached from the IRQ path in correct configurations; modes must not sleep while holding a spinlock.
>
> Verification (`check.sh solution`, in a QEMU guest running 6.18.55-dbg with lockdep): the driver's irqsave mode (4 threads × 20000 updates with a 500 µs periodic hrtimer) must end with `counter == list_len == N*K + irq_fires`; atomic mode must end with `counter == N*K + irq_fires`; mutex mode with no IRQ path must be exact; `dmesg` must contain no `BUG:`, `WARNING:`, `lockdep`, `possible ... deadlock`, or `inconsistent` lines.
>
> Example:
> ```text
> RESULT mode=irqsave timer_us=500 with_list=1 threads=4 k=20000 counter=80562 list_len=80562 irq_fires=562 expected=80562 lost=0 list_lost=0 ...
> ```
>
> Then explain: why a `mutex` can't be taken in the hrtimer callback; why `spin_lock` without `_irqsave` deadlocks against the hard-IRQ path; why an atomic counter doesn't protect the list.

## 3. 시작 코드

세 파일이 함께 빌드된다: 모듈(`starter.c` · `solution.c` · `practice.c`, 각각 `VARIANT=` 로 고른다), 공유 ioctl 헤더 `syncdrv_uapi.h`, 게스트에서 돌리는 `stress.c`(`gcc -static`). `check.sh` 가 **VM 안에서** 빌드 → initramfs → QEMU 부팅 → 판정을 한다.

```bash
# VM(Lima elinux) 안에서. 모듈은 항상 QEMU 게스트 안에서만 올라간다 (VM 커널에 insmod 금지)
limactl shell elinux -- bash -lc 'bash <레슨 디렉터리>/check.sh starter'    # TODO 때문에 FAIL, exit 1
limactl shell elinux -- bash -lc 'bash <레슨 디렉터리>/check.sh solution'   # 전부 PASS, exit 0
limactl shell elinux -- bash -lc 'bash <레슨 디렉터리>/check.sh demo spin_irq' # 잘못 쓴 예 재현, 로그 출력
```

<!-- code: syncdrv_uapi.h -->

<!-- code: stress.c -->

<!-- code: check.sh -->

<!-- code: starter.c -->

## 4. 힌트

<details><summary>힌트 1 — spin_lock_irqsave 는 왜 flags 를 받나</summary>

호출 시점에 IRQ가 켜져 있었을 수도, 이미 꺼져 있었을 수도 있다. 그래서 "끈다"가 아니라 "저장하고 끄고, 끝나면 **저장한 대로** 돌린다". 헬퍼 인자가 `unsigned long *flags` 이므로 매크로에는 `*flags` 를 넘긴다(`spin_lock_irqsave(&sd.lock, *flags)` 는 매크로이고 인자에 값을 대입한다). 해제는 값으로 받은 `flags` 를 `spin_unlock_irqrestore` 에 넘긴다.
</details>

<details><summary>힌트 2 — atomic 은 어느 API 인가</summary>

`atomic_long_t acounter` 가 이미 구조체에 있다. 증가 하나면 된다: `atomic_long_inc(&sd.acounter)`. 메모리 순서 얘기는 C-A03 소관이고, 여기서는 "read-modify-write 하나가 쪼개지지 않는다"만 쓴다.
</details>

<details><summary>힌트 3 — 왜 starter 가 FAIL 이 나는가</summary>

`selftest` ioctl은 동시 실행과 상관없이 결정적으로 검사한다. `sd_irqsave_lock()` 이 빈 함수면 `spin_is_locked(&sd.lock)` 이 0이므로 FAIL, 1000번 호출해도 `acounter` 가 0이므로 FAIL이다. 스트레스 결과는 그 다음이다.
</details>

<details><summary>힌트 4 — 데모가 "멈출" 때</summary>

`spin_deadlock` 시나리오는 진짜로 멈춘다. `check.sh` 는 QEMU를 `timeout` 으로 감싸므로 Mac이나 VM은 영향을 받지 않는다. 멈춘 뒤 콘솔에 RCU stall 경고가 찍히는 것까지가 데모다.
</details>

## 5. 풀이

### A1 — spin_lock_irqsave: 왜 irqsave 인가

```c
static void sd_irqsave_lock(unsigned long *flags)
{
	spin_lock_irqsave(&sd.lock, *flags);
}

static void sd_irqsave_unlock(unsigned long flags)
{
	spin_unlock_irqrestore(&sd.lock, flags);
}
```

같은 CPU에서 생기는 교착 때문이다. 프로세스 경로가 `spin_lock` 으로 락을 쥔 동안 그 CPU에 타이머 인터럽트가 들어오면, 콜백이 같은 락을 잡으려고 스핀한다. 락을 쥔 쪽(프로세스)은 인터럽트에 눌려 있어서 영원히 못 푼다. **`irqsave` 는 이 CPU의 인터럽트를 끄고 락을 잡아서 그 끼어들기를 없앤다.** 다른 CPU에서 온 IRQ는 스핀으로 기다리면 되고, 그 동안 락 쥔 CPU는 진행한다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">같은 CPU: plain spin_lock + 인터럽트 = 스스로를 기다리는 교착</text>
<text x="10" y="58" fill="currentColor">CPU0 (프로세스)</text>
<rect x="130" y="40" width="130" height="30" class="f-ok" stroke="currentColor"/>
<text x="140" y="60" fill="currentColor">spin_lock(&amp;sd.lock)</text>
<rect x="260" y="40" width="160" height="30" class="f-mut" stroke="currentColor"/>
<text x="270" y="60" fill="currentColor">임계구역 (IRQ 켜짐)</text>
<rect x="420" y="40" width="290" height="30" class="f-warn" stroke="currentColor"/>
<text x="430" y="60" fill="currentColor">인터럽트에 눌려 영원히 재개 못 함</text>
<text x="10" y="118" fill="currentColor">CPU0 (hrtimer)</text>
<line x1="330" y1="70" x2="330" y2="100" stroke="currentColor" stroke-dasharray="4 3"/>
<rect x="330" y="100" width="380" height="30" class="f-warn" stroke="currentColor"/>
<text x="340" y="120" fill="currentColor">spin_lock(&amp;sd.lock) → 무한 스핀</text>
<text x="10" y="170" fill="currentColor" font-weight="bold">spin_lock_irqsave 로 고치면</text>
<text x="10" y="208" fill="currentColor">CPU0 (프로세스)</text>
<rect x="130" y="190" width="170" height="30" class="f-ok" stroke="currentColor"/>
<text x="140" y="210" fill="currentColor">lock + IRQ off, 임계구역</text>
<rect x="300" y="190" width="150" height="30" class="f-mut" stroke="currentColor"/>
<text x="310" y="210" fill="currentColor">unlock + IRQ 복원</text>
<rect x="450" y="190" width="260" height="30" class="f-ok" stroke="currentColor"/>
<text x="460" y="210" fill="currentColor">대기하던 타이머 IRQ가 이제 실행</text>
<text x="10" y="258" fill="currentColor" font-size="12">다른 CPU의 IRQ/스레드는 그냥 스핀하며 기다린다: 락 쥔 CPU는 진행 중이므로 문제없다.</text>
</svg>
```

콜백 쪽에서 `irqsave` 를 그대로 쓰는 것도, `spin_lock` 만 쓰는 것도 맞다 (콜백 안에서는 이미 인터럽트가 꺼져 있다). 이 코드는 단순성을 위해 양쪽 다 같은 헬퍼를 쓴다. 나누는 변형은 추가 연습 P3이다.

### A2 — atomic

`atomic_long_inc` 한 줄이다. 공유 상태가 **단일 워드 하나**면 락이 없어도 된다.

### 결과 — 표

`check.sh solution` 과 `check.sh demo nolock|atomic_list` (4스레드 × 20000 + 500 µs 타이머, 다른 작성자와 VM을 공유해서 `uptime` load average 1–7 사이에서 실측). 각 행은 한 번의 실행이고 lost는 `expected − counter`.

| 모드 | 타이머 | 리스트 | counter | list_len | expected | lost | list_lost | dmesg |
|---|---|---|---:|---:|---:|---:|---:|---|
| `none` | 500 µs | on | 75558 | 74315 | 80144 | **4586** | **5829** | 경고 0 |
| `none` | 500 µs | on | 75219 | 74145 | 80140 | **4921** | **5995** | 경고 0 |
| `none` | 없음 | on | 75917 | 74565 | 80000 | **4083** | **5435** | 경고 0 |
| `mutex` | 없음 | on | 80000 | 80000 | 80000 | 0 | 0 | 경고 0 |
| `irqsave` | 500 µs | on | 80562 | 80562 | 80562 | 0 | 0 | 경고 0 |
| `atomic` | 500 µs | off | 80134 | — | 80134 | 0 | — | 경고 0 |
| `atomic` | 500 µs | **on** | 80146 | 73909 | 80146 | 0 | **6237** | 경고 0 |

`none` 의 세 번째 행: **타이머 없이 스레드끼리도 lost update**가 난다. `irqsave` 와 `atomic` 은 스트레스 3회 모두 정확했다(§5 끝 "실행 결과"). `atomic` 마지막 행이 Part C의 답이다. `none` 행의 dmesg가 깨끗한 것도 짚자: **락이 없으면 lockdep은 볼 것이 없다.** 데이터 레이스 자체는 KCSAN의 일이고(이 커널에는 없음), lockdep은 락의 순서와 컨텍스트만 본다.

### B1 — mutex를 하드 IRQ에서: `BUG: sleeping function called from invalid context`

`check.sh demo mutex_irq` 의 실제 출력(뒤는 "…"로 줄임):

```text
[    5.178513] BUG: sleeping function called from invalid context at kernel/locking/mutex.c:575
[    5.178593] in_atomic(): 1, irqs_disabled(): 1, non_block: 0, pid: 87, name: stress
[    5.178608] preempt_count: 10000, expected: 0
[    5.178619] RCU nest depth: 0, expected: 0
...
[    5.182393]  mutex_lock_nested+0x2c/0x40
[    5.182402]  sd_update+0x64/0x180 [solution]
[    5.182629]  sd_timer_fn+0x38/0xe0 [solution]
[    5.182655]  __hrtimer_run_queues+0xd4/0x3e8
[    5.182674]  hrtimer_interrupt+0xe8/0x250
[    5.182687]  arch_timer_handler_virt+0x3c/0x60
```

읽는 법: `in_atomic(): 1, irqs_disabled(): 1` 은 "지금 잠들면 안 되는 컨텍스트", `preempt_count: 10000` 의 `0x10000` 은 `HARDIRQ_OFFSET` (하드 IRQ 한 단계). 호출 스택 아래쪽이 `el1_interrupt` → `hrtimer_interrupt` → 우리 `sd_timer_fn` 이므로 **인터럽트가 끼어들어 실행된 코드가 잠들려 했다**는 뜻이다. `mutex_lock` 안의 `__might_sleep` 이 잡았다. 이 실행의 카운터는 `counter=4036 expected=4036` 으로 맞았지만 그게 안전하다는 뜻은 아니다. mutex가 다른 스레드에 잡혀 있을 때 IRQ가 들어오면 하드 IRQ 안에서 `schedule()` 로 들어가게 되고, 이 실행은 그 순간을 안 밟았을 뿐이다. 같은 실행에서 lockdep도 다른 각도로 지적했다.

```text
[    5.184909] [ BUG: Invalid wait context ]
[    5.185107] stress/87 is trying to lock:
[    5.185154] ffffce988cae9158 (&sd.mtx){....}-{4:4}, at: sd_update+0x64/0x180 [solution]
[    5.185405] context-{3:3}
```

`{4:4}` 는 mutex의 대기 타입(`LD_WAIT_SLEEP`), `context-{3:3}` 은 이 컨텍스트에서 허용되는 최대 대기 타입(`LD_WAIT_CONFIG`)이다 — `task_wait_context()` 와 `include/linux/lockdep_types.h` 의 enum을 읽고 확인한 내용. 고치는 길은 **IRQ에서는 일을 넘기기**: 하드 IRQ 경로는 `atomic` 으로 "할 일 개수"만 올리고, 실제 처리는 workqueue에서 mutex로 한다(추가 연습 P2, 비교 전체는 ELX-M06).

### B2 — plain spin_lock을 IRQ 경로와 같이: `inconsistent lock state`

`check.sh demo spin_irq`:

```text
[    9.416134] WARNING: inconsistent lock state
[    9.417505] inconsistent {IN-HARDIRQ-W} -> {HARDIRQ-ON-W} usage.
[    9.420483] stress/90 [HC0[0]:SC0[0]:HE1:SE1] takes:
[    9.420907] ffffaf4e2dd401a0 (&sd.lock){?...}-{3:3}, at: sd_update+0x12c/0x180 [solution]
[    9.423961] {IN-HARDIRQ-W} state was registered at:
[    9.424630]   _raw_spin_lock+0x58/0xb0
[    9.424714]   sd_update+0x12c/0x180 [solution]
[    9.424779]   sd_timer_fn+0x38/0xe0 [solution]
...
[    9.434525]        CPU0
[    9.434693]        ----
[    9.435263]   lock(&sd.lock);
[    9.435932]   <Interrupt>
[    9.435980]     lock(&sd.lock);
[    9.436036]
[    9.436036]  *** DEADLOCK ***
```

- `IN-HARDIRQ-W`: 이 락(클래스)은 하드 IRQ 안에서 쓰기로 잡힌 적이 있다. 등록 위치가 스택으로 나온다(`sd_timer_fn` → `hrtimer_interrupt`).
- `HARDIRQ-ON-W`: 그런데 지금 **IRQ가 켜진 채로** 잡았다(`HE1` = hardirqs enabled). 두 사실이 같은 클래스에 공존하면, "IRQ가 켜진 채로 락을 잡은 사이에 인터럽트가 와서 다시 그 락을 잡는다" = 위 그림의 교착.
- **핵심: 실제로 교착이 나지 않아도 잡힌다.** 이 실행(`spin_irq`)은 `RESULT counter=4048 expected=4048` 로 끝까지 돌았다. lockdep은 "두 사실이 동시에 있을 수 있다"는 그래프를 본다 (OS-M11의 "차례로 실행해도 잡는다"와 같은 원리).

그럼 진짜로 멈추는가? `spin_deadlock` 시나리오는 스레드를 CPU0에 고정해서 타이머와 같은 CPU에서 돌게 한다. 위와 같은 리포트 뒤 게스트가 **`RESULT`/`GUEST_DONE` 없이 멈추고** `timeout` 이 끊었다(`qemu exit: 124`). 멈춘 CPU는 RCU가 지적했다.

```text
[   30.838791] rcu: INFO: rcu_preempt detected stalls on CPUs/tasks:
[   30.839685] rcu: 	0-...0: (1 ticks this GP) idle=96fc/1/0x4000000000000000 softirq=144/144 fqs=1347
[   30.839906] rcu: 	(detected by 2, t=6502 jiffies, g=-811, q=3 ncpus=4)
[   30.840097] Sending NMI from CPU 2 to CPUs 0:
```

주의할 점: 이 로그에 `BUG: spinlock recursion` 줄은 **없다**. `CONFIG_DEBUG_SPINLOCK=y` 인데도 그렇다. `kernel/locking/spinlock_debug.c` 의 `spin_bug()` 는 `debug_locks_off()` 가 참일 때만 찍는데, lockdep이 첫 리포트에서 `debug_locks` 를 이미 꺼 버렸기 때문이다. 그래서 `debug_locks_off` 한 번으로 경고가 끝나는 것을 기억해 둔다: **lockdep은 첫 경고 뒤 침묵한다. 경고가 하나 있었으면 뒤의 "조용함"을 믿지 말라.** 시나리오를 부팅마다 나눈 이유다.

### B3 — ABBA: `possible circular locking dependency`

`check.sh demo abba` — **스레드 하나가 차례로** A→B, B→A 를 잡는다. 실제 교착은 불가능하다.

```text
[   20.150199] WARNING: possible circular locking dependency detected
[   20.150707] stress/87 is trying to acquire lock:
[   20.150801] ffffdd2eb8d941e8 (&sd.lock_a){+.+.}-{3:3}, at: sd_ioctl+0x548/0x5c8 [solution]
[   20.152461] ffffdd2eb8d94230 (&sd.lock_b){+.+.}-{3:3}, at: sd_ioctl+0x540/0x5c8 [solution]
[   20.152625] which lock already depends on the new lock.
[   20.152731] the existing dependency chain (in reverse order) is:
[   20.152845] -> #1 (&sd.lock_b){+.+.}-{3:3}:
[   20.153635]        sd_ioctl+0x244/0x5c8 [solution]
[   20.154296] -> #0 (&sd.lock_a){+.+.}-{3:3}:
[   20.154687]        sd_ioctl+0x548/0x5c8 [solution]
[   20.155560]        CPU0                    CPU1
[   20.155690]   lock(&sd.lock_b);
[   20.156102]                                lock(&sd.lock_a);
[   20.156506]                                lock(&sd.lock_b);
[   20.156704]   lock(&sd.lock_a);
[   20.156788]  *** DEADLOCK ***
```

그래프는 OS-M11에서 직접 만든 것과 같다. "`lock_a` 를 쥔 채 `lock_b` 를 잡음"이 간선 하나, 이번 `lock_b → lock_a` 가 사이클을 닫는다. `{+.+.}` 네 글자는 왼쪽부터 hardirq(쓰기·읽기), softirq(쓰기·읽기) 상태인데 `lockdep-design.rst` 표를 그대로 옮기면 이렇다: `+` = "그 IRQ 컨텍스트에서 잡힌 적은 없고 **그 IRQ가 켜진 채** 잡음", `.` = "그 컨텍스트 아님, 그 IRQ가 꺼진 채 잡음", `-` = "그 IRQ 컨텍스트에서 잡음", `?` = "그 IRQ 컨텍스트에서 잡히고 켜진 채로도 잡음". 앞서 `spin_irq` 의 `{?...}` 는 첫 글자 `?` — 하드 IRQ에서도, IRQ 켠 채로도 잡혔다는 뜻 — 이고 바로 그것이 `inconsistent` 의 내용이다.

**클래스 vs 인스턴스.** lockdep의 노드는 락 **변수**(인스턴스)가 아니라 **클래스**다. 클래스의 키는 보통 `spin_lock_init()` 을 부른 코드 위치마다 하나다. `solution.c` 의 `sd.inst[0]`, `sd.inst[1]` 은 한 줄의 `spin_lock_init` 로 초기화되어 한 클래스다. `check.sh demo recursive`:

```text
[    5.865506] WARNING: possible recursive locking detected
[    5.865849] stress/87 is trying to acquire lock:
[    5.865917] ffffad79eaf172c0 (&sd.inst[i]){+.+.}-{3:3}, at: sd_ioctl+0x578/0x5c8 [solution]
[    5.867390] ffffad79eaf17278 (&sd.inst[i]){+.+.}-{3:3}, at: sd_ioctl+0x570/0x5c8 [solution]
[    5.867992]   lock(&sd.inst[i]);
[    5.868062]  *** DEADLOCK ***
[    5.868140]  May be due to missing lock nesting notation
```

주소는 다른데(`...72c0` vs `...7278`) 이름은 `&sd.inst[i]` 로 같다. **다른 두 인스턴스를 같은 클래스로 보고 경고했다.** 진짜 버그일 수도 있지만, 같은 종류의 객체 둘을 잡는 코드(inode 2개 등)에서는 흔한 false positive다. `May be due to missing lock nesting notation` 이라고 커널이 힌트를 준다: `spin_lock_nested(b, SINGLE_DEPTH_NESTING)` 으로 주석을 달고, **주소 순서로 잡는 규칙**을 지키면 조용해진다 — 추가 연습 P5.

### B4 — 스핀락을 쥔 채 잠들기

`check.sh demo sleep_in_spin` (`spin_lock(); msleep(1); spin_unlock();`) 실제 출력:

```text
[    5.403844] BUG: scheduling while atomic: stress/87/0x00000002
[    5.403910] 1 lock held by stress/87:
...
[    5.411514] BUG: sleeping function called from invalid context at kernel/rseq.c:248
[    5.411603] in_atomic(): 1, irqs_disabled(): 0, non_block: 0, pid: 87, name: stress
[    5.411628] preempt_count: ffffffff, expected: 0
...
[    5.434754] Kernel panic - not syncing: Aiee, killing interrupt handler!
```

스핀락은 쥐는 동안 선점을 막는데(`preempt_count` 증가), 그 상태로 `schedule()` 이 불렸다. `preempt_count: ffffffff` 처럼 카운트가 무너지고 게스트가 패닉까지 갔다. (패닉까지 가는 경로는 이 실행의 관찰이다. 항상 패닉한다는 뜻은 아니다.) 스핀락 안에서는 `msleep`, `mutex_lock`, `GFP_KERNEL` 할당, `copy_from_user`처럼 잠들 수 있는 호출을 하면 안 된다.

### C1 — atomic은 왜 리스트를 못 지키나

위 표 마지막 행: `atomic counter=80146 lost=0`, `list_len=73909 list_lost=6237`. 카운터는 한 번의 read-modify-write 이고, `atomic_long_inc` 는 그 RMW를 쪼개지 않는다. 그런데 `list_add` 는:

```c
next->prev = new;
new->next = next;
new->prev = prev;
WRITE_ONCE(prev->next, new);
```

**서로 다른 위치에 쓰기 4번**이다. 하나의 원자 명령으로 묶을 수 없다. 두 CPU가 같은 `next` 를 보고 끼어들면 뒤에 쓴 쪽이 앞의 삽입을 덮어써서 노드가 사라진다. 카운터(자기 값만 있으면 됨)와 달리 리스트는 **여러 필드의 불변식**(`head → node → head` 가 이어져 있음)을 지켜야 한다.

이 드라이버는 노드를 풀(pool)에서 꺼내고 런 동안 해제하지 않으며 walk를 제한(`SD_POOL_SIZE + 1`)했다. 그래서 잘못된 리스트 순회가 멈추지 않고 "길이가 짧다"로 끝난다. 실제 드라이버에서 같은 버그는 `list_del`·해제와 겹쳐 use-after-free나 무한 순회로 나타날 수 있다. 이 커널은 `CONFIG_DEBUG_LIST=n` 이라 `list_add corruption` 리포트도 없다. **lockdep은 이것도 못 본다** — 경고 0개인데 데이터는 틀렸다. 그래서 "경고 0"은 "정확함"의 증거가 아니며, 두 검사(카운트 + 리포트)가 다 필요하다.

### C2 — HRTIMER_MODE_REL_HARD 와 [ BUG: Invalid wait context ]

주 실행은 **일반 `HRTIMER_MODE_REL`** 이다. 이 커널(비 PREEMPT_RT)에서 콜백은 `in_hardirq()` 가 참인 하드 IRQ에서 돈다(`not_hardirq=0`). 타이머를 `500h` 로 바꾸면 `HRTIMER_MODE_REL_HARD` 로 시작하는데, 코드는 그대로인데 다음이 찍힌다. `check.sh demo irqsave_hard`:

```text
[   17.270953] [ BUG: Invalid wait context ]
[   17.271485] stress/86 is trying to lock:
[   17.271559] ffffc892de9071a0 (&sd.lock){....}-{3:3}, at: sd_update+0xd4/0x180 [solution]
[   17.272623] context-{2:2}
...
[   17.274982]  _raw_spin_lock_irqsave+0x68/0xd0
[   17.275103]  sd_update+0xd4/0x180 [solution]
[   17.275161]  sd_timer_fn+0x38/0xe0 [solution]
```

그래도 카운터는 `counter=80390 expected=80390` 으로 정확했다. 락은 정상 동작했고, **lockdep은 "이 코드가 PREEMPT_RT 커널에서는 틀리다"고 미리 말한 것이다**. `CONFIG_PROVE_RAW_LOCK_NESTING=y` 일 때 켜지는 검사다(Kconfig 설명: "ensure that the lock nesting rules for PREEMPT_RT enabled kernels are not violated"). 소스를 읽으면 차이가 보인다.

```c
/* include/linux/hrtimer.h */
 * HRTIMER_MODE_HARD		- Timer callback function will be executed in
 *				  hard irq context even on PREEMPT_RT.

/* kernel/locking/lockdep.c: task_wait_context() */
	if (lockdep_hardirq_context()) {
		if (curr->hardirq_threaded || curr->irq_config)
			return LD_WAIT_CONFIG;
		return LD_WAIT_SPIN;
	}
```

`lockdep_hrtimer_enter()` 는 `!is_hard` 인 타이머에서만 `irq_config = 1` 을 켠다. 즉:

| 콜백 종류 | lockdep의 컨텍스트 | `spinlock_t` |
|---|---|---|
| `HRTIMER_MODE_REL` 콜백 | `LD_WAIT_CONFIG` (3) | 허용 (RT에서는 스레드에서 돈다고 간주) |
| `request_irq()` 핸들러 (기본) | `LD_WAIT_CONFIG` (`lockdep_hardirq_threaded`) | 허용 (같은 이유) |
| `HRTIMER_MODE_REL_HARD` 콜백 | `LD_WAIT_SPIN` (2) | 금지 → `raw_spinlock_t` 만 |

PREEMPT_RT에서 `spinlock_t` 는 `rt_mutex` 기반의 **잠들 수 있는 락**이 되고, `_irqsave` 도 실제 인터럽트를 끄지 않는다(`Documentation/locking/locktypes.rst`). 그래서 진짜 하드 IRQ에서 돌도록 HARD로 표시한 콜백이 `spinlock_t` 를 잡으면 안 되고, `raw_spinlock_t` 만 허용된다. 고친 코드는 추가 연습 P1이고 lockdep 경고가 사라진다. 이번 커널(비 RT)에서는 실제 동작 문제는 없고 **규칙 위반만 미리 알려 준 것**이다. RT 동작 자체는 이 VM에서 돌리지 않았다(문서로만 확인, 참고자료).

**왜 irq_work가 아니라 hrtimer를 골랐나.** 두 가지 다 "하드 IRQ 컨텍스트에서 내 코드 실행"이 가능하다. `irq_work` 는 누군가가 `irq_work_queue()` 로 깨워 줘야 하므로 주기적으로 쏘려면 어차피 트리거(타이머)가 필요하다. hrtimer는 콜백이 자기 자신을 `hrtimer_forward_now` + `HRTIMER_RESTART` 로 다시 예약해서 **혼자 주기적으로 돌고**, 주기를 µs 단위로 정할 수 있으며, `irq_fires` 로 실행 횟수를 정확히 셀 수 있어서 기댓값을 만들기 좋다.

### lock_stat 숫자

`check.sh demo lockstat` — `irqsave` 모드에서 `/proc/lock_stat` 실측 (`uptime` load average 4.33, TCG):

```text
class name    con-bounces  contentions  waittime-min  waittime-max  waittime-total  waittime-avg  acq-bounces  acquisitions  holdtime-min  holdtime-max  holdtime-total  holdtime-avg
&sd.lock:          12588        13286          0.37      53382.40      6712289.31        505.22        45536        80520          0.37      13404.59       421502.16          5.23
```

`contentions/acquisitions = 13286/80520 ≈ 16%`. 시간 수치의 정수부는 마이크로초(문서: "integer part of the time values is in us")인데 TCG에서 vCPU가 쉬는 시간까지 포함되므로 `waittime-max` 같은 값은 믿지 않는다. **믿을 것은 "이 락이 얼마나 자주 경합하나" (contentions, acquisitions) 비율** 이다. 실제 드라이버에서 이 표는 "락 하나가 병목인가"를 정량화하는 첫 도구다.

### 실행 결과 (solution)

```text
PASS: irqsave helper holds sd.lock
PASS: irqsave helper disables IRQs
PASS: irqsave unlock restores IRQs
PASS: 1000 atomic touches == 1000
RESULT mode=irqsave timer_us=500 with_list=1 threads=4 k=20000 counter=80562 list_len=80562 irq_fires=562 expected=80562 lost=0 list_lost=0 not_hardirq=0 walk_overflow=0 pool_exhausted=0
PASS: irqsave counter exact
PASS: irqsave list length exact
PASS: irqsave IRQ path ran in hard IRQ
PASS: irqsave pool not exhausted
RESULT mode=atomic timer_us=500 with_list=0 threads=4 k=20000 counter=80134 list_len=0 irq_fires=134 expected=80134 lost=0 list_lost=0 not_hardirq=0 walk_overflow=0 pool_exhausted=0
PASS: atomic counter exact
PASS: atomic IRQ path ran in hard IRQ
PASS: atomic pool not exhausted
RESULT mode=mutex timer_us=0 with_list=1 threads=4 k=20000 counter=80000 list_len=80000 irq_fires=0 expected=80000 lost=0 list_lost=0 not_hardirq=0 walk_overflow=0 pool_exhausted=0
PASS: mutex counter exact
PASS: mutex list length exact
PASS: mutex pool not exhausted
DMESG_BAD=0
14/14 passed, dmesg bad lines: 0
RESULT: PASS (solution)
```

`check.sh` 의 판정은 PASS/FAIL 줄과 `DMESG_BAD` 만 본다. **시간은 판정에 쓰지 않는다.** `check.sh starter` 는 TODO 때문에 FAIL로 끝난다 (8/14 passed, exit 1).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

바뀐 줄은 세 함수의 본문뿐이다: `sd_irqsave_lock()`, `sd_irqsave_unlock()`, `sd_atomic_touch()`. 나머지(ioctl, 타이머, 리스트)는 `starter.c` 그대로다. `mode=none/mutex/spin` 은 **버그를 보여 주기 위해 일부러 남긴** 경로이므로 수정 대상이 아니다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| IRQ와 공유하는데 `spin_lock` 만 씀 | 같은 CPU에서 인터럽트가 락 쥔 프로세스 위에 끼어들어 교착. B2의 리포트, `spin_deadlock` 의 실제 멈춤 | `spin_lock_irqsave` (프로세스 경로) |
| IRQ 콜백에서 `mutex_lock` | 하드 IRQ는 잠들 수 없음. `BUG: sleeping function called` | 일을 workqueue로 넘기기 (P2) 또는 spinlock으로 전환 |
| `atomic_t` 로 바꾸고 리스트는 그대로 | 카운터만 정확. 리스트는 여러 번 쓰기 (`list_lost=6237`) | 리스트는 락으로, 또는 락 없는 구조는 DS-A01/DS-A03 |
| 스핀락 안에서 `copy_from_user`, `msleep`, `GFP_KERNEL` | 잠들 수 있는 호출. `BUG: scheduling while atomic` | 락 밖에서 복사/할당, 또는 `GFP_ATOMIC` |
| `spin_lock_irqsave` 의 `flags` 를 전역/공유 변수에 저장 | 호출마다 상태가 달라야 하는데 덮어씀 | 지역 변수 (`unsigned long flags;`) 로 스택에 |
| lockdep 경고 없음 = 안전하다고 판단 | lockdep은 락의 순서·컨텍스트만 본다. `none` · `atomic`+리스트 모두 경고 0개인데 데이터는 틀렸다 | 카운트 검증 + (데이터 레이스엔 KCSAN) |
| lockdep 경고가 한 번 나온 뒤 계속 돌림 | 첫 경고 뒤 `debug_locks_off()` 로 lockdep이 꺼진다 | 경고 하나 고치고 다시 부팅 |
| `HRTIMER_MODE_REL_HARD` 콜백에서 `spinlock_t` | PROVE_RAW_LOCK_NESTING이 Invalid wait context로 잡음 (RT에서 잠드는 락) | `raw_spinlock_t` (P1) |

## 8. 면접 Q&A

<details><summary>Q1. IRQ 컨텍스트에서 mutex를 쓰면 왜 안 되나요?</summary>

mutex는 경합 시 호출자를 **재운다**. 하드 IRQ에는 재울 태스크 컨텍스트가 없고, 깨워 줄 스케줄러 흐름도 없다(`in_atomic()`). 이 레슨에서 `mutex_lock` 안의 `__might_sleep` 이 `BUG: sleeping function called from invalid context at kernel/locking/mutex.c:575` 를 냈다. 경합이 없어 잠들지 않아도 **검사기는 경고한다**, 즉 규칙이 "실제로 잠들었느냐"가 아니라 "잠들 가능성이 있는 호출이냐"다.

**꼬리질문: 그럼 IRQ 핸들러에서 해야 할 일이 mutex가 필요하면?** 하드 IRQ에서는 최소한만 하고 나머지를 threaded IRQ (ELX-M05)나 workqueue (ELX-M06)로 미룬다. 추가 연습 P2가 그 예다: IRQ 경로는 `atomic_long_inc(&pending)` + `schedule_work`, 프로세스 컨텍스트의 worker가 mutex로 처리.
</details>

<details><summary>Q2. 왜 spin_lock 이 아니라 spin_lock_irqsave 인가요?</summary>

같은 CPU에서 락을 쥔 코드 위로 IRQ가 끼어들어 같은 락을 잡으면 무한 스핀한다. `irqsave` 는 그 CPU의 인터럽트를 끄고 잡는다. 호출 시점의 IRQ 상태를 저장했다가 복원하므로 **이미 IRQ가 꺼진 곳에서 불러도 안전**하다.

**꼬리질문: 그럼 인터럽트 핸들러 자체에서는?** 하드 IRQ 안에서는 이미 인터럽트가 꺼져 있으므로 plain `spin_lock` 이면 충분하다. 대칭으로 프로세스 경로에서는 `spin_lock_irq`(IRQ가 켜져 있음을 안다면)도 된다 (P3). **꼬리질문: `spin_lock_irq` 가 위험한 상황은?** 호출 전에 IRQ가 꺼져 있었는데 `spin_unlock_irq` 가 무조건 켜면 호출자의 상태를 깨뜨린다. 그래서 상태를 모르면 `irqsave` 다.
</details>

<details><summary>Q3. 싱글코어(UP)에서 spinlock은 어떻게 되나요?</summary>

`include/linux/spinlock_api_up.h` 를 보면 **비디버그 UP 커널에서는 스핀할 대상이 없으므로 실제 잠금이 없다**: `preempt_disable()` (`__LOCK`) 또는 그에 더해 IRQ 끄기(`_irqsave` 변형)만 하고, 락 변수는 쓰지 않는다. UP에서 위험한 끼어들기는 **선점과 인터럽트** 둘뿐이기 때문이다. 그래서 UP에서 빌드한 코드에서 `spin_lock` 을 빠뜨린 버그는 SMP에서야 드러나고, 반대로 `irqsave` 를 빠뜨리면 UP에서도 인터럽트 쪽 버그가 그대로 있다. 이 레슨은 SMP 게스트(`-smp 4`)에서 본 것이고 UP 커널은 돌리지 않았다.

**꼬리질문: Cortex-M 단일 코어 RTOS로 바꾸면?** 스핀 대신 인터럽트 마스킹(`PRIMASK`/`BASEPRI`)이 같은 역할이다 (RTOS-M 시리즈, C-M14).
</details>

<details><summary>Q4. PREEMPT_RT 에서 spinlock_t 는 어떻게 달라지나요?</summary>

`Documentation/locking/locktypes.rst` 기준: `spinlock_t` 가 rt_mutex 기반으로 바뀌어 **잠들 수 있는 락**이 되고, 선점을 막지 않으며, `_irqsave` 도 CPU의 IRQ를 실제로 끄지 않는다. 대부분의 인터럽트 핸들러와 softirq가 스레드에서 돌기 때문에 가능하다. 진짜로 스핀해야 하는 곳은 `raw_spinlock_t` 를 쓴다. 이 레슨에서 일반 커널로도 `CONFIG_PROVE_RAW_LOCK_NESTING=y` 덕분에 `[ BUG: Invalid wait context ]` 로 그 위반을 볼 수 있었다 (C2). RT 커널 동작 자체는 이 환경에서 돌려 보지 않았다.

**꼬리질문: 드라이버에서 raw_spinlock을 그냥 다 쓰면 안 되나요?** RT에서 임계구역이 선점·PI 없이 돌아서 지연이 나빠진다. 문서도 "real critical core code, low-level interrupt handling" 에서만 쓰라고 한다.
</details>

<details><summary>Q5. 스핀락을 쥔 채 잠들면 어떻게 되나요?</summary>

스핀락은 쥔 동안 선점을 막으므로 `schedule()` 을 하면 `BUG: scheduling while atomic` 이 나고, 선점 카운트가 무너져 시스템이 깨진다. 이 레슨의 데모(`spin_lock(); msleep(1);`)에서는 `BUG: scheduling while atomic: stress/87/0x00000002` 뒤 `preempt_count: ffffffff`, 결국 `Kernel panic - not syncing: Aiee, killing interrupt handler!` 까지 갔다. 또 잠든 사이 다른 CPU가 같은 락을 잡으려 스핀하면 긴 시간 CPU를 낭비한다.

**꼬리질문: 잠들 수 있는 함수인지 어떻게 아나요?** `might_sleep()` 이 들어 있는 경로를 `CONFIG_DEBUG_ATOMIC_SLEEP` 이 검사한다. 대표적으로 `mutex_lock`, `GFP_KERNEL` kmalloc, `copy_from/to_user`, `msleep`.
</details>

<details><summary>Q6. lockdep은 어떻게 실제 교착 없이 교착을 잡나요?</summary>

락마다 클래스를 두고, "이 클래스를 쥔 채 저 클래스를 잡았다" 간선과 "이 클래스를 IRQ 컨텍스트에서 잡았다/IRQ 켠 채 잡았다" 상태를 **모든 실행에서 누적**한다. 그래프에 사이클이 생기거나 양립 불가능한 상태가 공존하면 그 즉시 경고한다. 이번 `abba` 데모는 스레드 하나가 차례로 돌았는데 경고가 났다. 원리는 OS-M11에서 직접 구현한 mini lockdep과 같다.

**꼬리질문: 클래스와 인스턴스 차이는요?** 노드는 락 변수가 아니라 클래스(보통 초기화 위치마다 하나)다. 인스턴스가 만 개여도 그래프가 작게 유지되어 성능이 나온다. 대가는 같은 클래스의 인스턴스 둘을 잡을 때 `possible recursive locking` false positive가 나는 것이고, `spin_lock_nested` 로 주석을 단다.

**꼬리질문: lockdep의 한계는?** 실행된 경로만 본다, 락 없는 데이터 레이스는 못 본다(이번 `none` 모드), 첫 경고 뒤 꺼진다, 커널이 크게 느려진다.
</details>

<details><summary>Q7. atomic_t 와 spinlock은 언제 어느 쪽을 쓰나요?</summary>

공유 상태가 **단일 워드의 단일 RMW** 면 atomic이 맞다(통계 카운터, 플래그). 불변식이 두 필드 이상에 걸치면(리스트 포인터, "카운터와 리스트가 일치") 락이 필요하다. 이 레슨의 `atomic` + 리스트 실측: 카운터 lost 0, 리스트 lost 6237. 그리고 `atomic_long_inc` 는 값만 지킬 뿐 다른 메모리에 대한 순서를 약속하지 않는다 (C-A03).

**꼬리질문: 통계 카운터가 진짜로 hot하면?** per-CPU 카운터(`this_cpu_inc`, P4): 경합이 사라지고 읽을 때만 합산한다. 합산은 근사치지만 이 레슨 판정처럼 쓰기가 다 끝난 뒤 읽으면 정확하다.
</details>

<details><summary>Q8. lockdep 리포트를 받으면 어디부터 보나요?</summary>

1) 제목 (`inconsistent lock state` / `circular locking` / `recursive locking` / `Invalid wait context`). 2) 락 이름과 `{...}` 상태. `&sd.lock` 처럼 소스에 있는 이름이다. 3) `IN-HARDIRQ-W` 등이 등록된 스택(언제 그런 사용이 있었나). 4) 아래쪽의 `Possible unsafe locking scenario` 그림: 2열 CPU 다이어그램이 곧 결론. 5) 여러 개면 **첫 번째만** 믿는다.

**꼬리질문: dmesg에 lockdep이 안 나오면 코드가 맞는 건가요?** 아니다. 그 경로를 안 돌렸을 수도 있고(lockdep은 동적 도구), 첫 경고 뒤 꺼져 있을 수도 있다. 그래서 boot마다 시나리오를 분리했다.
</details>

## 9. 추가 연습

`practice.c` 에 다음 다섯 패턴을 모두 구현했다. `check.sh practice` 하나로 한 번에 검증한다.

**P1. raw_spinlock_t 로 진짜 하드 IRQ 콜백 지키기.** `HRTIMER_MODE_REL_HARD` 타이머로 돌려도 lockdep이 조용해지도록 바꿔라.

<details><summary>답</summary>

`raw_spinlock_t rlock` 을 두고 양쪽에서 `raw_spin_lock_irqsave(&sd.rlock, flags)`. C2의 `[ BUG: Invalid wait context ]` 가 사라진다. `check.sh practice` 의 `raw_irqsave 500h` 행이 `DMESG_BAD=0` 이다.
</details>

**P2. IRQ 경로를 workqueue로 미루고 mutex 유지.** 프로세스 경로의 mutex를 그대로 쓰면서 IRQ 경로에서 mutex를 안 부르도록 바꿔라.

<details><summary>답</summary>

하드 IRQ 경로는 `atomic_long_inc(&sd.pending); schedule_work(&sd.work);` 만 한다. `work_fn` 은 프로세스 컨텍스트에서 mutex를 잡고 `atomic_long_xchg(&pending, 0)` 만큼 `sd_touch()` 한다. `xchg` 이므로 work 항목이 합쳐져도(이미 pending이면 다시 안 들어감) 개수를 잃지 않는다. 타이머 정지 시 `flush_work` 로 남은 일을 끝낸다. 실측 `counter=80721 list_len=80721 expected=80721`.
</details>

**P3. spin_lock_irq + spin_lock 나누기.** 프로세스 경로와 하드 IRQ 경로에서 서로 다른 변형을 쓰고 lockdep이 조용한지 확인하라.

<details><summary>답</summary>

프로세스 경로(IRQ가 켜져 있음을 안다) `spin_lock_irq/spin_unlock_irq`, 하드 IRQ 경로 plain `spin_lock/spin_unlock`. `lockdep` 경고 없음, `counter=80525 list_len=80525`.
</details>

**P4. per-CPU 카운터.** 카운터만 있는 경로에서 락도 atomic도 없이 정확한 합을 내라.

<details><summary>답</summary>

`DEFINE_PER_CPU(unsigned long, sd_pcount)` 를 두고 양쪽에서 `this_cpu_inc`. per-CPU 연산은 자기 CPU의 인터럽트에 대해서도 원자적이다. 읽을 때는 `for_each_possible_cpu` 로 합한다(쓰기가 끝난 뒤 읽으므로 정확). `counter=80116 expected=80116`.
</details>

**P5. 같은 클래스 두 인스턴스 안전하게 잡기.** `recursive` 경고가 나지 않게 `sd.inst[]` 두 개를 호출 순서와 무관하게 잡아라.

<details><summary>답</summary>

주소 순서로 정렬 (`if (a > b) swap(a, b);`) 하고 `spin_lock(a); spin_lock_nested(b, SINGLE_DEPTH_NESTING);`. 호출 순서를 바꿔 불러도 잡는 순서는 같으므로 ABBA가 불가능하고, `SINGLE_DEPTH_NESTING` 으로 같은 클래스의 두 번째임을 lockdep에 알린다. `check.sh practice` 의 `nested` 행.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash -lc 'bash <레슨 디렉터리>/check.sh practice'
```

실측 (raw_irqsave 500h, wq_mutex, spin_irq, percpu, nested): `20/20 passed, dmesg bad lines: 0`, `RESULT: PASS (practice)`.

## 10. 핵심 정리

- 공유 상태의 **두 경로**를 먼저 적는다: 프로세스만? 프로세스 + IRQ? 여러 필드 불변식인가 단일 워드인가?
- 프로세스끼리만 → `mutex`. IRQ와 공유 → `spin_lock_irqsave` (프로세스) / `spin_lock` (IRQ 안).
- 하드 IRQ에서 mutex 금지(`sleeping function called`), 일은 workqueue/threaded IRQ로 넘긴다.
- 단일 워드 RMW → `atomic`. 리스트처럼 쓰기 여러 번인 구조는 atomic으로 못 지킨다 (`atomic` 실측 list_lost 6237).
- lockdep은 교착이 실제로 나기 전에 순서·컨텍스트 위반을 잡지만, 락 없는 race는 못 보고, 첫 경고 뒤 꺼진다.
- 노드는 인스턴스가 아니라 클래스: 같은 init 위치의 두 인스턴스는 `spin_lock_nested` + 주소 순서.
- `spinlock_t` 는 일반 커널에서 스핀, PREEMPT_RT에서는 잠드는 락. 진짜 하드 IRQ(`REL_HARD`)는 `raw_spinlock_t`.
- 검증은 두 개가 다 있어야 한다: **카운트 정확 + dmesg 깨끗.** 둘 중 하나만으로는 안 된다 (`atomic`+리스트, `none` 참조).

## 참고자료

- Linux 6.18.55 소스 (`/var/lab/src/linux-6.18.55`): `kernel/locking/lockdep.c`, `kernel/locking/spinlock_debug.c`, `include/linux/hrtimer.h`, `include/linux/lockdep_types.h`, `include/linux/spinlock_api_up.h`, `kernel/irq/handle.c`, `lib/Kconfig.debug` (실제로 읽고 확인)
- [Documentation/locking/lockdep-design.rst](https://www.kernel.org/doc/html/latest/locking/lockdep-design.html) — 클래스, `{+.+.}` 상태 문자
- [Documentation/locking/locktypes.rst](https://www.kernel.org/doc/html/latest/locking/locktypes.html) — spinlock_t / raw_spinlock_t, PREEMPT_RT 규칙
- [Documentation/locking/lockstat.rst](https://www.kernel.org/doc/html/latest/locking/lockstat.html) — `/proc/lock_stat` 필드
- [Documentation/locking/mutex-design.rst](https://www.kernel.org/doc/html/latest/locking/mutex-design.html)
- 레슨 교차 참조: C-M14 (ISR 공유 변수), C-A03 (atomic), OS-M07 (스핀락), OS-M11 (mini lockdep), RTOS-B10 (mutex vs 세마포어), ELX-M05 (threaded IRQ), ELX-M06 (bottom half 비교)
