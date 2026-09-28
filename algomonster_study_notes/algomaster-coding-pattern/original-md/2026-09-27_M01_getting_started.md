# Module 1 — Getting Started (시작하기: 로드맵·기초 DSA·정렬)

> 출처: AlgoMonster Coding Patterns, Module 1 (레슨 18개) · 한국어 개념 정리 노트
> 목표: 무엇을 어떤 순서로 공부할지(ROI) 정하고, 입력 크기·키워드로 알고리즘을 역추론하는 감각 + 기초 DSA·정렬·시뮬레이션을 빠르게 다지기.

## 한눈에 보기 (5분 복습용)

| 패턴/주제 | 언제 쓰나(키워드) | 핵심 아이디어 | 복잡도 |
|---|---|---|---|
| 입력 크기 역추론 | Constraints의 n 범위 | n≤12 → N!, ≤20 → 2ᴺ, ≤3000 → N², ≤10⁶ → N log N / N, 더 크면 log N / O(1) | ~10⁷ 연산/초 |
| 키워드 → 알고리즘 | Top K, sorted, substring, shortest, prerequisites… | 문구가 알고리즘을 흘린다 (Heap, Binary Search, Sliding Window, BFS, Topo Sort) | — |
| Stack | 괄호, 최근 것, 중첩, next greater | LIFO, push/pop/peek | O(1) 연산 |
| Queue / Deque | BFS, 순서대로 처리, 회전 | FIFO, `deque.popleft()` / ring buffer | O(1) 연산 |
| Hash Map / Set | 본 적 있나, 빈도, 짝 찾기 | 해시 → bucket, 충돌은 chaining/open addressing | 평균 O(1), 최악 O(n) |
| 기본 정렬 (Insertion/Selection/Bubble) | 거의 정렬됨 / swap 비쌈 | 정렬된 prefix 확장 | O(n²) |
| Merge Sort | 안정성·최악 보장, inversion 세기 | 반으로 나누고 두 포인터 병합 | O(n log n), 공간 O(n), stable |
| Quick Sort | in-place 고속, partition 아이디어 | pivot 기준 재배치 후 재귀 | 평균 O(n log n), 최악 O(n²) |
| Custom Comparator | "~순, 동률이면 ~순" | Python 튜플 key, `cmp_to_key` | O(n log n) |
| Simulation | Design ~, 로봇 이동, 게임 규칙 | 상태 정의 + 단계별 전이 + 엣지 케이스 | 문제별 |

**우선순위:** DFS·BFS·Two Pointers → 기초 DSA → Heap → (시간 되면) DP·Greedy. 사람 이름 붙은 학술 알고리즘은 스킵.

**문제 읽는 순서:** Constraints(n) 먼저 → 허용 복잡도 → 키워드로 후보 패턴 → 템플릿 적용.

**면접은 LeetCode가 아니다:** 테스트 케이스는 내가 만들고, 생각을 말로 하고, 깔끔한 코드로 평가받는다.

---

## 1. Patterns (패턴별 출제 통계와 ROI)
> 섹션: Overview

### 핵심 개념
- 실제 면접 문제 데이터셋을 **패턴별로 분류**해서 "가장 자주 나오고 투자 대비 효과(ROI)가 큰 영역"부터 공부하자는 데이터 기반 접근.

### ROI 순위
| 우선순위 | 영역 | 코멘트 |
|---|---|---|
| 최상 | **DFS, BFS, Two Pointers** | 전체 문제의 큰 비중. DFS는 트리·그래프·조합 문제까지 커버 |
| 매우 높음 | 기초 DSA (Linked List, Array, Hash Map, Stack, Queue, Sorting) | 쉬운 면접 대부분 통과. 변형이 적음 (리스트 뒤집기, 중간 노드, 사이클 검출) |
| 중간 | Priority Queue / Heap | 생각보다 자주 나옴: 데이터 스트림 중앙값, k closest points |
| 낮음 | DP, Greedy | DP는 Google 외엔 드묾 → 막히면 **DFS + memoization**으로 대체 가능. Greedy는 증명이 어려워 ROI 낮음 |
| 보조 | Trie, Union Find | 가끔 나옴. 2순위 |

### 회사별 경향
- **Amazon:** 고전 문제 그대로. Two pointers·DFS·BFS가 절반.
- **Meta:** 고전 문제, Amazon보다 약간 어려움. **버그 없는 코딩**을 가장 중시 → 고전 문제 반복 연습.
- **Google:** 인터넷에 있는 문제 금지 정책 → 새 문제. **패턴 조합**(prefix sum + binary search, DFS + prefix sum) 자주.
- **Oracle/IBM/Expedia 등:** 고정된 문제은행.
- **유니콘 스타트업(Airbnb, Uber, Dropbox):** Amazon보다 어려움. Trie·Union Find도 대비.

### "학술 알고리즘"은 과감히 스킵
- 규칙: **사람 이름이 붙은 알고리즘은 대체로 무시해도 됨.**
- 거의 안 나옴: Kruskal/Prim(MST), Ford-Fulkerson(max flow), Bellman-Ford, Boyer-Moore.
- KMP: 기대하지 않음 → 버그 없는 brute force가 더 중요.
- Dijkstra: 드물지만 priority queue를 쓰므로 **개념은 알아두기**.
- Rabin-Karp rolling hash: 일부 two-pointer 문자열 문제에 유용.

- ✅ 체크포인트: 시간이 부족하면 DFS/BFS/Two Pointers → 기초 DSA → Heap → 나머지 순.

---

## 2. Roadmap (학습 로드맵)

### 어느 코스부터?
- 자료구조가 낯설다 → **Foundation Course** (배열·문자열·연결 리스트·스택·큐·해시 테이블부터).
- **Easy 문제를 15분 안에** 풀 수 있다 → 바로 **Core Patterns**(이 코스).

### 학습 순서 원칙
- 사이드바 섹션 순서대로. 각 섹션은 앞 섹션 위에 쌓이도록 설계됨 → 아는 내용이라도 **순서대로** 따라가는 게 권장.

### 한 섹션의 학습 루프
1. 개념 글 읽기 → 2. 끝의 **퀴즈**로 확인 → 3. 섹션 **연습 문제** 풀기 → 4. **Speedrun**(객관식 패턴 인식 훈련) → 5. 3분짜리 Monster Breakdown Shorts 영상 → 6. **AlgoMonster 50** 문제 리스트로 확장.
- 마무리: 회사별 인터뷰 가이드 + 회사별 문제 세트로 최종 스프린트.

### 시간이 부족할 때
- Core Patterns는 **DP 모듈 직전까지**만. DP는 폭넓은 연습·수학 감각이 필요 → Google급이 아니면 후순위.
- 면접이 임박: 각 패턴의 **핵심 템플릿**만 익히고 곧장 회사별 문제로.
- 자투리 시간: Shorts 영상.

- ✅ 체크포인트: Don의 경우 → Core Patterns 순서대로, DP 전까지 우선 완주 + 각 섹션 Speedrun으로 패턴 인식 점검.

---

## 3. Interview Process (실제 면접 진행과 LeetCode와의 차이)

### 면접 구조 (대부분 원격)
| 단계 | 시간 | 할 일 |
|---|---|---|
| 자기소개 | ~5분 | 관련 프로젝트 짧게, 회사에 대한 관심 |
| 코딩 | 30–45분 | HackerRank / CoderPad / Google Doc 등에서 본 평가 |
| Q&A | 5–10분 | 팀·역할 질문 → 열정·컬처 핏 |

- 문제 출처: 대기업은 자체 문제은행(LeetCode 회사 태그가 대체로 정확), 중소기업은 **Blind 75** 류.

### 코딩 세션 7단계
1. 문제 이해 — **명확화 질문**
2. 엣지 케이스·제약 조건 논의
3. 여러 접근법을 **말로** 제시
4. 생각을 소리 내어 (think out loud)
5. 시간/공간 복잡도로 접근법 선택·정당화
6. 설명하면서 코딩
7. **직접 테스트 케이스 만들어** 검증

### LeetCode vs 실제 면접
| LeetCode | 실제 면접 |
|---|---|
| 테스트 케이스 제공 | **내가** 테스트 케이스를 만든다 |
| 런타임 백분위 경쟁 | 런타임은 중요하지만 **접근 과정**으로 평가 |
| 조용히 타이핑해도 됨 | **커뮤니케이션 필수** |
| 돌아가기만 하면 됨 | 깔끔·가독성·유지보수성 |

### 평가 4축
1. **알고리즘 사고** — 적절한 DS/알고리즘 + 합리적 복잡도
2. **문제 해결 자율성** — 힌트 없이 다음 단계를 찾는가
3. **커뮤니케이션** — 사고 과정을 명확히 설명
4. **코드 품질** — 구조·가독성

### 준비 팁
- 문제가 아니라 **패턴**을 배우기. BFS/DFS/binary search 등 **템플릿을 외워** 보일러플레이트 실수 줄이기.
- 혼자 공부할 때도 소리 내어 설명 연습. 시간 제한 걸고 연습.

- 🎤 면접 한마디: "먼저 입력 범위와 엣지 케이스를 확인하겠습니다 — 빈 배열, 중복, 음수가 가능한가요?"

---

## 4. How to Study (공부법: PTS 시스템)

### 핵심 개념
- 가장 중요한 단계는 **무엇을 공부하지 "않을지"** 정하기. 예: Burrows-Wheeler 같은 학술 알고리즘은 면접에 안 나옴 → 시간 낭비 + 기회비용.
- 실제 면접 문제는 **소수의 패턴**으로 압축된다 → 패턴 마스터가 목표.
- 면접관이 보는 기술적 두 가지: ① 뛰어난 **문제 해결력** ② **버그 없는 코드**(실무에서 스펙대로 코딩하는 능력과 직결 → 어떤 면에선 더 중요).

### PTS = Patterns → Templates → Speedrun
| 단계 | 내용 | 목표 |
|---|---|---|
| 0. 기초 DS | 배열·스택·연결 리스트 + 내부 동작 | 언어별 DS 개요로 복습 |
| 1. **Patterns** | 수천 문제 → 소수 패턴. 기본형부터 점진적으로 | "모르는 건 추론 못 한다" — 압박 속에선 **준비한 수준으로 떨어진다** |
| 2. **Templates** | 패턴 코드를 템플릿화 | 인라인 에디터로 반복해 **10–15분 안에 버그 없이** |
| 3. **Speedrun** | 코딩 없이 많은 문제를 보고 패턴 판별 + **머릿속 풀이** | 시간 절약 + 패턴 인식 폭 확대 |
| 4. Shorts 영상 | 3분 문제 해설 | 자투리 시간 활용 |
| 5. Quizzes | 글 끝 퀴즈 | active recall, 약점 파악 |

- **Flowchart:** 문제 문구의 단서로 패턴을 고르는 결정 트리 — 배운 패턴을 통합.
- 질문은 AI TA / "Help Me!" 버튼(디버깅·복잡도 분석) / 포럼·Discord.
- 드라이버 코드가 제공되므로 로컬 IDE로 복붙해 돌려볼 수 있음.

- ✅ 체크포인트: 템플릿은 "이해"가 아니라 **손이 기억할 때까지** 반복. 그 다음 Speedrun으로 폭을 넓힌다.

---

## 5. The Mindset (면접 준비 마인드셋)

### 핵심 메시지
- 가장 큰 장벽은 지능이 아니라 **마음가짐과 집중력**. 코딩 면접은 "풀린 문제" — 평균 지능 + 집중 + 꾸준함이면 통과 가능.

### 집중 환경 만들기
| 영역 | 방법 |
|---|---|
| 스마트폰 | 다른 방에 두기 / 방해금지 모드 / SNS 앱 삭제(즉각 보상 도파민 → 지연 보상 과제에 집중 불가) / 공부용 "깨끗한" 기기 분리 |
| **흑백 모드 (최강 팁)** | iPhone: 설정 > 손쉬운 사용 > 디스플레이 및 텍스트 크기 > 색상 필터 > 흑백. 색이 사라지면 폰이 "슬롯머신 → 도구"가 됨 |
| 데스크톱 | YouTube 추천 차단 확장 / 공부 전용 사용자 프로필 / `/etc/hosts`로 방해 사이트를 127.0.0.1로 리다이렉트 |

### 동기 유지
- **Inner Game** 접근: 결과(취업·비교)에 대한 불안이 아니라 **눈앞의 문제 자체에 몰입**. 인생은 싱글 플레이어 게임 — 경쟁 상대는 어제의 나.
- **성장 마인드셋:** 각 문제를 능력 시험이 아니라 성장 기회로. 집중된 노력이 쌓이면 오퍼는 따라온다.

- ✅ 체크포인트: 공부 세션 시작 전 — 폰 다른 방, 공부 전용 프로필, 타이머.

---

## 6. Math Basics (면접용 수학 기초)

### 핵심 개념
- 필요한 수학은 **고등학교 수준**. 특정 수학 트릭을 요구하는 문제는 실제 면접에선 드물고, 나와도 리뷰 과정에서 가중치가 낮아지는 경우가 많음.

### 진법
- 10진수 352 = 3·10² + 5·10¹ + 2·10⁰. 2진수 1011 = 1·2³ + 0·2² + 1·2¹ + 1·2⁰ = 11.

### 로그 (log₂)
- 지수의 역함수: log₂(16) = 4 ⇔ 2⁴ = 16.
- 직관: **n을 2로 몇 번 나누면 1이 되나** → 반씩 줄이는 알고리즘(binary search, 균형 트리 높이)의 복잡도가 log n인 이유.

### 경우의 수
| 개념 | 공식 | 면접 연결 |
|---|---|---|
| 순열 (permutation) | n! (첫 자리 n개, 다음 n−1개, …) | 순열 생성 backtracking = O(n·n!) |
| 부분집합 (subset) | **2ⁿ** (원소마다 넣기/빼기 스위치, 공집합·전체 포함) | 부분집합 생성 = O(n·2ⁿ) |
| 등차수열 합 | (첫항 + 끝항) × 개수 / 2 | 삼각형 중첩 루프 1+2+…+n = n(n+1)/2 → **O(n²)** |
| 등비수열 합 | a(1 − rⁿ)/(1 − r) | 완전 이진 트리 노드 수 1+2+4+…+2ʰ = 2ʰ⁺¹ − 1 |

```python
for i in range(n):
    for j in range(i + 1):   # 1 + 2 + ... + n 번
        do_something()        # 총 n(n+1)/2 → O(n^2)
```

### 모듈러 연산
- 일정 값에서 "감기는" 정수 (시계: 15시 ≡ 3시 mod 12). `x % y` = x에서 y를 x < y가 될 때까지 뺀 나머지.
- **분배 성질:** `(a + b) % c == ((a % c) + (b % c)) % c` (곱셈도 동일) → 큰 수 오버플로 방지, "답을 1e9+7로 나눈 나머지" 문제.
- **나누어떨어짐** 문제에서 떠올리기. 예: 소수 판별은 [2, √n]까지만 나눠보면 충분 (약수는 √n을 기준으로 짝지어짐).

```python
def is_prime(n: int) -> bool:
    if n < 2:
        return False              # 1은 소수 아님
    i = 2
    while i * i <= n:             # sqrt(n)까지만
        if n % i == 0:
            return False
        i += 1
    return True                   # O(sqrt n)
```
- 🐛 흔한 실수: n=1을 소수로 판정, `range(2, int(n**0.5))`처럼 **+1 빠뜨려** 완전제곱수(9, 25) 놓치기.

---

## 7. Runtime to Algo Cheat Sheet (입력 크기 → 알고리즘 역추론)

### 핵심 개념
- 시간 복잡도 = 입력 크기가 커질 때 실행 시간이 **어떻게 증가하는가**(최악 기준 상한). 상수·저차항은 버림: O(5n + 17) = O(n).
- 경험칙: 채점 플랫폼은 대략 **초당 1,000만~2,000만(10⁷~2·10⁷) 연산**까지 허용 → **제약 조건 n을 보고 필요한 복잡도를 역산**한다.

### 치트 시트
| 복잡도 | 허용 n (대략) | 대표 알고리즘 / 상황 |
|---|---|---|
| O(1) | n > 10⁹ | 해시맵 조회, 배열 인덱싱, 스택 push/pop, **수학 공식** |
| O(log N) | N > 10⁸ (값 제약) | **binary search**, 균형 BST 조회, 정수 N의 자릿수 처리 |
| O(N) | n ≤ 10⁶ | 배열/리스트 순회, **two pointers**, 일부 greedy, 트리/그래프 순회, stack/queue |
| O(K log N) | n ≤ 10⁶ | **heap** push/pop K번 (top K, K closest, merge K sorted), binary search K번 |
| O(N log N) | n ≤ 10⁶ | **정렬**, 선형 merge를 가진 divide & conquer (예: 오른쪽의 더 작은 수 세기) |
| O(N²) | n ≤ 3,000 | 중첩 루프, 행렬 전체 방문, 대부분의 brute force |
| O(2ᴺ) | n ≤ 20 | 조합/**부분집합** backtracking, 메모 없는 재귀 Fibonacci |
| O(N!) | n ≤ 12 | **순열** backtracking |

```python
N = 10**8
while N > 0:        # 매번 절반 → O(log N)
    N //= 2

for a in range(N):          # 안쪽이 1..N번으로 변해도
    for j in range(a, N):   # 최악 기준 → O(N^2)
        pass
```
- 면접에선 n이 작아 O(N²)이 통과하더라도 더 좋은 해법이 있으면 그걸 보여줘야 인상적.
- DB primary key 조회도 B-tree라서 O(log N). log₂(10⁶) ≈ 20.

### 분할상환 (Amortized)
- 드물게 비싼 연산이 전체에 희석되는 것. **연산당 평균** 비용.
- 동적 배열 append: 꽉 차면 capacity 2배 + 복사(O(n)) — 드물게 발생 → 평균 **O(1)**.

```python
def append(x):
    global arr, size, cap
    if size == cap:                    # 가득 차면 2배로 키우고 복사
        cap = max(1, 2 * cap)
        new = [None] * cap
        new[:size] = arr[:size]
        arr = new
    arr[size] = x
    size += 1
```

### 연습 문제 정답 (직접 풀어 보고 확인)
| 문항 | 답 |
|---|---|
| 3N+2N+N / 2N³+5N² / N+logN / N²logN / 2ᴺ+N² / 10 | O(N) / O(N³) / O(N) / O(N² log N) / O(2ᴺ) / O(1) |
| 입력 N개 읽고 최댓값 | O(N) |
| N×N 입력 후 각 행 정렬 | O(N² log N) |
| 각 행을 절반만 swap (행 뒤집기) | O(N²) |
| 깊이 log₂N의 이진 재귀 | O(2^(log N)) = **O(N)** |
| N을 2로 나누며 append | O(log N) |

- 🔑 키워드: 문제의 **Constraints 줄(n ≤ …)을 먼저 읽기** → 허용 복잡도 → 후보 패턴.
- 🎤 "n ≤ 10⁵이니 O(n²)은 10¹⁰이라 안 되고, O(n log n) 정렬이나 O(n) two pointers를 목표로 하겠습니다."

---

## 8. Keyword to Algo Cheat Sheet (문제 키워드 → 알고리즘)

### 핵심 개념
- 문제는 알고리즘을 직접 말하지 않지만 **문구가 힌트를 흘린다.** 키워드를 읽을 줄 알면 코딩 전에 절반은 끝난 것.

### 치트 시트
| 키워드 | 알고리즘 | 이유 / 세부 신호 | 대표 문제 |
|---|---|---|---|
| "Top K", "K-th largest" | **Heap** | 크기 K 경계만 유지 → O(n log K) | K Closest Points |
| "sorted", "rotated sorted", "O(log n)" | **Binary Search** | 순서로 탐색 공간 절반. 단조 predicate면 배열 아니어도 OK | Binary Search |
| "How many ways" | DFS / DP | 결정 트리면 DFS, 격자·수열에서 부분문제 겹치면 DP | Decode Ways / Robot Paths |
| "Substring" | **Sliding Window** | 연속 구간을 O(1)로 늘리고 줄임 | Longest Substring w/o Repeat |
| "prefix", "autocomplete", "starts with" | **Trie** | 공통 접두사 공유, 조회 O(단어 길이) | Implement Trie |
| "Palindrome" | Two Pointers / DFS / DP | 검증 → 양끝 포인터, 모든 분할 나열 → DFS, 최소 컷·개수 → DP | Valid Palindrome / Palindrome Partitioning I·II |
| "Tree" | BFS / DFS | **레벨·최소 깊이·right-side view → BFS**, 합·높이·경로 → DFS | Level-Order / Max Depth |
| "Parentheses" | **Stack** | 마지막에 연 것이 먼저 닫힘(LIFO) | Valid Parentheses |
| "next greater/smaller", "warmer day" | **Monotonic Stack** | 후보를 단조 순서로 유지, 원소별 답이 pop될 때 결정 (amortized O(1)) | Next Greater Element II |
| "Subarray" | Sliding Window / Prefix Sum / 해시맵+prefix sum | 고정 크기·단조 제약 → window, 구간합 질의 → prefix sum, **합 == target → prefix sum 해시맵** | Subarray Sum (Fixed) / Continuous Subarray Sum |
| "Max subarray" | **Greedy (Kadane)** | 각 위치에서 이어가기 vs 새로 시작 | Kadane's Algorithm |
| "Two sum / K sum" | **Two Pointers** (정렬 후) | 한 번 움직일 때 합이 확실히 증가/감소 | Two Sum |
| "Max/longest sequence" | DP·memo DFS / Monotonic Deque | 이전 위치에 의존 → DP, 고정 창 안 최댓값 → deque | LIS / Sliding Window Maximum |
| "Minimum / Shortest" | BFS / Dijkstra / DP | 무가중 최소 간선 → BFS, 음수 없는 가중치 → Dijkstra, 격자·수열 누적 비용 → DP | Shortest Path / Minimal Path Sum |
| "Partition / split" | DFS (+memo) | 각 분할 지점이 재귀 선택, suffix 반복 시 memo | Decode Ways |
| "Subsequence" | DP / memo DFS | 각 인덱스에서 pick or skip, 부분문제 겹침 | LIS |
| "all combinations / permutations / subsets", "generate every" | **Backtracking** | 부분해 기록 → 재귀 → 되돌리기 | Permutations, Subsets |
| "Matrix", grid | BFS·DFS / DP | 격자 = 그래프. 연결성·flood fill·섬 개수 → BFS/DFS, 이웃에서 쌓는 최적화 → DP | Number of Islands / Maximal Square |
| "Jump" | Greedy / DP | 도달 가능 여부(최대한 멀리) → Greedy, 점프 횟수·greedy 실패 → DP | Jump Game / Jump Game II |
| "Game" (이길 수 있나) | DP on states | 상태 재귀 + 겹침 | Divisor Game |
| "Connected component" | **Union Find** | 간선 추가마다 BFS 다시 돌리는 것보다 빠름. 그래프 고정·순회 몇 번이면 BFS/DFS | Number of Connected Components |
| "schedule", "prerequisites", "build order", "course" | **Topological Sort** | 방향 의존성을 선형화 (Kahn BFS / DFS post-order), 사이클 검출 덤 | Course Schedule |
| "transitive relationship" (A~B, B~C ⇒ A~C) | BFS / Union Find | 관계 = 그래프. 단일 질의 → BFS, 고정 관계 다중 질의 → Union Find | Word Ladder / Sentence Similarity / Evaluate Division |
| "Interval" | **정렬 후 Greedy** | 시작점 기준 정렬(merge) 또는 끝점 기준(최대 배치) → 한 패스 | Merge Intervals |

- ✅ 체크포인트: 같은 단어(Minimum, Subarray, Palindrome, Tree)가 **여러 알고리즘**을 가리킬 수 있다 → 세부 신호(가중치 유무, 연속성, 검증 vs 생성)로 가른다.
- 🎤 "'shortest'인데 간선 가중치가 없으니 BFS로 가겠습니다. 가중치가 있다면 Dijkstra입니다."

---

## 9. Basic Data Structures and Algorithms (기초 DSA 개요)
> 섹션: Quick DSA Review

### 핵심 개념
- CS 첫 과목 수준의 빌딩 블록. 이미 아는 사람을 위한 **짧은 복습**(모르면 Foundation Course / CS50).
- 난이도는 대부분 Easy → 이 코스의 초점은 아님.

| DS/알고리즘 | 정의 | 면접 포인트 |
|---|---|---|
| Linked List | 노드가 다음 노드를 가리킴 | 뒤집기, 중간 노드, 사이클 |
| Array | 연속 메모리, 인덱스 접근 O(1) | 대부분 문제의 입력 |
| Hash Map | 해시 함수로 삽입·삭제·조회 평균 O(1) | "본 적 있나?", 카운팅 |
| Stack | LIFO | 괄호, monotonic stack, DFS |
| Queue | FIFO | **BFS** |
| Sorting | 오름/내림차순 정렬 | Binary Search·Greedy와 짝 |

### 입문 연습 문제 (DS 매핑)
| 문제 | DS | 한 줄 풀이 |
|---|---|---|
| Two Sum | Array + Hash Map | `target - x`를 해시맵에서 찾기, O(n) |
| Reverse Linked List | Linked List | prev/cur/next 3포인터로 링크 뒤집기 |
| Number of Recent Calls | Queue | 새 ping 넣고 `t - 3000` 미만을 앞에서 pop |
| Valid Parentheses | Stack | 여는 괄호 push, 닫는 괄호면 top과 짝 확인 |
| Array Partition | Sorting | 정렬 후 짝수 인덱스 합 (인접끼리 짝지으면 min 합 최대) |

```python
def reverse_list(head):
    prev = None
    while head:
        head.next, prev, head = prev, head, head.next   # 링크 뒤집고 한 칸 전진
    return prev
```

---

## 10. Stack Intro (스택 입문)

### 핵심 개념
- 책 더미: 위에 올리고, 위에서 꺼내고, 위만 본다. **LIFO / FILO** — 나중에 넣은 것이 먼저 나옴.
- 연산 3개 (모두 O(1)): **push**(top에 추가), **peek**(top 확인), **pop**(top 제거).
- 쓰임: **재귀는 내부적으로 call stack**, DFS는 스택(명시적 또는 재귀), 괄호 매칭, undo.

### 템플릿: 명령어 실행기
**문제:** `["push 1", "push 2", "peek", "pop"]` 같은 명령 리스트 실행, peek마다 top 출력, 최종 스택 반환 → 출력 `2`, 반환 `[1]`.
```python
def execute(program: list[str]) -> list[int]:
    stack: list[int] = []
    for ins in program:
        if ins == "peek":
            print(stack[-1])
        elif ins == "pop":
            stack.pop()
        else:                         # "push <n>"
            stack.append(int(ins[5:]))
    return stack
```

### 직접 구현 (선택)
- 고정 크기 배열 + **top 포인터**(다음 빈 칸). push: `arr[top] = x; top += 1`, pop: `top -= 1` (값을 지울 필요 없음 — 다음 push가 덮어씀. GC 없는 언어에선 참조 해제 고려).
- **Underflow:** 빈 스택 pop → pop 전에 empty 체크. **Overflow:** 고정 배열이 가득 참 → 동적 배열이면 자동 확장(amortized O(1)).
- 언어별: Python `list`(append/pop), Java는 `Stack` 클래스(동기화로 느림) 대신 `ArrayList`/`ArrayDeque`.

- 🐛 흔한 실수: 빈 스택에 `stack[-1]` / `pop()` → IndexError. 항상 `if stack:` 가드.
- 🔑 "가장 최근 것", "되돌리기", "짝 맞추기", "중첩 구조" → Stack.

---

## 11. Queue Intro (큐·덱 입문)

### 핵심 개념
- 매표소 줄: 먼저 온 사람이 먼저. **FIFO**. 연산: push(뒤에 추가), peek(앞 확인), pop(앞 제거) — 모두 O(1).
- **Deque**(double-ended queue, "덱"): 양끝 모두 push/peek/pop 가능 (6개 연산). Sliding window max의 monotonic deque, 0-1 BFS 등에 사용.
- 쓰임: **BFS**, 스케줄링, 스트림 버퍼.

### 템플릿: 0이 맨 앞에 올 때까지 왼쪽 회전
**문제:** 0이 하나 있는 배열을 왼쪽으로 회전해 0을 맨 앞으로. `[1, 2, 0, 3]` → `[0, 3, 1, 2]`.
```python
from collections import deque

def rotate_left_till_zero(nums: list[int]) -> list[int]:
    q = deque(nums)
    while q[0] != 0:
        q.append(q.popleft())    # 앞에서 빼서 뒤로
    return list(q)
```

### 직접 구현 (선택)
- 배열 + 포인터 2개(head, tail). push: `arr[tail] = x; tail += 1`, pop: `head += 1`.
- 결함: tail이 배열 끝에 닿으면 앞쪽에 빈칸이 있어도 overflow → **Circular Buffer(ring buffer)**: 인덱스를 `% capacity`로 감아서 빈 칸 재사용.

```python
class RingQueue:
    def __init__(self, cap):
        self.buf, self.head, self.size, self.cap = [None] * cap, 0, 0, cap
    def push(self, x):
        if self.size == self.cap: raise OverflowError
        self.buf[(self.head + self.size) % self.cap] = x
        self.size += 1
    def pop(self):
        if self.size == 0: raise IndexError       # underflow
        x = self.buf[self.head]
        self.head = (self.head + 1) % self.cap
        self.size -= 1
        return x
```
- 🐛 흔한 실수: Python에서 `list.pop(0)`을 큐로 사용 → **O(n)**. 반드시 `collections.deque`의 `popleft()`.
- 💡 Don 메모: 펌웨어의 UART/DMA ring buffer와 동일한 구조 — full/empty 구분은 `size` 카운터 또는 "한 칸 비워두기".

---

## 12. Hashmap Intro (해시맵 입문)

### 해시 함수
- 임의 크기 데이터 → **고정 크기 값**(보통 32-bit 정수). 예: 배열 원소 합 % 100 → `[5,23,84]` → 12.
- 필수 조건: **같은 값이면 항상 같은 해시.** 역은 성립 안 함 → 다른 값이 같은 해시 = **collision** (`[32,10]`과 `[18,10,14]` 모두 42).
- 좋은 해시: 계산이 빠름, 충돌 확률 낮음, 출력 범위를 **고르게** 사용.

### 해시 테이블 동작
- 고정 크기 배열(bucket). `index = hash(key) % k` 자리에 값 저장, 조회 때 같은 계산으로 찾음.
- 비둘기집 원리로 충돌은 **필연** → 처리 전략:
  - **Separate chaining:** bucket마다 (key, value) 리스트. 키 존재 확인 → 갱신 또는 append.
  - **Open addressing:** 같은 배열에서 다음 빈 칸을 찾아 저장 (probing).

### 효율
| 경우 | 복잡도 |
|---|---|
| 평균 (균등 분포, load factor n/k) | O(n/k) → 동적 리사이즈로 **O(1)** |
| 최악 (전부 한 bucket) | **O(n)** |
- Python `dict`, Java `HashMap` = 동적 해시 테이블. "정수가 아닌 인덱스가 필요한 배열"로 생각.

### 템플릿: 빈도 세기
```python
def get_counter(arr: list[int]) -> dict[int, int]:
    counter: dict[int, int] = {}
    for x in arr:
        counter[x] = counter.get(x, 0) + 1
    return counter
# 또는 collections.Counter(arr)
```
- 🐛 흔한 실수: mutable 타입(list)을 키로 사용 → TypeError. `tuple`로 변환.
- 🎤 "해시맵 조회가 왜 O(1)인가?" → 평균적으로 load factor를 상수로 유지하도록 리사이즈하기 때문. 최악은 O(n)(충돌 폭주).
- 🔑 "본 적 있나", "빈도", "짝 찾기(two sum)", "그룹화(anagram)" → Hash Map.

---

## 13. Data Structures by Language (Python 자료구조 레퍼런스)

### Python 자료구조 ↔ 연산 비용
| 추상 DS | Python | 핵심 연산 (평균) |
|---|---|---|
| Array | `list` (동적 배열) | 인덱스 O(1), `a[-1]` 마지막 원소, 슬라이스 `a[i:j]`는 O(j−i) 복사 |
| Linked List | 내장 없음 — 면접에서 `ListNode(val, next)` 제공 | append·탐색 O(N) |
| Stack | `list` | `append` / `pop` / `l[-1]` / `len` 모두 O(1) |
| Queue / Deque | `collections.deque` | `append`, **`popleft()`**, `q[0]`, `len` O(1) |
| Hash Table | `dict` | `d[k]`(없으면 KeyError), `d[k]=v`, `del d[k]` O(1). 최악 O(N) |
| Counter | `collections.Counter` | 없는 키는 **0** 반환, `len(counter)` = 고유 원소 수 |
| Hash Set | `set` | `a in s`, `s.add`, `s.discard`(없어도 에러 X) O(1) — BFS/DFS visited |
| Tree | 내장 없음 — `TreeNode(val, left, right)` / n-ary는 `children` 리스트 | |
| Infinity | `float('inf')`, `math.inf` | min/max 초기값 |

### 반열린 구간 `[i, j)` — off-by-one 방지의 핵심
- `a[2:5]` = 인덱스 2,3,4. `a[:2]` 앞 2개, `a[2:]` 끝까지.
- 장점: 인접 슬라이스가 경계를 공유 (`a[:i] + a[i:j] + a[j:]`), 길이 = `j - i`, 빈 구간 = `a[i:i]`.
- 순회는 `while`보다 `for` + `enumerate`가 안전 (조건 관리로 원소를 건너뛰는 실수 없음).

```python
from collections import deque, Counter
nums = [0, 10, 20, 30]
for i, x in enumerate(nums): ...
q = deque([1]); q.append(2); q.popleft()      # 큐
c = Counter("occur"); c['c'] == 2; c['y'] == 0
best = float('inf')
```
- 🐛 흔한 실수: `deque.pop()`은 **오른쪽** 끝 제거 — 큐로 쓸 땐 `popleft()`. `import collections.Counter`는 잘못된 문법 → `from collections import Counter`.
- 💡 스택 2개 + 유한 상태 기계 = 튜링 머신. 재귀·함수 호출도 내부적으로 스택.

---

## 14. Intro to Sorting (정렬 입문: O(n²) 정렬 3종)
> 섹션: Basic Algorithms

### 핵심 개념
- 정렬 알고리즘을 설명하는 두 속성 (+ 시간 복잡도):
  - **Stable(안정):** 같은 키의 원소들의 **원래 상대 순서 유지**. 예: 값만으로 카드 정렬 시 ♥7이 ♠7 앞에 있었다면 계속 앞에. 키가 일부 정보(무늬)를 무시할 때만 의미 있음.
  - **In-place(제자리):** 리스트 사본 없이 **O(1) 추가 메모리**(swap용 임시 변수 정도)만 사용.

### Insertion Sort (삽입 정렬)
- 왼쪽 정렬된 prefix를 한 칸씩 키움. 새 원소를 **앞 원소보다 작은 동안 왼쪽으로 swap** (카드 손패 정리).
```python
def insertion_sort(a):
    for i in range(len(a)):
        j = i
        while j > 0 and a[j] < a[j - 1]:     # strict < → 같은 값은 넘지 않음 = stable
            a[j], a[j - 1] = a[j - 1], a[j]
            j -= 1
    return a
```
- 최악 1+2+…+(n−1) = O(n²). **이미 거의 정렬된 입력이면 O(n)에 가까움.**

### Selection Sort (선택 정렬)
- 매 패스마다 미정렬 구간의 **최솟값을 찾아** 첫 미정렬 자리와 swap.
```python
def selection_sort(a):
    n = len(a)
    for i in range(n):
        m = min(range(i, n), key=a.__getitem__)   # 최솟값 인덱스
        a[i], a[m] = a[m], a[i]
    return a
```
- 항상 O(n²) 비교, swap은 최대 n번. **Unstable**: 멀리 swap하면서 같은 값의 순서를 뒤집을 수 있음 (예: `[5a, 5b, 1]` → `[1, 5b, 5a]`).

### Bubble Sort (버블 정렬)
- 인접 쌍 비교 후 순서 틀리면 swap. 한 패스마다 **가장 큰 값이 끝으로 떠오름** → 다음 패스는 끝 제외.
- **조기 종료:** 한 패스에 swap이 없으면 이미 정렬 → 종료 (정렬된 입력 O(n)).
```python
def bubble_sort(a):
    for end in range(len(a) - 1, 0, -1):
        swapped = False
        for j in range(end):
            if a[j] > a[j + 1]:
                a[j], a[j + 1] = a[j + 1], a[j]
                swapped = True
        if not swapped:
            break
    return a
```
| 정렬 | 시간 | Stable | In-place |
|---|---|---|---|
| Insertion | O(n²) (best O(n)) | ✅ | ✅ |
| Selection | O(n²) 항상 | ❌ | ✅ |
| Bubble | O(n²) (best O(n)) | ✅ | ✅ |
- 🐛 흔한 실수: insertion/bubble 비교를 `<=`/`>=`로 쓰면 같은 값도 swap → **stable 깨짐**.
- 🎤 "Selection sort는 왜 unstable?" → 최솟값을 멀리 있는 자리와 swap하면서 중간의 같은 값보다 앞선 원소를 뒤로 보내기 때문.

---

## 15. Advanced Sorting Algorithms — Merge Sort | Quick Sort (O(n log n) 정렬)

### 핵심 개념
- 둘 다 **divide & conquer**, 평균 O(n log n). 차이는 **일을 어디서 하느냐**:
  - Merge sort: 가운데서 **무작정 반으로 나누고**, **합칠 때** 정렬.
  - Quick sort: **나눌 때(partition)** pivot 기준으로 배치 → 합치는 일 없음.
- 면접에서 재발명하라고 하진 않음 → **데이터가 움직이는 방식과 성질**을 이해하는 게 목표.

### Merge Sort
- 관찰: **이미 정렬된 두 리스트 병합은 한 패스**(앞끼리 비교해 작은 것 꺼내기).
- 크기 1이 될 때까지 반씩 분할(top-down, log n 레벨) → 쌍으로 병합(bottom-up).
```python
def merge_sort(a):
    if len(a) <= 1:
        return a
    mid = len(a) // 2
    L, R = merge_sort(a[:mid]), merge_sort(a[mid:])
    out, i, j = [], 0, 0
    while i < len(L) and j < len(R):
        if L[i] <= R[j]:            # <= : 같으면 왼쪽 먼저 → stable
            out.append(L[i]); i += 1
        else:
            out.append(R[j]); j += 1
    out.extend(L[i:]); out.extend(R[j:])
    return out
```
- **복잡도:** log n 레벨 × 레벨마다 모든 원소 1번 이동 O(n) = **O(n log n) (최악도)**. 공간 O(n) — **not in-place**. **Stable.**

### Quick Sort
- pivot 선택 → **partition**: pivot보다 작은 값은 왼쪽, 크거나 같은 값은 오른쪽. pivot은 **최종 위치에 확정** → 양쪽을 재귀.
- 두 포인터 partition (마지막 원소 pivot): 왼쪽 포인터는 pivot 미만을 건너뛰고, 오른쪽 포인터는 pivot 이상을 건너뛰다가, 둘 다 멈추면 swap. 만나면 pivot을 그 자리로.
```python
def quick_sort(a, lo=0, hi=None):
    if hi is None: hi = len(a)
    if hi - lo <= 1:
        return a
    pivot = a[hi - 1]
    i, j = lo, hi - 1
    while i < j:
        while i < j and a[i] < pivot:  i += 1
        while i < j and a[j] >= pivot: j -= 1
        a[i], a[j] = a[j], a[i]
    a[i], a[hi - 1] = a[hi - 1], a[i]     # pivot 확정
    quick_sort(a, lo, i)
    quick_sort(a, i + 1, hi)
    return a
```
- **복잡도:** 평균 O(n log n). **최악 O(n²)** — pivot이 매번 최소/최대(예: 이미 정렬된 배열 + 마지막 원소 pivot). → **랜덤 pivot**으로 회피.
- In-place지만 **재귀 스택** 평균 O(log n), 최악 O(n). **Unstable.**

### 비교
| | Merge Sort | Quick Sort |
|---|---|---|
| 평균 / 최악 | O(n log n) / **O(n log n)** | O(n log n) / **O(n²)** |
| 추가 공간 | O(n) | O(log n) 스택 (최악 O(n)) |
| Stable | ✅ | ❌ |
| 실전 속도 | 데이터 이동 많음 | 보통 더 빠름 (이동 적음, 캐시 친화) |
- 라이브러리: in-place 속도용은 quick sort 변형(똑똑한 pivot), stable 정렬은 merge sort 기반.
- 🐛 흔한 실수: merge에서 `<`를 쓰면 stable 깨짐 / partition 루프에서 `i < j` 가드 누락 → 범위 밖 접근.
- 🎤 "왜 quick sort가 실전에서 빠른가?" → in-place로 메모리 이동이 적고 캐시 locality가 좋음. 최악은 랜덤 pivot으로 확률적으로 제거.
- 🔑 D&C 병합 아이디어는 "inversion count", "오른쪽의 더 작은 수 세기"에 재사용.

---

## 16. Sorting Summary (정렬 총정리)

### 언제 무엇을
| 알고리즘 | 시간 (평균/최악) | 공간 | Stable | 강점 / 쓰임 |
|---|---|---|---|---|
| Insertion | O(n²) / O(n²), 거의 정렬 시 ~O(n) | O(1) | ✅ | **거의 정렬된** 입력, 작은 n |
| Bubble | O(n²) / O(n²), 조기 종료 | O(1) | ✅ | 거의 정렬된 입력 (교육용) |
| Selection | O(n²) 항상 | O(1) | ❌ | **swap이 매우 비쌀 때** — swap 최대 n−1번 |
| Merge | O(n log n) 보장 | O(n) | ✅ | 안정성·최악 보장 필요, D&C 응용 |
| Quick | O(n log n) / O(n²) | O(log n) | ❌ | 실전 최속, 안정성 불필요 시 |
| Heap sort | O(n log n) | O(1) | ❌ | in-place + 최악 보장 |
| Tree sort (BST) | O(n log n) (균형 시) | O(n) | 가능 | BST 중위 순회 |
| **Counting sort** | **O(n + m)** | O(m) | ✅ | 정수 **값 범위 m이 작을 때** |

- 기본 정렬은 짧은 리스트에서 상수 오버헤드가 작음. 고급 정렬은 n이 커질수록 압도적.
- 마지막 원소 pivot quick sort는 거의 정렬된 입력에서 O(n²) → **중간값/랜덤 pivot**(swap 시 pivot 인덱스 이동 주의).

### 내장 정렬 (면접에선 이걸 쓰면 충분)
| 언어 | API | 알고리즘 | Stable |
|---|---|---|---|
| Python | `list.sort()`(in-place), `sorted(x)`(새 리스트) | **Timsort** (merge + 작은 구간 insertion) | ✅ |
| Java 기본형 | `Arrays.sort(int[])` | Dual-Pivot Quicksort | ❌ (기본형이라 관측 불가) |
| Java 객체 | `Arrays.sort(Object[])`, `Collections.sort` | Timsort 변형 | ✅ |

### 면접 대비 우선순위
- Insertion/Selection/Bubble: **빠르게 버그 없이** 쓸 수 있어야 (off-by-one 주의) — 루프 작성 연습.
- Merge sort: D&C 대표. **Count of Smaller Numbers After Self** 같은 실전 문제에 응용.
- Quick sort: 직접 코딩 요구는 드묾. 하지만 **pivot 기준 재배치 아이디어는 Two Pointers**(Dutch flag, partition)에 등장.

```python
def counting_sort(a, max_val):
    cnt = [0] * (max_val + 1)
    for x in a:
        cnt[x] += 1
    return [v for v, c in enumerate(cnt) for _ in range(c)]   # O(n + m)
```
- 🎤 "값이 0–100 사이 정수 백만 개를 정렬하라" → counting sort, O(n + 101).

---

## 17. Built-in Sort with Custom Comparator (내장 정렬 + 커스텀 비교자)

### 핵심 개념
- 기본 순서가 아닌 순서로 정렬하려면 비교 함수/키를 넘긴다. 커스텀 "순서"는 두 규칙을 지켜야 결과가 일관됨:
  - **Transitivity(추이성):** a<b, b<c ⇒ a<c
  - **Asymmetry(비대칭성):** a<b이면 b<a가 아니어야 함

### Python: `key`가 기본, 필요하면 `cmp_to_key`
```python
from functools import cmp_to_key
nums.sort(reverse=True)                                  # 내림차순
tasks = [("Cook dinner", 5), ("Buy grocery", 3)]
sorted(tasks, key=lambda t: t[1])                        # 우선순위 오름차순
sorted(tasks, key=lambda t: (-t[1], t[0]))               # 우선순위 내림, 동률이면 이름 오름 (튜플 키)
sorted(tasks, key=cmp_to_key(lambda a, b: a[1] - b[1]))  # 비교 함수 방식
# 학생을 총점 기준 정렬
sorted(students, key=lambda s: s.math + s.english)
```

### 언어별 치트 시트
| 언어 | 기본 정렬 | 커스텀 | 비교 규약 |
|---|---|---|---|
| Python | `list.sort()`, `sorted()` (stable) | `key=`, `reverse=`, `cmp_to_key` | cmp 음수 → a 먼저 |
| Java | `Arrays.sort`, `Collections.sort` | `Comparator` 람다, `Collections.reverseOrder()` | 음수 → a 먼저, 양수 → b 먼저, 0 → 유지. `Integer.compare` 사용 |
| JavaScript | `arr.sort()` — **문자열로 변환해 비교!** | `sort((a,b) => a - b)` | 음수 → a 먼저 |
| C++ | `std::sort` (unstable), `std::stable_sort` | 3번째 인자 `comp(a,b)` → **a가 앞이면 true** (strict weak ordering), `std::greater<int>()` | bool 반환 |
| C# | `Array.Sort`, `List.Sort` (introsort) | `(a,b) => b.CompareTo(a)` | 정수 반환 |
| Go | `sort.Ints`, `sort.Strings` | `sort.Slice(x, func(i,j int) bool {...})` | less 함수 |

- 🐛 흔한 실수:
  - JS `[40,100,1,5].sort()` → `[1,100,40,5]` (문자열 비교). 반드시 비교 함수.
  - C++ comparator에 `<=` 사용 → strict weak ordering 위반 → UB/크래시 가능.
  - Java에서 `a - b` 비교는 **정수 오버플로** 위험 → `Integer.compare(a, b)`.
- 🔑 "우선순위 순으로", "동률이면 ~ 순으로" → 튜플 key 정렬. Interval 문제의 첫 단계(시작점/끝점 기준 정렬)에서 매번 사용.

---

## 18. Simulation Coding Problems: Introduction and Strategies (시뮬레이션 문제)
> 섹션: Simulation

### 핵심 개념
- 문제에 서술된 시스템/과정을 **그대로 흉내 내는** 프로그램. 고급 알고리즘보다 **정확한 모델링과 꼼꼼한 구현**이 관건.
- 특징 4가지: ① **상태 관리** ② **단계별 실행**(규칙에 따라 상태 갱신) ③ **조건 분기** ④ **엣지 케이스** 처리.

### 예제: Design Parking System
**문제:** big/medium/small 칸 수로 초기화. `addCar(carType)`(1=big, 2=medium, 3=small)는 같은 크기 칸이 남아 있으면 주차하고 True, 없으면 False.
예: `ParkingSystem(1, 1, 0)` → `addCar(1)` True, `addCar(3)` False, `addCar(1)` False.

**핵심 관찰:** 상태 = 타입별 남은 칸 수 3개. 차가 오면 해당 카운터만 확인·감소.
```python
class ParkingSystem:
    def __init__(self, big: int, medium: int, small: int):
        self.slots = [0, big, medium, small]    # carType을 그대로 인덱스로 사용

    def addCar(self, carType: int) -> bool:
        if self.slots[carType] == 0:
            return False
        self.slots[carType] -= 1
        return True
```
**복잡도:** 연산당 O(1), 공간 O(1).
- 🐛 함정: 초기 칸이 0인 타입 → 항상 False. 큰 칸에 작은 차를 세우는 식의 "대체 주차"는 **허용 안 됨**(문제 조건을 추측하지 말고 확인).

### 풀이 전략
1. 문제를 읽고 **상태 변수**를 먼저 정의 (좌표, 방향, 카운터, 보드).
2. 한 스텝의 **전이 규칙**을 함수로 분리.
3. 엣지 케이스(빈 입력, 경계, 0개)를 코드 전에 나열.
4. 작은 예제로 손으로 따라가며 검증.

### 대표 연습 문제 (모두 Easy)
| 문제 | 상태 | 한 줄 요령 |
|---|---|---|
| Fizz Buzz | i | 15 → 3 → 5 순서로 검사 |
| Robot Return to Origin | (x, y) | U/D/L/R 누적, 끝에 (0,0)인지 |
| Design Parking System | 타입별 칸 수 | 위 코드 |
| Transpose Matrix | 새 행렬 | `res[j][i] = m[i][j]` (R×C → C×R) |
| Design Tic-Tac-Toe | 행/열/대각선 합 | 플레이어 +1/−1 누적, |합| == n이면 승리 → move O(1) |

- 🔑 "Design ~ system", "시뮬레이트하라", "로봇이 움직인다", "게임 규칙" → Simulation.
- 🎤 "특별한 알고리즘보다 상태를 명확히 정의하고, 각 단계 규칙을 함수로 분리해서 엣지 케이스를 놓치지 않겠습니다."

---

## 용어·패턴 사전

| 용어 | 뜻 |
|---|---|
| ROI | 투자 대비 효과. 출제 빈도 높은 패턴(DFS/BFS/Two Pointers)부터 |
| PTS | Patterns → Templates → Speedrun 학습 시스템 |
| Speedrun | 코딩 없이 객관식으로 패턴 인식을 빠르게 훈련 |
| Big-O | 입력 증가에 따른 실행 시간 증가율의 상한 (상수·저차항 무시) |
| Amortized | 드문 비싼 연산을 전체에 나눈 연산당 평균 비용 (동적 배열 append O(1)) |
| 10⁷ 연산 규칙 | 1초에 약 1천만~2천만 연산 → 제약 n으로 허용 복잡도 역산 |
| LIFO / FIFO | 스택(후입선출) / 큐(선입선출) |
| Deque | 양끝에서 O(1) 삽입·삭제 가능한 큐 (`collections.deque`) |
| Circular buffer | 인덱스를 `% cap`으로 감아 빈 칸을 재사용하는 배열 큐 |
| Hash collision | 다른 키가 같은 해시값 → separate chaining / open addressing |
| Load factor | 원소 수 / bucket 수 (n/k). 작게 유지해야 O(1) |
| 반열린 구간 `[i, j)` | 왼쪽 포함·오른쪽 제외. 길이 j−i, 빈 구간 a[i:i] |
| Stable sort | 같은 키 원소의 원래 순서 유지 |
| In-place | O(1) 추가 메모리로 동작 |
| Divide & Conquer | 쪼개서 풀고 합치기 (merge sort, quick sort) |
| Partition / Pivot | pivot 기준으로 작은 값·큰 값을 양쪽으로 재배치 |
| Timsort | Python/Java 객체 정렬. merge + insertion 하이브리드, stable |
| Counting sort | 값 범위 m이 작을 때 O(n + m) 정렬 |
| Comparator | 커스텀 순서 함수. 추이성·비대칭성 필수 |
| Simulation | 규칙대로 상태를 단계별로 갱신하며 과정을 모사하는 문제 |
