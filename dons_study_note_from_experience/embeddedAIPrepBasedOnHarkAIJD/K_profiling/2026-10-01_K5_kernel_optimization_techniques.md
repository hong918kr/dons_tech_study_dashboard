# K5. 커널 최적화 기법 — 루프 변환 카탈로그, 데이터 레이아웃, Winograd·FFT conv, DMA와 연산 겹치기

> **이 노트를 다 읽으면**: 프로파일과 roofline 위치만 보고 "구현 문제 / 바이트 문제 / 연산 문제"를 판정해 처방 순서를 정할 수 있다 · unroll, unroll-and-jam, interchange, fusion·fission, strip-mining, software pipelining, LICM·restrict, strength reduction을 각각 10~30줄 C로 보여 주고 이 Mac과 Cortex-M4에서 효과가 어떻게 다른지 숫자로 말할 수 있다 · AoS/SoA, NCHW/NHWC/NCHWc 중 무엇을 언제 고르는지 측정값으로 설명할 수 있다 · Winograd F(2×2,3×3)을 손으로 유도·구현·검증하고 int8에서 왜 어려운지, FFT conv가 언제 이기는지 말할 수 있다 · MCU에서 single/double/triple buffering의 총 시간을 손계산·시뮬레이션하고 SRAM 예산 안에서 타일 크기를 고를 수 있다
> **JD 연결**: "Profile and optimize memory usage, power consumption, real-time performance", "Optimizing models for MCUs & edge processors (Cortex-M/A, RISC-V, DSP)" — study_prep_list **K5**: loop tiling, cache blocking, im2col / Winograd 개념, DMA와 연산 겹치기 / 메모리 layout 변경 (성능 개선 실무). 프로젝트 PJ7(INT8 conv/matmul 커널 + NEON/Helium 비교)의 "최적화 방법론" 쪽.
> **Don 기준 난이도**: 루프·포인터 최적화, ping-pong DMA, 캐시 maintenance, "측정 → 고침 → 재측정" 습관은 SSD 펌웨어에서 이미 몸에 밴 것 / 변환들을 **이름 붙은 카탈로그**로 정리하는 것, 컴파일러가 이미 해 주는 것과 손으로 해야 하는 것을 구별하는 법, Winograd·FFT 같은 **알고리즘 수준의 연산 절감**과 그 수치 정밀도 대가가 새로 배울 부분
> **선행 노트**: K2 (사이클 측정), D3 (roofline·fusion), E1 (캐시·accumulator·tiling transpose·prefetch), E2 (NEON·Helium·SMLAD), E5 (NPU tiling·double buffering), E7 (DMA·캐시 일관성), J3 (int8 conv 루프 순서·im2col·register blocking·TCM tiling), C6 (graph 수준 layout·fusion)

---

## 0. 큰 그림 — 이게 왜 필요한가

edge ML 엔지니어가 받는 전형적인 티켓은 이렇다: "wake word 모델이 Cortex-M에서 프레임당 14 ms 걸리는데 예산은 10 ms다." 혹은 "Hexagon으로 옮겼더니 depthwise 레이어 하나가 전체의 40 %를 먹는다." 모델 구조를 바꿀 수 없다면(이미 정확도 검증이 끝났다면) 남는 건 **같은 수학을 더 싸게 계산하는 것**이다. 이 노트는 그 "더 싸게"의 도구 상자를 정리한다.

Don의 SSD 경험으로 치면 FTL hot path 튜닝과 똑같다. 먼저 트레이스로 어디서 시간이 새는지 찾고, 그게 NAND 대기(메모리)인지 CPU 연산인지 ISR·락 오버헤드인지 분류한 다음, 원인에 맞는 처방(버퍼링, 루프 정리, 큐 깊이 조절)을 하나씩 넣고 다시 잰다. 커널 최적화도 순서가 같고, 다른 것은 처방 목록뿐이다.

```
               이 노트의 지도
  ┌──────────────────────────────────────────────────────────────┐
  │ 1절 결정 절차: 측정 → Amdahl → roofline 위치 → 처방 선택       │
  └──────────────┬───────────────────────────────────────────────┘
       구현·오버헤드 문제        바이트 문제              연산 문제
  ┌──────────────────────┐ ┌───────────────────┐ ┌─────────────────────┐
  │ 2절 루프 변환 카탈로그 │ │ 3절 데이터 레이아웃 │ │ 4절 Winograd · FFT  │
  │ unroll, jam, interch.│ │ AoS/SoA, NCHWc    │ │ (연산 수 자체를 줄임) │
  │ LICM, restrict, ...  │ │ 5절 DMA 겹치기     │ │                     │
  └──────────────────────┘ └───────────────────┘ └─────────────────────┘
       6절 고정소수점 트릭 (requant 합치기, SSAT, LUT, 역수 곱)
       7절 검증: 매 단계 bit-exact/허용오차 + 성능 회귀 게이트
```

### 0.1 이미 다른 노트에 있는 것 — 여기서는 반복하지 않는다

| 주제 | 어디서 배웠나 | 이 노트에서는 |
|---|---|---|
| matmul 루프 순서 ijk/ikj, 행렬 전치 tiling | A1, E1 4절 | 다른 커널(열 합, 분리형 필터)로 interchange·strip-mining |
| 다중 accumulator, prefetch, branchless | E1 2·5·7절 | 다시 재지 않는다. unroll-and-jam이 "accumulator + load 재사용"인 이유만 연결 |
| NEON SDOT/SMMLA, 자동 벡터화 리포트 | E2 | `-Rpass` 리포트를 판정 도구로만 쓴다 |
| NPU tiling planner, Python double buffering | E5 5절 | **MCU 쪽**: CPU가 DMA를 거는 비용, ISR, 캐시 invalidate, 연산 jitter, 타일 크기 스윕을 C로 |
| DMA descriptor, 캐시 stale/dirty 버그 | E7 5·6절 | 비용 모델에만 반영 |
| roofline, fusion 2.9× | D3 | 결정 절차의 분류기로 사용 |
| int8 conv 루프 순서, im2col vs direct, register blocking 9.5→120 GMAC/s, 64 KB TCM tiling | J3 7·8절 | 반복 안 함. Winograd·레이아웃·카탈로그로 확장 |

### 0.2 실험 환경과 측정 규칙

- 호스트: Apple M2 (P-core L1D 128 KiB, L2 16 MiB, 캐시 라인 128 B), Apple clang 21. C는 전부 `cc -std=c11 -Wall -Wextra -O2`, **경고 0개**. Python은 `.venv/bin/python` (numpy 2.0.2, scipy 1.13.1).
- MCU는 실물이 없다. `--target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16`로 **어셈블리만** 만들고, 루프 몸통의 명령 수·load 수·spill 수를 세는 작은 스크립트(`loopstat.py`)로 비교한다. 명령 수는 cycle이 아니다. 실제 cycle은 보드에서 DWT CYCCNT(K2)로 재야 한다.
- 측정 중 다른 작업이 함께 돌고 있었다(load average 3~7). 그래서 모든 C 벤치마크는 아래 `bench.h`로 **R번 재서 median**을 쓰고, 프로그램 전체를 **3번** 돌려 비교했다. 본문 출력은 그중 한 번이고, 세 번 사이의 흔들림은 본문에 적는다. 소수점이 아니라 **배수와 자릿수**를 읽자.
- 스크래치 파일은 전부 `/private/tmp/claude-501/k5/`에 있다. 노트 폴더에는 아무것도 남기지 않는다. 지면을 아끼려고 일부 예제는 `main`을 "무엇을 하는지" 주석 한 줄로 줄였다(초기화 → `BENCH` → 기준과 비교 → 출력이라는 같은 틀이다). 커널 함수와 출력은 실제 실행한 그대로다.

모든 C 예제가 공유하는 타이밍 헤더다. 확인할 것: 시간 재는 방법과, 컴파일러가 벤치마크를 "최적화로 없애 버리는" 사고를 막는 장치.

```c
/* bench.h — 공통 타이밍 도구: body를 IN번 돌린 시간을 R번 재서 median(중앙값)/IN 을 쓴다 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
static double now_s(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static int cmp_d(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }
#define BENCH(res, R, IN, body) do { double ts_[R]; for (int r_ = 0; r_ < (R); r_++) { double t0_ = now_s(); \
    for (int i_ = 0; i_ < (IN); i_++) { body; __asm__ volatile("" ::: "memory"); } ts_[r_] = (now_s() - t0_) / (IN); } \
    qsort(ts_, (R), sizeof(double), cmp_d); (res) = ts_[(R) / 2]; } while (0)
#define NOINLINE __attribute__((noinline))
/* 컴파일러가 "이 버퍼는 아무도 안 본다"고 판단해 계산을 루프 밖으로 빼거나 지우지 못하게 한다 */
static inline void escape(void *p) { __asm__ volatile("" : : "r"(p) : "memory"); }
```

여기서 실제로 겪은 함정 두 개를 먼저 적어 둔다. (1) macOS의 `CLOCK_MONOTONIC`은 해상도가 약 1 µs라서, 1 µs 미만 커널은 `IN`번 반복해서 재야 한다(처음에 안 했더니 0.5 µs 커널이 "1.0 µs 또는 2.0 µs"로만 찍혔다). (2) 결과를 반환만 하는 순수 함수(3.1절 AoS 예제)는 컴파일러가 "인자가 같으니 한 번만 부르면 된다"고 판단해 **루프 밖으로 빼 버렸다** — 시간이 0.00 µs로 나왔다. memory clobber만으로는 부족했고, 버퍼 포인터를 `escape()`로 "누가 볼 수도 있다"고 알려 준 뒤에야 정상 측정이 됐다. 마이크로벤치마크에서 숫자가 **너무 좋으면** 먼저 의심하자.

---

## 1. 최적화 결정 절차 — 재고, 찍고, 처방하고, 다시 잰다

### 1.1 직관 — 의사의 진단 순서

환자가 "피곤하다"고 하면 의사는 바로 약을 주지 않는다. 문진(어디가 얼마나), 검사(혈액·영상), 진단(원인 분류), 처방, 그리고 재검사. 커널도 같다.

1. **문진 = 프로파일**: 전체 추론 시간 중 이 커널이 몇 %인가? (K2)
2. **검사 = roofline 위치**: 이 커널은 FLOP·바이트로 보면 어디까지 빨라질 수 있나? 지금 그 천장의 몇 %인가? (D3)
3. **진단 = 세 가지 중 하나**: 구현·오버헤드 문제 / 바이트 문제 / 연산 문제
4. **처방**: 진단에 맞는 변환 하나
5. **재검사**: 결과가 맞는지(bit-exact 또는 허용 오차), 실제로 빨라졌는지

### 1.2 Amdahl 먼저 — "어디를 고칠 가치가 있나"

Amdahl의 법칙: 전체 시간 중 비율 f인 부분을 s배 빠르게 하면 전체 속도 향상은

```
speedup_total = 1 / ( (1 − f) + f / s )
```

말로 하면: **안 고친 나머지(1 − f)가 바닥을 정한다.** 아무리 s를 키워도 1 / (1 − f)를 넘지 못한다.

손계산: conv가 46 %(f = 0.46)일 때 그 부분을 2배 빠르게 하면 `1 / (0.54 + 0.23) = 1 / 0.77 = 1.30`배. 무한히 빠르게 해도 `1 / 0.54 = 1.85`배다. 반대로 비율 4 %짜리 softmax를 10배 빠르게 해도 `1 / (0.96 + 0.004) = 1.037`배 — 3.7 % 개선이다.

아래는 가상의 KWS(keyword spotting) 모델 프로파일(숫자는 설명용으로 만든 것)에 Amdahl을 적용한 것이다. 확인할 것: 어느 커널부터 손대야 하는지.

```python
# Amdahl: 전체 시간 중 비율 f인 부분을 s배 빠르게 하면 전체는 1 / ((1 - f) + f / s) 배
profile = {"conv2d (standard)": 0.46, "depthwise conv": 0.22, "MFCC front-end": 0.14,
           "fully connected": 0.08, "softmax/other ops": 0.04, "interpreter overhead": 0.06}   # 예시용 가상 프로파일
def amdahl(f, s): return 1 / ((1 - f) + f / s)
for name, f in profile.items():
    print(f"{name:22s} share {f*100:4.0f}%  2x faster -> {amdahl(f, 2):.2f}x total   infinitely faster -> {amdahl(f, 1e9):.2f}x")
f = profile["conv2d (standard)"] + profile["depthwise conv"]
print(f"all conv 2x faster -> {amdahl(f, 2):.2f}x total; Winograd on standard conv only (1.8x) -> {amdahl(0.46, 1.8):.2f}x")
```

```text
conv2d (standard)      share   46%  2x faster -> 1.30x total   infinitely faster -> 1.85x
depthwise conv         share   22%  2x faster -> 1.12x total   infinitely faster -> 1.28x
MFCC front-end         share   14%  2x faster -> 1.08x total   infinitely faster -> 1.16x
fully connected        share    8%  2x faster -> 1.04x total   infinitely faster -> 1.09x
softmax/other ops      share    4%  2x faster -> 1.02x total   infinitely faster -> 1.04x
interpreter overhead   share    6%  2x faster -> 1.03x total   infinitely faster -> 1.06x
```

출력에서 볼 것: 같은 "2배"라도 커널 비중에 따라 전체 효과가 1.02×에서 1.30×까지 다르다. 4절의 Winograd(이 Mac에서 약 1.8×)를 standard conv에만 적용하면 전체는 1.26×다. 면접에서 "microbenchmark에선 2배인데 end-to-end는 5 %밖에 안 늘었다"는 질문의 답이 바로 이 식이다.

### 1.3 roofline 위치로 진단하기

D3에서 배운 대로 커널의 상한은 `min(peak, I × BW)`이고 I = FLOP ÷ 바이트(arithmetic intensity)다. 여기에 **실측 성능 ÷ 그 상한**을 하나 더 계산하면 진단이 된다.

| 실측 ÷ 지붕 | I vs ridge | 진단 | 처방 (이 노트의 절) |
|---|---|---|---|
| 30 % 미만 | 상관없음 | 구현·오버헤드 문제: 벡터화 실패, 나쁜 접근 패턴, 루프·호출 오버헤드 | 2절 루프 변환, 3절 레이아웃 |
| 70 % 이상 | I < ridge | 메모리 지붕에 붙음: 바이트를 줄여야 한다 | fusion, tiling, int8, SRAM 상주, 5절 DMA |
| 50 % 이상 | I > ridge | 연산 쪽: 연산 수를 줄이거나 peak를 올린다 | register blocking, 4절 Winograd, SIMD·NPU |

경계값 30 %·70 %는 정해진 규칙이 아니라 이 노트에서 쓰는 실무 감각이다. 중요한 건 **"지붕까지 멀면 알고리즘보다 구현을 먼저 의심한다"**는 순서다.

아래 코드는 이 노트 2~3절에서 실제로 잰 커널 다섯 개를 이 Mac의 천장(D3에서 잰 NEON fp32 1코어 약 93 GFLOP/s, DRAM 약 70 GB/s)에 찍고 진단한다. 확인할 것: 같은 "느린 커널"이라도 처방이 다르다.

```python
# 이 Mac(M2 P-core 1개)의 천장: D3에서 잰 값 — NEON fp32 약 93 GFLOP/s, DRAM 약 70 GB/s
PEAK, BW = 93.0, 70.0
kernels = [  # 이름, 측정 시간(s), FLOP, DRAM 바이트(대략) — 이 노트 2~4절의 실측
    ("FIR K runtime (1M x 5 taps)",  2.14e-3, 2 * 5 * 2**20,        8 * 2**20),
    ("colsum j-i (strided)",         12.99e-3, 2048 * 2048,          4 * 2048 * 2048),
    ("fused a*x+b,relu,*s (16M)",    2.35e-3, 3 * 2**24,             8 * 2**24),
    ("depthwise NCHW8c+4px",         51.8e-6, 2 * 64 * 56 * 56 * 9,  4 * 64 * (58 * 58 + 56 * 56)),
    ("pointwise NHWC 64->64",        470.7e-6, 2 * 64 * 64 * 3136,   4 * (2 * 64 * 3136 + 64 * 64)),
]
print(f"ridge = {PEAK / BW:.2f} FLOP/B")
print(f"{'kernel':30s} {'I':>6s} {'meas':>6s} {'roof':>6s} {'%roof':>6s}  verdict")
for name, t, fl, by in kernels:
    I = fl / by; meas = fl / t / 1e9; roof = min(PEAK, I * BW); frac = meas / roof
    bound = "memory" if I * BW < PEAK else "compute"
    if frac < 0.3:   verdict = "fix implementation (vectorize/pattern/overhead)"
    elif bound == "memory": verdict = "cut bytes (fuse/int8/tile)"
    else:            verdict = "cut ops or raise peak (block/Winograd/NPU)"
    print(f"{name:30s} {I:6.2f} {meas:6.1f} {roof:6.1f} {frac*100:5.0f}%  {bound}: {verdict}")
```

```text
ridge = 1.33 FLOP/B
kernel                              I   meas   roof  %roof  verdict
FIR K runtime (1M x 5 taps)      1.25    4.9   87.5     6%  memory: fix implementation (vectorize/pattern/overhead)
colsum j-i (strided)             0.25    0.3   17.5     2%  memory: fix implementation (vectorize/pattern/overhead)
fused a*x+b,relu,*s (16M)        0.38   21.4   26.2    82%  memory: cut bytes (fuse/int8/tile)
depthwise NCHW8c+4px             2.17   69.7   93.0    75%  compute: cut ops or raise peak (block/Winograd/NPU)
pointwise NHWC 64->64           15.84   54.6   93.0    59%  compute: cut ops or raise peak (block/Winograd/NPU)
```

출력에서 볼 것: FIR(탭 수가 런타임 값)과 strided 열 합은 지붕의 6 %, 2 %다 — 알고리즘이 아니라 **코드가 문제**이고, 2.1절·2.3절에서 각각 10배, 33배 빨라진다. fused elementwise는 이미 메모리 지붕의 82 %라서 루프를 더 다듬어도 소용없고 바이트를 줄여야 한다(int8로 바꾸면 4배). depthwise·pointwise는 데이터가 L2에 들어가므로 DRAM 지붕으로 판정하는 건 근사다. 그래도 "지붕에 가깝다 → 더 짜내려면 연산 자체를 줄여야 한다"는 방향은 맞다.

함정: 바이트를 셀 때 **어느 메모리 레벨**인지 정해야 한다. 작업 집합(working set)이 L1/L2에 들어가면 DRAM 대역폭으로 계산한 지붕은 너무 낮다(D3 6절의 계층별 roofline). MCU에서는 SRAM이 곧 "DRAM 자리"라서 오히려 단순하다.

### 1.4 한 장으로

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 470"><defs><marker id="k1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10" y="20" font-size="14" text-anchor="start">커널 최적화 결정 절차 — 재고, 위치를 찍고, 원인에 맞는 처방, 검증 후 다시 잰다</text><rect x="190" y="34" width="300" height="44" rx="6" fill="#888" fill-opacity="0.15" stroke="#888"/><text x="340.0" y="52.5" font-size="12" text-anchor="middle">① 측정 (K2): 전체 프로파일 → 시간 비중 순 정렬</text><text x="340.0" y="67.5" font-size="12" text-anchor="middle">Amdahl: 비중 큰 것부터 하나만 고른다</text><rect x="190" y="98" width="300" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/><text x="340.0" y="116.5" font-size="12" text-anchor="middle">② 그 커널의 FLOP·바이트를 세고 실측 성능을</text>
<text x="340.0" y="131.5" font-size="12" text-anchor="middle">roofline 위에 찍는다 (D3)</text><rect x="215" y="162" width="250" height="40" rx="6" fill="#888" fill-opacity="0.15" stroke="#888"/><text x="340.0" y="186.0" font-size="12" text-anchor="middle">③ 실측 ÷ 지붕 = ?</text><rect x="10" y="232" width="210" height="120" rx="6" fill="#d0564a" fill-opacity="0.15" stroke="#d0564a"/><text x="115.0" y="258.5" font-size="12" text-anchor="middle">지붕의 30 % 미만</text><text x="115.0" y="273.5" font-size="12" text-anchor="middle">= 구현·오버헤드 문제</text><text x="115.0" y="288.5" font-size="12" text-anchor="middle">벡터화 리포트 확인, interchange,</text><text x="115.0" y="303.5" font-size="12" text-anchor="middle">layout(AoS→SoA), unroll,</text><text x="115.0" y="318.5" font-size="12" text-anchor="middle">LICM·restrict, strength reduction,</text>
<text x="115.0" y="333.5" font-size="12" text-anchor="middle">호출 수 줄이기(batch·fuse)</text><rect x="235" y="232" width="210" height="120" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/><text x="340.0" y="258.5" font-size="12" text-anchor="middle">memory 지붕 근처</text><text x="340.0" y="273.5" font-size="12" text-anchor="middle">= 바이트를 줄인다</text><text x="340.0" y="288.5" font-size="12" text-anchor="middle">fusion, tiling·strip-mining,</text><text x="340.0" y="303.5" font-size="12" text-anchor="middle">layout 변경, int8·int4,</text><text x="340.0" y="318.5" font-size="12" text-anchor="middle">SRAM 상주, DMA 겹치기</text><text x="340.0" y="333.5" font-size="12" text-anchor="middle">(겹치기는 숨길 뿐 못 줄임)</text><rect x="460" y="232" width="210" height="120" rx="6" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="565.0" y="258.5" font-size="12" text-anchor="middle">compute 지붕 근처</text><text x="565.0" y="273.5" font-size="12" text-anchor="middle">= 연산을 줄이거나 피크를 올린다</text><text x="565.0" y="288.5" font-size="12" text-anchor="middle">SIMD·register blocking,</text><text x="565.0" y="303.5" font-size="12" text-anchor="middle">unroll-and-jam, Winograd,</text><text x="565.0" y="318.5" font-size="12" text-anchor="middle">SDOT/SMLAD/Helium,</text><text x="565.0" y="333.5" font-size="12" text-anchor="middle">DSP·NPU로 offload</text><rect x="140" y="380" width="400" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/><text x="340.0" y="398.5" font-size="12" text-anchor="middle">④ 검증: bit-exact(int) 또는 허용 오차(float) 테스트 통과?</text><text x="340.0" y="413.5" font-size="12" text-anchor="middle">⑤ 같은 조건으로 재측정 → 회귀 기록 (I5) → ①로</text>
<line x1="340" y1="78" x2="340" y2="96" stroke="currentColor" marker-end="url(#k1)"/><line x1="340" y1="142" x2="340" y2="160" stroke="currentColor" marker-end="url(#k1)"/><line x1="260" y1="202" x2="115" y2="230" stroke="currentColor" marker-end="url(#k1)"/><line x1="340" y1="202" x2="340" y2="230" stroke="currentColor" marker-end="url(#k1)"/><line x1="420" y1="202" x2="565" y2="230" stroke="currentColor" marker-end="url(#k1)"/><line x1="115" y1="352" x2="250" y2="378" stroke="currentColor" marker-end="url(#k1)"/><line x1="340" y1="352" x2="340" y2="378" stroke="currentColor" marker-end="url(#k1)"/><line x1="565" y1="352" x2="430" y2="378" stroke="currentColor" marker-end="url(#k1)"/><polyline points="540,402 660,402 660,56 492,56" fill="none" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#k1)"/><text x="600" y="395" font-size="12" text-anchor="middle">반복</text>
<text x="10" y="452" font-size="12" text-anchor="start">작업 단위는 '커널 하나 × 변환 하나'. 한 번에 여러 개를 바꾸면 무엇이 효과였는지 모른다.</text></svg>
```

그림 1 — 이 노트의 결정 절차. ①~②로 대상과 위치를 정하고, ③의 비율로 세 갈래 중 하나를 고른 뒤, ④ 검증과 ⑤ 재측정을 거쳐 다시 ①로 돌아간다. 한 번에 변환 하나만 넣는다.

### 1.5 함정

- **프로파일 없이 "느려 보이는" 코드부터 고친다.** Amdahl 표의 softmax처럼 4 %짜리를 이틀 걸려 10배 만들어도 전체는 3.7 %다.
- **roofline 위치를 안 보고 Winograd부터 꺼낸다.** 지붕의 6 %에서 도는 커널에 연산 수를 줄이는 알고리즘을 넣으면 "느린 코드로 짠 더 복잡한 알고리즘"이 될 뿐이다.
- **한 번에 세 가지를 바꾼다.** 빨라졌어도 무엇 덕분인지, 느려졌어도 무엇 때문인지 모른다. SSD 펌웨어에서 "한 커밋에 한 가지 튜닝"을 지키던 이유와 같다.

---

## 2. 루프 변환 카탈로그 — 이름, 효과, 이 Mac과 Cortex-M4에서의 차이

### 2.0 읽는 법과 도구

**루프 변환(loop transformation)**은 결과를 바꾸지 않고(또는 허용된 범위에서만 바꾸고) 루프의 모양을 바꿔 하드웨어에 더 잘 맞게 만드는 것이다. 컴파일러 교과서(Allen & Kennedy)에 나오는 이름들이 있고, 면접에서도 이 이름으로 말한다. 각 변환마다 이렇게 정리한다:

- **무엇을 바꾸나** — 한 줄 정의
- **왜 빨라지나** — 줄어드는 것(분기, load, 바이트, 의존성 체인, 나눗셈)
- **측정** — 10~30줄 C, 기준 버전과 결과 비교(bit-exact 또는 최대 오차)
- **Cortex-M4에서는** — OoO 코어(M2)와 in-order MCU는 효과가 다르다. M4 어셈블리의 루프 몸통을 세서 추론한다

M4 어셈블리는 아래 스크립트로 만들고 센다. 확인할 것: "명령 수·load 수·spill 수"라는 세 지표.

```sh
#!/bin/bash
# usage: m4.sh file.c out.s [extra flags]  — Cortex-M4F 어셈블리 생성
src=$1; out=$2; shift 2
cc -std=c11 -O2 --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 -ffreestanding -S "$src" -o "$out" "$@"
```

```python
import sys, re
# usage: loopstat.py file.s label  — 루프 헤더 label부터 그 label로 돌아가는 분기까지 명령 수를 센다
lines = open(sys.argv[1]).read().splitlines(); L = sys.argv[2]
i = next(k for k, s in enumerate(lines) if s.startswith(L + ":"))
n = ld = st = sp = 0
for s in lines[i + 1:]:
    t = s.strip()
    if not t or t.startswith((".", "@")) or t.endswith(":"): continue
    op = t.split()[0]; n += 1
    ld += bool(re.match(r"v?ldr|vldm|ldm", op)); st += bool(re.match(r"v?str|vstm|stm", op)); sp += ("Spill" in t or "Reload" in t)
    if L in t and op.startswith("b"): break
print(f"{sys.argv[2]:10s} insns={n} loads={ld} stores={st} spill/reload={sp}")
```

주의: 컴파일러가 루프를 2배·4배로 풀어 놓기 때문에 **몸통 하나가 원소 몇 개를 처리하는지** 확인하고 나눠야 한다(store 개수로 확인한다).

### 2.1 Unrolling — 그리고 "컴파일러가 이미 하는 것"

**무엇을**: 루프 몸통을 k번 복사하고 반복 횟수를 1/k로 줄인다. **왜**: 분기·카운터 갱신이 줄고, 복사된 몸통 사이에서 명령을 섞어(scheduling) 지연을 숨길 수 있다. 하지만 현대 컴파일러는 `-O2`에서 이미 이걸 한다. 손으로 할 가치가 큰 경우는 **반복 횟수를 컴파일러가 모를 때**다. 횟수가 상수이면 안쪽 루프가 완전히 풀리고(full unroll), 그 덕에 **바깥 루프가 벡터화**되는 연쇄 효과가 생긴다.

손계산: 5탭 FIR의 출력 하나는 곱 5번 + 덧셈 5번. 탭 수 K가 런타임 값이면 안쪽 루프 5회에 매번 비교·분기·카운터가 붙고, 안쪽 루프가 너무 짧아서 벡터화 이득도 없다. K가 상수 5면 안쪽이 사라지고, 바깥 i 루프를 4개씩 묶어 `fmla` 5번으로 출력 4개를 만든다.

```c
#include "bench.h"
#define N (1 << 20)
/* (a) 탭 수 K가 런타임 값: 안쪽 루프 길이를 컴파일러가 모른다 */
NOINLINE void fir_rt(const float *x, const float *h, float *y, int n, int K) {
    for (int i = 0; i < n; i++) { float s = 0; for (int k = 0; k < K; k++) s += h[k] * x[i + k]; y[i] = s; }
}
/* (b) K = 5가 컴파일 타임 상수: 안쪽 루프가 완전히 풀리고(full unroll) 바깥 i 루프가 벡터화된다 */
NOINLINE void fir_k5(const float *x, const float *h, float *y, int n) {
    for (int i = 0; i < n; i++) { float s = 0; for (int k = 0; k < 5; k++) s += h[k] * x[i + k]; y[i] = s; }
}
/* (c) 단순 덧셈을 손으로 4배 unroll — 컴파일러가 이미 하는 일 */
NOINLINE void add_plain(const int32_t *a, const int32_t *b, int32_t *c, int n) { for (int i = 0; i < n; i++) c[i] = a[i] + b[i]; }
NOINLINE void add_unr4(const int32_t *a, const int32_t *b, int32_t *c, int n) {
    for (int i = 0; i < n; i += 4) { c[i] = a[i] + b[i]; c[i+1] = a[i+1] + b[i+1]; c[i+2] = a[i+2] + b[i+2]; c[i+3] = a[i+3] + b[i+3]; }
}
int main(void) {
    float *x = malloc((N + 8) * 4), *y1 = malloc(N * 4), *y2 = malloc(N * 4), h[5] = {0.1f, 0.2f, 0.4f, 0.2f, 0.1f};
    int32_t *a = malloc(N * 4), *b = malloc(N * 4), *c = malloc(N * 4);
    for (int i = 0; i < N + 8; i++) x[i] = (float)((i * 7919) % 1000) / 1000.0f;
    for (int i = 0; i < N; i++) { a[i] = i; b[i] = 3 * i; }
    volatile int K = 5; double t1, t2, t3, t4;
    BENCH(t1, 31, 5, fir_rt(x, h, y1, N, K));   BENCH(t2, 31, 5, fir_k5(x, h, y2, N));
    BENCH(t3, 31, 5, add_plain(a, b, c, N));    BENCH(t4, 31, 5, add_unr4(a, b, c, N));
    float md = 0; for (int i = 0; i < N; i++) { float d = y1[i] - y2[i]; if (d < 0) d = -d; if (d > md) md = d; }
    printf("FIR K runtime : %6.3f ms\nFIR K=5 const : %6.3f ms  (%.1fx, max|diff|=%g)\n", t1 * 1e3, t2 * 1e3, t1 / t2, md);
    printf("add plain     : %6.3f ms\nadd unroll x4 : %6.3f ms  (%.2fx)\n", t3 * 1e3, t4 * 1e3, t3 / t4);
    return 0;
}
```

```text
FIR K runtime :  2.144 ms
FIR K=5 const :  0.208 ms  (10.3x, max|diff|=1.19209e-07)
add plain     :  0.151 ms
add unroll x4 :  0.142 ms  (1.06x)
```

출력에서 볼 것: 탭 수를 상수로 만든 것만으로 **10배**(세 번 실행 10.3~11.9×). 손 unroll ×4는 1.06~1.16×로 잡음 수준이다 — 컴파일러가 이미 벡터화·unroll을 했기 때문이다. 그리고 `max|diff| = 1.19e-07`(1 ulp 근처)이 나왔다. 원인을 확인하려고 `-ffp-contract=off`로 다시 빌드하면 차이가 0이 된다. 즉 (b)는 곱셈과 덧셈을 한 번에 반올림하는 FMA(`fmla`)를 쓰고, (a)는 따로 반올림(`fmul`+`fadd`)했다. **float 커널은 최적화가 결과를 1 ulp 바꿀 수 있다** — 7절에서 다시 다룬다.

실무 버전: 탭 수가 몇 가지로 정해져 있으면(3, 5, 7) `switch`로 상수 버전을 따로 부르거나, C++ 템플릿·매크로로 특수화한다. CMSIS-DSP·TFLM 커널에 "kernel 3×3 전용 경로"가 따로 있는 이유다.

**Cortex-M4에서는**: 같은 `fir_k5`를 M4로 컴파일하면 몸통 하나가 출력 4개를 처리하는데 명령 90개, load 40개다(출력당 load 10개). 이유를 어셈블리에서 보면 **h[0..4]를 출력마다 다시 읽는다**. `y`에 쓰는 store가 `h`를 바꿀 수도 있다고(aliasing) 컴파일러가 가정하기 때문이다. 포인터에 `restrict`를 붙이면(2.7절) h가 레지스터 5개에 머물고, 창이 한 칸씩 미끄러질 때 x도 레지스터에서 재사용된다.

```c
void fir_k5_r(const float *restrict x, const float *restrict h, float *restrict y, int n) {
    for (int i = 0; i < n; i++) { float s = 0; for (int k = 0; k < 5; k++) s += h[k] * x[i + k]; y[i] = s; }
}
```

```text
.LBB1_3    insns=90 loads=40 stores=4 spill/reload=0     (fir_k5, restrict 없음: 출력 4개)
.LBB0_3    insns=58 loads=8 stores=4 spill/reload=0      (fir_k5_r, restrict: 출력 4개)
```

출력에서 볼 것: 출력당 명령 22.5 → 14.5개, load 10 → 2개. M4의 load는 보통 2 cycle(연속 load는 파이프라인되어 1 cycle씩 추가)이라 load 수가 곧 cycle에 큰 몫을 한다. OoO인 M2에서는 이 차이가 가려지지만 **in-order MCU에서는 그대로 시간**이 된다. (위 출력의 괄호 설명은 내가 붙인 것이고, 숫자 줄이 실제 출력이다.)

### 2.2 Unroll-and-jam — 바깥 루프를 풀고 안쪽을 합친다

**무엇을**: 바깥 루프를 k번 풀고(unroll), 풀린 k개의 안쪽 루프를 하나로 합친다(jam). **왜**: 안쪽 루프에서 공통으로 읽는 값(여기서는 `x[k]`)을 한 번 load해서 k번 쓴다. 동시에 독립 accumulator가 k개 생겨서 의존성 체인도 끊긴다(E1 2절). J3의 "register blocking"이 바로 2차원 unroll-and-jam이다.

손계산 (GEMV, int16): 기준은 MAC 1번에 load 2번(A 하나, x 하나). 4행 jam이면 MAC 4번에 load 5번(A 넷, x 하나) → MAC당 1.25번, **load 37.5 % 감소**. 다만 A는 어차피 한 번씩 다 읽어야 하므로, A가 캐시 밖에서 오면(메모리 지붕) 이득이 작다.

```c
#include "bench.h"
#ifndef M
#define M 256
#endif
#ifndef K
#define K 2048
#endif
/* (a) 행마다 내적: x[]를 행마다 다시 읽는다 (MAC 1번에 load 2번) */
NOINLINE void gemv(const int16_t *A, const int16_t *x, int32_t *y) {
    for (int m = 0; m < M; m++) { int32_t s = 0; for (int k = 0; k < K; k++) s += A[m * K + k] * x[k]; y[m] = s; }
}
/* (b) unroll-and-jam: 바깥 m 루프를 4배 풀고, 풀린 4개의 안쪽 루프를 하나로 합친다(jam) */
NOINLINE void gemv_jam4(const int16_t *A, const int16_t *x, int32_t *y) {
    for (int m = 0; m < M; m += 4) {
        const int16_t *a0 = A + m * K, *a1 = a0 + K, *a2 = a1 + K, *a3 = a2 + K;
        int32_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
        for (int k = 0; k < K; k++) { int32_t xv = x[k];   /* x[k]를 한 번 읽어 4행에 재사용 */
            s0 += a0[k] * xv; s1 += a1[k] * xv; s2 += a2[k] * xv; s3 += a3[k] * xv; }
        y[m] = s0; y[m + 1] = s1; y[m + 2] = s2; y[m + 3] = s3;
    }
}
int main(void) {
    int16_t *A = malloc(M * K * 2), *x = malloc(K * 2); int32_t y1[M], y2[M];
    for (int i = 0; i < M * K; i++) A[i] = (int16_t)((i * 37) % 255 - 127);
    for (int k = 0; k < K; k++) x[k] = (int16_t)((k * 11) % 255 - 127);
    double t1, t2; BENCH(t1, 51, 200, gemv(A, x, y1)); BENCH(t2, 51, 200, gemv_jam4(A, x, y2));
    int bad = memcmp(y1, y2, sizeof y1) != 0; double macs = (double)M * K;
    printf("gemv        : %7.2f us  %5.1f GMAC/s\n", t1 * 1e6, macs / t1 / 1e9);
    printf("gemv jam x4 : %7.2f us  %5.1f GMAC/s  (%.2fx, mismatch=%d)\n", t2 * 1e6, macs / t2 / 1e9, t1 / t2, bad);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 -DM=32 -DK=512 t2_jam.c -o t2_small && ./t2_small   # A = 32 KiB, L1에 들어감
cc -std=c11 -Wall -Wextra -O2 t2_jam.c -o t2_big && ./t2_big                       # A = 1 MiB, L2
```

```text
gemv        :    0.53 us   30.6 GMAC/s
gemv jam x4 :    0.40 us   41.0 GMAC/s  (1.34x, mismatch=0)
gemv        :   23.65 us   22.2 GMAC/s
gemv jam x4 :   20.13 us   26.0 GMAC/s  (1.17x, mismatch=0)
```

출력에서 볼 것: A가 L1에 있으면 1.34×(세 번 중 한 번은 기준 버전이 잡음으로 느려 2.9×로 찍혔다 — median의 median을 보자), L2에서 오면 1.12~1.17×. **load를 줄이는 변환은 load가 병목일 때만** 크게 듣는다. 정수 누산이라 순서가 바뀌어도 결과는 bit-exact다(`mismatch=0`).

**Cortex-M4에서는**: SMLAD(16비트 MAC 2개)가 load 한 번으로 얻은 32비트 워드 두 개를 쓰므로, 4행 jam이면 x 워드 하나를 SMLAD 4번에 재사용한다. M4는 범용 레지스터가 13개(r0~r12) 정도라 **jam 폭은 레지스터가 정한다**: 4행이면 포인터 4 + 누산기 4 + x 1 + 카운터 = 10개로 들어가지만 8행이면 spill이 난다. 그래서 CMSIS-NN의 행렬 커널은 2행·4행 단위가 흔하다(J3 7절의 2×2 블록).

### 2.3 Interchange — 루프 순서를 바꿔 연속 접근으로

**무엇을**: 중첩 루프의 바깥·안쪽을 바꾼다. **왜**: 안쪽 루프가 메모리를 연속으로 걷게 해서 캐시 라인·SIMD를 다 쓰고, 안쪽 반복끼리 독립이 되게 한다. A1에서 matmul로 봤으니 여기서는 **열 합**(column sum)을 쓴다. 센서 프레임 R개 × 채널 C개 행렬에서 채널별 합을 구하는, 펌웨어에서 흔한 모양이다.

손계산: 2048 × 2048 float = 16 MiB. j-바깥/i-안쪽은 안쪽 반복마다 `C × 4 = 8 KiB`씩 건너뛴다 — 128 B 캐시 라인에서 4 B만 쓰고, 열 하나의 덧셈 2048개가 한 줄짜리 의존성 체인이다(fadd 지연 약 3 cycle). 체인만으로 하한이 `4M × 3 cycle ÷ 3.5 GHz ≈ 3.6 ms`. i-바깥/j-안쪽은 16 MiB를 순서대로 한 번 읽는 것이라 45 GB/s면 `16.8 MB ÷ 45 GB/s ≈ 0.37 ms`.

```c
#include "bench.h"
#define R 2048
#define C 2048
/* 행렬의 열 합(column sum) — 예: 센서 프레임 R개 × 채널 C개의 채널별 합 */
NOINLINE void colsum_ji(const float *A, float *s) {        /* j 바깥, i 안쪽: stride = C×4 바이트 */
    for (int j = 0; j < C; j++) { float acc = 0; for (int i = 0; i < R; i++) acc += A[i * C + j]; s[j] = acc; }
}
NOINLINE void colsum_ij(const float *A, float *s) {        /* 교환(interchange): i 바깥, j 안쪽: 연속 접근 */
    for (int j = 0; j < C; j++) s[j] = 0;
    for (int i = 0; i < R; i++) for (int j = 0; j < C; j++) s[j] += A[i * C + j];
}
int main(void) {
    float *A = malloc((size_t)R * C * 4), *s1 = malloc(C * 4), *s2 = malloc(C * 4);
    for (size_t i = 0; i < (size_t)R * C; i++) A[i] = (float)(i % 17) * 0.25f;
    double t1, t2; BENCH(t1, 11, 1, colsum_ji(A, s1)); BENCH(t2, 11, 1, colsum_ij(A, s2));
    int bad = memcmp(s1, s2, C * 4) != 0;     /* 각 열의 덧셈 순서(i=0..R-1)가 같으므로 bit-exact여야 한다 */
    printf("j-i (strided)    : %6.2f ms  %5.1f GB/s\n", t1 * 1e3, R * C * 4.0 / t1 / 1e9);
    printf("i-j (contiguous) : %6.2f ms  %5.1f GB/s  (%.1fx, mismatch=%d)\n", t2 * 1e3, R * C * 4.0 / t2 / 1e9, t1 / t2, bad);
    return 0;
}
```

```text
j-i (strided)    :  12.99 ms    1.3 GB/s
i-j (contiguous) :   0.39 ms   42.7 GB/s  (33.0x, mismatch=0)
```

출력에서 볼 것: 33배(세 번 33.0~37.4×). 0.39 ms는 손계산 0.37 ms와 거의 같다 — 메모리 지붕에 붙었다. 그리고 **bit-exact**다. 바꾼 뒤에도 각 열 안의 덧셈 순서(i = 0, 1, 2, …)는 그대로이기 때문이다. float에서 interchange가 안전한지 따지는 기준이 이것이다: **같은 누산기에 더해지는 순서가 바뀌는가?** 바뀌면 결과가 ulp 단위로 달라진다.

**Cortex-M4에서는**: 캐시 없는 SRAM이면 stride 자체의 벌칙은 거의 없다(어느 주소든 같은 대기). 하지만 i-j 순서는 `s[j]`를 매번 load·store하고, j-i 순서는 `acc`를 레지스터에 둔다 — M4에서는 오히려 **j-i가 메모리 접근이 적을 수 있다**. 캐시가 있는 M7·M55, 외부 PSRAM이면 다시 i-j가 이긴다. 같은 변환의 효과가 메모리 계층에 따라 뒤집히는 대표 예다.

### 2.4 Fusion vs fission — 합치면 바이트가, 쪼개면 레지스터가 준다

**Fusion(합치기)**: 같은 범위를 도는 루프 여러 개를 하나로. 중간 배열이 메모리를 왕복하지 않는다(C6의 op fusion, D3의 2.9×가 이것). **Fission(쪼개기, distribution)**: 루프 하나를 여러 개로. 벡터화되는 부분과 안 되는 부분을 분리하거나, 레지스터 압박을 줄인다.

손계산 (fusion): `y = relu(a·x + b)·s`를 3패스로 하면 원소당 read 4 B + write 4 B가 세 번 = 24 B. 1패스면 8 B. **3배 적은 바이트**. 배열이 DRAM에 있으면(16M 원소 = 64 MiB) 그대로 3배, L1에 있으면(4096 원소) 바이트보다 루프·load 명령 수가 이득을 정한다.

```c
#include "bench.h"
/* fusion: y = relu(a·x + b) · s 를 3패스(중간 배열 2개) vs 1패스로 */
NOINLINE void three_pass(const float *x, float *t1, float *t2, float *y, int n, float a, float b, float s) {
    for (int i = 0; i < n; i++) t1[i] = a * x[i] + b;
    for (int i = 0; i < n; i++) t2[i] = t1[i] > 0 ? t1[i] : 0;
    for (int i = 0; i < n; i++) y[i] = t2[i] * s;
}
NOINLINE void fused(const float *x, float *y, int n, float a, float b, float s) {
    for (int i = 0; i < n; i++) { float t = a * x[i] + b; t = t > 0 ? t : 0; y[i] = t * s; }
}
/* fission: 벡터화 가능한 부분 + 루프 간 의존(누적합)이 한 루프에 섞여 있으면 전체가 스칼라가 된다 */
NOINLINE void mixed(const float *x, float *y, float *cum, int n, float g) {
    float c = 0; for (int i = 0; i < n; i++) { y[i] = x[i] * x[i] * g; c += y[i]; cum[i] = c; }
}
NOINLINE void split(const float *x, float *y, float *cum, int n, float g) {
    for (int i = 0; i < n; i++) y[i] = x[i] * x[i] * g;          /* 벡터화된다 */
    float c = 0; for (int i = 0; i < n; i++) { c += y[i]; cum[i] = c; }   /* 직렬 의존은 여기만 */
}
/* main: n = 4096(L1)과 16M(DRAM) 각각에 대해 네 함수를 BENCH로 재고, 결과를 memcmp로 비교해 한 줄씩 출력한다 */
```

```text
n=4096      3-pass      0.7 us | fused      0.3 us (2.12x, mismatch=0) | mixed      4.7 us | fission      4.8 us (0.99x, mismatch=0)
n=16777216  3-pass   6440.0 us | fused   3101.0 us (2.08x, mismatch=0) | mixed  19935.0 us | fission  20871.0 us (0.96x, mismatch=0)
```

출력에서 볼 것: fusion은 L1 크기에서 2.1~2.3×, DRAM 크기에서 2.1~3.9×(세 번 실행; DRAM 측정이 가장 시끄럽다). fission은 **0.91~0.99×로 오히려 약간 느렸다**. 이유: `mixed`의 시간은 누적합 체인(fadd 지연 × n)이 정하는데, M2의 OoO 엔진이 `x·x·g` 계산을 그 체인 뒤에 이미 숨기고 있었다. 쪼개면 y를 한 번 더 읽는 패스만 늘어난다. **"벡터화가 되니까 빨라지겠지"는 병목이 그 부분일 때만 맞다.**

**Cortex-M4에서는 fission이 이긴다 — 레지스터 압박.** 2.7절의 6축 IMU 보정 루프(입력 6 + 출력 6 스트림 + 상수 12개)를 한 루프로 두면 M4의 레지스터가 모자라 spill이 난다. 채널마다 루프를 쪼갠 버전과 비교했다.

```c
/* fission: 6채널을 한 루프 대신 채널마다 한 루프 (restrict 없음) */
static void cal1(const float *in, float *out, const float *b, const float *g, int n) {
    for (int i = 0; i < n; i++) out[i] = (in[i] - *b) * *g;
}
void cal_fission(const float *const in[6], float *const out[6], const float *b, const float *g, int n) {
    for (int c = 0; c < 6; c++) cal1(in[c], out[c], b + c, g + c, n);
}
```

```text
.LBB0_4    insns=109 loads=53 stores=16 spill/reload=21     (한 루프, 6채널: 샘플 2개)
.LBB0_3    insns=29 loads=12 stores=4 spill/reload=0        (fission, 1채널: 샘플 4개)
```

출력에서 볼 것: 한 루프 버전은 샘플당 명령 54.5개·load 26.5개·spill 다수, fission 버전은 채널당 샘플 4개에 29개 → 6채널이면 샘플당 43.5개, spill 0. 같은 변환이 **OoO 코어에서는 손해, in-order MCU에서는 이득**이다.

### 2.5 Strip-mining / tiling — 중간 결과를 캐시 크기로 자르기

**Strip-mining**: 긴 루프 하나를 "길이 S짜리 조각 × 조각 수"의 2중 루프로 바꾼다. 그 자체로는 아무것도 안 바뀌지만, 조각 단위로 **다른 루프와 fusion**하면 중간 결과가 조각 크기만큼만 생겨 캐시(또는 SRAM)에 머문다. 다차원으로 하면 tiling(cache blocking)이다. conv에서의 tiling은 J3 8절에 있으니, 여기서는 이미지 처리·센서 전처리에서 흔한 **분리형 필터**(separable filter: 2D 5×5 가우시안 = 가로 5탭 후 세로 5탭)를 쓴다.

손계산 (4096 × 4096 float 이미지):

```
전체 가로 패스 → 전체 세로 패스 : 중간 tmp = 4096 × 4096 × 4 B = 64 MiB → DRAM 왕복
strip S행씩: 가로 S+4행 → 세로 S행 : tmp = (S+4) × 4096 × 4 B
   S = 128 → 2.1 MiB (L2 16 MiB 안),  S = 32 → 576 KiB,  S = 8 → 192 KiB
대가: strip마다 위아래 4행을 가로 패스로 다시 계산 → 중복 비율 4/S (S=8이면 50 %)
```

```c
#include "bench.h"
#define H 4096
#define W 4096
static const float k5[5] = {0.0625f, 0.25f, 0.375f, 0.25f, 0.0625f};   /* 1-4-6-4-1 / 16 */
/* 가로 5탭: in의 행 r0..r1-1 → tmp (행 단위) */
static void hpass(const float *in, float *tmp, int r0, int r1) {
    for (int r = r0; r < r1; r++) { const float *p = in + (size_t)r * W; float *q = tmp + (size_t)(r - r0) * W;
        for (int c = 0; c < W - 4; c++) q[c] = k5[0]*p[c] + k5[1]*p[c+1] + k5[2]*p[c+2] + k5[3]*p[c+3] + k5[4]*p[c+4]; }
}
/* 세로 5탭: tmp의 행 → out 행 o0..o1-1 */
static void vpass(const float *tmp, float *out, int o0, int o1, int tbase) {
    for (int o = o0; o < o1; o++) { const float *t = tmp + (size_t)(o - tbase) * W; float *q = out + (size_t)o * W;
        for (int c = 0; c < W - 4; c++) q[c] = k5[0]*t[c] + k5[1]*t[c+W] + k5[2]*t[c+2*W] + k5[3]*t[c+3*W] + k5[4]*t[c+4*W]; }
}
/* S = 출력 행 strip 높이. S = H-4 이면 "전체 가로 패스 → 전체 세로 패스" */
NOINLINE void sep_blur(const float *in, float *tmp, float *out, int S) {
    for (int o0 = 0; o0 < H - 4; o0 += S) { int o1 = o0 + S < H - 4 ? o0 + S : H - 4;
        hpass(in, tmp, o0, o1 + 4);          /* strip에 필요한 S+4 행만 가로 필터 */
        vpass(tmp, out, o0, o1, o0); }
}
/* main: 4096×4096 난수 이미지로 S = 4092(전체), 512, 128, 32, 8, 2를 차례로 BENCH하고, S = 4092 결과와 memcmp로 bit-exact를 확인한다 */
```

```text
strip S=4092  tmp=   65536 KiB   11.69 ms  redundant hpass=  0.1%  bit-exact=1
strip S= 512  tmp=    8256 KiB    8.93 ms  redundant hpass=  0.8%  bit-exact=1
strip S= 128  tmp=    2112 KiB    7.59 ms  redundant hpass=  3.1%  bit-exact=1
strip S=  32  tmp=     576 KiB    7.90 ms  redundant hpass= 12.5%  bit-exact=1
strip S=   8  tmp=     192 KiB    9.34 ms  redundant hpass= 50.0%  bit-exact=1
strip S=   2  tmp=      96 KiB   16.39 ms  redundant hpass=200.0%  bit-exact=1
```

출력에서 볼 것: **U자 곡선**이다. S가 크면 tmp가 캐시를 넘쳐 DRAM을 왕복하고, S가 작으면 중복 계산(4/S)과 strip 오버헤드가 커진다. 이 Mac에서는 S = 32~128이 바닥이고 전체 대비 1.2~1.5×(세 번 실행 기준). 모든 S에서 bit-exact다 — 같은 출력 픽셀은 같은 순서로 계산되기 때문이다.

다음 단계: 중복도 없애려면 **line buffer**(최근 5행만 담는 링버퍼)로 가로 결과를 행 단위로 굴린다. ISP·오디오 프레임 처리의 "스트리밍 conv"와 같은 구조이고, MCU에서는 SRAM 5행만으로 큰 이미지를 처리할 수 있게 해 준다.

**Cortex-M에서는**: 캐시가 없으면 strip 크기는 **SRAM(TCM) 예산**으로 정한다. 예: 행 320 픽셀 int16, 5×5 필터면 line buffer는 `5 × 320 × 2 B = 3.2 KB`. 입력은 DMA로 strip 단위로 들여온다(5절).

### 2.6 Software pipelining — 다음 반복의 load를 미리

**무엇을**: 반복 i의 연산과 반복 i+1의 load를 겹치도록 루프를 재배치한다. 앞에 prologue(첫 load), 뒤에 epilogue(마지막 연산)가 붙는다. **왜**: in-order 코어에서 load 결과를 바로 쓰면 그만큼 멈춘다(load-use stall). OoO 코어는 하드웨어가 알아서 겹치므로 효과가 거의 없다. VLIW DSP(Hexagon, Xtensa HiFi)에서는 컴파일러가 이걸 "modulo scheduling"으로 하고, 안 될 때 사람이 손으로 한다.

예제는 int8 activation LUT(B1, J3의 softmax LUT와 같은 모양): `out[i] = lut[in[i]]` — load → 그 결과를 주소로 쓰는 load → store.

```c
#include "bench.h"
/* int8 activation LUT: out[i] = lut[(uint8_t)in[i]] — load → 의존 load → store 체인 */
NOINLINE void lut_plain(const int8_t *in, int8_t *out, const int8_t *lut, int n) {
    for (int i = 0; i < n; i++) out[i] = lut[(uint8_t)in[i]];
}
/* 소프트웨어 파이프라이닝: 다음 반복의 입력을 미리 읽어 두고(prologue), 루프 안에서는
   "이번 것 사용 + 다음 것 load"를 겹친다, 마지막 하나는 epilogue */
NOINLINE void lut_swp(const int8_t *in, int8_t *out, const int8_t *lut, int n) {
    uint8_t cur = (uint8_t)in[0];                       /* prologue */
    for (int i = 0; i < n - 1; i++) { uint8_t nxt = (uint8_t)in[i + 1]; out[i] = lut[cur]; cur = nxt; }
    out[n - 1] = lut[cur];                              /* epilogue */
}
/* main: 64K개 난수 입력과 LUT(i/2 − 64)로 두 함수를 BENCH하고 출력을 memcmp로 비교한다 */
```

```text
plain :   20.8 us (0.32 ns/elem)
swp   :   19.4 us (0.30 ns/elem)  ratio=1.07x  mismatch=0
```

출력에서 볼 것: M2에서는 1.01~1.07× — 잡음 수준이다. OoO가 이미 수십 개의 load를 동시에 띄우고 있다.

**Cortex-M4에서는** 두 버전의 루프 몸통 명령 수가 같다(둘 다 원소 4개에 18개). 다른 것은 **순서**다. plain 버전은 `ldrb r7, [r6, #3]` 다음 줄이 바로 `ldrb r7, [r2, r7]` — 방금 읽은 값을 다음 load의 주소로 쓴다. swp 버전은 이전 반복에서 읽어 둔 인덱스로 LUT를 읽고, 그 사이에 독립된 load가 끼어 있다.

```text
plain (.LBB0_3)            swp (.LBB1_5)
  ldrb  r7, [r6, #3]         ldrb  r4, [r2, r4]     ← 이전 반복에서 읽어 둔 r4
  add.w r3, r8, r4           ldrb  r7, [r5, #1]     ← 다음 원소 미리 load
  ldrb  r7, [r2, r7]  ←의존   add.w r6, r10, r3
  adds  r4, #4               strb  r4, [r6, #-1]
  strb  r7, [r3, #3]         ldrb  r4, [r2, r7]
```

plain 버전은 앞 load의 결과가 나와야 다음 load의 주소를 만들 수 있다(load-use 의존). M4는 연속된 독립 load를 파이프라인으로 겹칠 수 있지만, 의존 load는 결과를 기다려야 한다. 그래서 swp 버전이 원소당 1 cycle 안팎 빠를 것으로 **예상**하지만, 정확한 stall 수는 TRM의 파이프라이닝 조건과 메모리 대기 상태에 달려 있고 이 노트에서는 실기로 재지 않았다. 보드에서 DWT CYCCNT로 확인할 것(K2). 실무 교훈: **in-order 코어·VLIW DSP에서만 손으로 할 가치가 있고, 그 전에 컴파일러 출력부터 본다.**

### 2.7 LICM과 aliasing — 그리고 `restrict`

**LICM(loop-invariant code motion)**: 반복마다 같은 값을 내는 계산·load를 루프 밖으로 뺀다. 컴파일러가 기본으로 하지만, **포인터 aliasing** 때문에 못 하는 경우가 많다. 루프 안에서 `out[i]`에 store하면, 컴파일러는 그 store가 `*p`(파라미터 구조체, 계수 배열)를 바꿨을 수도 있다고 가정하고 매번 다시 읽는다. 특히 `int8_t`·`uint8_t`(=char 계열) 포인터는 C 규칙상 **무엇과도 alias할 수 있어서** int8 커널에서 자주 터진다.

해결책 세 가지: (1) 계수를 지역 변수로 복사(손 LICM), (2) `restrict`로 "안 겹친다"고 약속, (3) 출력에 바로 누적하지 말고 지역 누산기에 모은 뒤 마지막에 한 번 store(scalar replacement).

이 Mac에서 먼저 확인한 사실: Apple clang은 **런타임 alias 검사**(두 포인터 범위가 겹치는지 루프 앞에서 비교하고, 안 겹치면 벡터 버전으로 가는 versioning)를 넣어서, int8 GEMV·requant·6스트림 보정 루프 모두 `restrict` 없이도 벡터화했다. `restrict`를 붙인 버전과의 차이는 0.9~1.3×로 잡음 안이었다(`-Rpass=loop-vectorize` 리포트로 세 버전 모두 "vectorized loop" 확인). 그래서 효과는 M4 어셈블리에서 본다.

```c
void cal_plain(const float *ax, const float *ay, const float *az, const float *gx, const float *gy, const float *gz,
               float *oax, float *oay, float *oaz, float *ogx, float *ogy, float *ogz, const float *b, const float *g, int n) {
    for (int i = 0; i < n; i++) {
        oax[i] = (ax[i] - b[0]) * g[0]; oay[i] = (ay[i] - b[1]) * g[1]; oaz[i] = (az[i] - b[2]) * g[2];
        ogx[i] = (gx[i] - b[3]) * g[3]; ogy[i] = (gy[i] - b[4]) * g[4]; ogz[i] = (gz[i] - b[5]) * g[5]; }
}
void cal_restrict(const float *restrict ax, const float *restrict ay, const float *restrict az,
                  const float *restrict gx, const float *restrict gy, const float *restrict gz,
                  float *restrict oax, float *restrict oay, float *restrict oaz, float *restrict ogx, float *restrict ogy,
                  float *restrict ogz, const float *restrict b, const float *restrict g, int n) {
    for (int i = 0; i < n; i++) {
        oax[i] = (ax[i] - b[0]) * g[0]; oay[i] = (ay[i] - b[1]) * g[1]; oaz[i] = (az[i] - b[2]) * g[2];
        ogx[i] = (gx[i] - b[3]) * g[3]; ogy[i] = (gy[i] - b[4]) * g[4]; ogz[i] = (gz[i] - b[5]) * g[5]; }
}
```

```sh
./m4.sh k_cal.c k_cal_m4.s
python3 loopstat.py k_cal_m4.s .LBB0_4    # cal_plain 루프
python3 loopstat.py k_cal_m4.s .LBB1_3    # cal_restrict 루프
```

```text
.LBB0_4    insns=109 loads=53 stores=16 spill/reload=21
.LBB1_3    insns=119 loads=27 stores=24 spill/reload=0
```

출력에서 볼 것: store 개수로 보면 plain 몸통은 샘플 2개(벡터 store 12개 = 6채널 × 2), restrict 몸통은 샘플 4개(24개)를 처리한다. 샘플당으로 나누면:

| 버전 | 샘플당 명령 | 샘플당 load | spill/reload |
|---|---|---|---|
| plain (restrict 없음) | 54.5 | 26.5 | 21 (몸통당) |
| fission (2.4절) | 43.5 | 18 | 0 |
| restrict | 29.75 | 6.75 | 0 |

plain은 store마다 `b[k]`, `g[k]`를 다시 읽고(12개 계수 × 매 샘플), 포인터 12개를 레지스터에 다 못 올려 스택에 spill한다. restrict는 계수 12개를 FPU 레지스터(s0~s31 중)에 루프 내내 올려 둔다. **M4에서 약 1.8배 적은 명령**이다. 펌웨어 규칙으로 정리하면: 커널 함수의 포인터 인자에는 `restrict`를, 파라미터 구조체 필드는 루프 전에 지역 변수로.

함정: `restrict`는 **약속**이다. 실제로 겹치는 버퍼(in-place 연산: `out == in`)에 `restrict` 함수를 부르면 정의되지 않은 동작이고, 최적화 레벨에 따라 결과가 달라지는 최악의 버그가 된다. in-place를 허용할 커널이면 `restrict`를 빼거나 API 문서에 "in-place 금지"를 명시한다.

### 2.8 Strength reduction — 비싼 연산을 싼 연산으로

**무엇을**: 루프 안의 비싼 연산(나눗셈, 나머지, 곱셈)을 싼 연산(덧셈, 비교, 시프트)으로 바꾼다. 고전적인 예는 `a[i × stride]`의 곱셈을 포인터 증가로 바꾸는 것인데, 이건 컴파일러가 이미 한다. 컴파일러가 **못 하는** 대표 예는 런타임 길이의 **링버퍼 인덱스** `(head + k) % N`이다 — N이 2의 거듭제곱이 아니면 매번 나눗셈이다. 오디오·IMU 히스토리 버퍼에서 늘 나온다.

세 버전을 비교한다: (a) 매번 `%`, (b) 인덱스를 1씩 늘리고 N이면 0으로(비교+분기로 대체 — 교과서적 strength reduction), (c) 버퍼를 **두 개의 연속 구간으로 쪼개서**(index set splitting) 각 구간을 그냥 선형 루프로.

```c
#include "bench.h"
/* 링버퍼(길이 N, 2의 거듭제곱 아님)에서 최근 L개 샘플의 가중합 — 오디오/IMU 히스토리 */
NOINLINE int32_t ring_mod(const int16_t *buf, const int16_t *w, int head, int N, int L) {
    int32_t s = 0; for (int k = 0; k < L; k++) s += w[k] * buf[(head + k) % N];      /* 매 반복 나눗셈 */
    return s;
}
NOINLINE int32_t ring_wrap(const int16_t *buf, const int16_t *w, int head, int N, int L) {
    int32_t s = 0; int idx = head % N;                                                 /* 나눗셈은 한 번 */
    for (int k = 0; k < L; k++) { s += w[k] * buf[idx]; if (++idx == N) idx = 0; }      /* 비교+분기로 대체 */
    return s;
}
NOINLINE int32_t ring_split(const int16_t *buf, const int16_t *w, int head, int N, int L) {
    int32_t s = 0; int idx = head % N, n1 = N - idx < L ? N - idx : L;                 /* 두 구간으로 쪼갠다 */
    for (int k = 0; k < n1; k++) s += w[k] * buf[idx + k];                             /* 연속 → 벡터화 */
    for (int k = n1; k < L; k++) s += w[k] * buf[k - n1];
    return s;
}
/* main: N = 1000(volatile로 런타임 값), L = 800, head를 37씩 옮기며 세 함수를 2000번씩 BENCH하고 합이 같은지 확인한다 */
```

```text
modulo :    607 ns
wrap   :    873 ns (0.7x)
split  :     89 ns (6.9x)  same=1
```

출력에서 볼 것: 교과서적인 "나눗셈 → 비교"는 M2에서 **오히려 0.7배로 느려졌다**. `%` 버전은 반복끼리 독립이라 OoO가 나눗셈 여러 개를 겹쳐 돌리지만, wrap 버전은 `idx`가 반복마다 앞 값에 의존하는 체인(증가 → 비교 → 선택)이 됐기 때문이다. 구간 분할은 7~15×(세 번 실행) — 나눗셈도, 체인도 없고 두 구간 모두 벡터화된다. `same=1`: 정수라 세 버전 결과가 같다.

**Cortex-M4에서는** 셋 모두 몸통 하나가 원소 4개를 처리한다.

```text
.LBB0_5    insns=33 loads=10 stores=1 spill/reload=3    (modulo: sdiv 4개 + mls 4개)
.LBB1_5    insns=31 loads=8 stores=0 spill/reload=0     (wrap: subs + it ne + movne)
.LBB2_5    insns=20 loads=8 stores=0 spill/reload=0     (split 첫 구간)
.LBB2_18   insns=14 loads=8 stores=0 spill/reload=0     (split 둘째 구간)
```

M4의 `SDIV`는 피연산자에 따라 2~12 cycle이다(Cortex-M4 TRM). modulo 버전은 원소마다 sdiv + mls(곱해서 빼기)가 붙고 spill까지 있다. wrap은 나눗셈은 없지만 명령 수가 비슷하고, split이 원소당 3.5~5개로 가장 적다. **in-order 코어에서는 나눗셈 제거가 확실히 이득이고, 구간 분할은 어디서나 이득**이다. 더 단순한 해법: 버퍼 길이를 2의 거듭제곱으로 잡으면 `% N`이 `& (N − 1)` 한 명령이 된다(SRAM이 허락하면 이게 제일 좋다). 또는 버퍼를 L만큼 **두 번 써서**(mirror) 항상 연속 구간이 되게 한다.

### 2.9 카탈로그 요약

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 412"><text x="10" y="20" font-size="14" text-anchor="start">루프 변환별 속도 향상 — 이 Mac(M2, -O2) 실측 median, 로그 축</text><line x1="230.0" y1="34" x2="230.0" y2="352" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="230.0" y="364" font-size="12" text-anchor="middle">0.5×</text><line x1="293.2" y1="34" x2="293.2" y2="352" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="293.2" y="364" font-size="12" text-anchor="middle">1×</text><line x1="356.4" y1="34" x2="356.4" y2="352" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="356.4" y="364" font-size="12" text-anchor="middle">2×</text><line x1="440.0" y1="34" x2="440.0" y2="352" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="440.0" y="364" font-size="12" text-anchor="middle">5×</text>
<line x1="503.2" y1="34" x2="503.2" y2="352" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="503.2" y="364" font-size="12" text-anchor="middle">10×</text><line x1="566.4" y1="34" x2="566.4" y2="352" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="566.4" y="364" font-size="12" text-anchor="middle">20×</text><line x1="650.0" y1="34" x2="650.0" y2="352" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="650.0" y="364" font-size="12" text-anchor="middle">50×</text><text x="224" y="55" font-size="12" text-anchor="end">interchange (열 합)</text><rect x="293.2" y="44" width="319.9" height="15" fill="#4a7bd0" fill-opacity="0.8"/><text x="616.7" y="56" font-size="12" text-anchor="start">33.2×</text><text x="224" y="79" font-size="12" text-anchor="end">strength red. → 구간 분할</text>
<rect x="293.2" y="68" width="242.5" height="15" fill="#4a7bd0" fill-opacity="0.8"/><text x="539.2" y="80" font-size="12" text-anchor="start">14.2×</text><text x="224" y="103" font-size="12" text-anchor="end">full unroll (K 상수, FIR)</text><rect x="293.2" y="92" width="213.2" height="15" fill="#4a7bd0" fill-opacity="0.8"/><text x="509.9" y="104" font-size="12" text-anchor="start">10.3×</text><text x="224" y="127" font-size="12" text-anchor="end">AoS → SoA (energy, 4M)</text><rect x="293.2" y="116" width="176.7" height="15" fill="#3f9a6b" fill-opacity="0.8"/><text x="473.4" y="128" font-size="12" text-anchor="start">6.9×</text><text x="224" y="151" font-size="12" text-anchor="end">fusion 3패스→1 (16M)</text><rect x="293.2" y="140" width="102.5" height="15" fill="#4a7bd0" fill-opacity="0.8"/><text x="399.2" y="152" font-size="12" text-anchor="start">3.06×</text>
<text x="224" y="175" font-size="12" text-anchor="end">역수 곱 (나눗셈 대체)</text><rect x="293.2" y="164" width="91.1" height="15" fill="#e08a3c" fill-opacity="0.8"/><text x="387.8" y="176" font-size="12" text-anchor="start">2.7×</text><text x="224" y="199" font-size="12" text-anchor="end">strip-mining S=128</text><rect x="293.2" y="188" width="36.3" height="15" fill="#4a7bd0" fill-opacity="0.8"/><text x="333.0" y="200" font-size="12" text-anchor="start">1.48×</text><text x="224" y="223" font-size="12" text-anchor="end">unroll-and-jam (L1 GEMV)</text><rect x="293.2" y="212" width="27.2" height="15" fill="#4a7bd0" fill-opacity="0.8"/><text x="323.9" y="224" font-size="12" text-anchor="start">1.34×</text><text x="224" y="247" font-size="12" text-anchor="end">손 unroll ×4 (add)</text><rect x="293.2" y="236" width="11.6" height="15" fill="#888" fill-opacity="0.8"/>
<text x="308.4" y="248" font-size="12" text-anchor="start">1.13×</text><text x="224" y="271" font-size="12" text-anchor="end">software pipelining (LUT)</text><rect x="293.2" y="260" width="2.3" height="15" fill="#888" fill-opacity="0.8"/><text x="299.0" y="272" font-size="12" text-anchor="start">1.02×</text><text x="224" y="295" font-size="12" text-anchor="end">restrict (6스트림)</text><rect x="293.2" y="284" width="0.5" height="15" fill="#888" fill-opacity="0.8"/><text x="297.2" y="296" font-size="12" text-anchor="start">1×</text><text x="224" y="319" font-size="12" text-anchor="end">fission (누적합)</text><rect x="286.6" y="308" width="7.1" height="15" fill="#d0564a" fill-opacity="0.8"/><text x="297.2" y="320" font-size="12" text-anchor="start">0.93×</text><text x="224" y="343" font-size="12" text-anchor="end">% 대신 비교·분기</text>
<rect x="260.7" y="332" width="33.0" height="15" fill="#d0564a" fill-opacity="0.8"/><text x="297.2" y="344" font-size="12" text-anchor="start">0.7×</text><line x1="293.2" y1="34" x2="293.2" y2="352" stroke="currentColor"/><text x="10" y="388" font-size="12" text-anchor="start">파랑 = 루프 변환, 초록 = 데이터 레이아웃, 주황 = 고정소수점 트릭, 회색 = 효과 없음(잡음 수준), 빨강 = 오히려 느려짐</text></svg>
```

그림 2 — 이 절의 측정 요약(세 번 실행의 median). 로그 축이라 1× 왼쪽 막대는 느려진 것이다. 큰 이득(10× 이상)은 모두 "벡터화를 가능하게 만든" 변환이고, 손 unroll·software pipelining·restrict는 OoO 코어에서는 효과가 없었다.

| 변환 | 줄이는 것 | M2 (OoO, NEON) 실측 | Cortex-M4 (in-order) 예상·근거 | 언제 쓰나 |
|---|---|---|---|---|
| unroll (손) | 분기·카운터 | 1.06~1.16× (컴파일러가 이미 함) | 루프 오버헤드 3~4 cycle/반복 절약. `-O2`도 하므로 asm 확인 후 | 반복 수를 컴파일러가 모를 때 |
| full unroll (상수 K) | 짧은 안쪽 루프 | 10.3× (바깥 루프 벡터화) | 출력당 22.5 → 14.5 명령 (restrict와 함께) | 탭·커널 크기가 정해진 경로 |
| unroll-and-jam | 공유 load, 의존 체인 | 1.17~1.34× | SMLAD 재사용. jam 폭 ≤ 레지스터 | GEMV·conv 출력 블로킹 |
| interchange | stride·의존 체인 | 33× | 캐시 없는 SRAM이면 이득 작음, 오히려 반대일 수도 | 안쪽 루프가 연속 접근이 아닐 때 |
| fusion | 중간 배열 바이트 | 2.1~3.9× | SRAM 왕복·DMA 감소 | elementwise 체인, conv+bias+act |
| fission | 레지스터 압박 | 0.91~0.99× (손해) | 샘플당 54.5 → 43.5 명령, spill 제거 | 스트림이 많은 루프, 레지스터 적은 코어 |
| strip-mining | 중간 결과 크기 | 1.2~1.5× (U자) | strip = SRAM 예산 | 다단계 필터, line buffer |
| software pipelining | load-use stall | 1.01~1.07× | 의존 load 분리 (실측 필요) | in-order, VLIW DSP |
| LICM·restrict | 재load, spill | 1.0× (런타임 alias 검사) | 샘플당 54.5 → 29.75 명령 | 커널 포인터 인자 전부 |
| strength reduction | 나눗셈 | `%`→비교 0.7×, 구간 분할 7~15× | SDIV 2~12 cycle 제거 | 링버퍼, 런타임 나눗셈 |

---

## 3. 데이터 레이아웃 — 같은 바이트, 다른 순서

루프 변환이 "어떤 순서로 걷나"를 바꾼다면, 레이아웃 변환은 "바이트가 어떤 순서로 놓여 있나"를 바꾼다. 둘은 짝이다: 좋은 루프 순서는 레이아웃이 받쳐 줘야 연속 접근이 되고, 좋은 레이아웃은 그걸 쓰는 루프가 있어야 의미가 있다.

### 3.1 AoS vs SoA — 센서 프레임

**AoS(Array of Structures)**: 샘플 하나를 구조체로, 그 배열. 펌웨어가 센서 FIFO를 읽어 그대로 쌓으면 자연스럽게 이 모양이다. **SoA(Structure of Arrays)**: 필드마다 배열. ML 전처리·DSP는 대부분 "한 필드를 길게" 처리하므로 SoA가 SIMD에 맞는다.

손계산: IMU 샘플 `{ax, ay, az, gx, gy, gz: int16, ts: uint32}` = 16 B. 가속도 크기² 합(착용·움직임 감지용 energy)은 필드 3개 = 6 B만 필요하다. AoS로 읽으면 캐시 라인 단위로 16 B를 다 가져오므로 **필요한 바이트의 2.7배**를 옮긴다. `az`만 쓰는 작업이면 8배다. 그리고 AoS에서 `ax`들은 16 B 간격이라 한 번의 벡터 load로 8개를 모을 수 없다.

```c
#include "bench.h"
/* IMU 샘플 하나 = 16바이트 (AoS) */
typedef struct { int16_t ax, ay, az, gx, gy, gz; uint32_t ts; } imu_aos_t;
/* 같은 데이터를 필드별 배열로 (SoA) */
typedef struct { int16_t *ax, *ay, *az, *gx, *gy, *gz; uint32_t *ts; } imu_soa_t;
/* 작업 A: 가속도 크기² 합 (필드 3개 사용) — 착용/움직임 감지용 energy */
NOINLINE int64_t energy_aos(const imu_aos_t *s, int n) { int64_t e = 0;
    for (int i = 0; i < n; i++) e += s[i].ax * s[i].ax + s[i].ay * s[i].ay + s[i].az * s[i].az; return e; }
NOINLINE int64_t energy_soa(const imu_soa_t *s, int n) { int64_t e = 0;
    for (int i = 0; i < n; i++) e += s->ax[i] * s->ax[i] + s->ay[i] * s->ay[i] + s->az[i] * s->az[i]; return e; }
/* 작업 B: az 하나만 평균 (필드 1개 사용) */
NOINLINE int64_t sumz_aos(const imu_aos_t *s, int n) { int64_t e = 0; for (int i = 0; i < n; i++) e += s[i].az; return e; }
NOINLINE int64_t sumz_soa(const imu_soa_t *s, int n) { int64_t e = 0; for (int i = 0; i < n; i++) e += s->az[i]; return e; }
```

main은 같은 난수 데이터를 두 레이아웃에 채우고, 결과가 같은지 확인한 뒤 `BENCH`로 잰다(1024 샘플과 4M 샘플). 0.2절에서 말한 `escape()`가 바로 이 예제에서 필요했다 — 없으면 AoS 쪽 순수 함수 호출이 루프 밖으로 빠져 0.00 µs가 찍혔다.

```text
n=1024     energy AoS     0.86 us SoA     0.12 us (7.1x, same=1) | az-sum AoS     0.15 us SoA     0.07 us (2.1x)
n=4194304  energy AoS  3923.50 us SoA   567.00 us (6.9x, same=1) | az-sum AoS  1539.50 us SoA   610.50 us (2.5x)
```

출력에서 볼 것: energy는 L1 크기(1024 샘플 = 16 KiB)에서도 7.1배 — 바이트 문제가 아니라 **벡터화 문제**다. `-Rpass=loop-vectorize` 리포트를 보면 SoA 루프는 "vectorized loop (vectorization width: 8, interleaved count: 4)"이고, AoS 루프는 "interleaved loop"뿐(스칼라 4중 interleave)이다. 16 B 간격으로 흩어진 `ax`를 벡터 하나로 모을 수 없기 때문이다. 4M 샘플(64 MiB)에서도 6.8~7.8×(세 번 실행). az 하나만 쓰는 작업은 큰 배열에서 2.5~4.2×, 1024 샘플에서는 0.9~2.1×였다(0.1 µs 수준이라 잡음이 크다).

언제 AoS가 낫나: 샘플 하나의 **모든 필드를 함께** 쓰는 작업(예: 샘플마다 6축 보정 + 타임스탬프 기록 + 패킷화)이고 SIMD를 안 쓸 때, 또는 센서 FIFO에서 DMA로 그대로 들어오는 경우(변환 비용 자체를 아끼고 싶을 때). 절충안 **AoSoA**는 "8샘플 묶음 안에서는 SoA, 묶음끼리는 AoS"로, 벡터 폭만큼만 SoA로 만든다. 3.2절의 NCHWc가 같은 발상이다.

**MCU 연결**: Hark 같은 웨어러블이라면 IMU FIFO를 DMA로 읽어 오면 AoS(센서 레지스터 순서)로 들어온다. 전처리 첫 단계에서 SoA int16 링버퍼(축별)로 **한 번** 풀어 두면, 이후의 필터·특징 추출·모델 입력이 모두 연속 접근이 된다. Cortex-M4 DSP 확장의 SMLAD는 같은 필드 두 개(16비트 × 2)를 32비트 워드 하나로 받아야 하므로, AoS에서는 SMLAD를 쓰려면 먼저 PKHBT 같은 명령으로 짝을 맞춰야 한다 — SoA가 더 직접적이다.

### 3.2 NCHW vs NHWC vs NCHWc

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 290"><text x="10" y="20" font-size="14" text-anchor="start">같은 텐서(C=8 채널, 픽셀 p0~p3)의 메모리 순서 — 주소 0부터 16칸</text><text x="10" y="62" font-size="13" text-anchor="start">NCHW</text><rect x="80" y="40" width="35" height="32" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/><text x="97.5" y="61" font-size="12" text-anchor="middle">c0p0</text><rect x="117" y="40" width="35" height="32" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/><text x="134.5" y="61" font-size="12" text-anchor="middle">c0p1</text><rect x="154" y="40" width="35" height="32" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/><text x="171.5" y="61" font-size="12" text-anchor="middle">c0p2</text><rect x="191" y="40" width="35" height="32" fill="#d0564a" fill-opacity="0.25" stroke="#d0564a"/><text x="208.5" y="61" font-size="12" text-anchor="middle">c0p3</text>
<rect x="228" y="40" width="35" height="32" fill="#4a7bd0" fill-opacity="0.32" stroke="#4a7bd0"/><text x="245.5" y="61" font-size="12" text-anchor="middle">c1p0</text><rect x="265" y="40" width="35" height="32" fill="#e08a3c" fill-opacity="0.32" stroke="#e08a3c"/><text x="282.5" y="61" font-size="12" text-anchor="middle">c1p1</text><rect x="302" y="40" width="35" height="32" fill="#3f9a6b" fill-opacity="0.32" stroke="#3f9a6b"/><text x="319.5" y="61" font-size="12" text-anchor="middle">c1p2</text><rect x="339" y="40" width="35" height="32" fill="#d0564a" fill-opacity="0.32" stroke="#d0564a"/><text x="356.5" y="61" font-size="12" text-anchor="middle">c1p3</text><rect x="376" y="40" width="35" height="32" fill="#4a7bd0" fill-opacity="0.39" stroke="#4a7bd0"/><text x="393.5" y="61" font-size="12" text-anchor="middle">c2p0</text>
<rect x="413" y="40" width="35" height="32" fill="#e08a3c" fill-opacity="0.39" stroke="#e08a3c"/><text x="430.5" y="61" font-size="12" text-anchor="middle">c2p1</text><rect x="450" y="40" width="35" height="32" fill="#3f9a6b" fill-opacity="0.39" stroke="#3f9a6b"/><text x="467.5" y="61" font-size="12" text-anchor="middle">c2p2</text><rect x="487" y="40" width="35" height="32" fill="#d0564a" fill-opacity="0.39" stroke="#d0564a"/><text x="504.5" y="61" font-size="12" text-anchor="middle">c2p3</text><rect x="524" y="40" width="35" height="32" fill="#4a7bd0" fill-opacity="0.46" stroke="#4a7bd0"/><text x="541.5" y="61" font-size="12" text-anchor="middle">c3p0</text><rect x="561" y="40" width="35" height="32" fill="#e08a3c" fill-opacity="0.46" stroke="#e08a3c"/><text x="578.5" y="61" font-size="12" text-anchor="middle">c3p1</text>
<rect x="598" y="40" width="35" height="32" fill="#3f9a6b" fill-opacity="0.46" stroke="#3f9a6b"/><text x="615.5" y="61" font-size="12" text-anchor="middle">c3p2</text><rect x="635" y="40" width="35" height="32" fill="#d0564a" fill-opacity="0.46" stroke="#d0564a"/><text x="652.5" y="61" font-size="12" text-anchor="middle">c3p3</text><text x="80" y="90" font-size="12" text-anchor="start">한 채널의 픽셀들이 연속 → 픽셀(W) 방향 SIMD, depthwise·큰 이미지에 유리</text><text x="10" y="137" font-size="13" text-anchor="start">NHWC</text><rect x="80" y="115" width="35" height="32" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/><text x="97.5" y="136" font-size="12" text-anchor="middle">c0p0</text><rect x="117" y="115" width="35" height="32" fill="#4a7bd0" fill-opacity="0.32" stroke="#4a7bd0"/><text x="134.5" y="136" font-size="12" text-anchor="middle">c1p0</text>
<rect x="154" y="115" width="35" height="32" fill="#4a7bd0" fill-opacity="0.39" stroke="#4a7bd0"/><text x="171.5" y="136" font-size="12" text-anchor="middle">c2p0</text><rect x="191" y="115" width="35" height="32" fill="#4a7bd0" fill-opacity="0.46" stroke="#4a7bd0"/><text x="208.5" y="136" font-size="12" text-anchor="middle">c3p0</text><rect x="228" y="115" width="35" height="32" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/><text x="245.5" y="136" font-size="12" text-anchor="middle">c4p0</text><rect x="265" y="115" width="35" height="32" fill="#4a7bd0" fill-opacity="0.32" stroke="#4a7bd0"/><text x="282.5" y="136" font-size="12" text-anchor="middle">c5p0</text><rect x="302" y="115" width="35" height="32" fill="#4a7bd0" fill-opacity="0.39" stroke="#4a7bd0"/><text x="319.5" y="136" font-size="12" text-anchor="middle">c6p0</text>
<rect x="339" y="115" width="35" height="32" fill="#4a7bd0" fill-opacity="0.46" stroke="#4a7bd0"/><text x="356.5" y="136" font-size="12" text-anchor="middle">c7p0</text><rect x="376" y="115" width="35" height="32" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/><text x="393.5" y="136" font-size="12" text-anchor="middle">c0p1</text><rect x="413" y="115" width="35" height="32" fill="#e08a3c" fill-opacity="0.32" stroke="#e08a3c"/><text x="430.5" y="136" font-size="12" text-anchor="middle">c1p1</text><rect x="450" y="115" width="35" height="32" fill="#e08a3c" fill-opacity="0.39" stroke="#e08a3c"/><text x="467.5" y="136" font-size="12" text-anchor="middle">c2p1</text><rect x="487" y="115" width="35" height="32" fill="#e08a3c" fill-opacity="0.46" stroke="#e08a3c"/><text x="504.5" y="136" font-size="12" text-anchor="middle">c3p1</text>
<rect x="524" y="115" width="35" height="32" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/><text x="541.5" y="136" font-size="12" text-anchor="middle">c4p1</text><rect x="561" y="115" width="35" height="32" fill="#e08a3c" fill-opacity="0.32" stroke="#e08a3c"/><text x="578.5" y="136" font-size="12" text-anchor="middle">c5p1</text><rect x="598" y="115" width="35" height="32" fill="#e08a3c" fill-opacity="0.39" stroke="#e08a3c"/><text x="615.5" y="136" font-size="12" text-anchor="middle">c6p1</text><rect x="635" y="115" width="35" height="32" fill="#e08a3c" fill-opacity="0.46" stroke="#e08a3c"/><text x="652.5" y="136" font-size="12" text-anchor="middle">c7p1</text><text x="80" y="165" font-size="12" text-anchor="start">한 픽셀의 채널들이 연속 → 채널 방향 SIMD, pointwise(1×1)·GEMM에 유리 (TFLite 기본)</text><text x="10" y="212" font-size="13" text-anchor="start">NCHW4c</text>
<rect x="80" y="190" width="35" height="32" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/><text x="97.5" y="211" font-size="12" text-anchor="middle">c0p0</text><rect x="117" y="190" width="35" height="32" fill="#4a7bd0" fill-opacity="0.32" stroke="#4a7bd0"/><text x="134.5" y="211" font-size="12" text-anchor="middle">c1p0</text><rect x="154" y="190" width="35" height="32" fill="#4a7bd0" fill-opacity="0.39" stroke="#4a7bd0"/><text x="171.5" y="211" font-size="12" text-anchor="middle">c2p0</text><rect x="191" y="190" width="35" height="32" fill="#4a7bd0" fill-opacity="0.46" stroke="#4a7bd0"/><text x="208.5" y="211" font-size="12" text-anchor="middle">c3p0</text><rect x="228" y="190" width="35" height="32" fill="#e08a3c" fill-opacity="0.25" stroke="#e08a3c"/><text x="245.5" y="211" font-size="12" text-anchor="middle">c0p1</text>
<rect x="265" y="190" width="35" height="32" fill="#e08a3c" fill-opacity="0.32" stroke="#e08a3c"/><text x="282.5" y="211" font-size="12" text-anchor="middle">c1p1</text><rect x="302" y="190" width="35" height="32" fill="#e08a3c" fill-opacity="0.39" stroke="#e08a3c"/><text x="319.5" y="211" font-size="12" text-anchor="middle">c2p1</text><rect x="339" y="190" width="35" height="32" fill="#e08a3c" fill-opacity="0.46" stroke="#e08a3c"/><text x="356.5" y="211" font-size="12" text-anchor="middle">c3p1</text><rect x="376" y="190" width="35" height="32" fill="#3f9a6b" fill-opacity="0.25" stroke="#3f9a6b"/><text x="393.5" y="211" font-size="12" text-anchor="middle">c0p2</text><rect x="413" y="190" width="35" height="32" fill="#3f9a6b" fill-opacity="0.32" stroke="#3f9a6b"/><text x="430.5" y="211" font-size="12" text-anchor="middle">c1p2</text>
<rect x="450" y="190" width="35" height="32" fill="#3f9a6b" fill-opacity="0.39" stroke="#3f9a6b"/><text x="467.5" y="211" font-size="12" text-anchor="middle">c2p2</text><rect x="487" y="190" width="35" height="32" fill="#3f9a6b" fill-opacity="0.46" stroke="#3f9a6b"/><text x="504.5" y="211" font-size="12" text-anchor="middle">c3p2</text><rect x="524" y="190" width="35" height="32" fill="#d0564a" fill-opacity="0.25" stroke="#d0564a"/><text x="541.5" y="211" font-size="12" text-anchor="middle">c0p3</text><rect x="561" y="190" width="35" height="32" fill="#d0564a" fill-opacity="0.32" stroke="#d0564a"/><text x="578.5" y="211" font-size="12" text-anchor="middle">c1p3</text><rect x="598" y="190" width="35" height="32" fill="#d0564a" fill-opacity="0.39" stroke="#d0564a"/><text x="615.5" y="211" font-size="12" text-anchor="middle">c2p3</text>
<rect x="635" y="190" width="35" height="32" fill="#d0564a" fill-opacity="0.46" stroke="#d0564a"/><text x="652.5" y="211" font-size="12" text-anchor="middle">c3p3</text><text x="80" y="240" font-size="12" text-anchor="start">4채널 묶음 안에서는 NHWC, 묶음 밖은 NCHW → 벡터 폭에 맞춘 절충 (oneDNN 등)</text><text x="10" y="278" font-size="12" text-anchor="start">색 = 픽셀(p0 파랑, p1 주황, p2 초록, p3 빨강). 같은 색이 붙어 있을수록 그 방향으로 벡터 load가 연속이다.</text></svg>
```

그림 3 — 같은 텐서(채널 8개, 픽셀 4개)의 메모리 순서. NCHW는 한 채널의 픽셀이 붙어 있고, NHWC는 한 픽셀의 채널이 붙어 있다. NCHW4c는 채널을 4개씩 묶어 묶음 안에서는 채널이 붙어 있게 한 절충이다.

**정의**: 4차원 텐서(N 배치, C 채널, H 높이, W 너비)를 1차원 메모리에 펼치는 순서. 주소 계산으로 쓰면(A1 7절의 stride와 같다):

```
NCHW   : addr(n,c,h,w) = ((n·C + c)·H + h)·W + w            가장 빠른 축 = w
NHWC   : addr(n,h,w,c) = ((n·H + h)·W + w)·C + c            가장 빠른 축 = c
NCHWc  : addr = (((n·C/cb + c/cb)·H + h)·W + w)·cb + c%cb   가장 빠른 축 = 채널 묶음 안의 c (cb = 4/8/16)
```

말로 하면: **가장 빠른 축이 SIMD가 도는 축**이 되면 좋다. NCHW는 픽셀 방향 SIMD, NHWC는 채널 방향 SIMD, NCHWc는 "벡터 폭 = cb"로 맞춘 채널 방향 SIMD다. TFLite·TFLM·CMSIS-NN은 NHWC, PyTorch 기본은 NCHW, Intel oneDNN은 nChw8c·nChw16c 같은 blocked 형식을 쓴다.

**측정 1 — depthwise 3×3** (C = 64, 입력 58×58, 출력 56×56, float). 네 가지 구현: NCHW, NHWC, NCHWc, 그리고 NCHWc + 출력 4픽셀 블로킹(2.2절의 unroll-and-jam을 레이아웃 위에 얹은 것).

```c
#include "bench.h"
#define C 64
#define H 58
#define W 58
#define OH (H - 2)
#define OW (W - 2)
/* depthwise 3×3, stride 1, valid. 세 가지 레이아웃, 같은 수학 */
NOINLINE void dw_nchw(const float *x, const float *w, float *y) {          /* x[c][h][w], w[c][9] */
    for (int c = 0; c < C; c++) for (int oh = 0; oh < OH; oh++) for (int ow = 0; ow < OW; ow++) {
        float s = 0; for (int kh = 0; kh < 3; kh++) for (int kw = 0; kw < 3; kw++)
            s += x[(c * H + oh + kh) * W + ow + kw] * w[c * 9 + kh * 3 + kw];
        y[(c * OH + oh) * OW + ow] = s; }
}
NOINLINE void dw_nhwc(const float *x, const float *w, float *y) {          /* x[h][w][c], w[9][c] */
    for (int oh = 0; oh < OH; oh++) for (int ow = 0; ow < OW; ow++) {
        float s[C] = {0};
        for (int kh = 0; kh < 3; kh++) for (int kw = 0; kw < 3; kw++)
            for (int c = 0; c < C; c++) s[c] += x[((oh + kh) * W + ow + kw) * C + c] * w[(kh * 3 + kw) * C + c];
        for (int c = 0; c < C; c++) y[(oh * OW + ow) * C + c] = s[c]; }
}
#ifndef CB
#define CB 8
#endif
NOINLINE void dw_nchwc(const float *x, const float *w, float *y) {         /* x[C/CB][h][w][CB], w[C/CB][9][CB] */
    for (int cb = 0; cb < C / CB; cb++) for (int oh = 0; oh < OH; oh++) for (int ow = 0; ow < OW; ow++) {
        float s[CB] = {0};
        for (int kh = 0; kh < 3; kh++) for (int kw = 0; kw < 3; kw++)
            for (int c = 0; c < CB; c++) s[c] += x[((cb * H + oh + kh) * W + ow + kw) * CB + c] * w[(cb * 9 + kh * 3 + kw) * CB + c];
        for (int c = 0; c < CB; c++) y[((cb * OH + oh) * OW + ow) * CB + c] = s[c]; }
}
/* NCHWc + 출력 4픽셀 블로킹(unroll-and-jam): weight 벡터 한 번 load로 4픽셀에 재사용 */
NOINLINE void dw_nchwc_b4(const float *x, const float *w, float *y) {
    for (int cb = 0; cb < C / CB; cb++) for (int oh = 0; oh < OH; oh++) for (int ow = 0; ow + 4 <= OW; ow += 4) {
        float s[4][CB] = {{0}};
        for (int kh = 0; kh < 3; kh++) for (int kw = 0; kw < 3; kw++) { const float *wp = w + (cb * 9 + kh * 3 + kw) * CB;
            const float *xp = x + ((cb * H + oh + kh) * W + ow + kw) * CB;
            for (int j = 0; j < 4; j++) for (int c = 0; c < CB; c++) s[j][c] += xp[j * CB + c] * wp[c]; }
        for (int j = 0; j < 4; j++) for (int c = 0; c < CB; c++) y[((cb * OH + oh) * OW + ow + j) * CB + c] = s[j][c]; }
}
```

main은 지면상 생략했다. 하는 일: 같은 값을 세 레이아웃 배열에 채우고, 네 출력을 **레이아웃을 맞춰 원소별로** 비교한 뒤 GFLOP/s를 출력한다. `-DCB=4/8/16`으로 세 번 빌드했다.

```text
depthwise 3x3 C=64 58x58  NCHW   78.6 us  45.9 GFLOP/s | NHWC  153.2 us  23.6 | NCHW4c  127.8 us  28.3 | +4px block   54.9 us  65.9 | mismatch=0
depthwise 3x3 C=64 58x58  NCHW   77.6 us  46.6 GFLOP/s | NHWC  150.7 us  24.0 | NCHW8c  128.4 us  28.1 | +4px block   51.8 us  69.7 | mismatch=0
depthwise 3x3 C=64 58x58  NCHW   78.6 us  45.9 GFLOP/s | NHWC  148.1 us  24.4 | NCHW16c  128.3 us  28.2 | +4px block   90.2 us  40.1 | mismatch=0
```

출력에서 볼 것 세 가지.

1. **레이아웃만 바꾸면 오히려 느려진다.** 단순 NHWC·NCHWc(28 GFLOP/s)가 NCHW(46)보다 느리다. NCHW에서는 컴파일러가 ow 방향으로 벡터화하면서 weight 9개를 레지스터에 broadcast로 붙잡아 두는데(load 1번당 FMA 1번), NCHWc 단순 버전은 출력 픽셀마다 x 벡터 9개 + w 벡터 9개를 읽는다(FMA 1번당 load 2번).
2. **레이아웃 + 블로킹이 이긴다.** NCHW8c에 출력 4픽셀 블로킹을 얹으면 70 GFLOP/s로 NCHW의 1.5배. weight 벡터를 한 번 읽어 4픽셀에 쓰기 때문이다. 레이아웃은 **좋은 루프 구조를 가능하게 하는 조건**이지 그 자체가 처방이 아니다.
3. **블록이 너무 크면 레지스터가 터진다.** 레지스터 손계산(NEON 128비트 레지스터 32개, float 4개씩):

```
cb = 8 , 4px : 누산기 4 × 8 = 32 float = 8 레지스터,  weight 9 × 8 = 72 float = 18 레지스터 → 26 ≤ 32  ✓
cb = 16, 4px : 누산기 4 × 16 = 16 레지스터,           weight 9 × 16 = 36 레지스터 → 52 > 32  ✗ spill
```

그래서 NCHW16c + 4px는 40 GFLOP/s로 떨어졌다. 결과는 세 레이아웃 모두 `mismatch=0` — 각 출력의 누산 순서(kh, kw 순)가 같아서 bit-exact다.

**측정 2 — pointwise 1×1 conv** (64 → 64 채널, 3136 픽셀). 1×1 conv는 사실상 GEMM이다. NCHW는 픽셀 방향, NHWC는 채널 방향으로 벡터화한다. 그리고 **레이아웃을 바꾸는 비용**(NCHW → NHWC 전치)도 같이 잰다.

```c
#include "bench.h"
#define CI 64
#define CO 64
#define P (56 * 56)
/* pointwise(1×1) conv = GEMM. NCHW: y[co][p], 픽셀 방향이 연속 → p로 벡터화 */
NOINLINE void pw_nchw(const float *x, const float *w, float *y) {           /* w[co][ci] */
    for (int co = 0; co < CO; co++) { float *yr = y + co * P; for (int p = 0; p < P; p++) yr[p] = 0;
        for (int ci = 0; ci < CI; ci++) { float wv = w[co * CI + ci]; const float *xr = x + ci * P;
            for (int p = 0; p < P; p++) yr[p] += wv * xr[p]; } }
}
/* NHWC: y[p][co], 채널 방향이 연속 → co로 벡터화 (weight는 [ci][co]로 저장) */
NOINLINE void pw_nhwc(const float *x, const float *wt, float *y) {          /* wt[ci][co] */
    for (int p = 0; p < P; p++) { float acc[CO] = {0};
        for (int ci = 0; ci < CI; ci++) { float xv = x[p * CI + ci]; const float *wr = wt + ci * CO;
            for (int co = 0; co < CO; co++) acc[co] += xv * wr[co]; }
        memcpy(y + p * CO, acc, sizeof acc); }
}
/* 레이아웃 변환 비용: NCHW → NHWC (= C×P 행렬 전치) */
NOINLINE void nchw_to_nhwc(const float *x, float *xt) {
    for (int c = 0; c < CI; c++) for (int p = 0; p < P; p++) xt[p * CI + c] = x[c * P + p];
}
/* main: 같은 값을 NCHW 배열과 NHWC 배열(전치), w[co][ci]와 wt[ci][co]에 채우고 세 함수를 BENCH, 두 출력을 원소별로 비교해 GFLOP/s와 전치 비용을 출력한다 */
```

```text
pointwise 64->64, 3136 px: NCHW 1367.1 us  18.8 GFLOP/s | NHWC  470.7 us  54.6 GFLOP/s | mismatch=0
transpose NCHW->NHWC (784 KiB):  244.0 us = 18% of NCHW conv time
```

출력에서 볼 것: pointwise는 **NHWC가 2.6~2.9배** 빠르다(세 번 실행). NHWC에서는 한 픽셀의 출력 누산기 64개(256 B)가 L1에 머물고 weight 16 KiB도 L1에 있어, 안쪽 co 루프가 L1 안에서만 돈다(리포트: 폭 4 × interleave 4로 벡터화). NCHW 버전은 출력 행 12 KiB를 `ci` 64번 동안 메모리로 다시 읽고 쓴다. depthwise와 정반대 결론이다. 그리고 전치 한 번이 NCHW conv 시간의 17~23 %(NHWC conv 시간의 약 절반)다.

### 3.3 정렬(alignment)과 패딩

- **정렬**: 벡터 load·DMA burst는 정렬된 주소에서 가장 빠르다. Cortex-M4는 비정렬 LDR을 허용하지만 추가 cycle이 들고, LDRD·LDM 같은 다중 load는 비정렬이면 fault다. SIMD·DMA 엔진마다 정렬 요구가 다르니 해당 TRM·데이터시트를 확인한다. 버퍼는 `__attribute__((aligned(16)))` 또는 링커 스크립트로 정렬한다. DMA + D-cache 시스템에서는 **캐시 라인(32 B) 정렬**이 정확성 문제이기도 하다 — 라인 일부만 invalidate하면 이웃 변수가 망가진다(E7 6절).
- **채널 패딩**: 채널 수를 벡터 폭의 배수로 올린다. 예: C = 30, int8 SIMD 폭 16이면 32로 패딩 → 메모리 `32/30 = 1.067`배, 대신 꼬리(tail) 처리 루프가 사라진다. C6 7.5절과 같은 이야기다.
- **행 pitch 패딩**: 2D 이미지의 행 길이를 캐시 set 충돌을 피하도록 살짝 늘린다(예: 4096 → 4096 + 16 float). E1 4.2절 전치 실험에서 본 conflict miss를 피하는 방법이다.

손계산 (꼬리 비용): C = 30 채널을 16폭 SIMD로 돌리면 `30 = 16 + 14` → 벡터 1번 + 스칼라 14번(또는 마스크 벡터 1번). 32로 패딩하면 벡터 2번. Helium은 **tail predication**(`VCTP` + 루프 `DLSTP/LETP`)이 있어 패딩 없이도 마지막 벡터를 마스크로 처리한다.

### 3.4 레이아웃은 한 번 바꾸나, op마다 바꾸나

측정 2의 숫자로 판단해 보자. pointwise 하나만 보면 "전치(244 µs) + NHWC conv(471 µs) = 715 µs < NCHW conv(1367 µs)"라 op마다 바꿔도 이득이다. 하지만 depthwise는 NCHW 쪽이 단순 구현에서 빨랐다. MobileNet 블록처럼 depthwise와 pointwise가 번갈아 나오면 op마다 전치하는 비용이 계속 쌓인다.

규칙:

1. **그래프 전체에 한 레이아웃**을 정하고(대개 그 하드웨어의 가속 커널이 원하는 것: Cortex-M·TFLM은 NHWC, NPU는 벤더 blocked 형식), 경계(입력·출력)에서만 한 번 바꾼다. 그래프 컴파일러가 Transpose 쌍을 지우는 이유다(C6 7절).
2. 그 레이아웃에서 약한 op(여기서는 depthwise)는 **블로킹으로 보완**한다(측정 1의 NCHW8c + 4px처럼).
3. 레이아웃 변환이 꼭 필요하면 **다른 패스에 fusion**한다: 전처리(예: MFCC 결과를 쓰는 순간 NHWC로 쓴다)나 DMA(2D strided DMA가 전치를 대신 하게 한다, E7 5.2절).

---

## 4. 알고리즘을 바꾸기 — Winograd, FFT, Strassen

2·3절은 "같은 연산을 더 잘 배치"했다. compute 지붕에 붙은 커널은 그걸로 더 빨라지지 않는다. 남은 길은 **연산 수 자체를 줄이는 알고리즘**이다. 대가는 항상 같다: 덧셈·변환 비용 증가, 수치 정밀도 손실, 구현 복잡도.

### 4.1 Winograd 직관 — 곱셈을 덧셈으로 바꾸는 거래

출력 2개를 3탭 필터로 만드는 1D 상관(CNN의 "conv")을 보자. 입력 d0..d3, 필터 g0..g2:

```
y0 = d0·g0 + d1·g1 + d2·g2
y1 = d1·g0 + d2·g1 + d3·g2          → 곱셈 6번
```

Winograd(Lavin & Gray 2016이 CNN에 가져온 Winograd의 최소 필터링 알고리즘)는 이렇게 쓴다:

```
m1 = (d0 − d2) · g0
m2 = (d1 + d2) · (g0 + g1 + g2) / 2
m3 = (d2 − d1) · (g0 − g1 + g2) / 2
m4 = (d1 − d3) · g2
y0 = m1 + m2 + m3
y1 = m2 − m3 − m4                    → 곱셈 4번
```

말로 하면: 필터 쪽 조합(`(g0 + g1 + g2)/2` 등)은 **weight가 고정이니 오프라인에서 한 번** 계산해 둔다. 실행 시에는 입력을 덧셈·뺄셈으로 섞고(입력 변환), 곱 4번, 다시 덧셈으로 섞는다(출력 변환). 곱셈 6 → 4.

손으로 확인 (d = [1, 2, 3, 4], g = [1, 0, −1]):

```
직접:   y0 = 1·1 + 2·0 + 3·(−1) = −2,   y1 = 2·1 + 3·0 + 4·(−1) = −2
필터 변환 U = [g0, (g0+g1+g2)/2, (g0−g1+g2)/2, g2] = [1, 0, 0, −1]
입력 변환 V = [d0−d2, d1+d2, d2−d1, d1−d3]          = [−2, 5, 1, −2]
곱     M = U ⊙ V                                    = [−2, 0, 0, 2]
출력   y0 = −2 + 0 + 0 = −2,  y1 = 0 − 0 − 2 = −2    ✓
```

행렬로 쓰면 `y = Aᵀ [ (G g) ⊙ (Bᵀ d) ]`이고, F(2,3)의 표준 행렬(Lavin & Gray 2016)은:

```
      ┌ 1  0 −1  0 ┐         ┌ 1    0    0  ┐
Bᵀ =  │ 0  1  1  0 │    G =  │ 1/2  1/2  1/2 │    Aᵀ = ┌ 1  1  1  0 ┐
      │ 0 −1  1  0 │         │ 1/2 −1/2  1/2 │         └ 0  1 −1 −1 ┘
      └ 0  1  0 −1 ┘         └ 0    0    1  ┘
```

numpy로 손계산과 무작위 1000건을 확인한다. 확인할 것: 손계산 값과, float64에서의 오차 크기.

```python
import numpy as np
# Winograd F(2,3): 입력 4개 d, 필터 3개 g → 출력 2개 (상관(correlation), CNN의 "conv")
BT = np.array([[1, 0, -1, 0], [0, 1, 1, 0], [0, -1, 1, 0], [0, 1, 0, -1]], dtype=float)
G  = np.array([[1, 0, 0], [0.5, 0.5, 0.5], [0.5, -0.5, 0.5], [0, 0, 1]])
AT = np.array([[1, 1, 1, 0], [0, 1, -1, -1]], dtype=float)
d = np.array([1., 2., 3., 4.]); g = np.array([1., 0., -1.])
U = G @ g            # 필터 변환 (오프라인에서 한 번)
V = BT @ d           # 입력 변환 (덧셈만)
M = U * V            # 원소별 곱 4번  ← 곱셈은 여기만
y = AT @ M           # 출력 변환 (덧셈만)
direct = np.array([d[0:3] @ g, d[1:4] @ g])   # 직접 계산: 곱셈 6번
print("U =", U, " V =", V, " M =", M)
print("winograd y =", y, " direct y =", direct)
rng = np.random.default_rng(0); err = 0
for _ in range(1000):
    d = rng.standard_normal(4); g = rng.standard_normal(3)
    err = max(err, np.abs(AT @ ((G @ g) * (BT @ d)) - np.array([d[0:3] @ g, d[1:4] @ g])).max())
print(f"random 1000 cases, float64 max|err| = {err:.2e}")
```

```text
U = [ 1.  0.  0. -1.]  V = [-2.  5.  1. -2.]  M = [-2.  0.  0.  2.]
winograd y = [-2. -2.]  direct y = [-2. -2.]
random 1000 cases, float64 max|err| = 1.78e-15
```

출력에서 볼 것: U, V, M이 손계산과 똑같다. 무작위 입력에서도 float64 반올림 수준(1e-15)으로 일치한다 — 수학적으로 같은 식이다.

### 4.2 2D F(2×2, 3×3) — 곱셈 2.25배 절감, 그리고 정밀도

2D는 1D 변환을 행과 열에 한 번씩 적용한다(중첩, nesting):

```
Y = Aᵀ [ (G g Gᵀ) ⊙ (Bᵀ d B) ] A
   d : 4×4 입력 타일,  g : 3×3 필터,  Y : 2×2 출력
   곱셈: 직접 2×2×9 = 36번  vs  Winograd 4×4 = 16번  → 2.25배 적다
```

출력 타일을 더 크게 하면 절감이 커진다. F(4×4, 3×3)은 입력 6×6 타일, 곱 36번으로 출력 16개(직접 144번) → **4배**. 대신 변환 행렬에 큰 수와 분수(4, 5, 1/24, 8…)가 들어가 **반올림 오차가 커진다**. 두 경우를 float32·float16으로 계산해 float64 직접 계산과 비교한다.

```python
import numpy as np
F2 = dict(BT=np.array([[1,0,-1,0],[0,1,1,0],[0,-1,1,0],[0,1,0,-1]], float),
          G=np.array([[1,0,0],[.5,.5,.5],[.5,-.5,.5],[0,0,1]]),
          AT=np.array([[1,1,1,0],[0,1,-1,-1]], float), m=2)
F4 = dict(BT=np.array([[4,0,-5,0,1,0],[0,-4,-4,1,1,0],[0,4,-4,-1,1,0],[0,-2,-1,2,1,0],[0,2,-1,-2,1,0],[0,4,0,-5,0,1]], float),
          G=np.array([[1/4,0,0],[-1/6,-1/6,-1/6],[-1/6,1/6,-1/6],[1/24,1/12,1/6],[1/24,-1/12,1/6],[0,0,1]]),
          AT=np.array([[1,1,1,1,1,0],[0,1,-1,2,-2,0],[0,1,1,4,4,0],[0,1,-1,8,-8,1]], float), m=4)
def wino_tile(F, d, g, dt):                 # 2D: Y = Aᵀ [ (G g Gᵀ) ⊙ (Bᵀ d B) ] A, 모든 단계를 dtype dt로
    BT, G, AT = (F[k].astype(dt) for k in ("BT", "G", "AT"))
    U = G @ g.astype(dt) @ G.T; V = BT @ d.astype(dt) @ BT.T
    return AT @ (U * V) @ AT.T
def direct_tile(d, g, m):                   # 같은 타일의 직접 상관 (float64 기준값)
    return np.array([[np.sum(d[i:i+3, j:j+3] * g) for j in range(m)] for i in range(m)])
rng = np.random.default_rng(1)
for name, F in (("F(2x2,3x3)", F2), ("F(4x4,3x3)", F4)):
    m = F["m"]; t = m + 2
    e = {dt: 0.0 for dt in (np.float32, np.float16)}; ed = {np.float32: 0.0, np.float16: 0.0}
    for _ in range(2000):
        d = rng.uniform(-1, 1, (t, t)); g = rng.uniform(-1, 1, (3, 3)); ref = direct_tile(d, g, m)
        for dt in e:
            e[dt] = max(e[dt], np.abs(wino_tile(F, d, g, dt).astype(float) - ref).max())
            dd = direct_tile(d.astype(dt), g.astype(dt), m)   # 같은 dtype의 직접 계산
            ed[dt] = max(ed[dt], np.abs(dd.astype(float) - ref).max())
    mults_direct, mults_w = m * m * 9, t * t
    print(f"{name}: tile in {t}x{t} -> out {m}x{m}, mults direct {mults_direct} vs winograd {mults_w} ({mults_direct/mults_w:.2f}x fewer)")
    print(f"   max|err| fp32: winograd {e[np.float32]:.1e}  direct {ed[np.float32]:.1e} | fp16: winograd {e[np.float16]:.1e}  direct {ed[np.float16]:.1e}")
```

```text
F(2x2,3x3): tile in 4x4 -> out 2x2, mults direct 36 vs winograd 16 (2.25x fewer)
   max|err| fp32: winograd 6.7e-07  direct 3.5e-07 | fp16: winograd 5.8e-03  direct 1.9e-03
F(4x4,3x3): tile in 6x6 -> out 4x4, mults direct 144 vs winograd 36 (4.00x fewer)
   max|err| fp32: winograd 1.0e-05  direct 4.4e-07 | fp16: winograd 7.3e-02  direct 2.2e-03
```

출력에서 볼 것: F(4×4)의 행렬도 직접 계산과 맞으므로(fp32 오차 1e-5) 위 행렬 값이 올바르다는 검증이 된다. 정밀도는 F(2×2)가 직접 계산의 약 2~3배 오차, F(4×4)는 fp32에서 23배, **fp16에서 33배**(0.073 — 입력이 ±1 범위인데 출력 오차가 7 %)다. 그래서 fp16 추론(GPU·NPU)에서는 F(2×2)만 쓰거나, F(4×4)를 쓸 때 변환을 fp32로 하는 식의 타협을 한다. (주의: numpy의 float16 행렬곱 내부 누산 정밀도는 구현에 따라 다를 수 있다. 여기서는 단계마다 결과를 fp16 배열로 저장했다는 정도로 읽자.)

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300"><defs><marker id="k4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10" y="20" font-size="14" text-anchor="start">Winograd F(2×2, 3×3) 한 타일의 데이터 흐름 — 곱셈은 가운데 ⊙에만 있다</text><rect x="10" y="45" width="120" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/><text x="70.0" y="71.5" font-size="12" text-anchor="middle">입력 타일 d</text><text x="70.0" y="86.5" font-size="12" text-anchor="middle">4×4 (C_in개)</text><rect x="170" y="45" width="130" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/><text x="235.0" y="71.5" font-size="12" text-anchor="middle">V = Bᵀ d B</text><text x="235.0" y="86.5" font-size="12" text-anchor="middle">덧셈·뺄셈만 32회</text>
<rect x="10" y="175" width="120" height="60" rx="6" fill="#888" fill-opacity="0.15" stroke="#888"/><text x="70.0" y="201.5" font-size="12" text-anchor="middle">필터 g</text><text x="70.0" y="216.5" font-size="12" text-anchor="middle">3×3 (C_out×C_in)</text><rect x="170" y="175" width="130" height="60" rx="6" fill="#888" fill-opacity="0.15" stroke="#888"/><text x="235.0" y="201.5" font-size="12" text-anchor="middle">U = G g Gᵀ</text><text x="235.0" y="216.5" font-size="12" text-anchor="middle">오프라인 1회 (×½)</text><rect x="340" y="105" width="150" height="70" rx="6" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/><text x="415.0" y="129.0" font-size="12" text-anchor="middle">M = Σ_ci U ⊙ V</text><text x="415.0" y="144.0" font-size="12" text-anchor="middle">16개 원소마다 GEMM</text><text x="415.0" y="159.0" font-size="12" text-anchor="middle">곱 16회 (직접 36회)</text>
<rect x="530" y="105" width="140" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/><text x="600.0" y="129.0" font-size="12" text-anchor="middle">Y = Aᵀ M A</text><text x="600.0" y="144.0" font-size="12" text-anchor="middle">덧셈만 24회</text><text x="600.0" y="159.0" font-size="12" text-anchor="middle">→ 출력 2×2</text><line x1="130" y1="75" x2="168" y2="75" stroke="currentColor" marker-end="url(#k4)"/><line x1="130" y1="205" x2="168" y2="205" stroke="currentColor" marker-end="url(#k4)"/><line x1="300" y1="75" x2="338" y2="125" stroke="currentColor" marker-end="url(#k4)"/><line x1="300" y1="205" x2="338" y2="155" stroke="currentColor" marker-end="url(#k4)"/><line x1="490" y1="140" x2="528" y2="140" stroke="currentColor" marker-end="url(#k4)"/>
<text x="10" y="268" font-size="12" text-anchor="start">입력 타일은 2칸씩 겹친다(4×4 창, stride 2). 변환 비용은 C_in·C_out에 걸쳐 나눠지므로 채널이 많을수록 이득.</text><text x="10" y="288" font-size="12" text-anchor="start">depthwise(채널 합 없음)에서는 변환 비용을 나눌 곳이 없어 이득이 거의 없다.</text></svg>
```

그림 4 — F(2×2, 3×3) 한 타일의 흐름. 입력 변환과 출력 변환은 덧셈만, 곱셈은 가운데 원소별 곱(채널 합까지 포함하면 GEMM 16개)에만 있다. 필터 변환은 배포 전에 끝난다.

### 4.3 C로 구현 — 다채널 conv에서 실제로 빨라지나

다채널 conv(C_in = C_out = 64, 출력 56×56)에서 Winograd는 이렇게 짠다:

1. 입력 변환: 모든 입력 채널·모든 타일(28 × 28 = 784개)에 대해 V = Bᵀ d B를 계산해 `V[16][C_in][784]`에 흩어 둔다.
2. **원소 e = 0..15마다 GEMM**: `M[e] (C_out × 784) = U[e] (C_out × C_in) × V[e] (C_in × 784)`. 채널 합이 여기서 일어난다. 곱셈은 전부 여기.
3. 출력 변환: 각 (출력 채널, 타일)마다 Y = Aᵀ M A로 2×2 출력.

비교 대상인 직접 conv는 같은 스타일(NCHW, 픽셀 방향 벡터화, 탭마다 weight broadcast)로 짰다. 아래는 핵심 세 함수다(필터 변환 `wino_filter`, 직접 conv, main은 같은 파일에 있고 출력만 보인다).

```c
NOINLINE void wino_in(const float *x, float *V) {
    for (int ci = 0; ci < CI; ci++) for (int th = 0; th < TH; th++) for (int tw = 0; tw < TW; tw++) {   /* 1) 입력 변환 Bᵀ d B */
        const float *d = x + (ci * H + 2 * th) * W + 2 * tw; float t[4][4];
        for (int j = 0; j < 4; j++) { float d0 = d[j], d1 = d[W + j], d2 = d[2 * W + j], d3 = d[3 * W + j];
            t[0][j] = d0 - d2; t[1][j] = d1 + d2; t[2][j] = d2 - d1; t[3][j] = d1 - d3; }
        int tt = th * TW + tw;
        for (int i = 0; i < 4; i++) { float *v = V + (i * 4 * CI + ci) * T + tt;
            v[0] = t[i][0] - t[i][2]; v[CI * T] = t[i][1] + t[i][2]; v[2 * CI * T] = t[i][2] - t[i][1]; v[3 * CI * T] = t[i][1] - t[i][3]; } }
}
NOINLINE void wino_gemm(const float *U, const float *V, float *M) {
    memset(M, 0, sizeof(float) * 16 * CO * T);
    for (int e = 0; e < 16; e++) for (int co = 0; co < CO; co++) { float *m = M + (e * CO + co) * T;   /* 2) 16개의 GEMM */
        for (int ci = 0; ci < CI; ci++) { float u = U[(e * CO + co) * CI + ci]; const float *v = V + (e * CI + ci) * T;
            for (int t = 0; t < T; t++) m[t] += u * v[t]; } }
}
NOINLINE void wino_out(const float *M, float *y) {
    for (int co = 0; co < CO; co++) for (int tt = 0; tt < T; tt++) {                                      /* 3) 출력 변환 Aᵀ m A */
        float m[16], r[2][4]; for (int e = 0; e < 16; e++) m[e] = M[(e * CO + co) * T + tt];
        for (int j = 0; j < 4; j++) { r[0][j] = m[j] + m[4 + j] + m[8 + j]; r[1][j] = m[4 + j] - m[8 + j] - m[12 + j]; }
        float *yo = y + (co * OH + 2 * (tt / TW)) * OW + 2 * (tt % TW);
        for (int i = 0; i < 2; i++) { yo[i * OW] = r[i][0] + r[i][1] + r[i][2]; yo[i * OW + 1] = r[i][1] - r[i][2] - r[i][3]; } }
}
```

직접 conv는 이렇다(`#define CI 64, CO 64, H 58, W 58, OH 56, OW 56, TH 28, TW 28, T 784`):

```c
NOINLINE void conv_direct(const float *x, const float *w, float *y) {
    memset(y, 0, sizeof(float) * CO * OH * OW);
    for (int co = 0; co < CO; co++) for (int ci = 0; ci < CI; ci++) for (int k = 0; k < 9; k++) {
        float wv = w[(co * CI + ci) * 9 + k]; int kh = k / 3, kw = k % 3;
        for (int oh = 0; oh < OH; oh++) { float *yr = y + (co * OH + oh) * OW; const float *xr = x + (ci * H + oh + kh) * W + kw;
            for (int ow = 0; ow < OW; ow++) yr[ow] += wv * xr[ow]; } }
}
```

```text
direct  :   10.93 ms   115.6 M mult
winograd:    5.84 ms    51.4 M mult (GEMM part)  speedup 1.87x
max|diff| = 1.67e-06  (max|y| = 1.88, relative 8.9e-07)
  phases: input transform 0.32 ms | 16 GEMMs 5.37 ms | output transform 0.19 ms
```

출력에서 볼 것: 곱셈 수가 2.25배 줄었고(115.6M → 51.4M), 시간은 1.6~2.1배(세 번 실행) 줄었다. 변환 두 개는 합쳐 0.3~0.5 ms로 전체의 5~9 % — 채널이 64개라 변환 비용이 GEMM에 비해 작다. 상대 오차 9e-7은 float32에서 문제없는 수준이다. 이 "1.8배"가 1.2절 Amdahl 표에서 쓴 숫자다.

언제 이득이 사라지나:

- **depthwise**: 채널 합이 없어서 GEMM 단계가 원소별 곱 16번뿐이고, 변환(덧셈 32 + 24번)을 나눌 곳이 없다. 직접 계산 36 MAC 대비 이득이 거의 없다.
- **stride 2, 1×1, 5×5 이상**: F(2,3)은 3×3 stride 1 전용이다. 다른 모양은 다른 행렬이 필요하고, 5×5 이상은 오차가 빨리 커진다.
- **채널이 적을 때**(예: 첫 레이어 C_in = 1~3): 입력 변환 비용 비중이 커진다.
- **메모리**: V와 M 버퍼(여기서는 각 16 × 64 × 784 × 4 B = 3.2 MB)가 필요하다. MCU라면 타일 단위로 쪼개야 하고, 그러면 GEMM이 작아져 효율이 떨어진다.

### 4.4 int8 Winograd가 어려운 이유 — 변환이 값을 키운다

int8 커널의 매력은 "int8 × int8 → int32 누산"을 SIMD로 넓게 하는 것이다(SDOT은 128비트에 int8 곱 16개). Winograd는 곱하기 **전에** 입력과 weight를 변환하는데, 변환이 값의 범위를 키운다.

손계산: Bᵀ의 각 행은 절댓값 합이 2다(예: `[1, 0, −1, 0]`). V = Bᵀ d B의 원소 하나는 최악의 경우 `2 × 2 × 128 = 512`. int8(−128..127)에 안 들어가고 **10비트**가 필요하다. 필터 쪽은 G에 1/2가 있어서 정수로 만들려면 2G를 쓰고, 그러면 U는 4배 스케일된 값으로 최대 `3 × 3 × 128 = 1152`(12비트)다.

```python
import numpy as np, itertools
BT2 = np.array([[1,0,-1,0],[0,1,1,0],[0,-1,1,0],[0,1,0,-1]])
G2x2 = np.array([[2,0,0],[1,1,1],[1,-1,1],[0,0,2]])          # 2G: 정수로 만들려고 2배
BT4 = np.array([[4,0,-5,0,1,0],[0,-4,-4,1,1,0],[0,4,-4,-1,1,0],[0,-2,-1,2,1,0],[0,2,-1,-2,1,0],[0,4,0,-5,0,1]])
# 입력 변환 V = Bᵀ d B 의 최대 크기: 원소마다 |Bᵀ| 행합 × |Bᵀ| 행합 × 128 (부호를 최악으로 고를 수 있다)
for name, BT in (("F(2x2,3x3)", BT2), ("F(4x4,3x3)", BT4)):
    r = np.abs(BT).sum(1); worst = np.outer(r, r).max() * 128
    print(f"{name}: |B^T| row sums {r.tolist()}  -> max|V| = {worst}  (needs {int(np.ceil(np.log2(worst))) + 1} signed bits, int8 input was 8)")
# 필터 변환 U = (2G) g (2G)ᵀ, g ∈ int8 이면 U 범위는? (4배 스케일된 값)
r = np.abs(G2x2).sum(1); print("F(2x2): |2G| row sums", r.tolist(), "-> max|4U| =", np.outer(r, r).max() * 128,
      f"(needs {int(np.ceil(np.log2(np.outer(r, r).max() * 128))) + 1} bits)")
# 실제 최악 입력 하나를 만들어 확인: V[1][1] = (d11+d21+d12+d22) 계열 → 모두 같은 부호면 최대
d = np.full((4, 4), -128); V = BT2 @ d @ BT2.T; print("all -128 tile: V =\n", V)
d = np.array([[127 if (i in (1, 2)) == (j in (1, 2)) else -128 for j in range(4)] for i in range(4)])
print("adversarial tile max|V| =", np.abs(BT2 @ d @ BT2.T).max())
```

```text
F(2x2,3x3): |B^T| row sums [2, 2, 2, 2]  -> max|V| = 512  (needs 10 signed bits, int8 input was 8)
F(4x4,3x3): |B^T| row sums [10, 10, 10, 6, 6, 10]  -> max|V| = 12800  (needs 15 signed bits, int8 input was 8)
F(2x2): |2G| row sums [2, 3, 3, 2] -> max|4U| = 1152 (needs 12 bits)
all -128 tile: V =
 [[   0    0    0    0]
 [   0 -512    0    0]
 [   0    0    0    0]
 [   0    0    0    0]]
adversarial tile max|V| = 510
```

출력에서 볼 것: 모든 값이 −128인 타일 하나로 V의 한 원소가 정확히 −512가 된다 — 이론 최악값이 실제로 나온다. F(4×4)는 15비트가 필요하다.

그래서 int8 Winograd의 선택지는:

| 방법 | 대가 |
|---|---|
| 변환 후 int16으로 곱하기 (int16 × int16 → int32) | SIMD 폭이 int8의 절반. 곱셈 2.25배 절감이 폭 2배 손실과 상쇄되어 이득이 거의 없다 |
| 변환 후 다시 int8로 requantize | 변환 영역에서 추가 양자화 오차. 정확도 검증·재보정 필요 |
| 변환 영역에서 양자화한 weight(U)를 학습 시 반영 (Winograd-aware 학습) | 학습 파이프라인 변경 |
| F(2×2)만 쓰고 입력을 7비트로 제한 | 범위 손실 |

그리고 int8 커널의 핵심 요구인 **bit-exact**(TFLite reference와 1 LSB도 안 틀리기, J3)를 Winograd는 원리적으로 보장하기 어렵다(정수로 해도 requant 지점이 다르다). 실무에서 int8 MCU 커널(CMSIS-NN, TFLM)이 Winograd를 기본으로 쓰지 않는 이유가 여기 있다. float·fp16 GPU 라이브러리(cuDNN 등)와 일부 모바일 CPU 런타임은 3×3 conv에 Winograd를 쓴다.

### 4.5 FFT convolution — 커널이 길 때

**정의**: 합성곱 정리(convolution theorem) — 시간 영역의 합성곱은 주파수 영역의 원소별 곱이다. 길이 N 신호와 길이 K 커널의 선형 합성곱은 둘을 길이 L ≥ N + K − 1로 0-패딩한 뒤 `IFFT( FFT(x) ⊙ FFT(h) )`로 얻는다.

손계산 (연산량 감각):

```
직접:  N × K 번의 MAC                         → K에 비례
FFT :  FFT 2번 + IFFT 1번 + 원소별 곱 L번     → 대략 c · L · log₂L, K에 거의 무관 (L이 다음 크기로 넘어갈 때만 계단)
N = 16000, L ≈ 32768: L · log₂L = 32768 × 15 ≈ 4.9×10⁵
교차점 K ≈ c · 4.9×10⁵ / 16000 ≈ 31 · c     (c = 복소 연산·메모리 패스 비용을 뭉뚱그린 상수, 대략 3~5)
→ K ≈ 100~150 근처에서 뒤집힐 것으로 예상
```

numpy·scipy로 재 본다. 1D는 1초 오디오(16 kHz) × 탭 수 K, 2D는 256×256 이미지 × K×K. 2D 직접 계산은 scipy의 범용 N차원 경로가 느려서(3×3에 9 ms) 공정하지 않으므로, K² 번의 "이미지 전체 axpy"로 직접 짰다.

```python
import numpy as np, time
from scipy import signal
def med(f, r=21):                                   # r번 재서 중앙값
    ts = []
    for _ in range(r): t0 = time.perf_counter(); f(); ts.append(time.perf_counter() - t0)
    return sorted(ts)[r // 2]
def direct2d(img, h):                               # 직접 상관(valid): K² 번의 "이미지 전체 axpy"
    K = h.shape[0]; H, W = img.shape; out = np.zeros((H - K + 1, W - K + 1))
    for i in range(K):
        for j in range(K): out += h[i, j] * img[i:i + H - K + 1, j:j + W - K + 1]
    return out
rng = np.random.default_rng(0)
x = rng.standard_normal(16000)                      # 1초 오디오 @16 kHz
print("1D, N=16000:    K   direct(ms)   fft(ms)  winner")
for K in (3, 9, 17, 33, 65, 129, 257, 513, 1025, 2049):
    h = rng.standard_normal(K)
    td = med(lambda: np.convolve(x, h)); tf = med(lambda: signal.fftconvolve(x, h))
    assert np.allclose(np.convolve(x, h), signal.fftconvolve(x, h))
    print(f"            {K:5d}  {td*1e3:9.3f}  {tf*1e3:8.3f}  {'fft' if tf < td else 'direct'}")
img = rng.standard_normal((256, 256))
print("2D, 256x256:    K   direct(ms)   fft(ms)  winner")
for K in (3, 5, 7, 9, 13, 17, 25, 33):
    h = rng.standard_normal((K, K)); ref = signal.correlate(img, h, mode="valid", method="direct") if K <= 5 else None
    td = med(lambda: direct2d(img, h), 7); tf = med(lambda: signal.fftconvolve(img, h[::-1, ::-1], mode="valid"), 7)
    if ref is not None: assert np.allclose(direct2d(img, h), ref) and np.allclose(signal.fftconvolve(img, h[::-1, ::-1], mode="valid"), ref)
    print(f"            {K:5d}  {td*1e3:9.3f}  {tf*1e3:8.3f}  {'fft' if tf < td else 'direct'}")
```

```text
1D, N=16000:    K   direct(ms)   fft(ms)  winner
                3      0.010     0.240  direct
                9      0.019     0.243  direct
               17      0.115     0.229  direct
               33      0.135     0.231  direct
               65      0.174     0.228  direct
              129      0.286     0.234  fft
              257      0.517     0.210  fft
              513      1.009     0.217  fft
             1025      3.100     0.224  fft
             2049      6.040     0.238  fft
2D, 256x256:    K   direct(ms)   fft(ms)  winner
                3      0.438     0.706  direct
                5      1.318     0.721  fft
                7      2.285     0.695  fft
                9      3.774     0.737  fft
               13      8.542     0.781  fft
               17     12.895     0.750  fft
               25     26.897     0.731  fft
               33     43.244     0.728  fft
```

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 335"><text x="10" y="20" font-size="14" text-anchor="start">직접 conv vs FFT conv — 커널 크기 K에 따른 시간 (numpy/scipy 실측, 로그-로그)</text><line x1="70" y1="270" x2="320" y2="270" stroke="currentColor"/><line x1="70" y1="50" x2="70" y2="270" stroke="currentColor"/><text x="195.0" y="44" font-size="13" text-anchor="middle">1D: N = 16000 (1초 오디오)</text><line x1="66" y1="270.0" x2="70" y2="270.0" stroke="currentColor"/><text x="64" y="274.0" font-size="12" text-anchor="end">0.01</text><line x1="66" y1="196.7" x2="70" y2="196.7" stroke="currentColor"/><text x="64" y="200.7" font-size="12" text-anchor="end">0.1</text><line x1="66" y1="123.3" x2="70" y2="123.3" stroke="currentColor"/><text x="64" y="127.3" font-size="12" text-anchor="end">1</text><line x1="66" y1="50.0" x2="70" y2="50.0" stroke="currentColor"/>
<text x="64" y="54.0" font-size="12" text-anchor="end">10</text><text x="70.0" y="286" font-size="12" text-anchor="middle">3</text><text x="112.1" y="286" font-size="12" text-anchor="middle">9</text><text x="161.9" y="286" font-size="12" text-anchor="middle">33</text><text x="214.1" y="286" font-size="12" text-anchor="middle">129</text><text x="267.0" y="286" font-size="12" text-anchor="middle">513</text><text x="320.0" y="286" font-size="12" text-anchor="middle">2049</text><polyline points="70.0,270.0 112.1,249.6 136.4,192.2 161.9,187.1 187.8,179.0 214.1,163.2 240.5,144.3 267.0,123.0 293.5,87.3 320.0,66.1" fill="none" stroke="#4a7bd0" stroke-width="2.5"/><polyline points="70.0,168.8 112.1,168.4 136.4,170.3 161.9,170.0 187.8,170.4 214.1,169.6 240.5,173.0 267.0,172.0 293.5,171.0 320.0,169.1" fill="none" stroke="#e08a3c" stroke-width="2.5"/>
<circle cx="70.0" cy="270.0" r="3" fill="#4a7bd0"/><circle cx="70.0" cy="168.8" r="3" fill="#e08a3c"/><circle cx="112.1" cy="249.6" r="3" fill="#4a7bd0"/><circle cx="112.1" cy="168.4" r="3" fill="#e08a3c"/><circle cx="136.4" cy="192.2" r="3" fill="#4a7bd0"/><circle cx="136.4" cy="170.3" r="3" fill="#e08a3c"/><circle cx="161.9" cy="187.1" r="3" fill="#4a7bd0"/><circle cx="161.9" cy="170.0" r="3" fill="#e08a3c"/><circle cx="187.8" cy="179.0" r="3" fill="#4a7bd0"/><circle cx="187.8" cy="170.4" r="3" fill="#e08a3c"/><circle cx="214.1" cy="163.2" r="3" fill="#4a7bd0"/><circle cx="214.1" cy="169.6" r="3" fill="#e08a3c"/><circle cx="240.5" cy="144.3" r="3" fill="#4a7bd0"/><circle cx="240.5" cy="173.0" r="3" fill="#e08a3c"/><circle cx="267.0" cy="123.0" r="3" fill="#4a7bd0"/><circle cx="267.0" cy="172.0" r="3" fill="#e08a3c"/><circle cx="293.5" cy="87.3" r="3" fill="#4a7bd0"/>
<circle cx="293.5" cy="171.0" r="3" fill="#e08a3c"/><circle cx="320.0" cy="66.1" r="3" fill="#4a7bd0"/><circle cx="320.0" cy="169.1" r="3" fill="#e08a3c"/><line x1="400" y1="270" x2="650" y2="270" stroke="currentColor"/><line x1="400" y1="50" x2="400" y2="270" stroke="currentColor"/><text x="525.0" y="44" font-size="13" text-anchor="middle">2D: 256×256 이미지, K×K</text><line x1="396" y1="270.0" x2="400" y2="270.0" stroke="currentColor"/><text x="394" y="274.0" font-size="12" text-anchor="end">0.1</text><line x1="396" y1="196.7" x2="400" y2="196.7" stroke="currentColor"/><text x="394" y="200.7" font-size="12" text-anchor="end">1</text><line x1="396" y1="123.3" x2="400" y2="123.3" stroke="currentColor"/><text x="394" y="127.3" font-size="12" text-anchor="end">10</text><line x1="396" y1="50.0" x2="400" y2="50.0" stroke="currentColor"/>
<text x="394" y="54.0" font-size="12" text-anchor="end">100</text><text x="400.0" y="286" font-size="12" text-anchor="middle">3</text><text x="453.3" y="286" font-size="12" text-anchor="middle">5</text><text x="514.5" y="286" font-size="12" text-anchor="middle">9</text><text x="552.9" y="286" font-size="12" text-anchor="middle">13</text><text x="650.0" y="286" font-size="12" text-anchor="middle">33</text><polyline points="400.0,223.0 453.3,187.9 488.3,170.3 514.5,154.4 552.9,128.4 580.8,115.2 621.1,91.8 650.0,76.7" fill="none" stroke="#4a7bd0" stroke-width="2.5"/><polyline points="400.0,207.8 453.3,207.1 488.3,208.3 514.5,206.4 552.9,204.5 580.8,205.8 621.1,206.6 650.0,206.8" fill="none" stroke="#e08a3c" stroke-width="2.5"/><circle cx="400.0" cy="223.0" r="3" fill="#4a7bd0"/><circle cx="400.0" cy="207.8" r="3" fill="#e08a3c"/><circle cx="453.3" cy="187.9" r="3" fill="#4a7bd0"/>
<circle cx="453.3" cy="207.1" r="3" fill="#e08a3c"/><circle cx="488.3" cy="170.3" r="3" fill="#4a7bd0"/><circle cx="488.3" cy="208.3" r="3" fill="#e08a3c"/><circle cx="514.5" cy="154.4" r="3" fill="#4a7bd0"/><circle cx="514.5" cy="206.4" r="3" fill="#e08a3c"/><circle cx="552.9" cy="128.4" r="3" fill="#4a7bd0"/><circle cx="552.9" cy="204.5" r="3" fill="#e08a3c"/><circle cx="580.8" cy="115.2" r="3" fill="#4a7bd0"/><circle cx="580.8" cy="205.8" r="3" fill="#e08a3c"/><circle cx="621.1" cy="91.8" r="3" fill="#4a7bd0"/><circle cx="621.1" cy="206.6" r="3" fill="#e08a3c"/><circle cx="650.0" cy="76.7" r="3" fill="#4a7bd0"/><circle cx="650.0" cy="206.8" r="3" fill="#e08a3c"/><text x="195" y="305" font-size="12" text-anchor="middle">K (탭 수)</text><text x="525" y="305" font-size="12" text-anchor="middle">K</text><text x="20" y="160" font-size="12" text-anchor="middle">ms</text>
<text x="355" y="160" font-size="12" text-anchor="middle">ms</text><line x1="70" y1="322" x2="94" y2="322" stroke="#4a7bd0" stroke-width="3"/><text x="100" y="326" font-size="12" text-anchor="start">직접 (np.convolve / numpy shift-add)</text><line x1="380" y1="322" x2="404" y2="322" stroke="#e08a3c" stroke-width="3"/><text x="410" y="326" font-size="12" text-anchor="start">FFT (scipy.signal.fftconvolve)</text></svg>
```

그림 5 — 위 표를 로그-로그로 그린 것. 직접 계산(파랑)은 K에 비례해 오르고, FFT(주황)는 K와 거의 무관하게 평평하다. 1D는 K = 65와 129 사이, 2D는 K = 3과 5 사이에서 교차한다.

출력에서 볼 것: 1D 교차점은 65~129탭 — 손계산(100~150)과 같은 자릿수다(같은 스크립트를 두 번 더 돌렸을 때도 교차점은 65~129 사이였다). 2D는 직접 비용이 K²로 늘어 K = 5에서 이미 FFT가 이긴다. 다만 이 2D 직접 구현은 numpy의 "이미지 전체 axpy"라서 매 탭마다 이미지를 메모리로 왕복한다 — 레지스터 블로킹한 C 커널이면 교차점이 더 오른쪽으로 간다. 1D `np.convolve`가 K = 9와 17 사이에서 6배 뛰는 계단도 보인다(numpy 내부 구현 경로가 바뀌는 것으로 보이지만 원인은 확인하지 않았다).

ML 엔지니어에게 의미:

- **CNN의 3×3 conv에는 FFT를 거의 안 쓴다.** 커널이 작고, 채널이 많고, 출력 크기만큼 FFT 크기를 키워야 해서 메모리가 커진다. 3×3에는 Winograd가 맞는 도구다.
- **긴 FIR**(오디오 잔향, matched filter, 긴 1D conv, 큰 receptive field의 일부 오디오 모델)에는 FFT가 맞다. 스트리밍이면 블록 단위 **overlap-add / overlap-save**(scipy의 `oaconvolve`)로 한다. Don이 아는 DSP 블록 처리와 같다.
- MCU에서는 CMSIS-DSP의 `arm_rfft_fast_f32`·`arm_rfft_q15`로 FFT를 하고, 고정소수점 FFT는 단계마다 스케일링(1/2)으로 오버플로를 막는 대신 SNR을 잃는다(E4·G5).
- 이미 MFCC를 위해 FFT를 계산하고 있다면(G5) 그 스펙트럼 위에서 필터를 적용하는 것이 공짜에 가깝다 — 알고리즘 선택은 **파이프라인 전체**를 보고 한다.

### 4.6 Strassen — 이름만 알아 두기

Strassen 알고리즘(1969)은 2×2 블록 행렬곱을 곱셈 8번 대신 7번으로 하고, 재귀로 O(n^2.807)을 얻는다. 면접에서 "행렬곱 연산량을 줄이는 알고리즘"으로 언급할 수 있지만 edge 추론에서는 거의 안 쓴다: 덧셈이 18번으로 늘고, 이득이 나려면 n이 수백~수천이어야 하고, 수치 안정성이 나쁘고, 블록 재귀가 캐시·SIMD 친화적인 GEMM 블로킹과 맞지 않는다. Winograd conv가 "작은 타일 + 고정 weight"라는 CNN의 특성 덕에 실용적인 것과 대조된다.

### 4.7 함정

- **연산 수 절감 = 시간 절감으로 착각.** Winograd 곱셈 2.25배 절감이 시간으로는 1.6~2.1배였다. 변환, 메모리 재배치, 버퍼 크기가 나머지를 먹는다.
- **int8에 그대로 적용.** 변환 영역 범위가 10~15비트로 커진다(4.4절). bit-exact 테스트가 깨지는 게 정상이다.
- **"FFT가 O(N log N)이니 항상 빠르다".** 작은 K에서는 상수항(FFT 3번, 복소수, 메모리 패스)이 이긴다. 1D에서 K = 3이면 FFT가 24배 느렸다.
- **2D 직접 구현이 느려서 생긴 거짓 교차점.** 처음 scipy의 범용 direct 경로로 쟀을 때는 3×3에서도 FFT가 9배 빨랐다. 비교 대상을 제대로 짜지 않으면 "FFT가 항상 이긴다"는 잘못된 결론이 나온다.

---

## 5. MCU에서 DMA와 연산 겹치기

### 5.1 E5·E7과 무엇이 다른가

E5 5.5절은 NPU를 "load·compute·store 세 엔진이 독립"인 이상적인 모델로 Python 시뮬레이션했고, E7 5.4절은 DMA descriptor와 ping-pong의 구조를 다뤘다. MCU에서는 다음이 추가된다 — Don이 SSD 펌웨어에서 매일 보던 것들이다.

- **CPU가 DMA를 건다.** descriptor 설정, 채널 enable, 완료 인터럽트(ISR)가 모두 **CPU 시간**이다. NPU처럼 command stream이 알아서 돌지 않는다.
- **캐시 maintenance.** Cortex-M7·M55처럼 D-cache가 있으면 DMA가 쓴 버퍼를 CPU가 읽기 전에 해당 범위를 invalidate해야 한다(E7 6절). 이것도 CPU 시간이고, 바이트에 비례한다.
- **연산 시간이 흔들린다(jitter).** 다른 ISR(오디오 I2S, BLE)이 끼어들고, 데이터에 따라 분기가 달라진다. 평균만 맞춰 버퍼를 2개로 잡으면 가끔씩 CPU가 데이터를 기다린다.
- **SRAM 예산이 빡빡하다.** 버퍼 3개 × 타일 크기가 수십 KB를 넘기 어렵다.

예시 상황(가정): Hark 같은 웨어러블의 MCU가 dense 레이어 weight 64 KiB를 외부 OSPI flash/PSRAM에서 SRAM으로 스트리밍하며 계산한다(내부 SRAM에 모델 전체가 안 들어가는 경우). 숫자는 설명용으로 정한 것이다.

| 파라미터 | 값 | 의미 |
|---|---|---|
| DMA 대역폭 | 0.25 B/cycle (200 MHz에서 50 MB/s) | OSPI급 외부 메모리 가정 |
| DMA 시작 지연 | 200 cycle | 버스 중재 + 첫 burst |
| 타일당 CPU 오버헤드 | 150 cycle | descriptor 설정 + 완료 ISR |
| 캐시 invalidate | 1/16 cycle/B (32 B 라인당 2 cycle) | 캐시 없는 SRAM이면 0 |
| 연산 | 4 cycle/B (시나리오 B는 2) | 예: weight 바이트당 MAC 몇 개 + requant |
| jitter | 0 또는 ±50 % | 시나리오 C |

### 5.2 손계산 — 타일 2 KiB, 32개

```
DMA 한 타일   = 지연 200 + 2048 B ÷ 0.25 B/cycle        = 200 + 8192 = 8392 cycle
CPU 한 타일   = 오버헤드 150 + invalidate 2048/16 + 연산 2048 × 4 = 150 + 128 + 8192 = 8470 cycle

single buffer : DMA 끝나야 계산, 계산 끝나야 다음 DMA → 32 × (8392 + 8470)        = 539,584 cycle
double buffer : 첫 DMA 하나만 노출, 이후는 느린 쪽(CPU 8470)이 지배 → 8392 + 32 × 8470 = 279,432 cycle
triple buffer : 병목(CPU)이 100 % 바쁘면 버퍼를 늘려도 그대로                       = 279,432 cycle

시나리오 B (연산 2 cycle/B → CPU 한 타일 = 150 + 128 + 4096 = 4374):
single : 32 × (8392 + 4374) = 408,512      double : 32 × 8392 + 마지막 CPU 4374 = 272,918  (DMA가 병목)
```

말로 하면: 겹치기를 하면 총 시간은 **"느린 쪽 × 타일 수 + 양 끝에 한 번씩 노출되는 빠른 쪽"**이 된다. 겹치기는 병목을 숨길 뿐 없애지 못한다 — 시나리오 B에서는 아무리 버퍼를 늘려도 DMA 32 × 8392 cycle 아래로 내려가지 않는다.

### 5.3 C 시뮬레이션 — single / double / triple, jitter, 타일 크기

이벤트 기반 시뮬레이터다. 규칙은 펌웨어 ping-pong 그대로: DMA 채널은 하나(직렬), 타일 i의 DMA는 **그 버퍼 슬롯이 비어야**(이전 사용자의 연산이 끝나야) 걸 수 있고, CPU는 데이터가 도착하고 자신이 비어야 시작한다. 확인할 것: 손계산과 같은 숫자, jitter가 있을 때 triple의 효과.

```c
/* MCU DMA/연산 겹치기 시뮬레이터 — 외부 flash/PSRAM의 weight를 타일 단위로 SRAM 버퍼에 DMA하며 계산 */
#include <stdio.h>
#include <stdint.h>
typedef struct { double bw;      /* DMA 대역폭 [byte/cycle] */
                 double lat;     /* DMA 시작 지연 [cycle] */
                 double ovh;     /* 타일당 CPU 오버헤드: descriptor 설정 + 완료 ISR [cycle] */
                 double inv;     /* 캐시 invalidate [cycle/byte] (캐시 없는 SRAM이면 0) */
                 double cpb;     /* 연산 [cycle/byte] */
                 double jit; } sys_t;   /* 연산 시간 흔들림 비율 (0.3 → ±30 %) */
static uint32_t rng = 1;
static double urand(void) { rng = rng * 1664525u + 1013904223u; return (rng >> 8) / 16777216.0; }
/* nb = 버퍼 개수(1, 2, 3). 반환: 총 cycle. busy = 순수 연산 cycle의 비율 */
double simulate(const sys_t *s, int total, int B, int nb, double *busy, int verbose) {
    enum { MAXT = 4096 }; static double ds[MAXT], de[MAXT], cs[MAXT], ce[MAXT];
    int n = (total + B - 1) / B; double work = 0; rng = 1;
    for (int i = 0; i < n; i++) {
        double kick = (i < nb) ? 0 : (nb == 1 ? ce[i - 1] : cs[i - nb + 1]);   /* 빈 슬롯이 생긴 뒤에야 DMA를 건다 */
        double dma_free = i ? de[i - 1] : 0;                                    /* DMA 채널은 하나: 직렬 */
        ds[i] = kick > dma_free ? kick : dma_free;
        de[i] = ds[i] + s->lat + B / s->bw;
        double comp = B * s->cpb * (1 + s->jit * (2 * urand() - 1)); work += comp;
        double cpu_free = i ? ce[i - 1] : 0;                                    /* 데이터가 도착하고 CPU가 비어야 시작 */
        cs[i] = cpu_free > de[i] ? cpu_free : de[i];
        ce[i] = cs[i] + s->ovh + B * s->inv + comp;
        if (verbose) printf("  tile %d: DMA %6.0f-%6.0f  CPU %6.0f-%6.0f\n", i, ds[i], de[i], cs[i], ce[i]);
    }
    *busy = work / ce[n - 1]; return ce[n - 1];
}
int main(int argc, char **argv) {
    (void)argv;
    sys_t base = {.bw = 0.25, .lat = 200, .ovh = 150, .inv = 1.0 / 16, .cpb = 4.0, .jit = 0.0};
    const char *nm[3] = {"single", "double", "triple"}; double busy, T;
    for (int sc = 0; sc < 3; sc++) {
        sys_t s = base; if (sc == 1) s.cpb = 2.0; if (sc == 2) s.jit = 0.5;
        printf("%s (DMA %.0f cyc/B, compute %.0f cyc/B, jitter ±%.0f%%), 64 KiB in 2 KiB tiles\n",
               sc == 0 ? "A: compute ~ DMA" : sc == 1 ? "B: DMA-bound" : "C: A + jitter", 1 / s.bw, s.cpb, s.jit * 100);
        for (int nb = 1; nb <= 3; nb++) { T = simulate(&s, 65536, 2048, nb, &busy, 0);
            printf("   %-6s buffer: %8.0f cycles = %6.2f ms @200MHz  CPU compute busy %5.1f%%\n", nm[nb - 1], T, T / 200e3, busy * 100); }
    }
    if (argc > 1) {                                   /* 타일 크기 스윕: SRAM 버퍼 예산 24 KiB 안에서 */
        sys_t s = base; s.jit = 0.5;
        printf("tile sweep (scenario C, 64 KiB, SRAM budget 24 KiB):\n   tile   double(ms)  triple(ms)\n");
        int tiles[] = {128, 256, 512, 1024, 2048, 4096, 8192, 12288};
        for (int k = 0; k < 8; k++) { int B = tiles[k]; double t2 = simulate(&s, 65536, B, 2, &busy, 0), t3 = simulate(&s, 65536, B, 3, &busy, 0);
            printf("   %5d   %8.3f   ", B, t2 / 200e3);
            if (3 * B <= 24576) printf("%8.3f\n", t3 / 200e3); else printf("  (3 x %d > 24 KiB)\n", B); }
        for (int nb = 1; nb <= 3; nb++) { printf("gantt %s, 6 tiles x 2 KiB, scenario C:\n", nm[nb - 1]); simulate(&s, 6 * 2048, 2048, nb, &busy, 1); }
    }
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 d1_dma.c -o d1_dma && ./d1_dma sweep
```

```text
A: compute ~ DMA (DMA 4 cyc/B, compute 4 cyc/B, jitter ±0%), 64 KiB in 2 KiB tiles
   single buffer:   539584 cycles =   2.70 ms @200MHz  CPU compute busy  48.6%
   double buffer:   279432 cycles =   1.40 ms @200MHz  CPU compute busy  93.8%
   triple buffer:   279432 cycles =   1.40 ms @200MHz  CPU compute busy  93.8%
B: DMA-bound (DMA 4 cyc/B, compute 2 cyc/B, jitter ±0%), 64 KiB in 2 KiB tiles
   single buffer:   408512 cycles =   2.04 ms @200MHz  CPU compute busy  32.1%
   double buffer:   272918 cycles =   1.36 ms @200MHz  CPU compute busy  48.0%
   triple buffer:   272918 cycles =   1.36 ms @200MHz  CPU compute busy  48.0%
C: A + jitter (DMA 4 cyc/B, compute 4 cyc/B, jitter ±50%), 64 KiB in 2 KiB tiles
   single buffer:   527876 cycles =   2.64 ms @200MHz  CPU compute busy  47.4%
   double buffer:   301218 cycles =   1.51 ms @200MHz  CPU compute busy  83.1%
   triple buffer:   280906 cycles =   1.40 ms @200MHz  CPU compute busy  89.2%
tile sweep (scenario C, 64 KiB, SRAM budget 24 KiB):
   tile   double(ms)  triple(ms)
     128      1.952      1.837
     256      1.709      1.595
     512      1.604      1.485
    1024      1.552      1.432
    2048      1.506      1.405
    4096      1.548      1.466
    8192      1.581      1.542
   12288      1.757     (3 x 12288 > 24 KiB)
gantt single, 6 tiles x 2 KiB, scenario C:
  tile 0: DMA      0-  8392  CPU   8392- 14703
  tile 1: DMA  14703- 23095  CPU  23095- 30494
  tile 2: DMA  30494- 38886  CPU  38886- 47391
  tile 3: DMA  47391- 55783  CPU  55783- 65931
  tile 4: DMA  65931- 74323  CPU  74323- 79111
  tile 5: DMA  79111- 87503  CPU  87503- 94904
gantt double, 6 tiles x 2 KiB, scenario C:
  tile 0: DMA      0-  8392  CPU   8392- 14703
  tile 1: DMA   8392- 16784  CPU  16784- 24183
  tile 2: DMA  16784- 25176  CPU  25176- 33681
  tile 3: DMA  25176- 33568  CPU  33681- 43829
  tile 4: DMA  33681- 42073  CPU  43829- 48617
  tile 5: DMA  43829- 52221  CPU  52221- 59622
gantt triple, 6 tiles x 2 KiB, scenario C:
  tile 0: DMA      0-  8392  CPU   8392- 14703
  tile 1: DMA   8392- 16784  CPU  16784- 24183
  tile 2: DMA  16784- 25176  CPU  25176- 33681
  tile 3: DMA  25176- 33568  CPU  33681- 43829
  tile 4: DMA  33568- 41960  CPU  43829- 48617
  tile 5: DMA  41960- 50352  CPU  50352- 57753
```

출력에서 볼 것:

1. 시나리오 A·B의 숫자가 5.2절 손계산과 **정확히 같다**(539,584 / 279,432 / 408,512 / 272,918). 시뮬레이터를 믿기 전에 손계산과 맞춰 보는 것이 첫 검증이다.
2. jitter가 없으면 triple은 double과 같다. **jitter ±50 %가 생기면 double이 1.40 → 1.51 ms로 8 % 느려지고, triple이 그 손실을 거의 다 되찾는다**(1.40 ms). 아래 Gantt에서 이유를 본다.
3. DMA-bound(B)에서는 CPU가 48 %만 바쁘다. 이때 할 일은 버퍼 추가가 아니라 바이트 줄이기(weight int4, 압축, 내부 SRAM 상주)나 다른 일을 CPU에 주는 것이다.

### 5.4 Gantt로 보기 — triple buffering이 jitter를 흡수하는 순간

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 335"><text x="10" y="20" font-size="14" text-anchor="start">버퍼 1·2·3개 타임라인 — 6 타일 × 2 KiB, DMA 4 cyc/B, 연산 4 cyc/B ±50 % (시뮬레이션)</text><text x="10" y="52" font-size="13" text-anchor="start">single</text><text x="84" y="71" font-size="12" text-anchor="end">DMA</text><text x="84" y="97" font-size="12" text-anchor="end">CPU</text><rect x="90.0" y="58" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="113.9" y="71" font-size="12" text-anchor="middle">0</text><rect x="137.8" y="84" width="35.0" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="155.8" y="97" font-size="12" text-anchor="middle">0</text><rect x="173.8" y="58" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="197.7" y="71" font-size="12" text-anchor="middle">1</text>
<rect x="221.6" y="84" width="41.2" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="242.7" y="97" font-size="12" text-anchor="middle">1</text><rect x="263.8" y="58" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="287.7" y="71" font-size="12" text-anchor="middle">2</text><rect x="311.7" y="84" width="47.5" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="335.9" y="97" font-size="12" text-anchor="middle">2</text><rect x="360.1" y="58" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="384.0" y="71" font-size="12" text-anchor="middle">3</text><rect x="408.0" y="84" width="56.8" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="436.9" y="97" font-size="12" text-anchor="middle">3</text><rect x="465.8" y="58" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="489.7" y="71" font-size="12" text-anchor="middle">4</text><rect x="513.6" y="84" width="26.3" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="527.3" y="97" font-size="12" text-anchor="middle">4</text><rect x="540.9" y="58" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="564.8" y="71" font-size="12" text-anchor="middle">5</text><rect x="588.8" y="84" width="41.2" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="609.9" y="97" font-size="12" text-anchor="middle">5</text><line x1="631.0" y1="54" x2="631.0" y2="106" stroke="#d0564a" stroke-dasharray="4 3"/><text x="635.0" y="52" font-size="12" text-anchor="start">끝 94904</text><text x="10" y="137" font-size="13" text-anchor="start">double</text><text x="84" y="156" font-size="12" text-anchor="end">DMA</text><text x="84" y="182" font-size="12" text-anchor="end">CPU</text>
<rect x="90.0" y="143" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="113.9" y="156" font-size="12" text-anchor="middle">0</text><rect x="137.8" y="169" width="35.0" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="155.8" y="182" font-size="12" text-anchor="middle">0</text><rect x="137.8" y="143" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="161.8" y="156" font-size="12" text-anchor="middle">1</text><rect x="185.7" y="169" width="41.2" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="206.8" y="182" font-size="12" text-anchor="middle">1</text><rect x="185.7" y="143" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="209.6" y="156" font-size="12" text-anchor="middle">2</text><rect x="233.5" y="169" width="47.5" height="18" fill="#e08a3c" fill-opacity="0.75"/>
<text x="257.7" y="182" font-size="12" text-anchor="middle">2</text><rect x="233.5" y="143" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="257.4" y="156" font-size="12" text-anchor="middle">3</text><rect x="282.0" y="169" width="56.8" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="310.9" y="182" font-size="12" text-anchor="middle">3</text><rect x="282.0" y="143" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="305.9" y="156" font-size="12" text-anchor="middle">4</text><rect x="339.8" y="169" width="26.3" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="353.5" y="182" font-size="12" text-anchor="middle">4</text><rect x="339.8" y="143" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="363.7" y="156" font-size="12" text-anchor="middle">5</text>
<rect x="387.7" y="169" width="41.2" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="408.8" y="182" font-size="12" text-anchor="middle">5</text><line x1="429.8" y1="139" x2="429.8" y2="191" stroke="#d0564a" stroke-dasharray="4 3"/><text x="433.8" y="137" font-size="12" text-anchor="start">끝 59622</text><text x="10" y="222" font-size="13" text-anchor="start">triple</text><text x="84" y="241" font-size="12" text-anchor="end">DMA</text><text x="84" y="267" font-size="12" text-anchor="end">CPU</text><rect x="90.0" y="228" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="113.9" y="241" font-size="12" text-anchor="middle">0</text><rect x="137.8" y="254" width="35.0" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="155.8" y="267" font-size="12" text-anchor="middle">0</text><rect x="137.8" y="228" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="161.8" y="241" font-size="12" text-anchor="middle">1</text><rect x="185.7" y="254" width="41.2" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="206.8" y="267" font-size="12" text-anchor="middle">1</text><rect x="185.7" y="228" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="209.6" y="241" font-size="12" text-anchor="middle">2</text><rect x="233.5" y="254" width="47.5" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="257.7" y="267" font-size="12" text-anchor="middle">2</text><rect x="233.5" y="228" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="257.4" y="241" font-size="12" text-anchor="middle">3</text><rect x="282.0" y="254" width="56.8" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="310.9" y="267" font-size="12" text-anchor="middle">3</text>
<rect x="281.3" y="228" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="305.3" y="241" font-size="12" text-anchor="middle">4</text><rect x="339.8" y="254" width="26.3" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="353.5" y="267" font-size="12" text-anchor="middle">4</text><rect x="329.2" y="228" width="46.8" height="18" fill="#4a7bd0" fill-opacity="0.75"/><text x="353.1" y="241" font-size="12" text-anchor="middle">5</text><rect x="377.0" y="254" width="41.2" height="18" fill="#e08a3c" fill-opacity="0.75"/><text x="398.1" y="267" font-size="12" text-anchor="middle">5</text><line x1="419.2" y1="224" x2="419.2" y2="276" stroke="#d0564a" stroke-dasharray="4 3"/><text x="423.2" y="222" font-size="12" text-anchor="start">끝 57753</text><line x1="90.0" y1="296" x2="90.0" y2="300" stroke="currentColor"/>
<text x="90.0" y="314" font-size="12" text-anchor="middle">0k</text><line x1="204.0" y1="296" x2="204.0" y2="300" stroke="currentColor"/><text x="204.0" y="314" font-size="12" text-anchor="middle">20k</text><line x1="318.0" y1="296" x2="318.0" y2="300" stroke="currentColor"/><text x="318.0" y="314" font-size="12" text-anchor="middle">40k</text><line x1="432.0" y1="296" x2="432.0" y2="300" stroke="currentColor"/><text x="432.0" y="314" font-size="12" text-anchor="middle">60k</text><line x1="546.0" y1="296" x2="546.0" y2="300" stroke="currentColor"/><text x="546.0" y="314" font-size="12" text-anchor="middle">80k</text><line x1="660.0" y1="296" x2="660.0" y2="300" stroke="currentColor"/><text x="660.0" y="314" font-size="12" text-anchor="middle">100k</text><line x1="90" y1="296" x2="660" y2="296" stroke="currentColor"/>
<text x="375" y="328" font-size="12" text-anchor="middle">시간 (cycle) — 파랑: DMA가 타일을 SRAM에 채움, 주황: CPU 오버헤드 + 연산</text></svg>
```

그림 6 — 시나리오 C의 처음 6 타일. 파랑은 DMA, 주황은 CPU(오버헤드 + 연산). double에서 타일 3의 연산이 길어지자(33,681~43,829), 타일 4용 DMA는 타일 2의 버퍼가 비는 33,681까지 기다렸다가 시작하고, 타일 5용 DMA는 타일 3이 끝나는 43,829까지 기다린다. 그 결과 타일 4가 일찍 끝난 뒤 CPU가 48,617~52,221 동안 데이터를 기다린다. triple은 타일 5용 슬롯이 이미 비어 있어서 DMA가 타일 4 전송 직후(41,960)에 바로 이어지고, CPU 대기가 48,617~50,352로 double(48,617~52,221)의 절반 이하다.

말로 하면: double buffering은 "평균적으로" DMA와 연산이 맞을 때 충분하다. **연산 시간이 들쭉날쭉하면** 긴 타일 하나가 다음 DMA의 시작을 막고, 그 뒤의 짧은 타일에서 CPU가 굶는다. 버퍼 하나를 더 두면 DMA가 한 타일 더 앞서 나갈 수 있어서 흔들림을 흡수한다. SSD에서 NAND read 지연이 들쭉날쭉할 때 큐 깊이를 늘리던 것과 같은 원리다(Little's law, E7 4.6절).

### 5.5 타일 크기 고르기 — SRAM 예산 안에서

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 320"><text x="10" y="20" font-size="14" text-anchor="start">타일 크기 vs 총 시간 — 64 KiB weight 스트리밍, SRAM 버퍼 예산 24 KiB (시뮬레이션)</text><line x1="80" y1="270" x2="640" y2="270" stroke="currentColor"/><line x1="80" y1="40" x2="80" y2="270" stroke="currentColor"/><line x1="76" y1="270.0" x2="80" y2="270.0" stroke="currentColor"/><text x="73" y="274.0" font-size="12" text-anchor="end">1.3</text><line x1="76" y1="237.1" x2="80" y2="237.1" stroke="currentColor"/><text x="73" y="241.1" font-size="12" text-anchor="end">1.4</text><line x1="76" y1="204.3" x2="80" y2="204.3" stroke="currentColor"/><text x="73" y="208.3" font-size="12" text-anchor="end">1.5</text><line x1="76" y1="171.4" x2="80" y2="171.4" stroke="currentColor"/><text x="73" y="175.4" font-size="12" text-anchor="end">1.6</text>
<line x1="76" y1="138.6" x2="80" y2="138.6" stroke="currentColor"/><text x="73" y="142.6" font-size="12" text-anchor="end">1.7</text><line x1="76" y1="105.7" x2="80" y2="105.7" stroke="currentColor"/><text x="73" y="109.7" font-size="12" text-anchor="end">1.8</text><line x1="76" y1="72.9" x2="80" y2="72.9" stroke="currentColor"/><text x="73" y="76.9" font-size="12" text-anchor="end">1.9</text><line x1="76" y1="40.0" x2="80" y2="40.0" stroke="currentColor"/><text x="73" y="44.0" font-size="12" text-anchor="end">2.0</text><text x="80.0" y="286" font-size="12" text-anchor="middle">128</text><text x="165.0" y="286" font-size="12" text-anchor="middle">256</text><text x="250.1" y="286" font-size="12" text-anchor="middle">512</text><text x="335.1" y="286" font-size="12" text-anchor="middle">1024</text><text x="420.2" y="286" font-size="12" text-anchor="middle">2048</text>
<text x="505.2" y="286" font-size="12" text-anchor="middle">4096</text><text x="590.3" y="286" font-size="12" text-anchor="middle">8192</text><text x="640.0" y="286" font-size="12" text-anchor="middle">12288</text><polyline points="80.0,55.8 165.0,135.6 250.1,170.1 335.1,187.2 420.2,202.3 505.2,188.5 590.3,177.7 640.0,119.8" fill="none" stroke="#4a7bd0" stroke-width="2.5"/><polyline points="80.0,93.6 165.0,173.1 250.1,209.2 335.1,226.6 420.2,235.5 505.2,215.5 590.3,190.5" fill="none" stroke="#3f9a6b" stroke-width="2.5"/><circle cx="80.0" cy="55.8" r="3.5" fill="#4a7bd0"/><circle cx="80.0" cy="93.6" r="3.5" fill="#3f9a6b"/><circle cx="165.0" cy="135.6" r="3.5" fill="#4a7bd0"/><circle cx="165.0" cy="173.1" r="3.5" fill="#3f9a6b"/><circle cx="250.1" cy="170.1" r="3.5" fill="#4a7bd0"/><circle cx="250.1" cy="209.2" r="3.5" fill="#3f9a6b"/><circle cx="335.1" cy="187.2" r="3.5" fill="#4a7bd0"/>
<circle cx="335.1" cy="226.6" r="3.5" fill="#3f9a6b"/><circle cx="420.2" cy="202.3" r="3.5" fill="#4a7bd0"/><circle cx="420.2" cy="235.5" r="3.5" fill="#3f9a6b"/><circle cx="505.2" cy="188.5" r="3.5" fill="#4a7bd0"/><circle cx="505.2" cy="215.5" r="3.5" fill="#3f9a6b"/><circle cx="590.3" cy="177.7" r="3.5" fill="#4a7bd0"/><circle cx="590.3" cy="190.5" r="3.5" fill="#3f9a6b"/><circle cx="640.0" cy="119.8" r="3.5" fill="#4a7bd0"/><text x="240" y="304" font-size="12" text-anchor="middle">타일 크기 (byte, log2 축)</text><text x="24" y="155" font-size="12" text-anchor="middle">ms</text><text x="100" y="60" font-size="12" text-anchor="start">← 작은 타일: 타일당 고정 비용(설정·ISR·DMA 지연)이 쌓인다</text><text x="380" y="150" font-size="12" text-anchor="start">큰 타일: 첫 DMA·마지막 연산이 안 겹친다 →</text><line x1="440" y1="296" x2="464" y2="296" stroke="#4a7bd0" stroke-width="3"/>
<text x="470" y="300" font-size="12" text-anchor="start">double</text><line x1="530" y1="296" x2="554" y2="296" stroke="#3f9a6b" stroke-width="3"/><text x="560" y="300" font-size="12" text-anchor="start">triple (3B ≤ 24 KiB)</text></svg>
```

그림 7 — 타일 크기 스윕(시나리오 C, 버퍼 예산 24 KiB). 작은 타일은 타일당 고정 비용(DMA 지연 200 + CPU 오버헤드 150 cycle)이 쌓이고, 큰 타일은 처음 DMA 하나와 마지막 연산 하나가 겹치지 못하는 시간이 커진다. triple은 3 × 8 KiB까지만 예산에 들어간다.

손계산 (작은 타일): 128 B 타일이면 DMA 전송 512 cycle + 연산 512 cycle에 고정 비용 350 cycle이 붙는다 → 고정 비용이 타일 시간의 40 % 안팎. 2 KiB 타일이면 350 ÷ 8192 ≈ 4 %. 큰 타일: 12 KiB 타일이면 처음 DMA(200 + 49,152 cycle ≈ 0.25 ms)가 통째로 노출된다.

결정 절차:

1. **SRAM 예산**을 먼저 정한다: 이 레이어가 쓸 수 있는 버퍼 = 전체 SRAM − arena(K1) − 스택 − 다른 태스크. 여기서는 24 KiB.
2. jitter가 크면 **triple**, 아니면 double. 버퍼 수 × 타일 ≤ 예산.
3. 타일 크기는 "고정 비용 ≪ 타일 시간"이 되는 가장 작은 크기부터 키워 보고, U자 바닥을 고른다. 여기서는 2 KiB(triple 1.405 ms).
4. 타일 경계를 **연산의 자연스러운 단위**(weight 행 몇 개, 출력 채널 몇 개)에 맞춘다. 그래야 타일마다 커널 호출이 깔끔하다.
5. 마지막으로 **실측**: DWT CYCCNT로 "DMA 완료 시각"과 "CPU가 대기한 시간"을 GPIO 토글이나 trace로 찍어 위 Gantt를 실제로 그린다(K2).

### 5.6 캐시 maintenance와 버스 경합 — 모델이 빠뜨린 것

- **invalidate 범위·정렬**: 위 모델은 32 B 라인당 2 cycle로 가정했다. 실제 비용은 코어·캐시 크기에 따라 다르고, 버퍼가 라인 정렬이 아니면 이웃 데이터를 망가뜨리는 정확성 버그가 된다(E7 6절). DMA 버퍼는 라인 정렬 + 라인 크기의 배수로 잡는다. 아니면 MPU로 그 영역을 non-cacheable로 둬서 maintenance 자체를 없앤다(대신 CPU 접근이 느려진다 — 이것도 측정 대상).
- **출력 쪽**: 결과를 DMA로 내보내면 그 전에 clean(write-back)이 필요하다. 이 모델은 입력 스트리밍만 다뤘다. 출력도 있으면 E5의 load·compute·store 3단 모델이 된다.
- **버스 경합**: DMA와 CPU가 같은 SRAM bank·버스를 쓰면 서로 느려진다. 모델은 둘이 완전히 독립이라고 가정했다. MCU의 SRAM이 여러 bank로 나뉘어 있으면(예: 여러 SRAM 블록 + 버스 매트릭스) **DMA 버퍼와 CPU가 주로 읽는 데이터(코드·스택·weight)를 다른 bank에 두는 것**이 실무 요령이다. 이 효과는 시뮬레이션으로는 안 나오고 보드에서만 보인다.
- **ISR 지연**: 다른 높은 우선순위 ISR이 DMA 완료 ISR을 늦추면 그만큼 다음 DMA가 늦게 걸린다. 링크드 descriptor(scatter-gather, E7 5.1절)로 DMA가 다음 타일을 **CPU 개입 없이** 이어 가게 하면 타일당 CPU 오버헤드와 이 지연이 함께 사라진다.

펌웨어 골격은 이렇다(구조 설명용 의사 코드):

```
init:   dma_start(slot0, tile0); dma_start_queued(slot1, tile1); dma_start_queued(slot2, tile2)   // triple
loop i: wait(dma_done[i % 3])                  // 완료 ISR이 세운 플래그 또는 세마포어
        cache_invalidate(slot[i % 3])          // D-cache가 있으면
        compute(slot[i % 3])                   // 커널
        if (i + 3 < n) dma_start_queued(slot[i % 3], tile[i + 3])   // 방금 비운 슬롯에 3칸 앞 타일
```

---

## 6. 고정소수점 전용 트릭 (짧게)

int8 커널의 기본 구조는 J3, Q 형식·포화는 E4에 있다. 여기서는 "최적화 기법" 관점에서 네 가지만 짚는다.

### 6.1 requant 합치기

int8 conv의 출력 단계는 `bias 더하기 → 곱하기(M0) → 시프트 → zero-point 더하기 → clamp(activation)`다. 최적화 순서: (1) input zero-point 항을 bias에 미리 접기(J3 2.5절 "bias 접기"), (2) ReLU·ReLU6를 **clamp 범위에 합치기**(별도 패스 없음), (3) per-channel이면 M0·shift 배열을 출력 채널 루프 바깥에서 레지스터로 올리기(2.7절 LICM). 결과적으로 출력 원소당 "곱 1 + 시프트 1 + 덧셈 1 + SSAT 1" 수준이 된다.

### 6.2 포화 명령은 컴파일러가 찾아 준다 — 확인만 하자

```c
#include <stdint.h>
int8_t clamp_i8(int32_t v) { return (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v); }
int16_t add_sat16(int16_t a, int16_t b) { int32_t s = (int32_t)a + b; return (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s); }
uint32_t div_rt(uint32_t a, uint32_t n) { return a / n; }
uint32_t div_c7(uint32_t a) { return a / 7; }
```

```text
clamp_i8:
	ssat	r0, #8, r0
	bx	lr
add_sat16:
	qadd16	r0, r0, r1
	sxth	r0, r0
	bx	lr
div_rt:
	udiv	r0, r0, r1
	bx	lr
div_c7:
	movw	r1, #18725
	movt	r1, #9362
	umull	r1, r2, r0, r1
	subs	r0, r0, r2
	add.w	r0, r2, r0, lsr #1
	lsrs	r0, r0, #2
	bx	lr
```

출력에서 볼 것(Cortex-M4, `-O2`): 삼항 연산자 clamp가 `SSAT` 한 명령, int16 포화 덧셈이 `QADD16`, **상수 7로 나누기**는 나눗셈 없이 `UMULL`(곱 + 상위 32비트) 기반의 "magic number" 곱으로 바뀌었다(0x24924925). 반면 **런타임 값 n으로 나누기**는 `UDIV`(2~12 cycle) 그대로다. 그래서 나눗셈 피하기의 실무 대상은 "런타임 나눗셈"이다.

### 6.3 LUT로 비선형 함수

int8 입력은 256가지뿐이라 sigmoid·tanh·exp·GELU는 256칸 표로 끝난다(B1, J3 6절의 softmax LUT). 최적화 관점의 요점: 표가 SRAM·TCM에 있어야 하고(flash XIP이면 wait state), 표 load가 앞 load에 의존하는 패턴이라 2.6절의 software pipelining 대상이다. int16 입력이면 65,536칸(128 KB)이 너무 크므로 구간 선형 보간(256구간 + 기울기)이나 다항식 근사를 쓴다.

### 6.4 나눗셈 피하기 — 역수 곱과 그 검증

average pooling은 경계에서 창 안의 원소 수 n이 달라서(SAME 패딩) 런타임 나눗셈이 생긴다. n의 범위가 작으면 역수 표 `R[n] = ceil(2^S / n)`를 만들어 곱하고 시프트한다. 문제는 **S가 충분히 커야 정확**하다는 것이다.

손으로 유도: `R = (2^S + e) / n`, 0 ≤ e < n이라 하자. `a · R / 2^S = a / n + a · e / (n · 2^S)`. 몫이 정확하려면 오차항이 `a / n`의 소수부를 다음 정수로 넘기지 않아야 하고, 충분조건은 `a · e < 2^S`. 여기서 a < 2^14(분자 = |합| + n/2 ≤ 128 × 64 + 32 = 8224), e < n ≤ 64 = 2^6이므로 `a · e < 2^20` → **S ≥ 20이면 충분**. 전수 검사로 확인한다.

```c
#include "bench.h"
/* 반올림 나눗셈 (0에서 먼 쪽으로 반올림) — avg pool에서 경계마다 개수 n이 달라 런타임 나눗셈 */
static inline int32_t div_ref(int32_t sum, int32_t n) { return sum >= 0 ? (sum + n / 2) / n : -((-sum + n / 2) / n); }
/* 역수 곱: R[n] = ceil(2^24 / n), 곱하고 24비트 시프트. 분자 < 2^14 이고 n ≤ 64 이면 정확(아래에서 전수 검사) */
static uint32_t R[65];
static inline int32_t div_mul(int32_t sum, int32_t n) {
    uint32_t a = (uint32_t)(sum >= 0 ? sum : -sum) + (uint32_t)(n / 2);
    int32_t q = (int32_t)(((uint64_t)a * R[n]) >> 24); return sum >= 0 ? q : -q;
}
NOINLINE void pool_div(const int32_t *s, const int32_t *n, int8_t *o, int len) { for (int i = 0; i < len; i++) o[i] = (int8_t)div_ref(s[i], n[i]); }
NOINLINE void pool_mul(const int32_t *s, const int32_t *n, int8_t *o, int len) { for (int i = 0; i < len; i++) o[i] = (int8_t)div_mul(s[i], n[i]); }
int main(void) {
    for (int n = 1; n <= 64; n++) R[n] = (uint32_t)(((1ull << 24) + n - 1) / n);
    for (int S = 12; S <= 24; S += 4) {          /* 전수 검사: n = 1..64, sum = n×(-128) .. n×127, 시프트 S별 */
        long bad = 0, cnt = 0;
        for (int n = 1; n <= 64; n++) { uint64_t r = ((1ull << S) + n - 1) / n;
            for (int s = -128 * n; s <= 127 * n; s++, cnt++) { uint32_t a = (uint32_t)(s >= 0 ? s : -s) + (uint32_t)(n / 2);
                int32_t q = (int32_t)((a * r) >> S); bad += div_ref(s, n) != (s >= 0 ? q : -q); } }
        printf("shift %2d: %ld cases, mismatches = %ld\n", S, cnt, bad); }
    enum { L = 1 << 16 }; int32_t *s = malloc(L * 4), *n = malloc(L * 4); int8_t *o1 = malloc(L), *o2 = malloc(L);
    for (int i = 0; i < L; i++) { n[i] = 1 + (int32_t)((i * 2654435761u) >> 26); s[i] = (int32_t)(((i * 40503u) % 256) - 128) * n[i]; }
    double a, b; BENCH(a, 31, 50, pool_div(s, n, o1, L)); BENCH(b, 31, 50, pool_mul(s, n, o2, L));
    printf("divide: %6.1f us  reciprocal-mul: %6.1f us  (%.1fx)  mismatch=%d\n", a * 1e6, b * 1e6, a / b, memcmp(o1, o2, L) != 0);
    return 0;
}
```

```text
shift 12: 530464 cases, mismatches = 184666
shift 16: 530464 cases, mismatches = 6830
shift 20: 530464 cases, mismatches = 0
shift 24: 530464 cases, mismatches = 0
divide:   65.0 us  reciprocal-mul:   24.0 us  (2.7x)  mismatch=0
```

출력에서 볼 것: S = 16에서도 53만 건 중 6,830건이 1 틀린다 — **"대충 맞는" 역수 곱은 bit-exact 테스트에서 걸린다.** 유도한 대로 S = 20부터 0건이다. 속도는 M2에서 2.4~2.8×(세 번 실행). M4에서는 UDIV/SDIV(2~12 cycle) 대신 UMULL(1 cycle)이므로 이득이 더 클 것으로 예상한다. 이런 변환은 **입력 범위 전체를 전수 검사할 수 있을 때만** 넣는다 — 여기서는 53만 건이라 1초도 안 걸렸다.

---

## 7. 최적화 검증 — 매 단계 테스트, 그리고 성능 회귀

### 7.1 무엇을 "같다"고 할 것인가

| 변환 | 정수 커널 | float 커널 |
|---|---|---|
| unroll, interchange(누산 순서 유지), strip-mining, LICM, restrict, layout | bit-exact | bit-exact (이 노트의 측정 전부 `mismatch=0`) |
| FMA 축약(contraction) 여부가 바뀜 | 해당 없음 | 1 ulp 차이 가능 (2.1절 최대 차이 1.19e-07) |
| 누산 순서 변경 (다중 accumulator, 벡터 reduction, jam의 일부) | bit-exact (정수 덧셈은 결합법칙 성립, 오버플로 없으면) | 허용 오차 필요 |
| Winograd, FFT | requant 지점이 바뀌어 bit-exact 불가 | 상대 오차 1e-6(fp32 F2) ~ 1e-2(fp16 F4) |
| 역수 곱(나눗셈 대체) | 범위 전수 검사로 bit-exact 확인 가능 | 1 ulp 차이 (컴파일러는 `-ffast-math` 없이 안 함) |

실무 규칙(J3, J6, C8):

1. 최적화 전에 **기준 구현**(느리지만 명백히 맞는 reference)을 고정한다.
2. 변환 하나마다 **무작위 입력 + 경계 입력**(최댓값·최솟값·0·패딩 경계)으로 기준과 비교한다. 정수는 bit-exact, float은 "기준의 최대 오차 × 몇 배"를 허용 오차로 문서화한다.
3. 입력 공간이 작으면(나눗셈 표, LUT) **전수 검사**한다(6.4절).
4. 정확도 지표(모델 출력 top-1, wake word FAR/FRR)는 커널 테스트를 대신하지 못하고, 커널 테스트도 모델 지표를 대신하지 못한다. 둘 다 돌린다(C8).

### 7.2 성능 회귀 게이트 — 잡음을 아는 임계값

빨라진 커널이 다음 커밋에서 조용히 느려지는 것을 막으려면 CI에 성능 게이트를 둔다(I5, K2). 핵심은 **임계값을 측정 잡음에 맞추는 것**이다. 이 노트의 C 벤치마크 3회 실행 로그(`runs.txt`)에서 지표 다섯 개의 잡음을 재고, 가상의 새 빌드 결과를 판정해 본다.

```python
import re, statistics as st
# runs.txt = 이 노트의 C 벤치마크를 3번 돌린 실제 로그. 지표 몇 개를 뽑아 잡음 폭을 잰다
pat = {"FIR K=5 const (ms)": r"FIR K=5 const :\s+([\d.]+)", "fused 16M (us)": r"n=16777216 .*fused\s+([\d.]+)",
       "winograd (ms)": r"winograd:\s+([\d.]+)", "dw NCHW8c+4px (us)": r"NCHW8c .*block\s+([\d.]+)",
       "pointwise NHWC (us)": r"NHWC\s+([\d.]+) us\s+[\d.]+ GFLOP/s \|? ?mismatch"}
log = open("runs.txt").read()
base = {k: [float(v) for v in re.findall(p, log)] for k, p in pat.items()}
def gate(name, new):                     # 기준 median + max(5 %, 관측된 상대 잡음 폭 × 1.5)를 넘으면 실패
    xs = base[name]; m = st.median(xs); noise = (max(xs) - min(xs)) / m; thr = m * (1 + max(0.05, 1.5 * noise))
    return m, noise, thr, ("FAIL" if new > thr else "ok")
cand = {"FIR K=5 const (ms)": 0.212, "fused 16M (us)": 2600.0, "winograd (ms)": 6.1,
        "dw NCHW8c+4px (us)": 63.0, "pointwise NHWC (us)": 480.0}       # 가상의 새 빌드 결과
for k, new in cand.items():
    m, noise, thr, res = gate(k, new)
    print(f"{k:22s} runs={base[k]}  median={m:g}  noise={noise*100:4.1f}%  threshold={thr:8.3f}  new={new:g} -> {res}")
```

```text
FIR K=5 const (ms)     runs=[0.208, 0.208, 0.213]  median=0.208  noise= 2.4%  threshold=   0.218  new=0.212 -> ok
fused 16M (us)         runs=[3101.0, 2182.0, 2351.0]  median=2351  noise=39.1%  threshold=3729.500  new=2600 -> ok
winograd (ms)          runs=[5.84, 6.74, 6.94]  median=6.74  noise=16.3%  threshold=   8.390  new=6.1 -> ok
dw NCHW8c+4px (us)     runs=[51.8, 51.6, 51.9]  median=51.8  noise= 0.6%  threshold=  54.390  new=63 -> FAIL
pointwise NHWC (us)    runs=[470.7, 470.7, 538.3]  median=470.7  noise=14.4%  threshold= 572.100  new=480 -> ok
```

출력에서 볼 것: 캐시 안에서 도는 커널(FIR, depthwise)은 잡음이 0.6~2.4 %라 임계값이 촘촘하고, 21 % 느려진 depthwise를 잡아냈다. DRAM을 쓰는 fused elementwise는 잡음이 39 %라 임계값이 59 % 위에 있다 — **이 게이트로는 30 % 회귀도 못 잡는다.** 그래서:

- 호스트 CI에서는 잡음이 큰 지표를 **실행 횟수를 늘리거나**, 시간 대신 **명령 수·cycle 수**(Linux `perf stat`의 instructions, 시뮬레이터)로 본다.
- MCU에서는 DWT CYCCNT가 거의 결정적(캐시 없는 SRAM, 인터럽트 차단 시)이라 **cycle 단위로 정확한 게이트**를 걸 수 있다. 이것은 MCU 쪽의 큰 장점이다(K2, J6의 HIL).
- 이 노트처럼 2.1절의 M4 어셈블리 명령 수(`loopstat`)를 CI에 걸어 두는 것도 싸고 결정적인 방법이다: "이 커널의 안쪽 루프 명령 수가 늘면 실패".

---

## 8. 임베디드 관점에서 다시 보기

같은 처방이라도 타깃마다 효과가 다르다. 이 노트의 측정과 어셈블리 관찰을 타깃별로 정리한다(Hexagon·NPU 열은 공개 자료 수준의 일반론이다).

| 기법 | Cortex-M4 (in-order, DSP ext) | Cortex-M55 (Helium) | Cortex-A / Apple (OoO, NEON) | DSP (Hexagon·HiFi, VLIW) / NPU |
|---|---|---|---|---|
| unroll | 루프 오버헤드가 그대로 cycle → 이득 | 저오버헤드 루프(`LE`, `LETP`)가 대신함 | 컴파일러가 함, 손으로는 무의미 | 컴파일러 modulo scheduling의 재료 |
| unroll-and-jam | 레지스터 13개가 jam 폭 상한 | Q 레지스터 8개가 상한 | 1.2~1.3× (load가 병목일 때) | 핵심 기법 (넓은 레지스터 파일) |
| interchange | 캐시 없으면 효과 작음 | 캐시 있는 구성은 이득 | 33× (연속 + 벡터화) | DMA 패턴으로 대체 |
| fusion | SRAM 왕복 제거 | 동일 | 2~4× (DRAM일 때) | 그래프 컴파일러가 함 (C6) |
| fission | spill 제거로 이득 | 동일 | 손해일 수 있음 | 레지스터 압박 시 |
| restrict·LICM | 명령 1.8배 감소 관찰 | 동일 + 벡터화 조건 | 런타임 검사가 가려 줌 | 필수 (컴파일러가 보수적) |
| strength reduction | SDIV 2~12 cycle 제거 | 동일 | `%`가 더 빠를 수도 | 나눗셈 유닛이 없거나 느림 |
| AoS → SoA | SMLAD 짝 맞추기 쉬움 | 벡터 load 연속 | 7× | 사실상 필수 |
| NHWC / NCHWc | CMSIS-NN은 NHWC | NHWC, 채널 16의 배수 유리 | op에 따라 다름 | 벤더 blocked 형식 |
| Winograd | int8 bit-exact 깨짐 → 거의 안 씀 | 동일 | fp32 3×3에 1.6~2.1× | fp16에서 F(2×2) 정도 |
| FFT conv | 긴 FIR·오디오에만 | 동일 | K ≥ 100 근처부터 | 오디오 DSP에서 흔함 |
| DMA double/triple | CPU가 DMA·ISR·invalidate 부담 | 동일 + 캐시 | 해당 없음(하드웨어 prefetch) | 핵심 (TCM/VTCM 스트리밍) |

MCU 쪽 실무 순서를 C 함수 하나로 요약하면 이렇다: **포인터에 restrict → 파라미터는 지역 변수로 → 안쪽 루프는 연속 접근(SoA/NHWC) → 상수 크기 경로 특수화 → 레지스터가 허락하는 만큼 unroll-and-jam → 런타임 나눗셈 제거 → 입력이 외부 메모리면 DMA triple buffering**. 각 단계 뒤에 bit-exact 테스트와 CYCCNT 측정.

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 프로파일 없이 최적화 | 커널은 3배, 전체는 4 % | Amdahl: 비중이 작은 커널 | 시간 비중 순으로 대상 선정 (1.2절) |
| 벤치마크가 최적화로 사라짐 | 0.00 µs, 비현실적인 GB/s | 순수 함수를 컴파일러가 루프 밖으로 뺌 | 결과 소비, `escape()`, 어셈블리 확인 (0.2절) |
| 손 unroll을 기대 | 변화 없음 | 컴파일러가 이미 함 | `-Rpass=loop-vectorize`, `-S`로 먼저 확인 |
| int8 포인터 aliasing | M4에서 계수 재load, spill | char 계열 포인터는 무엇과도 alias 가능 | `restrict`, 지역 변수 복사 (2.7절) |
| `restrict` 함수에 in-place 호출 | 최적화 레벨마다 다른 출력 | 약속 위반 = 정의되지 않은 동작 | in-place 금지 문서화, assert(out != in) |
| 레이아웃만 바꿈 | 오히려 느려짐 | 그 레이아웃을 쓰는 블로킹이 없음 | 레이아웃 + 블로킹을 함께 (3.2절) |
| 블록을 너무 크게 | 성능 급락 | 레지스터 초과 → spill | 레지스터 수를 손으로 세기 (3.2절) |
| op마다 전치 | 레이어 사이에 Transpose가 시간의 20 % | 레이아웃 변환 비용 | 그래프 전체에 한 레이아웃 (3.4절) |
| int8에 Winograd | bit-exact 실패, 정확도 하락 | 변환이 범위를 10~15비트로 키움 | int8 경로는 direct·im2col 유지 (4.4절) |
| 작은 커널에 FFT | 24배 느림 | FFT 상수항 | K가 수십~백 이상일 때만 (4.5절) |
| double buffer인데 CPU가 가끔 대기 | 프레임 시간이 가끔 튐 | 연산 jitter | triple buffering, 링크드 descriptor (5.4절) |
| DMA 버퍼 캐시 라인 비정렬 | 이웃 변수가 가끔 깨짐 | 부분 라인 invalidate | 라인 정렬·라인 배수 크기 (5.6절, E7) |
| 역수 곱의 시프트가 작음 | 0.1~1 % 입력에서 1 LSB 차이 | 반올림 오차 | 오차 한계 유도 + 전수 검사 (6.4절) |

---

## 10. 면접에서 이렇게 말한다

**Q.** "Walk me through how you would optimize a slow conv kernel on an MCU."

**A.** 먼저 전체 프로파일에서 그 conv가 몇 %인지 보고(Amdahl), FLOP·바이트를 세서 roofline 위 위치를 정한다. 지붕보다 한참 아래면 구현 문제다: 어셈블리와 벡터화 리포트를 보고 접근 패턴·aliasing·루프 오버헤드를 고친다. 그다음 NHWC 레이아웃과 출력 블로킹(unroll-and-jam)으로 weight·입력 재사용을 늘리고, SMLAD·Helium 명령이 쓰이는지 확인한다. 입력이 외부 메모리면 DMA double/triple buffering으로 겹친다. 매 단계 reference와 bit-exact 비교, CYCCNT로 측정.

> I start with the end-to-end profile to make sure the conv is worth it, then place it on the roofline. If it is far below the roof, it's an implementation problem: I check the assembly and vectorization remarks, fix aliasing with restrict, make the inner loop contiguous, and specialize the 3×3 path. Then I block the outputs so each weight load feeds several MACs, verify the SIMD instructions like SMLAD are used, and overlap weight streaming with compute using double or triple buffered DMA. Every step is checked bit-exact against the reference kernel and measured with the cycle counter.

**Q.** "What is Winograd convolution and what are its trade-offs?"

**A.** 3×3 conv를 작은 타일로 쪼개, 입력·필터를 덧셈 위주의 변환으로 바꾼 뒤 원소별 곱을 하고 다시 변환하는 알고리즘이다. F(2×2,3×3)은 출력 2×2당 곱셈 36 → 16(2.25배), F(4×4,3×3)은 4배 줄인다. 대가는 변환 오버헤드, 추가 버퍼, 수치 오차(F4는 fp16에서 크다), 그리고 int8에서는 변환이 값을 10~15비트로 키워 SIMD 폭과 bit-exact를 잃는다는 것. 그래서 fp32/fp16의 3×3 stride-1 conv, 채널이 많은 레이어에 쓰고 int8 MCU 커널에는 거의 안 쓴다.

> Winograd computes a small output tile, like 2×2, from a 4×4 input tile using transforms that are mostly additions, so the multiplications drop from 36 to 16 per tile, or by 4x with the 4×4 variant. The filter transform is done offline. The costs are transform overhead, extra buffers, and numerical error that grows with tile size. For int8 it's awkward because the input transform expands the range to about 10 bits, so you lose int8 SIMD width and bit-exactness. I'd use it for float 3×3 stride-1 convs with many channels; on my machine it gave about 1.8x on a 64-channel layer.

**Q.** "AoS versus SoA — when does it matter?"

**A.** 한 필드를 많은 샘플에 걸쳐 처리할 때(필터, 특징 추출, SIMD) SoA가 이긴다. AoS에서는 필요 없는 필드까지 캐시 라인으로 옮기고 같은 필드가 띄엄띄엄 있어 벡터 load가 안 된다. IMU 가속도 energy 계산에서 SoA가 7배 빨랐다. 샘플 하나의 모든 필드를 같이 쓰고 SIMD를 안 쓰면 AoS도 괜찮다. 센서 FIFO가 AoS로 들어오면 전처리 입구에서 한 번 SoA로 바꾼다.

> It matters whenever you process one field across many samples, which is most DSP and ML preprocessing. With AoS you drag unused fields through the cache and the same field is strided, so the compiler can't use vector loads. On IMU data, computing acceleration energy was about 7x faster with SoA because that loop vectorized and the AoS one didn't. If the sensor FIFO delivers AoS, I convert once at the input of the pipeline, or use an AoSoA block equal to the vector width.

**Q.** "How do you overlap DMA and compute on an MCU?"

**A.** 버퍼를 둘 이상 두고 CPU가 한 버퍼로 계산하는 동안 DMA가 다음 타일을 다른 버퍼에 채운다. 총 시간은 "느린 쪽 × 타일 수 + 양 끝"이 되고, 병목 자체는 못 없앤다. MCU 특유의 비용 — DMA 설정·완료 ISR, 캐시 invalidate, 버스 경합 — 을 타일당 고정 비용으로 넣어서 타일 크기를 고르고, 연산 시간이 흔들리면 triple buffering이나 링크드 descriptor를 쓴다. 버퍼는 캐시 라인 정렬, 가능하면 CPU 데이터와 다른 SRAM bank.

> I use ping-pong or triple buffers: while the CPU computes on one tile, the DMA fills the next. Total time becomes the slower of the two times the number of tiles, plus one exposed transfer, so it hides the bottleneck but doesn't remove it. On an MCU I also budget the CPU-side costs: programming the descriptor, the completion ISR, and cache invalidation, which push you toward larger tiles, while the SRAM budget pushes back. If compute time jitters, a third buffer or linked descriptors keep the CPU from stalling. Buffers are cache-line aligned and ideally in a different SRAM bank than the CPU's working data.

**Q.** "When is FFT-based convolution faster than direct?"

**A.** 직접은 N × K, FFT는 대략 c · L log L이고 K와 거의 무관하다. 그래서 커널이 길 때 이긴다. 이 Mac에서 1D 16,000 샘플은 65~129탭에서 뒤집혔고, 3탭에서는 FFT가 24배 느렸다. CNN의 3×3에는 안 맞고, 긴 FIR·잔향·matched filter·긴 1D conv에 맞는다. 스트리밍이면 overlap-add/overlap-save로 블록 처리한다.

> Direct convolution costs N times K, while FFT convolution costs a few FFTs of length about N plus K, roughly independent of K. So FFT wins for long kernels: in my measurement on a one-second 16 kHz signal the crossover was between 65 and 129 taps, and for a 3-tap filter FFT was 24 times slower. It's the wrong tool for 3×3 CNN layers but the right one for long FIR filters, reverb, or matched filters, using overlap-add for streaming.

**Q.** "Your optimized kernel is 2x faster in a microbenchmark but the app is only 5% faster. Why?"

**A.** Amdahl. 그 커널이 전체의 약 10 %면 2배가 5 %가 된다(`1 / (0.9 + 0.05) = 1.053`). 그 밖에 microbenchmark는 캐시가 따뜻하고 입력이 고정인데, 실제 앱에서는 캐시가 차갑고 다른 태스크·ISR이 끼어든다. 프로파일 비중과 실제 조건(cold cache, 동시 실행)에서 다시 재야 한다.

> Mostly Amdahl's law: if the kernel is about ten percent of the frame, a 2x speedup gives about five percent overall. Microbenchmarks also run with warm caches and fixed inputs, while the real app has cold caches, other tasks and interrupts. So I always confirm the time share in the end-to-end profile and re-measure under real conditions.

**Q.** "When does `restrict` actually help?"

**A.** 컴파일러가 포인터가 안 겹친다는 걸 증명 못 할 때. 데스크톱 컴파일러는 런타임 alias 검사로 벡터화를 해 주는 경우가 많아서 이 Mac에서는 차이가 없었다. 하지만 Cortex-M4로 컴파일한 6채널 보정 루프는 restrict 없이 store마다 계수를 다시 읽고 spill이 생겨서, restrict로 샘플당 명령이 54.5 → 29.75개가 됐다. 특히 int8 포인터는 char 계열이라 무엇과도 alias할 수 있어 int8 커널에서 중요하다.

> When the compiler can't prove that pointers don't overlap. On a desktop, clang often inserts runtime overlap checks, so I saw no difference on my Mac. But compiling a six-channel calibration loop for Cortex-M4, without restrict it reloaded every coefficient after each store and spilled registers; with restrict the instructions per sample dropped from about 55 to 30. It matters especially for int8 kernels, because char-type pointers may alias anything.

---

## 11. 직접 해보기

1. **Amdahl 손계산**: 프레임 10 ms 중 conv 6 ms, depthwise 2 ms, 나머지 2 ms. conv를 Winograd로 1.8배, depthwise를 블로킹으로 1.5배 빠르게 하면 전체 시간은? 정답: `6/1.8 + 2/1.5 + 2 = 3.33 + 1.33 + 2 = 6.67 ms` (1.5배).
2. **Winograd 손계산**: F(2,3)으로 d = [2, −1, 0, 3], g = [1, 2, 1]의 출력 2개를 m1~m4로 계산하고 직접 계산과 비교하라. 정답: m1 = 2, m2 = (−1)·2 = −2, m3 = 1·0 = 0, m4 = (−4)·1 = −4 → y0 = 0, y1 = 2. 직접: y0 = 2 − 2 + 0 = 0, y1 = −1 + 0 + 3 = 2.
3. **DMA 손계산**: DMA 3,000 cycle/타일, CPU 2,000 cycle/타일, 타일 20개. single, double의 총 시간과 CPU 바쁨 비율은? 정답: single 20 × 5,000 = 100,000 (40 %), double 20 × 3,000 + 2,000 = 62,000 (약 65 %). 더 빨리 하려면 버퍼가 아니라 바이트(DMA)를 줄여야 한다.
4. **코드 과제 — fission on M4**: 2.4절의 `cal_fission`에 `restrict`를 붙이면 채널 루프의 load 수가 어떻게 되나? `m4.sh`와 `loopstat.py`로 확인하라. 힌트: 계수 `*b`, `*g`가 루프 밖으로 나가 샘플당 load가 1개(입력)로 준다.
5. **코드 과제 — line buffer**: 2.5절의 분리형 필터를 "최근 5행만 담는 링버퍼"로 바꿔 중복 계산 없이 구현하고, S = 128 strip 버전과 시간·bit-exact를 비교하라. 힌트: 가로 결과를 링버퍼의 `(r % 5)`행에 쓰고, 세로 패스는 5행을 회전 순서로 읽는다. 중복이 없어지는 대신 행마다 포인터 회전이 생긴다.
6. **코드 과제 — triple의 조건**: `d1_dma.c`에서 jitter를 0, 0.1, 0.3, 0.5, 0.8로 바꿔 double 대비 triple의 이득을 표로 만들어라. 정답(직접 돌려 본 값): 연산 4 cyc/B(DMA와 같음)에서 double ÷ triple = 1.000(jitter 0), 1.017(0.1), 1.047(0.3), 1.072(0.5), 1.095(0.8). 연산 2 cyc/B(DMA가 확실한 병목)이면 jitter와 무관하게 1.000, 6 cyc/B(CPU가 병목)이면 0.5에서 1.013, 0.8에서 1.055. triple은 **두 엔진의 속도가 비슷하고 jitter가 클 때** 값을 한다.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| Amdahl's law | 부분 가속의 전체 효과 | `1 / ((1 − f) + f / s)`, 안 고친 부분이 상한 |
| arithmetic intensity | FLOP ÷ 바이트 | roofline에서 커널의 가로 위치 (D3) |
| unrolling | 루프 펼치기 | 몸통 복사로 분기·카운터 감소, 스케줄링 여지 |
| full unroll | 완전 펼치기 | 반복 수가 상수일 때 안쪽 루프 제거 → 바깥 루프 벡터화 |
| unroll-and-jam | 펼치고 합치기 | 바깥 루프 k배 + 안쪽 합치기 → load 재사용, 독립 누산기 |
| loop interchange | 루프 순서 교환 | 안쪽 루프를 연속 접근으로 |
| loop fusion | 루프 합치기 | 중간 배열 제거, 바이트 감소 |
| loop fission (distribution) | 루프 쪼개기 | 벡터화 분리, 레지스터 압박 완화 |
| strip-mining | 조각내기 | 루프를 조각 × 조각 수로, 다른 루프와 fusion할 준비 |
| tiling (cache blocking) | 다차원 조각내기 | 작업 집합을 캐시·SRAM 크기로 |
| software pipelining | 반복 겹치기 | i의 연산과 i+1의 load를 겹침, prologue/epilogue |
| LICM | 불변 코드 이동 | 반복마다 같은 계산·load를 루프 밖으로 |
| aliasing | 포인터 겹침 | 두 포인터가 같은 메모리를 가리킬 가능성 |
| `restrict` | 비겹침 약속 | C99 한정자, 위반하면 정의되지 않은 동작 |
| strength reduction | 연산 강도 낮추기 | 나눗셈·곱셈을 덧셈·비교·시프트로 |
| index set splitting | 범위 쪼개기 | 링버퍼를 두 연속 구간으로 나눠 분기·나눗셈 제거 |
| AoS / SoA / AoSoA | 구조체 배열 / 배열 구조체 / 묶음 | 필드 단위 연속 접근의 정도 |
| NCHW / NHWC / NCHWc | 텐서 레이아웃 | 가장 빠른 축이 w / c / 채널 묶음 안의 c |
| tail predication | 꼬리 마스킹 | Helium에서 마지막 벡터를 마스크로 처리 |
| Winograd F(m, r) | 최소 필터링 | 출력 m개, 필터 r탭을 곱셈 m + r − 1번으로 |
| overlap-add / save | 블록 FFT conv | 스트리밍 신호를 블록 단위로 FFT 합성곱 |
| Strassen | 행렬곱 알고리즘 | 2×2 블록 곱셈 8 → 7, 실무 edge에서는 드묾 |
| double / triple buffering | 다중 버퍼 | DMA와 연산 겹치기, triple은 jitter 흡수 |
| linked descriptor | 연결 DMA 기술자 | CPU 개입 없이 다음 전송으로 이어짐 |
| magic number division | 역수 곱 나눗셈 | 상수·표 기반으로 나눗셈을 곱 + 시프트로 |

---

## 13. 요약 & 체크리스트

커널 최적화는 "측정 → Amdahl로 대상 선정 → roofline으로 원인 분류 → 처방 하나 → 검증 → 재측정"의 반복이다. 지붕에서 멀면 구현 문제라 루프 변환(full unroll, interchange, 구간 분할, restrict, fission)과 레이아웃(SoA, NHWC/NCHWc + 블로킹)이 큰 배수를 준다 — 이 Mac에서 10~33배였다. 메모리 지붕에 붙었으면 fusion·strip-mining·int8·DMA 겹치기로 바이트를 줄이거나 숨기고, compute 지붕에 붙었으면 Winograd(곱셈 2.25배 절감, 실측 1.6~2.1배)나 특수 유닛으로 연산을 줄인다. 같은 변환이 OoO 코어와 in-order MCU에서 정반대 효과를 내기도 하므로(fission, `%` 제거, restrict), MCU 효과는 어셈블리 명령 수와 CYCCNT로 확인한다. Winograd는 int8에서 범위가 10~15비트로 커져 bit-exact가 깨지고, FFT conv는 긴 커널(1D 약 100탭 이상)에서만 이긴다. MCU의 DMA 겹치기는 CPU가 내는 설정·ISR·invalidate 비용과 연산 jitter까지 넣어 모델링해야 하고, jitter가 크면 triple buffering이 double의 손실을 되찾는다. 모든 단계는 bit-exact(정수) 또는 문서화된 허용 오차(float) 테스트와 잡음을 아는 성능 게이트로 지킨다.

- [ ] Amdahl 식으로 "이 커널을 s배 빠르게 하면 전체는 몇 배"를 손으로 계산할 수 있다
- [ ] 실측 ÷ roofline 비율로 구현·바이트·연산 문제를 판정하고 처방 후보를 댈 수 있다
- [ ] unroll, unroll-and-jam, interchange, fusion, fission, strip-mining, software pipelining, LICM, strength reduction을 각각 한 문장과 작은 C 예로 설명할 수 있다
- [ ] 같은 변환이 OoO 코어와 Cortex-M4에서 다르게 작동하는 예(fission, `%` 제거, restrict)를 숫자로 말할 수 있다
- [ ] AoS/SoA, NCHW/NHWC/NCHWc 중 커널에 맞는 것을 고르고 레지스터 수로 블록 크기를 정할 수 있다
- [ ] Winograd F(2,3)을 손으로 계산하고 F(2×2,3×3)의 곱셈 절감(36 → 16)과 int8 범위 확장(512, 10비트)을 유도할 수 있다
- [ ] FFT conv의 교차점을 연산량으로 어림하고 실측으로 확인할 수 있다
- [ ] single/double/triple buffering의 총 시간을 손계산하고, jitter·고정 비용·SRAM 예산으로 타일 크기를 고를 수 있다
- [ ] 역수 곱 나눗셈의 시프트 크기를 오차 한계로 유도하고 전수 검사로 확인할 수 있다
- [ ] 측정 잡음에 맞춘 성능 회귀 게이트를 만들고, 잡음이 큰 지표를 cycle·명령 수로 대체할 수 있다

## 참고 자료

- Andrew Lavin, Scott Gray, "Fast Algorithms for Convolutional Neural Networks", CVPR 2016 (arXiv:1509.09308) — Winograd F(2×2,3×3), F(4×4,3×3) 행렬의 출처
- Randy Allen, Ken Kennedy, "Optimizing Compilers for Modern Architectures" (Morgan Kaufmann, 2001) — 루프 변환(interchange, fusion, distribution, unroll-and-jam, strip-mining)의 표준 교재
- Samuel Williams, Andrew Waterman, David Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", Communications of the ACM, 2009
- John L. Hennessy, David A. Patterson, "Computer Architecture: A Quantitative Approach" — Amdahl의 법칙, 캐시, 소프트웨어 파이프라이닝
- Arm, "Cortex-M4 Technical Reference Manual" (instruction timing: SDIV/UDIV, load pipelining), "Armv8-M Architecture Reference Manual" (MVE/Helium tail predication) — developer.arm.com
- CMSIS-NN, CMSIS-DSP 소스와 문서 — https://github.com/ARM-software/CMSIS-NN, https://github.com/ARM-software/CMSIS-DSP
- SciPy 문서: `scipy.signal.fftconvolve`, `scipy.signal.oaconvolve`, `scipy.signal.choose_conv_method` — https://docs.scipy.org
- Clang 문서: optimization remarks (`-Rpass`, `-Rpass-missed`, `-Rpass-analysis`) — https://clang.llvm.org/docs/UsersManual.html
- oneDNN 문서: memory format propagation, blocked layouts (nChw8c, nChw16c) — https://oneapi-src.github.io/oneDNN/
- Volker Strassen, "Gaussian Elimination is not Optimal", Numerische Mathematik 13, 1969
- MIT 6.5940 TinyML and Efficient Deep Learning Computing (Song Han) — https://efficientml.ai
- 이 저장소의 선행 노트: D3(roofline), E1(캐시·tiling), E2(SIMD), E5(NPU tiling·double buffering), E7(DMA·캐시 일관성), J3(int8 커널), J6(테스트), C6(그래프 layout·fusion), K2(사이클 측정)
