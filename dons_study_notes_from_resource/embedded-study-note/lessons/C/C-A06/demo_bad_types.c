/* C-A06 관찰: container_of / MY_OFFSETOF 의 컴파일 타임 검사가 무엇을 잡고 무엇을 못 잡나
 *   cc -std=c11 -Wall -Wextra -Werror -pedantic -c demo_bad_types.c -o /dev/null       # 기본: 깨끗
 *   cc -std=c11 -Wall -Wextra -c -DBAD_INT demo_bad_types.c -o /dev/null               # 각 -D 를 하나씩
 *   BAD_INT BAD_INT_STRICT BAD_ARRAY BAD_CONST BAD_BITFIELD BAD_BITFIELD_STD BAD_TOO_BIG BAD_WRAP
 * (-Werror 없이 빌드해서 "경고로 끝나는 것" 과 "무조건 에러" 를 구분해 본다)
 */
#include <stddef.h>
#include <stdint.h>

#define MY_OFFSETOF_ARENA_SIZE  1024u
#define MY_OFFSETOF_ARENA_ALIGN 64
static _Alignas(MY_OFFSETOF_ARENA_ALIGN) unsigned char my_offsetof_arena_[MY_OFFSETOF_ARENA_SIZE];
#define MY_OFFSETOF(type, member)                                                              \
    (sizeof(char[(sizeof(type) <= MY_OFFSETOF_ARENA_SIZE &&                                    \
                  _Alignof(type) <= MY_OFFSETOF_ARENA_ALIGN) ? 1 : -1]) - 1u +                 \
     (size_t)((unsigned char *)&((type *)(void *)my_offsetof_arena_)->member - my_offsetof_arena_))
#define container_of(ptr, type, member)                                                        \
    ((void)sizeof((ptr) == &((type *)0)->member),                                              \
     (type *)(void *)((char *)(ptr) - MY_OFFSETOF(type, member)))
#define QUAL_VOID_PTR_(p) (1 ? (p) : (void *)(p))
#define container_of_const(ptr, type, member)                                                  \
    _Generic(QUAL_VOID_PTR_(ptr),                                                              \
        const void *: ((void)sizeof((ptr) == &((type *)0)->member),                            \
                       (const type *)(const void *)((const char *)(ptr) - MY_OFFSETOF(type, member))), \
        default: container_of(ptr, type, member))
/* 연습용 강화판: 크기가 다르면 -Werror 없이도 무조건 에러 (구조체 안의 _Static_assert, 6.7.2.1p1) */
#define container_of_strict(ptr, type, member)                                                 \
    ((void)sizeof(struct { _Static_assert(sizeof(*(ptr)) == sizeof(((type *)0)->member),      \
                                          "container_of_strict: member size mismatch");        \
                           int dummy_; }),                                                     \
     container_of(ptr, type, member))

struct chan { uint8_t idx; uint32_t cnt; };
struct ctrl { uint32_t base; struct chan ch[4]; };
struct list_head { struct list_head *next, *prev; };
struct task { int id; struct list_head all; struct list_head run; };
struct hw { uint32_t en : 1; uint32_t mode : 3; };
struct huge { uint8_t b[2000]; struct list_head l; };
struct rx_link { struct list_head l; };              /* 같은 list_head 를 "다른 타입" 으로 감싼다 */
struct tx_link { struct list_head l; };
struct task2 { int id; struct rx_link rx; struct tx_link tx; };

int f(struct chan *c, struct task *t, const struct task *ct, int *ip, struct huge *h, struct task2 *t2);
int f(struct chan *c, struct task *t, const struct task *ct, int *ip, struct huge *h, struct task2 *t2) {
    int r = 0;
    r += container_of(c - c->idx, struct ctrl, ch[0])->base != 0;     /* 올바른 배열 원소 사용 */
    r += container_of_strict(&t->run, struct task, run)->id;
    struct task *nc = container_of(&ct->run, struct task, run);       /* const 가 조용히 사라진다 (경고 0) */
    r += nc->id;
#ifdef BAD_INT
    r += container_of(ip, struct task, run)->id;                      /* int * 를 list_head 로 착각 */
#endif
#ifdef BAD_INT_STRICT
    r += container_of_strict(ip, struct task, run)->id;
#endif
#ifdef BAD_ARRAY
    r += container_of(c, struct ctrl, ch)->base != 0;                 /* ch[0] 이 아니라 배열 전체 */
#endif
#ifdef BAD_CONST
    struct task *w = container_of_const(&ct->run, struct task, run);  /* const 결과를 non-const 에 */
    r += w->id;
#endif
#ifdef BAD_BITFIELD
    r += (int)MY_OFFSETOF(struct hw, mode);
#endif
#ifdef BAD_BITFIELD_STD
    r += (int)offsetof(struct hw, mode);
#endif
#ifdef BAD_TOO_BIG
    r += container_of(&h->l, struct huge, l)->b[0];                   /* 2016 B > arena 1024 B */
#endif
#ifdef BAD_WRAP
    r += container_of(&t2->rx, struct task2, tx)->id;                 /* 다른 멤버 이름 — 감싼 타입이 잡는다 */
#endif
    (void)ip; (void)h; (void)t2;
    return r;
}
