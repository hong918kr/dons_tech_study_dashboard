---
id: DS-A07
time: 110분
updated: 2026-11-08
---
# DS-A07 Cuckoo 해싱: 최악 O(1) 조회

## 학습 목표

- 두 테이블 · 두 해시 · 버킷당 1칸 cuckoo 해시를 구현하고, 불변식 **"모든 키는 자기 두 슬롯 중 하나에 있다"** 가 조회를 **항상 슬롯 2칸 비교**로 끝내는 이유를 설명한다. 100만 번 조회로 탐사 수 분포를 확인한다.
- 키를 그래프의 **간선**, 슬롯을 **정점**으로 보고(cuckoo graph), "연결 성분의 간선 수 ≤ 정점 수이면 배치 가능"을 이용해 어떤 삽입이 **구조적으로 불가능**한지 판정한다. 고전 축출 사슬이 왜 사이클을 **한 번 돌고도 성공**할 수 있는지 추적한다.
- 읽기 전용 경로 탐색 + **Brent 사이클 감지**로 삽입 불가능을 표를 건드리지 않고 판정한다. union-find 오라클과 대조해 판정이 정확함을 보인다. 실패 시 **새 시드로 예비 버퍼에 rebuild** 한다(malloc 없음).
- 경로를 찾은 뒤 **뒤에서부터 옮겨서** 어느 순간에도 키가 사라지지 않게 한다. 슬롯 쓰기마다 끼어드는 ISR 시뮬레이터로 "태스크가 갱신 중이어도 ISR 조회는 모든 키를 찾는다"를 검사한다.
- 부하율 한계(≈ 50%)와 축출 사슬 분포를 시드 수백 개로 **직접 측정**한다. "조회는 O(1)이지만 삽입은 아니다"를 숫자로 말한다.

## 1. 문제

"인터럽트 안에서 조회합니다. 최악 조회 시간을 몇 사이클로 묶을 수 있죠?" 해시 테이블을 쓴다고 하면 시니어 면접은 곧바로 최악을 묻는다. DS-M08의 Robin Hood는 분산을 줄이지만, 탐사 길이 상한은 여전히 부하율과 키 집합에 달려 있다. **Cuckoo 해싱은 키가 있을 수 있는 자리를 처음부터 딱 2개로 제한**한다. 그래서 조회는 구조적으로 2칸이다. 대가는 삽입 쪽으로 옮겨 간다: 자리가 차 있으면 먼저 온 키를 내쫓아야 하고(뻐꾸기처럼), 드물지만 전체를 다시 지어야 한다.

펌웨어에서 이 구조가 맞는 곳은 "조회는 ISR이나 패킷 경로에서 아주 자주, 갱신은 태스크에서 가끔"인 표다. CAN 29비트 ID → 핸들러, 플로우 ID → 큐, NPU 태스크 ID → 디스크립터 같은 것이다. ID 공간이 너무 커서 직접 인덱스 배열을 못 쓰고, 정렬 배열의 이진 탐색은 `log n`번 분기하며, 최악을 박아야 할 때 쓴다.

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">두 테이블, 키마다 자리 2개. 키 표기 (T0 인덱스, T1 인덱스).  a=(1,2) b=(1,5) c=(3,2) d=(3,5)</text>
  <text x="60" y="48" fill="currentColor">삽입 전 (a, b, c 가 있다)</text>
  <text x="440" y="48" fill="currentColor">d 삽입 후: 축출 3번</text>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="60" y="72">T0</text><text x="170" y="72">T1</text>
    <text x="440" y="72">T0</text><text x="550" y="72">T1</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="30" y="80" width="60" height="28"/><rect x="30" y="108" width="60" height="28"/><rect x="30" y="136" width="60" height="28"/><rect x="30" y="164" width="60" height="28"/><rect x="30" y="192" width="60" height="28"/><rect x="30" y="220" width="60" height="28"/>
    <rect x="140" y="80" width="60" height="28"/><rect x="140" y="108" width="60" height="28"/><rect x="140" y="136" width="60" height="28"/><rect x="140" y="164" width="60" height="28"/><rect x="140" y="192" width="60" height="28"/><rect x="140" y="220" width="60" height="28"/>
    <rect x="410" y="80" width="60" height="28"/><rect x="410" y="108" width="60" height="28"/><rect x="410" y="136" width="60" height="28"/><rect x="410" y="164" width="60" height="28"/><rect x="410" y="192" width="60" height="28"/><rect x="410" y="220" width="60" height="28"/>
    <rect x="520" y="80" width="60" height="28"/><rect x="520" y="108" width="60" height="28"/><rect x="520" y="136" width="60" height="28"/><rect x="520" y="164" width="60" height="28"/><rect x="520" y="192" width="60" height="28"/><rect x="520" y="220" width="60" height="28"/>
  </g>
  <rect x="30" y="108" width="60" height="28" class="f-mut"/>
  <rect x="30" y="164" width="60" height="28" class="f-mut"/>
  <rect x="140" y="136" width="60" height="28" class="f-mut"/>
  <rect x="410" y="108" width="60" height="28" class="f-ok"/>
  <rect x="410" y="164" width="60" height="28" class="f-acc"/>
  <rect x="520" y="136" width="60" height="28" class="f-ok"/>
  <rect x="520" y="220" width="60" height="28" class="f-ok"/>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="60" y="127">b</text><text x="60" y="183">c</text><text x="170" y="155">a</text>
    <text x="440" y="127">a</text><text x="440" y="183">d</text><text x="550" y="155">c</text><text x="550" y="239">b</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="12" y="127">1</text><text x="12" y="183">3</text><text x="212" y="155">2</text><text x="212" y="239">5</text>
    <text x="392" y="127">1</text><text x="392" y="183">3</text><text x="592" y="155">2</text><text x="592" y="239">5</text>
  </g>
  <g fill="currentColor">
    <text x="240" y="100">조회 d: T0[3] ?  T1[5] ?</text>
    <text x="240" y="120">이 두 칸만 보고 끝.</text>
    <text x="240" y="160">d → T0[3], c 쫓겨남</text>
    <text x="240" y="178">c → T1[2], a 쫓겨남</text>
    <text x="240" y="196">a → T0[1], b 쫓겨남</text>
    <text x="240" y="214">b → T1[5], 빈칸 도착</text>
    <text x="10" y="276">불변식: 표의 모든 키 k 는 슬롯 home0(k) 또는 home1(k) 에 있다. 다른 어디에도 없다.</text>
    <text x="10" y="298">그래서 조회는 2칸 비교로 끝나고, 지울 때 묘비(tombstone)도 필요 없다.</text>
    <text x="10" y="320">대가: 삽입이 사슬을 타고, 사슬이 안 끝나면 전체를 다시 지어야 한다. 부하율은 약 50% 까지다.</text>
  </g>
</svg>
```

| 파트 | 구현 | 체크 (결정적) | 관찰 데모 |
|---|---|---|---|
| A | `ck_locate`, `ck_del`, `ck_put_walk` (고전 사슬) | 손으로 만든 충돌 추적, 사이클 후 복귀, 예산 초과 시 키 보존, 100만 연산 vs 참조 모델 | — |
| B | `ck_walk` (Brent), `ck_put` (경로 우선), `ck_rebuild` | 오라클과 일치, 실패 시 표 불변, 새 시드 rebuild | `demo_load.c` |
| C | `map_lookup/put/del` (뱅크 2개) | 슬롯 쓰기마다 ISR 끼어들기, 고전 사슬의 빈틈 재현, 100만 조회 | `demo_bench.c`, `demo_asm.c` |

### Part A — 고전 삽입: 축출 사슬

- 표는 `slots[2*cap]` 하나다. `[0, cap)`가 T0, `[cap, 2cap)`이 T1이고, 슬롯 번호 하나가 테이블과 인덱스를 함께 나타낸다. `CK_EMPTY = 0xFFFFFFFF`가 빈 칸이다(키로 못 쓴다).
- `ck_locate(t, key, &probes)`: 두 슬롯만 비교한다. `ck_del`: 찾아서 비운다.
- `ck_put_walk`: T0에 놓고, 쫓겨난 키를 반대쪽으로 보낸다. `limit`번 쫓아내도 끝나지 않으면 `CK_BUDGET`을 돌려주고 손에 든 키를 `*homeless`로 내놓는다.
- 체크: 그림의 a, b, c, d 시나리오, 사이클을 돌고 되돌아오는 시나리오, 슬롯 쌍이 같은 키 3개(불가능)에서 키를 잃지 않는지, 100만 연산 vs 참조 모델.

### Part B — 경로 우선 삽입, 사이클 감지, 재해시

- `ck_walk`: 슬롯에서 시작해 "점유자를 반대쪽으로 보내면 어디로 가나"를 **읽기만** 하며 따라간다. Brent 알고리즘으로 O(1) 메모리로 사이클을 찾는다.
- `ck_put`: 두 슬롯 중 빈 곳이 있으면 바로 넣고, 아니면 T0 쪽 walk, 안 되면 T1 쪽 walk. 빈 칸 쪽 끝에서부터 뒤로 옮긴다. **실패하면 표를 한 바이트도 안 바꾼다.**
- `ck_rebuild(dst, src, extra, s0, s1)`: 예비 버퍼에 새 시드로 다시 넣는다. `src`는 건드리지 않는다.
- 체크: `ck_put`의 `CK_CYCLE` 판정이 union-find 오라클과 **정확히 일치**하는지 2만 번(cap 16), 표 불변(memcmp), 경로 예산에 따른 방향 선택, 시드 교체 rebuild.

### Part C — ISR 조회 + 태스크 갱신

- `ck_map_t`: 뱅크 2개 + `_Atomic cur`. ISR은 `cur`를 한 번 읽고 그 뱅크에서만 조회한다. 태스크는 활성 뱅크에 넣고, 실패하면 예비 뱅크에 rebuild한 뒤 `cur` 한 워드로 커밋한다.
- 체크: 슬롯을 쓸 때마다 ISR을 흉내 내어 살아 있는 키를 **전부** 조회한다. 고전 사슬은 키 하나가 사라지는 순간이 있고(missing == 1), 경로 우선은 없다(missing == 0). rehash 중에도 전부 찾아야 한다.
- 측정: 100만 조회의 탐사 수 분포, 부하율 한계, 축출 사슬 분포(demo_load.c), 시간(demo_bench.c).

## 2. Problem (English)

Implement a cuckoo hash table: two tables, two seeded hash functions, one slot per bucket, no dynamic allocation.

```c
ck_status_t ck_put_walk(ck_t *t, uint32_t key, uint32_t val, uint32_t *kicks, ck_slot_t *homeless);
int32_t     ck_locate(const ck_t *t, uint32_t key, uint32_t *probes);
bool        ck_del(ck_t *t, uint32_t key);
walk_t      ck_walk(const ck_t *t, uint32_t start, uint32_t *path, uint32_t *len);
ck_status_t ck_put(ck_t *t, uint32_t key, uint32_t val, uint32_t *kicks);
ck_status_t ck_rebuild(ck_t *dst, const ck_t *src, const ck_slot_t *extra, uint32_t s0, uint32_t s1);
bool        map_lookup(ck_map_t *m, uint32_t key, uint32_t *val, uint32_t *probes);
ck_status_t map_put(ck_map_t *m, uint32_t key, uint32_t val);
```

Requirements: (1) `ck_locate` inspects at most two slots, always. (2) A failed `ck_put` leaves the table byte-identical. (3) Report `CK_CYCLE` exactly when the key cannot be placed under the current hash functions, as judged by the components of the cuckoo graph (edges ≤ vertices in every component). (4) `ck_rebuild` must never modify `src`. (5) A reader that runs between any two slot writes of `ck_put` or `map_put` must find every key that was present before the call. (6) Reject the reserved key `CK_EMPTY`; never match it against an empty slot.

Measure the probe count of 1M lookups at load 0.45, the first-failure load factor over many seeds, and the eviction chain length distribution. Example: with `h0 = key & 0xFF` and `h1 = (key >> 8) & 0xFF`, inserting keys `0x0201`, `0x0501`, `0x0203`, `0x0503` in order with the classic walk evicts 0, 1, 0, and 3 keys.

## 3. 시작 코드

TODO는 9개다: `ck_locate`, `ck_del`, `ck_put_walk`, `ck_walk`, `ck_put`, `ck_rebuild`, `map_lookup`, `map_put`, `map_del`. 하네스(불변식 검사기, 참조 모델, union-find 오라클, ISR 시뮬레이터)는 그대로 둔다.

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크가 깨져 있을 때
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter   # 20/103 passed (FAIL 83개, exit 1)
```

스텁만 있어도 통과하는 20개는 불변식 검사기 자가 점검과, 스텁이 우연히 맞히는 "실패해야 하는" 체크다.

## 4. 힌트

<details><summary>힌트 1 — 슬롯 번호 하나로 다 표현하기</summary>

`s < cap`이면 T0, 아니면 T1이다. 어떤 키가 슬롯 `s`에 있을 때 "반대쪽"은 `ck_other(t, s, key)`, 즉 다른 테이블의 home이다. 두 슬롯은 항상 다른 테이블에 있으므로 자기 자신을 가리킬 일이 없다(cuckoo 그래프는 이분 그래프다).
</details>

<details><summary>힌트 2 — ck_locate 의 함정</summary>

`CK_EMPTY`를 찾는다고 하자. 빈 슬롯의 키가 바로 `CK_EMPTY`이니, 비교가 **성공**해 버린다. 슬롯을 읽기 전에 거른다. 예외 없이 이 경우 probes는 0이다.
</details>

<details><summary>힌트 3 — 고전 삽입에서 실패 시 키 보존</summary>

T0[s]에 `carry`를 쓰기 전에 원래 값을 `old`에 받아 둔다. 그래야 `old`가 빈 칸인지 알 수 있고, 예산이 끝났을 때 `*homeless = old`로 내놓을 수 있다. 이 시점에 표에는 `n`개가 그대로 있고, 집 없는 키가 1개 있다. `n`은 **늘리지 않는다**.
</details>

<details><summary>힌트 4 — Brent</summary>

`tortoise`를 `start`로 두고 한 걸음마다 `lam`을 센다. 다음 슬롯이 `tortoise`면 사이클이다. `lam`이 `power`에 닿으면 `tortoise`를 현재 슬롯으로 옮기고 `power *= 2`, `lam = 0`이다. 경로 배열을 넘기 전에 예산 검사를 먼저 해야 스택을 안 넘는다.
</details>

<details><summary>힌트 5 — 왜 T1 쪽 walk 가 따로 필요한가</summary>

T0 쪽 walk가 사이클로 끝나도, 키의 다른 집 T1 쪽에는 빈 칸으로 가는 길이 있을 수 있다. 읽기 전용 walk는 함수형 그래프를 따라갈 뿐이라 "되돌아서 반대편으로 가기"를 스스로 하지 않는다. 그래서 T1 쪽에서 한 번 더 시작한다. 둘 다 사이클이면 그 키는 이 해시 함수로는 들어갈 자리가 없다. 정확히 그때만 `CK_CYCLE`이다.
</details>

<details><summary>힌트 6 — 뒤에서부터 옮기기</summary>

경로가 `path[0], path[1], ..., path[len-1](빈 칸)`이면, `path[len-1] ← path[len-2]의 점유자`부터 시작해 `path[0]`을 마지막에 쓴다. 옮겨진 키는 잠깐 **두 슬롯에 동시에** 있지만, 어느 순간에도 **어딘가에는** 있다.
</details>

## 5. 풀이

### Part A — 조회가 왜 2칸인가, 고전 삽입

조회는 코드가 짧다.

```c
static int32_t ck_locate(const ck_t *t, uint32_t key, uint32_t *probes) {
    if (key == CK_EMPTY) return -1;
    uint32_t s = ck_home(t, 0, key);
    (*probes)++;
    if (t->slots[s].key == key) return (int32_t)s;
    s = ck_home(t, 1, key);
    (*probes)++;
    if (t->slots[s].key == key) return (int32_t)s;
    return -1;
}
```

이 코드에는 **반복문이 없다**. 2칸이라는 말은 "평균이 2"가 아니라 "구조상 3칸째를 볼 방법이 없다"이다. Robin Hood나 선형 탐사는 탐사 길이가 표 상태에 따라 달라지는데(DS-M08에서 최대 탐사 길이를 쟀다), cuckoo는 표 상태와 무관하게 같다. 맞바꾼 것은 삽입이다.

| 삽입 단계 | 상태 |
|---|---|
| ① `T0[h0(key)]`에 놓는다 | 전 점유자가 쫓겨난다 |
| ② 쫓겨난 키를 **자기 다른 슬롯**으로 보낸다 | 거기 있던 키가 또 쫓겨난다 |
| ③ 반복 | 빈 칸을 만나면 끝. `limit`번 넘으면 포기 |

위 그림의 d 삽입이 정확히 이 순서다. 하네스가 슬롯까지 확인한다. 3번 축출 후 `T0[3]=d, T1[2]=c, T0[1]=a, T1[5]=b`다.

**핵심: 사슬이 사이클을 돌았다고 끝난 게 아니다.** 하네스의 두 번째 시나리오는 이렇다. p, q가 슬롯 쌍 (T0[1], T1[2])을 둘 다 차지하고(정점 2개에 간선 2개, 이미 꽉 찬 사이클), r, s가 (T0[8], T1[3])에 있다. 이제 x=(1,7)을 넣는다.

| 단계 | 동작 | 쫓겨남 |
|---|---|---|
| 1 | x → T0[1] | q 쫓겨남 (1번째 축출) |
| 2 | q → T1[2] | p 쫓겨남 (2번째) |
| 3 | p → T0[1] | **x 쫓겨남** (3번째) |
| 4 | x → T1[7] | 빈 칸, 끝 |

사슬은 자기 자신을 내쫓고, 반대편(T1)으로 나가서 성공했다. Pagh와 Rodler 논문이 이 경우를 그대로 설명한다. 처음으로 다시 밟은 칸의 키부터 x2까지 원래 자리로 되돌아가고, x1은 T2 쪽으로 시도하며, 그 뒤 빈 칸을 만나거나 다시 밟은 칸을 만나면 "closed loop"(l−i+1개 키가 l−i칸을 나눠 쓰는 상황)이라 재해시가 필요하다고 한다. 그러니 **"같은 칸을 다시 봤다 = 실패"는 틀린 판정**이다(§7의 실수 2).

고전 삽입에서 예산이 끝나면 `*homeless`로 키를 내놓는다. 표에는 키 n개가 있고 집 없는 키가 하나 있으니, **키는 하나도 사라지지 않았다**. 다만 그 키는 처음 넣으려던 키와 다를 수 있다.

### Part B — 그래프로 보면 "불가능"이 정확해진다

슬롯을 **정점**, 키를 그 키의 두 슬롯을 잇는 **간선**으로 그리면 cuckoo 그래프가 된다. 키를 놓는다는 것은 간선을 자기 끝점 중 하나에 **방향을 정해 놓는다**는 뜻이고, 각 정점은 많아야 간선 하나를 받는다.

- 연결 성분에서 **간선 수 ≤ 정점 수**이면 배치할 수 있다(그런 경우 그 성분은 트리거나 사이클이 하나인 그래프다).
- 간선 수 > 정점 수이면 비둘기집 원리로 불가능하다.

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Cuckoo 그래프: 정점 = 슬롯, 간선 = 키. 연결 성분마다 간선 ≤ 정점이면 배치 가능</text>
  <g stroke="currentColor" fill="none" stroke-width="1.5">
    <path d="M60 110 L160 110"/><path d="M60 110 L60 200"/><path d="M160 110 L160 200"/>
    <path d="M310 110 Q360 70 410 110"/><path d="M310 110 Q360 150 410 110"/><path d="M410 110 L410 200"/>
    <path d="M560 110 Q610 70 660 110"/><path d="M560 110 Q610 150 660 110"/><path d="M560 110 L610 200"/><path d="M660 110 L610 200"/>
  </g>
  <circle cx="60" cy="110" r="15" class="f-mut" stroke="currentColor"/><circle cx="160" cy="110" r="15" class="f-mut" stroke="currentColor"/><circle cx="60" cy="200" r="15" class="f-mut" stroke="currentColor"/><circle cx="160" cy="200" r="15" class="f-ok" stroke="currentColor"/>
  <circle cx="310" cy="110" r="15" class="f-mut" stroke="currentColor"/><circle cx="410" cy="110" r="15" class="f-mut" stroke="currentColor"/><circle cx="410" cy="200" r="15" class="f-mut" stroke="currentColor"/>
  <circle cx="560" cy="110" r="15" class="f-warn" stroke="currentColor"/><circle cx="660" cy="110" r="15" class="f-warn" stroke="currentColor"/><circle cx="610" cy="200" r="15" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle">
    <text x="110" y="52" font-weight="bold">트리</text>
    <text x="360" y="52" font-weight="bold">사이클 1개</text>
    <text x="610" y="52" font-weight="bold">사이클 2개 (이상)</text>
    <text x="110" y="240">정점 4, 간선 3</text>
    <text x="360" y="240">정점 3, 간선 3</text>
    <text x="610" y="240">정점 3, 간선 4</text>
    <text x="110" y="262">빈 칸 1개로 모두 닿는다</text>
    <text x="360" y="262">빈 칸 없음, 새 키 불가</text>
    <text x="610" y="262">불가능: walk 끝없음</text>
  </g>
  <text x="10" y="296" fill="currentColor">슬롯 쌍이 같은 키 3개(Part A, B의 불가능 예)는 간선 3개가 정점 2개 사이에 있는 가장 작은 경우다.</text>
</svg>
```

**읽기 전용 walk가 정확한 이유.** 표가 고정되어 있으면 "슬롯 s → 점유자의 반대쪽 슬롯"은 함수다(슬롯마다 점유자가 많아야 하나이므로 나가는 화살표가 하나다). 그래서 walk는 빈 슬롯에 닿거나 사이클에 들어간다. 키 k의 두 슬롯 a, b에 대해 네 경우를 나눈다.

| a 쪽 성분 | b 쪽 성분 | 결과 |
|---|---|---|
| 트리 (빈 칸 있음) | 무엇이든 | a 쪽 walk가 빈 칸에 닿는다 |
| 사이클 1개 (꽉 참) | 트리 | a 쪽은 사이클, **b 쪽 walk가 성공** |
| 사이클 1개 | 사이클 1개 (같은 성분 포함) | 둘 다 사이클 → 간선 > 정점 → **진짜 불가능** |

트리 성분은 정점이 간선보다 1개 많으니 빈 칸이 정확히 1개다. 그러므로 **두 방향 walk 둘 다 사이클이면 불가능**이고, 그렇지 않으면 경로가 있다. `CK_CYCLE`은 "운이 없었다"가 아니라 "이 시드로는 방법이 없다"이다.

**Brent를 쓰는 이유.** walk의 상태는 슬롯 번호 하나다. `tortoise`와 `power`만 두면 O(1) 메모리로 사이클을 찾는다. 방문 비트맵을 쓰면 슬롯마다 1비트에 walk마다 초기화(O(cap) 또는 복잡한 세대 카운터)가 든다. Brent는 `start`에서 사이클 진입까지 μ, 사이클 길이 λ일 때 대략 3·max(μ, λ) 걸음 안에 잡는다. 나는 path 배열을 별도로 두고 예산 `limit`으로 잘랐다. 빈 칸에 닿을 경로는 길어야 `n+1`칸이다.

**경로를 읽기만 하고 나중에 옮기는 이유.** 실패가 표를 전혀 건드리지 않는다. 그러면 `homeless`도, 되돌리기(rollback)도 없다. 고전 삽입의 "실패 시 키를 어디 둘까" 문제가 사라진다.

```c
uint32_t path[CK_MAX_PATH + 1], len = 0;
walk_t r0 = ck_walk(t, s[0], path, &len), r = r0;
if (r0 != WALK_FOUND) r = ck_walk(t, s[1], path, &len);
if (r != WALK_FOUND)
    return (r0 == WALK_CYCLE && r == WALK_CYCLE) ? CK_CYCLE : CK_BUDGET;
```

`CK_MAX_PATH`는 스택 크기이자 **삽입 WCET 예산**이다. 예산을 넘어 포기한 경우는 `CK_BUDGET`이고 "더 긴 경로가 있었을 수도 있다"는 뜻이다. 사이클이면 `CK_CYCLE`이다. 둘 다 rebuild로 이어지지만, 원인이 다르다.

**오라클 검증.** union-find로 cuckoo 그래프를 만들고 성분의 간선 수와 정점 수를 비교한 값을 `ck_put`의 결과와 대조했다. cap=16(슬롯 32개)에서 시드 2만 개:

```text
oracle: ok=534866 cycle=99752 t1_rescue=7352 max_kicks=17
```

63만 번 넘는 판정에서 불일치가 0이다. `t1_rescue=7352`는 T0 쪽이 사이클이었는데 T1 쪽에서 성공한 횟수이고, 이걸 빼먹는 버그는 오라클이 잡는다(주입 테스트 `no_t1_fallback`).

**Rebuild.** 시드를 바꾸면 그래프가 통째로 바뀐다. 이 레슨의 전략은 "예비 버퍼에 다시 짓기"다.

| 전략 | 추가 RAM | 실패 시 | 재빌드 중 조회 |
|---|---|---|---|
| **예비 버퍼 rebuild (본문)** | 2·cap 슬롯 | 원본 그대로 → 안전 | ISR이 옛 뱅크를 계속 읽음 |
| 제자리 재시드 (연습 1) | 없음 | 일부 키가 옛 위치에 섞여 있음, 조회 불가 | 불가능 (정지 구간) |
| 1칸 stash, 버킷화 | 조금 | 첫 실패를 뒤로 미룸 | NETHW-M05 |

Pagh–Rodler 논문은 새 테이블 없이 표를 훑어 제자리에 없는 키를 다시 삽입하는 방법도 언급한다(연습 1). 다만 시도가 실패했을 때 표가 절반쯤 바뀐 상태로 남고, 그동안 ISR이 조회를 못 한다. 그래서 ISR이 읽는 표에는 예비 버퍼를 썼다. 여기서 메모리와 안전을 맞바꾼다.

### Part C — ISR이 읽는 동안 태스크가 옮긴다

고전 사슬은 쓰기 순서가 **키가 사라지는 순서**다. 한 번의 축출에서 T0[s]에 새 키를 쓰는 순간, 쫓겨난 키는 어느 슬롯에도 없다. 슬롯 쓰기 사이에 끼어든 ISR 조회는 표에 있어야 할 키를 **못 찾는다**(false miss). `ck_put`은 쓰기 순서를 뒤집어 이를 없앤다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">쓰기 순서가 전부다: 경로 x → a → b → (빈 칸)</text>
  <text x="10" y="50" fill="currentColor">고전 (앞에서부터)</text>
  <text x="400" y="50" fill="currentColor">경로 우선 (뒤에서부터)</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="62" width="64" height="26"/><rect x="84" y="62" width="64" height="26"/><rect x="148" y="62" width="64" height="26"/>
    <rect x="20" y="102" width="64" height="26"/><rect x="84" y="102" width="64" height="26"/><rect x="148" y="102" width="64" height="26"/>
    <rect x="20" y="142" width="64" height="26"/><rect x="84" y="142" width="64" height="26"/><rect x="148" y="142" width="64" height="26"/>
    <rect x="400" y="62" width="64" height="26"/><rect x="464" y="62" width="64" height="26"/><rect x="528" y="62" width="64" height="26"/>
    <rect x="400" y="102" width="64" height="26"/><rect x="464" y="102" width="64" height="26"/><rect x="528" y="102" width="64" height="26"/>
    <rect x="400" y="142" width="64" height="26"/><rect x="464" y="142" width="64" height="26"/><rect x="528" y="142" width="64" height="26"/>
    <rect x="400" y="182" width="64" height="26"/><rect x="464" y="182" width="64" height="26"/><rect x="528" y="182" width="64" height="26"/>
  </g>
  <rect x="20" y="102" width="64" height="26" class="f-warn"/>
  <rect x="84" y="142" width="64" height="26" class="f-warn"/>
  <rect x="528" y="102" width="64" height="26" class="f-ok"/>
  <rect x="464" y="142" width="64" height="26" class="f-ok"/>
  <rect x="400" y="182" width="64" height="26" class="f-acc"/>
  <g fill="currentColor" font-family="monospace" text-anchor="middle">
    <text x="52" y="80">a</text><text x="116" y="80">b</text><text x="180" y="80">.</text>
    <text x="52" y="120">x</text><text x="116" y="120">b</text><text x="180" y="120">.</text>
    <text x="52" y="160">x</text><text x="116" y="160">a</text><text x="180" y="160">.</text>
    <text x="432" y="80">a</text><text x="496" y="80">b</text><text x="560" y="80">.</text>
    <text x="432" y="120">a</text><text x="496" y="120">b</text><text x="560" y="120">b</text>
    <text x="432" y="160">a</text><text x="496" y="160">a</text><text x="560" y="160">b</text>
    <text x="432" y="200">x</text><text x="496" y="200">a</text><text x="560" y="200">b</text>
  </g>
  <g fill="currentColor">
    <text x="218" y="120">a 가 없다</text>
    <text x="218" y="160">b 가 없다</text>
    <text x="602" y="120">b 는 양쪽에</text>
    <text x="602" y="160">a 는 양쪽에</text>
    <text x="602" y="200">x 등장</text>
    <text x="10" y="236">고전 쪽 세 번째 쓰기(b → 빈 칸)가 끝나야 모두 다시 찾을 수 있다. 그 사이 ISR 은 키를 놓친다.</text>
    <text x="10" y="258">경로 우선은 언제나 모든 키가 최소 한 슬롯에 있고, 새 키 x 는 마지막 쓰기 한 번으로 나타난다.</text>
    <text x="10" y="282">중복 상태는 무해하다. 조회는 어느 쪽을 찾아도 같은 값을 본다.</text>
  </g>
</svg>
```

MemC3 논문(Fan, Andersen, Kaminsky, NSDI 2013)도 같은 이유로 경로 탐색과 실행을 분리하고 **역방향으로 옮긴다**. 고전 Insert에는 "floating" 상태의 희생 키가 항상 있어 false miss가 생기기 때문이다. 이 논문은 다중 reader/단일 writer를 위해 키별 버전 카운터까지 쓴다. 우리의 ISR 모델(단일 코어, ISR이 끝까지 실행)에서는 필요 없고, SMP에서는 필요하다(Q5).

**단일 코어 ISR에서 무엇이 필요한가.**

| 위험 | 대응 | 코드 위치 |
|---|---|---|
| 키가 잠깐 사라짐 | 뒤에서부터 복사 | `ck_put` |
| `{key, val}` 8바이트가 반쯤 쓰임 | 두 스토어를 인터럽트 금지로 감싼다 | `ck_store` 주석. 금지 구간은 스토어 2개 길이로 고정 |
| 뱅크 전환이 어중간하게 보임 | `cur`를 한 워드로, acquire/release | `map_lookup`, `map_put` |
| rebuild 동안 조회 | 예비 뱅크에 짓고 커밋 | `ck_rebuild` + `cur` |
| 뱅크 재사용 | ISR은 끝까지 실행되므로, 태스크로 돌아오면 옛 뱅크를 잡고 있는 조회가 없다 | SMP라면 RCU류 유예 기간이 필요 (Q5) |

8바이트 슬롯의 쓰기 원자성은 가정하지 않는다. Cortex-M에서 32비트 워드 두 개를 쓰는 것이고, 그 사이에 인터럽트가 들어올 수 있다. 그래서 `ck_store`의 몸체는 실제 타깃에서 `PRIMASK`/`BASEPRI`로 보호한다. 임계 구역은 슬롯 쓰기 하나의 길이로 **상수**다. 이것이 이 설계의 요점이다. 축출 사슬이 길어도 **ISR을 막는 시간은 슬롯 쓰기 하나로 고정**된다(태스크는 슬롯 쓰기 사이에서 얼마든지 선점될 수 있다).

슬롯 쓰기마다 끼어드는 ISR 시뮬레이터의 결과(스크립트 시나리오와 CAN ID 표 2만 연산):

```text
classic walk: isr calls=2 missing=1
path-first: status=0 kicks=1 isr calls=2 missing=0
map: puts=2460 dels=2216 n=244/512 rehashes=1 failed=0 isr_calls=20405 missing=0 maxp=2
```

고전 사슬은 정확히 한 번 키를 놓쳤고, 경로 우선은 0번이다. 2만 연산 동안 슬롯 쓰기 20405번마다 살아 있는 키 전부를 조회해도 놓친 키는 0개이고 최대 탐사는 2다(재해시가 1번 일어난 구간 포함).

### 데모 빌드

```bash
cc -std=c11 -O2 demo_load.c  -o demo_load  && ./demo_load    # 표 1~6 (작업량 계측, 결정적)
cc -std=c11 -O2 demo_bench.c -o demo_bench && ./demo_bench   # 조회/삽입 시간 (아래 표는 3회 실행)
cc -O2 -S -o - demo_asm.c                                    # 이 맥 arm64
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S -o - demo_asm.c   # Cortex-M4
```

### 측정 1: 조회는 정말 2칸인가

load 0.45(cap 16384 × 2, 키 14745개)에서 hit 50만 번과 miss 50만 번을 조회한 결과다.

```text
1M lookups at load 0.450: probes 1=329268 2=670732 >2=0, put_fail=0
```

탐사 3칸 이상은 **0번**이다. miss는 언제나 2칸이고, hit은 T0에서 찾는 비율이 65.9%(329268/500000)다. `CHECK(hist[3] == 0)`은 테스트이지 측정의 부산물이 아니다. 3칸째를 보도록 고친 뮤턴트(`third_probe`)는 실제로 FAIL한다.

Thumb-2 Cortex-M4 출력(`demo_asm.c`, clang -O2)을 보면 조회 함수에 **역방향 분기가 하나도 없다**. 명령 46개이고 분기는 앞으로만 간다(`bne .LBB0_3` 하나와 `itt eq` / `it ne` 조건 실행). arm64는 43개다. 사이클 수는 플래시 대기 상태와 캐시에 따라 달라지니 여기서 숫자를 약속하지 않지만, "최악 경로의 명령 수는 상수"는 어셈블리가 직접 보여 준다. 해시 2번, 시드 로드 2번, 키 비교용 로드 2번이 전부다.

### 측정 2: 부하율 한계 (`demo_load.c`, 작업량 계측이라 결정적)

키를 하나씩 넣다가 **첫 삽입 실패**가 나온 시점의 load = n/(2·cap)이다. 경로 예산 무제한, 시드 수백 개.

```text
[1] first-failure load, path budget unlimited (n/(2cap) when the first insert is impossible)
  cap(x2)  seeds     min     p5    median    p95     max
      256   2000  0.2578  0.4570  0.5703  0.6465  0.7305
     1024   2000  0.3857  0.4717  0.5415  0.5913  0.6411
     4096   1000  0.4000  0.4817  0.5259  0.5577  0.5872
    16384    300  0.3908  0.4841  0.5147  0.5335  0.5421
    65536     60  0.4891  0.4968  0.5090  0.5210  0.5239
```

중앙값은 cap이 커지면서 0.5 위쪽에서 내려온다. 0.5703 → 0.5415 → 0.5259 → 0.5147 → 0.5090이다. 표가 4배 커질 때마다 0.5와의 차이가 약 1.6~1.8배 줄어드는데, 이는 cap^(−1/3) 비율(4^(1/3) ≈ 1.59)에 가깝다. 고전적인 랜덤 그래프 결과인 임계 창(critical window) 폭 v^(2/3)과 맞아떨어진다. 이 레슨의 측정이 말하는 것은 그 경향뿐이고, 극한값 증명은 아니다.

이론 쪽 사실: Pagh–Rodler는 r ≥ (1+ε)n 조건에서 "a bit less than half full"이라고 쓰고, 해당 집합을 배치할 수 없을 확률을 O(1/n)이라고 인용한다. 표가 **딱 50%까지** 라는 뜻이 아니라, 50%는 무작위 그래프의 거대 성분이 생기는 문턱이다.

펌웨어 설계에서 중요한 숫자는 중앙값이 아니라 **밑꼬리**다.

```text
[3] P(reach load a without any failure), cap=4096x2, 1000 seeds, unlimited budget
  a=0.30   100.0%
  a=0.35   100.0%
  a=0.40   100.0%
  a=0.42    99.8%
  a=0.44    99.7%
  a=0.46    98.6%
  a=0.48    95.5%
  a=0.50    86.1%
  a=0.52    60.3%
  a=0.55    11.9%
```

첫 시도 성공 확률 99%를 원하면 cap 4096에서는 load를 **0.44 이하**로 잡는다. 표가 작으면 더 낮아진다. 표 1의 cap 256 행에서 최솟값 0.2578이 나온다. 작은 표일수록 운이 나쁘면 일찍 실패한다. 2·cap 슬롯 중 쓸 수 있는 몫이 약 절반이고, 두 뱅크까지 치면 키당 RAM이 **슬롯 약 4.4개 분**이다.

경로 예산을 제한하면 한계가 얼마나 내려가나:

```text
[2] first-failure load vs path budget K (max evictions), cap=16384x2, 100 seeds
     K   median     p5    p95
       4  0.3345  0.2674  0.3795
       8  0.4340  0.3696  0.4695
      16  0.4803  0.4445  0.5063
      32  0.5117  0.4830  0.5335
      64  0.5156  0.4859  0.5345
     128  0.5120  0.4825  0.5349
     512  0.5153  0.4793  0.5360
  262144  0.5156  0.4932  0.5314
```

이 표 크기에서 **K=32면 무제한과 거의 차이가 없다**(중앙값 0.5117 vs 0.5156). K=8은 0.434까지 내려간다. 즉 삽입 경로 예산을 32칸으로 묶어도 한계를 거의 잃지 않는다. 이 측정은 `CK_MAX_PATH`와 `limit`을 고르는 근거다.

### 측정 3: 축출 사슬 분포 (삽입은 O(1)이 아니다)

load 구간별로 **삽입 1번당** 축출 횟수를 쟀다(cap 16384, 시드 40개).

```text
[4] evictions per insert, by load window (cap=16384x2, 40 seeds, budget unlimited)
  window            scheme   inserts   P(0)   mean    p99  p99.9    max
  [0.20,0.25)  path-first    65520   96.7%   0.04      1      3      7
  [0.35,0.40)  path-first    65560   88.1%   0.23      5     11     25
  [0.43,0.45)  path-first    26200   82.8%   0.49      9     23     51
  [0.47,0.49)  path-first    26074   78.8%   0.88     17     43    115
  [0.20,0.25)     classic    65520   63.7%   0.45      3      7      9
  [0.35,0.40)     classic    65560   47.1%   1.01      9     15     25
  [0.43,0.45)     classic    26199   40.8%   1.66     15     33     83
  [0.47,0.49)     classic    24160   38.1%   2.72     33     73    131
```

평균은 1 미만이지만 max는 load에 따라 빠르게 자란다. `[0.47,0.49)`에서 평균 0.88인데 최악 115번이다. 경로 우선이 고전보다 평균·꼬리 모두 짧은 것은 양쪽 슬롯을 먼저 보고 쉬운 경우를 공짜로 끝내기 때문이다.

**솔직한 비용 요약(작업량).**

| 작업 | 슬롯 읽기 | 슬롯 쓰기 | 비고 |
|---|---|---|---|
| 조회 | 2 | 0 | 분기 2개, 반복 없음. **최악 상수** |
| 삭제 | 2 | 1 | 최악 상수 (묘비 없음) |
| 빈 슬롯 삽입 | 2 + 2 | 1 | |
| 축출 k번 삽입 | 대략 2 + 2k | k+1 | 표 4: k는 load 0.45에서 p99 = 9 |
| 불가능 판정 | 표 6 측정 | 0 | 표 불변 |
| rebuild | n + (재삽입 walk) | n + Σk | **O(n) 한 번**, 드물지만 있음 |

표 6은 불가능한 삽입 1건을 판정하는 비용(읽은 슬롯 수)이다.

```text
[6] cost of the first impossible insert, cap=16384x2, 300 seeds, Brent walk (slots read, both directions)
  trials 300  median 194  p95 430  max 862 slots read
```

고전 사슬은 같은 상황에서 예산 K까지 쓰기(`limit`번)를 하고, 그 후 키를 들고 빠져나온다. 경로 우선 + Brent는 읽기 몇백 번이면 판정이 끝나고 **쓰기는 0번**이다. 이 숫자가 "사이클 감지"가 단순 카운터보다 나은 실제 이유다. 카운터만 쓰면 사이클에서 예산을 끝까지 소모한다(둘 다 표를 망치진 않지만, 경로 우선은 쓰기 0으로 끝난다).

**쓸 수 있는 WCET 진술을 정리하면**: 조회와 삭제는 상수다. 삽입은 경로 예산 K로 슬롯 쓰기 수가 K+1로 묶인다(Part B). 다만 K로 실패하면 rebuild(O(n))가 필요하고, 그건 ISR이 아니라 **낮은 우선순위 태스크의 일**이다. 면접에서 "cuckoo 삽입은 O(1)인가요"에 대한 답은 "기대 상각 O(1)이고, 최악은 재빌드 포함 O(n)"이다. Pagh–Rodler의 분석은 삽입이 재해시를 일으킬 확률을 O(1/n²), 재해시의 n개 삽입이 모두 성공할 확률을 1 − O(1/n)으로 준다. 해시 함수 가정이 들어간 해석적 결과이고, 내가 실제로 잰 것은 위 표들이다.

### 측정 4: 해시 품질과 키 분포

```text
[5] first-failure load median, cap=16384x2, 60 seeds, unlimited budget
  hash / keys                        median    p5     p95
  fmix32 2 seeds, random keys        0.5160  0.4855  0.5324
  fmix32 2 seeds, sequential keys    0.5116  0.4845  0.5328
  fmix32 SAME seed, random keys      0.0348  0.0146  0.0549
  multiply-shift, random keys        0.5164  0.4886  0.5323
  multiply-shift, sequential keys    0.7731  0.3219  0.9193
  bit slices (no seed), random keys  0.0525  0.0194  0.5309
  bit slices (no seed), sequential   0.5001  0.0001  1.0000
```

- **같은 시드**: 두 슬롯이 `T0[i]`, `T1[i]`로 묶여 인덱스 i를 공유하는 키끼리만 경쟁한다. 그러면 표는 2-way 집합 연관 캐시와 같아져서 p5 = 0.0146에서 실패한다. `seed[0] != seed[1]` 확인은 빠뜨리면 안 된다.
- **시드 없는 비트 잘라 쓰기**: 펌웨어에서 흔한 "그냥 키 비트를 쓰자"는 무작위 키에서 중앙값 0.05로 무너진다. 순차 키에서는 중앙값이 0.5지만 p5가 0.0001이다. **rebuild로 시드를 바꿔도 소용없다.** 시드가 안 먹기 때문이다(하네스의 `h_stuck`이 그 경우다).
- **곱셈 시프트 + 순차 키**: 중앙값 0.77로 더 잘 나온다. 곱셈이 순차 키를 고르게 흩어 주기 때문이지만 p5 = 0.32다. 범위가 넓다는 건 믿을 수 없다는 뜻이다.
- 내 fmix32는 시드 XOR + Murmur3 finalizer이고, 무작위 키와 순차 키 모두 0.51~0.52다. 이 측정은 이 레슨의 키에 대한 것이고, 분포 일반에 대한 증명은 아니다.

### 시간 (`demo_bench.c`, Apple M6, -O2, 시간은 3회 실행 각 5회 측정의 중앙값 — 절댓값보다 비율을 볼 것)

같은 슬롯 수(같은 메모리), 같은 키. cuckoo는 load 0.45, 선형 탐사(DS-B11)는 load 0.45와 0.90이다.

| 슬롯 수 | 구조 | hit ns (3회 범위) | miss ns (3회 범위) | 최대 탐사 |
|---|---|---|---|---|
| 4096 (32 KiB) | cuckoo 0.45 | 2.43 (2.43–2.44) | 2.11 (2.09–2.14) | **2 (구조상 고정)** |
| | linear 0.45 | 2.34 (2.27–2.48) | 12.39 (10.64–12.42) | miss 17 |
| | linear 0.90 | 5.54 (4.38–7.61) | 48.28 (47.68–56.44) | miss 284 |
| 65536 (512 KiB) | cuckoo 0.45 | 7.88 (5.62–9.46) | 3.18 (2.79–4.73) | **2** |
| | linear 0.45 | 3.88 (3.43–5.26) | 21.09 (11.76–22.25) | miss 24 |
| | linear 0.90 | 20.58 (13.18–21.19) | 81.01 (55.68–87.02) | miss 838 |
| 1048576 (8 MiB) | cuckoo 0.45 | 11.26 (8.27–12.65) | 3.44 (3.37–6.54) | **2** |
| | linear 0.45 | 8.98 (7.79–13.82) | 18.85 (15.79–24.96) | miss 36 |
| | linear 0.90 | 27.81 (24.86–39.00) | 115.84 (104.23–175.40) | miss 1113 |

표에서 볼 것은 셋이다.

1. **hit 평균은 cuckoo가 이기지 않는다.** 슬롯 65536에서 cuckoo 7.88 ns vs 선형 3.88 ns다. T0와 T1이 무관한 두 곳이라 캐시 라인이 2개이고, 선형 탐사는 1개다. "cuckoo가 빠르다"고 말하면 안 된다. 쓰는 이유는 **상한**이다.
2. **miss에서 큰 차이**가 난다(이 맥에서 선형 0.45 대비 5.5~6.6배). miss가 흔한 경로(필터링, 패킷 드롭)에서 유리하다. hit보다 miss가 빠른 것은 "두 번 다 틀림"이 분기 예측기에 잘 맞기 때문으로 **추정**한다(측정한 건 시간뿐이고, 원인은 확인하지 않았다).
3. 런 간 산포가 크다(65536 슬롯의 cuckoo hit 5.62–9.46 ns). 스레드 고정이 없는 macOS와 E-core 이동의 결과일 수 있다. 이것이 **작업량(탐사 수)을 기본으로 보고** 시계는 보조로만 쓰는 이유다.

삽입 시간(슬롯 16384, load 0.45까지 채우는 과정의 마지막 20%, `mach_absolute_time` 틱 41.7 ns): p50은 0틱(대부분 한 틱 안에 끝남), p99는 3회 각각 83.3–125.0 ns, p99.9 166.7 ns, 최대값 중앙값 208–250 ns다. 그런데 런 내 최댓값의 범위 상단은 **5.8–26.7 ms**까지 튀었다. 삽입 작업량(max_kicks ≤ 31)으로는 설명이 안 되니 스케줄러나 인터럽트로 추정하지만, 이 계측으로는 원인을 분리할 수 없다. 그래서 이 표에서 WCET를 주장하지 않고, 앞의 **작업량 표(축출 횟수, 쓰기 횟수)** 로 말한다. 임베디드 타깃에서는 DWT `CYCCNT`로 재야 하고, 여기 숫자는 이 Mac의 것이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 103/103 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o s_san && ./s_san
```

전체 실행 < 0.4 s(-O2), ASan/UBSan 약 1.1 s이고 sanitizer 스케일링은 필요 없다. 100만 연산 랜덤 테스트의 불변식 전수 검사는 1024연산마다(K=1024) 하고, 작은 표(cap=16)와 Part B/C 쪽은 매 연산마다 한다. 실제 출력(PASS 줄 제외):

```text
-- 불변식 검사기 자가 점검 --
-- Part A: 고전 축출 사슬 --
   random 1M ops: n=624/2048, homeless=0, full checks=977, max_probes=2
   small table: 60000 ops, check every op, homeless=10644
-- Part B: 경로 우선 삽입과 사이클 감지 --
   oracle: ok=534866 cycle=99752 t1_rescue=7352 max_kicks=17
-- Part C: ISR 조회 + 태스크 갱신 --
   classic walk: isr calls=2 missing=1
   path-first: status=0 kicks=1 isr calls=2 missing=0
   map: puts=2460 dels=2216 n=244/512 rehashes=1 failed=0 isr_calls=20405 missing=0 maxp=2
   1M lookups at load 0.450: probes 1=329268 2=670732 >2=0, put_fail=0
   first-failure load (cap=1024x2, 30 seeds): min 0.444 median 0.543 max 0.600
103/103 passed
```

하네스의 테스트가 진짜인지 뮤턴트 10개로 확인했다(개인 사본에서). 전부 FAIL로 잡혔다.

| 뮤턴트 | 결과 |
|---|---|
| Brent의 `tortoise` 갱신 제거 | 102/103 |
| T1 쪽 walk 제거 | 95/103 |
| 경로를 앞에서부터 복사 | 92/103 |
| 새 키를 먼저 쓴 뒤 뒤로 복사 | 99/103 |
| rebuild에서 시드 안 바꿈 | 95/103 |
| `CK_EMPTY` 가드 제거 | 102/103 |
| `ck_del`에서 `n` 안 줄임 | 98/103 |
| rebuild 전에 `cur` 커밋 | 101/103 |
| 3번째 슬롯까지 탐사 | 100/103 |
| 둘 중 하나만 사이클이어도 CK_CYCLE | 102/103 |

## 7. 자주 하는 실수

1. **빈 슬롯 값을 키로 조회한다.** `ck_find(CK_EMPTY)`가 빈 슬롯에 맞아 "찾았다"가 된다. → 맨 앞에서 거른다. 삽입도 `CK_BAD_KEY`로 거절한다.
2. **"같은 슬롯을 두 번 밟으면 실패"로 판정한다.** Part A의 시나리오에서 사슬은 x 자신을 쫓아낸 뒤 반대편으로 나가 성공한다. → 고전 사슬은 횟수 예산으로만 끊고, 읽기 전용 walk에서는 양쪽 방향이 모두 사이클일 때만 실패로 본다.
3. **실패할 때 키를 잃어버린다.** 고전 사슬은 끝나는 순간 손에 든 키가 표 밖에 있다. 그 키를 놓치면 원래 있던 키가 조용히 사라진다. → `*homeless`로 돌려주거나, 애초에 표를 안 바꾸는 경로 우선을 쓴다.
4. **두 시드를 같게 둔다.** 측정 4에서 중앙값 0.035까지 떨어진다. → rebuild에서 s0 ≠ s1을 보장한다.
5. **시드 없는 해시에 rebuild를 믿는다.** rebuild는 그래프를 다시 뽑는 것이다. 해시가 시드를 안 쓰면 똑같은 그래프가 나오고 재시도가 끝없이 실패한다(`h_stuck`). → 시도 횟수를 제한하고 `CK_FULL`을 돌려준다.
6. **ISR이 읽는 표에 고전 사슬을 쓴다.** 놓친 키는 드롭된 프레임이고, 재현이 거의 불가능한 버그가 된다. → 경로 우선, 뒤에서부터 복사.
7. **슬롯 쓰기를 원자적이라고 가정한다.** 8바이트 `{key, val}`은 Cortex-M에서 한 번에 안 써진다. → 쓰기 2개를 임계 구역으로 묶는다. 구간 길이는 상수다.
8. **활성 뱅크에서 재해시한다.** 시드를 바꾸는 순간 조회가 무의미해진다. → 예비 뱅크에 짓고 `cur` 한 워드로 커밋한다.
9. **두 뱅크를 재해시 때마다 비우는 비용을 잊는다.** `ck_clear`는 O(cap)이다. rebuild 전체 비용에 들어간다.

## 8. 면접 Q&A

<details><summary>Q1. 왜 조회가 O(1)이라고 말할 수 있나요? 평균 O(1) 해시와 뭐가 다르죠?</summary>

불변식 때문이다. 키가 있을 수 있는 곳은 `home0(k)`와 `home1(k)` 둘뿐이고, 삽입이 그 사실을 유지한다. 조회 코드에 반복문이 없다(Thumb-2 출력에서 역방향 분기 0개, 명령 46개). 평균 O(1) 해시는 탐사 길이가 표 상태와 키 분포에 달려 있는 **기대값**이고, 측정에서 선형 탐사(load 0.90)의 miss 최대 탐사는 1113이다. cuckoo는 같은 조건에서 2다. 꼬리질문이 "해시 함수가 나쁘면요?"라면 답은 "조회는 여전히 2칸이지만 **삽입이 실패**한다"이다. 나쁜 해시의 비용이 조회가 아니라 삽입/재해시로 간다는 점이 임베디드에서는 좋은 분리다.
</details>

<details><summary>Q2. 삽입의 최악은 얼마죠? ISR 안에서 삽입해도 되나요?</summary>

삽입은 O(1)이 아니다. 사슬이 길어질 수 있고(load 0.47~0.49에서 max 115칸), 실패하면 O(n) rebuild가 온다. 대응은 ① **경로 예산 K**로 삽입 1회의 쓰기를 K+1로 묶는다(측정 2에서 K=32가 cap 16384의 한계를 거의 잃지 않는다). ② 그 예산 안에서 실패하면 **태스크로 미룬다** — ISR은 조회만, 삽입은 태스크. ISR에서 넣어야 한다면 `CK_BUDGET`에 "큐에 쌓고 태스크가 처리"를 붙인다. 또 경로 우선을 쓰더라도 표 구조를 수정하니 ISR·태스크 양쪽에서 쓰면 쓰기 측 상호 배제가 필요하다.
</details>

<details><summary>Q3. 왜 50%죠? 더 채울 수는 없나요?</summary>

키를 간선, 슬롯을 정점으로 보면, 연결 성분에 간선이 정점보다 많아지면 배치가 불가능하다. 무작위 그래프에서 거대 성분이 생기는 문턱이 간선 수 = 정점 수/2, 즉 슬롯 부하 0.5다. 측정한 첫 실패 load의 중앙값은 cap 65536에서 0.509이고, 표가 커지면 0.5로 다가간다. 더 채우려면 슬롯을 늘리는 쪽이 아니라 **키의 선택지를 늘린다**: 버킷당 여러 칸(MemC3는 버킷 4칸으로 약 95% 점유율을 보고한다; see:NETHW-M05), 해시 3개 이상, stash. 우리의 1칸 2테이블은 조회 코드가 가장 단순한 대신 RAM을 많이 쓴다.
</details>

<details><summary>Q4. 사이클 감지는 어떻게 하셨나요? 카운터만 있으면 되지 않나요?</summary>

고전 사슬에서는 카운터(`MaxLoop`, Pagh–Rodler는 3·log_{1+ε} r로 설정)로 충분하다. 사이클 구조를 보지 않고도 곧 끝난다. 하지만 사슬을 **실제로 실행하면서** 카운터가 다 찰 때까지 슬롯을 계속 쓴다. 나는 읽기 전용 walk를 Brent로 감시한다. 표 6에서 불가능한 삽입 1건을 판정하는 비용은 중앙값 194슬롯 읽기, 최대 862슬롯 읽기이고 쓰기 0번이다. 게다가 이 판정은 오라클로 검증한 것처럼 정확해서 `CK_CYCLE`과 `CK_BUDGET`(경로가 길어 포기)을 구분해 준다. 진단 카운터로 쓰면 "표가 구조적으로 포화됐다"와 "경로 예산이 빡빡하다"를 구별할 수 있다. 반면 방문 비트맵은 2·cap비트와 초기화 비용이 들어서 작은 표가 아니면 나쁘다.

꼬리질문 "walk가 왜 함수형 그래프죠?": 슬롯마다 점유자가 많아야 하나이고 점유자의 반대쪽 슬롯은 하나로 정해지니, 표가 고정되면 "슬롯 → 다음 슬롯"이 함수다.
</details>

<details><summary>Q5. 태스크가 삽입하는 동안 ISR이 조회합니다. 이 코드가 SMP에서도 안전한가요?</summary>

단일 코어 ISR 모델에서는 안전하다. 뒤에서부터 옮기면 언제나 키가 어딘가 있고, 슬롯 쓰기는 임계 구역이다. ISR은 끝까지 실행되므로 `cur`를 읽은 뒤 뱅크가 사라지지 않는다.

**SMP에서는 안 된다.** 두 가지가 깨진다. ① 판독기가 T0을 본 뒤 T1을 보기 전에 쓰기 측이 키를 T1 → T0으로 옮기면, T0에서 못 보고 T1에서도 못 본다(뒤에서부터 옮겨도 생기는 false miss). MemC3는 키별 버전 카운터로 이를 감지해 재시도한다(논문 §3.2.1: 단일 writer, 다중 reader). 즉 seqlock 패턴이다(see:LLAT-M02). ② 활성 뱅크 교체 후 옛 뱅크를 재사용하려면 옛 뱅크를 읽던 판독기가 끝날 때까지 기다려야 한다. 이는 RCU/epoch의 유예 기간 문제다(see:OS-A03). ISR 모델에서 공짜였던 이유는 ISR이 끝날 때까지 태스크가 못 돌기 때문이다.

꼬리질문 "`volatile`이면 안 되나요?": `cur` 쓰기가 데이터 쓰기보다 앞서 보이면 ISR이 덜 지어진 뱅크를 읽는다. C11 release/acquire가 맞는 도구다. 단일 코어에서는 컴파일러 재배치만 막으면 되므로 `dmb`가 없어도 되는 타깃이 있을 수 있지만(DS-A02 범위), 코드는 그 구분과 무관하게 올바르다.
</details>

<details><summary>Q6. rebuild는 어떻게 malloc 없이 했나요? 그 RAM이 아까운데요.</summary>

호출자가 준 예비 뱅크에 짓는다. 2·cap 슬롯을 항상 들고 있어야 하므로 cap 512면 RAM 16 KiB(8바이트 슬롯, 뱅크 2개 × 슬롯 1024개)다. 줄이는 방법은 ① 제자리 재시드(연습 1): 추가 RAM이 0이지만 그동안 조회가 불가능하고 실패하면 표가 뒤섞인다. ② 재해시를 드물게 만들어 예비 뱅크를 **뱅크 하나 분량**의 공유 스크래치 버퍼에서 잠깐 빌린다(다른 일회성 작업과 시간 공유). ③ 재해시 없이 포화되면 그냥 거절한다 — 고정 필터 표에서는 충분히 설득력 있는 답이다. ISR이 조회하는 동안 재빌드해야 하는 표라면 ①은 답이 아니고(조회가 멈춘다), 예비 뱅크를 쓴다. 키 집합이 정적인 설정 표라면 ③이나 아래의 빌드 타임 생성을 고른다.

꼬리질문 "빌드 타임에 표를 만들면요?": 키가 정적이면 빌드 시 시드를 찾아 ROM 표로 굽는다. 그러면 런타임엔 조회 2칸, 삽입 없음, RAM 0이다(GNU gperf류의 perfect hash와 같은 맥락).
</details>

<details><summary>Q7. SSD 펌웨어라면 L2P 캐시에 cuckoo를 쓰시겠어요?</summary>

L2P 전체 맵이라면 안 쓴다. LBA가 조밀해서 직접 인덱스 배열이 가장 빠르고 메모리도 작다. 부분 맵을 DRAM/SRAM에 올리는 캐시(DFTL류)의 **태그 조회**라면 후보지만, 탐사 2칸이 보장되는 대신 cap의 절반밖에 못 쓰므로 집합 연관 태그 RAM(하드웨어 CAM 포함)이 더 적합한 경우가 많다. 호스트 읽기 tail을 줄이는 목적이면 bounded 조회의 가치가 크고, 반대로 캐시 쫓겨남 정책까지 포함한 전체 경로로 판단해야 한다. 플래시에 표 자체를 저장할 경우 제자리 갱신이 안 되므로 cuckoo의 슬롯 왕복(축출 k번 = 쓰기 k+1번)은 쓰기 증폭 요인이 되니 고려 대상이 아니다. 이건 설계 판단이지 특정 제품이 어떻다는 얘기는 아니다.
</details>

<details><summary>Q8. 어떤 경우에 cuckoo를 쓰면 안 되나요?</summary>

① 메모리가 빡빡한데 키가 많을 때: 1칸 2테이블은 load 0.45 정도이므로 슬롯이 키의 2배 이상이고, 뱅크 2개면 4배 이상이다. 선형 탐사는 load 0.8도 가능하다. ② 조회 hit의 **평균** 지연을 최소화해야 할 때: 측정상 큰 표에서 cuckoo hit이 느렸다(캐시 라인 2개). ③ 키가 작은 정수 범위일 때: 직접 인덱스 배열이 항상 이긴다. ④ 범위 조회가 필요할 때: 해시는 안 된다(DS-A08 B+ 트리). ⑤ 키 집합이 정적일 때: 삽입 비용을 치를 이유가 없으니 정렬 배열 + 이진 탐색이나 빌드 타임 perfect hash가 낫다. 맞는 자리는 **조회가 ISR 또는 패킷 경로에 있고 최악 시간을 보장해야 하며, 갱신은 드문** 표다.
</details>

## 9. 추가 연습

### 연습 1 — 제자리 재시드 (RAM 0, 대신 정지)

예비 버퍼 없이 표를 스캔해 **새 시드 기준으로 제자리에 있지 않은 키**를 꺼내 다시 넣는다. 쫓겨난 키가 제자리에 있던 키면 반대쪽으로, 아니면 T0부터 다시 삽입한다. 실패하면 또 새 시드로 처음부터 스캔하되 **손에 든 키를 잃지 않아야** 한다.

<details><summary>답</summary>

"제자리"는 비트로 기록할 필요가 없다. 새 시드로 `home0(k)`, `home1(k)`를 계산해 지금 위치와 비교하면 그만이다. 이것이 Pagh–Rodler가 "새 테이블을 할당할 필요 없다"고 쓴 방법이다. 스캔 불변식은 "처리한 슬롯은 비었거나 올바른 키가 있다"이다. 잘못 놓인 키가 쫓겨나면 `T0`부터 다시 시작하는 것이 곧 "새로 삽입"이기 때문이다. 시도가 다 실패하면 `CK_FULL`을 돌려주고 손에 든 키를 `*homeless`로 내놓지만, **그 시점의 표는 뒤섞여 있다**(일부 키가 옛 위치). 다만 "키가 어딘가에는 있다"는 지켜진다. 시드가 안 먹는 해시로 시험하면 키 3개가 표 2개 + homeless 1개로 **모두 남아 있다**. 조회가 안 되는 구간이 있으니 ISR이 읽는 표에는 맞지 않는다.

실제 출력: `stuck hash: n=2 homeless=0x30004, keys accounted for=3/3`.
</details>

### 연습 2 — 양쪽 walk를 보고 짧은 쪽 고르기

`ck_put`은 T0 쪽이 되면 T1 쪽을 보지 않는다. 두 방향을 모두 걷고 짧은 쪽을 쓰도록 바꿔, load 0.45까지 채울 때 총 축출 횟수와 최대 사슬을 비교하라.

<details><summary>답</summary>

walk 비용이 두 배가 되는 대신 경로가 짧아진다. 실제 출력은 `T0-first: total kicks 3831 max 29   shorter: total 2734 max 12   (inserts 36880)`이다. 총 축출은 약 29% 줄고 최대 사슬은 29 → 12로 줄었다. 슬롯 쓰기가 비싼 상황이나 경로 예산이 빡빡한 경우에 맞는 선택이다. 읽기 비용은 두 배로 드니 ISR에 삽입을 넣는 설계에서는 그 비용도 WCET에 포함해야 한다(MemC3도 다중 경로 탐색을 쓴다).
</details>

### 연습 3 — 경로를 걷지 않는 판정: 증분 union-find

각 연결 성분에 "사이클 있음" 플래그를 두고 간선을 추가하면서 삽입 가능성을 O(α) 안에 판정한다. `ck_put`의 성공/실패와 일치하는가? 그리고 **삭제 이후에도** 일치하는가?

<details><summary>답</summary>

간선 (a,b)를 추가할 때 두 루트가 같으면: 플래그가 이미 있으면 불가능, 아니면 플래그 설정. 다르면: 둘 다 플래그면 불가능, 아니면 합치고 플래그 OR. 키 삽입만 있을 때 `ck_put`과 **100% 일치**한다(`adds=122266 rejects=77287 mismatches=0`). 그러나 union-find는 **간선을 지울 수 없다**. 삭제 후에도 옛 구조를 기억하므로 실제로는 들어가는 키를 거절한다. 실제 출력에서 삭제 후 `walk ok=35923, stale rejects=33192`이다. 삭제가 없는 표(일회성 설정 표)라면 walk 없이 판정해 삽입 시간을 줄일 수 있고, 삭제가 있으면 walk를 써야 한다.
</details>

### 연습 4 — 두 시드가 같으면

`seed[0] == seed[1]`인 표와 다른 표의 첫 실패 load를 40개 시드로 비교하라. 왜 무너지는가?

<details><summary>답</summary>

두 해시가 같은 값을 주므로 키의 슬롯이 `T0[i]`, `T1[i]`, 같은 인덱스의 쌍이 된다. 각 쌍은 독립적인 "칸 2개"이고, 키 3개가 같은 i에 몰리면 끝이다. 생일 문제다. 실제 출력은 `mean first-failure load: distinct seeds 0.544, same seed 0.082`다. 표는 두 테이블로 나뉘어 있어도 그래프로는 성분이 쌍마다 끊긴다. 초기화와 rebuild 양쪽에서 `s0 != s1`을 확인하는 이유다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice           # 16/16 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o p_san && ./p_san
```

## 10. 핵심 정리

- 불변식: 모든 키는 `home0(k)` 또는 `home1(k)`에만 있다. 그래서 조회는 **항상 2칸**(반복 없음)이고 삭제에 묘비가 없다. 100만 조회 측정에서 3칸 이상 0번.
- 키 = 간선, 슬롯 = 정점. **연결 성분의 간선 ≤ 정점이면 배치 가능**, 아니면 불가능. 그래프 전체의 거대 성분 문턱이 부하 0.5다.
- 고전 사슬은 사이클을 돌아도 성공할 수 있다(자기 자신을 쫓아내고 반대편으로). 진짜 실패는 양방향 모두 사이클일 때다.
- 읽기 전용 walk + Brent로 판정하면 실패해도 표가 안 변하고, 뒤에서부터 옮기면 ISR 조회가 키를 놓치지 않는다.
- 실패 시 예비 뱅크에 새 시드로 rebuild하고 `cur` 한 워드로 커밋한다. 해시가 시드를 안 쓰면 영원히 실패하고, 같은 시드는 한계를 0.03대로 떨어뜨린다.
- 측정(1칸 2테이블): 첫 실패 중앙값 0.51~0.57(cap 256에서 0.57, 65536에서 0.509, 0.5로 수렴). cap 4096 기준 첫 시도 99% 성공은 load 0.44까지. K=32 경로 예산은 한계를 거의 안 잃는다.
- **조회는 O(1), 삽입은 아니다.** 사슬 최악은 load가 오르면 빠르게 커진다(0.47–0.49에서 평균 0.88, max 115). 드문 O(n) rebuild는 태스크로.
- hit 평균은 선형 탐사가 더 빠를 수 있다(캐시 라인 2개 vs 1개). cuckoo를 고르는 이유는 평균이 아니라 **상한**이다. miss는 cuckoo가 크게 이긴다.

## 참고자료

- R. Pagh, F. F. Rodler, "Cuckoo Hashing", *Journal of Algorithms* 51(2):122–144, 2004 — 2표 삽입 절차, MaxLoop = 3·log_{1+ε} r, closed loop 분석, 재해시 확률, 제자리 재해시 언급(본문에서 직접 확인)
- B. Fan, D. G. Andersen, M. Kaminsky, "MemC3: Compact and Concurrent MemCache with Dumber Caching and Smarter Hashing", NSDI 2013 — 경로 탐색과 실행 분리, 역방향 이동, 단일 writer/다중 reader, 키별 버전 카운터(§3.2, 직접 확인)
- A. Kirsch, M. Mitzenmacher, U. Wieder, "More Robust Hashing: Cuckoo Hashing with a Stash", ESA 2008 — stash (see:NETHW-M05)
- R. P. Brent, "An Improved Cycle Finding Algorithm", *BIT* 20(2):176–184, 1980
- Austin Appleby, MurmurHash3 — finalizer `fmix32` 상수 출처
- 관련 레슨: DS-B11 (선형 탐사), DS-M08 (Robin Hood), DS-A08 (B+ 트리), NETHW-M05 (bucketed cuckoo + stash), STOR-B07 (cuckoo filter), DS-A02 (ISR 배리어), LLAT-M02 (seqlock), OS-A03 (RCU/epoch), ARM-M04 (LL/SC·원자성)
