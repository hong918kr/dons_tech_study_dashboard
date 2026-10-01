# J3. 고정소수점·SIMD 커널 직접 짜기 — int8 conv/depthwise/pool/softmax/add, requant, tiling, 검증

> **이 노트를 다 읽으면**: TFLite int8 규약을 C params 구조체로 옮기고 conv2d·depthwise·pool·add·softmax 커널을 TFLite reference와 **bit-exact**하게 짤 수 있다 · input offset·bias 접기·패딩이 서로 어떻게 얽히는지 설명하고 경계 버그를 피할 수 있다 · 루프 순서, im2col vs direct, 레지스터 블로킹, SDOT, 부분 im2col·타일링으로 같은 conv를 9.5 → 122 GMAC/s로 올리는 과정을 숫자로 설명할 수 있다 · Cortex-M4(SMLAD)·M55(Helium) 내부 루프를 읽고 MAC당 명령 수를 셀 수 있다 · randomized differential test + mutation test로 커널을 검증하고 면접 코딩 드릴 5개를 풀 수 있다
> **JD 연결**: "Optimizing models for MCUs & edge processors (Cortex-M/A, RISC-V, DSP)", "Integrate ML inference into embedded firmware in C, C++, or Rust" — study_prep_list **J3**: Q-format 연산, saturation, rounding / CMSIS-DSP/NN, NEON·Helium intrinsics / **INT8 matmul/conv 직접 작성** (커널 최적화, 코딩 인터뷰). 프로젝트 PJ7의 본체.
> **Don 기준 난이도**: 포화 연산, 반올림 시프트, 비트 조작, 루프·포인터 최적화, 레지스터 압박 감각은 이미 손에 익은 것 / TFLite 규약의 세부(패딩 앞쪽 계산, add의 left_shift 20, 두 번 반올림), im2col·블로킹이라는 "ML 커널 쪽 관용구", 그리고 커널을 **기준 구현과 1 LSB까지 맞추는** 검증 방법이 새로 배울 부분
> **선행 노트**: C1 (requantization M0/shift, SRDHM·RDBPOT, int8 dense C 커널), C2 (bit-exact requant), C8 (golden vector·bit-exact 판단), E2 (SMLAD·SXTB16, Helium, NEON SDOT/SMMLA, GEMM 블로킹 실측), E4 (Q15/Q31·포화), B1 (int8 ReLU6·LUT), B2 (conv 계산법), D4 (depthwise가 memory-bound인 이유), F1·F2 (TFLite·TFLM)

---

## 0. 큰 그림 — 커널 하나가 제품에 들어가기까지

C1에서 requantization 식을, E2에서 SIMD 명령을 배웠다. 그런데 면접관이 "int8 conv 커널을 C로 짜 보세요"라고 하거나, 실제로 새 DSP에 TFLite Micro를 올리다가 "CONV_2D가 느리니 직접 최적화 커널을 쓰자"는 결정을 하면, 필요한 건 그 둘을 **하나의 계약(contract)으로 묶는 능력**이다. 커널은 다음 세 가지를 동시에 만족해야 한다.

1. **규약 일치** — 변환기(converter)가 만들어 둔 scale·zero-point·multiplier·shift·padding·clamp를 그대로 해석한다. 하나라도 다르게 읽으면 숫자가 1 LSB씩, 혹은 경계에서만 틀린다.
2. **bit-exact** — 기준 구현(TFLite reference 커널)과 출력 int8이 **한 비트도 다르지 않아야** 한다. 정수 연산이라 가능하고(C8), 가능하니까 요구된다.
3. **빠르다** — 같은 결과를 SIMD·블로킹·타일링으로 몇 배~수십 배 빠르게 낸다. 그리고 빠르게 바꿀 때마다 2번을 다시 확인한다.

Don의 SSD 펌웨어 경험으로 치면, 1번은 "NVMe spec의 필드 정의대로 커맨드를 파싱하는 것", 2번은 "참조 모델(골든)과 비트 단위로 맞추는 회귀 테스트", 3번은 "FTL hot path를 프로파일하고 최적화하는 것"이다. 순서도 같다 — **스펙 → 정확한 느린 구현 → 검증 장치 → 최적화 → 재검증**.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 270"> <defs><marker id="a0" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <text x="10" y="22" font-size="14">int8 커널 개발 루프 — 스펙 해석 → 기준 구현 → 최적화 → 차등 검증</text> <rect x="10" y="45" width="120" height="56" rx="6" fill="#888" fill-opacity="0.18" stroke="#888"/> <text x="70" y="68" font-size="12" text-anchor="middle">.tflite 그래프</text><text x="70" y="86" font-size="12" text-anchor="middle">op + 텐서</text> <rect x="145" y="45" width="120" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/> <text x="205" y="68" font-size="12" text-anchor="middle">양자화 파라미터</text><text x="205" y="86" font-size="12" text-anchor="middle">s, zp, M0, shift</text> <rect x="280" y="45" width="120" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="#4a7bd0"/> <text x="340" y="68" font-size="12" text-anchor="middle">params 구조체</text><text x="340" y="86" font-size="12" text-anchor="middle">(C, 1절)</text> <rect x="415" y="45" width="120" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.18" stroke="#3f9a6b"/> <text x="475" y="68" font-size="12" text-anchor="middle">reference 커널</text><text x="475" y="86" font-size="12" text-anchor="middle">(2~6절)</text>
<rect x="550" y="45" width="120" height="56" rx="6" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/> <text x="610" y="68" font-size="12" text-anchor="middle">최적화 커널</text><text x="610" y="86" font-size="12" text-anchor="middle">SIMD (7~8절)</text> <line x1="130" y1="73" x2="143" y2="73" stroke="currentColor" marker-end="url(#a0)"/> <line x1="265" y1="73" x2="278" y2="73" stroke="currentColor" marker-end="url(#a0)"/> <line x1="400" y1="73" x2="413" y2="73" stroke="currentColor" marker-end="url(#a0)"/> <line x1="535" y1="73" x2="548" y2="73" stroke="currentColor" marker-end="url(#a0)"/> <rect x="10" y="165" width="255" height="56" rx="6" fill="#888" fill-opacity="0.18" stroke="#888"/> <text x="137" y="188" font-size="12" text-anchor="middle">TFLite BUILTIN_REF 인터프리터</text><text x="137" y="206" font-size="12" text-anchor="middle">= 골든 출력 (oracle)</text> <rect x="415" y="165" width="255" height="56" rx="6" fill="#d0564a" fill-opacity="0.18" stroke="#d0564a"/> <text x="542" y="188" font-size="12" text-anchor="middle">차등 비교 (9절)</text><text x="542" y="206" font-size="12" text-anchor="middle">mismatches == 0 ?</text> <line x1="70" y1="101" x2="70" y2="163" stroke="currentColor" marker-end="url(#a0)"/> <line x1="265" y1="193" x2="413" y2="193" stroke="currentColor" marker-end="url(#a0)"/> <line x1="475" y1="101" x2="475" y2="163" stroke="currentColor" marker-end="url(#a0)"/>
<line x1="610" y1="101" x2="610" y2="163" stroke="currentColor" marker-end="url(#a0)"/> <text x="280" y="185" font-size="12">같은 입력·파라미터</text> <text x="10" y="250" font-size="12">초록 = 느리지만 정확한 기준, 주황 = 빠른 버전. 주황은 언제나 초록·회색과 비트 단위로 비교된다.</text> </svg>
```

그림 1 — 이 노트의 작업 루프. 변환기가 만든 파라미터를 C 구조체로 옮기고, 느린 reference 커널을 먼저 TFLite reference와 bit-exact로 맞춘 다음, 최적화 커널을 만들 때마다 같은 비교를 다시 돌린다.

### 0.1 이미 다른 노트에 있는 것 — 여기서는 반복하지 않는다

| 주제 | 어디서 배웠나 | 이 노트에서는 |
|---|---|---|
| requant 식 유도, M0·shift 분해, SRDHM·RDBPOT 정의 | C1 7절, C8 3.4절 | 함수 3줄로 재사용만 한다 |
| int8 dense(GEMV) C 커널 bit-exact | C1 7절 | conv·depthwise·pool·add·softmax로 확장 |
| SMLAD·SXTB16 원리, MVE `VMLADAVA`, NEON SDOT/SMMLA, GEMM 블로킹 실측 | E2 2~5절 | **conv 내부 커널의 구조**(2×2 블록, 열 재사용, offset 처리)에 적용 |
| Q15/Q31 포화·guard bit | E4 | 드릴 1의 포화 누산에서 한 번 더 |
| int8 ReLU6·LUT activation | B1 | softmax LUT로 확장 |
| golden vector·HIL·정확도 회귀 | C8, J6 | **커널 단위** randomized differential test와 mutation test |

### 0.2 이 노트의 실험 환경

- 호스트: Apple M2 (P-core L1D 128 KB, L2 16 MB), Apple clang 21. C는 전부 `cc -std=c11 -Wall -Wextra -O2`로 컴파일했고 **경고 0개**였다. NEON 코드는 `-mcpu=apple-m2`를 더한다.
- MCU 쪽은 실물이 없어서 `--target=thumbv7em-none-eabihf -mcpu=cortex-m4`와 `--target=thumbv8.1m.main-none-eabihf -mcpu=cortex-m55`로 **어셈블리만** 보고, 커널 논리는 intrinsics를 C로 흉내 낸 host 빌드로 검증한다.
- 골든: `.venv-tf`의 TensorFlow 2.20 TFLite 인터프리터를 `experimental_op_resolver_type=BUILTIN_REF`(최적화 안 된 reference 커널)와 `experimental_preserve_all_tensors=True`(중간 텐서 보존)로 쓴다 (F1 8절과 같은 설정).
- 모든 스크래치 파일은 `/private/tmp/claude-501/j3/`에 있다. 노트 폴더에는 아무것도 남기지 않는다.

---

## 1. 커널 계약 — TFLite int8 규약을 C 구조체로

### 1.1 규약 한 장 요약

C1에서 배운 것을 "커널 작성자가 받는 입력"의 관점으로 다시 정리하면 아래 표가 전부다. 커널은 이 값들을 **해석만** 하고, 만들지 않는다 (만드는 건 변환기와 런타임의 Prepare 단계).

| 항목 | TFLite int8 규약 | 커널에서의 형태 |
|---|---|---|
| activation 레이아웃 | **NHWC** (batch, 높이, 너비, 채널). 채널이 가장 빨리 변한다 | 주소 = `((y·W + x)·C + c)` |
| conv weight 레이아웃 | **OHWI** = [출력채널][ky][kx][입력채널] | 한 출력 채널의 필터가 연속 |
| depthwise weight | [1][ky][kx][C] (channel multiplier 1일 때) | 채널이 가장 안쪽 |
| activation 양자화 | per-tensor, 비대칭: `r = s·(q − zp)`, zp ∈ [−128, 127] | `input_offset = −zp_in`, `output_offset = +zp_out` |
| weight 양자화 | **per-channel, 대칭**: zp = 0, 채널마다 s_w[oc] | zp 보정항 없음 |
| bias | int32, scale = s_in·s_w[oc], zp = 0 | 누산기에 그대로 더함 |
| 출력 배율 | M[oc] = s_in·s_w[oc] / s_out → (M0[oc], shift[oc]) | `multiplier[]`, `shift[]` 배열 |
| fused activation | ReLU·ReLU6는 별도 op 없이 clamp 범위로 | `act_min`, `act_max` |
| pooling | 입력·출력 scale·zp가 **같다** → requant 없음 | 평균의 반올림만 주의 |
| softmax 출력 | scale = 1/256, zp = −128로 **고정** | 출력 q = 256·p − 128 |

말로 하면: activation은 "비대칭 per-tensor", weight는 "대칭 per-channel", bias는 "누산기 단위의 int32", 그리고 출력 채널마다 정수 multiplier·shift 한 쌍이 붙는다. 이 네 가지가 int8 conv 커널의 계약 전부다.

### 1.2 params 구조체 — 커널 시그니처를 먼저 고정한다

TFLite Micro와 CMSIS-NN도 같은 생각으로 "op params 구조체 + per-channel quant 구조체 + dims 구조체"를 커널에 넘긴다 (필드 이름은 라이브러리마다 다르다). 이 노트의 모든 커널이 쓰는 헤더 `j3k.h`다.

```c
/* j3k.h — int8 NN 커널 (TFLite reference 규칙과 bit-exact를 목표) */
#pragma once
#include <stdint.h>
#include <limits.h>

/* ---------- 파라미터 구조체 ---------- */
typedef struct { int n, h, w, c; } dims_t;            /* NHWC */
typedef struct {
    int stride_h, stride_w, pad_h, pad_w;             /* pad = 앞쪽(위/왼쪽) 패딩 */
    int32_t input_offset;                             /* = -zp_in */
    int32_t output_offset;                            /* = +zp_out */
    int32_t act_min, act_max;                         /* fused ReLU 등 → clamp 범위 */
} conv_params_t;
typedef struct { const int32_t *multiplier; const int32_t *shift; } per_ch_quant_t;
typedef struct { int stride_h, stride_w, pad_h, pad_w, fh, fw; int32_t act_min, act_max; } pool_params_t;
typedef struct {
    int32_t in1_offset, in2_offset, out_offset;       /* -zp1, -zp2, +zp_out */
    int left_shift;                                   /* int8: 20 */
    int32_t in1_mult, in2_mult, out_mult;             /* Q31 */
    int in1_shift, in2_shift, out_shift;              /* <= 0 */
    int32_t act_min, act_max;
} add_params_t;

/* ---------- requantization primitives (gemmlowp 규칙, C1 7.4절) ---------- */
static inline int32_t srdhm(int32_t a, int32_t b) {           /* round(a*b / 2^31), 포화 */
    if (a == INT32_MIN && b == INT32_MIN) return INT32_MAX;
    int64_t ab = (int64_t)a * b;
    int64_t nudge = ab >= 0 ? (1 << 30) : (1 - (1 << 30));
    return (int32_t)((ab + nudge) / (1ll << 31));             /* C 나눗셈 = 0 쪽 절삭 */
}
static inline int32_t rdbpot(int32_t x, int e) {              /* x / 2^e, half away from zero */
    int32_t mask = (int32_t)((1ll << e) - 1);
    int32_t rem = x & mask, thr = (mask >> 1) + (x < 0);
    return (x >> e) + (rem > thr);
}
static inline int32_t mbqm(int32_t x, int32_t m, int shift) {  /* MultiplyByQuantizedMultiplier */
    int left = shift > 0 ? shift : 0, right = shift > 0 ? 0 : -shift;
    return rdbpot(srdhm(x * (1 << left), m), right);
}
static inline int8_t clamp_s8(int32_t v, int32_t lo, int32_t hi) {
    return (int8_t)(v < lo ? lo : v > hi ? hi : v);
}
```

설계 포인트 세 가지.

- **`input_offset`은 −zp_in으로 저장한다.** 커널 안에서는 "빼기"가 아니라 "더하기"만 한다. TFLite reference 코드도 이렇게 부른다. 부호를 잘못 넣는 실수가 가장 흔하다 — 이름에 부호 규약을 박아 두는 이유다.
- **pad는 "앞쪽" 값 하나만** 받는다. 뒤쪽 패딩은 경계 검사(`iy >= H`)가 자연스럽게 처리한다. SAME 패딩에서 앞뒤가 비대칭일 수 있다는 점이 1.4절의 함정이다.
- **multiplier·shift는 포인터**다. 채널 수만큼의 배열이 flash에 있고, 커널은 가리키기만 한다 (TFLM에서는 Prepare 단계에서 arena에 계산해 둔다 — F2).

### 1.3 손계산 — fused ReLU·ReLU6의 clamp 범위

ReLU는 "실수 0 아래를 자른다"이고, 실수 0은 정수로 `zp_out`이다. ReLU6은 위쪽을 "실수 6"에서 자른다.

```
act_min = max(−128, zp_out)                         (ReLU, ReLU6)
act_max = min( 127, zp_out + round(6 / s_out))      (ReLU6)
```

예: 2절 모델의 conv 출력은 `s_out = 0.02198872`, `zp_out = −128`이다.

```
ReLU  : act_min = max(−128, −128) = −128   ← clamp가 아무것도 안 한다!
ReLU6 : 6 / 0.02198872 = 272.87 → 273,  −128 + 273 = 145 → min(127, 145) = 127
```

말로 하면: 변환기는 ReLU 뒤 텐서의 범위를 [0, max]로 잡기 때문에 zp_out이 −128이 되고, 그러면 ReLU clamp는 int8 범위 자체와 같아져 **실제로는 아무 일도 하지 않는다**. ReLU6도 출력 범위를 [0, 6]으로 잡으면 마찬가지다. 이 사실은 9절에서 "변환기가 만든 파라미터만으로 테스트하면 clamp 버그를 못 잡는다"는 결과로 돌아온다.

### 1.4 SAME 패딩 — 앞쪽 패딩은 floor(total/2)

TFLite의 SAME 패딩은 출력 크기를 `out = ceil(in / stride)`로 정하고, 모자란 만큼을 앞뒤로 나눈다.

```
total = max((out − 1)·stride + k − in, 0)
pad_front = floor(total / 2)          pad_back = total − pad_front   (뒤쪽이 1 더 많을 수 있다)
```

손계산: in = 10, k = 3, stride = 2 → out = 5, total = 4·2 + 3 − 10 = 1 → **pad_front = 0**, pad_back = 1.

"3×3이니까 패딩 1"이라고 외워 두면 stride 1에서는 맞고(total = 2 → 1, 1) stride 2에서는 틀린다. 실제로 2절의 max pool(3×3, stride 2, SAME)을 처음에 `pad = 1`로 넣었더니 200개 출력 중 **124개가 틀렸다** (최대 차이 115). 고치자마자 0개. "숫자가 대부분 틀리는데 일부는 맞는다"면 오프셋(인덱싱) 버그부터 의심한다 — 펌웨어에서 DMA 주소가 한 칸 밀렸을 때와 같은 증상이다.

---

## 2. int8 conv2d — reference 루프 nest

### 2.1 직관 — NHWC에서 한 출력 값이 읽는 것

출력 `y[oy][ox][oc]` 하나는 입력의 k×k×C 창(window) 하나와 필터 하나(oc번)의 내적이다. NHWC에서는 **한 픽셀의 C개 채널이 메모리에 연속**이므로, 창 안의 각 탭(ky, kx)마다 길이 C짜리 연속 구간을 읽게 된다. OHWI 필터도 [oc][ky][kx][ic]라서 같은 탭의 C개 weight가 연속이다. 즉 가장 안쪽 루프를 입력 채널(ic)로 두면 **두 포인터가 나란히 1바이트씩 전진**한다 — Don이 아는 `memcmp`나 체크섬 루프와 같은 접근 패턴이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 290"> <text x="10" y="22" font-size="14">NHWC 메모리 — 입력 H=2, W=3, C=4 (픽셀마다 채널 4개가 연속)</text> <rect x="20" y="45" width="40" height="40" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="40" y="70" font-size="12" text-anchor="middle">0,0</text> <rect x="60" y="45" width="40" height="40" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="80" y="70" font-size="12" text-anchor="middle">0,1</text> <rect x="100" y="45" width="40" height="40" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="120" y="70" font-size="12" text-anchor="middle">0,2</text> <rect x="20" y="85" width="40" height="40" fill="#888" fill-opacity="0.25" stroke="currentColor"/><text x="40" y="110" font-size="12" text-anchor="middle">1,0</text> <rect x="60" y="85" width="40" height="40" fill="#888" fill-opacity="0.12" stroke="currentColor"/><text x="80" y="110" font-size="12" text-anchor="middle">1,1</text> <rect x="100" y="85" width="40" height="40" fill="#888" fill-opacity="0.05" stroke="currentColor"/><text x="120" y="110" font-size="12" text-anchor="middle">1,2</text> <text x="20" y="145" font-size="12">픽셀 (y, x)</text> <text x="180" y="60" font-size="12">주소 = ((y·W + x)·C + c)</text> <text x="180" y="80" font-size="12">픽셀 (0,1)의 c2 → ((0·3 + 1)·4 + 2) = 6</text>
<text x="180" y="100" font-size="12">픽셀 (1,0)의 c0 → ((1·3 + 0)·4 + 0) = 12</text> <g font-size="11" text-anchor="middle"> <rect x="20" y="170" width="26" height="30" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="33" y="190">c0</text> <rect x="46" y="170" width="26" height="30" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="59" y="190">c1</text> <rect x="72" y="170" width="26" height="30" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="85" y="190">c2</text> <rect x="98" y="170" width="26" height="30" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="111" y="190">c3</text> <rect x="124" y="170" width="26" height="30" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="137" y="190">c0</text> <rect x="150" y="170" width="26" height="30" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="163" y="190">c1</text> <rect x="176" y="170" width="26" height="30" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="189" y="190">c2</text> <rect x="202" y="170" width="26" height="30" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="215" y="190">c3</text> <rect x="228" y="170" width="26" height="30" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="241" y="190">c0</text>
<rect x="254" y="170" width="26" height="30" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="267" y="190">c1</text> <rect x="280" y="170" width="26" height="30" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="293" y="190">c2</text> <rect x="306" y="170" width="26" height="30" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="319" y="190">c3</text> <rect x="332" y="170" width="26" height="30" fill="#888" fill-opacity="0.25" stroke="currentColor"/><text x="345" y="190">c0</text> <rect x="358" y="170" width="26" height="30" fill="#888" fill-opacity="0.25" stroke="currentColor"/><text x="371" y="190">c1</text> <rect x="384" y="170" width="26" height="30" fill="#888" fill-opacity="0.25" stroke="currentColor"/><text x="397" y="190">c2</text> <rect x="410" y="170" width="26" height="30" fill="#888" fill-opacity="0.25" stroke="currentColor"/><text x="423" y="190">c3</text> </g> <text x="446" y="190" font-size="12">… (1,1), (1,2)</text> <text x="20" y="218" font-size="11">주소 0</text><text x="124" y="218" font-size="11">4</text><text x="228" y="218" font-size="11">8</text><text x="332" y="218" font-size="11">12</text> <text x="20" y="248" font-size="12">OHWI 필터 [oc][ky][kx][ic]도 같은 모양: 탭 (ky,kx) 하나의 ic 0..C−1이 연속</text> <text x="20" y="268" font-size="12">→ ic를 가장 안쪽 루프로 두면 입력·필터 포인터가 둘 다 +1씩 전진 (연속 접근)</text> </svg>
```

그림 2 — NHWC 주소 계산. 같은 색 4칸이 한 픽셀의 채널 4개다. 채널 방향이 메모리에서 연속이라, conv의 "탭 하나 × 채널 전부"가 짧은 연속 벡터 두 개의 내적이 된다.

### 2.2 정의 — 한 출력 값의 식

C1 7절의 식에 conv의 인덱스를 붙이면 끝이다 (weight zp = 0).

```
acc[oy][ox][oc] = bias[oc] + Σ_{ky,kx,ic: 입력 범위 안}  w[oc][ky][kx][ic] · ( x[iy][ix][ic] + input_offset )
      iy = oy·stride_h − pad_h + ky,   ix = ox·stride_w − pad_w + kx,   input_offset = −zp_in
y[oy][ox][oc] = clamp( MBQM(acc, M0[oc], shift[oc]) + zp_out,  act_min, act_max )
```

말로 하면: 창 안의 입력을 "실수 0 기준"으로 옮긴 값(x − zp_in)에 weight를 곱해 int32로 다 더하고, bias를 더하고, 채널마다 다른 정수 배율로 출력 눈금에 옮긴 뒤, 출력 zp를 더하고 자른다. 범위 밖(패딩) 탭은 **건너뛴다** — 패딩 값은 실수 0이고, 실수 0은 정수로 zp_in이라 `x + input_offset = 0`, 즉 기여가 0이기 때문이다.

### 2.3 손계산 — 2×2 입력, 2×2 필터, 출력 1개

```
x (2×2×1) = [10, −5, 3, 0],  zp_in = −2  → input_offset = +2  → x + off = [12, −3, 5, 2]
w (1×2×2×1) = [1, −2, 3, 4],  bias = 7
acc = 1·12 + (−2)·(−3) + 3·5 + 4·2 + 7 = 12 + 6 + 15 + 8 + 7 = 48

M = 0.05 = 0.8 · 2⁻⁴  →  M0 = round(0.8 · 2³¹) = 1717986918,  shift = −4
SRDHM(48, M0) = round(48 · 0.8) = round(38.4) = 38
RDBPOT(38, 4) = round(38 / 16) = round(2.375) = 2         (실수로는 48 · 0.05 = 2.4 → 2)
y = 2 + zp_out(3) = 5
```

### 2.4 코드 — reference conv

무엇을 확인하는 코드인지: 위 식을 그대로 옮긴 6중 루프. 최적화는 일부러 하지 않는다 — 이것이 이후 모든 최적화 커널의 "참값"이 된다.

```c
#include "j3k.h"
/* conv2d int8, NHWC 입력, OHWI 필터 [oc][fh][fw][ic], 출력 NHWC (batch 1) */
void conv_s8_ref(const conv_params_t *p, const per_ch_quant_t *q,
                 dims_t in, const int8_t *x, dims_t f, const int8_t *w,
                 const int32_t *bias, dims_t out, int8_t *y) {
    for (int oy = 0; oy < out.h; oy++)
    for (int ox = 0; ox < out.w; ox++)
    for (int oc = 0; oc < out.c; oc++) {
        int32_t acc = 0;
        for (int ky = 0; ky < f.h; ky++) {
            int iy = oy * p->stride_h - p->pad_h + ky;
            if (iy < 0 || iy >= in.h) continue;             /* 패딩 = (zp_in + offset)=0 기여 */
            for (int kx = 0; kx < f.w; kx++) {
                int ix = ox * p->stride_w - p->pad_w + kx;
                if (ix < 0 || ix >= in.w) continue;
                const int8_t *xp = &x[(iy * in.w + ix) * in.c];
                const int8_t *wp = &w[((oc * f.h + ky) * f.w + kx) * in.c];
                for (int ic = 0; ic < in.c; ic++)
                    acc += (int32_t)wp[ic] * ((int32_t)xp[ic] + p->input_offset);
            }
        }
        if (bias) acc += bias[oc];
        acc = mbqm(acc, q->multiplier[oc], q->shift[oc]) + p->output_offset;
        y[(oy * out.w + ox) * out.c + oc] = clamp_s8(acc, p->act_min, p->act_max);
    }
}
```

손계산을 이 함수로 확인한다 (`tiny.c`).

```c
/* 손계산 확인: 2x2x1 입력, 2x2 필터 1개, VALID → 출력 1개 */
#include <stdio.h>
#include "j3k.h"
void conv_s8_ref(const conv_params_t *, const per_ch_quant_t *, dims_t, const int8_t *, dims_t,
                 const int8_t *, const int32_t *, dims_t, int8_t *);
int main(void) {
    const int8_t x[4] = {10, -5, 3, 0}, w[4] = {1, -2, 3, 4};
    const int32_t bias[1] = {7}, mult[1] = {1717986918}, shift[1] = {-4};   /* M = 0.05 = 0.8·2^-4 */
    conv_params_t p = {1, 1, 0, 0, /*input_offset=-zp_in*/ 2, /*zp_out*/ 3, -128, 127};
    per_ch_quant_t q = {mult, shift};
    dims_t in = {1, 2, 2, 1}, f = {1, 2, 2, 1}, out = {1, 1, 1, 1};
    int8_t y; conv_s8_ref(&p, &q, in, x, f, w, bias, out, &y);
    int32_t acc = 0; for (int i = 0; i < 4; i++) acc += w[i] * (x[i] + 2); acc += 7;
    printf("acc=%d  srdhm=%d  rdbpot=%d  y=%d\n", acc, srdhm(acc, mult[0]), mbqm(acc, mult[0], -4), y);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 tiny.c j3k_ref.c -o tiny && ./tiny
```

```text
acc=48  srdhm=38  rdbpot=2  y=5
```

출력에서 볼 것: 손계산의 48 → 38 → 2 → 5가 그대로 나온다. 커널을 처음 짤 때는 이렇게 **손으로 검산 가능한 크기**의 케이스를 하나 먼저 통과시키고 나서 큰 무작위 테스트로 간다.

### 2.5 input offset 트릭과 "bias 접기" — 그리고 패딩의 함정

reference 루프는 MAC마다 `x + input_offset` 덧셈을 한다. 이 덧셈을 없애는 고전적 방법은 식을 전개해 상수항을 bias로 옮기는 것이다.

```
Σ w·(x + off) = Σ w·x + off·Σ w
bias_eff[oc] = bias[oc] + off · Σ_{ky,kx,ic} w[oc][ky][kx][ic]       ← 오프라인(또는 Prepare)에서 한 번
acc = bias_eff[oc] + Σ w·x                                           ← MAC 루프는 순수 int8 × int8
```

손계산(2.3절 숫자): `Σw = 1 − 2 + 3 + 4 = 6`, `bias_eff = 7 + 2·6 = 19`, `Σ w·x = 10 + 10 + 9 + 0 = 29`, `19 + 29 = 48`. 같다.

왜 이게 중요한가: `x + off`는 범위가 [−256, 254]라 **int8에 안 들어간다**. 그러면 int8×int8 SIMD 명령(NEON SDOT, Helium `VMLADAV.S8`)을 못 쓰고 16-bit로 넓혀야 한다. 접어 두면 피연산자가 둘 다 int8로 남는다 — 7.2절에서 이 차이가 컴파일된 명령(`smlal` vs `sdot`)으로 바로 보인다.

그런데 함정이 있다. `bias_eff`는 **9탭 전부**의 Σw로 만들었다. 경계 출력에서 패딩 탭을 "건너뛰면" 그 탭의 `off·w` 항은 bias에 들어 있는데 `w·x` 항은 없다. 패딩의 올바른 기여는 `w·(zp_in + off) = 0`이므로, 건너뛰는 대신 **패딩 칸의 값을 zp_in으로 채워서 계산**해야 접기가 정확해진다. im2col 버퍼를 zp_in으로 채우거나(`memset(d, zp_in, C)`), 입력을 미리 zp_in으로 패딩해 두는 이유가 이것이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300"> <text x="10" y="22" font-size="14">bias 접기 + 패딩: 경계 출력만 틀린다 (10×10 출력, 3×3 SAME)</text> <g stroke="currentColor" stroke-opacity="0.5"> <rect x="20" y="40" width="220" height="220" fill="#e08a3c" fill-opacity="0.35"/> <rect x="42" y="62" width="176" height="176" fill="#4a7bd0" fill-opacity="0.35"/> </g> <g stroke="currentColor" stroke-opacity="0.25"> <line x1="42" y1="40" x2="42" y2="260"/><line x1="64" y1="40" x2="64" y2="260"/><line x1="86" y1="40" x2="86" y2="260"/><line x1="108" y1="40" x2="108" y2="260"/><line x1="130" y1="40" x2="130" y2="260"/><line x1="152" y1="40" x2="152" y2="260"/><line x1="174" y1="40" x2="174" y2="260"/><line x1="196" y1="40" x2="196" y2="260"/><line x1="218" y1="40" x2="218" y2="260"/> <line x1="20" y1="62" x2="240" y2="62"/><line x1="20" y1="84" x2="240" y2="84"/><line x1="20" y1="106" x2="240" y2="106"/><line x1="20" y1="128" x2="240" y2="128"/><line x1="20" y1="150" x2="240" y2="150"/><line x1="20" y1="172" x2="240" y2="172"/><line x1="20" y1="194" x2="240" y2="194"/><line x1="20" y1="216" x2="240" y2="216"/><line x1="20" y1="238" x2="240" y2="238"/> </g> <text x="130" y="282" font-size="12" text-anchor="middle">주황 = 경계 36픽셀(×8채널 = 288), 파랑 = 내부 512</text> <text x="290" y="50" font-size="13">왼쪽 위 모서리 출력 (0,0)의 3×3 창</text> <g stroke="currentColor">
<rect x="290" y="62" width="40" height="40" fill="#888" fill-opacity="0.35"/><rect x="330" y="62" width="40" height="40" fill="#888" fill-opacity="0.35"/><rect x="370" y="62" width="40" height="40" fill="#888" fill-opacity="0.35"/> <rect x="290" y="102" width="40" height="40" fill="#888" fill-opacity="0.35"/><rect x="330" y="102" width="40" height="40" fill="#3f9a6b" fill-opacity="0.3"/><rect x="370" y="102" width="40" height="40" fill="#3f9a6b" fill-opacity="0.3"/> <rect x="290" y="142" width="40" height="40" fill="#888" fill-opacity="0.35"/><rect x="330" y="142" width="40" height="40" fill="#3f9a6b" fill-opacity="0.3"/><rect x="370" y="142" width="40" height="40" fill="#3f9a6b" fill-opacity="0.3"/> </g> <text x="420" y="88" font-size="12">회색 5탭 = 패딩 (실수 0 = zp_in)</text> <text x="420" y="128" font-size="12">초록 4탭 = 실제 입력</text> <text x="290" y="210" font-size="12">bias_eff에는 9탭 전부의 off·Σw가 들어 있다.</text> <text x="290" y="230" font-size="12">건너뛰면 off·w 몫이 남아 틀림 → 172/288</text> <text x="290" y="250" font-size="12">zp_in으로 계산: off·w + w·zp_in = 0 → 0/288</text> </svg>
```

그림 3 — 접은 bias와 패딩의 상호작용. 내부 출력은 창이 전부 실제 입력이라 어느 방식이든 맞고, 경계 출력만 패딩 처리 방식에 따라 갈린다.

무엇을 확인하는 코드인지: 2절 모델(아래 3절)의 실제 conv 파라미터로 "bias 접기 + 패딩 탭 건너뛰기"와 "bias 접기 + 패딩 탭을 zp_in으로 계산"을 TFLite 출력과 비교한다 (`fold.c`, 3절에서 만드는 `j3_model.h` 사용).

```c
/* bias 접기(input offset → bias)와 패딩의 상호작용: 경계에서만 틀린다 */
#include <stdio.h>
#include "j3_model.h"
int main(void) {
    int32_t beff[8];                                   /* bias_eff = bias + input_offset·Σw (모든 9탭) */
    for (int oc = 0; oc < 8; oc++) {
        int32_t sw = 0; for (int k = 0; k < 36; k++) sw += conv_w[oc * 36 + k];
        beff[oc] = conv_b[oc] + CONV_IN_OFF * sw;
    }
    int bad_border = 0, bad_inner = 0;
    for (int mode = 0; mode < 2; mode++) {             /* 0: 패딩 탭 건너뜀(틀림), 1: 패딩 탭을 zp_in 값으로 계산 */
        bad_border = bad_inner = 0;
        for (int oy = 0; oy < 10; oy++) for (int ox = 0; ox < 10; ox++) for (int oc = 0; oc < 8; oc++) {
            int32_t acc = beff[oc];
            for (int ky = 0; ky < 3; ky++) for (int kx = 0; kx < 3; kx++) {
                int iy = oy - 1 + ky, ix = ox - 1 + kx, out = iy < 0 || iy > 9 || ix < 0 || ix > 9;
                if (out && mode == 0) continue;
                for (int ic = 0; ic < 4; ic++) {
                    int32_t xv = out ? -CONV_IN_OFF : x_in[(iy * 10 + ix) * 4 + ic];   /* raw int8 (offset 없음) */
                    acc += conv_w[((oc * 3 + ky) * 3 + kx) * 4 + ic] * xv;
                }
            }
            int8_t y = clamp_s8(mbqm(acc, conv_mult[oc], conv_shift[oc]) + CONV_OUT_OFF, CONV_OUT_OFF, 127);
            int border = oy == 0 || oy == 9 || ox == 0 || ox == 9;
            if (y != tfl_conv_out[(oy * 10 + ox) * 8 + oc]) { if (border) bad_border++; else bad_inner++; }
        }
        printf("%s: mismatches border=%d/288 interior=%d/512\n",
               mode ? "pad taps = zp_in (im2col fill)" : "pad taps skipped          ", bad_border, bad_inner);
    }
    return 0;
}
```

```text
pad taps skipped          : mismatches border=172/288 interior=0/512
pad taps = zp_in (im2col fill): mismatches border=0/288 interior=0/512
```

출력에서 볼 것: 내부 512개는 두 방식 모두 0개 틀림, **경계 288개 중 172개만** 틀린다. 이런 "가장자리만 틀리는" 패턴은 거의 항상 패딩 처리 버그다. 테스트 입력이 작으면(예: 4×4) 경계 비율이 커서 잘 잡히고, 큰 입력 하나로만 테스트하면 틀린 비율이 작아 "가끔 1~2 LSB 틀린다"로 보여 오해하기 쉽다.

| 방식 | MAC 루프 | 패딩 처리 | 추가 메모리 | SIMD 친화 |
|---|---|---|---|---|
| reference (offset을 MAC마다 더함) | `w·(x+off)` 16-bit 피연산자 | 탭 건너뛰기 OK | 0 | 낮음 (int8×int8 명령 불가) |
| bias 접기 + 탭 건너뛰기 | `w·x` | **틀림** (경계) | 0 | — |
| bias 접기 + zp_in 패딩 | `w·x` int8×int8 | 패딩 칸 = zp_in | 패딩 버퍼 또는 im2col | 높음 (SDOT, VMLADAV.S8) |
| int16로 넓히며 offset 더함 (CMSIS-NN M4 경로로 알려진 방식) | `w·(x+off)` int16 | 패딩 칸 = 0 (offset 후) | int16 im2col 버퍼 | SMLAD (8.1절) |

---

## 3. TFLite reference와 bit-exact 맞추기

### 3.1 시험용 모델 — op 다섯 개를 한 그래프에

무엇을 확인하는 코드인지: conv(ReLU) → depthwise → add(서로 다른 scale) → avg pool → softmax, 그리고 max pool 가지를 가진 작은 Keras 모델을 int8로 변환하고, 각 op의 입출력 텐서와 양자화 파라미터를 본다 (`gen_model.py`, `.venv-tf`). 입력 10×10×4는 "Hark 같은 웨어러블의 작은 스펙트로그램 패치"라고 상상하면 된다.

```python
import os, contextlib, io; os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
import numpy as np, tensorflow as tf
tf.random.set_seed(0); rng = np.random.default_rng(0)
L = tf.keras.layers
x = L.Input((10, 10, 4), batch_size=1, name="x")
a = L.Conv2D(8, 3, padding="same", activation="relu", name="conv")(x)
b = L.DepthwiseConv2D(3, padding="same", name="dw")(a)
c = L.Add(name="add")([a, b])
d = L.AveragePooling2D(2, name="avg")(c)
e = L.MaxPooling2D(3, strides=2, padding="same", name="maxp")(c)
f = L.Softmax(name="sm")(d)
m = tf.keras.Model(x, [f, e])
w, bb = m.get_layer("conv").get_weights()
w *= np.linspace(0.3, 2.0, 8, dtype=np.float32)        # 채널마다 크기가 다르게 → per-channel scale 차이
m.get_layer("conv").set_weights([w, rng.normal(0, 0.1, 8).astype(np.float32)])
def rep():
    for _ in range(100):
        yield [rng.uniform(-0.5, 2.0, (1, 10, 10, 4)).astype(np.float32)]
cv = tf.lite.TFLiteConverter.from_keras_model(m)
cv.optimizations = [tf.lite.Optimize.DEFAULT]; cv.representative_dataset = rep
cv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
cv.inference_input_type = tf.int8; cv.inference_output_type = tf.int8
with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
    open("j3.tflite", "wb").write(cv.convert())
it = tf.lite.Interpreter("j3.tflite", experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_REF,
                         experimental_preserve_all_tensors=True)
it.allocate_tensors()
for op in it._get_ops_details():
    print(op["index"], op["op_name"], op["inputs"], op["outputs"])
for t in it.get_tensor_details():
    q = t["quantization_parameters"]
    print(t["index"], t["name"][:40], t["shape"], t["dtype"].__name__, q["scales"][:3], q["zero_points"][:3])
```

```text
0 CONV_2D [0 4 3] [5]
1 DEPTHWISE_CONV_2D [5 2 1] [6]
2 ADD [5 6] [7]
3 AVERAGE_POOL_2D [7] [8]
4 SOFTMAX [8] [9]
5 MAX_POOL_2D [7] [10]
0 serving_default_x:0 [ 1 10 10  4] int8 [0.00980367] [-77]
1 tfl.pseudo_qconst [8] int32 [4.2720210e-05 3.0707488e-05 4.4413373e-05] [0]
2 tfl.pseudo_qconst1 [1 3 3 8] int8 [0.00194282 0.00139651 0.00201983] [0]
3 tfl.pseudo_qconst2 [8] int32 [5.3812105e-06 9.7991897e-06 1.4170334e-05] [0]
4 tfl.pseudo_qconst3 [8 3 3 4] int8 [0.0005489  0.00099954 0.00144541] [0]
5 functional_1/conv_1/Relu;functional_1/co [ 1 10 10  8] int8 [0.02198872] [-128]
6 functional_1/dw_1/depthwise [ 1 10 10  8] int8 [0.02513492] [-68]
7 functional_1/add_1/Add [ 1 10 10  8] int8 [0.02469467] [-72]
8 functional_1/avg_1/AvgPool [1 5 5 8] int8 [0.02469467] [-72]
9 StatefulPartitionedCall_1:0 [1 5 5 8] int8 [0.00390625] [-128]
10 StatefulPartitionedCall_1:1 [1 5 5 8] int8 [0.02469467] [-72]
```

출력에서 볼 것 (1.1절 표가 실제 파일에 그대로 있다):

- 텐서 4 conv weight `[8 3 3 4]` = **OHWI**, scale이 채널마다 다르고(0.00055, 0.00100, 0.00145 …) zp = 0. 텐서 2 depthwise weight `[1 3 3 8]`.
- 텐서 3 conv bias의 scale `5.38e-06` = `0.00980367 × 0.0005489`(s_in × s_w[0]). bias는 누산기 단위다.
- 텐서 5 conv 출력 zp = −128 → 1.3절대로 ReLU clamp가 사실상 no-op.
- ADD의 두 입력 scale이 0.02199와 0.02513으로 다르고 출력은 0.02469 → 5절의 재료.
- avg pool 출력(8)과 max pool 출력(10)은 입력(7)과 scale·zp가 **같다**. softmax 출력(9)은 0.00390625 = 1/256, zp = −128.
- conv의 fused ReLU는 별도 op가 아니다 (op 목록에 RELU가 없다).

### 3.2 텐서와 파라미터를 C 헤더로 내보내기

무엇을 확인하는 코드인지: 무작위 int8 입력(−128..127 전 범위)으로 reference 인터프리터를 한 번 돌리고, weight·bias·중간 출력과 **TFLite가 Prepare에서 계산하는 것과 같은 규칙으로** 계산한 multiplier·shift를 `j3_model.h`로 쓴다 (`export_h.py`, 핵심 부분).

```python
def quantize_multiplier(M):                       # TFLite QuantizeMultiplier과 같은 규칙 (double)
    if M == 0: return 0, 0
    f, sh = math.frexp(M)
    m0 = int(math.floor(f * (1 << 31) + 0.5))      # std::round (half away from zero, f>0)
    if m0 == (1 << 31): m0 //= 2; sh += 1
    if sh < -31: m0, sh = 0, 0
    return m0, sh
# … (생략) BUILTIN_REF 인터프리터에 무작위 int8 입력을 넣고 invoke, arr()로 텐서를 C 배열 문자열로
def conv_quant(prefix, x_i, w_i, y_i):
    (sx,), (zx,) = qp(x_i); sw, _ = qp(w_i); (sy,), (zy,) = qp(y_i)
    mq = [quantize_multiplier(float(np.float32(sx)) * float(s) / float(np.float32(sy))) for s in sw]
    arr(prefix + "_mult", [m for m, _ in mq], "int32_t"); arr(prefix + "_shift", [s for _, s in mq], "int32_t")
    out.append(f"#define {prefix.upper()}_IN_OFF {-zx}\n#define {prefix.upper()}_OUT_OFF {zy}")
arr("x_in", xq, "int8_t")
arr("conv_w", g(4), "int8_t"); arr("conv_b", g(3), "int32_t"); conv_quant("conv", 0, 4, 5)
arr("dw_w", g(2), "int8_t");   arr("dw_b", g(1), "int32_t");   conv_quant("dw", 5, 2, 6)
for i, n in [(5, "conv_out"), (6, "dw_out"), (7, "add_out"), (8, "avg_out"), (9, "sm_out"), (10, "max_out")]:
    arr("tfl_" + n, g(i), "int8_t")
# … ADD 파라미터(5절), softmax LUT(6절)도 같은 식으로 추가하고 파일로 쓴다
```

```text
zp: in=-77 conv_out=-128 dw_out=-68 add_out=-72
conv (M0, shift)[0..2]: [(1076316302, -11), (1959973192, -11), (1417131178, -10)]
add: in1=(1878677393,-1) in2=(1073741824,0) out=(1092884172,-18)
j3_model.h: 17824 bytes
```

출력에서 볼 것: 채널 0의 M = 0.00980367 × 0.0005489 / 0.02198872 ≈ 2.447e-4 = 0.5012 × 2⁻¹¹ → M0 = 1076316302 (≈ 0.5012 × 2³¹), shift = −11. 채널마다 shift가 다를 수 있다(채널 2는 −10). 헤더는 18 KB 남짓 — 이것이 그대로 펌웨어 테스트 벡터가 된다 (C8 5절의 golden vector).

multiplier 계산에서 bit-exact를 좌우하는 세부 세 가지 (TFLite 소스의 Prepare 단계를 따라 한 것):

1. scale은 flatbuffer에 **float32**로 저장되어 있다. 곱하고 나누는 건 **double**로 한다: `(double)s_in · (double)s_w / (double)s_out`. float32로 계산하면 M0의 끝자리가 달라질 수 있다.
2. 가수를 Q31로 만들 때 반올림은 `std::round`(0에서 먼 쪽). Python의 `round()`는 짝수 쪽 반올림(banker's)이라 그대로 쓰면 안 된다 — 그래서 `floor(f·2³¹ + 0.5)`.
3. 반올림이 2³¹이 되면 M0를 반으로 하고 shift를 1 올린다.

### 3.3 C 커널로 다시 계산하고 비교

무엇을 확인하는 코드인지: 헤더의 입력·파라미터로 C 커널 다섯 개를 돌리고 TFLite 중간 텐서와 원소 단위로 비교한다. 각 op의 입력으로 **TFLite가 만든 앞 단 출력**을 쓴다 — 오차가 누적되지 않게 층마다 독립 검증하는 것이다 (C8 4절의 per-layer diff와 같은 생각).

```c
#include <stdio.h>
#include "j3_model.h"
void conv_s8_ref(const conv_params_t*, const per_ch_quant_t*, dims_t, const int8_t*, dims_t,
                 const int8_t*, const int32_t*, dims_t, int8_t*);
void dwconv_s8_ref(const conv_params_t*, const per_ch_quant_t*, dims_t, const int8_t*, dims_t,
                   const int8_t*, const int32_t*, dims_t, int8_t*);
void add_s8(const add_params_t*, int, const int8_t*, const int8_t*, int8_t*);
void avgpool_s8(const pool_params_t*, dims_t, const int8_t*, dims_t, int8_t*);
void maxpool_s8(const pool_params_t*, dims_t, const int8_t*, dims_t, int8_t*);
static int diff(const char *name, const int8_t *a, const int8_t *b, int n) {
    int bad = 0, maxd = 0;
    for (int i = 0; i < n; i++) { int d = a[i] - b[i]; if (d) bad++; if (d < 0) d = -d; if (d > maxd) maxd = d; }
    printf("%-8s n=%4d  mismatches=%d  max|diff|=%d\n", name, n, bad, maxd);
    return bad;
}
int main(void) {
    static int8_t a[800], b[800], c[800], d[200], e[200];
    dims_t in = {1, 10, 10, 4}, f = {8, 3, 3, 4}, o = {1, 10, 10, 8}, fd = {1, 3, 3, 8};
    conv_params_t pc = {1, 1, 1, 1, CONV_IN_OFF, CONV_OUT_OFF, CONV_OUT_OFF, 127}; /* ReLU: min=zp_out */
    per_ch_quant_t qc = {conv_mult, conv_shift};
    conv_s8_ref(&pc, &qc, in, x_in, f, conv_w, conv_b, o, a);
    int bad = diff("conv", a, tfl_conv_out, 800);
    conv_params_t pd = {1, 1, 1, 1, DW_IN_OFF, DW_OUT_OFF, -128, 127};
    per_ch_quant_t qd = {dw_mult, dw_shift};
    dwconv_s8_ref(&pd, &qd, o, tfl_conv_out, fd, dw_w, dw_b, o, b);  /* 입력은 TFLite 것을 써서 층별 독립 검증 */
    bad += diff("dwconv", b, tfl_dw_out, 800);
    add_s8(&ADD_P, 800, tfl_conv_out, tfl_dw_out, c);
    bad += diff("add", c, tfl_add_out, 800);
    pool_params_t pa = {2, 2, 0, 0, 2, 2, -128, 127}, pm = {2, 2, 0, 0, 3, 3, -128, 127};
    dims_t o5 = {1, 5, 5, 8};
    avgpool_s8(&pa, o, tfl_add_out, o5, d);  bad += diff("avgpool", d, tfl_avg_out, 200);
    maxpool_s8(&pm, o, tfl_add_out, o5, e);  bad += diff("maxpool", e, tfl_max_out, 200);
    printf("total mismatches = %d\n", bad);
    return bad != 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 main_exact.c j3k_ref.c j3k_more.c -o main_exact && ./main_exact
```

```text
conv     n= 800  mismatches=0  max|diff|=0
dwconv   n= 800  mismatches=0  max|diff|=0
add      n= 800  mismatches=0  max|diff|=0
avgpool  n= 200  mismatches=0  max|diff|=0
maxpool  n= 200  mismatches=0  max|diff|=0
total mismatches = 0
```

출력에서 볼 것: 다섯 op 모두 **mismatches = 0**. 2,800개 int8 출력이 한 비트도 다르지 않다. 처음 실행에서는 max pool만 124/200개가 틀렸는데(최대 차이 115), 원인은 1.4절의 SAME 앞쪽 패딩을 1로 넣은 것이었다. `pm`의 패딩을 0으로 고치자 0개가 됐다.

### 3.4 bit-exact 체크리스트 — "1 LSB 차이"가 날 때 볼 곳

TFLite reference(그리고 TFLM, CMSIS-NN의 s8 커널들)와 맞추려면 아래가 전부 같아야 한다. 하나라도 다르면 대부분은 맞고 **일부만 ±1** 틀린다.

| 항목 | TFLite 규칙 | 흔한 실수 |
|---|---|---|
| multiplier 계산 정밀도 | float32 scale → double 연산 | float32로 곱·나눗셈 |
| M0 반올림 | `std::round` (0에서 먼 쪽) | Python `round()` (짝수 쪽) |
| SRDHM nudge | 음수면 `1 − 2³⁰` | 부호 무관 `2³⁰` (9절 BUG 2) |
| shift 반올림 | RDBPOT = half away from zero | 그냥 `>>` (−∞ 쪽 버림, 9절 BUG 1) |
| 반올림 횟수 | **두 번** (SRDHM 후 RDBPOT) | 64-bit로 한 번에 반올림 (10.2절: 차이가 실제로 난다) |
| shift > 0 | 곱하기 전에 왼쪽 shift | 무시 |
| 패딩 | 앞쪽 = floor(total/2), 패딩 = 실수 0 | 대칭 패딩 가정, 패딩 값 0 (zp가 아니라) |
| avg pool | 유효 칸 수로 나누고 0에서 먼 쪽 반올림 | 항상 k² 로 나누기, C 나눗셈(0 쪽 절삭) |
| add | left_shift 20 + 입력별 multiplier + 출력 multiplier | 입력별로 바로 출력 scale로 requant (10.4절) |

"두 번 반올림"에 대한 메모: TFLite 소스에는 `TFLITE_SINGLE_ROUNDING`이라는 빌드 옵션이 있어서 켜면 한 번 반올림 규칙을 쓰는 것으로 알고 있다. 이 노트의 `.venv-tf` TFLite는 두 번 반올림 규칙과 100% 일치했다(위 결과 + 9절 1,000 케이스). 다른 빌드(TFLM 특정 포트, 벤더 런타임)와 맞출 때는 이 옵션부터 확인한다.

---

## 4. depthwise conv와 pooling

### 4.1 depthwise — 채널마다 따로, 그래서 다른 루프 모양

depthwise conv(channel multiplier 1)는 출력 채널 c가 입력 채널 c **하나만** 본다. 한 출력 값이 k×k = 9번의 MAC뿐이라 conv(9·C번)보다 재사용이 훨씬 적다 — D4에서 본 "depthwise는 memory-bound" 의 이유다. 커널 관점의 차이는 이것이다: conv는 "입력 채널 방향의 내적"이 길어서 그 방향을 안쪽 루프로 두지만, depthwise는 내적이 9탭으로 짧으므로 **채널 방향(c)을 가장 안쪽**에 두고 채널 여러 개를 동시에(SIMD lane으로) 계산한다. NHWC에서 c가 연속이니 lane 방향 로드가 자연스럽다.

reference 코드(`dwconv_s8_ref`)는 2.4절 conv에서 ic 루프를 없애고 weight 인덱스를 `w[(ky·fw + kx)·C + c]`, 입력을 같은 채널 c로 바꾼 것이다 — 같은 구조의 전체 코드가 10.5절 드릴에 있다. 3절에서 TFLite와 800개 일치, 9절에서 무작위 200 케이스 일치.

이 reference는 이미 c가 가장 안쪽이지만, 매 MAC마다 경계 검사와 offset 덧셈이 있어 벡터화가 안 된다. "미리 zp_in으로 패딩 + offset을 bias로 접기 + 누산기 배열 `acc[C]`"로 바꾸면 컴파일러가 채널 방향으로 벡터화한다. 같은 32×32×64 depthwise 3×3에서 잰 결과(`dwbench.c`, 7회 중앙값):

```text
depthwise 3x3 32x32x64: MACs=590K  mismatches=0
ref  (bounds check, offset add):  747.8 us    0.8 GMAC/s
fast (pre-pad, c-inner)       :   69.6 us    8.5 GMAC/s  (x10.7)
```

출력에서 볼 것: 10.7배 빨라졌지만 **8.5 GMAC/s**는 같은 크기 일반 conv의 최적 버전(7절, 약 120 GMAC/s)의 7%다. MAC 수가 conv의 1/64인데 시간은 1/4.5밖에 안 줄었다. MobileNet류에서 depthwise가 MAC 비중보다 훨씬 큰 시간 비중을 차지하는 이유가 이 숫자다 (D4, I2).

### 4.2 average pool — 반올림 한 줄이 전부

입력과 출력이 같은 scale·zp이므로 requant가 없다. 정수 평균만 구하면 된다. 실수 평균 `s·(Σq/n − zp)`에서 zp는 그대로 지나가므로 정수 평균 `Σq / n`만 반올림하면 된다.

구현(`avgpool_s8`)은 conv와 같은 창 순회에서 MAC 대신 `sum += x`, `cnt++`(유효 칸만)를 하고 끝에 `sum > 0 ? (sum + cnt/2)/cnt : (sum − cnt/2)/cnt`로 나눈 뒤 clamp한다 (같은 반올림 식이 10.3절 드릴에 있다).

두 가지 규칙을 기억한다.

- **나누는 수는 "유효 칸 수"(cnt)**다. SAME 패딩의 모서리 창은 패딩 칸을 평균에 넣지 않는다 (TFLite 규칙. 프레임워크에 따라 `count_include_pad` 옵션이 있다 — PyTorch의 `AvgPool2d`가 그렇다).
- 반올림은 **0에서 먼 쪽**. 손계산은 10.3절 드릴에서 표로 한다.

max pool은 반올림도 requant도 없다 — 최댓값을 고르고 clamp만 한다. 다만 시작값을 `-128`로 두고, 창이 전부 패딩인 경우가 없는지(유효 칸 ≥ 1) 보장해야 한다.

---

## 5. elementwise add — scale이 다른 두 텐서 더하기

### 5.1 왜 어렵나

ResNet의 skip connection, 오디오 모델의 residual — 두 int8 텐서를 더하는 일은 흔하다. 문제는 두 입력이 **서로 다른 scale**이라는 점이다. 3.1절의 ADD는 `s1 = 0.02198872`, `s2 = 0.02513492`, 출력 `so = 0.02469467`이다. 실수 식은 단순하다.

```
y_real = s1·(a − z1) + s2·(b − z2)
q_y    = zo + round( (s1/so)·(a − z1) + (s2/so)·(b − z2) )
```

말로 하면: 두 입력을 각자 실수로 바꿔 더하고 출력 눈금으로 옮기면 된다. 그런데 정수로 하려면 `s1/so`와 `s2/so` 두 배율을 따로 적용해야 하고, 각자 반올림하면 **반올림이 두 번 더해져** 오차가 커진다 (10.4절에서 65,536쌍 중 8,953개가 틀린다는 것을 확인한다).

### 5.2 TFLite의 방법 — 공통 scale로 키운 다음 한 번만 반올림

TFLite는 두 입력을 먼저 **2²⁰배 확대한 공통 정밀도**로 옮기고, 더한 다음, 출력 배율을 한 번 적용한다.

```
twice_max = 2 · max(s1, s2)
M1 = s1 / twice_max,   M2 = s2 / twice_max          (둘 다 ≤ 0.5)
Mo = twice_max / (2²⁰ · so)
a' = MBQM( (a − z1) · 2²⁰, M1 )                      ← 확대한 뒤 축소 → 소수부 정보가 20비트 남는다
b' = MBQM( (b − z2) · 2²⁰, M2 )
y  = clamp( MBQM(a' + b', Mo) + zo )                 ← 반올림은 사실상 여기 한 번
```

말로 하면: 입력을 int32 공간에서 2²⁰배로 "부풀려" 두면 M1·M2를 곱할 때 생기는 반올림 오차가 2⁻²⁰ 수준으로 작아진다. 실질적인 반올림은 마지막 한 번뿐이라, 결과가 실수 계산과 거의 같아진다. 2²⁰인 이유: int8 입력 차이 `(a − z)`는 최대 ±255 ≈ 2⁸이므로 2⁸ · 2²⁰ = 2²⁸로 int32 안에 여유 있게 들어간다 (두 개를 더해도 2²⁹).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 250"> <defs><marker id="a5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <text x="10" y="22" font-size="14">TFLite int8 ADD 데이터 경로 — a = 0, b = 0 일 때의 실제 값</text> <rect x="10" y="45" width="60" height="40" rx="5" fill="#888" fill-opacity="0.2" stroke="#888"/><text x="40" y="70" font-size="12" text-anchor="middle">a = 0</text> <rect x="95" y="45" width="95" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="142" y="62" font-size="12" text-anchor="middle">−z1 = +128</text><text x="142" y="78" font-size="11" text-anchor="middle">128</text> <rect x="215" y="45" width="95" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="262" y="62" font-size="12" text-anchor="middle">× 2²⁰</text><text x="262" y="78" font-size="11" text-anchor="middle">134217728</text> <rect x="335" y="45" width="105" height="40" rx="5" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="387" y="62" font-size="12" text-anchor="middle">× M1 (0.4374)</text><text x="387" y="78" font-size="11" text-anchor="middle">58708669</text>
<rect x="10" y="150" width="60" height="40" rx="5" fill="#888" fill-opacity="0.2" stroke="#888"/><text x="40" y="175" font-size="12" text-anchor="middle">b = 0</text> <rect x="95" y="150" width="95" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="142" y="167" font-size="12" text-anchor="middle">−z2 = +68</text><text x="142" y="183" font-size="11" text-anchor="middle">68</text> <rect x="215" y="150" width="95" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="262" y="167" font-size="12" text-anchor="middle">× 2²⁰</text><text x="262" y="183" font-size="11" text-anchor="middle">71303168</text> <rect x="335" y="150" width="105" height="40" rx="5" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="387" y="167" font-size="12" text-anchor="middle">× M2 (0.5)</text><text x="387" y="183" font-size="11" text-anchor="middle">35651584</text> <circle cx="470" cy="118" r="14" fill="none" stroke="currentColor"/><text x="470" y="123" font-size="14" text-anchor="middle">+</text> <rect x="500" y="98" width="80" height="40" rx="5" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="540" y="115" font-size="12" text-anchor="middle">× Mo</text><text x="540" y="131" font-size="11" text-anchor="middle">183</text>
<rect x="600" y="98" width="70" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/><text x="635" y="115" font-size="11" text-anchor="middle">+zo, clamp</text><text x="635" y="131" font-size="11" text-anchor="middle">111</text> <line x1="70" y1="65" x2="93" y2="65" stroke="currentColor" marker-end="url(#a5)"/><line x1="190" y1="65" x2="213" y2="65" stroke="currentColor" marker-end="url(#a5)"/><line x1="310" y1="65" x2="333" y2="65" stroke="currentColor" marker-end="url(#a5)"/> <line x1="70" y1="170" x2="93" y2="170" stroke="currentColor" marker-end="url(#a5)"/><line x1="190" y1="170" x2="213" y2="170" stroke="currentColor" marker-end="url(#a5)"/><line x1="310" y1="170" x2="333" y2="170" stroke="currentColor" marker-end="url(#a5)"/> <line x1="440" y1="70" x2="460" y2="106" stroke="currentColor" marker-end="url(#a5)"/><line x1="440" y1="165" x2="460" y2="130" stroke="currentColor" marker-end="url(#a5)"/> <line x1="484" y1="118" x2="498" y2="118" stroke="currentColor" marker-end="url(#a5)"/><line x1="580" y1="118" x2="598" y2="118" stroke="currentColor" marker-end="url(#a5)"/> <text x="470" y="160" font-size="11">합 94360253</text> <text x="10" y="222" font-size="12">파랑: 정수 오프셋·확대(정확), 주황: 고정소수점 곱(반올림 발생)</text> <text x="10" y="240" font-size="12">실수 정답: (2.8146 + 1.7092) / so = 183.19 → 183 − 72 = 111</text> </svg>
```

그림 4 — ADD의 정수 데이터 경로와 a = b = 0일 때 실제로 계산된 값. 입력별 곱은 2²⁰배 확대된 공간에서 일어나므로 거기서 생기는 반올림 오차는 무시할 만하고, 의미 있는 반올림은 출력 multiplier(Mo)에서 한 번뿐이다.

### 5.3 손계산과 코드

3.2절 출력의 ADD 파라미터로 손계산한다.

```
twice_max = 2 · 0.02513492 = 0.05026984
M1 = 0.02198872 / 0.05026984 = 0.43741  → (1878677393, −1)   [0.87483 · 2⁻¹]
M2 = 0.02513492 / 0.05026984 = 0.5      → (1073741824,  0)   [0.5 · 2⁰ — 큰 쪽은 항상 정확히 0.5]
Mo = 0.05026984 / (2²⁰ · 0.02469467) = 1.9414e-6 → (1092884172, −18)

a = 0:  (0 + 128) · 2²⁰ = 134217728  → × M1 → 58708669
b = 0:  (0 +  68) · 2²⁰ =  71303168  → × M2 → 35651584
합 94360253 → × Mo → 183 → + zo(−72) = 111
실수 검산: 0.02198872·128 + 0.02513492·68 = 2.8146 + 1.7092 = 4.5238,  /0.02469467 = 183.19 → 183 → 111
```

```c
/* elementwise add, 서로 다른 scale (TFLite int8 ADD 알고리즘) */
void add_s8(const add_params_t *p, int n, const int8_t *a, const int8_t *b, int8_t *y) {
    for (int i = 0; i < n; i++) {
        int32_t va = (a[i] + p->in1_offset) * (1 << p->left_shift);
        int32_t vb = (b[i] + p->in2_offset) * (1 << p->left_shift);
        int32_t sa = rdbpot(srdhm(va, p->in1_mult), -p->in1_shift);
        int32_t sb = rdbpot(srdhm(vb, p->in2_mult), -p->in2_shift);
        int32_t o  = rdbpot(srdhm(sa + sb, p->out_mult), -p->out_shift) + p->out_offset;
        y[i] = clamp_s8(o, p->act_min, p->act_max);
    }
}
```

`add_s8`의 각 단계를 그대로 풀어 쓴 작은 프로그램(`addhand.c`, 헤더의 `ADD_P` 사용)으로 a = b = 0의 중간값을 찍어 손계산과 맞춰 본다.

```text
va=134217728 vb=71303168  sa=58708669 sb=35651584  sum=94360253  raw=183  +zp=111  add_s8=111
```

출력에서 볼 것: 손계산의 모든 중간값이 그대로다. 3.3절에서 800개 전부 TFLite와 일치했고, 9절에서 무작위 add 200 케이스(76,272개 출력)도 전부 일치한다.

임베디드 연결: ADD는 MAC이 없는 op지만 원소당 SRDHM 3번(64-bit 곱 3번)이라 **원소당 비용은 conv의 MAC 하나보다 훨씬 비싸다**. M4에서 64-bit 곱은 `SMULL` 한 번이지만 반올림·포화 처리가 붙는다. CMSIS-NN 같은 라이브러리가 elementwise add를 별도로 최적화하는 이유이고, 변환기가 가능하면 add를 앞 conv의 bias나 출력에 접으려는 이유다.

---

## 6. int8 softmax — 256칸 LUT

### 6.1 직관 — 입력 차이가 256가지뿐이다

softmax는 `p_i = exp(x_i) / Σ exp(x_j)`이고, 모든 x에서 같은 값을 빼도 결과가 같다 (분자·분모에 같은 `exp(−c)`가 곱해진다). 그래서 **행의 최댓값을 빼고** 계산한다. int8 입력에서 `d = max − q_i`는 0..255 정수뿐이다.

```
p_i = exp(s·(q_i − max)) / Σ_j exp(s·(q_j − max)) = LUT[d_i] / Σ_j LUT[d_j],   LUT[d] = exp(−s·d)
```

말로 하면: 입력 scale s가 고정이면 `exp(−s·d)`는 256개 값 중 하나라서 **미리 표로 만들어 둘 수 있다**. 실행 시에는 표 조회, 덧셈, 나눗셈만 남는다. 출력은 TFLite 규약상 scale 1/256, zp −128이므로 `q_out = round(256·p) − 128`이다. LUT 값은 2¹⁶을 곱한 정수(Q16)로 둔다 — `exp(0) = 1 → 65536`.

B1에서 activation LUT를 256칸으로 만들었던 것과 같은 아이디어인데, softmax는 **정규화(나눗셈)**가 하나 더 붙는다.

### 6.2 손계산 — 3절 모델의 softmax 첫 행

입력(avg pool 출력) `[−67, −72, −67, −67, −72, −61, −72, −42]`, s = 0.0246946719.

```
max = −42,  d = [25, 30, 25, 25, 30, 19, 30, 0]
LUT[0] = 65536,  LUT[19] = round(e^(−0.4692)·65536) = 40993,
LUT[25] = 35348, LUT[30] = 31242
Σ = 3·35348 + 3·31242 + 40993 + 65536 = 306299
d=0 : 65536·256/306299 = 54.77 → 55 − 128 = −73
d=19: 34.26 → 34 → −94      d=25: 29.54 → 30 → −98      d=30: 26.11 → 26 → −102
```

### 6.3 코드와 비교

```c
/* softmax int8 → int8 (out scale 1/256, zp -128). lut[d] = round(exp(-s_in·d)·2^16), d = max - x ∈ [0,255] */
void softmax_s8_lut(int rows, int depth, const int8_t *x, const uint32_t lut[256], int8_t *y) {
    for (int r = 0; r < rows; r++, x += depth, y += depth) {
        int mx = -128;
        for (int i = 0; i < depth; i++) if (x[i] > mx) mx = x[i];
        uint32_t sum = 0;
        for (int i = 0; i < depth; i++) sum += lut[mx - x[i]];
        for (int i = 0; i < depth; i++) {                 /* q = round(e·256/sum) - 128 */
            int32_t q = (int32_t)(((uint64_t)lut[mx - x[i]] * 256 + sum / 2) / sum) - 128;
            y[i] = clamp_s8(q, -128, 127);
        }
    }
}
```

무엇을 확인하는 코드인지: 3절 모델의 softmax(25행 × 8)를 LUT 커널로 계산해 TFLite 출력, 그리고 double로 계산한 실수 softmax를 int8로 양자화한 값과 비교한다 (`main_sm.c`).

```c
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "j3_model.h"
void softmax_s8_lut(int, int, const int8_t*, const uint32_t*, int8_t*);
int main(void) {
    const double s_in = 0.0246946719;                       /* softmax 입력 scale (avgpool 출력) */
    int8_t y[200]; softmax_s8_lut(25, 8, tfl_avg_out, sm_lut, y);
    int bad_t = 0, md_t = 0, md_f = 0, bad_f = 0; double maxerr = 0;
    for (int r = 0; r < 25; r++) {
        double e[8], s = 0;
        for (int i = 0; i < 8; i++) { e[i] = exp(s_in * tfl_avg_out[r * 8 + i]); s += e[i]; }
        for (int i = 0; i < 8; i++) {
            int k = r * 8 + i; double p = e[i] / s;
            int qf = (int)lround(p * 256) - 128; if (qf > 127) qf = 127;   /* float 정답을 int8로 */
            int dt = abs(y[k] - tfl_sm_out[k]), df = abs(y[k] - qf);
            bad_t += dt != 0; bad_f += df != 0; if (dt > md_t) md_t = dt; if (df > md_f) md_f = df;
            double err = fabs((y[k] + 128) / 256.0 - p); if (err > maxerr) maxerr = err;
        }
    }
    printf("LUT vs TFLite : mismatches=%d/200  max|diff|=%d LSB\n", bad_t, md_t);
    printf("LUT vs float  : mismatches=%d/200  max|diff|=%d LSB  max prob err=%.5f\n", bad_f, md_f, maxerr);
    printf("row0 in : "); for (int i = 0; i < 8; i++) printf("%5d", tfl_avg_out[i]);
    printf("\nrow0 LUT: "); for (int i = 0; i < 8; i++) printf("%5d", y[i]);
    printf("\nrow0 TFL: "); for (int i = 0; i < 8; i++) printf("%5d", tfl_sm_out[i]);
    printf("\n"); return 0;
}
```

```text
LUT vs TFLite : mismatches=0/200  max|diff|=0 LSB
LUT vs float  : mismatches=0/200  max|diff|=0 LSB  max prob err=0.00194
row0 in : -67  -72  -67  -67  -72  -61  -72  -42
row0 LUT:  -98 -102  -98  -98 -102  -94 -102  -73
row0 TFL:  -98 -102  -98  -98 -102  -94 -102  -73
```

출력에서 볼 것: 손계산의 −73, −94, −98, −102가 그대로 나오고, 이 데이터에서는 TFLite와 200개 모두 일치했다. 확률 오차 최대 0.00194는 1/256 = 0.0039의 절반 = 양자화 자체의 한계다.

### 6.4 범위를 넓혀서 다시 — "bit-exact"를 약속하지 않는 op

이 데이터는 입력 범위가 좁아 운이 좋았을 수 있다. 입력 범위(±2, ±8, ±20)와 depth(8, 64)를 바꾼 softmax 단일 op 모델 6개를 만들어 무작위 int8 500행씩 비교했다 (`sm_stress.py`, 9절의 `convert` 함수 재사용, `.venv-tf`).

방법: 각 설정마다 `Input(depth) → Softmax` 모델을 9절의 `convert()`로 int8 변환하고, 입력 scale로 LUT를 만들어 C `softmax_s8_lut`(ctypes)와 TFLite 출력, 그리고 float64 softmax를 비교한다. 오차는 LSB(1/256) 단위.

```text
 range  depth  in_scale   LUT!=TFL  max|LUT-TFL|  max|LUT-float|  max|TFL-float|  (LSB = 1/256)
    2      8  0.01561       1/4000         1          0.50           0.50
    2     64  0.01567       2/32000         1          0.50           0.50
    8      8  0.06268       2/4000         1          0.76           0.76
    8     64  0.06271       2/32000         1          0.50           0.50
   20      8  0.15645       5/4000         1          1.00           1.00
   20     64  0.15657       2/32000         1          0.50           0.50
```

출력에서 볼 것:

- LUT 커널과 TFLite는 **수천 개 중 1~5개, 최대 1 LSB**만 다르다. bit-exact는 아니다.
- 그런데 실수 softmax 대비 오차는 둘이 **똑같다**(최대 0.5~1.0 LSB). 즉 둘 다 "정답에서 1 LSB 이내"이고, 서로 다른 지점에서 반올림했을 뿐이다. 1.00 LSB가 나온 경우는 확률이 1에 가까운 행이다 — int8 출력의 최댓값 127은 255/256 = 0.996이라 p = 1.0을 표현할 수 없다.
- TFLite의 int8 softmax는 버전·빌드에 따라 gemmlowp 고정소수점 exp 근사 또는 LUT 방식 등 다른 구현을 쓰는 것으로 알고 있고, 이 노트에서 그 알고리즘을 그대로 재현하지는 않았다. 그래서 softmax는 **"실수 대비 ≤ 1 LSB"를 합격 기준**으로 삼는다. C8 3절의 판단 기준 — "규격이 반올림까지 정의한 op는 bit-exact, 아니면 tolerance" — 의 실례다.

임베디드 연결: LUT는 256 × 4 B = 1 KB(Q16을 uint32로 둔 경우). 입력 scale이 모델마다 고정이라 **변환 시점에 만들어 flash에 둔다**. 행마다 나눗셈이 depth번 있는데, M4의 `UDIV`는 2~12 사이클이라 행당 역수 한 번(`256·2^k / sum`)을 구해 곱셈으로 바꾸는 최적화가 흔하다 (이 경우 반올림이 또 달라지므로 다시 tolerance 검증).

---

## 7. 성능 — 루프 순서, im2col, 레지스터 블로킹, SDOT, 타일링

### 7.1 실험 설계

대상: **conv 3×3, SAME, 32×32×64 → 32×32×64** (MAC = 32·32·64·64·9 = 37,748,736 ≈ 37.7 M). 키워드 스포팅 DS-CNN이나 작은 비전 모델 중간층에 있을 법한 크기다. 모든 변형은 같은 int8 입력·weight·bias·multiplier로 돌리고, 출력 int8 전체(65,536개)를 V0 reference 출력과 비교해 `mismatches`를 찍는다 — **빨라졌는데 틀리면 의미가 없다**. 시간은 한 번에 50 ms 이상 돌도록 반복 횟수를 정하고 7번 재서 중앙값을 쓴다 (호스트는 잡음이 많다 — min·max도 같이 본다).

변형 목록 (`bench.c`):

| 이름 | 무엇을 바꿨나 | 추가 scratch |
|---|---|---|
| V0 | reference (2.4절): ic 가장 안쪽, MAC마다 offset 덧셈과 경계 검사 | 0 |
| V1 | 필터를 HWIO로 재배치, **출력 채널(oc)을 가장 안쪽**, 누산기 배열 `acc[64]` | 256 B |
| V2 | 입력을 zp_in으로 미리 패딩 + offset을 bias로 접기, ic 가장 안쪽 | 패딩 입력 73,984 B |
| V3 | **im2col** 전체 + 단순 GEMM (행 × weight 행 내적) | 589,824 B |
| V4 | im2col + **4×4 레지스터 블로킹** (스칼라 C) | 589,824 B |
| V5 | im2col + 4×4 블록, 내부를 **SDOT** intrinsics로 | 589,824 B |
| V6 | **부분 im2col** (T 픽셀씩 펼치고 바로 GEMM) + SDOT 4×4 | T·576 B |
| V7 | im2col 없이 **직접 conv** + SDOT 4×4 (패딩 입력에서 바로 로드) | 73,984 B |

### 7.2 루프 순서 — "무엇을 가장 안쪽에 두나"

conv에는 6개 루프(oy, ox, oc, ky, kx, ic)가 있고 순서는 자유다. 결과는 같지만(정수 덧셈은 결합법칙이 성립 — float와 다르다, C8 3.2절) 속도는 다르다. 가장 안쪽 루프가 결정하는 것:

- **ic 가장 안쪽 (V0, V2)**: 입력·필터가 둘 다 연속 접근. 내적 하나가 길이 C(=64)짜리 9개 구간으로 쪼개진다.
- **oc 가장 안쪽 (V1)**: 입력 값 하나(`xv`)를 64개 출력 채널에 **브로드캐스트**하고 HWIO 필터의 연속 64바이트와 곱해 누산기 배열 64개에 더한다. 누산기 배열이 SIMD 레지스터로 매핑된다.

```c
/* V1: 필터 HWIO, 출력 채널이 가장 안쪽 → oc 방향으로 자동 벡터화 */
static void v1_oc_inner(void) {
    for (int oy = 0; oy < H; oy++) for (int ox = 0; ox < W; ox++) {
        int32_t acc[CO]; memcpy(acc, bias, sizeof acc);
        for (int ky = 0; ky < KH; ky++) { int iy = oy - 1 + ky; if (iy < 0 || iy >= H) continue;
        for (int kx = 0; kx < KW; kx++) { int ix = ox - 1 + kx; if (ix < 0 || ix >= W) continue;
            const int8_t *xp = &x[(iy * W + ix) * CI];
            for (int ic = 0; ic < CI; ic++) {
                int32_t xv = xp[ic] - ZP_IN; const int8_t *wr = &w_hwio[((ky * KW + kx) * CI + ic) * CO];
                for (int oc = 0; oc < CO; oc++) acc[oc] += wr[oc] * xv;
            } } }
        for (int oc = 0; oc < CO; oc++) y[(oy * W + ox) * CO + oc] = rq(acc[oc], oc);
    }
}
```

컴파일러가 실제로 무엇을 만들었는지 함수별 SIMD 곱셈 명령을 세어 봤다 (`cc -O2 -mcpu=apple-m2 -S bench.c`, 함수별 집계).

```text
v1_oc_inner {'smlal2.4s': 24, 'smlal.4s': 24, 'smull2.2d': 4, 'smull.2d': 4, 'smlal.2d': 4, 'smlal2.2d': 4}
v2_padded_folded {'smull': 1, 'smaddl': 1, 'sdot.4s': 18}
gemm_naive {'smull': 1, 'smaddl': 1, 'sdot.4s': 2}
gemm_4x4_c {'sdot.2s': 16, 'smull': 4, 'smaddl': 4}
gemm_4x4_sdot {'sdot.4s': 16, 'smull': 4, 'smaddl': 4}
v7_direct_sdot {'sdot.4s': 64, 'smull': 4, 'smaddl': 4}
```

출력에서 볼 것:

- V1은 `smlal`(16-bit × 16-bit → 32-bit 누산) 계열만 나온다. `xv = x − zp_in`이 int8 범위를 벗어나므로 컴파일러가 int8×int8 `sdot`을 쓸 수 없다 — **2.5절에서 말한 "offset이 SDOT을 막는다"가 명령으로 보인다**.
- offset을 bias로 접은 V2, 그리고 im2col 버전들은 순수 C(`gemm_naive`, `gemm_4x4_c`)인데도 **clang이 자동으로 `sdot`을 만들었다**. `-mcpu=apple-m2`로 dotprod 확장을 알려 줬기 때문이다. E2 6절에서 본 "자동 벡터화가 되는 모양"을 만들어 주면 intrinsics 없이도 상당 부분 얻는다.
- `smull`/`smaddl`은 requantization(SRDHM의 64-bit 곱)이다.

### 7.3 im2col — conv를 GEMM으로 바꾸는 대가

im2col은 출력 픽셀 하나가 보는 k×k×C 창을 **한 행으로 펼쳐** 행렬 A[P][K] (P = 출력 픽셀 수, K = k·k·C)를 만든다. 그러면 conv는 `Y[P][O] = A[P][K] · Wᵀ[K][O]` — 잘 최적화된 GEMM 하나가 된다. OHWI 필터는 이미 W[O][K] 모양이다(한 출력 채널의 필터가 연속 K바이트). NHWC에서는 창의 각 탭이 C바이트 연속이라 im2col이 `memcpy` 9번이다 — NCHW였다면 원소 단위로 흩어진 복사가 된다. 이것이 CPU·MCU 런타임이 NHWC를 선호하는 큰 이유 중 하나다.

```c
/* im2col: 출력 픽셀 p0..p0+n-1 의 3x3xCI 패치를 행으로 펼친다. 패딩 칸은 zp_in */
static void im2col(int p0, int n, int8_t *dst) {
    for (int p = p0; p < p0 + n; p++, dst += K) {
        int oy = p / W, ox = p % W;
        for (int ky = 0; ky < KH; ky++) for (int kx = 0; kx < KW; kx++) {
            int iy = oy - 1 + ky, ix = ox - 1 + kx; int8_t *d = dst + (ky * KW + kx) * CI;
            if (iy < 0 || iy >= H || ix < 0 || ix >= W) memset(d, ZP_IN, CI);
            else memcpy(d, &x[(iy * W + ix) * CI], CI);
        }
    }
}
```

`memset(d, ZP_IN, CI)` — 2.5절의 교훈대로 패딩 칸을 zp_in으로 채우니 bias 접기가 경계에서도 정확하다 (모든 변형 mismatches 0).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 280"> <defs><marker id="a7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <text x="10" y="22" font-size="14">im2col — 3×3×C 창을 한 행(K = 9·C)으로 펼친다</text> <g stroke="currentColor" stroke-opacity="0.5" fill="none"> <rect x="20" y="45" width="140" height="140"/><line x1="55" y1="45" x2="55" y2="185"/><line x1="90" y1="45" x2="90" y2="185"/><line x1="125" y1="45" x2="125" y2="185"/> <line x1="20" y1="80" x2="160" y2="80"/><line x1="20" y1="115" x2="160" y2="115"/><line x1="20" y1="150" x2="160" y2="150"/> </g> <rect x="20" y="45" width="105" height="105" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0" stroke-width="2"/> <rect x="55" y="45" width="105" height="105" fill="none" stroke="#e08a3c" stroke-width="2" stroke-dasharray="5,3"/> <text x="20" y="205" font-size="12">입력 4×4 (픽셀마다 C채널)</text> <text x="20" y="222" font-size="12">파랑 창 = 출력 p0, 주황 창 = p1</text> <line x1="175" y1="100" x2="215" y2="100" stroke="currentColor" marker-end="url(#a7)"/> <g font-size="10" text-anchor="middle"> <rect x="225" y="70" width="45" height="24" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="247" y="86">t0</text>
<rect x="270" y="70" width="45" height="24" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="292" y="86">t1</text> <rect x="315" y="70" width="45" height="24" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="337" y="86">t2</text> <rect x="360" y="70" width="45" height="24" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="382" y="86">t3</text> <rect x="405" y="70" width="45" height="24" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="427" y="86">t4</text> <rect x="450" y="70" width="45" height="24" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="472" y="86">t5</text> <rect x="495" y="70" width="45" height="24" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="517" y="86">t6</text> <rect x="540" y="70" width="45" height="24" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="562" y="86">t7</text> <rect x="585" y="70" width="45" height="24" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="607" y="86">t8</text> <rect x="225" y="100" width="45" height="24" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="247" y="116">t0</text> <rect x="270" y="100" width="45" height="24" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="292" y="116">t1</text>
<rect x="315" y="100" width="45" height="24" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="337" y="116">t2</text> <rect x="360" y="100" width="45" height="24" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="382" y="116">t3</text> <rect x="405" y="100" width="45" height="24" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="427" y="116">t4</text> <rect x="450" y="100" width="45" height="24" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="472" y="116">t5</text> <rect x="495" y="100" width="45" height="24" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="517" y="116">t6</text> <rect x="540" y="100" width="45" height="24" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="562" y="116">t7</text> <rect x="585" y="100" width="45" height="24" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="607" y="116">t8</text> </g> <text x="225" y="60" font-size="12">행 p0: 탭 9개 × C바이트 (각 탭 = memcpy 한 번)</text> <text x="640" y="88" font-size="12">p0</text><text x="640" y="118" font-size="12">p1</text> <text x="225" y="145" font-size="12">p0·p1의 창은 6탭이 겹침 → 같은 입력이 여러 번 복사됨</text> <text x="225" y="170" font-size="12">3×3, stride 1이면 입력 한 바이트가 최대 9번 복사 → 버퍼 ≈ 입력의 9배</text> <text x="225" y="200" font-size="12">이 실험: 입력 65,536 B → 전체 im2col 589,824 B</text>
<text x="225" y="225" font-size="12">부분 im2col(T=4 픽셀): 2,304 B만 쓰고 바로 GEMM에 넘김</text> <text x="10" y="265" font-size="12">대가: 메모리 9배 + 복사 시간. 얻는 것: 긴 연속 K 차원 → GEMM 블로킹·SIMD가 쉬워진다.</text> </svg>
```

그림 5 — im2col. 출력 픽셀마다 창 하나를 한 행으로 펼친다. 이웃 창이 겹치므로 같은 입력이 여러 번 복사되어 버퍼가 입력의 최대 k²배가 된다. MCU에서는 이 버퍼를 통째로 만들 SRAM이 없으므로 몇 픽셀씩만 펼치는 "부분 im2col"을 쓴다.

### 7.4 레지스터 블로킹 — 한 번 로드한 값을 여러 번 쓴다

단순 GEMM(V3)은 출력 하나마다 A의 한 행과 W의 한 행을 끝까지 읽는다 — 로드 2번에 MAC 1번꼴. 4×4 블로킹은 **A 4행 × W 4행**을 동시에 처리해 누산기 16개를 레지스터에 두고, k 한 걸음마다 로드 8번으로 MAC 16번을 한다. 로드당 MAC이 2배, 8×8이면 4배다 (E2 5.5절에서 GEMM으로 잰 것과 같은 원리).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 270"> <text x="10" y="22" font-size="14">4×4 레지스터 블로킹 + SDOT — k 16개 걸음마다 로드 8번, SDOT 16번</text> <text x="20" y="50" font-size="12">A (im2col) 4행</text> <rect x="20" y="58" width="150" height="22" fill="#4a7bd0" fill-opacity="0.3" stroke="currentColor"/><text x="95" y="74" font-size="11" text-anchor="middle">a0 (픽셀 p)</text> <rect x="20" y="82" width="150" height="22" fill="#4a7bd0" fill-opacity="0.3" stroke="currentColor"/><text x="95" y="98" font-size="11" text-anchor="middle">a1 (p+1)</text> <rect x="20" y="106" width="150" height="22" fill="#4a7bd0" fill-opacity="0.3" stroke="currentColor"/><text x="95" y="122" font-size="11" text-anchor="middle">a2 (p+2)</text> <rect x="20" y="130" width="150" height="22" fill="#4a7bd0" fill-opacity="0.3" stroke="currentColor"/><text x="95" y="146" font-size="11" text-anchor="middle">a3 (p+3)</text> <text x="20" y="180" font-size="12">W 4행 (출력 채널 o..o+3)</text> <rect x="20" y="188" width="150" height="22" fill="#e08a3c" fill-opacity="0.3" stroke="currentColor"/><text x="95" y="204" font-size="11" text-anchor="middle">b0..b3 (각 16 B)</text> <text x="20" y="232" font-size="11">각 행에서 16바이트(k..k+15)씩 int8x16 로드</text> <g font-size="10" text-anchor="middle">
<rect x="260" y="58" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="295" y="80">c[0][0]</text> <rect x="330" y="58" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="365" y="80">c[0][1]</text> <rect x="400" y="58" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="435" y="80">c[0][2]</text> <rect x="470" y="58" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="505" y="80">c[0][3]</text> <rect x="260" y="94" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="295" y="116">c[1][0]</text> <rect x="330" y="94" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="365" y="116">c[1][1]</text> <rect x="400" y="94" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="435" y="116">c[1][2]</text> <rect x="470" y="94" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="505" y="116">c[1][3]</text> <rect x="260" y="130" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="295" y="152">c[2][0]</text> <rect x="330" y="130" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="365" y="152">c[2][1]</text>
<rect x="400" y="130" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="435" y="152">c[2][2]</text> <rect x="470" y="130" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="505" y="152">c[2][3]</text> <rect x="260" y="166" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="295" y="188">c[3][0]</text> <rect x="330" y="166" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="365" y="188">c[3][1]</text> <rect x="400" y="166" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="435" y="188">c[3][2]</text> <rect x="470" y="166" width="70" height="36" fill="#3f9a6b" fill-opacity="0.25" stroke="currentColor"/><text x="505" y="188">c[3][3]</text> </g> <text x="260" y="50" font-size="12">누산기 16개 = int32x4 레지스터 16개</text> <text x="555" y="80" font-size="11">c[i][j] =</text><text x="555" y="96" font-size="11">SDOT(c, a_i, b_j)</text> <text x="555" y="130" font-size="11">레지스터:</text><text x="555" y="146" font-size="11">16 + 8 = 24 / 32</text> <text x="260" y="232" font-size="12">끝나면 c[i][j]의 4 lane을 더해(vaddvq) 출력 하나 → requant</text> <text x="260" y="252" font-size="12">1×1이면 로드 2번에 SDOT 1번, 4×4면 로드 8번에 SDOT 16번</text> </svg>
```

그림 6 — 4×4 블로킹. A의 4행과 W의 4행에서 16바이트씩 로드한 8개 벡터를 서로 짝지어 16번의 SDOT을 한다. 각 SDOT은 lane 4개 × int8 4쌍 = 16 MAC이므로 k 16걸음에 4×4×16 = 256 MAC.

```c
/* V5: 4x4 블록, 내부를 SDOT로 (픽셀 4개 × 출력채널 4개 = int32x4 누산기 16개) */
static void gemm_4x4_sdot(const int8_t *A, int p0, int n) {
    for (int p = 0; p < n; p += 4) for (int o = 0; o < CO; o += 4) {
        int32x4_t c[4][4];
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) c[i][j] = vdupq_n_s32(0);
        for (int k = 0; k < K; k += 16) {
            int8x16_t a[4], b[4];
            for (int i = 0; i < 4; i++) a[i] = vld1q_s8(A + (p + i) * K + k);
            for (int j = 0; j < 4; j++) b[j] = vld1q_s8(w + (o + j) * K + k);
            for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) c[i][j] = vdotq_s32(c[i][j], a[i], b[j]);
        }
        for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++)
            y[(p0 + p + i) * CO + o + j] = rq(vaddvq_s32(c[i][j]) + bias_eff[o + j], o + j);
    }
}
```

`rq()`는 `clamp_s8(mbqm(acc, mult[o], shft[o]) + ZP_OUT, -128, 127)`이다. 누산은 int8×int8 순수 내적이고, offset은 `bias_eff`에 접혀 있다.

V7은 같은 4×4 SDOT 블록을 **im2col 없이** 쓴다. NHWC에서 탭 하나의 64채널이 이미 연속이므로, V5의 내부 루프에서 A의 행 대신 패딩 입력 `xpad`에서 "가로로 이웃한 출력 픽셀 4개의 같은 탭"(`xr + i·CI + ic`, i = 0..3)을 로드하고, k 루프를 (ky, kx, ic 16개씩)으로 나누기만 하면 된다. 나머지(16개 누산기, `vaddvq_s32`, requant)는 V5와 같다.

### 7.5 결과

```sh
cc -std=c11 -Wall -Wextra -O2 -mcpu=apple-m2 bench.c j3k_ref.c -o bench && ./bench
```

```text
conv 3x3 SAME, 32x32x64 -> 32x32x64, MACs = 37.7 M
V0 reference (ic inner)         3.97 ms     9.5 GMAC/s (min-max   8.9-  9.6)  scratch       0 B  mismatches 0
V1 HWIO, oc inner               1.13 ms    33.5 GMAC/s (min-max  32.2- 33.6)  scratch     256 B  mismatches 0
V2 pre-pad + folded bias        1.17 ms    32.2 GMAC/s (min-max  30.8- 32.7)  scratch   73984 B  mismatches 0
V3 im2col + naive GEMM          0.65 ms    58.0 GMAC/s (min-max  57.1- 58.1)  scratch  589824 B  mismatches 0
V4 im2col + 4x4 C block         0.48 ms    77.9 GMAC/s (min-max  77.6- 78.3)  scratch  589824 B  mismatches 0
V5 im2col + 4x4 SDOT            0.31 ms   120.3 GMAC/s (min-max 120.0-120.5)  scratch  589824 B  mismatches 0
V6 partial im2col T=4 SDOT      0.31 ms   122.2 GMAC/s (min-max 118.4-122.6)  scratch    2304 B  mismatches 0
V6 partial im2col T=16 SDOT     0.31 ms   121.2 GMAC/s (min-max 119.4-121.8)  scratch    9216 B  mismatches 0
V6 partial im2col T=64 SDOT     0.31 ms   121.5 GMAC/s (min-max 117.0-121.7)  scratch   36864 B  mismatches 0
V6 partial im2col T=256 SDOT    0.32 ms   118.5 GMAC/s (min-max 115.5-119.9)  scratch  147456 B  mismatches 0
V7 direct (pre-pad) 4x4 SDOT    0.31 ms   120.8 GMAC/s (min-max 118.9-121.4)  scratch   73984 B  mismatches 0
```

비교를 위해 (1) 같은 소스를 자동 벡터화를 끄고(`-fno-vectorize -fno-slp-vectorize`) 컴파일한 결과, (2) 같은 shape의 int8 conv 한 층을 TFLite로 변환해 1스레드로 잰 결과(`tfl_bench.py`, 20회 × 7 중앙값)를 함께 놓는다.

```text
AUTO                                  0.22 ms   171.1 GMAC/s
BUILTIN_WITHOUT_DEFAULT_DELEGATES     0.22 ms   170.4 GMAC/s
BUILTIN_REF                           4.28 ms     8.8 GMAC/s
```

| 변형 | GMAC/s<br>자동 벡터화 켬 | GMAC/s<br>자동 벡터화 끔 | 추가 메모리 | 핵심 |
|---|---|---|---|---|
| V0 reference | 9.5 | 2.6 | 0 | TFLite BUILTIN_REF(8.8)와 같은 급 |
| V1 oc inner | 33.5 | 3.0 | 256 B | smlal 16-bit 경로 |
| V2 패딩 + bias 접기 | 32.2 | 2.7 | 74 KB | sdot 자동 생성, 짧은 내적(64)마다 reduce |
| V3 im2col + 단순 GEMM | 58.0 | 3.1 | 576 KB | 긴 내적(K = 576) |
| V4 im2col + 4×4 C | 77.9 | 3.4 | 576 KB | 컴파일러가 sdot.2s로 |
| V5 im2col + 4×4 SDOT | 120.3 | 119.8 | 576 KB | intrinsics라 플래그 무관 |
| V6 부분 im2col T=4 | 122.2 | 120.4 | **2.3 KB** | 메모리 1/256, 속도 동일 |
| V7 직접 SDOT | 120.8 | 121.6 | 74 KB (패딩) | im2col 불필요 |
| TFLite 기본 경로 | 171.1 | — | — | 실서비스 커널 (참고) |

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 410"> <text x="10" y="22" font-size="14">conv 3×3, 32×32×64→64 (37.7 M MAC), Apple M2 1코어, GMAC/s (7회 중앙값)</text> <line x1="220.0" y1="38" x2="220.0" y2="368" stroke="currentColor" stroke-opacity="0.2"/> <text x="220.0" y="384" font-size="11" text-anchor="middle">0</text> <line x1="290.0" y1="38" x2="290.0" y2="368" stroke="currentColor" stroke-opacity="0.2"/> <text x="290.0" y="384" font-size="11" text-anchor="middle">30</text> <line x1="360.0" y1="38" x2="360.0" y2="368" stroke="currentColor" stroke-opacity="0.2"/> <text x="360.0" y="384" font-size="11" text-anchor="middle">60</text> <line x1="430.0" y1="38" x2="430.0" y2="368" stroke="currentColor" stroke-opacity="0.2"/> <text x="430.0" y="384" font-size="11" text-anchor="middle">90</text> <line x1="500.0" y1="38" x2="500.0" y2="368" stroke="currentColor" stroke-opacity="0.2"/> <text x="500.0" y="384" font-size="11" text-anchor="middle">120</text> <line x1="570.0" y1="38" x2="570.0" y2="368" stroke="currentColor" stroke-opacity="0.2"/> <text x="570.0" y="384" font-size="11" text-anchor="middle">150</text> <line x1="640.0" y1="38" x2="640.0" y2="368" stroke="currentColor" stroke-opacity="0.2"/> <text x="640.0" y="384" font-size="11" text-anchor="middle">180</text> <text x="214" y="59" font-size="12" text-anchor="end">V0 reference (ic inner)</text>
<rect x="220" y="44" width="22.2" height="20" fill="#888" fill-opacity="0.8"/> <text x="247.2" y="59" font-size="12">9.5</text> <text x="214" y="91" font-size="12" text-anchor="end">V1 HWIO, oc inner</text> <rect x="220" y="76" width="78.2" height="20" fill="#4a7bd0" fill-opacity="0.8"/> <text x="303.2" y="91" font-size="12">33.5</text> <text x="214" y="123" font-size="12" text-anchor="end">V2 pre-pad + folded bias</text> <rect x="220" y="108" width="75.1" height="20" fill="#4a7bd0" fill-opacity="0.8"/> <text x="300.1" y="123" font-size="12">32.2</text> <text x="214" y="155" font-size="12" text-anchor="end">V3 im2col + naive GEMM</text> <rect x="220" y="140" width="135.3" height="20" fill="#e08a3c" fill-opacity="0.8"/> <text x="360.3" y="155" font-size="12">58.0</text> <text x="214" y="187" font-size="12" text-anchor="end">V4 im2col + 4x4 C block</text> <rect x="220" y="172" width="181.8" height="20" fill="#e08a3c" fill-opacity="0.8"/> <text x="406.8" y="187" font-size="12">77.9</text> <text x="214" y="219" font-size="12" text-anchor="end">V5 im2col + 4x4 SDOT</text> <rect x="220" y="204" width="280.7" height="20" fill="#3f9a6b" fill-opacity="0.8"/> <text x="505.7" y="219" font-size="12">120.3</text> <text x="214" y="251" font-size="12" text-anchor="end">V6 partial im2col T=4 SDOT</text> <rect x="220" y="236" width="285.1" height="20" fill="#3f9a6b" fill-opacity="0.8"/>
<text x="510.1" y="251" font-size="12">122.2</text> <text x="214" y="283" font-size="12" text-anchor="end">V7 direct 4x4 SDOT</text> <rect x="220" y="268" width="281.9" height="20" fill="#3f9a6b" fill-opacity="0.8"/> <text x="506.9" y="283" font-size="12">120.8</text> <text x="214" y="315" font-size="12" text-anchor="end">TFLite BUILTIN_REF</text> <rect x="220" y="300" width="20.5" height="20" fill="#888" fill-opacity="0.8"/> <text x="245.5" y="315" font-size="12">8.8</text> <text x="214" y="347" font-size="12" text-anchor="end">TFLite 기본(XNNPACK)</text> <rect x="220" y="332" width="399.2" height="20" fill="#d0564a" fill-opacity="0.8"/> <text x="624.2" y="347" font-size="12">171.1</text> <line x1="220" y1="38" x2="220" y2="370" stroke="currentColor"/> <text x="640" y="402" font-size="12" text-anchor="end">GMAC/s</text></svg>
```

그림 7 — 변형별 속도. 회색은 기준(우리 reference와 TFLite reference가 거의 같다), 파랑은 루프 재배치만, 주황은 im2col + 컴파일러 자동 벡터화, 초록은 SDOT 4×4 블록, 빨강은 TFLite의 기본 최적화 경로.

해석:

- **V0 → V5 (12.7배)**: 같은 정수 산술을 레이아웃·블로킹·명령 선택만으로 바꿔 얻은 것이다. 결과는 65,536개 전부 V0와 동일.
- **자동 벡터화를 끄면 V0~V4가 전부 2.6~3.4로 수렴**한다 — 스칼라 코드에서는 루프 순서나 im2col의 이점이 거의 없다. 이 둘의 이점은 "SIMD가 먹기 좋은 모양을 만드는 것"이다. MCU에서도 같다: M4의 SMLAD, M55의 MVE가 먹을 수 있는 모양으로 데이터를 정렬하는 것이 커널 작성의 절반이다.
- **V6: im2col 버퍼를 576 KB → 2.3 KB로 줄여도 속도가 같다** (122.2 vs 120.3). 이 Mac의 L1D(128 KB)·L2가 커서 전체 im2col의 이점(한 번에 펼치기)이 없고, 오히려 작은 버퍼가 L1에 머문다. MCU에서는 576 KB를 만들 SRAM 자체가 없으므로 부분 im2col이 유일한 선택이다.
- **V7 (im2col 없음)도 같은 속도**: NHWC + C가 16의 배수면 탭마다 연속 벡터가 이미 있으니 펼칠 필요가 없다. C가 작은 첫 층(예: C = 1~3)에서는 이 방법이 lane을 못 채워 im2col이 유리하다 — 그래서 실제 라이브러리는 shape에 따라 커널을 고른다.
- **TFLite 기본 경로는 171 GMAC/s** — 우리 최선보다 1.4배 빠르다. 더 큰 블록(E2에서 SDOT 4×4 GEMM이 199 GMAC/s까지 간 것처럼), 출력 requant의 벡터화(우리는 출력마다 스칼라 `mbqm`), 사전 packing 같은 차이로 추정하지만 그 내부를 직접 확인하지는 않았다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 350"> <text x="10" y="22" font-size="14">추가 scratch 메모리(로그) vs 속도 — 같은 conv 층</text> <line x1="70.0" y1="50" x2="70.0" y2="300" stroke="currentColor" stroke-opacity="0.2"/> <text x="70.0" y="316" font-size="11" text-anchor="middle">100 B</text> <line x1="215.0" y1="50" x2="215.0" y2="300" stroke="currentColor" stroke-opacity="0.2"/> <text x="215.0" y="316" font-size="11" text-anchor="middle">1 KB</text> <line x1="360.0" y1="50" x2="360.0" y2="300" stroke="currentColor" stroke-opacity="0.2"/> <text x="360.0" y="316" font-size="11" text-anchor="middle">10 KB</text> <line x1="505.0" y1="50" x2="505.0" y2="300" stroke="currentColor" stroke-opacity="0.2"/> <text x="505.0" y="316" font-size="11" text-anchor="middle">100 KB</text> <line x1="650.0" y1="50" x2="650.0" y2="300" stroke="currentColor" stroke-opacity="0.2"/> <text x="650.0" y="316" font-size="11" text-anchor="middle">1 MB</text> <line x1="70" y1="300.0" x2="650" y2="300.0" stroke="currentColor" stroke-opacity="0.12"/> <text x="64" y="304.0" font-size="11" text-anchor="end">0</text> <line x1="70" y1="264.3" x2="650" y2="264.3" stroke="currentColor" stroke-opacity="0.12"/> <text x="64" y="268.3" font-size="11" text-anchor="end">20</text> <line x1="70" y1="228.6" x2="650" y2="228.6" stroke="currentColor" stroke-opacity="0.12"/>
<text x="64" y="232.6" font-size="11" text-anchor="end">40</text> <line x1="70" y1="192.9" x2="650" y2="192.9" stroke="currentColor" stroke-opacity="0.12"/> <text x="64" y="196.9" font-size="11" text-anchor="end">60</text> <line x1="70" y1="157.1" x2="650" y2="157.1" stroke="currentColor" stroke-opacity="0.12"/> <text x="64" y="161.1" font-size="11" text-anchor="end">80</text> <line x1="70" y1="121.4" x2="650" y2="121.4" stroke="currentColor" stroke-opacity="0.12"/> <text x="64" y="125.4" font-size="11" text-anchor="end">100</text> <line x1="70" y1="85.7" x2="650" y2="85.7" stroke="currentColor" stroke-opacity="0.12"/> <text x="64" y="89.7" font-size="11" text-anchor="end">120</text> <line x1="70" y1="50.0" x2="650" y2="50.0" stroke="currentColor" stroke-opacity="0.12"/> <text x="64" y="54.0" font-size="11" text-anchor="end">140</text> <line x1="70" y1="300" x2="650" y2="300" stroke="currentColor"/><line x1="70" y1="50" x2="70" y2="300" stroke="currentColor"/> <line x1="478.4" y1="50" x2="478.4" y2="300" stroke="#d0564a" stroke-dasharray="5,4"/> <text x="474.4" y="62" font-size="11" text-anchor="end">입력 텐서 자체 = 64 KB</text> <circle cx="129.2" cy="240.2" r="5" fill="#4a7bd0"/> <text x="129.2" y="232.2" font-size="11" text-anchor="middle">V1</text> <circle cx="267.6" cy="81.8" r="5" fill="#3f9a6b"/> <text x="267.6" y="73.8" font-size="11" text-anchor="middle">V6 T=4</text>
<circle cx="354.9" cy="83.6" r="5" fill="#3f9a6b"/> <text x="354.9" y="75.6" font-size="11" text-anchor="middle">V6 T=16</text> <circle cx="442.2" cy="83.0" r="5" fill="#3f9a6b"/> <text x="442.2" y="99.0" font-size="11" text-anchor="middle">V6 T=64</text> <circle cx="486.0" cy="242.5" r="5" fill="#4a7bd0"/> <text x="486.0" y="256.5" font-size="11" text-anchor="middle">V2</text> <circle cx="486.0" cy="84.3" r="5" fill="#3f9a6b"/> <text x="486.0" y="76.3" font-size="11" text-anchor="middle">V7</text> <circle cx="529.5" cy="88.4" r="5" fill="#3f9a6b"/> <text x="529.5" y="104.4" font-size="11" text-anchor="middle">V6 T=256</text> <circle cx="616.8" cy="85.2" r="5" fill="#3f9a6b"/> <text x="608.8" y="81.2" font-size="11" text-anchor="end">V5 (full)</text> <circle cx="616.8" cy="160.9" r="5" fill="#e08a3c"/> <text x="608.8" y="156.9" font-size="11" text-anchor="end">V4 (full)</text> <circle cx="616.8" cy="196.4" r="5" fill="#e08a3c"/> <text x="608.8" y="192.4" font-size="11" text-anchor="end">V3 (full)</text> <text x="30" y="40" font-size="12">GMAC/s</text> <text x="650" y="334" font-size="12" text-anchor="end">추가 scratch 바이트</text></svg>
```

그림 8 — 추가 메모리(가로, 로그)와 속도(세로). 초록 점들이 2.3 KB에서 576 KB까지 수평으로 늘어서 있다: 이 층에서는 scratch를 250배 써도 속도가 늘지 않는다. 빨간 점선(입력 텐서 64 KB)보다 오른쪽은 "텐서보다 큰 임시 버퍼"라 MCU에서는 사실상 불가능한 영역이다. (V0은 추가 메모리 0이라 로그 축에 표시하지 않았다.)

### 7.6 타일링 — 층 전체가 TCM에 안 들어갈 때

호스트에서는 L1·L2가 크고 하드웨어 prefetcher가 있어서 타일링 효과가 잘 안 보인다. MCU/DSP는 다르다. 예를 들어 이 층을 **64 KB DTCM**(또는 NPU 옆 SRAM)에서 돌린다고 하자. 데이터는 weight 36,864 B(= 64 × 576) + 입력 65,536 B + 출력 65,536 B = 168 KB로, 한 번에 안 들어간다. 출력을 **R행짜리 띠(band)**로 나눠 처리하면, 띠 하나에 필요한 입력은 위아래 1행씩 더한 **R + 2행**(halo)이다.

```
행 하나 = W·C = 32·64 = 2,048 B
단일 버퍼:  weight 36,864 + 입력 (R+2)·2,048 + 출력 R·2,048 = 40,960 + 4,096·R
입력 이중 버퍼(DMA로 다음 띠를 미리 가져옴): 36,864 + 2·(R+2)·2,048 + R·2,048 = 45,056 + 6,144·R
입력 재전송 비율(halo 때문에 같은 행을 여러 번 가져옴) = (R+2)/R
```

| R (출력 행) | 단일 버퍼 바이트 | 이중 버퍼 바이트 | 64 KB 안? (이중) | 입력 재전송 비율 |
|---|---|---|---|---|
| 1 | 45,056 | 51,200 | O | 3.00 |
| 2 | 49,152 | 57,344 | O | 2.00 |
| 3 | 53,248 | 63,488 | O (2 KB 남음) | 1.67 |
| 4 | 57,344 | 69,632 | X | 1.50 |
| 6 | 65,536 | 81,920 | X | 1.33 |

말로 하면: 띠를 크게 할수록 halo 재전송이 줄지만 TCM을 더 먹는다. DMA 이중 버퍼링(J1의 핑퐁과 같은 구조)을 쓰면 R = 3이 한계이고, 이때 입력을 1.67배 더 읽는다. 스택·bias·multiplier 배열도 TCM에 있어야 하니 실제로는 R = 2가 안전하다. 이런 계산이 NPU 컴파일러가 하는 "tiling"의 축소판이다 (E5, E7).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 280"> <defs><marker id="a76" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <text x="10" y="22" font-size="14">출력 띠(R=3) 타일링 — 입력은 halo 포함 R+2 = 5행, TCM 64 KB 예산</text> <rect x="20" y="40" width="160" height="200" fill="none" stroke="currentColor"/> <text x="100" y="258" font-size="12" text-anchor="middle">입력 32행 (외부 SRAM/PSRAM)</text> <rect x="20" y="78" width="160" height="31" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/> <rect x="20" y="65.5" width="160" height="6.25" fill="#e08a3c" fill-opacity="0.5"/> <rect x="20" y="115.25" width="160" height="6.25" fill="#e08a3c" fill-opacity="0.5"/> <text x="185" y="70" font-size="11">halo 1행</text><text x="185" y="98" font-size="11">띠의 입력 3행</text><text x="185" y="122" font-size="11">halo 1행</text> <line x1="270" y1="95" x2="320" y2="95" stroke="currentColor" marker-end="url(#a76)"/><text x="270" y="85" font-size="11">DMA</text> <text x="330" y="50" font-size="12">TCM 64 KB (이중 버퍼, R = 3)</text> <rect x="330" y="60" width="207.4" height="30" fill="#888" fill-opacity="0.35" stroke="currentColor"/><text x="433" y="80" font-size="11" text-anchor="middle">weight 36,864</text>
<rect x="537.4" y="60" width="57.6" height="30" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor"/><text x="566" y="80" font-size="11" text-anchor="middle">입력A</text> <rect x="330" y="95" width="57.6" height="30" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/><text x="359" y="115" font-size="11" text-anchor="middle">입력B</text> <rect x="387.6" y="95" width="34.6" height="30" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/><text x="405" y="115" font-size="11" text-anchor="middle">출력</text> <rect x="422.2" y="95" width="11.5" height="30" fill="none" stroke="currentColor" stroke-dasharray="3,2"/> <text x="440" y="115" font-size="11">남는 2 KB (스택·파라미터)</text> <text x="330" y="150" font-size="12">각 입력 버퍼 = 5행 × 2,048 = 10,240 B</text> <text x="330" y="170" font-size="12">출력 = 3행 × 2,048 = 6,144 B</text> <text x="330" y="190" font-size="12">합 63,488 B / 65,536 B</text> <text x="330" y="215" font-size="12">계산(입력A) 하는 동안 DMA가 다음 띠(입력B)를 채운다</text> <text x="330" y="235" font-size="12">대가: 입력 행을 (R+2)/R = 1.67배 다시 읽는다</text> </svg>
```

그림 9 — 출력 3행 띠 타일링. 막대 길이는 바이트에 비례한다 (첫 줄 + 둘째 줄 = 64 KB). halo(주황)는 이웃 띠와 겹치는 입력 행이라 띠마다 다시 가져온다.

---

## 8. MCU 관점 — Cortex-M4(SMLAD)와 M55(Helium)의 conv 내부 커널

E2에서 "int8 내적 하나"를 SMLAD·VMLADAV로 짜 봤다. 실제 conv 커널은 그 내적을 **어떻게 묶고, offset을 어디서 처리하고, 레지스터를 어떻게 배분하는지**가 핵심이다.

### 8.1 M4 — int16 im2col 열 2개 × 출력 채널 2개

설계 (CMSIS-NN의 M4 DSP 경로로 알려진 구조를 따라 직접 짠 것 — 라이브러리 소스를 옮긴 것이 아니다):

- im2col 열을 만들 때 **int8 → int16 확장과 offset 덧셈을 한 번에**: `SXTAB16(off2, v)` = "v의 바이트 0·2를 부호 확장하고 off2의 두 16-bit 칸에 더한다". `ROR 8` 버전으로 바이트 1·3도. 이 비용은 열 하나당 한 번이고, 그 열은 모든 출력 채널이 재사용한다.
- 내부 루프: weight 워드(int8 ×4)를 SXTB16 두 번으로 펼치고, int16 열 워드와 SMLAD. **2×2 블록**(출력채널 2 × 열 2)이라 weight 확장 한 번이 열 2개에, 열 로드 한 번이 출력채널 2개에 쓰인다.
- 바이트 순서: 열은 [b0,b2],[b1,b3] 순서로 저장하고 weight도 같은 순서로 펼치므로 PKHBT 같은 재정렬이 필요 없다 (내적은 덧셈 순서와 무관 — E2 2.5절).

```c
/* M4용 conv 내부 커널: 출력채널 2개 × 출력픽셀(열) 2개 블록. HOST 정의 시 intrinsics를 C로 흉내 */
#include <stdint.h>

#ifdef HOST
static uint32_t __ror(uint32_t x, uint32_t r) { return (x >> r) | (x << (32 - r)); }
static uint32_t __sxtb16(uint32_t x) {
    return (uint16_t)(int8_t)x | (uint32_t)(uint16_t)(int8_t)(x >> 16) << 16; }
static uint32_t __sxtab16(uint32_t a, uint32_t x) {          /* a의 각 16-bit 칸 + sxtb16(x) */
    uint32_t s = __sxtb16(x);
    return (uint16_t)(a + s) | ((a >> 16) + (s >> 16)) << 16; }
static int32_t __smlad(uint32_t x, uint32_t y, int32_t acc) {
    return acc + (int16_t)x * (int16_t)y + (int16_t)(x >> 16) * (int16_t)(y >> 16); }
#else
#include <arm_acle.h>
#endif
static inline uint32_t rd32(const void *p) { uint32_t v; __builtin_memcpy(&v, p, 4); return v; }
/* im2col 한 열을 int16으로: (x + offset), 순서는 [b0,b2],[b1,b3] (재정렬 상태 그대로 둔다) */
void col_s8_to_s16_offset(const int8_t *src, int16_t *dst, int n, int32_t offset) {
    uint32_t off2 = (uint16_t)offset | (uint32_t)(uint16_t)offset << 16;
    for (int i = 0; i < n; i += 4, dst += 4) {
        uint32_t v = rd32(src + i);
        uint32_t e02 = __sxtab16(off2, v), e13 = __sxtab16(off2, __ror(v, 8));
        __builtin_memcpy(dst, &e02, 4); __builtin_memcpy(dst + 2, &e13, 4);
    }
}
/* 2x2 블록: out[o][c] = Σ_k w[o][k] · col[c][k]   (K % 4 == 0). 포인터 증가형 루프 = 레지스터 절약 */
void mat_2x2_s8_s16(const int8_t *w0, const int8_t *w1, const int16_t *c0, const int16_t *c1,
                    int K, int32_t acc[4]) {
    int32_t a00 = 0, a01 = 0, a10 = 0, a11 = 0;
    const int8_t *end = w0 + K;
#pragma clang loop unroll(disable)     /* M4는 범용 레지스터 13개 남짓 — 펼치면 spill */
    while (w0 < end) {
        uint32_t wa = rd32(w0), wb = rd32(w1);
        uint32_t wa02 = __sxtb16(wa), wa13 = __sxtb16(__ror(wa, 8));
        uint32_t wb02 = __sxtb16(wb), wb13 = __sxtb16(__ror(wb, 8));
        uint32_t x0a = rd32(c0), x0b = rd32(c0 + 2), x1a = rd32(c1), x1b = rd32(c1 + 2);
        a00 = __smlad(wa02, x0a, a00); a00 = __smlad(wa13, x0b, a00);
        a01 = __smlad(wa02, x1a, a01); a01 = __smlad(wa13, x1b, a01);
        a10 = __smlad(wb02, x0a, a10); a10 = __smlad(wb13, x0b, a10);
        a11 = __smlad(wb02, x1a, a11); a11 = __smlad(wb13, x1b, a11);
        w0 += 4; w1 += 4; c0 += 4; c1 += 4;
    }
    acc[0] = a00; acc[1] = a01; acc[2] = a10; acc[3] = a11;
}
```

M4 실물이 없으니 **intrinsics를 C로 흉내 낸 host 빌드**(`-DHOST`)로 논리를 검증한다 — 극단값(x = −128, offset = −128 → −256, weight ±127) 케이스를 3번에 1번 섞었다 (`m4_test.c`, 평범한 C 내적과 비교).

```text
2x2 SMLAD block (host emulation): 2000 trials x 4 outputs, mismatches = 0
```

그리고 M4 타깃 어셈블리:

```sh
cc --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfloat-abi=hard -std=c11 -Wall -Wextra -O2 -S m4_kernel.c -o m4_kernel.s
```

```text
.LBB1_2:                                @ =>This Inner Loop Header: Depth=1
	ldr	r4, [r0], #4
	ldr	r5, [r1, #4]!
	ldr	r7, [r2, #8]!
	ldr	r12, [r3, #8]!
	sxtb16	r6, r4
	smlad	r9, r6, r7, r9
	smlad	r11, r6, r12, r11
	sxtb16	r6, r5
	smlad	r10, r6, r7, r10
	smlad	r12, r6, r12, r8
	ldr	r6, [r2, #4]
	ldr	r7, [r3, #4]
	sxtb16	r4, r4, ror #8
	smlad	r9, r4, r6, r9
	smlad	r11, r4, r7, r11
	sxtb16	r4, r5, ror #8
	smlad	r10, r4, r6, r10
	cmp	r0, lr
	smlad	r8, r4, r7, r12
	blo	.LBB1_2
```

출력에서 볼 것: 한 반복 = **MAC 16개에 명령 20개** (`ldr` 6, `sxtb16` 4, `smlad` 8, `cmp`+`blo` 2). spill(스택 저장/복원)이 없다.

이 결과까지 두 번 고쳤다 — 그 과정이 M4 커널 작성의 실제 모습이다.

| 시도 | 내부 루프 (분기 포함 명령 수) | MAC 16개당 명령 | spill/reload |
|---|---|---|---|
| 1. 인덱스 루프 `for (k = 0; k < K; k += 4)`, 기본 `-O2` | 컴파일러가 4번 펼침: 111개 / MAC 64개 | 약 28 | 26개 |
| 2. + `#pragma clang loop unroll(disable)` | 25개 / MAC 16개 | 25 | 3개 (+ 스택에서 K를 매번 다시 읽음) |
| 3. + 포인터 증가형 루프(`while (w0 < end)`) | 20개 / MAC 16개 | **20** | **0** |

말로 하면: M4는 쓸 수 있는 범용 레지스터가 13개 남짓이다. 누산기 4 + 포인터 4 + 끝 주소 1 + 임시 4~5가 꽉 찬다. 인덱스 `k`와 `K`를 따로 두거나 루프를 펼치면 바로 스택으로 넘친다. **"블록을 키우면 재사용이 늘지만 레지스터가 넘친다"**가 MCU 커널의 핵심 긴장이다 — Cortex-A의 32개 NEON 레지스터에서는 4×4가 쉬웠던 이유다.

사이클 추정 (Cortex-M4 TRM의 명령 타이밍 기준, 메모리 대기 0인 TCM 가정 — 실측이 아니다): `smlad`·`sxtb16`·`cmp`는 1사이클, 연속된 `ldr`은 첫 개 2사이클 이후 파이프라인되어 대략 1사이클씩, 분기는 taken일 때 2~4사이클.

```
ldr 6개 ≈ 5 + 3 = 8,  sxtb16 4,  smlad 8,  cmp 1,  blo ≈ 2~3   →  약 23~24 사이클 / 16 MAC ≈ 1.5 사이클/MAC
reference C (ldrsb ×8, add ×4, mla ×4, 루프 관리 ≈ 22개 / 4 MAC)  → 대략 6~7 사이클/MAC
```

reference C의 M4 어셈블리(`j3k_ref.c`를 같은 옵션으로 컴파일)는 4번 펼쳐진 루프에 `ldrsb.w` 8개, `add`(offset) 4개, `mla` 4개가 나왔다 — MAC 4개에 명령 22개. 따라서 같은 conv가 **명령 수 기준 약 4배** 차이다. 7절의 37.7 M MAC 층을 M4 @ 100 MHz에서 돌린다면 1.5 사이클/MAC 기준 약 0.57초, reference는 2초 이상 — 웨어러블 always-on MCU에 이 크기의 층을 올리면 안 된다는 감이 바로 나온다 (I1의 예산 계산).

### 8.2 M55 — Helium은 누산기가 범용 레지스터다

Helium의 `VMLADAVA.S8`(어셈블러 표기 `vmlava.s8`)은 int8 16쌍의 곱을 **스칼라 범용 레지스터 하나에** 누산한다 (E2 3.6절). 그래서 MVE 블로킹의 제약은 Q 레지스터(8개)보다 **범용 레지스터**다. 4×2 블록(누산기 8 + 포인터 6 + 루프 카운터)과 2×2 블록(누산기 4 + 포인터 4)을 둘 다 짜서 비교했다. 아래는 2×2이고, 4×2(`mat_4x2_s8_mve`)는 같은 패턴을 weight 행 4개로 늘린 것이다 (열 2개를 한 번 로드해 weight 4행과 각각 곱함). 오프셋은 bias에 접고(2.5절) im2col 패딩은 zp_in으로 채운다고 가정한 int8×int8 순수 내적이다.

```c
/* 2x2 블록: 누산기 4개 + 포인터 4개 → GPR 안에 들어간다 */
void mat_2x2_s8_mve(const int8_t *w0, const int8_t *w1, const int8_t *c0, const int8_t *c1,
                    int K, int32_t acc[4]) {
    int32_t s00 = 0, s01 = 0, s10 = 0, s11 = 0;
    for (int n = K; n > 0; n -= 16) {
        mve_pred16_t p = vctp8q((uint32_t)n);
        int8x16_t x0 = vldrbq_z_s8(c0, p), x1 = vldrbq_z_s8(c1, p);
        int8x16_t a = vldrbq_z_s8(w0, p), b = vldrbq_z_s8(w1, p);
        s00 = vmladavaq_s8(s00, a, x0); s01 = vmladavaq_s8(s01, a, x1);
        s10 = vmladavaq_s8(s10, b, x0); s11 = vmladavaq_s8(s11, b, x1);
        c0 += 16; c1 += 16; w0 += 16; w1 += 16;
    }
    acc[0] = s00; acc[1] = s01; acc[2] = s10; acc[3] = s11;
}
```

(파일 맨 위에는 M4 버전과 같은 `#ifdef HOST` 블록이 있어서, host에서는 `int8x16_t`·`vctp8q`·`vldrbq_z_s8`·`vmladavaq_s8`을 16칸 C 루프로 흉내 낸다.) host 검증 — K = 1..600 (7 간격, 16의 배수가 아닌 K 포함 → tail predication 경로), 한 열은 전부 −128:

```text
4x2 and 2x2 MVE blocks (host emulation): 688 outputs over K=1..600, mismatches = 0
```

M55 어셈블리(`--target=thumbv8.1m.main-none-eabihf -mcpu=cortex-m55 -mfloat-abi=hard -O2 -S`). 4×2 블록의 내부 루프 (31개 명령 중 앞부분 발췌):

```text
.LBB0_2:                                @ =>This Inner Loop Header: Depth=1
	mov	r1, r10
	ldr.w	r10, [sp]                       @ 4-byte Reload
	vldrb.u8	q0, [r8], #16
	vldrb.u8	q2, [r4], #16
	vmlava.s8	r10, q2, q0
	vldrb.u8	q1, [r9], #16
	str.w	r10, [sp]                       @ 4-byte Spill
	mov	r10, r1
	...
	letp	lr, .LBB0_2
```

2×2 블록의 내부 루프:

```text
	dlstp.8	lr, r5
	.p2align	2
.LBB1_2:                                @ =>This Inner Loop Header: Depth=1
	vldrb.u8	q0, [r2], #16
	vldrb.u8	q1, [r0], #16
	vmlava.s8	r8, q1, q0
	vldrb.u8	q2, [r3], #16
	vmlava.s8	r6, q1, q2
	vldrb.u8	q1, [r1], #16
	vmlava.s8	r4, q1, q0
	vmlava.s8	r10, q1, q2
	letp	lr, .LBB1_2
```

출력에서 볼 것:

- 4×2: MAC 128개(VMLAVA 8 × 16)에 명령 31개인데, 그중 **spill/reload 8개와 `mov` 8개**가 레지스터 부족 때문이다. 누산기 8개 + 포인터 6개 + `lr`이 범용 레지스터를 넘쳤다.
- 2×2: **MAC 64개에 명령 9개**, spill 0. `dlstp.8`/`letp`로 tail predication이 하드웨어 루프에 흡수됐다 (E2 3.4절). 로드 4개 중 열 2개는 두 번씩, weight 2개도 두 번씩 쓰인다.
- 추정(실측 아님): M55는 beat 단위로 128-bit 명령을 처리하는 구조라 VMLAVA.S8 하나에 2 beat-사이클 정도로 알려져 있고, 로드와 MAC이 겹쳐 실행될 수 있다. 낙관적으로 9명령 ≈ 9~10 사이클이면 **약 6~7 MAC/사이클**, M4의 1.5 사이클/MAC(= 0.67 MAC/사이클)보다 10배 안팎. 실제 수치는 메모리 대기·im2col 비용을 포함해 FVP 또는 실보드에서 재야 한다 (K 노트).

### 8.3 CMSIS-NN은 무엇을 다르게 하나 (확인한 범위 + 추정)

CMSIS-NN(github.com/ARM-software/CMSIS-NN)은 Arm이 Cortex-M용으로 만든 int8/int16 커널 라이브러리이고, TFLM의 Cortex-M 최적화 커널이 이것을 호출한다 (F2). 이름은 버전마다 바뀌므로 실제로는 소스를 확인해야 하지만, 일반적으로 알려진 구조는 이렇다.

- **op별 진입 함수 + shape별 특화 커널**: `arm_convolve_s8`(일반), 1×1 전용, depthwise 3×3 전용 같은 변형을 두고 wrapper가 shape를 보고 고른다. 7.5절에서 "C가 작은 첫 층은 im2col, C가 큰 층은 direct"처럼 shape마다 최적이 다르다는 것을 봤는데, 라이브러리는 이것을 dispatch로 해결한다.
- **부분 im2col (2열)**: DSP(M4/M7) 경로는 출력 2픽셀 분량의 열만 int16으로 펼쳐 놓고(offset 포함) 행렬 커널을 부르는 구조로 알려져 있다 — 8.1절의 2×2와 같은 발상. 행렬 커널 이름으로 `arm_nn_mat_mult_kernel_s8_s16`이 있다.
- **MVE 경로는 int8 그대로**: Helium에서는 int8×int8 `VMLADAV`를 쓰므로 offset을 루프 밖에서 처리한다(행 합에 offset을 곱해 보정하는 방식으로 알고 있다 — 2.5절의 접기와 같은 대수).
- **requantization은 TFLite와 같은 규칙**: TFLM에서 CMSIS-NN 커널로 바꿔도 reference 커널과 같은 출력을 내는 것을 목표로 한다. 다만 "모든 op가 항상 bit-exact"인지는 버전·op마다 확인해야 한다 (C8의 원칙: 가정하지 말고 재라).

---

## 9. 커널 테스트 — randomized differential testing과 mutation testing

### 9.1 무엇을 무엇과 비교하나

커널 테스트의 기본형은 **differential testing**(차등 테스트)이다: 같은 입력을 두 구현에 넣고 출력을 비교한다. 한쪽이 신뢰할 수 있는 기준(oracle)이면 된다. 여기서는 두 층으로 한다.

| 층 | oracle | 입력·파라미터 생성 | 잡는 것 | 못 잡는 것 |
|---|---|---|---|---|
| A. TFLite 기준 | TFLite BUILTIN_REF 인터프리터 | 무작위 shape·stride·padding·activation의 Keras 모델 → **변환기가** 파라미터 생성 | 규약 해석 오류(패딩, multiplier 계산, layout, add 알고리즘) | 변환기가 만들지 않는 파라미터 영역 (9.4절) |
| B. 독립 reference | 규격에서 직접 쓴 numpy 구현 | **파라미터 자체를 무작위로** (임의 zp, 좁은 clamp, shift > 0, 극단 multiplier) | 경계값·반올림·clamp 버그 | 규약을 잘못 이해한 것(numpy도 같이 틀림) |

두 층은 서로의 사각지대를 메운다. Don의 SSD 검증 경험으로 치면 A는 "실제 호스트 드라이버가 보내는 커맨드로 하는 테스트", B는 "스펙의 모든 필드 조합을 퍼징하는 테스트"다 (C8, J6와 연결).

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300"> <defs><marker id="a9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs> <text x="10" y="22" font-size="14">두 층의 차등 테스트 harness (Python이 생성·비교, C는 ctypes로 호출)</text> <text x="10" y="48" font-size="12">층 A</text> <rect x="50" y="35" width="120" height="44" rx="5" fill="#888" fill-opacity="0.2" stroke="#888"/><text x="110" y="54" font-size="11" text-anchor="middle">무작위 shape</text><text x="110" y="70" font-size="11" text-anchor="middle">Keras 1-op 모델</text> <rect x="190" y="35" width="110" height="44" rx="5" fill="#888" fill-opacity="0.2" stroke="#888"/><text x="245" y="54" font-size="11" text-anchor="middle">converter</text><text x="245" y="70" font-size="11" text-anchor="middle">int8 .tflite</text> <rect x="320" y="35" width="130" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="385" y="54" font-size="11" text-anchor="middle">BUILTIN_REF 실행</text><text x="385" y="70" font-size="11" text-anchor="middle">oracle 출력</text> <rect x="320" y="95" width="130" height="44" rx="5" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="385" y="114" font-size="11" text-anchor="middle">같은 입력·파라미터</text><text x="385" y="130" font-size="11" text-anchor="middle">→ C 커널 (ctypes)</text>
<rect x="480" y="60" width="120" height="44" rx="5" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="540" y="79" font-size="11" text-anchor="middle">원소 비교</text><text x="540" y="95" font-size="11" text-anchor="middle">pass / fail</text> <line x1="170" y1="57" x2="188" y2="57" stroke="currentColor" marker-end="url(#a9)"/><line x1="300" y1="57" x2="318" y2="57" stroke="currentColor" marker-end="url(#a9)"/> <line x1="245" y1="79" x2="318" y2="112" stroke="currentColor" marker-end="url(#a9)"/> <line x1="450" y1="57" x2="478" y2="75" stroke="currentColor" marker-end="url(#a9)"/><line x1="450" y1="117" x2="478" y2="92" stroke="currentColor" marker-end="url(#a9)"/> <text x="10" y="188" font-size="12">층 B</text> <rect x="50" y="175" width="120" height="44" rx="5" fill="#888" fill-opacity="0.2" stroke="#888"/><text x="110" y="194" font-size="11" text-anchor="middle">무작위 파라미터</text><text x="110" y="210" font-size="11" text-anchor="middle">zp, clamp, shift&gt;0</text> <rect x="320" y="160" width="130" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.2" stroke="#4a7bd0"/><text x="385" y="179" font-size="11" text-anchor="middle">numpy reference</text><text x="385" y="195" font-size="11" text-anchor="middle">(규격에서 직접)</text>
<rect x="320" y="215" width="130" height="44" rx="5" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="385" y="234" font-size="11" text-anchor="middle">C 커널 (ctypes)</text><text x="385" y="250" font-size="11" text-anchor="middle">같은 .dylib</text> <rect x="480" y="190" width="120" height="44" rx="5" fill="#d0564a" fill-opacity="0.2" stroke="#d0564a"/><text x="540" y="209" font-size="11" text-anchor="middle">원소 비교</text><text x="540" y="225" font-size="11" text-anchor="middle">pass / fail</text> <line x1="170" y1="190" x2="318" y2="182" stroke="currentColor" marker-end="url(#a9)"/><line x1="170" y1="205" x2="318" y2="235" stroke="currentColor" marker-end="url(#a9)"/> <line x1="450" y1="182" x2="478" y2="205" stroke="currentColor" marker-end="url(#a9)"/><line x1="450" y1="237" x2="478" y2="222" stroke="currentColor" marker-end="url(#a9)"/> <text x="10" y="288" font-size="12">mutation test: C 커널에 버그를 일부러 넣은 .dylib로 두 층을 다시 돌려 "버그를 잡는가"를 본다 (9.4절)</text> </svg>
```

그림 10 — 두 층의 harness. 층 A는 변환기가 만든 "현실적인" 파라미터로 규약 해석을 검증하고, 층 B는 파라미터 공간 전체를 무작위로 훑어 경계·반올림을 검증한다.

### 9.2 층 A — TFLite oracle 기반 fuzz harness

C 커널을 공유 라이브러리로 빌드하고, 구조체 대신 평탄한 int 배열을 받는 얇은 wrapper를 둔다 (ctypes에서 구조체를 다루는 것보다 단순하고, 펌웨어의 테스트 커맨드 인터페이스와 비슷한 모양이다).

```c
/* ctypes용 평탄한 인터페이스: g[] = {ih,iw,ic, oh,ow,oc, fh,fw, sh,sw, ph,pw, in_off,out_off, amin,amax} */
void run_conv(const int *g, int depthwise, const int8_t *x, const int8_t *w, const int32_t *b,
              const int32_t *mult, const int32_t *shift, int8_t *y) {
    conv_params_t p = {g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]};
    per_ch_quant_t q = {mult, shift};
    dims_t in = {1, g[0], g[1], g[2]}, out = {1, g[3], g[4], g[5]}, f = {g[5], g[6], g[7], g[2]};
    if (depthwise) dwconv_s8_ref(&p, &q, in, x, f, w, b, out, y);
    else conv_s8_ref(&p, &q, in, x, f, w, b, out, y);
}
/* run_pool, run_add, run_requant도 같은 모양 (wrap.c) */
```

```sh
cc -std=c11 -Wall -Wextra -O2 -shared -fPIC wrap.c j3k_ref.c j3k_more.c -o libj3k.dylib
```

harness 본체 (`fuzz.py`, `.venv-tf`). 케이스마다: 무작위 shape(1~12)·채널(1~16)·커널(1, 2, 3, 5)·stride(1~3)·padding(SAME/VALID)·activation(conv·dw만), 무작위 calibration 범위(→ zp가 매번 다름), 채널마다 10⁻²~10¹배 다른 weight, 큰 bias. 입력은 무작위 / 전부 −128 / 전부 127 / −128·127 체커 중 하나 — **극단값 패턴을 의도적으로** 넣는다.

(발췌 — 전체 88줄 중 모델 생성·add 부분 생략)

```python
import os, sys, math, time, contextlib, io, warnings; os.environ["TF_CPP_MIN_LOG_LEVEL"] = "3"
warnings.filterwarnings("ignore")
import numpy as np, tensorflow as tf, ctypes as C
from qm import quantize_multiplier
lib = C.CDLL(os.environ.get("J3LIB", "./libj3k.dylib")); L = tf.keras.layers; f32 = np.float32
P = lambda a: a.ctypes.data_as(C.c_void_p)
REF = tf.lite.experimental.OpResolverType.BUILTIN_REF

def convert(model, rep):
    cv = tf.lite.TFLiteConverter.from_keras_model(model)
    cv.optimizations = [tf.lite.Optimize.DEFAULT]; cv.representative_dataset = rep
    cv.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    cv.inference_input_type = cv.inference_output_type = tf.int8
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        it = tf.lite.Interpreter(model_content=cv.convert(), experimental_op_resolver_type=REF,
                                 experimental_preserve_all_tensors=True)
    it.allocate_tensors(); return it

def one_case(rng, kind):
    H, W, Cc = rng.integers(1, 13), rng.integers(1, 13), int(rng.integers(1, 17))
    k = int(rng.choice([1, 2, 3, 5])); s = int(rng.integers(1, 4))
    # … (생략) padding·activation·calibration 범위 선택, Keras 1-op 모델 생성, weight 스케일 조정, rep()
    it = convert(m, rep)
    op = [o for o in it._get_ops_details() if o["op_name"] not in ("RESHAPE", "QUANTIZE")][-1]
    T = {t["index"]: t for t in it.get_tensor_details()}
    qp = lambda i: (T[i]["quantization_parameters"]["scales"], T[i]["quantization_parameters"]["zero_points"])
    for d in it.get_input_details():
        pat = rng.integers(4)                      # 0: 무작위, 1: 전부 -128, 2: 전부 127, 3: -128/127 체커
        sh = d["shape"]
        x = {0: rng.integers(-128, 128, sh), 1: np.full(sh, -128), 2: np.full(sh, 127),
             3: np.where(rng.random(sh) < .5, -128, 127)}[pat]
        it.set_tensor(d["index"], x.astype(np.int8))
    it.invoke(); ref = it.get_tensor(op["outputs"][0]); out = np.zeros_like(ref)
    xs = [np.ascontiguousarray(it.get_tensor(i)) for i in op["inputs"][: (2 if kind == "add" else 1)]]
    (sy,), (zy,) = qp(op["outputs"][0]); oh, ow, oc = ref.shape[1:]
    # … (생략) add: 5절 규칙으로 add_params를 만들어 lib.run_add로 비교하고 return
    (sx,), (zx,) = qp(op["inputs"][0])
    tot = lambda o, i: max((o - 1) * s + k - i, 0) if pad == "same" else 0
    lo_c, hi_c = -128, 127
    if act in ("relu", "relu6"): lo_c = max(lo_c, int(zy))
    if act == "relu6": hi_c = min(hi_c, int(zy) + int(math.floor(6 / float(sy) + 0.5)))
    g = np.array([H, W, Cc, oh, ow, oc, k, k, s, s, tot(oh, H) // 2, tot(ow, W) // 2,
                  -zx, zy, lo_c, hi_c], np.int32)
    if kind in ("avg", "max"):
        lib.run_pool(P(g), kind == "max", P(xs[0]), P(out))
    else:
        w = it.get_tensor(op["inputs"][1]); b = it.get_tensor(op["inputs"][2]).astype(np.int32)
        sw, _ = qp(op["inputs"][1])
        mq = [quantize_multiplier(float(sx) * float(v) / float(sy)) for v in sw]
        mult = np.array([a for a, _ in mq], np.int32); shf = np.array([b_ for _, b_ in mq], np.int32)
        lib.run_conv(P(g), op["op_name"] == "DEPTHWISE_CONV_2D", P(xs[0]), P(np.ascontiguousarray(w)),
                     P(b), P(mult), P(shf), P(out))
    return op["op_name"], int((out != ref).sum()), ref.size

if __name__ == "__main__":
    rng = np.random.default_rng(2026); N = int(sys.argv[1]); t0 = time.time()
    for kind in sys.argv[2:] or ["conv", "dw", "avg", "max", "add"]:
        res = [one_case(rng, kind) for _ in range(N)]
        npass = sum(r[1] == 0 for r in res); nel = sum(r[2] for r in res)
        names = sorted(set(r[0] for r in res))
        print(f"{kind:5s} cases={N}  pass={npass}  fail={N - npass}  outputs compared={nel}  ops={names}")
    print(f"elapsed {time.time() - t0:.0f}s")
```

`qm.py`는 3.2절의 `quantize_multiplier` 함수 하나만 담은 파일이다.

```sh
.venv-tf/bin/python fuzz.py 200 2>/dev/null
```

```text
conv  cases=200  pass=200  fail=0  outputs compared=27150  ops=['CONV_2D']
dw    cases=200  pass=200  fail=0  outputs compared=29336  ops=['DEPTHWISE_CONV_2D']
avg   cases=200  pass=200  fail=0  outputs compared=27145  ops=['AVERAGE_POOL_2D']
max   cases=200  pass=200  fail=0  outputs compared=27643  ops=['MAX_POOL_2D']
add   cases=200  pass=200  fail=0  outputs compared=76272  ops=['ADD']
elapsed 76s
```

출력에서 볼 것: **1,000 케이스 전부 통과, 187,546개 출력이 bit-exact**. 76초 — 케이스당 변환 포함 약 0.08초라 CI에 넣기 충분하다.

harness를 만들면서 실제로 걸린 것 세 가지 (모두 harness 쪽 버그였다 — 이것도 흔한 일이다):

- 처음 돌렸을 때 avg·max pool 일부가 틀렸다. 원인은 pool 케이스에도 무작위 activation을 골라 clamp 범위를 `zp_out` 이상으로 넣은 것 — pool layer는 activation을 받지 않는데 harness가 clamp만 적용했다. **oracle과 같은 모델을 기술하고 있는지**부터 의심한다.
- add 케이스 일부에서 변환기가 ADD 앞에 `RESHAPE`를 끼워 넣어 "op가 하나뿐"이라는 가정이 깨졌다. 그래서 op 목록에서 대상 op를 고르고, 그 op의 **실제 입력 텐서**(preserve_all_tensors로 보존된 값)를 C에 넘기도록 바꿨다.
- 처음엔 `export_h.py`를 import했더니 그 파일의 최상위 코드가 같이 실행됐다 → 함수를 `qm.py`로 분리.

### 9.3 층 B — 파라미터 공간 퍼징 (numpy 독립 reference)

변환기가 절대 만들지 않는 조합 — zp_in·zp_out 아무 값, clamp 범위 임의, shift > 0, multiplier 끝값 — 을 직접 만든다. 기준은 규격 문장("반올림은 0에서 먼 쪽")에서 바로 쓴 numpy int64 구현이다 (`level1.py`, `.venv`).

```python
import os, sys, ctypes as C, numpy as np
lib = C.CDLL(os.environ.get("J3LIB", "./libj3k.dylib")); P = lambda a: a.ctypes.data_as(C.c_void_p)
def requant_ref(x, m, s):                         # 규격에서 바로 쓴 기준: 반올림은 0에서 먼 쪽
    x = x.astype(np.int64) << np.maximum(s, 0)
    ab = x * m; t = np.sign(ab) * ((np.abs(ab) + (1 << 30)) >> 31)
    e = np.maximum(-s, 0); h = np.where(e > 0, np.int64(1) << np.maximum(e - 1, 0), 0)
    return (np.sign(t) * ((np.abs(t) + h) >> e)).astype(np.int32)
def conv_ref(x, w, b, m, s, zi, zo, lo, hi, st, ph, pw, oh, ow):   # x:(H,W,C) w:(O,kh,kw,C)
    O, kh, kw, Cc = w.shape; H, W, _ = x.shape
    xp = np.full((H + 2 * kh + 2 * st, W + 2 * kw + 2 * st, Cc), zi, np.int64)  # 패딩 = zp_in
    xp[ph:ph + H, pw:pw + W] = x
    acc = np.zeros((oh, ow, O), np.int64)
    for i in range(oh):
        for j in range(ow):
            patch = xp[i * st:i * st + kh, j * st:j * st + kw] - zi
            acc[i, j] = np.tensordot(w.astype(np.int64), patch, axes=([1, 2, 3], [0, 1, 2]))
    y = requant_ref((acc + b).reshape(-1, O), m[None, :], s[None, :]).reshape(oh, ow, O) + zo
    return np.clip(y, lo, hi).astype(np.int8)
rng = np.random.default_rng(7)
# (1) requant 단독: 극단 포함 200,000개
n = 200_000
x = rng.integers(-(1 << 24), 1 << 24, n).astype(np.int32); x[:4] = [0, 1, -1, -(1 << 24)]
m = rng.integers(1 << 30, (1 << 31) - 1, n).astype(np.int32); s = rng.integers(-31, 4, n).astype(np.int32)
y = np.zeros(n, np.int32); lib.run_requant(n, P(x), P(m), P(s), P(y))
print("requant: mismatches = %d / %d" % ((y != requant_ref(x, m, s)).sum(), n))
# (2) conv: 변환기가 만들지 않는 파라미터 (임의 zp, 좁은 act 범위, shift>0)
fails = runs = 0
for t in range(300):
    H, W, Cc, O, k, st = (int(v) for v in rng.integers([1, 1, 1, 1, 1, 1], [10, 10, 9, 9, 4, 3]))
    ph, pw = int(rng.integers(0, k)), int(rng.integers(0, k))
    oh, ow = (H + 2 * ph - k) // st + 1, (W + 2 * pw - k) // st + 1
    if oh < 1 or ow < 1: continue
    runs += 1
    xi = rng.integers(-128, 128, (H, W, Cc)).astype(np.int8); w = rng.integers(-127, 128, (O, k, k, Cc)).astype(np.int8)
    b = rng.integers(-5000, 5000, O).astype(np.int32); m = rng.integers(1 << 30, 1 << 31, O).astype(np.int32)
    s = rng.integers(-14, 2, O).astype(np.int32); zi, zo = (int(v) for v in rng.integers(-128, 128, 2))
    lo = int(rng.integers(-128, 0)); hi = int(rng.integers(lo, 128))
    g = np.array([H, W, Cc, oh, ow, O, k, k, st, st, ph, pw, -zi, zo, lo, hi], np.int32)
    out = np.zeros((oh, ow, O), np.int8)
    lib.run_conv(P(g), 0, P(xi), P(w), P(b), P(m), P(s), P(out))
    fails += not np.array_equal(out, conv_ref(xi, w, b, m, s, zi, zo, lo, hi, st, ph, pw, oh, ow))
print("conv (random params): fail = %d / %d" % (fails, runs))
```

```text
requant: mismatches = 0 / 200000
conv (random params): fail = 0 / 270
```

출력에서 볼 것: requant 20만 개(shift −31..+3), 무작위 파라미터 conv 270개(30개는 출력 크기가 0 이하라 건너뜀) 모두 일치. numpy reference는 C 구현과 **다른 방법**(부호·절댓값 분리 반올림, 패딩 버퍼를 zp_in으로 채우고 tensordot)으로 같은 규격을 구현했다 — 같은 코드를 두 번 쓰면 같은 버그가 두 번 들어가므로 차등 테스트가 의미 없어진다.

### 9.4 mutation testing — 테스트가 버그를 잡는지 시험한다

"테스트가 다 통과한다"는 것은 테스트가 약하다는 뜻일 수도 있다. 커널에 그럴듯한 버그를 하나씩 넣은 라이브러리(`-DBUG=n`)를 만들어 두 층을 다시 돌렸다.

```c
/* j3k.h / j3k_ref.c 에 넣은 버그 스위치 (발췌) */
#if BUG == 2
    int64_t nudge = 1 << 30;                                  /* 버그: 음수에도 같은 nudge */
#else
    int64_t nudge = ab >= 0 ? (1 << 30) : (1 - (1 << 30));
#endif
/* BUG 1: rdbpot() 안에서  return x >> e;   (반올림 없는 shift)
   BUG 3: conv 출력에서   clamp_s8(acc, -128, 127)  (fused ReLU 범위 무시)
   BUG 4: conv 누산기를   int16_t acc  (16-bit 누산 → wrap) */
```

```sh
for b in 1 2 3 4; do cc -std=c11 -Wall -Wextra -O2 -shared -fPIC -DBUG=$b wrap.c j3k_ref.c j3k_more.c -o libbug$b.dylib; done
for b in 1 2 3 4; do J3LIB=./libbug$b.dylib .venv-tf/bin/python fuzz.py 50 conv; J3LIB=./libbug$b.dylib .venv/bin/python level1.py; done
```

```text
== BUG=1
conv  cases=50  pass=5  fail=45  outputs compared=6734  ops=['CONV_2D']
requant: mismatches = 85463 / 200000
conv (random params): fail = 173 / 270
== BUG=2
conv  cases=50  pass=50  fail=0  outputs compared=6734  ops=['CONV_2D']
requant: mismatches = 14101 / 200000
conv (random params): fail = 8 / 270
== BUG=3
conv  cases=50  pass=50  fail=0  outputs compared=6734  ops=['CONV_2D']
requant: mismatches = 0 / 200000
conv (random params): fail = 262 / 270
== BUG=4
conv  cases=50  pass=2  fail=48  outputs compared=6734  ops=['CONV_2D']
requant: mismatches = 0 / 200000
conv (random params): fail = 190 / 270
```

| 버그 | 층 A (TFLite, conv 50) | 층 B requant 20만 | 층 B conv 270 | 해석 |
|---|---|---|---|---|
| 1. shift를 반올림 없이 `>>` | 45 fail | 85,463 | 173 | 누구나 잡는다 |
| 2. SRDHM 음수 nudge 틀림 | **0 fail** | 14,101 | 8 | 1 LSB 오차가 다음 RDBPOT(÷2¹⁰ 근처)에 거의 흡수돼 conv 출력에선 드물게만 드러남 |
| 3. fused ReLU clamp 무시 | **0 fail** | 0 | 262 | 변환기 파라미터에선 clamp가 no-op (1.3절) |
| 4. int16 누산 | 48 fail | 0 | 190 | 누산 범위 버그는 큰 채널·극단값에서 바로 드러남 |

출력에서 볼 것: **TFLite 기준 fuzz(층 A)만 있었다면 BUG 2와 BUG 3을 통과시켰다.** BUG 2는 단위 테스트(requant 단독)에서는 7%나 틀리는데, conv 전체로 보면 무작위 파라미터 270 케이스 중 8개, TFLite 파라미터 50 케이스 중 0개로 희석된다. BUG 3은 변환기가 만든 파라미터에서는 영원히 드러나지 않지만, 다른 변환기·다른 그래프(예: ReLU 뒤 텐서를 다른 op도 공유해서 출력 범위가 넓게 잡힌 경우)에서는 바로 틀린 결과를 낸다. 교훈:

1. **primitive(requant)는 따로, 아주 많이** 테스트한다 — 희석되기 전에 잡는다.
2. **파라미터 공간을 직접 퍼징**한다 — "현실적인" 파라미터만으로는 커버리지가 안 나온다.
3. mutation test로 **테스트의 검출력을 숫자로** 확인한다. 펌웨어로 치면 fault injection으로 에러 처리 경로를 검증하는 것과 같다.

### 9.5 엣지 케이스 체크리스트 (harness에 반드시 들어갈 것)

| 범주 | 케이스 | 왜 |
|---|---|---|
| 값 | 입력 전부 −128 / 전부 127 / 체커 | 누산기 최대·최소, offset 후 −256 |
| 값 | weight ±127, bias ±큰 값 | 누산 범위, int16 경로 오버플로 |
| zp | zp_in = −128, 127 | `x + off`의 끝값 (0 또는 −255 / +255) |
| 패딩 | SAME + stride 2 + 짝수/홀수 입력 | 앞뒤 비대칭 패딩 |
| 크기 | H·W = 1, 커널 > 입력(SAME), C = 1, C % 16 ≠ 0 | SIMD tail, 경계만 있는 출력 |
| requant | shift > 0, shift = −31, M0 = 2³⁰, 2³¹−1 | 왼쪽 shift, 포화, 극단 multiplier |
| clamp | act 범위가 int8보다 좁은 경우 | 변환기가 안 만드는 조합 (BUG 3) |
| 평균 | 음수 합, .5 동점, SAME 모서리(유효 칸 < k²) | 반올림 방향, 나누는 수 |

---

## 10. 면접 코딩 드릴 5개 — 풀이와 검증

면접에서 "화이트보드 15~20분"으로 나오는 형태다. 각 드릴은 문제 → 생각할 점 → 풀이 코드 → 실제 출력 순서다. 풀이는 전부 위 커널(TFLite와 bit-exact로 검증된 것)이나 수학적 기준과 비교해 확인했다.

### 10.1 드릴 1 — "int8 내적을 포화 포함으로 구현하라"

생각할 점: 질문이 모호하면 **먼저 되묻는다** — 누산기 폭은? 포화는 매 단계인가, 끝에서 한 번인가? int8×int8은 최대 16,384(= (−128)²)이라 int32 누산은 13만 항까지 안전하다 (C1 7.2절). DSP 스타일 int16 포화 누산(QADD16류)은 **덧셈 순서에 따라 결과가 달라진다** — 포화는 결합법칙을 깬다.

```c
/* 드릴 1: int8 내적 — (a) int32 정확 누산 + 결과 포화, (b) DSP식 int16 포화 누산 */
#include <stdint.h>
#include <stdio.h>
int32_t dot_s8_sat32(const int8_t *a, const int8_t *b, int n) {
    int64_t acc = 0;                                   /* 2^31 넘어도 안전하게 넓게 누산 */
    for (int i = 0; i < n; i++) acc += (int32_t)a[i] * b[i];
    return acc > INT32_MAX ? INT32_MAX : acc < INT32_MIN ? INT32_MIN : (int32_t)acc;
}
int16_t dot_s8_sat16(const int8_t *a, const int8_t *b, int n) {
    int32_t acc = 0;                                   /* 매 단계 int16으로 포화 (QADD16 스타일) */
    for (int i = 0; i < n; i++) {
        acc += (int32_t)a[i] * b[i];
        acc = acc > INT16_MAX ? INT16_MAX : acc < INT16_MIN ? INT16_MIN : acc;
    }
    return (int16_t)acc;
}
int main(void) {
    int8_t m128[4] = {-128, -128, -128, -128}, p127[4] = {127, 127, 127, 127};
    int8_t up[4] = {127, 127, 127, -128}, dn[4] = {127, 127, 127, 127};
    printf("(-128)·(-128) x4 : sat32=%d  sat16=%d\n", dot_s8_sat32(m128, m128, 4), dot_s8_sat16(m128, m128, 4));
    printf("(-128)·127   x4 : sat32=%d  sat16=%d\n", dot_s8_sat32(m128, p127, 4), dot_s8_sat16(m128, p127, 4));
    printf("up then down    : sat32=%d  sat16=%d  (exact = %d)\n",
           dot_s8_sat32(up, dn, 4), dot_s8_sat16(up, dn, 4), 3 * 127 * 127 - 128 * 127);
    static int8_t big[200000];
    for (int i = 0; i < 200000; i++) big[i] = -128;
    printf("n=200000, all -128: sat32=%d (exact %lld)\n", dot_s8_sat32(big, big, 200000), 200000LL * 16384);
    return 0;
}
```

```text
(-128)·(-128) x4 : sat32=65536  sat16=32767
(-128)·127   x4 : sat32=-65024  sat16=-32768
up then down    : sat32=32131  sat16=16511  (exact = 32131)
n=200000, all -128: sat32=2147483647 (exact 3276800000)
```

출력에서 볼 것: "up then down"의 정답 32,131은 int16 범위 안인데 매 단계 포화는 16,511을 낸다 — 중간에 32,767에서 잘린 뒤 −16,256이 더해졌기 때문이다. **최종값이 범위 안이어도 중간 포화가 결과를 바꾼다.** 그래서 NN 커널은 넓은 누산기(int32)로 정확히 더하고 **마지막에 한 번** 포화한다. 20만 항(2³¹ 초과)에서만 int32 포화가 실제로 일어난다.

### 10.2 드릴 2 — "int32 누산값을 multiplier/shift로 int8로 requantize하라"

생각할 점: (1) 실수 M을 (M0, shift)로 분해하는 함수, (2) 64-bit 곱 + 반올림 + 시프트, (3) zp 더하고 clamp. 면접에서 한 걸음 더 가는 포인트: **TFLite는 두 번 반올림한다**(SRDHM 후 RDBPOT). 수학적으로 정확한 "한 번 반올림"과 얼마나 다른지 재 본다.

```c
/* 드릴 2: requantize int32 → int8. M(실수) → (M0, shift) 분해 + TFLite식 두 번 반올림 vs 정확한 한 번 반올림 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "j3k.h"
static void quantize_multiplier(double M, int32_t *m0, int *shift) {
    if (M == 0) { *m0 = 0; *shift = 0; return; }
    double f = frexp(M, shift);                              /* M = f·2^shift, f ∈ [0.5,1) */
    int64_t q = (int64_t)round(f * (1ll << 31));
    if (q == (1ll << 31)) { q /= 2; (*shift)++; }
    *m0 = (int32_t)q;
}
static int8_t requant_tflite(int32_t acc, int32_t m0, int shift, int32_t zp) {
    return clamp_s8(mbqm(acc, m0, shift) + zp, -128, 127);
}
static int8_t requant_single(int32_t acc, int32_t m0, int shift, int32_t zp) {  /* round(acc·m0 / 2^(31-shift)) 한 번 */
    int total = 31 - shift; __int128 p = (__int128)acc * m0, h = (__int128)1 << (total - 1);
    int64_t r = (int64_t)(p >= 0 ? (p + h) >> total : -((-p + h) >> total));
    return clamp_s8((int32_t)r + zp, -128, 127);
}
int main(void) {
    int32_t m0; int sh; quantize_multiplier(0.0072, &m0, &sh);
    printf("M=0.0072 -> M0=%d shift=%d ; acc=12345 -> %d (float: %.3f)\n", m0, sh,
           requant_tflite(12345, m0, sh, 0), 12345 * 0.0072);
    srand(5);
    for (int k = 1; k <= 13; k += 3) {                     /* M ∈ [0.5,1)·2^-k  →  shift = -k */
        long diff = 0, n = 400000;
        for (long i = 0; i < n; i++) {
            double M = ldexp(0.5 + rand() / (2.0 * RAND_MAX), -k);
            quantize_multiplier(M, &m0, &sh);
            int32_t acc = (int32_t)((rand() / (double)RAND_MAX * 255.0 - 128.0) / M);  /* 출력이 int8 범위 안 */
            diff += requant_tflite(acc, m0, sh, 0) != requant_single(acc, m0, sh, 0);
        }
        printf("shift=%3d : double vs single rounding differ %6ld / %ld (%.4f%%)  ~2^-(k+1)=%.4f%%\n",
               sh, diff, n, 100.0 * diff / n, 100.0 * ldexp(1, -(k + 1)));
    }
    return 0;
}
```

```text
M=0.0072 -> M0=1979120930 shift=-7 ; acc=12345 -> 89 (float: 88.884)
shift= -1 : double vs single rounding differ 101157 / 400000 (25.2892%)  ~2^-(k+1)=25.0000%
shift= -4 : double vs single rounding differ  12631 / 400000 (3.1578%)  ~2^-(k+1)=3.1250%
shift= -7 : double vs single rounding differ   1479 / 400000 (0.3698%)  ~2^-(k+1)=0.3906%
shift=-10 : double vs single rounding differ    193 / 400000 (0.0483%)  ~2^-(k+1)=0.0488%
shift=-13 : double vs single rounding differ     27 / 400000 (0.0067%)  ~2^-(k+1)=0.0061%
```

출력에서 볼 것: C1의 손계산 예(M = 0.0072, acc = 12345 → 89)가 그대로 나온다. 그리고 두 번 반올림은 **shift가 작을수록 자주** 한 번 반올림과 다르다. 이유는 이렇다: SRDHM 결과는 `v·2^k`(v = 실수 정답)를 정수로 반올림한 값이라, 마지막 단계에서 보면 v에 최대 0.5·2⁻ᵏ의 오차가 이미 붙어 있다. v의 소수부가 0.5 바로 아래 `[0.5 − 2⁻⁽ᵏ⁺¹⁾, 0.5)`에 있으면 첫 반올림이 정확히 .5로 올려 버리고, 두 번째 반올림이 다시 올려서 1 차이가 난다. 그 구간 길이가 2⁻⁽ᵏ⁺¹⁾이니 차이 비율도 2⁻⁽ᵏ⁺¹⁾ — 측정과 일치한다. 실제 conv 층의 shift는 보통 −7 ~ −12라 0.4% ~ 0.01% 수준이다. **"정확한 반올림이 더 좋은데 왜 안 쓰나?" → 기준과 bit-exact가 목표면 기준의 반올림을 따라야 한다**가 답이다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 320"> <text x="10" y="22" font-size="14">TFLite식 두 번 반올림 vs 정확한 한 번 반올림 — 출력이 달라지는 비율</text> <line x1="80" y1="270.0" x2="640" y2="270.0" stroke="currentColor" stroke-opacity="0.15"/> <text x="74" y="274.0" font-size="11" text-anchor="end">0.001%</text> <line x1="80" y1="226.0" x2="640" y2="226.0" stroke="currentColor" stroke-opacity="0.15"/> <text x="74" y="230.0" font-size="11" text-anchor="end">0.01%</text> <line x1="80" y1="182.0" x2="640" y2="182.0" stroke="currentColor" stroke-opacity="0.15"/> <text x="74" y="186.0" font-size="11" text-anchor="end">0.1%</text> <line x1="80" y1="138.0" x2="640" y2="138.0" stroke="currentColor" stroke-opacity="0.15"/> <text x="74" y="142.0" font-size="11" text-anchor="end">1%</text> <line x1="80" y1="94.0" x2="640" y2="94.0" stroke="currentColor" stroke-opacity="0.15"/> <text x="74" y="98.0" font-size="11" text-anchor="end">10%</text> <line x1="80" y1="50.0" x2="640" y2="50.0" stroke="currentColor" stroke-opacity="0.15"/> <text x="74" y="54.0" font-size="11" text-anchor="end">100%</text> <text x="80.0" y="286" font-size="11" text-anchor="middle">0</text> <text x="160.0" y="286" font-size="11" text-anchor="middle">-2</text> <text x="240.0" y="286" font-size="11" text-anchor="middle">-4</text> <text x="320.0" y="286" font-size="11" text-anchor="middle">-6</text>
<text x="400.0" y="286" font-size="11" text-anchor="middle">-8</text> <text x="480.0" y="286" font-size="11" text-anchor="middle">-10</text> <text x="560.0" y="286" font-size="11" text-anchor="middle">-12</text> <text x="640.0" y="286" font-size="11" text-anchor="middle">-14</text> <line x1="80" y1="270" x2="640" y2="270" stroke="currentColor"/><line x1="80" y1="50" x2="80" y2="270" stroke="currentColor"/> <polyline points="80.0,63.2 100.0,69.9 120.0,76.5 140.0,83.1 160.0,89.7 180.0,96.4 200.0,103.0 220.0,109.6 240.0,116.2 260.0,122.8 280.0,129.5 300.0,136.1 320.0,142.7 340.0,149.3 360.0,156.0 380.0,162.6 400.0,169.2 420.0,175.8 440.0,182.5 460.0,189.1 480.0,195.7 500.0,202.3 520.0,208.9 540.0,215.6 560.0,222.2 580.0,228.8 600.0,235.4 620.0,242.1 640.0,248.7" fill="none" stroke="#4a7bd0" stroke-width="2"/> <circle cx="120.0" cy="76.3" r="5" fill="#e08a3c"/> <text x="128.0" y="70.3" font-size="11">25.2892%</text> <circle cx="240.0" cy="116.0" r="5" fill="#e08a3c"/> <text x="248.0" y="110.0" font-size="11">3.1578%</text> <circle cx="360.0" cy="157.0" r="5" fill="#e08a3c"/> <text x="368.0" y="151.0" font-size="11">0.3698%</text> <circle cx="480.0" cy="195.9" r="5" fill="#e08a3c"/> <text x="488.0" y="189.9" font-size="11">0.0483%</text> <circle cx="600.0" cy="233.7" r="5" fill="#e08a3c"/> <text x="608.0" y="227.7" font-size="11">0.0067%</text>
<text x="440.0" y="145.8" font-size="12">파랑 선: 이론 2^−(k+1)</text> <text x="440.0" y="161.8" font-size="12">주황 점: 실측 (각 400,000개)</text> <text x="640" y="306" font-size="12" text-anchor="end">shift (= −k, M ≈ 0.5~1 × 2^−k)</text></svg>
```

그림 11 — 두 번 반올림이 한 번 반올림과 달라지는 비율(세로 로그). shift가 1 작아질 때마다 절반으로 준다. 이론선 2⁻⁽ᵏ⁺¹⁾과 실측이 겹친다.

### 10.3 드릴 3 — "int8 average pool을 반올림까지 맞게 구현하라"

생각할 점: 입출력 scale이 같으니 requant는 없다. 남는 건 **음수 평균의 반올림**과 **나누는 수**(SAME 모서리에서 유효 칸 수). C의 `/`는 0 쪽 절삭, `>>`는 −∞ 쪽 버림, TFLite는 0에서 먼 쪽 반올림 — 셋이 음수에서 갈린다.

```c
/* 드릴 3: int8 average pool 2x2/stride 2 (HWC, VALID), 반올림 = 0에서 먼 쪽 (TFLite 규칙) */
#include <stdio.h>
#include <stdlib.h>
#include "j3k.h"
void avgpool_s8(const pool_params_t *, dims_t, const int8_t *, dims_t, int8_t *);
static int32_t div_round_away(int32_t s, int32_t n) { return s >= 0 ? (s + n / 2) / n : (s - n / 2) / n; }
void avgpool2x2_s8(const int8_t *x, int H, int W, int C, int8_t *y) {
    for (int oy = 0; oy < H / 2; oy++) for (int ox = 0; ox < W / 2; ox++) for (int c = 0; c < C; c++) {
        const int8_t *p = &x[((2 * oy) * W + 2 * ox) * C + c];
        int32_t s = p[0] + p[C] + p[W * C] + p[W * C + C];
        y[(oy * (W / 2) + ox) * C + c] = (int8_t)div_round_away(s, 4);   /* |평균| ≤ 128 → 범위 안 */
    }
}
int main(void) {
    printf(" sum  exact   C '/'  >>2(floor)  away\n");
    int sums[] = {6, -6, 5, -5, -2, -512};
    for (int i = 0; i < 6; i++)
        printf("%4d  %6.2f  %5d  %9d  %4d\n", sums[i], sums[i] / 4.0, sums[i] / 4, sums[i] >> 2,
               div_round_away(sums[i], 4));
    srand(6); int bad = 0, total = 0;
    for (int t = 0; t < 500; t++) {
        int H = 2 * (1 + rand() % 8), W = 2 * (1 + rand() % 8), C = 1 + rand() % 16;
        int8_t x[16 * 16 * 16], y0[8 * 8 * 16], y1[8 * 8 * 16];
        for (int i = 0; i < H * W * C; i++) x[i] = (int8_t)(t % 5 ? rand() % 256 - 128 : -128);
        pool_params_t p = {2, 2, 0, 0, 2, 2, -128, 127};
        dims_t in = {1, H, W, C}, out = {1, H / 2, W / 2, C};
        avgpool_s8(&p, in, x, out, y0); avgpool2x2_s8(x, H, W, C, y1);
        for (int i = 0; i < H / 2 * W / 2 * C; i++) { bad += y0[i] != y1[i]; total++; }
    }
    printf("vs TFLite-verified avgpool_s8: %d outputs, mismatches = %d\n", total, bad);
    return 0;
}
```

```text
 sum  exact   C '/'  >>2(floor)  away
   6    1.50      1          1     2
  -6   -1.50     -1         -2    -2
   5    1.25      1          1     1
  -5   -1.25     -1         -2    -1
  -2   -0.50      0         -1    -1
-512  -128.00   -128       -128  -128
vs TFLite-verified avgpool_s8: 89381 outputs, mismatches = 0
```

출력에서 볼 것: 양수 동점(6 → 1.5)에서는 C `/`와 `>>`가 같이 틀리고(1), 음수 비동점(−5 → −1.25)에서는 `>>`만 틀린다(−2). 세 방식이 모두 일치하는 건 정확히 나누어떨어질 때뿐이다. 5개 중 1개 테스트는 입력을 전부 −128로 둬서 −512/4 = −128 경계도 확인했다. `avgpool_s8`(9절에서 TFLite와 200 케이스 일치)과 89,381개 전부 같다.

### 10.4 드릴 4 — "scale이 다른 int8 두 텐서를 더하라"

생각할 점: 가장 먼저 떠오르는 풀이는 "각 입력을 출력 scale로 requant해서 더한다"다. 정수 연산이고 그럴듯하지만 **반올림이 두 번 더해진다**. 5절의 TFLite 방식(2²⁰ 확대 후 한 번 반올림)과 비교해 본다. 실수 정답은 `round((s1(a−z1) + s2(b−z2))/so) + zo`.

```c
/* 드릴 4: scale이 다른 int8 두 텐서 더하기 — (1) 각자 requant 후 합, (2) TFLite식 left_shift=20 공통 scale */
#include <math.h>
#include <stdio.h>
#include "j3k.h"
void add_s8(const add_params_t *, int, const int8_t *, const int8_t *, int8_t *);
static void qm(double M, int32_t *m0, int32_t *sh) {
    int e; double f = frexp(M, &e); int64_t q = (int64_t)round(f * (1ll << 31));
    if (q == (1ll << 31)) { q /= 2; e++; } *m0 = (int32_t)q; *sh = e;
}
int main(void) {
    const double s1 = 0.02198872, s2 = 0.02513492, so = 0.02469467;   /* 2절 모델의 실제 값 */
    const int z1 = -128, z2 = -68, zo = -72;
    int32_t m1, sh1, m2, sh2; qm(s1 / so, &m1, &sh1); qm(s2 / so, &m2, &sh2);
    double tw = 2 * fmax(s1, s2); add_params_t p = {-z1, -z2, zo, 20, 0, 0, 0, 0, 0, 0, -128, 127};
    qm(s1 / tw, &p.in1_mult, &p.in1_shift); qm(s2 / tw, &p.in2_mult, &p.in2_shift);
    qm(tw / ((1 << 20) * so), &p.out_mult, &p.out_shift);
    long bad_naive = 0, bad_tfl = 0, naive_vs_tfl = 0;
    for (int a = -128; a < 128; a++) for (int b = -128; b < 128; b++) {
        double real = s1 * (a - z1) + s2 * (b - z2);                 /* 실수 정답 */
        int8_t ref = clamp_s8((int32_t)lround(real / so) + zo, -128, 127);
        int8_t naive = clamp_s8(zo + mbqm(a - z1, m1, sh1) + mbqm(b - z2, m2, sh2), -128, 127);
        int8_t ia = (int8_t)a, ib = (int8_t)b, tfl; add_s8(&p, 1, &ia, &ib, &tfl);
        bad_naive += naive != ref; bad_tfl += tfl != ref; naive_vs_tfl += naive != tfl;
    }
    printf("M1=s1/so=%.4f (M0=%d,sh=%d)  M2=s2/so=%.4f (M0=%d,sh=%d)\n", s1 / so, m1, sh1, s2 / so, m2, sh2);
    printf("65536 pairs: naive vs float-ref mismatches=%ld, TFLite-add vs float-ref=%ld, naive vs TFLite=%ld\n",
           bad_naive, bad_tfl, naive_vs_tfl);
    return 0;
}
```

```text
M1=s1/so=0.8904 (M0=1912170385,sh=0)  M2=s2/so=1.0178 (M0=1092884207,sh=1)
65536 pairs: naive vs float-ref mismatches=8953, TFLite-add vs float-ref=0, naive vs TFLite=8953
```

출력에서 볼 것: 가능한 **모든 입력 쌍 65,536개**를 다 돌렸다 (int8 이항 연산은 전수 검사가 가능하다 — 면접에서 "어떻게 검증하겠나?"의 최고의 답). 순진한 방법은 13.7%(8,953개)가 실수 정답과 1 LSB 다르고, TFLite 방식은 0개다. M2 = 1.0178 > 1이라 shift = +1(왼쪽 시프트)이 나오는 것도 볼 것 — `mbqm`이 shift > 0을 처리해야 하는 이유다.

### 10.5 드릴 5 — "3×3 depthwise conv를 작성하라"

생각할 점: (1) NHWC 인덱싱, (2) SAME 패딩 앞쪽 = floor(total/2), (3) 패딩은 건너뛰기(= 실수 0), (4) 채널마다 multiplier·shift, (5) 출력 크기 `ceil(H/s)`. 15분 안에 쓰는 버전이므로 파라미터를 평탄하게 받는다.

```c
/* 드릴 5: 3x3 depthwise conv int8 (NHWC, multiplier 1, stride s, SAME 패딩) — 면접에서 15분 안에 쓰는 버전 */
#include <stdio.h>
#include <stdlib.h>
#include "j3k.h"
void dwconv_s8_ref(const conv_params_t *, const per_ch_quant_t *, dims_t, const int8_t *, dims_t,
                   const int8_t *, const int32_t *, dims_t, int8_t *);
void dw3x3_s8(const int8_t *x, int H, int W, int C, int s, const int8_t *w /*[3][3][C]*/,
              const int32_t *bias, const int32_t *m0, const int32_t *sh, int zp_in, int zp_out,
              int8_t *y, int OH, int OW) {
    int pad_t = ((OH - 1) * s + 3 - H) / 2, pad_l = ((OW - 1) * s + 3 - W) / 2;  /* SAME: 앞쪽 = floor(total/2) */
    if (pad_t < 0) pad_t = 0;
    if (pad_l < 0) pad_l = 0;
    for (int oy = 0; oy < OH; oy++) for (int ox = 0; ox < OW; ox++) for (int c = 0; c < C; c++) {
        int32_t acc = bias[c];
        for (int ky = 0; ky < 3; ky++) for (int kx = 0; kx < 3; kx++) {
            int iy = oy * s - pad_t + ky, ix = ox * s - pad_l + kx;
            if (iy < 0 || iy >= H || ix < 0 || ix >= W) continue;       /* 패딩은 '실수 0' = zp_in */
            acc += w[(ky * 3 + kx) * C + c] * (x[(iy * W + ix) * C + c] - zp_in);
        }
        y[(oy * OW + ox) * C + c] = clamp_s8(mbqm(acc, m0[c], sh[c]) + zp_out, -128, 127);
    }
}
int main(void) {
    static int8_t x[20 * 20 * 16], w[9 * 16], y0[20 * 20 * 16], y1[20 * 20 * 16];
    int32_t b[16], m[16], sh[16]; int bad = 0, total = 0; srand(7);
    for (int t = 0; t < 1000; t++) {
        int H = 1 + rand() % 20, W = 1 + rand() % 20, C = 1 + rand() % 16, s = 1 + rand() % 2;
        int OH = (H + s - 1) / s, OW = (W + s - 1) / s, zi = rand() % 256 - 128, zo = rand() % 256 - 128;
        for (int i = 0; i < H * W * C; i++) x[i] = (int8_t)(rand() % 256 - 128);
        for (int i = 0; i < 9 * C; i++) w[i] = (int8_t)(rand() % 255 - 127);
        for (int c = 0; c < C; c++) { b[c] = rand() % 4001 - 2000; m[c] = (1 << 30) + rand() % (1 << 30); sh[c] = -(5 + rand() % 6); }
        dw3x3_s8(x, H, W, C, s, w, b, m, sh, zi, zo, y1, OH, OW);
        int pt = ((OH - 1) * s + 3 - H) / 2, pl = ((OW - 1) * s + 3 - W) / 2;
        conv_params_t p = {s, s, pt > 0 ? pt : 0, pl > 0 ? pl : 0, -zi, zo, -128, 127}; per_ch_quant_t q = {m, sh};
        dims_t in = {1, H, W, C}, f = {1, 3, 3, C}, out = {1, OH, OW, C};
        dwconv_s8_ref(&p, &q, in, x, f, w, b, out, y0);
        for (int i = 0; i < OH * OW * C; i++) { bad += y0[i] != y1[i]; total++; }
    }
    printf("dw3x3 vs TFLite-verified ref: 1000 shapes, %d outputs, mismatches = %d\n", total, bad);
    return 0;
}
```

```text
dw3x3 vs TFLite-verified ref: 1000 shapes, 579327 outputs, mismatches = 0
```

출력에서 볼 것: 1×1 입력(커널보다 작다), 홀수·짝수 크기, stride 2의 비대칭 패딩을 포함한 1,000개 shape, 57만 개 출력이 일치. 면접에서는 코드를 다 쓴 뒤 **"이 함수를 어떻게 검증하겠습니까"**에 9절의 두 층 harness와 엣지 케이스 표로 답하면 된다. 후속 질문 대비: "더 빠르게?" → 미리 패딩 + offset을 bias로 접기 + 채널 방향 벡터화(4.1절, 10.7배), "MCU에서?" → 채널 4개씩 SXTB16 + SMLAD, 또는 Helium으로 채널 16개씩.

---

## 11. 임베디드 관점에서 다시 보기

이 노트의 실험을 MCU·DSP 배포 결정으로 옮기면 다음과 같다.

| 질문 | 이 노트의 숫자 | 펌웨어 결정 |
|---|---|---|
| 커널 하나에 scratch를 얼마나 줄까 | 전체 im2col 576 KB vs 부분 im2col 2.3 KB, 속도 같음 (7.5절) | TFLM의 scratch buffer 요청은 "부분 im2col 크기"로. arena 예산(K1)에 직접 들어간다 |
| depthwise가 왜 느린가 | 같은 크기 conv의 7% 속도 (4.1절) | DS-CNN·MobileNet 계열은 MAC 수보다 depthwise 시간이 지배. 프로파일은 op별로 |
| M4에서 이 층이 되나 | 약 1.5 사이클/MAC 추정 → 37.7 M MAC = 0.57 초 @ 100 MHz (8.1절) | always-on M4라면 층 크기를 1/10 이하로 줄이거나 M55/NPU로 |
| TCM에 안 들어가면 | 64 KB, 이중 버퍼면 출력 3행 띠, 입력 1.67배 재전송 (7.6절) | DMA 핑퐁(J1) + 띠 타일링. 대역폭 예산(D2·D3)에 1.67배를 반영 |
| 최적화 커널을 믿어도 되나 | 1,000 케이스 bit-exact + mutation 4종 (9절) | CI에 층 A·B를 넣고, 새 커널·새 컴파일러 버전마다 돌린다 (J6, O) |
| bit-exact를 요구할 op, tolerance로 볼 op | conv·dw·pool·add bit-exact, softmax ±1 LSB (6.4절) | 합격 기준표를 op별로 문서화 (C8) |

전체 그림을 한 문장으로: **커널 = 규약 해석(파라미터) + 데이터 배치(레이아웃·im2col·패딩) + 레지스터 배분(블로킹) + 명령 선택(SIMD) + 검증(차등·mutation)**. 마지막 하나가 빠지면 나머지는 의미가 없다.

Hark 같은 웨어러블(추정)에 대입해 보면: always-on 쪽 wake word·착용 감지 모델은 CMSIS-NN 같은 검증된 라이브러리를 쓰는 것이 기본이고, 직접 커널을 짜는 경우는 (1) 라이브러리에 없는 op(특수 activation, 커스텀 전처리 융합), (2) 특정 shape에서 병목이 확인된 경우, (3) 새 DSP/가속기 bring-up에서 reference가 필요할 때다. 세 경우 모두 이 노트의 순서 — **reference → oracle과 bit-exact → 최적화 → 재검증** — 를 그대로 따른다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| SAME 패딩을 대칭(앞 = k/2)으로 가정 | stride 2 층에서 대부분 틀림, 큰 차이(최대 115) | 앞쪽 = floor(total/2), 뒤가 1 더 많을 수 있음 | 1.4절 식으로 앞쪽 패딩 계산 |
| bias 접기 + 패딩 탭 건너뛰기 | **경계 출력만** 틀림 (172/288), 내부는 정확 | 접힌 bias에 패딩 탭의 offset·w가 남음 | 패딩 칸을 zp_in으로 채워 계산 |
| input_offset 부호 반대 (+zp_in) | 거의 전부 틀림, 출력이 한쪽으로 쏠림 | offset = −zp_in 규약 | 이름에 부호 규약을 박고 2×2 손계산으로 확인 |
| shift를 `>>`로만 | 약 절반이 1 LSB 차이, 음수에서 bias | RDBPOT는 0에서 먼 쪽 반올림 | rdbpot 구현, requant 단독 20만 개 테스트 |
| SRDHM 음수 nudge 실수 | conv 테스트는 거의 통과, 가끔 1 LSB | 오차가 후단 shift에 희석 | requant primitive를 따로 대량 테스트 (9.4절) |
| multiplier를 float32로 계산 / Python round | 특정 채널만 가끔 1 LSB | M0 끝자리 차이 | double + `std::round` 규칙 |
| clamp 범위 무시 | 변환기 모델에선 통과, 다른 그래프에서 틀림 | 변환기는 clamp가 no-op인 범위를 만듦 | 파라미터 공간 퍼징 (층 B) |
| add를 입력별 requant 후 합 | 13.7%가 1 LSB 틀림 | 반올림 두 번 누적 | left_shift 20 + 한 번 반올림 (5절) |
| avg pool을 C `/`나 `>>`로 | 음수·동점에서 1 LSB | 반올림 방향 | 0에서 먼 쪽, 유효 칸 수로 나누기 |
| M4 커널 블록 과대 / 루프 펼침 | 기대보다 느림, asm에 `Spill/Reload` | 범용 레지스터 13개 남짓 | 블록 축소, unroll 끄기, 포인터 증가형 루프 |
| offset을 MAC 루프에 둔 채 SIMD 기대 | 컴파일러가 smlal(16-bit)만 생성 | `x + off`가 int8 범위 밖 | offset을 bias로 접어 int8×int8 유지 |

---

## 13. 면접에서 이렇게 말한다

**Q.** Walk me through how you would implement an int8 convolution kernel that matches TFLite exactly.

**A.** 먼저 계약을 고정한다: NHWC 입력, OHWI per-channel 대칭 weight, int32 bias, 채널별 (M0, shift), 앞쪽 패딩, clamp 범위를 params 구조체로. reference 루프는 `acc += w·(x − zp_in)`, 패딩은 건너뛰고, bias 더하고, SRDHM + RDBPOT 두 번 반올림, zp_out 더하고 clamp. 그 다음 TFLite BUILTIN_REF 인터프리터에서 중간 텐서를 뽑아 C 헤더로 만들고 원소 단위로 0 mismatch를 확인한다. 실제로 해 보니 conv·depthwise·add·pool 2,800개가 전부 일치했고, 처음 틀렸던 건 SAME 패딩 앞쪽 계산 하나였다.

> I'd pin down the contract first — NHWC activations, OHWI per-channel symmetric weights, int32 bias, a per-channel multiplier and shift, front padding, and the activation clamp — in a params struct. The reference loop accumulates w times (x minus the input zero point), skips padded taps, adds bias, applies the two-step rounding of SaturatingRoundingDoublingHighMul plus RoundingDivideByPOT, adds the output zero point and clamps. Then I dump intermediate tensors from the TFLite reference interpreter into a C header and require zero mismatches. When I did this, conv, depthwise, add and both pools matched bit-exactly; the only early failure was SAME padding, where the front pad is floor of total over two.

**Q.** Why fold the input zero-point into the bias, and what's the catch?

**A.** `Σ w·(x + off) = Σ w·x + off·Σw`이고 뒤 항은 weight만의 함수라 미리 계산할 수 있다. 그러면 MAC 루프가 순수 int8×int8이 되어 SDOT·VMLADAV.S8을 쓸 수 있다 — offset을 루프에 두면 피연산자가 9-bit가 돼 컴파일러가 16-bit smlal로 간다. 함정은 패딩: 접은 bias는 모든 탭의 off·w를 포함하므로 패딩 탭을 건너뛰면 경계 출력이 틀린다(내 실험에서 경계 288개 중 172개). 패딩 칸을 zp_in으로 채워서 계산해야 한다.

> The offset term is a function of the weights only, so folding it into the bias leaves a pure int8 by int8 inner product, which is what SDOT or Helium's VMLADAV want. The catch is padding: the folded bias assumes every tap contributes, so if you skip padded taps the border outputs are wrong — in my test 172 of 288 border outputs differed while the interior was perfect. You have to fill padding with the input zero point, which im2col does naturally.

**Q.** im2col or direct convolution on a microcontroller?

**A.** im2col은 conv를 긴 연속 K 차원의 GEMM으로 바꿔 블로킹·SIMD를 쉽게 하지만, 전체로 만들면 입력의 최대 k²배 메모리다(내 실험 64 KB 입력 → 576 KB). MCU에서는 출력 몇 픽셀분만 펼치는 부분 im2col을 쓴다 — 2.3 KB 버퍼로 속도가 같았다. 채널이 16의 배수로 넉넉하면 NHWC에서는 탭마다 연속 벡터가 이미 있어서 직접 conv도 같은 속도가 나왔다. 채널이 1~3인 첫 층은 im2col이 유리하다. 그래서 라이브러리는 shape별로 커널을 고른다.

> im2col turns convolution into a GEMM with a long contiguous K dimension, which makes blocking and SIMD easy, but a full im2col costs up to k-squared times the input. On an MCU I'd use partial im2col — a couple of output pixels at a time. In my measurements a 2.3 KB partial buffer ran as fast as the 576 KB full one, and with 64 channels a direct NHWC kernel with SDOT matched both. For a first layer with one to three channels, im2col wins, so real libraries dispatch on shape.

**Q.** How would you optimize the inner loop for a Cortex-M4?

**A.** M4에는 int8 SIMD MAC이 없으니 int16 SMLAD를 쓴다. im2col 열을 만들 때 SXTAB16으로 int8 → int16 확장과 offset 덧셈을 한 번에 하고, weight는 SXTB16(+ROR 8)으로 펼친다. 출력채널 2 × 열 2 블록이면 weight 확장 한 번을 두 열에, 열 로드 한 번을 두 채널에 쓴다. 레지스터가 13개 남짓이라 블록을 키우거나 루프를 펼치면 spill이 난다 — 직접 asm을 보며 unroll을 끄고 포인터 증가형 루프로 바꿨더니 MAC 16개당 20명령, spill 0이 됐다. TRM 타이밍으로 약 1.5 사이클/MAC, reference C의 4분의 1 정도다.

> The M4 has no int8 SIMD multiply, so I widen to int16 and use SMLAD. When building the im2col column, SXTAB16 does sign extension and offset addition in one instruction; weights are expanded with SXTB16, with and without a rotate by eight. A two-channel by two-column block reuses each expanded weight twice and each column load twice. Register pressure is the real constraint — about thirteen usable registers — so I check the assembly for spills. Disabling unrolling and using pointer-increment loops got me to 20 instructions per 16 MACs with no spills, roughly 1.5 cycles per MAC by the TRM timings.

**Q.** How do you test a hand-optimized kernel?

**A.** 두 층의 differential testing이다. 층 A는 무작위 shape의 1-op 모델을 변환기로 int8 변환해 TFLite reference를 oracle로 쓴다 — 규약 해석 오류를 잡는다(1,000 케이스 통과). 층 B는 변환기가 만들지 않는 파라미터 — 임의 zp, 좁은 clamp, shift > 0 — 를 직접 퍼징하고 규격에서 따로 쓴 numpy 구현과 비교한다. 그리고 mutation test로 검출력을 확인했다: 층 A만으로는 SRDHM 반올림 버그와 clamp 버그를 놓쳤고, 층 B가 잡았다.

> Two layers of differential testing. Layer A generates random single-op models, converts them, and uses the TFLite reference interpreter as the oracle — that catches contract misreadings; I ran a thousand cases bit-exact. Layer B fuzzes the parameter space the converter never produces — arbitrary zero points, narrow clamps, positive shifts — against an independent numpy implementation written from the spec. Then mutation testing: with only layer A, an SRDHM rounding bug and an ignored clamp both slipped through; layer B caught them. Primitives like requantization also get their own high-volume tests, because a one-LSB error there gets diluted downstream.

**Q.** Your kernel is off by one LSB in a few outputs compared to the reference. Where do you look?

**A.** 순서대로: (1) 틀린 위치 패턴 — 경계에만이면 패딩, 특정 채널이면 그 채널의 multiplier 계산(float32 vs double, 반올림 함수), 전체에 흩어져 있으면 반올림 규칙. (2) requant primitive만 떼어 대량 비교 — SRDHM nudge, RDBPOT 방향, 두 번 반올림 여부(TFLITE_SINGLE_ROUNDING 같은 빌드 옵션). (3) op 고유 규칙 — avg pool 반올림·나누는 수, add의 left_shift. softmax처럼 기준이 bit-exact를 약속하지 않는 op면 실수 대비 ±1 LSB를 기준으로 삼는다.

> I'd look at the pattern first: border-only means padding; a specific channel points at that channel's multiplier — float32 versus double, or the rounding function; scattered single-LSB differences mean a rounding rule. Then I'd isolate the requantization primitive and compare a few hundred thousand values — nudge sign, rounding direction, and whether the reference rounds once or twice. Finally op-specific rules like average-pool rounding or the add's left shift. For ops like softmax where the reference doesn't promise bit-exactness, I'd accept within one LSB of the float result.

---

## 14. 직접 해보기

1. 손계산: 입력 10×10, 커널 3, stride 2, SAME. 출력 크기와 앞·뒤 패딩은? 커널 5, stride 2라면? 정답: k=3 → out 5, total = 4·2+3−10 = 1 → 앞 0, 뒤 1 / k=5 → out 5, total = 8+5−10 = 3 → 앞 1, 뒤 2.
2. 손계산: 2.3절 예제에서 zp_in = −2 대신 zp_in = 5라면 acc와 y는? (M, bias, zp_out 동일) 정답: off = −5 → x+off = [5, −10, −2, −5] → 5 + 20 − 6 − 20 + 7 = 6, 6·0.05 = 0.3 → 0 + 3 = 3.
3. 코드: `conv_s8_ref`에 dilation(팽창) 파라미터를 추가하고, 9.2절 harness에 `dilation_rate=2` Conv2D 케이스를 넣어 TFLite와 bit-exact를 확인하라. 힌트: `iy = oy·stride − pad + ky·dilation`, SAME 패딩 total 식의 k를 `(k−1)·dilation + 1`로.
4. 코드: 7절의 V6에 출력 requant 벡터화를 넣어라 — 4×4 블록 끝의 16개 `mbqm`을 NEON `vqrdmulhq_s32`(SQRDMULH) + `vrshlq_s32`로 바꾸고 mismatches 0과 GMAC/s를 확인하라. 힌트: SQRDMULH는 SRDHM의 벡터판으로 흔히 쓰이지만 동점(.5)에서 +∞ 쪽으로 올리는 것으로 알려져 있어 음수 동점에서 SRDHM(0에서 먼 쪽)과 다를 수 있고, `vrshlq`(반올림 오른쪽 시프트)도 RDBPOT와 음수 동점 처리가 다르다 — 9.3절 requant 테스트로 차이를 먼저 측정하고 필요하면 보정할 것.
5. 코드: 6절 softmax의 행별 나눗셈 depth번을 "역수 한 번 + 곱셈"으로 바꾸고, 6.4절 스트레스 테스트에서 실수 대비 최대 오차가 1 LSB 이내로 유지되는지 확인하라. 힌트: `inv = (256 << 16) / sum`처럼 고정소수점 역수를 만들고 `(lut[d]·inv + 반올림) >> 16`.
6. 설계: 64 KB TCM에서 7.6절의 층을 출력 채널 방향으로도 나눈다면(weight를 32채널씩), R과 재전송 비율은 어떻게 바뀌나? 표를 다시 만들어라. 힌트: weight 18,432 B로 줄지만 입력 띠를 두 번 읽어야 한다 — 입력 재전송 = 2·(R+2)/R.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| NHWC / OHWI | 텐서 레이아웃 | 채널이 가장 빨리 변하는 activation / [출력채널][ky][kx][입력채널] weight |
| input_offset | −zp_in | 커널 안에서 입력에 더해 "실수 0 기준"으로 옮기는 값 |
| bias 접기 (offset folding) | off·Σw를 bias에 미리 더함 | MAC 루프를 int8×int8로 유지. 패딩을 zp_in으로 채워야 정확 |
| SRDHM | SaturatingRoundingDoublingHighMul | `round(a·b/2³¹)`, Q31 곱 + 반올림 + 포화 |
| RDBPOT | RoundingDivideByPOT | `x/2ⁿ`을 0에서 먼 쪽으로 반올림하는 오른쪽 시프트 |
| 두 번 반올림 (double rounding) | SRDHM 후 RDBPOT | TFLite 기본 규칙. 정확한 한 번 반올림과 2⁻⁽ᵏ⁺¹⁾ 비율로 다름 |
| fused activation | op에 붙은 ReLU/ReLU6 | 별도 op 없이 clamp 범위(act_min/max)로 구현 |
| im2col | image to column | conv 창을 행으로 펼쳐 GEMM으로 바꾸는 변환 |
| 부분 im2col | partial im2col | 몇 픽셀분만 펼쳐 바로 GEMM. MCU의 표준 |
| 레지스터 블로킹 | register blocking | 여러 출력을 동시에 계산해 로드한 값을 재사용 |
| SDOT | NEON signed dot product | int8 4쌍의 곱을 int32 lane에 누산 (lane 4개 = 16 MAC) |
| SMLAD / SXTB16 / SXTAB16 | M4 DSP 명령 | int16 2쌍 곱-합 / 바이트 0·2 부호 확장 / 확장 + 덧셈 |
| VMLADAVA | Helium 곱-합-누산 | int8 16쌍 곱의 합을 범용 레지스터에 누산 |
| tail predication | MVE 루프 꼬리 처리 | `dlstp`/`letp`로 남은 원소만 활성 lane |
| spill | 레지스터 넘침 | 값을 스택에 저장했다 다시 읽음 — MCU 커널 성능의 적 |
| halo | 타일 경계의 겹침 | conv 띠 타일링에서 이웃 띠와 공유하는 입력 행 |
| differential testing | 차등 테스트 | 같은 입력을 두 구현에 넣고 출력 비교 |
| oracle | 기준 구현 | 차등 테스트에서 정답으로 삼는 쪽 (TFLite BUILTIN_REF) |
| mutation testing | 변이 테스트 | 일부러 버그를 넣어 테스트의 검출력 측정 |

---

## 16. 요약 & 체크리스트

int8 NN 커널은 "C1의 requant 식"과 "E2의 SIMD 명령" 사이를 잇는 계약이다. 계약은 NHWC·OHWI 레이아웃, per-channel 대칭 weight, input_offset = −zp_in, int32 bias, 채널별 (M0, shift), 앞쪽 패딩 = floor(total/2), clamp 범위로 이루어지고, 이것을 params 구조체로 고정한 뒤 reference 루프를 짜서 TFLite BUILTIN_REF와 비교하면 conv·depthwise·avg/max pool·add가 **bit-exact로 일치**한다(이 노트에서 2,800개 + 무작위 1,000 케이스 18.7만 개). softmax는 기준이 bit-exact를 약속하지 않으므로 실수 대비 ±1 LSB로 판정한다. 성능은 같은 정수 산술을 레이아웃(oc-inner, 패딩 + bias 접기), im2col, 레지스터 블로킹, SDOT로 재배치해 9.5 → 120~122 GMAC/s(약 13배)까지 올렸고, 부분 im2col 2.3 KB가 전체 im2col 576 KB와 같은 속도였다. MCU에서는 레지스터 수가 블록 크기를 정한다 — M4 2×2 SMLAD 블록은 MAC 16개당 20명령, M55 2×2 Helium 블록은 MAC 64개당 9명령이고, 더 큰 블록은 spill로 오히려 나빠졌다. 마지막으로, 변환기 파라미터만 쓰는 테스트는 반올림·clamp 버그를 놓친다 — 파라미터 공간 퍼징과 mutation test까지 해야 테스트를 믿을 수 있다.

- [ ] TFLite int8 conv의 계약(레이아웃, zp, bias scale, multiplier/shift, clamp)을 params 구조체로 쓸 수 있다
- [ ] 2×2 입력 conv 하나를 손으로 acc → SRDHM → RDBPOT → y까지 계산할 수 있다
- [ ] SAME 패딩의 앞·뒤 값을 stride 2에서 정확히 계산할 수 있다
- [ ] bias 접기의 대수와 "패딩을 zp_in으로 채워야 하는 이유"를 설명할 수 있다
- [ ] TFLite add의 left_shift 20 알고리즘을 유도하고, 순진한 방법이 왜 틀리는지 숫자로 말할 수 있다
- [ ] int8 softmax를 256칸 LUT로 구현하고 합격 기준을 정할 수 있다
- [ ] im2col·부분 im2col·직접 conv의 메모리와 속도 trade-off를 측정값으로 설명할 수 있다
- [ ] 4×4 블로킹이 로드당 MAC을 몇 배 늘리는지, 레지스터를 몇 개 쓰는지 계산할 수 있다
- [ ] M4/M55 어셈블리에서 내부 루프의 MAC당 명령 수와 spill을 찾아낼 수 있다
- [ ] 두 층 차등 테스트 harness와 mutation test를 설계하고 각 층의 사각지대를 말할 수 있다

## 참고 자료

- TensorFlow Lite / LiteRT 소스 — `tensorflow/lite/kernels/internal/reference/integer_ops/` (conv, depthwise_conv, pooling, add), `tensorflow/lite/kernels/internal/common.h`(MultiplyByQuantizedMultiplier), `quantization_util.cc`(QuantizeMultiplier): https://github.com/tensorflow/tensorflow
- TFLite 8-bit quantization spec: https://ai.google.dev/edge/litert/models/quantization_spec
- TensorFlow Lite Micro: https://github.com/tensorflow/tflite-micro
- CMSIS-NN: https://github.com/ARM-software/CMSIS-NN — 그리고 L. Lai, N. Suda, V. Chandra, "CMSIS-NN: Efficient Neural Network Kernels for Arm Cortex-M CPUs", arXiv:1801.06601 (2018)
- gemmlowp (SRDHM·RDBPOT의 원래 구현과 문서): https://github.com/google/gemmlowp
- B. Jacob et al., "Quantization and Training of Neural Networks for Efficient Integer-Arithmetic-Only Inference", CVPR 2018 (arXiv:1712.05877)
- Arm Cortex-M4 Technical Reference Manual (명령 타이밍), Arm Cortex-M55 TRM, Arm Helium(MVE) 프로그래머 가이드: https://developer.arm.com
- Arm C Language Extensions (ACLE) — `arm_acle.h`, `arm_neon.h`, `arm_mve.h` intrinsics: https://github.com/ARM-software/acle
- K. Chellapilla, S. Puri, P. Simard, "High Performance Convolutional Neural Networks for Document Processing" (2006) — conv를 행렬곱(im2col)으로 바꾸는 접근의 초기 문헌
- MIT 6.5940 TinyML and Efficient Deep Learning Computing (Song Han): https://efficientml.ai
- 이 노트 시리즈: C1(requantization), C8(검증), E2(SIMD), E4(DSP·Q-format), D4(depthwise 성능), F1·F2(TFLite·TFLM), J1(통합 패턴), J6(테스트)
