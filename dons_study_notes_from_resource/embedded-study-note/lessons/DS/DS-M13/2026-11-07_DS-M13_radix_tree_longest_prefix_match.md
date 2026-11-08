---
id: DS-M13
time: 90분
updated: 2026-11-07
---
# DS-M13 기수 트리와 최장 접두사 매칭(LPM)

## 학습 목표

- 최장 접두사 매칭(LPM, longest prefix match)이 "정확히 같은 키 찾기"(해시, DS-B10)와 왜 다른 문제인지 설명하고, **비트 하나 = 간선 하나**인 이진 트라이(binary trie)로 푼다 (DS-B14 트라이의 알파벳 {0,1} 버전).
- `/0` 기본 경로(default route)에서 `0xFFFFFFFFu << 32` 가 **정의되지 않은 동작(UB)** 이 되는 이유를 C11 조항과 실제 하드웨어 동작(arm64 vs Cortex-M)으로 설명하고, 명시적 분기로 막는다. `/32` 호스트 경로에서 `bit_at(addr, 32)` 도 같은 함정이다.
- 외길을 접은 경로 압축 트라이(Patricia / radix tree)의 **삽입 4가지 경우**와 **삭제 3가지 경우**를 그림으로 그리고, "glue 노드는 자식이 정확히 2개" 불변식으로 노드 수 ≤ 2N−1 을 증명한다.
- CIDR 문자열 파싱, 호스트 비트 정규화, TTL 처리를 갖춘 **패킷 포워딩 테이블**을 만들고, 선형 탐색 참조 모델과 무작위 대조로 검증한다.
- 같은 경로 집합에서 이진 트라이 / Patricia / 선형 탐색의 노드 수·메모리·방문 수·시간을 재서 트레이드오프를 숫자로 말한다.

## 1. 문제

라우터·게이트웨이 펌웨어는 패킷마다 "목적지 IP에 맞는 경로 중 **가장 구체적인(가장 긴) 것**"을 골라야 하고, 이 조회가 초당 수백만 번 일어난다. 면접에서는 "라우팅 테이블을 설계하라", "IP를 CIDR 목록에 매칭하라"로 나오고, 꼬리질문은 거의 항상 "/0 은? /32 는? 하드웨어는 어떻게 하나(TCAM)? Linux는?" 이다. 같은 구조가 MMU 페이지 테이블(고정 stride 기수 트리), Linux 페이지 캐시의 xarray, CAN ID 마스크 필터에도 숨어 있다.

**LPM이 뭔가**: 경로 `10.0.0.0/8 → 포트1`, `10.1.0.0/16 → 포트2`, `0.0.0.0/0 → 업링크` 가 있을 때 `10.1.2.3` 은 세 개 모두에 들어맞는다. 규칙은 **더 긴 prefix 가 이긴다** → `/16` → 포트2. `/len` 은 "주소의 상위 len 비트가 같으면 매치"라는 뜻이고, `/0` 은 아무 비트도 안 보니 모든 주소에 매치(기본 경로), `/32` 는 주소 하나(호스트 경로)다. 해시 테이블은 "정확히 같은 키"만 찾으므로 길이별로 33번 찾아야 한다 — 트라이는 주소 비트를 따라 **한 번 내려가면서** 지나가는 모든 매치를 본다.

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 경로 6개 (4비트 장난감 주소): A=*/0  B=1*/1  C=101*/3  D=1011/4  E=0010/4  F=0001/4</text>
  <text x="20" y="46" fill="currentColor">이진 트라이 — 비트 하나 = 간선 하나</text>
  <text x="400" y="46" fill="currentColor">Patricia — 외길을 접고, 갈림길만 남김</text>
  <g stroke="currentColor" fill="none">
    <path d="M180 72 L90 116"/><path d="M180 72 L270 116"/><path d="M90 130 L60 166"/><path d="M60 180 L30 216"/><path d="M60 180 L90 216"/>
    <path d="M30 230 L30 266"/><path d="M90 230 L90 266"/><path d="M270 130 L240 166"/><path d="M240 180 L270 216"/><path d="M270 230 L300 266"/>
    <line x1="370" y1="40" x2="370" y2="300" stroke-dasharray="4 4"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="126" y="90">0</text><text x="228" y="90">1</text><text x="66" y="150">0</text><text x="36" y="200">0</text><text x="80" y="200">1</text><text x="18" y="252">1</text><text x="96" y="252">0</text>
    <text x="244" y="150">0</text><text x="260" y="200">1</text><text x="290" y="252">1</text>
  </g>
  <g stroke="currentColor" class="f-mut">
    <circle cx="90" cy="123" r="11"/><circle cx="60" cy="173" r="11"/><circle cx="30" cy="223" r="11"/><circle cx="90" cy="223" r="11"/><circle cx="240" cy="173" r="11"/>
  </g>
  <g stroke="currentColor" class="f-ok">
    <circle cx="180" cy="65" r="13" class="f-acc"/><circle cx="270" cy="123" r="13"/><circle cx="270" cy="223" r="13"/><circle cx="300" cy="273" r="13"/><circle cx="30" cy="273" r="13"/><circle cx="90" cy="273" r="13"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="180" y="70">A</text><text x="270" y="128">B</text><text x="270" y="228">C</text><text x="300" y="278">D</text><text x="30" y="278">F</text><text x="90" y="278">E</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M560 82 L470 118"/><path d="M560 82 L650 118"/><path d="M470 144 L430 188"/><path d="M470 144 L510 188"/>
    <path d="M650 144 L650 188"/><path d="M650 214 L690 258"/>
  </g>
  <rect x="520" y="56" width="80" height="26" rx="6" class="f-acc" stroke="currentColor"/><rect x="430" y="118" width="80" height="26" rx="6" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <g class="f-ok" stroke="currentColor">
    <rect x="610" y="118" width="80" height="26" rx="6"/><rect x="390" y="188" width="80" height="26" rx="6"/><rect x="470" y="188" width="80" height="26" rx="6"/><rect x="610" y="188" width="80" height="26" rx="6"/>
    <rect x="650" y="258" width="80" height="26" rx="6"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="560" y="74">A */0</text><text x="470" y="136">glue 00/2</text><text x="650" y="136">B 1/1</text><text x="430" y="206">F 0001/4</text><text x="510" y="206">E 0010/4</text><text x="650" y="206">C 101/3</text>
    <text x="690" y="276">D 1011/4</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="498" y="100">0</text><text x="612" y="100">1</text><text x="436" y="170">0</text><text x="500" y="170">1</text><text x="656" y="170">0</text><text x="676" y="240">1</text>
  </g>
  <text x="10" y="318" fill="currentColor">이진: 노드 11 (경로 6 + 빈 노드 5) / Patricia: 노드 7 (경로 6 + glue 1, F·E 가 비트 2 에서 갈라짐)</text>
  <text x="10" y="340" fill="currentColor">주소 1010 조회: 이진 root→1→10→101 (4 방문, 마지막 경로 C) / Patricia A→B→C (3 방문). 둘 다 답은 C(/3).</text>
</svg>
```

경로 하나는 `(prefix, len, next-hop)`. next-hop 은 `uint16_t` (출력 포트/이웃 번호), `NH_NONE = 0xFFFF` 는 "경로 없음". 노드는 정적 풀 + `uint16_t` 인덱스(0 = 없음)로 malloc 없이 만든다 (DS-B07 인덱스 링크, DS-M02 메모리 풀 참고).

### Part A — 마스크와 이진 트라이

- `pfx_mask(len)` — 상위 len 비트만 1인 마스크. **len = 0 을 명시적으로 처리**해야 한다 (§5에서 왜).
- `bt_insert(t, prefix, len, nh)` — len > 32, `nh == NH_NONE` 은 거절. 호스트 비트가 켜진 prefix(`192.168.1.77/24`)는 **마스킹해 정규화**. 같은 prefix 재등록은 nh 교체. 노드 풀이 모자라면 **아무것도 바꾸지 않고** false (DS-B14의 "검증 → 변경" 순서).
- `bt_lookup(t, addr, &mlen)` — 내려가며 마지막으로 본 경로 = LPM. 일치 길이도 돌려준다.
- 테스트: `/0` 하나만 있는 테이블, `/8 /16 /24 /32` 중첩, 호스트 비트 정규화, 풀 부족, 무작위 2000 경로 × 20000 주소를 선형 탐색과 대조. 매 단계 `bt_check()`.

### Part B — 경로 압축 (Patricia)

- 노드 = `(key, len, nh, child[2])`. 경로가 아닌 갈림길 노드(glue, `nh == NH_NONE`)는 자식이 **정확히 2개**일 때만 존재.
- `pt_insert` — 4가지 경우(빈 링크 / 같은 prefix / 새 prefix 가 조상 / 라벨 중간에서 갈라짐).
- `pt_lookup` — 노드 라벨 전체(`addr & mask(len) == key`)를 비교하며 내려간다.
- `pt_delete` — 자식 2개면 glue 로 강등, 1개면 자식을 끌어올림, 0개면 제거 후 glue 부모 정리.
- 테스트: 위와 같은 중첩 + glue 생성/승격/강등/정리, 풀 부족(갈라짐은 노드 2개 필요), 무작위 삽입·삭제 20000회 동안 **매번** 불변식 + 참조 모델 조회 4개 대조, 마지막에 전부 지워 노드 0개 확인.

### Part C — 라우터 테이블

- `parse_cidr("a.b.c.d/len")` — 옥텟 0..255, len 0..32, 쓰레기 문자 거절, `/len` 이 없으면 `/32`. 호스트 비트 여부를 알려준다.
- `rt_forward(r, pkts, n, out_nh)` — 패킷 배치 포워딩: TTL ≤ 1 이면 버림(ttl_drop), 경로가 없으면 버림(no_route), 아니면 TTL 1 감소 후 `tx[nh]++`.
- 시나리오: 기본 경로 + 중첩 + 호스트 경로 + 정규화 → 포워딩 → 경로 플랩(`/16`, `/0` 삭제) 후 재포워딩.
- 측정: N = 100 / 1000 / 3000 (BGP 비슷한 길이 분포)에서 노드 수·바이트·조회당 방문 수.

## 2. Problem (English)

> **IPv4 longest-prefix-match routing table, no heap.**
>
> A route is `(uint32_t prefix, uint8_t len, uint16_t next_hop)`, `0 <= len <= 32`. `lookup(addr)` returns the next-hop of the matching route with the largest `len`, or `NH_NONE` if none matches. Prefixes with host bits set (e.g. `192.168.1.77/24`) are normalized by masking. Re-adding the same prefix replaces its next-hop.
>
> 1. Implement `uint32_t pfx_mask(uint32_t len)` correctly for **all** `len` in `0..32` without undefined behavior, then a binary trie (one bit per level) on a static node pool: `bt_insert`, `bt_lookup`. A failed insert (pool exhausted) must leave the trie unchanged.
> 2. Implement a path-compressed (Patricia) trie with `pt_insert`, `pt_lookup`, `pt_delete`. Internal non-route nodes must always have exactly two children. Prove the node count is at most `2N - 1`.
> 3. Parse CIDR strings and forward a batch of packets (`TTL <= 1` → drop, no route → drop, else decrement TTL and count per next-hop).
> 4. Validate every structure against a linear-scan reference on randomized inputs.
>
> Example: routes `0.0.0.0/0→7, 10.0.0.0/8→1, 10.1.0.0/16→2, 10.1.2.0/24→3, 10.1.2.3/32→4`: `lookup(10.1.2.3)=4 (/32)`, `lookup(10.1.2.4)=3 (/24)`, `lookup(10.200.0.1)=1 (/8)`, `lookup(11.0.0.1)=7 (/0)`.
>
> Follow-ups: worst-case lookup steps? What does a hardware router use (TCAM)? What does Linux use? How do readers stay safe while the control plane updates routes?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "4/44 passed", exit 1 — TODO 8곳을 채워 44/44 를 만든다
```

TODO: `pfx_mask`, `bt_insert`, `bt_lookup` (Part A) / `pt_insert`, `pt_lookup`, `pt_delete` (Part B) / `parse_cidr`, `rt_forward` (Part C). 주어지는 것: `bit_at`(MSB 기준 비트 i), `clp`(공통 선행 비트 수), Patricia 노드 할당/반납 `pt_new`/`pt_free`, 불변식 검사기 `bt_check`/`pt_check`, 참조 모델 `ref_*`(선형 탐색), 난수 경로 생성기, 하네스. 검사기와 참조 모델은 일부러 **다른 마스크 식** `mask64(len) = (uint32_t)~(0xFFFFFFFFull >> len)` 를 쓴다 — 64비트에서 시프트하므로 len = 32 까지 정의되고, 네 `pfx_mask` 와 독립적인 오라클이 된다.

## 4. 힌트

<details>
<summary>힌트 1 — /0 에서 무엇이 터지나</summary>

`0xFFFFFFFFu << (32 - len)` 에 len = 0 을 넣으면 시프트 양이 32 = 피연산자 폭이다. C11 6.5.7p3: "시프트 양이 음수이거나 승격된 왼쪽 피연산자의 폭 이상이면 동작은 정의되지 않는다." 결과가 0일 거라고 기대하면 안 된다. len == 0 을 먼저 분기로 처리하거나, 64비트에서 시프트하라.

</details>

<details>
<summary>힌트 2 — 이진 트라이 조회는 "마지막으로 본 경로"</summary>

addr 와 매치하는 prefix 는 전부 **addr 의 비트 경로 위**에 있다 (prefix 의 비트열 = addr 의 앞부분이므로). 그러니 루트부터 addr 의 비트를 따라 내려가며 경로가 달린 노드를 만날 때마다 덮어쓰면, 마지막 것이 가장 긴 매치다. 깊이 32 노드에서는 더 내려갈 비트가 없다 — `bit_at(addr, 32)` 를 부르기 전에 멈춰라.

</details>

<details>
<summary>힌트 3 — Patricia 삽입: 공통 길이 c 하나로 경우가 갈린다</summary>

지금 노드 n 과 새 prefix 의 공통 선행 비트 수를 c = min(clp(prefix, key), len, n→len) 로 잡는다. c == n→len 이면 n 의 라벨을 다 통과했으니 (len 도 같으면 교체, 아니면) 다음 비트로 내려간다. c == len (< n→len) 이면 새 prefix 가 n 의 조상. 둘 다 아니면 비트 c 에서 둘이 갈라지므로 glue 노드 `(prefix & mask(c), c)` 가 필요하다.

</details>

<details>
<summary>힌트 4 — 링크 포인터로 부모 갱신을 없앤다</summary>

`uint16_t *link = &t->root` 로 시작해 `link = &n->child[b]` 로 내려가면, "루트를 바꾸는 경우"와 "부모의 자식 칸을 바꾸는 경우"가 `*link = new` 한 줄로 같아진다 (DS-B05의 이중 포인터 삭제와 같은 기법).

</details>

<details>
<summary>힌트 5 — 삭제 후 glue 정리는 한 단계만</summary>

잎을 지우면 부모의 자식 수가 2 → 1 이 된다. 부모가 glue 면 불변식이 깨지니 부모도 자식을 끌어올리고 반납한다. 자식 1개인 노드를 지울 때는 그 자식이 자리를 이어받으므로 부모의 자식 수가 그대로다 — 더 위로 전파되지 않는다.

</details>

## 5. 풀이

### Part A — 마스크: /0 과 시프트 UB

가장 자연스러운 식 `0xFFFFFFFFu << (32 - len)` 은 len = 1..32 에서 맞고 **len = 0 에서 UB** 다. "UB면 그냥 이상한 값이 나오겠지"가 아니라, 실제로 무엇이 나오는지가 CPU·최적화 수준마다 다르다:

| 환경 | `x << 32` 실행 결과 | 결과 |
|---|---|---|
| AArch64 (`LSLV`, 이 Mac) | 시프트 양을 32로 나눈 나머지 사용 → `x << 0` | `0xFFFFFFFF` → `/0` 이 주소 0.0.0.0 하나만 매치 |
| x86 (`SHL r32, cl`) | 시프트 양 하위 5비트만 사용 → `x << 0` | AArch64와 같음 |
| ARMv7-M / Cortex-M (`LSL` 레지스터) | Rm 의 하위 8비트 사용, 32 이상이면 0 | `0x00000000` → 우연히 "맞음" |
| 컴파일러 최적화 | UB 경로는 "일어나지 않는다"고 가정 가능 | 아무거나 |

그래서 이 버그는 **Cortex-M 보드에서는 잘 돌고, 호스트 유닛 테스트나 다른 칩에서만 터지는** 최악의 형태가 된다. 실제로 확인한 것 — `demo_shift_ub.c` (volatile 로 상수 접기를 막아 하드웨어 시프트를 그대로 실행):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_shift_ub.c -o demo_shift_ub && ./demo_shift_ub
cc -std=c11 -O1 -fsanitize=undefined demo_shift_ub.c -o demo_shift_ub_san && ./demo_shift_ub_san
```

```text
len= 0  naive=0xFFFFFFFF  safe=0x00000000  mask64=0x00000000   <-- naive 가 틀림: /0 기본 경로가 0.0.0.0 하나만 매치
len= 1  naive=0x80000000  safe=0x80000000  mask64=0x80000000
len= 8  naive=0xFF000000  safe=0xFF000000  mask64=0xFF000000
len=24  naive=0xFFFFFF00  safe=0xFFFFFF00  mask64=0xFFFFFF00
len=32  naive=0xFFFFFFFF  safe=0xFFFFFFFF  mask64=0xFFFFFFFF
demo_shift_ub.c:10:63: runtime error: shift exponent 32 is too large for 32-bit type 'unsigned int'   (UBSan 빌드)
```

더 무서운 건 레슨 코드에서 `if (len == 0) return 0u;` 한 줄만 지웠을 때다. `-O0` 에서는 기본 경로·마스크 관련 테스트 10개가 FAIL(34/44) 했고, **`-O2` 에서는 출력 0바이트로 exit 133(= 128 + SIGTRAP)으로 죽었다** (버퍼에 쌓인 printf 도 못 내보냄) — 같은 소스가 최적화 수준에 따라 "틀린 답"과 "크래시"로 갈린다. UB는 "값이 이상함"이 아니라 "프로그램 전체의 의미가 없어짐"이다. 상수로 쓰면 컴파일러가 잡아 준다(`0xFFFFFFFFu << 32` → `warning: shift count >= width of type [-Wshift-count-overflow]`) — 하지만 len 이 변수면 런타임 UBSan 만 잡는다.

안전한 식 두 가지 (이 레슨은 둘 다 쓴다 — 하나는 구현, 하나는 검사 오라클):

```c
static uint32_t pfx_mask(uint32_t len) {          /* 분기: 의도가 코드에 보인다 */
    if (len == 0) return 0u;
    if (len >= 32) return 0xFFFFFFFFu;
    return 0xFFFFFFFFu << (32u - len);            /* 시프트 양 1..31 */
}
static uint32_t mask64(uint32_t len) { return (uint32_t)~(0xFFFFFFFFull >> len); }  /* 64비트: 0..32 모두 정의 */
```

같은 함정이 하나 더 있다: `bit_at(addr, i) = (addr >> (31 - i)) & 1` 에 i = 32 를 넣으면 `31u - 32u = 0xFFFFFFFF` 만큼 시프트(UB). 깊이 32 노드(`/32` 호스트 경로)에서 "다음 비트"를 읽으면 그렇게 된다. 레슨 코드에서 `if (i == 32u) break;` 를 지워 보면 이 Mac 에서는 **44/44 가 그대로 통과**했다 (AArch64가 시프트 양을 31로 잘라 우연히 MSB(이 레슨 번호로 비트 0)를 읽고, /32 노드는 자식이 없으니 멈춤). UBSan 빌드에서만 `shift exponent 4294967295 is too large` 가 떴다 — "테스트 통과 ≠ 정의된 동작"의 실례다. CI 에 `-fsanitize=undefined` 빌드를 넣는 이유.

### Part A — 이진 트라이

**불변식** (`bt_check`):

1. 깊이 d 노드 ↔ 길이 d 비트열 하나. 깊이 ≤ 32.
2. 루트가 아닌 노드 중 "경로도 없고 자식도 없는" 노드(죽은 가지)는 없다 — 실패한 삽입이 반쯤 만든 줄기를 남기면 여기서 잡힌다.
3. 루트에서 도달 가능한 노드 수 == `used`, 경로가 달린 노드 수 == `routes`.

**삽입 = 검증 → 변경.** 이미 있는 줄기를 따라 i 비트까지 내려간 뒤, 새로 필요한 노드는 정확히 `len − i` 개다. 이게 `cap − used` 보다 크면 아무것도 건드리지 않고 false. 이렇게 하면 "노드 20개 만들다가 21번째에서 풀 부족" 같은 반쯤 만든 상태가 원천적으로 없다. 테스트: cap = 40 에서 `/32` 하나(노드 32개)를 넣은 뒤 첫 비트부터 다른 `/32` 는 거절(used 33 그대로), 31비트를 공유하는 `/32` 는 노드 1개로 성공.

**조회 추적** — 테이블 `0/0→7, 10/8→1, 10.1/16→2, 10.1.2/24→3, 10.1.2.3/32→4`, 노드 33개 (중첩 경로는 한 줄기를 공유해서 32개 + 루트):

| 주소 | 이진 트라이 방문 | 결과 | Patricia 방문 | 이유 |
|---|---|---|---|---|
| 10.1.2.3 | 33 | 4 (/32) | 5 | 32비트 끝까지 |
| 10.1.2.4 | **30** | 3 (/24) | 5 | `...011` vs `...100` 이 비트 29에서 갈라짐 — /24 노드 뒤로 경로가 없는데도 깊이 29 까지 내려감 |
| 10.200.0.1 | 9 | 1 (/8) | 2 | 비트 8 (200 = `1...`) 에서 줄기 끝 |
| 11.0.0.1 | 8 | 7 (/0) | 2 | 비트 7 에서 10 과 갈라짐 |

(위 숫자는 `g_visits` 로 직접 잰 값.) 이진 트라이의 약점이 보인다: **경로가 없는 외길도 한 칸씩 걷는다**. 10.1.2.4 는 /24 노드 다음에도 쓸모없는 노드 5개를 더 방문했다. 메모리도 같은 이유로 낭비된다 — /32 하나가 노드 최대 32개.

### Part B — Patricia: 외길을 접는다

이진 트라이에서 "경로도 아니고 자식도 1개인" 노드는 정보가 없다 — 그냥 지나가는 길이다. 이걸 없애고 각 노드에 **자기까지의 비트열 전체(key, len)** 를 저장하면 Patricia(경로 압축 기수 트리)가 된다. 노드 = 경로 노드 또는 glue(갈림길) 노드뿐.

**불변식** (`pt_check`):

1. `key` 의 len 아래 비트는 0 (정규화).
2. 자식 c 는 부모보다 길고(`c.len > p.len`), 부모 라벨로 시작하며(`c.key & mask(p.len) == p.key`), 분기 비트가 맞다(`bit_at(c.key, p.len) == 칸 번호`).
3. glue 노드는 자식이 정확히 2개. `/32` 노드는 자식 0개.
4. 도달 노드 수 == `used`, free list 길이 == 남은 칸, 노드 수 ≤ 2·routes − 1.

**노드 ≤ 2N − 1 증명**: 잎에는 glue 가 올 수 없다(자식 2개여야 하므로) → 잎은 모두 경로, 잎 수 L ≤ N. 자식이 2개인 노드 수는 정확히 L − 1 (이진 트리 성질), glue 는 그 일부이므로 glue ≤ L − 1 ≤ N − 1. 전체 = 경로 N + glue ≤ 2N − 1. 이진 트라이가 최악 N·32 노드인 것과 비교하면 **노드 수가 N 에만 비례**한다.

**삽입 4가지 경우** — 링크를 따라 내려가며 `c = min(clp(prefix, n.key), len, n.len)`:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">pt_insert — 위: 전, 아래: 후. 초록 = 새 노드, 점선 = glue</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="44">① 빈 링크 → 새 잎</text><text x="195" y="44">② 같은 (key,len) → nh 교체</text><text x="380" y="44">③ 새 prefix 가 조상</text><text x="560" y="44">④ 라벨 중간에서 갈라짐</text>
    <text x="10" y="60">insert 10.1/16</text><text x="195" y="60">insert 10/7 → 9</text><text x="380" y="60">insert 10/8</text><text x="560" y="60">insert 11/8</text>
  </g>
  <g stroke="currentColor" fill="none">
    <line x1="185" y1="30" x2="185" y2="290" stroke-dasharray="3 4"/><line x1="370" y1="30" x2="370" y2="290" stroke-dasharray="3 4"/><line x1="550" y1="30" x2="550" y2="290" stroke-dasharray="3 4"/>
    <line x1="5" y1="150" x2="735" y2="150" stroke-dasharray="3 4"/><path d="M60 98 L60 122"/><path d="M60 194 L60 222"/>
    <path d="M277 98 L231 122"/><path d="M277 98 L321 122"/><path d="M277 194 L231 222"/><path d="M277 194 L321 222"/><path d="M440 194 L440 222"/>
    <path d="M640 194 L600 222"/><path d="M640 194 L690 222"/>
  </g>
  <rect x="25" y="74" width="70" height="24" rx="5" class="f-mut" stroke="currentColor"/><rect x="35" y="122" width="50" height="20" rx="5" fill="none" stroke="currentColor" stroke-dasharray="3 3"/><rect x="25" y="170" width="70" height="24" rx="5" class="f-mut" stroke="currentColor"/>
  <rect x="20" y="222" width="80" height="24" rx="5" class="f-ok" stroke="currentColor"/><rect x="237" y="74" width="80" height="24" rx="5" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/><rect x="200" y="122" width="62" height="22" rx="5" class="f-mut" stroke="currentColor"/>
  <rect x="290" y="122" width="62" height="22" rx="5" class="f-mut" stroke="currentColor"/><rect x="237" y="170" width="80" height="24" rx="5" class="f-ok" stroke="currentColor"/><rect x="200" y="222" width="62" height="22" rx="5" class="f-mut" stroke="currentColor"/>
  <rect x="290" y="222" width="62" height="22" rx="5" class="f-mut" stroke="currentColor"/><rect x="395" y="74" width="90" height="24" rx="5" class="f-mut" stroke="currentColor"/><rect x="405" y="170" width="70" height="24" rx="5" class="f-ok" stroke="currentColor"/>
  <rect x="395" y="222" width="90" height="24" rx="5" class="f-mut" stroke="currentColor"/><rect x="605" y="74" width="70" height="24" rx="5" class="f-mut" stroke="currentColor"/><rect x="600" y="170" width="80" height="24" rx="5" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="565" y="222" width="70" height="24" rx="5" class="f-mut" stroke="currentColor"/><rect x="655" y="222" width="70" height="24" rx="5" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="60" y="91">10/8</text><text x="60" y="136">빈칸</text><text x="60" y="187">10/8</text><text x="60" y="239">10.1/16</text><text x="277" y="91">10/7 glue</text><text x="231" y="138">10/8</text><text x="321" y="138">11/8</text>
    <text x="277" y="187">10/7 nh9</text><text x="231" y="238">10/8</text><text x="321" y="238">11/8</text><text x="440" y="91">10.1.2/24</text><text x="440" y="187">10/8</text><text x="440" y="239">10.1.2/24</text>
    <text x="640" y="91">10/8</text><text x="640" y="187">10/7 glue</text><text x="600" y="239">10/8</text><text x="690" y="239">11/8</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="276">c = n.len = 8 &lt; len → 내려감</text><text x="195" y="276">c = len = n.len = 7</text>
    <text x="380" y="276">c = 8 = len &lt; 24</text><text x="560" y="276">clp(10,11) = 7 &lt; 8</text>
  </g>
</svg>
```

| 경우 | 조건 | 처리 | 새 노드 |
|---|---|---|---|
| ① | `*link == 0` | 새 잎을 링크에 단다 | 1 |
| ② | `c == n.len == len` | `n.nh = nh` (glue 였으면 경로로 승격, routes++) | 0 |
| ②' | `c == n.len < len` | `link = &n.child[bit_at(prefix, n.len)]` 로 계속 | — |
| ③ | `c == len < n.len` | 새 노드 m, `m.child[bit_at(n.key, len)] = n`, `*link = m` | 1 |
| ④ | `c < len` 그리고 `c < n.len` | glue `g = (prefix & mask(c), c)`, g 의 두 칸에 n 과 새 잎 | **2** |

④ 에서 비트 c 는 두 키에서 **반드시 다르다** (c 가 공통 선행 비트 수이고 c < 32 이므로). 그래서 glue 의 두 칸이 겹치지 않는다. ④ 는 노드가 2개 필요하므로 **먼저 `cap − used ≥ 2` 를 확인**해야 한다 — glue 만 할당하고 잎에서 실패하면 자식 1개짜리 glue(불변식 위반) 또는 누수가 남는다. 테스트: cap = 2 에서 `10/8` 다음 `11/8` 은 거절(used 1 그대로).

**`clp` 와 CLZ**: 공통 선행 비트 수는 `__builtin_clz(a ^ b)` 한 번이다. 단 `__builtin_clz(0)` 은 UB(GCC/Clang 문서)라 `a == b` 를 따로 32로 처리한다. Cortex-M3 이상은 `CLZ` 명령이 0 입력에 32를 돌려주고 CMSIS `__CLZ()` 로 쓸 수 있다 — 하지만 이식성 있는 C 코드에서는 0 검사를 남겨 둔다. 그리고 clp 는 두 키의 "의미 없는" 하위 비트까지 비교하므로 반드시 `min(len, n.len)` 으로 잘라야 한다. 이 한 줄(`if (c > lim) c = lim;`)을 지우면 29/44 로 떨어지고 UBSan 이 `shift exponent 4294967295` 를 보고한다 (c = 32 인 채로 `bit_at(.., 32)` 를 부름).

**삽입 추적** (테스트 순서): 

| 삽입 | 경우 | 트리 (루트부터 한 줄기) | used |
|---|---|---|---|
| 10.1.2.0/24 | ① 빈 루트 | 10.1.2/24 | 1 |
| 10.0.0.0/8 | ③ c = min(clp=15, 8, 24) = 8 = len | 10/8 → 10.1.2/24 | 2 |
| 10.1.2.3/32 | ②' 두 번 → ① | 10/8 → 10.1.2/24 → 10.1.2.3/32 | 3 |
| 10.1.0.0/16 | ②' (10/8) → ③ (10.1.2/24 위에) | 10/8 → 10.1/16 → 10.1.2/24 → .3/32 | 4 |
| 0.0.0.0/0 | ③ 루트에서 c = 0 = len | 0/0 → 10/8 → … | 5 |

glue 없이 노드 5개 = 경로 5개. 같은 데이터가 이진 트라이에선 33개였다.

**조회**: 노드마다 라벨 전체를 비교한다 — `(addr & mask(n.len)) != n.key` 면 멈춘다(더 내려가도 이 라벨로 시작하는 것뿐이므로). 맞으면 경로일 때 기억하고, `n.len == 32` 면 멈추고, 아니면 `child[bit_at(addr, n.len)]`. 이진 트라이는 비트마다 "한 비트 비교", Patricia 는 노드마다 "최대 32비트 비교 한 번" — 건너뛴 비트를 나중에 한꺼번에 확인하는 셈이다. (원래 Morrison 의 PATRICIA 는 내려갈 때 비트를 확인하지 않고 끝에서 한 번만 비교하는 방식이지만, LPM 에서는 중간 경로를 모두 봐야 하므로 이 레슨처럼 노드마다 비교하는 변형을 쓴다 — BSD radix 와 Linux fib_trie 도 각자의 방식으로 중간 prefix 를 처리한다.)

**삭제 3가지 경우**:

```svg
<svg viewBox="0 0 740 240" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">pt_delete — 왼쪽: 전, 오른쪽: 후. 경고색 = 지울 경로, 점선 = glue</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="44">(a) 자식 2개 → glue 로 강등</text><text x="255" y="44">(b) 자식 1개 → 자식 끌어올림</text><text x="500" y="44">(c) 잎 + glue 부모 → 둘 다 반납</text>
  </g>
  <g stroke="currentColor" fill="none">
    <line x1="245" y1="30" x2="245" y2="200" stroke-dasharray="3 4"/><line x1="490" y1="30" x2="490" y2="200" stroke-dasharray="3 4"/><path d="M54 86 L27 124"/><path d="M54 86 L83 124"/><path d="M188 86 L161 124"/><path d="M188 86 L215 124"/>
    <path d="M300 86 L300 124"/><path d="M556 86 L528 124"/><path d="M556 86 L583 124"/>
  </g>
  <defs><marker id="m13d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10z" fill="currentColor"/></marker></defs>
  <g stroke="currentColor" fill="none" stroke-width="2" marker-end="url(#m13d)"><path d="M112 100 L132 100"/><path d="M350 100 L382 100"/><path d="M615 100 L645 100"/></g>
  <rect x="18" y="62" width="72" height="24" rx="5" class="f-warn" stroke="currentColor"/><rect x="4" y="124" width="46" height="22" rx="5" class="f-mut" stroke="currentColor"/><rect x="60" y="124" width="46" height="22" rx="5" class="f-mut" stroke="currentColor"/>
  <rect x="150" y="62" width="76" height="24" rx="5" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/><rect x="138" y="124" width="46" height="22" rx="5" class="f-mut" stroke="currentColor"/><rect x="192" y="124" width="46" height="22" rx="5" class="f-mut" stroke="currentColor"/>
  <rect x="265" y="62" width="70" height="24" rx="5" class="f-warn" stroke="currentColor"/><rect x="265" y="124" width="70" height="24" rx="5" class="f-mut" stroke="currentColor"/><rect x="395" y="62" width="80" height="24" rx="5" class="f-mut" stroke="currentColor"/>
  <rect x="520" y="62" width="72" height="24" rx="5" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/><rect x="505" y="124" width="46" height="22" rx="5" class="f-mut" stroke="currentColor"/><rect x="560" y="124" width="46" height="22" rx="5" class="f-warn" stroke="currentColor"/>
  <rect x="655" y="62" width="70" height="24" rx="5" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="monospace" font-size="12">
    <text x="54" y="79">10/7 nh9</text><text x="27" y="139">10/8</text><text x="83" y="139">11/8</text><text x="188" y="79">10/7 glue</text><text x="161" y="139">10/8</text><text x="215" y="139">11/8</text>
    <text x="300" y="79">10/8</text><text x="300" y="141">10.1/16</text><text x="435" y="79">10.1/16</text><text x="556" y="79">10/7 glue</text><text x="528" y="139">10/8</text><text x="583" y="139">11/8</text><text x="690" y="79">10/8</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="176">노드 수 그대로 (3)</text><text x="10" y="194">nh = NH_NONE 만</text><text x="255" y="176">*link = 자식, 노드 반납</text><text x="255" y="194">부모 자식 수 변화 없음</text>
    <text x="500" y="176">11/8 반납 → glue 자식 1개</text><text x="500" y="194">→ glue 도 반납, 10/8 이 올라옴 (3→1)</text><text x="10" y="222">삭제 경로를 찾을 때 glue 와 일치하면 false — glue 는 "경로"가 아니다 (10/7 삭제는 (a) 후엔 실패).</text>
  </g>
</svg>
```

(c) 에서 정리는 **한 단계만** 하면 된다. 잎을 지우면 부모의 자식 수만 2 → 1 로 줄고, 부모 glue 를 끌어올려도 조부모의 자식 수는 그대로이기 때문이다. 그래서 전체 경로 스택이 아니라 부모 링크 `plink` 하나만 기억한다. 이 정리 코드를 빼면 40/44 — `pt_check` 의 "glue 는 자식 2개" 검사가 바로 잡는다.

**무작위 테스트** 실제 출력 (경로 수가 300 근처에서 균형을 이루도록 삽입 확률을 조절, 삭제 대상의 1/4 은 없는 경로):

```text
[A] binary trie: 968 routes -> 5674 nodes, 20000 lookups vs linear: match
[B] patricia random ops: 11865 inserts, 6884 deletes, 1251 misses, drained 246: match
```

`gen_dense` 는 대부분 `10.0~3.x.x` 안에서 len 0..32 를 균등하게 뽑아 **/0, /32, 깊은 중첩, 같은 prefix 재등록, 호스트 비트**를 모두 자주 만든다. 2000번 삽입이 968개 경로로 줄어든 것은 짧은 prefix 가 정규화 후 자주 겹치기 때문이다 (예: 모든 `/0` 은 하나).

### 복잡도와 결정성

| | 조회 최악 | 조회 평균 (측정) | 삽입/삭제 | 노드 수 | 노드 크기 |
|---|---|---|---|---|---|
| 선형 탐색 | N 비교 | N | O(1) / O(N) | — (경로당 8 B) | 8 B |
| 이진 트라이 | **33 노드** (W + 1, W = 32) | 13.6~14.1 | O(W) | ≤ 1 + N·W | 6 B |
| Patricia | **33 노드** (len 이 엄격히 증가) | 5.8~8.3 | O(W) | ≤ 2N − 1 | 12 B |

두 트라이 모두 조회 상한이 **N 과 무관한 상수 33** 이다 — 실시간 경로(ISR 하반부, 패킷 처리 루프)에서 "최악 몇 사이클"을 말할 수 있다는 뜻이다. Patricia 의 깊이도 최악 33 인 이유: 루트에서 잎까지 len 이 엄격히 증가하고 0..32 사이이므로. 평균은 경로 집합에 따라 대략 log₂ N 근처다.

### Part C — CIDR 파싱과 포워딩

**파서는 신뢰할 수 없는 입력을 다룬다** (설정 파일, CLI, 원격 관리 프로토콜). 규칙: 옥텟은 숫자 1~3자리이고 ≤ 255, 점 정확히 3개, `/` 뒤 숫자 1~2자리이고 ≤ 32, 끝에 다른 문자 없음, 실패하면 **출력 인자를 건드리지 않음**. 테스트한 거절 사례: `256.0.0.0/8`, `10.0.0/8`, `10.0.0.0/33`, `10.0.0.0/`, `10..0.0/8`, `10.0.0.0/8x`, 빈 문자열, `a.b.c.d/8`, `1.2.3.4.5/8`, `10.0.0.0/-1`, `1234.0.0.0/8`, `10.0.0.0/100`, 앞뒤 공백. `atoi`/`sscanf("%u.%u.%u.%u/%u")` 는 쓰레기 문자와 범위 초과를 조용히 넘긴다 — 직접 자릿수를 세는 이유.

**호스트 비트 정책**: `192.168.1.77/24` 를 받으면 ① 마스킹해 `192.168.1.0/24` 로 저장(이 레슨, BSD `route` 계열의 관대한 방식) 또는 ② 거절(Linux 커널: `net/ipv4/fib_trie.c` 의 `fib_valid_key_len()` 이 `"Invalid prefix for given prefix length"` 로 -EINVAL). 이 레슨은 자료구조 쪽은 항상 정규화하고, 파서가 `host_bits` 를 알려줘 라우터가 `normalized` 카운터를 올린다(로그로 경고할 수 있게). 참고로 그 Linux 함수의 검사식은 `(plen < KEYLENGTH) && (key << plen)` — **plen == 32 일 때 `key << 32` 를 피하려고 조건을 먼저 둔** 것이다. 같은 UB 함정을 커널도 명시적으로 피한다.

**포워딩 시나리오** (테스트 그대로):

| 패킷 dst / TTL | 결과 | 이유 |
|---|---|---|
| 10.1.2.3 / 64 | nh 3 | /32 호스트 경로 |
| 10.1.2.4 / 64 | nh 2 | /16 (/24 는 없음) |
| 10.9.9.9 / 64 | nh 1 | /8 |
| 192.168.1.200 / 64 | nh 4 | `192.168.1.77/24` 가 정규화되어 매치 |
| 8.8.8.8 / 64 | nh 0 | /0 기본 경로 |
| 10.1.2.3 / 1 | 버림 (ttl_drop) | TTL 만료 (ICMP Time Exceeded 대상) |
| 192.168.2.1 / 64 | nh 0 | 기본 경로 |
| 10.1.255.255 / 2 | nh 2, TTL → 1 | 전달 후 TTL 1 |

그 뒤 `rt_del("10.1.99.99/16")` (정규화되어 10.1/16 삭제) 와 `rt_del("0.0.0.0/0")` → 10.1.2.4 는 /8(nh 1)로 떨어지고, 8.8.8.8 은 no_route 로 버려진다. 실제 라우터에서 "기본 경로가 사라지면 외부 트래픽 전부 블랙홀"이 정확히 이 모습이다.

TTL 을 경로 조회보다 먼저 보는 이유: 어차피 버릴 패킷에 조회 비용을 쓰지 않는다. (RFC 1812 는 TTL 감소와 만료 처리를 규정하고, ICMP 생성은 이 레슨 범위 밖이다.)

### Part C — 측정

같은 경로 집합(BGP 비슷한 분포: /21~/24 62%, /13~/20 20%, /25~/31 10%, /32 5%, /8~/12 3%, + /0 기본 경로)에 대해, 조회 주소 10만 개(절반은 기존 경로 안, 절반은 완전 무작위). `solution.c` 실제 출력:

```text
[C]     N routes | binary trie nodes/bytes  | patricia nodes/bytes     | visits/lookup avg(max): binary, patricia, linear
[C]   100    101 |   1151 nodes     6906 B |    192 nodes     2304 B | 13.6(33), 5.8(12), 101
[C]  1000    994 |   8405 nodes    50430 B |   1810 nodes    21720 B | 13.9(33), 7.5(15), 994
[C]  3000   2943 |  21140 nodes   126840 B |   5217 nodes    62604 B | 14.1(33), 8.3(17), 2943
```

| N (경로) | 이진 노드 / 경로 | Patricia 노드 / 경로 | Patricia ÷ 이진 (바이트) | 방문 avg: 이진 / Patricia |
|---|---|---|---|---|
| 101 | 11.4 | 1.90 | 33% | 13.6 / 5.8 |
| 994 | 8.5 | 1.82 | 43% | 13.9 / 7.5 |
| 2943 | 7.2 | 1.77 | 49% | 14.1 / 8.3 |

- Patricia 노드는 경로당 1.77~1.90개 — 이론 상한 2 바로 아래(glue 가 거의 경로 수만큼). 노드가 2배 크지만(12 B vs 6 B) 노드 수가 4~6배 적어 **메모리는 절반 이하**.
- 이진 트라이의 경로당 노드가 N 이 클수록 줄어드는 건 위쪽 줄기를 더 많이 공유하기 때문이다.
- 이진 트라이 최대 방문은 항상 33 (어떤 /32 경로 주소). Patricia 최대는 12~17.
- 평균 방문이 14 근처에서 거의 안 변하는 이유: 주소의 절반이 완전 무작위라 위쪽 8비트 근처에서 끝나고, 나머지 절반은 /24 근처까지 내려간다.

시간 (`demo_lpm_bench.c` — solution.c 를 `#define main solution_main` 으로 include 해서 같은 코드로 잼. 이 Mac, Apple Silicon, `-O2`, 조회 1회당 ns, 같은 실행을 두 번):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_lpm_bench.c -o demo_lpm_bench && ./demo_lpm_bench
```

```text
    N routes |    linear ns    binary ns  patricia ns | checksum
  100    101 |         43.2         28.0         22.3 | same
 1000    994 |        280.1         29.7         31.3 | same
 3000   2943 |        731.0         42.5         43.9 | same
    N routes |    linear ns    binary ns  patricia ns | checksum
  100    101 |         35.8         28.3         21.7 | same
 1000    994 |        369.9         30.6         29.0 | same
 3000   2943 |        740.1         31.7         37.1 | same
```

절대 ns 가 아니라 비율을 본다: 선형은 N 에 비례해 커지고(100 → 3000 에서 약 17~21배), 트라이 둘은 N 과 거의 무관하게 20~45 ns 에 머문다. 두 트라이 차이는 실행마다 뒤집힐 만큼 작다 — 방문 수는 Patricia 가 절반이지만 노드마다 마스크 계산·32비트 비교를 하고, 이진 트라이는 노드가 6 B 라 캐시 라인 하나에 10개씩 들어가고 할당 순서대로 한 줄기가 연속 배치되기 때문이다. N = 100 정도면 선형도 크게 나쁘지 않다(분기 예측이 잘 되는 단순 루프) — **경로 수십 개짜리 MCU 게이트웨이라면 정렬 배열 선형 탐색이 정답**일 수 있다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution   # 44/44 passed (약 0.5초)
cc -std=c11 -g -O1 -fsanitize=address,undefined solution.c -o solution_asan && ./solution_asan   # ASan/UBSan 깨끗
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `mask = ~0u << (32 - len)` | len = 0 이면 시프트 32 = UB. arm64/x86 에선 `0xFFFFFFFF`(기본 경로가 0.0.0.0 만 매치), Cortex-M 에선 우연히 0, `-O2` 에선 이 레슨 코드가 SIGTRAP 으로 죽음 | `len == 0 ? 0 : ~0u << (32 - len)` 또는 `(uint32_t)~(0xFFFFFFFFull >> len)` |
| `/32` 노드에서 `bit_at(addr, 32)` 로 다음 비트 읽기 | 시프트 양 `31u - 32u` = 0xFFFFFFFF, UB. 이 Mac 에선 테스트 통과 — UBSan 만 잡음 | 깊이/len 이 32 면 먼저 멈춤 |
| `__builtin_clz(a ^ b)` 에 같은 키 | `clz(0)` 은 UB | `x == 0 ? 32 : clz(x)` |
| clp 를 `min(len, n.len)` 으로 자르지 않음 | 하위 "의미 없는" 비트까지 같다고 판단해 c = 32 → 잘못된 경우 선택 + `bit_at(.., 32)` (실측 29/44) | `if (c > lim) c = lim;` |
| 정규화 없이 저장 (`192.168.1.77/24` 그대로) | `addr & mask == key` 비교가 영원히 거짓 → 경로가 있는데 매치 안 됨 | 삽입·삭제 입구에서 `prefix &= mask(len)` |
| 조회에서 첫 매치에서 멈춤 | 그건 "최단" 매치. /0 이 루트라 모든 주소가 기본 경로로 감 | 끝까지 내려가며 마지막 경로를 기억 |
| 이진 트라이 삽입 중 풀 부족 → 그때 false | 반쯤 만든 죽은 가지가 남아 노드 누수 | 필요한 노드 수(len − i)를 먼저 세고 확인 |
| 갈라짐(④)에서 glue 만 할당 후 잎 할당 실패 | 자식 1개 glue 또는 누수 | `cap − used >= 2` 를 먼저 확인 |
| 삭제 후 glue 부모 정리 누락 | 자식 1개짜리 glue 가 남아 노드 ≤ 2N−1 깨짐, 메모리 누수 (실측 40/44) | 잎 삭제 시 부모가 glue 면 끌어올림 |
| glue 를 경로처럼 삭제 허용 | 존재하지 않는 경로 삭제가 성공 → 제어 평면과 데이터 평면 불일치 | `nh == NH_NONE` 이면 false |
| `sscanf("%u.%u.%u.%u/%u")` 로 파싱 | `300.1.1.1/8`, `1.2.3.4/8garbage` 를 통과시킴 | 자릿수·범위·끝 문자 직접 검사 |

## 8. 면접 Q&A

<details>
<summary>Q1. LPM을 해시 테이블로 할 수는 없나요?</summary>

길이별로 해시 테이블을 하나씩 두고(최대 33개) 긴 길이부터 `addr & mask(len)` 을 찾으면 된다 — 최악 33번 해시 조회. 개선은 **길이에 대한 이진 탐색**(Waldvogel 등, SIGCOMM 1997): 각 길이 테이블에 "더 긴 prefix 가 있다"는 마커를 넣고 이진 탐색해 log₂ 33 ≈ 5~6번에 끝낸다 (마커에는 미리 계산한 최선 매치를 달아 되돌아가지 않게 함). 실제로 존재하는 길이만 테이블로 두면 더 줄어든다. 트라이 대비 장점은 조회 수가 적다는 것, 단점은 해시 충돌로 최악 시간이 덜 결정적이고 갱신이 복잡하다는 것.

</details>

<details>
<summary>Q2. Patricia 노드 수 상한과 조회 최악 단계는? 이진 트라이와 비교해 보세요.</summary>

glue 는 자식 2개이므로 잎이 될 수 없다 → 잎은 전부 경로(L ≤ N). 자식 2개 노드는 L − 1개, glue 는 그 부분집합 → 전체 ≤ N + (N − 1) = 2N − 1. 이진 트라이는 경로당 최대 32개라 1 + 32N. 조회는 둘 다 루트에서 잎까지 len 이 엄격히 증가하므로 최대 33 노드 — N 과 무관한 상수 상한이라 실시간 분석이 쉽다. 측정(N ≈ 3000): 이진 21140 노드 / 평균 14.1 방문, Patricia 5217 노드 / 평균 8.3 방문.

</details>

<details>
<summary>Q3. 하드웨어 라우터/스위치는 LPM을 어떻게 하나요? (TCAM)</summary>

**TCAM**(ternary content-addressable memory): 각 엔트리 비트가 0/1/X(don't care) 셋 중 하나이고, 입력 주소를 **모든 엔트리와 한 사이클에 병렬 비교**한다. 여러 엔트리가 매치하면 우선순위 인코더가 가장 낮은 인덱스를 고르므로, 엔트리를 **prefix 길이 내림차순으로 배치**해 두면 첫 매치 = 최장 매치. 장점: 조회 1 사이클, 결정적. 단점: 비트당 트랜지스터가 많아 면적·전력이 크고 비싸며, 새 경로 삽입 시 길이 순서를 지키려고 엔트리를 옮겨야 한다(길이별 빈칸을 남겨 두는 배치로 이동을 줄임). SRAM 기반 대안으로 **DIR-24-8**(Gupta·Lin·McKeown, INFOCOM 1998): 상위 24비트로 2^24 엔트리 표를 직접 인덱싱하고 /24 보다 긴 경로만 2단계 표로 → 메모리 접근 1~2회. 연습 2의 stride-8 멀티비트 트라이가 같은 계열이다. 임베디드 쪽 친척: STM32 bxCAN 필터 뱅크의 identifier-mask 모드는 "ID & mask == filter" 를 하드웨어로 비교하는 작은 ternary 매처다.

</details>

<details>
<summary>Q4. Linux 커널은 IPv4 라우팅 조회에 무엇을 쓰나요?</summary>

`net/ipv4/fib_trie.c` 의 **LC-trie**(level-compressed + path-compressed trie, Nilsson & Karlsson 1999). Patricia 처럼 외길을 접고(path compression), 거기에 더해 꽉 찬 서브트리를 노드 하나의 2^bits 칸 배열로 바꾼다(level compression) — 노드마다 "몇 번째 비트부터(pos) 몇 비트(bits)를 인덱스로 쓰는지"를 갖는 적응형 멀티비트 트라이다. 그래서 깊이가 Patricia 보다 훨씬 얕다. 조회는 RCU 로 보호되어 읽기 쪽에 락이 없다. 구조는 `/proc/net/fib_trie` 로 볼 수 있다. 입력 검증은 `fib_valid_key_len()` 에서 len > 32 와 호스트 비트(`(plen < KEYLENGTH) && (key << plen)` — 32 시프트 UB를 조건으로 피함)를 거절한다. 역사적으로 IPv4 라우트 캐시(흐름별 해시)가 있었지만 3.6 에서 제거되었다 — 캐시는 DoS(무작위 주소 폭주)에 약하고 무효화가 어렵다. BSD 계열은 `net/radix.c` 의 radix 트리(Sklower 1991)를 쓴다.

</details>

<details>
<summary>Q5. 포워딩 경로(ISR/다른 코어)가 조회하는 동안 제어 평면이 경로를 바꾸면?</summary>

삽입은 **새 노드를 완전히 초기화한 뒤 링크 하나를 바꾸는 단일 저장**으로 끝나게 짜면 읽기 쪽은 옛 트리나 새 트리 중 하나를 본다 — 이 레슨의 ③·④ 가 그 순서다(`t->n[g].child[..] = ...` 를 다 쓴 뒤 `*link = g`). 실제 동시 실행에서는 그 마지막 저장을 release 저장(C11 `atomic_store_explicit(..., memory_order_release)`, 커널의 `rcu_assign_pointer`)으로 해야 다른 코어가 덜 초기화된 노드를 보지 않는다. 삭제가 더 어렵다: 링크를 끊어도 이미 그 노드에 들어가 있는 읽기 쪽이 있을 수 있으므로 **유예 기간(grace period)** 뒤에 반납해야 한다(RCU, epoch). 단일 코어 MCU 라면 단순하게 ① 짧은 임계 구역(조회가 최대 33 단계로 상한이 있으므로 길이도 상한이 있음) ② 테이블 두 벌을 두고 새 벌을 다 만든 뒤 포인터 교체(double buffering) — 메모리 2배 대신 읽기 쪽 비용 0.

</details>

<details>
<summary>Q6. /0 과 /32 에서 무엇을 조심했나요? 왜 그냥 "~0u &lt;&lt; (32 - len)" 이면 안 되나요?</summary>

C11 6.5.7p3: 시프트 양이 왼쪽 피연산자 폭 이상이면 UB. len = 0 이면 32 시프트. AArch64 `LSLV`·x86 `SHL` 은 시프트 양을 32로 나눈 나머지만 써서 `~0u` 가 그대로 나오고(기본 경로가 0.0.0.0 만 매치), ARMv7-M `LSL`(레지스터)은 하위 8비트를 써서 32 이상이면 0 — **보드에서는 맞고 호스트 테스트에서 틀리는** 버그가 된다. 이 레슨 코드에서 분기를 지우면 `-O0` 에선 10개 FAIL, `-O2` 에선 SIGTRAP 으로 즉사했다. /32 쪽은 "다음 비트" `bit_at(addr, 32)` 가 `31u - 32u` 시프트라 같은 UB — 테스트는 통과하고 UBSan 만 잡았다. 그래서 둘 다 명시적 분기 + CI 의 UBSan 빌드. 64비트 시프트(`~(0xFFFFFFFFull >> len)`)도 정답이지만, 의도가 드러나는 분기를 더 선호하는 팀이 많다.

</details>

<details>
<summary>Q7. IPv6 라면? 메모리를 더 줄이거나 조회를 더 빠르게 하려면?</summary>

IPv6 는 키가 128비트라 이진 트라이는 경로당 최대 128 노드, 조회 129 단계 — Patricia/LC-trie 의 경로 압축이 훨씬 중요해진다(실제 IPv6 경로는 /64 이하가 대부분이라 상위 64비트만 트라이로 보는 하드웨어도 있다). 속도는 stride 를 키운 멀티비트 트라이(연습 2: stride 8 이면 IPv4 조회 ≤ 4 접근)와 prefix expansion, 메모리는 노드 압축(Tree Bitmap, Eatherton 등 2004 — 자식 배열을 비트맵 + popcount 로 압축)이 정석이다. 측정해 보면 stride-8 은 188 경로에 노드 128개 = 196 KB 로, 같은 수의 Patricia(경로당 12 B × 약 2 = 4~5 KB)보다 40배 이상 크다 — 속도와 메모리를 맞바꾼다. MMU 페이지 테이블이 바로 고정 stride 기수 트리다(AArch64 4 KB granule: 레벨당 9비트, 48비트 VA 에 4단계, 상위 레벨의 block 디스크립터 = 짧은 prefix).

</details>

## 9. 추가 연습

### 연습 1 — 이진 트라이 삭제와 가지치기

Part A 이진 트라이에 free list 를 붙이고 `bt_delete(prefix, len)` 를 구현하라. 지운 뒤 "경로도 자식도 없는" 노드를 아래에서부터 떼어 반납해야 한다. 무작위 삽입/삭제 20000회 동안 매번 불변식 + 선형 참조 대조, 마지막에 전부 지웠을 때 루트 하나만 남는지.

<details>
<summary>답 보기</summary>

내려가며 노드 인덱스를 `path[0..len]` 에 기록한다(깊이 ≤ 32 라 스택 배열 33칸이면 충분, 재귀 없음). nh 를 지운 뒤 d = len 부터 위로: 노드가 경로도 자식도 없으면 부모의 `child[bit_at(p, d − 1)]` 을 0 으로 하고 반납, 아니면 멈춤. **루트(d = 0)는 절대 반납하지 않는다** — `/0` 경로를 지워도 루트는 남는다. `10/8` 과 `10.1.2.3/32` 가 있을 때 /32 를 지우면 /8 아래 24개가 반납되어 33 → 9 노드.

```text
[1] binary trie insert/delete 20000 ops vs linear, drained to root only: ok
```

</details>

### 연습 2 — stride-8 멀티비트 트라이 (prefix expansion)

노드 하나 = 256칸 배열(`{nh, plen, child}`), 주소를 바이트 단위로 4단계 내려가는 트라이를 만들어라. /8 의 배수가 아닌 길이는 **확장(controlled prefix expansion)**: `/len` 은 단계 `d = (len−1)/8` 의 2^(8(d+1) − len) 칸에 펼쳐 쓴다. 삽입 순서와 무관하게 짧은 prefix 가 긴 prefix 를 덮어쓰면 안 된다.

<details>
<summary>답 보기</summary>

칸마다 `plen` 을 저장하고 `plen <= len` 일 때만 덮어쓴다(같으면 교체). 조회는 단계마다 `nh` 가 있으면 기억하고 `child` 로 내려감 — 메모리 접근 최대 4번. `/0` 은 단계 0 의 256칸 전부, `/7` 은 2칸, `/25` 는 단계 3 의 128칸. 확장의 대가는 메모리: 노드가 1536 B.

```text
[2] stride-8 multibit: 188 routes -> 128 nodes (196608 B), 50000 lookups avg 2.13 max 4 accesses: match
```

삭제가 어려운 이유: 펼친 칸을 지울 때 그 칸을 원래 덮던 **더 짧은 prefix** 로 되돌려야 하므로 그 값을 다시 찾아야 한다(보통 제어 평면이 Patricia 같은 정확한 트리를 따로 갖고, 데이터 평면 표를 다시 계산한다 — 실제 라우터의 RIB/FIB 분리).

</details>

### 연습 3 — longer-prefixes 열거

`show ip route 10.0.0.0/14 longer-prefixes` 처럼 주어진 prefix 안에 들어가는 모든 경로를 **(prefix, len) 오름차순**으로 나열하라. 개수는 다 세고 기록은 `max` 까지.

<details>
<summary>답 보기</summary>

prefix 노드까지 내려간 뒤 서브트리를 **전위 순회, 0 쪽 먼저**. 전위 순회가 곧 정렬 순서인 이유: 조상(짧은 prefix)은 같은 시작 주소의 후손보다 먼저 나오고(주소 같으면 len 오름차순), 0 서브트리의 모든 주소는 1 서브트리보다 작다. 키는 내려가며 `key | (b << (31 − depth))` 로 복원한다. 선형 테이블을 걸러 삽입 정렬한 결과와 4개 질의(/0 포함 — 결과 = 전체 테이블)에서 일치. DS-B14 자동완성의 "후보 다 세고 max 까지 기록" 규약과 같다.

```text
[3] longer-prefixes == filtered+sorted linear table (4 queries, 221 routes): ok
```

</details>

### 연습 4 — IP 범위 → 최소 CIDR 목록

방화벽 설정에서 `10.0.0.5 ~ 10.0.0.20` 같은 범위를 최소 개수의 CIDR 로 바꿔라 (LeetCode 751 변형). `0.0.0.0 ~ 255.255.255.255` 도 처리할 것.

<details>
<summary>답 보기</summary>

그리디: 현재 시작 a 에서 ① a 의 최하위 1비트(`a & -a`, a = 0 이면 2^32)가 정렬 한계, ② 남은 크기를 넘지 않을 때까지 반으로. 크기와 끝은 **64비트로** 계산한다 — 전체 범위는 크기 2^32 라 `uint32_t` 에 안 들어가고 `hi + 1` 이 0 으로 넘친다. 결과: `10.0.0.5/32, .6/31, .8/29, .16/30, .20/32` (5개). 독립 검증으로 [0, 2^32) 를 재귀 반분해 "범위 안에 완전히 든 가장 큰 블록"만 모으는 방법과 3000개 무작위 범위에서 목록이 같은지, 빈틈·겹침 없이 정확히 덮는지 확인하고, 일부는 이진 트라이에 넣어 lo−1 / lo / hi / hi+1 조회로 경계를 검사했다. 블록 수 상한은 62 (`1 ~ 0xFFFFFFFE` 가 31 + 31 블록).

```text
[4] range->CIDR greedy == recursive split for 3000 random ranges (max 43 blocks): ok
```

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 16/16 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined practice.c -o practice_asan && ./practice_asan   # 깨끗
```

## 10. 핵심 정리

- LPM = 매치하는 prefix 중 가장 긴 것. 매치하는 prefix 는 전부 주소의 비트 경로 위에 있다 → 트라이를 한 번 내려가며 **마지막 경로**를 기억.
- `~0u << (32 − len)` 은 len = 0 에서 UB (C11 6.5.7p3). arm64 는 `~0u`, Cortex-M 은 0, `-O2` 는 SIGTRAP 까지 실측 — 명시적 분기 또는 64비트 시프트. `/32` 에서 `bit_at(addr, 32)` 도 같은 UB.
- 이진 트라이: 노드 6 B, 경로당 7~11 노드, 조회 ≤ 33. 삽입은 필요한 노드 수를 먼저 세는 검증 → 변경.
- Patricia: 노드에 (key, len) 라벨, glue 는 자식 정확히 2개 → 노드 ≤ 2N − 1 (실측 경로당 1.8). 삽입 4경우(빈 링크/교체/조상/갈라짐), 삭제 3경우(강등/끌어올림/잎+glue 정리).
- `c = min(clp(a, b), len, n.len)`, `clp` 는 `clz(a ^ b)` + 0 검사. 링크 포인터(`uint16_t *link`)로 루트·자식 갱신을 한 줄로.
- 호스트 비트: 정규화(이 레슨) vs 거절(Linux `fib_valid_key_len`). 파서는 외부 입력 — 자릿수·범위·끝 문자를 직접 검사, 실패 시 출력 불변.
- 하드웨어는 TCAM(병렬 비교 + 길이순 배치) 또는 DIR-24-8, Linux 는 LC-trie(`fib_trie.c`, RCU 읽기), 경로 수십 개 MCU 는 선형 탐색으로 충분.

## 참고자료

- ISO/IEC 9899:2011 (C11) §6.5.7 Bitwise shift operators — 시프트 양 ≥ 폭이면 UB
- [RFC 4632 — Classless Inter-domain Routing (CIDR)](https://www.rfc-editor.org/rfc/rfc4632)
- [RFC 1812 — Requirements for IP Version 4 Routers](https://www.rfc-editor.org/rfc/rfc1812) (최장 일치 경로 선택, TTL 처리)
- Donald R. Morrison, "PATRICIA — Practical Algorithm To Retrieve Information Coded in Alphanumeric", JACM 15(4), 1968
- Stefan Nilsson & Gunnar Karlsson, "IP-Address Lookup Using LC-Tries", IEEE JSAC 17(6), 1999
- [Linux `net/ipv4/fib_trie.c`](https://github.com/torvalds/linux/blob/master/net/ipv4/fib_trie.c) — LC-trie FIB, `fib_valid_key_len()`
- Keith Sklower, "A Tree-Based Packet Routing Table for Berkeley Unix", USENIX Winter 1991 (BSD `net/radix.c`)
- P. Gupta, S. Lin, N. McKeown, "Routing Lookups in Hardware at Memory Access Speeds", IEEE INFOCOM 1998 (DIR-24-8)
- V. Srinivasan & G. Varghese, "Fast Address Lookups Using Controlled Prefix Expansion", ACM TOCS 17(1), 1999
- M. Waldvogel, G. Varghese, J. Turner, B. Plattner, "Scalable High Speed IP Routing Lookups", ACM SIGCOMM 1997
- M. Ruiz-Sánchez, E. Biersack, W. Dabbous, "Survey and Taxonomy of IP Address Lookup Algorithms", IEEE Network 15(2), 2001
- [Arm Architecture Reference Manual — LSLV (AArch64), LSL register (ARMv7-M)](https://developer.arm.com/documentation) — 레지스터 시프트 양 처리 차이
- [GCC Other Builtins — `__builtin_clz(0)` 은 정의되지 않음](https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html)
