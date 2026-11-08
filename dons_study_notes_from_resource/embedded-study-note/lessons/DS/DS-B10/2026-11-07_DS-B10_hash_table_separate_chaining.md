---
id: DS-B10
time: 60분
updated: 2026-11-07
---
# DS-B10 해시 테이블: 분리 체이닝

## 학습 목표

- 버킷 배열 + 단일 연결 리스트로 **분리 체이닝(separate chaining)** 해시 테이블의 `put / get / del` 을 구현한다.
- malloc 없이 **정적 노드 풀 + free list** 로 노드를 O(1) 에 빌리고 돌려준다.
- **포인터의 포인터(`Node **pp`)** 로 head·중간·tail 삭제를 분기 없이 한 줄로 처리한다.
- 같은 버킷 충돌, 삭제 후 재삽입, 풀 고갈 같은 경계 시나리오와 **참조 모델 대조 랜덤 테스트**로 구현을 검증한다.
- 부하율 α 와 체인 길이, 풀 크기 같은 설계 숫자를 펌웨어 메모리 예산으로 설명한다.

## 1. 문제

"malloc 없이 문자열 키 해시 맵을 만들어 보세요" 는 펌웨어 면접의 단골이다. 실무에서도 CLI 명령 이름 → 핸들러, MQTT 토픽 → 콜백, 센서 이름 → 드라이버, (SSD 라면) 쓰기 캐시의 LBA → 버퍼 슬롯 조회가 전부 이 구조다. 동적 할당 없이, 최악의 경우가 예측 가능하게, 용량이 다 찼을 때 *깔끔하게 실패* 해야 한다.

분리 체이닝은 버킷마다 연결 리스트를 하나씩 두고, 같은 버킷으로 온 키들을 그 리스트에 줄 세운다. 노드는 전부 미리 잡아 둔 배열(풀)에서 나온다.

```svg
<svg viewBox="0 0 720 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b10a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">HashTable = bucket[8] (체인의 head) + pool[32] (노드 창고) + free_list</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="40" width="70" height="200"/>
    <line x1="20" y1="65" x2="90" y2="65"/><line x1="20" y1="90" x2="90" y2="90"/><line x1="20" y1="115" x2="90" y2="115"/>
    <line x1="20" y1="140" x2="90" y2="140"/><line x1="20" y1="165" x2="90" y2="165"/><line x1="20" y1="190" x2="90" y2="190"/><line x1="20" y1="215" x2="90" y2="215"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="28" y="57">[0] ∅</text><text x="28" y="82">[1] •</text><text x="28" y="107">[2] ∅</text><text x="28" y="132">[3] ∅</text>
    <text x="28" y="157">[4] ∅</text><text x="28" y="182">[5] ∅</text><text x="28" y="207">[6] •</text><text x="28" y="232">[7] •</text>
  </g>
  <g class="f-acc" stroke="currentColor">
    <rect x="140" y="65" width="130" height="34" rx="4"/>
    <rect x="140" y="185" width="110" height="34" rx="4"/><rect x="290" y="185" width="110" height="34" rx="4"/><rect x="440" y="185" width="110" height="34" rx="4"/>
    <rect x="140" y="232" width="150" height="34" rx="4"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="148" y="87">"imu0_x" | 111</text>
    <text x="148" y="207">"s22" | 13</text><text x="298" y="207">"s13" | 12</text><text x="448" y="207">"s8" | 11</text>
    <text x="148" y="254">"baro1_raw" | 200</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#b10a)">
    <path d="M78 77 L138 82"/><path d="M78 202 L138 202"/><path d="M250 202 L288 202"/><path d="M400 202 L438 202"/><path d="M78 228 L138 246"/>
  </g>
  <text x="556" y="207" fill="currentColor" font-size="12">→ "s0" → NULL</text>
  <text x="300" y="82" fill="currentColor">버킷 = FNV-1a(key) &amp; 7</text>
  <text x="300" y="104" fill="currentColor">같은 버킷 → 체인 head 에 붙는다</text>
  <g class="f-acc" stroke="currentColor"><rect x="20" y="285" width="34" height="26"/><rect x="60" y="285" width="34" height="26"/><rect x="100" y="285" width="34" height="26"/><rect x="140" y="285" width="34" height="26"/></g>
  <g class="f-mut" stroke="currentColor"><rect x="180" y="285" width="34" height="26"/><rect x="220" y="285" width="34" height="26"/><rect x="260" y="285" width="34" height="26"/></g>
  <text x="305" y="303" fill="currentColor">pool[32]: 강조 = 사용 중, 회색 = 빈 노드(free_list)</text>
  <text x="20" y="280" fill="currentColor" font-size="12">pool</text>
</svg>
```

### Part A — 정적 노드 풀

- `ht_init(t)` — 버킷을 모두 NULL, `count = 0`, `pool[]` 의 노드 32개를 **free list** 로 엮는다 (노드의 `next` 필드를 재활용).
- `pool_alloc(t)` — free list 의 head 를 떼어 돌려준다. 비었으면 `NULL`.
- `pool_free(t, n)` — 노드를 free list 의 head 에 다시 꽂는다.

### Part B — 조회와 삽입

- `bool ht_get(t, key, &out)` — 키의 버킷 체인을 따라가며 `strcmp`.
- `HtResult ht_put(t, key, value)` — 키가 이미 있으면 값만 바꾸고 `HT_UPDATED`. 없으면 노드를 빌려 키를 **노드 안에 복사**하고 체인 head 에 붙인 뒤 `HT_INSERTED`. 풀이 비었으면 `HT_ERR_FULL`, 키가 15자를 넘으면 `HT_ERR_KEY`.

### Part C — 삭제와 시나리오 검증

- `bool ht_del(t, key)` — `Node **pp` 로 체인을 걸으며, 찾으면 `*pp = n->next` 로 떼어내고 풀에 반납.
- 하네스가 확인하는 시나리오:
  1. **같은 버킷 충돌** — 버킷이 같은 키 4개(`s0, s8, s13, s22` → 모두 버킷 6)를 넣고 중간·head·tail 순서로 지운다.
  2. **삭제 후 재삽입** — 지운 키를 다시 넣으면 `HT_UPDATED` 가 아니라 `HT_INSERTED`, free 노드 수가 정확히 돌아와야 한다.
  3. **풀 고갈** — 32개를 채우면 새 키는 `HT_ERR_FULL`, 그래도 **기존 키 갱신은 성공**해야 한다.
  4. **랜덤 테스트** — xorshift32(고정 시드)로 20,000 연산(put 50% / get 30% / del 20%)을 돌리며 배열 기반 **참조 모델**과 결과를 매번 비교하고, 100 연산마다 불변식을 검사한다.

## 2. Problem (English)

> **Fixed-capacity string hash map without dynamic allocation.**
>
> Implement a separate-chaining hash table mapping NUL-terminated string keys (at most 15 characters) to `uint32_t` values. Use `NBUCKET = 8` buckets selected by `fnv1a(key) & 7` and a statically allocated pool of `POOL_N = 32` nodes managed by a free list.
>
> ```c
> void     ht_init(HashTable *t);
> HtResult ht_put(HashTable *t, const char *key, uint32_t value); // INSERTED | UPDATED | ERR_FULL | ERR_KEY
> bool     ht_get(const HashTable *t, const char *key, uint32_t *out);
> bool     ht_del(HashTable *t, const char *key);
> ```
>
> Requirements: all operations are O(1 + chain length); no `malloc`; keys are copied into the node; updating an existing key must succeed even when the pool is exhausted; deleting returns the node to the pool so it can be reused. Validate with collision-heavy cases (several keys in one bucket, delete head/middle/tail), delete-then-reinsert, pool exhaustion, and a seeded randomized test against a simple reference model.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 풀과 put/get/del 이 비어 있어 FAIL 이 쭉 나온다 — "6/30 passed"
```

> 맥 SDK 27.2에서 `ld: tapi error` 가 나면 `export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk` 후 다시 빌드.

## 4. 힌트

<details>
<summary>힌트 1 — free list 는 "빈 노드들의 스택"</summary>

빈 노드의 `next` 필드는 어차피 안 쓰니, 그걸로 빈 노드끼리 엮는다. `alloc` = 스택 pop (`n = free_list; free_list = n->next;`), `free` = 스택 push (`n->next = free_list; free_list = n;`). 둘 다 O(1), 별도 메모리 0 바이트.

</details>

<details>
<summary>힌트 2 — put 은 "먼저 찾고, 없을 때만 할당"</summary>

순서를 바꿔 할당부터 하면 ① 같은 키가 두 번 들어가고 ② 풀이 꽉 찼을 때 *갱신* 까지 `ERR_FULL` 로 실패한다. 체인을 한 번 훑어 같은 키가 있으면 값만 바꾸고 끝.

</details>

<details>
<summary>힌트 3 — head 에 붙이면 O(1)</summary>

어차피 중복 검사로 체인 끝까지 갔다 왔지만, 새 노드를 head 에 붙이면 `n->next = bucket[b]; bucket[b] = n;` 두 줄이다. tail 포인터를 따로 관리할 필요가 없다.

</details>

<details>
<summary>힌트 4 — 삭제는 "이전 노드" 대신 "나를 가리키는 칸"</summary>

`prev` 를 들고 다니면 head 삭제(`bucket[b] = n->next`)와 중간 삭제(`prev->next = n->next`)가 갈린다. `Node **pp = &bucket[b]` 로 시작해 `pp = &(*pp)->next` 로 전진하면, 찾았을 때 항상 `*pp = n->next;` 한 줄이다.

</details>

<details>
<summary>힌트 5 — 참조 모델은 최대한 멍청하게</summary>

키를 `"k00".."k47"` 48종으로 제한하면 참조 모델은 `bool has[48]; uint32_t val[48]; count` 배열이면 된다. 해시도 체인도 없어서 틀릴 곳이 없다. put 의 기대값은 `has[k] ? UPDATED : (count == POOL_N ? FULL : INSERTED)`.

</details>

## 5. 풀이

### Part A — 풀과 free list

```text
ht_init 후:   free_list → pool[0] → pool[1] → ... → pool[31] → NULL
alloc 3번:    pool[0], pool[1], pool[2] 사용 중, free_list → pool[3] → ...
pool[1] free: free_list → pool[1] → pool[3] → ...   (LIFO: 방금 반납한 노드가 다음에 먼저 나간다)
```

`ht_init` 은 뒤에서부터 push 해서 `pool[0]` 이 처음 나가게 했다 (디버깅할 때 주소가 순서대로 보여 편하다). 순서 자체는 정확성과 무관하다.

**메모리 예산.** 노드 = `next` 포인터 + `value` 4 + `key[16]`.

| 플랫폼 | sizeof(Node) | pool[32] | bucket[8] | 합계 |
|---|---|---|---|---|
| Cortex-M (32비트 포인터) | 4 + 4 + 16 = 24 B | 768 B | 32 B | 약 0.8 KB |
| 맥 (64비트 포인터, 실측) | 8 + 4 + 16 = 28 → **32 B** (정렬 패딩) | 1024 B | 64 B | 약 1.1 KB |

전부 `.bss` 에 잡히므로 링커 맵에서 크기가 그대로 보이고, 런타임에 힙 단편화나 할당 실패가 끼어들 틈이 없다. MISRA C:2012 Rule 21.3 (`malloc`/`free` 금지)을 지키는 표준 방법이다.

### Part B — put 의 흐름

```svg
<svg viewBox="0 0 720 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b10b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">ht_put(t, key, value)</text>
  <g stroke="currentColor" fill="none">
    <rect x="10" y="45" width="120" height="34" rx="6"/><rect x="170" y="45" width="150" height="34" rx="6"/>
    <rect x="360" y="45" width="160" height="34" rx="6"/><rect x="170" y="125" width="150" height="34" rx="6"/>
    <rect x="360" y="125" width="160" height="34" rx="6"/><rect x="560" y="125" width="150" height="34" rx="6"/>
  </g>
  <rect x="560" y="45" width="150" height="34" rx="6" class="f-ok"/>
  <rect x="10" y="125" width="120" height="34" rx="6" class="f-warn"/><rect x="170" y="195" width="150" height="34" rx="6" class="f-warn"/>
  <rect x="560" y="195" width="150" height="34" rx="6" class="f-ok"/>
  <g fill="currentColor">
    <text x="20" y="67">len ≥ 16 ?</text><text x="178" y="67" font-size="12">b = fnv1a(key) &amp; 7</text>
    <text x="370" y="67">체인에 같은 키?</text><text x="570" y="67">값만 갱신 → UPDATED</text>
    <text x="20" y="147">ERR_KEY</text><text x="180" y="147">n = pool_alloc()</text>
    <text x="370" y="147">키 복사, value 저장</text><text x="570" y="147">head 삽입, count++</text>
    <text x="180" y="217">NULL → ERR_FULL</text><text x="570" y="217">INSERTED</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#b10b)">
    <path d="M130 62 L168 62"/><path d="M320 62 L358 62"/><path d="M520 62 L558 62"/>
    <path d="M70 79 L70 123"/><path d="M440 79 L440 100 L245 100 L245 123"/>
    <path d="M320 142 L358 142"/><path d="M520 142 L558 142"/><path d="M245 159 L245 193"/><path d="M635 159 L635 193"/>
  </g>
  <g fill="currentColor" font-size="12"><text x="132" y="54">아니오</text><text x="530" y="56">예</text><text x="76" y="105">예</text><text x="450" y="95">아니오</text></g>
</svg>
```

- **왜 키를 복사하나?** 호출자가 넘긴 `key` 가 UART 수신 버퍼나 스택 배열이면, 다음 패킷이 오는 순간 내용이 바뀐다. 포인터만 저장하면 테이블이 남의 메모리를 들고 있게 된다. 그래서 `KEY_MAX` 상한을 두고 노드 안에 `memcpy(n->key, key, len + 1)` — NUL 까지 복사한다.
- **길이 검사는 맨 앞.** `strlen(key) < KEY_MAX` 를 통과한 키만 받으므로 `memcpy` 가 넘칠 수 없다. `strncpy` 는 꽉 찬 경우 NUL 을 안 붙여서 오히려 위험하다.
- **조회 비용**: 버킷 계산 O(len) + 체인 길이만큼 `strcmp`. 평균 체인 길이 = 부하율 **α = count / NBUCKET**. 이 레슨은 일부러 NBUCKET = 8, 용량 32 로 잡아 꽉 차면 α = 4 — 충돌 경로를 많이 밟게 하려는 설정이다. 실무에선 α ≤ 1 정도로 버킷을 잡는다 (버킷 1개 = 포인터 4 B 라 싸다).

### Part C — 포인터의 포인터로 삭제

```svg
<svg viewBox="0 0 720 245" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b10c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" fill="currentColor" font-weight="bold">ht_del("s13"): pp 는 "s13 을 가리키고 있는 칸"의 주소</text>
  <g stroke="currentColor" fill="none"><rect x="20" y="60" width="80" height="34"/></g>
  <g class="f-acc" stroke="currentColor"><rect x="150" y="60" width="130" height="34" rx="4"/><rect x="450" y="60" width="130" height="34" rx="4"/></g>
  <rect x="300" y="60" width="130" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="28" y="82">bucket[6]</text>
    <text x="158" y="82">"s22" | next</text><text x="308" y="82">"s13" | next</text><text x="458" y="82">"s8" | next</text>
  </g>
  <g stroke="currentColor" fill="none" marker-end="url(#b10c)">
    <path d="M100 77 L148 77"/><path d="M280 77 L298 77"/><path d="M430 77 L448 77"/>
    <path d="M250 135 L250 98"/>
    <path d="M265 60 C320 25 400 25 455 58" stroke-dasharray="5 3"/>
  </g>
  <text x="200" y="152" fill="currentColor">pp = &amp;s22-&gt;next  (*pp == s13)</text>
  <text x="300" y="122" fill="currentColor" font-size="12">*pp = s13-&gt;next  (점선: 새 연결)</text>
  <text x="10" y="182" fill="currentColor">head 삭제도 pp = &amp;bucket[6] 일 뿐, 코드는 똑같이 *pp = n-&gt;next;</text>
  <text x="10" y="202" fill="currentColor">prev 도, if (prev == NULL) 분기도 없다.</text>
  <text x="10" y="226" fill="currentColor">떼어낸 s13 은 pool_free 로 free_list head 에 반납 → 다음 put 이 바로 이 노드를 쓴다.</text>
</svg>
```

Linus Torvalds 가 "좋은 취향(good taste)" 의 예로 든 그 패턴이다. Linux 의 `hlist_node` 는 아예 `pprev` (이전 노드의 `next` 필드 주소)를 노드 안에 저장해서 체인을 다시 훑지 않고도 O(1) 삭제를 한다.

**시나리오별 확인 포인트**

| 시나리오 | 무엇을 잡나 | 확인 |
|---|---|---|
| `s22 → s13 → s8 → s0` 에서 s13(중간), s22(head), s0(tail) 삭제 | head/tail 분기 실수, 체인 끊김 | 남은 `s8` 값 11, 체인 길이 1 |
| s8 삭제 → 다시 삭제 → 재삽입 | 반납 누락(풀 누수), 이중 삭제, 갱신/삽입 혼동 | free 32개 복귀, 2번째 del false, 재삽입 `INSERTED` |
| 32개 채움 → 새 키 / 기존 키 / 하나 지우고 새 키 | 고갈 처리, "할당 먼저" 버그 | `ERR_FULL` / `UPDATED` / `INSERTED` |
| 랜덤 20,000 연산 | 위에서 생각 못 한 조합 | 참조 모델과 매 연산 비교 |

**불변식 (`ht_check`)** — 랜덤 테스트가 100 연산마다 부른다.

1. 체인의 모든 노드는 `bucket_of(key) == 자기 버킷` 이다.
2. 테이블 전체에 같은 키가 두 번 없다.
3. 체인 길이의 합 == `count`.
4. 체인 노드 수 + free list 노드 수 == `POOL_N` (노드 누수·이중 반납 없음, 사이클 없음).

**랜덤 테스트 실측 결과** (`./solution` 출력 그대로)

```text
random: 20000 ops, mismatch=0, invariant_fail=0, FULL=1067, final count=30, max chain=7
30/30 passed
```

- 키 종류(48)를 용량(32)보다 크게 잡았기 때문에 `ERR_FULL` 경로를 **1067번** 밟았다. 키 종류가 용량보다 작으면 고갈 코드는 한 번도 실행되지 않은 채 "통과" 한다 — 테스트가 그 경로를 *실제로* 밟았는지(`n_full > 0`)까지 CHECK 한다.
- 최대 체인 길이 7: 평균 α ≈ 4 인데 버킷 8개 중 하나가 7 까지 찼다. 체이닝의 최악은 O(n) 이라는 걸 숫자로 보여 준다.
- 시드가 고정이라 실패하면 같은 연산 번호에서 매번 재현된다. 실패한 `op` 번호를 찍고 그 직전까지 돌려 디버깅한다.
- ASan/UBSan 으로도 확인했다: `cc -std=c11 -Wall -Wextra -Werror -O1 -fsanitize=address,undefined -g solution.c && ./a.out` → 30/30, 경고 없음.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 30/30 passed
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `n->key_ptr = key;` (포인터만 저장) | 호출자 버퍼(UART rx, 스택)가 바뀌면 키가 바뀜 → 엉뚱한 버킷에 있는 노드 | 노드 안 `char key[KEY_MAX]` 에 `memcpy(.., len + 1)` |
| put 에서 할당부터 하고 중복 검사 | 같은 키 2개, 풀이 꽉 차면 갱신도 `ERR_FULL` | 체인 검색 → 없을 때만 `pool_alloc` |
| `prev` 로 삭제하며 head 일 때 `prev->next` | NULL 역참조 (head 삭제에서만 터짐) | `Node **pp` 패턴 |
| 삭제 후 `pool_free` 누락 | 풀 누수 — put/del 32번 반복 뒤 영원히 `ERR_FULL` | 불변식 ④ (체인 + free = POOL_N) 로 검출 |
| `strncpy(n->key, key, KEY_MAX)` | 15자 초과면 NUL 없음 → `strcmp` 가 노드 밖까지 읽음 | 길이 검사 후 `memcpy(len + 1)` |
| 지운 노드 포인터를 계속 사용 | LIFO 풀이라 다음 put 이 **그 노드를 바로 재사용** → 다른 키의 값을 읽음 (풀판 use-after-free) | 노드 포인터를 밖에 내주지 않음, 또는 세대(generation) 번호 |
| ISR 과 메인이 락 없이 같은 테이블 수정 | 체인 relink 중간에 끼어들면 노드 유실·사이클 | 임계 구역 또는 한쪽만 쓰기 (DS-B10 은 단일 컨텍스트 전제) |

## 8. 면접 Q&A

<details>
<summary>Q1. 분리 체이닝과 오픈 어드레싱의 차이, 펌웨어에선 무엇을 고르나요?</summary>

체이닝: 버킷마다 리스트. 삭제가 단순하고 부하율이 1을 넘어도 동작, 대신 노드마다 포인터 비용과 캐시 미스. 오픈 어드레싱(DS-B11): 배열 한 장에 직접 저장, 캐시 친화적이고 메모리가 촘촘하지만 삭제에 tombstone 이 필요하고 α 가 0.7~0.8 을 넘으면 급격히 느려진다. 펌웨어에선 항목 수가 작고 고정이면 오픈 어드레싱, 항목 크기가 크거나 다른 리스트와 노드를 공유(침입형)해야 하면 체이닝. Linux `hashtable.h` 는 hlist 체이닝이다.

</details>

<details>
<summary>Q2. 평균·최악 시간 복잡도는? 최악을 어떻게 막나요?</summary>

평균 O(1 + α), 최악 O(n) (모든 키가 한 버킷). 이번 랜덤 테스트에서도 α ≈ 4 인데 최대 체인 7. 막는 법: 좋은 해시(DS-B09), α ≤ 1 이 되도록 버킷 수 설계, 신뢰할 수 없는 입력이면 keyed hash(SipHash), 체인 길이 상한을 두고 넘으면 거부. Java 8 `HashMap` 은 체인이 8 을 넘으면 레드블랙 트리로 바꿔 최악을 O(log n) 으로 만든다.

</details>

<details>
<summary>Q3. malloc 없이 테이블을 키우려면(rehash)?</summary>

노드는 이미 풀에 있으니 **새로 만들 필요가 없다** — 버킷 배열만 크게 잡아 두고(`MAX_BUCKET`) 각 노드를 새 버킷으로 relink 하면 된다. 노드에 해시를 캐시해 두면 재계산도 없다(추가 연습 3). 용량(노드 수) 자체는 컴파일 타임에 정해지는 게 펌웨어의 정상 상태이고, 그래서 `ERR_FULL` 처리가 API 의 일부다.

</details>

<details>
<summary>Q4. 왜 malloc 대신 정적 풀인가요? 단점은?</summary>

장점: 할당·해제가 O(1) 결정적(실시간), 단편화 없음, 메모리 사용량이 링크 시점에 확정, 실패 지점이 명확(`ERR_FULL`), MISRA 21.3 준수. 단점: 최대 용량을 미리 정해야 하고 쓰지 않는 칸도 RAM 을 차지, 키 길이 상한(`KEY_MAX`) 같은 제약. 꼬리질문 "풀을 여러 테이블이 공유하면?" → 공용 풀 + 테이블별 사용량 제한, 또는 DS 트랙의 메모리 풀/슬랩 할당기.

</details>

<details>
<summary>Q5. ISR 에서 이 테이블을 조회해도 되나요?</summary>

메인이 `put/del` 로 링크를 바꾸는 도중에 ISR 이 체인을 걸으면 반쯤 바뀐 리스트를 본다. 선택지: ① 수정 구간을 `__disable_irq()`/PRIMASK 저장·복원으로 감싸기(짧으니 실용적) ② ISR 은 큐에 요청만 넣고 메인이 처리 ③ RTOS 면 뮤텍스(ISR 에선 불가). 읽기만 동시에 일어나는 경우엔 Linux 처럼 RCU 스타일로 "새 노드를 완성한 뒤 포인터 하나만 원자적으로 교체" 하는 방법도 있다.

</details>

<details>
<summary>Q6. Linux 커널의 hlist_head 는 왜 list_head 와 다른가요?</summary>

해시 버킷은 수천 개라 head 크기가 중요하다. `list_head` 는 포인터 2개(prev, next), `hlist_head` 는 `first` 포인터 1개라 버킷 배열이 절반이 된다. 대신 노드의 `pprev` 를 "이전 노드의 next 필드 주소" (`struct hlist_node **`)로 둬서 head 든 중간이든 O(1) 삭제 — 이 레슨의 `Node **pp` 를 노드에 저장한 것이다.

</details>

<details>
<summary>Q7. 랜덤 테스트에서 참조 모델을 쓰는 이유는? 무엇을 더 하면 좋나요?</summary>

참조 모델은 같은 동작을 "느리지만 틀릴 수 없는" 방식(키 번호로 인덱싱한 배열)으로 구현한 것 — 둘의 결과가 매 연산 같으면 상태 공간을 넓게 훑은 셈이다. 고정 시드로 재현 가능하게, 연산 비율을 조절해 고갈·빈 테이블 같은 극단을 일부러 오가게, 그리고 경로 커버리지(`n_full > 0`)를 확인한다. 더 하면: ASan/UBSan, 여러 시드, 실패 시 연산 로그를 최소화(shrinking).

</details>

## 9. 추가 연습

practice.c 는 본문 테이블을 조금 넓혀 **버킷 수를 변수(`nbucket`)** 로, 노드에 **해시 값 캐시(`hash`)** 를 추가한 버전이다.

### 연습 1 — 순회(foreach)

`ht_foreach(t, fn, ctx)` 를 만들어 `sensor0..sensor19` (값 = i×10) 의 값 합과 개수를 콜백으로 구하라.

<details>
<summary>답 보기</summary>

버킷 0..nbucket-1 을 돌며 체인을 따라 `fn(key, value, ctx)`. C 에 클로저가 없으니 누적 상태는 `void *ctx` 로 넘긴다 (`uint32_t acc[2]` → 합 1900, 개수 20). 순서는 버킷 순서라 *삽입 순서가 아니다* — 출력 순서에 의존하는 코드를 쓰지 말 것. 순회 중 `ht_del` 을 부르면 현재 노드가 free list 로 가서 `n->next` 가 망가진다 (다음 포인터를 미리 저장하는 `_safe` 순회가 필요한 이유, Linux `hlist_for_each_entry_safe`).

</details>

### 연습 2 — move-to-front

찾은 노드를 체인 head 로 옮기는 `ht_get_mtf` 를 만들고, tail 에 있던 키를 한 번 찾은 뒤 두 번째 조회의 `strcmp` 호출이 1번인지 확인하라.

<details>
<summary>답 보기</summary>

`pp` 로 찾은 뒤 `pp != head` 면 `*pp = n->next; n->next = *head; *head = n;`. 체인 길이는 그대로, 자주 쓰는 키(예: CLI 의 `status`, 주기적으로 갱신되는 센서)가 앞으로 와서 평균 비교 횟수가 준다. 대가: **조회가 쓰기**가 되어 `const` 가 사라지고, ISR 과 공유할 때 읽기 전용 경로가 없어진다.

</details>

### 연습 3 — malloc 없는 rehash (8 → 32 버킷)

`ht_grow(t, 32)` 로 버킷 수를 늘리되 노드를 새로 할당하지 말고 링크만 다시 걸어라. 불변식과 모든 값이 유지되는지 확인.

<details>
<summary>답 보기</summary>

새 버킷 [8, 32) 를 NULL 로 → 옛 버킷마다 체인을 통째로 떼어 내고(`bucket[b] = NULL`) 노드를 하나씩 `hash & 31` 버킷의 head 로 push. 떼어 낸 뒤에 넣으니 같은 버킷 b 로 돌아가는 노드가 있어도 안전하다. 캐시된 해시를 쓰므로 FNV 재계산 0번. 실측: 최대 체인 3 → 2 (키 20개). 2배씩 키우면 노드는 `b` 아니면 `b + old_n` 으로만 간다 — 이걸 이용하면 체인 하나를 두 개로 나누는 점진적 rehash 도 가능하다.

</details>

### 연습 4 — 해시 캐시로 strcmp 줄이기

키 32개(8 버킷, α = 4)에서 존재하는 키 32번 + 없는 키 32번을 조회할 때, 노드에 저장한 32비트 해시를 먼저 비교하면 `strcmp` 호출이 몇 번으로 주나?

<details>
<summary>답 보기</summary>

실측: 캐시 없음 **208번** → 캐시 있음 **32번** (찾는 키 본인과의 비교만 남음). 같은 버킷 안의 다른 키는 32비트 해시가 거의 항상 달라서 정수 비교 한 번으로 걸러진다. 노드당 4 B 를 더 쓰는 대신 문자열 비교와 rehash 때 해시 재계산을 없앤다 — 키가 길수록(토픽 이름, 파일 경로) 이득이 크다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 10/10 passed
```

## 10. 핵심 정리

- 분리 체이닝 = `bucket[m]` (체인 head) + 단일 연결 리스트. 평균 O(1 + α), 최악 O(n).
- malloc 금지 → `pool[N]` + free list (빈 노드의 `next` 재활용). alloc = pop, free = push, 둘 다 O(1).
- put: **찾기 먼저**, 있으면 갱신(풀이 꽉 차도 성공), 없으면 alloc → 키 복사(NUL 포함) → head 삽입.
- del: `for (Node **pp = &bucket[b]; *pp; pp = &(*pp)->next)` → `*pp = n->next;` 한 줄. 반납 잊지 말 것.
- 불변식: 자기 버킷 · 중복 없음 · Σ체인 = count · 체인 + free = POOL_N.
- 검증 = 경계 시나리오(같은 버킷, 삭제 후 재삽입, 고갈) + 고정 시드 랜덤 vs 참조 모델 + 경로를 실제로 밟았는지 확인.
- 실측: 20,000 연산 mismatch 0, ERR_FULL 1067회, 최대 체인 7 (α ≈ 4).

## 참고자료

- [Linux kernel include/linux/list.h — hlist_head / hlist_node](https://github.com/torvalds/linux/blob/master/include/linux/list.h)
- [Linux kernel include/linux/hashtable.h](https://github.com/torvalds/linux/blob/master/include/linux/hashtable.h)
- [Zephyr RTOS — Hashmap (separate chaining / open addressing 구현)](https://docs.zephyrproject.org/latest/kernel/data_structures/hashmap.html)
- *Introduction to Algorithms (CLRS)*, 11장 Hash Tables — 체이닝 분석, 부하율
- MISRA C:2012 Rule 21.3 — `<stdlib.h>` 메모리 할당 함수 금지
- [Linus Torvalds on "good taste" — pointer-to-pointer 리스트 삭제 (TED 2016)](https://www.ted.com/talks/linus_torvalds_the_mind_behind_linux)
