# 퀀트 회사 로우레벨 · 임베디드 소프트웨어 직군 로드맵 (2026-11-08)

트레이딩 회사(HFT · 마켓메이커)에는 퀀트 리서처나 트레이더가 아닌 **로우레벨 엔지니어 자리**가 따로 있다. 주문이 거래소에 닿기까지 걸리는 시간을 나노초 단위로 줄이는 일이라, **펌웨어 · 드라이버 · 하드웨어 경험이 그대로 강점**이 된다. 이 문서는 임베디드 경력에서 출발해 그 직군으로 가는 경로를 스터디 노트 아이템으로 짠 것이다.

> 범위: 퀀트 리서치(알파 · 통계 모델)는 다루지 않는다. 확률 퀴즈는 대부분 회사가 엔지니어에게도 묻기 때문에 마지막 단계에 최소한만 넣었다.
>
> 속도 가정: 주 15개(하루 3개 × 주 5일). 전체 핵심 경로 약 120개 → **약 4개월**.

## 어떤 자리들이 있나

| 직군 (흔한 이름) | 하는 일 | 면접에서 보는 것 | 임베디드 경력이 통하는 지점 |
|---|---|---|---|
| **Low-Latency C++ / Trading Systems Engineer** | 시세 수신 → 전략 → 주문 송신 경로의 C++ 코드 | C++ 깊이, 캐시 · 메모리 순서, lock-free, 리눅스 튜닝, 코딩 테스트 | 할당 없는 코드, ISR 같은 hot path 감각, 측정 습관 |
| **FPGA Engineer** | 시세 파싱 · 주문 생성을 하드웨어로 (수십 ns) | RTL 설계 · 검증, 파이프라이닝, 타이밍 클로저, 프로토콜 파싱 | 레지스터 · 버스 · 타이밍 다이어그램, 실리콘 검증 경험 |
| **Network / Hardware Engineer** | NIC · 스위치 · 시간 동기 · 코로케이션 인프라 | 커널 바이패스, 타임스탬핑, PTP, 패킷 경로, 하드웨어 디버깅 | NVMe 큐 · 도어벨 = NIC 큐 · 도어벨, RF · 계측 경험 |
| **Embedded / Platform Engineer** | 자체 하드웨어(맞춤 NIC, 타이밍 카드)의 펌웨어 · 드라이버 | C, 드라이버, DMA, 부팅, 하드웨어 bring-up | 거의 그대로 |

## 내 경력 → 퀀트 로우레벨 대응표

| 이미 하던 것 | 퀀트 쪽에서 같은 개념 | 다시 확인할 아이템 |
|---|---|---|
| NVMe SQ/CQ, 도어벨, PRP | NIC RX/TX 디스크립터 링, 도어벨, DMA | [NETHW-B02](../tracks/NETHW.html#NETHW-B02) NIC 데이터패스 해부: 와이어에서 소켓까지 · [PCIE-M01](../tracks/PCIE.html#PCIE-M01) SQ/CQ 링: head/tail, doorbell, Phase Tag |
| ISR에서 할당 · 블로킹 금지 | hot path에서 할당 · 시스템 콜 금지 | [LLAT-B08](../tracks/LLAT.html#LLAT-B08) 할당 없는 hot path · [C-M14](../tracks/C.html#C-M14) 인터럽트 안전 코드: 공유 변수와 원자성 |
| 링 버퍼, ISR ↔ 메인 큐 | 시세 스레드 ↔ 전략 스레드 SPSC 큐 | [DS-A01](../tracks/DS.html#DS-A01) Lock-free SPSC 큐: C11 atomics · [LLAT-M01](../tracks/LLAT.html#LLAT-M01) SPSC 큐 지연 튜닝: 캐시된 인덱스·패딩·배치 |
| 레지스터 맵, 비트 필드 | 거래소 이진 프로토콜(ITCH · SBE) 디코딩 | [LLAT-A01](../tracks/LLAT.html#LLAT-A01) 이진 프로토콜 제로카피 디코딩 (SBE 스타일) · [C-M03](../tracks/C.html#C-M03) 비트필드 vs 마스크/시프트: 이식성 분석 |
| 오실로스코프 · 타이밍 측정 | 하드웨어 타임스탬프, tick-to-trade 측정 | [LLAT-A04](../tracks/LLAT.html#LLAT-A04) 타임스탬핑: HW vs SW, SO_TIMESTAMPING, PTP 개념 · [LLAT-B01](../tracks/LLAT.html#LLAT-B01) 시간 측정: 클럭 소스와 오버헤드 |
| 실리콘 검증, 레지스터 테스트 | FPGA 검증 (cocotb · 커버리지) | [RTLDV-B09](../tracks/RTLDV.html#RTLDV-B09) cocotb 입문: Python 테스트벤치로 FIFO 구동 · [RTLDV-M12](../tracks/RTLDV.html#RTLDV-M12) 기능·코드 커버리지: Covergroup·Cross·Hole 분석 |

진입 장벽은 **C++ 깊이**, **리눅스 성능 튜닝**, **시장 도메인 어휘** 세 가지다. 아래 단계가 그 순서다.

## 한눈에

| 단계 | 범위 | 아이템 | 기간 | 끝나면 |
|---|---|---|---|---|
| 1. C++와 측정 | CPP-M · MCPP 핵심 · LLAT-B | 약 30 | 2주 | 저지연 C++ 코드를 쓰고 ns 단위로 측정·설명 |
| 2. 저지연 코어 | lock-free · 메모리 순서 · 오더북 · 이진 프로토콜 | 약 20 | 1.5주 | hot path 전체를 직접 짜고 p99로 증명 |
| 3. 리눅스 · 네트워크 경로 | CPU 격리 · 커널 바이패스 · 타임스탬프 · perf | 약 20 | 1.5주 | 패킷이 NIC에서 내 코드까지 오는 경로를 설명하고 튜닝 |
| 4. 시장 도메인 | 주문 · 오더북 · 피드 · FIX · 리스크 · 거래소 | 약 18 | 1.5주 | 트레이딩 시스템 설계 질문에 도메인 어휘로 답 |
| 5. 갈래 하나 | C++ 시스템 / FPGA / 네트워크 · 타이밍 | 10–20 | 1–2주 | 지원 직군의 심화 질문 |
| 6. 면접 마무리 | 코딩 · 확률 최소 세트 · 설계 | 약 15 | 1주 | 실전 라운드 |

## 1. C++와 측정 (약 2주)

퀀트 로우레벨 면접은 거의 전부 C++로 본다. "C처럼 쓰는 C++"가 아니라 **move, 템플릿, 메모리 모델을 정확히** 말할 수 있어야 한다.

| 묶음 | 아이템 |
|---|---|
| C++ 언어 | CPP-M01–M14 (Basic은 완료) · [MCPP-B03](../tracks/MCPP.html#MCPP-B03) Move semantics 구현: 소유 버퍼 클래스 · [MCPP-B04](../tracks/MCPP.html#MCPP-B04) Copy elision: RVO / NRVO / 보장된 생략 · [MCPP-B06](../tracks/MCPP.html#MCPP-B06) Perfect forwarding: make_unique / emplace 구현 · [MCPP-M10](../tracks/MCPP.html#MCPP-M10) 예외의 내부 비용 vs std::expected monadic 체인 · [MCPP-M11](../tracks/MCPP.html#MCPP-M11) Allocator & PMR: 할당 전략 교체 · [MCPP-M13](../tracks/MCPP.html#MCPP-M13) 메모리 모델 규칙: fence·release sequence · [MCPP-A04](../tracks/MCPP.html#MCPP-A04) Undefined Behavior 사냥 |
| 측정 기초 | [LLAT-B01](../tracks/LLAT.html#LLAT-B01) 시간 측정: 클럭 소스와 오버헤드 · [LLAT-B02](../tracks/LLAT.html#LLAT-B02) 지연 분포: 히스토그램 & 백분위수 · [LLAT-B03](../tracks/LLAT.html#LLAT-B03) 마이크로벤치마크 방법론 · [HPC-B01](../tracks/HPC.html#HPC-B01) 성능 측정 하네스: 반복·워밍업·통계와 GFLOP/s·GB/s |
| hot path 기본기 | [LLAT-B04](../tracks/LLAT.html#LLAT-B04) Cold path 제거: 프리폴트·mlock·캐시 워밍 · [LLAT-B05](../tracks/LLAT.html#LLAT-B05) hot path 데이터 레이아웃: hot/cold 분리 & 패딩 · [LLAT-B06](../tracks/LLAT.html#LLAT-B06) atomic 연산 비용: relaxed·acq_rel·seq_cst·CAS 경합 · [LLAT-B07](../tracks/LLAT.html#LLAT-B07) 분기 예측 & branchless 코드 · [LLAT-B08](../tracks/LLAT.html#LLAT-B08) 할당 없는 hot path |
| 캐시 감각 | [ARCH-M07](../tracks/ARCH.html#ARCH-M07) 캐시 친화 코드: 루프 순서와 타일링 · [ARCH-M08](../tracks/ARCH.html#ARCH-M08) False Sharing 측정 |

- **LLAT-B01–B03이 이 단계의 핵심이다.** 저지연 면접은 "몇 ns였나, 어떻게 쟀나, 분포는?"을 반드시 묻는다.
- MCPP는 전부가 아니라 성능과 안전성에 직결되는 것만 골랐다. 나머지는 C++ 깊이를 따로 증명해야 할 때 푼다.

## 2. 저지연 코어 (약 1.5주)

스레드 사이로 데이터를 넘기는 방법과, 오더북 같은 핵심 자료구조를 직접 짠다.

| 묶음 | 아이템 |
|---|---|
| 메모리 순서 | [C-A03](../tracks/C.html#C-A03) C11 Atomics: 플래그·카운터·acquire/release · [OS-M08](../tracks/OS.html#OS-M08) C11 atomics 동기화 패턴: 메시지 패싱·DCLP · [ARCH-A05](../tracks/ARCH.html#ARCH-A05) 메모리 순서 리트머스 테스트 on Apple M2 |
| lock-free 큐 | [DS-A01](../tracks/DS.html#DS-A01) Lock-free SPSC 큐: C11 atomics · [LLAT-M01](../tracks/LLAT.html#LLAT-M01) SPSC 큐 지연 튜닝: 캐시된 인덱스·패딩·배치 · [LLAT-M02](../tracks/LLAT.html#LLAT-M02) Seqlock: 단일 writer 스냅샷 공유 · [LLAT-M04](../tracks/LLAT.html#LLAT-M04) LMAX Disruptor 패턴 구현 · [LLAT-M05](../tracks/LLAT.html#LLAT-M05) 스레드 간 핸드오프 지연 (ping-pong) · [LLAT-M06](../tracks/LLAT.html#LLAT-M06) Busy polling vs blocking 대기 · [LLAT-M03](../tracks/LLAT.html#LLAT-M03) lock-free 메모리 회수: Hazard Pointer |
| 오더북 · 프로토콜 | [LLAT-M08](../tracks/LLAT.html#LLAT-M08) 오더북 1: 가격 레벨 & FIFO 매칭 · [LLAT-M09](../tracks/LLAT.html#LLAT-M09) 오더북 2: 캐시 친화 최적화 & ITCH 리플레이 · [LLAT-A01](../tracks/LLAT.html#LLAT-A01) 이진 프로토콜 제로카피 디코딩 (SBE 스타일) · [QFIN-B01](../tracks/QFIN.html#QFIN-B01) 가격·수량 표현: 고정소수점 틱과 반올림 |
| 로깅 | [LLAT-A06](../tracks/LLAT.html#LLAT-A06) 저지연 비동기 로거 (NanoLog 스타일) |

- **LLAT-M08/M09 오더북**은 퀀트 코딩 면접의 단골이다. 가격 레벨 자료구조 선택(배열 vs 트리 vs 해시)을 지연 수치로 설명할 수 있어야 한다.
- 동시성 기초가 약하면 **🧵 동시성 연습 가이드**의 1·3·5층을 먼저 본다.

## 3. 리눅스 · 네트워크 경로 (약 1.5주)

"패킷이 선을 타고 들어와서 내 `on_market_data()`가 불릴 때까지" 무슨 일이 일어나는지 설명하는 단계다. 대부분 env가 `linux`라 Lima VM `elinux`에서 한다.

| 묶음 | 아이템 |
|---|---|
| 패킷 수신 경로 | [NETHW-B02](../tracks/NETHW.html#NETHW-B02) NIC 데이터패스 해부: 와이어에서 소켓까지 · [NET-A02](../tracks/NET.html#NET-A02) 수신 경로 추적: IRQ → NAPI poll → GRO → 프로토콜 · [NET-A04](../tracks/NET.html#NET-A04) RSS Toeplitz 해시와 RPS/RFS/XPS · [NETHW-B09](../tracks/NETHW.html#NETHW-B09) Linux에서 NIC 관찰: ethtool -l/-g/-k/-c/-S |
| 커널 바이패스 | [LLAT-A02](../tracks/LLAT.html#LLAT-A02) 커널 바이패스: DPDK 폴링 모드 · [NET-A05](../tracks/NET.html#NET-A05) XDP 패킷 필터와 BPF 맵 (eBPF) · [LLAT-A03](../tracks/LLAT.html#LLAT-A03) io_uring 기반 저지연 I/O |
| CPU · 노이즈 | [LLAT-M07](../tracks/LLAT.html#LLAT-M07) CPU 격리 & 피닝: isolcpus·nohz_full·IRQ affinity · [LLAT-A05](../tracks/LLAT.html#LLAT-A05) 꼬리 지연 원인 분석: page fault·TLB·인터럽트 · [LPERF-A01](../tracks/LPERF.html#LPERF-A01) OS 노이즈 측정: osnoise·timerlat tracer와 IRQ 시간 |
| 시간 | [LLAT-A04](../tracks/LLAT.html#LLAT-A04) 타임스탬핑: HW vs SW, SO_TIMESTAMPING, PTP 개념 · [DIST-M01](../tracks/DIST.html#DIST-M01) PTP(IEEE 1588) 시간 동기 |
| 프로파일링 | [LPERF-B03](../tracks/LPERF.html#LPERF-B03) perf 시작하기: perf stat과 소프트웨어 이벤트 · [LPERF-B05](../tracks/LPERF.html#LPERF-B05) 샘플링 프로파일링: perf record·report·annotate · [LPERF-M01](../tracks/LPERF.html#LPERF-M01) PMU 하드웨어 이벤트와 IPC 해석 · [LPERF-A03](../tracks/LPERF.html#LPERF-A03) perf c2c로 false sharing 찾기 |

- **NVMe 경험을 살리는 지점**: NIC의 RX 디스크립터 링과 NVMe 완료 큐는 구조가 거의 같다. 면접에서 이 비유로 설명하면 강하다.
- 하드웨어 PMU가 Lima에서 안 잡힐 수 있다. 그럴 때는 소프트웨어 이벤트와 타이밍 측정으로 대신하고, 레슨에 그 한계를 적는다.

## 4. 시장 도메인 (약 1.5주)

엔지니어 면접에서도 "주문이 어떻게 체결되나", "피드가 끊기면?" 같은 질문이 나온다. 깊은 금융 이론이 아니라 **시스템이 다루는 대상**을 아는 수준이면 된다.

| 묶음 | 아이템 |
|---|---|
| 주문 · 체결 | [QFIN-B02](../tracks/QFIN.html#QFIN-B02) 주문 유형·TIF 의미론과 주문 상태 머신 · [QFIN-B03](../tracks/QFIN.html#QFIN-B03) 매칭 규칙 확장: IOC/FOK/Post-only/STP · [QFIN-B04](../tracks/QFIN.html#QFIN-B04) L1/L2/L3: MBO에서 MBP 집계 뷰 만들기 · [QFIN-B05](../tracks/QFIN.html#QFIN-B05) 호가 지표: Spread·Mid·Microprice·Imbalance · [QFIN-B07](../tracks/QFIN.html#QFIN-B07) 포지션·PnL 회계: 실현/미실현·수수료·리베이트 |
| 시세 피드 | [QFIN-M03](../tracks/QFIN.html#QFIN-M03) 피드 아비트리지: A/B 라인 병합 · [QFIN-M04](../tracks/QFIN.html#QFIN-M04) 갭 복구와 스냅샷 동기화 |
| 주문 경로 | [QFIN-B08](../tracks/QFIN.html#QFIN-B08) FIX 1: tag=value 파서·BodyLength·CheckSum · [QFIN-M05](../tracks/QFIN.html#QFIN-M05) FIX 2: 세션 계층 — 시퀀스·하트비트·재전송 · [QFIN-M06](../tracks/QFIN.html#QFIN-M06) 주문 게이트웨이와 사전 리스크 체크 · [QFIN-M07](../tracks/QFIN.html#QFIN-M07) OMS: ExecutionReport 처리와 Cancel/Replace 경쟁 |
| 거래소 · 인프라 | [QFIN-A07](../tracks/QFIN.html#QFIN-A07) 거래소 설계: 시퀀서·저널·결정적 리플레이 · [QFIN-A09](../tracks/QFIN.html#QFIN-A09) 거래소 연결성: 코로케이션·전송 매체·시간 동기 · [LLAT-A07](../tracks/LLAT.html#LLAT-A07) Tick-to-trade 파이프라인 설계 & 지연 예산 |

- **QFIN-M06 사전 리스크 체크**와 **LLAT-A07 tick-to-trade 지연 예산**은 시스템 설계 라운드에서 거의 항상 나온다.
- 옵션 가격결정(QFIN-B09–B11, M10–M11)은 로우레벨 직군에서는 선택이다.

## 5. 갈래 — 지원 직군 하나 고르기

### A. Low-Latency C++ / Trading Systems

| 아이템 | 이유 |
|---|---|
| [QFIN-A06](../tracks/QFIN.html#QFIN-A06) 스마트 오더 라우터(SOR) | 여러 거래소로 주문 라우팅, 상태 관리 |
| [QFIN-M08](../tracks/QFIN.html#QFIN-M08) 이벤트 기반 백테스터 1: 이벤트 루프·체결 시뮬레이터 · [QFIN-M09](../tracks/QFIN.html#QFIN-M09) 백테스터 2: 편향·비용 모델·성과 지표 | 백테스터 = 결정적 이벤트 처리 엔진 |
| [MCPP-A07](../tracks/MCPP.html#MCPP-A07) 템플릿 메타프로그래밍: typelist & 컴파일 타임 검증 · [MCPP-A08](../tracks/MCPP.html#MCPP-A08) Expression templates: 임시 객체 없는 벡터 연산 | 컴파일 타임 계산으로 런타임 분기 제거 |
| [LPERF-A07](../tracks/LPERF.html#LPERF-A07) 종합: 꼬리 지연 미스터리 사후 분석 | 꼬리 지연 사후 분석 (면접형) |
| [TEST-A05](../tracks/TEST.html#TEST-A05) 결정론적 시뮬레이션 테스트: 가상 시간과 재현 가능한 seed | 결정적 리플레이 테스트 |

### B. FPGA Engineer

RTL을 처음 한다면 Basic부터 순서대로 간다. 실리콘 검증 경험이 있어서 검증 쪽은 빠르게 지나갈 수 있다.

| 아이템 | 이유 |
|---|---|
| RTLDV-B01–B11 | RTL 기본기 (Verilator · iverilog · cocotb) |
| [RTLDV-M01](../tracks/RTLDV.html#RTLDV-M01) Skid Buffer: 등록된 Ready로 풀 스루풋 유지 · [RTLDV-M02](../tracks/RTLDV.html#RTLDV-M02) 파이프라인 데이터패스: Stall·Flush·Bubble Collapse · [RTLDV-M05](../tracks/RTLDV.html#RTLDV-M05) CDC 1: 2-FF 동기화기·펄스 동기화·Req/Ack 핸드셰이크 · [RTLDV-M06](../tracks/RTLDV.html#RTLDV-M06) CDC 2: 비동기 FIFO RTL (Gray 포인터) | skid buffer, 파이프라인, CDC — 저지연 데이터패스의 기본 부품 |
| [RTLDV-A07](../tracks/RTLDV.html#RTLDV-A07) 타이밍 클로저: 파이프라이닝·리타이밍과 등가성 검사 | 타이밍 클로저 (클럭을 올리는 법) |
| [RTLDV-A09](../tracks/RTLDV.html#RTLDV-A09) 저지연 스트리밍 패킷 파서 RTL (64-bit AXI-Stream) | **저지연 스트리밍 패킷 파서** — 시세 파서와 같은 구조 |
| [QFIN-A08](../tracks/QFIN.html#QFIN-A08) FPGA 트레이딩 가속: 사이클 모델 피드 파서 | FPGA 트레이딩 가속: 사이클 모델 피드 파서 |
| [NETHW-M07](../tracks/NETHW.html#NETHW-M07) P4_16 L3 라우터: bmv2 simple_switch와 P4Runtime | P4 match-action (스위치 쪽 하드웨어 파이프라인) |

### C. Network / Timing / Hardware

| 아이템 | 이유 |
|---|---|
| [DIST-M02](../tracks/DIST.html#DIST-M02) BMCA와 gPTP 그랜드마스터 선택 · [AUTO-M09](../tracks/AUTO.html#AUTO-M09) Automotive Ethernet TSN: gPTP와 Time-Aware Shaper | PTP 그랜드마스터 선택, gPTP — 시간 동기 심화 |
| [NET-A03](../tracks/NET.html#NET-A03) GSO/TSO/GRO 오프로드 효과 측정 · [NET-A08](../tracks/NET.html#NET-A08) 제로카피 송수신: sendfile, splice, MSG_ZEROCOPY · [NET-A10](../tracks/NET.html#NET-A10) 패킷 드롭 디버깅 방법론 (프로덕션 면접형) | 오프로드, 제로카피, 패킷 드롭 디버깅 |
| [NETHW-A04](../tracks/NETHW.html#NETHW-A04) RDMA NIC 엔진 내부: QP 컨텍스트, MTT/MPT, PSN · [NETHW-A10](../tracks/NETHW.html#NETHW-A10) NIC 펌웨어 인터페이스: Admin Queue, health, 리셋 복구 | NIC 내부 엔진과 펌웨어 인터페이스 |
| [DCN-B01](../tracks/DCN.html#DCN-B01) DC 패브릭 지표: 지연 분해와 Bisection Bandwidth | 지연 분해 (직렬화 · 전파 · 스위치) — 코로케이션 설계 근거 |
| [HWLAB-M07](../tracks/HWLAB.html#HWLAB-M07) 클럭 지터 측정: Period·Cycle-to-Cycle·TIE | 클럭 지터 측정 |

## 6. 면접 마무리 (약 1주)

| 라운드 | 준비 | 아이템 |
|---|---|---|
| 코딩 | C++로 자료구조 · 알고리즘. 오더북 · LRU · 링 버퍼 · 파서가 단골 | [LLAT-M08](../tracks/LLAT.html#LLAT-M08) 오더북 1: 가격 레벨 & FIFO 매칭 · [DS-M05](../tracks/DS.html#DS-M05) LRU 캐시: 해시 + 이중 연결 리스트 · ALGO-B 묶음 |
| 확률 · 암산 (최소 세트) | 엔지니어에게도 짧게 묻는 회사가 많다 | [QPROB-B04](../tracks/QPROB.html#QPROB-B04) 기댓값의 선형성과 지시변수 트릭 · [QPROB-B11](../tracks/QPROB.html#QPROB-B11) CLT·정규근사와 멘탈 매스 드릴 · [QPROB-M01](../tracks/QPROB.html#QPROB-M01) 마르코프 체인 흡수: first-step과 기본행렬 · [QPROB-M06](../tracks/QPROB.html#QPROB-M06) 기댓값 게임과 후진 귀납 (최적 정지 DP) · [QPROB-A03](../tracks/QPROB.html#QPROB-A03) 마켓 메이킹 게임: 가격 제시와 역선택 |
| 시스템 설계 | "시세 → 주문 시스템을 설계하라", "지연 예산을 나눠라" | [LLAT-A07](../tracks/LLAT.html#LLAT-A07) Tick-to-trade 파이프라인 설계 & 지연 예산 · [QFIN-A07](../tracks/QFIN.html#QFIN-A07) 거래소 설계: 시퀀서·저널·결정적 리플레이 |
| 로우레벨 질문 | "이 코드의 지연을 줄여라", "왜 p99가 튀나" | [LPERF-A07](../tracks/LPERF.html#LPERF-A07) 종합: 꼬리 지연 미스터리 사후 분석 · [LLAT-A05](../tracks/LLAT.html#LLAT-A05) 꼬리 지연 원인 분석: page fault·TLB·인터럽트 |

- 코딩 패턴 연습은 허브의 **AlgoMonster Quant SWE Prep 매핑표** 문서를 같이 본다. 거기서 어떤 레슨이 어떤 모듈과 겹치는지 정리돼 있다.
- 확률 퀴즈를 더 깊게 묻는 회사(옵션 마켓메이커 등)에 지원한다면 QPROB-B 전체를 1주 추가한다.

## 순서 요약

```
C++ 깊이 + 측정 ──▶ lock-free · 오더북 ──▶ 리눅스 · NIC 경로 ──▶ 시장 도메인 ──▶ 갈래(C++ / FPGA / 네트워크) ──▶ 면접
   (2주)                (1.5주)               (1.5주)              (1.5주)           (1–2주)                     (1주)
```

## 참고자료

- LLAT · QFIN 트랙 소개와 참고자료 (트랙 페이지 하단)
- 🧵 동시성 연습 가이드 · ⚡ HPC 학습 가이드 — 2·3단계의 배경
- AlgoMonster Quant SWE Prep → 스터디노트 매핑표 — 코딩 라운드 준비
- Carl Cook, "When a Microsecond Is an Eternity: High Performance Trading Systems in C++" (CppCon 2017 발표)
- David Gross, "Trading at light speed: designing low latency systems in C++" (Meeting C++ 2022 발표)
