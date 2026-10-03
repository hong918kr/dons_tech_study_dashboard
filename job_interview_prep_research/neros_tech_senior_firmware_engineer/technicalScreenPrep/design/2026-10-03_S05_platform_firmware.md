# 🧱 S05 · Platform firmware — 무엇을 만들고, 어떻게 테스트하나

> Platform 팀은 flight · ground-station · autonomy 팀이 **모두 올라타는 공통 runtime**(logging · telemetry · IPC · configuration), SDK, 그리고 MCU와 Linux를 아우르는 **크로스 컴파일 빌드 시스템**을 만든다 [확인됨 공고 5195308007]. 고객이 "다른 엔지니어"라서, 테스트의 핵심은 기능 하나가 맞는지보다 **인터페이스가 안 깨지는지 · 모든 타깃에서 빌드되는지 · 전원이 끊겨도 데이터가 안 깨지는지 · 자원 예산을 안 넘는지**다. 이 노트는 그 네 가지를 잡는 테스트 시스템을 시나리오로 설계한다. Don의 원래 지원 트랙이라 온사이트의 Senior FW Platform HM 대비도 겸한다.

## 0. 한 장 요약

| 항목 | 내용 |
|---|---|
| 이 직군이 만드는 것 | logging · telemetry · IPC · config 라이브러리, SDK, bootloader/update 경로, Bazel 크로스 컴파일 빌드 [확인됨 공고 5195308007 · 5244552007] |
| 주로 고장 나는 곳 | 소비자 팀 코드와 만나는 **인터페이스 경계**, 특정 타깃에서만 깨지는 빌드, 전원 차단 중 flash/config 쓰기, 자원(RAM · flash · CPU · 대역폭) 초과, 스키마 버전 불일치 |
| 대표 테스트베드 | ① host 단위 + sanitizer + fuzz ② **consumer × target CI 매트릭스** ③ QEMU/Renode 에뮬레이션 ④ **전원 차단 주입 리그** ⑤ OTA rollback 미니 fleet ⑥ footprint/CPU 예산 게이트 ⑦ telemetry 스키마 계약 테스트 |
| 합격 기준 예 | 전원 차단 10,000회에 config 손상 0 · 모든 consumer×target 빌드 green · 라이브러리별 flash +2% 초과 시 PR 차단 · fuzz 24h crash 0 · OTA 중단 100회 전부 이전 이미지로 복구 |
| Don 연결 | NVMe **telemetry** 디버그 기능, production FW의 **error reporting/handling scheme**, test platform **SDK**(다른 팀이 사용) — Bazel·embedded Linux는 레쥬메에 없음 (정직하게) |

## 1. 이 직군 이해하기 — Neros 공고 근거

- **Senior Firmware Engineer, Platform** [확인됨 공고 5195308007]: "own the common runtime, libraries, SDK, and build system that our flight, ground-station, and autonomy software teams build on — across microcontroller and Linux targets alike", "Your customers are other engineers"
- 책임: logging · telemetry · IPC · configuration 라이브러리 / "fast, reproducible, hermetic builds across every target we ship" / "keeping [interfaces] stable as the platform evolves underneath" / "Drive resource trade-offs — compute, memory, bandwidth" [확인됨 공고 5195308007]
- 요구: "Hands-on ownership of an embedded build system (e.g. Bazel)", MCU/RTOS + embedded Linux [확인됨 공고 5195308007]
- **Lead, Platform Software** [확인됨 공고 5244552007]: "Run the platform as a product: keep the interfaces … stable, versioned, and evolving without breaking them", "Set and uphold engineering quality practices — code review, testing, and CI/CD — for foundational software"
- **Senior Embedded Linux Engineer** [확인됨 공고 5195277007]도 같은 runtime · SDK를 Linux 쪽에서 소유 → MCU 쪽 platform과 Linux 쪽 platform이 **같은 라이브러리 API를 두 타깃에서** 제공하는 구조 [추정]
- 테스트 관점의 의미 [추정]: platform 버그 하나가 **모든 제품 라인**에 퍼진다. 그래서 테스트 조직이 platform에 기여할 일은 "기능 테스트"보다 **회귀를 막는 게이트**(빌드 매트릭스, 계약, 예산, 전원 차단 내성)다
- 관련: 이 팀이 쓰는 MCU는 STM32 [확인됨 10-01 Michael], 공통 runtime 위에 flight([S01](2026-10-03_S01_flight_software.md))와 autonomy([S06](2026-10-03_S06_embedded_linux_autonomy.md))가 올라간다

## 2. 이 펌웨어가 하는 일 — 데이터 흐름

```text
  consumer teams:  Flight FW (STM32)      Ground FW        Autonomy (Linux, C++)
                        │                     │                    │
                        ▼                     ▼                    ▼
  ┌──────────────────── platform SDK (stable, versioned API) ───────────────────┐
  │  log_write()   telem_publish(topic,msg)   ipc_send()/recv()   cfg_get/set()  │
  │      │                 │                      │                   │          │
  │  ring buffer      schema registry        queues / shm         A/B config    │
  │  (RAM, ISR-safe)  (msg id + version)     (MCU: queue,          pages in     │
  │      │                 │                  Linux: UDS/shm)      flash + CRC  │
  │      ▼                 ▼                      │                   │          │
  │  flash/SD log     serializer ──► link (UART/CRSF/MAVLink/Ethernet) ──► GCS   │
  └──────────────────────────────────────────────────────────────────────────────┘
        ▲                                                        ▲
   bootloader (verify image → jump / fallback)          build: Bazel → every target
```

- **ISR → ring buffer → 백그라운드 flush**가 로깅의 기본형 ([onsitePrep N05](../../onsitePrep/site/notes/2026-10-01_N05_ring_buffer_onsite.html))
- config는 두 페이지(A/B) + 시퀀스 번호 + CRC로 원자적 갱신 ([onsitePrep N06 7절 config 저장](../../onsitePrep/site/notes/2026-10-01_N06_generic_system_design.html))
- telemetry는 **메시지 id + 스키마 버전**을 실어야 소비자(GCS, 로그 디코더, 데이터 플랫폼)가 구버전 기체도 해석한다 [추정]
- OTA/bootloader 상세 설계는 [onsitePrep N06 4절 OTA](../../onsitePrep/site/notes/2026-10-01_N06_generic_system_design.html) — 여기서는 **그걸 어떻게 테스트하나**만

## 3. 무엇이 고장 나나

| failure mode | 증상 | 어느 레벨에서 잡나 |
|---|---|---|
| API/ABI breaking change (struct 필드 순서, enum 값 이동) | 다른 팀 빌드는 되는데 런타임 값이 틀림 | 계약 테스트, consumer CI 매트릭스, ABI diff (PR) |
| 특정 타깃 빌드 깨짐 (정렬, `sizeof(long)`, 컴파일러 버전) | "내 보드에선 되는데" | 크로스 타깃 빌드 매트릭스 (PR) |
| 전원 차단 중 flash 쓰기 | 부팅 후 config 초기화 · 파라미터 깨짐 · brick | 전원 차단 주입 리그 (nightly, 수천 회) |
| 로깅이 ISR 시간을 잡아먹음 | 제어 루프 jitter, 센서 샘플 누락 | HIL 타이밍 측정, GPIO 토글 + LA (nightly) |
| ring buffer overflow · drop 미보고 | 로그 구멍, 사후 분석 불가 | 스트레스 테스트 + drop counter 검증 (component) |
| 파서 취약 (길이 필드, 잘린 프레임) | crash, 메모리 손상 | fuzz (libFuzzer + ASan) (nightly) |
| 스키마 버전 불일치 | GCS가 잘못된 값 표시, 디코더 실패 | producer/consumer 계약 테스트 (PR) |
| flash/RAM 예산 초과 | 링크 실패 · 스택 overflow · 다른 팀 기능 밀려남 | footprint 게이트 (PR), 스택 watermark (HIL) |
| OTA 중단 · 잘못된 이미지 | 부팅 불가, 필드 회수 | OTA rollback fleet (release) |
| 비재현 빌드 | 같은 커밋인데 다른 바이너리 → 디버그 심볼 불일치 | hermetic 빌드 해시 비교 (nightly) |

## 4. 테스트 레벨 — 누가, 어디서, 언제

| 레벨 | 누가 | 무엇을 | 어디서 | CI 단계 |
|---|---|---|---|---|
| unit | platform FW 개발자 | 함수 · 모듈 로직 (ring buffer, CRC, serializer, config 상태 머신) | host (x86), sanitizer | PR |
| component | 개발자 + 테스트 | 라이브러리 하나를 실제 동시성으로 (ISR 시뮬레이션, 스레드), fuzz | host + QEMU/Renode | PR (짧게) · nightly (fuzz 장시간) |
| integration | **테스트 팀** | 라이브러리 조합 + 소비자 코드: logging + telemetry + config가 한 이미지에서 | 에뮬레이터 + 실보드 1종 | PR (빌드 매트릭스) · nightly |
| HIL | **테스트 팀** | 실제 STM32 보드, 실제 flash, 실제 전원: 전원 차단 · 타이밍 · 스택 | 리그 (PSU 릴레이, LA) | nightly · release |
| system | 테스트 + 소비자 팀 | 전체 기체 이미지에서 platform 서비스 (로그 추출, 파라미터 변경, OTA) | 기체 벤치 ([S08](2026-10-03_S08_system_integration_release.md)) | release |
| field | flight test + platform | 실제 비행 로그 무결성, OTA 성공률, drop counter 텔레메트리 | 필드 | 출시 후 모니터링 |

- 원칙: **unit은 개발자 몫**, 테스트 팀은 "경계"를 맡는다 — 타깃 간, 팀 간, 전원·시간 같은 물리 조건
- C 라이브러리를 host에서 pytest로 구동하는 방법: [문제 07 ctypes](../python/problems/07_ctypes_c_ring_buffer.md)

## 5. 테스트베드 설계

```text
 GitLab CI ──► Bazel build matrix ─────────────────────────────┐
   │            (targets: stm32f4, stm32h7, x86_host, arm64_linux) × (consumers: flight, ground, autonomy)
   │                                                            │ artifacts (elf, map, hash)
   ├──► host jobs: unit · ASan/UBSan · libFuzzer · footprint diff (from .map) · ABI diff
   │
   ├──► emulation jobs: QEMU / Renode (Cortex-M) — boot, logging, config state machine
   │
   └──► HIL rigs (runner tag: platform-hil)
          ┌──────────────────────────────────────────────────────────┐
          │ rig host (Linux, pytest)                                  │
          │   ├─ SWD probe (ST-LINK/J-Link) ── flash, read RAM/stack  │
          │   ├─ USB-UART ── DUT CLI + telemetry stream               │
          │   ├─ PSU (SCPI) + relay/MOSFET ── 전원 차단 주입          │
          │   ├─ GPIO/LA (Saleae) ── ISR·로깅 타이밍 마커             │
          │   └─ USB hub per-port power ── 재열거                      │
          │           │                                               │
          │        DUT: STM32 board (flight 보드 리비전별)             │
          └──────────────────────────────────────────────────────────┘
          OTA mini-fleet: 같은 보드 6대 + Linux companion 2대, 원격 전원
```

| 장비 | 용도 | 대략 비용 [추정] |
|---|---|---|
| STM32 DUT 보드 × 리비전별 2 | 실제 flash · 타이밍 | 보드당 $50–300 |
| ST-LINK V3 / J-Link | 플래시, RAM 읽기, 스택 watermark | $40–600 |
| 프로그래머블 PSU (SCPI) | 전압 · 전류 측정, 브라운아웃 램프 | $400–1,500 |
| 릴레이/MOSFET 보드 + 타이밍 제어 MCU | μs 단위로 정확한 전원 차단 | $50–150 |
| Logic analyzer (Saleae 등) | GPIO 마커로 ISR · flush 시간 측정 | $500–1,500 |
| 리그 호스트 (mini PC) | pytest · CI runner | $500 |
| 리그 1대 합계 | | 약 $2–4K |

**자동화 구조 (pytest)**

| 층 | 이름 예 | 역할 |
|---|---|---|
| fixture | `rig` (session), `dut` (function, reset) | [문제 04 HIL conftest](../python/problems/04_hil_conftest.md) 패턴 |
| fixture | `power_cutter` | `cut_at(event="flash_write_start", delay_us=...)` — DUT가 GPIO로 "쓰기 시작"을 알리면 지정 지연 후 차단 |
| fixture | `footprint` | `.map` 파싱 → 라이브러리별 `.text/.data/.bss` |
| driver | `SwdProbe`, `DutCli`, `ScpiPsu`, `LogicCapture` | 계측기 래퍼 |
| driver | `TelemetryDecoder(schema_dir)` | 스키마 버전별 디코드, 계약 테스트용 |
| marker | `@pytest.mark.power_cycle`, `@pytest.mark.slow` | nightly 전용 |

**리그 수 산정** [추정]: 전원 차단 1회 = 차단 → 부팅 → config 검증 ≈ 3초 → 리그 1대가 1시간에 ~1,200회. nightly 10,000회 × 보드 리비전 3종 = 30,000회 ≈ 25 리그-시간 → 전원 차단 전용 리그 3대가 8시간 밤에 처리. PR용 HIL 스모크는 15분 × 하루 40 PR = 10시간 → 리그 2대. 합계 **5대 + OTA fleet 1세트**에서 시작.

## 6. 대표 테스트 케이스

| ID | 시나리오 | 자극 stimulus | 관측 | 합격 기준 |
|---|---|---|---|---|
| PL-01 | config 쓰기 중 전원 차단 | `cfg_set` 직후 0–5 ms 무작위 지연으로 차단, 10,000회 | 재부팅 후 config 값 · CRC · 시퀀스 번호 | 손상 0, 값은 **이전 또는 새 값 중 하나** (중간값 금지) |
| PL-02 | 브라운아웃 램프 | 3.3 V → 2.5 V로 100 ms 동안 천천히 하강, 복귀 | BOR 리셋 여부, flash 상태 | 부분 쓰기 0, 리셋 후 정상 부팅 100% |
| PL-03 | 로깅이 제어 루프를 방해하지 않음 | 로그 rate 최대(예: 10 kHz 메시지) + 8 kHz 루프 | GPIO 마커로 루프 주기 jitter | p99 jitter < 5 μs [추정], 루프 누락 0 |
| PL-04 | ring buffer overflow 보고 | 소비자(flush)를 멈추고 생산 지속 | drop counter, high-water mark 텔레메트리 | drop 수 = 실제 손실 수 정확히 일치 |
| PL-05 | 파서 fuzz | libFuzzer로 telemetry/IPC 디코더에 무작위 입력 24h | crash, ASan/UBSan 리포트 | crash 0, 새 coverage 정체 확인 |
| PL-06 | 스키마 계약 | producer가 v3 메시지, consumer 디코더 v2 · v3 | 디코드 결과 | v2는 알려진 필드만 정확히, 미지 필드는 무시 (crash 금지) |
| PL-07 | ABI 안정성 | PR 전후 공개 헤더 비교 (`abidiff` 또는 struct offset 덤프) | 필드 offset · size · enum 값 | minor 버전에서 변경 0 |
| PL-08 | 빌드 매트릭스 | 모든 consumer × target | 빌드 결과, 경고 | 전부 green, `-Werror` 위반 0 |
| PL-09 | footprint 예산 | PR 빌드의 `.map` vs main | 라이브러리별 flash/RAM 증가량 | 라이브러리당 +2% 또는 +1 KB 초과 시 차단 (승인 라벨로 예외) |
| PL-10 | 재현 가능 빌드 | 같은 커밋을 두 runner에서 빌드 | 바이너리 해시 | 해시 동일 |
| PL-11 | OTA 중단 복구 | 다운로드 50% · 쓰기 중 · 검증 중 · 첫 부팅 중 전원 차단, 각 25회 | 부팅 슬롯, 버전 | 100회 전부 정상 이미지로 부팅, brick 0 |
| PL-12 | 잘못된 이미지 거부 | 서명 깨진 이미지, 다른 보드용 이미지, 다운그레이드 | bootloader 판정 | 전부 거부 + 이유 로그 |

## 7. 면접 시나리오 — "Design a test system for our platform libraries"

### 요구사항 질문 (영어)

- "Which targets do we ship today — how many MCU families and Linux boards — and how many consumer teams build against the SDK?"
- "Is the SDK consumed as source, prebuilt libraries, or through Bazel targets in a monorepo?"
- "What does 'stable interface' mean here — source compatibility, binary compatibility, or wire-format compatibility for telemetry?"
- "Where does persistent state live — internal flash, external flash, SD — and has power loss caused field issues?"
- "What's the release cadence for the platform versus the consumer teams?"

### 가정 숫자 [추정]

- 타깃 4종 (STM32 2종, x86 host, arm64 Linux) × consumer 3팀 = 빌드 12조합, 조합당 4분 → 병렬로 PR당 ~5분
- 공개 API 헤더 ~40개, telemetry 메시지 ~150종
- PR 하루 40개, nightly 1회, 릴리즈 2주마다

### 블록도

- 5절 블록도 그대로: **host 게이트 → 에뮬레이션 → HIL → OTA fleet**, 아래로 갈수록 느리고 비싸서 실행 빈도를 낮춘다

### 핵심 결정과 trade-off

| 결정 | 선택 | trade-off |
|---|---|---|
| 회귀를 어디서 잡나 | 최대한 **host와 빌드 단계**에서 (ABI diff, 계약, footprint) | 하드웨어 문제(전원, 타이밍)는 못 잡음 → HIL은 그 두 가지에 집중 |
| consumer 코드를 platform CI에서 빌드 | 예 — consumer 대표 타깃을 platform PR마다 빌드 | CI 시간 증가 vs "platform이 깨뜨린 걸 consumer가 먼저 발견"하는 비용 |
| 에뮬레이터 (QEMU/Renode) | 로직 · 부팅 흐름은 에뮬, 타이밍 · flash는 실보드 | 에뮬은 주변장치 모델이 부정확 → 타이밍 판정 금지 |
| 전원 차단 주입 | DUT가 GPIO로 이벤트를 알리고 리그가 지연 후 차단 | 무작위 차단만 하면 위험 구간(수 ms)을 거의 못 맞힘 |
| footprint 게이트 | 절대값 + 증가율, 예외는 라벨로 | 너무 빡빡하면 개발 속도 저하 → 예산표를 팀과 합의 |
| 스키마 관리 | 메시지 정의 단일 소스 → 코드 생성, 버전 필드 필수 | 초기 도입 비용 vs 필드 기체 · 데이터 플랫폼 호환 |

### 실패 모드

- 리그 flash 마모: 전원 차단 수만 회 → DUT flash 수명 소모 → 보드를 소모품으로 관리, 쓰기 횟수 카운트
- 계약 테스트가 형식적: 스키마 변경을 "버전만 올리고" 통과 → 구버전 디코더 실행을 필수로
- footprint 예외 남발: 예외 라벨 사용 횟수를 대시보드에 ([N04 문제 C](../notes/2026-10-02_N04_system_design_test_tooling.md))
- 인프라 실패와 제품 실패 혼동: 프로브 연결 실패는 `error(infra)`로 분리

### 영어 2분 요약

> "Platform bugs multiply across every product line, so I'd put most of the gating as early and as cheap as possible. Every platform PR builds the SDK for every target and builds a representative consumer from each team, plus an ABI diff on the public headers, a telemetry schema contract check, and a footprint diff from the map files with a per-library budget. Logic runs on the host under sanitizers, and parsers get fuzzed nightly.
>
> Hardware is reserved for what only hardware shows: power loss and timing. A power-cut rig has the DUT signal the start of a flash write on a GPIO, and a relay cuts power after a controlled delay — ten thousand cycles a night, and config must always be either the old value or the new one, never corrupted. A logic analyzer measures control-loop jitter while logging runs at full rate. Before a release, a small fleet of boards goes through interrupted OTA updates at every stage, and every one must boot a valid image."

## 8. 꼬리 질문

**Q. How do you keep an interface stable while the platform evolves?**

> "Version the API and the wire format separately, add fields only at the end with defaults, never reuse enum values, and run an ABI diff plus old-consumer builds in CI so a break is caught in the PR, not by another team a week later."

**Q. Random power cuts didn't find anything. Are we safe?**

> "Not necessarily — the vulnerable window during a flash write is a few milliseconds, so random cuts rarely hit it. I'd have the firmware raise a GPIO at the start and end of the write and sweep the cut delay across that window."

**Q. What can an emulator like Renode not tell you?**

> "Real timing, real flash behavior and peripheral quirks. I trust it for boot flow and logic, and I keep timing and power-loss verdicts on real boards."

**Q. How do you test that logging doesn't hurt the control loop?**

> "Toggle a GPIO at the start of each loop iteration, capture it on a logic analyzer while logging at the maximum rate, and gate on the p99 period jitter and zero missed iterations."

**Q. A consumer team says your telemetry change broke their dashboard.**

> "That's a missing contract test. I'd add their decoder version to the compatibility matrix so producer changes are checked against every supported consumer before merge."

## 9. Don 경험 연결 (레쥬메 범위)

| 이 노트의 포인트 | Don 경험 | 말할 문장 |
|---|---|---|
| telemetry · 스키마 · 소비자 | SK hynix NVMe **telemetry** 디버그 기능 (MS · Dell · HPE 고객) | "I shipped an NVMe telemetry debug feature that data-center customers parsed with their own tools, so format compatibility was not optional." (Don: 버전 관리 방식 — 레쥬메에 없음) |
| 에러 처리 · 로깅 | Solidigm production FW의 **error reporting/handling scheme** | "I designed error reporting in production SSD firmware, where losing an error record means losing the root cause." |
| 다른 팀이 쓰는 SDK | SK hynix 챔버 테스트 플랫폼 **SDK** | "Other engineering teams built on my SDK, so I learned to treat interface changes as a product decision." |
| 전원 · 신뢰성 | SSD FW (전원 손실 대비는 SSD의 핵심 주제) | (Don: 실제 담당했던 power-loss 관련 작업 — 레쥬메에 명시 없음, 지어내지 말 것) |
| 빌드 시스템 (Bazel) | ❌ 레쥬메에 없음 | "I haven't owned a Bazel build, but I've lived with multi-target firmware builds and I know what the CI gates need to check." |
| embedded Linux | ❌ 레쥬메에 없음 | [S06](2026-10-03_S06_embedded_linux_autonomy.md) 9절 참고 |

## 체크

- [ ] 5절 블록도를 보지 않고 그리기 (host → 에뮬 → HIL → OTA fleet)
- [ ] PL-01 전원 차단 테스트의 "GPIO로 위험 구간 알리기" 아이디어를 30초 영어로
- [ ] 7절 영어 2분 요약 소리 내어 1회
- [ ] footprint 게이트 · ABI diff · 스키마 계약 — 각각 무엇을 막는지 한 줄씩
- [ ] onsitePrep N06의 config · OTA 문제와 이 노트의 PL-01 · PL-11을 연결해서 설명해 보기
