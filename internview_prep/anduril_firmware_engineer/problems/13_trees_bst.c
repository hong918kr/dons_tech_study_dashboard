// 13_trees_bst.c  —  PRACTICE STUB (여기 빈칸을 채우세요)
// 트리 & 이진탐색트리 (Trees & BST)  —  Q1~Q9
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 13_trees_bst.c -o /tmp/andb_13 && /tmp/andb_13
// 각 함수의 // TODO 를 구현하고 다시 빌드/실행해 FAIL -> PASS 로 바꾸세요.
// 지금 상태로도 컴파일/실행은 되며(placeholder 반환), 대부분 [FAIL] 로 나옵니다.
// C11, STL 없음 — 노드/큐/스택을 파일 안에 직접 정의. tree_build 헬퍼는 제공됨.
//
// 임베디드 힌트:
//   - 순회는 값 기록 시점만 다르다: pre(루트먼저)/in(중간)/post(마지막).
//   - 반복 inorder = 노드 포인터 명시적 스택. BFS = 배열 FIFO 큐(좌->우 enqueue).
//   - 균형 판정은 높이를 반환하되 불균형이면 -1 조기 전파 -> O(n).
//   - BST: 왼 < 루트 < 오. 유효성은 (lo,hi) 경계 좁히기, LCA 는 값 비교로 O(h).
// ---------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

// ===========================================================================
// 테스트 하네스 (PASS/FAIL)
// ===========================================================================
static int g_pass = 0;
static int g_fail = 0;
#define T(label, cond) do {                                   \
    if (cond) { printf("  [PASS] %s\n", (label)); g_pass++; } \
    else      { printf("  [FAIL] %s\n", (label)); g_fail++; } \
} while (0)

// ===========================================================================
// 트리 노드 + 유틸 (제공됨)
// ===========================================================================
#define NIL INT_MIN     // 배열 빌드에서 자식 없음(null) 센티넬

typedef struct tnode {
    int           val;
    struct tnode *left;
    struct tnode *right;
} tnode_t;

static tnode_t *node_new(int val) {
    tnode_t *n = (tnode_t *)malloc(sizeof *n);
    if (!n) { perror("malloc"); exit(1); }
    n->val = val;
    n->left = n->right = NULL;
    return n;
}

static size_t tree_count(const tnode_t *root) {
    return root ? 1 + tree_count(root->left) + tree_count(root->right) : 0;
}

static void tree_free(tnode_t *root) {
    if (!root) return;
    tree_free(root->left);
    tree_free(root->right);
    free(root);
}

// (하네스 헬퍼, 제공됨) 레벨오더 배열 -> 트리. NIL 은 자식 없음. 내부 부모 큐 사용.
static tnode_t *tree_build(const int *arr, size_t n) {
    if (n == 0 || arr[0] == NIL) return NULL;
    tnode_t *root = node_new(arr[0]);
    tnode_t **q = (tnode_t **)malloc(n * sizeof *q);
    size_t head = 0, tail = 0;
    q[tail++] = root;
    size_t i = 1;
    while (i < n && head < tail) {
        tnode_t *cur = q[head++];
        if (i < n) { if (arr[i] != NIL) { cur->left  = node_new(arr[i]); q[tail++] = cur->left;  } i++; }
        if (i < n) { if (arr[i] != NIL) { cur->right = node_new(arr[i]); q[tail++] = cur->right; } i++; }
    }
    free(q);
    return root;
}

// ===========================================================================
// Q1. 재귀 순회 (pre/in/post) -> out[*k] 에 채우고 *k 전진
//   pre = 루트->왼->오,  in = 왼->루트->오,  post = 왼->오->루트.
//   BST inorder = 오름차순.  O(n) 시간, O(h) 스택.
// ===========================================================================
void tree_preorder(const tnode_t *root, int *out, size_t *k) {
    (void)root; (void)out; (void)k;
    // TODO: 루트 값 기록 -> 왼쪽 재귀 -> 오른쪽 재귀
}

void tree_inorder(const tnode_t *root, int *out, size_t *k) {
    (void)root; (void)out; (void)k;
    // TODO: 왼쪽 재귀 -> 루트 값 기록 -> 오른쪽 재귀
}

void tree_postorder(const tnode_t *root, int *out, size_t *k) {
    (void)root; (void)out; (void)k;
    // TODO: 왼쪽 재귀 -> 오른쪽 재귀 -> 루트 값 기록
}

// ===========================================================================
// Q2. 반복 inorder — 명시적 스택. 채운 개수 반환.  O(n) 시간, O(h) 스택.
//   패턴: 왼쪽 끝까지 push -> pop 후 기록 -> 오른쪽으로.
//   힌트: const tnode_t **stk = malloc(tree_count(root) * sizeof *stk);
// ===========================================================================
size_t tree_inorder_iter(const tnode_t *root, int *out) {
    (void)root; (void)out; (void)tree_count;   // tree_count 로 스택 크기 산정
    // TODO: 노드 포인터 스택으로 재귀 없이 inorder
    return 0;   // placeholder
}

// ===========================================================================
// Q3. 레벨오더 BFS — 배열 FIFO 큐. 채운 개수 반환.  O(n) 시간/공간.
//   dequeue -> 값 기록 -> 좌/우 자식 enqueue.
// ===========================================================================
size_t tree_level_order(const tnode_t *root, int *out) {
    (void)root; (void)out;
    // TODO: 큐 용량 = tree_count(root). 루트 enqueue 후 빌 때까지 반복.
    return 0;   // placeholder
}

// ===========================================================================
// Q4. 최대 깊이.  빈 트리 0, 그 외 1 + max(왼, 오).  O(n).
// ===========================================================================
int tree_max_depth(const tnode_t *root) {
    (void)root;
    // TODO
    return 0;   // placeholder
}

// ===========================================================================
// Q5. 트리 뒤집기(mirror). 각 노드 좌/우 교환, 루트 반환.  O(n).
// ===========================================================================
tnode_t *tree_invert(tnode_t *root) {
    // TODO: 서브트리 재귀 뒤집고 root->left <-> root->right 교환
    return root;   // placeholder (제자리 변형 아님)
}

// ===========================================================================
// Q6. 높이 균형 여부. 모든 노드에서 |왼-오| <= 1.
//   높이를 반환하되 불균형이면 -1 로 조기 전파 -> O(n).
// ===========================================================================
static int balanced_height(const tnode_t *root) {
    (void)root;
    // TODO: 왼/오 높이 구해 -1 전파, |차| > 1 이면 -1, 아니면 max+1
    return 0;   // placeholder
}

bool tree_is_balanced(const tnode_t *root) {
    return balanced_height(root) >= 0;
}

// ===========================================================================
// Q7. BST 삽입 & 탐색.  왼 < 루트 < 오. 삽입은 새 루트 반환(중복 무시). O(h).
// ===========================================================================
tnode_t *bst_insert(tnode_t *root, int val) {
    (void)val;
    // TODO: 빈 자리면 node_new, 아니면 값 비교로 좌/우 재귀 삽입
    return root;   // placeholder
}

bool bst_search(const tnode_t *root, int val) {
    (void)root; (void)val;
    // TODO: 값 비교로 한 방향씩 내려가며 탐색
    return false;  // placeholder
}

// ===========================================================================
// Q8. BST 유효성. 조상 경계까지 봐야 함. (lo,hi) 열린구간 좁히기.
//   INT 경계 오버플로 회피 위해 long 사용.  O(n).
// ===========================================================================
static bool bst_valid_bounds(const tnode_t *root, long lo, long hi) {
    (void)root; (void)lo; (void)hi;
    // TODO: 값이 (lo,hi) 안인지 + 왼(상한=현재)/오(하한=현재) 재귀
    return false;  // placeholder
}

bool bst_is_valid(const tnode_t *root) {
    return bst_valid_bounds(root, LONG_MIN, LONG_MAX);
}

// ===========================================================================
// Q9. BST 최소 공통 조상(LCA). 두 값이 모두 작으면 왼, 모두 크면 오,
//   갈라지면 현재가 LCA.  O(h) 시간, O(1) 공간. (a,b 존재 가정.)
// ===========================================================================
tnode_t *bst_lca(tnode_t *root, int a, int b) {
    (void)a; (void)b;
    // TODO: 값 비교로 좌/오 내려가다 분기점 반환
    return root;   // placeholder
}

// ===========================================================================
// 테스트 헬퍼
// ===========================================================================
static bool arr_eq(const int *a, const int *b, size_t n) {
    for (size_t i = 0; i < n; ++i) if (a[i] != b[i]) return false;
    return true;
}

// ===========================================================================
// main : 모든 케이스 PASS/FAIL
// ===========================================================================
int main(void) {
    int buf[64];

    int a1[] = {1, 2, 3, 4, 5, NIL, 6};
    tnode_t *t = tree_build(a1, sizeof a1 / sizeof a1[0]);

    printf("== Q1. recursive traversals ==\n");
    { size_t k = 0; tree_preorder(t, buf, &k);
      T("preorder  -> 1 2 4 5 3 6", k == 6 && arr_eq(buf, (int[]){1,2,4,5,3,6}, 6)); }
    { size_t k = 0; tree_inorder(t, buf, &k);
      T("inorder   -> 4 2 5 1 3 6", k == 6 && arr_eq(buf, (int[]){4,2,5,1,3,6}, 6)); }
    { size_t k = 0; tree_postorder(t, buf, &k);
      T("postorder -> 4 5 2 6 3 1", k == 6 && arr_eq(buf, (int[]){4,5,2,6,3,1}, 6)); }
    { size_t k = 0; tree_preorder(NULL, buf, &k);
      T("preorder empty -> k==0", k == 0); }

    printf("== Q2. iterative inorder (stack) ==\n");
    { size_t k = tree_inorder_iter(t, buf);
      T("iter inorder -> 4 2 5 1 3 6", k == 6 && arr_eq(buf, (int[]){4,2,5,1,3,6}, 6)); }
    T("iter inorder empty -> 0", tree_inorder_iter(NULL, buf) == 0);
    { tnode_t *one = node_new(42);
      size_t k = tree_inorder_iter(one, buf);
      T("iter inorder single -> [42]", k == 1 && buf[0] == 42);
      tree_free(one); }

    printf("== Q3. level-order BFS (queue) ==\n");
    { size_t k = tree_level_order(t, buf);
      T("level order -> 1 2 3 4 5 6", k == 6 && arr_eq(buf, (int[]){1,2,3,4,5,6}, 6)); }
    T("level order empty -> 0", tree_level_order(NULL, buf) == 0);

    printf("== Q4. max depth ==\n");
    T("depth of sample -> 3", tree_max_depth(t) == 3);
    T("depth empty -> 0",     tree_max_depth(NULL) == 0);
    { tnode_t *one = node_new(7);
      T("depth single -> 1", tree_max_depth(one) == 1);
      tree_free(one); }

    printf("== Q5. invert tree ==\n");
    tree_invert(t);
    { size_t k = tree_level_order(t, buf);
      T("invert -> level 1 3 2 6 5 4", k == 6 && arr_eq(buf, (int[]){1,3,2,6,5,4}, 6)); }
    tree_invert(t);
    T("invert twice -> restored",
      (tree_level_order(t, buf), arr_eq(buf, (int[]){1,2,3,4,5,6}, 6)));
    T("invert empty -> NULL", tree_invert(NULL) == NULL);
    tree_free(t);

    printf("== Q6. is balanced ==\n");
    { int b[] = {1, 2, 3, 4, 5, NIL, 6};
      tnode_t *bt = tree_build(b, sizeof b / sizeof b[0]);
      T("balanced sample -> true", tree_is_balanced(bt));
      tree_free(bt); }
    { int sk[] = {1, 2, NIL, 3};
      tnode_t *st = tree_build(sk, sizeof sk / sizeof sk[0]);
      T("skewed 1->2->3 -> false", !tree_is_balanced(st));
      tree_free(st); }
    T("empty balanced -> true", tree_is_balanced(NULL));

    printf("== Q7. BST insert & search ==\n");
    tnode_t *bst = NULL;
    int ins[] = {5, 3, 8, 1, 4, 7, 9};
    for (size_t i = 0; i < sizeof ins / sizeof ins[0]; ++i)
        bst = bst_insert(bst, ins[i]);
    { size_t k = 0; tree_inorder(bst, buf, &k);
      T("BST inorder sorted -> 1 3 4 5 7 8 9",
        k == 7 && arr_eq(buf, (int[]){1,3,4,5,7,8,9}, 7)); }
    T("search 7 -> true",  bst_search(bst, 7));
    T("search 4 -> true",  bst_search(bst, 4));
    T("search 6 -> false", !bst_search(bst, 6));
    bst = bst_insert(bst, 5);
    { size_t k = 0; tree_inorder(bst, buf, &k);
      T("dup insert 5 -> unchanged (7 nodes)", k == 7); }
    tree_free(bst);

    printf("== Q8. validate BST ==\n");
    { int v[] = {5, 3, 8, 1, 4, 7, 9};
      tnode_t *vt = tree_build(v, sizeof v / sizeof v[0]);
      T("valid BST -> true", bst_is_valid(vt));
      tree_free(vt); }
    { int inv[] = {5, 1, 4, NIL, NIL, 3, 6};
      tnode_t *it = tree_build(inv, sizeof inv / sizeof inv[0]);
      T("invalid BST [5,1,4,_,_,3,6] -> false", !bst_is_valid(it));
      tree_free(it); }
    T("empty is valid BST -> true", bst_is_valid(NULL));
    { tnode_t *one = node_new(INT_MAX);
      T("single INT_MAX valid -> true", bst_is_valid(one));
      tree_free(one); }

    printf("== Q9. BST lowest common ancestor ==\n");
    int la[] = {6, 2, 8, 0, 4, 7, 9, NIL, NIL, 3, 5};
    tnode_t *lt = tree_build(la, sizeof la / sizeof la[0]);
    { tnode_t *r = bst_lca(lt, 2, 8); T("LCA(2,8) -> 6", r && r->val == 6); }
    { tnode_t *r = bst_lca(lt, 2, 4); T("LCA(2,4) -> 2 (ancestor)", r && r->val == 2); }
    { tnode_t *r = bst_lca(lt, 3, 5); T("LCA(3,5) -> 4", r && r->val == 4); }
    { tnode_t *r = bst_lca(lt, 0, 5); T("LCA(0,5) -> 2", r && r->val == 2); }
    tree_free(lt);

    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
