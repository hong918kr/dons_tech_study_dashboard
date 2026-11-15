---
id: ARCH-M04
time: 80분
updated: 2026-11-11
---
# ARCH-M04 직접 사상 캐시 시뮬레이터

## 학습 목표

- 용량 C · 라인 B 를 받아 주소를 **tag / index / offset** 으로 나누고 (`offset = log2 B`, `index = log2(C/B)`, tag = 나머지), 나눗셈 없이 마스크·시프트로 계산할 수 있다.
- 직접 사상(direct-mapped) 캐시의 hit/miss 판정과 통계(접근·히트·미스·**compulsory**·eviction)를 구현하고, 다른 방식으로 짠 참조 모델과 결정적 랜덤 트레이스로 대조할 수 있다.
- 순차·stride·행렬 순회 트레이스를 만들어 히트율을 **손으로 먼저 예측**하고 시뮬레이터로 확인한다: 행 순회 87.5%, 열 순회 0%, 한 라인 패딩 후 87.5%.
- **conflict miss** 사례(열 순회, C 간격 두 배열, 2의 거듭제곱 stride)를 set 매핑으로 설명하고, "워킹셋이 라인 수 이하면 compulsory 가 아닌 미스는 전부 conflict" 라는 판정을 쓸 수 있다. 패딩 규칙 `set 수 = min(R, S / gcd(p, S))` 를 말할 수 있다.
- MCU 플래시 명령 캐시에서 메인 루프와 ISR 이 같은 set 을 쓰는지 검사하고, 충돌 없는 배치 주소를 찾아 fetch cycle 차이를 계산한다.

## 1. 문제

"주소 0x12345678 이 16 KB / 64 B 캐시의 몇 번 set 으로 가나?", "행렬을 열 방향으로 돌면 왜 느린가, 패딩 한 줄로 왜 고쳐지나?" — 캐시 면접의 1번 문제이고, 실제 칩에서도 MCU 플래시 가속기·작은 I-cache 는 단순한 구조라서 **배치(링커 주소) 하나로 ISR 지연이 흔들리는** 일이 생긴다. 직접 사상 캐시는 모든 캐시의 기본형이다: N-way 는 "칸이 N 개인 직접 사상" 이다 (ARCH-M05).

직접 사상 캐시는 라인이 S = C / B 개이고, 주소의 index 비트가 **들어갈 칸을 하나로 정해 버린다**. 칸에는 "지금 어느 라인이 들어 있나" 를 적은 tag 와 valid 비트가 있다. 조회 = 칸 하나를 읽고 tag 를 한 번 비교.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="m04-arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">주소 0x12345678 을 C 4 KB · B 32 B 직접 사상 캐시가 읽는 법</text>
  <rect x="40" y="34" width="400" height="32" class="f-acc" stroke="currentColor"/>
  <rect x="440" y="34" width="140" height="32" class="f-warn" stroke="currentColor"/>
  <rect x="580" y="34" width="100" height="32" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="170" y="55">tag [31:12] · 20 bit</text>
    <text x="452" y="55">index [11:5] · 7</text>
    <text x="590" y="55">offset [4:0] · 5</text>
    <text x="210" y="86" font-family="monospace">0x12345</text>
    <text x="466" y="86" font-family="monospace">0x33 (= 51)</text>
    <text x="596" y="86" font-family="monospace">0x18 (= 24)</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="98" y="114">V</text><text x="160" y="114">tag</text><text x="320" y="114">data (32 B)</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="90" y="122" width="30" height="26"/><rect x="120" y="122" width="130" height="26"/><rect x="250" y="122" width="220" height="26"/>
    <rect x="90" y="148" width="30" height="26"/><rect x="120" y="148" width="130" height="26"/><rect x="250" y="148" width="220" height="26"/>
    <rect x="90" y="226" width="30" height="26"/><rect x="120" y="226" width="130" height="26"/><rect x="250" y="226" width="220" height="26"/>
    <rect x="90" y="252" width="30" height="26"/><rect x="120" y="252" width="130" height="26"/><rect x="250" y="252" width="220" height="26"/>
  </g>
  <g class="f-acc" stroke="currentColor">
    <rect x="90" y="200" width="30" height="26"/><rect x="120" y="200" width="130" height="26"/><rect x="250" y="200" width="220" height="26"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="140">set 0</text><text x="101" y="140">0</text><text x="130" y="140">—</text>
    <text x="10" y="166">set 1</text><text x="101" y="166">1</text><text x="130" y="166" font-family="monospace">0x00A41</text>
    <text x="30" y="192">⋮</text><text x="300" y="192">⋮</text>
    <text x="10" y="218" font-weight="bold">set 51</text><text x="101" y="218">1</text><text x="130" y="218" font-family="monospace">0x12345</text>
    <text x="258" y="218">바이트 24 (0x18) 부터 읽는다</text>
    <text x="10" y="244">⋮</text>
    <text x="10" y="270">set 127</text><text x="101" y="270">1</text><text x="130" y="270" font-family="monospace">0x20000</text>
  </g>
  <rect x="490" y="168" width="240" height="30" rx="5" fill="none" stroke="currentColor"/>
  <rect x="490" y="206" width="240" height="30" rx="5" class="f-ok" stroke="currentColor"/>
  <rect x="490" y="244" width="240" height="30" rx="5" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="500" y="188">저장 tag == 주소 tag 0x12345 ?</text>
    <text x="500" y="226">같고 V = 1 → hit (비교기 1 개)</text>
    <text x="500" y="264">다르면 miss: 라인 채우고 tag 교체</text>
  </g>
  <path d="M470 213 L488 185" stroke="currentColor" fill="none" marker-end="url(#m04-arr)"/>
  <g fill="currentColor" font-size="12">
    <text x="10" y="302">offset = 라인 안 바이트 위치, index = 어느 칸(set), tag = 그 칸에 지금 어느 라인이 있나.</text>
    <text x="10" y="322">칸이 하나뿐이라 같은 index 의 다른 라인이 오면 무조건 쫓아낸다 → conflict miss.</text>
  </g>
</svg>
```

이미 아는 것(다시 가르치지 않는다): AMAT 와 로컬/전역 미스율(ARCH-B10), 128 B 일관성 단위(ARCH-M08), 버스 wait state 와 플래시 프리페치(ARCH-B12). 이 레슨은 **직접 사상 한 단계** 만 다룬다 — N-way·교체 정책·정확한 3C 분류는 ARCH-M05, 쓰기 정책과 DRAM 트래픽은 ARCH-M06, 실측 행렬 곱은 ARCH-M07 이 같은 공통 규칙(바이트 주소 `uint32_t`, 트레이스 `{addr, rw, size}`, S = C / (B·W)) 으로 이어 간다.

### Part A — 분해와 판정

- `dm_init(c, C, B)` — C·B 가 2의 거듭제곱, 4 ≤ B ≤ C, S ≤ 4096 인지 검사하고 모든 칸을 무효로. `off_bits`, `idx_bits` 계산.
- `dm_split(c, addr, &tag, &index, &offset)` — 마스크와 시프트만.
- `dm_access(c, addr, rw)` — 1 hit / 0 miss / −1 잘못된 rw. 미스면 **compulsory**(그 라인 주소를 처음 봄) 와 **eviction**(유효 라인을 밀어냄, set 별 카운트) 을 센다. `'W'` 도 `'R'` 과 같은 판정 (write-allocate 가정 — 트래픽은 ARCH-M06).
- 테스트: 경계 형상, 분해→재조립, 4 KB/8 KB 순차, 그리고 나눗셈·나머지로 따로 짠 참조 모델과 30 개 형상 × 4000 접근 퍼즈 대조.

### Part B — 트레이스와 conflict miss

- `dm_access_bytes(c, addr, size, rw)` — 라인 경계를 넘는 접근은 라인마다 조회 (0x1E 에서 4 B = 2 라인). 주소 공간 끝을 넘으면 거부.
- `gen_matrix(t, max, base, rows, cols, elem, pitch, by_col)` — 행/열 순회 트레이스. 원소 (i, j) = `base + i·pitch + j·elem`.
- `dm_sets_touched(c, t, n)` — 시뮬레이션 없이 주소만 보고 몇 개의 set 을 쓰는지.
- `dm_conflict_if_fits(c)` — 지금까지 본 라인 수 ≤ S 면 `misses − compulsory` (전부 conflict), 아니면 `UINT64_MAX` (여기서는 판정 불가 → ARCH-M05).
- 사례: 64×64 int 행 순회 vs 열 순회 vs 열 순회 + 32 B 패딩, 1 KB pitch 이미지의 16×16 타일, `a[i]·b[i]` 에서 b = a + C, stride 반복 루프.

### Part C — 플래시 명령 캐시: 메인 루프 vs ISR

- `ranges_shared_sets(c, a, la, b, lb)` — 두 코드 범위가 공유하는 set 수 (링커 맵에서 바로 계산할 수 있는 "CI 검사").
- `place_no_conflict(c, hot, hot_len, start, len, &out)` — hot 루프와 set 을 공유하지 않는, start 이상 첫 라인 정렬 주소.
- 1 KB / 16 B 명령 캐시, 루프 256 B 를 100 바퀴, 10 바퀴마다 128 B ISR: 충돌 배치 vs 고친 배치의 미스와 fetch cycle.

## 2. Problem (English)

> **Direct-mapped cache simulator.**
>
> Byte addresses are `uint32_t`. A cache has capacity `C` and line size `B` (powers of two, `4 <= B <= C`, `C/B <= 4096`), one line per set, so `S = C/B` sets; `offset = addr[log2 B - 1 : 0]`, `index = next log2 S bits`, `tag = the rest`.
>
> 1. Implement `dm_init`, `dm_split` (masks and shifts only), and `dm_access(addr, rw)` returning 1 on hit, 0 on miss (allocate on every miss, reads and writes alike), −1 for an invalid `rw`. Keep stats: accesses, hits, misses, compulsory misses (first touch of that line address), evictions (a valid line replaced), and per-set evictions. Cross-check against an independent reference model on deterministic random traces.
> 2. Split accesses that straddle line boundaries. Generate traces for row-major and column-major traversal of a matrix with a given row pitch. Count the distinct sets a trace maps to. If the number of distinct lines touched is ≤ S, report all non-compulsory misses as conflict misses. Show: 64×64 `int32` matrix on a 4 KB/32 B cache — row walk 87.5% hits, column walk 0%, column walk with 288 B pitch 87.5%; two arrays exactly `C` apart thrash, one line of padding fixes it.
> 3. For a 1 KB/16 B instruction cache, compute how many sets a hot loop and an ISR share, find the lowest non-conflicting line-aligned ISR address at or above a given start, and compare misses and fetch cycles (hit = 1 cycle, miss = +5) for both placements.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
cd lessons/ARCH/ARCH-M04
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
mkdir -p /tmp/esn_ARCH-M04
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/esn_ARCH-M04/st && /tmp/esn_ARCH-M04/st; echo "exit $?"
```

실제 출력 (앞부분과 끝):

```text
-- Part A: geometry, split, hit/miss --
FAIL  dm_init(&g_c, 4096, 32) == 0 && g_c.sets == 128 && g_c.off_bits == 5 && g_c.idx_bits == 7  (line 209)
FAIL  dm_init(&g_c, 32, 32) == 0 && g_c.sets == 1 && g_c.idx_bits == 0  (line 210)
PASS  dm_init(&g_c, 4096, 24) == -1
...
FAIL  dm_conflict_if_fits(&g_c) == 144  (line 373)
FAIL  g_c.st.misses == 24 && dm_conflict_if_fits(&g_c) == 0  (line 377)
FAIL  fetch_cycles(&bad, 5) == 7560 && fetch_cycles(&g_c.st, 5) == 6840  (line 378)
14/60 passed
exit 1
```

TODO 는 9 개 (`dm_init`, `dm_split`, `dm_access`, `dm_access_bytes`, `gen_matrix`, `dm_sets_touched`, `dm_conflict_if_fits`, `ranges_shared_sets`, `place_no_conflict`). 스텁 `dm_init` 이 −1 을 돌려주므로 "잘못된 형상 거부" 테스트 몇 개는 처음부터 PASS 로 찍힌다 — 그건 우연이다. 본 라인 집합(`lineset_t`), stride/펌웨어 fetch 트레이스 생성기, `fetch_cycles` 는 주어진 도구다.

## 4. 힌트

<details>
<summary>힌트 1 — 비트 수는 log2 두 번</summary>

`off_bits = log2 B`, `idx_bits = log2(C/B)`. `offset = addr & (B-1)`, `index = (addr >> off_bits) & (S-1)`, `tag = addr >> (off_bits + idx_bits)`. C ≤ 2^31 이라 시프트 양은 최대 31 (32 시프트 UB 없음). S = 1 이면 `idx_bits = 0`, `index` 는 항상 0.

</details>

<details>
<summary>힌트 2 — compulsory 는 미스일 때만 확인해도 된다</summary>

처음 보는 라인은 캐시에 있을 수가 없으므로 반드시 미스다. 그래서 미스 경로에서만 `ls_insert(&c->seen, addr >> off_bits) == 1` 이면 compulsory++. eviction 은 "채우기 전에 그 칸이 valid 였나".

</details>

<details>
<summary>힌트 3 — 라인 걸침은 라인 번호로 돈다</summary>

첫 라인 = `addr >> off_bits`, 마지막 라인 = `(addr + size − 1) >> off_bits`. 끝 주소는 `uint64_t` 로 계산해 4 GB 를 넘으면 거부. 라인 번호 l 에 대해 `dm_access(c, l << off_bits, rw)`.

</details>

<details>
<summary>힌트 4 — 열 순회가 쓰는 set 수</summary>

pitch 가 p 라인(p = pitch / B, 정수일 때)이면 i 번째 줄의 set 은 `(i·p + 상수) mod S`. 서로 다른 값의 개수는 `S / gcd(p, S)` 개에서 반복된다. S 가 2의 거듭제곱이면 **p 가 홀수일 때** 주기가 S 로 가장 길다.

</details>

<details>
<summary>힌트 5 — 범위가 덮는 set 은 최대 S 개</summary>

`ranges_shared_sets`: a 의 라인 번호 `a>>ob .. (a+la−1)>>ob` 를 돌며 `l & (S−1)` 칸을 표시하되, S 개를 넘게 볼 필요는 없다 (그 뒤는 반복). b 도 같은 식으로 돌며 표시된 칸을 센다 — 한 번 센 칸은 지워서 중복을 막는다. `place_no_conflict` 는 후보를 라인 단위로 S 개만 보면 된다 (C 만큼 옮기면 매핑이 똑같다).

</details>

## 5. 풀이

### Part A — 분해, 판정, 그리고 "정답 확인"

**분해 표.** 32 비트 주소, C = 4 KB, B = 32 B → S = 128:

| 필드 | 비트 | 폭 | 0x12345678 | 0xFFFFFFFF |
|---|---|---:|---|---|
| offset | [4:0] | 5 | 0x18 | 31 |
| index | [11:5] | 7 | 0x33 (51) | 127 |
| tag | [31:12] | 20 | 0x12345 | 0xFFFFF |

직접 사상에서는 `offset + index = log2 C` 라서 **tag = 주소 / C**, **index = (주소 mod C) / B** 로 외워도 된다. 그래서 "C 의 배수만큼 떨어진 두 주소는 반드시 같은 set" — Part B 의 모든 conflict 사례가 이 한 줄에서 나온다.

**판정.**

```text
dm_access(addr):
  (tag, idx) = split(addr)
  if valid[idx] && tagv[idx] == tag:  hit
  else:                               miss
       if 처음 본 라인:   compulsory++
       if valid[idx]:     evictions++, set_evictions[idx]++
       valid[idx] = 1; tagv[idx] = tag          ← 무조건 교체 (선택지가 없다 = 교체 정책 불필요)
```

**순차 트레이스로 손 예측 → 확인.** 4 B 씩 4 KB 를 읽으면 라인(32 B)마다 첫 접근만 미스 → 1024 접근 중 128 미스, 히트율 7/8 = 87.5%, 전부 compulsory. 4 KB 는 캐시에 딱 들어가므로 두 번째 바퀴는 전부 히트. 8 KB 를 두 바퀴 돌면 앞 4 KB 와 뒤 4 KB 가 같은 set 들을 나눠 쓰므로 두 번째 바퀴도 라인마다 미스 → 미스 512, compulsory 256, eviction 384 (512 − 처음 빈 칸 128). 이 경우는 워킹셋(256 라인) > S(128) 라서 **완전 연관 캐시라도** 미스한다 → capacity 성격, `dm_conflict_if_fits` 는 `UINT64_MAX` (판정 불가).

**참조 모델 대조.** 시프트/마스크 구현이 맞는지는 같은 방식으로 다시 짜서는 확인이 안 된다. 테스트의 `ref_access` 는 `line = addr / B`, `set = line % S` 로 **나눗셈** 을 쓰고 set 마다 "라인 번호" 하나를 기억한다. 30 개 형상(B 4–256, C = B·2^0..12) × 4000 접근(순차 5/8, 근처 2/8, 아무 데나 1/8)에서 접근마다 hit/miss 가 같아야 PASS. 실리콘 검증의 "스코어보드(scoreboard) 대 참조 모델" 과 같은 구조다.

**하드웨어에서는 배선.** 고정 형상의 분해는 명령 하나다 (`demo_split_asm.c`):

```bash
cc -O2 -S -o - demo_split_asm.c                                                   # arm64
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_split_asm.c -o -   # Cortex-M4
```

```text
_dm_offset:                             ; @dm_offset
	and	w0, w0, #0x1f
	ret
_dm_index:                              ; @dm_index
	ubfx	w0, w0, #5, #7
	ret
_dm_tag:                                ; @dm_tag
	lsr	w0, w0, #12
	ret
```

```text
dm_offset:                              @ @dm_offset
	and	r0, r0, #31
	bx	lr
dm_index:                               @ @dm_index
	ubfx	r0, r0, #5, #7
	bx	lr
dm_tag:                                 @ @dm_tag
	lsrs	r0, r0, #12
	bx	lr
```

(위 arm64, 아래 Cortex-M4. 지시문·주석 줄만 뺐다.) `ubfx` = unsigned bit-field extract: 비트 5 부터 7 개. 실제 캐시 하드웨어에서는 이것조차 없다 — 주소선 [11:5] 가 tag/data SRAM 의 주소 입력에 그대로 꽂히고, [31:12] 가 비교기로 간다.

### Part B — conflict miss 는 set 매핑으로 설명한다

**라인 걸침.** 4 B 를 0x1E 에서 읽으면 0x1E..0x21 → 라인 0 과 1 을 둘 다 조회한다 (조회 2, 미스 2). 정렬된 접근만 쓰는 이유 중 하나: 미정렬 접근은 캐시 조회를 두 번 하고 (Cortex-M 은 미정렬 지원이 명령/영역에 따라 다르다), 페이지 경계라면 TLB 조회도 두 번.

**64×64 int 행렬 (16 KB) 을 4 KB / 32 B 캐시로.**

| 순회 | pitch | 미스 / 4096 | 히트율 | 왜 |
|---|---:|---:|---:|---|
| 행 우선 | 256 | 512 | 87.5% | 라인당 원소 8 개, 첫 원소만 미스 (공간 지역성) |
| 열 우선 | 256 | 4096 | 0% | 한 열 64 줄 → set 16 개에 4 줄씩 → 다음 열 갈 때 이미 쫓겨남 |
| 열 우선 | 288 | 512 | 87.5% | pitch = 9 라인 → 64 줄이 64 개 서로 다른 set |

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">64×64 int 행렬의 열 하나(64 줄)가 쓰는 set — C 4 KB, B 32 B, 128 set</text>
  <text x="10" y="52" fill="currentColor">pitch 256 B = 8 라인: set = 8·i mod 128 → set 16 개에 4 줄씩</text>
  <rect x="40" y="60" width="640" height="22" class="f-mut" stroke="currentColor" opacity=".5"/>
  <g stroke="currentColor" stroke-width=".4">
    <rect x="40" y="60" width="5" height="22" class="f-warn"/><rect x="80" y="60" width="5" height="22" class="f-warn"/><rect x="120" y="60" width="5" height="22" class="f-warn"/><rect x="160" y="60" width="5" height="22" class="f-warn"/><rect x="200" y="60" width="5" height="22" class="f-warn"/><rect x="240" y="60" width="5" height="22" class="f-warn"/><rect x="280" y="60" width="5" height="22" class="f-warn"/><rect x="320" y="60" width="5" height="22" class="f-warn"/>
    <rect x="360" y="60" width="5" height="22" class="f-warn"/><rect x="400" y="60" width="5" height="22" class="f-warn"/><rect x="440" y="60" width="5" height="22" class="f-warn"/><rect x="480" y="60" width="5" height="22" class="f-warn"/><rect x="520" y="60" width="5" height="22" class="f-warn"/><rect x="560" y="60" width="5" height="22" class="f-warn"/><rect x="600" y="60" width="5" height="22" class="f-warn"/><rect x="640" y="60" width="5" height="22" class="f-warn"/>
  </g>
  <text x="10" y="122" fill="currentColor">pitch 288 B = 9 라인 (+1 라인): set = 9·i mod 128 → 64 개에 1 줄씩</text>
  <rect x="40" y="130" width="640" height="22" class="f-mut" stroke="currentColor" opacity=".5"/>
  <g stroke="currentColor" stroke-width=".4">
    <rect x="40" y="130" width="5" height="22" class="f-ok"/><rect x="45" y="130" width="5" height="22" class="f-ok"/><rect x="55" y="130" width="5" height="22" class="f-ok"/><rect x="65" y="130" width="5" height="22" class="f-ok"/><rect x="75" y="130" width="5" height="22" class="f-ok"/><rect x="85" y="130" width="5" height="22" class="f-ok"/><rect x="90" y="130" width="5" height="22" class="f-ok"/><rect x="100" y="130" width="5" height="22" class="f-ok"/>
    <rect x="110" y="130" width="5" height="22" class="f-ok"/><rect x="120" y="130" width="5" height="22" class="f-ok"/><rect x="130" y="130" width="5" height="22" class="f-ok"/><rect x="135" y="130" width="5" height="22" class="f-ok"/><rect x="145" y="130" width="5" height="22" class="f-ok"/><rect x="155" y="130" width="5" height="22" class="f-ok"/><rect x="165" y="130" width="5" height="22" class="f-ok"/><rect x="175" y="130" width="5" height="22" class="f-ok"/>
    <rect x="180" y="130" width="5" height="22" class="f-ok"/><rect x="190" y="130" width="5" height="22" class="f-ok"/><rect x="200" y="130" width="5" height="22" class="f-ok"/><rect x="210" y="130" width="5" height="22" class="f-ok"/><rect x="220" y="130" width="5" height="22" class="f-ok"/><rect x="225" y="130" width="5" height="22" class="f-ok"/><rect x="235" y="130" width="5" height="22" class="f-ok"/><rect x="245" y="130" width="5" height="22" class="f-ok"/>
    <rect x="255" y="130" width="5" height="22" class="f-ok"/><rect x="265" y="130" width="5" height="22" class="f-ok"/><rect x="270" y="130" width="5" height="22" class="f-ok"/><rect x="280" y="130" width="5" height="22" class="f-ok"/><rect x="290" y="130" width="5" height="22" class="f-ok"/><rect x="300" y="130" width="5" height="22" class="f-ok"/><rect x="310" y="130" width="5" height="22" class="f-ok"/><rect x="315" y="130" width="5" height="22" class="f-ok"/>
    <rect x="325" y="130" width="5" height="22" class="f-ok"/><rect x="335" y="130" width="5" height="22" class="f-ok"/><rect x="345" y="130" width="5" height="22" class="f-ok"/><rect x="355" y="130" width="5" height="22" class="f-ok"/><rect x="370" y="130" width="5" height="22" class="f-ok"/><rect x="380" y="130" width="5" height="22" class="f-ok"/><rect x="390" y="130" width="5" height="22" class="f-ok"/><rect x="400" y="130" width="5" height="22" class="f-ok"/>
    <rect x="415" y="130" width="5" height="22" class="f-ok"/><rect x="425" y="130" width="5" height="22" class="f-ok"/><rect x="435" y="130" width="5" height="22" class="f-ok"/><rect x="445" y="130" width="5" height="22" class="f-ok"/><rect x="460" y="130" width="5" height="22" class="f-ok"/><rect x="470" y="130" width="5" height="22" class="f-ok"/><rect x="480" y="130" width="5" height="22" class="f-ok"/><rect x="490" y="130" width="5" height="22" class="f-ok"/>
    <rect x="505" y="130" width="5" height="22" class="f-ok"/><rect x="515" y="130" width="5" height="22" class="f-ok"/><rect x="525" y="130" width="5" height="22" class="f-ok"/><rect x="535" y="130" width="5" height="22" class="f-ok"/><rect x="550" y="130" width="5" height="22" class="f-ok"/><rect x="560" y="130" width="5" height="22" class="f-ok"/><rect x="570" y="130" width="5" height="22" class="f-ok"/><rect x="580" y="130" width="5" height="22" class="f-ok"/>
    <rect x="595" y="130" width="5" height="22" class="f-ok"/><rect x="605" y="130" width="5" height="22" class="f-ok"/><rect x="615" y="130" width="5" height="22" class="f-ok"/><rect x="625" y="130" width="5" height="22" class="f-ok"/><rect x="640" y="130" width="5" height="22" class="f-ok"/><rect x="650" y="130" width="5" height="22" class="f-ok"/><rect x="660" y="130" width="5" height="22" class="f-ok"/><rect x="670" y="130" width="5" height="22" class="f-ok"/>
  </g>
  <text x="10" y="192" fill="currentColor">pitch 384 B = 12 라인 (+4 라인): set = 12·i mod 128 → 32 개에 2 줄씩</text>
  <rect x="40" y="200" width="640" height="22" class="f-mut" stroke="currentColor" opacity=".5"/>
  <g stroke="currentColor" stroke-width=".4">
    <rect x="40" y="200" width="5" height="22" class="f-warn"/><rect x="60" y="200" width="5" height="22" class="f-warn"/><rect x="80" y="200" width="5" height="22" class="f-warn"/><rect x="100" y="200" width="5" height="22" class="f-warn"/><rect x="120" y="200" width="5" height="22" class="f-warn"/><rect x="140" y="200" width="5" height="22" class="f-warn"/><rect x="160" y="200" width="5" height="22" class="f-warn"/><rect x="180" y="200" width="5" height="22" class="f-warn"/>
    <rect x="200" y="200" width="5" height="22" class="f-warn"/><rect x="220" y="200" width="5" height="22" class="f-warn"/><rect x="240" y="200" width="5" height="22" class="f-warn"/><rect x="260" y="200" width="5" height="22" class="f-warn"/><rect x="280" y="200" width="5" height="22" class="f-warn"/><rect x="300" y="200" width="5" height="22" class="f-warn"/><rect x="320" y="200" width="5" height="22" class="f-warn"/><rect x="340" y="200" width="5" height="22" class="f-warn"/>
    <rect x="360" y="200" width="5" height="22" class="f-warn"/><rect x="380" y="200" width="5" height="22" class="f-warn"/><rect x="400" y="200" width="5" height="22" class="f-warn"/><rect x="420" y="200" width="5" height="22" class="f-warn"/><rect x="440" y="200" width="5" height="22" class="f-warn"/><rect x="460" y="200" width="5" height="22" class="f-warn"/><rect x="480" y="200" width="5" height="22" class="f-warn"/><rect x="500" y="200" width="5" height="22" class="f-warn"/>
    <rect x="520" y="200" width="5" height="22" class="f-warn"/><rect x="540" y="200" width="5" height="22" class="f-warn"/><rect x="560" y="200" width="5" height="22" class="f-warn"/><rect x="580" y="200" width="5" height="22" class="f-warn"/><rect x="600" y="200" width="5" height="22" class="f-warn"/><rect x="620" y="200" width="5" height="22" class="f-warn"/><rect x="640" y="200" width="5" height="22" class="f-warn"/><rect x="660" y="200" width="5" height="22" class="f-warn"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="40" y="238">set 0</text><text x="358" y="238">64</text><text x="660" y="238">127</text>
    <text x="10" y="264">열 j 다음 열 j+1 은 같은 64 라인을 다시 쓴다 (j mod 8 ≠ 7).</text>
    <text x="10" y="284">64 라인이 서로 다른 set 에 있어야 살아남는다: set 수 = min(64, 128 / gcd(p, 128)).</text>
    <text x="10" y="304">p (pitch 의 라인 수) 가 홀수면 64. 히트율 (demo_sweep.c): 256 → 0%, 288 → 87.5%, 384 → 0%.</text>
  </g>
</svg>
```

열 순회는 열 j 에서 64 개 라인을 가져오고, 열 j+1 (같은 라인 안의 다음 4 B) 에서 **같은 64 라인을 다시** 쓴다. 그때까지 살아 있으려면 64 라인이 서로 다른 set 에 있어야 한다. pitch 256 B = 8 라인이면 `8·i mod 128` 은 16 개 값뿐 — 행렬 전체(16 KB)는 캐시보다 크지만, 한 열에 필요한 것은 64 라인 = 2 KB 로 캐시의 절반이다. **용량은 충분한데 배치가 나빠서** 미스한다. 일반식 (추가 연습 4 에서 시뮬레이터와 대조):

```text
열 하나가 쓰는 set 수 = min(R, S / gcd(p, S))      p = pitch / B (정수 라인), R = 줄 수
S = 128: p = 8 → 16, p = 9 → 64, p = 10 → 64, p = 12 → 32
```

그래서 "패딩은 **홀수 라인**" 이 안전한 규칙이다. 4 라인(128 B) 패딩은 오히려 나쁘다. `demo_sweep.c` 의 실제 출력:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_sweep.c -o /tmp/esn_ARCH-M04/demo && /tmp/esn_ARCH-M04/demo
```

```text
== 2. 64x64 int32 matrix (16 KB), C = 4 KB, B = 32 B: hit% by traversal and row pitch ==
pitch(B)  pad(B)   row-major  col-major
     256       0       87.5        0.0
     260       4       87.3       83.8
     272      16       86.7       29.3
     288      32       87.5       87.5
     320      64       87.5       87.5
     384     128       87.5        0.0
     512     256       87.5        0.0

== 3. fixing the column walk (B = 32): grow the cache vs pad one line ==
C(KB)  col pitch 256  col pitch 288
    4            0.0           87.5
    8            0.0           87.5
   16           87.5           87.5
   32           87.5           87.5
```

읽는 법: 4 B 패딩(260)도 83.8% 까지 회복한다 — 행 시작이 라인 경계에서 어긋나며 set 이 퍼진다 (대신 행 순회가 87.3% 로 조금 손해: 일부 줄이 라인 하나를 더 걸친다). 표 3: 패딩 없이 고치려면 캐시를 **4 배(16 KB)** 로 키워야 한다 (S = 512 → `8·i mod 512` 가 64 개 값). 32 B 패딩은 공짜다.

**순수 conflict 판정.** 위 64×64 열 순회는 워킹셋(512 라인) > S 라 capacity 와 conflict 가 섞여 있다. 정확한 분리는 Hill 의 3C 정의 — "같은 용량의 완전 연관 LRU 도 미스하면 capacity, 아니면 conflict" — 가 필요하고 그건 ARCH-M05 의 몫이다. 하지만 **워킹셋 ≤ S** 인 경우엔 완전 연관 캐시가 아무것도 내보낼 일이 없으므로 compulsory 가 아닌 미스는 **전부** conflict 다. 테스트의 두 사례:

| 사례 | 접근 | 미스 | compulsory | conflict | 고친 뒤 미스 |
|---|---:|---:|---:|---:|---:|
| 1 KB pitch 이미지의 16×16 int 타일, 열 순회 (워킹셋 32 라인) | 256 | 256 | 32 | 224 | 32 (pitch 1056) |
| `a[i]·b[i]`, 256 원소, b = a + 4 KB (워킹셋 64 라인) | 512 | 512 | 64 | 448 | 64 (b = a + 4 KB + 32) |

내적 사례가 면접 단골 "두 배열이 C 배수만큼 떨어져 있다" 이다. `a[i]` 와 `b[i]` 가 늘 같은 set → 서로를 번갈아 쫓아내 **히트 0**. 메모리 할당자가 큰 배열을 페이지(4 KB/16 KB) 정렬로 주면 저절로 생긴다. 패딩 한 라인으로 b 의 라인 k 가 set k+1 로 밀리고, a 가 set k+1 에 올 때는 b 의 라인 k 를 이미 다 쓴 뒤라 미스가 compulsory 64 개로 줄어든다.

**stride 반복 루프.** 원소 64 개를 stride s 로 4 바퀴 돈다 (`demo_sweep.c` 표 1):

```text
== 1. stride sweep (C = 4096 B, B = 32 B, direct-mapped, 128 lines) ==
stride(B)  stream 64KB hit%  | loop 64 elems x4: lines  sets  misses  hit%
        4              87.5  |                        8     8       8   96.9
        8              75.0  |                       16    16      16   93.8
       16              50.0  |                       32    32      32   87.5
       32               0.0  |                       64    64      64   75.0
       64               0.0  |                       64    64      64   75.0
      128               0.0  |                       64    32     256    0.0
      256               0.0  |                       64    16     256    0.0
      512               0.0  |                       64     8     256    0.0
     1024               0.0  |                       64     4     256    0.0
     2048               0.0  |                       64     2     256    0.0
     4096               0.0  |                       64     1     256    0.0
     8192               0.0  |                       64     1     256    0.0
```

- 왼쪽(64 KB 를 한 번 훑기): 히트율 = 1 − s/B, s ≥ B 면 0. 이건 공간 지역성만의 이야기다 — 어떤 캐시든 한 번 지나가는 데이터는 재사용이 없다.
- 오른쪽(64 원소 반복): s = 64 까지는 64 라인이 64 개 set 에 퍼져 첫 바퀴만 미스. **s = 128 = C/32** 부터 set 이 32 → 16 → … → 1 개로 줄어 64 라인이 들어갈 자리가 없다 → 전부 미스. 워킹셋은 고작 64 라인(캐시의 절반)인데. 일반적으로 stride s (2의 거듭제곱, s ≥ B) 루프가 쓸 수 있는 set 은 최대 `max(1, C / s)` 개 — 원소 수가 그보다 많으면 thrash.

### Part C — 플래시 명령 캐시와 ISR 배치

MCU 는 플래시 wait state(ARCH-B12) 를 숨기려고 작은 명령 캐시/프리페치 버퍼를 둔다. 예: STM32F4 의 ART 가속기는 RM0090 에서 명령 캐시를 **128 bit 라인 64 개** (= 1 KB) 로 설명한다. 이 레슨은 그 크기를 빌려 **직접 사상으로 단순화** 했다 — 실제 ART 의 연관도·교체 방식은 여기서 모델링하지 않는다.

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">1 KB 플래시 명령 캐시 (16 B 라인 × 64 set): 루프와 ISR 이 쓰는 set</text>
  <text x="10" y="60" fill="currentColor" font-size="12">루프 0x0800_0400, 256 B</text>
  <rect x="200" y="44" width="512" height="22" class="f-mut" stroke="currentColor" opacity=".5"/>
  <rect x="200" y="44" width="128" height="22" class="f-acc" stroke="currentColor"/>
  <text x="10" y="100" fill="currentColor" font-size="12">ISR 0x0800_1400, 128 B</text>
  <rect x="200" y="84" width="512" height="22" class="f-mut" stroke="currentColor" opacity=".5"/>
  <rect x="200" y="84" width="64" height="22" class="f-warn" stroke="currentColor"/>
  <text x="10" y="140" fill="currentColor" font-size="12">ISR 0x0800_1500, 128 B</text>
  <rect x="200" y="124" width="512" height="22" class="f-mut" stroke="currentColor" opacity=".5"/>
  <rect x="328" y="124" width="64" height="22" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-size="12">
    <text x="200" y="164">set 0</text><text x="324" y="164">16</text><text x="388" y="164">24</text><text x="694" y="164">63</text>
    <text x="10" y="194">0x1400 − 0x0400 = 4 KB = C 의 배수 → ISR 이 루프의 set 0–7 을 덮는다.</text>
    <text x="10" y="214">인터럽트 1 번 = ISR 8 미스 + 루프 재적재 8 미스.</text>
    <text x="10" y="234">ISR 을 0x0800_1500 으로 옮기면 공유 set 0 → 첫 실행 뒤 미스 0.</text>
    <text x="10" y="254">100 바퀴, ISR 10 번: 미스 168 vs 24 → 미스당 +5 cycle 이면 7560 vs 6840.</text>
  </g>
</svg>
```

시나리오: 메인 루프 64 명령(4 B, 256 B = 16 라인) 을 100 바퀴, 10 바퀴마다 ISR 32 명령(128 B = 8 라인). 루프는 0x0800_0400, ISR 은 0x0800_1400 — 링커가 그냥 순서대로 놓은 결과다. 차이 0x1000 = 4 KB 는 C(1 KB) 의 배수 → ISR 의 8 라인이 루프의 set 0–7 과 같다 (`ranges_shared_sets` = 8).

| 배치 | 미스 | compulsory | conflict | fetch cycle (히트 1, 미스 +5) |
|---|---:|---:|---:|---:|
| ISR 0x0800_1400 (충돌) | 168 = 16 + 10·8 + 9·8 | 24 | 144 | 7560 |
| ISR 0x0800_1500 (`place_no_conflict`) | 24 | 24 | 0 | 6840 |

미스 168 의 내역: 루프 첫 바퀴 16, ISR 은 **매번** 8 (직전 루프가 쫓아냈으니까), ISR 뒤 루프 바퀴가 8 라인 재적재 (마지막 ISR 뒤에는 바퀴가 없어 9 번). 전체로는 10.5% 느려질 뿐이지만, 펌웨어에서 중요한 건 **ISR 1 회의 지연**이다: 충돌 배치에서는 ISR 진입 후 8 라인 × 5 = **+40 cycle** 이 매번 붙고 (첫 명령 fetch 부터 미스), 메인 루프도 +40 cycle 을 뒤에 낸다. 고친 배치에서는 첫 실행 뒤로 0. "가끔 ISR 이 느리다", "코드 한 줄 추가했더니 다른 루프가 느려졌다" 의 흔한 원인이 이 aliasing 이다 — 코드를 추가하면 뒤의 함수 주소가 다 밀리기 때문.

해결책 (현실 순서):
1. **배치 검사를 빌드에 넣는다** — 링커 맵(`.map`) 에서 hot 함수 범위를 뽑아 `ranges_shared_sets` 같은 검사. `place_no_conflict` 가 준 주소는 링커 스크립트에서 섹션 정렬/순서로 맞춘다.
2. **결정적 메모리로 옮긴다** — 실시간 ISR 은 ITCM/RAM 실행(`.ramfunc` 류 섹션) 으로 캐시를 아예 안 거치게 (ARCH-B10 Q6: 평균이 아니라 최악).
3. **연관도 있는 캐시** — Cortex-M7 L1 D-cache 는 4-way, 32 B 라인 (I-cache 는 2-way 로 알려져 있다). 2 way 만 돼도 루프 하나 + ISR 하나의 충돌은 사라진다 (ARCH-M05 에서 확인).

**이 맥과 비교.** Apple M6 의 L1 은 직접 사상이 아니고 훨씬 크다:

```bash
$ sysctl hw.cachelinesize hw.perflevel0.l1dcachesize hw.perflevel0.l1icachesize hw.pagesize
hw.cachelinesize: 128
hw.perflevel0.l1dcachesize: 131072
hw.perflevel0.l1icachesize: 196608
hw.pagesize: 16384
```

연관도는 sysctl 에 나오지 않는다. 그래도 "2의 거듭제곱 stride 가 같은 set 으로 몰린다" 는 원리는 set-associative 에서도 그대로라, way 수보다 많은 라인이 한 set 에 몰리면 같은 현상이 난다 (실측은 ARCH-M07).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/esn_ARCH-M04/so && /tmp/esn_ARCH-M04/so; echo "exit $?"
# -- Part A: geometry, split, hit/miss --
# PASS  dm_init(&g_c, 4096, 32) == 0 && g_c.sets == 128 && g_c.off_bits == 5 && g_c.idx_bits == 7
# ...
# 60/60 passed
# exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/esn_ARCH-M04/san && /tmp/esn_ARCH-M04/san
# 60/60 passed   (ASan/UBSan 경고 없음)
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드/생각 |
|---|---|---|
| `index = addr % S` | 라인 번호가 아니라 **바이트 주소** 의 나머지 → 같은 라인의 바이트들이 다른 set 으로 흩어진다 | `index = (addr >> off_bits) & (S − 1)` |
| `tag = addr >> idx_bits` | offset 비트를 빼먹음. 같은 라인 안 두 주소의 tag 가 달라져 히트가 미스로 | `tag = addr >> (off_bits + idx_bits)` |
| valid 비트 없이 tag 만 비교 | 리셋 직후 tag 배열이 0 → 주소 0x0000_0xxx 가 "히트" | `valid[idx] && tag[idx] == tag` |
| 미스에서 compulsory 를 "그 set 이 비어 있었나" 로 판단 | 빈 칸 채우기 ≠ 첫 접근. 쫓겨났다 돌아온 라인이 빈 set 에 들어갈 수도, 처음 본 라인이 남의 라인을 쫓아낼 수도 있다 | 라인 주소 집합으로 "처음 봤나" (`ls_insert`) |
| 라인 걸치는 접근을 한 번만 조회 | 0x1E 의 4 B 는 라인 두 개. 미스가 과소 계산 | `dm_access_bytes` 로 라인마다 |
| 패딩을 "아무 크기나" — 128 B 패딩 | p = 12 라인 → gcd(12,128) = 4 → set 32 개, 여전히 0% | 홀수 라인 (+32 B) 이 안전 |
| 열 순회가 느린 걸 "캐시가 작아서" 로 결론 | 한 열에 필요한 건 2 KB, 캐시 4 KB. 문제는 배치(conflict) | `dm_sets_touched`, `dm_conflict_if_fits` 로 확인 |
| `end = addr + size − 1` 을 32 비트로 | 0xFFFFFFFE + 4 가 wrap → 라인 0 을 조회하는 엉뚱한 결과 | `uint64_t` 로 계산 후 거부 |

## 8. 면접 Q&A

<details>
<summary>Q1. 32 비트 주소, 16 KB 직접 사상, 64 B 라인 — tag/index/offset 은 몇 비트? 4-way 로 바꾸면?</summary>

offset 6, S = 16384/64 = 256 → index 8, tag 32 − 14 = 18. 4-way 면 S = 64 → index 6, tag 20. **연관도를 올리면 index 가 줄고 tag 가 늘어난다** (C 고정). 완전 연관이면 index 0, tag 26.
꼬리질문: "tag RAM 은 얼마나 필요?" → 라인당 tag 18 + valid 1 (+ write-back 이면 dirty 1) = 20 bit × 256 = 5120 bit = 640 B, 데이터 16 KB 의 약 3.9% (추가 연습 3).

</details>

<details>
<summary>Q2. 직접 사상 캐시의 장점은? 왜 아직도 쓰나요?</summary>

조회가 칸 하나 + 비교기 하나 → **히트 시간이 가장 짧고** 면적·전력이 작다. 게다가 데이터를 tag 비교와 **동시에** 읽어 바로 넘길 수 있다 (틀리면 버린다) — N-way 는 어느 way 의 데이터인지 고르는 mux 가 비교 뒤에 붙는다. 교체 정책도 필요 없다. 그래서 MCU 플래시 가속기, 작은 I-cache, 일부 큰 DRAM 캐시처럼 "단순·빠름" 이 중요한 곳에 쓰인다.
꼬리질문: "conflict miss 는 어떻게 줄이나?" → 연관도(ARCH-M05), 작은 완전 연관 **victim cache** (Jouppi 1990 — 쫓겨난 라인 몇 개를 보관), 소프트웨어 패딩/배치, index 해싱 (상위 비트를 XOR — 일부 LLC 가 쓴다고 알려져 있다).

</details>

<details>
<summary>Q3. 행렬을 열 방향으로 돌면 왜 느리고, 패딩 한 줄은 왜 고치나요?</summary>

pitch 가 2의 거듭제곱 × 라인이면 한 열의 원소들이 소수의 set 에 몰린다 (64×64 int, 4 KB/32 B: 64 줄 → 16 set). 다음 열은 같은 라인들을 다시 쓰는데 이미 쫓겨난 뒤 → 0%. pitch 를 한 라인 늘리면 p = 9 (홀수) 라 `9·i mod 128` 이 64 개 서로 다른 set → 87.5%. 일반식 `min(R, S / gcd(p, S))`.
꼬리질문: "그럼 4 라인 패딩은?" → gcd(12, 128) = 4 → 32 set, 여전히 0%. BLAS 의 leading dimension, FFT 버퍼에 "2의 거듭제곱 크기를 피하라" 는 조언이 같은 이유다.

</details>

<details>
<summary>Q4. conflict miss 와 capacity miss 를 어떻게 구분하나요?</summary>

Hill 의 3C: 처음 접근 = compulsory, **같은 용량의 완전 연관 LRU 캐시도 미스** = capacity, 나머지 = conflict. 그래서 정확한 분류엔 FA-LRU 시뮬레이션이 하나 더 필요하다 (ARCH-M05). 지름길: 서로 다른 라인 수(워킹셋) ≤ S 면 FA 캐시는 내보낼 일이 없으므로 compulsory 가 아닌 미스는 전부 conflict (`dm_conflict_if_fits`).
꼬리질문: "8 KB 를 4 KB 캐시로 두 번 순차로 돌면?" → 둘째 바퀴 미스는 capacity — FA-LRU 도 순차 스캔에서는 전부 미스한다 (LRU 가 순환 스캔에 최악). 연관도를 올려도 안 줄어든다.

</details>

<details>
<summary>Q5. 라인 크기를 키우면 미스율이 항상 줄어드나요?</summary>

아니다. 공간 지역성이 좋은 접근(행 순회)은 줄지만, C 고정이면 라인 수가 줄어 conflict/capacity 가 늘고, 미스 페널티(라인 채우는 시간)도 커진다. `demo_sweep.c` 표 4 (C = 4 KB, pitch = 256 + B):

```text
B(B)  lines  row-major  col-major
   8    512       50.0       50.0
  16    256       75.0       75.0
  32    128       87.5       87.5
  64     64       93.8       93.8
 128     32       96.9        0.0
 256     16       98.4        0.0
```

B = 128 부터 라인이 32 개 — 한 열이 필요로 하는 64 라인이 **어떤 배치로도** 안 들어간다 (이건 capacity). 
꼬리질문: "미스 페널티를 줄이는 하드웨어 기법?" → critical-word-first / early restart (필요한 워드부터 먼저 넘김), 섹터 캐시 (라인을 sub-block 으로 나눠 valid 를 따로).

</details>

<details>
<summary>Q6. (펌웨어) 코드 몇 줄 추가했더니 관계없는 ISR 의 지연이 들쭉날쭉해졌습니다. 어떻게 진단하나요?</summary>

플래시 캐시/I-cache aliasing 을 의심한다. 링커 맵에서 ISR 과 hot 루프의 주소를 뽑아 `(주소 mod C) / B` 로 set 범위를 계산 → 겹치면 그것 (`ranges_shared_sets`). 코드 추가로 뒤 함수들이 밀리며 우연히 겹쳤을 수 있다. 측정은 GPIO 토글 + 스코프나 DWT 사이클 카운터 (BM-A10 다음 단계) 로 ISR 진입~종료를 히스토그램으로.
꼬리질문: "고치는 법?" → 섹션 정렬로 배치 고정(`place_no_conflict`), ISR 을 ITCM/RAM 으로, 캐시 연관도가 있으면 문제가 작아진다. "캐시 끄기" 는 평균 성능을 다 잃으니 마지막 수단.

</details>

<details>
<summary>Q7. (실리콘 검증) 문서 없는 캐시의 라인 크기와 용량을 소프트웨어로 알아내려면?</summary>

directed test 두 개 (추가 연습 2). 라인 크기: 캐시를 비우고 0 을 읽은 뒤 d = 1, 2, 4… 바이트 떨어진 곳을 읽어 **처음 미스가 나는 d = B**. 용량(직접 사상): 0 → X → 0 에서 마지막 0 이 미스하는 가장 작은 X (B, 2B, 4B, …) = C. 실칩에서는 hit/miss 를 지연(사이클 카운터)이나 PMU refill 카운트로 판정한다.
꼬리질문: "연관도는?" → C 간격 주소 k 개를 돌린 뒤 처음 것을 다시 읽어, 미스가 시작되는 k−1 = way 수. 프리페처가 결과를 오염시키니 끄거나 랜덤 순서로.

</details>

<details>
<summary>Q8. index 를 주소 상위 비트로 잡으면 어떻게 되나요?</summary>

연속된 라인들(배열, 코드)이 **같은 set** 으로 몰린다 — 순차 스캔이 칸 하나를 계속 덮어써 공간 지역성을 다 잃는다. 하위 비트(offset 바로 위) 를 index 로 쓰는 이유는 연속 라인을 서로 다른 set 에 펼치기 위해서다.
꼬리질문: "그런데 L1 이 가상 주소로 index 하면?" → VIPT: index 비트가 페이지 오프셋 안이면 별칭 문제 없음 → "way 당 크기 ≤ 페이지 크기" 제약 (ARCH-B10 Q4, 이 맥 페이지 16 KB).

</details>

## 9. 추가 연습

1. **세 배열**: `c[i] = a[i] + b[i]`, 256 원소, a/b/c 가 C(4 KB) 간격. 패딩 없음, b 만 +32, b +32 · c +64, b +32 · c +32 의 미스는? (768 접근)

<details>
<summary>답</summary>

768 / 544 / **96** / 544. 패딩 없으면 세 라인이 한 set 을 돌려 쓴다 → 전부 미스. b 만 밀면 a 와 c (2C 간격) 가 여전히 충돌 (a 256 + c 256 + b 32). 둘 다 같은 양만큼 밀면 이번엔 b 와 c 가 충돌. **세 배열이 서로 다른 set** 이 되도록 0, +1, +2 라인 → compulsory 96 (32 × 3) 만 남는다.

</details>

2. **블랙박스 형상 찾기**: `bb_flush()`, `bb_access(addr)` (1 = hit) 만으로 숨은 직접 사상 캐시의 B 와 C 를 알아내라.

<details>
<summary>답</summary>

`probe_line_size`: 비우고 0 읽기 → d 를 1 부터 2 배씩, `bb_access(d)` 가 처음 미스인 d = B. `probe_capacity`: X = B 부터 2 배씩, 비우고 0 → X → 0, 마지막이 미스면 X = C. 5 개 형상(1 KB/16 B … 256 B/4 B) 모두 맞춘다. C 를 찾을 때 X < C 면 0 과 X 가 다른 set 이라 0 이 살아남는다는 것이 핵심.

</details>

3. **tag RAM**: 32 비트 주소, 16 KB, 32 B 라인, write-back (valid + dirty). tag 비트와 tag RAM 크기는? 64 B 라인이면? 1 KB / 16 B 플래시 캐시의 tag 는?

<details>
<summary>답</summary>

tag = 32 − 5 − 9 = 18, 512 라인 × 20 bit = 10240 bit (1280 B, 데이터의 7.8%). 64 B 라인이면 256 × 20 = 5120 bit — 라인을 키우는 이유 중 하나가 tag 오버헤드. 1 KB / 16 B: tag = 32 − 10 = 22 bit (플래시 주소 범위만 캐시한다면 실제 하드웨어는 더 적게 저장할 수 있다).

</details>

4. **set 수 닫힌 식**: 한 열(R 줄, pitch = p 라인) 이 쓰는 set 수 = `min(R, S / gcd(p, S))` 를 p = 1..40, R = 1..157 에서 시뮬레이션과 대조하라.

<details>
<summary>답</summary>

`i·p mod S` 는 i 가 `S / gcd(p, S)` 마다 반복된다 (가법군 Z_S 에서 p 가 생성하는 부분군의 크기). R 이 그보다 작으면 R 개 모두 다르다. 시뮬레이션 대조 PASS. 결론: S 가 2^k 면 p 홀수 ⇔ 주기 S.

</details>

5. **전치**: 64×64 int `dst[j][i] = src[i][j]` (src 행 순회 읽기 + dst 열 순회 쓰기), 4 KB / 32 B. pitch 256 (dst = src + 16 KB), pitch 288 (dst = src + 18 KB), pitch 288 에 dst 를 한 라인 더 민 경우의 미스는? (8192 접근)

<details>
<summary>답</summary>

4664 / 1296 / 1268. 하한은 compulsory 1024 (src 512 + dst 512 라인). pitch 256 이면 dst 열 쓰기가 Part B 와 같은 이유로 거의 전부 미스이고, src 의 현재 라인까지 얻어맞는다. 288 로 dst 의 열이 64 set 에 펴지면 대부분 회복. 남는 272/244 는 주로 src 의 현재 라인과 dst 의 64 라인이 set 을 나눠 쓰며 생기는 충돌 — 직접 사상의 한계이고, 타일링(ARCH-M07) 이나 2-way 이상(ARCH-M05) 이 나머지를 줄인다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/esn_ARCH-M04/pr && /tmp/esn_ARCH-M04/pr
# -- 1: three arrays C apart --
#       misses: no pad 768, pad b only 544, pad b=32 c=64 96, pad b=32 c=32 544 (of 768)
# ...
#       misses (of 8192): pitch 256 4664, pitch 288 1296, pitch 288 + dst shifted 1 line 1268
# 16/16 passed   (exit 0)
```

## 10. 핵심 정리

- 직접 사상: S = C / B, `offset = addr & (B−1)`, `index = (addr >> log2 B) & (S−1)`, `tag = addr >> log2 C`. index = (주소 mod C) / B.
- 조회 = 칸 하나 + 비교기 하나 (valid && tag 같음). 교체 정책 없음 — 히트 시간이 가장 짧은 대신 conflict 에 약하다.
- **C 의 배수만큼 떨어진 주소는 같은 set** → 두 배열 C 간격, 2의 거듭제곱 pitch 열 순회, stride s 반복 루프(set 을 C/s 개만 씀) 가 thrash.
- 열 하나가 쓰는 set 수 = `min(R, S / gcd(p, S))` → 패딩은 **홀수 라인**. 4 KB 캐시에서 32 B 패딩이 캐시 4 배와 같은 효과.
- 워킹셋 ≤ S 면 compulsory 외 미스는 전부 conflict. 아니면 FA-LRU 와 비교해야 한다 (Hill 3C, ARCH-M05).
- 라인을 키우면 공간 지역성 ↑, 라인 수 ↓ → 어떤 접근은 오히려 0%.
- 펌웨어: 작은 플래시/I-cache 에서 ISR 과 hot 루프가 같은 set 이면 인터럽트마다 미스 비용이 붙는다 → 링커 배치 검사, ITCM/RAM 실행.

## 참고자료

- Hennessy & Patterson, *Computer Architecture: A Quantitative Approach* — Appendix B (cache organization, 3C model, ways to reduce miss rate), Ch. 2 (compiler optimizations: padding, loop interchange, blocking).
- Patterson & Hennessy, *Computer Organization and Design (ARM Edition)* — Ch. 5.3 (basics of caches: direct-mapped address split, tag overhead).
- M. D. Hill, A. J. Smith, "Evaluating Associativity in CPU Caches", *IEEE Trans. Computers*, 1989 — 3C 정의.
- N. P. Jouppi, "Improving Direct-Mapped Cache Performance by the Addition of a Small Fully-Associative Cache and Prefetch Buffers", ISCA 1990 — victim cache.
- Ulrich Drepper, *What Every Programmer Should Know About Memory* — https://people.freebsd.org/~lstewart/articles/cpumemory.pdf
- STMicroelectronics RM0090 (STM32F405/407 reference manual) — Flash interface, ART accelerator (instruction cache 64 × 128-bit lines).
- Arm Cortex-M7 Technical Reference Manual (L1 caches, TCM) — https://developer.arm.com/documentation/ddi0489/latest
- 관련 레슨: ARCH-B10 (AMAT), ARCH-B12 (플래시 wait state·프리페치), ARCH-M08 (128 B 라인), ARCH-M12 (DMA 와 캐시), 다음 단계 ARCH-M05 (N-way·교체 정책·3C), ARCH-M06 (쓰기 정책), ARCH-M07 (실측 행렬 곱 + 타일링), BM-A10 (DWT 로 IRQ 지연 측정).
