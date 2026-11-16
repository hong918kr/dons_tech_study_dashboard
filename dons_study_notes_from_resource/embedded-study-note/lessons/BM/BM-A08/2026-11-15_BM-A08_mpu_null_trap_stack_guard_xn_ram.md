---
id: BM-A08
time: 120분
updated: 2026-11-15
---
# BM-A08 MPU 설정: NULL 트랩, 스택 가드, 실행 금지 RAM

## 학습 목표

- PMSAv7 MPU 영역 규칙(크기 2^n ≥ 32 B, base 는 크기의 배수, SIZE 필드 = log2(크기) − 1, 서브리전은 256 B 이상에서만 8 조각)을 검사하는 `mpu_encode()` 를 쓰고, 틀린 입력을 **이유별 에러 코드**로 거부할 수 있다.
- 영역 우선순위(번호가 큰 영역이 이긴다), PRIVDEFENA(배경 영역), HFNMIENA(HardFault/NMI 안에서 MPU 를 켤지)를 QEMU 실측으로 설명할 수 있다.
- NULL 역참조, 스택 오버플로, RAM 코드 실행을 각각 MemManage fault 로 바꾸고 CFSR / MMFAR / 쌓인 PC 를 근거로 무엇이 일어났는지 말할 수 있다 (`DACCVIOL+MMARVALID`, `MSTKERR`, `IACCVIOL` 은 MMFAR 무효).
- fault 핸들러가 기록한 뒤 **리셋 없이 복구**(명령 건너뛰기 / 호출자로 복귀 / PSP 태스크 되감기)해서 한 부팅에 14 개 fault 를 처리하는 구조를 만든다.
- "32 B 가드면 충분한가?" 에 실측 숫자로 답한다: 예외 프레임이 가드 아래 2 워드를 덮는 경우, 프레임이 가드를 통째로 건너뛰는 경우, 그리고 가드 크기 규칙.

## 1. 문제

MPU 는 펌웨어 면접에서 "메모리 버그를 **조용한 손상** 대신 **즉시, 정확한 주소가 찍힌 fault** 로 바꾸는 장치" 로 나온다. Don 이 SSD 펌웨어에서 겪었을 "몇 시간 뒤 엉뚱한 곳에서 터지는 메타데이터 손상" 을, 잘못된 접근 바로 그 명령에서 멈추게 하는 것이 목적이다. BM-M13 은 32 B 읽기 전용 영역 하나로 MemManage 를 **한 번** 보였고 fault 마다 리셋했다. 이 레슨은 한 걸음 더 들어간다: 영역 인코더를 규칙대로 직접 만들고, 진짜 Cortex-M3 (QEMU `mps2-an385`, MPU 8 영역) 에서 NULL 트랩 · 스택 가드 · 실행 금지 RAM 을 걸고, 핸들러가 기록 후 **복구**해서 한 번의 부팅으로 시험 14 묶음(T0–T13)을 끝까지 돈다.

아래 그림이 이 레슨의 부팅 배치다 (주소는 실측: `run_qemu.sh syms`, solution 출력 첫 줄). 겹치는 곳은 **번호가 큰 영역이 이긴다**.

```svg
<svg viewBox="0 0 740 380" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">BM-A08 MPU 배치 (QEMU mps2-an385 실측 주소)</text>
  <text x="10" y="44" fill="currentColor">코드 0x00000000 (256 KB)</text>
  <rect x="10" y="52" width="350" height="40" class="f-ok" stroke="currentColor"/>
  <text x="80" y="76" fill="currentColor">R0 FLASH: AP=110 (RO), 실행 허용</text>
  <rect x="10" y="98" width="70" height="40" class="f-acc" stroke="currentColor"/>
  <text x="14" y="114" fill="currentColor">R2 NULL</text>
  <text x="14" y="130" fill="currentColor">0x0–0xFF</text>
  <text x="88" y="114" fill="currentColor">AP=000, XN — R2 &gt; R0 이라 이긴다</text>
  <text x="88" y="130" fill="currentColor">벡터 표 0x00–0xBF 도 여기</text>
  <text x="10" y="156" fill="currentColor">→ .text 는 0x100 부터 (a08.ld ASSERT)</text>
  <text x="400" y="44" fill="currentColor">SRAM 0x20000000 (64 KB)</text>
  <rect x="400" y="52" width="330" height="40" class="f-ok" stroke="currentColor"/>
  <text x="410" y="76" fill="currentColor">R1 SRAM: AP=011 (RW), XN — RAM 실행 금지</text>
  <rect x="400" y="98" width="80" height="40" class="f-acc" stroke="currentColor"/>
  <text x="404" y="114" fill="currentColor">R3 GUARD</text>
  <text x="404" y="130" fill="currentColor">32 B 4E0</text>
  <rect x="486" y="98" width="80" height="40" class="f-warn" stroke="currentColor"/>
  <text x="490" y="114" fill="currentColor">R5 SUB</text>
  <text x="490" y="130" fill="currentColor">256 B 900</text>
  <rect x="572" y="98" width="80" height="40" class="f-warn" stroke="currentColor"/>
  <text x="576" y="114" fill="currentColor">R4 RAMX</text>
  <text x="576" y="130" fill="currentColor">32 B A00</text>
  <text x="400" y="156" fill="currentColor">R4/R5 는 시험 중에만 켠다 (T10, T11)</text>
  <text x="10" y="190" fill="currentColor" font-weight="bold">한 주소의 판정 (ptw.c get_phys_addr_pmsav7 와 같은 순서)</text>
  <rect x="10" y="200" width="200" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="18" y="220" fill="currentColor">MPU 꺼짐 / PPB / 벡터 읽기 /</text>
  <text x="18" y="238" fill="currentColor">HardFault 중 + HFNMIENA=0</text>
  <path d="M210 225 L250 225" stroke="currentColor"/><path d="M250 225 l-8 -4 l0 8 z" fill="currentColor"/>
  <text x="256" y="229" fill="currentColor">→ 기본 메모리 맵 (MPU 무시)</text>
  <rect x="10" y="262" width="200" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="18" y="282" fill="currentColor">R7 → R0 순서로 찾는다</text>
  <text x="18" y="300" fill="currentColor">(꺼진 서브리전 = 안 맞음)</text>
  <path d="M210 287 L250 287" stroke="currentColor"/><path d="M250 287 l-8 -4 l0 8 z" fill="currentColor"/>
  <text x="256" y="291" fill="currentColor">처음 맞는 영역의 AP/XN 으로 판정 → 위반이면 MemManage</text>
  <rect x="10" y="324" width="200" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="18" y="344" fill="currentColor">아무 영역도 안 맞음</text>
  <text x="18" y="362" fill="currentColor">(T12: 0x40028000)</text>
  <path d="M210 349 L250 349" stroke="currentColor"/><path d="M250 349 l-8 -4 l0 8 z" fill="currentColor"/>
  <text x="256" y="345" fill="currentColor">특권 + PRIVDEFENA=1 → 기본 맵 (배경 영역)</text>
  <text x="256" y="363" fill="currentColor">그 외 → MemManage DACCVIOL (MMFAR = 주소)</text>
</svg>
```

### Part A — 영역 인코더와 적재

- 주어진 것: `a08.ld` (FLASH 256 KB / RAM 64 KB, **.text 는 0x100 부터** — NULL 영역 자리), startup (BM-B08 축약판), fault 진입 `Fault_Entry` (naked: MSP/PSP + EXC_RETURN + IPSR → `fault_c`), 복구 정책 3 가지, PSP 태스크 실행기 `run_on_psp()`, 주입 함수(`pc_ldr`/`pc_str`/`pc_big` 라벨), 시험 T0–T13.
- **TODO 1** `mpu_encode(region, base, size, ap, xn, attr, srd, *out)` — 규칙 검사 6 가지 + RBAR/RASR 조립.
- **TODO 2** `mpu_load(tab, n, ctrl)` — 끄기 → 영역 0..7 전부 비활성 → RBAR(VALID)/RASR → CTRL → DSB/ISB.
- **TODO 3** `boot_layout(tab)` — 위 그림의 4 영역 (번호 = 우선순위 설계).
- **TODO 4** `thumb_insn_len(hw)` — fault 난 명령을 건너뛰려면 길이(2/4 B)를 알아야 한다.

### Part B — 세 가지 버그를 fault 로 (그리고 그 함정)

- **NULL 트랩**: `ldr [0]`, `ldr [0x10]` (NULL 구조체의 멤버), `str [0]` → MemManage `CFSR=0x82`, MMFAR = 그 주소. 충돌: 벡터 표도 0x0–0xFF 안 — 소프트웨어 읽기(`ldr [4]`)는 막히고, **예외 진입의 벡터 읽기는 통과**하는지 QEMU 에서 확인.
- **스택 가드**: PSP 태스크의 무한 재귀 → 가드에서 MemManage. 실측 `CFSR=0x92` (DACCVIOL + MMARVALID + **MSTKERR**) 와 "가드 아래 2 워드 손상" 의 이유, 64 B 가드로 0 이 되는 것. 예외 진입의 쌓기만으로 생기는 `MSTKERR` 단독 (`CFSR=0x10`, MMFAR 무효). 프레임이 가드보다 큰 함수(160 B)는 32 B 가드를 **건너뛴다**.
- **실행 금지 RAM**: RAM 에 복사한 `movs r0,#42; bx lr` 호출 → `IACCVIOL` (`CFSR=0x01`, MMFAR 무효, 쌓인 PC = RAM 주소). 더 높은 번호 영역(R4, AP=RO, XN=0)으로 32 B 만 실행 허용 → 42, 그 영역에 쓰기 → fault (W^X).

### Part C — 한 부팅 안의 기록·복구, 그리고 CTRL 비트

- 핸들러는 MPU 를 잠시 끄고 기록 → CFSR W1C → 정책별 복구 → MPU 복원. `REC_SKIP` (쌓인 PC += 명령 길이), `REC_RETURN` (PC = LR, r0 = 표식), `REC_ABORT_TASK` (PSP 에 새 프레임 → `task_abort_landing`).
- 서브리전(T11), PRIVDEFENA=0 (T12: 주변장치 읽기 fault, PPB 는 통과), HFNMIENA (T13: HardFault 안에서 NULL 이 읽힌다 / `lockup` 모드: HFNMIENA=1 이면 QEMU 가 Lockup 으로 종료).
- 모든 결과를 `T# … CFSR= MMFAR= pc=` 한 줄 로그로 남기고 95 개 검사 → `SYS_EXIT 0`.

## 2. Problem (English)

> **Configure the ARMv7-M MPU of a Cortex-M3 (QEMU `mps2-an385`, 8 regions) to trap NULL dereferences, task-stack overflow and execution from RAM — and recover from each fault without a reset.**
>
> 1. `mpu_rc_t mpu_encode(uint32_t region, uint32_t base, uint64_t size, uint32_t ap, uint32_t xn, uint32_t attr, uint32_t srd, mpu_region_t *out)` returns `MPU_ERR_REGION` (region ≥ 8), `MPU_ERR_SIZE` (not a power of two in [32 B, 4 GB]), `MPU_ERR_ALIGN` (base not a multiple of size), `MPU_ERR_SRD` (srd > 0xFF, or srd ≠ 0 for size < 256 B), `MPU_ERR_AP` (AP > 7 or the reserved 0b100), `MPU_ERR_ATTR` (xn > 1 or reserved TEX/C/B). On success: `RBAR = base | VALID | region`, `RASR = XN<<28 | AP<<24 | attr<<16 | SRD<<8 | (log2(size)−1)<<1 | ENABLE`. On error `*out` is untouched.
> 2. `void mpu_load(const mpu_region_t *tab, uint32_t n, uint32_t ctrl)` disables the MPU, clears all 8 regions, programs the table, writes `MPU_CTRL = ctrl`, then `DSB; ISB`.
> 3. Layout: R0 flash 256 KB RO executable, R1 SRAM 64 KB RW XN, R2 `[0, 0x100)` no-access XN, R3 32-byte no-access guard below the task stack. `ctrl = PRIVDEFENA | ENABLE`.
> 4. One MemManage/HardFault handler records `{IPSR, CFSR, HFSR, MMFAR if MMARVALID, stacked PC/LR unless MSTKERR}`, clears CFSR, and recovers by policy: skip the faulting instruction (`uint32_t thumb_insn_len(uint16_t first_halfword)`), return to the caller, or rewind a PSP task.
>
> Expected: `ldr [0]` → `exc=4, CFSR=0x00000082, MMFAR=0x0`; `ldr [0x10]` → `MMFAR=0x10`; calling RAM code → `CFSR=0x00000001`, MMFAR invalid, stacked PC = function address; IRQ entry with PSP 16 B above the guard → `CFSR=0x00000010`. `mpu_encode(2, 0, 256, AP_NONE, 1, 0x02, 0)` → `RBAR=0x00000012, RASR=0x1002000F`.
>
> Follow-ups: The vector table is at address 0 — how do exceptions still work with a no-access region there? Is a 32-byte guard enough? Why does the overflow come with `MSTKERR`? What does `PRIVDEFENA=0` cost you? What changes on ARMv8-M?

## 3. 시작 코드

<!-- code: starter.c -->

같이 쓰는 파일: `a08.ld`, `run_qemu.sh` (VM 빌드·실행), `demo_probe.c` (QEMU 가 규칙 위반 설정을 어떻게 다루는지), `practice.c` (호스트 C).

<!-- code: a08.ld -->

실행은 Lima VM `elinux` 안에서 (Mac 홈은 VM 에서 읽기 전용 → 스크립트가 `/tmp/BM-A08_work` 로 복사해 빌드). **한 번의 부팅**으로 끝나고 `SYS_EXIT` 로 종료 (`timeout 20` 안전망). Mac 에서:

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-A08
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # 21/87, qemu exit=1
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"   # 95/95, qemu exit=0
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh probe"      # 규칙 위반 설정 실측 (-d guest_errors)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh lockup"     # HFNMIENA=1 + HardFault 안의 NULL 읽기
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh syms"       # 라벨 주소, 명령 길이, recurse 프레임
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh nullub"     # C 로 *(uint32_t*)0 을 쓰면 GCC 가 만드는 것
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # practice.c (호스트 C)
```

시작 코드 실행 결과 (실제 출력 발췌 — FAIL 66 줄 중 일부):

```text
T0 MPU_TYPE=0x00000800 CTRL=0x00000000
FAIL starter.c:430  mpu_encode(2, 0x0u, 256u, AP_NONE, 1u, ATTR_FLASH, 0u, &r) == MPU_OK  [T1 encode]
...
boot layout loaded: CTRL=0x00000000
T2 NULL read   faults=0 exc=0 CFSR=0x00000000 MMFAR=(invalid, raw 0x00000000) pc=0x00000000 result=0x20010000
T4 NULL write  faults=0 exc=0 CFSR=0x00000000 MMFAR=(invalid, raw 0x00000000) pc=0x00000000 result=0x00000000
FAIL starter.c:505  peek_nompu(0x0u) == estack  [T4 NULL write ]
T6 NULL is R0  faults=0 exc=0 CFSR=0x00000000 MMFAR=(invalid, raw 0x00000000) pc=0x00000000 result=0x00000bad
T7 recursion32 faults=0 exc=0 CFSR=0x00000000 MMFAR=(invalid, raw 0x00000000) pc=0x00000000 result=0x00125b22
   depth reached=53 victim damaged words=27 victim[54..55]=0x5a5a5a5a 0x0000024f
T10 XN RAM     faults=0 exc=0 CFSR=0x00000000 MMFAR=(invalid, raw 0x00000000) pc=0x00000000 result=0x0000002a
...
total faults handled in ONE boot: 0
21/87 passed
qemu exit=1
```

MPU 가 없으면 무슨 일이 일어나는지가 그대로 보인다: NULL 읽기는 초기 SP 값 `0x20010000` 을 돌려주고, NULL 쓰기는 **벡터[0] 을 0xBAD 로 바꿔 버리며** (T6 이 그 값을 읽는다 — QEMU 의 코드 메모리는 쓰기 가능한 SSRAM), 재귀는 가드를 지나 victim 27 워드를 덮고, RAM 코드는 그냥 실행된다. (시작 코드는 `boot_layout` 이 0 개를 돌려줘서 되읽기 검사 8 개가 빠지므로 총 검사 수가 87 이다.)

## 4. 힌트

<details><summary>힌트 1 — 크기 검사 한 줄</summary>

2 의 거듭제곱 판정은 `(size & (size - 1)) == 0`. 4 GB 를 표현하려고 `size` 가 `uint64_t` 다. 범위 `32 ≤ size ≤ 1<<32`. 정렬은 `base & (size - 1)` (64 비트에서 계산). SIZE 필드는 log2 − 1: 32 B → 4, 256 B → 7, 4 GB → 31.
</details>

<details><summary>힌트 2 — RBAR.VALID 로 RNR 을 건너뛴다</summary>

RBAR 에 VALID(bit4)=1 과 REGION[3:0] 을 같이 쓰면 하드웨어가 RNR 을 그 번호로 바꾼다 (QEMU 실측 P2: `RBAR VALID|5 -> RNR=0x00000005`). 그래서 적재는 `RBAR = tab[i].rbar; RASR = tab[i].rasr;` 두 줄이면 된다. 되읽은 RBAR 에는 VALID 가 0 이고 REGION 은 현재 RNR 이다.
</details>

<details><summary>힌트 3 — 왜 "전부 비활성" 부터?</summary>

이전 적재(또는 부트로더)가 켜 둔 영역 6 번이 남아 있으면 새 표에 없는데도 살아 있다. 그리고 바꾸는 동안 MPU 를 꺼 두면 "반쯤 바뀐 표" 로 접근하는 순간이 없다. 마지막 `DSB; ISB` 는 이후 명령 인출·접근이 새 설정을 보게 한다.
</details>

<details><summary>힌트 4 — NULL 영역은 FLASH 영역보다 번호가 커야</summary>

두 영역이 0x0–0xFF 에서 겹친다. 높은 번호가 이긴다 → NULL 이 2 번, FLASH 가 0 번. 가드(3 번)도 SRAM(1 번) 위에 있어야 한다. T6 이 반대로 하면 NULL 읽기가 그대로 통과함을 보인다.
</details>

<details><summary>힌트 5 — Thumb-2 명령 길이</summary>

첫 halfword 의 비트 [15:11] 이 `0b11101`, `0b11110`, `0b11111` 이면 32 비트. `0b11100` 은 16 비트 무조건 분기 `B` 다 (`0xE000` → 2). 32 비트 `str.w` (`0xF8C0 …`) 를 2 B 만 건너뛰면 두 번째 halfword `0x1000` 이 16 비트 명령으로 실행된다.
</details>

## 5. 풀이

### Part A — 규칙 → 비트

RBAR / RASR 레이아웃 (ARMv7-M ARM B3.5 — 확인 권장. QEMU 는 RASR 의 `0x173FFF3F` 비트만 저장한다: probe P4).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">MPU_RBAR (0xE000ED9C) / MPU_RASR (0xE000EDA0)</text>
  <rect x="10" y="30" width="560" height="36" class="f-ok" stroke="currentColor"/>
  <text x="200" y="53" fill="currentColor">ADDR [31:5] (실제로는 [31:N], N = log2 크기)</text>
  <rect x="570" y="30" width="40" height="36" class="f-acc" stroke="currentColor"/>
  <text x="574" y="53" fill="currentColor">VALID</text>
  <rect x="610" y="30" width="120" height="36" class="f-warn" stroke="currentColor"/>
  <text x="620" y="53" fill="currentColor">REGION [3:0]</text>
  <text x="10" y="82" fill="currentColor">예: 0x200004F3 = base 0x200004E0 | VALID | 3 (가드)</text>
  <rect x="10" y="100" width="40" height="36" class="f-acc" stroke="currentColor"/>
  <text x="18" y="123" fill="currentColor">XN</text>
  <rect x="50" y="100" width="90" height="36" class="f-warn" stroke="currentColor"/>
  <text x="60" y="123" fill="currentColor">AP [26:24]</text>
  <rect x="140" y="100" width="150" height="36" class="f-ok" stroke="currentColor"/>
  <text x="148" y="123" fill="currentColor">TEX[21:19] S C B</text>
  <rect x="290" y="100" width="150" height="36" class="f-warn" stroke="currentColor"/>
  <text x="300" y="123" fill="currentColor">SRD [15:8] 조각 끄기</text>
  <rect x="440" y="100" width="170" height="36" class="f-ok" stroke="currentColor"/>
  <text x="450" y="123" fill="currentColor">SIZE [5:1] = log2 − 1</text>
  <rect x="610" y="100" width="120" height="36" class="f-acc" stroke="currentColor"/>
  <text x="620" y="123" fill="currentColor">ENABLE [0]</text>
  <text x="10" y="152" fill="currentColor">b28</text>
  <text x="140" y="152" fill="currentColor">b21</text>
  <text x="290" y="152" fill="currentColor">b15</text>
  <text x="440" y="152" fill="currentColor">b5</text>
  <text x="700" y="152" fill="currentColor">b0</text>
  <text x="10" y="182" fill="currentColor">0x1002000F = XN | AP 000 | attr 0x02 | SRD 0 | SIZE 7 (256 B) | EN  → R2 NULL</text>
  <text x="10" y="202" fill="currentColor">0x10030009 = XN | AP 000 | attr 0x03 | SRD 0 | SIZE 4 (32 B)  | EN  → R3 GUARD</text>
  <text x="10" y="222" fill="currentColor">0x10030F0F = XN | AP 000 | attr 0x03 | SRD 0x0F | SIZE 7     | EN  → 256 B, 조각 0..3 꺼짐</text>
  <text x="10" y="242" fill="currentColor">0x06020023 = AP 110 (RO) | attr 0x02 | SIZE 17 (256 KB) | EN    → R0 FLASH, 실행 허용</text>
</svg>
```

검사 순서와 이유:

| 규칙 | 코드 | 왜 |
|---|---|---|
| 영역 번호 < DREGION (8) | `MPU_ERR_REGION` | QEMU 는 RNR ≥ 8 쓰기를 무시하고 `MPU region out of range 9/8` 경고 (probe P3) |
| 크기 = 2^k, 5 ≤ k ≤ 32 | `MPU_ERR_SIZE` | 비교기가 "상위 비트 일치" 로 구현됨. SIZE<4 는 UNPREDICTABLE (ARM ARM, 확인 권장) |
| base % 크기 == 0 | `MPU_ERR_ALIGN` | 영역 = 주소의 상위 (32−k) 비트가 같은 블록. QEMU 는 오정렬 영역을 **조용히 무시** (P5) |
| SRD 는 크기 ≥ 256 B 에서만 | `MPU_ERR_SRD` | 조각 = 크기/8 ≥ 32 B. QEMU 는 128 B 영역의 SRD 를 무시해 영역 전체가 살아 있다 (P6) |
| AP ≠ 100 | `MPU_ERR_AP` | 예약값. QEMU 는 "권한 없음" 으로 다룸 + 경고 (P7) |
| TEX/C/B 예약 조합 아님 | `MPU_ERR_ATTR` | 001-01, 010-{01,10,11}, 011-xx (Table B3-13 기억 기반 — 확인 권장). 합법 코드 48/64 (연습 1) |

AP 표 (특권 / 비특권, ARMv7-M — 확인 권장): `000` 없음/없음, `001` RW/없음, `010` RW/RO, `011` RW/RW, `100` 예약, `101` RO/없음, `110` RO/RO, `111` RO/RO. 실행은 "읽기 가능 + XN=0" 일 때만 (QEMU `ptw.c` 의 AP 디코드도 읽기 권한에 PAGE_EXEC 를 묶는다).

**적재 순서** (`mpu_load`): `CTRL=0; DSB; ISB` → `RNR=i; RASR=0` (i=0..7) → 표 → `CTRL=ctrl; DSB; ISB`. 영역 6 번에 "예전 영역" 을 남겨 두고 다시 적재하는 검사가 있다 — 비활성 루프를 빼면(돌연변이 M5) `MPU_RASR == 0` 과 서브리전 시험 2 개가 FAIL.

**부팅 배치** (`boot_layout`) 실측 되읽기: `R0=0x00000010/0x06020023 R1=0x20000011/0x1303001f R2=0x00000012/0x1002000f R3=0x200004f3/0x10030009`, `CTRL=0x00000005` (PRIVDEFENA|ENABLE). `MPU_TYPE=0x00000800` → DREGION=8, SEPARATE=0 (통합 MPU). 이 값은 QEMU `armv7m_nvic.c` 의 `return cpu->pmsav7_dregion << 8;` (v10.2.1 1357 행 근처) 와 같다.

### Part B — 실측 표

solution 실제 출력 (`run_qemu.sh solution`, 한 부팅):

```text
   text	   data	    bss	    dec	    hex	filename
  12540	      4	   2152	  14696	   3968	solution.elf
BM-A08 MPU on QEMU mps2-an385  CPUID=0x410fc231 VTOR=0x00000000 &g_ta=0x20000400 g_sub=0x20000900 g_ram_code=0x20000a00
T0 MPU_TYPE=0x00000800 CTRL=0x00000000
boot layout loaded: CTRL=0x00000005 R0=0x00000010/0x06020023 R1=0x20000011/0x1303001f R2=0x00000012/0x1002000f R3=0x200004f3/0x10030009
T2 NULL read   faults=1 exc=4 CFSR=0x00000082 MMFAR=0x00000000 pc=0x00000210 result=0xeeeeeeee
T3 NULL->0x10  faults=1 exc=4 CFSR=0x00000082 MMFAR=0x00000010 pc=0x00000210 result=0xeeeeeeee
T4 NULL write  faults=1 exc=4 CFSR=0x00000082 MMFAR=0x00000000 pc=0x00000214 result=0x00000000
T5 read vec[1] faults=1 exc=4 CFSR=0x00000082 MMFAR=0x00000004 pc=0x00000210 result=0xeeeeeeee
T5 IRQ7 vector faults=0 exc=0 CFSR=0x00000000 MMFAR=(invalid, raw 0x00000000) pc=0x00000000 result=0x00000001
T6 NULL is R0  faults=0 exc=0 CFSR=0x00000000 MMFAR=(invalid, raw 0x00000000) pc=0x00000000 result=0x20010000
T7 recursion32 faults=1 exc=4 CFSR=0x00000092 MMFAR=0x200004f8 pc=0xffffffff result=0xdead7a5c
   depth reached=42 victim damaged words=2 victim[54..55]=0x0000002b 0x20000420
T7 recursion64 faults=1 exc=4 CFSR=0x00000092 MMFAR=0x200004f8 pc=0xffffffff result=0xdead7a5c
T8 MSTKERR     faults=1 exc=4 CFSR=0x00000010 MMFAR=(invalid, raw 0x200004f8) pc=0xffffffff result=0xdead7a5c
   irq7 runs=1
T9 big frame32 faults=0 exc=0 CFSR=0x00000000 MMFAR=(invalid, raw 0x00000000) pc=0x00000000 result=0xb16f0000
T9 guard 256 B faults=1 exc=4 CFSR=0x00000092 MMFAR=0x20000480 pc=0xffffffff result=0xdead7a5c
T10 XN RAM     faults=1 exc=4 CFSR=0x00000001 MMFAR=(invalid, raw 0x20000480) pc=0x20000a00 result=0xfa11fa11
T10 RAMX region faults=0 exc=0 CFSR=0x00000000 MMFAR=(invalid, raw 0x00000000) pc=0x00000000 result=0x0000002a
T10 W^X write  faults=1 exc=4 CFSR=0x00000082 MMFAR=0x20000a00 pc=0x00000214 result=0x00000000
T11 SRD sub0   faults=0 exc=0 CFSR=0x00000000 MMFAR=(invalid, raw 0x00000000) pc=0x00000000 result=0x00000000
T11 SRD sub3   faults=0 exc=0 CFSR=0x00000000 MMFAR=(invalid, raw 0x00000000) pc=0x00000000 result=0x00000000
T11 SRD sub4   faults=1 exc=4 CFSR=0x00000082 MMFAR=0x20000980 pc=0x00000214 result=0x00000000
T11 SRD sub7   faults=1 exc=4 CFSR=0x00000082 MMFAR=0x200009fc pc=0x00000214 result=0x00000000
T12 noPRIVDEF  faults=1 exc=4 CFSR=0x00000082 MMFAR=0x40028000 pc=0x00000210 result=0xeeeeeeee
T12 PPB CPUID  faults=0 exc=0 CFSR=0x00000000 MMFAR=(invalid, raw 0x00000000) pc=0x00000000 result=0x410fc231
T12 PRIVDEF=1  faults=0 exc=0 CFSR=0x00000000 MMFAR=(invalid, raw 0x00000000) pc=0x00000000 result=0x00000000
T13 HF probe   faults=1 exc=3 CFSR=0x00000082 MMFAR=0x00000000 pc=0x00000210 result=0xeeeeeeee
   HardFault read *0 = 0x20010000 HFSR=0x40000000
total faults handled in ONE boot: 14
95/95 passed
qemu exit=0
```

`pc_ldr=0x210` (`6810 ldr r0,[r2,#0]`, 16 비트), `pc_str=0x214` (`f8c0 1000 str.w r1,[r0]`, 32 비트) — `run_qemu.sh syms` 실측. 쌓인 PC 는 언제나 **fault 난 명령 자신**의 주소이고, 복구 뒤 돌아간 PC 는 각각 0x212, 0x218 로 검사된다 (`g_f.resume`).

**NULL 트랩과 벡터 표의 충돌.** mps2-an385 는 코드가 0x0 에 있고 벡터 표(48×4 = 192 B)도 거기 있다. 세 가지가 동시에 성립해야 한다: (1) 소프트웨어의 `[0x0..0xFF]` 접근은 fault — T2~T5 실측 ✓, (2) 예외 진입의 벡터 읽기는 성공 — T5 의 IRQ7 (벡터 주소 0x5C, NULL 영역 안) 이 정상 실행됐고, 모든 fault 핸들러도 벡터 0x10/0x0C 로 들어왔다 ✓, (3) 코드는 영역 밖 — `a08.ld` 가 `.text` 를 0x100 에 놓고 `ASSERT(_stext >= _null_trap_size)` 로 지킨다. (2) 는 아키텍처 규칙("벡터 표 읽기는 항상 기본 메모리 맵" — ARMv7-M ARM B3.5, 확인 권장) 이고, QEMU 도 그렇게 구현했다: `target/arm/tcg/m_helper.c` v10.2.1 678–684 행 주석 *"the rules for vector loads are special: they always use the default memory map"* 그리고 MPU 조회 없이 `address_space_ldl()` 로 직접 읽는다. 부트로더가 앱 벡터 표를 **소프트웨어로** 읽는 코드(BM-A03 의 sanity check)는 MPU 를 켜기 전에 하거나 영역 밖 별칭 주소로 읽어야 한다.

실기 메모: STM32F4 는 0x0 이 부팅 메모리(보통 플래시 0x08000000)의 **별칭**이라, MPU 가 없으면 NULL 읽기가 초기 SP 값을 조용히 돌려준다 (시작 코드 T2 의 `0x20010000` 과 같은 현상). VTOR 을 0x08000000 으로 두고 0x0 에 NULL 영역을 걸면 충돌 자체가 없다 (RM0090 메모리 맵, 확인 권장).

**MSTKERR 과 "가드 아래 2 워드".** 아래 그림은 T7 의 실측 주소다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">T7: 재귀 오버플로 (g_ta = 0x20000400, PSP 스택은 위에서 아래로)</text>
  <rect x="60" y="40" width="200" height="60" class="f-ok" stroke="currentColor"/>
  <text x="70" y="64" fill="currentColor">stack  0x500–0x8FF</text>
  <text x="70" y="84" fill="currentColor">recurse 프레임 24 B × 42</text>
  <rect x="60" y="100" width="200" height="50" class="f-acc" stroke="currentColor"/>
  <text x="70" y="122" fill="currentColor">guard 0x4E0–0x4FF (32 B)</text>
  <text x="70" y="140" fill="currentColor">AP=000</text>
  <rect x="60" y="150" width="200" height="60" class="f-warn" stroke="currentColor"/>
  <text x="70" y="174" fill="currentColor">victim 0x400–0x4DF</text>
  <text x="70" y="194" fill="currentColor">(다른 데이터)</text>
  <path d="M260 104 L330 104" stroke="currentColor"/>
  <text x="336" y="100" fill="currentColor">① recurse(43): push lr @0x50C (OK), sub sp,#20 → SP=0x4F8</text>
  <text x="336" y="118" fill="currentColor">② str r0,[sp] @0x4F8 → DACCVIOL, MMFAR=0x4F8</text>
  <path d="M260 160 L330 160" stroke="currentColor"/>
  <text x="336" y="146" fill="currentColor">③ 예외 진입: 프레임 32 B 를 SP−32 = 0x4D8 부터 위로 쓴다</text>
  <text x="336" y="164" fill="currentColor">   r0 → 0x4D8 (=n 0x2b), r1 → 0x4DC: victim 손상</text>
  <text x="336" y="182" fill="currentColor">   r2 → 0x4E0 = 가드 → MSTKERR, 쌓기 중단 (QEMU 순서)</text>
  <text x="336" y="200" fill="currentColor">   CFSR = 0x92, 프레임 없음 → PC 를 못 믿음</text>
  <rect x="10" y="230" width="720" height="90" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="250" fill="currentColor">규칙: SP 는 가드에 "들어간 뒤에야" 잡히고, 그때 예외 프레임이 SP 아래로 또 쌓인다.</text>
  <text x="20" y="268" fill="currentColor">가드 ≥ (한 번에 SP 가 내려가는 최대량) + (예외 프레임 32 B, FP 확장이면 104 B), 2 의 거듭제곱으로 올림.</text>
  <text x="20" y="286" fill="currentColor">이 함수: 24 + 32 = 56 → 64 B. 실측 recursion64: 같은 MMFAR 0x4F8, victim 손상 0.</text>
  <text x="20" y="304" fill="currentColor">T9: 160 B 를 한 번에 내리는 함수는 32 B 가드를 건너뛰어 0x480 에 씀 (fault 0, 손상 1) → 256 B 가드에서 잡힘.</text>
</svg>
```

QEMU 의 쌓기는 `v7m_push_stack()` 이 r0 (가장 낮은 주소) 부터 `v7m_stack_write()` 를 `&&` 로 이어 부르다 처음 실패에서 멈추고, 실패 시 `CFSR.MSTKERR` 를 세우고 MemManage 를 pend 한다 (`m_helper.c` v10.2.1 213–300 행, 250–251 행). 실기에서 쌓기 쓰기의 순서와 "가드 위쪽 워드는 써지는가" 는 구현 정의 — 그래서 결론은 **QEMU 숫자(2 워드)가 아니라 크기 규칙**이다. 연습 4 의 `guard_sim()` 이 같은 모델로 2 와 0 을 다시 계산하고, 무작위 프레임 열 5000 개에서 규칙의 충분성을 확인한다.

**MSTKERR 단독 (T8).** PSP = 가드 위 16 B 인 태스크가 IRQ7 을 pend → 예외 진입 쌓기가 가드에 닿는다 → `CFSR=0x00000010`. MMARVALID 가 없으므로 MMFAR 의 원시 값 `0x200004f8` 은 **T7 의 낡은 값**이다 (BM-M13 의 BFAR 낡은 값과 같은 함정). 핸들러가 태스크를 되감은 뒤, 아직 pending 인 IRQ7 이 새 PSP 위에서 정상 실행된다 (`irq7 runs=1`).

**XN (T10).** 명령 인출 위반은 `IACCVIOL` 만 서고 MMFAR 는 무효 — 위반 주소는 쌓인 PC (`0x20000a00`) 에서 얻는다 (`m_helper.c` 2339–2340: IACCVIOL 경로는 MMFAR 를 쓰지 않는다; DACCVIOL 경로 2343–2347 은 MMARVALID 와 MMFAR 를 쓴다). 핸들러가 `PC = LR` 로 돌려보내 호출이 `0xfa11fa11` 을 받는다. 그 32 B 를 R4 (AP=110 RO, XN=0) 로 덮으면 **번호가 큰 R4 가 R1 의 XN 을 이겨** 42 를 돌려받고, 쓰기는 막힌다 (W^X).

**MPU 를 켜기 전 함정 하나 — C 로 주소 0 읽기.** 처음 쓴 `peek_nompu()` 는 `*(volatile uint32_t *)0` 였는데, 실행하자 UsageFault(UNDEF) 가 났다. `run_qemu.sh nullub` 실측:

```text
--- arm-none-eabi-gcc -O2 
00000000 <peek0>:
   0:	2300      	movs	r3, #0
   2:	681b      	ldr	r3, [r3, #0]
   4:	deff      	udf	#255	@ 0xff
--- arm-none-eabi-gcc -O2 -fno-delete-null-pointer-checks
00000000 <peek0>:
   0:	2300      	movs	r3, #0
   2:	6818      	ldr	r0, [r3, #0]
   4:	4770      	bx	lr
```

GCC 는 NULL 역참조를 UB 로 보고 읽기 **뒤에** `udf` 트랩을 심는다. 펌웨어에서 주소 0 이 진짜 메모리라면 `-fno-delete-null-pointer-checks` (Linux 커널도 쓴다) 또는 asm 접근이 필요하다. 이 레슨은 `raw_load()` asm 을 쓴다.

QEMU 의 "규칙 위반 설정" 실측 (`run_qemu.sh probe`, `-d guest_errors`):

```text
P1 reset: MPU_TYPE=0x00000800 CTRL=0x00000000 RNR=0x00000000 RASR0=0x00000000
P2 RBAR VALID|5 -> RNR=0x00000005 RBAR(read)=0x20000405
MPU region out of range 9/8
P3 RNR=9 -> RNR=0x00000005
P4 RASR=0xFFFFFFFF -> read 0x173fff3f (구현된 비트만 남음)
DRBAR[0]: 0x20000500 misaligned to DRSR region size, mask = 0x3ff
P5 base misaligned (1KB @+256)  RASR(read)=0x00000013 access 0x20000500 -> no fault  CFSR=0x00000000
P6 128B region, SRD=0xFF        RASR(read)=0x0000ff0d access 0x20000400 -> FAULT CFSR=0x00000082
DRACR[0]: Bad value for AP bits: 0x4
P7 AP=100 (reserved)            RASR(read)=0x04000013 access 0x20000400 -> FAULT CFSR=0x00000082
P8 SIZE=3 (16 B) inside         RASR(read)=0x00000007 access 0x20000400 -> FAULT CFSR=0x00000082
P8 SIZE=3 (16 B) +16            RASR(read)=0x00000007 access 0x20000410 -> no fault  CFSR=0x00000000
P9 control: 1KB aligned AP=000  RASR(read)=0x00000013 access 0x20000500 -> FAULT CFSR=0x00000082
MPU_CTRL: HFNMIENA and !ENABLE is UNPREDICTABLE
P10 CTRL=HFNMIENA only -> read 0x00000002
qemu exit=0
```

(guest_errors 경고 줄은 접근마다 반복되어 P5 앞에 세 번 찍힌다 — 위에선 한 번만 남김.) 핵심: **오정렬 base 는 QEMU 에서 아무 경고 없이 보호가 사라진다** (P5 — 펌웨어는 `-d guest_errors` 없이 돌므로 모른다). 실기는 UNPREDICTABLE 이다. 인코더가 `MPU_ERR_ALIGN` 을 돌려줘야 하는 이유. P8 처럼 QEMU 는 16 B 영역도 받아 주지만 실기에서 SIZE < 4 는 UNPREDICTABLE — 에뮬레이터에서 "된다" 를 사양으로 착각하지 말 것.

### Part C — 한 부팅에서 복구하기

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">fault_c(): 기록 → W1C → 복구 → MPU 복원 (BM-M13 은 여기서 리셋)</text>
  <rect x="10" y="32" width="160" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="18" y="52" fill="currentColor">Fault_Entry (naked)</text>
  <text x="18" y="70" fill="currentColor">LR bit2 → MSP/PSP</text>
  <path d="M170 60 L200 60" stroke="currentColor"/><path d="M200 60 l-8 -4 l0 8 z" fill="currentColor"/>
  <rect x="200" y="32" width="250" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="208" y="52" fill="currentColor">CTRL 저장 → MPU 끔 (프레임이 가드 안일 수도)</text>
  <text x="208" y="70" fill="currentColor">CFSR/HFSR/MMFAR(VALID 일 때만) 기록</text>
  <path d="M450 60 L480 60" stroke="currentColor"/><path d="M480 60 l-8 -4 l0 8 z" fill="currentColor"/>
  <rect x="480" y="32" width="250" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="488" y="52" fill="currentColor">MSTKERR 면 프레임 = 없음</text>
  <text x="488" y="70" fill="currentColor">CFSR = cfsr (W1C), 정책 선택</text>
  <rect x="10" y="120" width="230" height="74" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="18" y="140" fill="currentColor" font-weight="bold">REC_SKIP</text>
  <text x="18" y="158" fill="currentColor">frame[6] += thumb_insn_len()</text>
  <text x="18" y="176" fill="currentColor">T2–T5, T10 W^X, T11, T12, T13</text>
  <rect x="255" y="120" width="230" height="74" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="263" y="140" fill="currentColor" font-weight="bold">REC_RETURN</text>
  <text x="263" y="158" fill="currentColor">PC = LR&amp;~1, r0 = 0xfa11fa11</text>
  <text x="263" y="176" fill="currentColor">T10 XN (호출된 함수 자체가 금지)</text>
  <rect x="500" y="120" width="230" height="74" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="508" y="140" fill="currentColor" font-weight="bold">REC_ABORT_TASK</text>
  <text x="508" y="158" fill="currentColor">PSP 꼭대기에 새 프레임, PC = 착지점</text>
  <text x="508" y="176" fill="currentColor">EXC_RETURN=0xFFFFFFFD (T7–T9)</text>
  <path d="M370 88 L125 120" stroke="currentColor" fill="none"/>
  <path d="M370 88 L370 120" stroke="currentColor" fill="none"/>
  <path d="M600 88 L615 120" stroke="currentColor" fill="none"/>
  <rect x="10" y="212" width="720" height="40" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="236" fill="currentColor">CTRL 복원 + DSB/ISB → bx EXC_RETURN. 64 번 넘게 fault 면 SYS_EXIT 3 (무한 루프 방지)</text>
</svg>
```

- **왜 태스크는 PSP 위에서?** 가드에 닿은 SP 로는 핸들러가 아무것도 쌓을 수 없다. 스레드가 PSP 를 쓰면 핸들러는 MSP 로 돌아 안전하다 — RTOS 가 태스크마다 가드를 두는 구조와 같다 (RTOS-M08, RTOS-A01). MSP 자체의 오버플로는 핸들러도 같은 스택을 쓰므로 이렇게 복구할 수 없다: 핸들러 진입 쌓기부터 실패 → 승격 → Lockup 위험. v8-M 의 MSPLIM 이 이 문제를 하드웨어로 푼다 (확인 권장).
- **되감기 = longjmp 의 하드웨어 판.** `run_on_psp` 가 MSP 에 `{r4, lr}` 을 저장하고 PSP 로 전환해 함수를 부른다. 핸들러는 PSP 꼭대기에 `{r0=TASK_ABORTED, …, pc=task_abort_landing, xpsr=T}` 프레임을 만들고 `0xFFFFFFFD` 로 복귀한다. 착지점이 CONTROL.SPSEL 을 0 으로 돌리고 `pop {r4, pc}` — 호출자는 `0xdead7a5c` 를 받는다.
- **HFNMIENA (T13).** MEMFAULTENA 를 끄고 NULL 읽기 → HardFault (FORCED, `HFSR=0x40000000`, CFSR 에 원인 0x82 유지). HardFault 안에서 0x0 을 읽으면 `0x20010000` — HFNMIENA=0 이면 HardFault/NMI(음의 우선순위) 동안 MPU 가 꺼진 것처럼 동작한다 (QEMU `ptw.c` `regime_translation_disabled()` 255–270 행: `ENABLE` 만이면 `mmu_idx & ARM_MMU_IDX_M_NEGPRI` 일 때 비활성). HFNMIENA=1 로 같은 일을 하면 (`run_qemu.sh lockup`):

```text
qemu: fatal: Lockup: can't escalate 3 to HardFault (current priority -1)
R12=2000ff60 R13=2000ff40 R14=000001b7 R15=00000538
XPSR=61000003 -ZC- T handler
qemu exit=134
```

  R15=0x538 은 `fault_c` 안의 그 `ldr` 이다. 실기의 Lockup 은 코어가 멈추고 (디버거/워치독만이 깨운다) QEMU 는 프로세스를 abort 한다 — BM-B08 F3, BM-M13 과 같은 관찰. 그래서 보통 HFNMIENA=0 이 기본값이다: 마지막 방어선인 HardFault 핸들러가 MPU 설정 실수로 죽지 않게.

- **PRIVDEFENA (T12).** 0 이면 어떤 영역에도 없는 주소는 특권 코드여도 fault (`MMFAR=0x40028000`, FPGAIO). 영역 8 개로 플래시·RAM·스택·주변장치를 **전부** 덮어야 하는 대신 "모르는 주소" 를 모두 잡는다. PPB(0xE000_0000–0xE00F_FFFF)는 항상 기본 맵이라 CPUID 읽기는 통과 — 그래서 fault 핸들러가 MPU 를 끌 수 있다 (`ptw.c` 2622–2632 행).

릴리스 체크리스트로 남길 표:

| 항목 | 이 레슨의 값 | 근거 |
|---|---|---|
| NULL 영역 | 0x0, 256 B, AP=000, XN, R2 | 벡터 192 B ≤ 256, `.text` ≥ 0x100 (ld ASSERT) |
| 태스크 가드 | 32 B → **64 B 권장** | 최대 프레임 24 B + 예외 32 B (FP 면 104 B) |
| 프레임 > 가드인 함수 | 금지 또는 가드 확대 | T9: 160 B 프레임 → 256 B 가드 필요 |
| RAM 실행 | 기본 XN, 필요한 32 B 만 R4 (RO+X) | W^X |
| HFNMIENA / PRIVDEFENA | 0 / 1 | Lockup 회피 / 배경 허용 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

## 7. 자주 하는 실수

1. **SIZE 필드에 log2 를 그대로** — `SIZE = log2(size)` 로 쓰면 모든 영역이 두 배가 된다. NULL 영역이 256 B → 512 B 가 되어 0x100–0x1FF 의 코드까지 덮는다. 실측 돌연변이 M3: 첫 fault 의 핸들러 `Fault_Entry` (0x19c) 인출이 막혀 `unexpected fault: exc=3 CFSR=0x00000001 pc=0x0000019c` (IACCVIOL → HardFault, HardFault 안에서는 HFNMIENA=0 이라 MPU 가 꺼져 핸들러가 돈다) 로 멈춘다. 고친 코드: `((log2 - 1u) << 1)`.
2. **겹치는 영역의 번호를 거꾸로** — NULL 영역을 0 번, FLASH 를 2 번으로 두면 FLASH(RO) 가 이겨 NULL 읽기가 통과한다 (T6 실측 `result=0x20010000`; 돌연변이 M2: T2 의 6 개 검사 포함 79/95). 좁고 엄격한 영역일수록 **큰 번호**.
3. **재적재 전에 영역을 지우지 않음** — 예전 영역이 남아 새 정책을 뒤집는다 (M5: 서브리전 영역이 남아 T11 FAIL). `for i in 0..7: RNR=i; RASR=0`.
4. **오정렬 base** — 실기 UNPREDICTABLE, QEMU 는 조용히 무시 (P5). 런타임 주소(`&g_ta.guard`)로 영역을 만들면 링커가 정렬을 바꿀 때 보호가 사라진다 → `aligned(512)` 속성 + 인코더의 `MPU_ERR_ALIGN` 검사를 **런타임에도** 확인.
5. **IACCVIOL/MSTKERR 에서 MMFAR 를 믿음** — MMARVALID 가 0 이면 MMFAR 는 낡은 값 (T8 의 raw 0x200004f8 = T7 의 주소, T10 의 raw 0x20000480 = T9 의 주소). 고친 코드: `mmfar = (cfsr & MMARVALID) ? MMFAR : NOADDR`.
6. **MSTKERR 인데 쌓인 프레임을 읽음** — 프레임이 가드 안이거나 일부만 써졌다. 핸들러가 `frame[6]` 을 읽으면 MPU 가 켜진 상태에선 fault-in-fault, 꺼도 쓰레기 PC. 고친 코드: `frame_ok = !(cfsr & MSTKERR)`, 태스크 되감기.
7. **명령 길이 2 고정** — `str.w` 의 두 번째 halfword(0x1000)가 16 비트 명령으로 실행된다. 이번엔 우연히 무해했지만 (`asrs` 류) 다른 인코딩이면 아무 일이나 일어난다. 돌연변이 M1 은 `resume == pc_str + 4` 와 인코딩 표 검사로 잡혔다.
8. **`*(volatile uint32_t *)0` 로 NULL 영역 시험** — GCC 가 `udf #255` 를 심는다 (nullub 실측). asm 또는 `-fno-delete-null-pointer-checks`.
9. **ENABLE 없이 HFNMIENA** — QEMU 경고 `HFNMIENA and !ENABLE is UNPREDICTABLE` (P10). CTRL 은 한 번에 원하는 조합으로 쓴다.

## 8. 면접 Q&A

<details><summary>Q1. 벡터 표가 주소 0 에 있는데 0x0–0xFF 를 no-access 로 막으면 인터럽트는 어떻게 들어가나?</summary>

예외 진입 시의 벡터 읽기는 MPU 를 거치지 않고 항상 기본 메모리 맵으로 한다 (ARMv7-M ARM, 확인 권장). QEMU 도 같은 규칙을 구현한다 (`m_helper.c` 의 "vector loads are special" 주석, MPU 조회 없이 직접 읽기). 실측: NULL 영역이 켜진 채 IRQ7 (벡터 0x5C) 이 정상 실행되고, 14 번의 fault 모두 0x10/0x0C 벡터로 들어왔다. 막히는 것은 **소프트웨어** 읽기다 (T5: `ldr [4]` → MMFAR=4). 그래서 (1) 벡터 표를 읽는 코드(부트로더 검사, 핸들러 테이블 조회)는 MPU 전에 실행하거나, (2) VTOR 을 별칭/RAM 사본으로 옮긴다 (BM-A01, 확인 권장), (3) 코드는 영역 밖으로 (`.text` ≥ 0x100). 꼬리질문 "STM32 는?" → 0x0 은 플래시 별칭이고 VTOR=0x08000000 이 보통이라 충돌이 없다.
</details>

<details><summary>Q2. 스택 가드 32 B 면 충분한가? 이 설계가 틀리는 경우는?</summary>

두 가지로 틀린다. (a) 오버플로를 잡는 순간 SP 는 이미 가드 **안**에 있고, 예외 진입이 SP 아래로 32 B (FP 확장이면 104 B) 를 더 쌓는다 → 가드 아래가 덮인다. 실측 T7: SP=0x4F8 (가드 안 24 B), 쌓기가 0x4D8 부터 시작해 victim 2 워드 손상, `CFSR=0x92`. (b) 한 번에 SP 를 가드보다 많이 내리는 함수(큰 지역 배열, `alloca`)는 가드를 통째로 건너뛴다 — T9: 160 B 프레임이 32 B 가드를 넘어 0x480 에 쓰고 fault 0. 규칙: 가드 ≥ 최대 SP 감소량 + 예외 프레임, 2 의 거듭제곱 (연습 4 의 `guard_min`). 큰 프레임은 `-fstack-usage` 로 찾는다 (BM-A07, RTOS-M08). 보완: v8-M 의 PSPLIM/MSPLIM (명령 단위로 SP 를 검사, 확인 권장), GCC 의 스택 프로빙.
</details>

<details><summary>Q3. 스택 오버플로 fault 에서 CFSR 에 DACCVIOL 과 MSTKERR 가 같이 서는 이유는? 핸들러는 무엇을 조심하나?</summary>

DACCVIOL 은 가드에 쓰는 명령(`str r0,[sp]`) 이 낸 것, MSTKERR 는 그 fault 를 받으려고 예외 프레임을 쌓다가 다시 가드에 닿은 것이다. 쌓기가 실패했으므로 핸들러가 받은 "프레임" 은 존재하지 않거나 일부만 있다 — PC/LR 를 믿으면 안 되고, 그 스택으로 돌아갈 수도 없다. 그래서 (1) 핸들러는 다른 스택(MSP)에서 돈다, (2) 기록은 레지스터(CFSR/MMFAR)로만, (3) 복구는 태스크 종료/재시작(이 레슨의 되감기) 또는 리셋. MSTKERR 단독(T8: `CFSR=0x10`) 은 명령이 아니라 **예외 진입** 자체가 넘친 것 — 인터럽트가 깊은 곳에서 들어온 경우다.
</details>

<details><summary>Q4. RAM 에서 코드를 실행해야 할 때(플래시 쓰기 루틴, 부트로더 갱신)는 XN 을 어떻게 다루나?</summary>

SRAM 전체는 XN 으로 두고, 실행할 32 B~수 KB 만 **더 높은 번호** 의 영역으로 "RO + 실행" 을 준다 (T10: R4 AP=110, XN=0 → 42). 쓰기와 실행을 동시에 허용하지 않는다 (W^X): 복사할 땐 RW+XN, 복사 후 RO+X 로 바꾸고 DSB/ISB (I-캐시가 있는 M7 이면 캐시 무효화도 — 확인 권장). XN 위반은 `IACCVIOL` 로 MMFAR 가 무효라서 위반 주소는 쌓인 PC 에서 얻는다 (`0x20000a00`).
</details>

<details><summary>Q5. PRIVDEFENA 를 0 으로 하면 무엇을 얻고 무엇을 잃나?</summary>

얻는 것: 정의하지 않은 모든 주소(빈 주소, 쓰지 않는 주변장치, 미러)가 특권 코드에서도 fault 가 된다 (T12: FPGAIO 읽기 → `MMFAR=0x40028000`). 잃는 것: 영역 8 개로 플래시 · RAM · 스택 · 쓰는 주변장치 · NULL · 가드를 **전부** 표현해야 한다 — 영역 예산이 빠듯해 SRD 와 2 의 거듭제곱 정렬 퍼즐이 된다 (연습 3 `mpu_cover`). 놓치면 핸들러 코드나 MSP 자체가 영역 밖이라 fault-in-fault. PPB 는 항상 통과하므로 MPU 를 끄는 탈출구는 남는다. 보통의 선택: PRIVDEFENA=1 + 비특권 태스크 (비특권은 배경 영역을 못 쓴다 — 연습 2 의 모델).
</details>

<details><summary>Q6. HFNMIENA 는 왜 기본 0 인가? 1 로 하면 무엇이 위험한가?</summary>

0 이면 HardFault/NMI (그리고 FAULTMASK=1) 동안 MPU 가 꺼진 것처럼 동작한다 — T13 에서 HardFault 핸들러가 NULL 영역을 읽어 `0x20010000` 을 얻었다. 1 이면 HardFault 핸들러가 MPU 규칙을 어기는 순간 더 갈 곳이 없어 Lockup: QEMU 실측 `Lockup: can't escalate 3 to HardFault (current priority -1)`, exit 134. 1 을 쓰는 경우는 "HardFault 핸들러도 신뢰하지 않는" 보안 설계뿐이고, 그땐 핸들러가 접근하는 모든 것을 영역으로 덮고 워치독을 Lockup 탈출구로 둔다.
</details>

<details><summary>Q7. fault 후 리셋하지 않고 계속 돌리는 것이 언제 정당한가? (BM-M13 과 비교)</summary>

복구가 정당하려면 (1) 손상 범위를 안다 — MPU 가 접근 **전에** 막았으니 메모리는 그대로다 (단, Q2 의 쌓기 손상 예외), (2) 실패한 단위를 격리할 수 있다 — 태스크/요청 하나를 버리고 나머지는 산다 (RTOS 에서 태스크 kill), (3) 같은 fault 가 반복되면 결국 리셋한다 (이 레슨의 fault storm 가드 64 회). 안전 출력이 있는 시스템(BM-M13 의 모터) 이라면 리셋이 기본이고, 이 레슨의 SKIP 은 **시험 장치**로만 정당하다 — 실제 코드에서 "명령 하나 건너뛰기" 는 결과 레지스터가 쓰레기인 채 진행하는 것이다.
</details>

<details><summary>Q8. ARMv8-M (Cortex-M33/M55) MPU 라면 이 설계를 어떻게 바꾸겠나?</summary>

PMSAv8 은 RBAR/RLAR 로 **base 와 limit** 을 32 B 단위로 정한다 — 2 의 거듭제곱/정렬 규칙과 서브리전이 없어지고, 대신 영역이 겹치면 fault (우선순위 규칙이 없다), 메모리 속성은 MAIR 인덱스로 (확인 권장). 그래서 (1) 인코더는 정렬 퍼즐 대신 "겹침 금지" 검사로, (2) 스택 가드는 MPU 영역 대신 MSPLIM/PSPLIM 로 (UsageFault STKOF, 쓰기 전에 잡으므로 Q2 의 손상이 없다 — QEMU `v7m_push_stack` 의 limit 경로도 "pushes below the limit must not be performed" 라고 적는다), (3) TrustZone 이 있으면 Secure/Non-secure MPU 가 따로다. 이 레슨의 정책 (NULL, 가드, W^X, 복구 단위) 은 그대로 옮겨진다.
</details>

<details><summary>Q9. 런타임에 영역 하나(태스크 전환 시 스택 가드)를 바꾸는 코드는 어떻게 짜나?</summary>

`mpu_region_write()`: RBAR(VALID+번호) → RASR=0 (먼저 비활성) → RBAR → RASR → DSB/ISB. 비활성화 단계가 없으면 RBAR 만 바뀌고 RASR 은 예전 크기인 순간이 생긴다 — 그 사이 인터럽트가 들어오면 엉뚱한 범위가 보호된다. RTOS 는 가장 낮은 우선순위의 PendSV(컨텍스트 전환) 안에서 바꾸고, 예외 복귀가 문맥 동기화 역할을 하더라도 DSB 는 넣는다 (ARM 권장, 확인 권장). 비용은 PPB 레지스터 쓰기 4 번 + 배리어 — 태스크마다 영역 하나만 바꾸도록 배치를 설계하는 이유다 (FreeRTOS-MPU 류 설계, 확인 권장).
</details>

## 9. 추가 연습

### 연습 1 — `mpu_decode()` 와 인코더 전수 검사

`mpu_decode(rbar, rasr, &info)` 를 쓰고, 독립 참조 규칙 `ref_rule()` (반복문 대신 정의 그대로) 과 인코더를 비교하라: 크기 2^0..2^33 · 비-거듭제곱 · 정렬/비정렬 base, 그리고 영역 0..9 × AP 0..15 × attr 0..65 × XN 0..2 × SRD 4 가지 × 크기 4 가지. 성공하면 decode 왕복, 실패하면 `*out` 불변.

<details><summary>답</summary>

실측: `accepted 43064, rejected 465796, mismatches vs reference 0, round-trip errors 0, legal attr codes 48/64`. 합법 attr 48 = 64 − 16 (예약: TEX=001 CB=01 ×2(S), TEX=010 CB≠00 ×6, TEX=011 ×8).
</details>

### 연습 2 — `mpu_check()`: 한 접근의 판정 모델

`mpu_check(regions, ctrl, addr, acc, priv, negpri)` → `{fault, region}`. 우회(MPU 끔 / PPB / 벡터 / HardFault+HFNMIENA=0) → R7..R0 첫 일치(꺼진 서브리전은 불일치) → 배경(특권+PRIVDEFENA) → fault. AP 표와 기본 맵 XN(0x4000_0000–0x5FFF_FFFF, 0xA000_0000 이상) 포함.

<details><summary>답</summary>

이름 붙은 16 경우 + 성질 "HardFault(HFNMIENA=0) 의 판정 == MPU 끈 판정" 을 무작위 20000 접근에서 검사: `violations 0`. 비특권 접근은 배경 영역이 없어 0x40028000 이 fault — 태스크를 비특권으로 돌리면 주변장치 영역을 따로 줘야 한다.
</details>

### 연습 3 — `mpu_cover()`: 범위를 영역 하나 + SRD 로

`[base, base+len)` 를 정확히 덮는 가장 작은 (크기, base, SRD) 를 찾아라. 브루트포스(모든 크기 × SRD 256 가지) 와 비교: base, len 이 32 B 배수인 모든 쌍 (base < 4 KB, 끝 ≤ 8 KB).

<details><summary>답</summary>

크기 S 를 32 B 부터 키우며 `B = base & ~(S−1)`, 범위가 `[B, B+S)` 안이고 (정확히 S 이거나) S ≥ 256 이며 경계가 S/8 의 배수면 `SRD = ~(켜진 조각) & 0xFF`. 예: 0x20000000 부터 40 KB → 64 KB 영역, `SRD=0xE0` (위 3 조각 끔). 96 B @+32 → 256 B, `SRD=0xF1`. 4 KB 경계를 넘는 512 B 는 불가. 실측: `832 coverable (base,len) pairs, wrong 0, not-minimal 0`.
</details>

### 연습 4 — 가드 크기 규칙 `guard_sim()` / `guard_min()`

SP 가 `drops[i]` 씩 내려가며 새 SP 에 쓰고, 가드에 처음 닿으면 예외 프레임을 SP−frame 부터 위로 쓰다 가드에서 멈추는 모델. T7 (꼬리 호출 0 B + 24 B 프레임들, 32 B / 64 B 가드) 과 T9 (160 B 한 번) 을 재현하고, `guard_min(max_drop, exc_frame)` 이상이면 손상 0 임을 무작위로 확인하라.

<details><summary>답</summary>

모델이 실측과 같은 숫자를 낸다: 32 B 가드 → 가드 아래 2 워드, 64 B → 0, 160 B 프레임 → 잡지 못하고 1 워드. `guard_min(24,32)=64`, `(160,32)=256`, `(24,104)=128`. 무작위 5000 열 (프레임 8..256 B, 예외 프레임 32/104 B): `bad 0`. 증명 스케치: 직전 SP ≥ 가드 위 → 새 SP ≥ 가드 위 − max_drop ≥ 가드 아래 + 예외 프레임 → 쌓기 시작 주소도 가드 안.
</details>

### 연습 5 — QEMU 실측 표 대조

solution 의 T2~T13 과 lockup 모드 (21 줄), demo_probe 의 P5~P8 을 `mpu_check()` 예측과 비교하라. 모델이 QEMU 와 다른 줄이 있으면 그것이 "사양 vs 에뮬레이터" 의 차이인지 모델 버그인지 판정한다.

<details><summary>답</summary>

`QEMU observations 21, model agrees 21`. P5 (오정렬 → 무시), P6 (128 B 의 SRD 무시), P7 (AP=100 → 권한 없음), P8 (16 B 영역) 은 모델을 **QEMU 동작에 맞춰 정의**했다 — 실기에선 모두 UNPREDICTABLE 이므로 인코더가 애초에 거부하는 것이 정답이다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-A08_pr && /tmp/BM-A08_pr    # 35/35 passed
# VM 에서: limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"
```

실행 결과 (실제 출력):

```text
  ex1: accepted 43064, rejected 465796, mismatches vs reference 0, round-trip errors 0, legal attr codes 48/64
  ex2: 20000 random accesses, HardFault(HFNMIENA=0) == MPU-off: violations 0
  ex3: 832 coverable (base,len) pairs, wrong 0, not-minimal 0
  ex4: T7 model below-guard words 32B=2, 64B=0; 5000 random frame sequences with guard_min: bad 0
  ex5: QEMU observations 21, model agrees 21
35/35 passed
```

실행 스크립트와 QEMU 확인 펌웨어 전체:

<!-- code: run_qemu.sh -->

<!-- code: demo_probe.c -->

## 10. 핵심 정리

- PMSAv7 영역: 크기 2^n (32 B–4 GB), base 는 크기의 배수, `SIZE = log2 − 1`, SRD 는 256 B 이상에서 8 조각. 규칙 위반은 실기 UNPREDICTABLE, QEMU 는 **조용히 무시** (오정렬) → 인코더가 거부해야 한다.
- 겹치면 **번호가 큰 영역이 이긴다**: FLASH(0) < SRAM(1) < NULL(2) < GUARD(3) < RAMX(4) < SUB(5). 꺼진 서브리전은 아래 영역으로 내려간다.
- 적재: `CTRL=0 → 0..7 RASR=0 → RBAR(VALID)/RASR → CTRL → DSB; ISB`. 런타임 변경은 비활성화 후.
- NULL: `CFSR=0x82`, MMFAR = 주소. 벡터 읽기는 기본 맵이라 0x0 의 벡터 표와 공존 (QEMU `m_helper.c` 확인), 단 `.text` 는 영역 밖, 소프트웨어 벡터 읽기는 막힌다.
- 스택 가드: 오버플로 = `DACCVIOL+MMARVALID+MSTKERR (0x92)`, 프레임 없음 → 다른 스택(MSP)에서 태스크 되감기. 가드 ≥ 최대 SP 감소 + 32 B (FP 104 B).
- XN: `IACCVIOL (0x01)`, MMFAR 무효 → 주소는 쌓인 PC. 실행은 필요한 범위만 높은 번호 영역으로, W^X.
- PRIVDEFENA=1 은 배경 허용(특권만), HFNMIENA=0 이 기본 (1 + HardFault 위반 = Lockup, exit 134).
- MMARVALID 없는 MMFAR, MSTKERR 의 프레임, `*(uint32_t*)0` (GCC `udf`) 는 믿지 않는다.

## 참고자료

- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403E) — B3.5 PMSAv7 (MPU_TYPE/CTRL/RNR/RBAR/RASR, AP 표, Table B3-13 TEX/C/B, 서브리전, 벡터 읽기와 기본 메모리 맵, HFNMIENA/PRIVDEFENA), B1.5 예외 진입·MSTKERR (절 번호 확인 권장).
- Arm, *Cortex-M3 Devices Generic User Guide* (DUI 0552A) — 4.5 Memory Protection Unit; *ARMv8-M Architecture Reference Manual* — PMSAv8 RBAR/RLAR, MSPLIM/PSPLIM (Q8, 확인 권장).
- QEMU v10.2.1 소스 (실행한 QEMU 와 같은 버전):
  - https://raw.githubusercontent.com/qemu/qemu/v10.2.1/target/arm/tcg/m_helper.c — 213–300 `v7m_stack_write` (MSTKERR, 250–251), 678–684 벡터 읽기는 기본 맵, 2339–2347 IACCVIOL / DACCVIOL+MMFAR.
  - https://raw.githubusercontent.com/qemu/qemu/v10.2.1/target/arm/ptw.c — 255–270 HFNMIENA, 2525–2563 기본 맵 XN, 2605–2796 `get_phys_addr_pmsav7` (2622 PPB/비활성, 2634 높은 번호부터, 2653 오정렬 무시, 2681 256 B 미만 SRD 없음, 2719 배경 fault, 2737–2785 AP).
  - https://raw.githubusercontent.com/qemu/qemu/v10.2.1/hw/intc/armv7m_nvic.c — 1355–1415 MPU 레지스터 읽기 (TYPE = DREGION<<8), 1829–1935 쓰기 (RBAR.VALID → RNR, RASR 마스크, HFNMIENA 경고).
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* (3rd ed.) — 11 장 MPU.
- GCC 문서: `-fdelete-null-pointer-checks` (주소 0 이 유효한 임베디드 타깃에서의 함정).
- 관련 레슨: BM-M13 (fault 정책, MPU 한 번, 리셋 기록), BM-B08 (startup, Lockup 관찰), BM-A07 (스택 페인팅·HWM, `-fstack-usage`), BM-A03 (부트로더 → 앱 점프, 벡터 표 검사), BM-A01 (RAM 벡터 표 / VTOR), RTOS-M08 (태스크 스택 측정), RTOS-A01 (mps2 위 mini-RTOS). 다음 단계: ARM-M12 (CFSR/HFSR 전체 디코드 + 언와인딩), ARM-M10 (PRIMASK/BASEPRI), SEC-* (보안 부팅·권한 분리).
