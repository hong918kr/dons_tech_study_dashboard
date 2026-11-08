/* C-M06 demo — 명령/ops 표가 어느 섹션에 놓이나 (오브젝트 파일만 만든다, main 없음)
 *
 *   clang -std=c11 -O2 -c demo_sections.c -o /tmp/sec_mac.o && nm -m /tmp/sec_mac.o
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -c demo_sections.c -o /tmp/sec_m4.o
 *   xcrun llvm-objdump -t /tmp/sec_m4.o ; xcrun llvm-readelf -r /tmp/sec_m4.o
 */
#include <stdint.h>

typedef int (*cmd_fn)(int argc, char **argv, void *ctx);
struct cmd { const char *name; cmd_fn fn; };

static int c_peek(int argc, char **argv, void *ctx) { (void)argv; (void)ctx; return argc; }
static int c_poke(int argc, char **argv, void *ctx) { (void)argv; (void)ctx; return -argc; }

/* ① 표 전체가 const → 플래시(.rodata) 에 둘 수 있다 */
const struct cmd k_cmds_const[] = { { "peek", c_peek }, { "poke", c_poke } };
/* ② const 를 빼먹음 → .data: 플래시에 초기값 + 부팅 때 RAM 으로 복사 + RAM 에서 덮어쓸 수 있음 */
struct cmd g_cmds_rw[] = { { "peek", c_peek }, { "poke", c_poke } };
/* ③ 포인터 배열: const 위치가 중요하다 (C-B08) */
const char *g_names_ptr_rw[] = { "peek", "poke" };            /* 가리키는 문자만 const → .data   */
const char *const k_names_ptr_const[] = { "peek", "poke" };   /* 포인터 자체도 const → .rodata */
