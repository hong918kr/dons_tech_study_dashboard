# 15. Q15 고정소수점 필터

> **주제**: 고정소수점 표현 · 이동평균 · 1차 IIR · 오버플로 · **난이도**: 중급 · **목표 시간**: 30분
> **해설은 보지 말 것**: 먼저 `starters/15_fixed_point_filter.c`를 채워 `make run N=15`로 통과시킨다.

## 면접관의 문장

"This part has no FPU, and even on the parts that do, I do not want floating point inside an audio interrupt. So we do everything in Q15. Write me two filters: a boxcar moving average over eight samples, and a first-order IIR of the form y plus alpha times x minus y. Integers only in the filter itself. I want the rounding right, because a half-LSB bias per sample turns into an audible DC offset after a few thousand samples. And I want you to tell me exactly which combination of inputs overflows, before you write the line that overflows. Then prove to me that your fixed-point version tracks a floating-point reference, and tell me your error bound."

## 요구사항

1. `q15_t`는 `int16_t`다. 정수 `v`는 실수 `v / 32768`을 뜻한다. 표현 범위는 `-1.0` 이상 `+0.999969482421875` 이하이고, `+1.0`은 표현할 수 없다.
2. `sat16()`은 `int32_t`를 `int16_t` 범위로 **포화**시킨다. wrap이 아니다. `32768`은 `32767`이 되고 `-32769`는 `-32768`이 된다.
3. `q15_mul(a, b)`는 Q15 곱이다. 곱은 `int32_t`로 넓혀서 하고, `>> 15` 전에 반올림 항을 더해 **round-half-up**으로 만든다.
4. `q15_mul(Q15_MIN, Q15_MIN)`은 `+1.0`이 되어야 하지만 표현할 수 없으므로 `32767`로 포화한다. 오버플로하는 조합은 이것 하나뿐이다. 나머지 모든 입력 쌍에서 포화가 일어나서는 안 된다.
5. `q15_mul`은 float 기준값과 **1 LSB 이내**로 같아야 한다.
6. 이동평균은 창 크기 `MA_N`(8)이고 **러닝 합**을 유지한다. `ma_push()`는 창 크기와 무관하게 O(1)이어야 한다.
7. 창이 다 차기 전(워밍업)에는 **지금까지 들어온 개수**로 나눈다. 첫 샘플 `1000` 하나만 들어왔으면 결과는 `1000`이다. 0으로 희석하지 않는다.
8. 이동평균의 반올림은 0에서 **먼 방향**으로 대칭이어야 한다. 합 `+4`를 8로 나누면 `+1`, 합 `-4`를 8로 나누면 `-1`이다.
9. 러닝 합은 `int32_t`다. 전 샘플이 `32767`이어도(합 `262136`) 넘치지 않아야 한다.
10. 이동평균은 float 평균과 **0.5 LSB 이내**로 같아야 한다.
11. IIR 상태는 Q30(`int32_t`)으로 들고 있다. `acc = y_q15 * 32768`이다. 출력은 Q15다.
12. `iir_init()`은 음수 `alpha`를 `0`으로 클램프한다(음수면 발산한다).
13. `alpha == 0`이면 출력이 초기값에 얼어붙는다. `alpha == 16384`(0.5)면 첫 스텝에 간격의 절반, 두 번째에 3/4를 간다.
14. 상수 입력을 충분히 오래 넣으면 출력이 **정확히 그 값**에 도달해야 한다. dead band에 걸려 `x - 1`에서 멈추면 안 된다.
15. 최악 입력(`alpha`가 최대이고 입력이 매 샘플 `Q15_MIN`과 `Q15_MAX`를 왕복)에서도 중간 계산이 `int32_t`를 넘지 않아야 한다. 넘치지 않는 이유를 코드 주석에 숫자로 적는다.
16. IIR은 float 기준 IIR과 **2 LSB 이내**로 같아야 한다. `alpha`가 0.01, 0.1, 0.5, 0.9인 네 경우 모두에서다.
17. 필터 본체(`sat16`, `q15_mul`, `ma_push`, `iir_push`)에는 부동소수점 연산이 **한 줄도** 없어야 한다. float은 테스트의 기준값 계산에만 쓴다.

## 인터페이스

```c
#include <stdint.h>

typedef int16_t q15_t;

#define Q15_SHIFT  15
#define Q15_SCALE  32768
#define Q15_MAX    ((q15_t)32767)
#define Q15_MIN    ((q15_t)-32768)
#define MA_N       8u

typedef struct {
    int16_t buf[MA_N];
    int32_t sum;
    uint8_t idx;
    uint8_t n;
} ma_t;

typedef struct {
    int32_t acc;     /* y in Q30 */
    q15_t   alpha;
} iir_t;

int16_t sat16(int32_t v);
q15_t   q15_mul(q15_t a, q15_t b);
void    ma_init(ma_t *f);
int16_t ma_push(ma_t *f, int16_t x);
void    iir_init(iir_t *f, q15_t alpha, q15_t y0);
q15_t   iir_push(iir_t *f, q15_t x);
```

## 제약

- 필터 본체에 `float`, `double`, `math.h` 금지. 나눗셈은 이동평균의 평균 계산 한 곳만 허용한다.
- 동적 할당 금지. 상태는 전부 구조체 안에 있다.
- 64비트 정수를 쓰지 않고도 되는 형태가 있다. 찾아보고, 못 찾으면 쓴 이유를 주석에 적는다.
- 음수의 좌시프트(`neg << k`)는 C11에서 undefined behavior다. 쓰지 않는다. 우시프트는 implementation-defined이며 이 파일은 산술 시프트를 가정하고 `main()`에서 assert로 못박는다.
- C11 표준 라이브러리만. `cc -std=c11 -Wall -Wextra -O2`에서 경고 0개.

## 예시 동작

Q15 눈금.

```
정수      -32768   -16384        0    16384    32767
실수        -1.0      -0.5      0.0     +0.5   +0.99997
```

`q15_mul` 확인값.

```
q15_mul(16384, 16384)  =  8192      (0.5 x 0.5 = 0.25)
q15_mul(-16384, 16384) = -8192      (-0.5 x 0.5 = -0.25)
q15_mul(32767, 32767)  =  32766     (0.99997^2 = 0.99994)
q15_mul(-32768, 32767) = -32767
q15_mul(-32768, -32768)=  32767     <- 포화. +1.0은 Q15에 없다
```

이동평균 워밍업.

```
push 1000 -> 1000     (합 1000 / n 1)
push 2000 -> 1500     (합 3000 / n 2)
push 3000 -> 2000     (합 6000 / n 3)
```

alpha = 0.5인 IIR의 스텝 응답.

```
step:      0      1      2      3      4
x:         -   20000  20000  20000  20000
y:         0   10000  15000  17500  18750
                절반   3/4    7/8    15/16
```

## 스스로 점검할 질문

1. `>> 15` 앞의 반올림 항을 빼면 `q15_mul(-1, 32767)`이 얼마가 되나? 1000번 곱하면 얼마나 치우치나?
2. `int16_t a * int16_t b`를 `int16_t`에 담으면 어느 입력에서 처음 틀리나?
3. IIR에서 `err = x - y`의 최대 크기는 얼마인가? 그것과 `alpha`의 곱은 `int32_t`에 들어가나? 계산해 보라.
4. `alpha`를 Q16(0부터 65535)으로 키우면 15번 요구사항이 왜 깨지나?
5. `y += (x - y) >> 3` 형태의 IIR은 상수 입력에서 어디에 멈추나? 이 문제의 형태는 왜 멈추지 않나?
6. 이동평균과 IIR 중 스텝 입력에 더 빨리 반응하는 쪽은? 같은 "부드러움"을 얻는 데 드는 RAM은 각각 얼마인가?
7. 이동평균에서 `sum`을 `int16_t`로 두면 몇 번째 샘플에서 깨지나?
8. 포화(saturate)와 wrap 중 오디오에서 덜 나쁜 쪽은 어느 쪽이고, 소리로는 각각 어떻게 들리나?

## follow-up (면접관이 이어서 물을 것)

1. "이걸 2차 IIR(biquad)로 올리면 무엇이 더 필요한가?"
2. "Cortex-M4의 SIMD 명령(`SMLAD`, `SSAT`)으로 바꾸면 어디가 줄어드나?"
3. "샘플레이트가 두 배가 되면 같은 컷오프를 유지하려면 `alpha`를 어떻게 바꾸나?"
4. "Q15 대신 Q31을 쓰면 무엇이 좋아지고 무엇이 비싸지나?"
5. "이 필터의 -3 dB 컷오프 주파수를 `alpha`로 어떻게 계산하나?"

---

해설: [L2 노트](../drills/L2_embedded_idioms.html) §2
