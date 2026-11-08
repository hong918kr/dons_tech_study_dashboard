---
id: SIV-M14
time: 90분
updated: 2026-11-08
---
# SIV-M14 ECC 검증: 오류 주입과 CE/UE 보고 확인

## 학습 목표

- 메모리 컨트롤러 ECC 의 쓰기 경로(encoder → 주입 XOR → 배열)와 읽기 경로(decoder → 정정 → 보고)를 그리고, 각 단계를 **무엇으로 관측하는지**(bypass 읽기, RDATA, 버스 응답, STATUS, 카운터, 오류 주소·신드롬, IRQ 라인) 말할 수 있다.
- 선형 부호의 성질 — **신드롬은 데이터와 무관하고 오류 패턴만의 함수** — 로 주입 마스크만 보고 기대 판정(CE/UE)·신드롬·비트 번호를 계산하고, 데이터 1비트 · 체크 1비트 · 2비트 조합 × walking 주소로 **128 케이스 매트릭스** 를 만든다.
- 주입이 "실제로 들어갔는지" 를 bypass 읽기로 먼저 확인해 **주입기 결함과 ECC 결함을 구별** 한다.
- 매트릭스(케이스마다 clear)로는 안 보이는 **첫 오류 캡처·overflow, 반복 읽기와 scrub, 카운터 포화** 를 별도 시험으로 잡는다.
- 정정 안 함, UE 를 CE 로 오정정, 체크비트 오류를 UE 로, 주소 비트 누락, UE 인터럽트·버스 에러 누락 등 10가지 결함을 주입해 **어느 시험·어느 단계가 잡는지** 와 원인 분류를 리포트로 낸다.

## 1. 문제

ECC 는 서버 DRAM·HBM·자동차 SoC 의 SRAM/DRAM 에서 "켜져 있다" 는 것만으로는 아무것도 보장하지 않는다 — **정정하는지, 못 고치는 오류를 UE 로 정확히 올리는지, 주소·카운터·인터럽트가 스펙대로인지** 를 오류를 일부러 넣어서 확인해야 하고, NVIDIA·Rivian 류 실리콘 검증 면접에서 "메모리 컨트롤러 ECC 를 어떻게 검증하겠느냐" 로 그대로 나온다.

여기서 검증 대상은 코덱 수학이 아니다 (코덱은 MEM-B07 의 SECDED(72,64)를 축약해 넣었다). 검증 대상은 **컨트롤러가 코덱 결과를 스펙대로 소프트웨어에 보고하는가** 다. 실제 컨트롤러는 이 그림처럼 생겼다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m14a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">DMC0 의 ECC 경로와 관측 지점 ①~⑥</text>
  <rect x="10" y="110" width="100" height="40" class="f-mut"/>
  <text x="60" y="128" text-anchor="middle" fill="currentColor">쓰기 데이터</text>
  <text x="60" y="143" text-anchor="middle" fill="currentColor" font-size="12">d (64b)</text>
  <line x1="110" y1="130" x2="133" y2="130" stroke="currentColor" marker-end="url(#m14a)"/>
  <rect x="135" y="110" width="100" height="40" class="f-acc"/>
  <text x="185" y="128" text-anchor="middle" fill="currentColor">Encoder</text>
  <text x="185" y="143" text-anchor="middle" fill="currentColor" font-size="12">c = enc(d) 8b</text>
  <line x1="235" y1="130" x2="254" y2="130" stroke="currentColor" marker-end="url(#m14a)"/>
  <circle cx="270" cy="130" r="14" fill="none" stroke="currentColor"/>
  <text x="270" y="135" text-anchor="middle" fill="currentColor">XOR</text>
  <rect x="200" y="34" width="150" height="44" class="f-warn"/>
  <text x="275" y="52" text-anchor="middle" fill="currentColor" font-size="12">INJ_MASK_LO/HI/CHK</text>
  <text x="275" y="68" text-anchor="middle" fill="currentColor" font-size="12">INJ_EN (one-shot)</text>
  <line x1="270" y1="78" x2="270" y2="114" stroke="currentColor" marker-end="url(#m14a)"/>
  <line x1="284" y1="130" x2="303" y2="130" stroke="currentColor" marker-end="url(#m14a)"/>
  <rect x="305" y="105" width="110" height="50" class="f-mut"/>
  <text x="360" y="126" text-anchor="middle" fill="currentColor">DRAM 배열</text>
  <text x="360" y="143" text-anchor="middle" fill="currentColor" font-size="12">64 + 8 비트</text>
  <line x1="415" y1="118" x2="448" y2="60" stroke="currentColor" marker-end="url(#m14a)"/>
  <rect x="450" y="34" width="150" height="44" class="f-mut"/>
  <text x="525" y="52" text-anchor="middle" fill="currentColor" font-size="12">RD_BYPASS 읽기</text>
  <text x="525" y="68" text-anchor="middle" fill="currentColor" font-size="12">원시 d' + BYP_CHK ①</text>
  <line x1="415" y1="130" x2="448" y2="130" stroke="currentColor" marker-end="url(#m14a)"/>
  <rect x="450" y="110" width="120" height="40" class="f-acc"/>
  <text x="510" y="128" text-anchor="middle" fill="currentColor">Decoder</text>
  <text x="510" y="143" text-anchor="middle" fill="currentColor" font-size="12">신드롬 s, p</text>
  <line x1="570" y1="130" x2="598" y2="130" stroke="currentColor" marker-end="url(#m14a)"/>
  <rect x="600" y="110" width="130" height="40" class="f-ok"/>
  <text x="665" y="128" text-anchor="middle" fill="currentColor" font-size="12">정정 → RDATA ②</text>
  <text x="665" y="143" text-anchor="middle" fill="currentColor" font-size="12">RRESP (UE=SLVERR) ③</text>
  <line x1="510" y1="150" x2="510" y2="188" stroke="currentColor" marker-end="url(#m14a)"/>
  <rect x="360" y="190" width="370" height="62" class="f-warn"/>
  <text x="372" y="210" fill="currentColor" font-size="12">보고: ECC_STATUS(CE/UE/OVF, W1C) · ECC_CNT ④</text>
  <text x="372" y="227" fill="currentColor" font-size="12">CE_ADDR/CE_SYND · UE_ADDR/UE_SYND (첫 오류 캡처) ⑤</text>
  <text x="372" y="244" fill="currentColor" font-size="12">IRQ → INTC 입력 20 (CE) / 21 (UE) ⑥</text>
  <text x="10" y="285" fill="currentColor">주입은 encoder 뒤에서 XOR → 배열에는 "오류가 난 코드워드" 가 저장된다. 읽기 경로만 검증 대상.</text>
</svg>
```

호스트(맥)에는 실리콘이 없으므로 `reg_read32/reg_write32` 와 `mem_write64/mem_read64` 뒤에 mock 을 둔다 (SIV-B02 의 mock 을 축약). 레지스터 맵 — 이름은 실제 컨트롤러(예: Synopsys uMCTL2 계열의 ECCSTAT / ECCCLR / ECCERRCNT / ECCCADDR / ECCUADDR / ECCPOISONADDR)를 단순화한 것이다.

| off | 이름 | access | 의미 |
|---|---|---|---|
| `0x000` | `ECC_CTRL` | RW | bit0 `ECC_EN`, bit1 `RD_BYPASS`(검사·보고 없이 원시값), bit2 `CE_IRQ_EN`, bit3 `UE_IRQ_EN` |
| `0x004` | `ECC_STATUS` | W1C | bit0 CE, bit1 UE, bit2 CE_OVF, bit3 UE_OVF (sticky) |
| `0x008` | `ECC_CNT` | RO | [15:0] CE 횟수, [31:16] UE 횟수, **0xFFFF 에서 포화** |
| `0x00C` | `ECC_CLR` | WO | bit0/bit1 = CE/UE 카운터 0 |
| `0x010`/`0x014` | `CE_ADDR`/`CE_SYND` | RO | STATUS.CE 가 0→1 될 때의 주소 / [7:0] 신드롬, [15:8] 정정한 비트 번호 |
| `0x018`/`0x01C` | `UE_ADDR`/`UE_SYND` | RO | UE 의 첫 오류 주소 / [7:0] 신드롬 |
| `0x020` | `INJ_CTRL` | RW | bit0 `INJ_EN`: `INJ_ADDR` 에 대한 다음 쓰기 1회에 마스크 XOR, 그 뒤 자동 0 |
| `0x024`~`0x030` | `INJ_ADDR`, `INJ_MASK_LO/HI/CHK` | RW | 주입 주소, 데이터 [31:0]/[63:32] 와 체크 [7:0] 의 뒤집을 비트 |
| `0x034` | `BYP_CHK` | RO | `RD_BYPASS` 읽기 때 저장돼 있던 체크 바이트 |
| `0x800` | `INTC_RAW` | RO | 인터럽트 컨트롤러 입력 레벨 (CE = 20, UE = 21) |

코덱 규칙: 코드워드 위치 1..71 중 2의 거듭제곱(1,2,4,…,64)에 체크비트 c0..c6, 나머지 64 위치에 데이터, c7 = 72비트 전체 짝수 패리티. 신드롬 바이트 = `s | p<<7` (s = 1 인 비트들의 위치 XOR, p = 전체 패리티). 비트 번호는 데이터 0..63, 체크 c0..c7 = 64..71.

mock 의 `fault_t` 로 넣는 결함 10가지: `no_correct`(보고는 하는데 데이터를 안 고침 — task 가 요구한 결함), `ignore_parity`(decoder 가 c7 무시), `chk_as_ue`, `ce_cnt_dead`, `cnt_wrap`, `addr_drop`(캡처 주소에서 비트 누락), `ce_capture_last`(첫 오류 대신 마지막 오류 캡처), `ue_irq_dead`, `ue_no_buserr`(UE 인데 OKAY 응답), `inj_chk_dead`(체크비트 주입기 고장).

### Part A — 기대값 계산, 매트릭스, 한 케이스의 시퀀스

- `ecc_expect(c, e)` — 주입 마스크(`mdata`, `mchk`)만 보고 판정(OK/CE/UE/스펙 밖), STATUS, CNT, IRQ 라인, 버스 응답, 신드롬(+ CE 면 비트 번호)을 계산한다.
- `build_matrix(addrs, n, seed, out, max)` — NONE(주소마다) · D1(데이터 64비트 전부) · C1(체크 8비트 전부) · D2(경계 쌍 2개 + 랜덤 30개) · DC2(데이터+체크) · C2(체크 2개, c7 포함) = **8 + 120 = 128 케이스**. 주소는 walking 비트 `{0x000,0x008,…,0x100,0x1F8}`. 데이터는 xorshift64 고정 시드.
- `ecc_run_case(c, f)` — PRE → 주입(INJ_EN 소모 확인) → **bypass 읽기로 착지 확인** → 일반 읽기 → STATUS, RESP, DATA, CNT, ADDR, SYND, LINE → W1C·카운터 clear → 0 확인. 첫 실패에서 멈추고 실패 기록을 남긴다. 끝나면 항상 `mc_cleanup`.
- 주입: `no_correct` 에서 D1 케이스가 `DATA` 단계, diff = 주입 비트로 실패해야 한다.

### Part B — 매트릭스가 못 보는 것: 캡처 순서, 반복 읽기, 포화

- `capture_test` — clear 없이 CE@0x018 → CE@0x0F0 → UE@0x0A8 를 읽고 STATUS = `CE|CE_OVF|UE`, CNT = `1<<16 | 2`, **CE_ADDR = 첫 오류(0x018)**, 부분 W1C 후 UE 만 남는지, 재무장(clear 후 다음 CE 가 새로 캡처)되는지.
- `reread_scrub_test(addr, bit, n)` — 같은 주소를 n 번 읽으면 CE 가 n 번 세진다(정정 읽기는 배열을 고치지 않는다). scrub(정정값 재기록) 후에는 더 안 세진다.
- `cnt_sat_test` — 65,537 회 읽어 CE 카운터가 0xFFFF 에서 멈추는지 (wrap 금지).
- 주입: `ce_capture_last` 와 `cnt_wrap` 은 **매트릭스 128 케이스를 모두 통과** 하고 Part B 에서만 잡힌다.

### Part C — 야간 회귀: 결함 주입 캠페인과 리포트

- `classify(f)` — 실패 기록(단계 + 기대/실제 + 주입 정보)으로 원인 후보를 고른다: `NO_CORRECTION`, `MISCORRECT_SDC`, `CHKBIT_AS_UE`, `ERROR_MISSED`, `INJECTOR`, `ADDR_CAPTURE`, `CAPTURE_ORDER`, `CNT_SATURATION`, `IRQ_DEAD`, `NO_BUSERR` 등 23가지.
- 주어진 `run_suite()` 가 매트릭스 128 + Part B 3 = 131 시험을 돌린다. 10가지 결함마다 실패 수와 **첫 실패의 시험·유형·단계·원인** 을 확인하고, `no_correct` 에 대해 리포트(첫 실패 3줄 + 유형별 요약 + 원인 집계)를 출력한다.

## 2. Problem (English)

> **ECC verification: error injection and CE/UE reporting.**
>
> A memory controller `DMC0` stores 64-bit words with 8 SECDED check bits (Hamming(72,64) + overall parity). It has an error-injection unit (`INJ_ADDR`, `INJ_MASK_LO/HI/CHK`, one-shot `INJ_EN`) that XORs the masks into the codeword of the next write to `INJ_ADDR`, a raw-read mode (`ECC_CTRL.RD_BYPASS`, check byte visible in `BYP_CHK`), and reporting registers: W1C `ECC_STATUS` (CE, UE, CE_OVF, UE_OVF), saturating 16-bit counters in `ECC_CNT`, first-error capture `CE_ADDR/CE_SYND/UE_ADDR/UE_SYND`, CE/UE interrupt lines 20/21 on an interrupt controller. A UE read must complete with `SLVERR`.
>
> 1. `void ecc_expect(const ecc_case_t *c, expect_t *e)` — from the injected masks alone, compute the expected class (OK/CE/UE/unspecified), status, counters, IRQ lines, bus response and syndrome (plus corrected bit number for CE).
> 2. `int build_matrix(const uint32_t *addrs, int n, uint64_t seed, ecc_case_t *out, int max)` — build the fixed-order matrix: clean, all 64 single data bits, all 8 single check bits, 32 data pairs (two boundary pairs + random), 8 data+check pairs, 8 check pairs. Return the count or -1.
> 3. `bool ecc_run_case(const ecc_case_t *c, fail_t *f)` — inject, prove the injection landed using a bypass read, then read normally and check status, response, data, counters, captured address, syndrome and IRQ lines; clear and check everything returns to zero. Stop at the first mismatch, record it, always clean up.
> 4. `capture_test`, `reread_scrub_test`, `cnt_sat_test` — first-error capture with overflow and partial clear, repeated reads and scrub, counter saturation.
> 5. `int classify(const fail_t *f)` — map a failure record to a probable root cause.
>
> Example: if the controller logs a CE but returns uncorrected data, case (D1, bit 17) must fail at step `DATA` with `exp ^ act == 1<<17`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 깨짐 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

실제로 돌린 결과 (앞부분과 끝만):

```text
FAIL starter.c:509  eok == (int)(sizeof et / sizeof et[0])
FAIL starter.c:511  e.status == ST_UE && e.cnt == 0x10000u && e.line == UE_LINE && e.resp == RESP_SLVERR
FAIL starter.c:515  n == 128
FAIL starter.c:516  cs[0].type == T_NONE && cs[7].addr == 0x1F8 && cs[7].mdata == 0 && cs[7].mchk == 0
...
[good] runs=3 failures=3 bus_accesses=1
...
7/60 passed
```

코덱, mock(`reg_*`, `mem_*`, `fault_t`), `expect_val/expect_reg`, `inject_write`, `mc_cleanup`, `run_suite`, 리포트 출력은 주어진 코드다. 채울 곳은 `ecc_expect`, `build_matrix`, `ecc_run_case`, `capture_test`, `reread_scrub_test`, `cnt_sat_test`, `classify` 일곱 개. 통과하는 7개는 코덱·mock sanity 와 "결함 mock 이면 실패해야 한다" 쪽 CHECK 일부다 — 스텁이 무조건 false 이니 우연히 맞는다. 좋은 mock 이 통과하는 방향은 전부 FAIL.

## 4. 힌트

<details>
<summary>힌트 1 — 기대 신드롬은 마스크만으로 계산된다</summary>

저장된 코드워드 = `enc(d) ⊕ e`. 신드롬은 선형이라 `S(enc(d) ⊕ e) = S(enc(d)) ⊕ S(e) = S(e)`. 그러니 `s = hsyn64(mdata) ^ (mchk & 0x7F)`, `p = (뒤집은 비트 수) & 1`. 데이터 `d` 는 필요 없다. 뒤집은 비트가 1개면 CE, 2개면 UE, 3개 이상은 SECDED 가 아무것도 약속하지 않는다.

</details>

<details>
<summary>힌트 2 — bypass 읽기는 "ECC 가 아니라 주입기" 를 시험한다</summary>

`ECC_CTRL = RUN | RD_BYPASS` 로 읽으면 원시 데이터와 `BYP_CHK` 가 나온다. 기대값은 `data ^ mdata` 와 `secded_encode(data) ^ mchk`. 여기서 틀리면 원인은 주입기다. 이 단계 없이 바로 일반 읽기를 하면, 체크비트 주입이 고장 난 칩에서 "CE 가 안 올라온다" 를 보고 decoder 를 의심하게 된다. bypass 읽기 뒤에 STATUS 가 여전히 0 인지도 확인한다.

</details>

<details>
<summary>힌트 3 — 일반 읽기 후 비교 순서: STATUS 를 먼저</summary>

데이터는 먼저 읽어 지역 변수에 두고, 비교는 `STATUS → RESP → DATA → CNT → ADDR → SYND → LINE` 순으로 `&&` 연결한다. STATUS(컨트롤러의 판정)가 틀렸으면 그게 근본 원인이고 나머지 불일치는 결과일 뿐이다. UE 는 DATA 비교를 건너뛰고, OK 는 ADDR/SYND 를 건너뛴다.

</details>

<details>
<summary>힌트 4 — classify 의 STATUS 분기</summary>

`e = exp & (CE|UE)`, `a = act & (CE|UE)`. 같으면 OVF 비트 문제, `a == 0` 이면 놓침, `e == 0` 이면 거짓 오류, UE 를 기대했는데 CE 만 있으면 **오정정(SDC)**, CE 를 기대했는데 UE 면 C1 케이스는 `CHKBIT_AS_UE`, 아니면 `CE_AS_UE`. DATA 단계는 `exp ^ act == mdata` 면 "정정 안 함", 아니면 "엉뚱한 비트를 뒤집음".

</details>

## 5. 풀이

### Part A — 기대값은 오류 패턴에서 나온다

ECC 는 선형 부호다. H 를 8×72 검사 행렬이라 하면 정상 코드워드 `c` 는 `H·c = 0`. 저장된 값이 `c ⊕ e` 이면

```text
S = H·(c ⊕ e) = H·c ⊕ H·e = H·e
```

즉 **신드롬은 데이터와 무관하고 오류 패턴 e 만의 함수** 다. 이 레슨의 H 는 열(column) j 가 `[위치(j) 7비트 ; 1]` 이므로 `s = 뒤집은 비트 위치들의 XOR`, `p = 뒤집은 비트 수 mod 2`. 실리콘 검증에서 이게 중요한 이유: 테스트는 데이터를 임의로(랜덤 패턴으로) 바꿔 가면서도 신드롬·비트 번호 기대값을 **주입 마스크만으로** 계산할 수 있다 (추가 연습 1 이 1000개 랜덤 데이터로 이 성질을 확인한다). H 행렬은 컨트롤러 데이터시트/RTL 스펙에 있는 것을 테스트 쪽 참조 모델로 옮겨 적는다.

Part A 단위 테스트의 기대값 (POS[i] = 데이터 비트 i 의 위치: d0=3, d5=10, d63=71):

| 주입 | s | p | 판정 | 신드롬 레지스터 | 메모 |
|---|---|---|---|---|---|
| 없음 | 0 | 0 | OK | — | |
| d5 | 10 | 1 | CE | `0x58A` (bit 5 << 8 \| 0x8A) | |
| d63 | 71 | 1 | CE | `0x3FC7` | 가장 높은 위치 |
| c0 | 1 | 1 | CE | `0x4081` (bit 64) | 체크비트도 CE — 데이터는 그대로 |
| c7 | 0 | 1 | CE | `0x4780` (bit 71) | s=0, p=1 이 유일한 c7 지문 |
| d0+d63 | 3^71=68 | 0 | UE | `0x044` | |
| d5+c7 | 10 | 0 | UE | `0x00A` | c7 은 s 에 안 들어간다 |
| c0+c1 | 3 | 0 | UE | `0x003` | s=3 은 d0 위치! p 가 없으면 d0 를 "정정" |
| d0+d1+d2 | 3^5^6=0 | 1 | 스펙 밖 | (`0x080`) | decoder 는 "c7 오류" 로 착각 |

마지막 줄이 매트릭스에 3비트 이상을 넣지 않는 이유다 — 결과가 스펙으로 정해져 있지 않으니 PASS/FAIL 기준이 없다 (추가 연습 1 에서 분포만 본다).

매트릭스 구성과 각 유형이 시험하는 RTL:

| 유형 | 개수 | 무엇을 덮나 |
|---|---|---|
| NONE | 8 | 오류 없는 읽기에서 거짓 CE/UE·인터럽트가 없는가, 주입기 arm 만 하고 마스크 0 |
| D1 | 64 | decoder 의 **비트별 정정 mux 64개** 를 하나씩 — 비트 하나의 신드롬 디코드가 틀리면 그 비트에서만 터진다 |
| C1 | 8 | 체크비트 오류는 CE 이고 데이터는 손대지 않아야 한다. c7 은 s=0, p=1 이라 별도 경로 |
| D2 | 32 | 경계 쌍 {0,63}, {31,32}(32비트 레지스터 경계 = MASK_LO/HI 경계) + 랜덤 30 |
| DC2 | 8 | 데이터 1 + 체크 1 (c7 포함 → s 가 데이터 위치와 같아지는 "위험한" UE) |
| C2 | 8 | 체크 2개. (c6,c7) → s=64, (c7,c0) → s=1 은 단일 체크비트 오류와 s 가 같다 — p 로만 구별 |

주소는 D1 비트 b 를 `A[b % 8]` 에 둔다: 8개 주소 × 8비트씩. walking 주소 `{0x000, 0x008, 0x010, 0x020, 0x040, 0x080, 0x100, 0x1F8}` 는 워드 주소 비트 3..8 을 각각 1 로 만들어 본다 — 오류 주소 캡처 레지스터의 비트가 빠지거나 붙은 결함은 **그 비트가 1 인 주소에서만** 보인다 (Part C 의 `addr_drop`).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="m14b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">ecc_run_case: 주입 → 착지 확인 → 판정·보고 확인 → clear (첫 실패에서 중단, 항상 cleanup)</text>
  <g font-family="monospace" fill="currentColor" text-anchor="middle">
    <rect x="10" y="34" width="86" height="40" class="f-mut"/><text x="53" y="51">PRE</text><text x="53" y="66">STATUS,CNT=0</text>
    <rect x="110" y="34" width="86" height="40" class="f-warn"/><text x="153" y="51">inject_</text><text x="153" y="66">write</text>
    <rect x="210" y="34" width="86" height="40" class="f-mut"/><text x="253" y="51">INJ_ARM</text><text x="253" y="66">INJ_CTRL=0</text>
    <rect x="310" y="34" width="86" height="40" class="f-warn"/><text x="353" y="51">BYPASS</text><text x="353" y="66">읽기</text>
    <rect x="410" y="34" width="96" height="40" class="f-acc"/><text x="458" y="51">LAND_DATA</text><text x="458" y="66">d ^ mdata</text>
    <rect x="520" y="34" width="100" height="40" class="f-acc"/><text x="570" y="51">LAND_CHK</text><text x="570" y="66">enc(d)^mchk</text>
    <rect x="634" y="34" width="96" height="40" class="f-acc"/><text x="682" y="51">BYP_QUIET</text><text x="682" y="66">STATUS=0</text>
  </g>
  <g stroke="currentColor">
    <line x1="96" y1="54" x2="108" y2="54" marker-end="url(#m14b)"/><line x1="196" y1="54" x2="208" y2="54" marker-end="url(#m14b)"/>
    <line x1="296" y1="54" x2="308" y2="54" marker-end="url(#m14b)"/><line x1="396" y1="54" x2="408" y2="54" marker-end="url(#m14b)"/>
    <line x1="506" y1="54" x2="518" y2="54" marker-end="url(#m14b)"/><line x1="620" y1="54" x2="632" y2="54" marker-end="url(#m14b)"/>
    <path d="M682,74 L682,92 L40,92 L40,106" fill="none" marker-end="url(#m14b)"/>
  </g>
  <g font-family="monospace" fill="currentColor" text-anchor="middle">
    <rect x="10" y="108" width="62" height="40" class="f-warn"/><text x="41" y="125">READ</text><text x="41" y="140">d,resp</text>
    <rect x="82" y="108" width="62" height="40" class="f-acc"/><text x="113" y="125">STATUS</text><text x="113" y="140">판정</text>
    <rect x="154" y="108" width="62" height="40" class="f-acc"/><text x="185" y="125">RESP</text><text x="185" y="140">SLVERR?</text>
    <rect x="226" y="108" width="62" height="40" class="f-acc"/><text x="257" y="125">DATA</text><text x="257" y="140">정정</text>
    <rect x="298" y="108" width="62" height="40" class="f-acc"/><text x="329" y="125">CNT</text><text x="329" y="140">+1</text>
    <rect x="370" y="108" width="62" height="40" class="f-acc"/><text x="401" y="125">ADDR</text><text x="401" y="140">캡처</text>
    <rect x="442" y="108" width="62" height="40" class="f-acc"/><text x="473" y="125">SYND</text><text x="473" y="140">s,p,bit</text>
    <rect x="514" y="108" width="62" height="40" class="f-acc"/><text x="545" y="125">LINE</text><text x="545" y="140">20/21</text>
    <rect x="586" y="108" width="62" height="40" class="f-ok"/><text x="617" y="125">CLR_*</text><text x="617" y="140">3개=0</text>
    <rect x="658" y="108" width="72" height="40" class="f-mut"/><text x="694" y="125">cleanup</text><text x="694" y="140">격리</text>
  </g>
  <g stroke="currentColor">
    <line x1="72" y1="128" x2="80" y2="128" marker-end="url(#m14b)"/><line x1="144" y1="128" x2="152" y2="128" marker-end="url(#m14b)"/>
    <line x1="216" y1="128" x2="224" y2="128" marker-end="url(#m14b)"/><line x1="288" y1="128" x2="296" y2="128" marker-end="url(#m14b)"/>
    <line x1="360" y1="128" x2="368" y2="128" marker-end="url(#m14b)"/><line x1="432" y1="128" x2="440" y2="128" marker-end="url(#m14b)"/>
    <line x1="504" y1="128" x2="512" y2="128" marker-end="url(#m14b)"/><line x1="576" y1="128" x2="584" y2="128" marker-end="url(#m14b)"/>
    <line x1="648" y1="128" x2="656" y2="128" marker-end="url(#m14b)"/>
  </g>
  <text x="10" y="176" fill="currentColor" font-weight="bold" font-size="13">유형별 기대값</text>
  <g fill="currentColor" font-weight="bold"><text x="20" y="198">판정</text><text x="90" y="198">STATUS</text><text x="170" y="198">RESP</text><text x="240" y="198">DATA</text><text x="320" y="198">CNT</text><text x="420" y="198">ADDR / SYND</text><text x="580" y="198">LINE</text></g>
  <line x1="10" y1="205" x2="730" y2="205" stroke="currentColor" opacity=".35"/>
  <g fill="currentColor" font-family="monospace">
    <text x="20" y="226">OK</text><text x="90" y="226">0</text><text x="170" y="226">OKAY</text><text x="240" y="226">== d</text><text x="320" y="226">0</text><text x="420" y="226">(안 봄)</text><text x="580" y="226">0</text>
    <text x="20" y="250">CE</text><text x="90" y="250">CE</text><text x="170" y="250">OKAY</text><text x="240" y="250">== d</text><text x="320" y="250">0x00001</text><text x="420" y="250">CE_ADDR, s|p|bit</text><text x="580" y="250">bit20</text>
    <text x="20" y="274">UE</text><text x="90" y="274">UE</text><text x="170" y="274">SLVERR</text><text x="240" y="274">(안 봄)</text><text x="320" y="274">0x10000</text><text x="420" y="274">UE_ADDR, s|p</text><text x="580" y="274">bit21</text>
  </g>
  <text x="10" y="296" fill="currentColor">C1 도 DATA == d: 체크비트만 틀렸으니 데이터는 "정정할 게 없는데도" 그대로 나와야 한다.</text>
</svg>
```

```c
    if (ok) {                                                       /* 2. 착지 확인: bypass 읽기 */
        uint64_t raw;
        reg_write32(ECC_CTRL, CTRL_RUN | CTRL_BYPASS);
        (void)mem_read64(c->addr, &raw);
        reg_write32(ECC_CTRL, CTRL_RUN);
        ok = expect_val(f, S_LAND_DATA, OFF_RDATA, c->data ^ c->mdata, raw)
          && expect_reg(f, S_LAND_CHK, BYP_CHK, (uint8_t)(secded_encode(c->data) ^ c->mchk))
          && expect_reg(f, S_BYP_QUIET, ECC_STATUS, 0);
    }
    if (ok) {                                                       /* 3. 일반 읽기 → 판정·보고 */
        uint64_t d;
        int resp = mem_read64(c->addr, &d);
        ok = expect_reg(f, S_STATUS, ECC_STATUS, e.status)
          && expect_val(f, S_RESP, OFF_RESP, (uint64_t)e.resp, (uint64_t)resp)
          && (e.cls == ECC_UE || expect_val(f, S_DATA, OFF_RDATA, c->data, d))
          && expect_reg(f, S_CNT, ECC_CNT, e.cnt)
          && (e.cls == ECC_OK || expect_reg(f, S_ADDR, e.cls == ECC_CE ? CE_ADDR : UE_ADDR, c->addr))
          && (e.cls == ECC_OK || expect_reg(f, S_SYND, e.cls == ECC_CE ? CE_SYND : UE_SYND, e.synd))
          && expect_reg(f, S_LINE, INTC_RAW, e.line);
    }
```

각 체크가 왜 있는가:

| 단계 | 이게 실패하면 의심할 것 |
|---|---|
| `PRE` STATUS/CNT = 0 | 이전 케이스의 잔여 상태 — 안 보면 이후 STATUS 비교가 "원래 서 있던 비트" 로 거짓 통과 |
| `INJ_ARM` INJ_CTRL = 0 | 주입이 소모되지 않음 = 쓰기가 INJ_ADDR 에 매칭되지 않음(주소 비교기 폭·정렬 문제). 주입이 안 들어갔는데 다음 쓰기에서 터지면 엉뚱한 케이스가 실패한다 |
| `LAND_DATA/CHK` | **주입기 결함**. ECC 판정을 보기 전에 "문제를 정말 냈는가" 를 확인 |
| `BYP_QUIET` | bypass 읽기가 보고를 남기면 진단 도구(raw dump)가 카운터를 오염시킨다 |
| `STATUS` | 판정 그 자체. CE↔UE 혼동, 놓침, 거짓 오류 |
| `RESP` | UE 데이터가 버스에서 OKAY 로 나가면 CPU/DMA 가 오염 데이터를 정상 값으로 소비 |
| `DATA` | 정정 mux. diff 가 주입 비트면 "정정 안 함", 다른 비트면 "오정정" |
| `CNT` | 카운터 증가 경로 (CE 와 UE 가 서로 다른 필드로 가는지) |
| `ADDR/SYND` | 소프트웨어가 페이지 offline·DIMM 교체를 결정하는 근거. 주소 비트 하나가 틀리면 **엉뚱한 페이지를 retire** 한다 |
| `LINE` | IRQ 배선 (SIV-B11 의 trigger-check-clear 와 같은 원리) |
| `CLR_*` | W1C 와 카운터 clear |

좋은 mock 에서 128 케이스 전부 통과하고, 스위트 전체(131 시험)의 버스 접근은 실제 측정 69,451 회 — 대부분은 Part B 의 포화 시험 65,537 읽기다.

### Part B — 첫 오류 캡처, 반복 읽기, 포화

매트릭스는 케이스마다 STATUS 와 카운터를 지우므로 **"오류가 쌓였을 때"** 의 동작을 한 번도 보지 않는다. 그런데 실제 시스템에서는 ISR 이 늦게 돌거나 patrol scrubber 가 여러 개를 연달아 찾는 일이 흔하다.

**① 첫 오류 캡처와 OVF.** 이 컨트롤러 스펙: STATUS.CE 가 0→1 될 때만 CE_ADDR/CE_SYND 를 캡처하고, CE 가 이미 서 있으면 CE_OVF 만 세운다. 소프트웨어는 "첫 오류 주소는 정확하고, 그 뒤에 더 있었다(OVF)" 를 안다. `ce_capture_last` 결함은 매트릭스 128 개를 모두 통과하지만(테스트가 확인한 사실: `pass == 128`) capture_test 에서 `FIRST_CAP exp=0x018 act=0x0F0` 로 잡힌다. 부분 clear(CE|CE_OVF 만 W1C → UE 는 남아야)와 재무장(clear 후 다음 CE 가 새로 캡처)도 같은 시험에서 본다.

```text
CE@0x018(d5) → CE@0x0F0(d40) → UE@0x0A8(d1+d2), clear 없이 읽기
  STATUS == CE|CE_OVF|UE (0x7)     CNT == 0x00010002
  CE_ADDR == 0x018, CE_SYND == 0x58A     UE_ADDR == 0x0A8     INTC_RAW == bit20|bit21
W1C(CE|CE_OVF) → STATUS == UE, INTC_RAW == bit21  →  W1C(UE) → 0, 0
0x0F0 다시 읽기 → CE_ADDR == 0x0F0 (재무장)
```

**② 반복 읽기와 scrub.** 정정은 **읽기 경로에서만** 일어난다. 배열의 코드워드는 여전히 틀려 있으므로 같은 주소를 3번 읽으면 CE 가 3번 세진다. 정정값을 다시 써야(scrub) 배열이 고쳐진다. 이걸 알아야 카운터를 해석할 수 있다: "CE 카운트 1000" 은 1000개의 서로 다른 오류일 수도, 한 주소를 1000번 읽은 것일 수도 있다. 실제 컨트롤러는 patrol scrub(주기적으로 전 영역 읽기 + 정정값 재기록)과 demand scrub(CE 가 난 읽기에서 자동 재기록) 옵션을 두는데, demand scrub 이 켜진 칩이라면 이 시험의 기대값은 "CNT == 1" 로 바뀐다 — 그래서 시험은 스펙 문구를 그대로 옮긴 것이어야 한다. 반복 읽기 뒤 scrub 해도 CE 가 다시 나면 그건 저장 셀 자체의 고장(hard error)이다 (추가 연습 3).

**③ 카운터 포화.** 16비트 카운터가 0xFFFF 다음에 0 으로 돌아가면, 오류 폭주 중인 DIMM 이 모니터링 소프트웨어에는 "오류 1개" 로 보인다. 경계값 테스트의 정석대로 포화점을 **넘겨서**(65,537 회) 확인한다: `cnt_wrap` 결함은 `SAT exp=0x0000FFFF act=0x00000001`. 매트릭스와 capture/reread 는 모두 통과한다. 실리콘에서 65,537 회 읽기는 JTAG 로는 느리므로 온칩 테스트 펌웨어에서 돌린다.

| 결함 | 매트릭스 128 | capture | reread | sat |
|---|---|---|---|---|
| `ce_capture_last` | 전부 통과 | **FIRST_CAP** | 통과 (같은 주소) | 통과 |
| `cnt_wrap` | 전부 통과 | 통과 | 통과 | **SAT** |
| `no_correct` | D1 64개 DATA | DATA | DATA | 통과 (데이터 안 봄) |

### Part C — 결함 주입 캠페인과 원인 분류

`classify()` 의 결정표:

| step | 조건 | 원인 | 디버그 엔지니어의 다음 행동 |
|---|---|---|---|
| `PRE` | — | `STALE_STATE` | 직전 케이스 로그, cleanup 경로 |
| `INJ_ARM` | — | `INJ_NOT_CONSUMED` | 주입 주소 비교기, 주소 정렬 |
| `LAND_*` | — | `INJECTOR` | 주입 마스크 → 배열 배선. **ECC 결론 보류** |
| `BYP_QUIET` | — | `BYPASS_LEAK` | bypass 모드가 보고 로직을 게이트하는지 |
| `STATUS` | 기대 UE, 실제 CE | `MISCORRECT_SDC` | p(전체 패리티) 경로, decoder 판정 로직 — **최우선** |
| | 기대 CE, 실제 UE (C1) | `CHKBIT_AS_UE` | 체크비트 위치 신드롬 디코드 |
| | 실제 0 | `ERROR_MISSED` | decoder enable, 해당 신드롬 디코드 |
| | 기대 0 | `FALSE_ERROR` | encoder/decoder H 행렬 불일치 |
| `RESP` | 기대 SLVERR | `NO_BUSERR` | UE → 버스 응답 연결, poison 신호 |
| `DATA` | diff == 주입 | `NO_CORRECTION` | 정정 mux enable |
| | 그 외 | `MISCORRECT_SDC` | 신드롬 → 비트 디코드 테이블 |
| `CNT` / `SAT` | — | `COUNTER` / `CNT_SATURATION` | 카운터 증가·포화 로직 |
| `ADDR` / `FIRST_CAP` | — | `ADDR_CAPTURE` / `CAPTURE_ORDER` | diff 비트 = 빠진 주소 비트 / 캡처 enable 조건 |
| `SYND` | — | `SYNDROME` | 신드롬 레지스터 비트 매핑 |
| `LINE` | 빠진 비트 / 남는 비트 | `IRQ_DEAD` / `IRQ_SPURIOUS` | IRQ 배선, enable 게이트 |

solution 을 실제로 돌린 출력:

```text
[good] runs=131 failures=0 bus_accesses=69451
[no_correct] failures=66/131 first: FAIL DMC0 inj type=D1 addr=0x000 inj=d0 step=DATA reg=RDATA exp=0x02CF666ED27515F0 act=0x02CF666ED27515F1 diff=0x0000000000000001 bits=[0] cause=NO_CORRECTION
[ignore_parity] failures=50/131 first: FAIL DMC0 inj type=C1 addr=0x1F8 inj=c7 step=STATUS reg=ECC_STATUS exp=0x00000001 act=0x00000000 diff=0x00000001 bits=[0] cause=ERROR_MISSED
[chk_as_ue] failures=8/131 first: FAIL DMC0 inj type=C1 addr=0x000 inj=c0 step=STATUS reg=ECC_STATUS exp=0x00000001 act=0x00000002 diff=0x00000003 bits=[0,1] cause=CHKBIT_AS_UE
[ce_cnt_dead] failures=75/131 first: FAIL DMC0 inj type=D1 addr=0x000 inj=d0 step=CNT reg=ECC_CNT exp=0x00000001 act=0x00000000 diff=0x00000001 bits=[0] cause=COUNTER
[cnt_wrap] failures=1/131 first: FAIL DMC0 sat type=D1 addr=0x040 inj=d0 step=SAT reg=ECC_CNT exp=0x0000FFFF act=0x00000001 diff=0x0000FFFE bits=[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15] cause=CNT_SATURATION
[addr_drop=0x100] failures=30/131 first: FAIL DMC0 inj type=D1 addr=0x100 inj=d6 step=ADDR reg=CE_ADDR exp=0x00000100 act=0x00000000 diff=0x00000100 bits=[8] cause=ADDR_CAPTURE
[ce_capture_last] failures=1/131 first: FAIL DMC0 capture type=D1 addr=0x018 inj=d5 step=FIRST_CAP reg=CE_ADDR exp=0x00000018 act=0x000000F0 diff=0x000000E8 bits=[3,5,6,7] cause=CAPTURE_ORDER
[ue_irq_dead] failures=49/131 first: FAIL DMC0 inj type=D2 addr=0x000 inj=d0+d63 step=LINE reg=INTC_RAW exp=0x00200000 act=0x00000000 diff=0x00200000 bits=[21] cause=IRQ_DEAD
[ue_no_buserr] failures=49/131 first: FAIL DMC0 inj type=D2 addr=0x000 inj=d0+d63 step=RESP reg=RRESP exp=SLVERR act=OKAY cause=NO_BUSERR
[inj_chk_dead] failures=24/131 first: FAIL DMC0 inj type=C1 addr=0x000 inj=c0 step=LAND_CHK reg=BYP_CHK exp=0x0000009D act=0x0000009C diff=0x00000001 bits=[0] cause=INJECTOR
---- report: DMC0 fault=no_correct ----
FAIL DMC0 inj type=D1 addr=0x000 inj=d0 step=DATA reg=RDATA exp=0x02CF666ED27515F0 act=0x02CF666ED27515F1 diff=0x0000000000000001 bits=[0] cause=NO_CORRECTION
FAIL DMC0 inj type=D1 addr=0x008 inj=d1 step=DATA reg=RDATA exp=0x9D6ED90D379C83DB act=0x9D6ED90D379C83D9 diff=0x0000000000000002 bits=[1] cause=NO_CORRECTION
FAIL DMC0 inj type=D1 addr=0x010 inj=d2 step=DATA reg=RDATA exp=0x84C1545102902C1C act=0x84C1545102902C18 diff=0x0000000000000004 bits=[2] cause=NO_CORRECTION
  ... (63 more)
  type  cases  fail  first-fail-step
  NONE      8     0  -
  D1       64    64  DATA
  C1        8     0  -
  D2       32     0  -
  DC2       8     0  -
  C2        8     0  -
  capture=F reread=F sat=P
  causes: NO_CORRECTION x66
60/60 passed
```

`no_correct` 리포트에서 읽을 것: **D1 만 64/64 실패, C1 은 0/8**. 체크비트 오류는 데이터에 영향이 없으니 정정 mux 가 죽어도 통과한다 — "모든 단일 오류가 CE 로 보고되고 카운트도 맞다" 만 보는 시험은 이 결함을 100% 놓친다. STATUS·CNT·ADDR 이 전부 맞기 때문에 소프트웨어(EDAC)는 "CE 정정됨" 을 기록하고, 실제로는 틀린 데이터가 CPU 로 간다. 가장 위험한 종류의 버그다.

결함 × 시험 유형 (칸 = 실패 개수, 강조 = 잡음). 실행 결과를 유형별로 집계한 것이다:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">결함 × 시험 유형 — 실패 개수 (매트릭스 128 + capture/reread/sat)</text>
  <g fill="currentColor" font-weight="bold">
    <text x="10" y="44">주입 결함</text><text x="160" y="44">NONE</text><text x="208" y="44">D1</text><text x="250" y="44">C1</text>
    <text x="292" y="44">D2</text><text x="332" y="44">DC2</text><text x="376" y="44">C2</text><text x="414" y="44">cap</text>
    <text x="456" y="44">rr</text><text x="494" y="44">sat</text><text x="532" y="44">계</text><text x="572" y="44">주 원인</text>
  </g>
  <line x1="10" y1="51" x2="730" y2="51" stroke="currentColor" opacity=".35"/>
  <g class="f-warn">
    <rect x="200" y="57" width="36" height="20"/><rect x="408" y="57" width="36" height="20"/><rect x="448" y="57" width="36" height="20"/>
    <rect x="242" y="80" width="36" height="20"/><rect x="284" y="80" width="36" height="20"/><rect x="326" y="80" width="36" height="20"/><rect x="368" y="80" width="36" height="20"/><rect x="408" y="80" width="36" height="20"/>
    <rect x="242" y="103" width="36" height="20"/>
    <rect x="200" y="126" width="36" height="20"/><rect x="242" y="126" width="36" height="20"/><rect x="408" y="126" width="36" height="20"/><rect x="448" y="126" width="36" height="20"/><rect x="488" y="126" width="36" height="20"/>
    <rect x="488" y="149" width="36" height="20"/>
    <rect x="200" y="172" width="36" height="20"/><rect x="242" y="172" width="36" height="20"/><rect x="284" y="172" width="36" height="20"/><rect x="326" y="172" width="36" height="20"/><rect x="368" y="172" width="36" height="20"/>
    <rect x="408" y="195" width="36" height="20"/>
    <rect x="284" y="218" width="36" height="20"/><rect x="326" y="218" width="36" height="20"/><rect x="368" y="218" width="36" height="20"/><rect x="408" y="218" width="36" height="20"/>
    <rect x="284" y="241" width="36" height="20"/><rect x="326" y="241" width="36" height="20"/><rect x="368" y="241" width="36" height="20"/><rect x="408" y="241" width="36" height="20"/>
    <rect x="242" y="264" width="36" height="20"/><rect x="326" y="264" width="36" height="20"/><rect x="368" y="264" width="36" height="20"/>
  </g>
  <g fill="currentColor">
    <text x="10" y="72">no_correct</text><text x="10" y="95">ignore_parity</text><text x="10" y="118">chk_as_ue</text>
    <text x="10" y="141">ce_cnt_dead</text><text x="10" y="164">cnt_wrap</text><text x="10" y="187">addr_drop=0x100</text>
    <text x="10" y="210">ce_capture_last</text><text x="10" y="233">ue_irq_dead</text><text x="10" y="256">ue_no_buserr</text>
    <text x="10" y="279">inj_chk_dead</text>
  </g>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="218" y="72">64</text><text x="426" y="72">F</text><text x="466" y="72">F</text><text x="540" y="72">66</text>
    <text x="260" y="95">1</text><text x="302" y="95">32</text><text x="344" y="95">8</text><text x="386" y="95">8</text><text x="426" y="95">F</text><text x="540" y="95">50</text>
    <text x="260" y="118">8</text><text x="540" y="118">8</text>
    <text x="218" y="141">64</text><text x="260" y="141">8</text><text x="426" y="141">F</text><text x="466" y="141">F</text><text x="506" y="141">F</text><text x="540" y="141">75</text>
    <text x="506" y="164">F</text><text x="540" y="164">1</text>
    <text x="218" y="187">16</text><text x="260" y="187">2</text><text x="302" y="187">8</text><text x="344" y="187">2</text><text x="386" y="187">2</text><text x="540" y="187">30</text>
    <text x="426" y="210">F</text><text x="540" y="210">1</text>
    <text x="302" y="233">32</text><text x="344" y="233">8</text><text x="386" y="233">8</text><text x="426" y="233">F</text><text x="540" y="233">49</text>
    <text x="302" y="256">32</text><text x="344" y="256">8</text><text x="386" y="256">8</text><text x="426" y="256">F</text><text x="540" y="256">49</text>
    <text x="260" y="279">8</text><text x="344" y="279">8</text><text x="386" y="279">8</text><text x="540" y="279">24</text>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="572" y="72">NO_CORRECTION</text><text x="572" y="95">MISCORRECT+SYND</text><text x="572" y="118">CHKBIT_AS_UE</text>
    <text x="572" y="141">COUNTER</text><text x="572" y="164">CNT_SATURATION</text><text x="572" y="187">ADDR_CAPTURE</text>
    <text x="572" y="210">CAPTURE_ORDER</text><text x="572" y="233">IRQ_DEAD</text><text x="572" y="256">NO_BUSERR</text>
    <text x="572" y="279">INJECTOR</text>
  </g>
</svg>
```

표에서 읽을 것:

1. **첫 실패가 가장 중요한 실패는 아니다.** `ignore_parity` 의 첫 실패는 c7 단일 오류를 놓친 `ERROR_MISSED` 1개다. 그런데 집계는 `MISCORRECT_SDC x41 SYNDROME x8 ERROR_MISSED x1` (테스트가 확인: `count_cause == 41 / 8 / 1`). 2비트 오류를 CE 로 보고하고 **세 번째 비트를 뒤집는** 조용한 데이터 오염이 진짜 문제다. 리포트는 원인별 집계를 같이 보여 줘야 한다 (SIV-B14).
2. **SYNDROME 8 개는 무엇인가.** c7 을 무시하는 decoder 라도 s 가 71 을 넘는(데이터·체크 위치가 아닌) 2비트 조합은 여전히 UE 로 올린다 — STATUS·RESP·CNT 는 맞다. 이 경우를 잡은 건 **UE_SYND 의 bit7(p)이 1** 로 보고된 것뿐이다(`exp=0x56 act=0xD6`). 신드롬 레지스터까지 비교하지 않았다면 32개 D2 중 5개는 통과했을 것이다.
3. **`inj_chk_dead` 는 ECC 결함이 아니다.** LAND_CHK 단계가 없었다면 C1 8개는 "CE 를 기대했는데 0" → `ERROR_MISSED` 로 분류되어 decoder 팀에 버그가 갔을 것이다. 착지 확인이 원인 귀속(어느 블록 버그인가)을 바꾼다.
4. **`addr_drop` 은 주소 비트 8 이 1 인 두 주소(0x100, 0x1F8)에서만**, 30개 모두 `bits=[8]` (테스트가 확인: `a8 == 30`). 주소가 0x0xx 만 있는 매트릭스였다면 0개 — 추가 연습 2.
5. **`ue_irq_dead` 와 `ue_no_buserr` 는 같은 49개 케이스** 에서 다른 단계로 잡힌다. 둘 다 "UE 를 소프트웨어가 모른다" 는 같은 결말이지만, 하나는 인터럽트 배선(SoC 통합), 하나는 버스 응답(컨트롤러 RTL)이라 담당자가 다르다.

### 실제 검증 현장과의 연결

- **주입 수단**: 컨트롤러 자체의 poison/inject 레지스터(Synopsys uMCTL2 의 ECCPOISONADDR0/1 + poison enable — Linux `drivers/edac/synopsys_edac.c` 가 디버그용 주입 속성을 제공), Arm RAS 확장의 pseudo-fault generation 레지스터(ERR\<n\>PFGCTL 등, RAS v1.1), x86 서버의 ACPI APEI EINJ(`/sys/kernel/debug/apei/einj/`, error_type 0x8 = memory correctable, 0x10 = memory uncorrectable non-fatal). 주입 레지스터가 없으면 "ECC 끄고 쓰기 → 켜고 읽기" 로 데이터와 체크를 어긋나게 만든다.
- **보고 확인**: 이 레슨의 레지스터 비교가 하드웨어 쪽, Linux 에서는 EDAC 의 `/sys/devices/system/edac/mc/mc0/ce_count`, `ue_count` 와 dmesg 의 주소·신드롬 로그가 end-to-end 확인이다. 레지스터는 맞는데 EDAC 가 다른 주소를 찍으면 드라이버의 주소 변환(컨트롤러 주소 → 시스템 물리 주소) 문제다.
- **자동차(ISO 26262)**: ECC 는 메모리의 안전 메커니즘이고, 안전 메커니즘이 조용히 죽어 있는 것은 latent fault 다. 그래서 자동차 SoC 는 부팅 때 ECC 를 주입으로 자가 시험하는 경우가 많다 — 이 레슨의 매트릭스를 줄여서 돌리는 것이 바로 그 시험이다.
- **pre-silicon 과의 차이**: RTL 시뮬레이션에서는 force/deposit 로 배열의 아무 비트나 뒤집을 수 있고 스코어보드가 매 읽기를 참조 모델과 비교한다. post-silicon 에서는 칩이 제공하는 주입 레지스터와 관측 레지스터가 전부이므로, **주입기 자체를 먼저 검증**(LAND 단계)해야 하고 시험 수를 버스 접근 비용으로 계산해야 한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 60/60 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san
# 60/60 passed, exit 0, 출력에서 "runtime error"/"AddressSanitizer" 0건 확인
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| CE 가 보고되면 통과 | `no_correct` 는 STATUS·CNT·ADDR 이 전부 맞다. 데이터를 안 보면 66개 실패를 모두 놓친다 | CE 케이스마다 `RDATA == 원래 데이터` |
| 주입 후 바로 일반 읽기 | 주입기 고장을 decoder 고장으로 오진 (`inj_chk_dead` → ERROR_MISSED 로 보임) | bypass 읽기로 LAND_DATA/LAND_CHK 먼저 |
| 기대 신드롬을 RTL 이 낸 값으로 "학습" | 첫 실행 값을 골든으로 삼으면 버그도 골든이 된다 | 마스크 + H 행렬(스펙)로 독립 계산 |
| 데이터 비트만 주입 | 체크비트 경로(c0..c7)와 c7 의 특수 경로(s=0)를 안 본다 | C1 8개, C2 (c7 포함) |
| 2비트는 랜덤만 | 레지스터 경계(31/32), 양 끝(0/63), "s 가 유효 위치가 되는" 쌍을 놓칠 수 있다 | 경계 쌍 고정 + 랜덤 (고정 시드) |
| 3비트 주입을 PASS/FAIL 로 판정 | SECDED 는 3비트에 아무 약속도 안 한다 — 스펙 없는 기대값 | 3비트는 분포만 기록 (연습 1) |
| 한 주소에서만 시험 | 주소 캡처 비트 결함은 그 비트가 1 인 주소에서만 보인다 | walking 주소 (비트마다 0/1 둘 다) |
| 케이스마다 clear 하니 충분 | 캡처 순서·OVF·포화는 오류가 쌓여야 보인다 | capture / reread / sat 별도 시험 |
| UE 읽기 데이터를 비교 | UE 데이터는 스펙상 무의미(원시값 또는 poison 패턴) — 구현 세부에 테스트가 묶인다 | UE 는 RESP 와 STATUS 로 판정 |
| ECC_EN 상태에서 cleanup 없이 다음 케이스 | 남은 오류 코드워드를 다음 시험이 읽어 연쇄 실패 | 항상 깨끗한 값 재기록 + W1C + 카운터 clear |

## 8. 면접 Q&A

<details>
<summary>Q1. 메모리 컨트롤러 ECC 를 post-silicon 에서 어떻게 검증하겠습니까?</summary>

ECC 수학이 아니라 **보고 경로** 를 검증한다. ① 주입: 컨트롤러의 inject/poison 레지스터로 데이터 1비트(64개 전부), 체크 1비트(8개 전부), 2비트(경계 + 랜덤), 데이터+체크 2비트를 넣는다. ② 착지 확인: bypass/raw 읽기로 저장된 코드워드가 의도대로인지. ③ 일반 읽기 후 판정(STATUS), 버스 응답(UE 는 에러), 정정 데이터, CE/UE 카운터, 오류 주소·신드롬·비트 번호, 인터럽트를 기대값과 비교 — 기대값은 H 행렬과 마스크로 독립 계산. ④ clear 확인. 이걸 walking 주소로 돌리고, 별도로 오류 누적(첫 오류 캡처·OVF·부분 clear), 반복 읽기와 scrub, 카운터 포화를 본다. 마지막으로 OS 의 EDAC 카운트와 로그로 end-to-end.
꼬리질문 "inject 레지스터가 없으면?" → ECC 를 끈 상태로 데이터만 쓰고(체크는 이전 값 또는 별도 경로) 켜서 읽기, 컨트롤러의 check-bit write-through 모드, 또는 DRAM 리프레시를 늘리거나 전압을 낮춰 자연 오류를 유도(이건 재현성이 없어 특성화용).

</details>

<details>
<summary>Q2. 기대 신드롬을 어떻게 계산했나요? 데이터가 랜덤인데요.</summary>

선형 부호라서 `S(c ⊕ e) = S(c) ⊕ S(e) = S(e)` — 신드롬은 오류 패턴만의 함수다. 그래서 주입 마스크에 H 를 곱하면 끝이다 (이 레슨에선 위치 XOR + 패리티). 데이터를 랜덤으로 바꾸는 이유는 신드롬이 아니라 **데이터 경로**(정정 mux, 바이트 레인, 패턴 의존 결함)를 흔들기 위해서다.
꼬리질문 "실제 DRAM ECC 는 Hamming 이 아니라 Hsiao 코드라던데?" → Hsiao(72,64) 는 H 의 모든 열을 홀수 무게로 골라 별도의 전체 패리티 비트 없이 SECDED 가 되는 코드다(단일 오류 = 홀수 무게 신드롬 = 어떤 열과 일치, 2비트 = 0 이 아닌 짝수 무게). 판정 규칙과 신드롬 값은 다르지만 "기대값 = H·e" 라는 검증 방법은 그대로다. 데이터시트의 H 행렬을 참조 모델로 옮기면 된다.

</details>

<details>
<summary>Q3. 2비트 오류를 CE 로 보고하는 버그가 왜 최악인가요?</summary>

UE 는 시스템이 안다 — 버스 에러/머신 체크로 프로세스를 죽이거나 페이지를 버린다. 2비트를 CE 로 판정하면 decoder 는 신드롬이 가리키는 **세 번째 비트를 뒤집어서** 3비트가 틀린 데이터를 OKAY 로 내보내고, 로그엔 "정정됨" 이 남는다. 조용한 데이터 오염(SDC)이고 원인 추적이 거의 불가능하다. 이 레슨의 `ignore_parity` 가 그 예: 매트릭스의 2비트 48개 중 40개를 CE 로 보고했다(나머지 8개는 UE 로 올렸지만 신드롬을 틀리게 보고).
꼬리질문 "3비트 오류는?" → SECDED 의 보장 밖이다. 이 레슨 H 로 72비트 중 3비트 59,640 조합을 전수 조사하면 UE 로 잡히는 건 14,336 (24.0%), 나머지 45,304 (76.0%)는 단일 오류로 오인된다(연습 1). 다중 비트가 흔한 환경(x4 칩 하나 전체 고장 등)은 chipkill/SDDC 같은 심볼 단위 코드로 간다.

</details>

<details>
<summary>Q4. 같은 주소에서 CE 카운트가 계속 올라갑니다. 무엇을 의심하고 어떻게 확인하나요?</summary>

먼저 "같은 오류를 여러 번 읽은 것" 인지 "새 오류가 계속 나는 것" 인지 구별한다. 정정은 읽기 경로에서만 일어나므로 scrub(정정값 재기록) 전에는 같은 비트로 계속 센다. scrub 후 다시 읽어 CE 가 또 나면 **hard error**(셀 stuck, 약한 셀) — 보수 패턴도 써 본다. 대응은 페이지 offline(Linux RAS 의 CE collector 가 임계치 넘는 페이지를 offline), DRAM 의 post-package repair, DIMM 교체. 안 나면 soft error(우주선·알파 입자)로 기록만 한다. 추가 연습 3 이 이 트리아지를 구현한다.
꼬리질문 "patrol scrub 이 켜져 있으면 카운터 해석이 어떻게 바뀌나?" → scrubber 가 읽어서 찾은 CE 도 세지므로 "소프트웨어가 안 건드린 영역" 의 오류도 카운트에 들어간다. 오류 출처(demand read vs scrub)를 구분하는 상태 비트가 있는지 확인하고, scrub 주기 × 메모리 크기로 기대 증가율을 계산한다.

</details>

<details>
<summary>Q5. UE 가 났을 때 하드웨어와 펌웨어는 각각 무엇을 해야 하나요?</summary>

하드웨어: 읽기를 에러 응답(AXI SLVERR 등)이나 poison 표시와 함께 완료해 소비자가 데이터를 쓰지 않게 하고, UE 상태·주소·신드롬을 캡처하고 인터럽트를 올린다. CPU 쪽에서는 Arm 이면 synchronous external abort 나 SError, x86 이면 machine check 로 나타난다. 펌웨어/OS: 주소로 영향받은 페이지·프로세스를 찾아 죽이거나(사용자 페이지) 패닉(커널 데이터), 로그. 쓰기 경로도 중요하다 — 부분 쓰기(RMW)가 UE 워드를 읽어 병합하고 새 ECC 를 계산하면 오염 데이터가 "깨끗한 ECC" 를 얻는다(세탁). 병합 후에도 poison 을 유지해야 한다 (추가 연습 4).
꼬리질문 "검증에서 SLVERR 을 어떻게 확인하나?" → 호스트 mock 에선 반환값, 실보드에선 CPU 의 abort/SError 핸들러가 기록한 syndrome(ESR) 과 fault address 를 테스트가 확인한다. 예외 핸들러가 테스트 프레임워크로 돌아올 수 있게(recoverable) 만들어 두는 게 준비 작업이다.

</details>

<details>
<summary>Q6. ECC 를 켜기 전에 메모리를 초기화해야 하는 이유는?</summary>

전원 투입 직후 DRAM/SRAM 내용은 무작위다. 데이터와 체크 비트가 서로 맞을 리 없으므로, 초기화 안 한 영역을 ECC 켜고 읽으면(캐시 라인 fill, 스택 접근, prefetch) UE 폭주가 난다. 그래서 부트 코드는 컨트롤러의 init/scrub 엔진이나 DMA 로 전 영역을 한 번 써서 유효한 코드워드로 만든 뒤 보고를 켠다. 검증 항목: 초기화 엔진이 전 영역을 덮는가(마지막 주소, 뱅크/랭크 경계), 초기화 중 보고가 억제되는가, 초기화 완료 상태 비트. mock 에서 메모리를 0 으로 리셋한 이유이기도 하다 — `encode(0) == 0` 이라 0 은 유효한 코드워드다.
꼬리질문 "부분 초기화 버그는 어떻게 잡나?" → 초기화 직후 bypass 읽기로 샘플 주소들의 체크 바이트가 `encode(data)` 와 맞는지 확인하고, 일반 읽기로 전 영역을 훑어 CE/UE 카운터가 0 인지 본다.

</details>

<details>
<summary>Q7. 이 테스트들의 커버리지를 어떻게 주장하나요? 랜덤 2비트 30개면 충분합니까?</summary>

차원별로 말한다. 비트: 72 위치 단일 오류 전부(decoder 의 정정 경로 전부). 판정: OK/CE/UE 각각 + c7 특수 경로 + "s 가 유효 위치가 되는 2비트". 주소: 캡처 레지스터의 각 주소 비트를 0/1 로. 상태 의미론: 첫 오류 캡처·OVF·부분 clear·재무장, 카운터 포화. 출력: RDATA·RRESP·STATUS·CNT·ADDR·SYND·IRQ 각각을 최소 한 결함이 단독으로 실패시키는지 — 이 레슨의 캠페인 표가 그 증거다(각 관측 지점마다 그것만 잡는 결함이 있다). 2비트 조합은 2,556 개뿐이라 온칩 펌웨어로는 전수도 가능하다. 시간이 문제면 랜덤 + 고정 경계 쌍으로 시작하고 functional coverage(SIV-M13)로 "s 값 구간" 같은 bin 을 채웠는지 본다.

</details>

## 9. 추가 연습

### 연습 1 — 3비트 오류 전수 조사

72비트 중 1·2·3비트 오류를 전부 만들어 판정을 세라: 단일은 모두 정확히 정정, 2비트는 모두 UE 여야 한다. 3비트(59,640 조합)는 UE / 데이터 비트로 오인 / 체크 비트로 오인으로 나눠 세라. 덤으로 신드롬이 데이터와 무관함을 랜덤 1000개로 확인하라.

<details>
<summary>답 보기</summary>

실제 출력: `single: 72/72 corrected, double: 2556/2556 UE, triple: 59640 total, UE=14336, miscorrect data=40320, miscorrect chk=4984`. 3비트 중 24.0% 만 UE 로 잡히고, 67.6% 는 데이터 비트 하나를 더 뒤집어 4비트 오염 데이터를 "정정됨" 으로, 8.4% 는 체크비트 오류로 보고(데이터 3비트 오염 그대로). UE 로 잡히는 건 s 가 72..127(존재하지 않는 위치)일 때뿐이다. 그래서 매트릭스에 3비트를 넣지 않고, 다중 비트가 현실적인 고장 모드면 코드 자체를 바꿔야(심볼 코드) 한다는 결론이 나온다.

</details>

### 연습 2 — 주소 비트 커버리지

주소 집합에서 "0 과 1 을 모두 가져 본 비트" 를 돌려주는 `addr_bits_covered()` 를 쓰고, `addr_drop = 0x100` 결함이 주소 집합에 따라 숨는지 확인하라.

<details>
<summary>답 보기</summary>

`seen1 = OR(a)`, `seen0 = OR(~a)`, 결과 = `seen1 & seen0 & width`. `{0x000..0x080}` 6개는 `0x0F8`(bit 8 이 한 번도 1 이 아님) → `addr_drop=0x100` 불일치 **0개**(결함이 숨는다). 8개 walking 집합은 `0x1F8` → 불일치 2개(0x100, 0x1F8). `{0x000, 0x1F8}` 두 개도 모든 비트를 토글하지만, `addr_drop = 0x018` 처럼 두 비트가 같이 빠지면 불일치 1개로 "어느 비트인지" 는 모른다 — walking 1 이 비트별 진단을 준다 (SIV-B05/B06 의 주소 라인 테스트와 같은 원리).

</details>

### 연습 3 — CE 트리아지: soft 인가 hard 인가

CE 가 보고된 주소를 받아 NONE / SOFT / HARD 를 판정하는 `triage_ce()` 를 작성하라: 읽기 → scrub → 다시 읽기 → 보수 패턴 쓰고 읽기 → 원래 값 복원. mock 에 셀 stuck 결함(`stuck_bit`, `stuck_val`)을 넣어 구별되는지 확인하라.

<details>
<summary>답 보기</summary>

실제 출력: `soft flip -> SOFT, stuck-at-0 -> HARD, clean -> NONE`. 주입으로 만든 일회성 뒤집힘은 scrub 후 사라진다. bit7 에 1 을 썼는데 셀이 0 에 고정된 경우는 scrub(같은 값 재기록) 후에도 CE 가 다시 난다. 보수 패턴 단계는 scrub 후 우연히 셀 값과 데이터가 맞아 떨어진 경우를 대비한 것이다. 실제 시스템에선 한 번의 판정이 아니라 시간 창 안의 CE 빈도와 임계치로 페이지 offline 을 결정한다. 트리아지는 데이터를 덮어쓰므로 사용 중인 페이지에서는 OS 와 협조해야 한다.

</details>

### 연습 4 — 부분 쓰기(RMW)에서 UE 세탁 금지

바이트 쓰기 `pm_write8()` 는 워드를 읽어 디코드하고, 바이트를 병합해 새 ECC 로 쓴다. 옛 워드가 UE 면 병합 후에도 **poison**(체크 c0,c1 을 뒤집어 s=3, p=0 → 반드시 UE)을 남겨야 한다. 세탁(`launder`) 결함을 잡는 시험을 쓰고, CE 워드의 RMW 는 정정 후 병합되는지도 확인하라.

<details>
<summary>답 보기</summary>

실제 출력: `good : RMW on UE word -> ue_cnt=1, next read SLVERR` / `launder: next read OKAY data=0x11223344556674AB (bits 8,9 still flipped)`. 세탁된 워드는 다음 읽기가 OKAY 이고, 바이트 1 의 비트 8,9 가 틀린 채로 "유효한 ECC" 를 갖는다 — 이후 어떤 시험도 이 오염을 볼 수 없다. CE 워드의 RMW 는 정정 후 병합되어 `0x00FEF00DCAFEF00D`, CE 는 RMW 의 읽기에서 1번만 세진다. 실제 버스에서는 AXI5/CHI 의 poison 표시 같은 메커니즘이 오염 데이터를 쓰기 경로 너머까지 운반한다. 시험 포인트: 부분 쓰기 크기(1/2/4 바이트) × 옛 워드 상태(OK/CE/UE).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 14/14 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san   # 14/14 passed, 보고 없음
```

## 10. 핵심 정리

- 검증 대상은 코덱이 아니라 **보고**: STATUS, 버스 응답, 정정 데이터, CE/UE 카운터, 오류 주소·신드롬·비트 번호, IRQ. 하나라도 빠지면 그것만 틀리는 결함을 놓친다.
- 기대값 = H·(주입 마스크). 선형이라 데이터와 무관 → 데이터는 랜덤으로 흔들고 신드롬은 독립 계산.
- 매트릭스: D1 64 + C1 8(c7 은 s=0,p=1) + D2(경계 0/63, 31/32 + 랜덤) + DC2 + C2 + NONE, walking 주소. 3비트 이상은 스펙 밖 → 분포만.
- 주입 후 **bypass 읽기로 착지 확인** — 주입기 결함을 ECC 결함으로 오진하지 않는다.
- 비교 순서는 STATUS 먼저(판정이 근본 원인), UE 는 데이터 비교 생략, CE 는 데이터 필수(`no_correct` 는 다른 모든 게 맞다).
- 매트릭스 밖: 첫 오류 캡처·OVF·부분 clear·재무장, 반복 읽기와 scrub, 카운터 포화(넘겨서 확인).
- 리포트는 첫 실패 + 원인별 집계: `ignore_parity` 의 첫 줄은 ERROR_MISSED 지만 본질은 MISCORRECT_SDC 41.
- 현장: inject/poison 레지스터, Arm RAS pseudo-fault, ACPI EINJ, Linux EDAC ce/ue_count, ISO 26262 latent fault 를 위한 부팅 자가 시험.

## 참고자료

- Richard W. Hamming, "Error Detecting and Error Correcting Codes", Bell System Technical Journal, 1950 — 위치 XOR 신드롬의 원형
- M. Y. Hsiao, "A Class of Optimal Minimum Odd-weight-column SEC-DED Codes", IBM Journal of R&D, 1970 — 실제 DRAM ECC 의 표준 코드
- Linux kernel `Documentation/driver-api/edac.rst` 와 `Documentation/admin-guide/ras.rst` — EDAC sysfs(ce_count, ue_count), CE/UE 정의
- Linux kernel `drivers/ras/cec.c` (CONFIG_RAS_CEC) — CE collector: 임계치를 넘는 페이지를 offline
- Linux kernel `Documentation/firmware-guide/acpi/apei/einj.rst` — ACPI EINJ 오류 주입 (error_type, param1, error_inject)
- Linux kernel `drivers/edac/synopsys_edac.c` — uMCTL2 계열 DDR 컨트롤러의 ECC 상태·주소·신드롬·poison 레지스터 사용 예
- Arm Reliability, Availability, and Serviceability (RAS) Specification (Armv8/Armv9) — ERR\<n\>STATUS(CE/UE/OF), ERR\<n\>ADDR, pseudo-fault generation
- AMBA AXI Protocol Specification — RRESP 인코딩(OKAY/EXOKAY/SLVERR/DECERR), AXI5 의 poison
- JEDEC JESD79-5 (DDR5) — on-die ECC 와 ECS(error check and scrub) 모드
- ISO 26262-5 — 하드웨어 안전 메커니즘과 latent fault metric
- 관련 레슨: MEM-B07 (SECDED(72,64) 코덱), SIV-B02 (mock MMIO), SIV-B05/B06 (walking·주소 디코드), SIV-B09 (poll-with-timeout), SIV-B11 (IRQ trigger-check-clear), SIV-B14 (리포트), SIV-M05 (DDR 스트레스), SIV-M13 (커버리지)
