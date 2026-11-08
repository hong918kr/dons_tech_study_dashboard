# AlgoMonster Coding Patterns → 스터디노트 커리큘럼 매핑표

- 소스: `algomaster-coding-pattern/original-md/` (M01–M11, 222 레슨 한국어 요약, 2026-09-27)
- 대상: 주로 `ALGO`(42) · `DS`(39), 보조로 `C`·`OS`·`SYSD`
- 강도: ◎ 직접 겹침 · ○ 부분 겹침 · △ 주변 · ✕ 없음(갭)
- 핵심 차이: 소스는 **LeetCode형 패턴 문제**(트리·그래프·DP·구간), 우리 `ALGO`/`DS`는 **임베디드/시스템 맥락 문제**(CRC·링 버퍼·할당기). 같은 알고리즘이라도 문제 도메인이 달라서, 코딩테스트 대비 패턴은 상당 부분 **갭**이다.

## 한눈에 보기

| 모듈 | 주제 | 대응 | 커버 |
|---|---|---|---|
| M01 | Getting Started (스택/큐/해시/정렬/치트시트) | DS-B02/B09/B10, ALGO-B05~B08 | ○ |
| M02 | Binary Search | ALGO-B02~B04 | ◎ |
| M03 | Two Pointers / Sliding Window / Prefix Sum / Fast-Slow | ALGO-B09, B10, M13, DS-B05 | ○ |
| M04 | DFS · Tree · BST | DS-B13, DS-M07, ALGO-B11 | △ **트리 문제 갭** |
| M05 | Backtracking · Memoization | ALGO-M03, M07 | ○ |
| M06 | BFS (트리 레벨 순회) | ALGO-M04 | △ |
| M07 | Graph (BFS/DFS/위상정렬/Dijkstra/MST) | ALGO-M04~M06, A02, A10 | ○ MST ✕ |
| M08 | Heap / Priority Queue | DS-B12, DS-M11, ALGO-M11 | ◎ |
| M09 | Dynamic Programming | ALGO-M07, M08, A03 | ○ 변형 다수 ✕ |
| M10 | DSU / Trie / LRU / Segment Tree | ALGO-M14, DS-B14, DS-M05 | ○ Segment Tree ✕ |
| M11 | Intervals / Monotonic Stack / D&C / Sweep / Greedy / Sieve | ALGO-M09, A09, A11 | △ |

결론: **코딩 인터뷰 패턴 중 약 절반(이분탐색·힙·DSU·Trie·LRU·위상정렬·Dijkstra·기본 DP)은 이미 아이템이 있고, 트리/BST·monotonic stack·구간(intervals)·세그먼트 트리·MST·DP 변형(grid/interval/knapsack 변종)은 갭.**

---

## M01 Getting Started

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1~5 패턴 통계 · 로드맵 · 면접 프로세스 · 공부법 · 마인드셋 | CAP 트랙 (인터뷰 준비) | △ | 메타 정보. 아이템화 불필요 |
| 6 Math Basics | ALGO-B01, ALGO-B12, ALGO-M15 | ○ | |
| 7 Runtime → Algo 치트시트 | ALGO-B01 | ○ | 입력 크기로 알고리즘 역추론 — 레슨 보강 자료로 유용 |
| 8 Keyword → Algo 치트시트 | (없음) | ✕ | 패턴 인식 훈련 아이템 없음 |
| 10 Stack Intro | DS-B02 | ◎ | |
| 11 Queue/Deque Intro | DS-B03, DS-B04 | ○ | 링 버퍼 중심 |
| 12 Hashmap Intro | DS-B09, DS-B10, DS-B11 | ◎ | |
| 14~16 정렬 (O(n²), merge, quick) | ALGO-B05, B06, B07, B08 | ◎ | |
| 17 Built-in Sort + Comparator | (없음) | △ | C의 `qsort` 비교 함수는 C 트랙에서 일부 다룸 |
| 18 Simulation 문제 | ALGO-B15, ALGO-M12 | ○ | 파서/상태기계형 |

## M02 Binary Search

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1~4 Intro / First True / Monotonic / Lower bound | ALGO-B02 | ◎ | |
| 5 First Occurrence | ALGO-B02 | ◎ | |
| 6 Square Root Estimation | ALGO-M15, ALGO-B04 | ◎ | isqrt |
| 7 Rotated Sorted Array 최솟값 | (없음) | ✕ | 소규모 갭 |
| 8 Peak of Mountain Array | (없음) | ✕ | 소규모 갭 |
| 9 Newspapers (답 이분탐색) | ALGO-B04 | ◎ | Parametric Search |
| 10 Speedrun | — | — | |
| (참고) 보정 테이블 보간 | ALGO-B03 | — | 소스엔 없는 임베디드 응용 |

## M03 Two Pointers / Sliding Window / Prefix Sum

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1~4 Invariant · Same Direction | ALGO-B09 | ○ | |
| 5 Remove Duplicates · 7 Move Zeros | ALGO-B09 | ◎ | |
| 6 Middle of Linked List · 8 Remove N-th from End | DS-B05 | ○ | 연결 리스트 연산 |
| 10 Two Sum Sorted · 11 Valid Palindrome · 12 Container | ALGO-B09 | ○ | 쌍 합만 직접 |
| 14~19 Sliding Window (fixed/longest/shortest) | ALGO-B10 | ◎ | 이동 평균·최장 구간 |
| 15 Anagrams · 27 Minimum Window Substring | ALGO-B10, ALGO-A04 | △ | 해시 카운트 윈도 변형 |
| 20~23 Prefix Sum (구간 합, 곱 제외) | ALGO-M13 | ◎ | 1D·2D |
| 25 Linked List Cycle (Fast/Slow) | DS-B05 | ◎ | |
| 28 Teleporter Arrays | (없음) | ✕ | |

## M04 DFS · Tree · BST

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1 Recursion Intro | ALGO-B11 | ◎ | 재귀↔반복 |
| 2~4 Trees · DFS on Tree | DS-B13 | ○ | 순회 중심 |
| 5~9 Max Depth / Visible Node / Balanced / Subtree / Invert | (없음) | ✕ | **트리 재귀 문제 갭** |
| 10~13 BST Intro / Valid / Insert / LCA of BST | DS-B13 | ○ | 삽입·삭제·순회 |
| 14 Reconstruct from Preorder+Inorder | (없음) | ✕ | |
| 15 Serialize / Deserialize Tree | (없음) | ✕ | |
| 16 Lowest Common Ancestor | (없음) | ✕ | |
| (참고) 균형 트리 | DS-M07 AVL, DS-A05 RB, DS-A08 B+ | — | 소스에는 없는 시스템형 |

## M05 Backtracking · Memoization

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1~9 DFS with States · 전화번호 조합 · 가지치기 · 괄호 · 순열 | ALGO-M03 | ◎ | 부분집합·순열·N-Queens |
| 5 Partition String Into Palindromes | ALGO-M03 | ○ | |
| 10 Memoization | ALGO-M07 | ○ | |
| 11 Word Break · 12 Decode Ways · 13 Min Coins | ALGO-M07 | ○ | 동전 교환 ◎, 나머지는 변형 |
| 14~16 Deduplication · Combination Sum · Subsets | ALGO-M03 | ◎ | |

## M06 BFS

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1 BFS Intro | ALGO-M04 | ◎ | 격자 기반 |
| 2~5 Level Order / ZigZag / Right Side / Min Depth | (없음) | ✕ | 트리 BFS 갭 (M04 트리 갭과 같이 해결) |

## M07 Graph

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1~5 Graph Intro · BFS/DFS · Shortest Path | ALGO-M04, ALGO-M06 | ◎ | |
| 6 Clone Graph | (없음) | ✕ | |
| 7~15 Matrix as Graph · Flood Fill · Islands · Knight · Walls&Gates · Word Ladder · Lock · Sliding Puzzle | ALGO-M04 | ○ | 점유 격자 탐색 1개로 전체 대표 |
| 16~21 Topological Sort · Task Scheduling · Alien Dictionary · Course Schedule | ALGO-M05, ALGO-M10 | ◎ | |
| 22 Dijkstra | ALGO-M06, ALGO-A02 | ◎ | |
| 23~24 MST (Kruskal/Prim) · Forests | (없음; ALGO-M14 DSU로 기반만) | ✕ | **MST 아이템 없음** |
| (참고) SCC | ALGO-A10 | — | 소스엔 없는 확장 |

## M08 Heap / Priority Queue

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1 Heap Intro | DS-B12 | ◎ | |
| 2~5 K Closest · Merge K Lists · Kth Largest · Kth in Matrix | ALGO-M11 | ◎ | Top-K |
| 6 Reorganize String · 7 Ugly Number | ALGO-M10 | △ | |
| 8 Median of Data Stream | ALGO-M11 | ◎ | 스트리밍 중앙값 |
| (참고) decrease-key | DS-M11 | — | |

## M09 Dynamic Programming

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1~7 Intro · Climbing Stairs · Tribonacci · House Robber · Min Cost · Tickets | ALGO-M07 | ◎ | 1D DP |
| 8~14 Grid DP (Unique Paths · Min Path Sum · Maximal Square · Triangle · Dungeon) | (없음; ALGO-M08은 2D지만 LCS/편집거리 중심) | ✕ | **Grid DP 갭** |
| 15~20 Dual-Sequence (LCS · Edit Distance · Distinct Subsequences · SCS) | ALGO-M08 | ◎ | |
| 21~25 Non-constant Transition (LIS · Partition · Divisible Subset) | ALGO-M07 | ○ | LIS만 직접 |
| 26~37 Knapsack (weight-only · Subset Sum · Target Sum · Unbounded · Coin Change II · Perfect Squares · 0/1 · Bounded) | ALGO-M08 | ○ | 0/1 배낭만 직접, **변형 다수 갭** |
| 38~40 Interval DP (Palindromic Substrings · Coin Game) | (없음) | ✕ | **Interval DP 갭** |
| (참고) 비트마스크 DP | ALGO-A03 | — | 소스엔 없는 확장 |

## M10 Advanced Data Structures

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1~7 DSU · Path Compression · Union by Rank · 응용 | ALGO-M14 | ◎ | |
| 8~12 Trie · Autocomplete · Prefix Count · Word Search II | DS-B14, DS-M13 | ◎ | 명령어 파서/LPM |
| 13 LRU Cache | DS-M05 | ◎ | |
| 14~15 Segment Tree · Range max | (없음) | ✕ | **세그먼트 트리/Fenwick 갭** |

## M11 Miscellaneous

| 소스 섹션 | 대응 아이템 | 강도 | 메모 |
|---|---|---|---|
| 1~8 Intervals (Merge · Insert · Meeting Rooms · Non-overlapping · Arrows · Partition Labels) | ALGO-M09, ALGO-A09 | ○ | 그리디 구간 스케줄링·스위프 |
| 9 Min Stack | DS-B02 | △ | 스택 확장 |
| 10 Basic Calculator | ALGO-B15 | △ | 파싱 |
| 11~15 Monotonic Stack (Sliding Window Max · Daily Temp · Next Greater · Histogram) | (없음) | ✕ | **Monotonic stack/deque 갭** |
| 16~18 Divide & Conquer (Skyline · Count Smaller) | (없음; ALGO-B07 머지소트만) | ✕ | |
| 19~20 Line Sweep (Union Area) | ALGO-A09 | ◎ | |
| 21 Closest BST Values II | (없음) | ✕ | |
| 22~23 Greedy (Gas Station) | ALGO-M09, ALGO-M10 | ○ | |
| 24~25 Prime Sieve | (없음) | ✕ | |
| 26 Sparse Matrix Multiplication | DS-M12 | ○ | CSR |

---

## 갭 요약 (새 아이템 후보, 우선순위순)

1. **트리 재귀/BFS 패턴** — Max depth, balanced, invert, LCA, 레벨 순회, 직렬화. (M04, M06) 후보: `DS` Mid 또는 `ALGO` 신규 2~3개.
2. **Monotonic stack/deque** — Next Greater, Sliding Window Max, Histogram. (M11) 후보: `ALGO` Mid.
3. **Grid DP + Interval DP + Knapsack 변형** — (M09) 후보: `ALGO-M08` 확장 또는 신규 ALGO-M16~M17.
4. **Segment Tree / Fenwick** — 센서 윈도 구간 쿼리로 임베디드 맥락 가능. (M10) 후보: `DS` Adv.
5. **MST (Kruskal/Prim)** — DSU 이후 자연스러운 연결. (M07)
6. **구간(Intervals) 패턴** — Merge/Insert/Meeting Rooms. 메모리 영역 병합으로 임베디드 맥락 가능. (M11)
7. 소규모: Rotated array 이분탐색, Prime sieve, Divide & Conquer(Skyline).

## 방향성 메모

- 이 갭들은 **코딩테스트(LeetCode형) 대비 용도**다. 임베디드 면접(Anduril/Rivian 류)은 이미 기존 아이템으로 충분하고, 갭을 채우려면 기존처럼 "임베디드 맥락으로 변환한 문제"로 만드는 편이 일관적이다 (예: 구간 병합 → MPU/메모리 영역 병합, segment tree → 센서 슬라이딩 구간 최대).
- 순수 LeetCode 풀이 연습은 `algomaster-coding-pattern/site/` 소스 노트를 그대로 읽는 편이 낫고, 커리큘럼에는 중복 추가하지 않아도 된다.
- 소스의 `Keyword → Algo`, `Runtime → Algo` 치트시트(M01-7/8)는 아이템보다 **ALGO 트랙 도입 레슨의 참고 표**로 쓰기 좋다.
