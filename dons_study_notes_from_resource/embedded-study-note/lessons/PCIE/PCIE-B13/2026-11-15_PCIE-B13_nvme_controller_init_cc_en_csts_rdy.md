---
id: PCIE-B13
time: 60분
updated: 2026-11-15
---
# PCIE-B13 NVMe 컨트롤러 초기화 시퀀스: CC.EN과 CSTS.RDY

## 학습 목표

- NVMe 호스트 드라이버의 컨트롤러 reset/enable 절차를 **순서 그대로** 말하고 짠다: CAP 읽기 → 설정 검사 → CC.EN=0 → CSTS.RDY=0 대기 → AQA/ASQ/ACQ → CC → CC.EN=1 → CSTS.RDY=1 대기.
- CAP.TO(500 ms 단위)로 대기 예산을 계산하고, "만료 뒤에도 마지막 한 번은 읽는" 타임아웃 폴링 루프를 짠다. 반환 코드는 실패 지점마다 다르게.
- 오류 주입 6가지(RDY 안 옴, disable 안 끝남, CFS, 처음부터 CFS, 장치 사라짐 0xFFFFFFFF, 잘못된 MPS/CSS/큐 크기/정렬)를 반환 코드·단계·레지스터 쓰기 로그로 판정한다.
- 공유 레지스터를 `_Atomic uint32_t`, 로그를 mutex 로 지켜 **TSan 이 깨끗한** 2-스레드 mock 을 만든다. 판정은 시간 측정이 아니라 상태와 로그로 한다.
- SSD 펌웨어 쪽(컨트롤러)에서 EN 0→1 을 받았을 때 무엇을 검사하는지 구현하고, bring-up 로그(`CSTS=0x...`)를 보고 원인을 좁힌다.

## 1. 문제

면접에서 "NVMe 드라이버가 컨트롤러를 켜는 순서를 말해 보세요 → RDY 가 안 오면요? CFS 가 뜨면요?" 를 묻는 이유: **Linux `nvme` probe 의 첫 단계이자 SSD 펌웨어의 첫 핸드셰이크**라서, bring-up 에서 SSD 가 안 잡힐 때 가장 먼저 보는 곳이 바로 이 레지스터 몇 개다.

Don 은 SSD 펌웨어 쪽에서 이 시퀀스의 반대편을 짜 봤다: 호스트가 CC.EN 을 1 로 쓰면 펌웨어가 AQA/ASQ/ACQ 를 읽어 Admin 큐를 준비하고 CSTS.RDY 를 올린다. 이 레슨은 **호스트 드라이버 쪽**을 정확하게 짜고(Part A·B), 컨트롤러 쪽 검사도 한 번 구현한다(Part C). BAR0 레지스터 지도 전체와 CAP/VS/CC/CSTS 디코더는 PCIE-B12, 그 뒤의 Admin 큐 링·Identify·I/O 큐 생성은 PCIE-M01..M03 이다.

이 레슨이 쓰는 레지스터 (오프셋·비트는 **Linux `include/linux/nvme.h` (커널 7.0 헤더) 기준**, Lima VM 에서 직접 확인):

| 오프셋 | 이름 | 크기 | 이 레슨에서 쓰는 필드 |
|---|---|---|---|
| 0x00 | CAP (Controller Capabilities) | 64, RO | MQES 15:0 (0-based), AMS 18:17, **TO 31:24 (500 ms 단위)**, CSS 44:37, MPSMIN 51:48, MPSMAX 55:52 |
| 0x14 | CC (Controller Configuration) | 32 | **EN 0**, CSS 6:4, MPS 10:7, AMS 13:11, SHN 15:14, IOSQES 19:16, IOCQES 23:20 |
| 0x1C | CSTS (Controller Status) | 32, RO | **RDY 0**, **CFS 1**, SHST 3:2, NSSRO 4, PP 5 |
| 0x24 | AQA (Admin Queue Attributes) | 32 | ASQS 11:0, ACQS 27:16 (둘 다 0-based 엔트리 수) |
| 0x28 | ASQ (Admin SQ Base) | 64 | 63:12 = 페이지 정렬 물리 주소 |
| 0x30 | ACQ (Admin CQ Base) | 64 | 63:12 = 페이지 정렬 물리 주소 |

CAP.AMS 는 헤더에 매크로가 없어 스펙 표의 비트 18:17 로 썼다(스펙 확인 권장). 나머지는 헤더의 `NVME_REG_*`, `NVME_CAP_*`, `NVME_CC_*`, `NVME_CSTS_*` 와 같은 이름·값을 코드에 `#define` 으로 옮겼다 (맥에는 헤더가 없으므로).

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">CC (0x14) = 0x00460001 — 이 레슨의 init 이 마지막으로 쓰는 값</text>
  <g fill="currentColor">
    <text x="62" y="36">31:24</text><text x="222" y="36">23:20</text><text x="302" y="36">19:16</text>
    <text x="382" y="36">15:14</text><text x="422" y="36">13:11</text><text x="482" y="36">10:7</text>
    <text x="562" y="36">6:4</text><text x="622" y="36">3:1</text><text x="684" y="36">0</text>
  </g>
  <g stroke="currentColor">
    <rect x="60" y="42" width="160" height="30" class="f-mut"/>
    <rect x="220" y="42" width="80" height="30" class="f-acc"/>
    <rect x="300" y="42" width="80" height="30" class="f-acc"/>
    <rect x="380" y="42" width="40" height="30" class="f-warn"/>
    <rect x="420" y="42" width="60" height="30" class="f-mut"/>
    <rect x="480" y="42" width="80" height="30" class="f-ok"/>
    <rect x="560" y="42" width="60" height="30" class="f-ok"/>
    <rect x="620" y="42" width="60" height="30" class="f-mut"/>
    <rect x="680" y="42" width="20" height="30" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="66" y="62">rsvd (24 = CRIME)</text><text x="226" y="62">IOCQES</text><text x="306" y="62">IOSQES</text>
    <text x="386" y="62">SHN</text><text x="426" y="62">AMS</text><text x="486" y="62">MPS</text>
    <text x="566" y="62">CSS</text><text x="626" y="62">rsvd</text><text x="682" y="62">EN</text>
  </g>
  <g fill="currentColor" font-family="monospace">
    <text x="100" y="90">0000 0000</text><text x="244" y="90">0100</text><text x="324" y="90">0110</text>
    <text x="390" y="90">00</text><text x="436" y="90">000</text><text x="504" y="90">0000</text>
    <text x="576" y="90">000</text><text x="636" y="90">000</text><text x="686" y="90">1</text>
  </g>
  <g fill="currentColor">
    <text x="232" y="106">= 4 (16 B)</text><text x="312" y="106">= 6 (64 B)</text><text x="384" y="106">없음</text>
    <text x="426" y="106">RR</text><text x="486" y="106">4 KiB</text><text x="566" y="106">NVM</text>
  </g>
  <text x="10" y="138" fill="currentColor" font-weight="bold" font-size="13">CSTS (0x1C) 하위 6비트 — 호스트가 폴링하는 값</text>
  <g stroke="currentColor">
    <rect x="60" y="148" width="60" height="30" class="f-mut"/>
    <rect x="120" y="148" width="60" height="30" class="f-mut"/>
    <rect x="180" y="148" width="120" height="30" class="f-acc"/>
    <rect x="300" y="148" width="60" height="30" class="f-warn"/>
    <rect x="360" y="148" width="60" height="30" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="72" y="168">PP 5</text><text x="124" y="168">NSSRO 4</text><text x="200" y="168">SHST 3:2</text>
    <text x="310" y="168">CFS 1</text><text x="370" y="168">RDY 0</text>
    <text x="440" y="158">RDY: EN 0→1 뒤 1, EN 1→0 뒤 0 (CAP.TO 안에)</text>
    <text x="440" y="174">CFS: 치명 오류 → 호스트가 reset 해야 함</text>
    <text x="440" y="190">SHST: 00 정상 · 01 진행 중 · 10 완료 · 11 rsvd</text>
  </g>
  <text x="10" y="222" fill="currentColor" font-weight="bold" font-size="13">AQA (0x24) = 0x001F001F — Admin SQ/CQ 각 32 엔트리 (0-based 라서 31)</text>
  <g stroke="currentColor">
    <rect x="60" y="232" width="80" height="30" class="f-mut"/>
    <rect x="140" y="232" width="240" height="30" class="f-acc"/>
    <rect x="380" y="232" width="80" height="30" class="f-mut"/>
    <rect x="460" y="232" width="240" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor">
    <text x="70" y="252">31:28 rsvd</text><text x="150" y="252">ACQS 27:16 = 0x01F (32−1)</text>
    <text x="390" y="252">15:12 rsvd</text><text x="470" y="252">ASQS 11:0 = 0x01F (32−1)</text>
    <text x="60" y="284">범위: 0x001..0xFFF → 2..4096 엔트리. ASQ/ACQ 는 CC.MPS 페이지(여기 4 KiB) 정렬 → 하위 12비트 0</text>
  </g>
</svg>
```

**시뮬레이션 모델 (이 레슨이 정한 것).** 실제 컨트롤러 대신 `mock_ctrl` 이 별도 pthread 로 돈다. 레지스터는 `_Atomic uint32_t reg[16]` (BAR0 0x00..0x3F), 호스트는 `mmio_rd32 / mmio_wr32` 로만 접근한다 (64비트 레지스터는 32비트 두 번: lo → hi). 컨트롤러 스레드는 20 µs 마다 CC 를 보고 반응한다.

| 항목 | 값 / 동작 |
|---|---|
| 시간 축척 | **CAP.TO 1 단위 = 실제 500 ms → 시뮬 10 ms** (`SIM_TO_UNIT_US`, 50배 빠름). 테스트 전체 < 0.4 s |
| EN 0→1 | `EV_EN_SEEN` → 1 ms 뒤 `fw_validate_enable` 통과면 RDY=1, 실패면 CFS=1 (`EV_READY` / `EV_BAD_CFG`) |
| EN 1→0 | 0.5 ms 뒤 RDY·CFS·SHST 를 0 으로 (`EV_DISABLED`) — "reset 이 성공하면 CFS 도 풀린다" 는 **이 mock 의 정책** |
| 결함 주입 | `F_NO_READY`(RDY 영영 안 옴), `F_DISABLE_STUCK`(RDY 1 에 붙음), `F_CFS_ON_ENABLE`, `F_GONE_ON_ENABLE`(링크 사라짐 → 모든 읽기 0xFFFFFFFF), 연습용 `F_CFS_ONCE`, `F_SHN_STUCK` |
| 위반 기록 | EN=1(또는 RDY=1)인데 AQA/ASQ/ACQ 쓰기, EN=1 인데 CC 의 CSS/MPS/AMS 바꾸기, RDY=1 인데 EN 0→1, RO 레지스터 쓰기 |
| 로그 | 호스트 쓰기 로그(오프셋, 값), 컨트롤러 이벤트 로그, 위반 로그 — 전부 mutex 보호. **이벤트는 레지스터를 바꾸기 전에 기록**한다 |

### Part A — 정상 경로

- A1 `nvme_rdy_budget_us(cap)`: CAP.TO → 대기 예산 (시뮬 µs).
- A2 `nvme_wait_rdy(m, want, budget, r)`: CSTS 폴링. want=1 은 RDY=1, want=0 은 RDY=0 **그리고** CFS=0 이면 성공. 0xFFFFFFFF → `NV_ERR_GONE`, want=1 일 때 CFS → `NV_ERR_CFS`, 만료 → `NV_ERR_TO_ENABLE` / `NV_ERR_TO_DISABLE`. 만료 뒤에도 마지막 한 번은 읽는다.
- A3 `nvme_disable(m, cap, r)`: EN=1 이면 EN·SHN 만 지워 쓰고 대기, EN=0 인데 RDY 나 CFS 가 1 이면 대기만, 다 0 이면 쓰기도 대기도 없이 OK.
- A4 `nvme_check_cfg(cap, cfg, &cc)`: 레지스터를 건드리기 **전에** MPS → CSS → 큐 크기 → 정렬을 검사하고 CC 값(EN=0)을 만든다.
- A5 `nvme_init(m, cfg, r)`: 전체 시퀀스. 단계(stage)를 기록한다. 처음 켜진 컨트롤러와 이미 켜진(EN=1, RDY=1) 컨트롤러 둘 다에서 정확한 쓰기 순서가 나와야 한다.

### Part B — 오류 주입

RDY 가 안 옴(정확히 `NV_ERR_TO_ENABLE`, 예산 = TO × 단위), disable 이 안 끝남(`NV_ERR_TO_DISABLE`, AQA 를 절대 안 씀), EN 뒤 CFS(`NV_ERR_CFS` + reset 수행 + reset 완료 확인), 켜질 때부터 CFS(init 의 disable 이 곧 복구), MPS/CSS/큐 크기/정렬 오류 11가지(쓰기 0개), 장치 사라짐(CAP all-1s, EN 직후 all-1s), 예산 0, 그리고 버그 있는 쓰기 순서를 mock 이 위반으로 잡는지.

### Part C — 컨트롤러(SSD FW) 쪽 검사와 bring-up 복구

C1 `fw_validate_enable(m)`: EN 0→1 을 본 순간 SSD 펌웨어가 할 검사 (MPS 범위, CSS, AMS, AQA 최소 2 엔트리, ASQ/ACQ 0 아님 + CC.MPS 페이지 정렬). C2: ACQ 를 빼먹은 bring-up 드라이버 → 컨트롤러가 CFS + 이유 `BAD_ACQ` → 같은 장치에 정상 `nvme_init` → disable 이 CFS 를 지우고 정상 기동.

## 2. Problem (English)

> **NVMe controller enable sequence.** You are writing the probe path of an NVMe host driver. The controller's BAR0 is
> reachable only through 32-bit accessors `mmio_rd32(m, off)` / `mmio_wr32(m, off, v)`. A controller model runs in its own
> thread and reacts to `CC.EN` changes after a delay bounded by `CAP.TO` (units of 500 ms; scaled to 10 ms here).
>
> ```c
> uint32_t nvme_rdy_budget_us(uint64_t cap);
> int nvme_wait_rdy(mock_ctrl *m, int want, uint32_t budget_us, nvme_report *r);
> int nvme_disable(mock_ctrl *m, uint64_t cap, nvme_report *r);
> int nvme_check_cfg(uint64_t cap, const nvme_admin_cfg *c, uint32_t *cc_out);
> int nvme_init(mock_ctrl *m, const nvme_admin_cfg *c, nvme_report *r);
> ```
>
> 1. Read CAP. Validate the host page size against `CAP.MPSMIN/MPSMAX`, `CAP.CSS` NVM support, admin queue depths (2..4096)
>    and page alignment of ASQ/ACQ **before writing any register**.
> 2. If the controller is enabled, clear `CC.EN` and wait for `CSTS.RDY == 0` (timeout from `CAP.TO`).
> 3. Program AQA, ASQ, ACQ (64-bit registers written low half first), then CC with CSS=NVM, MPS, AMS=RR, IOSQES=6, IOCQES=4,
>    then set `CC.EN` and wait for `CSTS.RDY == 1`.
> 4. Errors: `CSTS == 0xFFFFFFFF` → device gone; `CSTS.CFS` while enabling → reset the controller and fail with `NV_ERR_CFS`;
>    timeouts must be distinguishable (disable vs enable).
>
> Example: fresh controller, admin queues at `0x1_2345_6000` / `0x1_2345_7000`, 32 entries each, 4 KiB pages →
> writes `AQA=0x001F001F, ASQ=0x23456000,0x00000001, ACQ=0x23457000,0x00000001, CC=0x00460000, CC=0x00460001`, returns 0.
>
> Follow-ups: What does `CC.EN=0` do to existing queues? Why must the host not set EN while RDY is still 1? What do you do
> if CFS never clears? How does the SSD firmware side validate the configuration when it sees EN go to 1?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 여섯 개: A1 `nvme_rdy_budget_us`, A2 `nvme_wait_rdy`, A3 `nvme_disable`, A4 `nvme_check_cfg`, A5 `nvme_init`, C1 `fw_validate_enable`. mock 컨트롤러·MMIO 접근 함수·로그·하네스는 주어진다. 스텁은 `0 / NV_ERR_PARAM / BAD_NONE` 을 돌려주고 대기 루프가 없으므로 멈추지 않는다 (하네스의 동기화 대기도 최대 2 초). 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:380  nvme_rdy_budget_us(CAP_LAB) == 400000u
FAIL starter.c:381  nvme_rdy_budget_us(make_cap(0, 0xFF, 1, 0, 0, 0)) == 2550000u
FAIL starter.c:386  nvme_check_cfg(CAP_LAB, &good, &cc) == NV_OK && cc == 0x00460000u
…
A fresh controller           rc=ERR_PARAM       stage=NONE     polls=0   budget=    0 us  last CSTS=0x00000000  writes=0
FAIL starter.c:410  rc == NV_OK && r.stage == ST_READY && r.cap == CAP_LAB
…
C2 then correct init         rc=ERR_PARAM       stage=NONE     polls=0   budget=    0 us  last CSTS=0x00000000  writes=5
FAIL starter.c:610  rc == NV_OK && reg_ld(&g_m, NVME_REG_CSTS) == NVME_CSTS_RDY && atomic_load(&g_m.bad_reason) == BAD_NONE
FAIL starter.c:611  n_ev(&g_m, EV_DISABLED) == 1 && n_ev(&g_m, EV_READY) == 1 && g_m.nviol == 0
9/48 passed
```

(exit 1.) 처음부터 통과하는 9개는 mock 자체의 검사다 — CAP 비트 위치 검산, 32비트 두 번으로 읽는 CAP, B10 의 위반 기록 5가지. 즉 **mock 은 맞고, 드라이버만 비어 있다.**

## 4. 힌트

<details><summary>힌트 1 — CAP.TO 는 "얼마나 기다려도 되나" 의 상한이다</summary>

TO 는 비트 31:24, 단위 500 ms. 실제 드라이버라면 `TO × 500 ms`, 이 레슨은 `TO × SIM_TO_UNIT_US`. 0x28 이면 실제 20 s, 시뮬 400 000 µs. 다른 필드가 섞이지 않게 `(cap >> 24) & 0xff`.
</details>

<details><summary>힌트 2 — 폴링 루프는 "시간 먼저, 읽기 나중"</summary>

`expired = now >= deadline` 을 **읽기 전에** 계산하고, 읽은 값으로 성공/실패를 먼저 판정한 뒤, 그때도 `expired` 면 타임아웃. 그러면 예산이 0 이어도 정확히 한 번은 읽고, 마지막 sleep 중에 RDY 가 올라온 경우를 놓치지 않는다. 0xFFFFFFFF 검사는 RDY/CFS 비트 검사보다 먼저 — all-1s 는 RDY=1, CFS=1 로도 읽히기 때문이다.
</details>

<details><summary>힌트 3 — disable 의 세 경우</summary>

(EN=1) → EN 과 SHN 만 지운 값을 쓰고 대기. (EN=0, RDY 나 CFS 가 1) → 앞선 reset 이 진행 중이니 쓰지 말고 대기만. (EN=0, RDY=0, CFS=0) → 이미 꺼짐, 아무것도 안 함. RDY=0 대기는 CFS=0 도 함께 확인한다 — 이유는 풀이의 "RDY=0 만으로는 부족" 그림.
</details>

<details><summary>힌트 4 — 검사 먼저, 쓰기 나중</summary>

page_size 가 2의 거듭제곱인지 → `log2(page) − 12` 가 MPSMIN..MPSMAX 안인지 → CAP.CSS bit0 → 깊이 2..4096 → `asq & (page−1) == 0`. 하나라도 실패하면 `*cc_out` 도, 레지스터도 건드리지 말고 반환. CC 값은 `CSS_NVM | MPS<<7 | AMS_RR | SHN_NONE | 6<<16 | 4<<20`.
</details>

<details><summary>힌트 5 — CFS 처리와 컨트롤러 쪽 검사</summary>

RDY=1 대기 중 CFS 를 보면: EN=0 을 쓰고 RDY=0 && CFS=0 을 기다린 뒤(결과는 `reset_rc`), `NV_ERR_CFS` 반환. 컨트롤러 쪽 `fw_validate_enable` 은 MPS → CSS → AMS → AQA → ASQ → ACQ 순서로, 정렬은 **CC.MPS 가 말하는 페이지 크기** 기준 (`(1 << (12+mps)) − 1` 마스크).
</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "CAP 을 먼저 읽습니다 — TO(타임아웃), MPSMIN/MAX, CSS, MQES 가 다 여기 있어요. all-1s 면 장치가 없거나 BAR 디코드가 꺼진 겁니다 (PCIE-B03)."
2. "설정을 **레지스터 쓰기 전에** 검사합니다. 페이지 크기, 명령 집합, Admin 큐 깊이 2..4096, ASQ/ACQ 페이지 정렬."
3. "EN 이 1 이면 0 으로 쓰고 RDY 가 0 이 될 때까지 CAP.TO 안에서 폴링. 이게 controller reset 입니다."
4. "AQA, ASQ, ACQ 를 씁니다. 64비트는 lo 먼저."
5. "CC 에 CSS, MPS, AMS, IOSQES=6, IOCQES=4 를 넣고, EN=1 을 씁니다."
6. "RDY=1 을 CAP.TO 안에서 기다립니다. 중간에 0xFFFFFFFF 면 장치 사라짐, CFS 면 reset 하고 실패 반환."
7. "그다음은 Admin 큐로 Identify (PCIE-M01, M02)."

### Part A — CAP.TO 와 대기 예산

CAP.TO 는 "EN 을 바꾼 뒤 RDY 가 따라오기까지 **최악** 얼마나 걸리나" 를 컨트롤러가 스스로 신고하는 값이다. EN 0→1 (켜기)과 1→0 (끄기) **두 방향 모두**에 쓴다. shutdown(SHST) 대기에는 쓰지 않는다 (연습 1).

| CAP.TO | 실제 대기 상한 (× 500 ms) | 이 레슨의 시뮬 예산 (× 10 ms) |
|---|---|---|
| 0x01 | 0.5 s | 10 000 µs |
| 0x05 (`CAP_FAST`) | 2.5 s | 50 000 µs |
| 0x28 (`CAP_LAB`) | 20 s | 400 000 µs |
| 0xFF | 127.5 s | 2 550 000 µs |

이 레슨의 가상 컨트롤러 `CAP_LAB = 0x00400020280103FF` 를 비트로 풀면 (테스트 첫 줄이 이 값을 검산한다):

| 비트 | 필드 | 값 | 뜻 |
|---|---|---|---|
| 15:0 | MQES | 0x03FF | I/O 큐 최대 1024 엔트리 (0-based) |
| 16 | CQR | 1 | 큐가 물리적으로 연속이어야 함 |
| 18:17 | AMS | 0 | RR 만 지원 |
| 31:24 | TO | 0x28 | 40 × 500 ms = 20 s |
| 44:37 | CSS | 0x01 | bit0: NVM 명령 집합 |
| 51:48 | MPSMIN | 0 | 2^(12+0) = 4 KiB |
| 55:52 | MPSMAX | 4 | 2^(12+4) = 64 KiB |

위쪽 32비트 0x00400020 = (MPSMAX 4 → 비트 54 → 위 워드 비트 22) 0x00400000 + (CSS bit0 → 비트 37 → 위 워드 비트 5) 0x20. 아래쪽 0x280103FF = TO 0x28 << 24 + CQR 0x10000 + MQES 0x3FF. **이 값은 가상**이다 — 실제 드라이브의 CAP 은 PCIE-B12 에서 디코드한다.

### Part A — 폴링 루프: 시간 먼저, 읽기 나중

```c
uint64_t deadline = now_us() + budget_us;
for (;;) {
    int expired = now_us() >= deadline;          /* ① 시간 */
    uint32_t csts = mmio_rd32(m, NVME_REG_CSTS); /* ② 읽기 */
    if (csts == 0xFFFFFFFFu) return NV_ERR_GONE; /* ③ all-1s 가 가장 먼저 */
    if (want && (csts & NVME_CSTS_CFS)) return NV_ERR_CFS;
    if (조건 충족) return NV_OK;                   /* ④ 성공 판정 */
    if (expired) return want ? NV_ERR_TO_ENABLE : NV_ERR_TO_DISABLE;   /* ⑤ 그다음에야 타임아웃 */
    sleep_us(HOST_POLL_US);
}
```

왜 이 순서인가: `while (now < deadline) { read; sleep; } return TIMEOUT;` 으로 짜면, 마지막 sleep 도중에 RDY 가 올라와도 다시 읽지 않고 타임아웃을 낸다 — 예산 경계에서 **성공을 실패로** 보고하는 버그. "시간 먼저 → 읽기 → 판정 → 만료면 실패" 로 하면 만료 뒤 반드시 한 번 더 읽는다. 테스트 B9 가 이걸 고정한다: 예산 0 으로 불러도 `polls == 1`, 이미 RDY=0 이면 `NV_OK`.

③ 이 먼저인 이유: 링크가 죽거나(surprise removal), Command 레지스터의 Memory Space 가 꺼져 있으면(PCIE-B03) MMIO 읽기는 Unsupported Request 로 끝나고 CPU 에는 0xFFFFFFFF 가 돌아온다(PCIE-B08). 0xFFFFFFFF 는 RDY=1, CFS=1 로도 읽히므로 비트를 보기 전에 걸러야 한다. 안 그러면 "CFS 가 떴다 → reset 하자" 로 죽은 장치에 계속 쓴다.

### Part A — disable 의 세 경우

| 읽은 CC.EN | 읽은 CSTS | 의미 | 하는 일 | 쓰기 |
|---|---|---|---|---|
| 1 | 아무거나 | 켜져 있거나 켜는 중 | `cc & ~(EN\|SHN)` 쓰기 → RDY=0 && CFS=0 대기 | 1 |
| 0 | RDY=1 또는 CFS=1 | 앞선 reset 이 진행 중 | 대기만 | 0 |
| 0 | RDY=0, CFS=0 | 이미 꺼짐 (전원 직후) | 바로 OK | 0 |

EN 만 지우고 나머지(CSS, MPS, IOSQES...)는 그대로 둔다. SHN 도 지우는 이유: shutdown 통지가 남아 있는 CC 로 다시 켜면 안 되기 때문이다 (Linux `nvme_disable_ctrl` 도 SHN 마스크를 지운다 — 기억 기반).

### Part A — 값 추적: 처음 켜진 컨트롤러

입력: `asq = 0x0000_0001_2345_6000`, `acq = 0x0000_0001_2345_7000`, 깊이 32/32, page 4096.

| # | 단계 | 계산 | 쓰기 (오프셋 = 값) |
|---|---|---|---|
| 1 | CAP | lo 0x280103FF, hi 0x00400020 | — |
| 2 | 검사 | 4096 = 2^12 → MPS 0 (0..4 안), CSS bit0=1, 32 ∈ [2,4096], 0x6000 & 0xFFF = 0 | — |
| 3 | disable | CC=0, CSTS=0 → 이미 꺼짐 | — |
| 4 | AQA | (32−1)<<16 \| (32−1) | 0x24 = 0x001F001F |
| 5 | ASQ | lo, hi | 0x28 = 0x23456000, 0x2C = 0x00000001 |
| 6 | ACQ | lo, hi | 0x30 = 0x23457000, 0x34 = 0x00000001 |
| 7 | CC | CSS 0 \| MPS 0<<7 \| AMS 0 \| SHN 0 \| 6<<16 \| 4<<20 | 0x14 = 0x00460000 |
| 8 | EN | \| 1 | 0x14 = 0x00460001 |
| 9 | 대기 | CSTS 0x0 … 0x1 | — |

page 16 KiB 이면 MPS = 14 − 12 = 2 → 2 << 7 = 0x100 → CC = 0x00460100. 테스트는 page_size 2^0..2^31 32개를 전부 넣어 `CAP_LAB` 에서 2^12..2^16 다섯 개만 받아들이고, 각 CC 값이 `0x00460000 | (s−12)<<7` 인지 확인한다.

이미 켜진 컨트롤러(EN=1, RDY=1 — UEFI 드라이버나 이전 커널이 남긴 상태, kexec 후 흔함)에서는 맨 앞에 `0x14 = 0x00460000` (EN 지움) 하나가 더 붙고, 그 뒤 `EV_DISABLED` 가 기록된 **다음에야** AQA 를 쓴다 (테스트가 `wlog[1].off == AQA` 와 이벤트 순서 `DISABLED → EN_SEEN → READY` 로 고정).

```svg
<svg viewBox="0 0 740 560" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b13a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <g stroke="currentColor">
    <rect x="80" y="14" width="370" height="36" rx="5" class="f-mut"/>
    <rect x="80" y="74" width="370" height="36" rx="5" class="f-mut"/>
    <rect x="80" y="134" width="370" height="36" rx="5" class="f-acc"/>
    <rect x="80" y="194" width="370" height="36" rx="5" class="f-acc"/>
    <rect x="80" y="254" width="370" height="36" rx="5" class="f-mut"/>
    <rect x="80" y="314" width="370" height="36" rx="5" class="f-mut"/>
    <rect x="80" y="374" width="370" height="36" rx="5" class="f-acc"/>
    <rect x="80" y="434" width="370" height="36" rx="5" class="f-acc"/>
    <rect x="80" y="506" width="370" height="36" rx="5" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="92" y="37">① CAP 읽기 (32비트 lo → hi)</text>
    <text x="92" y="97">② 검사: MPS · CSS · 깊이 2..4096 · 정렬 (쓰기 전)</text>
    <text x="92" y="157">③ CC.EN=1 이면 EN·SHN 지워 쓰기 (꺼져 있으면 생략)</text>
    <text x="92" y="217">④ RDY=0 &amp;&amp; CFS=0 대기 (예산 = CAP.TO)</text>
    <text x="92" y="277">⑤ AQA → ASQ lo/hi → ACQ lo/hi</text>
    <text x="92" y="337">⑥ CC = 0x00460000 (EN=0)</text>
    <text x="92" y="397">⑦ CC = 0x00460001 (EN=1)</text>
    <text x="92" y="457">⑧ RDY=1 대기 (예산 = CAP.TO)</text>
    <text x="92" y="529">⑨ READY → Admin 큐로 Identify (PCIE-M01, M02)</text>
  </g>
  <g stroke="currentColor" marker-end="url(#b13a)">
    <line x1="265" y1="50" x2="265" y2="72"/><line x1="265" y1="110" x2="265" y2="132"/>
    <line x1="265" y1="170" x2="265" y2="192"/><line x1="265" y1="230" x2="265" y2="252"/>
    <line x1="265" y1="290" x2="265" y2="312"/><line x1="265" y1="350" x2="265" y2="372"/>
    <line x1="265" y1="410" x2="265" y2="432"/><line x1="265" y1="470" x2="265" y2="504"/>
    <line x1="450" y1="32" x2="488" y2="32"/><line x1="450" y1="92" x2="488" y2="92"/>
    <line x1="450" y1="212" x2="488" y2="212"/>
    <line x1="450" y1="446" x2="488" y2="420"/><line x1="450" y1="452" x2="488" y2="462"/>
    <line x1="450" y1="460" x2="488" y2="504"/>
  </g>
  <g stroke="currentColor">
    <rect x="490" y="16" width="245" height="32" rx="5" class="f-warn"/>
    <rect x="490" y="76" width="245" height="32" rx="5" class="f-warn"/>
    <rect x="490" y="196" width="245" height="32" rx="5" class="f-warn"/>
    <rect x="490" y="404" width="245" height="32" rx="5" class="f-warn"/>
    <rect x="490" y="446" width="245" height="32" rx="5" class="f-warn"/>
    <rect x="490" y="488" width="245" height="32" rx="5" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="500" y="37">all-1s → ERR_GONE, 쓰기 0</text>
    <text x="500" y="97">ERR_MPS/CSS/QSIZE/ALIGN, 쓰기 0</text>
    <text x="500" y="217">만료 → ERR_TO_DISABLE (AQA 안 씀)</text>
    <text x="500" y="425">CFS → EN=0, reset 대기 → ERR_CFS</text>
    <text x="500" y="467">만료(+마지막 1회 읽기) → ERR_TO_ENABLE</text>
    <text x="500" y="509">0xFFFFFFFF → ERR_GONE</text>
  </g>
</svg>
```

### 실행 흐름 — 두 스레드가 레지스터를 공유한다

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="b13b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="8" y="74" fill="currentColor" font-weight="bold">호스트</text>
  <text x="8" y="154" fill="currentColor" font-weight="bold">컨트롤러</text>
  <line x1="70" y1="70" x2="725" y2="70" stroke="currentColor"/>
  <line x1="70" y1="150" x2="725" y2="150" stroke="currentColor"/>
  <g stroke="currentColor" stroke-width="2">
    <line x1="85" y1="58" x2="85" y2="82"/><line x1="97" y1="58" x2="97" y2="82"/><line x1="109" y1="58" x2="109" y2="82"/>
    <line x1="121" y1="58" x2="121" y2="82"/><line x1="133" y1="58" x2="133" y2="82"/>
    <line x1="160" y1="58" x2="160" y2="82"/><line x1="180" y1="52" x2="180" y2="88"/>
  </g>
  <g fill="currentColor">
    <text x="78" y="44">AQA·ASQ·ACQ</text><text x="150" y="30">CC, CC|EN</text>
  </g>
  <g stroke="currentColor">
    <line x1="210" y1="66" x2="210" y2="74"/><line x1="240" y1="66" x2="240" y2="74"/><line x1="270" y1="66" x2="270" y2="74"/>
    <line x1="300" y1="66" x2="300" y2="74"/><line x1="330" y1="66" x2="330" y2="74"/><line x1="360" y1="66" x2="360" y2="74"/>
    <line x1="390" y1="66" x2="390" y2="74"/><line x1="420" y1="66" x2="420" y2="74"/><line x1="450" y1="66" x2="450" y2="74"/>
    <line x1="480" y1="66" x2="480" y2="74"/>
  </g>
  <text x="250" y="100" fill="currentColor">CSTS 폴링 (50 µs 마다, RDY=0 …)</text>
  <circle cx="500" cy="70" r="6" class="f-ok" stroke="currentColor"/>
  <text x="512" y="56" fill="currentColor">RDY=1 읽음 → NV_OK</text>
  <circle cx="195" cy="150" r="5" class="f-acc" stroke="currentColor"/>
  <text x="140" y="176" fill="currentColor">EV_EN_SEEN (≤ 20 µs tick)</text>
  <line x1="195" y1="132" x2="470" y2="132" stroke="currentColor" stroke-dasharray="4,3"/>
  <text x="270" y="126" fill="currentColor">ready_delay 1 ms</text>
  <circle cx="470" cy="150" r="6" class="f-ok" stroke="currentColor"/>
  <text x="400" y="176" fill="currentColor">EV_READY 기록 → 그다음 RDY=1</text>
  <line x1="472" y1="144" x2="498" y2="78" stroke="currentColor" marker-end="url(#b13b)"/>
  <line x1="180" y1="88" x2="193" y2="143" stroke="currentColor" stroke-dasharray="3,3" marker-end="url(#b13b)"/>
  <rect x="180" y="200" width="545" height="12" class="f-mut" stroke="currentColor"/>
  <text x="180" y="230" fill="currentColor">대기 예산 = CAP.TO × 10 ms = 400 ms (실제 20 s) — 눈금 비례 아님, 정상 경로는 몇 ms 만에 끝남</text>
  <text x="180" y="250" fill="currentColor">F_NO_READY: RDY 가 안 오면 예산 끝 + 마지막 1회 읽기 → NV_ERR_TO_ENABLE</text>
</svg>
```

공유되는 것은 레지스터 배열 하나다. 호스트 스레드는 CC/AQA/ASQ/ACQ 를 쓰고 CSTS 를 읽고, 컨트롤러 스레드는 CC 를 읽고 CSTS 를 쓴다. 평범한 `uint32_t` 로 두면 이건 **데이터 레이스 = C11 에서 정의되지 않은 동작**이고 TSan 이 바로 잡는다. 그래서:

- 레지스터는 `_Atomic uint32_t` (seq_cst load/store/fetch_or/fetch_and). 실제 MMIO 에서 이 역할은 `volatile` + 순서 보장(`readl/writel` 의 배리어, ARCH-M12)이 한다 — 장치는 C 메모리 모델 밖에 있으니까.
- 로그 세 개는 mutex. 컨트롤러 스레드 전용 상태(`seen_cc`, `pending`, `due`)는 그 스레드만 만진다. `faults`·지연값은 `pthread_create` **전에** 정해서 생성이 동기화 지점이 된다.
- 컨트롤러는 **이벤트를 먼저 기록하고 레지스터를 바꾼다.** 그러면 "호스트가 RDY=1 을 봤다 ⇒ `EV_READY` 는 이미 로그에 있다" 가 성립해서 테스트가 시간이 아닌 로그로 판정할 수 있다. 처음 버전은 순서가 반대였고, 부하를 걸면 이벤트 검사가 가끔 실패했다.

판정이 시간에 기대지 않게 한 것들: 정상 경로는 예산(400 ms)이 지연(1 ms)의 400배라 결과가 흔들리지 않는다. 타임아웃 경로는 결함이 "영원히 안 옴" 이라 결과가 시간과 무관하다. polls 횟수는 출력만 하고 `>= 2` 같은 하한만 검사한다. 맥에서 TSan 빌드 16개 + ASan 빌드 16개를 동시에 돌려(32 프로세스) 전부 통과를 확인했다. 그 과정에서 "EN=0, RDY=1" 테스트가 이전 테스트의 `r.polls` 를 읽는 바람에 가끔 실패하는 것을 찾아 고쳤다 — 대기 함수가 일찍 반환하면 report 는 이전 값 그대로다.

### Part B — 오류별 판정

| 테스트 | 결함 | 호스트가 보는 것 | rc | stage | 쓰기 | 추가 확인 |
|---|---|---|---|---|---|---|
| B1 | `F_NO_READY`, TO=5 | CSTS 0 계속 | `ERR_TO_ENABLE` | ENABLE | 7 | budget 50 000 µs, polls ≥ 2 |
| B2 | `F_DISABLE_STUCK` | RDY=1 계속 | `ERR_TO_DISABLE` | DISABLE | 1 (CC 만) | AQA 쓰기 0 |
| B3 | `F_CFS_ON_ENABLE` | CSTS 0x2 | `ERR_CFS` | ENABLE | 8 (마지막 = CC EN=0) | `reset_rc == OK`, 이벤트 EN_SEEN→CFS→DISABLED, 최종 CSTS 0 |
| B4 | 시작 상태 EN=1, RDY=1, CFS=1 | — | `OK` | READY | 8 | disable 이 곧 복구 |
| B5 | 설정 오류 11가지 | — | MPS/CSS/QSIZE/ALIGN | CHECK | 0 | CC 그대로 0x00460001 |
| B7 | 시작부터 gone | CAP = all-1s | `ERR_GONE` | CAP | 0 | |
| B8 | `F_GONE_ON_ENABLE` | CSTS 0xFFFFFFFF | `ERR_GONE` | ENABLE | 7 | 타임아웃까지 안 기다림 |
| B9 | 예산 0 | — | OK / `ERR_TO_ENABLE` | — | 0 | polls == 1 |

B2 에서 "AQA 를 절대 안 쓴다" 가 중요하다. disable 이 끝나지 않았는데 Admin 큐 주소를 바꾸면, 아직 살아 있는 컨트롤러가 **옛 큐를 처리하는 도중 주소가 바뀐다** — DMA 가 엉뚱한 메모리를 읽고 쓴다. mock 은 이걸 `V_ADMINQ_WHILE_EN` 으로 기록한다.

B5 의 11가지: page 64 KiB > MPSMAX 16 KiB, 2 KiB < 4 KiB, 6144 (2의 거듭제곱 아님), 0, 4 KiB < MPSMIN 8 KiB, CAP.CSS 에 NVM 없음 (0x40 = I/O 명령 집합 지원 비트만), SQ 깊이 1, CQ 깊이 4097, CQ 깊이 0, ASQ 가 0x800 어긋남, page 16 KiB 인데 ACQ 가 4 KiB 정렬뿐. 모두 **쓰기 0개**로 끝나야 한다: 잘못된 MPS 로 CC 를 써 버리면 그 자체가 컨트롤러에는 "설정 바꿈" 이고, 이미 켜진 장치였다면 disable 까지 해 버린 뒤다. 검사를 앞에 두면 실패해도 장치 상태가 그대로다.

Admin 큐 깊이 상한이 MQES+1 이 아니라 4096 인 이유: AQA 의 ASQS/ACQS 가 12비트(0-based)이고, 스펙은 MQES 를 **I/O 큐**의 상한으로 정의한다 (스펙 확인 권장 — 보수적인 드라이버는 Admin 큐도 MQES+1 이하로 잡는다. Linux 는 `NVME_AQ_DEPTH = 32` 고정, 헤더에서 확인). I/O 큐 깊이 계산은 연습 5.

### Part B — CFS: reset 하고, reset 이 끝났는지 확인

CFS(Controller Fatal Status)는 "완료 큐로 알릴 수 없는 치명 오류" 다. 호스트가 할 수 있는 첫 조치는 controller reset (CC.EN 1→0)이다. 그런데 여기 함정이 있다:

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="b13c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">시작 상태: EN=1, RDY=0, CFS=1 (켜다가 치명 오류 — RDY 는 한 번도 1 이 아니었다)</text>
  <text x="10" y="44" fill="currentColor" font-weight="bold">RDY 만 보는 대기 (버그)</text>
  <g stroke="currentColor">
    <rect x="10" y="52" width="130" height="44" rx="4" class="f-mut"/>
    <rect x="155" y="52" width="130" height="44" rx="4" class="f-warn"/>
    <rect x="300" y="52" width="130" height="44" rx="4" class="f-mut"/>
    <rect x="445" y="52" width="130" height="44" rx="4" class="f-mut"/>
    <rect x="590" y="52" width="140" height="44" rx="4" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="18" y="70">CC.EN=0 쓰기</text><text x="18" y="88">(reset 시작)</text>
    <text x="163" y="70">폴링 #1: RDY=0</text><text x="163" y="88">→ 바로 "끝났다"</text>
    <text x="308" y="70">AQA..CC|EN</text><text x="308" y="88">즉시 씀</text>
    <text x="453" y="70">컨트롤러: reset</text><text x="453" y="88">못 끝내고 다시 켜짐</text>
    <text x="598" y="70">CFS 그대로</text><text x="598" y="88">→ ERR_CFS</text>
  </g>
  <text x="10" y="128" fill="currentColor" font-weight="bold">RDY=0 &amp;&amp; CFS=0 대기 (이 레슨)</text>
  <g stroke="currentColor">
    <rect x="10" y="136" width="130" height="44" rx="4" class="f-mut"/>
    <rect x="155" y="136" width="130" height="44" rx="4" class="f-acc"/>
    <rect x="300" y="136" width="130" height="44" rx="4" class="f-acc"/>
    <rect x="445" y="136" width="130" height="44" rx="4" class="f-mut"/>
    <rect x="590" y="136" width="140" height="44" rx="4" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="18" y="154">CC.EN=0 쓰기</text><text x="18" y="172">(reset 시작)</text>
    <text x="163" y="154">폴링: CFS=1</text><text x="163" y="172">→ 계속 기다림</text>
    <text x="308" y="154">reset 끝:</text><text x="308" y="172">CSTS = 0x0</text>
    <text x="453" y="154">AQA..CC|EN</text><text x="453" y="172">이제 씀</text>
    <text x="598" y="154">RDY=1 → OK</text>
  </g>
  <g stroke="currentColor" marker-end="url(#b13c)">
    <line x1="140" y1="74" x2="153" y2="74"/><line x1="285" y1="74" x2="298" y2="74"/><line x1="430" y1="74" x2="443" y2="74"/><line x1="575" y1="74" x2="588" y2="74"/>
    <line x1="140" y1="158" x2="153" y2="158"/><line x1="285" y1="158" x2="298" y2="158"/><line x1="430" y1="158" x2="443" y2="158"/><line x1="575" y1="158" x2="588" y2="158"/>
  </g>
  <text x="10" y="202" fill="currentColor">practice.c 연습 4 실측: 버그 판 "wait0 rc=OK polls=1 -&gt; enable rc=ERR_CFS", 고친 판 "rc=OK EV_DISABLED=1"</text>
</svg>
```

RDY 가 **원래 0** 이었으면 "RDY=0 을 기다린다" 는 아무 정보도 주지 않는다. 스펙의 핸드셰이크는 "EN 1→0 뒤 컨트롤러가 다시 켜질 준비가 되면 RDY 를 0 으로" 인데, 이미 0 인 비트는 바뀌지 않는다. 이 레슨의 mock 은 "reset 이 끝나면 CFS 도 지운다" 를 정책으로 정했고, 그래서 RDY=0 대기를 **RDY=0 && CFS=0** 으로 정의했다. 처음에 RDY 만 보게 짰을 때 실제로 C2 가 `rc=ERR_CFS stage=ENABLE polls=1` 로 실패했다 — 그 경험을 연습 4 로 고정해 두었다.

실제 HW 에서 CFS 가 reset 후 언제 풀리는지는 컨트롤러마다 다르다 (스펙 확인 권장). Linux 에는 EN 을 바꾼 뒤 RDY 를 보기 전에 2.3 초를 그냥 기다리는 quirk(`NVME_QUIRK_DELAY_BEFORE_CHK_RDY`)가 있는데, 바로 "RDY 만으로는 reset 완료를 알 수 없는" 장치들 때문이다 (기억 기반, 소스 확인 권장). CFS 가 reset 으로도 안 풀리면 다음 단계로 올라간다: NVM Subsystem Reset (CAP.NSSRS 가 1 이면 NSSR 레지스터에 0x4E564D65 "NVMe" 쓰기) → PCIe Function Level Reset / 링크 hot reset → 전원 껐다 켜기 (PCIE-A03).

### Part B — mock 이 잡는 위반

| 위반 | 언제 | 왜 나쁜가 |
|---|---|---|
| `V_ADMINQ_WHILE_EN` | EN=1 또는 RDY=1 인데 AQA/ASQ/ACQ(각 반쪽 포함) 쓰기 | 살아 있는 컨트롤러의 큐 주소가 바뀜 → DMA 오염. 스펙상 EN=0 일 때만 바꾼다 |
| `V_CC_CFG_WHILE_EN` | EN=1 상태에서 CC 의 CSS/MPS/AMS 를 바꾸는 쓰기 | 켜진 뒤 페이지 크기가 바뀌면 PRP 해석(PCIE-M04)이 달라짐 |
| `V_EN_WHILE_RDY` | EN 0→1 쓰기 때 RDY 가 아직 1 | 이전 reset 이 안 끝났는데 다시 켬 → 결과 정의 안 됨 |
| `V_RO_WRITE` | CAP/VS/CSTS 쓰기 | 무시됨. 대개 오프셋 계산 실수의 증거 |

정상 `nvme_init` 은 모든 테스트에서 위반 0 건이고(`g_m.nviol == 0` 을 여러 곳에서 검사), B10 의 일부러 틀린 순서 6개 쓰기는 정확히 위반 5 건(ADMINQ 2, CC_CFG 1, RO 1, EN_WHILE_RDY 1)이다. 위반이어도 mock 은 쓰기를 반영한다 — 실제 하드웨어도 막아 주지 않는다.

### Part C — SSD 펌웨어 쪽: EN 0→1 을 받으면

Don 이 짰던 쪽이다. 펌웨어는 CC 쓰기 인터럽트(또는 폴링)에서 EN 0→1 을 보면 Admin 큐 레지스터를 래치하고 검사한다. 이 레슨의 `fw_validate_enable` 정책 (실제 컨트롤러는 제각각 — 어떤 것은 CFS, 어떤 것은 RDY 를 안 올림, 어떤 것은 첫 명령에서 실패):

| 순서 | 검사 | 실패 시 | C1 케이스 |
|---|---|---|---|
| 1 | MPSMIN ≤ CC.MPS ≤ MPSMAX | `BAD_MPS` | MPS=5 > 4 |
| 2 | CC.CSS == 0(NVM) 이고 CAP.CSS bit0 | `BAD_CSS` | CSS=6, CAP.CSS=0x40 |
| 3 | AMS 0 은 항상, 1 은 CAP.AMS bit0, 7 은 CAP.AMS bit1, 나머지 reserved | `BAD_AMS` | WRR 미지원, AMS=2 |
| 4 | ASQS ≠ 0 그리고 ACQS ≠ 0 (최소 2 엔트리) | `BAD_AQA` | 0x001F0000, 0x0000001F |
| 5 | ASQ ≠ 0, CC.MPS 페이지 정렬 | `BAD_ASQ` | 0, MPS=16K 인데 4K 정렬 |
| 6 | ACQ ≠ 0, CC.MPS 페이지 정렬 | `BAD_ACQ` | +0x40 |

순서가 MPS 먼저인 이유: 정렬 검사가 MPS 로 정한 페이지 크기에 의존하기 때문이다. IOSQES/IOCQES 는 여기서 검사하지 않는다 — 스펙상 I/O 큐를 만들 때(Create I/O SQ/CQ, PCIE-M03) 의미가 생기고, Identify Controller 의 SQES/CQES 범위와 비교된다.

C2 는 bring-up 의 전형이다: ACQ 쓰기를 빼먹은 드라이버 → `C2 buggy driver (no ACQ): rc=ERR_CFS CSTS=0x00000002 bad_reason=6` (6 = `BAD_ACQ`). 같은 장치에 정상 `nvme_init` → EN=1 이 남아 있으니 disable 이 EN=0 을 쓰고, reset 이 CFS 를 지운 뒤 정상 기동한다.

### Part C — bring-up 에서 로그 보고 좁히기

| 증상 | 의미 | 먼저 볼 것 |
|---|---|---|
| CAP / CSTS = 0xFFFFFFFF | MMIO 가 UR 로 끝남: 링크 다운, BAR 미할당, Command.Memory Space=0 | `lspci -vvv` 의 Control `Mem+`, LnkSta, BAR 주소 (PCIE-B03, B06) |
| EN=1 뒤 RDY 가 TO 안에 안 옴, CSTS=0x0 | 펌웨어가 안 돎 / 부팅 중 / CAP.TO 를 너무 작게 신고 | 펌웨어 UART 로그, 전원 시퀀스, TO 값, 리셋 후 대기 |
| CSTS=0x2 (CFS) | 컨트롤러가 설정을 거부했거나 내부 assert | AQA/ASQ/ACQ/CC 값 덤프 (이 레슨의 쓰기 로그처럼), IOMMU 로 ASQ 주소가 장치에 보이나 (PCIE-M15) |
| EN=0 뒤 RDY 가 1 에 붙음 | reset 이 안 끝남 | NSSR, FLR, 전원 순서 |
| 켜지자마자 RDY=1, CFS=0 인데 Identify 가 타임아웃 | 큐 주소/MPS 불일치, MSI-X 미설정 | PCIE-M01..M03, M10 |

Linux 는 RDY 대기에 실패하면 `nvme nvme0: Device not ready; aborting initialisation, CSTS=0x...` 비슷한 줄을 남긴다 (메시지 형식은 기억 기반). 위 표의 첫 열을 이 CSTS 값으로 읽는다.

### 실제 Linux 드라이버와 비교 (기억 기반, `drivers/nvme/host/core.c` 확인 권장)

- `nvme_disable_ctrl()` 는 CC 에서 SHN·EN 을 지워 쓰고 RDY=0 을 기다린다. `nvme_enable_ctrl()` 은 MPS/AMS/SHN/IOSQES/IOCQES 를 넣은 CC 를 **EN 없이 먼저 쓰고**, CAP 을 다시 읽은 뒤 EN 을 켜서 쓴다 — 이 레슨의 ⑥⑦ 두 번 쓰기와 같은 모양.
- 대기 예산은 대략 `(CAP.TO + 1) / 2` 초, 폴링 간격은 수십~100 ms 수준. NVMe 2.0 의 CAP.CRMS 가 있으면 CRTO 레지스터(0x68, 헤더에 `NVME_REG_CRTO`)의 더 긴 타임아웃을 쓴다.
- ASQ/ACQ 는 `lo_hi_writeq` 로 lo 먼저 쓴다 (64비트 MMIO 원자성을 가정하지 않음, PCIE-B12).
- Admin 큐 깊이 `NVME_AQ_DEPTH = 32` 는 7.0 헤더에서 직접 확인한 값이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 48/48 passed, exit 0 (약 0.25 초)
cc -std=c11 -g -O1 -fsanitize=thread solution.c -o sol_tsan && ./sol_tsan       # 48/48, TSan 경고 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
```

실행 출력 (실제; `polls` 는 실행마다 조금씩 다르다 — 판정에는 쓰지 않는다):

```text
A fresh controller           rc=OK              stage=READY    polls=14  budget=400000 us  last CSTS=0x00000001  writes=7
A already enabled            rc=OK              stage=READY    polls=8   budget=400000 us  last CSTS=0x00000001  writes=8
B1 RDY never comes           rc=ERR_TO_ENABLE   stage=ENABLE   polls=274 budget=50000 us  last CSTS=0x00000000  writes=7
B2 disable stuck             rc=ERR_TO_DISABLE  stage=DISABLE  polls=218 budget=50000 us  last CSTS=0x00000001  writes=1
B3 CFS on enable             rc=ERR_CFS         stage=ENABLE   polls=12  budget=400000 us  last CSTS=0x00000002  writes=8
B4 CFS already set           rc=OK              stage=READY    polls=8   budget=400000 us  last CSTS=0x00000001  writes=8
B5 config rejections: 11 cases, bad=0
B8 surprise removal          rc=ERR_GONE        stage=ENABLE   polls=11  budget=400000 us  last CSTS=0xFFFFFFFF  writes=7
C1 fw_validate_enable: 13 cases, bad=0
C2 buggy driver (no ACQ): rc=ERR_CFS CSTS=0x00000002 bad_reason=6
C2 then correct init         rc=OK              stage=READY    polls=13  budget=400000 us  last CSTS=0x00000001  writes=13
48/48 passed
```

B1/B2 의 polls 가 수백인 것은 50 ms 예산 ÷ (50 µs sleep + 스케줄링 지연) 이라서다. 실제 드라이버는 이 간격이 100 ms 근처라 수십 번 정도.

## 7. 자주 하는 실수

**1. 타임아웃 없는 `while (!(readl(CSTS) & RDY));`.** 펌웨어가 안 돌면 probe 가 영원히 멈추고, 커널이면 부팅이 멈춘다. 고친 코드: CAP.TO 로 예산을 정하고 `NV_ERR_TO_ENABLE` 같은 **구별되는** 코드로 실패 (A2).

**2. 0xFFFFFFFF 를 CSTS 비트로 해석한다.** all-1s 는 RDY=1 이라 "준비됨" 으로 통과하거나, CFS=1 이라 reset 루프에 빠진다. 고친 코드: `if (csts == 0xFFFFFFFFu) return NV_ERR_GONE;` 을 비트 검사보다 먼저.

**3. 만료 확인을 읽기 전에 해서 마지막 기회를 버린다.** `while (now < deadline) {…} return TIMEOUT;` 은 마지막 sleep 중 올라온 RDY 를 못 본다. 고친 코드: "시간 먼저 → 읽기 → 판정 → 만료면 실패".

**4. disable 이 끝나기 전에 AQA/ASQ/ACQ 를 쓴다** (또는 EN=0 을 쓰고 바로 EN=1). mock 이 `V_ADMINQ_WHILE_EN` / `V_EN_WHILE_RDY` 로 잡는다. 실제로는 "가끔 첫 Identify 가 쓰레기를 돌려준다" 같은 재현 어려운 버그가 된다. 고친 코드: disable 의 반환값을 확인하고 실패면 중단 (B2: 쓰기 1개에서 멈춤).

**5. RDY=0 만 기다리고 reset 이 끝났다고 믿는다.** RDY 가 원래 0 이던 CFS 상태에서는 첫 폴링에 "끝" 이 된다 (연습 4 실측: polls=1 → `ERR_CFS`). 고친 코드: `(csts & (RDY | CFS)) == 0` 까지.

**6. 설정 검사를 CC 를 쓴 뒤에 한다.** 잘못된 MPS 를 이미 썼거나, 켜져 있던 장치를 disable 해 버린 뒤 "MPS 가 범위 밖" 을 발견한다. 고친 코드: `nvme_check_cfg` 를 CAP 읽기 바로 뒤, 첫 쓰기 전에 (B5: 11가지 모두 쓰기 0).

**7. AQA 에 엔트리 수를 그대로 쓴다.** 32 를 쓰면 33 엔트리가 되고, 컨트롤러는 호스트가 할당하지 않은 33번째 슬롯을 DMA 한다. 고친 코드: `(cq_depth − 1) << 16 | (sq_depth − 1)` — NVMe 의 크기 필드는 거의 전부 0-based (MQES, ASQS, ACQS, NLB...).

**8. 테스트 공유 상태를 재사용한다.** 이 레슨을 쓰면서 실제로 겪었다: `nvme_disable` 이 대기 없이 일찍 반환하면 `r.polls` 가 이전 테스트 값 그대로라, 부하가 걸렸을 때만 실패했다. 고친 코드: 테스트마다 `memset(&r, 0, sizeof r)`, 그리고 polls 같은 시간 의존 값 대신 이벤트·레지스터 상태로 판정.

## 8. 면접 Q&A

<details><summary>Q1. CC.EN 을 1 에서 0 으로 쓰면 컨트롤러에 무슨 일이 일어나나요?</summary>

Controller reset 이다. 모든 I/O SQ/CQ 가 지워지고, 처리 중이던 명령은 버려지거나 중단되며, Admin 큐의 head/tail 은 0 으로 돌아간다. 컨트롤러는 다시 켜질 준비가 되면 CSTS.RDY 를 0 으로 내린다. AQA/ASQ/ACQ 의 값 자체는 이 reset 으로 지워지지 않는 것으로 알고 있다 (스펙 확인 권장). 그래서 드라이버는 reset 뒤에 I/O 큐를 Create I/O CQ/SQ 로 **다시 만들어야** 한다 (PCIE-M03, 에러 복구 경로는 PCIE-A03). PCIe 링크나 config space 는 그대로다 — 그건 FLR 이나 hot reset 의 영역.
</details>

<details><summary>Q2. CAP.TO 의 단위와 의미는? 어느 방향 전환에 쓰나요?</summary>

비트 31:24, 단위 500 ms. "CC.EN 을 바꾼 뒤 CSTS.RDY 가 따라오기까지 호스트가 기다려야 하는 최악 시간" 으로, 0→1 과 1→0 **둘 다**에 쓴다. 0x28 이면 20 s, 0xFF 면 127.5 s. Linux 는 대략 `(TO+1)/2` 초를 기다린다 (기억 기반). NVMe 2.0 은 이게 부족한 장치를 위해 CAP.CRMS 와 CRTO 레지스터(0x68)를 추가했다. shutdown 완료(SHST) 대기에는 쓰지 않는다 — 그건 RTD3E 나 드라이버 파라미터.
</details>

<details><summary>Q3. 왜 AQA/ASQ/ACQ 를 먼저 쓰고 EN 을 마지막에 쓰나요? CC 설정과 EN 을 한 번에 써도 되나요?</summary>

컨트롤러는 EN 0→1 을 본 순간 Admin 큐 레지스터를 래치하고 검사한다(Part C). 그 전에 값이 다 들어가 있어야 한다. AMS, MPS, CSS 는 스펙상 EN 을 1 로 하기 **전에** 유효한 값이어야 한다. 한 번의 쓰기로 설정과 EN 을 같이 넣는 드라이버도 있지만, Linux 최근 버전은 EN 없이 CC 를 먼저 쓰고 CAP 을 다시 읽은 뒤 EN 을 켠다 (기억 기반) — 이 레슨도 같은 두 번 쓰기를 한다. 켜진 뒤에 CSS/MPS/AMS 를 바꾸면 안 된다 (`V_CC_CFG_WHILE_EN`).
</details>

<details><summary>Q4. CSTS 를 읽었더니 0xFFFFFFFF 입니다. 무슨 뜻이고 어떻게 처리하나요?</summary>

장치가 응답하지 않는다는 뜻이다: 링크 다운, surprise removal, BAR 미할당, Command 레지스터 Memory Space=0. MMIO 읽기가 Unsupported Request/Completion Timeout 으로 끝나면 root complex 가 all-1s 를 돌려준다 (PCIE-B03, B08). RDY/CFS 비트로 해석하기 전에 걸러서 `NV_ERR_GONE` 으로 바로 실패한다 — 타임아웃까지 기다릴 이유가 없다(B8). 그다음은 `lspci -vvv` 의 LnkSta, AER 상태(PCIE-M12), hot-plug(PCIE-A07).
</details>

<details><summary>Q5. CFS 가 떴습니다. 드라이버는 무엇을 하나요? reset 해도 안 풀리면요?</summary>

1) EN=0 으로 controller reset 하고, RDY=0 **그리고** CFS=0 이 될 때까지 CAP.TO 안에서 기다린다 — RDY 가 원래 0 이었으면 RDY 만으로는 reset 완료를 알 수 없다. 2) 다시 init (연습 3 의 재시도). 3) 안 풀리면 NVM Subsystem Reset (CAP.NSSRS=1 일 때 NSSR 에 0x4E564D65), 4) PCIe FLR 또는 링크 hot reset, 5) 전원 사이클. 그리고 원인 분석용으로 CSTS·CC·AQA/ASQ/ACQ 값과 펌웨어 로그(Telemetry Log, PCIE-A05)를 남긴다.
</details>

<details><summary>Q6. MPS 는 무엇이고, 왜 레지스터 쓰기 전에 범위를 검사하나요?</summary>

CC.MPS 는 호스트 메모리 페이지 크기 = 2^(12+MPS). PRP 엔트리 해석(PCIE-M04)과 ASQ/ACQ 정렬이 이 크기를 기준으로 한다. 컨트롤러가 지원하는 범위는 CAP.MPSMIN..MPSMAX. 범위 밖이면 컨트롤러가 거부(CFS)하거나 정의되지 않은 동작을 한다. 쓰기 전에 검사하면 실패해도 장치 상태가 그대로라 다른 설정으로 다시 시도할 수 있다 — 이미 켜져 있던 장치를 쓸데없이 reset 하지도 않는다.
</details>

<details><summary>Q7. CAP.MQES = 1023 이면 I/O 큐에 엔트리가 몇 개, 명령은 동시에 몇 개까지?</summary>

MQES 는 0-based 라 최대 1024 엔트리. 링 버퍼는 "head == tail 이면 비었음" 이라 꽉 찬 상태와 구별하려고 한 칸을 비워 두므로 동시에 최대 1023 개 (PCIE-M01). Admin 큐 깊이는 AQA 의 12비트 필드로 2..4096 — MQES 는 I/O 큐의 상한으로 정의된다 (스펙 확인 권장). 드라이버는 `min(원하는 깊이, MQES+1)` 로 잡는다 (연습 5).
</details>

<details><summary>Q8. 이 mock 에서 레지스터를 왜 _Atomic 으로 했나요? 실제 드라이버에서는요?</summary>

호스트 스레드와 컨트롤러 스레드가 같은 변수를 읽고 쓰니 평범한 변수면 데이터 레이스이고, C11 에서는 정의되지 않은 동작이다 — TSan 이 바로 보고한다. `_Atomic` 은 접근을 쪼개지지 않게 하고 스레드 사이 순서를 정해 준다. 실제 MMIO 는 C 메모리 모델 밖의 장치라 `volatile` 접근 + 아키텍처 배리어(`readl/writel`, ARM 의 `dmb/dsb`, ARCH-M12)로 같은 일을 한다. 그리고 mock 은 이벤트를 레지스터보다 먼저 기록해서 "호스트가 변화를 봤다면 로그에 있다" 를 보장한다 — 판정을 시간 대신 로그로 하기 위한 장치.
</details>

<details><summary>Q9. RDY 는 왜 인터럽트가 아니라 폴링으로 기다리나요?</summary>

이 시점에는 인터럽트를 쓸 수단이 없다. NVMe 인터럽트는 완료 큐 단위로 MSI-X 벡터에 묶이는데(PCIE-M10), RDY=1 이 되기 전에는 Admin CQ 조차 동작하지 않는다. 그리고 RDY 전환은 레지스터 상태 변화일 뿐 완료 엔트리가 아니다. 그래서 init 과 reset 경로는 폴링 + 타임아웃이 표준이다. 폴링 간격은 CPU 를 낭비하지 않을 만큼 (Linux 는 수십~100 ms 수준, 이 레슨 시뮬은 50 µs).
</details>

## 9. 추가 연습

### 연습 1 — normal shutdown 과 재사용

`nvme_shutdown(m, shn, budget_us, r)`: CC 의 SHN 만 01(normal)로 바꿔 쓰고(EN 유지) CSTS.SHST == 10b 를 예산 안에서 기다린다. EN=0 이면 쓰기 없이 OK. shutdown 뒤 같은 장치를 다시 쓰려면?

<details><summary>답</summary>

CC = 0x00460001 | 1<<14 = **0x00464001**. mock 은 SHN 을 보면 SHST=01(진행) → 0.8 ms 뒤 SHST=10(완료). 실측: `P1 normal shutdown: rc=OK polls=7 CSTS=0x00000009 last CC write=0x00464001` (0x9 = SHST 10 + RDY 1 — shutdown 은 RDY 를 내리지 않는다). 재사용하려면 **controller reset** 을 거쳐야 한다: `nvme_init` 이 EN 1→0 을 쓰고, reset 이 SHST 를 00 으로 되돌린 뒤 다시 켠다 (테스트: 재-init 후 CSTS == 0x1, 위반 0). 대기 예산은 CAP.TO 가 아니라 RTD3E(Identify Controller) 나 드라이버의 shutdown_timeout 으로 정한다 (Linux 기본 5 s — 기억 기반). shn 값이 1·2 가 아니면 `NV_ERR_PARAM`.
</details>

### 연습 2 — abrupt shutdown 과 끝나지 않는 shutdown

SHN=10(abrupt)으로 같은 함수를 부르고, `F_SHN_STUCK` 컨트롤러에서는 `NV_ERR_TO_SHN` 이 나오는지, SHST 가 어디에 멈췄는지 확인하라.

<details><summary>답</summary>

abrupt 는 CC = 0x00460001 | 2<<14 = **0x00468001**. 스턱: `P2 shutdown stuck: rc=ERR_TO_SHN polls=198 CSTS=0x00000005` — 0x5 = SHST 01(진행 중) + RDY. 의미: 펌웨어가 FTL 매핑 테이블을 NAND 에 플러시하다 멈췄다. 호스트는 기다릴 만큼 기다린 뒤 전원을 끊을 수밖에 없고, 다음 부팅에서 SSD 는 비정상 종료 복구(sudden power-off recovery)를 한다 — Don 이 SSD FW 에서 SPOR 로 다뤘던 그 경로. Linux 는 abrupt 를 "빨리 끝내야 할 때"(예: 타임아웃 뒤) 쓴다. EN=0 장치에 부르면 쓰기 0, OK.
</details>

### 연습 3 — CFS 면 다시 시도

`nvme_init_retry(m, cfg, max_tries, &tries, r)`: `NV_ERR_CFS` 일 때만 다시 init. 다른 오류는 즉시 반환.

<details><summary>답</summary>

`nvme_init` 이 CFS 를 보면 이미 reset 해 두므로 다시 부르기만 하면 된다. `F_CFS_ONCE` → tries=2, OK, EV_CFS 1회. 매번 CFS → `P3 always CFS: rc=ERR_CFS stage=ENABLE tries=3 EV_CFS=3 EV_DISABLED=3` — 세 번 다 reset 은 성공했지만 켜기는 실패. page 1 MiB(MPS 범위 밖) → tries=1, 쓰기 0: **설정 오류는 다시 해도 똑같이 실패**하므로 재시도 대상이 아니다. 재시도 횟수에 상한을 두는 것과 "어떤 오류가 일시적인가" 를 나누는 것이 핵심.
</details>

### 연습 4 — RDY 만 보는 disable 의 함정 재현

(EN=1, RDY=0, CFS=1) 컨트롤러에서, RDY 비트만 보는 대기로 reset 하고 바로 다시 켜 보라. 그다음 `nvme_init` 으로 같은 상황을 처리하라. (재현을 확실히 하려고 이 연습의 reset 지연은 100 ms.)

<details><summary>답</summary>

실측: `P4 RDY-only disable: wait0 rc=OK polls=1 -> enable rc=ERR_CFS CSTS=0x00000002` — RDY 가 원래 0 이라 첫 폴링에서 "끝" 이 되고, 컨트롤러가 reset 을 마치기 전에 다시 켰으니 CFS 가 그대로다. `nvme_init` (RDY=0 && CFS=0 대기): `P4 fixed (RDY=0 && CFS=0): rc=OK EV_DISABLED=1`. 실제 HW 에서 이 함정을 피하는 방법은 장치마다 다르다: CFS 가 풀리는 걸 확인, EN 을 바꾼 뒤 고정 지연(Linux quirk), 또는 RDY 가 1 이 된 뒤에만 EN 을 내리기.
</details>

### 연습 5 — CSTS 분류, I/O 큐 깊이, CAP.TO 실제 시간

`csts_classify(v)`: 우선순위 all-1s > CFS > SHST > RDY 로 GONE / FATAL / SHN_RSVD / SHN_DONE / SHN_BUSY / READY / NOT_READY. 하위 6비트 64개 + 0xFFFFFFFF 를 전수로 분류하고 개수를 예측하라. 그리고 `io_queue_depth(cap, want) = clamp(want, 2, MQES+1)`, `cap_to_real_ms(cap) = TO × 500`.

<details><summary>답</summary>

CFS 가 1 인 값이 64 개 중 32 → FATAL 32. 나머지 32 개를 SHST 4가지로 8 씩: 11 → RSVD 8, 10 → DONE 8, 01 → BUSY 8, 00 인 8 개 중 RDY 1/0 이 4/4. 실측: `P5 CSTS classes: GONE=1 FATAL=32 SHN_RSVD=8 SHN_DONE=8 SHN_BUSY=8 READY=4 NOT_READY=4`. (NSSRO, PP 비트는 분류에 영향 없음 — 0x21 은 READY.) 큐 깊이: MQES 0x3FF 에 4096 요청 → 1024, 1 요청 → 2, MQES 0xFFFF 에 70000 → 65536. CAP.TO: 1 → 500 ms, 0x28 → 20 000 ms, 0xFF → 127 500 ms.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 23/23 passed, 약 0.35 초
cc -std=c11 -g -O1 -fsanitize=thread practice.c -o pr_tsan && ./pr_tsan          # TSan 경고 0
```

```text
P1 normal shutdown: rc=OK polls=7 CSTS=0x00000009 last CC write=0x00464001
P2 shutdown stuck:  rc=ERR_TO_SHN polls=198 CSTS=0x00000005
P3 always CFS: rc=ERR_CFS stage=ENABLE tries=3 EV_CFS=3 EV_DISABLED=3
P4 RDY-only disable: wait0 rc=OK polls=1 -> enable rc=ERR_CFS CSTS=0x00000002
P4 fixed (RDY=0 && CFS=0): rc=OK EV_DISABLED=1
P5 CSTS classes: GONE=1 FATAL=32 SHN_RSVD=8 SHN_DONE=8 SHN_BUSY=8 READY=4 NOT_READY=4
23/23 passed
```

## 10. 핵심 정리

- 순서: CAP → **검사(쓰기 전)** → EN=0 → RDY=0 대기 → AQA, ASQ lo/hi, ACQ lo/hi → CC(EN=0) → CC|EN → RDY=1 대기. 처음 켜진 장치면 쓰기 7개, 켜져 있던 장치면 8개.
- CAP.TO = 31:24, 500 ms 단위, EN 의 **두 방향 모두**의 최악 대기. 0x28 = 20 s. shutdown 은 별도 예산.
- 폴링 루프: 시간 먼저 → 읽기 → all-1s → CFS → 성공 → 만료. 만료 뒤에도 한 번은 읽는다. 타임아웃 코드는 disable/enable 을 구별.
- 0xFFFFFFFF = 장치 없음(UR). 비트로 해석하기 전에 거른다.
- CFS → reset(EN=0) → RDY=0 **&& CFS=0** 대기 → 실패 반환/재시도. RDY 가 원래 0 이면 RDY 만으로는 reset 완료를 모른다. 안 풀리면 NSSR → FLR → 전원.
- CC 값 0x00460001 = IOCQES 4 (16 B) | IOSQES 6 (64 B) | MPS 0 (4 KiB) | CSS NVM | EN. AQA 는 0-based: 32 엔트리 → 0x001F001F. Admin 2..4096, I/O 는 MQES+1 까지.
- EN=1 동안 AQA/ASQ/ACQ·CSS/MPS/AMS 는 건드리지 않는다. 컨트롤러(SSD FW)는 EN 0→1 에서 MPS·CSS·AMS·AQA·정렬을 검사하고 거부하면 CFS.
- 2-스레드 mock: 레지스터 `_Atomic`, 로그 mutex, 이벤트를 레지스터보다 먼저 기록 → 판정은 로그와 상태로, TSan 0.

## 참고자료

- NVM Express, *NVM Express Base Specification* (2.0 이후) 및 *NVMe over PCIe Transport Specification* — Controller Registers (CAP, CC, CSTS, AQA, ASQ, ACQ, CRTO), Controller Initialization, Controller Reset, Shutdown 절: <https://nvmexpress.org/specifications/> (이 레슨의 순서 규칙·CFS 처리·Admin 큐 크기 한계는 원문 확인 권장)
- NVM Express, *NVM Express Revision 1.4* — 7.6.1 "Initialization" 의 단계 목록 (절 번호는 기억 기반).
- Linux `include/linux/nvme.h` (커널 7.0 헤더, Lima VM `/usr/src/linux-headers-7.0.0-38-generic/include/linux/nvme.h` 에서 확인) — `NVME_REG_*`, `NVME_CAP_*`, `NVME_CC_*`, `NVME_CSTS_*`, `NVME_AQ_DEPTH`, `NVME_NVM_IOSQES/IOCQES`.
- Linux `drivers/nvme/host/core.c` — `nvme_wait_ready`, `nvme_disable_ctrl`, `nvme_enable_ctrl`; `drivers/nvme/host/pci.c` — `nvme_pci_configure_admin_queue` (세부는 기억 기반, 소스 확인 권장): <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/drivers/nvme/host>
- QEMU `hw/nvme/ctrl.c` — 에뮬레이션 컨트롤러의 CC.EN 처리 (mock 설계 비교용).
- 관련 레슨: PCIE-B03 (Command.Memory Space, all-1s), PCIE-B06 (lspci), PCIE-B08 (UR → 0xFFFFFFFF), PCIE-B12 (NVMe 레지스터 디코드, 64비트 lo/hi), PCIE-M01..M03 (큐 링, Identify, I/O 큐), PCIE-M04 (PRP 와 MPS), PCIE-M10 (MSI-X), PCIE-M12 (AER), PCIE-M15 (IOMMU), PCIE-A03 (타임아웃·reset 복구), PCIE-A05 (SSD FW 로그), PCIE-A07 (surprise removal), ARCH-M12 / BM-M08 (MMIO 배리어, DMA), BM-B04 (레지스터 RMW), OS 동시성 레슨 (C11 atomics, data race).
