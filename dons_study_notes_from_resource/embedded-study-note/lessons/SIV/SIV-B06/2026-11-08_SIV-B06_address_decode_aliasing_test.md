---
id: SIV-B06
time: 70분
updated: 2026-11-08
---
# SIV-B06 주소 디코드·Aliasing 테스트

## 학습 목표

- 레지스터마다 "쓰고 바로 읽는" 테스트(SIV-B04)가 왜 **aliasing**(두 주소가 같은 플롭을 가리킴)을 못 잡는지 설명하고, **전부 쓰고 → 전부 읽는** 고유 패턴 테스트를 작성한다.
- **walking-address**(앵커 0x000 + 탐침 2^k) 테스트로 주소 비트 하나하나를 시험하고, 결과를 "의심 주소 비트 마스크"로 낸다.
- 좁은 필드(2비트) 때문에 해시 패턴이 놓치는 aliasing을 **index-bit 다중 패스**로 확실히 잡고, 홀(hole) 검사로 **부분 디코드(mirror)** 를 잡는다.
- 결함 주입(주소 비트 무시 / 디코드 엔트리 오류 / 상위 비트 미디코드)마다 **원인 비트·원인 레지스터 쌍**을 추정해 디버그 엔지니어가 바로 쓸 리포트를 낸다.
- RC·W1C·RO 레지스터를 건드리지 않는 aliasing 테스트를 설계하고, pre-silicon(UVM RAL backdoor)과 post-silicon(JTAG `mww`/`mdw`)에서의 차이를 말한다.

## 1. 문제

first silicon 에서 레지스터 R/W 테스트가 전부 초록색인데 드라이버가 CH1 을 설정하자 CH0 이 망가진다 — 원인은 **주소 디코더**다. "address decode 버그를 어떤 테스트로, 몇 번의 버스 접근으로 찾고, 어느 주소 비트인지 어떻게 짚나?" 는 Rivian·NVIDIA 류 실리콘 검증 면접의 단골 질문이다.

버스(APB/AXI)는 주소를 내보내고, IP 안의 **디코더**가 그 주소를 보고 레지스터 하나의 select 선을 켠다. 디코더가 틀리면 두 버스 주소가 **같은 플롭**을 고른다 = aliasing. 이번 레슨의 결함 3종:

| 결함 | 원인(실리콘에서) | mock 모델 (`fault_t`) |
|---|---|---|
| ① 주소 비트 A[k] 무시 | 디코더 비교기에서 비트 누락, 주소선 끊김(open)·0 고정 | `ignore_bit = k` → 디코드 전에 비트 k 를 0 으로 |
| ② 디코드 엔트리 하나 오류 | RTL 의 case 문/파라미터 오타, 레지스터 기술서(SIV-B01)의 오프셋 중복 | `remap_from/to` → from 주소가 to 레지스터를 선택 |
| ③ 상위 비트 미디코드(partial decode) | 슬롯은 1 KB 인데 디코더가 [8:2] 만 봄 → 블록이 +0x200 에 **mirror** | `ignore_bit = 9` |

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">VDMA: 버스 주소 A[9:2] → 디코더 → 레지스터 select. 결함은 디코더에만 넣는다</text>
  <rect x="10" y="50" width="110" height="60" rx="6" stroke="currentColor" class="f-mut"/>
  <text x="65" y="76" fill="currentColor" text-anchor="middle">CPU / JTAG</text>
  <text x="65" y="96" fill="currentColor" text-anchor="middle" font-size="12">reg_write32(off)</text>
  <g stroke="currentColor">
    <path d="M120 60 H250"/><path d="M120 70 H250"/><path d="M120 80 H250"/><path d="M120 90 H250"/><path d="M120 100 H250"/>
  </g>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="128" y="57">A9</text><text x="160" y="67">A8</text><text x="128" y="87">A6</text><text x="160" y="107">A2</text>
  </g>
  <path d="M190 76 l12 12 M202 76 l-12 12" stroke="currentColor" stroke-width="2"/>
  <text x="140" y="135" fill="currentColor" font-size="12">① A[6] 무시 (✕)</text>
  <rect x="250" y="45" width="150" height="160" rx="6" stroke="currentColor" class="f-acc"/>
  <text x="325" y="68" fill="currentColor" text-anchor="middle" font-weight="bold">디코더</text>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="262" y="92">0x000 → CTRL</text>
    <text x="262" y="110">0x040 → PRIO</text>
    <text x="262" y="128">0x08C → CH0_CCTRL</text>
    <text x="262" y="146">0x0CC → CH0_CCTRL</text>
    <text x="262" y="164">0x1xx → CH2/CH3</text>
    <text x="262" y="182">0x2xx → DECERR</text>
  </g>
  <rect x="258" y="134" width="136" height="17" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="250" y="225" fill="currentColor" font-size="12">② 엔트리 오류: 0x0CC 가 CH0_CCTRL 선택</text>
  <text x="250" y="243" fill="currentColor" font-size="12">③ A[9] 미디코드: 0x2xx 가 0x0xx 로 접힘</text>
  <g stroke="currentColor">
    <rect x="470" y="45" width="120" height="24" class="f-ok"/>
    <rect x="470" y="73" width="120" height="24" class="f-ok"/>
    <rect x="470" y="101" width="120" height="24" class="f-ok"/>
    <rect x="470" y="129" width="120" height="24" class="f-warn"/>
    <rect x="470" y="157" width="120" height="24" class="f-ok"/>
    <rect x="470" y="185" width="120" height="40" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="530" y="62">CTRL 플롭</text>
    <text x="530" y="90">PRIO 플롭</text>
    <text x="530" y="118">CH0_CCTRL 플롭</text>
    <text x="530" y="146">CH1_CCTRL (고립)</text>
    <text x="530" y="174">CH0..CH3 플롭</text>
    <text x="530" y="202">홀 0x200–0x3FC</text>
    <text x="530" y="218">응답 = DECERR</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M400 88 H468"/><path d="M400 106 L468 88" stroke-dasharray="4 3"/>
    <path d="M400 124 L468 113"/><path d="M400 142 L468 116" stroke-dasharray="4 3"/>
  </g>
  <text x="598" y="92" fill="currentColor" font-size="12">① 0x040→CTRL</text>
  <text x="598" y="120" fill="currentColor" font-size="12">② 두 주소, 한 플롭</text>
  <text x="10" y="275" fill="currentColor">aliasing = 서로 다른 버스 주소 두 개가 같은 플롭을 고른다.</text>
  <text x="10" y="295" fill="currentColor">"쓰고 바로 읽기" 는 방금 쓴 플롭을 읽어 통과 → 전부 쓰고, 그다음 전부 읽기.</text>
</svg>
```

대상 IP 는 가상 DMA 컨트롤러 **VDMA**. 인터커넥트가 1 KB 슬롯(0x000–0x3FC)을 줬고 레지스터는 0x000–0x1FC 에만 있다.

| off | 이름 | access | 쓰기/의미 마스크 | 리셋 |
|---|---|---|---|---|
| 0x000 | CTRL | RW | 0x00000107 | 0 |
| 0x004 | SCRATCH | RW | 0xFFFFFFFF | 0 |
| 0x008 | INTEN | RW | 0x0000000F | 0 |
| 0x00C | INTSTAT | **W1C** | 0x0000000F | (HW 가 0x5 세움) |
| 0x010 | CFG | RW | 0x0000FFFF | 0x10 |
| 0x014 | DBG_CNT | **RC** | 0x0000FFFF | (카운트 7) |
| 0x018 / 0x01C | ID / VERSION | RO | — | 0x56444D41 / 0x00010002 |
| 0x020 / 0x040 | TIMEOUT / PRIO | RW | 0xFFFF / 0xFF | 0xFFF / 0 |
| 0x080 + 0x40·n | CHn_SRC, _DST, _LEN, _CCTRL, _CSTAT | RW×4 + RO | full, full, 0xFFFFFF, **0x3**, — | 0 … CSTAT=1 |

RW 22개, RO 6개, W1C 1개, RC 1개, 홀 226워드. mock 은 SIV-B02 의 레지스터 파일(access type 시맨틱)을 축약해 넣었고, 결함은 디코더(`decode()`)에만 넣는다. 버스 에러(DECERR)는 읽기값 0xDEADBEEF + 카운터 — 실칩에선 abort/BusFault 핸들러가 세는 값이다.

### Part A — 고유 패턴 테스트와 walking-address

1. `addr_hash(off)` — 주소마다 다른 32비트 패턴. 전단사(bijection), `off=0` 도 0 이 아니고, 하위 비트까지 섞여야 한다.
2. `alias_unique_test(lg)` — 모든 RW 에 `addr_hash(off)` 를 **먼저 다 쓰고** 그다음 **전부 읽어** 비교. 두 번째 패스는 보수(`~hash`). RO 는 쓰지 않고 리셋값인지 확인. W1C/RC 는 쓰지도 읽지도 않는다.
3. `walk_addr_test()` — 앵커 0x000, 탐침 `1<<k` (k=2..8). 반환은 의심 주소 비트 마스크(A[6] 이면 0x040).

### Part B — 좁은 필드, 홀, 위험 레지스터

1. `alias_indexbits_test(lg)` — RW 순번 r=1..N, 패스 p 에서 `(r>>p)&1 ? 0xFFFFFFFF : 0`. 2비트 필드끼리의 aliasing 을 해시는 놓치지만(실제로 놓친다) 이 방식은 반드시 잡는다.
2. `hole_test(lg)` — 슬롯 안 모든 홀을 읽고(버스 에러가 나야 함) 쓴 뒤 레지스터가 그대로인지. A[9] 미디코드(mirror)는 이것만 잡는다.
3. 위 테스트 전부를 돌린 뒤에도 RC 읽기 0회, W1C·RO 쓰기 0회, INTSTAT pending 0x5 와 DBG_CNT 7 이 보존돼야 한다.

### Part C — VDMA 결함 주입과 원인 추정

`find_culprit(victim, &culprit)` 와 `diagnose()` 를 완성한다. 정상 실리콘 → PASS, A[2]..A[8] 무시 → `DX_ADDR_BIT` + 정확한 k, 디코드 엔트리 오류 → `DX_DECODE_ENTRY` + 두 주소, A[9] → `DX_MIRROR` bit 9. 리포트는 `FAIL reg=… off=… exp=… act=… diff=… bits=[…]` 와 한 줄 `DIAG` 로 끝난다.

> 레지스터 기술서·codegen 은 SIV-B01, access type mock 은 SIV-B02, 리셋값은 SIV-B03, 레지스터별 R/W·bit-bash 는 SIV-B04, **데이터** 버스 walking-1/0 은 SIV-B05, SRAM March 는 SIV-B07 이 맡는다. 여기서는 **주소** 쪽만 본다.

## 2. Problem (English)

> **Detect address-decode aliasing in a register block.**
>
> You get a register description `regs[]` (name, offset, access type RW/RO/W1C/RC, mask, reset) for a DMA IP that owns a 1 KiB slot; registers live in `0x000–0x1FC`, the rest are holes that must return a decode error. Accesses go through `reg_read32(off)` / `reg_write32(off, v)`.
>
> 1. `uint32_t addr_hash(uint32_t off)` — a bijective, well-mixed pattern. `int alias_unique_test(fail_log_t *lg)` — write every RW register with its pattern, *then* read all of them back (two passes: pattern and complement); check RO registers still hold reset. Never write RO/W1C, never read RC. `uint32_t walk_addr_test(void)` — anchor `0x000`, probes `1<<k` for k = 2..8; return a mask of suspect address bits.
> 2. `int alias_indexbits_test(fail_log_t *lg)` — ceil(log2(N+1)) passes writing all-ones/all-zeros by the bits of each register's rank 1..N; it must catch aliasing between two 2-bit fields whose hashes collide. `int hole_test(fail_log_t *lg)` — every hole must bus-error and must not disturb any register.
> 3. `dx_t diagnose(...)` — classify: single address bit k ignored, a single wrong decode entry (report both offsets), or the block mirrored because bit 9 is not decoded.
>
> Example: with bit 6 ignored, `walk_addr_test() == 0x040` and the diagnosis names `CTRL(0x000) <-> PRIO(0x040)`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/SIV-B06_st && /tmp/SIV-B06_st
```

시작 코드 실행 결과 (실측, 일부):

```text
FAIL starter.c:284  addr_hash(0x000u) != 0u && addr_hash(0x000u) != addr_hash(0x004u)
FAIL starter.c:293  alias_unique_test(&lg) > 0
FAIL starter.c:305  walk_addr_test() == (1u << k)
...
== VDMA fault: decoder ignores A[6] ==
  DIAG PASS: no aliasing, walk_mask=0x000, holes all DECERR
FAIL starter.c:343  d.kind == DX_ADDR_BIT && d.bit == k && (d.victim ^ d.culprit) == (1u << k)
...
8/33 passed
```

눈여겨볼 점: 스텁은 정상 실리콘 체크를 **전부 통과**하고, 결함 실리콘에도 `DIAG PASS` 를 찍는다. "항상 0 을 돌려주는 테스트" 가 검증에서 가장 위험한 테스트다 — 그래서 모든 체크를 **양방향**(정상은 통과, 주입 결함은 검출)으로 건다.

## 4. 힌트

<details>
<summary>힌트 1 — Part A: 왜 "전부 쓰고 나서 전부 읽기" 인가</summary>

A 와 B 가 한 플롭이면, A 에 쓰고 바로 A 를 읽으면 방금 쓴 값이 나온다. A 를 쓰고, **B 를 쓴 다음** A 를 읽어야 B 의 값이 보인다. 그래서 루프를 두 개로 나눈다: 쓰기 루프, 읽기 루프. 비교는 `((act ^ exp) & mask) == 0` — 마스크 밖 비트는 예약/RO 라 아무 값이나 읽힐 수 있다.

</details>

<details>
<summary>힌트 2 — Part A: walking-address 의 세 단계</summary>

0) 탐침 7개와 앵커에 PAT → 각 탐침이 PAT 인지. 1) 앵커만 ANTI → 탐침이 변하면 그 탐침 비트가 앵커로 접힌 것. 2) 탐침 k 만 ANTI → 앵커가 변하면 비트 k, 다른 탐침 j 가 변하면 j·k short. 레지스터마다 마스크가 달라 접힌 탐침은 "PAT" 이 아닌 값을 읽으므로, 1)·2) 는 **직전에 읽어 둔 값과** 비교해야 한다.

</details>

<details>
<summary>힌트 3 — Part B: 2비트 필드에 고유 패턴은 4개뿐</summary>

마스크 0x3 레지스터가 4개면 해시 하위 2비트가 겹칠 확률이 크다 (실제로 0x08C 와 0x0CC 둘 다 `hash & 3 == 2`). 값 하나로 구별하려 하지 말고, 순번의 **비트 하나씩** 패스를 나눠라. 두 순번이 다르면 어떤 비트 p 에서 다르다 → 패스 p 에서 한쪽은 전부 1, 다른 쪽은 전부 0.

</details>

<details>
<summary>힌트 4 — Part B: 홀에서 버스 에러가 났는지 어떻게 아나</summary>

읽기 전후로 `bus_err_count()` 를 비교한다. 실칩에서는 BusFault/abort 핸들러가 "예상된 폴트" 플래그를 보고 카운트만 올린 뒤 다음 명령으로 복귀하게 만든다. 홀에 쓴 값이 어느 레지스터로 새는지는 쓰기 후 RW/RO 전체 재확인으로 본다.

</details>

<details>
<summary>힌트 5 — Part C: 범인 찾기는 0 바탕에 1 하나</summary>

모든 RW 를 0 으로 깔고 victim 이 0 인지 확인(아니면 DECERR 등 → 판단 불가). 후보 W 마다 0xFFFFFFFF 를 쓰고 victim 을 읽고 W 를 다시 0 으로. victim 이 0 이 아니게 되는 첫 W 가 범인이다. walk_mask 가 비트 하나면 주소선, 0 인데 불일치가 있으면 엔트리 하나, 홀만 응답하면 mirror.

</details>

## 5. 풀이

### Part A — 고유 패턴 테스트와 walking-address

**즉시 write→read 는 aliasing 을 우연히만 잡는다.** `demo_fault_matrix.c` 로 결함마다 다섯 테스트의 불일치 개수를 실측했다 (`rw_each` = SIV-B04 식 레지스터별 PAT/ANTI 쓰고 바로 읽기):

```text
fault                         rw_each   unique  idxbits     walk     hole
good                                0        0        0    0x000        0
A[6] ignored                        2       16       16    0x040       20
A[2] ignored                        2       22       26    0x004       19
entry CH1_CCTRL->CH0_CCTRL          0        0        1    0x000        0
entry CH1_SRC->CH0_SRC              0        2        2    0x000        1
A[9] not decoded (mirror)           0        0        0    0x000       51
```

A[6] 에서 `rw_each` 의 2개는 PRIO(마스크 0xFF)가 CTRL 플롭(마스크 0x107)에 접혀 **마스크가 달라서** 생긴 불일치다. 같은 마스크끼리의 aliasing(CH1_SRC→CH0_SRC)은 0 — 즉 B04 테스트는 aliasing 테스트가 아니다.

```svg
<svg viewBox="0 0 740 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">결함 × 테스트 (demo_fault_matrix 실측: 불일치 수 / walk 의심 비트)</text>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="290" y="48">즉시 W/R</text><text x="380" y="48">해시 2패스</text><text x="470" y="48">index-bit</text>
    <text x="560" y="48">walking</text><text x="660" y="48">홀 검사</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="78">A[6] 무시 (주소선)</text>
    <text x="10" y="113">엔트리 CH1_CCTRL→CH0 (2비트)</text>
    <text x="10" y="148">엔트리 CH1_SRC→CH0 (32비트)</text>
    <text x="10" y="183">A[9] 미디코드 (mirror)</text>
  </g>
  <g stroke="currentColor">
    <rect x="245" y="60" width="90" height="28" class="f-mut"/><rect x="335" y="60" width="90" height="28" class="f-ok"/>
    <rect x="425" y="60" width="90" height="28" class="f-ok"/><rect x="515" y="60" width="90" height="28" class="f-ok"/>
    <rect x="605" y="60" width="110" height="28" class="f-ok"/>
    <rect x="245" y="95" width="90" height="28" class="f-warn"/><rect x="335" y="95" width="90" height="28" class="f-warn"/>
    <rect x="425" y="95" width="90" height="28" class="f-ok"/><rect x="515" y="95" width="90" height="28" class="f-warn"/>
    <rect x="605" y="95" width="110" height="28" class="f-warn"/>
    <rect x="245" y="130" width="90" height="28" class="f-warn"/><rect x="335" y="130" width="90" height="28" class="f-ok"/>
    <rect x="425" y="130" width="90" height="28" class="f-ok"/><rect x="515" y="130" width="90" height="28" class="f-warn"/>
    <rect x="605" y="130" width="110" height="28" class="f-ok"/>
    <rect x="245" y="165" width="90" height="28" class="f-warn"/><rect x="335" y="165" width="90" height="28" class="f-warn"/>
    <rect x="425" y="165" width="90" height="28" class="f-warn"/><rect x="515" y="165" width="90" height="28" class="f-warn"/>
    <rect x="605" y="165" width="110" height="28" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="290" y="79">2 (우연)</text><text x="380" y="79">16</text><text x="470" y="79">16</text><text x="560" y="79">0x040</text><text x="660" y="79">20</text>
    <text x="290" y="114">0</text><text x="380" y="114">0 (놓침)</text><text x="470" y="114">1</text><text x="560" y="114">0x000</text><text x="660" y="114">0</text>
    <text x="290" y="149">0</text><text x="380" y="149">2</text><text x="470" y="149">2</text><text x="560" y="149">0x000</text><text x="660" y="149">1</text>
    <text x="290" y="184">0</text><text x="380" y="184">0</text><text x="470" y="184">0</text><text x="560" y="184">0x000</text><text x="660" y="184">51</text>
  </g>
  <text x="10" y="218" fill="currentColor" font-size="12">혼자 전부 잡는 테스트는 없다. walking = 어느 주소선, 홀 검사 = 슬롯 밖 누수,</text>
  <text x="10" y="234" fill="currentColor" font-size="12">index-bit = 좁은 필드의 엔트리 오류 → 셋을 묶어 진단한다 (Part C).</text>
</svg>
```

**해시 패턴의 요구사항**

| 요구 | 이유 | `addr_hash` 에서 |
|---|---|---|
| 전단사 | 32비트 전부 쓸 수 있는 두 레지스터는 절대 같은 패턴을 받지 않는다 | 홀수 곱(mod 2^32 역원 존재) + xorshift 는 각각 전단사 |
| off=0 → 0 아님 | 패턴이 리셋값과 같으면 "쓰기가 안 먹힘" 이 안 보인다 | 먼저 `off ^ 0x5A5A5A5A` |
| 하위 비트까지 섞임 | 원시 주소는 하위 2비트가 항상 00 → 2비트 필드에 0 만 들어간다 | lowbias32 계열 섞기 (곱 2번, 시프트 3번) |
| 보수 패스 | 쓰기 가능한 모든 비트가 0 과 1 을 한 번씩 → 마스크 때문에 우연히 리셋값과 같아져도 다른 패스에서 드러남 | `addr_hash(off) ^ inv` |

RO 레지스터도 읽는다. A[3] 무시 결함에서 ID(0x018)는 CFG(0x010)로 접히는데, 해시를 쓰지 않는 RO 라도 **리셋값 비교**(SIV-B03)로 잡힌다 (`log_has(&lg, 0x018)` 체크).

**리포트 읽는 법.** A[6] 무시에서 실제로 나온 앞 줄들:

```text
  FAIL reg=CTRL off=0x000 exp=0x00000105 act=0x00000007 diff=0x00000102 bits=[1,8]
  FAIL reg=PRIO off=0x040 exp=0x0000005F act=0x00000007 diff=0x00000058 bits=[3,4,6]
  FAIL reg=CH0_SRC off=0x080 exp=0x7078422B act=0x7AC62CF7 diff=0x0ABE6EDC bits=[2,3,4,6,7,9,10,11,...]
```

CTRL 의 `act=0x007` 은 PRIO 의 패턴 `0x5F` 를 CTRL 마스크 0x107 로 자른 값이고, CH0_SRC 의 `act=0x7AC62CF7` 은 정확히 CH1_SRC(0x0C0)의 해시다. **act 가 다른 레지스터의 기대값과 같다 = 그 레지스터가 범인.** 0x080 ^ 0x0C0 = 0x040 → 비트 6. 고유 패턴을 쓰는 진짜 이유가 이것이다: 실패 값 자체가 "어디서 왔는지" 를 말해 준다.

**walking-address.** 메모리 테스트의 고전(Barr 의 address-bus test)을 레지스터 맵에 옮겼다. 주소 비트 k 를 혼자 1 로 만든 오프셋 `1<<k` 와 앵커 0x000 은 비트 k **하나만** 다르므로, 둘이 엮이면 범인은 A[k] 뿐이다. VDMA 맵은 이를 위해 2^k 자리에 RW 를 두었다: SCRATCH(0x004), INTEN(0x008), CFG(0x010), TIMEOUT(0x020), PRIO(0x040), CH0_SRC(0x080), CH2_SRC(0x100).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">walking-address: 앵커 1개 + 탐침 7개, 접근 수 = O(주소 비트 수²)</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="80" height="34" class="f-acc"/>
    <rect x="110" y="40" width="80" height="34" class="f-ok"/><rect x="195" y="40" width="80" height="34" class="f-ok"/>
    <rect x="280" y="40" width="80" height="34" class="f-ok"/><rect x="365" y="40" width="80" height="34" class="f-ok"/>
    <rect x="450" y="40" width="80" height="34" class="f-warn"/><rect x="535" y="40" width="80" height="34" class="f-ok"/>
    <rect x="620" y="40" width="80" height="34" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">
    <text x="50" y="62">0x000</text><text x="150" y="62">0x004</text><text x="235" y="62">0x008</text>
    <text x="320" y="62">0x010</text><text x="405" y="62">0x020</text><text x="490" y="62">0x040</text>
    <text x="575" y="62">0x080</text><text x="660" y="62">0x100</text>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="50" y="90">앵커</text><text x="150" y="90">A[2]</text><text x="235" y="90">A[3]</text><text x="320" y="90">A[4]</text>
    <text x="405" y="90">A[5]</text><text x="490" y="90">A[6]</text><text x="575" y="90">A[7]</text><text x="660" y="90">A[8]</text>
  </g>
  <path d="M490 74 C 470 120, 80 120, 55 76" stroke="currentColor" fill="none" stroke-dasharray="5 4"/>
  <path d="M55 76 l-2 10 l8 -6 z" fill="currentColor"/>
  <text x="200" y="128" fill="currentColor" font-size="12">A[6] 무시: 0x040 접근이 앵커 플롭으로 접힌다</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="160">0) 전부 PAT → 탐침이 PAT 가 아니면 그 비트 (기준값 base[k] 저장)</text>
    <text x="10" y="180">1) 앵커만 ANTI → 탐침 k 가 base[k] 에서 변하면 A[k]  (앵커 쪽으로 접힘)</text>
    <text x="10" y="200">2) 탐침 k 만 ANTI → 앵커가 변하면 A[k], 다른 탐침 j 가 변하면 A[j]·A[k] short → 복구</text>
    <text x="10" y="225">결과: bad 마스크. 결함 A[6] → 0x040 정확히 한 비트 (solution 의 7개 k 전부 확인)</text>
  </g>
</svg>
```

| 단계 | 바뀌면 안 되는 곳이 바뀜 | 결론 |
|---|---|---|
| 0 기본 | 탐침 k ≠ PAT (마스크 안) | A[k] 접힘, 또는 그 레지스터 자체 고장 |
| 1 앵커 ANTI | 탐침 k | A[k] 가 0 으로 보임 (탐침이 앵커로 접힘) |
| 2 탐침 k ANTI | 앵커 | A[k] (반대 방향 접힘 포함) |
| 2 탐침 k ANTI | 탐침 j | A[j]·A[k] short — 두 비트 모두 표시 |

**실제로 한 번 틀렸던 곳.** 처음엔 1)·2) 에서도 "탐침 == PAT" 로 비교했다. 그러자 모든 결함에서 `walk_mask=0x1FC`(전 비트 의심)가 나왔다 (실측 `DIAG unknown: walk_mask=0x1FC`). 접힌 탐침은 CTRL 플롭의 **0x107 마스크로 잘린** 값을 읽으므로 PAT 와 영원히 다르고, 매 단계마다 "short" 로 오판된 것이다. 고친 방법: 0) 에서 읽은 값을 `base[k]` 로 저장하고 이후엔 **변화**만 본다. 앵커도 매 반복 끝에 다시 PAT 로 쓴다 (탐침 복구 쓰기가 접혀 앵커를 다른 마스크 값으로 바꿨을 수 있음). 메모리처럼 셀 폭이 같으면 생기지 않는, 레지스터 맵 특유의 함정이다.

walking 은 접근 수가 작다: 탐침 7개면 쓰기 8 + 기본 읽기 8 + 단계1 9 + 단계2 7×10 = **95회** (접근 카운터를 단 사본으로 실측 `walk ops=95`). 한계는 2^k 자리에 RW 가 있어야 한다는 것 — 희소한 맵에서는 비트 k 만 다른 RW 쌍을 찾아 쓴다 (연습 1).

### Part B — 좁은 필드, 홀, 위험 레지스터

**해시가 놓치는 경우.** CH1_CCTRL(0x0CC) 디코드 엔트리가 CH0_CCTRL(0x08C)을 고르는 결함. 둘 다 마스크 0x3 이고 `addr_hash` 하위 2비트가 둘 다 2 다 (실측: `08C h=00000002`, `0CC h=00000002`). 어느 쪽을 나중에 쓰든 읽히는 값이 기대값과 같아 `alias_unique_test == 0` — 체크로 "놓친다" 를 고정해 두었다. 보수 패스도 둘 다 같이 뒤집히므로 소용없다.

`practice.c` 에서 "모든 패스에서 공통 마스크 비트가 같은 RW 쌍"(= 데이터로 구별 불가) 을 셌다 (RW 22개 → 231쌍):

| 패턴 방식 | 패스 | 구별 불가 쌍 |
|---|---|---|
| 원시 주소 `off` | 1 | 91 |
| `addr_hash` | 1 | 20 |
| `addr_hash` + 보수 | 2 | 20 |
| 순번값 `r` 그대로 | 1 | 17 |
| index-bit (순번의 비트별 전부 1/0) | 5 | **0** |

**index-bit 가 반드시 잡는 이유.** 순번 r_A ≠ r_B 면 어떤 비트 p 에서 다르다. 패스 p 에서 한쪽은 0xFFFFFFFF, 다른 쪽은 0 을 받는다. 둘이 한 플롭이면 나중 쓰기가 이기고, 먼저 쓴 쪽은 공통 마스크 비트 전체가 틀린다. 조건 두 가지: ① 순번을 **1부터** (0 이면 그 레지스터는 모든 패스에서 0 = 리셋값 → "안 써짐" 을 못 봄), ② 두 레지스터의 쓰기 가능 비트가 **겹쳐야** 한다 — 안 겹치면 데이터로는 원리적으로 구별 불가하고 RO/홀/버스 에러 쪽 증거가 필요하다. 패스 수는 ⌈log2(N+1)⌉ = 5 (N=22).

이건 메모리 테스트의 "address-in-data" 를 비트 평면으로 쪼갠 것이고, 같은 원리로 주소선 k 가 두 레지스터를 합쳐 버리면 패스 하나에서 반드시 드러난다.

**홀 검사.** 디코더는 "있는 레지스터를 고르는 것" 뿐 아니라 "없는 주소를 거절하는 것" 도 해야 한다. 정상 mock 에서 홀 226개 × (읽기 + 쓰기) = 버스 에러 452회 (`bus_err_count() == 2u * (SLOT_SIZE / 4u - NREGS)` 체크). A[9] 미디코드에서는 레지스터끼리의 테스트가 **전부 통과**하고 (`alias_unique_test == 0 && walk_addr_test() == 0`), 홀 검사만 51건을 낸다:

```text
  FAIL reg=<hole> off=0x200 exp=DECERR act=0x00000105 (somebody answered)
  FAIL reg=<hole> off=0x204 exp=DECERR act=0xA38E202D (somebody answered)
```

0x200 이 CTRL 의 해시 0x105 로, 0x204 가 SCRATCH 의 해시로 대답한다 = 블록 전체가 +0x200 에 mirror. 저가 MCU 의 부분 디코드(주변장치가 여러 주소에 반복 보임)와 같은 현상이고, SoC 에서는 이 IP 가 **옆 IP 의 슬롯을 먹는** 버그가 된다. 홀의 정답 동작은 스펙이 정한다: AXI 라면 DECERR/SLVERR 응답, 어떤 블록은 RAZ/WI(read-as-zero, write-ignored). RAZ/WI 블록이면 "읽기 == 0 이고 레지스터가 안 변함" 으로 바꿔 검사한다.

**위험 레지스터 보호.** 모든 테스트는 RW 에만 쓰고, RW·RO 만 읽는다.

| 레지스터 | 하면 안 되는 것 | 이유 |
|---|---|---|
| W1C (INTSTAT) | 패턴 쓰기 | pending 비트가 지워진다 → 이후 인터럽트 테스트(SIV-B11) 오염, 패턴 비교도 무의미 |
| RC (DBG_CNT) | 읽기 | 읽는 순간 0 → 카운터 손실. 읽기값이 HW 상태라 기대값도 없다 |
| RO | 쓰기 | 효과가 없어야 정상이지만 쓰기 자체가 "RO 에 쓰기 금지" 하드웨어 에러를 낼 수 있다 |

체크: 정상 mock 에서 네 테스트를 돌린 뒤 `g_rc_reads == 0`, `g_w1c_writes == 0`, `g_ro_writes == 0`, INTSTAT=0x5, DBG_CNT=7. 단 **결함이 있으면** 테스트가 부작용을 낼 수 있다: A[3] 무시에서 VERSION(0x01C) 읽기는 DBG_CNT(0x014)로 접혀 카운터를 지운다. 그래서 aliasing 테스트는 bring-up 초기(SIV-B10 의 ID 확인 직후, 기능 테스트 전)에 돌리고, 끝나면 IP 를 리셋한다.

### Part C — VDMA 결함 주입과 원인 추정

진단 규칙 (`diagnose`):

| walk_mask | 레지스터 불일치 | 홀 응답 | 판정 | 근거 |
|---|---|---|---|---|
| 0 | 0 | 0 | PASS | — |
| 비트 하나 k | 있음 | 있을 수 있음 | **ADDR_BIT k** | 비트 k 만 다른 쌍이 엮였다 = 그 주소선 |
| 0 | 있음 | — | **DECODE_ENTRY** | 주소선이 멀쩡한데 특정 쌍만 → 테이블 한 줄 |
| 0 | 0 | 있음 | **MIRROR** (홀 ^ 레지스터 = 한 비트) | 슬롯 안 레지스터끼리는 멀쩡, 바깥이 새어 들어옴 |
| 그 외 | | | UNKNOWN | 여러 비트·short — 사람이 본다 |

엔트리 오류의 두 주소 XOR 이 **한 비트**여도(0x08C ^ 0x0CC = 0x040) walking 이 통과하면 주소선이 아니다 — 주소선 결함이면 비트 6 만 다른 **모든** 쌍(CTRL/PRIO 포함)이 엮여야 하기 때문이다. 이 구분을 안 하면 RTL 팀에 "A[6] 배선" 이라는 틀린 버그를 낸다.

`find_culprit` 은 "0 바탕에 1 하나" 다: RW 전부 0 → victim 이 0 인지 확인 → 후보 W 에 0xFFFFFFFF → victim 이 0 이 아니면 W 가 범인 → W 를 0 으로 복구. victim 이 처음부터 0 이 아니면(DECERR 0xDEADBEEF, W1C 플롭에 접혀 pending 이 보임 등) 판단을 포기하고 다음 피해자로 넘어간다 — 틀린 범인을 대는 것보다 낫다.

solution 실행 결과 (실측, 발췌):

```text
== VDMA good silicon ==
  DIAG PASS: no aliasing, walk_mask=0x000, holes all DECERR
== VDMA fault: decoder ignores A[2] ==
  ... 67 more
  DIAG addr-bit A[2] (0x004): decoder does not see it; e.g. CTRL(0x000) <-> SCRATCH(0x004); walk_mask=0x004 fails=67 bus_errs=0
== VDMA fault: decoder ignores A[7] ==
  ... 93 more
  DIAG addr-bit A[7] (0x080): decoder does not see it; e.g. CTRL(0x000) <-> CH0_SRC(0x080); walk_mask=0x080 fails=93 bus_errs=44
== VDMA fault: decode entry CH1_CCTRL -> CH0_CCTRL ==
  FAIL reg=CH0_CCTRL off=0x08C exp=0x00000000 act=0x00000003 diff=0x00000003 bits=[0,1]
  DIAG decode-entry: CH0_CCTRL(0x08C) and CH1_CCTRL(0x0CC) select one register; address lines OK (walk_mask=0x000) fails=1
== VDMA fault: A[9] not decoded (mirror) ==
  FAIL reg=<hole> off=0x200 exp=DECERR act=0x00000105 (somebody answered)
  ...
  DIAG partial-decode: A[9] not decoded -> block mirrored at +0x200 (hole 0x200 hits CTRL 0x000)
33/33 passed
```

A[7] 의 `bus_errs=44` 도 정보다: CH1_DST(0x0C4) 등은 비트 7 을 잃으면 홀(0x044)로 가서 버스 에러가 난다. 버스 에러가 난 레지스터들이 모두 **비트 k = 1** 쪽이면 "A[k] 가 0 으로 보임(무시/0 고정)", 모두 **비트 k = 0** 쪽이면 "A[k] 가 1 로 고정" — 극성 추정은 연습 2.

**디버그 엔지니어에게 넘길 것.** 판정 한 줄 + 대표 쌍 + walk_mask + 첫 FAIL 몇 줄 + 버스 에러 수. 재현은 JTAG 로 두 줄이면 된다 (OpenOCD 의 `mww`/`mdw`): `mww <BASE+0x0CC> 0x3` 후 `mdw <BASE+0x08C>` 가 3 이면 재현. 칩 리비전·die ID·전압/온도 조건도 같이 남긴다 (SIV-B14).

**pre-silicon 과의 차이.** 시뮬레이션의 UVM RAL 은 **backdoor**(RTL 계층 경로로 플롭을 직접 peek)가 있어서, `uvm_reg_access_seq` 처럼 front-door 로 쓰고 backdoor 로 확인하는 시퀀스가 엉뚱한 플롭에 써지는 디코드 버그를 바로 잡는다. 실리콘에는 backdoor 가 없다 — 그래서 post-silicon 은 "전부 쓰고 전부 읽기 + 패턴 설계 + 홀 검사" 로 데이터만 보고 추론해야 한다. 기술서 단계(SystemRDL/IP-XACT)의 주소 겹침은 SIV-B01 의 lint 가, 버스 패브릭 쪽 슬롯 겹침은 SoC 주소 맵 검사가 먼저 걸러야 한다. 실리콘 단계별로는 ATE(테스터)가 스캔/BIST 로 구조 결함을, SLT·시스템 검증이 이런 기능 레벨 디코드 버그를 맡는다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/SIV-B06_so && /tmp/SIV-B06_so           # 33/33 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/SIV-B06_san && /tmp/SIV-B06_san   # 33/33, 진단 없음
cc -std=c11 -Wall -Wextra -O2 demo_fault_matrix.c -o /tmp/SIV-B06_dm && /tmp/SIV-B06_dm          # 5. 풀이의 매트릭스
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 레지스터마다 `write(x); read(x)` 로 끝 | 방금 쓴 플롭을 읽으므로 aliasing 통과. 마스크가 다를 때만 우연히 잡힘 (demo: 32비트 쌍 0건) | 쓰기 루프와 읽기 루프를 분리 |
| 패턴 = 원시 주소 `off` | 하위 2비트 항상 0, 구별 불가 91쌍 (해시 20쌍) | `addr_hash(off)` + 보수 패스 |
| 좁은 필드를 해시 하나로 끝 | 2비트 필드끼리 25% 확률로 충돌 → 0x08C/0x0CC 놓침 | index-bit 다중 패스 (순번 1..N) |
| walking 에서 접힌 탐침을 PAT 와 계속 비교 | 마스크가 다른 플롭을 읽어 매 단계 오판 → walk_mask 0x1FC | 0) 의 읽은 값을 기준으로 **변화**만 비교, 앵커 재기록 |
| 모든 레지스터에 패턴 쓰기 | W1C pending 소거, RO 쓰기 에러, RC 읽기로 카운터 손실 | access type 으로 걸러 RW 만 쓰고 RW·RO 만 읽기 |
| 슬롯 안 레지스터만 검사 | 상위 비트 미디코드(mirror)는 레지스터끼리 멀쩡해 보임 | 홀 전체 읽기(버스 에러 기대) + 쓰기 후 재확인 |
| XOR 가 한 비트면 곧바로 "주소선 k" | 엔트리 오류도 XOR 한 비트일 수 있다 (0x08C^0x0CC) | walking 결과와 교차 확인 |
| DECERR 피해자에게 범인 찾기 | 처음부터 0xDEADBEEF → 첫 후보를 범인으로 오판 | victim 이 0 바탕에서 0 이 아니면 판단 포기 |

## 8. 면접 Q&A

<details>
<summary>Q1. 레지스터 R/W 테스트가 전부 통과했는데 aliasing 이 남아 있을 수 있나요?</summary>

네. R/W·bit-bash 테스트는 레지스터 하나에 쓰고 바로 그것을 읽는다. 두 주소가 한 플롭이면 방금 쓴 값이 그대로 나와 통과한다. 이번 실측에서 32비트 레지스터끼리의 엔트리 오류(CH1_SRC→CH0_SRC)를 즉시 W/R 은 0건, 전부 쓰고 전부 읽기는 2건 잡았다. 꼬리질문 "그럼 bit-bash 는 왜 하나?" → stuck-at·RO/W1C 시맨틱 같은 **레지스터 내부** 결함용이고(SIV-B04/B05), aliasing 은 **레지스터 사이** 결함이라 테스트 구조가 다르다.

</details>

<details>
<summary>Q2. 패턴으로 그냥 주소값을 쓰면 안 되나요? 해시는 왜 쓰죠?</summary>

주소값은 워드 정렬이라 하위 2비트가 항상 0 이고, 이웃 주소끼리 비트가 거의 같다. 2비트 필드(채널 enable 등)에는 모두 0 이 들어가 서로 구별이 안 된다 (VDMA: 원시 주소 91쌍 vs 해시 20쌍 구별 불가). 해시는 전단사라 32비트 레지스터끼리는 절대 같지 않고, 실패 값(act)이 곧 범인 레지스터의 패턴이라 역추적이 된다. 꼬리질문 "해시도 2비트 필드에선 충돌하는데?" → 값 하나로는 4종류뿐이다. 순번의 비트별로 전부 1/0 을 쓰는 ⌈log2(N+1)⌉ 패스가 겹치는 마스크가 있는 한 모든 쌍을 구별한다.

</details>

<details>
<summary>Q3. walking-address 는 몇 번 접근하고 무엇을 못 잡나요?</summary>

주소 비트가 n 개면 앵커 + 탐침 n 개라 O(n) 쓰기와 O(n²) 읽기(단계 2 에서 다른 탐침 확인) — 레지스터 수 N 과 무관하다. 잡는 것: 주소선 하나가 0/1 로 보이거나 두 주소선이 short 된 것. 못 잡는 것: 특정 디코드 엔트리 하나의 오류(탐침이 아닌 주소), 슬롯 밖 비트(그건 홀 검사), 2^k 자리에 RW 가 없는 맵(쌍 탐색으로 대체). 꼬리질문 "short 면 결과가 어떻게 보이나?" → 탐침 k 를 바꿨을 때 탐침 j 가 변해서 bad 마스크에 두 비트가 같이 선다. 진단은 UNKNOWN 으로 두고 두 비트를 보고한다.

</details>

<details>
<summary>Q4. 홀(매핑 안 된 주소)을 읽으면 무엇이 나와야 하고, 버스 에러가 나는데 어떻게 테스트를 계속하나요?</summary>

스펙이 정한다. AXI 는 DECERR(인터커넥트에 슬레이브 없음)나 SLVERR(슬레이브가 거절), 일부 블록은 RAZ/WI. 버스 에러는 CPU 에서 abort/BusFault 가 되므로, 테스트 펌웨어는 "예상된 폴트" 플래그를 세운 뒤 접근하고, 핸들러가 플래그를 보면 카운트만 올리고 폴트 난 명령 다음으로 복귀한다. JTAG 로 접근하면 디버거가 에러 응답을 보고한다. 꼬리질문 "홀 검사로만 잡히는 버그는?" → 상위 주소 비트 미디코드. 레지스터끼리는 멀쩡하고 블록이 +0x200 에 거울처럼 나타난다 (실측: 레지스터 테스트 0건, 홀 51건).

</details>

<details>
<summary>Q5. RC·W1C 레지스터가 있는 블록에서 aliasing 테스트는 어떻게 하나요?</summary>

쓰기는 RW 에만, 읽기는 RW·RO 에만 한다. W1C 에 패턴을 쓰면 pending 이 지워지고, RC 를 읽으면 카운터/FIFO 가 소모된다. 그 레지스터들의 디코드는 간접적으로 본다: 다른 주소에 쓴 값이 그쪽으로 새면 RW 쪽 불일치로 보이고, 홀 검사의 재확인에서도 보인다. 꼬리질문 "결함 때문에 RO 를 읽었는데 실제로는 RC 가 읽혔다면?" → 테스트가 부작용을 낸다 (A[3] 무시에서 VERSION 읽기가 DBG_CNT 를 지움). 그래서 aliasing 테스트는 bring-up 초반에 돌리고, 끝나면 IP 리셋 후 다음 테스트로 넘어간다.

</details>

<details>
<summary>Q6. 결함을 찾았을 때 RTL/설계 팀에 무엇을 보고하나요?</summary>

① 판정(주소선 A[k] / 디코드 엔트리 / 미디코드) ② 대표 쌍과 XOR(예: CTRL 0x000 ↔ PRIO 0x040, 0x040) ③ walk_mask 와 불일치 수, 버스 에러 수(극성 단서) ④ FAIL 줄 몇 개 (exp/act/diff/bits) ⑤ 두 줄짜리 JTAG 재현 (`mww` → `mdw`) ⑥ 칩 리비전·die ID·전압/온도·클럭 조건. 꼬리질문 "A[k] 가 0 고정인지 무시인지는?" → 짝이 홀인 레지스터에서 버스 에러가 나므로, 에러 난 레지스터들이 모두 비트 k=1 쪽이면 0 으로 보임, 모두 0 쪽이면 1 고정 (연습 2).

</details>

<details>
<summary>Q7. 이런 버그는 pre-silicon 에서 왜 안 잡혔을까요? 어디서 잡아야 하나요?</summary>

시뮬레이션의 UVM RAL 은 backdoor 로 실제 플롭을 볼 수 있어 front-door 쓰기 + backdoor 확인으로 디코드 오류를 잡는다. 그러나 레지스터 모델과 RTL 을 같은 잘못된 기술서에서 생성하면 둘이 같이 틀려 통과한다 — 그래서 기술서 lint(겹침·폭 초과, SIV-B01)가 첫 방어선이다. 또 게이트 레벨/배선 결함, 상위 비트를 자르는 인터커넥트 설정, 특정 전압에서만 나는 문제는 실리콘에서 처음 보인다. 꼬리질문 "그럼 post-silicon 에선 backdoor 대신?" → 데이터만으로 추론: 전부 쓰고 전부 읽기, 패턴 설계, 홀 검사, 그리고 스캔 체인 덤프 같은 DFT 수단이 있으면 보조로 쓴다.

</details>

## 9. 추가 연습

### 연습 1 — 희소 맵에서 비트 k 의 RW 짝 찾기

2^k 자리에 RW 가 없을 수 있다. `find_bit_pair(k, &a, &b)` 로 비트 k 만 다르고(a 의 비트 k = 0), 둘 다 RW 이고, 마스크가 겹치는 쌍을 찾고, `pair_coverage(2, 9)` 로 쌍이 있는 비트들의 마스크를 구하라.

<details>
<summary>답 보기</summary>

기술서를 순회해 RW 인 `off` 중 비트 k 가 0 인 것마다 `off | 1<<k` 를 찾는다. VDMA: 비트 5 → (0x000, 0x020), 비트 7 → (0x000, 0x080), 비트 9 → 없음 (0x200 이상엔 레지스터가 없다 = 홀 검사의 몫). 실측 `coverage A[9:2] = 0x1FC`. 커버되지 않는 비트가 있으면 테스트 플랜에 "A[k] 는 홀 검사/버스 에러로만 확인" 이라고 적는다.

</details>

### 연습 2 — 결함 극성: 무시(0) vs 1 고정

디코더 결함에 `stuck1_bit` 를 더하고, RW·RO 를 하나씩 읽어 버스 에러가 난 레지스터들을 모은 뒤 `guess_polarity(k, offs, n)` 이 0(0 으로 보임) / 1(1 고정) / -1(판단 불가)을 내게 하라.

<details>
<summary>답 보기</summary>

A[k] 가 0 으로 보이면 비트 k=1 인 레지스터가 짝(비트 k=0) 주소로 가고, 그 짝이 홀이면 버스 에러 → 피해자는 전부 비트 k=1. 1 고정이면 반대. 실측: `ignore A[7]: 4 bus-error regs, first 0x0C4` → 0, `stuck1 A[7]: 13 bus-error regs, first 0x018` → 1. A[5] 무시는 TIMEOUT 의 짝 CTRL 이 실재해서 버스 에러가 0 → -1 (데이터만으로는 극성을 모른다고 정직하게 보고).

</details>

### 연습 3 — 패턴 방식별 구별 불가 쌍 세기

패스 함수 `fn(off, rank, pass)` 와 패스 수를 받아 "모든 패스에서 공통 마스크 비트가 같은 RW 쌍" 을 세는 `count_indistinct_multi` 를 작성하고 원시 주소 / 해시 / 해시+보수 / 순번값 / index-bit 을 비교하라.

<details>
<summary>답 보기</summary>

실측 `indistinct pairs: raw=91 hash=20 hash+inv=20 rank=17 indexbits(5 passes)=0`. 보수 패스는 두 레지스터를 **같이** 뒤집으므로 구별력을 늘리지 않는다 (0↔1 커버리지용). 순번값을 그대로 쓰면 2비트 마스크에서 r mod 4 가 겹친다. index-bit 는 순번이 다르면 어느 패스에서 전부-1 vs 전부-0 이라 공통 비트가 하나라도 있으면 구별된다.

</details>

### 연습 4 — 버스 접근 비용

RW N개, RO R개일 때 해시 2패스 = 2(2N+R), index-bit = 2N·⌈log2(N+1)⌉, 쌍마다(w,w,r,r) 하는 전수 비교 = 2N(N−1). 공식을 함수로 쓰고, mock 에 접근 카운터를 달아 앞의 두 공식과 계측값이 같은지 확인하라.

<details>
<summary>답 보기</summary>

실측 `ops N=22: unique=100 indexbits=220 pairwise=924`, `ops N=4096: unique=16384 indexbits=106496 pairwise=33546240`. 수천 개 레지스터(GPU·NIC 급)에서 전수 비교는 JTAG 로 수십 분이 걸릴 수 있어(접근당 수 µs~수십 µs 가정) 쓰지 않는다. 해시 2패스로 빠르게 훑고, index-bit 로 좁은 필드를 보강하고, 실패한 피해자에 대해서만 `find_culprit`(O(N)) 을 돈다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/SIV-B06_pr && /tmp/SIV-B06_pr   # 14/14 passed
```

## 10. 핵심 정리

- aliasing = 두 버스 주소가 한 플롭. 레지스터별 write→read 는 못 잡는다 → **전부 쓰고, 그다음 전부 읽기**.
- 패턴 = 전단사 주소 해시 + 보수 패스. 실패 값(act)이 범인 레지스터의 패턴이라 역추적이 된다.
- walking-address: 앵커 0x000 + 탐침 2^k. 접힌 탐침은 다른 마스크로 읽히므로 **기준값 대비 변화**로 판정 → 의심 주소 비트 마스크.
- 좁은 필드는 해시가 충돌한다 → 순번(1..N)의 비트별 전부-1/전부-0, ⌈log2(N+1)⌉ 패스면 모든 쌍 구별 (마스크가 겹치는 한).
- 홀 검사: 모든 홀은 버스 에러(또는 스펙상 RAZ/WI), 쓰기 후 레지스터 불변. mirror(상위 비트 미디코드)는 이것만 잡는다.
- RW 에만 쓰고 RW·RO 만 읽는다 (W1C 소거·RC 소모 금지). 결함이 있으면 테스트가 부작용을 낼 수 있으니 bring-up 초기에 돌리고 리셋.
- 진단: walk 비트 하나 → 주소선, walk 0 + 불일치 → 디코드 엔트리, 홀만 → mirror. 보고는 쌍·XOR·walk_mask·버스 에러 수·JTAG 재현.

## 참고자료

- Michael Barr, "Software-Based Memory Testing" (Embedded Systems Programming, 2000) — data bus / address bus / device test, walking 1s 와 power-of-two 주소 테스트의 고전 설명
- Ad J. van de Goor, *Testing Semiconductor Memories: Theory and Practice* (Wiley, 1991) — address decoder fault(AF) 모델과 March 테스트 (SIV-B07)
- [Accellera UVM 1.2 Class Reference — `uvm_reg_hw_reset_seq`, `uvm_reg_bit_bash_seq`, `uvm_reg_access_seq`, `uvm_mem_walk_seq`](https://www.accellera.org/downloads/standards/uvm)
- [Accellera SystemRDL 2.0](https://www.accellera.org/downloads/standards/systemrdl) · IEEE 1685 (IP-XACT) — 레지스터 기술서 단일 소스
- [Arm AMBA AXI Protocol Specification — OKAY/EXOKAY/SLVERR/DECERR 응답](https://developer.arm.com/documentation/ihi0022/latest)
- [OpenOCD User's Guide — `mdw` / `mww` 메모리 접근 명령](https://openocd.org/doc/html/General-Commands.html)
- [Chris Wellons, "Prospecting for Hash Functions" (lowbias32)](https://nullprogram.com/blog/2018/07/31/)
- 관련 레슨: SIV-B01 (기술서·lint), SIV-B02 (access type mock), SIV-B03 (리셋값), SIV-B04 (R/W·access type), SIV-B05 (walking 1s/0s·데이터 버스), SIV-B07 (March C-), SIV-B10 (bring-up 순서), SIV-B11 (W1C 인터럽트), SIV-B14 (리포트), C-M01 (MMIO 레지스터 구조체), C-M02 (W1C·RMW)
