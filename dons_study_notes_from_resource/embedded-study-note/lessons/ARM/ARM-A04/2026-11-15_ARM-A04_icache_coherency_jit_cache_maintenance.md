---
id: ARM-A04
time: 110분
updated: 2026-11-15
---
# ARM-A04 I-cache 일관성: JIT 코드 생성과 캐시 유지보수

## 학습 목표

- 데이터로 쓴 바이트를 명령으로 실행하기 전에 필요한 **DC CVAU → DSB ISH → IC IVAU → DSB ISH → ISB** 순서를 쓰고, 명령 하나하나가 무엇을 막는지 설명한다.
- AArch64 명령(ADD imm, MOVZ/MOVK, RET, B/BL, CBZ)을 C 로 인코딩·디코딩하고, 진짜 어셈블러(clang) 워드와 비트 단위로 맞춘다.
- macOS 의 JIT 규칙 (`MAP_JIT`, 스레드별 `pthread_jit_write_protect_np`, `sys_icache_invalidate`)을 쓰고, W^X 를 어기면 실제로 어떤 시그널이 오는지 확인한다.
- 이 M6 에서 **유지보수를 빼면 정말로 옛 코드가 실행된다**는 것을 실측하고 (2000번 중 1999번), `CTR_EL0` 의 IDC/DIC·줄 크기로 결과를 설명한다.
- 같은 문제를 펌웨어 쪽으로 옮긴다: RAM 함수, 부트로더의 코드 복사, Cortex-M7 `SCB_InvalidateICache`, 핫패치 B 한 워드.

## 1. 문제

면접에서 "JIT 코드 쓰고 바로 부르면 왜 가끔 옛날 코드가 돌아?" 는 캐시를 진짜로 이해했는지 가르는 질문이다. 펌웨어도 똑같다: 부트로더가 앱을 RAM 에 복사하고 점프할 때, 플래시를 다시 쓴 뒤, 디버거가 브레이크포인트를 심을 때 — **코드를 데이터로 쓰는** 모든 순간에 같은 문제가 생긴다.

ARM 코어의 L1 은 명령 캐시(I-cache)와 데이터 캐시(D-cache)가 나뉜 하버드 구조다. `str` 은 D 쪽으로 가고, 명령 가져오기(fetch)는 I 쪽에서 한다. 하드웨어가 둘을 자동으로 맞춰 주는지는 칩마다 다르고, `CTR_EL0` 의 두 비트(IDC, DIC)가 그 약속을 말해 준다. 약속이 없으면 소프트웨어가 직접 맞춘다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">코드를 데이터로 쓴 뒤 실행하기까지 (한 코어 기준)</text>
  <rect x="20" y="40" width="150" height="60" class="f-acc" stroke="currentColor"/>
  <text x="95" y="65" text-anchor="middle" fill="currentColor">코어 파이프라인</text>
  <text x="95" y="85" text-anchor="middle" fill="currentColor" font-size="12">(이미 가져온 명령)</text>
  <rect x="250" y="40" width="130" height="50" class="f-mut" stroke="currentColor"/>
  <text x="315" y="70" text-anchor="middle" fill="currentColor">L1 D-cache</text>
  <rect x="250" y="130" width="130" height="50" class="f-warn" stroke="currentColor"/>
  <text x="315" y="152" text-anchor="middle" fill="currentColor">L1 I-cache</text>
  <text x="315" y="170" text-anchor="middle" fill="currentColor" font-size="12">옛 명령이 남아 있다</text>
  <rect x="480" y="80" width="230" height="60" class="f-ok" stroke="currentColor"/>
  <text x="595" y="105" text-anchor="middle" fill="currentColor">PoU (Point of Unification)</text>
  <text x="595" y="125" text-anchor="middle" fill="currentColor" font-size="12">I·D 가 같은 값을 보는 지점 (보통 L2)</text>
  <g stroke="currentColor" fill="none">
    <line x1="170" y1="60" x2="250" y2="60"/><polygon points="250,60 242,56 242,64" fill="currentColor"/>
    <line x1="380" y1="65" x2="480" y2="95"/><polygon points="480,95 471,89 469,97" fill="currentColor"/>
    <line x1="480" y1="125" x2="380" y2="150"/><polygon points="380,150 389,144 391,152" fill="currentColor"/>
    <line x1="250" y1="160" x2="170" y2="90"/><polygon points="170,90 172,99 179,94" fill="currentColor"/>
  </g>
  <text x="185" y="52" fill="currentColor" font-size="12">str (새 명령)</text>
  <text x="392" y="68" fill="currentColor" font-size="12">① DC CVAU</text>
  <text x="400" y="160" fill="currentColor" font-size="12">③ IC IVAU 후 재적재</text>
  <text x="120" y="140" fill="currentColor" font-size="12">fetch</text>
  <rect x="20" y="200" width="700" height="88" class="f-mut" stroke="currentColor"/>
  <text x="30" y="220" fill="currentColor" font-family="monospace">① dc cvau  ② dsb ish  ③ ic ivau  ④ dsb ish  ⑤ isb</text>
  <text x="30" y="240" fill="currentColor">① 새 바이트를 PoU 까지 밀기 (IDC=1 이면 생략) ③ I-cache 의 옛 줄 버리기 (DIC=1 이면 생략)</text>
  <text x="30" y="258" fill="currentColor">② ④ 앞 단계가 끝난 뒤에 다음 단계  ⑤ 파이프라인이 이미 가져온 옛 명령 버리기 (항상 필요)</text>
  <text x="30" y="278" fill="currentColor">이 M6 실측: 아무것도 안 하면 2000번 중 1999번 옛 코드 실행 (Part B)</text>
</svg>
```

| 층 | 파일 | 하는 일 |
|---|---|---|
| 인코더/디코더 | `starter.c` → `solution.c` | `enc_add_imm`, `enc_movz/movk`, `enc_ret`, `enc_b/bl`, `enc_cbz`, `insn_decode` |
| 골든 | `shared_ref.S` | clang 이 어셈블한 33개 워드 (`ref_insns[]`) — 인코더를 진짜 어셈블러와 비교 |
| 캐시 유지보수 | `starter.S` → `solution.S` | `cache_sync_range` (DC/IC 횟수를 돌려줌), `read_ctr_el0` |
| JIT | `solution.c` | `jit_alloc` (주어짐), `jit_patch` (TODO), `jit_verify` (주어진 검사기), `compile_affine` (TODO) |
| 실측 | `demo_stale.c`, `demo_wx.c`, `demo_bench.c`, `demo_ctr.c`, `demo_libsys.lldb` | 옛 코드 실행, W^X, 비용, CTR 값, libsystem 디스어셈블 — 결과는 `*_m6.txt`, `*_vm.txt` |

### Part A — 인코더, JIT, 표준 유지보수 시퀀스

명령 7종의 인코더와 디코더를 쓰고 `shared_ref.S` 의 진짜 워드와 맞춘다. imm12 × sh, imm16 × hw 는 전수(exhaustive), 분기 오프셋은 무작위 3만 개로 왕복(encode → decode) 검사한다. 그 인코더로 `f(x) = x + imm` 을 만들어 **같은 자리를 3000번 다시 패치**하며 부르고, `sys_icache_invalidate` 와 직접 짠 asm `cache_sync_range` 두 가지로 맞춘다. asm 은 실행한 DC/IC 개수를 돌려주므로, 줄 경계 처리가 맞는지를 C 모델과 전수 비교한다.

### Part B — 빼먹으면 어떻게 되나

유지보수를 빼거나(없음 / ISB 만 / ISB 빼고 / 줄 간격 128), 다른 스레드에서 실행하는 경우를 `demo_stale.c` 로 **실측**해 기록한다 (CHECK 하지 않는다 — 마이크로아키텍처 현상이라). W^X 위반(보호 중 쓰기, 쓰기 창에서 실행)은 fork 한 자식에서 일으켜 시그널을 CHECK 한다. EL0 에서 `CTR_EL0` 를 읽으면 macOS 는 SIGILL 이라 줄 크기를 직접 모른다 — `ctr_decode` 를 QEMU/리눅스에서 읽은 실제 값으로 검사한다.

### Part C — 표현식 JIT 와 펌웨어

필터 계수 같은 상수를 명령에 박아 넣는 작은 컴파일러: `y = (a·x + b) >> s` 를 MOVZ/MOVK·MUL·ADD·ASR 워드로 만들고 인터프리터와 1만 6천 번 비교한다. 예: ADC 보정 `(gain_Q16 · raw + offset) >> 16`. 그리고 같은 순서가 Cortex-M7 (`SCB_CleanDCache_by_Addr` / `SCB_InvalidateICache`), 부트로더, 핫패치에서 어떻게 나타나는지 연결한다.

## 2. Problem (English)

> **Generate AArch64 code at run time and make it safely executable.**
>
> 1. Encoders (64-bit forms, return `false` and leave `*out` untouched on any out-of-range field):
>    `bool enc_add_imm(uint32_t *out, unsigned rd, unsigned rn, uint32_t imm12, unsigned sh)`, `enc_movz/enc_movk(out, rd, imm16, hw)`, `enc_ret(out, rn)`, `enc_b/enc_bl(out, int64_t off)` (±128 MB), `enc_cbz(out, rt, int64_t off)` (±1 MB). `off` is in bytes relative to the branch, multiple of 4.
> 2. `bool insn_decode(uint32_t w, struct insn *d)` recognises exactly those seven and rejects neighbours (ADDG, MOVN, CBNZ, B.cond, SUB, 32-bit ADD, RETAA, BR).
> 3. `bool ctr_decode(uint64_t ctr, struct ctr_info *ci)` → line sizes in bytes, L1Ip, ERG/CWG (0 → 2 KB), IDC, DIC; reject a value whose RES1 bit 31 is clear.
> 4. In assembly: `uint64_t cache_sync_range(void *p, size_t len, size_t dline, size_t iline, unsigned flags)` — DC CVAU per D-line, DSB ISH, IC IVAU per I-line, DSB ISH, ISB; flags bit0 skips the DC loop (IDC), bit1 skips the IC loop (DIC); returns `(dc_ops << 32) | ic_ops`; `len == 0` does nothing.
> 5. `bool jit_patch(struct jit *j, size_t at, const uint32_t *w, size_t n, enum maint m)` on a `MAP_JIT` region: open the per-thread write window, store each word with one 32-bit store, close it, then maintain (`MAINT_SYS` = `sys_icache_invalidate`, `MAINT_MANUAL` = your asm).
> 6. `size_t compile_affine(uint32_t *buf, size_t cap, uint64_t a, uint64_t b, unsigned s)` → code for `y = (a*x + b) >> s` (wrapping 64-bit, arithmetic shift, `s ≤ 63`), skipping the multiply when `a == 1`, the add when `b == 0`, the shift when `s == 0`.
>
> Examples: `add x3, x4, #1, lsl #12` = `0x91400483`; `b . - 8` = `0x17fffffe`; `cbz x0, . - 0x100000` = `0xb4800000`; `cache_sync_range(p, 8, 64, 64, 0)` with `p % 64 == 60` returns `(2 << 32) | 2`; `compile_affine(buf, 16, 1, 0, 0) == 1` (just `ret`).
>
> Follow-ups: Why is each barrier there? What changes if another core is executing the code? What do IDC/DIC let you skip — and can you ever skip the ISB? Where do you get the line size on macOS? Which single-word patches are safe while other cores run? How does this look on a Cortex-M7?

## 3. 시작 코드

C 하네스 (TODO 11곳: 인코더 6, 디코더, `ctr_decode`, `jit_patch`, `emit_imm64`, `compile_affine`). 플랫폼 층, 검사기 `jit_verify`, Part C 용 `enc_mul/enc_add_reg/enc_asr`, 참조 인터프리터는 주어진다.

<!-- code: starter.c -->

asm (TODO: DC 루프, DSB, IC 루프, DSB, ISB — 스텁은 아무 명령도 안 하고 0 을 돌려준다):

<!-- code: starter.S -->

골든 워드 (모든 변형에 함께 링크):

<!-- code: shared_ref.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c starter.S shared_ref.S -o /tmp/ARM-A04_starter && /tmp/ARM-A04_starter
```

처음 실행 (실제 출력 일부, exit 1):

```text
INFO  cache geometry: dline=64 iline=64 flags=0 (macOS fallback (CTR_EL0 trapped))
  enc add x0, x0, #1               -> 0x00000000 (want 0x91000400)
FAIL  starter.c:381  enc_bad == 0
FAIL  starter.c:436  cnt_bad == 0
INFO  sys_icache_invalidate: 0 patch+call ok
FAIL  starter.c:455  bad == 0
INFO  write while RX -> signal 10 (Bus error: 10), exec while RW -> signal 10 (Bus error: 10)
INFO  mrs ctr_el0 at EL0 -> signal 4
FAIL  starter.c:491  ctr_decode(UINT64_C(0x84448004), &ci) && ci.iline == 64 && ...
FAIL  starter.c:509  compile_affine(code, 16, 1, 0, 0) == 1 && code[0] == RET_WORD
19/46 passed
```

스텁 상태에서도 **크래시하지 않는다**: JIT 영역은 처음에 전부 `ret` 으로 채워지고 (`f(x) = x`), 생성 코드는 실행 전에 `jit_verify` 가 검사한다 (허용 명령·레지스터 x0–x17·앞으로 가는 분기만). W^X 시그널과 범위 밖 패치 거절은 주어진 코드라 처음부터 PASS.

## 4. 힌트

<details>
<summary>힌트 1 — 인코딩은 "고정 비트 | 필드 << 위치"</summary>

ADD imm: `0x91000000 | sh<<22 | imm12<<10 | Rn<<5 | Rd`. MOVZ `0xD2800000`, MOVK `0xF2800000` (+ `hw<<21 | imm16<<5 | Rd`). RET `0xD65F0000 | Rn<<5`. B `0x14000000`, BL `0x94000000` (+ imm26). CBZ(64) `0xB4000000 | imm19<<5 | Rt`. 먼저 범위를 검사하고 나서 `*out` 에 쓴다.

</details>

<details>
<summary>힌트 2 — 음수 오프셋</summary>

`off >> 2` 를 부호 있는 정수에 하면 구현정의(C11 6.5.7p5). `((uint64_t)off >> 2) & 0x3FFFFFF` 처럼 부호 없는 값으로 바꾼 뒤 시프트하고 마스크한다. 디코드는 거꾸로: 하위 26/19비트를 부호 확장 → ×4. 범위는 [−2²⁷, 2²⁷−4] / [−2²⁰, 2²⁰−4].

</details>

<details>
<summary>힌트 3 — 디코더 마스크는 "이웃" 과 구분되게</summary>

ADD imm 은 bit 23 까지 봐야 ADDG(FEAT_MTE, bit 23 = 1)와 갈린다 → `(w & 0xFF800000) == 0x91000000`. MOVZ/MOVK/MOVN 은 opc(bit 30:29)만 다르다. B/BL 은 bit 31 만 다르다 → `(w & 0x7C000000) == 0x14000000`. CBZ/CBNZ 는 bit 24.

</details>

<details>
<summary>힌트 4 — cache_sync_range 의 줄 경계</summary>

시작 주소를 줄 크기로 **내림**(`bic x9, x0, (line-1)`)하고 `x9 < end` 인 동안 `line` 씩 전진. 내리지 않으면 `[60, 68)` 같은 범위에서 두 번째 줄을 놓친다. DC 루프 뒤 `dsb ish`, IC 루프 뒤 `dsb ish`, 마지막 `isb` — SKIP_IC 여도 ISB 는 한다.

</details>

<details>
<summary>힌트 5 — jit_patch 순서와 compile_affine</summary>

`jit_writable(true)` → 워드마다 `__atomic_store_n(…, __ATOMIC_RELAXED)` → `jit_writable(false)` → 유지보수. `compile_affine`: a ≠ 1 이면 `emit_imm64(x9, a)` + `enc_mul(0,0,9)`; 0 < b < 4096 이면 `add x0, x0, #b`, 그보다 크면 `emit_imm64(x10, b)` + `enc_add_reg(0,0,10)`; s ≠ 0 이면 `enc_asr(0,0,s)`; 마지막 `ret`. 워드 수가 `cap` 을 넘으면 0.

</details>

## 5. 풀이

### Part A — 인코딩: 비트 배치와 진짜 어셈블러

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">64-bit 인코딩 (bit 31 → 0), 예시 워드는 clang + objdump 실측</text>
  <g stroke="currentColor">
    <rect x="20" y="30" width="200" height="26" class="f-acc"/><rect x="220" y="30" width="22" height="26" class="f-warn"/>
    <rect x="242" y="30" width="270" height="26" class="f-ok"/><rect x="512" y="30" width="110" height="26" class="f-mut"/><rect x="622" y="30" width="110" height="26" class="f-mut"/>
    <rect x="20" y="80" width="200" height="26" class="f-acc"/><rect x="220" y="80" width="44" height="26" class="f-warn"/>
    <rect x="264" y="80" width="358" height="26" class="f-ok"/><rect x="622" y="80" width="110" height="26" class="f-mut"/>
    <rect x="20" y="130" width="130" height="26" class="f-acc"/><rect x="150" y="130" width="582" height="26" class="f-ok"/>
    <rect x="20" y="180" width="180" height="26" class="f-acc"/><rect x="200" y="180" width="422" height="26" class="f-ok"/><rect x="622" y="180" width="110" height="26" class="f-mut"/>
    <rect x="20" y="230" width="492" height="26" class="f-acc"/><rect x="512" y="230" width="110" height="26" class="f-mut"/><rect x="622" y="230" width="110" height="26" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace">
    <text x="120" y="48">1 0 0 100010 0</text><text x="231" y="48">sh</text><text x="377" y="48">imm12 [21:10]</text><text x="567" y="48">Rn</text><text x="677" y="48">Rd</text>
    <text x="120" y="98">1 opc 100101</text><text x="242" y="98">hw</text><text x="443" y="98">imm16 [20:5]</text><text x="677" y="98">Rd</text>
    <text x="85" y="148">op 0 0101</text><text x="441" y="148">imm26 [25:0] = off / 4 (2의 보수)</text>
    <text x="110" y="198">1 011010 op</text><text x="411" y="198">imm19 [23:5] = off / 4</text><text x="677" y="198">Rt</text>
    <text x="266" y="248">1101011 0 0 10 11111 000000</text><text x="567" y="248">Rn</text><text x="677" y="248">00000</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="72">ADD imm: add x3, x4, #1, lsl #12 = 0x91400483 (sh=1, imm12=1, Rn=4, Rd=3)</text>
    <text x="20" y="122">MOVZ opc=10 / MOVK opc=11 / MOVN opc=00: movk x9, #0x1234, lsl #48 = 0xf2e24689</text>
    <text x="20" y="172">B op=0 / BL op=1: b . − 8 = 0x17fffffe (imm26 = 0x3fffffe = −2)</text>
    <text x="20" y="222">CBZ op=0 / CBNZ op=1: cbz x0, . − 0x100000 = 0xb4800000 (imm19 최소 −2^18)</text>
    <text x="20" y="272">RET Xn: ret = 0xd65f03c0 (Rn = 30), ret x16 = 0xd65f0200</text>
    <text x="20" y="292">레지스터 31 의 뜻: ADD imm 의 Rd/Rn → SP,  MOVZ/MOVK 의 Rd → XZR</text>
  </g>
</svg>
```

`shared_ref.S` 를 clang 으로 어셈블하고 `/usr/bin/objdump -d` 로 본 실제 결과 (일부):

```text
       8: 91400483     	add	x3, x4, #0x1, lsl #12   ; =0x1000
      14: 910003e0     	mov	x0, sp
      18: d297dde9     	mov	x9, #0xbeef             ; =48879
      20: f2e24689     	movk	x9, #0x1234, lsl #48
      34: 17fffffe     	b	0x2c <ltmp0+0x2c>
      38: 15ffffff     	b	0x8000034 <_ref_insns_end+0x7ffffb0>
      3c: 16000000     	b	0xfffffffff800003c <_ref_insns_end+0xfffffffff7ffffb8>
      4c: b4800000     	cbz	x0, 0xfffffffffff0004c <_ref_insns_end+0xffffffffffefffc8>
      6c: 92800002     	mov	x2, #-0x1               ; =-1
      70: 91810820     	addg	x0, x1, #0x10, #0x2
      7c: d65f0bff     	retaa
```

손으로 하나 풀어 보자. `b . - 8`: off = −8 → imm26 = −2 → 26비트 2의 보수 `0x3FFFFFE` → `0x14000000 | 0x3FFFFFE = 0x17FFFFFE`. 최소 −128 MB (`0x16000000`) 는 imm26 = `0x2000000` = −2²⁵ 로 부호 비트만 선 값이다. 디스어셈블러가 별칭(alias)으로 보여 주는 것도 주의: `add x0, sp, #0` 은 `mov x0, sp`, `movz x9, #0xbeef` 는 `mov x9, #0xbeef`, `movn x2, #0` 은 `mov x2, #-1` 로 찍힌다 — 워드는 같다.

| 이웃 인코딩 (ref 25–32) | 워드 | ADD/MOVZ/B/CBZ/RET 와 다른 곳 | 디코더가 보는 비트 |
|---|---|---|---|
| `b.ne . + 8` | `0x54000041` | B.cond 는 별도 클래스 (bit 30, bit 4 = 0) | `(w & 0x7C000000) != 0x14000000` |
| `cbnz x1, . - 4` | `0xb5ffffe1` | bit 24 = 1 | `& 0xFF000000 == 0xB4` 아님 |
| `movn x2, #0` | `0x92800002` | opc = 00 | MOVZ/MOVK 마스크 둘 다 불일치 |
| `addg x0, x1, #16, #2` | `0x91810820` | **bit 23 = 1** (FEAT_MTE) | `& 0xFF800000` — `0xFF000000` 이면 잘못 받아들인다 (변이 M5) |
| `sub x0, x0, #1` | `0xd1000400` | bit 30 = 1 | |
| `add w0, w0, #1` | `0x11000400` | sf(bit 31) = 0 | |
| `retaa` | `0xd65f0bff` | bit 11 (A=1), Rm=11111 | RET 마스크 `0xFFFFFC1F` |
| `br x16` | `0xd61f0200` | opc = 0000 | |

어셈블러도 범위를 검사한다 — 같은 경계에서 실제 에러:

```text
bad.S:4:17: error: expected compatible register, symbol or integer in range [0, 4095]
    add x0, x0, #4097
.../bad2-b471c4.s:9:9: error: fixup value out of range
    b . + 0x8000000
.../bad2-b471c4.s:10:15: error: fixup value out of range
    cbz x0, . + 0x100000
.../bad2-b471c4.s:11:9: error: fixup not sufficiently aligned
    b . + 2
```

(`add x0, x0, #4096` 은 에러가 아니다 — clang 이 `#1, lsl #12` 로 바꿔 `0x91400400` 을 낸다. 우리 인코더는 imm12 그대로만 받는 저수준 API 라 4096 을 거절한다.)

### Part A — JIT 수명주기: MAP_JIT 와 스레드별 W^X

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">jit_patch: 같은 페이지, 스레드마다 다른 권한</text>
  <text x="20" y="58" fill="currentColor">쓰는 스레드</text>
  <text x="20" y="128" fill="currentColor">실행 스레드</text>
  <g stroke="currentColor">
    <rect x="120" y="40" width="90" height="30" class="f-ok"/><rect x="210" y="40" width="200" height="30" class="f-warn"/>
    <rect x="410" y="40" width="130" height="30" class="f-ok"/><rect x="540" y="40" width="180" height="30" class="f-acc"/>
    <rect x="120" y="110" width="600" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="165" y="60">RX</text><text x="310" y="54">RW 창: 쓰기만, 실행 불가</text><text x="310" y="66" font-family="monospace">str w (워드마다)</text>
    <text x="475" y="60">RX</text><text x="630" y="54">유지보수</text><text x="630" y="66" font-family="monospace">ic ivau … dsb; isb</text>
    <text x="420" y="130">RX 그대로 (자기 토글 없이 계속 실행 가능)</text>
  </g>
  <g stroke="currentColor"><line x1="210" y1="34" x2="210" y2="146" stroke-dasharray="4 3"/><line x1="410" y1="34" x2="410" y2="146" stroke-dasharray="4 3"/></g>
  <g fill="currentColor" font-size="12">
    <text x="150" y="162">jit_write_protect_np(0)</text><text x="380" y="162">(1)</text>
  </g>
  <rect x="20" y="178" width="700" height="82" class="f-mut" stroke="currentColor"/>
  <text x="30" y="198" fill="currentColor">토글 = dmb ishst; msr S3_4_C15_C2_7 (Apple 구현정의); isb  ← libsystem 실측</text>
  <text x="30" y="218" fill="currentColor">토글에는 ISB 가 있지만 IC IVAU 는 없다 → 토글만 하고 부르면 2000번 중 1985–1998번 옛 코드</text>
  <text x="30" y="238" fill="currentColor">RX 중 쓰기 → SIGBUS, RW 창에서 실행 → SIGBUS (fork 한 자식, 실측)</text>
  <text x="30" y="254" fill="currentColor">권한은 스레드별: 실행 스레드는 쓰는 스레드의 창과 무관하게 계속 돈다 (cross 실험)</text>
</svg>
```

macOS(Apple Silicon)의 실행 가능 메모리 규칙을 `demo_wx.c` 로 실제로 찔러 본 결과 (`wx_m6.txt`):

| 시도 | 결과 |
|---|---|
| `mmap(RWX)` (MAP_JIT 없이) | `Permission denied` (EACCES) |
| `mmap(RW)` → `mprotect(RX)` → 실행 | 된다 (`f(41) = 42`) — 서명 안 된 페이지도 실행 (hardened runtime 아님) |
| `mprotect(→ RWX)` | `Permission denied` |
| `mmap(RWX, MAP_JIT)` | 된다, `pthread_jit_write_protect_supported_np() = 1` |
| MAP_JIT, 보호 중 쓰기 / 쓰기 창에서 실행 | 둘 다 **SIGBUS (10)** |
| `codesign -dv` | `flags=0x20002(adhoc,linker-signed)` — 링커 ad-hoc 서명만 |

그래서 이 레슨의 바이너리는 entitlement 없이 MAP_JIT 를 쓴다. 앱을 hardened runtime 으로 서명하면 `com.apple.security.cs.allow-jit` entitlement 가 필요하다 (Apple 문서 기준, 확인 권장). 그리고 `cc` 기본 타깃은 `arm64` 이지 `arm64e` 가 아니다 (`file` 실측: `Mach-O 64-bit executable arm64`) — 그래서 JIT 주소를 그냥 함수 포인터로 캐스트해 부를 수 있다. arm64e 에서는 함수 포인터가 PAC 서명돼 있어야 하므로 `ptrauth_sign_unauthenticated(p, ptrauth_key_function_pointer, 0)` 같은 서명이 필요하다 (`<ptrauth.h>`, ARM-A10 영역, 확인 권장).

`jit_patch` 의 정답은 순서 그대로다:

```c
jit_writable(true);                                   /* 1) 이 스레드: RW, 실행 불가 */
for (size_t i = 0; i < n; i++) __atomic_store_n(&dst[i], w[i], __ATOMIC_RELAXED);
jit_writable(false);                                  /* 2) 다시 RX */
if (m == MAINT_SYS) os_icache_sync(dst, n * 4);       /* 3) I-cache 와 맞춘다 */
else if (m == MAINT_MANUAL) (void)cache_sync_range(dst, n * 4, g_geo.dline, g_geo.iline, g_geo.flags);
```

`memcpy` 대신 워드마다 32비트 store 를 쓰는 이유: 정렬된 32비트 store 는 단일 복사 원자적(single-copy atomic)이라, 다른 코어가 그 자리를 실행하는 중에도 "반쯤 바뀐 명령" 을 보지 않는다. `memcpy` 는 바이트 단위로 쓸 수도 있다 (보장 없음). 이것이 핫패치(아래 Part C, Q6)의 전제다.

### Part A — libsystem 은 실제로 무엇을 하나

`demo_libsys.lldb` 로 본 `sys_icache_invalidate` (전체는 `libsys_m6.txt`):

```text
libsystem_platform.dylib`sys_icache_invalidate:
    0x18c537f74 <+0>:   cbz    x1, 0x18c537fc8           ; <+84>
    0x18c537f78 <+4>:   and    x9, x0, #0xffffffffffffffc0
    ...
    0x18c537f90 <+28>:  dsb    ish
    0x18c537f94 <+32>:  mov    x2, #0x14                 ; =20 
    0x18c537f9c <+40>:  ic     ivau, x9
    0x18c537fa0 <+44>:  add    x9, x9, #0x40
    ...
    0x18c537fc0 <+76>:  dsb    ish
    0x18c537fc4 <+80>:  isb    
libsystem_platform.dylib`sys_dcache_flush:
    0x18c538028 <+0>: dsb    ish
    0x18c53802c <+4>: ret    
```

읽을 거리 네 가지.

1. **DC CVAU 가 없다.** 처음 `dsb ish` 하나로 앞선 store 들을 끝낸 뒤 곧바로 IC IVAU. 즉 Apple 은 이 코어에서 "D→PoU clean 이 필요 없다" (IDC=1 과 같은 동작)고 본다. 아래 Part B 실측도 같다: DC 없이 `dsb; ic; dsb; isb` 만으로 옛 코드 0/2000.
2. **64 B 간격.** 시작을 `& ~0x3f` 로 내리고 `add #0x40`. `sysctl hw.cachelinesize` 는 **128** 을 말하지만 (실측), IC IVAU 의 줄 단위는 64 B 로 쓴다. Lima 게스트에서 읽은 `CTR_EL0` 의 IminLine 도 64 B 다 (`ctr_vm.txt`).
3. **IC 20개마다 DSB (일부 CPU 만).** 20번째마다 commpage 의 `cpufamily` (0xFFFFFC080) 를 `cpus_that_need_dsb_for_ic_ivau` 목록과 비교한다. 이 Mac 의 값 `hw.cpufamily = 0x6d0ccb0c` 는 그 목록 (`0x1b588bb3 0xda33d83d 0x8765edea 0xfa33415e 0x2876f5b5 0x72015832 0x5f4dea93`, lldb `memory read` 실측) 에 없다 → M6 에서는 중간 DSB 없이 끝까지 돈다. 어느 패밀리가 어느 칩인지는 xnu 헤더로 확인 권장.
4. **`sys_dcache_flush` = `dsb ish` 하나.** DMA 와 캐시가 하드웨어로 일관된 SoC 라는 뜻으로 읽힌다 (추정).

리눅스 쪽 같은 일을 하는 libgcc 의 `__aarch64_sync_cache_range` (`__builtin___clear_cache` 가 부르는 것, VM 에서 `objdump -d`, `libgcc_vm.txt`) 는 우리 `cache_sync_range` 와 거의 한 줄씩 같다:

```text
    33d0:	d53b0023 	mrs	x3, ctr_el0
    33d8:	37e001c3 	tbnz	w3, #28, 3410 <...+0x50>      // IDC=1 → DC 루프 건너뜀
    3400:	d50b7b22 	dc	cvau, x2
    3410:	d5033b9f 	dsb	ish
    3418:	37e801e2 	tbnz	w2, #29, 3454 <...+0x94>      // DIC=1 → IC 루프 건너뜀
    3440:	d50b7520 	ic	ivau, x0
    3450:	d5033b9f 	dsb	ish
    3454:	d5033fdf 	isb
```

### Part A — 표준 시퀀스와 `cache_sync_range`

| 단계 | 명령 | 막는 문제 | 생략 조건 |
|---|---|---|---|
| ① | `dc cvau, x` (D-line 마다) | 새 바이트가 아직 L1D 에만 있어 I-fetch (PoU 아래에서 읽음) 가 못 본다 | `CTR_EL0.IDC = 1` |
| ② | `dsb ish` | ① 이 끝나기 전에 ③ 이 시작돼, 무효화 직후 다시 옛 값을 채운다 | 없음 (IDC=1 이어도 store 완료용 DSB 는 둔다) |
| ③ | `ic ivau, x` (I-line 마다) | I-cache 에 남은 옛 줄 (inner shareable 전체에 브로드캐스트) | `CTR_EL0.DIC = 1` |
| ④ | `dsb ish` | ③ 이 모든 코어에서 끝났음을 보장 | DIC=1 이면 ③ 과 함께 생략 |
| ⑤ | `isb` | **이 코어** 가 이미 가져온/디코드한 옛 명령 | 생략 불가 |

(각 명령의 정확한 정의 — PoU, IC IVAU 의 브로드캐스트 범위 — 는 Arm ARM DDI 0487 의 캐시 유지보수 장 기준, 확인 권장.)

`solution.S` 를 링크한 실제 기계어 (`/usr/bin/objdump -d --no-show-raw-insn /tmp/ARM-A04_solution`):

```text
100003068:     	bic	x9, x0, x8
10000306c:     	dc	cvau, x9
100003080:     	dsb	ish
100003084:     	tbnz	w4, #0x1, 0x1000030a8 <_cache_sync_range+0x58>
10000308c:     	bic	x9, x0, x8
100003090:     	ic	ivau, x9
1000030a4:     	dsb	ish
1000030a8:     	isb
1000030ac:     	orr	x0, x7, x6, lsl #32
```

**줄 개수 모델.** `[p, p+len)` 이 걸치는 줄 = `(p+len−1)/line − p/line + 1` (len > 0). 테스트는 시작 오프셋 0–127 × 길이 0–260 × (D, I) 줄 크기 (16, 64)/(64, 16) 를 전수로 돌려 asm 의 반환값과 비교한다. 경계 예: `p % 64 == 60`, len 8 → 두 줄 → `(2<<32)|2`. 시작을 내리지 않는 실수(변이 M2/M7)는 여기서 바로 잡힌다.

**줄 크기는 어디서?** 원칙은 `CTR_EL0` (DminLine, IminLine = log2(워드 수) → 바이트 = 4 << 필드). 그런데 macOS 는 EL0 의 `mrs ctr_el0` 를 막는다:

```text
  mrs ctr_el0                              -> killed by signal 4 (Illegal instruction: 4)
  mrs midr_el1                             -> killed by signal 4 (Illegal instruction: 4)
  DCZID_EL0 = 0x4 (DC ZVA block = 64 B)
  dc cvau (data buffer)                    -> ok (exit 0)
  ic ivau (data buffer)                    -> ok (exit 0)
  ic iallu (EL1-only op)                   -> killed by signal 4 (Illegal instruction: 4)
```

CTR 은 트랩되고 DC CVAU / IC IVAU 는 허용된다 — `SCTLR_EL1.UCT = 0`, `UCI = 1` 과 맞는 모습이다 (커널 설정을 직접 본 것은 아니다, 추정). 그래서 하네스의 macOS 경로는 근거 세 개 (libsystem 의 64 B 간격, 게스트 CTR, 아래 stride 실험) 로 64/64 를 쓰고 IDC/DIC 는 모른다고 보고 둘 다 수행한다. 리눅스 VM 에서는 `read_ctr_el0()` 가 그대로 되고 `flags=1` (IDC) 이 나온다.

`ctr_decode` 는 실제로 읽은 값으로 검사한다 (`ctr_vm.txt`, `demo_ctr.c`):

| 출처 | CTR_EL0 | I/D 줄 | L1Ip | ERG/CWG | IDC | DIC |
|---|---|---|---|---|---|---|
| Lima 게스트 (M6 위 vz 가상화) | `0x99444c004` | 64/64 | 3 (PIPT) | 64/64 | **1** | 0 |
| qemu `-cpu cortex-a53` | `0x84448004` | 64/64 | 2 (VIPT) | 64/64 | 0 | 0 |
| qemu `cortex-a72/a76/neoverse-n1` | `0x8444c004` | 64/64 | 3 | 64/64 | 0 | 0 |
| qemu `neoverse-v1` | `0x9444c004` | 64/64 | 3 | 64/64 | 1 | 0 |
| qemu `a64fx` | `0x86668006` | 256/256 | 2 | 256/256 | 0 | 0 |
| qemu-user `max` | `0x80038003` | 32/32 | 2 | 0 → 2 KB | 0 | 0 |

QEMU 값은 QEMU 의 CPU 모델이 정한 것이지 실리콘을 잰 것이 아니다. 게스트 값은 하이퍼바이저/게스트 커널이 손댔을 수 있다 (확인 권장). 게스트 값의 bit 32 이상 (`0x9`) 은 TminLine 필드 자리로 이 레슨에서는 쓰지 않는다.

### Part B — 빼먹으면? (M6 실측)

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">패치 직후 첫 호출이 옛 코드인 비율 (2000회, demo_stale alias 모드, 1회차)</text>
  <g fill="currentColor">
    <text x="20" y="48">유지보수 없음</text><text x="20" y="78">isb 만</text><text x="20" y="108">dsb; ic ivau; dsb (isb 없음)</text>
    <text x="20" y="138">ic ivau, 128 B 간격 (줄 놓침)</text><text x="20" y="168">dsb; ic ivau; dsb; isb</text>
    <text x="20" y="198">dc cvau; dsb; ic; dsb; isb</text><text x="20" y="228">sys_icache_invalidate</text>
  </g>
  <g stroke="currentColor">
    <rect x="250" y="36" width="400" height="16" class="f-warn"/>
    <rect x="250" y="66" width="400" height="16" class="f-warn"/>
    <rect x="250" y="96" width="398" height="16" class="f-warn"/>
    <rect x="250" y="126" width="400" height="16" class="f-warn"/>
    <rect x="250" y="156" width="2" height="16" class="f-ok"/>
    <rect x="250" y="186" width="2" height="16" class="f-ok"/>
    <rect x="250" y="216" width="2" height="16" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="660" y="48">1999</text><text x="660" y="78">2000</text><text x="660" y="108">1988</text>
    <text x="660" y="138">1999</text><text x="262" y="168">0</text><text x="262" y="198">0</text><text x="262" y="228">0</text>
  </g>
  <line x1="250" y1="28" x2="250" y2="236" stroke="currentColor"/>
  <text x="20" y="258" fill="currentColor">isb 없음: 옛 코드가 최대 24–44 번의 추가 호출 뒤 사라진다 (이미 가져온 명령만 남음)</text>
  <text x="20" y="276" fill="currentColor">유지보수 없음 / isb 만 / 128 B: 10^6 번 더 불러도 2000 중 수백 개는 여전히 옛 코드</text>
  <text x="20" y="294" fill="currentColor">DC CVAU 는 없어도 0 → 이 코어는 IDC=1 처럼 동작 (libsystem 도 DC 를 안 함)</text>
</svg>
```

`demo_stale.c` 의 alias 모드는 같은 물리 페이지를 RW 주소와 RX 주소에 따로 매핑한다 (`mach_vm_remap` + `mach_vm_protect`). 쓰기와 실행 사이에 권한 토글도 시스템 콜도 없으니 가장 순수한 I-cache 실험이다. 패치할 명령은 일부러 워드 19 (바이트 76) — 128 B 블록의 **두 번째** 64 B 줄 — 에 두었다. 실제 출력 (`stale_m6.txt`, 부하 `load averages: 21.20`, 두 번 돌린 것 중 1회차):

```text
alias: RW view 0x100a30000, RX view 0x100a34000 (same physical page)
  no maintenance                               stale 1999/2000   max extra calls 1000000   still stale after 1e6 calls: 348
  isb only                                     stale 2000/2000   max extra calls 1000000   still stale after 1e6 calls: 396
  dsb; ic ivau; dsb (no isb)                   stale 1988/2000   max extra calls      24   still stale after 1e6 calls: 0
  ic ivau on 128B-aligned addr (stride 128)    stale 1999/2000   max extra calls 1000000   still stale after 1e6 calls: 368
  dsb; ic ivau; dsb; isb                       stale    0/2000   max extra calls       0   still stale after 1e6 calls: 0
  dc cvau; dsb; ic ivau; dsb; isb              stale    0/2000   max extra calls       0   still stale after 1e6 calls: 0
  sys_icache_invalidate                        stale    0/2000   max extra calls       0   still stale after 1e6 calls: 0
jit: toggle W/X + nothing                  stale 1985/2000   still stale after 1e6 calls: 384
jit: toggle W/X + sys_icache_invalidate    stale    0/2000   still stale after 1e6 calls: 0
fresh: brand-new MAP_JIT region, no maintenance, first call stale 0/500
fresh: munmap+mmap (same VA 500/500), no maintenance, first call stale 0/500
cross: writer no maintenance,        executor no isb   stale-first-call 1816/2000   still stale after 1e6 calls: 423
cross: writer no maintenance,        executor isb      stale-first-call 1885/2000   still stale after 1e6 calls: 561
cross: writer sys_icache_invalidate, executor no isb   stale-first-call    0/2000   still stale after 1e6 calls: 0
cross: writer sys_icache_invalidate, executor isb      stale-first-call    0/2000   still stale after 1e6 calls: 0
```

줄마다 해석:

- **없음 / ISB 만** → 거의 100%. I-cache 는 D 쪽 store 를 엿듣지(snoop) 않는다 (DIC=0 과 일치). ISB 는 파이프라인만 비우고 다시 **I-cache 에서** 가져오므로 소용없다. 10⁶ 번 더 불러도 일부만 풀린다 — 풀린 것은 그 사이 줄이 밀려났거나 스레드가 다른 코어로 옮겨 간 경우로 보인다 (부하 ~21, 추정).
- **ISB 빼고 IC 만** → 첫 호출은 여전히 옛 코드 (1988/2000), 그러나 24–44 번 안에 풀린다. I-cache 는 고쳐졌지만 코어가 **이미 가져와 둔** 옛 명령(프리페치 버퍼·디코드된 형태 — 구현 세부는 비공개, 추정)이 남아 있다. 그래서 ⑤ ISB 가 필요하다.
- **128 B 간격** → `hw.cachelinesize = 128` 을 믿고 시작을 128 로 내리면 IC IVAU 는 바이트 0–63 줄에만 떨어지고, 패치한 64–127 줄은 그대로. 이 결과 자체가 **I-line 이 64 B 이하** 라는 실측 증거다. 줄 크기를 크게 잘못 알면 "가끔" 이 아니라 "절반의 줄" 이 틀린다 (연습 3).
- **DC CVAU 유무** 는 차이 없음 (둘 다 0) → IDC=1 처럼 동작. 하지만 이식성 있는 코드는 CTR 을 보고 결정한다 — 다른 칩(qemu cortex-a53 모델은 IDC=0)에서는 필수다.
- **MAP_JIT 토글만** → 1985/2000. 토글 안의 ISB 는 I-cache 를 고치지 못한다.
- **처음 쓰는 영역 / munmap 후 같은 주소** → 0/500. 한 번도 실행된 적 없는 줄은 I-cache 에 없으니 처음 fetch 가 새 값을 읽는다. 같은 VA 를 다시 받아도 새로 0 으로 채운 물리 페이지일 가능성이 크고 (커널 내부 처리는 확인 못 함), I-cache 가 물리 주소로 태그되면 (게스트 CTR 의 L1Ip = PIPT) 옛 줄과 안 맞는다. **이게 JIT 버그가 간헐적인 이유다: 처음 생성은 우연히 맞고, 같은 자리를 다시 패치할 때만 틀린다.**
- **cross-thread** → 쓰는 스레드가 유지보수를 안 하면 실행 스레드가 ISB 를 해도 옛 코드 (ISB 는 I-cache 를 안 고친다). 쓰는 스레드가 `sys_icache_invalidate` 를 하면 실행 스레드의 ISB 유무와 관계없이 0/2000 — IC IVAU 가 다른 코어의 I-cache 까지 무효화한 것이다. 다만 아키텍처는 **실행하는 코어에도 문맥 동기화(context synchronization) 사건이 있어야** 새 명령을 본다고 보장한다 (DDI 0487 의 동시 수정·실행 규칙, 확인 권장). 이 실험에서 그 차이가 안 보인 것은 실행 스레드가 원자 변수 대기 → 호출 사이에 이미 오래된 명령을 버렸기 때문으로 추정한다. **관찰 안 됨 ≠ 안전**: 리눅스 커널은 커널 텍스트를 고친 뒤 모든 CPU 에 IPI 를 보내 (`kick_all_cpus_sync()`) 문맥 동기화를 강제하고, 유저 JIT 는 `membarrier(MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE)` 를 쓴다 (커널 버전마다 다름, 확인 권장).

### Part B — 테스트가 무엇을 잡나 (변이 주입)

정답 사본(개인 scratch 디렉터리)에 버그를 넣고 하네스를 돌린 실제 결과:

| 변이 | 무엇을 바꿨나 | 결과 | 잡은 테스트 |
|---|---|---|---|
| M1 | `enc_imm26` 에서 `& 0x3FFFFFF` 삭제 | **FAIL** 44/46 | 골든 (`b . - 8` 이 opcode 비트를 덮음), B/BL 왕복 |
| M2 | DC 루프 시작을 내리지 않음 (`bic` → `mov`) | **FAIL** 44/46 | 줄 개수 전수, `buf+60` 경계 |
| M3 | `MAINT_MANUAL` 이 아무것도 안 함 | **FAIL** 42/46 | JIT 재패치 (실제로 옛 코드가 돌아서) |
| M4 | `ctr_decode` 에서 `4u <<` 대신 `1u <<` (워드↔바이트) | **FAIL** 40/46 | CTR 값 표 |
| M5 | ADD 디코드 마스크 `0xFF000000` (ADDG 수용) | **FAIL** 45/46 | 이웃 인코딩 거절 |
| M7 | IC 루프 시작을 내리지 않음 | **FAIL** 43/46 | 줄 개수 |
| M8 | `ic ivau` 를 `nop` 으로 (DSB·ISB 는 유지) | **FAIL** 42/46 (3번 모두, 3000 중 0–1 개만 맞음) | JIT 재패치 |
| M6 | 마지막 `isb` 를 `nop` 으로 | **PASS 46/46** (5번 모두) | 못 잡음 |

M3/M8 이 잡힌 것은 결정적 논리 때문이 아니라 **이 칩에서 옛 코드가 실제로 실행되기 때문**이다 (IC 가 일관된 칩이었다면 통과했을 것). M6 은 못 잡는다: 하네스는 패치 뒤 호출 전에 `jit_verify` 로 256 워드를 읽는 등 수백 개 명령을 실행하므로, ISB 없이도 그사이 옛 명령이 빠져나간다. demo 의 "isb 없음" 줄은 패치 **직후** 바로 불렀기 때문에 보였다. 교훈: ISB 누락은 테스트로 잡기 어렵다 → 리뷰 체크리스트·정적 검사로 잡는다.

### Part C — 표현식 JIT

`compile_affine` 이 만든 ADC 보정 코드 (gain 1.25 = 81920 in Q16, offset −100 LSB = −6553600 in Q16, s = 16). 워드를 `.inst` 로 다시 어셈블해 objdump 로 본 실제 디스어셈블:

```text
       0: d2880009     	mov	x9, #0x4000             ; =16384
       4: f2a00029     	movk	x9, #0x1, lsl #16
       8: 9b097c00     	mul	x0, x0, x9
       c: d280000a     	mov	x10, #0x0               ; =0
      10: f2bff38a     	movk	x10, #0xff9c, lsl #16
      14: f2dfffea     	movk	x10, #0xffff, lsl #32
      18: f2ffffea     	movk	x10, #0xffff, lsl #48
      1c: 8b0a0000     	add	x0, x0, x10
      20: 9350fc00     	asr	x0, x0, #16
      24: d65f03c0     	ret
```

`f(2000) = (81920·2000 − 6553600) >> 16 = 157286400 >> 16 = 2400`, `f(40) = −3276800 >> 16 = −50` — 하네스가 두 값을 CHECK 한다. 음수 offset 은 MOVZ + MOVK 3개 (4워드) 가 들었다. MOVN 을 쓰면 `movn x10, #0x63, lsl #16` + `movk x10, #0` 2워드면 된다 — 이것이 ARM-B03 의 MOVZ/MOVN 계획 문제다.

| 상수 모양 | 워드 수 | 예 |
|---|---|---|
| a=1, b=0, s=0 | 1 | `ret` |
| a=3, b=7, s=2 | 5 | `movz x9,#3` `mul` `add #7` `asr #2` `ret` |
| a=−3, b=0x123456789, s=63 | 11 | `movz/movk×3` `mul` `movz/movk×2` `add x0,x0,x10` `asr #63` `ret` |

참조 인터프리터는 **부호 없는** 64비트 산술로 쓴다: `asr64(a*x + b, s)`. 부호 있는 `int64_t` 로 `a*x` 를 하면 넘침이 UB(C11 6.5p5)이고, 음수의 `>>` 는 구현정의(6.5.7p5)다. 기계어는 둘 다 정의돼 있다 (MUL = 하위 64비트, ASR = 부호 비트 복제). 그래서 테스트는 C 의 UB 를 평가하지 않고 같은 비트 결과를 부호 없는 연산으로 만든다.

**펌웨어로 옮기면.**

- **Cortex-M0/M3/M4** 는 아키텍처 캐시가 없다. 하지만 벤더 플래시 가속기(예: STM32F4 의 ART: 명령 캐시 + 프리페치)가 있으면 플래시를 다시 쓴 뒤 그 캐시를 리셋해야 한다 (`FLASH_ACR` 의 ICRST 비트, RM0090 확인 권장). RAM 으로 복사한 함수는 `dsb; isb` 후 Thumb 비트(LSB=1) 붙은 주소로 부른다.
- **Cortex-M7** 은 L1 I/D 캐시 (줄 32 B) 가 있다. RAM/ITCM 으로 코드를 복사했다면 D 쪽 clean (`SCB->DCCMVAU` 줄마다) → DSB → I 쪽 무효화 (`SCB->ICIMVAU` 줄마다 또는 `SCB->ICIALLU`) → DSB → ISB. CMSIS 의 `SCB_InvalidateICache()` 는 `__DSB(); __ISB(); SCB->ICIALLU = 0; __DSB(); __ISB();` (core_cm7.h 기준, 확인 권장). 연습 4 가 이 순서를 가짜 레지스터 로그로 검증한다.
- **Cortex-A 베어메탈/부트로더**: 앱 이미지를 DRAM 에 복사하고 점프하기 전에 같은 시퀀스. MMU/캐시를 켠 상태라면 EL1 에서 `ic iallu` (전체) 도 쓸 수 있다 — 위 실측처럼 EL0 에서는 SIGILL.
- **DMA 로 코드를 받는 경우**: DMA 가 캐시와 일관되지 않으면 ①이 아니라 "D-cache invalidate" 가 먼저 필요하다 (방향이 반대). Apple 은 `sys_dcache_flush` 가 `dsb ish` 뿐이다 — 일관된 DMA 로 읽힌다 (추정).

**비용** (`demo_bench.c`, `bench_m6.txt`, 중앙값 7회, 두 번 실행의 범위, 부하 ~21, 코어 종류 고정 안 함 — 비율로만 읽을 것):

| 연산 | ns/op |
|---|---|
| `pthread_jit_write_protect_np(0)` + `(1)` | 34–38 |
| `sys_icache_invalidate` 4 B (1줄) | 94–99 |
| `cache_sync_range` DC+IC 4 B | 100–142 |
| `sys_icache_invalidate` 4 KB (64줄) | 441–495 |
| `cache_sync_range` DC+IC 4 KB / IC 만 4 KB | 440–470 / 379–401 |
| `sys_icache_invalidate` 64 KB | 4383–5550 |
| `mprotect` RW + RX 왕복 (시스템 콜 2번) | 1202–1615 |
| 1워드 패치 전체 (토글 + store + 1줄 무효화) | 147–210 |

1워드 패치 하나가 수백 ns, 줄당 약 7 ns. MAP_JIT 토글은 mprotect 왕복보다 30배 이상 싸다 (두 실행의 비 31–47배) — 스레드별 토글이 시스템 콜 없이 레지스터 쓰기 하나라서다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

<!-- code: solution.S diff=starter.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c solution.S shared_ref.S -o /tmp/ARM-A04_solution && /tmp/ARM-A04_solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c solution.S shared_ref.S -o /tmp/ARM-A04_san && /tmp/ARM-A04_san
```

실제 출력 (PASS 줄 생략, exit 0, 0.16 s; ASan/UBSan 빌드도 `46/46 passed`, 보고 없음 — fork 한 자식은 시그널 처리를 `SIG_DFL` 로 되돌려 ASan 의 SIGBUS 처리기가 끼지 않게 했다):

```text
INFO  cache geometry: dline=64 iline=64 flags=0 (macOS fallback (CTR_EL0 trapped))
INFO  sys_icache_invalidate: 3000 patch+call ok
INFO  manual DC/IC/ISB: 3000 patch+call ok
INFO  write while RX -> signal 10 (Bus error: 10), exec while RW -> signal 10 (Bus error: 10)
INFO  mrs ctr_el0 at EL0 -> signal 4
INFO  affine JIT: 16000 calls matched the interpreter
46/46 passed
```

**리눅스(ELF)에서도.** `.S` 의 `FUNC/ENDF` 매크로와 `SYM()` 이 Mach-O/ELF 차이를 흡수하고, C 의 플랫폼 층이 `MAP_JIT`/토글 대신 RWX 매핑 + `__builtin___clear_cache` 를 쓴다 (W^X 테스트는 macOS 전용이라 2개 적다). Lima VM 실제 실행:

```text
gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
INFO  cache geometry: dline=64 iline=64 flags=1 (CTR_EL0)
INFO  sys_icache_invalidate: 3000 patch+call ok
INFO  manual DC/IC/ISB: 3000 patch+call ok
INFO  affine JIT: 16000 calls matched the interpreter
44/44 passed
Ubuntu clang version 21.1.8 (6ubuntu1)
...
44/44 passed
   101: 00000000000032a0   100 FUNC    GLOBAL DEFAULT   12 cache_sync_range
```

(리눅스 쪽 INFO 의 "sys_icache_invalidate" 는 실제로 `__builtin___clear_cache` 다. `flags=1` = 게스트 CTR 의 IDC=1 → DC 루프 생략.) 처음 VM 빌드는 `shared_ref.S:51: Error: selected processor does not support 'retaa'` 로 실패했다 — Apple clang 은 기본 타깃에 PAuth 가 있지만 리눅스 gcc/clang 은 아니다. `.arch_extension pauth` 를 넣어 고쳤다 (작성 중 실제로 겪은 것).

실측 데모 빌드:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_stale.c -o /tmp/ARM-A04_stale && /tmp/ARM-A04_stale all        # stale_m6.txt
cc -std=c11 -Wall -Wextra -Werror -O2 demo_wx.c -o /tmp/ARM-A04_wx && /tmp/ARM-A04_wx                   # wx_m6.txt
lldb -b -s demo_libsys.lldb /tmp/ARM-A04_wx                                                              # libsys_m6.txt
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c solution.S -o /tmp/ARM-A04_bench && /tmp/ARM-A04_bench # bench_m6.txt
limactl shell elinux -- bash -lc 'gcc -O2 -static <레슨경로>/demo_ctr.c -o /tmp/ARM-A04_work/ctr && /tmp/ARM-A04_work/ctr'  # ctr_vm.txt
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 코드를 쓰고 바로 함수 포인터로 호출 | I-cache 에 옛 줄 → M6 실측 1999/2000 옛 코드 | `sys_icache_invalidate(p, n)` 또는 DC/DSB/IC/DSB/ISB |
| `isb` 만 하면 된다고 생각 | ISB 는 파이프라인만 비우고 다시 I-cache 에서 가져온다 (실측 2000/2000) | IC IVAU 가 먼저, ISB 는 마지막 |
| IC 루프 뒤 `isb` 생략 | 이미 가져온 옛 명령 실행 (실측: 첫 호출 1988/2000, 24–44 호출 뒤 회복) | `dsb ish; isb` |
| `hw.cachelinesize` (128) 를 IC 간격으로 | I-line 은 64 B → 줄 절반 누락 (실측 1999/2000) | `CTR_EL0.IminLine` (macOS 면 `sys_icache_invalidate` 를 쓰는 게 정답) |
| 시작 주소를 줄 경계로 안 내림 | `[60, 68)` 의 두 번째 줄을 놓침 | `bic x9, x0, (line − 1)` |
| `memcpy` 로 실행 중인 B 를 교체 | 바이트 단위 쓰기 가능 → 반쯤 바뀐 명령 | 정렬된 32비트 store (`__atomic_store_n`) |
| 다른 코어 고려 없이 내 코어만 ISB | 실행 코어에 문맥 동기화가 없을 수 있다 | IPI/`membarrier(SYNC_CORE)`, 또는 CMODX 안전 명령만 교체 (연습 5) |
| macOS 에서 `mrs ctr_el0` | SIGILL (실측) | `sys_icache_invalidate` 에 맡기거나 64 B 근거를 문서화 |
| 음수 `off >> 2` | 부호 있는 오른쪽 시프트는 구현정의 | `((uint64_t)off >> 2) & mask` |
| ADD 디코드를 `0xFF000000` 로 | ADDG(MTE) 를 ADD 로 오인 | `0xFF800000` |
| arm64e 에서 JIT 주소를 그냥 캐스트 | PAC 서명 없는 함수 포인터 → 인증 실패 | `ptrauth_sign_unauthenticated` (확인 권장) |

## 8. 면접 Q&A

<details>
<summary>Q1. JIT 로 코드를 쓴 뒤 실행하려면 무엇을 어떤 순서로 해야 하나? 각 명령의 이유는?</summary>

`dc cvau` (D-line 마다, 새 바이트를 PoU 까지) → `dsb ish` (clean 완료 후에 무효화 시작) → `ic ivau` (I-line 마다, 옛 줄 제거, inner shareable 브로드캐스트) → `dsb ish` (무효화 완료) → `isb` (이 코어가 이미 가져온 명령 폐기). 순서가 바뀌면 무효화 직후 아직 L1D 에만 있는 새 값 대신 PoU 의 옛 값으로 I-cache 가 다시 채워질 수 있다. M6 실측: 아무것도 안 하면 1999/2000, ISB 만 2000/2000, ISB 를 빼면 첫 호출 1988/2000 이 옛 코드.

</details>

<details>
<summary>Q2. CTR_EL0 의 IDC, DIC 가 1 이면 무엇을 생략할 수 있나? ISB 도 생략할 수 있나?</summary>

IDC=1: I/D 일관성을 위해 D→PoU clean 이 필요 없다 → DC 루프 대신 `dsb ish` 만 (store 완료). DIC=1: I-cache 무효화가 필요 없다 → IC 루프 생략. 그래도 ISB 는 남는다 — 파이프라인에 이미 들어온 명령은 캐시와 무관하다. libgcc 의 `__aarch64_sync_cache_range` 가 정확히 `tbnz w3, #28` / `tbnz w2, #29` 로 이렇게 분기하고 마지막 `isb` 는 항상 실행한다 (VM 디스어셈블 실측). Lima 게스트에서 읽은 M6 값은 IDC=1, DIC=0.

</details>

<details>
<summary>Q3. 줄 크기는 어디서 알아내나? macOS 에서는? 꼬리질문: hw.cachelinesize 를 쓰면 왜 안 되나?</summary>

`CTR_EL0` 의 IminLine/DminLine (log2 워드 수, 바이트 = 4 << 필드). 이종(big.LITTLE) 시스템은 코어마다 다를 수 있어 리눅스는 시스템 전체의 안전한 값(최솟값)을 쓴다 (확인 권장). macOS 는 EL0 의 `mrs ctr_el0` 가 SIGILL (실측)이라 직접 못 읽는다 → `sys_icache_invalidate` 를 쓰는 게 정답이고, 그 안은 64 B 간격이다. `hw.cachelinesize` = 128 은 다른 수준(아마 L2/일관성 단위)의 줄이다; 이걸 IC 간격으로 쓰면 실측대로 64 B 줄의 절반을 놓친다. 모를 때의 안전한 선택은 **작은** 간격(과잉 무효화는 느릴 뿐 틀리지 않는다).

</details>

<details>
<summary>Q4. 다른 코어가 그 코드를 실행 중이라면? (staff 꼬리질문: 어떻게 설계하겠나?)</summary>

IC IVAU 는 inner shareable 도메인에 브로드캐스트되고 DSB ISH 로 완료를 기다리므로 다른 코어의 I-cache 도 고쳐진다 (실측: writer 만 `sys_icache_invalidate` 해도 0/2000). 하지만 아키텍처 보장은 "실행하는 코어가 문맥 동기화 사건(ISB, 예외 진입/복귀)을 거친 뒤" 다. 설계 선택지: (1) 새 코드를 **새 주소**에 쓰고 발행(publish)은 포인터 하나의 release store 로 → 실행 쪽은 acquire load 후 처음 보는 주소로 점프 (옛 줄이 없다 — fresh 실험 0/500). (2) 제자리 수정이 꼭 필요하면 모든 실행 스레드를 안전 지점에 세우거나 `membarrier(SYNC_CORE)`/IPI 로 문맥 동기화를 강제 (리눅스 커널의 `kick_all_cpus_sync()`). (3) 한 워드짜리 B/NOP 교체만 하는 핫패치 (Q6). (1) 은 "제자리 수정" 자체를 없애는 설계라 가장 단순하지만 코드 메모리 회수(옛 버전을 아무도 실행하지 않는 시점 판단)가 새 문제가 된다.

</details>

<details>
<summary>Q5. MAP_JIT 와 pthread_jit_write_protect_np 는 무엇을 막나? 왜 스레드별인가? 대안과 비교하면?</summary>

W^X: 한 시점에 한 스레드에게 페이지는 쓰기 가능이거나 실행 가능, 둘 중 하나. RWX 매핑은 거절되고 (`mmap(RWX)` EACCES 실측), MAP_JIT 페이지는 스레드가 토글로 창을 연다. 어기면 SIGBUS (쓰기·실행 둘 다 실측). 스레드별이라 쓰는 스레드가 창을 연 동안 다른 스레드는 계속 실행할 수 있고, 토글은 시스템 콜 없이 레지스터 쓰기 + ISB 라 34–38 ns (mprotect 왕복 1.2–1.6 µs 보다 30배 이상 빠름). 대안: 같은 물리 페이지의 RW 뷰 + RX 뷰 이중 매핑 (리눅스 memfd 두 번 매핑, 이 레슨의 alias 실험) — 토글이 없지만 RW 주소가 공격자에게 새면 코드 주입이 된다. `mprotect` 전환은 단순하지만 느리고 프로세스 전체에 영향.

</details>

<details>
<summary>Q6. 실행 중인 함수의 입구 B 한 워드만 바꾸는 핫패치는 언제 안전한가? 이 설계가 틀리는 경우는?</summary>

조건: (1) 정렬된 32비트 store 하나 (단일 복사 원자성 — `memcpy` 아님), (2) 옛 명령과 새 명령이 모두 Arm ARM 의 "동시 수정·실행 허용" 집합 — B, BL, BRK, NOP, ISB, SVC, HVC, SMC (리눅스 `aarch64_insn_hotpatch_safe()` 도 같은 목록, 문서 확인 권장), (3) 이후 IC IVAU + DSB, 그리고 실행 코어들의 문맥 동기화 전에는 옛/새 중 아무거나 실행될 수 있음을 받아들일 것. 틀리는 경우: 두 워드 이상 (ADRP+ADD 쌍, MOVZ/MOVK 상수) 을 바꾸면 중간 상태가 실행될 수 있다; ADD 의 즉시값을 바꾸는 것은 집합 밖이다 (연습 5 가 `add #1 → add #2` 를 거절); B 의 ±128 MB 를 넘으면 veneer 가 필요한데 veneer 자체가 여러 워드다 (연습 2). 그래서 ftrace 는 함수 입구에 NOP 를 미리 심어 두고 NOP ↔ BL 만 바꾼다 (확인 권장).

</details>

<details>
<summary>Q7. "처음 JIT 할 땐 맞는데 같은 자리를 다시 고치면 가끔 틀린다." 왜 간헐적이고 어떻게 찾나?</summary>

처음 쓰는 영역은 I-cache 에 없으니 첫 fetch 가 새 값을 읽는다 (실측 0/500). 같은 자리를 이미 실행해 I-cache 가 데워진 뒤 고칠 때만 옛 줄이 남는다 (1999/2000). 줄이 밀려나거나 스레드가 다른 코어로 옮겨 가면 저절로 풀리기도 한다 (10⁶ 호출 뒤 일부 회복) → 재현이 들쭉날쭉하다. 찾는 법: (1) 패치 함수에 유지보수가 빠진 경로가 없는지 리뷰, (2) 이 레슨처럼 "데운 뒤 고치고 바로 부르는" 스트레스 루프, (3) 쓰는 주소 범위와 무효화 범위(줄 경계 내림, 길이)를 로깅, (4) 의심되면 무효화 범위를 크게 잡아 증상이 사라지는지.

</details>

<details>
<summary>Q8. 펌웨어에서는 이 문제가 언제 나오나? Cortex-M 에도 있나?</summary>

부트로더가 앱을 RAM 으로 복사해 점프할 때, 플래시를 다시 쓴 뒤 (XIP), RAM 함수(플래시 지우는 동안 실행할 코드)를 쓸 때, 디버거가 소프트웨어 브레이크포인트를 심을 때, DMA 로 코드를 받을 때. Cortex-M0/M3/M4 는 아키텍처 캐시가 없지만 벤더 플래시 가속기(ART 등)가 명령을 캐시하면 리셋이 필요하다. Cortex-M7 은 L1 캐시가 있어 `SCB_CleanDCache_by_Addr` → `SCB_InvalidateICache` (DSB/ISB 포함) 순서. Cortex-A 베어메탈은 이 레슨과 같은 시퀀스, EL1 이면 `ic iallu` 도 가능 (EL0 에서는 SIGILL 실측). 실리콘 검증에서는 "self-modifying code" 테스트가 바로 이 시퀀스의 각 단계를 빼고 실패를 확인하는 형태다.

</details>

<details>
<summary>Q9. 이 레슨의 테스트로 ISB 누락을 잡을 수 있나? 못 잡는다면 어떻게 하겠나?</summary>

못 잡았다 (변이 M6: 5번 모두 46/46). 하네스는 패치와 호출 사이에 수백 개 명령(검사기)을 실행해 옛 명령이 이미 빠져나간다. demo 처럼 패치 직후 바로 호출하는 경로에서만 보였다 (첫 호출 1988/2000). 대책: 유지보수는 검증된 한 함수(`sys_icache_invalidate`, `__builtin___clear_cache`)로만 하게 강제하고, 직접 짠 asm 은 objdump 로 마지막 명령이 `isb` 인지 검사하는 정적 테스트를 둔다. 또 IC 가 일관된 칩 (DIC=1) 에서는 M3/M8 같은 변이도 통과할 것이다 — "테스트가 PASS = 코드가 이식 가능" 이 아니다.

</details>

## 9. 추가 연습

### 연습 1 — B.cond 와 CBNZ

`bool enc_bcond(uint32_t *out, unsigned cond, int64_t off)`, `bool enc_cbnz(uint32_t *out, unsigned rt, int64_t off)`, 그리고 둘을 구분해 디코드하는 `dec_cond_branch`. `practice.S` 의 `b.eq/b.ne/b.hi/b.lt`, `cbnz` 워드와 비교.

<details>
<summary>답 보기</summary>

B.cond = `0x54000000 | imm19<<5 | cond` (bit 4 = 0), CBNZ(64) = `0xB5000000 | imm19<<5 | Rt`. 범위는 CBZ 와 같은 ±1 MB. 실제 워드: `b.eq . + 0x20` = `0x54000100`, `b.lt . - 0x100000` = `0x5480000b`, `cbnz x30, . - 0x100000` = `0xb580001e`. 디코드 마스크 `(w & 0xFF000010) == 0x54000000` 이 B.cond, `& 0xFF000000 == 0xB5000000` 이 CBNZ. 무작위 2만 개 왕복. JIT 에서 조건 분기 범위가 ±1 MB 뿐이라 큰 코드 버퍼에서는 "조건 반전 + B" 로 바꿔야 한다.

</details>

### 연습 2 — 분기 또는 veneer

`size_t emit_jump(uint32_t *buf, uint64_t site, uint64_t target)` — 거리가 B 범위면 1워드, 아니면 고정 5워드 veneer (`movz x16` + `movk x16 ×3` + `br x16`). JIT 에서 C 함수 `c_target(x) = 3x + 1` 을 veneer 로 실제 호출.

<details>
<summary>답 보기</summary>

정확히 −128 MB (`0x16000000`) 는 B, 4 B 더 멀면 veneer. veneer 는 0 인 halfword 도 `movk` 로 써서 **크기를 고정** — 패치 슬롯을 미리 5워드로 잡아 둘 수 있다. x16(IP0)는 AAPCS64 가 링커 veneer 용으로 비워 둔 레지스터라 호출 사이에 깨도 된다. `br x16` 은 LR 을 안 바꾸므로 `c_target` 이 JIT 코드를 부른 쪽으로 바로 돌아간다 (꼬리 호출). 실측에서 JIT 영역과 `__TEXT` 는 ASLR 에 따라 128 MB 안에 들어올 때가 많아 `emit_jump` 는 1워드를 골랐고, veneer 는 따로 강제로 만들어 `f(7) = 22` 를 확인했다. 링커가 BL 범위 밖 호출에 넣는 것도 같은 veneer (ARM-A09).

</details>

### 연습 3 — 줄 크기를 잘못 알면 몇 줄을 놓치나

`uint64_t missed_lines(start, len, real_line, stride)` — 실제 줄이 `real_line` 인 캐시에서 시작을 `stride` 로 내리고 `stride` 간격으로 IC IVAU 하면, 범위가 걸친 실제 줄 중 몇 개가 무효화되지 않는가. 무작위 5000 개를 브루트포스와 비교.

<details>
<summary>답 보기</summary>

`(0x1000+76, 4 B, 64, 128)` → 1 (demo_stale 의 128 B 실험 그대로). 정렬된 4 KB 에서 stride 128 → 64줄 중 **32줄** 누락, stride 32 (작게 잡음) → 0. "작게 잡으면 느리고, 크게 잡으면 틀린다". 브루트포스를 처음 쓸 때 범위 앞 줄에 떨어지는 IC 주소(시작을 stride 로 내린 결과)를 배열 인덱스 −1 에 써서 정적 변수를 덮었고 UBSan 이 `index 18446744073709551615 out of bounds` 로 잡았다 (작성 중 실제로 낸 버그 — 테스트 카운터가 이상한 값이 됐다).

</details>

### 연습 4 — Cortex-M7 SCB 유지보수 순서

`m7_sync_icache(log, addr, len)` — RAM 에 복사한 코드에 대해 레지스터 쓰기 로그를 만든다: `DCCMVAU`(0xE000EF64) 32 B 줄마다 → DSB → `ICIMVAU`(0xE000EF58) 줄마다 → DSB → ISB. `m7_invalidate_icache_all` 은 CMSIS 순서.

<details>
<summary>답 보기</summary>

`0x20000010`, 100 B → 줄 `0x20000000, 20, 40, 60` 4개 → 로그 4 + 1 + 4 + 1 + 1 = 11개. 전체 무효화는 `DSB, ISB, ICIALLU(0xE000EF50)=0, DSB, ISB`. 레지스터 주소와 32 B 줄은 CMSIS core_cm7.h / Cortex-M7 TRM 기준 (확인 권장). 실제 보드라면 로그 대신 `*(volatile uint32_t *)0xE000EF64 = a;` 다. 캐시가 꺼진 M7 이나 M4 에서는 이 레지스터 쓰기가 필요 없다 — 하지만 DSB/ISB 는 여전히 둔다 (코드 복사 후 파이프라인).

</details>

### 연습 5 — 동시 수정해도 되는 명령 쌍

`bool cmodx_safe(uint32_t old, uint32_t new)` — 둘 다 {B, BL, BRK, SVC, HVC, SMC, NOP, ISB} 면 true. `practice.S` 의 진짜 워드로 9×9 쌍은 전부 true, `add/movz/ret/dsb` 가 끼면 false.

<details>
<summary>답 보기</summary>

마스크: B/BL `(w & 0x7C000000) == 0x14000000`, BRK `(w & 0xFFE0001F) == 0xD4200000`, SVC/HVC/SMC 는 하위 2비트 01/10/11, NOP `0xD503201F`, ISB `(w & 0xFFFFF0FF) == 0xD50330DF`. `dsb ish` (`0xd5033b9f`) 는 같은 배리어 그룹이지만 집합 밖. 이 목록 밖의 교체 (`add #1 → add #2`) 는 다른 코어가 실행 중이면 멈추고 바꿔야 한다. 목록은 Arm ARM 의 concurrent modification and execution 절 / 리눅스 `aarch64_insn_hotpatch_safe()` 기준이며, 아키텍처 버전에 따라 조건이 달라질 수 있다 (문서 확인 권장).

</details>

### 한 번에 검증

<!-- code: practice.c -->

<!-- code: practice.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_ref.S -o /tmp/ARM-A04_practice && /tmp/ARM-A04_practice   # 29/29 passed
```

ASan/UBSan 빌드 (`-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined`) 도 `29/29 passed`.

## 10. 핵심 정리

- 코드를 데이터로 썼으면: `dc cvau` → `dsb ish` → `ic ivau` → `dsb ish` → `isb`. IDC=1 이면 DC 생략, DIC=1 이면 IC 생략, **ISB 는 항상**.
- M6 실측: 유지보수 없음 1999/2000, ISB 만 2000/2000, ISB 빼면 첫 호출 1988/2000 (24–44 호출 뒤 회복), IC+ISB 0/2000. DC 는 없어도 0 (IDC=1 처럼).
- 처음 쓰는 영역은 우연히 맞는다 (0/500) → 재패치 때만 틀린다 = 간헐적 JIT 버그.
- 줄 크기는 `CTR_EL0` (4 << 필드). macOS 는 EL0 CTR 읽기가 SIGILL → `sys_icache_invalidate` (64 B 간격, DC 없음, 마지막 isb). `hw.cachelinesize` 128 을 쓰면 줄 절반을 놓친다.
- macOS: RWX 는 MAP_JIT 만, 스레드별 토글 (dmb + msr 구현정의 레지스터 + isb, 34–38 ns). W^X 위반은 SIGBUS.
- 다른 코어: IC IVAU 는 브로드캐스트되지만 실행 코어의 문맥 동기화가 보장 조건 → 새 주소 발행, IPI/membarrier, 또는 B/NOP 한 워드 핫패치.
- 인코딩: ADD imm `0x91000000`, MOVZ `0xD2800000`, MOVK `0xF2800000`, RET `0xD65F03C0`, B `0x14000000` (±128 MB), CBZ `0xB4000000` (±1 MB). 음수 오프셋은 부호 없이 시프트 후 마스크.
- 펌웨어: RAM 함수·부트로더 복사·플래시 재기록·M7 `SCB_InvalidateICache` 가 같은 문제.

## 참고자료

- [Arm Architecture Reference Manual for A-profile (DDI 0487)](https://developer.arm.com/documentation/ddi0487/latest) — 캐시 유지보수 명령 (DC CVAU, IC IVAU, PoU), "Concurrent modification and execution of instructions", CTR_EL0 / SCTLR_EL1.UCT·UCI, B/BL/CBZ/ADD/MOVZ 인코딩
- [Arm — CTR_EL0 register description](https://developer.arm.com/documentation/ddi0601/latest/AArch64-Registers/CTR-EL0--Cache-Type-Register)
- [Apple — Porting just-in-time compilers to Apple silicon](https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon) (MAP_JIT, `pthread_jit_write_protect_np`, `sys_icache_invalidate`, allow-jit entitlement)
- Linux `arch/arm64/include/asm/cacheflush.h` (`flush_icache_range`, `kick_all_cpus_sync`), `arch/arm64/mm/cache.S` (`caches_clean_inval_pou`), `arch/arm64/kernel/insn.c` (`aarch64_insn_hotpatch_safe`), `membarrier(2)` (`MEMBARRIER_CMD_PRIVATE_EXPEDITED_SYNC_CORE`)
- libgcc `config/aarch64/sync-cache.c` (`__aarch64_sync_cache_range`) — VM 디스어셈블 `libgcc_vm.txt`
- QEMU `target/arm/cpu64.c` (CPU 모델별 CTR 값) — `ctr_vm.txt`
- Arm Cortex-M7 Technical Reference Manual / CMSIS `core_cm7.h` (`SCB_InvalidateICache`, `SCB_CleanDCache_by_Addr`, ICIALLU/ICIMVAU/DCCMVAU)
- STMicroelectronics RM0090 (STM32F4 ART accelerator, FLASH_ACR ICRST)
- 이 레슨의 실측 파일: `stale_m6.txt`, `wx_m6.txt`, `libsys_m6.txt`, `bench_m6.txt`, `ctr_vm.txt`, `libgcc_vm.txt`
- 관련 레슨: ARM-B03 (MOVZ/MOVN 계획, 인코딩 기초), ARM-M05 (DSB/ISB 의미), ARM-M03 (ISB 로 직렬화한 측정), ARCH-M06 (DCZID/캐시 줄), ARM-B12 (Thumb 인코딩); 다음 단계 ARM-A09 (ADRP/GOT, veneer), ARM-A10 (PAC/BTI, arm64e 함수 포인터 서명)
