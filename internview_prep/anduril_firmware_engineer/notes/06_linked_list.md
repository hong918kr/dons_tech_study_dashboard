# 06. 연결 리스트 (Linked List, malloc 없이 포함) — Q66~Q75 🔗

> 배열과 함께 임베디드 코딩 인터뷰의 양대 산맥. 특히 **이중 포인터 삭제 관용구**,
> **slow/fast 포인터**, 그리고 **힙 없는 정적 노드 풀**은 Anduril 류 방산 펌웨어에서
> 실제로 나오는 주제다.

---

## 왜 Anduril 펌웨어 인터뷰에 나오나

- 드론/무장 시스템 펌웨어는 **동적 할당(malloc)을 금지**하는 경우가 많다 (MISRA-C,
  실시간 결정성, 힙 단편화 회피). 그래서 "리스트를 힙 없이 어떻게 만들까?"가 단골 질문 →
  **정적 노드 풀 + free-list** (Q75).
- 이벤트 큐, 타이머 콜백 리스트, 센서 핸들러 체인, 커맨드 큐 등은 노드를 삽입/삭제하는
  자료구조다. 배열 링버퍼로 안 되는(중간 삭제, 가변 길이) 곳에 연결 리스트를 쓴다.
- 인터뷰어는 포인터 조작 정확도(특히 **head/꼬리 경계, NULL, 사이클**)로 지원자의
  C 실력을 빠르게 가늠한다.

---

## 핵심 개념 치트시트

| 개념 | 한 줄 요약 |
|------|-----------|
| 이중 포인터 삭제 (`node **`) | `*pp` = "현재 노드로 들어오는 링크". head/중간/꼬리 삭제를 **한 코드**로. |
| slow/fast 중앙 찾기 | fast 2칸, slow 1칸 → fast 끝나면 slow 가 중앙. 한 번 순회, O(1) 공간. |
| Floyd 사이클 탐지 | slow/fast 가 **만나면** 순환. 노드 주소 저장 불필요 → 메모리 0. |
| two-pointer 간격 | lead 를 n 칸 앞세우고 함께 이동 → 끝에서 N번째를 한 번에. |
| 스택 더미 노드 | 병합/삽입에서 head 특수 케이스 제거용. `node_t dummy;` (malloc 불필요). |
| 정적 노드 풀 | 컴파일 타임 배열 + free-list. `pool_alloc/pool_free` = O(1), 힙 0. |

---

## (A) 단일 연결 리스트 Singly

### 삭제는 무조건 이중 포인터로

```c
bool list_delete_value(node_t **head, int value) {
    node_t **pp = head;                 // pp: "다음 노드를 가리키는 링크"의 주소
    while (*pp) {
        if ((*pp)->data == value) {
            node_t *dead = *pp;
            *pp = dead->next;           // 들어오는 링크를 건너뛰게 재연결
            free(dead);
            return true;
        }
        pp = &(*pp)->next;
    }
    return false;
}
```

- `head` 삭제와 중간 삭제가 **동일 코드**가 되는 게 포인트. 단일 포인터로 짜면 `prev`
  추적 + head 특수 케이스가 생겨 버그가 난다.

### 중앙 / 사이클 = slow·fast 쌍

```c
node_t *slow = head, *fast = head;
while (fast && fast->next) {            // 이 조건이 짝수/홀수/NULL 모두 커버
    slow = slow->next;
    fast = fast->next->next;
    // if (slow == fast) return true;   // 사이클 탐지면 이 줄 추가
}
// 여기서 slow = 중앙 (짝수 길이면 뒤쪽 중앙)
```

- **가드 순서 주의**: `fast && fast->next` — `fast`를 먼저 검사해야 `fast->next`
  역참조가 안전 (short-circuit). 순서 바꾸면 NULL 역참조 UB.

### 끝에서 N번째 = 간격 고정 two-pointer

```c
node_t *lead = head;
for (size_t i = 0; i < n; ++i) { if (!lead) return NULL; lead = lead->next; }
node_t *trail = head;
while (lead) { lead = lead->next; trail = trail->next; }
return trail;   // n=1 이면 마지막 노드
```

### 병합은 스택 더미로

```c
node_t dummy; dummy.next = NULL;       // 힙 안 씀
node_t *tail = &dummy;
while (a && b) {
    if (a->data <= b->data) { tail->next = a; a = a->next; }  // <= 라야 안정
    else                    { tail->next = b; b = b->next; }
    tail = tail->next;
}
tail->next = a ? a : b;
return dummy.next;
```

---

## (B) 이중 연결 리스트 Doubly (Q74)

- 삽입/삭제 시 **prev 와 next 를 둘 다** 갱신해야 한다. 하나라도 빠지면 역방향 순회가
  깨진다.
- 삭제 정석:

```c
if (cur->prev) cur->prev->next = cur->next;  // 앞쪽 재연결
else           *head = cur->next;            // head 삭제
if (cur->next) cur->next->prev = cur->prev;  // 뒤쪽 재연결 (꼬리면 생략)
```

- 뒤집기는 각 노드에서 `prev ↔ next` 를 맞바꾸고, 마지막에 `head` 를 옛 꼬리로.
- 검증 팁: 앞으로 훑으며 `node->prev == 직전노드` 인지 확인하는 헬퍼(`dlist_prev_ok`)로
  링크 무결성을 테스트한다.

---

## (C) malloc 없는 정적 노드 풀 (Q75) — 임베디드 핵심

```c
#define POOL_SIZE 8
static pnode_t  g_pool[POOL_SIZE];   // 컴파일 타임 정적 저장 (heap 아님)
static pnode_t *g_free_list = NULL;  // 안 쓰는 노드들의 연결 리스트

void pool_init(void) {               // 리셋 시 한 번
    for (int i = 0; i < POOL_SIZE - 1; ++i) g_pool[i].next = &g_pool[i + 1];
    g_pool[POOL_SIZE - 1].next = NULL;
    g_free_list = &g_pool[0];
}
pnode_t *pool_alloc(void) {          // O(1)
    if (!g_free_list) return NULL;   // 고갈 → NULL (우아한 실패)
    pnode_t *n = g_free_list; g_free_list = n->next; n->next = NULL; return n;
}
void pool_free(pnode_t *n) {         // O(1) — free-list 머리에 반납
    if (!n) return; n->next = g_free_list; g_free_list = n;
}
```

**왜 이렇게?**

- 힙 없음 → **결정론적 지연시간** (ISR/RTOS 에서 `malloc` 은 잠재적 블로킹/단편화).
- 고갈 시 크래시 대신 `NULL/false` 로 실패 → 방어적. 상위에서 back-pressure 처리.
- `pool_free` 는 실제로 메모리를 반환하지 않고 **재사용 링에 되돌릴** 뿐 → 누수 개념이
  "free-list 개수"로 환원된다. 테스트에서 `pool_free_count()` 로 검증.

---

## 인터뷰 팔로업 & 임베디드 함정

- **Q. 왜 삭제에 `node **` 를 쓰나?** → head 특수 케이스 제거, prev 추적 불필요. 코드 절반.
- **Q. slow/fast 에서 짝수 길이면 어느 중앙?** → `while(fast && fast->next)` 는 뒤쪽
  중앙. 앞쪽을 원하면 `while(fast->next && fast->next->next)`.
- **Q. Floyd 대신 방문 노드 집합을 쓰면?** → O(n) 메모리. 임베디드에선 금물. Floyd 는 O(1).
- **함정: 병합 시 `<` vs `<=`** → `<=` 라야 동일 키의 상대 순서 보존(안정성).
- **함정: 사이클 있는 리스트를 print/free** → 무한 루프. 사이클 가능성 있으면 먼저 탐지.
- **함정: 정적 풀을 여러 리스트가 공유** → `pool_free` 를 빠뜨리면 풀만 마르고 힙 오류는
  안 뜬다. "왜 alloc 이 갑자기 NULL?" 디버깅의 흔한 원인.
- **함정: 노드 풀 + 인터럽트** → ISR 와 main 이 같은 free-list 를 만지면 경쟁. 임계구역
  보호(인터럽트 마스킹) 또는 SPSC 설계 필요.

---

## 복잡도 요약

| 연산 | 시간 | 공간 |
|------|------|------|
| push_front | O(1) | O(1) |
| push_back (꼬리 순회) | O(n) | O(1) |
| delete_value | O(n) | O(1) |
| reverse | O(n) | O(1) |
| find_middle / has_cycle | O(n) | O(1) |
| merge_sorted | O(n+m) | O(1) |
| nth_from_end | O(n) | O(1) |
| pool_alloc / pool_free | O(1) | O(1) |
