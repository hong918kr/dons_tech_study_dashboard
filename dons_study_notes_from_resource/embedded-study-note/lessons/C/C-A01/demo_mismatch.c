/* C-A01 demo_mismatch.c — 테스트가 실패하면 무엇이 보이나
 * solution.c 를 그대로 포함하고(그쪽 main 은 이름만 바꿈) 버그 있는 init 을 같은 기대 시퀀스·규칙 모델에 넣는다.
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_mismatch.c -o demo_mismatch && ./demo_mismatch
 */
#define main solution_main
#include "solution.c"
#undef main

/* 본문 spi_init(a) 와 같은 값을 쓰지만 SPE 를 CR2 보다 먼저 켠다 (최종 상태는 같다) */
static void init_spe_before_cr2(void) {
    REG_MODIFY(RCC_APB2ENR, 0u, RCC_APB2_SPI1);
    (void)REG_READ(RCC_APB2ENR);
    REG_MODIFY(RCC_APB2RSTR, 0u, RCC_APB2_SPI1);
    REG_MODIFY(RCC_APB2RSTR, RCC_APB2_SPI1, 0u);
    REG_WRITE(SPI1_CR1, 0x31Fu);
    REG_WRITE(SPI1_CR1, 0x35Fu);
    REG_WRITE(SPI1_CR2, 0x000u);
}

int main(void) {
    char msg[160];
    const reg_expect_t exp[] = {
        EXP_R(RCC_APB2ENR),  EXP_W(RCC_APB2ENR, 0x00001010u), EXP_R(RCC_APB2ENR),
        EXP_R(RCC_APB2RSTR), EXP_W(RCC_APB2RSTR, 0x00001000u),
        EXP_R(RCC_APB2RSTR), EXP_W(RCC_APB2RSTR, 0x00000000u),
        EXP_W(SPI1_CR1, 0x0000031Fu), EXP_W(SPI1_CR2, 0x00000000u), EXP_W(SPI1_CR1, 0x0000035Fu),
    };

    mock_reset();
    mock_set(RCC_APB2ENR, 1u << 4);
    init_spe_before_cr2();
    int at = mock_check_seq(exp, NELEM(exp), msg, sizeof msg);
    printf("[1] SPE before CR2: check=%d\n    %s\n", at, msg);
    printf("    final CR1=0x%03X CR2=0x%03X viol=0x%02X\n", (unsigned)mock_peek(SPI1_CR1),
           (unsigned)mock_peek(SPI1_CR2), g_viol);

    mock_reset();
    bug_init_wrong_bus();
    mock_dump("[2] bug_init_wrong_bus");

    mock_reset();
    bug_init_no_ssi();
    mock_dump("[3] bug_init_no_ssi");
    printf("    after: CR1=0x%03X SR=0x%02X (MODF=%d)\n", (unsigned)mock_peek(SPI1_CR1),
           (unsigned)mock_peek(SPI1_SR), (mock_peek(SPI1_SR) & SPI_SR_MODF) != 0u);
    return 0;
}
