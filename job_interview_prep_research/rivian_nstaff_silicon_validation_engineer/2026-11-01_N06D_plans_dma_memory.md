# N06D · 연습② 데이터 이동 · 메모리 — DMA · DDR · SRAM ECC · 캐시/인터커넥트 · IOMMU
<!-- theme: pastel -->
> **묶음**: 블록 연습② 데이터 이동·메모리 · **우선**: P0 ★ (D1 DMA, D2 DDR) / P1 (D3–D5) · **목록**: [N06 인덱스](2026-11-01_N06_validation_test_plan_design.html) · **이전/다음**: [N06C 연습① peripheral](2026-11-01_N06C_plans_peripherals_soc_infra.html) / [N06E 연습③ 고속 I/O](2026-11-01_N06E_plans_highspeed_io_sensor.html) · **관련**: [deck §6.2 DMA, §7 시나리오 1/3](2026-10-25_rivian_hm_screen_prep.html) · [BV09 DDR bring-up·training·memtest](2026-11-01_BV09_ddr_bringup_training_memtest.html) · [LP08 DMA·IOMMU·coherency](2026-10-31_LP08_dma_iommu_coherency.html) · [V3 DMA descriptor ring](hmScreenPrep/problems/V3_dma_desc_ring/index.html) · [V1 March memtest](hmScreenPrep/problems/V1_march_memtest/index.html) · [N05F memtest·무결성 C](2026-11-01_N05F_memtest_integrity.html) · [BV12 shmoo·PVT](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) · [성능 메트릭](2026-10-30_perf_metrics_ssd_vs_soc.html) · **코드**: [onsitePrep/n06/d/run.sh](onsitePrep/n06/d/run.sh) — [dma_matrix.c](onsitePrep/n06/d/dma_matrix.c) ([출력](onsitePrep/n06/d/dma_matrix.out)), [secded_inject.c](onsitePrep/n06/d/secded_inject.c) ([출력](onsitePrep/n06/d/secded_inject.out))

## 0. TL;DR · 사용법

- **이 페이지**: "Design a test plan for a **DMA engine** / **DDR controller** / on-chip SRAM ECC / interconnect / IOMMU" 를 10분 안에 말하기 위한 블록 5개. 블록마다 같은 틀: 🔰 → clarify → **매트릭스 한 장** → P0/P1/P2 → 장비 → 잘 터지는 버그 → 10분 영어 뼈대 → 꼬리 질문 → Don 연결.
- **★ 블록**: **D1 DMA**(deck §6.2를 25행 매트릭스로 확장) · **D2 DDR/LPDDR**(BV09의 내용을 **테스트 플랜 형태로 재배열** — 설명은 BV09에, 여기엔 "무엇을 · 어떤 조건에서 · 몇 번 · 합격 기준"만). D3–D5는 매트릭스만 훑어도 됨.
- **외울 것 3가지**
  1. **DMA = "완료"와 "정확"은 다르다** → 모든 테스트는 data compare + **앞뒤 guard 영역** + status/에러 레지스터 + SMMU/버스 에러 로그 0건까지 본다.
  2. **DDR = 성공률·마진·온도** → "training이 한 번 통과"가 아니라 **"1000번 cold boot × 3 온도에서 0 실패 + lane별 마진이 기준 이상 + 고온에서 refresh 배율이 실제로 바뀐다"**.
  3. **매트릭스를 믿는 이유 = 오류 주입** → 이 페이지 코드: DMA 모델에 버그 11개를 하나씩 넣었더니 매트릭스가 **11/11** 잡았고, **랜덤 soak 혼자서는 5/11**만 잡음 (§실습). ECC는 1비트 72,000건 정정 · 2비트 511,200건 전부 UE · **3비트는 75.9%가 오정정**(실측).
- **30초 영어 답 (headline: "Design a test plan for a DMA engine")**: *"I'd first ask what the engine supports — memcpy only or also peripheral handshakes, descriptor format, number of channels, whether it sits behind the SMMU and whether it's IO-coherent. Then I go bottom-up: registers and a single transfer, then a size-by-alignment sweep with guard bytes, 4 KB boundary crossings, descriptor chains and ring wrap, then all channels concurrently with priorities, error injection like decode errors and aborts, interrupt coalescing, and the cache-maintenance flow if it's non-coherent. On silicon I add bandwidth and latency against the theoretical number and data integrity soak across voltage and temperature. P0 for A0 is registers, single copy, and the sweep; done means every row passed on corner parts and every injected bug was caught."*
- **연습법**: 블록 하나 고르고 **10분 타이머** → §6 뼈대만 보고 영어로 소리 내 말하기(녹음) → 끝나면 §2 매트릭스와 비교해 **빠진 카테고리** 체크 → §7 `<details>` 열어 꼬리 질문 답 비교. 2일차 목표: D1 · D2 각 1회.

## 1. 공통 틀 복습

> 자세한 설명은 [N06A 템플릿](2026-11-01_N06A_methodology_test_plan_template.html). 여기선 한 장 요약만.

| 단계 | 시간 | 영어 첫 문장 템플릿 |
|---|---|---|
| ① clarify | 30초 | "Before I start, a few scoping questions: …" |
| ② 블록 그림 | 1분 | "Let me draw where it sits: bus, interrupts, memory, pins." |
| ③ 카테고리 매트릭스 | 3분 | "I organize the plan by category, bottom-up: registers, functional, …" |
| ④ 우선순위·플랫폼 | 1분 | "P0 for A0 bring-up is …; FPGA covers function, silicon adds timing, analog, PVT." |
| ⑤ 통과 기준·커버리지 | 1분 | "Each row has a measurable pass criterion, for example …" |
| ⑥ 자동화·회귀 | 1분 | "The same C test runs on FPGA, emulation, and silicon through a HAL, nightly." |
| ⑦ 리스크·A0 첫 주 | 1분 | "The biggest risks are …; on day one of A0 I would …" |

| # | 카테고리 | DMA/메모리에서 한 줄 예 |
|---|---|---|
| ① | 레지스터 | reset 값, RW/RO/W1C, 채널별 독립성 |
| ② | 기본 기능 | 1 descriptor memcpy, 1회 training |
| ③ | 설정 조합 | 크기 × 정렬 × 방향 × burst 설정, DDR 속도 bin × rank |
| ④ | 경계·corner | len 1 / max, 4 KiB 경계, ring wrap, 주소 맵 끝 |
| ⑤ | 인터럽트 | 완료 · 에러 · coalescing · mask/clear |
| ⑥ | 에러·negative | DECERR/SLVERR, abort, ECC 주입, SMMU fault |
| ⑦ | 리셋·전원 | 동작 중 채널 reset, self-refresh, suspend/resume |
| ⑧ | 동시성·stress | 다채널, 다 master 경합, CPU 동시 접근 |
| ⑨ | 성능 | GB/s 효율, latency p50/p99, QoS |
| ⑩ | PVT·margin | V/F/T 코너, VREF/delay shmoo |
| ⑪ | soak·신뢰성 | 수 시간 CRC soak, 1000회 boot |
| ⑫ | 시스템·interop | Linux dmatest / 실제 드라이버, 카메라→NPU 경로 |

## D1 · DMA engine (★ P0)

### 0. 🔰 이 블록이 뭔가

- **비유**: CPU는 사장, DMA는 **이삿짐 트럭 회사**. 사장이 "A 창고 상자 300개를 B 창고로" 라고 **작업지시서(descriptor)** 를 써서 게시판(ring)에 붙이고 "다 붙였다" 벨(doorbell)을 누르면, 트럭이 알아서 실어 나르고 끝나면 지시서에 "완료" 도장(status, OWN=0)을 찍고 전화(IRQ)를 한다. 사장은 그동안 다른 일을 한다.
- **정확한 정의**: 시스템 버스(AXI 등)의 **bus master** 로서, SW가 메모리에 써 둔 descriptor(src, dst, len, ctrl)를 읽어 메모리↔메모리 또는 메모리↔peripheral FIFO 사이 데이터를 **burst 단위로** 옮기는 엔진. 보통 여러 **채널**(독립 큐) + 채널 간 **arbiter**, scatter-gather(descriptor 체인), 완료/에러 **인터럽트**, peripheral **handshake**(DREQ/ACK) 를 가진다. 예: Arm PL330/DMA-350, Synopsys DW_axi_dmac, 자체 IP [추정 — Rivian이 어떤 IP를 쓰는지 모름].
- SoC 안에서 DMA는 **곳곳에** 있다: 범용 system DMA, peripheral 전용(SPI/UART DMA), PCIe/Ethernet 내장 DMA, **NPU/ISP 내부 DMA**(타일 load/store). 테스트 플랜의 틀은 같다.

```text
            APB/AXI-lite (CSR: 채널 enable, ring base/size, doorbell, status, IRQ mask)
  CPU ──────────────┐
  (cache)           ▼
              ┌──────────────────────────── DMA engine ───────────────────────────┐
              │ ch0 ┐                                                              │
              │ ch1 ├─► arbiter (prio/RR/aging) ─► desc fetch ─► burst splitter ─► │──► AXI master ──► SMMU ──► NoC/interconnect ──► DDR / SRAM
              │ ch2 │      ▲ peripheral DREQ/ACK          (4 KiB rule, max burst)  │        ▲ RRESP/BRESP (OKAY/SLVERR/DECERR)
              │ ch3 ┘      │                                                       │
              │      status write-back (OWN=0, DONE/ERR) ─► IRQ coalescer ─► GIC ──┼──► CPU
              └────────────┼───────────────────────────────────────────────────────┘
                     SPI/UART/I2S FIFO (mem↔dev 모드)
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| descriptor | — | 작업지시서: src, dst, len, ctrl, status | 형식·순서 버그가 가장 흔함 |
| ring / chain | — | 지시서를 원형 배열 또는 linked list로 연결 | wrap·full/empty 경계 버그 |
| OWN bit | ownership | 이 지시서가 지금 HW 것인지 SW 것인지 | 필드 먼저, OWN 나중 (barrier) — [V3](hmScreenPrep/problems/V3_dma_desc_ring/index.html) |
| scatter-gather | SG | 여러 조각을 한 번의 전송으로 모으기/뿌리기 | EOP(마지막 조각) 처리 |
| burst | — | 주소 한 번에 여러 beat 연속 전송 | AXI burst는 **4 KiB 경계를 넘으면 안 됨** [확인됨][3] |
| outstanding | — | 응답 안 받은 요청 수 | 대역폭·순서·hang에 영향 |
| coherent / non-coherent | — | DMA가 CPU 캐시를 snoop 하나 | non-coherent면 SW가 clean/invalidate — [LP08](2026-10-31_LP08_dma_iommu_coherency.html) |
| SLVERR / DECERR | slave / decode error | 대상이 에러 응답 / 주소에 아무 slave 없음 | 에러 경로 테스트의 핵심 |
| coalescing | interrupt coalescing | 완료 N개 또는 T 시간마다 IRQ 1번 | 꼬리 완료가 "영원히 안 알려짐" 버그 |
| doorbell | — | "새 일 있음" MMIO 쓰기 | doorbell 전 wmb 필요 |
| handshake | DREQ/ACK | peripheral이 "FIFO에 자리 있음" 신호 | mem↔dev 모드 underrun/overrun |

### 1. 먼저 물어볼 것 (clarify)

1. *"Is this our own IP or licensed — and is there a DV environment or C reference model I can reuse as the golden?"* → 가정: 라이선스 IP + 자체 wrapper, DV는 블록 레벨 UVM 완료, C 모델 없음 → memcmp가 golden.
2. *"Which modes are in scope: mem-to-mem only, or also peripheral handshake, 2D/strided, memset, CRC offload?"* → 가정: mem2mem + mem↔dev + SG. 2D는 P2.
3. *"Descriptor format and limits: ring or linked list, max length, number of channels, max outstanding, burst size?"* → 가정: 채널 8개, 8–256 slot ring, len 최대 16 MiB [예시].
4. *"Does it sit behind the SMMU, and is its port IO-coherent or do we do cache maintenance?"* → 가정: SMMU 뒤, non-coherent (최악 가정 — cache 행 필수).
5. *"What's the performance target — GB/s at what transfer size, and latency from doorbell to interrupt?"* → 가정: 4 KiB 이상에서 이론치의 85% 이상 [추정].
6. *"FPGA image available, and does the FPGA have real DDR or a slower memory model?"* → 가정: HAPS에 DDR 있음, 클럭 1/10 [추정].

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 레지스터 | reset 값 · RW/RO/W1C · 채널 간 aliasing | 모든 CSR, walking-1 쓰기 후 읽기, 다른 채널 값 불변 | 둘 다 | 스펙 reset 값 100% 일치, W1C는 1 쓰기로만 clear | P0 |
| 기본 기능 | single descriptor memcpy | 4 KiB, 정렬, 채널마다 | 둘 다 | 데이터 일치 + DONE + IRQ 1회 + 에러 0 | P0 |
| 설정 조합 | 크기 × src/dst 정렬 sweep | len {1,2,3,4,5,7,8,15,16,63,64,65,255,256,257,1023,1024,4095,4096,4097,8192, max} × src off {0,1,3,7} × dst off {0,1,2,3,63} | 둘 다 | 모든 case 데이터 일치 + **앞뒤 16 B guard 불변** | P0 |
| 설정 조합 | 방향 · burst 길이 · outstanding 설정 | mem2mem, mem2dev, dev2mem × burst {1,4,16} × outstanding {1, max} | 둘 다 | 데이터 일치, 설정별 성능 단조 증가 | P1 |
| 경계 | 4 KiB 경계 직전 시작 | 경계 −1, −16, −64, −256 B 시작, len 2~4097 | 둘 다 | 데이터 일치, AXI monitor 상 burst가 경계 미통과 | P0 |
| 경계 | 주소 맵 끝 · 최대 len · len 0 | DDR 마지막 페이지, len max, len 0 | 둘 다 | max 정상, len 0은 스펙대로(거부 또는 no-op + DONE) | P1 |
| 기능 | descriptor chain / scatter-gather | 조각 2~ring 크기, 조각 크기 랜덤 1~700 B, gather·scatter 둘 다 | 둘 다 | 연결 결과 = 조각 concat, EOP에서만 완료 1회 | P0 |
| 경계 | ring wrap · ring full | ring 크기 × 3 + 3 연속 제출, ring 꽉 채운 상태 4라운드 | 둘 다 | wrap 후 slot 0 처리, full에서 SW가 -EBUSY, 순서 보존 | P0 |
| 인터럽트 | 완료 IRQ · mask · clear · 누락 | 채널별 완료, mask 상태에서 pending 유지, W1C clear | 둘 다 | IRQ 수 = 완료 수, mask 해제 시 즉시 발생, spurious 0 | P0 |
| 인터럽트 | coalescing (개수 + 타이머) | threshold {1,4,16} × timeout {짧게, 길게}, 완료 수가 threshold 배수 아님 | 둘 다 | **마지막 꼬리 완료도 timeout 안에 알림**, signaled 합 = 완료 수 | P1 |
| 에러·negative | decode error / slave error | src 또는 dst가 맵 밖, 맵 끝 걸침, 보호 영역 | 둘 다 | status에 DECERR/SLVERR, dst 미변경(시작 전 에러), **다음 descriptor 정상 진행** 또는 스펙대로 halt | P0 |
| 에러·negative | abort / 잘못된 descriptor | 전송 중 채널 abort, 잘못된 ctrl 비트, OWN 안 된 descriptor | 둘 다 | abort 후 버스 idle(outstanding 0), 이후 재시작 정상, hang 0 | P1 |
| 에러·negative | SMMU fault 경로 | 매핑 안 된 IOVA, read-only에 write | Si (FPGA에 SMMU 있으면 둘 다) | SMMU event 1건, DMA status 에러, 다른 채널 영향 0 | P1 |
| 리셋·전원 | 동작 중 채널 reset · 전체 reset | descriptor fetch 직후 / 중간 / 마지막 burst에서 reset | 둘 다 | reset 후 **메모리 쓰기 0**, 재초기화 후 정상 전송 | P0 |
| 리셋·전원 | clock gating · suspend/resume | idle 시 gating, 전송 중 suspend 요청, resume 후 ring 상태 | Si | resume 후 남은 descriptor 정상 처리, 데이터 일치 | P1 |
| 동시성 | 전 채널 동시 + 우선순위 | 고우선 채널 포화 + 저우선 3채널 | 둘 다 | 저우선도 기한 내 완료(starvation 없음), wait 상한 이하 | P0 |
| 동시성 | RR 공정성 · 대역폭 분배 | 같은 부하 4채널 RR, 가중치 설정 | 둘 다 | 채널별 share 25% ± 2.5% 또는 설정 가중치 ± 10% | P1 |
| 동시성 | CPU · 다른 master와 경합 | CPU memcpy 부하, NPU/ISP 트래픽 동시 | Si | 데이터 일치, DMA 대역폭 하한 유지 | P1 |
| 캐시 | non-coherent cache maintenance 흐름 | CPU가 src를 cache에 쓰고 dst를 미리 읽은 상태, 크기 {64,100,4096} × dst off {0,8} | 둘 다 | clean/invalidate 넣으면 일치, 빼면 실패(**negative control**) | P0 |
| 캐시 | IO-coherent 모드 (있으면) | coherent 포트, CMO 없이 | Si | CMO 없이 일치, coherent 모드 대역폭 측정 | P1 |
| 성능 | throughput 효율 | len 64 B ~ 16 MiB, 1·N 채널, read/write 위치 DDR/SRAM | Si (FPGA는 상대 비교만) | 4 KiB 이상 이론치의 ≥ 85% [추정 목표], 크기별 곡선 리포트 | P1 |
| 성능 | latency | doorbell → 첫 write, doorbell → IRQ, p50/p99/max | Si | p99 < 목표 [추정 — 스펙 받기], max 이상치 원인 설명 | P1 |
| PVT·margin | 데이터 무결성 at 코너 | Vmin/Vmax × −40/25/125 °C × SS/TT/FF 칩, 최대 부하 | Si | CRC mismatch 0 in N시간 (예: 10^12 B 이동) | P2 |
| soak | 랜덤 soak | 랜덤 크기·정렬·체인·채널, seed 기록, 수 시간 | 둘 다 | mismatch 0, hang 0, seed로 재현 가능 | P1 |
| 시스템 | Linux dmaengine / 실제 드라이버 | `dmatest` (threads_per_chan, iterations, alignment), SPI/UART DMA 실제 사용 | Si | dmatest summary 0 failures, 실제 장치 전송 정상 | P1 |

### 3. 우선순위 · A0 첫 주

- **P0 bring-up (A0 첫 주)**
  1. CSR reset 값·RW — "엔진이 살아 있나" (10분이면 끝).
  2. single 4 KiB memcpy, 채널마다 — 데이터 + DONE + IRQ.
  3. **크기 × 정렬 sweep with guard** — 하루 안에 엔진 datapath 버그 대부분이 여기서 나온다 (모델 실측: 데이터 경로 버그 3개 모두 R01이 잡음).
  4. chain + ring wrap — 드라이버가 실제로 쓰는 모드.
  5. DECERR 1건 + cache maintenance negative control — "에러가 보고되나", "테스트가 실패할 수 있나".
- **P1**: 동시성/우선순위, coalescing, abort/reset, SMMU, 성능 곡선, dmatest, soak.
- **P2**: PVT 코너 무결성, 2D/strided 같은 부가 모드, 장기 신뢰성.
- **일부러 안 하는 것**: AXI 프로토콜 규칙 하나하나(handshake 타이밍, ID 순서 규칙) — **DV가 protocol checker/VIP로 덮음**. 우리는 AXI monitor로 "위반 0" 만 확인. 모든 레지스터 필드 조합의 exhaustive 탐색도 DV 몫. 실리콘에서는 **DV가 못 보는 것**: 실제 DDR 지연·경합, 실제 캐시·SMMU, 전원/클럭 도메인, PVT, 장시간.

### 4. 장비 · 셋업 · 관찰 지점

```text
 ┌──────────── test FW (bare-metal C, HAL) ─────────────┐
 │ pattern gen (PRBS/address-as-data, seed)             │      UART/공유메모리 → 결과 라인 "RESULT D1-R03 PASS seed=…"
 │ guard fill 0xA5 · CRC32 · timer(시작/끝 cycle)        │
 └──────┬───────────────────────────────────────────────┘
        ▼ CSR / doorbell
   DMA engine ──AXI──► [AXI monitor / ILA (FPGA) · trace (Si: CoreSight/NoC probe)] ──► SMMU ──► NoC ──► DDR
        │ IRQ                                           ▲ perf counters (NoC/DDR controller PMU)
        ▼                                                │
       GIC ── 인터럽트 카운트 · latency 타임스탬프 · GPIO 토글 → scope/LA (doorbell→IRQ 실측)
```

- **패턴**: address-as-data(틀린 위치가 곧 주소를 알려 줌), PRBS(seed 기록), 0x00/0xFF/0x55/0xAA, walking-1 — [LP08 §3.5](2026-10-31_LP08_dma_iommu_coherency.html) 표 재사용.
- **검사 3종**: ① dst == src (memcmp 또는 CRC32) ② **guard 영역** 앞뒤 16 B 이상 불변 (overrun/underrun) ③ **src 불변** (엔진이 src에 쓰는 버그 — Linux dmatest도 src를 다시 검사 [확인됨][2]).
- **레지스터/카운터**: 채널 status·에러 코드·현재 descriptor 포인터·남은 len, IRQ raw/masked status, SMMU event queue, NoC/DDR PMU (read/write 바이트, 대기 cycle).
- **Linux**: `modprobe dmatest timeout=2000 iterations=1000 channel=dma0chan0 run=1` → `dmatest: …: summary 1000 tests, 0 failures … iops … KB/s` [확인됨][1]; 파라미터 `threads_per_chan`, `test_buf_size`, `alignment`, `transfer_size`, `norandom`, `polled`, `noverify`(성능만) [확인됨][2]. dmatest 검증 방식 자체가 좋은 교본: src/dst 바이트마다 역할 비트(SRC/COPY/OVERWRITE)와 index를 넣어 **범위 밖 쓰기·src 덮어쓰기**를 분류 [확인됨][2].
- **FPGA vs Si**: FPGA는 ILA로 AXI 채널을 직접 보니 4 KiB·burst·outstanding 확인에 최적. 실리콘은 성능·PVT·장시간.

### 5. 잘 터지는 버그

| 증상 | 그럴듯한 원인 | 플랜에서 잡는 행 |
|---|---|---|
| 끝 1–3 바이트가 틀리거나 guard 1 B가 바뀜 | 꼬리(len % bus width) 처리, byte strobe 마스크 | 크기 × 정렬 sweep + guard |
| 4 KiB 근처 시작할 때만 데이터가 페이지 앞부분에 덮어써짐 | burst splitter가 4 KiB 분할 안 함, 주소 하위 12비트만 증가 | 4 KiB 경계 행 (모델 B2) |
| src/dst 상대 정렬이 다를 때만 마지막 바이트 누락 | realign buffer flush 누락 | 정렬 sweep (src off ≠ dst off) |
| ring을 한 바퀴 돈 직후 멈춤 | wrap 시 포인터 계산(off-by-one), ring size 레지스터 해석 | ring wrap 행 |
| 부하 높을 때 저우선 채널 timeout | 엄격 우선순위에 aging 없음 | 동시성/우선순위 행 |
| 잘못된 주소인데 "성공" 보고, dst는 쓰레기 | 에러 응답을 status에 반영 안 함 | DECERR 행 |
| 마지막 몇 개 완료가 인터럽트로 안 옴 (polling하면 보임) | coalescing timer 미동작 | coalescing 행 (threshold 배수 아닌 개수) |
| 데이터가 64 B 단위로 옛 값 | cache clean/invalidate 누락, 버퍼가 캐시 라인 공유 | cache 행 — deck §7 시나리오 1 |
| reset 후에도 메모리가 바뀜 | in-flight descriptor 상태가 reset으로 안 지워짐 | 동작 중 reset 행 |

### 6. 10분 영어 답 뼈대

1. *Clarify*: "Mem-to-mem and peripheral modes? Ring or linked list, channels, max length? Behind the SMMU, coherent or not? Performance target? FPGA image?"
2. *Picture*: "It's an AXI master: CSRs on APB, descriptors in DDR, arbiter across channels, burst splitter, status write-back, interrupts to the GIC."
3. "I build the plan bottom-up by category."
4. "Registers: reset values, access types, channel isolation."
5. "Functional: a single copy per channel, then the size-by-alignment sweep — odd sizes, 1 to max, src and dst offsets — always with guard bytes before and after, and I re-check the source."
6. "Corners: 4 KB boundary crossings, end of the address map, max length, ring wrap and ring-full."
7. "Scatter-gather chains: completion only at end-of-packet, data equals the concatenation."
8. "Interrupts: one per completion, masking, W1C clear, and coalescing — especially the leftover completions the timer has to flush."
9. "Errors: decode and slave errors, abort, bad descriptors, SMMU faults — the error must be reported and the engine must recover."
10. "Reset mid-transfer: no memory writes after reset; clean restart."
11. "Concurrency: all channels, high priority saturating, low priority must not starve; then CPU and NPU traffic at the same time."
12. "Cache: if it's non-coherent, the clean/invalidate flow, plus a negative control where I skip it and expect a failure."
13. "Performance on silicon: GB/s versus transfer size against the theoretical bus number, latency p50/p99."
14. "PVT and soak: random seeds for hours, CRC, at voltage and temperature corners on corner parts."
15. *Priority*: "P0 for A0 is registers, single copy, the sweep, chains, ring wrap. FPGA covers function, silicon adds performance and PVT; I skip AXI protocol rules because DV owns those."
16. *Done*: "Every row passed on SS/TT/FF parts, zero mismatches in the soak, and every injected bug in the model caught by at least one row."
17. *Automation*: "Same C tests through a HAL on FPGA and silicon, nightly, results with seed and chip ID."
18. *Risk*: "Biggest risks are coherency and 4 KB splitting — that's what I run first."

### 7. 꼬리 질문

**Q1. "A DMA test fails once in 10,000 runs with a few wrong bytes. How do you debug it?"**

<details><summary>답</summary>

First I characterize the mismatch: which offsets, how many bytes, what the wrong value looks like — old data, zeros, or data from another buffer. If the bad bytes cluster on 64-byte cache lines it smells like cache maintenance or a buffer sharing a line with another variable; if it's the last bytes it's tail handling; if it's at 4 KB it's burst splitting or the SMMU. Then I raise the failure rate with stress and correlate with alignment, channel, temperature and voltage. Finally I prove the fix statistically — with zero failures in about 30,000 runs I can say the rate is below 1 in 10,000 at 95% confidence.

한국어 핵심: **틀린 바이트의 위치 패턴**(64 B / 끝 / 4 KiB / 특정 비트)이 원인 계층을 알려 준다 → 빈도 올리기 → rule of three로 수정 증명.

</details>

**Q2. "Why guard bytes? Isn't comparing the destination enough?"**

<details><summary>답</summary>

Comparing the destination only proves the bytes I asked for are right; it says nothing about bytes the engine wrote that I didn't ask for. A one-byte overrun past the end corrupts whatever lives next to the buffer, and that shows up later as a random crash somewhere else, which is the worst kind of bug to debug. So I fill a guard region before and after with a known pattern and check it, and I also re-check the source, because an engine that writes the source is another silent corruption. Linux's dmatest does the same thing.

한국어 핵심: dst 비교 = "맞게 썼나", guard = "**안 써야 할 곳에 안 썼나**" — overrun은 나중에 엉뚱한 곳에서 터진다.

</details>

**Q3. "How do you know your test plan is good enough?"**

<details><summary>답</summary>

I test the test. In a model or on FPGA I inject known bugs — tail overrun, no 4 KB split, ring wrap off-by-one, error reported as OK, coalescing timer off — and check that at least one row fails for each. In my own model, the full matrix caught 11 out of 11, but random soak alone caught only 5, because control-path bugs like starvation, silent errors and reset handling need directed tests. That's also how I justify keeping directed tests next to random ones.

한국어 핵심: **오류 주입(mutation)으로 매트릭스 검증** — 실측 11/11 vs 랜덤만 5/11.

</details>

**Q4. "The engine reaches only 60% of the theoretical bandwidth. What do you look at?"**

<details><summary>답</summary>

First I make sure the theoretical number is right — bus width times clock, minus protocol overhead. Then I look at transfer size, because descriptor fetch and per-burst overhead dominate small transfers; I plot efficiency against size. Then outstanding transactions versus memory latency — if the engine allows only a few outstanding reads, latency limits bandwidth by Little's law. Then contention from other masters and DDR efficiency itself — page misses, refresh, read/write turnaround — using the NoC and DDR controller counters.

한국어 핵심: 이론치 재확인 → 크기별 곡선 → **outstanding × latency (Little's law)** → 경합·DDR 효율 카운터.

</details>

**Q5. "Coherent versus non-coherent DMA — what changes in your plan?"**

<details><summary>답</summary>

For non-coherent, software must clean the source before the DMA reads it and invalidate the destination before the CPU reads it, so I add a cache-flow row with a negative control: skip the clean or the invalidate and the test must fail, otherwise my test isn't actually exercising the cache. For IO-coherent ports I test without maintenance, check the snoop path under CPU load, and measure the bandwidth cost of snooping. And I check that the device tree or firmware describes the coherency correctly, because lying in either direction breaks something.

한국어 핵심: non-coherent = clean/invalidate 흐름 + **빼면 실패해야 하는 negative control**; coherent = snoop 경로 + 비용 측정.

</details>

### 8. Don 연결

- *"At Solidigm I verified DMA and SRAM/DRAM paths on the pre-silicon FPGA before RTL freeze — [숫자 채우기: 블록 수 / 발견 버그 수] — and the most productive tests were exactly the size-and-alignment sweeps with guard regions."*
- *"In SSD firmware the whole datapath is DMA — host DMA over PCIe and NAND DMA — so descriptor rings, completion ordering and cache maintenance were daily work for me."*

## D2 · DDR / LPDDR memory controller (★ P0)

> 설명(3층 구조, training 단계, MR4, ECC 종류, March C-)은 [BV09](2026-11-01_BV09_ddr_bringup_training_memtest.html)에 있다. 여기서는 **같은 지식을 "테스트 플랜" 모양으로 재배열**한다.

### 0. 🔰 이 블록이 뭔가

- **비유**: DDR은 **거대한 냉장 창고**인데 (1) 상자(셀)가 저절로 녹아서(전하 누설) 주기적으로 다시 얼려야 하고(**refresh**), 더우면 더 자주 얼려야 하며(**온도 보정**), (2) 창고로 가는 도로(배선)의 길이가 차선마다 달라서 처음 문을 열 때 "트럭이 언제 도착하는지" 차선별로 재야 한다(**training**). 메모리 컨트롤러는 창고 관리자 — 주문(read/write)을 효율 좋게 순서 바꾸고, 냉장(refresh)·검수(ECC)·여러 손님(CPU, NPU, ISP) 줄 세우기(QoS)를 한다.
- **정확한 정의**: SoC 쪽 **memory controller**(명령 스케줄링, 주소 매핑, refresh, ECC, QoS, 저전력 상태) + **PHY**(IO, delay line, VREF, training 엔진·펌웨어) + DRAM 장치(LPDDR4x/5/5X). 자동차 AI SoC는 대역폭·전력 때문에 LPDDR5/5X가 주력이고 RAP1은 MCM 패키지 내 메모리일 가능성 [추정 — BV09 §2.1].

```text
 CPU · NPU · ISP · GPU · DMA ──► NoC (QoS 우선순위, 대역폭 regulator)
                                   │
                    ┌──────────────▼───────────────────┐
                    │ memory controller                 │  CSR: timing(tRCD,tRP,tRFC,tREFI…), 주소 맵, ECC, refresh, QoS
                    │  scheduler · page policy · refresh│  counters: CE/UE, read/write bytes, page hit, refresh 수
                    │  ECC (inline 또는 sideband) · MR4  │  IRQ: ECC UE/CE, 온도 경고 ──► GIC / safety island
                    └──────────────┬────────────────────┘
                                   │ DFI
                    ┌──────────────▼────────────────────┐
                    │ DDR PHY + training FW (벤더)       │  training 결과: lane별 delay·VREF·eye 폭
                    └──────────────┬────────────────────┘
                     package / PCB │ CA · CK · DQ/DQS(WCK) · ZQ
                              LPDDR5/5X 장치 (MR4 = 온도·refresh 배율)
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| training | — | 차선별 지연·VREF 측정해서 맞추기 | 성공률·마진이 P0 지표 |
| eye / margin | — | 데이터가 맞게 읽히는 시간·전압 창 넓이 | PVT에서 줄어듦 → shmoo |
| tREFI | refresh interval | refresh 명령 간격 | LPDDR4 기본 3.9 µs (32 ms/8K) [확인됨 — BV09 출처] |
| MR4 | mode register 4 | DRAM이 알려 주는 "지금 필요한 refresh 배율" | 85 °C 넘으면 2×/4× refresh 필요 |
| self-refresh | SR | 컨트롤러가 쉬는 동안 DRAM이 스스로 refresh | suspend/저전력 진입·탈출 |
| DFS / FSP | dynamic freq scaling / freq set point | 주파수 바꾸기 / 주파수별 MR 세트 저장(LPDDR5 3개) | 전환 중 트래픽·retraining |
| inline ECC | — | 여분 칩 없이 같은 DRAM 일부에 ECC 저장 | LPDDR + 자동차 안전에서 흔함 [추정 — BV09] |
| QoS | quality of service | 누가 먼저 쓰나 우선순위·대역폭 보장 | ISP 실시간 데드라인 |
| bandwidth efficiency | — | 실측 GB/s ÷ 이론 GB/s | page miss·refresh·turnaround 손실 |

### 1. 먼저 물어볼 것 (clarify)

1. *"Which DRAM: LPDDR5 or 5X, speed bins, channels and ranks — and is it on-package?"* → 가정: LPDDR5X on-package, 4 × x16 채널 [추정].
2. *"Who owns training firmware — the PHY vendor's binary, or do we write the init sequence? Can I read per-lane training results?"* → 가정: 벤더 FW, 결과 레지스터/로그 노출됨.
3. *"Is ECC enabled — inline or sideband — and does the controller have error injection and an error address log?"* → 가정: inline ECC + poison 레지스터 + CE/UE 카운터.
4. *"What's the temperature range — AEC-Q100 grade 2 to 105 or grade 1 to 125 °C ambient — and does the controller act on MR4 automatically?"* → 가정: Grade 2, 자동 derating + SW polling 백업.
5. *"Which frequency points do we use in DVFS, and which masters have real-time deadlines — ISP, display?"* → 가정: 3 FSP, ISP 실시간.
6. *"Is there an emulator or FPGA DDR model, or is training only testable on silicon?"* → 가정: training·마진은 **실리콘 전용**, 컨트롤러 기능(주소 맵·ECC 로직·QoS)은 FPGA/에뮬에서도.

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 레지스터 | 컨트롤러·PHY CSR reset 값, timing 레지스터 프로그래밍 확인 | 모든 timing 값 vs DRAM 데이터시트 표 (tRCD, tRP, tRFC, tREFI, CL…) | 둘 다 | 쓰고 읽은 값 = 설정 표, 계산식 리뷰 서명 | P0 |
| 기본 기능 | init + training 1회, 결과 로그 | 대표 속도, 상온 | Si | training 완료 비트, 모든 단계 PASS, lane별 결과 기록 | P0 |
| 기본 기능 | data bus → address bus → March C- (캐시 off) | 각 채널·rank 전체 용량 | Si (FPGA는 컨트롤러 경로만) | 0 오류, 순서 지키기 — [BV09 §2.6](2026-11-01_BV09_ddr_bringup_training_memtest.html) · [V1](hmScreenPrep/problems/V1_march_memtest/index.html) | P0 |
| 신뢰성 | **training 성공률 · 결과 분포** | **cold boot 1000회 × {−40, 25, 105 °C}**, warm reset 1000회 | Si | 실패 0/3000 (→ 95% 상한 ≈ 0.1%), lane별 delay 분포가 기준 폭 이내, 이상치 0 | P0 |
| 설정 조합 | 속도 bin · rank · 채널 interleave · 주소 맵 | 모든 지원 속도 × 1/2 rank × interleave on/off | 둘 다 | 모든 조합 March 0 오류, 주소 aliasing 0 (address-as-data) | P1 |
| 기능 | 패턴 스트레스 | moving inversions, random, bit-fade(지연 후 확인), **row hammer 패턴**, 동시 SSN 패턴(모든 DQ 동시 토글) | Si | 0 오류; hammer는 스펙/완화 동작 확인 [추정 — 장치·컨트롤러 기능 확인 필요] | P1 |
| refresh | tREFI 실제 간격 | PMU refresh 카운터 또는 프로브로 refresh 명령 빈도 측정 | Si | 설정값 ± 허용 오차, 온도 상태별 배율 반영 | P1 |
| refresh·온도 | **MR4 기반 refresh 배율 · derating** | 챔버 램프 25 → 105 °C (85 °C 통과), **85 °C 이상에서 부팅**(cold start at hot) | Si | MR4 값 변화 → refresh 2×/4× 적용, 고온 부팅에서도 derating 동작 (i.MX8MM ERR050805 류 회귀) [확인됨][4] | P0 |
| 리셋·전원 | self-refresh 진입/탈출 · retention | suspend/resume 1000회, 진입 전 패턴 → 탈출 후 확인 | Si | 데이터 100% 보존, 탈출 latency < 목표 | P0 |
| 리셋·전원 | power-down 모드 · clock stop · warm reset 시 메모리 보존(스펙 지원 시) | idle 타이머 설정별 | Si | 0 오류, 저전력 상태 진입 확인(카운터/전류) | P1 |
| DFS | 주파수 전환 under traffic | FSP 0↔1↔2 전환 10,000회, DMA·CPU 트래픽 진행 중 | Si | 데이터 오류 0, 전환 시간 < 목표, 실시간 master underflow 0 | P0 |
| 온도 보정 | 주기적 ZQ cal · retraining / VT tracking | 온도 램프 중 장시간 트래픽 | Si | 램프 내내 0 오류, retraining 이벤트 로그 | P1 |
| ECC | CE/UE 주입 · 정정 · 검출 | poison 레지스터로 lane·bit 위치별 1비트·2비트, 읽기 경로·write-back | 둘 다 (로직은 FPGA, 실제 DRAM은 Si) | 1비트 100% 정정 + CE 카운터 +1 + 주소 로그, 2비트 100% UE + IRQ, 오정정 0 | P0 |
| ECC | ECC init · scrub · 보고 경로 | 부팅 시 전체 초기화 시간, patrol scrub rate, Linux EDAC `ce_count`/`ue_count` | Si | 초기화 전 읽기 UE 없음, scrub이 주입 CE를 찾아 카운트, EDAC 값 = 레지스터 | P1 |
| ECC | inline ECC 오버헤드 | ECC on/off 대역폭·용량 | Si | 손실 % 리포트 (목표 [추정]) | P2 |
| 성능 | bandwidth 효율 | read/write/copy, 순차·랜덤, 1~N master, 채널 수 | Si | 이론치 대비 효율 리포트, 순차 read ≥ 목표 % [추정] — 이론치 예: LPDDR5 6400 MT/s × 16 bit = **12.8 GB/s per x16 채널** (계산) | P1 |
| 성능 | latency | idle pointer-chase, 부하 중 latency 분포 | Si | p50/p99 vs 목표, 부하 곡선(latency vs bandwidth) | P1 |
| 동시성·QoS | 다 master 경합 | NPU 최대 부하 + ISP 실시간 + CPU + DMA 동시 | Si | ISP/디스플레이 **underflow 0**, 우선순위 설정대로 대역폭 분배, CPU latency 상한 | P0 |
| margin | shmoo: VREF × delay, 전압 × 주파수 | lane별 read/write eye sweep, VDDQ ± %, 속도 +1 bin | Si | 모든 lane 마진 ≥ 기준 (예: eye 폭 ≥ X UI [추정]), 보드·칩 간 분포 — [BV12](2026-11-01_BV12_si_pi_margining_shmoo_pvt.html) | P1 |
| PVT | 코너 칩 × 전압 × 온도 전체 | SS/TT/FF × Vmin/Vnom/Vmax × −40/25/105(125) °C | Si | 모든 코너 training 성공 + March 0 오류 + stress N시간 0 오류 | P2 |
| soak | 장시간 시스템 stress | `stressapptest` 류 수 시간~수 일, 전 master 동시 | Si | 0 오류, CE 추세 0 또는 설명 가능 | P1 |
| 시스템 | Linux 부팅 · memtester · 실제 워크로드 | 부팅 1000회, NPU 추론 루프 | Si | 커널 panic/EDAC UE 0, 추론 결과 golden 일치 | P1 |
| 에러·negative | 범위 밖 주소 · 보호 영역 접근 | 맵 밖 주소, 보안 영역(TZ) 접근 | 둘 다 | 버스 에러 응답, 데이터 미변경, 로그 | P1 |

### 3. 우선순위 · A0 첫 주

- **P0 bring-up**
  1. 전원·클럭·리셋 확인 후 **training 1회 성공 + 로그 읽기** (실패 시 [BV09 §3.3](2026-11-01_BV09_ddr_bringup_training_memtest.html) 표로 단계 → 신호 그룹 국소화).
  2. **data bus → address bus → March C-** (캐시 off) — 순서 바꾸면 오진.
  3. 낮은 속도에서 먼저 통과 → 목표 속도로 올림 (마진 문제인지 hard 실패인지 분리).
  4. ECC 주입 1비트·2비트 (안전 메커니즘이 살아 있나).
  5. **cold boot 루프 시작** — 첫 주부터 밤마다 1000회 돌려 성공률 데이터를 쌓는다.
- **P1**: 온도 챔버(MR4/derating, 고온 부팅), self-refresh·DFS, QoS 경합, 성능 효율, shmoo.
- **P2**: 전체 PVT 코너 매트릭스, 장기 soak, inline ECC 오버헤드 특성화.
- **일부러 안 하는 것**: DRAM 셀 자체의 결함 스크리닝(DRAM 벤더가 함), 컨트롤러 scheduler 알고리즘의 모든 상태(DV), JEDEC 명령 타이밍 하나하나의 프로토콜 준수(DV + 벤더 PHY 검증). 우리는 **"이 칩 + 이 패키지 + 이 펌웨어 설정"이 PVT 전체에서 마진을 갖는지**에 집중.

### 4. 장비 · 셋업 · 관찰 지점

```text
 thermal chamber / thermal head (−40 ~ 125 °C) ──┐
                                                ▼
 programmable PSU (VDD2/VDDQ ± %) ──► [ DUT 보드: SoC + LPDDR5X ] ──UART──► 결과 로그 (chip ID · 온도 · V · FW hash · training dump)
                                                │ JTAG (training 레지스터 덤프, 실패 시 멈춤)
                                                │ PMU/counters: refresh 수, page hit, BW, CE/UE
 boot-loop 컨트롤러(전원 relay) ────────────────┘   (선택) DDR interposer + LA / 고대역 scope — 패키지 내 메모리면 불가 [추정]
```

- **training 결과 덤프**: lane별 write leveling delay, read/write eye 중심·폭, VREF — 매 부팅 CSV로 남겨 **분포·추세**를 본다 (온도별, 칩별).
- **카운터**: 컨트롤러 ECC_ERRCNT/CE 주소/syndrome, refresh 카운터, 대역폭 PMU — Linux에서는 EDAC sysfs `mc*/ce_count`, `ue_count`, `sdram_scrub_rate` [확인됨 — BV09 출처 14].
- **도구**: U-Boot `mtest`, memtester, `stressapptest`(랜덤 대량 메모리 트래픽 + CRC 검증, SI·타이밍·컨트롤러 버그에 강하고 셀 결함엔 중간 정도라고 스스로 밝힘) [확인됨][7], STREAM 류 대역폭 벤치마크, pointer-chase latency 측정 — 결과 집계는 [V4](hmScreenPrep/problems/V4_perf_bench_stats/index.html).
- **패키지 내 메모리면** 프로브를 못 댄다 → 관찰은 **PHY 내부 eye 스캔·training 레지스터·카운터** 위주 [추정].

### 5. 잘 터지는 버그

| 증상 | 그럴듯한 원인 | 플랜에서 잡는 행 |
|---|---|---|
| 1000번 중 3번 training 실패 | 마진 경계 lane, 초기 delay 시드값, PLL lock 대기 부족 | **training 성공률 루프** + 결과 분포 |
| 고온에서만 메모리 오류 | refresh 배율 미적용, VREF/임피던스 drift, 마진 부족 | MR4/derating 챔버 행, shmoo at hot (deck §7 시나리오 3) |
| 85 °C 이상에서 **부팅**하면 그 뒤 계속 위험 | 컨트롤러가 온도 갱신 플래그를 못 받아 derating 안 함 (NXP ERR050805 사례: 고정 2× refresh로 우회) [확인됨][4] | "고온 부팅" 행 — 램프 업만 테스트하면 놓침 |
| suspend/resume 수백 회 중 한 번 데이터 깨짐 | self-refresh 탈출 타이밍, 탈출 후 retraining 누락 | self-refresh 1000회 행 |
| DFS 전환 순간 ISP 프레임 깨짐 | 전환 중 트래픽 정지 시간 > ISP 버퍼 여유 | DFS under traffic + QoS 행 |
| training 통과, Linux가 부하에서 랜덤 crash | 마진 부족(특정 패턴), 주소 맵/interleave 설정 오류, ECC off | 패턴 스트레스, address-as-data, stressapptest |
| ECC CE 카운터가 특정 lane에만 쌓임 | 해당 바이트 lane 마진(SI)·solder | CE 주소/syndrome 로그 → lane 매핑 분석 |
| 대역폭이 기대의 60% | page policy, 주소 매핑이 bank 병렬성 못 씀, refresh 과다, QoS 설정 | 효율 행 + PMU page hit 률 |

### 6. 10분 영어 답 뼈대

1. *Clarify*: "LPDDR5 or 5X, speed bins, channels, on-package? Who owns training firmware? ECC inline with injection? Temperature grade? DVFS points? Which masters are real-time?"
2. *Picture*: "Masters go through the NoC with QoS into the controller — scheduler, refresh, ECC — then DFI to the PHY with its training engine, then the DRAM, which reports temperature through MR4."
3. "Bring-up first: registers and timing values reviewed against the datasheet, one successful training with the per-lane results logged, then data bus, address bus, March C- with caches off — in that order."
4. "Then the most important statistic: training success rate. A thousand cold boots at cold, room and hot, zero failures, and I keep the per-lane delays from every boot to watch the distribution, not just pass/fail."
5. "Functional: all speed bins, ranks, interleave settings; pattern stress — moving inversions, random, bit-fade, simultaneous-switching patterns, and row-hammer behavior."
6. "Refresh and temperature: measure the actual refresh rate, ramp the chamber through 85 °C and check the controller applies the MR4 multiplier — and also boot already hot, because that's a known erratum class."
7. "Power: self-refresh entry and exit a thousand times with data retention; frequency switching between set points under live traffic."
8. "ECC: inject single and double-bit errors by lane and bit through the poison registers — every single corrected and counted with the address logged, every double detected with an interrupt, zero miscorrections; scrub and the EDAC reporting path."
9. "Performance: bandwidth efficiency against the theoretical number — for LPDDR5 at 6400, 12.8 GB/s per x16 channel — and latency under load."
10. "Multi-master QoS: NPU at full load plus ISP plus CPU; the real-time masters must never underflow."
11. "Margin: VREF and delay shmoo per lane, voltage and frequency margins, across boards and corner chips."
12. *Platform*: "FPGA and emulation cover controller logic — address map, ECC logic, QoS; training and margin are silicon only."
13. *Done*: "Zero training failures across the boot loops at all temperatures, margins above the agreed threshold on every lane for SS/TT/FF parts, ECC injection 100%, soak clean."
14. *Risk*: "Hot temperature and marginal lanes are the risk, so the boot loop and the chamber start in week one."

### 7. 꼬리 질문

**Q1. "Training passes. Why isn't that enough?"**

<details><summary>답</summary>

Passing once tells me there is an eye; it doesn't tell me how wide it is or whether it's there every boot, at every temperature, on every part. So I look at the per-lane results from many boots — if one lane's delay is bimodal or its eye is half the width of the others, that lane will fail in the field. And I margin it: sweep VREF and delay, lower the voltage, go up one speed bin. A thousand clean boots at three temperatures gives me a failure-rate bound around 0.1 percent; production needs much better, so margin data is what really gives confidence.

한국어 핵심: 한 번 통과 = eye가 "있다"일 뿐 → **분포·마진·성공률**이 증거.

</details>

**Q2. "How do you validate temperature-compensated refresh?"**

<details><summary>답</summary>

The DRAM reports through MR4 what refresh rate it needs; above about 85 °C the interval has to shrink, for LPDDR4 to half or a quarter. I put the board in a chamber and ramp through that point while running traffic, read MR4 and the controller's refresh counter, and check the rate actually changes and changes back. I also boot at hot, because there's a real erratum where the controller missed the update flag if the memory started above 85 °C. Optionally I characterize retention by stretching the refresh interval on a lab part to see how much margin there is.

한국어 핵심: 챔버 램프 + MR4 값 + **refresh 카운터 실측**, 그리고 **고온에서 부팅** 케이스 (ERR050805).

</details>

**Q3. "How do you validate ECC if you can't make DRAM cells fail on demand?"**

<details><summary>답</summary>

I use the controller's poison or injection registers, or I write data with ECC disabled and read it back with ECC enabled so the check bits mismatch. I sweep every bit position, both data and check bits, for single and double errors, and verify correction, the counters, the error address log, and the interrupt to the safety manager. I also check what happens beyond the guarantee: in my own SEC-DED model, triple-bit errors were silently miscorrected about three quarters of the time, which is why scrubbing and reporting matter. Finally the full reporting path up to Linux EDAC.

한국어 핵심: 주입은 poison 레지스터 또는 ECC off 쓰기 → on 읽기; **모든 비트 위치 × 1·2비트**, 카운터·로그·IRQ까지; 3비트는 오정정(실측 75.9%).

</details>

**Q4. "The ISP drops frames when the NPU runs. How would your plan have caught it?"**

<details><summary>답</summary>

That's the multi-master QoS row: run the NPU at maximum memory bandwidth while the ISP streams at its real-time rate, and monitor the ISP's buffer underflow counter and frame counter. If it fails, I look at QoS priorities and bandwidth regulators on the NoC, the controller's scheduling of real-time traffic, and whether refresh or a frequency switch is creating a stall longer than the ISP buffer can absorb. The pass criterion is zero underflows over hours, at the lowest DDR frequency point we use, because that's the worst case.

한국어 핵심: 동시 부하 + ISP underflow 카운터, **가장 낮은 DDR 주파수 점이 최악**.

</details>

### 8. Don 연결

- *"At SK hynix I spent seven years on the memory side — chamber-based reliability platforms and interface margin shmoo — so a DDR plan built around boot loops, chamber ramps and margin distributions is how I naturally think. [숫자 채우기: chamber 수 / 동시 DUT 수]"*
- *"At Solidigm I verified the SoC's DRAM path on FPGA before RTL freeze, which is the controller-logic part of this plan; training and margin I'd bring from the NAND interface work at SK hynix."*

## D3 · On-chip SRAM · ECC · parity (P1)

### 0. 🔰 이 블록이 뭔가

- **비유**: 사무실 책상 서랍(SRAM) — 빠르고 가깝지만 우주선(입자)이 가끔 종이 한 장을 뒤집는다(**soft error**). 그래서 중요한 서랍엔 **검수 스티커(ECC)** 를 붙이고, 덜 중요한 건 "바뀌었는지만 아는" 스티커(**parity**)를 붙인다.
- **정확한 정의**: SoC 내부 SRAM(NPU 버퍼, TCM, 캐시 data/tag, FIFO, shared SRAM)과 그 보호 로직. 보통 64 bit + 8 check(SEC-DED) 또는 32+7, 캐시 tag/작은 FIFO는 parity. **MBIST**(memory built-in self-test, 하드웨어 March 엔진)가 제조 시험·부팅 시 셀 결함을 보고, 런타임 soft error는 ECC가 처리. 자동차에선 ISO 26262 안전 메커니즘으로 진단 커버리지가 요구됨 [추정 — N06F F4].

```text
  master (CPU/NPU) ─► SRAM wrapper ─┬─► ECC encode ─► SRAM array ─► ECC decode ─┬─► data
                                     │       ▲ inject(poison) 레지스터              ├─► CE/UE 카운터 · 주소 로그 ─► IRQ ─► safety manager
                                     │       MBIST 엔진 (boot/ATE) ◄── 결과 레지스터  └─► scrub 엔진 (백그라운드 읽기·고쳐 쓰기)
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| SEC-DED | single error correct, double error detect | 1비트 고침, 2비트 알아챔 | 3비트 이상은 보장 없음 (오정정 가능) |
| parity | — | 1비트 오류 검출만 | 검출 후 동작(재읽기/무효화)이 테스트 대상 |
| MBIST | memory BIST | HW March 테스트 엔진 | 제조·부팅 결함 검출, SW 테스트로 대체 불가(속도·접근) |
| scrub | — | 백그라운드로 읽어 CE 고쳐 쓰기 | 1비트가 쌓여 2비트가 되기 전 제거 |
| poison / inject | — | 일부러 틀린 check bit 쓰기 | ECC 검증의 유일한 현실적 방법 |

### 1. 먼저 물어볼 것 (clarify)

1. *"Which SRAMs have ECC versus parity versus nothing, and what's the word width?"* → 가정: NPU·shared SRAM은 SEC-DED 64+8, 캐시 tag는 parity.
2. *"Is there an injection mechanism per instance, and where do errors report — interrupt to CPU, or to a safety island?"* → 가정: 인스턴스별 inject 레지스터, UE는 safety island로.
3. *"Is MBIST run at boot, and who owns its patterns — DFT team?"* → 가정: DFT 소유, 부팅 시 선택 실행, 결과 레지스터 노출.
4. *"Is there hardware scrubbing, and what's the read-modify-write behavior for partial writes?"* → 가정: 있음, 부분 쓰기는 RMW.
5. *"Do any SRAMs keep contents in retention power states?"* → 가정: 일부 retention.

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 레지스터 | ECC 제어·카운터·로그 레지스터 | reset 값, W1C clear, 카운터 포화 동작 | 둘 다 | 스펙 일치, 포화 시 wrap 안 함 | P0 |
| 기본 기능 | SW March C- / address-as-data (ECC on) | 인스턴스별 전체 주소 | 둘 다 | 0 오류, CE 0 | P0 |
| 에러 주입 | 1비트: 모든 비트 위치 (data + check) | 인스턴스별, 여러 주소 | 둘 다 | 100% 정정, 데이터 정확, CE +1, 주소 로그 정확 | P0 |
| 에러 주입 | 2비트: 모든 쌍 (또는 대표 쌍) | 인스턴스별 | 둘 다 | 100% UE, IRQ, 오정정 0, 데이터 소비 차단(poison 전파) | P0 |
| 에러 주입 | 3비트 이상 (특성화) | 랜덤 | 둘 다 | 결과 분포 리포트 (오정정 가능함을 문서화) | P2 |
| parity | parity 오류 → 동작 | 캐시 tag parity 주입 | 둘 다 | 검출 + 무효화/재fetch 또는 스펙대로 보고 | P1 |
| 인터럽트 | CE 임계·UE 즉시 보고 | CE threshold 설정, mask | 둘 다 | 설정대로 IRQ, safety 경로 latency < 목표 | P1 |
| 경계 | 부분 쓰기 RMW | byte write 후 ECC, RMW 중 CE 존재 | 둘 다 | 데이터 정확, RMW가 오류를 숨기지 않음 | P1 |
| 기능 | scrub | 주입 후 scrub 1 pass, scrub rate 설정 | 둘 다 | 주입한 CE 전부 발견·수정, 카운터 일치 | P1 |
| 리셋·전원 | ECC init · retention | 부팅 init 시간, retention 진입/탈출 | Si | init 전 읽기 UE 없음, retention 후 데이터·ECC 정상 | P1 |
| MBIST | 부팅 MBIST 결과 · 시간 | 모든 인스턴스, PVT 코너 | Si | 양품 칩 PASS, 시간 < 부팅 예산 | P1 |
| 동시성 | 다 master 동시 접근 + 오류 주입 | NPU + DMA 동시, 한쪽에 주입 | 둘 다 | 오류가 올바른 master/주소에 귀속 | P2 |
| PVT | Vmin 특성화 | SRAM Vmin shmoo, 온도 | Si | Vmin margin ≥ 기준 [추정] | P2 |

### 3. 우선순위 · A0 첫 주

- **P0**: SW March 1회, 1비트·2비트 주입 한 인스턴스씩, 카운터·로그 확인. → 안전 메커니즘이 "살아 있다"는 증거가 가장 먼저 필요.
- **P1**: 전 인스턴스 주입 sweep(자동화), scrub, parity 경로, MBIST 결과 통합. **P2**: Vmin, 3비트 특성화.
- **안 하는 것**: 셀 결함 스크리닝(MBIST/ATE 몫), ECC 인코더 논리의 exhaustive 증명(DV/formal 몫). 우리는 **주입 → 보고 → 시스템 반응**의 end-to-end 경로.

### 4. 장비 · 셋업 · 관찰 지점

- inject 레지스터 + CE/UE 카운터 + 주소/syndrome 로그 + IRQ 타임스탬프. 실리콘 soft error 실측이 필요하면 **가속 시험(중성자/알파 빔)** — 보통 신뢰성팀 몫 [추정].
- 이 페이지 코드 [secded_inject.c](onsitePrep/n06/d/secded_inject.c)가 그대로 "주입 sweep + 카운터 검증 + scrub 효과" 테스트의 모델 (§실습).

### 5. 잘 터지는 버그

- **check bit 쪽 1비트 오류**를 data 오류로 잘못 정정해 데이터를 깨뜨림 → 주입 sweep에 **check bit 위치 포함**해야 잡힘.
- **주소 로그가 마지막 오류로 덮어써짐**(스펙은 첫 오류 유지) → 연속 2회 주입 테스트.
- **부분 쓰기 RMW가 기존 CE를 고치지 않고 새 check bit로 덮어씀** → 오류가 영구화(나중엔 "정상" 데이터로 보임).
- **UE 데이터가 그대로 소비됨**(poison 전파 안 됨) → NPU가 쓰레기 값으로 추론 계속.
- **카운터 W1C/포화 버그** → 안전 모니터가 오류율을 못 셈.
- **ECC init 누락 영역** → 부팅 직후 첫 읽기에서 UE 폭주.

### 6. 10분 영어 답 뼈대

1. "Clarify which SRAMs have ECC or parity, injection support, where errors report, MBIST ownership."
2. "Picture: encoder, array, decoder, counters and address log, interrupt to the safety manager, scrub engine."
3. "Functional: software March with ECC on, zero errors and zero correctables."
4. "Injection: every bit position including check bits, single errors corrected and counted with the right address, double errors detected with an interrupt and never consumed."
5. "Beyond the guarantee: characterize triple errors so the safety analysis knows miscorrection is possible."
6. "Partial writes and read-modify-write, scrubbing, ECC initialization at boot, retention states."
7. "MBIST: results and boot-time budget at PVT corners; Vmin characterization."
8. "Done: injection 100% on every instance, reporting latency within the safety budget."

### 7. 꼬리 질문

**Q1. "MBIST already tests the SRAM. Why do you need software tests?"**

<details><summary>답</summary>

MBIST tests the array for manufacturing defects with hardware patterns, but it bypasses the functional path — the ECC logic as used by real masters, the interconnect, the error reporting, the interrupt routing to the safety manager. My tests check that the whole mechanism works end to end in mission mode. MBIST results are an input to my plan, not a replacement.

한국어 핵심: MBIST = 셀 결함, SW = **기능 경로 + 보고 경로 end-to-end**.

</details>

**Q2. "Why does scrubbing matter if ECC corrects on read?"**

<details><summary>답</summary>

ECC corrects on read only for the words that are read; data that sits untouched accumulates upsets, and two in the same word becomes uncorrectable. Scrubbing reads everything periodically and writes back the corrected value. In my model with one random upset per epoch over 4,096 words, no scrubbing gave 757 uncorrectable words, scrubbing every 100 epochs gave 43, and every 5 epochs gave 4 — the scrub interval is a real design parameter we should validate.

한국어 핵심: 안 읽는 데이터에 1비트가 쌓여 2비트 → **scrub 주기 = 검증할 파라미터** (모델 실측 757 → 43 → 4).

</details>

### 8. Don 연결

- *"I verified SRAM paths with ECC on the Solidigm FPGA before RTL freeze, and in SSD firmware ECC counters and error logs were the primary field-debug signal. [숫자 채우기]"*

## D4 · Cache · coherency · interconnect (NoC/AXI) (P1)

### 0. 🔰 이 블록이 뭔가

- **비유**: 인터커넥트는 **도시의 도로망 + 신호등**(NoC), 캐시 일관성은 **여러 사람이 공유 문서를 각자 메모장에 복사해 쓸 때 "누가 최신인지" 맞추는 규칙**. 도로가 막히면(경합) 구급차(실시간 master)가 먼저 가야 하고(QoS), 없는 주소로 가면 "그런 주소 없음"(decode error)을 돌려줘야 한다.
- **정확한 정의**: CPU 클러스터, NPU, ISP, DMA, PCIe 등 master와 DDR·SRAM·peripheral slave를 잇는 **AXI/ACE/CHI 기반 interconnect/NoC**. 주요 기능: 주소 decode, arbitration·QoS, **coherency**(snoop/snoop filter — full-coherent CPU vs IO-coherent ACE-Lite master vs non-coherent), ordering 보장, **timeout**(응답 없는 slave 감지), 보안 필터(TZ), PMU. Arm 표준 기반 IP(CMN/CCI/NIC 류) 또는 벤더 NoC(Arteris 등) [추정 — RAP1 구성 모름].

```text
  CPU cluster (L1/L2, full coherent) ──┐                 ┌──► DDR controller
  NPU (IO-coherent?) ──────────────────┤   coherent      ├──► on-chip SRAM
  ISP / camera DMA (non-coherent) ─────┼─► interconnect ─┼──► peripheral bus (APB)  ──► 없는 주소 → DECERR
  PCIe / Ethernet DMA ── SMMU ─────────┤   + snoop filter├──► (응답 없는 slave → timeout 모듈)
  safety island ───────────────────────┘   QoS · PMU     └──► secure 영역 (TZ 필터)
```

| 용어 | 풀네임 | 쉬운 설명 | 왜 중요한가 |
|---|---|---|---|
| snoop | — | 다른 캐시에 최신 복사본 있나 확인 | coherent 경로의 정확성·비용 |
| CMO | cache maintenance operation | clean / invalidate / clean+invalidate | non-coherent master의 필수 |
| ordering / barrier | DMB/DSB | 메모리 접근 순서 보장 | 약한 메모리 모델(Arm)에서만 나는 버그 |
| QoS | — | master별 우선순위·대역폭 상한 | 실시간 master 보호 |
| timeout | — | 응답 안 하는 slave 감지 후 에러 응답 | 없으면 시스템 hang |
| exclusive access | LDXR/STXR, atomics | 원자적 read-modify-write | 클러스터 간 lock 정확성 |

### 1. 먼저 물어볼 것 (clarify)

1. *"Which masters are fully coherent, IO-coherent, or non-coherent — and is that configurable per master?"* → 가정: CPU full, NPU IO-coherent, ISP·카메라 DMA non-coherent [추정].
2. *"What QoS mechanisms exist — priority fields, bandwidth regulators, per-master limits — and who sets them, firmware or Linux?"* → 가정: 부트 FW가 설정, 런타임 변경 없음.
3. *"Is there a bus timeout unit, and what does a master see on timeout — SLVERR, interrupt, both?"* → 가정: 둘 다.
4. *"Which PMU counters are available on the interconnect — per-port bandwidth, latency histograms?"* → 가정: 포트별 바이트·대기 cycle.
5. *"Is this licensed IP with its own verification, so we focus on our integration and configuration?"* → 가정: 라이선스 IP, 우리 몫은 **통합·설정·시스템 동작**.

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 레지스터 | 주소 맵 · decode | 모든 slave base/size 읽기·쓰기, 구멍(hole) 주소 | 둘 다 | 맵 문서와 100% 일치, 구멍은 DECERR (hang 아님) | P0 |
| 기능 | coherency: CPU ↔ IO-coherent master | NPU가 CPU 캐시에 있는 dirty 데이터 읽기 / 쓰기 후 CPU 읽기, CMO 없이 | 둘 다 | 항상 최신 데이터 | P0 |
| 기능 | non-coherent master + CMO | D1 cache 행과 동일, + 캐시 라인 공유 버퍼 | 둘 다 | CMO 있으면 일치, 없으면 실패(negative control) | P0 |
| 기능 | 멀티코어 coherency · atomics | 클러스터 간 공유 카운터 atomic 증가, spinlock | Si (FPGA 코어 수 제한) | 최종 카운트 정확, lock 상호 배제 위반 0 | P1 |
| ordering | litmus 테스트 (message passing 등) | barrier 유무, 코어 쌍 조합, 수백만 회 | Si | barrier 있으면 금지 결과 0회 — 없으면 관찰될 수 있음을 문서화 | P1 |
| ordering | device memory · MMIO 순서 | doorbell 앞 데이터 쓰기, posted write | 둘 다 | 장치가 항상 완성된 데이터 관찰 | P1 |
| 에러 | decode error · timeout | 없는 주소, clock-gated slave 접근, 응답 안 하는 slave(가능하면 주입) | 둘 다 | 에러 응답 + IRQ, **시스템 hang 0**, 로그에 master/주소 | P0 |
| 에러 | 보안 필터 | non-secure master가 secure 영역 접근 | 둘 다 | 거부 + 로그, 데이터 미변경 | P1 |
| QoS | 경합 하 우선순위·대역폭 상한 | 모든 master 최대 부하, QoS 설정 조합 | Si | 설정대로 분배(± 10% [추정]), 실시간 master 데드라인 위반 0 | P1 |
| 성능 | 포트별 bandwidth·latency | idle latency, 부하 곡선, snoop 비용(coherent vs non) | Si | 리포트 + 목표 대비 | P1 |
| 리셋·전원 | 전원 도메인 off 중 접근 | 꺼진 도메인 slave 접근, 도메인 off 중 진행 중 트랜잭션 | Si | 에러 응답(hang 아님), 재전원 후 정상 | P1 |
| stress | 랜덤 다 master 트래픽 soak | 모든 master 랜덤 주소·크기, CRC | Si | 데이터 오류 0, deadlock 0 (N시간) | P1 |
| PVT | 코너 동작 | Vmin/Vmax × 온도, 최대 부하 | Si | 오류·hang 0 | P2 |

### 3. 우선순위 · A0 첫 주

- **P0**: 주소 맵(모든 slave 접근 가능, 구멍은 에러), coherency 기본 2방향, decode error/timeout이 hang이 아니라 에러로 오는지 — **"hang 대신 에러"가 디버그 가능성을 결정**.
- **P1**: QoS·성능·litmus·atomics·전원 도메인. **P2**: PVT.
- **안 하는 것**: 프로토콜 규칙·coherency 프로토콜 상태 머신 전수(IP 벤더 + DV + formal). 우리는 **이 칩의 설정(QoS 값, coherency 연결, 주소 맵)이 맞는지**.

### 4. 장비 · 셋업 · 관찰 지점

- 인터커넥트 PMU(포트별 바이트·대기 cycle·snoop hit), CoreSight trace/ETM, timeout 모듈 로그, Linux `perf`(CPU PMU + 있으면 interconnect PMU 드라이버) [추정 — 벤더별].
- litmus 테스트는 coherency 모델 검증 표준 방법 (herd/litmus 도구 계열) [추정 — 출처 미조회].

### 5. 잘 터지는 버그

- 없는 주소 접근 시 **응답이 안 와서 CPU 전체 hang** (timeout 미설정/미연결) → JTAG로도 못 들어감.
- master의 coherency 속성 설정(AxCACHE/AxDOMAIN 같은 속성 비트, 드라이버 DT 속성)이 실제 연결과 불일치 → 랜덤 데이터 깨짐 (LP08 §2.6).
- QoS 기본값이 모두 같아 ISP가 NPU 부하에서 underflow.
- 꺼진 전원 도메인 접근으로 interconnect가 막혀 다른 master까지 정지.
- barrier 누락 코드가 x86/FPGA 소프트코어에선 통과, Arm 실리콘에서만 간헐 실패.

### 6. 10분 영어 답 뼈대

1. "Clarify coherency type per master, QoS mechanisms, timeout behavior, PMU, and whether it's licensed IP — then I focus on our integration and configuration."
2. "Address map first: every slave reachable, every hole returns an error instead of hanging."
3. "Coherency both directions for IO-coherent masters; cache maintenance with a negative control for non-coherent ones; atomics across clusters."
4. "Ordering: litmus tests with and without barriers, MMIO doorbell ordering."
5. "Errors: decode error, timeouts, security filter, powered-off domains — always an error response, never a hang."
6. "QoS and performance under full contention with PMU counters; real-time masters never miss deadlines."
7. "Random multi-master soak with CRC, then PVT."

### 7. 꼬리 질문

**Q1. "Why do you care so much that errors don't hang the system?"**

<details><summary>답</summary>

Because a hang destroys observability: if a CPU waits forever on a read from a dead slave, I can't read logs, sometimes not even through JTAG, and the board farm sees a timeout with no information. A decode error or timeout response gives me the master, the address and a clean exception I can log. So one of my first tests is to hit every hole in the address map and every gated slave, and confirm I get an error response.

한국어 핵심: hang = 관찰 불가 → **에러 응답이 디버그 가능성의 전제**.

</details>

**Q2. "How do you validate QoS settings?"**

<details><summary>답</summary>

I define the worst realistic concurrency — NPU and GPU at full bandwidth, cameras streaming, CPU running the stack — and measure per-master bandwidth and latency with interconnect counters, plus the real-time masters' own underflow counters. Then I vary the QoS settings to show the margin: how much more load before the ISP underflows. The pass criterion is zero deadline misses at the lowest memory frequency we use.

한국어 핵심: 최악 동시 부하 + PMU + underflow 카운터, **QoS 마진(얼마나 더 버티나)** 까지.

</details>

### 8. Don 연결

- *"At Solidigm I tuned controller performance with the silicon validation team, which was mostly interconnect and memory contention analysis with performance counters. [숫자 채우기: 개선 %]"*

## D5 · IOMMU / SMMU · 메모리 보호 (P1, 짧게)

### 0. 🔰 이 블록이 뭔가

- **비유**: 건물 **경비실 출입 명부** — 각 장치(택배 기사)는 명부에 적힌 방(매핑된 페이지)에만, 적힌 권한(읽기/쓰기)으로만 들어갈 수 있고, 위반하면 경비가 막고 기록(fault event)을 남긴다. 개념 설명은 [LP08 §1.4 · §2.5](2026-10-31_LP08_dma_iommu_coherency.html).
- **정확한 정의**: 장치 DMA 주소(IOVA)를 물리 주소로 변환하고 권한을 검사하는 Arm **SMMUv3** 류. StreamID로 장치 구분 → Stream Table → (stage 1/2) 페이지 테이블 → 실패 시 Event Queue + IRQ. 메모리 보호 쪽으로는 TrustZone 주소 필터(Arm TZC-400 류) 같은 영역 방화벽도 같은 플랜에 묶는다 [추정 — RAP1 구성 모름].

```text
  device DMA (StreamID, IOVA) ─► SMMU: STE ─► CD ─► page table walk (TLB) ─► PA ─► interconnect
                                    └─ fault ─► Event Queue (F_TRANSLATION / F_PERMISSION …) ─► IRQ
```

### 1. 먼저 물어볼 것 (clarify)

1. *"Which masters sit behind which SMMU instance, and how are StreamIDs assigned?"*
2. *"Stage 1 only, or stage 2 for virtualization — is there a hypervisor?"*
3. *"Are there region firewalls for secure memory, and who programs them — boot ROM, TF-A?"*
4. *"Do any devices use ATS or PASID?"* → 가정: 없음 (P2).

### 2. 테스트 매트릭스

| 카테고리 | 테스트 항목 | 조건/파라미터 | 플랫폼 | 통과 기준 | 우선 |
|---|---|---|---|---|---|
| 기능 | 기본 변환 | 4 KiB / 2 MiB 페이지, 여러 장치 | 둘 다 | DMA 데이터 정확, PA 일치 | P0 |
| 에러 | translation fault | 매핑 안 된 IOVA | 둘 다 | event 1건(정확한 SID/주소), 메모리 미변경, 장치 에러 보고 | P0 |
| 에러 | permission fault | read-only에 write, 권한 없는 실행 | 둘 다 | permission event, 데이터 미변경 | P0 |
| 격리 | 장치 간 격리 | 장치 A가 장치 B 매핑 접근 | 둘 다 | fault, B 데이터 무손상 | P1 |
| TLB | invalidation | unmap 직후 접근, strict vs lazy | Si | strict에서 unmap 후 접근 fault, stale 접근 0 | P1 |
| 보안 | secure 영역 방화벽 | non-secure master → secure DDR | 둘 다 | 거부 + 로그 | P1 |
| 성능 | TLB miss 비용 | 랜덤 IOVA vs 순차, passthrough 비교 | Si | 대역폭 손실 % 리포트 | P2 |
| stress | fault 폭주 | 초당 수천 fault | Si | event queue overflow 처리, 시스템 안정 | P2 |

### 3. 우선순위 · A0 첫 주

- **P0**: 변환 1건 + translation fault + permission fault (QEMU 실측 fault 로그 예: [LP08 §3.2](2026-10-31_LP08_dma_iommu_coherency.html)). **P1**: 격리·TLB invalidation·방화벽. **P2**: 성능·fault 폭주.
- **안 하는 것**: 페이지 테이블 형식·walk 로직 전수(IP 벤더 + DV). 우리는 **StreamID 연결·설정·fault 보고 경로**.

### 4. 장비 · 셋업 · 관찰 지점

- SMMU event queue 내용(SID · 주소 · 종류), `dmesg`의 fault 줄, `/sys/kernel/iommu_groups`, 방화벽 에러 로그 레지스터. bare-metal이면 event queue를 직접 polling.

### 5. 잘 터지는 버그

- StreamID 배선/설정 오류 → 엉뚱한 장치 컨텍스트로 변환 (다른 장치 메모리 접근).
- fault IRQ 미연결 → fault가 조용히 무시되고 장치는 timeout만 보고.
- TLB invalidation 순서 버그 → unmap 후에도 잠깐 접근 가능 (lazy 모드와 구분 필요).

### 6. 10분 영어 답 뼈대

1. "Clarify which masters sit behind which SMMU, StreamIDs, stages, firewalls."
2. "Map and translate with 4 KB and 2 MB pages; then translation and permission faults, checking the exact event contents."
3. "Isolation between devices; TLB invalidation timing in strict mode; secure-region firewall."
4. "Then the performance cost of translation and behavior under fault storms."

### 7. 꼬리 질문

**Q. "Why is the SMMU good for validation, not just security?"**

<details><summary>답</summary>

Without an SMMU, a DMA bug that writes to the wrong address silently corrupts memory and shows up later as a random crash. With the SMMU in translated mode, that same bug produces a fault event with the device, the address and the access type, right at the moment it happens. So I keep the SMMU on during validation runs even if production might use passthrough for some devices.

한국어 핵심: SMMU = 잘못된 DMA를 **즉시 fault 로그로** 바꿔 주는 검증 도구.

</details>

### 8. Don 연결

- *"On PCIe-attached devices at Apple and Solidigm, IOMMU faults were often the first clean evidence of a DMA addressing bug. [숫자 채우기]"*

## 실습 · 코드 (실행 결과)

> 위치: [onsitePrep/n06/d/](onsitePrep/n06/d/run.sh) · 실행: `./run.sh` (macOS clang, `-Wall -Wextra -Werror -fsanitize=address,undefined`, 2026-11-01 실행 ✅). 결과는 **모델**이며 실리콘 수치가 아님 — 면접에서 "how do I know my plan is good"의 근거로 쓰는 용도.

### dma_matrix.c — D1 매트릭스 11행 + 버그 11개 주입 (mutation test)

- 모델: 4채널 · 채널당 8-slot ring · OWN handoff · 256 B burst · 16 B/cycle 버스 · descriptor fetch 16 cycle · **4 KiB 분할** · 우선순위+aging / RR arbiter · IRQ coalescing(개수+타이머) · DECERR · 채널 reset · CPU write-back 캐시 모델(non-coherent 흐름).
- 각 행은 데이터 + **앞뒤 16 B guard** + status를 검사. 버그를 하나씩 켜고 전체 매트릭스를 다시 돌린다.

```text
== baseline (no injected bug) ==
R01 size x src/dst alignment sweep       cases= 420 fail=0  PASS
R02 4 KiB boundary crossing              cases=  50 fail=0  PASS
R03 descriptor chain / gather            cases= 140 fail=0  PASS
R04 ring wrap + ring full (-EBUSY)       cases=  31 fail=0  PASS
R05 4-ch priority/aging + RR fairness    cases=   2 fail=0  PASS
R06 error injection (DECERR)             cases=   4 fail=0  PASS
R07 IRQ coalescing (count + timer)       cases=   2 fail=0  PASS
R08 non-coherent cache maintenance       cases=   6 fail=0  PASS
R09 channel reset mid-transfer           cases=   3 fail=0  PASS
R10 throughput efficiency                cases=   5 fail=0  PASS
R11 random soak 1200 xfers, seed         cases=1200 fail=0  PASS
R05 detail: low-prio max wait=99 cycles (limit 256); RR share min/max = 25.0% / 25.0%
R10 detail (model: 16 B/cycle bus, 16-cycle desc fetch, 256 B bursts):
  len=   64  cycles=   21  efficiency= 19%
  len=  256  cycles=   33  efficiency= 48%
  len= 1024  cycles=   84  efficiency= 76%
  len= 4096  cycles=  288  efficiency= 88%
  len=16384  cycles= 1104  efficiency= 92%

== mutation matrix: x = row FAILED (bug caught), . = row passed ==
injected bug                                 01 02 03 04 05 06 07 08 09 10 11  caught
B1  tail overrun: +1 byte when len%4!=0       x  x  x  x  .  .  .  .  .  .  x  YES
B2  burst crosses 4KiB, addr wraps in page    x  x  x  .  x  .  .  x  .  .  x  YES
B3  src/dst misaligned: last byte dropped     x  x  x  .  .  .  .  .  .  .  x  YES
B4  ring wrap skips slot 0                    x  x  x  x  x  x  x  .  .  .  x  YES
B5  strict priority w/o aging (starvation)    .  .  .  .  x  .  .  .  .  .  .  YES
B6  DECERR reported as OK                     .  .  .  .  .  x  .  .  .  .  .  YES
B7  coalescing timer never fires              .  .  .  .  .  .  x  .  .  .  .  YES
B8  chain: EOP marked done one desc early     .  .  x  .  .  .  .  .  .  .  x  YES
B9  channel reset keeps in-flight desc        .  .  .  .  .  .  .  .  x  .  .  YES
B10 driver: no cache clean before DMA         .  .  .  .  .  .  .  x  .  .  .  YES
B11 driver: no invalidate after DMA           .  .  .  .  .  .  .  x  .  .  .  YES

baseline: ALL PASS
bugs caught by full matrix : 11/11
bugs caught by random soak : 5/11  (directed rows needed for the rest)
RESULT dma_matrix PASS
```

- **읽는 법 (면접에서 말할 것)**
  - 데이터 경로 버그(B1–B4, B8)는 **크기·정렬 sweep(R01)과 랜덤 soak(R11)이 둘 다** 잡는다.
  - 제어 경로 버그 — starvation(B5) · 에러 은폐(B6) · coalescing 타이머(B7) · reset(B9) · 캐시 흐름(B10/B11) — 는 **각각 전용 directed 행 하나만** 잡는다 → "랜덤만 돌리면 된다"는 틀렸다는 실측 근거 (soak 5/11).
  - R10(성능)은 기능 버그를 하나도 못 잡는다 — 성능 행은 기능 행을 대체하지 못함. 효율 곡선: 64 B 19% → 4 KiB 88% → 16 KiB 92% (descriptor fetch 오버헤드가 작은 전송을 지배).
  - B4(ring wrap)는 R05·R06·R07에서도 우연히 잡힘 — 전송이 8개를 넘으면 어디서든 wrap하기 때문. **"우연히 잡히는" 커버리지에 기대지 말고 전용 행(R04)을 두는 것**이 원칙.

### secded_inject.c — D2/D3 ECC (72,64) SEC-DED 주입 검증

```text
T1 single-bit : 72000 injections, corrected+exact 72000  PASS
T2 double-bit : 511200 injections (200 words x 2556 pairs), UE 511200, not-UE 0  PASS
T3 triple-bit : 200000 injections -> mis-corrected (reported CE, data wrong) 75.9%, detected UE 24.1%, silent 0.0%  INFO
T4 counters   : CE@100 logged=1, 2nd CE keeps first addr=1, re-read after write-back clean=1, 2-bit -> UE irq=1  PASS
T5 patrol scrub (4096 words, 1 upset/epoch, 4000 epochs):
  no scrub             : CE= 1741  UE=757
  scrub every 400 epochs: CE= 3654  UE=152
  scrub every 100 epochs: CE= 3898  UE=43
  scrub every  20 epochs: CE= 3978  UE=10
  scrub every   5 epochs: CE= 3992  UE=4
T5 trend (more scrub -> fewer UE): PASS
RESULT secded_inject PASS
```

- T1/T2: **check bit 8개 위치까지 포함**해 1비트 72위치 × 1000 word 전부 정정, 2비트 2556쌍 × 200 word 전부 UE — 매트릭스의 "모든 비트 위치" 행이 실제로 어떤 모양인지.
- T3: 3비트 오류는 **75.9%가 "정정했다(CE)"고 보고되면서 데이터가 틀림** (extended Hamming 모델 기준; Hsiao 등 다른 SEC-DED 코드는 비율이 다를 수 있음 [추정]) → "ECC 있으니 안전"이 아니라 scrub·오류율 모니터링이 필요한 이유.
- T5: 같은 upset 시퀀스에서 scrub 주기를 줄이면 UE 757 → 152 → 43 → 10 → 4. scrub rate는 **검증할 설정값**.

## 자주 틀리는 것

1. **dst만 비교** — guard 영역·src 재검사 없이 "PASS" → overrun은 다른 데서 crash로 나타난다.
2. **정렬된 크기(4 KiB, 64 B 배수)만 테스트** — 버그는 1, 3, 63, 65, 4097 같은 홀수 크기와 src·dst **상대** 정렬 차이에 숨어 있다.
3. **랜덤 soak으로 충분하다고 말하기** — 실측 5/11. 에러·reset·coalescing·starvation은 directed 테스트가 필요.
4. **"training 통과 = DDR OK"** — 성공률(부팅 루프 × 온도), lane별 분포, 마진이 증거. 1000회 0 실패도 95% 상한 ≈ 0.3% (rule of three: 3/n).
5. **고온 테스트를 "온도 올리고 돌리기"로만** — 고온에서 **부팅**(cold start at hot), 85 °C 통과 시 refresh 배율 변화 실측까지 (ERR050805류).
6. **ECC 주입을 data bit에만** — check bit 오류도 정정돼야 하고, 2비트는 "절대 정정하면 안 됨", 3비트는 오정정 가능(특성화)임을 구분.
7. **ECC 종류 혼동** — on-die ECC(DRAM 내부, 버스 오류 못 봄) ≠ link ECC ≠ inline/system ECC (BV09 §2.4).
8. **cache 행에 negative control 없음** — CMO를 빼도 통과하면 그 테스트는 캐시를 안 거치고 있는 것.
9. **성능을 이론치 없이 GB/s만 보고** — 이론치(폭 × 클럭 × 효율)와 크기별 곡선, outstanding × latency 설명까지.
10. **DV가 덮은 AXI 프로토콜 규칙을 실리콘 플랜에 나열** — 시간 낭비 시그널. "DV owns protocol compliance; on silicon I focus on what only silicon shows."

## 출처

1. Linux kernel docs, "DMA Test Guide" (dmatest 사용법, 결과/summary 줄 형식) — https://docs.kernel.org/driver-api/dmaengine/dmatest.html
2. Linux `drivers/dma/dmatest.c` (module_param 목록: threads_per_chan, test_buf_size, alignment, transfer_size, noverify, norandom, polled …; PATTERN_SRC/COPY/OVERWRITE 검증, src 덮어쓰기·범위 밖 쓰기 분류) — https://raw.githubusercontent.com/torvalds/linux/master/drivers/dma/dmatest.c
3. AXI 4 KiB burst 경계 규칙 — Xilinx wiki "Validating a master AXI4 interface using the Verification IP as a slave (part 2)" (AXI_ERRM_AWADDR_BOUNDARY, 0xFC8 예) — https://xilinx-wiki.atlassian.net/wiki/spaces/A/pages/18841951/Validating+a+master+AXI4+interface+using+the+Verification+IP+as+a+slave+part+2 · Arm Community 답변(4 KiB = 최소 slave 크기 trade-off) — https://community.arm.com/support-forums/f/soc-design-and-simulation-forum/8138/axi/28420
4. U-Boot commit "verdin-imx8mm: set fixed LPDDR4 refresh rate as per errata ERR050805" (85 °C 이상 시작 시 자동 derating 실패, 고정 2× refresh 우회) — https://git01.mediatek.com/plugins/gitiles/filogic/uboot/+/bcdd9387ed9d682983c52658ff76daffd669c1ec (검색 요약으로 확인)
5. NXP Community, "LPDDR4 Temperature Sensor" (MR4는 raw 온도가 아니라 refresh 배율 bitfield) — https://community.nxp.com/t5/i-MX-Processors/LPDDR4-Temperature-Sensor/m-p/1226091 (검색 요약으로 확인)
6. Intel 12th Gen Core datasheet vol.2, "MR4_RANK_TEMPERATURE" (MR4 읽어 refresh·thermal 결정, LPDDR5 1× refresh 코드 언급) — https://edc.intel.com/content/www/it/it/design/publications/12th-generation-core-processor-datasheet-volume-2-of-2/mr4-rank-temperature-mr4-rank-temperature-0-0-0-mchbar-offset-e424 (검색 요약으로 확인) — LPDDR5 MR4 정확한 인코딩과 "85 °C 초과 시 4×" 주장은 JEDEC JESD209-5 원문 미확인 → [추정]
7. stressapptest (Google, README: 랜덤 대량 메모리/IO 트래픽 + CRC, SI·타이밍·컨트롤러 버그에 강함) — https://github.com/stressapptest/stressapptest
8. BV09가 인용한 DDR 자료(LPDDR4 tREFI 3.9 µs/32 ms·MR4 0.5×~4× = arXiv 2302.03591, Synopsys LPDDR5 FSP·link/inline ECC, Linux synopsys_edac 주입, EDAC sysfs) — [BV09 §8](2026-11-01_BV09_ddr_bringup_training_memtest.html) 경유
9. 이 페이지 코드 실행 결과 — [dma_matrix.out](onsitePrep/n06/d/dma_matrix.out) · [secded_inject.out](onsitePrep/n06/d/secded_inject.out) (2026-11-01, macOS clang + ASan/UBSan)
