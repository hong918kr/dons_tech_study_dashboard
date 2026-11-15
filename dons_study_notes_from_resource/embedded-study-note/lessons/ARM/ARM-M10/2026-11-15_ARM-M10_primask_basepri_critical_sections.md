---
id: ARM-M10
time: 80분
updated: 2026-11-15
---
# ARM-M10 PRIMASK·BASEPRI로 크리티컬 섹션 구현

## 학습 목표

- `MRS/MSR PRIMASK` + `CPSID I` 로 **중첩 가능한** `crit_enter()/crit_exit(saved)` 를 Thumb-2 로 쓰고, "무조건 `cpsie i`" 버그가 중첩을 깨는 것을 실측으로 잡는다.
- `BASEPRI` 와 `BASEPRI_MAX` 의 의미(값이 **크거나 같은** 우선순위를 막는다, 0 = 아무것도 안 막음, `_MAX` = 더 급한 쪽으로만)를 레지스터로 확인하고, 논리 우선순위 → 구현 비트(`PRIO_BITS` 3 / 8) 바이트 변환을 만든다.
- QEMU `mps2-an385` 에서 SysTick ISR 과 main 이 같은 카운터를 RMW 할 때 **lost update 를 실제로 세고**, PRIMASK / 올바른 BASEPRI 로 0 이 되는 것, 잘못된 BASEPRI 로는 여전히 잃는 것을 결정적으로(매번 같은 숫자로) 보인다.
- "PRIMASK 는 NMI·HardFault 를 못 막는다", "BASEPRI 섹션 안에서도 급한 모터 IRQ 는 돈다", "inline asm 에 `memory` clobber 가 없으면 크리티컬 섹션이 텅 빈다" 를 실측으로 설명한다.

## 1. 문제

**왜 중요한가:** 펌웨어 면접의 단골 질문 "ISR 과 main 이 같이 쓰는 변수는 어떻게 보호해?" 의 진짜 답은 `__disable_irq()` 한 줄이 아니라 **중첩·우선순위·지연(latency)** 까지다 — "PRIMASK 랑 BASEPRI 차이?", "크리티컬 섹션 안에서 SVC 부르면?", "FreeRTOS 는 왜 BASEPRI 를 쓰나?" 가 꼬리질문으로 온다.

C-M14 는 PRIMASK 를 C 목(mock)으로 흉내 냈고, ARM-M11 은 LDREX/STREX 로 락 없이 풀었고, BM-M02 는 우선순위 표를 설계했다. 이번에는 진짜 Cortex-M3 레지스터(QEMU)에서 **마스크 레지스터 자체** 를 다룬다.

Cortex-M 의 마스크 레지스터 세 개와 우선순위 숫자선. 숫자가 **작을수록 급하다**. 아래 값들은 이 레슨의 테스트에서 쓰는 실제 값이다 (3비트 기준 논리 우선순위 × 0x20).

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg">
  <text x="10" y="20" font-size="14" fill="currentColor">실행 우선순위 숫자선 (작을수록 급함) — 어떤 마스크가 무엇을 막나</text>
  <line x1="40" y1="70" x2="720" y2="70" stroke="currentColor" stroke-width="2"/>
  <line x1="40" y1="64" x2="40" y2="76" stroke="currentColor"/>
  <text x="22" y="95" font-size="12" fill="currentColor">-2 NMI</text>
  <line x1="90" y1="64" x2="90" y2="76" stroke="currentColor"/>
  <text x="70" y="58" font-size="12" fill="currentColor">-1 HardFault</text>
  <line x1="140" y1="64" x2="140" y2="76" stroke="currentColor"/>
  <text x="132" y="95" font-size="12" fill="currentColor">0x00</text>
  <line x1="215" y1="64" x2="215" y2="76" stroke="currentColor"/>
  <text x="190" y="95" font-size="12" fill="currentColor">0x20 모터</text>
  <line x1="290" y1="64" x2="290" y2="76" stroke="currentColor" stroke-width="3"/>
  <text x="262" y="58" font-size="12" fill="currentColor">BASEPRI=0x40</text>
  <line x1="440" y1="64" x2="440" y2="76" stroke="currentColor"/>
  <text x="408" y="95" font-size="12" fill="currentColor">0x80 SysTick</text>
  <line x1="535" y1="64" x2="535" y2="76" stroke="currentColor"/>
  <text x="510" y="95" font-size="12" fill="currentColor">0xA0 통신</text>
  <line x1="700" y1="64" x2="700" y2="76" stroke="currentColor"/>
  <text x="684" y="95" font-size="12" fill="currentColor">0xFF</text>
  <rect x="290" y="115" width="430" height="30" class="f-warn" opacity="0.5"/>
  <text x="300" y="135" font-size="13" fill="currentColor">BASEPRI=0x40: 값 ≥ 0x40 인 예외를 막음 (SysTick·통신)</text>
  <rect x="40" y="115" width="250" height="30" class="f-ok" opacity="0.5"/>
  <text x="50" y="135" font-size="13" fill="currentColor">통과: NMI·HardFault·모터</text>
  <rect x="140" y="160" width="580" height="30" class="f-warn" opacity="0.5"/>
  <text x="150" y="180" font-size="13" fill="currentColor">PRIMASK=1: 설정 가능한 우선순위(0x00~0xFF) 전부 막음</text>
  <rect x="40" y="160" width="100" height="30" class="f-ok" opacity="0.5"/>
  <text x="48" y="180" font-size="13" fill="currentColor">NMI·HF</text>
  <rect x="90" y="205" width="630" height="30" class="f-warn" opacity="0.5"/>
  <text x="150" y="225" font-size="13" fill="currentColor">FAULTMASK=1: HardFault 까지 막음 (실행 우선순위 -1)</text>
  <rect x="40" y="205" width="50" height="30" class="f-ok" opacity="0.5"/>
  <text x="46" y="225" font-size="13" fill="currentColor">NMI</text>
  <text x="10" y="262" font-size="13" fill="currentColor">BASEPRI=0 은 "아무것도 안 막음".</text>
  <text x="10" y="282" font-size="13" fill="currentColor">실행 우선순위 = 가장 급한 값(활성 핸들러, BASEPRI, PRIMASK→0, FAULTMASK→-1)</text>
  <text x="10" y="304" font-size="13" fill="currentColor">구현 비트: QEMU 8 비트 (실측) · 실제 AN385 M3 3 비트 · STM32F4 4 비트 — 상위 비트부터 구현됨</text>
</svg>
```

### Part A — 프리미티브 (Thumb-2 어셈블리, `starter.S` 의 TODO)

| 함수 | 할 일 |
|---|---|
| `uint32_t crit_enter(void)` | 옛 PRIMASK 를 돌려주고 인터럽트를 끈다 |
| `void crit_exit(uint32_t saved)` | PRIMASK 를 **저장값으로 복원** (켜는 게 아니라) |
| `basepri_get / basepri_set` | BASEPRI 읽기 / 무조건 쓰기 |
| `void basepri_raise(uint32_t v)` | `MSR BASEPRI_MAX` — 더 급한 쪽으로만 바뀐다 |
| `uint32_t basepri_enter(uint32_t level)` / `basepri_exit(old)` | 옛 값 반환 + 조건부 상승 / 복원 — 중첩 가능 |
| `uint32_t prio_encode(uint32_t logical, uint32_t bits)` | CMSIS 식 `(logical << (8 - bits)) & 0xFF` |

검증: 중첩 enter/enter/exit/exit 의 PRIMASK 값, 이미 꺼진 상태에서 진입했을 때 exit 이 켜 버리지 않는지, `BASEPRI_MAX` 의 상승·무시 규칙, 3 비트·8 비트 인코딩. 그리고 **결정적 주입**: `rmw_inc_inject()` 가 `ldr` 와 `str` 사이에서 IRQ 를 스스로 pend → 보호 없으면 100 번 중 100 번 잃고, PRIMASK 로 감싸면 0 번 잃는다.

### Part B — 우선순위와 버그 주입

1. **모터는 계속 돈다:** BASEPRI=0x40 섹션 안에서 모터(0x20)와 통신(0xA0)을 동시에 pend → 모터만 실행, 통신은 pending 으로 남았다가 exit 직후 실행. 대가: 모터 ISR 이 같은 데이터를 만지면 BASEPRI 로는 못 지킨다 (50/50 lost).
2. **BASEPRI 방향:** BASEPRI=0xA0 은 통신(0xA0)을 막고(≥), 0xC0 은 못 막는다(0xA0 &lt; 0xC0).
3. **중첩 BASEPRI:** 바깥 0x40 안에서 안쪽이 `basepri_set(0xC0)` 으로 "그냥 쓰면" 마스크가 **낮아져** 0x80 IRQ 가 새어 들어온다 (10/10 lost). `basepri_enter` (BASEPRI_MAX) 는 0x40 을 지킨다.
4. **PRIMASK 의 한계:** PRIMASK=1 인 채로 NMI 가 실행되고(NMI 안에서 PRIMASK 를 읽으면 1), `udf` 는 HardFault(FORCED) 로 들어간다.
5. **버그 `crit_exit` = 무조건 `cpsie i`:** enter, enter, (버그) exit → 바깥 섹션이 아직 진행 중인데 인터럽트가 켜짐 → 20/20 lost. 테스트가 잡는다.

### Part C — SysTick 스트레스: 진짜 타이머 인터럽트로 lost update 세기

main 이 공유 카운터를 2만 번 `ldr/adds/str` 하는 동안 SysTick(RELOAD=97, 즉 98 클럭마다)이 같은 카운터를 올린다. 네 가지 모드(보호 없음 / PRIMASK / BASEPRI=0x80 (SysTick 0x80 을 막음) / BASEPRI=0xA0 (못 막음))에서 `기대값 − 실제값` 을 센다. 이 실험이 **흔들리지 않게** QEMU 를 `-icount` 모드로 돌리고, 왜 그래야 하는지(기본 TCG 에서는 lost 0, `one-insn-per-tb` 에서는 매번 다름)를 실측으로 보인다.

| 파일 | 내용 |
|---|---|
| `harness.c` | 펌웨어 테스트 (Part A/B/C) — starter/solution 공용 |
| `starter.S` / `solution.S` | TODO 프리미티브 / 정답 |
| `shared_rmw.S` | 주어진 도구: `rmw_inc`, `rmw_inc_inject`, `primask_get`, `crit_exit_buggy`, `do_udf`, HardFault 진입 |
| `shared_startup.c`, `arm_m10.ld` | 벡터 테이블(예외 16 + IRQ 32), 리셋, 세미호스팅 (ARM-B14 축약판) |
| `practice.S` / `practice.c` | 추가 연습 답과 검증 |
| `demo_clobber.c`, `demo_m0_basepri.c` | `memory` clobber 실험, Cortex-M0 에 BASEPRI 가 없음 |
| `run_qemu.sh` | `starter / solution / practice / modes / clobber / objdump / m0` |

## 2. Problem (English)

> You are writing the interrupt-masking layer of a motor-controller firmware on a Cortex-M3 (ARMv7-M).
>
> ```c
> uint32_t crit_enter(void);                 /* returns previous PRIMASK, then disables IRQs      */
> void     crit_exit(uint32_t saved);        /* restores PRIMASK exactly; must be nestable        */
> uint32_t basepri_get(void);
> void     basepri_set(uint32_t v);
> void     basepri_raise(uint32_t v);        /* BASEPRI_MAX semantics: only ever raises the mask   */
> uint32_t basepri_enter(uint32_t level);    /* returns previous BASEPRI, raises to level          */
> void     basepri_exit(uint32_t old);
> uint32_t prio_encode(uint32_t logical, uint32_t bits);   /* (logical << (8-bits)) & 0xFF        */
> ```
>
> Constraints: leaf functions, AAPCS (args r0–r1, result r0), no stack. `crit_exit` must not enable interrupts that were disabled before the matching `crit_enter`. The device implements `bits` priority bits (test 3 and 8).
>
> 1. A shared `uint32_t counter` is incremented by `main` with `ldr/adds/str` and by an ISR. Show that updates are lost without protection, lost **zero** times with your PRIMASK section, zero times with a BASEPRI section that covers the ISR priority, and still lost when BASEPRI does not cover it.
> 2. A motor IRQ at priority 0x20 must keep running while a section masks priorities ≥ 0x40. Prove it fires inside the section while a 0xA0 IRQ is held pending.
> 3. Explain why `cpsid i` does not mask NMI or HardFault, and what happens to an SVC executed inside a PRIMASK section.
>
> Example: `prio_encode(5, 3) == 0xA0`, `prio_encode(5, 8) == 0x05`; after `basepri_set(0x40); basepri_raise(0x80);` BASEPRI is still `0x40`.

## 3. 시작 코드

펌웨어 테스트 하네스 (starter 와 solution 공용):

<!-- code: harness.c -->

주어진 실험 도구 (TODO 없음):

<!-- code: shared_rmw.S -->

TODO 가 있는 어셈블리:

<!-- code: starter.S -->

맥에서 한 줄 (Lima VM `elinux`: arm-none-eabi-gcc 14.2.1, qemu-system-arm 10.2.1). Mac 홈은 VM 에서 읽기 전용이라 스크립트가 `/tmp/ARM-M10_work` 로 복사해서 빌드한다.

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ARM/ARM-M10
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # solution / practice / modes / clobber / objdump / m0
```

`run_qemu.sh starter` 가 하는 일:

```bash
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror -ffreestanding -nostdlib \
    -fno-tree-loop-distribute-patterns -T arm_m10.ld harness.c starter.S shared_rmw.S shared_startup.c -o starter.elf -lgcc
timeout 20 qemu-system-arm -M mps2-an385 -nographic -monitor none -serial none \
    -semihosting-config enable=on,target=native -icount shift=0,align=off,sleep=off -kernel starter.elf
```

<!-- code: run_qemu.sh -->

VM 없이 맥에서 "어셈블은 되나?" 만 확인: `clang --target=thumbv7m-none-eabi -c solution.S -o /tmp/ARM-M10_solution.o` (실행은 QEMU 필요).

처음 실행 결과 (실제 출력, PASS 줄 생략):

```text
== ARM-M10 PRIMASK / BASEPRI critical sections (QEMU mps2-an385) ==
FAIL line 142: k1 == 0 && primask_get() == 1
FAIL line 144: k2 == 1 && primask_get() == 1
FAIL line 146: primask_get() == 1
FAIL line 152: k3 == 1 && primask_get() == 1
INFO inject none    lost = 100
INFO inject PRIMASK lost = 100
FAIL line 162: lost == 0 && g_low_hits == 200
...
INFO stress PRIMASK        lost = 10
INFO stress BASEPRI=0x80   lost = 6
FAIL line 245: l_pm == 0
FAIL line 246: l_bp == 0
15/35 passed
qemu exit=1
```

## 4. 힌트

<details><summary>힌트 1 — 어떤 명령으로 PRIMASK 를 읽고 쓰나</summary>

특수 레지스터는 `MRS Rd, <spec>` / `MSR <spec>, Rn` 으로만 만진다 (`primask`, `basepri`, `basepri_max`, `faultmask`). 켜고 끄기만 할 때는 16비트 `CPSID I` / `CPSIE I` 가 있다. `crit_enter` 는 "읽고 → 끈다" 두 줄이면 된다. 순서가 반대면?

</details>

<details><summary>힌트 2 — 중첩 가능하다는 것의 의미</summary>

`crit_exit` 가 할 일은 "인터럽트를 켠다" 가 아니라 "**진입 전 상태로 되돌린다**" 다. 저장값이 1(이미 꺼져 있었다)이면 계속 꺼진 채여야 한다. 그래서 `cpsie i` 가 아니라 `msr primask, r0`.

</details>

<details><summary>힌트 3 — BASEPRI 방향</summary>

BASEPRI = B (≠ 0) 이면 우선순위 **값이 B 이상**인 예외가 막힌다 (덜 급한 것들). 0 은 특별해서 "아무것도 안 막음". `BASEPRI_MAX` 에 쓰면 새 값이 0 이 아니고, (현재 BASEPRI 가 0 이거나 새 값이 현재보다 작을 때) 만 반영된다 — 즉 마스크를 **넓히기만** 한다.

</details>

<details><summary>힌트 4 — prio_encode 를 Thumb-2 로</summary>

`shift = 8 - bits` 는 `rsb r1, r1, #8` (reverse subtract). 레지스터 시프트 `lsls r0, r0, r1`, 바이트 자르기 `uxtb r0, r0`. 세 줄.

</details>

<details><summary>힌트 5 — 스트레스 실험이 흔들리면</summary>

QEMU TCG 는 번역 블록(TB) 경계에서만 인터럽트를 확인하고, 타이머는 호스트 시간을 따른다. `-icount shift=0` 은 가상 시간을 "실행한 명령 수" 로 정의해서 SysTick 이 매번 같은 명령 위치에 떨어지게 한다. 그런데 틱 주기가 루프 길이의 배수면 매번 **같은 위상**에 떨어진다 — main 에 가변 길이 일을 끼워 넣어 위상을 섞는다.

</details>

## 5. 풀이

### 0. 레지스터 네 개 — 인코딩까지 (`run_qemu.sh objdump`, 실제 출력)

```text
00000a58 <crit_enter>:
     a58:	f3ef 8010 	mrs	r0, PRIMASK
     a5c:	b672      	cpsid	i
     a5e:	4770      	bx	lr
00000a60 <crit_exit>:
     a60:	f380 8810 	msr	PRIMASK, r0
     a64:	4770      	bx	lr
00000a72 <basepri_raise>:
     a72:	f380 8812 	msr	BASEPRI_MAX, r0
     a76:	4770      	bx	lr
00000a78 <basepri_enter>:
     a78:	f3ef 8111 	mrs	r1, BASEPRI
     a7c:	f380 8812 	msr	BASEPRI_MAX, r0
     a80:	4608      	mov	r0, r1
     a82:	4770      	bx	lr
00000a8a <prio_encode>:
     a8a:	f1c1 0108 	rsb	r1, r1, #8
     a8e:	4088      	lsls	r0, r1
     a90:	b2c0      	uxtb	r0, r0
     a92:	4770      	bx	lr
```

`MRS/MSR` 는 32비트 Thumb-2, 두 번째 halfword 의 하위 8비트가 SYSm 번호다. 같은 비트들이 맥 clang(`--target=thumbv7m-none-eabi`) 에서도 나온다 (`f3ef 8111 / f380 8812`).

| 레지스터 | SYSm | 폭 | 효과 | 쓰는 법 |
|---|---|---|---|---|
| PRIMASK | 16 (0x10) | 1 비트 | 1 이면 실행 우선순위 = 0 → 설정 가능한 예외 전부 막음 | `cpsid i` / `cpsie i` / `msr primask` |
| BASEPRI | 17 (0x11) | 8 비트 (구현 비트만) | ≠0 이면 실행 우선순위 ≤ BASEPRI → 값 ≥ BASEPRI 막음 | `msr basepri` |
| BASEPRI_MAX | 18 (0x12) | (같은 레지스터) | 쓰기 전용 별칭: 조건부 상승 | `msr basepri_max` |
| FAULTMASK | 19 (0x13) | 1 비트 | 실행 우선순위 = -1 → HardFault 까지 막음, NMI 만 통과 | `cpsid f` (연습 2) |

세 마스크는 **독립된 레지스터**다. 실행 우선순위는 "활성 예외들의 우선순위, BASEPRI, PRIMASK, FAULTMASK 가 각각 요구하는 값 중 가장 급한 것" 이고, 새 예외는 그보다 **엄격히 더 급할 때만** 선점한다 (ARMv7-M ARM B1.5.4 기준, 확인 권장). 그리고 이 레지스터들은 **예외 진입 때 자동으로 바뀌지 않는다** — NMI 안에서 읽은 PRIMASK 가 1 이었던 것이 그 증거다 (Part B).

### Part A — 중첩 PRIMASK, 한 줄씩

`crit_enter = mrs r0, primask ; cpsid i`. 순서가 중요하다: 끄고 나서 읽으면 항상 1 을 저장하게 되어 바깥 exit 이 영원히 인터럽트를 안 켠다.

| 단계 | 명령 | r0 | PRIMASK | 테스트 |
|---|---|---|---|---|
| 시작 | — | — | 0 | |
| `k1 = crit_enter()` | `mrs r0, primask` → `cpsid i` | 0 | 1 | `k1 == 0 && primask == 1` |
| `k2 = crit_enter()` | 〃 | 1 | 1 | `k2 == 1` |
| `crit_exit(k2)` | `msr primask, r0`(=1) | 1 | **1** | 안쪽 exit 뒤에도 꺼짐 |
| `crit_exit(k1)` | `msr primask, r0`(=0) | 0 | 0 | 다시 켜짐 |
| `cpsid i` (남이 끔) → `k3 = enter` → `exit(k3)` | | 1 | **1** | exit 이 남의 상태를 안 바꿈 |

그림으로 — 위는 올바른 복원, 아래는 `crit_exit_buggy` (`cpsie i`):

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg">
  <text x="10" y="18" font-size="14" fill="currentColor">중첩 크리티컬 섹션의 PRIMASK — 복원(위) vs 무조건 cpsie(아래)</text>
  <text x="10" y="58" font-size="13" fill="currentColor">복원</text>
  <rect x="120" y="40" width="480" height="26" class="f-ok" opacity="0.6"/>
  <text x="300" y="58" font-size="13" fill="currentColor">PRIMASK = 1 (끝까지)</text>
  <line x1="120" y1="32" x2="120" y2="75" stroke="currentColor"/>
  <line x1="240" y1="32" x2="240" y2="75" stroke="currentColor"/>
  <line x1="360" y1="32" x2="360" y2="75" stroke="currentColor"/>
  <line x1="600" y1="32" x2="600" y2="75" stroke="currentColor"/>
  <text x="90" y="92" font-size="12" fill="currentColor">enter→k1=0</text>
  <text x="208" y="92" font-size="12" fill="currentColor">enter→k2=1</text>
  <text x="326" y="92" font-size="12" fill="currentColor">exit(1): 그대로</text>
  <text x="450" y="92" font-size="12" fill="currentColor">RMW + IRQ pend</text>
  <text x="572" y="92" font-size="12" fill="currentColor">exit(0)→ISR</text>
  <text x="10" y="148" font-size="13" fill="currentColor">버그</text>
  <rect x="120" y="130" width="240" height="26" class="f-ok" opacity="0.6"/>
  <rect x="360" y="130" width="240" height="26" class="f-warn" opacity="0.6"/>
  <text x="380" y="148" font-size="13" fill="currentColor">PRIMASK = 0 — 바깥 섹션이 열림!</text>
  <line x1="120" y1="122" x2="120" y2="165" stroke="currentColor"/>
  <line x1="240" y1="122" x2="240" y2="165" stroke="currentColor"/>
  <line x1="360" y1="122" x2="360" y2="165" stroke="currentColor"/>
  <line x1="600" y1="122" x2="600" y2="165" stroke="currentColor"/>
  <text x="326" y="182" font-size="12" fill="currentColor">exit_buggy: cpsie i</text>
  <text x="450" y="182" font-size="12" fill="currentColor">ISR 이 ldr~str 사이에 끼어듦</text>
  <text x="10" y="218" font-size="13" fill="currentColor">실측 (각 20회 주입): 복원 → lost 0 · 버그 → lost 20.</text>
  <text x="10" y="238" font-size="13" fill="currentColor">사고는 "안쪽 함수가 crit 을 쓰는 줄 몰랐던" 호출에서 난다.</text>
  <text x="10" y="258" font-size="13" fill="currentColor">예: 드라이버 함수 A 가 섹션 안에서 로그 함수 B 를 부르고, B 가 자기 섹션을 cpsie 로 닫는다.</text>
</svg>
```

**결정적 주입.** "언젠가 인터럽트가 RMW 사이에 들어오면" 을 기다리면 테스트가 흔들린다. 그래서 `rmw_inc_inject` 가 스스로 최악의 순간을 만든다:

```text
ldr  r2, [r0]        ① 옛 값
str  r1, [ISPR0]     IRQ6 pend
dsb                  쓰기가 NVIC 에 도달
isb                  ← 마스크 안 됐으면 IRQ 가 여기서 받아들여진다 (ISR: counter++)
adds r2, r2, #1      ② 옛 값 + 1
str  r2, [r0]        ③ ISR 의 증가를 덮어씀
```

Arm 은 소프트웨어로 pend 한 인터럽트가 확실히 보이게 하려면 `DSB` + `ISB` 를 쓰라고 한다 (AN321 "Memory Barrier Instructions" 의 NVIC 예, 확인 권장). QEMU 쪽에서도 `ISB` 는 번역 블록을 끊어 대기 중인 인터럽트를 바로 확인하게 번역된다 (QEMU `target/arm/tcg` 의 ISB 처리 주석 기억 — 확인 권장). 실측은 기대와 정확히 같다:

| 주입 실험 (harness.c) | ISR 우선순위 | 마스크 | lost / 횟수 |
|---|---|---|---|
| 보호 없음 | 0xA0 | — | **100 / 100** |
| `crit_enter/exit` | 0xA0 | PRIMASK=1 | **0 / 100** (ISR 은 200 회 모두 실행 — 늦을 뿐 안 사라짐) |
| `basepri_enter(0xA0)` | 0xA0 | BASEPRI=0xA0 | 0 / 100 (0xA0 ≥ 0xA0 → 막힘) |
| `basepri_enter(0xC0)` | 0xA0 | BASEPRI=0xC0 | **100 / 100** (0xA0 &lt; 0xC0 → 통과) |

"ISR 은 200 회 모두 실행" 이 중요하다: 마스크는 인터럽트를 **버리지 않고 미룬다**. NVIC 의 pending 비트가 기억하고 있다가 `msr primask, r0` 로 0 이 되는 순간 실행된다. 단, pending 비트는 하나라서 마스크된 동안 같은 IRQ 가 두 번 오면 한 번으로 합쳐진다 (C-M14 `demo_pending_merge.c`). 그래서 크리티컬 섹션은 **짧게**.

**BASEPRI 와 BASEPRI_MAX.** 실측 순서 그대로:

| 동작 | 결과 BASEPRI | 규칙 |
|---|---|---|
| `basepri_set(0x80)` | 0x80 | 그냥 쓰기 |
| `basepri_set(0)` | 0 | 0 = 마스크 없음 |
| `basepri_set(0x40); basepri_raise(0x80)` | **0x40** | 0x80 은 덜 급함 → 무시 |
| `basepri_raise(0x20)` | 0x20 | 더 급함 → 반영 |
| `basepri_raise(0)` | **0x20** | 0 은 "상승" 이 아님 → 무시 |

`basepri_enter` 가 `msr basepri_max` 를 쓰는 이유가 이것이다: 바깥 섹션이 이미 더 넓게 막고 있으면 안쪽 요청은 아무 일도 안 한다. 그래서 별도의 비교·분기 없이 중첩이 된다.

**prio_encode.** NVIC 는 우선순위 바이트의 **위쪽** 비트부터 구현한다. 구현 안 된 아래 비트는 0 으로 읽힌다(RAZ/WI). 그래서 논리 우선순위를 `8 − bits` 만큼 왼쪽으로 민다.

| logical | bits=3 | bits=4 | bits=8 | 비고 |
|---|---|---|---|---|
| 5 | 0xA0 | 0x50 | 0x05 | |
| 7 | 0xE0 | 0x70 | 0x07 | 3비트 칩의 가장 덜 급한 값 |
| 3 | 0x60 | 0x30 | 0x03 | |
| 9 | **0x20** | 0x90 | 0x09 | 3비트에서 범위 밖 → 잘려서 1 (아주 급함!) |

테스트는 `PRIO_BITS = 3` 으로 만든 값(0x20 의 배수)만 쓴다. 그러면 3비트 실칩(실제 AN385 FPGA 의 M3, nRF52 등 — 칩 문서 확인 권장)과 8비트 QEMU 에서 같은 비교 결과가 나온다. 8비트 QEMU 에서 0x45 를 쓰면 0x45 가 읽히지만 3비트 칩에서는 0x40 이 된다 — 하위 비트에 의미를 두는 코드는 실칩에서 조용히 달라진다. (PRIGROUP 이 바이트를 그룹/서브로 자르고 BASEPRI 비교는 그룹 우선순위 기준이라는 점도 0x20 배수만 쓰면 피해 간다 — BM-M02.)

### Part B — 우선순위로 나눠 막기

**모터는 계속 돈다.** BASEPRI=0x40 에서 모터(0x20)와 통신(0xA0)을 한 번에 pend:

```text
basepri_enter(0x40) → ISPR0 = (1<<6)|(1<<7) → dsb; isb
  모터 ISR:  1 회 실행   (0x20 < 0x40)
  통신 ISR:  0 회, ISPR0 bit6 = 1 (대기 중)
basepri_exit(0) → dsb; isb
  통신 ISR:  1 회 실행
```

이것이 BASEPRI 를 쓰는 이유다. PRIMASK 섹션이 3 µs 면 모터 PWM 갱신 ISR 의 지터(jitter)도 3 µs 늘어난다. BASEPRI 섹션은 "섹션이 공유하는 데이터를 만지는 ISR 들" 만 막고, 그보다 급한 ISR(데이터를 공유하지 않는 것)은 지연 0 으로 둔다. FreeRTOS 의 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 가 바로 이 경계값이다: 그보다 급한 ISR 은 커널 API 를 부를 수 없는 대신 커널이 절대 막지 않는다 (FreeRTOS Cortex-M 포트 문서, 확인 권장).

대가도 실측했다: 모터 ISR 이 같은 카운터를 올리면 `basepri_enter(0x40)` 섹션 안에서 **50/50 lost**. BASEPRI 는 "누가 이 데이터를 만지나" 의 설계가 맞을 때만 안전하다. 모터와 공유하는 데이터는 PRIMASK 섹션(혹은 ARM-M11 의 LDREX/STREX)으로.

**중첩 BASEPRI.** 통신 IRQ 를 0x80 으로 바꾼 뒤:

| 바깥 | 안쪽 | 안쪽 뒤 BASEPRI | 통신(0x80) 주입 10회 lost |
|---|---|---|---|
| `basepri_enter(0x40)` | `basepri_enter(0xC0)` (MAX) | 0x40 | **0** |
| `basepri_enter(0x40)` | `basepri_set(0xC0)` (그냥 쓰기) | 0xC0 | **10** |

안쪽 함수는 "나는 0xC0 이하만 막으면 충분" 이라고 생각했지만, 그냥 쓰기는 바깥이 요구한 0x40 을 **낮춰 버린다**. PRIMASK 의 "무조건 cpsie" 와 같은 종류의 버그다.

**PRIMASK 가 못 막는 것.** PRIMASK=1 인 채로:

```text
SCB->ICSR = NMIPENDSET  → NMI 실행, NMI 안에서 읽은 PRIMASK = 1
do_udf() (udf #0)       → HardFault, HFSR = FORCED(bit30), CFSR.UNDEFINSTR(bit16),
                          stacked PC = udf_insn 주소 → 핸들러가 PC += 2 하고 복귀
```

NMI(-2)와 HardFault(-1)는 우선순위가 음수로 고정돼 있어서 PRIMASK 가 만드는 실행 우선순위 0 보다 항상 급하다. UsageFault 는 기본으로 꺼져 있어서(SHCSR.USGFAULTENA=0) HardFault 로 승격(FORCED)된다. 크리티컬 섹션 안에서 fault 가 나도 시스템이 fault 를 처리할 수 있다는 뜻이고, 반대로 "NMI 핸들러와 공유하는 데이터는 PRIMASK 로 못 지킨다" 는 뜻이다. (고장 난 명령 건너뛰기는 테스트 하네스용 — 원인 해석·정책은 ARM-M12 / BM-M13.)

### Part C — SysTick 스트레스와 QEMU 실행 모드

같은 `solution.elf` 를 세 모드로 각 3 회 (`run_qemu.sh modes`, 실제 출력 요약):

| QEMU 모드 | 보호 없음 ticks / lost | PRIMASK lost | BASEPRI=0x80 lost | BASEPRI=0xA0 ticks / lost |
|---|---|---|---|---|
| 기본 TCG | 0 / 0 · 12 / 0 · 135 / 0 | 0 · 0 · 0 | 0 · 0 · 0 | 14 / 0 · 76 / 0 · 50 / 0 |
| `-accel tcg,one-insn-per-tb=on` | 1006 / 70 · **12 / 0** · 874 / 67 | 0 · 0 · 0 | 0 · 0 · 0 | 1872 / 131 · 2335 / 128 · 1976 / 130 |
| `-icount shift=0,align=off,sleep=off` | **140 / 14** ×3 | 0 ×3 | 0 ×3 | **202 / 6** ×3 |

읽는 법:

- **기본 TCG:** 보호가 없어도 lost 0. 인터럽트는 번역 블록 경계에서만 확인되는데 `ldr/adds/str` 은 한 블록 안이다 (ARM-M11 실측 3 과 같은 현상). 틱 수도 0~135 로 들쭉날쭉하다 — SysTick 이 호스트 시간을 따르고, icount 가 없을 때 QEMU ptimer 는 너무 짧은 주기를 약 10 µs 로 늘린다 (QEMU `hw/core/ptimer.c` 주석 기억 — 확인 권장). 세 번 모두 33/35 — "틱 > 100" 과 "보호 없음 lost > 0" 두 CHECK 가 FAIL.
- **one-insn-per-tb:** 명령마다 인터럽트를 확인하니 lost 가 보인다 (보호 없음 약 7%). 그러나 run 2 의 보호 없음 구간은 틱이 12 번뿐이라 lost 0 — 호스트 부하에 따라 결과가 바뀐다. CHECK 에 쓰면 가끔 실패하는 테스트가 된다.
- **icount:** 가상 시간이 "실행한 명령 수 × 2^shift ns" 로 정의된다. SysTick 마감 시각에 정확히 맞춰 번역 블록을 자르므로 인터럽트가 명령 사이 정확한 위치에 떨어지고, **세 번 다 같은 숫자**다. `run_qemu.sh solution` 을 5 번 돌린 출력의 md5 가 모두 같았다 (`a42e3bfc…`). 채점 실행은 이 모드다.

**위상 고정 함정 (실측).** 처음에는 main 루프에 `other_work()` 가 없었다 (그 줄만 뺀 빌드, icount):

```text
INFO stress none          ticks = 40
INFO stress none           lost = 40
INFO stress PRIMASK       ticks = 86
INFO stress PRIMASK        lost = 0
INFO stress BASEPRI=0x80  ticks = 97
INFO stress BASEPRI=0x80   lost = 0
INFO stress BASEPRI=0xA0  ticks = 97
INFO stress BASEPRI=0xA0   lost = 10
```

틱마다 하나도 빠짐없이 잃었다 (RELOAD 31/37/53/61/127 로 바꿔도 "none lost = ticks" 그대로). 계산해 보면 이유가 보인다. 25 MHz 클럭 · shift=0 이면 한 클럭 = 40 ns = 40 명령, 틱 주기 = 98 × 40 = 3920 명령. 그중 SysTick_Handler 가 8 명령(objdump), main 이 3912 명령. 그런데 "보호 없음" 루프는 `mov, bl, ldr, adds, str, bx, subs, bne` 딱 8 명령이고 3912 = 8 × 489 → 틱이 **매번 같은 명령 위치** — 하필 `ldr` 와 `str` 사이 — 에 떨어진다. RELOAD 가 바뀌어도 주기는 40 의 배수라 그대로다. 실칩에서도 주기가 맞물리면(고정 길이 루프와 같은 클럭의 타이머) 재현율이 0% 나 100% 로 치우칠 수 있다. 그래서 main 에 xorshift 로 정한 0~7 개 `nop` 을 끼워 위상을 섞었다 (고정 시드 → 여전히 결정적). 이제 "보호 없음" 반복은 평균 약 27 명령(objdump 로 셈)이고 창은 2 칸이니 기대 손실률 약 7%, 실측 14/140 = 10% (표본 140 개).

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg">
  <text x="10" y="18" font-size="14" fill="currentColor">틱이 떨어지는 위치 — 창(window)은 ldr 뒤 · adds 뒤 두 칸</text>
  <rect x="20" y="40" width="80" height="30" class="f-mut"/>
  <text x="30" y="60" font-size="12" fill="currentColor">bl rmw_inc</text>
  <rect x="100" y="40" width="80" height="30" class="f-mut"/>
  <text x="118" y="60" font-size="12" font-family="monospace" fill="currentColor">ldr</text>
  <rect x="180" y="40" width="80" height="30" class="f-warn"/>
  <text x="196" y="60" font-size="12" font-family="monospace" fill="currentColor">adds</text>
  <rect x="260" y="40" width="80" height="30" class="f-warn"/>
  <text x="280" y="60" font-size="12" font-family="monospace" fill="currentColor">str</text>
  <rect x="340" y="40" width="80" height="30" class="f-mut"/>
  <text x="358" y="60" font-size="12" font-family="monospace" fill="currentColor">bx lr</text>
  <rect x="420" y="40" width="300" height="30" class="f-mut"/>
  <text x="440" y="60" font-size="12" fill="currentColor">other_work: 0~7 nop 등 (평균 약 17 명령)</text>
  <text x="20" y="92" font-size="12" fill="currentColor">IRQ 가 adds 앞이나 str 앞에서 받아들여지면(주황) 그 틱의 증가가 덮어써진다</text>
  <text x="20" y="122" font-size="13" fill="currentColor">jitter 없음 (R=97):</text>
  <line x1="190" y1="130" x2="190" y2="150" stroke="currentColor" stroke-width="3"/>
  <text x="200" y="146" font-size="12" fill="currentColor">틱 40/40 이 매번 같은 칸 → lost 40</text>
  <text x="20" y="182" font-size="13" fill="currentColor">jitter 있음:</text>
  <line x1="130" y1="165" x2="130" y2="190" stroke="currentColor"/>
  <line x1="250" y1="165" x2="250" y2="190" stroke="currentColor" stroke-width="3"/>
  <line x1="380" y1="165" x2="380" y2="190" stroke="currentColor"/>
  <line x1="470" y1="165" x2="470" y2="190" stroke="currentColor"/>
  <line x1="560" y1="165" x2="560" y2="190" stroke="currentColor"/>
  <line x1="660" y1="165" x2="660" y2="190" stroke="currentColor"/>
  <text x="480" y="205" font-size="12" fill="currentColor">틱 140 중 14 개만 창 안 → lost 14</text>
</svg>
```

최종 Part C 실측 (icount, 채점 실행과 같은 출력):

```text
INFO stress none          ticks = 140
INFO stress none           lost = 14
INFO stress PRIMASK       ticks = 181
INFO stress PRIMASK        lost = 0
INFO stress BASEPRI=0x80  ticks = 202
INFO stress BASEPRI=0x80   lost = 0
INFO stress BASEPRI=0xA0  ticks = 202
INFO stress BASEPRI=0xA0   lost = 6
35/35 passed
qemu exit=0
```

BASEPRI=0xA0 의 손실률(6/202 ≈ 3%)이 보호 없음(10%)보다 낮은 이유: 루프에 `mov/bl` 과 `basepri_enter/exit` 로 약 11 명령이 더해져 반복이 길어졌는데 창은 여전히 2 칸이라서다. **"덜 자주 깨진다" 는 "안 깨진다" 가 아니다** — 실칩에서 이런 버그는 하루 한 번 터지는 버그가 된다.

### 컴파일러 쪽 함정 — `memory` clobber (`run_qemu.sh clobber`, 실제 출력)

C 에서 inline asm 으로 같은 섹션을 만들 때 clobber 를 빼면 (`demo_clobber.c`, `-O2`):

```text
00000000 <add_n_noclob>:
   8:	ldr	r1, [r4, #0]          ← 루프 전에 g_count 를 한 번 읽고
   a:	mrs	r2, PRIMASK
   e:	cpsid	i
  10:	msr	PRIMASK, r2           ← 섹션 안이 텅 비었다
  14:	adds	r3, #1
  16:	cmp	r0, r3
  18:	bne.n	a <add_n_noclob+0xa>
  1a:	add	r1, r0
  1c:	str	r1, [r4, #0]          ← 루프 뒤에 한 번에 씀 (인터럽트 켜진 상태)
00000028 <add_n_ok>:
  2e:	mrs	ip, PRIMASK
  32:	cpsid	i
  34:	ldr	r3, [r1, #0]
  36:	adds	r3, #1
  38:	str	r3, [r1, #0]
  3a:	msr	PRIMASK, ip
```

`asm volatile` 는 asm 문끼리의 순서와 삭제만 막는다. 일반 메모리 접근을 asm 앞뒤로 옮기지 말라는 약속은 `"memory"` clobber 다. 그래서 CMSIS 의 `__disable_irq()` 는 `__ASM volatile ("cpsid i" : : : "memory")` 로 정의돼 있다 (CMSIS-Core `cmsis_gcc.h`). 이 레슨의 프리미티브는 별도 `.S` 의 함수라서 호출 자체가 컴파일러 장벽 역할을 한다 (컴파일러는 외부 함수가 전역을 읽고 쓴다고 가정한다). 하지만 LTO 나 inline 판으로 바꾸는 순간 clobber 가 필요하다 — 추가 연습 대신 이 출력을 기억하자.

하드웨어 쪽 순서: ARMv7-M 에서 `CPSID I` 는 다음 명령부터 효과가 있어 ISB 가 필요 없다고 Arm 문서(AN321)는 설명한다. 반면 `MSR BASEPRI` 로 마스크를 올리는 것은 Cortex-M7 r0p1 에서 바로 반영되지 않는 erratum(837070)이 있어 FreeRTOS 의 `ARM_CM7/r0p1` 포트는 `cpsid i; msr basepri; dsb; isb; cpsie i` 로 감싼다 (둘 다 기억에 의존, 확인 권장). QEMU 는 이런 차이를 보여 주지 않는다.

### Cortex-M0 에는 없다 (`run_qemu.sh m0`, 실제 출력)

```text
Attempt to write unknown special register 17
Attempt to read unknown special register 17
Attempt to write unknown special register 19
Attempt to read unknown special register 19
M0: BASEPRI after msr 0x80 = 0x00000000  FAULTMASK after cpsid f = 0x00000000
qemu exit=0
```

ARMv6-M (M0/M0+) 에는 BASEPRI·FAULTMASK 가 없다. 놀랍게도 GNU as 2.45.50 (VM 의 arm-none-eabi binutils) 은 `.cpu cortex-m0` 에서도 `msr basepri_max` 와 `cpsid f` 를 오류 없이 어셈블했다 (인코딩 `f380 8812`, `b671`). QEMU `-M microbit` 는 경고를 찍고 무시하며 0 을 읽는다 (아키텍처상 동작은 UNPREDICTABLE 계열로 기억 — 확인 권장). 그래서 M0 용 RTOS 포트는 PRIMASK 만 쓴다.

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"    # 마지막 두 줄: 35/35 passed, qemu exit=0
clang --target=thumbv7m-none-eabi -c $L/solution.S -o /tmp/ARM-M10_solution.o   # 맥: 어셈블 확인
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 (실측) | 고친 코드 |
|---|---|---|
| `crit_exit` 가 `cpsie i` | 중첩 안쪽 exit 이 바깥 섹션을 연다 → 주입 20/20 lost | `msr primask, r0` (저장값 복원) |
| `cpsid i` 를 먼저, `mrs` 를 나중에 | 저장값이 항상 1 → 바깥 exit 이 인터럽트를 영원히 안 켬 | `mrs r0, primask` → `cpsid i` |
| 안쪽 섹션에서 `msr basepri, x` | 바깥이 막던 범위를 낮춤 → 0x80 IRQ 10/10 lost | `msr basepri_max, x` + 옛 값 복원 |
| BASEPRI 방향을 반대로 | BASEPRI=0xC0 은 0xA0 ISR 을 못 막음 → 100/100 lost | 막을 ISR 우선순위 **이하** 값(= 같거나 더 급한 값)으로 |
| 논리 우선순위를 시프트 안 하고 씀 | 3비트 칩에서 `IPR = 5` → 0x00 (최고 우선순위!) | `prio_encode(5, 3) = 0xA0` |
| 범위 밖 논리값 | `prio_encode(9, 3)` → 0x20: 덜 급하게 하려던 것이 매우 급해짐 | 설정 시 `assert(logical < (1u << bits))` |
| BASEPRI 섹션이 모터 데이터도 지킨다고 믿음 | 모터(0x20)는 BASEPRI=0x40 을 통과 → 50/50 lost | 모터와 공유하는 데이터는 PRIMASK 나 LDREX/STREX (ARM-M11) |
| inline asm 에 `"memory"` 없음 | -O2 가 증가를 섹션 밖으로 빼서 섹션이 텅 빔 (`demo_clobber.c`) | `: : : "memory"` |
| QEMU 기본 모드 스트레스 통과 = 안전 | 기본 TCG 는 보호 없어도 lost 0 | 결정적 주입 + icount, 결국 실보드 |
| PRIMASK 섹션 안에서 RTOS API(SVC) 호출 | SVC 를 받을 수 없어 HardFault(FORCED) — 연습 3 실측 | 섹션 밖에서 호출, 또는 FromISR 류 API |

## 8. 면접 Q&A

<details><summary>Q1. PRIMASK, BASEPRI, FAULTMASK 차이를 한 문장씩?</summary>

PRIMASK=1 은 실행 우선순위를 0 으로 올려 설정 가능한 모든 예외(IRQ, SysTick, PendSV, SVC, 설정 가능한 fault)를 막는다 — NMI·HardFault 만 통과. BASEPRI=B 는 우선순위 값이 B 이상인 것만 막고 더 급한 것은 통과시킨다 (0 = 아무것도 안 막음). FAULTMASK=1 은 실행 우선순위를 -1 로 올려 HardFault 까지 막고 NMI 만 통과시킨다 (연습 2 실측: 우선순위 0x00 IRQ 도 대기). 꼬리: "FAULTMASK 는 언제 쓰나?" → fault 핸들러가 다른 fault 를 피하며 정리하는 특수 상황, 예외 복귀 시 자동으로 0 이 된다 (NMI 복귀 제외 — ARMv7-M ARM, 확인 권장). 일반 코드에서는 거의 안 쓴다.

</details>

<details><summary>Q2. crit_exit 에서 왜 cpsie i 를 쓰면 안 되나? 중첩이 실제로 언제 생기나?</summary>

섹션 안에서 부른 함수가 자기도 섹션을 쓰면 중첩이다 (로그·큐 push·드라이버 레지스터 갱신 함수). 안쪽이 `cpsie i` 로 끝나면 바깥 섹션의 나머지가 보호 없이 돈다 — 이 레슨에서 20/20 lost. 또 ISR 안에서 부른 함수가 `cpsie i` 를 하면 "ISR 안에서 인터럽트를 켜는" 셈이 된다 (Cortex-M 은 우선순위로 중첩되므로 치명적이진 않지만 의도와 다름). 저장/복원 패턴은 호출자 상태를 모른 채로도 정확하다. 꼬리: "카운터 방식(nesting count)과 비교?" → 카운터는 전역 상태가 필요하고 ISR/스레드가 공유하면 그 자체가 보호 대상이다. 저장값은 각 호출의 지역 변수(레지스터)라 그런 문제가 없다.

</details>

<details><summary>Q3. BASEPRI 에 0x40 을 쓰면 우선순위 0x40 인 IRQ 는 막히나? 0 을 쓰면?</summary>

막힌다. 새 예외는 실행 우선순위보다 **엄격히 더 급할 때만** 선점하므로 값 ≥ BASEPRI 는 막힌다 (이 레슨: BASEPRI=0xA0 이 0xA0 ISR 을 막아 lost 0). 0 은 특별값으로 "마스크 없음" 이다 — 그래서 BASEPRI 로는 우선순위 0 인 IRQ 를 막을 방법이 없다 (막으려면 PRIMASK). 꼬리: "구현 비트가 3 인 칩에서 0x45 를 쓰면?" → 아래 비트는 RAZ/WI 라 0x40 이 된다. QEMU 는 8 비트를 구현해서 0x45 가 그대로 읽힌다 — 에뮬레이터에서 통과한 코드가 실칩에서 달라지는 지점.

</details>

<details><summary>Q4. BASEPRI_MAX 는 왜 있나? 그냥 MRS 해서 비교하고 쓰면 안 되나?</summary>

읽기-비교-쓰기 세 단계 사이에 인터럽트가 들어와 BASEPRI 를 바꾸면(ISR 이 자기 섹션을 열고 닫는 경우는 복원하므로 대개 괜찮지만) 레이스 창이 생기고 명령도 많다. `MSR BASEPRI_MAX` 는 "새 값이 0 이 아니고 현재보다 급하거나 현재가 0 일 때만 쓴다" 를 한 명령으로 한다. 그래서 `basepri_enter` = `mrs` + `msr basepri_max` 두 줄로 중첩이 된다. 실측: 0x40 상태에서 0x80 → 그대로, 0x20 → 0x20, 0 → 그대로. 꼬리: "BASEPRI_MAX 로 마스크를 풀 수 있나?" → 없다. 푸는 것은 `msr basepri` (복원)뿐.

</details>

<details><summary>Q5. FreeRTOS(또는 Zephyr)는 왜 Cortex-M3/M4 에서 PRIMASK 대신 BASEPRI 를 쓰나?</summary>

커널 크리티컬 섹션이 모든 인터럽트를 막으면, 커널과 무관한 초고속 ISR(모터 PWM, 고속 ADC, 통신 PHY)까지 지터를 먹는다. BASEPRI 로 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 이상(덜 급한 쪽)만 막으면 그보다 급한 ISR 은 커널 섹션 중에도 바로 돈다 — 대신 그 ISR 들은 커널 API 를 부르면 안 된다. 이 레슨 Part B 의 "모터 1 회 실행, 통신 대기" 가 그 구조다. Zephyr 도 ARMv7-M 에서 `irq_lock()` 을 BASEPRI 로 구현하고 "zero-latency IRQ" 옵션을 둔다 (기억 — 확인 권장). M0(ARMv6-M) 포트는 BASEPRI 가 없어서 PRIMASK 를 쓴다 (`run_qemu.sh m0`: 레지스터 17·19 는 무시되고 0 이 읽힘).

</details>

<details><summary>Q6. 크리티컬 섹션 안에서 fault 나 NMI 가 나면?</summary>

PRIMASK 는 실행 우선순위를 0 으로만 올리므로 NMI(-2)와 HardFault(-1)는 그대로 들어온다. 실측: NMI 핸들러 안에서 읽은 PRIMASK 는 1 (마스크 레지스터는 예외 진입 때 바뀌지 않는다), `udf` 는 HFSR.FORCED + CFSR.UNDEFINSTR 로 HardFault. 그래서 NMI 와 공유하는 데이터는 PRIMASK 로 보호할 수 없다 → 락 없는 구조(단일 쓰기 변수, LDREX/STREX, 이중 버퍼)로. FAULTMASK 상태에서 fault 가 나면 HardFault 로도 갈 수 없어 lockup 이 된다 (ARMv7-M ARM, 이 레슨에서 실험 안 함).

</details>

<details><summary>Q7. PRIMASK 섹션 안에서 SVC(예: RTOS API)를 부르면?</summary>

SVCall 의 우선순위가 실행 우선순위(0)보다 급하지 않으니 받을 수 없고, 동기 예외는 미룰 수 없으므로 HardFault 로 승격된다. QEMU 실측 (연습 3): HFSR = 0x40000000 (FORCED 만), CFSR = 0, stacked PC = SVC **다음** 명령 (`svc_insn + 2`) — 그래서 핸들러가 PC 를 건너뛸 필요가 없었다. 실제 RTOS 에서 "섹션 안에서 `xQueueSend` 불렀더니 HardFault" 의 정체다. 꼬리: "ISR 안에서 SVC 는?" → 같은 원리로 SVCall 우선순위가 현재 핸들러보다 급하지 않으면 HardFault (ARM-M09).

</details>

<details><summary>Q8. 마스크하면 인터럽트를 잃나? 얼마나 길게 막아도 되나?</summary>

잃지 않는다 — NVIC 의 pending 비트가 기억했다가 마스크가 풀리는 즉시 실행한다 (실측: PRIMASK 주입 100 회 동안 ISR 도 100 회 모두 실행). 단 pending 비트는 IRQ 당 1 개라서 막힌 동안 같은 IRQ 가 두 번 오면 한 번으로 합쳐진다 (UART 바이트 유실, 타이머 틱 누락). 허용 시간 = "가장 빠른 막히는 IRQ 의 주기와 데드라인" 에서 거꾸로 계산한다 — 예: 115200 bps UART 에 FIFO 가 없으면 바이트 간격 약 87 µs 안에 읽어야 한다. 섹션은 레지스터 몇 개 갱신 수준으로 짧게, 긴 일은 섹션 밖으로.

</details>

<details><summary>Q9. LDREX/STREX 가 있는데 왜 아직 인터럽트를 끄나?</summary>

LDREX/STREX(ARM-M11)는 한 워드의 RMW 를 락 없이 원자화하지만, 여러 필드·여러 워드(예: 링버퍼 head 와 데이터, 64비트 틱 — 연습 5)를 한 번에 일관되게 바꾸기는 어렵고 재시도 루프가 생긴다. 또 ARMv6-M 에는 없다. 인터럽트 마스크는 단일 코어에서 임의 길이의 코드를 원자적으로 만들지만 지연 비용이 있고 멀티코어에서는 다른 코어를 못 막는다 (스핀락 + 마스크 조합 필요). 그래서 "짧은 단일 워드 → LDREX/STREX, 여러 필드 → 짧은 BASEPRI/PRIMASK 섹션" 이 보통 답이다.

</details>

## 9. 추가 연습

### 연습 1 — 구현된 우선순위 비트 수 탐침

`uint32_t prio_bits_probe(volatile uint8_t *ipr)` 를 asm 으로: 원래 값을 보관하고 0xFF 를 써서 다시 읽은 뒤, 위쪽 1 의 개수를 세고 원래 값을 복원한다.

<details><summary>답</summary>

`mvn r1, r1, lsl #24` 로 "읽은 바이트를 맨 위로 올리고 반전" 하면 위쪽 1 들이 위쪽 0 이 되고, `clz` 가 그 개수를 센다. 3비트 칩: 0xE0 → `~0xE0000000 = 0x1FFFFFFF` → clz 3. QEMU 실측: `implemented priority bits (QEMU) = 8`. 부팅 때 이 값을 `__NVIC_PRIO_BITS` 와 비교하는 assert 는 잘못된 보드 설정을 잡는 싼 방법이다.

</details>

### 연습 2 — FAULTMASK 섹션

`fault_enter()/fault_exit()` (`mrs faultmask` + `cpsid f` / `msr faultmask`). 섹션 안에서 우선순위 **0x00** IRQ 와 NMI 를 동시에 pend 하면?

<details><summary>답</summary>

실측: IRQ 0 회, NMI 1 회 실행 → exit 후 IRQ 1 회. PRIMASK 로도 0x00 IRQ 는 막히지만 FAULTMASK 는 추가로 HardFault 와 설정 가능한 fault 까지 막는다. 그래서 일반 크리티컬 섹션 용도로는 과하다.

</details>

### 연습 3 — PRIMASK 안에서 SVC

`svc_call()` (`svc #7`) 를 `crit_enter/exit` 안에서 부르면 무엇이 실행되나? HFSR/CFSR/stacked PC 는?

<details><summary>답</summary>

SVC_Handler 는 안 불리고 HardFault 가 온다. 실측: `HFSR = 0x40000000`, `CFSR = 0x00000000`, `stacked PC = 0x000004a4` (`svc_insn = 0x000004a2` 의 다음). 섹션 밖에서 다시 부르면 SVC_Handler 가 정상 실행된다.

</details>

### 연습 4 — ISR 안에서의 BASEPRI 섹션 (FreeRTOS `..._FROM_ISR` 패턴)

IRQ6(0xA0) 핸들러 안에서 `old = basepri_enter(0x40)` 을 하고 모터(0x20)를 pend 하면? `old` 는?

<details><summary>답</summary>

`old = 0` (BASEPRI 는 예외 진입 때 안 바뀐다), 모터는 섹션 안에서 바로 선점 실행(1 회). 핸들러 안의 섹션도 같은 저장/복원 패턴이면 된다. FreeRTOS 의 `taskENTER_CRITICAL_FROM_ISR()` 이 옛 BASEPRI 를 반환하는 이유다.

</details>

### 연습 5 — 64비트 틱 카운터 읽기

ISR 이 `lo`, `hi` 두 워드로 된 틱을 올린다 (lo 가 0 으로 넘어가면 hi++). main 의 `lo` 읽기와 `hi` 읽기 사이에 ISR 이 끼면? 크리티컬 섹션 판과 락 없는 hi-lo-hi 판을 만들어라.

<details><summary>답</summary>

`5:FFFFFFFF` 에서 naive 읽기는 `6:FFFFFFFF` (실제로 존재한 적 없는 값, 약 43 억 틱 미래)를 돌려준다. 크리티컬 섹션 판은 `5:FFFFFFFF` (읽은 순간의 일관된 값, ISR 은 exit 뒤 실행), hi-lo-hi 판은 hi 가 바뀐 것을 보고 재시도해 `6:00000000`. 세 결과 모두 CHECK 로 고정했다. hi-lo-hi 는 "ISR 이 한 번의 재시도 동안 두 번 넘기지 않는다" 는 가정이 필요하다 (32비트 lo 면 충분히 참).

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # practice.c + practice.S + shared_rmw.S + shared_startup.c → 12/12 passed
```

## 10. 핵심 정리

- `crit_enter = mrs r0, primask; cpsid i` / `crit_exit = msr primask, r0` — **복원**이지 켜기가 아니다. `cpsie i` exit 은 중첩을 깬다 (20/20 lost).
- BASEPRI=B 는 값 ≥ B 를 막는다. 0 = 아무것도 안 막음. 더 급한 ISR(모터)은 섹션 중에도 돈다 — 그 ISR 과 공유하는 데이터는 못 지킨다.
- 중첩 BASEPRI 는 `msr basepri_max` (올리기만) + 옛 값 복원. 그냥 쓰기는 바깥 마스크를 낮춘다.
- PRIMASK 는 NMI·HardFault 를 못 막는다. FAULTMASK 는 HardFault 까지, NMI 만 통과. 섹션 안 SVC → HardFault(FORCED).
- 우선순위 바이트는 위쪽 비트부터 구현: `logical << (8 − bits)`. QEMU 8 비트, 실칩 3~4 비트 — 0x20 배수 같은 "공통 값" 으로 테스트.
- 마스크는 인터럽트를 미룰 뿐 버리지 않지만 pending 은 1 개 → 섹션은 짧게.
- inline asm 섹션엔 `"memory"` clobber 필수 (없으면 -O2 가 섹션을 비운다).
- QEMU 로 레이스를 세려면: 결정적 주입(`str ISPR; dsb; isb`) + `-icount` + 위상 섞기. 기본 TCG 는 lost 0 으로 거짓 안심을 준다.

## 참고자료

- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403E) — 특수 레지스터(PRIMASK/BASEPRI/FAULTMASK), 실행 우선순위와 선점, MRS/MSR·CPS, NVIC (절 번호 B1.4.3 / B1.5.4 / B5.2 / B3.4 는 기억 — 확인 권장): <https://developer.arm.com/documentation/ddi0403/latest>
- Arm, *Cortex-M3 Devices Generic User Guide* (DUI 0552) — 4.2 NVIC, 2.1.3 core registers (BASEPRI_MAX): <https://developer.arm.com/documentation/dui0552/latest>
- Arm, *AN321 ARM Cortex-M Programming Guide to Memory Barrier Instructions*: <https://developer.arm.com/documentation/dai0321/latest>
- Arm, *Cortex-M7 Software Developer Errata Notice* — erratum 837070 (BASEPRI 상승이 즉시 반영 안 됨, r0p1; 문서 번호는 developer.arm.com 에서 "Cortex-M7 errata" 검색)
- CMSIS-Core(M) `cmsis_gcc.h` (`__disable_irq`, `__set_BASEPRI_MAX`): <https://github.com/ARM-software/CMSIS_5>
- FreeRTOS, "Running the RTOS on a ARM Cortex-M Core" (`configMAX_SYSCALL_INTERRUPT_PRIORITY`): <https://www.freertos.org/RTOS-Cortex-M3-M4.html>
- QEMU — Arm system emulator, `mps2-an385`, `microbit`, `-icount`, `-accel tcg,one-insn-per-tb=on`: <https://www.qemu.org/docs/master/system/target-arm.html>, <https://www.qemu.org/docs/master/devel/tcg-icount.html>
- 관련 레슨: C-M14 (PRIMASK C 목), ARM-M11 (LDREX/STREX), BM-M02 (우선순위·PRIGROUP), ARM-B14 (MRS/MSR 레지스터 모델), ARM-M09 (예외 진입·SVC), ARM-M12 (HardFault 분석)
