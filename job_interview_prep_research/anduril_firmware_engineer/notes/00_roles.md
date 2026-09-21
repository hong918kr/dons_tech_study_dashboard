# Anduril 펌웨어/임베디드 엔지니어 — 역할 인텔 브리핑

> 스터디 대시보드용 역할 정리 (Korean-primary). 데이터 원본: `data/roles.json`
> 참고: 공개 정보 기반 재구성이며, 실제 채용 공고와 세부 수치는 변동될 수 있음.

---

## 한 줄 요약

Anduril 펌웨어/임베디드 엔지니어링은 **자율 무기·감시 시스템(드론, 요격체, AUV, 자율전투기, 순항미사일, 감시탑)의 하드웨어를 실제로 동작시키는 저수준 소프트웨어**를 만든다. C/C++(및 점점 Rust)로 베어메탈·RTOS·임베디드 리눅스 위에서 모터·전력·센서·통신 서브시스템을 제어하고, 각종 버스를 브링업하며, JTAG·오실로스코프·로직분석기로 하드웨어를 디버깅한다.

---

## 1. 사업부(Divisions) & 제품

| 사업부 | 대표 제품 | 펌웨어 관점 핵심 |
|---|---|---|
| **Air (sUAS / Counter-UAS)** | Ghost, Bolt / Bolt-M, Anvil / Anvil-M, Roadrunner / Roadrunner-M, Altius | 비행 제어(flight controller), 모터 제어(ESC/BLDC), IMU/GPS 센서 융합, 저지연 무선 링크 |
| **Autonomous Systems / Maritime** | Dive-LD / Dive-XL, Ghost Shark(호주), Copperhead | 저전력 임베디드 리눅스, 추진/조향 액추에이터, 수중 음향·관성 항법, 에너지 관리 |
| **Air Dominance (CCA)** | Fury / YFQ-44A | 항공 등급 실시간 시스템, 안전(DO-178 스타일), 고신뢰 버스(1553/ARINC) |
| **Strike / Munitions** | Barracuda, Menace-family | 유도항법제어(GNC), 세이프&암·신관, 텔레메트리, 저비용 대량생산 설계 |
| **Sensing / Ground (Sentry & EW)** | Sentry Tower, 레이더/EO-IR, Pulsar(전자전) | 센서 프론트엔드 제어, PTZ 짐벌, 전력·열 관리, RF 데이터 경로 |
| **Lattice OS / Platform** | Lattice OS, Menace(C2), 공통 컴퓨트 모듈 | 엣지 컴퓨트 보드 브링업, 드라이버, 부트로더, OTA, HAL |

---

## 2. 역할(Roles) & 레벨

### Firmware Engineer — Mid (IC / L3–L4)
- **위치:** Costa Mesa, CA(본사) / Seattle / Atlanta / Boston
- **연봉:** $130k–$180k + equity + bonus · **경력:** 3–6년
- 베어메탈·RTOS(FreeRTOS/Zephyr) 위 C/C++ 개발
- I2C/SPI/UART/CAN 저수준 드라이버 브링업·디버깅
- 보드 브링업, 스키매틱 리뷰, HW 팀 협업
- 오실로스코프/로직분석기/JTAG 하드웨어 디버깅

### Senior Firmware Engineer — Senior (IC / L5)
- **위치:** Costa Mesa / Seattle / Washington DC 인근
- **연봉:** $168k–$230k + equity + bonus · **경력:** 6–10년
- 서브시스템(모터제어·전력관리·항법센서) 아키텍처 소유
- 실시간·안전·신뢰성 요구사항 정의·검증
- 부트로더·OTA·secure boot 설계
- 주니어 멘토링, 크로스팀 인터페이스 정의

### Staff / Principal Firmware Engineer — Staff+ (IC / L6–L7)
- **위치:** Costa Mesa / 원격 협의
- **연봉:** $205k–$280k + significant equity · **경력:** 10년+
- 다중 제품 펌웨어 플랫폼·HAL·툴체인 표준 정의
- SoC/FPGA(Zynq) 통합, 파워·열·EMI 시스템 트레이드오프
- 안전·사이버보안·공급망(부품 EOL) 전략
- 기술 로드맵·전사 리뷰 게이트 소유

### Embedded Software Engineer — Mid–Senior (IC)
- **위치:** Costa Mesa / Boston / Seattle
- **연봉:** $140k–$210k + equity · **경력:** 4–8년
- 임베디드 리눅스(Yocto/Buildroot), Rust/C++ 애플리케이션 계층
- 디바이스 드라이버, 커널 모듈, 센서/액추에이터 통합
- 자율성 스택 ↔ 하드웨어 미들웨어(DDS/gRPC) 브리지
- CI/HIL 테스트 자동화

---

## 3. 핵심 역량(Core Skills) & 스터디 매핑

| 역량 | 설명 | 연결 토픽 |
|---|---|---|
| 베어메탈·RTOS 펌웨어(C/C++) | 인터럽트·DMA·타이머·스케줄링 | c_pointers_memory, rtos_scheduling, interrupts_dma, concurrency_locking |
| 시리얼 버스 드라이버 브링업·디버그 | I2C/SPI/UART/CAN 신호 레벨 디버깅 | i2c_spi_uart, can_canfd, logic_analyzer_scope, register_level_programming |
| 보드 브링업·스키매틱 독해 | 데이터시트·HW 협업 | board_bringup, power_sequencing, clock_reset, datasheet_reading |
| 모터·전력·센서 제어 | BLDC/ESC, PMIC, IMU/GPS 융합 | motor_control_pwm, power_management, sensor_fusion, control_loops_pid |
| 실시간·안전 설계 | watchdog, fail-safe, redundancy | real_time_constraints, watchdog_faulthandling, safety_state_machines, testing_hil |
| 부트로더·OTA·secure boot | 서명 검증·업데이트 흐름 | bootloader_flash, ota_update, secure_boot_crypto, memory_map_linker |
| 디버그 툴링 | JTAG/SWD, GDB, scope | jtag_swd_debug, gdb_openocd, trace_profiling, hardware_debug |
| 임베디드 Rust | 메모리 안전 시스템 코드(Anduril 적극 채택) | rust_embedded, memory_safety, no_std, hal_abstraction |

---

## 4. 프로토콜 / 버스

`I2C` · `SPI` · `QSPI` · `UART/RS-232/422/485` · `CAN` · `CAN-FD` · `USB` · `Ethernet(TSN/gigE)` · `PCIe` · **`MIL-STD-1553`** · **`ARINC-429`** · **`ARINC-825(CAN)`** · `MAVLink` · `DDS` · `gRPC/Protobuf`

> 방산 특화: **MIL-STD-1553, ARINC-429/825** 는 상용 임베디드에서 잘 안 보이므로 별도 학습 필요.

## 5. MCU / 코어

ARM Cortex-M(STM32, NXP i.MX RT) · Cortex-A(임베디드 리눅스) · Cortex-R(안전/실시간) · **Xilinx Zynq / Zynq UltraScale+ (FPGA+ARM SoC)** · TI C2000(모터/실시간) · NXP i.MX · RISC-V(일부 신규)

## 6. 디버그 도구

JTAG/SWD · SEGGER J-Link · ST-Link · OpenOCD · GDB · 오실로스코프 · 로직분석기(Saleae) · Lauterbach TRACE32 · CAN 분석기(PCAN/Vector) · Yocto/Buildroot · HIL 테스트 리그

---

## 7. Clearance & ITAR (필독)

- **Clearance:** 대부분 역할이 **US Person(시민/영주권자)** 요구. 다수 포지션이 **Secret/Top Secret 인가 취득 가능 자격**을 요구하며, 일부 프로그램은 채용 시점 기존 clearance 보유 필수.
- **ITAR:** Anduril 제품은 **ITAR 및 수출통제 대상**. 기술 데이터 접근을 위해 **US Person 자격(시민권/영주권/보호 개인)** 충족 필수.

---

## 8. Don 관점 준비 로드맵

Don은 **SSD 펌웨어 7년 + PCIe/I2C/SPI/RFFE 브링업·시스템 디버깅** 경험이 있어 직결도가 높다. 권장 순서:

1. **C 기초 재정비** — 포인터/메모리/동시성 + RTOS 스케줄링
2. **버스 프로토콜 심화** — 각 버스의 레지스터 레벨·타이밍·엣지케이스 정리
3. **부팅 체인** — 부트로더 / secure boot / OTA 흐름
4. **실시간·안전 설계** — fail-safe 상태머신 패턴
5. **임베디드 Rust 기초** — no_std, HAL
6. **방산 특화** — MIL-STD-1553 / ARINC, ITAR·clearance 요건 숙지
