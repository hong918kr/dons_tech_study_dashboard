# Ch.22 물리 메모리 너머: 정책 — 어떤 페이지를 내쫓을 것인가

> 📖 원문: [22. Beyond Physical Memory: Policies](../book-md/C22_beyond_physical_memory_policies.md) · [PDF p.250](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=250) · ⏱️ 읽기 약 50분 · 🔗 선행: [Ch.21 스와핑 메커니즘](2026-09-30_C21_swapping_mechanisms.md), [Ch.19 TLB](2026-09-30_C19_tlb.md)

## 0. 한눈에 보기

> **THE CRUX: How can the OS decide which page (or pages) to evict from memory?**
> (OS는 메모리에서 어떤 페이지를 내쫓을지 어떻게 정하는가?)

- 메모리는 "가상 페이지들의 캐시"다. 목표는 miss(= 디스크 접근)를 최소화하는 것. 디스크가 메모리보다 10만 배 느리므로 **miss율 0.1%만 늘어도 성능이 몇 배 떨어진다**.
- 이론상 최적은 **OPT(Belady MIN)**: "가장 먼 미래에 쓰일 페이지를 버린다". 미래를 모르니 비교 기준으로만 쓴다.
- 현실 정책: **FIFO / Random**(단순, 역사 무시) → **LRU**(과거로 미래를 추정, 지역성 활용) → LRU는 정확히 구현하기 비싸서 **use bit + Clock 알고리즘**으로 근사. 쫓아낼 때 **dirty page는 write-back 비용**이 드니 clean 페이지를 우선.
- FIFO는 **Belady's anomaly**(캐시를 늘렸는데 hit가 줄어듦)가 있고, LRU는 **stack property** 덕분에 없다. LRU/FIFO는 **looping-sequential** 워크로드에서 0%로 무너진다.
- 메모리가 아예 모자라면 **thrashing**: admission control로 일부만 돌리거나, Linux처럼 **OOM killer**로 하나를 죽인다. 결론: "메모리를 더 사라".

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 교체 정책(replacement policy) | 메모리가 꽉 찼을 때 어떤 페이지를 내보낼지 정하는 규칙 | 냉장고가 꽉 찼을 때 뭘 버릴지 |
| AMAT | 평균 메모리 접근 시간 = P_hit·T_M + P_miss·T_D | 1% miss면 거의 디스크 속도 |
| compulsory miss | 처음 접근이라 생기는 miss (cold-start) | 첫 출근날엔 책상이 비어 있음 |
| capacity miss | 공간이 모자라 내보낸 걸 다시 찾을 때 miss | 가방이 작아서 놓고 온 책 |
| conflict miss | HW 캐시의 set-associativity 제약 때문 — OS 페이지 캐시엔 없음 | OS는 fully-associative |
| OPT / MIN | 가장 먼 미래에 다시 쓰일 페이지를 버림 (Belady) | 시험 일정표를 보고 책 정리 |
| FIFO | 가장 먼저 들어온 페이지를 버림 | 오래된 우유부터 버리기 |
| Random | 아무거나 버림 | 제비뽑기 |
| LRU / LFU | 가장 오래 전에 / 가장 적게 쓰인 페이지를 버림 | 최근 안 본 책부터 정리 |
| Belady's anomaly | 캐시를 키웠는데 hit가 줄어드는 현상 (FIFO) | 1,2,3,4,1,2,5,1,2,3,4,5 |
| stack property | 크기 N+1 캐시 내용 ⊇ 크기 N 캐시 내용 (LRU, OPT) | 큰 캐시가 항상 같거나 좋음 |
| use(reference) bit | 페이지 접근 시 HW가 1로 세팅, 지우는 건 OS | ARM의 Access Flag, x86의 A bit |
| Clock 알고리즘 | 원형 리스트를 바늘로 돌며 use bit 0인 페이지를 찾음 | 2번째 기회(second chance) |
| dirty(modified) bit | 페이지가 쓰였는지 — 쓰였으면 내보낼 때 디스크에 써야 함 | 저장 안 한 문서 |
| demand paging / prefetching | 필요할 때 가져옴 / 미리 가져옴 | 주문 생산 / 예측 재고 |
| clustering(grouping) of writes | 여러 dirty 페이지를 모아 한 번에 씀 | 택배 묶음 배송 |
| thrashing | 작업 집합이 메모리보다 커서 계속 페이징만 하는 상태 | 책상보다 책이 많아 정리만 하다 하루 끝 |
| working set | 프로세스가 최근 활발히 쓰는 페이지 집합 (Denning) | 지금 펼쳐 둔 책들 |
| admission control / OOM killer | 일부 프로세스만 돌리기 / 하나를 죽이기 | 입장 제한 / 강제 퇴장 |

## 2. 캐시 관리 관점으로 보기 (22.1)

메인 메모리는 시스템 전체 페이지 중 일부만 담는다. 그러니 메모리는 **가상 메모리 페이지의 캐시**다. 교체 정책의 목표는 단 하나 — **cache miss 수를 최소화**(= hit 수 최대화)하는 것.

성능 지표는 컴퓨터 구조에서 쓰는 AMAT(Average Memory Access Time)다.

```text
AMAT = (P_hit × T_M) + (P_miss × T_D)
  T_M : 메모리 접근 비용,  T_D : 디스크 접근 비용
  P_hit + P_miss = 1
```

### 원문 예제 계산

4KB 주소 공간, 256B 페이지 → VPN 4비트 + offset 8비트, 페이지 16개. 접근 0x000, 0x100, …, 0x900 (페이지 0~9의 첫 바이트). 페이지 3만 메모리에 없다고 하자.

```text
hit hit hit MISS hit hit hit hit hit hit  → P_hit = 9/10 = 0.9, P_miss = 0.1

T_M = 100 ns, T_D = 10 ms
AMAT = 0.9 × 100 ns + 0.1 × 10 ms
     = 90 ns + 1,000,000 ns
     = 1,000,090 ns ≈ 1.00009 ms     ← 사실상 "디스크 속도"

hit rate 99.9% 라면:
AMAT = 0.999 × 100 ns + 0.001 × 10 ms
     = 99.9 ns + 10,000 ns ≈ 10.1 µs  ← 약 100배 빨라짐
```

핵심 교훈: **디스크가 메모리보다 10^5 배 느리면, 아주 작은 miss율이 AMAT를 지배한다.**

### 새 예제: 요즘의 NVMe SSD 스왑이라면?

T_D를 10 ms(HDD) 대신 NVMe SSD의 4KB 랜덤 읽기 지연 약 100 µs로 잡아 보자 (T_M = 100 ns 그대로).

```text
hit rate 99%   : 0.99 × 100 ns + 0.01 × 100,000 ns  =  99 ns + 1,000 ns = 1,099 ns  (≈ 11배 느림)
hit rate 99.9% : 0.999 × 100 ns + 0.001 × 100,000 ns = 99.9 ns + 100 ns  ≈   200 ns  (≈ 2배 느림)
hit rate 99.99%: 99.99 ns + 10 ns ≈ 110 ns
```

SSD로 바뀌어도 비율이 1000배라 결론은 똑같다. **miss율을 소수점 아래 자리까지 깎아야 의미가 있다.** (Don이 SSD FW에서 매핑 테이블 캐시 hit율을 99.x%에서 소수점 하나 더 올리려고 애쓴 것과 같은 계산이다.)

## 3. 최적 정책 OPT (22.2)

Belady(1966)의 MIN: **"앞으로 가장 늦게 쓰일 페이지를 버린다."** 이 정책이 miss 수를 최소화한다는 것이 증명되어 있다.

직관: 어차피 하나는 버려야 한다면, 다른 페이지들은 모두 그 페이지보다 먼저 다시 쓰인다. 그러니 가장 먼 미래의 페이지를 버리는 게 손해가 가장 적다.

> **TIP — 최적과 비교하는 습관을 들여라 (Comparing Against Optimal Is Useful)**
> "내 알고리즘 hit rate 80%"는 혼자서는 아무 의미가 없다. "OPT가 82%인데 내 건 80%"라고 해야 얼마나 개선 여지가 남았는지, 언제 그만 다듬어도 되는지 알 수 있다.

### 원문 트레이스 (캐시 3칸): 0, 1, 2, 0, 1, 3, 0, 3, 1, 2, 1

```text
접근  결과   쫓아냄   캐시 상태      판단 근거
 0    Miss           0             cold
 1    Miss           0,1           cold
 2    Miss           0,1,2         cold
 0    Hit            0,1,2
 1    Hit            0,1,2
 3    Miss   2       0,1,3         미래: 0은 곧(+1), 1은 +3, 2는 +4 → 2를 버림
 0    Hit            0,1,3
 3    Hit            0,1,3
 1    Hit            0,1,3
 2    Miss   3       0,1,2         미래: 1만 다시 쓰임. 0,3은 다시 안 쓰임 → 둘 중 아무거나
 1    Hit            0,1,2
```

- hit rate = 6 / (6+5) = **54.5%**
- compulsory miss(첫 접근 3개 + 페이지 3 첫 접근 + …)를 빼면? 서로 다른 페이지가 4개(0,1,2,3)이므로 compulsory miss 4개. 남은 7번 접근 중 6번 hit → **6/7 = 85.7%**.

> **ASIDE — 캐시 miss의 3C**: compulsory(처음이라), capacity(공간 부족), conflict(set-associativity 제약). OS 페이지 캐시는 어떤 페이지든 어떤 프레임에나 놓을 수 있는 **fully-associative**라 conflict miss가 없다.

미래를 아는 OS는 없으니(책의 각주: "알면 연락 줘라, 같이 부자 되자") OPT는 **비교 기준점**으로만 쓴다.

## 4. 단순 정책: FIFO (22.3)

들어온 순서대로 큐에 넣고, 가장 먼저 들어온 페이지를 내보낸다. 구현이 아주 쉽다(큐 하나).

```text
접근  결과   쫓아냄   캐시 (왼쪽 = First-in)
 0    Miss           0
 1    Miss           0,1
 2    Miss           0,1,2
 0    Hit            0,1,2
 1    Hit            0,1,2
 3    Miss   0       1,2,3      ← 0을 두 번이나 썼는데도 "먼저 왔다"는 이유로 퇴출
 0    Miss   1       2,3,0
 3    Hit            2,3,0
 1    Miss   2       3,0,1
 2    Miss   3       0,1,2
 1    Hit            0,1,2
```

hit 4 / 11 = **36.4%** (compulsory 제외 4/7 = 57.1%). FIFO는 블록의 **중요도를 전혀 판단하지 못한다.**

### Belady's anomaly (ASIDE)

참조열 `1,2,3,4,1,2,5,1,2,3,4,5` 를 FIFO로 돌려 보자. 캐시를 3 → 4로 키우면 hit가 **늘어야** 정상인데…

```text
캐시 3칸 (FIFO)                         캐시 4칸 (FIFO)
1 M [1]                                 1 M [1]
2 M [1,2]                               2 M [1,2]
3 M [1,2,3]                             3 M [1,2,3]
4 M [2,3,4]    evict 1                  4 M [1,2,3,4]
1 M [3,4,1]    evict 2                  1 H [1,2,3,4]
2 M [4,1,2]    evict 3                  2 H [1,2,3,4]
5 M [1,2,5]    evict 4                  5 M [2,3,4,5]  evict 1
1 H                                     1 M [3,4,5,1]  evict 2
2 H                                     2 M [4,5,1,2]  evict 3
3 M [2,5,3]    evict 1                  3 M [5,1,2,3]  evict 4
4 M [5,3,4]    evict 2                  4 M [1,2,3,4]  evict 5
5 H                                     5 M [2,3,4,5]  evict 1
hits = 3 (25.0%)                        hits = 2 (16.7%)   ← 더 나빠짐!
```

왜? **LRU는 stack property**가 있다: 크기 N+1 캐시의 내용은 항상 크기 N 캐시의 내용을 포함한다(최근 N개 ⊂ 최근 N+1개). 그래서 캐시를 키우면 hit는 같거나 늘 뿐이다. FIFO와 Random에는 이 성질이 없다. 위 표의 5번째 접근 직후를 보면, 3칸 캐시는 {3,4,1} 인데 4칸 캐시는 {1,2,3,4}이고, 7번째(5 접근) 이후 3칸은 {1,2,5}, 4칸은 {2,3,4,5} — **4칸 캐시에 1이 없다**. 포함 관계가 깨진 순간 anomaly가 생긴다.

## 5. 또 다른 단순 정책: Random (22.4)

메모리 압박 시 아무 페이지나 골라 버린다. FIFO처럼 구현이 쉽고 똑똑하지 않다. 성능은 **운**이다. 책의 실험(Figure 22.4): 예제 트레이스를 서로 다른 시드로 10,000번 돌리면, 40% 남짓은 OPT와 같은 6 hit, 가끔은 2 hit 이하.

원문 Figure 22.3의 한 번의 Random 실행은 5 hit(45.5%)로 FIFO보다 조금 낫고 OPT보다 조금 못했다.

Random의 숨은 장점: **이상한 corner case가 없다.** 아래 looping 워크로드에서 다시 본다.

## 6. 과거를 이용하기: LRU (22.5)

FIFO·Random은 곧 다시 쓰일 중요한 페이지를 쫓아낼 수 있다. 스케줄링(MLFQ)에서 그랬듯, **과거를 보고 미래를 추정**하자.

- **빈도(frequency)**: 여러 번 쓰인 페이지는 가치가 있다 → **LFU**
- **최근성(recency)**: 최근에 쓰인 페이지는 곧 또 쓰인다 → **LRU** (더 흔히 쓰임)

이 계열의 근거가 **지역성의 원리(principle of locality)**다.

> **ASIDE — 지역성의 두 종류**
> - **공간 지역성(spatial)**: 페이지 P를 쓰면 P-1, P+1도 곧 쓴다 (배열 순회).
> - **시간 지역성(temporal)**: 최근에 쓴 페이지를 곧 또 쓴다 (루프 변수, 코드).
> 지역성은 "법칙"이 아니라 **자주 맞는 휴리스틱**이다. 랜덤하게 메모리를 긁는 프로그램도 있다.

### 원문 트레이스 (LRU, 캐시 3칸)

```text
접근  결과   쫓아냄   LRU → MRU 순서
 0    Miss           0
 1    Miss           0,1
 2    Miss           0,1,2
 0    Hit            1,2,0        ← 0이 MRU 쪽으로 이동
 1    Hit            2,0,1
 3    Miss   2       0,1,3        ← LRU 끝에 있던 2를 버림 (OPT와 같은 선택)
 0    Hit            1,3,0
 3    Hit            1,0,3
 1    Hit            0,3,1
 2    Miss   0       3,1,2        ← LRU인 0을 버림
 1    Hit            3,2,1
```

hit 6 / 11 = **54.5% — OPT와 동일.** (책의 각주: "결과를 좀 요리했다(cooked)".) 반대 개념인 MFU, MRU도 있지만 지역성을 거스르므로 대부분 나쁘다.

## 7. 워크로드로 비교하기 (22.6)

책의 세 실험을 내 C 시뮬레이터(아래 "직접 해보기")로 그대로 재현했다. 고유 페이지 100개, 총 10,000번 접근, 캐시 크기 1~100.

1. **No-locality**: 매번 100개 중 무작위. → LRU·FIFO·Random·Clock 모두 hit rate ≈ 캐시 크기/100 (예: 캐시 20 → 약 20%). **지역성이 없으면 현실 정책 간 차이가 없다.** 캐시가 전부를 담으면(100) 모두 수렴. OPT만 확연히 높다(캐시 20에서 51.8%).
2. **80-20**: 접근의 80%가 hot 20페이지, 20%가 나머지 80페이지. → LRU가 hot 페이지를 잘 붙잡아 FIFO·Random보다 높다.
3. **Looping-sequential**: 0,1,…,49,0,1,…를 반복. → **LRU와 FIFO는 캐시가 49칸이어도 0%.** 다음에 필요한 페이지가 하필 "가장 오래된" 페이지라서 매번 그것을 버린다. Random은 0이 아니다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
<text x="340" y="18" text-anchor="middle" fill="currentColor" font-weight="bold">80-20 워크로드: 캐시 크기별 hit rate (내 C 시뮬레이터 결과)</text>
<line x1="60" y1="250" x2="620" y2="250" stroke="currentColor"/>
<line x1="60" y1="250" x2="60" y2="30" stroke="currentColor"/>
<line x1="60" y1="250" x2="620" y2="250" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="254" text-anchor="end" fill="currentColor" font-size="11">0%</text>
<line x1="60" y1="195" x2="620" y2="195" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="199" text-anchor="end" fill="currentColor" font-size="11">25%</text>
<line x1="60" y1="140" x2="620" y2="140" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="144" text-anchor="end" fill="currentColor" font-size="11">50%</text>
<line x1="60" y1="85" x2="620" y2="85" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="89" text-anchor="end" fill="currentColor" font-size="11">75%</text>
<line x1="60" y1="30" x2="620" y2="30" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="34" text-anchor="end" fill="currentColor" font-size="11">100%</text>
<text x="60" y="266" text-anchor="middle" fill="currentColor" font-size="11">0</text>
<text x="172" y="266" text-anchor="middle" fill="currentColor" font-size="11">20</text>
<text x="284" y="266" text-anchor="middle" fill="currentColor" font-size="11">40</text>
<text x="396" y="266" text-anchor="middle" fill="currentColor" font-size="11">60</text>
<text x="508" y="266" text-anchor="middle" fill="currentColor" font-size="11">80</text>
<text x="620" y="266" text-anchor="middle" fill="currentColor" font-size="11">100</text>
<text x="340" y="284" text-anchor="middle" fill="currentColor" font-size="12">캐시 크기 (페이지)</text>
<polyline fill="none" stroke="currentColor" stroke-width="2.5" points=""/>
<line x1="540" y1="102" x2="570" y2="102" stroke="currentColor" stroke-width="2.5"/>
<text x="576" y="106" fill="currentColor" font-size="12">OPT</text>
<polyline fill="none" style="stroke:var(--accent)" stroke-width="2.5" points=""/>
<line x1="540" y1="122" x2="570" y2="122" style="stroke:var(--accent)" stroke-width="2.5"/>
<text x="576" y="126" fill="currentColor" font-size="12">LRU</text>
<polyline fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 4" points=""/>
<line x1="540" y1="142" x2="570" y2="142" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 4"/>
<text x="576" y="146" fill="currentColor" font-size="12">FIFO</text>
<polyline fill="none" style="stroke:var(--accent)" stroke-width="1.5" stroke-dasharray="2 3" points=""/>
<line x1="540" y1="162" x2="570" y2="162" style="stroke:var(--accent)" stroke-width="1.5" stroke-dasharray="2 3"/>
<text x="576" y="166" fill="currentColor" font-size="12">RAND</text>
</svg>
```

캐시 30칸에서 LRU 76.0% vs FIFO 66.3%. miss율로 보면 24% vs 33.7%, 즉 **miss가 약 30% 줄었다**. miss 한 번이 10 ms라면 아주 큰 차이다. 반대로 miss가 싸면(예: 압축 메모리에서 꺼내기) LRU의 이점은 작아진다 — 책의 답 "it depends".

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
<text x="340" y="18" text-anchor="middle" fill="currentColor" font-weight="bold">Looping-sequential(50페이지) 워크로드: LRU·FIFO는 49칸까지 0%</text>
<line x1="60" y1="250" x2="620" y2="250" stroke="currentColor"/>
<line x1="60" y1="250" x2="60" y2="30" stroke="currentColor"/>
<line x1="60" y1="250" x2="620" y2="250" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="254" text-anchor="end" fill="currentColor" font-size="11">0%</text>
<line x1="60" y1="195" x2="620" y2="195" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="199" text-anchor="end" fill="currentColor" font-size="11">25%</text>
<line x1="60" y1="140" x2="620" y2="140" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="144" text-anchor="end" fill="currentColor" font-size="11">50%</text>
<line x1="60" y1="85" x2="620" y2="85" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="89" text-anchor="end" fill="currentColor" font-size="11">75%</text>
<line x1="60" y1="30" x2="620" y2="30" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="34" text-anchor="end" fill="currentColor" font-size="11">100%</text>
<text x="60" y="266" text-anchor="middle" fill="currentColor" font-size="11">0</text>
<text x="172" y="266" text-anchor="middle" fill="currentColor" font-size="11">20</text>
<text x="284" y="266" text-anchor="middle" fill="currentColor" font-size="11">40</text>
<text x="396" y="266" text-anchor="middle" fill="currentColor" font-size="11">60</text>
<text x="508" y="266" text-anchor="middle" fill="currentColor" font-size="11">80</text>
<text x="620" y="266" text-anchor="middle" fill="currentColor" font-size="11">100</text>
<text x="340" y="284" text-anchor="middle" fill="currentColor" font-size="12">캐시 크기 (페이지)</text>
<polyline fill="none" stroke="currentColor" stroke-width="2.5" points=""/>
<line x1="540" y1="102" x2="570" y2="102" stroke="currentColor" stroke-width="2.5"/>
<text x="576" y="106" fill="currentColor" font-size="12">OPT</text>
<polyline fill="none" style="stroke:var(--accent)" stroke-width="2.5" points=""/>
<line x1="540" y1="122" x2="570" y2="122" style="stroke:var(--accent)" stroke-width="2.5"/>
<text x="576" y="126" fill="currentColor" font-size="12">LRU</text>
<polyline fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 4" points=""/>
<line x1="540" y1="142" x2="570" y2="142" stroke="currentColor" stroke-width="1.5" stroke-dasharray="6 4"/>
<text x="576" y="146" fill="currentColor" font-size="12">FIFO</text>
<polyline fill="none" style="stroke:var(--accent)" stroke-width="1.5" stroke-dasharray="2 3" points=""/>
<line x1="540" y1="162" x2="570" y2="162" style="stroke:var(--accent)" stroke-width="1.5" stroke-dasharray="2 3"/>
<text x="576" y="166" fill="currentColor" font-size="12">RAND</text>
</svg>
```

이 looping 패턴은 DB의 순차 스캔처럼 실제로 흔하다. 그래서 현대 알고리즘(ARC, Linux의 active/inactive 리스트 등)은 **scan resistance**를 갖추려고 한다.

## 8. 과거 기반 알고리즘 구현하기 (22.7)

LRU를 **정확히** 구현하려면 **매 메모리 접근마다**(명령어 fetch, load, store 전부!) 그 페이지를 리스트의 MRU 끝으로 옮겨야 한다. FIFO는 페이지가 들어오고 나갈 때만 리스트를 건드리는 것과 대조된다.

HW 도움으로 "페이지마다 마지막 접근 시각 필드"를 HW가 갱신한다고 해도, 교체 시점에 전부 스캔해야 한다.

```text
4 GB 메모리 / 4 KB 페이지 = 2^32 / 2^12 = 2^20 ≈ 100만 페이지
→ 교체할 때마다 타임스탬프 100만 개를 스캔? 너무 비싸다.
```

> **CRUX: How to implement an LRU replacement policy** — 완벽한 LRU는 비싸다. 근사해도 원하는 효과를 얻을 수 있을까?

## 9. LRU 근사: use bit와 Clock 알고리즘 (22.8)

HW 지원 하나: **use bit(reference bit)**. 페이지가 읽히거나 쓰이면 **HW가 1로 세팅**한다. HW는 절대 0으로 지우지 않는다 — 지우는 것은 OS 몫. (최초는 Atlas one-level store, 1962.)

**Clock 알고리즘**(Corbató, 1969): 모든 물리 페이지를 원형 리스트로 놓고 바늘(hand)이 한 페이지를 가리킨다. 교체가 필요하면:

1. 바늘이 가리키는 페이지 P의 use bit가 **1**이면 → "최근에 쓰였구나", **0으로 지우고** 바늘을 다음으로.
2. use bit가 **0**이면 → 이 페이지가 victim.
3. 최악의 경우 한 바퀴 돌며 전부 0으로 지운 뒤 처음 페이지를 고른다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
<defs><marker id="C22-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="190" y="20" text-anchor="middle" font-weight="bold" fill="currentColor">Clock: 물리 페이지를 원형으로, 바늘이 돈다</text>
<circle cx="190" cy="165" r="105" fill="none" stroke="currentColor" stroke-dasharray="4 4" stroke-opacity="0.5"/>
<rect x="166" y="43" width="48" height="34" rx="6" fill="none" stroke="currentColor"/>
<text x="190" y="58" text-anchor="middle" fill="currentColor" font-weight="bold">A</text>
<text x="190" y="72" text-anchor="middle" fill="currentColor" font-size="11">use=1</text>
<rect x="240" y="74" width="48" height="34" rx="6" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
<text x="264" y="89" text-anchor="middle" fill="currentColor" font-weight="bold">B</text>
<text x="264" y="103" text-anchor="middle" fill="currentColor" font-size="11">use=0</text>
<rect x="271" y="148" width="48" height="34" rx="6" fill="none" stroke="currentColor"/>
<text x="295" y="163" text-anchor="middle" fill="currentColor" font-weight="bold">C</text>
<text x="295" y="177" text-anchor="middle" fill="currentColor" font-size="11">use=1</text>
<rect x="240" y="222" width="48" height="34" rx="6" fill="none" stroke="currentColor"/>
<text x="264" y="237" text-anchor="middle" fill="currentColor" font-weight="bold">D</text>
<text x="264" y="251" text-anchor="middle" fill="currentColor" font-size="11">use=1</text>
<rect x="166" y="253" width="48" height="34" rx="6" fill="none" stroke="currentColor"/>
<text x="190" y="268" text-anchor="middle" fill="currentColor" font-weight="bold">E</text>
<text x="190" y="282" text-anchor="middle" fill="currentColor" font-size="11">use=0</text>
<rect x="92" y="222" width="48" height="34" rx="6" fill="none" stroke="currentColor"/>
<text x="116" y="237" text-anchor="middle" fill="currentColor" font-weight="bold">F</text>
<text x="116" y="251" text-anchor="middle" fill="currentColor" font-size="11">use=1</text>
<rect x="61" y="148" width="48" height="34" rx="6" fill="none" stroke="currentColor"/>
<text x="85" y="163" text-anchor="middle" fill="currentColor" font-weight="bold">G</text>
<text x="85" y="177" text-anchor="middle" fill="currentColor" font-size="11">use=1</text>
<rect x="92" y="74" width="48" height="34" rx="6" fill="none" stroke="currentColor"/>
<text x="116" y="89" text-anchor="middle" fill="currentColor" font-weight="bold">H</text>
<text x="116" y="103" text-anchor="middle" fill="currentColor" font-size="11">use=0</text>
<line x1="190" y1="165" x2="190" y2="90" stroke="currentColor" stroke-width="3" marker-end="url(#C22-arrow)"/>
<circle cx="190" cy="165" r="5" fill="currentColor"/>
<text x="190" y="189" text-anchor="middle" fill="currentColor" font-size="12">hand</text>
<path d="M 224 40 A 128 128 0 0 1 282 70" fill="none" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C22-arrow)"/>
<text x="360" y="70" fill="currentColor" font-weight="bold">교체가 필요할 때:</text>
<text x="360" y="89" fill="currentColor">① 바늘이 A를 가리킴: use=1</text>
<text x="360" y="108" fill="currentColor">   → 최근에 쓰였음: use=0으로 지우고 다음 칸</text>
<text x="360" y="127" fill="currentColor">② B: use=0 → B가 victim!</text>
<text x="360" y="146" fill="currentColor">    → 새 페이지를 B 자리에 넣고 use=1</text>
<text x="360" y="165" fill="currentColor">    → 바늘은 C로 이동</text>
<text x="360" y="203" fill="currentColor" font-weight="bold">평소 (교체 없을 때):</text>
<text x="360" y="222" fill="currentColor">• 페이지를 읽거나 쓰면 HW가 use=1</text>
<text x="360" y="241" fill="currentColor">• HW는 절대 0으로 지우지 않음 (OS 몫)</text>
<text x="360" y="279" fill="currentColor">모든 use=1이면 한 바퀴 돌며 다 지운 뒤</text>
<text x="360" y="298" fill="currentColor">출발점을 버림 → FIFO와 같아짐</text>
</svg>
```

Clock은 "주기적으로 use bit를 지우고, 0인 것 중에서 고른다"는 아이디어의 한 구현일 뿐이다. 장점은 **메모리 전체를 반복 스캔하지 않는다**는 것. 원문 Figure 22.9는 무작위로 스캔하는 Clock 변형이 80-20에서 LRU에는 약간 못 미치지만 이력 무시 정책들보다 낫다는 것을 보여 준다. 내 시뮬레이터 결과도 같다: 캐시 30에서 Clock 72.5%, LRU 76.0%, FIFO 66.3%.

### Clock 손 계산 (원문 트레이스, 캐시 3칸, 바늘은 프레임 0에서 시작)

```text
t=5 접근 3: 프레임 [0* 1* 2*] 모두 use=1
   바늘 0: 0*→0 지움 → 1: 1*→1 지움 → 2: 2*→2 지움 → 0: use=0 → 페이지 0 퇴출
   결과 [3* 1 2], 바늘→1
t=6 접근 0: 바늘 1: use=0 → 페이지 1 퇴출 → [3* 0* 2], 바늘→2
...
```

모든 use bit가 1이면 Clock은 **FIFO로 퇴화**한다. 그래서 이 짧은 예제에서는 FIFO와 똑같이 4 hit가 나온다(아래 실제 출력 참고). Clock의 이점은 use bit가 "섞여" 있을 때, 즉 큰 메모리에서 일부 페이지만 hot일 때 드러난다.

## 10. Dirty page 고려 (22.9)

페이지가 **수정(dirty)**됐으면 내보낼 때 디스크에 다시 써야 한다(비쌈). **clean**이면 그냥 프레임을 재사용하면 된다(공짜). 그래서 HW는 **modified(dirty) bit**를 두고, Clock은 이렇게 확장된다:

```text
우선순위  (use, dirty)   의미                      비용
  1       (0, 0)        최근 안 씀 + 깨끗함        퇴출 공짜 ← 최우선
  2       (0, 1)        최근 안 씀 + 더러움        write-back 필요
  3       (1, 0)        최근 씀 + 깨끗함           곧 다시 쓸 듯
  4       (1, 1)        최근 씀 + 더러움           최후의 선택
```

(이것이 흔히 "Enhanced second-chance" 또는 NRU 클래스라 불리는 방식이다.)

## 11. 다른 VM 정책들 (22.10)

- **Page selection policy** (언제 가져올까, Denning): 대부분 **demand paging**(접근할 때 가져옴). 가능성 높을 때만 **prefetching** — 예: 코드 페이지 P를 가져오면 P+1도 같이.
- **쓰기 정책**: 한 페이지씩 쓰지 않고 dirty 페이지를 모아서 한 번에 큰 write — **clustering / grouping of writes**. 디스크는 큰 순차 쓰기가 훨씬 효율적이기 때문.

## 12. Thrashing (22.11)

실행 중인 프로세스들의 메모리 수요 총합이 물리 메모리를 넘으면, 시스템은 **계속 페이징만** 한다 → thrashing.

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
<defs><marker id="C22-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" text-anchor="middle" font-weight="bold" fill="currentColor">Thrashing: 동시에 돌리는 프로세스 수 vs 실제 처리량 (개념도)</text>
<rect x="380" y="40" width="250" height="190" style="fill:var(--accent-soft)"/>
<line x1="70" y1="230" x2="640" y2="230" stroke="currentColor" marker-end="url(#C22-arrow2)"/>
<line x1="70" y1="230" x2="70" y2="40" stroke="currentColor" marker-end="url(#C22-arrow2)"/>
<text x="355" y="262" text-anchor="middle" fill="currentColor" font-size="12">동시 실행 프로세스 수 (degree of multiprogramming)</text>
<text x="30" y="135" text-anchor="middle" fill="currentColor" font-size="12" transform="rotate(-90 30 135)">처리량 / CPU 활용</text>
<text x="505" y="62" text-anchor="middle" fill="currentColor" font-size="12">Σ working set &gt; 물리 메모리</text>
<path d="M 75 220 C 160 120, 260 70, 360 62 C 400 60, 420 90, 450 150 C 480 205, 540 222, 630 225" fill="none" style="stroke:var(--accent)" stroke-width="3"/>
<line x1="380" y1="40" x2="380" y2="230" stroke="currentColor" stroke-dasharray="5 4"/>
<text x="372" y="245" text-anchor="end" fill="currentColor" font-size="11">메모리 한계</text>
<text x="250" y="160" text-anchor="middle" fill="currentColor" font-size="12">프로세스↑ → CPU 놀 틈↓</text>
<text x="540" y="150" text-anchor="middle" fill="#d9534f" font-size="13" font-weight="bold">thrashing</text>
<text x="540" y="168" text-anchor="middle" fill="currentColor" font-size="11">대부분 시간을 page fault 처리와</text>
<text x="540" y="183" text-anchor="middle" fill="currentColor" font-size="11">디스크 대기에 씀</text>
<text x="230" y="100" text-anchor="middle" fill="currentColor" font-size="11">admission control은 이 왼쪽 영역에 머물게 함</text>
</svg>
```

대응:

- **Admission control**: 일부 프로세스를 아예 돌리지 않아서 나머지의 **working set**이 메모리에 들어가게 한다. "모든 걸 엉망으로 하느니 적은 걸 제대로 하는 게 낫다."
- **OOM killer**(일부 Linux): 메모리를 많이 쓰는 프로세스 하나를 골라 죽인다. 효과는 확실하지만, X 서버를 죽이면 화면 쓰는 앱이 다 죽는 식의 부작용이 있다.

### 새 예제: working set 합으로 thrashing 판단

물리 메모리 8 GB, 각 프로세스 working set이 1.5 GB인 학습 데이터 로더 프로세스 6개.

```text
합계 = 6 × 1.5 = 9 GB > 8 GB   → 각자 일부 페이지가 계속 밀려남 → thrashing 위험
admission control로 5개만 실행: 5 × 1.5 = 7.5 GB ≤ 8 GB → 안정
처리량 비교(대략): 6개 thrashing (거의 0) vs 5개 정상 (5 단위)  → "덜 돌리는 게 더 빠르다"
```

## 13. 요약 (22.12)

현대 시스템은 Clock 같은 LRU 근사에 **scan resistance**(예: ARC) 같은 개선을 더한다. 하지만 메모리-디스크 속도 차가 너무 커서 페이징 자체가 감당이 안 되니, 과도한 페이징의 가장 좋은 해법은 지적으로는 불만족스럽지만 단순하다: **메모리를 더 사라.**

## 14. 직접 해보기

### 14.1 C로 FIFO / LRU / Clock / OPT / Random 구현

`code/C22_page_replacement.c` — 프레임 배열 하나로 다섯 정책을 구현했다.

- FIFO는 `loaded`(적재 시각) 최소, LRU는 `used`(마지막 접근 시각) 최소 프레임을 버린다. `used`를 **매 접근마다** 갱신하는 줄이 바로 "진짜 LRU의 비용"이다.
- Clock은 `ref`(use bit)와 `hand`로 구현. 접근 시 `ref = 1`은 HW가 하는 일을 흉내 낸 것.
- OPT는 트레이스 미래를 앞으로 스캔한다(시뮬레이션에서만 가능).
- 80-20 / looping 워크로드는 결정적 xorshift 난수로 만들었다.

```c
/*
 * C22_page_replacement.c
 * OSTEP Ch.22 — FIFO / LRU / Clock / OPT / Random 페이지 교체 정책 시뮬레이터
 *
 *  1) 책의 예제 트레이스(0,1,2,0,1,3,0,3,1,2,1)를 캐시 3칸으로 돌려 hit/miss 트레이스 출력
 *  2) Belady's anomaly 트레이스(1,2,3,4,1,2,5,1,2,3,4,5)를 캐시 3 vs 4로 비교
 *  3) 80-20 워크로드 / looping-sequential 워크로드에서 캐시 크기별 hit rate 표
 *
 * build: cc -Wall -Wextra -O0 code/C22_page_replacement.c -o .work/bin/C22_page_replacement
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MAXC 128   /* 최대 캐시 크기(프레임 수) */

enum policy { FIFO, LRU, CLOCK, OPT, RAND };
static const char *pname[] = { "FIFO", "LRU", "CLOCK", "OPT", "RAND" };

/* 프레임 하나 = 물리 페이지 하나 */
struct frame {
    int page;          /* 들어 있는 가상 페이지 번호, -1 = 비어 있음 */
    uint64_t loaded;   /* FIFO용: 들어온 시각 */
    uint64_t used;     /* LRU용: 마지막 접근 시각 (HW 타임스탬프 흉내) */
    int ref;           /* CLOCK용: use(reference) bit */
};

struct cache {
    struct frame f[MAXC];
    int size;
    int hand;          /* CLOCK 바늘 */
};

static void cache_init(struct cache *c, int size)
{
    c->size = size;
    c->hand = 0;
    for (int i = 0; i < size; i++) {
        c->f[i].page = -1;
        c->f[i].loaded = c->f[i].used = 0;
        c->f[i].ref = 0;
    }
}

static int lookup(const struct cache *c, int page)
{
    for (int i = 0; i < c->size; i++)
        if (c->f[i].page == page)
            return i;
    return -1;
}

/* OPT: 미래에서 가장 늦게(또는 다시는) 쓰일 페이지를 고른다.
 * 10,000개 트레이스에서도 빠르게 돌도록 앞으로 스캔하되 지금까지 본 최댓값보다
 * 멀어지면 바로 멈춘다 (실제 OS는 미래를 모르니 시뮬레이션 전용). */
static int pick_opt(const struct cache *c, const int *trace, int n, int now)
{
    int victim = 0, best = -1;
    for (int i = 0; i < c->size; i++) {
        int next = n + 1;                 /* 다시 안 쓰이면 무한대 취급 */
        for (int t = now + 1; t < n; t++)
            if (trace[t] == c->f[i].page) { next = t; break; }
        if (next > best) { best = next; victim = i; }
        if (best == n + 1) break;         /* 다시 안 쓰이는 페이지 발견 = 최선 */
    }
    return victim;
}

/* 결정적 난수 (xorshift32) */
static uint32_t rng = 2463534242u;
static uint32_t xr(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}
static uint32_t vrng = 12345u;   /* RAND 정책 전용 난수 상태 */

/* victim 프레임 인덱스 선택 (캐시가 꽉 찼을 때만 호출) */
static int pick_victim(struct cache *c, enum policy p,
                       const int *trace, int n, int now)
{
    int v = 0;
    switch (p) {
    case FIFO:
        for (int i = 1; i < c->size; i++)
            if (c->f[i].loaded < c->f[v].loaded) v = i;
        return v;
    case LRU:
        for (int i = 1; i < c->size; i++)
            if (c->f[i].used < c->f[v].used) v = i;
        return v;
    case CLOCK:
        for (;;) {                         /* use bit 1이면 0으로 지우고 넘어감 */
            struct frame *fr = &c->f[c->hand];
            if (fr->ref == 0) {
                v = c->hand;
                c->hand = (c->hand + 1) % c->size;
                return v;
            }
            fr->ref = 0;
            c->hand = (c->hand + 1) % c->size;
        }
    case OPT:
        return pick_opt(c, trace, n, now);
    case RAND:
        vrng ^= vrng << 13; vrng ^= vrng >> 17; vrng ^= vrng << 5;
        return (int)(vrng % (uint32_t)c->size);
    }
    return 0;
}

/* 트레이스 전체를 돌리고 hit 수만 리턴 (집계용) */
static int run(enum policy p, int size, const int *trace, int n)
{
    struct cache c;
    int hits = 0;
    cache_init(&c, size);
    for (int t = 0; t < n; t++) {
        int idx = lookup(&c, trace[t]);
        if (idx >= 0) {
            hits++;
        } else {
            idx = lookup(&c, -1);          /* 빈 프레임이 있으면 먼저 사용 */
            if (idx < 0)
                idx = pick_victim(&c, p, trace, n, t);
            c.f[idx].page = trace[t];
            c.f[idx].loaded = (uint64_t)t + 1;
        }
        c.f[idx].used = (uint64_t)t + 1;   /* 매 접근마다 갱신 = 진짜 LRU의 비용 */
        c.f[idx].ref = 1;                  /* HW가 세팅하는 use bit 흉내 */
    }
    return hits;
}

/* 한 줄씩 상태를 찍어 주는 버전 (작은 트레이스용) */
static int run_trace(enum policy p, int size, const int *trace, int n)
{
    struct cache c;
    int hits = 0;
    cache_init(&c, size);
    printf("[%s, cache=%d]\n", pname[p], size);
    for (int t = 0; t < n; t++) {
        int pg = trace[t];
        int idx = lookup(&c, pg);
        int hit = idx >= 0, evicted = -1;
        if (hit) {
            hits++;
        } else {
            idx = lookup(&c, -1);
            if (idx < 0) {
                idx = pick_victim(&c, p, trace, n, t);
                evicted = c.f[idx].page;
            }
            c.f[idx].page = pg;
            c.f[idx].loaded = (uint64_t)t + 1;
        }
        c.f[idx].used = (uint64_t)t + 1;
        c.f[idx].ref = 1;

        printf("  %2d: access %d  %-4s ", t, pg, hit ? "HIT" : "MISS");
        if (evicted >= 0) printf("evict %d ", evicted); else printf("        ");
        printf(" frames:");
        for (int i = 0; i < c.size; i++) {
            if (c.f[i].page < 0) printf(" .");
            else if (p == CLOCK) printf(" %d%s", c.f[i].page, c.f[i].ref ? "*" : "");
            else printf(" %d", c.f[i].page);
        }
        if (p == CLOCK) printf("   (hand->%d)", c.hand);
        printf("\n");
    }
    printf("  => hits %d / %d  (hit rate %.1f%%)\n\n", hits, n, 100.0 * hits / n);
    return hits;
}


#define NREF 10000
static int wl[NREF];

int main(void)
{
    /* 1) 책의 예제 트레이스 */
    int book[] = { 0, 1, 2, 0, 1, 3, 0, 3, 1, 2, 1 };
    int nb = (int)(sizeof book / sizeof book[0]);
    printf("=== 1. Book trace 0,1,2,0,1,3,0,3,1,2,1 (cache=3) ===\n");
    run_trace(OPT, 3, book, nb);
    run_trace(FIFO, 3, book, nb);
    run_trace(LRU, 3, book, nb);
    run_trace(CLOCK, 3, book, nb);

    /* 2) Belady's anomaly */
    int bel[] = { 1, 2, 3, 4, 1, 2, 5, 1, 2, 3, 4, 5 };
    int nbel = (int)(sizeof bel / sizeof bel[0]);
    printf("=== 2. Belady's anomaly trace 1,2,3,4,1,2,5,1,2,3,4,5 ===\n");
    for (int pol = FIFO; pol <= RAND; pol++) {
        printf("  %-5s", pname[pol]);
        for (int cs = 1; cs <= 5; cs++)
            printf("  C=%d:%2d hits", cs, run((enum policy)pol, cs, bel, nbel));
        printf("\n");
    }

    /* 3) 워크로드: 100개 고유 페이지, 10,000번 접근 */
    const char *wname[] = { "no-locality", "80-20", "looping-50" };
    for (int w = 0; w < 3; w++) {
        rng = 2463534242u;
        for (int i = 0; i < NREF; i++) {
            if (w == 0) {
                wl[i] = (int)(xr() % 100);
            } else if (w == 1) {               /* 80%는 hot 20페이지로 */
                if (xr() % 100 < 80) wl[i] = (int)(xr() % 20);
                else                 wl[i] = 20 + (int)(xr() % 80);
            } else {                           /* 0..49 반복 */
                wl[i] = i % 50;
            }
        }
        printf("\n=== 3.%d workload: %s (hit rate %%) ===\n", w + 1, wname[w]);
        printf("  cache   OPT   LRU  FIFO CLOCK  RAND\n");
        int sizes[] = { 1, 10, 20, 30, 40, 49, 50, 60, 80, 100 };
        for (int k = 0; k < 10; k++) {
            int cs = sizes[k];
            printf("  %5d", cs);
            int order[] = { OPT, LRU, FIFO, CLOCK, RAND };
            for (int j = 0; j < 5; j++)
                printf(" %5.1f", 100.0 * run((enum policy)order[j], cs, wl, NREF) / NREF);
            printf("\n");
        }
    }
    return 0;
}
```

컴파일·실행:

```text
cc -Wall -Wextra -O0 code/C22_page_replacement.c -o .work/bin/C22_page_replacement
.work/bin/C22_page_replacement
```

실제 출력 (경고 0개, Apple clang / arm64):

```text
=== 1. Book trace 0,1,2,0,1,3,0,3,1,2,1 (cache=3) ===
[OPT, cache=3]
   0: access 0  MISS          frames: 0 . .
   1: access 1  MISS          frames: 0 1 .
   2: access 2  MISS          frames: 0 1 2
   3: access 0  HIT           frames: 0 1 2
   4: access 1  HIT           frames: 0 1 2
   5: access 3  MISS evict 2  frames: 0 1 3
   6: access 0  HIT           frames: 0 1 3
   7: access 3  HIT           frames: 0 1 3
   8: access 1  HIT           frames: 0 1 3
   9: access 2  MISS evict 0  frames: 2 1 3
  10: access 1  HIT           frames: 2 1 3
  => hits 6 / 11  (hit rate 54.5%)

[FIFO, cache=3]
   0: access 0  MISS          frames: 0 . .
   1: access 1  MISS          frames: 0 1 .
   2: access 2  MISS          frames: 0 1 2
   3: access 0  HIT           frames: 0 1 2
   4: access 1  HIT           frames: 0 1 2
   5: access 3  MISS evict 0  frames: 3 1 2
   6: access 0  MISS evict 1  frames: 3 0 2
   7: access 3  HIT           frames: 3 0 2
   8: access 1  MISS evict 2  frames: 3 0 1
   9: access 2  MISS evict 3  frames: 2 0 1
  10: access 1  HIT           frames: 2 0 1
  => hits 4 / 11  (hit rate 36.4%)

[LRU, cache=3]
   0: access 0  MISS          frames: 0 . .
   1: access 1  MISS          frames: 0 1 .
   2: access 2  MISS          frames: 0 1 2
   3: access 0  HIT           frames: 0 1 2
   4: access 1  HIT           frames: 0 1 2
   5: access 3  MISS evict 2  frames: 0 1 3
   6: access 0  HIT           frames: 0 1 3
   7: access 3  HIT           frames: 0 1 3
   8: access 1  HIT           frames: 0 1 3
   9: access 2  MISS evict 0  frames: 2 1 3
  10: access 1  HIT           frames: 2 1 3
  => hits 6 / 11  (hit rate 54.5%)

[CLOCK, cache=3]
   0: access 0  MISS          frames: 0* . .   (hand->0)
   1: access 1  MISS          frames: 0* 1* .   (hand->0)
   2: access 2  MISS          frames: 0* 1* 2*   (hand->0)
   3: access 0  HIT           frames: 0* 1* 2*   (hand->0)
   4: access 1  HIT           frames: 0* 1* 2*   (hand->0)
   5: access 3  MISS evict 0  frames: 3* 1 2   (hand->1)
   6: access 0  MISS evict 1  frames: 3* 0* 2   (hand->2)
   7: access 3  HIT           frames: 3* 0* 2   (hand->2)
   8: access 1  MISS evict 2  frames: 3* 0* 1*   (hand->0)
   9: access 2  MISS evict 3  frames: 2* 0 1   (hand->1)
  10: access 1  HIT           frames: 2* 0 1*   (hand->1)
  => hits 4 / 11  (hit rate 36.4%)

=== 2. Belady's anomaly trace 1,2,3,4,1,2,5,1,2,3,4,5 ===
  FIFO   C=1: 0 hits  C=2: 0 hits  C=3: 3 hits  C=4: 2 hits  C=5: 7 hits
  LRU    C=1: 0 hits  C=2: 0 hits  C=3: 2 hits  C=4: 4 hits  C=5: 7 hits
  CLOCK  C=1: 0 hits  C=2: 0 hits  C=3: 3 hits  C=4: 2 hits  C=5: 7 hits
  OPT    C=1: 0 hits  C=2: 3 hits  C=3: 5 hits  C=4: 6 hits  C=5: 7 hits
  RAND   C=1: 0 hits  C=2: 0 hits  C=3: 3 hits  C=4: 6 hits  C=5: 7 hits

=== 3.1 workload: no-locality (hit rate %) ===
  cache   OPT   LRU  FIFO CLOCK  RAND
      1   1.0   1.0   1.0   1.0   1.0
     10  35.2  10.2  10.2  10.2  10.1
     20  51.8  20.1  20.4  20.2  19.9
     30  63.3  30.3  30.6  30.4  30.3
     40  72.1  40.6  40.6  40.5  40.4
     49  78.4  49.5  49.7  49.4  49.6
     50  79.0  50.4  50.5  50.4  50.2
     60  84.7  60.4  59.7  60.3  60.0
     80  93.4  79.7  79.5  79.3  79.8
    100  99.0  99.0  99.0  99.0  99.0

=== 3.2 workload: 80-20 (hit rate %) ===
  cache   OPT   LRU  FIFO CLOCK  RAND
      1   3.3   3.3   3.3   3.3   3.3
     10  60.0  30.9  29.7  30.2  29.9
     20  80.0  58.0  52.2  55.4  52.3
     30  87.4  76.0  66.3  72.5  66.1
     40  90.9  83.7  74.6  81.7  74.7
     49  93.1  86.6  79.9  86.0  80.7
     50  93.3  86.8  80.4  86.4  79.9
     60  95.1  89.2  85.6  89.4  85.4
     80  97.6  94.5  93.1  94.5  93.2
    100  99.0  99.0  99.0  99.0  99.0

=== 3.3 workload: looping-50 (hit rate %) ===
  cache   OPT   LRU  FIFO CLOCK  RAND
      1   0.0   0.0   0.0   0.0   0.0
     10  18.3   0.0   0.0   0.0   0.5
     20  38.6   0.0   0.0   0.0  10.4
     30  58.9   0.0   0.0   0.0  31.7
     40  79.2   0.0   0.0   0.0  61.6
     49  97.5   0.0   0.0   0.0  95.5
     50  99.5  99.5  99.5  99.5  99.5
     60  99.5  99.5  99.5  99.5  99.5
     80  99.5  99.5  99.5  99.5  99.5
    100  99.5  99.5  99.5  99.5  99.5
```

읽는 법:

- 1번: FIFO/LRU/OPT 결과가 원문 Figure 22.1, 22.2, 22.5와 hit 수까지 일치한다(FIFO 4, LRU 6, OPT 6). 내 출력은 **프레임 슬롯 순서**로 찍어서 원문(FIFO 큐 순서 / LRU→MRU 순서) 과 나열 순서만 다르다. OPT의 t=9에서 나는 0을, 원문은 3을 버렸는데 둘 다 다시 안 쓰이므로 둘 다 최적이다(원문도 "0 would have been a fine choice too").
- 2번: **FIFO는 C=3에서 3 hit, C=4에서 2 hit** — Belady's anomaly 재현. **CLOCK도 똑같이** 3→2로 떨어진다(모든 use bit가 1이 되어 FIFO처럼 동작하므로). LRU(2→4)와 OPT(5→6)는 단조 증가 — stack property.
- 3.3번: 캐시 49까지 LRU/FIFO/CLOCK은 0.0%, RAND는 40칸에서 61.6%, 49칸에서 95.5%. 50칸부터는 compulsory miss 50개만 남아 모두 99.5%.

### 14.2 OSTEP 시뮬레이터 `paging-policy.py`

```text
cd .tools/ostep-homework/vm-beyondphys-policy
python3 paging-policy.py -a 0,1,2,0,1,3,0,3,1,2,1 -p LRU -C 3 -c
```

```text
Access: 0  MISS LRU ->          [0] <- MRU Replaced:- [Hits:0 Misses:1]
Access: 1  MISS LRU ->       [0, 1] <- MRU Replaced:- [Hits:0 Misses:2]
Access: 2  MISS LRU ->    [0, 1, 2] <- MRU Replaced:- [Hits:0 Misses:3]
Access: 0  HIT  LRU ->    [1, 2, 0] <- MRU Replaced:- [Hits:1 Misses:3]
Access: 1  HIT  LRU ->    [2, 0, 1] <- MRU Replaced:- [Hits:2 Misses:3]
Access: 3  MISS LRU ->    [0, 1, 3] <- MRU Replaced:2 [Hits:2 Misses:4]
Access: 0  HIT  LRU ->    [1, 3, 0] <- MRU Replaced:- [Hits:3 Misses:4]
Access: 3  HIT  LRU ->    [1, 0, 3] <- MRU Replaced:- [Hits:4 Misses:4]
Access: 1  HIT  LRU ->    [0, 3, 1] <- MRU Replaced:- [Hits:5 Misses:4]
Access: 2  MISS LRU ->    [3, 1, 2] <- MRU Replaced:0 [Hits:5 Misses:5]
Access: 1  HIT  LRU ->    [3, 2, 1] <- MRU Replaced:- [Hits:6 Misses:5]

FINALSTATS hits 6   misses 5   hitrate 54.55
```

같은 명령에서 `-p OPT`는 `hits 6 misses 5 hitrate 54.55`, `-p FIFO`는 `hits 4 misses 7 hitrate 36.36` — 내 C 결과와 일치.

**Belady's anomaly** (README의 "왜 재밌을까?" 예제):

```text
python3 paging-policy.py -C 3 -a 1,2,3,4,1,2,5,1,2,3,4,5 -c | tail -1
FINALSTATS hits 3   misses 9   hitrate 25.00
python3 paging-policy.py -C 4 -a 1,2,3,4,1,2,5,1,2,3,4,5 -c | tail -1
FINALSTATS hits 2   misses 10   hitrate 16.67
```

**숙제 1번** (`-s 0 -n 10`, 캐시 3): 먼저 `-c` 없이 돌려 손으로 풀고, 그다음 `-c`로 확인.

```text
python3 paging-policy.py -s 0 -n 10 -c        # FIFO (기본)
Access: 8  MISS FirstIn ->          [8] <- Lastin  Replaced:- [Hits:0 Misses:1]
Access: 7  MISS FirstIn ->       [8, 7] <- Lastin  Replaced:- [Hits:0 Misses:2]
Access: 4  MISS FirstIn ->    [8, 7, 4] <- Lastin  Replaced:- [Hits:0 Misses:3]
Access: 2  MISS FirstIn ->    [7, 4, 2] <- Lastin  Replaced:8 [Hits:0 Misses:4]
Access: 5  MISS FirstIn ->    [4, 2, 5] <- Lastin  Replaced:7 [Hits:0 Misses:5]
Access: 4  HIT  FirstIn ->    [4, 2, 5] <- Lastin  Replaced:- [Hits:1 Misses:5]
Access: 7  MISS FirstIn ->    [2, 5, 7] <- Lastin  Replaced:4 [Hits:1 Misses:6]
Access: 3  MISS FirstIn ->    [5, 7, 3] <- Lastin  Replaced:2 [Hits:1 Misses:7]
Access: 4  MISS FirstIn ->    [7, 3, 4] <- Lastin  Replaced:5 [Hits:1 Misses:8]
Access: 5  MISS FirstIn ->    [3, 4, 5] <- Lastin  Replaced:7 [Hits:1 Misses:9]

FINALSTATS hits 1   misses 9   hitrate 10.00
```

같은 트레이스에서 `-p LRU` → `hits 2 misses 8 hitrate 20.00`, `-p OPT` → `hits 4 misses 6 hitrate 40.00`.

**숙제 4번** (지역성 있는 트레이스 + CLOCK 비트 수): 80-20 트레이스 1000개를 만들어 파일로 넣었다.

```text
python3 -c "
import random
random.seed(0)
for i in range(1000):
    print(random.randint(0,19) if random.random()<0.8 else random.randint(20,99))" > /tmp/trace8020.txt
for p in OPT LRU FIFO RAND; do python3 paging-policy.py -f /tmp/trace8020.txt -p $p -C 20 -c -N | grep FINAL; done
for b in 1 2 3; do python3 paging-policy.py -f /tmp/trace8020.txt -p CLOCK -b $b -C 20 -c -N | grep FINAL; done
```

```text
OPT        FINALSTATS hits 795   misses 205   hitrate 79.50
LRU        FINALSTATS hits 587   misses 413   hitrate 58.70
FIFO       FINALSTATS hits 525   misses 475   hitrate 52.50
RAND       FINALSTATS hits 534   misses 466   hitrate 53.40
CLOCK -b 1 FINALSTATS hits 559   misses 441   hitrate 55.90
CLOCK -b 2 FINALSTATS hits 584   misses 416   hitrate 58.40
CLOCK -b 3 FINALSTATS hits 608   misses 392   hitrate 60.80
```

해석: 캐시 20칸 = hot 페이지 수와 같은 크기. LRU가 FIFO보다 6%p 높고, Clock은 1비트일 때 그 중간, **비트를 늘릴수록 LRU에 가까워진다**(이 시드에서는 3비트가 LRU를 살짝 넘었는데, 짧은 트레이스의 우연이다). 그래도 OPT와는 20%p 차이 — 미래를 아는 것의 위력.

## 15. 펌웨어 엔지니어의 눈으로

- **SSD FTL 매핑 테이블 캐시 = 정확히 이 챕터의 문제.** DRAM-less 혹은 DRAM이 작은 컨트롤러는 L2P 매핑을 NAND에 두고 SRAM/DRAM에 일부만 캐시한다(DFTL 류). 매 호스트 I/O마다 "어떤 매핑 세그먼트를 내보낼까"가 교체 정책이고, **dirty 매핑 세그먼트를 내보내면 NAND write(= 쓰기 증폭 + 수명 소모)**가 생기므로 22.9의 clean-first 규칙이 그대로 적용된다. 순차 워크로드가 캐시를 오염시키는 문제는 looping/scan 문제와 같은 모양이다.
- **GC victim 선택도 교체 정책의 사촌.** Greedy(유효 페이지 최소 블록)와 cost-benefit(나이 고려)은 "지금 비용 vs 과거 이력" 의 트레이드오프로, FIFO vs LRU 논쟁과 구조가 같다. hot/cold 분리는 80-20 워크로드를 활용하는 기법이다.
- **Clustering of writes ↔ NAND 프로그램 단위.** OS가 dirty 페이지를 묶어 큰 write를 내듯, FW는 4KB 쓰기를 모아 NAND page/multi-plane/superpage 단위로 프로그램한다. 원리는 동일: 매체가 큰 단위 전송에서 효율적이다.
- **GPU Unified Memory 오버서브스크립션.** CUDA UVM에서 GPU 메모리보다 큰 데이터를 쓰면 드라이버가 GPU 페이지 폴트를 받고 큰 단위로 페이지를 eviction/migration한다. 접근 패턴이 순환이면 바로 이 챕터의 looping 워크로드처럼 thrashing이 일어난다. 그래서 prefetch 힌트나 명시적 배치가 중요하다.
- **AI 가속기 SRAM 스크래치패드는 OPT를 쓸 수 있다.** 정적 계산 그래프는 텐서 접근 순서를 컴파일 타임에 알기 때문에, 컴파일러의 메모리 플래너가 "가장 늦게 다시 쓰일 텐서를 DRAM으로 내보낸다"는 Belady식 결정을 내릴 수 있다. 범용 OS가 못 하는 OPT를 가속기 컴파일러는 근사적으로 할 수 있다는 점이 면접에서 좋은 포인트다.
- **온디바이스 LLM의 KV cache.** 메모리가 모자라면 어떤 시퀀스/블록을 내보낼지(재계산 vs 스왑) 결정해야 하는데, vLLM의 PagedAttention처럼 KV cache를 페이지 단위로 관리하면 이 챕터의 용어(페이지, eviction, thrashing, admission control = 동시 요청 수 제한)가 그대로 쓰인다.

## 16. 면접 질문

### Q1. LRU를 실제 OS에서 정확히 구현하지 않는 이유는? 대신 무엇을 하나?
<details>
<summary>답 보기</summary>

- 정확한 LRU는 **매 메모리 접근마다** 리스트/타임스탬프를 갱신해야 한다. 명령어 fetch와 load/store마다 SW가 개입할 수 없고, HW 타임스탬프를 둬도 교체 때 **수백만 페이지를 스캔**해야 한다.
- 그래서 HW는 접근 시 **use(Accessed) bit**만 세팅하고, OS는 주기적으로 지우면서 **Clock(second chance)** 같은 근사를 쓴다.
- 실제 커널(Linux)은 여기에 **active/inactive 리스트**(최근 2세대) 같은 구조를 더해 scan resistance를 얻는다.
- 핵심 키워드: **use bit, 주기적 clear, Clock, 근사로 충분**.

</details>

### Q2. Belady's anomaly란 무엇이고, 왜 LRU에는 없나?
<details>
<summary>답 보기</summary>

- 캐시(프레임 수)를 늘렸는데 **miss가 오히려 늘어나는** 현상. FIFO + `1,2,3,4,1,2,5,1,2,3,4,5`: 3프레임 9 miss, 4프레임 10 miss.
- LRU와 OPT는 **stack property**(inclusion property): 크기 N 캐시 내용 ⊆ 크기 N+1 캐시 내용. 따라서 어떤 접근이 N에서 hit이면 N+1에서도 hit이다.
- FIFO·Random·(모든 bit가 1일 때의) Clock은 이 성질이 없어 anomaly가 가능하다.
- 실무적 의미: stack 알고리즘은 **한 번의 시뮬레이션으로 모든 캐시 크기의 hit rate를 계산**할 수 있다(Mattson의 stack distance).

</details>

### Q3. LRU가 최악인 워크로드는? 어떻게 막나?
<details>
<summary>답 보기</summary>

- **캐시보다 조금 큰 범위를 순환하는 looping-sequential** 접근. 50페이지 루프 + 49칸 캐시면 LRU·FIFO 모두 **hit 0%**. 다음에 쓰일 페이지가 항상 가장 오래된 페이지라서.
- 큰 파일 순차 스캔이 hot 페이지를 밀어내는 **cache pollution**도 같은 문제.
- 대책: **scan-resistant** 정책(ARC, 2Q, Linux active/inactive 리스트 — 한 번만 쓰인 페이지는 inactive에 머묾), Random 섞기, 애플리케이션 힌트(`madvise(MADV_SEQUENTIAL)`, `posix_fadvise(POSIX_FADV_DONTNEED)`), DB의 자체 버퍼 관리(MRU 사용).

</details>

### Q4. 페이지를 내보낼 때 dirty bit는 왜 중요한가?
<details>
<summary>답 보기</summary>

- dirty 페이지를 내보내려면 **디스크 write가 먼저** 필요하다 → 지연 증가, I/O 대역폭 소모, (SSD라면) 쓰기 수명 소모.
- clean 페이지는 프레임을 **즉시 재사용** 가능(파일 기반이면 원본에서 다시 읽으면 됨).
- 그래서 Clock을 (use, dirty) 4클래스로 확장해 **(0,0) 먼저**, 그다음 (0,1) 순으로 고른다.
- 실제 OS는 백그라운드 **writeback(flusher)**이 dirty 페이지를 미리 묶어서 써 두어, 교체 시점엔 clean 페이지가 충분하도록 한다(clustering).

</details>

### Q5. 시스템이 thrashing에 빠졌다. 어떻게 감지하고 무엇을 할 수 있나?
<details>
<summary>답 보기</summary>

- 증상: **page fault율 급증, CPU 사용률은 낮은데 디스크 I/O는 포화**, 처리량 급락. Linux에선 `vmstat`의 si/so, PSI(`/proc/pressure/memory`) 같은 지표.
- 원인: 실행 중 프로세스들의 **working set 합 > 물리 메모리**.
- 대책: **admission control**(동시 실행 수 줄이기, 일부 프로세스 swap-out/중단), working-set 기반 스케줄링, 최후 수단으로 **OOM killer**. 근본 해결은 메모리 추가 또는 워크로드 축소.
- 임베디드/디바이스 관점: 스왑이 없는 시스템(대부분의 모바일/RTOS)은 thrashing 대신 **즉시 kill**(Android lmkd, iOS jetsam)을 택한다.

</details>

### Q6. 컴파일러가 관리하는 NPU SRAM에서는 어떤 교체 정책을 쓸 수 있나?
<details>
<summary>답 보기</summary>

- 정적 그래프에서는 **미래 접근 순서를 알 수 있다** → Belady의 **OPT(가장 늦게 다시 쓰일 텐서를 내보냄)**를 근사할 수 있다.
- 추가로 고려할 것: 텐서 크기가 제각각이라 단순 OPT가 최적이 아니다(가변 크기 캐싱은 어려운 문제), DMA 전송 비용과 계산의 **overlap(더블 버퍼링)**, dirty 텐서(다시 써야 하는 activation)는 write-back 비용.
- 동적 형태(가변 시퀀스 길이 LLM 등)에서는 미래를 모르니 다시 LRU 류 런타임 정책이 필요하다.

</details>

## 17. 자가 점검 & 숙제

### 퀴즈

**Q1.** T_M = 100 ns, T_D = 10 ms일 때 AMAT를 1 µs 이하로 만들려면 hit rate가 최소 얼마여야 하나?
<details>
<summary>정답</summary>

P_miss × 10,000,000 ns + (1 − P_miss) × 100 ns ≤ 1,000 ns → P_miss × (10,000,000 − 100) ≤ 900 → P_miss ≤ 약 0.00009. 즉 **hit rate ≥ 99.991%**.

</details>

**Q2.** 캐시 3칸, 참조열 `7,0,1,2,0,3,0,4` 에서 LRU의 miss 수는?
<details>
<summary>정답</summary>

7M [7] → 0M [7,0] → 1M [7,0,1] → 2M (evict 7) [0,1,2] → 0H [1,2,0] → 3M (evict 1) [2,0,3] → 0H [2,3,0] → 4M (evict 2) [3,0,4]. **miss 6, hit 2.**

</details>

**Q3.** Clock 알고리즘에서 모든 페이지의 use bit가 1이면 어떤 정책처럼 동작하나?
<details>
<summary>정답</summary>

바늘이 한 바퀴 돌며 전부 0으로 지운 뒤 **출발점의 페이지**를 버린다. 이것이 반복되면 **FIFO**와 같다. 그래서 Clock에도 Belady's anomaly가 생길 수 있다(위 실행 결과의 CLOCK 3→2 hit).

</details>

**Q4.** OS 페이지 캐시에 conflict miss가 없는 이유는?
<details>
<summary>정답</summary>

어떤 가상 페이지든 **어떤 물리 프레임에나** 매핑할 수 있는 **fully-associative** 구조이기 때문이다. HW 캐시는 주소 비트로 set이 정해져 같은 set끼리 충돌한다.

</details>

**Q5.** "No-locality" 워크로드에서 LRU, FIFO, Random의 hit rate가 거의 같은 이유는?
<details>
<summary>정답</summary>

다음 접근이 과거와 무관하게 균등 무작위이므로, 어떤 페이지를 남겨도 다음에 hit할 확률은 **캐시 크기 / 전체 페이지 수**로 같다. 이력이 정보를 주지 못한다. 미래를 아는 OPT만 더 좋다.

</details>

### 꼭 해볼 원문 숙제

- **Homework 1** (`-s 0/1/2 -n 10`, FIFO/LRU/OPT): 손으로 트레이스를 그려 hit/miss와 캐시 상태를 맞히는 연습. 면접 화이트보드 대비.
- **Homework 2** (캐시 5칸에서 FIFO/LRU/MRU 최악 참조열 만들기): 예를 들어 FIFO·LRU는 `0,1,2,3,4,5,0,1,2,3,4,5,…`(6페이지 순환)이 최악이고 캐시를 6으로만 늘려도 급격히 좋아진다는 것 — looping 워크로드의 직관 확인.
- **Homework 4** (지역성 트레이스 + `-p CLOCK -b N`): use bit 수(=이력 해상도)가 LRU 근사 품질에 미치는 영향 확인. 위 14.2 결과를 다른 시드로 재현해 보자.

## 18. 다음으로

- 다음 챕터: [Ch.23 VAX/VMS 가상 메모리 시스템](2026-09-30_C23_vax_vms.md) — 이 챕터의 개념(FIFO, second chance, dirty 리스트, clustering)이 실제 OS에서 어떻게 조합되는지. 특히 VAX에는 **use bit가 없어서** 보호 비트로 흉내 내는 트릭이 나온다.
- 복습: [Ch.21 스와핑 메커니즘](2026-09-30_C21_swapping_mechanisms.md) (present bit, page fault 처리, swap daemon의 high/low watermark).
- 더 읽을거리: Megiddo & Modha, *ARC* (FAST '03), Mattson et al. (1970) stack property.
