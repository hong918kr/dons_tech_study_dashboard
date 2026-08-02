// 13_trees_bst.c  —  REFERENCE SOLUTION
// 트리 & 이진탐색트리 (Trees & BST)  —  Q1~Q9
// ---------------------------------------------------------------------------
// 빌드: cc -std=c11 -Wall -Wextra 13_trees_bst.c -o /tmp/andb_13 && /tmp/andb_13
// C11, STL 없음 — 트리 노드 / BFS용 큐 / 반복 순회용 스택을 파일 안에 직접 정의.
//
// 임베디드 관점 핵심:
//   - 재귀 순회(pre/in/post)는 명령 파싱 트리·표현식 평가의 기본기.
//   - 반복 inorder = 명시적 스택 -> 콜스택이 얕은 MCU/ISR에서 재귀 대체.
//   - BFS = 명시적 큐(링 아님, 배열 FIFO) -> 상태 그래프/토폴로지 계층 탐색.
//   - BST 불변식(왼 < 루트 < 오)이 곧 정렬된 인덱스/룩업 테이블. LCA는 O(h).
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
// 트리 노드 + 유틸 (파일 자체 완결: 외부 헤더/STL 없음)
// ---------------------------------------------------------------------------
// 배열 빌드에서 자식 없음(null)을 나타내는 센티넬. 실제 값으로 INT_MIN 은 안 씀.
// ===========================================================================
#define NIL INT_MIN

typedef struct tnode {
    int           val;
    struct tnode *left;
    struct tnode *right;
} tnode_t;

static tnode_t *node_new(int val) {
    tnode_t *n = (tnode_t *)malloc(sizeof *n);
    if (!n) { perror("malloc"); exit(1); }   // 하네스에선 실패 시 즉시 중단
    n->val = val;
    n->left = n->right = NULL;
    return n;
}

// 노드 개수 — 큐/스택 버퍼 크기 산정에 사용. O(n).
static size_t tree_count(const tnode_t *root) {
    return root ? 1 + tree_count(root->left) + tree_count(root->right) : 0;
}

// 후위 순서로 전체 해제 (자식 먼저). O(n).
static void tree_free(tnode_t *root) {
    if (!root) return;
    tree_free(root->left);
    tree_free(root->right);
    free(root);
}

// (하네스 헬퍼) 레벨오더 배열로부터 트리 생성. NIL 은 자식 없음.
//   예: [1,2,3,NIL,4] ->      1
//                            / \
//                           2   3
//                            \
//                             4
// 내부에서 부모 큐(배열 FIFO)를 써서 좌/우 자식을 순서대로 붙인다. O(n).
static tnode_t *tree_build(const int *arr, size_t n) {
    if (n == 0 || arr[0] == NIL) return NULL;
    tnode_t *root = node_new(arr[0]);
    tnode_t **q = (tnode_t **)malloc(n * sizeof *q);   // 부모 큐
    size_t head = 0, tail = 0;
    q[tail++] = root;
    size_t i = 1;
    while (i < n && head < tail) {
        tnode_t *cur = q[head++];
        if (i < n) {                                    // 왼쪽 자식
            if (arr[i] != NIL) { cur->left  = node_new(arr[i]); q[tail++] = cur->left; }
            i++;
        }
        if (i < n) {                                    // 오른쪽 자식
            if (arr[i] != NIL) { cur->right = node_new(arr[i]); q[tail++] = cur->right; }
            i++;
        }
    }
    free(q);
    return root;
}

// ===========================================================================
// Q1. 재귀 순회 (pre/in/post) -> 배열에 채우기
// ---------------------------------------------------------------------------
// out[*k] 에 값을 쓰고 *k 를 전진. 방문 시점(값 기록)의 위치만 다르다:
//   pre  = 루트 -> 왼 -> 오,  in = 왼 -> 루트 -> 오,  post = 왼 -> 오 -> 루트.
// BST 를 inorder 로 돌면 오름차순 정렬 결과가 나온다. O(n) 시간, O(h) 스택.
// ===========================================================================
void tree_preorder(const tnode_t *root, int *out, size_t *k) {
    if (!root) return;
    out[(*k)++] = root->val;         // 루트 먼저
    tree_preorder(root->left,  out, k);
    tree_preorder(root->right, out, k);
}

void tree_inorder(const tnode_t *root, int *out, size_t *k) {
    if (!root) return;
    tree_inorder(root->left,  out, k);
    out[(*k)++] = root->val;         // 왼쪽 다 본 뒤 루트
    tree_inorder(root->right, out, k);
}

void tree_postorder(const tnode_t *root, int *out, size_t *k) {
    if (!root) return;
    tree_postorder(root->left,  out, k);
    tree_postorder(root->right, out, k);
    out[(*k)++] = root->val;         // 자식 다 본 뒤 루트
}

// ===========================================================================
// Q2. 반복 inorder — 명시적 스택
// ---------------------------------------------------------------------------
// 재귀 대신 노드 포인터 스택을 직접 관리한다. MCU/ISR 처럼 콜스택 깊이가
// 위험한 곳에서 재귀 순회를 대체. 채운 개수를 반환. O(n) 시간, O(h) 스택.
//   패턴: 현재에서 왼쪽 끝까지 밀어넣고 -> pop 후 기록 -> 오른쪽으로.
// ===========================================================================
size_t tree_inorder_iter(const tnode_t *root, int *out) {
    size_t n = tree_count(root);
    if (n == 0) return 0;
    const tnode_t **stk = (const tnode_t **)malloc(n * sizeof *stk);
    size_t top = 0, k = 0;
    const tnode_t *cur = root;
    while (cur || top > 0) {
        while (cur) { stk[top++] = cur; cur = cur->left; }  // 왼쪽 끝까지
        cur = stk[--top];                                   // 가장 왼쪽 pop
        out[k++] = cur->val;                                // 방문 기록
        cur = cur->right;                                   // 오른쪽 서브트리로
    }
    free(stk);
    return k;
}

// ===========================================================================
// Q3. 레벨오더 BFS — 명시적 큐(배열 FIFO)
// ---------------------------------------------------------------------------
// 큐에서 노드를 꺼내 기록하고 자식(좌->우)을 큐에 넣는다. 계층 순서 방문.
// 채운 개수 반환. 큐 용량 = 노드 수. O(n) 시간/공간.
// ===========================================================================
size_t tree_level_order(const tnode_t *root, int *out) {
    if (!root) return 0;
    size_t n = tree_count(root);
    const tnode_t **q = (const tnode_t **)malloc(n * sizeof *q);
    size_t head = 0, tail = 0, k = 0;
    q[tail++] = root;                        // enqueue 루트
    while (head < tail) {                     // 큐가 빌 때까지
        const tnode_t *cur = q[head++];       // dequeue
        out[k++] = cur->val;
        if (cur->left)  q[tail++] = cur->left;   // 좌 자식 enqueue
        if (cur->right) q[tail++] = cur->right;  // 우 자식 enqueue
    }
    free(q);
    return k;
}

// ===========================================================================
// Q4. 최대 깊이 (max depth)
// ---------------------------------------------------------------------------
// 빈 트리는 0, 그 외 1 + max(왼, 오). O(n) 시간, O(h) 스택.
// ===========================================================================
int tree_max_depth(const tnode_t *root) {
    if (!root) return 0;
    int l = tree_max_depth(root->left);
    int r = tree_max_depth(root->right);
    return (l > r ? l : r) + 1;
}

// ===========================================================================
// Q5. 트리 뒤집기 (invert / mirror)
// ---------------------------------------------------------------------------
// 각 노드의 좌/우 자식을 재귀적으로 맞바꾼다. 제자리 변형, 루트 반환. O(n).
// ===========================================================================
tnode_t *tree_invert(tnode_t *root) {
    if (!root) return NULL;
    tnode_t *l = tree_invert(root->left);   // 먼저 서브트리 뒤집고
    tnode_t *r = tree_invert(root->right);
    root->left  = r;                         // 좌우 교환
    root->right = l;
    return root;
}

// ===========================================================================
// Q6. 높이 균형 여부 (height-balanced)
// ---------------------------------------------------------------------------
// 모든 노드에서 |왼 높이 - 오 높이| <= 1 이면 균형. 높이를 반환하되 불균형이면
// -1 로 조기 전파해 O(n) 한 번 순회로 판정 (naive 재계산 O(n^2) 회피).
// ===========================================================================
static int balanced_height(const tnode_t *root) {
    if (!root) return 0;
    int l = balanced_height(root->left);
    if (l < 0) return -1;                    // 왼쪽에서 이미 불균형
    int r = balanced_height(root->right);
    if (r < 0) return -1;                    // 오른쪽에서 불균형
    int d = l > r ? l - r : r - l;
    if (d > 1) return -1;                    // 이 노드에서 불균형
    return (l > r ? l : r) + 1;
}

bool tree_is_balanced(const tnode_t *root) {
    return balanced_height(root) >= 0;
}

// ===========================================================================
// Q7. BST 삽입 & 탐색
// ---------------------------------------------------------------------------
// 불변식: 왼쪽 서브트리 < 노드 < 오른쪽 서브트리. 삽입은 자리 찾아 새 노드 연결
// (중복은 무시), 새 루트 반환. 탐색은 값 비교로 한 방향씩 내려간다. O(h).
// ===========================================================================
tnode_t *bst_insert(tnode_t *root, int val) {
    if (!root) return node_new(val);              // 빈 자리 = 삽입 지점
    if (val < root->val)      root->left  = bst_insert(root->left,  val);
    else if (val > root->val) root->right = bst_insert(root->right, val);
    // val == root->val : 중복 -> 무시
    return root;
}

bool bst_search(const tnode_t *root, int val) {
    while (root) {
        if (val == root->val) return true;
        root = (val < root->val) ? root->left : root->right;  // 한 방향으로
    }
    return false;
}

// ===========================================================================
// Q8. BST 유효성 검사 (validate BST)
// ---------------------------------------------------------------------------
// "노드 값이 왼 자식보다 크다" 국소 검사만으론 부족 — 조상 경계까지 봐야 한다.
// (lo, hi) 열린구간을 좁혀가며 검사. INT 경계 오버플로 회피 위해 long 사용.
// O(n) 시간, O(h) 스택.
// ===========================================================================
static bool bst_valid_bounds(const tnode_t *root, long lo, long hi) {
    if (!root) return true;
    if (root->val <= lo || root->val >= hi) return false;      // 경계 위반
    return bst_valid_bounds(root->left,  lo, root->val) &&      // 왼쪽 상한 = 현재
           bst_valid_bounds(root->right, root->val, hi);        // 오른쪽 하한 = 현재
}

bool bst_is_valid(const tnode_t *root) {
    return bst_valid_bounds(root, LONG_MIN, LONG_MAX);
}

// ===========================================================================
// Q9. BST 최소 공통 조상 (LCA on BST)
// ---------------------------------------------------------------------------
// BST 성질 활용: 두 값이 모두 현재보다 작으면 왼쪽, 모두 크면 오른쪽, 갈라지면
// (혹은 하나가 현재와 같으면) 현재가 LCA. O(h) 시간, O(1) 공간.
// (a, b 는 트리에 존재한다고 가정.)
// ===========================================================================
tnode_t *bst_lca(tnode_t *root, int a, int b) {
    while (root) {
        if (a < root->val && b < root->val)      root = root->left;
        else if (a > root->val && b > root->val) root = root->right;
        else return root;                         // 분기점 = LCA
    }
    return NULL;
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

    // 표준 트리:        1
    //                  / \
    //                 2   3
    //                / \   \
    //               4   5   6
    int a1[] = {1, 2, 3, 4, 5, NIL, 6};
    tnode_t *t = tree_build(a1, sizeof a1 / sizeof a1[0]);

    // -------- Q1 재귀 순회 --------
    printf("== Q1. recursive traversals ==\n");
    { size_t k = 0; tree_preorder(t, buf, &k);
      T("preorder  -> 1 2 4 5 3 6", k == 6 && arr_eq(buf, (int[]){1,2,4,5,3,6}, 6)); }
    { size_t k = 0; tree_inorder(t, buf, &k);
      T("inorder   -> 4 2 5 1 3 6", k == 6 && arr_eq(buf, (int[]){4,2,5,1,3,6}, 6)); }
    { size_t k = 0; tree_postorder(t, buf, &k);
      T("postorder -> 4 5 2 6 3 1", k == 6 && arr_eq(buf, (int[]){4,5,2,6,3,1}, 6)); }
    { size_t k = 0; tree_preorder(NULL, buf, &k);
      T("preorder empty -> k==0", k == 0); }

    // -------- Q2 반복 inorder --------
    printf("== Q2. iterative inorder (stack) ==\n");
    { size_t k = tree_inorder_iter(t, buf);
      T("iter inorder -> 4 2 5 1 3 6", k == 6 && arr_eq(buf, (int[]){4,2,5,1,3,6}, 6)); }
    T("iter inorder empty -> 0", tree_inorder_iter(NULL, buf) == 0);
    { tnode_t *one = node_new(42);
      size_t k = tree_inorder_iter(one, buf);
      T("iter inorder single -> [42]", k == 1 && buf[0] == 42);
      tree_free(one); }

    // -------- Q3 레벨오더 BFS --------
    printf("== Q3. level-order BFS (queue) ==\n");
    { size_t k = tree_level_order(t, buf);
      T("level order -> 1 2 3 4 5 6", k == 6 && arr_eq(buf, (int[]){1,2,3,4,5,6}, 6)); }
    T("level order empty -> 0", tree_level_order(NULL, buf) == 0);

    // -------- Q4 최대 깊이 --------
    printf("== Q4. max depth ==\n");
    T("depth of sample -> 3", tree_max_depth(t) == 3);
    T("depth empty -> 0",     tree_max_depth(NULL) == 0);
    { tnode_t *one = node_new(7);
      T("depth single -> 1", tree_max_depth(one) == 1);
      tree_free(one); }

    // -------- Q5 트리 뒤집기 --------
    printf("== Q5. invert tree ==\n");
    tree_invert(t);   // 좌우 미러
    { size_t k = tree_level_order(t, buf);
      T("invert -> level 1 3 2 6 5 4", k == 6 && arr_eq(buf, (int[]){1,3,2,6,5,4}, 6)); }
    tree_invert(t);   // 원복 (뒤 테스트 편의)
    T("invert twice -> restored",
      (tree_level_order(t, buf), arr_eq(buf, (int[]){1,2,3,4,5,6}, 6)));
    T("invert empty -> NULL", tree_invert(NULL) == NULL);
    tree_free(t);

    // -------- Q6 균형 여부 --------
    printf("== Q6. is balanced ==\n");
    { int b[] = {1, 2, 3, 4, 5, NIL, 6};       // 균형
      tnode_t *bt = tree_build(b, sizeof b / sizeof b[0]);
      T("balanced sample -> true", tree_is_balanced(bt));
      tree_free(bt); }
    { int sk[] = {1, 2, NIL, 3};                // 왼쪽 편향(skewed)
      tnode_t *st = tree_build(sk, sizeof sk / sizeof sk[0]);
      T("skewed 1->2->3 -> false", !tree_is_balanced(st));
      tree_free(st); }
    T("empty balanced -> true", tree_is_balanced(NULL));

    // -------- Q7 BST 삽입 & 탐색 --------
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
    bst = bst_insert(bst, 5);   // 중복
    { size_t k = 0; tree_inorder(bst, buf, &k);
      T("dup insert 5 -> unchanged (7 nodes)", k == 7); }
    tree_free(bst);

    // -------- Q8 BST 유효성 --------
    printf("== Q8. validate BST ==\n");
    { int v[] = {5, 3, 8, 1, 4, 7, 9};          // 레벨오더가 유효 BST
      tnode_t *vt = tree_build(v, sizeof v / sizeof v[0]);
      T("valid BST -> true", bst_is_valid(vt));
      tree_free(vt); }
    { int inv[] = {5, 1, 4, NIL, NIL, 3, 6};    // 고전 반례: 4 가 5의 오른쪽
      tnode_t *it = tree_build(inv, sizeof inv / sizeof inv[0]);
      T("invalid BST [5,1,4,_,_,3,6] -> false", !bst_is_valid(it));
      tree_free(it); }
    T("empty is valid BST -> true", bst_is_valid(NULL));
    { tnode_t *one = node_new(INT_MAX);          // 경계값 단일 노드
      T("single INT_MAX valid -> true", bst_is_valid(one));
      tree_free(one); }

    // -------- Q9 BST LCA --------
    printf("== Q9. BST lowest common ancestor ==\n");
    //            6
    //          /   \
    //         2     8
    //        / \   / \
    //       0   4 7   9
    //          / \
    //         3   5
    int la[] = {6, 2, 8, 0, 4, 7, 9, NIL, NIL, 3, 5};
    tnode_t *lt = tree_build(la, sizeof la / sizeof la[0]);
    { tnode_t *r = bst_lca(lt, 2, 8); T("LCA(2,8) -> 6", r && r->val == 6); }
    { tnode_t *r = bst_lca(lt, 2, 4); T("LCA(2,4) -> 2 (ancestor)", r && r->val == 2); }
    { tnode_t *r = bst_lca(lt, 3, 5); T("LCA(3,5) -> 4", r && r->val == 4); }
    { tnode_t *r = bst_lca(lt, 0, 5); T("LCA(0,5) -> 2", r && r->val == 2); }
    tree_free(lt);

    // -------- 결과 --------
    printf("\n==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
