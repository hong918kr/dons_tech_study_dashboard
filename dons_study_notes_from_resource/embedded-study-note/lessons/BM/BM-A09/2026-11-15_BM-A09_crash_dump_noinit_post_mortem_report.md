---
id: BM-A09
time: 120분
updated: 2026-11-15
---
# BM-A09 크래시 덤프: .noinit RAM에 fault 기록, 리셋 후 보고

## 학습 목표

- 버전·크기·CRC-32 가 붙은 **사후 분석용 덤프 형식** (`crash_dump_t` v2, 204 B) 을 설계하고, "magic 은 마지막에 쓴다 + CRC 는 magic 뒤 196 B" 라는 커밋 규칙을 지킨다.
- naked 진입 스텁이 **push 0 번**으로 r4–r11 을 고정 주소에 저장하고 **전용 fault 스택으로 MSP 를 바꾼 뒤에야** C 를 부르는 이유를, 망가진 SP 에서 실제로 Lockup 이 나는 것으로 증명한다.
- EXC_RETURN 으로 프레임이 있는 스택(MSP/PSP)·모드(thread/handler)·프레임 크기를 고르고, CFSR 의 MSTKERR/STKERR 와 RAM/가드 범위로 "프레임을 읽어도 되나" 를 판정한다.
- **2 단계 커밋** (레지스터 먼저 봉인 → 위험한 스택 읽기는 scratch 로 → 다시 봉인) 과 **중첩 fault 가드** 로, 덤프 중에 또 fault 가 나도 1 단계 기록이 남게 한다.
- 콜드 부팅 쓰레기 / 찢어진 커밋 / 낡은 버전 / 1 비트 손상을 각각 다른 코드로 거르고, 오수용 확률을 계산·실측(1e6 건 ×2)한다. 다음 부팅의 UART 보고를 `decode_dump.py` 가 CRC 재검증 + 심볼화까지 한다.

## 1. 문제

현장에서 죽은 장비 앞에서 면접관이 묻는 건 "HardFault 났을 때 뭘 남기나요?" 다. 디버거는 없고, 재현은 안 되고, 고객은 로그 한 줄만 보내 준다. 답은 **죽는 순간의 CPU 상태를 리셋을 건너는 RAM(.noinit) 에 남기고, 다음 부팅에서 검증한 뒤 밖으로 내보내는 체계** 다. Don 이 SSD 펌웨어에서 assert 덤프를 남기고 PLP(power-loss protection) 로 메타데이터를 커밋하던 것과 구조가 같다 — "커밋 표시는 마지막에", "읽을 때는 무조건 의심". BM-M13 은 `{magic, reason, CFSR, HFSR, 주소, PC}` + FNV-1a 로 **정책**을 다뤘고, 이 레슨은 그 위에 **진짜 사후 분석 포맷과 도구** 를 올린다: 전체 레지스터(r0–r12, LR, PC, xPSR), EXC_RETURN, MSP/PSP, SCB fault 레지스터, 스택 16 워드, 버전·크기·CRC-32, 그리고 SP 자체가 망가진 경우와 덤프 도중의 fault 까지.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">crash_dump_t v2 — 204 B, 리틀엔디언 (offsetof 는 _Static_assert 로 고정)</text>
  <rect x="10" y="32" width="60" height="40" class="f-acc" stroke="currentColor"/>
  <text x="16" y="50" fill="currentColor">magic</text><text x="16" y="66" fill="currentColor">+0</text>
  <rect x="70" y="32" width="130" height="40" class="f-mut" stroke="currentColor"/>
  <text x="76" y="50" fill="currentColor">version size boot</text><text x="76" y="66" fill="currentColor">+4 +8 +12</text>
  <rect x="200" y="32" width="170" height="40" class="f-mut" stroke="currentColor"/>
  <text x="206" y="50" fill="currentColor">reason exc EXC_RET flags</text><text x="206" y="66" fill="currentColor">+16 +20 +24 +28</text>
  <rect x="370" y="32" width="130" height="40" class="f-mut" stroke="currentColor"/>
  <text x="376" y="50" fill="currentColor">stage sp msp psp</text><text x="376" y="66" fill="currentColor">+32 … +44</text>
  <rect x="500" y="32" width="230" height="40" class="f-ok" stroke="currentColor"/>
  <text x="506" y="50" fill="currentColor">r0–r12 (13 워드)</text><text x="506" y="66" fill="currentColor">+48 … +96</text>
  <rect x="10" y="84" width="170" height="40" class="f-ok" stroke="currentColor"/>
  <text x="16" y="102" fill="currentColor">lr pc xpsr</text><text x="16" y="118" fill="currentColor">+100 +104 +108</text>
  <rect x="180" y="84" width="230" height="40" class="f-warn" stroke="currentColor"/>
  <text x="186" y="102" fill="currentColor">CFSR HFSR MMFAR BFAR SHCSR</text><text x="186" y="118" fill="currentColor">+112 … +128</text>
  <rect x="410" y="84" width="60" height="40" class="f-mut" stroke="currentColor"/>
  <text x="416" y="102" fill="currentColor">nstack</text><text x="416" y="118" fill="currentColor">+132</text>
  <rect x="470" y="84" width="200" height="40" class="f-ok" stroke="currentColor"/>
  <text x="476" y="102" fill="currentColor">stack[16] (프레임 위)</text><text x="476" y="118" fill="currentColor">+136 … +196</text>
  <rect x="670" y="84" width="60" height="40" class="f-acc" stroke="currentColor"/>
  <text x="676" y="102" fill="currentColor">crc</text><text x="676" y="118" fill="currentColor">+200</text>
  <path d="M70 140 L670 140" stroke="currentColor"/>
  <path d="M70 134 L70 146 M670 134 L670 146" stroke="currentColor"/>
  <text x="250" y="160" fill="currentColor">CRC-32/ISO-HDLC (zlib) 범위 = [4, 200) = 196 B</text>
  <text x="10" y="190" fill="currentColor" font-weight="bold">쓰는 순서 (dump_seal)</text>
  <text x="10" y="208" fill="currentColor">magic = 0 → DSB → 내용 → crc = CRC32([4,200)) → DSB → magic = 0x504D5544 ("DUMP") → DSB</text>
  <text x="10" y="236" fill="currentColor" font-weight="bold">읽는 순서 (dump_valid) — 이유마다 다른 코드</text>
  <text x="10" y="254" fill="currentColor">magic=="READ"? CONSUMED → magic? NO_MAGIC → version==2? BAD_VERSION → size==204? BAD_SIZE</text>
  <text x="10" y="272" fill="currentColor">→ CRC? BAD_CRC → stage∈{1,2}, nstack≤16, stage1⇒nstack=0? BAD_FIELD → OK</text>
  <text x="10" y="300" fill="currentColor">version 을 CRC 보다 먼저: 낡은 v1(32 B)은 CRC 범위가 다르다. 길이 필드는 CRC 가 맞아도 상식 검사 후에만 믿는다.</text>
  <text x="10" y="320" fill="currentColor">magic 이 CRC 밖이라 "보고 완료" 표시(magic = "READ")로 바꿔도 내용·CRC 는 그대로 남는다.</text>
</svg>
```

### Part A — 덤프 형식, 프레임 선택, 2 단계 캡처

- 주어진 것: `a09.ld` (FLASH 256 KB / RAM 60 KB / **NOINIT 4 KB @0x2000F000**, RAM 안에 `.fault_stack` 1 KB 와 **64 B MPU 가드 + 4 KB 주 스택**), startup (BM-B08 축약판), CMSDK UART0 출력, naked `Fault_Entry` (네 fault 벡터의 alias), 결함 주입 9 단계, 보고 함수(@@ 줄 블록), 최종 검사.
- **TODO 1** `select_frame(excret, msp, psp, cfsr, bounds)`: EXC_RETURN 합법성, bit2 → PSP, [3:0]=1 → handler, bit4=0 → FP 프레임(104 B), MSTKERR/STKERR → 프레임 없음, RAM 범위·정렬·가드 겹침 → 프레임 없음.
- **TODO 2** `dump_seal()`: magic 무효화 → CRC → magic. **TODO 3** `dump_valid()`: 위 그림의 순서와 코드.
- **TODO 4** `dump_capture()`: 1 단계(코어 레지스터 + SCB) 봉인 → 2 단계(스택 16 워드, RAM 끝·가드에서 자름) 를 **scratch 로 먼저** 읽고, 다 읽은 뒤에만 덤프에 반영해 재봉인.

### Part B — 망가진 SP, 덤프 중 fault, 쓰레기 RAM

- `STACK_OVF`: 재귀가 64 B MPU 가드에 닿는다 → MemManage, CFSR `DACCVIOL|MSTKERR|MMARVALID`, 진입 순간 MSP 가 **가드 안**. `BAD_SP`: MSP = 0x30000100 (매핑 없음) 후 UDF → 쌓기 실패 `STKERR`. 두 경우 모두 덤프가 남아야 한다.
- 결함 주입 데모: 진입 스텁이 스택을 안 바꾸면(`naive`) → QEMU **Lockup**, 덤프 0. 가드가 32 B 면(`naive32`) → 가드를 **뛰어넘어** 조용히 .bss 를 지운다.
- `NESTED`: 2 단계에서 BUS_HOLE 을 읽게 주입 → UsageFault 안의 BusFault → HardFault FORCED → 스텁이 `g_fault_active` 로 재진입을 알아채고 최소 기록 + 즉시 리셋. 1 단계 덤프는 살아 있어야 한다. 세분 fault 를 끄면(`lockup`) HardFault 안의 fault = Lockup.
- 쓰레기: QEMU 콜드 RAM(0), 낡은 v1 덤프, 1 비트 손상 — 그리고 호스트에서 **무작위 1e6 건 + 헤더만 맞는 1e6 건** 오수용 0 을 확인 (`-DHOST_TEST` 로 같은 함수를 맥에서).

### Part C — 다음 부팅의 보고와 도구

- 부팅마다 `dump_valid` → 유효하면 UART0 에 `@@DUMP-BEGIN … @@HEX … @@DUMP-END` (204 B 원본 hex) + 사람용 한 줄, 아니면 `@@NODUMP rc=…`, 중첩 기록은 `@@NESTED`. 보고 뒤 magic = `"READ"` (소비 표시).
- `decode_dump.py` (stdlib: struct, zlib): HEX → 구조체 → **zlib CRC 재검증** → CFSR/HFSR 비트 이름 → PC/LR 을 `nm` 출력으로 `함수+오프셋` → 스택 워드에서 복귀 주소 후보. `--check` 로 시나리오별 기대값 대조.
- 10 부팅을 한 QEMU 프로세스로 (BM-M13 방식), 마지막 부팅이 92 개 검사 + `SYS_EXIT`.

## 2. Problem (English)

> **Post-mortem crash dump for a Cortex-M3 (QEMU `mps2-an385`).**
>
> 1. Define a versioned dump record (`magic, version=2, size=204, boot_count, reset_reason, exc, exc_return, flags, stage, sp, msp, psp, r0–r12, lr, pc, xpsr, CFSR, HFSR, MMFAR, BFAR, SHCSR, nstack, stack[16], crc`) in a `.noinit` section. `crc` = CRC-32/ISO-HDLC over bytes `[4, 200)`; `magic` is written last.
> 2. All fault vectors enter one naked stub that must not touch the stack until it has (a) stored r4–r11 to a fixed RAM area and (b) switched MSP to a dedicated fault stack. A second fault while dumping must be detected (re-entry flag) and cause an immediate reset with a minimal nested record.
> 3. `frame_sel_t select_frame(uint32_t exc_return, uint32_t msp, uint32_t psp, uint32_t cfsr, const stack_bounds_t *b)` — choose MSP/PSP from `EXC_RETURN[2]`; never dereference a frame when `CFSR.MSTKERR/STKERR` is set, when it is misaligned, outside RAM, or overlapping the stack guard.
> 4. `void dump_capture(...)` — two-phase: seal registers first; copy 16 stack words into a scratch buffer, then re-seal with `stage = 2`.
> 5. `int dump_valid(const crash_dump_t *d)` → `OK | NO_MAGIC | CONSUMED | BAD_VERSION | BAD_SIZE | BAD_CRC | BAD_FIELD`.
> 6. Next boot: print the dump over UART0 as a parseable block; a host tool re-verifies the CRC and symbolizes PC/LR from `nm`.
>
> Example (QEMU): UDF with `r0..r12 = 0xA00N000N` → `exc=6 CFSR=0x00010000 EXC_RETURN=0xFFFFFFF9 pc=pc_udf r4..r11 captured`; LDR from `0x30000000` → `exc=5 CFSR=0x00008200 BFAR=0x30000000`; UDIV by 0 on PSP → `EXC_RETURN=0xFFFFFFFD`, `stack[0..1]` = the two words the task pushed; MSP = `0x30000100` then UDF → `exc=5 CFSR=0x00011000 (STKERR|UNDEFINSTR)`, `msp=0x300000E0`, no frame.
>
> Follow-ups: why can't the handler be plain C? What if the SP is inside the MPU guard? How do you tell cold-boot garbage from a dump? What is the false-accept probability? What changes on a Cortex-M7 with a D-cache, or with ECC SRAM?

## 3. 시작 코드

<!-- code: starter.c -->

같이 쓰는 파일: `a09.ld`, `run_qemu.sh` (VM 빌드·실행), `decode_dump.py` (호스트 해독기), `demo_garbage.c`, `practice.c` (호스트 C), 캡처 `solution_uart.txt` / `solution_nm.txt` (맥에서 해독기를 바로 돌려 볼 수 있게).

<!-- code: a09.ld -->

starter/solution 은 **한 파일 두 빌드**다: 펌웨어 (`/* ENV: qemu */`, VM 의 arm-none-eabi-gcc) 와 `-DHOST_TEST` 호스트 시험 (같은 `select_frame`/`dump_seal`/`dump_valid` 를 맥이나 VM 의 cc 로). `run_qemu.sh starter|solution` 이 셋을 다 돌린다: QEMU 10 부팅 → `decode_dump.py --check` → 호스트 시험. Mac 에서:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-A09
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # 13/92, decode exit=1, host 13/37 → exit 1
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"   # 92/92, check: ALL OK, host 37/37 → ALL PASS
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh syms"       # nm / 세그먼트 / Fault_Entry 디스어셈블
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh naive"      # 결함 주입: 스택 전환 없는 진입 (BAD_SP)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh naive32"    # 결함 주입: 32 B 가드 + 스택 전환 없음
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh naiveovf"   # 64 B 가드 + 스택 전환 없음 (STACK_OVF)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh lockup"     # 세분 fault 끄고 덤프 중 fault
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # practice.c (호스트)
# 맥에서 호스트 시험만 (lesson dir 에서):
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cd $L && cc -std=c11 -Wall -Wextra -Werror -O2 -DHOST_TEST starter.c -o /tmp/BM-A09_st && /tmp/BM-A09_st
python3 -I decode_dump.py solution_uart.txt solution_nm.txt --check
```

시작 코드 실행 결과 (실제 출력, 줄임 표시는 …):

```text
   text	   data	    bss	    dec	    hex	filename
   9576	      0	   8696	  18272	   4760	starter.elf
== boot #1 ==
  cold boot: .noinit @0x2000f888 raw[0..3]=0x00000000 0x00000000 0x00000000 0x00000000 
@@NODUMP rc=NO_MAGIC magic=0x00000000 ver=0
  inject 0: UDF_MSP
  [fault] exc=6 msp=0x20001820 -> dump stage=0 sealed, SYSRESETREQ
== boot #2 ==
@@NODUMP rc=NO_MAGIC magic=0x00000000 ver=0
  … (부팅 3~10 도 전부 @@NODUMP rc=NO_MAGIC) …
== post-mortem summary (QEMU mps2-an385) ==
0    UDF_MSP      NO_MAGIC    (no dump)
  … (9 단계 모두 NO_MAGIC) …
FAIL starter.c:480  r->rc == DV_OK  [step 0]
  … (FAIL 79 줄) …
13/92 passed
qemu exit=1
--- decode_dump.py --check ---
decoded 0 dump(s), 10 no-dump boot(s) ['NO_MAGIC', … ]
check: 2 problem(s): expected 7 dumps, got 0; no-dump sequence […] != [NO_MAGIC, BAD_VERSION, BAD_CRC]
decode exit=1
--- host test (cc -DHOST_TEST) ---
  H3 select_frame: 200000 random cases, frame chosen 0, violations 0
  H4 single-bit flips: 1632, wrong code 1600
13/37 passed
host exit=1
```

읽는 법: 실행 틀(진입 스텁 → fault 스택 → 리셋 → 다음 부팅 보고)은 이미 돈다. 9 번 fault 가 났는데 **보고서는 0 장**이다. 현장의 "리셋됐어요, 로그엔 아무것도 없어요" 가 바로 이 상태다.

## 4. 힌트

<details>
<summary>힌트 1 — EXC_RETURN 비트 세 개</summary>

v7-M 의 합법 값은 0xFFFFFFF1 / F9 / FD (FP 확장 프레임이면 E1 / E9 / ED) 여섯 개뿐이다 (ARMv7-M ARM B1.5.8 — 확인 권장). bit2 = 1 이면 프레임은 PSP 에, bit3 = 0 (즉 [3:0] = 0001) 이면 fault 가 핸들러 모드에서, bit4 = 0 이면 프레임이 26 워드(104 B). 모양이 아니면(예: 0x0800012B) 누가 LR 을 덮은 것 — 프레임을 찾지 말 것.

</details>

<details>
<summary>힌트 2 — "읽어도 되나" 는 세 겹</summary>

(1) CFSR.MSTKERR (bit4) / STKERR (bit12): 하드웨어가 프레임을 **못 썼다** — 그 자리는 쓰레기거나 읽으면 또 fault. (2) 4-정렬 + `[sp, sp+fsize)` 가 RAM 안 — `sp + fsize` 는 오버플로할 수 있으니 `ram_hi - sp < fsize` 꼴로 비교. (3) 가드와 겹치면 안 된다 (MPU 가 읽기도 막는다).

</details>

<details>
<summary>힌트 3 — 왜 scratch 인가</summary>

2 단계에서 `d->stack[i] = src[i]` 를 바로 하면, 읽기 도중 fault 가 났을 때 덤프는 "magic 은 유효한데 stack[] 일부가 바뀐" 상태 → CRC 불일치 → 1 단계 기록까지 잃는다. 반대로 magic 을 먼저 0 으로 하고 읽으면 그 순간 덤프가 무효. 정답: 위험한 읽기는 scratch 로 다 끝낸 뒤, **안전한 쓰기만으로 된 짧은 구간**에서 magic=0 → 복사 → 재봉인.

</details>

<details>
<summary>힌트 4 — 봉인과 검증은 대칭</summary>

`dump_seal` 이 CRC 를 계산하는 범위(`(uint8_t*)d + 4`, 196 B) 와 `dump_valid` 가 검사하는 범위가 한 바이트라도 다르면 모든 덤프가 BAD_CRC 다. 호스트 시험 H4 가 204 × 8 비트를 하나씩 뒤집어 필드별 기대 코드(magic → NO_MAGIC, version → BAD_VERSION, size → BAD_SIZE, 나머지 → BAD_CRC)를 확인한다.

</details>

## 5. 풀이

### Part A — 형식, 진입 스텁, 프레임 선택, 2 단계 캡처

**메모리 배치** (`run_qemu.sh syms` 실제 출력 일부):

```text
20000000 B g_fault_active
20000004 B g_fault_r4_r11
20000470 B _sfault_stack
20000870 B _fault_stack_top
20000880 B _sstack_guard
200008c0 B _estack_guard
200018c0 B _estack
2000f000 B g_plan
2000f888 B g_dump
2000f954 B g_bootctr
2000f95c B g_nested
  LOAD           0x00f000 0x2000f000 0x2000f000 0x00000 0x00978 RW  0x10000
```

`.noinit` 세그먼트는 FileSiz 0 — QEMU 가 리셋 때 ELF 를 다시 올려도 덮을 내용이 없다 (BM-M13 에서 실측한 사실). 콜드 부팅의 원시 값은 `raw[0..3]=0x00000000 …` — **QEMU RAM 은 0 으로 시작한다** (실칩 SRAM 은 전원 투입 시 임의 패턴; 그래서 Part B 의 호스트 쓰레기 시험이 필요하다).

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">fault 한 번의 경로 (실측 주소, 64 B 가드 빌드)</text>
  <rect x="10" y="32" width="150" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="18" y="52" fill="currentColor">fault 명령</text>
  <text x="18" y="70" fill="currentColor">HW 가 8 워드 쌓기</text>
  <text x="18" y="84" fill="currentColor">(실패하면 STKERR)</text>
  <path d="M160 60 L190 60" stroke="currentColor"/><path d="M190 60 l-8 -4 l0 8 z" fill="currentColor"/>
  <rect x="190" y="32" width="250" height="96" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="198" y="52" fill="currentColor" font-weight="bold">Fault_Entry (naked, push 0 번)</text>
  <text x="198" y="70" fill="currentColor">g_fault_active ≠ 0 → 중첩 경로</text>
  <text x="198" y="86" fill="currentColor">stmia → g_fault_r4_r11 (0x20000004)</text>
  <text x="198" y="102" fill="currentColor">r0=LR r1=MSP r2=PSP r3=IPSR</text>
  <text x="198" y="118" fill="currentColor">MSP ← _fault_stack_top (0x20000870)</text>
  <path d="M440 70 L470 70" stroke="currentColor"/><path d="M470 70 l-8 -4 l0 8 z" fill="currentColor"/>
  <rect x="470" y="32" width="260" height="96" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="478" y="52" fill="currentColor" font-weight="bold">fault_capture → dump_capture</text>
  <text x="478" y="70" fill="currentColor">1) select_frame, 레지스터·SCB → 봉인</text>
  <text x="478" y="86" fill="currentColor">2) 스택 16 워드 → g_scratch</text>
  <text x="478" y="102" fill="currentColor">3) magic=0 → stack[] → stage 2 → 봉인</text>
  <text x="478" y="118" fill="currentColor">4) UART 한 줄 → SYSRESETREQ</text>
  <path d="M600 128 L600 160" stroke="currentColor"/><path d="M600 160 l-4 -8 l8 0 z" fill="currentColor"/>
  <text x="608" y="148" fill="currentColor">2) 에서 fault</text>
  <rect x="470" y="160" width="260" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="478" y="180" fill="currentColor">같은 우선순위 0 → HardFault FORCED</text>
  <text x="478" y="196" fill="currentColor">→ Fault_Entry: 플래그 1 → fault_nested</text>
  <text x="478" y="212" fill="currentColor">→ g_nested 기록 → 즉시 리셋</text>
  <rect x="190" y="160" width="250" height="60" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="198" y="180" fill="currentColor">다음 부팅: 1 단계 덤프 (stage 1)</text>
  <text x="198" y="196" fill="currentColor">+ @@NESTED exc=3 hfsr=FORCED</text>
  <text x="198" y="212" fill="currentColor">pc = fault_capture+0x212</text>
  <path d="M470 190 L440 190" stroke="currentColor"/><path d="M440 190 l8 -4 l0 8 z" fill="currentColor"/>
  <rect x="10" y="240" width="720" height="110" rx="6" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="20" y="260" fill="currentColor" font-weight="bold">왜 스텁에서 스택을 바꾸나 — 진입 순간의 MSP (실측)</text>
  <text x="20" y="280" fill="currentColor">정상 fault: 0x20001860 (주 스택) — 아무 데서나 C 를 돌려도 된다</text>
  <text x="20" y="298" fill="currentColor">STACK_OVF: 0x200008a0 = 가드 [0x20000880, 0x200008c0) 안 → push 하면 다시 MemManage</text>
  <text x="20" y="316" fill="currentColor">BAD_SP: 0x300000e0 = 매핑 없음 → push 하면 BusFault → HardFault → 또 실패 → Lockup (naive 실측)</text>
  <text x="20" y="336" fill="currentColor">fault 스택은 .bss 아래쪽(가드 밑)에 있어 주 스택이 넘쳐도 안전하다. 크기 1 KB = C 경로 최대 깊이 + 여유.</text>
</svg>
```

**진입 스텁** — `run_qemu.sh syms` 의 실제 디스어셈블 (네 벡터가 같은 주소 0x1c4 의 alias 라 objdump 는 `BusFault_Handler` 라는 이름으로 표시):

```text
     1c4:	f240 0c00 	movw	ip, #0
     1c8:	f2c2 0c00 	movt	ip, #8192	@ 0x2000
     1cc:	f8dc 0000 	ldr.w	r0, [ip]
     1d0:	b9a0      	cbnz	r0, 1fc <BusFault_Handler+0x38>
     1d2:	f240 0c04 	movw	ip, #4
     1d6:	f2c2 0c00 	movt	ip, #8192	@ 0x2000
     1da:	e88c 0ff0 	stmia.w	ip, {r4, r5, r6, r7, r8, r9, sl, fp}
     1de:	4670      	mov	r0, lr
     1e0:	f3ef 8108 	mrs	r1, MSP
     1e4:	f3ef 8209 	mrs	r2, PSP
     1e8:	f3ef 8305 	mrs	r3, IPSR
     1ec:	f640 0c70 	movw	ip, #2160	@ 0x870
     1f0:	f2c2 0c00 	movt	ip, #8192	@ 0x2000
     1f4:	f38c 8808 	msr	MSP, ip
     1f8:	f000 b9b6 	b.w	568 <fault_capture>
     1fc:	4670      	mov	r0, lr
     1fe:	f3ef 8108 	mrs	r1, MSP
     202:	f3ef 8205 	mrs	r2, IPSR
     206:	f000 bb27 	b.w	858 <fault_nested>
```

포인트: (1) **push 가 하나도 없다**. r0–r3, r12 는 하드웨어가 이미 쌓았으니(또는 쌓다 실패했으니) 마음껏 덮어쓰고, r4–r11 은 C 가 건드리기 전에 **고정 주소**로 `stmia`. (2) 주소를 `ldr r12, =sym` (리터럴 풀) 대신 `movw/movt #:lower16:/#:upper16:` 로 만든다 — 인라인 asm 의 리터럴 풀은 4 KB 이내에 놓인다는 보장이 없다. (3) `msr msp` 뒤에야 C (`b fault_capture` — `bl` 이 아니라 `b`: 돌아올 일이 없다). (4) 같은 스텁이 중첩 경로도 판정한다 — C 에서 판정하면 그 C 의 prologue push 가 이미 fault 일 수 있다.

**select_frame — 판정표** (호스트 시험 H2 의 이름 붙은 경우; bounds = RAM [0x20000000, 0x2000F000), 가드 [0x20001000, 0x20001020)):

| EXC_RETURN | SP | CFSR | 결과 |
|---|---|---|---|
| 0xFFFFFFF9 | MSP 0x20002000 | 0 | frame = MSP, `FRAME_OK` |
| 0xFFFFFFFD | PSP 0x20003000 | 0 | frame = PSP, `PSP|FRAME_OK` |
| 0xFFFFFFF1 | MSP | 0 | `HANDLER|FRAME_OK` (ISR 안의 fault) |
| 0xFFFFFFE9 | MSP | 0 | `FPFRAME|FRAME_OK`, fsize 104 |
| 0xFFFFFFF9 | MSP | MSTKERR | frame 없음, `STKERR` |
| 0xFFFFFFF9 | 0x20002002 / 0x20001010 / 0x20000FF0 | 0 | 비정렬 / 가드 안 / 가드에 걸침 → 없음 |
| 0xFFFFFFF9 | 0x2000EFE0 / 0x2000EFE4 / 0xFFFFFFF0 | 0 | RAM 끝에 딱 → OK / 넘음 / 오버플로 → 없음 |
| 0x0800012B, 0xFFFFFFF5 | — | — | `BAD_EXCRET` |

무작위 20 만 건 성질 검사(H3): 프레임을 고른 15244 건 전부 RAM 안·가드 밖·정렬, 위반 0.

**정답 실행 결과** (실제 출력, `@@HEX` 줄 생략):

```text
   text	   data	    bss	    dec	    hex	filename
  11412	      0	   8760	  20172	   4ecc	solution.elf
== boot #1 ==
  cold boot: .noinit @0x2000f888 raw[0..3]=0x00000000 0x00000000 0x00000000 0x00000000 
@@NODUMP rc=NO_MAGIC magic=0x00000000 ver=0
  inject 0: UDF_MSP
  [fault] exc=6 msp=0x20001860 -> dump stage=2 sealed, SYSRESETREQ
== boot #2 ==
@@DUMP-BEGIN tag=UDF_MSP v=2 size=204 boot=1 exc=6 crc=0x66e6bf06
@@HEX 44554d5002000000cc000000010000000100000006000000f9ffffff02000000
@@DUMP-END
  crash in boot #1: exc 6 CFSR=0x00010000 HFSR=0x00000000 pc=0x00000274 lr=0x00001d47 sp=0x20001860 MSP/thread stage=2 nstack=16
  … (부팅 3~5: BUS_PRECISE, DIV0_PSP, IRQ_UDF) …
  inject 4: STACK_OVF
  [fault] exc=4 msp=0x200008a0 -> dump stage=1 sealed, SYSRESETREQ
== boot #6 ==
  crash in boot #5: exc 4 CFSR=0x00000092 HFSR=0x00000000 pc=0xffffffff lr=0xffffffff sp=0x200008a0 MSP/thread stage=1 nstack=0
  inject 5: BAD_SP
  [fault] exc=5 msp=0x300000e0 -> dump stage=1 sealed, SYSRESETREQ
== boot #7 ==
  crash in boot #6: exc 5 CFSR=0x00011000 HFSR=0x00000000 pc=0xffffffff lr=0xffffffff sp=0x300000e0 MSP/thread stage=1 nstack=0
  inject 6: NESTED
== boot #8 ==
  crash in boot #7: … stage=1 nstack=0
@@NESTED exc=3 excret=0xfffffff1 cfsr=0x00018200 hfsr=0x40000000 pc=0x0000077a
  inject 7: STALE_V1
== boot #9 ==
@@NODUMP rc=BAD_VERSION magic=0x504d5544 ver=1
  inject 8: BAD_CRC
== boot #10 ==
@@NODUMP rc=BAD_CRC magic=0x504d5544 ver=2
== post-mortem summary (QEMU mps2-an385) ==
step tag         rc          exc excret     flags      stage nstk CFSR       pc
0    UDF_MSP      OK          6 0xfffffff9 0x00000002 2 16 0x00010000 0x00000274
1    BUS_PRECISE  OK          5 0xfffffff9 0x00000002 2 16 0x00008200 0x000002e0
2    DIV0_PSP     OK          6 0xfffffffd 0x00000003 2 16 0x02000000 0x000002fa
3    IRQ_UDF      OK          6 0xfffffff1 0x00000006 2 16 0x00010000 0x00000314
4    STACK_OVF    OK          4 0xfffffff9 0x00000010 1 0 0x00000092 0xffffffff
5    BAD_SP       OK          5 0xfffffff9 0x00000010 1 0 0x00011000 0xffffffff
6    NESTED       OK          6 0xfffffff9 0x00000002 1 0 0x00010000 0x00000354 +nested
7    STALE_V1     BAD_VERSION (no dump)
8    BAD_CRC      BAD_CRC     (no dump)
92/92 passed
qemu exit=0
```

검사 92 개에는 이런 것들이 들어 있다: UDF_MSP 의 r0–r12 **13 개 전부** = `0xA00N000N` (r0–r3·r12 는 쌓인 프레임에서, r4–r11 은 스텁의 `stmia` 에서), DIV0_PSP 의 `psp == &task_stack[256] − 8 − 32` 과 `stack[0..1] == 0x7A5C0001/2` (태스크가 push 한 두 워드), IRQ_UDF 의 쌓인 xPSR IPSR = 23 (= 16 + IRQ7), 모든 덤프의 `boot_count == k+1`, 복사본도 `dump_valid == OK`.

### Part B — 망가진 SP, 중첩 fault, 쓰레기

**STACK_OVF 해부** (`decode_dump.py` 출력에서): `MMFAR 0x200008bc` 는 재귀 함수의 `push {lr}` 이 쓰려던 주소(SP=0x200008c0 − 4) — 가드 맨 위 워드. 이 push 는 정밀 fault 라 SP 는 안 바뀌었고, 이어서 MemManage 진입의 8 워드 쌓기가 0x200008a0 부터 시작 → 전부 가드 안 → `MSTKERR`. 그런데 **MSP 는 0x200008a0 으로 내려가 있다** — 쌓기가 실패해도 SP 는 −32 된다 (QEMU 관측; BAD_SP 에서도 0x30000100 → 0x300000e0. ARMv7-M ARM 의 derived exception 규칙과 일치하는지는 확인 권장). 그래서 진입 순간 SP 가 **가드 안** 이다.

**BAD_SP**: 원래 예외는 UDF(UsageFault) 인데 받은 핸들러는 **BusFault(5)**, CFSR = `STKERR|UNDEFINSTR` (0x00011000). 쌓기 실패가 만든 파생(derived) BusFault 가 먼저 처리되었다 (QEMU 관측). 덤프의 정직한 결론: "원인 명령은 UDF, 그런데 SP 가 0x30000100 이라 프레임을 못 쌓았다 → PC 모름". PC 를 모르는 덤프도 **SP 값 자체가 증거**다 — 잘못된 문맥 복원·스택 포인터 오염을 가리킨다.

**결함 주입 1 — 스택 전환을 빼면** (`run_qemu.sh naive`, BAD_SP 단계, 실제 출력):

```text
qemu: fatal: Lockup: can't escalate 3 to HardFault (current priority -1)

R00=fffffff1 R01=300000c0 R02=00000000 R03=00000003
R04=00000004 R05=00000000 R06=2000f000 R07=2000f898
R08=2000f898 R09=00000000 R10=00000000 R11=00000000
R12=20000004 R13=300000c0 R14=fffffff1 R15=00000418
XPSR=61000003 -ZC- T handler
== boot #1 ==
  …
  inject 5: BAD_SP
qemu exit=134
```

C 의 첫 push 가 0x300000c0 근처를 쳐서 BusFault → 같은 우선순위라 HardFault 로 승격 → HardFault 의 쌓기도 실패 → **Lockup**. 덤프 0 장. QEMU 는 Lockup 을 모델링하는 대신 `fatal` 로 프로세스를 끝낸다 (exit 134 = abort). 실칩은 LOCKUP 상태로 멈추고, 칩에 따라 lockup 을 리셋으로 연결하거나(시스템 설정 — 확인 권장) 워치독이 깨운다 (BM-M10). 어느 쪽이든 **기록은 없다**.

**결함 주입 2 — 가드가 너무 작으면** (`run_qemu.sh naive32`, 실제 출력):

```text
  inject 4: STACK_OVF
== boot #2 ==
@@NODUMP rc=NO_MAGIC magic=0x00000000 ver=2
demo build: done
qemu exit=0
```

같은 `naive32.elf` 를 `qemu-system-arm … -d int -D int.log` 로 다시 돌린 예외 로그 (실제 출력, 부팅 1 부분):

```text
Taking exception 18 [v7M INVSTATE UsageFault] on CPU 0
...taking pending nonsecure exception 6
Taking exception 18 [v7M INVSTATE UsageFault] on CPU 0
...taking pending nonsecure exception 3
Loaded reset SP 0x200018a0 PC 0x769 from vector table
```

`recurse` 의 프레임은 56 B (`push {lr}` + `sub sp, #52`) 이고 쓰는 곳은 sp+0, sp+44, sp+52 뿐 — **40 B 짜리 빈틈**이 32 B 가드를 통째로 건너뛴다. MemManage 는 한 번도 안 났고, 재귀는 .fault_stack·.bss·.data 를 지나 0x20000000 아래(QEMU 의 RAZ/WI "RESERVED 2")까지 내려갔다가, 돌아오며 0 을 PC 로 꺼내 **INVSTATE** UsageFault. 그때는 `g_fault_r4_r11`, `g_fault_active` 까지 다 지워진 뒤라 덤프는 `magic=0, version=2` 로 찢어졌다. 교훈: **가드 ≥ 가장 큰 단일 SP 감소량** (가장 큰 함수 프레임, 예외 프레임 32/104 B). 큰 지역 배열이 있으면 `-fstack-usage` 로 최대 프레임을 확인하고(BM-A07, RTOS-M08), 가드를 키우거나 MPU 영역을 스택 바닥 전체로 잡는다. 64 B 빌드에선 같은 함수가 잡혔다.

참고로 64 B 가드에서 스택 전환 없이 STACK_OVF 만 돌리면(`run_qemu.sh naiveovf`) 덤프가 **남는다** (실제 출력):

```text
  inject 4: STACK_OVF
  [fault] exc=4 msp=0x20000878 -> dump stage=1 sealed, SYSRESETREQ
== boot #2 ==
  crash in boot #1: exc 4 CFSR=0x00000092 HFSR=0x00000000 pc=0xffffffff lr=0xffffffff sp=0x20000878 MSP/thread stage=1 nstack=0
```

이 빌드는 부팅 1 에서 바로 STACK_OVF 로 가서 재귀가 시작하는 깊이가 정답 실행(부팅 5)과 다르고, 그래서 넘친 지점이 가드 위쪽 끝과 맞물리는 위치도 다르다: 실패한 쌓기 뒤 MSP 가 가드 **아래**(0x20000878)에 떨어졌고, naive C 는 가드 밑의 .fault_stack 영역을 조용히 덮으며 돌았다. 정답 실행에선 같은 함수가 0x200008a0(가드 **안**)에 떨어졌다. "운 좋게 동작" 은 증명이 아니다 — 어디에 떨어질지는 호출 깊이에 달렸고, BAD_SP 처럼 SP 가 아예 쓸 수 없는 곳이면 Lockup 이다.

**중첩 fault** (`NESTED`): 1 단계 봉인 뒤, 2 단계에서 BUS_HOLE 읽기 → BusFault(우선순위 0) 는 실행 중인 UsageFault(0) 를 선점 못 함 → HardFault FORCED → 스텁이 `g_fault_active=1` 을 보고 `fault_nested` → `@@NESTED exc=3 cfsr=0x00018200 hfsr=0x40000000 pc=0x0000077a` (= `fault_capture+0x212`, dump_capture 가 인라인된 그 읽기). 다음 부팅의 덤프는 **stage 1, 유효** — scratch 설계 덕분. 세분 fault 를 끄면(`run_qemu.sh lockup`): 첫 fault 가 이미 HardFault 라 덤프 중 fault 를 받을 곳이 없다 → `Lockup: can't escalate 3 to HardFault (current priority -1)`, `R13=20000848` (fault 스택), exit 134. **세분 fault 를 켜 두는 것 = 덤프 코드에 한 번의 기회를 더 주는 것** (BM-M13 Q1 의 연장).

**쓰레기·찢어짐·낡음** — 호스트 시험 (`-DHOST_TEST`, 실제 출력):

```text
  H3 select_frame: 200000 random cases, frame chosen 15244, violations 0
  H4 single-bit flips: 1632, wrong code 0
  H6 1e6 random fills: OK 0 NO_MAGIC 1000000 CONSUMED 0 BAD_VERSION 0 BAD_SIZE 0 BAD_CRC 0 BAD_FIELD 0
  H7 1e6 header-valid fills: OK 0 BAD_CRC 1000000 BAD_FIELD 0 (expected false accepts 1e6/2^32 = 0.000233)
37/37 passed
```

확률로 보면: 균일 무작위 RAM 이 통과하려면 magic(2^-32) · version(2^-32) · size(2^-32) · CRC(2^-32) 가 모두 맞아야 한다 → 2^-128. H6 은 magic 단계에서 100 만 건이 전부 걸렸다(기대 통과 수 2.3e-4 건조차 magic 만으로). H7 은 헤더 3 워드를 강제로 맞춰 **CRC 혼자의 힘**을 잰다: 기대 1e6/2^32 = 0.000233 건, 실측 0. `demo_garbage.c` 는 같은 실험을 검사 폭별로 한다 (2^20 건, 실제 출력):

```text
check bits   false accepts   expected N/2^bits
         8            4153           4096.0000
        16              23             16.0000
        24               0              0.0625
        32               0              0.0002
```

8 비트 4153 vs 기대 4096 (표준편차 ≈ 64 — 1σ 안), 16 비트 23 vs 16 (σ ≈ 4 — 2σ 안쪽). 확률 법칙대로다. **1 비트 손상은 CRC-32 가 확률이 아니라 보장으로** 잡는다 (H4: 1632 가지 전부, 필드별 정확한 코드). 다만 "쓰레기가 우연히 통과" 보다 현실적인 위험은 **우리 자신의 낡은 덤프**다: 부팅마다 보고 후 magic 을 `"READ"` 로 바꾸지 않으면, 다음의 원인 모를 리셋(워치독, 브라운아웃)이 지난 fault 를 다시 보고한다. 낡은 형식은 version 으로 거른다 (STALE_V1 → BAD_VERSION, CRC 까지 맞는 v1 인데도).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">RAM 지도와 세 가지 "나쁜 SP" (실측 주소)</text>
  <rect x="20" y="40" width="300" height="26" class="f-mut" stroke="currentColor"/>
  <text x="28" y="58" fill="currentColor">.bss 0x20000000.. (g_fault_active, r4–r11, task 스택)</text>
  <rect x="20" y="66" width="300" height="34" class="f-ok" stroke="currentColor"/>
  <text x="28" y="88" fill="currentColor">.fault_stack 0x20000470–0x20000870 (1 KB)</text>
  <rect x="20" y="100" width="300" height="30" class="f-acc" stroke="currentColor"/>
  <text x="28" y="120" fill="currentColor">MPU 가드 0x20000880–0x200008c0 (64 B, AP=000)</text>
  <rect x="20" y="130" width="300" height="70" fill="none" stroke="currentColor"/>
  <text x="28" y="160" fill="currentColor">주 스택 4 KB ↑ _estack 0x200018c0</text>
  <text x="28" y="180" fill="currentColor">정상 fault 의 MSP 0x20001860</text>
  <rect x="20" y="212" width="300" height="30" class="f-ok" stroke="currentColor"/>
  <text x="28" y="232" fill="currentColor">.noinit 0x2000F000 (g_plan, g_dump @F888)</text>
  <text x="20" y="270" fill="currentColor">0x30000000 대: 매핑 없음 → 정밀 BusFault (BM-M13 probe)</text>
  <path d="M330 115 L380 115" stroke="currentColor"/>
  <text x="385" y="110" fill="currentColor">STACK_OVF: push → MMFAR 0x200008bc,</text>
  <text x="385" y="126" fill="currentColor">쌓기 실패 MSTKERR, MSP = 0x200008a0 (가드 안)</text>
  <path d="M330 266 L380 266" stroke="currentColor"/>
  <text x="385" y="262" fill="currentColor">BAD_SP: MSP 0x30000100 → 쌓기 STKERR,</text>
  <text x="385" y="278" fill="currentColor">MSP = 0x300000e0. 스택 전환 없으면 Lockup</text>
  <path d="M330 75 L380 75" stroke="currentColor"/>
  <text x="385" y="64" fill="currentColor">32 B 가드 빌드: 56 B 프레임이 가드를 건너뛰어</text>
  <text x="385" y="80" fill="currentColor">.fault_stack·.bss·.data 를 지우고 INVSTATE</text>
  <text x="385" y="180" fill="currentColor">규칙: 가드 ≥ 최대 프레임, fault 스택은 가드 밖,</text>
  <text x="385" y="196" fill="currentColor">진입 스텁은 push 0 번, 프레임은 판정 후에만 읽기</text>
</svg>
```

### Part C — 보고 형식과 해독기

부팅 시 보고는 **기계용 블록 + 사람용 한 줄**. 블록은 204 B 원본을 32 B 씩 hex 로 (7 줄), BEGIN 줄에 `tag/v/size/boot/exc/crc` 요약. 원본을 그대로 보내는 이유: 펌웨어 쪽 형식 변환 코드가 작고(버그가 적고), 해독기가 **같은 CRC 를 다시 계산**해 전송·옮겨 적기 오류까지 잡는다 (연습 4: 손상 2 만 건 미검출 0).

`python3 -I decode_dump.py solution_uart.txt solution_nm.txt --check` (맥에서, 캡처 파일로; 실제 출력 일부):

```text
no valid dump: rc=NO_MAGIC (magic=0x00000000 ver=0)
dump tag=UDF_MSP  crash in boot #1  CRC 0x66e6bf06 OK (zlib)
  exception : UsageFault (6)   EXC_RETURN 0xfffffff9   flags FRAME_OK   stage 2
  CFSR 0x00010000 : UNDEFINSTR
  pc 0x00000274 = inject_udf_regs+0x68 [pc_udf]
  lr 0x00001d47 = main+0x1406
  sp 0x20001860 (MSP, thread mode)  msp 0x20001860 psp 0x00000000  xpsr 0xa1000000 (IPSR 0)
  r0-r3  a0000000 a0010001 a0020002 a0030003
  r4-r11 a0040004 a0050005 a0060006 a0070007 a0080008 a0090009 a00a000a a00b000b
  r12    a00c000c
  stack  00000000 … 00000919 00000000 00000000 00000000 ffffffff
    maybe return addr: 0x00000919 = Reset_Handler+0x4c
  check: OK
dump tag=DIV0_PSP  crash in boot #3  CRC 0xbb115e28 OK (zlib)
  exception : UsageFault (6)   EXC_RETURN 0xfffffffd   flags PSP|FRAME_OK   stage 2
  CFSR 0x02000000 : DIVBYZERO
  pc 0x000002fa = task_div0+0x16 [pc_div0]
  lr 0x00000311 = run_on_psp+0x10
  stack  7a5c0001 7a5c0002 00000000 …
dump tag=IRQ_UDF  crash in boot #4  CRC 0x6ca4b8ce OK (zlib)
  exception : UsageFault (6)   EXC_RETURN 0xfffffff1   flags FRAME_OK|HANDLER   stage 2
  pc 0x00000314 = Irq7_Handler+0x0 [pc_irq]
  lr 0xfffffff9 (EXC_RETURN)
  sp 0x20001840 (MSP, handler mode)  … xpsr 0x01000017 (IPSR 23)
    maybe return addr: 0x00001fa1 = main+0x1660
dump tag=STACK_OVF  crash in boot #5  CRC 0xb33f04ad OK (zlib)
  exception : MemManage (4)   EXC_RETURN 0xfffffff9   flags STKERR   stage 1
  CFSR 0x00000092 : DACCVIOL|MSTKERR|MMARVALID
  MMFAR 0x200008bc  (MMARVALID)
  pc 0xffffffff (unknown: frame not readable)
dump tag=NESTED  crash in boot #7  CRC 0xa4881172 OK (zlib)
  pc 0x00000354 = inject_udf_plain+0x0 [pc_nest]
  NESTED fault while dumping: HardFault, HFSR FORCED, CFSR PRECISERR|BFARVALID|UNDEFINSTR, pc 0x0000077a = fault_capture+0x212
no valid dump: rc=BAD_VERSION (magic=0x504d5544 ver=1)
no valid dump: rc=BAD_CRC (magic=0x504d5544 ver=2)
decoded 7 dump(s), 3 no-dump boot(s) ['NO_MAGIC', 'BAD_VERSION', 'BAD_CRC']
check: ALL OK
```

읽는 법 몇 가지. IRQ_UDF 의 쌓인 LR 이 `0xfffffff9` — IRQ7 핸들러가 leaf 라 LR 이 아직 그 IRQ 의 EXC_RETURN 이다: "ISR 안에서 죽었고, 그 ISR 은 thread/MSP 를 끊고 들어왔다" 까지 읽힌다. 스택 워드의 "maybe return addr" 는 **휴리스틱**(홀수 + .text 범위)이다 — `main+0x1660` 같은 오래된 값이 섞인다. 진짜 unwind(프레임 크기·push 목록 해석, `.ARM.exidx`)는 ARM-M12 (다음 단계). 심볼화는 **같은 빌드의 nm** 이어야 한다 — 그래서 실무에선 빌드 ID(이미지 해시)를 덤프 헤더에 넣는다 (Q6).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

해독기와 실행 스크립트:

<!-- code: decode_dump.py -->

<!-- code: run_qemu.sh -->

## 7. 자주 하는 실수

**실수 1 — fault 핸들러를 그냥 C 로.** `void HardFault_Handler(void) { save(...); }` 는 prologue 에서 push 한다. SP 가 가드 안이거나 매핑 밖이면 그 push 가 두 번째 fault → Lockup (naive 실측: exit 134, 덤프 0). 게다가 컴파일러가 r4–r11 을 먼저 써 버려 원래 값을 잃는다. 고침: naked 스텁에서 `stmia` 로 r4–r11 저장 → `msr msp, fault_stack_top` → `b fault_capture`.

**실수 2 — EXC_RETURN 을 안 보고 MSP 에서 프레임을 읽기.** RTOS 태스크(PSP)에서 난 fault 의 PC 가 엉뚱하게 나온다 — MSP 위에는 그 순간 아무 의미 없는 값이 있다. 변이체 1(bit2 무시)이 DIV0_PSP 에서 89/92 로 잡힌다. 고침: `excret & 4 ? psp : msp`.

**실수 3 — STKERR 인데 프레임을 읽기.** 쌓기에 실패한 프레임 자리는 쓰레기이거나 읽으면 또 fault 다. 변이체 4(STKERR 무시)는 이번엔 범위·가드 검사가 대신 막아서 덤프는 남았지만, `flags` 에서 STKERR 이 빠져 "왜 프레임을 못 읽었나" 의 근거가 사라진다 — 가드 밖에서 쌓기가 실패한 경우였다면 그 쓰레기 프레임을 그대로 읽었을 것이다 (90/92, 호스트 34/37). 검사는 겹으로.

**실수 4 — 2 단계에서 magic 을 먼저 지우고 위험한 읽기.** 읽다가 중첩 fault → 덤프가 통째로 무효 → NESTED 단계 rc=NO_MAGIC (변이체 2: 90/92, decode exit 1). 고침: 위험한 읽기 → scratch, 그 다음 짧은 안전 구간에서 magic=0 → 복사 → 봉인.

**실수 5 — version 검사 없이 CRC 만.** 예전 펌웨어(v1, 32 B)가 남긴 덤프에 새 구조체를 씌우면 size 가 32 라 BAD_SIZE 로 엉뚱하게 분류되거나, 길이가 우연히 맞으면 엉뚱한 필드로 "해석" 된다 (변이체 3: STALE_V1 이 BAD_VERSION 대신 다른 코드, 91/92 + 호스트 34/37). 고침: magic → version → size → CRC 순서, 그리고 버전별 리더(연습 2).

**실수 6 — 보고한 덤프를 그대로 두기.** 다음 리셋이 워치독이어도 같은 덤프를 또 보고한다. 고침: 보고 후 magic = `"READ"` (내용은 남겨 재조회 가능), 콜드/쓰레기면 magic = 0.

**실수 7 — 가드 크기를 "최소 32 B" 로.** naive32 실측: 56 B 프레임이 32 B 가드를 건너뛰었다. 고침: 가드 ≥ 최대 프레임 (또는 `-fstack-check`/스택 프로빙, v8-M 이면 MSPLIM — BM-A07/A08).

## 8. 면접 Q&A

<details>
<summary>Q1. HardFault 핸들러를 C 로만 쓰면 안 되는 이유를 구체적으로 말해 보세요.</summary>

세 가지. (1) **스택**: 핸들러의 prologue 가 push 하는데, fault 원인이 스택 넘침이나 오염이면 그 push 가 두 번째 fault 다. 이 레슨의 BAD_SP 에서 naive 진입은 `Lockup: can't escalate 3 to HardFault` 로 끝났고 덤프는 0 장이었다. (2) **r4–r11**: 하드웨어는 r0–r3, r12, LR, PC, xPSR 만 쌓는다. 콜리 저장 레지스터는 C 함수가 쓰기 전에 잡아야 한다 — 크래시 원인이 r4 에 든 포인터일 때가 많다. (3) **어느 스택인가**: EXC_RETURN(LR) 을 C 가 받으려면 스텁이 넘겨야 한다 (`mov r0, lr`). 그래서 몇 줄짜리 naked 스텁 + 전용 fault 스택이 표준 모양이다.

</details>

<details>
<summary>Q2. 콜드 부팅 쓰레기를 덤프로 오인할 확률은 얼마이고, 어떻게 줄이나요?</summary>

균일 무작위 SRAM 가정이면 magic 32 비트만으로 2^-32/부팅. 100 만 대 × 하루 10 부팅이면 1 년에 약 0.85 번 (연습 5) — 함대 규모에선 "일어난다". version·size·CRC-32 까지 맞아야 하면 2^-128. 실측: 무작위 1e6 건 통과 0, 헤더만 맞춘 1e6 건(CRC 혼자) 통과 0 (기대 0.000233). 다만 실제 SRAM 은 균일 무작위가 아니다 — 전원 투입 패턴이 칩마다 치우쳐 있고 짧은 정전이면 **이전 내용이 남는다**. 그래서 확률보다 더 중요한 건 하드웨어 리셋 원인(STM32 RCC_CSR 의 PORRSTF/BORRSTF, BM-M10)과 함께 보는 것: POR 이면 덤프 영역을 아예 지운다.

</details>

<details>
<summary>Q3. 덤프를 쓰는 도중에 또 fault 가 나면요?</summary>

설계로 두 가지를 보장했다. (1) **감지**: 진입 스텁이 `g_fault_active` 를 C 보다 먼저 본다. 두 번째 진입이면 스택을 바꾸지 않고(이미 fault 스택 위다) 7 워드짜리 `g_nested` 기록 후 즉시 리셋. (2) **손실 최소화**: 2 단계 커밋. 레지스터는 먼저 봉인하고, 위험한 읽기(스택 워드)는 scratch 로 하니, 중첩 fault 가 나도 stage-1 덤프는 유효하다 (NESTED 실측: stage 1 + `@@NESTED exc=3 pc=fault_capture+0x212`). 단, 이 두 번째 기회는 첫 fault 가 **설정 가능한 fault**(MemManage/Bus/Usage)일 때만 있다 — 첫 fault 가 HardFault 면 그 안의 fault 는 Lockup (lockup 실측, exit 134). 그래서 세분 fault 를 켠다.

</details>

<details>
<summary>Q4. Cortex-M7 (D-cache, write-back) 이라면 무엇을 바꾸겠나요?</summary>

`.noinit` 이 캐시 가능한 SRAM 이면, 덤프를 쓴 뒤 SYSRESETREQ 하는 순간 **캐시에만 있던 줄이 사라진다** — 다음 부팅엔 반쯤 쓴 덤프나 옛 값이 보인다. 고침: 봉인 후 덤프 영역을 `SCB_CleanDCache_by_Addr` (또는 DCCMVAC 반복) + DSB, 그 다음 리셋. 더 단순하게는 덤프 영역을 MPU 로 non-cacheable / DTCM 에 둔다 (DTCM 은 캐시를 안 거친다 — 칩별 확인 권장). 역방향도 있다: 다음 부팅 초기에 캐시를 켜기 **전에** 덤프를 읽거나, 켜기 전 invalidate 가 덤프 줄을 덮지 않게 순서를 정한다. ARCH-M12 (DMA 캐시 일관성) 와 같은 원리.

</details>

<details>
<summary>Q5. ECC 가 있는 SRAM 이면 .noinit 이 문제가 되나요?</summary>

된다. STM32H7/G4 같은 ECC SRAM 은 전원 투입 후 한 번도 안 쓴 워드를 읽으면 ECC 오류(NMI/BusFault)를 낼 수 있다 (칩별, 확인 권장). 그래서 스타트업이 RAM 전체를 0 으로 초기화하는데, 그러면 `.noinit` 도 지워진다. 해법: (1) POR(전원 투입)일 때만 `.noinit` 을 초기화하고 시스템 리셋에선 건드리지 않는다 — 리셋 원인 플래그로 분기. (2) 덤프 전용 영역을 ECC 없는 백업 SRAM/RTC 백업 레지스터에. (3) 첫 읽기는 단일 비트 정정 오류를 견디게. 이 레슨의 `dump_valid` 는 **읽기 자체는 안전하다** 고 가정한다 — ECC 칩에선 그 가정부터 확인해야 한다.

</details>

<details>
<summary>Q6. 현장에서 받은 덤프의 PC 를 어떤 ELF 로 심볼화하나요? 버전이 섞이면?</summary>

nm/addr2line 은 **정확히 같은 빌드**여야 한다. 이 레슨에서도 BAD_SP 단계를 추가하자 `pc_nest` 가 0x340 → 0x354 로, UDF_MSP 의 LR 이 0x1bfb → 0x1d47 로 옮겨 갔다 — 옛 nm 으로 새 덤프를 풀면 틀린 함수 이름이 나온다. 실무: (1) 덤프 헤더에 빌드 ID (이미지 SHA-256 앞 4~8 바이트, 또는 GNU build-id) 를 넣는다 — 이 레슨의 v2 에 필드를 더한다면 v3, `size` 가 커지고 리더는 v2/v3 둘 다 읽는다 (연습 2 의 방식). (2) 릴리스마다 ELF + map 을 심볼 서버에 보관. (3) 해독기는 빌드 ID 가 다르면 심볼화를 거부하고 원시 주소만 보여 준다 — 틀린 함수 이름보다 모르는 게 낫다.

</details>

<details>
<summary>Q7. 덤프에 스택을 16 워드만 담았습니다. 이 설계가 틀리는 경우는?</summary>

(1) 깊은 호출에서 원인이 몇 프레임 위에 있으면 16 워드로는 복귀 주소가 안 잡힌다 — 해독기의 "maybe return addr" 는 휴리스틱이라 오래된 값도 섞인다 (IRQ_UDF 의 `main+0x1660`). (2) 스택 넘침이면 스택 자체가 의미가 없다 (STACK_OVF: 0 워드). (3) RTOS 면 **다른 태스크들**의 상태(태스크 이름, 각 PSP, 스택 HWM)가 더 중요할 때가 많다. 바꾼다면: 링에 여러 덤프 (연습 1), 덤프 크기를 플래시 섹터 하나로 키워 다음 부팅에 RAM → 플래시로 옮기기(현장 회수), 또는 FP 를 켠 빌드(`-fno-omit-frame-pointer`)로 프레임 체인 저장. 전체 unwind 는 ARM-M12.

</details>

<details>
<summary>Q8. 같은 크래시가 1 만 대에서 올라옵니다. 어떻게 묶나요?</summary>

크래시 시그니처 = 해시(exc, CFSR 원인 비트, PC, LR(bit0 제거)) — r0–r12, SP, 부팅 번호, BFAR 값처럼 매번 다른 것은 뺀다 (연습 3: 버그 3 개 × 잡음 3000 건 → 서명 정확히 3 개). PC 가 2 바이트만 달라도 다른 서명이다 — 같은 함수의 다른 명령은 다른 버그일 수 있다. 꼬리질문 "펌웨어 버전이 다르면?" → PC 가 바뀌므로 (빌드 ID, 함수+오프셋) 으로 정규화한 뒤 해시한다.

</details>

<details>
<summary>Q9. QEMU 로 검증한 것 중 실칩에서 다시 봐야 하는 건?</summary>

이번에 관측한 것들: (1) 쌓기 실패 뒤 SP 가 −32 된 채로 핸들러 진입 (0x30000100 → 0x300000e0), (2) BAD_SP 에서 BusFault(파생)가 먼저 처리되고 CFSR 에 UNDEFINSTR 도 남음, (3) Lockup = QEMU 프로세스 종료(실칩은 LOCKUP 상태 + 리셋/워치독), (4) 콜드 RAM 이 0, (5) 매핑 안 된 일부 영역은 RAZ/WI (0x20000000 아래 "RESERVED 2" — 그래서 32 B 가드 실험의 재귀가 조용히 바닥까지 갔다). 그리고 BM-M13 Q9 의 목록(정밀/부정확 BusFault, 쌓인 r0–r3 값). 덤프 포맷·검증 로직·도구 체인은 QEMU 로 충분히 검증되지만, 파생 예외의 순서와 리셋 후 SRAM 보존은 보드에서 확인한다.

</details>

## 9. 추가 연습

`practice.c` 는 **호스트 C** (맥 `cc`, 또는 VM 에서 `run_qemu.sh practice`). 덤프 형식은 축약판(같은 204 B 레이아웃, 같은 CRC-32 — 비트 단위 구현)으로 내장했다.

### 연습 1 — 덤프 링: 최근 4 개

슬롯 4 개, `boot_count` 자리를 seq 로 쓴다. 새 덤프는 빈/깨진 슬롯 → 없으면 seq 가 가장 작은 슬롯에. 쓰는 순서: 그 슬롯의 magic 무효화 → 내용 → 봉인. 찢어진 쓰기(봉인 전 정전)에도 다른 세 개는 살아야 한다.

<details>
<summary>답</summary>

10 개를 넣으면 남는 것은 7~10, 최신 seq 10. 찢어진 쓰기는 가장 오래된 7 하나만 잃고(유효 3 개, 최신은 여전히 10), 재시도는 깨진 슬롯을 먼저 재사용해 seq 11. 최신본을 1 비트 손상시키면 그 전 것(10)이 최신으로 올라온다. "덮어쓸 슬롯을 먼저 무효화" 가 핵심 — 무효화 없이 덮으면 반쯤 쓴 슬롯이 옛 magic 으로 유효해 보일 수 있다 (CRC 가 거르지만, 우연에 기대는 설계).

</details>

### 연습 2 — 버전 호환 리더

`dump_read_any(buf, len, &view)` 가 v1(32 B) 과 v2(204 B) 를 모두 공통 뷰 `{version, exc, pc, lr, cfsr}` 로. 길이 부족, 모르는 버전, 크기 불일치, CRC 오류를 각각 다른 코드로.

<details>
<summary>답</summary>

헤더 12 B 를 먼저 읽고 version 으로 분기 → 그 버전의 size 와 일치해야 하고 버퍼 길이가 그만큼 있어야 → 그 버전의 CRC 범위로 검사. 실수하기 쉬운 곳: 길이 검사 전에 구조체 전체를 memcpy (버퍼 넘어 읽기), v2 몸통에 version=1 만 바꾼 입력 (→ size 204 ≠ 32 → BAD_SIZE). 이 레슨 펌웨어의 `dump_valid` 는 v2 만 받지만, **해독기(호스트)는 모든 과거 버전을 읽어야** 한다 — 현장엔 옛 펌웨어가 남아 있다.

</details>

### 연습 3 — 크래시 시그니처

`crash_sig(d)` = CRC-32(exc, CFSR & ~(MMARVALID|BFARVALID), pc, lr) — lr 이 EXC_RETURN 이면 그대로, 코드 주소면 bit0 제거.

<details>
<summary>답</summary>

이 레슨 QEMU 실행의 실제 값(UDF_MSP pc 0x274 / lr 0x1d47, BUS_PRECISE 0x2e0 / 0x1c47, IRQ_UDF 0x314 / 0xfffffff9)으로 3000 건, 레지스터·SP·부팅 번호·BFAR 무작위 + VALID 비트·LR bit0 를 무작위로 흔들어도 서명은 정확히 3 개: `45ecf00e 7182463f 622113fc`, 불일치 0. VALID 비트를 빼는 이유: 같은 버그도 BFAR 이 갱신됐느냐에 따라 BFARVALID 가 달라질 수 있다 (부정확 BusFault 등).

</details>

### 연습 4 — @@HEX 전송 무결성

펌웨어와 같은 32 B/줄 hex 인코딩을 C 로 만들고 왕복 확인. 손상 2 만 건(글자 하나 바꾸기 / 줄 누락 / 줄 중복 / 두 줄 뒤바꾸기, 각 5000)이 전부 거절되는지.

<details>
<summary>답</summary>

7 줄(6 × 32 B + 12 B), 왕복 일치. 미검출 0. 글자 하나 = 바이트 하나 변화 → CRC-32 가 항상 검출. 누락·중복 = 길이 ≠ 204. 뒤바꾸기 = 길이는 같지만 CRC 불일치 (확률 2^-32 로만 통과). 그래서 줄마다 번호를 붙이지 않아도 된다 — 전체 CRC 가 이미 순서를 지킨다. 다만 사람이 로그를 잘라 붙일 때 "어느 줄이 빠졌나" 를 알려 주려면 줄 오프셋을 넣는 게 친절하다.

</details>

### 연습 5 — 함대 규모 오수용

`fleet_false_accepts_per_year(devices, boots_per_day, header_words, check_bits)`.

<details>
<summary>답</summary>

100 만 대 × 하루 10 부팅: magic 만 0.85 회/년, magic + CRC-32 1.98e-10 회/년, magic + version + size + CRC 1.07e-29 회/년. magic 만으로는 "1 년에 한 번쯤 유령 크래시 보고" — 실제로 보면 원인 불명의 이상한 덤프 하나가 몇 주 동안 팀을 괴롭힌다. 가정(균일 무작위 RAM)이 틀릴 수 있다는 점(Q2)도 같이 말한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-A09_pr && /tmp/BM-A09_pr    # 23/23 passed
# VM 에서: limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"
```

실행 결과 (실제 출력):

```text
  ex3: 3000 noisy crashes from 3 bugs -> signatures 45ecf00e 7182463f 622113fc, mismatches 0
  ex4: 20000 corrupted transfers (char 5000, drop 5000, dup 5000, swap 5000) -> undetected 0
  ex5: 1e6 devices x 10 boots/day: magic only 0.85/yr, magic+CRC 1.98e-10/yr, magic+ver+size+CRC 1.07e-29/yr
23/23 passed
```

검사 폭별 오수용 데모:

<!-- code: demo_garbage.c -->

## 10. 핵심 정리

- 덤프 = `{magic, version, size, …, 전체 레지스터, EXC_RETURN, MSP/PSP, CFSR/HFSR/MMFAR/BFAR/SHCSR, 스택 N 워드, crc}`. CRC 는 magic 뒤 전부, **magic 은 마지막**, 보고 후 `"READ"` 로 소비 표시.
- 진입 스텁: **push 0 번** → r4–r11 고정 주소 → LR/MSP/PSP/IPSR → **MSP = 전용 fault 스택** → `b` C. 안 바꾸면 SP 오염 시 Lockup (실측, 덤프 0).
- 프레임: EXC_RETURN bit2 PSP / [3:0]=1 handler / bit4=0 104 B. MSTKERR·STKERR·범위·정렬·가드 중 하나라도 걸리면 **읽지 않는다** (PC=0xFFFFFFFF 로 정직하게).
- 2 단계 커밋: 레지스터 봉인 → 위험한 읽기는 scratch → 짧은 안전 구간에서 재봉인. 중첩 fault 는 플래그로 감지해 최소 기록 + 즉시 리셋.
- 세분 fault 를 켜야 덤프 코드에 두 번째 기회가 있다 (HardFault 안의 fault = Lockup). 가드 ≥ 최대 프레임 (32 B 가드는 56 B 프레임에 뚫렸다).
- 검증 순서 magic → version → size → CRC → 필드 상식. 쓰레기 1e6 + 헤더만 맞는 1e6 모두 오수용 0, 1 비트 손상 1632 가지 전부 정확한 코드.
- 보고는 원본 hex + 사람용 한 줄. 해독기가 CRC 재검증, CFSR 이름, **같은 빌드의 nm** 으로 함수+오프셋. 빌드 ID 를 헤더에.
- M7 은 봉인 후 D-cache clean, ECC SRAM 은 POR 때만 초기화 — QEMU 가 보여 주지 않는 실칩 함정.

## 참고자료

- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403E) — B1.5.6 예외 진입 쌓기, B1.5.8 EXC_RETURN, B1.5.x 파생 예외·Lockup, B3.2.15 CFSR (MMFSR/BFSR/UFSR: MSTKERR, STKERR …), B3.5 PMSAv7 MPU (HFNMIENA, PRIVDEFENA) — 절 번호 확인 권장.
- Arm, *Cortex-M3 Devices Generic User Guide* (DUI 0552A) 2.3 예외 모델, 4.3 SCB; Arm Application Note 209 *Using Cortex-M3/M4/M7 Fault Exceptions* (핸들러 스텁 구조, 확인 권장).
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* 3rd ed. — 12 장 fault 처리, Lockup.
- CRC-32/ISO-HDLC 파라미터 (poly 0x04C11DB7 반사 0xEDB88320, init/xorout 0xFFFFFFFF, check 0xCBF43926) — Python `zlib.crc32` 와 동일 (호스트 시험 H1 + 해독기의 zlib 재검증으로 교차 확인).
- QEMU 10.2.1: `-d int` 예외 로그, `info mtree -f` (mps2-an385 메모리 맵 — 0x20000000 아래 "RESERVED 2" RAZ/WI 확인), Lockup 시 `qemu: fatal: Lockup: can't escalate …` (관측). 소스: `target/arm/tcg/m_helper.c` (v7m 예외 진입·쌓기), `hw/intc/armv7m_nvic.c` (파생 예외, 승격).
- STM32 RM0090 RCC_CSR 리셋 플래그 (BM-M10), STM32H7 RM0433 ECC RAM 초기화 주의 (확인 권장), Arm Cortex-M7 TRM D-cache clean (확인 권장).
- Memfault, *Interrupt* 블로그 — "How to debug a HardFault", 코어덤프 수집 설계 (실무 참고).
- 관련 레슨: BM-M13 (fault 정책 + .noinit 리셋 기록, QEMU 리셋 사실), BM-B08 (startup), BM-B09 (UART/세미호스팅), BM-B06 (.noinit 고정 주소 레이아웃), BM-M10 (리셋 원인 레지스터), COMM-B08 / ALGO-M01·M02 (CRC), RTOS-M08 (태스크 스택 측정), BM-A07 (스택 HWM), BM-A08 (MPU 깊이), BM-A04 (이미지 헤더 CRC/SHA). 다음 단계: ARM-M12 (CFSR/HFSR 전체 해독 + unwinding), ARM-M08 (asm 벡터 테이블), TOOL-A07 (링커 스크립트 심화).
