/* C-M15 demo — goto 언와인딩에서 컴파일러가 잡아 주는 것 / 못 잡는 것
 * 빌드 (경고를 보려는 것이므로 -Werror 없이):
 *   cc -std=c11 -Wall -Wextra -O2 -c demo_goto_pitfalls.c -o /dev/null
 */
#include <stdlib.h>

int  hw_enable(void);
int  chan_alloc(void);
void chan_free(int ch);
void hw_disable(void);

/* ① goto 가 초기화 있는 선언을 건너뛴다: C 에서는 합법(C++ 은 컴파일 에러).
 *    err 경로에서 ch 는 "초기화 안 된" 값 → chan_free(쓰레기) */
int pitfall_skip_init(void) {
    int ret = hw_enable();
    if (ret < 0) goto err;
    int ch = chan_alloc();
    if (ch < 0) { ret = ch; goto err_hw; }
    return 0;
err_hw:
    hw_disable();
err:
    chan_free(ch);              /* ret < 0 첫 경로에서 ch 는 indeterminate */
    return ret;
}

/* ② "one err bug" (Linux coding-style 의 이름): 라벨 하나로 몰면 아직 없는 것을 해제 */
struct foo { char *bar; };
int pitfall_one_err(struct foo **out) {
    struct foo *foo = malloc(sizeof *foo);
    if (!foo) goto err;
    foo->bar = malloc(16);
    if (!foo->bar) goto err;
    *out = foo;
    return 0;
err:
    free(foo->bar);             /* foo == NULL 인 첫 경로에서 NULL 역참조 */
    free(foo);
    return -12;
}

/* ③ C11 에서 라벨 뒤에는 "문장" 이 와야 한다: 블록 끝 라벨 / 선언 앞 라벨은 C23 부터 */
void pitfall_label_end(int x) {
    if (x) goto out;
    x++;
out:
}
