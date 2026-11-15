/* config.h — 전역 설정/상태 (TODO: 링크 에러 + 조용한 버그를 고친다) */
#ifndef CONFIG_H
#define CONFIG_H

int g_log_level = 2;             /* 초기값 있는 정의 */
unsigned long g_ticks;           /* 초기값 없는 정의 (tentative definition) */
static unsigned g_err_count;     /* "static 붙이면 에러 안 나던데?" */

#endif
