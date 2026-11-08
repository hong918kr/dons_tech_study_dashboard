/* SIV-B08 demo — linker section 으로 테스트 등록하기 (constructor 없이)
 *
 * TEST() 가 test_case 를 가리키는 포인터 하나를 전용 섹션에 넣는다. 링커가 여러 .o 의 같은 섹션을
 * 이어 붙이므로, 실행 중에 "섹션 시작 ~ 끝" 을 훑으면 모든 테스트가 나온다. main 전 코드가 돌 필요가 없다.
 *
 *   Mach-O (이 맥):  section("__DATA,__siv_tests")  +  getsectiondata() 또는 section$start$/section$end$ 심볼
 *   ELF (Linux/arm-none-eabi + GNU ld/lld):  section("siv_tests")  +  링커가 만들어 주는 __start_siv_tests/__stop_siv_tests
 *
 *   맥:    cc -std=c11 -Wall -Wextra -Werror -O2 demo_section.c -o demo_section && ./demo_section
 *   ELF:   clang --target=aarch64-linux-gnu -ffreestanding -std=c11 -O2 -c demo_section.c -o demo_section_elf.o
 *          (이 맥엔 ELF 링커가 없어 .o 의 섹션·재배치만 확인)
 */
#include <stdint.h>

typedef struct { const char *name; int (*fn)(void); } test_case;   /* fn: 0 = PASS */

#if defined(__APPLE__)
#  define SIV_SECTION "__DATA,__siv_tests"        /* "세그먼트,섹션", 섹션 이름은 16자 이하 */
#else
#  define SIV_SECTION "siv_tests"                 /* C 식별자로 쓸 수 있어야 __start_ 가 생긴다 */
#endif

/* used: 아무도 참조하지 않는 static 변수지만 버리지 말라 (컴파일러 + Mach-O 는 no_dead_strip 까지) */
#define TEST(name)                                                                  \
    static int name(void);                                                          \
    static const test_case name##_tc = { #name, name };                             \
    __attribute__((used, section(SIV_SECTION)))                                     \
    static const test_case *const name##_ptr = &name##_tc;                          \
    static int name(void)

TEST(reg_reset_values) { return 0; }
TEST(reg_rw_patterns)  { return 0; }
TEST(mem_march_c)      { return 1; }               /* 일부러 실패 */

#if defined(__APPLE__)
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>                         /* _mh_execute_header */
#include <stdio.h>

/* Apple ld 가 제공하는 섹션 경계 심볼 (ELF 의 __start_/__stop_ 에 해당) */
extern const test_case *const siv_start __asm__("section$start$__DATA$__siv_tests");
extern const test_case *const siv_end   __asm__("section$end$__DATA$__siv_tests");

int main(void) {
    unsigned long size = 0;
    const test_case *const *v =
        (const test_case *const *)getsectiondata(&_mh_execute_header, "__DATA", "__siv_tests", &size);
    size_t n = size / sizeof *v;
    printf("getsectiondata: %zu bytes -> %zu tests\n", (size_t)size, n);
    int fails = 0;
    for (size_t i = 0; i < n; i++) {
        int rc = v[i]->fn();
        fails += rc != 0;
        printf("  %-18s %s\n", v[i]->name, rc ? "FAIL" : "PASS");
    }
    printf("section$start/end: %td tests (same array: %s)\n", &siv_end - &siv_start,
           (const void *)&siv_start == (const void *)v ? "yes" : "no");
    return fails ? 1 : 0;
}
#else
/* ELF: GNU ld / lld 는 C 식별자 이름의 섹션마다 __start_<sec> / __stop_<sec> 를 자동으로 정의한다 */
extern const test_case *const __start_siv_tests[];
extern const test_case *const __stop_siv_tests[];

int run_all(void) {
    int fails = 0;
    for (const test_case *const *p = __start_siv_tests; p < __stop_siv_tests; p++)
        fails += (*p)->fn() != 0;
    return fails;
}
#endif
