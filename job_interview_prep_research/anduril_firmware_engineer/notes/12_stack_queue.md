# 12. 스택 & 큐 (Stacks & Queues) 🥞

> 배열 기반 스택 하나로 괄호 검증부터 파서까지 풀린다. 여기에 **단조 스택**(monotonic
> stack)과 **단조 덱**(monotonic deque)이라는 두 개의 관용구를 얹으면, 얼핏 O(n²) 로 보이는
> "다음 큰 값 / 온도 대기일 / 슬라이딩 윈도우 최댓값" 이 전부 **O(n)** 으로 떨어진다.
> LC-medium 코딩 라운드의 단골 패턴들이다.

---

## 왜 Anduril 펌웨어 인터뷰에 나오나

- 스택은 **파서/평가기의 뼈대**다. 텔레메트리 명령어 파싱, 수식/설정 문법(RPN), 중첩 구조
  디코딩 등은 전부 스택으로 처리한다.
- 배열 기반 스택은 **힙 없이 O(1)**, 최대 깊이가 컴파일 타임에 고정 → 결정론적. malloc 을
  금지하는 방산 펌웨어(MISRA-C, 실시간성)와 잘 맞는다.
- 단조 스택/덱은 신호 처리에서 **국소 피크 추적**과 **슬라이딩 최댓값**(고정 지연 필터)의
  실시간 O(n) 알고리즘이다. 센서 스트림에서 "언제 임계 초과가 풀리나"가 곧 daily_temperatures.
- 인터뷰어는 "O(n²) → O(n)" 최적화를 스스로 떠올리는지, 그리고 **덱의 앞/뒤 제거 조건**을
  정확히 짜는지로 실력을 가늠한다.

---

## 핵심 개념 치트시트

| 개념 | 한 줄 요약 |
|------|-----------|
| 배열 기반 스택 | `int a[]; int top;` — push `a[top++]`, pop `a[--top]`. 힙 0, O(1). |
| 짝 맞추기(paren) | 여는 건 push, 닫는 건 top 과 짝 확인 후 pop. 끝에 비면 유효. |
| 보조 스택(MinStack) | 값 스택과 나란히 "접두 최솟값" 스택을 쌓아 getMin O(1). |
| 단조 스택 | top 이 조건을 어기면 pop 하며 답 확정. 각 원소 1회 push/pop → O(n). |
| 단조 덱 | 앞=창의 최댓값. 앞은 만료로, 뒤는 더 큰 값 등장으로 제거. O(n). |
| 두 스택 큐 | in/out 두 스택. out 이 빌 때만 in 을 뒤집어 옮김 → 분할상환 O(1). |
| 스택 파싱(RPN/decode) | 피연산자·부분결과를 쌓았다가 연산자·']' 에서 꺼내 합침. |

---

## (A) 배열 기반 스택 — 모든 것의 기본

```c
typedef struct { int *a; int top; int cap; } istk_t;   // top == 크기 == 다음 삽입 위치
static void istk_push(istk_t *s, int v) { s->a[s->top++] = v; }
static int  istk_pop (istk_t *s)        { return s->a[--s->top]; }
static int  istk_peek(const istk_t *s)  { return s->a[s->top - 1]; }
static bool istk_empty(const istk_t *s) { return s->top == 0; }
```

- `top` 을 "다음에 쓸 칸"으로 두면 push/pop 이 대칭이라 off-by-one 이 없다.
- 임베디드에선 `cap` 을 최대 깊이로 고정하고 `malloc` 대신 정적 배열을 쓰면 힙이 0.

### 괄호 검증 (valid_parentheses)

```c
if (여는 괄호) push;
else if (닫는 괄호) {
    if (스택 비었으면) return false;      // 짝 없는 닫힘
    if (top 이 짝이 아니면) return false;  // ( 와 ] 처럼 엇갈림
    pop;
}
// 끝: top == 0 이어야 유효 (남은 여는 괄호 없음)
```

- 세 가지 실패 모드: **엇갈린 짝**, **닫는데 빈 스택**, **끝에 남은 여는 괄호**. 셋 다 잡아야 한다.

### MinStack — getMin 을 O(1) 로

```c
push(x): val[top]=x; mn[top]=min(x, top>0 ? mn[top-1] : x); top++;
getMin(): return mn[top-1];   // 접두 최솟값이라 pop 해도 자동 복원
```

- 핵심 아이디어: **각 위치에 "그 시점까지의 최솟값"을 캐시**한다. pop 하면 그 아래 캐시가
  그대로 이전 최솟값 → 롤백 공짜. 값 하나마다 int 하나 추가 = O(n) 공간, 모든 연산 O(1).

---

## (B) 단조 스택 (Monotonic Stack) — O(n²) → O(n)

> **아이디어**: 스택에 "아직 답을 못 찾은 인덱스"만, 값이 **단조**가 되도록 쌓는다.
> 현재 원소가 스택 top 의 조건을 깨는 순간, 그 원소가 곧 대기자들의 답이다.

### next_greater_element (오른쪽 첫 더 큰 값)

```c
for i in 0..n:
    while (!empty && a[peek] < a[i]) out[pop] = a[i];  // 현재값이 대기자들의 next greater
    push(i);
// 끝까지 안 나온 인덱스는 -1 (초기값)
```

### daily_temperatures (더 따뜻한 날까지 며칠)

```c
while (!empty && t[peek] < t[i]) { j = pop; out[j] = i - j; }  // 날짜 차이가 답
push(i);
```

- **왜 O(n)?** 각 인덱스는 **정확히 한 번 push, 한 번 pop** 된다. while 이 안쪽에 있어도
  전체 pop 횟수는 n 을 넘지 않는다 → 분할상환 O(n).
- **인덱스를 저장**하는 게 포인트. 값만 저장하면 "며칠 차이" 같은 위치 정보를 못 낸다.
- "다음 **작은** 값"이면 부등호만 뒤집으면 된다(`>` 로). 단조 증가 스택.

---

## (C) 단조 덱 (Monotonic Deque) — 슬라이딩 윈도우 최댓값

> 스택은 한쪽만 열려 있다. 창이 **앞에서 만료**되고 뒤에서 새 원소가 들어오려면 **양쪽이
> 열린 덱**이 필요하다. 값 기준 단조 감소로 유지하면 **앞(head)이 항상 창의 최댓값**.

```c
int dq[n], head=0, tail=0;                       // 인덱스 저장, [head, tail)
for i in 0..n:
    if (head<tail && dq[head] <= i-k) head++;    // (1) 창 벗어난 앞 원소 만료
    while (head<tail && a[dq[tail-1]] <= a[i]) tail--;  // (2) 더 작은 뒤 원소 제거
    dq[tail++] = i;                              // (3) 현재 인덱스 push
    if (i >= k-1) out[oi++] = a[dq[head]];       // (4) 창이 차면 최댓값 기록
```

- (1) 만료는 `if` 로 충분(한 스텝에 최대 하나만 창을 벗어남). (2) 는 여러 개 지울 수 있어 `while`.
- 각 인덱스 역시 한 번 push/한 번 pop → **O(n)**. 힙-기반 O(n log k) 보다 빠르고 결정론적.
- **인덱스를 저장**해야 만료 조건(`dq[head] <= i-k`)을 검사할 수 있다.

---

## (D) 두 스택으로 큐 (Queue via two stacks)

```c
enqueue(x): in.push(x);
shift():    if (out.empty) while(!in.empty) out.push(in.pop());  // 뒤집어 옮김
dequeue():  shift(); return out.pop();
peek():     shift(); return out.peek();
```

- **왜 분할상환 O(1)?** 각 원소는 in→out 으로 **최대 한 번만** 이동한다. 어떤 dequeue 는
  O(n)(뒤집기)이지만, n 번의 연산에 걸쳐 총 이동은 O(n) → 평균 O(1).
- **함정**: out 이 비었을 때만 옮겨야 한다. 매번 옮기면 순서가 뒤죽박죽.

---

## (E) 스택 파싱 — RPN 과 중첩 디코드

### eval_rpn (후위 표기 평가)

```c
피연산자 -> push
연산자   -> b=pop; a=pop; push(a op b);   // 순서 주의! b 가 먼저
```

- **뺄셈/나눗셈은 순서가 생명**: `a - b`, `a / b` 이고 **b 를 먼저 pop**. 바꾸면 부호가 뒤집힌다.
- 음수 토큰 `"-11"` 을 연산자로 오인하지 않도록: "길이 1 이고 +−*/ 중 하나" 일 때만 연산자.

### decode_string ("3[a2[c]]" → "accaccacc")

```c
숫자 스택 + 문자열 스택:
  숫자      -> num = num*10 + d          // 여러 자리
  '['       -> nums.push(num); num=0; strs.push(cur); cur="";  // 상태 저장, 안쪽 새로
  ']'       -> k=nums.pop(); prev=strs.pop(); prev += cur*k; cur=prev;  // 되돌리며 합침
  그 외 문자 -> cur += c
```

- 재귀로도 풀 수 있지만(호출 스택 = 명시 스택), **명시적 두 스택**이 깊이 제한을 스스로
  통제해 임베디드에 안전하다.
- 반환 문자열은 `malloc` — **호출자가 free**. 소유권 규약을 시그니처 주석에 남긴다.

---

## 인터뷰 팔로업 & 임베디드 함정

- **Q. 단조 스택이 왜 O(n)?** → 각 원소 push 1회 + pop 1회. while 중첩이어도 총 pop ≤ n.
- **Q. 값 대신 인덱스를 왜 저장?** → 위치 차이(daily), 만료 판정(sliding) 때문. 값만으론 부족.
- **Q. 슬라이딩 최댓값에 왜 힙 말고 덱?** → 힙은 O(n log k)+삭제 지연. 덱은 O(n), 결정론적.
- **함정: RPN 피연산자 순서** → `a op b` 에서 b 가 먼저 pop. `-`,`/` 에서 특히 치명적.
- **함정: 두 스택 큐의 조기 shift** → out 이 안 비었는데 옮기면 FIFO 깨짐.
- **함정: 스택 오버플로** → 배열 cap 고정 시 push 전 용량 체크(임베디드에선 필수).
- **함정: decode 반환 메모리 누수** → 반환 버퍼 free 규약 누락. 중첩마다 임시 버퍼도 free.
- **함정: 괄호 검증에서 "끝에 남은 여는 괄호"** → `"("` 는 false. top==0 확인을 빠뜨리기 쉽다.

---

## 복잡도 요약

| 연산/문제 | 시간 | 공간 |
|-----------|------|------|
| valid_parentheses | O(n) | O(n) |
| MinStack push/pop/top/getMin | O(1) | O(n) |
| next_greater_element | O(n) | O(n) |
| daily_temperatures | O(n) | O(n) |
| eval_rpn | O(n) | O(n) |
| queue_via_two_stacks | 분할상환 O(1) | O(n) |
| sliding_window_max | O(n) | O(k) |
| decode_string | O(출력 길이) | O(출력 길이) |
