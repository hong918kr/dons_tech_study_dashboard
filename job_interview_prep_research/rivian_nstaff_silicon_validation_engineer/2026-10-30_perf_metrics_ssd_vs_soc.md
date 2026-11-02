# 성능 메트릭 4종 — SSD에서 하던 것 vs Rivian SoC 검증에서 할 것
<!-- theme: pastel -->

> **JD 원문**: "Build benchmark tools to evaluate performance of the hardware system, including **throughput, latency, resource utilization, IO or compute frequency** metrics" · **목적**: Don의 SSD(Solidigm·SK hynix) 성능 경험을 Rivian AI accel SoC 언어로 번역 · **같이 볼 것**: [최종 훑어보기](2026-10-30_final_skim.html) · [HM screen deck §4 S7](2026-10-25_rivian_hm_screen_prep.html) · **코드**: `cBasicsCram/bench_sketch.c` (컴파일·실행 확인)

## TL;DR — 면접에서 이 3문장이면 된다

- **메트릭 4개는 똑같다. 바뀌는 건 "무엇을 재느냐(DUT)"와 "어디서 재느냐(관측 지점)"다.**
  - SSD: DUT = **스토리지 디바이스 전체**, 관측 = 주로 **호스트 쪽**(fio, NVMe 명령 완료 시간) + FW 내부 텔레메트리.
  - Rivian: DUT = **SoC 안의 블록들**(NPU, DMA, DDR, NoC, ISP/카메라 경로, 칩간 링크), 관측 = **칩 안에서 bare-metal**(cycle counter, HW 성능 카운터).
- **둘 다 Little's law로 묶인다**: `동시에 진행 중인 일 = throughput × latency`. SSD의 QD(queue depth) ↔ SoC의 **outstanding transactions**.
- **영어 한 줄**: "It's the same four metrics I tuned on SSDs — throughput, latency, utilization, and frequency scaling — but the device under test moves from the whole drive seen from the host to individual SoC blocks measured on-chip with cycle counters and hardware performance counters."

## 1. JD 문장 단어별 해석

| JD 단어 | 뜻 (이 팀 맥락) | 확신도 |
|---|---|---|
| **benchmark tools** | 한 번 재는 스크립트가 아니라 **재사용 가능한 툴**: 워크로드 생성 + 측정 + 결과 수집 + 리포트/회귀 비교 | [JD] |
| **hardware system** | 자체 SoC(RAP1 계열) 블록 + 보드(DDR, 카메라, Ethernet, PCIe) | [추정] |
| **throughput** | 단위 시간당 처리량: GB/s(DMA·DDR·링크), TOPS·inferences/s(NPU), frames/s(카메라·ISP) | [추정] |
| **latency** | 한 작업의 시작→끝: 메모리 접근 ns, DMA 완료 µs, 인터럽트 응답, 레이어/추론 ms, **센서→결과 end-to-end** | [추정] |
| **resource utilization** | 피크 대비 실제 사용률: NPU MAC 사용률, DDR 대역폭 %, SRAM 점유, DMA 채널 busy, CPU 부하 | [추정] |
| **IO or compute frequency** | 두 가지로 읽힘 → **면접에서 clarify 하면 좋음**: ① **클럭 주파수**(IO 도메인·compute 도메인) 바꿔가며 성능/전력 스케일링 ② 작업 발생 **빈도/rate**(IO ops/s, compute ops/s) | ①이 더 유력 [추정] |

- clarify 문장: "By IO or compute frequency, do you mean sweeping the clock of each domain to see how performance scales, or the rate of IO and compute operations? I've done both — frequency sweeps through shmoo work, and IOPS on SSDs."

## 2. 한눈에 비교표

| 메트릭 | SSD 업계 (Don 경험) | Rivian AI accel SoC (이 포지션) | 공통 원리 |
|---|---|---|---|
| **Throughput** | Seq read/write **MB/s** (128K), Random **IOPS** (4K), mixed 70/30 | DMA/DDR **GB/s**, NPU **effective TOPS**, **inferences/s**, 카메라 **frames/s**, Ethernet/PCIe 링크 GB/s | 이론 peak 대비 **% of peak**로 말하기 |
| **Latency** | 4K QD1 read **µs**, **QoS tail** p99/p99.99/p99.999, GC로 인한 tail | DDR load **ns**, DMA 완료 **µs**, IRQ latency, 레이어/추론 **ms**, **sensor→inference** end-to-end, **jitter·worst case** | 평균 말고 **분포(percentile)** |
| **Utilization** | NAND **channel/die busy %**, FW 코어 CPU %, DRAM 버퍼, PCIe 링크 사용률, **WAF** | **MAC 사용률**, DDR 대역폭 %, SRAM 점유, NoC/DMA busy, CPU %, **TOPS/W** | **병목 찾기** = 가장 먼저 100% 찍는 자원 |
| **Frequency** | 컨트롤러 코어 클럭, NAND I/F MT/s, PCIe GT/s, **thermal throttling**, power state | NPU/CPU/NoC 클럭, DDR data rate, MIPI lane Gbps, **DVFS 동작점**, Fmax(PVT) | 클럭 올렸는데 성능이 **안 오르면 병목은 다른 곳** |
| 워크로드 | fio job (bs, QD, numjobs, rw mix) | 마이크로벤치(memcpy/DMA, pointer-chase, 레이어 단위) + 실제 모델·카메라 파이프라인 | 마이크로 → 매크로 순서 |
| 측정 위치 | 호스트 OS (fio clat), NVMe telemetry log, PCIe analyzer, 전력계 | SoC 위 bare-metal, **PMU cycle counter**, **HW perf counter**(AXI 모니터·DDR 컨트롤러·NPU busy 카운터), scope | **측정 오버헤드** 빼기 |
| 상태 통제 | **preconditioning**, steady state, FOB vs 꽉 찬 드라이브 | **캐시/TLB warm 상태**, SRAM vs DDR 배치, 다른 마스터 트래픽 | 같은 상태에서 반복 측정 |
| 숨은 성능 절벽 | **SLC cache 소진**, GC, read disturb 재시도 | **SRAM 넘쳐 DDR로 spill**, DDR refresh, 다른 마스터와 경합, 캐시 thrash | 절벽 지점 찾는 sweep |
| 고객 언어 | 데이터센터 QoS (Google/Meta: p99.99 SLA) | 자동차: **프레임 deadline**, 결정성, 기능 안전 | SLA = 목표 percentile |

## 3. Throughput (처리량)

### 3.1 SSD에서

- **두 가지 단위**: 큰 블록 순차 = **MB/s**(대역폭), 작은 블록 랜덤 = **IOPS**. 변환: `MB/s = IOPS × block size` → 1M IOPS × 4 KiB ≈ **4.1 GB/s**.
- **천장 계산 (호스트 링크)**: PCIe Gen4 = 16 GT/s/lane, 128b/130b 인코딩 → lane당 ≈ 1.97 GB/s → **x4 ≈ 7.88 GB/s raw**, TLP/DLLP 헤더 오버헤드 빼면 실측 최고 ≈ 7 GB/s대. Gen5 x4 ≈ **15.75 GB/s raw** → 실측 ≈ 14 GB/s대.
- **천장 계산 (NAND 쪽)**: `channels × dies/channel × planes × page size / tPROG(또는 tR)` — 쓰기는 보통 NAND 쪽이, 읽기는 보통 PCIe 쪽이 먼저 막힘.
- **함정**: FOB(새 드라이브) 성능 ≠ steady state. **SLC cache** 소진 후 쓰기 속도 절벽. GC가 돌면 host write가 깎임(**WAF** = NAND writes / host writes).
- **측정**: fio `--bs=128k --rw=read --iodepth=32 --numjobs=4`, preconditioning(2× 용량 쓰기) 후 steady state 구간만.

### 3.2 Rivian SoC에서

| 대상 | 단위 | 이론 peak 계산 (예시 숫자) | 재는 법 |
|---|---|---|---|
| **DDR (LPDDR5 등)** | GB/s | LPDDR5-6400 × 64-bit = 6400 MT/s × 8 B = **51.2 GB/s** | 큰 버퍼 read/write/copy 스트림 + DDR 컨트롤러 카운터 |
| **DMA 엔진** | GB/s | 버스 폭 × 클럭: 128-bit AXI @ 1 GHz = **16 GB/s/포트** | 디스크립터 N개 실행 → 완료 IRQ까지 cycle |
| **NPU** | TOPS, inferences/s | `MAC 수 × 2 (곱+합) × 클럭`: 4096 MAC @ 1 GHz = **8.19 TOPS** (가상의 예) | 레이어/모델 실행 cycle → 실제 ops / 시간 |
| **카메라/ISP** | frames/s, MB/s | 8 MP × RAW12(1.5 B) × 30 fps = **360 MB/s/카메라** | 프레임 카운터, FS/FE 타임스탬프 |
| **칩간 링크/PCIe/Ethernet** | GB/s, Gbps | lane rate × lanes × 인코딩 효율 | 루프백 트래픽, 에러 없는 최대 rate |

- 숫자는 **설명용 예시**이고 RAP1 실제 스펙은 모름 [추정]. 면접에서도 "for example, if the NPU had 4K MACs at 1 GHz…" 식으로 가정을 먼저 말하기.
- **핵심 포인트: 자원 하나만 재면 안 된다** — 카메라 여러 대 + NPU + CPU가 **같은 DDR을 동시에** 쓴다. 단독 DMA가 45 GB/s 나와도 NPU가 돌 때 줄어든다 → **동시 트래픽(concurrency) 벤치마크**가 진짜. SSD에서 "mixed read/write 70/30 + 여러 namespace"를 재던 것과 같은 사고.

### 3.3 Throughput 측정 코드 (bare-metal 형태)

```c
/* Throughput: bytes moved per second over many iterations, timer overhead removed. */
uint64_t bench_bw(void *dst, const void *src, size_t bytes, uint32_t iters)
{
    uint64_t o = read_cycles();
    uint64_t overhead = read_cycles() - o;
    memcpy(dst, src, bytes);                       /* warm-up: cache/TLB state */
    uint64_t t0 = read_cycles();
    for (uint32_t i = 0; i < iters; i++)
        memcpy(dst, src, bytes);                   /* on target: DMA start + wait done */
    uint64_t cyc = read_cycles() - t0 - overhead;
    if (cyc == 0)
        cyc = 1;
    return (uint64_t)bytes * iters * CPU_HZ / cyc; /* bytes/s; 1 MiB*100*1e9 fits in u64 */
}
```

- 말할 포인트: **warm-up 1회**, 반복으로 timer 해상도 문제 줄이기, **timer 오버헤드 빼기**, 곱셈 overflow 확인, 결과를 **이론 peak와 비교**("we got 41 of 51.2 GB/s, 80% — typical DDR efficiency is 70–90% because of refresh, page misses and read/write turnaround").

## 4. Latency (지연)

### 4.1 SSD에서

- **대표 지표**: 4K random read **QD1** latency (TLC NAND tR 수십 µs + FW + ECC + 전송 → 보통 수십~100 µs 대).
- **QoS = tail latency**: 데이터센터 고객은 평균이 아니라 **p99 / p99.99 / p99.999** 를 SLA로 건다. 원인: GC, read retry(ECC 실패 후 재읽기), 쓰기와 읽기 같은 die 충돌, thermal throttling.
- **FW 내부 분해**: 명령 fetch → FTL 매핑 조회 → NAND 명령 → tR → ECC 디코드 → 호스트로 DMA → completion queue → MSI-X. 각 단계 타임스탬프로 **어디서 tail이 생기는지** 찾음.
- **측정**: fio `clat` percentile, `--iodepth=1`; FW 쪽은 내부 타임스탬프 trace.

### 4.2 Rivian SoC에서

| 대상 | 크기 | 왜 중요 | 재는 법 |
|---|---|---|---|
| **메모리 load latency** | SRAM 수 ns · DDR ~100 ns 대 | NPU/CPU가 기다리는 시간, prefetch로 숨길 수 있는지 | **pointer chase** (아래 코드) |
| **DMA 완료 latency** | µs | 작은 전송은 셋업 비용이 지배 | doorbell write → 완료 IRQ cycle |
| **인터럽트 latency** | 수백 ns~µs | 카메라 프레임 이벤트 처리 | HW 타임스탬프 vs ISR 진입, GPIO + scope |
| **NPU 레이어/추론 latency** | ms | 프레임 예산 안에 들어가는지 | 레이어 시작/끝 카운터 |
| **end-to-end (sensor→inference)** | ms | **30 fps면 프레임당 33.3 ms 예산** | 카메라 FS 타임스탬프 ~ 결과 시점, **같은 시간축** |
| **칩간 링크 latency** | ns~µs | 멀티칩 분할 추론 시 동기화 | ping-pong 메시지 왕복 / 2 |

- **SSD와 결정적 차이 — 결정성(determinism)**: SSD는 "p99.99가 몇 µs냐"를 통계로 관리하지만, 자동차는 **"worst case가 deadline을 넘으면 안 된다"**. 그래서 **max와 jitter(분포 폭)** 를 따로 보고, 최악 조건(다른 마스터 풀로드 + 고온 + DDR refresh)에서 잰다.
- **같은 시간축 필요**: 블록마다 다른 클럭이면 타임스탬프를 비교할 수 없음 → **전역 64-bit 타이머**(ARM generic timer) 하나로 통일. → [C 정리 C4](2026-10-29_c_basics_cram.html)의 hi-lo-hi 읽기가 여기서 쓰인다.

### 4.3 Latency 측정 코드 — percentile + pointer chase

```c
/* Latency: keep every sample, report percentiles, not just the mean. */
static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

uint64_t percentile(uint64_t *s, uint32_t n, uint32_t p)   /* p in 0..100, sorts s */
{
    qsort(s, n, sizeof s[0], cmp_u64);
    uint32_t idx = (uint32_t)(((uint64_t)p * (n - 1) + 50) / 100);
    return s[idx];
}

/* Random single-cycle order (Sattolo): a constant stride would be caught by the HW prefetcher
 * and report prefetch-hidden latency instead of real DRAM latency. */
void chase_build(uintptr_t *buf, size_t n, size_t *idx)
{
    uint32_t r = 2463534242u;                      /* xorshift32 seed */
    for (size_t i = 0; i < n; i++)
        idx[i] = i;
    for (size_t i = n - 1; i > 0; i--) {
        r ^= r << 13; r ^= r >> 17; r ^= r << 5;
        size_t j = r % i;                          /* j < i -> one big cycle */
        size_t t = idx[i]; idx[i] = idx[j]; idx[j] = t;
    }
    for (size_t i = 0; i < n; i++)
        buf[idx[i]] = (uintptr_t)&buf[idx[(i + 1) % n]];
}

uint64_t chase_run(const uintptr_t *buf, uint32_t hops, uintptr_t *sink)
{
    uintptr_t p = (uintptr_t)buf;
    uint64_t t0 = read_cycles();
    for (uint32_t i = 0; i < hops; i++)
        p = *(const uintptr_t *)p;
    uint64_t cyc = read_cycles() - t0;
    *sink = p;                                     /* keep the chain alive */
    return cyc / hops;
}
```

- **왜 pointer chase?** 다음 주소가 이전 load 결과라서 CPU가 load를 겹칠 수 없음 → 한 hop = 순수 접근 latency. 일반 배열 순회는 **bandwidth**를 재는 것.
- **왜 random 순서?** 이 Mac에서 실제로 돌려 보니 **고정 stride는 ≈5 ns/hop (prefetcher가 숨김), random cycle은 ≈55–62 ns/hop**. "측정이 틀리면 엉뚱한 걸 잰다"는 좋은 면접 예시.
- 버퍼 크기를 4 KB → 64 KB → 1 MB → 64 MB로 sweep 하면 **L1 / L2 / SLC / DDR 계단**이 그래프로 나온다 (SSD의 "SLC cache 절벽 찾기"와 같은 sweep 사고).
- 타이머 해상도 주의: 이 Mac의 `clock_gettime` 은 해상도가 거칠어 4 KB copy 하나는 0으로 찍힘 → **배치로 묶어서 평균**. 실리콘에선 cycle counter라 문제 적음.

## 5. Resource Utilization (자원 사용률)

### 5.1 SSD에서

- **NAND die/channel utilization**: 몇 개 die가 동시에 바쁜가. 랜덤 4K 읽기에서 IOPS가 안 나오면 die 병렬성이 부족한지(striping, 매핑) 확인.
- **FW CPU 사용률**: 코어별 역할 분리(front-end 명령 처리 / FTL / back-end NAND) → 한 코어가 100%면 그게 병목 → 작업 재배치, 핫패스 최적화.
- **DRAM 버퍼·매핑 테이블 캐시**, **PCIe 링크 사용률**(TLP 실효), ECC 엔진 busy.
- **효율 지표**: **WAF**(쓰기 증폭), 전력당 IOPS.

### 5.2 Rivian SoC에서

| 자원 | 정의 | 예시 계산 | 카운터 |
|---|---|---|---|
| **NPU MAC utilization** | 실제 연산 / (peak 연산 × 시간) | 8.19 TOPS peak에서 실측 5 TOPS → **61%** | NPU busy/stall cycle 카운터 |
| **DDR bandwidth utilization** | 실측 GB/s / peak GB/s | 41 / 51.2 → **80%** | DDR 컨트롤러 read/write beat 카운터 |
| **SRAM 점유** | 타일/가중치가 on-chip에 다 들어가는지 | 넘치면 DDR spill → 대역폭 급증 | 컴파일러 리포트 + DMA 트래픽 |
| **NoC / AXI 포트** | 포트별 트랜잭션, outstanding 수, stall | 한 포트만 포화 → 라우팅/QoS 문제 | AXI performance monitor (Zynq에도 APM IP 있음) |
| **DMA 채널** | busy 시간 비율 | 채널 1개 100%, 나머지 놀면 분배 문제 | 채널 상태 레지스터 샘플링 |
| **CPU** | 제어 코어 부하, 캐시 miss | NPU 대기 중 CPU가 디스크립터 준비 못 함 | ARM PMU (cycles, instructions, cache miss) |
| **전력 효율** | **TOPS/W**, 프레임당 mJ | 같은 성능 낮은 클럭이면 이득 | 전원 레일 전류 측정 |

### 5.3 Roofline — "utilization이 낮은 이유"를 설명하는 도구 ★

- **arithmetic intensity (AI)** = 연산 수 / DDR에서 옮긴 바이트 (ops/byte).
- **ridge point** = peak compute / peak bandwidth = 8.19e12 / 51.2e9 ≈ **160 ops/byte**.
- AI가 160보다 낮은 레이어 → **memory bound**: 최대 성능 = AI × 대역폭. 예: AI = 40 → 40 × 51.2 GB/s ≈ **2.05 TOPS = peak의 25%** → MAC을 늘려도 소용없음, **대역폭·재사용(tiling)** 을 개선해야 함.
- AI가 높은 레이어(큰 conv) → **compute bound**: MAC 사용률이 낮으면 스케줄링/파이프라인 stall 문제.
- SSD 비유: "IOPS가 안 나올 때 FW CPU가 병목인지 NAND die가 병목인지 가르던 것" = compute bound vs memory bound 판정.

## 6. IO or Compute Frequency (주파수)

### 6.1 해석 ① — 클럭 주파수 스윕 (더 유력)

- **방법**: IO 도메인(DDR data rate, NoC, 인터페이스)과 compute 도메인(NPU, CPU) 클럭을 **하나씩 바꿔가며** 같은 벤치마크 → 성능·전력 곡선.
- **읽는 법**

| 관찰 | 해석 |
|---|---|
| NPU 클럭 ↑ → 성능 **비례 ↑** | compute bound. 클럭이 곧 성능 |
| NPU 클럭 ↑ → 성능 **평평** | memory/IO bound. DDR·NoC가 병목 → NPU 클럭 낮춰 **전력 절약** 가능 |
| DDR rate ↑ → 성능 ↑ | memory bound 확인 |
| 특정 클럭 이상에서 에러 | **Fmax / margin 한계** → PVT·shmoo 영역으로 넘어감 |

- 결과물: **DVFS 동작점 테이블**(어떤 전압·클럭 조합이 성능/전력 최적인가), 제품 SKU 클럭 결정 근거.
- **Don 연결 ★**: SK hynix·Solidigm에서 한 **shmoo(전압×주파수 pass/fail 맵)** 가 바로 이 작업의 "기능" 버전. 여기에 "그 동작점에서 성능은 얼마인가"를 얹으면 benchmark가 된다. → "Shmoo tells you where it works; the frequency sweep tells you what performance you get at each working point."

### 6.2 해석 ② — 작업 빈도/rate

- IO frequency = 초당 IO 트랜잭션 수 (SSD의 **IOPS**, SoC의 DMA 디스크립터/s, 인터럽트/s, 카메라 frame rate).
- compute frequency = 초당 연산(ops/s) 또는 초당 커널 실행 수.
- 인터럽트 rate가 너무 높으면 CPU가 ISR에 묶임 → **interrupt coalescing**(SSD NVMe에도 있음: N개 완료마다 1번 MSI-X) — 공통 주제.

### 6.3 SSD에서 주파수가 성능에 영향 준 사례들 (Don 언어)

- **thermal throttling**: 온도 올라가면 컨트롤러/NAND 속도 낮춤 → 성능 계단 하락 → 벤치마크는 **온도까지 기록**해야 재현 가능.
- **NVMe power state (PS0–PS4), APST**: 저전력 상태 진입/복귀 latency가 tail latency를 만든다 → 자동차 SoC의 **DVFS 전환 latency**와 같은 문제.
- **NAND 인터페이스 속도**(MT/s)와 **PCIe Gen** 변화: 고속일수록 margin이 작아 shmoo 필요.

## 7. 측정 방법론 — 공통 원칙과 SoC에서 달라지는 점

| 원칙 | SSD에서 하던 것 | SoC에서 할 것 |
|---|---|---|
| 메트릭 정의를 먼저 | "IOPS at QD32, 4K, 100% random read, steady state" | "DDR copy GB/s, 4 MiB buffer, caches on, NPU idle" — **조건을 이름에 포함** |
| 상태 통제 | preconditioning, steady state | 캐시 flush/warm, 버퍼 위치(SRAM/DDR), 다른 마스터 off/on |
| 반복 & 분포 | 여러 run, percentile | N회 반복, p50/p99/max, 표준편차, **run-to-run 편차** 보고 |
| 이론값과 비교 | PCIe/NAND 천장 계산 | 버스 폭 × 클럭, MAC × 클럭 → **% of peak** |
| 측정 오버헤드 | fio 자체 CPU 사용 | 타이머 읽기 cycle, 카운터 설정 비용, printf 금지(측정 구간 안) |
| 교차 검증 | 호스트 fio vs FW 내부 통계 | **SW cycle 측정 vs HW 카운터** 결과가 맞는지 |
| 회귀 추적 | FW 빌드별 성능 DB | 실리콘 리비전·FW·클럭 설정별 **perf regression** 대시보드, 임계값 넘으면 알람 |
| 환경 기록 | 온도, FW 버전, 드라이브 상태 | 칩 ID/lot, 전압, 온도, 클럭 설정, 빌드 해시 |

### 7.1 Pre-silicon(FPGA/에뮬레이터) 성능 숫자의 함정 ★ (Manjiang이 FPGA/HAPS 전문)

- FPGA 프로토타입은 **수십 MHz** 로 돈다 → 초 단위 절대값은 무의미. **cycle 단위로 보고**.
- 더 큰 문제: **클럭 비율이 실리콘과 다르다**. 예: 실리콘은 NPU 1 GHz / DDR 6400 MT/s인데, FPGA에선 로직은 수십 MHz로 느려지고 DDR은 FPGA 보드 DDR이라 **상대적으로 메모리가 빨라 보임** → memory bound 레이어가 FPGA에선 compute bound처럼 보일 수 있음.
- 그래서: FPGA에선 **기능 검증 + 툴 자체 디버그 + cycle 단위 상대 비교**, 절대 성능 결론은 실리콘에서. "Tests and tools ready on day one" 메시지와 연결.
- 영어: "On FPGA I'd report in cycles and treat ratios carefully, because the memory-to-logic clock ratio is different from silicon, so the bottleneck can move."

## 8. 내가 만들 벤치마크 툴 구조 (설계 질문 대비)

| 계층 | 내용 |
|---|---|
| **on-target 라이브러리 (C, bare-metal)** | `timer_now()`(64-bit, hi-lo-hi) · `pmu_start/stop()` · HW 카운터 설정/읽기 API · 결과 구조체(이름, 조건, 샘플 배열) |
| **워크로드 모음** | memcpy/DMA bandwidth · pointer-chase latency · IRQ latency · NPU 레이어 마이크로벤치 · **트래픽 생성기**(여러 마스터 동시 부하) · 카메라 파이프라인 end-to-end |
| **파라미터** | 버퍼 크기, 반복 수, 동시성(outstanding), 클럭 설정, 캐시 on/off — **코드 수정 없이 설정으로 sweep** |
| **결과 전달** | 측정 구간 밖에서 UART / 공유 메모리 mailbox로 CSV/JSON (측정 중 printf 금지) |
| **host 오케스트레이터 (Python)** | 이미지 로드(JTAG/UART), 클럭·전압·온도 sweep, 결과 파싱, 그래프(scaling 곡선, 히스토그램, roofline), **이전 결과와 비교해 회귀 알람** |
| **신뢰성** | 이론값 sanity check, 알려진 지연을 넣어 측정이 맞는지 확인(**도구 자체 검증**), run-to-run 편차 임계값 |

- 영어 요약: "A small on-target C library for timers and hardware counters, a set of parameterized workloads from micro-benchmarks up to the full camera-to-NPU pipeline, and a Python host side that sweeps clocks, voltages and buffer sizes, stores results, and flags regressions against previous silicon or firmware."

## 9. Little's law — SSD와 SoC를 한 공식으로 ★

- `L = λ × W` → **동시에 진행 중인 요청 수 = throughput × latency**.

| 상황 | 계산 | 의미 |
|---|---|---|
| SSD 4K random read | QD 32, latency 200 µs → 32 / 200 µs = **160K IOPS** | IOPS를 올리려면 QD↑ 또는 latency↓ |
| SSD 천장 | 1M IOPS를 100 µs latency로 → 필요한 동시 요청 = **100** | die 병렬성이 100개 요청을 소화해야 |
| DDR 대역폭 | 51.2 GB/s × 100 ns = **5,120 B in flight = 64 B 라인 80개** | 마스터가 outstanding 트랜잭션 80개 이상을 낼 수 있어야 peak 도달 |
| DMA | outstanding 4개만 허용 × 64 B / 100 ns = **2.56 GB/s** | 엔진 설정(burst, outstanding) 때문에 대역폭이 안 나오는 전형적 원인 |

- 영어: "Little's law ties them together: in-flight requests equal throughput times latency. On SSDs that's queue depth; on an SoC it's outstanding AXI transactions — if a DMA engine can only keep four 64-byte reads in flight against 100-nanosecond DDR latency, it can't exceed about 2.5 GB/s no matter how fast the DDR is."

## 10. Don 경험 → 답변 연결 (영어 스크립트)

### Q. "Have you built performance benchmarks before?"

> "Yes — at Solidigm I worked with the silicon validation team on firmware performance tuning for PCIe/NVMe controllers, including performance-critical features for hyperscale customers. We defined each metric precisely — for example 4K random read IOPS at a given queue depth in steady state — measured throughput and tail latency at each stage of the data path, found the bottleneck was [ ], and got [ ]% back. The same discipline applies here; the device under test just moves from the whole drive to individual SoC blocks, measured on-chip with cycle counters and hardware counters."

- `[ ]` 두 칸은 [deck §4 S7](2026-10-25_rivian_hm_screen_prep.html)에서 채운 숫자를 쓸 것. 숫자가 기억 안 나면 "a double-digit percentage" 처럼 정직하게.

### Q. "Utilization of our NPU is only 40%. How do you debug it?"

> "First I'd check whether it's memory bound or compute bound: compute the arithmetic intensity of the layers and compare to the ridge point of peak TOPS over DDR bandwidth. Then I'd look at counters — DDR bandwidth near peak means memory bound, so tiling and data reuse; DDR low and MAC stalls high means scheduling or dependency stalls. A frequency sweep confirms it: if raising the NPU clock doesn't help, it's not compute. On SSDs I did the same thing deciding whether IOPS were limited by firmware CPU or by NAND die parallelism."

### Q. "How do you make your measurements trustworthy?"

> "I compare to the theoretical peak, subtract timer overhead, warm up, repeat and report percentiles, and cross-check software timing against hardware counters. I also validate the tool itself — insert a known delay and check I measure it. And I record the full configuration: silicon revision, clocks, voltage, temperature, build hash — otherwise you can't compare runs."

### Q. "What's different between SSD performance work and this?"

> "Three things. The observation point moves on-chip, so I rely on cycle counters and hardware performance monitors instead of host-side fio. Shared resources matter more — cameras, NPU and CPU all contend for the same DDR, so concurrency benchmarks matter. And the customer metric changes from statistical tail latency to determinism: in a car the worst case has to fit the frame deadline."

## 11. 숫자 치트시트 (외울 것만)

| 항목 | 값 |
|---|---|
| PCIe Gen4 / Gen5 x4 raw | ≈ 7.88 / 15.75 GB/s |
| LPDDR5-6400 × 64-bit | 51.2 GB/s |
| 128-bit AXI @ 1 GHz | 16 GB/s |
| 4096 MAC @ 1 GHz | 8.19 TOPS (MAC × 2 × f) |
| ridge point 예 | 8.19 TOPS / 51.2 GB/s ≈ 160 ops/B |
| 30 fps 프레임 예산 | 33.3 ms |
| 8 MP RAW12 @ 30 fps | 360 MB/s |
| DDR load latency | ~100 ns 대 (이 Mac 실측 random chase ≈ 55–62 ns) |
| 1 GHz 32-bit 카운터 wrap | ≈ 4.29 s |
| Little's law | in-flight = throughput × latency |
