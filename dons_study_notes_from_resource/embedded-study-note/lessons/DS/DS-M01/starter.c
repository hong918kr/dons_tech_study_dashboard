/* DS-M01 침입형 리스트: Linux list_head 스타일
 * 링크(list_head)가 객체 안에 박혀 있고(intrusive), container_of 로 "링크 주소 → 객체 주소" 를 되찾는다.
 * 한 task 가 all 리스트(all_node)와 ready/wait 리스트(run_node)에 동시에 속한다. malloc 없음.
 * __typeof__ 는 GCC/Clang 확장 (C23 에선 표준 typeof). 그 외는 순수 C11.
 * Linux 의 __list_add / __list_del 은 '__' 예약 이름(C11 7.1.3)이라 list_add_between / list_unlink 로 바꿨다.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct list_head {
    struct list_head *next, *prev;
};

/* list_del 후 링크에 넣는 '독 주소'. 매핑되지 않은 주소라 지운 노드를 쓰면 바로 fault (Linux poison.h 와 같은 값) */
#define LIST_POISON1 ((struct list_head *)(uintptr_t)0x100)
#define LIST_POISON2 ((struct list_head *)(uintptr_t)0x122)

/* ---------------- Part A: container_of, 기본 연산, 순회 ---------------- */
/* ptr = 객체 안 member 의 주소 → 객체 주소. sizeof 안의 비교는 실행되지 않는 컴파일 타임 타입 검사:
 * ptr 이 member 와 다른 타입의 포인터면 "comparison of distinct pointer types" 진단이 나온다. */
#define container_of(ptr, type, member)                     \
    ((type *)NULL) /* TODO: offsetof 로 되돌아가기 + 타입 검사 */

#define list_entry(ptr, type, member) container_of(ptr, type, member)
#define list_first_entry(head, type, member) list_entry((head)->next, type, member)
#define list_last_entry(head, type, member) list_entry((head)->prev, type, member)
#define list_next_entry(pos, member) list_entry((pos)->member.next, __typeof__(*(pos)), member)

/* head 의 모든 객체를 앞에서부터. 끝 조건: pos 의 member 가 head(sentinel) 자신이면 한 바퀴 끝. */
#define list_for_each_entry(pos, head, member)                    \
    for (pos = NULL, (void)(head);  /* TODO: 첫 객체부터 */         \
         pos != NULL;               /* TODO: 끝 조건 */            \
         pos = NULL)                /* TODO: 다음 객체로 */

/* 순회 중 pos 를 지우거나 다른 리스트로 옮겨도 되는 버전: 다음 객체 n 을 미리 저장 */
#define list_for_each_entry_safe(pos, n, head, member)               \
    for (n = NULL, pos = n, (void)(head); /* TODO: pos 와 n 을 함께 */ \
         pos != NULL;                     /* TODO */                  \
         pos = NULL)                      /* TODO */

static inline void INIT_LIST_HEAD(struct list_head *h) {
    (void)h; /* TODO: 빈 리스트 = 자기 고리 */
}
static inline bool list_empty(const struct list_head *h) {
    (void)h;
    return false; /* TODO */
}
/* 이웃한 prev, next 사이에 n 을 끼운다 (Linux __list_add) */
static inline void list_add_between(struct list_head *n, struct list_head *prev, struct list_head *next) {
    (void)n; (void)prev; (void)next; /* TODO: 포인터 4개 (DS-B06 insert_between) */
}
/* head 바로 뒤 = 맨 앞 (스택) */
static inline void list_add(struct list_head *n, struct list_head *head) {
    (void)n; (void)head; (void)list_add_between; /* TODO */
}
/* head 바로 앞 = 맨 뒤 (큐) */
static inline void list_add_tail(struct list_head *n, struct list_head *head) {
    (void)n; (void)head; /* TODO */
}

/* ---------------- Part B: 삭제 · 이동 · splice ---------------- */
/* prev 와 next 를 직접 잇는다 (Linux __list_del) */
static inline void list_unlink(struct list_head *prev, struct list_head *next) {
    (void)prev; (void)next; /* TODO */
}
static inline void list_del_entry(struct list_head *e) {
    (void)e; (void)list_unlink; /* TODO */
}
/* 떼고 독 주소를 넣는다 → 재사용 버그가 즉시 터진다 */
static inline void list_del(struct list_head *e) {
    (void)e; (void)list_del_entry; /* TODO */
}
/* 떼고 자기 고리로 → list_empty(e) 로 "어디에도 없음" 을 물을 수 있다 */
static inline void list_del_init(struct list_head *e) {
    (void)e; /* TODO */
}
/* 지금 있는 리스트(어디든)에서 떼어 head 의 맨 앞 / 맨 뒤로 */
static inline void list_move(struct list_head *e, struct list_head *head) {
    (void)e; (void)head; /* TODO */
}
static inline void list_move_tail(struct list_head *e, struct list_head *head) {
    (void)e; (void)head; /* TODO */
}
/* list 의 노드 전부(첫~끝)를 prev 와 next 사이에 끼운다. list 는 비어 있지 않아야 한다. */
static inline void list_splice_between(const struct list_head *list, struct list_head *prev,
                                       struct list_head *next) {
    (void)list; (void)prev; (void)next; /* TODO: 이음매 두 개 = 포인터 4개 */
}
/* list 를 head 맨 앞에 붙인다. list 의 head 는 갱신하지 않는다(낡은 포인터가 남음!) */
static inline void list_splice(const struct list_head *list, struct list_head *head) {
    (void)list; (void)head; (void)list_splice_between; /* TODO */
}
/* 붙이고 list 를 빈 리스트로 다시 초기화 */
static inline void list_splice_init(struct list_head *list, struct list_head *head) {
    (void)list; (void)head; /* TODO */
}
static inline void list_splice_tail_init(struct list_head *list, struct list_head *head) {
    (void)list; (void)head; /* TODO */
}

/* ---------------- Part C: 두 리스트에 동시에 속한 task — 미니 스케줄러 ---------------- */
enum { T_DEAD, T_READY, T_BLOCKED };
struct task {
    int id;
    int state;
    bool kill;                  /* reap 대상 표시 */
    struct list_head all_node;  /* sched.all: 생성 순서. 살아 있는 동안 항상 여기 */
    struct list_head run_node;  /* sched.ready 또는 sched.wait 중 정확히 하나 */
};
struct sched {
    struct list_head all, ready, wait;
    int nr;                     /* 살아 있는 task 수 */
};

static void sched_init(struct sched *s) {
    INIT_LIST_HEAD(&s->all);
    INIT_LIST_HEAD(&s->ready);
    INIT_LIST_HEAD(&s->wait);
    s->nr = 0;
}
/* all 꼬리 + ready 꼬리에 동시에 넣는다 */
static void task_create(struct sched *s, struct task *t, int id) {
    t->id = id;
    t->state = T_READY;
    t->kill = false;
    (void)s; /* TODO: 두 리스트에 넣고 nr++ */
}
/* 라운드 로빈: ready 맨 앞을 골라 맨 뒤로 돌린다. 없으면 NULL */
static struct task *sched_pick(struct sched *s) {
    (void)s;
    return NULL; /* TODO */
}
/* ready → wait 꼬리 */
static void task_block(struct sched *s, struct task *t) {
    (void)s; (void)t; /* TODO */
}
/* wait → ready 꼬리 */
static void task_wake(struct sched *s, struct task *t) {
    (void)s; (void)t; /* TODO */
}
/* wait 전체를 순서 그대로 ready 꼬리에 O(1) 로 붙인다 (상태 갱신 순회는 O(k)). 깨운 수 반환 */
static int sched_wake_all(struct sched *s) {
    (void)s;
    return 0; /* TODO */
}
/* ready 를 돌면서 pred 가 참인 task 를 wait 꼬리로 옮긴다 (순회 중 이동 → _safe). 옮긴 수 반환 */
static int sched_block_if(struct sched *s, bool (*pred)(const struct task *)) {
    (void)s; (void)pred;
    return 0; /* TODO */
}
/* all 을 돌면서 kill 표시된 task 를 두 리스트 모두에서 뺀다. run_node 가 ready/wait 어디에 있든 list_del 하나 */
static int sched_reap(struct sched *s) {
    (void)s;
    return 0; /* TODO */
}

/* ---------------- 테스트 도우미 (심판) ---------------- */
/* container_of 를 쓰지 않고 raw 링크만 걷는다 → 학생 매크로와 독립. 노드를 pool[i] 의 all_node(which=0) 또는
 * run_node(which=1) 와 주소 비교로만 식별하고, 알려진 노드만 역참조한다. 모든 링크의 next->prev == 자기,
 * max 개 초과·모르는 주소(NULL, poison 포함)면 -1. */
static int node_index(const struct list_head *p, const struct task *pool, int npool, int which) {
    for (int i = 0; i < npool; i++)
        if (p == (which ? &pool[i].run_node : &pool[i].all_node)) return i;
    return -1;
}
static int walk_ids(const struct list_head *head, const struct task *pool, int npool, int which,
                    int *out, int max) {
    const struct list_head *p = head;
    for (int k = 0;; k++) {
        const struct list_head *nx = p->next;
        if (nx == head) return head->prev == p ? k : -1;
        int i = node_index(nx, pool, npool, which);
        if (k >= max || i < 0 || nx->prev != p) return -1;
        out[k] = i;
        p = nx;
    }
}
static bool ids_eq(const struct list_head *head, const struct task *pool, int npool, int which,
                   const int *want, int n) {
    int got[32];
    if (walk_ids(head, pool, npool, which, got, 32) != n) return false;
    for (int i = 0; i < n; i++)
        if (got[i] != want[i]) return false;
    return true;
}

/* 랜덤 테스트: 16 task 슬롯, 모델 = 슬롯 번호 배열 3개 (all / ready / wait 순서) */
#define NPOOL 16
static struct task R[NPOOL];
static int m_all[NPOOL], m_rdy[NPOOL], m_wt[NPOOL], n_all, n_rdy, n_wt;
static bool alive[NPOOL];
static long op_count[7], moved_total, killed_total;
static int max_alive;

static uint32_t rng = 0x1BADB002u;
static uint32_t xs32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static int g_mod, g_rem;
static bool pred_mod(const struct task *t) { return t->id % g_mod == g_rem; }

static void m_erase(int *a, int *n, int v) {
    int k = 0;
    while (k < *n && a[k] != v) k++;
    for (; k < *n - 1; k++) a[k] = a[k + 1];
    (*n)--;
}
static bool sched_matches(const struct sched *s) {
    if (!ids_eq(&s->all, R, NPOOL, 0, m_all, n_all) || !ids_eq(&s->ready, R, NPOOL, 1, m_rdy, n_rdy) ||
        !ids_eq(&s->wait, R, NPOOL, 1, m_wt, n_wt) || s->nr != n_all || n_all != n_rdy + n_wt)
        return false;
    for (int k = 0; k < n_rdy; k++) if (R[m_rdy[k]].state != T_READY) return false;
    for (int k = 0; k < n_wt; k++) if (R[m_wt[k]].state != T_BLOCKED) return false;
    for (int i = 0; i < NPOOL; i++)
        if (!alive[i] && (R[i].state != T_DEAD || R[i].all_node.next != LIST_POISON1 ||
                          R[i].run_node.prev != LIST_POISON2))
            return false;
    return true;
}

static bool random_test(int ops) {
    static struct sched s;
    int next_id = 100;
    sched_init(&s);
    for (int i = 0; i < NPOOL; i++) {
        R[i] = (struct task){.state = T_DEAD};
        R[i].all_node.next = R[i].run_node.next = LIST_POISON1;
        R[i].all_node.prev = R[i].run_node.prev = LIST_POISON2;
    }
    for (int step = 0; step < ops; step++) {
        uint32_t r = xs32();
        int op = (int)(r % 16), pick = (int)(r >> 16);
        if (op < 4) {                                    /* 0-3 create */
            int i = pick % NPOOL, tries = 0;
            while (alive[i] && tries++ < NPOOL) i = (i + 1) % NPOOL;
            if (alive[i]) continue;
            task_create(&s, &R[i], next_id++);
            alive[i] = true;
            m_all[n_all++] = i;
            m_rdy[n_rdy++] = i;
            op_count[0]++;
        } else if (op < 7) {                             /* 4-6 pick (라운드 로빈) */
            struct task *t = sched_pick(&s);
            if (n_rdy == 0) { if (t) return false; continue; }
            if (t != &R[m_rdy[0]]) { printf("  pick: 다른 task\n"); return false; }
            int i = m_rdy[0];
            m_erase(m_rdy, &n_rdy, i);
            m_rdy[n_rdy++] = i;
            op_count[1]++;
        } else if (op < 10) {                            /* 7-9 block 임의 ready task */
            if (n_rdy == 0) continue;
            int i = m_rdy[pick % n_rdy];
            task_block(&s, &R[i]);
            m_erase(m_rdy, &n_rdy, i);
            m_wt[n_wt++] = i;
            op_count[2]++;
        } else if (op < 12) {                            /* 10-11 wake 임의 wait task */
            if (n_wt == 0) continue;
            int i = m_wt[pick % n_wt];
            task_wake(&s, &R[i]);
            m_erase(m_wt, &n_wt, i);
            m_rdy[n_rdy++] = i;
            op_count[3]++;
        } else if (op == 12) {                           /* wake_all */
            if (sched_wake_all(&s) != n_wt) return false;
            for (int k = 0; k < n_wt; k++) m_rdy[n_rdy++] = m_wt[k];
            n_wt = 0;
            op_count[4]++;
        } else if (op == 13) {                           /* block_if: id % m == r */
            g_mod = 2 + pick % 3;
            g_rem = (pick >> 4) % g_mod;
            int expect = 0;
            for (int k = 0; k < n_rdy;) {
                int i = m_rdy[k];
                if (R[i].id % g_mod == g_rem) { m_erase(m_rdy, &n_rdy, i); m_wt[n_wt++] = i; expect++; }
                else k++;
            }
            if (sched_block_if(&s, pred_mod) != expect) return false;
            moved_total += expect;
            op_count[5]++;
        } else {                                         /* 14-15 kill 1~3개 표시 후 reap */
            if (n_all == 0) continue;
            int want = 1 + pick % 3, marked = 0;
            for (int k = 0; k < want; k++) {
                int i = m_all[(pick >> (2 + k * 4)) % n_all];
                if (!R[i].kill) { R[i].kill = true; marked++; }
            }
            if (sched_reap(&s) != marked) return false;
            for (int i = 0; i < NPOOL; i++) {
                if (alive[i] && R[i].kill) {
                    int in_rdy = 0;
                    for (int k = 0; k < n_rdy; k++) in_rdy |= m_rdy[k] == i;
                    m_erase(m_all, &n_all, i);
                    if (in_rdy) m_erase(m_rdy, &n_rdy, i); else m_erase(m_wt, &n_wt, i);
                    alive[i] = false;
                }
            }
            killed_total += marked;
            op_count[6]++;
        }
        if (n_all > max_alive) max_alive = n_all;
        if (!sched_matches(&s)) {
            printf("  random_test: step %d op %d 에서 불일치\n", step, op);
            return false;
        }
    }
    return true;
}

static int g_pass, g_total;
#define CHECK(expr) do { g_total++; if (expr) { g_pass++; } \
    else printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } while (0)

static struct task T[10];
static bool odd_id(const struct task *t) { return t->id % 2 != 0; }

int main(void) {
    for (int i = 0; i < 10; i++) T[i].id = i;
    struct task *t, *n;

    /* Part A: container_of — 어느 링크에서 출발해도 같은 객체 */
    CHECK(offsetof(struct task, run_node) > offsetof(struct task, all_node));
    CHECK(container_of(&T[3].all_node, struct task, all_node) == &T[3]);
    CHECK(container_of(&T[3].run_node, struct task, run_node) == &T[3]);
    CHECK((void *)&T[3].run_node != (void *)&T[3]);      /* 단순 캐스트로는 안 된다 */

    struct list_head all = {0}, ready = {0}, wait = {0};
    INIT_LIST_HEAD(&all);
    INIT_LIST_HEAD(&ready);
    INIT_LIST_HEAD(&wait);
    CHECK(list_empty(&all) && all.next == &all && all.prev == &all);
    for (int i = 0; i < 5; i++) list_add_tail(&T[i].all_node, &all);   /* 큐: 0 1 2 3 4 */
    list_add(&T[0].run_node, &ready);                                    /* 스택: 4 2 0 */
    list_add(&T[2].run_node, &ready);
    list_add(&T[4].run_node, &ready);
    CHECK(ids_eq(&all, T, 10, 0, (int[]){0, 1, 2, 3, 4}, 5));
    CHECK(ids_eq(&ready, T, 10, 1, (int[]){4, 2, 0}, 3));

    int got[16], k = 0, sum = 0;
    list_for_each_entry(t, &ready, run_node) got[k++] = t->id;
    CHECK(k == 3 && got[0] == 4 && got[1] == 2 && got[2] == 0);
    list_for_each_entry(t, &all, all_node) sum += t->id;
    CHECK(sum == 10);
    k = 0;
    list_for_each_entry(t, &wait, run_node) k++;           /* 빈 리스트: 0 회 */
    CHECK(k == 0);
    CHECK(list_first_entry(&ready, struct task, run_node) == &T[4] &&
          list_last_entry(&all, struct task, all_node) == &T[4]);   /* 두 리스트, 같은 객체 */

    /* Part B: del / del_init / move / splice */
    list_del(&T[2].run_node);                              /* ready 에서만 빠짐, all 은 그대로 */
    CHECK(ids_eq(&ready, T, 10, 1, (int[]){4, 0}, 2) && ids_eq(&all, T, 10, 0, (int[]){0, 1, 2, 3, 4}, 5));
    CHECK(T[2].run_node.next == LIST_POISON1 && T[2].run_node.prev == LIST_POISON2);
    list_del_init(&T[1].all_node);
    CHECK(list_empty(&T[1].all_node) && ids_eq(&all, T, 10, 0, (int[]){0, 2, 3, 4}, 4));
    list_add(&T[1].all_node, &all);                        /* del_init 한 노드는 바로 재사용 */
    CHECK(ids_eq(&all, T, 10, 0, (int[]){1, 0, 2, 3, 4}, 5));
    list_move_tail(&T[4].run_node, &wait);                 /* 다른 리스트로 */
    list_move(&T[0].run_node, &wait);
    CHECK(list_empty(&ready) && ids_eq(&wait, T, 10, 1, (int[]){0, 4}, 2));
    list_move_tail(&T[1].all_node, &all);                  /* 같은 리스트 안 = 회전 */
    CHECK(ids_eq(&all, T, 10, 0, (int[]){0, 2, 3, 4, 1}, 5));
    list_move(&T[0].all_node, &all);                       /* 이미 맨 앞 → 그대로 */
    CHECK(ids_eq(&all, T, 10, 0, (int[]){0, 2, 3, 4, 1}, 5));

    list_splice_init(&ready, &wait);                       /* 빈 src → 변화 없음 */
    CHECK(ids_eq(&wait, T, 10, 1, (int[]){0, 4}, 2) && list_empty(&ready));
    for (int i = 5; i < 8; i++) list_add_tail(&T[i].run_node, &ready);
    list_splice_init(&ready, &wait);                       /* 5 6 7 을 맨 앞에 */
    CHECK(ids_eq(&wait, T, 10, 1, (int[]){5, 6, 7, 0, 4}, 5) && list_empty(&ready));
    list_splice_tail_init(&wait, &ready);                  /* 빈 dst 로 전체 */
    CHECK(ids_eq(&ready, T, 10, 1, (int[]){5, 6, 7, 0, 4}, 5) && list_empty(&wait));
    struct list_head tmp = {0};
    INIT_LIST_HEAD(&tmp);
    list_add_tail(&T[8].run_node, &tmp);
    list_splice(&tmp, &ready);                             /* _init 없는 splice: tmp 는 낡은 포인터 */
    CHECK(ids_eq(&ready, T, 10, 1, (int[]){8, 5, 6, 7, 0, 4}, 6));
    CHECK(tmp.next == &T[8].run_node && !list_empty(&tmp));  /* 비지 않은 척 → 그래서 _init 이 있다 */
    INIT_LIST_HEAD(&tmp);

    /* Part B: 순회 중 삭제 — 짝수 id 지우기, 전부 지우기, 빈 리스트 */
    k = 0;
    list_for_each_entry_safe(t, n, &ready, run_node) {
        if (t->id % 2 == 0) { list_del(&t->run_node); k++; }
    }
    CHECK(k == 4 && ids_eq(&ready, T, 10, 1, (int[]){5, 7}, 2));
    k = 0;
    list_for_each_entry_safe(t, n, &ready, run_node) { list_del_init(&t->run_node); k++; }
    CHECK(k == 2 && list_empty(&ready) && list_empty(&T[5].run_node));
    k = 0;
    list_for_each_entry_safe(t, n, &ready, run_node) k++;
    CHECK(k == 0);

    /* Part C: 미니 스케줄러 — 한 task 가 all + (ready|wait) 에 동시에 */
    static struct task S[8];
    struct sched sc = {0};
    sched_init(&sc);
    for (int i = 0; i < 5; i++) task_create(&sc, &S[i], i);
    CHECK(sc.nr == 5 && ids_eq(&sc.all, S, 8, 0, (int[]){0, 1, 2, 3, 4}, 5) &&
          ids_eq(&sc.ready, S, 8, 1, (int[]){0, 1, 2, 3, 4}, 5));
    k = 0;
    for (int i = 0; i < 6; i++) { struct task *p = sched_pick(&sc); got[k++] = p ? p->id : -1; }
    CHECK(got[0] == 0 && got[1] == 1 && got[4] == 4 && got[5] == 0);   /* 라운드 로빈 */
    task_block(&sc, &S[2]);
    task_block(&sc, &S[0]);
    CHECK(ids_eq(&sc.ready, S, 8, 1, (int[]){1, 3, 4}, 3) && ids_eq(&sc.wait, S, 8, 1, (int[]){2, 0}, 2) &&
          S[2].state == T_BLOCKED && ids_eq(&sc.all, S, 8, 0, (int[]){0, 1, 2, 3, 4}, 5));
    CHECK(sched_block_if(&sc, odd_id) == 2 && ids_eq(&sc.ready, S, 8, 1, (int[]){4}, 1) &&
          ids_eq(&sc.wait, S, 8, 1, (int[]){2, 0, 1, 3}, 4));
    task_wake(&sc, &S[0]);
    CHECK(ids_eq(&sc.ready, S, 8, 1, (int[]){4, 0}, 2) && S[0].state == T_READY);
    S[1].kill = S[4].kill = true;                          /* 1 은 wait, 4 는 ready 에 있다 */
    CHECK(sched_reap(&sc) == 2 && sc.nr == 3 && S[1].state == T_DEAD);
    CHECK(ids_eq(&sc.all, S, 8, 0, (int[]){0, 2, 3}, 3) && ids_eq(&sc.ready, S, 8, 1, (int[]){0}, 1) &&
          ids_eq(&sc.wait, S, 8, 1, (int[]){2, 3}, 2));
    CHECK(sched_wake_all(&sc) == 2 && list_empty(&sc.wait) && S[3].state == T_READY &&
          ids_eq(&sc.ready, S, 8, 1, (int[]){0, 2, 3}, 3));

    CHECK(random_test(20000));
    printf("random: create %ld, pick %ld, block %ld, wake %ld, wake_all %ld, block_if %ld (moved %ld), "
           "reap %ld (killed %ld), max alive %d\n",
           op_count[0], op_count[1], op_count[2], op_count[3], op_count[4], op_count[5], moved_total,
           op_count[6], killed_total, max_alive);

    printf("%d/%d passed\n", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
