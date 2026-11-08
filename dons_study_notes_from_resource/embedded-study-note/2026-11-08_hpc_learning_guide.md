# HPC 학습 가이드 (2026-11-08)

HPC(High Performance Computing, 고성능 컴퓨팅)는 "같은 하드웨어에서 더 빠르게, 더 많은 코어로 더 크게"를 **숫자로 증명**하는 분야다. AI 학습 클러스터, 과학 시뮬레이션, 칩 검증 팜이 모두 같은 원리 위에서 돈다.

이번에 **HPC · 병렬 컴퓨팅 트랙(HPC, 30개)**을 새로 만들었다. 기존 트랙에 흩어져 있던 roofline · SIMD · GEMM · GPU · all-reduce · AI 병렬화 아이템과 합쳐 8개 층으로 묶었다. 새 트랙은 OpenMP(공유 메모리)와 MPI(분산 메모리)로 stencil · SpMV · N-body · FFT 같은 대표 문제를 직접 병렬화하는 쪽을 맡는다.

**준비물**: `brew install libomp open-mpi` 한 번이면 HPC 트랙 전체가 Mac에서 된다. OpenMP는 `clang -Xpreprocessor -fopenmp -I$(brew --prefix libomp)/include -L$(brew --prefix libomp)/lib -lomp`, MPI는 `mpicc x.c && mpirun -np 4 ./a.out` (노트북 한 대에서 프로세스 4개로 분산 메모리를 흉내 낸다).

> **쓰는 법**
> - 층을 위에서부터 내려간다. 층마다 **면접 체크**를 말로 설명할 수 있으면 다음 층으로 간다.
> - ⭐ 표시 20개가 **핵심 경로**다. 시간이 없으면 ⭐만 순서대로 푼다.
> - 🆕 표시는 이 가이드를 만들며 새로 추가한 아이템이다 (30개).
> - 아이템 ID를 누르면 트랙 표의 그 줄로 간다. 레슨이 써졌는지(학습가능/준비중)는 그 표에서 실시간으로 보인다.
> - env가 `linux`면 Lima VM `elinux`, `qemu`면 QEMU, `gpu`면 NVIDIA GPU가 필요하다. 나머지는 Mac에서 바로 된다.

## 한눈에: 8개 층

| 층 | 주제 | 주로 나오는 트랙 | 아이템 수 | ⭐ |
|---|---|---|---|---|
| 1. 측정과 성능 모델 | 측정 하네스 · scaling · roofline | AIPERF · HPC · MEM · ARCH | 8 | 3 |
| 2. 단일 코어 최적화 — 캐시와 SIMD | 루프 순서 · 타일링 · 데이터 레이아웃 · 벡터화 | ARM · MEM · EML · ARCH · HPC · TOOL | 9 | 2 |
| 3. 공유 메모리 병렬 — OpenMP | parallel for · reduction · 스케줄링 · work-span | HPC · MEM · OS | 7 | 3 |
| 4. 대표 커널 — HPC 문제 은행 | stencil · SpMV · N-body · CG · multigrid · FFT | HPC · DSP · NUM | 12 | 4 |
| 5. 분산 메모리 병렬 — MPI | 점대점 · 집합 통신 · halo · 오버랩 · 하이브리드 | HPC | 7 | 4 |
| 6. 통신 알고리즘과 네트워크 | α-β 모델 · all-reduce · RDMA | DCN · GPUSYS | 8 | 1 |
| 7. GPU 가속 | 병렬도 · reduction · scan · GEMM · 커널 퓨전 | GPGPU | 8 | 1 |
| 8. 대규모 시스템 — AI 학습과 운영 | 학습 병렬화 · 체크포인트 · 병렬 I/O · 잡 스케줄러 · 종합 | AISYS · HPC | 9 | 2 |

합계 68개 · ⭐ 20개. 층 안의 순서가 권장 순서다.

## 1. 측정과 성능 모델

HPC의 모든 답은 "그래서 몇 배 빨라졌고, 왜 거기서 멈췄나"다. 숫자를 믿을 수 있게 재는 법과, 한계를 미리 계산하는 모델부터 익힌다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [HPC-B01](../tracks/HPC.html#HPC-B01) ⭐ 🆕 | Basic · mac | 성능 측정 하네스: 반복·워밍업·통계와 GFLOP/s·GB/s | HPC 실험의 기본기인 '믿을 수 있는 숫자'를 만드는 측정 하네스를 갖춘다. |
| [HPC-B02](../tracks/HPC.html#HPC-B02) ⭐ 🆕 | Basic · mac | Strong vs Weak Scaling: Amdahl·Gustafson 실측과 Karp-Flatt | 스레드 수를 늘릴 때의 확장성을 두 가지 방식으로 측정하고 직렬 비율을 역추정한다. |
| [ARCH-A11](../tracks/ARCH.html#ARCH-A11) ⭐ | Adv · mac | 성능 모델: Amdahl과 Roofline | 연산 강도와 메모리 대역폭으로 커널 성능 상한을 추정한다 |
| [MEM-B11](../tracks/MEM.html#MEM-B11) | Basic · mac | 메모리 대역폭 측정: STREAM Copy/Scale/Add/Triad | 지속 대역폭과 이론 피크의 차이, 스레드 수의 영향을 측정한다. |
| [AIPERF-B04](../tracks/AIPERF.html#AIPERF-B04) | Basic · mac | Roofline 대역폭 천장 정하기: STREAM 결과 해석 | 측정한 지속 대역폭을 roofline 천장과 LLM 토큰 속도 상한으로 바꾸는 법을 익힌다 |
| [AIPERF-B06](../tracks/AIPERF.html#AIPERF-B06) | Basic · mac | Roofline 모델 구축과 커널 배치 | 피크 연산과 대역폭으로 roofline을 그리고 커널의 위치로 병목을 판정한다 |
| [AIPERF-M07](../tracks/AIPERF.html#AIPERF-M07) | Mid · mac | 계층적 Roofline: L1/L2/DRAM | 캐시 계층별 대역폭 천장을 그려 타일링 최적화의 효과를 해석한다 |
| [HPC-M11](../tracks/HPC.html#HPC-M11) 🆕 | Mid · mac | 병렬 프로그램 프로파일링: 타임라인 계측과 불균형 지표 | PMU 없이도 병렬 프로그램의 병목(불균형·동기화·통신)을 숫자로 찾는다. |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ strong scaling과 weak scaling의 차이, 각각 Amdahl·Gustafson 중 무엇과 맞나
- ☐ 8코어에서 speedup이 5배면 직렬 비율은 대략 얼마인가 (Karp-Flatt)
- ☐ roofline의 두 천장과 연산 강도(FLOP/byte)로 커널이 메모리/연산 바운드인지 판정하기
- ☐ 벤치마크에서 워밍업·반복·중앙값이 필요한 이유, 컴파일러가 루프를 지우는 문제

## 2. 단일 코어 최적화 — 캐시와 SIMD

병렬화 전에 코어 하나를 제대로 쓰는 단계다. 코어 하나가 느리면 코어 100개도 느리다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [ARCH-M07](../tracks/ARCH.html#ARCH-M07) ⭐ | Mid · mac | 캐시 친화 코드: 루프 순서와 타일링 | 공간/시간 지역성을 이용한 루프 변환의 실제 효과를 측정한다 |
| [HPC-B06](../tracks/HPC.html#HPC-B06) ⭐ 🆕 | Basic · mac | 데이터 레이아웃: AoS vs SoA와 자동 벡터화 | 메모리 레이아웃이 캐시 활용과 SIMD 벡터화 가능성을 어떻게 바꾸는지 측정으로 보인다. |
| [EML-B02](../tracks/EML.html#EML-B02) | Basic · mac | CPU GEMM: Naive → Loop 순서 → Cache Blocking | 모든 NN 연산의 핵심인 GEMM의 캐시 친화적 구현과 성능 변화를 체감한다 |
| [MEM-A08](../tracks/MEM.html#MEM-A08) | Adv · mac | Roofline으로 메모리 병목 분석: 타일링 전후 | 커널이 메모리 대역폭에 묶였는지 판정하고 데이터 재사용으로 개선한다. |
| [ARM-M06](../tracks/ARM.html#ARM-M06) | Mid · mac | NEON 기초: 벡터 덧셈·내적·리덕션 | NEON intrinsic과 어셈블리로 SIMD 레인 연산을 작성한다 |
| [TOOL-A10](../tracks/TOOL.html#TOOL-A10) | Adv · mac | 최적화 리포트 읽기: -Rpass와 인라이닝·벡터화 | 컴파일러가 왜 인라인·벡터화를 했거나 못 했는지 리포트로 확인하고 코드를 고친다 |
| [ARM-A11](../tracks/ARM.html#ARM-A11) | Adv · mac | 컴파일러 최적화 출력 판독: 벡터화와 언롤 | 자동 벡터화·루프 언롤·꼬리 처리 코드를 읽고 성능 원인을 설명한다 |
| [ARM-A05](../tracks/ARM.html#ARM-A05) | Adv · mac | int8 GEMM 마이크로커널: SDOT·SMMLA | AI 추론의 핵심 연산을 NEON 도트곱/행렬곱 명령으로 최적화한다 |
| [MEM-A07](../tracks/MEM.html#MEM-A07) | Adv · mac | 하드웨어 prefetcher 모델: stride·stream | prefetcher의 accuracy/coverage/timeliness 지표를 측정한다. |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ 행렬곱에서 ijk와 ikj의 속도 차이를 캐시 라인으로 설명
- ☐ 타일 크기는 어떻게 정하나 (L1/L2 용량, 세 타일이 동시에 들어가야 함)
- ☐ AoS vs SoA — 어느 쪽이 벡터화에 유리하고 왜
- ☐ 컴파일러가 루프를 벡터화하지 못하는 대표 이유 (aliasing, 루프 의존성, 분기)

## 3. 공유 메모리 병렬 — OpenMP

노드 하나 안의 여러 코어를 쓰는 표준 방법이다. 레이스와 false sharing은 동시성 가이드에서 먼저 익혀 두면 좋다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [HPC-B03](../tracks/HPC.html#HPC-B03) ⭐ 🆕 | Basic · mac | OpenMP 입문: parallel for·데이터 공유 속성·reduction | OpenMP의 fork-join 모델과 shared/private/firstprivate, reduction 절을 정확히 쓴다. |
| [HPC-B04](../tracks/HPC.html#HPC-B04) ⭐ 🆕 | Basic · mac | OpenMP 스케줄링: static·dynamic·guided와 부하 불균형 | 반복마다 비용이 다른 루프에서 스케줄 정책과 chunk 크기가 성능에 주는 영향을 설명한다. |
| [HPC-B05](../tracks/HPC.html#HPC-B05) ⭐ 🆕 | Basic · mac | Work-Span 모델: 병렬 reduction·prefix sum과 Brent 정리 | 병렬 알고리즘을 work·span·병렬도로 분석하고 실제 속도 향상의 상한을 예측한다. |
| [HPC-B07](../tracks/HPC.html#HPC-B07) 🆕 | Basic · mac | 병렬 reduction의 비결정성: 스레드 수와 결과가 바뀌는 이유 | 부동소수점 덧셈의 비결합성이 병렬 결과의 재현성을 깨는 원리와 해결책을 안다. |
| [HPC-M02](../tracks/HPC.html#HPC-M02) 🆕 | Mid · mac | 이기종 코어 스케줄링: Apple Silicon P/E 코어와 부하 분산 | 성능이 다른 코어가 섞인 머신에서 정적 분할이 깨지는 이유와 대응을 측정으로 보인다. |
| [OS-A05](../tracks/OS.html#OS-A05) | Adv · mac | work-stealing 스레드 풀: Chase-Lev deque | 로드 밸런싱을 위한 작업 훔치기 스케줄러와 lock-free deque의 메모리 오더링을 구현한다(false sharing 측정은 see:MEM) |
| [MEM-M09](../tracks/MEM.html#MEM-M09) | Mid · mac | NUMA: 지역성, first-touch, 원격 접근 비용 | NUMA 시스템에서 메모리 배치가 지연·대역폭에 주는 영향을 모델링한다. |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ `#pragma omp parallel for reduction(+:s)`가 내부적으로 하는 일
- ☐ static vs dynamic 스케줄은 언제 어느 쪽이 이기나
- ☐ work W, span S일 때 P코어 시간의 상한 (Brent 정리)
- ☐ 병렬 합 결과가 스레드 수마다 마지막 자리까지 달라지는 이유와 해결책
- ☐ P코어/E코어가 섞인 머신에서 static 분할이 느린 이유

## 4. 대표 커널 — HPC 문제 은행

HPC 면접과 과제는 거의 이 커널들의 변형이다. 각 커널이 메모리 바운드인지, 연산 바운드인지, 불규칙한지를 먼저 말할 수 있어야 한다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [HPC-B08](../tracks/HPC.html#HPC-B08) ⭐ 🆕 | Basic · mac | Stencil 1: 2D 열전도 방정식 Jacobi 반복 (직렬 기준 구현) | HPC의 대표 패턴인 stencil 계산을 정확히 구현하고 연산 강도를 계산한다. |
| [HPC-M01](../tracks/HPC.html#HPC-M01) ⭐ 🆕 | Mid · mac | Stencil 2: 공간 블로킹·OpenMP 병렬화와 roofline 배치 | stencil을 캐시 블로킹과 스레드 병렬화로 최적화하고 개선 원인을 roofline으로 설명한다. |
| [HPC-A01](../tracks/HPC.html#HPC-A01) 🆕 | Adv · mac | Stencil 3: Temporal Blocking·Wavefront로 메모리 천장 넘기 | 시간 축까지 블로킹해 메모리 바운드 stencil의 연산 강도를 올리는 고급 기법을 구현한다. |
| [HPC-B09](../tracks/HPC.html#HPC-B09) ⭐ 🆕 | Basic · mac | SpMV 1: CSR·COO·ELL 포맷과 대역폭 바운드 분석 | 희소 행렬-벡터 곱의 포맷별 메모리 접근과 성능 상한을 계산한다. |
| [HPC-M06](../tracks/HPC.html#HPC-M06) 🆕 | Mid · mac | SpMV 2: 병렬화와 행 길이 불균형, SELL-C-σ | 희소 행렬의 행 길이 편차가 병렬 성능을 깨는 원리와 포맷 수준 해결책을 안다. |
| [HPC-M07](../tracks/HPC.html#HPC-M07) ⭐ 🆕 | Mid · mac | N-body 1: O(N²) 직접합의 SIMD·OpenMP 최적화 | 연산 바운드 커널을 레이아웃·벡터화·병렬화로 피크 FLOP에 가깝게 끌어올린다. |
| [HPC-A02](../tracks/HPC.html#HPC-A02) 🆕 | Adv · mac | N-body 2: Barnes-Hut 트리의 병렬 구축과 순회 | O(N²)을 O(N log N)으로 줄이는 계층 근사와 그 병렬화의 어려움(불규칙성)을 다룬다. |
| [NUM-M10](../tracks/NUM.html#NUM-M10) | Mid · mac | 반복 해법 2: Conjugate Gradient와 전처리 | 대형 희소 SPD 시스템을 CG/PCG로 풀고 전처리 효과를 측정한다. |
| [HPC-M09](../tracks/HPC.html#HPC-M09) 🆕 | Mid · mac | 병렬 CG 솔버: dot product allreduce가 확장성을 막는 이유 | Krylov 솔버를 분산 병렬화할 때 전역 동기화가 병목이 되는 구조를 이해한다. |
| [HPC-A04](../tracks/HPC.html#HPC-A04) 🆕 | Adv · mac | Multigrid V-cycle 포아송 솔버 | 격자 크기와 무관한 반복 수로 수렴하는 multigrid의 원리와 병렬 구현 쟁점을 안다. |
| [DSP-M01](../tracks/DSP.html#DSP-M01) | Mid · mac | Radix-2 DIT FFT 구현 (float) | 면접 단골인 FFT를 비트 반전과 butterfly로 직접 구현한다 |
| [HPC-A03](../tracks/HPC.html#HPC-A03) 🆕 | Adv · mac | 분산 2D FFT: Pencil/Slab 분해와 전치 All-to-All | 다차원 FFT를 분산할 때 통신이 전치(all-to-all)로 바뀌는 구조를 구현으로 이해한다. |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ 5-point stencil 한 셀의 FLOP와 바이트 → 연산 강도, 그래서 어디에 막히나
- ☐ CSR SpMV가 대역폭 바운드인 이유와 x 벡터 gather 문제
- ☐ N-body 직접합은 왜 연산 바운드이고, Barnes-Hut은 무엇을 근사하나
- ☐ 분산 CG에서 반복마다 allreduce가 확장성을 막는 이유
- ☐ multigrid가 격자 크기와 무관한 반복 수로 수렴하는 직관

## 5. 분산 메모리 병렬 — MPI

노드 여러 대로 넘어가는 단계다. 메모리를 공유하지 않으니 "누가 무엇을 언제 보내나"를 직접 설계해야 한다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [HPC-B10](../tracks/HPC.html#HPC-B10) ⭐ 🆕 | Basic · mac | MPI 입문: rank·Send/Recv·π 적분과 메시지 비용 | 분산 메모리 모델(프로세스마다 독립 메모리)과 MPI 점대점 통신의 기본을 익힌다. |
| [HPC-M03](../tracks/HPC.html#HPC-M03) ⭐ 🆕 | Mid · mac | MPI 집합 통신: Bcast·Allreduce·Alltoall 실측과 α-β 적합 | MPI 집합 통신 API를 쓰고 메시지 크기별 비용을 모델과 대조한다. |
| [HPC-M04](../tracks/HPC.html#HPC-M04) ⭐ 🆕 | Mid · mac | 영역 분할과 Halo Exchange: MPI 2D 열전도 | 분산 메모리에서 격자를 나누고 경계(ghost cell)를 교환하는 표준 패턴을 구현한다. |
| [HPC-M05](../tracks/HPC.html#HPC-M05) ⭐ 🆕 | Mid · mac | 통신-계산 오버랩: Isend/Irecv와 내부/경계 분리 | 비블로킹 통신으로 통신 시간을 계산 뒤에 숨기는 방법과 그 한계를 측정한다. |
| [HPC-M08](../tracks/HPC.html#HPC-M08) 🆕 | Mid · mac | 병렬 정렬: Sample Sort (OpenMP → MPI) | 분산 정렬의 핵심인 splitter 선택과 all-to-all 재분배를 구현한다. |
| [HPC-M10](../tracks/HPC.html#HPC-M10) 🆕 | Mid · mac | Hybrid MPI + OpenMP: 프로세스×스레드 구성 탐색 | 노드 안은 스레드, 노드 사이는 프로세스로 나누는 하이브리드 구성의 장단점을 측정한다. |
| [HPC-A05](../tracks/HPC.html#HPC-A05) 🆕 | Adv · mac | 분산 동적 부하 분산: Master-Worker vs 분산 Work Stealing | 작업 비용을 미리 모를 때 분산 메모리에서 일을 나누는 전략을 비교한다. |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ MPI_Send가 블로킹인데도 데드락이 나는 경우와 피하는 법 (Sendrecv, 짝/홀 순서)
- ☐ 1D vs 2D 영역 분할의 통신량 비교 (표면적/부피 비)
- ☐ Isend/Irecv로 통신을 숨기려면 계산을 어떻게 나눠야 하나 (내부/경계)
- ☐ MPI + OpenMP 하이브리드를 쓰는 이유 (메모리, halo 감소)와 단점
- ☐ master-worker의 병목과 work stealing의 장점

## 6. 통신 알고리즘과 네트워크

MPI 집합 통신과 NCCL이 내부에서 쓰는 알고리즘과 하드웨어다. AI 인프라 면접에서는 이 층 비중이 크다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [DCN-M12](../tracks/DCN.html#DCN-M12) ⭐ | Mid · mac | Collective α-β 비용 모델: Ring·Tree·Halving-Doubling | 집합 통신 알고리즘들의 지연(α)·대역폭(β)·연산(γ) 항을 유도하고 메시지 크기별 최적 알고리즘을 고른다. |
| [DCN-M13](../tracks/DCN.html#DCN-M13) | Mid · mac | Recursive Halving-Doubling All-Reduce 구현 | halving-doubling(Rabenseifner) all-reduce를 직접 구현하고 비(非) 2의 거듭제곱 처리를 익힌다. |
| [DCN-A01](../tracks/DCN.html#DCN-A01) | Adv · mac | 계층적(2D) All-Reduce와 Rail-optimized 토폴로지 | 노드 내 NVLink와 노드 간 스케일아웃 네트워크를 함께 쓰는 계층적 collective를 모델링한다. |
| [GPUSYS-A02](../tracks/GPUSYS.html#GPUSYS-A02) | Adv · mac | 멀티 GPU Collective: Ring All-Reduce | 분산 학습/추론의 핵심 통신인 all-reduce 알고리즘과 비용 모델을 구현한다 |
| [DCN-B09](../tracks/DCN.html#DCN-B09) | Basic · linux | RDMA 개념과 Verbs 객체: PD·MR·QP·CQ | RDMA verbs 객체 모델과 lkey/rkey 보호 모델을 Soft-RoCE에서 직접 확인한다. |
| [DCN-B11](../tracks/DCN.html#DCN-B11) | Basic · linux | One-sided RDMA WRITE/READ vs SEND | one-sided 연산이 원격 CPU 개입 없이 동작하는 구조와 성능 특성을 비교한다. |
| [DCN-M02](../tracks/DCN.html#DCN-M02) | Mid · linux | RDMA 성능 패턴: Unsignaled·Inline·Doorbell Batching | RNIC 친화적인 verbs 사용 패턴으로 메시지율을 끌어올린다. |
| [DCN-A03](../tracks/DCN.html#DCN-A03) | Adv · mac | NCCL 내부 구조: 토폴로지 탐지·채널·프로토콜 | NCCL이 토폴로지를 탐지해 ring/tree 채널과 LL/LL128/Simple 프로토콜을 고르는 과정을 해석한다. |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ ring all-reduce의 시간 식과, 메시지가 작을 때 tree가 이기는 이유
- ☐ α(지연)와 β(대역폭) 중 무엇이 지배하는지 메시지 크기로 판정
- ☐ RDMA one-sided WRITE가 SEND보다 빠를 수 있는 이유 (원격 CPU 개입 없음)

## 7. GPU 가속

현대 HPC 연산의 대부분은 GPU에서 돈다. Mac에서는 Metal로 같은 개념을 연습한다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [GPGPU-B05](../tracks/GPGPU.html#GPGPU-B05) | Basic · mac | 지연 은닉과 Little's Law: 필요한 병렬도 계산 | GPU가 많은 스레드로 메모리 지연을 숨기는 원리를 Little's law로 정량화한다 |
| [GPGPU-B06](../tracks/GPGPU.html#GPGPU-B06) | Basic · mac | 메모리 대역폭 마이크로벤치마크 (M2 GPU) | GPU 메모리 대역폭의 실측치와 이론치 차이를 측정하는 방법을 익힌다 |
| [GPGPU-M01](../tracks/GPGPU.html#GPGPU-M01) | Mid · mac | 병렬 Reduction: 단계별 최적화 | 고전적 reduction 최적화 단계(divergence 제거, sequential addressing, SIMD 셔플)를 체득한다 |
| [GPGPU-M02](../tracks/GPGPU.html#GPGPU-M02) | Mid · mac | Prefix Sum(Scan): Hillis-Steele vs Blelloch | 병렬 스캔의 work-efficiency 차이와 대형 배열의 다중 블록 스캔 구조를 이해한다 |
| [GPGPU-M05](../tracks/GPGPU.html#GPGPU-M05) ⭐ | Mid · mac | Tiled GEMM: Shared Memory 타일링과 Register Blocking | GEMM 성능을 데이터 재사용(arithmetic intensity) 관점에서 단계적으로 끌어올린다 |
| [GPGPU-A02](../tracks/GPGPU.html#GPGPU-A02) | Adv · mac | 행렬 유닛 GEMM: simdgroup_matrix / Tensor Core | GPU의 행렬 전용 유닛(Apple simdgroup_matrix, NVIDIA Tensor Core)을 쓰는 GEMM 구조를 익힌다 |
| [GPGPU-A06](../tracks/GPGPU.html#GPGPU-A06) | Adv · mac | 커널 퓨전과 런치 오버헤드 | 작은 커널을 여러 번 띄우는 비용과 elementwise 연산 퓨전의 효과를 측정한다 |
| [GPGPU-A07](../tracks/GPGPU.html#GPGPU-A07) | Adv · mac | GPU 수치 결정성과 결과 검증 하네스 | 병렬 합산 순서로 인한 비결정성과 허용 오차 기반 검증 방법을 정립한다 |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ Little's Law로 메모리 지연을 숨기는 데 필요한 동시 요청 수 계산
- ☐ 병렬 reduction을 단계별로 빠르게 만드는 순서 (divergence, bank conflict, warp shuffle)
- ☐ Hillis-Steele vs Blelloch scan의 work·span 차이
- ☐ 커널 퓨전이 이득인 이유 (메모리 왕복, 런치 오버헤드)

## 8. 대규모 시스템 — AI 학습과 운영

수천 노드 규모에서만 보이는 문제들이다. 장애는 "일어나느냐"가 아니라 "얼마나 자주"의 문제가 된다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [AISYS-B02](../tracks/AISYS.html#AISYS-B02) | Basic · mac | 학습 컴퓨트 예산: 6ND, MFU와 GPU-시간 | 모델 크기·토큰 수·클러스터 크기·MFU로 학습 기간과 비용을 봉투 뒷면 계산한다 |
| [AISYS-B07](../tracks/AISYS.html#AISYS-B07) ⭐ | Basic · mac | 데이터 병렬 1: 그래디언트 평균과 통신량 | DDP가 수학적으로 단일 디바이스 학습과 같음을 확인하고 스텝당 통신 바이트를 계산한다 |
| [AISYS-B08](../tracks/AISYS.html#AISYS-B08) | Basic · mac | 텐서 병렬 1: Megatron Column/Row 분할 MLP | MLP·attention 가중치를 열/행으로 나눠 GEMM을 분산하는 방법과 층당 필요한 통신 횟수를 이해한다 |
| [AISYS-M06](../tracks/AISYS.html#AISYS-M06) | Mid · mac | 파이프라인 병렬 1: GPipe 스케줄과 버블 비율 | 마이크로배치 파이프라인의 유휴 시간(bubble)을 계산하고 시뮬레이션으로 확인한다 |
| [AISYS-A06](../tracks/AISYS.html#AISYS-A06) | Adv · mac | 3D 병렬 구성 탐색기: DP×TP×PP×CP 스텝 시간 모델 | 주어진 모델·클러스터에서 병렬 구성 조합의 메모리 적합성과 스텝 시간을 추정해 최적 구성을 고른다 |
| [HPC-A06](../tracks/HPC.html#HPC-A06) 🆕 | Adv · mac | 체크포인트/재시작과 최적 주기: Young/Daly 공식 | 대규모 장시간 작업에서 장애를 견디는 체크포인트 전략을 수식과 실험으로 설계한다. |
| [HPC-A07](../tracks/HPC.html#HPC-A07) 🆕 | Adv · mac | 병렬 I/O: 파일당 프로세스 vs MPI-IO 집합 쓰기 | 수천 프로세스가 결과를 저장할 때의 I/O 패턴과 메타데이터 병목을 이해한다. |
| [HPC-A08](../tracks/HPC.html#HPC-A08) 🆕 | Adv · mac | HPC 잡 스케줄러: Slurm 개념과 Backfill 시뮬레이터 | 클러스터 자원 관리자가 잡을 배치하는 방식과 사용자 쪽 잡 스크립트 작성을 안다. |
| [HPC-A09](../tracks/HPC.html#HPC-A09) ⭐ 🆕 | Adv · mac | 종합: 하이브리드 병렬 미니앱과 확장성 리포트 | 트랙 전체를 묶어 '측정하고 설명할 수 있는' HPC 미니앱과 성능 리포트를 만든다. |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ 데이터 병렬 · 텐서 병렬 · 파이프라인 병렬이 각각 무엇을 나누고 무엇을 통신하나
- ☐ 노드 1만 대일 때 시스템 MTBF와 최적 체크포인트 주기 (Young/Daly)
- ☐ 파일당 프로세스 쓰기 vs MPI-IO 집합 쓰기의 장단점
- ☐ Slurm에서 --ntasks, --cpus-per-task가 MPI rank·OpenMP 스레드에 어떻게 대응하나

## ⭐ 핵심 경로 20개

AI 인프라 · GPU · 칩 회사 면접 기준. 하루 2개면 약 2주. 마지막 HPC-A09 미니앱이 포트폴리오가 된다.

| # | 아이템 | 제목 | 층 |
|---|---|---|---|
| 1 | [HPC-B01](../tracks/HPC.html#HPC-B01) | 성능 측정 하네스: 반복·워밍업·통계와 GFLOP/s·GB/s | 1층 |
| 2 | [HPC-B02](../tracks/HPC.html#HPC-B02) | Strong vs Weak Scaling: Amdahl·Gustafson 실측과 Karp-Flatt | 1층 |
| 3 | [ARCH-A11](../tracks/ARCH.html#ARCH-A11) | 성능 모델: Amdahl과 Roofline | 1층 |
| 4 | [ARCH-M07](../tracks/ARCH.html#ARCH-M07) | 캐시 친화 코드: 루프 순서와 타일링 | 2층 |
| 5 | [HPC-B06](../tracks/HPC.html#HPC-B06) | 데이터 레이아웃: AoS vs SoA와 자동 벡터화 | 2층 |
| 6 | [HPC-B03](../tracks/HPC.html#HPC-B03) | OpenMP 입문: parallel for·데이터 공유 속성·reduction | 3층 |
| 7 | [HPC-B04](../tracks/HPC.html#HPC-B04) | OpenMP 스케줄링: static·dynamic·guided와 부하 불균형 | 3층 |
| 8 | [HPC-B05](../tracks/HPC.html#HPC-B05) | Work-Span 모델: 병렬 reduction·prefix sum과 Brent 정리 | 3층 |
| 9 | [HPC-B08](../tracks/HPC.html#HPC-B08) | Stencil 1: 2D 열전도 방정식 Jacobi 반복 (직렬 기준 구현) | 4층 |
| 10 | [HPC-M01](../tracks/HPC.html#HPC-M01) | Stencil 2: 공간 블로킹·OpenMP 병렬화와 roofline 배치 | 4층 |
| 11 | [HPC-B09](../tracks/HPC.html#HPC-B09) | SpMV 1: CSR·COO·ELL 포맷과 대역폭 바운드 분석 | 4층 |
| 12 | [HPC-M07](../tracks/HPC.html#HPC-M07) | N-body 1: O(N²) 직접합의 SIMD·OpenMP 최적화 | 4층 |
| 13 | [HPC-B10](../tracks/HPC.html#HPC-B10) | MPI 입문: rank·Send/Recv·π 적분과 메시지 비용 | 5층 |
| 14 | [HPC-M03](../tracks/HPC.html#HPC-M03) | MPI 집합 통신: Bcast·Allreduce·Alltoall 실측과 α-β 적합 | 5층 |
| 15 | [HPC-M04](../tracks/HPC.html#HPC-M04) | 영역 분할과 Halo Exchange: MPI 2D 열전도 | 5층 |
| 16 | [HPC-M05](../tracks/HPC.html#HPC-M05) | 통신-계산 오버랩: Isend/Irecv와 내부/경계 분리 | 5층 |
| 17 | [DCN-M12](../tracks/DCN.html#DCN-M12) | Collective α-β 비용 모델: Ring·Tree·Halving-Doubling | 6층 |
| 18 | [GPGPU-M05](../tracks/GPGPU.html#GPGPU-M05) | Tiled GEMM: Shared Memory 타일링과 Register Blocking | 7층 |
| 19 | [AISYS-B07](../tracks/AISYS.html#AISYS-B07) | 데이터 병렬 1: 그래디언트 평균과 통신량 | 8층 |
| 20 | [HPC-A09](../tracks/HPC.html#HPC-A09) | 종합: 하이브리드 병렬 미니앱과 확장성 리포트 | 8층 |

## 직무별로 더할 것

| 목표 직무 | 더할 아이템 | 이유 |
|---|---|---|
| AI 학습 인프라 (NVIDIA, 하이퍼스케일러) | [DCN-M13](../tracks/DCN.html#DCN-M13) · [DCN-A01](../tracks/DCN.html#DCN-A01) · [DCN-A03](../tracks/DCN.html#DCN-A03) · [AISYS-M06](../tracks/AISYS.html#AISYS-M06) · [AISYS-A06](../tracks/AISYS.html#AISYS-A06) · [HPC-A06](../tracks/HPC.html#HPC-A06) | collective 알고리즘과 학습 병렬화, 장애 대응 |
| GPU · 가속기 성능 | [GPGPU-M01](../tracks/GPGPU.html#GPGPU-M01) · [GPGPU-M02](../tracks/GPGPU.html#GPGPU-M02) · [GPGPU-A02](../tracks/GPGPU.html#GPGPU-A02) · [GPGPU-A06](../tracks/GPGPU.html#GPGPU-A06) · [AIPERF-M07](../tracks/AIPERF.html#AIPERF-M07) · [ARM-A05](../tracks/ARM.html#ARM-A05) | 커널 최적화와 계층적 roofline |
| 과학 계산 · 국립연구소 | [HPC-A01](../tracks/HPC.html#HPC-A01) · [HPC-A03](../tracks/HPC.html#HPC-A03) · [HPC-A04](../tracks/HPC.html#HPC-A04) · [HPC-M09](../tracks/HPC.html#HPC-M09) · [HPC-A07](../tracks/HPC.html#HPC-A07) · [HPC-A08](../tracks/HPC.html#HPC-A08) | PDE 솔버, 병렬 FFT, 병렬 I/O, 잡 스케줄링 |
| 임베디드 · 엣지 성능 | [HPC-B06](../tracks/HPC.html#HPC-B06) · [ARM-M06](../tracks/ARM.html#ARM-M06) · [EML-B02](../tracks/EML.html#EML-B02) · [ARM-A05](../tracks/ARM.html#ARM-A05) · [MEM-A07](../tracks/MEM.html#MEM-A07) · [HPC-M02](../tracks/HPC.html#HPC-M02) | 단일 SoC 안에서 SIMD · 캐시 · 이기종 코어 최적화 |

## 같은 개념이 여러 트랙에 나오는 이유

중복처럼 보이지만 관점이나 도메인이 달라서 일부러 둔 것들이다. 순서대로 비교하며 풀면 좋다.

| 개념 | 아이템 | 무엇이 다른가 |
|---|---|---|
| Amdahl · roofline | [ARCH-A11](../tracks/ARCH.html#ARCH-A11) · [HPC-B02](../tracks/HPC.html#HPC-B02) · [AIPERF-B06](../tracks/AIPERF.html#AIPERF-B06) · [AIPERF-M07](../tracks/AIPERF.html#AIPERF-M07) | 모델 → 스레드 실측 → AI 커널 배치 → 캐시 계층별 |
| GEMM 타일링 | [ARCH-M07](../tracks/ARCH.html#ARCH-M07) · [EML-B02](../tracks/EML.html#EML-B02) · [MEM-A08](../tracks/MEM.html#MEM-A08) · [GPGPU-M05](../tracks/GPGPU.html#GPGPU-M05) · [ARM-A05](../tracks/ARM.html#ARM-A05) | 캐시 미스 → ML 커널 → roofline → GPU → NEON int8 |
| all-reduce | [HPC-M03](../tracks/HPC.html#HPC-M03) · [DCN-M12](../tracks/DCN.html#DCN-M12) · [DCN-M13](../tracks/DCN.html#DCN-M13) · [GPUSYS-A02](../tracks/GPUSYS.html#GPUSYS-A02) · [AISYS-B07](../tracks/AISYS.html#AISYS-B07) | MPI API 실측 → 비용 모델 → 구현 → GPU → 학습 |
| prefix sum | [HPC-B05](../tracks/HPC.html#HPC-B05) · [GPGPU-M02](../tracks/GPGPU.html#GPGPU-M02) | CPU work-span → GPU scan |
| work stealing | [OS-A05](../tracks/OS.html#OS-A05) · [HPC-M02](../tracks/HPC.html#HPC-M02) · [HPC-A05](../tracks/HPC.html#HPC-A05) | 공유 메모리 deque → 이기종 코어 → 분산 메모리 |
| CG 솔버 | [NUM-M10](../tracks/NUM.html#NUM-M10) · [HPC-M09](../tracks/HPC.html#HPC-M09) | 수치 알고리즘·전처리 → 분산 병렬화와 allreduce 병목 |

## 참고자료

- Hager & Wellein, *Introduction to High Performance Computing for Scientists and Engineers* — 단일 코어부터 MPI까지 한 권
- Victor Eijkhout, *The Art of HPC* — 무료 교재 시리즈, https://theartofhpc.com
- LLNL HPC Tutorials (OpenMP · MPI · Parallel Computing 입문) — https://hpc-tutorials.llnl.gov
- OpenMP 명세 — https://www.openmp.org/specifications/ · MPI 표준 — https://www.mpi-forum.org/docs/
- Williams, Waterman, Patterson, *Roofline* (CACM 2009)
- 동시성 연습 가이드 — 레이스 · 원자 연산 · false sharing은 그쪽 문서에서 먼저
