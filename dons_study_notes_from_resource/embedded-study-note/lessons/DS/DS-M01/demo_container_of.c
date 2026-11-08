/* DS-M01 관찰용: container_of 의 주소 계산, 두 가지 타입 검사, 침입형 vs 비침입형 메모리.
 *   cc -std=c11 -Wall -Wextra -Werror -O2 demo_container_of.c -o /tmp/DS-M01_demo && /tmp/DS-M01_demo
 *   cc -std=c11 -Wall -Wextra -Werror -O2 -DBAD_TYPE demo_container_of.c        # 포터블 검사 → 컴파일 에러
 *   cc -std=c11 -Wall -Wextra -Werror -O2 -DBAD_TYPE_GNU demo_container_of.c    # Linux 식 검사 → 컴파일 에러
 *   clang --target=thumbv7em-none-eabi -ffreestanding -std=c11 -c demo_container_of.c -o /tmp/DS-M01_m4.o
 *                                                       # Cortex-M4 (32-bit) 레이아웃을 _Static_assert 로 확인
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct list_head { struct list_head *next, *prev; };

struct task {
    int id;
    int state;
    bool kill;
    struct list_head all_node;
    struct list_head run_node;
};

/* 포터블 C11: 주소 계산 + sizeof 안의 포인터 비교로 타입 검사 (실행되지 않음) */
#define container_of(ptr, type, member)                     \
    ((void)sizeof((ptr) == &((type *)0)->member),           \
     (type *)(void *)((char *)(ptr) - offsetof(type, member)))

/* Linux include/linux/container_of.h 와 같은 모양 (GNU 확장: 문장 표현식, typeof, builtin) */
#define container_of_gnu(ptr, type, member) ({                                      \
    void *mptr__ = (void *)(ptr);                                                    \
    _Static_assert(__builtin_types_compatible_p(__typeof__(*(ptr)),                  \
                                                __typeof__(((type *)0)->member)) ||  \
                   __builtin_types_compatible_p(__typeof__(*(ptr)), void),           \
                   "pointer type mismatch in container_of()");                       \
    ((type *)((char *)mptr__ - offsetof(type, member))); })

#if UINTPTR_MAX == 0xFFFFFFFFu   /* 32-bit 타깃 (Cortex-M): 포인터 4 B */
_Static_assert(sizeof(struct list_head) == 8, "list_head = 2 x 4 B");
_Static_assert(offsetof(struct task, all_node) == 12, "id 4 + state 4 + kill 1 + pad 3");
_Static_assert(offsetof(struct task, run_node) == 20, "");
_Static_assert(sizeof(struct task) == 28, "");
#else                            /* 64-bit (이 Mac) */
_Static_assert(sizeof(struct list_head) == 16, "list_head = 2 x 8 B");
_Static_assert(offsetof(struct task, all_node) == 16, "id 4 + state 4 + kill 1 + pad 7");
_Static_assert(offsetof(struct task, run_node) == 32, "");
_Static_assert(sizeof(struct task) == 48, "");
#endif

#if __STDC_HOSTED__
#include <stdio.h>
#include <stdlib.h>
#include <malloc/malloc.h>   /* macOS malloc_size(): 실제로 잡힌 블록 크기 */

/* 비침입형(std::list<task*> 식) 노드: 링크 + 객체 포인터를 따로 할당 */
struct ext_node { struct ext_node *next, *prev; struct task *obj; };

int main(void) {
    static struct task t = {.id = 7};
    struct list_head *a = &t.all_node, *r = &t.run_node;
    printf("sizeof(struct list_head) = %zu, sizeof(struct task) = %zu\n", sizeof(struct list_head),
           sizeof(struct task));
    printf("offsetof(task, kill) = %zu, all_node = %zu, run_node = %zu\n", offsetof(struct task, kill),
           offsetof(struct task, all_node), offsetof(struct task, run_node));
    printf("&t           = %p\n", (void *)&t);
    printf("&t.all_node  = %p  (&t + %td)\n", (void *)a, (char *)a - (char *)&t);
    printf("&t.run_node  = %p  (&t + %td)\n", (void *)r, (char *)r - (char *)&t);
    struct task *x = container_of(a, struct task, all_node), *y = container_of(r, struct task, run_node);
    struct task *z = container_of_gnu(r, struct task, run_node);
    printf("container_of(all_node) = %p, container_of(run_node) = %p, gnu = %p, id = %d\n", (void *)x,
           (void *)y, (void *)z, y->id);
    printf("naive cast (struct task *)&t.run_node = %p  -> 32 B 어긋남\n", (void *)(struct task *)(void *)r);
    /* 삭제된 노드(LIST_POISON1 = 0x100)를 non-safe 순회가 따라가면 계산되는 가짜 객체 주소 */
    printf("poison 0x100 에서 container_of(.., all_node) -> 0x%zx, 그 ->kill 읽기 주소 0x%zx\n",
           (size_t)0x100 - offsetof(struct task, all_node),
           (size_t)0x100 - offsetof(struct task, all_node) + offsetof(struct task, kill));

    /* 메모리: task 1개가 리스트 2개에 속할 때 */
    void *p1 = malloc(sizeof(struct ext_node)), *p2 = malloc(sizeof(struct ext_node));
    printf("intrusive: 링크 2 x %zu = %zu B (task 안), 추가 할당 0회\n", sizeof(struct list_head),
           2 * sizeof(struct list_head));
    printf("external : ext_node %zu B 요청 -> malloc_size %zu B, x2 = %zu B + 할당 2회 + 포인터 한 번 더 따라가기\n",
           sizeof(struct ext_node), malloc_size(p1), malloc_size(p1) + malloc_size(p2));
    free(p1);
    free(p2);

#ifdef BAD_TYPE
    int *wrong = &t.id;
    (void)container_of(wrong, struct task, run_node);       /* int* 를 list_head 멤버로 착각 */
#endif
#ifdef BAD_TYPE_GNU
    int *wrong2 = &t.id;
    (void)container_of_gnu(wrong2, struct task, run_node);
#endif
    return 0;
}
#endif
