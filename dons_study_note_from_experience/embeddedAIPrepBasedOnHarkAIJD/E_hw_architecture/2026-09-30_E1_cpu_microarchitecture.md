# E1. CPU 마이크로아키텍처 — 파이프라인, OoO, 캐시, 분기 예측을 ML 커널 관점에서

> **이 노트를 다 읽으면**: latency와 throughput을 구분해서 "accumulator가 몇 개 필요한가"를 손으로 계산할 수 있다 · pointer chasing과 stride 벤치마크로 캐시 계층·라인 크기·TLB를 직접 재고 그래프를 해석할 수 있다 · 루프 순서, tiling, padding, prefetch, branchless, false sharing이 ML 커널에서 왜 효과가 있는지(또는 없는지) 숫자로 설명할 수 있다 · Cortex-M4/M7 어셈블리를 읽고 in-order MCU와 OoO 코어에서 같은 커널이 어떻게 다르게 도는지 말할 수 있다
> **JD 연결**: "Familiarity with embedded systems, CPU/DSP/NPU HW architectures" · "Profile and optimize memory usage, power consumption, real-time performance" · study_prep_list **E1** 행: pipeline, superscalar, out-of-order, cache 계층, cache line, TLB, prefetch, branch prediction ("커널 최적화·프로파일 해석")
> **Don 기준 난이도**: 캐시·TCM·DMA·bare-metal 타이밍은 펌웨어 수준으로 이미 안다 / 새로 채울 것은 "OoO 코어가 무엇을 숨겨 주고 무엇을 못 숨기나"의 직관, 그리고 그것을 마이크로벤치마크로 재서 ML 커널(GEMM, conv, 후처리) 설계에 연결하는 방법
> **선행 노트**: A1 8절(루프 순서와 캐시, ijk vs ikj 12배), D3 5절(이 Mac의 L1/L2/DRAM 대역폭 실측, NEON FMA 천장), D6 2절·4절(벤치마크 하니스와 측정 함정). 병렬 작성 중인 E2(Cortex-M/A, NEON/Helium), E7(메모리 시스템)과 함께 읽으면 좋다

---

## 0. 큰 그림 — 이게 왜 필요한가

ML 커널(행렬곱, conv, attention, 후처리)은 결국 **CPU 코어 위에서 도는 루프**다. 같은 수학, 같은 MAC 수인데 A1 8절에서 루프 순서만 바꿔 12배가 났고, D3 5절에서 같은 GEMV가 행렬 크기에 따라 DRAM 지붕 위아래를 오갔다. 그 차이를 만드는 것이 이 노트의 주제인 **마이크로아키텍처**(microarchitecture: 같은 ISA를 실제 하드웨어로 어떻게 구현했나 — 파이프라인 깊이, 폭, 캐시, 예측기)다.

Don은 Cortex-R8/R82, M0+, Xtensa에서 캐시를 켜고 끄고, TCM에 코드를 올리고, DMA로 버퍼를 옮겨 봤다. 그건 **펌웨어가 보는 캐시**(설정, coherence 관리, flush/invalidate)다. ML 커널 엔지니어가 보는 캐시는 조금 다르다: "이 루프가 한 번 가져온 캐시 라인을 몇 번 재사용하나", "지금 동시에 몇 개의 miss가 날아가고 있나", "이 FMA는 앞 FMA 결과를 기다리고 있나". 이 노트는 그 관점을 채운다.

한 코어를 ML 커널 관점으로 단순화하면 이렇다.

```
                ┌──────────────────── CPU 코어 1개 ────────────────────┐
  명령어 ──▶    │ Front-end: fetch → decode  ◀── 분기 예측기 (7절)       │
  (I-cache)     │        │                                             │
                │        ▼                                             │
                │ (OoO 코어만) rename → 대기열/ROB  ── 명령 창 (1절)     │
                │        │                                             │
                │        ▼                                             │
                │ 실행 유닛: ALU×n, FMA/SIMD×n (2절), Load/Store×n     │
                │        │                  │                          │
                │        │           store buffer (6절)                │
                │        ▼                  ▼                          │
                │      L1 D-cache (3절)  ◀── prefetcher (5절)          │
                └────────┬─────────────────────────────────────────────┘
                         ▼
                 L2 (클러스터 공유) ──▶ (SLC) ──▶ DRAM (LPDDR)  (3절, E7)
                         ▲
                 다른 코어들 — coherence, false sharing (8절)
```

ML 커널 성능은 이 그림에서 네 가지 질문으로 거의 결정된다.

| 질문 | 막히면 생기는 일 | 이 노트의 절 | 대표 처방 |
|---|---|---|---|
| 연산 유닛이 매 cycle 채워지나? | 의존 체인 때문에 FMA 파이프가 논다 | 1, 2 | 여러 accumulator, unroll, SIMD |
| 데이터가 가까운 캐시에 있나? | L1 miss → L2/DRAM 대기 | 3, 4 | 루프 순서, tiling, layout, padding |
| miss를 겹쳐서 기다리나? | 한 번에 한 miss씩 → latency 그대로 노출 | 5, 6 | 순차 접근(HW prefetch), 독립 접근 늘리기 |
| 분기를 잘 맞히나? | misprediction마다 파이프라인 flush | 7 | branchless(select), 데이터 정렬, SIMD 마스크 |

### 측정 환경과 규칙

이 노트의 모든 숫자는 아래 기계에서 직접 잰 것이다.

| 항목 | 값 | 출처 |
|---|---|---|
| SoC | Apple M2, P-core 4 + E-core 4 | `sysctl hw.perflevel0/1.physicalcpu` |
| L1d / L1i (P-core) | 128 KiB / 192 KiB | `sysctl hw.perflevel0.l1dcachesize`, `l1icachesize` |
| L2 (P 클러스터 4코어 공유) | 16 MiB | `sysctl hw.perflevel0.l2cachesize`, `cpusperl2` |
| L1d / L2 (E-core) | 64 KiB / 4 MiB | `sysctl hw.perflevel1.*` |
| 캐시 라인 | 128 B | `sysctl hw.cachelinesize` |
| 페이지 | 16 KiB | `sysctl hw.pagesize` (x86·많은 Linux ARM은 4 KiB) |
| 컴파일러 | Apple clang 21, `cc -std=c11 -Wall -Wextra -O2` | 모든 예제 경고 0개 |

- 측정 중 **다른 작업이 동시에 돌고 있었다**(load average 2.3~3.6). 그래서 모든 벤치마크는 여러 번 반복해 **최소값(best)**을 쓰고, 여러 번 실행한 결과를 비교해 흔들림을 적었다. 소수점 둘째 자리는 믿지 말고 **자릿수와 비율**을 읽자(D6 2절의 원칙 그대로).
- macOS는 스레드를 특정 코어에 고정(affinity)하는 API를 공개하지 않는다(8.3절). 단일 스레드 측정은 스케줄러가 P-core에 올렸다고 **추정**한다 — 1-cycle 명령 체인으로 잰 클럭이 약 3.2~3.45 GHz로 나와서(M2 P-core 최대 클럭 약 3.5 GHz) 이 추정과 맞는다.
- 벤치마크 코드는 모두 `/private/tmp/claude-501/e1/`에서 만들고 실행했다. 노트 폴더에는 남기지 않는다.

---

## 1. 파이프라인 기초 — 한 명령이 아니라 "흐름"으로 보기

### 1.1 직관 — 세탁소 비유

빨래 한 번 = 세탁(30분) → 건조(30분) → 개기(30분). 한 사람이 빨래 4번을 순서대로 하면 12단계 × 30분 = 6시간. 그런데 첫 빨래가 건조기에 들어가는 순간 다음 빨래를 세탁기에 넣으면, 기계 세 대가 동시에 일해서 3 + (4 − 1) = 6단계 = 3시간에 끝난다. **빨래 하나에 걸리는 시간(latency)은 그대로 90분**인데, **시간당 끝나는 빨래 수(throughput)가 3배**가 됐다. CPU 파이프라인이 정확히 이것이다.

### 1.2 정의 — 교과서 5단 파이프라인

| 단계 | 하는 일 | Don에게 익숙한 대응 |
|---|---|---|
| IF (fetch) | PC 주소에서 명령어를 I-cache/flash에서 가져온다 | flash wait state, I-cache |
| ID (decode) | 명령을 해석하고 레지스터를 읽는다 | |
| EX (execute) | ALU/곱셈기 계산, 주소 계산 | MAC 유닛 |
| MEM (memory) | load/store가 D-cache/SRAM에 접근 | TCM 1-cycle 접근 |
| WB (writeback) | 결과를 레지스터에 쓴다 | |

- **IPC**(instructions per cycle): cycle당 끝나는(retire) 명령 수. 역수는 **CPI**(cycles per instruction). 이상적인 스칼라 파이프라인은 IPC = 1.
- **hazard**: 다음 명령이 다음 cycle에 들어갈 수 없는 상황. 아래 세 종류다.
- **structural hazard**: 자원이 모자람 (예: 메모리 포트 하나를 fetch와 load가 동시에 원함).
- **data hazard**: 앞 명령 결과가 아직 없음 (예: `ldr r1` 직후 `add r2, r1, r4`).
- **control hazard**: 분기 결과를 모르는데 다음 명령을 가져와야 함 → 분기 예측(7절).
- **forwarding(bypass)**: 결과를 레지스터 파일에 쓰기 전에 다음 명령의 EX 입력으로 바로 넘겨 주는 배선. 대부분의 data hazard를 없애지만, **load 결과는 MEM이 끝나야** 나오므로 바로 뒤 명령은 1 cycle 기다린다(load-use hazard).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 300"><text x="20" y="22" font-size="14">5단 파이프라인: 명령 4개가 겹쳐 흐른다 (위) / load-use hazard로 bubble 1개 (아래)</text><text x="146.0" y="44" font-size="12" text-anchor="middle">c1</text><text x="198.0" y="44" font-size="12" text-anchor="middle">c2</text><text x="250.0" y="44" font-size="12" text-anchor="middle">c3</text><text x="302.0" y="44" font-size="12" text-anchor="middle">c4</text><text x="354.0" y="44" font-size="12" text-anchor="middle">c5</text><text x="406.0" y="44" font-size="12" text-anchor="middle">c6</text><text x="458.0" y="44" font-size="12" text-anchor="middle">c7</text><text x="510.0" y="44" font-size="12" text-anchor="middle">c8</text><text x="562.0" y="44" font-size="12" text-anchor="middle">c9</text><text x="114" y="65" font-size="12" text-anchor="end">i1: ldr r1,[r0]</text><rect x="121" y="50" width="50" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><text x="146.0" y="65" font-size="12" text-anchor="middle">IF</text><rect x="173" y="50" width="50" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><text x="198.0" y="65" font-size="12" text-anchor="middle">ID</text><rect x="225" y="50" width="50" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="250.0" y="65" font-size="12" text-anchor="middle">EX</text><rect x="277" y="50" width="50" height="22" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><text x="302.0" y="65" font-size="12" text-anchor="middle">MEM</text><rect x="329" y="50" width="50" height="22" fill="#888" fill-opacity="0.35" stroke="#888"/><text x="354.0" y="65" font-size="12" text-anchor="middle">WB</text><text x="114" y="91" font-size="12" text-anchor="end">i2: add r2,r3,r4</text><rect x="173" y="76" width="50" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><text x="198.0" y="91" font-size="12" text-anchor="middle">IF</text><rect x="225" y="76" width="50" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><text x="250.0" y="91" font-size="12" text-anchor="middle">ID</text><rect x="277" y="76" width="50" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="302.0" y="91" font-size="12" text-anchor="middle">EX</text><rect x="329" y="76" width="50" height="22" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><text x="354.0" y="91" font-size="12" text-anchor="middle">MEM</text><rect x="381" y="76" width="50" height="22" fill="#888" fill-opacity="0.35" stroke="#888"/><text x="406.0" y="91" font-size="12" text-anchor="middle">WB</text><text x="114" y="117" font-size="12" text-anchor="end">i3: sub r5,r6,r7</text><rect x="225" y="102" width="50" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><text x="250.0" y="117" font-size="12" text-anchor="middle">IF</text><rect x="277" y="102" width="50" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><text x="302.0" y="117" font-size="12" text-anchor="middle">ID</text><rect x="329" y="102" width="50" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="354.0" y="117" font-size="12" text-anchor="middle">EX</text><rect x="381" y="102" width="50" height="22" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><text x="406.0" y="117" font-size="12" text-anchor="middle">MEM</text><rect x="433" y="102" width="50" height="22" fill="#888" fill-opacity="0.35" stroke="#888"/><text x="458.0" y="117" font-size="12" text-anchor="middle">WB</text><text x="114" y="143" font-size="12" text-anchor="end">i4: mul r8,r9,r9</text><rect x="277" y="128" width="50" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><text x="302.0" y="143" font-size="12" text-anchor="middle">IF</text><rect x="329" y="128" width="50" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><text x="354.0" y="143" font-size="12" text-anchor="middle">ID</text><rect x="381" y="128" width="50" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="406.0" y="143" font-size="12" text-anchor="middle">EX</text><rect x="433" y="128" width="50" height="22" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><text x="458.0" y="143" font-size="12" text-anchor="middle">MEM</text><rect x="485" y="128" width="50" height="22" fill="#888" fill-opacity="0.35" stroke="#888"/><text x="510.0" y="143" font-size="12" text-anchor="middle">WB</text><line x1="20" y1="170" x2="640" y2="170" stroke="#888" stroke-dasharray="3 3"/><text x="114" y="203" font-size="12" text-anchor="end">i1: ldr r1,[r0]</text><rect x="121" y="188" width="50" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><text x="146.0" y="203" font-size="12" text-anchor="middle">IF</text><rect x="173" y="188" width="50" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><text x="198.0" y="203" font-size="12" text-anchor="middle">ID</text><rect x="225" y="188" width="50" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="250.0" y="203" font-size="12" text-anchor="middle">EX</text><rect x="277" y="188" width="50" height="22" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><text x="302.0" y="203" font-size="12" text-anchor="middle">MEM</text><rect x="329" y="188" width="50" height="22" fill="#888" fill-opacity="0.35" stroke="#888"/><text x="354.0" y="203" font-size="12" text-anchor="middle">WB</text><text x="114" y="229" font-size="12" text-anchor="end">i2: add r2,r1,r4</text><rect x="173" y="214" width="50" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><text x="198.0" y="229" font-size="12" text-anchor="middle">IF</text><rect x="225" y="214" width="50" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><text x="250.0" y="229" font-size="12" text-anchor="middle">ID</text><rect x="277" y="214" width="50" height="22" fill="none" stroke="currentColor" stroke-dasharray="3 2"/><text x="302.0" y="229" font-size="12" text-anchor="middle">stall</text><rect x="329" y="214" width="50" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="354.0" y="229" font-size="12" text-anchor="middle">EX</text><rect x="381" y="214" width="50" height="22" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><text x="406.0" y="229" font-size="12" text-anchor="middle">MEM</text><rect x="433" y="214" width="50" height="22" fill="#888" fill-opacity="0.35" stroke="#888"/><text x="458.0" y="229" font-size="12" text-anchor="middle">WB</text><text x="114" y="255" font-size="12" text-anchor="end">i3: sub r5,r6,r7</text><rect x="225" y="240" width="50" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><text x="250.0" y="255" font-size="12" text-anchor="middle">IF</text><rect x="277" y="240" width="50" height="22" fill="none" stroke="currentColor" stroke-dasharray="3 2"/><text x="302.0" y="255" font-size="12" text-anchor="middle">stall</text><rect x="329" y="240" width="50" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><text x="354.0" y="255" font-size="12" text-anchor="middle">ID</text><rect x="381" y="240" width="50" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="406.0" y="255" font-size="12" text-anchor="middle">EX</text><rect x="433" y="240" width="50" height="22" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><text x="458.0" y="255" font-size="12" text-anchor="middle">MEM</text><rect x="485" y="240" width="50" height="22" fill="#888" fill-opacity="0.35" stroke="#888"/><text x="510.0" y="255" font-size="12" text-anchor="middle">WB</text><path d="M328 199.0 C 338 209.0, 338 215.0, 330 218" fill="none" stroke="#d0564a" stroke-width="1.5"/><text x="120" y="282" font-size="12">빨간 화살표: r1 값은 i1의 MEM 끝에야 나온다 → i2의 EX가 1 cycle 밀리고 i3도 따라 밀린다</text></svg>
```

그림 1 — 위: hazard가 없으면 매 cycle 명령 하나가 들어가고 하나가 끝난다(c5부터 IPC = 1). 아래: load 직후 그 값을 쓰는 명령은 forwarding이 있어도 1 cycle bubble(stall)이 생긴다. 컴파일러가 load와 사용 사이에 무관한 명령을 끼워 넣는 것(instruction scheduling)이 이 bubble을 메우는 방법이다.

### 1.3 손계산 — 명령 100개의 cycle 수

5단 파이프라인, 명령 100개, hazard 없음: `5 + (100 − 1) = 104 cycle` → IPC = 100 ÷ 104 ≈ 0.96.
그중 20개가 load이고, 그 load 결과를 바로 다음 명령이 쓴다면 bubble 20개: `104 + 20 = 124 cycle` → IPC ≈ 0.81.
분기가 10개이고 그중 30 %를 틀리며 틀릴 때마다 2 cycle을 버린다면: `124 + 10 × 0.3 × 2 = 130 cycle` → IPC ≈ 0.77.

말로 하면: 파이프라인의 이론 IPC는 1이지만, **load-use 대기와 분기 실패가 조금씩 깎아 먹는다.** 짧은 파이프라인(Cortex-M4의 3단)은 한 번 틀릴 때 버리는 cycle이 적고, 깊은 파이프라인(고클럭 앱 코어)은 많다. 그래서 고클럭 코어일수록 분기 예측기가 크고 정교하다.

### 1.4 superscalar와 out-of-order (OoO)

- **superscalar**: 한 cycle에 명령을 여러 개 fetch/decode/issue한다. 이론 IPC > 1.
- **in-order**: 명령을 프로그램 순서대로만 issue한다. 앞 명령이 막히면(cache miss 등) 뒤의 무관한 명령도 기다린다.
- **out-of-order (OoO)**: 명령을 큰 창(window)에 모아 두고, **입력이 준비된 명령부터** 실행한다. 결과는 ROB(reorder buffer)에서 원래 순서대로 확정(retire)해서 프로그램이 보기에는 순서대로 실행된 것과 같다. **register renaming**(레지스터 이름 바꾸기)으로 같은 레지스터 이름을 재사용해서 생기는 가짜 의존성(WAR, WAW)을 없앤다.

식당 주방 비유: in-order 주방은 주문 1번 스테이크(20분)가 굽히는 동안 주문 2번 샐러드(2분)도 기다린다. OoO 주방은 샐러드를 먼저 내보낸다. 단, **주문 3번이 "주문 1번 스테이크를 잘라서 만든 샌드위치"라면 OoO도 기다릴 수밖에 없다.** 이것이 2절의 의존 체인이다.

OoO가 **숨겨 주는 것**: 짧은 latency(L1/L2 miss 일부, 곱셈 latency), 명령 스케줄링이 서툰 코드, 서로 독립인 여러 miss를 동시에 날리기(6절).
OoO가 **못 숨기는 것**: (1) 진짜 의존 체인(누산 하나에 모든 FMA가 물린 루프), (2) 창 크기보다 긴 대기(DRAM miss 약 100 ns = 수백 cycle 동안 창이 꽉 차면 멈춘다), (3) 분기 예측 실패(창 안의 추측 실행분을 버린다).

### 1.5 코어 비교 — ML 커널을 돌릴 후보들

| 코어 | 파이프라인 | 실행 방식 | 캐시 / 근접 메모리 | ML 커널 관점 한 줄 |
|---|---|---|---|---|
| Cortex-M0+ | 2단 | in-order, 스칼라 | 보통 캐시 없음, SRAM 직결 | 곱셈기만 있음. 아주 작은 모델·후처리용 |
| Cortex-M4(F) | 3단 + 분기 추측 | in-order, 스칼라 | 코어에 캐시 없음(벤더가 flash accelerator 추가) | DSP 확장(SIMD 16×2, SMLAD), 단정밀도 FPU. 사이클 예측이 쉽다 |
| Cortex-M7 | 6단 | in-order, **dual-issue** superscalar, 분기 예측 | I/D 캐시(옵션) + ITCM/DTCM | M4의 약 2배급 성능. 캐시를 쓰면 타이밍이 흔들린다 → TCM 권장 |
| Cortex-M55 / M85 | (E2 참조) | in-order | TCM + 캐시 | Helium(MVE) 벡터 — E2의 주제 |
| Cortex-A55 | 8단(Arm 발표 기준) | in-order, 부분 dual-issue | L1/L2(/L3) | 폰의 LITTLE 코어. NEON dot-product |
| Cortex-A7x / X 계열 | 깊은 파이프라인 | **OoO**, 넓은 decode | 큰 L1/L2 + 공유 L3 | 폰의 big 코어. CPU 추론의 주력 |
| Apple M2 P-core | 깊은 파이프라인 | **OoO**, 매우 넓다(공개 분석 기준 decode 8개급 — Apple 비공개) | L1d 128 KiB, L2 16 MiB 공유 | 이 노트의 측정 대상 |

- A55·A7x·Apple 코어의 세부(폭, ROB 크기)는 벤더 발표나 외부 분석에서 나온 값이고, 이 노트에서 직접 확인한 것은 아래 벤치마크 결과뿐이다. 면접에서는 "in-order dual-issue vs 넓은 OoO" 수준까지만 단정하는 게 안전하다.
- Don 경험과 연결: Cortex-R8은 OoO 요소가 일부 있는 실시간 코어이고, R82는 Armv8-R 64-bit 코어다. SSD 펌웨어에서 "캐시를 켜면 평균은 빨라지지만 최악 지연이 흔들린다"를 겪었다면, 그게 바로 M7(캐시) vs M4(캐시 없음) 선택의 핵심이다(9절).

### 1.6 함정

- "IPC가 높으면 좋은 코드"가 아니다. 쓸모없는 명령(스칼라로 SIMD 할 일을 하는 코드)을 많이 실행해도 IPC는 높다. ML 커널은 **FLOP/cycle 또는 MAC/cycle**로 판단한다.
- in-order 코어에서는 **컴파일러의 명령 스케줄링이 곧 성능**이다. OoO에서 괜찮던 코드가 A55나 M7에서 느리면 load-use 간격부터 의심한다.

---

## 2. Latency vs throughput — ML 커널이 accumulator를 여러 개 쓰는 이유

### 2.1 직관

FMA(fused multiply-add, `d = a × b + c`를 한 번의 반올림으로) 유닛이 4단 파이프라인이라고 하자. 한 FMA의 결과가 나오기까지 **4 cycle**(latency)이 걸리지만, 매 cycle 새 FMA를 **하나씩 넣을 수는 있다**(throughput 1/cycle/유닛). 이런 유닛이 4개 있다면 코어는 cycle당 FMA 4개를 할 수 있다.

그런데 내적을 가장 단순하게 쓰면 이렇다.

```c
float acc = 0;
for (int i = 0; i < n; i++) acc = fmaf(a[i], b[i], acc);   /* 매 FMA가 직전 acc를 기다린다 */
```

매 FMA가 **직전 FMA의 결과(acc)**를 입력으로 쓴다. 파이프라인에 4 cycle짜리 체인이 하나만 흐르므로 FMA 유닛 4개 × 4단 = 16개 칸 중 **1칸만** 쓰인다. accumulator를 `acc0 … acc15`로 나누면 16개의 서로 독립인 체인이 파이프를 꽉 채운다.

### 2.2 정의와 손계산 — Little's law

필요한 독립 체인 수는 대기열 이론의 Little's law와 같은 식이다.

```
동시에 날아가야 하는 작업 수 = latency × throughput
필요 accumulator 수        = FMA latency(cycle) × FMA 발행 폭(개/cycle)
                           = 4 × 4 = 16
```

말로 하면: "결과 하나가 나오기까지 걸리는 시간 동안 새로 넣을 수 있는 작업 수"만큼 독립 작업이 있어야 파이프가 쉬지 않는다. Don에게 익숙한 말로는 **DMA 엔진의 outstanding 요청 수**, NVMe의 **queue depth**와 같은 식이다. QD 1로는 SSD 대역폭이 안 나오는 것과 똑같이, 체인 1개로는 FMA 대역폭이 안 나온다.

체인 k개일 때 예상 처리량: `min(k ÷ 4, 4)` FMA/cycle → k = 1, 2, 4, 8, 16이면 0.25, 0.5, 1, 2, 4.

### 2.3 코드로 확인 — 스칼라 FMA 체인 1~16개

무엇을 확인하나: 스칼라 `fmadd` 명령을 체인 1/2/4/8/16개로 돌려 cycle당 FMA 수를 잰다. 클럭은 1-cycle짜리 정수 `add` 의존 체인으로 잰다(자를 먼저 만든다). 컴파일러가 체인을 합치거나 NEON으로 바꾸면 실험이 망가지므로 FMA 한 개를 inline asm으로 고정했다.

```c
/* 스칼라 FMA: 의존 체인 1개 vs 독립 체인 2/4/8/16개 — latency와 throughput 차이 */
#include <stdio.h>
#include <time.h>
#define ITERS 50000000L
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
/* 컴파일러가 체인을 합치거나 NEON으로 바꾸지 못하게 fmadd 한 개를 inline asm으로 고정 */
#define FMA(x) __asm__ volatile("fmadd %s0, %s0, %s1, %s2" : "+w"(x) : "w"(m), "w"(c))
static double ghz(void){ long x=0; double t0=now();   /* 1-cycle add 의존 체인 = 클럭 자 */
  for(long i=0;i<ITERS;i++) __asm__ volatile("add %0, %0, #1" : "+r"(x));
  return ITERS/(now()-t0)/1e9; }
static double run(int nc){
  float m=0.9999999f, c=1e-7f, a[16]; for(int k=0;k<16;k++) a[k]=(float)k;
  float a0=a[0],a1=a[1],a2=a[2],a3=a[3],a4=a[4],a5=a[5],a6=a[6],a7=a[7],
        a8=a[8],a9=a[9],a10=a[10],a11=a[11],a12=a[12],a13=a[13],a14=a[14],a15=a[15];
  double t0=now();
  for(long i=0;i<ITERS;i++){
    FMA(a0); if(nc>=2){FMA(a1);} if(nc>=4){FMA(a2);FMA(a3);}
    if(nc>=8){FMA(a4);FMA(a5);FMA(a6);FMA(a7);}
    if(nc>=16){FMA(a8);FMA(a9);FMA(a10);FMA(a11);FMA(a12);FMA(a13);FMA(a14);FMA(a15);} }
  double dt=now()-t0; volatile float sink=a0+a1+a2+a3+a4+a5+a6+a7+a8+a9+a10+a11+a12+a13+a14+a15; (void)sink;
  return dt/((double)ITERS*nc); }
int main(void){
  int nc[]={1,2,4,8,16}; double g=0;
  for(int r=0;r<5;r++){double x=ghz(); if(x>g) g=x;}
  printf("est. clock (1-cycle add chain, best of 5): %.2f GHz\n", g);
  for(int j=0;j<5;j++){ double best=1e9;
    for(int r=0;r<5;r++){double t=run(nc[j]); if(t<best) best=t;}
    printf("chains=%2d  %.3f ns/FMA = %5.2f cycles/FMA -> %.2f FMA/cycle\n",
           nc[j], best*1e9, best*1e9*g, 1.0/(best*1e9*g)); }
  return 0; }
```

```sh
cc -std=c11 -Wall -Wextra -O2 fma_chains.c -o fma_chains && ./fma_chains
```

```text
est. clock (1-cycle add chain, best of 5): 3.45 GHz
chains= 1  1.167 ns/FMA =  4.03 cycles/FMA -> 0.25 FMA/cycle
chains= 2  0.584 ns/FMA =  2.02 cycles/FMA -> 0.50 FMA/cycle
chains= 4  0.293 ns/FMA =  1.01 cycles/FMA -> 0.99 FMA/cycle
chains= 8  0.150 ns/FMA =  0.52 cycles/FMA -> 1.93 FMA/cycle
chains=16  0.075 ns/FMA =  0.26 cycles/FMA -> 3.87 FMA/cycle
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 330"><text x="20" y="22" font-size="14">독립 FMA 체인 수 vs 처리량 (Apple M2 P-core, 스칼라 fmadd, 실측)</text><line x1="70" y1="280" x2="610" y2="280" stroke="currentColor"/><line x1="70" y1="40" x2="70" y2="280" stroke="currentColor"/><line x1="66" y1="280.0" x2="610" y2="280.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="284.0" font-size="12" text-anchor="end">0</text><line x1="66" y1="226.7" x2="610" y2="226.7" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="230.7" font-size="12" text-anchor="end">1</text><line x1="66" y1="173.3" x2="610" y2="173.3" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="177.3" font-size="12" text-anchor="end">2</text><line x1="66" y1="120.0" x2="610" y2="120.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="124.0" font-size="12" text-anchor="end">3</text><line x1="66" y1="66.7" x2="610" y2="66.7" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="70.7" font-size="12" text-anchor="end">4</text><rect x="94.0" y="266.7" width="60" height="13.3" fill="#4a7bd0"/><text x="124.0" y="260.7" font-size="12" text-anchor="middle">0.25</text><text x="124.0" y="298" font-size="12" text-anchor="middle">1</text><line x1="88.0" y1="266.7" x2="160.0" y2="266.7" stroke="#e08a3c" stroke-width="2.5" stroke-dasharray="6 3"/><rect x="202.0" y="253.3" width="60" height="26.7" fill="#4a7bd0"/><text x="232.0" y="247.3" font-size="12" text-anchor="middle">0.50</text><text x="232.0" y="298" font-size="12" text-anchor="middle">2</text><line x1="196.0" y1="253.3" x2="268.0" y2="253.3" stroke="#e08a3c" stroke-width="2.5" stroke-dasharray="6 3"/><rect x="310.0" y="227.2" width="60" height="52.8" fill="#4a7bd0"/><text x="340.0" y="221.2" font-size="12" text-anchor="middle">0.99</text><text x="340.0" y="298" font-size="12" text-anchor="middle">4</text><line x1="304.0" y1="226.7" x2="376.0" y2="226.7" stroke="#e08a3c" stroke-width="2.5" stroke-dasharray="6 3"/><rect x="418.0" y="177.1" width="60" height="102.9" fill="#4a7bd0"/><text x="448.0" y="171.1" font-size="12" text-anchor="middle">1.93</text><text x="448.0" y="298" font-size="12" text-anchor="middle">8</text><line x1="412.0" y1="173.3" x2="484.0" y2="173.3" stroke="#e08a3c" stroke-width="2.5" stroke-dasharray="6 3"/><rect x="526.0" y="73.6" width="60" height="206.4" fill="#4a7bd0"/><text x="556.0" y="67.6" font-size="12" text-anchor="middle">3.87</text><text x="556.0" y="298" font-size="12" text-anchor="middle">16</text><line x1="520.0" y1="66.7" x2="592.0" y2="66.7" stroke="#e08a3c" stroke-width="2.5" stroke-dasharray="6 3"/><text x="340.0" y="320" font-size="13" text-anchor="middle">독립 accumulator(체인) 개수</text><text x="18" y="160.0" font-size="13" text-anchor="middle" transform="rotate(-90 18 160.0)">FMA / cycle</text><rect x="90" y="52" width="14" height="10" fill="#4a7bd0"/><text x="110" y="61" font-size="12">실측</text><line x1="150" y1="57" x2="176" y2="57" stroke="#e08a3c" stroke-width="2.5" stroke-dasharray="6 3"/><text x="182" y="61" font-size="12">모델: min(체인 수 ÷ latency 4, 파이프 4개)</text></svg>
```

그림 2 — 실측(파랑)이 모델 `min(k ÷ 4, 4)`(주황 점선)에 거의 그대로 붙는다. 체인 수를 두 배로 하면 처리량도 두 배. 명령어 종류도, 데이터도, 연산 수도 같은데 **의존성 구조만 바꿔서 15.5배**다.

출력에서 볼 것:

- **체인 1개 = 4.03 cycle/FMA** → 이 코어의 스칼라 FMA latency는 약 **4 cycle**.
- 체인 16개 = 0.26 cycle/FMA → cycle당 약 **3.9개**, 즉 FMA를 발행할 수 있는 파이프가 **4개**로 보인다. `4 latency × 4 파이프 = 16` 이라는 2.2절 손계산과 정확히 맞는다. (외부 분석에서도 M1/M2 P-core의 FP/SIMD 파이프를 4개로 본다 — Apple 공식 자료는 아니다.)
- 여러 번 실행하면 체인 16개 값이 3.6~3.9 사이에서 흔들렸다(배경 부하). 체인 1~8은 거의 흔들리지 않았다 — latency-bound 코드는 노이즈에 둔감하다.
- 같은 실험을 NEON 4-lane 벡터(`vfmaq_f32`)로 한 것이 D3 5.6절이다: accumulator 4 → 26, 8 → 51, 16 → 93 GFLOP/s, 24에서 포화. **스칼라든 SIMD든 같은 법칙**이다. SIMD는 한 체인이 4 lane을 한꺼번에 나를 뿐이다.

### 2.4 ML 커널에서 어떻게 쓰이나 — register blocking

GEMM 마이크로커널(가장 안쪽 루프)은 C의 작은 타일 `mr × nr`을 **레지스터에 accumulator로 통째로 들고** k 방향으로 FMA를 쏟아붓는다. 예를 들어 fp32 `mr × nr = 8 × 12` 타일이면:

```
accumulator: 8 × 12 = 96 float = NEON q 레지스터 24개 (4 lane씩)   ← AArch64 NEON 레지스터는 32개
k 한 스텝마다: A에서 8 float + B에서 12 float = 20 float 로드
             FMA 96개 (= 벡터 FMA 24개)
→ 로드한 float 1개당 FMA 4.8개.  독립 체인 24개 ≥ 필요량 16  → 파이프도 꽉 참
```

말로 하면: 타일이 크면 (1) 독립 accumulator가 많아서 FMA latency가 숨고, (2) 레지스터로 가져온 A·B 값을 여러 번 재사용해서 load 대역폭도 아낀다. 한 번에 두 문제를 푸는 것이 register blocking이다. 8×12는 설명용 예시다(BLAS 라이브러리마다 코어에 맞춰 타일 모양을 고른다).

반대로, **컴파일러는 float 누산을 알아서 여러 개로 쪼개지 않는다.** `acc += a[i] × b[i]`를 accumulator 4개로 바꾸면 덧셈 순서가 바뀌어 결과 비트가 달라질 수 있는데, C 표준은 float 덧셈의 결합법칙을 가정하지 않기 때문이다(A1 8절에서 ijk 루프가 벡터화되지 않은 이유와 같다). 그래서 (a) 손으로 accumulator를 나누거나, (b) `-ffast-math`/`-fassociative-math`를 쓰거나, (c) 정수(int8 → int32 누산)처럼 결합법칙이 성립하는 타입을 쓴다. 9.3절의 Cortex-M7 어셈블리에서 이 "누산기 1개" 현상을 실제로 본다.

### 2.5 임베디드 연결과 함정

- Cortex-M4의 FPU는 단정밀도 덧셈·곱셈이 1 cycle짜리라(Arm Cortex-M4 TRM의 FPU 명령 타이밍 표 기준) 체인 하나로도 크게 손해 보지 않는다. **latency가 짧고 폭이 1인 코어일수록 multiple accumulator의 이득이 작고, 넓고 깊은 코어(OoO 앱 코어, NEON, DSP의 VLIW)일수록 크다.**
- 함정: accumulator를 너무 늘리면 레지스터가 모자라 **spill**(레지스터 내용을 스택에 내렸다 올리기)이 생겨 오히려 느려진다. D3 5.6절에서 NEON 24개가 16개보다 약간 느렸던 것이 그 신호일 수 있다(확인하지는 않았다).
- 함정: 벤치마크 코드에서 컴파일러가 체인을 합치거나 SIMD로 바꿔 버리면 "측정한 것"이 "측정하려던 것"과 다르다. 이 절 코드에서 inline asm을 쓴 이유다. **마이크로벤치마크는 항상 디스어셈블(`objdump -d`)로 확인한다.**

---

## 3. 캐시 계층 — 재 보고, 계단을 읽는다

### 3.1 정의 — line, set, way, 그리고 miss의 세 종류

- **캐시 라인(cache line)**: 캐시가 메모리와 주고받는 최소 단위. 1바이트만 읽어도 라인 전체(이 Mac 128 B, 흔한 Cortex-A/M7은 64 B 또는 32 B)를 가져온다.
- **set-associative**: 주소의 일부 비트(index)로 set을 고르고, 그 set 안의 **way** 중 아무 칸에나 넣는다. 4-way면 set 하나에 라인 4개.
- 주소 분해: 상위 비트부터 tag · index · offset 순서. offset 비트 = log₂(라인 크기), index 비트 = log₂(set 수).
- miss의 세 종류(3C): **compulsory**(처음 접근), **capacity**(작업 집합이 캐시보다 큼), **conflict**(캐시 전체는 여유 있는데 같은 set에 몰림).

손계산 — 예시 L1: 32 KiB, 4-way, 64 B 라인 (Cortex-A 계열에서 흔한 구성).

```
set 수   = 32768 ÷ (64 × 4) = 128
offset   = log₂ 64  = 6 bit   (bit 0~5)
index    = log₂ 128 = 7 bit   (bit 6~12)
주소 0x2000_1A48 → offset = 0x08, index = (0x1A48 >> 6) & 127 = 105

같은 set으로 가는 주소 간격 = 64 × 128 = 8 KiB
→ 8 KiB 간격으로 5개 주소를 번갈아 읽으면: set 105에 5개가 경쟁, way는 4개 → 매번 miss (conflict)
```

말로 하면: **2의 거듭제곱 간격(4 KiB, 8 KiB, 16 KiB …)으로 접근하는 코드는 같은 set에 몰리기 쉽다.** 행렬 한 행의 크기가 1024 float(4 KiB)나 4096 float(16 KiB)일 때 열 방향으로 걷는 코드가 딱 이 경우다. 3.5절과 4.2절에서 이 Mac에서 실제로 본다.

### 3.2 코드로 확인 — pointer chasing으로 latency 계단 재기

무엇을 확인하나: 작업 집합(working set) 크기를 4 KiB부터 256 MiB까지 키우며 **load 한 번의 latency**를 잰다. 핵심 트릭은 **다음 주소 = 이번 load의 결과**가 되도록 연결 리스트를 만드는 것이다. 그러면 OoO 코어도 다음 load를 미리 할 수 없고(주소를 모르니까), 순서를 랜덤으로 섞었으므로 prefetcher도 못 맞힌다. 라인 하나에 노드 하나를 둬서 매 접근이 새 라인이 되게 했다.

```c
/* pointer chasing: 작업 집합 크기별 load-to-use latency (캐시 계층 계단) */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define LINE 128                       /* 노드 1개 = 캐시 라인 1개 (M2: 128 B) */
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static double chase_ns(size_t bytes){
  size_t n = bytes / LINE; char *buf = malloc(bytes); size_t *perm = malloc(n*sizeof *perm);
  for(size_t i=0;i<n;i++) perm[i]=i;
  for(size_t i=n-1;i>0;i--){ size_t j=(size_t)rand()%(i+1); size_t t=perm[i]; perm[i]=perm[j]; perm[j]=t; }
  for(size_t i=0;i<n;i++)             /* 랜덤 순서로 한 바퀴 도는 원형 리스트 */
    *(void**)(buf+perm[i]*LINE) = buf+perm[(i+1)%n]*LINE;
  void **p=(void**)(buf+perm[0]*LINE); long steps = 20000000; double best=1e9;
  for(long i=0;i<(long)n;i++) p=(void**)*p;                        /* warm-up 한 바퀴 */
  for(int r=0;r<3;r++){ double t0=now();
    for(long i=0;i<steps;i++) p=(void**)*p;                         /* 다음 주소 = 이번 load 결과 */
    double ns=(now()-t0)/steps*1e9; if(ns<best) best=ns; }
  if(p==NULL) puts("");               /* p를 쓰게 해서 루프가 지워지지 않게 */
  free(buf); free(perm); return best; }
int main(void){
  srand(1);
  for(size_t kb=4; kb<=256*1024; kb*=2)            /* 4, 6, 8, 12, 16 ... KiB */
    for(size_t k=kb; k<=kb*3/2 && k<=256*1024; k+=kb/2)
      printf("%8zu KiB  %6.2f ns/load\n", k, chase_ns(k*1024));
  return 0; }
```

첫 번째 실행의 실제 출력:

```text
       4 KiB    0.91 ns/load
       6 KiB    0.90 ns/load
       8 KiB    0.90 ns/load
      12 KiB    0.91 ns/load
      16 KiB    0.92 ns/load
      24 KiB    0.91 ns/load
      32 KiB    0.91 ns/load
      48 KiB    0.91 ns/load
      64 KiB    0.91 ns/load
      96 KiB    0.91 ns/load
     128 KiB    0.91 ns/load
     192 KiB    5.73 ns/load
     256 KiB    5.86 ns/load
     384 KiB    5.79 ns/load
     512 KiB    5.84 ns/load
     768 KiB    5.90 ns/load
    1024 KiB    5.83 ns/load
    1536 KiB    5.90 ns/load
    2048 KiB    5.94 ns/load
    3072 KiB    5.82 ns/load
    4096 KiB    5.94 ns/load
    6144 KiB    7.17 ns/load
    8192 KiB    7.65 ns/load
   12288 KiB   18.50 ns/load
   16384 KiB  103.96 ns/load
   24576 KiB  113.98 ns/load
   32768 KiB  105.99 ns/load
   49152 KiB  114.90 ns/load
   65536 KiB  104.91 ns/load
   98304 KiB  106.44 ns/load
  131072 KiB  113.00 ns/load
  196608 KiB  120.74 ns/load
  262144 KiB  120.42 ns/load
```

같은 프로그램을 세 번 돌렸을 때 경계 부근만 모으면 (ns/load):

| 작업 집합 | 1회 | 2회 | 3회 | 해석 |
|---|---|---|---|---|
| 128 KiB | 0.91 | 1.15 | 1.09 | L1 안 (배경 부하로 조금 흔들림) |
| 4 MiB | 5.94 | 6.49 | 5.91 | L2 안, 안정적 |
| 8 MiB | 7.65 | 8.59 | 7.17 | L2 안인데 조금 오름 (TLB로 추정, 3.4절) |
| 12 MiB | 18.50 | 24.57 | 12.20 | **크게 흔들림** |
| 16 MiB | 103.96 | 75.66 | 19.81 | **크게 흔들림** |
| 24 MiB | 113.98 | 99.15 | 69.39 | 대부분 DRAM |
| 256 MiB | 120.42 | 115.55 | 111.24 | DRAM |

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 360"><text x="20" y="22" font-size="14">pointer-chase latency vs 작업 집합 크기 (M2 P-core, 3회 중 최소, 로그-로그)</text><line x1="70" y1="300" x2="630" y2="300" stroke="currentColor"/><line x1="70" y1="40" x2="70" y2="300" stroke="currentColor"/><line x1="70.0" y1="300" x2="70.0" y2="305" stroke="currentColor"/><text x="70.0" y="319" font-size="12" text-anchor="middle">4K</text><line x1="210.0" y1="300" x2="210.0" y2="305" stroke="currentColor"/><text x="210.0" y="319" font-size="12" text-anchor="middle">64K</text><line x1="350.0" y1="300" x2="350.0" y2="305" stroke="currentColor"/><text x="350.0" y="319" font-size="12" text-anchor="middle">1M</text><line x1="490.0" y1="300" x2="490.0" y2="305" stroke="currentColor"/><text x="490.0" y="319" font-size="12" text-anchor="middle">16M</text><line x1="630.0" y1="300" x2="630.0" y2="305" stroke="currentColor"/><text x="630.0" y="319" font-size="12" text-anchor="middle">256M</text><line x1="70" y1="269.9" x2="630" y2="269.9" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="273.9" font-size="12" text-anchor="end">1 ns</text><line x1="70" y1="170.0" x2="630" y2="170.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="174.0" font-size="12" text-anchor="end">10 ns</text><line x1="70" y1="70.1" x2="630" y2="70.1" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="74.1" font-size="12" text-anchor="end">100 ns</text><line x1="245.0" y1="40" x2="245.0" y2="300" stroke="#e08a3c" stroke-dasharray="5 4"/><text x="241.0" y="52" font-size="12" text-anchor="end">L1d 128 KiB</text><line x1="490.0" y1="40" x2="490.0" y2="300" stroke="#e08a3c" stroke-dasharray="5 4"/><text x="486.0" y="52" font-size="12" text-anchor="end">L2 16 MiB</text><polyline points="70.0,274.0 90.5,274.5 105.0,274.5 125.5,274.0 140.0,273.5 160.5,274.0 175.0,274.0 195.5,274.0 210.0,274.0 230.5,274.0 245.0,274.0 265.5,194.2 280.0,193.2 300.5,193.7 315.0,194.5 335.5,193.3 350.0,193.4 370.5,192.9 385.0,192.9 405.5,194.0 420.0,192.8 440.5,188.8 455.0,184.4 475.5,161.4 490.0,140.3 510.5,85.9 525.0,71.4 545.5,69.7 560.0,68.0 580.5,67.4 595.0,66.5 615.5,66.3 630.0,65.5" fill="none" stroke="#4a7bd0" stroke-width="2.5"/><circle cx="70.0" cy="274.0" r="2.5" fill="#4a7bd0"/><circle cx="90.5" cy="274.5" r="2.5" fill="#4a7bd0"/><circle cx="105.0" cy="274.5" r="2.5" fill="#4a7bd0"/><circle cx="125.5" cy="274.0" r="2.5" fill="#4a7bd0"/><circle cx="140.0" cy="273.5" r="2.5" fill="#4a7bd0"/><circle cx="160.5" cy="274.0" r="2.5" fill="#4a7bd0"/><circle cx="175.0" cy="274.0" r="2.5" fill="#4a7bd0"/><circle cx="195.5" cy="274.0" r="2.5" fill="#4a7bd0"/><circle cx="210.0" cy="274.0" r="2.5" fill="#4a7bd0"/><circle cx="230.5" cy="274.0" r="2.5" fill="#4a7bd0"/><circle cx="245.0" cy="274.0" r="2.5" fill="#4a7bd0"/><circle cx="265.5" cy="194.2" r="2.5" fill="#4a7bd0"/><circle cx="280.0" cy="193.2" r="2.5" fill="#4a7bd0"/><circle cx="300.5" cy="193.7" r="2.5" fill="#4a7bd0"/><circle cx="315.0" cy="194.5" r="2.5" fill="#4a7bd0"/><circle cx="335.5" cy="193.3" r="2.5" fill="#4a7bd0"/><circle cx="350.0" cy="193.4" r="2.5" fill="#4a7bd0"/><circle cx="370.5" cy="192.9" r="2.5" fill="#4a7bd0"/><circle cx="385.0" cy="192.9" r="2.5" fill="#4a7bd0"/><circle cx="405.5" cy="194.0" r="2.5" fill="#4a7bd0"/><circle cx="420.0" cy="192.8" r="2.5" fill="#4a7bd0"/><circle cx="440.5" cy="188.8" r="2.5" fill="#4a7bd0"/><circle cx="455.0" cy="184.4" r="2.5" fill="#4a7bd0"/><circle cx="475.5" cy="161.4" r="2.5" fill="#4a7bd0"/><circle cx="490.0" cy="140.3" r="2.5" fill="#4a7bd0"/><circle cx="510.5" cy="85.9" r="2.5" fill="#4a7bd0"/><circle cx="525.0" cy="71.4" r="2.5" fill="#4a7bd0"/><circle cx="545.5" cy="69.7" r="2.5" fill="#4a7bd0"/><circle cx="560.0" cy="68.0" r="2.5" fill="#4a7bd0"/><circle cx="580.5" cy="67.4" r="2.5" fill="#4a7bd0"/><circle cx="595.0" cy="66.5" r="2.5" fill="#4a7bd0"/><circle cx="615.5" cy="66.3" r="2.5" fill="#4a7bd0"/><circle cx="630.0" cy="65.5" r="2.5" fill="#4a7bd0"/><text x="140.0" y="292.0" font-size="12" text-anchor="middle">약 0.9 ns ≈ 3 cycle</text><text x="350.0" y="211.6" font-size="12" text-anchor="middle">약 5.8 ns ≈ 20 cycle</text><text x="560.0" y="58.0" font-size="12" text-anchor="middle">DRAM 약 105~111 ns</text><text x="350.0" y="340" font-size="13" text-anchor="middle">작업 집합 크기 (log)</text><text x="18" y="170.0" font-size="13" text-anchor="middle" transform="rotate(-90 18 170.0)">load 1개당 ns (log)</text></svg>
```

그림 3 — 세 번 실행 중 최소값으로 그린 latency 계단(로그-로그). 평평한 단 = 한 캐시 레벨 안, 절벽 = 다음 레벨로 넘어감. 점선은 `sysctl`이 알려 준 L1d(128 KiB)와 L2(16 MiB) 크기다.

출력에서 볼 것:

- **L1: 약 0.91 ns ≈ 3.1 cycle**(3.45 GHz 기준). load-to-use latency 3 cycle짜리 L1이다. 128 KiB에서 딱 끊긴다 — `sysctl` 값과 정확히 일치.
- **L2: 약 5.8 ns ≈ 20 cycle**. 192 KiB부터 4 MiB까지 평평하다.
- **DRAM: 약 105~120 ns ≈ 360~410 cycle**. L1의 100배 이상이다. 이 차이가 "데이터가 어디 있느냐"가 FMA 개수보다 중요한 이유다.
- **12~24 MiB가 실행마다 크게 흔들린다.** L2 16 MiB는 P-core 4개가 **공유**하므로 동시에 돌던 다른 작업이 L2 일부를 쓰고 있었을 것이다. 또 M2에는 L2 뒤에 SoC 전체가 공유하는 system level cache(SLC)가 있다고 알려져 있는데(Apple이 세부를 공개하지 않음), 그 효과가 섞였을 수도 있다. 여기서는 원인을 분리하지 않았다. 교훈: **공유 캐시 경계 근처의 측정은 다른 작업에 크게 흔들린다.** 실제 기기에서도 NPU·GPU·다른 코어가 같은 L3/SLC를 쓴다.
- 4 MiB → 8 MiB에서 L2 안인데 5.9 → 7.2~8.6 ns로 조금 오른다. 페이지 16 KiB × 수백 개를 넘으면서 TLB miss가 섞이기 시작한 것으로 추정한다(3.4절 실험이 이 해석을 뒷받침한다).

Don 경험과 연결: FPGA bring-up 때 "SRAM 접근은 몇 cycle, DDR은 몇 cycle"을 로직 분석기나 트레이스로 쟀던 것과 같은 일을 소프트웨어만으로 한 것이다. 새 SoC를 받으면 데이터시트의 캐시 크기를 믿기 전에 이 계단부터 그려 보자.

### 3.3 코드로 확인 — stride로 캐시 라인 크기 보기

무엇을 확인하나: 256 MiB 배열에서 `s` 바이트마다 1바이트씩 증가시키는 **총 시간**을 잰다. 라인이 128 B라면, stride가 128 B 이하일 때는 어차피 **모든 라인**을 가져와야 하므로 총 시간이 비슷하고, 128 B를 넘으면 가져오는 라인 수가 절반씩 줄어서 시간도 절반씩 줄어야 한다.

```c
/* stride 실험: 256 MiB 배열에서 s 바이트마다 1바이트씩 증가 — 캐시 라인 크기가 보인다 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define BYTES (256u<<20)
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(void){
  unsigned char *a = malloc(BYTES); memset(a, 1, BYTES);
  printf("stride(B)  accesses   total(ms)  ns/access\n");
  for(size_t s=4; s<=16384; s*=2){
    double best=1e9; size_t cnt=BYTES/s;
    for(int r=0;r<5;r++){
      double t0=now();
      for(size_t i=0;i<BYTES;i+=s) a[i]++;          /* 읽고 쓰기: 라인 전체를 가져와야 함 */
      double dt=now()-t0; if(dt<best) best=dt; }
    printf("%8zu  %9zu  %9.2f  %9.2f\n", s, cnt, best*1e3, best*1e9/cnt);
  }
  long sum=0; for(size_t i=0;i<BYTES;i+=4096) sum+=a[i]; printf("checksum %ld\n", sum);
  free(a); return 0; }
```

```text
stride(B)  accesses   total(ms)  ns/access
       4   67108864      29.51       0.44
       8   33554432      13.35       0.40
      16   16777216       7.71       0.46
      32    8388608       7.18       0.86
      64    4194304       7.13       1.70
     128    2097152       8.65       4.13
     256    1048576       4.67       4.45
     512     524288       2.43       4.63
    1024     262144       1.23       4.70
    2048     131072       0.60       4.60
    4096      65536       0.43       6.59
    8192      32768       0.25       7.57
   16384      16384       0.14       8.36
checksum 3915776
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 350"><text x="20" y="22" font-size="14">256 MiB 배열을 stride s로 한 번 훑는 총 시간 (M2, 5회 중 최소, 로그-로그)</text><line x1="70" y1="290" x2="630" y2="290" stroke="currentColor"/><line x1="70" y1="40" x2="70" y2="290" stroke="currentColor"/><line x1="70.0" y1="290" x2="70.0" y2="295" stroke="currentColor"/><text x="70.0" y="309" font-size="12" text-anchor="middle">4</text><line x1="116.7" y1="290" x2="116.7" y2="295" stroke="currentColor"/><text x="116.7" y="309" font-size="12" text-anchor="middle">8</text><line x1="163.3" y1="290" x2="163.3" y2="295" stroke="currentColor"/><text x="163.3" y="309" font-size="12" text-anchor="middle">16</text><line x1="210.0" y1="290" x2="210.0" y2="295" stroke="currentColor"/><text x="210.0" y="309" font-size="12" text-anchor="middle">32</text><line x1="256.7" y1="290" x2="256.7" y2="295" stroke="currentColor"/><text x="256.7" y="309" font-size="12" text-anchor="middle">64</text><line x1="303.3" y1="290" x2="303.3" y2="295" stroke="currentColor"/><text x="303.3" y="309" font-size="12" text-anchor="middle">128</text><line x1="350.0" y1="290" x2="350.0" y2="295" stroke="currentColor"/><text x="350.0" y="309" font-size="12" text-anchor="middle">256</text><line x1="396.7" y1="290" x2="396.7" y2="295" stroke="currentColor"/><text x="396.7" y="309" font-size="12" text-anchor="middle">512</text><line x1="443.3" y1="290" x2="443.3" y2="295" stroke="currentColor"/><text x="443.3" y="309" font-size="12" text-anchor="middle">1K</text><line x1="490.0" y1="290" x2="490.0" y2="295" stroke="currentColor"/><text x="490.0" y="309" font-size="12" text-anchor="middle">2K</text><line x1="536.7" y1="290" x2="536.7" y2="295" stroke="currentColor"/><text x="536.7" y="309" font-size="12" text-anchor="middle">4K</text><line x1="583.3" y1="290" x2="583.3" y2="295" stroke="currentColor"/><text x="583.3" y="309" font-size="12" text-anchor="middle">8K</text><line x1="630.0" y1="290" x2="630.0" y2="295" stroke="currentColor"/><text x="630.0" y="309" font-size="12" text-anchor="middle">16K</text><line x1="70" y1="290.0" x2="630" y2="290.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="294.0" font-size="12" text-anchor="end">0.1 ms</text><line x1="70" y1="193.9" x2="630" y2="193.9" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="197.9" font-size="12" text-anchor="end">1 ms</text><line x1="70" y1="97.8" x2="630" y2="97.8" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="101.8" font-size="12" text-anchor="end">10 ms</text><rect x="163.3" y="40" width="140.0" height="250" fill="#3f9a6b" fill-opacity="0.12"/><text x="233.3" y="54" font-size="12" text-anchor="middle">평탄: 어차피 모든 라인을 가져온다</text><text x="443.3" y="148.1" font-size="12">128 B를 넘으면 2배마다 절반</text><text x="76.0" y="72.4" font-size="12">명령어 수가 병목</text><line x1="303.3" y1="40" x2="303.3" y2="290" stroke="#e08a3c" stroke-dasharray="5 4"/><text x="307.3" y="282" font-size="12">128 B (hw.cachelinesize)</text><polyline points="70.0,55.9 116.7,86.0 163.3,109.2 210.0,111.7 256.7,112.5 303.3,103.9 350.0,129.6 396.7,156.9 443.3,185.3 490.0,215.2 536.7,229.1 583.3,251.8 630.0,276.0" fill="none" stroke="#4a7bd0" stroke-width="2.5"/><circle cx="70.0" cy="55.9" r="3" fill="#4a7bd0"/><circle cx="116.7" cy="86.0" r="3" fill="#4a7bd0"/><circle cx="163.3" cy="109.2" r="3" fill="#4a7bd0"/><circle cx="210.0" cy="111.7" r="3" fill="#4a7bd0"/><circle cx="256.7" cy="112.5" r="3" fill="#4a7bd0"/><circle cx="303.3" cy="103.9" r="3" fill="#4a7bd0"/><circle cx="350.0" cy="129.6" r="3" fill="#4a7bd0"/><circle cx="396.7" cy="156.9" r="3" fill="#4a7bd0"/><circle cx="443.3" cy="185.3" r="3" fill="#4a7bd0"/><circle cx="490.0" cy="215.2" r="3" fill="#4a7bd0"/><circle cx="536.7" cy="229.1" r="3" fill="#4a7bd0"/><circle cx="583.3" cy="251.8" r="3" fill="#4a7bd0"/><circle cx="630.0" cy="276.0" r="3" fill="#4a7bd0"/><text x="350.0" y="330" font-size="13" text-anchor="middle">stride s (bytes, log)</text><text x="18" y="165.0" font-size="13" text-anchor="middle" transform="rotate(-90 18 165.0)">총 시간 (log)</text></svg>
```

그림 4 — 두 번 실행 중 최소값의 총 시간(로그-로그). 초록 띠(16~128 B)는 평평하고, 128 B를 넘으면 stride가 두 배일 때마다 시간이 절반이 된다. 꺾이는 곳이 캐시 라인 크기다.

출력에서 볼 것:

- **4~8 B**: 접근 횟수가 많아서 **명령어 실행이 병목**이다(접근 1개당 약 0.4 ns ≈ 1.4 cycle). 메모리는 아직 여유가 있다.
- **16~128 B: 총 시간 약 7~9 ms로 평평.** 모든 라인을 한 번씩 가져오는 비용이다. 256 MiB를 읽고 쓰는 데 약 7 ms → 약 70 GB/s 상당(읽기+쓰기). D3 5.3절의 DRAM 대역폭과 같은 자릿수다.
- **256 B부터 절반씩**: 4.67 → 2.43 → 1.23 → 0.60 ms. 라인의 절반만 건드리면 시간도 절반. **128 B 라인**이라는 `sysctl hw.cachelinesize`와 일치한다. 만약 라인이 64 B였다면 128 B stride에서 이미 절반으로 줄었어야 한다.
- 접근 1개당 시간이 128~2048 B에서 약 4.5 ns로, pointer chase의 DRAM latency(약 110 ns)보다 **25배 짧다.** 주소가 규칙적이라서 하드웨어 prefetcher가 미리 가져오고, OoO 코어가 여러 miss를 동시에 날리기 때문이다(5, 6절).
- 4 KiB 이상에서 접근당 시간이 6.6 → 8.4 ns로 다시 오른다. 매 접근이 새 4 KiB 구간에 들어가는 지점인데, prefetcher가 이런 큰 stride를 덜 따라가거나 TLB 부담이 커진 것으로 추정한다. 이 실험만으로는 둘을 구분할 수 없다.

ML 연결: **캐시 라인 안의 바이트를 다 쓰지 않으면 그만큼 대역폭을 버린다.** NCHW 텐서에서 채널 방향으로 걷기(stride = H·W·원소크기), 행렬의 열 방향 접근, AoS(struct 배열)에서 필드 하나만 읽기가 모두 이 그래프의 오른쪽 영역에서 "라인 하나에 쓸모 있는 바이트 몇 개"를 가져오는 패턴이다.

### 3.4 코드로 확인 — TLB와 conflict miss

**TLB**(translation lookaside buffer)는 가상 주소 → 물리 주소 변환 결과를 기억하는 작은 캐시다. 페이지마다 항목 하나를 쓴다. TLB miss가 나면 하드웨어가 page table을 걸어가서(page walk) 변환을 찾는데, 그 page table 자체도 메모리(캐시)에 있다. Don의 bare-metal 펌웨어는 MMU를 끄거나 flat mapping을 쓰는 경우가 많아서 이 비용을 못 봤을 수 있다. Linux/Android 위의 ML 런타임은 항상 이 비용을 낸다.

무엇을 확인하나: 캐시 라인 **개수는 같게** 두고, (a) 라인을 붙여 두기, (b) 16 KiB 페이지마다 라인 1개씩 흩어 두기(페이지 안 위치는 `i % 128`로 분산), (c) 페이지마다 1개씩이되 **페이지 안 위치를 항상 0**으로. (a)와 (b)의 차이는 TLB, (b)와 (c)의 차이는 conflict miss다.

```c
/* 같은 개수의 캐시 라인을 (a) 붙여서 (b) 16 KiB 페이지마다 1개씩 흩어서 chase — 차이 = TLB(+α) */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define LINE 128
#define PAGE 16384                                  /* macOS arm64 페이지 = 16 KiB */
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
/* mode 0: 붙임, 1: 페이지당 1라인(오프셋 i%128로 분산), 2: 페이지당 1라인(오프셋 항상 0) */
static double chase(size_t n, int mode){
  size_t step = mode ? PAGE : LINE; char *buf = malloc(n*step + PAGE);
  size_t *perm = malloc(n*sizeof *perm); for(size_t i=0;i<n;i++) perm[i]=i;
  for(size_t i=n-1;i>0;i--){ size_t j=(size_t)rand()%(i+1); size_t t=perm[i]; perm[i]=perm[j]; perm[j]=t; }
  #define ADDR(k) (buf + (k)*step + (mode==1 ? ((k)%(PAGE/LINE))*LINE : 0))
  for(size_t i=0;i<n;i++) *(void**)ADDR(perm[i]) = ADDR(perm[(i+1)%n]);
  void **p=(void**)ADDR(perm[0]); double best=1e9; long steps=10000000;
  for(size_t i=0;i<n;i++) p=(void**)*p;
  for(int r=0;r<3;r++){ double t0=now(); for(long i=0;i<steps;i++) p=(void**)*p;
    double ns=(now()-t0)/steps*1e9; if(ns<best) best=ns; }
  if(!p) puts(""); free(buf); free(perm); return best; }
int main(void){
  srand(2);
  printf("   lines  lines(KiB)  packed  1/page(spread)  1/page(same offset)  [ns/load]\n");
  for(size_t n=64;n<=16384;n*=2)
    printf("%8zu  %9zu  %6.2f  %14.2f  %19.2f\n", n, n*LINE/1024, chase(n,0), chase(n,1), chase(n,2));
  return 0; }
```

```text
   lines  lines(KiB)  packed  1/page(spread)  1/page(same offset)  [ns/load]
      64          8    0.94            0.93                 5.29
     128         16    0.94            0.94                 5.20
     256         32    0.91            0.91                 5.25
     512         64    0.91            2.74                 7.90
    1024        128    0.92            2.79                23.14
    2048        256    5.73            7.93                37.35
    4096        512    5.89           17.02                44.79
    8192       1024    5.98           17.40                48.11
   16384       2048    5.79           16.98                79.86
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 660 360"><text x="20" y="22" font-size="14">같은 라인 수를 붙여 두기 vs 16 KiB 페이지마다 1개씩 (M2, pointer chase, 로그-로그)</text><line x1="70" y1="290" x2="630" y2="290" stroke="currentColor"/><line x1="70" y1="40" x2="70" y2="290" stroke="currentColor"/><line x1="70.0" y1="290" x2="70.0" y2="295" stroke="currentColor"/><text x="70.0" y="309" font-size="12" text-anchor="middle">64</text><line x1="140.0" y1="290" x2="140.0" y2="295" stroke="currentColor"/><text x="140.0" y="309" font-size="12" text-anchor="middle">128</text><line x1="210.0" y1="290" x2="210.0" y2="295" stroke="currentColor"/><text x="210.0" y="309" font-size="12" text-anchor="middle">256</text><line x1="280.0" y1="290" x2="280.0" y2="295" stroke="currentColor"/><text x="280.0" y="309" font-size="12" text-anchor="middle">512</text><line x1="350.0" y1="290" x2="350.0" y2="295" stroke="currentColor"/><text x="350.0" y="309" font-size="12" text-anchor="middle">1024</text><line x1="420.0" y1="290" x2="420.0" y2="295" stroke="currentColor"/><text x="420.0" y="309" font-size="12" text-anchor="middle">2048</text><line x1="490.0" y1="290" x2="490.0" y2="295" stroke="currentColor"/><text x="490.0" y="309" font-size="12" text-anchor="middle">4096</text><line x1="560.0" y1="290" x2="560.0" y2="295" stroke="currentColor"/><text x="560.0" y="309" font-size="12" text-anchor="middle">8192</text><line x1="630.0" y1="290" x2="630.0" y2="295" stroke="currentColor"/><text x="630.0" y="309" font-size="12" text-anchor="middle">16384</text><line x1="70" y1="257.3" x2="630" y2="257.3" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="261.3" font-size="12" text-anchor="end">1 ns</text><line x1="70" y1="148.6" x2="630" y2="148.6" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="152.6" font-size="12" text-anchor="end">10 ns</text><line x1="70" y1="40.0" x2="630" y2="40.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="44.0" font-size="12" text-anchor="end">100 ns</text><polyline points="70.0,261.7 140.0,261.7 210.0,261.7 280.0,261.7 350.0,261.7 420.0,174.9 490.0,173.6 560.0,173.2 630.0,174.4" fill="none" stroke="#4a7bd0" stroke-width="2.5"/><circle cx="70.0" cy="261.7" r="3" fill="#4a7bd0"/><circle cx="140.0" cy="261.7" r="3" fill="#4a7bd0"/><circle cx="210.0" cy="261.7" r="3" fill="#4a7bd0"/><circle cx="280.0" cy="261.7" r="3" fill="#4a7bd0"/><circle cx="350.0" cy="261.7" r="3" fill="#4a7bd0"/><circle cx="420.0" cy="174.9" r="3" fill="#4a7bd0"/><circle cx="490.0" cy="173.6" r="3" fill="#4a7bd0"/><circle cx="560.0" cy="173.2" r="3" fill="#4a7bd0"/><circle cx="630.0" cy="174.4" r="3" fill="#4a7bd0"/><line x1="90" y1="56" x2="114" y2="56" stroke="#4a7bd0" stroke-width="3"/><text x="120" y="60" font-size="12">붙여 둠 (라인 연속)</text><polyline points="70.0,260.7 140.0,261.7 210.0,261.7 280.0,209.7 350.0,209.7 420.0,161.3 490.0,124.4 560.0,124.4 630.0,123.7" fill="none" stroke="#e08a3c" stroke-width="2.5"/><circle cx="70.0" cy="260.7" r="3" fill="#e08a3c"/><circle cx="140.0" cy="261.7" r="3" fill="#e08a3c"/><circle cx="210.0" cy="261.7" r="3" fill="#e08a3c"/><circle cx="280.0" cy="209.7" r="3" fill="#e08a3c"/><circle cx="350.0" cy="209.7" r="3" fill="#e08a3c"/><circle cx="420.0" cy="161.3" r="3" fill="#e08a3c"/><circle cx="490.0" cy="124.4" r="3" fill="#e08a3c"/><circle cx="560.0" cy="124.4" r="3" fill="#e08a3c"/><circle cx="630.0" cy="123.7" r="3" fill="#e08a3c"/><line x1="90" y1="74" x2="114" y2="74" stroke="#e08a3c" stroke-width="3"/><text x="120" y="78" font-size="12">페이지당 1개, 오프셋 분산</text><polyline points="70.0,178.7 140.0,179.5 210.0,179.1 280.0,159.8 350.0,109.1 420.0,87.7 490.0,77.9 560.0,75.4 630.0,59.1" fill="none" stroke="#d0564a" stroke-width="2.5"/><circle cx="70.0" cy="178.7" r="3" fill="#d0564a"/><circle cx="140.0" cy="179.5" r="3" fill="#d0564a"/><circle cx="210.0" cy="179.1" r="3" fill="#d0564a"/><circle cx="280.0" cy="159.8" r="3" fill="#d0564a"/><circle cx="350.0" cy="109.1" r="3" fill="#d0564a"/><circle cx="420.0" cy="87.7" r="3" fill="#d0564a"/><circle cx="490.0" cy="77.9" r="3" fill="#d0564a"/><circle cx="560.0" cy="75.4" r="3" fill="#d0564a"/><circle cx="630.0" cy="59.1" r="3" fill="#d0564a"/><line x1="90" y1="92" x2="114" y2="92" stroke="#d0564a" stroke-width="3"/><text x="120" y="96" font-size="12">페이지당 1개, 오프셋 같음(0)</text><text x="350.0" y="330" font-size="13" text-anchor="middle">건드리는 캐시 라인 수 = 페이지 수 (log)</text><text x="18" y="165.0" font-size="13" text-anchor="middle" transform="rotate(-90 18 165.0)">load 1개당 ns (log)</text></svg>
```

그림 5 — 두 번 실행 중 최소값. 파랑(붙여 둠)은 3.2절 계단과 같다. 주황(페이지마다 1개)은 같은 양의 데이터인데 페이지 수가 많아지면 계단이 두 번 더 생긴다. 빨강(페이지 안 위치가 모두 같음)은 라인 64개(8 KiB)만 써도 이미 L1에 안 들어간다.

출력에서 볼 것:

- **주황 vs 파랑 (TLB)**: 페이지 256개(4 MiB 범위)까지는 똑같이 0.9 ns. 512~1024 페이지에서 약 **+1.8 ns(≈ 6 cycle)**, 4096 페이지(64 MiB 범위) 이상에서 약 **17 ns**로 붙여 둔 경우(5.9 ns)보다 약 11 ns 더 걸린다. 해석(추정): 1단 TLB가 페이지 수백 개를 커버하고, 넘치면 2단 TLB에서 몇 cycle에 찾고, 그것도 넘치면 page walk를 한다. 정확한 TLB 항목 수는 Apple이 공개하지 않았고, 이 실험의 계단 위치로 "수백 개 / 수천 개" 정도의 자릿수만 말할 수 있다.
- **빨강 vs 주황 (conflict)**: 라인 64개 = 8 KiB로 L1(128 KiB)의 1/16인데도 5.3 ns(L2 수준)다. 16 KiB 간격 주소는 **모두 같은 L1 set**으로 가서 way 수만큼만 들어가기 때문이다(3.1절 손계산). 개수가 늘면 L2에서도 set이 겹쳐 23~80 ns까지 나빠진다.
- 이 Mac의 L1 set 수·way 수는 공개되지 않아서 "16 KiB 간격이 같은 set"이라는 부분은 결과에서 거꾸로 추론한 것이다. 페이지 크기(16 KiB)가 L1 한 way의 크기 이상이면 이렇게 되는데, 이것은 흔한 설계(가상 주소로 index를 뽑아도 aliasing이 없게 하는 조건)다.

ML 연결:

- **큰 모델 weight를 여기저기 조금씩 읽는 커널**(embedding lookup, sparse weight, LLM KV 캐시를 페이지 단위로 관리하는 paged attention)은 TLB를 많이 쓴다. 그래서 서버 추론 엔진은 **huge page**(2 MiB 등)를 쓰기도 한다. Android/Linux 기기에서도 옵션이 있을 수 있지만, 기기·커널 설정에 달려 있다.
- **행 pitch가 2의 거듭제곱인 행렬**(예: 1024 × 4 B = 4 KiB, 4096 × 4 B = 16 KiB)을 열 방향으로 읽으면 conflict miss가 난다. 해결책은 **행 끝에 padding**을 넣어 pitch를 2의 거듭제곱에서 비키는 것이다. 4.2절에서 이것 하나로 전치가 2배 빨라지는 것을 본다.

---

## 4. 지역성(locality)과 ML 커널 — 루프 순서, tiling, im2col

### 4.1 두 종류의 지역성

- **공간 지역성(spatial locality)**: 방금 읽은 주소 근처를 곧 읽는다 → 한 캐시 라인의 바이트를 다 쓴다. row-major 배열을 행 방향으로 훑기.
- **시간 지역성(temporal locality)**: 방금 읽은 데이터를 곧 다시 읽는다 → 캐시에서 여러 번 재사용. GEMM에서 A의 한 행을 B의 여러 열과 곱하기.

A1 8절 복습(다시 가르치지 않는다): 256×256 행렬곱을 ijk → ikj로 루프 순서만 바꾸니 약 12배 빨라졌다. 그중 약 2.7배는 캐시(열 방향 점프 → 행 방향 연속), 나머지는 연속 접근 덕분에 가능해진 SIMD 벡터화였다. 이 절은 그 다음 단계인 **tiling**을 다룬다.

### 4.2 코드로 확인 — 전치(transpose): tiling과 padding

전치는 루프 순서를 어떻게 바꿔도 **읽기나 쓰기 중 한쪽은 반드시 열 방향**이다. 그래서 루프 순서만으로는 못 고치고 tiling이 필요한 가장 단순한 예다. NCHW ↔ NHWC 변환, attention의 `Kᵀ`, im2col이 모두 전치의 친척이다.

무엇을 확인하나: 4096 × 4096 float(64 MiB) 전치를 naive와 T×T 타일로 비교한다. 행 pitch(LD)를 4096(16 KiB, 2의 거듭제곱)과 4128(16 KiB + 128 B)로 바꿔 conflict miss의 영향도 본다.

```c
/* 전치(transpose): naive vs 타일(blocking) — 한쪽이 반드시 열 방향이 되는 연산에서 tiling의 효과 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define N 4096
#ifndef LD
#define LD N        /* 행 pitch(원소 수). -DLD=4128 이면 행마다 128 B 패딩 */
#endif
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static void naive(const float *A, float *B){
  for(int i=0;i<N;i++) for(int j=0;j<N;j++) B[(size_t)j*LD+i] = A[(size_t)i*LD+j]; }   /* B 쓰기가 16 KiB 점프 */
static void tiled(const float *A, float *B, int T){
  for(int ii=0;ii<N;ii+=T) for(int jj=0;jj<N;jj+=T)          /* T×T 타일 하나를 캐시 안에서 끝낸다 */
    for(int i=ii;i<ii+T;i++) for(int j=jj;j<jj+T;j++) B[(size_t)j*LD+i] = A[(size_t)i*LD+j]; }
int main(void){
  float *A=malloc(sizeof(float)*N*LD), *B=malloc(sizeof(float)*N*LD);
  for(size_t k=0;k<(size_t)N*LD;k++) A[k]=(float)(k%1000);
  double best=1e9; for(int r=0;r<3;r++){double t0=now(); naive(A,B); double d=now()-t0; if(d<best)best=d;}
  printf("LD=%d\n", LD);
  printf("naive      %7.1f ms  %5.1f GB/s  B[7*LD+3]=%.0f\n", best*1e3, 2.0*N*N*4/best/1e9, B[7*LD+3]);
  int Ts[]={8,16,32,64,128};
  for(int t=0;t<5;t++){ best=1e9;
    for(int r=0;r<3;r++){double t0=now(); tiled(A,B,Ts[t]); double d=now()-t0; if(d<best)best=d;}
    printf("tile %3d   %7.1f ms  %5.1f GB/s  B[7*LD+3]=%.0f\n", Ts[t], best*1e3, 2.0*N*N*4/best/1e9, B[7*LD+3]); }
  free(A); free(B); return 0; }
```

```sh
cc -std=c11 -Wall -Wextra -O2 transpose.c -o transpose && ./transpose
cc -std=c11 -Wall -Wextra -O2 -DLD=4128 transpose.c -o transpose_pad && ./transpose_pad
```

```text
LD=4096
naive        100.4 ms    1.3 GB/s  B[7*LD+3]=295
tile   8      23.6 ms    5.7 GB/s  B[7*LD+3]=295
tile  16      70.9 ms    1.9 GB/s  B[7*LD+3]=295
tile  32      68.4 ms    2.0 GB/s  B[7*LD+3]=295
tile  64      69.0 ms    1.9 GB/s  B[7*LD+3]=295
tile 128      89.2 ms    1.5 GB/s  B[7*LD+3]=295
LD=4128
naive         48.7 ms    2.8 GB/s  B[7*LD+3]=391
tile   8      18.9 ms    7.1 GB/s  B[7*LD+3]=391
tile  16      14.2 ms    9.4 GB/s  B[7*LD+3]=391
tile  32      11.2 ms   12.0 GB/s  B[7*LD+3]=391
tile  64      11.3 ms   11.9 GB/s  B[7*LD+3]=391
tile 128      12.3 ms   10.9 GB/s  B[7*LD+3]=391
```

(`B[7·LD+3]` 값이 두 빌드에서 다른 것은 A의 초기값을 `k % 1000`으로 채워서 LD가 바뀌면 같은 위치의 값도 바뀌기 때문이다. 같은 빌드 안에서 모든 버전이 같은 값을 내는지가 검증 포인트다.) 두 번 실행해서 차이는 수 % 이내였다.

출력에서 볼 것:

- **padding 없는 LD = 4096에서는 타일 16~64가 거의 도움이 안 된다**(100 → 약 70 ms). 타일 T개의 행은 16 KiB 간격이라 3.4절 빨간 선처럼 **같은 L1 set**에 몰린다. T = 16이면 한 set에 라인 16개가 경쟁하니 way 수를 넘어서 타일이 캐시에 머물지 못한다. T = 8만 겨우 들어가서 4배 빨라졌다(23.6 ms).
- **행마다 128 B만 padding하면(LD = 4128) 모든 버전이 빨라진다.** naive조차 2배, 타일 32는 **11.2 ms — 원래 naive 대비 약 9배.**
- 타일이 너무 크면(128) 다시 느려진다. 타일 두 개(A 쪽 T×T, B 쪽 T×T)가 L1에 같이 들어가야 하는데, 128 × 128 × 4 B × 2 = 128 KiB로 L1 크기와 같아져서 넘친다.
- 12 GB/s는 DRAM 대역폭(약 60 GB/s)보다 한참 낮다. 전치는 SIMD 레지스터 안에서 4×4 블록을 섞는 명령(NEON `TRN`/`ZIP` 등)까지 써야 더 빨라진다 — 그건 E2의 영역이다.

### 4.3 GEMM의 blocking 계층 — 한 장으로

GEMM(`C[M×N] = A[M×K] × B[K×N]`)을 빠르게 만드는 표준 구조(Goto/BLIS 방식)는 **메모리 계층마다 한 번씩 타일링**한다.

```
DRAM ─── B를 kc × nc 블록으로 잘라 L2(또는 L3)에 올림      ← 수 MiB
  │
  └─ L2 ─── A를 mc × kc 블록으로 잘라 L2/L1에 올림         ← 수백 KiB
        │
        └─ L1 ─── B의 kc × nr 패널, A의 mr × kc 패널       ← 수십 KiB
              │
              └─ 레지스터 ─── C의 mr × nr 타일 = accumulator (2.4절)
                              k 방향으로 FMA를 kc번 반복
```

손계산 — 위 8 × 12 마이크로커널에서 `kc = 256`, fp32:

```
L1에 머무를 것:  A 패널 8 × 256 × 4 B = 8 KiB,  B 패널 256 × 12 × 4 B = 12 KiB  → 합 20 KiB
M2 L1d 128 KiB, Cortex-A55급 32~64 KiB 모두에 여유 있게 들어감
이 패널 쌍으로 하는 FMA = 8 × 12 × 256 = 24,576개,  L1에서 읽는 양 = 20 KiB
→ L1 → 레지스터 intensity = 24,576 × 2 FLOP ÷ 20,480 B = 2.4 FLOP/B
```

말로 하면: **바깥 레벨일수록 큰 블록을 한 번 가져와 오래 재사용하고, 안쪽으로 갈수록 작은 블록을 더 자주 재사용한다.** D3 6절의 hierarchical roofline을 레벨마다 넘기 위한 설계가 바로 이 계층 tiling이다. 이 구조를 손으로 짜 볼 필요는 거의 없다(BLAS, XNNPACK, CMSIS-NN이 한다). 하지만 **"왜 이 타일 크기인가"를 캐시 크기로 설명**할 수 있어야 프로파일 결과를 읽고 벤더 커널을 평가할 수 있다.

### 4.4 im2col — conv를 GEMM으로 바꾸는 대가

**im2col**: conv의 각 출력 위치에 필요한 입력 패치(kh × kw × Cin)를 한 행으로 펼쳐서 큰 행렬을 만든 뒤, conv를 GEMM 한 번으로 계산하는 방법. B2(CNN)에서 conv = GEMM 관계를 봤다.

```
입력 (H=4, W=4, C=1), 3×3 kernel, stride 1, padding 0 → 출력 2×2 = 4 위치

입력                    im2col 행렬 [4 위치 × 9]
 a b c d               row0: a b c  e f g  i j k
 e f g h       →       row1: b c d  f g h  j k l
 i j k l               row2: e f g  i j k  m n o
 m n o p               row3: f g h  j k l  n o p

conv 출력 = im2col[4 × 9] × weight[9 × Cout]
```

손계산 — MCU급 레이어: 입력 32 × 32 × 16 (int8, NHWC), 3 × 3, stride 1, same padding.

```
입력 크기        = 32 × 32 × 16          = 16,384 B  (16 KiB)
im2col 행렬 크기 = (32 × 32) × (3 × 3 × 16) = 1024 × 144 = 147,456 B  (144 KiB) → 9배 팽창
```

말로 하면: im2col은 **같은 입력 값을 최대 9번 복사**해서 메모리를 써 버리는 대신, 연속 접근과 GEMM 마이크로커널의 이점을 얻는다. 앱 코어(L2 수 MiB)에서는 대개 이득이지만, SRAM이 수백 KiB인 MCU에서는 144 KiB 버퍼 자체가 부담이다. 그래서 MCU용 라이브러리(CMSIS-NN 등)는 **출력 몇 개 분량만 조금씩 im2col**하거나, im2col 없이 직접 conv 루프를 돈다. 그리고 **NHWC 레이아웃**이면 한 패치의 한 행(kw × Cin)이 메모리에서 연속이라 im2col 복사가 memcpy 몇 번으로 끝난다 — MCU/모바일 런타임이 NHWC를 선호하는 이유 중 하나다(A1 7절).

### 4.5 함정

- tiling 크기를 "L1 크기에 딱 맞게" 잡으면 넘친다. 다른 데이터(스택, 출력, 코드), set 충돌, 하드웨어 prefetch가 가져온 라인이 같이 산다. **절반~2/3 정도**를 목표로 잡고 재 본다.
- 2의 거듭제곱 크기(채널 64, 128, 256…)는 ML에서 매우 흔하다. **텐서 pitch가 2의 거듭제곱이면 conflict를 의심**하자. padding은 메모리를 조금 쓰지만 효과가 크다.

---

## 5. Prefetching — 하드웨어가 해 주는 것, 소프트웨어가 해야 하는 것

### 5.1 정의

- **hardware prefetcher**: 코어가 접근 패턴(연속, 일정 stride)을 감지해서 아직 요청하지 않은 라인을 미리 가져온다. 3.3절에서 stride 접근이 110 ns가 아니라 4.5 ns였던 큰 이유다. 대부분 **페이지 경계를 잘 넘지 않는다**고 알려져 있다(물리 주소가 연속이라는 보장이 없으므로).
- **software prefetch**: 프로그래머가 "곧 이 주소를 쓸 것"이라고 힌트를 준다. GCC/clang의 `__builtin_prefetch(addr)`는 AArch64에서 `PRFM PLDL1KEEP` 명령이 된다(아래 코드에서 디스어셈블로 확인했다). 힌트이므로 틀린 주소를 줘도 fault가 나지 않는다.
- **prefetch distance**: 몇 반복 앞의 주소를 미리 요청할지. 손계산: `거리 ≈ 메모리 latency ÷ 반복 1회 시간`.

### 5.2 코드로 확인 — 순차 vs 랜덤 gather, 그리고 software prefetch

무엇을 확인하나: 128 MiB 배열 `a`를 인덱스 배열을 통해 읽는 합산 `s += a[idx[i]]`. idx가 순차일 때와 랜덤 순열일 때를 비교하고, 랜덤일 때 `dist`개 뒤 원소를 `__builtin_prefetch`하면 빨라지는지 본다.

```c
/* 순차 vs 랜덤(gather) 접근 대역폭, 그리고 소프트웨어 prefetch가 랜덤 gather를 돕는가 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define N (32u<<20)                                   /* uint32 32M개 = 128 MiB (L2 16 MiB의 8배) */
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
__attribute__((noinline)) static unsigned long gather(const unsigned *a, const unsigned *idx, int dist){
  unsigned long s=0;
  for(size_t i=0;i<N;i++){
    if(dist) __builtin_prefetch(&a[idx[(i+dist) & (N-1)]]);   /* dist개 뒤의 원소를 미리 요청 */
    s += a[idx[i]]; }
  return s; }
int main(void){
  unsigned *a=malloc(sizeof(unsigned)*N); unsigned *seq=malloc(4*(size_t)N), *rnd=malloc(4*(size_t)N);
  srand(3); for(size_t i=0;i<N;i++){ a[i]=(unsigned)(i&7); seq[i]=(unsigned)i; rnd[i]=(unsigned)i; }
  for(size_t i=N-1;i>0;i--){ size_t j=(((size_t)rand()<<31)^(size_t)rand())%(i+1); unsigned t=rnd[i]; rnd[i]=rnd[j]; rnd[j]=t; }
  struct { const char *name; const unsigned *idx; int dist; } cfg[] = {
    {"sequential idx       ", seq, 0}, {"sequential + pf 64   ", seq, 64},
    {"random idx           ", rnd, 0}, {"random + pf 8        ", rnd, 8},
    {"random + pf 32       ", rnd, 32}, {"random + pf 128      ", rnd, 128} };
  for(int c=0;c<6;c++){ double best=1e9; unsigned long s=0;
    for(int r=0;r<3;r++){ double t0=now(); s=gather(a,cfg[c].idx,cfg[c].dist); double d=now()-t0; if(d<best)best=d; }
    printf("%s %7.1f ms  %6.2f ns/elem  sum=%lu\n", cfg[c].name, best*1e3, best*1e9/N, s); }
  return 0; }
```

세 번 실행 중 첫 번째 출력 (나머지 두 번도 모든 행이 ±3 % 이내):

```text
sequential idx           11.4 ms    0.34 ns/elem  sum=117440512
sequential + pf 64       16.4 ms    0.49 ns/elem  sum=117440512
random idx              128.6 ms    3.83 ns/elem  sum=117440512
random + pf 8           141.9 ms    4.23 ns/elem  sum=117440512
random + pf 32          122.8 ms    3.66 ns/elem  sum=117440512
random + pf 128         115.5 ms    3.44 ns/elem  sum=117440512
```

출력에서 볼 것 (정직하게):

- **순차 vs 랜덤: 약 11배.** 같은 원소를 같은 횟수 읽는데 순서만 다르다. 순차는 hardware prefetcher가 완벽하게 따라가고, 한 라인(128 B = uint32 32개)을 32번 재사용한다. 랜덤은 거의 매 원소가 새 라인 + DRAM이다.
- **순차 + software prefetch는 오히려 43 % 느려졌다.** hardware가 이미 하고 있는 일에 명령(인덱스 load + PRFM)만 추가한 셈이다.
- **랜덤 + software prefetch는 거리에 따라 −10 % ~ +10 %.** 거리 8은 오히려 느리고, 128에서 약 10 % 빨라졌다. 기대보다 효과가 작은 이유: 랜덤 gather도 이미 원소당 3.8 ns인데, DRAM latency가 110 ns이므로 **OoO 코어가 이미 약 110 ÷ 3.8 ≈ 29개의 miss를 동시에 날리고 있다**(6절). `idx[i]`끼리 서로 독립이라서 OoO 창이 앞쪽 load들을 알아서 먼저 발행한다. software prefetch가 채울 빈틈이 별로 없었다.
- 손계산으로 거리를 잡으면: `110 ns ÷ 약 3.5 ns/반복 ≈ 31` → 32~128 근처가 맞다는 것과 결과가 일치한다. 8은 너무 가까워서 prefetch가 도착하기 전에 실제 load가 온다.

그러면 software prefetch는 언제 효과가 클까:

- **OoO 창이 작거나 없는 코어**(in-order A55, 일부 DSP): 하드웨어가 miss를 겹치지 못하므로 prefetch가 곧 MLP다. DSP에서는 prefetch 대신 **DMA로 다음 타일을 미리 가져오는 double buffering**이 같은 역할을 한다(D3 9.2절).
- **pointer chasing처럼 주소가 앞 load에 의존하는 경우**: OoO도 못 앞서 가므로, 몇 단계 앞 노드 주소를 따로 알고 있다면 prefetch가 크게 돕는다(트리·그래프 탐색). 주소를 모르면 prefetch도 못 한다.
- **반복 사이의 계산량이 많아서 OoO 창이 계산 명령으로 가득 차는 경우**: 창이 다음 load까지 닿지 못한다.
- 교훈: **software prefetch는 "측정해서 이득일 때만" 넣는다.** 넣기 전후를 반드시 재고, 다른 코어·다른 데이터 크기에서도 재 본다.

ML 연결: embedding table lookup(추천 모델·토크나이저 임베딩), sparse 연산(C4의 CSR), KV 캐시 페이지 인덱싱은 **random gather**다. dense conv/GEMM은 순차에 가까워서 hardware prefetcher의 몫이다.

---

## 6. Memory-level parallelism, store buffer

### 6.1 정의와 손계산

**MLP**(memory-level parallelism): 동시에 진행 중인 cache miss 개수. 코어에는 miss를 추적하는 버퍼(MSHR, line fill buffer 등으로 부른다)가 유한 개 있다. 2절의 FMA와 똑같이 Little's law가 적용된다.

```
달성 대역폭 = (동시에 날아가는 miss 수 × 라인 크기) ÷ latency

miss 1개:   1 × 128 B ÷ 110 ns ≈ 1.2 GB/s
miss 24개: 24 × 128 B ÷ 110 ns ≈ 28 GB/s
DRAM 60 GB/s를 내려면: 60 GB/s × 110 ns ÷ 128 B ≈ 52개의 라인이 동시에 날아가야 함
```

말로 하면: DRAM 대역폭은 "파이프 굵기"만으로 정해지지 않는다. **latency가 긴 파이프를 채울 만큼 요청을 동시에 넣어야** 한다. NVMe에서 queue depth를 올려야 IOPS가 나오는 것과 같은 식이다.

### 6.2 코드로 확인 — 독립 pointer chase 체인 1~32개

무엇을 확인하나: 256 MiB 원형 리스트 위의 서로 먼 k개 지점에서 동시에 chase를 진행한다. 각 체인은 여전히 의존 load지만 **체인끼리는 독립**이므로 OoO 코어가 겹칠 수 있다.

```c
/* memory-level parallelism: 256 MiB에서 독립 pointer-chase 체인 1~32개를 번갈아 진행 */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define LINE 128
#define BYTES (256u<<20)
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
int main(void){
  size_t n=BYTES/LINE; char *buf=malloc(BYTES); size_t *perm=malloc(n*sizeof *perm);
  srand(4); for(size_t i=0;i<n;i++) perm[i]=i;
  for(size_t i=n-1;i>0;i--){ size_t j=(size_t)rand()%(i+1); size_t t=perm[i]; perm[i]=perm[j]; perm[j]=t; }
  for(size_t i=0;i<n;i++) *(void**)(buf+perm[i]*LINE) = buf+perm[(i+1)%n]*LINE;   /* 큰 원 하나 */
  for(int k=1;k<=32;k*=2){
    void **p[32]; for(int c=0;c<k;c++) p[c]=(void**)(buf+perm[c*(n/32)]*LINE);  /* 원 위의 서로 먼 k개 지점 */
    long steps=4000000/k; double best=1e9;
    for(int r=0;r<3;r++){ double t0=now();
      for(long i=0;i<steps;i++) for(int c=0;c<k;c++) p[c]=(void**)*p[c];     /* k개 체인은 서로 독립 */
      double ns=(now()-t0)/(steps*(double)k)*1e9; if(ns<best) best=ns; }
    int nul=0; for(int c=0;c<k;c++) nul+= (p[c]==NULL);
    printf("chains=%2d  %6.2f ns/load (average)  -> %5.1f loads in flight (110 ns 가정)%s\n", k, best, 110.0/best, nul?"!":""); }
  free(buf); free(perm); return 0; }
```

```text
chains= 1  111.20 ns/load (average)  ->   1.0 loads in flight (110 ns 가정)
chains= 2   56.19 ns/load (average)  ->   2.0 loads in flight (110 ns 가정)
chains= 4   28.43 ns/load (average)  ->   3.9 loads in flight (110 ns 가정)
chains= 8   15.60 ns/load (average)  ->   7.1 loads in flight (110 ns 가정)
chains=16    8.30 ns/load (average)  ->  13.3 loads in flight (110 ns 가정)
chains=32    4.66 ns/load (average)  ->  23.6 loads in flight (110 ns 가정)
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 320"><text x="20" y="22" font-size="14">DRAM(256 MiB) pointer chase: 독립 체인 수 vs load당 평균 시간 (M2, 실측)</text><line x1="70" y1="260" x2="610" y2="260" stroke="currentColor"/><line x1="70" y1="40" x2="70" y2="260" stroke="currentColor"/><line x1="70" y1="260.0" x2="610" y2="260.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="264.0" font-size="12" text-anchor="end">0</text><line x1="70" y1="214.2" x2="610" y2="214.2" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="218.2" font-size="12" text-anchor="end">25</text><line x1="70" y1="168.3" x2="610" y2="168.3" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="172.3" font-size="12" text-anchor="end">50</text><line x1="70" y1="122.5" x2="610" y2="122.5" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="126.5" font-size="12" text-anchor="end">75</text><line x1="70" y1="76.7" x2="610" y2="76.7" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="80.7" font-size="12" text-anchor="end">100</text><rect x="87.0" y="56.1" width="56" height="203.9" fill="#e08a3c"/><text x="115.0" y="50.1" font-size="12" text-anchor="middle">111.2</text><text x="115.0" y="278" font-size="12" text-anchor="middle">1</text><rect x="177.0" y="157.0" width="56" height="103.0" fill="#e08a3c"/><text x="205.0" y="151.0" font-size="12" text-anchor="middle">56.2</text><text x="205.0" y="278" font-size="12" text-anchor="middle">2</text><rect x="267.0" y="207.9" width="56" height="52.1" fill="#e08a3c"/><text x="295.0" y="201.9" font-size="12" text-anchor="middle">28.4</text><text x="295.0" y="278" font-size="12" text-anchor="middle">4</text><rect x="357.0" y="231.4" width="56" height="28.6" fill="#e08a3c"/><text x="385.0" y="225.4" font-size="12" text-anchor="middle">15.6</text><text x="385.0" y="278" font-size="12" text-anchor="middle">8</text><rect x="447.0" y="244.8" width="56" height="15.2" fill="#e08a3c"/><text x="475.0" y="238.8" font-size="12" text-anchor="middle">8.3</text><text x="475.0" y="278" font-size="12" text-anchor="middle">16</text><rect x="537.0" y="251.5" width="56" height="8.5" fill="#e08a3c"/><text x="565.0" y="245.5" font-size="12" text-anchor="middle">4.7</text><text x="565.0" y="278" font-size="12" text-anchor="middle">32</text><text x="340.0" y="300" font-size="13" text-anchor="middle">동시에 진행하는 독립 체인 수</text><text x="18" y="150.0" font-size="13" text-anchor="middle" transform="rotate(-90 18 150.0)">ns / load</text></svg>
```

그림 6 — 체인이 두 배가 되면 load당 평균 시간이 거의 절반. 2절 FMA 체인 그래프와 같은 모양이다. latency는 그대로(각 load는 여전히 약 110 ns)인데 **겹쳐서 기다리니까** 처리량이 오른다.

출력에서 볼 것:

- 32개까지도 거의 선형으로 좋아진다. 이 코어의 동시 miss 한계는 32보다 크거나 비슷한 것으로 보인다(정확한 수는 확인하지 않았다 — 64 체인까지 늘리면 포화 지점을 찾을 수 있다, 14절 연습).
- 32 체인에서 4.66 ns/load × 128 B → 약 27 GB/s. 5.2절 랜덤 gather(3.8 ns/원소)와 비슷한 수준이다 — 그쪽도 결국 OoO가 만든 수십 개의 동시 miss였다.
- **MCU 관점**: Cortex-M4/M7 같은 in-order 코어는 동시에 기다릴 수 있는 miss가 매우 적다(설계상 소수). 그래서 MCU에서는 "캐시 miss를 겹친다"는 선택지가 거의 없고, **DMA로 미리 SRAM/TCM에 가져다 놓는 것**이 유일하게 확실한 방법이다(E7).

### 6.3 store buffer (짧게)

store는 메모리에 바로 쓰지 않고 **store buffer**에 들어간 뒤 나중에 캐시로 빠져나간다. 그래서 store miss는 대개 코어를 멈추지 않는다(버퍼가 꽉 차기 전까지). 같은 주소를 곧바로 다시 읽으면 캐시까지 가지 않고 store buffer에서 값을 넘겨받는다(**store-to-load forwarding**). 8.1절의 false sharing 실험에서 스레드 1개가 `volatile` 카운터를 1억 번 증가시키는 데 약 30 ms(증가 1회 ≈ 0.3 ns ≈ 1 cycle)밖에 안 걸렸는데, load → add → store → (다음 반복) load가 이렇게 빨리 이어지는 것은 이런 forwarding 경로 덕분으로 보인다(Apple 코어의 정확한 메커니즘은 공개되어 있지 않다).

ML 연결: 큰 출력 텐서를 쓰는 커널(elementwise, 전치)은 store 대역폭에 걸리기도 한다. 결과를 캐시에 남길 필요가 없는 큰 출력은 non-temporal store(AArch64 `STNP` 등)로 캐시 오염을 줄이는 기법이 있지만, 효과는 코어마다 달라서 재 봐야 한다.

---

## 7. 분기 예측 — 정렬된 데이터가 빠른 이유, branchless가 이기는 때

### 7.1 직관과 정의

OoO 코어는 분기 결과가 나오기 전에 **예측한 방향으로 수십~수백 개의 명령을 미리 실행**한다. 맞으면 공짜, 틀리면 추측 실행분을 전부 버리고 올바른 방향부터 다시 fetch한다. 그 손해가 **misprediction penalty**다. 깊고 넓은 코어일수록 크다.

- **branchy**: `if (x >= thr) s += x;` → 조건 분기 명령.
- **branchless**: 비교 결과를 **값**으로 바꿔 쓴다. AArch64의 `CSEL`(조건 선택), x86의 `CMOV`, 또는 마스크 `x & -(x >= thr)`. 분기가 없으니 예측할 것도 없다. 대신 **양쪽을 모두 계산**한다.
- 예측기는 과거 패턴을 학습한다. 정렬된 데이터(앞 절반은 전부 false, 뒤 절반은 전부 true)는 거의 100 % 맞힌다. 랜덤 데이터에서 조건이 참일 확률이 p이면 최선의 예측기도 약 `min(p, 1 − p)`의 비율로 틀린다.

### 7.2 코드로 확인 — 정렬/비정렬 × branchy/branchless

무엇을 확인하나: 1M개 랜덤 바이트에서 `d[i] >= THR`인 원소를 합산한다. branchy 버전은 빈 inline asm으로 컴파일러가 if를 select로 바꾸지 못하게 막았고, branchless 버전은 마스크 식 + asm 장벽으로 NEON 벡터화를 막아 **스칼라 비교끼리** 비교되게 했다. 마지막은 평범한 코드를 `-O2`에 맡긴 결과다.

```c
/* threshold 합: 정렬 vs 비정렬 데이터 × 분기(branchy) vs 무분기(branchless) */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define N (1<<20)
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static int cmp(const void *x, const void *y){ return *(const unsigned char*)x - *(const unsigned char*)y; }
__attribute__((noinline)) static long branchy(const unsigned char *d, int thr){
  long s=0;
  #pragma clang loop vectorize(disable)
  for(int i=0;i<N;i++)
    if(d[i] >= thr){ s += d[i]; __asm__ volatile(""); }   /* 빈 asm: 컴파일러가 if를 select로 못 바꾸게 */
  return s; }
__attribute__((noinline)) static long branchless(const unsigned char *d, int thr){
  long s=0;
  for(int i=0;i<N;i++){ long keep = -(long)(d[i] >= thr);  /* 0 또는 0xFFFF...: 비교 결과를 마스크로 */
    s += d[i] & keep; __asm__ volatile("" : "+r"(s)); }   /* asm: NEON 벡터화 방지 → 스칼라 비교 */
  return s; }
__attribute__((noinline)) static long compiler(const unsigned char *d, int thr){
  long s=0; for(int i=0;i<N;i++) if(d[i] >= thr) s += d[i];   /* 평범한 코드: -O2가 알아서 */
  return s; }
static int THR=128;
static double bench(long (*f)(const unsigned char*,int), const unsigned char *d, long *out){
  double best=1e9; for(int r=0;r<20;r++){ double t0=now(); *out=f(d,THR); double dt=now()-t0; if(dt<best)best=dt; }
  return best*1e9/N; }
int main(int argc, char **argv){
  if(argc>1) THR=atoi(argv[1]);   /* 조건이 참일 확률 = (256-THR)/256 */
  unsigned char *u=malloc(N), *s=malloc(N); srand(5);
  for(int i=0;i<N;i++) u[i]=s[i]=(unsigned char)(rand()&255);
  qsort(s,N,1,cmp); long r;
  printf("unsorted branchy    %.2f ns/elem", bench(branchy,u,&r));    printf("  sum=%ld\n", r);
  printf("sorted   branchy    %.2f ns/elem", bench(branchy,s,&r));    printf("  sum=%ld\n", r);
  printf("unsorted branchless %.2f ns/elem", bench(branchless,u,&r)); printf("  sum=%ld\n", r);
  printf("sorted   branchless %.2f ns/elem", bench(branchless,s,&r)); printf("  sum=%ld\n", r);
  printf("unsorted compiler   %.2f ns/elem", bench(compiler,u,&r));   printf("  sum=%ld\n", r);
  return 0; }
```

```sh
cc -std=c11 -Wall -Wextra -O2 branch.c -o branch && ./branch && ./branch 250
```

```text
unsorted branchy    3.01 ns/elem  sum=100279590
sorted   branchy    0.30 ns/elem  sum=100279590
unsorted branchless 0.32 ns/elem  sum=100279590
sorted   branchless 0.32 ns/elem  sum=100279590
unsorted compiler   0.21 ns/elem  sum=100279590
unsorted branchy    0.48 ns/elem  sum=6164872
sorted   branchy    0.29 ns/elem  sum=6164872
unsorted branchless 0.31 ns/elem  sum=6164872
sorted   branchless 0.32 ns/elem  sum=6164872
unsorted compiler   0.21 ns/elem  sum=6164872
```

실제로 생성된 명령 (`objdump -d`에서 루프 부분):

```text
_branchy:       ldrb w10, [x0, x9]      ; d[i]
                cmp  w1, w10
                b.gt ...                ; ← 조건 분기: 예측 대상
                add  x8, x8, x10
_branchless:    ldrb w10, [x0, x9]
                cmp  w1, w10
                csel x10, xzr, x10, gt  ; ← 조건 선택: 분기 없음
                add  x8, x10, x8
_compiler:      ldr q21, [x0, x8] / tbl.16b / cmgt.4s / ...   ; ← NEON으로 16바이트씩, 비교는 마스크
```

THR을 바꿔 가며 비정렬 branchy를 잰 결과(두 번 실행해 거의 같음)와 모델:

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 640 340"><text x="20" y="22" font-size="14">조건이 참일 확률 p vs 원소당 시간 (랜덤 데이터, M2, 실측)</text><line x1="70" y1="280" x2="610" y2="280" stroke="currentColor"/><line x1="70" y1="40" x2="70" y2="280" stroke="currentColor"/><line x1="70.0" y1="280" x2="70.0" y2="285" stroke="currentColor"/><text x="70.0" y="299" font-size="12" text-anchor="middle">0</text><line x1="205.0" y1="280" x2="205.0" y2="285" stroke="currentColor"/><text x="205.0" y="299" font-size="12" text-anchor="middle">0.25</text><line x1="340.0" y1="280" x2="340.0" y2="285" stroke="currentColor"/><text x="340.0" y="299" font-size="12" text-anchor="middle">0.5</text><line x1="475.0" y1="280" x2="475.0" y2="285" stroke="currentColor"/><text x="475.0" y="299" font-size="12" text-anchor="middle">0.75</text><line x1="610.0" y1="280" x2="610.0" y2="285" stroke="currentColor"/><text x="610.0" y="299" font-size="12" text-anchor="middle">1</text><line x1="70" y1="280.0" x2="610" y2="280.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="284.0" font-size="12" text-anchor="end">0</text><line x1="70" y1="211.4" x2="610" y2="211.4" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="215.4" font-size="12" text-anchor="end">1</text><line x1="70" y1="142.9" x2="610" y2="142.9" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="146.9" font-size="12" text-anchor="end">2</text><line x1="70" y1="74.3" x2="610" y2="74.3" stroke="#888" stroke-width="0.5" stroke-dasharray="2 4"/><text x="62" y="78.3" font-size="12" text-anchor="end">3</text><polyline points="70.0,260.1 83.5,250.9 97.0,241.6 110.5,232.3 124.0,223.1 137.5,213.8 151.0,204.6 164.5,195.3 178.0,186.1 191.5,176.8 205.0,167.5 218.5,158.3 232.0,149.0 245.5,139.8 259.0,130.5 272.5,121.3 286.0,112.0 299.5,102.7 313.0,93.5 326.5,84.2 340.0,75.0 353.5,84.2 367.0,93.5 380.5,102.7 394.0,112.0 407.5,121.3 421.0,130.5 434.5,139.8 448.0,149.0 461.5,158.3 475.0,167.5 488.5,176.8 502.0,186.1 515.5,195.3 529.0,204.6 542.5,213.8 556.0,223.1 569.5,232.3 583.0,241.6 596.5,250.9 610.0,260.1" fill="none" stroke="#888" stroke-width="1.5" stroke-dasharray="5 3"/><polyline points="70.0,260.1 82.7,247.1 124.8,216.9 205.0,162.7 340.0,74.3 475.0,166.2 555.2,219.0 597.3,247.1 610.0,260.1" fill="none" stroke="#d0564a" stroke-width="2.5"/><circle cx="70.0" cy="260.1" r="3.5" fill="#d0564a"/><circle cx="82.7" cy="247.1" r="3.5" fill="#d0564a"/><circle cx="124.8" cy="216.9" r="3.5" fill="#d0564a"/><circle cx="205.0" cy="162.7" r="3.5" fill="#d0564a"/><circle cx="340.0" cy="74.3" r="3.5" fill="#d0564a"/><circle cx="475.0" cy="166.2" r="3.5" fill="#d0564a"/><circle cx="555.2" cy="219.0" r="3.5" fill="#d0564a"/><circle cx="597.3" cy="247.1" r="3.5" fill="#d0564a"/><circle cx="610.0" cy="260.1" r="3.5" fill="#d0564a"/><line x1="70" y1="258.1" x2="610" y2="258.1" stroke="#3f9a6b" stroke-width="2.5"/><line x1="90" y1="52" x2="114" y2="52" stroke="#d0564a" stroke-width="3"/><text x="120" y="56" font-size="12">branchy (if)</text><line x1="90" y1="70" x2="114" y2="70" stroke="#3f9a6b" stroke-width="3"/><text x="120" y="74" font-size="12">branchless (csel) 0.32 ns 일정</text><line x1="90" y1="88" x2="114" y2="88" stroke="#888" stroke-width="1.5" stroke-dasharray="5 3"/><text x="120" y="92" font-size="12">모델: 0.29 + 5.4 ns × min(p, 1−p)</text><text x="340.0" y="320" font-size="13" text-anchor="middle">p = P(d[i] ≥ THR)</text><text x="18" y="160.0" font-size="13" text-anchor="middle" transform="rotate(-90 18 160.0)">ns / 원소</text></svg>
```

그림 7 — 빨강: branchy, 비정렬 데이터. p = 0.5(THR = 128)에서 3.0 ns로 최대, 양 끝(항상 참/항상 거짓)에서 0.29 ns. 초록: branchless는 p와 무관하게 0.32 ns. 회색 점선: "틀릴 확률 min(p, 1 − p) × 5.4 ns"라는 단순 모델이 실측과 잘 맞는다.

출력에서 볼 것:

- **같은 데이터, 같은 연산인데 정렬 여부만으로 10배**(3.01 vs 0.30 ns). 차이는 전부 분기 예측 실패다.
- 역산하면 misprediction 한 번의 손해 ≈ `(3.0 − 0.29) ÷ 0.5 ≈ 5.4 ns ≈ 18~19 cycle`. 이것은 "파이프라인을 비우고 다시 채우는 시간 + 버린 작업"을 합친 **실효 손해**다. 코어 설계의 공식 penalty 숫자와는 정의가 다를 수 있다.
- **branchless는 항상 0.32 ns** — 잘 맞히는 branchy(0.29 ns)보다 아주 약간 느리다. 분기가 잘 예측될 때는 branchy가 공짜에 가깝고, branchless는 매번 양쪽을 계산하기 때문이다.
- **p = 2.3 %(THR = 250)**처럼 한쪽으로 치우치면 branchy도 0.48 ns로 괜찮다. branchless가 확실히 이기는 건 **p가 0.1~0.9 사이의 예측 불가 구간**이다.
- **컴파일러에 맡긴 평범한 코드가 가장 빠르다(0.21 ns).** clang이 NEON으로 16바이트씩 비교해 마스크로 더했다. SIMD에는 "분기"라는 개념이 없고 모두 마스크다. 결국 **벡터화 = 가장 강력한 branchless**다.

### 7.3 ML에서 어디에 나오나

| 코드 | 분기 성질 | 권장 |
|---|---|---|
| ReLU `max(x, 0)` | 부호가 거의 랜덤 (p ≈ 0.5) | `fmax`/`vmaxq`·int8은 `max` 명령. 분기로 쓰지 않는다 |
| 양자화 clamp `min(max(x, lo), hi)` | 대부분 범위 안 | select/min/max 명령 (C1) |
| 검출 score threshold (`score > 0.5`) | 대부분 앵커가 탈락 → p 작음 | branchy도 괜찮지만, 벡터 비교 + 압축(compress)이 더 빠르다 |
| NMS의 IoU 비교 | 박스 순서·겹침에 따라 불규칙 | 후보 수를 먼저 줄이고(top-k), IoU 계산은 벡터로, 억제 판단만 스칼라 |
| argmax / top-k | "새 최댓값인가?" — 초반엔 자주, 후반엔 드물게 참 | 대체로 예측 잘 됨. 벡터 max 후 위치 찾기 |
| 조건부 실행 모델 (early exit, MoE routing) | 입력마다 다름 | 분기 비용보다 캐시·weight 로드가 더 큼 — 레이어 단위로 판단 |

in-order MCU에서는 이야기가 조금 다르다. Cortex-M4는 3단 파이프라인이라 분기 실패 손해가 몇 cycle 수준으로 작고(Arm 문서상 분기는 1~3 cycle 정도), 대신 **분기 자체가 명령 수를 늘린다**. Thumb-2의 `IT` 블록(조건 실행)이 짧은 if를 분기 없이 처리하는 도구다 — 9.3절 어셈블리에서 `it eq / popeq`가 나온다.

---

## 8. 멀티코어 — false sharing, P-core vs E-core

### 8.1 코드로 확인 — false sharing

**cache coherence**: 여러 코어가 같은 주소를 캐시에 가지고 있을 때 값이 어긋나지 않게 하는 하드웨어 프로토콜(MESI 계열). 한 코어가 쓰려면 다른 코어의 복사본을 무효화(invalidate)해야 한다. 이 단위가 **캐시 라인**이다.

**false sharing**: 두 스레드가 **서로 다른 변수**를 쓰는데, 그 변수들이 **같은 캐시 라인**에 있어서 라인이 코어 사이를 계속 핑퐁하는 현상. 논리적으로는 공유가 없는데 하드웨어는 공유로 본다.

무엇을 확인하나: 스레드 2개가 각자 자기 카운터만 1억 번 증가시킨다. (a) 두 카운터가 8 B 떨어진 경우(같은 라인), (b) 128 B 떨어진 경우.

```c
/* false sharing: 스레드 2개가 각자 자기 카운터만 증가 — 같은 캐시 라인 vs 128 B 떨어진 라인 */
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#define ITERS 100000000L
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
struct same   { volatile long a; volatile long b; };                          /* a,b가 8 B 차이: 같은 라인 */
struct padded { volatile long a; char pad[120]; volatile long b; } __attribute__((aligned(128)));  /* 128 B 차이 */
static struct same   S __attribute__((aligned(128)));
static struct padded P;
static void *inc(void *p){ volatile long *c=p; for(long i=0;i<ITERS;i++) (*c)++; return NULL; }
static double run2(volatile long *x, volatile long *y){
  pthread_t t1,t2; double t0=now();
  pthread_create(&t1,NULL,inc,(void*)x); pthread_create(&t2,NULL,inc,(void*)y);
  pthread_join(t1,NULL); pthread_join(t2,NULL); return now()-t0; }
int main(void){
  for(int r=0;r<3;r++){
    double t1=now(); inc((void*)&S.a); t1=now()-t1;               /* 비교 기준: 스레드 1개 */
    double ts=run2(&S.a,&S.b), tp=run2(&P.a,&P.b);
    printf("run %d: 1 thread %.0f ms | 2 threads same line %.0f ms | 2 threads padded %.0f ms\n",
           r, t1*1e3, ts*1e3, tp*1e3); }
  printf("check: %ld %ld %ld %ld\n", S.a, S.b, P.a, P.b);
  return 0; }
```

```text
run 0: 1 thread 59 ms | 2 threads same line 146 ms | 2 threads padded 36 ms
run 1: 1 thread 31 ms | 2 threads same line 144 ms | 2 threads padded 30 ms
run 2: 1 thread 30 ms | 2 threads same line 144 ms | 2 threads padded 31 ms
check: 600000000 300000000 300000000 300000000
```

(`S.a`가 6억인 것은 1-스레드 기준 측정도 `S.a`를 증가시켰기 때문이다: 3회 × (1억 + 1억).)

출력에서 볼 것:

- **같은 라인: 약 145 ms, 떨어뜨리면: 약 31 ms — 약 4.7배.** 두 스레드가 병렬로 돌면 1-스레드(30 ms)와 같은 시간이 나와야 정상인데, 같은 라인이면 **두 스레드인데 1-스레드보다 5배 느리다.**
- run 0의 1-스레드가 59 ms인 것은 첫 실행에서 코어 클럭이 올라오는 중(DVFS ramp)이었거나 스케줄링 영향으로 보인다. **첫 반복은 버린다**는 D6의 규칙이 여기서도 맞는다.
- 추가 실험: padding을 56 B로 줄여 **64 B만 떨어뜨려도** false sharing이 사라졌다(padded 30~32 ms). `hw.cachelinesize`는 128 B인데 coherence가 64 B 단위로 동작하는 것처럼 보인다. 원인은 확인하지 못했다(L1과 L2의 라인/섹터 크기가 다를 가능성 등). **안전한 규칙은 "보고된 라인 크기(128 B)로 정렬"**이다. C11/C++17에서는 `alignas(128)`, C++17의 `std::hardware_destructive_interference_size`도 있다.

ML 연결: 멀티스레드 추론 런타임이 스레드별 통계 카운터, 스레드별 부분합(reduction), 작업 큐 인덱스를 배열에 붙여 두면 이 현상이 난다. **스레드별 출력 타일의 경계**가 같은 라인을 공유해도 같은 일이 생긴다 — 출력 채널을 스레드에 나눌 때 타일 경계를 라인에 맞추는 이유다.

### 8.2 P-core vs E-core (big.LITTLE)

- **big.LITTLE / heterogeneous multi-processing**: 성능용 큰 코어(big, Apple은 P-core)와 효율용 작은 코어(LITTLE, E-core)를 한 칩에 둔다. 폰 SoC는 보통 큰 코어 1~4개 + 작은 코어 4개(예: Cortex-X + A7x + A5x의 3단 구성도 흔하다).
- 두 코어는 ISA는 같지만 **마이크로아키텍처가 전혀 다르다**. 이 Mac만 봐도 E-core는 L1d 64 KiB(P-core의 절반), L2 4 MiB(P의 1/4)다. 같은 모델이 어느 코어에 올라가느냐에 따라 추론 시간이 몇 배 달라진다.
- 스케줄러가 스레드를 어디에 올릴지 정한다. 벤치마크 결과가 "가끔 3배 느린" 이중 분포를 보이면 **코어 이동**을 먼저 의심한다(D6 2.5절의 꼬리 지연).

### 8.3 코드로 확인 — macOS에서 QoS 클래스와 코어

무엇을 확인하나: macOS는 스레드를 특정 코어에 고정하는 공개 API가 없다. 대신 스레드에 **QoS 클래스**(USER_INTERACTIVE, BACKGROUND 등)를 **힌트**로 준다. QoS만 바꾸면 E-core로 가는지, 1-cycle add 체인으로 "보이는 클럭"을 재서 확인한다.

```c
/* macOS QoS 클래스별로 같은 1-cycle add 체인을 돌려 "보이는 클럭"을 비교 (P-core vs E-core 추정) */
#include <stdio.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <time.h>
#define ITERS 200000000L
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static double ghz(void){ long x=0; double t0=now();
  for(long i=0;i<ITERS;i++) __asm__ volatile("add %0, %0, #1" : "+r"(x));
  return ITERS/(now()-t0)/1e9; }
static void *worker(void *arg){
  qos_class_t q = *(qos_class_t*)arg;
  printf("%-18s", q==QOS_CLASS_USER_INTERACTIVE ? "USER_INTERACTIVE" : "BACKGROUND");
  for(int r=0;r<6;r++) printf(" %.2f", ghz());
  printf("  (GHz-equivalent per run)\n"); return NULL; }
int main(void){
  qos_class_t qs[2]={QOS_CLASS_USER_INTERACTIVE, QOS_CLASS_BACKGROUND};
  for(int k=0;k<2;k++){ pthread_t t; pthread_attr_t a; pthread_attr_init(&a);
    pthread_attr_set_qos_class_np(&a, qs[k], 0);   /* 코어 지정이 아니라 스케줄러에 주는 "힌트" */
    pthread_create(&t,&a,worker,&qs[k]); pthread_join(t,NULL); }
  return 0; }
```

```sh
./qos                      # 그냥 실행
taskpolicy -b ./qos        # 프로세스 전체를 background 정책으로 실행
```

```text
USER_INTERACTIVE   2.40 3.45 3.45 3.45 3.43 3.38  (GHz-equivalent per run)
BACKGROUND         3.41 3.41 3.45 3.45 3.45 3.43  (GHz-equivalent per run)
```

```text
USER_INTERACTIVE   0.95 0.99 1.27 1.32 1.26 1.01  (GHz-equivalent per run)
BACKGROUND         1.03 1.13 1.16 1.11 1.27 0.94  (GHz-equivalent per run)
```

출력에서 볼 것 (정직하게):

- **스레드 QoS를 BACKGROUND로 줘도 이 환경에서는 P-core 속도(약 3.4 GHz 상당)로 돌았다.** `pthread_set_qos_class_self_np`로 바꿔 봐도 마찬가지였고, 호출은 성공(반환값 0)했다. 스레드 QoS가 프로세스 쪽 정책에 의해 무시·상향된 것인지 등 원인은 확인하지 못했다. **QoS는 명령이 아니라 힌트**라는 것을 실제로 본 셈이다.
- **`taskpolicy -b`로 프로세스 전체를 background로 돌리면 두 스레드 모두 약 1.0~1.3 GHz 상당**으로 떨어졌다. E-core로 옮겨졌고(추정), background 정책에서 클럭도 낮게 유지된 것으로 보인다(M2 E-core의 최대 클럭은 약 2.4 GHz로 알려져 있다). 실행마다 0.94~1.32로 흔들린다.
- 첫 측정(2.40)은 클럭이 올라오기 전 값이다. **워밍업 없이 첫 추론만 재면 이런 값을 잡는다.**
- "GHz 상당"은 두 코어 모두 정수 add latency가 1 cycle이라는 가정에서 나온 값이다.

Android/Linux에서는 `sched_setaffinity()`(또는 `taskset`)로 스레드를 특정 CPU 번호에 고정할 수 있어서 벤치마크가 더 재현 가능하다. 다만 폰에서도 열·전력 정책(K4, E9)이 클럭을 바꾸므로, **고정 + 워밍업 + 여러 번 반복 + 분포 보고**는 여전히 필요하다. TFLite·ONNX Runtime 같은 런타임은 "big 코어에만 스레드 N개"를 고르는 옵션을 제공하는 경우가 많다.

---

## 9. In-order MCU에서 보면 — Cortex-M4/M7, TCM, 결정적 타이밍

### 9.1 MCU 파이프라인의 확실한 사실만

| 항목 | Cortex-M4 | Cortex-M7 |
|---|---|---|
| 파이프라인 | 3단 (fetch, decode, execute) + 분기 추측 | 6단, **dual-issue** superscalar (in-order) |
| 분기 예측 | 없음 수준 (분기 대상 미리 fetch 정도) | 있음 (BTAC) |
| 캐시 | 코어에 없음 (벤더가 flash 가속기 추가) | I-cache·D-cache 옵션 (각 0~64 KiB) |
| TCM | 없음 (SRAM을 버스로 직접) | ITCM·DTCM 옵션 |
| FPU | 단정밀도 FPv4-SP (옵션) | 단정밀도 또는 배정밀도 FPv5 (옵션) |
| DSP 확장 | SIMD 16×2 / 8×4, `SMLAD` 등 | 동일 + 더 높은 클럭 |

- **TCM**(tightly coupled memory): 캐시처럼 코어 옆에 붙어 있지만 **주소가 고정된 SRAM**이다. hit/miss가 없어서 접근 시간이 항상 같다. Don이 R8에서 ATCM/BTCM에 ISR과 핫 데이터를 올렸던 것과 같다. 캐시는 평균을 빠르게 하지만 최악 시간을 흔들고, TCM은 최악 시간을 보장한다. 메모리 계층 전체 이야기는 E7에서 한다.
- **결정적 타이밍(deterministic timing)**: M4에서 SRAM·TCM 위의 루프는 명령 cycle을 더해서 실행 시간을 거의 정확히 예측할 수 있다(flash wait state와 버스 경합이 없다면). 앱 코어에서는 OoO·캐시·예측기·DVFS 때문에 같은 루프가 매번 다르게 걸린다. **always-on 오디오 파이프라인이 MCU/DSP에 있는 이유 중 하나**가 이 결정성이다(D6 5절 deadline).

### 9.2 손계산 — M4에서 float 내적 1원소의 cycle

Cortex-M4 TRM의 FPU 타이밍 표 기준(대략, 메모리 wait state 0 가정): `VLDR` 2 cycle, `VMUL.F32` 1, `VADD.F32` 1, `VMLA/VFMA.F32` 3.

```
원소 1개 = VLDR × 2 (a, b) + VMUL + VADD ≈ 2 + 2 + 1 + 1 = 6 cycle
루프 오버헤드 (카운터 감소, 비교, 분기) 는 4배 unroll로 원소당 약 1 cycle 이하
→ 원소당 약 6~7 cycle,  1024-원소 내적 ≈ 6,500 cycle
   168 MHz M4 이면 ≈ 39 µs
```

말로 하면: M4에서는 **load가 계산보다 비싸다.** 연속 load를 `VLDM`(여러 레지스터를 한 번에 로드)으로 묶거나, int8/int16 데이터를 32-bit 한 번에 가져와 SIMD로 처리하는 것(CMSIS-NN의 방식)이 효과적인 이유다. 이 숫자는 실제 보드에서 DWT CYCCNT로 확인해야 한다(D6 4.2절).

### 9.3 코드로 확인 — clang으로 Cortex-M4 어셈블리 읽기

무엇을 확인하나: float 내적과 int8 내적을 Cortex-M4F용으로 컴파일해서, 컴파일러가 어떤 명령을 고르고 루프를 어떻게 만드는지 본다. 이 Mac에서는 ARM 타깃용 링크를 하지 않고 `-S`(어셈블리 출력)만 한다.

```c
/* Cortex-M4F에서 float 내적과 int8 내적이 어떤 명령으로 번역되나 (-S 어셈블리만 생성) */
#include <stdint.h>
float dot_f32(const float *a, const float *b, int n) {
    float acc = 0.0f;
    for (int i = 0; i < n; i++)
        acc += a[i] * b[i];
    return acc;
}
int32_t dot_s8(const int8_t *a, const int8_t *b, int n) {
    int32_t acc = 0;
    for (int i = 0; i < n; i++)
        acc += (int32_t)a[i] * (int32_t)b[i];
    return acc;
}
```

```sh
clang --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfloat-abi=hard -O2 -S dot_m4.c -o dot_m4.s
```

경고 0개. 생성된 `dot_m4.s`에서 `dot_f32`의 **안쪽 루프 부분만** 그대로 옮긴다(`.fpu fpv4-sp-d16`로 선언됨).

```text
.LBB0_3:                                @ =>This Inner Loop Header: Depth=1
	vldr	s2, [r4, #16]
	vldr	s10, [r3, #16]
	vldr	s4, [r4, #20]
	vldr	s12, [r3, #20]
	vldr	s6, [r4, #24]
	vldr	s14, [r3, #24]
	vmul.f32	s2, s2, s10
	vldr	s8, [r4, #28]
	vldr	s1, [r3, #28]
	vadd.f32	s0, s2, s0
	vmul.f32	s2, s4, s12
	vadd.f32	s0, s2, s0
	vmul.f32	s2, s6, s14
	vadd.f32	s0, s2, s0
	vmul.f32	s2, s8, s1
	subs	r2, #4
	adds	r4, #16
	adds	r3, #16
	vadd.f32	s0, s2, s0
	cmp	lr, r2
	bne	.LBB0_3
```

한 줄씩 읽기:

- `vldr s2, [r4, #16]`: 단정밀도 레지스터 s2에 주소 `r4 + 16`의 float을 로드. r4는 a, r3은 b의 포인터(루프 전에 16을 빼 둬서 오프셋 16~28을 쓴다).
- **4배 unroll**: 한 반복에 a, b에서 4개씩(오프셋 16, 20, 24, 28) 로드하고 포인터를 16 B씩 올린다(`adds r4, #16`). 루프 카운터 `r2`는 4씩 감소(`subs r2, #4`). 나머지 0~3개는 루프 뒤 `.LBB0_8` 부분에서 따로 처리한다(생략).
- `vmul.f32` + `vadd.f32`: 곱과 덧셈을 **따로** 한다. 누산기는 **s0 하나** — 2.4절에서 말한 "컴파일러는 float 누산을 쪼개지 않는다"가 여기 그대로 보인다. M4는 VADD가 1 cycle이라 이 체인이 큰 손해는 아니다.
- load 사이에 `vmul`을 끼워 넣은 순서(`vldr … vmul … vldr`)는 컴파일러의 **명령 스케줄링**이다 — in-order 코어에서 load-use bubble을 줄이려는 배치다(1.2절).
- `subs … bne`: Thumb-2의 감소 후 조건 분기. M4에는 DSP처럼 zero-overhead loop 하드웨어가 없어서 이 명령들이 매 반복 실행된다(E4의 DSP와 대비).

**VFMA/VMLA는 왜 안 나왔나?** FPv4-SP ISA에는 `VMLA.F32`(곱한 뒤 반올림하고 더하는 chained multiply-accumulate)와 `VFMA.F32`(한 번만 반올림하는 fused multiply-add)가 모두 있다. 그런데 이 clang은 M4 대상에서 `-ffp-contract=fast`를 줘도 `vmul + vadd`를 골랐고, `__builtin_fmaf()`는 아예 라이브러리 함수 `fmaf` 호출(`b fmaf`)로 번역했다. 같은 코드를 `-mcpu=cortex-m7`(`.fpu fpv5-d16`)로 컴파일하면 기본 옵션에서 `vfma.f32`가 나온다.

```text
@ -mcpu=cortex-m7 -ffp-contract=fast, 안쪽 루프 일부
	vldr	s2, [r4, #16]
	vldr	s4, [r3, #16]
	vfma.f32	s0, s2, s4
	vldr	s2, [r4, #20]
	vldr	s4, [r3, #20]
	subs	r2, #4
	cmp	lr, r2
	vfma.f32	s0, s2, s4
```

M4에서 fused 명령을 피하는 이유는 LLVM의 Cortex-M4 튜닝 설정 때문으로 보이지만, 이 노트에서 확인한 것은 **출력뿐**이다. 실무 교훈: **MCU에서는 컴파일러가 실제로 무슨 명령을 냈는지 반드시 `-S`나 `objdump`로 본다.** 같은 C 코드가 코어·컴파일러 버전·플래그에 따라 전혀 다른 명령이 된다. 그리고 M7 출력도 누산기가 **s0 하나**다 — M7의 FMA latency가 1보다 크다면 여기서도 accumulator 분할이 이득이다(14절 연습).

`dot_s8`의 안쪽 루프(생성된 그대로):

```text
.LBB1_3:                                @ =>This Inner Loop Header: Depth=1
	add.w	r6, r9, r5
	add.w	r3, r8, r5
	ldrsb.w	r7, [r6, #3]
	ldrsb.w	r4, [r3, #3]
	ldrsb.w	r11, [r6, #4]
	smlabb	r10, r4, r7, r2
	ldrsb.w	r7, [r3, #4]
	ldrsb.w	r2, [r6, #5]
	ldrsb.w	r4, [r3, #5]
	smlabb	r7, r7, r11, r10
	smlabb	r2, r4, r2, r7
	ldrsb.w	r4, [r6, #6]
	ldrsb.w	r3, [r3, #6]
	adds	r5, #4
	add.w	r6, lr, r5
	adds	r6, #4
	smlabb	r2, r3, r4, r2
	bne	.LBB1_3
```

- `ldrsb`: 부호 있는 바이트 하나를 32-bit로 확장해 로드. **원소마다 load 한 번**이다.
- `smlabb r10, r4, r7, r2`: `r10 = r2 + (r4의 하위 16bit × r7의 하위 16bit)` — DSP 확장의 16×16+32 MAC. 원소 하나당 MAC 하나.
- 컴파일러는 **SIMD를 쓰지 않았다.** CMSIS-NN 같은 손 최적화 커널은 32-bit 한 번 로드로 int8 4개를 가져와 `SXTB16`으로 16-bit 두 쌍으로 펼치고, `SMLAD`(16×16 곱 두 개를 한 명령에 더함)로 **명령 하나에 MAC 2개**를 한다. 이 차이(자동 벡터화가 못 하는 것을 intrinsic/손 코드로)가 E2의 주제다.

### 9.4 함정

- M7의 D-cache를 켠 채 DMA 버퍼를 쓰면 coherence 문제(clean/invalidate 누락)가 생긴다 — Don은 이미 잘 알지만, ML 추론 버퍼(입력 오디오 프레임, 출력 텐서)도 똑같다. 추론 입력을 DMA로 받는다면 **DTCM에 두거나 non-cacheable 영역**에 두는 것이 가장 단순하다.
- "MCU는 in-order라서 마이크로아키텍처를 몰라도 된다"는 틀리다. flash wait state(예: 5 wait state면 분기마다 큰 손해), 버스 행렬(bus matrix) 경합, SRAM 뱅크 충돌이 MCU판 "캐시 miss"다.

---

## 10. 프로파일러 출력을 이 지식으로 읽기

K2에서 도구(perf, simpleperf, Streamline, Instruments, DWT)를 자세히 다룬다. 여기서는 **숫자를 보고 어느 절의 문제인지** 판단하는 법만 정리한다.

### 10.1 핵심 카운터와 파생 지표

| 지표 | 계산 | 의심할 것 | 이 노트의 실험 |
|---|---|---|---|
| IPC | instructions ÷ cycles | 낮으면(OoO 코어에서 < 1) 메모리 대기 또는 의존 체인 | 2절 체인 1개: FMA 파이프 1/16만 사용 |
| L1D MPKI | L1D refill × 1000 ÷ instructions | 높으면 지역성 문제 (루프 순서, 레이아웃, 타일) | 3.3절 stride, 4.2절 전치 |
| LLC/DRAM 대역폭 | (miss × 라인 크기) ÷ 시간 | 이론 대역폭의 70~80 %면 memory-bound 천장 | D3 5절, 3.3절 평평한 구간 |
| branch miss rate | mispredicts ÷ branches | 5 % 이상 + 핫루프면 branchless/벡터화 검토 | 7절 비정렬 50 % |
| dTLB miss | TLB refill ÷ 접근 | 큰 작업 집합의 흩어진 접근 | 3.4절 페이지당 1라인 |
| stall 비율 | backend stall cycles ÷ cycles | 연산 유닛은 있는데 입력이 안 옴 | 6절 MLP |

ARMv8 PMU에는 아키텍처로 정해진 공통 이벤트 번호가 있다. 예: `0x11 CPU_CYCLES`, `0x08 INST_RETIRED`, `0x03 L1D_CACHE_REFILL`, `0x04 L1D_CACHE`, `0x05 L1D_TLB_REFILL`, `0x10 BR_MIS_PRED`, `0x12 BR_PRED`. Linux `perf stat -e cycles,instructions,branch-misses,cache-misses`나 Android `simpleperf stat`이 이 카운터를 읽는다. 어느 이벤트가 구현되어 있는지는 코어마다 다르므로 TRM을 확인한다. Cortex-M3/M4/M7은 DWT에 `CYCCNT` 외에 `CPICNT`(추가 cycle), `LSUCNT`(load/store 추가 cycle), `EXCCNT`, `SLEEPCNT`, `FOLDCNT` 같은 8-bit 카운터가 있다.

### 10.2 손계산 — 프로파일 읽기 연습

어떤 conv 레이어를 A7x급 코어에서 쟀더니: cycles 2.0 × 10⁹, instructions 1.0 × 10⁹, L1D refill 4.0 × 10⁷, branch miss 1.0 × 10⁵.

```
IPC          = 1.0e9 ÷ 2.0e9 = 0.5              → OoO 코어치고 낮다
L1D MPKI     = 4.0e7 × 1000 ÷ 1.0e9 = 40        → 매우 높다 (잘 타일된 GEMM은 한 자릿수 이하가 흔함)
branch miss  = 1.0e5 → 1 miss당 20 cycle 잡아도 2e6 cycle = 전체의 0.1 %  → 무시
L1 refill 바이트 = 4.0e7 × 64 B = 2.56 GB,  1 s(= 2e9 cycle ÷ 2 GHz) 동안 → 2.56 GB/s
```

말로 하면: 분기는 범인이 아니다. IPC가 낮고 L1 miss가 많으니 **지역성 문제**다. 다음 확인 순서: (1) 텐서 레이아웃(NCHW인데 커널이 NHWC를 기대?), (2) im2col 버퍼가 L2를 넘나?, (3) 2의 거듭제곱 pitch로 conflict가 나나?(4.2절), (4) 이 레이어가 원래 memory-bound인가(D3 roofline으로 intensity 계산).

### 10.3 판단 흐름

```
느린 커널 발견 (K2: 레이어별 시간)
 │
 ├─ roofline(D3)으로 기대 성능 계산 ─ 이미 천장 근처? ─ 예 → 알고리즘/양자화/fusion (C, D3 7절)
 │                                                   └ 아니오 ↓
 ├─ IPC 낮고 cache miss 많음 → 3, 4절: 순서·타일·layout·padding
 ├─ IPC 낮고 miss 적음      → 2절: 의존 체인 → accumulator, unroll, SIMD 확인(objdump)
 ├─ branch miss 높음        → 7절: branchless, 벡터 마스크, 데이터 정렬
 ├─ 멀티스레드에서만 느림    → 8절: false sharing, 코어 이동(P/E), 스레드 수
 └─ 실행마다 크게 다름      → 8.2절·D6: 코어 이동, DVFS 워밍업, 공유 캐시 간섭
```

---

## 11. 임베디드 관점에서 다시 보기

같은 dense layer(예: 256 → 128, int8)가 타깃마다 어떤 마이크로아키텍처 문제로 바뀌는지 정리하면:

| 타깃 | 주된 병목 | 이 노트의 도구 | 전형적 처방 |
|---|---|---|---|
| Cortex-M4 (SRAM, 캐시 없음) | 명령 수, load 비용 | 9.2 손계산, 9.3 asm | SIMD(SMLAD), 32-bit 로드로 4개씩, weight를 SRAM/TCM에 |
| Cortex-M7 (캐시 + TCM) | 캐시 miss로 인한 타이밍 흔들림, dual-issue 활용 | 9.1 표 | 핫 코드 ITCM, 버퍼 DTCM, accumulator 분할 |
| Cortex-A55 (in-order) | load-use stall, miss를 겹치지 못함 | 1.2, 5.2절 | 컴파일러 스케줄링, software prefetch가 효과 있을 가능성 큼 |
| Cortex-A7x / Apple P-core (OoO) | 의존 체인, L2 넘는 작업 집합 | 2, 3, 6절 | register blocking, 캐시 tiling, 스레드를 big 코어에 |
| DSP (E4) | VLIW 슬롯 채우기, TCM ↔ DDR DMA | 2절(같은 Little's law) | software pipelining, double buffering |
| NPU (E5) | CPU는 전처리·후처리·fallback op 담당 | 7절(후처리 분기), 8절 | 후처리 벡터화, CPU 스레드 수 제한(NPU 드라이버 스레드와 경합) |

Hark 같은 웨어러블이라면(추정), always-on MCU에서 VAD/wake word를 돌리고 무거운 모델은 AP의 NPU/DSP로 넘긴다. 이때 MCU 쪽은 **결정적 cycle 예산**(9절), AP 쪽 CPU는 **NPU가 못 하는 op과 후처리를 얼마나 빨리 끝내느냐**(2~8절)가 end-to-end 지연을 좌우한다(D6 3절).

C로 보는 한 줄 요약 — M4에서 accumulator 분할 + 4개 단위 처리의 모양. 무엇을 확인하나: 누산기를 2개로 나누면 (1) M4 어셈블리에서 실제로 누산 레지스터가 둘로 갈리는지, (2) 호스트에서 결과 비트가 1-누산기 버전과 달라지는지.

```c
/* 4개씩 처리하면서 누산기를 2개로 나눈 모양 — 실제 효과는 타깃에서 DWT로 잰다 */
float dot_2acc(const float *a, const float *b, int n) {
    float s0 = 0.0f, s1 = 0.0f;
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        s0 += a[i] * b[i];     s1 += a[i + 1] * b[i + 1];
        s0 += a[i + 2] * b[i + 2]; s1 += a[i + 3] * b[i + 3];
    }
    for (; i < n; i++) s0 += a[i] * b[i];
    return s0 + s1;            /* 덧셈 순서가 바뀌므로 결과 비트가 원래와 다를 수 있다 */
}
```

```sh
clang --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfloat-abi=hard -O2 -Wall -Wextra -S dot2acc.c -o dot2acc.s
# 호스트: 같은 함수 + 누산기 1개 버전(dot_1)을 n = 103, a[i] = 0.01·i, b[i] = 1 − 0.003·i 로 비교
cc -std=c11 -Wall -Wextra -O2 t2.c dot2acc.c -o t2 && ./t2
```

M4 어셈블리 안쪽 루프 일부(실제 출력, 경고 0개)와 호스트 실행 출력:

```text
	vmul.f32	s4, s4, s12
	vldr	s10, [r7, #12]
	vldr	s3, [r5, #12]
	vadd.f32	s2, s4, s2
	vmul.f32	s4, s6, s14
	vadd.f32	s0, s4, s0
```

```text
1acc=41.761349 2acc=41.761353
```

출력에서 볼 것: 누산이 `s2`와 `s0` 두 레지스터로 번갈아 간다(컴파일러는 이 루프를 16개 단위로 더 unroll했다). 호스트 결과는 마지막 자리가 다르다 — 수학적으로 같은 식이어도 **float 덧셈 순서가 바뀌면 비트가 바뀐다.** 레퍼런스(PyTorch)와 비트 단위로 비교하는 검증(C8)에서는 이 차이를 허용 오차로 다뤄야 한다. M4에서 실제로 빨라지는지는 VADD가 1 cycle이라 작을 것으로 예상되지만, 보드에서 DWT로 재야 안다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 누산기 하나로 내적/GEMM 작성 | 앱 코어에서 peak의 1/4~1/16 | FMA latency × 발행 폭만큼 독립 체인이 없음 (2절) | accumulator 여러 개, register blocking, BLAS 사용 |
| 행 pitch가 2의 거듭제곱인 행렬을 열 방향으로 접근 | 작업 집합이 작은데도 L1 miss 폭증 | conflict miss — 같은 set에 몰림 (3.4, 4.2절) | 행 끝 padding (예: +128 B), 타일 크기 조정 |
| 타일을 L1 크기에 꽉 맞춤 | 타일 키웠더니 오히려 느려짐 | 다른 데이터·set 충돌로 넘침 (4.2절 T=128) | L1의 1/2~2/3 목표, 스윕해서 결정 |
| software prefetch를 습관적으로 추가 | 순차 루프가 40 % 느려짐 | HW prefetcher가 이미 하는 일에 명령만 추가 (5.2절) | 측정해서 이득일 때만, 거리 = latency ÷ 반복 시간 |
| 랜덤 데이터에 대한 if 분기 | 정렬된 입력에서는 빠른데 실데이터에서 10배 느림 | misprediction ≈ 18 cycle × 50 % (7절) | select/min/max, 벡터 마스크 |
| 스레드별 카운터·부분합을 배열에 붙여 둠 | 스레드를 늘리면 오히려 느려짐 | false sharing (8.1절) | 128 B 정렬·padding, 스레드 로컬 변수 후 마지막에 합치기 |
| 첫 실행 시간을 결과로 보고 | 결과가 2배씩 흔들림 | DVFS ramp, 캐시·TLB cold, 코어 이동 (8.3절) | 워밍업, 반복, min/median/분포 보고 |
| 컴파일러 출력을 확인하지 않음 | 벤치마크가 너무 빠르거나(루프 삭제) 기대와 다름 | 상수 전파·벡터화·라이브러리 호출 (2.3, 9.3절) | `objdump -d`/`-S` 확인, 결과를 출력해 dead code 방지 |
| macOS에서 코어 고정을 가정 | E-core에 올라간 실행이 섞여 이중 분포 | QoS는 힌트일 뿐 (8.3절) | 코어 고정이 필요한 측정은 Linux/Android 기기에서 `sched_setaffinity` |

---

## 13. 면접에서 이렇게 말한다

**Q.** Why do GEMM kernels use multiple accumulators?

**A.** FMA 하나의 latency가 여러 cycle이고, 코어는 cycle마다 여러 FMA를 발행할 수 있다. 누산기가 하나면 매 FMA가 앞 결과를 기다려 파이프가 거의 빈다. 필요한 독립 누산기 수는 latency × 발행 폭(Little's law)이다. M2에서 직접 재 보니 스칼라 FMA latency 4 cycle, 발행 4개/cycle이라 16개가 필요했고, 1개 → 16개로 15배 빨라졌다. GEMM은 C의 mr×nr 타일을 레지스터에 누산기로 들고 있어서 이 문제와 load 재사용을 동시에 푼다.

> "An FMA has multi-cycle latency but the core can issue several per cycle, so a single accumulator serializes everything on the dependency chain. You need roughly latency times issue width independent chains — on an M2 I measured 4-cycle latency and 4 FMA pipes, so 16 accumulators, and going from 1 to 16 gave about 15x. GEMM micro-kernels hold an mr-by-nr tile of C in registers, which both hides the latency and reuses every loaded A and B value many times."

**Q.** What is a cache line and why does traversal order matter?

**A.** 캐시가 메모리와 주고받는 최소 단위다(이 Mac은 128 B, Cortex-A/M7은 보통 32~64 B). 1바이트만 읽어도 라인 전체를 가져오므로, 라인 안의 바이트를 다 쓰는 순서(row-major 배열을 행 방향으로)로 걸어야 대역폭을 버리지 않는다. stride 실험에서 128 B까지는 총 시간이 같고 그 이후로 절반씩 줄어드는 것을 봤고, 행렬곱 루프 순서만 바꿔 12배, 전치에 타일과 padding을 넣어 9배 차이를 봤다.

> "A cache line is the unit of transfer between cache and memory — 64 or 128 bytes. Touching one byte costs the whole line, so traversal order decides how many of those bytes you actually use. Walking a row-major matrix along rows uses every byte; walking down columns wastes most of each line and can also hit conflict misses when the row pitch is a power of two. I've seen a 12x difference from loop order alone in a naive matmul, and 9x from tiling plus padding in a transpose."

**Q.** How would you measure cache latency?

**A.** pointer chasing을 쓴다. 버퍼에 랜덤 순열로 원형 연결 리스트를 만들어서(라인 하나에 노드 하나) 다음 주소가 이번 load 결과가 되게 하면, OoO도 prefetcher도 앞서 갈 수 없어서 순수한 load-to-use latency가 나온다. 작업 집합을 4 KiB부터 수백 MiB까지 키우며 load당 ns를 찍으면 계단이 나오고, 단의 높이가 레벨별 latency, 절벽 위치가 캐시 크기다. 반복해서 최소값을 쓰고, 공유 캐시 경계는 다른 작업에 흔들린다는 점을 주의한다.

> "Pointer chasing: build a random cyclic linked list, one node per cache line, so each load's address depends on the previous load. That defeats both out-of-order overlap and the prefetcher. Sweep the working set from a few KB to hundreds of MB and plot nanoseconds per load — you get a staircase whose steps are the per-level latencies and whose edges are the cache sizes. On an M2 I got about 3 cycles for L1, around 20 for L2 and roughly 110 ns for DRAM. Repeat and take the minimum, and expect noise near shared-cache boundaries."

**Q.** Branchy vs branchless — when does it matter?

**A.** 분기 방향이 예측 불가능할 때(조건 확률이 대략 10~90 %이고 데이터가 랜덤)만 크게 중요하다. M2에서 랜덤 바이트 threshold 합은 branchy 3.0 ns, branchless 0.32 ns로 약 10배였지만, 같은 데이터를 정렬하거나 조건이 2 %만 참이면 branchy도 0.3~0.5 ns로 충분했다. branchless는 양쪽을 다 계산하므로 잘 예측되는 분기에서는 약간 손해다. 가장 좋은 건 벡터화 — SIMD는 원래 마스크로 동작한다. ML에서는 ReLU·clamp는 min/max 명령으로, 검출 후처리의 threshold·NMS는 벡터 비교와 후보 압축으로 처리한다.

> "It matters when the branch is unpredictable — roughly when the condition is true between 10 and 90 percent of the time on random data. I measured about 10x on an M2 for a threshold-sum over random bytes: 3 ns per element branchy versus 0.3 branchless, but sorted input or a 2-percent condition made the branchy version just as fast. Branchless computes both sides, so it slightly loses when prediction is good. The best option is usually vectorizing, since SIMD works with masks anyway — ReLU and clamps become max/min instructions and score thresholding becomes a vector compare."

**Q.** In-order MCU vs out-of-order application core for ML kernels?

**A.** OoO 코어는 독립 명령과 여러 cache miss를 겹쳐서 평균 성능이 높지만, 캐시·예측기·DVFS 때문에 실행 시간이 흔들린다. Cortex-M4 같은 in-order MCU는 3단 파이프라인에 캐시가 없어 SRAM/TCM 위의 루프 시간을 cycle 단위로 예측할 수 있다. 대신 miss를 숨기지 못하므로 데이터를 DMA로 TCM에 미리 올려 두고, 컴파일러 스케줄링과 SIMD 명령(SMLAD)을 직접 챙겨야 한다. 그래서 always-on 저전력 경로(VAD, wake word)는 결정적 타이밍의 MCU/DSP에, 무거운 모델은 OoO 코어·NPU에 둔다.

> "An out-of-order core overlaps independent work and many cache misses, so average throughput is high, but timing varies with caches, prediction and DVFS. An in-order MCU like a Cortex-M4 has a three-stage pipeline and no cache, so a loop in SRAM or TCM has cycle-predictable timing — but nothing hides a stall, so you stage data into TCM with DMA and make sure the compiler actually emits SIMD MACs like SMLAD; I always check the generated assembly. That's why always-on audio stages sit on MCUs or DSPs with deterministic budgets, and heavier models go to the big cores or the NPU."

**Q.** What is false sharing and how do you avoid it?

**A.** 서로 다른 스레드가 서로 다른 변수를 쓰는데 그 변수들이 같은 캐시 라인에 있어서, coherence 프로토콜이 라인을 코어 사이로 계속 옮기는 현상이다. M2에서 두 스레드가 인접 카운터를 증가시키면 145 ms, 128 B 떨어뜨리면 31 ms였다. 스레드별 데이터를 라인 크기로 정렬·padding하거나, 스레드 로컬 변수에 모았다가 마지막에 한 번 합친다.

> "Two threads write different variables that happen to share a cache line, so the coherence protocol keeps bouncing the line between cores. With two threads incrementing adjacent counters on an M2 I measured about 145 ms versus 31 ms once they were 128 bytes apart. The fix is aligning or padding per-thread data to the line size, or accumulating in thread-local variables and combining once at the end."

---

## 14. 직접 해보기

1. **손계산**: FMA latency 3 cycle, cycle당 FMA 2개를 발행하는 코어에서 fp32 NEON(4 lane) 내적의 최대 속도를 내려면 벡터 accumulator가 몇 개 필요한가? 클럭 2 GHz에서 그때 GFLOP/s는? 정답: 3 × 2 = 6개. 2 FMA/cycle × 4 lane × 2 FLOP × 2 GHz = 32 GFLOP/s.
2. **손계산**: L1 64 KiB, 4-way, 64 B 라인. set 수, index 비트 위치, 같은 set으로 가는 주소 간격은? 정답: 64 KiB ÷ (64 × 4) = 256 set, offset bit 0~5, index bit 6~13, 간격 64 × 256 = 16 KiB.
3. **손계산**: DRAM latency 120 ns, 라인 64 B, 목표 대역폭 20 GB/s. 동시에 몇 개의 miss가 날아가야 하나? in-order 코어가 동시에 4개만 기다릴 수 있다면 최대 대역폭은? 정답: 20e9 × 120e-9 ÷ 64 ≈ 37.5개. 4 × 64 B ÷ 120 ns ≈ 2.1 GB/s.
4. **코드 과제**: 6.2절 `mlp.c`의 체인 수를 64, 128까지 늘려 이 코어의 동시 miss 한계(포화 지점)를 찾아라. 배열 크기를 늘려야 할 수도 있다(`void **p[..]` 크기 주의). 힌트: 포화하면 ns/load가 더 이상 절반이 되지 않는다. 그 지점의 "loads in flight"가 한계의 추정치다.
5. **코드 과제**: 9.3절 `dot_f32`를 누산기 4개로 나눠 `-mcpu=cortex-m7 -ffp-contract=fast -O2 -S`로 컴파일하고, 안쪽 루프에서 `vfma.f32`의 목적 레지스터가 몇 개로 바뀌는지 확인하라. 정답: s0 하나 대신 서로 다른 목적 레지스터 4개가 나와야 한다(레지스터 번호는 컴파일러가 정한다).
6. **코드 과제**: 4.2절 전치에서 LD를 4096 + 16(64 B 패딩), 4096 + 1(4 B 패딩)로 바꿔 보고 타일 32의 시간을 비교하라. 라인 크기의 배수가 아닌 padding은 어떤 추가 비용이 있을까? 힌트: 4 B 패딩도 set 충돌은 풀지만 행 시작이 라인 경계에 정렬되지 않아 SIMD 로드가 라인을 가로지를 수 있다. 결과는 재 봐야 안다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| pipeline | 명령 처리 단계를 겹쳐 실행 | latency는 그대로, throughput을 올린다 |
| IPC / CPI | cycle당 명령 수 / 그 역수 | 코어가 얼마나 바쁜지. ML에서는 FLOP/cycle과 함께 본다 |
| hazard | 다음 명령이 못 들어가는 상황 | structural / data / control |
| forwarding (bypass) | 결과를 레지스터 쓰기 전에 바로 전달 | data hazard 대부분을 없앤다. load-use는 남는다 |
| superscalar | cycle당 여러 명령 발행 | in-order dual-issue(M7, A55)도 superscalar다 |
| out-of-order (OoO) | 준비된 명령부터 실행 | ROB에서 순서대로 retire. 독립 작업과 miss를 겹친다 |
| register renaming | 아키텍처 레지스터를 물리 레지스터로 다시 매핑 | 가짜 의존(WAR/WAW)을 없앤다 |
| ROB | reorder buffer | OoO의 명령 창. 크기가 "얼마나 앞서 볼 수 있나" |
| latency / throughput | 결과까지 걸리는 시간 / 단위 시간당 완료 수 | 필요 병렬도 = latency × throughput |
| FMA | fused multiply-add | a × b + c를 한 번의 반올림으로 |
| accumulator | 누산 레지스터 | 독립 누산기 수 = 독립 체인 수 |
| register blocking | C 타일을 레지스터에 누산기로 유지 | GEMM 마이크로커널의 핵심 |
| cache line | 캐시 전송 단위 | 64 B / 128 B가 흔하다 |
| set / way | 캐시 index로 고르는 묶음 / 그 안의 칸 수 | 같은 set에 way보다 많이 몰리면 conflict miss |
| 3C miss | compulsory / capacity / conflict | miss의 원인 분류 |
| TLB | 주소 변환 캐시 | 페이지 수가 많으면 miss → page walk |
| page walk | page table을 따라가 변환을 찾음 | 수~수십 ns 추가 |
| hardware prefetcher | 패턴을 보고 미리 가져옴 | 연속·일정 stride에 강함 |
| software prefetch | `__builtin_prefetch` → `PRFM` | 힌트. 측정해서 이득일 때만 |
| MLP | 동시에 진행 중인 miss 수 | 대역폭 = MLP × 라인 ÷ latency |
| store buffer | retire된 store가 캐시로 가기 전 대기열 | store-to-load forwarding |
| branch prediction | 분기 방향·대상을 미리 추측 | 틀리면 파이프라인 flush |
| misprediction penalty | 예측 실패 손해 | M2 실측 실효 약 18~19 cycle |
| branchless | 비교를 값(CSEL/마스크)으로 | 예측 불가 분기에서 이득 |
| cache coherence | 코어 간 캐시 일관성 | 라인 단위로 동작 |
| false sharing | 다른 변수가 같은 라인을 공유해 핑퐁 | 라인 크기 정렬로 해결 |
| big.LITTLE / P·E-core | 이종 코어 구성 | 코어 이동이 벤치마크 분포를 흔든다 |
| QoS class | macOS 스레드 우선순위 힌트 | 코어 고정 API가 아니다 |
| TCM | 코어 옆 고정 주소 SRAM | 결정적 접근 시간 |
| im2col | conv 입력 패치를 행렬로 펼침 | conv → GEMM. 메모리 k²배 팽창 |
| tiling / blocking | 작업을 캐시 크기 블록으로 자름 | 재사용을 가까운 레벨에서 |
| MPKI | misses per kilo-instruction | 프로파일에서 지역성 문제 판단 |

---

## 16. 요약 & 체크리스트

CPU 코어는 파이프라인으로 명령을 겹쳐 실행하고, OoO 코어는 여기에 더해 독립 명령과 여러 cache miss를 겹친다. ML 커널이 코어를 채우려면 **독립 작업이 latency × 폭만큼** 있어야 한다 — FMA에서는 accumulator 수(M2: 4 × 4 = 16, 1 → 16개로 15배), 메모리에서는 동시 miss 수(1 → 32 체인으로 24배)다. 데이터는 가까운 캐시에 있어야 한다: M2에서 L1 3 cycle, L2 약 20 cycle, DRAM 약 110 ns로 100배 넘게 차이 나고, 라인(128 B)을 다 쓰는 순서, 타일, 2의 거듭제곱 pitch를 피하는 padding이 각각 수 배씩을 좌우한다. 예측 불가능한 분기는 약 18 cycle씩 손해를 보므로 branchless나 벡터 마스크로 바꾸고, 스레드 간에는 false sharing을 피한다. in-order MCU는 이런 것을 숨겨 주지 않는 대신 타이밍이 결정적이어서, 컴파일러 출력을 직접 읽고 데이터를 TCM에 배치하는 것이 최적화의 중심이다. 그리고 모든 결론은 **측정으로 확인한다** — 이 노트의 software prefetch와 스레드 QoS처럼 교과서 기대와 다른 결과가 나오는 경우가 있다.

- [ ] 5단 파이프라인에서 load-use hazard와 분기 실패를 포함한 cycle 수를 손으로 계산할 수 있다
- [ ] "필요 accumulator 수 = FMA latency × 발행 폭"을 유도하고 실측 그래프로 설명할 수 있다
- [ ] pointer chasing 벤치마크를 직접 짜서 캐시 계층 계단을 그리고 각 단의 cycle을 읽을 수 있다
- [ ] stride 실험 결과에서 캐시 라인 크기를 읽어 낼 수 있다
- [ ] 캐시 set 수·index 비트·같은 set 간격을 손으로 계산하고 2의 거듭제곱 pitch의 위험을 설명할 수 있다
- [ ] 전치·GEMM에서 tiling과 padding이 왜 효과가 있는지, 타일 크기를 어떻게 고르는지 말할 수 있다
- [ ] Little's law로 목표 대역폭에 필요한 동시 miss 수를 계산할 수 있다
- [ ] software prefetch가 효과 있는 경우와 없는 경우를 이 노트의 측정으로 설명할 수 있다
- [ ] branchy vs branchless의 손익 분기와 ML 후처리에서의 선택을 설명할 수 있다
- [ ] Cortex-M4/M7용 `-S` 어셈블리를 읽고 unroll, 누산기, VFMA/SMLABB 같은 명령을 짚을 수 있다

---

## 참고 자료

- John L. Hennessy, David A. Patterson, "Computer Architecture: A Quantitative Approach" — 파이프라인, OoO, 캐시, 분기 예측의 표준 교재
- David A. Patterson, John L. Hennessy, "Computer Organization and Design: ARM Edition" — 5단 파이프라인과 hazard를 ARM 명령으로 설명
- Ulrich Drepper, "What Every Programmer Should Know About Memory" (2007) — 캐시·TLB·prefetch 실험의 고전. https://www.akkadia.org/drepper/cpumemory.pdf
- Agner Fog, "The microarchitecture of Intel, AMD and VIA CPUs" 및 최적화 매뉴얼 — latency/throughput 측정 방법론 (x86 중심). https://www.agner.org/optimize/
- Kazushige Goto, Robert A. van de Geijn, "Anatomy of High-Performance Matrix Multiplication", ACM TOMS (2008) — GEMM blocking 계층의 원전
- Arm, "Cortex-M4 Technical Reference Manual", "Cortex-M7 Technical Reference Manual" — 파이프라인, FPU 명령 타이밍, DWT 카운터 (developer.arm.com)
- Arm, "Arm Architecture Reference Manual for A-profile" — PMU 공통 이벤트 번호, PRFM 명령
- Apple, "Apple Silicon CPU Optimization Guide" — Apple 코어용 최적화 권고 (developer.apple.com)
- MIT 6.5940 TinyML and Efficient Deep Learning (Song Han) — MCU 추론에서 메모리 계층과 im2col·tiling
- 이 노트 세트: A1 8절(루프 순서), D3 5~6절(대역폭·roofline 실측), D6 2·4절(벤치마크 하니스), E2(NEON/Helium), E7(메모리 시스템), K2(프로파일링 도구), K5(커널 최적화 기법)
