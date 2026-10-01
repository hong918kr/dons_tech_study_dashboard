# E5. NPU 아키텍처 — MAC 배열, systolic array, dataflow, 온칩 SRAM, tiling, 컴파일러

> **이 노트를 다 읽으면**: NPU가 CPU·GPU보다 MAC당 에너지가 수십~수백 배 싼 이유를 명령어 오버헤드와 데이터 재사용으로 설명하고, TOPS 숫자를 "MAC/cycle × 2 × 주파수"로 분해할 수 있다 · weight-stationary systolic array를 사이클 단위로 시뮬레이션해서 skew, fill/drain, 모양별 활용률(GEMM 94% vs depthwise·GEMV 2~3%)을 직접 보일 수 있다 · weight-/output-/input-/row-stationary dataflow가 무엇을 PE에 붙잡아 두는지와 버퍼 접근 횟수를 세고, SRAM 크기에 맞춘 tiling·double buffering·layer fusion을 계산할 수 있다 · NPU 컴파일러 파이프라인(partition → 양자화 제약 → tiling → command stream)과 CPU fallback의 비용을 설명하고, 새 NPU와 SDK를 bring-up하는 체크리스트와 칩 평가 질문을 말할 수 있다
> **JD 연결**: "Work with platform vendors to bring up toolchains, SDKs and new accelerator" · "Deploying workloads on NPUs or specialized accelerators" · "Evaluate and select silicon platforms (GPUs, NPUs etc.)" · study_prep_list **E5** 행: MAC array, systolic array, dataflow(weight-/output-/row-stationary), on-chip SRAM·DMA·tiling, 지원 op·정밀도 제약, 컴파일러의 역할, Ethos-U55/U65/U85·Hexagon NPU·Apple ANE·Edge TPU·NXP Neutron·MediaTek APU. **F8**(벤더 SDK bring-up), **M1**(평가 기준표)과 직결
> **Don 기준 난이도**: 새 실리콘 IP를 FPGA pre-silicon에서 bring-up하고 레지스터·DMA·메모리 맵·벤더 errata와 씨름한 경험은 그대로 무기다 / NPU 내부(PE 격자, dataflow, 재사용, tiling)와 ML 컴파일러가 모델을 command stream으로 바꾸는 과정은 새로 배운다
> **선행 노트**: D1(MAC 세기), D2(peak activation·arena), D3(roofline·arithmetic intensity·SRAM이 intensity 상한을 정한다), D4 3.5절(systolic 활용률 장난감 모델), D7(연산당 에너지·Horowitz 표·Eyeriss 상대 비용), C1·C2(int8 양자화), C6(graph 최적화·fusion·CPU fallback), C7(HW-aware 설계), C8(bit-exact 검증). 병렬 작성 중인 E4(DSP·Hexagon), E6(GPU), E7(메모리 시스템), E8(이기종 SoC)은 ID로만 가리킨다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 컨트롤러에서 새 IP(예: 새 ECC 엔진, 새 NAND 인터페이스)를 FPGA에 올려 bring-up하던 장면을 떠올려 보자. 먼저 클럭·리셋이 살아 있는지 보고, 레지스터를 읽어 ID를 확인하고, DMA로 버퍼 하나를 보내서 결과가 golden model과 비트 단위로 같은지 비교했다. 그다음 처리량을 재고, 스펙 대비 몇 퍼센트인지 따지고, 벤더와 errata를 주고받았다.

**NPU bring-up은 이 과정과 거의 같다.** 다른 점은 "그 IP가 무엇을 하는가"다. NPU(Neural Processing Unit)는 신경망의 핵심 연산인 **MAC(multiply-accumulate, 곱하고 더하기)** 을 수백~수천 개 병렬로 하는 **고정 기능 가속기**다. ECC 엔진이 "코드워드를 받아 오류를 고친다"는 한 가지 일만 극도로 효율적으로 하듯이, NPU는 "int8 행렬곱과 convolution"을 극도로 효율적으로 한다.

Hark JD의 세 문장이 이 노트의 뼈대다.

- "bring up toolchains, SDKs and new accelerator" → 8절 bring-up 체크리스트, 6절 컴파일러
- "deploying workloads on NPUs" → 3~5절: 왜 어떤 모델은 NPU에서 빠르고 어떤 모델은 느린가 (활용률, dataflow, tiling)
- "evaluate and select silicon (NPUs)" → 1.5절 TOPS 해석, 9절 평가 질문

NPU가 기기 안에서 어디에 있는지부터 보자. 예를 들어 Hark 같은 웨어러블이라면(추정), 항상 켜진 저전력 MCU 옆에 작은 microNPU가 있을 수도 있고, 메인 SoC 안에 큰 NPU가 있을 수도 있다.

```
             ┌──────────────────────── SoC ────────────────────────┐
  센서 ──►   │  always-on MCU ──(+ microNPU?)     AP (Cortex-A)     │
  (mic, IMU) │        │                               │             │
             │        ▼            interconnect       ▼             │
             │   [SRAM 수백 KB] ◄═══════════════► [NPU] [DSP] [GPU] │
             │                        ║                            │
             └────────────────────────╫────────────────────────────┘
                                      ▼
                                LPDDR (수 GB)  ← 모델 weight, 큰 activation
```

이 노트의 질문은 하나로 요약된다. **"MAC 수천 개를 달아 놓았는데, 왜 실제로는 그중 일부만 일하고, 어떻게 하면 더 많이 일하게 만들 수 있나?"** 답은 세 단어다: **재사용(reuse), 모양(shape), 데이터 이동(data movement)**.

이 노트에서 쓰는 약어를 미리 정리한다.

| 약어 | 풀이 | 한 줄 뜻 |
|---|---|---|
| PE | processing element | MAC 하나 + 작은 레지스터. 배열의 한 칸 |
| GLB | global buffer | NPU 안의 큰 on-chip SRAM (수십 KB~수 MB) |
| RF | register file | PE 안의 아주 작은 저장소 (수~수백 바이트) |
| WS / OS / IS / RS | weight- / output- / input- / row-stationary | PE에 무엇을 붙잡아 두는지에 따른 dataflow 이름 |
| DMA | direct memory access | DRAM ↔ SRAM 복사를 CPU 없이 하는 엔진 |
| command stream | 명령 스트림 | 컴파일러가 만든, NPU가 순서대로 실행하는 명령 목록 |

---

## 1. 왜 NPU인가 — CPU·GPU의 오버헤드와 고정 기능 MAC 배열

### 1.1 직관 — 장인과 컨베이어

CPU로 MAC 하나를 하는 과정을 펌웨어 엔지니어의 눈으로 보자. `acc += a[i] × w[i]` 한 줄이 실제로는 이렇게 된다.

```
1. 명령어 fetch (I-cache 읽기)       ← 에너지
2. decode, 레지스터 파일 읽기         ← 에너지
3. load a[i]  (D-cache 읽기, 주소 계산) ← 에너지
4. load w[i]  (D-cache 읽기)          ← 에너지
5. MLA (곱 + 덧셈)                    ← 진짜 일은 여기뿐
6. 레지스터 쓰기, 루프 카운터, 분기      ← 에너지
```

CPU는 **매번 작업지시서를 새로 읽는 장인**이다. 무엇이든 할 수 있지만, 곱셈 하나를 할 때마다 지시서를 읽고(fetch), 해석하고(decode), 창고에서 재료를 가져온다(load). NPU는 **컨베이어 벨트**다. 할 수 있는 일은 정해져 있지만(행렬곱, conv, 몇 가지 activation), 일단 벨트가 돌면 지시서 없이 재료가 흘러가며 곱해진다.

GPU는 그 중간이다. 명령 하나(warp 단위)로 32개 스레드가 같은 일을 하니 명령 오버헤드가 나뉘지만, 범용 레지스터 파일, 캐시 계층, 스케줄러의 비용은 남아 있다. 그리고 GPU는 수천 개 스레드를 채울 만큼 일이 많아야(batch가 커야) 효율이 난다. batch 1 추론이 많은 edge에서는 이게 약점이다(E6).

### 1.2 명령어 오버헤드를 숫자로

D7 2절의 Horowitz 표(45 nm)를 다시 쓴다. int8 곱 0.2 pJ, int32 덧셈 0.1 pJ이니 **int8 MAC 한 번의 산술은 약 0.3 pJ**이다. 반면 범용 프로세서가 명령어 하나를 처리하는 비용(fetch, decode, 레지스터 파일, 파이프라인 제어)은 **수십 pJ** 자릿수다(D7 2.5절). 여기서는 명령당 50 pJ로 가정한다.

손계산 세 가지를 해 보자.

```
CPU scalar : MAC 1개에 명령 약 3개(load 2 + MLA 1)
             3 × 50 pJ + 피연산자 2 B × 2.5 pJ/B(32 KB L1) + 0.3 pJ ≈ 155 pJ/MAC
CPU SIMD   : 명령 하나에 int8 MAC 16개 (예: 128-bit 벡터 dot-product)
             3 × 50 / 16 + 2 × 2.5 + 0.3 ≈ 14.7 pJ/MAC
NPU        : 명령 없음. 피연산자를 8 KB 로컬 버퍼(1.25 pJ/B)에서 가져오되,
             한 번 가져온 값을 16번 재사용한다면 2 B / 16 × 1.25 + 0.3 ≈ 0.46 pJ/MAC
```

말로 하면: CPU scalar에서는 에너지의 99% 이상이 "지시서 읽기"에 쓰이고, SIMD는 그것을 16분의 1로 나누지만 캐시 접근이 남는다. NPU는 지시서를 없애고, 재료를 PE 근처에 두고 여러 번 쓰는 것(재사용)으로 메모리 비용까지 나눈다. **NPU의 효율 = 명령 제거 × 데이터 재사용**이다.

### 1.3 TOPS 숫자 만들기

데이터시트의 "X TOPS"는 대부분 이 식이다.

```
peak TOPS = (MAC 개수 / cycle) × 2 op/MAC × 주파수(Hz) / 10¹²
```

말로 하면: MAC 하나를 곱셈 1 op + 덧셈 1 op로 두 번 센다. 256 MAC/cycle짜리 NPU가 500 MHz로 돌면 256 × 2 × 5×10⁸ = 2.56×10¹¹ op/s = 0.256 TOPS다. 이 숫자는 **모든 MAC이 매 사이클 쉬지 않고 일할 때**의 값이다(peak).

### 1.4 코드로 확인 — TOPS와 MAC당 에너지 (예제 1)

TOPS 식과 1.2절의 손계산을 코드로 한 번에 확인한다. 에너지 상수는 D7의 45 nm 값과 명령당 50 pJ 가정이다.

```python
# TOPS 숫자 만들기 + "MAC 하나"에 드는 에너지를 CPU scalar / CPU SIMD / NPU로 비교 (45 nm Horowitz급 가정값)
def tops(macs_per_cycle, f_hz): return macs_per_cycle * 2 * f_hz / 1e12   # MAC = 곱 + 덧셈 = 2 op
for mpc, f in [(256, 500e6), (512, 1e9), (2048, 1e9), (256 * 256, 700e6)]:
    print(f"{mpc:6d} MAC/cycle @ {f/1e6:5.0f} MHz = {tops(mpc, f):6.2f} TOPS (peak)")
E_MAC = 0.2 + 0.1          # pJ: 8b mult + 32b add
E_INSTR = 50.0             # pJ: 명령 fetch/decode/RF/제어 (가정, '수십 pJ' 자릿수)
E_L1 = 20 / 8              # pJ/B: 32 KB SRAM 64-bit 읽기 20 pJ
E_BUF = 10 / 8             # pJ/B: 8 KB 로컬 버퍼 64-bit 읽기 10 pJ
cases = {
    "CPU scalar (3 instr/MAC)": 3 * E_INSTR + 2 * E_L1 + E_MAC,
    "CPU SIMD 16 MAC/instr":    3 * E_INSTR / 16 + 2 * E_L1 + E_MAC,
    "NPU, reuse 16":            2 / 16 * E_BUF + E_MAC,
    "NPU, reuse 64":            2 / 64 * E_BUF + E_MAC,
}
for k, e in cases.items():
    print(f"{k:26s}: {e:7.2f} pJ/MAC  → {2 / e:6.3f} TOPS/W (MAC 연산부만)")
```

```text
   256 MAC/cycle @   500 MHz =   0.26 TOPS (peak)
   512 MAC/cycle @  1000 MHz =   1.02 TOPS (peak)
  2048 MAC/cycle @  1000 MHz =   4.10 TOPS (peak)
 65536 MAC/cycle @   700 MHz =  91.75 TOPS (peak)
CPU scalar (3 instr/MAC)  :  155.30 pJ/MAC  →  0.013 TOPS/W (MAC 연산부만)
CPU SIMD 16 MAC/instr     :   14.68 pJ/MAC  →  0.136 TOPS/W (MAC 연산부만)
NPU, reuse 16             :    0.46 pJ/MAC  →  4.384 TOPS/W (MAC 연산부만)
NPU, reuse 64             :    0.34 pJ/MAC  →  5.899 TOPS/W (MAC 연산부만)
```

출력에서 볼 것: 256×256 = 65,536 MAC을 700 MHz로 돌리면 약 92 TOPS인데, 이것이 Google TPU v1 논문(Jouppi et al., ISCA 2017)이 밝힌 peak 92 TOPS와 같은 계산이다. 그리고 MAC당 에너지가 CPU scalar 155 pJ → SIMD 14.7 pJ → NPU 0.3~0.5 pJ로 자릿수가 두 번 바뀐다. 마지막 열의 TOPS/W는 **MAC 연산부만**의 값이다. 실제 칩은 DRAM 접근, static 전력, 제어 로직이 더해져서 이보다 훨씬 낮다(D7 5절).

### 1.5 TOPS가 말하지 않는 것 (D3, M4)

TOPS는 "공장의 최대 생산 능력"이다. 실제 생산량은 세 가지로 깎인다.

| 깎이는 이유 | 무엇 | 이 노트의 절 |
|---|---|---|
| 활용률 | 레이어 모양이 배열에 안 맞아서 PE가 논다 | 3절 (depthwise 2.8%, GEMV 2.1%) |
| 메모리 대역폭 | DRAM에서 데이터를 못 가져와서 MAC이 기다린다 | 5절, D3 roofline |
| 지원 op | NPU가 모르는 op가 CPU로 떨어진다 | 6절 |

그리고 스펙 자체에 숨은 조건이 있다(M4): int4나 sparsity 기준 TOPS(int8 기준의 2배로 표기되기도 한다), 짧은 burst에서만 가능한 최고 주파수, NPU + DSP + GPU를 합친 "플랫폼 TOPS" 등이다. **"몇 TOPS냐"보다 "내 모델에서 몇 ms, 몇 mJ냐"가 진짜 질문**이다.

### 1.6 Don 경험과 연결, 그리고 함정

SSD 컨트롤러가 CRC·ECC·암호화를 하드웨어 엔진에 맡기는 이유와 같다. 펌웨어 루프로 LDPC 디코딩을 하면 명령어 오버헤드로 전력과 시간이 폭발한다. 그래서 반복적이고 규칙적인 계산은 고정 기능 엔진으로 내린다. 신경망의 conv·matmul은 가장 규칙적인 계산이다.

함정: "NPU가 있으니 모든 게 빨라진다"는 틀렸다. NPU는 **규칙적이고 크고 int8로 양자화된** 연산에서만 압도적이다. 작은 연산, 불규칙한 연산(동적 shape, gather, 정렬), float가 필요한 연산에서는 CPU·DSP가 더 나을 수 있다.

---

## 2. NPU 해부도

### 2.1 블록도

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="e5a" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker><marker id="e5as" viewBox="0 0 8 8" refX="1" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M8,0 L0,4 L8,8 z" fill="currentColor"/></marker></defs><rect x="200" y="14" width="470" height="396" rx="8" fill="none" stroke="currentColor" stroke-opacity="0.5" stroke-dasharray="6 4"/><text x="660.0" y="32.0" font-size="13" text-anchor="end" font-weight="bold">NPU</text><rect x="10" y="40" width="160" height="60" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="90.0" y="66.0" font-size="12" text-anchor="middle">Host CPU (Cortex-M/A)</text><text x="90.0" y="82.0" font-size="12" text-anchor="middle">driver · runtime</text><rect x="10" y="130" width="160" height="50" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="90.0" y="151.0" font-size="12" text-anchor="middle">host interface</text><text x="90.0" y="167.0" font-size="12" text-anchor="middle">레지스터(APB) · IRQ</text><rect x="10" y="250" width="160" height="40" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="90.0" y="274.0" font-size="12" text-anchor="middle">SMMU / IOMMU</text><rect x="10" y="330" width="160" height="60" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="90.0" y="356.0" font-size="12" text-anchor="middle">DRAM (LPDDR) / flash</text><text x="90.0" y="372.0" font-size="12" text-anchor="middle">weights · activations</text><rect x="220" y="40" width="200" height="50" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="320.0" y="61.0" font-size="12" text-anchor="middle">command processor</text><text x="320.0" y="77.0" font-size="12" text-anchor="middle">command stream 해석 · 순서 제어</text><rect x="440" y="40" width="210" height="50" rx="5" fill="#e08a3c" fill-opacity="0.08" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="5 4"/><text x="545.0" y="61.0" font-size="12" text-anchor="middle">제어 CPU + firmware</text><text x="545.0" y="77.0" font-size="12" text-anchor="middle">(있는 칩도, 없는 칩도)</text><rect x="220" y="120" width="80" height="270" rx="5" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b" stroke-width="1.5"/><text x="260.0" y="251.0" font-size="12" text-anchor="middle">DMA</text><text x="260.0" y="267.0" font-size="12" text-anchor="middle">engines</text><rect x="320" y="120" width="120" height="60" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="380.0" y="146.0" font-size="12" text-anchor="middle">weight buffer</text><text x="380.0" y="162.0" font-size="12" text-anchor="middle">(SRAM)</text><rect x="320" y="200" width="120" height="60" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="380.0" y="226.0" font-size="12" text-anchor="middle">activation buffer</text><text x="380.0" y="242.0" font-size="12" text-anchor="middle">(SRAM)</text><rect x="320" y="272" width="120" height="46" rx="5" fill="#888" fill-opacity="0.08" stroke="#888" stroke-width="1.5"/><text x="380.0" y="291.0" font-size="12" text-anchor="middle">perf counters</text><text x="380.0" y="307.0" font-size="12" text-anchor="middle">debug · trace</text><rect x="320" y="330" width="120" height="60" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="380.0" y="356.0" font-size="12" text-anchor="middle">output / accum</text><text x="380.0" y="372.0" font-size="12" text-anchor="middle">buffer (int32)</text><rect x="470" y="120" width="180" height="150" rx="5" fill="#d0564a" fill-opacity="0.08" stroke="#d0564a" stroke-width="1.5"/><rect x="482" y="130" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="502" y="130" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="522" y="130" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="542" y="130" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="562" y="130" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="582" y="130" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="602" y="130" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="622" y="130" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="482" y="150" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="502" y="150" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="522" y="150" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="542" y="150" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="562" y="150" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="582" y="150" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="602" y="150" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="622" y="150" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="482" y="170" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="502" y="170" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="522" y="170" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="542" y="170" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="562" y="170" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="582" y="170" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="602" y="170" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="622" y="170" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="482" y="190" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="502" y="190" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="522" y="190" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="542" y="190" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="562" y="190" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="582" y="190" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="602" y="190" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="622" y="190" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="482" y="210" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="502" y="210" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="522" y="210" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="542" y="210" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="562" y="210" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="582" y="210" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="602" y="210" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><rect x="622" y="210" width="14" height="14" fill="#d0564a" fill-opacity="0.45"/><text x="560.0" y="245.0" font-size="12" text-anchor="middle">MAC array (PE 격자)</text><text x="560.0" y="262.0" font-size="12" text-anchor="middle">int8×int8 → int32 누산</text><rect x="470" y="300" width="180" height="90" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="560.0" y="333.0" font-size="12" text-anchor="middle">vector / activation unit</text><text x="560.0" y="349.0" font-size="12" text-anchor="middle">ReLU · pool · requant</text><text x="560.0" y="365.0" font-size="12" text-anchor="middle">LUT (sigmoid, exp …)</text><line x1="90" y1="100" x2="90" y2="130" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5a)"/><line x1="170" y1="155" x2="220" y2="75" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5a)"/><line x1="90" y1="290" x2="90" y2="330" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5a)" marker-start="url(#e5as)"/><line x1="170" y1="270" x2="220" y2="270" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5a)" marker-start="url(#e5as)"/><line x1="260" y1="90" x2="260" y2="120" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5a)"/><line x1="300" y1="150" x2="320" y2="150" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5a)"/><line x1="300" y1="230" x2="320" y2="230" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5a)" marker-start="url(#e5as)"/><line x1="320" y1="360" x2="300" y2="360" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5a)"/><line x1="440" y1="150" x2="470" y2="150" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5a)"/><line x1="440" y1="230" x2="470" y2="230" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5a)"/><line x1="560" y1="270" x2="560" y2="300" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5a)"/><line x1="470" y1="360" x2="440" y2="360" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5a)"/><text x="340.0" y="428.0" font-size="12" text-anchor="middle">데이터 타입: int8(기본) · int16 activation · fp16(일부) · int4 weight(신형) — 칩마다 다르다</text>
</svg>
```

그림 1 — 일반적인 NPU의 블록 구성. 칩마다 이름과 경계는 다르지만 거의 모든 NPU에 이 블록들이 어떤 형태로든 있다. 점선의 제어 CPU는 칩에 따라 있거나(내장 마이크로컨트롤러가 firmware를 돌림) 없다(host가 직접 command stream을 넘김).

블록마다 하는 일을 SSD 컨트롤러의 비슷한 블록과 짝지어 보자.

| 블록 | 하는 일 | SSD 컨트롤러에서 비슷한 것 |
|---|---|---|
| MAC array (PE 격자) | int8 × int8 곱을 int32로 누산. 수십~수천 개가 병렬 | ECC/LDPC 엔진의 병렬 연산 유닛 |
| weight buffer | 현재 레이어(또는 타일)의 weight를 담는 SRAM | 엔진 전용 로컬 SRAM |
| activation buffer | 입력 feature map 타일, 다음 레이어로 넘길 출력 | DRAM 캐시 버퍼 |
| output / accum buffer | int32 부분합. K 방향 타일을 누적 | 중간 결과 버퍼 |
| vector / activation unit | ReLU, pooling, requant(int32 → int8), LUT로 sigmoid·exp | 후처리 HW (CRC 삽입, scrambler) |
| DMA engines | DRAM ↔ SRAM 복사, 2D/3D stride 지원 | NAND ↔ DRAM DMA, scatter-gather |
| command processor | command stream을 읽고 DMA·MAC·vector 유닛을 순서대로 시작 | HW sequencer, NVMe command 처리기 |
| 제어 CPU + firmware | 스케줄링, 동기화, 전원 관리 (있는 칩만) | 컨트롤러 안의 Cortex-R 코어 |
| host interface | 레지스터(APB), 인터럽트, doorbell | NVMe doorbell 레지스터 + MSI |
| SMMU / IOMMU | NPU가 보는 주소를 물리 주소로 변환, 보호 | PCIe ATS/IOMMU |
| perf counters | 사이클, stall 원인, DMA 바이트 카운트 | 컨트롤러 성능 카운터·telemetry |

### 2.2 데이터 타입

NPU의 기본은 **int8 × int8 → int32 누산**이다(C1, C2). 곱은 8비트라 싸고, 누산은 32비트라 수백~수천 번 더해도 넘치지 않는다. int8 MAC 한 번의 곱은 최대 128 × 128 = 16,384 ≈ 2¹⁴이므로 int32에 2¹⁷(약 13만) 번 가까이 더해도 안전하다.

| 타입 | 어디에 | 비고 |
|---|---|---|
| int8 weight × int8 activation | 거의 모든 edge NPU의 기본 | per-channel weight scale, per-tensor activation scale이 흔하다 |
| int8 weight × int16 activation | 오디오·고정밀 레이어 | Ethos-U 등 일부 NPU가 지원 (처리량은 int8보다 낮다) |
| fp16 / bf16 | 큰 SoC NPU 일부 | LLM, 정확도에 민감한 레이어 |
| int4 weight | 신형 NPU | LLM weight 대역폭을 절반으로 (C3) |
| int32 | 누산기, bias | 사용자가 직접 다루지는 않는다 |

출력 경로는 항상 같다: int32 누산 → bias 더하기 → **requant**(정수 곱 + shift로 스케일 적용, C8 3.4절) → activation(ReLU 등) → int8로 saturate. 이 경로가 vector unit에서 일어나고, 여기서의 반올림 규칙이 참조 구현과 한 비트라도 다르면 bit-exact가 깨진다(8.3절).

### 2.3 레이어 하나가 실행되는 순서

컴파일러가 이미 모든 것을 정해 두었다고 하자. 런타임에서 conv 레이어 하나는 이렇게 흘러간다.

1. host 드라이버가 command stream의 주소와 길이를 NPU 레지스터에 쓰고 doorbell을 울린다.
2. command processor가 첫 명령을 읽는다: "DMA: DRAM 0x8000_0000에서 weight 타일 16 KB를 weight buffer 0x0000으로".
3. 다음 명령: "DMA: 입력 타일 12 KB를 activation buffer로". 두 DMA는 동시에 돌 수 있다.
4. "MAC: weight buffer의 타일 × activation buffer의 타일, 결과는 accum buffer로". DMA 완료를 기다린 뒤 시작한다(의존성은 명령 안의 wait/signal 비트 같은 것으로 표현된다).
5. MAC이 도는 동안 다음 타일의 DMA가 이미 시작된다(double buffering, 5.5절).
6. "vector: accum → requant → ReLU → activation buffer". 다음 레이어가 이 결과를 SRAM에서 바로 읽으면 DRAM 왕복이 없다(fusion, 5.6절).
7. 마지막 명령이 끝나면 인터럽트. 드라이버가 완료를 확인하고 결과 버퍼를 CPU에 넘긴다(캐시 invalidate 필수).

펌웨어 엔지니어에게 익숙한 모양이다. **NPU 프로그래밍은 결국 DMA descriptor 체인 + 연산 엔진 kick + 인터럽트 처리**다. 다만 그 descriptor 체인을 사람이 아니라 컴파일러가 만든다.

### 2.4 제어 방식 세 가지

| 방식 | 설명 | 예 (공개 자료 기준) |
|---|---|---|
| host가 command stream을 넘김 | NPU 안에 범용 CPU 없음. host MCU가 스트림 주소를 넘기고 IRQ를 받는다 | Arm Ethos-U55/U65 (Cortex-M 등이 host) |
| 내장 제어 CPU + firmware | NPU 안의 마이크로컨트롤러가 firmware로 스케줄링. host는 작업 단위만 보낸다 | 큰 SoC NPU 다수 (세부는 대개 비공개) |
| 프로그래머블 DSP + 행렬 유닛 | VLIW DSP가 제어·벡터 연산을 하고 행렬 유닛을 부른다 | Qualcomm Hexagon NPU의 scalar/vector/tensor 구성 (E4) |

bring-up 난이도가 다르다. 첫 번째는 드라이버가 얇고 투명하다. 두 번째는 NPU firmware 이미지를 로드하고 부팅시키는 단계가 추가되고, firmware 버그는 벤더만 고칠 수 있다. 세 번째는 DSP 툴체인까지 따라온다.

### 2.5 함정

- **캐시 일관성**: NPU DMA가 DRAM에 쓴 결과를 CPU가 읽기 전에 D-cache invalidate를 안 하면 옛 값을 읽는다. 입력을 CPU가 쓴 뒤 clean을 안 하면 NPU가 옛 입력을 읽는다. SSD 펌웨어에서 겪었던 바로 그 버그다(E7).
- **정렬**: DMA와 buffer는 보통 16/32/64바이트 정렬, 채널 수는 8/16/32의 배수를 원한다. 정렬이 안 맞으면 컴파일러가 패딩을 넣거나(메모리 낭비) CPU로 떨어뜨린다(C6 7.5절).
- **"NPU = MAC array"라는 착각**: 실제 레이턴시에서 vector unit(softmax, LayerNorm, requant)과 DMA가 차지하는 비중이 크다. transformer에서는 특히 그렇다.

---

## 3. Systolic array — NPU의 심장

### 3.1 직관 — 양동이 릴레이

불이 났을 때 사람들이 줄을 서서 양동이를 옆 사람에게 넘기는 장면을 떠올리자. 각 사람은 **바로 옆 사람하고만** 주고받는다. 멀리 있는 우물까지 뛰어가지 않는다. **systolic array**는 이 릴레이를 2차원으로 한 것이다. "systolic"은 심장 수축(systole)에서 온 말로, 심장이 박동할 때마다 피가 한 칸씩 밀려가듯 매 클럭마다 데이터가 이웃 PE로 한 칸씩 밀려간다(H. T. Kung, 1978~1982).

왜 좋은가? 각 PE는 옆에서 받은 값을 쓰고 다시 옆으로 넘기므로, **값 하나를 SRAM에서 한 번 읽으면 PE N개가 그것을 재사용**한다. 배선은 이웃끼리만 짧게 연결되니 클럭을 높이기 쉽고, 에너지도 작다.

### 3.2 정의 — weight-stationary systolic array

`C = A · W`를 계산한다고 하자. A는 `M × K`(activation, M행), W는 `K × N`(weight), C는 `M × N`이다. N×N 격자(여기서 격자 크기도 N으로 쓴다)의 PE(i, j)에 대해:

```
준비    : PE(i, j)가 W[i][j]를 레지스터에 들고 있는다 (weight-stationary)
매 사이클: activation은 왼쪽 → 오른쪽으로 한 칸
          부분합(psum)은 위 → 아래로 한 칸
          PE(i, j):  psum_out = psum_in(위에서) + a_in(왼쪽에서) × W[i][j]
결과    : 열 j의 바닥에서 C[m][j] = Σ_i A[m][i] × W[i][j] 가 나온다
```

말로 하면: 행 i는 "감소축 K의 i번째 원소"를 담당하고, 열 j는 "출력 채널 j"를 담당한다. A의 한 행(길이 K)이 격자를 가로지르면서 각 열에서 W의 한 열과 내적이 된다.

그런데 부분합이 위에서 아래로 내려오는 데 시간이 걸린다. PE(1, j)가 A[m][1]을 곱할 때에는 PE(0, j)의 결과(A[m][0] × W[0][j])가 이미 내려와 있어야 한다. 그래서 **행 i의 입력을 i사이클 늦게 넣는다**. 이것이 **skew(비스듬히 밀어 넣기)** 다.

```svg
<svg viewBox="0 0 680 410" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="e5b" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs><rect x="300" y="60" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="325.0" y="90.0" font-size="13" text-anchor="middle">w00</text><line x1="350" y1="78" x2="370" y2="78" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><line x1="334" y1="110" x2="334" y2="130" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><rect x="370" y="60" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="395.0" y="90.0" font-size="13" text-anchor="middle">w01</text><line x1="420" y1="78" x2="440" y2="78" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><line x1="404" y1="110" x2="404" y2="130" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><rect x="440" y="60" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="465.0" y="90.0" font-size="13" text-anchor="middle">w02</text><line x1="490" y1="78" x2="510" y2="78" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><line x1="474" y1="110" x2="474" y2="130" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><rect x="510" y="60" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="535.0" y="90.0" font-size="13" text-anchor="middle">w03</text><line x1="544" y1="110" x2="544" y2="130" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><line x1="278" y1="78" x2="300" y2="78" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><text x="260.0" y="83.0" font-size="12" text-anchor="middle">a00</text><text x="224.0" y="83.0" font-size="12" text-anchor="middle">a10</text><text x="188.0" y="83.0" font-size="12" text-anchor="middle">a20</text><rect x="300" y="130" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="325.0" y="160.0" font-size="13" text-anchor="middle">w10</text><line x1="350" y1="148" x2="370" y2="148" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><line x1="334" y1="180" x2="334" y2="200" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><rect x="370" y="130" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="395.0" y="160.0" font-size="13" text-anchor="middle">w11</text><line x1="420" y1="148" x2="440" y2="148" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><line x1="404" y1="180" x2="404" y2="200" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><rect x="440" y="130" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="465.0" y="160.0" font-size="13" text-anchor="middle">w12</text><line x1="490" y1="148" x2="510" y2="148" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><line x1="474" y1="180" x2="474" y2="200" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><rect x="510" y="130" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="535.0" y="160.0" font-size="13" text-anchor="middle">w13</text><line x1="544" y1="180" x2="544" y2="200" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><line x1="278" y1="148" x2="300" y2="148" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><text x="224.0" y="153.0" font-size="12" text-anchor="middle">a01</text><text x="188.0" y="153.0" font-size="12" text-anchor="middle">a11</text><text x="152.0" y="153.0" font-size="12" text-anchor="middle">a21</text><text x="260.0" y="153.0" font-size="14" text-anchor="middle">·</text><rect x="300" y="200" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="325.0" y="230.0" font-size="13" text-anchor="middle">w20</text><line x1="350" y1="218" x2="370" y2="218" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><line x1="334" y1="250" x2="334" y2="270" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><rect x="370" y="200" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="395.0" y="230.0" font-size="13" text-anchor="middle">w21</text><line x1="420" y1="218" x2="440" y2="218" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><line x1="404" y1="250" x2="404" y2="270" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><rect x="440" y="200" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="465.0" y="230.0" font-size="13" text-anchor="middle">w22</text><line x1="490" y1="218" x2="510" y2="218" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><line x1="474" y1="250" x2="474" y2="270" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><rect x="510" y="200" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="535.0" y="230.0" font-size="13" text-anchor="middle">w23</text><line x1="544" y1="250" x2="544" y2="270" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><line x1="278" y1="218" x2="300" y2="218" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><text x="188.0" y="223.0" font-size="12" text-anchor="middle">a02</text><text x="152.0" y="223.0" font-size="12" text-anchor="middle">a12</text><text x="116.0" y="223.0" font-size="12" text-anchor="middle">a22</text><text x="260.0" y="223.0" font-size="14" text-anchor="middle">·</text><text x="224.0" y="223.0" font-size="14" text-anchor="middle">·</text><rect x="300" y="270" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="325.0" y="300.0" font-size="13" text-anchor="middle">w30</text><line x1="350" y1="288" x2="370" y2="288" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><rect x="370" y="270" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="395.0" y="300.0" font-size="13" text-anchor="middle">w31</text><line x1="420" y1="288" x2="440" y2="288" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><rect x="440" y="270" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="465.0" y="300.0" font-size="13" text-anchor="middle">w32</text><line x1="490" y1="288" x2="510" y2="288" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><rect x="510" y="270" width="50" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0" stroke-width="1.5"/><text x="535.0" y="300.0" font-size="13" text-anchor="middle">w33</text><line x1="278" y1="288" x2="300" y2="288" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5b)"/><text x="152.0" y="293.0" font-size="12" text-anchor="middle">a03</text><text x="116.0" y="293.0" font-size="12" text-anchor="middle">a13</text><text x="80.0" y="293.0" font-size="12" text-anchor="middle">a23</text><text x="260.0" y="293.0" font-size="14" text-anchor="middle">·</text><text x="224.0" y="293.0" font-size="14" text-anchor="middle">·</text><text x="188.0" y="293.0" font-size="14" text-anchor="middle">·</text><line x1="334" y1="320" x2="334" y2="348" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><text x="334.0" y="364.0" font-size="12" text-anchor="middle">C[m][0]</text><line x1="334" y1="34" x2="334" y2="60" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><line x1="404" y1="320" x2="404" y2="348" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><text x="404.0" y="364.0" font-size="12" text-anchor="middle">C[m][1]</text><line x1="404" y1="34" x2="404" y2="60" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><line x1="474" y1="320" x2="474" y2="348" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><text x="474.0" y="364.0" font-size="12" text-anchor="middle">C[m][2]</text><line x1="474" y1="34" x2="474" y2="60" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><line x1="544" y1="320" x2="544" y2="348" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><text x="544.0" y="364.0" font-size="12" text-anchor="middle">C[m][3]</text><line x1="544" y1="34" x2="544" y2="60" stroke="#e08a3c" stroke-width="1.6" marker-end="url(#e5b)"/><text x="430.0" y="28.0" font-size="12" text-anchor="middle">psum = 0 이 위에서 들어간다 (주황 = 부분합, 아래로)</text><text x="150.0" y="350.0" font-size="12" text-anchor="middle">← 시간: 행 i는 i사이클 늦게 시작 (skew)</text><text x="150.0" y="368.0" font-size="12" text-anchor="middle">a_mi = A[m][i], · = 빈 사이클</text><text x="430.0" y="402.0" font-size="12" text-anchor="middle">열 j의 결과는 j사이클 늦게 바닥으로 나온다 (de-skew 필요)</text>
</svg>
```

그림 2 — 4×4 weight-stationary systolic array. 각 PE가 weight 하나를 들고 있고, activation(검정 화살표)은 오른쪽으로, 부분합(주황)은 아래로 흐른다. 행 i의 입력은 i사이클 늦게 들어가고(왼쪽의 `·`가 빈 사이클), 열 j의 결과는 j사이클 늦게 나온다.

### 3.3 손계산 — 2×2 배열을 사이클마다 따라가기

`A = [[1, 2], [3, 4]]`, `W = [[5, 6], [7, 8]]`. 정답은 `C = [[1·5+2·7, 1·6+2·8], [3·5+4·7, 3·6+4·8]] = [[19, 22], [43, 50]]`이다.

PE(0,0)=5, PE(0,1)=6, PE(1,0)=7, PE(1,1)=8을 들고 있다. 행 0에는 A의 0번째 열(1, 3)이, 행 1에는 1번째 열(2, 4)이 **한 사이클 늦게** 들어간다. `pᵢⱼ`는 PE(i, j)의 psum 레지스터다.

| cycle | 행 0 입력 | 행 1 입력 | PE 계산 (새 값) | 바닥으로 나온 결과 |
|---|---|---|---|---|
| 0 | a=1 | — | p00 = 1·5 = 5 | — |
| 1 | a=3 | a=2 | p00 = 3·5 = 15<br>p01 = 1·6 = 6<br>p10 = p00(이전 5) + 2·7 = 19 | C[0][0] = 19 |
| 2 | — | a=4 | p01 = 3·6 = 18<br>p10 = p00(이전 15) + 4·7 = 43<br>p11 = p01(이전 6) + 2·8 = 22 | C[1][0] = 43<br>C[0][1] = 22 |
| 3 | — | — | p11 = p01(이전 18) + 4·8 = 50 | C[1][1] = 50 |

4사이클 동안 유용한 MAC은 1 + 3 + 3 + 1 = 8개(= M·K·N = 2·2·2)이고, PE-사이클은 4 × 4 = 16개다. **활용률 50%**. 앞의 1사이클은 배열이 차는 중(fill), 마지막 1사이클은 비는 중(drain)이다.

일반식은 이렇다(타일 하나, weight 적재 제외).

```
사이클 수   T = M + 2N − 2          (M행 입력 + 행 방향 skew N−1 + 열 방향 skew N−1)
유용한 MAC  = M × k × n            (k ≤ N: 실제 K 크기, n ≤ N: 실제 출력 채널 수)
활용률      = M·k·n / (T · N²)
```

말로 하면: M이 N보다 훨씬 크면 skew 비용(2N)이 묻혀서 활용률이 k·n/N²에 가까워지고, M이 작으면(GEMV는 M = 1) fill/drain이 전부라서 활용률이 바닥이다. 2×2에서 M = 2이면 T = 4, 활용률 = 8 / 16 = 50%로 위 표와 맞는다.

### 3.4 코드로 확인 — 사이클 단위 시뮬레이터 (예제 2)

위 규칙을 그대로 옮긴 시뮬레이터다. 레지스터(`a`, `av`, `p`)를 매 사이클 동기식으로 갱신한다. RTL의 always 블록 하나를 numpy로 흉내 낸 것이라고 보면 된다. 이후 예제에서 `import`해서 쓰므로 파일로 저장한다.

```python
# e5_systolic.py — cycle-accurate weight-stationary systolic array (N x N PE)
import numpy as np

def run_tile(A, Wt, N):
    """A: [M x k] int, Wt: [k x n] int (k, n <= N). 한 타일을 사이클 단위로 돌린다.
    PE(i,j)는 Wt[i][j]를 들고 있다. activation은 왼→오, psum은 위→아래로 흐른다."""
    M, k = A.shape
    n = Wt.shape[1]
    W = np.zeros((N, N), dtype=np.int64); W[:k, :n] = Wt       # 남는 PE는 weight 0
    real = np.zeros((N, N), bool); real[:k, :n] = True           # 진짜 일하는 PE
    a = np.zeros((N, N), np.int64); av = np.zeros((N, N), bool)  # activation 레지스터 + valid
    p = np.zeros((N, N), np.int64)                               # psum 레지스터
    out = np.zeros((M, n), np.int64); busy = []
    T = M + 2 * N - 2                                            # 마지막 출력까지의 사이클 수
    for t in range(T):
        left = np.zeros(N, np.int64); lv = np.zeros(N, bool)
        for i in range(N):                                       # skew: 행 i는 i사이클 늦게 들어간다
            m = t - i
            if i < k and 0 <= m < M:
                left[i] = A[m, i]; lv[i] = True
        a = np.hstack([left[:, None], a[:, :-1]])                # activation 오른쪽으로 한 칸
        av = np.hstack([lv[:, None], av[:, :-1]])
        above = np.vstack([np.zeros((1, N), np.int64), p[:-1, :]])  # 위 PE의 psum (지난 사이클 값)
        p = above + np.where(av, a, 0) * W                       # MAC
        busy.append(int((av & real).sum()))                      # 이번 사이클에 유효 MAC 한 PE 수
        for j in range(n):                                       # 열 j 바닥에서 row m 결과가 나온다
            m = t - (N - 1) - j
            if 0 <= m < M:
                out[m, j] = p[N - 1, j]
    return out, busy

def run_gemm(A, B, N, wload=True):
    """큰 GEMM을 N x N 타일로 쪼개 돌린다. weight 적재 N사이클(겹치지 않음)을 포함."""
    M, K = A.shape; _, Nc = B.shape
    C = np.zeros((M, Nc), np.int64); cycles = 0; macs = 0
    for k0 in range(0, K, N):
        for n0 in range(0, Nc, N):
            o, busy = run_tile(A[:, k0:k0+N], B[k0:k0+N, n0:n0+N], N)
            C[:, n0:n0+N] += o                                   # K 방향 부분합은 누산 버퍼에서 더한다
            cycles += len(busy) + (N if wload else 0); macs += sum(busy)
    return C, cycles, macs
```

4×4 배열에 `[5×4] @ [4×4]`를 넣고, numpy 결과와 비교하고, 사이클마다 일한 PE 수와 skew 모양을 찍는다.

```python
# 4x4 weight-stationary systolic array로 [5x4] @ [4x4] 를 사이클 단위로 돌리고 numpy와 비교한다
import numpy as np
from e5_systolic import run_tile
rng = np.random.default_rng(0)
A = rng.integers(-3, 4, size=(5, 4))       # activation: M=5행, K=4
W = rng.integers(-3, 4, size=(4, 4))       # weight: K=4, N=4 → PE 16개에 하나씩
out, busy = run_tile(A, W, N=4)
print("systolic ==", "numpy:", np.array_equal(out, A @ W))
print(out)
print("cycles =", len(busy), " (M + 2N - 2 =", 5 + 2*4 - 2, ")")
print("busy PEs per cycle:", busy)
print("MACs =", sum(busy), "=", 5*4*4, "  utilization =", f"{sum(busy)/(len(busy)*16):.3f}")
print("skew — 행 i에 들어가는 A 원소 (cycle 0..7):")
for i in range(4):
    print(f"  row{i}:", " ".join(f"a{t-i}{i}" if 0 <= t-i < 5 else " . " for t in range(8)))
```

```text
systolic == numpy: True
[[  1   1   8  -9]
 [  8 -26   2  18]
 [ -5  13  -9  -2]
 [ -1  15   0  -9]
 [-11  11  -8  -3]]
cycles = 11  (M + 2N - 2 = 11 )
busy PEs per cycle: [1, 3, 6, 10, 13, 14, 13, 10, 6, 3, 1]
MACs = 80 = 80   utilization = 0.455
skew — 행 i에 들어가는 A 원소 (cycle 0..7):
  row0: a00 a10 a20 a30 a40  .   .   . 
  row1:  .  a01 a11 a21 a31 a41  .   . 
  row2:  .   .  a02 a12 a22 a32 a42  . 
  row3:  .   .   .  a03 a13 a23 a33 a43
```

출력에서 볼 것: 결과가 numpy와 **정확히** 같다(정수라서 bit-exact). 사이클은 5 + 2·4 − 2 = 11로 식과 같다. busy PE 수가 1 → 3 → 6 → 10 → 13 → 14 → 13 → … → 1로 **삼각형처럼 올라갔다 내려온다**. 16개 PE가 동시에 일하는 사이클은 한 번도 없고, 활용률은 80 / (11 × 16) = 45.5%다. 아래 skew 표에서 행 i의 입력이 i칸 오른쪽으로 밀려 있는 것이 그림 2의 계단 모양이다.

### 3.5 fill, drain, 그리고 weight 적재

타일을 여러 개 이어서 돌리면 모양이 더 분명해진다. 16×16 배열에 M = 48, K = 32(타일 2개)를 넣고, 타일마다 weight를 적재하는 16사이클(한 사이클에 한 행씩 밀어 넣는다고 가정)을 포함해서 사이클별 busy PE 수를 그렸다(시뮬레이터 실측 데이터).

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="250" x2="650" y2="250" stroke="currentColor"/><line x1="60" y1="250" x2="60" y2="30" stroke="currentColor"/><line x1="56" y1="250.0" x2="60" y2="250.0" stroke="currentColor"/><text x="52.0" y="254.0" font-size="12" text-anchor="end">0</text><line x1="56" y1="195.0" x2="60" y2="195.0" stroke="currentColor"/><text x="52.0" y="199.0" font-size="12" text-anchor="end">64</text><line x1="60" y1="195.0" x2="650" y2="195.0" stroke="currentColor" stroke-opacity="0.12"/><line x1="56" y1="140.0" x2="60" y2="140.0" stroke="currentColor"/><text x="52.0" y="144.0" font-size="12" text-anchor="end">128</text><line x1="60" y1="140.0" x2="650" y2="140.0" stroke="currentColor" stroke-opacity="0.12"/><line x1="56" y1="85.0" x2="60" y2="85.0" stroke="currentColor"/><text x="52.0" y="89.0" font-size="12" text-anchor="end">192</text><line x1="60" y1="85.0" x2="650" y2="85.0" stroke="currentColor" stroke-opacity="0.12"/><line x1="56" y1="30.0" x2="60" y2="30.0" stroke="currentColor"/><text x="52.0" y="34.0" font-size="12" text-anchor="end">256</text><line x1="60" y1="30.0" x2="650" y2="30.0" stroke="currentColor" stroke-opacity="0.12"/><line x1="60.0" y1="250" x2="60.0" y2="254" stroke="currentColor"/><text x="60.0" y="268.0" font-size="12" text-anchor="middle">0</text><line x1="123.1" y1="250" x2="123.1" y2="254" stroke="currentColor"/><text x="123.1" y="268.0" font-size="12" text-anchor="middle">20</text><line x1="186.2" y1="250" x2="186.2" y2="254" stroke="currentColor"/><text x="186.2" y="268.0" font-size="12" text-anchor="middle">40</text><line x1="249.3" y1="250" x2="249.3" y2="254" stroke="currentColor"/><text x="249.3" y="268.0" font-size="12" text-anchor="middle">60</text><line x1="312.4" y1="250" x2="312.4" y2="254" stroke="currentColor"/><text x="312.4" y="268.0" font-size="12" text-anchor="middle">80</text><line x1="375.5" y1="250" x2="375.5" y2="254" stroke="currentColor"/><text x="375.5" y="268.0" font-size="12" text-anchor="middle">100</text><line x1="438.6" y1="250" x2="438.6" y2="254" stroke="currentColor"/><text x="438.6" y="268.0" font-size="12" text-anchor="middle">120</text><line x1="501.7" y1="250" x2="501.7" y2="254" stroke="currentColor"/><text x="501.7" y="268.0" font-size="12" text-anchor="middle">140</text><line x1="564.8" y1="250" x2="564.8" y2="254" stroke="currentColor"/><text x="564.8" y="268.0" font-size="12" text-anchor="middle">160</text><line x1="627.9" y1="250" x2="627.9" y2="254" stroke="currentColor"/><text x="627.9" y="268.0" font-size="12" text-anchor="middle">180</text><polygon points="60.0,250 60.0,250.0 63.2,250.0 66.3,250.0 69.5,250.0 72.6,250.0 75.8,250.0 78.9,250.0 82.1,250.0 85.2,250.0 88.4,250.0 91.6,250.0 94.7,250.0 97.9,250.0 101.0,250.0 104.2,250.0 107.3,250.0 110.5,249.1 113.6,247.4 116.8,244.8 119.9,241.4 123.1,237.1 126.3,232.0 129.4,225.9 132.6,219.1 135.7,211.3 138.9,202.7 142.0,193.3 145.2,183.0 148.3,171.8 151.5,159.8 154.7,146.9 157.8,133.1 161.0,120.2 164.1,108.2 167.3,97.0 170.4,86.7 173.6,77.3 176.7,68.7 179.9,60.9 183.0,54.1 186.2,48.0 189.4,42.9 192.5,38.6 195.7,35.2 198.8,32.6 202.0,30.9 205.1,30.0 208.3,30.0 211.4,30.0 214.6,30.0 217.8,30.0 220.9,30.0 224.1,30.0 227.2,30.0 230.4,30.0 233.5,30.0 236.7,30.0 239.8,30.0 243.0,30.0 246.1,30.0 249.3,30.0 252.5,30.0 255.6,30.0 258.8,30.0 261.9,30.9 265.1,32.6 268.2,35.2 271.4,38.6 274.5,42.9 277.7,48.0 280.9,54.1 284.0,60.9 287.2,68.7 290.3,77.3 293.5,86.7 296.6,97.0 299.8,108.2 302.9,120.2 306.1,133.1 309.3,146.9 312.4,159.8 315.6,171.8 318.7,183.0 321.9,193.3 325.0,202.7 328.2,211.3 331.3,219.1 334.5,225.9 337.6,232.0 340.8,237.1 344.0,241.4 347.1,244.8 350.3,247.4 353.4,249.1 356.6,250.0 359.7,250.0 362.9,250.0 366.0,250.0 369.2,250.0 372.4,250.0 375.5,250.0 378.7,250.0 381.8,250.0 385.0,250.0 388.1,250.0 391.3,250.0 394.4,250.0 397.6,250.0 400.7,250.0 403.9,250.0 407.1,249.1 410.2,247.4 413.4,244.8 416.5,241.4 419.7,237.1 422.8,232.0 426.0,225.9 429.1,219.1 432.3,211.3 435.5,202.7 438.6,193.3 441.8,183.0 444.9,171.8 448.1,159.8 451.2,146.9 454.4,133.1 457.5,120.2 460.7,108.2 463.9,97.0 467.0,86.7 470.2,77.3 473.3,68.7 476.5,60.9 479.6,54.1 482.8,48.0 485.9,42.9 489.1,38.6 492.2,35.2 495.4,32.6 498.6,30.9 501.7,30.0 504.9,30.0 508.0,30.0 511.2,30.0 514.3,30.0 517.5,30.0 520.6,30.0 523.8,30.0 527.0,30.0 530.1,30.0 533.3,30.0 536.4,30.0 539.6,30.0 542.7,30.0 545.9,30.0 549.0,30.0 552.2,30.0 555.3,30.0 558.5,30.9 561.7,32.6 564.8,35.2 568.0,38.6 571.1,42.9 574.3,48.0 577.4,54.1 580.6,60.9 583.7,68.7 586.9,77.3 590.1,86.7 593.2,97.0 596.4,108.2 599.5,120.2 602.7,133.1 605.8,146.9 609.0,159.8 612.1,171.8 615.3,183.0 618.4,193.3 621.6,202.7 624.8,211.3 627.9,219.1 631.1,225.9 634.2,232.0 637.4,237.1 640.5,241.4 643.7,244.8 646.8,247.4 650.0,249.1 650.0,250" fill="#4a7bd0" fill-opacity="0.2"/><polyline points="60.0,250.0 63.2,250.0 66.3,250.0 69.5,250.0 72.6,250.0 75.8,250.0 78.9,250.0 82.1,250.0 85.2,250.0 88.4,250.0 91.6,250.0 94.7,250.0 97.9,250.0 101.0,250.0 104.2,250.0 107.3,250.0 110.5,249.1 113.6,247.4 116.8,244.8 119.9,241.4 123.1,237.1 126.3,232.0 129.4,225.9 132.6,219.1 135.7,211.3 138.9,202.7 142.0,193.3 145.2,183.0 148.3,171.8 151.5,159.8 154.7,146.9 157.8,133.1 161.0,120.2 164.1,108.2 167.3,97.0 170.4,86.7 173.6,77.3 176.7,68.7 179.9,60.9 183.0,54.1 186.2,48.0 189.4,42.9 192.5,38.6 195.7,35.2 198.8,32.6 202.0,30.9 205.1,30.0 208.3,30.0 211.4,30.0 214.6,30.0 217.8,30.0 220.9,30.0 224.1,30.0 227.2,30.0 230.4,30.0 233.5,30.0 236.7,30.0 239.8,30.0 243.0,30.0 246.1,30.0 249.3,30.0 252.5,30.0 255.6,30.0 258.8,30.0 261.9,30.9 265.1,32.6 268.2,35.2 271.4,38.6 274.5,42.9 277.7,48.0 280.9,54.1 284.0,60.9 287.2,68.7 290.3,77.3 293.5,86.7 296.6,97.0 299.8,108.2 302.9,120.2 306.1,133.1 309.3,146.9 312.4,159.8 315.6,171.8 318.7,183.0 321.9,193.3 325.0,202.7 328.2,211.3 331.3,219.1 334.5,225.9 337.6,232.0 340.8,237.1 344.0,241.4 347.1,244.8 350.3,247.4 353.4,249.1 356.6,250.0 359.7,250.0 362.9,250.0 366.0,250.0 369.2,250.0 372.4,250.0 375.5,250.0 378.7,250.0 381.8,250.0 385.0,250.0 388.1,250.0 391.3,250.0 394.4,250.0 397.6,250.0 400.7,250.0 403.9,250.0 407.1,249.1 410.2,247.4 413.4,244.8 416.5,241.4 419.7,237.1 422.8,232.0 426.0,225.9 429.1,219.1 432.3,211.3 435.5,202.7 438.6,193.3 441.8,183.0 444.9,171.8 448.1,159.8 451.2,146.9 454.4,133.1 457.5,120.2 460.7,108.2 463.9,97.0 467.0,86.7 470.2,77.3 473.3,68.7 476.5,60.9 479.6,54.1 482.8,48.0 485.9,42.9 489.1,38.6 492.2,35.2 495.4,32.6 498.6,30.9 501.7,30.0 504.9,30.0 508.0,30.0 511.2,30.0 514.3,30.0 517.5,30.0 520.6,30.0 523.8,30.0 527.0,30.0 530.1,30.0 533.3,30.0 536.4,30.0 539.6,30.0 542.7,30.0 545.9,30.0 549.0,30.0 552.2,30.0 555.3,30.0 558.5,30.9 561.7,32.6 564.8,35.2 568.0,38.6 571.1,42.9 574.3,48.0 577.4,54.1 580.6,60.9 583.7,68.7 586.9,77.3 590.1,86.7 593.2,97.0 596.4,108.2 599.5,120.2 602.7,133.1 605.8,146.9 609.0,159.8 612.1,171.8 615.3,183.0 618.4,193.3 621.6,202.7 624.8,211.3 627.9,219.1 631.1,225.9 634.2,232.0 637.4,237.1 640.5,241.4 643.7,244.8 646.8,247.4 650.0,249.1" fill="none" stroke="#4a7bd0" stroke-width="2"/><rect x="60.0" y="30" width="50.5" height="220" fill="#e08a3c" fill-opacity="0.15"/><text x="89.2" y="60.0" font-size="12" text-anchor="middle">weight 적재</text><rect x="356.6" y="30" width="50.5" height="220" fill="#e08a3c" fill-opacity="0.15"/><text x="385.8" y="60.0" font-size="12" text-anchor="middle">weight 적재</text><text x="154.7" y="144.0" font-size="12" text-anchor="middle">fill</text><text x="233.5" y="22.0" font-size="12" text-anchor="middle">정상 상태 256/256</text><text x="318.7" y="144.0" font-size="12" text-anchor="middle">drain</text><text x="355.0" y="288.0" font-size="12" text-anchor="middle">cycle (16×16 WS 배열, M=48, K=32 → 타일 2개)</text><text x="18.0" y="140.0" font-size="12" text-anchor="middle" transform="rotate(-90 18 140.0)">busy PE 수</text>
</svg>
```

그림 3 — 16×16 weight-stationary 배열의 사이클별 busy PE 수. 주황 구간은 weight 적재(0개 일함), 파란 사다리꼴의 경사는 fill과 drain, 평평한 윗부분만 256개 전부가 일한다. 188사이클 중 256/256인 사이클은 타일당 18사이클뿐이다.

이 그림이 systolic array의 **활용률 문제**를 한 장으로 보여 준다.

- **fill/drain**: 타일마다 2N − 2 사이클의 경사가 생긴다. M이 N의 몇 배는 되어야 묻힌다.
- **weight 적재**: weight-stationary는 타일을 바꿀 때마다 weight를 다시 깔아야 한다. 실제 설계는 PE에 **그림자(shadow) weight 레지스터**를 두고 현재 타일을 계산하는 동안 다음 타일의 weight를 미리 밀어 넣어 이 구간을 숨긴다(TPU v1이 weight를 double-buffer했다고 논문에 나온다).
- **모양 불일치**: K나 N이 배열 크기보다 작으면 일부 행·열이 영원히 논다(3.7절).

### 3.6 C로 같은 것을 — RTL을 C로 흉내 내기 (예제 3)

Don이 FPGA 검증에서 봤을 RTL은 보통 `always @(posedge clk)` 안에서 non-blocking 대입(`<=`)으로 모든 레지스터를 동시에 갱신한다. C에서는 순서대로 실행되니, **아래·오른쪽 PE부터 갱신**해서 "이전 사이클 값"을 읽도록 만든다. 아래 C 코드는 4×4 배열에 `[6×4] @ [4×4]`를 돌리고 삼중 루프 참조와 `memcmp`로 비교한다.

```c
/* systolic.c — weight-stationary N x N systolic array, 사이클 단위 (C 버전) */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#define N 4
#define M 6
static int8_t  A[M][N], W[N][N];
static int32_t a_reg[N][N], p_reg[N][N], out[M][N], ref[M][N];
static int     v_reg[N][N];

int main(void) {
    uint32_t s = 1u;                                    /* LCG로 결정적 입력 */
    for (int m = 0; m < M; m++) for (int k = 0; k < N; k++) { s = s * 1103515245u + 12345u; A[m][k] = (int8_t)((s >> 16) % 15) - 7; }
    for (int k = 0; k < N; k++) for (int j = 0; j < N; j++) { s = s * 1103515245u + 12345u; W[k][j] = (int8_t)((s >> 16) % 15) - 7; }
    int T = M + 2 * N - 2, macs = 0;
    for (int t = 0; t < T; t++) {
        /* 1) psum: 아래 행부터 갱신해야 "지난 사이클 값"을 읽는다 (RTL의 non-blocking 대입 흉내) */
        for (int i = N - 1; i >= 0; i--)
            for (int j = N - 1; j >= 0; j--) {
                int32_t a = (j == 0) ? ((t - i >= 0 && t - i < M) ? A[t - i][i] : 0) : a_reg[i][j - 1];
                int     v = (j == 0) ? (t - i >= 0 && t - i < M) : v_reg[i][j - 1];
                int32_t above = (i == 0) ? 0 : p_reg[i - 1][j];
                p_reg[i][j] = above + (v ? a * W[i][j] : 0);
                a_reg[i][j] = a; v_reg[i][j] = v; macs += v;
            }
        for (int j = 0; j < N; j++) {                   /* 2) 열 j 바닥에서 결과 수집 */
            int m = t - (N - 1) - j;
            if (m >= 0 && m < M) out[m][j] = p_reg[N - 1][j];
        }
    }
    for (int m = 0; m < M; m++) for (int j = 0; j < N; j++) {   /* 참조: 삼중 루프 */
        ref[m][j] = 0;
        for (int k = 0; k < N; k++) ref[m][j] += A[m][k] * W[k][j];
    }
    printf("cycles=%d MACs=%d (expect %d) util=%.3f\n", T, macs, M * N * N, (double)macs / (T * N * N));
    printf("bit-exact vs triple loop: %s\n", memcmp(out, ref, sizeof out) == 0 ? "PASS" : "FAIL");
    for (int m = 0; m < 2; m++) printf("out[%d] = %d %d %d %d\n", m, out[m][0], out[m][1], out[m][2], out[m][3]);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 systolic.c -o systolic -lm && ./systolic
```

```text
cycles=12 MACs=96 (expect 96) util=0.500
bit-exact vs triple loop: PASS
out[0] = 17 9 -17 -8
out[1] = 25 0 -40 38
```

출력에서 볼 것: 경고 0개로 컴파일되고, 사이클 수 6 + 2·4 − 2 = 12, MAC 96 = 6·4·4, 활용률 50%, 참조와 bit-exact다. 갱신 순서를 `i = 0`부터로 바꾸면 한 사이클에 psum이 두 칸 내려가는 버그가 생겨 FAIL이 난다(직접 해보기 3번). **RTL 시뮬레이터를 C 모델과 맞춰 보던 일과 똑같다.** 실제로 NPU 벤더는 이런 C/C++ 사이클 모델(또는 bit-accurate 모델)을 golden reference로 쓴다.

### 3.7 레이어 모양별 활용률 (예제 4)

이제 16×16 배열에 실제 레이어 모양을 올려 본다. conv는 im2col로 GEMM이 된다고 본다(B2, D1): M = 출력 픽셀 수, K = C_in × 3 × 3, N = C_out. depthwise는 채널마다 따로 계산하므로 "K = 9, N = 1짜리 GEMM을 채널 수만큼"이다. 모든 경우에 결과를 numpy와 비트 단위로 비교한다.

```python
# 16x16 WS systolic array에서 레이어 모양별 활용률을 사이클 시뮬레이션으로 잰다 (weight 적재 16사이클 포함)
import numpy as np
from e5_systolic import run_gemm
rng = np.random.default_rng(1); N = 16
def case(M, K, Nc, groups=1):
    cyc = mac = 0
    for _ in range(groups):                       # depthwise: 채널마다 독립 GEMM
        A = rng.integers(-8, 8, (M, K)); B = rng.integers(-8, 8, (K, Nc))
        C, c, m = run_gemm(A, B, N)
        assert np.array_equal(C, A @ B)           # 매번 numpy와 비트 단위 비교
        cyc += c; mac += m
    return cyc, mac, mac / (cyc * N * N)
cases = [("conv3x3 16->64 @14x14", 196, 144, 64, 1),
         ("conv1x1 64->64 @14x14", 196, 64, 64, 1),
         ("conv1x1 64->64 @28x28", 784, 64, 64, 1),
         ("first conv3x3 3->16",   196, 27, 16, 1),
         ("conv1x1 8->16 (small)", 196, 8, 16, 1),
         ("depthwise3x3 C=16",     196, 9, 1, 16),
         ("FC 256->256 batch1",      1, 256, 256, 1),
         ("FC 256->256 batch16",    16, 256, 256, 1)]
for name, M, K, Nc, g in cases:
    cyc, mac, u = case(M, K, Nc, g)
    print(f"{name:23s} M={M:4d} K={K:3d} N={Nc:3d} x{g:<2d} MAC={mac:7d} cycles={cyc:6d} util={u*100:5.1f}%")
```

```text
conv3x3 16->64 @14x14   M= 196 K=144 N= 64 x1  MAC=1806336 cycles=  8712 util= 81.0%
conv1x1 64->64 @14x14   M= 196 K= 64 N= 64 x1  MAC= 802816 cycles=  3872 util= 81.0%
conv1x1 64->64 @28x28   M= 784 K= 64 N= 64 x1  MAC=3211264 cycles= 13280 util= 94.5%
first conv3x3 3->16     M= 196 K= 27 N= 16 x1  MAC=  84672 cycles=   484 util= 68.3%
conv1x1 8->16 (small)   M= 196 K=  8 N= 16 x1  MAC=  25088 cycles=   242 util= 40.5%
depthwise3x3 C=16       M= 196 K=  9 N=  1 x16 MAC=  28224 cycles=  3872 util=  2.8%
FC 256->256 batch1      M=   1 K=256 N=256 x1  MAC=  65536 cycles= 12032 util=  2.1%
FC 256->256 batch16     M=  16 K=256 N=256 x1  MAC=1048576 cycles= 15872 util= 25.8%
```

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<line x1="190" y1="20" x2="190" y2="276" stroke="currentColor"/><line x1="190.0" y1="20" x2="190.0" y2="276" stroke="currentColor" stroke-opacity="0.12"/><text x="190.0" y="292.0" font-size="12" text-anchor="middle">0%</text><line x1="300.0" y1="20" x2="300.0" y2="276" stroke="currentColor" stroke-opacity="0.12"/><text x="300.0" y="292.0" font-size="12" text-anchor="middle">25%</text><line x1="410.0" y1="20" x2="410.0" y2="276" stroke="currentColor" stroke-opacity="0.12"/><text x="410.0" y="292.0" font-size="12" text-anchor="middle">50%</text><line x1="520.0" y1="20" x2="520.0" y2="276" stroke="currentColor" stroke-opacity="0.12"/><text x="520.0" y="292.0" font-size="12" text-anchor="middle">75%</text><line x1="630.0" y1="20" x2="630.0" y2="276" stroke="currentColor" stroke-opacity="0.12"/><text x="630.0" y="292.0" font-size="12" text-anchor="middle">100%</text><rect x="190" y="26" width="356.4" height="20" fill="#3f9a6b" fill-opacity="0.8"/><text x="182.0" y="41.0" font-size="12" text-anchor="end">conv3x3 16→64</text><text x="552.4" y="41.0" font-size="12" text-anchor="start">81.0%</text><rect x="190" y="58" width="356.4" height="20" fill="#3f9a6b" fill-opacity="0.8"/><text x="182.0" y="73.0" font-size="12" text-anchor="end">conv1x1 64→64 @14²</text><text x="552.4" y="73.0" font-size="12" text-anchor="start">81.0%</text><rect x="190" y="90" width="415.8" height="20" fill="#3f9a6b" fill-opacity="0.8"/><text x="182.0" y="105.0" font-size="12" text-anchor="end">conv1x1 64→64 @28²</text><text x="611.8" y="105.0" font-size="12" text-anchor="start">94.5%</text><rect x="190" y="122" width="300.5" height="20" fill="#3f9a6b" fill-opacity="0.8"/><text x="182.0" y="137.0" font-size="12" text-anchor="end">first conv 3→16</text><text x="496.5" y="137.0" font-size="12" text-anchor="start">68.3%</text><rect x="190" y="154" width="178.2" height="20" fill="#e08a3c" fill-opacity="0.8"/><text x="182.0" y="169.0" font-size="12" text-anchor="end">conv1x1 8→16</text><text x="374.2" y="169.0" font-size="12" text-anchor="start">40.5%</text><rect x="190" y="186" width="12.3" height="20" fill="#d0564a" fill-opacity="0.8"/><text x="182.0" y="201.0" font-size="12" text-anchor="end">depthwise C=16</text><text x="208.3" y="201.0" font-size="12" text-anchor="start">2.8%</text><rect x="190" y="218" width="9.2" height="20" fill="#d0564a" fill-opacity="0.8"/><text x="182.0" y="233.0" font-size="12" text-anchor="end">FC batch 1</text><text x="205.2" y="233.0" font-size="12" text-anchor="start">2.1%</text><rect x="190" y="250" width="113.5" height="20" fill="#e08a3c" fill-opacity="0.8"/><text x="182.0" y="265.0" font-size="12" text-anchor="end">FC batch 16</text><text x="309.5" y="265.0" font-size="12" text-anchor="start">25.8%</text><text x="410.0" y="312.0" font-size="12" text-anchor="middle">MAC 활용률 (16×16 WS systolic, weight 적재 포함, 사이클 시뮬레이션 실측)</text>
</svg>
```

그림 4 — 16×16 WS systolic array의 레이어별 MAC 활용률(예제 4의 실제 시뮬레이션 결과). 초록은 60% 이상, 주황은 20~60%, 빨강은 20% 미만.

출력에서 볼 것, 한 줄씩:

| 레이어 | 활용률 | 왜 |
|---|---|---|
| conv1x1 64→64 @28² | 94.5% | M = 784가 커서 fill/drain·weight 적재(타일당 16 + 30 사이클)가 묻힌다 |
| conv3x3, conv1x1 @14² | 81.0% | M = 196이라 타일당 오버헤드 46사이클이 196 대비 19% |
| first conv 3→16 | 68.3% | K = 27이라 두 번째 K 타일이 11/16행만 쓴다 |
| conv1x1 8→16 | 40.5% | K = 8, 16행 중 8행만 일한다 |
| depthwise C=16 | 2.8% | 채널마다 K = 9, N = 1 → 256개 PE 중 9개. 게다가 채널마다 weight 적재와 fill/drain |
| FC batch 1 (GEMV) | 2.1% | M = 1. 입력 한 행이 격자를 통과하는 동안 대부분 PE는 논다 |
| FC batch 16 | 25.8% | M = 16이면 skew 30 + weight 16 사이클 대비 흐르는 행 16개 |

이것이 D4 3.5절에서 식으로 본 결론을 **사이클 시뮬레이션으로** 다시 확인한 것이다. depthwise는 MAC 수가 conv3x3의 1.6%(28,224 vs 1,806,336)인데 사이클은 44%(3,872 vs 8,712)나 쓴다.

### 3.8 배열 크기의 딜레마와 실제 NPU의 대응

배열이 클수록 peak TOPS는 올라가지만 **작은 레이어에서 활용률은 떨어진다**. N×N 배열의 fill/drain은 2N이고, 모양 불일치로 노는 PE도 늘어난다. TPU v1은 데이터센터용이라 256×256을 쓸 수 있었다(큰 batch). edge NPU는 batch 1, 작은 채널이 많아서 배열을 작게 여러 개 두거나, 배열 모양을 레이어에 맞게 바꿀 수 있게 만든다.

실제 NPU들이 쓰는 대응(공개 논문·문서에서 흔히 보이는 기법):

- **depthwise 전용 경로**: 채널 하나를 PE 열 하나에 대응시키는 channel-parallel 매핑이나, 3×3 전용 벡터 유닛을 둔다.
- **여러 개의 작은 코어**: 큰 격자 하나 대신 작은 MAC 블록 여러 개가 서로 다른 타일을 동시에 처리한다.
- **systolic이 아닌 MAC 배열**: 모든 PE에 같은 입력을 broadcast하고 adder tree로 더하는 구조도 흔하다. 모양 유연성과 배선 비용을 맞바꾼다. 어떤 구조를 쓰든 "재사용·모양·데이터 이동"이라는 질문은 같다.
- **batch 대신 공간 병렬**: 오디오·IMU 스트리밍처럼 batch가 1이면 M은 시간 프레임 수나 출력 픽셀 수에서 나와야 한다. 모델 설계에서 이를 고려하는 것이 C7(HW-aware 설계)이다.

### 3.9 함정

- **활용률 = MAC 활용률만이 아니다**: 배열이 90% 일해도 DMA가 못 따라오면 전체는 느리다(5절). 반대로 활용률이 낮아도 그 레이어가 전체 시간의 1%면 무시해도 된다. **프로파일 먼저**.
- **"MAC 수가 적은 모델 = 빠른 모델"이 아니다**: depthwise·GEMV·작은 채널은 MAC이 적어도 사이클이 많다.
- **weight-stationary에서 weight 적재 비용을 잊는다**: M이 작은(batch 1, 작은 feature map) 레이어일수록 weight 적재가 지배한다.

---

## 4. Dataflow — 무엇을 PE에 붙잡아 둘 것인가

### 4.1 직관 — 재사용 세 가지

conv 한 레이어를 보면 같은 값이 여러 번 쓰인다. C = 4, K = 4(출력 채널), 입력 10×10, 3×3 필터, 출력 8×8인 작은 conv를 예로 들자.

| 재사용 종류 | 무엇이 | 몇 번 쓰이나 (이 예) |
|---|---|---|
| weight reuse | weight 하나가 모든 출력 픽셀에서 | 8 × 8 = 64번 |
| input reuse | 입력 픽셀 하나가 여러 필터 위치와 모든 출력 채널에서 | 최대 3 × 3 × 4 = 36번 |
| output (psum) reuse | 출력 하나가 누산될 때 | C × 3 × 3 = 36번 더해진다 |

MAC 총수는 K × C × 8 × 8 × 9 = 9,216이다. weight는 144개, 입력은 400개, 출력은 256개뿐이다. 즉 **값 하나당 평균 수십 번 쓰인다**. 그 수십 번을 비싼 메모리(GLB, DRAM)에서 매번 가져오느냐, 싼 메모리(PE 레지스터)에 잡아 두고 쓰느냐가 에너지를 가른다.

문제는 **세 가지를 동시에 다 잡아 둘 수는 없다**는 것이다. PE의 레지스터는 작다. 그래서 "무엇을 붙잡아 둘지(stationary)" 고르는 것이 dataflow다.

### 4.2 네 가지 dataflow

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="e5c" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs><rect x="10" y="10" width="325" height="175" rx="6" fill="none" stroke="currentColor" stroke-opacity="0.35"/><text x="172.0" y="30.0" font-size="13" text-anchor="middle" font-weight="bold">Weight-stationary</text><rect x="40" y="48" width="28" height="28" rx="3" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><line x1="68" y1="62" x2="78" y2="62" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="54" y1="76" x2="54" y2="86" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="78" y="48" width="28" height="28" rx="3" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><line x1="106" y1="62" x2="116" y2="62" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="92" y1="76" x2="92" y2="86" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="116" y="48" width="28" height="28" rx="3" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><line x1="130" y1="76" x2="130" y2="86" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="40" y="86" width="28" height="28" rx="3" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><line x1="68" y1="100" x2="78" y2="100" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="54" y1="114" x2="54" y2="124" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="78" y="86" width="28" height="28" rx="3" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><line x1="106" y1="100" x2="116" y2="100" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="92" y1="114" x2="92" y2="124" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="116" y="86" width="28" height="28" rx="3" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><line x1="130" y1="114" x2="130" y2="124" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="40" y="124" width="28" height="28" rx="3" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><line x1="68" y1="138" x2="78" y2="138" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="78" y="124" width="28" height="28" rx="3" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><line x1="106" y1="138" x2="116" y2="138" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="116" y="124" width="28" height="28" rx="3" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/><text x="185.0" y="70.0" font-size="12" text-anchor="start">PE에 머무름: weight</text><text x="185.0" y="95.0" font-size="12" text-anchor="start">흐름: input →, psum ↓</text><text x="185.0" y="120.0" font-size="12" text-anchor="start">재사용: weight (각 1회 적재)</text><text x="185.0" y="160.0" font-size="12" text-anchor="start">(칸 = PE, 화살표 = 이웃 전달)</text><rect x="345" y="10" width="325" height="175" rx="6" fill="none" stroke="currentColor" stroke-opacity="0.35"/><text x="507.0" y="30.0" font-size="13" text-anchor="middle" font-weight="bold">Output-stationary</text><rect x="375" y="48" width="28" height="28" rx="3" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><line x1="403" y1="62" x2="413" y2="62" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="389" y1="76" x2="389" y2="86" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="413" y="48" width="28" height="28" rx="3" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><line x1="441" y1="62" x2="451" y2="62" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="427" y1="76" x2="427" y2="86" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="451" y="48" width="28" height="28" rx="3" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><line x1="465" y1="76" x2="465" y2="86" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="375" y="86" width="28" height="28" rx="3" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><line x1="403" y1="100" x2="413" y2="100" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="389" y1="114" x2="389" y2="124" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="413" y="86" width="28" height="28" rx="3" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><line x1="441" y1="100" x2="451" y2="100" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="427" y1="114" x2="427" y2="124" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="451" y="86" width="28" height="28" rx="3" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><line x1="465" y1="114" x2="465" y2="124" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="375" y="124" width="28" height="28" rx="3" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><line x1="403" y1="138" x2="413" y2="138" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="413" y="124" width="28" height="28" rx="3" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><line x1="441" y1="138" x2="451" y2="138" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="451" y="124" width="28" height="28" rx="3" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/><text x="520.0" y="70.0" font-size="12" text-anchor="start">PE에 머무름: psum(출력)</text><text x="520.0" y="95.0" font-size="12" text-anchor="start">흐름: input →, weight ↓</text><text x="520.0" y="120.0" font-size="12" text-anchor="start">재사용: psum (끝까지 누산)</text><text x="520.0" y="160.0" font-size="12" text-anchor="start">(칸 = PE, 화살표 = 이웃 전달)</text><rect x="10" y="195" width="325" height="175" rx="6" fill="none" stroke="currentColor" stroke-opacity="0.35"/><text x="172.0" y="215.0" font-size="13" text-anchor="middle" font-weight="bold">Input-stationary</text><rect x="40" y="233" width="28" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><line x1="68" y1="247" x2="78" y2="247" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="54" y1="261" x2="54" y2="271" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="78" y="233" width="28" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><line x1="106" y1="247" x2="116" y2="247" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="92" y1="261" x2="92" y2="271" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="116" y="233" width="28" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><line x1="130" y1="261" x2="130" y2="271" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="40" y="271" width="28" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><line x1="68" y1="285" x2="78" y2="285" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="54" y1="299" x2="54" y2="309" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="78" y="271" width="28" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><line x1="106" y1="285" x2="116" y2="285" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="92" y1="299" x2="92" y2="309" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="116" y="271" width="28" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><line x1="130" y1="299" x2="130" y2="309" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="40" y="309" width="28" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><line x1="68" y1="323" x2="78" y2="323" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="78" y="309" width="28" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><line x1="106" y1="323" x2="116" y2="323" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="116" y="309" width="28" height="28" rx="3" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/><text x="185.0" y="255.0" font-size="12" text-anchor="start">PE에 머무름: input</text><text x="185.0" y="280.0" font-size="12" text-anchor="start">흐름: weight →, psum ↓</text><text x="185.0" y="305.0" font-size="12" text-anchor="start">재사용: input</text><text x="185.0" y="345.0" font-size="12" text-anchor="start">(칸 = PE, 화살표 = 이웃 전달)</text><rect x="345" y="195" width="325" height="175" rx="6" fill="none" stroke="currentColor" stroke-opacity="0.35"/><text x="507.0" y="215.0" font-size="13" text-anchor="middle" font-weight="bold">Row-stationary</text><rect x="375" y="233" width="28" height="28" rx="3" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><line x1="403" y1="247" x2="413" y2="247" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="389" y1="261" x2="389" y2="271" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="413" y="233" width="28" height="28" rx="3" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><line x1="441" y1="247" x2="451" y2="247" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="427" y1="261" x2="427" y2="271" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="451" y="233" width="28" height="28" rx="3" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><line x1="465" y1="261" x2="465" y2="271" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="375" y="271" width="28" height="28" rx="3" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><line x1="403" y1="285" x2="413" y2="285" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="389" y1="299" x2="389" y2="309" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="413" y="271" width="28" height="28" rx="3" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><line x1="441" y1="285" x2="451" y2="285" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><line x1="427" y1="299" x2="427" y2="309" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="451" y="271" width="28" height="28" rx="3" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><line x1="465" y1="299" x2="465" y2="309" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="375" y="309" width="28" height="28" rx="3" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><line x1="403" y1="323" x2="413" y2="323" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="413" y="309" width="28" height="28" rx="3" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><line x1="441" y1="323" x2="451" y2="323" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5c)"/><rect x="451" y="309" width="28" height="28" rx="3" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/><text x="520.0" y="255.0" font-size="12" text-anchor="start">PE에 머무름: filter 1행</text><text x="520.0" y="280.0" font-size="12" text-anchor="start">흐름: input 대각선, psum ↑</text><text x="520.0" y="305.0" font-size="12" text-anchor="start">재사용: 셋 다 RF에서 (Eyeriss)</text><text x="520.0" y="345.0" font-size="12" text-anchor="start">(칸 = PE, 화살표 = 이웃 전달)</text>
</svg>
```

그림 5 — 네 가지 dataflow의 개념. 각 격자는 PE 배열이고, 무엇이 PE에 머무르고 무엇이 이웃으로 흘러가는지가 다르다.

| dataflow | PE에 머무름 | 움직이는 것 | 최대화하는 재사용 | 약점 | 대표 예 |
|---|---|---|---|---|---|
| weight-stationary (WS) | weight | input, psum | weight | psum을 매번 읽고 써야 한다 | TPU v1의 행렬 유닛, 많은 NPU |
| output-stationary (OS) | psum(출력) | input, weight | psum (끝까지 누산 후 한 번 씀) | weight·input을 매번 공급 | ShiDianNao 계열, 많은 edge NPU |
| input-stationary (IS) | input | weight, psum | input | psum 트래픽 큼 | 일부 sparse 가속기 |
| row-stationary (RS) | 필터 한 행 + 입력 창 + psum을 RF에 | PE 간 대각선 input 공유, 세로 psum 누산 | 셋 다 균형 있게 | 제어·매핑이 복잡 | MIT Eyeriss |

**row-stationary**는 Eyeriss(Chen, Emer, Sze, ISCA 2016)가 제안한 방식이다. PE 하나가 "필터 한 행 × 입력 한 행 → 출력 한 행의 부분합"이라는 1D convolution을 맡는다. 필터 행(S개 weight)을 RF에 두고, 입력 행을 창처럼 밀면서(sliding window) 쓰고, 부분합도 RF에서 누산한다. 세로로 쌓인 PE들이 필터의 다른 행을 맡아 부분합을 합친다. 한 가지 재사용만 최대화하지 않고 **전체 에너지**를 최소화하는 것이 목표다.

### 4.3 손계산 — PE 하나에서 GLB 접근 세기

가장 단순한 모델을 쓰자. **PE 하나 + 각 피연산자마다 레지스터 1칸**. 필요한 값이 레지스터에 없으면 GLB에서 읽고, 부분합이 쫓겨나면 GLB에 쓴다. 공간 재사용(이웃 PE끼리 넘기기)은 무시하고 **시간 재사용**만 본다. 루프 순서가 곧 dataflow다.

```
WS: for k,c,r,s (weight 고정):  for p,q (출력 픽셀):  MAC
    weight 읽기 = weight 개수 = K·C·R·S = 4·4·9 = 144
    input 읽기  = 매 MAC = 9,216
    psum 쓰기   = 매 MAC = 9,216,  psum 읽기 = 9,216 − 256(첫 누산은 0에서 시작) = 8,960

OS: for k,p,q (출력 고정):  for c,r,s:  MAC
    weight 읽기 = input 읽기 = 9,216,   psum 쓰기 = 출력 개수 = 256,  psum 읽기 = 0

IS: for c,y,x (입력 고정):  for k,r,s:  MAC
    input 읽기 = 입력 개수 = C·H·W = 400,  weight 9,216,  psum은 WS와 같다

RS: for k,c,p,r (필터 행 고정, RF에 weight 3개·input 창 3개):  for q,s:  MAC
    weight 읽기 = K·C·P·R·S = 4·4·8·3·3 = 1,152
    input 읽기  = K·C·P·R·W = 4·4·8·3·10 = 3,840
    psum 쓰기   = K·C·P·R·Q = 4·4·8·3·8 = 3,072,  psum 읽기 = 3,072 − 256 = 2,816
```

말로 하면: WS는 weight를 완벽하게 재사용하지만 psum을 MAC마다 GLB에 왕복시킨다. OS는 psum을 완벽하게 재사용하지만 weight·input을 MAC마다 읽는다. RS는 RF를 조금 더 크게(3 + 3 + 1칸) 써서 셋 다 적당히 줄인다.

### 4.4 코드로 확인 — dataflow별 접근 횟수 (예제 5)

위 루프 네 개를 실제로 돌리면서 conv를 **진짜로 계산**하고(참조와 비교), LRU 레지스터 모델로 GLB 접근을 센다. 에너지는 Eyeriss의 상대 비용(RF 1, GLB 6; D7 2.3절)으로 환산한다.

```python
# 한 PE + 작은 레지스터 파일(RF) 모델로 dataflow별 글로벌 버퍼(GLB) 접근 횟수를 센다 (conv를 실제로 계산하며 검증)
import numpy as np
from collections import OrderedDict
def run(order, rf, C=4, K=4, H=10, R=3):
    P = H - R + 1; rng = np.random.default_rng(0)
    x = rng.integers(-4, 5, (C, H, H)); w = rng.integers(-4, 5, (K, C, R, R))
    out = {}; cnt = dict(W=0, I=0, Pr=0, Pw=0, RF=0)
    caches = {t: OrderedDict() for t in "WIP"}
    def touch(t, key):                       # LRU RF: 있으면 RF 접근, 없으면 GLB에서 가져온다
        c = caches[t]; cnt["RF"] += 1
        if key in c: c.move_to_end(key); return
        if t == "P" and key in out: cnt["Pr"] += 1   # 이전 부분합을 GLB에서 다시 읽음
        if t in "WI": cnt[t] += 1
        c[key] = 1
        if len(c) > rf[t]:
            old, _ = c.popitem(last=False)
            if t == "P": cnt["Pw"] += 1              # 쫓겨나는 부분합은 GLB에 써야 한다
    for k, c, p, q, r, s in order(C, K, P, R):
        touch("W", (k, c, r, s)); touch("I", (c, p + r, q + s)); touch("P", (k, p, q))
        out[(k, p, q)] = out.get((k, p, q), 0) + w[k, c, r, s] * x[c, p + r, q + s]
    cnt["Pw"] += len(caches["P"])                    # 마지막 flush
    ref = np.array([[[np.sum(w[k] * x[:, p:p+R, q:q+R]) for q in range(P)] for p in range(P)] for k in range(K)])
    ok = all(out[(k, p, q)] == ref[k, p, q] for k in range(K) for p in range(P) for q in range(P))
    return cnt, ok
from itertools import product
WS = lambda C,K,P,R: ((k,c,p,q,r,s) for k,c,r,s,p,q in product(range(K),range(C),range(R),range(R),range(P),range(P)))
OS = lambda C,K,P,R: ((k,c,p,q,r,s) for k,p,q,c,r,s in product(range(K),range(P),range(P),range(C),range(R),range(R)))
IS = lambda C,K,P,R: ((k,c,y-r,x-s,r,s) for c,y,x,k,r,s in product(range(C),range(P+R-1),range(P+R-1),range(K),range(R),range(R)) if 0<=y-r<P and 0<=x-s<P)
RS = lambda C,K,P,R: ((k,c,p,q,r,s) for k,c,p,r,q,s in product(range(K),range(C),range(P),range(R),range(P),range(R)))
one = dict(W=1, I=1, P=1)
for name, order, rf in [("WS", WS, one), ("OS", OS, one), ("IS", IS, one), ("RS", RS, dict(W=3, I=3, P=1))]:
    c, ok = run(order, rf); glb = c["W"] + c["I"] + c["Pr"] + c["Pw"]
    print(f"{name}: W={c['W']:5d} I={c['I']:5d} Psum r/w={c['Pr']:5d}/{c['Pw']:5d} GLB={glb:6d} "
          f"energy(RF=1,GLB=6)={c['RF'] + 6 * glb:6d} ok={ok}")
print("MACs =", 4 * 4 * 8 * 8 * 9, " weights =", 4 * 4 * 9, " inputs =", 4 * 10 * 10, " outputs =", 4 * 8 * 8)
```

```text
WS: W=  144 I= 9216 Psum r/w= 8960/ 9216 GLB= 27536 energy(RF=1,GLB=6)=192864 ok=True
OS: W= 9216 I= 9216 Psum r/w=    0/  256 GLB= 18688 energy(RF=1,GLB=6)=139776 ok=True
IS: W= 9216 I=  400 Psum r/w= 8960/ 9216 GLB= 27792 energy(RF=1,GLB=6)=194400 ok=True
RS: W= 1152 I= 3840 Psum r/w= 2816/ 3072 GLB= 10880 energy(RF=1,GLB=6)= 92928 ok=True
MACs = 9216  weights = 144  inputs = 400  outputs = 256
```

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<line x1="90" y1="260" x2="650" y2="260" stroke="currentColor"/><line x1="90" y1="260" x2="90" y2="30" stroke="currentColor"/><line x1="86" y1="260.0" x2="650" y2="260.0" stroke="currentColor" stroke-opacity="0"/><text x="82.0" y="264.0" font-size="12" text-anchor="end">0</text><line x1="86" y1="183.3" x2="650" y2="183.3" stroke="currentColor" stroke-opacity="0.12"/><text x="82.0" y="187.3" font-size="12" text-anchor="end">10,000</text><line x1="86" y1="106.7" x2="650" y2="106.7" stroke="currentColor" stroke-opacity="0.12"/><text x="82.0" y="110.7" font-size="12" text-anchor="end">20,000</text><line x1="86" y1="30.0" x2="650" y2="30.0" stroke="currentColor" stroke-opacity="0.12"/><text x="82.0" y="34.0" font-size="12" text-anchor="end">30,000</text><rect x="130" y="258.9" width="70" height="1.1" fill="#4a7bd0" fill-opacity="0.8"/><rect x="130" y="188.2" width="70" height="70.7" fill="#3f9a6b" fill-opacity="0.8"/><rect x="130" y="48.9" width="70" height="139.3" fill="#e08a3c" fill-opacity="0.8"/><text x="165.0" y="278.0" font-size="13" text-anchor="middle">WS</text><text x="165.0" y="42.9" font-size="12" text-anchor="middle">27,536</text><rect x="265" y="189.3" width="70" height="70.7" fill="#4a7bd0" fill-opacity="0.8"/><rect x="265" y="118.7" width="70" height="70.7" fill="#3f9a6b" fill-opacity="0.8"/><rect x="265" y="116.7" width="70" height="2.0" fill="#e08a3c" fill-opacity="0.8"/><text x="300.0" y="278.0" font-size="13" text-anchor="middle">OS</text><text x="300.0" y="110.7" font-size="12" text-anchor="middle">18,688</text><rect x="400" y="189.3" width="70" height="70.7" fill="#4a7bd0" fill-opacity="0.8"/><rect x="400" y="186.3" width="70" height="3.1" fill="#3f9a6b" fill-opacity="0.8"/><rect x="400" y="46.9" width="70" height="139.3" fill="#e08a3c" fill-opacity="0.8"/><text x="435.0" y="278.0" font-size="13" text-anchor="middle">IS</text><text x="435.0" y="40.9" font-size="12" text-anchor="middle">27,792</text><rect x="535" y="251.2" width="70" height="8.8" fill="#4a7bd0" fill-opacity="0.8"/><rect x="535" y="221.7" width="70" height="29.4" fill="#3f9a6b" fill-opacity="0.8"/><rect x="535" y="176.6" width="70" height="45.1" fill="#e08a3c" fill-opacity="0.8"/><text x="570.0" y="278.0" font-size="13" text-anchor="middle">RS</text><text x="570.0" y="170.6" font-size="12" text-anchor="middle">10,880</text><rect x="110" y="290" width="14" height="14" fill="#4a7bd0" fill-opacity="0.8"/><text x="130.0" y="302.0" font-size="12" text-anchor="start">weight 읽기</text><rect x="280" y="290" width="14" height="14" fill="#3f9a6b" fill-opacity="0.8"/><text x="300.0" y="302.0" font-size="12" text-anchor="start">input 읽기</text><rect x="450" y="290" width="14" height="14" fill="#e08a3c" fill-opacity="0.8"/><text x="470.0" y="302.0" font-size="12" text-anchor="start">psum 읽기+쓰기</text><text x="20.0" y="145.0" font-size="12" text-anchor="middle" transform="rotate(-90 20 145)">GLB 접근 횟수</text>
</svg>
```

그림 6 — 예제 5의 GLB 접근 횟수(쌓은 막대). WS는 weight(파랑)가 거의 0이지만 psum(주황)이 크고, OS는 psum이 거의 0이지만 weight·input이 크다. RS는 셋 다 작다.

출력에서 볼 것: 네 dataflow 모두 `ok=True`로 같은 conv를 정확히 계산한다. **같은 계산, 같은 MAC 수인데 GLB 접근은 10,880 ~ 27,792로 2.5배 차이**가 난다. 손계산 숫자(144, 8,960, 1,152, 3,840, 2,816, 3,072)가 전부 그대로 나온다. 상대 에너지는 RS 92,928 < OS 139,776 < WS 192,864 < IS 194,400이다.

### 4.5 해석 — 이 모델이 말하는 것과 말하지 않는 것

이 장난감 모델은 **"PE 하나의 RF에서의 시간 재사용"** 만 본다. 실제 배열에서는:

- WS의 psum이 GLB가 아니라 **이웃 PE로** 흘러간다(systolic). 그러면 psum 비용은 GLB(6)가 아니라 PE 간 전달(Eyeriss 기준 약 2)이 된다. 3절의 systolic array가 바로 이것이다. 즉 WS의 약점은 배열 구조로 상당 부분 가려진다.
- 입력을 여러 PE에 한 번에 뿌리는 **multicast/broadcast**가 있으면 input 읽기가 줄어든다.
- 레이어 모양에 따라 승자가 바뀐다. weight가 많고 출력 픽셀이 적은 FC·transformer 층에서는 OS가 불리하지 않을 수 있고, 입력 해상도가 큰 앞쪽 conv에서는 WS의 weight 재사용이 크다.

그래서 실무에서 중요한 것은 "어느 dataflow가 최고냐"가 아니라 **"내 모델의 주요 레이어 모양에서 이 NPU의 dataflow가 무엇을 싸게 만들고 무엇을 비싸게 만드냐"** 다. 벤더에게 물을 질문이다(9절). 더 정교한 분석에는 Timeloop/Accelergy, MAESTRO 같은 공개 도구가 쓰인다.

### 4.6 에너지와 연결 (D7)

D7에서 본 비율을 기억하자: RF ≈ 1, PE 간 전달 ≈ 2, GLB ≈ 6, DRAM ≈ 200(Eyeriss 상대 비용, MAC = 1). GLB 접근 1만 번이 DRAM 접근 300번과 비슷하다. 그러니 dataflow가 GLB 접근을 2.5배 줄이는 것도 크지만, **DRAM 접근을 줄이는 것(5절 tiling)이 한 자릿수 더 크다**. dataflow는 on-chip 에너지를, tiling은 off-chip 에너지를 다룬다.

### 4.7 함정

- **dataflow 이름을 외우고 끝낸다**: 면접에서는 "무엇이 머무르고, 무엇이 움직이고, 그래서 어떤 레이어에서 유리한가"까지 말해야 한다.
- **RF를 키우면 다 해결된다고 생각한다**: RF가 커지면 RF 접근 자체가 비싸지고 PE 면적이 커져서 PE 수가 줄어든다. Eyeriss의 PE당 RF는 수백 바이트 수준이었다.
- **모델은 모델이다**: 이 절의 숫자는 비율 감각용이다. 실제 칩의 에너지 분해는 벤더 power model이나 실측으로만 안다.

---

## 5. On-chip SRAM과 tiling

### 5.1 직관 — 작업대와 창고

NPU의 SRAM은 **작업대**, DRAM은 **창고**다. 작업대는 작지만 손만 뻗으면 되고(싸고 빠름), 창고는 크지만 다녀오는 데 오래 걸리고 힘들다(DRAM 바이트는 on-chip 1 MB SRAM의 수~십수 배 에너지, D7). 레이어 하나의 weight·입력·출력이 작업대에 다 안 올라가면 **조각(tile)으로 나눠서** 가져와야 한다. 문제는 나누는 방식에 따라 **같은 데이터를 창고에서 몇 번 다시 가져오느냐**가 달라진다는 것이다.

SSD 비유: FTL 매핑 테이블이 DRAM에 다 안 들어가면 일부만 캐시하고 나머지는 NAND에서 가져온다. 접근 패턴을 알면 캐시 적중률을 설계할 수 있다. NPU에서는 접근 패턴을 **컴파일러가 완전히 안다**(정적 그래프). 그래서 캐시 대신 **명시적으로 관리되는 SRAM(scratchpad)** 과 DMA를 쓰고, 무엇을 언제 가져올지 컴파일러가 미리 계산한다.

### 5.2 트래픽 공식 — 손으로 유도

GEMM `C[M×N] = A[M×K] · B[K×N]`(int8, 누산 int32)을 `Tm × Tn` 출력 타일로 나누고, 각 출력 타일에 대해 K 방향을 `Tk`씩 끝까지 누산한다고 하자(출력 타일은 SRAM의 int32 누산 버퍼에 머무름 = 타일 수준 output-stationary).

```
출력 타일 하나를 만들려면 A의 Tm×K 띠와 B의 K×Tn 띠가 필요하다.
A 전체는 N 방향 타일 수만큼 다시 읽힌다:   A 트래픽 = M·K · ⌈N/Tn⌉
B 전체는 M 방향 타일 수만큼 다시 읽힌다:   B 트래픽 = K·N · ⌈M/Tm⌉
C는 한 번 쓴다:                          C 트래픽 = M·N
SRAM 필요량 = 2·(Tm·Tk + Tk·Tn)  (A·B 타일 double buffer)  +  4·Tm·Tn  (int32 누산)
```

말로 하면: 출력 타일이 **클수록** A와 B를 다시 읽는 횟수가 줄지만, 누산 버퍼(4·Tm·Tn)가 SRAM을 먹는다. SRAM 크기가 타일 크기의 상한을 정하고, 타일 크기가 DRAM 트래픽을 정한다. D3 4.9절의 "SRAM 크기가 intensity 상한을 정한다"와 같은 이야기다.

작은 transformer의 FFN 확장층(M = 256 token, K = 512, N = 2048, int8)으로 손계산해 보자. 각 텐서를 한 번씩만 옮기는 최소(compulsory) 트래픽은 256·512 + 512·2048 + 256·2048 = 131,072 + 1,048,576 + 524,288 = 1,703,936 B ≈ 1.63 MiB다. 16×16 타일로 순진하게 자르면:

```
A: 131,072 × (2048/16 = 128) = 16,777,216
B: 1,048,576 × (256/16 = 16) = 16,777,216
C: 524,288
합계 34,078,720 B ≈ 32.5 MiB  → 최소의 20배
```

SRAM 64 KiB에서 Tm = 128, Tn = 112를 고르면:

```
A: 131,072 × ⌈2048/112⌉ = 131,072 × 19 = 2,490,368
B: 1,048,576 × ⌈256/128⌉ = 1,048,576 × 2 = 2,097,152
C: 524,288
합계 5,111,808 B ≈ 4.88 MiB  → 최소의 3배
SRAM: 2·(128·16 + 16·112) + 4·128·112 = 7,680 + 57,344 = 65,024 B ≤ 65,536
```

같은 64 KiB 안에서 타일 모양만 바꿔 DRAM 트래픽이 6.7배 줄었다.

### 5.3 코드로 확인 — tiling planner (예제 6)

가능한 (Tm, Tn, Tk)를 전부 훑어서 SRAM 예산 안에서 트래픽이 최소인 것을 고른다. 실제 NPU 컴파일러의 tiling 탐색을 아주 단순하게 만든 것이다.

```python
# SRAM 크기가 주어졌을 때 GEMM 타일(Tm, Tn, Tk)을 골라 DRAM 트래픽을 최소화하는 작은 planner
from math import ceil
M, K, N = 256, 512, 2048          # 예: 작은 transformer FFN 확장층, 256 token, int8
def traffic(Tm, Tn):              # A는 n-타일마다, B는 m-타일마다 다시 읽는다. C(int8)는 한 번 쓴다
    return M * K * ceil(N / Tn) + K * N * ceil(M / Tm) + M * N
def sram(Tm, Tn, Tk):             # A·B 타일은 double buffer(×2), 누산기는 int32(×4)
    return 2 * (Tm * Tk + Tk * Tn) + 4 * Tm * Tn
def plan(budget):
    best = None
    for Tm in range(16, M + 1, 16):
        for Tn in range(16, N + 1, 16):
            for Tk in (16, 32, 64, 128, 256, 512):
                if sram(Tm, Tn, Tk) > budget: continue
                key = (traffic(Tm, Tn), -Tk)   # 트래픽 최소, 같으면 큰 Tk(DMA 횟수 적음)
                if best is None or key < best[0]: best = (key, Tm, Tn, Tk)
    return best
MIN = M * K + K * N + M * N; MACS = M * K * N
print(f"MACs={MACS/1e6:.1f}M  compulsory traffic={MIN/1024:.0f} KiB")
print(f"naive 16x16 tiles: {traffic(16, 16)/2**20:6.2f} MiB")
for kb in (16, 32, 64, 128, 256, 512, 1024, 2048):
    (t, _), Tm, Tn, Tk = plan(kb * 1024)
    print(f"SRAM {kb:5d} KiB: Tm={Tm:3d} Tn={Tn:4d} Tk={Tk:3d} used={sram(Tm,Tn,Tk)/1024:6.1f} KiB "
          f"DRAM={t/2**20:5.2f} MiB ({t/MIN:4.2f}x min)  MAC/B={MACS/t:6.1f}")
```

```text
MACs=268.4M  compulsory traffic=1664 KiB
naive 16x16 tiles:  32.50 MiB
SRAM    16 KiB: Tm= 64 Tn=  48 Tk= 16 used=  15.5 KiB DRAM= 9.88 MiB (6.08x min)  MAC/B=  25.9
SRAM    32 KiB: Tm= 64 Tn=  96 Tk= 16 used=  29.0 KiB DRAM= 7.25 MiB (4.46x min)  MAC/B=  35.3
SRAM    64 KiB: Tm=128 Tn= 112 Tk= 16 used=  63.5 KiB DRAM= 4.88 MiB (3.00x min)  MAC/B=  52.5
SRAM   128 KiB: Tm=128 Tn= 208 Tk= 32 used= 125.0 KiB DRAM= 3.75 MiB (2.31x min)  MAC/B=  68.3
SRAM   256 KiB: Tm=256 Tn= 240 Tk= 16 used= 255.5 KiB DRAM= 2.62 MiB (1.62x min)  MAC/B=  97.5
SRAM   512 KiB: Tm=256 Tn= 416 Tk= 64 used= 500.0 KiB DRAM= 2.12 MiB (1.31x min)  MAC/B= 120.5
SRAM  1024 KiB: Tm=256 Tn= 688 Tk=128 used= 924.0 KiB DRAM= 1.88 MiB (1.15x min)  MAC/B= 136.5
SRAM  2048 KiB: Tm=256 Tn=1024 Tk=256 used=1664.0 KiB DRAM= 1.75 MiB (1.08x min)  MAC/B= 146.3
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="250" x2="640" y2="250" stroke="currentColor"/><line x1="70" y1="250" x2="70" y2="30" stroke="currentColor"/><line x1="66" y1="250.0" x2="70" y2="250.0" stroke="currentColor"/><text x="62.0" y="254.0" font-size="12" text-anchor="end">0</text><line x1="66" y1="213.3" x2="70" y2="213.3" stroke="currentColor"/><text x="62.0" y="217.3" font-size="12" text-anchor="end">2</text><line x1="66" y1="176.7" x2="70" y2="176.7" stroke="currentColor"/><text x="62.0" y="180.7" font-size="12" text-anchor="end">4</text><line x1="66" y1="140.0" x2="70" y2="140.0" stroke="currentColor"/><text x="62.0" y="144.0" font-size="12" text-anchor="end">6</text><line x1="66" y1="103.3" x2="70" y2="103.3" stroke="currentColor"/><text x="62.0" y="107.3" font-size="12" text-anchor="end">8</text><line x1="66" y1="66.7" x2="70" y2="66.7" stroke="currentColor"/><text x="62.0" y="70.7" font-size="12" text-anchor="end">10</text><line x1="66" y1="30.0" x2="70" y2="30.0" stroke="currentColor"/><text x="62.0" y="34.0" font-size="12" text-anchor="end">12</text><line x1="70.0" y1="250" x2="70.0" y2="254" stroke="currentColor"/><text x="70.0" y="268.0" font-size="12" text-anchor="middle">16K</text><line x1="151.4" y1="250" x2="151.4" y2="254" stroke="currentColor"/><text x="151.4" y="268.0" font-size="12" text-anchor="middle">32K</text><line x1="232.9" y1="250" x2="232.9" y2="254" stroke="currentColor"/><text x="232.9" y="268.0" font-size="12" text-anchor="middle">64K</text><line x1="314.3" y1="250" x2="314.3" y2="254" stroke="currentColor"/><text x="314.3" y="268.0" font-size="12" text-anchor="middle">128K</text><line x1="395.7" y1="250" x2="395.7" y2="254" stroke="currentColor"/><text x="395.7" y="268.0" font-size="12" text-anchor="middle">256K</text><line x1="477.1" y1="250" x2="477.1" y2="254" stroke="currentColor"/><text x="477.1" y="268.0" font-size="12" text-anchor="middle">512K</text><line x1="558.6" y1="250" x2="558.6" y2="254" stroke="currentColor"/><text x="558.6" y="268.0" font-size="12" text-anchor="middle">1M</text><line x1="640.0" y1="250" x2="640.0" y2="254" stroke="currentColor"/><text x="640.0" y="268.0" font-size="12" text-anchor="middle">2M</text><line x1="70" y1="220.2" x2="640" y2="220.2" stroke="#d0564a" stroke-dasharray="5 4"/><text x="80.0" y="236.2" font-size="12" text-anchor="start">compulsory 1.63 MiB (각 텐서 1회)</text><polyline points="70.0,68.9 151.4,117.1 232.9,160.5 314.3,181.2 395.7,202.0 477.1,211.1 558.6,215.5 640.0,217.9" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="70.0" cy="68.9" r="3.5" fill="#4a7bd0"/><text x="76.0" y="60.9" font-size="12" text-anchor="start">9.88</text><circle cx="151.4" cy="117.1" r="3.5" fill="#4a7bd0"/><text x="157.4" y="109.1" font-size="12" text-anchor="start">7.25</text><circle cx="232.9" cy="160.5" r="3.5" fill="#4a7bd0"/><text x="238.9" y="152.5" font-size="12" text-anchor="start">4.88</text><circle cx="314.3" cy="181.2" r="3.5" fill="#4a7bd0"/><text x="320.3" y="173.2" font-size="12" text-anchor="start">3.75</text><circle cx="395.7" cy="202.0" r="3.5" fill="#4a7bd0"/><text x="401.7" y="194.0" font-size="12" text-anchor="start">2.62</text><circle cx="477.1" cy="211.1" r="3.5" fill="#4a7bd0"/><text x="483.1" y="203.1" font-size="12" text-anchor="start">2.12</text><circle cx="558.6" cy="215.5" r="3.5" fill="#4a7bd0"/><text x="564.6" y="207.5" font-size="12" text-anchor="start">1.88</text><circle cx="640.0" cy="217.9" r="3.5" fill="#4a7bd0"/><text x="646.0" y="209.9" font-size="12" text-anchor="start">1.75</text><text x="355.0" y="288.0" font-size="12" text-anchor="middle">on-chip SRAM 크기 (log 눈금) — GEMM 256×512×2048 int8, planner 최적 타일</text><text x="18.0" y="140.0" font-size="12" text-anchor="middle" transform="rotate(-90 18 140.0)">DRAM 트래픽 (MiB)</text>
</svg>
```

그림 7 — SRAM 크기별 최적 타일의 DRAM 트래픽(예제 6 실측). 16 KiB에서 최소의 6배, 256 KiB에서 1.6배, 2 MiB에서 1.08배. SRAM을 두 배로 늘릴 때마다 이득이 줄어든다(수확 체감).

출력에서 볼 것:

- 손계산한 64 KiB의 답(Tm = 128, Tn = 112, 4.88 MiB, 3.00×)이 그대로 나온다.
- MAC/B(DRAM 바이트당 MAC, 즉 DRAM 기준 arithmetic intensity)가 16 KiB에서 25.9, 2 MiB에서 146이다. D3의 roofline에 올리면: 예를 들어 DRAM 8 GB/s, 1 TMAC/s인 가상의 NPU라면 ridge point는 125 MAC/B다. **SRAM 512 KiB까지는 이 레이어가 memory-bound(120.5 < 125), 1 MiB부터 compute-bound(136.5 > 125)**다. SRAM 크기가 이 레이어의 병목 자체를 바꾼다.
- Tn = 48, 112, 208처럼 "예쁘지 않은" 숫자가 나온다. ⌈N/Tn⌉이 정수로 떨어지는 지점을 찾기 때문이다. 실제 컴파일러도 배열 크기(16의 배수), DMA burst, bank 충돌까지 고려해서 이런 모양을 고른다.

이 planner가 빼먹은 것: weight가 DRAM이 아니라 flash(XIP)에 있을 수 있고(MCU, D2 1.3절), 누산기를 int32로 두지 않고 부분합을 DRAM에 흘리는 선택지, conv의 halo(5.6절), bank 충돌이다. 그래도 **"SRAM이 작으면 트래픽이 몇 배 늘어나는가"** 라는 1차 질문에는 이걸로 충분히 답한다.

### 5.4 SRAM 크기는 어떻게 정하나 (M1, D2)

칩을 고를 때 on-chip SRAM 크기는 TOPS보다 중요할 때가 많다. 두 가지를 본다.

1. **peak activation이 SRAM에 들어가는가** (D2): 레이어 사이의 activation이 SRAM에 머무르면 레이어 간 DRAM 왕복이 없다. 예를 들어 KWS DS-CNN이면 activation peak가 수십 KB라 수백 KB SRAM이면 모델 전체가 on-chip에서 돈다.
2. **가장 큰 레이어의 타일이 충분히 큰가**: 그림 7처럼 SRAM이 작으면 weight가 여러 번 다시 읽힌다. LLM decode처럼 weight를 한 번씩만 쓰는 경우(GEMV)에는 SRAM을 아무리 키워도 weight 트래픽은 못 줄인다. 모델 크기 전체가 들어가지 않는 한 대역폭이 답이다(D5).

### 5.5 Double buffering — DMA와 계산을 겹치기 (예제 7)

타일을 가져오는(DMA load) 동안 MAC 배열이 놀면 안 된다. 버퍼를 두 개 두고 **하나로 계산하는 동안 다른 하나에 다음 타일을 채운다**. Don에게는 SSD의 ping-pong 버퍼나 오디오 I2S DMA의 half/full 인터럽트와 똑같은 이야기다.

손계산: 타일 6개, load 3, compute 4, store 1 (단위 시간).

```
serial (겹침 없음) : 6 × (3 + 4 + 1) = 48
double buffering  : 처음 load 3 + compute 6 × 4 + 마지막 store 1 = 28   (compute가 병목)
memory-bound(load 6): load 6 × 6 + 마지막 compute 4 + store 1 = 41     (DMA가 병목)
```

말로 하면: 겹치고 나면 전체 시간은 **느린 쪽(max(load, compute)) × 타일 수 + 양 끝 한 번씩**이 된다. 버퍼를 3개로 늘려도 병목 자원이 100% 바쁘면 더 빨라지지 않는다.

```python
# DMA(load) · compute · DMA(store)를 타일 단위로 스케줄한다: 버퍼 개수가 겹침(overlap)을 결정한다
def schedule(n, L, Cc, S, nbuf, serial=False):
    ld, cp, st = [], [], []            # 각 타일의 (start, end)
    for i in range(n):
        if serial:                     # 아무것도 안 겹침: load → compute → store → 다음 타일
            t0 = st[-1][1] if st else 0
            ld.append((t0, t0 + L)); cp.append((t0 + L, t0 + L + Cc)); st.append((t0 + L + Cc, t0 + L + Cc + S))
            continue
        free = cp[i - nbuf][1] if i >= nbuf else 0          # 입력 버퍼가 비는 시각
        s = max(ld[-1][1] if ld else 0, free); ld.append((s, s + L))
        s = max(ld[i][1], cp[-1][1] if cp else 0); cp.append((s, s + Cc))
        s = max(cp[i][1], st[-1][1] if st else 0); st.append((s, s + S))
    return st[-1][1], ld, cp, st
n = 6
for label, L, Cc, S in [("compute-bound", 3, 4, 1), ("memory-bound ", 6, 4, 1)]:
    for name, nb, ser in [("serial", 1, True), ("1 buffer", 1, False), ("2 buffers", 2, False), ("3 buffers", 3, False)]:
        T, ld, cp, st = schedule(n, L, Cc, S, nb, ser)
        busy = n * Cc / T
        print(f"{label} {name:9s}: total={T:3d}  MAC unit busy={busy*100:5.1f}%")
    print(f"  bound: max(n*L, n*C) + 나머지 = {max(n*L, n*Cc) + min(L, Cc) + S}")
T, ld, cp, st = schedule(n, 3, 4, 1, 2)
print("2-buffer compute-bound load:", ld); print("compute:", cp); print("store:", st)
```

```text
compute-bound serial   : total= 48  MAC unit busy= 50.0%
compute-bound 1 buffer : total= 43  MAC unit busy= 55.8%
compute-bound 2 buffers: total= 28  MAC unit busy= 85.7%
compute-bound 3 buffers: total= 28  MAC unit busy= 85.7%
  bound: max(n*L, n*C) + 나머지 = 28
memory-bound  serial   : total= 66  MAC unit busy= 36.4%
memory-bound  1 buffer : total= 61  MAC unit busy= 39.3%
memory-bound  2 buffers: total= 41  MAC unit busy= 58.5%
memory-bound  3 buffers: total= 41  MAC unit busy= 58.5%
  bound: max(n*L, n*C) + 나머지 = 41
2-buffer compute-bound load: [(0, 3), (3, 6), (7, 10), (11, 14), (15, 18), (19, 22)]
compute: [(3, 7), (7, 11), (11, 15), (15, 19), (19, 23), (23, 27)]
store: [(7, 8), (11, 12), (15, 16), (19, 20), (23, 24), (27, 28)]
```

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<text x="10.0" y="32.0" font-size="13" text-anchor="start" font-weight="bold">serial</text><text x="102.0" y="57.0" font-size="12" text-anchor="end">DMA load</text><rect x="110" y="42" width="32" height="22" fill="#4a7bd0" fill-opacity="0.75"/><text x="126.5" y="57.0" font-size="12" text-anchor="middle">0</text><rect x="198" y="42" width="32" height="22" fill="#4a7bd0" fill-opacity="0.75"/><text x="214.5" y="57.0" font-size="12" text-anchor="middle">1</text><rect x="286" y="42" width="32" height="22" fill="#4a7bd0" fill-opacity="0.75"/><text x="302.5" y="57.0" font-size="12" text-anchor="middle">2</text><rect x="374" y="42" width="32" height="22" fill="#4a7bd0" fill-opacity="0.75"/><text x="390.5" y="57.0" font-size="12" text-anchor="middle">3</text><rect x="462" y="42" width="32" height="22" fill="#4a7bd0" fill-opacity="0.75"/><text x="478.5" y="57.0" font-size="12" text-anchor="middle">4</text><rect x="550" y="42" width="32" height="22" fill="#4a7bd0" fill-opacity="0.75"/><text x="566.5" y="57.0" font-size="12" text-anchor="middle">5</text><text x="102.0" y="87.0" font-size="12" text-anchor="end">MAC array</text><rect x="143" y="72" width="43" height="22" fill="#e08a3c" fill-opacity="0.75"/><text x="165.0" y="87.0" font-size="12" text-anchor="middle">0</text><rect x="231" y="72" width="43" height="22" fill="#e08a3c" fill-opacity="0.75"/><text x="253.0" y="87.0" font-size="12" text-anchor="middle">1</text><rect x="319" y="72" width="43" height="22" fill="#e08a3c" fill-opacity="0.75"/><text x="341.0" y="87.0" font-size="12" text-anchor="middle">2</text><rect x="407" y="72" width="43" height="22" fill="#e08a3c" fill-opacity="0.75"/><text x="429.0" y="87.0" font-size="12" text-anchor="middle">3</text><rect x="495" y="72" width="43" height="22" fill="#e08a3c" fill-opacity="0.75"/><text x="517.0" y="87.0" font-size="12" text-anchor="middle">4</text><rect x="583" y="72" width="43" height="22" fill="#e08a3c" fill-opacity="0.75"/><text x="605.0" y="87.0" font-size="12" text-anchor="middle">5</text><text x="102.0" y="117.0" font-size="12" text-anchor="end">DMA store</text><rect x="187" y="102" width="10" height="22" fill="#3f9a6b" fill-opacity="0.75"/><text x="192.5" y="117.0" font-size="12" text-anchor="middle">0</text><rect x="275" y="102" width="10" height="22" fill="#3f9a6b" fill-opacity="0.75"/><text x="280.5" y="117.0" font-size="12" text-anchor="middle">1</text><rect x="363" y="102" width="10" height="22" fill="#3f9a6b" fill-opacity="0.75"/><text x="368.5" y="117.0" font-size="12" text-anchor="middle">2</text><rect x="451" y="102" width="10" height="22" fill="#3f9a6b" fill-opacity="0.75"/><text x="456.5" y="117.0" font-size="12" text-anchor="middle">3</text><rect x="539" y="102" width="10" height="22" fill="#3f9a6b" fill-opacity="0.75"/><text x="544.5" y="117.0" font-size="12" text-anchor="middle">4</text><rect x="627" y="102" width="10" height="22" fill="#3f9a6b" fill-opacity="0.75"/><text x="632.5" y="117.0" font-size="12" text-anchor="middle">5</text><line x1="638" y1="38" x2="638" y2="136" stroke="#d0564a" stroke-dasharray="4 3"/><text x="642.0" y="34.0" font-size="12" text-anchor="start">끝 = 48</text><text x="10.0" y="182.0" font-size="13" text-anchor="start" font-weight="bold">2 buffers</text><text x="102.0" y="207.0" font-size="12" text-anchor="end">DMA load</text><rect x="110" y="192" width="32" height="22" fill="#4a7bd0" fill-opacity="0.75"/><text x="126.5" y="207.0" font-size="12" text-anchor="middle">0</text><rect x="143" y="192" width="32" height="22" fill="#4a7bd0" fill-opacity="0.75"/><text x="159.5" y="207.0" font-size="12" text-anchor="middle">1</text><rect x="187" y="192" width="32" height="22" fill="#4a7bd0" fill-opacity="0.75"/><text x="203.5" y="207.0" font-size="12" text-anchor="middle">2</text><rect x="231" y="192" width="32" height="22" fill="#4a7bd0" fill-opacity="0.75"/><text x="247.5" y="207.0" font-size="12" text-anchor="middle">3</text><rect x="275" y="192" width="32" height="22" fill="#4a7bd0" fill-opacity="0.75"/><text x="291.5" y="207.0" font-size="12" text-anchor="middle">4</text><rect x="319" y="192" width="32" height="22" fill="#4a7bd0" fill-opacity="0.75"/><text x="335.5" y="207.0" font-size="12" text-anchor="middle">5</text><text x="102.0" y="237.0" font-size="12" text-anchor="end">MAC array</text><rect x="143" y="222" width="43" height="22" fill="#e08a3c" fill-opacity="0.75"/><text x="165.0" y="237.0" font-size="12" text-anchor="middle">0</text><rect x="187" y="222" width="43" height="22" fill="#e08a3c" fill-opacity="0.75"/><text x="209.0" y="237.0" font-size="12" text-anchor="middle">1</text><rect x="231" y="222" width="43" height="22" fill="#e08a3c" fill-opacity="0.75"/><text x="253.0" y="237.0" font-size="12" text-anchor="middle">2</text><rect x="275" y="222" width="43" height="22" fill="#e08a3c" fill-opacity="0.75"/><text x="297.0" y="237.0" font-size="12" text-anchor="middle">3</text><rect x="319" y="222" width="43" height="22" fill="#e08a3c" fill-opacity="0.75"/><text x="341.0" y="237.0" font-size="12" text-anchor="middle">4</text><rect x="363" y="222" width="43" height="22" fill="#e08a3c" fill-opacity="0.75"/><text x="385.0" y="237.0" font-size="12" text-anchor="middle">5</text><text x="102.0" y="267.0" font-size="12" text-anchor="end">DMA store</text><rect x="187" y="252" width="10" height="22" fill="#3f9a6b" fill-opacity="0.75"/><text x="192.5" y="267.0" font-size="12" text-anchor="middle">0</text><rect x="231" y="252" width="10" height="22" fill="#3f9a6b" fill-opacity="0.75"/><text x="236.5" y="267.0" font-size="12" text-anchor="middle">1</text><rect x="275" y="252" width="10" height="22" fill="#3f9a6b" fill-opacity="0.75"/><text x="280.5" y="267.0" font-size="12" text-anchor="middle">2</text><rect x="319" y="252" width="10" height="22" fill="#3f9a6b" fill-opacity="0.75"/><text x="324.5" y="267.0" font-size="12" text-anchor="middle">3</text><rect x="363" y="252" width="10" height="22" fill="#3f9a6b" fill-opacity="0.75"/><text x="368.5" y="267.0" font-size="12" text-anchor="middle">4</text><rect x="407" y="252" width="10" height="22" fill="#3f9a6b" fill-opacity="0.75"/><text x="412.5" y="267.0" font-size="12" text-anchor="middle">5</text><line x1="418" y1="188" x2="418" y2="286" stroke="#d0564a" stroke-dasharray="4 3"/><text x="422.0" y="184.0" font-size="12" text-anchor="start">끝 = 28</text><line x1="110" y1="315" x2="110" y2="320" stroke="currentColor"/><text x="110.0" y="334.0" font-size="12" text-anchor="middle">0</text><line x1="198" y1="315" x2="198" y2="320" stroke="currentColor"/><text x="198.0" y="334.0" font-size="12" text-anchor="middle">8</text><line x1="286" y1="315" x2="286" y2="320" stroke="currentColor"/><text x="286.0" y="334.0" font-size="12" text-anchor="middle">16</text><line x1="374" y1="315" x2="374" y2="320" stroke="currentColor"/><text x="374.0" y="334.0" font-size="12" text-anchor="middle">24</text><line x1="462" y1="315" x2="462" y2="320" stroke="currentColor"/><text x="462.0" y="334.0" font-size="12" text-anchor="middle">32</text><line x1="550" y1="315" x2="550" y2="320" stroke="currentColor"/><text x="550.0" y="334.0" font-size="12" text-anchor="middle">40</text><line x1="638" y1="315" x2="638" y2="320" stroke="currentColor"/><text x="638.0" y="334.0" font-size="12" text-anchor="middle">48</text><line x1="110" y1="315" x2="638" y2="315" stroke="currentColor"/><text x="374.0" y="352.0" font-size="12" text-anchor="middle">시간 (단위: load 3, compute 4, store 1 · 타일 6개)</text>
</svg>
```

그림 8 — 예제 7의 타임라인(compute-bound 경우). 위: 겹침 없이 48. 아래: 버퍼 2개로 load(파랑)와 compute(주황)가 겹쳐 28. MAC 배열은 3부터 27까지 쉬지 않는다.

출력에서 볼 것: 버퍼 1개는 store만 겹쳐서 48 → 43, 2개면 28(손계산과 같음), 3개도 28이다. memory-bound 경우는 double buffering을 해도 MAC 활용률이 58.5%에서 멈춘다. **겹치기는 병목을 숨길 뿐 없애지 못한다.** memory-bound면 타일을 키우거나(5.3절), fusion(5.6절)이나 양자화로 바이트를 줄여야 한다.

### 5.6 Layer fusion — 중간 텐서를 DRAM에 안 보내기 (예제 8)

C6 4절에서 본 op fusion(conv + BN + ReLU를 커널 하나로)은 원소별 op를 합치는 것이었다. NPU에서는 한 단계 더 나아가 **레이어 두 개를 타일 단위로 이어서** 실행한다. conv1의 출력 타일을 SRAM에 두고, 그걸로 곧장 conv2의 출력 타일을 만든다. 중간 feature map 전체가 DRAM에 나갔다 들어오는 일이 없어진다.

대가가 있다. 3×3 conv 두 개를 이으면 출력 T×T 타일에 중간 (T+2)×(T+2), 입력 (T+4)×(T+4)가 필요하다. 이웃 타일과 겹치는 테두리(**halo**)를 타일마다 **다시 읽고 다시 계산**한다.

```
T = 14, 56×56×16 int8, 타일 16개:
입력 읽기 = 16 × 18² × 16 = 82,944 B,  출력 쓰기 = 50,176 B  → 133,120 B
unfused   = 입력 + 중간 쓰기 + 중간 읽기 + 출력 = 4 × 50,176 = 200,704 B
MAC 증가  = (16² + 14²) / (2 × 14²) − 1 = 452 / 392 − 1 = 15.3%
```

아래 코드는 fused 계산이 unfused와 **정확히 같은지** 작은 크기(16×16, C = 4, T = 4)로 확인하고, 56×56×16 크기에서 타일 크기별 비용을 센다.

```python
# 3x3 conv 두 개를 공간 타일 단위로 fuse: 중간 텐서를 DRAM에 안 쓰는 대신 halo를 재계산한다
import numpy as np
from numpy.lib.stride_tricks import sliding_window_view as swv
def conv3(x, w):                      # x [C,H+2,W+2] (이미 패딩됨) → [K,H,W], ReLU
    return np.maximum(np.einsum("chwrs,kcrs->khw", swv(x, (3, 3), axis=(1, 2)), w), 0)
rng = np.random.default_rng(0); C, H, T = 4, 16, 4
x = rng.integers(-4, 5, (C, H, H)); w1 = rng.integers(-2, 3, (C, C, 3, 3)); w2 = rng.integers(-2, 3, (C, C, 3, 3))
ref = conv3(np.pad(conv3(np.pad(x, ((0,0),(1,1),(1,1))), w1), ((0,0),(1,1),(1,1))), w2)   # unfused
xp = np.pad(x, ((0, 0), (2, 2), (2, 2))); out = np.zeros_like(ref)
for ty in range(0, H, T):
    for tx in range(0, H, T):
        mid = conv3(xp[:, ty:ty + T + 4, tx:tx + T + 4], w1)       # (T+2)x(T+2) 중간 타일 (halo 포함)
        yy, xx = np.arange(ty - 1, ty + T + 1), np.arange(tx - 1, tx + T + 1)
        mid = mid * ((yy[:, None] >= 0) & (yy[:, None] < H) & (xx >= 0) & (xx < H))  # 이미지 밖 = 0 패딩
        out[:, ty:ty + T, tx:tx + T] = conv3(mid, w2)
print("fused == unfused:", np.array_equal(out, ref))
H, C = 56, 16                          # 실제 크기로 비용 계산 (int8)
base_mac = 2 * H * H * C * C * 9
print(f"unfused: DRAM = in {H*H*C} + mid write {H*H*C} + mid read {H*H*C} + out {H*H*C} = {4*H*H*C} B")
for T in (4, 7, 8, 14, 28, 56):
    n = (-(-H // T)) ** 2                                         # 타일 개수
    dram = n * (T + 4) ** 2 * C + H * H * C                         # 타일마다 halo 포함 입력 읽기 + 출력 쓰기
    mac = n * ((T + 2) ** 2 + T * T) * C * C * 9
    sram = ((T + 4) ** 2 + (T + 2) ** 2 + T * T) * C
    print(f"T={T:2d}: DRAM={dram:6d} B  MAC overhead={mac/base_mac-1:6.1%}  SRAM/tile={sram:6d} B")
```

```text
fused == unfused: True
unfused: DRAM = in 50176 + mid write 50176 + mid read 50176 + out 50176 = 200704 B
T= 4: DRAM=250880 B  MAC overhead= 62.5%  SRAM/tile=  1856 B
T= 7: DRAM=174080 B  MAC overhead= 32.7%  SRAM/tile=  4016 B
T= 8: DRAM=163072 B  MAC overhead= 28.1%  SRAM/tile=  4928 B
T=14: DRAM=133120 B  MAC overhead= 15.3%  SRAM/tile= 12416 B
T=28: DRAM=115712 B  MAC overhead=  7.4%  SRAM/tile= 43328 B
T=56: DRAM=107776 B  MAC overhead=  3.6%  SRAM/tile=161600 B
```

출력에서 볼 것:

- `fused == unfused: True`. 단, 이 코드에는 **"이미지 밖 중간값 = 0" 마스크**가 들어 있다. 마스크 줄을 지우고 돌리면 `False`가 되고, 불일치가 16×16의 테두리 60픽셀(16·4 − 4)에만 생긴다. conv2의 zero-padding은 "conv1 출력의 바깥이 0"이라는 뜻인데, halo를 계산하면 이미지 밖 위치에도 conv1 값이 생기기 때문이다. **테두리에서만 틀리는 NPU 출력**은 실제 bring-up에서 흔한 증상이고, 원인은 대개 padding·halo 처리다.
- T = 4면 fused가 오히려 DRAM을 더 쓴다(250,880 > 200,704). halo 재읽기가 중간 텐서 왕복보다 비싸다. T = 14에서 DRAM 34% 절감에 MAC 15% 증가, T = 28이면 42% 절감에 7.4%. 그 대신 타일당 SRAM이 43 KB로 커진다. **fusion도 SRAM 크기와의 거래**다. (T = 56의 DRAM 값은 테두리 padding도 읽는다고 친 보수적 계산이다.)
- 이 아이디어를 극단까지 밀어 MCU의 peak SRAM을 줄인 것이 MCUNetV2의 patch 기반 추론이다(C7 7.4절).

### 5.7 함정

- **SRAM 용량을 전부 쓸 수 있다고 가정**: 실제로는 bank 구조, 정렬, 런타임·드라이버가 쓰는 영역, 다른 IP와의 공유(예: Ethos-U의 shared SRAM 모드) 때문에 쓸 수 있는 양이 줄어든다.
- **DMA descriptor 오버헤드 무시**: 타일이 너무 작으면 DMA 설정·동기화 비용이 데이터 전송보다 커진다.
- **fusion 후 검증을 빼먹음**: 5.6절처럼 경계에서만 틀리는 버그는 평균 오차 지표로는 안 보인다. 레이어별·위치별 diff를 봐야 한다(C8 4절).

---

## 6. 컴파일러와 툴체인 — 모델이 command stream이 되기까지

### 6.1 파이프라인

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="e5d" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs><rect x="10" y="20" width="200" height="60" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="110.0" y="46.0" font-size="12" text-anchor="middle">모델 파일</text><text x="110.0" y="62.0" font-size="12" text-anchor="middle">.tflite / ONNX / .pt</text><line x1="210" y1="50" x2="235" y2="50" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5d)"/><rect x="235" y="20" width="200" height="60" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="335.0" y="46.0" font-size="12" text-anchor="middle">그래프 import</text><text x="335.0" y="62.0" font-size="12" text-anchor="middle">shape 고정 · 검증</text><line x1="435" y1="50" x2="460" y2="50" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5d)"/><rect x="460" y="20" width="200" height="60" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="560.0" y="46.0" font-size="12" text-anchor="middle">partition</text><text x="560.0" y="62.0" font-size="12" text-anchor="middle">NPU 지원? → 아니면 CPU</text><rect x="10" y="130" width="200" height="60" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="110.0" y="156.0" font-size="12" text-anchor="middle">양자화 제약 확인</text><text x="110.0" y="172.0" font-size="12" text-anchor="middle">int8 per-channel · 스케일</text><line x1="210" y1="160" x2="235" y2="160" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5d)"/><rect x="235" y="130" width="200" height="60" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="335.0" y="156.0" font-size="12" text-anchor="middle">tiling · 스케줄</text><text x="335.0" y="172.0" font-size="12" text-anchor="middle">SRAM 할당 · DMA · fusion</text><line x1="435" y1="160" x2="460" y2="160" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5d)"/><rect x="460" y="130" width="200" height="60" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0" stroke-width="1.5"/><text x="560.0" y="156.0" font-size="12" text-anchor="middle">코드 생성</text><text x="560.0" y="172.0" font-size="12" text-anchor="middle">command stream · weight 재배치</text><path d="M560,80 L560,100 L110,100 L110,130" fill="none" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5d)"/><rect x="10" y="250" width="200" height="60" rx="5" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b" stroke-width="1.5"/><text x="110.0" y="276.0" font-size="12" text-anchor="middle">NPU 바이너리</text><text x="110.0" y="292.0" font-size="12" text-anchor="middle">(Vela tflite · QNN context …)</text><rect x="235" y="250" width="200" height="60" rx="5" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c" stroke-width="1.5"/><text x="335.0" y="276.0" font-size="12" text-anchor="middle">CPU fallback 부분</text><text x="335.0" y="292.0" font-size="12" text-anchor="middle">TFLM / TFLite 커널</text><rect x="460" y="250" width="210" height="60" rx="5" fill="#888" fill-opacity="0.15" stroke="#888" stroke-width="1.5"/><text x="565.0" y="276.0" font-size="12" text-anchor="middle">리포트</text><text x="565.0" y="292.0" font-size="12" text-anchor="middle">op 매핑 · SRAM · cycle 추정</text><line x1="560" y1="190" x2="110" y2="250" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5d)"/><line x1="560" y1="190" x2="335" y2="250" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5d)"/><line x1="560" y1="190" x2="565" y2="250" stroke="currentColor" stroke-width="1.4" marker-end="url(#e5d)"/>
</svg>
```

그림 9 — NPU 컴파일러의 전형적인 단계. 결과물은 NPU 바이너리, CPU로 떨어진 부분, 그리고 리포트다. 이름과 순서는 툴마다 다르지만 하는 일은 비슷하다.

| 단계 | 하는 일 | 실패하면 |
|---|---|---|
| 그래프 import | TFLite·ONNX 등을 읽고 shape을 확정, 상수 folding (C6) | 동적 shape이면 거부되거나 CPU로 |
| partition | op마다 "NPU가 이 op·이 파라미터 조합을 지원하는가" 판단, 연속된 NPU op를 묶는다 | 미지원 op는 CPU fallback |
| 양자화 제약 확인 | int8/int16인가, per-channel scale 규칙, zero-point 제약, bias 범위 | 제약 위반 op는 CPU로 또는 에러 |
| tiling · 스케줄링 | SRAM 할당, 타일 크기(5절), DMA 순서, double buffering, fusion | SRAM 부족이면 타일이 작아져 느려진다 |
| 코드 생성 | command stream, weight 재배치(NPU가 원하는 순서로 섞고 압축), 레지스터 설정 | — |
| 리포트 | op별 매핑, SRAM·flash 사용량, 사이클 추정 | 읽지 않으면 fallback을 모른다 |

"weight 재배치"를 강조하고 싶다. NPU는 weight를 PE 격자에 까는 순서대로 DRAM에 두기를 원한다(예: 16채널씩 묶어서 interleave). 컴파일러가 이걸 미리 해 두기 때문에, **컴파일된 모델의 weight 바이트는 원본 모델 파일의 weight와 순서가 다르다**. bring-up 중 메모리 덤프를 볼 때 알아야 할 사실이다.

### 6.2 CPU fallback은 왜 비싼가 (예제 9)

C6 12절에서 fallback의 모양을 봤다. 여기서는 비용을 숫자로 본다. NPU ↔ CPU 경계를 넘을 때마다 (1) NPU 작업 완료 대기와 인터럽트, (2) 캐시 clean/invalidate, (3) 레이아웃 변환이나 복사, (4) CPU 쪽 커널 실행, (5) 다시 NPU kick이 필요하다. op 자체보다 **경계 비용**이 크다.

작은 CNN(conv, depthwise, HardSwish, …, softmax)에서 NPU가 HardSwish와 Softmax를 지원하지 않는다고 가정한다. 모든 상수는 설명용 가정이다.

```python
# 컴파일러 partition 장난감: 지원 op는 NPU, 아니면 CPU. 경계마다 전환 비용이 붙는다 (모든 상수는 가정)
ops = [  # (이름, MAC, 출력 바이트 int8)
    ("Conv3x3",   5_000_000, 50_000), ("DWConv", 450_000, 50_000), ("Conv1x1", 3_200_000, 50_000),
    ("HardSwish",         0, 50_000), ("DWConv", 450_000, 50_000), ("Conv1x1", 3_200_000, 50_000),
    ("Mean",         50_000,    256), ("FC",         65_536,    256), ("Softmax",       0,     12)]
NPU = 64e9; CPU = 4e9            # MAC/s: NPU 유효 64 GMAC/s, CPU(SIMD) 4 GMAC/s
ELEM_CPU = 1e-9                  # CPU 원소별 op: 원소당 1 ns
SWITCH = 60e-6; BW = 4e9         # 전환 1회: 동기화·캐시 관리 60 µs + 경계 텐서 복사(4 GB/s)
def t_op(name, mac, nb, dev):
    if dev == "NPU": return max(mac / NPU, nb / 16e9)       # NPU는 원소별 op도 빠르다
    return mac / CPU + (nb * ELEM_CPU if mac == 0 else 0)
def run(supported, first_cut=False):
    t, prev, segs, cut = 0.0, None, [], False
    for name, mac, nb in ops:
        dev = "NPU" if name in supported and not cut else "CPU"
        if first_cut and dev == "CPU": cut = True             # Edge TPU식: 첫 미지원 op 이후 전부 CPU
        if prev and dev != prev: t += SWITCH + nb / BW
        if dev != prev: segs.append(dev)
        t += t_op(name, mac, nb, dev); prev = dev
    return t * 1e3, "→".join(segs)
base = {"Conv3x3", "DWConv", "Conv1x1", "Mean", "FC"}
print(f"{'all CPU':25s}: {run(set())[0]:6.3f} ms")
for label, sup, fc in [("NPU, no HardSwish/Softmax", base, False),
                       ("same, first-cut policy", base, True),
                       ("HardSwish -> supported", base | {"HardSwish"}, False),
                       ("Softmax also on NPU", base | {"HardSwish", "Softmax"}, False)]:
    ms, segs = run(sup, fc); print(f"{label:25s}: {ms:6.3f} ms  [{segs}]")
```

```text
all CPU                  :  3.154 ms
NPU, no HardSwish/Softmax:  0.449 ms  [NPU→CPU→NPU→CPU]
same, first-cut policy   :  1.199 ms  [NPU→CPU]
HardSwish -> supported   :  0.257 ms  [NPU→CPU]
Softmax also on NPU      :  0.197 ms  [NPU]
```

출력에서 볼 것:

- 전부 CPU면 3.15 ms, NPU를 쓰면 0.45 ms다. 그런데 HardSwish 하나만 NPU로 옮기면(지원 op로 교체하거나 C6 11절처럼 지원 op 조합으로 분해) **0.26 ms로 43% 더 빨라진다**. HardSwish 자체는 CPU에서 50 µs짜리인데, 경계 두 번의 비용(각 60 µs + 복사)이 더 크다.
- **first-cut 정책**: Google Coral 문서는 Edge TPU 컴파일러가 그래프를 한 번만 나눌 수 있어서, 첫 미지원 op부터 **그 뒤 전부**가 CPU에서 돈다고 설명했다(적어도 당시 문서 기준). 그러면 1.2 ms로, 여러 번 나누는 경우보다 2.7배 느리다. 미지원 op가 그래프 **앞쪽**에 있으면 재앙이다.
- 마지막 Softmax처럼 **그래프 끝의** CPU op 하나는 경계가 한 번뿐이라 상대적으로 싸다. fallback은 "몇 개냐"보다 **"어디에 있고 경계가 몇 번 생기냐"** 가 중요하다.

고치는 법(우선순위 순):

1. 컴파일러 리포트에서 CPU로 떨어진 op와 이유를 찾는다(파라미터 조합 때문인 경우가 많다: 예를 들어 stride, kernel 크기, 채널 수 제한).
2. 그래프 수술로 등가인 지원 op 조합으로 바꾼다(C6 11절).
3. 모델을 바꾼다: HardSwish → ReLU6, GELU → 근사, 동적 연산 → 고정 크기(C7).
4. 경계를 그래프 끝으로 몰아서 한 번만 넘게 한다.
5. 그래도 안 되면 CPU 커널을 최적화한다(CMSIS-NN, NEON, E2).

### 6.3 동적 shape와 제어 흐름이 싫은 이유

NPU 컴파일러는 **모든 텐서의 shape을 컴파일 시점에 알아야** 타일 크기, SRAM 주소, DMA descriptor를 정할 수 있다. 입력 길이가 매번 바뀌는 모델(가변 길이 오디오, LLM의 KV-cache 길이)은:

- 최대 크기로 고정하고 padding하거나(낭비),
- 몇 가지 크기별로 여러 번 컴파일해 두거나(bucket, 메모리 증가),
- 해당 부분만 CPU에서 돌린다.

`if`, `while` 같은 데이터 의존 제어 흐름도 같은 이유로 대부분 CPU로 간다. 펌웨어로 말하면, **DMA descriptor 체인을 부팅 때 한 번 만들어 두고 절대 안 바꾸는 설계**가 NPU가 좋아하는 모양이다.

### 6.4 실제 툴체인 예 (공개 문서 기준, 버전에 따라 다름)

**Arm Ethos-U + Vela.** 가장 문서가 잘 되어 있는 예다. 흐름은 이렇다.

1. 모델을 TFLite **int8**(또는 int16 activation)로 완전 양자화한다(C2).
2. `vela`(Python 패키지 `ethos-u-vela`)로 컴파일한다. 대상 NPU 구성(예: `ethos-u55-128` = U55의 128 MAC/cycle 구성), 시스템 구성·메모리 모드(SRAM만 쓰는지, flash·DRAM과 나눠 쓰는지)를 옵션이나 설정 파일로 준다.
3. 출력은 **다시 .tflite 파일**이다. NPU가 할 수 있는 연속 op 묶음은 `ethos-u`라는 custom operator 하나로 바뀌고, 그 안에 command stream과 재배치된 weight가 들어간다. NPU가 못 하는 op는 원래 TFLite op로 남는다.
4. 기기에서는 TensorFlow Lite Micro가 이 파일을 읽는다. 일반 op는 CPU 커널(CMSIS-NN 등)로, `ethos-u` op는 Ethos-U 드라이버로 넘겨 NPU에서 돈다.
5. Vela는 컴파일 요약으로 SRAM·flash 사용량, NPU/CPU op 개수, 사이클 추정 등을 출력한다.

예시 명령은 대략 이런 모양이다(이 노트에서 실행하지 않았다. 옵션 이름은 설치한 버전의 `vela --help`로 확인할 것).

```sh
pip install ethos-u-vela
vela --accelerator-config ethos-u55-128 --output-dir out/ model_int8.tflite
# → out/model_int8_vela.tflite  (+ 요약 리포트)
```

pre-silicon 단계에서는 Arm이 제공하는 **Corstone-300 FVP**(Fixed Virtual Platform, Cortex-M55 + Ethos-U55 모델) 같은 가상 플랫폼에서 먼저 돌려 볼 수 있다. Don이 FPGA 프로토타입에서 하던 일을 소프트웨어 모델에서 하는 것이다.

**Qualcomm Hexagon NPU + QNN.** Qualcomm AI Engine Direct(QNN) SDK의 HTP(Hexagon Tensor Processor) backend가 NPU를 대상으로 한다. 모델을 QNN 형식으로 변환·양자화한 뒤, 기기에서 매번 그래프를 준비(finalize)하는 시간을 줄이려고 미리 **context binary**를 만들어 두고 로드하는 흐름이 문서화되어 있다. Hexagon의 scalar/vector(HVX)/tensor(HMX) 구성은 E4에서 다룬다. 세부 옵션과 지원 op는 SDK 버전마다 바뀌므로 해당 버전의 문서를 기준으로 삼는다.

**그 밖에**: NXP eIQ(Neutron NPU 등), MediaTek NeuroPilot, Google Coral의 `edgetpu_compiler`, Apple Core ML(ANE로의 배치는 Core ML이 결정)이 있다. 이름과 "모델 → 벤더 컴파일러 → 바이너리 + CPU fallback + 리포트"라는 공통 모양만 기억하면 충분하다.

### 6.5 컴파일러 리포트에서 볼 것

| 항목 | 왜 보나 | 나쁜 신호 |
|---|---|---|
| NPU op 수 / CPU op 수 | fallback 찾기 | CPU op가 그래프 중간에 있다 |
| NPU 구간(subgraph) 개수 | 경계 횟수 | 1보다 많다 |
| SRAM 사용량 / 가용량 | tiling 여유 | 거의 100%면 타일이 작아졌을 가능성 |
| DRAM/flash 트래픽 추정 | memory-bound 여부 | 바이트 대비 MAC이 낮다 (D3) |
| 레이어별 사이클 추정 | 병목 레이어 | depthwise·첫 conv·FC가 튄다 |
| 경고 메시지 | 양자화 규칙 위반, 채널 패딩 | "falling back", "unsupported", "padding" |

### 6.6 함정

- **리포트를 안 읽는다**: 모델이 "돌아가니까" 끝이 아니다. 30%가 CPU에서 돌고 있을 수 있다.
- **컴파일러 버전을 고정하지 않는다**: 같은 모델도 버전에 따라 매핑이 바뀐다. 결과와 함께 SDK·컴파일러·firmware 버전을 기록한다(F8).
- **사이클 추정을 실측으로 착각**: 컴파일러의 추정은 DRAM 경합, 다른 마스터, 온도를 모른다. 실측과 비교해 오차를 알아 둔다.

---

## 7. 실제 NPU 계열 — 공개 자료로 확실한 것만

숫자는 벤더 발표 기준이며 구성·세대에 따라 다르다. 확인되지 않은 세부는 적지 않는다.

| 계열 | 위치 | 공개적으로 알려진 것 | 비고 |
|---|---|---|---|
| Arm Ethos-U55 | MCU급 microNPU | 32 / 64 / 128 / 256 MAC/cycle 구성, int8 weight, int8·int16 activation. Cortex-M55·M85 같은 host와 짝. Vela 컴파일러 | Alif Ensemble 등에 탑재 |
| Arm Ethos-U65 | U55의 상위 | 256 / 512 MAC/cycle 구성, Cortex-A 기반 시스템에도 붙음 | NXP i.MX 93에 탑재 |
| Arm Ethos-U85 | 2024년 발표 | 128~2048 MAC/cycle, 1 GHz에서 최대 약 4 TOPS(Arm 발표), transformer 연산 지원 강화 | 제품 탑재·세부 지원 op는 문서 확인 필요 |
| Google Edge TPU (Coral) | USB·M.2·SoM | int8 기준 4 TOPS, 약 2 TOPS/W(Coral 문서). 완전 int8 TFLite + `edgetpu_compiler`. 파라미터 캐시용 on-chip 메모리 약 8 MB | 첫 미지원 op 이후 CPU (6.2절) |
| Google TPU v1 | 데이터센터 | 256×256 int8 systolic array, 700 MHz, peak 92 TOPS, 24 MiB unified buffer (ISCA 2017) | 이 노트 3절의 원형 |
| MIT Eyeriss | 연구 칩 | 168 PE(12×14), row-stationary, 65 nm (ISCA 2016, JSSC 2017) | 4절의 RS |
| Apple Neural Engine | iPhone·Mac SoC | A11(2017)부터 탑재. Core ML로만 접근. 마케팅 TOPS 외 마이크로아키텍처는 거의 비공개 | 세부를 단정하지 말 것 |
| Qualcomm Hexagon NPU | Snapdragon SoC | scalar·vector(HVX)·tensor(HMX) 가속기 구성, QNN SDK HTP backend | E4에서 자세히 |
| NXP eIQ Neutron, MediaTek APU/NeuroPilot | SoC·MCU | 이름만 기억 | 평가 시 문서로 확인 |

이 표에서 가져갈 것: **MAC/cycle 구성이 설정 가능한 IP(Ethos-U)** 는 SoC 설계자가 면적·전력과 성능을 골라 넣는다. 그래서 같은 "Ethos-U55"라도 칩마다 성능이 8배 다를 수 있다. 칩을 비교할 때는 IP 이름이 아니라 **구성(MAC/cycle), 클럭, SRAM 크기, DRAM 유무**를 물어야 한다.

MCU급 예로 계산해 보자. Ethos-U55-128이 400 MHz로 돈다고 가정하면 peak = 128 × 2 × 4×10⁸ = 102.4 GOPS = 0.1 TOPS다. KWS DS-CNN이 약 2.7M MAC(D7)이라면, 활용률 30%로도 2.7×10⁶ / (128 × 0.3 × 4×10⁸) ≈ 0.18 ms다. 웨어러블의 always-on wake word라면 차고 넘친다. 이런 기기에서 병목은 대개 MAC이 아니라 **SRAM 크기, flash 읽기, 전원 켜고 끄는 비용**이다(E9).

---

## 8. NPU bring-up — Don의 무기

### 8.1 FPGA pre-silicon bring-up과의 대응

| Don이 해 본 것 (SSD 컨트롤러 IP) | NPU에서의 같은 일 |
|---|---|
| FPGA 이미지에서 새 IP 레지스터 ID·버전 읽기 | NPU ID·config 레지스터(MAC 구성, SRAM 크기) 읽기 |
| 클럭·리셋·power domain 시퀀스 | NPU power domain on, clock gate 해제, reset deassert 순서 |
| DMA로 버퍼 보내고 golden과 비교 | 단일 conv 한 개를 NPU로 돌리고 참조 구현과 bit-exact 비교 |
| 메모리 맵·주소 변환 확인 | SMMU/IOMMU 매핑, NPU가 보는 주소 = 드라이버가 준 주소인지 |
| 인터럽트·타임아웃 처리 | 완료 IRQ, watchdog, hang 시 register dump |
| 성능 카운터로 처리량 측정 | NPU perf counter(사이클, stall, DMA 바이트)로 활용률 계산 |
| errata 목록 관리, 벤더 FAE와 이슈 추적 | SDK·firmware·컴파일러 버전 매트릭스, 벤더 errata, 재현 케이스 전달 |
| 전력·온도 margin sign-off | 지속 부하에서 NPU 전력·thermal throttling 측정 |

### 8.2 bring-up 체크리스트 (순서가 중요하다)

1. **전원·클럭·리셋**: power domain, 클럭 소스와 주파수, reset 순서. NPU ID 레지스터를 읽어서 기대값과 비교한다. 여기서 막히면 그 위의 모든 것이 의미 없다.
2. **메모리 맵과 주소 변환**: NPU가 접근할 SRAM·DRAM 영역, SMMU 페이지 테이블, 캐시 속성(cacheable/non-cacheable), 정렬 요구사항. DMA 테스트 패턴(증가 패턴, walking-1)을 NPU DMA로 복사해서 확인한다.
3. **firmware·command stream 로드**: 내장 제어 CPU가 있다면 firmware 이미지를 로드하고 부팅 응답(mailbox, heartbeat)을 확인한다. 없다면 벤더가 준 가장 작은 command stream(예: DMA only 명령)을 실행해 본다.
4. **드라이버·런타임**: 커널 드라이버(Linux) 또는 bare-metal 드라이버, 사용자 공간 런타임 라이브러리, 인터럽트 연결. 타임아웃과 에러 경로를 먼저 만든다(hang이 나면 register dump를 남기게).
5. **첫 모델 = 단일 conv 하나**: 가장 단순한 int8 conv(작은 채널, stride 1, padding 없음)를 컴파일해서 돌리고, 같은 입력으로 참조 구현(TFLite reference kernel, 벤더의 bit-accurate 모델)과 **bit-exact** 비교(8.3절, C8).
6. **op coverage sweep**: op 종류 × 파라미터 조합(kernel 크기, stride, padding, 채널 수, dilation, activation 종류)을 자동 생성해서 하나씩 돌리고 bit-exact 여부와 NPU/CPU 매핑을 표로 만든다. 경계 조건(채널 1, 매우 큰 feature map, 정렬 안 맞는 크기)을 반드시 넣는다.
7. **실제 모델 end-to-end**: 레이어별 출력을 덤프해서 참조와 비교(C8 4절), 정확도 회귀(C8).
8. **성능**: perf counter로 레이어별 사이클, 활용률, DMA 바이트를 뽑고 컴파일러 추정과 비교한다. 차이가 크면 DRAM 경합, 클럭 설정, 캐시 속성부터 의심한다.
9. **전력·열**: 지속 부하에서 전력, 온도, throttling 시점. 추론당 에너지(D7). 저전력 상태 진입·복귀 시간과 SRAM retention.
10. **안정성**: 수천~수만 번 반복 실행, 전원 상태 전환 중 실행, 다른 마스터(카메라, 오디오 DMA)와 동시 부하. SSD 양산 펌웨어에서 하던 soak test다.

### 8.3 첫 모델을 bit-exact로 — 불일치 패턴으로 원인 찾기 (예제 10)

int8 conv의 참조 구현을 C로 만들고, "NPU 출력"을 흉내 낸 두 가지 버그와 비교한다. (1) requant 반올림을 하지 않고 버린다(truncate). (2) weight layout이 뒤바뀌었다(K와 C 축, 즉 OIHW vs IOHW 같은 실수). 비교 함수는 불일치 개수, 최대 차이, 첫 위치, ±1 차이의 개수를 찍는다.

```c
/* bringup.c — "첫 모델" 검증: int8 conv 참조 구현 vs (가짜) NPU 출력, 불일치 패턴으로 원인 추정 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#define C 8
#define K 8
#define H 12
#define P (H - 2)
static int8_t x[C][H][H], w[K][C][3][3], ref[K][P][P], dut[K][P][P];

static int8_t requant(int32_t acc, int32_t mult, int shift, int round) {
    int64_t v = (int64_t)acc * mult;                       /* Q31 multiplier */
    int64_t r = round ? ((int64_t)1 << (30 + shift)) : 0;  /* round-half-up vs truncate */
    int32_t q = (int32_t)((v + r) >> (31 + shift));
    return (int8_t)(q > 127 ? 127 : q < -128 ? -128 : q);
}
static void conv(int8_t o[K][P][P], int round, int swap_layout) {
    for (int k = 0; k < K; k++) for (int p = 0; p < P; p++) for (int q = 0; q < P; q++) {
        int32_t acc = 0;
        for (int c = 0; c < C; c++) for (int r = 0; r < 3; r++) for (int s = 0; s < 3; s++)
            acc += x[c][p + r][q + s] * (swap_layout ? w[c][k][r][s] : w[k][c][r][s]);  /* 버그: K/C 뒤바뀜 */
        o[k][p][q] = requant(acc, 1518500250 /* ≈0.7071 in Q31 */, 4, round);
    }
}
static void compare(const char *name) {
    int n = 0, maxd = 0, first = -1, hist[3] = {0};
    for (int i = 0; i < K * P * P; i++) {
        int d = ((int8_t *)dut)[i] - ((int8_t *)ref)[i];
        if (d) { n++; if (first < 0) first = i; if (abs(d) > maxd) maxd = abs(d); if (d >= -1 && d <= 1) hist[d + 1]++; }
    }
    printf("%-18s mismatches=%4d/%d  max|diff|=%3d  first=%4d  diffs(-1,+1)=(%d,%d)\n",
           name, n, K * P * P, maxd, first, hist[0], hist[2]);
}
int main(void) {
    srand(7);
    for (int i = 0; i < C * H * H; i++) ((int8_t *)x)[i] = (int8_t)(rand() % 256 - 128);
    for (int i = 0; i < K * C * 9; i++) ((int8_t *)w)[i] = (int8_t)(rand() % 21 - 10);
    conv(ref, 1, 0);
    conv(dut, 1, 0); compare("healthy DUT");
    conv(dut, 0, 0); compare("rounding=truncate");
    conv(dut, 1, 1); compare("weight layout swap");
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 bringup.c -o bringup -lm && ./bringup
```

```text
healthy DUT        mismatches=   0/800  max|diff|=  0  first=  -1  diffs(-1,+1)=(0,0)
rounding=truncate  mismatches= 232/800  max|diff|=  1  first=   8  diffs(-1,+1)=(232,0)
weight layout swap mismatches= 707/800  max|diff|=255  first=   1  diffs(-1,+1)=(3,1)
```

출력에서 볼 것: 불일치의 **모양**이 원인을 말해 준다.

| 증상 | 이 예의 숫자 | 흔한 원인 |
|---|---|---|
| 불일치 0 | healthy | — |
| 최대 차이 1, 전부 한 방향(−1) | truncate: 232/800, 전부 −1 | 반올림 모드(round-half-up vs truncate vs round-to-even), requant shift 규칙 |
| 최대 차이가 크고 거의 전부 틀림 | layout swap: 707/800, 최대 255 | weight/activation layout(NHWC vs NCHW, OHWI vs OIHW), 채널 순서, 스케일 오적용 |
| 테두리에서만 틀림 | 5.6절 | padding, halo, zero-point로 패딩했는지 0으로 했는지 |
| 특정 채널만 틀림 | — | per-channel scale 배열 순서, 채널 패딩 |
| 가끔, 실행마다 다름 | — | 캐시 일관성, DMA 경합, 타이밍 (HW 문제 의심) |

`first=8`처럼 **첫 불일치 위치**를 찍는 것도 중요하다. 인덱스를 (k, p, q)로 풀면 어느 채널·어느 픽셀부터 틀렸는지 알 수 있다. 참고로 이 코드의 `>>`는 음수에 대해 산술 시프트라고 가정한다. C 표준에서는 구현 정의 동작이지만 주요 컴파일러(GCC, Clang, Arm Compiler)는 산술 시프트를 한다. 이런 가정도 참조 구현과 NPU가 달라지는 지점이 될 수 있어서, TFLite 같은 참조 구현은 반올림 규칙을 명시적으로 코드로 정해 둔다(C8 3.4절).

### 8.4 op coverage, 성능 카운터, 전력

- **op coverage 표**: 벤더의 "지원 op 목록"은 op 이름만 있고 파라미터 제약은 각주에 숨어 있는 경우가 많다. 직접 sweep한 표(op × 파라미터 → NPU/CPU, bit-exact, 사이클)가 팀의 자산이 된다. 새 SDK 버전이 나오면 이 표를 다시 돌려 회귀를 잡는다.
- **성능 카운터**: 활용률 = 유효 MAC ÷ (사이클 × MAC/cycle). 레이어별로 이 값을 뽑아 3절의 표와 비교하면 "모양 문제인지, 메모리 문제인지"를 가를 수 있다. DMA 바이트 카운터 ÷ 레이어 시간 = 실효 대역폭(E7).
- **전력**: 추론 구간과 idle 구간을 나눠 재고, 추론당 에너지 = ∫(P − P_idle)dt로 계산한다(D7). NPU 클럭별로 재서 race-to-idle이 유리한지 본다.

### 8.5 벤더 협업 (F8)

- **버전 매트릭스**: 칩 stepping, NPU firmware, 드라이버, 컴파일러, 런타임, 모델 변환기의 버전 조합을 표로 관리한다. "이 조합에서 이 모델이 bit-exact"를 기록한다.
- **재현 케이스를 작게**: 벤더 FAE에게 전체 모델이 아니라 **실패하는 단일 op + 입력 텐서 + 기대 출력 + 버전 정보**를 보낸다. SSD 시절 벤더에게 NAND 이슈를 보낼 때 로그·재현 조건을 정리하던 방식 그대로다.
- **errata와 workaround**: "이 op는 채널 수가 X일 때 틀린 결과" 같은 errata는 컴파일러가 자동 회피하기도 하고, 사용자가 모델을 바꿔야 하기도 한다. 어느 쪽인지 문서로 확인한다.
- **로드맵 질문**: 다음 SDK에서 추가될 op, int4 지원, transformer 연산 지원 계획. 칩 선정 시 중요하다(9절).

---

## 9. NPU 평가하기 — 무엇을 물을 것인가 (M1)

칩을 고를 때 쓰는 질문 목록이다. 각 질문이 이 노트의 어느 절과 연결되는지 함께 적었다.

| 항목 | 질문 | 왜 (절) |
|---|---|---|
| 내 모델 활용률 | 우리 대표 모델 3개를 컴파일해서 레이어별 사이클과 활용률을 보여 줄 수 있나? | 3절: 모양이 활용률을 정한다 |
| peak vs sustained | peak TOPS는 어떤 정밀도·sparsity·클럭 기준인가? 지속 부하 온도에서의 클럭은? | 1.5절, M4 |
| SRAM 크기 | NPU 전용 SRAM이 몇 KB/MB인가? 다른 IP와 공유하나? 우리 모델의 peak activation이 들어가나? | 5.4절, D2 |
| 대역폭 | NPU가 쓸 수 있는 DRAM 실효 대역폭은? 다른 마스터와 경합 시? | 5.3절, D3, E7 |
| op coverage | 우리 모델의 op 전부가 NPU에서 도나? 파라미터 제약은? | 6.2절 |
| 정밀도 | int8 per-channel, int16 activation, fp16, int4 weight 지원? | 2.2절, C1~C3 |
| depthwise·GEMV·attention | 이 세 가지 연산의 효율은? | 3.7절, D4, D5 |
| 툴체인 성숙도 | 컴파일러 릴리스 주기, 공개 문서, 오류 메시지 품질, 프로파일러 | 6절, F8 |
| 벤더 지원 | FAE 대응 시간, errata 공개, 레퍼런스 모델 | 8.5절 |
| 전력 상태 | NPU power gating, SRAM retention, 깨어나는 시간 | D7, E9 |
| 에너지 | 우리 모델 추론당 mJ (실측) | D7 5절 |

실무 팁: 벤더 벤치마크(MobileNet, ResNet)가 아니라 **우리 모델**을 벤더 툴로 컴파일해 보는 것이 가장 빠른 평가다. 컴파일 리포트 하나로 op coverage, SRAM 적합성, 사이클 추정을 한 번에 볼 수 있다. 그다음 개발 보드에서 실측한다(M2).

---

## 10. 임베디드 관점에서 다시 보기

예를 들어 Hark 같은 웨어러블이라면(추정), NPU가 두 층에 있을 수 있다.

| 층 | 예 | 모델 | 병목 |
|---|---|---|---|
| always-on MCU + microNPU | Ethos-U55급, 수백 KB SRAM | wake word, 착용 감지, IMU 제스처 | SRAM 크기, flash 읽기, 전원 on/off 비용 |
| 메인 SoC NPU | Hexagon NPU급, LPDDR | ASR, 작은 LLM, 비전 | DRAM 대역폭(LLM decode), fallback, 열 |

이 노트의 내용이 두 층에서 다르게 드러난다.

- **MCU microNPU**: 모델 전체의 activation을 SRAM에 두는 것이 목표다. weight는 flash에서 DMA로 읽는 경우가 많아 flash 대역폭이 곧 roofline의 메모리 천장이다. 채널 수를 NPU 배열 폭(예: 8·16의 배수)에 맞추고, depthwise가 효율적인지 확인한다(C7).
- **SoC NPU**: LLM decode는 GEMV라 3.7절의 "FC batch 1" 행처럼 배열 활용률이 바닥이고 weight 대역폭이 전부다(D5). 그래서 int4 weight, KV-cache 양자화가 중요하다. CNN·ASR encoder는 GEMM이라 NPU가 빛난다.
- **공통**: fallback 한 개가 전체를 망친다(6.2절). bring-up 첫날부터 컴파일러 리포트와 bit-exact 비교를 자동화한다.

펌웨어 통합에서 보이는 NPU의 모습은 결국 이것이다: **정렬된 버퍼 몇 개, 캐시 관리, DMA가 가능한 메모리 영역, 인터럽트 하나, 그리고 컴파일러가 만든 blob**. 드라이버 입장에서는 SSD의 NVMe queue나 오디오 DMA와 크게 다르지 않다. 달라지는 것은 그 blob 안에서 벌어지는 일(3~5절)을 이해하고, 느릴 때 어디를 봐야 하는지 아는 것이다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| TOPS로 칩을 비교 | 스펙상 2배 칩이 실제로는 비슷하거나 느림 | 활용률·대역폭·fallback 무시 | 우리 모델로 컴파일·실측 (9절) |
| 컴파일러 리포트를 안 읽음 | NPU를 쓰는데 CPU보다 조금 빠를 뿐 | 중간 op가 CPU fallback, 경계 여러 번 | 리포트에서 CPU op 찾기, 교체·수술 (6.2절) |
| 캐시 관리 누락 | 가끔 틀린 결과, 첫 실행만 맞음 | NPU DMA와 CPU 캐시 불일치 | 입력 clean, 출력 invalidate, 또는 non-cacheable 영역 |
| requant 반올림 규칙 불일치 | ±1 차이가 한 방향으로 다수 | truncate vs round, shift 규칙 | 참조 구현을 NPU 규칙에 맞춤 (8.3절, C8) |
| layout 착각 | 거의 전부 틀리고 차이가 큼 | NHWC/NCHW, OHWI/OIHW | 레이어 하나로 bit-exact 확인 후 확장 |
| padding·halo 처리 오류 | 테두리 픽셀만 틀림 | fusion·tiling에서 이미지 밖 값 | 경계 마스크, 레이어·위치별 diff (5.6절) |
| depthwise·작은 채널 무시 | MAC이 적은데 느림 | 배열 활용률 2~40% | 채널 정렬, depthwise 효율 확인, 모델 수정 (3.7절) |
| SRAM 전부를 쓸 수 있다고 가정 | 컴파일은 되는데 예상보다 느림 | 공유 SRAM, 런타임 예약, 타일 축소 | 리포트의 SRAM 사용량, 메모리 모드 확인 |
| 동적 shape 모델 | 컴파일 실패 또는 대부분 CPU | 컴파일 시 shape 필요 | 최대 크기 고정, bucket, 정적 export |
| 버전 기록 안 함 | 어제 되던 것이 오늘 안 됨 | SDK·firmware·컴파일러 변경 | 버전 매트릭스와 회귀 스크립트 (8.5절) |

---

## 12. 면접에서 이렇게 말한다

**Q.** "Explain a systolic array and its utilization problem."

**A.** PE 격자에서 데이터가 매 사이클 이웃으로 한 칸씩 흐르며 MAC을 하는 구조다. weight-stationary라면 각 PE가 weight 하나를 잡고, activation은 옆으로, 부분합은 아래로 흐른다. 값 하나를 SRAM에서 한 번 읽어 여러 PE가 재사용하니 에너지가 싸다. 문제는 skew 때문에 타일마다 fill/drain 2N 사이클이 생기고, K나 N이 배열보다 작으면 행·열이 논다는 것이다. 16×16 사이클 시뮬레이션에서 큰 1×1 conv는 94%였지만 depthwise는 2.8%, batch-1 FC는 2.1%였다.

> "A systolic array is a grid of MAC units where data moves one hop per cycle between neighbors — in a weight-stationary design each PE holds a weight, activations flow right and partial sums flow down, so each value fetched from SRAM is reused across a whole row or column. The catch is utilization: inputs have to be skewed, so every tile pays roughly 2N cycles of fill and drain, plus weight loading, and if the layer's reduction dimension or output channels are smaller than the array, rows and columns just sit idle. In a cycle-level model of a 16×16 array I got about 94% on a large pointwise conv but under 3% on depthwise and batch-1 fully-connected layers."

**Q.** "Weight-stationary vs output-stationary — what's the difference and when does each win?"

**A.** WS는 weight를 PE에 고정하고 input과 부분합을 움직인다. weight 재사용이 최대이고 부분합은 계속 이동한다. OS는 부분합을 PE에서 끝까지 누산하고 weight와 input을 흘려보낸다. 부분합 쓰기가 출력당 한 번뿐이다. 출력 픽셀이 많은 conv 앞단에서는 weight 재사용이 큰 WS가, 누산 길이가 길고 부분합 트래픽이 문제인 경우에는 OS가 유리하다. Eyeriss의 row-stationary는 셋 다 균형 있게 재사용해서 전체 에너지를 줄인다.

> "Weight-stationary pins weights in the PEs and streams inputs and partial sums, so weight reuse is maximal but partial sums keep moving. Output-stationary keeps the accumulator in the PE until the output is finished and streams weights and inputs instead, so each output is written once. Which wins depends on layer shape: early convs with many output pixels reward weight reuse, while long reductions favor keeping partial sums local. Row-stationary from Eyeriss balances all three reuse types to minimize total energy rather than one access type."

**Q.** "How does tiling interact with on-chip SRAM size?"

**A.** 레이어가 SRAM에 다 안 들어가면 타일로 나누는데, 출력 타일이 작을수록 입력과 weight를 DRAM에서 여러 번 다시 읽는다. SRAM이 타일 크기의 상한을 정하고, 타일 크기가 DRAM 트래픽과 arithmetic intensity를 정한다. 256×512×2048 GEMM에서 타일 planner를 돌려 보니 16 KB SRAM이면 최소 트래픽의 6배, 256 KB면 1.6배, 2 MB면 1.08배였다. 그리고 double buffering에 SRAM 두 배가 들고, fusion은 halo 재계산과 SRAM을 맞바꾼다.

> "On-chip SRAM bounds the tile size, and tile size sets how many times inputs and weights get re-fetched from DRAM — which is effectively the layer's arithmetic intensity against DRAM. In a simple planner for a 256×512×2048 int8 GEMM, 16 KB of SRAM meant about 6× the compulsory traffic, 256 KB about 1.6×, and 2 MB about 1.08×. Double buffering to overlap DMA with compute costs you half the space, and layer fusion trades halo recomputation and SRAM for eliminating intermediate DRAM round-trips. So when I evaluate an NPU, SRAM size relative to our peak activations and largest layer matters as much as TOPS."

**Q.** "An op in your model falls back to the CPU. What's the impact and how do you fix it?"

**A.** op 자체보다 경계 비용이 크다. NPU 완료 대기, 캐시 clean/invalidate, 레이아웃 변환, 다시 NPU kick이 경계마다 생긴다. 그래프 중간이면 경계가 두 번이고, 일부 컴파일러는 첫 미지원 op 이후를 전부 CPU로 보낸다. 먼저 컴파일러 리포트로 어떤 op가 왜 떨어졌는지(대개 파라미터 제약) 확인하고, 등가인 지원 op 조합으로 그래프 수술을 하거나, 모델에서 그 op를 바꾸거나, 그래프 끝으로 몰아 경계를 한 번으로 줄인다.

> "The op itself is usually cheap; the partition boundary is what hurts — you wait for the NPU, do cache maintenance, maybe convert layout, run the CPU kernel, then kick the NPU again, and an op in the middle of the graph costs you two boundaries. Some toolchains even run everything after the first unsupported op on the CPU. I'd start from the compiler report to see exactly which op fell back and why — often it's a parameter constraint like stride or channel count — then either rewrite it as an equivalent sequence of supported ops, change the model, or move it to the end of the graph so there's only one boundary."

**Q.** "How would you bring up a new NPU and its SDK?"

**A.** pre-silicon에서 새 IP를 bring-up하던 순서와 같다. 전원·클럭·리셋과 ID 레지스터, 메모리 맵·SMMU·캐시 속성과 DMA 패턴 테스트, firmware나 최소 command stream 실행, 드라이버의 인터럽트·타임아웃·에러 경로. 그다음 단일 int8 conv를 참조 구현과 bit-exact로 맞추고, op × 파라미터 coverage sweep을 자동화하고, 실제 모델을 레이어별로 비교한다. 이후 perf counter로 활용률과 대역폭, 지속 부하 전력·열을 보고, 모든 결과를 SDK·firmware 버전 매트릭스와 함께 기록해 벤더에 작은 재현 케이스로 이슈를 보낸다.

> "I'd treat it like the pre-silicon IP bring-ups I've done on FPGA. First power, clocks, reset and the ID registers; then the memory map, SMMU mappings and cache attributes, with DMA pattern tests; then firmware load or the smallest possible command stream, and a driver with interrupts, timeouts and register dumps on hang. The first real workload is a single int8 conv compared bit-exactly against a reference implementation, then an automated op-by-parameter coverage sweep, then full models compared layer by layer. After that it's performance counters for utilization and bandwidth, sustained power and thermals, and a version matrix of silicon, firmware, driver and compiler, so issues go to the vendor as minimal reproducible cases."

**Q.** "What would you look at when evaluating an NPU for our product?"

**A.** TOPS는 출발점일 뿐이다. 우리 대표 모델을 벤더 컴파일러로 돌려 op coverage와 fallback, SRAM 적합성, 레이어별 활용률을 보고, NPU가 쓸 수 있는 실효 DRAM 대역폭(LLM이면 이게 전부다), 지원 정밀도(int8 per-channel, int16, int4), depthwise·GEMV·attention 효율, 툴체인 성숙도와 벤더 지원, 전력 상태와 추론당 mJ 실측을 본다.

> "TOPS is just the starting point. I'd compile our actual models with the vendor toolchain and look at op coverage and fallbacks, whether our peak activations fit in the NPU's SRAM, and per-layer utilization — especially depthwise, batch-1 matrix-vector and attention. Then effective DRAM bandwidth available to the NPU, which dominates LLM decode, supported precisions like per-channel int8, int16 activations and int4 weights, toolchain maturity and vendor support, and finally measured energy per inference and power-state behavior on a dev board."

---

## 13. 직접 해보기

1. **손계산**: 8×8 weight-stationary 배열에 M = 24, K = 8, N = 8 타일 하나를 넣는다. weight 적재를 빼면 사이클 수와 활용률은? 정답: T = 24 + 16 − 2 = 38, 활용률 = 24·8·8 / (38·64) = 1536 / 2432 ≈ 63.2%.
2. **손계산**: 256 MAC/cycle NPU가 800 MHz로 돈다. peak TOPS는? 어떤 레이어에서 활용률이 25%라면 실효 TOPS는? 정답: 256 × 2 × 8×10⁸ = 0.41 TOPS, 실효 약 0.10 TOPS.
3. **코드**: 예제 3(C 시뮬레이터)에서 psum 갱신 루프를 `for (int i = 0; i < N; i++)`로 바꾸고 돌려 보라. 왜 FAIL이 나는가? 정답: 행 0을 먼저 갱신하면 행 1이 "이번 사이클에 막 계산된" psum을 읽어 한 사이클에 두 칸 내려간다. RTL의 blocking 대입 버그와 같다.
4. **코드**: 예제 5의 WS에 psum RF를 `dict(W=1, I=1, P=8)`과 `dict(W=1, I=1, P=64)`로 주고 다시 세어 보라. psum GLB 접근이 어떻게 변하나? 정답: P = 8이면 전혀 안 변한다(8,960 / 9,216). 가장 안쪽 루프가 출력 64개(8×8)를 훑기 때문에 LRU 8칸은 매번 쫓겨난다. P = 64면 한 출력 채널 전체가 RF에 머물러 psum 읽기 0, 쓰기 256이 된다. 루프 순서와 RF 크기가 **함께** 재사용을 정한다.
5. **코드**: 예제 6의 planner에서 Tk를 K(= 512)로 고정하고, 누산기는 배열 안에 있어 SRAM을 안 쓴다고(4·Tm·Tn 항 제거) 가정해 64 KiB에서 최적 타일을 다시 찾아보라. 정답: Tm = Tn = 32, 16.5 MiB. K를 16씩 쪼개고 누산기를 SRAM에 두는 원래 답(4.88 MiB)보다 3.4배 많다. 입력 타일을 K 전체로 잡으면 SRAM이 A·B 띠에 다 쓰여 출력 타일이 작아지기 때문이다. SRAM을 입력 타일과 누산기에 어떻게 나눌지가 설계 변수다.
6. **bring-up 설계**: 새 NPU의 op coverage sweep 스크립트를 설계하라. conv2d 하나에 대해 어떤 파라미터 축을 몇 개 값으로 sweep하고, 결과 표에 어떤 열을 둘지 적어 보라. 정답: 예) kernel {1, 3, 5, 7}, stride {1, 2}, padding {valid, same}, C_in·C_out {1, 3, 8, 15, 16, 17, 64, 256}, dilation {1, 2}, activation {none, ReLU, ReLU6}, 입력 크기 {1×1, 7×7, 정렬 안 맞는 크기, 최대}; 열: NPU/CPU 매핑, bit-exact 여부, 최대 차이, 사이클, SRAM 사용량, SDK 버전.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| NPU | neural processing unit | 신경망 연산(주로 int8 MAC)에 특화된 고정 기능 가속기 |
| MAC | multiply-accumulate | `acc += a × w`. 2 op로 센다 |
| PE | processing element | MAC + 작은 레지스터. 배열의 한 칸 |
| systolic array | 수축 배열 | 데이터가 매 사이클 이웃 PE로 한 칸씩 흐르는 PE 격자 |
| skew | 비스듬히 넣기 | 행 i의 입력을 i사이클 늦게 넣어 부분합과 타이밍을 맞춤 |
| fill / drain | 채우기 / 비우기 | 배열이 차고 비는 동안 일부 PE만 일하는 구간 (약 2N 사이클) |
| utilization | 활용률 | 유효 MAC ÷ (사이클 × 전체 MAC 수) |
| peak TOPS | 최대 처리량 | MAC/cycle × 2 × 주파수. 활용률 100% 가정 |
| weight-stationary | WS | weight를 PE에 고정, input·psum이 이동 |
| output-stationary | OS | psum을 PE에서 끝까지 누산, weight·input이 이동 |
| input-stationary | IS | input을 PE에 고정 |
| row-stationary | RS | Eyeriss의 방식. 1D 행 conv 단위로 세 가지 재사용을 균형 있게 |
| reuse | 재사용 | 한 번 가져온 값을 여러 MAC에 쓰는 것. 에너지 절약의 핵심 |
| GLB | global buffer | NPU 안의 큰 공용 SRAM |
| scratchpad | 명시적 관리 SRAM | 캐시와 달리 컴파일러·소프트웨어가 주소를 정해 쓰는 SRAM |
| tiling | 타일링 | 텐서를 SRAM에 맞는 조각으로 나눠 처리 |
| compulsory traffic | 최소 트래픽 | 각 텐서를 DRAM에서 딱 한 번씩 옮길 때의 바이트 |
| double buffering | 이중 버퍼 | 한 버퍼로 계산하는 동안 다른 버퍼를 DMA로 채움 |
| layer fusion | 레이어 융합 | 연속 레이어를 타일 단위로 이어 실행해 중간 텐서의 DRAM 왕복 제거 |
| halo | 헤일로 | 이웃 타일과 겹치는 테두리. fusion 시 재읽기·재계산 |
| command stream | 명령 스트림 | 컴파일러가 만든 NPU 실행 명령 목록 (DMA, MAC, vector, 동기화) |
| partition | 분할 | 그래프를 NPU 구간과 CPU 구간으로 나누기 |
| CPU fallback | CPU 대체 실행 | NPU가 못 하는 op를 CPU에서 실행. 경계 비용 발생 |
| requant | 재양자화 | int32 누산값을 정수 곱·shift로 int8 스케일로 되돌림 |
| Vela | Arm의 Ethos-U 컴파일러 | TFLite int8 → NPU 구간을 `ethos-u` custom op로 바꾼 TFLite |
| context binary | QNN의 사전 컴파일 결과 | 기기에서 그래프 준비 시간을 줄이려고 미리 만든 바이너리 |
| FVP | fixed virtual platform | Arm의 소프트웨어 시스템 모델. silicon 전에 소프트웨어 개발 |
| SMMU / IOMMU | 입출력 MMU | NPU·DMA가 보는 주소를 물리 주소로 변환하고 보호 |
| bit-exact | 비트 단위 일치 | 정수 NPU 출력이 참조 구현과 모든 비트가 같음 |

---

## 15. 요약 & 체크리스트

NPU는 명령어 fetch·decode를 없애고, 값을 PE 가까이에 두고 여러 번 쓰는(재사용) 고정 기능 MAC 배열이라서 MAC당 에너지가 CPU보다 두 자릿수 이상 작다. 그 대가로 **모양에 민감하다**. weight-stationary systolic array는 skew 때문에 타일마다 fill/drain이 생기고, K·N이 배열보다 작거나 M이 작으면(depthwise, GEMV, 작은 채널) 활용률이 몇 퍼센트까지 떨어진다. dataflow(WS, OS, IS, RS)는 무엇을 PE에 붙잡아 둘지의 선택이고, on-chip 접근 에너지를 바꾼다. on-chip SRAM 크기는 타일 크기를, 타일 크기는 DRAM 트래픽과 arithmetic intensity를 정하고, double buffering과 layer fusion은 DMA를 숨기거나 없앤다. 컴파일러는 그래프를 partition하고, 양자화 제약을 확인하고, tiling·스케줄링을 거쳐 command stream을 만든다. 미지원 op는 CPU fallback이 되어 경계 비용을 치른다. bring-up은 Don이 FPGA에서 하던 새 IP bring-up과 같은 순서이고(전원 → 메모리 맵 → 최소 명령 → 단일 conv bit-exact → coverage sweep → 성능·전력), 칩 평가는 TOPS가 아니라 **우리 모델의 활용률·SRAM 적합성·대역폭·op coverage·툴체인 성숙도**로 한다.

- [ ] peak TOPS를 MAC/cycle × 2 × 주파수로 계산하고, TOPS가 말하지 않는 세 가지(활용률, 대역폭, 지원 op)를 말할 수 있다
- [ ] CPU scalar·SIMD·NPU의 MAC당 에너지 차이를 명령 오버헤드와 재사용으로 손계산할 수 있다
- [ ] NPU 블록도(MAC array, 버퍼 3종, DMA, command processor, vector unit, SMMU)를 그리고 각 블록의 역할을 설명할 수 있다
- [ ] 2×2 weight-stationary systolic array를 사이클마다 손으로 따라가고, T = M + 2N − 2와 활용률 식을 유도할 수 있다
- [ ] depthwise·GEMV·작은 채널의 활용률이 낮은 이유를 배열 모양으로 설명할 수 있다
- [ ] WS·OS·IS·RS가 각각 무엇을 붙잡아 두고 무엇을 움직이는지, conv 하나의 GLB 접근 횟수를 셀 수 있다
- [ ] GEMM 타일의 DRAM 트래픽 식을 쓰고, SRAM 크기에 따른 트래픽 변화를 설명할 수 있다
- [ ] double buffering의 총 시간을 max(load, compute) × 타일 수 + 양 끝으로 계산할 수 있다
- [ ] layer fusion의 halo 비용과 경계 padding 함정을 설명할 수 있다
- [ ] NPU 컴파일러 단계와 CPU fallback의 비용·해결책, 그리고 NPU bring-up 10단계 체크리스트를 말할 수 있다

## 참고 자료

- H. T. Kung, "Why Systolic Architectures?", IEEE Computer, 1982 — systolic array의 원전
- N. P. Jouppi et al., "In-Datacenter Performance Analysis of a Tensor Processing Unit", ISCA 2017 — TPU v1의 256×256 systolic array, 92 TOPS, unified buffer
- Y.-H. Chen, J. Emer, V. Sze, "Eyeriss: A Spatial Architecture for Energy-Efficient Dataflow for Convolutional Neural Networks", ISCA 2016 — row-stationary, dataflow 분류
- Y.-H. Chen, T. Krishna, J. Emer, V. Sze, "Eyeriss: An Energy-Efficient Reconfigurable Accelerator for Deep Convolutional Neural Networks", IEEE JSSC 2017
- V. Sze, Y.-H. Chen, T.-J. Yang, J. Emer, "Efficient Processing of Deep Neural Networks", Morgan & Claypool, 2020 (및 Proceedings of the IEEE 2017 tutorial) — dataflow·재사용·에너지 모델의 교과서
- M. Horowitz, "Computing's Energy Problem (and what we can do about it)", ISSCC 2014 — 연산·메모리 에너지 (D7)
- A. Parashar et al., "Timeloop: A Systematic Approach to DNN Accelerator Evaluation", ISPASS 2019 — dataflow·tiling 탐색 도구
- Arm Ethos-U 문서와 Vela — [developer.arm.com](https://developer.arm.com) · [pypi.org/project/ethos-u-vela](https://pypi.org/project/ethos-u-vela/)
- Google Coral Edge TPU 문서 (모델 요구사항, 컴파일러) — [coral.ai/docs](https://coral.ai/docs/)
- Qualcomm AI Engine Direct (QNN) SDK 문서 — Qualcomm 개발자 사이트 (버전별 문서 확인)
- TensorFlow Lite Micro — [github.com/tensorflow/tflite-micro](https://github.com/tensorflow/tflite-micro)
- MIT 6.5940 "TinyML and Efficient Deep Learning Computing" (Song Han) — [efficientml.ai](https://efficientml.ai)
- J. Hennessy, D. Patterson, "Computer Architecture: A Quantitative Approach" (6th ed.), 7장 Domain-Specific Architectures — TPU 사례
