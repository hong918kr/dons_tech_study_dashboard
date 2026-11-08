---
id: SIV-B13
time: 70분
updated: 2026-11-08
---
# SIV-B13 실리콘 검증 개요: Pre-silicon vs Post-silicon

## 학습 목표

- 칩 개발의 다섯 단계 — **시뮬레이션(SIM) · 에뮬레이션(EMU) · FPGA 프로토타입 · first silicon · 양산(PROD)** — 의 속도·가시성(visibility)·수정 비용을 표 하나로 말할 수 있다.
- 버그를 "그 버그를 보려면 무엇이 필요한가(논리, 4-state X, 실제 IO, 실제 타이밍, 아날로그, PVT, 대량 유닛) + 몇 사이클이 필요한가" 로 분해해, **가장 일찍 잡을 수 있는 단계** 를 코드(`earliest_stage`)로 판정한다.
- 레지스터 테스트(SIV-B03~B05)를 `reg_read32/reg_write32` 위에만 쓰고, 아래의 **bus 백엔드(mock / JTAG·OpenOCD / 직접 MMIO)** 만 바꿔 같은 테스트를 재사용하는 골격을 구현한다.
- 버스 오류(JTAG 끊김, SLVERR)를 레지스터 불일치와 섞지 않고 **INFRA** 로 분리한다 — 안 그러면 "죽은 버스가 PASS" 가 나온다.
- 같은 스위트를 pre-silicon 모델과 A0 실리콘에 돌린 두 결과로 **RTL_BUG / SILICON_ONLY / MODEL_MISMATCH / DIVERGENT / INFRA** 를 판정하고 다음 디버그 행동을 정한다.

## 1. 문제

검증 면접(Rivian Silicon & Sensing, NVIDIA/Apple post-silicon validation)의 단골 첫 질문은 "pre-silicon 과 post-silicon 검증은 뭐가 다르고, 어떻게 연결하나?" 다 — first silicon 이 도착한 날 아침에 돌릴 테스트가 이미 준비돼 있는지가 이 답에 달려 있다.

칩은 RTL 이 완성된 뒤에도 실리콘이 나오기까지 수개월이 걸린다. 그동안 검증팀은 RTL 을 **느리지만 다 보이는** 플랫폼(시뮬레이터)과 **빠르지만 덜 보이는** 플랫폼(에뮬레이터, FPGA)에서 돌린다. 실리콘이 오면 속도는 GHz 로 올라가고 실제 타이밍·아날로그가 생기지만, 내부 신호는 거의 안 보인다. 단계를 지날수록 **찾을 수 있는 버그의 종류는 늘고, 보이는 것은 줄고, 고치는 비용은 커진다.**

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">단계를 지날수록: 속도 ↑ · 가시성 ↓ · 수정 비용 ↑ · 볼 수 있는 현상 ↑</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="36" width="130" height="44" rx="6"/><rect x="157" y="36" width="130" height="44" rx="6"/>
    <rect x="304" y="36" width="130" height="44" rx="6"/><rect x="451" y="36" width="130" height="44" rx="6"/>
    <rect x="598" y="36" width="132" height="44" rx="6"/>
    <path d="M140 58 L155 58 M287 58 L302 58 M434 58 L449 58 M581 58 L596 58"/>
  </g>
  <rect x="451" y="36" width="130" height="44" rx="6" class="f-acc" opacity=".35"/>
  <g fill="currentColor" text-anchor="middle" font-weight="bold">
    <text x="75" y="56">SIM</text><text x="222" y="56">EMU</text><text x="369" y="56">FPGA</text>
    <text x="516" y="56">FIRST SILICON</text><text x="664" y="56">PROD</text>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="75" y="73">RTL, 4-state</text><text x="222" y="73">RTL 매핑, 2-state</text><text x="369" y="73">RTL 합성, 2-state</text>
    <text x="516" y="73">A0 평가보드</text><text x="664" y="73">ATE · SLT · 필드</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="104">속도</text><text x="10" y="154">가시성</text><text x="10" y="204">수정 비용</text>
  </g>
  <g class="f-ok">
    <rect x="60" y="112" width="12" height="14"/><rect x="207" y="112" width="40" height="14"/>
    <rect x="354" y="112" width="70" height="14"/><rect x="501" y="112" width="120" height="14"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="78" y="124">Hz~kHz</text><text x="251" y="124">~MHz</text><text x="428" y="124">수~수십 MHz</text><text x="625" y="124">GHz</text>
  </g>
  <g class="f-mut">
    <rect x="60" y="162" width="120" height="14"/><rect x="207" y="162" width="100" height="14"/>
    <rect x="354" y="162" width="45" height="14"/><rect x="501" y="162" width="25" height="14"/>
    <rect x="648" y="162" width="8" height="14"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="60" y="190">모든 신호</text><text x="207" y="190">거의 전부(깊이 제한)</text><text x="354" y="190">고른 프로브</text>
    <text x="501" y="190">레지스터·스캔·trace</text><text x="648" y="190">pass/fail</text>
  </g>
  <g class="f-warn">
    <rect x="60" y="212" width="8" height="14"/><rect x="207" y="212" width="10" height="14"/>
    <rect x="354" y="212" width="16" height="14"/><rect x="501" y="212" width="80" height="14"/>
    <rect x="648" y="212" width="82" height="14"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="60" y="244">RTL 수정+회귀</text><text x="207" y="244">+재컴파일</text><text x="354" y="244">+비트스트림</text>
    <text x="501" y="244">ECO/respin, errata</text><text x="648" y="244">리콜·FW 패치</text>
    <text x="10" y="264">막대 길이는 상대 비교용 어림값이다 (칩·플랫폼마다 다르다)</text>
  </g>
</svg>
```

이 레슨은 두 가지를 코드로 만든다. ① 버그를 단계별로 분류하는 작은 모델, ② **같은 레지스터 테스트를 pre-silicon 모델과 실보드에 그대로 돌리는 골격**. 두 번째가 실무의 핵심이다 — pre-silicon 에서 쓴 테스트를 실리콘 날에 다시 짜면 bring-up 이 몇 주 늦어진다. 레지스터 맵은 가상의 QSPI 컨트롤러(7개 레지스터, base `0x4001_0000`)이고, mock 은 SIV-B02 의 것을 축약해 넣었다 (RW/RO/W1C/WO + force 레지스터 + 결함 주입).

### Part A — 버그 20개를 발견 단계별로 분류

단계마다 `caps`(볼 수 있는 현상의 비트마스크)와 `log10_cycles`(테스트 하나에 현실적으로 쓸 수 있는 사이클 수의 log10)가 있다. 버그는 `needs` 와 `log10_cycles` 를 가진다.

- `earliest_stage(b)` — `caps ⊇ needs` 이고 사이클 예산 안에 드는 **가장 이른 단계**. 없으면 `ST_NONE`.
- `missing_caps(s, b)` — 단계 s 에서 못 보는 이유 = `needs` 중 s 에 없는 능력.
- `stage_histogram(bugs, n, hist)` — 단계별 개수, 반환값은 `ST_NONE` 개수.

### Part B — 백엔드만 바꿔 같은 테스트 돌리기

- `reg_read32(off, &v)` / `reg_write32(off, v)` — 현재 `bus_t` 로 `base + off` 접근을 위임하고, 상태를 그대로 돌려준다. **성공한** 트랜잭션만 `txn_log` 에 (방향, 오프셋, 값) 지문을 남긴다.
- `jtag_read32` / `jtag_write32` — OpenOCD 명령 `mdw 0x%08x` / `mww 0x%08x 0x%08x` 를 transport 로 보내고 응답을 검사한다: 링크 오류 → `TIMEOUT`, `ERR…` → `SLVERR`, 형식 오류나 **주소 echo 불일치** → `PROTO`.
- `run_test(t, r)` — 테스트 실행 후, 버스가 한 번이라도 실패했으면 비교 결과와 상관없이 `R_INFRA`.

### Part C — 같은 스위트, 두 단계, triage

- `triage(pre, pre_sig, post, post_sig)` — (모델 결과, 실리콘 결과) 조합으로 판정. `sig` 는 첫 실패 문자열(`reg=… exp=… act=… bits=[…]`).
- `run_campaign(pre_bus, post_bus, base, c)` — 스위트 전체를 pre 에, 그다음 post 에 돌리고 테스트마다 triage, 판정별 개수 집계.
- 결함 주입: 모델·실리콘 공통 RTL 버그(INT_STAT 의 W1C 가 RW 로 구현), 실리콘만의 결함(CTRL 리셋 tie-off 오류, SCRATCH bit13 stuck-at-1), 옛 스펙으로 만든 모델(ID 불일치), JTAG 링크 끊김.

## 2. Problem (English)

> **Pre-silicon vs post-silicon: classify bugs by stage, and reuse one register test suite across backends.**
>
> 1. Each stage `SIM, EMU, FPGA, SILICON, PROD` has a capability mask (`LOGIC, 4STATE, REALIO, TIMING, ANALOG, PVT, VOLUME`) and a practical cycle budget (log10). Each bug needs a capability set and a number of cycles to manifest. Implement `stage_t earliest_stage(const bug_t *b)`, `uint32_t missing_caps(stage_t s, const bug_t *b)` and `int stage_histogram(const bug_t *bugs, int n, int hist[ST_COUNT])`.
> 2. Register tests call only `bus_status_t reg_read32(uint32_t off, uint32_t *val)` / `reg_write32(uint32_t off, uint32_t val)`, which dispatch through a `bus_t { read32, write32, ctx }`. Implement the dispatch, and an OpenOCD-style backend that sends `mdw 0x40010000` / `mww 0x40010018 0xcafef00d` and parses `0x40010000: 51530a01`. Map link loss to `TIMEOUT`, an `ERR` reply to `SLVERR`, and a malformed reply or a wrong address echo to `PROTO`. A test whose bus access failed must be reported as `INFRA`, never as PASS or FAIL.
> 3. Run the same suite on the pre-silicon model and on silicon, and triage each test: both PASS → `PASS`; pre PASS / post FAIL → `SILICON_ONLY`; pre FAIL / post PASS → `MODEL_MISMATCH`; both FAIL with the same first-failure signature → `RTL_BUG`, otherwise `DIVERGENT`; any `INFRA` → `INFRA`.
>
> Example: with a CTRL reset tie-off error on silicon only, `reset_value` gives `pre=PASS post=FAIL -> SILICON_ONLY post: reg=CTRL off=0x004 exp=0x00000400 act=0x00000600 diff=0x00000200 bits=[9]`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # SDK 27.2 링크가 깨진 맥에서만
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter   # 11/43 passed, exit 1
```

시작 코드를 돌려 보면 눈여겨볼 장면이 있다. `reg_read32` 가 TODO 라 **모든 버스 접근이 `BUS_ERR_PROTO` 로 실패하는데**, 캠페인 출력은 이렇게 나온다 (실제 출력 일부):

```text
FAIL starter.c:390  reg_read32(0x00, &v) == BUS_OK && v == 0x51530A01u
FAIL starter.c:391  reg_read32(0x40, &v) == BUS_ERR_SLVERR
FAIL starter.c:397  mock_txn > 100
...
== A0 bring-up: RTL model vs A0 silicon (JTAG)
  reset_value  pre=PASS  post=PASS  -> PASS
  rw_pattern   pre=PASS  post=PASS  -> PASS
  walking_1_0  pre=PASS  post=PASS  -> PASS
  w1c_clear    pre=PASS  post=PASS  -> PASS
...
11/43 passed
```

버스가 죽었는데 스위트가 **전부 PASS** 다. 테스트가 첫 접근에서 조용히 return 하고, 시작 코드의 `run_test` 는 "실패 기록이 0개 = PASS" 로 판단하기 때문이다. 실제 랩에서 JTAG 케이블이 빠진 밤샘 회귀가 초록불로 끝나는 바로 그 버그다 — Part B 의 `R_INFRA` 가 이걸 막는다.

## 4. 힌트

<details>
<summary>힌트 1 — earliest_stage 는 부분집합 검사 + 예산 비교</summary>

"needs 가 caps 의 부분집합" 은 `(caps & needs) == needs`. 단계 배열이 이른 순서로 정렬돼 있으니 앞에서부터 처음 맞는 것을 돌려주면 된다. `missing_caps` 는 `needs & ~caps` 한 줄.

</details>

<details>
<summary>힌트 2 — 접근 계층은 얇게, 상태는 그대로</summary>

`g_bus->read32(g_bus->ctx, g_base + off, val)` 의 반환값을 그대로 돌려준다. 지문(`txn_log`)에는 **오프셋** 을 넣는다 — 절대 주소를 넣으면 base 가 다른 플랫폼(예: FPGA 빌드의 다른 메모리 맵)끼리 비교할 수 없다. 실패한 접근은 기록하지 않는다 (값이 의미 없다).

</details>

<details>
<summary>힌트 3 — 원격 응답은 믿지 말고 검사</summary>

`sscanf(resp, "0x%x: %x %c", &a, &v, &extra)` 가 정확히 2 를 돌려주고 `a == addr` 일 때만 성공. `%c` 는 뒤에 쓰레기가 붙은 응답을 걸러낸다(공백은 건너뛰므로 OpenOCD 의 끝 공백은 통과). 주소 echo 검사는 응답이 다른 요청의 것(버퍼 밀림)인지 확인하는 싼 방법이다.

</details>

<details>
<summary>힌트 4 — triage 는 순서가 전부</summary>

INFRA 를 **맨 먼저** 거른다 (한쪽 결과가 무효면 비교 자체가 무효). 그다음 PASS/PASS, 한쪽만 FAIL 두 경우, 마지막에 둘 다 FAIL 이면 `strcmp(pre_sig, post_sig)` 로 같은 증상인지 본다.

</details>

## 5. 풀이

### Part A — 단계별 특성과 20개 버그 분류

먼저 단계별 특성표. 숫자는 플랫폼·설계 크기에 따라 크게 달라지는 **어림값** 이다.

| 단계 | 대표 플랫폼 | 속도 (전체 SoC) | 가시성 | 수정 비용 | 잘 잡는 것 |
|---|---|---|---|---|---|
| SIM | 이벤트 시뮬레이터 (VCS, Xcelium, Questa), Verilator | Hz~kHz (블록 단위는 훨씬 빠름) | **모든 신호**, 4-state X/Z (Verilator 는 2-state) | RTL 수정 + 회귀: 시간~일 | 논리, X 전파, 프로토콜 corner (VIP) |
| EMU | Palladium, Veloce, ZeBu | ~1 MHz 대 | 거의 모든 신호, 단 캡처 깊이·창 제한 | 위 + 재컴파일 수 시간 | OS 부팅, 펌웨어+HW, 10^9~10^11 사이클 시나리오 |
| FPGA | HAPS, Protium, 자체 보드 | 수~수십 MHz | 미리 고른 프로브 (ILA). 바꾸려면 재합성 | 위 + 비트스트림 재빌드 | 장시간 SW, 실제 IO interop (속도 낮춰서) |
| FIRST SILICON | 평가보드 + JTAG/SWD, 랩 장비 | GHz (실속도) | 레지스터, 스캔 덤프, on-chip trace (CoreSight ETM/ELA), 핀·오실로스코프 | metal ECO 수 주~수 개월, base-layer respin 은 더 길고 마스크 비용 큼. 또는 FW 우회 + errata | 타이밍, 아날로그, PVT, 실워크로드 |
| PROD | ATE, SLT, 필드 | — | pass/fail, bin, 필드 오류 로그 | 스크린 추가, FW 업데이트, errata, 리콜 | 공정 산포, 경년 열화, 희귀 유닛 결함 |

`STAGES[]` 는 이 표를 능력 비트와 예산으로 줄인 것이다: SIM `LOGIC|4STATE`·10^8, EMU/FPGA `LOGIC|REALIO`·10^11/10^12, SILICON 에 `TIMING|ANALOG|PVT` 가 더해져 10^14, PROD 에 `VOLUME` 이 더해진다. 사이클 예산은 "속도 × 하루 남짓" 이다 (1 kHz × 86400 s ≈ 10^8). 이제 20개 버그를 손으로 분류해 보자. 마지막 열이 `missing_caps` 또는 예산 초과다.

| # | 버그 | needs | 10^k | 최초 단계 | 그 앞에서 못 잡는 이유 |
|---|---|---|---|---|---|
| 0 | FIFO full 플래그 off-by-one | LOGIC | 4 | SIM | — |
| 1 | 레지스터 리셋값 오류 (RTL) | LOGIC | 2 | SIM | — |
| 2 | W1C 를 RW 로 구현 | LOGIC | 2 | SIM | — |
| 3 | 주소 디코더가 bit 4 무시 | LOGIC | 3 | SIM | — |
| 4 | 리셋 없는 플롭 → X 전파 | LOGIC, 4STATE | 3 | SIM | (EMU/FPGA 는 `4STATE` 없음 → X 가 0/1 로 둔갑) |
| 5 | 아비터 starvation (부팅 트래픽) | LOGIC | 10 | EMU | SIM 예산 10^8 초과 |
| 6 | Linux 부팅 중 드라이버-HW 행 | LOGIC | 10 | EMU | SIM 예산 초과 |
| 7 | 멀티코어 캐시 일관성 deadlock | LOGIC | 11 | EMU | SIM 예산 초과 |
| 8 | 실제 USB 호스트와 interop | LOGIC, REALIO | 9 | EMU | SIM 에는 실제 상대 장치가 없다 (VIP 는 스펙 해석이 같을 수 있다) |
| 9 | 카운터 rollover 경쟁 | LOGIC | 12 | FPGA | EMU 예산 10^11 초과 |
| 10 | slow 코너 setup 위반 | TIMING, PVT | 6 | SILICON | FPGA 타이밍 ≠ ASIC 타이밍 |
| 11 | CDC 메타안정 → FIFO 오염 | TIMING | 12 | SILICON | RTL 시뮬은 메타안정을 모델링하지 않는다 |
| 12 | 저온 PLL lock 실패 | ANALOG, PVT | 3 | SILICON | 아날로그는 behavioral 모델뿐 |
| 13 | DDR PHY 아이 마진 부족 | ANALOG, TIMING | 9 | SILICON | 위와 같음 |
| 14 | 긴 채널 SerDes BER 초과 | ANALOG, REALIO | 13 | SILICON | 아날로그 + 10^13 비트 |
| 15 | 전원 시퀀스 → POR 글리치 | ANALOG | 1 | SILICON | 전원망은 RTL 에 없다 |
| 16 | 전 코어 동시 토글 → 전압 droop | ANALOG, TIMING | 9 | SILICON | 위와 같음 |
| 17 | 실워크로드 10^13 사이클 후 TLB errata | LOGIC | 13 | SILICON | **논리 버그지만** FPGA 예산 10^12 초과 |
| 18 | 저전압 SRAM 약한 셀 (0.1% 유닛) | ANALOG, PVT, VOLUME | 3 | PROD | 몇 개 샘플로는 안 걸린다 |
| 19 | 트랜지스터 경년 열화 | ANALOG, VOLUME | 15 | PROD | 시간 + 대량 |

결과 히스토그램 `{SIM 5, EMU 4, FPGA 1, SILICON 8, PROD 2}` — 테스트가 그대로 확인한다. 이 표에서 읽어야 할 세 가지:

1. **실리콘에서 처음 보이는 버그의 대부분은 "논리가 아닌" 버그다** (#10~#16). 그래서 post-silicon 검증의 도구는 shmoo, PVT 매트릭스, 아이 스캔, BER 측정(SIV-M05~M08)처럼 *마진* 을 재는 쪽으로 간다.
2. **#17 같은 논리 버그도 사이클 예산 때문에 실리콘까지 간다.** 실제 예: AMD Phenom(B2 스테핑)의 TLB 관련 errata 는 출시 무렵 공개돼 BIOS 우회(성능 손해)로 막았고 B3 스테핑에서 고쳐졌다. 1994년 Pentium FDIV 는 나눗셈 룩업 테이블 항목 누락이라는 순수 논리 버그였는데, 필드에서 발견돼 Intel 이 4억 7,500만 달러를 비용 처리했다.
3. **#4 는 거꾸로 간다.** X 전파 버그는 4-state 시뮬레이터에서만 X 로 보인다. 에뮬레이터와 FPGA 는 2-state 라 리셋 안 된 플롭이 "어쩌다 0" 으로 시작해 테스트를 통과할 수 있다 — 그리고 실리콘에서는 전원 켤 때마다 랜덤한 값이 된다. "빠른 플랫폼이 느린 플랫폼을 대체한다" 가 아닌 이유다.

> 이 모델의 "최초 단계" 는 **동적 테스트로 증상을 관측** 할 수 있는 첫 단계다. 정적 기법은 별도 축이다: STA 는 #10 을, CDC lint(구조 검사)는 #11 을, 형식 검증(formal)은 #9 같은 깊은 상태 버그를 RTL 단계에서 잡도록 설계돼 있다. 그런데도 실리콘까지 가는 이유는 모델의 한계다 — 라이브러리 특성화·기생 추출·온칩 편차(OCV) 모델이 실제와 다르거나, 제약(constraint)이 빠졌기 때문. 이것을 줄이는 노력 전체를 **shift-left** 라고 부른다 (추가 연습 2).

```c
static stage_t earliest_stage(const bug_t *b) {
    for (int s = 0; s < ST_COUNT; s++)
        if ((STAGES[s].caps & b->needs) == b->needs && b->log10_cycles <= STAGES[s].log10_cycles)
            return (stage_t)s;
    return ST_NONE;   /* 10^19 사이클 후 한 번: 어떤 테스트로도 못 잡는다 → 필드 escape */
}
```

### Part B — 백엔드 교체 골격

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">테스트는 위 두 층만 안다 — 아래는 단계마다 바꿔 끼운다</text>
  <rect x="150" y="32" width="440" height="34" rx="5" class="f-ok" opacity=".45"/>
  <rect x="150" y="32" width="440" height="34" rx="5" fill="none" stroke="currentColor"/>
  <text x="370" y="54" fill="currentColor" text-anchor="middle">t_reset · t_rw · t_walk · t_w1c  (SIV-B03~B05, B11 축약)</text>
  <rect x="150" y="84" width="440" height="34" rx="5" class="f-acc" opacity=".35"/>
  <rect x="150" y="84" width="440" height="34" rx="5" fill="none" stroke="currentColor"/>
  <text x="370" y="106" fill="currentColor" text-anchor="middle" font-family="monospace">reg_read32(off) / reg_write32(off) → status + txn 지문</text>
  <rect x="150" y="136" width="440" height="30" rx="5" fill="none" stroke="currentColor"/>
  <text x="370" y="156" fill="currentColor" text-anchor="middle" font-family="monospace">bus_t { read32, write32, ctx }</text>
  <g stroke="currentColor" fill="none">
    <path d="M370 66 L370 84 M370 118 L370 136"/>
    <path d="M240 166 L110 196 M370 166 L370 196 M500 166 L630 196"/>
    <rect x="10" y="196" width="200" height="120" rx="5"/>
    <rect x="270" y="196" width="200" height="120" rx="5"/>
    <rect x="530" y="196" width="200" height="120" rx="5"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="20" y="216" font-weight="bold">mock_t (모델)</text>
    <text x="20" y="236">레지스터 기술표 + access 의미</text>
    <text x="20" y="254">결함 주입: tie-off, stuck-at,</text>
    <text x="20" y="272">W1C→RW</text>
    <text x="20" y="296">pre-si: DPI-C/transactor</text>
    <text x="20" y="310">→ RTL (EMU-M03)</text>
    <text x="280" y="216" font-weight="bold">jtag_t (원격)</text>
    <text x="280" y="236" font-family="monospace">"mdw 0x40010000"</text>
    <text x="280" y="254">→ OpenOCD (TCL :6666)</text>
    <text x="280" y="272">→ JTAG/SWD → DAP MEM-AP</text>
    <text x="280" y="290">→ SoC 버스 → IP</text>
    <text x="280" y="310">오류 = 반환값 (TIMEOUT 등)</text>
    <text x="540" y="216" font-weight="bold">mmio_t (직접)</text>
    <text x="540" y="236" font-family="monospace">*(volatile u32 *)va</text>
    <text x="540" y="254">베어메탈: va = pa</text>
    <text x="540" y="272">Linux: mmap(/dev/mem, UIO)</text>
    <text x="540" y="290">오류 = BusFault/SError 예외</text>
    <text x="540" y="310">→ 반환값으로 못 받는다</text>
  </g>
</svg>
```

**왜 함수 포인터 한 층인가.** 같은 테스트를 단계마다 재사용하는 경로는 실무에서 여러 가지다: (a) 테스트를 SoC 의 CPU 용으로 컴파일해 RTL 시뮬/에뮬레이션 안의 CPU 가 실행 (같은 바이너리가 실리콘에서도 돈다), (b) 호스트 C 가 DPI-C·transactor 로 RTL 버스를 구동 (EMU-M03, EMU-M02), (c) 실리콘에서 호스트 C 가 JTAG/OpenOCD 로 원격 접근, (d) 실리콘 위 펌웨어나 Linux 유저공간이 MMIO 로 직접 접근. 어느 경우든 테스트 본문이 "어떻게 버스에 닿는지" 를 모르면 재사용된다. 그 경계가 `reg_read32/reg_write32` 이고, 그 아래 `bus_t` 가 바뀐다 (EMU-B04 의 HAL 경계와 같은 발상, BM-B02 의 read/write hook 의 일반화). UVM 에서 레지스터 모델(`uvm_reg_block`)이 `uvm_reg_adapter` 로 버스 에이전트와 분리돼 있는 것과 같은 구조이고, Accellera PSS(Portable Stimulus)는 이 재사용을 표준화하려는 시도다.

구현에서 중요한 결정 네 가지:

| 결정 | 이유 | 이 코드에서 |
|---|---|---|
| 읽기는 **상태 반환** + 값은 out 인자 | 원격 백엔드는 실패할 수 있다. 값 반환형(`uint32_t reg_read32(off)`)은 오류를 값(0, 0xFFFFFFFF)과 구분할 수 없다 | `bus_status_t reg_read32(uint32_t off, uint32_t *val)` |
| 지문은 **오프셋** 기준 | FPGA 빌드·다른 SoC 통합에선 base 가 다르다 | `txn_log('R', off, *val)` |
| 성공한 접근만 기록 | 실패한 읽기의 값은 쓰레기다 | `if (s == BUS_OK) txn_log(...)` |
| 버스 실패 → INFRA | 가짜 mismatch 수십 줄이 진짜 실리콘 버그처럼 보이면 triage 가 엉망이 된다 | `if (r->infra) return R_INFRA;` |

테스트가 확인하는 것: 깨끗한 mock 과 깨끗한 "실리콘"(JTAG 너머의 mock)에서 4개 테스트가 모두 PASS 이고, **트랜잭션 수와 FNV 지문이 완전히 같다** (`g_txn == mock_txn && g_txn_hash == mock_hash`). 즉 두 백엔드에서 테스트가 *같은 순서로 같은 값을 읽고 썼다*. 이게 "재사용" 의 기계적 증거다 — 결과가 다르면 백엔드 차이가 아니라 하드웨어 차이다.

**JTAG 백엔드.** OpenOCD 는 telnet(기본 4444)과 TCL 서버(기본 6666, 명령·응답 끝을 `0x1a` 로 구분)를 제공하고, `mdw addr` 는 `0x40010000: 51530a01` 형식으로 출력한다. 이 레슨의 `target_t` 는 그 서버 + 보드를 흉내 내는 가짜 transport 다 (실보드라면 `xfer` 가 소켓 send/recv 가 된다). 응답 검사 세 단계:

```c
if (j->xfer(j->tctx, j->last_cmd, resp, sizeof resp) != 0) return BUS_ERR_TIMEOUT;  /* 링크 */
if (strncmp(resp, "ERR", 3) == 0) return BUS_ERR_SLVERR;                             /* 타깃 버스 */
if (sscanf(resp, "0x%x: %x %c", &a, &v, &extra) != 2 || a != addr) return BUS_ERR_PROTO;
```

`garbled_xfer` 는 항상 `0x40010004: …` 를 돌려주는 고장난 transport 다. `0x40010000` 을 읽었는데 `0x40010004` 의 응답이 오면 값이 아무리 그럴듯해도 `PROTO` 다 — 응답 버퍼가 한 칸 밀리면 모든 레지스터가 "옆 레지스터 값" 으로 FAIL 하는, 디버그하기 끔찍한 상황을 입구에서 막는다.

**INFRA 분리.** `drop_after = 3` 이면 4번째 명령부터 링크가 끊긴다. `t_reset` 은 4번째 읽기에서 `rd()` 가 false → 즉시 return, `r.fails == 0`, 결과 `R_INFRA`. 시작 코드의 `run_test` 였다면 PASS 다 (3. 시작 코드의 출력). 반대로 "끊긴 뒤에도 계속 읽고 실패를 기록" 하는 구현이었다면 `act=0x00000000` FAIL 이 수십 줄 나왔을 것이다. 둘 다 틀렸다.

**mmio 백엔드와 "RAM 은 모델이 아니다".** 호스트에서 `mmio_t` 의 `va` 를 평범한 배열로 두면 접근 자체는 동작한다(`ram[6] == 0x1234`). 하지만 reset 테스트는 `reg=ID off=0x000 exp=0x51530A01 act=0x00000000 … bits=[0,9,11,16,17,20,22,24,28,30]` 로 실패한다. 접근 의미(RO, W1C, force)와 리셋값이 없는 메모리는 레지스터 모델이 아니다 — 그래서 mock 에 access type 이 필요하다 (SIV-B02). 실보드의 Linux 유저공간 백엔드는 대략 이렇다 (이 맥에는 `/dev/mem` 이 없어 실행하지 않은 스니펫):

```c
int fd = open("/dev/mem", O_RDWR | O_SYNC);                 /* 또는 /dev/uioN */
void *p = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0x40010000);
mmio_t mm = { (volatile uint32_t *)p, 0x40010000u };        /* 이후는 본문 mmio_read32 그대로 */
```

이 경로에서 꺼진 클럭 도메인의 IP 를 읽으면 인터커넥트 설계에 따라 버스가 멈추거나, 오류 응답(SLVERR/DECERR → SError/BusFault)이 오거나, `0xDEADBEEF` 같은 고정 패턴이 읽힌다. 반환값으로 오류를 못 받으므로, 직접 MMIO 하네스는 예외 핸들러에서 오류를 기록하고 테스트 쪽 sticky 플래그로 넘겨 주는 식으로 같은 INFRA 의미를 만든다 (Q4).

### Part C — 같은 테스트를 두 번 돌리는 이유: triage

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">pre(모델/RTL) × post(실리콘) 결과 → 판정과 다음 행동</text>
  <g fill="currentColor" font-weight="bold" text-anchor="middle">
    <text x="300" y="48">post PASS</text><text x="560" y="48">post FAIL</text>
  </g>
  <g fill="currentColor" font-weight="bold"><text x="20" y="92">pre PASS</text><text x="20" y="162">pre FAIL</text></g>
  <rect x="170" y="60" width="260" height="60" class="f-ok" opacity=".5"/>
  <rect x="430" y="60" width="260" height="60" class="f-warn" opacity=".5"/>
  <rect x="170" y="120" width="260" height="70" class="f-mut" opacity=".5"/>
  <rect x="430" y="120" width="260" height="70" class="f-acc" opacity=".4"/>
  <g stroke="currentColor" fill="none"><rect x="170" y="60" width="520" height="130"/>
    <path d="M430 60 L430 190 M170 120 L690 120"/></g>
  <g fill="currentColor" font-size="12">
    <text x="180" y="84" font-weight="bold">PASS</text>
    <text x="180" y="104">다음 테스트로</text>
    <text x="440" y="84" font-weight="bold">SILICON_ONLY</text>
    <text x="440" y="104">타이밍·아날로그·tie-off·제조 → 여러 유닛</text>
    <text x="180" y="144" font-weight="bold">MODEL_MISMATCH</text>
    <text x="180" y="164">모델/테스트/스펙 버전 확인</text>
    <text x="180" y="180">→ 실리콘 버그로 올리지 않는다</text>
    <text x="440" y="144" font-weight="bold">같은 sig → RTL_BUG</text>
    <text x="440" y="162">시뮬에서 재현·파형 디버그</text>
    <text x="440" y="180">다른 sig → DIVERGENT (둘 다 조사)</text>
  </g>
  <rect x="170" y="204" width="520" height="34" class="f-warn" opacity=".25"/>
  <rect x="170" y="204" width="520" height="34" fill="none" stroke="currentColor"/>
  <text x="180" y="226" fill="currentColor" font-size="12"><tspan font-weight="bold">어느 쪽이든 INFRA</tspan> → 비교 무효. 링크·전원·클럭 확인 후 재실행 (먼저 걸러낸다)</text>
</svg>
```

pre-silicon 과 post-silicon 에 **같은 테스트** 를 돌렸기 때문에 이 표가 가능하다. 테스트가 달랐다면 "실리콘에서 실패했다" 는 사실 하나만 남고, 그게 실리콘 문제인지 테스트 문제인지부터 다시 따져야 한다. 실제 캠페인 출력 (solution 실행 결과):

```text
== A0 bring-up: RTL model vs A0 silicon (JTAG)
  reset_value  pre=PASS  post=FAIL  -> SILICON_ONLY   post: reg=CTRL off=0x004 exp=0x00000400 act=0x00000600 diff=0x00000200 bits=[9]
  rw_pattern   pre=PASS  post=FAIL  -> SILICON_ONLY   post: reg=SCRATCH off=0x018 exp=0x00000000 act=0x00002000 diff=0x00002000 bits=[13]
  walking_1_0  pre=PASS  post=FAIL  -> SILICON_ONLY   post: reg=SCRATCH off=0x018 exp=0x00000001 act=0x00002001 diff=0x00002000 bits=[13]
  w1c_clear    pre=FAIL  post=FAIL  -> RTL_BUG        post: reg=INT_STAT off=0x00C exp=0x00000000 act=0x00000001 diff=0x00000001 bits=[0]
== model built from old spec
  reset_value  pre=FAIL  post=PASS  -> MODEL_MISMATCH pre: reg=ID off=0x000 exp=0x51530A01 act=0x51530A00 diff=0x00000001 bits=[0]
  rw_pattern   pre=PASS  post=PASS  -> PASS
  walking_1_0  pre=PASS  post=PASS  -> PASS
  w1c_clear    pre=PASS  post=PASS  -> PASS
== different symptoms
  reset_value  pre=PASS  post=FAIL  -> SILICON_ONLY   post: reg=SCRATCH off=0x018 exp=0x00000000 act=0x00002000 diff=0x00002000 bits=[13]
  rw_pattern   pre=FAIL  post=FAIL  -> DIVERGENT      post: reg=SCRATCH off=0x018 exp=0x00000000 act=0x00002000 diff=0x00002000 bits=[13] pre: reg=SCRATCH off=0x018 exp=0xFFFFFFFF act=0xFFFFFFFE diff=0x00000001 bits=[0]
  walking_1_0  pre=FAIL  post=FAIL  -> DIVERGENT      post: reg=SCRATCH off=0x018 exp=0x00000001 act=0x00002001 diff=0x00002000 bits=[13] pre: reg=SCRATCH off=0x018 exp=0x00000001 act=0x00000000 diff=0x00000001 bits=[0]
  w1c_clear    pre=PASS  post=PASS  -> PASS
== JTAG link drop
  reset_value  pre=PASS  post=INFRA -> INFRA
  rw_pattern   pre=PASS  post=INFRA -> INFRA
  walking_1_0  pre=PASS  post=INFRA -> INFRA
  w1c_clear    pre=PASS  post=INFRA -> INFRA
43/43 passed
```

한 줄씩 디버그 엔지니어 입장에서 읽어 보자.

| 줄 | 판정 | 리포트가 알려 주는 것 | 다음 행동 |
|---|---|---|---|
| CTRL reset | SILICON_ONLY | bit 9 하나, 기대 CLKDIV=4 → 실제 6 | RTL 리셋값은 맞다(pre PASS). 넷리스트 tie-off·ECO 결과·퓨즈/OTP 기본값을 의심. 모든 유닛 공통이면 설계, 문서화 후 FW 가 부팅 때 덮어쓰기 (errata) |
| SCRATCH bit13 | SILICON_ONLY | 0 을 써도 bit13=1, walking 에서도 같은 비트 | 다른 유닛에서 재현되나? 한 유닛뿐이면 제조 결함 → ATE 스크린 누락 보고 (추가 연습 3) |
| INT_STAT bit0 | RTL_BUG | force 후 1 을 써도 안 지워짐, 모델과 실리콘이 **같은 서명** | 시뮬에서 재현 → 파형으로 원인. 실리콘은 metal ECO 후보 또는 SW 우회 (ack 대신 다른 경로) |
| ID bit0 | MODEL_MISMATCH | 모델만 `0x51530A00` | 모델(또는 테스트 기대값)이 옛 스펙 버전. **실리콘 버그 티켓을 열지 않는다** |
| SCRATCH 양쪽 다른 비트 | DIVERGENT | 모델 bit0, 실리콘 bit13 | 증상이 다르면 원인도 둘일 수 있다. 각각 따로 추적 |
| JTAG drop | INFRA | 실패 0건, 비교 없음 | 케이블·보드 전원·JTAG 클럭 확인 후 재실행. INFRA 비율 자체를 랩 건강 지표로 추적 |

"CTRL 리셋값이 RTL 에선 맞고 실리콘에선 틀리다" 가 어떻게 가능할까? 실제로 리셋값은 RTL 에서 상수로 쓰이지만, 실리콘으로 가는 길에 합성·ECO·스캔 삽입을 거치고, 일부 기본값은 퓨즈/OTP 에서 로드된다. 그래서 같은 리셋 테스트를 **게이트 수준 시뮬레이션** 에도 돌리는 팀이 많다 — 같은 테스트 재사용의 또 다른 이득이다. 반대로 INT_STAT 의 W1C 버그는 pre 에서도 실패했다: **이건 pre-silicon 에서 잡혔어야 하는 버그다**. 실제로 pre-silicon 회귀에 이 테스트가 있었다면 tapeout 전에 걸렸을 것이고, 그래서 "post-silicon 에서 RTL_BUG 판정이 나오면 pre-silicon 테스트 커버리지 구멍도 같이 보고한다" 가 성숙한 팀의 관행이다 (SIV-M13 커버리지).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 43/43 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 깨끗, 43/43
```

## 7. 자주 하는 실수

**1. "기록된 실패 0개 = PASS".** 시작 코드가 정확히 이 실수다. 버스가 죽었거나, 테스트가 레지스터를 하나도 못 읽었거나, 필터 때문에 0개 레지스터를 돌았어도 PASS 가 된다. 고친 코드: 버스 실패는 `R_INFRA`. 더 나아가면 "검사한 항목 수 > 0" 도 확인한다 (추가 연습의 `ok == NTESTS` 처럼 집계를 별도로 검증).

**2. 버스 오류를 값으로 돌려준다.** `uint32_t reg_read32(off)` 가 실패 시 0 이나 `0xFFFFFFFF` 를 돌려주면, 리셋값이 0 인 레지스터는 끊긴 링크에서도 PASS 다. PCIe 에서 링크가 내려간 장치의 설정 공간 읽기가 `0xFFFFFFFF` 로 오는 것과 같은 함정이다 — 값으로 "없음" 을 표현하는 순간 진짜 값과 구분할 수 없다.

**3. 지문/로그에 절대 주소를 쓴다.** FPGA 빌드에서 블록 base 가 다르면 두 로그가 전부 달라 보인다. 오프셋 + 블록 이름으로 기록한다.

**4. 원격 응답을 검증 없이 파싱.** `sscanf(resp, "%*s %x", &v)` 처럼 값만 뽑으면 주소가 다른 응답, 오류 문자열 일부, 이전 명령의 잔여 출력을 값으로 받아들인다. 형식·주소 echo·꼬리 문자를 다 본다.

**5. pre FAIL / post PASS 를 "실리콘은 괜찮으니 무시".** 모델과 실리콘이 다르다는 것 자체가 결함이다 — 다음 tapeout 의 pre-silicon 회귀가 이 모델을 쓴다. 모델·기대값·스펙 버전을 고친다.

**6. 실리콘에서 잡힌 버그를 "post-silicon 이 잘했다" 로 끝낸다.** RTL_BUG 판정은 pre-silicon 테스트 구멍의 증거다. 같은 테스트가 pre 회귀에 들어가 있는지 확인하고 넣는다.

## 8. 면접 Q&A

<details>
<summary>Q1. Pre-silicon 과 post-silicon 검증의 차이를 한 문장씩으로.</summary>

Pre-silicon 은 **설계(RTL)가 스펙대로인지** 를 느리지만 모든 신호가 보이는 환경에서 확인하고, post-silicon 은 **제조된 칩이 실제 속도·전압·온도·보드에서 스펙대로인지** 를 빠르지만 거의 안 보이는 환경에서 확인한다. 꼬리질문 "그럼 post-silicon 에선 논리 버그는 없나?" → 있다. 사이클 예산 때문에 pre 에서 못 닿은 깊은 시나리오(멀티코어, 장시간 워크로드)의 논리 버그가 나온다 (표의 #17). 다만 비율로는 타이밍·아날로그·PVT 쪽이 많다.

</details>

<details>
<summary>Q2. 실리콘에서 버그를 찾았는데 내부 신호가 안 보인다. 어떻게 디버그하나?</summary>

① 먼저 **pre-silicon 에서 재현** 을 시도한다 — 같은 테스트(이 레슨의 골격)와 같은 레지스터 설정으로 시뮬/에뮬에 돌린다. 재현되면 파형으로 끝. ② 안 되면 실리콘 쪽 가시성을 모은다: 레지스터·카운터 덤프, on-chip trace(CoreSight ETM/ELA, 디버그 버스 mux), 스캔 덤프(클럭을 멈추고 스캔 체인으로 플롭 상태를 빼서 RTL 시뮬 상태와 비교), 외부 핀·로직 분석기. ③ 조건을 좁힌다: 전압·주파수·온도 shmoo(SIV-M07), 유닛 간 비교, 펌웨어 bisect(SIV-M11). 꼬리질문 "스캔 덤프의 한계는?" → 클럭을 멈춘 한 시점의 스냅샷이고, 멈추는 순간을 트리거로 잘 잡아야 하며, 아날로그·타이밍 원인은 직접 보이지 않는다.

</details>

<details>
<summary>Q3. 같은 테스트를 pre/post 에서 재사용하려면 테스트 코드를 어떻게 설계하나?</summary>

레지스터 접근을 한 층(`reg_read32/reg_write32`, 상태 반환)으로 모으고, 그 아래를 백엔드(모델, DPI/transactor, JTAG, MMIO)로 바꾼다. 테스트는 레지스터 기술서(SystemRDL/IP-XACT 에서 생성, SIV-B01)에서 기대값을 얻고, 플랫폼 차이(FPGA 빌드 ID, 없는 PLL, 낮은 클럭)는 테스트 본문이 아니라 **플랫폼 오버라이드 표** 로 둔다 (추가 연습 4). 시간은 실시간이 아니라 사이클/주입된 시계로 재고(SIV-B09), 결과 로그는 오프셋·기대값·실제값·비트 형식으로 통일해 두 단계 결과를 기계적으로 비교한다. 꼬리질문 "UVM 에선 어떻게 하나?" → `uvm_reg` 모델에 `uvm_reg_hw_reset_seq`, `uvm_reg_bit_bash_seq`, `uvm_reg_access_seq`, `uvm_mem_walk_seq` 같은 내장 시퀀스가 있고, 어댑터가 버스 에이전트와 연결한다. 그 시퀀스들의 C 버전이 SIV-B03~B07 이다.

</details>

<details>
<summary>Q4. JTAG 링크가 중간에 끊기면 테스트 결과는 어떻게 남겨야 하나? 직접 MMIO 에서는?</summary>

비교 결과 대신 **INFRA** 로 남기고, 마지막 성공 명령·실패한 명령·오류 종류(TIMEOUT/SLVERR/PROTO)·경과 시간을 기록한다. 끊긴 뒤 읽은 값으로 FAIL 을 만들면 안 된다. 직접 MMIO 에서는 오류가 예외(Cortex-M BusFault, AArch64 SError/동기 외부 abort)로 오므로, 핸들러가 주소와 상태를 기록하고 sticky 플래그를 세워 테스트 프레임워크가 INFRA 로 바꾼다. 꼬리질문 "버스가 아예 멈추면(hang)?" → 소프트웨어가 못 빠져나오므로 워치독 리셋, 디버거에서 AP 상태 확인, 또는 인터커넥트의 타임아웃 기능을 켠다. 꺼진 클럭/전원 도메인 접근이 대표 원인이라 bring-up 순서(SIV-B10)부터 확인한다.

</details>

<details>
<summary>Q5. 에뮬레이터가 시뮬레이터보다 수천 배 빠른데 왜 시뮬레이터를 계속 쓰나?</summary>

① 4-state: 에뮬레이터·FPGA 는 2-state 라 X 전파(리셋 안 된 플롭, 초기화 안 된 메모리)를 못 본다. ② 가시성과 디버그 회전: 시뮬레이터는 아무 신호나 언제든 보고 바로 재실행한다. ③ 컴파일 비용: 에뮬레이터 컴파일은 수 시간, 블록 단위 시뮬은 수 분. ④ 제약 랜덤 + 커버리지 + 어서션 생태계. 그래서 블록·서브시스템 검증은 시뮬, 풀칩 장시간 시나리오는 에뮬, SW 개발은 FPGA 로 나눈다 (EMU-B01, EMU-B02). 꼬리질문 "Verilator 는?" → 사이클 기반 2-state 라 빠르지만 X 는 못 본다.

</details>

<details>
<summary>Q6. 실리콘에서 리셋값 하나가 틀렸다. RTL 시뮬에선 PASS 였다. 원인 후보와 확인 순서는?</summary>

후보: 넷리스트 단계의 tie-off/상수 전파 오류, ECO 결과, 스캔 삽입 후 리셋 경로, 퓨즈/OTP 로 로드되는 기본값, 리셋 해제 전 읽기(리셋 시퀀스 문제), 다른 블록이 부팅 중 이미 써 놓음(ROM 코드), 또는 단순히 **모델/기대값이 실리콘과 다른 버전**. 순서: ① 여러 유닛에서 재현(전부면 설계) → ② ROM/부트 코드가 그 레지스터를 건드리는지 확인(가장 흔하고 싸다) → ③ 게이트 수준 시뮬에 같은 리셋 테스트 → ④ 넷리스트에서 해당 플롭의 리셋 값 추적. 꼬리질문 "RC(read-clear) 레지스터의 리셋값은 어떻게 테스트하나?" → 리셋 직후 **첫 읽기** 만 의미가 있고 두 번째 읽기는 0 이다. 테스트 순서를 고정하거나, 기술서에서 해당 레지스터를 "첫 읽기만 비교" 로 표시한다 (UVM 에서는 `NO_REG_HW_RESET_TEST` 같은 속성으로 아예 제외하기도 한다).

</details>

<details>
<summary>Q7. Metal ECO 와 full respin 의 차이, 그리고 스테핑 이름(A0, A1, B0)과의 관계는?</summary>

Metal ECO 는 미리 뿌려 둔 spare 셀을 금속층 배선만 바꿔 연결하는 수정이라 금속층 마스크만 새로 만든다 — 빠르고 싸지만 고칠 수 있는 범위가 작다. Full(base-layer) respin 은 트랜지스터 층부터 다시 만든다. 흔한 관례로 숫자가 바뀌면(A0→A1) metal 수정, 문자가 바뀌면(A→B) base-layer 수정이다 (회사마다 다를 수 있다). 꼬리질문 "ECO 대신 무엇을 하나?" → FW/드라이버 우회 + errata 문서화, 기능 비활성화(chicken bit), 퓨즈로 경로 선택, 스크린 추가로 해당 유닛 배제.

</details>

## 9. 추가 연습

### 연습 1 — 읽기 재시도 decorator

`bus_t` 를 감싸는 `bus_t` 를 만들어 TIMEOUT 난 **읽기만** `max_retry` 번까지 다시 시도하라. 5번째 읽기마다 응답이 유실되는 transport(`flaky_xfer`: 타깃은 읽기를 실행했지만 응답이 사라짐)에서, 재시도 없이는 INFRA, 재시도 있으면 4개 테스트가 PASS 여야 한다. 쓰기는 왜 재시도하지 않나?

<details>
<summary>답 보기</summary>

```c
bus_status_t s = r->inner->read32(r->inner->ctx, addr, val);
for (int k = 0; s == BUS_ERR_TIMEOUT && k < r->max_retry; k++) { r->retries++; s = r->inner->read32(...); }
```

실제 출력: `ex1: retries=29 over 148 mdw commands`. 쓰기는 명령이 타깃에 반영됐는지 알 수 없다 — W1S/W1C, FIFO push, doorbell 은 두 번 먹으면 결과가 다르다. 읽기도 사실 완전히 안전하진 않다: 응답만 유실된 경우 타깃에선 읽기가 **이미 실행됐다**. 이 레슨의 STATUS.BUSY 는 읽을 때마다 바뀌는 volatile 비트라 재시도로 값이 달라지지만 비교에서 제외되니 괜찮다. RC(read-clear) 레지스터라면 첫 읽기에서 이벤트가 지워져 재시도 값은 0 이 된다 → RC 레지스터는 재시도 금지 목록에 올린다. decorator 로 만든 이유: mock·JTAG·MMIO 어디에든 같은 정책을 씌울 수 있다.

</details>

### 연습 2 — shift-left 격차

`escape_levels(b, found)` = 실제 발견 단계 − 가장 이른 가능 단계. `found` 가 물리적으로 불가능한 단계면 -1 (분류 데이터 오류). 리셋값 오류(#1)가 실리콘에서 발견되면? 아비터 starvation(#5)이 실리콘에서 발견되면?

<details>
<summary>답 보기</summary>

#1 은 3 (SIM→SILICON), #5 는 2 (EMU→SILICON), PLL 저온(#12)이 실리콘에서 발견되면 0 — 그게 최선이다. PLL 을 "SIM 에서 발견" 했다는 기록은 -1 → 아날로그 behavioral 모델의 결함을 RTL 버그로 잘못 분류했을 가능성이 크다. 격차 합계를 버그 트래커에서 매 tapeout 집계하면 pre-silicon 계획의 구멍(어떤 종류의 버그가 계속 늦게 잡히나)이 숫자로 보인다. 흔히 말하는 "단계마다 비용 약 10배" 는 정확한 법칙이 아니라 경험적 어림이다.

</details>

### 연습 3 — SILICON_ONLY 를 여러 유닛에서

본문 캠페인을 유닛 3개에 돌려 SILICON_ONLY 실패가 **설계 문제인지(전 유닛) 유닛 결함인지(일부)** 를 판정하는 `unit_verdict(res, n)` 를 작성하라. INFRA 유닛은 판단에서 뺀다.

<details>
<summary>답 보기</summary>

유효 유닛 중 FAIL 이 0개 → `NOT_REPRODUCED`, 전부 → `DESIGN`, 일부 → `UNIT_SPECIFIC`. 실행 결과: stuck-at 을 유닛 1 에만 넣으면 `rw_pattern` 이 `[PASS, FAIL, PASS]` → UNIT_SPECIFIC, CTRL tie-off 를 3개 모두에 넣으면 → DESIGN. 유닛 특이 결함은 **ATE 스크린이 놓친 제조 결함** 이라는 뜻이니 제품·테스트 엔지니어링에 넘기고(SIV-M01, SIV-A08), 설계 문제는 errata/ECO 트랙으로 간다. 실제로는 3개로는 부족하다 — 공정 코너(TT/FF/SS) 웨이퍼에서 고른 유닛을 섞어야 "전부 실패" 의 의미가 생긴다.

</details>

### 연습 4 — 플랫폼별 기대값

FPGA 빌드의 ID 레지스터는 하위 바이트에 빌드 태그가 있어 `0x51530AF1` 이다. 테스트 본문을 고치지 않고 플랫폼 오버라이드 표로 해결하라. 실리콘에 FPGA 기대값을 쓰면 어떻게 되어야 하나?

<details>
<summary>답 보기</summary>

`expected_reset(platform, reg)` 가 오버라이드 표를 먼저 보고 없으면 기술서 리셋값을 돌려준다. FPGA 칩 + FPGA 플랫폼 → PASS, FPGA 칩 + 실리콘 플랫폼 → `reg=ID off=0x000 exp=0x51530A01 act=0x51530AF1 diff=0x000000F0 bits=[4,5,6,7]`, 실리콘 칩 + FPGA 플랫폼 → FAIL. 마지막이 FAIL 이어야 "잘못된 플랫폼 설정으로 돌린 회귀" 를 잡는다. 오버라이드는 리뷰 대상인 데이터로 두고, 테스트 코드 안 `#ifdef FPGA` 로 흩뿌리지 않는다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 15/15 passed
```

## 10. 핵심 정리

- 단계: SIM(Hz~kHz, 모든 신호, 4-state) → EMU(~MHz, 거의 전부) → FPGA(수~수십 MHz, 고른 프로브) → 실리콘(GHz, 레지스터·스캔·trace) → 양산(pass/fail). 속도 ↑, 가시성 ↓, 수정 비용 ↑.
- 버그의 최초 발견 단계 = 필요한 현상(논리·X·실IO·타이밍·아날로그·PVT·대량)을 모두 갖추고 사이클 예산 안에 드는 첫 단계. 실리콘 첫 발견은 대부분 비논리 버그지만, 깊은 논리 버그도 사이클 때문에 온다.
- 2-state(EMU/FPGA)는 X 를 못 본다 — 빠른 플랫폼이 느린 플랫폼을 대체하지 않는다.
- 재사용 경계: 테스트 → `reg_read32/reg_write32`(상태 반환) → `bus_t` 백엔드(mock / DPI·transactor / JTAG / MMIO).
- 버스 실패는 INFRA. "실패 기록 0개 = PASS" 는 죽은 버스를 초록불로 만든다.
- 같은 테스트를 pre/post 에 돌려야 triage 가 된다: PASS/FAIL → SILICON_ONLY, FAIL/PASS → MODEL_MISMATCH, 같은 sig FAIL/FAIL → RTL_BUG(+pre 커버리지 구멍), 다른 sig → DIVERGENT.
- SILICON_ONLY 다음은 여러 유닛 재현: 전부 → 설계(errata/ECO), 일부 → 제조 결함(스크린).

## 참고자료

- P. Mishra, F. Farahmandi (eds.), *Post-Silicon Validation and Debug*, Springer 2019 — 가시성, trace 신호 선택, 스캔 기반 디버그
- [Accellera SystemRDL 2.0](https://www.accellera.org/downloads/standards/systemrdl), IEEE 1685 IP-XACT — 레지스터 기술서 단일 소스 (SIV-B01)
- IEEE 1800.2 UVM — `uvm_reg`, `uvm_reg_adapter`, 내장 시퀀스 `uvm_reg_hw_reset_seq` / `uvm_reg_bit_bash_seq` / `uvm_reg_access_seq` / `uvm_mem_walk_seq`
- [Accellera Portable Test and Stimulus Standard (PSS)](https://www.accellera.org/downloads/standards/portable-stimulus) — 시뮬·에뮬·실리콘 간 테스트 의도 재사용
- [OpenOCD User's Guide](https://openocd.org/doc/html/index.html) — `mdw`/`mww` 메모리 명령, TCL 서버(포트 6666, `0x1a` 종결)
- Arm CoreSight Architecture Specification, Arm Debug Interface (ADIv5/ADIv6) — DAP, MEM-AP, ETM, ELA
- IEEE 1149.1 JTAG Boundary-Scan
- Verilator 문서 (2-state, cycle-based); Cadence Palladium / Siemens Veloce / Synopsys ZeBu, Synopsys HAPS / Cadence Protium 제품 자료 — 속도·가시성 비교
- Intel Pentium FDIV (1994), AMD Phenom TLB erratum (B2/B3 스테핑) — 공개 보도 자료 기반 사례
- 관련 레슨: SIV-B01 (기술서→헤더), SIV-B02 (mock MMIO), SIV-B03 (리셋값), SIV-B04 (R/W·access type), SIV-B05 (walking 1/0), SIV-B09 (timeout), SIV-B10 (bring-up), SIV-B11 (인터럽트), SIV-M01 (ATE/SLT/시스템), SIV-M07 (shmoo), SIV-M13 (커버리지), EMU-B01/B02 (플랫폼 지도), EMU-B04 (HAL 경계), EMU-M02/M03 (transactor, DPI-C), BM-B02
