# 11. Arrays & Hashing (배열 & 해싱) · Arrays/Hash 🗂️

> STL 없는 C로 **해시맵/해시셋을 직접 구현**하고, prefix-sum·two-pointer·sliding
> window 로 브루트포스 O(n²) 를 O(n) 으로 접는 드릴. Anduril 펌웨어/DSA 라운드의
> "배열을 한 번만 훑어서 풀 수 있나? 해시를 C로 직접 짜봐라" 유형.

---

## 왜 이 토픽이 인터뷰에 나오나

- C 인터뷰에는 `unordered_map` 이 없다. **개방주소법(open addressing) 해시맵을
  손으로 짜는 능력** 자체가 신호다 — 해시 함수, 충돌 처리, 부하율(load factor).
- 임베디드/텔레메트리는 **한 번의 패스로 스트림을 처리**해야 한다. prefix-sum(누적
  센서합), sliding window(이동평균/버스트 검출), two-pointer 는 O(1) 추가 상태로
  O(n) 을 달성하는 실전 패턴이다.
- `product_except_self` 와 `top_k_frequent` 는 리포트된 실제 Anduril 스타일 문제.
  전자는 "나눗셈 없이", 후자는 "빈도 집계 + 정렬"을 물어 자료구조 설계를 본다.

---

## 핵심 패턴 치트시트

| 패턴 | 요점 | 복잡도 |
|---|---|---|
| **hashmap (open addressing)** | 값->인덱스/카운트. 선형 탐사, cap=2^k 마스킹. | 평균 O(1) 조회 |
| **hashset** | 존재 여부만. 해시맵의 val 없는 버전. | 평균 O(1) |
| **prefix sum** | `pre[k]=a[0..k-1]합`. 구간합 = `pre[r+1]-pre[l]`. | 전처리 O(n), 질의 O(1) |
| **Kadane** | `cur=max(x, cur+x); best=max(best,cur)`. | O(n), O(1) |
| **sliding window (고정 k)** | `sum += in[i] - in[i-k]`. | O(n), O(1) |
| **two-pointer (정렬)** | 양끝에서 합 비교로 lo++/hi--. | O(n), O(1) |
| **in-place 압축** | write 포인터로 원하는 원소를 앞으로. | O(n), O(1) |
| **Boyer-Moore 투표** | 후보+카운트, count==0 이면 교체. | O(n), O(1) |

---

## 해시맵을 C로 직접 짜기 (이 토픽의 심장)

```c
typedef struct { int key, val; bool used; } HMSlot;
typedef struct { HMSlot *s; size_t cap; } HashMap;

static size_t hash_int(int key, size_t cap) {   // Knuth 곱셈 해시, 음수 안전
    uint32_t x = (uint32_t)key;
    x *= 2654435761u;                            // 2^32 / 황금비
    return (size_t)x & (cap - 1);                // cap 은 2의 거듭제곱
}
// 삽입/조회: h = hash; 빈 슬롯 만날 때까지 (h+1)&(cap-1) 선형 탐사.
```

- **cap 을 2의 거듭제곱**으로 잡으면 `% cap` 대신 `& (cap-1)` — 나눗셈 회피(임베디드에서 중요).
- **부하율**: 필요 원소수의 2배(≤0.5)를 잡아 탐사 길이를 짧게. 넘치면 실무는 rehash.
- **삭제**는 tombstone 필요(이 드릴은 삽입/조회만 쓰므로 생략).
- 대안은 **체이닝**(슬롯마다 연결 리스트) — 삭제 쉽고 부하율에 관대하나 포인터/할당 비용.

---

## 문제별 요점 & 함정

### 1. two_sum (hashmap)
- 값->인덱스 맵을 채우며 `target-nums[i]` 조회. 찾으면 (이전 인덱스, i). **한 패스**로 O(n).
- 함정: `nums[i]` 를 조회 **후** 삽입해야 자기 자신과 짝짓지 않음. 중복값(`{3,3}`) 정상.

### 2. contains_duplicate (hashset)
- 삽입 중 이미 존재하면 즉시 true. NULL/빈/단일은 false.
- 함정: 정렬(O(n log n)) 대신 해시로 O(n). 값 범위가 좁으면 비트셋도 고려.

### 3. product_except_self (Anduril 리포트)
- **나눗셈 금지**. 좌측 누적곱을 out 에 쓰고, 우측 누적곱을 되돌아오며 곱한다 → O(n), 출력 외 O(1).
- 함정: 0 이 하나면 그 자리만 곱 살아남음. 0 이 둘 이상이면 전부 0. 나눗셈 풀이는 0 에서 터짐.

### 4. range_sum (prefix-sum)
- `pre[k]=nums[0..k-1]합`, `sum[l..r]=pre[r+1]-pre[l]`. **질의 여러 번**이면 전처리 후 질의당 O(1).
- 함정: `size_t` 언더플로/역구간(`l>r`)·범위 초과(`r>=n`) 가드. 오버플로 방지 누적은 `long`.

### 5. max_subarray (Kadane)
- `cur=max(x, cur+x); best=max(best,cur)`. 전부 음수면 best=최댓값(단일 원소).
- 함정: best 를 0 이 아니라 **nums[0]** 으로 초기화(전부 음수 케이스). 빈 배열 정책 명시.

### 6. max_sum_window_k (고정 sliding window)
- 첫 창 합을 구한 뒤 `sum += nums[i]-nums[i-k]` 로 슬라이드 → O(n).
- 함정: `k==0`·`k>n` 가드. 매 창을 다시 더하면 O(n·k) — 슬라이드로 O(n).

### 7. pair_sum_sorted (two-pointer)
- **정렬 전제**. lo=0,hi=n-1 에서 합 비교: 크면 hi--, 작으면 lo++.
- 함정: 정렬 안 된 입력엔 못 씀(그땐 해시). `int` 합 오버플로는 `long` 으로.

### 8. move_zeroes (in-place)
- write 포인터로 비-0 을 앞으로 압축 후 나머지 0 채움. 상대순서 유지, O(1) 추가공간.
- 함정: 스왑 방식은 불필요한 쓰기 발생. 순서 유지가 요구조건.

### 9. majority_element (Boyer-Moore)
- 후보 하나 + 카운트만. `count==0` 이면 후보 교체, 같으면 ++, 다르면 --. **O(1) 공간**.
- 함정: 다수 원소가 **존재한다는 가정**. 없으면 결과 무의미 → 실무는 2패스 검증 추가.

### 10. top_k_frequent (Anduril 리포트, hashmap+sort)
- 해시맵으로 빈도 집계 → (값,빈도) 배열 → 빈도 내림차순(동률 값 오름차순) 정렬 → 앞 k.
- 함정: 동률 tie-break 를 정해 **결정적**으로. 대규모면 정렬 대신 힙/버킷정렬로 O(n).

---

## 인터뷰 팔로업 (자주 나오는 꼬리질문)

1. **"해시 충돌은 어떻게?"** → 개방주소법(선형/이차 탐사) vs 체이닝. 부하율·rehash·tombstone.
2. **"해시 함수 뭘 쓰나?"** → 정수는 곱셈 해시(Knuth)·FNV. cap=2^k 로 `&` 마스킹.
3. **"정렬 vs 해시?"** → 해시 O(n) 평균이나 상수·메모리 큼. 정렬 O(n log n) 이나 제자리·캐시친화.
4. **"prefix sum 을 2D 로?"** → 적분 이미지(integral image), 구간합 O(1).
5. **"sliding window 가변 크기?"** → 조건 만족까지 우측 확장, 위반 시 좌측 축소(투 포인터).
6. **"Boyer-Moore 가 왜 맞나?"** → 다수 원소는 상쇄 후에도 살아남음(등장>n/2 라서).
7. **"top-k 를 O(n) 으로?"** → 최소힙 크기 k(O(n log k)) 또는 버킷정렬(빈도 인덱스, O(n)).

---

## 빌드 & 실행

```sh
# 연습(직접 채우기)
cc -std=c11 -Wall -Wextra problems/11_arrays_hashing.c -o /tmp/andb_11 && /tmp/andb_11
# 정답 확인 (전부 PASS)
cc -std=c11 -Wall -Wextra solutions/11_arrays_hashing.c -o /tmp/andb_11 && /tmp/andb_11
# 또는
make prob N=11_arrays_hashing      #  연습
make sol  N=11_arrays_hashing      #  정답
```

> **핵심 한 줄**: "배열은 한 번만 훑어라." 해시로 짝을 O(1) 에 찾고, prefix 로 구간을
> 미리 접고, window/two-pointer 로 상태를 굴려 O(n²) 를 O(n) 으로 내린다 — STL 없이.
