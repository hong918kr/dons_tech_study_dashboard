---
id: SIV-B12
time: 75분
updated: 2026-11-08
---
# SIV-B12 테스트 플랜 작성: Feature → Test Case → Pass Criteria

## 학습 목표

- IP 스펙 1페이지를 받으면 **feature list → test case(정상/경계/에러/스트레스) → 측정 가능한 pass 기준 → 장비·펌웨어 → 우선순위(P0/P1)** 순서로 테스트 플랜 표를 5분 안에 말하고 쓸 수 있다.
- pass 기준을 세 겹으로 쓴다: **① 표준(UM10204) 한계 ② 제품 요구 ③ IP 프로그래밍 모델(설정값과 측정값의 일치)** — 그리고 허용오차(tolerance)를 측정 해상도와 결함 크기 사이에서 고른다.
- 플랜의 테스트 3개(데이터 경로·NACK 복구·SCL 타이밍)를 mock I2C 컨트롤러 위에서 C 로 구현하고, 결함 6개를 주입해 **결함마다 정확히 한 테스트가 잡는지**(fault → test 매트릭스)를 보인다.
- 실패 리포트를 디버그 엔지니어가 바로 행동할 수 있는 형태(`mode=Fm rule=REQ f=156250Hz tHIGH=5000/1100ns`)로 남기고, P0 실패를 블로킹으로 판정하는 실행기를 만든다.

## 1. 문제

"이 I2C 컨트롤러 IP 의 테스트 플랜을 써 보세요" — NVIDIA·Apple·Rivian 같은 회사의 post-silicon validation 면접 단골이고, first silicon 이 도착한 첫 주에 검증 엔지니어가 실제로 쓰는 첫 문서다. 정답이 하나인 문제가 아니라서, **구조 없이 떠오르는 대로 말하면 바로 티가 난다**.

좋은 답은 늘 같은 뼈대를 따른다. 스펙에서 기능(feature)을 뽑고, 기능마다 네 종류의 테스트를 만들고, 각 테스트에 **숫자로 판정 가능한** 기준을 붙이고, 무엇이 필요한지(장비·펌웨어)와 언제까지 돌려야 하는지(우선순위)를 적는다. 마지막으로 표의 각 줄이 코드·로그와 ID 로 연결(traceability)되어야 실패를 스펙까지 거슬러 올라갈 수 있다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">테스트 플랜의 뼈대 — 표의 한 줄 = TC ID 하나 = 코드 함수 하나 = 로그 한 줄</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="40" width="160" height="70" rx="6"/><rect x="195" y="40" width="160" height="70" rx="6"/>
    <rect x="380" y="40" width="160" height="70" rx="6"/><rect x="565" y="40" width="165" height="70" rx="6"/>
    <rect x="10" y="150" width="160" height="70" rx="6"/><rect x="195" y="150" width="160" height="70" rx="6"/>
    <rect x="380" y="150" width="160" height="70" rx="6"/><rect x="565" y="150" width="165" height="70" rx="6"/>
  </g>
  <rect x="380" y="40" width="160" height="70" rx="6" class="f-acc" opacity=".35"/>
  <rect x="565" y="40" width="165" height="70" rx="6" class="f-acc" opacity=".35"/>
  <rect x="195" y="150" width="160" height="70" rx="6" class="f-ok" opacity=".35"/>
  <g fill="currentColor">
    <text x="20" y="62" font-weight="bold">① IP 스펙 1p</text><text x="20" y="80">레지스터·모드·FIFO</text><text x="20" y="98">에러 동작</text>
    <text x="205" y="62" font-weight="bold">② Feature list</text><text x="205" y="80" font-size="12">F1 데이터 … F9 레지스터</text><text x="205" y="98">"무엇을 약속하나"</text>
    <text x="390" y="62" font-weight="bold">③ Test case</text><text x="390" y="80">정상 · 경계</text><text x="390" y="98">에러 · 스트레스</text>
    <text x="575" y="62" font-weight="bold">④ Pass 기준</text><text x="575" y="80">숫자 + 비교 연산</text><text x="575" y="98">표준 / 요구 / 설정값</text>
    <text x="575" y="172" font-weight="bold">⑤ 장비 · 펌웨어</text><text x="575" y="190">LA, 타깃, 2nd master</text><text x="575" y="208">test FW, 드라이버</text>
    <text x="390" y="172" font-weight="bold">⑥ 우선순위</text><text x="390" y="190">P0 = 블로킹</text><text x="390" y="208">P1 = 추적·특성화</text>
    <text x="205" y="172" font-weight="bold">⑦ 구현 · 실행</text><text x="205" y="190" font-size="12">TC-01 → tc01_…()</text><text x="205" y="208">mock → 실보드</text>
    <text x="20" y="172" font-weight="bold">⑧ 리포트 · 추적</text><text x="20" y="190" font-size="12">FAIL → TC → F → 스펙 절</text><text x="20" y="208">결함 → errata / ECO</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M170 75 H190 M355 75 H375 M540 75 H560"/><path d="M647 110 V145"/>
    <path d="M565 185 H545 M380 185 H360 M195 185 H175"/>
  </g>
  <g fill="currentColor">
    <path d="M190 71 L196 75 L190 79 Z"/><path d="M375 71 L381 75 L375 79 Z"/><path d="M560 71 L566 75 L560 79 Z"/>
    <path d="M643 145 L647 151 L651 145 Z"/>
    <path d="M545 181 L539 185 L545 189 Z"/><path d="M360 181 L354 185 L360 189 Z"/><path d="M175 181 L169 185 L175 189 Z"/>
  </g>
  <text x="10" y="242" fill="currentColor" font-size="12">강조: 면접관이 가장 많이 파고드는 칸 — "그 테스트가 PASS 라는 건 정확히 무슨 숫자가 나왔다는 뜻인가요?"</text>
</svg>
```

**대상 IP — XI2C (가상의 I2C 컨트롤러) 1페이지 스펙 요약.** 실제 IP(Synopsys DesignWare, Cadence 등)를 단순화한 것이다.

| 항목 | 내용 |
|---|---|
| 인터페이스 | APB 슬레이브, 32-bit 레지스터, PCLK 50 MHz (20 ns) |
| 모드 | I2C 컨트롤러(마스터), 7-bit 주소. Sm 100 kHz / Fm 400 kHz / Fm+ 1 MHz — `SCLCNT` 로 설정 |
| FIFO | TX 8-entry, RX 8-entry. 전송 1회 = `LEN` 1..8 바이트 |
| 에러 | 주소/데이터 NACK → STOP 생성, 전송 중단(abort), **TX FIFO flush**, `STATUS.NACK=1`, `DONE=1` |
| 기타 | 타깃의 clock stretching 지원, 멀티 마스터 arbitration(`STATUS.ARBL`), 50 ns 스파이크 필터, DONE/NACK 인터럽트, bus clear(SCL 9 펄스) |

| 오프셋 | 이름 | 비트 (access) |
|---|---|---|
| 0x00 | CTRL | [0] EN (RW) |
| 0x04 | STATUS | [0] BUSY (RO), [1] DONE (W1C), [2] NACK (W1C), [3] ARBL (W1C, mock 미구현) |
| 0x08 | TAR | [6:0] 타깃 주소 (RW) |
| 0x0C | CMD | [7:0] LEN, [8] READ (WO) — **쓰는 순간 전송 시작** |
| 0x10 / 0x14 | TXDATA / RXDATA | TX push (WO) / RX pop (RO, 읽기 부작용) |
| 0x18 | FIFOLVL | [3:0] TXLVL, [11:8] RXLVL (RO) |
| 0x1C | SCLCNT | [15:0] LCNT, [31:16] HCNT (RW). tLOW = (LCNT+1)·tPCLK, tHIGH = (HCNT+1)·tPCLK. 리셋값 = Sm 설정 `0x00F900F9` |
| 0x20 | ID | `0x12C00102` (RO) |

검증 보드에는 0x50 에 24C02 류 EEPROM 타깃이 있고 0x51 에는 아무것도 없다. 로직 분석기(LA)가 SCL 을 잡는다. mock 은 SIV-B02 의 bus ops 를 축약해 넣었고(`reg_read32/reg_write32` → 함수 포인터), 결함은 6비트 마스크로 주입한다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">테스트 벤치 — 코드에서 바뀌는 건 bus ops 와 la_capture 두 곳뿐</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="40" width="150" height="120" rx="6"/>
    <rect x="200" y="40" width="170" height="120" rx="6"/>
    <rect x="470" y="40" width="120" height="50" rx="6"/>
    <rect x="610" y="40" width="120" height="50" rx="6" stroke-dasharray="5 4"/>
    <rect x="530" y="200" width="200" height="50" rx="6"/>
  </g>
  <rect x="200" y="180" width="240" height="70" rx="6" class="f-warn" opacity=".45"/>
  <rect x="530" y="200" width="200" height="50" rx="6" class="f-mut" opacity=".45"/>
  <g fill="currentColor">
    <text x="20" y="62" font-weight="bold">테스트 FW (CPU)</text>
    <text x="20" y="82" font-size="12">PLAN[] → run_plan()</text><text x="20" y="100" font-size="12">tc01 / tc02 / tc03</text>
    <text x="20" y="118" font-size="12">xi2c_xfer() 드라이버</text><text x="20" y="136" font-size="12">reg_read32/write32</text>
    <text x="210" y="62" font-weight="bold">XI2C (APB)</text>
    <text x="210" y="82" font-family="monospace" font-size="12">CTRL STATUS TAR CMD</text>
    <text x="210" y="100" font-family="monospace" font-size="12">TX/RX FIFO ×8</text>
    <text x="210" y="118" font-family="monospace" font-size="12">SCLCNT → SCL 생성기</text>
    <text x="210" y="136" font-family="monospace" font-size="12">NACK → abort+flush</text>
    <text x="480" y="62" font-weight="bold">24C02</text><text x="480" y="80">@0x50 (ACK)</text>
    <text x="620" y="62" font-weight="bold">빈 주소</text><text x="620" y="80">@0x51 (NACK)</text>
    <text x="210" y="202" font-weight="bold">fault mask (주입)</text>
    <text x="210" y="220" font-size="12">RXFIFO_FULL_DROP, NACK_IGN,</text>
    <text x="210" y="238" font-size="12">HANG, TX_NOT_FLUSHED, HCNT, LCNT</text>
    <text x="540" y="222" font-weight="bold">로직 분석기 100 MS/s</text>
    <text x="540" y="240" font-size="12">la_capture() = SCL 에지 export</text>
    <text x="400" y="122">SCL</text><text x="400" y="152">SDA</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M160 100 H195"/><path d="M370 128 H730 M370 158 H730"/>
    <path d="M530 90 V128 M550 90 V158 M670 90 V128 M690 90 V158"/>
    <path d="M285 160 V180"/><path d="M600 128 V200"/>
  </g>
  <g fill="currentColor">
    <path d="M195 96 L201 100 L195 104 Z"/><circle cx="530" cy="128" r="3"/><circle cx="550" cy="158" r="3"/>
    <circle cx="670" cy="128" r="3"/><circle cx="690" cy="158" r="3"/><circle cx="600" cy="128" r="3"/>
  </g>
  <text x="10" y="190" fill="currentColor" font-size="12">mock: bus ops = 함수 포인터</text>
  <text x="10" y="208" fill="currentColor" font-size="12">실보드: *(volatile uint32_t *)</text>
  <text x="10" y="226" fill="currentColor" font-size="12">(BASE + off), JTAG 로 로드</text>
</svg>
```

### Part A — 드라이버 한 개와 TC-01 (정상 + FIFO 경계)

- `xi2c_xfer(addr, rd, buf, len)` — 전송 하나. `len` 이 1..8 이 아니면 **버스 접근 없이** `XFER_BADARG`. 이전 W1C 상태 정리 → TAR → (write) TX FIFO 채우기 → CMD → STATUS 를 **최대 64번** 폴링, 완료 조건은 `BUSY=0 && DONE=1` → 본 DONE/NACK 만 W1C ack → NACK 이면 `XFER_NACK` → (read) drain 직전 RXLVL 을 기록하고 꺼낸다.
- `tc01_data_path()` — 0x20, 0x27 에 7바이트씩 write(오프셋 1 + 데이터 7 = TX FIFO 가득), 그다음 길이 **1, 7, 8**(RX FIFO 가득) read-back 을 `pat(i)` 와 비교하고, 매번 `STATUS==0 && FIFOLVL==0` 확인.

### Part B — 에러 경로와 타이밍 (TC-02, TC-03)

- `tc02_nack_recovery()` — 0x51 에 2바이트 write → 반드시 NACK. ack 뒤 `STATUS==0`, `TXLVL==0`. 그다음 0x50 에 정상 write/read 로 복구 확인. 타임아웃이면 마지막 STATUS 를 메시지에.
- `scl_measure(edges, n, &m)` — LA 에지 배열에서 tLOW/tHIGH 의 min·max 와 fSCL. 레벨이 번갈아 나오지 않으면(글리치·누락) false.
- `tc03_scl_timing()` — Sm/Fm/Fm+ 모두 측정해 **rule=UM10204 → rule=REQ(≥ 90 % nominal) → rule=PROG(|측정−설정| ≤ 10 ns)** 순서로 판정.

### Part C — 플랜 실행기와 결함 캠페인

- `run_plan(plan, n, faults, &r)` — 플랜의 12개 TC 중 함수가 있는 것만 실행(나머지 `NOT_RUN`), **테스트마다 DUT 리셋**(격리), 결과·메시지·`fail_mask`·`blocking`(P0 FAIL) 기록.
- 하네스가 결함 7가지(없음 + 6)를 돌려 **결함마다 정확히 한 TC 가 FAIL** 하는지, 메시지에 올바른 레지스터·비트·모드가 찍히는지 확인하고 매트릭스를 출력한다.

## 2. Problem (English)

> **Write a test plan for an I2C controller IP and implement three of its test cases.**
>
> You are given a one-page spec of `XI2C` (APB, 7-bit addressing, Sm/Fm/Fm+ via `SCLCNT.LCNT/HCNT`, 8-entry TX/RX FIFOs, NACK aborts the transfer and flushes the TX FIFO). Produce a test plan table: features, test cases (normal / boundary / error / stress), equipment and firmware, measurable pass/fail criteria, and priority (P0/P1). Then, against a mock register model with fault injection:
>
> 1. Implement `xfer_t xi2c_xfer(uint8_t addr, bool rd, uint8_t *buf, unsigned len)` with a bounded poll (64 reads). Completion is `BUSY==0 && DONE==1`.
> 2. Implement `TC-01` (write 2×7 bytes, read back lengths 1, 7, 8), `TC-02` (address NACK on `0x51`: NACK reported, TX FIFO flushed, controller recovers), and `TC-03` (measure `tLOW`, `tHIGH`, `fSCL` from a logic-analyzer capture for each speed mode; check UM10204 limits, ≥ 90 % of nominal frequency, and `|measured − programmed| ≤ 10 ns`).
> 3. Implement `run_plan()` that runs only implemented tests, resets the DUT before each, and marks the run as blocking if any P0 test fails.
>
> Each injected defect must be caught by exactly one test, with a failure message such as `TC-01 rd len=8 idx=7 exp=0x5D act=0x00 (RXLVL=7)` or `TC-03 mode=Fm rule=REQ f=156250Hz tLOW=1400/1400ns tHIGH=5000/1100ns (meas/prog)`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

실제 실행 결과(앞부분과 끝):

```text
FAIL starter.c:287  xi2c_xfer(EEP_ADDR, false, w, 2) == XFER_OK
FAIL starter.c:288  D.eep[0x10] == 0xAB && D.txn == 0
FAIL starter.c:290  xi2c_xfer(EEP_ADDR, false, &p, 1) == XFER_OK && xi2c_xfer(EEP_ADDR, true, &b, 1) == XFER_OK
...
fault             TC-01 TC-02 TC-03  verdict
none              -     -     -      ok
RXFIFO_FULL_DROP  -     -     -      ok
...
plan: 12 tests, 0 run, 0 not run

17/59 passed
```

TODO 는 6개(A1 드라이버, A2 TC-01, B1 TC-02, B2 측정, B3 TC-03, C1 실행기)다. mock(`mock_rd/mock_wr/mock_start/la_record`)은 "실리콘"이므로 고치지 않는다. 결함 주입 테스트(`!run_one(..., F_xxx)`)는 TODO 스텁이 `false` 를 돌려도 통과하므로, 진짜 확인은 그 뒤의 `WHY_HAS(...)` — **올바른 이유로 실패했는가** — 다.

## 4. 힌트

<details>
<summary>힌트 1 — 완료는 BUSY=0 이 아니라 DONE=1</summary>

CTRL.EN=0 이면 CMD 쓰기가 무시되고 BUSY 는 한 번도 서지 않는다. "BUSY 가 0 이 될 때까지" 폴링하면 첫 읽기에서 바로 성공이다 — 일어나지도 않은 전송을. 실칩에서도 CMD 쓰기가 I2C 코어 클럭 도메인으로 넘어가는 동안(CDC) 몇 사이클은 BUSY=0 이 보일 수 있다. sticky 한 DONE 을 완료 조건으로 쓰고, 폴링 횟수에 상한을 둔다 (SIV-B09).

</details>

<details>
<summary>힌트 2 — 경계값은 "최소, 최대−1, 최대"</summary>

RX FIFO 깊이 8 → 읽기 길이 1, 7, 8. FIFO 의 full 플래그·레벨 카운터 off-by-one 은 정확히 8 에서만 드러난다. write 는 오프셋 바이트가 FIFO 한 칸을 먹으므로 데이터 7 + 오프셋 1 = 8 이 TX FIFO 의 경계다.

</details>

<details>
<summary>힌트 3 — 에러 테스트의 절반은 "그 다음"</summary>

NACK 을 보고했는지만 보면 반쪽이다. ① BUSY 가 내려갔나(버스 해제) ② W1C 로 지워지나 ③ TX FIFO 에 찌꺼기가 없나 ④ **다음 정상 전송이 되나**. ③을 빼먹으면 abort 된 바이트가 다음 타깃에 쓰인다.

</details>

<details>
<summary>힌트 4 — 측정과 판정을 분리하라</summary>

`scl_measure` 는 숫자만 만든다(min/max/f). 판정은 TC-03 이 표(UM10204 최소값, 제품 요구, 설정값)와 비교한다. fSCL = (하강 에지 수 − 1) × 10⁹ / (마지막 하강 − 첫 하강). `uint64_t` 로 곱해야 넘치지 않는다.

</details>

## 5. 풀이

### 플랜 — 면접에서 말하는 순서

1. **범위 확인(30초)**: "컨트롤러만인가, 타깃 모드도? 10-bit 주소? 제품이 쓰는 속도 모드는? 멀티 마스터를 쓰나?" — 이 답이 우선순위를 정한다.
2. **Feature list**: 스펙의 약속 하나 = feature 하나.
3. **Feature 마다 4종 테스트**: 정상(대표 사용), 경계(FIFO 깊이·최대 속도·최소 길이), 에러(NACK·arbitration·stuck bus), 스트레스(랜덤·장시간·PVT 코너).
4. **Pass 기준**: 숫자와 비교 연산이 있는 문장. 출처(표준/요구/설정값)를 적는다.
5. **장비·펌웨어**, 6. **우선순위**, 7. **추적성**: TC ID 가 코드 함수·로그·버그 티켓에 그대로 쓰인다.

| Feature | 내용 (스펙의 약속) |
|---|---|
| F1 데이터 전송 | 7-bit 주소, write/read, LEN 1..8, 바이트 순서 보존 |
| F2 FIFO | TX/RX 8-entry, FIFOLVL 정확, full/empty 경계에서 손실 없음 |
| F3 NACK 처리 | 주소/데이터 NACK → STOP, abort, TX flush, STATUS.NACK, 복구 가능 |
| F4 SCL 타이밍 | Sm/Fm/Fm+ 에서 UM10204 만족, LCNT/HCNT 대로 동작, 50 ns 스파이크 필터 |
| F5 Clock stretching | 타깃이 SCL 을 잡고 있는 동안 대기, 데이터 무손상 |
| F6 Arbitration | 다른 마스터에 지면 ARBL, 버스를 놓고 멈추지 않음 |
| F7 인터럽트 | DONE/NACK → IRQ, IE 마스크, W1C 클리어 (SIV-B11) |
| F8 Bus clear | SDA 가 low 로 붙으면 SCL 9 펄스로 해제 (UM10204 "Bus clear") |
| F9 레지스터 | 리셋값·access type·ID (SIV-B03/B04) |

**테스트 플랜 표** (코드의 `PLAN[]` 과 같은 ID — ✔ 는 이 레슨에서 구현):

| ID | F | 분류 | 자극 (stimulus) | Pass 기준 | 장비·FW | P | 구현 |
|---|---|---|---|---|---|---|---|
| TC-01 | F1·F2 | 정상+경계 | 7B write ×2, read len 1/7/8 | read == written (전 바이트), 이후 STATUS=0, FIFOLVL=0 | 24C02, test FW | P0 | ✔ |
| TC-02 | F3 | 에러 | 빈 주소 0x51 에 2B write, 이어 0x50 정상 전송 | ≤64 폴링 안에 NACK+DONE, BUSY=0; ack 뒤 STATUS=0; TXLVL=0; 복구 read == 0xC3 | 24C02, test FW | P0 | ✔ |
| TC-03 | F4 | 경계 | Sm/Fm/Fm+ 각각 1B write, SCL 캡처 | tLOW/tHIGH ≥ UM10204 최소, f ≤ fmax; f ≥ 90% nominal; \|측정−설정\| ≤ 10 ns | LA ≥100 MS/s | P0 | ✔ |
| TC-04 | F3 | 에러 | 타깃이 k번째 데이터 바이트에 NACK | abort at k, NACK=1, TXLVL=0, k 이후 바이트 미기록 | 타깃 에뮬레이터 | P0 | |
| TC-05 | F5 | 정상 | 타깃이 각 바이트 뒤 SCL 을 1 ms 잡음 | 데이터 무손상, 타임아웃 없음, DONE | 타깃 에뮬레이터 | P0 | |
| TC-06 | F7 | 정상 | DONE·NACK 유발, IE on/off | IE=1: IRQ 1회, IE=0: IRQ 0회, W1C 뒤 IRQ 해제 | test FW + ISR | P0 | |
| TC-07 | F9 | 정상 | 리셋 직후 전 레지스터 읽기, bit-bash | 리셋값·access type 표와 비트 단위 일치 | test FW | P0 | |
| TC-08 | F6 | 에러 | 2nd master 가 같은 순간 START, 낮은 주소 | 진 쪽 ARBL=1, BUSY=0, 버스 hang 없음 | 2nd master (호스트 어댑터/FPGA) | P1 | |
| TC-09 | F8 | 에러 | 타깃이 SDA 를 low 로 잡은 채 리셋 | bus clear 후 SDA=1, 다음 전송 OK | FPGA 타깃 | P1 | |
| TC-10 | F4 | 경계 | SCL 에 40 ns / 100 ns 글리치 주입 | 40 ns: 무시(데이터 무손상), 100 ns: 결과 기록(특성화) | FPGA·펄스 발생기, 스코프 | P1 | |
| TC-11 | F1 | 스트레스 | 시드 고정 랜덤 1e6 전송 (len·offset·r/w) | 스코어보드 불일치 0, 타임아웃 0, 시드 로그 | test FW | P1 | |
| TC-12 | F4 | 스트레스 | TC-01/03 을 Vmin/Vmax × −40/25/125 °C | 모든 코너에서 TC-01/03 기준 그대로 PASS | 전원, thermal forcer | P1 | |

**장비·펌웨어**: 검증 보드(풀업 저항 교체 가능 — 상승 시간 tr ≤ 1000/300/120 ns, Sm/Fm/Fm+), 0x50 타깃(실제 24C02 는 내부 write cycle 동안 약 5 ms NACK 하므로 **NACK·stretching 을 마음대로 만드는 FPGA/호스트 어댑터 타깃이 더 낫다**), LA + I2C 디코더(타이밍), 오실로스코프(아날로그 레벨·상승 시간), 프로그래머블 전원·thermal forcer(PVT). 펌웨어는 bare-metal 테스트 이미지 — bus ops 계층, `xi2c_xfer`, 테스트 레지스트리(SIV-B08), JSON 로그(SIV-B14) — 를 JTAG/OpenOCD 로 올려 돌린다.

**Pass 기준 쓰는 법** — 판정이 사람마다 달라지면 기준이 아니다:

| 나쁜 기준 | 좋은 기준 |
|---|---|
| "I2C 가 잘 동작한다" | "len 1/7/8 read-back 이 쓴 값과 일치, 이후 STATUS=0x0, FIFOLVL=0x0" |
| "NACK 를 처리한다" | "0x51 에 대해 64 폴링 안에 STATUS=NACK\|DONE, BUSY=0; TXLVL=0; 다음 전송 OK" |
| "400 kHz 로 동작" | "f ≤ 400 kHz 이고 ≥ 360 kHz; tLOW ≥ 1.3 µs; tHIGH ≥ 0.6 µs; \|측정−설정\| ≤ 10 ns" |
| "스트레스에서 안정" | "시드 고정 1e6 랜덤 전송, 스코어보드 불일치 0, 타임아웃 0" |

**우선순위** (회사마다 이름은 달라도 구조는 비슷하다): **P0** = first silicon 첫 주에 돌아야 하고, 하나라도 FAIL 이면 블로킹 — 원인 분석, errata·소프트웨어 workaround 또는 ECO(metal fix) 결정 전엔 다음 단계로 못 간다. 제품이 실제로 쓰는 기능·모드, 기본 에러 경로, 레지스터. **P1** = 특성화(characterization)·코너·스트레스·제품이 안 쓰는 기능(이 보드가 멀티 마스터를 안 쓰면 TC-08 은 P1) — 실패는 추적(WARN)하되 즉시 블로킹은 아니다. 기준은 "제품이 쓰나 / 실패하면 데이터가 깨지나 성능만 떨어지나 / workaround 가 있나".

### Part A — 드라이버와 TC-01

```c
reg_write32(XI2C_STATUS, ST_DONE | ST_NACK);          /* 이전 전송의 sticky 비트 정리 (W1C) */
...
reg_write32(XI2C_CMD, len | (rd ? CMD_READ : 0u));
for (n = 0; n < POLL_MAX; n++) {
    g_last_st = reg_read32(XI2C_STATUS);
    if (!(g_last_st & ST_BUSY) && (g_last_st & ST_DONE)) break;
}
if (n == POLL_MAX) return XFER_TIMEOUT;               /* 마지막 STATUS 는 g_last_st 에 */
reg_write32(XI2C_STATUS, g_last_st & (ST_DONE | ST_NACK));   /* 본 것만 ack (C-M02) */
```

세 가지가 다 검증 이유가 있다. **완료 조건 DONE**: 하네스는 EN=0 으로 CMD 를 무시시키고 `XFER_TIMEOUT`, `g_last_st == 0` 을 요구한다 — `!BUSY` 만 보는 드라이버는 여기서 `XFER_OK` 를 돌려 "버스에 아무것도 안 나갔는데 PASS" 를 만든다. 테스트 펌웨어가 거짓 PASS 를 내면 플랜 전체가 무의미하다. **폴링 상한**: 실리콘이 멈추면 테스트도 멈추면 안 된다(TC-02 의 HANG 결함). **잘못된 인자는 버스 접근 0**: 하네스는 `D.nacc` 가 그대로인지 본다 — 반쯤 채운 TX FIFO 를 남기면 다음 테스트가 오염된다.

TC-01 의 설계는 **경계값 분석(boundary value analysis)** 그대로다. 길이 1(최소), 7(최대−1), 8(최대 = RX FIFO full). 주입한 `F_RXFIFO_FULL_DROP` 은 레벨 카운터가 full 직전에서 한 칸 잘못 비교하는 흔한 RTL 버그 유형이고, 길이 1·7 은 통과하고 8 에서만 잡힌다:

```text
FAIL TC-01 rd len=8 idx=7 exp=0x5D act=0x00 (RXLVL=7)
```

이 한 줄로 디버그 엔지니어는 "8번째 바이트, FIFO 레벨이 7 — 데이터 경로가 아니라 FIFO full 처리" 까지 바로 좁힌다. `RXLVL` 을 drain **전에** 기록한 이유다. 매 read 뒤 `STATUS==0 && FIFOLVL==0` 은 "LEN 보다 한 바이트 더 읽어 온다" 같은 결함을 잡는다. 실보드에서는 LA 디코드로 **마지막 읽기 바이트에 마스터가 NACK 를 보냈는지** 도 기준에 넣는다 — 마지막 바이트를 ACK 하면 타깃이 다음 바이트를 내보내려고 SDA 를 잡아 버스가 멈출 수 있다.

반대로 하네스는 `F_HCNT_STUCK` 에서 TC-01 이 **통과** 함을 확인한다. 데이터 경로 테스트는 타이밍 결함을 못 본다 — 그래서 플랜에 feature 별 테스트가 따로 있어야 한다.

### Part B — 에러 경로(TC-02)와 타이밍(TC-03)

**TC-02** 는 결함 세 개를 각자 다른 체크로 잡는다:

| 결함 | 실리콘에서 보이는 증상 | 잡는 체크 | 실제 메시지 |
|---|---|---|---|
| `F_NACK_IGNORED` | 없는 장치에 write 가 "성공" — 설정이 조용히 사라짐 | 반환값이 NACK 인가 | `tar=0x51 exp NACK act=OK STATUS=0x00000002 (bit2 NACK=0)` |
| `F_HANG_AFTER_NACK` | STOP 을 못 만들고 FSM 정지, 버스 hang | 폴링 상한 | `timeout after 64 polls, last STATUS=0x00000005 (BUSY stuck)` |
| `F_TX_NOT_FLUSHED` | abort 된 바이트가 다음 타깃에 쓰임 | ack 뒤 TXLVL==0 | `after NACK TXLVL=2 exp=0 (TX FIFO not flushed)` |

세 번째가 가장 위험하다. PMIC 에 보내려던 바이트가 다음 전송에서 다른 장치의 레지스터에 쓰일 수 있다. TXLVL 체크가 없더라도 복구 단계(0x30 에 0xC3 을 쓰고 읽기)가 0xEE 를 읽어 잡지만, 그 메시지("recovery exp=0xC3 act=0xEE")는 원인을 가리키지 않는다. **가장 가까운 관측점에서 먼저 확인하라** — 실패 메시지가 원인을 직접 말하게 된다.

타임아웃 메시지의 `STATUS=0x00000005` 는 BUSY=1, NACK=1 — "NACK 는 봤는데 STOP 단계에서 멈췄다" 는 뜻이다. 마지막 읽은 값이 없으면 "timeout" 한 단어로는 아무도 행동할 수 없다.

**TC-03** — SCL 생성기는 LCNT/HCNT 를 PCLK 로 센다. 측정은 LA 가 한다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">TC-03: 설정값 → SCL 파형 → 세 가지 규칙 (Fm, PCLK 20 ns)</text>
  <text x="10" y="58" fill="currentColor">정상</text>
  <path d="M85 40 H100 V70 H170 V40 H225 V70 H295 V40 H350 V70 H420 V40 H440" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="100" y="40" width="70" height="30" class="f-ok" opacity=".35"/>
  <g fill="currentColor" font-size="12">
    <text x="104" y="88">tLOW 1400</text><text x="172" y="34">tHIGH 1100</text>
    <text x="460" y="48">LCNT=69 → (69+1)×20 = 1400 ns</text>
    <text x="460" y="66">HCNT=54 → (54+1)×20 = 1100 ns → 400 kHz</text>
  </g>
  <text x="10" y="138" fill="currentColor">HCNT 미배선</text>
  <path d="M85 120 H100 V150 H170 V120 H420 V150 H440" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="170" y="120" width="250" height="30" class="f-warn" opacity=".45"/>
  <g fill="currentColor" font-size="12">
    <text x="120" y="168">1400</text><text x="215" y="114">tHIGH 5000 (리셋값 249)</text>
    <text x="460" y="128">SCLCNT 읽기 = 0x00360045 (맞음!)</text>
    <text x="460" y="146">f = 156250 Hz → rule=REQ (&lt; 360 kHz)</text>
  </g>
  <line x1="10" y1="185" x2="730" y2="185" stroke="currentColor" opacity=".35"/>
  <g fill="currentColor" font-size="12">
    <text x="10" y="206" font-weight="bold">① UM10204</text><text x="110" y="206">Sm: tLOW≥4.7 tHIGH≥4.0 µs f≤100k · Fm: ≥1.3 ≥0.6 µs f≤400k · Fm+: ≥0.5 ≥0.26 µs f≤1M</text>
    <text x="10" y="224" font-weight="bold">② REQ</text><text x="110" y="224">f ≥ 90 % nominal — 표준은 하한이 없지만 제품은 너무 느려도 실패</text>
    <text x="10" y="242" font-weight="bold">③ PROG</text><text x="110" y="242">|측정 − (CNT+1)·tPCLK| ≤ 10 ns (LA 샘플 1개) — 프로그래밍 모델대로 동작하나</text>
  </g>
</svg>
```

왜 규칙이 세 겹인가:

- **① 표준**은 "버스 위의 다른 장치가 따라올 수 있나" 다. 타깃은 UM10204 최소값을 믿고 설계된다.
- **② 제품 요구**: UM10204 의 fSCL 은 0..fmax 라 156 kHz 도 "Fm 위반" 이 아니다. 그래도 400 kHz 를 기대한 센서 폴링 루프는 대역폭이 모자란다. 표준과 제품의 기준은 다르다.
- **③ 프로그래밍 모델**은 "IP 가 레지스터 설명대로 동작하나" 다. 드라이버(Linux `i2c-designware` 처럼 HCNT/LCNT 를 공식으로 계산하는 코드)가 이 모델을 믿는다.

`F_HCNT_STUCK` 은 **레지스터는 읽으면 맞는데 SCL 생성기에 연결이 안 된** 결함이다. 하네스는 `SCLCNT` 에 `0x00360045` 를 쓰고 그대로 읽히는 것을 CHECK 한다 — 리셋값 테스트(SIV-B03)도, R/W 테스트(SIV-B04)도, UVM 의 `uvm_reg_bit_bash_seq` 도 이 결함을 **통과시킨다**. 레지스터 테스트는 "저장소" 를 보고, 기능 테스트는 "효과" 를 본다. 그리고 Sm 은 리셋값과 설정값이 같아서 통과한다 — **모든 속도 모드를 돌려야 하는 이유**다:

```text
FAIL TC-03 mode=Fm rule=REQ f=156250Hz tLOW=1400/1400ns tHIGH=5000/1100ns (meas/prog)
```

`F_LCNT_OFF_BY_ONE`(카운터 비교 `<=` 를 `<` 로 쓴 꼴)은 tLOW 가 20 ns 짧다. 권장값이 정확히 100 kHz 에 맞춰져 있어서 f 가 100200 Hz 로 fmax 를 넘고 ① 에서 잡힌다. 마진을 둔 설정에선 ①②를 통과하고 ③만 잡는다 — 추가 연습 1.

```text
FAIL TC-03 mode=Sm rule=UM10204 f=100200Hz tLOW=4980/5000ns tHIGH=5000/5000ns (meas/prog)
```

**허용오차 10 ns** 는 LA 100 MS/s 의 샘플 하나다. 측정 불확도보다 작으면 정상 칩이 FAIL 하고, 결함 크기(20 ns = PCLK 1개) 이상이면 off-by-one 을 놓친다. 실측에서는 UM10204 가 정한 전압 레벨(VDD 의 30 %/70 %) 기준으로 시간을 재고, 상승 시간 tr 이 주기에 더해진다 — 많은 컨트롤러가 SCL 이 실제로 high 로 감지된 뒤부터 HCNT 를 세기 때문이다(클럭 동기화·stretching 지원). 그래서 실보드의 ③ 기준은 "설정값 + 동기화 지연 몇 PCLK + tr" 를 IP 데이터북 공식대로 옮겨 적어야 한다. mock 은 tr = 0 으로 단순화했다.

`scl_measure` 는 레벨이 번갈아 나오지 않으면 false 를 돌려준다. LA 캡처에 같은 레벨이 두 번 나오면 글리치 필터를 통과한 스파이크거나 캡처 누락이다 — 그 데이터로 min/max 를 계산하면 거짓 PASS·FAIL 이 나온다. **측정 도구의 실패를 DUT 의 실패와 구분**하는 것도 플랜의 일이다 (`bad capture` 메시지).

### Part C — 실행기와 결함 캠페인

```c
mock_reset(faults);            /* 테스트 격리: 앞 테스트의 hang/FIFO 찌꺼기가 다음으로 새지 않게 */
g_why[0] = '\0';
if (plan[i].fn()) { ...PASS... }
r->fail_mask |= 1u << i;
if (plan[i].prio == 0) r->blocking = true;
```

실제 출력(solution):

```text
fault             TC-01 TC-02 TC-03  verdict
none              pass  pass  pass   ok
RXFIFO_FULL_DROP  FAIL  pass  pass   BLOCK
NACK_IGNORED      pass  FAIL  pass   BLOCK
HANG_AFTER_NACK   pass  FAIL  pass   BLOCK
TX_NOT_FLUSHED    pass  FAIL  pass   BLOCK
HCNT_STUCK        pass  pass  FAIL   BLOCK
LCNT_OFF_BY_ONE   pass  pass  FAIL   BLOCK
[RXFIFO_FULL_DROP] FAIL TC-01 rd len=8 idx=7 exp=0x5D act=0x00 (RXLVL=7)
[NACK_IGNORED] FAIL TC-02 tar=0x51 exp NACK act=OK STATUS=0x00000002 (bit2 NACK=0)
[HANG_AFTER_NACK] FAIL TC-02 tar=0x51 timeout after 64 polls, last STATUS=0x00000005 (BUSY stuck)
[TX_NOT_FLUSHED] FAIL TC-02 after NACK TXLVL=2 exp=0 (TX FIFO not flushed)
[HCNT_STUCK] FAIL TC-03 mode=Fm rule=REQ f=156250Hz tLOW=1400/1400ns tHIGH=5000/1100ns (meas/prog)
[LCNT_OFF_BY_ONE] FAIL TC-03 mode=Sm rule=UM10204 f=100200Hz tLOW=4980/5000ns tHIGH=5000/5000ns (meas/prog)
plan: 12 tests, 3 run, 9 not run
```

이 매트릭스가 플랜의 품질 증거다. **좋은 DUT 는 전부 PASS(거짓 FAIL 없음), 결함마다 FAIL 이 정확히 하나(거짓 PASS 없음, 진단이 겹치지 않음)**. 한 결함에 TC 세 개가 동시에 FAIL 하면 원인 범위가 넓어지고, 어떤 결함도 못 잡는 TC 는 플랜에서 빈칸이다. pre-silicon 에서는 이런 "결함을 넣어 보고 테스트가 잡는지" 를 mutation/fault injection 으로, 기능 커버리지(covergroup)로 측정한다. `HANG_AFTER_NACK` 에서 TC-03 이 PASS 인 건 테스트 격리 덕이다 — `mock_reset` 을 빼면 BUSY 가 붙은 채로 TC-03 이 시작돼 엉뚱한 FAIL 이 생긴다(7. 실수 표).

`9 not run` 도 숨기지 않고 보고한다. 플랜 진척률(구현 3/12, P0 3/7)은 매일 스탠드업에서 나오는 숫자다. P0 가 NOT_RUN 인 채로 "전부 PASS" 라고 보고하면 sign-off 할 수 없다 (추가 연습 2 의 `INCOMPLETE`).

**현장과 연결.** pre-silicon 에서는 같은 플랜이 vPlan(검증 계획)이 되어 UVM 시퀀스·커버리지 항목에 연결된다. 레지스터 쪽은 `uvm_reg_hw_reset_seq`·`uvm_reg_bit_bash_seq`·`uvm_reg_access_seq` 가 TC-07 에 해당한다. post-silicon 에서는 C 테스트 이미지를 JTAG/OpenOCD 나 부트 ROM 으로 올리고, 같은 TC ID 로 로그를 남긴다. 양산 단계에선 ATE(테스터 위 구조적·파라메트릭 테스트), SLT(실제 시스템 부팅 수준), 시스템 검증으로 나뉘는데, 이 플랜의 TC-12(PVT)는 특성화 결과가 ATE 테스트 한계를 정하는 데 쓰인다. pre/post 의 차이와 재사용 경로는 SIV-B13 참고.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution    # 매트릭스 출력 후 59/59 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san   # 59/59, 진단 0
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드·기준 |
|---|---|---|
| 완료 = `while (STATUS & BUSY);` | 시작 안 된 전송(EN=0, CDC 지연)도 즉시 "완료". 상한도 없어 hang 하면 테스트가 영원히 멈춤 | `BUSY==0 && DONE==1`, `n < POLL_MAX` |
| 타임아웃 메시지 = `"timeout"` | 어디서, 무엇을 기다리다, 마지막에 뭘 봤는지 없음 | `tar=0x51 timeout after 64 polls, last STATUS=0x00000005` |
| pass 기준 "동작 확인" | 사람마다 판정이 다름, 자동화 불가 | 숫자 + 비교: `f ≥ 360 kHz`, `TXLVL=0` |
| 경계 테스트 = 길이 4 하나 | full/empty off-by-one 은 정확히 깊이에서만 보인다 | 1, 깊이−1, 깊이 |
| 에러 테스트가 NACK 비트만 확인 | flush 누락·hang·복구 불가를 못 봄 | BUSY 해제, W1C, TXLVL=0, 다음 전송 OK |
| 타이밍을 Sm 에서만 확인 | 리셋값=Sm 설정이면 미배선 필드가 숨는다 | 모든 지원 모드 |
| 표준 한계만 기준으로 | 156 kHz Fm 도 "표준 위반 아님" → 성능 결함 놓침, off-by-one 도 마진 있으면 놓침 | ① 표준 ② 제품 요구 ③ 설정값 일치 |
| 테스트 사이 DUT 리셋 없음 | 앞 테스트의 hang/FIFO 찌꺼기가 다음 테스트 FAIL 로 번짐 — 매트릭스가 대각선이 아니게 됨 | `mock_reset()` / 실보드는 IP soft reset + 상태 확인 |
| 결함 주입 테스트가 "FAIL 했다" 만 확인 | 스텁·엉뚱한 이유의 FAIL 도 통과 | FAIL **이유**(TC, 레지스터, 비트, 모드)까지 CHECK |
| 미구현 TC 를 표에서 빼고 보고 | 진척률·커버리지가 부풀려짐 | `NOT_RUN` 으로 남기고 P0 NOT_RUN 은 sign-off 불가 |

## 8. 면접 Q&A

<details>
<summary>Q1. I2C 컨트롤러 IP 의 테스트 플랜을 써 보세요. 어떤 순서로 답하나요?</summary>

범위 질문(속도 모드, 10-bit, 멀티 마스터, 타깃 모드)부터 → feature list → feature 마다 정상/경계/에러/스트레스 → 각 테스트의 측정 가능한 pass 기준 → 장비·펌웨어 → P0/P1. 예를 하나씩 바로 든다: "데이터 경로는 길이 1/7/8 read-back, NACK 는 빈 주소로 abort·flush·복구, 타이밍은 LA 로 tLOW/tHIGH 를 UM10204 와 비교". 꼬리질문 "시간이 1주일밖에 없다면?" → P0 만, 그것도 제품이 쓰는 모드부터. 레지스터(리셋값·RW)는 자동 생성 테스트(SIV-B03/B04)로 첫날 돌린다.

</details>

<details>
<summary>Q2. 경계 테스트의 값은 어떻게 고르나요? I2C 에서 예를 들어 보세요.</summary>

스펙의 모든 한계에서 "최소, 최대−1, 최대, (가능하면) 최대+1" — FIFO 깊이(1/7/8, 그리고 9 는 BADARG/overflow), 속도(각 모드 fmax), 주소(0x00 general call, 0x78–0x7F 예약 주소), LEN(0 은 거부). off-by-one 이 RTL 의 가장 흔한 버그라 경계 바로 옆이 결함 밀도가 가장 높다. 꼬리질문 "경계+1 은 왜?" → 하드웨어가 거부하는지(overflow 비트), 조용히 잘라 먹는지 확인. 조용히 망가지는 게 최악이다.

</details>

<details>
<summary>Q3. "pass 기준을 측정 가능하게" 란 무슨 뜻인가요? 400 kHz 테스트의 기준을 말해 보세요.</summary>

누가 판정해도 같은 결과가 나오도록 숫자와 비교 연산으로 쓴다. 400 kHz 라면 ① UM10204: f ≤ 400 kHz, tLOW ≥ 1.3 µs, tHIGH ≥ 0.6 µs ② 제품: f ≥ 360 kHz ③ 설정값: |측정 − (CNT+1)·tPCLK| ≤ 허용오차. 꼬리질문 "허용오차는 어떻게?" → 측정 불확도(LA 샘플 주기, 트리거 지터)보다는 크고, 잡고 싶은 결함 크기(PCLK 1개 = 20 ns)보다는 작게. 그 사이에 값이 없으면 측정 장비를 바꿔야 한다(더 빠른 샘플링). 꼬리질문 2 "UM10204 엔 fSCL 하한이 없는데?" → 그래서 ② 가 따로 있다. 표준 적합성과 제품 요구는 다른 기준이다.

</details>

<details>
<summary>Q4. P0 와 P1 은 어떻게 나누나요? P0 테스트가 실패하면 무슨 일이 일어나나요?</summary>

제품이 그 기능을 쓰는가, 실패하면 데이터가 깨지나(vs 성능만), workaround 가 있나로 나눈다. P0 FAIL 은 블로킹 — 재현·원인 분석(설계팀과 waveform/RTL 대조) 후 ① 소프트웨어 workaround + errata 문서화 ② metal ECO(다음 스테핑) ③ 기능 비활성화 중 하나를 결정한다. 같은 결함이 P0 에서 P1 으로 내려가는 경우도 있다: workaround 가 확정되면 "workaround 적용 상태로 P0 PASS" 를 새 기준으로 삼는다. 꼬리질문 "P0 가 NOT_RUN 이면?" → 장비가 없어 못 돌렸다면 sign-off 불가(INCOMPLETE), 리스크로 명시한다.

</details>

<details>
<summary>Q5. 테스트가 timeout 났을 때 로그에 무엇을 남기나요?</summary>

TC ID, 기다린 조건(어떤 레지스터의 어떤 비트를 어떤 값으로), 상한(폴링 횟수/µs), **마지막으로 읽은 값**, 경과 시간, 그 시점의 관련 레지스터 스냅샷(STATUS, FIFOLVL, TAR, SCLCNT), 시드·보드·FW 버전(SIV-B14). 이 레슨의 `timeout after 64 polls, last STATUS=0x00000005` 는 BUSY=1·NACK=1 이라 "NACK 후 STOP 단계에서 FSM 정지" 까지 좁혀 준다. 꼬리질문 "그다음 행동은?" → 버스 상태(SCL/SDA 레벨)를 LA 로 보고, IP soft reset 으로 복구 가능한지 확인한 뒤 다음 테스트로 — hang 이 나머지 테스트를 막으면 안 된다.

</details>

<details>
<summary>Q6. 레지스터 리셋값·R/W 테스트가 다 통과했는데 기능이 틀릴 수 있나요?</summary>

있다. 레지스터 테스트는 플롭(저장소)을 보고, 기능 테스트는 그 플롭이 연결된 로직의 효과를 본다. `SCLCNT.HCNT` 가 읽고 쓰기는 정상인데 SCL 생성기가 tie-off 된 값을 쓰는 경우가 대표적이다(이 레슨의 `F_HCNT_STUCK`). 그래서 레지스터 필드마다 "효과를 보는 기능 테스트가 최소 하나" 있는지를 추적 매트릭스로 확인한다. 꼬리질문 "그걸 pre-silicon 에서 못 잡았을까?" → 기능 커버리지에 "모든 속도 모드 × 실제 SCL 주기 체크" 가 없었다면 잡기 어렵다. 리셋값이 기본 모드와 같으면 더 숨는다.

</details>

<details>
<summary>Q7. 같은 플랜인데 pre-silicon 과 post-silicon 에서 무엇이 달라지나요?</summary>

pre-silicon(시뮬/에뮬): 내부 신호 가시성 100 %, 느림, 아날로그·PVT 없음 → 기능 커버리지와 에러 주입(force)이 쉽다. post-silicon: 실시간 속도, 가시성은 레지스터·핀·LA 뿐, 대신 실제 상승 시간·잡음·온도·전압을 본다 → TC-03/10/12 같은 타이밍·코너 테스트가 의미를 갖는다. 테스트 로직(TC-01/02)은 bus ops 만 바꿔 재사용하고(SIV-B13), 에러 자극은 force 대신 FPGA 타깃·2nd master 로 만든다. 꼬리질문 "post-silicon 에서만 보이는 버그 예?" → 풀업·버스 용량에 따른 rise time 마진, 스파이크 필터와 실제 잡음, 온도에 따른 타이밍 마진.

</details>

<details>
<summary>Q8. 테스트 플랜이 "충분한지" 어떻게 판단하나요?</summary>

① 추적성: 모든 feature·레지스터 필드에 테스트가 있나(빈칸 없는 매트릭스) ② 결함 주입: 알려진 결함 모델을 넣었을 때 잡히나, 진단이 겹치지 않나(이 레슨의 캠페인 표) ③ pre-silicon 기능 커버리지 결과 ④ 과거 errata·다른 프로젝트의 버그 데이터베이스 대조 ⑤ 리뷰 — 설계자·펌웨어·시스템 팀이 같이 본다. 꼬리질문 "결함 주입이 잡은 결함이 0 개면?" → 테스트가 무용하거나 결함 모델이 틀렸다. 둘 다 플랜 버그다.

</details>

## 9. 추가 연습

### 연습 1 — 마진이 있으면 off-by-one 은 rule=PROG 만 잡는다

Fm 을 380 kHz 목표(LCNT=74 → 1500 ns, HCNT=56 → 1140 ns)로 설정하면 `F_LCNT_OFF_BY_ONE` 이 어느 규칙에서 잡히나? 허용오차를 20 ns 로 늘리면?

<details>
<summary>답 보기</summary>

정상: 1500 + 1140 = 2640 ns → 378 787 Hz. 결함: tLOW 1480 ns → 2620 ns → 381 679 Hz. tLOW 1480 ≥ 1300, f ≤ 400 k(①통과), f ≥ 342 k(②통과) → **③ PROG 만** |1480 − 1500| = 20 > 10 으로 잡는다. 허용오차 20 ns 면 20 > 20 이 거짓이라 **놓친다**. 결론: 허용오차는 결함 크기보다 엄격하게 작아야 하고, 그러려면 측정 해상도가 충분해야 한다(LA 100 MS/s → 10 ns). `F_HCNT_STUCK` 은 같은 설정에서 rule=REQ 로 잡힌다. `practice.c` 의 `tc03_rule()`.

</details>

### 연습 2 — 우선순위 게이팅: BLOCK / INCOMPLETE / WARN / PASS

플랜과 결과 배열을 받아 판정하는 `plan_verdict()` 를 작성하라. P0 FAIL → BLOCK, P0 NOT_RUN → INCOMPLETE, P1 FAIL → WARN, 나머지 PASS. 여러 개면 강한 쪽.

<details>
<summary>답 보기</summary>

enum 을 강도 순(`V_PASS < V_WARN < V_INCOMPLETE < V_BLOCK`)으로 두고 항목마다 판정해 최댓값을 취한다. 강도 순서를 enum 값에 넣으면 `if (x > v) v = x;` 한 줄로 합쳐진다. P1 FAIL 과 P0 NOT_RUN 이 같이 있으면 INCOMPLETE — "아직 판정할 수 없음" 이 "경고" 보다 강하다. 이 레슨의 PLAN 은 TC-04~07(P0)이 NOT_RUN 이라 좋은 mock 이어도 INCOMPLETE 다. 그게 정직한 상태다.

</details>

### 연습 3 — 플랜 lint

`plan_lint()` 로 ① ID 중복 ② 측정 불가능한 pass 기준(숫자나 `= < >` 가 없음) ③ 우선순위가 0/1 이 아님 ④ 네 분류(정상/경계/에러/스트레스) 중 빠진 것 을 거부하라.

<details>
<summary>답 보기</summary>

O(n²) 중복 검사면 충분하다(플랜은 수십~수백 줄). "측정 가능" 판정은 거친 휴리스틱이지만 `"works fine"` 같은 문장은 확실히 걸러낸다. 분류는 비트마스크 `cats |= 1u << cat` 로 모아 마지막에 네 비트를 확인한다. 실제 팀에서는 플랜을 스프레드시트·YAML 로 두고 CI 에서 이런 lint 를 돌려, 코드의 `PLAN[]`/테스트 레지스트리와 ID 가 일치하는지도 검사한다(SIV-B08). 실행 결과: 앞 4줄만 넘기면 `no NORMAL test in plan`.

</details>

### 연습 4 — 랜덤 스트레스 + 스코어보드 (TC-11 축소판)

xorshift32 시드로 300개 랜덤 전송(write 1..7 바이트 / read 1..8 바이트, 임의 오프셋)을 만들고, 참조 모델 `ref[256]` 과 비교하라. 실패 메시지에 시드와 iteration 을 넣어 그대로 재현되게 하라.

<details>
<summary>답 보기</summary>

먼저 256 바이트 전체에 **배경 패턴**을 쓴다 — 배경이 0 이면 "0 을 읽어 오는" 결함(FIFO drop, underflow)이 0 인 위치에서는 보이지 않는다. write 는 ref 를 갱신, read 는 ref 와 비교. 포인터는 `uint8_t` 라 0xFF 다음 0x00 으로 감긴다 — ref 인덱스도 `(uint8_t)(off + i)`. 실제 출력:

```text
ex4: stress seed=0x1234ABCD iter=6 rd off=0xFE len=8 idx=7 exp=0xA0 act=0x00
```

같은 시드로 두 번 돌려 iteration·메시지가 같은지 CHECK 한다(재현성). `F_HCNT_STUCK` 에서는 300번 모두 통과 — 데이터 스트레스는 타이밍 결함을 못 본다. 스트레스는 기능별 테스트를 대신하지 못한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
```

실제 출력:

```text
ex1: off-by-one on Fm380 -> rule=PROG (tol 10ns), missed with tol 20ns
ex3: no NORMAL test in plan
ex4: stress seed=0x1234ABCD iter=6 rd off=0xFE len=8 idx=7 exp=0xA0 act=0x00
18/18 passed
```

ASan/UBSan 빌드(`-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer`)도 solution 59/59, practice 18/18, 진단 0 이었다 (Apple clang 21.0.0, arm64).

## 10. 핵심 정리

- 플랜 = **스펙 → Feature → TC(정상/경계/에러/스트레스) → 측정 가능한 Pass 기준 → 장비·FW → P0/P1 → 추적성**. 면접에서도 이 순서로 말한다.
- Pass 기준은 숫자 + 비교 연산. 출처를 세 겹으로: **표준(UM10204) / 제품 요구 / 프로그래밍 모델**. 허용오차는 측정 해상도 < tol < 결함 크기.
- 경계 = 최소·최대−1·최대(+1). FIFO full off-by-one 은 정확히 깊이에서만 보인다.
- 에러 테스트는 "보고했나" + "버스 해제·W1C·찌꺼기 없음·다음 전송 OK".
- 완료는 sticky DONE, 폴링엔 상한, 타임아웃 로그엔 마지막 읽은 값.
- 레지스터 테스트 PASS ≠ 기능 PASS (미배선 필드). 모든 모드를 돌린다.
- 결함 캠페인 매트릭스가 대각선이면(결함당 FAIL 하나, 좋은 DUT 전부 PASS) 플랜이 진단력이 있다는 증거. 테스트마다 DUT 리셋.
- P0 FAIL = BLOCK, P0 NOT_RUN = INCOMPLETE, P1 FAIL = WARN. 미구현을 숨기지 않는다.

## 참고자료

- [NXP UM10204 I2C-bus specification and user manual (Rev. 7.0)](https://www.nxp.com/docs/en/user-guide/UM10204.pdf) — Sm/Fm/Fm+ 타이밍 표(tLOW, tHIGH, tr, tSP), clock stretching, arbitration, Bus clear
- [System Management Bus (SMBus) Specification](http://smbus.org/specs/) — 클럭 low 타임아웃(25–35 ms), I2C 와의 차이
- Linux `drivers/i2c/busses/i2c-designware-*.c` — HCNT/LCNT 계산, TX abort 원인 레지스터 처리 (실제 IP 드라이버가 프로그래밍 모델을 어떻게 믿는지)
- IEEE 1800.2 (UVM) — `uvm_reg_hw_reset_seq`, `uvm_reg_bit_bash_seq`, `uvm_reg_access_seq`, `uvm_mem_walk_seq`
- Accellera SystemRDL 2.0, IEEE 1685 (IP-XACT) — 레지스터 기술서에서 테스트·헤더 생성 (SIV-B01)
- Microchip 24LC02B 데이터시트 — 내부 write cycle 동안의 NACK (ACK polling)
- 관련 레슨: SIV-B02 (mock MMIO·bus ops), SIV-B03/B04 (리셋값·R/W), SIV-B08 (테스트 레지스트리), SIV-B09 (poll-with-timeout), SIV-B11 (인터럽트 trigger-check-clear), SIV-B13 (pre vs post-silicon), SIV-B14 (로그 → 리포트), C-M02 (W1C 와 "본 것만 ack")
