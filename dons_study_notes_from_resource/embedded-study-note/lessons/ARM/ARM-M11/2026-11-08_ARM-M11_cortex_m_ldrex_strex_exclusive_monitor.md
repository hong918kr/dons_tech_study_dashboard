---
id: ARM-M11
time: 95분
updated: 2026-11-08
---
# ARM-M11 Cortex-M LDREX/STREX와 Exclusive Monitor

## 학습 목표

- Cortex-M3/M4 의 **로컬 배타적 모니터(local exclusive monitor)** 를 Open/Exclusive 두 상태로 설명하고, `LDREX`·`STREX`·`CLREX`·**예외 진입/복귀** 가 상태를 어떻게 바꾸는지 그림으로 그린다.
- Thumb-2 로 `atomic_inc`, strong `cas`(실패 경로 `CLREX`), `mutex_try_lock`/`unlock`(+`DMB`) 을 손으로 쓰고, `arm-none-eabi-objdump` 로 인코딩을 확인한다.
- QEMU `mps2-an385` + `gdb-multiarch` 로 **LDREX 와 STREX 사이에 멈춰 인터럽트를 pend** 해서 `STREX` 가 1 을 돌려주고 루프가 재시도하는 경로를 실제로 만든다.
- QEMU 가 모니터를 어떻게 흉내 내는지(주소 + **값 비교**, 예외 시 클리어) 소스와 실험으로 확인하고, **실리콘과 다른 점**(DMA/디버거 쓰기를 QEMU 는 잡고 로컬 모니터는 못 잡음)을 말한다.
- Cortex-M0(ARMv6-M) 에는 배타 명령이 없다는 것을 어셈블러 에러와 `__atomic_fetch_add_4` 링크 에러로 보이고, PRIMASK 임계 구역으로 대신한다.

## 1. 문제

"`LDREX` 와 `STREX` 사이에 인터럽트가 들어오면 어떻게 되죠?" — Cortex-M 펌웨어 면접의 단골 질문이고, 답의 핵심은 한 줄이다: **ARMv7-M 은 예외 진입과 복귀 때 로컬 모니터를 지운다 → `STREX` 가 실패(1) → 루프가 다시 읽고 다시 쓴다.** 이 레슨은 그 한 줄을 C 모델로 증명하고, 진짜 Cortex-M3(QEMU)에서 gdb 로 그 순간을 강제로 만들어 눈으로 본다.

C-A03 은 C11 `atomic_fetch_add` 가 M4 에서 `ldrex/strex/cmp/bne` 로 내려가는 것을 **읽기만** 했다. C-M14 Q5 는 "예외가 모니터를 지운다" 를 **말로만** 했다. 여기서는 한 층 더 내려가 그 루프를 직접 쓰고, 모니터 규칙을 직접 구현하고, 규칙이 없으면 무엇이 깨지는지(lost update)를 결정적으로 재현한다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">로컬 배타적 모니터 (코어마다 하나) — ARMv7-M A3.4</text>
  <rect x="60" y="90" width="170" height="70" rx="10" class="f-ok" stroke="currentColor"/>
  <text x="105" y="120" fill="currentColor" font-weight="bold">Open Access</text>
  <text x="80" y="142" fill="currentColor">표시된 주소 없음</text>
  <rect x="480" y="90" width="200" height="70" rx="10" class="f-acc" stroke="currentColor"/>
  <text x="515" y="120" fill="currentColor" font-weight="bold">Exclusive Access</text>
  <text x="510" y="142" fill="currentColor">주소 x 를 표시해 둠</text>
  <path d="M230 105 L480 105" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M470 99 L480 105 L470 111" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="290" y="97" fill="currentColor" font-family="monospace">LDREX x  (값 읽기 + 표시)</text>
  <path d="M480 148 L230 148" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M240 142 L230 148 L240 154" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="250" y="172" fill="currentColor" font-family="monospace">STREX x → 쓰고 0 (성공)</text>
  <text x="250" y="190" fill="currentColor" font-family="monospace">CLREX → Open</text>
  <text x="250" y="208" fill="currentColor" font-weight="bold">예외 진입 / 예외 복귀 → Open</text>
  <path d="M120 90 C 100 40, 190 40, 170 90" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M166 80 L170 90 L176 81" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="70" y="45" fill="currentColor" font-family="monospace">STREX → 안 쓰고 1 (실패)</text>
  <path d="M540 90 C 520 40, 640 40, 620 90" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M616 80 L620 90 L626 81" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="520" y="45" fill="currentColor" font-family="monospace">LDREX y → 표시를 y 로</text>
  <rect x="10" y="230" width="720" height="60" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="252" fill="currentColor">STREX 는 끝나면 성공·실패와 관계없이 Open 으로 돌아간다. 그래서 ISR 이 자기 LDREX/STREX 를 쓰든,</text>
  <text x="20" y="272" fill="currentColor">카운터를 아예 안 건드리든, "사이에 예외가 있었다" 는 사실만으로 main 의 STREX 는 실패한다 (gdb R1·R2).</text>
</svg>
```

| 펌웨어 상황 | 이 레슨의 대응 |
|---|---|
| ISR 과 main 이 같은 통계 카운터(`rx_bytes`, `err_cnt`)를 올림 | `atomic_inc` — 인터럽트를 끄지 않아 지연이 늘지 않는다 |
| 상태 플래그·소유권 교환, 워터마크 | strong `cas` (실패 시 `CLREX`) |
| RTOS 없는 드라이버의 설정 락, ISR 은 **절대 기다리면 안 됨** | `try_lock` + deferred 카운터 (Part C) |
| Cortex-M0/M0+ (STM32F0/G0/L0, nRF51, RP2040) | 배타 명령 없음 → PRIMASK 임계 구역 (연습 3, `demo_m0_primask.c`) |

### Part A — 로컬 모니터 모델 (호스트 C)

맥에서 결정적으로 돌리기 위해 CPU 를 모델로 만든다. `cpu_t` 에 모니터 상태(`excl`, `tag`, `tag_val`)가 있고, 정책이 3가지다:

- `MON_V7M` — ARMv7-M 규칙: 예외 진입·복귀가 모니터를 Open 으로.
- `MON_NOCLEAR` — 예외가 모니터를 건드리지 않는 가상의 코어 (ARMv7-A 처럼 **OS 가 `CLREX` 해야** 하는 모델).
- `MON_QEMU` — QEMU TCG 방식: `STREX` 는 표시 주소 **그리고 LDREX 때 읽은 값** 이 같을 때만 성공, 예외 진입·복귀 시 클리어.

구현할 것: `mon_ldrex`, `mon_strex`, `mon_clrex`, `exc_entry`, `exc_exit`.

### Part B — 세 프리미티브 + "k 번째 명령에서 인터럽트" 주입기

모델 명령(`LDREX/STREX/CLREX/DMB/LDR/STR/OP`)은 실행 직전에 `tick()` 을 부르고, 주입 목록의 번호와 같으면 그 명령 **직전에** ISR(예외) 또는 외부 마스터 쓰기(DMA/디버거)를 끼워 넣는다. 이 위에서:

- `atomic_inc(c, p)` — 순서 `LDREX, OP, STREX`, 실패면 처음부터. 새 값 반환.
- `cas(c, p, expected, desired)` — strong. 값이 다르면 `CLREX` 후 옛 값 반환, 값이 같은데 `STREX` 실패면 재시도.
- `try_lock(c, lock)` / `unlock(c, lock)` — 성공 후 `DMB`(acquire), 해제 전 `DMB`(release).
- 테스트는 k = 0..4 의 **모든** 주입 위치에 대해 최종 값, `STREX` 실패 횟수, 반환값을 확인한다. 정책을 바꿔 "예외가 모니터를 안 지우면 평범한 ISR 이 증가를 잃는다" 도 CHECK 로 고정한다.

### Part C — 센서 드라이버: ISR 은 기다리지 않는다

main 은 설정 두 필드(`gain`, `offset = gain*10`)를 락 안에서 바꾼다. 샘플 준비 ISR 은 그 설정으로 샘플을 처리하는데, **main 이 락을 쥔 채 선점된 상태라면 ISR 이 기다려 봐야 main 은 영원히 돌아오지 않는다** (단일 코어). 그래서:

- `drv_isr(c)` — `try_lock` 성공이면 처리 후 `unlock`, 실패면 `atomic_inc(&deferred)` 만 하고 끝.
- `drv_set_gain(c, gain)` — 락, `gain`, `offset` 쓰기, `unlock`, 그다음 `deferred` 를 `cas` 로 0 으로 가져와 그만큼 처리. **deferred 가 0 이 될 때까지 반복**.
- 검증: ISR 하나를 k = 0..39 모든 위치에, ISR 둘을 모든 (k1 &lt; k2) 쌍에 넣어도 찢어진 설정을 본 샘플 0개, 처리 수 == ISR 수, 끝나면 `deferred == 0`, `lock == 0`. 대조군(락을 무시하는 ISR)은 정확히 한 위치(`gain` 과 `offset` 사이)에서 찢어진 설정을 본다.

### 실기 — 진짜 Thumb-2 를 QEMU 에서 (demo 파일, `ENV: qemu`)

- `demo_m3_prims.S` — 같은 세 프리미티브를 손으로 쓴 Thumb-2. `inc_after_ldrex` / `inc_after_strex` 전역 라벨로 gdb 가 정확히 그 사이에 멈출 수 있게.
- `demo_m3_main.c` + `demo_m3.ld` — 벡터 테이블, 리셋 핸들러, 세미호스팅 출력, `SYS_EXIT` 종료. 단위 테스트 → SysTick 스트레스 → gdb 시나리오 5 라운드.
- `demo_gdb_isr.gdb` — LDREX 직후 멈춤 → `SCB->ICSR` PENDSVSET 또는 `NVIC->ISPR0` 쓰기 → continue → ISR → `STREX` 결과 r12 확인.
- `demo_m0_primask.c` + `demo_m0.ld` — Cortex-M0(`-M microbit`) 에서 PRIMASK 판. `run_qemu.sh` 에 모든 명령.

## 2. Problem (English)

> **Exclusive-monitor atomics on Cortex-M (ARMv7-M).**
>
> 1. Model a single core's local exclusive monitor with three policies: ARMv7-M (exception entry and return clear the monitor), NOCLEAR (exceptions leave it alone; software must `CLREX`), and QEMU (store-exclusive succeeds only if the address matches *and* memory still holds the value loaded by `LDREX`). Implement `mon_ldrex`, `mon_strex` (returns 0 on success, 1 on failure; the monitor is Open afterwards either way), `mon_clrex`, `exc_entry`, `exc_exit`.
> 2. On top of model instructions that can be preempted before any instruction *k*, implement `uint32_t atomic_inc(cpu_t*, uint32_t *p)`, a strong `uint32_t cas(cpu_t*, uint32_t *p, uint32_t expected, uint32_t desired)` that clears the monitor on mismatch, and `bool try_lock(cpu_t*, uint32_t *lock)` / `void unlock(cpu_t*, uint32_t *lock)` with the barriers in the right places. For every *k*, the final count must be exact and `STREX` must fail exactly when an exception lands between `LDREX` and `STREX`.
> 3. A sensor driver: `drv_set_gain()` updates two config fields under the lock; `drv_isr()` must never wait for the lock — it defers work through an atomic counter that main drains. With one or two interrupts at any instruction, no sample may see a torn config and no deferred sample may be lost.
> 4. On real hardware (QEMU `mps2-an385`, Cortex-M3): write the same primitives in Thumb-2, break between `LDREX` and `STREX` with gdb, pend PendSV / an NVIC IRQ, and show the `STREX` status register going to 1 and the loop retrying.
>
> Follow-ups: Why doesn't a Cortex-M context switch need `CLREX` while a Cortex-A one does? What happens if a DMA engine writes the variable between `LDREX` and `STREX`? How does QEMU decide whether `STREX` succeeds? What do you do on a Cortex-M0? Why must an ISR never spin on a lock held by thread code?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력):

```text
FAIL starter.c:210  mon_ldrex(&c, &x) == 5u && c.excl && c.tag == &x
FAIL starter.c:211  mon_strex(&c, &x, 6u) == 0u && x == 6u && !c.excl
FAIL starter.c:212  mon_strex(&c, &x, 7u) == 1u && x == 6u
FAIL starter.c:217  !c.excl && mon_strex(&c, &x, 8u) == 1u && x == 6u && c.n_clrex == 1u
FAIL starter.c:224  c.excl && mon_strex(&c, &x, 10u) == 0u && x == 10u
FAIL starter.c:231  !c.excl && c.n_ldrex == 2u && c.n_strex_ok == 0u && c.n_strex_fail == 1u
SKIP Part B/C: Part A (monitor) checks failed
3/34 passed
```

하네스 설계: **Part A 가 전부 통과해야 Part B/C 가 돈다.** 모니터가 틀린 채로(`STREX` 가 늘 1) `atomic_inc` 루프를 돌리면 끝나지 않기 때문이다. Part A 만 채운 상태에서는 Part B/C 의 FAIL 20줄과 `SKIP 3 sweep checks (Part C)` 가 나오고 멈추지 않는다 (직접 확인함). Part C 의 스윕도 Part C 단위 테스트가 통과해야 돈다.

QEMU 실습 (Lima VM `elinux`, Mac 홈은 VM 에서 읽기 전용이라 스크립트가 `/tmp/ARM-M11_work` 로 복사해 빌드):

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ARM/ARM-M11
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh run"    # 단위 테스트 + 스트레스 + 시나리오(주입 없음)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh run1"   # -accel tcg,one-insn-per-tb=on (IRQ 가 명령 사이 어디든)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh gdb"    # -s -S + gdb-multiarch -batch -x demo_gdb_isr.gdb
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh m0"     # Cortex-M0: 어셈블 에러, 링크 에러, PRIMASK 판 실행
```

<!-- code: run_qemu.sh -->

## 4. 힌트

<details>
<summary>힌트 1 — STREX 는 "끝나면 항상 Open"</summary>

`mon_strex` 의 성공 조건은 `excl && tag == p` (QEMU 정책은 여기에 `*p == tag_val` 추가). 성공이면 쓰고 0, 실패면 쓰지 않고 1. 그리고 **어느 쪽이든** `excl = false`. 이게 없으면 한 번 성공한 뒤 LDREX 없이 또 STREX 해도 성공해 버린다 (Part A 세 번째 CHECK).

</details>

<details>
<summary>힌트 2 — 예외 클리어는 정책 하나로</summary>

`exc_entry` / `exc_exit` 는 `policy != MON_NOCLEAR` 일 때만 `excl = false`. 하네스의 `take_exception()` 이 `exc_entry → ISR → exc_exit` 순으로 부른다. 모니터를 지우는 쪽이 "ISR 이 무엇을 했는가" 가 아니라 **예외 자체** 라는 점이 핵심이다.

</details>

<details>
<summary>힌트 3 — 루프는 LDREX 부터 다시</summary>

`STREX` 가 실패하면 값을 **다시 읽어야** 한다. 실패 원인이 ISR 의 증가라면 옛 값 + 1 은 틀린 값이다. `cas` 는 "값이 다름" (진짜 실패 → `CLREX` 하고 반환) 과 "값은 같은데 모니터를 잃음" (가짜 실패 → 재시도) 을 구분한다. `try_lock` 도 같다: 잠겨 있으면 `CLREX` 하고 false, 풀려 있었는데 `STREX` 실패면 다시 확인.

</details>

<details>
<summary>힌트 4 — deferred 는 "다 가져올 때까지"</summary>

main 이 deferred 샘플을 처리하려고 다시 락을 잡은 사이에 ISR 이 또 오면, 그 ISR 은 또 deferred 를 올린다. 한 번만 가져오고 끝내면 그 마지막 하나가 남는다 (스윕 2 가 잡는다). `for (;;) { n = take_all(); if (!n) break; ... }`.

</details>

## 5. 풀이

### 0. 아키텍처 규칙 — 어디에 쓰여 있나

| 출처 | 내용 |
|---|---|
| ARMv7-M ARM (DDI 0403) A3.4 "Synchronization and semaphores" | 로컬 모니터(Non-shareable)와 글로벌 모니터(Shareable)의 상태기계, `LDREX/STREX/CLREX` 동작 |
| 같은 문서 A3.4.4 "Context switch support" | 아래 인용 |
| 같은 문서 B1.5 예외 진입/복귀 의사코드 | `ExceptionTaken()` 등에 `ClearExclusiveLocal()` 이 들어 있다고 알려져 있음 |

> "It is necessary to ensure that the local monitor is in the Open Access state after a context switch. In ARMv7-M, the local monitor is changed to Open Access automatically as part of an exception entry or exit sequence." — ARMv7-M ARM A3.4.4 Context switch support

**정직하게:** 이 문장은 FreeRTOS 버그 트래커 #132 에서 Richard Barry 가 Arm 문서를 인용한 것을 통해 확인했다. PDF 원문은 이 레슨에서 직접 열어 보지 못했다. 절 번호(A3.4.4)도 2차 출처 기준이다 (문서 기준, 미검증). 같은 스레드에서 FreeRTOS 는 PendSV 컨텍스트 스위치에 `CLREX` 를 넣자는 제안을 이 규칙을 근거로 기각했다. Linux 의 ARMv7-M 포트도 예외 복귀 경로에 `clrex` 를 넣지 않고 "하드웨어가 지운다" 는 주석만 둔다고 한다 (커밋 "ARM: ARMv7-M: Add support for exception handling" — 검색 결과 기준, 코드 직접 미확인).

**QEMU 는 어떻게 하나 — 소스를 직접 확인 (v10.2.1 태그, VM 의 `qemu-system-arm 10.2.1` 과 같은 버전):**

| 파일 · 함수 | 하는 일 |
|---|---|
| `target/arm/tcg/translate.c` `gen_store_exclusive()` | `if (env->exclusive_addr == addr && env->exclusive_val == [addr])` 이면 쓰고 Rd=0, 아니면 Rd=1. 실제로는 `tcg_gen_atomic_cmpxchg_i32` 로 **값을 비교**. 끝에서 `exclusive_addr = -1` |
| 같은 파일 `gen_clrex()` | `exclusive_addr = -1` |
| `target/arm/tcg/m_helper.c` `v7m_exception_taken()` | `arm_clear_exclusive(env);` (예외 진입) |
| 같은 파일, 예외 복귀 성공 경로 | `/* Otherwise, we have a successful exception exit. */ arm_clear_exclusive(env);` |
| `target/arm/internals.h` `arm_clear_exclusive()` | `env->exclusive_addr = -1;` "like the guest CLREX instruction" |

즉 QEMU 는 M-profile 에서 **예외 규칙은 아키텍처대로**, 모니터 자체는 "주소 + 값" 비교로 흉내 낸다. 진짜 로컬 모니터는 값을 보지 않고 "내가 LDREX 한 뒤 무슨 일이 있었나" 만 본다. 그래서 차이가 생기는 곳은 **예외가 끼지 않은 다른 마스터 쓰기**다 (아래 표, gdb R3·R4).

### Part A — 모니터 모델

```c
static uint32_t mon_strex(cpu_t *c, uint32_t *p, uint32_t v) {
    bool ok = c->excl && c->tag == p;
    if (ok && c->policy == MON_QEMU) ok = (*p == c->tag_val);   /* QEMU: 값까지 같아야 */
    if (ok) *p = v;
    c->excl = false;                                            /* 성공·실패 모두 Open */
    ...
}
```

모델에서 단순화한 점 두 가지 (아키텍처는 더 느슨하다):

- **다른 주소로 STREX** (`LDREX x` 후 `STREX y`) — 모델은 실패로 둔다. 아키텍처는 이런 짝이 안 맞는 사용을 보장하지 않는다 (A3.4.5 사용 제한, IMPLEMENTATION DEFINED/UNPREDICTABLE 계열 — 정확한 조문은 미검증). 실전 코드는 반드시 같은 주소로 짝을 맞춘다.
- **같은 코어의 일반 STR 이 표시 주소에 쓰면** — 모니터를 지울지 말지는 구현마다 다를 수 있다 (문서 기준, 미검증). 모델은 "안 지움" 으로 두었다. 그래서 Part B 의 B2 처럼 "ISR 이 평범한 `ldr/add/str` 로 같은 변수를 올리는" 경우를 막아 주는 것은 **오직 예외 클리어 규칙** 이다.
- **표시 단위(Exclusives Reservation Granule)** — 실제 하드웨어는 주소 하나가 아니라 일정 크기 블록을 표시한다 (A3.4.3, 크기는 구현 정의). 모델은 정확한 주소만 비교한다. 같은 블록의 다른 변수에 쓰면 실칩에서는 가짜 실패가 날 수 있다는 정도만 기억한다.

### Part B — 모든 k 에 ISR 을 넣어 본 결과 (CHECK 로 고정된 값)

`atomic_inc` 한 번의 명령 번호: 0=`LDREX`, 1=`OP`(add), 2=`STREX`, 재시도면 3..5. "k" 는 그 명령 **직전** 에 예외가 들어온다는 뜻이다. k 가 실행 길이보다 크면 `finish()` 가 main 이 끝난 뒤에 ISR 을 돌린다.

| 정책 · ISR | k=0 | k=1 | k=2 | k=3 | k=4 | 뜻 |
|---|---|---|---|---|---|---|
| V7M · ISR 도 `atomic_inc` — 최종값 | 2 | 2 | 2 | 2 | 2 | 항상 정확 |
| 〃 — main 의 STREX 실패 수 | 0 | **1** | **1** | 0 | 0 | 창(window) = LDREX 뒤 ~ STREX 전 |
| 〃 — main 반환값 | 2 | 2 | 2 | 1 | 1 | 재시도는 새 값을 다시 읽는다 |
| NOCLEAR · ISR 은 평범한 `ldr/add/str` | 2 | **1** | **1** | 2 | 2 | **lost update** — 클리어 규칙이 없으면 깨짐 |
| V7M · 평범한 ISR | 2 | 2 | 2 | 2 | 2 | 예외 클리어가 지켜 준다 |
| NOCLEAR · ISR 도 `atomic_inc` | 2 | 2 | 2 | 2 | 2 | ISR 의 STREX 성공이 모니터를 Open 으로 |

마지막 두 줄이 중요하다. 예외가 모니터를 지우지 않는 코어라도 **ISR 이 같은 변수에 LDREX/STREX 를 쓰면** ISR 의 `STREX` 가 모니터를 Open 으로 돌려놓으므로 main 이 실패한다. 하지만 "ISR 은 main 에게 선점당하지 않으니 평범한 `++` 로 충분" 이라고 쓴 ISR 은 NOCLEAR 에서 증가를 잃는다. ARMv7-M 은 이 경우까지 하드웨어가 막아 준다.

**ISR 이 카운터를 안 건드려도 실패한다** (B5): V7M·QEMU 정책 모두 k=1 에서 STREX 실패 1, NOCLEAR 는 0. main 입장에서는 **가짜 실패(spurious failure)** 다. C11 `atomic_compare_exchange_weak` 가 가짜 실패를 허용하는 이유가 바로 이것이다 (C11 7.17.7.4, C-A03 참고).

**다른 마스터의 쓰기** (B6, `INJ_EXT` — 예외가 아니다):

| k=1 에 +100 쓰기 | V7M (실칩 로컬 모니터 모델) | QEMU |
|---|---|---|
| STREX | 성공 (0) | 실패 (1) → 재시도 |
| 최종값 | **1 — +100 유실** | 101 |
| 같은 값 다시 쓰기(+0) | 성공 | 성공 (값 비교라 못 알아챔) |

실칩에서 이게 왜 중요한가: Cortex-M3/M4 단일 코어 MCU 에서 **DMA 나 다른 버스 마스터가 쓰는 변수에 LDREX/STREX 를 써도 원자적이지 않다.** 로컬 모니터는 자기 코어의 배타 접근만 추적한다. 다른 마스터와의 배타는 그 메모리 영역에 **글로벌 모니터** 가 있어야 하고(Shareable 영역, 버스의 exclusive 응답), 대부분의 단일 코어 MCU SRAM 에는 없다 (문서 기준 일반론 — 칩별 레퍼런스 매뉴얼 확인, SOCX-B03). QEMU 에서는 그 버그가 **숨겨진다**: 값 비교 덕분에 DMA 쓰기를 "잡아 주기" 때문이다.

**CAS 와 try_lock** (B7–B9): 값이 다르면 `CLREX` 가 한 번 실행되고 모니터는 Open (`n_clrex == 1`, `!excl`). k=1 에 ISR 이 0→1 로 바꾸면 `cas(0→5)` 는 STREX 실패 → 다시 LDREX 해서 1 을 보고 → 불일치 → `CLREX` → 1 반환. ISR 의 증가를 덮어쓰지 않는다. `try_lock` 의 명령 기록은 `XOYD` (LDREX, cmp, STREX, DMB), `unlock` 은 `DS` (DMB, STR). k=2 에 ISR 이 같은 락을 잡았다 놓으면 main 의 STREX 가 실패하고, 재시도에서 락을 얻는다.

**실패 경로에 CLREX 가 꼭 필요한가?** 이 모델과 단일 코어 M-profile 에서는 다음 `LDREX` 가 어차피 표시를 다시 하고, 예외가 오면 지워지므로 정확성 문제는 드물다. 그래도 넣는 이유: ① 열린 Exclusive 상태가 남아 있으면, **LDREX 없이** 이어지는 엉뚱한 `STREX`(짝이 안 맞는 코드, 다른 함수)가 성공할 수 있다. ② 글로벌 모니터가 있는 멀티코어 시스템에서는 표시가 남아 있는 것 자체가 다른 코어에 영향을 줄 수 있다 (구현 의존). ③ 컴파일러가 C11 CAS 실패 경로에 그대로 넣는다 (C-A03 `demo_asm.c` 실측: thumbv7em 의 CAS weak 불일치 경로에 `clrex`). 비용은 명령 하나다.

### Part C — 드라이버

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">ISR 이 main 의 락을 만나면: 기다리지 말고 deferred++ (main 이 unlock 뒤에 처리)</text>
  <text x="10" y="62" fill="currentColor">main</text>
  <rect x="60" y="45" width="90" height="28" class="f-acc" stroke="currentColor"/>
  <text x="68" y="64" fill="currentColor" font-family="monospace">try_lock</text>
  <rect x="155" y="45" width="85" height="28" class="f-mut" stroke="currentColor"/>
  <text x="162" y="64" fill="currentColor" font-family="monospace">gain=7</text>
  <rect x="330" y="45" width="100" height="28" class="f-mut" stroke="currentColor"/>
  <text x="336" y="64" fill="currentColor" font-family="monospace">offset=70</text>
  <rect x="435" y="45" width="75" height="28" class="f-acc" stroke="currentColor"/>
  <text x="443" y="64" fill="currentColor" font-family="monospace">unlock</text>
  <rect x="515" y="45" width="215" height="28" class="f-ok" stroke="currentColor"/>
  <text x="522" y="64" fill="currentColor" font-family="monospace">drain: cas(deferred→0)</text>
  <path d="M245 75 L245 120" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M239 110 L245 120 L251 110" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="252" y="100" fill="currentColor">k=5 IRQ</text>
  <text x="10" y="142" fill="currentColor">ISR</text>
  <rect x="160" y="125" width="90" height="28" class="f-acc" stroke="currentColor"/>
  <text x="166" y="144" fill="currentColor" font-family="monospace">try_lock</text>
  <rect x="255" y="125" width="70" height="28" class="f-warn" stroke="currentColor"/>
  <text x="262" y="144" fill="currentColor" font-family="monospace">false</text>
  <rect x="160" y="160" width="165" height="28" class="f-ok" stroke="currentColor"/>
  <text x="166" y="179" fill="currentColor" font-family="monospace">atomic_inc(deferred)</text>
  <path d="M330 175 L330 75" stroke="currentColor" fill="none" stroke-width="2" stroke-dasharray="4 3"/>
  <path d="M324 85 L330 75 L336 85" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="340" y="120" fill="currentColor">복귀 → main 계속</text>
  <text x="10" y="220" fill="currentColor">락을 무시하는 ISR 은 k=5 에서 gain=7, offset=10 을 본다 (찢어진 설정, 스윕 중 1곳).</text>
  <text x="10" y="242" fill="currentColor">ISR 이 while(!try_lock) 로 기다리면: main 은 ISR 이 끝나야 돌아오므로 영원히 대기 (단일 코어 교착).</text>
</svg>
```

`drv_set_gain` 의 명령 번호: 0–3 `try_lock`(X O Y D), 4 `STR gain`, 5 `STR offset`, 6–7 `unlock`(D S), 8 `LDR deferred`, … deferred 가 있으면 `cas` 와 처리 루프가 이어진다. 결과 (CHECK):

- 주입 없음(k=99, main 이 끝난 뒤 ISR) / k=5 (락 보유 중 → deferred 경로) 단위 테스트 통과.
- 스윕 1: ISR 하나 k = 0..39 → 40가지 모두 `torn == 0`, `processed == isr_calls == 1`, `deferred == 0`, `lock == 0`, `gain == 7`, `offset == 70`.
- 스윕 2: ISR 둘 (k1 &lt; k2 &lt; 40) → 780가지 모두 같은 불변식. 여기에 "drain 중 다시 락을 잡은 사이에 두 번째 ISR" 이 포함되므로, drain 을 한 번만 돌면 실패한다.
- 대조: 락 무시 ISR → 40가지 중 **정확히 1가지(k=5)** 가 찢어진 설정.

펌웨어 현실: 이 패턴은 "ISR 에서 락 대신 플래그/카운터로 일을 미루고 main(또는 태스크)이 처리" 하는 bottom-half 구조다. FreeRTOS 라면 ISR 에서 `xSemaphoreTakeFromISR` 은 기다리지 않고 실패를 돌려주고, 일은 `xTaskNotifyFromISR` 로 태스크에 넘긴다 (RTOS 트랙). 설정이 자주 바뀌지 않고 ISR 이 읽기만 한다면 seqlock(C-M14 Part C) 도 답이다.

### 실측 1 — 손으로 쓴 Thumb-2 와 컴파일러 출력 (`arm-none-eabi-objdump -d m3.elf`)

<!-- code: demo_m3_prims.S -->

실제 인코딩 (발췌):

```text
000006f4 <m_atomic_inc>:
 6f4:	2300      	movs	r3, #0
 6f6:	3301      	adds	r3, #1
 6f8:	e850 2f00 	ldrex	r2, [r0]
000006fc <inc_after_ldrex>:
 6fc:	3201      	adds	r2, #1
 6fe:	e840 2c00 	strex	ip, r2, [r0]
00000702 <inc_after_strex>:
 702:	f1bc 0f00 	cmp.w	ip, #0
 706:	d1f6      	bne.n	6f6 <m_atomic_inc+0x2>
 ...
00000724 (m_cas 실패 경로):
 724:	f3bf 8f2f 	clrex
0000072c <m_mutex_try_lock>:
 72e:	e850 1f00 	ldrex	r1, [r0]
 732:	b939      	cbnz	r1, 744 <m_mutex_try_lock+0x18>
 734:	e840 2300 	strex	r3, r2, [r0]
 738:	2b00      	cmp	r3, #0
 73a:	d1f8      	bne.n	72e <m_mutex_try_lock+0x2>
 73c:	f3bf 8f5f 	dmb	sy
0000074c <m_mutex_unlock>:
 74c:	f3bf 8f5f 	dmb	sy
 750:	2100      	movs	r1, #0
 752:	6001      	str	r1, [r0, #0]
```

같은 펌웨어 안의 C11 판 (`arm-none-eabi-gcc 14.2 -mcpu=cortex-m3 -O2`):

```text
000002ac <c11_inc_relaxed>:                 000002c0 <c11_inc_seq_cst>:
 2ae:	e853 0f00 	ldrex	r0, [r3]         2c2:	f3bf 8f5b 	dmb	ish
 2b2:	3001      	adds	r0, #1           2c6:	e853 0f00 	ldrex	r0, [r3]
 2b4:	e843 0200 	strex	r2, r0, [r3]     2ca:	3001      	adds	r0, #1
 2b8:	2a00      	cmp	r2, #0           2cc:	e843 0200 	strex	r2, r0, [r3]
 2ba:	d1f8      	bne.n	2ae              2d0:	2a00      	cmp	r2, #0
 2bc:	4770      	bx	lr               2d2:	d1f8      	bne.n	2c6
                                              2d4:	f3bf 8f5b 	dmb	ish
```

읽는 법:

- `LDREX/STREX` 는 **32비트 Thumb-2 전용** 인코딩(`e85x`/`e84x`)이다. 16비트 판이 없다 → ARMv6-M 에는 존재할 수 없다 (실측 3).
- `STREX` 의 상태 레지스터(r12=`ip`)는 데이터 레지스터와 달라야 한다. 손 코드는 r12 를 써서 `cmp.w`(32비트)가 됐고, gcc 는 r2 를 골라 16비트 `cmp` 를 썼다.
- `cbnz` 는 **앞으로만** 분기한다. 그래서 재시도(뒤로)는 `cmp + bne`, 포기(앞으로)는 `cbnz`.
- relaxed 판은 손 코드와 같은 루프이고 배리어가 없다. seq_cst 판은 앞뒤에 `dmb ish` 가 붙는다. 손 코드의 `dmb` 는 어셈블러가 `dmb sy` 로 냈다. ARMv7-M 은 `DMB` 옵션으로 SY 만 정의하고 나머지 인코딩은 SY 처럼 실행된다고 알려져 있다 (문서 기준, 미검증). 그래서 M3 에서 둘의 차이는 사실상 없다.
- Cortex-M3/M4 단일 코어에서 이 `DMB` 들이 꼭 필요한가? M3/M4 파이프라인은 일반 메모리 접근 순서를 바꾸지 않는다. 그래서 같은 코어의 ISR 상대로는 생략해도 보통 동작한다는 것이 Arm 앱노트들의 설명이다 (대표 설명, 미검증). 하지만 **M7(쓰기 버퍼·캐시)·멀티코어·DMA 상대로는 필요** 하다. 아키텍처 계약도 배리어를 요구한다. 그러니 이식성을 위해 넣는다 (C-A02, ARM-M05).

### 실측 2 — gdb 로 "LDREX 와 STREX 사이 예외" 를 강제 (`run_qemu.sh gdb`)

<!-- code: demo_gdb_isr.gdb -->

브레이크 조건 `($xpsr & 0x1ff) == 0` 은 IPSR 0 = Thread 모드 = main 일 때만 멈추라는 뜻이다. ISR 안에서 부르는 같은 `m_atomic_inc` 는 그냥 지나간다. 실제 세션 (gdb-multiarch 17.1 + qemu-system-arm 10.2.1, 브레이크 줄 일부 생략):

```text
=== scenario_begin reached (stress done, SysTick off) ===
Breakpoint 2 at 0x6fc: file demo_m3_prims.S, line 23.
Breakpoint 3 at 0x702: file demo_m3_prims.S, line 27.
Breakpoint 4 at 0x26c: file demo_m3_main.c, line 72.
Breakpoint 5 at 0x21c: file demo_m3_main.c, line 73.

##### R1: pend PendSV (SCB->ICSR.PENDSVSET) between LDREX and STREX #####
Breakpoint 2, m_atomic_inc () at demo_m3_prims.S:23
23	        adds    r2, r2, #1
[main] after LDREX: r2(loaded)=200420  g_counter=200420  xpsr=0x01000000
[gdb ] ICSR=0x1000e800 (bit28 PENDSVSET written)
Breakpoint 4, PendSV_Handler () at demo_m3_main.c:72
[isr ] stopped in handler: IPSR=14
Breakpoint 3, m_atomic_inc () at demo_m3_prims.S:27
27	        cmp     r12, #0
[main] after STREX: r12(status)=1  g_counter=200421  r3(attempt)=1
Breakpoint 2, m_atomic_inc () at demo_m3_prims.S:23
[main] retry LDREX: r2(loaded)=200421  r3(attempt)=2
Breakpoint 3, m_atomic_inc () at demo_m3_prims.S:27
[main] after STREX: r12(status)=0  g_counter=200422

##### R2: pend IRQ30 (NVIC->ISPR0 bit30); ISR does NOT touch g_counter #####
[main] after LDREX: r2=200422 g_counter=200422
Breakpoint 5, IRQ30_Handler () at demo_m3_main.c:73
[isr ] stopped in handler: IPSR=46 (16+30=46), g_isr_mode=1
[main] after STREX: r12(status)=1  g_counter=200422 (unchanged by ISR)
[main] retry LDREX: r2=200422 r3(attempt)=2
[main] after STREX: r12(status)=0  g_counter=200423

##### R3: no exception; gdb (an 'external master') writes g_counter += 100 #####
[main] after LDREX: r2=200423 g_counter=200423
[gdb ] g_counter now 200523
[main] after STREX: r12(status)=1  g_counter=200523
[main] retry LDREX: r2=200523 r3(attempt)=2
[main] after STREX: r12(status)=0  g_counter=200524

##### R4: no exception; gdb writes the SAME value back #####
[main] after LDREX: r2=200524 g_counter=200524
[main] after STREX: r12(status)=0  g_counter=200525

##### R5: control — nothing injected #####
[main] after LDREX: r2=200525
[main] after STREX: r12(status)=0  g_counter=200526
[Inferior 1 (process 1) exited normally]
```

펌웨어가 세미호스팅으로 찍은 같은 라운드 요약:

```text
R1 PendSV, ISR incs counter : attempts=2 before=200420 after=200422 isr_hits+=1 ipsr_in_isr=14
R2 IRQ30,  ISR touches nothing: attempts=2 before=200422 after=200423 isr_hits+=1 ipsr_in_isr=46
R3 no exc, gdb writes +100   : attempts=2 before=200423 after=200524 isr_hits+=0 ipsr_in_isr=46
R4 no exc, gdb writes same   : attempts=1 before=200524 after=200525 isr_hits+=0 ipsr_in_isr=46
R5 nothing (control)         : attempts=1 before=200525 after=200526 isr_hits+=0 ipsr_in_isr=46
8/8 passed
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">R1 실측 타임라인 (QEMU Cortex-M3)</text>
  <path d="M20 70 L730 70" stroke="currentColor" fill="none"/>
  <text x="20" y="60" fill="currentColor">Thread</text>
  <rect x="70" y="76" width="80" height="26" class="f-acc" stroke="currentColor"/>
  <text x="78" y="94" fill="currentColor" font-family="monospace">LDREX</text>
  <text x="60" y="122" fill="currentColor">r2=200420</text>
  <path d="M160 40 L160 160" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="125" y="36" fill="currentColor">gdb: ICSR←PENDSVSET</text>
  <rect x="170" y="140" width="80" height="26" class="f-warn" stroke="currentColor"/>
  <text x="176" y="158" fill="currentColor">진입: Open</text>
  <rect x="255" y="140" width="150" height="26" class="f-mut" stroke="currentColor"/>
  <text x="260" y="158" fill="currentColor" font-family="monospace">PendSV: inc → 200421</text>
  <rect x="410" y="140" width="80" height="26" class="f-warn" stroke="currentColor"/>
  <text x="416" y="158" fill="currentColor">복귀: Open</text>
  <text x="270" y="190" fill="currentColor">Handler (IPSR=14)</text>
  <rect x="500" y="76" width="70" height="26" class="f-warn" stroke="currentColor"/>
  <text x="506" y="94" fill="currentColor" font-family="monospace">STREX</text>
  <text x="500" y="122" fill="currentColor">r12=1</text>
  <rect x="580" y="76" width="145" height="26" class="f-ok" stroke="currentColor"/>
  <text x="586" y="94" fill="currentColor" font-family="monospace">재시도 → 200422</text>
  <text x="580" y="122" fill="currentColor">r12=0, attempts=2</text>
  <text x="10" y="225" fill="currentColor">ISR 의 증가를 덮지 않았다: 옛 값+1(200421)을 쓰려던 STREX 가 실패, 다시 읽어 200422 를 썼다.</text>
  <text x="10" y="243" fill="currentColor">R2 는 ISR 이 아무것도 안 해도 똑같이 r12=1 — 모니터를 지운 것은 예외 자체다.</text>
</svg>
```

라운드별 해석:

| 라운드 | QEMU 결과 | QEMU 가 그렇게 한 이유 | 실칩 Cortex-M3 (아키텍처 기준) |
|---|---|---|---|
| R1 PendSV, ISR 이 같은 카운터 증가 | STREX 1 → 재시도 | 예외 진입·복귀 클리어 + ISR 자신의 STREX | 같음 (A3.4.4) |
| R2 IRQ30, ISR 이 아무것도 안 함 | STREX 1 → 재시도 | `v7m_exception_taken()` 의 `arm_clear_exclusive` | 같음 |
| R3 예외 없이 +100 쓰기 | STREX 1 → 재시도, 최종 +101 | 값 비교 (`cmpxchg` 불일치) | **STREX 0 예상 → +100 유실** (로컬 모니터는 디버거/DMA 쓰기를 못 봄 — 문서 기준 추론, 실칩 미검증) |
| R4 예외 없이 같은 값 쓰기 | STREX 0 | 값이 같으니 통과 | STREX 0 |
| R5 대조 | STREX 0 | — | STREX 0 |

덤으로 보인 것: **gdb 브레이크로 멈췄다가 continue 해도** (R4, R5) QEMU 의 모니터는 지워지지 않았다. 할트 디버그(halting debug)는 예외가 아니기 때문이다. 실칩에서도 할트 모드 스텝은 모니터를 지우지 않을 것으로 보지만, **DebugMonitor 예외로 스텝하는 방식**(모니터 모드 디버그)이라면 예외이므로 지워질 것이다. 그러면 LDREX~STREX 를 한 명령씩 스텝할 때마다 STREX 가 실패하는 현상이 생길 수 있다 (추론, 미검증).

### 실측 3 — 자연스러운 인터럽트 스트레스: QEMU 는 창을 거의 안 연다

`stress()` 는 main 이 20만 번 증가하는 동안 SysTick(RELOAD=400) ISR 도 같은 카운터를 올린다. 실제 출력 (각 3회):

| 모드 | 판 | isr_hits | lost | strex_retries |
|---|---|---|---|---|
| 기본 TCG (`run`) | LDREX/STREX | 333 / 336 / 309 | 0 | **0 / 0 / 0** |
| 〃 | ldr/add/str | 283 / 181 / 244 | **0 / 0 / 0** | — |
| `-accel tcg,one-insn-per-tb=on` (`run1`) | LDREX/STREX | 2197 / 2481 / 2534 | 0 | 236 / 255 / 290 |
| 〃 | ldr/add/str | 1075 / 1047 / 1125 | **219 / 243 / 245** | — |

기본 모드에서는 **틀린 `ldr/add/str` 도 한 번도 증가를 잃지 않았다.** QEMU TCG 는 명령을 묶음(translation block) 단위로 번역해 실행하고, 인터럽트는 그 묶음 경계에서 확인한다. `ldr…str` 은 한 묶음 안에 있으니 그 사이로 IRQ 가 들어올 기회가 거의 없다 (TCG 구조에 따른 설명 — 아래 one-insn-per-tb 결과와 일치). 명령 하나를 묶음 하나로 만들면 IRQ 가 아무 명령 사이에나 들어오고, 그제야 lost update(ISR 의 약 22%)와 STREX 재시도(ISR 의 약 11%)가 보인다. 두 비율은 "창이 루프 명령 중 몇 칸인가" 와 대략 맞는다: plain 은 `ldr` 뒤·`adds` 뒤 2칸 / 반복당 약 10명령, atomic 은 `ldrex` 뒤·`adds` 뒤 2칸 / 약 18명령 (objdump 기준: 호출 루프 8 + `m_atomic_inc` 10).

**교훈:** "QEMU 에서 스트레스가 통과했다" 는 원자성의 증거가 아니다. 이 레슨이 결정적 모델(k 스윕)과 gdb 강제 주입을 쓰는 이유다.

### 실측 4 — Cortex-M0 (ARMv6-M): 명령이 없다 (`run_qemu.sh m0`)

```text
--- (1) demo_m3_prims.S assembled for cortex-m0 (ARMv6-M) ---
demo_m3_prims.S:20: Error: selected processor does not support `ldrex r2,[r0]' in Thumb mode
demo_m3_prims.S:24: Error: selected processor does not support `strex r12,r2,[r0]' in Thumb mode
demo_m3_prims.S:27: Error: cannot honor width suffix -- `cmp r12,#0'
demo_m3_prims.S:48: Error: selected processor does not support `clrex' in Thumb mode
demo_m3_prims.S:60: Error: selected processor does not support `cbnz r1,2f' in Thumb mode
...
--- (2) C11 atomic_fetch_add on cortex-m0 without our __atomic_fetch_add_4 ---
/tmp/ARM-M11_work/demo_m0_primask.c:53:(.text+0x5a): undefined reference to `__atomic_fetch_add_4'
--- (3) with PRIMASK __atomic_fetch_add_4, run on -M microbit (Cortex-M0) ---
00000144 <__atomic_fetch_add_4>:
 146:	f3ef 8210 	mrs	r2, PRIMASK
 14a:	b672      	cpsid	i
 14c:	6800      	ldr	r0, [r0, #0]
 14e:	1841      	adds	r1, r0, r1
 150:	6019      	str	r1, [r3, #0]
 152:	f382 8810 	msr	PRIMASK, r2
 156:	4770      	bx	lr
ARM-M11 Cortex-M0 PRIMASK fallback, CPUID=0x410cc200
counter=100212 expected=100212 isr_hits=212  PASS
```

- (1) `.S` 에 `.cpu` 지시어를 일부러 두지 않았다 (처음엔 `.cpu cortex-m3` 이 있어서 `-mcpu=cortex-m0` 를 줘도 어셈블이 **성공** 해 버렸다 — 지시어가 명령행보다 우선). `ldrex/strex/clrex` 는 물론 `cbnz` 와 r12 비교(`cmp.w`)도 v6-M 에 없다. `dmb` 는 v6-M 에도 있어서 에러가 없다.
- (2) gcc 14.2 는 v6-M 에서 `atomic_fetch_add` 를 `bl __atomic_fetch_add_4` 로 내보낸다. arm-none-eabi 의 libgcc 에는 이 함수가 없고, `libatomic.a` 도 설치돼 있지 않았다 (`-print-file-name=libatomic.a` 가 경로 없이 이름만 돌려줌). 그래서 **누가 구현하나 → 보통 펌웨어 자신** 이다 (CPP-A04 에서 clang/compiler-rt 쪽과 `is_always_lock_free` 를 다룬다).
- (3) 직접 구현: PRIMASK 저장 → `cpsid i` → RMW → PRIMASK 복원. 중첩에 안전하다 (원래 꺼져 있었으면 꺼진 채로 돌아간다). QEMU `microbit` 의 CPUID `0x410cc200` = Arm, part `0xC20` = Cortex-M0 이다. 단, 이건 **단일 코어 전제** 다. RP2040(M0+ 두 개)처럼 코어가 둘이면 인터럽트를 꺼도 다른 코어는 막지 못한다 → SIO 하드웨어 스핀락이 필요하다 (C-A03).

### A-profile 과 M-profile: 컨텍스트 스위치의 CLREX

| | ARMv7-M (Cortex-M3/M4/M7) | ARMv7-A/R (Cortex-A9, R5 등) |
|---|---|---|
| 예외 진입/복귀가 로컬 모니터를 지우나 | **예** (A3.4.4) | 아니오 — OS 가 지워야 함 |
| 컨텍스트 스위치 코드 | PendSV 안에서 일어나므로 자동. FreeRTOS CM3/CM4 포트에 `CLREX` 없음 | 스위치/예외 복귀 경로에 `CLREX` (ARMv6 은 더미 `STREX`). Linux arm 의 예외 복귀 경로가 그 예 (기억 기준, 미확인) |
| 안 하면 | — | 연습 1: B 의 LDREX 가 남긴 표시를 A 의 LDREX 가 다시 세우고, B 가 돌아와 **오래된 값으로 STREX 성공** → 증가 하나 유실 |

ARMv8-A 의 예외 복귀(`ERET`)가 로컬 모니터를 지우는지는 DDI 0487 의 Exclusives 절을 확인해야 한다 (이 레슨에서 미검증). AArch64 쪽은 ARM-M04 참고.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 34/34 passed, exit 0 (5회 반복 동일)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 깨끗
```

호스트 모델은 단일 스레드라 TSan 대상이 아니다 (실행해도 보고 없음). QEMU 쪽 펌웨어 파일들:

<!-- code: demo_m3_main.c -->

<!-- code: demo_m0_primask.c -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| STREX 실패 후 **같은 값으로** 다시 STREX | 실패 원인이 ISR 의 증가면 옛 값+1 은 틀린 값 → 결국 lost update | 재시도는 `LDREX` 부터 (`bne 1b` 가 `ldrex` 앞 라벨로) |
| LDREX 와 STREX 사이에 함수 호출·다른 메모리 접근·긴 계산 | 창이 넓어져 재시도가 잦아지고, 사이의 다른 배타 접근이 표시를 바꾼다. 아키텍처도 짧게 쓰라고 제한한다 (A3.4.5) | 사이에는 레지스터 계산만 (`adds`, `cmp`, `orr`) |
| CAS 불일치 경로에 `CLREX` 없음 | Exclusive 상태가 남아 짝 안 맞는 STREX 가 성공할 수 있다 | `2: clrex` 후 반환 |
| ISR 에서 `while (!try_lock(&l))` | main 이 락을 쥔 채 선점됐으면 main 은 ISR 이 끝나야 돌아온다 → 단일 코어 교착 | 실패하면 `atomic_inc(&deferred)` 하고 반환 (Part C) |
| deferred 를 한 번만 drain | drain 중 다시 락을 잡은 사이에 온 ISR 의 몫이 남는다 | `for (;;) { n = take_all(); if (!n) break; ... }` |
| "QEMU 스트레스 통과 = 원자적" | 기본 TCG 는 블록 경계에서만 IRQ → 틀린 `ldr/add/str` 도 lost 0 (실측 3) | one-insn-per-tb + gdb 강제 주입 + 결정적 모델 |
| DMA 가 쓰는 변수에 LDREX/STREX | 로컬 모니터는 다른 마스터를 못 본다 (QEMU 는 값 비교로 숨김) | DMA 와는 소유권 비트·디스크립터 순서로 (C-A02), 공유 변수는 마스터 하나만 쓰게 |
| v6-M 에 C11 atomic 그대로 | `__atomic_fetch_add_4` 링크 에러 (실측 4) | PRIMASK 임계 구역 libcall 직접 제공, 또는 처음부터 `irq_save/restore` |
| 손 코드 `dmb` 생략 "M3 에선 동작하니까" | M7 의 쓰기 버퍼·캐시, 듀얼 코어, 이식 시 깨짐 | lock 성공 뒤 `dmb`, unlock 앞 `dmb` |

```asm
@ 틀림: 실패 후 같은 r2 로 다시 STREX           @ 맞음: LDREX 부터
1:  ldrex   r2, [r0]                            1:  ldrex   r2, [r0]
    adds    r2, r2, #1                              adds    r2, r2, #1
2:  strex   r12, r2, [r0]                           strex   r12, r2, [r0]
    cmp     r12, #0                                 cmp     r12, #0
    bne     2b      @ 표시도 없고 값도 낡음         bne     1b
```

## 8. 면접 Q&A

<details>
<summary>Q1. LDREX/STREX 사이에 인터럽트가 오면 정확히 무슨 일이 일어나나요?</summary>

`LDREX` 가 로컬 모니터를 그 주소에 대해 Exclusive 로 만든다. 예외 진입(그리고 복귀)이 모니터를 Open 으로 되돌린다 (ARMv7-M A3.4.4). ISR 이 끝나고 돌아오면 `STREX` 가 Open 상태를 보고 **쓰지 않고 1 을 반환**, 루프가 `LDREX` 부터 다시 해서 ISR 이 바꾼 새 값을 읽는다. 이 레슨의 gdb R1 에서 실제로 r12=1 → attempts=2 → 최종 +2 를 보았다. 꼬리: "ISR 이 그 변수를 안 건드렸으면?" → 그래도 실패한다 (R2). main 입장에서는 가짜 실패이고, C11 `compare_exchange_weak` 가 가짜 실패를 허용하는 이유다.

</details>

<details>
<summary>Q2. 그럼 Cortex-M RTOS 의 컨텍스트 스위치에는 CLREX 가 필요 없나요? Cortex-A 는요?</summary>

Cortex-M 의 스위치는 PendSV(예외) 안에서 일어나고, 예외 진입·복귀가 모니터를 지우므로 필요 없다. FreeRTOS 도 그 근거로 PendSV 에 `CLREX` 를 넣자는 제안을 기각했다. ARMv7-A/R 은 예외가 로컬 모니터를 지우지 않으므로 OS 가 스위치/예외 복귀 경로에서 `CLREX` 해야 한다. 안 하면 연습 1 처럼 **이전 태스크의 LDREX 표시를 다른 태스크가 이어받아** 오래된 값으로 STREX 가 성공한다 (5+3=8 이어야 할 것이 7). 꼬리: "M-profile 에서 CLREX 는 언제 쓰나?" → CAS/try_lock 의 실패 경로처럼, LDREX 를 했지만 STREX 를 하지 않고 빠져나갈 때.

</details>

<details>
<summary>Q3. DMA 가 LDREX 와 STREX 사이에 그 변수를 쓰면요? QEMU 에서는요?</summary>

단일 코어 Cortex-M3/M4 의 **로컬** 모니터는 자기 코어의 배타 접근만 추적하므로, DMA 쓰기를 모른 채 STREX 가 성공해 DMA 값을 덮을 수 있다. 다른 마스터와의 배타는 Shareable 영역의 **글로벌** 모니터가 필요한데, 대부분의 단일 코어 MCU 에는 없다 (칩 문서 확인). QEMU 는 STREX 를 "주소 + LDREX 때 값" 비교(`cmpxchg`)로 구현해서 값이 바뀌면 실패시킨다 — gdb R3 에서 +100 쓰기 후 r12=1. 그래서 **QEMU 는 이 버그를 숨긴다.** 반대로 값을 같은 값으로 다시 쓰면 QEMU 도 못 알아챈다 (R4). 꼬리: "해결책?" → DMA 와는 원자 RMW 가 아니라 소유권 비트(OWN)·디스크립터 순서·`DSB` 로 (C-A02).

</details>

<details>
<summary>Q4. Cortex-M0/M0+ 에서 atomic_fetch_add 는요? 64비트 atomic 은 M4 에서요?</summary>

ARMv6-M 에는 `LDREX/STREX` 가 없다 — 어셈블러가 `selected processor does not support 'ldrex'` 를 낸다. gcc 는 C11 RMW 를 `bl __atomic_fetch_add_4` 로 내보내는데, arm-none-eabi 의 libgcc 에는 그 함수가 없어서 링크 에러가 난다 (실측). 펌웨어가 PRIMASK 임계 구역으로 직접 구현한다. 인터럽트 지연이 RMW 몇 명령만큼 늘어나는 대가가 있다. M3/M4(ARMv7-M)는 `LDREXD/STREXD` 가 없어서 64비트 atomic 이 lock-free 가 아니다. `_Atomic uint64_t` 는 라이브러리 호출이 된다 (C-M14 실측, CPP-A04 에서 `is_always_lock_free` 확인). 꼬리: "듀얼 M0+ RP2040 은?" → PRIMASK 는 다른 코어를 못 막으니 SIO 하드웨어 스핀락.

</details>

<details>
<summary>Q5. try_lock 에 DMB 는 왜, 어디에? Cortex-M3 에서 정말 필요해요?</summary>

획득 성공 **뒤** `DMB` = acquire: 임계 구역의 읽기·쓰기가 잠금보다 먼저 일어나지 않게. 해제 **앞** `DMB` = release: 임계 구역의 쓰기가 해제(`str 0`)보다 먼저 보이게. M3/M4 단일 코어는 일반 메모리 접근을 재배치하지 않아서 같은 코어 ISR 상대로는 없어도 대개 동작한다. 하지만 M7(쓰기 버퍼·캐시), 듀얼 코어, DMA 가 끼면 필요하고, 아키텍처 계약상 필요하다. 컴파일러도 seq_cst 에 `dmb ish` 를 앞뒤로 넣는다 (실측: `c11_inc_seq_cst`). 꼬리: "컴파일러 재배치는?" → asm 의 `"memory"` 클로버나 C11 atomic 이 막는다. `DMB` 는 CPU 쪽이다 (C-A02).

</details>

<details>
<summary>Q6. QEMU 로 이 코드를 검증하면 충분한가요?</summary>

아니다. 이유 셋 (모두 실측). ① 기본 TCG 는 블록 경계에서만 인터럽트를 확인해서 틀린 `ldr/add/str` 도 SysTick 스트레스에서 lost 0 이었다. `one-insn-per-tb` 를 켜야 lost 219~245 가 보였다. ② STREX 를 값 비교로 흉내 내서 DMA/디버거 쓰기 버그를 숨긴다 (R3). ③ 타이밍·버스·캐시·글로벌 모니터가 없다. 그래서 정확성은 **결정적 모델(모든 k 스윕)** 과 **gdb 강제 주입** 으로, 성능과 칩 고유 동작은 실보드에서 확인한다. 꼬리: "실보드에서 같은 gdb 실험을 하려면?" → J-Link/OpenOCD 로 같은 스크립트(ICSR/ISPR 쓰기). 다만 디버그 방식(할트 vs 모니터 모드)에 따라 스텝 자체가 모니터를 지울 수 있음에 주의 (추론).

</details>

<details>
<summary>Q7. ISR 이 main 이 쥔 락을 만나면 어떻게 설계하나요?</summary>

단일 코어에서 ISR 은 기다릴 수 없다. main 은 ISR 이 끝나야 다시 돈다. 선택지: ① 그 데이터를 만지는 main 쪽 구간에서 해당 IRQ 를 마스킹(BASEPRI/PRIMASK) — 가장 흔하다. ② `try_lock` 실패 시 일을 미루기(deferred 카운터/플래그) → main 이 unlock 뒤 처리 — Part C. ③ 읽기만 하면 seqlock/더블 버퍼로 락 자체를 없애기 (C-M14). RTOS 라면 ISR 은 `...FromISR` API 로 실패를 받고, 일은 태스크 알림으로 넘긴다. 꼬리: "멀티코어면?" → 다른 코어의 ISR 은 기다려도 되지만(스핀락 + 같은 코어 IRQ 마스킹 = `spin_lock_irqsave` 구조), 같은 코어는 여전히 안 된다.

</details>

## 9. 추가 연습

### 연습 1 — A-profile 식 컨텍스트 스위치와 CLREX

`MON_NOCLEAR` 정책에서 스케줄러 예외 `os_switch(c, do_clrex)` 를 만들고, 대본 "B: LDREX → 스위치 → A: atomic_inc 완료 + 두 번째 LDREX → 스위치 → B: STREX → 스위치 → A: STREX" 를 돌려라 (x=5, 증가 3번 → 정답 8). CLREX 없이 / 있이 / V7M 정책 세 경우의 최종값은?

<details>
<summary>답</summary>

`os_switch` = `exc_entry; if (do_clrex) CLREX; exc_exit`. 결과: NOCLEAR·CLREX 없음 → **7** (B 의 STREX 가 A 가 세운 표시로 성공해 A 의 첫 증가를 덮음), NOCLEAR·CLREX → 8, V7M → 8 (예외가 자동으로 지움). Q2 의 답을 코드로 고정한 것이다.

</details>

### 연습 2 — atomic_or: 이벤트 플래그

`uint32_t atomic_or(c, p, bits)` (옛 값 반환)를 LDREX/STREX 로 쓰고, main 이 bit0 을 세우는 동안 ISR 이 k = 0..5 에 bit1 을 세워도 항상 `flags == 3` 인지 확인하라. 재시도는 몇 번?

<details>
<summary>답</summary>

`atomic_inc` 와 같은 모양에 `old | bits`. 6가지 k 모두 3, STREX 실패는 k=1,2 의 2번뿐. FreeRTOS EventGroup / Zephyr `k_event` 의 "ISR 에서 비트 세우기" 핵심이다. M3 에서는 비트밴드(bit-band) 별칭 주소로 비트 하나를 쓰는 방법도 있다 (M7/M33 에는 비트밴드 없음).

</details>

### 연습 3 — 재시도 상한 + PRIMASK 폴백

인터럽트가 매 `STREX` 직전에 오는 폭주(k = 2, 5, 8, 11)에서 `atomic_inc_bounded(c, p, 4, &out)` 는 실패해야 한다. 그다음 `crit_inc` (CPSID, LDR, OP, STR, CPSIE) 로 폴백하는데, 그 안(k=13)에 온 ISR 은 pend 됐다가 CPSIE 에서 처리돼야 한다. 최종값과 지연(명령 수)은?

<details>
<summary>답</summary>

bounded: STREX 4번 모두 실패, ISR 4번이 카운터를 4 로. crit_inc 는 5 를 쓰고, pend 된 ISR 이 CPSIE 직후 실행돼 6. 지연은 13~16 의 **4 명령**. 이론상 LL/SC 는 인터럽트가 창보다 자주 오면 끝없이 실패할 수 있다(livelock). 현실의 Cortex-M 에서는 창이 2~3 명령이라 드물지만, 상한 + 폴백이 그 이론상 구멍을 막는다. 또 crit_inc 의 k 0..6 스윕 모두 정확하다 (연습 3b).

</details>

### 연습 4 — sem_try_take: 0 이면 실패하는 감소

`bool sem_try_take(c, p)` — `*p > 0` 이면 1 감소, 0 이면 `CLREX` 후 false. 세마포어 1개에서 main 이 두 번 take 하고, ISR 이 k = 0..9 어딘가에서 한 번 give(atomic_inc) 할 때 보존 법칙은?

<details>
<summary>답</summary>

`남은 수 + 가져간 수 == 2` (처음 1 + give 1), 가져간 수 ≥ 1. 모든 k 에서 성립한다. 0 에서 실패할 때 `CLREX` 1번, 모니터 Open. `xSemaphoreTakeFromISR` / `k_sem_take(K_NO_WAIT)` 의 핵심 연산이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 12/12 passed, exit 0
```

## 10. 핵심 정리

- 로컬 모니터 = Open/Exclusive. `LDREX` 표시, `STREX` 는 Exclusive 일 때만 쓰고 0 — **끝나면 항상 Open**. `CLREX` 는 Open 으로.
- **ARMv7-M: 예외 진입·복귀가 모니터를 지운다** (A3.4.4) → ISR 이 끼면 STREX 1 → `LDREX` 부터 재시도. ISR 이 아무것도 안 해도 실패(가짜 실패).
- Cortex-M 컨텍스트 스위치(PendSV)에는 `CLREX` 불필요, ARMv7-A 는 OS 가 해야 한다. 실패 경로(CAS 불일치, 잠김)에는 `CLREX`.
- 로컬 모니터는 DMA/다른 마스터 쓰기를 못 본다. QEMU 는 "주소+값" 비교라 그걸 잡아서 버그를 숨긴다. 같은 값 쓰기는 QEMU 도 못 본다.
- QEMU 기본 TCG 는 블록 경계에서만 IRQ → 틀린 RMW 도 통과. `one-insn-per-tb` + gdb 강제 주입 + 결정적 k 스윕으로 검증.
- ISR 은 main 의 락을 기다리면 안 된다: try_lock 실패 → deferred → main 이 0 될 때까지 drain.
- v6-M(M0/M0+): 배타 명령 없음, gcc 는 `__atomic_*_4` libcall (libgcc 에 없음) → PRIMASK 임계 구역. v7-M: 64비트 배타 없음.
- 락 성공 뒤·해제 앞 `DMB`. M3/M4 단일 코어에선 대개 무해한 생략이지만 M7·멀티코어·이식성 때문에 넣는다.

## 참고자료

- ARMv7-M Architecture Reference Manual (DDI 0403) — A3.4 Synchronization and semaphores (A3.4.1 로컬 모니터, A3.4.2 글로벌 모니터, A3.4.3 marked block 크기, A3.4.4 Context switch support, A3.4.5 사용 제한), A3.7 메모리 배리어, B1.5 예외 모델: <https://developer.arm.com/documentation/ddi0403/latest/>
- ARMv6-M Architecture Reference Manual (DDI 0419) — 배타 명령 없음, PRIMASK/CPS: <https://developer.arm.com/documentation/ddi0419/latest/>
- Arm A-profile ARM (DDI 0487) — AArch64/AArch32 Exclusives, ERET 와 모니터 (ARM-M04 와 함께): <https://developer.arm.com/documentation/ddi0487/latest/>
- Arm DHT0008A "ARM Synchronization Primitives" — LDREX/STREX 사용법, 컨텍스트 스위치 시 CLREX
- FreeRTOS bug #132 "PendSV handler does not execute a CLREX" (A3.4.4 인용, invalid 로 종료): <https://sourceforge.net/p/freertos/bugs/132>
- QEMU 소스 v10.2.1 — `target/arm/tcg/translate.c` (`gen_store_exclusive`, `gen_clrex`), `target/arm/tcg/m_helper.c` (`v7m_exception_taken`, 예외 복귀), `target/arm/internals.h` (`arm_clear_exclusive`), `hw/arm/mps2.c` (AN385 메모리 맵, num-irq 32): <https://gitlab.com/qemu-project/qemu/-/tree/v10.2.1/target/arm>
- QEMU Arm system emulator — mps2-an385, microbit, `-accel tcg,one-insn-per-tb=on`, 세미호스팅: <https://www.qemu.org/docs/master/system/target-arm.html>
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* — 배타 접근, 세마포어 장
- 관련 레슨: C-M14 (ISR↔main, PRIMASK, Q5), C-A02 (컴파일러 배리어·DMA 순서), C-A03 (C11 atomics, thumbv7em asm, `clrex`), CPP-A04 (lock-free 여부·M0 libatomic), ARM-M04 (AArch64 LDXR/STXR·LSE), ARM-M05 (DMB/DSB/ISB), RISCV-M04 (LR/SC 예약 집합), OS-M08 (C11 패턴), SOCX-B03 (글로벌 모니터)
