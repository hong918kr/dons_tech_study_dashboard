---
id: ARM-A03
time: 120분
updated: 2026-11-15
---
# ARM-A03 AArch64 MMU 켜기: 변환 테이블·MAIR·TCR

## 학습 목표

- 4 KB 그래뉼, 39 비트 VA(T0SZ = 25)에서 **L1 테이블 → L2 테이블 → 2 MB 블록** 구조를 직접 만들고, descriptor 의 비트(AttrIndx · AP · SH · AF · PXN/UXN · 출력 주소)를 한 비트씩 설명한다.
- `MAIR_EL1`(속성 인덱스 → 메모리 타입), `TCR_EL1`(T0SZ · IPS · 그래뉼 · 캐시성/공유성 · EPD1), `TTBR0_EL1`, `SCTLR_EL1.M/C/I` 를 **올바른 순서와 배리어**로 써서 MMU 를 켜고, 아이덴티티 매핑 덕분에 PC 가 살아남는 이유를 말한다.
- C 로 **소프트웨어 page walk** 를 짜서 하드웨어 walker(`AT S1E1R/W` → `PAR_EL1`)와 6150 개 VA 에서 비교한다.
- 폴트를 실측으로 분류한다: translation(레벨 0/1/2/3), permission(쓰기 RO, XN 실행 = **EC 0x21**), access flag, alignment(Device 의 비정렬), 외부 어보트(PA 에 아무것도 없음).
- 운영 중 매핑 바꾸기 — AF 지연 설정과 **break-before-make + TLBI** — 를 구현하고, TLBI 를 빼면 QEMU 에서도 **옛 변환이 남는 것**을 직접 본다.

## 1. 문제

"MMU 켜는 순서 말해 봐. MAIR 인덱스 틀리면? TLBI 는 왜?" — Cortex-A 부트로더(U-Boot, TF-A), 리눅스 `head.S`, RTOS 의 MPU 대신 MMU 를 쓰는 SoC(i.MX8, Jetson, 차량용 SoC), 그리고 SSD 컨트롤러처럼 Cortex-R/A 위에서 DMA 버퍼 캐시 속성을 다루는 펌웨어 면접에서 반드시 나오는 질문이다. MMU 를 켜는 코드는 몇십 줄이지만 **한 비트만 틀려도 켠 순간 아무 출력 없이 멈춘다** — 그래서 이 레슨은 하드웨어를 켜기 전에 소프트웨어 walker 로 미리 검사(preflight)하고, 켠 뒤에는 `AT` 명령으로 하드웨어에게 직접 물어 맞춰 본다.

ARM-M13 에서 만든 베어메탈 이미지(QEMU `-M virt -cpu cortex-a53`, EL1, 세미호스팅)를 축약해서 그대로 쓴다. M13 은 MMU off 였다 — 그래서 데이터 접근이 전부 Device 메모리 취급이라 비정렬 접근이 폴트였다. 이번에는 MMU 를 켜고, 메모리 맵을 우리가 정한다.

```svg
<svg viewBox="0 0 760 430" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="14" font-weight="bold">이 레슨의 VA → PA 맵 (TTBR0, T0SZ = 25, 4 KB 그래뉼)</text>
  <text x="20" y="40" fill="currentColor" font-weight="bold">VA (가상)</text>
  <text x="520" y="40" fill="currentColor" font-weight="bold">PA (QEMU virt)</text>
  <rect x="20" y="50" width="250" height="26" class="f-mut" stroke="currentColor"/>
  <text x="28" y="67" fill="currentColor" font-family="monospace">0x08000000 GIC  2MB Dev XN</text>
  <rect x="20" y="78" width="250" height="26" class="f-mut" stroke="currentColor"/>
  <text x="28" y="95" fill="currentColor" font-family="monospace">0x09000000 UART 2MB Dev XN</text>
  <rect x="20" y="106" width="250" height="22" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="28" y="121" fill="currentColor" font-family="monospace">0x0A000000 구멍 → L2 fault</text>
  <rect x="20" y="134" width="250" height="26" class="f-ok" stroke="currentColor"/>
  <text x="28" y="151" fill="currentColor" font-family="monospace">0x40000000.. RAM Normal WB</text>
  <rect x="20" y="162" width="250" height="22" class="f-warn" stroke="currentColor"/>
  <text x="28" y="177" fill="currentColor" font-family="monospace">0x46000000 XN (실행 금지)</text>
  <rect x="20" y="186" width="250" height="22" class="f-warn" stroke="currentColor"/>
  <text x="28" y="201" fill="currentColor" font-family="monospace">0x46200000 RO (AP=10) XN</text>
  <rect x="20" y="210" width="250" height="22" class="f-warn" stroke="currentColor"/>
  <text x="28" y="225" fill="currentColor" font-family="monospace">0x46400000 AF = 0</text>
  <rect x="20" y="234" width="250" height="22" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="28" y="249" fill="currentColor" font-family="monospace">0x46600000 구멍 (invalid)</text>
  <rect x="20" y="258" width="250" height="22" class="f-ok" stroke="currentColor"/>
  <text x="28" y="273" fill="currentColor" font-family="monospace">0x46800000 SHARED (아이덴티티)</text>
  <rect x="20" y="290" width="250" height="22" class="f-acc" stroke="currentColor"/>
  <text x="28" y="305" fill="currentColor" font-family="monospace">0x48000000 ALIAS Normal</text>
  <rect x="20" y="314" width="250" height="22" class="f-acc" stroke="currentColor"/>
  <text x="28" y="329" fill="currentColor" font-family="monospace">0x48200000 DEVALIAS Device</text>
  <rect x="20" y="338" width="250" height="22" class="f-acc" stroke="currentColor"/>
  <text x="28" y="353" fill="currentColor" font-family="monospace">0x48400000 REMAP (Part C)</text>
  <rect x="20" y="368" width="250" height="22" class="f-acc" stroke="currentColor"/>
  <text x="28" y="383" fill="currentColor" font-family="monospace">0x80000000 1 GB 블록 (L1)</text>
  <rect x="20" y="394" width="250" height="22" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="28" y="409" fill="currentColor" font-family="monospace">0xC0000000.. L1 invalid</text>
  <rect x="520" y="50" width="220" height="54" class="f-mut" stroke="currentColor"/>
  <text x="528" y="72" fill="currentColor" font-family="monospace">0x08000000 GICv2</text>
  <text x="528" y="92" fill="currentColor" font-family="monospace">0x09000000 PL011</text>
  <rect x="520" y="134" width="220" height="200" class="f-ok" stroke="currentColor"/>
  <text x="528" y="152" fill="currentColor" font-family="monospace">RAM 0x40000000</text>
  <text x="528" y="170" fill="currentColor" font-family="monospace">.. 0x47FFFFFF (128 MB)</text>
  <text x="528" y="273" fill="currentColor" font-family="monospace">0x46800000 SHARED</text>
  <text x="528" y="295" fill="currentColor" font-family="monospace">0x46A00000 SPARE</text>
  <rect x="520" y="350" width="220" height="40" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="528" y="374" fill="currentColor" font-family="monospace">0x48000000.. 빈 PA</text>
  <path d="M270 268 L520 268" stroke="currentColor" fill="none"/>
  <path d="M270 300 L500 270 L520 270" stroke="currentColor" fill="none"/>
  <path d="M270 325 L505 272 L520 272" stroke="currentColor" fill="none"/>
  <path d="M270 348 L510 290 L520 290" stroke="currentColor" stroke-dasharray="5 3" fill="none"/>
  <path d="M270 378 L470 150 L520 150" stroke="currentColor" fill="none"/>
  <text x="300" y="222" fill="currentColor">별칭 3 개가 같은 PA 로</text>
  <text x="300" y="238" fill="currentColor">(점선 = Part C 재매핑 후)</text>
  <text x="280" y="400" fill="currentColor">1 GB 블록 = RAM 전체 + 그 뒤 빈 PA(외부 어보트)</text>
  <text x="280" y="420" fill="currentColor">VA ≥ 2^39 → 레벨 0 translation fault</text>
</svg>
```

### Part A — 테이블 만들고 MMU 켜기

- `mk_block(pa, attr_idx, ap, sh, xn, af)`, `mk_table(next)` (C): descriptor 64 비트를 만든다. 골든 값으로 검사 (`0x40000701` 등).
- `make_tcr(t0sz, mmfr0)` (C): `ID_AA64MMFR0_EL1.PARange` 를 읽어 IPS 로 쓰는 TCR 값. QEMU cortex-a53 은 PARange = 2 (40 비트) — 실측.
- `build_tables()` (C): 위 그림 그대로. L1[0] → `l2_low`(GIC, UART), L1[1] → `l2_ram`(RAM 64 블록 + 특수 블록 + 별칭), L1[2] = 1 GB 블록.
- `walk(root, t0sz, va, &res)` (C): 소프트웨어 page walk. 결과 = 0 또는 DFSC 와 같은 폴트 코드. 0..4 GB 의 모든 2 MB 블록 시작 + 블록마다 무작위 오프셋 2 개 + 범위 밖 VA 6 개 = **6150 개 VA** 를 "명세"(`spec()`, 테이블과 따로 쓴 표)와 비교.
- `mmu_enable(ttbr0, tcr, mair)` (asm): MAIR → TCR → TTBR0 → `dsb ish; isb` → `tlbi vmalle1` → `dsb; isb` → SCTLR.M|C|I → `isb`.
- `at_s1e1r/at_s1e1w(va)` (asm): `at s1e1r, x0; isb; mrs x0, par_el1`. MMU 를 켠 뒤 6150 개 VA 에서 **AT 결과 = walk 결과**.

### Part B — 폴트와 함정 (전부 실측)

- 구멍 / L1 invalid / VA ≥ 2^39 → translation fault 레벨 2 / 1 / 0, FAR = 그 주소, ELR = `ldr` 자신.
- RO 블록에 쓰기 → permission fault (WnR = 1), 값은 그대로. AF = 0 블록 읽기 → access flag fault.
- XN 블록으로 `blr` → **instruction abort EC 0x21**, ELR = FAR = 목적지. 핸들러가 ELR = LR 로 "return" 시켜 복구. 같은 코드 바이트를 실행 가능한 블록에 두면 42 를 돌려준다.
- 비정렬 `ldr`: Normal 은 통과(SCTLR.A = 0), 같은 PA 의 Device 별칭은 alignment fault(DFSC 0x21).
- 1 GB 블록으로 PA 0x50000000(RAM 밖) 읽기 → 변환은 성공, **외부 어보트 DFSC 0x10**.
- 버그 주입 (`run_qemu.sh bug_*`): TLBI 빼기, AF 빼기, UART 를 MAIR 인덱스 0(Normal)으로, T0SZ = 16, ISB 빼기 → 무엇이 보이고 무엇이 안 보이는지 (7.).

### Part C — 운영 중에 매핑 바꾸기

- `af_fixup(far)` (C): AF 폴트가 나면 그 descriptor 에 AF 를 켜고 **같은 명령을 재실행** (ELR 그대로) — 리눅스가 페이지 "young" 비트를 소프트웨어로 관리하는 것과 같은 모양.
- `remap_block(va, new_desc)` (C): **break-before-make** — invalid 로 → `dsb ishst; tlbi vaae1is; dsb ish; isb` → 새 descriptor → `dsb ishst; isb`. VA_REMAP 을 SHARED → SPARE 로 바꾸고 새 데이터가 보이는지.
- 관찰: TLBI 없이 descriptor 만 바꾸면 QEMU 에서 load 는 **옛 PA**, AT 는 **새 PA** 를 본다.

| 파일 | 역할 |
|---|---|
| `starter.c` / `solution.c` | 테이블·walk·TCR·AF·BBM (TODO / 정답) + 예외 핸들러(주어짐) + 펌웨어 테스트(main) |
| `starter.S` / `solution.S` | `mmu_enable`, `at_s1e1r/w` (TODO / 정답) + 시스템 레지스터 읽기 · TLBI · 폴트용 load/store · 벡터 (주어짐) |
| `shared_start.S` / `shared_rt.c` | 부트 · 세미호스팅 · `check()`/`finish()` (ARM-M13 축약판) |
| `arm_a03.ld` | 0x40080000 링크, 64 KB 스택. 이미지 전체가 RAM 첫 2 MB 블록 안 |
| `run_qemu.sh` | VM 안에서 빌드·실행: `starter|solution|practice|gdb|objdump|bug_*` |
| `demo_mmu.gdb` / `demo_mmu_out.txt` | gdb 로 본 MMU 레지스터·테이블·VA/PA 읽기 (실제 출력) |
| `practice.c` / `practice.S` | 추가 연습 답 (solution 을 `#include` 해서 이어 붙임) |

## 2. Problem (English)

> **Turn on the AArch64 MMU, bare metal.** Target: QEMU `-M virt -cpu cortex-a53`, entered at EL1 with the MMU off, semihosting output, your own linker script (image at 0x40080000).
>
> 1. `uint64_t mk_block(uint64_t pa, unsigned attr_idx, unsigned ap, unsigned sh, unsigned xn, unsigned af)` and `uint64_t mk_table(const void *next)` — stage-1 VMSAv8-64 descriptors, 4 KB granule. `xn` sets both PXN and UXN.
> 2. `uint64_t make_tcr(unsigned t0sz, uint64_t id_aa64mmfr0)` — TTBR0 only (EPD1 = 1), 4 KB granule, inner-shareable write-back walks, IPS from PARange (cap at 48 bits).
> 3. Build an identity map: RAM `0x40000000`–`0x47FFFFFF` Normal WB (MAIR attr 0xFF) in 2 MB blocks, UART `0x09000000` and GIC `0x08000000` Device-nGnRE (0x04), plus one XN block, one read-only block, one block with AF = 0, one hole, two aliases of the same PA (Normal and Device), and a 1 GB level-1 block at VA `0x80000000` → PA `0x40000000`.
> 4. `unsigned walk(const uint64_t *root, unsigned t0sz, uint64_t va, struct walk_res *out)` — a software table walk returning 0 or the DFSC-style fault code (translation 0x04+level, access flag 0x08+level). Validate it against a hand-written spec and, after the MMU is on, against `AT S1E1R/S1E1W` + `PAR_EL1`.
> 5. `void mmu_enable(uint64_t ttbr0, uint64_t tcr, uint64_t mair)` in assembly. Don't enable unless your walker proves code, stack and vectors are mapped (preflight).
> 6. With a synchronous-exception handler that records ESR/FAR/ELR and skips the access, demonstrate: translation faults at levels 0/1/2, a permission fault on a write to RO memory, an instruction abort (EC 0x21) when branching into XN memory, an access-flag fault, an alignment fault on unaligned Device access (and none on Normal), and an external abort through a valid mapping to absent PA.
> 7. Implement lazy access-flag setting (fix and retry) and `remap_block()` with break-before-make.
>
> Example (real run): `ID_AA64MMFR0_EL1 = 0x1122` → TCR `0x280803519`; `SCTLR_EL1` `0xc50838` → `0xc5183d`; `PAR_EL1(UART) = 0x0400000009000b00`; branching into the XN block → `ESR.EC = 0x21, IFSC = 0x0E, ELR = FAR = 0x46000000`.
>
> Follow-ups: Why must the code be identity-mapped? What breaks if MAIR index and descriptor disagree? Why TLBI before turning on, and after every change? Who sets the AF on ARMv8.0? What does break-before-make prevent?

## 3. 시작 코드

C 쪽 (TODO 7 개: `mk_block`, `mk_table`, `make_tcr`, `build_tables`, `walk`, `af_fixup`, `remap_block`):

<!-- code: starter.c -->

어셈블리 쪽 (TODO: `mmu_enable`, `at_s1e1r`, `at_s1e1w`):

<!-- code: starter.S -->

주어진 부트 코드·런타임·링커 스크립트 (TODO 없음, ARM-M13 축약판):

<!-- code: shared_start.S -->

<!-- code: shared_rt.c -->

<!-- code: arm_a03.ld -->

빌드·실행은 Lima VM `elinux`(aarch64 Ubuntu, gcc 15.2, qemu-system-aarch64 10.2.1) 안에서 한다. VM 의 네이티브 gcc 가 곧 AArch64 컴파일러라 `-ffreestanding -nostdlib -nostartfiles -static -T arm_a03.ld` 로 베어메탈 ELF 를 만든다(플래그 이유는 ARM-M13 3. 참고). Mac 홈은 VM 에서 읽기 전용이라 스크립트가 `/tmp/ARM-A03_work` 로 복사해서 빌드한다. 맥에서 한 줄:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ARM/ARM-A03
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"   # solution / practice / gdb / objdump / bug_noaf / bug_t0sz / bug_mair / bug_noisb / bug_notlbi
```

<!-- code: run_qemu.sh -->

M13 과 다른 점: `-serial file:uart_<v>.txt` — 펌웨어가 Device 매핑을 통해 PL011 데이터 레지스터에 쓴 글자를 파일로 받아, 실행 뒤에 보여 준다.

처음 실행 결과 (실제 출력, 앞부분 생략):

```text
FAIL line 279: tcr == 0x0000000280803519ull
...
  walk vs spec: VAs = 6150, mapped = 0, mismatches = 6145
FAIL line 302: bad == 0u
...
PASS walk(l1, T0SZ, 1ull << 39, &w) == FSC_TRANS(0)
...
FAIL line 330: ready
  preflight failed: identity map would not cover code/stack/vectors -> MMU stays off
6/24 passed
--- PL011 (uart_starter.txt) ---
qemu exit=1
```

**안전장치 (preflight)**: MMU 를 켜기 전에 `walk()` 로 `_start`, `main`, 벡터, 예외 핸들러(실행 가능해야), 전역 변수·스택·테이블·UART(쓰기 가능해야)가 아이덴티티로 매핑됐는지 검사하고, 하나라도 아니면 MMU 를 켜지 않는다. 테이블이 틀린 채로 켜면 **켜는 순간의 다음 명령부터 폴트 → 벡터 자신도 폴트 → 영원히** 이기 때문이다(7. 실수 2·4 의 실측 로그). 스텁 `walk` 가 항상 폴트를 돌려주므로 starter 는 여기서 멈춘다. starter 의 6 개 PASS 는 ID 레지스터 검사 5 개 + 범위 밖 VA 검사 1 개(스텁의 반환값이 우연히 맞음)다.

VM 없이 맥에서 "어셈블은 되나?" 만 확인:

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
clang --target=aarch64-none-elf -c solution.S -o /tmp/ARM-A03_solution.o && /usr/bin/objdump -d /tmp/ARM-A03_solution.o
```

```text
00000000000000bc <mmu_enable>:
      bc: d518a202     	msr	MAIR_EL1, x2
      c0: d5182041     	msr	TCR_EL1, x1
      c4: d5182000     	msr	TTBR0_EL1, x0
      c8: d5033b9f     	dsb	ish
      cc: d5033fdf     	isb
      d0: d508871f     	tlbi	vmalle1
     ...
      f8: d5181003     	msr	SCTLR_EL1, x3
      fc: d5033fdf     	isb
     100: d65f03c0     	ret
```

(starter.S, practice.S 도 같은 명령으로 어셈블된다 — 실측.)

## 4. 힌트

<details>
<summary>힌트 1 — descriptor 는 "주소 + 아래쪽 속성 + 위쪽 속성"</summary>

블록 descriptor: [1:0] = 01, [4:2] AttrIndx, [5] NS, [7:6] AP[2:1], [9:8] SH, [10] AF, [11] nG, [47:n] 출력 주소, [53] PXN, [54] UXN. 테이블 descriptor: [1:0] = 11, [47:12] 다음 테이블 주소. L3 에서는 [1:0] = 11 이 "페이지" 다. 비트 하나씩 OR 하면 된다. `0x40000701` 을 손으로 풀어 보고 시작하자.
</details>

<details>
<summary>힌트 2 — T0SZ 가 시작 레벨을 정한다</summary>

VA 비트 수 = 64 − T0SZ. 4 KB 그래뉼은 12 비트 오프셋 + 레벨마다 9 비트. 39 비트면 12 + 9 + 9 + 9 → L1, L2, L3 세 단계 = **L1 에서 시작**. 레벨 l 의 인덱스 = `(va >> (39 − 9·l)) & 511`. 범위 밖(VA[63:39] ≠ 0) 은 테이블을 보기도 전에 레벨 0 translation fault.
</details>

<details>
<summary>힌트 3 — walk 의 종료 조건</summary>

각 레벨에서: bit 0 = 0 → translation fault(이 레벨). L0–L2 에서 [1:0] = 11 → 다음 테이블로. L1/L2 에서 [1:0] = 01 → 블록(오프셋 비트 = shift). L0 의 01, L3 의 01 → invalid. 성공했는데 AF = 0 이면 access flag fault(이 레벨). 권한(AP, PXN)은 접근 종류에 따라 따로 판정한다 (`el1_access`).
</details>

<details>
<summary>힌트 4 — MMU 켜는 순서</summary>

"켜기 직전에 모든 것이 이미 맞아 있어야 한다." MAIR/TCR/TTBR0 은 SCTLR.M 보다 먼저, 테이블 쓰기는 `dsb` 로 끝내고, 시스템 레지스터 쓰기는 `isb` 로 확정, 남아 있을지 모르는 TLB 는 `tlbi vmalle1`, 마지막 `msr sctlr_el1` 뒤 `isb`. 이미지가 아이덴티티 매핑이라 `msr` 다음 명령의 VA = PA 이므로 그냥 이어서 실행된다.
</details>

<details>
<summary>힌트 5 — AT 와 PAR_EL1</summary>

`at s1e1r, x0` 는 "EL1 이 읽기로 x0 를 접근한다면?" 을 하드웨어 walker 에게 묻고 결과를 `PAR_EL1` 에 쓴다. 읽기 전에 `isb`. PAR.F (bit 0) = 1 이면 실패, FST = [6:1]. 0 이면 PA = [47:12], ATTR = [63:56] (MAIR 바이트 그대로), SH = [8:7].
</details>

<details>
<summary>힌트 6 — break-before-make</summary>

유효한 매핑의 출력 주소를 바꿀 때: ① 0 을 쓰고 ② 그 VA 를 TLBI(앞뒤 dsb) ③ 새 값을 쓰고 ④ dsb + isb. ②를 빼면 TLB 가 들고 있는 옛 변환이 계속 쓰인다 — 이 레슨에서 실제로 보인다.
</details>

## 5. 풀이

### 0. 규칙 정리 — 근거와 확인 방법

| 사실 | 근거 |
|---|---|
| descriptor 비트 배치, AP/XN/AF/SH 의미, T0SZ ↔ 시작 레벨, TCR/MAIR/SCTLR 필드 | Arm ARM DDI 0487 D8 (VMSAv8-64) — 기억으로 쓴 것, **문서 확인 권장**. 단 값은 전부 QEMU 실측(PAR/ESR)으로 교차 검사했다 |
| QEMU cortex-a53: PARange 40 비트, HAFDBS = 0, 4 KB 지원 | `ID_AA64MMFR0_EL1 = 0x1122`, `ID_AA64MMFR1_EL1 = 0x0` 실측 |
| 각 폴트의 EC/DFSC/FAR/ELR | 펌웨어가 기록한 실측값 (Part B 표) |
| AT 는 AF = 0 엔트리를 성공으로 보고 | QEMU 10.2 실측 + `target/arm/ptw.c` (v10.2.0) 2131–2148 행: AF 검사가 `if (likely(!ptw->in_at))` 안에 있다. **최신 master 에서는 바뀌었다** (AF 검사가 `!ptw->in_debug` 조건 아래로 이동, 2239–2244 행) |
| TLBI 없이 descriptor 를 바꾸면 옛 변환 사용 | QEMU 실측 (Part C) |
| 캐시 속성(MAIR 의 WB vs Device)의 성능·부작용 | QEMU TCG 는 캐시를 모델링하지 않는다 → **실칩 설명은 기억 기반, 확인 권장** |

### Part A — 테이블 만들고 MMU 켜기

**1) descriptor 한 개를 비트로 읽기.** 정답 코드의 골든 값 네 개를 풀면 이렇다.

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="14" font-weight="bold">2 MB 블록 descriptor (stage 1, 4 KB 그래뉼, 레벨 2)</text>
  <rect x="10" y="34" width="60" height="40" class="f-warn" stroke="currentColor"/>
  <text x="16" y="52" fill="currentColor" font-family="monospace">63:55</text>
  <text x="16" y="68" fill="currentColor">SW/무시</text>
  <rect x="70" y="34" width="50" height="40" class="f-acc" stroke="currentColor"/>
  <text x="76" y="52" fill="currentColor" font-family="monospace">54</text>
  <text x="76" y="68" fill="currentColor">UXN</text>
  <rect x="120" y="34" width="50" height="40" class="f-acc" stroke="currentColor"/>
  <text x="126" y="52" fill="currentColor" font-family="monospace">53</text>
  <text x="126" y="68" fill="currentColor">PXN</text>
  <rect x="170" y="34" width="60" height="40" class="f-mut" stroke="currentColor"/>
  <text x="176" y="52" fill="currentColor" font-family="monospace">52:48</text>
  <text x="176" y="68" fill="currentColor">Cont 등</text>
  <rect x="230" y="34" width="180" height="40" class="f-ok" stroke="currentColor"/>
  <text x="236" y="52" fill="currentColor" font-family="monospace">47:21</text>
  <text x="236" y="68" fill="currentColor">출력 주소 (2 MB 정렬)</text>
  <rect x="410" y="34" width="60" height="40" class="f-mut" stroke="currentColor"/>
  <text x="416" y="52" fill="currentColor" font-family="monospace">20:12</text>
  <text x="416" y="68" fill="currentColor">0</text>
  <rect x="470" y="34" width="34" height="40" class="f-mut" stroke="currentColor"/>
  <text x="474" y="52" fill="currentColor" font-family="monospace">11</text>
  <text x="474" y="68" fill="currentColor">nG</text>
  <rect x="504" y="34" width="34" height="40" class="f-acc" stroke="currentColor"/>
  <text x="508" y="52" fill="currentColor" font-family="monospace">10</text>
  <text x="508" y="68" fill="currentColor">AF</text>
  <rect x="538" y="34" width="44" height="40" class="f-ok" stroke="currentColor"/>
  <text x="542" y="52" fill="currentColor" font-family="monospace">9:8</text>
  <text x="542" y="68" fill="currentColor">SH</text>
  <rect x="582" y="34" width="44" height="40" class="f-warn" stroke="currentColor"/>
  <text x="586" y="52" fill="currentColor" font-family="monospace">7:6</text>
  <text x="586" y="68" fill="currentColor">AP</text>
  <rect x="626" y="34" width="30" height="40" class="f-mut" stroke="currentColor"/>
  <text x="630" y="52" fill="currentColor" font-family="monospace">5</text>
  <text x="630" y="68" fill="currentColor">NS</text>
  <rect x="656" y="34" width="50" height="40" class="f-acc" stroke="currentColor"/>
  <text x="660" y="52" fill="currentColor" font-family="monospace">4:2</text>
  <text x="660" y="68" fill="currentColor">Attr</text>
  <rect x="706" y="34" width="44" height="40" class="f-ok" stroke="currentColor"/>
  <text x="710" y="52" fill="currentColor" font-family="monospace">1:0</text>
  <text x="710" y="68" fill="currentColor">01</text>
  <text x="10" y="102" fill="currentColor" font-family="monospace">0x0000000040000701 = PA 0x40000000 | AF | SH=11 | AP=00 | Attr0 | 블록   (RAM, Normal WB, RW, 실행 가능)</text>
  <text x="10" y="124" fill="currentColor" font-family="monospace">0x0060000009000405 = PA 0x09000000 | PXN|UXN | AF | SH=00 | Attr1 | 블록  (UART, Device-nGnRE)</text>
  <text x="10" y="146" fill="currentColor" font-family="monospace">0x0060000046200781 = PA 0x46200000 | PXN|UXN | AF | SH=11 | AP=10 | 블록 (RO, XN)</text>
  <text x="10" y="168" fill="currentColor" font-family="monospace">0x0000000046400301 = PA 0x46400000 | SH=11 | 블록  ← AF 가 없다 (첫 접근에 폴트)</text>
  <text x="10" y="198" fill="currentColor">AttrIndx 는 "MAIR_EL1 의 몇 번째 바이트" 를 가리키는 포인터일 뿐 — 메모리 타입은 MAIR 에 있다.</text>
  <text x="10" y="218" fill="currentColor">AP[2:1]: 00 EL1 RW · 01 EL1/EL0 RW (EL1 실행 자동 금지) · 10 EL1 RO · 11 EL1/EL0 RO.</text>
  <text x="10" y="238" fill="currentColor">테이블 descriptor: [47:12] = 다음 테이블 PA, [1:0] = 11 → l1[0] = 0x40087003 (l2_low @ 0x40087000).</text>
</svg>
```

**2) MAIR 와 TCR.** 이 레슨의 값:

| 레지스터 | 값 | 필드 |
|---|---|---|
| `MAIR_EL1` | `0x004404FF` | Attr0 = 0xFF Normal Inner/Outer WB non-transient RW-alloc · Attr1 = 0x04 Device-nGnRE · Attr2 = 0x44 Normal NC · Attr3 = 0x00 Device-nGnRnE |
| `TCR_EL1` | `0x2_8080_3519` | T0SZ = 25 (0x19) · IRGN0 = 01, ORGN0 = 01 (walk 가 WB 캐시로) · SH0 = 11 · TG0 = 00 (4 KB) · EPD1 = 1 (bit 23) · TG1 = 10 (4 KB, bit 31) · IPS = 010 (40 비트, [34:32]) |
| `TTBR0_EL1` | `0x40088000` | `l1` 의 PA (ASID = 0). 4 KB 정렬 (`aligned(4096)`) |
| `SCTLR_EL1` | `0xc50838` → `0xc5183d` | M (bit 0), C (bit 2), I (bit 12) 만 추가. A (bit 1) = 0 유지 |

- IPS 는 `ID_AA64MMFR0_EL1[3:0]`(PARange)를 그대로 쓴다 — 칩마다 다르기 때문에 상수로 박지 않는 것이 이식성의 핵심. QEMU 실측 `0x1122` → PARange = 2 (40 비트). 실제 Cortex-A53 도 40 비트 PA 로 알고 있다 (TRM 확인 권장). PARange = 6 (52 비트)은 다른 descriptor 형식(FEAT_LPA)이 필요해서 `make_tcr` 는 5 (48 비트)로 자른다 — `make_tcr(32, 0x6)` 테스트.
- **TG1 의 인코딩은 TG0 과 다르다**(TG0: 00 = 4 KB, TG1: 10 = 4 KB). EPD1 = 1 이면 TTBR1 walk 를 안 하니 실제로 문제가 안 될 수 있다 — 뮤턴트로 TG1 = 00 을 넣어 봤더니 QEMU 는 그대로 동작했고, 골든 값 검사만 잡았다.
- `EPD1 = 1`: VA 의 상위 반쪽(커널 공간, TTBR1)을 아예 안 쓴다. 상위 VA(0xFFFFFF80_00000000 등)는 레벨 0 translation fault — AT 로 실측.

**3) 39 비트 VA 가 테이블을 따라가는 길.**

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="14" font-weight="bold">VA 0x46812345 의 walk (T0SZ = 25 → L1 시작)</text>
  <rect x="10" y="32" width="150" height="34" class="f-warn" stroke="currentColor"/>
  <text x="16" y="54" fill="currentColor" font-family="monospace">[63:39] = 0</text>
  <rect x="160" y="32" width="150" height="34" class="f-acc" stroke="currentColor"/>
  <text x="166" y="54" fill="currentColor" font-family="monospace">[38:30] L1 idx = 1</text>
  <rect x="310" y="32" width="160" height="34" class="f-acc" stroke="currentColor"/>
  <text x="316" y="54" fill="currentColor" font-family="monospace">[29:21] L2 idx = 52</text>
  <rect x="470" y="32" width="270" height="34" class="f-ok" stroke="currentColor"/>
  <text x="476" y="54" fill="currentColor" font-family="monospace">[20:0] 블록 안 오프셋 = 0x12345</text>
  <rect x="20" y="100" width="190" height="70" class="f-mut" stroke="currentColor"/>
  <text x="28" y="120" fill="currentColor" font-family="monospace">TTBR0 = 0x40088000 (l1)</text>
  <text x="28" y="140" fill="currentColor" font-family="monospace">l1[1] = 0x40086003</text>
  <text x="28" y="160" fill="currentColor">테이블 → l2_ram</text>
  <rect x="270" y="100" width="210" height="70" class="f-mut" stroke="currentColor"/>
  <text x="278" y="120" fill="currentColor" font-family="monospace">l2_ram @ 0x40086000</text>
  <text x="278" y="140" fill="currentColor" font-family="monospace">l2_ram[52] = 0x46800701</text>
  <text x="278" y="160" fill="currentColor">블록 (2 MB), AF=1, Attr0</text>
  <rect x="540" y="100" width="200" height="70" class="f-ok" stroke="currentColor"/>
  <text x="548" y="120" fill="currentColor" font-family="monospace">PA = 0x46800000</text>
  <text x="548" y="140" fill="currentColor" font-family="monospace">   | 0x12345</text>
  <text x="548" y="160" fill="currentColor" font-family="monospace">= 0x46812345</text>
  <path d="M235 49 L235 92 L120 92 L120 100" stroke="currentColor" fill="none"/>
  <path d="M390 66 L390 100" stroke="currentColor" fill="none"/>
  <path d="M210 135 L270 135" stroke="currentColor" fill="none"/>
  <path d="M480 135 L540 135" stroke="currentColor" fill="none"/>
  <text x="10" y="200" fill="currentColor">같은 PA 를 VA 0x86812345 로 가면: L1 idx = 2 → l1[2] = 0x0060000040000701 (1 GB 블록), 오프셋 30 비트.</text>
  <text x="10" y="220" fill="currentColor">walk(l1, 25, 0x86812345) = 0, pa = 0x46812345, level = 1 — 테스트 CHECK 로 확인.</text>
  <text x="10" y="244" fill="currentColor">T0SZ = 16 으로 틀리면: L0 시작 → l1 을 L0, l2_low 를 L1 로 읽음 → l2_low[1] = 0 → L1 fault.</text>
  <text x="10" y="262" fill="currentColor">소프트웨어 walk 가 이것을 예측하고(CHECK), 하드웨어도 똑같이 IFSC 0x05 를 낸다 (7. 실수 4).</text>
</svg>
```

walk 의 검증은 두 겹이다. ① **명세 대 walk**: `spec()` 은 `build_tables()` 를 보지 않고 메모리 맵 표만으로 기댓값을 계산한다. 0..4 GB 의 2048 개 블록 시작점 + 블록마다 xorshift 오프셋 2 개 + 범위 밖 6 개 = 6150 개 VA. 실측 `walk vs spec: VAs = 6150, mapped = 1737, mismatches = 0`. ② **하드웨어 대 walk**: MMU 를 켠 뒤 같은 6150 개 VA 를 `AT S1E1R`(읽기) 와 `AT S1E1W`(쓰기)로 물어 PAR 의 폴트 코드·PA·ATTR 를 walk 결과와 비교 — 실측 `AT vs walk: faults = 4410, mismatches = 0`.

**AT 가 AF 를 다루는 방식 (예상과 달랐다)**: AF = 0 블록의 VA 3 개에서 walk 는 access flag fault(0x0A) 인데 QEMU 의 AT 는 **성공**(`PAR_EL1(AF=0, r) = 0xff00000046400b80`)을 돌려줬다. QEMU 10.2.0 의 `ptw.c` 2131–2148 행은 AF 검사를 `if (likely(!ptw->in_at))` 안에 두어 AT 에서는 건너뛴다 (주석: AT 에서 AF 를 갱신할지는 IMPLEMENTATION DEFINED). 최신 master 소스는 AF 검사를 `in_debug` 조건으로 옮겨서 AT 도 AF 폴트를 보고하게 바뀌었다. 그래서 테스트는 이 3 개 VA 에서 두 동작을 모두 허용하고, 실제 load 에서는 AF 폴트를 엄격히 검사한다. 교훈: **AT 결과는 "실제 접근이 폴트 날지" 의 완전한 대리인이 아니다** — 아키텍처가 구현에 맡긴 부분이 있다(Arm ARM 의 AT 규칙 확인 권장).

**PAR_EL1 읽는 법 (실측값):**

| VA | PAR_EL1 | 해석 |
|---|---|---|
| UART `0x09000000` | `0x0400000009000b00` | F = 0, ATTR = 0x04 (Device-nGnRE), PA = 0x09000000, SH[8:7] = 10 (Outer — descriptor 는 SH = 00 이었는데 Device 는 항상 Outer Shareable 로 취급), NS[9] = 1, bit 11 = 1 (RES1 로 알고 있음, 확인 권장) |
| ALIAS `0x48000123` | `0xff00000046800b80` | ATTR = 0xFF, PA = 0x46800000, SH = 11 (Inner) |
| RO `0x46200000`, AT 쓰기 | `0x81d` | F = 1, FST = (0x1d >> 1) = 0x0E → 레벨 2 permission fault |

**4) MMU 켜는 순서 — 왜 각 단계가 거기 있나.**

| # | 명령 | 이유 |
|---|---|---|
| 0 | (C) 테이블 쓰기 → `dsb ishst` | walker 는 데이터 경로와 별개의 "관찰자" — 쓰기가 끝나야 본다 |
| 1 | `msr mair_el1` · `msr tcr_el1` · `msr ttbr0_el1` | M = 0 인 동안에는 아무 효과 없다. 켜기 **전에** 전부 맞아 있어야 한다 |
| 2 | `dsb ish` · `isb` | 테이블 쓰기 완료 + 시스템 레지스터 쓰기 확정 (컨텍스트 동기화) |
| 3 | `tlbi vmalle1` · `dsb nsh` | 리셋 후 TLB 내용은 보장되지 않는다(실칩). 부트로더가 다른 테이블로 MMU 를 썼다면 그 잔재도 |
| 4 | `ic iallu` · `dsb nsh` · `isb` | 캐시 off 동안 I-cache 에 남은 것 제거 (실칩 관행, QEMU 무해) |
| 5 | `mrs` → `orr M|C|I` → `bic A` → `msr sctlr_el1` | 한 번에 켠다. C/I 는 M 과 같이 켜야 Normal 속성이 의미가 있다 |
| 6 | `isb` | 이 다음 명령부터 변환된 주소로 가져오도록. 아이덴티티라 VA = PA 이므로 PC 그대로 계속 |

`run_qemu.sh objdump` 실측: `mmu_enable` 은 0x400820bc, `msr sctlr_el1, x3` 가 0x400820f8, 그 다음 `isb` 가 **0x400820fc** — MMU 가 켜진 뒤 처음 가져오는 명령이다. 7. 의 hang 로그에서 첫 폴트 주소가 바로 이것이다.

아이덴티티 매핑이 필요한 이유: `msr sctlr_el1` 직후 PC 는 여전히 0x400820fc 다. 이 VA 가 같은 PA 로 매핑돼 있지 않으면 다음 명령을 못 가져온다. 리눅스는 그래서 `idmap` (아이덴티티 맵)으로 MMU 를 켜고, 켠 뒤 상위 VA(커널 링크 주소)로 점프한 다음 idmap 을 버린다.

### Part B — 폴트와 함정 (실측 표)

예외 핸들러는 ARM-M13 의 벡터/`exc_common` 축약판을 쓰고, C 쪽 정책은 단순하다: **데이터 어보트(EC 0x25) → ELR += 4, x0 = 0xBAD**, **명령 어보트(EC 0x21) → ELR = LR, x0 = 0xDEAD** (뛰어든 함수가 그냥 return 한 것처럼).

| 실험 | ESR.EC | DFSC/IFSC | FAR | ELR | 의미 |
|---|---|---|---|---|---|
| `ldr` VA_HOLE+8 | 0x25 | 0x06 | 0x46600008 | `load64_insn` | L2 descriptor invalid |
| `ldr` 0xC0000010 | 0x25 | 0x05 | 0xC0000010 | 〃 | L1 descriptor invalid |
| `ldr` 1<<39, 0x0000FFFFFFFFF000 | 0x25 | 0x04 | 그 주소 | 〃 | T0SZ 범위 밖 (EPD1 = 1) |
| `ldr` 0x0A000000 | 0x25 | 0x06 | | | 저 영역 L2 구멍 |
| `str` RO+0x10 | 0x25 | 0x0E, WnR = 1 | 0x46200010 | `store64_insn` | 레벨 2 permission. 값 그대로 |
| `ldr` AF=0 블록 | 0x25 | 0x0A | 0x46400040 | | 레벨 2 access flag |
| `ldr` 0x80000000+0x10000000 | 0x25 | **0x10** | 0x90000000 | | 변환 OK, PA 0x50000000 에 아무것도 없음 → 동기 외부 어보트 |
| `blr` → 0x46000000 (XN) | **0x21** | 0x0E | 0x46000000 | **0x46000000** | 명령 어보트: ELR = FAR = 목적지 |
| `blr` → 0x46A00000 (실행 가능, 같은 바이트) | — | — | — | — | `mov x0,#42; ret` 실행 → 42 |
| `ldr` SHARED+0x201 (Normal) | — | — | — | — | 폴트 없음: 0x9988776655443322 |
| `ldr` DEVALIAS+0x201 (Device) | 0x25 | **0x21** | 0x48200201 | | 정렬 폴트: Device 는 SCTLR.A 와 무관하게 비정렬 금지 |

포인트:

- **같은 PA, 다른 VA, 다른 결과.** SHARED(0x46800000)는 VA 세 개로 보인다: 아이덴티티(Normal), ALIAS(Normal, XN), DEVALIAS(Device), 그리고 1 GB 블록 경로까지 네 개. 쓰기는 어느 경로로 해도 다른 경로에서 보인다 (CHECK 4 개). 비정렬 접근만 메모리 **타입**에 따라 갈린다. ARM-M13 에서 MMU off 상태의 비정렬 `ldr` 가 DFSC 0x21 이었던 이유가 여기서 확인된다: MMU off = 데이터 접근이 Device 취급.
- **XN 은 "메모리 속성", 코드 바이트는 무관.** 0x46000000 과 0x46A00000 에 같은 8 바이트(`d2800540 d65f03c0`)를 쓰고 `dc cvau / ic ivau / isb` 로 동기화한 뒤 불렀다: 하나는 42, 하나는 EC 0x21. 코드를 RAM 에 쓰고 실행하는 순서(D 정리 → I 무효화 → ISB)는 ARM-A04 의 주제다.
- **EC 0x21 의 ELR.** 데이터 어보트는 ELR = 그 `ldr`, 명령 어보트는 ELR = **가져오려던 주소** 자체. 그래서 "ELR 을 +4" 하면 XN 영역 안의 다음 명령으로 가서 또 폴트 난다 — 복구는 LR 로 돌아가는 것이 맞다.
- **외부 어보트 vs translation fault.** 0x10 은 "MMU 는 통과했고 버스가 거절했다", 0x04–0x07 은 "MMU 가 거절했다". 크래시 덤프에서 이 차이로 "테이블 버그" 와 "디바이스/주소맵 버그" 를 가른다.
- UART 는 Device 매핑을 통해 진짜로 쓴다: PrimeCell ID `0xb105f00d`, Periph ID `0x141011` (part 0x011 = PL011), `uart_solution.txt` 에 `ARM-A03: hello from PL011 through a Device-nGnRE mapping`. GICD_IIDR 는 `0x43b` (읽기만 함).

### Part C — 운영 중에 매핑 바꾸기

**AF 지연 설정.** ARMv8.0(Cortex-A53)은 하드웨어 AF 갱신(FEAT_HAFDBS, v8.1)이 없다 — `ID_AA64MMFR1_EL1.HAFDBS = 0` 실측. 그래서 AF = 0 인 엔트리에 처음 접근하면 폴트가 나고, OS 가 AF 를 켜고 같은 명령을 다시 실행시킨다. 리눅스는 이것을 페이지 "접근됨(young)" 추적에 쓴다 (페이지 회수 때 AF 를 지웠다가, 다시 쓰이면 폴트로 알게 되는 방식 — 세부는 커널 소스 확인 권장).

```c
int af_fixup(uint64_t far) {
    uint64_t *p = leaf_desc(l1, T0SZ, far);
    if (!p || (*p & D_AF)) return 0;
    *p |= D_AF;          /* valid → valid, AF 만 0→1 */
    dsb_ishst();
    g_af_fixups++;
    return 1;            /* ELR 그대로 → 재실행 */
}
```

실측: `lazy AF: value = 0xaf0000000000af01` (MMU off 일 때 심어 둔 값), 폴트 1 번, 두 번째 접근부터 폴트 0 번, 이후 walk 와 AT 모두 성공. TLBI 를 안 한 이유: AF 폴트를 낸 변환은 TLB 에 캐시되지 않는다(Arm ARM 규칙으로 알고 있음, 확인 권장) — 그리고 AF 0→1 은 BBM 이 필요 없는 변경으로 분류된다.

**break-before-make.**

```svg
<svg viewBox="0 0 760 230" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-size="14" font-weight="bold">remap_block(VA_REMAP, SPARE): 시간 순서</text>
  <rect x="10" y="34" width="140" height="44" class="f-ok" stroke="currentColor"/>
  <text x="18" y="54" fill="currentColor">desc = SHARED</text>
  <text x="18" y="70" fill="currentColor">TLB: VA→SHARED</text>
  <rect x="160" y="34" width="130" height="44" class="f-warn" stroke="currentColor"/>
  <text x="168" y="54" fill="currentColor">① desc = 0</text>
  <text x="168" y="70" fill="currentColor">(break)</text>
  <rect x="300" y="34" width="170" height="44" class="f-acc" stroke="currentColor"/>
  <text x="308" y="54" fill="currentColor" font-family="monospace">② dsb ishst; tlbi</text>
  <text x="308" y="70" fill="currentColor" font-family="monospace">vaae1is; dsb ish; isb</text>
  <rect x="480" y="34" width="130" height="44" class="f-ok" stroke="currentColor"/>
  <text x="488" y="54" fill="currentColor">③ desc = SPARE</text>
  <text x="488" y="70" fill="currentColor">(make)</text>
  <rect x="620" y="34" width="130" height="44" class="f-ok" stroke="currentColor"/>
  <text x="628" y="54" fill="currentColor" font-family="monospace">④ dsb ishst; isb</text>
  <text x="628" y="70" fill="currentColor">read → 0xbb</text>
  <text x="10" y="108" fill="currentColor">①–② 사이 다른 코어가 접근하면 translation fault (재시도 가능) — 옛 것과 새 것이 TLB 에 동시에 있는 일은 없다.</text>
  <text x="10" y="128" fill="currentColor">②를 빼면 (bug_notlbi): QEMU 실측 read = 0xaa (옛 PA) → CHECK(r == 0xBB) FAIL.</text>
  <text x="10" y="148" fill="currentColor">①을 빼고 valid → 다른 valid 로 바로 바꾸면: 출력 주소·크기·메모리 타입 변경은 아키텍처상 BBM 필요 —</text>
  <text x="10" y="168" fill="currentColor">어기면 TLB conflict abort 나 두 변환이 섞인 결과가 허용된다 (Arm ARM 확인 권장). QEMU 는 재현 안 함.</text>
  <text x="10" y="196" fill="currentColor">vae1is = 그 VA·그 ASID, vaae1is = 그 VA·모든 ASID, vmalle1is = 전부. "is" = Inner Shareable 의 모든 코어에 방송.</text>
  <text x="10" y="216" fill="currentColor">이 레슨은 nG = 0(전역) 엔트리만 써서 vaae1is 를 썼다.</text>
</svg>
```

**TLB 가 정말 옛 변환을 들고 있나 — QEMU 실측.** 재매핑 테스트 뒤에 일부러 TLBI 없이 descriptor 만 SHARED 로 되돌리고(`dsb ishst; isb` 는 함):

```text
  remap (BBM) read   = 0xbb
  no-TLBI read       = 0xbb          ← load 는 아직 SPARE (옛 변환, QEMU 소프트 TLB)
  no-TLBI AT PA      = 0x46800000    ← AT 는 테이블을 새로 걸어서 SHARED
  after TLBI read    = 0xaa          ← tlbi vaae1is 뒤에야 SHARED
```

QEMU TCG 는 VA→호스트 주소 변환을 소프트 TLB 에 캐시하고 TLBI 때 비운다 — 그래서 "TLBI 를 빼먹은 버그" 가 에뮬레이터에서도 재현된다. 반면 같은 시점 AT 는 새 테이블을 보여 줬다: 이 QEMU 의 AT 는 TLB 를 참조하지 않고 walk 를 새로 한다(관찰). 실칩의 AT 가 TLB 를 쓰는지는 구현에 달려 있다고 알고 있다 — **"AT 로 확인했으니 TLB 도 맞다" 는 추론은 틀릴 수 있다.**

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

<!-- code: solution.S diff=starter.S -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"   # 65/65 passed, qemu exit=0 (VM 쉘 포함 0.75 s)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh gdb"        # demo_mmu.gdb → demo_mmu_out.txt 와 같은 출력
```

실제 출력 (PASS 줄 생략):

```text
ARM-A03 AArch64 MMU on (QEMU virt, cortex-a53)
  CurrentEL        = 0x1
  ID_AA64MMFR0_EL1 = 0x1122
  ID_AA64MMFR1_EL1 = 0x0
  SCTLR_EL1 (reset)= 0xc50838
  image            = 0x40080000
  image end        = 0x40099010
  TCR_EL1 value    = 0x280803519
  l1 @             = 0x40088000
  l1[0..2]         = 0x40087003
                     0x40086003
                     0x60000040000701
  walk vs spec: VAs = 6150, mapped = 1737, mismatches = 0
  SCTLR_EL1 (on)   = 0xc5183d
  TCR_EL1          = 0x280803519
  MAIR_EL1         = 0x4404ff
  TTBR0_EL1        = 0x40088000
  MMU on: still running (identity map)
  AT vs walk: faults = 4410, mismatches = 0, AF=0 VAs = 3
  PAR_EL1(AF=0, r) = 0xff00000046400b80
  PAR_EL1(UART)    = 0x400000009000b00
  PAR_EL1(ALIAS)   = 0xff00000046800b80
  PAR_EL1(RO, w)   = 0x81d
  PL011 PrimeCell ID = 0xb105f00d
  PL011 Periph ID    = 0x141011
  GICD_IIDR          = 0x43b
  hole   ESR.DFSC  = 0x6
  L1hole ESR.DFSC  = 0x5
  >39bit ESR.DFSC  = 0x4
  RO wr  ESR.DFSC  = 0xe
  AF=0   ESR.DFSC  = 0xa
  noPA   ESR.DFSC  = 0x10
  XN exec ESR.EC   = 0x21
  XN exec ESR.IFSC = 0xe
  XN exec ELR=FAR  = 0x46000000
  Dev unaligned DFSC = 0x21
  faults so far      = 10
  lazy AF: value     = 0xaf0000000000af01
  remap (BBM) read   = 0xbb
  no-TLBI read       = 0xbb
  no-TLBI AT PA      = 0x46800000
  after TLBI read    = 0xaa
  exceptions taken   = 11
65/65 passed
--- PL011 (uart_solution.txt) ---
ARM-A03: hello from PL011 through a Device-nGnRE mapping
qemu exit=0
```

`#ifdef BUG_*` 줄은 7. 의 버그 재현 스위치(기본 꺼짐). 정답이 버그를 잡는지도 따로 확인했다 — 비공개 사본에 뮤턴트 6 개를 넣어 돌린 결과: walk 의 shift 를 40 − 9·level 로(8 FAIL, preflight 가 MMU 를 안 켬), XN 을 UXN 만(7 FAIL — EL1 은 PXN 을 본다), `af_fixup` 이 AF 를 안 켬(같은 ldr 에서 무한 폴트 → 폭주 감시 exit 3), TG1 = 00(골든 값 2 FAIL; QEMU 실행은 멀쩡), TLBI 빼기(1 FAIL), AP 시프트 5(7 FAIL). 전부 실패로 잡혔다.

gdb 스크립트와 실제 출력 (`demo_mmu_out.txt` 에서 발췌):

<!-- code: demo_mmu.gdb -->

```text
(1) before   SCTLR_EL1=0xc50838 TCR_EL1=0 MAIR_EL1=0 TTBR0_EL1=0
    args     x0(ttbr0)=0x40088000 x1(tcr)=0x280803519 x2(mair)=0x4404ff
0x40088000 <l1>:	0x0000000040087003	0x0000000040086003
0x40088010 <l1+16>:	0x0060000040000701
0x40086200 <l2_ram+512>:	0x0060000046800701
(2) after    SCTLR_EL1=0xc5183d TCR_EL1=0x280803519 MAIR_EL1=0x4404ff TTBR0_EL1=0x40088000
(3) at remap_block: gdb memory reads use the guest MMU (VA):
0x46800100:	0x1111222233334444	0xa1a5a1a5a1a5a1a5
0x48000100:	0x1111222233334444	0xa1a5a1a5a1a5a1a5
0x86800100:	0x1111222233334444	0xa1a5a1a5a1a5a1a5
(4) QEMU gdbstub physical-memory mode: addresses are PAs now (0x46600000 = plain RAM)
sending: Qqemu.PhyMemMode:1
received: "OK"
0x46600000:	0x0000000000000000
(5) back to VA mode: unmapped VA (VA_HOLE) through gdb:
unknown command: 'info tlb'
Cannot access memory at address 0x46600000
```

- gdb 의 `x` 는 **게스트 MMU 를 거친 VA** 를 읽는다: 세 별칭이 같은 값을 보이고, 구멍 VA 는 "Cannot access memory". `maint packet Qqemu.PhyMemMode:1` 로 QEMU gdbstub 을 물리 주소 모드로 바꾸면 같은 0x46600000 이 평범한 RAM 으로 읽힌다 — MMU 디버깅에서 "VA 로 봤나 PA 로 봤나" 를 가르는 도구.
- QEMU 모니터의 `info tlb` 는 이 gdbstub 경로에서 `unknown command` 였다 (실측). TLB 내용을 직접 덤프하는 방법은 찾지 못했다 — 그래서 TLB 상태는 Part C 처럼 **행동으로** 관찰했다.

## 7. 자주 하는 실수

**1. 매핑을 바꾸고 TLBI 를 안 함.** `run_qemu.sh bug_notlbi` (remap_block 의 tlbi_va 를 뺌) 실측:

```text
  remap (BBM) read   = 0xaa
FAIL line 552: r == 0xBBu
64/65 passed
```

descriptor 는 SPARE 를 가리키는데 load 는 옛 SHARED 를 읽는다. 실칩에서는 다른 코어의 TLB 까지 문제라 `…is` (Inner Shareable 방송) 형이 필요하다. 고친 코드 = Part C 의 4 단계.

**2. AF 를 빠뜨림.** `bug_noaf` (mk_block 이 AF 를 무시) 실측: 소프트웨어 walk 가 먼저 잡는다(`walk vs spec: mapped = 0, mismatches = 1737`, preflight 실패). preflight 를 무시하고 켜면:

```text
qemu exit=124 (124 = timeout: 끝나지 않았다)
Taking exception 3 [Prefetch Abort] on CPU 0
...with ESR 0x21/0x8600000a
...with FAR 0x400820fc
...with ELR 0x400820fc
...to EL1 PC 0x40082a00 PSTATE 0x3c5
Taking exception 3 [Prefetch Abort] on CPU 0
...with ESR 0x21/0x8600000a
...with FAR 0x40082a00
--- 같은 예외가 몇 번? ---
937852
```

MMU 를 켠 직후 첫 명령(0x400820fc, `isb`)에서 명령 어보트 IFSC 0x0A(레벨 2 access flag) → 벡터 0x40082a00(VBAR + 0x200)도 같은 블록이라 같은 폴트 → 5 초 동안 수십만 번(실행마다 횟수 다름). 출력 한 줄 없이 멈추는 "MMU 켜자마자 죽음" 의 전형. 고친 코드: `if (af) d |= D_AF;`.

**3. MAIR 인덱스를 틀림 (UART 를 Attr0 = Normal WB 로).** `bug_mair` 실측:

```text
  walk vs spec: VAs = 6150, mapped = 1737, mismatches = 3
  PAR_EL1(UART)    = 0xff00000009000a00
FAIL line 446: par_attr(par) == 0x04u && par_pa(par) == VA_UART
  PL011 PrimeCell ID = 0xb105f00d
63/65 passed
--- PL011 (uart_mair.txt) ---
ARM-A03: hello from PL011 through a Device-nGnRE mapping
```

**QEMU 에서는 UART 가 그대로 동작한다** — TCG 는 캐시를 모델링하지 않고, MMIO 는 메모리 타입과 무관하게 디바이스 모델로 간다. 틀림이 보이는 곳은 PAR 의 ATTR(0xFF)과 SH(00)뿐 → 이 레슨의 AT 검사가 잡는다. 실칩에서는(기억 기반, 확인 권장): Normal 은 투기적 읽기·쓰기 합치기·재정렬·캐시 라인 단위 접근이 허용되므로 FIFO 읽기 레지스터가 투기적 읽기로 비워지고, 쓰기가 캐시에 머물러 디바이스에 안 가며, 비정렬 접근이 디바이스로 나간다. "에뮬레이터에서 되니까 맞다" 가 가장 위험한 버그.

**4. T0SZ 와 테이블 레벨 불일치.** `bug_t0sz` (테이블은 L1 부터인데 T0SZ = 16 → L0 부터):

```text
Taking exception 3 [Prefetch Abort] on CPU 0
...with ESR 0x21/0x86000005
...with FAR 0x400820fc
```

IFSC 0x05 = **레벨 1 translation fault** — 소프트웨어 walk 의 예측(`walk(l1, 16, 0x40080000) == FSC_TRANS(1)`, CHECK 통과)과 정확히 같다. L1 테이블을 L0 로 읽으면 l1[0](→ l2_low)이 "L1 테이블" 이 되고, VA 0x40080000 의 L1 인덱스 1 = l2_low[1] = 0 → 레벨 1 에서 invalid.

**5. ISB 를 빠뜨림 — QEMU 에서는 안 보인다.** `bug_noisb` (mmu_enable 의 ISB 3 개 제거) 실측 `65/65 passed`. QEMU TCG 는 시스템 레지스터 쓰기를 즉시 반영하므로 "아직 확정 안 된 설정으로 가져온 명령" 이 없다. 실칩에서는 SCTLR 쓰기 뒤 파이프라인에 이미 들어온 명령이 MMU off 로 가져온 것일 수 있고, TCR/TTBR 쓰기가 SCTLR.M 보다 늦게 보일 수도 있다(아키텍처상 ISB 없이는 순서 보장 없음 — ARM-M05 참고). **에뮬레이터 PASS 는 배리어 정확성의 증거가 아니다.**

**6. XN 을 UXN 만 켬.** EL1 코드의 실행 금지는 **PXN**(bit 53)이 결정한다. 뮤턴트(`if (xn) d |= D_UXN;`)로 돌리면 EL1 이 0x46000000 의 `mov x0,#42; ret` 를 그냥 실행해서 `r == 0xDEAD`, `EC == 0x21`, `ELR == FAR == VA_XN` 세 CHECK 가 실패한다(실측, 골든 값·walk 검사까지 7 FAIL). 커널의 사용자 페이지 = UXN 0 + PXN 1, 커널 코드 = UXN 1 + PXN 0.

## 8. 면접 Q&A

<details>
<summary>Q1. AArch64 에서 MMU 를 켜는 순서를 말해 보세요. 왜 그 순서인가요?</summary>

테이블을 메모리에 쓰고 `dsb` → MAIR, TCR, TTBR0 쓰기 → `isb` → `tlbi vmalle1; dsb` → (`ic iallu`) → `isb` → SCTLR.M(+C, I) → `isb`. SCTLR.M 이 켜지는 순간 다음 명령부터 변환되므로 그 전에 모든 설정과 테이블이 "보이는 상태" 여야 한다. 그리고 그 순간의 PC 를 위해 **아이덴티티 매핑**이 있어야 한다. 이 레슨의 실측: `msr sctlr_el1` 다음 `isb`(0x400820fc)가 MMU 켜진 첫 명령이고, 테이블이 틀리면 정확히 거기서 명령 어보트가 난다.
</details>

<details>
<summary>Q2. MAIR 와 descriptor 의 AttrIndx 는 어떤 관계인가요? 인덱스를 틀리면?</summary>

descriptor 는 3 비트 인덱스만 갖고, 실제 메모리 타입(Device-nGnRE, Normal WB…)은 MAIR_EL1 의 그 번째 바이트다 — 엔트리마다 8 비트를 아끼고, 타입 정의를 한 곳에서 바꿀 수 있게. 틀리면 MMIO 가 Normal 이 되어 투기적 읽기·쓰기 병합·캐시가 디바이스에 적용된다(실칩, 확인 권장). QEMU 에서는 UART 가 그대로 동작해서 **안 보였고**, `AT` 의 `PAR_EL1.ATTR` 이 0xFF 로 나온 것만 단서였다(실측). 그래서 bring-up 때는 AT/PAR 로 속성까지 검사하는 자가 테스트를 둔다.
</details>

<details>
<summary>Q3. T0SZ 와 IPS 는 각각 무엇이고, 어떻게 정하나요?</summary>

T0SZ = 64 − (TTBR0 쪽 VA 비트 수). 4 KB 그래뉼에서 시작 레벨을 정한다: 16–24 → L0, 25–33 → L1, 34–39 → L2 (연습 2 에서 24 개 값 전부 검사). 범위 밖 VA 는 레벨 0 translation fault(실측 DFSC 0x04). IPS 는 출력 PA 크기 — `ID_AA64MMFR0_EL1.PARange` 이하로, 보통 그대로 복사한다(QEMU a53 실측 PARange 2 = 40 비트). descriptor 에 IPS 보다 큰 PA 를 넣으면 address size fault 가 난다(이 레슨에서는 실험 안 함).
</details>

<details>
<summary>Q4. ESR 에서 EC 0x21 과 0x25 의 차이, 그리고 DFSC 를 읽어서 무엇을 알 수 있나요?</summary>

0x20/0x21 = 명령 어보트(하위 EL/같은 EL), 0x24/0x25 = 데이터 어보트. 명령 어보트는 ELR = FAR = 가져오려던 주소, 데이터 어보트는 ELR = 접근 명령, FAR = 데이터 주소, ISS.WnR = 쓰기 여부. FSC 하위 2 비트가 레벨: 0x04–0x07 translation, 0x08–0x0B access flag, 0x0C–0x0F permission, 0x10 외부 어보트, 0x21 alignment. 실측: XN 실행 0x21/0x0E, RO 쓰기 0x25/0x0E WnR = 1, 구멍 0x06, 범위 밖 0x04, RAM 밖 PA 0x10.
</details>

<details>
<summary>Q5. TLBI 는 왜 필요하고, 어떤 종류를 언제 쓰나요? 꼬리질문: AF 를 켤 때도 TLBI 가 필요한가요?</summary>

TLB 는 테이블의 캐시라 테이블을 바꿔도 자동으로 무효화되지 않는다 — QEMU 에서도 TLBI 없이 바꾼 매핑이 옛 PA 를 읽었다(실측 0xbb vs 0xaa). `vae1is`(VA+ASID), `vaae1is`(VA, 모든 ASID), `aside1is`(ASID 전부), `vmalle1is`(전부); `is` 는 Inner Shareable 의 모든 코어. 순서: `dsb ishst` → `tlbi` → `dsb ish` → `isb`. AF 0→1 은 AF 폴트 난 엔트리가 TLB 에 없으므로(규칙상 캐시 안 됨 — 확인 권장) TLBI 없이 `dsb` 만으로 재시도가 성공했다(실측). invalid → valid 도 마찬가지(연습 1).
</details>

<details>
<summary>Q6. break-before-make 를 설명하고, 이것을 지키지 않으면 무엇이 허용되나요?</summary>

유효 매핑의 출력 주소·블록 크기·메모리 타입 등을 바꿀 때는 먼저 invalid 로 만들고 TLBI 로 모든 코어에서 지운 뒤 새 값을 쓴다. 그렇지 않으면 한 VA 에 대해 옛 엔트리와 새 엔트리가 TLB 에 동시에 존재할 수 있고, 아키텍처는 그 경우 TLB conflict abort 나 두 엔트리가 섞인 결과를 허용한다(Arm ARM 확인 권장). 리눅스가 2 MB 블록을 4 KB 페이지로 쪼갤 때(예: 메모리 hotplug, `set_memory_ro`)도 이 규칙 때문에 까다롭다. 단점: break 와 make 사이 다른 코어가 접근하면 translation fault 가 나므로 핸들러가 재시도를 감당해야 한다.
</details>

<details>
<summary>Q7. 왜 Device 메모리에서는 비정렬 접근이 항상 폴트인가요? 이 레슨에서 어떻게 확인했나요?</summary>

Device 접근은 크기·횟수가 그대로 디바이스에 전달되어야 한다는 계약이라, 비정렬 접근을 여러 버스 트랜잭션으로 쪼개는 일을 허용하지 않는다 — SCTLR.A 와 무관하게 alignment fault. 같은 PA(0x46800000)를 Normal 별칭과 Device 별칭으로 매핑해서 같은 오프셋 0x201 을 읽었더니 Normal 은 `0x9988776655443322`, Device 는 DFSC 0x21(실측). ARM-M13 의 "MMU off 에서 비정렬 폴트" 와 같은 원인이다(MMU off = Device).
</details>

<details>
<summary>Q8. (staff) 부트로더에서 커널로 넘길 때 MMU/캐시 상태를 어떻게 해야 하나요? 이 설계가 틀리는 경우는?</summary>

arm64 리눅스 부트 규약은 MMU off, D-cache off(또는 이미지 영역이 PoC 까지 clean)로 진입을 요구한다고 알고 있다(Documentation/arch/arm64/booting.rst 확인 권장). 그래서 부트로더는 MMU 를 끄기 전에 dirty 라인을 clean 해야 한다 — 그렇지 않으면 캐시에만 있던 커널 이미지/DTB 쓰기가 MMU off(non-cacheable) 접근에서 안 보인다. 이 레슨의 연습 4 는 QEMU 에서 MMU off → 재설정 → on 을 했는데, **QEMU 는 캐시를 모델링하지 않아서 clean 을 빼도 통과한다** — 바로 이 설계가 실칩에서 틀리는 경우다. 실칩 검증에는 캐시 모델이 있는 FVP 나 실제 보드가 필요하다.
</details>

<details>
<summary>Q9. (staff) 2 MB 블록 대신 4 KB 페이지로만 매핑하면 무엇이 달라지나요? 직접 설계한다면 어떻게 바꾸겠나요?</summary>

블록은 TLB 엔트리 하나가 2 MB 를 덮어 TLB 압박과 walk 깊이(메모리 접근 횟수)가 줄고 테이블 메모리도 적다(128 MB RAM = L2 엔트리 64 개 vs L3 테이블 64 개 × 4 KB). 페이지는 권한을 세밀하게(텍스트 RX / 데이터 RW-XN / 가드 페이지) 줄 수 있다. 이 레슨의 이미지는 블록 0 하나에 코드·데이터·스택·테이블이 다 있어 **W^X 를 못 한다**(SCTLR.WXN 을 켜면 코드 블록이 쓰기 가능이라 실행 금지가 되어 바로 멈춘다). 바꾼다면: 이미지 블록만 L3 로 쪼개 `.text` RO+X, `.rodata` RO+XN, `.data/.bss/스택` RW+XN, 스택 아래 가드 페이지(연습 1 의 구성), 나머지 RAM 은 블록 유지. 그 위에서 WXN 을 켠다.
</details>

## 9. 추가 연습

### 연습 1 — 2 MB 를 4 KB 페이지로 쪼개기 (가드 · RO · 별칭)

VA 0x48600000(l2_ram[67])에 L3 테이블을 달아 PA 0x46C00000 의 512 페이지를 매핑한다. 페이지 5 = 가드(invalid), 6 = RO, 7 = 페이지 0 의 별칭. walk(본문 그대로 L3 를 지원)와 AT 를 1024 개 VA 에서 비교하고, 가드 접근과 RO 쓰기의 DFSC 를 확인하라.

<details>
<summary>답</summary>

`mk_page = mk_block(...) | 2` (L3 에서는 [1:0] = 11 이 페이지, 01 은 invalid). L2 엔트리는 원래 invalid 였으므로 table descriptor 를 쓰고 `dsb ishst; isb` 만 한다. 실측: `L3 pages: walk mismatches = 0, AT mismatches = 0`, 가드 DFSC **0x7**(레벨 3 translation), RO 쓰기 DFSC **0xf**(레벨 3 permission). 페이지 0 에 쓴 값이 페이지 7 에서 보인다.
</details>

### 연습 2 — T0SZ → 시작 레벨 / 루트 엔트리 수, MAIR 바이트 해석기

`start_level(t0sz)` 와 `root_entries(t0sz)` 를 "12 비트 + 9 비트씩" 으로 계산한 독립 참조와 16..39 전부 비교하라. 그리고 `mair_kind(byte)` 로 MAIR 속성 바이트를 Device 4 종 / Normal (outer, inner) / reserved 로 나누고, v8.0 규칙에서 유효한 바이트가 몇 개인지 세라.

<details>
<summary>답</summary>

루트 엔트리: T0SZ 25 → 512, 32 → 4, 33 → 2, 16 → 512 (L0). Device = `0000ddxx` 에서 xx = 00 인 4 개(nGnRnE 0x00, nGnRE 0x04, nGRE 0x08, GRE 0x0C). Normal 은 니블마다 0100 = NC, 00RW = WT transient, 01RW = WB transient, 10RW = WT, 11RW = WB(RW ≠ 00) → 니블당 15 가지, inner 0000 은 UNPREDICTABLE → 15 × 15 = 225 + 4 = **229** (실측 출력). 0xF0 은 v8.0 에선 reserved 로 셌지만 FEAT_MTE2 에서는 "Tagged Normal" 이고, FEAT_XS 도 일부 값을 새로 정의한다 — 확장마다 의미가 늘어난다는 점에 주의(확인 권장).
</details>

### 연습 3 — 주소 공간 전환 (TTBR0 바꾸기)

`l1b`/`l2_ram_b` 를 복사해 VA_ALIAS 만 SPARE 로 가리키는 두 번째 주소 공간을 만들고, `mmu_enable` (TLBI VMALLE1 포함)로 TTBR0 를 바꿔 같은 VA 가 다른 데이터를 보는지 확인하라. 그리고 TLBI 없이 TTBR0 만 바꾸면?

<details>
<summary>답</summary>

실측: `space B read (with TLBI) = 0xb0b0`, 원래 공간으로 돌아오면 0xa0a0. **TLBI 없이 TTBR0 만 바꾸면 `0xa0a0` — 옛 공간의 변환이 그대로 쓰였다**(QEMU 실측), `tlbi vmalle1is` 뒤에야 0xb0b0. 같은 ASID(0)로 TTBR 를 바꾸면 TLB 는 어느 테이블에서 온 엔트리인지 모른다. OS 는 그래서 프로세스마다 ASID 를 다르게 주고(TTBR0[63:48]) 사용자 매핑을 nG = 1 로 만들어, 전환 때 TLB 를 비우지 않고도 섞이지 않게 한다 — 이 레슨은 거기까지는 안 했다.
</details>

### 연습 4 — MMU 끄고 T0SZ = 32 로 다시 켜기

`mmu_disable()`(SCTLR.M/C/I 끄기 + TLBI)을 asm 으로 쓰고, 같은 L1 테이블로 T0SZ = 32 (4 GB VA)를 켜라. 1 GB 블록(L1[2])은 여전히 되는지, VA 4 GB 는 어떤 폴트인지, 마지막에 T0SZ 25 로 복귀하라.

<details>
<summary>답</summary>

T0SZ 25..33 은 모두 L1 시작이라 같은 테이블이 그대로 쓰인다 — 바뀌는 것은 범위뿐(L1 엔트리 4 개만 의미). 실측: `TCR_EL1 (T0SZ 32) = 0x280803520`, 1 GB 블록 경로 읽기 OK, `VA 4GB DFSC = 0x4`(레벨 0 = 범위 밖). 같은 VA 를 T0SZ 25 로 walk 하면 레벨 1 fault(L1[4] invalid) — 같은 주소가 설정에 따라 다른 레벨의 폴트가 된다. 실칩에서는 D-cache 를 끄기 전에 clean 이 필요하다(Q8) — QEMU 는 캐시가 없어 확인 불가.
</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # 92/92 passed, qemu exit=0
```

```text
  -- (solution tests above, practice below) --
  [1] L3 pages: walk mismatches = 0, AT mismatches = 0
  [1] guard page DFSC = 0x7
  [1] RO page wr DFSC = 0xf
  [2] valid MAIR attribute bytes (v8.0 rules) = 229
  [3] space B read (with TLBI)  = 0xb0b0
  [3] space B read (no TLBI)    = 0xa0a0
  [3] space B read (after TLBI) = 0xb0b0
  [4] SCTLR after disable = 0xc50838
  [4] TCR_EL1 (T0SZ 32)   = 0x280803520
  [4] VA 4GB DFSC          = 0x4
92/92 passed
qemu exit=0
```

## 10. 핵심 정리

- descriptor = 출력 주소 + [1:0] 종류(01 블록 / 11 테이블·페이지) + AttrIndx(→ MAIR 바이트) + AP(00 RW, 10 RO) + SH + **AF** + PXN/UXN. EL1 실행 금지는 PXN.
- MAIR = 타입 사전(0xFF Normal WB, 0x04 Device-nGnRE). TCR = T0SZ(VA 크기 → 시작 레벨), TG0, IRGN/ORGN/SH(walk 의 캐시성), EPD1, IPS(= PARange).
- 켜는 순서: 테이블 → dsb → MAIR/TCR/TTBR0 → isb → TLBI → dsb/isb → SCTLR.M|C|I → isb. PC 가 사는 곳은 반드시 아이덴티티 매핑.
- 폴트 코드: 0x04+L translation, 0x08+L AF, 0x0C+L permission, 0x10 외부 어보트, 0x21 alignment. EC 0x21 명령 어보트 = ELR = FAR = 목적지.
- v8.0 은 하드웨어 AF 갱신이 없다(HAFDBS = 0) → AF 폴트에서 OS 가 켜고 재시도. AF 빠뜨리면 MMU 켜자마자 무한 폴트.
- 매핑 변경 = break-before-make + `dsb ishst; tlbi …is; dsb ish; isb`. TLBI 를 빼면 QEMU 에서도 옛 변환이 보인다. TTBR 전환도 마찬가지(ASID 가 해답).
- QEMU 는 캐시·ISB 누락·MAIR 타입 오류를 보여 주지 않는다 → AT/PAR 로 속성까지 자가 검사하고, 실칩 규칙은 문서로.
- 테이블을 켜기 전에 소프트웨어 walk 로 preflight — "켜는 순간 무소식" 을 진단 메시지로 바꾼다.

## 참고자료

- Arm Architecture Reference Manual for A-profile (DDI 0487) — D8 "The AArch64 Virtual Memory System Architecture" (descriptor 형식, AP/XN/AF, TLB 유지보수, break-before-make), TCR_EL1/MAIR_EL1/SCTLR_EL1/PAR_EL1/ID_AA64MMFR0/1_EL1 레지스터 설명, AT 명령: <https://developer.arm.com/documentation/ddi0487/latest/>
- Arm "Learn the architecture — AArch64 memory management" (변환 테이블, TLB, 아이덴티티 맵 입문): <https://developer.arm.com/documentation/101811/latest/>
- Arm "Learn the architecture — AArch64 memory attributes and properties" (MAIR, Device-nGnRE, 공유성): <https://developer.arm.com/documentation/102376/latest/>
- Arm Cortex-A53 Technical Reference Manual (PA 크기, MMU/TLB 구성): <https://developer.arm.com/documentation/ddi0500/latest/>
- QEMU `target/arm/ptw.c` v10.2.0 (AT 와 AF 처리, 2131–2148 행): <https://github.com/qemu/qemu/blob/v10.2.0/target/arm/ptw.c> · master (바뀐 동작): <https://github.com/qemu/qemu/blob/master/target/arm/ptw.c>
- QEMU 'virt' 보드 문서 (메모리 맵): <https://www.qemu.org/docs/master/system/arm/virt.html> · QEMU gdbstub 의 `Qqemu.PhyMemMode`: <https://www.qemu.org/docs/master/system/gdb.html>
- Linux `arch/arm64/kernel/head.S` (idmap 으로 MMU 켜기), `arch/arm64/mm/proc.S` (`__cpu_setup`: MAIR/TCR 값), `Documentation/arch/arm64/booting.rst` (커널 진입 시 MMU/캐시 상태): <https://github.com/torvalds/linux/tree/master/arch/arm64>
- Arm PL011 TRM (PrimeCell/Periph ID 레지스터): <https://developer.arm.com/documentation/ddi0183/latest/>
- 관련 레슨: ARM-M13 (EL, VBAR, ESR, MMU off = Device 메모리), ARM-M05 (DSB/ISB), ARM-M14 (크래시 덤프 읽기), ARCH-A01 (page table walker 시뮬레이터 — 이 레슨의 walk 를 실하드웨어에 붙인 것), ARCH-A02 (TLB·ASID·flush 비용), ARCH-M12 (DMA 캐시 clean/invalidate), BM-A08 (Cortex-M MPU: 널 트랩·스택 가드·XN), ARM-A02 (IRQ·GIC·전체 컨텍스트 저장), ARM-A04 (코드 쓰고 실행: D/I 캐시 동기화)
