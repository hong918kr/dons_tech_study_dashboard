# N06G · 플랫폼 · 회귀 · 자동화 — pre-si에서 silicon까지 재사용
<!-- theme: pastel -->
> **묶음**: 방법론 (플랫폼·회귀·자동화) · **우선**: P1 · **목록**: [N06 인덱스](2026-11-01_N06_validation_test_plan_design.html) · **이전/다음**: [N06F NPU·SoC·안전](2026-11-01_N06F_plans_npu_soc_safety.html) / [N06H 디버그·triage](2026-11-01_N06H_debug_methodology_triage.html) · **관련**: [deck §5.1 Q2/Q3/Q7, §6.1, §6.4, §6.5](2026-10-25_rivian_hm_screen_prep.html), [BV14 §2.10–2.13 보드 팜·자동화·triage·ECO](2026-11-01_BV14_board_validation_plan_automation.html), [LP14 §2.9 하네스, §2.11 test of the test](2026-10-31_LP14_validation_methodology.html), [BV01 §2.1 엔진 4종, §2.9 FPGA vs 실리콘](2026-11-01_BV01_si_board_validation_big_picture.html), [V5 테스트 러너·커버리지](hmScreenPrep/problems/V5_test_runner_coverage/index.html) · **코드**: [onsitePrep/n06/g/](onsitePrep/n06/g/run.sh) ([test_runner.c](onsitePrep/n06/g/test_runner.c) · [classify_results.py](onsitePrep/n06/g/classify_results.py) · [bisect_demo.sh](onsitePrep/n06/g/bisect_demo.sh) · [run_output.txt](onsitePrep/n06/g/outputs/run_output.txt))

## 0. TL;DR

- 이 페이지로 답할 것: **"Same test on FPGA, emulator and silicon — how?"**, "회귀를 어떻게 계층화하나 (시간 예산)", "flaky 테스트 정책", "새 실패가 어느 빌드부터인지 자동으로 찾나", "FPGA는 통과, 실리콘은 실패 — 어떤 순서로 보나", "테스트 자체가 맞는지 어떻게 아나".
- **외울 것 ① 재사용 = 4개의 손잡이**: 테스트 코드는 플랫폼 상수를 하나도 모른다. **주소는 플랫폼 맵에서**, **timeout은 실리콘 기준 시간 × 플랫폼 배율**, **없는 블록은 caps로 SKIP 또는 stub(커버리지 credit 없음)**, **결과는 한 줄 계약(`RESULT <id> <STATUS> k=v…`) + mailbox + exit code**로 돌아온다.
- **외울 것 ② 회귀 4층 + 예산**: pre-merge smoke **≤15분** → nightly **≤8시간(밤 사이)** → weekly full **≤48시간(주말)** → PVT 캠페인(일–주) · ECO/stepping은 **추적성 기반 영향 범위** + nightly 전체 [예시 숫자].
- **외울 것 ③ 결과 판정 규칙**: retry PASS는 **PASS가 아니라 flaky**, 실패는 **signature로 dedup**(시드·시간·오프셋 제거), **flaky는 quarantine하되 계속 돌리고 담당·기한**, 새 결정적 실패는 **자동 bisect**(`git bisect run`: 0 good / 1 bad / 125 skip).
- **FPGA 통과·실리콘 실패 순서**: 같은 바이너리·설정인가 → **테스트가 FPGA 특성에 기댔나**(timeout·초기값·clock gate 미모델링) → 리셋 값 diff → 주파수 내려 보기 → 전압·온도 → 칩 여러 개 → 아날로그/PHY/DDR → 진짜 silicon 버그(errata).
- **Test of the test**: negative control(일부러 실패해야 하는 조건), fault injection, **mutation(체커를 일부러 망가뜨려 테스트가 잡는지)**, 체커 self-test를 **매 회귀마다** 실행 — 코드(§6)에서 실제로 "체커가 눈멀면 DMA 테스트가 PASS로 둔갑"하는 걸 재현.
- 실습: mini bare-metal 스타일 C 러너(FPGA/SILICON HAL, fake tick timeout, seed·repeat, fault injection, mutation) + Python 분류기(stable-pass/stable-fail/flaky/timeout, dedup, FPGA↔Si diff, gate) + bisect 데모 — 전부 실제 출력.
- **30초 영어 (헤드라인)**: "I write each test once against a thin HAL. The test never sees a platform constant: block addresses come from the platform's address map, timeouts are written in silicon time and scaled by the platform — an FPGA prototype runs ten to a hundred times slower — and blocks that don't exist on the FPGA, like the analog PHY or PLL, are either skipped by capability flags or served by a stub that's tagged so it never counts as coverage. Every test prints one machine-parseable result line with its seed, so the host side is identical everywhere: it parses, classifies stable versus flaky, dedups by failure signature, and bisects new failures automatically. And every run includes a negative control, so a broken checker can't produce green."

## 1. 🔰 처음 보는 사람을 위한 설명

### 1.1 비유 — "레시피 한 장으로 세 주방에서 요리하기"

- **테스트 = 레시피**. 같은 레시피를 ① 연습용 캠핑 버너(**FPGA 프로토타입** — 화력이 약해 모든 게 느림) ② 시뮬레이터 주방(**에뮬레이터** — 느리지만 냄비 안을 다 들여다볼 수 있음) ③ 진짜 식당 주방(**실리콘**)에서 쓴다.
- 레시피에 "**가스 다이얼 3번 칸에서 5분**"이라고 쓰면 버너마다 결과가 다르다 → "**물이 끓을 때까지, 단 실제 주방 기준 5분 × 이 주방의 느림 배율까지 기다림**"이라고 써야 한다 = **timeout 스케일링**.
- 레시피에 "**왼쪽에서 두 번째 서랍의 칼**"이라고 쓰면 주방마다 서랍 배치가 다르다 → "**이 주방 배치도에서 '칼' 위치**"를 찾게 한다 = **주소 맵 추상화**.
- 캠핑 버너엔 **오븐이 없다** → 오븐 단계는 "오븐 없음 → 건너뜀"(SKIP)이거나, 플라스틱 모형 오븐(stub)으로 흉내만 내되 "**이건 진짜로 구운 게 아님**" 스티커를 붙인다 = **stub은 커버리지 credit 없음**.
- 요리가 끝나면 모든 주방이 **같은 양식의 평가표 한 장**을 낸다 → 본부(호스트)는 주방이 어디든 같은 방식으로 채점 = **결과 라인 계약**.
- **맛 감별사(체커)도 검사**한다: 일부러 소금을 왕창 넣은 접시를 줬는데 "맛있다"고 하면 감별사가 고장 = **negative control / mutation**.

### 1.2 그림 — 테스트 한 벌이 4개 플랫폼을 지나 결과 DB로

```text
         ┌──────────────── 같은 테스트 소스 (C, requirement ID 태그) ────────────────┐
         │  TEST(DMA_COPY_4K)  TEST(ECC_SCRUB)  TEST(PLL_LOCK)  TEST(ETH_LINK) ...   │
         └───────────────────────────────┬──────────────────────────────────────────┘
                                         │ 오직 HAL만 호출 (mmio, poll, delay, caps, log)
         ┌──────────────┬────────────────┼─────────────────┬───────────────────────┐
         ▼              ▼                ▼                 ▼                       ▼
   RTL sim (DV 쪽)   Emulator        FPGA proto        Silicon (A0/B0)         SLT 소켓
   Hz–kHz            ~MHz            수십 MHz           GHz                     양산 tester
   주로 DV 소관      boot/FW         SW·장시간          타이밍·아날로그·PVT       짧고 수율 높은 subset
         │              │                │                 │                       │
         └──── RESULT 줄 (UART / mailbox / tohost·semihosting / GPIO pass pin) ────┘
                                         ▼
          Host: 파서 → (plat, test) 별 분류 → signature dedup → gate → bisect → 티켓
                                         ▼
          Results DB: test, req, plat, build hash, bitstream/RTL drop, chip ID, board, V/F/T, seed
```

### 1.3 용어 사전

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| pre-silicon | Pre-silicon validation | 칩이 나오기 전, RTL을 시뮬·에뮬·FPGA로 돌려 검증 | 실리콘 Day 1에 "차이만" 디버그하려면 |
| emulator | Hardware emulator (Palladium/ZeBu/Veloce) | RTL을 전용 HW에 올려 MHz로 실행, 가시성 높음 | 부팅 ROM·FW 초기 실행 |
| FPGA prototype | FPGA prototyping (HAPS, Zynq/VCU 보드 등) | RTL을 FPGA에 합성, 수십 MHz, 가시성 낮음 | 장시간 SW·실제 IO 연결 |
| HAL | Hardware Abstraction Layer | 플랫폼 차이를 숨기는 얇은 함수층 | "한 번 쓰고 어디서나"의 핵심 |
| platform descriptor | (플랫폼 기술 구조체) | 주소 맵·클럭·배율·caps·드라이버 포인터를 담은 struct | 테스트에서 상수 제거 |
| caps | Capabilities (bitmask) | 이 플랫폼에 진짜로 있는 블록 표시 | 없는 블록 테스트를 SKIP |
| stub | Stub driver / model | 없는 블록 대신 그럴듯한 값을 돌려주는 가짜 | SW 경로는 돌지만 HW 증거는 아님 |
| timeout scale | (timeout 배율) | 실리콘 기준 시간 × 플랫폼 느림 배율 × 여유 | FPGA에서 거짓 TIMEOUT 방지 |
| result line | (결과 라인 계약) | 테스트 하나당 정해진 형식의 한 줄 | 호스트 파서·DB의 입력 |
| mailbox | Results mailbox (shared memory) | 고정 주소의 결과 구조체 (magic + 카운트 + CRC) | UART가 죽어도 JTAG로 읽음 |
| seed | Random seed | 무작위 선택(크기·정렬·순서)의 출발값 | 실패를 그대로 재현 |
| regression tier | (회귀 계층) | smoke·nightly·weekly·PVT 등 시간 예산별 묶음 | 빠른 피드백 vs 넓은 커버리지 |
| flaky | Flaky test | 같은 조건에서 PASS/FAIL이 바뀜 | 진짜 실패를 묻어 버림 |
| quarantine | Quarantine | gating에서 빼되 계속 실행·추적 | 신호 오염 방지 |
| signature | Failure signature | 실패를 구분하는 안정 문자열 (가변 필드 제거) | 40건 → 티켓 1개 |
| bisect | Auto-bisect (`git bisect run`) | good/bad 사이 빌드를 이분 탐색 | 원인 빌드 자동 지목 |
| negative control | Negative control | 반드시 FAIL해야 하는 조건에서 FAIL하는지 | 못 실패하는 테스트 적발 |
| fault injection | Fault injection | 알려진 결함(비트 플립·CRC 오류)을 일부러 넣음 | 검출 능력 측정 |
| mutation | Mutation testing | 테스트/체커 코드를 일부러 틀리게 바꿔 봄 | 체커가 실제로 일하는지 |
| golden model | Golden / reference model | 기대값을 만드는 기준 구현 | golden 자체 오류도 의심해야 |
| ECO | Engineering Change Order | 설계(칩 metal / 보드) 변경 | 회귀 범위 결정 |
| SLT | System-Level Test | 양산에서 실제 보드에 꽂아 실제 SW로 테스트 | validation 테스트의 재사용 출구 |

## 2. 핵심 개념 (정확한 버전)

### 2.1 플랫폼 4종 × 무엇을 검증하나 (요약 — 상세는 deck·BV01)

엔진별 속도·가시성은 [BV01 §2.1](2026-11-01_BV01_si_board_validation_big_picture.html), FPGA가 잡는 것/못 잡는 것은 [deck §5.1 Q2·Q3](2026-10-25_rivian_hm_screen_prep.html)에 있다. 여기선 **"테스트 하나가 어느 플랫폼에서 무슨 증거가 되나"** 로 다시 자른다.

| 테스트 종류 | RTL sim | Emulator | FPGA proto | Silicon | SLT |
|---|---|---|---|---|---|
| 레지스터 reset 값·RW | ✅ (DV) | ✅ 빠른 sanity | ✅ 통합·주소 맵 | ✅ **reset tree·퓨즈 영향** | ❌ |
| 기능 normal path (DMA copy 등) | ✅ (DV) | ✅ | ✅ **장시간·랜덤 시드 대량** | ✅ 최종 증거 | 🟡 대표 1–2개 |
| 부팅 ROM·FW·드라이버 시퀀스 | ❌ 너무 느림 | ✅ | ✅ | ✅ | ✅ 부팅 자체 |
| 인터럽트·동시성·stress | 🟡 짧게 | 🟡 | ✅ 시간 많이 | ✅ 실제 속도에서 race 재노출 | 🟡 |
| 아날로그 (PLL, SerDes PHY, DDR PHY training, 온도 센서) | ❌ 모델뿐 | ❌ | ❌ 대체 IP / 없음 | ✅ **여기서 처음** | ✅ |
| 타이밍·성능 절대값 | ❌ | 🟡 cycle 비율만 | 🟡 클럭 비율이 다를 수 있음 | ✅ | 🟡 |
| PVT·margin·soak | ❌ | ❌ | ❌ | ✅ | ✅ (스크린) |

- 숫자 감각: FPGA 프로토타입은 실리콘보다 **한두 자릿수(10–100×) 느리다**; Android 부팅 ≈ 300억 사이클 → 2 GHz에서 ~15 s, 50 MHz FPGA에서 ~10 min [1]. 그래서 **timeout·polling·watchdog 값이 플랫폼마다 달라야 한다**는 게 §2.2의 출발점.
- Staff 시그널 문장: "Pre-silicon is where I prove function at scale; silicon is where I prove what pre-silicon couldn't — analog, real timing, power and margin. The same test source runs on both, so on silicon I'm only debugging the difference."

### 2.2 "테스트를 한 번 쓰고 모든 플랫폼에서" — 구현 방법

#### 2.2.1 HAL 계층 그림

```text
 ┌──────────────────────────────────────────────────────────────────────────────┐
 │ L4  Test content      tests/dma_copy.c, tests/ecc_scrub.c ...                 │
 │                        - 플랫폼 상수 0개, 주소·timeout·caps를 HAL에서만 얻음   │
 ├──────────────────────────────────────────────────────────────────────────────┤
 │ L3  Test framework    registry, setup/run/teardown, budget timeout, seed,     │
 │                        repeat, RESULT 출력, mailbox, exit code                 │
 ├──────────────────────────────────────────────────────────────────────────────┤
 │ L2  Block drivers     dma_xfer(), ecc_scrub_start(), temp_read() ...           │
 │                        - 블록당 real / stub / FPGA-대체 구현을 포인터로 선택    │
 ├──────────────────────────────────────────────────────────────────────────────┤
 │ L1  Platform HAL      mmio_rd/wr, hal_poll(addr,mask,val,si_us), delay,        │
 │                        now(), caps, irq_wait, log_putc                         │
 ├──────────────┬───────────────┬────────────────┬──────────────────────────────┤
 │ L0  plat_sim │ plat_emu      │ plat_fpga      │ plat_silicon                 │
 │  (DV TB      │ (tohost/      │ map: 0x4000_.. │ map: 0x1A00_..               │
 │   backdoor)  │  semihost)    │ slowdown 20×   │ slowdown 1×                  │
 │              │               │ caps: no PHY,  │ caps: PLL, PHY, clk gating,  │
 │              │               │  no PLL, ...   │  analog sensors              │
 └──────────────┴───────────────┴────────────────┴──────────────────────────────┘
   빌드 시 하나의 platform descriptor를 링크하거나(이미지 분리), 런타임에 chip ID/board ID로 선택(이미지 하나)
```

- **원칙**: 위층은 아래층의 *인터페이스*만 안다. 테스트 파일에 `#ifdef FPGA`가 보이면 설계가 샌 것 — 차이는 L0 descriptor 한 곳에.
- **이미지 하나 vs 플랫폼별 이미지**: 런타임 선택(chip ID 레지스터 → descriptor)은 "실리콘에서 돈 바로 그 바이너리"를 FPGA에서도 돌릴 수 있어 재현에 유리; 빌드 분리는 코드 크기·주소 링크가 다를 때 불가피 [추정 — 둘 다 흔함].

플랫폼 descriptor (코드 §6의 실제 struct):

```c
typedef struct platform {
    const char *name;
    uint32_t slowdown;      /* HW time multiplier vs silicon (FPGA ~20x slower) */
    uint32_t timeout_scale; /* silicon-us timeouts x this (slowdown x 2 margin) */
    addr_map_t map;         /* address map differs per platform                 */
    uint32_t caps;          /* CAP_PLL | CAP_ETH_PHY | CAP_CLK_GATING | ...     */
    uint32_t sysctl_ctrl_reset; /* reset value as built (FPGA init vs Si tree)  */
    int (*temp_read_mc)(const struct platform *p, int32_t *mc, int *is_stub);
} platform_t;
```

#### 2.2.2 플랫폼별 timeout 스케일

| 규칙 | 왜 | 예 (코드 §6) |
|---|---|---|
| timeout은 **시간 단위**(µs)로, **실리콘 기준**으로 쓴다 | 루프 횟수 delay는 CPU 속도에 따라 의미가 바뀜 | `hal_poll(DMA_STAT, 1, 1, 18 /*si us*/)` |
| HAL이 `× timeout_scale`을 곱한다 (= slowdown × 여유 2) | FPGA 20× 느림 → 40× | FPGA `limit_us=1200` vs Si `30` (DMA_COPY_4K) |
| **시간축이 어느 클럭에서 오나** 확인 | FPGA의 타이머가 *같이 느려진* 시스템 클럭에서 오면 tick 기반 timeout은 자동으로 맞음; 보드의 실제 오실레이터(실시간)에서 오면 스케일 필요 | descriptor에 `timebase_hz`와 "scaled/real" 표시 [추정 설계 관행] |
| 외부 실세계 상대(UART baud, I2C 센서, PHY 링크 파트너)는 **실시간** | 상대 장치는 느려지지 않음 → 그쪽 timeout은 스케일 금지 | 센서 응답 대기 vs 내부 DMA 완료 대기를 구분 |
| timeout 3층: **poll(µs–ms) < test budget(ms–s) < host watchdog(s–min)** | 안쪽이 먼저 터져야 원인이 남음; 바깥은 hang 복구용 | `POLL_TIMEOUT` vs `OVER_BUDGET` vs 호스트 power-cycle |
| 실리콘에서 **너무 짧아진 delay** 주의 | FPGA에서 "우연히 충분했던" busy-loop delay가 실리콘에서 20× 짧아져 reset 해제·PLL 안정 대기 위반 | `for(i<1000)` → `hal_delay_us(si_us)` |

- 실제 출력 (§6): 스케일을 끄면 FPGA에서 **멀쩡한 테스트 5개가 TIMEOUT** — `DMA_COPY_4K TIMEOUT ... t_us=36 limit_us=30 code=POLL_TIMEOUT` (FPGA에선 4 KB 복사에 36 µs가 걸리는데 실리콘 기준 18 µs 한도를 그대로 씀).

#### 2.2.3 주소 맵 차이

- FPGA 빌드는 블록을 **다른 base에 두거나**(파티셔닝·AXI interconnect 재구성), 일부 블록이 빠지거나, 디버그용 블록(trace, pattern gen)이 추가된다 [추정 — 흔한 패턴].
- 방법: 주소는 **단일 소스**(레지스터 기술 파일 → 생성 헤더; IP-XACT / SystemRDL 류)에서 블록 *offset*만 생성하고, **base는 플랫폼 descriptor**(또는 device tree)에서. 테스트는 `plat->map.dma + DMA_CTRL`처럼만 접근.
- 함정 탐지: 코드 §6의 가짜 버스는 현재 플랫폼 맵에 없는 주소를 `0xDEADBEEF`(decode error)로 돌려준다 — 실리콘에선 bus error/SError, FPGA에선 interconnect DECERR 응답 [추정 — 구현별]. **REG_RESET_VALUES의 chip ID 체크가 첫 줄**인 이유: 맵이 틀리면 거기서 바로 드러난다.

#### 2.2.4 FPGA에 없는 블록 — SKIP / stub / 대체 구현

| 방법 | 언제 | 결과 처리 | 코드 §6 |
|---|---|---|---|
| **caps로 SKIP** | 블록이 아예 없음 (PLL, 아날로그 PHY) | `SKIP reason=missing_block` → 커버리지 분모엔 남되 "FPGA 미지원" 표시 | `PLL_LOCK`, `ETH_PHY_LINK_UP` on FPGA |
| **stub 드라이버** | 위층 SW 흐름(드라이버·상태기계)은 돌려 봐야 함 | PASS여도 `stub=1` 태그 → **HW 증거 아님, 커버리지 credit 0** | `TEMP_SENSOR_RANGE` on FPGA (`mc=25000`) |
| **FPGA 대체 IP** | 벤더 PHY·DDR4 하드 컨트롤러로 대체 (MIPI·PCIe·LPDDR) [2] | 상위 프로토콜·드라이버는 검증, PHY 초기화·training은 실리콘 전용 테스트로 분리 | — |
| **loopback/패턴 생성기** | 외부 장치 대신 | 데이터 경로는 증명, 상대 장치 interop은 실리콘에서 | — |

- 잊기 쉬운 것: SKIP이 많은 플랫폼에서 "전부 green"은 **아무 의미가 없다** → 리포트는 `executed / skipped / stub` 를 따로 센다 (분류기의 `not-run` 행).

#### 2.2.5 결과가 호스트로 돌아오는 길

| 채널 | 어디서 | 장점 | 약점 |
|---|---|---|---|
| UART `RESULT` 줄 | 모든 플랫폼 (FPGA·실리콘·SLT) | 사람이 읽음, 파서 단순 | 느림(115200 baud ≈ 11.5 KB/s), 깨진 바이트 → 정규식 anchored + malformed 카운트 |
| **mailbox (공유 메모리)** | 고정 SRAM 주소의 `{magic, pass, fail, timeout, skip, crc}` | UART가 죽어도 JTAG로 읽음, CRC로 무결성 | 상세 로그 없음 |
| 에뮬/시뮬 종료 신호 | 특수 주소 write (RISC-V `tohost` 관례), Arm semihosting exit [추정 — 툴별] | 시뮬레이터가 pass/fail로 종료 → CI 판정 | 플랫폼 전용 |
| GPIO pass/fail 핀 + 토글 | SLT·스코프 트리거 | tester가 핀만 봄, 실패 순간 스코프 트리거 | 정보량 1비트 |
| exit code (Linux 테스트) | Linux userspace 테스트 | `git bisect run`과 바로 연결 (0/1/125) | bare-metal엔 없음 → 호스트가 만들어 줌 |

- 결과 라인 계약 (V5·BV14와 같은 정신, 이 페이지 코드의 실제 포맷):

```text
RESULT <ID> <PASS|FAIL|TIMEOUT|SKIP> plat=<P> build=<hash> iter=<i> seed=0x<s> t_us=<n> limit_us=<n> [stub=1] [code=<CODE> k=v ...]
```

- 규칙: **한 줄 = 한 판정**, 공백 없는 `k=v`, `code=`는 안정 식별자(대문자 enum), 가변 값(seed·t_us·off·got)은 별도 키로 → 파서가 signature에서 제거할 수 있게.

### 2.3 bare-metal 테스트 프레임워크 구조 (C)

```text
 main(argv/boot params) ─► platform 선택 ─► RUN 헤더 출력 (plat, build, seed, repeat, inject, scale)
   for iter in 0..repeat-1:
     for t in TESTS[]:                       (static table, 등록 순서 = 실행 순서)
        seed_t = mix(seed, iter, idx)        (같은 base seed → 플랫폼 간 같은 선택 → 비교 가능)
        dev/state reset
        caps 부족?  ── 예 ─► SKIP reason=missing_block
        setup()     ── 실패 ─► FAIL code=SETUP_FAIL (teardown 안 함: 얻은 게 없음)
        r = run()   ── 내부 hal_poll timeout ─► TIMEOUT code=POLL_TIMEOUT reg=...
        teardown()  ── PASS든 FAIL이든 **항상** (clock gate·DMA abort·IRQ mask 원복)
        elapsed > budget × scale ?  ─► TIMEOUT code=OVER_BUDGET
        printf RESULT ... ; mailbox 카운트++
   mailbox.crc = crc32(...) ; MAILBOX 줄 ; return any(FAIL/TIMEOUT) ? 1 : 0
```

| 요소 | 설계 선택 | 이유 |
|---|---|---|
| 등록 | **static const 테이블** (이 코드) 또는 링커 섹션 매크로 (Zephyr `ZTEST(suite, test)` 류) [3] | 테이블은 이식성·가독성, 링커 섹션은 파일만 추가하면 자동 등록 |
| fixture | setup / run / teardown (Zephyr는 suite setup·before·after·teardown 4단) [3] | teardown 누락 = 다음 테스트를 오염시키는 1순위 원인 |
| 판정 | 0 = PASS, 그 외 = 원인 코드 + detail 문자열 | "FAIL"만으론 dedup 불가 |
| timeout | poll / test budget / host watchdog 3층 | 원인을 남기면서 hang에서도 회복 |
| seed | 테스트별 파생 seed를 결과 줄에 출력, `--only ID --seed S --repeat 1`로 정확 재현 | 랜덤 테스트의 재현성 |
| repeat | `--repeat N` 전체 테이블 반복 (iter 번호 출력) | 간헐 실패 율 측정 · soak |
| flags | `requires` caps, (V5처럼) `SKIP_ON_FPGA`, `DESTRUCTIVE`(OTP burn) | 플랫폼·위험 관리 |
| self-test | `SELFTEST_CHECKER`를 **테이블 맨 앞**에 | 체커가 눈멀면 그 런의 모든 PASS를 의심 (§2.9) |
| fault injection | 런 전체 플래그(`--inject`) + 테스트 내부 1회 주입 | negative control 캠페인 + 상시 self-test |

핵심 코드 (발췌, 전체는 [test_runner.c](onsitePrep/n06/g/test_runner.c)):

```c
static const test_t TESTS[] = {
    { "SELFTEST_CHECKER", 0, 20, dma_setup, t_selftest, dma_teardown },
    { "REG_RESET_VALUES", 0, 10, NULL, t_reg_reset, NULL },
    { "DMA_COPY_4K", 0, 30, dma_setup, t_dma_4k, dma_teardown },
    { "ECC_SCRUB_DONE", 0, 100, NULL, t_ecc_scrub, ecc_teardown },
    { "PLL_LOCK", CAP_PLL, 200, NULL, t_pll, NULL },
    { "ETH_PHY_LINK_UP", CAP_ETH_PHY, 8000, NULL, t_eth, eth_teardown },
    /* ... */
};
/* timeouts are given in SILICON us; the HAL scales them per platform */
static int hal_poll(uint32_t addr, uint32_t mask, uint32_t want, uint32_t si_timeout_us)
{
    uint64_t deadline = g_now_ns + scaled_us(si_timeout_us) * 1000u;
    for (;;) {
        if ((mmio_read(addr) & mask) == want) return 0;
        if (g_now_ns >= deadline) return -1;
        hal_delay_ns(hw_ns(200)); /* poll interval is "hardware time" */
    }
}
```

### 2.4 회귀 계층 — 시간 예산과 gate

| 계층 | 트리거 | 내용 | 플랫폼 | 시간 예산 [예시] | gate |
|---|---|---|---|---|---|
| **pre-merge smoke** | 테스트/FW 코드 PR마다 | 빌드 + 부팅 + 레지스터 sanity + 블록별 대표 1개 + checker self-test | FPGA 1–2대 (또는 에뮬 빠른 subset) + 실리콘 보드 1대 | **≤10–15분** | 실패 시 merge 차단 |
| **nightly** | 매일 밤 최신 main | P0+P1 기능 전부, seed 다르게 N회 반복 | FPGA 풀 + 실리콘 보드 팜 | **≤8시간** (밤 사이 끝나야 아침 triage) | 새 결정적 실패 → 자동 bisect + 티켓, 아침 리포트 |
| **weekly full** | 주말 | 전체 테이블 + stress/동시성 + soak(수십 시간) + 실리콘 전 보드 | 실리콘 전부 (+ FPGA 장시간) | **≤48시간** | 주간 리포트·추세 |
| **PVT 캠페인** | 마일스톤 (A0 bring-up 후, stepping 후) | 대표 suite × V × T × corner 칩 | 실리콘 + 챔버/thermal forcer | 일–주 | margin 기준 ([BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html)) |
| **ECO / stepping / bitstream 회귀** | 칩 metal ECO, B0, 새 RTL drop, 보드 ECO | **추적성으로 영향 requirement → 테스트** + nightly 전체 | 해당 플랫폼 | 변경 크기에 비례 | 영향 범위 100% 통과 ([BV14 §2.13](2026-11-01_BV14_board_validation_plan_automation.html)) |

- 예산 맞추는 법: ① 테스트별 **런타임 예산**을 테이블에 적고 초과 시 OVER_BUDGET 경고 ② 느린 FPGA엔 **seed 수를 줄이고 보드 수를 늘림** ③ smoke는 "가장 자주 깨진 테스트 + 변경된 블록" 우선 [추정 관행] ④ 실리콘 보드 시간은 비싸니 FPGA로 가능한 건 FPGA로.
- **계층마다 다른 질문**: smoke = "방금 망가뜨렸나", nightly = "어제보다 나빠졌나", weekly = "드물게 터지는 게 있나", PVT = "얼마나 여유 있나", ECO = "바꾼 것이 무엇을 건드렸나".

### 2.5 flaky 정책 (BV14 §2.11을 블록 무관으로 일반화)

- 왜 엄격해야 하나: Google CI에서 **pass→fail 전환의 약 84%가 flaky 테스트와 관련**, 테스트 **7개 중 1개 이상**이 가끔 코드 변경과 무관하게 실패 [4] — flaky를 방치하면 엔지니어가 빨간불을 무시하게 된다.
- 분류 규칙 (코드 §6 `classify_results.py`가 그대로 구현):

| 관측 (같은 plat × test, 여러 런) | 분류 | 처리 |
|---|---|---|
| 실행분 전부 PASS | stable-pass | rule of three: n회 무실패 → 실패율 95% 상한 ≈ 3/n (n=12면 25% — **12번 통과는 거의 증거가 안 됨**) |
| PASS + FAIL/TIMEOUT 섞임 | **flaky** | PASS로 합치지 않음, 실패율 k/n 기록, quarantine(비-gating) + 담당·기한 + loop로 율 측정 |
| 실행분 전부 TIMEOUT | timeout | hang/대기 조건 문제 — 증거 수집(레지스터 덤프) 우선 |
| PASS 0, FAIL 포함 | stable-fail | 결정적 → 자동 bisect, 티켓 |
| 전부 SKIP | not-run | 커버리지 구멍으로 리포트 |

- 정책 6줄:
  1. retry는 허용, **모든 attempt 기록**, retry 통과는 FLAKY.
  2. flaky는 **gate에서 빼되 계속 실행**(quarantine) — 안 돌리면 고쳐졌는지도 모른다.
  3. 담당·기한 없는 quarantine은 "영구 무시"와 같다 → 주간 리포트에 나이(age) 표시.
  4. **보드 flaky vs 테스트 flaky 구분**: 특정 보드에만 몰리면 보드 health 문제 ([BV14 §2.10–2.11](2026-11-01_BV14_board_validation_plan_automation.html)), 모든 보드에 낮은 율이면 테스트·FW·HW 타이밍.
  5. 실리콘에서 flaky는 **진짜 HW margin 문제일 수 있다** — "테스트가 불안정하다"로 덮지 말고 V/T 상관부터 본다 (§3의 ETH 예).
  6. seed로 재현되는 flaky는 사실 결정적 버그 — seed를 결과에 남기면 flaky의 상당수가 "재현 가능"으로 바뀐다 (§6 실제: 실패 seed로 3/3 재현). 단 **아날로그 랜덤성은 seed로 재현되지 않는다**(mock에선 모델 RNG를 seed에서 파생해서 재현될 뿐) [주의].

### 2.6 자동 bisect

- `git bisect run <script>` 계약 [5]: exit **0 = good**, **1–127(125 제외) = bad**, **125 = skip**(이 빌드는 테스트 불가 — 빌드 실패·이미지 없음), **>127 = bisect 중단**. 그래서 러너/호스트 스크립트는 "**인프라 고장 → 125 또는 중단**, DUT 실패 → 1"을 구분해야 한다 (보드가 죽었는데 1을 돌려주면 bisect가 엉뚱한 커밋을 지목).
- 무엇을 bisect하나: FW/테스트 빌드 (git) · **FPGA bitstream / RTL drop 번호** · 툴체인 버전 · 보드 rev(2×2 실험) — 축마다 "good/bad 두 점 + 단조성 가정".
- **flaky 실패의 bisect**: 한 단계에서 n회 반복해 "한 번이라도 실패하면 bad". 놓칠 확률 = (1−p)^n, 95%로 잡으려면 n ≥ ln 0.05 / ln(1−p):

| 실패율 p | 단계당 반복 n (95% 검출) | 16 빌드 bisect 총 런 (≈4 단계) |
|---|---|---|
| 42% (§6 ETH) | 6 | ~24 |
| 25% | 11 | ~44 |
| 10% | 29 | ~116 |
| 1% | 299 | ~1,200 → **먼저 stress로 율을 올린다** |

- skip의 대가 (§6 실제 출력): 빌드 10이 안 빌드되고 원인이 빌드 11이면 git은 **"could be any of: build 10, build 11"**에서 멈춘다 — 빌드 깨짐을 오래 방치하면 bisect 해상도가 떨어진다.
- Staff 문장: "Bisect is only as good as its verdicts — infrastructure failures must map to skip or abort, never to bad, and intermittent failures need enough repeats per step that 'good' actually means good."

### 2.7 실패 signature dedup · 헬스 체크

- **signature = 안정 필드만**: `test | status | code | reg | exp` — 시드·시간·오프셋·측정값(`off=443`, `got=0x31`)은 제거. 실제 출력: **29건 실패 관측 → 3개 signature(티켓)**.

```text
[12x] REG_RESET_VALUES|FAIL|code=RESET_VAL|reg=SYS_CTRL|exp=0x0
[12x] ECC_SCRUB_DONE|TIMEOUT|code=POLL_TIMEOUT|reg=ECC_STAT
[ 5x] ETH_PHY_LINK_UP|TIMEOUT|code=POLL_TIMEOUT|reg=ETH_STAT
```

- 너무 거칠면(`test|FAIL`) 다른 버그가 합쳐지고, 너무 세밀하면(`off=` 포함) 같은 버그가 수백 티켓 → **필드 등급을 결과 라인 설계 때 정한다**(§2.2.5).
- 같은 signature라도 **flaky와 결정적 FAIL은 다른 티켓** (BV14 §6의 B04 vs B08 사례).
- 헬스 체크: "**망가진 보드가 테스트 실패처럼 보이면 안 된다**" — 잡 시작 전 부팅 → READY → chip ID → rail, 2회 실패 시 quarantine, 인프라 실패(콘솔·PDU·프로브)는 DUT 실패와 따로 집계. 팜 설계·재현 패키지·DB 스키마는 [BV14 §2.10–2.11](2026-11-01_BV14_board_validation_plan_automation.html), 하네스 원칙은 [LP14 §2.9](2026-10-31_LP14_validation_methodology.html) — 여기서 반복하지 않음.

### 2.8 FPGA vs silicon 차이 표 + "FPGA는 통과, 실리콘은 실패" 사고 순서

전체 표는 [BV01 §2.9](2026-11-01_BV01_si_board_validation_big_picture.html). 테스트 설계 관점으로 압축:

| 축 | FPGA에서 | 실리콘에서 | 테스트가 미리 할 일 |
|---|---|---|---|
| 타이밍·속도 | 10–100× 느림, 블록 간 클럭 비율이 다를 수 있음 [1] | 실제 속도 → SW race·CDC 문제 재노출 | 시간 단위 timeout, 비율 가정 금지 |
| 아날로그 | PHY·PLL·DDR PHY·센서 없음/대체 [2] | 실제 아날로그, 캘리브레이션, training | caps로 분리된 실리콘 전용 테스트 |
| 메모리 모델 | FPGA BRAM/DDR4 하드 IP, **초기값이 0일 때가 많음** [추정] | 실제 SRAM/DRAM, 초기값 랜덤, ECC 초기화 필요 | 초기화 안 된 메모리에 기대지 않기, ECC 초기화 명시 |
| 리셋 | 이상적, FPGA 초기화 값(GSR) | reset tree 순서·타이밍, 퓨즈 영향 | 리셋 값 테스트를 양쪽에서, diff 리포트 |
| 초기값·퓨즈 | 개발 모드 고정 | 퓨즈 상태에 따라 기능·보안 다름 | 퓨즈 상태를 결과에 기록 |
| clock gating·power | 보통 모델 안 됨 (항상 켜짐) | 실제로 끊김 → enable 누락이 드러남 | setup에서 clock/power enable 명시, teardown에서 원복 |
| 환경 | 실온·고정 전압 | PVT 전 범위 | margin 테스트는 실리콘 신규 |

**사고 순서** (deck §6.4 확장 — 싼 것·자주 맞는 것부터):

1. **정말 같은 테스트인가**: 바이너리 hash, 설정, seed, 테스트 버전, 플랫폼 descriptor가 같나 — 결과 라인의 `build=`·`seed=`로 바로 확인.
2. **테스트가 FPGA 특성에 기댔나** (제일 흔함 [추정]): timeout/delay가 FPGA 속도에 맞춰져 있음, 초기화 안 된 메모리가 FPGA에선 0, **clock gate/power domain을 FPGA가 무시**해서 enable 누락이 숨음 — §6의 `ECC_SCRUB_DONE`이 정확히 이것.
3. **리셋·초기값 diff**: 같은 레지스터 덤프를 FPGA와 실리콘에서 떠서 diff — §6의 `REG_RESET_VALUES` (`SYS_CTRL` reset 0x1 vs spec 0x0).
4. **주파수 의존**: 실리콘 클럭을 내려 본다 → 통과하면 타이밍·CDC 또는 속도로 드러난 SW race; 그대로면 기능/아날로그 쪽.
5. **전압·온도 의존**: V 올려/내려, 온도 바꿔 → 의존하면 margin 문제 → shmoo ([BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html)).
6. **칩 의존**: 여러 칩·corner(SS/FF) → 한 칩만이면 개체 결함, 전부면 설계.
7. **아날로그/PHY/DDR 경로**: FPGA에 애초에 없던 경로인가 — 그렇다면 "FPGA 통과"는 증거가 아니었다.
8. **진짜 silicon 버그**: 최소 재현 + 파형/덤프 → 설계팀, errata + workaround (→ [N06H](2026-11-01_N06H_debug_methodology_triage.html)).

- 분류기 출력의 "FPGA stable-pass but SILICON not" 목록이 이 순서의 **입력 큐**다.

### 2.9 Test of the test — negative control · 주입 · mutation · golden 신뢰

| 기법 | 무엇 | 실리콘 검증에서의 형태 | 이 페이지 코드 |
|---|---|---|---|
| **negative control** | 반드시 FAIL해야 하는 조건에서 FAIL하나 | 틀린 기대값, 없는 장치, 링크 강제 down | `--inject dma_bitflip` → DMA 테스트 2/2 **CAUGHT** |
| **fault injection** | 알려진 결함을 넣고 검출 확인 | ECC 오류 주입 레지스터, CRC 깨뜨리기, PCIe AER 주입, 전압 droop | 런 전체 bit flip + `SELFTEST_CHECKER`의 1회 주입 |
| **mutation** | 테스트/체커 코드를 일부러 틀리게 바꿈 → 테스트가 그걸 "죽여야" 함 (mutation score = 죽인 mutant / 전체) [6] | 비교 생략, `>`↔`>=`, 레지스터 read를 캐시값으로 바꿈 | `--mutate skip_compare` → DMA 테스트가 **PASS로 둔갑**, 그러나 `SELFTEST_CHECKER FAIL code=CHECKER_BLIND`가 잡음 |
| **checker self-test 상시 실행** | 모든 회귀 런 첫 테스트로 체커가 살아 있음을 증명 | 매 nightly 첫 줄 | 테이블 맨 앞 |
| **golden 모델 신뢰** | 기대값 생성기 자체가 틀릴 수 있음 | 독립 구현 2개 교차, known-answer vector(KAT), FPGA·이전 칩·시뮬과 3-way 비교 | (N06F NPU golden 참고) |
| **트리거가 실제로 효과가 있었나** | "리셋했다"가 진짜 리셋됐나 | 상태 관측(LTSSM, reset reason) | [LP14 §2.11](2026-10-31_LP14_validation_methodology.html)의 QEMU Link Disable 사례 |
| **측정 독립성** | SW 판정 vs 외부 계측 | analyzer/scope로 교차 | — |

- 핵심 문장 (deck Q7과 같은 결): "A test that has never failed hasn't been tested." — 여기에 **"그리고 매 런마다 증명한다"**를 더하는 게 Staff 수준: 한 번 한 negative control은 나중 리팩터링으로 체커가 죽어도 모른다.

## 3. 실전 적용 — 예시: 테스트 하나(`ECC_SCRUB_DONE`)가 FPGA와 실리콘을 지나가는 길

**상황** `[예시]` `[추정 — Rivian 실제 IP·이름 아님]`: 자동차 AI SoC의 on-chip SRAM에 **ECC scrub 엔진**(백그라운드로 메모리를 읽어 1-bit 오류를 정정·기록)이 있다. ISO 26262 관점에서 "scrub이 주기적으로 끝까지 돈다"는 안전 메커니즘 요구사항(예: REQ-SRAM-ECC-007 `[예시]`)이다.

**① pre-si (FPGA, RTL drop R42 `[예시]`)**

- 테스트: scrub start → `ECC_STAT.done` poll (실리콘 기준 50 µs, FPGA ×40 = 2 ms 한도) → teardown에서 stop.
- FPGA 결과 (§6 실제): `RESULT ECC_SCRUB_DONE PASS plat=FPGA ... t_us=408 limit_us=4000` — 12/12 stable-pass.
- 숨은 사실: 테스트 setup이 **ECC 블록 clock enable을 켜지 않았다**. FPGA는 clock gate를 모델링하지 않아서(caps에 `CAP_CLK_GATING` 없음) 아무 문제가 없었다.

**② 실리콘 A0 Day 3**

- 같은 소스, 같은 seed 체계: `RESULT ECC_SCRUB_DONE TIMEOUT plat=SILICON ... t_us=50 limit_us=100 code=POLL_TIMEOUT reg=ECC_STAT clken=0x0` — 12/12 timeout.
- 분류기: `ECC_SCRUB_DONE FPGA=stable-pass SILICON=timeout` → §2.8 큐에 올라감, signature `ECC_SCRUB_DONE|TIMEOUT|code=POLL_TIMEOUT|reg=ECC_STAT` 티켓 1개 (12건 → 1).

**③ §2.8 순서대로**

1. 같은 테스트? `build=g1a2b3c` 동일, seed 체계 동일 → ✅.
2. FPGA 특성 의존? 결과 줄에 이미 `clken=0x0`가 찍혀 있다(실패 시 관련 상태를 detail에 넣어 둔 덕) → clock enable 누락 가설 → setup에 `CLKEN_ECC` 추가 후 재실행으로 확인 (mock에선 1줄 수정) — **HW 버그가 아니라 테스트 버그**.
3. 회귀 고정: "clock enable 없이 scrub start → done이 안 와야 한다"를 **negative 테스트로 추가**(clock gate가 실제로 동작하는지의 증거도 됨), 그리고 FPGA descriptor에 "clock gating 미모델링"을 문서화해 같은 유형을 리뷰 체크리스트에 넣는다.

**④ 같은 런의 다른 두 줄도 같은 방식으로**

- `REG_RESET_VALUES FAIL reg=SYS_CTRL exp=0x0 got=0x1` (실리콘만) → §2.8 단계 3: spec과 실리콘 reset 값 불일치. 설계팀에 확인 → spec 오류면 spec·테스트 수정, 실리콘 오류면 errata + FW workaround(부팅 시 명시적 write) [예시].
- `ETH_PHY_LINK_UP` 7 PASS / 5 TIMEOUT (실리콘만, FPGA엔 PHY 없음) → flaky → quarantine + loop로 율 측정 → V/T/보드 상관 → 링크 업 시간 분포(실제 출력: PASS 7건 2.38–4.87 ms, 나머지 5건은 5 ms 한도 초과 TIMEOUT)를 spec 한도와 비교: **테스트 한도가 spec보다 빡빡한지, PHY가 spec보다 느린지**를 가른다. 실패 seed로 3/3 재현(mock 한정).

**⑤ 무엇이 재사용됐나**: 테스트 소스·결과 포맷·파서·분류·dedup·티켓 경로는 **전부 그대로**; 바뀐 건 platform descriptor 한 개. 이것이 "on silicon I'm only debugging the difference"의 실체.

## 4. 꼬리 질문 체인

### 체인 A — "How do you make the same test run on FPGA, emulator and silicon?"

Q → 꼬리1 "What about blocks that don't exist on the FPGA?" → 꼬리2 "How do results get back to the host?" → 꼬리3 "Single image or one per platform?"

<details><summary>A-Q. Same test on FPGA, emulator and silicon — how?</summary>

- **EN**: "Tests are written against a thin HAL and never contain a platform constant. A platform descriptor holds the address map, the clock and slowdown factor, a capability mask, and driver pointers. Timeouts are written in silicon time and the HAL multiplies them — on a prototype that's ten to a hundred times slower, so I scale by the slowdown with a two-times margin. Anything that talks to the real world, like a sensor or a link partner, keeps real-time timeouts. If I see an ifdef FPGA inside a test, the abstraction has leaked."
- 한국어 핵심: 플랫폼 상수 0개 · descriptor(맵·배율·caps·드라이버) · 실리콘 시간 × 배율 · 외부 상대는 실시간.

</details>

<details><summary>A-꼬리1. What about blocks that don't exist on the FPGA?</summary>

- **EN**: "Three options. If the block simply isn't there — a PLL or an analog PHY — the capability mask makes the test report SKIP with a reason, so it stays visible as a coverage gap instead of disappearing. If I still want the software path exercised, I use a stub driver, but the result is tagged stub so it gets no coverage credit. And where the prototype substitutes a vendor PHY or a DDR4 hard controller, I validate the protocol and driver on the FPGA and keep PHY init and training as silicon-only tests."
- 한국어 핵심: SKIP(이유 포함) / stub(credit 0) / 대체 IP는 상위만, PHY·training은 실리콘 전용.

</details>

<details><summary>A-꼬리2. How do results get back to the host?</summary>

- **EN**: "One machine-parseable line per test over the UART, with the test ID, status, platform, build hash, seed, elapsed time and a stable failure code — so the host parser is identical everywhere. In parallel the runner fills a results mailbox in SRAM with a magic number, counters and a CRC, which the host can read over JTAG if the console dies. On emulators the runner ends with the simulator's exit convention, and on SLT a GPIO pass pin. The host converts all of it into an exit code, which plugs straight into CI and git bisect."
- 한국어 핵심: UART 한 줄 계약 + mailbox(JTAG) + 에뮬 종료 규약 + GPIO 핀 → exit code.

</details>

<details><summary>A-꼬리3. Single image or one per platform?</summary>

- **EN**: "I prefer one image that selects its descriptor at runtime from the chip ID, because then the exact binary that failed on silicon can be rerun on the FPGA, which removes one variable from triage. When link addresses or memory sizes differ too much, I build per platform from the same source and record the build hash and platform in every result line."
- 한국어 핵심: 가능하면 chip ID로 런타임 선택 = 같은 바이너리로 비교; 아니면 소스 동일 + hash 기록.

</details>

### 체인 B — "How do you structure regressions?"

Q → 꼬리1 "Nightly is taking 14 hours. What do you cut?" → 꼬리2 "A chip ECO is coming — what reruns?"

<details><summary>B-Q. How do you structure regressions?</summary>

- **EN**: "By the question each tier answers and its time budget. A pre-merge smoke in about ten to fifteen minutes answers 'did I just break it' — boot, register sanity, one test per block and the checker self-test. Nightly, which has to finish overnight, runs all P0 and P1 functional tests with fresh seeds on the FPGA pool and the silicon farm, and new deterministic failures are auto-bisected before the morning. Weekly runs everything plus stress and soak, and PVT campaigns run at milestones across voltage, temperature and corner parts."
- 한국어 핵심: 계층 = 질문 + 예산 (smoke 15분 · nightly 밤 · weekly 48 h · PVT 마일스톤).

</details>

<details><summary>B-꼬리1. Nightly is taking 14 hours. What do you cut?</summary>

- **EN**: "First I look at the runtime per test, because usually a few tests dominate — long soaks that belong in weekly, or FPGA runs with too many seeds. I move soaks to weekly, reduce seeds per FPGA and add boards instead, and run silicon-only content on silicon. I don't drop coverage silently: anything that moves out of nightly is recorded in the plan with its new cadence, so the traceability still shows when each requirement was last proven."
- 한국어 핵심: 테스트별 런타임 → soak는 weekly, seed↓ 보드↑, 이동은 계획에 기록.

</details>

<details><summary>B-꼬리2. A chip ECO is coming — what reruns?</summary>

- **EN**: "The ECO description tells me which blocks and signals changed; traceability maps those to requirements and from there to tests. I run that impacted set fully — all conditions, several parts — plus the normal nightly to catch unexpected side effects, and I compare against the previous stepping's results using the same build so the only variable is the silicon."
- 한국어 핵심: 변경 → requirement → 테스트 전부 + nightly, 같은 빌드로 이전 stepping과 비교.

</details>

### 체인 C — "A test passes on FPGA but fails on silicon."

Q → 꼬리1 "How do you know it's not a silicon bug?" → 꼬리2 "How do you stop this class from recurring?"

<details><summary>C-Q. Passes on FPGA, fails on silicon — walk me through it.</summary>

- **EN**: "First I confirm it's really the same test — same binary hash, configuration and seed. Then I check the cheapest and most common cause: the test relying on prototype behavior — timeouts tuned to the slow clock, memory that happened to be zero, or a clock gate the FPGA doesn't model. Then I diff register dumps between the two, lower the silicon frequency, move voltage and temperature, and try several parts. Each step splits the space: frequency dependence points at timing or a race, voltage at margin, one chip at a defect, all chips at design. Only after that do I call it a silicon bug."
- 한국어 핵심: 같은 테스트? → FPGA 의존(timeout·초기값·clock gate) → 덤프 diff → 주파수 → V/T → 칩 → silicon.

</details>

<details><summary>C-꼬리1. How do you know it's not a silicon bug?</summary>

- **EN**: "I need a fix in the test or software that makes it pass on every part and every condition, plus an explanation consistent with the spec. In my mock, the ECC scrub timeout printed the clock-enable register in its failure line — zero — and enabling the clock in setup fixed it on silicon. That's a test bug that the FPGA masked. If instead the behavior contradicts the spec with correct programming, I build a minimal reproduction and take it to design."
- 한국어 핵심: 스펙과 맞는 설명 + 수정으로 전 칩·전 조건 통과 = 테스트/SW 버그; 아니면 최소 재현 → 설계.

</details>

<details><summary>C-꼬리2. How do you stop this class from recurring?</summary>

- **EN**: "Three things. I add a negative test that proves the clock gate really gates, so the behavior is checked rather than assumed. I document in the FPGA platform descriptor what it doesn't model — clock gating, analog, memory init — and add those to the test review checklist. And I make failure lines carry the relevant state, like the enable register, because that turned a debug session into a one-line diagnosis."
- 한국어 핵심: negative 테스트 추가 · FPGA 미모델 목록을 체크리스트로 · 실패 줄에 관련 상태.

</details>

### 체인 D — "How do you know your test itself is correct?"

Q → 꼬리1 "You ran a negative control once. Is that enough?" → 꼬리2 "What about the golden model?"

<details><summary>D-Q. How do you know the test is correct?</summary>

- **EN**: "I prove it can fail. A negative control runs under a condition that must fail — I inject a single bit flip into a DMA copy and check the comparison catches it. I also mutate the test itself, for example skipping the compare, and confirm something goes red. In my runner, with the mutated checker the DMA tests passed despite the injected corruption, which is exactly the false green you're worried about — but the built-in checker self-test failed with 'checker blind', so the run was flagged."
- 한국어 핵심: 실패할 수 있음을 증명 — 주입 + mutation, 실제로 mutation 시 PASS 둔갑을 self-test가 잡음.

</details>

<details><summary>D-꼬리1. You ran a negative control once. Is that enough?</summary>

- **EN**: "No — a checker that worked last month can be broken by a refactor today. So the self-test is the first entry in the test table and runs in every regression, and a failing self-test marks every pass in that run as suspect. For bigger changes to the framework I rerun the full fault-injection campaign and check the detection rate."
- 한국어 핵심: 한 번이 아니라 매 런 — self-test 실패 시 그 런의 PASS 전부 의심.

</details>

<details><summary>D-꼬리2. What about the golden model?</summary>

- **EN**: "The golden model is code too, so I don't trust a single source. I cross-check against an independent implementation or known-answer vectors, compare three ways when I can — golden, FPGA and previous silicon — and when they disagree I investigate the reference as seriously as the hardware."
- 한국어 핵심: golden도 의심 — 독립 구현·KAT·3-way 비교.

</details>

### 체인 E — "A new failure appeared in last night's run. What happens automatically?"

Q → 꼬리1 "It only fails one run in ten." → 꼬리2 "The bisect pointed at a commit that can't be right."

<details><summary>E-Q. What happens automatically on a new failure?</summary>

- **EN**: "The parser classifies it — stable fail, flaky or timeout — and computes a signature with the volatile fields removed, so it checks whether this is a known ticket. If the signature is new and deterministic, the host starts git bisect run between the last good and the current build, with the board health check mapped to 'skip' so infrastructure problems never count as 'bad'. The morning report has the first bad commit, a repro command with the seed, and the logs."
- 한국어 핵심: 분류 → signature 비교 → 새 결정적 실패면 bisect(인프라 = skip) → 아침에 커밋·재현 명령.

</details>

<details><summary>E-꼬리1. It only fails one run in ten.</summary>

- **EN**: "Then each bisect step repeats the test until a miss is unlikely: at a ten percent failure rate, twenty-nine clean runs give ninety-five percent confidence that a build is good, so a sixteen-build bisect costs around a hundred runs. If the rate is one percent, that's three hundred per step, so I first stress the suspected axis to raise the rate."
- 한국어 핵심: n ≥ ln0.05/ln(1−p) — 10%면 29회/단계, 1%면 먼저 율을 올린다.

</details>

<details><summary>E-꼬리2. The bisect pointed at a commit that can't be right.</summary>

- **EN**: "Usually a verdict was wrong: a flaky 'good' because of too few repeats, or an infrastructure failure reported as 'bad'. I recheck the verdicts around the boundary with more repeats, and confirm by reverting the suspected commit on top of the current build. Unbuildable commits next to the culprit also leave an ambiguous range, which is another reason to keep the build green."
- 한국어 핵심: 판정 오류(반복 부족·인프라=bad) 의심 → 경계 재검 + revert로 확인, skip 인접 시 범위 모호.

</details>

## 5. Don 경험 연결

- "At Solidigm I brought up Cortex-R8, R82 and M0+ subsystems and verified I2C, SPI, DMA, PCIe and SRAM/DRAM on FPGA prototypes before RTL freeze, and the tests were written so they carried over to silicon — `[숫자 채우기: 블록 수 / 테스트 수 / RTL freeze 전 버그 수]`."
- "At SK hynix I built the chamber test platform SDK and UART automation that scheduled and analyzed reliability test cases across `[숫자 채우기: chamber 수 / 동시 DUT 수]`, with a `[숫자 채우기: 회귀 주기]` regression cadence — the same parse-classify-report loop I sketched here."
- "My habit from that work is that every failure line carries enough state to diagnose it without rerunning, and that a test isn't trusted until I've seen it fail."

## 6. 실습 / 코드

### 6.1 파일

| 파일 | 내용 |
|---|---|
| [test_runner.c](onsitePrep/n06/g/test_runner.c) | 미니 bare-metal 스타일 러너: static 테이블 9개 테스트, setup/run/teardown, fake tick(`g_now_ns`, 디바이스 모델만 시간 진행), `hal_poll` 실리콘 µs × 배율, FPGA/SILICON descriptor(주소 맵·slowdown·caps·reset 값·temp 드라이버 포인터), seed·repeat, `--inject dma_bitflip`, `--mutate skip_compare`, `--no-timeout-scale`, `--only`, mailbox+CRC, exit 0/1 |
| [classify_results.py](onsitePrep/n06/g/classify_results.py) | stdlib 파서: anchored 정규식, malformed 카운트, (plat,test)별 stable-pass/stable-fail/flaky/timeout/not-run, rule-of-three 상한, stub 표시, signature dedup + 재현 명령, FPGA↔Si diff 큐, gate, `--negctrl` 검사 |
| [bisect_demo.sh](onsitePrep/n06/g/bisect_demo.sh) | 임시 git repo 16 빌드, 빌드 11 회귀·빌드 10 빌드 불가 → `git bisect run` 0/1/125 |
| [run.sh](onsitePrep/n06/g/run.sh) | 빌드(ASan+UBSan, `-Werror`) → 3 nights × 2 플랫폼 × repeat 4 → negative control 2종 → 스케일 끈 FPGA → 분류 → flaky seed 재현 → bisect. 출력은 [run_output.txt](onsitePrep/n06/g/outputs/run_output.txt) |

- mock의 숫자(FPGA slowdown 20×, DMA 4 B/ns, PHY 링크 2–6 ms, 레지스터 이름·주소)는 전부 **이 mock을 위해 만든 값**이다 — 실제 IP 아님 `[예시]`.

### 6.2 실제 출력 (macOS, 2026-11-01, `./run.sh`) [확인됨]

**① 6개 nightly 로그 (mailbox 줄)**

```text
[fpga night1] exit=0 MAILBOX magic=0x52534c54 pass=28 fail=0 timeout=0 skip=8 crc=0x5df5df45
[silicon night1] exit=1 MAILBOX magic=0x52534c54 pass=26 fail=4 timeout=6 skip=0 crc=0x0eb9acb6
[fpga night2] exit=0 MAILBOX magic=0x52534c54 pass=28 fail=0 timeout=0 skip=8 crc=0x5df5df45
[silicon night2] exit=1 MAILBOX magic=0x52534c54 pass=27 fail=4 timeout=5 skip=0 crc=0x2e5e3ac4
[fpga night3] exit=0 MAILBOX magic=0x52534c54 pass=28 fail=0 timeout=0 skip=8 crc=0x5df5df45
[silicon night3] exit=1 MAILBOX magic=0x52534c54 pass=26 fail=4 timeout=6 skip=0 crc=0x0eb9acb6
```

- 읽는 법: FPGA는 매일 밤 **green(exit 0)** 이지만 skip=8 (PLL·PHY × 4 repeat) — "green인데 8개는 안 돌았다". 실리콘은 매일 밤 fail 4(REG_RESET × 4) + timeout 5–6(ECC × 4 + ETH 1–2). mailbox만 봐도 UART 없이 요약을 알 수 있다.

**② 분류 (216 RESULT 줄)**

```text
== 216 RESULT lines from 6 logs, malformed=0
PLAT     TEST               CLASS          n pass fail tmo skip  note
FPGA     SELFTEST_CHECKER   stable-pass   12   12    0   0    0  95% UB fail rate <= 3/12 = 0.250
FPGA     REG_RESET_VALUES   stable-pass   12   12    0   0    0  95% UB fail rate <= 3/12 = 0.250
FPGA     REG_RW_WALK        stable-pass   12   12    0   0    0  95% UB fail rate <= 3/12 = 0.250
FPGA     DMA_COPY_4K        stable-pass   12   12    0   0    0  95% UB fail rate <= 3/12 = 0.250
FPGA     DMA_RANDOM_SIZES   stable-pass   12   12    0   0    0  95% UB fail rate <= 3/12 = 0.250
FPGA     ECC_SCRUB_DONE     stable-pass   12   12    0   0    0  95% UB fail rate <= 3/12 = 0.250
FPGA     PLL_LOCK           not-run       12    0    0   0   12  missing_block
FPGA     ETH_PHY_LINK_UP    not-run       12    0    0   0   12  missing_block
FPGA     TEMP_SENSOR_RANGE  stable-pass   12   12    0   0    0  95% UB fail rate <= 3/12 = 0.250 [stub driver: no coverage credit]
SILICON  SELFTEST_CHECKER   stable-pass   12   12    0   0    0  95% UB fail rate <= 3/12 = 0.250
SILICON  REG_RESET_VALUES   stable-fail   12    0   12   0    0  
SILICON  REG_RW_WALK        stable-pass   12   12    0   0    0  95% UB fail rate <= 3/12 = 0.250
SILICON  DMA_COPY_4K        stable-pass   12   12    0   0    0  95% UB fail rate <= 3/12 = 0.250
SILICON  DMA_RANDOM_SIZES   stable-pass   12   12    0   0    0  95% UB fail rate <= 3/12 = 0.250
SILICON  ECC_SCRUB_DONE     timeout       12    0    0  12    0  
SILICON  PLL_LOCK           stable-pass   12   12    0   0    0  95% UB fail rate <= 3/12 = 0.250
SILICON  ETH_PHY_LINK_UP    flaky         12    7    0   5    0  fail rate 5/12 = 42% (NOT a pass; gate=quarantine+ticket)
SILICON  TEMP_SENSOR_RANGE  stable-pass   12   12    0   0    0  95% UB fail rate <= 3/12 = 0.250

== dedup: 29 failing observations -> 3 unique signatures (tickets)
  [12x] REG_RESET_VALUES|FAIL|code=RESET_VAL|reg=SYS_CTRL|exp=0x0
        plats=SILICON logs=3 repro: test_runner --plat silicon --only REG_RESET_VALUES --seed 0x6fa596ca --repeat 1
  [12x] ECC_SCRUB_DONE|TIMEOUT|code=POLL_TIMEOUT|reg=ECC_STAT
        plats=SILICON logs=3 repro: test_runner --plat silicon --only ECC_SCRUB_DONE --seed 0x36c0e348 --repeat 1
  [ 5x] ETH_PHY_LINK_UP|TIMEOUT|code=POLL_TIMEOUT|reg=ETH_STAT
        plats=SILICON logs=3 repro: test_runner --plat silicon --only ETH_PHY_LINK_UP --seed 0xdb4c1568 --repeat 1

== FPGA stable-pass but SILICON not: (triage queue, see N06G 2.8 order)
  REG_RESET_VALUES   FPGA=stable-pass  SILICON=stable-fail
  ECC_SCRUB_DONE     FPGA=stable-pass  SILICON=timeout

== GATE: RED  stable-fail/timeout=2 flaky(quarantined, tracked)=1
```

- **4가지 분류가 모두 나옴**: stable-pass(대부분), stable-fail(`REG_RESET_VALUES` 실리콘 — reset tree 차이), timeout(`ECC_SCRUB_DONE` 실리콘 — clock gate 누락), flaky(`ETH_PHY_LINK_UP` 실리콘 — 링크 업 시간 분포 꼬리가 5 ms 한도 초과).
- `3/12 = 0.250`: 12번 무실패는 "실패율 25% 미만" 정도만 말해 준다 → 실리콘 sign-off에는 훨씬 큰 n이 필요 ([N06B](2026-11-01_N06B_coverage_metrics_exit_criteria.html)).
- `TEMP_SENSOR_RANGE`는 FPGA에서 PASS지만 stub → 커버리지 credit 없음 표시.
- dedup: 29건 → 티켓 3개. `REG_RESET_VALUES`의 `got=0x1`은 안정 키가 아니라서 버렸지만 `exp`는 유지 — 키 등급은 결과 라인 설계 때 정한다.
- FPGA↔Si diff 큐에 정확히 §3의 두 테스트가 올라옴. `ETH`는 FPGA에서 not-run이라 큐에 안 올라옴(비교 불가 — "FPGA 통과"라는 근거 자체가 없음).

**③ negative control 1 — bit flip 주입: 잡아야 한다**

```text
RESULT SELFTEST_CHECKER PASS plat=SILICON build=g1a2b3c iter=0 seed=0x76555b40 t_us=1 limit_us=20
RESULT DMA_COPY_4K FAIL plat=SILICON build=g1a2b3c iter=0 seed=0xaf2a4447 t_us=2 limit_us=30 code=DATA_MISMATCH len=4096 off=443 exp=0x39 got=0x31
RESULT DMA_RANDOM_SIZES FAIL plat=SILICON build=g1a2b3c iter=0 seed=0x52375a0b t_us=2 limit_us=400 code=DATA_MISMATCH len=5273 off=4352 exp=0x1d got=0x3d

== negative control: outputs/negctrl_bitflip.log  expect FAIL for DMA_COPY_4K,DMA_RANDOM_SIZES
  DMA_COPY_4K        CAUGHT  (1/1 FAIL)
  DMA_RANDOM_SIZES   CAUGHT  (1/1 FAIL)
  verdict: OK - injected fault detected
negctrl1 rc=0
```

- `exp=0x39 got=0x31`: 정확히 1비트(0x08) 차이 — 주입이 의도대로 단일 비트였음을 결과 줄이 스스로 증명.

**④ negative control 2 — 같은 주입 + 체커 mutation: 거짓 green이 생기고, self-test가 잡는다**

```text
RESULT SELFTEST_CHECKER FAIL plat=SILICON build=g1a2b3c iter=0 seed=0x76555b40 t_us=1 limit_us=20 code=CHECKER_BLIND injected=1 detected=0
RESULT DMA_COPY_4K PASS plat=SILICON build=g1a2b3c iter=0 seed=0xaf2a4447 t_us=2 limit_us=30
RESULT DMA_RANDOM_SIZES PASS plat=SILICON build=g1a2b3c iter=0 seed=0x52375a0b t_us=18 limit_us=400

== negative control: outputs/negctrl_mutated.log  expect FAIL for DMA_COPY_4K,DMA_RANDOM_SIZES
  DMA_COPY_4K        ESCAPED (0/1 FAIL)
  DMA_RANDOM_SIZES   ESCAPED (0/1 FAIL)
  SELFTEST_CHECKER   FAIL -> checker itself is blind; every PASS in this run is suspect
  verdict: NEGATIVE CONTROL ESCAPED
negctrl2 rc=1 (expected 1: the mutation must be detected)
```

- 이 블록이 페이지의 핵심 증거: **데이터가 깨졌는데 DMA 테스트 2개가 PASS**. 상시 self-test가 없었다면 이 런은 green으로 기록됐다. mutation 관점에선 "mutant(skip_compare)를 self-test가 죽였다".

**⑤ timeout 스케일을 끄면 (FPGA)**

```text
RUN plat=FPGA build=g1a2b3c seed=0x00000077 repeat=1 slowdown=20 timeout_scale=1 timeout_scaling=OFF
RESULT SELFTEST_CHECKER TIMEOUT plat=FPGA build=g1a2b3c iter=0 seed=0x76555b40 t_us=32 limit_us=20 code=OVER_BUDGET
RESULT REG_RW_WALK TIMEOUT plat=FPGA build=g1a2b3c iter=0 seed=0xa43d3dc6 t_us=128 limit_us=20 code=OVER_BUDGET
RESULT DMA_COPY_4K TIMEOUT plat=FPGA build=g1a2b3c iter=0 seed=0xaf2a4447 t_us=36 limit_us=30 code=POLL_TIMEOUT reg=DMA_STAT len=4096 limit_si_us=18
RESULT DMA_RANDOM_SIZES TIMEOUT plat=FPGA build=g1a2b3c iter=0 seed=0x52375a0b t_us=36 limit_us=400 code=POLL_TIMEOUT reg=DMA_STAT len=5273 limit_si_us=20
RESULT ECC_SCRUB_DONE TIMEOUT plat=FPGA build=g1a2b3c iter=0 seed=0xfa9297d4 t_us=56 limit_us=100 code=POLL_TIMEOUT reg=ECC_STAT clken=0x0
```

- 멀쩡한 기능 5개가 거짓 TIMEOUT. 두 층이 다 보인다: 테스트 **budget**(`OVER_BUDGET` — 레지스터 walk 64 access가 FPGA에서 128 µs) 과 **poll**(`POLL_TIMEOUT` — 18 µs 한도 vs 36 µs). 반대 방향(FPGA에 맞춘 긴 timeout이 실리콘에서 hang 검출을 늦춤)도 같은 원리.

**⑥ flaky 실패를 seed로 재현**

```text
RESULT ETH_PHY_LINK_UP TIMEOUT plat=SILICON build=g1a2b3c iter=0 seed=0xdb4c1568 t_us=5000 limit_us=8000 code=POLL_TIMEOUT reg=ETH_STAT limit_si_us=5000
RESULT ETH_PHY_LINK_UP TIMEOUT plat=SILICON build=g1a2b3c iter=0 seed=0xdb4c1568 t_us=5000 limit_us=8000 code=POLL_TIMEOUT reg=ETH_STAT limit_si_us=5000
RESULT ETH_PHY_LINK_UP TIMEOUT plat=SILICON build=g1a2b3c iter=0 seed=0xdb4c1568 t_us=5000 limit_us=8000 code=POLL_TIMEOUT reg=ETH_STAT limit_si_us=5000
```

- 3/3 재현 — **단, mock에서 PHY의 "아날로그 랜덤성"을 seed에서 파생했기 때문**. 실제 PHY의 링크 업 시간은 seed로 재현되지 않는다; seed가 재현해 주는 건 테스트 쪽 선택(크기·순서·패턴)뿐. 실 HW에선 loop로 율을 재고 V/T 상관을 본다 (§2.5 6번).

**⑦ 자동 bisect (git bisect run, 16 빌드)**

```text
  build=8: PASS -> 0 (good)
  build=12: FAIL -> 1 (bad)
  build=10: cannot build -> 125 (skip)
  build=11: FAIL -> 1 (bad)
  build=9: PASS -> 0 (good)
There are only 'skip'ped commits left to test.
The first bad commit could be any of:
We cannot bisect more!
    candidate: build 10
    candidate: build 11
```

- 5번 테스트로 16 빌드를 {10, 11}까지 좁힘. 빌드 10이 skip(125)이라 "10이 원인일 수도"를 배제 못 함 — §2.6의 "빌드 깨짐 방치 = bisect 해상도 손실" 실례.

### 6.3 더 해 볼 것 `[미구현]`

- 결과를 SQLite에 넣고 (build, plat) 별 추세·새 signature 강조 (BV14 DB 스키마 재사용).
- `--loop` 모드로 flaky 율 + Clopper-Pearson 구간 (LP14 `link_cycle.py` 재사용).
- 링커 섹션 등록(`__attribute__((section))`) 버전 — Mach-O/ELF 섹션 이름 차이 때문에 이 mock은 테이블로 둠.

## 7. 자주 틀리는 것

1. **timeout을 루프 횟수나 FPGA에서 잰 값으로 쓴다** → FPGA→실리콘에서 delay가 20× 짧아지거나, 실리콘 값을 FPGA에 쓰면 거짓 TIMEOUT (§6 ⑤).
2. **외부 상대 장치(센서·링크 파트너) 대기까지 스케일**한다 → 실세계 장치는 느려지지 않는다.
3. **테스트 안에 `#ifdef FPGA`** → 플랫폼 차이가 테스트 수백 개에 흩어져 실리콘 Day 1에 전부 다시 본다.
4. **stub PASS를 커버리지로 센다** → "FPGA에서 온도 센서 검증 완료"는 거짓말 (§6 `stub=1`).
5. **SKIP이 많은 green을 green으로 보고** → executed / skipped / stub를 따로 리포트.
6. **retry 통과를 PASS로 합친다** → flaky가 사라져 보이고, 실리콘 margin 문제를 덮는다.
7. **flaky를 quarantine하고 안 돌린다** / 담당·기한 없음 → 영원히 고쳐졌는지 모름.
8. **인프라 실패를 bisect에 "bad"로 넘긴다** → 엉뚱한 커밋 지목; 0/1/125/중단 계약을 지킬 것.
9. **negative control을 한 번만** 하고 끝 → 체커가 나중에 죽어도 green (§6 ④). 매 런 self-test.
10. **"FPGA에선 됐으니 실리콘 버그"** 로 바로 간다 → 가장 흔한 원인은 테스트가 FPGA 특성(timeout·초기값·clock gate)에 기댄 것 (§3).

## 8. 출처

1. Synopsys — FPGA Prototyping for Scaled-Out Chip Design & Validation (실리콘 대비 한두 자릿수 느림, Android 부팅 300억 사이클 예): https://www.synopsys.com/blogs/chip-design/fpga-prototyping-scaled-out-chip-design-validation.html
2. S2C — FPGA Prototyping in Practice: Addressing Peripheral Connectivity Challenges (PHY 부재·speed adaptor·메모리 대체; BV01 경유 인용): https://www.s2cinc.com/resources/lit/en/wp/fpga-prototyping-in-practice-addressing-peripheral-connectivity-challenges.pdf
3. Zephyr Project — Test Framework (ztest: `ZTEST_SUITE`/`ZTEST` 등록, setup·before·after·teardown, 결과 출력 형식): https://docs.zephyrproject.org/latest/develop/test/ztest.html
4. John Micco, Google Testing Blog — Flaky Tests at Google and How We Mitigate Them (2016-05; pass→fail 전환 ~84%가 flaky 관련, 테스트 7개 중 1개 이상 flaky — 원문 수치는 검색 결과 인용으로 확인, 본문 페이지는 댓글만 로드됨): https://testing.googleblog.com/2016/05/flaky-tests-at-google-and-how-we.html
5. Git documentation — git-bisect (`bisect run` exit code 0 good, 1–127 bad except 125 skip, >127 abort; skip 인접 시 모호): https://git-scm.com/docs/git-bisect
6. Wikipedia — Mutation testing (mutant killed, mutation score, equivalent mutant, 연산자 예): https://en.wikipedia.org/wiki/Mutation_testing
7. Linaro — LAVA documentation (중앙 서버 + worker 구조의 보드 자동화 CI; 팜 상세는 BV14): https://docs.lavasoftware.org/lava/
