---
id: SIV-A05
time: 110분
updated: 2026-11-08
---
# SIV-A05 Errata 관리와 칩 리비전별 워크어라운드

## 학습 목표

- chip ID 레지스터를 디코드(파트 번호 / 메이저 / 마이너(메탈 스핀) / fab)하고, 리비전을 하나의 정수로 만들어 비교한다.
- errata 테이블 `{ID, 영향 리비전, applies-if, 워크어라운드, 검증 probe, status}` 을 설계하고, A0/A1/B0 에서 **정확히 맞는 집합**만 적용되는지 테스트한다.
- 워크어라운드의 phase 순서(클럭·MMU·캐시 변경 전), 의존성, 멱등성, 충돌, 미지의 리비전 정책을 코드와 테스트로 다룬다.
- ECO 뒤 **WA 를 끈 상태로** 검증해 "버그가 정말 없어졌다"(stale entry) 와 "표가 틀렸다"(missing entry), "WA 가 테스트를 가렸다"(masked)를 구분하고, 이 방법이 무엇을 놓치는지 숫자로 말한다.

## 1. 문제

칩은 리스핀마다 버그가 바뀌고 펌웨어는 한 벌이다 — "A0 에서만 켜야 하는 chicken bit, B0 에서 새로 생긴 버그, 아직 안 나온 C0" 를 안전하게 다루는 errata 프레임워크는 first-silicon bring-up 직무(Rivian·NVIDIA·Apple post-silicon validation 인터뷰)에서 그대로 묻는 설계 문제다.

SoC 가 나오면 errata 시트가 따라온다. 벤더마다 이름은 다르지만(STM32 는 ES 로 시작하는 Errata sheet, Arm 은 SDEN(Software Developer Errata Notice), Intel 은 Specification Update, AMD 는 Revision Guide) 내용은 같다: *어느 리비전에서, 어떤 조건에서, 무엇이 깨지고, 소프트웨어가 어떻게 피할 수 있는가*. 내부 프로젝트라면 이 표를 validation 팀이 직접 만든다. 이 레슨은 그 표를 **실행 가능한 코드**로 만든다. 리비전 체계는 대표적인 관례를 따른다: 글자(A, B)는 base layer 까지 바꾼 전체 스핀, 숫자(A0→A1)는 메탈 레이어만 바꾼 메탈 스핀(metal spin)이다. 실제 체계는 회사마다 다르다.

**이 레슨의 대상은 가상 SoC "SNS-100"** 이다(센싱 SoC 라는 가정, 레지스터·리비전·errata 는 전부 교육용 가상이다). `CHIP_ID` 레지스터 구조는 아래와 같고, mock 레지스터는 SIV-B02 를 축약해 넣었다.

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">CHIP_ID (0x000, RO) 와 부트 흐름에서 errata 가 끼어드는 자리</text>
  <rect x="10" y="34" width="300" height="34" class="f-mut"/>
  <text x="160" y="56" text-anchor="middle" fill="currentColor">[31:16] part = 0x5A10</text>
  <rect x="310" y="34" width="110" height="34" class="f-acc"/>
  <text x="365" y="56" text-anchor="middle" fill="currentColor">[15:12] major</text>
  <rect x="420" y="34" width="110" height="34" class="f-ok"/>
  <text x="475" y="56" text-anchor="middle" fill="currentColor">[11:8] minor</text>
  <rect x="530" y="34" width="200" height="34" class="f-mut"/>
  <text x="630" y="56" text-anchor="middle" fill="currentColor">[7:0] fab (리비전 아님)</text>
  <text x="365" y="86" text-anchor="middle" fill="currentColor" font-size="12">0=A, 1=B, ...</text>
  <text x="475" y="86" text-anchor="middle" fill="currentColor" font-size="12">메탈 스핀</text>
  <defs><marker id="a5a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="110" width="120" height="44" class="f-mut"/>
  <text x="70" y="128" text-anchor="middle" fill="currentColor">CHIP_ID 읽기</text>
  <text x="70" y="145" text-anchor="middle" fill="currentColor" font-size="12">decode, 분류</text>
  <line x1="130" y1="132" x2="154" y2="132" stroke="currentColor" marker-end="url(#a5a)"/>
  <rect x="156" y="110" width="124" height="44" class="f-acc"/>
  <text x="218" y="128" text-anchor="middle" fill="currentColor">PH_EARLY</text>
  <text x="218" y="145" text-anchor="middle" fill="currentColor" font-size="12">클럭 올리기 전</text>
  <line x1="280" y1="132" x2="304" y2="132" stroke="currentColor" marker-end="url(#a5a)"/>
  <rect x="306" y="110" width="100" height="44" class="f-warn"/>
  <text x="356" y="128" text-anchor="middle" fill="currentColor">clock ramp</text>
  <text x="356" y="145" text-anchor="middle" fill="currentColor" font-size="12">PLL 잠김</text>
  <line x1="406" y1="132" x2="430" y2="132" stroke="currentColor" marker-end="url(#a5a)"/>
  <rect x="432" y="110" width="124" height="44" class="f-acc"/>
  <text x="494" y="128" text-anchor="middle" fill="currentColor">PH_PRE_CACHE</text>
  <text x="494" y="145" text-anchor="middle" fill="currentColor" font-size="12">MMU·캐시 켜기 전</text>
  <line x1="556" y1="132" x2="580" y2="132" stroke="currentColor" marker-end="url(#a5a)"/>
  <rect x="582" y="110" width="150" height="44" class="f-warn"/>
  <text x="657" y="128" text-anchor="middle" fill="currentColor">cache/MMU on</text>
  <text x="657" y="145" text-anchor="middle" fill="currentColor" font-size="12">chicken bit 잠김</text>
  <line x1="657" y1="154" x2="657" y2="188" stroke="currentColor" marker-end="url(#a5a)"/>
  <rect x="582" y="190" width="150" height="44" class="f-acc"/>
  <text x="657" y="208" text-anchor="middle" fill="currentColor">PH_LATE</text>
  <text x="657" y="225" text-anchor="middle" fill="currentColor" font-size="12">드라이버 초기화</text>
  <text x="10" y="186" fill="currentColor" font-size="12">PH_EARLY 다음 PLL_CFG 쓰기는 무시되고, PH_PRE_CACHE 다음 L2_CHICKEN 쓰기는 무시된다.</text>
  <text x="10" y="206" fill="currentColor" font-size="12">즉 phase 를 잘못 적으면 WA 는 "성공" 하지만 하드웨어에 도달하지 않는다 → read-back 필요.</text>
</svg>
```

표에 실린 errata (모두 가상):

| ID | 내용 | 영향 범위 | applies-if | phase | WA | status |
|---|---|---|---|---|---|---|
| E1001 | DMA burst 16 이 4K 경계를 넘을 때 데이터 손상 | A0..A1 | 항상 | LATE | `DMA_CTRL.BURST=8` | FIXED (B0) |
| E1002 | L2 eviction 중재 hazard (간헐적) | A0..A0 | L2 있음 | PRE_CACHE | `L2_CHICKEN.ARB_SAFE` | FIXED (A1) |
| E1003 | 저온에서 PLL false lock | A0..A0 | 항상 | EARLY | `PLL_CFG.LOCK_FILT` | FIXED (A1) |
| E1004 | PCIe L0s 복귀 시 hang | A0..OPEN | PCIe 있음 | LATE | `PCIE_PM.L0S_EN=0` (기능 끄기) | OPEN |
| E1005 | ISP 통계 레지스터 torn read — B0 에서 새로 생김 | B0..B0 | 항상 | LATE | `ISP_CFG.SYNC_RD` | FIXED |
| E1006 | SSC jitter 가 lock 을 깸 | A0..A0 | 항상 | EARLY, E1003 필요 | `PLL_CFG.SSC_OFF` | FIXED (A1) |

칩 모델(mock 의 ground truth): A0 = {DMA4K, L2ARB, PLLCOLD, L0S, SSC}, A1 = {DMA4K, L0S}, B0 = {ISPTORN, L0S}. Part C 에서는 **B0-ECO** (L0s 까지 고침)와 **B0-BR** (A0 데이터베이스에서 분기해 A1 메탈 수정이 빠짐, L2ARB 재발)을 추가로 쓴다.

### Part A — ID 디코드와 정확한 적용

`chip_decode`, `rev_in_range`, `erratum_matches`, `errata_select`, `errata_apply_one` 을 채운다. A0/A1/B0 × SKU 조합에서 선택되는 항목 집합이 **정확히 기대값과 같아야** 하고(적용 안 됐어야 할 레지스터는 안 바뀌어야 한다), 적용은 read-back 과 쓰기 감사로 확인한다. `0x00000000`/`0xFFFFFFFF` 는 ID 가 아니라 버스 문제로 처리한다.

### Part B — 순서, 의존성, 멱등성, 충돌, 미지의 리비전

`chip_classify`(known 목록), `errata_order`(phase + 위상 정렬 + 사이클/phase 역전 검출), `errata_find_conflict`(겹치는 비트에 다른 값), `errata_check_idempotent`, `errata_boot` 를 채운다. 미지의 리비전 정책: 프로덕션은 `POLICY_HALT` 로 클럭을 올리기 전에 멈추고, 랩은 `POLICY_LAB` 으로 **rev_max 가 열려 있는 항목만** 적용하고 `tainted` 표시 후 진행한다. 잘못된 파트나 버스 불량은 어떤 정책에서도 진행하지 않는다.

### Part C — ECO 제거: 버그가 정말 사라졌는가

`errata_lint`, `probe_run`, `audit_one`, `eco_propose_rev_max` 를 채운다. 각 칩·항목에 대해 **WA OFF / WA ON 으로 probe** 를 돌려 표와 비교하고 판정한다: NEEDED, CLEAN, STALE(표는 영향이라 하는데 버그 없음), MISSING(표에 없는데 버그 있음), WA_BROKEN, INCONCLUSIVE, masked(WA 가 트리거 자체를 제거). 간헐 버그·코너 누락으로 생기는 오판까지 측정한다.

## 2. Problem (English)

You are writing the errata layer of boot firmware for a SoC with revisions A0, A1 and B0. The chip identifies itself through a 32-bit ID register: `[31:16]` part number, `[15:12]` major revision (0=A, 1=B, ...), `[11:8]` minor revision (metal spin), `[7:0]` fab code, which is not part of the revision.

Implement a table-driven framework:

```c
typedef struct erratum {
    uint32_t id;
    uint16_t rev_min, rev_max;              /* inclusive; rev_max may be REV_OPEN */
    bool (*applies_if)(const soc_t *s);     /* NULL = always */
    int  (*apply)(soc_t *s);                /* 0 = success */
    probe_t (*probe)(soc_t *s, int trials); /* tries to trigger the bug */
    erratum_status_t status;                /* OPEN / WONTFIX / FIXED */
    phase_t phase;                          /* EARLY, PRE_CACHE, LATE */
    uint32_t reg, mask, val;                /* declared footprint */
    uint32_t after[2], requires_id;
} erratum_t;

int  chip_decode(uint32_t raw, chip_id_t *c);
bool rev_in_range(const erratum_t *e, uint16_t rev);
int  errata_select(const erratum_t *t, int n, const soc_t *s, const chip_id_t *c,
                   const erratum_t **out, int max);
int  errata_order(const erratum_t *const *sel, int n, const erratum_t **out);
int  errata_boot(const erratum_t *t, int n, soc_t *s, const uint16_t *known, int nk,
                 policy_t pol, errata_ctx_t *ctx);
void audit_one(const erratum_t *t, int n, const erratum_t *e, const chip_model_t *m,
               const audit_opts_t *o, audit_t *a);
```

Requirements: apply exactly the correct set on each revision and SKU; never let a workaround silently fail (read back the declared field, audit undeclared writes); order by phase, then dependency; detect dependency cycles and phase inversions; reject conflicting workarounds; on an unknown revision either halt before touching clocks or, in a lab build, apply only entries whose range is open-ended and mark the boot tainted. For ECO removal, verify with the workaround *disabled*.

Example: on A0 with both SKU fuses set, the applied order is `E1003, E1006, E1002, E1001, E1004`. On B0 it is `E1004, E1005`. On unknown C0 with `POLICY_HALT` the boot stops with `E_UNKNOWN_REV` before the PLL ramps.

## 3. 시작 코드

아래 코드의 `TODO` 주석이 있는 함수 14개를 채운다. mock 과 테이블, 하네스는 주어져 있다.

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/SIV-A05_st && /tmp/SIV-A05_st
```

스텁 상태의 실제 결과 (경고 없이 빌드되고 일부 FAIL, exit 1):

```text
10/69 passed
exit=1
```

## 4. 힌트

<details><summary>힌트 1 — 리비전을 정수 하나로</summary>

`rev = major<<8 | minor` 하나로 만들면 비교가 전부 정수 비교가 된다. 글자·숫자 문자열 비교(`"A9" > "B0"`?)는 함정이다. `rev_max` 의 "상한 없음"은 `0xFFFF` 같은 sentinel 로 둔다.

</details>

<details><summary>힌트 2 — select 와 apply 를 분리</summary>

선택은 순수 함수(테이블 + ID + 퓨즈)로 만들면 칩 없이 테이블 테스트가 된다. 적용에는 세 단계가 있다: apply 호출 → 선언된 필드 read-back → mock 쓰기 로그와 선언된 mask 비교. read-back 없이는 "캐시가 이미 켜져서 쓰기가 무시된" 실패를 잡을 수 없다.

</details>

<details><summary>힌트 3 — phase 는 위상 정렬보다 먼저</summary>

의존 대상이 **더 늦은** phase 면 어떤 순서도 불가능하다(사이클이 아니라 표의 오류). 먼저 이 "phase 역전"을 거르고, 그다음 phase 안에서만 위상 정렬한다. 더 이른 phase 에 있는 의존 대상은 이미 끝난 것이므로 조건을 만족한다. 선택 안 된 ID 에 대한 `after` 는 그냥 무시한다.

</details>

<details><summary>힌트 4 — 미지의 리비전과 범위</summary>

A2 는 숫자로는 A1 과 B0 사이에 있어 범위 비교만으로는 "known" 처럼 보인다. known 은 **목록 멤버십**으로 판정한다. LAB 정책에서 "닫힌 범위의 WA" 를 켜지 않는 이유는 먼저 스스로 생각해 보고 §8 Q2 에서 확인하자.

</details>

<details><summary>힌트 5 — 감사의 핵심 축</summary>

"표가 영향이라 하는가" × "WA 를 끈 probe 에서 실패가 나는가" 의 2×2 표다. `on` 쪽 결과만으로는 절대 STALE 를 판단할 수 없다 (WA 가 켜져 있으면 버그가 있든 없든 실패가 없다).

</details>

<details><summary>힌트 6 — probe 가 공허한 경우</summary>

`exercised` 가 0 이면 그 결과는 "버그 없음"이 아니라 "시험 안 함"이다. WA 가 켜졌을 때 `exercised==0` 이면 masked, WA 가 꺼졌을 때도 0 이면 INCONCLUSIVE.

</details>

## 5. 풀이

### Part A — 디코드, 범위, 집합

**디코드에서 살펴볼 것은 세 가지다.**

| 관찰값 | 처리 | 이유 |
|---|---|---|
| `0x00000000`, `0xFFFFFFFF` | `E_BUS` | 버스 미준비·디코드 에러의 전형적인 응답. ID 처럼 해석하면 안 됨 |
| part 불일치 | `E_WRONG_PART` | 이 펌웨어는 이 칩용이 아니다. errata 를 논할 수 없다 |
| fab 코드 | 리비전 아님 | fab 만 달라도 errata 가 바뀌면 그건 fab 을 applies-if 로 표현할 일이다 |

실제 ID 레지스터 예: Arm Cortex 코어의 `MIDR_EL1` / Cortex-M 의 `CPUID` 는 `Implementer[31:24], Variant[23:20], Architecture[19:16], PartNum[15:4], Revision[3:0]` 구조이고 errata 문서의 "rNpM" 표기가 Variant N, Revision M 이다. JTAG `IDCODE` 는 `Version[31:28], Part[27:12], Manufacturer[11:1], 1` 구조다. 숫자 ID 와 사람이 부르는 리비전 이름의 매핑은 벤더 표로 봐야 한다 — 예를 들어 STM32F4 의 RM(RM0090) `DBGMCU_IDCODE` REV_ID 표에서는 `0x1000 = A`, `0x1001 = Z` 처럼 문자 순서와 숫자 순서가 어긋난다.

**리비전을 정수로:** `REV(major, minor) = major<<8 | minor`. A9 < B0 (0x0009 < 0x0100) 이 자동으로 맞는다. 문자열 비교는 "A9" > "A10" 함정이 있고, `major*10+minor` 는 minor ≥ 10 에서 깨진다.

**선택 집합이 정확해야 한다**는 건 *빠짐 없음 + 남는 것 없음* 둘 다다. 테스트는 A0/A1/B0 × SKU 표를 `ids_eq` 로 비교하고, 레지스터로도 확인한다.

| 칩 / SKU | 기대 적용 |
|---|---|
| A0 전체 | E1001, E1002, E1006, E1003, E1004 |
| A0 (L2 없음) | E1001, E1006, E1003, E1004 |
| A0 (PCIe 없음) | E1001, E1002, E1006, E1003 |
| A1 전체 | E1001, E1004 |
| B0 전체 | E1004, E1005 |
| B0 (PCIe 없음) | E1005 |

B0 에서는 `L2_CHICKEN == 0`, `PLL_CFG == 0`, `DMA_CTRL.BURST == 16` 이어야 한다. "B0 에서는 안 켜도 되는데 켜도 나쁠 건 없지" 는 틀린 생각이다. A0 전용 chicken bit 는 B0 에서 다른 뜻으로 재사용되었을 수 있고, 성능 페널티(이 표의 E1001 은 burst 절반)를 공짜로 치르게 된다.

`errata_apply_one` 의 세 검사가 왜 다 필요한지:

| 단계 | 잡는 것 | 빠지면 |
|---|---|---|
| `apply()` 반환값 | WA 함수 자체의 실패 (예: 하드웨어 응답 없음) | 침묵 실패 |
| read-back (`reg & mask == val`) | 잠긴 레지스터, 잘못된 phase | "적용됨" 로그 + 버그 그대로 |
| wr_log vs 선언 mask | WA 가 선언 밖의 비트/레지스터를 건드림 | 충돌 검사(Part B)가 거짓말이 됨 |

### Part B — 순서, 의존성, 멱등성, 충돌, 미지의 리비전

**phase 가 첫 번째 축인 이유.** 워크어라운드의 효력은 "언제 쓰느냐" 에 달린 경우가 많다. 실제 사례:

- 32비트 Arm Linux 의 `arch/arm/mm/proc-v7.S` 는 MMU 를 켜기 전에 Cortex-A9/A15 errata 비트(CP15 ACTLR/diagnostic 레지스터)를 쓴다.
- TF-A 는 reset handler 단계에서 코어별 errata 를 적용하고 빌드 플래그(`ERRATA_A53_835769` 같은 형태)로 포함 여부를 정하며, 적용 여부(applied/missing)를 부팅 로그로 보고한다.
- Linux 의 PCI quirk 는 `DECLARE_PCI_FIXUP_EARLY/HEADER/FINAL/ENABLE/RESUME` 처럼 phase 가 API 에 박혀 있다 — RESUME 이 별도인 이유는 연습 3 에서 다룬다.

mock 은 이를 두 규칙으로 흉내 낸다: `clk_fast` 뒤 `PLL_CFG` 쓰기 무시, `cache_on` 뒤 `L2_CHICKEN` 쓰기 무시. 늦게 쓰면 쓰기가 무시되면서 `late_writes` 가 올라가고 read-back 이 실패한다. 테스트에서 E1002 를 `PH_LATE` 로 옮기면 정확히 `E_NOT_STUCK`, `fail_id=1002`, `late_writes=1` 이 나온다.

**순서 결정 절차**:

```text
1. phase 역전 검사: 의존 대상이 나보다 늦은 phase → E_PHASE_INV
2. phase 0..N 각각에 대해:
     그 phase 의 아직 안 끝난 항목 중, 의존이 모두 끝난(또는 선택 안 된) 것을 table 순서로 하나 꺼낸다 (Kahn)
     못 꺼내는데 남아 있으면 E_CYCLE
```

A0 전체의 결과: `E1003, E1006, E1002, E1001, E1004`. 테이블에서는 E1006 이 E1003 앞이지만 `requires_id=1003` 때문에 뒤로 간다. mock 은 `PLL_CFG.SSC_OFF` 가 `LOCK_FILT` 없이 켜지면 `order_viol` 을 올리는데, requires 선언을 지운 테이블로 돌리면 `order_viol == 1` 이 나온다(테스트로 확인) — 의존 선언이 **동작을 바꾼다**.

**두 종류의 의존성.** `after` 는 순서만(둘 다 적용될 때), `requires_id` 는 존재까지 요구한다. 예: E1006 의 범위를 A0..A1 로 바꾸면 A1 에서 E1003 이 선택되지 않아 `E_REQUIRES` — 필수 의존인데 이 칩에서 표가 그걸 제공하지 않으므로 **침묵하지 않고 실패**한다.

**충돌.** 선언된 `(reg, mask, val)` 끼리 비교한다. 겹치는 비트에 다른 값이면 충돌, 같은 값이면 공존 가능하다. "A0 에서만 동시에 선택되는" 충돌도 있으므로 전체 테이블이 아니라 **선택된 집합**에서 본다(테스트: 같은 테이블이 A0 에선 `E_CONFLICT`, B0 에선 OK). 왜 부팅 때 실패인가? 두 WA 중 어느 쪽이 이겨야 하는지 코드가 결정하면 안 된다. 사람이 errata 시트를 읽고 결정할 일이고, CI 에서 전 칩·SKU 조합 부팅을 돌려 미리 잡는 것이 정상이다.

**멱등성.** 부트 경로가 warm reset, resume, 또는 재시도로 WA 를 두 번 돌릴 수 있다. `reg ^= bit` 또는 `trim += 3` 은 두 번 돌면 망가진다. 프레임워크는 `errata_check_idempotent` 로 적용 두 번 후 레지스터가 같은지 비교한다. 함정: 멱등성 검사를 통과해도 전역 "이미 적용함" 플래그는 전원 도메인 재시작 후 거짓말을 한다 — 연습 3.

**미지의 리비전** 정책은 설계 결정이다. 선택지를 비교하면:

| 정책 | 프로덕션 | 랩 | 위험 |
|---|---|---|---|
| 조용히 표대로 진행 | ✗ | ✗ | 새 칩 버그가 그대로 드러나거나, 범위에 안 맞는 WA 가 몰래 빠짐 |
| 전부 적용 | ✗ | ✗ | 닫힌 범위 WA 의 chicken bit 는 새 리비전에서 재사용/제거되었을 수 있다. 쓰는 것 자체가 위험 |
| HALT (fail-closed) | ○ | 필요 시 | 부트가 안 되지만 안전. 클럭 올리기 전이 좋다 |
| 열린 범위만 적용 + tainted (LAB) | ✗ | ○ | 검증 데이터가 "오염됨" 으로 표시되어 프로덕션 근거로 못 쓰게 됨 |

닫힌 범위 항목을 안 켜는 근거는 "그 버그는 그 이후 리비전에서 고쳐졌다" 는 표의 주장이다. 다만 Part C 의 B0-BR 처럼 그 주장이 틀릴 수 있으므로 tainted 표시와 함께 쓰고, 실제 트리아지는 Part C 의 감사로 넘긴다. 실제 Linux 는 표에 맞는 MIDR 에만 WA 를 켠다(`ERRATA_MIDR_ALL_VERSIONS` 처럼 열린 항목은 예외). 부트로더처럼 사람이 없는 경우라면 HALT 혹은 보수적 프로파일이 더 흔한 선택이다.

```text
boot: SNS-100 rev=A1 fab=0x11 tainted=0 status=0 applied=[E1001,E1004]
```

### Part C — ECO 후 제거: 버그가 정말 사라졌는가

ECO(Engineering Change Order) 는 메탈 레이어를 바꿔 버그를 고치거나 새 스핀에 반영하는 변경이다. errata 항목을 범위에서 빼면(`rev_max` 를 좁히면) WA 가 사라진다. **제거의 근거가 "ECO 노트에 고쳤다고 써 있음" 뿐이면 안 된다.** 테스트로 증명해야 한다. 이 감사의 판정표:

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">감사 판정 (WA OFF 에서 probe, 표의 applies 와 교차)</text>
  <text x="290" y="48" text-anchor="middle" fill="currentColor">WA OFF: 실패 관측 없음</text>
  <text x="560" y="48" text-anchor="middle" fill="currentColor">WA OFF: 실패 관측 있음</text>
  <rect x="150" y="60" width="270" height="86" class="f-ok"/>
  <rect x="425" y="60" width="270" height="86" class="f-warn"/>
  <rect x="150" y="150" width="270" height="86" class="f-warn"/>
  <rect x="425" y="150" width="270" height="86" class="f-ok"/>
  <text x="285" y="92" text-anchor="middle" fill="currentColor" font-weight="bold">CLEAN</text>
  <text x="285" y="112" text-anchor="middle" fill="currentColor" font-size="12">표: 비대상 / 측정: 없음 — 정상</text>
  <text x="560" y="92" text-anchor="middle" fill="currentColor" font-weight="bold">MISSING</text>
  <text x="560" y="112" text-anchor="middle" fill="currentColor" font-size="12">표: 비대상인데 버그 있음 — 표 오류 또는 회귀</text>
  <text x="560" y="130" text-anchor="middle" fill="currentColor" font-size="12">(B0-BR: A1 수정이 B0 에 없음)</text>
  <text x="285" y="182" text-anchor="middle" fill="currentColor" font-weight="bold">STALE</text>
  <text x="285" y="202" text-anchor="middle" fill="currentColor" font-size="12">표: 대상인데 버그 없음 — 제거 후보</text>
  <text x="285" y="220" text-anchor="middle" fill="currentColor" font-size="12">(단, exercised&gt;0 이고 모든 코너에서)</text>
  <text x="560" y="182" text-anchor="middle" fill="currentColor" font-weight="bold">NEEDED</text>
  <text x="560" y="202" text-anchor="middle" fill="currentColor" font-size="12">WA ON 실패 0 이면 정상 적용</text>
  <text x="560" y="220" text-anchor="middle" fill="currentColor" font-size="12">WA ON 도 실패면 WA_BROKEN</text>
  <text x="140" y="108" text-anchor="end" fill="currentColor" font-size="12">표: 비대상</text>
  <text x="140" y="196" text-anchor="end" fill="currentColor" font-size="12">표: 대상</text>
  <text x="10" y="262" fill="currentColor" font-size="12">exercised==0 (트리거를 건드리지 못함) 이면 어느 칸도 말할 수 없다 → INCONCLUSIVE. WA ON 쪽의 exercised==0 은 masked.</text>
</svg>
```

**왜 WA OFF 로 재야 하는가.** E1004 는 기능을 끄는 WA 다(`L0S_EN=0`). WA 를 켠 상태에서 probe 를 돌리면 L0s 를 아예 안 쓰므로 트리거가 없다. 그러면 B0(버그 있음)와 B0-ECO(버그 없음)의 **WA ON 측정치는 완전히 같다.** 측정값:

```text
[2] E1004 probe: WA on vs WA off
chip     WA on (ex/fail)        WA off (ex/fail)       verdict
A0       0/0                    3072/3072              NEEDED
B0       0/0                    3072/3072              NEEDED
B0-ECO   0/0                    3072/0                 STALE
```

(실행: `cc -std=c11 -Wall -Wextra -Werror -O2 demo_limits.c -o /tmp/SIV-A05_demo && /tmp/SIV-A05_demo`.)

즉 **WA ON 회귀 통과는 ECO 제거의 근거가 될 수 없다.** 실제 랩에서는 이 때문에 `WA disable` override(부트 파라미터, 디버그 빌드, 시험용 플래그)를 프레임워크에 일부러 만들어 둔다. 대신 프로덕션 빌드에서는 그 override 를 지워야 한다.

**전체 감사 결과** (실측):

```text
[1] audit matrix (corners -40/25/105C, 1024 trials each)
       A0            A1            B0            B0-ECO        B0-BR         
E1001  NEEDED        NEEDED        CLEAN         CLEAN         CLEAN         
E1002  NEEDED        CLEAN         CLEAN         CLEAN         MISSING       
E1006  NEEDED        CLEAN         CLEAN         CLEAN         CLEAN         
E1003  NEEDED        CLEAN         CLEAN         CLEAN         CLEAN         
E1004  NEEDED*       NEEDED*       NEEDED*       STALE*        NEEDED*       
E1005  CLEAN         CLEAN         NEEDED        NEEDED        NEEDED        
(* = WA 가 트리거를 없애서 on-probe 는 exercised=0)
```

읽는 법: E1001 은 task 요구 그대로 A0 에서 `off(fail=3072)`, B0 에서 `off(fail=0)`. solution 은 이를 숫자로 `CHECK` 한다. B0-BR 의 E1002 는 `MISSING`, 즉 **표가 틀렸다**는 신호다(메탈 수정이 후속 스핀에 포함 안 됨). 실제로 base-layer 스핀은 어느 DB 시점에서 분기하느냐에 따라 앞선 메탈 수정을 포함하지 않을 수 있다 (연습 1 의 비연속 집합).

**리포트 형식** (실제 출력):

```text
stale : FAIL erratum=E1001 chip=B0 range=A0..B0 verdict=STALE off(ex=3072,fail=0) on(ex=3072,fail=0)
miss  : FAIL erratum=E1002 chip=B0-BR range=A0..A0 verdict=MISSING off(ex=3072,fail=46) on(ex=3072,fail=0)
```

첫 줄은 "표의 범위를 B0 까지 남겨 둔 채 잊어버린" 항목이다 (E1001 은 B0 에서 고쳐졌는데 표에는 A0..B0 로 되어 있는 가상의 실수). `off(ex=..., fail=...)` 를 함께 찍는 이유는 판정의 증거를 그대로 남기기 위해서다 — 디버그 엔지니어가 "exercised=3072 인데 fail=0" 을 보고 신뢰할지 판단할 수 있다.

**ECO 제거 절차** (`eco_propose_rev_max` 를 사람이 확인하고 커밋):

```text
1. lineup = 지원하는 리비전 전부 (A0, A1, B0-ECO), 코너 3, trials 1024 로 감사
2. WA OFF 실패가 관측된 가장 높은 리비전을 찾는다
   마지막 리비전에서도 보이면 OPEN 유지 (제거 불가)
   없으면 REV_NONE (probe 가 애초에 재현 못 함 → 제거가 아니라 probe 먼저 의심)
   그 외 그 리비전 = 새 rev_max 제안
3. 사람이 errata 시트/ECO 노트와 대조 후 커밋, status = ST_FIXED
4. 커밋 후 재감사: B0-ECO 는 CLEAN (WA 제거), A0/A1 은 NEEDED 유지
   이전 B0(버그 있음) 를 재실행하면 MISSING 으로 뜬다 — "표를 고치면 옛 칩을 지원 못 하는" 것을 잡아준다
```

마지막 단계가 안전망이다. `errata_lint` 는 정적 전제(중복, 잘못된 범위, 함수 없는 항목, status 와 범위 불일치)를 CI 에서 미리 거른다.

**이 방법의 한계 — 측정값으로.** 감사가 "버그 없음" 이라 해도 그건 *이 조건, 이 횟수에서 관측되지 않았다* 일 뿐이다.

1. **코너 누락.** E1003 은 저온에서만 터진다. 25°C 단독 감사는 A0 에서도 `STALE` 로 오판한다(테스트: `ROOM` 옵션에서 `V_STALE`, 3 코너에서 `V_NEEDED`). 코너는 errata 시트의 트리거 조건에서 와야 한다.
2. **간헐 버그와 시도 횟수.** N 번 시도에서 0 번 관측될 확률은 `(1-p)^N` 이다. 5000 시드 측정(demo_limits):

```text
[3] intermittent bug: P(0 failures in N trials) measured over 5000 seeds (WA off)
1/p      N        measured     (1-p)^N     
64       1        0.9860       0.9844      
64       16       0.7804       0.7773      
64       64       0.3632       0.3650      
64       256      0.0178       0.0177      
64       1024     0.0000       0.0000      
64       4096     0.0000       0.0000      
1024     1        0.9996       0.9990      
1024     16       0.9840       0.9845      
1024     64       0.9374       0.9394      
1024     256      0.7832       0.7787      
1024     1024     0.3776       0.3677      
1024     4096     0.0182       0.0183      
```

`solution.c` 의 체크는 N=32, p=1/64 에서 2000 시드의 STALE 오판율이 이론값 ±3σ 안인지(실측 0.6040, 이론 0.6041)를 본다. 허용오차를 3σ 로 잡은 이유: 시드는 고정이지만 이것은 표본 분포이므로 ±1σ 로 잡으면 유의한 편차 없이도 실패할 수 있다. 그리고 N=4096 에서는 200 시드 중 오판 0.

"p 를 모르는데 N 을 어떻게 정하나?" — 95% 신뢰로 `p ≥ p0` 를 배제하려면 `N ≥ ln(0.05)/ln(1-p0) ≈ 3/p0` (rule of three, SIV-M06 의 BER 식과 같은 뿌리). 그래서 "ECO 후 N 시간 무실패" 라는 표현은 어떤 p 까지 배제하는지 수치를 달아야 의미가 있다. N 이 크면 랩 시간이 든다(가정: trial 1초 × 코너 3 × 칩 5개 × 1024 trials ≈ 4시간/항목).

3. **트리거가 시험에 없는 경우.** probe 는 errata 설명의 트리거만 건드린다. 버그의 실제 조건이 시트와 다르면(흔하다) 감사는 조용히 CLEAN 을 준다. `exercised` 는 "트리거 시도 횟수" 일 뿐 "실제 트리거 조건에 닿았다" 의 증명이 아니다. 내부 카운터(arb 충돌 count, L0s 진입 count)로 확인하는 것이 맞다(SIV-M13 커버리지).
4. **WA 끄기가 불가능한 경우.** 퓨즈로 WA 가 영구 적용된 칩은 WA OFF 를 잴 수 없다 → 감사는 `INCONCLUSIVE` 쪽으로 가야 한다.

마지막 항: masked 표시는 "그래서 WA 가 무효다" 가 아니라 "WA ON 측정이 시험의 근거가 되지 않는다" 다. E1004 는 기능 끄기 WA 라 masked 이며 정상이다. 대신 "L0s 를 못 쓰는 비용"(전력)을 리포트에 적어야 한다.

Part A/B/C 전체 실제 결과:

```text
== Part A: decode, range, exact set ==
== Part B: order, conflict, idempotency, unknown rev, boot ==
boot: SNS-100 rev=A1 fab=0x11 tainted=0 status=0 applied=[E1001,E1004]
== Part C: ECO removal audit ==
stale : FAIL erratum=E1001 chip=B0 range=A0..B0 verdict=STALE off(ex=3072,fail=0) on(ex=3072,fail=0)
miss  : FAIL erratum=E1002 chip=B0-BR range=A0..A0 verdict=MISSING off(ex=3072,fail=46) on(ex=3072,fail=0)
limit : p=1/64 N=32 false-STALE 0.6040 (theory 0.6041)
69/69 passed
```

부팅 로그 (demo_limits, 실제 출력):

```text
[4] boot logs
A0/HALT rc=0   clk_fast=1 SNS-100 rev=A0 fab=0x11 tainted=0 status=0 applied=[E1003,E1006,E1002,E1001,E1004]
A1/HALT rc=0   clk_fast=1 SNS-100 rev=A1 fab=0x11 tainted=0 status=0 applied=[E1001,E1004]
B0/HALT rc=0   clk_fast=1 SNS-100 rev=B0 fab=0x11 tainted=0 status=0 applied=[E1004,E1005]
C0/HALT rc=-3  clk_fast=0 SNS-100 rev=C0 fab=0x11 tainted=0 status=-3 applied=[]
C0/LAB  rc=0   clk_fast=1 SNS-100 rev=C0 fab=0x11 tainted=1 status=0 applied=[E1004]
A2/LAB  rc=0   clk_fast=1 SNS-100 rev=A2 fab=0x11 tainted=1 status=0 applied=[E1004]
```

`rc=-3` = `E_UNKNOWN_REV`, `clk_fast=0` 은 PLL ramp 전에 멈췄다는 뜻이다.

### 실제 현장과의 연결: Linux arm64 errata

Linux `arch/arm64/kernel/cpu_errata.c` 의 `arm64_errata[]` 는 이 레슨의 테이블과 같은 구조다.

| 이 레슨 | Linux arm64 |
|---|---|
| `id` | `.desc` + capability 번호 (`ARM64_WORKAROUND_...`), Kconfig `CONFIG_ARM64_ERRATUM_xxxxxx` |
| `rev_min..rev_max` | `ERRATA_MIDR_RANGE`, `ERRATA_MIDR_REV_RANGE(model, var, rmin, rmax)`, `struct midr_range` (`model`, `rv_min`, `rv_max`) |
| `applies_if` | `.matches` 콜백 (`is_affected_midr_range`, 범위 목록용 `is_affected_midr_range_list` 등) |
| `apply` | `.cpu_enable` 콜백 또는 alternatives 코드 패치 |
| `rev_max==REV_OPEN` | `ERRATA_MIDR_ALL_VERSIONS` |
| SKU/기능 조건 | matches 안에서 추가로 확인 |

예: Cortex-A53 의 erratum 835769, 843419 는 컴파일러/링커 옵션(`-mfix-cortex-a53-835769`, `--fix-cortex-a53-843419`)으로도 처리되고 Kconfig 로 끌 수 있다. 즉 "워크어라운드" 는 레지스터 쓰기만이 아니라 **툴체인 옵션, 코드 패치, 마이크로코드, 드라이버 fallback** 까지 포함한다.

부팅 뒤 올라오는 CPU 는 연습 2 의 문제다: `ARM64_CPUCAP_LOCAL_CPU_ERRATUM` 타입은 부팅 시점 CPU 중 하나라도 필요하면 시스템 전체로 켜고, 그 뒤 올라온 CPU 가 시스템이 안 켠 WA 를 요구하면 그 CPU 를 거절한다. `Documentation/arch/arm64/silicon-errata.rst` 는 커널이 아는 errata 목록이다 — 사람이 읽는 표와 코드가 짝을 이룬다는 점이 이 레슨의 `errata_lint` + 감사 리포트와 같은 생각이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/SIV-A05_so && /tmp/SIV-A05_so
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/SIV-A05_san && /tmp/SIV-A05_san
```

실측: `69/69 passed`, exit 0, ASan/UBSan 깨끗함. 측정 데모는 `demo_limits.c` (solution.c 를 include 해 CHECK 없이 측정만 한다).

## 7. 자주 하는 실수

**실수 1 — read-back 없이 "적용 완료" 로 기록한다.**
왜 틀렸나: 잠긴 레지스터나 늦은 phase 의 쓰기는 조용히 무시된다. 로그는 applied, 하드웨어는 unapplied.
```c
/* 고친 코드: errata_apply_one */
if ((reg_read32(s, e->reg) & e->mask) != (e->val & e->mask)) { ctx->fail_id = e->id; return E_NOT_STUCK; }
```

**실수 2 — 범위 비교로 "known" 을 판정한다.**
왜 틀렸나: A2 는 만들어진 적 없는 리비전이지만 `A0..B0` 범위 안이다. 범위는 "영향" 의 표현이고 known 은 멤버십이다.
```c
for (int i = 0; i < nk; i++) if (known[i] == c->rev) return E_OK;
return E_UNKNOWN_REV;
```

**실수 3 — WA 를 켠 채로 ECO 를 검증한다.**
왜 틀렸나: 버그가 있어도 없어도 WA 가 켜져 있으면 결과는 같다. WA OFF 경로가 프레임워크에 **있어야** 한다.
```c
probe_t off = probe_run(t, n, e, m, temp, seed, trials, fuse, /*wa_on=*/false);
```

**실수 4 — exercised 를 안 본다.**
왜 틀렸나: 트리거를 건드리지 못한 probe 의 "실패 0" 은 STALE 가 아니라 INCONCLUSIVE 다.
```c
else if (a->off_ex == 0) a->v = V_INCONCLUSIVE;   /* off_fail==0 판단보다 먼저 */
```

**실수 5 — 테이블 순서를 적용 순서로 쓴다.**
왜 틀렸나: 표는 ID 나 사람 편의로 정렬될 뿐이다. E1006 이 E1003 보다 앞에 있어도 phase·의존성을 따라야 한다.
```c
int k = errata_order(sel, m, ord);      /* 적용은 ord 순서로, phase 별 */
```

**실수 6 — 충돌을 전체 테이블에서 찾는다.**
왜 틀렸나: 한 리비전에서만 겹치는 항목은 다른 리비전에서 정상이다. 선택된 집합에서 검사한다.
```c
if (errata_find_conflict(sel, m, &a, &b)) { /* table 이 아니라 sel */ }
```

**실수 7 — 전역 플래그로 멱등성을 대신한다.**
왜 틀렸나: 전원 도메인 복귀로 레지스터가 사라져도 플래그는 남는다. epoch 단위로 잊어야 한다 (연습 3).
```c
if (w->applied_epoch != d->epoch) { d->reg |= WA_BIT; w->applied_epoch = d->epoch; }
```

**실수 8 — fab 코드나 예약 비트를 리비전에 섞는다.**
왜 틀렸나: 같은 리비전이 fab 마다 다른 값으로 읽혀 표에 맞지 않게 된다. fab 은 별도 predicate 로 다룬다.
```c
c->rev = REV(c->major, c->minor);       /* fab 은 c->fab 에만 */
```

## 8. 면접 Q&A

<details><summary>Q1. errata 테이블의 "영향 리비전" 을 범위로 쓰면 뭐가 문제인가요?</summary>

리비전 사이의 상속이 항상 선형은 아니다. A1 에 메탈로 넣은 수정을 B0 에서 분기한 DB 에 안 넣었으면 "A0, B0 영향 / A1 비영향" 이 된다. 범위로 쓰면 A1 도 끌려 들어온다. 그래서 known 리비전 비트마스크나 명시적 목록을 쓰고, 범위는 편의 표현으로만 둔다 (연습 1).

**꼬리질문: 그래도 Linux 는 범위를 많이 쓰지 않나요?** 코어 IP 는 rNpM 이 선형인 편이다. 그래도 `is_affected_midr_range_list` 같은 목록 형태가 있다. SoC 통합 수준의 버그는 더 비선형적이다.

**꼬리질문: known 목록 유지 비용은?** 새 리비전마다 표 + known 을 고쳐야 하고, 그 작업을 안 하면 HALT 가 난다. 이것이 의도한 비용이다. "모르면 멈춤" 이 없으면 아무도 표를 갱신하지 않는다.

</details>

<details><summary>Q2. 미래 리비전 C0 가 나왔는데 펌웨어가 모른다면 어떻게 동작해야 하나요?</summary>

제품 빌드는 fail-closed(HALT) — 안전 관련 보드에서 "아마 괜찮겠지" 로 진행하면 책임을 질 수 없다. 랩 빌드는 열린 항목만 적용 + `tainted` 표시로 진행해 새 실리콘 bring-up 을 막지 않는다. tainted 가 데이터에 찍혀야 그 측정치를 프로덕션 근거로 쓰지 않는다.

**꼬리질문: 왜 닫힌 범위 WA 를 켜지 않나요?** 표는 "그 버그는 이후 리비전에서 없다" 고 말한다. 또 A0 전용 chicken bit 주소/의미는 C0 에서 재사용될 수 있어 쓰는 것 자체가 위험하다.

**꼬리질문: 그럼 안 켜서 나는 버그는?** 그래서 tainted 와 C0 audit 를 세트로 돌린다. 감사에서 MISSING 이 뜨면 바로 표를 고친다. 비용은 first-silicon 첫 주의 트리아지로 옮겨진다.

</details>

<details><summary>Q3. ECO 후 "그 버그가 고쳐졌다" 를 어떻게 증명하나요?</summary>

WA OFF 로 트리거 조건을 재현하는 probe 를 돌려 실패가 0 이어야 하고, 반대로 같은 probe 가 이전 리비전(A0)에서는 실패해야 한다. 둘 다 있어야 probe 가 살아 있다는 뜻이다(A0 에서 probe 가 안 잡으면 B0 "통과"는 아무 의미 없다). 조건은 errata 의 트리거 코너(온도·전압·트래픽)를 포함해야 한다.

**꼬리질문: 간헐 버그라면?** 0 번 관측은 `p ≤ 3/N` (95%) 을 뜻할 뿐이다. 예: 1024 시도 무실패는 p 약 0.3% 미만을 배제. 측정표는 §5.

**꼬리질문: 그 시간은 누가 쓰나요?** 랩 시간이 진짜 비용이다. ECO 제거는 위험과 시간의 교환이다 — 대개 WA 비용이 큰 항목부터 제거한다. "제거해서 얻는 것"(burst 16 회복)을 숫자로 적어야 우선순위가 정해진다.

</details>

<details><summary>Q4. 워크어라운드를 MMU/캐시 켜기 전에 적용해야 한다면 어떻게 강제하나요?</summary>

(1) 표에 phase 를 명시하고 프레임워크가 phase 별로 호출, (2) 적용 후 read-back 으로 실패를 잡음. 실제로는 ROM 다음 첫 스테이지 코드에서 호출하고, MMU 전 코드는 위치 독립·데이터 접근 제약이 있다. arm64 kernel 초기 코드는 그런 제약 때문에 특정 영역에 분리되어 있고, TF-A 는 reset handler 에서 처리한다.

**꼬리질문: read-back 으로 안 잡히는 경우는?** 쓰기는 들어갔는데 시퀀스가 틀려 효력이 없는 경우(예: 캐시를 깨끗이 안 한 채 설정 변경). 그래서 phase 만으로는 부족하고 WA 후 probe(또는 내부 상태 카운터) 검증이 있어야 한다.

**꼬리질문: WA 가 다른 코어에도 필요하면?** per-CPU 스코프다. 각 코어의 reset 경로에서 돌리고, 시스템 전역 플래그는 "모든 코어에서 적용되었는지" 집계용으로만 쓴다 (연습 2).

</details>

<details><summary>Q5. 워크어라운드 때문에 성능이 떨어지면 어떻게 하나요? 언제 리스핀을 요구하나요?</summary>

WA 는 비용이 있다: E1001 은 burst 절반(이 레슨의 대표값), E1004 는 L0s 저전력 상태 포기. 프레임워크는 항목당 비용을 리포트에 함께 남겨야 한다 — "이 errata 들 때문에 메모리 대역폭 X% 손실, 대기 전력 Y mW 증가" 라는 표가 리스핀 의사결정 입력이다.

**꼬리질문: 분류 기준은?** 데이터 손상 · 복구 불가 hang · WA 불가 · 양산 일정에 영향이 우선이다. Arm 의 SDEN 은 Category A/B/C 로 심각도를 나눈다. 내부 프로젝트라도 severity/WA 가능 여부/비용을 함께 적는다.

**꼬리질문: WA 가 양산에 들어가면 "제거" 는 언제 하나요?** 수정 리비전이 양산되고 구 리비전의 지원이 끝나는 시점. 그 전까지 표의 항목은 남는다. 표에서 지우는 것과 WA 코드를 지우는 것은 다른 커밋이다.

</details>

<details><summary>Q6. 같은 레지스터를 만지는 워크어라운드 두 개가 충돌하면요?</summary>

선언된 footprint 로 CI 에서 정적 탐지한다. 값이 같으면 공존, 다르면 설계 문제이므로 `E_CONFLICT` 로 부팅 실패시키고 사람이 통합 WA 를 만든다. 런타임에는 write 감사로 선언 밖의 쓰기를 잡는다.

**꼬리질문: 선언이 틀렸다면?** 그게 write 감사의 존재 이유다. 선언 없이 건드린 비트가 있으면 `E_FOOTPRINT`. 실제 코드에서는 AXI 트랜잭션 트레이스(또는 레지스터 쉐도우 비교)로 같은 효과를 낸다.

**꼬리질문: 레지스터가 아니라 시간 조건 충돌은?** (예: 한쪽은 L2 clean 을 먼저, 다른 쪽은 나중을 요구) footprint 로는 안 잡힌다. 이런 건 `after`/phase 로 표현하고 조합을 실제로 돌려봐야 한다.

</details>

<details><summary>Q7. 워크어라운드가 버그를 숨겨서 검증이 망가지는 경우는 어떻게 다루나요?</summary>

기능을 끄는 WA(E1004)는 probe 를 공허하게 만든다. `masked` 로 표시하고 "WA ON 결과는 시험의 근거가 아님" 으로 분리한다. 동시에 검증 빌드에는 WA OFF 경로를 유지해 기능 자체를 계속 시험하고, 프로덕션 커버리지 리포트에서 "이 기능은 WA 때문에 비활성" 을 명시한다.

**꼬리질문: 그럼 비활성 기능의 커버리지는?** 커버리지 홀로 집계하고(SIV-M13), 고객 데이터시트에 기능 제한으로 반영한다. 조용히 빼면 안 된다.

**꼬리질문: WA 가 간헐 버그를 간헐적으로만 숨기면?** WA ON 실패 카운트를 양산 스트레스에서 계속 수집한다 — 0 이 아니면 WA 설계가 불충분(WA_BROKEN) 하고 그 분포는 SIV-M10 트리아지로 간다.

</details>

<details><summary>Q8. CHIP_ID 레지스터 자체가 틀렸다면요?</summary>

흔한 사고다: 메탈 ECO 가 로직을 고치면서 revision 타이오프를 안 올려 A1 이 A0 로 읽히거나, 플래시/퓨즈 기반 ID 가 프로그래밍 안 되어 0xFFFFFFFF 가 읽힌다. 대응은 (1) ID 체크 (`E_BUS`), (2) 패키지 마킹·ATE 로그와의 대조, (3) 감사의 MISSING/STALE 패턴(ID 는 A1 인데 A0 처럼 버그가 다 있음)으로 교차 확인이다.

**꼬리질문: 펌웨어가 그걸 자동으로 판단해도 되나요?** 권하지 않는다. 동작으로 리비전을 추정하면 그 probe 자체가 틀렸을 때 잘못된 WA 집합을 조용히 고른다. 판단은 ID 로 하고, 감사는 **신호**만 주며 결론은 사람이 내린다.

</details>

## 9. 추가 연습

### 연습 1 — 비연속 영향 집합

범위 `A0..B0` 대신 known 리비전 비트마스크로 "A0 와 B0 만 영향, A1 은 아님" 을 표현하라. 미지의 리비전은 항상 비영향으로 나와야 한다(대신 분류기가 먼저 잡아낸다).

<details><summary>풀이</summary>

`rev_slot()` 으로 known 인덱스를 찾아 mask 의 비트를 본다. 범위 표현은 A1 을 끌어들이므로 B0 DB 분기 사실을 표현할 수 없다.

</details>

### 연습 2 — 혼합 리비전 CPU 클러스터

각 CPU 의 필요 WA 집합이 다를 때 시스템이 켤 WA(부팅 CPU 전부의 합집합)를 계산하고, 늦게 올라온 CPU 를 받을지 결정하라(시스템이 안 켠 WA 를 필요로 하면 거절).

<details><summary>풀이</summary>

`enabled = OR(need_i)`, `late_ok = (need & ~enabled) == 0`. Linux arm64 의 late CPU 거절과 같은 조건이다. WA 를 나중에 켜는 건 이미 돌고 있는 코어에 대해 시스템 전역 alternatives 패치와 경쟁이 있어 일반적으로 하지 않는다.

</details>

### 연습 3 — 전원 도메인 복귀

`done` 플래그 방식과 epoch 방식을 비교하라. 도메인이 전원을 잃어 레지스터가 사라진 뒤에도 WA 가 다시 들어가야 한다.

<details><summary>풀이</summary>

`done` 는 소프트웨어 상태라 하드웨어 리셋을 모른다. 도메인 epoch 와 저장해 둔 epoch 를 비교하고, 다르면 다시 적용한다. PCI quirk 의 RESUME phase 가 왜 필요한지와 같다. (도메인 전원 시퀀싱 자체는 SIV-A11.)

</details>

### 연습 4 — 부팅 errata 리포트

각 항목의 상태를 `na / applied / MISSING` 로 만들고, 프로덕션에서는 MISSING 이 있으면 exit 1 로 돌려주는 리포트 함수를 작성하라.

<details><summary>풀이</summary>

`needed=false` → na, `needed && built_in` → applied, `needed && !built_in` → MISSING. TF-A 의 errata 리포트(applied/missing)와 같은 발상이다. 정책은 랩과 프로덕션이 다르다.

</details>

### 연습 5 — 필요한 시도 횟수

`(1-p)^N ≤ 1-CL` 을 만족하는 최소 N 을 구하는 `trials_for(p, cl)` 를 작성하라. 로그 없이 정수 반복으로.

<details><summary>풀이</summary>

`miss=1; while (miss > 1-cl) { miss *= 1-p; n++; }`. p=1/64, CL=95% → 191, p=1% → 299, p=0.1% → 2995. 작은 p 에서 `3/p` 근사와 맞는다(rule of three).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/SIV-A05_pr && /tmp/SIV-A05_pr
```

실측: `23/23 passed`, exit 0. ASan/UBSan 빌드도 같은 결과로 깨끗함.

## 10. 핵심 정리

- errata 표는 `{ID, 범위, applies-if, WA, 검증 probe, status, phase, footprint, 의존}` 이고, 표 순서가 아니라 phase → 의존 순으로 적용한다.
- 리비전은 `major<<8|minor` 정수로 비교하고, known 은 목록 멤버십으로 판정한다(A2 함정). `0x00000000`/`0xFFFFFFFF` 는 ID 가 아니다.
- 적용은 read-back + write 감사까지 해야 "적용됨" 이다. MMU·캐시·클럭 뒤에 쓰면 조용히 무시되는 레지스터가 있다.
- 미지의 리비전: 프로덕션 HALT, 랩은 열린 범위만 적용 + tainted. 잘못된 파트는 어떤 정책에서도 진행 금지.
- 표는 "선형 이력" 을 가정하지만 현실의 리비전은 분기한다. STALE/MISSING 은 감사로 찾는다.
- **ECO 제거는 WA OFF probe 로만 증명된다.** WA ON 통과는 버그 유무를 구분 못 한다(§5 E1004 측정).
- "관측 안 됨" ≠ "없음". 0 번 관측은 `p ≤ 3/N` 이고 코너가 빠지면 틀린다. exercised 가 0 이면 시험이 아니다.
- 기능을 끄는 WA 는 masked 로 표시하고 비용(성능·전력)을 리포트에 적는다.

## 참고자료

- Linux kernel `arch/arm64/kernel/cpu_errata.c` (`arm64_errata[]`, `MIDR_ALL_VERSIONS`, `is_affected_midr_range`), `Documentation/arch/arm64/silicon-errata.rst`.
- Arm Architecture Reference Manual (MIDR_EL1 필드), Cortex-A53 SDEN (errata 843419/835769).
- Trusted Firmware-A: CPU 별 errata 빌드 옵션(`docs/design/cpu-specific-build-macros.rst`), reset handler, errata 리포트.
- Linux `drivers/pci/quirks.c` (`DECLARE_PCI_FIXUP_*`).
- ST RM0090 (STM32F4, `DBGMCU_IDCODE` REV_ID), Errata sheet.
- 관련 레슨: SIV-B02 (mock MMIO), SIV-B09 (poll), SIV-B10 (bring-up 순서), SIV-M06 (신뢰 구간), SIV-M10 (트리아지), SIV-M13 (커버리지), SIV-A09 (Day-0 캠페인), SIV-A11 (전원 도메인).
