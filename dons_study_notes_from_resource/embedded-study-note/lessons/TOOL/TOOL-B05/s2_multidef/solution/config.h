/* config.h — 전역 설정/상태 (TODO: 링크 에러 + 조용한 버그를 고친다) */
#ifndef CONFIG_H
#define CONFIG_H

extern int g_log_level;          /* 선언만 (정의는 config.c 한 곳) */
extern unsigned long g_ticks;
extern unsigned g_err_count;

#endif
