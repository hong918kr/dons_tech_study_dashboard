# C6. 해시맵을 직접 짠다 — 오픈 어드레싱

> **이 노트를 읽고 나면**
> - malloc 없이 고정 배열 하나로 해시맵을 처음부터 끝까지 손으로 짤 수 있다
> - 정수 키와 바이트열 키에 각각 맞는 해시 함수를 고르고, **왜 하위 비트만 쓰면 망하는지** 숫자로 설명할 수 있다
> - 오픈 어드레싱에서 삭제가 왜 어려운지 말하고, 톰스톤 / backward shift / 시간 기반 lazy 만료 중 하나를 골라 이유를 댈 수 있다
>
> **선행**: 없음. C 배열과 구조체, 비트 AND(`&`)만 알면 된다.
>
> **이 개념을 쓰는 문제**: [07_badge_audit_dedupe](../07_badge_audit_dedupe/question_note) Part 2 (`audit_is_duplicate` — badge_id로 중복 판정) ·
> [09_wifi_scan_snapshot](../09_wifi_scan_snapshot/question_note) Part 2 (`wifi_lookup` — 6바이트 BSSID로 AP 조회).
> top-k 쪽은 [C7. 힙과 top-k](C7_heap_and_topk.md)에서 이어서 다룬다.

---

## 1. 왜 이게 필요한가

07번 문제에서 막히는 지점은 딱 한 줄이다.

> `int audit_is_duplicate(uint32_t badge_id, uint64_t now_us, uint64_t window_us);`
> "O(1) average. 이 함수는 문이 열리는 경로에 있다."

카드를 댄 사람이 30초 안에 같은 카드를 또 댔는지(anti-passback) 판정해야 한다. 최근에 본
badge_id를 전부 기억해야 하는데, C에는 표준 해시맵이 없다. 그래서 직접 짠다.

그리고 임베디드에서는 **직접 짜는 쪽이 더 낫다.** 이유는 세 가지다.

- **malloc이 없다(또는 쓰면 안 된다)**: 장시간 돌아가는 door controller에서 힙을 계속 쓰면
  단편화가 쌓여 언젠가 `malloc`이 NULL을 돌려준다. 그 시점은 예측할 수 없다.
- **메모리를 미리 못 박아야 한다**: `dedupe_slot_t map[1024]` 는 링커 맵에 16 KB로 찍힌다.
  "이 기능은 16 KB"를 빌드 시점에 말할 수 있다. 동적 자료구조는 못 한다.
- **최악 지연이 예측 가능해야 한다**: 뒤에 나오는 **probe window**가 최악을 상수로 못 박는다.

정렬 배열은 삽입이 O(n)이라 카드를 댈 때마다 512개를 memmove 한다. 이진 탐색 트리는 노드마다
포인터 두 개에 malloc이 필요하고 조회 한 번에 캐시 미스가 log n 번 난다.
해시맵은 **삽입도 조회도 평균 O(1), 메모리는 고정, 캐시 라인 한두 줄**이다.

---

## 2. 그림으로 먼저

용량 8칸짜리 작은 테이블에 badge 1001, 1009, 1002를 넣는 장면이다.
`home` = 해시가 가리키는 "원래 자리". 이미 차 있으면 **옆 칸으로 밀린다**(선형 탐사).

```svg
<svg viewBox="0 0 640 250" role="img" aria-label="선형 탐사로 충돌을 옆 칸으로 밀어내는 그림">
  <text x="10" y="20" class="lbl">slot</text>
  <rect class="box" x="60"  y="30" width="66" height="40" rx="6"/>
  <rect class="box" x="126" y="30" width="66" height="40" rx="6"/>
  <rect class="fill-soft" x="192" y="30" width="66" height="40" rx="6"/>
  <rect class="fill-soft" x="258" y="30" width="66" height="40" rx="6"/>
  <rect class="fill-soft" x="324" y="30" width="66" height="40" rx="6"/>
  <rect class="box" x="390" y="30" width="66" height="40" rx="6"/>
  <rect class="box" x="456" y="30" width="66" height="40" rx="6"/>
  <rect class="box" x="522" y="30" width="66" height="40" rx="6"/>
  <text x="225" y="55" text-anchor="middle">1001</text>
  <text x="291" y="55" text-anchor="middle">1009</text>
  <text x="357" y="55" text-anchor="middle">1002</text>
  <text class="lbl" x="93"  y="86" text-anchor="middle">0</text>
  <text class="lbl" x="159" y="86" text-anchor="middle">1</text>
  <text class="lbl" x="225" y="86" text-anchor="middle">2</text>
  <text class="lbl" x="291" y="86" text-anchor="middle">3</text>
  <text class="lbl" x="357" y="86" text-anchor="middle">4</text>
  <text class="lbl" x="423" y="86" text-anchor="middle">5</text>
  <text class="lbl" x="489" y="86" text-anchor="middle">6</text>
  <text class="lbl" x="555" y="86" text-anchor="middle">7</text>

  <text x="20" y="130">1001</text>
  <line class="accent" x1="60" y1="124" x2="222" y2="124"/>
  <polygon class="accent" points="222,124 214,120 214,128"/>
  <text class="lbl" x="130" y="118">home=2, 비어 있다 → 그대로</text>

  <text x="20" y="168">1009</text>
  <line class="muted" x1="60" y1="162" x2="222" y2="162"/>
  <polygon class="muted" points="222,162 214,158 214,166"/>
  <line class="accent" x1="228" y1="162" x2="288" y2="162"/>
  <polygon class="accent" points="288,162 280,158 280,166"/>
  <text class="lbl" x="150" y="156">home=2, 차 있다</text>
  <text class="lbl" x="300" y="156">→ 옆 칸 3</text>

  <text x="20" y="206">1002</text>
  <line class="muted" x1="60" y1="200" x2="288" y2="200"/>
  <polygon class="muted" points="288,200 280,196 280,204"/>
  <line class="accent" x1="294" y1="200" x2="354" y2="200"/>
  <polygon class="accent" points="354,200 346,196 346,204"/>
  <text class="lbl" x="160" y="194">home=3, 두 칸 차 있다</text>
  <text class="lbl" x="366" y="194">→ 4</text>

  <text class="lbl" x="20" y="238">조회도 같은 길을 걷는다: home 부터 오른쪽으로, 키가 같은 칸이 나올 때까지.</text>
</svg>
```

07번이 쓰는 실제 모양은 이렇다. 1024칸 테이블이지만 **한 키가 걸어도 되는 거리는 16칸으로
못 박는다**. 그게 `AUDIT_DEDUPE_WINDOW`다.

```
badge_id ──▶ dedupe_home() ──▶ home = 417
                                 │
      map[1024]  ...  [415][416][417][418][419] ... [432][433] ...
                            │    └──── probe window = 16칸 ────┘
                            │         이 안에서만 찾고, 이 안에서만 넣는다
                            │
                 16칸이 다 차면? 여기서 멈춘다 (O(1) 최악 보장)
                 → 희생자를 골라 덮어쓰고 evictions++ 카운터를 올린다
```

---

## 3. 개념 (용어를 하나씩)

### 해시 함수 (hash function)

**정의**: 임의의 키를 고정 크기 정수로 바꾸는 함수. `uint32_t h = hash(key);`

**왜 존재하나**: 배열 인덱스는 0..CAP-1 이어야 하는데 키는 그보다 훨씬 넓은 공간에 있다.
badge_id는 32비트(43억 가지)인데 슬롯은 1024개다. 43억을 1024로 **접어 넣는** 것이 해시다.

### home 슬롯과 `hash & (CAP-1)`

**정의**: 키가 "원래 있어야 할 자리". `home = hash(key) & (CAP - 1)`.

**왜 AND인가**: CAP이 2의 거듭제곱이면 `x % CAP` 과 `x & (CAP-1)` 이 같다.
`1024 = 2^10` 이므로 `x & 1023` 은 x의 하위 10비트다. 나눗셈은 Cortex-M 같은 코어에서
수십 사이클이고 하드웨어 나눗셈이 아예 없는 칩도 있다. AND는 1사이클이다.

그래서 두 문제 다 용량을 2의 거듭제곱으로 강제한다.

```c
/* 07_badge_audit_dedupe/audit_log_solution.c */
_Static_assert((AUDIT_DEDUPE_CAP & (AUDIT_DEDUPE_CAP - 1u)) == 0u, "dedupe cap must be 2^n");
#define HMASK (AUDIT_DEDUPE_CAP - 1u)
```

`_Static_assert`는 컴파일 시점에 검사한다. 누군가 CAP을 1000으로 바꾸면 **빌드가 깨진다** —
런타임에 조용히 틀리는 것보다 훨씬 낫다. 이 줄이 없으면 CAP=1000일 때 `& 999` 는
0..999를 고르게 덮지 않고 이상한 부분집합만 가리킨다(999는 2^n-1이 아니다).

### 충돌 (collision)

**정의**: 서로 다른 키가 같은 home을 갖는 것. 키 공간이 슬롯 수보다 크므로 **반드시 일어난다** —
1024칸에 서로 다른 41개 키만 넣어도 충돌 확률이 50%를 넘는다(생일 문제).
**그래서 해시맵 설계는 곧 "충돌을 어떻게 처리하나"의 설계다.**

### 체이닝 vs 오픈 어드레싱

| | 체이닝 (chaining) | 오픈 어드레싱 (open addressing) |
|---|---|---|
| 충돌 처리 | 슬롯마다 연결 리스트, 뒤에 매단다 | 배열 안의 다른 빈 칸을 찾아 들어간다 |
| 메모리 | 버킷 배열 + 노드마다 malloc + 포인터 8B | 배열 하나. 끝 |
| malloc | 필요 | **불필요** |
| 부하율 | 1.0 넘어도 동작(리스트가 길어질 뿐) | 1.0 넘을 수 없다. 0.7~0.75 밑으로 유지 |
| 캐시 | 노드마다 포인터 추적 → 미스 연발 | 옆 칸으로 가니 같은 캐시 라인, prefetch가 먹는다 |
| 삭제 | 쉽다. 리스트에서 떼면 끝 | **어렵다**(§5에서 이 노트의 절반을 차지한다) |
| 최악 조회 | O(리스트 길이) | O(probe 길이) — window로 상수로 묶을 수 있다 |

임베디드에서 오픈 어드레싱이 기본값인 이유가 표에 다 있다. malloc이 없고, 캐시가 작고,
최악 지연을 상수로 묶어야 한다. 삭제의 어려움만 해결하면 된다.

### 선형 탐사 (linear probing)와 클러스터링

**정의**: 충돌하면 `home+1, home+2, ...` 순서로 오른쪽 빈 칸을 찾는 방식.

**왜 이것인가**: 메모리를 앞으로만 순차 접근하므로 하드웨어 prefetcher가 다음 칸을 미리
읽어 둔다. 16개 슬롯 × 16바이트 = 256바이트 = 캐시 라인 4줄이다.

**클러스터링(clustering)**: 찬 칸이 길게 붙어 덩어리를 이루는 현상. 덩어리 안에 home이
떨어진 **다른** 키는 덩어리 끝까지 걸어야 한다. 덩어리에 붙을 확률이 길이에 비례하므로
눈덩이처럼 커진다. 선형 탐사의 유일한 약점이고, 해시를 잘 고르면 사라진다.

### 좋은 해시의 조건

- **고르게 퍼진다**: "이론적으로 균등"이 아니라 **이 제품에 실제로 들어오는 키**에 균등해야 한다.
- **한 비트가 바뀌면 많이 바뀐다(avalanche)**: 그러면 연속된 키, 비슷한 MAC이 멀리 떨어진다.
- **싸다**: 문 여는 경로다. 곱셈 한 번, XOR 한 번 수준. SHA-256은 정답이 아니다.

암호학적 안정성은 **필요 없다.** 공격자가 키를 고를 수 있는 서버(해시 DoS)라면 다르지만,
badge_id는 우리 카드 프린터가 발급하고 BSSID는 공기에서 온다.

### 정수 키: 곱셈(Fibonacci) 해시

32비트 정수 키는 **큰 홀수를 한 번 곱하는 것**으로 충분하다. 2654435761은 2^32를 황금비로
나눈 값에 가까운 소수다(Knuth의 Fibonacci hashing).

```c
/* 07_badge_audit_dedupe/audit_log_solution.c */
static uint32_t dedupe_home(uint32_t id)
{
    return (uint32_t)(id * 2654435761u) & HMASK;     /* Fibonacci hashing */
}
```

곱셈은 `uint32_t`에서 자동으로 mod 2^32 로 잘린다(부호 없는 정수의 오버플로는 C에서
정의된 동작이다 — `int`로 하면 UB다). 연속된 id는 곱한 결과가 `2654435761 mod 1024 = 433`
씩 벌어진다. 433과 1024는 서로소이므로 연속된 카드 600장이 테이블 전체에 골고루 퍼진다.

### 바이트열 키: FNV-1a

키가 6바이트 MAC 주소처럼 바이트 배열이면 바이트마다 XOR 후 소수를 곱한다.

```c
/* 09_wifi_scan_snapshot/ap_table_solution.c */
static uint32_t bssid_hash(const uint8_t b[6])
{
    uint32_t h = 2166136261u;             /* FNV offset basis */
    for (int i = 0; i < 6; i++) {
        h ^= (uint32_t)b[i];
        h *= 16777619u;                   /* FNV prime */
    }
    h ^= h >> 16;
    return h;
}
```

- `h ^= b[i]` 먼저, 그 다음 `h *= prime` — 이 순서가 FNV-**1a**다. 순서를 뒤집으면
  (FNV-1) 하위 바이트의 영향이 약해져서 품질이 떨어진다.
- 마지막 `h ^= h >> 16` 이 **xor fold**다. 우리는 `h & (CAP-1)` 로 **하위** 비트만 쓰는데
  FNV는 상위 비트가 더 잘 섞여 있다. 상위 16비트를 하위로 접어 내려 섞는다.
  이 줄이 없으면 벤더가 연속으로 발급한 BSSID들이 몇 개 버킷에 뭉친다.

### 왜 하위 비트만 쓰면 망하는가 (실측)

말로만 하면 안 믿긴다. 1024칸 · window 16 테이블에 600개 키를 넣고 세 가지 해시를 비교했다
(드라이버는 §7에 있다). `mul>>low` 는 07번이 쓰는 `(id*2654435761) & MASK`,
`mul>>high` 는 곱한 결과의 **상위** 10비트 `(id*2654435761) >> 22` 를 쓰는 변형이다.

| 키 집합 | 해시 | 평균 probe | 최악 probe | 살아 있는 항목을 밀어낸 삽입 |
|---|---|---|---|---|
| 연속 id 10000..10599 | `id & MASK` | 1.00 | 1 | 0 / 600 |
| 연속 id 10000..10599 | `mul>>high` | 1.00 | 1 | 0 / 600 |
| **하위 10비트가 시설 코드** `(seq<<10)\|0x2a` | `id & MASK` | 16.77 | 17 | **584 / 600** |
| 같은 키 집합 | `mul>>low` | 16.77 | 17 | **584 / 600** |
| 같은 키 집합 | `mul>>high` | 1.11 | 2 | 0 / 600 |
| 8개 사이트, stride 4096 | `id & MASK` | 4.50 | 8 | 0 / 600 |
| 같은 키 집합 | `mul>>high` | 1.11 | 7 | 0 / 600 |

읽는 법이 중요하다.

- **연속된 카드만 들어올 때는 masking도 멀쩡하다.** "dense counter니까 masking은 망한다"는
  거친 요약이다. 연속 id는 연속 슬롯에 들어가므로 충돌 자체가 안 난다.
- 망하는 건 **키의 하위 비트가 상수이거나 stride를 가질 때**다. 카드 번호에 시설 코드가
  하위 10비트로 박혀 있으면 600장이 모두 같은 home으로 가고, window 16이 꽉 차서
  584장이 살아 있는 항목을 밀어낸다 — anti-passback이 조용히 죽는다.
- **`mul>>low` 가 masking과 완전히 같다는 점이 핵심이다.** 홀수를 곱해 하위 10비트를 취하는
  연산은 하위 10비트 위에서의 **1대1 대응(bijection)** 이다. 슬롯 이름만 바꿔 줄 뿐
  "어느 키들이 서로 충돌하나"는 못 바꾼다. 하위 비트가 같은 두 키는 곱한 뒤에도 같다.
- 곱셈의 진짜 효과는 **클러스터를 깨는 것**이다. 700개 연속 id로 테이블을 68%까지 채우고
  나서 관계 없는 키 100개를 넣어 보면:

```
D) 700 consecutive ids fill the table to 68%, then 100 unrelated ids arrive
  id & MASK      avg probes for the 100 latecomers=259.57  worst=741
  mul>>low bits  avg probes for the 100 latecomers=3.09  worst=12
  mul>>high bits avg probes for the 100 latecomers=3.02  worst=9
```

masking은 700칸짜리 덩어리 하나를 만들어서 나중에 온 키가 평균 **260칸**을 걷는다.
곱셈은 3칸이다. 07번이 곱셈을 쓰는 이유가 이것이고, 그 주석의 "433 slots apart"는 맞는 설명이다.

**면접에서 말할 한 줄**: "Multiplying by an odd constant and then masking is a bijection on
the low bits, so it breaks up clusters but it cannot fix keys that share low bits. If the low
bits carry a facility code, take the **high** bits of the product instead."

바이트열 키에서 "MAC 마지막 바이트를 그냥 쓰자"가 안 되는 이유도 같다. 한 벤더가 라디오마다
BSSID를 16씩 띄워 발급하면(흔한 일이다) 하위 4비트가 고정된다.

```
test2 last byte : 300 BSSIDs (stride 16) ->  16/256 buckets used, worst bucket=19
test2 FNV-1a+fold: 300 BSSIDs (stride 16) -> 205/256 buckets used, worst bucket=4
```

### 부하율 (load factor)과 용량 산정

**정의**: `부하율 = 사용 중인 슬롯 수 / 전체 슬롯 수`.

선형 탐사에서 평균 probe 길이는 부하율 α에 대해 대략 `(1 + 1/(1-α)^2)/2` 로 늘어난다.
α=0.5면 2.5칸, α=0.75면 8.5칸, α=0.9면 50칸이다. **0.9를 넘기면 절벽이다.** 그래서
`CAP >= (동시에 살아 있어야 하는 키 개수) / 0.75` 를 2의 거듭제곱으로 올려 잡는다.

07번의 계산: retention 30초 안에 서로 다른 카드 몇 장이 올 수 있나 → 넉넉히 768장으로 잡고
`768 / 0.75 = 1024`. 그래서 `AUDIT_DEDUPE_CAP = 1024`, `AUDIT_DEDUPE_LOAD_LIMIT = 768`.

중요한 점: 이 768은 **설계 예산이지 코드가 강제하는 한계가 아니다.** malloc이 없으니
rehash로 키울 수도 없다. 넘으면 성능이 나빠지고 eviction이 시작될 뿐이다.
그래서 **카운터를 내보낸다** — 조용히 나빠지는 대신 대시보드에 숫자가 뜬다.

### 가득 찼을 때의 정책

두 문제가 서로 다른 답을 골랐고, 둘 다 맞다.

| | 07 (dedupe) | 09 (AP table) |
|---|---|---|
| window/테이블이 다 찼을 때 | 가장 오래된 항목을 **덮어쓴다** | 새 항목을 **거부한다** |
| 카운터 | `audit_dedupe_evictions()` | `wifi_table_dropped()` |
| 왜 | 오래된 카드는 anti-passback 창이 곧 끝난다. 최신 정보가 더 중요 | 살아 있는 AP를 임의로 버리면 roaming 판단이 틀린다. 거부가 정직 |

희생자 우선순위는 07번이 명확하다. **만료된 슬롯 → 빈 슬롯 → 가장 오래된 살아 있는 슬롯**.
만료된 슬롯이 빈 슬롯보다 먼저인 게 포인트다(이유는 바로 다음 절).

---

## 4. 코드로 보기

가장 작은 조회부터. 07번의 `audit_is_duplicate` 본체다.

```c
/* 07_badge_audit_dedupe/audit_log_solution.c */
int audit_is_duplicate(uint32_t badge_id, uint64_t now_us, uint64_t window_us)
{
    uint32_t home = dedupe_home(badge_id);
    int dup = 0;

    pthread_mutex_lock(&idx.m);
    for (uint32_t i = 0; i < AUDIT_DEDUPE_WINDOW; i++) {
        /* No early exit on an empty slot — see the NO TOMBSTONES note. */
        const dedupe_slot_t *e = &idx.map[(home + i) & HMASK];
        if (e->used && e->id == badge_id) {
            uint64_t t = e->last_seen;
            dup = (t <= now_us && now_us - t <= window_us) ? 1 : 0;
            break;
        }
    }
    pthread_mutex_unlock(&idx.m);
    return dup;
}
```

한 줄씩.

- `(home + i) & HMASK` — 테이블 끝을 넘어가면 0으로 감싼다. 없으면 배열 밖을 읽는다
  (home=1020, i=10 이면 인덱스 1030).
- `e->used && e->id == badge_id` — `used` 를 먼저 봐야 한다. 안 쓴 슬롯의 `id` 는 0이라
  badge_id 0이 들어오면 빈 칸을 히트로 착각한다.
- `t <= now_us && now_us - t <= window_us` — 뺄셈 전에 순서를 확인한다. `uint64_t` 에서
  `now_us - t` 가 음수면 underflow로 거대한 값이 되어 항상 "중복 아님"이 된다. 이 줄이
  없으면 시계가 뒤로 점프한 순간 anti-passback이 무력화된다.
- **빈 칸에서 멈추지 않는다** — 교과서는 "첫 빈 칸을 만나면 없는 것"이라고 가르치는데,
  여기서는 그게 **틀린다.** 왜 틀리는지가 §5의 핵심이다.

삽입 쪽은 희생자를 고르는 부분만 보면 된다.

```c
/* 07_badge_audit_dedupe/audit_log_solution.c — dedupe_note() 중 희생자 선택 */
long victim;
if (first_stale >= 0) {
    victim = first_stale;                    /* lazy age eviction           */
    idx.stale_reuse++;
} else if (first_empty >= 0) {
    victim = first_empty;
    idx.live++;
} else {
    victim = oldest;                         /* 창이 다 살아 있다: 테이블이 작다 */
    idx.evictions++;
}
idx.map[victim].id = id;
idx.map[victim].last_seen = ts;
idx.map[victim].used = true;
```

`first_stale` 이 `first_empty` 보다 먼저인 이유: 만료된 슬롯을 그대로 두고 빈 칸을 쓰면
사용 중 슬롯이 하나 늘어난다(부하율 상승). 만료된 슬롯을 재사용하면 부하율이 그대로다.
**죽은 항목부터 먹는 게 공짜 청소다.**

---

## 5. 단계별로 만들어 보기

### v0 — 한 칸만 쓴다 (틀림)

```c
/* v0: 충돌을 아예 처리하지 않는다 */
static uint32_t ids[1024];
static uint64_t seen[1024];

static void note_v0(uint32_t id, uint64_t ts) { uint32_t s = id & 1023u; ids[s] = id; seen[s] = ts; }
static int  dup_v0(uint32_t id, uint64_t now, uint64_t win)
{
    uint32_t s = id & 1023u;
    return (ids[s] == id && now - seen[s] <= win) ? 1 : 0;
}
```

**무엇이 틀렸나**: 1001과 2025는 home이 같다(둘 다 하위 10비트가 같다). 2025를 넣으면 1001의
기록이 사라진다. 1001이 30초 안에 또 들어와도 "중복 아님"이 되고, 카드 한 장으로 두 사람이
들어간다. 이게 바로 이 기능이 막아야 했던 일이다.

### v1 — 선형 탐사를 붙이고, 삭제도 붙인다 (더 미묘하게 틀림)

탐사를 붙이면 v0의 문제는 사라진다. 그런데 "만료된 항목은 지워야지" 하고 진짜 삭제
(`used = false`)를 넣는 순간 새 버그가 생긴다. 8칸 테이블에 home이 모두 0인 키 0, 8, 16을
넣고 가운데 8을 지워 본 결과다.

```
(1) inserted 0,8,16 -> slots: XXX.....
    after deleting 8   -> slots: X.X.....
    find(16) early-exit=LOST   full-window=FOUND   <- 16 은 아직 테이블에 있다
```

그림으로 보면 이렇다.

```
넣을 때:   home(0)=home(8)=home(16)=0
  slot:   0    1    2    3
         [ 0 ][ 8 ][16 ][   ]        ← 16은 "0과 8을 지나서" 여기 있다

8을 지우면:
  slot:   0    1    2    3
         [ 0 ][   ][16 ][   ]
                ↑
         조회가 여기서 "빈 칸이네, 없구나" 하고 멈춘다
         → 16은 테이블에 있는데 조회는 못 찾는다. 사슬(probe chain)이 끊겼다.
```

**오픈 어드레싱에서 삭제가 어려운 이유가 정확히 이것이다.** 항목의 위치는 그 항목만의
사정이 아니라, "그 앞에 무엇이 있었는가"의 역사다. 앞을 비우면 뒤가 미아가 된다.

### v2 — 세 가지 해법

**(a) 톰스톤 (tombstone)**: 슬롯에 `DELETED` 라는 제3의 상태를 둔다. 조회는 톰스톤을
"지나가되 멈추지 않고", 삽입은 톰스톤을 재사용한다. 교과서적이지만 톰스톤이 쌓인다 —
오래 반복하면 조회가 항상 테이블 전체를 걷게 되고, 그래서 진짜 구현은 주기적으로 rehash 해서
쓸어낸다. **우리는 rehash를 할 수 없다(malloc 없음).**

**(b) 뒤 항목 당겨오기 (backward shift deletion)**: 지운 칸 뒤를 훑어서 사슬이 끊기면 안 되는
항목을 앞으로 당겨 온다. 톰스톤이 안 쌓이고 부하율이 정직하다. 대신 삭제가 O(probe 길이)이고,
무엇을 당겨야 하는지 판정이 까다롭고(`home` 을 다시 계산해 비교), **항목이 움직이는 동안**
다른 스레드의 조회가 놓칠 수 있어 락을 더 넓게 잡아야 한다.

**(c) 시간 기반 lazy 만료 — 이 저장소가 고른 것**: **아예 지우지 않는다.**
슬롯은 "빈 칸 → 사용 중"으로 한 번만 가고 되돌아오지 않는다. 각 항목에 `last_seen`을 두고
규칙 두 개만 정한다.

- **조회**: 만료된 항목은 결과에 안 보인다(07은 `now - t <= window_us`, 09는 `entry_stale()`).
- **삽입**: 만료된 슬롯은 **그 자리에서 재사용**한다. 사슬은 그대로다 — 슬롯이 비지 않으니까.

```svg
<svg viewBox="0 0 640 210" role="img" aria-label="시간 기반 lazy 만료: 만료된 슬롯이 조회에는 안 보이지만 자리는 지킨다">
  <line class="muted" x1="40" y1="170" x2="600" y2="170"/>
  <polygon class="muted" points="600,170 592,166 592,174"/>
  <text class="lbl" x="600" y="190" text-anchor="end">시간</text>

  <rect class="fill-soft" x="40" y="40" width="200" height="34" rx="6"/>
  <text x="140" y="62" text-anchor="middle">retention 안 — 살아 있다</text>
  <rect class="box" x="240" y="40" width="360" height="34" rx="6"/>
  <text x="420" y="62" text-anchor="middle">retention 지남 — 죽었다 (슬롯은 그대로)</text>

  <line class="accent" x1="240" y1="30" x2="240" y2="180"/>
  <text class="lbl" x="244" y="26">last_seen + RETENTION</text>

  <text x="40" y="110">조회:</text>
  <text class="lbl" x="100" y="110">"중복" 이라고 답한다</text>
  <text class="lbl" x="300" y="110">"모르는 카드" 라고 답한다 — 지운 것과 같은 효과</text>

  <text x="40" y="145">삽입:</text>
  <text class="lbl" x="100" y="145">건드리지 않는다</text>
  <text class="lbl" x="300" y="145">이 슬롯을 새 키에게 준다 (stale_reuse++)</text>
</svg>
```

**왜 이게 이 문제에 맞나** (07번 주석의 논리):

- 만료된 항목이 남아 있어도 **틀린 답을 만들지 않는다**. anti-passback 창(3초)이
  retention(30초)보다 항상 작으므로 만료된 기록은 어차피 "중복 아님"으로 판정된다.
- 남겨 두는 비용은 **슬롯 하나**뿐이고, 슬롯을 원하는 코드는 삽입뿐이다. 삽입은 이미 그
  window를 걷고 있으므로 청소가 **공짜로** 얹힌다.
- sweeper 스레드는 반대다: 16 KB를 차가운 캐시로 훑고, 문 여는 경로가 쓰는 mutex를 잡고,
  아무도 안 보는 항목을 지우는 스레드를 하나 더 만든다. 문 4개짜리 컨트롤러에서는 순수한 jitter다.

**대가**: 만료된 슬롯이 같은 window 안의 빈 칸 **뒤에** 올 수 있다. 그래서 "첫 빈 칸에서
멈춘다"는 최적화를 **포기해야 한다** — 조회는 항상 16칸을 다 걷는다. 16칸은 상수이고 분기
예측이 잘 되는 루프라서, 톰스톤 없는 불변식을 사는 값으로 싸다.

09번은 같은 lazy 만료를 쓰지만 window가 없어서(테이블 전체를 탐사) **빈 칸에서 멈춘다.**

```c
/* 09_wifi_scan_snapshot/ap_table_solution.c */
if (!g_tab.slot[j].used)
    return -1;                    /* EMPTY terminates the probe chain */
```

가능한 이유는 09도 **재사용할 때 슬롯을 비우지 않고 덮어쓰기만** 하기 때문이다
(`used`는 true에서 false로 절대 안 간다).

---

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
|---|---|---|---|
| `used` 검사 없이 `id` 만 비교 | badge_id 0(또는 빈 BSSID)이 항상 "중복"으로 나온다 | 초기화된 슬롯의 `id`가 0이라 키 0과 구분이 안 된다 | `if (e->used && e->id == key)` 로 항상 `used`를 먼저 본다 |
| `(home + i)` 에 마스크를 안 씌움 | 테이블 끝 근처 키에서 가끔 crash 또는 옆 변수 손상 | 인덱스가 CAP을 넘어 배열 밖을 읽는다 | 매번 `(home + i) & MASK` |
| CAP을 2의 거듭제곱이 아닌 값으로 | 슬롯 절반만 쓰이고 충돌이 폭증한다 | `& (CAP-1)` 은 CAP이 2^n일 때만 `% CAP` 과 같다 | `_Static_assert((CAP & (CAP-1)) == 0)` 로 빌드에서 막는다 |
| 진짜 삭제(`used = false`) 후 "첫 빈 칸에서 멈춤" 조회 | 테이블에 있는 키를 못 찾는다. 재현이 어렵고 간헐적 | probe chain이 끊겨 뒤 항목이 미아가 된다 | 톰스톤 / backward shift / 삭제를 안 하는 lazy 만료 중 하나를 고른다 |
| 하위 비트가 상수인 키에 masking 또는 `곱셈 & MASK` | 평균 probe 17, eviction 카운터 폭주, anti-passback이 조용히 죽는다 | 홀수 곱셈 + 하위 비트 추출은 하위 비트 위의 1대1 대응이라 충돌 구조를 못 바꾼다 | 곱셈 결과의 **상위** 비트를 쓴다: `(id * 2654435761u) >> (32 - LOG2CAP)` |
| `now - t <= window` 를 순서 확인 없이 | 시계가 뒤로 점프하면 모든 조회가 "중복 아님" | `uint64_t` 뺄셈 underflow → 거대한 값 | `t <= now && now - t <= window`, 또는 `t + window >= now` 로 덧셈으로 쓴다 |
| 부하율을 0.9 넘게 운영 | 평균 지연이 갑자기 10배로 뛴다 | 선형 탐사 평균 probe는 `1/(1-α)^2` 로 폭발한다 | CAP = 예상 키 수 / 0.75, 2의 거듭제곱 올림. eviction 카운터를 내보낸다 |
| 테이블이 꽉 찼을 때 조용히 무시 | 필드에서 기능이 "가끔 안 된다"고 보고된다 | 실패가 어디에도 기록되지 않는다 | 거부하든 덮어쓰든 **카운터를 올리고 getter로 노출**한다 |

---

## 7. 손으로 확인하기

먼저 모범답안이 통과하는 것을 본다.

```sh
cd 07_badge_audit_dedupe && ./main.sh sol     # dedupe 해시맵 + per-door ring
cd 09_wifi_scan_snapshot && ./main.sh sol     # BSSID 해시맵 + top-k
```

그 다음 **내 해시맵을 brute force와 맞춰 본다.** 이게 이 노트에서 가장 중요한 연습이다.
brute force는 `id` 로 직접 인덱싱하는 큰 배열 — 느리지만 절대 안 틀린다.

```c
/* 참조 구현: 틀릴 수가 없게 만든다 */
static uint64_t ref_seen[NIDS];
static bool     ref_has[NIDS];

static int ref_is_dup(uint32_t id, uint64_t now, uint64_t win)
{
    if (!ref_has[id]) return 0;
    uint64_t t = ref_seen[id];
    return (t <= now && now - t <= win) ? 1 : 0;
}
```

`hash_check.c` 를 임시 폴더에 만들어 10000번의 랜덤 삽입/조회를 돌리고, 매 조회를 참조와
비교했다. 실제 출력이다.

```
test1 random ops: ops=10000 dup-hits=4411 mismatches=0 live=600 evict=0 reuse=0
test2 id & MASK : distinct home slots=   1  worst home load=600  inserts that had to evict a LIVE entry=584/600
test2 fibonacci : distinct home slots=   1  worst home load=600  inserts that had to evict a LIVE entry=584/600
test2 last byte : 300 BSSIDs (stride 16) ->  16/256 buckets used, worst bucket=19
test2 FNV-1a+fold: 300 BSSIDs (stride 16) -> 205/256 buckets used, worst bucket=4
test3 lazy expiry: 900 badges over 360s  live=175  stale_reuse=725  evictions=0  visible within 25s=62
ALL OK
```

읽는 법:

- `mismatches=0` — 600개 서로 다른 키(부하율 0.59)에서 내 맵과 참조가 10000번 전부 일치했다.
- `test2` 두 줄이 **똑같다**. 이게 §3에서 증명한 "하위 비트 곱셈은 충돌 구조를 못 바꾼다"의
  실측이다. 노트를 쓰면서 이 줄을 보고 설명을 고쳤다.
- `test3` — 900개 키(CAP의 0.88배)를 6분에 걸쳐 넣었는데 `evictions=0` 이고
  `stale_reuse=725` 다. 시간이 지나면서 죽은 슬롯을 계속 재활용했다는 뜻이다.
  살아 있는 항목을 밀어낸 일은 한 번도 없다.

**충돌을 일부러 만드는 법**도 반드시 연습한다. 버그는 충돌에서만 나온다.

- 해시를 임시로 `return id & 7;` 로 바꿔서 8칸에 강제로 몰아넣는다 — 위 `bug_demo` 처럼.
- 또는 `home + k*CAP` 형태의 키를 만든다: 어떤 해시든 `& MASK` 를 쓰면 같은 home이 된다.
- 삽입과 조회 사이에 시계를 retention 너머로 점프시켜서 만료 경로를 강제로 탄다.

- [ ] 07번 `audit_log.c` 에 해시맵을 직접 구현하고 `./main.sh` 로 전부 PASS
- [ ] 09번 `ap_table.c` 의 `wifi_lookup` 을 직접 구현
- [ ] brute force 비교 드라이버를 **보지 않고** 처음부터 써 보기
- [ ] `dedupe_home` 을 `return id & HMASK;` 로 바꿔 보고 eviction 카운터가 튀는 것 확인

---

## 8. 자가 점검

```check
Q: 용량을 2의 거듭제곱으로 고집하는 이유는? 1000칸으로 하면 무엇이 깨지나?
A: 인덱스를 `hash & (CAP-1)` 로 만들 수 있어서다. 나눗셈(`%`)은 코어에 따라 수십 사이클이고
아예 없는 칩도 있는데, AND는 1사이클이다. CAP=1000이면 `& 999` 는 `% 1000` 과 다르다 —
999는 2^n-1이 아니므로 0..999를 고르게 덮지 않고 이상한 부분집합만 가리킨다.
`_Static_assert((CAP & (CAP-1)) == 0)` 로 빌드 시점에 막는다.

Q: 오픈 어드레싱에서 항목을 그냥 지우면(`used = false`) 무슨 일이 생기나? 왜 재현이 어렵나?
A: 지운 칸 뒤에 있던 항목이 미아가 된다. 그 항목은 "지운 칸을 지나서" 그 자리에 놓인
것이므로, 조회가 빈 칸에서 멈추면 못 찾는다. 재현이 어려운 이유는 충돌이 있었던
키에서만 나타나기 때문이다. 충돌이 없으면 삭제가 아무 문제를 안 일으킨다.

Q: 이 저장소는 왜 톰스톤을 안 쓰나? 그 대가로 무엇을 포기했나?
A: 톰스톤은 쌓이고, 쌓인 톰스톤은 rehash로만 없앨 수 있는데 malloc이 없으니 rehash를
할 수 없다. 그래서 아예 삭제를 하지 않고, 만료된 슬롯을 삽입이 그 자리에서 재사용한다
(slot은 empty → occupied 로 한 번만 간다). 대가는 "첫 빈 칸에서 조회를 멈추는" 최적화다 —
만료 슬롯이 빈 칸 뒤에 올 수 있어서 07번은 window 16칸을 항상 다 걷는다.

Q: `(id * 2654435761u) & 1023` 은 `id & 1023` 보다 무엇을 개선하고 무엇을 개선하지 못하나?
A: 클러스터를 깬다. 연속된 id가 433칸씩 벌어져서, 테이블이 68% 찬 뒤에 온 키의 평균 probe가
260칸에서 3칸으로 줄었다(실측). 개선하지 못하는 것은 충돌 자체다 — 홀수 곱셈 후 하위 비트
추출은 하위 비트 위의 1대1 대응이라, 하위 비트가 같은 두 키는 곱한 뒤에도 같은 home으로
간다. 시설 코드가 하위 10비트에 박힌 키 600개는 masking과 똑같이 584번 eviction을 일으켰다.
고치려면 곱한 결과의 상위 비트를 쓴다.

Q: FNV-1a 끝의 `h ^= h >> 16` 은 무엇을 하나? 없으면 어떻게 되나?
A: xor fold다. 우리는 `h & (CAP-1)` 로 **하위** 비트만 쓰는데 FNV는 상위 비트가 더 잘
섞여 있다. 상위 16비트를 하위로 접어 내려 섞어 준다. 없으면 한 벤더가 연속(또는 stride)으로
발급한 BSSID들이 몇 개 버킷으로 뭉친다. 실측으로 "마지막 바이트만 쓰기"는 256칸 중 16칸만,
FNV-1a+fold는 205칸을 썼다.

Q: 테이블이 다 찼을 때 덮어쓰기와 거부 중 어느 쪽이 맞나?
A: 데이터의 의미에 달렸다. 07번의 dedupe는 오래된 항목을 덮어쓴다 — 가장 오래 못 본 카드는
anti-passback 창이 거의 끝난 카드라서 잃어도 손해가 가장 작다. 09번의 AP 테이블은 거부한다 —
살아 있는 AP를 임의로 버리면 roaming이 잘못된 판단을 하니, 정직하게 실패하는 편이 낫다.
공통점은 **어느 쪽이든 카운터를 올리고 getter로 노출한다**는 것이다. 조용한 실패가 최악이다.

Q: 부하율이 0.75를 넘으면 무슨 일이 생기나? malloc 없이 어떻게 대응하나?
A: 선형 탐사의 평균 probe 길이는 대략 `1/(1-α)^2` 로 늘어난다. 0.5에서 2.5칸, 0.75에서
8.5칸, 0.9에서 50칸 — 0.9 근처가 절벽이다. rehash가 불가능하므로 대응은 런타임이 아니라
설계 시점이다: `CAP = 예상 동시 키 수 / 0.75` 를 2의 거듭제곱으로 올림. 그리고 런타임에는
eviction/drop 카운터를 내보내서 "이 유닛은 테이블이 작다"를 대시보드가 보게 한다.
```

---

## 9. 요약 카드

- 오픈 어드레싱 = **배열 하나**. malloc 없음, 캐시 친화적, 삭제만 어렵다.
- `CAP` 은 2의 거듭제곱, 인덱스는 `hash & (CAP-1)`, `_Static_assert` 로 못 박는다.
- 정수 키는 **큰 홀수 곱셈**, 바이트열 키는 **FNV-1a + xor fold**.
- 하위 비트가 상수/stride인 키에는 `곱셈 & MASK` 가 masking과 **똑같이** 무력하다 →
  곱셈 결과의 **상위** 비트를 써라.
- 슬롯의 위치는 "그 앞에 무엇이 있었나"의 역사다. 앞을 비우면 뒤가 미아가 된다.
- 삭제 3종: 톰스톤(쌓인다, rehash 필요) / backward shift(O(probe), 락이 넓어진다) /
  **시간 기반 lazy 만료**(지우지 않는다 — 이 저장소의 선택).
- 희생자 우선순위: **만료된 슬롯 → 빈 슬롯 → 가장 오래된 살아 있는 슬롯**.
- `CAP = 예상 키 수 / 0.75`, 그 위는 성능 절벽. 실패는 **카운터로 내보낸다**.
- 테스트는 언제나 brute force 비교 + **충돌을 일부러 만들기**.
