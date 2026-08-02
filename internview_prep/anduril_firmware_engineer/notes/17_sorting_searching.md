# 17. Sorting & Searching (정렬 & 탐색) · Sort/Search 🔎

> **정렬된 데이터를 log 시간에 때려잡는** 드릴. 이진탐색 3형제(정확 매칭 /
> lower·upper bound / 회전배열) + 고전 정렬 4종(merge/quick/counting/insertion)
> + quickselect(k번째) + **두 배열 최근접 매칭**(브루트포스 → 정렬+이진탐색).
> 마지막 문제는 **Anduril 폰스크린 기출**로 알려진 유형이다.

---

## 왜 이 토픽이 인터뷰에 나오나

- 이진탐색은 **off-by-one의 지뢰밭**이다. `[lo,hi]` 폐구간 vs `[lo,hi)` 반개구간,
  `mid=(lo+hi)/2` 오버플로, `size_t` 언더플로 — 면접관이 가장 좋아하는 디테일.
- 정렬은 **트레이드오프 대화**다. 안정성? 추가 메모리? 최악 O(n²)? 캐시 지역성?
  "왜 이 상황에서 merge 대신 quick/counting/insertion?"을 설명할 수 있어야 한다.
- **quickselect**는 "전부 정렬하지 말고 k번째만 O(n)에 뽑아라"는 사고 전환.
- **두 배열 최근접 매칭**은 브루트포스 O(n·m)를 **정렬+이진탐색 O((n+m)log m)**로
  줄이는 전형적 스케일업 문제. Anduril 폰스크린에서 실제로 나온 것으로 보고된 문제.

---

## 핵심 개념 치트시트

| 개념 | 요점 |
|---|---|
| **binary_search** | 폐구간 `[lo,hi]`, `mid=lo+(hi-lo)/2`(오버플로 회피). `lo<=hi` 조건, `mid==0` 언더플로 방어. |
| **lower_bound** | `a[i] >= target` 첫 위치. 반개구간 `[lo,hi)`, `a[mid]<target → lo=mid+1`. 없으면 n. |
| **upper_bound** | `a[i] > target` 첫 위치. `a[mid]<=target → lo=mid+1`. **개수 = ub - lb**. |
| **search_rotated** | 회전배열. 매 스텝 **절반은 정렬돼 있다** → 그 범위에 target 포함 여부로 방향 결정. O(log n). |
| **merge_sort** | 안정, O(n log n), **O(n) 보조버퍼**. 외부정렬/연결리스트에 강함. 병합이 핵심. |
| **quicksort** | 평균 O(n log n), 최악 O(n²). in-place, 불안정. **median-of-3**로 최악 완화. |
| **quickselect** | 분할 후 **k가 든 쪽만** 재귀 → 평균 **O(n)**. 전체 정렬(n log n) 불필요. |
| **counting_sort** | 값이 `[0,K]`면 O(n+K). 비교정렬 하한(n log n) **우회**. 사실상 히스토그램. |
| **insertion_sort** | O(n²)지만 **거의 정렬된/작은 n**에서 최고. quicksort 재귀 바닥 전환용. |
| **nearest match** | 브루트포스 O(n·m) → **B 정렬 + 이진탐색** O((n+m)log m). |

---

## 이진탐색 3형제 — 경계 규칙 정리

```text
binary_search : [lo, hi]  폐구간   while(lo<=hi)  hi=mid-1 / lo=mid+1
lower/upper   : [lo, hi)  반개구간 while(lo<hi)   hi=mid   / lo=mid+1
```

- **폐구간**: `hi=n-1`에서 시작, `lo<=hi`가 조건. `hi=mid-1`이라 `mid==0`이면
  `size_t` 언더플로 → `if(mid==0) break;` 가드가 필수.
- **반개구간**: `hi=n`에서 시작, `lo<hi`가 조건. `hi=mid`(감소만) → 언더플로 없음.
  lower/upper bound·삽입점 계산은 반개구간이 훨씬 깔끔하다.
- `mid = lo + (hi-lo)/2` 는 `(lo+hi)` 오버플로를 막는 관용구. 임베디드 32비트에서 실제로 중요.

---

## 문제별 요점 & 함정

### 1. binary_search
- 폐구간 정석. `a[mid]==target` 즉시 반환, `<`면 오른쪽, `>`면 왼쪽.
- 함정: `hi=mid-1`에서 `mid==0` 언더플로, `mid=(lo+hi)/2` 오버플로, `lo<hi`로 잘못 쓰면 마지막 원소 누락.

### 2. lower_bound / upper_bound
- 차이는 비교 연산자 하나: lower는 `a[mid]<target`, upper는 `a[mid]<=target`일 때 `lo=mid+1`.
- **중복값**의 시작/끝을 잡는 도구. `ub-lb`가 등장 횟수. 삽입 위치(정렬 유지)도 곧 lower_bound.
- 함정: "찾았다/못 찾았다"를 반환하려면 `lb<n && a[lb]==target` 별도 확인.

### 3. search_rotated
- `a[lo]<=a[mid]`면 **왼쪽 절반이 정렬됨** → `a[lo]<=target<a[mid]`면 왼쪽, 아니면 오른쪽.
- 아니면 오른쪽 절반이 정렬됨 → `a[mid]<target<=a[hi]`면 오른쪽, 아니면 왼쪽.
- 함정: 중복 있으면 `a[lo]==a[mid]` 판정 모호 → 최악 O(n)로 퇴화(이 문제는 중복 없음 가정).

### 4. merge_sort
- 안정 정렬. 병합에서 `a[i]<=a[j]`로 **왼쪽 우선**해야 안정성 유지.
- 보조 버퍼를 **한 번만** 할당해 재귀에서 재사용(매 호출 malloc 금지).
- 함정: 병합 후 `tmp`를 원본으로 되쓰기 누락, `mid` 경계 off-by-one.

### 5. quicksort (Lomuto)
- Lomuto: 피벗=마지막 원소, `a[lo..i)<pivot` 불변식 유지하며 스캔, 끝에 피벗 스왑.
- **median-of-3**로 이미 정렬/역정렬 입력의 O(n²)를 완화. 작은 쪽만 재귀 → 스택 O(log n).
- 함정: 중복 많은 배열에서 Lomuto는 불균형(3-way/Hoare가 유리), `p-1` 언더플로.

### 6. quickselect_kth
- quicksort와 같은 분할이되 **k가 든 쪽만** 재귀 → 평균 O(n), 최악 O(n²).
- k는 0-index(0=최소). 피벗 위치 `p==k`면 즉시 답. 배열은 재배치됨(부분 정렬).
- 함정: `k>=n` 방어, `p==0` 언더플로. 최악 회피엔 median-of-medians(이론), 실무는 랜덤 피벗.

### 7. counting_sort
- 값 범위 `[0,K]`가 작을 때 O(n+K). 히스토그램 집계 후 값 순서로 펼침.
- prefix-sum 변형을 쓰면 **안정 정렬**로 만들 수 있고 radix sort의 한 자리 단계가 된다.
- 함정: K가 크면 메모리 폭발, 음수/범위 밖 값 방어, `int` 인덱스 오버플로.

### 8. nearest_match_two_arrays  ★ Anduril 폰스크린 기출
- **문제**: A의 각 원소마다 B에서 가장 가까운 값을 찾고, 전체 최대 거리도 반환.
- **(a) 브루트포스**: 각 A[i]마다 B 전체 스캔 → **O(n·m)**. 코드 짧고 정답이지만 느림.
- **(b) 스케일업 팔로업**: B를 **정렬(O(m log m))** → 각 A[i]를 **lower_bound 이진탐색**으로
  삽입점 찾고 그 **좌/우 두 후보만** 비교 → **O((n+m) log m)**.
- 왜 두 후보만? 정렬된 B에서 A[i]에 가장 가까운 값은 반드시 삽입점 바로 앞/뒤에 있다.
- 동거리(tie) 처리 규칙을 정해야 두 구현의 결과가 일치한다(여기선 '더 큰 값' 선택).
- 함정: `pos==m`(모든 B<A[i])면 우측 후보 없음 → 마지막 원소, `pos==0`이면 좌측 없음.
  거리 계산 `|A[i]-B[j]|`는 `long`으로 승격해 int 오버플로 방지.
- **면접 포인트**: "n,m이 수천×수천이 되면?"에 즉시 정렬+이진탐색으로 답하고,
  n이 아주 크고 B가 고정이면 **B를 한 번만 정렬**해 재사용(전처리 분리)한다고 덧붙인다.

### 9. insertion_sort
- 안정, in-place, O(1) 추가공간. 거의 정렬된 입력은 O(n)에 근접(각 원소 살짝만 이동).
- 함정: 시프트 방향 실수, `j>0 && a[j-1]>key` 조건에서 `>`(안정) vs `>=`(불안정) 구분.

---

## 인터뷰 팔로업 (자주 나오는 꼬리질문)

1. **"이진탐색 오버플로/언더플로 어디서 나나?"** → `(lo+hi)/2` 오버플로, `hi=mid-1`의 `mid==0` 언더플로.
2. **"lower_bound와 upper_bound 차이?"** → 비교 연산자 `<` vs `<=`. `ub-lb`가 중복 개수.
3. **"merge vs quick 언제?"** → 안정성/최악보장/외부정렬이면 merge, in-place/평균속도/캐시면 quick.
4. **"quicksort 최악은?"** → 이미 정렬된 입력에 끝 피벗 → O(n²). median-of-3/랜덤 피벗으로 완화.
5. **"k번째만 필요한데 전부 정렬?"** → 아니오, quickselect 평균 O(n).
6. **"counting sort 언제 못 쓰나?"** → 값 범위가 크거나 실수/임의 키일 때. 그땐 비교정렬/radix.
7. **"두 배열 최근접을 더 빠르게?"** → B 정렬 후 이진탐색 O((n+m)log m). B가 고정이면 정렬 1회 재사용.

---

## 빌드 & 실행

```sh
# 연습(직접 채우기)
cc -std=c11 -Wall -Wextra problems/17_sorting_searching.c -o /tmp/andb_17 && /tmp/andb_17
# 정답 확인 (전부 PASS)
cc -std=c11 -Wall -Wextra solutions/17_sorting_searching.c -o /tmp/andb_17 && /tmp/andb_17
# 또는
make prob N=17_sorting_searching      #  연습
make sol  N=17_sorting_searching      #  정답
```

> **핵심 한 줄**: "데이터가 정렬돼 있으면 선형 스캔 대신 **log 시간**이다."
> 이진탐색으로 룩업 테이블/캘리브레이션 값을 찾고, 정렬로 최근접 매칭을 스케일업하며,
> counting sort로 센서 히스토그램을 O(n)에 세운다.
