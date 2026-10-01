# E4. DSP 아키텍처 — VLIW, MAC, 고정소수점, Xtensa HiFi, Qualcomm Hexagon(HVX/HMX)

> **이 노트를 다 읽으면**: DSP가 일반 CPU와 무엇이 다른지(single-cycle MAC, Harvard 메모리, AGU의 post-increment·circular addressing, zero-overhead loop, 포화·반올림, guard bit 누산기, SIMD, VLIW)를 한 장의 데이터 경로 그림으로 설명할 수 있다 · Q15/Q31 곱셈을 반올림·포화까지 포함해 C로 bit-exact하게 짜고, 누산기 guard bit와 block floating point가 왜 필요한지 숫자로 보여 줄 수 있다 · VLIW 번들을 손으로 스케줄하고 software pipelining의 II(initiation interval)를 ResMII·RecMII로 계산할 수 있다 · Cadence Tensilica(Xtensa·HiFi·Vision)와 Qualcomm Hexagon(scalar·HVX·HMX·VTCM)이 edge ML 파이프라인에서 무엇을 맡는지, NPU·CPU와 어떻게 나누는지 말할 수 있다
> **JD 연결**: "Familiarity with embedded systems, **CPU/DSP/NPU** HW architectures", "Optimizing models for MCUs & edge processors (Cortex-M/A, RISC-V, **DSP**)", "Work with platform vendors to bring up toolchains, SDKs and new accelerator" — study_prep_list **E4**: VLIW, SIMD, MAC 유닛, zero-overhead loop, circular addressing / Q-format 고정소수점 / **Cadence Tensilica HiFi / Vision (Xtensa)**, CEVA / **Qualcomm Hexagon: scalar + HVX(vector) + HMX(matrix)**. Hark 단서 "Hexagon DSP"(P0). 함께 보는 행: **G5**(FIR/IIR, FFT, mel, 고정소수점 DSP 구현, CMSIS-DSP)
> **Don 기준 난이도**: Xtensa 기반 SSD 컨트롤러 펌웨어(NAND 데이터 경로), Q15 연산, DMA·링버퍼, 사이클 단위 성능 튜닝은 이미 몸에 있다 / 같은 Xtensa 가문의 **HiFi·Vision DSP**, VLIW 스케줄링을 "컴파일러 입장"에서 보는 법, Hexagon HVX·HMX·VTCM과 QNN HTP의 역할 분담은 새로 배운다
> **선행 노트**: E1 (CPU 파이프라인·superscalar·OoO — 이 노트의 비교 대상), C1 (Q-format과 requantization, SRDHM), B5 (오디오 front-end, Q15 Hann 창, DMA ping-pong), B2·B3 (1D conv·RNN의 상태 = delay line), D3 (roofline), D7 (연산당 에너지). 같이 쓰이는 노트: E2 (ARM SIMD), E5 (NPU), E8 (이기종 SoC)

---

## 0. 큰 그림 — 이게 왜 필요한가

웨어러블 AI 기기(예를 들어 Hark 같은 기기라면 — 구조는 추정이다)는 마이크 신호를 **항상** 듣는다. 마이크 → 필터 → FFT → mel → 작은 신경망 → (깨어나면) 큰 신경망이라는 줄이 하루 종일 돈다. 이 줄의 앞부분은 "샘플이 끊임없이 흘러 들어오고, 샘플마다 곱하고 더하는" 전형적인 **신호 처리**이고, 뒷부분은 "행렬 곱을 대량으로 하는" **신경망 추론**이다. 전자를 가장 싸게 하도록 수십 년간 다듬어진 프로세서가 **DSP(Digital Signal Processor)** 다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg"> <defs><marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <text x="10" y="20" font-size="13">웨어러블 SoC(가정)에서 음성 파이프라인이 도는 곳</text> <rect x="10" y="40" width="120" height="70" rx="6" fill="#888" fill-opacity="0.18" stroke="#888"/> <text x="70" y="68" font-size="12" text-anchor="middle">마이크 · IMU</text> <text x="70" y="86" font-size="12" text-anchor="middle">PDM / I2S</text> <rect x="170" y="40" width="150" height="110" rx="6" fill="#3f9a6b" fill-opacity="0.18" stroke="#3f9a6b"/>
<text x="245" y="62" font-size="12" text-anchor="middle">always-on island</text> <text x="245" y="82" font-size="12" text-anchor="middle">저전력 DSP 또는 MCU</text> <text x="245" y="102" font-size="12" text-anchor="middle">VAD · 1단 wake word</text> <text x="245" y="122" font-size="12" text-anchor="middle">수 mW 이하 목표</text> <rect x="360" y="40" width="150" height="110" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/> <text x="435" y="62" font-size="12" text-anchor="middle">DSP (Hexagon · HiFi)</text> <text x="435" y="82" font-size="12" text-anchor="middle">FFT · mel · 필터</text> <text x="435" y="102" font-size="12" text-anchor="middle">beamforming · AEC</text>
<text x="435" y="122" font-size="12" text-anchor="middle">작은 NN (HVX 등)</text> <rect x="545" y="40" width="125" height="110" rx="6" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/> <text x="607" y="62" font-size="12" text-anchor="middle">NPU</text> <text x="607" y="82" font-size="12" text-anchor="middle">큰 NN: ASR,</text> <text x="607" y="102" font-size="12" text-anchor="middle">vision, SLM</text> <text x="607" y="122" font-size="12" text-anchor="middle">int8/int4 GEMM</text> <rect x="170" y="190" width="340" height="60" rx="6" fill="#888" fill-opacity="0.18" stroke="#888"/> <text x="340" y="214" font-size="12" text-anchor="middle">CPU (Cortex-A / Kryo 등)</text>
<text x="340" y="234" font-size="12" text-anchor="middle">제어 · 스케줄 · 후처리 · fallback op</text> <rect x="545" y="190" width="125" height="60" rx="6" fill="#888" fill-opacity="0.1" stroke="#888" stroke-dasharray="5 4"/> <text x="607" y="214" font-size="12" text-anchor="middle">공유 메모리</text> <text x="607" y="234" font-size="12" text-anchor="middle">SRAM · TCM · DDR</text> <line x1="130" y1="75" x2="168" y2="75" stroke="currentColor" marker-end="url(#ar)"/> <line x1="320" y1="95" x2="358" y2="95" stroke="currentColor" marker-end="url(#ar)"/> <line x1="510" y1="95" x2="543" y2="95" stroke="currentColor" marker-end="url(#ar)"/>
<line x1="435" y1="150" x2="435" y2="188" stroke="currentColor" marker-end="url(#ar)"/> <line x1="245" y1="150" x2="245" y2="188" stroke="currentColor" marker-end="url(#ar)"/> <line x1="607" y1="150" x2="607" y2="188" stroke="currentColor" marker-end="url(#ar)"/> <text x="10" y="280" font-size="12">같은 데이터(오디오 프레임)가 전력이 낮은 블록에서 높은 블록으로 올라간다. 이 노트는 가운데 파란 블록(DSP)을 판다.</text> <text x="10" y="302" font-size="12">화살표 = 데이터 흐름. 실제 블록 구성은 칩마다 다르며 Hark 기기 구조는 추정이다.</text> </svg>
```

그림 1 — 웨어러블 SoC(가정)의 블록과 음성 파이프라인. DSP는 always-on 블록과 NPU 사이에서 "신호 처리 + 작은 NN"을 맡는 경우가 많다. Qualcomm SoC라면 가운데 블록이 Hexagon, 많은 다른 SoC에서는 Cadence Tensilica HiFi인 경우가 흔하다.

Don에게 좋은 소식이 있다. **Cadence Tensilica의 HiFi 오디오 DSP와 Vision DSP는 전부 Xtensa 코어 위에 만든 것**이다. Don이 SK hynix에서 SSD 컨트롤러의 NAND 데이터 경로 펌웨어를 짤 때 쓴 Xtensa는 "설정 가능한(configurable) 프로세서"이고, 같은 코어에 오디오용 SIMD·MAC·포화 연산 명령을 잔뜩 붙인 제품이 HiFi다. 툴체인(Xtensa Xplorer, `xt-xcc`/`xt-clang` 계열 컴파일러, ISS 시뮬레이터), zero-overhead loop 명령, windowed register ABI 같은 것들이 그대로 이어진다. 그래서 면접에서 "DSP 경험 있나?"라는 질문에 Don은 "Xtensa 위에서 사이클 단위로 데이터 경로를 최적화해 봤다"로 시작할 수 있다.

이 노트의 순서는 다음과 같다.

1. DSP를 DSP로 만드는 8가지 — CPU(E1)와 비교
2. 고정소수점 깊이 파기 — Q15/Q31 곱셈, 포화, guard bit, FIR SNR, block floating point
3. 원형 버퍼와 modulo addressing — 하드웨어가 공짜로 해 주는 이유
4. VLIW와 스케줄링 — 번들 채우기, software pipelining, II
5. Cadence Tensilica — Xtensa, TIE, HiFi, Vision
6. Qualcomm Hexagon — scalar, HVX, HMX, VTCM, QNN HTP
7. CEVA와 그 밖의 DSP
8. 프로그래밍 모델 — intrinsic, 자동 벡터화의 한계, 벤더 라이브러리, TCM·DMA
9. DSP vs NPU vs CPU — ML 파이프라인의 어느 층을 어디에

---

## 1. DSP를 DSP로 만드는 것 — CPU와 무엇이 다른가

### 1.1 직관: FIR 한 줄이 무엇을 요구하나

DSP의 모든 특징은 다음 한 줄에서 나온다.

```
y[n] = Σ_{k=0}^{T−1} h[k] · x[n−k]
```

말로 하면: 최근 T개의 입력 샘플에 계수를 하나씩 곱해서 모두 더한다. 이것이 FIR 필터이고, 1D conv(B2)의 한 출력 채널이고, dense 층(B1)의 한 출력이다. 이 합의 한 항을 처리하려면 매번 다음 일곱 가지가 필요하다.

1. 다음 명령을 가져온다 (instruction fetch)
2. 메모리에서 `x[n−k]`를 읽는다
3. 메모리에서 `h[k]`를 읽는다
4. `x` 포인터를 한 칸 옮긴다 (그리고 버퍼 끝이면 처음으로 되감는다)
5. `h` 포인터를 한 칸 옮긴다
6. 곱하고 누산기에 더한다
7. 루프 카운터를 줄이고, 0이 아니면 분기한다

일반 CPU(E1의 in-order RISC)라면 이 일곱 가지가 대부분 **별도의 명령**이다. DSP는 이것을 **한 cycle에 전부 병렬로** 하도록 만든 기계다. T = 32 탭이면 CPU는 대략 32 × 5~7 명령, DSP는 32 cycle(+ 약간)이다.

### 1.2 여덟 가지 특징

| 특징 | 무엇 | 1.1의 어느 단계를 없애나 | Don 경험 연결 |
|---|---|---|---|
| Single-cycle MAC | 곱셈+누산을 한 명령, 매 cycle 1개 이상 발행 | 6 | Cortex-M4 `SMLAD` (E2) |
| Harvard / 다중 메모리 포트 | 명령 버스와 데이터 버스 분리, 데이터 메모리 bank 2개 이상 | 1·2·3을 동시에 | TCM의 I-TCM/D-TCM 분리 |
| AGU + post-increment | 주소 계산 전용 유닛, load 하면서 포인터 갱신 | 4·5 | NAND 버퍼 descriptor 포인터 전진 |
| Circular (modulo) addressing | 버퍼 끝에 닿으면 하드웨어가 시작 주소로 되감음 | 4의 wrap | SSD 링버퍼, NVMe SQ/CQ |
| Zero-overhead loop | 루프 시작·끝·횟수를 레지스터에 두고 분기 없이 반복 | 7 | Xtensa `LOOP` 명령 |
| 포화·반올림 산술 | 넘치면 최대값에 고정, 반올림 모드 내장 | 오류 처리 분기 | `__SSAT`, `QADD` |
| Guard bit 누산기 | 누산기를 곱 폭보다 넓게 (예: 32+8 = 40비트) | 중간 오버플로 검사 | 누산 폭 설계 |
| SIMD + VLIW | 한 명령으로 여러 데이터, 한 번들에 여러 명령 | 여러 항을 한 cycle에 | Xtensa FLIX 번들 |

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"> <defs><marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <text x="10" y="20" font-size="13">전형적인 DSP 데이터 경로 (개념도)</text> <rect x="10" y="40" width="130" height="50" rx="6" fill="#888" fill-opacity="0.18" stroke="#888"/> <text x="75" y="62" font-size="12" text-anchor="middle">Program 메모리</text> <text x="75" y="80" font-size="12" text-anchor="middle">(명령 전용 버스)</text> <rect x="10" y="120" width="130" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/>
<text x="75" y="142" font-size="12" text-anchor="middle">X 데이터 메모리</text> <text x="75" y="160" font-size="12" text-anchor="middle">예: 입력 x[n]</text> <rect x="10" y="200" width="130" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/> <text x="75" y="222" font-size="12" text-anchor="middle">Y 데이터 메모리</text> <text x="75" y="240" font-size="12" text-anchor="middle">예: 계수 h[k]</text> <rect x="180" y="120" width="110" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.18" stroke="#3f9a6b"/> <text x="235" y="142" font-size="12" text-anchor="middle">AGU 0</text> <text x="235" y="160" font-size="12" text-anchor="middle">p++ · 모듈로</text>
<rect x="180" y="200" width="110" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.18" stroke="#3f9a6b"/> <text x="235" y="222" font-size="12" text-anchor="middle">AGU 1</text> <text x="235" y="240" font-size="12" text-anchor="middle">p++ · 비트역순</text> <rect x="180" y="40" width="110" height="50" rx="6" fill="#888" fill-opacity="0.18" stroke="#888"/> <text x="235" y="62" font-size="12" text-anchor="middle">loop 제어기</text> <text x="235" y="80" font-size="12" text-anchor="middle">LBEG·LEND·LCOUNT</text> <line x1="140" y1="65" x2="178" y2="65" stroke="currentColor" marker-end="url(#ar)"/> <line x1="140" y1="145" x2="178" y2="145" stroke="currentColor" marker-end="url(#ar)"/>
<line x1="140" y1="225" x2="178" y2="225" stroke="currentColor" marker-end="url(#ar)"/> <rect x="330" y="160" width="80" height="50" rx="6" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/> <text x="370" y="190" font-size="12" text-anchor="middle">× 곱셈기</text> <line x1="290" y1="145" x2="328" y2="175" stroke="currentColor" marker-end="url(#ar)"/> <line x1="290" y1="225" x2="328" y2="195" stroke="currentColor" marker-end="url(#ar)"/> <rect x="440" y="160" width="70" height="50" rx="6" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/> <text x="475" y="190" font-size="12" text-anchor="middle">+ 가산</text>
<line x1="410" y1="185" x2="438" y2="185" stroke="currentColor" marker-end="url(#ar)"/> <rect x="540" y="150" width="130" height="70" rx="6" fill="#d0564a" fill-opacity="0.18" stroke="#d0564a"/> <text x="605" y="175" font-size="12" text-anchor="middle">누산기 40-bit</text> <text x="605" y="193" font-size="12" text-anchor="middle">8 guard + 32</text> <text x="605" y="211" font-size="12" text-anchor="middle">acc0 · acc1</text> <line x1="510" y1="185" x2="538" y2="185" stroke="currentColor" marker-end="url(#ar)"/> <path d="M605,220 L605,250 L475,250 L475,212" fill="none" stroke="currentColor" marker-end="url(#ar)"/>
<text x="490" y="266" font-size="12">되먹임 (acc += x·h)</text> <rect x="540" y="40" width="130" height="60" rx="6" fill="#d0564a" fill-opacity="0.1" stroke="#d0564a"/> <text x="605" y="64" font-size="12" text-anchor="middle">round · saturate</text> <text x="605" y="82" font-size="12" text-anchor="middle">→ Q15 저장</text> <line x1="605" y1="150" x2="605" y2="102" stroke="currentColor" marker-end="url(#ar)"/> <text x="10" y="292" font-size="12">한 cycle에: 명령 fetch + X load + Y load + 포인터 2개 갱신 + MAC 1회 + loop 카운트 → 모두 병렬.</text> <text x="10" y="314" font-size="12">일반 CPU라면 이 7가지가 대부분 별도 명령이다 (5절 시뮬레이션의 A 경우).</text>
</svg>
```

그림 2 — 전형적인 DSP 데이터 경로. 명령 메모리와 두 개의 데이터 메모리가 따로 있어서 한 cycle에 명령 1개와 데이터 2개를 동시에 읽는다. AGU(Address Generation Unit, 주소 생성 전용 가산기)가 포인터를 갱신하고, 곱셈기와 가산기가 넓은 누산기에 되먹임한다. loop 제어기의 레지스터 이름(LBEG·LEND·LCOUNT)은 Xtensa의 zero-overhead loop 옵션에서 가져온 것이다.

### 1.3 하나씩 짧게

**Single-cycle MAC.** MAC(multiply-accumulate) = `acc += a · b`. DSP는 곱셈기를 파이프라인화해서 **매 cycle 새 MAC을 하나씩(SIMD면 여러 개) 시작**할 수 있다. 결과가 나오기까지 지연(latency)은 2~3 cycle일 수 있지만, 처리량(throughput)이 1/cycle이라는 게 핵심이다. 이 구분이 4절 스케줄링의 출발점이다.

**Harvard 구조와 다중 메모리 포트.** 폰 노이만 구조(명령과 데이터가 같은 버스)에서는 "명령 fetch + 데이터 2개 load"를 한 cycle에 못 한다. DSP는 명령 메모리와 데이터 메모리를 분리하고, 데이터 메모리도 bank를 2개 이상 두어 `x`와 `h`를 동시에 읽는다. 옛날 DSP는 X/Y 메모리라고 불렀다. 현대 DSP는 캐시와 TCM(tightly-coupled memory, 캐시 없이 고정 지연으로 붙은 SRAM)을 섞는다.

**AGU와 post-increment.** `x = *p++;`를 한 명령에 하는 것. load가 끝나는 동시에 AGU가 `p`를 stride만큼 전진시킨다. ALU가 포인터 덧셈에 한 cycle도 쓰지 않는다. ARM도 post-index 주소 모드(`LDR r0, [r1], #4`)가 있으니 Don에게 새로운 개념은 아니다. DSP의 차이는 AGU가 **두 개 이상**이고, 3절의 circular와 FFT용 **bit-reversed** 주소 모드까지 지원한다는 것이다.

**Zero-overhead loop.** 루프 본문의 시작 주소, 끝 주소, 반복 횟수를 특수 레지스터에 넣어 두면, PC가 끝 주소에 닿을 때 하드웨어가 횟수를 줄이고 시작 주소로 점프한다. 분기 명령·비교·카운터 감소가 없고, 분기 예측 실패도 없다. Xtensa의 `LOOP`/`LOOPNEZ`/`LOOPGTZ` 명령과 `LBEG`/`LEND`/`LCOUNT` 레지스터가 정확히 이것이다 (Xtensa 구성에서 loop 옵션이 켜져 있을 때). Don이 SSD 펌웨어 디스어셈블리에서 `loop a3, .Lend` 같은 줄을 봤다면 바로 그것이다.

```
; Xtensa zero-overhead loop 모양 (개념 예시)
        movi    a3, 32          ; 반복 횟수
        loop    a3, .Lend       ; LBEG = 다음 명령, LEND = .Lend, LCOUNT = 31
        ...                     ; 본문: load, MAC, ... (분기 명령 없음)
.Lend:                          ; PC가 여기 오면 하드웨어가 LCOUNT를 보고 LBEG로
```

**포화(saturation)와 반올림.** 정수 덧셈이 넘치면 CPU는 wrap-around(+최대 → −최대)한다. 오디오에서 wrap은 "딸깍" 하는 큰 잡음이지만 포화(최대값에 고정)는 살짝 찌그러진 소리일 뿐이다. 그래서 DSP 명령은 기본이 포화이고, 곱셈 뒤 시프트에 반올림 모드가 붙어 있다. 2절에서 C로 확인한다.

**Guard bit 누산기.** 16×16 곱은 32비트다. 이걸 수백 개 더하면 32비트를 넘는다. 누산기를 40비트(8 guard bit)처럼 넓게 두면 중간 합이 넘쳐도 최종 결과가 범위 안이면 정답이 나온다. TI C54x 계열과 ADI Blackfin의 40비트 누산기가 대표적인 예다. 2.3절에서 숫자로 본다.

**SIMD.** 한 명령으로 레인(lane) 여러 개를 동시에 처리한다. 64비트 레지스터에 int16 4개, 1024비트 HVX 레지스터에 int8 128개. ML에서는 이게 곧 처리량이다. ARM 쪽 SIMD(NEON, Helium)는 E2에서 다룬다.

**VLIW.** Very Long Instruction Word. 한 "명령어"가 사실은 서로 독립인 명령 여러 개의 묶음(bundle, packet)이고, 하드웨어는 묶음 안의 명령을 **검사 없이 동시에** 발행한다. 무엇을 한 묶음에 넣을지는 **컴파일러(또는 어셈블리 프로그래머)가** 정한다. 4절에서 직접 스케줄러를 짠다.

### 1.4 CPU(E1)와 비교 표

| 항목 | 일반 CPU (Cortex-A, E1) | DSP (HiFi, Hexagon 등) | 이유 |
|---|---|---|---|
| 병렬성 찾는 주체 | 하드웨어 (superscalar, OoO) | 컴파일러 (VLIW 번들) | OoO 로직은 면적·전력이 크다 |
| 실행 시간 예측성 | 캐시·분기 예측 때문에 변동 | 결정적(deterministic)에 가깝다 | 실시간 오디오는 최악 시간이 중요 |
| 메모리 | 캐시 중심, 가상 메모리 | TCM/scratchpad + DMA 중심 | 데이터 이동을 소프트웨어가 계획 |
| 루프 | 분기 + 예측 | zero-overhead loop | 짧은 루프에서 오버헤드 0 |
| 주소 계산 | 범용 ALU | 전용 AGU, circular, bit-reversed | FIR·FFT 패턴 |
| 산술 | wrap-around, float 중심 | 포화·반올림·고정소수점, guard bit | 오디오·통신 신호 품질 |
| 전력 효율 | 제어 코드에 강함 | 규칙적 수치 루프에 강함 | 명령당 제어 오버헤드 차이 |
| 약점 | 수치 루프당 에너지 | 분기 많은 제어 코드, OS | VLIW에서 분기는 번들을 비운다 |

말로 하면: CPU는 "어떤 코드가 와도 빠르게" 만들려고 하드웨어에 똑똑함을 넣었고, DSP는 "규칙적인 수치 루프를 최소 전력으로" 돌리려고 똑똑함을 **컴파일러로 옮기고** 하드웨어는 단순하고 넓게 만들었다.

### 1.5 흔한 함정

- "DSP = 신호 처리 알고리즘"과 "DSP = 프로세서"를 섞어 쓴다. 면접에서는 "DSP core" 또는 "DSP processor"라고 명확히 말한다. G5가 알고리즘, E4가 프로세서다.
- DSP도 제어 코드를 돌릴 수 있다. 다만 분기가 많으면 VLIW 번들이 대부분 NOP이 되어 효율이 급감한다. 그래서 제어·스케줄은 CPU/MCU, 수치 루프는 DSP로 나눈다.

---

## 2. 고정소수점 깊이 파기 — Q15, Q31, 포화, guard bit

C1 1.4절에서 "Q-format은 scale이 2의 거듭제곱으로 고정된 양자화"라고 봤다. 여기서는 DSP 명령이 실제로 하는 비트 연산을 C로 재현한다.

### 2.1 정의: Qm.n

- **Q15**: int16을 `q / 2¹⁵`로 해석. 범위 [−1, 1 − 2⁻¹⁵] = [−1, 0.999969…], 간격(LSB) 2⁻¹⁵ ≈ 3.05 × 10⁻⁵.
- **Q31**: int32를 `q / 2³¹`로 해석. 범위 [−1, 1 − 2⁻³¹], LSB ≈ 4.66 × 10⁻¹⁰.
- 일반형 **Qm.n**: 정수부 m비트, 소수부 n비트(부호 별도). Q15 = Q0.15, 40비트 누산기에 Q30 곱을 쌓으면 Q9.30.

손계산: 0.5를 Q15로 → 0.5 × 32768 = 16384. −0.25 → −8192. 1.0은? 32768은 int16 범위(최대 32767)를 넘으므로 **표현 불가**, 가장 가까운 것은 32767 = 0.99997. 이 비대칭(−1은 되는데 +1은 안 됨)이 뒤의 모든 함정의 근원이다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg"> <text x="10" y="20" font-size="13">Q15 · 곱셈 결과 Q30 · 40-bit 누산기의 비트 배치 (칸 1개 = 1 bit)</text> <text x="10" y="54" font-size="12">Q15 (16b)</text> <rect x="436" y="40" width="13" height="20" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/> <rect x="450" y="40" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="464" y="40" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="478" y="40" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="492" y="40" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="506" y="40" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="520" y="40" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="534" y="40" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="548" y="40" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="562" y="40" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="576" y="40" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="590" y="40" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="604" y="40" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="618" y="40" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="632" y="40" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="646" y="40" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <text x="100" y="78" font-size="12">부호 1 + 소수 15 · 범위 [−1, 1−2⁻¹⁵]</text> <text x="10" y="114" font-size="12">Q30 (32b)</text> <rect x="212" y="100" width="13" height="20" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<rect x="226" y="100" width="13" height="20" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/> <rect x="240" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="254" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="268" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="282" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="296" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="310" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="324" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="338" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="352" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="366" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="380" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="394" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="408" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="422" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="436" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="450" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="464" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="478" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="492" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="506" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="520" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="534" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="548" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="562" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="576" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="590" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="604" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="618" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="632" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="646" y="100" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <text x="212" y="138" font-size="12">곱 = 부호 2개(중복) + 소수 30 · 범위 [−2, 2)</text> <text x="10" y="174" font-size="12">acc 40b</text> <rect x="100" y="160" width="13" height="20" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/> <rect x="114" y="160" width="13" height="20" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<rect x="128" y="160" width="13" height="20" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/> <rect x="142" y="160" width="13" height="20" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/> <rect x="156" y="160" width="13" height="20" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/> <rect x="170" y="160" width="13" height="20" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/> <rect x="184" y="160" width="13" height="20" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/> <rect x="198" y="160" width="13" height="20" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/> <rect x="212" y="160" width="13" height="20" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<rect x="226" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="240" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="254" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="268" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="282" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="296" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="310" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="324" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="338" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="352" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="366" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="380" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="394" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="408" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="422" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="436" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="450" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="464" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="478" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="492" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="506" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="520" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="534" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="548" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="562" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="576" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="590" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="604" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="618" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="632" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="646" y="160" width="13" height="20" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <text x="100" y="198" font-size="12">guard 8 bit(초록) + 부호 + 소수 30 = Q9.30 · 범위 [−512, 512)</text> <text x="10" y="230" font-size="12">guard bit가 있으면 최악의 곱 (−1)·(−1) = 1.0을 511번까지 더해도 넘치지 않는다 (2절 예제).</text> </svg>
```

그림 3 — Q15, Q15 × Q15 곱(Q30), 40비트 누산기의 비트 배치. 곱은 부호 비트가 2개(중복)이고, 누산기 위쪽 8비트가 guard bit다.

### 2.2 Q15 곱셈 = 곱 → 반올림 → 시프트 → 포화

```
a, b ∈ Q15        p = a · b            (int32, Q30)
                  p' = p + 2¹⁴         (반올림: 결과 LSB의 절반을 더함)
                  r = sat16(p' >> 15)  (Q30 → Q15, 범위 밖이면 고정)
```

말로 하면: 두 Q15를 곱하면 소수부가 30비트가 되니 15비트 오른쪽으로 밀어 Q15로 되돌린다. 밀기 전에 버려질 부분의 절반(2¹⁴)을 더하면 내림이 아니라 반올림이 된다. 마지막으로 결과가 int16을 넘으면 최대/최소로 고정한다.

손계산 세 개:

- 0.5 × 0.5: 16384 × 16384 = 268435456 = 2²⁸. (2²⁸ + 2¹⁴) >> 15 = 8192 + 0 = 8192 = 0.25. 정확.
- 3 × 16384 (≈ 0.0000916 × 0.5): 49152. 49152 / 32768 = 1.5 LSB. 반올림 → 2, 단순 시프트(내림) → 1.
- (−1) × (−1): (−32768)² = 2³⁰ = 1073741824. >> 15 = 32768 → int16에 안 들어간다. 포화 → 32767. 포화 없이 int16으로 자르면 **−32768 = −1.0**, 즉 "음수 × 음수 = 음수"가 된다.

```c
#include <stdio.h>
#include <stdint.h>

static int16_t sat16(int32_t x) {
    if (x > INT16_MAX) return INT16_MAX;
    if (x < INT16_MIN) return INT16_MIN;
    return (int16_t)x;
}
/* Q15 x Q15 -> Q15, round-half-up, saturate */
static int16_t q15_mul(int16_t a, int16_t b) {
    int32_t p = (int32_t)a * b;          /* Q30 */
    p += 1 << 14;                         /* +0.5 LSB of result */
    return sat16(p >> 15);                /* Q30 -> Q15 */
}
/* truncating, no saturation: what naive C does */
static int16_t q15_mul_naive(int16_t a, int16_t b) {
    return (int16_t)(((int32_t)a * b) >> 15);
}
static double f(int16_t q) { return q / 32768.0; }

int main(void) {
    int16_t t[][2] = {{16384, 16384}, {-16384, 16384}, {3, 16384},
                      {-32768, -32768}, {32767, 32767}, {-3, 16384}};
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        int16_t a = t[i][0], b = t[i][1];
        int16_t r = q15_mul(a, b), n = q15_mul_naive(a, b);
        printf("%7.4f x %7.4f = exact %8.5f | q15_mul %6d (%8.5f) | naive %6d (%8.5f)\n",
               f(a), f(b), f(a) * f(b), r, f(r), n, f(n));
    }
    return 0;
}
```

```text
 0.5000 x  0.5000 = exact  0.25000 | q15_mul   8192 ( 0.25000) | naive   8192 ( 0.25000)
-0.5000 x  0.5000 = exact -0.25000 | q15_mul  -8192 (-0.25000) | naive  -8192 (-0.25000)
 0.0001 x  0.5000 = exact  0.00005 | q15_mul      2 ( 0.00006) | naive      1 ( 0.00003)
-1.0000 x -1.0000 = exact  1.00000 | q15_mul  32767 ( 0.99997) | naive -32768 (-1.00000)
 1.0000 x  1.0000 = exact  0.99994 | q15_mul  32766 ( 0.99994) | naive  32766 ( 0.99994)
-0.0001 x  0.5000 = exact -0.00005 | q15_mul     -1 (-0.00003) | naive     -2 (-0.00006)
```

출력에서 볼 것:

- 넷째 줄: `(−1)·(−1)`을 naive로 하면 **−1.0**이다. 부호가 뒤집힌 오류라서 오디오에선 큰 "펑", NN에선 activation 하나가 최대 음수로 튄다. `q15_mul`은 0.99997로 포화한다. ARM `QDMULH`/`SQRDMULH`, Hexagon·HiFi의 포화 곱 명령이 하드웨어로 하는 일이 이것이다. 참고로 C에서 범위 밖 값을 `int16_t`로 변환하는 결과는 implementation-defined다(보통 wrap).
- 셋째·여섯째 줄: 1.5 LSB는 반올림 2, 내림 1. −1.5 LSB는 round-half-up −1, 내림(산술 시프트) −2. 단순 `>> 15`는 항상 **−∞ 방향으로 내리기** 때문에 평균 −0.5 LSB의 **bias**가 생긴다. 한 번은 작지만 수천 번 누적하면 DC 오프셋이 된다(2.4절에서 숫자로 본다).
- 다섯째 줄: 0.99997² 도 1이 아니라 0.99994다. "1.0을 곱하면 원래 값"이 Q15에선 성립하지 않는다(값이 1 LSB씩 줄어든다). 그래서 이득 1.0 경로는 곱셈을 건너뛰는 코드가 흔하다.

> 반올림 방식은 여러 가지다(round-half-up, round-half-away-from-zero, round-half-even). C1 7.4절의 SRDHM(gemmlowp)은 "doubling high multiply + 반올림"으로 Q31 곱을 한다. 벤더 명령마다 반올림 규칙이 조금씩 달라서 **bit-exact 검증 시 레퍼런스 모델도 같은 규칙**을 써야 한다.

### 2.3 누산기와 guard bit — 1000개 곱을 더하면

FIR·dot product는 곱을 수백~수천 개 더한다. 각 곱은 Q30(int32)이다. int32의 Q30 범위는 [−2, 2)다. 즉 **곱 두 개만 더해도 넘칠 수 있다.**

손계산: 0.5 × 0.5 = 0.25를 1000번 더하면 250이다.

```
곱 하나 (Q30)      = 0.25 · 2³⁰ = 2²⁸ = 268 435 456
1000개 합          = 268 435 456 000 ≈ 2.68 × 10¹¹
int32 최대         = 2³¹ − 1 ≈ 2.15 × 10⁹          → 125배 초과
40비트 최대        = 2³⁹ − 1 ≈ 5.50 × 10¹¹         → 들어간다
wrap 결과          = 268 435 456 000 mod 2³² = 2³¹ → int32로 −2³¹ = Q30에서 −2.0
```

말로 하면: 합 250은 Q30으로 2.68 × 10¹¹이라 32비트에는 안 들어가지만 40비트에는 들어간다. 40비트 누산기의 Q30 범위는 [−512, 512)이다(그림 3의 Q9.30). 최악의 곱 (−1)·(−1) = 1.0을 **511번**까지 더해도 안전하다.

```c
#include <stdio.h>
#include <stdint.h>

#define ACC40_MAX (((int64_t)1 << 39) - 1)
#define ACC40_MIN (-((int64_t)1 << 39))
static int64_t sat40(int64_t x) {           /* 40-bit accumulator w/ saturation */
    if (x > ACC40_MAX) return ACC40_MAX;
    if (x < ACC40_MIN) return ACC40_MIN;
    return x;
}
static int32_t sat_add32(int32_t a, int32_t b) {
    int64_t s = (int64_t)a + b;
    return s > INT32_MAX ? INT32_MAX : s < INT32_MIN ? INT32_MIN : (int32_t)s;
}
/* Q31 x Q31 MAC into a Q62 int64 accumulator, saturating */
static int64_t q31_mac(int64_t acc, int32_t a, int32_t b) {
    int64_t p = (int64_t)a * b;             /* Q62, |p| <= 2^62 */
    if (p > 0 && acc > INT64_MAX - p) return INT64_MAX;
    if (p < 0 && acc < INT64_MIN - p) return INT64_MIN;
    return acc + p;
}

int main(void) {
    const int N = 1000;
    int16_t x = 16384, h = 16384;            /* 0.5 * 0.5 = 0.25 each */
    uint32_t wrap = 0; int32_t sat32 = 0; int64_t a40 = 0, a64 = 0;
    for (int i = 0; i < N; i++) {
        int32_t p = (int32_t)x * h;          /* Q30 = 2^28 */
        wrap  += (uint32_t)p;                /* int32 wrap-around (defined via uint32) */
        sat32  = sat_add32(sat32, p);
        a40    = sat40(a40 + p);
        a64   += p;
    }
    const double q30 = 1073741824.0;
    printf("exact sum          = %.4f\n", N * 0.25);
    printf("int32 wrap         = %.4f\n", (int32_t)wrap / q30);
    printf("int32 saturate     = %.4f\n", sat32 / q30);
    printf("40-bit acc         = %.4f\n", a40 / q30);
    printf("int64 acc          = %.4f\n", a64 / q30);
    printf("worst case (-1*-1) products a 40-bit Q30 acc holds: %lld\n",
           (long long)(ACC40_MAX / ((int64_t)1 << 30)));
    int64_t acc = 0;                         /* Q31 MAC demo */
    acc = q31_mac(acc, INT32_MIN, INT32_MIN); /* (-1)*(-1) = 1.0 */
    acc = q31_mac(acc, 1 << 30, 1 << 30);     /* 0.5*0.5 = 0.25 */
    printf("q31_mac: %.6f (Q62 -> real)\n", (double)acc / 4611686018427387904.0);
    return 0;
}
```

```text
exact sum          = 250.0000
int32 wrap         = -2.0000
int32 saturate     = 2.0000
40-bit acc         = 250.0000
int64 acc          = 250.0000
worst case (-1*-1) products a 40-bit Q30 acc holds: 511
q31_mac: 1.250000 (Q62 -> real)
```

출력에서 볼 것:

- int32 wrap은 **−2.0** — 250이 부호까지 뒤집힌 쓰레기가 된다. 포화 int32는 약 2.0(2³¹−1을 2³⁰으로 나눈 1.99999…가 반올림되어 찍힘)에 붙어서 틀리긴 마찬가지다. **중간 합**에 포화를 걸면 답이 틀린다는 게 핵심이다 — 포화는 **마지막에 한 번** 걸어야 한다.
- 40비트와 64비트 누산기는 250을 정확히 낸다. 40비트는 최악 곱을 511개까지 담는다. 탭이 더 많으면 64비트 누산이나 중간 스케일링이 필요하다.
- `q31_mac`: Q31 × Q31 = Q62를 int64에 쌓으면 범위가 [−2, 2)라서 guard bit가 1개뿐이다. (−1)·(−1) + 0.25 = 1.25는 들어가지만 곱 2개만 크면 넘친다. Q31 누산을 길게 할 때 더 넓은 누산기나 블록마다 시프트를 쓰는 이유다.

C로 흉내 낼 때 주의: C에서 **signed 오버플로는 undefined behavior**다. 그래서 위 코드는 wrap을 `uint32_t`로 흉내 냈다. 하드웨어는 wrap이 정의된 동작이지만, C 컴파일러는 "넘치지 않는다"고 가정하고 최적화해 버릴 수 있다. 펌웨어에서 wrap을 의도한다면 unsigned로 계산해야 한다.

Don 경험 연결: CMSIS-DSP의 `arm_dot_prod_q15`는 결과를 **q63**(64비트)로 돌려준다. Cortex-M에는 40비트 누산기가 없으니 64비트 누산(`SMLALD`)으로 guard bit를 확보하는 것이다. C1 7절의 int8 NN 층이 **int32**에 누산하는 것도 같은 계산이다: int8 × int8 곱은 최대 2¹⁴이라 int32에 2¹⁶ ≈ 65 000개 이상 안전하게 쌓인다.

### 2.4 Q15 FIR vs float — SNR로 비교

31탭 저역통과 FIR(창 함수 sinc, 차단 0.1 fs)을 세 가지로 계산해 double 레퍼런스와 비교한다. 입력은 두 톤의 합이고, 진폭을 0.9 → 0.1 → 0.01로 줄인다. 계수와 입력은 모두 Q15로 정확히 표현되는 값으로 맞춰서 **순수하게 연산 방식의 오차**만 본다.

- float32: 곱·합을 float32로
- Q15 wide-acc: 곱을 넓은 누산기(int64)에 Q30 그대로 쌓고 **마지막에 한 번** 반올림·포화 (DSP 방식)
- Q15 trunc-each: 곱마다 `>> 15`로 Q15로 자른 뒤 더함 (흔한 잘못된 구현)

```c
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#define NT 31
#define NS 4000
static double hd[NT]; static float hf[NT]; static int16_t hq[NT];
static int16_t sat16(int64_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)v; }

static void snr_db(double amp) {
    static double xd[NS]; static float xf[NS]; static int16_t xq[NS];
    for (int n = 0; n < NS; n++) {               /* 2 tones, input already Q15-exact */
        double v = amp * (0.6 * sin(0.05 * n) + 0.4 * sin(0.9 * n + 1.0));
        xq[n] = sat16(lround(v * 32768.0)); xd[n] = xq[n] / 32768.0; xf[n] = (float)xd[n];
    }
    double es = 0, ef = 0, eq = 0, et = 0;
    for (int n = NT; n < NS; n++) {
        double yd = 0; float yf = 0; int64_t acc = 0; int32_t acct = 0;
        for (int k = 0; k < NT; k++) {
            yd += hd[k] * xd[n - k];
            yf += hf[k] * xf[n - k];
            acc += (int32_t)hq[k] * xq[n - k];                 /* Q30 in wide acc */
            acct += ((int32_t)hq[k] * xq[n - k]) >> 15;        /* truncate each product */
        }
        double yq = sat16((acc + (1 << 14)) >> 15) / 32768.0;  /* round once at the end */
        double yt = sat16(acct) / 32768.0;
        es += yd * yd; ef += (yf - yd) * (yf - yd);
        eq += (yq - yd) * (yq - yd); et += (yt - yd) * (yt - yd);
    }
    printf("amp %.2f | float32 %6.1f dB | Q15 wide-acc %5.1f dB | Q15 trunc-each %5.1f dB\n",
           amp, ef > 0 ? 10 * log10(es / ef) : INFINITY, 10 * log10(es / eq), 10 * log10(es / et));
}
int main(void) {
    double s = 0;
    for (int k = 0; k < NT; k++) {               /* windowed-sinc low-pass, fc = 0.1 fs */
        double m = k - (NT - 1) / 2.0;
        double sinc = m == 0 ? 2 * 0.1 : sin(2 * M_PI * 0.1 * m) / (M_PI * m);
        hd[k] = sinc * (0.54 - 0.46 * cos(2 * M_PI * k / (NT - 1))); s += hd[k];
    }
    for (int k = 0; k < NT; k++) { hd[k] /= s; hq[k] = sat16(lround(hd[k] * 32768.0)); hd[k] = hq[k] / 32768.0; hf[k] = (float)hd[k]; }
    snr_db(0.9); snr_db(0.1); snr_db(0.01);
    return 0;
}
```

```text
amp 0.90 | float32  140.4 dB | Q15 wide-acc  92.7 dB | Q15 trunc-each  60.0 dB
amp 0.10 | float32  142.1 dB | Q15 wide-acc  73.6 dB | Q15 trunc-each  40.9 dB
amp 0.01 | float32    inf dB | Q15 wide-acc  53.6 dB | Q15 trunc-each  20.9 dB
```

손계산으로 92.7 dB를 예측해 보자. wide-acc의 오차는 출력 반올림 한 번뿐이고, 그 잡음 전력은 LSB²/12 = (2⁻¹⁵)²/12 ≈ 7.76 × 10⁻¹¹이다. 0.05 rad/sample 톤(진폭 0.9 × 0.6 = 0.54)은 통과대역에, 0.9 rad/sample ≈ 0.143 cycle/sample 톤은 저지대역에 있어서 출력 전력은 약 0.54²/2 ≈ 0.146이다.

```
SNR ≈ 10 · log10(0.146 / 7.76e−11) = 10 · log10(1.88e9) ≈ 92.7 dB
진폭 1/10 → 신호 전력 1/100 → −20 dB : 92.7 → 72.7  (측정 73.6, 두 번째 톤 잔여분 차이)
```

말로 하면: 고정소수점 잡음은 신호 크기와 **무관하게 일정**하고, 그래서 신호가 10배 작아질 때마다 SNR이 20 dB씩 떨어진다. float는 지수가 신호 크기를 따라가기 때문에 SNR이 거의 그대로다(140 dB대). 진폭 0.01에서 float32 오차가 0(inf dB)으로 나온 것은 이 입력에선 작은 정수 곱과 합이 float32 가수 24비트 안에 딱 들어가서 우연히 정확했기 때문이다.

곱마다 자르면 탭 31개 각각에서 평균 −0.5 LSB bias가 쌓여 약 −15.5 LSB의 DC 오차가 생긴다. 오차 전력 ≈ 15.5² + 31/12 ≈ 243 LSB², wide-acc의 1/12 LSB²보다 약 2900배(≈ 35 dB) 크다고 근사할 수 있고, 측정은 약 33 dB 차이다.

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg"> <text x="10" y="20" font-size="13">31-tap FIR 출력 SNR (vs double 기준) — 입력 진폭별</text> <line x1="60" y1="250" x2="620" y2="250" stroke="currentColor"/><line x1="60" y1="250" x2="60" y2="50" stroke="currentColor"/> <line x1="56" y1="250.0" x2="60" y2="250.0" stroke="currentColor"/> <text x="52" y="254.0" font-size="12" text-anchor="end">0</text> <line x1="56" y1="210.0" x2="60" y2="210.0" stroke="currentColor"/> <text x="52" y="214.0" font-size="12" text-anchor="end">30</text> <line x1="56" y1="170.0" x2="60" y2="170.0" stroke="currentColor"/>
<text x="52" y="174.0" font-size="12" text-anchor="end">60</text> <line x1="56" y1="130.0" x2="60" y2="130.0" stroke="currentColor"/> <text x="52" y="134.0" font-size="12" text-anchor="end">90</text> <line x1="56" y1="90.0" x2="60" y2="90.0" stroke="currentColor"/> <text x="52" y="94.0" font-size="12" text-anchor="end">120</text> <line x1="56" y1="50.0" x2="60" y2="50.0" stroke="currentColor"/> <text x="52" y="54.0" font-size="12" text-anchor="end">150</text> <text x="14" y="140" font-size="12">dB</text> <rect x="90" y="62.8" width="40" height="187.2" fill="#4a7bd0" fill-opacity="0.6"/> <text x="110" y="56.80000000000001" font-size="12" text-anchor="middle">140.4</text>
<rect x="138" y="126.4" width="40" height="123.6" fill="#e08a3c" fill-opacity="0.6"/> <text x="158" y="120.4" font-size="12" text-anchor="middle">92.7</text> <rect x="186" y="170.0" width="40" height="80.0" fill="#d0564a" fill-opacity="0.6"/> <text x="206" y="164.0" font-size="12" text-anchor="middle">60.0</text> <text x="158" y="268" font-size="12" text-anchor="middle">진폭 0.90</text> <rect x="275" y="60.5" width="40" height="189.5" fill="#4a7bd0" fill-opacity="0.6"/> <text x="295" y="54.53333333333336" font-size="12" text-anchor="middle">142.1</text> <rect x="323" y="151.9" width="40" height="98.1" fill="#e08a3c" fill-opacity="0.6"/>
<text x="343" y="145.86666666666667" font-size="12" text-anchor="middle">73.6</text> <rect x="371" y="195.5" width="40" height="54.5" fill="#d0564a" fill-opacity="0.6"/> <text x="391" y="189.46666666666667" font-size="12" text-anchor="middle">40.9</text> <text x="343" y="268" font-size="12" text-anchor="middle">진폭 0.10</text> <rect x="460" y="50" width="40" height="200" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-dasharray="4 3"/> <text x="480" y="44" font-size="12" text-anchor="middle">오차 0</text> <rect x="508" y="178.5" width="40" height="71.5" fill="#e08a3c" fill-opacity="0.6"/> <text x="528" y="172.53333333333333" font-size="12" text-anchor="middle">53.6</text>
<rect x="556" y="222.1" width="40" height="27.9" fill="#d0564a" fill-opacity="0.6"/> <text x="576" y="216.13333333333333" font-size="12" text-anchor="middle">20.9</text> <text x="528" y="268" font-size="12" text-anchor="middle">진폭 0.01</text> <rect x="80" y="278" width="12" height="12" fill="#4a7bd0" fill-opacity="0.6"/> <text x="98" y="289" font-size="12">float32</text> <rect x="250" y="278" width="12" height="12" fill="#e08a3c" fill-opacity="0.6"/> <text x="268" y="289" font-size="12">Q15 넓은 누산기</text> <rect x="420" y="278" width="12" height="12" fill="#d0564a" fill-opacity="0.6"/> <text x="438" y="289" font-size="12">Q15 곱마다 절삭</text>
</svg>
```

그림 4 — 입력 진폭별 FIR 출력 SNR (위 실행 결과). 파랑 float32는 거의 일정, 주황 Q15 wide-acc는 진폭 10배당 20 dB 하락, 빨강 곱마다 절삭은 거기서 약 33 dB 더 나쁘다. 점선 막대는 오차가 0으로 측정된 경우.

임베디드 연결:

- 오디오 front-end에서 작은 소리(속삭임, 먼 화자)는 진폭 0.01 수준이다. Q15 한 번 반올림으로도 SNR 54 dB면 KWS 입력으로 대부분 충분하지만, **곱마다 자르는 구현은 21 dB**라 인식률이 눈에 띄게 떨어질 수 있다.
- 해결책은 세 가지: (1) 넓은 누산기 + 마지막 한 번 반올림, (2) Q31 경로(HiFi·Hexagon 모두 32비트 곱 지원), (3) 2.5절의 block floating point.

### 2.5 Block floating point (BFP) — 지수를 블록마다 하나

float는 샘플마다 지수를 들고 다니고, 고정소수점은 지수가 전역 상수다. 그 중간이 **block floating point**: 샘플 N개(예: 32개)가 지수 하나를 공유하고, 각 샘플은 정수 가수(mantissa)만 가진다. 블록의 최대 절대값에 맞춰 지수를 정하므로 조용한 블록은 작은 지수를 받아 해상도를 되찾는다.

```
블록 b의 지수 e = ceil(log2(max|x_i|))       (max|x| ≤ 2^e)
가수 q_i = round(x_i / 2^(e − (bits−1)))       (bits 비트 정수)
복원 x̂_i = q_i · 2^(e − (bits−1))
```

말로 하면: 블록마다 "이 블록은 몇 비트 시프트해서 읽어라"라는 숫자 하나를 붙인다. FFT의 stage마다 스케일링(B5 표의 "stage마다 scaling")이 이 아이디어이고, ML의 **per-group 양자화**(C1 5절)와 MX 포맷(블록 공유 지수)도 같은 계열이다.

```python
import numpy as np
rng = np.random.default_rng(0)
n = np.arange(4096)
env = np.where(n < 2048, 0.7, 0.005)                  # loud half, then very quiet half
x = env * np.sin(2 * np.pi * 0.013 * n) + env * 0.1 * rng.standard_normal(n.size)

def snr(ref, q):
    return 10 * np.log10(np.sum(ref**2) / np.sum((ref - q)**2))

def fixed(x, bits):                                   # one global power-of-2 scale (Q-format)
    s = 2.0 ** -(bits - 1)
    return np.clip(np.round(x / s), -2**(bits-1), 2**(bits-1) - 1) * s

def bfp(x, bits, block=32):                           # shared exponent per block
    y = np.empty_like(x)
    for i in range(0, x.size, block):
        b = x[i:i + block]
        e = np.ceil(np.log2(np.max(np.abs(b)) + 1e-30))   # block exponent: max|b| <= 2^e
        s = 2.0 ** (e - (bits - 1))
        y[i:i + block] = np.clip(np.round(b / s), -2**(bits-1), 2**(bits-1) - 1) * s
    return y

for bits in (8, 16):
    f, b = fixed(x, bits), bfp(x, bits)
    print(f"{bits:2d}-bit mantissa | fixed: loud {snr(x[:2048], f[:2048]):5.1f} dB, "
          f"quiet {snr(x[2048:], f[2048:]):5.1f} dB | BFP(32): loud {snr(x[:2048], b[:2048]):5.1f} dB, "
          f"quiet {snr(x[2048:], b[2048:]):5.1f} dB")
print("BFP overhead: one exponent byte per 32 samples ->", 8 / 32, "extra bits/sample")
```

```text
 8-bit mantissa | fixed: loud  47.0 dB, quiet   2.3 dB | BFP(32): loud  47.0 dB, quiet  46.2 dB
16-bit mantissa | fixed: loud  95.0 dB, quiet  52.3 dB | BFP(32): loud  95.0 dB, quiet  94.2 dB
BFP overhead: one exponent byte per 32 samples -> 0.25 extra bits/sample
```

출력에서 볼 것:

- 8비트 고정(전역 scale)은 조용한 구간(진폭 0.005)에서 SNR **2.3 dB** — 거의 모든 샘플이 0 또는 ±1 LSB라 신호가 사라진다. 같은 8비트 BFP(32 샘플 블록)는 **46.2 dB**로, 시끄러운 구간과 비슷한 품질을 유지한다.
- 16비트에서도 고정은 조용한 구간이 52 dB로 떨어지고 BFP는 94 dB를 유지한다.
- 비용은 블록당 지수 1바이트 = 샘플당 0.25비트. 하드웨어 입장에선 블록마다 "최대값 찾기(또는 leading-zero count) + 시프트"가 추가된다. DSP에 `NSA`(normalize shift amount, Xtensa)나 count-leading-sign 류 명령이 있는 이유다.

### 2.6 흔한 함정

- `(int16_t)((a * b) >> 15)`만 쓰고 `−1 × −1`을 테스트하지 않는다 → 드물게 부호 뒤집힘. 테스트 벡터에 반드시 `INT16_MIN`을 넣는다.
- 중간 합에 포화를 건다 → 순서에 따라 결과가 달라지고 틀린다. 넓게 누산하고 마지막에 한 번.
- 곱마다 `>> 15` → bias 누적. SNR 30 dB 이상 손해.
- Q31 × Q31을 `int64`에 넣고 guard bit가 충분하다고 착각 → Q62는 1비트 여유뿐.
- float 레퍼런스와 비교하면서 "정확히 같아야 한다"고 기대 → 비교 기준은 SNR 또는 bit-exact **고정소수점 레퍼런스**(같은 반올림 규칙).

---

## 3. 원형 버퍼와 modulo addressing

### 3.1 직관: 새 샘플이 올 때마다 전부 밀 것인가

FIR은 최근 T개 샘플이 필요하다. 샘플이 올 때마다 배열을 한 칸씩 밀면(`memmove`) 샘플당 T번 복사다. 대신 **원형 버퍼**를 쓰면 쓰기 위치 `w`만 한 칸 옮기고 가장 오래된 값을 덮어쓴다. Don이 SSD에서 NVMe submission/completion queue나 로그 링버퍼로 매일 쓰던 그 구조다. 차이는 FIR은 **매 샘플마다 T개를 거꾸로 읽는다**는 것이고, 그 T번의 읽기마다 wrap 검사가 필요하다는 것이다.

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg"> <defs><marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <text x="10" y="20" font-size="13">길이 8 원형 delay line: 새 샘플은 w에 쓰고, FIR은 w에서 거꾸로 읽는다</text> <rect x="140.0" y="50.0" width="60" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/> <text x="170.0" y="70.0" font-size="12" text-anchor="middle">x[n−5]</text> <text x="170.0" y="117.0" font-size="12" text-anchor="middle">[0]</text> <rect x="207.2" y="77.8" width="60" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="237.2" y="97.8" font-size="12" text-anchor="middle">x[n−4]</text> <text x="203.2" y="130.8" font-size="12" text-anchor="middle">[1]</text> <rect x="235.0" y="145.0" width="60" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/> <text x="265.0" y="165.0" font-size="12" text-anchor="middle">x[n−3]</text> <text x="217.0" y="164.0" font-size="12" text-anchor="middle">[2]</text> <rect x="207.2" y="212.2" width="60" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/> <text x="237.2" y="232.2" font-size="12" text-anchor="middle">x[n−2]</text> <text x="203.2" y="197.2" font-size="12" text-anchor="middle">[3]</text>
<rect x="140.0" y="240.0" width="60" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/> <text x="170.0" y="260.0" font-size="12" text-anchor="middle">x[n−1]</text> <text x="170.0" y="211.0" font-size="12" text-anchor="middle">[4]</text> <rect x="72.8" y="212.2" width="60" height="30" rx="5" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/> <text x="102.8" y="232.2" font-size="12" text-anchor="middle">x[n]</text> <text x="136.8" y="197.2" font-size="12" text-anchor="middle">[5]</text> <rect x="45.0" y="145.0" width="60" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="75.0" y="165.0" font-size="12" text-anchor="middle">x[n−7]</text> <text x="123.0" y="164.0" font-size="12" text-anchor="middle">[6]</text> <rect x="72.8" y="77.8" width="60" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/> <text x="102.8" y="97.8" font-size="12" text-anchor="middle">x[n−6]</text> <text x="136.8" y="130.8" font-size="12" text-anchor="middle">[7]</text> <text x="170" y="164" font-size="12" text-anchor="middle">w = 5</text> <text x="330" y="70" font-size="13">주소 계산 3가지</text> <text x="330" y="95" font-size="12">① 소프트웨어 %:  i = (i + 1) % N   → 나눗셈</text> <text x="330" y="118" font-size="12">② 2의 거듭제곱:  i = (i + 1) &amp; (N − 1)</text>
<text x="330" y="141" font-size="12">③ DSP 하드웨어: load x, p++ (circ)</text> <text x="350" y="160" font-size="12">AGU가 경계를 넘으면 시작 주소로 되감음</text> <text x="350" y="178" font-size="12">→ 추가 명령 0개, 추가 cycle 0</text> <text x="330" y="210" font-size="12">다음 샘플이 오면 [6]에 쓴다 (가장 오래된</text> <text x="330" y="228" font-size="12">x[n−7]을 덮어씀). memmove가 없다.</text> <text x="330" y="262" font-size="12">주황 = 방금 쓴 샘플 · 파랑 = FIR이 읽는 이력</text> </svg>
```

그림 5 — 길이 8 원형 delay line. 쓰기 위치 w=5에 x[n]을 쓰고, FIR은 [5], [4], …, [0], [7], [6] 순서로 읽는다. [0]에서 [7]로 넘어가는 순간이 wrap이다.

### 3.2 주소 계산 세 가지

| 방식 | 코드 | 비용 | 제약 |
|---|---|---|---|
| 소프트웨어 나머지 | `i = (i + 1) % N` | 정수 나눗셈 (수~수십 cycle) | 없음 |
| 비트 마스크 | `i = (i + 1) & (N − 1)` | AND 1개 | N이 2의 거듭제곱 |
| 비교·분기 | `if (++i == N) i = 0;` | 비교 + 분기(또는 조건부 이동) | 없음, 분기 예측 의존 |
| 미러(2N) 버퍼 | 쓸 때 두 곳에 쓰기 | 쓰기 1회 추가, 메모리 2배 | 읽기 구간이 항상 연속 |
| DSP 하드웨어 circular | `load x, p++ (circ)` | **0** — AGU가 처리 | 버퍼 시작·길이 레지스터 설정 |

하드웨어 circular addressing은 AGU 안에 "시작 주소 B, 길이 L" 레지스터를 두고, post-increment 결과가 `B + L`을 넘으면 L을 빼는 비교·뺄셈을 **같은 cycle에** 한다. 명령도 cycle도 추가되지 않는다. N이 2의 거듭제곱일 필요도 없다(구현에 따라 다름). ADI의 I/M/L/B 레지스터, TI C6000의 AMR 레지스터, Hexagon의 `:circ` 주소 모드가 이 기능이다. HiFi도 원형 버퍼 load/store를 지원하는 것으로 알려져 있다(정확한 명령·레지스터 이름은 ISA 문서 확인).

### 3.3 이 Mac에서 벤치마크

32탭 FIR을 200만 샘플에 돌리며 탭마다 읽기 주소를 `%`로, `&`로 계산하는 경우, 그리고 미러 버퍼로 wrap 자체를 없앤 경우를 비교한다. `%`의 N은 `volatile`에서 읽어 컴파일러가 `&`로 바꾸지 못하게 했다(실제 펌웨어에서 길이가 런타임 설정인 경우와 같다).

```c
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <string.h>
#define T 32                      /* taps = delay-line length (power of 2) */
#define NS 2000000
static int16_t h[T], x[NS], buf[2 * T];
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

#define RUN(NAME, NEXT)                                                        \
    { unsigned w = 0; int64_t chk = 0; memset(buf, 0, sizeof buf); double t0 = now(); \
      for (int n = 0; n < NS; n++) {                                           \
          buf[w] = x[n]; int32_t acc = 0; unsigned r = w;                      \
          for (int k = 0; k < T; k++) { acc += h[k] * buf[r]; r = (r + len - 1) NEXT; } \
          chk += acc; w = (w + 1) NEXT; }                                      \
      printf("%-14s %6.2f ns/sample  chk=%lld\n", NAME, (now() - t0) / NS * 1e9, (long long)chk); }

int main(void) {
    volatile unsigned vlen = T;   /* runtime length: compiler can't turn % into & */
    unsigned len = vlen, mask = len - 1;
    for (int k = 0; k < T; k++) h[k] = (int16_t)(k * 37 % 200 - 100);
    for (int n = 0; n < NS; n++) x[n] = (int16_t)((n * 7919) % 2001 - 1000);
    RUN("modulo %", % len)
    RUN("mask &", & mask)
    { unsigned w = 0; int64_t chk = 0; memset(buf, 0, sizeof buf); double t0 = now(); /* mirrored buffer: no wrap in inner loop */
      for (int n = 0; n < NS; n++) {
          buf[w] = buf[w + T] = x[n]; int32_t acc = 0; const int16_t *p = &buf[w + T];
          for (int k = 0; k < T; k++) acc += h[k] * p[-k];
          chk += acc; w = (w + 1) & mask; }
      printf("%-14s %6.2f ns/sample  chk=%lld\n", "mirror (2N)", (now() - t0) / NS * 1e9, (long long)chk); }
    return 0;
}
```

```text
modulo %        74.93 ns/sample  chk=132180
mask &          13.38 ns/sample  chk=132180
mirror (2N)      1.46 ns/sample  chk=132180
```

출력에서 볼 것:

- 세 방식의 `chk`가 같다 — 결과는 동일하고 비용만 다르다.
- `%`는 샘플당 약 75 ns = 탭당 약 2.3 ns(다시 돌리면 75~85 ns 사이로 변동한다 — 벤치마크 숫자는 자릿수만 믿는다). 어셈블리를 보면 탭마다 `udiv`가 들어 있다. `&`는 탭당 약 0.42 ns로 5배 이상 빠르다.
- 미러 버퍼는 샘플당 약 1.5 ns로 `&`보다 또 9배 빠르다. wrap이 사라져 읽기가 연속 주소가 되자 컴파일러가 NEON으로 **자동 벡터화**했기 때문이다(`smlal.4s`, 역순 읽기를 위한 `rev64` 확인). 즉 wrap 검사는 그 자체의 비용보다 **벡터화를 막는 비용**이 더 크다.
- DSP에서는 `load, p++ (circ)`가 wrap을 공짜로 처리하므로 "미러 버퍼 없이" 벡터 load까지 가능하다. 이것이 "DSP에선 circular addressing이 free"의 의미다. 숫자는 Apple M2 기준이고 CPU마다 다르다.

### 3.4 delay line은 FIR만의 것이 아니다 — IIR, RNN 상태, 스트리밍 conv

- **IIR 필터** (G5): `y[n] = Σ b_k·x[n−k] − Σ a_k·y[n−k]`. 입력과 **출력**의 과거값 두 개의 delay line이 필요하다. 되먹임이 있어서 고정소수점 오차가 누적·발산할 수 있고, 그래서 2차 섹션(biquad)으로 쪼개고 Q31이나 넓은 누산을 쓴다.
- **스트리밍 1D conv** (B2 8절): 커널 크기 K, dilation d인 causal conv는 층마다 과거 `(K−1)·d` 프레임을 들고 있어야 한다. 이게 층별 원형 버퍼다. 스트리밍 KWS 모델을 DSP에 올릴 때 "층마다 상태 버퍼"를 원형으로 관리한다.
- **RNN/GRU 상태** (B3): 은닉 상태 h는 delay line 길이 1짜리 되먹임이다.

### 3.5 블록 처리 + DMA ping-pong + 상태 버퍼 (B5와 연결)

실제 오디오 펌웨어는 샘플 단위가 아니라 DMA 블록 단위(예: 64 샘플, 4 ms @16 kHz)로 처리한다. 이때 가장 흔한 구조는 CMSIS-DSP `arm_fir_*`와 같은 **상태 버퍼 = [직전 T−1개 | 새 블록 B개]** 이다. 블록 안에서는 읽기가 연속이라 벡터화가 되고, 블록 끝에서 T−1개만 앞으로 옮긴다. DMA가 ping을 채우는 동안 CPU/DSP는 pong을 처리한다(B5 9절의 ping-pong).

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#define T 8                                  /* taps */
#define B 64                                 /* DMA block (samples per ping/pong half) */
#define NB 10                                /* blocks to stream */
static const int16_t h[T] = {1000, 3000, 6000, 8000, 8000, 6000, 3000, 1000};
static int16_t state[T - 1 + B];             /* [T-1 history | current block] contiguous */

static int16_t fir_out(const int16_t *newest) {       /* newest points at x[n] */
    int64_t acc = 0;
    for (int k = 0; k < T; k++) acc += (int32_t)h[k] * newest[-k];
    acc = (acc + (1 << 14)) >> 15;
    return (int16_t)(acc > 32767 ? 32767 : acc < -32768 ? -32768 : acc);
}
int main(void) {
    static int16_t x[NB * B], y_blk[NB * B], y_ref[NB * B], dma[2][B];
    for (int n = 0; n < NB * B; n++) x[n] = (int16_t)((n * 2654435761u >> 17) % 20001 - 10000);
    for (int b = 0; b < NB; b++) {
        memcpy(dma[b & 1], &x[b * B], sizeof dma[0]);          /* "DMA fills ping or pong" */
        memcpy(&state[T - 1], dma[b & 1], sizeof dma[0]);      /* append block after history */
        for (int i = 0; i < B; i++) y_blk[b * B + i] = fir_out(&state[T - 1 + i]);
        memmove(state, &state[B], (T - 1) * sizeof state[0]); /* keep last T-1 as history */
    }
    int16_t hist[T] = {0}; int mism = 0;                        /* reference: sample by sample */
    for (int n = 0; n < NB * B; n++) {
        memmove(&hist[1], hist, (T - 1) * sizeof hist[0]); hist[0] = x[n];
        int16_t rev[T]; for (int k = 0; k < T; k++) rev[T - 1 - k] = hist[k];
        y_ref[n] = fir_out(&rev[T - 1]);
        mism += y_ref[n] != y_blk[n];
    }
    printf("samples=%d mismatches=%d  y[0..3]=%d %d %d %d  y[64..65]=%d %d\n",
           NB * B, mism, y_blk[0], y_blk[1], y_blk[2], y_blk[3], y_blk[64], y_blk[65]);
    printf("state RAM = %zu B (T-1+B int16), history copy per block = %d samples\n", sizeof state, T - 1);
    return 0;
}
```

```text
samples=640 mismatches=0  y[0..3]=-305 -1213 -2793 -4496  y[64..65]=-2454 -1815
state RAM = 142 B (T-1+B int16), history copy per block = 7 samples
```

출력에서 볼 것: 블록 처리 결과가 샘플 단위 레퍼런스와 640개 전부 일치한다(`mismatches=0`). 블록 경계를 넘는 출력(`y[64]`)도 이전 블록의 이력을 제대로 쓴다. 블록당 복사는 T−1 = 7개뿐이라 B = 64일 때 샘플당 약 0.1개다. RAM은 `(T−1+B) × 2 = 142 B`.

Don 경험 연결: SSD에서 NAND 페이지를 DMA로 받으면서 이전 페이지 꼬리(ECC codeword 경계 등)를 붙잡아 두던 것과 같은 모양이다. "블록 경계에서 상태를 이어 붙이는 코드"가 버그의 온상이라는 것도 같다 — 첫 블록(이력 0), 블록 크기 < T, 블록 크기 변경 시를 반드시 테스트한다.

---

## 4. VLIW와 스케줄링 — 누가 번들을 채우나

### 4.1 직관: 슬롯이 4개인 트럭

VLIW 기계는 매 cycle 출발하는 트럭 한 대에 칸(slot)이 4개 있다고 생각하면 된다. 칸마다 실을 수 있는 짐 종류(load, MAC, ALU…)가 정해져 있고, 트럭은 짐이 덜 찼어도 **정시에 출발**한다(빈 칸 = NOP). 어떤 짐을 어느 트럭에 실을지는 출발 전에 **컴파일러가 전부 정한다.** 하드웨어는 검사하지 않는다(적어도 전통적 VLIW는). 그래서:

- 하드웨어가 단순하다 → 면적·전력이 작다 → DSP에 적합
- 성능이 컴파일러 품질에 달려 있다 → 벤더 컴파일러와 hand-tuned 라이브러리가 중요
- 컴파일 시점에 모르는 지연(캐시 미스)에 약하다 → TCM + DMA로 지연을 고정

superscalar OoO CPU(E1)는 반대다. 하드웨어가 실행 중에 의존성을 보고 명령을 재배열한다. 같은 병렬성을 찾는 일을 **실행 시간에 하드웨어로** 하느냐, **컴파일 시간에 소프트웨어로** 하느냐의 차이다.

### 4.2 장난감 기계와 dot product

4-slot 기계를 정의한다: load 유닛 2개(LD0, LD1), MAC 유닛 1개, ALU 1개(포인터 덧셈·카운터·분기). 지연은 load 2 cycle, MAC 2 cycle, ALU 1 cycle. dot product 한 번의 반복을 두 가지로 쓴다.

- **A (RISC식)**: `ldx`, `ldh`, `add px`, `add ph`, `mac`, `dec`, `bnz` — 7개 명령
- **B (DSP식)**: `ldx++`, `ldh++`(post-increment load, AGU가 포인터 처리), `mac` — 3개. 카운터와 분기는 zero-overhead loop가 가져간다

먼저 한 반복을 **겹치지 않고** 스케줄한다. 각 op을 의존성이 풀린 가장 이른 cycle, 그리고 그 cycle에 해당 유닛이 비어 있는 곳에 놓는 greedy **list scheduling**이다.

```python
# 4-slot VLIW toy: 2 load units (LD), 1 MAC unit (MAC), 1 ALU (pointer add, loop counter, branch)
UNITS = {"LD": 2, "MAC": 1, "ALU": 1}
LAT = {"LD": 2, "MAC": 2, "ALU": 1}          # cycles until the result can be used

def list_schedule(ops, deps):
    """ops: {name: unit}; deps: [(src, dst)] inside one iteration. Greedy ASAP list scheduling."""
    t, used = {}, {}
    for op in ops:                             # ops are given in topological order
        ready = max([t[s] + LAT[ops[s]] for s, d in deps if d == op], default=0)
        c = ready
        while used.get((c, ops[op]), 0) >= UNITS[ops[op]]:
            c += 1
        t[op] = c
        used[(c, ops[op])] = used.get((c, ops[op]), 0) + 1
    return t

def show(t, ops):
    for c in range(max(t.values()) + 1):
        print(f"  cycle {c}: {{ " + " | ".join(o for o in t if t[o] == c) + " }")
    return max(t.values()) + 1                 # next iteration starts after the last bundle

# A) plain RISC-style loop body: explicit pointer adds, counter, branch
opsA = {"ldx": "LD", "ldh": "LD", "addpx": "ALU", "addph": "ALU",
        "mac": "MAC", "dec": "ALU", "bnz": "ALU"}
depsA = [("ldx", "mac"), ("ldh", "mac"), ("dec", "bnz")]
# B) DSP-style: post-increment loads (AGU), zero-overhead HW loop -> no ALU ops at all
opsB = {"ldx++": "LD", "ldh++": "LD", "mac": "MAC"}
depsB = [("ldx++", "mac"), ("ldh++", "mac")]

N = 64
for name, ops, deps in [("A: RISC-style", opsA, depsA), ("B: post-inc + HW loop", opsB, depsB)]:
    print(name)
    L = show(list_schedule(ops, deps), ops)
    print(f"  one iteration = {L} cycles -> {N} MACs, no overlap = {N * L} cycles\n")
```

```text
A: RISC-style
  cycle 0: { ldx | ldh | addpx }
  cycle 1: { addph }
  cycle 2: { mac | dec }
  cycle 3: { bnz }
  one iteration = 4 cycles -> 64 MACs, no overlap = 256 cycles

B: post-inc + HW loop
  cycle 0: { ldx++ | ldh++ }
  cycle 1: {  }
  cycle 2: { mac }
  one iteration = 3 cycles -> 64 MACs, no overlap = 192 cycles
```

출력에서 볼 것:

- A는 ALU op이 4개(`add px`, `add ph`, `dec`, `bnz`)인데 ALU가 1개라서 반복당 최소 4 cycle이다. MAC 유닛은 4 cycle 중 1 cycle만 일한다(효율 25%). 병목은 곱셈이 아니라 **오버헤드 명령**이다.
- B는 오버헤드가 사라졌지만 load → MAC 사이의 지연 2 cycle 때문에 cycle 1이 **빈 번들**(`{ }`)이다. 반복을 겹치지 않는 한 3 cycle/반복에서 더 줄지 않는다.
- 다음 단계: 다음 반복의 load를 이번 반복의 빈 칸에 미리 넣으면 된다. 이게 software pipelining이다.

### 4.3 Software pipelining과 II

**Software pipelining**(modulo scheduling)은 루프의 반복 i, i+1, i+2…를 조립 라인처럼 겹쳐서, 매 **II cycle마다 새 반복을 시작**하게 만드는 컴파일러 기법이다. II = initiation interval. II가 작을수록 빠르다. II의 하한은 두 가지다.

```
ResMII = max over 유닛 종류 ⌈ (그 유닛을 쓰는 op 수) / (그 유닛 개수) ⌉     (자원 한계)
RecMII = max over 되먹임 사이클 ⌈ (사이클의 지연 합) / (사이클의 반복 거리 합) ⌉   (의존성 한계)
MII    = max(ResMII, RecMII)
총 cycle ≈ (반복 수 − 1) · II + 한 반복의 깊이
```

말로 하면: 유닛이 모자라면 한 반복을 II cycle 안에 다 실을 수 없고(ResMII), 이번 반복의 결과를 다음 반복이 기다려야 하면 그 지연만큼은 기다려야 한다(RecMII). dot product의 되먹임은 `acc += …`이다 — 이번 MAC 결과를 다음 MAC이 쓴다. MAC 지연이 2면 RecMII = 2.

손계산:

- A: ALU op 4개 / ALU 1개 → ResMII = 4. RecMII = 2. II = 4 → **4 cycle/MAC**. 겹쳐도 안 빨라진다(ALU가 병목).
- B: load 2개 / LD 2개 = 1, MAC 1개 / 1 = 1 → ResMII = 1. RecMII = 2 → II = 2 → **2 cycle/MAC**.
- C: B를 2배 unroll하고 **누산기를 2개**(`acc0`, `acc1`) 쓴다. 한 반복에 load 4개, MAC 2개 → ResMII = max(4/2, 2/1) = 2. 되먹임은 acc0끼리, acc1끼리 각각 지연 2 / 거리 1 → RecMII = 2. II = 2에 MAC 2개 → **1 cycle/MAC**. 마지막에 `acc0 + acc1`을 한 번 더한다.

```python
import math
UNITS = {"LD": 2, "MAC": 1, "ALU": 1}
LAT = {"LD": 2, "MAC": 2, "ALU": 1}

def modulo_schedule(ops, deps):
    """deps: (src, dst, distance). distance=1 means 'dst of the NEXT iteration needs src'."""
    res_mii = max(math.ceil(sum(u == k for u in ops.values()) / UNITS[k]) for k in UNITS)
    rec_mii = max([math.ceil(LAT[ops[s]] / d) for s, dd, d in deps if s == dd and d > 0], default=1)
    ii = max(res_mii, rec_mii)
    while True:                                   # try II, II+1, ... (no backtracking: toy)
        t, mrt = {}, {}
        for op in ops:
            c = max([t[s] + LAT[ops[s]] - ii * d for s, dd, d in deps if dd == op and s in t], default=0)
            while mrt.get((c % ii, ops[op]), 0) >= UNITS[ops[op]]:
                c += 1
            t[op] = c
            mrt[(c % ii, ops[op])] = mrt.get((c % ii, ops[op]), 0) + 1
        ok = all(t[dd] >= t[s] + LAT[ops[s]] - ii * d for s, dd, d in deps)
        if ok:
            return res_mii, rec_mii, ii, t
        ii += 1

N = 64
cases = {
    "A: RISC-style body": ({"ldx": "LD", "ldh": "LD", "addpx": "ALU", "addph": "ALU", "mac": "MAC",
                            "dec": "ALU", "bnz": "ALU"},
                           [("ldx", "mac", 0), ("ldh", "mac", 0), ("mac", "mac", 1), ("dec", "bnz", 0)], 1),
    "B: 1 accumulator": ({"ldx": "LD", "ldh": "LD", "mac": "MAC"},
                         [("ldx", "mac", 0), ("ldh", "mac", 0), ("mac", "mac", 1)], 1),
    "C: 2 accumulators (unroll x2)": (
        {"ldx0": "LD", "ldh0": "LD", "ldx1": "LD", "ldh1": "LD", "mac0": "MAC", "mac1": "MAC"},
        [("ldx0", "mac0", 0), ("ldh0", "mac0", 0), ("ldx1", "mac1", 0), ("ldh1", "mac1", 0),
         ("mac0", "mac0", 1), ("mac1", "mac1", 1)], 2),
}
for name, (ops, deps, macs_per_iter) in cases.items():
    res, rec, ii, t = modulo_schedule(ops, deps)
    depth = max(t.values()) + 1                   # cycles one iteration spans (prologue depth)
    iters = N // macs_per_iter
    total = (iters - 1) * ii + depth
    print(f"{name}: ResMII={res} RecMII={rec} -> II={ii}, stage times {t}")
    print(f"   {iters} iters: (iters-1)*II + depth = {total} cycles = {total / N:.2f} cycles/MAC")
```

```text
A: RISC-style body: ResMII=4 RecMII=2 -> II=4, stage times {'ldx': 0, 'ldh': 0, 'addpx': 0, 'addph': 1, 'mac': 2, 'dec': 2, 'bnz': 3}
   64 iters: (iters-1)*II + depth = 256 cycles = 4.00 cycles/MAC
B: 1 accumulator: ResMII=1 RecMII=2 -> II=2, stage times {'ldx': 0, 'ldh': 0, 'mac': 2}
   64 iters: (iters-1)*II + depth = 129 cycles = 2.02 cycles/MAC
C: 2 accumulators (unroll x2): ResMII=2 RecMII=2 -> II=2, stage times {'ldx0': 0, 'ldh0': 0, 'ldx1': 1, 'ldh1': 1, 'mac0': 2, 'mac1': 3}
   32 iters: (iters-1)*II + depth = 66 cycles = 1.03 cycles/MAC
```

출력에서 볼 것:

- A: 256 cycle(4.00 cycle/MAC). 파이프라이닝해도 그대로 — **명령 수를 줄이는 것(AGU·HW loop)이 먼저**다.
- B: 129 cycle(2.02) — 지연을 겹쳐 3 → 2로 줄었지만 누산 되먹임(RecMII)에 막힌다.
- C: 66 cycle(1.03) — 누산기를 나누자 MAC 유닛이 **매 cycle** 일한다. 이론 한계 1 cycle/MAC에 도달.
- 이 "누산기 여러 개로 나누기"는 CPU에서도 똑같이 쓰는 기법이다(E1의 OoO도 레지스터 의존성 체인은 못 깬다). 다만 float는 덧셈 순서가 바뀌면 결과가 달라지므로 컴파일러가 `-ffast-math` 없이는 이 변환을 하지 않는다. 정수(고정소수점)는 결합법칙이 성립해서(넓은 누산기라면) 자유롭게 바꿀 수 있다 — DSP가 고정소수점을 좋아하는 또 하나의 이유다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg"> <text x="10" y="24" font-size="13">A: RISC식 루프 본문 (반복당 4 cycle)</text> <text x="30" y="48" font-size="12" text-anchor="middle">cycle</text> <text x="102.0" y="48" font-size="12" text-anchor="middle">LD0</text> <text x="166.0" y="48" font-size="12" text-anchor="middle">LD1</text> <text x="230.0" y="48" font-size="12" text-anchor="middle">MAC</text> <text x="294.0" y="48" font-size="12" text-anchor="middle">ALU</text> <text x="30" y="80" font-size="12" text-anchor="middle">0</text> <rect x="73" y="61" width="58" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.22" stroke="#4a7bd0"/>
<text x="102.0" y="80" font-size="12" text-anchor="middle">ldx</text> <rect x="137" y="61" width="58" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.22" stroke="#4a7bd0"/> <text x="166.0" y="80" font-size="12" text-anchor="middle">ldh</text> <rect x="201" y="61" width="58" height="28" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/> <rect x="265" y="61" width="58" height="28" rx="4" fill="#d0564a" fill-opacity="0.22" stroke="#d0564a"/> <text x="294.0" y="80" font-size="12" text-anchor="middle">add px</text> <text x="30" y="114" font-size="12" text-anchor="middle">1</text> <rect x="73" y="95" width="58" height="28" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/>
<rect x="137" y="95" width="58" height="28" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/> <rect x="201" y="95" width="58" height="28" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/> <rect x="265" y="95" width="58" height="28" rx="4" fill="#d0564a" fill-opacity="0.22" stroke="#d0564a"/> <text x="294.0" y="114" font-size="12" text-anchor="middle">add ph</text> <text x="30" y="148" font-size="12" text-anchor="middle">2</text> <rect x="73" y="129" width="58" height="28" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/> <rect x="137" y="129" width="58" height="28" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/>
<rect x="201" y="129" width="58" height="28" rx="4" fill="#e08a3c" fill-opacity="0.22" stroke="#e08a3c"/> <text x="230.0" y="148" font-size="12" text-anchor="middle">mac</text> <rect x="265" y="129" width="58" height="28" rx="4" fill="#d0564a" fill-opacity="0.22" stroke="#d0564a"/> <text x="294.0" y="148" font-size="12" text-anchor="middle">dec</text> <text x="30" y="182" font-size="12" text-anchor="middle">3</text> <rect x="73" y="163" width="58" height="28" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/> <rect x="137" y="163" width="58" height="28" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/>
<rect x="201" y="163" width="58" height="28" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/> <rect x="265" y="163" width="58" height="28" rx="4" fill="#d0564a" fill-opacity="0.22" stroke="#d0564a"/> <text x="294.0" y="182" font-size="12" text-anchor="middle">bnz</text> <text x="340" y="24" font-size="13">C: DSP식 kernel (II=2, MAC 2개)</text> <text x="360" y="48" font-size="12" text-anchor="middle">cycle</text> <text x="432.0" y="48" font-size="12" text-anchor="middle">LD0</text> <text x="496.0" y="48" font-size="12" text-anchor="middle">LD1</text> <text x="560.0" y="48" font-size="12" text-anchor="middle">MAC</text>
<text x="624.0" y="48" font-size="12" text-anchor="middle">ALU</text> <text x="360" y="80" font-size="12" text-anchor="middle">2k</text> <rect x="403" y="61" width="58" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.22" stroke="#4a7bd0"/> <text x="432.0" y="80" font-size="12" text-anchor="middle">ldx0 i+1</text> <rect x="467" y="61" width="58" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.22" stroke="#4a7bd0"/> <text x="496.0" y="80" font-size="12" text-anchor="middle">ldh0 i+1</text> <rect x="531" y="61" width="58" height="28" rx="4" fill="#e08a3c" fill-opacity="0.22" stroke="#e08a3c"/> <text x="560.0" y="80" font-size="12" text-anchor="middle">mac0 i</text>
<rect x="595" y="61" width="58" height="28" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/> <text x="360" y="114" font-size="12" text-anchor="middle">2k+1</text> <rect x="403" y="95" width="58" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.22" stroke="#4a7bd0"/> <text x="432.0" y="114" font-size="12" text-anchor="middle">ldx1 i+1</text> <rect x="467" y="95" width="58" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.22" stroke="#4a7bd0"/> <text x="496.0" y="114" font-size="12" text-anchor="middle">ldh1 i+1</text> <rect x="531" y="95" width="58" height="28" rx="4" fill="#e08a3c" fill-opacity="0.22" stroke="#e08a3c"/>
<text x="560.0" y="114" font-size="12" text-anchor="middle">mac1 i</text> <rect x="595" y="95" width="58" height="28" rx="4" fill="none" stroke="#888" stroke-dasharray="3 3"/> <text x="340" y="160" font-size="12">LD 슬롯 2개와 MAC 슬롯이 매 cycle 가득 참</text> <text x="340" y="180" font-size="12">ALU 슬롯은 비어 있음: post-increment(AGU)와</text> <text x="340" y="198" font-size="12">zero-overhead loop가 포인터·카운터 일을 가져감</text> <text x="10" y="222" font-size="12">빨강 = 오버헤드 op (포인터 add, 카운터, 분기) · 파랑 = load · 주황 = MAC · 점선 = 빈 슬롯(NOP)</text> <text x="10" y="250" font-size="12">A: 7 op / 4 cycle, MAC 효율 25%    C: 6 op / 2 cycle, MAC 효율 100% (1 MAC/cycle)</text>
<text x="10" y="278" font-size="12">i = 반복 번호. C의 kernel은 반복 i의 MAC과 반복 i+1의 load를 같은 bundle에 섞는다 (software pipelining)</text> </svg>
```

그림 6 — A(RISC식 본문)와 C(software pipelined kernel)의 번들. A는 오버헤드 op(빨강)이 ALU 슬롯을 독점하고 MAC 슬롯은 4 cycle에 한 번만 찬다. C의 kernel은 반복 i의 MAC과 반복 i+1의 load를 섞어서 LD·MAC 슬롯을 매 cycle 채운다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg"> <text x="10" y="20" font-size="13">같은 반복 5개: 순차 실행 vs software pipelining (II=2)</text> <text x="163.0" y="40" font-size="12" text-anchor="middle">0</text> <text x="189.0" y="40" font-size="12" text-anchor="middle">1</text> <text x="215.0" y="40" font-size="12" text-anchor="middle">2</text> <text x="241.0" y="40" font-size="12" text-anchor="middle">3</text> <text x="267.0" y="40" font-size="12" text-anchor="middle">4</text> <text x="293.0" y="40" font-size="12" text-anchor="middle">5</text> <text x="319.0" y="40" font-size="12" text-anchor="middle">6</text>
<text x="345.0" y="40" font-size="12" text-anchor="middle">7</text> <text x="371.0" y="40" font-size="12" text-anchor="middle">8</text> <text x="397.0" y="40" font-size="12" text-anchor="middle">9</text> <text x="423.0" y="40" font-size="12" text-anchor="middle">10</text> <text x="449.0" y="40" font-size="12" text-anchor="middle">11</text> <text x="475.0" y="40" font-size="12" text-anchor="middle">12</text> <text x="501.0" y="40" font-size="12" text-anchor="middle">13</text> <text x="527.0" y="40" font-size="12" text-anchor="middle">14</text> <text x="553.0" y="40" font-size="12" text-anchor="middle">15</text>
<text x="579.0" y="40" font-size="12" text-anchor="middle">16</text> <text x="605.0" y="40" font-size="12" text-anchor="middle">17</text> <text x="631.0" y="40" font-size="12" text-anchor="middle">18</text> <text x="10" y="40" font-size="12">cycle</text> <text x="10" y="62" font-size="12">순차 (3 cycle/반복)</text> <rect x="152" y="50" width="22" height="20" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/> <text x="163.0" y="65" font-size="12" text-anchor="middle">L</text> <rect x="204" y="50" width="22" height="20" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/> <text x="215.0" y="65" font-size="12" text-anchor="middle">M</text>
<line x1="174" y1="60" x2="204" y2="60" stroke="#888" stroke-dasharray="2 2"/> <rect x="230" y="50" width="22" height="20" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/> <text x="241.0" y="65" font-size="12" text-anchor="middle">L</text> <rect x="282" y="50" width="22" height="20" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/> <text x="293.0" y="65" font-size="12" text-anchor="middle">M</text> <line x1="252" y1="60" x2="282" y2="60" stroke="#888" stroke-dasharray="2 2"/> <rect x="308" y="50" width="22" height="20" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="319.0" y="65" font-size="12" text-anchor="middle">L</text> <rect x="360" y="50" width="22" height="20" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/> <text x="371.0" y="65" font-size="12" text-anchor="middle">M</text> <line x1="330" y1="60" x2="360" y2="60" stroke="#888" stroke-dasharray="2 2"/> <rect x="386" y="50" width="22" height="20" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/> <text x="397.0" y="65" font-size="12" text-anchor="middle">L</text> <rect x="438" y="50" width="22" height="20" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/> <text x="449.0" y="65" font-size="12" text-anchor="middle">M</text>
<line x1="408" y1="60" x2="438" y2="60" stroke="#888" stroke-dasharray="2 2"/> <rect x="464" y="50" width="22" height="20" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/> <text x="475.0" y="65" font-size="12" text-anchor="middle">L</text> <rect x="516" y="50" width="22" height="20" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/> <text x="527.0" y="65" font-size="12" text-anchor="middle">M</text> <line x1="486" y1="60" x2="516" y2="60" stroke="#888" stroke-dasharray="2 2"/> <text x="10" y="90" font-size="12">→ 5반복 = 15 cycle</text> <text x="10" y="125" font-size="12">pipelined</text>
<rect x="152" y="110" width="22" height="20" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/> <text x="163.0" y="125" font-size="12" text-anchor="middle">L</text> <rect x="204" y="110" width="22" height="20" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/> <text x="215.0" y="125" font-size="12" text-anchor="middle">M</text> <line x1="174" y1="120" x2="204" y2="120" stroke="#888" stroke-dasharray="2 2"/> <text x="132" y="125" font-size="12" text-anchor="middle">i0</text> <rect x="204" y="134" width="22" height="20" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/> <text x="215.0" y="149" font-size="12" text-anchor="middle">L</text>
<rect x="256" y="134" width="22" height="20" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/> <text x="267.0" y="149" font-size="12" text-anchor="middle">M</text> <line x1="226" y1="144" x2="256" y2="144" stroke="#888" stroke-dasharray="2 2"/> <text x="132" y="149" font-size="12" text-anchor="middle">i1</text> <rect x="256" y="158" width="22" height="20" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/> <text x="267.0" y="173" font-size="12" text-anchor="middle">L</text> <rect x="308" y="158" width="22" height="20" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/> <text x="319.0" y="173" font-size="12" text-anchor="middle">M</text>
<line x1="278" y1="168" x2="308" y2="168" stroke="#888" stroke-dasharray="2 2"/> <text x="132" y="173" font-size="12" text-anchor="middle">i2</text> <rect x="308" y="182" width="22" height="20" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/> <text x="319.0" y="197" font-size="12" text-anchor="middle">L</text> <rect x="360" y="182" width="22" height="20" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/> <text x="371.0" y="197" font-size="12" text-anchor="middle">M</text> <line x1="330" y1="192" x2="360" y2="192" stroke="#888" stroke-dasharray="2 2"/> <text x="132" y="197" font-size="12" text-anchor="middle">i3</text>
<rect x="360" y="206" width="22" height="20" rx="4" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/> <text x="371.0" y="221" font-size="12" text-anchor="middle">L</text> <rect x="412" y="206" width="22" height="20" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/> <text x="423.0" y="221" font-size="12" text-anchor="middle">M</text> <line x1="382" y1="216" x2="412" y2="216" stroke="#888" stroke-dasharray="2 2"/> <text x="132" y="221" font-size="12" text-anchor="middle">i4</text> <text x="10" y="143" font-size="12">→ (5−1)·2+3 = 11</text> <line x1="150" y1="236" x2="202" y2="236" stroke="#3f9a6b" stroke-width="3"/>
<text x="176.0" y="254" font-size="12" text-anchor="middle">prologue</text> <line x1="202" y1="236" x2="410" y2="236" stroke="#e08a3c" stroke-width="3"/> <text x="306.0" y="254" font-size="12" text-anchor="middle">kernel (L와 M이 겹침)</text> <line x1="410" y1="236" x2="436" y2="236" stroke="#3f9a6b" stroke-width="3"/> <text x="423.0" y="254" font-size="12" text-anchor="middle">epilogue</text> <text x="10" y="262" font-size="12">L = 두 load (한 bundle), M = MAC. load 지연 2 cycle을 다음 반복의 load로 채운다.</text> </svg>
```

그림 7 — B 경우를 시간축으로. 순차 실행은 반복당 3 cycle(5반복 15 cycle), 파이프라이닝은 2 cycle마다 새 반복을 시작해 11 cycle. 처음 2 cycle(prologue)과 마지막(epilogue)은 파이프가 차고 비는 구간이다. 루프가 짧으면(반복 수 적으면) prologue·epilogue 비중이 커져 이득이 줄어든다.

### 4.4 실제 VLIW DSP에서 달라지는 점

- **슬롯 제약**: 실제 기계는 "슬롯 0·1만 load, 슬롯 2·3만 곱셈"처럼 슬롯마다 가능한 명령이 다르다. Hexagon은 한 packet에 최대 4개 명령을 넣고 슬롯별 제약이 있다(ISA 매뉴얼의 slot 표 참고).
- **Xtensa FLIX**: Flexible Length Instruction Xtensions. Xtensa는 24/16비트 기본 명령에 더해, 여러 연산을 넓은 번들(예: 64비트급) 하나에 담는 FLIX 포맷을 구성할 수 있다. HiFi·Vision DSP가 VLIW처럼 동작하는 방식이 이것이다. Don이 본 SSD용 Xtensa에 FLIX가 켜져 있었는지는 그 코어 설정에 따라 다르다.
- **예측 불가 지연**: 캐시 미스가 나면 VLIW는 전체가 stall한다. 그래서 핫 루프의 데이터는 TCM/VTCM에 DMA로 미리 가져다 둔다(8.4절).
- **레지스터 압박**: software pipelining은 여러 반복이 동시에 살아 있어서 레지스터를 많이 쓴다. 레지스터가 모자라면 spill이 생겨 II가 늘어난다.
- **컴파일러 리포트 읽기**: 벤더 컴파일러는 보통 루프별 "software pipelined, II = N" 같은 정보를 어셈블리 주석이나 리포트로 준다(도구마다 옵션이 다르다). 성능 튜닝의 첫 단계는 핫 루프의 II와 MII를 비교하는 것이다. II > MII면 원인(aliasing, 레지스터 부족, 호출, 분기)을 찾는다.

### 4.5 함정

- "VLIW라서 자동으로 4배 빠르다" → 번들이 비면 NOP이다. 제어 코드에선 IPC가 1 근처로 떨어진다.
- 포인터 aliasing을 알려 주지 않는다(`restrict` 누락) → 컴파일러가 load를 store 앞으로 당기지 못해 파이프라이닝 실패.
- 누산기 하나로 긴 reduction → RecMII에 묶인다. 누산기를 MAC 지연 × 유닛 수만큼 나눈다.

---

## 5. Cadence Tensilica — Xtensa, TIE, HiFi, Vision

### 5.1 Xtensa: "설정 가능한" 프로세서

Tensilica는 Xtensa라는 **configurable processor** IP를 만든 회사이고, 2013년 Cadence에 인수되었다. Xtensa의 핵심은 칩 설계자가 코어를 **설정**해서 뽑는다는 것이다.

- 옵션 켜고 끄기: MAC16, 곱셈기, FPU, zero-overhead loop, windowed register, 캐시·TCM 크기, 인터럽트 수, 버스 인터페이스 등
- **TIE (Tensilica Instruction Extension)** 언어: 설계자가 **자기만의 명령·레지스터 파일·상태**를 Verilog 비슷한 언어로 정의하면, 하드웨어와 함께 컴파일러·어셈블러·시뮬레이터·디버거가 그 명령을 알게 된다. C에서는 intrinsic 함수로 부른다.
- **FLIX**: 여러 연산을 한 번들에 넣는 VLIW 포맷(4.4절)
- 툴: Xtensa Xplorer IDE, XCC 컴파일러 계열, ISS(명령어 수준 시뮬레이터), 설정별로 생성되는 "core package"

Don 연결 — 가장 중요한 부분:

- SSD 컨트롤러가 Xtensa를 쓰는 이유는 흔히 "데이터 경로에 맞춘 커스텀 명령(TIE)과 로컬 메모리"를 붙일 수 있어서다. 예를 들어 CRC·ECC 보조 연산, 비트 조작, 큐 조작 같은 것을 명령 하나로 만들 수 있다(Don이 쓴 코어에 실제 어떤 확장이 있었는지는 Don의 기억이 기준이다).
- HiFi는 **같은 방식으로 오디오용 확장을 Cadence가 미리 만들어 둔 제품**이다. 즉 "SSD용 Xtensa + 데이터 경로 TIE" ↔ "Xtensa + 오디오 SIMD/MAC TIE(= HiFi)". 툴체인·디버깅(OCD/JTAG, trace)·링커 스크립트·메모리 맵·zero-overhead loop·windowed ABI 경험이 그대로 옮겨진다.
- 면접 문장으로: "I've shipped production firmware on Xtensa cores, so I know the toolchain, the configurable-core model and TIE-style custom instructions. HiFi and Vision DSPs are the same Xtensa base with Cadence's audio and vision ISA extensions, so the ramp-up is mostly the SIMD intrinsics and the NN library, not the platform."

### 5.2 HiFi 오디오 DSP 계열

확실히 말할 수 있는 수준으로 정리한다(세부 스펙은 Cadence 공식 자료로 확인).

| 항목 | 내용 |
|---|---|
| 정체 | Xtensa 기반 오디오/음성 DSP IP. HiFi 2, HiFi 3(3z), HiFi 4, HiFi 5 등 세대가 있고 저전력 HiFi 1도 있다 |
| 주 용도 | 오디오 코덱 디코드·인코드, 음성 전처리(잡음 제거, 에코 제거, beamforming), wake word, 후처리 |
| 연산 | 고정소수점 SIMD MAC(16/24/32비트 계열), 포화·반올림, 세대에 따라 부동소수점(단정밀도 등) 옵션 |
| NN | 최근 세대(HiFi 5 등)는 신경망 연산(특히 8비트 계열) 성능 강화를 강조한다 — 정확한 MAC/cycle은 설정별로 다르니 데이터시트 확인 |
| 소프트웨어 | Cadence의 DSP 라이브러리(NatureDSP로 알려진 것), HiFi용 NN 라이브러리(`foss-xtensa` GitHub의 `nnlib-hifi4`/`nnlib-hifi5`), TFLite Micro의 Xtensa 최적화 커널, Sound Open Firmware(SOF) |
| 어디서 보이나 | 많은 SoC의 오디오 서브시스템·always-on 음성 블록. 예: NXP i.MX RT600(HiFi 4), i.MX 8M 일부, Intel 오디오 DSP(SOF가 지원), 여러 모바일·스마트 스피커·이어버드 SoC(칩마다 확인) |

HiFi 코드의 모양은 대략 이렇다. **예시 — 컴파일하지 않음.** 실제 intrinsic 이름(`AE_…`)과 타입은 HiFi 세대·설정마다 다르므로 여기서는 의사코드로 쓴다.

```
/* 의사코드: HiFi류 2-way SIMD Q15 FIR 내부 루프의 모양 (예시 — 컴파일하지 않음) */
acc0 = acc1 = 0                              /* 넓은 누산 레지스터 2개 */
loop T/4 times:                              /* zero-overhead loop */
    x01 = load_2xQ15_circ(px)                /* 원형 버퍼에서 2개, 포인터 자동 wrap */
    h01 = load_2xQ15_postinc(ph)             /* 계수 2개, post-increment */
    acc0, acc1 += mul_2x16(x01, h01)          /* 한 명령에 2 MAC (VLIW 번들 안에서 load와 동시) */
    ... 같은 것 한 번 더 (unroll x2, 4.3절의 누산기 나누기)
y = round_sat_to_Q15(acc0 + acc1)             /* 마지막에 한 번 반올림·포화 */
```

말로 하면: 2~3절에서 C로 본 것(포화 곱, 넓은 누산, circular load, post-increment, 누산기 2개, 마지막 한 번 반올림)이 각각 **명령 하나**가 되고, 그 명령들이 FLIX 번들 하나에 같이 실린다.

### 5.3 Vision DSP 계열

Cadence는 Xtensa 기반 비전/이미징 DSP도 판다. Vision P6, Vision Q7, Vision Q8 같은 이름이 널리 알려져 있고(이후 새 이름의 제품도 나왔다), 넓은 SIMD(수백~천 비트급)와 VLIW로 이미지 필터·특징 추출·CNN 일부를 돌린다. 카메라 ISP 뒤의 전처리, 얼굴 검출 같은 작은 비전 모델에 쓰인다. 세부 폭·MAC 수는 제품마다 다르므로 면접에선 "wide-SIMD VLIW vision DSP on Xtensa" 수준으로 말하는 게 안전하다. Cadence는 이와 별도로 NPU IP 라인도 갖고 있다(E5에서 NPU 일반론).

### 5.4 Don이 새로 익힐 것

- HiFi SIMD 타입·intrinsic 체계(레지스터 파일이 따로 있다: TIE로 추가된 오디오 레지스터)
- 정렬되지 않은 load를 위한 "align register" 류 패턴(넓은 SIMD DSP의 공통 패턴)
- NN 라이브러리의 데이터 레이아웃 요구(채널 순서, 패딩, 정렬)
- 그 외 — 툴체인, 링커, 메모리 맵, 디버거, 인터럽트 — 는 이미 아는 것이다.

---

## 6. Qualcomm Hexagon — scalar, HVX, HMX, VTCM, QNN HTP

Hark 기기가 Qualcomm SoC를 쓴다면(추정), 모델이 도는 곳은 거의 확실히 Hexagon 계열이다. 공개적으로 잘 문서화된 사실 위주로 정리하고, 불확실한 것은 표시한다.

### 6.1 Scalar 코어: VLIW + 하드웨어 스레드

- Hexagon(옛 이름 QDSP6)은 Qualcomm이 자체 설계한 DSP 아키텍처다. 명령은 **packet**으로 묶이고, 한 packet에 **최대 4개 명령**이 들어가 한 번에 발행된다(4절의 4-slot 기계와 같은 모양). 슬롯마다 가능한 명령 종류에 제약이 있다.
- 하나의 코어가 **여러 하드웨어 스레드**를 번갈아 실행한다(스레드 수는 세대마다 다르다). 한 스레드가 메모리를 기다리는 동안 다른 스레드가 실행되어 지연을 숨긴다 — OoO 대신 멀티스레딩으로 지연을 감추는 설계다.
- 스칼라 명령에도 DSP 기능이 있다: 포화·반올림 곱셈, 원형(`:circ`)·비트역순(`:brev`) 주소 모드, 하드웨어 루프(`loop0`/`loop1`).
- 한 SoC 안에 Hexagon이 **여러 인스턴스**로 들어간다. 흔히 부르는 이름: aDSP(오디오, LPASS 안), cDSP(compute, HVX·HTP가 붙은 것), mDSP(모뎀), sDSP/SLPI(센서, always-on). Linux 커널에서 이들은 remoteproc(`qcom_q6v5_pas` 등)로 부팅되고, Android에서 CPU는 **FastRPC**로 DSP 함수를 호출한다. 어느 칩에 어떤 인스턴스가 있는지는 칩마다 다르다.

### 6.2 HVX — Hexagon Vector eXtensions

- **1024비트(128바이트) 벡터 레지스터** 32개. int8이면 한 레지스터에 128개, int16이면 64개, int32면 32개. (초기 세대는 64바이트 모드도 있었다.)
- 스칼라 코어의 스레드가 HVX "컨텍스트"를 잡아서 벡터 명령을 발행한다. 스칼라 쪽이 주소·루프·제어, HVX가 데이터를 맡는 분업.
- 용도: 카메라·이미지 처리(Halide가 HVX 백엔드를 지원), 오디오, 그리고 **HMX가 없던 세대의 NN 추론**(옛 hexagon_nn, QNN의 DSP 백엔드).
- 세대: HVX는 대략 Hexagon V60(Snapdragon 820 무렵)에 처음 등장했다고 널리 알려져 있다.

HVX intrinsic의 모양. **예시 — 컴파일하지 않음.** Hexagon SDK의 헤더에 `HVX_Vector` 타입과 `Q6_<결과타입>_<연산>_<입력타입>` 형식의 intrinsic이 있다. 아래에서 `Q6_Vw_vadd_VwVw`(int32 레인 32개 덧셈)만 실제 이름이고 나머지는 설명용 의사코드다.

```c
/* 예시 — 컴파일하지 않음 (Hexagon SDK + HVX 128B 모드 가정) */
#include <hexagon_types.h>
#include <hvx_hexagon_protos.h>   /* 헤더 이름은 SDK/툴체인 버전에서 확인 */

void add_i32(const HVX_Vector *a, const HVX_Vector *b, HVX_Vector *out, int nvec) {
    for (int i = 0; i < nvec; i++)            /* 한 반복 = int32 32개 */
        out[i] = Q6_Vw_vadd_VwVw(a[i], b[i]); /* 128바이트를 한 명령으로 */
}
/* 의사코드: int8 dot product 누산은 "4개씩 곱해 int32 레인에 더하는" 류의
   벡터 곱-누산 명령으로 한다. 정확한 명령 이름은 HVX 프로그래머 가이드 확인. */
```

### 6.3 HMX — 행렬 확장

- 최근 세대 Hexagon에는 NN의 행렬 곱(conv, GEMM)을 전담하는 **HMX(Hexagon Matrix eXtensions)** 가 붙는다. E5에서 보는 MAC 배열(systolic 류)에 가까운 역할이다.
- HMX의 세부(배열 크기, 지원 정밀도, 명령 인터페이스)는 공개 문서가 제한적이다. 일반 개발자는 직접 프로그래밍하지 않고 **QNN HTP 백엔드(컴파일러·런타임)** 를 통해 쓴다.
- 안전한 말하기: "HVX handles vector work and ops the matrix engine doesn't cover; HMX handles the bulk of conv and matmul through Qualcomm's compiler."

### 6.4 VTCM — 벡터용 TCM

- **VTCM (Vector Tightly Coupled Memory)**: HVX(와 HMX)가 가장 빠르게 접근하는 on-chip SRAM scratchpad. 캐시가 아니라 소프트웨어가 명시적으로 잡아서(할당·예약 API) 쓰고, DMA로 채운다. 크기는 세대·칩마다 다르다(수백 KB ~ 수 MB 규모로 알려짐, 확인 필요).
- 최적화의 핵심은 D3의 roofline과 같다: 타일을 VTCM에 올리고, 계산하는 동안 다음 타일을 DMA로 가져온다(8.4절). QNN HTP 컴파일러가 이 타일링과 VTCM 배치를 대신 한다.
- Don 연결: Cortex-R의 TCM과 SSD 컨트롤러의 SRAM 버퍼를 DMA descriptor로 관리하던 것과 같은 일이다. 차이는 크기(수 MB)와 폭(1024비트 load).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"> <defs><marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <text x="10" y="20" font-size="13">Hexagon 계열 개념도 — 세대·칩마다 구성이 다르다 (공개 자료 수준의 단순화)</text> <rect x="10" y="40" width="200" height="150" rx="6" fill="#888" fill-opacity="0.18" stroke="#888"/> <text x="110" y="62" font-size="12" text-anchor="middle">Scalar VLIW 코어</text> <text x="110" y="84" font-size="12" text-anchor="middle">packet = 최대 4 명령</text> <rect x="25" y="100" width="38" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/>
<text x="44" y="119" font-size="12" text-anchor="middle">T0</text> <rect x="70" y="100" width="38" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/> <text x="89" y="119" font-size="12" text-anchor="middle">T1</text> <rect x="115" y="100" width="38" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/> <text x="134" y="119" font-size="12" text-anchor="middle">T2</text> <rect x="160" y="100" width="38" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/> <text x="179" y="119" font-size="12" text-anchor="middle">T3</text> <text x="110" y="148" font-size="12" text-anchor="middle">하드웨어 스레드</text>
<text x="110" y="168" font-size="12" text-anchor="middle">(개수는 세대별 상이)</text> <rect x="240" y="40" width="200" height="100" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/> <text x="340" y="62" font-size="12" text-anchor="middle">HVX</text> <text x="340" y="84" font-size="12" text-anchor="middle">1024-bit (128 B) 벡터</text> <text x="340" y="104" font-size="12" text-anchor="middle">int8/int16/int32 SIMD</text> <text x="340" y="124" font-size="12" text-anchor="middle">오디오 · 이미지 · 작은 NN</text> <rect x="470" y="40" width="200" height="100" rx="6" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/>
<text x="570" y="62" font-size="12" text-anchor="middle">HMX (행렬 확장)</text> <text x="570" y="84" font-size="12" text-anchor="middle">NN용 행렬 곱 엔진</text> <text x="570" y="104" font-size="12" text-anchor="middle">QNN HTP가 주로 사용</text> <text x="570" y="124" font-size="12" text-anchor="middle">(세부 비공개가 많음)</text> <rect x="240" y="160" width="430" height="50" rx="6" fill="#d0564a" fill-opacity="0.18" stroke="#d0564a"/> <text x="455" y="182" font-size="12" text-anchor="middle">VTCM — 벡터용 tightly-coupled 메모리 (소프트웨어가 관리하는 scratchpad)</text> <text x="455" y="200" font-size="12" text-anchor="middle">HVX/HMX가 가장 빠르게 읽는 곳. DMA로 채운다.</text>
<rect x="10" y="230" width="660" height="40" rx="6" fill="#888" fill-opacity="0.1" stroke="#888"/> <text x="340" y="255" font-size="12" text-anchor="middle">L2 cache / 메모리 계층 → DDR (CPU·GPU와 공유)</text> <line x1="210" y1="90" x2="238" y2="90" stroke="currentColor" marker-end="url(#ar)"/> <line x1="440" y1="90" x2="468" y2="90" stroke="currentColor" marker-end="url(#ar)"/> <line x1="340" y1="140" x2="340" y2="158" stroke="currentColor" marker-end="url(#ar)"/> <line x1="570" y1="140" x2="570" y2="158" stroke="currentColor" marker-end="url(#ar)"/> <line x1="455" y1="210" x2="455" y2="228" stroke="currentColor" marker-end="url(#ar)"/>
<text x="10" y="296" font-size="12">스칼라 코어가 제어와 주소 계산을, HVX가 벡터 연산을, HMX가 행렬 연산을 맡는다.</text> <text x="10" y="316" font-size="12">HMX 세부(크기·정밀도)는 공개 문서가 적다. 면접에서는 역할 수준으로만 말하는 게 안전하다.</text> </svg>
```

그림 8 — Hexagon 계열 개념도. 스칼라 VLIW 코어의 하드웨어 스레드가 HVX·HMX에 일을 주고, 둘은 VTCM에서 데이터를 읽는다. 세대·칩마다 구성이 달라서 개념 수준의 그림이다.

### 6.5 소프트웨어 스택 — 무엇을 쓰나

| 층 | 이름 | 무엇 |
|---|---|---|
| 저수준 SDK | Hexagon SDK | 컴파일러(LLVM 기반), 시뮬레이터, FastRPC, HVX intrinsic 헤더, 예제·라이브러리. DSP에 직접 C/C++ 코드를 올릴 때 |
| NN 런타임 | QNN (Qualcomm AI Engine Direct) | 백엔드: CPU, GPU, **HTP**(Hexagon Tensor Processor = HVX+HMX가 있는 NPU 쪽), 옛 DSP 백엔드. 모델을 컨텍스트 바이너리로 컴파일 (F4) |
| 상위 | TFLite/LiteRT delegate, ONNX Runtime QNN EP, Qualcomm AI Hub | 프레임워크 모델을 QNN으로 넘기는 경로 (F4, PJ4) |
| 마케팅 이름 | Qualcomm AI Engine, Hexagon NPU | "AI Engine"은 CPU·GPU·Hexagon을 묶어 부르는 이름으로 쓰여 왔고, 최근 Snapdragon에서는 Hexagon을 NPU라고 부른다 |

말로 하면: 신호 처리 코드를 직접 쓰려면 Hexagon SDK, 신경망을 올리려면 QNN HTP. 면접에서 "HTP"와 "Hexagon DSP"를 같은 것처럼 섞어 말하면 헷갈린다 — HTP는 NN용으로 HMX 등이 붙은 Hexagon 인스턴스를 가리키는 QNN의 백엔드 이름이다.

### 6.6 무엇이 어디서 도나 (일반론, 칩마다 확인)

| 작업 | 흔한 위치 | 이유 |
|---|---|---|
| 마이크 PDM → PCM, 데시메이션 필터 | 오디오 HW 블록 또는 aDSP | 항상 켜짐, 고정 기능 |
| VAD, 1단 wake word | always-on 저전력 블록(센서/오디오 섬, 전용 저전력 코어) | mW 이하 예산 |
| FFT·mel·AEC·beamforming·noise suppression | aDSP / HiFi | 고정소수점 신호 처리, 원형 버퍼, 실시간 |
| 2단 wake word, 작은 NN | aDSP HVX 또는 HTP | 크기·전력 trade-off |
| ASR encoder, vision, SLM | HTP (NPU) | 대량 GEMM, int8/int4 |
| 제어, 토크나이저, 디코딩 루프 일부 | CPU | 분기 많은 코드 |

Hark 같은 기기라면 "귀에서 항상 듣는 부분은 저전력 섬, 깨어난 뒤 무거운 모델은 HTP"라는 분업을 가정하고 질문하면 된다(E8에서 IPC와 전원 도메인까지).

---

## 7. CEVA와 그 밖의 DSP (짧게)

| 계열 | 특징 | 어디서 |
|---|---|---|
| Ceva (구 CEVA) | 라이선스 DSP IP 회사. 오디오·음성 DSP(TeakLite, BX 계열), 비전 DSP(XM 계열), 센서 허브 DSP(SensPro), NPU(NeuPro 계열) | 이어버드·TWS, 스마트폰 모뎀, IoT SoC (제품 라인업은 공식 사이트 확인) |
| TI C6000 | 고전 VLIW DSP. 8개 기능 유닛(.L .S .M .D × 2 datapath) | 통신·산업·오디오. VLIW 교과서 예제 |
| TI C54x/C55x | 16비트 고정소수점, 40비트 누산기 | 저전력 음성 |
| ADI SHARC / Blackfin | SHARC는 부동소수점 오디오 DSP, Blackfin은 40비트 누산기 2개 | 프로 오디오, 자동차 오디오 |
| Arm Cortex-M DSP 확장 | `SMLAD`, `QADD`, `SSAT` 등 SIMD-in-register, Helium(M55/M85) | MCU에서 DSP 역할 (E2) |
| RISC-V + 벡터/커스텀 | RVV, 벤더 커스텀 명령 (E3) | 새로 떠오르는 대안 |

면접에서 중요한 건 브랜드가 아니라 공통 패턴: **MAC 폭, 누산 폭, 주소 모드, 메모리 구조, 툴체인 품질, NN 라이브러리 유무**. 새 DSP를 평가할 때 이 여섯 가지를 물으면 된다.

---

## 8. 프로그래밍 모델 — intrinsic, 자동 벡터화, 라이브러리, 메모리

### 8.1 세 가지 층

1. **Plain C + 벤더 컴파일러**: 가장 이식성이 높다. software pipelining·기본 벡터화는 컴파일러가 해 준다. 하지만 아래 이유로 보통 이론치의 일부만 나온다.
2. **Intrinsic**: C 함수처럼 생긴 한 줄이 명령 하나에 대응한다(`Q6_Vw_vadd_VwVw`, CMSIS의 `__SMLAD`, HiFi의 `AE_…`). 레지스터 할당과 스케줄은 여전히 컴파일러가 한다. 실무의 주력.
3. **어셈블리**: 최후의 수단. 최근엔 드물고, 벤더 라이브러리 안에 있다.

### 8.2 자동 벡터화가 막히는 이유

| 막는 요인 | 예 | 해결 |
|---|---|---|
| 포인터 aliasing | `out`과 `in`이 겹칠 수 있음 | `restrict`, 별도 버퍼 |
| 포화·반올림 의미 | C에 "포화 덧셈" 연산자가 없음 | intrinsic 또는 컴파일러가 인식하는 관용구 |
| wrap 있는 주소 | `%`, 조건 분기 (3.3절) | 미러 버퍼, 블록 처리, HW circular intrinsic |
| float reduction 순서 | `sum += a[i]·b[i]` | `-ffast-math` 류 또는 정수 |
| 모르는 반복 수·정렬 | 꼬리 처리 | 블록 크기를 SIMD 폭의 배수로 설계 |
| 데이터 의존 인덱스 | LUT, gather | 벡터 LUT 명령, 재배치 |
| 함수 호출·분기 | 루프 안 `if` | 조건 없는 형태(select, mask)로 |

말로 하면: 컴파일러는 "안전하다고 증명할 수 있는 것"만 벡터화한다. DSP 코드의 반은 컴파일러에게 안전함을 알려 주는 일이고, 나머지 반은 C로 표현이 안 되는 연산(포화, 원형 주소)을 intrinsic으로 쓰는 일이다.

### 8.3 벤더 NN·DSP 라이브러리

- Cortex-M: CMSIS-DSP, CMSIS-NN (C1 11.2, E2)
- HiFi: Cadence DSP 라이브러리, `nnlib-hifi4`/`nnlib-hifi5`, TFLite Micro Xtensa 커널
- Hexagon: Hexagon SDK 라이브러리, QNN HTP(NN은 사실상 여기)
- 원칙: **라이브러리가 있는 op 모양으로 모델을 맞춘다**(C7 HW-aware 설계). 라이브러리에 없는 op는 CPU로 fallback되고, 그 한 op이 전체 지연을 지배하는 일이 흔하다(F4 fallback 목록).

### 8.4 메모리: TCM/VTCM과 DMA double buffering (B5와 연결)

DSP의 성능 모델은 결국 D3 roofline이다. MAC이 아무리 넓어도 데이터가 제때 안 오면 쉰다. 그래서:

```
시간 →      ┌──────────┬──────────┬──────────┬──────────┐
DMA        │ load T0  │ load T1  │ load T2  │ store... │     (DDR → TCM/VTCM)
compute    │          │ comp T0  │ comp T1  │ comp T2  │     (버퍼 A/B 번갈아)
버퍼       │   A←     │ A:계산 B← │ B:계산 A← │ A:계산   │
```

- 타일 크기 = TCM 용량의 절반(ping/pong) 이하. 계산 시간 ≥ DMA 시간이면 DMA가 완전히 숨는다(compute-bound).
- 동기화: DMA 완료 인터럽트/폴링 → 계산 시작. SSD에서 NAND DMA 완료와 ECC 엔진을 겹치던 것과 같은 모양이다. Don이 가장 강한 부분이니 면접에서 적극적으로 연결한다.
- 흔한 버그: 캐시가 있는 경로에서 DMA 버퍼의 cache clean/invalidate 누락, 두 버퍼 역할 뒤바뀜, 마지막 타일 크기 처리, DMA descriptor의 정렬 요구.

---

## 9. DSP vs NPU vs CPU — ML 파이프라인의 어느 층을 어디에

### 9.1 front-end와 NN의 연산량 비교

KWS 파이프라인(16 kHz, 25 ms 창, 10 ms hop, 512-FFT, 40 mel, 13 MFCC)의 front-end 연산량과 B5 3.2절 DS-CNN(2.66 M MAC, 100 ms마다 실행)을 비교한다. 연산 기계별로 필요한 clock을 이상적인 MAC/cycle로 나눠 본다(장난감 숫자, 메모리·오버헤드 무시).

```python
import math
fs, win, nfft, hop_ms, n_mel, n_mfcc = 16000, 400, 512, 10, 40, 13
frames_per_s = 1000 // hop_ms
fe = {
    "window (400 mul)": win,
    "FFT 512 complex (N/2*log2N bfly x 4 mul)": (nfft // 2) * int(math.log2(nfft)) * 4,
    "power |X|^2 (257 x 2)": (nfft // 2 + 1) * 2,
    "mel filterbank (<= 2 per bin)": (nfft // 2 + 1) * 2,
    "DCT -> MFCC (13 x 40)": n_mfcc * n_mel,
}
per_frame = sum(fe.values())
for k, v in fe.items():
    print(f"  {k:42s} {v:6d} MAC")
fe_rate = per_frame * frames_per_s
kws_rate = 2.66e6 * 10                       # DS-CNN (B5 3.2): 2.66 M MAC, run every 100 ms
print(f"front-end : {per_frame} MAC/frame x {frames_per_s} frame/s = {fe_rate / 1e6:.2f} M MAC/s")
print(f"KWS DS-CNN: {kws_rate / 1e6:.1f} M MAC/s  (NN / front-end = {kws_rate / fe_rate:.0f}x)")
for name, mac_per_cycle in [("scalar MCU, 1 MAC/cyc", 1), ("dual-16b MAC MCU, 2", 2),
                            ("DSP 4-way SIMD MAC, 4 (toy)", 4), ("wide vector DSP, 64 (toy)", 64)]:
    need = (fe_rate + kws_rate) / mac_per_cycle / 1e6
    print(f"  {name:28s} -> ~{need:6.2f} MHz of ideal cycles")
```

```text
  window (400 mul)                              400 MAC
  FFT 512 complex (N/2*log2N bfly x 4 mul)     9216 MAC
  power |X|^2 (257 x 2)                         514 MAC
  mel filterbank (<= 2 per bin)                 514 MAC
  DCT -> MFCC (13 x 40)                         520 MAC
front-end : 11164 MAC/frame x 100 frame/s = 1.12 M MAC/s
KWS DS-CNN: 26.6 M MAC/s  (NN / front-end = 24x)
  scalar MCU, 1 MAC/cyc        -> ~ 27.72 MHz of ideal cycles
  dual-16b MAC MCU, 2          -> ~ 13.86 MHz of ideal cycles
  DSP 4-way SIMD MAC, 4 (toy)  -> ~  6.93 MHz of ideal cycles
  wide vector DSP, 64 (toy)    -> ~  0.43 MHz of ideal cycles
```

출력에서 볼 것:

- front-end는 프레임당 약 1.1만 MAC, 초당 1.12 M MAC — NN(26.6 M MAC/s)의 약 1/24이다. FFT가 front-end의 80% 이상이다.
- 연산량만 보면 NN이 압도적이다. 그런데 front-end는 **10 ms마다 끊김 없이** 돌아야 하고(실시간), 로그 스펙트럼의 큰 동적 범위 때문에 Q15/Q31·BFP 정밀도 관리가 필요하고, FFT의 bit-reversed 주소와 butterfly 패턴이 필요하다. 이건 DSP의 전공이고, 행렬 곱에 특화된 NPU는 보통 FFT를 잘 못 하거나 지원하지 않는다(NPU 지원 op 목록 확인).
- 1 MAC/cycle MCU면 약 28 MHz, 4-way SIMD DSP면 약 7 MHz, 넓은 벡터(64 MAC/cycle)면 1 MHz 미만. 넓은 벡터일수록 "잠깐 깨서 빨리 끝내고 잔다"(race-to-idle, D7)가 쉬워진다.

### 9.2 어디에 무엇을 — 결정 표

| 층·작업 | CPU | DSP (HiFi, Hexagon aDSP/HVX) | NPU (HTP 등) |
|---|---|---|---|
| 필터, 리샘플링, AEC, beamforming | 가능하나 비효율 | **최적** | 보통 미지원 |
| FFT, mel, MFCC | 가능 | **최적** | 대개 미지원·비효율 |
| 작은 NN (KWS, VAD, 수십 K 파라미터) | MCU급으로 충분 | **좋음** (NPU를 깨우지 않음) | 가능하나 깨우기 비용 |
| conv/GEMM 대량 (ASR, vision, SLM) | 느림 | 가능(HVX), 효율 중간 | **최적** |
| 비표준 op, 동적 shape, 후처리 | **최적** | 가능 | fallback 대상 |
| 제어, 스케줄, 디코딩 루프 | **최적** | 비효율 | 불가 |

### 9.3 에너지 직관 (D7과 연결)

- 연산 하나의 에너지는 "산술 자체 + 명령 fetch/decode/제어 + 데이터 이동"이다(D7 2절). CPU는 명령당 제어 오버헤드(fetch, decode, 분기 예측, OoO 재배열)가 산술보다 훨씬 크다.
- DSP는 VLIW 번들 하나·SIMD 명령 하나로 여러 연산을 하므로 **제어 오버헤드를 여러 MAC에 나눠 낸다**. 그리고 TCM을 쓰니 캐시 태그 검사·미스가 적다.
- NPU는 한 발 더 나가 명령 대신 **데이터 흐름으로** MAC 배열을 돌리고, 한 번 읽은 weight·activation을 배열 안에서 여러 번 재사용한다(E5 dataflow). 대량 GEMM에서 가장 효율적이다.
- 하지만 NPU를 깨우고 모델을 올리고 DMA를 설정하는 **고정 비용**이 있다. 10 ms마다 몇 만 MAC짜리 일을 하려고 NPU를 깨우면 고정 비용이 계산 비용보다 클 수 있다. 그래서 "작고 자주 = DSP/MCU, 크고 가끔 = NPU"가 경험칙이다. 정확한 교차점은 칩마다 측정해야 한다(D7 8절의 측정 방법).

### 9.4 NPU가 꺼져 있을 때 — DSP에 작은 NN 올리기

always-on 구간에서는 NPU 전원 도메인이 꺼져 있는 게 보통이다(E8, E9). 이때 KWS·VAD·착용 감지 같은 작은 모델은 DSP나 MCU에서 돈다. 설계 포인트:

- int8 weight, int16 또는 int8 activation(C1 6.5절 "16x8")
- 라이브러리(nnlib, CMSIS-NN, TFLM 커널)가 지원하는 op로만 구성 — depthwise conv, pointwise conv, dense, 평균 pooling, 간단한 activation
- 상태 버퍼(3.4절)를 원형으로 두는 스트리밍 모델
- 결과가 양성일 때만 NPU·AP를 깨움(cascade, B5 8절)

---

## 10. 임베디드 관점에서 다시 보기

새 DSP를 받았을 때(또는 벤더와 첫 미팅에서) 확인할 목록이다. Don이 SSD·RF 칩 bring-up에서 쓰던 체크리스트 감각 그대로다.

| 항목 | 물어볼 것 | 왜 |
|---|---|---|
| MAC 폭 | int8/int16/int32 MAC per cycle, float 지원 | 이론 처리량 |
| 누산 폭 | 누산 레지스터 비트 수, guard bit, 포화 모드 | 긴 dot product 정확도 (2.3절) |
| 주소 모드 | post-increment, circular, bit-reversed, 비정렬 load | FIR·FFT 효율 (3절) |
| 루프 | zero-overhead loop 중첩 수 | 짧은 루프 오버헤드 |
| VLIW·SIMD | 슬롯 수, 슬롯 제약, SIMD 폭 | 스케줄 한계 (4절) |
| 메모리 | TCM/VTCM 크기, bank 수, DMA 채널, 캐시 | 타일 크기, double buffering (8.4절) |
| 툴체인 | 컴파일러 버전, II 리포트, 프로파일러, ISS, 디버거 | 튜닝 가능성 |
| 라이브러리 | DSP 라이브러리, NN 라이브러리, TFLM/QNN 지원 op | 개발 기간 |
| 전원 | 전원 도메인, retention, 깨우기 지연 | always-on 설계 (E8, E9) |
| IPC | AP ↔ DSP 호출 방식(FastRPC, mailbox, shared memory) | 시스템 통합 (E8) |

C로 확인하는 습관: 2~3절처럼 **호스트에서 bit-exact 고정소수점 모델**을 먼저 만들고, DSP 결과를 그 모델과 비교한다. 벤더 intrinsic의 반올림 규칙을 레퍼런스에 반영하는 게 핵심이다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `(a·b)>>15`에 포화 없음 | 드물게 큰 "펑" 잡음, NN activation 부호 반전 | (−1)·(−1) = 32768 → int16 −32768 | 포화 곱 intrinsic, 테스트 벡터에 INT16_MIN |
| 곱마다 `>>15` 후 누산 | 작은 신호에서 SNR 30 dB 이상 손실, DC 오프셋 | 절삭 bias가 탭 수만큼 누적 | 넓은 누산기, 마지막 한 번 반올림 |
| 중간 합 포화 | 합이 범위 안인데 결과가 틀림 | 포화는 결합법칙을 깬다 | guard bit 누산, 최종 포화 |
| int32로 Q30 곱 누산 | 탭이 늘면 갑자기 쓰레기 | 여유 1비트 | int64/40비트 누산, 스케일 다운 |
| 원형 인덱스에 `%` | FIR이 예상보다 5~50배 느림 | 나눗셈, 벡터화 불가 | `&` 마스크, 미러 버퍼, HW circular |
| 블록 경계 상태 누락 | 블록마다 딸깍 소리, 첫 출력 틀림 | T−1 이력 미보존 | 상태 버퍼 = T−1 이력 + B 블록, 경계 테스트 |
| 누산기 하나로 reduction | II가 MAC 지연에 묶임 | RecMII | 누산기 여러 개로 나누기 |
| `restrict` 누락 | 컴파일러 리포트에 파이프라이닝 실패 | aliasing 가능성 | `restrict`, 버퍼 분리 |
| 핫 데이터가 DDR에 | 이론치의 10~30%만 나옴 | VLIW stall | TCM/VTCM + DMA ping-pong |
| 작은 NN마다 NPU 깨움 | 배터리 예산 초과 | 깨우기·설정 고정 비용 | DSP/MCU에서 실행, cascade |

---

## 12. 면접에서 이렇게 말한다

**Q.** "What makes a DSP different from a general-purpose CPU?"

**A.** DSP는 FIR 한 탭에 필요한 일 — 명령 fetch, 데이터 2개 load, 포인터 2개 갱신, MAC, 루프 카운트 — 을 한 cycle에 병렬로 하도록 만든 프로세서다. 이를 위해 single-cycle MAC, Harvard/다중 메모리 포트, post-increment·circular 주소를 처리하는 AGU, zero-overhead loop, 포화·반올림 산술, guard bit 누산기, SIMD와 VLIW를 갖는다. CPU가 하드웨어(OoO)로 병렬성을 찾는 반면 DSP는 컴파일러가 찾게 해서 전력·면적을 아끼고 실행 시간을 결정적으로 만든다.

> "A DSP is built so that one tap of a FIR — fetch, two loads, two pointer updates, a multiply-accumulate and the loop count — happens in a single cycle. That's why it has single-cycle MACs, multiple memory ports, address generation units with post-increment and circular addressing, zero-overhead loops, saturating and rounding arithmetic, wide accumulators with guard bits, and SIMD plus VLIW issue. A CPU finds parallelism in hardware with out-of-order execution; a DSP moves that work to the compiler, which saves power and gives deterministic timing for real-time audio. I worked on Xtensa-based SSD firmware, and HiFi DSPs are the same Xtensa base with audio extensions."

**Q.** "Explain a Q15 multiply with rounding and saturation."

**A.** Q15 두 개를 곱하면 32비트 Q30이다. 결과 LSB의 절반인 2¹⁴를 더한 뒤 15비트 오른쪽 시프트하면 반올림된 Q15가 된다. 유일한 오버플로는 (−1)·(−1)로, 32768이 나와 int16에 안 들어가니 32767로 포화한다. 포화가 없으면 −1이 되어 부호가 뒤집힌다. 누산할 때는 곱마다 자르지 말고 넓은 누산기에 Q30으로 쌓은 뒤 마지막에 한 번 반올림·포화한다 — 제 측정에서 곱마다 자르면 SNR이 30 dB 이상 나빠졌다.

> "Multiply two Q15 values into a 32-bit Q30 product, add 2 to the 14th — half an output LSB — and shift right by 15. The only overflow case is minus one times minus one, which gives 32768, so the result saturates to 32767; without saturation it wraps to minus one and flips sign. For dot products I keep full Q30 products in a wide accumulator and round and saturate once at the end. Truncating every product adds a bias of half an LSB per tap; on a 31-tap FIR that cost me about 33 dB of SNR in a quick experiment."

**Q.** "What is VLIW, and who does the scheduling?"

**A.** VLIW는 한 명령어가 독립적인 연산 여러 개의 묶음이고 하드웨어는 검사 없이 동시에 발행한다. 스케줄은 컴파일러가 정적으로 한다. 루프에서는 software pipelining으로 여러 반복을 겹쳐 II cycle마다 새 반복을 시작하게 하는데, II의 하한은 자원(ResMII)과 되먹임 의존성(RecMII)이다. 예를 들어 MAC 지연이 2면 누산기 하나로는 2 cycle/MAC이 한계이고, 누산기를 둘로 나누면 1 cycle/MAC이 된다.

> "In a VLIW machine each instruction word is a bundle of independent operations that issue together without hardware dependency checks, so the compiler schedules everything statically. For loops it uses software pipelining — overlapping iterations so a new one starts every II cycles — and II is bounded by resources, the ResMII, and by loop-carried dependencies, the RecMII. With a two-cycle MAC latency, a single accumulator limits you to one MAC every two cycles; splitting into two accumulators gets you to one per cycle. Hexagon issues packets of up to four instructions, and Xtensa does the same with FLIX bundles."

**Q.** "What is HVX, and when would you use it versus the NPU?"

**A.** HVX는 Hexagon의 벡터 확장으로 1024비트(128바이트) 레지스터에 int8 128개 같은 SIMD 연산을 한다. 신호 처리, 이미지 처리, NPU가 지원하지 않는 op, 작은 모델에 쓴다. 대량 conv/GEMM은 HMX를 쓰는 HTP(QNN 백엔드)가 효율적이다. 기준은 op 지원, 크기, 호출 빈도다 — 10 ms마다 도는 FFT·mel과 작은 KWS는 DSP, 깨어난 뒤의 ASR·vision은 NPU. NPU를 깨우는 고정 비용과 데이터를 VTCM에 두는 방식까지 고려한다.

> "HVX is Hexagon's vector extension: 1024-bit, 128-byte vectors, so 128 int8 lanes per instruction, fed from VTCM. I'd use it for signal processing — FFT, mel, filters — for pre- and post-processing, for ops the matrix engine doesn't support, and for small always-on models where waking the NPU costs more than the compute. Large convolutions and matmuls go to the HTP backend in QNN, which uses the HMX matrix engine. The decision is op support, model size, and invocation rate — and I'd verify it by profiling, not by assumption."

**Q.** "Why do DSPs have circular addressing?"

**A.** FIR·IIR·스트리밍 conv는 최근 N개 샘플을 원형 버퍼에 두고 매 샘플마다 거꾸로 읽는다. 소프트웨어로 wrap하면 `%`는 나눗셈, `&`는 2의 거듭제곱 제약, 분기는 파이프라인 오버헤드가 있고, 무엇보다 벡터화를 막는다. AGU가 post-increment와 동시에 wrap을 처리하면 비용이 0이다. 이 Mac에서 32탭 FIR을 재 보니 `%`가 `&`보다 5배, wrap 없는 연속 버퍼보다 50배 느렸다.

> "Streaming filters keep the last N samples in a ring buffer and read it backwards every sample. Doing the wrap in software costs a divide for modulo, forces power-of-two sizes for masking, or adds branches — and it blocks vectorization. A circular addressing mode lets the address generator wrap the pointer in the same cycle as the post-increment, so the delay line is free. In a quick benchmark on my laptop, modulo indexing was about five times slower than masking and fifty times slower than a contiguous, vectorized buffer."

**Q.** "Why does a DSP accumulator have guard bits?"

**A.** 16×16 곱은 이미 32비트이고 Q30의 int32 범위는 [−2, 2)라 곱 두 개만 더해도 넘칠 수 있다. 40비트 누산기는 8 guard bit로 최악 곱을 511개까지 안전하게 더한다. 중간 합은 넘치더라도 최종 결과가 범위 안이면 정답이 나오도록 넓게 쌓고, 포화는 마지막에 한 번만 한다.

> "A 16-by-16 product is already 32 bits, and in Q30 a 32-bit register only covers minus two to two, so two products can overflow. A 40-bit accumulator adds eight guard bits — enough for 511 worst-case products — so intermediate sums can grow and only the final result is rounded and saturated. On a Cortex-M without a 40-bit accumulator, CMSIS-DSP uses 64-bit accumulation for the same reason."

---

## 13. 직접 해보기

1. **손계산**: Q15 값 a = 20000, b = 25000. `q15_mul(a, b)`의 정수 결과와 실수값은? 반올림 없는 `>> 15`와 비교하라. a = −20000이면? 정답: 곱 = 500 000 000, /32768 = 15258.79 → 반올림 15259 (≈ 0.46567), `>>15`는 15258. a = −20000이면 −15258.79 → 반올림도 내림도 −15259로 같다(내림은 −∞ 방향이라 음수에선 우연히 가까운 쪽). 부호에 따라 오차가 비대칭인 것이 bias의 정체다.
2. **손계산**: 24비트 오디오 샘플(Q23)과 Q15 계수를 곱해 64탭을 누산한다. 최악의 경우 몇 비트 누산기가 필요한가? 정답: 곱은 24 + 16 = 40비트(부호 중복 포함), 64 = 2⁶이므로 guard 6비트 → 46비트. 64비트 누산기면 충분.
3. **VLIW 손 스케줄**: 4.2절 기계에서 load 지연이 3, MAC 지연이 3이면 B와 C 경우의 II는? 1 cycle/MAC을 얻으려면 누산기가 몇 개 필요한가? 정답: B: RecMII = 3 → 3 cycle/MAC. 누산기 3개(unroll ×3)면 ResMII = max(6/2, 3/1) = 3, RecMII = 3 → II = 3에 MAC 3개 = 1 cycle/MAC.
4. **코드 과제**: 3.3절 벤치마크에 `if (r == 0) r = len; r--;` 분기 방식을 추가하고 속도를 비교하라. 컴파일러가 분기를 조건부 선택(`csel`)으로 바꿨는지 `cc -O2 -S`로 확인하라. 힌트: 분기 없는 `csel`이면 `&`와 비슷하고, 여전히 벡터화는 안 된다.
5. **코드 과제**: 2.4절 FIR을 Q31 계수·입력, int64 누산(Q62 → 필요하면 탭 수에 맞춰 스케일)으로 바꾸고 진폭 0.01에서 SNR을 재라. 힌트: 출력 반올림 잡음의 LSB가 2⁻¹⁵에서 2⁻³¹로 16비트 내려가므로 이론상 최대 약 96 dB(16 × 6.02) 개선이다. Q62 누산은 guard bit가 1개뿐이라 곱을 몇 비트 오른쪽으로 밀어 쌓아야 하고, 민 비트 수 × 6 dB만큼 개선이 줄어든다.
6. **설계 문제**: Hark 같은 이어버드(가정)에서 "wake word 이후 3초 음성 명령 → on-device ASR"을 CPU·DSP·NPU에 배치하라. 각 블록의 입력·출력·깨우는 조건을 한 줄씩 쓰라. 힌트: 마이크→(HW 데시메이션)→DSP front-end + VAD/KWS(항상) → 양성이면 NPU 켜고 ASR encoder → CPU가 디코딩·후처리. 9.2절 표와 E8.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| DSP | Digital Signal Processor | 규칙적인 수치 루프를 최소 전력으로 돌리도록 만든 프로세서 |
| MAC | multiply-accumulate | `acc += a·b`. DSP·NPU 성능의 기본 단위 |
| Harvard 구조 | 명령·데이터 메모리 분리 | 한 cycle에 명령과 데이터를 동시에 읽음 |
| AGU | Address Generation Unit | 포인터 갱신·원형·비트역순 주소를 전담하는 유닛 |
| post-increment | 접근 후 포인터 증가 | `x = *p++`를 명령 하나로 |
| circular addressing | modulo 주소 | 버퍼 끝에서 하드웨어가 시작으로 되감음 |
| bit-reversed addressing | 비트역순 주소 | FFT 입력·출력 재배치용 |
| zero-overhead loop | 하드웨어 루프 | 분기·카운터 명령 없이 반복 (Xtensa `LOOP`, Hexagon `loop0`) |
| saturation | 포화 | 넘치면 최대·최소값에 고정 |
| Q15 / Q31 | 고정소수점 포맷 | int16/int32를 2⁻¹⁵/2⁻³¹ 단위로 해석 |
| guard bit | 누산기 여유 비트 | 중간 합 오버플로 방지 (40비트 = 8 guard) |
| block floating point | 블록 공유 지수 | 블록마다 지수 하나 + 정수 가수 |
| SIMD | Single Instruction Multiple Data | 한 명령으로 여러 레인 처리 |
| VLIW | Very Long Instruction Word | 독립 연산 묶음을 컴파일러가 정적으로 스케줄 |
| bundle / packet | 한 번에 발행되는 명령 묶음 | Hexagon packet 최대 4 명령 |
| list scheduling | 목록 스케줄링 | 준비된 op을 빈 슬롯에 greedy하게 배치 |
| software pipelining | 반복 겹치기 | 여러 루프 반복을 겹쳐 II마다 시작 |
| II | initiation interval | 새 반복을 시작하는 간격 (cycle) |
| ResMII / RecMII | II의 하한 | 자원 한계 / 되먹임 의존성 한계 |
| Xtensa | Cadence Tensilica 설정형 코어 | HiFi·Vision DSP의 기반 |
| TIE | Tensilica Instruction Extension | 커스텀 명령·레지스터를 정의하는 언어 |
| FLIX | Flexible Length Instruction Xtensions | Xtensa의 VLIW 번들 포맷 |
| HiFi | Cadence 오디오 DSP 계열 | Xtensa + 오디오·음성 SIMD 확장 |
| Hexagon | Qualcomm DSP 아키텍처 | VLIW packet + 하드웨어 스레드 |
| HVX | Hexagon Vector eXtensions | 1024비트(128바이트) 벡터 |
| HMX | Hexagon Matrix eXtensions | NN 행렬 곱 엔진 |
| VTCM | Vector TCM | HVX/HMX용 on-chip scratchpad |
| HTP | Hexagon Tensor Processor | QNN의 NPU 백엔드 이름 |
| FastRPC | Hexagon 원격 호출 | CPU에서 DSP 함수를 호출하는 메커니즘 |
| TCM | Tightly-Coupled Memory | 캐시 없이 고정 지연으로 붙은 SRAM |

---

## 15. 요약 & 체크리스트

DSP는 FIR 한 탭에 필요한 일(명령 fetch, 데이터 두 개 load, 포인터 갱신, MAC, 루프 카운트)을 한 cycle에 병렬로 하도록 만든 프로세서다. 그 수단이 single-cycle MAC, 다중 메모리 포트, AGU의 post-increment·circular addressing, zero-overhead loop, 포화·반올림 산술, guard bit 누산기, SIMD, VLIW이고, 병렬성을 찾는 일을 하드웨어가 아니라 컴파일러에 맡겨 전력과 예측성을 얻는다. 고정소수점에서는 곱을 넓은 누산기에 쌓고 마지막에 한 번 반올림·포화하는 것이 핵심이고(곱마다 자르면 SNR 30 dB 이상 손실), 동적 범위가 넓으면 block floating point를 쓴다. VLIW 루프의 속도는 II로 정해지며, II는 자원(ResMII)과 되먹임(RecMII) 중 큰 쪽이 하한이다. Cadence HiFi·Vision은 Don이 다룬 Xtensa에 오디오·비전 확장을 붙인 것이고, Qualcomm Hexagon은 스칼라 VLIW 코어 + HVX(1024비트 벡터) + HMX(행렬) + VTCM 구조로 신호 처리는 DSP/HVX, 대량 NN은 QNN HTP가 맡는다. front-end와 작은 always-on 모델은 DSP, 큰 모델은 NPU, 제어와 fallback은 CPU가 기본 분업이다.

- [ ] DSP의 8가지 특징을 FIR 한 탭의 7가지 일과 짝지어 설명할 수 있다
- [ ] Q15 곱셈을 반올림·포화 포함해 손으로 계산하고 (−1)·(−1)의 함정을 설명할 수 있다
- [ ] 1000개 Q15 곱 합이 int32에서 넘치는 이유와 40비트 누산기가 몇 개까지 담는지 계산할 수 있다
- [ ] 고정소수점 FIR의 SNR을 LSB²/12로 예측하고, 곱마다 절삭하면 왜 나빠지는지 말할 수 있다
- [ ] block floating point의 원리와 per-group 양자화와의 관계를 설명할 수 있다
- [ ] 원형 버퍼 주소 계산 방식 5가지의 비용을 비교하고 HW circular addressing이 왜 공짜인지 말할 수 있다
- [ ] 4-slot VLIW 기계에서 dot product를 list scheduling하고 ResMII·RecMII로 II를 계산할 수 있다
- [ ] 누산기를 나눠 RecMII를 깨는 이유를 설명할 수 있다
- [ ] Xtensa·TIE·FLIX·HiFi·Vision의 관계를 Don의 SSD 경험과 연결해 설명할 수 있다
- [ ] Hexagon scalar·HVX·HMX·VTCM·HTP의 역할과, 신호 처리·작은 NN·큰 NN을 DSP/NPU/CPU에 나누는 기준을 말할 수 있다

## 참고 자료

- R. G. Lyons, "Understanding Digital Signal Processing", 3rd ed., Prentice Hall — 고정소수점, FIR/IIR, FFT 기초 (G5와 공통)
- J. L. Hennessy, D. A. Patterson, "Computer Architecture: A Quantitative Approach" — VLIW, 정적 스케줄링, software pipelining 장
- J. A. Fisher, P. Faraboschi, C. Young, "Embedded Computing: A VLIW Approach to Architecture, Compilers and Tools", Morgan Kaufmann, 2004
- B. R. Rau, "Iterative Modulo Scheduling", HP Labs Technical Report (1995) — II, ResMII, RecMII의 원전
- ARM CMSIS-DSP — [github.com/ARM-software/CMSIS-DSP](https://github.com/ARM-software/CMSIS-DSP) (Q15/Q31 함수, 64비트 누산 관례)
- Cadence Tensilica Xtensa / HiFi / Vision 제품 페이지 — [cadence.com](https://www.cadence.com) (IP → Tensilica 프로세서)
- foss-xtensa NN 라이브러리 — [github.com/foss-xtensa](https://github.com/foss-xtensa) (`nnlib-hifi4`, `nnlib-hifi5`)
- TensorFlow Lite Micro Xtensa 커널 — [github.com/tensorflow/tflite-micro](https://github.com/tensorflow/tflite-micro) (`tensorflow/lite/micro/kernels/xtensa`)
- Sound Open Firmware — [thesofproject.github.io](https://thesofproject.github.io) (Xtensa DSP 위 오디오 펌웨어)
- Qualcomm Hexagon SDK, Hexagon V6x HVX Programmer's Reference Manual, QNN(Qualcomm AI Engine Direct) 문서 — [developer.qualcomm.com](https://developer.qualcomm.com) (로그인 필요한 문서 있음)
- Qualcomm AI Hub — [aihub.qualcomm.com](https://aihub.qualcomm.com) (PJ4 실습)
- Halide Hexagon 백엔드 — [halide-lang.org](https://halide-lang.org)
- Linux kernel Qualcomm remoteproc 드라이버 (`drivers/remoteproc/qcom_q6v5_pas.c`) — aDSP/cDSP/SLPI 부팅 구조 확인용
- M. Horowitz, "Computing's Energy Problem (and what we can do about it)", ISSCC 2014 (9.3절 에너지 직관, D7)
