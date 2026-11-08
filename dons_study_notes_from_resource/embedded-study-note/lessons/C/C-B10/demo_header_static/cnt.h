/* cnt.h — 잘못된 예: 헤더에 static 변수 정의 → include 한 파일마다 별도 사본 */
#ifndef CNT_H
#define CNT_H
static int counter;
void inc_a(void);
int  get_a(void);
#endif
