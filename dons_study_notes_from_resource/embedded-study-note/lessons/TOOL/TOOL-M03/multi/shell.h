/* TOOL-M03 demo: 여러 .c 파일이 같은 표에 등록하는 모양 (solution.c 의 이식성 블록과 같은 내용).
 * -DNO_USED     : used 를 빼고 빌드 (gc-sections / dead_strip 실험)
 * -DSHELL_RETAIN: used + retain (ELF SHF_GNU_RETAIN "R" 플래그 → --gc-sections 가 못 지움)
 * -DSHELL_WEAK  : ELF 경계 심볼을 weak 로 선언 (섹션이 없어도 링크 성공 → NULL..NULL)
 * -DSHELL_GLOBAL: 항목을 static 이 아닌 외부 링키지로 → 같은 이름 두 번 등록 = 링크 에러 (중복 검사를 링커에게)
 */
#ifndef SHELL_H
#define SHELL_H
struct shell_cmd { const char *name; int (*fn)(int, char **); const char *help; };

#if defined(NO_USED)
#  define SHELL_USED
#elif defined(SHELL_RETAIN)
#  define SHELL_USED used, retain,
#else
#  define SHELL_USED used,
#endif
#ifndef SECT_ID
#  define SECT_ID shellcmds
#endif
#define S_CAT_(a, b) a##b
#define S_CAT(a, b)  S_CAT_(a, b)
#define S_STR_(x) #x
#define S_STR(x)  S_STR_(x)

#if defined(__APPLE__)
#  define SHELL_SECTION "__DATA,__" S_STR(SECT_ID)
extern const struct shell_cmd shell_begin[] __asm("section$start$__DATA$__" S_STR(SECT_ID));
extern const struct shell_cmd shell_end[]   __asm("section$end$__DATA$__" S_STR(SECT_ID));
#elif defined(SHELL_LDSCRIPT)
#  define SHELL_SECTION ".shellcmds"
extern const struct shell_cmd __shellcmds_start[], __shellcmds_end[];
#  define shell_begin __shellcmds_start
#  define shell_end   __shellcmds_end
#else
#  define SHELL_SECTION S_STR(SECT_ID)
#  ifdef SHELL_WEAK
#    define SHELL_EXT extern __attribute__((weak))
#  else
#    define SHELL_EXT extern
#  endif
SHELL_EXT const struct shell_cmd S_CAT(__start_, SECT_ID)[];
SHELL_EXT const struct shell_cmd S_CAT(__stop_, SECT_ID)[];
#  define shell_begin S_CAT(__start_, SECT_ID)
#  define shell_end   S_CAT(__stop_, SECT_ID)
#endif

#ifdef SHELL_GLOBAL
#  define SHELL_LINKAGE              /* 파일 범위 const 객체 = C 에서는 외부 링키지 */
#else
#  define SHELL_LINKAGE static
#endif
#define SHELL_CMD(name_, fn_, help_)                                                   \
    SHELL_LINKAGE const struct shell_cmd S_CAT(shell_cmd_, name_)                      \
        __attribute__((SHELL_USED aligned(sizeof(void *)), section(SHELL_SECTION))) = \
            { #name_, fn_, help_ }
#endif
