---
id: ARCH-M15
time: 90분
updated: 2026-11-11
---
# ARCH-M15 MPU 영역 계산기와 접근 검사

## 학습 목표

- ARMv7-M PMSAv7 MPU 의 영역 규칙(크기 = 2^(SIZE+1), 최소 32 B, base 는 크기의 배수, 8 서브리전 + SRD, AP 3비트, XN)을 C 검사기로 옮기고, 잘못된 설정(SIZE<4, 정렬 안 된 base, 작은 영역의 SRD, AP=100)을 **설정 단계에서** 거른다.
- "영역이 겹치면 누가 이기나 → **번호가 큰 영역**", "서브리전을 끄면 그 주소는 **아래 영역이나 기본 맵으로 떨어진다**", "어느 영역에도 안 걸리면 특권+PRIVDEFENA 만 기본 맵" 을 테스트로 보여 줄 수 있다.
- 여러 바이트 접근(STRD, LDM, 비정렬 LDR)이 영역 경계를 걸칠 때를 판정하고, **판정은 32 B 그래뉼 안에서 일정하다** 는 사실로 바이트마다 대신 그래뉼마다 검사한다. 독립 참조 모델과 20만 접근을 비교한다.
- 임의 버퍼 `[start, start+len)` 를 영역 1개로 덮는 **영역 계산기** 를 짜고(가장 작은 정렬 블록 + 서브리전으로 넘침 줄이기, 128 B 이하에선 256 B + SRD 가 더 나은 경우), RBAR/RASR 값으로 인코딩한다.
- RTOS 태스크 구성(Flash RX-only, SRAM XN, 주변장치 창, 32 B 스택 가드)에서 MemManage 보고(IACCVIOL/DACCVIOL, MMARVALID, MMFAR)를 예측하고, ARMv8-M PMSAv8(base/limit, 겹치면 폴트)과 대비해 설명한다.

## 1. 문제

"MPU 영역이 겹치면 누가 이기죠?", "6 KB 스택을 MPU 로 어떻게 지키나요?", "스택 가드 32 B 면 충분한가요?" — RTOS·안전 펌웨어(FreeRTOS-MPU, Zephyr userspace, ISO 26262/DO-178 프로젝트) 면접에서 MPU 는 단골이고, 규칙이 **비직관적**(2^n 크기, 정렬, 번호 우선, 서브리전)이라 손으로 계산해 보지 않으면 틀린다.

MPU(Memory Protection Unit)는 MMU 와 달리 **주소 변환이 없다.** 주소를 그대로 두고 "이 주소를, 이 특권 수준으로, 이 종류(읽기/쓰기/실행)로 접근해도 되나" 만 판정한다. ARMv7-M(Cortex-M3/M4/M7)은 보통 8개(M7 은 16개 옵션) 영역을 갖고, 영역마다 비교기가 병렬로 붙어 있어 매 접근을 한 사이클 안에 판정한다. 이 레슨은 그 판정기를 C 로 만든다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">영역 겹쳐 쌓기 — 위(번호 큰 것)부터 보고, 처음 걸린 영역이 판정 (비특권 태스크 기준)</text>
  <g font-size="12" fill="currentColor">
    <text x="10" y="70">R7 가드</text><text x="10" y="105">R4 공유표</text>
    <text x="10" y="140">R3 태스크</text><text x="10" y="175">R1 SRAM</text>
    <text x="10" y="230" font-weight="bold">유효 권한</text>
  </g>
  <rect x="135" y="56" width="6" height="22" class="f-warn" stroke="currentColor"/>
  <text x="148" y="71" fill="currentColor" font-size="12">32 B AP=000 (그림은 확대)</text>
  <rect x="610" y="91" width="25" height="22" class="f-acc" stroke="currentColor"/>
  <text x="520" y="106" fill="currentColor" font-size="12">AP=010 →</text>
  <g stroke="currentColor">
    <rect x="135" y="126" width="25" height="22" class="f-ok"/><rect x="160" y="126" width="25" height="22" class="f-ok"/>
    <rect x="185" y="126" width="25" height="22" class="f-ok"/><rect x="210" y="126" width="25" height="22" class="f-ok"/>
    <rect x="235" y="126" width="25" height="22" class="f-ok"/><rect x="260" y="126" width="25" height="22" class="f-ok"/>
    <rect x="285" y="126" width="25" height="22" fill="none" stroke-dasharray="4 3"/>
    <rect x="310" y="126" width="25" height="22" fill="none" stroke-dasharray="4 3"/>
  </g>
  <text x="345" y="141" fill="currentColor" font-size="12">8 KB, AP=011, SRD=0xC0 (6·7 번 끔 → 아래로 떨어짐)</text>
  <rect x="110" y="161" width="600" height="22" class="f-mut" stroke="currentColor"/>
  <text x="300" y="176" fill="currentColor" font-size="12">128 KB, AP=001 (특권 RW / 비특권 금지)</text>
  <g stroke="currentColor">
    <rect x="110" y="216" width="25" height="22" class="f-mut"/>
    <rect x="135" y="216" width="6" height="22" class="f-warn"/>
    <rect x="141" y="216" width="144" height="22" class="f-ok"/>
    <rect x="285" y="216" width="325" height="22" class="f-mut"/>
    <rect x="610" y="216" width="25" height="22" class="f-acc"/>
    <rect x="635" y="216" width="75" height="22" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="190" y="232">RW</text><text x="420" y="232">금지 (R1)</text><text x="614" y="232">RO</text>
  </g>
  <g stroke="currentColor"><path d="M110 248 V256 M135 248 V256 M285 248 V256 M610 248 V256 M710 248 V256"/></g>
  <g font-family="monospace" font-size="12" fill="currentColor" text-anchor="middle">
    <text x="110" y="272">3000</text><text x="140" y="290">4000</text><text x="285" y="272">5800</text>
    <text x="610" y="272">8000</text><text x="705" y="272">9000</text>
  </g>
  <text x="380" y="292" fill="currentColor" font-size="12">주소 0x2000_xxxx (가로 1 KB = 25 px)</text>
</svg>
```

모델 규칙 (ARMv7-M ARM PMSAv7 + Cortex-M4 Generic User Guide 에서):

| 항목 | 규칙 | 모델에서 |
|---|---|---|
| 크기 | RASR.SIZE[5:1], 크기 = 2^(SIZE+1). SIZE=4 → 32 B(최소), 31 → 4 GB | SIZE<4 는 `CFG_SIZE_BAD` |
| base | 크기의 배수여야 함(소프트웨어 책임) | 아니면 `CFG_BASE_ALIGN` |
| 서브리전 | 256 B 이상 영역 = 8 등분, SRD bit k = 1 → k 번째 조각은 이 영역이 아님 | 128 B 이하에 SRD≠0 → `CFG_SRD_SMALL` |
| AP | 3비트 특권/비특권 표 (§5), 100 은 예약 | 예약 → `CFG_AP_RESERVED`, 검사기는 금지로 |
| XN | 1 이면 명령 fetch 금지 | 실행 = XN=0 **그리고** 읽기 허용 |
| 겹침 | 번호 큰 영역의 속성 | 7 → 0 첫 히트 |
| 미스 | 특권 && PRIVDEFENA → 기본 맵, 아니면 폴트 | `F_NOMATCH` |
| PPB | 0xE000_0000–0xE00F_FFFF 는 MPU 와 무관 | 비특권 → BusFault |

이미 아는 것 (다시 가르치지 않는다): 메모리 맵·주소 디코더·BusFault/BFAR 와 기본 맵 XN 구역 (ARCH-B11 — 거기 연습 2 가 기본 맵 분류기), 비트 필드 GET/SET (C-B04), DMA 와 캐시 속성 (ARCH-M12). 실제 코어에서 MPU 를 켜고 폴트를 잡는 것은 **BM-A08**, 예외 스택 프레임은 **ARM-M09** 의 몫이다.

### Part A — 영역 1개와 바이트 1개 판정

- `region_validate(r)` — 꺼진 영역은 OK. 순서대로 SIZE(4..31) → base 정렬 → 작은 영역 SRD → AP 예약.
- `ap_allows(ap, priv, write)` — AP 3비트 표. 8 × 4 경우 전수 테스트.
- `region_hit(r, addr)` — 켜짐 + `[base, base+size)` + 서브리전. 4 GB 영역과 주소 공간 맨 끝 32 B 영역에서도 맞아야 한다.
- `mpu_check(m, addr, acc, priv, hf_nmi)` — 바이트 1개 판정 → `mm_report_t` (허용/이유/영역 번호/MemManage 비트/MMFAR).

### Part B — 겹침, 구멍, 기본 맵, 걸침

- 겹침: 큰 RW 영역 위에 작은 RO 영역 — 번호를 바꾸면 결과가 뒤집힌다(크기가 아니라 번호).
- 서브리전 구멍: 꺼진 조각은 아래 영역으로, 아래가 없으면 PRIVDEFENA 규칙으로.
- MPU 꺼짐 / HardFault 안에서 HFNMIENA=0 / PPB / 기본 맵 XN.
- `mpu_access(m, addr, size, ...)` — 여러 바이트 접근. 첫 폴트 바이트를 보고. 32 B 그래뉼 단위로 검사.
- 독립 참조 모델(영역 0→7 마지막 히트 + 바이트마다)과 무작위 2000 구성 × 100 접근 비교.

### Part C — 계산기, 레지스터, RTOS 구성

- `fit_region / mpu_calc(start, len, &r, &over)` — 버퍼를 덮는 영역 1개, 넘침 최소. 무작위 2만 버퍼에서 무식한 최솟값과 일치.
- `mpu_encode / mpu_decode` — RBAR(VALID+REGION) / RASR 값. 1만 개 왕복.
- RTOS 구성: R0 Flash 512 KB RO/RO 실행 가능, R1 SRAM 128 KB 특권 전용 XN, R2 주변장치 512 MB 특권 전용 XN, R3 태스크 6 KB(계산기 결과), R4 공유 표 RW/RO, R5 태스크 UART 창, R7 32 B 스택 가드. 스택 넘침, STRD 걸침, 큰 프레임의 가드 건너뛰기, SRAM 실행, 창 밖 주변장치, HardFault 덤프, PPB 접근을 검사.

## 2. Problem (English)

> **ARMv7-M PMSAv7 MPU region calculator and access checker.**
>
> An MPU has 8 regions `{enable, base, SIZE, SRD, AP[2:0], XN, TEX/S/C/B}` and control bits `ENABLE, HFNMIENA, PRIVDEFENA`. Region size is `2^(SIZE+1)` bytes (SIZE 4..31, i.e. 32 B .. 4 GB); base must be a multiple of the size; regions of 256 B or more are split into 8 equal subregions and SRD bit k removes subregion k from the region.
>
> 1. Implement `region_validate`, `ap_allows(ap, priv, write)` (AP table below), `region_hit(r, addr)` and `mpu_check(m, addr, acc, priv, hf_nmi)` for a single byte. When several regions match, the **highest-numbered** one decides. An instruction fetch needs read permission and XN=0. With no match: privileged + PRIVDEFENA → default memory map, otherwise fault. MPU disabled, or `hf_nmi && !HFNMIENA` → default map. The PPB (0xE000_0000–0xE00F_FFFF) always uses the default map; unprivileged PPB access is a BusFault.
> 2. Implement `mpu_access(m, addr, size, ...)` for multi-byte accesses: report the first faulting byte (as MMFAR) or the first byte's report if all bytes are allowed. Do it in O(size/32).
> 3. Implement `mpu_calc(start, len, &region, &over)`: one region covering the buffer with minimum over-coverage, using subregions; and `mpu_encode/mpu_decode` for RBAR/RASR.
>
> AP (priv/unpriv): `000 --/--, 001 RW/--, 010 RW/RO, 011 RW/RW, 100 reserved, 101 RO/--, 110 RO/RO, 111 RO/RO`.
>
> Example: `mpu_calc(0x2000_7000, 0x2000)` → base `0x2000_0000`, SIZE 15 (64 KB), SRD `0xE7`, over `0x2000`. An unprivileged 8-byte store at `0x2000_57FC` where `[0x2000_5800, …)` is a disabled subregion over a privileged-only SRAM region → MemManage DACCVIOL, MMFAR = `0x2000_5800`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/ARCH/ARCH-M15
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있다
mkdir -p /tmp/esn_ARCH-M15
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/esn_ARCH-M15/st && /tmp/esn_ARCH-M15/st; echo "exit $?"
```

실제 출력 (앞뒤만):

```text
FAIL starter.c:295  region_validate(&(region_t){ .en = true, .base = 0x20000000u, .size_f = 3, .ap = 3 }) == CFG_SIZE_BAD
FAIL starter.c:296  region_validate(&(region_t){ .en = true, .base = 0x20000010u, .size_f = 4, .ap = 3 }) == CFG_BASE_ALIGN
FAIL starter.c:297  region_validate(&(region_t){ .en = true, .base = 0x20000100u, .size_f = 9, .ap = 3 }) == CFG_BASE_ALIGN
...
FAIL starter.c:551  mpu_check(&m, 0x20004010u, ACC_R, true, true).why == F_AP
FAIL starter.c:553  mpu_check(&m, 0xE000ED28u, ACC_R, false, false).why == F_PPB_UNPRIV && mpu_check(&m, 0xE000ED28u, ACC_R, true, false).why == OK_PPB
13/87 passed
exit 1
```

TODO 는 9개: `region_validate`, `ap_allows`, `region_hit`, `mpu_check` (Part A), `mpu_access` (Part B), `fit_region`, `mpu_calc`, `mpu_encode`, `mpu_decode` (Part C). 지나가는 13개는 "꺼진 영역은 OK" 처럼 스텁이 우연히 맞히는 것들이다. `rep_ok / rep_fault`(보고 만들기), `default_xn / in_ppb`, `enabled_bytes`, 참조 모델은 주어진다.

## 4. 힌트

<details>
<summary>힌트 1 — SIZE 필드는 log2(크기) − 1</summary>

32 B = 2^5 → SIZE 4, 1 KB = 2^10 → SIZE 9, 4 GB = 2^32 → SIZE 31. 크기를 `uint64_t` 로 (`(uint64_t)1 << (SIZE+1)`) 계산해야 4 GB 에서 `1u << 32` UB 를 피한다. 정렬 검사는 `base & (size − 1)`.

</details>

<details>
<summary>힌트 2 — 포함 검사와 서브리전 번호</summary>

ARCH-B11 과 같은 `off = addr − base` (부호 없는 32비트) 한 번, `off < size` 는 64비트로 비교. 서브리전 크기 = size/8 = 2^(SIZE+1−3) 이므로 번호는 `off >> (SIZE − 2)`. 128 B 이하에는 서브리전이 없다.

</details>

<details>
<summary>힌트 3 — "번호 큰 영역이 이긴다" 를 코드로</summary>

`for (i = 7; i >= 0; i--)` 에서 **처음** 걸린 영역에서 멈춘다. 걸렸으면 그 영역이 허용하든 거부하든 끝 — 아래 영역을 더 보지 않는다(거부도 이긴다). 서브리전이 꺼진 조각은 "안 걸림" 이라 다음 영역으로 넘어간다.

</details>

<details>
<summary>힌트 4 — 판정 순서</summary>

PPB → (MPU 꺼짐 또는 HardFault/NMI && !HFNMIENA) → 영역 7..0 → 미스 처리. 실행 접근은 XN 을 먼저 보고(→ `F_XN`), 그다음 읽기 권한(→ `F_AP`). 기본 맵을 쓸 때도 실행이면 `default_xn()` 을 본다.

</details>

<details>
<summary>힌트 5 — 왜 32 B 마다 한 번이면 되나</summary>

검증 통과한 설정에서 영역 경계(base, base+size), 서브리전 경계(size/8 ≥ 32), 기본 맵·PPB 경계는 모두 32 의 배수다. 그러니 32 B 정렬 블록 안의 바이트는 모두 같은 판정을 받는다. 첫 바이트, 그다음 `(addr | 31) + 1` 부터 32 씩.

</details>

<details>
<summary>힌트 6 — 계산기: 가장 작은 덮는 블록이 (거의) 최적</summary>

SIZE 를 4 부터 올리며 `base = start & ~(size−1)`, `base + size ≥ end` 인 첫 블록. 256 B 이상이면 버퍼와 안 겹치는 서브리전을 끈다. 더 큰 블록은 서브리전 격자가 더 거칠어서 넘침이 줄 수 없다. 예외: 그 블록이 128 B 이하면 서브리전이 없으니 256 B + 32 B 서브리전과 비교.

</details>

## 5. 풀이

### Part A — SIZE, AP, 히트

**SIZE 표.** 하드웨어는 "주소 상위 `32 − N` 비트 == base 상위 비트" 비교기 하나로 영역을 판정한다(N = log2 크기). 그래서 크기는 2^N, base 는 2^N 의 배수여야 한다 — 범위 비교기(덧셈기 두 개)보다 훨씬 싸고 빠르다. ARCH-B11 의 디코더 비교기와 같은 이유다.

| SIZE | 크기 | 대표 용도 |
|---|---|---|
| 4 | 32 B | 스택 가드, NULL 근처 막기 |
| 9 | 1 KB | 주변장치 블록 1개 |
| 12 | 8 KB | 태스크 스택 |
| 18 | 512 KB | Flash |
| 28 | 512 MB | 주변장치 영역 전체 |
| 31 | 4 GB | 배경(background) 영역 |

**AP 표** (특권 / 비특권). 외우는 법: bit2 = "특권도 읽기 전용", 아래 두 비트 = 비특권에게 얼마나 열어 주나.

| AP | 특권 | 비특권 | 용도 |
|---|---|---|---|
| 000 | 금지 | 금지 | 스택 가드, 막아 둔 구멍 |
| 001 | RW | 금지 | 커널 데이터, 주변장치 |
| 010 | RW | RO | 커널이 쓰고 태스크가 읽는 표 |
| 011 | RW | RW | 태스크 스택·데이터 |
| 100 | — | — | 예약 (UNPREDICTABLE) |
| 101 | RO | 금지 | 커널 상수 |
| 110 | RO | RO | Flash 코드·상수 |
| 111 | RO | RO | 110 과 같음 (ARMv7-M) |

**히트.**

```c
uint32_t off = addr - r->base;                 /* addr < base 면 감겨서 큰 수 → 자동 탈락 */
if ((uint64_t)off >= sz) return false;         /* 4 GB 영역: off ≤ 0xFFFF_FFFF < 2^32 → 항상 안 */
if (sz >= 256 && (r->srd & (1u << (off >> (r->size_f - 2))))) return false;
```

### Part B — 판정 흐름과 걸침

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m15-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">mpu_check — 바이트 1개 (addr, R/W/X, 특권?, HardFault/NMI?)</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="150" height="44" rx="6" class="f-mut"/>
    <rect x="190" y="40" width="190" height="44" rx="6" class="f-mut"/>
    <rect x="410" y="40" width="150" height="44" rx="6" class="f-mut"/>
    <rect x="410" y="140" width="150" height="44" rx="6" class="f-mut"/>
    <rect x="190" y="140" width="190" height="44" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="85" y="58">① PPB?</text><text x="85" y="75">0xE000_0000–0xE00F_FFFF</text>
    <text x="285" y="58">② MPU 꺼짐, 또는</text><text x="285" y="75">HF/NMI &amp;&amp; !HFNMIENA ?</text>
    <text x="485" y="58">③ 영역 7 → 0</text><text x="485" y="75">region_hit (SRD 포함)</text>
    <text x="485" y="158">④ 첫 히트 영역</text><text x="485" y="175">X&amp;&amp;XN? AP 허용?</text>
    <text x="285" y="158">⑤ 히트 없음</text><text x="285" y="175">특권 &amp;&amp; PRIVDEFENA ?</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M160 62 H188" marker-end="url(#m15-arr)"/><path d="M380 62 H408" marker-end="url(#m15-arr)"/>
    <path d="M485 84 V138" marker-end="url(#m15-arr)"/>
    <path d="M560 62 H600 V120 H330 V138" marker-end="url(#m15-arr)"/>
    <path d="M85 84 V228" marker-end="url(#m15-arr)"/><path d="M285 84 V100 H250 V228" marker-end="url(#m15-arr)"/>
    <path d="M485 184 V228" marker-end="url(#m15-arr)"/><path d="M285 184 V228" marker-end="url(#m15-arr)"/>
  </g>
  <g font-size="12" fill="currentColor">
    <text x="150" y="34">아니오</text><text x="372" y="34">아니오</text><text x="492" y="112">히트</text>
    <text x="565" y="110">미스</text><text x="92" y="120">예</text><text x="226" y="112">예</text>
  </g>
  <g stroke="currentColor">
    <rect x="10" y="230" width="150" height="86" rx="6" class="f-warn"/>
    <rect x="175" y="230" width="90" height="86" rx="6" class="f-ok"/>
    <rect x="275" y="230" width="120" height="86" rx="6" class="f-ok"/>
    <rect x="410" y="230" width="150" height="86" rx="6" class="f-acc"/>
    <rect x="575" y="230" width="155" height="86" rx="6" class="f-warn"/>
  </g>
  <g font-size="12" fill="currentColor">
    <text x="18" y="250">X → XN 폴트</text><text x="18" y="268">비특권 → BusFault</text><text x="18" y="286">특권 → OK_PPB</text>
    <text x="183" y="250">기본 맵</text><text x="183" y="268">X &amp; XN구역</text><text x="183" y="286">→ 폴트</text>
    <text x="283" y="250">예: 기본 맵</text><text x="283" y="268">(OK_DEFAULT)</text><text x="283" y="286">아니오: F_NOMATCH</text>
    <text x="418" y="250">F_XN / F_AP</text><text x="418" y="268">또는 OK_REGION</text><text x="418" y="286">아래 영역은 안 봄</text>
    <text x="583" y="250">폴트 보고</text><text x="583" y="268">D: DACCVIOL+MMFAR</text><text x="583" y="286">X: IACCVIOL, MMFAR 무효</text>
  </g>
</svg>
```

**겹침.** 테스트는 64 KB RW 영역(R0)과 그 안의 1 KB RO 영역(R1)으로 시작한다 — 쓰기는 R1 이 거부. 번호만 바꾸면(R0 = 1 KB RO, R1 = 64 KB RW) 같은 주소 쓰기가 **허용** 된다. "더 구체적인(작은) 영역이 이긴다" 는 오답이다. 그래서 RTOS 는 "넓은 배경 → 좁은 예외" 순서로 번호를 매긴다: 배경 영역(SRAM 전체, 주변장치 전체)은 낮은 번호, 태스크 영역·가드는 높은 번호.

**서브리전 구멍.** 꺼진 서브리전은 "이 영역은 이 주소를 모른다" 와 같다 → 아래 번호 영역을 계속 본다. 테스트에서 8 KB RW/RW 영역(R1, SRD=0x30)의 4·5 번 조각 `[0x2000_3000, 0x2000_3800)` 에 비특권 쓰기를 하면 R0(특권 전용)이 판정해 `F_AP region 0`, 특권이면 허용. R0 를 끄면 구멍 아래가 비어 비특권은 `F_NOMATCH`, 특권은 PRIVDEFENA 에 따라 `OK_DEFAULT` / `F_NOMATCH`. 서브리전은 **겹침과 같이 써서** 큰 영역에 구멍을 뚫거나, 2^n 이 아닌 버퍼를 덮는 데 쓴다.

**MPU 밖의 길.**

| 상황 | 판정 | 실제 코어에서 |
|---|---|---|
| MPU_CTRL.ENABLE = 0 | 기본 맵 | 기본 맵 XN 구역 실행은 여전히 MemManage (B11 연습 2 의 XN 표) |
| HardFault/NMI 안, HFNMIENA = 0 | 기본 맵 | 폴트 핸들러가 망가진 영역 설정과 무관하게 돈다 |
| PPB | 항상 기본 맵 | NVIC·SCB·MPU 레지스터 자체를 MPU 가 막지 않는다; 비특권 접근은 BusFault (STIR 같은 예외는 모델 밖) |

**여러 바이트 접근.** R0 `[0x000, 0x400)` RW/RW, R1 `[0x400, 0x800)` 특권 전용에서 비특권 4바이트 쓰기 at `0x2000_03FE` — 앞 2바이트는 R0, 뒤 2바이트는 R1. 첫 바이트만 보면 허용으로 오판한다. 모델은 `MMFAR = 0x2000_0400` (첫 폴트 바이트)을 보고한다. **주의:** Cortex-M4 Generic User Guide 는 비정렬 접근이 여러 정렬 접근으로 쪼개질 수 있어서 MMFAR 가 "요청 범위 안의 어떤 주소든" 될 수 있다고 적고 있다 — 첫 폴트 바이트는 이 모델의 선택이다. 그래뉼 단위 검사는 참조 모델(바이트마다)과 20만 접근에서 일치했다(`bad == 0`). 그래뉼을 64 B 로 바꾸는 변이를 넣어 보니 이 비교가 실제로 FAIL 했다 — 테스트가 차이를 잡는다는 확인.

### Part C — 계산기, 레지스터, RTOS 구성

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">mpu_calc(0x2000_7000, 8 KB) — 32 KB 경계를 걸치면 64 KB 영역이 필요하다</text>
  <g stroke="currentColor">
    <rect x="60" y="60" width="80" height="40" fill="none" stroke-dasharray="4 3"/>
    <rect x="140" y="60" width="80" height="40" fill="none" stroke-dasharray="4 3"/>
    <rect x="220" y="60" width="80" height="40" fill="none" stroke-dasharray="4 3"/>
    <rect x="300" y="60" width="80" height="40" class="f-ok"/>
    <rect x="380" y="60" width="80" height="40" class="f-ok"/>
    <rect x="460" y="60" width="80" height="40" fill="none" stroke-dasharray="4 3"/>
    <rect x="540" y="60" width="80" height="40" fill="none" stroke-dasharray="4 3"/>
    <rect x="620" y="60" width="80" height="40" fill="none" stroke-dasharray="4 3"/>
    <rect x="340" y="120" width="80" height="26" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12" text-anchor="middle">
    <text x="100" y="85">0</text><text x="180" y="85">1</text><text x="260" y="85">2</text><text x="340" y="85">3 켬</text>
    <text x="420" y="85">4 켬</text><text x="500" y="85">5</text><text x="580" y="85">6</text><text x="660" y="85">7</text>
    <text x="380" y="138">버퍼 8 KB</text>
  </g>
  <g fill="currentColor" font-size="12" font-family="monospace" text-anchor="middle">
    <text x="60" y="50">0000</text><text x="380" y="50">8000</text><text x="700" y="50">10000</text>
    <text x="340" y="164">7000</text><text x="420" y="164">9000</text>
  </g>
  <path d="M380 40 V200" stroke="currentColor" stroke-dasharray="2 3"/>
  <text x="60" y="190" fill="currentColor" font-size="12">base 0x2000_0000, SIZE=15 (64 KB), 서브리전 8 KB, SRD = 0xE7 (3·4 만 켬)</text>
  <text x="60" y="210" fill="currentColor" font-size="12">덮은 16 KB − 버퍼 8 KB = 넘침 8 KB → 버퍼를 0x2000_8000 에 정렬했다면 SIZE=12, 넘침 0</text>
</svg>
```

**계산기.** `fit_region` 은 SIZE 를 4 부터 올리며 "버퍼를 다 덮는 정렬 블록" 을 찾고, 256 B 이상이면 버퍼와 겹치지 않는 조각을 끈다. 더 큰 블록이 넘침을 줄일 수 없는 이유: 2S 블록의 조각(S/4)은 S 블록의 조각(S/8) 두 개를 합친 것이고, 버퍼와 닿는 조각의 합집합은 격자가 거칠수록 커지기만 한다. 유일한 예외는 서브리전이 없는 32/64/128 B 블록: `[0x2000_0020, +64)` 는 128 B 블록(넘침 64)보다 256 B 블록 + 32 B 조각 1·2 번(넘침 0)이 낫다. 동률이면 작은 영역(`[0x2000_0040, +64)` → 64 B 그대로). 무작위 2만 버퍼에서 결과가 (1) 검증 통과 (2) 버퍼 전체 히트 (3) 넘침이 모든 SIZE 를 다 시도한 최솟값과 같음을 확인했다.

`demo_region_map.c` 는 solution.c 를 `#define main lesson_main` 으로 가져와 표를 찍는다:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_region_map.c -o /tmp/esn_ARCH-M15/demo && /tmp/esn_ARCH-M15/demo
```

```text
영역 계산기 (mpu_calc)
  0x20000000 +0x6000   → base 0x20000000 SIZE=14 ( 32K) sub   4K SRD=0xC0  over=0x0  24 KB, 맨 앞 정렬
  0x20001000 +0x6000   → base 0x20000000 SIZE=14 ( 32K) sub   4K SRD=0x81  over=0x0  24 KB, 가운데
  0x20007000 +0x2000   → base 0x20000000 SIZE=15 ( 64K) sub   8K SRD=0xE7  over=0x2000  8 KB 가 32 KB 경계를 걸침
  0x20000020 +0x40     → base 0x20000000 SIZE= 7 (256B) sub  32B SRD=0xF9  over=0x0  128 B 대신 256 B + 서브리전
  0x20004000 +0x20     → base 0x20004000 SIZE= 4 ( 32B) sub    - SRD=0x00  over=0x0  스택 가드
  0x20001234 +0x3E8    → base 0x20001000 SIZE=10 (  2K) sub 256B SRD=0x83  over=0x118  정렬 안 된 1000 B
  0x7FFFFFF0 +0x20     → base 0x00000000 SIZE=31 (  4G) sub 512M SRD=0xE7  over=0x3FFFFFE0  2 GB 경계를 걸친 32 B

RTOS 구성 (영역 번호가 클수록 우선)
  #  name        base       size  AP    XN SRD   RBAR       RASR
  0  Flash       0x08000000 512K  RO/RO    0x00  0x08000010 0x06020025
  1  SRAM        0x20000000 128K  RW/-- XN 0x00  0x20000011 0x11060021
  2  Periph      0x40000000 512M  RW/-- XN 0x00  0x40000012 0x11010039
  3  TaskA RAM   0x20004000 8K    RW/RW XN 0xC0  0x20004013 0x1306C019
  4  shared tbl  0x20008000 1K    RW/RO XN 0x00  0x20008014 0x12000013
  5  TaskA UART  0x40004400 1K    RW/RW XN 0x00  0x40004415 0x13010013
  6  -           (disabled)  RBAR 0x00000016 RASR 0x00000000
  7  stack guard 0x20004000 32B   --/-- XN 0x00  0x20004017 0x10000009

접근 로그 (PRIVDEFENA=1, HFNMIENA=0)
  user    X2 @0x08001234 → OK_REGION    r0   ; 태스크가 Flash 코드 실행
  user    W4 @0x08001234 → F_AP         r0   [MemManage DACCVIOL] MMFAR=0x08001234  ; 태스크가 Flash 에 쓰기
  user    W4 @0x20004100 → OK_REGION    r3   ; 태스크 스택 push
  user    W4 @0x20004010 → F_AP         r7   [MemManage DACCVIOL] MMFAR=0x20004010  ; 스택 넘침 → 가드
  priv    W4 @0x20004010 → F_AP         r7   [MemManage DACCVIOL] MMFAR=0x20004010  ; ISR 이 가드에 쓰기
  user    W8 @0x200057FC → F_AP         r1   [MemManage DACCVIOL] MMFAR=0x20005800  ; STRD 뒤 절반이 서브리전 구멍
  priv    W4 @0x20003FF0 → OK_REGION    r1   ; 큰 프레임이 가드 건너뜀
  user    X2 @0x20004100 → F_XN         r3   [MemManage IACCVIOL MMFAR 무효]  ; SRAM 에서 코드 실행
  user    W4 @0x40004404 → OK_REGION    r5   ; 태스크 UART 창
  user    W4 @0x40004800 → F_AP         r2   [MemManage DACCVIOL] MMFAR=0x40004800  ; 창 밖 주변장치
  priv    R4 @0x60000000 → OK_DEFAULT   r-1  ; 영역 없는 주소 (특권)
  user    R4 @0x60000000 → F_NOMATCH    r-1  [MemManage DACCVIOL] MMFAR=0x60000000  ; 영역 없는 주소 (비특권)
  priv+HF R4 @0x20004010 → OK_MPU_OFF   r-1  ; HardFault 핸들러가 가드 덤프
  user    R4 @0xE000ED28 → F_PPB_UNPRIV r-1  [BusFault]  ; 태스크가 SCB->CFSR 읽기
```

읽는 법:

- **RASR 해부** — `0x1306C019` = XN(bit28) · AP=011(`0x03`<<24) · TEX/S/C/B=`0x06`(S=1, C=1) · SRD=`0xC0` · SIZE=12(`0x18`) · ENABLE. 필드 위치는 CMSIS `core_cm4.h` 의 `MPU_RASR_*_Pos` 와 같다. TEX/S/C/B 의 의미(Normal/Device, 캐시 정책)는 이 모델이 보관만 하고 해석하지 않는다 — 실제 값 선택은 BM-A08, 캐시 속성과 DMA 는 ARCH-M12.
- **RBAR** — 하위 5비트 = VALID(bit4) + REGION. VALID=1 로 쓰면 RNR 을 따로 안 써도 그 번호 영역의 base 가 바뀐다. base 비트는 [31:N] 만 의미가 있어서 RBAR 는 32 B 단위 마스크(`0xFFFF_FFE0`)로 담고, 정렬은 `region_validate` 가 따로 본다(크기에 안 맞는 base 의 실제 동작은 스펙이 소프트웨어 책임으로 두므로 모델은 에러로 처리).
- **가드는 특권도 막는다** (AP=000). 그래서 ISR 이 태스크 스택을 넘쳐도 잡힌다. 하지만 **큰 프레임은 건너뛴다**: 지역 배열 64 B 짜리 함수가 SP 를 가드 아래로 내리고 거기 쓰면 R1(특권 RW)이 받아 조용히 오염된다. 가드 크기 ≥ 가장 큰 스택 프레임이거나, 컴파일러 스택 프로빙, 또는 ARMv8-M 의 스택 한계 레지스터(MSPLIM/PSPLIM)가 답이다.
- **IACCVIOL 은 MMFAR 가 무효** — 폴트 난 명령 주소는 스택된 PC 에서 읽는다.
- **HardFault 핸들러가 가드를 덤프** 할 수 있는 건 HFNMIENA=0 이라서다. 1 로 두면 HardFault 안의 접근도 MPU 를 거치고, 거기서 또 폴트하면 lockup.

**레지스터 쓰기는 어떤 명령이 되나 (`demo_mpu_asm.c`).** 실제 출력(지시어 줄 제거, 주석은 설명용으로 붙임):

```bash
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_mpu_asm.c -o -
```

```text
mpu_guard_on:
	movs	r1, #23                ; 0x17 = VALID | 영역 7
	bfi	r0, r1, #0, #5         ; (stack_lo & ~31) | 0x17 를 BFI 한 번으로
	movw	r1, #60820
	movt	r1, #57344             ; r1 = 0xE000_ED94 (MPU_CTRL)
	str	r0, [r1, #8]           ; RBAR (0xE000_ED9C)
	movs	r0, #9
	movt	r0, #4096              ; 0x1000_0009 = XN | AP=000 | SIZE=4 | EN — 상수로 접힘
	str	r0, [r1, #12]          ; RASR (0xE000_EDA0)
	movs	r0, #5
	str	r0, [r1]               ; CTRL = PRIVDEFENA | ENABLE
	dsb	sy
	isb	sy
	bx	lr
mpu_load:
	...
	movw	r4, #60828
	movt	r4, #57344             ; r4 = 0xE000_ED9C (RBAR)
.LBB1_3:                               ; 4개씩 펼친 루프
	ldr	r2, [r3, #-28]
	str	r2, [r4]               ; RBAR
	ldr	r2, [r3, #-24]
	str	r2, [r4, #4]           ; RASR
	...
```

RASR 값은 컴파일 타임 상수(`0x1000_0009`)가 되고, 영역 하나 = `str` 두 번이다. 컨텍스트 스위치마다 태스크 영역 3~4개를 바꿔도 수십 사이클 수준이라, RTOS 는 "정적 영역(Flash, SRAM 배경, 주변장치) + 태스크마다 바꾸는 영역" 으로 나눈다. 활성화 뒤 `DSB; ISB` 는 새 설정이 다음 명령부터 확실히 적용되게 하는 관용구(CMSIS `ARM_MPU_Enable` 과 같은 순서) — 정확한 배리어 요구는 BM-A08 에서 실제 코어로 확인한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/esn_ARCH-M15/so && /tmp/esn_ARCH-M15/so; echo "exit $?"
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/esn_ARCH-M15/san && /tmp/esn_ARCH-M15/san; echo "exit $?"
```

```text
87/87 passed
exit 0
87/87 passed
exit 0
```

(Apple clang 21.0.0, Apple M6. ASan/UBSan 경고 없음.) 테스트가 실제로 규칙을 잡는지 변이로 확인했다: 영역 루프를 0→7 첫 히트로 바꾸면 14개 FAIL(73/87), 256 B + SRD 대안을 빼면 계산기 2개 FAIL(85/87), 그래뉼 간격을 64 B 로 바꾸면 무작위 비교 1개 FAIL(86/87).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| SIZE 에 `log2(크기)` 를 그대로 | 크기 = 2^(SIZE+1) 이라 영역이 **두 배** — 옆 버퍼까지 열린다 | `SIZE = log2(크기) − 1` (32 B → 4) |
| 6 KB 스택에 base 0x2000_5000, 8 KB 영역 | base 가 크기의 배수가 아님 — 스펙상 소프트웨어 책임, 결과를 믿을 수 없다 | `mpu_calc` → 0x2000_4000 + SRD, 또는 스택을 정렬해 배치 |
| "작은(구체적) 영역이 이긴다" | PMSAv7 은 **번호** 만 본다 | 배경 = 낮은 번호, 예외(가드·창) = 높은 번호 |
| 가드를 R0 에, SRAM 배경을 R1 에 | 가드가 SRAM 영역에 가려 아무것도 안 막는다 | 가드는 가장 높은 번호 (R7) |
| 128 B 영역에 SRD | 128 B 이하에는 서브리전이 없다 (SRD 는 0 이어야 함) | 256 B 영역 + SRD, 또는 영역 2개 |
| 첫 바이트만 검사 | STRD/LDM/비정렬 LDR 이 경계를 걸치면 뒤쪽 바이트가 다른 영역 | 그래뉼마다 검사 (`mpu_access`) |
| `base + size` 를 32비트로 | 4 GB 영역·맨 끝 영역에서 0 으로 감김 | `off = addr − base`, `(uint64_t)off < size` |
| AP=010 을 "비특권 RW" 로 | 010 = 특권 RW / 비특권 **RO** | 표를 코드 상수로 두고 전수 테스트 |
| SRAM 영역에 XN 빠뜨림 | 버퍼 오버플로로 쓴 코드가 실행 가능 | 데이터 영역은 모두 XN=1, RAM 함수 구간만 예외 |
| IACCVIOL 에서 MMFAR 를 범인 주소로 | 명령 fetch 위반은 MMARVALID=0 — MMFAR 는 쓰레기 | 스택된 PC 를 본다 |
| PRIVDEFENA=0 으로 켜고 Flash 영역을 안 만듦 | 다음 명령 fetch 부터 폴트 → 핸들러도 fetch 못 함 | 켜기 전에 코드·벡터·스택 영역부터, 또는 PRIVDEFENA=1 |

## 8. 면접 Q&A

<details>
<summary>Q1. MPU 영역 두 개가 겹치면 어느 쪽 속성이 적용되나요?</summary>

ARMv7-M PMSAv7: **번호가 큰 영역**. 크기나 "더 구체적" 인지는 상관없다. 그래서 큰 배경 영역(SRAM 전체 특권 전용)을 낮은 번호에, 그 안의 예외(태스크 스택 RW/RW, 32 B 가드 no-access)를 높은 번호에 둔다. 이 겹침 + 우선순위가 v7 에서 "구멍 뚫기" 의 기본 도구다.

꼬리질문: "ARMv8-M(Cortex-M33/M55)에서도 같나요?" → 아니다. PMSAv8 은 영역이 base/limit(32 B 단위)라 임의 크기가 되는 대신, **한 주소가 두 영역에 걸리면 MemManage 폴트** 다(연습 1). 그래서 v8 에선 영역을 겹치지 않게 잘라 배치해야 하고, v7 코드를 그대로 옮기면 겹침마다 폴트가 난다.

</details>

<details>
<summary>Q2. 왜 크기가 2의 거듭제곱이고 base 가 크기에 정렬돼야 하나요? 6 KB 스택은 어떻게 보호하나요?</summary>

영역마다 비교기 하나가 "주소 상위 32−N 비트 == base 상위 비트" 만 보면 되도록 — 범위 비교(덧셈·크기 비교)보다 게이트가 적고 한 사이클에 8개가 병렬로 끝난다. 대가는 낭비와 정렬 제약.

6 KB: 8 KB 정렬 블록 + SRD 로 6/8 만 켠다(계산기 결과 `SIZE=12, SRD=0xC0`). 블록 경계를 걸치게 놓으면 64 KB 영역까지 커질 수 있으니(Part C 그림), 링커 스크립트에서 스택을 크기에 맞춰 정렬해 둔다. 또는 영역 2개(4 KB + 2 KB).

꼬리질문: "서브리전은 32 B 단위까지 되나요?" → 256 B 영역의 서브리전이 32 B 라서 최소 단위는 32 B. 128 B 이하 영역에는 서브리전이 없다.

</details>

<details>
<summary>Q3. 스택 가드를 어떻게 설계하나요? 32 B 로 충분한가요?</summary>

스택 바닥(가장 낮은 주소)에 AP=000·XN 영역을 가장 높은 번호로 둔다. 특권도 막히므로 ISR 이 넘쳐도 잡힌다. 한계: (1) 가드보다 큰 지역 변수 프레임은 SP 가 가드를 **건너뛰어** 아래 메모리를 쓴다 — 테스트의 `0x2000_3FF0` 쓰기가 R1 에서 조용히 허용됐다. 가드 크기 ≥ 최대 프레임, 또는 큰 배열을 정적으로. (2) 넘침이 **예외 진입 스태킹** 중에 나면 MSTKERR 로 보고되고 MMFAR 는 유효하지 않다(스태킹 실패 → 핸들러가 쓸 스택도 없음 → 보통 HardFault 로 이어짐). 그래서 폴트 핸들러는 MSP 로, 여유 있게.

꼬리질문: "ARMv8-M 에선?" → (Mainline 기준) 스택 한계 레지스터 MSPLIM/PSPLIM 이 SP 가 한계 아래로 내려가는 순간 UsageFault(STKOF) — MPU 영역을 쓰지 않고, 프레임 크기와 무관하게 잡는다(SP 자체를 비교하므로).

</details>

<details>
<summary>Q4. PRIVDEFENA 와 HFNMIENA 는 각각 무엇이고, 잘못 두면 무슨 일이 나나요?</summary>

PRIVDEFENA=1: 특권 접근이 어느 영역에도 안 걸리면 기본 맵(ARCH-B11 연습 2 의 표)을 쓴다 — 커널은 영역을 덜 쓰고, 비특권 태스크만 영역으로 가둔다. 0 이면 특권도 영역에 없는 주소는 폴트.

HFNMIENA=0(기본): HardFault·NMI(그리고 FAULTMASK 로 우선순위가 음수인 동안)에는 MPU 를 쓰지 않는다 — 영역 설정이 망가져도 폴트 핸들러가 돌고 덤프할 수 있다(테스트의 `OK_MPU_OFF`). 1 이면 그 핸들러들도 MPU 를 거친다.

꼬리질문: "MPU 를 켜자마자 lockup 이 났다" → 전형: PRIVDEFENA=0 인데 지금 실행 중인 Flash 를 덮는 영역이 없음 → 다음 fetch 가 MemManage → MemManage 핸들러 fetch 도 실패 → HardFault 로 승격. HFNMIENA=0 이면 HardFault 는 돌 수 있지만, HFNMIENA=1 이면 HardFault 핸들러 fetch 도 실패 → lockup. 그래서 "코드·벡터 테이블·스택 영역을 먼저, 그다음 ENABLE, 그다음 DSB/ISB".

</details>

<details>
<summary>Q5. MemManage 핸들러에서 무엇을 읽고 어떻게 해석하나요?</summary>

CFSR 의 하위 바이트 MMFSR: IACCVIOL(명령 fetch 위반), DACCVIOL(데이터 접근 위반), MUNSTKERR/MSTKERR(예외 복귀/진입 스태킹 중 위반), MLSPERR(FPU lazy 스태킹, M4F), MMARVALID. MMARVALID=1 일 때만 MMFAR 를 믿는다 — DACCVIOL 은 보통 유효, IACCVIOL 은 무효(스택된 PC 가 폴트 명령). 다 읽은 뒤 해당 비트에 1 을 써서 지운다(W1C, C-M02). 연습 3 이 이 한 줄 디코더다.

꼬리질문: "STRD 가 영역 경계를 걸쳐 폴트하면 MMFAR 는?" → 접근 범위 안의 어떤 주소일 수 있다(코어가 정렬 접근으로 쪼개므로). 모델은 첫 폴트 바이트 `0x2000_5800` 을 냈지만, 핸들러는 "MMFAR ± 접근 크기" 로 해석해야 한다. 그리고 비특권 PPB 접근은 MemManage 가 아니라 **BusFault** 라 MMFSR 이 아니라 BFSR 을 봐야 한다.

</details>

<details>
<summary>Q6. RTOS 컨텍스트 스위치에서 MPU 는 어떻게 다루나요? 비용은?</summary>

영역을 정적(Flash, SRAM 배경, 주변장치 배경, 커널 데이터)과 태스크별(스택, 태스크 데이터, 주변장치 창, 공유 버퍼)로 나누고, 스위치 때 태스크별 영역 3~4개의 RBAR/RASR 만 다시 쓴다. `demo_mpu_asm.c` 에서 영역 하나 = `str` 두 번, 표 로드는 펼친 `ldr/str` 루프 — 수십 사이클 수준이다(실측은 BM-A10 의 DWT 로). 바꾸는 영역을 쓰는 동안 그 영역이 잠깐 "반쯤 바뀐" 상태가 되지 않게, 바꾸는 순서(RASR.ENABLE 끄고 → RBAR → RASR)와 그 사이 인터럽트를 신경 쓴다. 쓰고 난 뒤 DSB/ISB.

꼬리질문: "영역이 8개뿐이면 태스크가 주변장치 10개를 써야 할 땐?" → 주변장치를 2^n 정렬 블록 하나로 묶이게 메모리 맵을 고르거나(칩 선택 단계의 일), 커널 드라이버 호출(SVC)로 대신 접근하게 한다. 8개 제약 때문에 MPU 설계는 "영역 예산" 문제가 된다.

</details>

<details>
<summary>Q7. Flash 를 지우는 함수는 RAM 에서 돌아야 하는데, SRAM 을 XN 으로 두면 어떻게 하나요?</summary>

`.ramfunc` 섹션을 정렬해 링크하고 그 구간만 XN=0 영역(가능하면 특권 RO+실행)으로 연다. 나머지 SRAM 은 XN — 버퍼 오버플로로 쓴 바이트가 실행되는 것을 막는다(W^X 원칙). 실행에는 XN=0 뿐 아니라 **읽기 권한** 도 필요하다(모델: `F_XN` 먼저, 그다음 `F_AP`). 그리고 SRAM 영역을 XN 으로 두면 MPU 가 꺼져 있을 때와 달리 SRAM 실행이 막힌다는 걸 잊지 말 것 — 기본 맵에서 SRAM 은 실행 가능 구역이다.

꼬리질문: "MPU 없이 기본 맵만으로 주변장치 영역 실행은?" → 기본 맵에서 Peripheral/Device/System 구역은 XN 이라 MPU 가 꺼져도 MemManage(IACCVIOL) — 테스트의 `0x4000_0000` 실행 케이스.

</details>

## 9. 추가 연습

### 연습 1 — ARMv8-M PMSAv8 검사기

RBAR = `BASE[31:5] | SH[4:3] | AP[2:1] | XN[0]`, RLAR = `LIMIT[31:5] | AttrIndx[3:1] | EN[0]` 를 디코드하고(limit 의 하위 5비트는 1 로), AP 2비트(00 특권 RW · 01 RW 누구나 · 10 특권 RO · 11 RO 누구나)와 "두 영역 이상 걸리면 폴트" 로 판정하라. 본문의 6 KB 스택 + 32 B 가드를 v8 로 옮기면 무엇이 달라지나?

<details>
<summary>답 보기</summary>

6 KB 스택은 서브리전 없이 `[0x2000_4000, 0x2000_57FF]` 그대로 된다. 하지만 v7 처럼 SRAM 배경 위에 스택을 겹치거나, 스택 위에 가드를 겹치면 그 주소는 `V8_F_OVERLAP`. 배경 SRAM 을 스택 위·아래로 잘라 영역 2개로 만들고, 스택을 가드 위(0x2000_4020)에서 시작해야 한다. 그리고 v8 AP 에는 **"아무도 접근 못 함" 인코딩이 없다** — AP=00 가드는 특권을 통과시킨다(테스트로 확인). 특권까지 막는 v8 식 가드는 "영역 없는 틈 + PRIVDEFENA=0"(대신 커널이 쓰는 모든 메모리에 영역이 필요) 또는 PSPLIM/MSPLIM. (v8 비트 배치는 ARMv8-M ARM 기준으로 적었지만, 실제 부품에서는 영역 수·속성 간접 표(MAIR)까지 확인할 것.)

</details>

### 연습 2 — 서브리전 없이 정확히 덮기

`split_pow2(start, len)` — `[start, start+len)` 를 크기 정렬된 2^k(≥32 B) 블록들로 정확히 나누는 그리디. `[0x2000_0020, 0x2000_1000)` 는 몇 개가 필요한가? 서브리전을 쓰면 몇 개로 줄어드나?

<details>
<summary>답 보기</summary>

그리디: 매 위치에서 "정렬이 허락하고 남은 길이를 넘지 않는 가장 큰 블록". 32+64+128+256+512+1024+2048 = 7개 — 8개 영역 예산을 거의 다 쓴다. 서브리전으로는 3개: 4 KB @0x2000_0000 SRD=0x01(512 B 조각 0 끔 → `[0x200, 0x1000)`), 512 B @0x2000_0000 SRD=0x01(64 B 조각 0 끔 → `[0x040, 0x200)`), 32 B @0x2000_0020. 바이트 단위로 0x2000_0000–0x2000_10FF 를 훑어 "덮임 == 범위 안" 이고 겹침 0 임을 확인한다. 극단: `[0, 0xFFFF_FFE0)` 는 서브리전 없이 27개.

</details>

### 연습 3 — MMFSR 디코더

`mmfsr_decode(mmfsr, mmfar, buf)` — `0x82, 0x2000_4010` → `"DACCVIOL MMFAR=0x20004010"`, `0x01` → `"IACCVIOL"`(MMARVALID 없으면 주소를 찍지 않음), `0x10` → `"MSTKERR"`, 예약 비트(2, 6)만 → `"none"`.

<details>
<summary>답 보기</summary>

비트 0 IACCVIOL, 1 DACCVIOL, 3 MUNSTKERR, 4 MSTKERR, 5 MLSPERR, 7 MMARVALID(CFSR 의 하위 바이트, 주소 0xE000_ED28). 주소는 bit7 이 1 일 때만 찍는다 — "MMFAR 를 무조건 찍는 핸들러" 는 IACCVIOL 에서 이전 폴트의 주소나 쓰레기를 범인으로 보여 준다. 실제 핸들러는 읽은 뒤 같은 비트에 1 을 써서 지운다.

</details>

### 연습 4 — 유효 권한 맵

본문 RTOS 구성의 SRAM 쪽(R1, R3, R4, R7)으로 `0x2000_3000` 부터 1 KB 칸 24개를 비특권/특권으로 한 글자씩(`W` 읽기+쓰기, `R` 읽기만, `-` 금지) 찍어라.

<details>
<summary>답 보기</summary>

비특권 `-----WWWWW----------R---`: 첫 4칸은 R1(특권 전용), 5번째 칸(0x2000_4000)은 칸 첫 주소가 가드라 `-`, 이어 R3 다섯 칸, 0x2000_5800 부터 R3 의 꺼진 조각 → R1 → `-`, 0x2000_8000 은 R4 의 비특권 RO. 특권 `WWWW-WWWWWWWWWWWWWWWWWWW` — 가드만 막힌다. 32 B 칸으로 확대하면 `-WWW`. 1 KB 칸 표에서는 가드가 칸 하나를 통째로 막은 것처럼 보이지만 실제로는 32 B 다 — 샘플링 해상도가 판정 단위(32 B)보다 거칠면 맵이 거짓말을 한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/esn_ARCH-M15/pr && /tmp/esn_ARCH-M15/pr   # 22/22 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o /tmp/esn_ARCH-M15/psan && /tmp/esn_ARCH-M15/psan   # 22/22 passed
```

## 10. 핵심 정리

- PMSAv7 영역 = 2^(SIZE+1) 바이트(SIZE 4..31 = 32 B..4 GB), base 는 크기의 배수. 이유: 영역마다 상위 비트 비교기 하나.
- 256 B 이상 영역은 8 서브리전, SRD bit = 1 이면 그 조각은 "이 영역 아님" → 아래 번호 영역/기본 맵으로 떨어진다.
- 겹치면 **번호 큰 영역** 이 판정(거부도 이긴다). 배경은 낮은 번호, 가드·창은 높은 번호. v8-M 은 겹침 = 폴트.
- AP: 000 금지 · 001 RW/− · 010 RW/RO · 011 RW/RW · 101 RO/− · 110/111 RO/RO. 실행 = 읽기 허용 && XN=0.
- 미스: 특권 && PRIVDEFENA → 기본 맵, 그 외 폴트. HFNMIENA=0 이면 HardFault/NMI 는 MPU 무시. PPB 는 항상 기본 맵(비특권 → BusFault).
- 판정은 32 B 그래뉼 안에서 일정 → 여러 바이트 접근은 그래뉼마다. 경계 걸침의 MMFAR 는 범위 안 어느 주소일 수 있다.
- 계산기: 가장 작은 덮는 정렬 블록 + SRD 가 최적(128 B 이하면 256 B + SRD 와 비교). 정렬 경계를 걸친 버퍼는 영역을 몇 배로 키운다 → 링커에서 정렬.
- 32 B 가드는 특권도 막지만 큰 프레임은 건너뛴다. DACCVIOL 은 MMFAR 유효, IACCVIOL 은 스택된 PC.

## 참고자료

- [ARMv7-M Architecture Reference Manual (DDI 0403)](https://developer.arm.com/documentation/ddi0403/latest) — PMSAv7: 영역 매칭, 서브리전, AP/XN, PRIVDEFENA/HFNMIENA, 기본 메모리 맵
- [Cortex-M4 Devices Generic User Guide (DUI 0553)](https://developer.arm.com/documentation/dui0553/latest) — MPU 레지스터(TYPE/CTRL/RNR/RBAR/RASR), AP 표, 서브리전, MMFSR/MMFAR, 비정렬 접근과 MMFAR
- [ARMv8-M Architecture Reference Manual (DDI 0553)](https://developer.arm.com/documentation/ddi0553/latest) — PMSAv8 RBAR/RLAR, 겹침 폴트, MSPLIM/PSPLIM
- [CMSIS-Core MPU functions (ARMv7)](https://arm-software.github.io/CMSIS_5/Core/html/group__mpu__functions.html) — `ARM_MPU_RBAR`, `ARM_MPU_RASR`, `ARM_MPU_Enable` (DSB/ISB 순서)
- [FreeRTOS-MPU](https://www.freertos.org/FreeRTOS-MPU-memory-protection-unit.html) — 특권/비특권 태스크, 태스크별 영역
- [Zephyr — Arm Cortex-M developer guide (MPU, stack guards)](https://docs.zephyrproject.org/latest/hardware/arch/arm_cortex_m.html) — MPU 스택 가드, 사용자 모드 스레드
- Joseph Yiu, *The Definitive Guide to ARM Cortex-M3 and Cortex-M4 Processors* — MPU 장, 폴트 처리 장
