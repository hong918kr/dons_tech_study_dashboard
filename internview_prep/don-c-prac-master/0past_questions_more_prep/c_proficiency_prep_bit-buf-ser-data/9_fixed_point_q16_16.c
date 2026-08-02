/*
9. 고정소수점(Q16.16) 산술 라이브러리 - 실제 인터뷰 스타일 상세 설명

상황 설명:
당신은 저가형 MCU 기반의 산업용 센서 노드 펌웨어를 개발 중입니다.
이 MCU는 하드웨어 부동소수점 연산(FPU)이 없거나, 부동소수점 연산이 매우 느립니다.
하지만, 센서 데이터의 보정, PID 제어, 신호 필터링 등에서 소수점 이하 연산이 반드시 필요합니다.

이런 환경에서는 Q16.16(상위 16비트: 정수, 하위 16비트: 소수) 고정소수점 연산이 널리 사용됩니다.
고정소수점은 부동소수점보다 빠르고, 메모리 사용량이 적으며, 연산 결과가 항상 예측 가능합니다.
단, 오버플로/언더플로, 반올림, 정밀도 손실 등은 개발자가 직접 관리해야 합니다.

면접관이 평가하고자 하는 포인트:
- Q 포맷의 비트 구조와 변환 원리 이해
- 덧셈/뺄셈/곱셈/나눗셈의 비트 연산 및 오버플로 처리
- double <-> Q16.16 변환의 정확성
- API 설계 및 테스트 케이스 작성 능력

문제:
아래 시그니처에 따라 Q16.16 고정소수점 산술 라이브러리를 구현하세요.
- q16_16_from_double, q16_16_to_double
- q16_16_add, q16_16_sub, q16_16_mul, q16_16_div

아래는 구현 예시와 테스트 코드입니다.
*/

#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <assert.h>

typedef int32_t q16_16_t;

#define Q16_16_ONE   (1 << 16)
#define Q16_16_HALF  (1 << 15)

// double -> Q16.16 변환 (반올림)
q16_16_t q16_16_from_double(double d) {
    if (d >= 32768.0) return INT32_MAX;
    if (d < -32768.0) return INT32_MIN;
    return (q16_16_t)lround(d * Q16_16_ONE);
}

// Q16.16 -> double 변환
double q16_16_to_double(q16_16_t q) {
    return ((double)q) / Q16_16_ONE;
}

// 덧셈
q16_16_t q16_16_add(q16_16_t a, q16_16_t b) {
    int64_t r = (int64_t)a + b;
    if (r > INT32_MAX) return INT32_MAX;
    if (r < INT32_MIN) return INT32_MIN;
    return (q16_16_t)r;
}

// 뺄셈
q16_16_t q16_16_sub(q16_16_t a, q16_16_t b) {
    int64_t r = (int64_t)a - b;
    if (r > INT32_MAX) return INT32_MAX;
    if (r < INT32_MIN) return INT32_MIN;
    return (q16_16_t)r;
}

// 곱셈 (64비트 중간값 사용, 반올림)
q16_16_t q16_16_mul(q16_16_t a, q16_16_t b) {
    int64_t r = (int64_t)a * b;
    // 반올림
    r += Q16_16_HALF;
    r >>= 16;
    if (r > INT32_MAX) return INT32_MAX;
    if (r < INT32_MIN) return INT32_MIN;
    return (q16_16_t)r;
}

// 나눗셈 (64비트 중간값 사용, 반올림)
q16_16_t q16_16_div(q16_16_t a, q16_16_t b) {
    if (b == 0) return (a >= 0) ? INT32_MAX : INT32_MIN;
    int64_t r = ((int64_t)a << 16);
    // 반올림
    if ((r >= 0 && b > 0) || (r < 0 && b < 0))
        r += b / 2;
    else
        r -= b / 2;
    r /= b;
    if (r > INT32_MAX) return INT32_MAX;
    if (r < INT32_MIN) return INT32_MIN;
    return (q16_16_t)r;
}

// 테스트 코드
int main() {
    // 변환 테스트
    double vals[] = {0.0, 1.0, -1.0, 123.456, -123.456, 32767.999, -32768.0};
    for (int i = 0; i < 7; ++i) {
        q16_16_t q = q16_16_from_double(vals[i]);
        double d = q16_16_to_double(q);
        // 1 LSB 이내 오차 허용
        assert(fabs(d - vals[i]) < 1.0 / Q16_16_ONE);
    }

    // 덧셈/뺄셈
    q16_16_t a = q16_16_from_double(1.5);
    q16_16_t b = q16_16_from_double(2.25);
    assert(fabs(q16_16_to_double(q16_16_add(a, b)) - 3.75) < 1.0 / Q16_16_ONE);
    assert(fabs(q16_16_to_double(q16_16_sub(b, a)) - 0.75) < 1.0 / Q16_16_ONE);

    // 곱셈
    q16_16_t c = q16_16_mul(a, b); // 1.5 * 2.25 = 3.375
    assert(fabs(q16_16_to_double(c) - 3.375) < 1.0 / Q16_16_ONE);

    // 나눗셈
    q16_16_t d = q16_16_div(b, a); // 2.25 / 1.5 = 1.5
    assert(fabs(q16_16_to_double(d) - 1.5) < 1.0 / Q16_16_ONE);

    // 오버플로/언더플로
    q16_16_t max = q16_16_from_double(32767.999);
    q16_16_t min = q16_16_from_double(-32768.0);
    assert(q16_16_add(max, a) == INT32_MAX);
    assert(q16_16_sub(min, a) == INT32_MIN);

    // 0 나눗셈
    assert(q16_16_div(a, 0) == INT32_MAX);
    assert(q16_16_div(-a, 0) == INT32_MIN);

    printf("All Q16.16 fixed-point tests passed!\n");
    return 0;
}