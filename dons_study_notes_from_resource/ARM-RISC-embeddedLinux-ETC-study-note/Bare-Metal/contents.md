# Bare-Metal — Study Contents (나침반)

> **작성일**: 2026-10-18 · **목표**: ① OS 없이 칩을 **전원 인가부터 직접 살리는** 능력을 컴퓨터 구조 · 레지스터 · 전기 신호까지 원천부터 설명 ② MCU 펌웨어 · 실리콘 bring-up · 검증 FW 면접 통과 ③ 입사 후 새 칩 · 새 보드에서 첫 코드를 올리고, 드라이버 · 부트로더 · 테스트 FW를 바로 쓰는 실무자
> **용도**: 연구할 개념을 **빠짐없이 순서대로 나열한 목차**. Embedded Linux · RTOS 나침반과 같은 구조(모듈 A–S, ID · 우선순위 · Don 출발점)로 만들고, 각 항목을 노트(쉬운 설명 · 그림 · 실제로 돌려 본 코드)로 채워 간다
> **근거**: Arm v7-M / v8-M / v8-A 아키텍처 레퍼런스, RISC-V 특권 스펙, GNU ld 문서, STM32 · NXP 레퍼런스 매뉴얼, 컴퓨터 구조 교재(Patterson & Hennessy, Harris & Harris), 2026-10 웹 확인 트렌드(§5), Rivian(실리콘 · 센싱) · Neros · Anduril · Scout JD 요구사항
> **중복 표시 규칙**: [Embedded Linux 나침반](../../../EmbeddedLinux/site/compass/contents.html)과 [RTOS 나침반](../../../RTOS/site/compass/contents.html)에 같은 주제가 있으면 각 항목의 **겹침** 칸에 `EL H9` · `RTOS B2`처럼 표시만 해 둔다. 어느 쪽에서 깊게 쓸지(주인)는 **writer 의뢰 시점에 정한다** (§3 표)
> **노트 사이트**: [전체 노트 목록](../index.html) — 빌드 `python3 build_site.py`

---

## 0. 이 문서 읽는 법

**표기**
- **ID**: 모듈 알파벳 + 번호 (예: `F3`). 노트 파일 이름으로 쓴다 → `F_toolchain/2026-10-xx_F3_linker_script.md`
- **겹침**: `EL H9` = Embedded Linux 나침반 H9, `RTOS B2` = RTOS 나침반 B2, `Hark E3` = Hark Embedded AI 노트, `Anduril` = Anduril C 문제은행. `—` = 겹침 없음 (Bare-Metal 고유)
- **우선순위**: `P0` = 1차 기술 면접 전에 반드시 · `P1` = 온사이트 · 입사 전 · `P2` = 넓게 알아 두기
- **Don 출발점** (추정 — Don이 고쳐 쓰면 됨):
  - ✅ 이미 강함 (SSD 컨트롤러 FW 7년 · RF 통합 · 인터럽트 · DMA · JTAG · scope) → **말로 정리**만 하면 됨
  - 🟡 인접 경험 있음 → 기존 지식에서 확장
  - ❌ 새로 배움 → 처음부터

**전체 그림 — bare-metal 펌웨어의 층**
```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg"><rect x="10" y="10" width="660" height="66" rx="6" fill="#4a7bd0" fill-opacity="0.06" stroke="#4a7bd0" stroke-width="1.3"/><text x="22" y="30" font-size="13" font-weight="bold">펌웨어 구조 · 알고리즘</text><rect x="22" y="40" width="404" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="34" y="58" font-size="13">super-loop · FSM · 이벤트 구동 · 협력형 스케줄러</text><text x="414" y="58" font-size="12" text-anchor="end" font-weight="bold">K</text><rect x="434" y="40" width="224" height="26" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="1.3"/><text x="446" y="58" font-size="13">성능 · DSP · 제어 루프</text><text x="646" y="58" font-size="12" text-anchor="end" font-weight="bold">P</text><rect x="10" y="84" width="660" height="66" rx="6" fill="#3f9a6b" fill-opacity="0.06" stroke="#3f9a6b" stroke-width="1.3"/><text x="22" y="104" font-size="13" font-weight="bold">드라이버 · 부트</text><rect x="22" y="114" width="250" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="34" y="132" font-size="13">레지스터 수준 주변장치</text><text x="260" y="132" font-size="12" text-anchor="end" font-weight="bold">I</text><rect x="280" y="114" width="200" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="292" y="132" font-size="13">인터럽트 · 동시성</text><text x="468" y="132" font-size="12" text-anchor="end" font-weight="bold">J</text><rect x="488" y="114" width="170" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b" stroke-width="1.3"/><text x="500" y="132" font-size="13">부트로더 · 보안</text><text x="646" y="132" font-size="12" text-anchor="end" font-weight="bold">L</text><rect x="10" y="158" width="660" height="34" rx="4" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.3"/><text x="22" y="180" font-size="13">언어 · 툴체인: 임베디드 C / C++ / Rust · 링커 스크립트 · startup · 이미지</text><text x="658" y="180" font-size="12" text-anchor="end" font-weight="bold">G, F</text><rect x="10" y="200" width="660" height="66" rx="6" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-width="1.3"/><text x="22" y="220" font-size="13" font-weight="bold">CPU 아키텍처 (원천: 컴퓨터 구조 B)</text><rect x="22" y="230" width="200" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="34" y="248" font-size="13">Cortex-M</text><text x="210" y="248" font-size="12" text-anchor="end" font-weight="bold">C</text><rect x="230" y="230" width="210" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="242" y="248" font-size="13">Cortex-A · R</text><text x="428" y="248" font-size="12" text-anchor="end" font-weight="bold">D</text><rect x="448" y="230" width="210" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c" stroke-width="1.3"/><text x="460" y="248" font-size="13">RISC-V</text><text x="646" y="248" font-size="12" text-anchor="end" font-weight="bold">E</text><rect x="10" y="274" width="660" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a" stroke-width="1.3"/><text x="22" y="294" font-size="13">칩 기반: 클럭 트리 · PLL · 리셋 · 전원 도메인 · option byte</text><text x="658" y="294" font-size="12" text-anchor="end" font-weight="bold">H</text><rect x="10" y="312" width="660" height="30" rx="4" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.3"/><text x="22" y="332" font-size="13">실리콘 · 보드: 회로도 · 신호 · 데이터시트 · pre/post-silicon bring-up</text><text x="658" y="332" font-size="12" text-anchor="end" font-weight="bold">N, O</text><text x="340" y="378" font-size="13" text-anchor="middle" font-weight="bold">모든 층을 가로지르는 것:</text><text x="340" y="400" font-size="13" text-anchor="middle">디버깅 · 측정(M) · 테스트 · 신뢰성 · 안전(Q) · 개념 · 지형(A)</text></svg>
```

그림 1 — bare-metal 펌웨어의 층. 오른쪽 글자는 해당 모듈 ID. RTOS는 이 그림의 "펌웨어 구조(K)" 자리에 커널이 들어가는 것이고, Linux는 F~L 대부분을 커널 · 부트로더가 대신한다

---

## 1. bare-metal을 "원천적으로" 이해하려면 무엇을 알아야 하나 (선행 지식 지도)

> bare-metal = "C 한 줄이 어떤 명령이 되고, 그 명령이 어떤 버스를 지나, 어떤 레지스터 비트를 바꾸고, 그 비트가 어떤 핀의 전압을 바꾸는가"를 끝까지 따라가는 일. Don은 **위(드라이버 · 디버깅)와 아래(신호 · 측정)가 강하고, 가운데(컴퓨터 구조 이론 · A 프로파일 · RISC-V · 실리콘 검증 흐름)를 체계화**하면 된다.

**한 줄의 여정 — `GPIOA->BSRR = 1u << 5;`**

| 단계 | 일어나는 일 | 모듈 |
|---|---|---|
| ① 컴파일 | `volatile` 포인터 → `STR` 명령, 상수는 리터럴 풀 | G1, G2, C2 |
| ② 링크 · 배치 | 코드는 flash(.text), 레지스터 주소는 장치 헤더 상수 | F3, B5 |
| ③ 실행 | 파이프라인 → 쓰기 버퍼 → 버스 매트릭스 → AHB/APB 브리지 | B2, B4, B6 |
| ④ 주변장치 | 클럭이 켜져 있어야 레지스터가 반응, BSRR은 원자적 set | H1, I1 |
| ⑤ 핀 | 출력 드라이버 → 슬루 · 구동 전류 → 보드 배선 → LED | N3, N4 |
| ⑥ 확인 | scope로 핀 상승 시간, DWT로 사이클 수 | M6, M7 |

**"bare-metal을 안다"고 말하기 위한 체크리스트** (전부 "예"면 실무 투입 가능)
- [ ] 벤더 코드 없이 링커 스크립트 + startup + 레지스터만으로 LED와 UART를 살릴 수 있다 (F3, F4, S2)
- [ ] 리셋 직후부터 `main`까지 CPU · 메모리에서 일어나는 일을 순서대로 말할 수 있다 (C4, F4, R2)
- [ ] `volatile`이 보장하는 것과 보장하지 않는 것을 정확히 말할 수 있다 (G1, J5)
- [ ] 클럭을 올리는 순서(전압 → wait state → PLL)를 틀리면 무슨 일이 생기는지 안다 (H2)
- [ ] 레지스터를 썼는데 반응이 없을 때 확인하는 순서를 갖고 있다 (H1, B6, M10)
- [ ] 메인 루프와 ISR이 공유하는 데이터의 레이스를 찾고, 크리티컬 섹션 없이 링버퍼로 없앨 수 있다 (J2, J3)
- [ ] HardFault 레지스터와 스택 프레임으로 원인 줄을 찾는다 (C8, M3)
- [ ] 부트로더에서 앱으로 점프하는 절차를 빠짐없이 쓴다 (L2)
- [ ] AArch64 코어에서 EL3 → EL1로 내려가 MMU와 캐시를 켤 수 있다 (D2, D4, D5)
- [ ] RISC-V 트랩 핸들러를 어셈블리로 쓰고 mcause를 해석한다 (E2, E5)
- [ ] 새 실리콘의 bring-up 순서와 검증 테스트 FW의 구조를 설명한다 (O3, O4)
- [ ] 고정소수점 Q15로 필터를 구현하고 오차를 계산한다 (B8, P5, P6)

---

## 2. 업무 장면 → 필요한 모듈

| 실무자가 실제로 하는 일 | 필요한 모듈 |
|---|---|
| 새 보드가 왔다, 첫 LED를 켜라 | N2, H1, H8, F4, M2, S2 |
| 레지스터를 썼는데 주변장치가 반응하지 않는다 | H1, B6, G2, M10 |
| `-O2`로 빌드하면 깨지고 `-O0`이면 된다 | G1, G3, J5, F6 |
| 플래시가 1 KB 넘쳤다 | F6, F5, P4 |
| 필드에서 랜덤 리셋이 난다 | H3, C8, M9, N5 |
| 부트로더 + 필드 업데이트를 처음부터 만들어라 | L1–L5, I9 |
| 새 SoC 실리콘이 나왔다, 첫 코드를 올려라 (Rivian 실리콘 · AI 칩) | O3, D1–D6, M1, N8 |
| 실리콘 검증용 테스트 FW 세트를 만들어라 | O2, O4, O5, O6 |
| 가속기를 명령 큐 · doorbell로 구동하는 FW | O8, D5, I8 |
| 고속 ADC 샘플링 + DMA + 필터 | I2, I6, I8, P5, P6 |
| 모터 PWM · 엔코더 · 전류 루프 | I2, P7, J8 |
| 배터리 장비 대기 전류를 µA 단위로 | H5, P8, P9, I14 |
| FW Test 직군: 호스트 테스트 + HIL CI | Q1–Q4 |
| 면접: "전원을 넣으면 main까지 무슨 일이?" | C4, F4, R2 |

---

## 3. 겹침 지도 (표시만 — 주인은 writer 의뢰 시 결정, 🔁 = 이미 결정됨)

> Bare-Metal은 RTOS · Linux의 **아래층**이라 겹침이 많다. 특히 RTOS B 모듈(MCU 하드웨어 기반)과 거의 같은 주제가 있다. 아래 표는 "어디와 겹치나"만 기록한다.

| 주제 묶음 | Bare-Metal 항목 | RTOS 항목 | Embedded Linux 항목 | 메모 |
|---|---|---|---|---|
| Cortex-M 프로그래머 모델 · 예외 · NVIC | C1, C4, C5 | B1, B2, B3 | — | 🔁 **주인 = RTOS B1–B3** (2026-10-25, RTOS 먼저 작성) — Bare-Metal은 링크 + bare-metal 관점만 |
| Cortex-M 세대 · FPU · Helium | C10, C11 | B6, B7 | — | 🔁 **주인 = RTOS B6, B7** (10-25) |
| Fault 예외 · HardFault 해독 | C8, M3 | M1 | M3 (oops는 Linux판) | |
| MPU · TrustZone-M | C9, C12 | F4, F6 | — | 🔁 MPU 리전 · 정렬 · 속성 · v8-M base/limit(C9) **주인 = RTOS F4**, 스택 가드 리전 · 카나리(C9 · Q6 일부) **주인 = RTOS F3**, TrustZone-M 개념 · SAU/IDAU · NSC · 상태 전환(C12) **주인 = RTOS F6** (11-01); Bare-Metal은 OS 없는 1회 설정 · TF-M 없는 최소 Secure 이미지 관점만 |
| Cortex-R | D9 | B9 | — | 🔁 **주인 = RTOS B9** (10-25) |
| Cortex-A 예외 레벨 · 부트 체인 | D1, D2, L6 | — | C5, C6 (TF-A · 커널 시작) | Bare-Metal은 "부트 코드를 직접 쓰는" 관점 |
| MMU · 캐시 · TLB | B3, D4, D5 | B8 | F11, H9 | 🔁 MCU 캐시 · TCM(B3, B9 일부) **주인 = RTOS B8** (10-25); MMU · TLB(D4, D5)는 Bare-Metal |
| GIC | D6 | — | H6 | |
| 배리어 · 메모리 순서 | B6, J5 | D8 | F5 | 🔁 MCU에서 배리어가 필요한 곳(J5, B6 순서 부분) **주인 = RTOS D8** (10-31); B6 메모리 타입(Normal / Device)은 Bare-Metal |
| 원자 연산 · 링버퍼 · 크리티컬 섹션 | J2, J3, J4 | C7, D8 | F4 | Anduril SPSC 링버퍼 · 🔁 크리티컬 섹션 구현 **주인 = RTOS C7** (10-25) · 🔁 LDREX/STREX · C11 atomics · SPSC 링버퍼(J3, J4, J2 레이스 일부) **주인 = RTOS D8** (10-31) |
| RISC-V | E1–E5 | B10 | — | Hark E3 ISA 개요 · 🔁 E1–E4 개요 **주인 = RTOS B10** (10-25); E5 startup은 Bare-Metal |
| 툴체인 · ELF · libc | F1, F2, F5 | B11, F7 | B1–B3 | 🔁 F5 · F6(MCU libc · 크기 최적화) **주인 = RTOS B11** (10-25) · 재진입 · `_reent` · malloc 락 · errno(F5 일부) **주인 = RTOS F7** (11-01) |
| 링커 스크립트 · startup | F3, F4 | B5 | — | 🔁 **주인 = RTOS B5** (10-25) |
| 빌드 시스템 | F7 | — | B4 | |
| volatile · 비트 연산 · FSM | G1, G4, K2 | — | — | Anduril C 문제은행 |
| C++ / Rust | G9, G10, K10 | I7, I8 | E8 (Rust for Linux) | Hark J4 |
| 클럭 · 리셋 · 레귤레이터 | H1–H4 | — | H3 | Linux는 clk 프레임워크 관점 |
| 저전력 모드 | H5, P8, P9 | P1–P3 | N5, N6 | |
| GPIO · pinctrl | I1 | — | H4 | |
| 타이머 · ADC · PWM | I2, I6 | E5, J5 | I9 (PWM) | 🔁 입력 캡처 · 출력 비교 · 32→64비트 시간 확장 · 타이머 트리거 ADC(I2 일부) **주인 = RTOS E5** (11-01); 프리스케일러 · ARR · PWM 모드 · 엔코더 · 데드타임 레지스터 설정은 Bare-Metal |
| UART · SPI · I2C | I3, I4, I5 | J3, J4 | H5, H7, H8 | Bare-Metal은 레지스터 수준, RTOS는 스레드 안전 패턴 |
| DMA | I8 | E6 | H9 | 🔁 원형 · 더블 버퍼 사용법, half / full 완료 → 태스크 통지, 캐시 정리(I8 일부) **주인 = RTOS E6** (11-01); DMAMUX 요청 매핑 · linked-list 디스크립터 · M2M · 우선순위 · 에러 레지스터는 Bare-Metal |
| 내부 플래시 · 워치독 | I9, I10 | J8, E8 | I9 (watchdog) | 🔁 워치독 전략(IWDG / WWDG 개념 · 윈도 · heartbeat 감시 · 리프레시 위치 · 디버그 정지)과 리셋 원인 기록(H3 일부) **주인 = RTOS E8** (11-01); IWDG / WWDG 레지스터 순서 · 리셋 소스 · 도메인은 Bare-Metal |
| CAN · USB · 이더넷 | I11, I12, I13 | J6, K4, K1 | I10, H12, I6 | |
| 인터럽트 지연 · 우선순위 | J1, J7, J8, M7 | E2, E3, E4 | N3 | 🔁 J8 우선순위 · 중첩 **주인 = RTOS E2**, J7 지연 · 지터 측정 + M7 측정 기법 **주인 = RTOS E3**, J1 ISR 설계 원칙 **주인 = RTOS E4** (11-01) |
| 실행 모델 (super-loop → 협력형) | A2, K1, K4, K5 | A2, C11 | — | 🔁 A2 · K1 · K4는 RTOS A2(10-25 작성)가 주인, K5 협력형 스케줄러는 RTOS C11(10-25) |
| bare-metal vs RTOS vs Linux | A4 | A7 | N4 | 🔁 **주인 = RTOS A7** (10-25 작성) |
| 부트로더 · OTA · secure boot | L1–L7 | L1–L4, L6 | C2–C5, L4, L5 | |
| 디버거 · 트레이스 · 크래시 수집 | M1, M2, M5, M9 | M2, M5, M6 | M4, M11, L7 | |
| 버그 패턴 사례집 | M10 | M7 | — | |
| 데이터시트 · 회로도 · bring-up | N1, N2, N8, O3 | — | P1, P3, P4 | |
| 가속기 제어 | O8 | — | O4 | Linux 드라이버 쪽은 EL |
| 메모리 배치 · TCM | P2, G6, Q7 | F8 | — | 🔁 TCM/SRAM 배치 · ramfunc · section attribute · noinit · ECC RAM 초기화(P2, G6 섹션 부분, Q7 초기화 부분) **주인 = RTOS F8** (11-01); 스크러빙 · SEU 처리(Q7)와 const · 초기화 순서(G6)는 Bare-Metal |
| 제어 루프 · FOC | P7 | Q3, Q4 | — | |
| 단위 테스트 · 시뮬레이션 · HIL | Q1, Q3, Q4 | M8, M9 | P6 | |
| MISRA · 안전 메커니즘 | Q5, Q6 | O3, O5 | — | |
| 메모리 안전 · Rust 인증 | Q9 | I11 | — | |

**다른 Don 노트와의 겹침** (링크 후보): Anduril C 문제은행(비트 연산, volatile, 링버퍼, FSM, CRC) · Hark Embedded AI 노트(E3 RISC-V, J4 C++/Rust) · OSTEP(가상 메모리 · 원자 연산) · Neros 저지연 백서(인터럽트 지연)

---

## 4. 모듈별 목차

### A. bare-metal 개념과 지형 — "OS가 없다는 것"

> OS가 해 주던 일(초기화 · 메모리 · 시간 · 에러 처리)을 **전부 펌웨어가 직접 책임진다**는 것부터 정한다.

| ID | 주제 | 핵심 개념 | 왜 필요 (인터뷰 / 실무) | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| A1 | bare-metal이란 | OS 없이 하드웨어 직접 소유, "펌웨어가 곧 OS", 책임 범위(startup · 클럭 · 인터럽트 · 메모리 · 에러 처리) | 출발점 정의 | P0 | ✅ | — |
| A2 | 실행 모델 지도 | super-loop, foreground/background, 이벤트 구동, time-triggered, 협력형, 인터럽트 전용(sleep-on-exit) | 설계 판단의 어휘 | P0 | ✅ | RTOS A2 |
| A3 | 펌웨어 개발 흐름 | 데이터시트 · RM → 레지스터 맵 → 드라이버 → 앱 → 측정 → 양산, 문서 종류(DS · RM · PM · AN · errata) | 업무 전체 그림 | P0 | ✅ | EL P3 |
| A4 | bare-metal vs RTOS vs Linux | 결정 기준: 지연 · 메모리 · 인증 · 전력 · 생태계 · 인력, 하이브리드(Linux + MCU) | 시스템 설계 단골 | P0 | ✅ | RTOS A7, EL N4 |
| A5 | 2026 MCU · SoC 지형 | STM32 · NXP(i.MX RT, MCX) · TI · Renesas RA · Nordic nRF54 · ESP32 · RP2350 · Microchip · Infineon, NPU MCU(STM32N6, Alif Ensemble), RISC-V MCU(ESP32-C, CH32V, GD32V) | 칩 고르기 · 회사 대화 | P1 | 🟡 | — |
| A6 | Cortex-A / R / 가속기 위의 bare-metal | boot ROM, 1단계 부트로더, 실리콘 검증 테스트 FW, 가속기 제어 FW, SSD 컨트롤러 실행부 | AI 칩 · 실리콘 진로 (O 모듈 입구) | P0 | ✅ | — |
| A7 | 벤더 SDK vs 레지스터 직접 | CMSIS · HAL · LL · 레지스터 직접의 장단점, 코드 생성기(CubeMX, MCUXpresso Config), "HAL 버그를 레지스터로 확인" | 실무 판단 | P1 | ✅ | RTOS J1 |

### B. 컴퓨터 구조 원천 — CPU가 명령을 실행하는 방식

> 레지스터 값이 "왜 늦게 반영되는지", 루프가 "왜 예상보다 느린지"는 전부 이 모듈에서 답이 나온다.

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| B1 | ISA와 명령 실행 | load/store RISC, 레지스터 파일, 명령 인코딩, 아키텍처 vs 마이크로아키텍처 | 모든 것의 바닥 | P0 | 🟡 | Hark E3 |
| B2 | 파이프라인 | 3단(M3 / M4) · 6단 dual-issue(M7) · M85, 해저드 · 스톨, 분기 예측 · 분기 비용, 명령별 사이클 수 | 사이클 단위 최적화 | P1 | 🟡 | — |
| B3 | 캐시 원리 | line · set-associative, write-back / write-through, allocate 정책, hit / miss 비용, 일관성 문제의 근원 | 고성능 MCU · A 코어 | P0 | ✅ | RTOS B8, EL H9 |
| B4 | 버스와 인터커넥트 | AHB · APB · AXI, bus matrix, master / slave, wait state, 브리지, 버스 오류가 뜻하는 것 | 성능 · BusFault 해석 | P0 | ✅ | — |
| B5 | 메모리 맵 | 4 GB 주소 공간 분할(Code · SRAM · Peripheral · Device · System), bit-banding(M3 / M4), alias 영역 | 레지스터 접근의 전제 | P0 | ✅ | — |
| B6 | 메모리 타입 · 순서 | Normal / Device / Strongly-ordered, 쓰기 버퍼 · write posting, "레지스터 쓰기가 늦게 반영" 현상 | 드라이버 레이스 | P0 | 🟡 | EL F5, RTOS D8 |
| B7 | 엔디언 · 정렬 | little / big endian, unaligned access 허용 범위(M0는 불가), 프로토콜 바이트 순서 | 이식성 · 정렬 fault | P1 | ✅ | — |
| B8 | 수 표현 | 2의 보수 · 오버플로, 고정소수점 Q 포맷, IEEE 754(subnormal · NaN · 반올림), 단정밀도 FPU의 한계 | DSP · 제어 · 센서 변환 | P0 | 🟡 | — |
| B9 | 플래시 · SRAM 기술 | 플래시 읽기 wait state, 프리페치 · ART 가속기, 플래시 수명 · ECC, SRAM 뱅크 · 리텐션, TCM | 성능 · 데이터 보존 | P1 | ✅ | RTOS B8 |

### C. Arm Cortex-M 아키텍처

> MCU 펌웨어의 공통어. RTOS B 모듈과 겹치는 항목이 많다 — 겹침 칸 참조.

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| C1 | 프로그래머 모델 | R0–R15, xPSR, Thread / Handler 모드, privileged / unprivileged, MSP / PSP, CONTROL | 모든 Cortex-M 질문의 전제 | P0 | 🟡 | RTOS B1 |
| C2 | Thumb-2 어셈블리 읽기 | 주요 명령(LDR · STR · B · BL · CMP), IT 블록, 조건 플래그, 디스어셈블리 · 리터럴 풀 읽기 | 컴파일러 출력 검증 · 크래시 분석 | P0 | 🟡 | — |
| C3 | AAPCS 호출 규약 | 인자 R0–R3, callee-saved R4–R11, 스택 8바이트 정렬, 반환값 · 구조체 반환, 가변 인자 | C ↔ asm 연결 · 스택 해석 | P0 | 🟡 | — |
| C4 | 예외 진입 · 복귀 | 벡터 테이블, 자동 스태킹(8워드), EXC_RETURN, tail-chaining, late-arrival, VTOR | "ISR을 C 함수로 쓸 수 있는 이유" | P0 | ✅ | RTOS B2 |
| C5 | NVIC | 우선순위 비트 수, PRIGROUP, PRIMASK · BASEPRI · FAULTMASK, pending · active 비트, 소프트웨어 트리거 | 인터럽트 설계의 기반 | P0 | 🟡 | RTOS B3 |
| C6 | 시스템 제어 블록(SCB) | CPUID, AIRCR(SYSRESETREQ · PRIGROUP), SCR(SLEEPDEEP · SLEEPONEXIT), CCR(unaligned trap · div0), CPACR(FPU enable), SHCSR | bring-up 필수 레지스터 | P0 | 🟡 | — |
| C7 | SysTick · DWT | SysTick 설정, DWT CYCCNT 켜기(DEMCR.TRCENA), 정확한 지연 함수 직접 구현 | 시간 측정의 기본 도구 | P0 | ✅ | RTOS B4, M6 |
| C8 | Fault 예외 | HardFault · MemManage · BusFault · UsageFault · SecureFault, escalation, lockup 상태, precise vs imprecise | 크래시 이해 | P0 | 🟡 | RTOS M1 |
| C9 | MPU (bare-metal 용도) | 리전 · 속성, 널 포인터 가드, 스택 가드, non-cacheable DMA 영역, XN | OS 없이도 쓰는 보호 | P1 | 🟡 | RTOS F4 |
| C10 | FPU · DSP · Helium | FPU 켜는 순서, lazy stacking, DSP 명령(SMLAD · SSAT · QADD), MVE beat 구조 | 신호 처리 성능 | P1 | 🟡 | RTOS B6 |
| C11 | Cortex-M 세대 · Armv8.1-M | M0+ ~ M85, v6-M / v7-M / v8-M / v8.1-M, PACBTI, LOB(저오버헤드 루프), 2026 신규 코어 없음 | 칩 고르기 · 포팅 차이 | P1 | 🟡 | RTOS B7 |
| C12 | TrustZone-M | Secure / Non-secure, SAU · IDAU, NSC veneer, 보안 상태 전환, bare-metal 두 이미지 구성 | Armv8-M 보안 질문 | P1 | ❌ | RTOS F6 |

### D. Arm Cortex-A / R bare-metal — SoC · AI 칩 bring-up 쪽

> Linux가 올라가기 **전** 코드, 또는 Linux 없이 도는 A/R 코어 코드. 실리콘 · AI 칩 직군에서 MCU와 함께 요구된다.

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| D1 | Armv8-A 예외 레벨 | EL0–EL3, Secure / Non-secure, ERET · SPSR · ELR, 레벨별 시스템 레지스터 | SoC bring-up의 전제 | P0 | 🟡 | EL C5 |
| D2 | AArch64 리셋 · startup | 리셋 벡터, MPIDR로 secondary 코어 정지, 스택 · BSS, EL3 → EL1 내려가기, VBAR 벡터 테이블 | 첫 코드 직접 쓰기 | P0 | ❌ | EL C6 |
| D3 | A64 어셈블리 · AAPCS64 | X0–X30, SP 16바이트 정렬, MRS / MSR 시스템 레지스터 접근, 예외 프레임 저장 | 부트 코드 읽기 | P1 | ❌ | — |
| D4 | MMU 직접 켜기 | 변환 테이블(4K granule · 레벨 · 블록 vs 페이지), MAIR · TCR · TTBR, identity map, 켜는 순서 | 캐시를 켜려면 MMU가 필요 | P0 | ❌ | EL F11 |
| D5 | 캐시 · TLB 유지 | DC CIVAC / CVAC / IVAC, IC IALLU, TLBI, PoC vs PoU, DSB / ISB 순서, 자기 수정 코드 | DMA · 가속기 일관성 | P0 | 🟡 | EL H9, F5 |
| D6 | GICv3 bare-metal 설정 | distributor · redistributor · CPU interface(ICC_* 레지스터), 그룹, 우선순위, SPI / PPI / SGI, ack · EOI | A 코어 인터럽트 | P0 | 🟡 | EL H6 |
| D7 | Generic Timer | CNTFRQ · CNTPCT, 물리 / 가상 타이머, 타이머 인터럽트 | 시간 · 지연 | P1 | ❌ | EL F8 |
| D8 | 멀티코어 기동 | PSCI CPU_ON, spin-table, 코어별 스택, 일관성 도메인 진입, LDAXR / STLXR 스핀락 | 멀티코어 bring-up | P1 | 🟡 | EL F4 |
| D9 | Cortex-R5 / R52 bare-metal | R 프로파일 startup, TCM ECC 초기화, MPU 설정, lockstep(DCLS), R52 EL2 | SSD · 자동차 · AI SoC 안전 코어 | P1 | ✅ | RTOS B9 |

### E. RISC-V bare-metal

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| E1 | ISA와 확장 | RV32I / E, M · A · F · D · C, Zicsr · Zifencei, Zba / Zbb / Zbs, Zc*, V, P(DSP), 프로파일(RVA23 = 애플리케이션용) | 코어 사양 읽기 | P1 | 🟡 | Hark E3, RTOS B10 |
| E2 | 특권 아키텍처 | M / S / U 모드, mstatus · mtvec(direct / vectored) · mcause · mepc · mtval · mie · mip, mret | 트랩 처리의 핵심 | P0 | ❌ | RTOS B10 |
| E3 | 인터럽트 컨트롤러 | CLINT(mtime · mtimecmp · msip), PLIC claim / complete, CLIC(벡터 · 중첩, 아직 비준 전), AIA(IMSIC · APLIC) | 인터럽트 설계 | P1 | ❌ | RTOS B10 |
| E4 | PMP / ePMP | TOR / NAPOT 영역, lock 비트, Smepmp | 보호 · 보안 | P1 | ❌ | RTOS B10 |
| E5 | RISC-V startup | `_start`, gp 설정(linker relaxation), 스택, 트랩 핸들러 저장 / 복원 어셈블리, crt0 | 첫 코드 직접 쓰기 | P0 | ❌ | — |
| E6 | 호출 규약 · 어셈블리 | ABI 이름(a0–a7 · s0–s11 · ra · sp), 압축 명령, 의사 명령 | 디스어셈블리 읽기 | P1 | ❌ | — |
| E7 | RISC-V 디버그 | Debug Module, abstract command, OpenOCD 설정, E-Trace / N-Trace | 디버거 연결 | P1 | ❌ | — |
| E8 | 벤더 코어 지형 | SiFive · Andes · Codasip · ESP32-C / P · RP2350 Hazard3 · WCH CH32V · OpenHW CORE-V · Ibex, 커스텀 확장 | 회사 · 칩 대화 | P2 | ❌ | — |

### F. 툴체인 · 빌드 · 링크 — 코드가 이미지가 되기까지

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| F1 | 툴체인 지형 | GNU Arm(arm-none-eabi), Arm Toolchain for Embedded(ATfE, LLVM 기반), IAR, Keil / armclang, riscv-none-elf, GCC 15부터 C 기본값 gnu23 | 도구 선택 · 이식 | P0 | 🟡 | EL B1 |
| F2 | 컴파일 → 오브젝트 | 전처리 · 컴파일 · 어셈블 · 링크, 섹션 · 심볼 · 재배치, nm · objdump · readelf | 빌드 산출물 읽기 | P0 | ✅ | EL B3 |
| F3 | 링커 스크립트 심화 | MEMORY / SECTIONS, VMA vs LMA(`AT>`), KEEP, ALIGN, PROVIDE, 여러 RAM 영역, C에서 링커 심볼 쓰기 | **bare-metal의 핵심 파일** | P0 | 🟡 | RTOS B5 |
| F4 | startup 코드 직접 쓰기 | C 배열 벡터 테이블, Reset_Handler, .data 복사 · .bss 0, SystemInit(클럭), `__libc_init_array` · C++ 생성자, main이 return하면 | 면접 · bring-up 단골 | P0 | ✅ | RTOS B5 |
| F5 | C 런타임 · libc | newlib / newlib-nano / picolibc / LLVM-libc, syscall stub(`_sbrk` · `_write`), semihosting, printf 크기 | 코드 크기 · 링크 에러 | P1 | 🟡 | RTOS B11, EL B2 |
| F6 | 최적화와 코드 크기 | -O 레벨 · -Os / -Oz, LTO, `-ffunction-sections` + `--gc-sections`, map 파일 분석(bloaty · puncover), 최적화가 타이밍을 바꾸는 경우 | 플래시에 넣기 | P0 | 🟡 | RTOS B11 |
| F7 | 빌드 시스템 | Make, CMake 툴체인 파일, Ninja, 재현 가능 빌드, git hash · 빌드 정보 넣기 | 팀 빌드 | P1 | ✅ | EL B4 |
| F8 | 이미지 후처리 | objcopy bin / hex / srec, CRC · 헤더 삽입, 서명 단계, 플래시 쓰기 도구 | 양산 · 부트로더 연동 | P1 | ✅ | — |
| F9 | 컴파일러 확장 | attribute(section · weak · alias · naked · interrupt · used · aligned · packed), 인라인 asm 제약, CMSIS intrinsics(`__DSB` 등), builtin | 저수준 코드 작성 | P0 | 🟡 | — |
| F10 | 링크 시점 기법 | weak 기본 핸들러, `--wrap` 함수 래핑, 위치 독립 펌웨어(PIC), 섹션으로 테이블 모으기(linker set) | 테스트 · 플러그인 구조 | P1 | 🟡 | — |

### G. 임베디드 C · 언어 원천

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| G1 | volatile 정확히 | 보장하는 것(매번 접근) vs 보장 안 하는 것(원자성 · 순서), 레지스터 · ISR 공유 변수, 남용의 비용 | **면접 최빈출** | P0 | ✅ | Anduril |
| G2 | 레지스터 접근 패턴 | 포인터 캐스트, struct 오버레이, CMSIS 장치 헤더, 비트필드 함정, read-modify-write, W1C / W1S, 읽으면 지워지는 레지스터 | 드라이버의 기본 | P0 | ✅ | — |
| G3 | UB · 최적화 함정 | 정수 승격, 부호 오버플로, strict aliasing, 평가 순서, 컴파일러가 루프 · 검사를 지우는 이유 | "-O2에서만 깨짐" | P0 | 🟡 | — |
| G4 | 비트 조작 관용구 | set / clear / toggle / test, 마스크 · 시프트 매크로, 필드 추출 · 삽입, popcount / clz | 화이트보드 단골 | P0 | ✅ | Anduril |
| G5 | 데이터 배치 · 직렬화 | stdint, packed struct, 패딩 · 정렬, 엔디언 변환, 프로토콜 프레임 구조 | 통신 · 저장 | P0 | ✅ | — |
| G6 | 저장 클래스 · 배치 | const → flash, static, 섹션 지정, 초기화 순서, ROM 테이블 | 메모리 예산 | P1 | ✅ | — |
| G7 | 설계 관용구 | 함수 포인터 테이블, 콜백, opaque 핸들, 테이블 구동, X-macro | 유지보수 가능한 FW | P1 | 🟡 | — |
| G8 | C11 / C23 | `_Static_assert`, stdatomic, alignas / C23: typeof · constexpr · nullptr · `#embed` · `_BitInt` · 이진 리터럴 · `[[attributes]]` | 2026 컴파일러 기본값 | P1 | ❌ | — |
| G9 | 임베디드 C++ | `-fno-exceptions` / `-fno-rtti`, constexpr · template 레지스터 추상화, RAII 크리티컬 섹션, 정적 할당, GCC 16의 C++20 기본값 | C++ 펌웨어 팀 | P1 | 🟡 | Hark J4 |
| G10 | Embedded Rust (레지스터 수준) | svd2rust PAC, typestate, volatile 접근, cortex-m-rt, embedded-hal 1.0, unsafe 경계 | 트렌드 · 메모리 안전 | P1 | ❌ | RTOS I7, Hark J4 |

### H. 칩 기반 — 클럭 · 리셋 · 전원

> "레지스터를 썼는데 아무 일도 없다"의 절반은 여기서 나온다.

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| H1 | 클럭 트리 | 내부 RC / 외부 크리스털, PLL, 버스 분주, 주변장치 클럭 선택 · enable, MCO로 클럭 확인 | 모든 주변장치의 전제 | P0 | ✅ | EL H3 |
| H2 | 클럭 올리는 순서 | 전압 스케일 → flash wait state → PLL lock → 전환, CSS(클럭 고장 감지) 복구, 순서를 틀린 증상 | bring-up 단골 버그 | P0 | ✅ | — |
| H3 | 리셋 | POR · BOR · 핀 · 소프트웨어 · 워치독 · lockup 리셋, 리셋 원인 레지스터, 리셋 도메인, 리셋 후 남는 것 | 필드 리셋 분석 | P0 | ✅ | RTOS E8 |
| H4 | 전원 구조 | 전원 도메인, LDO / SMPS, 전압 스케일링, 백업 도메인, PVD | 전력 · 안정성 | P1 | 🟡 | EL H3 |
| H5 | 저전력 모드 (레지스터 수준) | sleep / stop / standby, WFI / WFE, SLEEPONEXIT, 깨어난 뒤 클럭 복구 | 배터리 장비 | P1 | 🟡 | RTOS P1, EL N5 |
| H6 | 오실레이터 하드웨어 | 크리스털 부하 용량, 기동 시간, 32 kHz 크리스털 문제, 클럭 정확도(ppm)와 UART · USB · CAN | 하드웨어 협업 | P1 | 🟡 | — |
| H7 | option byte · fuse · OTP | 부트 설정, 읽기 보호, HW 워치독 시작, 되돌릴 수 없는 설정의 위험 | 양산 · 보안 | P1 | ✅ | RTOS L1, L6 |
| H8 | 클럭 · 전원 bring-up 확인 | MCO + scope, 소비 전류로 상태 확인, 첫 LED blink까지 체크리스트 | 새 보드 첫날 | P0 | ✅ | EL P1 |

### I. 주변장치 — 레지스터 수준 드라이버

> 프로토콜 설명은 EL H5 · H7 · H8에 있다. 여기서는 **MCU 레지스터 · 상태 플래그 · 에러 처리**를 다룬다.

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| I1 | GPIO | 모드 · AF · push-pull / open-drain · 풀업 · 속도(슬루), 원자 set / reset(BSRR), EXTI | 첫 드라이버 | P0 | ✅ | EL H4 |
| I2 | 타이머 | 프리스케일러 · ARR, PWM 모드, 입력 캡처 · 출력 비교, 엔코더 모드, 마스터 / 슬레이브 동기, 데드타임 | 모터 · 측정 · 타이밍 | P0 | ✅ | RTOS E5, J5 |
| I3 | UART | baud 계산 · 오차, 오버샘플링, 프레이밍 · 노이즈 · 오버런 에러, FIFO, idle 감지, RS-485 DE | 디버그 · 통신 | P0 | ✅ | EL H8, RTOS J4 |
| I4 | SPI | CPOL / CPHA, NSS HW / SW, FIFO · 데이터 크기, 전이중 DMA, 클럭 한계 | 센서 · 플래시 | P0 | ✅ | EL H7, RTOS J3 |
| I5 | I2C | START · ADDR · ACK · STOP 상태 머신, 타이밍 레지스터, clock stretching, 버스 행 복구, 멀티마스터 | 센서 · PMIC | P0 | ✅ | EL H5, RTOS J3 |
| I6 | ADC | SAR 원리, 샘플링 시간 · 입력 임피던스, 해상도 · ENOB, 스캔 / 연속 / 주입 채널, 교정, 오버샘플링 | 측정 · 제어 | P0 | 🟡 | RTOS J5 |
| I7 | DAC · 비교기 · OPAMP | 아날로그 주변장치, 출력 버퍼, 비교기 → 타이머 차단 | 전원 · 모터 보호 | P2 | 🟡 | — |
| I8 | DMA 컨트롤러 | 채널 · 스트림 · 요청 매핑(DMAMUX), 원형 · 더블 버퍼, M2M, linked-list 디스크립터, 우선순위 · 버스트, 에러 | 고속 데이터 경로 | P0 | ✅ | EL H9, RTOS E6 |
| I9 | 내부 플래시 프로그래밍 | unlock 순서, 페이지 / 섹터 지우기, 쓰기 단위 · ECC, 실행 중 쓰기(RWW · 듀얼뱅크), 에러 플래그 | 설정 저장 · 부트로더 | P0 | ✅ | RTOS J8 |
| I10 | 워치독 | IWDG / WWDG 레지스터, 디버그 정지 시 동작, 리프레시 위치 설계 | 필드 안정성 | P0 | ✅ | RTOS E8, EL I9 |
| I11 | CAN / FDCAN | 비트 타이밍(TQ · 샘플 포인트), 필터, 메시지 RAM, 에러 상태 · bus-off | 로봇 · 차량 | P1 | ❌ | RTOS J6, EL I10 |
| I12 | USB 디바이스 (레지스터) | PHY, 엔드포인트 버퍼, enumeration 흐름, 디스크립터, CDC 최소 구현 | 업데이트 · 디버그 포트 | P2 | 🟡 | EL H12, RTOS K4 |
| I13 | 이더넷 MAC / PHY | MDIO, RMII / RGMII, DMA 디스크립터 링, 링크 협상 | 산업 · 네트워크 장비 | P2 | 🟡 | EL I6, RTOS K1 |
| I14 | RTC · 백업 레지스터 · 저전력 타이머 | 캘린더 · 알람, LPTIM, tamper, 백업 도메인 | 저전력 · 시간 | P1 | 🟡 | — |
| I15 | 외부 메모리 | QSPI / OSPI 메모리 매핑(XIP), FMC SDRAM 초기화, HyperRAM · PSRAM | 큰 이미지 · 버퍼 | P1 | 🟡 | — |
| I16 | 보안 · 보조 주변장치 | CRC 유닛, TRNG, AES / HASH / PKA 가속기, 고유 ID | 무결성 · 보안 | P1 | 🟡 | RTOS L6 |

### J. 인터럽트 · 동시성 (OS 없이)

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| J1 | ISR 설계 원칙 | 짧게, 플래그 클리어 위치(늦게 지우면 재진입), 이벤트를 기록하고 메인에서 처리 | 매일 쓰는 규칙 | P0 | ✅ | RTOS E4 |
| J2 | 메인 ↔ ISR 데이터 레이스 | 찢어진 읽기(tearing), RMW 레이스, PRIMASK 저장 / 복원 크리티컬 섹션, BASEPRI | 숨은 버그 1순위 | P0 | ✅ | RTOS C7, D8 |
| J3 | 락 없는 링버퍼 | SPSC 인덱스 설계, 2의 거듭제곱 크기, 가득 참 / 빔 판정, 배리어 | 화이트보드 단골 | P0 | ✅ | RTOS D8, Anduril |
| J4 | 원자 연산 | LDREX / STREX · CLREX, C11 atomics(M3 이상), M0에서의 대체 | 플래그 · 카운터 | P1 | 🟡 | RTOS D8, EL F4 |
| J5 | 배리어가 필요한 순간 | 플래그 클리어 후 DSB(ISR 이중 진입), VTOR · CONTROL 변경 후 ISB, DMA 시작 전 DMB | "-O2에서만 깨짐"의 다른 원인 | P0 | 🟡 | EL F5, RTOS D8 |
| J6 | 폴링 vs 인터럽트 vs DMA | 판단 기준(속도 · 지연 · CPU 부하 · 전력), 혼합 방식 | 설계 질문 | P0 | ✅ | — |
| J7 | 인터럽트 지연 · 지터 측정 | GPIO 토글 + scope, DWT, 최악 경로 찾기 | 수치로 말하기 | P0 | ✅ | RTOS E3, EL N3 |
| J8 | 우선순위 · 중첩 설계 | 선점 그룹, 우선순위 배치표, 공유 자원과 우선순위 | 모터 · 고속 ISR | P1 | ✅ | RTOS E2 |

### K. 펌웨어 아키텍처 — OS 없이 구조 잡기

> RTOS를 쓰지 않을 때 코드가 스파게티가 되지 않게 하는 설계 도구들.

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| K1 | super-loop 구조 | 장단점, 최악 루프 시간, 타임 슬롯, 블로킹 코드가 만드는 문제 | 가장 흔한 구조 | P0 | ✅ | RTOS A2 |
| K2 | 유한 상태 기계 | switch / 테이블 / 함수 포인터 구현, 이벤트 큐, 진입 · 퇴장 동작 | 프로토콜 · 장치 제어 | P0 | ✅ | Anduril |
| K3 | 계층형 상태 기계(HSM) | UML statechart, 상태 중첩 · 전이 규칙, Samek QP | 복잡한 제어 로직 | P1 | 🟡 | — |
| K4 | 이벤트 구동 · Active Object | 이벤트 큐 + 실행 객체, QV 협력형 커널, run-to-completion | OS 없는 동시성 | P1 | ❌ | RTOS A2 |
| K5 | 협력형 · time-triggered 스케줄러 | 태스크 테이블, Pont TTC, 시간 예산 · 오버런 감지 | 안전 · 결정성 | P1 | 🟡 | RTOS C11 |
| K6 | C 코루틴 · protothreads | 스택 없는 코루틴, Duff's device 방식, 한계 | 작은 MCU의 대안 | P2 | ❌ | — |
| K7 | 계층화 · 이식성 | BSP / HAL / driver / service / app, 의존성 역전, 보드 변형 관리 | 코드베이스 설계 | P0 | ✅ | RTOS J1 |
| K8 | 에러 처리 전략 | 에러 코드, assert 정책, fault 기록, safe state, 재시도 · 복구 | 신뢰성 | P0 | ✅ | — |
| K9 | 설정 · 파라미터 · 버전 | 컴파일 시점 vs 런타임 설정, 보정(calibration) 데이터, 버전 정보 섹션 | 양산 · 필드 지원 | P1 | ✅ | — |
| K10 | Rust async bare-metal | Embassy 실행 모델 개요(단일 스택 · 컴파일 시점 상태 기계) | 트렌드 | P2 | ❌ | RTOS I8 |

### L. 부트로더 · 업데이트 · 보안

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| L1 | MCU 부트 모드 | ROM 부트로더, boot 핀, 시스템 메모리, 부트 주소 · 별칭 | bring-up · 복구 | P0 | ✅ | RTOS L1 |
| L2 | 커스텀 부트로더 작성 | 메모리 분할, 앱 점프(인터럽트 끄기 · 주변장치 정리 · VTOR · MSP · reset handler), 앱 → 부트로더 신호(noinit magic) | **면접 · 실무 단골** | P0 | ✅ | RTOS L1 |
| L3 | 이미지 무결성 | 헤더 · CRC / 해시 · 버전, 서명 검증(ECDSA · Ed25519), 롤백 카운터 | 안전한 업데이트 | P0 | 🟡 | RTOS L2 |
| L4 | 플래시 레이아웃 · 교체 전략 | 단일 / 듀얼 슬롯, 스왑, 듀얼뱅크 bank swap, 전원 차단 안전성 | 벽돌 방지 | P0 | ✅ | RTOS L2, EL L4, J5 |
| L5 | 업데이트 프로토콜 직접 구현 | UART XMODEM / YMODEM, USB DFU, CAN(UDS · ISO-TP) | 필드 업데이트 | P1 | ✅ | RTOS L3 |
| L6 | secure boot · 신뢰 루트 | ROM 검증, OTP 키 해시, 디버그 잠금 연계, A 코어 BL1 / BL2 | 보안 질문 | P1 | 🟡 | RTOS L4, EL C5, L5 |
| L7 | 하드닝 | RDP · readout 보호, JTAG 잠금, 글리치 · 사이드채널 기초 | 공격 표면 | P1 | 🟡 | RTOS L6 |
| L8 | 오픈 실리콘 신뢰 루트 | OpenTitan(2026-03 Chromebook 출하, SLH-DSA PQC secure boot), Caliptra(데이터센터 RoT), CHERIoT(능력 기반 메모리 안전 MCU) | 실리콘 · 보안 트렌드 | P2 | ❌ | — |

### M. 디버깅 · 측정 — 실무 생존 기술

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| M1 | SWD / JTAG 원천 | DP / AP, MEM-AP로 메모리 읽기, JTAG TAP 상태 머신, 스캔 체인 | 디버거가 없을 때 · 새 실리콘 | P1 | ✅ | EL M11, RTOS M6 |
| M2 | 디버거 실전 | GDB + OpenOCD / pyOCD / J-Link / probe-rs, 브레이크 · 워치포인트, SVD로 레지스터 보기 | 일상 | P0 | ✅ | RTOS M2, EL M4 |
| M3 | HardFault 해독 | CFSR · HFSR · MMFAR · BFAR, 스택 프레임에서 PC / LR, fault 핸들러 덤프 | 면접 · 현장 1순위 | P0 | 🟡 | RTOS M1 |
| M4 | printf 대체 출력 | ITM / SWO, SEGGER RTT, semihosting(느림 · 멈춤 주의), UART 로그 버퍼 | 로그 인프라 | P0 | ✅ | — |
| M5 | 명령 트레이스 | ETM · ETB · MTB, 트레이스 프로브, 사후 분석 | 재현 안 되는 버그 | P2 | 🟡 | RTOS M6 |
| M6 | 계측 장비 | 오실로스코프(트리거 · 대역폭 · 프로브), 로직 애널라이저 · 프로토콜 디코드, 전류 측정 | 하드웨어 경계 디버깅 | P0 | ✅ | — |
| M7 | 타이밍 측정 기법 | GPIO 토글, DWT CYCCNT, 타이머 스냅샷, 측정이 측정을 바꾸는 문제 | 수치로 말하기 | P0 | ✅ | RTOS E3 |
| M8 | 벽돌 복구 | connect under reset, 낮은 SWD 클럭, mass erase, boot 핀, option byte 복구 | 실무 생존 | P1 | ✅ | — |
| M9 | 사후 분석 | noinit 크래시 로그, 메모리 덤프, map 파일 · addr2line로 주소 → 심볼 | 필드 이슈 | P1 | ✅ | RTOS M5, EL L7 |
| M10 | 버그 패턴 사례집 (bare-metal) | 클럭 미활성, 플래그 클리어 누락, volatile 누락, 스택 오버플로, 정렬 fault, 캐시 + DMA, 최적화로 사라진 지연 루프 — 증상 → 원인 표 | 디버깅 면접 | P0 | ✅ | RTOS M7 |

### N. 보드 하드웨어 이해 — 회로 · 신호 · 데이터시트

> Don의 RF 통합 배경이 가장 빛나는 모듈. 대부분 **정리**만 하면 된다.

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| N1 | 문서 읽기 | 데이터시트 vs 레퍼런스 매뉴얼 vs 프로그래밍 매뉴얼 vs 에라타, 레지스터 표, 전기적 특성(절대 최대 · AC 타이밍) | 모든 작업의 시작 | P0 | ✅ | EL P3 |
| N2 | 회로도 읽기 | 전원 트리, 리셋 · boot 핀, 디버그 헤더, 풀업 / 풀다운, 테스트 포인트 | bring-up | P0 | ✅ | EL P4 |
| N3 | 디지털 I/O 전기 | CMOS 레벨, 오픈 드레인, 레벨 시프트, 슈미트 트리거, 구동 전류, 5 V 허용 | 핀 설정 판단 | P0 | ✅ | — |
| N4 | 신호 무결성 기초 | 상승 시간, 반사 · 링잉, 종단, 크로스토크, 긴 SPI 배선 | 고속 인터페이스 | P1 | ✅ | — |
| N5 | 전원 무결성 · ESD · EMI | 디커플링, 브라운아웃, 접지, ESD 보호, EMC 시험 실패 대응 | 랜덤 리셋 · 인증 | P1 | ✅ | — |
| N6 | 아날로그 프런트엔드 | ADC 앞단 RC, 안티앨리어싱, 기준 전압, 노이즈 · 접지 분리 | 측정 품질 | P1 | 🟡 | — |
| N7 | 레지스터 기술 파일 | CMSIS-SVD, CMSIS-Pack, 장치 헤더 생성 | 도구 · 헤더 출처 | P1 | 🟡 | — |
| N8 | HW–FW 협업 | bring-up 체크리스트, 하드웨어 버그 vs 펌웨어 버그 가르기, rework, 에라타 우회 | 시니어 역할 | P0 | ✅ | EL P1 |

### O. 실리콘 bring-up · 검증 펌웨어 — AI 칩 · 실리콘 진로

> MCU를 **쓰는** 쪽에서 칩을 **만드는** 쪽으로 가는 다리. Rivian 실리콘 · 센싱, AI 가속기 회사가 보는 영역.

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| O1 | pre-silicon 펌웨어 | RTL 시뮬레이션 · FPGA 프로토타입 · 에뮬레이터(Palladium · ZeBu), Verilator, 가상 플랫폼(QEMU · Renode · FVP) | 칩 나오기 전 FW 개발 | P1 | ❌ | — |
| O2 | 레지스터 맵 단일 원천 | SystemRDL · IP-XACT, PeakRDL로 C 헤더 · UVM · 문서 생성 | HW / FW 일관성 | P1 | ❌ | — |
| O3 | post-silicon bring-up 순서 | 첫 JTAG 연결 → ROM 확인 · 우회 → SRAM 테스트 → 클럭 · PLL → DDR → 주변장치, power-on 체크리스트 | **실리콘 직군 핵심** | P0 | 🟡 | EL P1 |
| O4 | 검증 테스트 펌웨어 | directed test, self-checking 테스트, 레지스터 reset 값 · RW 테스트, 메모리 테스트(March C-), 인터럽트 · DMA 스트레스 | 검증 FW 작성 | P0 | 🟡 | — |
| O5 | DDR / LPDDR 초기화 | 컨트롤러 · PHY, training(write leveling · read gate · eye), 파라미터 출처 | SoC bring-up 병목 | P1 | 🟡 | — |
| O6 | HW / SW 공동 검증 | cocotb, DPI · co-simulation, DV 테스트를 FW로 재사용, pre / post-silicon 테스트 공유(PSS) | 검증 효율 | P2 | ❌ | — |
| O7 | 양산 · 특성화 테스트 | BIST · scan · ATE, 온도 · 전압 코너, 수율 데이터, 퓨즈 프로그래밍 | 양산 협업 | P2 | 🟡 | — |
| O8 | 가속기 bare-metal 제어 | 명령 큐 · doorbell · 디스크립터 · 완료 인터럽트, 캐시 일관성, 성능 카운터, FW–드라이버 인터페이스 | AI 칩 FW | P1 | ✅ | EL O4 |
| O9 | 실리콘 디버그 | 하드웨어 버그 재현 최소화, 에라타 작성, workaround 패턴, 칩 리비전 대응 | 시니어 역할 | P1 | ✅ | — |

### P. 성능 · 저전력 · 수치 연산

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| P1 | 성능 측정 · 프로파일 | 사이클 측정, DWT PC 샘플링, 핫스팟 찾기 | 최적화의 시작 | P0 | ✅ | RTOS M4 |
| P2 | 실행 위치 최적화 | flash vs RAM vs TCM 실행, 캐시 켜기, 정렬, ramfunc | 고속 ISR · 루프 | P1 | ✅ | RTOS F8 |
| P3 | 컴파일러 출력 읽고 최적화 | 루프 · 인라인 · 분기 제거, restrict, Compiler Explorer | 근거 있는 최적화 | P1 | 🟡 | — |
| P4 | 메모리 예산 | flash / RAM 예산표, 스택 + 힙 + 정적, 빌드마다 추적 | 설계 · 리뷰 | P0 | ✅ | — |
| P5 | 고정소수점 연산 | Q15 / Q31, 포화 연산, 스케일링, 오차 분석 | DSP · 제어 | P0 | 🟡 | — |
| P6 | DSP 기초 알고리즘 | FIR / IIR, 이동 평균, CORDIC, LUT + 보간, CMSIS-DSP | 센서 처리 | P1 | 🟡 | — |
| P7 | 제어 루프 구현 | PID 이산화, anti-windup, 샘플링 주기 · 지연, FOC 개관 | 모터 · 드론 | P1 | 🟡 | RTOS Q3, Q4 |
| P8 | 저전력 설계 | 전류 예산, duty cycling, 주변장치 끄기, 미사용 핀 상태, 누설 | 배터리 장비 | P1 | 🟡 | RTOS P1, P2 |
| P9 | 에너지 측정 | PPK2 · Joulescope, 평균 전류 계산, 배터리 수명 추정 | 수치로 말하기 | P2 | 🟡 | RTOS P3 |

### Q. 테스트 · 신뢰성 · 안전

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| Q1 | 호스트 단위 테스트 · TDD | Unity · CMock · Ceedling, Grenning TDD, 이중 타깃 빌드 | 테스트 문화 | P0 | 🟡 | RTOS M8 |
| Q2 | 테스트 가능한 HW 추상화 | 레지스터 모킹, 링크 시점 치환, 가짜 주변장치 | 드라이버 테스트 | P1 | 🟡 | — |
| Q3 | 시뮬레이터 | QEMU · Renode · Unicorn · FVP, 주변장치 모델 작성 | 보드 없는 CI | P1 | 🟡 | RTOS M9 |
| Q4 | HIL 리그 | 장비 제어, 자동 플래시 · 리셋, 결과 수집, CI 연결 | **FW Test 직군(Neros) 핵심** | P0 | 🟡 | RTOS M9, EL P6 |
| Q5 | 정적 분석 · 코딩 표준 | `-Wall -Wextra`, clang-tidy · cppcheck, MISRA C:2023, CERT C | 품질 게이트 | P1 | 🟡 | RTOS O3, M10 |
| Q6 | 방어적 펌웨어 | 시작 시 RAM · flash CRC 자가 테스트, 스택 카나리, 레지스터 재검증, 제어 흐름 검사 | 안전 · 신뢰성 | P1 | 🟡 | RTOS O5 |
| Q7 | 메모리 오류 · ECC | SRAM ECC 초기화, 스크러빙, 단일 / 이중 비트 오류 처리, SEU | 고신뢰 장비 | P1 | ✅ | — |
| Q8 | 펌웨어 퍼징 | 호스트 퍼징(libFuzzer), 에뮬레이션 기반 퍼징(Fuzzware · LibAFL QEMU) | 보안 테스트 | P2 | ❌ | — |
| Q9 | 메모리 안전 트렌드 | CISA 메모리 안전 로드맵(2026-01-01 목표, 권고), Rust 전환, CHERIoT, Ferrocene core 인증 | 2026 대화 | P2 | ❌ | RTOS I11 |

### R. 인터뷰 준비

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| R1 | bare-metal 개념 Top 50 | 모듈 A–Q에서 뽑은 단골 질문 + 30초 · 2분 답 | 1차 기술 면접 | P0 | ❌ | RTOS R1, EL R1 |
| R2 | "무슨 일이 일어나나" 시리즈 | 전원 → main, 인터럽트 → ISR → 복귀, 레지스터 쓰기 → 핀 변화, printf → UART 핀 | 깊이 확인 질문 | P0 | ❌ | RTOS R2 |
| R3 | 화이트보드 코딩 | 레지스터 매크로, 비트 연산, 링버퍼, 디바운스, CRC, 고정소수점 연산, memcpy | 라이브 코딩 | P0 | ✅ | Anduril |
| R4 | 디버깅 시나리오 | 주변장치 무반응, 랜덤 리셋, 클럭 틀어짐, 최적화하면 깨짐, 보드마다 다름 | 실무 감각 평가 | P0 | ✅ | RTOS R4 |
| R5 | 시스템 설계 | 센서 노드 · 모터 컨트롤러 · 부트로더 · 데이터 로거: 블록도 + 메모리 · 전력 · 타이밍 예산 | 시니어 라운드 | P1 | 🟡 | RTOS R5 |
| R6 | Don 스토리 브리지 | SSD FW · RF bring-up · 실리콘 경험을 bare-metal / 실리콘 검증 언어로 | 행동 면접 | P0 | ✅ | RTOS R6, EL R6 |

### S. 실습 프로젝트 — 손으로 증명하기

| ID | 주제 | 핵심 개념 | 왜 필요 | 우선 | Don | 겹침 |
|---|---|---|---|---|---|---|
| S1 | 랩 만들기 | QEMU(mps2-an385 = M3, virt AArch64, RISC-V virt), Renode / 선택: 실제 보드(Nucleo, Pico 2, ESP32-C3) + 디버그 프로브 | 모든 실습의 기반 | P0 | ❌ | RTOS S1 |
| S2 | 0에서 blink | 벤더 코드 없이 링커 스크립트 + startup + 레지스터로 LED · UART | **원천 이해의 증명** | P0 | ❌ | — |
| S3 | 인터럽트 UART 드라이버 | ISR + 링버퍼 + 명령 셸 | I3 · J 체득 | P0 | ❌ | — |
| S4 | 타이머 트리거 ADC + DMA | 더블 버퍼 샘플링, 고정소수점 필터 | I6 · I8 · P5 체득 | P1 | ❌ | — |
| S5 | 커스텀 부트로더 | 앱 점프, CRC 검증, UART 업데이트, 두 슬롯 | L 체득, 포트폴리오 | P0 | ❌ | RTOS S6 |
| S6 | AArch64 bare-metal | QEMU virt에서 EL 전환, MMU · 캐시 켜기, GIC 타이머 인터럽트 | D 체득, 실리콘 직군 | P1 | ❌ | — |
| S7 | RISC-V bare-metal | 트랩 핸들러, CLINT 타이머, PLIC | E 체득 | P1 | ❌ | — |
| S8 | HardFault 핸들러 + 크래시 로그 | noinit 로그, 사후 분석 스크립트 | M3 · M9 체득 | P0 | ❌ | — |
| S9 | 호스트 테스트 + Renode CI | 드라이버 단위 테스트, 시뮬레이션 회귀 | Q 체득 | P1 | ❌ | — |
| S10 | 미니 검증 펌웨어 | SystemRDL → 헤더 생성, 레지스터 reset · RW 테스트, March C- 메모리 테스트 | O 체득, AI 칩 포트폴리오 | P1 | ❌ | — |

---

## 5. 2026 트렌드 지도 (2026-10-18 웹 확인)

| 트렌드 | 확인한 사실 | 관련 항목 |
|---|---|---|
| 툴체인 기본값 변화 | GCC 15부터 C 기본 `-std=gnu23`, GCC 16.1(2026-04-30)은 C++ 기본을 C++20으로, `_BitInt`가 Arm · RISC-V로 확대, `counted_by` 포인터 지원 | F1, G8, G9 |
| LLVM 기반 Arm 툴체인 | Arm Toolchain for Embedded(ATfE) 2025 출시, 상용판 ATfEP 22.1.0이 Development Studio 2026.0(2026-06)에 포함, LLVM-libc 평가용 오버레이 | F1, F5 |
| 메모리 안전 압력 | CISA · FBI *Product Security Bad Practices*: 2026-01-01까지 메모리 안전 로드맵 권고(강제 아님) · Ferrocene 26.02: Rust `core` 일부가 IEC 61508 SIL 2 + ISO 26262 ASIL B 인증(함수 5,169개) | G10, Q9 |
| CHERI 하드웨어 메모리 안전 | SCI ICENI(CHERIoT-Ibex, RV32E, GlobalFoundries 생산) 첫 실리콘 2026 목표, 고성능 CHERIoT Kudu 코어 발표(2026-04) — 양산 출하는 아직 확인 안 됨 | L8, Q9 |
| 오픈 실리콘 신뢰 루트 | OpenTitan(Earl Grey, Nuvoton 생산) 2026-03 Chromebook 출하, SLH-DSA PQC secure boot · Caliptra = 데이터센터 칩 RoT 표준화 진행 | L8, O |
| RISC-V 표준화 | RVA23 비준(애플리케이션 프로파일), MCU용 CLIC는 아직 로드맵 단계 | E1, E3 |
| Cortex-M 세대 | 2026 신규 Cortex-M 코어 발표 확인 안 됨 → 최신은 M52 · M55 · M85(v8.1-M, Helium, PACBTI) | C10, C11 |
| NPU 탑재 MCU | Ethos-U85, STM32N6, Alif Ensemble (2026 이전 사실, RTOS §5 참조) | A5, RTOS Q6 |
| 시뮬레이션 우선 개발 | QEMU · Renode · Arm FVP로 보드 없이 CI (RTOS §5 참조) | Q3, S9 |
| 규제 | EU CRA 신고 의무 2026-09-11 시행 (RTOS L7 참조) | L6, L7 |

---

## 6. 학습 순서 (참고용 — 실제 순서는 Don이 정함)

| 단계 | 모듈 | 끝났을 때 할 수 있는 것 |
|---|---|---|
| 원천 | A, B, C, F, G + S1 · S2 | 벤더 코드 없이 칩을 살리고, C 한 줄이 핀까지 가는 길을 설명 |
| 칩 · 주변장치 | H, I, J, K + S3 · S4 | 레지스터 수준 드라이버와 OS 없는 구조를 근거 있게 설계 |
| 시스템 | L, M, P + S5 · S8 | 부트로더 · 디버깅 · 성능 · 전력 문제를 수치로 해결 |
| 실리콘 · 확장 | D, E, N, O + S6 · S7 · S10 | A 코어 · RISC-V bring-up과 검증 FW 대화 |
| 품질 · 면접 | Q, R + S9 | 테스트 · 안전 대화, 면접 답변 |

> **P0만 먼저 보려면** (91개, 그중 Don ✅가 절반 이상 — ✅는 정리만): A1–A4, A6, B1, B3–B6, B8, C1–C8, D1, D2, D4–D6, E2, E5, F1–F4, F6, F9, G1–G5, H1–H3, H8, I1–I6, I8–I10, J1–J3, J5–J7, K1, K2, K7, K8, L1–L4, M2–M4, M6, M7, M10, N1–N3, N8, O3, O4, P1, P4, P5, Q1, Q4, R1–R4, R6, S1–S3, S5, S8

---

## 7. 참고 자료 (노트 작성 시 1차 출처)

**무료 · 공식**
- Arm: *Armv7-M / Armv8-M Architecture Reference Manual*, *Arm Architecture Reference Manual for A-profile*, *Learn the architecture* 시리즈(예외 모델 · MMU · GIC · 캐시)
- RISC-V: *Unprivileged / Privileged ISA* 스펙, CLIC · AIA 초안, Debug 스펙
- GNU `ld` 문서(링커 스크립트), GCC 매뉴얼(attribute · inline asm), CMSIS 문서
- STM32 레퍼런스 매뉴얼(RM0090 · RM0433 등) · NXP i.MX RT RM · 에라타 시트
- Memfault *Interrupt* 블로그(zero-to-main, HardFault, 부트로더, 링커 스크립트 글), Embedded Artistry
- SystemRDL 2.0 스펙 · PeakRDL 문서, cocotb 문서, OpenTitan 문서

**책**
- Joseph Yiu, *The Definitive Guide to Arm Cortex-M3/M4* · *Cortex-M23/M33* — C 모듈 축
- Patterson & Hennessy, *Computer Organization and Design (RISC-V판)* · Harris & Harris, *Digital Design and Computer Architecture* — B · E 모듈 축
- Elecia White, *Making Embedded Systems* 2nd ed. — 실무 전반
- James Grenning, *Test-Driven Development for Embedded C* — Q 모듈
- Miro Samek, *Practical UML Statecharts in C/C++* — K 모듈
- Michael Pont, *Patterns for Time-Triggered Embedded Systems* — K5
- Jack Ganssle, *The Art of Designing Embedded Systems* — 설계 · 디버깅
- Howard Johnson, *High-Speed Digital Design* — N4 · N5

**2026 트렌드 확인 출처 (2026-10-18)**
- GCC 16.1 https://gcc.gnu.org/pipermail/gcc-announce/2026/000190.html · 표준 기본값 https://gcc.gnu.org/onlinedocs/gcc-16.2.0/gcc/Standards.html
- Arm Development Studio 2026.0 / ATfEP https://developer.arm.com/community/arm-community-blogs/b/tools-software-ides-blog/posts/developmentstudio20260 · ATfE https://docs.zephyrproject.org/latest/develop/toolchains/arm_toolchain_for_embedded.html
- CISA 메모리 안전 로드맵 https://techrepublic.com/article/cisa-fbi-memory-safety-recommendations
- Ferrocene 26.02 https://ferrous-systems.com/blog/ferrocene-26-02-0/
- CHERIoT ICENI https://cheri-alliance.org/iceni-first-look/ · Kudu https://cheri-alliance.org/wp-content/uploads/2026/04/D2-1.7.1-David-Chisnall-CHERIoT-Kudu-A-high-performance-CHERIoT-microcontroller-CHERI-Blossoms-2026.pdf
- OpenTitan 출하 https://opensource.googleblog.com/2026/03/opentitan-shipping-in-production.html
- RISC-V CLIC 상태 https://wiki.riscv.org/x/VwD5Ag

**Don 기존 노트 (겹침 링크 대상)**
- [Embedded Linux 나침반](../../../EmbeddedLinux/site/compass/contents.html) · [RTOS 나침반](../../../RTOS/site/compass/contents.html)
- Anduril C 문제은행(비트 연산 · volatile · 링버퍼 · FSM · CRC), Hark Embedded AI 노트(E3 RISC-V, J4 C++/Rust)

---

## 8. 진행 현황

| 모듈 | 항목 수 | 노트 작성 | 읽음 | 메모 |
|---|---|---|---|---|
| A bare-metal 개념과 지형 | 7 | 0 | 0 | |
| B 컴퓨터 구조 원천 | 9 | 0 | 0 | |
| C Arm Cortex-M 아키텍처 | 12 | 0 | 0 | |
| D Cortex-A / R bare-metal | 9 | 0 | 0 | |
| E RISC-V bare-metal | 8 | 0 | 0 | |
| F 툴체인 · 빌드 · 링크 | 10 | 0 | 0 | |
| G 임베디드 C · 언어 원천 | 10 | 0 | 0 | |
| H 칩 기반 — 클럭 · 리셋 · 전원 | 8 | 0 | 0 | |
| I 주변장치 — 레지스터 수준 드라이버 | 16 | 0 | 0 | |
| J 인터럽트 · 동시성 | 8 | 0 | 0 | |
| K 펌웨어 아키텍처 | 10 | 0 | 0 | |
| L 부트로더 · 업데이트 · 보안 | 8 | 0 | 0 | |
| M 디버깅 · 측정 | 10 | 0 | 0 | |
| N 보드 하드웨어 이해 | 8 | 0 | 0 | |
| O 실리콘 bring-up · 검증 펌웨어 | 9 | 0 | 0 | |
| P 성능 · 저전력 · 수치 연산 | 9 | 0 | 0 | |
| Q 테스트 · 신뢰성 · 안전 | 9 | 0 | 0 | |
| R 인터뷰 준비 | 6 | 0 | 0 | |
| S 실습 프로젝트 | 10 | 0 | 0 | |
| **합계** | **176** | **0** | **0** | 2026-10-18 목차 작성 — 겹침은 표시만, 주인은 writer 의뢰 시 결정 |
