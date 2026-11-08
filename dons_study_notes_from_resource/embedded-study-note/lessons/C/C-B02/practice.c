/* C-B02 추가 연습 — 한 번에 검증 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* 연습 1: 결과 예측 퀴즈 — 아래 CHECK 들이 모두 참이 되는 이유를 설명할 수 있어야 한다 */
static void quiz(void);

/* 연습 2: 16비트 시퀀스 번호 비교 (랩어라운드 안전, TCP/BLE 패킷 번호) */
static bool seq16_before(uint16_t a, uint16_t b) {
    return (int16_t)(uint16_t)(a - b) < 0;      /* 차이를 부호 있는 16비트로 해석 */
}

/* 연습 3: 12비트 2의 보수 ADC 값 → int16_t 부호 확장 (이식성 있게) */
static int16_t sign_extend12(uint16_t raw) {
    raw &= 0x0FFFu;
    return (int16_t)((int32_t)(raw ^ 0x800u) - 0x800);
}

/* 연습 4: 두 uint32_t 의 평균 — a + b 가 넘쳐도 정확하게 (내림) */
static uint32_t avg_u32(uint32_t a, uint32_t b) {
    return (a & b) + ((a ^ b) >> 1);            /* 공통 비트 + 다른 비트의 절반 */
}

/* 연습 5: INT32_MIN 에서도 안전한 절댓값 → uint32_t */
static uint32_t abs_u32(int32_t x) {
    return x < 0 ? 0u - (uint32_t)x : (uint32_t)x;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static void quiz(void) {
    uint8_t a = 200, b = 100;
    CHECK(a + b == 300);                       /* int 로 승격돼서 계산 → 300 */
    CHECK((uint8_t)(a + b) == 44);             /* 저장할 때 잘림 */
    CHECK(sizeof(a + b) == sizeof(int));       /* 결과 타입은 int */
    CHECK((-1 < 1u) == 0);                     /* -1 → 0xFFFFFFFF 로 바뀌어 비교 (거짓!) */
    CHECK((-1 < (int64_t)1u) == 1);            /* 더 넓은 signed 타입이면 정상 */
    CHECK((int8_t)0x80 == -128);               /* 범위 밖 변환: 구현 정의(보통 2의 보수) */
    uint16_t u = 0xFFFF;
    CHECK(u + 1 == 0x10000);                   /* uint16_t 는 int 로 승격 → 넘치지 않음 */
    uint32_t w = 0xFFFFFFFFu;
    CHECK(w + 1 == 0);                         /* uint32_t 는 승격 안 됨 → 모듈로 */
}

int main(void) {
    quiz();

    CHECK(seq16_before(1, 2) == true);
    CHECK(seq16_before(65530, 5) == true);      /* 65530 → 5 는 11 앞선 것 */
    CHECK(seq16_before(5, 65530) == false);

    CHECK(sign_extend12(0x7FF) == 2047);
    CHECK(sign_extend12(0x800) == -2048);
    CHECK(sign_extend12(0xFFF) == -1);

    CHECK(avg_u32(0xFFFFFFFFu, 0xFFFFFFFDu) == 0xFFFFFFFEu);
    CHECK(avg_u32(3, 4) == 3);

    CHECK(abs_u32(-5) == 5u);
    CHECK(abs_u32(INT32_MIN) == 2147483648u);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
