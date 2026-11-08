# AlgoMonster Quant SWE Prep → 스터디노트 커리큘럼 매핑표

- 소스: `algomaster-quant-swe-prep/original-md/` (M01–M10, 한국어 요약 약 3,000줄, 2026-09-27)
- 대상: `curriculum/tracks/*.json` (2149 items)
- 강도: ◎ 직접 겹침(레슨 쓸 때 근거로 바로 사용) · ○ 부분 겹침 · △ 주변 지식 · ✕ 커리큘럼에 없음(갭)
- 활용법: 소스 노트는 설명식 요약이라 레슨(문제→힌트→풀이→코드→Q&A)으로 그대로 쓰지 않고, 해당 아이템 레슨을 쓸 때 "배경 설명/용어 근거"로 참고한다.

## 한눈에 보기

| 모듈 | 주제 | 주 대응 트랙 | 갭 |
|---|---|---|---|
| M01 | Read Hot Code | LLAT, MCPP, ARCH | 거의 없음 (할당·분기·벤치마킹 ◎) |
| M02 | Write Safe C++ | MCPP, CPP | 컨테이너 무효화·C++ 캐스트 ○ |
| M03 | Choose Data Structure | DS, MCPP, MEM | STL 선택 가이드 자체 ✕ |
| M04 | Threads | OS, LLAT, DS, MCPP | condvar·lock scope ◎, lock-free ◎ |
| M05 | Machine | ARCH, MEM, LPERF | **SIMD/벡터화 ✕** (NEON만 ARM/DSP) |
| M06 | OS | OS, ARCH, LLAT | 거의 없음 |
| M07 | Network | NET, QFIN, LLAT, DCN | 피드 핸들러 ◎, RDMA △ |
| M08 | Debug Latency | LLAT, LPERF | 거의 없음 (퍼센타일·perf·락 경합 ◎) |
| M09 | Trading Systems | QFIN, LLAT | 거의 없음 (오더북·게이트웨이·OMS ◎) |
| M10 | Numbers & Time | NUM, QFIN, LLAT | 거의 없음 |

결론: **M01·M04·M08·M09는 이미 아이템이 있어 새 아이템 추가가 거의 필요 없다.** 갭은 M05 SIMD, M03 STL 선택 가이드, M02 컨테이너 무효화 정도.

---

## M01 Read Hot Code

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1 Cost Model (Big-O가 숨기는 것) | ALGO-B01, LLAT-B03 | ○ | 상수항·캐시를 실측으로 보이는 방식 |
| 2 Memory Layout (vector vs list) | DS-A11, ARCH-M07, LLAT-B05 | ◎ | AoS/SoA, hot/cold 분리 |
| 3 Cache Lines / Locality / Pointer Chasing | ARCH-M04, ARCH-M05, MEM-B10, MEM-M03 | ◎ | 포인터 체이싱 지연 측정 |
| 4 Allocation in the Hot Path | LLAT-B08, LLAT-B04, DS-M02, CPP-B14 | ◎ | 할당 없는 hot path |
| 5 Copies, Moves, Object Layout | MCPP-B02~B04, CPP-B08, CPP-M09 | ◎ | Rule of 0/3/5, RVO, 패딩 |
| 6 Branches / Indirect Calls / Inlining | LLAT-B07, ARCH-M03, CPP-M06 | ◎ | branchless, devirtualization |
| 7 Virtual Functions & Vtables | CPP-M06, CPP-M05 | ◎ | CRTP 대안 포함 |
| 8 Type Erasure (`std::function`) | MCPP-M07, MCPP-M08, CPP-M12 | ◎ | SBO 구현·무힙 콜백 |
| 9 Benchmarking Without Lying | LLAT-B03, LLAT-B01, LLAT-B02 | ◎ | 마이크로벤치 방법론 |

## M02 Write Safe C++

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1 Object Lifetime | MCPP-B13, MCPP-B12, MCPP-A04 | ○ | 수명 함정·UB 사냥 |
| 2 RAII | CPP-B06, CPP-B07, MCPP-B11 | ◎ | 예외 안전성 포함 |
| 3 raw/unique/shared/weak | MCPP-M01, MCPP-M02, MCPP-M03 | ◎ | 직접 구현형 |
| 4 Dangling (`string_view`, `span`) | MCPP-B13, CPP-B11 | ◎ | |
| 5 Move Semantics | MCPP-B01, B03, B05, B06 | ◎ | 값 범주·forwarding |
| 6 Lambdas & Capture Lifetime | MCPP-B12 | ◎ | |
| 7 Container Invalidation & Stable Handles | (DS-M01 침입형, DS-B07 인덱스 링크) | △ | **iterator 무효화 규칙 자체를 다루는 아이템 없음 → 갭** |
| 8 C++ Casts & Type Safety | MCPP-A04, MCPP-A05 | △ | static/reinterpret/bit_cast 비교 아이템 없음 |

## M03 Choose The Right Data Structure

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1 STL Container Choice | (없음) | ✕ | **STL 컨테이너 선택 가이드 아이템 없음 → 갭** |
| 2 vector size/capacity/reserve | DS-B01, CPP-M10 | ○ | 고정 용량 벡터/static_vector |
| 3 unordered_map bucket/load factor/rehash | DS-B10, DS-B11, DS-M08 | ◎ | 체이닝·오픈어드레싱·리사이즈 |
| 4 map/list/deque 트레이드오프 | DS-B05, DS-B06, DS-B13, DS-A05 | ○ | |
| 5 pmr / Allocators / Pools | MCPP-M11, DS-M02, DS-M03 | ◎ | |
| 6 Small Buffer Optimization | MCPP-M07 | ◎ | |
| 7 Templates & Generic Programming | MCPP-B07~B09, MCPP-M04 | ◎ | |

## M04 Handle Threads Without Lying To Yourself

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1 Threads / Data Races | OS-B04 | ◎ | 공유 카운터 레이스 |
| 2 Mutex / Lock Scope / Contention | OS-B05, OS-M07, OS-M09, OS-A01 | ◎ | |
| 3 Condition Variables / Producer-Consumer | OS-B06 | ◎ | |
| 4 Atomics & Memory Ordering | LLAT-B06, MCPP-M13, OS-M08, ARCH-A05, MEM-M07 | ◎ | |
| 5 SPSC Ring Buffer Handoff | DS-A01, LLAT-M01, DS-B04 | ◎ | |
| 6 Lock-Free Queue Concepts | DS-A03, DS-A04, LLAT-M03, LLAT-M04 | ◎ | MPMC·ABA·hazard pointer |

## M05 Understand The Machine

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1 CPU Cache Hierarchy | ARCH-M04, ARCH-M05, MEM-M03 | ◎ | |
| 2 Cache Coherency & MESI | ARCH-A04, MEM-M04, MEM-M05 | ◎ | |
| 3 False Sharing | ARCH-M08, LPERF-A03, LLAT-B05 | ◎ | 측정 + perf c2c |
| 4 Pipeline & Branch Prediction | ARCH-M01, ARCH-M02, ARCH-M03 | ◎ | |
| 5 Prefetching & Streaming | MEM-A07, MEM-B11, MEM-A08 | ○ | |
| 6 SIMD & Vectorization | ARM/DSP/EML 일부 (NEON) | △ | **범용 SIMD 입문(auto-vectorization, intrinsics) 아이템 없음 → 갭** |

## M06 Understand The OS

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1 Virtual Memory / Pages / TLB | ARCH-A01, ARCH-A02, OS-B10, OS-M01 | ◎ | |
| 2 Page Faults & Latency | ARCH-A03, LLAT-A05, LPERF-B09, OS-M03 | ◎ | |
| 3 Context Switch & Scheduler | OS-M02, OS-B12, OS-A04 | ◎ | |
| 4 Processes / Threads / IPC | OS-B01, OS-B02, OS-B14 | ◎ | |
| 5 malloc Internals | OS-M04, OS-M05, OS-M06, LLAT-B08 | ◎ | |
| 6 Kernel Bypass & Real-Time Sched (intro) | LLAT-A02, LLAT-M07, LLAT-A03 | ◎ | DPDK·isolcpus·io_uring |

## M07 Understand The Network

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1 Socket Programming Basics | NET-B09, NET-B07, OS-M12, OS-M13 | ◎ | |
| 2 TCP vs UDP Trade-offs | NET-B10, NET-B11, NET-M05 | ◎ | Nagle·NODELAY |
| 3 Market Data Feed Handler Shape | QFIN-B04, QFIN-M03, QFIN-M04, LLAT-A01 | ◎ | A/B 병합·갭 복구·SBE |
| 4 Packet Drops & Backpressure | NET-M09, NET-A10, NET-M13 | ◎ | |
| 5 Network Stack Latency | NET-A02, NET-A03, LLAT-A04 | ◎ | |
| 6 RDMA & Specialized Networking | DCN, NETHW 트랙 | △ | HFT 관점 RDMA는 요약 수준만 |

## M08 Debug Latency

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1 Latency Measurement & Percentiles | LLAT-B01, LLAT-B02 | ◎ | |
| 2 Profiling the Hot Path | LPERF-B03, B05, B06 | ◎ | flame graph 직접 제작 |
| 3 Performance Counters Intro | LPERF-B04, LPERF-M01, LPERF-M02 | ◎ | |
| 4 Memory Bottleneck Identification | MEM-A08, LPERF-B08, LPERF-B09 | ○ | roofline |
| 5 Lock Contention Debugging | LPERF-M07, OS-B05 | ◎ | |
| 6 Systematic Debugging Storytelling | LPERF-A07, LLAT-A05, NET-A10 | ◎ | 꼬리 지연 사후분석 |

## M09 Speak Trading Systems

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1 Trade Lifecycle | QFIN-B02, QFIN-M07 | ◎ | |
| 2 Order Book API Shape | LLAT-M08, LLAT-M09, QFIN-B04, QFIN-B05 | ◎ | |
| 3 Matching Engine Concepts | QFIN-B03, QFIN-M01, QFIN-M02, QFIN-A07 | ◎ | |
| 4 Order Gateway & Risk Checks | QFIN-M06, QFIN-M05, QFIN-B08 | ◎ | FIX |
| 5 Market Data → Strategy Handoff | LLAT-A07, LLAT-M01, QFIN-M03 | ◎ | tick-to-trade |
| 6 C++ HFT Pattern Map | LLAT-M02, LLAT-M04, LLAT-A06, MCPP-M07 | ○ | seqlock·Disruptor·로거 |

## M10 Handle Numbers And Time

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1 Floating-Point Representation | NUM-B01, NUM-B02, ARCH-B03 | ◎ | |
| 2 Integer Ticks & Price Representation | QFIN-B01 | ◎ | |
| 3 Numerical Stability Intro | NUM-B03, NUM-M01 | ◎ | Kahan, 조건수 |
| 4 Time, Timestamps, Clocks | LLAT-B01, LLAT-A04 | ◎ | |

---

## 갭 요약 (새 아이템 후보)

1. **SIMD/벡터화 입문** — auto-vectorization 확인, NEON intrinsics(M-class), 정렬·tail 처리 (M05-6). 후보 위치: `ARCH` 또는 `LLAT`.
2. **STL 컨테이너 선택 + iterator 무효화** — vector/deque/list/map/unordered_map 비용 표, 무효화 규칙 테스트 (M02-7, M03-1). 후보: `MCPP` 또는 `CPP`.
3. **C++ 캐스트 안전성** — static/dynamic/reinterpret/bit_cast, aliasing UB (M02-8). 후보: `MCPP-A04` 확장.
4. (선택) HFT 관점 RDMA 한 장 요약 — `DCN`/`NETHW`에 이미 있어 우선순위 낮음.

## 사용 방법

- LLAT/QFIN/NUM/MCPP/LPERF 레슨 작성 시 위 표의 해당 소스 모듈 md를 `LESSON_WRITER_BRIEF`의 참고 자료로 넘긴다.
- 면접 직전 복습용으로는 소스 사이트(`algomaster-quant-swe-prep/site/index.html`)의 "5분 복습" 블록이 더 빠르다.
