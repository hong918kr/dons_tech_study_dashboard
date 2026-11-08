/* C-M08 데모: 함수 끝의 memset(key, 0, n) 은 "죽은 저장(dead store)" 이라 -O2 에서 사라진다
 *   cc -std=c11 -O2 -S demo_deadstore.c -o - | grep -E '^_|bl\s'
 * 연습 4 의 secure_zero(volatile 루프) 는 남는다. 표준 해법: C23 memset_explicit, C11 Annex K memset_s.
 */
#include <stddef.h>
#include <string.h>

void get_key(unsigned char *k);            /* 외부: 키를 채운다 */
void use_key(const unsigned char *k);      /* 외부: 키를 쓴다   */

static void secure_zero(void *p, size_t n) {
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}

void session_plain(void) {
    unsigned char key[32];
    get_key(key);
    use_key(key);
    memset(key, 0, sizeof key);            /* 이후 key 를 읽는 코드가 없다 → 지워도 관찰 불가 → 삭제 */
}

void session_secure(void) {
    unsigned char key[32];
    get_key(key);
    use_key(key);
    secure_zero(key, sizeof key);          /* volatile 쓰기 32번은 생략 불가 */
}
