---
id: ARCH-A02
time: 110분
updated: 2026-11-11
---
# ARCH-A02 TLB 모델과 ASID·플러시 비용

## 학습 목표

- ARCH-A01 의 페이지 테이블 워커 앞에 **완전 연관(fully associative) TLB** 를 붙이고(크기 N, 교체 정책 LRU / FIFO / RANDOM), 엔트리 하나가 4 KB 페이지든 2 MB·1 GB 블록이든 **리프 크기 단위로** 한 번에 덮는다는 것을 숫자(512 페이지 순회 → 미스 512 vs 1)로 보인다.
- "TLB 히트는 새로 워크한 결과와 **항상** 같아야 한다" 를 불변식으로 삼아 랜덤 주소 36 만 개로 검사하고, 그 불변식이 깨지는 두 가지 실전 버그 — **unmap 뒤 TLBI 누락(stale 변환)** 과 **ASID 재사용 때 플러시 누락(다른 프로세스의 매핑이 보임)** — 를 직접 재현하고 고친다.
- 두 프로세스가 K 접근마다 교대하는 트레이스로 **전체 플러시 vs 비전역 플러시 vs ASID 태깅** 의 TLB 미스·페이지 워크·워크 읽기 수를 표로 만들고, ASID 가 이기는 조건(두 작업 집합이 TLB 에 함께 들어갈 때)과 이득이 사라지는 조건을 말한다.
- 폭이 작은 ASID 할당기 + 세대(generation) 카운터(Linux arm64 방식의 단일 CPU 축소판)를 짜고, "ASID 가 8 비트면 257 번째 프로세스는?" 에 롤오버 = 전체 플러시 빈도 공식 `floor((S−1)/(2^b−1))` 로 답한다.

## 1. 문제

"TLB 미스 하나에 메모리 접근이 몇 번 생기나요? 컨텍스트 스위치 때 TLB 는 어떻게 되나요?" — Linux 를 올리는 Cortex-A SoC 펌웨어/BSP, 하이퍼바이저, 그리고 SMMU/IOMMU 를 가진 AI 가속기의 검증 면접에서 MMU 다음에 반드시 나오는 질문이다. ARCH-A01 이 "변환이 **맞는가**" 였다면, 이 레슨은 "변환이 **얼마나 드는가**, 그리고 캐시한 변환이 **언제 틀려지는가**" 다.

현실 연결: Cortex-M 은 MMU 가 없어서(MPU 만, ARCH-M15) 이 문제가 없다. Cortex-A 는 L1 micro-TLB 수십 개 + 공유 L2 TLB 수백~수천 개(코어마다 다름, 각 TRM 참고)를 갖고, 엔트리마다 ASID(8 또는 16 비트)를 단다. SoC 의 SMMU(SOCX-M12)도 StreamID·ASID·VMID 로 태그된 TLB 를 갖고 있어서, 드라이버가 버퍼를 unmap 하고 **SMMU TLB 무효화를 빼먹으면 NPU/DMA 가 해제된 메모리에 계속 쓴다** — 이 레슨 Part B 의 첫 번째 버그가 바로 그 모양이다. 이 맥(Apple M6, 16 KB 페이지)의 실제 TLB 비용 측정은 ARCH-A03 이 한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">mmu_access(va) — TLB 히트면 끝, 미스면 단계마다 디스크립터 1 개 읽기 (3-level)</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="95" height="30" class="f-acc"/><rect x="115" y="40" width="95" height="30" class="f-acc"/>
    <rect x="210" y="40" width="95" height="30" class="f-acc"/><rect x="305" y="40" width="95" height="30" class="f-mut"/>
    <rect x="440" y="36" width="280" height="118" rx="6" class="f-ok"/>
    <rect x="20" y="200" width="150" height="40" rx="4" class="f-mut"/>
    <rect x="190" y="200" width="160" height="40" rx="4" class="f-mut"/>
    <rect x="370" y="200" width="160" height="40" rx="4" class="f-mut"/>
    <rect x="550" y="200" width="170" height="40" rx="4" class="f-warn"/>
  </g>
  <g fill="currentColor">
    <text x="28" y="60" font-size="12">[38:30] L1</text><text x="123" y="60" font-size="12">[29:21] L2</text><text x="218" y="60" font-size="12">[20:12] L3</text><text x="313" y="60" font-size="12">[11:0] 오프셋</text>
    <text x="20" y="90">VA (39 비트, 위쪽 비트 ≠ 0 → ADDRESS_SIZE, TLB·워크 없음)</text>
    <text x="452" y="56" font-weight="bold">TLB: N 엔트리, 완전 연관</text>
    <text x="452" y="76" font-family="monospace" font-size="12">V ASID G shift VPN   PA     perms</text>
    <text x="452" y="94" font-family="monospace" font-size="12">1  1   0  12  0x400 0x2000_0 RW-U</text>
    <text x="452" y="112" font-family="monospace" font-size="12">1  1   1  12  0x4000000 ... RW-G</text>
    <text x="452" y="132" font-size="12">맞음: V · (G 또는 ASID 같음) · VPN 같음</text>
    <text x="452" y="148" font-size="12">히트 → 캐시된 perms 로 권한 재검사 → PA</text>
    <text x="30" y="218">TTBR → L1 표</text><text x="30" y="234" font-size="12">읽기 1</text>
    <text x="198" y="218">L2 표</text><text x="198" y="234" font-size="12">읽기 2 · 2 MB 블록 끝</text>
    <text x="378" y="218">L3 표</text><text x="378" y="234" font-size="12">읽기 3 · 4 KB page</text>
    <text x="558" y="218">리프 → 권한 검사</text><text x="558" y="234" font-size="12">NONE·PERMISSION 채움</text>
    <text x="20" y="275">미스 비용 = 리프까지의 단계 수 (walk_reads): 4-level 4, 3-level 3; 3-level 기준 2 MB 블록 2, 1 GB 블록 1.</text>
    <text x="20" y="295">폴트(TRANSLATION)는 채우지 않는다. 테이블을 바꾼 뒤 무효화(TLBI)를 안 하면 TLB 는 옛 답을 계속 준다.</text>
    <text x="20" y="315">ASID: 엔트리에 주소 공간 번호를 달아 스위치 때 비우지 않는다. G=1(커널)은 모든 ASID 와 맞는다.</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5" fill="none">
    <path d="M400 55 L438 55"/><path d="M430 50 L440 55 L430 60"/>
    <path d="M560 154 L560 180 L75 180 L75 198"/><path d="M70 190 L75 199 L80 190"/>
    <path d="M170 220 L188 220"/><path d="M350 220 L368 220"/><path d="M530 220 L548 220"/>
    <path d="M640 200 L640 158" stroke-dasharray="4 3"/><path d="M635 166 L640 156 L645 166"/>
  </g>
  <g fill="currentColor" font-size="12"><text x="566" y="174">미스</text><text x="648" y="186">채움 (fill)</text></g>
</svg>
```

**ARCH-A01 모델을 축소해 넣었다** — 포맷은 A01 과 같다: "ARMv8-A VMSA 4KB granule 을 단순화한 교육용 포맷" 이고 실제 ARM 디스크립터 비트 위치가 **아니다** (ARM 은 nG = not-global, AP[2:1], UXN/PXN 등 다른 배치).

| 비트 | 이름 | 의미 |
|---|---|---|
| 0 | V | valid |
| 1 | T | L0–L2: 1 = 다음 단계 테이블, 0 = 블록(L1 1 GB · L2 2 MB만; L0 블록 = TRANSLATION). L3: 1 = page, 0 = 잘못된 인코딩 → TRANSLATION |
| 2 / 3 / 4 | R / W / X | 리프 권한 (W 단독은 map API 가 거부) |
| 5 | U | user 접근 허용. USER→U=0 = PERMISSION, KERNEL 의 U=1 실행 = PERMISSION (SMEP/PXN 개념) |
| 6 | A | access flag — 이 레슨은 무시 |
| 7 | G | global — **이 레슨의 주인공**: TLB 에서 ASID 와 무관하게 맞는다 |
| 12..47 | OA | 출력 주소 (프레임 / 블록 base, 블록 크기 정렬) |

levels ∈ {2,3,4}, VA 비트 = 12 + 9·levels (30/39/48), 마지막 단계는 항상 L3. 시뮬레이션 물리 메모리 4 MB(1024 프레임)는 **테이블 페이지 전용** 이다 — 데이터 PA 는 숫자일 뿐 내용을 저장하지 않는다. A01 에서 다룬 블록 분할은 여기선 하지 않는다: 블록 일부 unmap 은 `E_SPLIT` 로 거부.

### Part A — TLB 와 통계

- `tlb_lookup(T, va, asid)`: 히트면 인덱스, 미스면 −1. 맞는 조건 = `valid && (global || asid 같음) && (va >> shift) == vpn`. LRU 면 히트 때 stamp 갱신.
- `tlb_victim(T)`: 빈 엔트리 우선, 없으면 LRU·FIFO = stamp 최소, RANDOM = 고정 시드 xorshift32 `% n`.
- `mmu_access(M, va, acc)`: VA 범위 검사 → 조회 → 히트면 캐시된 perms 로 권한 재검사, 미스면 워크(`walk_reads` 셈) → 리프를 찾았으면(NONE 또는 PERMISSION) 채움. 통계 accesses · hits · misses · walks · walk_reads · faults[4].
- 검사: (1) 구성별 첫 접근의 walk_reads(4-level 4, 3-level 3, 2-level 2, 3-level 기준 2 MB 블록 2·1 GB 블록 1 / 4-level 1 GB 블록 2), 둘째 접근은 히트, (2) 2 MB 영역 512 페이지 순회 — 4 KB 페이지면 미스 512, 블록이면 1, (3) a b c d a e 다음 LRU 는 b 를, FIFO 는 a 를 잃는다, N+1 순환에서 LRU·FIFO 히트 0, (4) **랜덤 등가성**: 2 구성 × 3 크기 × 3 정책 × 20000 접근, 매번 TLB 결과 == 새 워크.

### Part B — 무효화, 두 프로세스, 그리고 두 가지 버그

- `tlbi_all` (TLBI VMALLE1 개념), `tlbi_nonglobal` (PCID 없는 x86 CR3 쓰기 효과), `tlbi_asid` (TLBI ASIDE1: 그 ASID 의 비전역만), `tlbi_va_asid` (TLBI VAE1: 그 VA 를 덮는, 그 ASID 또는 **global** 엔트리), `context_switch(M, p, FLUSH_ALL | FLUSH_NONGLOBAL | ASID)`.
- 버그 재현과 수정: unmap 후 TLBI 없이 같은 VA 쓰기 → **해제한 프레임에 쓰기 성공**, 재매핑 후 옛 PA. ASID 3 을 무효화 없이 새 프로세스에 재사용 → B 가 A 의 페이지와 A 의 "비밀" 페이지를 본다. 하네스의 `tlb_audit` 가 TLB 엔트리마다 주인 테이블을 새로 워크해 stale 수를 센다.
- K 스윕: 두 프로세스(같은 user VA → 다른 PA) + 공유 커널 global 8 페이지, K ∈ {1,4,16,64,256,1024}, 작업 집합 16 페이지(합 40 ≤ TLB 64) 와 48 페이지(합 104 > 64), 65536 접근 → 정책별 미스·walk_reads 표.

### Part C — 작은 ASID 할당기와 세대 카운터

- `asid_switch_to(A, M, p)`: 같은 세대면 그대로(빠른 경로), 아니면 비트맵에서 빈 ASID, 없으면 **롤오버**(세대++, 비트맵 비움, TLB 전체 플러시). ASID 0 예약 → 쓸 수 있는 수 2^b − 1.
- 4 비트(15 개)로 롤오버를 강제하는 단위 테스트, P = 20 프로세스의 실제 변환 정확성(+ "롤오버 때 플러시 빼먹기" 돌연변이가 잡히는지), 프로세스 수별 **1000 스위치당 롤오버(= 전체 플러시) 수** 표 (4 비트·8 비트 × RR·랜덤 스케줄).

## 2. Problem (English)

> **TLB with ASIDs on top of a page-table walker.** Reuse the ARCH-A01 educational VMSA-like format (4 KB granule, 9 index bits per level, 2–4 levels, L1/L2 blocks, leaf-only permissions, faults ADDRESS_SIZE / TRANSLATION / PERMISSION). Add a fully associative TLB with N entries and a replacement policy parameter (LRU, FIFO, random with a fixed-seed xorshift). An entry is `{valid, asid, vpn at the leaf granularity, leaf size, pa base, perms, global}`; global entries match any ASID.
>
> A. `int tlb_lookup(tlb_t*, uint64_t va, uint16_t asid)`, `int tlb_victim(tlb_t*)`, `xlate_t mmu_access(mmu_t*, uint64_t va, int acc)`. Count hits, misses, walks and descriptor reads. Invariant: a TLB hit returns exactly what a fresh walk returns (address, leaf size, permissions, fault type and level) — check it on tens of thousands of random addresses, including addresses that fault.
>
> B. Implement invalidation by all / non-global / ASID / VA+ASID and a context switch under three policies: full flush, flush non-global, ASID tagging. Two processes alternate every K accesses: report TLB misses, page walks and descriptor reads for K = 1 … 1024, for working sets that fit and that do not fit in the TLB. Demonstrate (1) a stale translation after unmap without invalidation, (2) process B observing process A's mapping after ASID reuse without a flush — then fix both.
>
> C. A b-bit ASID allocator with a generation counter: on exhaustion, bump the generation, clear the bitmap and flush the whole TLB. Measure full flushes per 1000 context switches versus the number of processes.
>
> Follow-ups: how many memory accesses does one TLB miss cost under 4-level paging, and under virtualization? With 8-bit ASIDs, what happens at the 257th process? Why must the PTE store be ordered before the TLBI, and the TLBI completed before returning to user space on a multi-core system? When does an ASID *not* help?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
```

TODO 는 9 개 함수: Part A `tlb_lookup` · `tlb_victim` · `mmu_access`, Part B `tlbi_all` · `tlbi_nonglobal` · `tlbi_asid` · `tlbi_va_asid` · `context_switch`, Part C `asid_switch_to`. A01 축소판(워커·map·unmap), `tlb_insert`, `entry_to_xlate`, 감사 함수 `tlb_audit`, 트레이스 생성기는 starter 와 solution 이 같다. 스텁 `mmu_access` 는 **"TLB 없는 MMU"** — 매번 워크하므로 결과는 맞지만 히트가 0 이다. 처음 실행 (실제 출력 일부, exit 1):

```text
== Part A: TLB 히트 == 새 워크, 교체 정책, 엔트리 크기 ==
  config            first access (miss)   second access
  3-level 4KB page   walk_reads=3 hit=0     hit=0 walk_reads=6
FAIL starter.c:397  r2.hit && M->st.walk_reads == rd1 && r2.pa == r1.pa + 8
...
  2 MB 영역 512 페이지 순회, 2MB 블록: misses=0 hits=0 walk_reads=1024
FAIL starter.c:410  M->st.misses == 1 && M->st.hits == 511 && M->st.walk_reads == 2
...
  [bug] unmap, no TLBI: access hit=0 fault=TRANSLATION pa=0x0 | fresh walk: TRANSLATION L3 | audit stale=0
FAIL starter.c:553  r0.fault == F_NONE && r1.hit && r1.fault == F_NONE && r1.pa == 0x80000010ull
...
    16 |     0.00       0.00 |     0.00       0.00
63/149 passed
```

재미있는 점: TLB 가 없으면 Part B 의 "버그 재현" 테스트도 **실패** 한다 — stale 변환은 캐시가 있어야 생기는 버그이기 때문이다. 버그를 보려면 먼저 TLB 를 만들어야 한다.

## 4. 힌트

<details><summary>힌트 1 — VPN 은 "리프 크기 단위"</summary>

엔트리에 `shift`(12 / 21 / 30)를 같이 저장하고, 비교는 `(va >> e->shift) == e->vpn`. 그러면 2 MB 블록 엔트리 하나가 512 개 4 KB 페이지를 모두 맞춘다. 4 KB VPN 으로 통일하면 블록 하나가 엔트리 512 개를 먹거나(낭비), 블록 안 다른 페이지에서 미스가 난다(틀린 건 아니지만 TLB 를 쓰는 의미가 없다).
</details>

<details><summary>힌트 2 — 히트에서도 권한을 다시 검사</summary>

TLB 는 **변환과 권한** 을 캐시한다. 폴트 여부는 캐시하지 않는다 — 같은 페이지라도 읽기는 되고 쓰기는 PERMISSION 일 수 있으니까. 그래서 워크가 PERMISSION 으로 끝나도 리프는 찾았으므로 채운다(다음 읽기는 히트). TRANSLATION 은 리프가 없으니 채울 것이 없다. `entry_to_xlate` 가 히트 경로의 결과를 만든다.
</details>

<details><summary>힌트 3 — LRU 와 FIFO 는 stamp 의 의미만 다르다</summary>

`tlb_insert` 가 삽입 때 `stamp = ++clock` 을 이미 해 준다. LRU 는 히트 때도 갱신하고, FIFO 는 갱신하지 않는다. 그러면 victim 은 둘 다 "stamp 최소" 다. 빈 엔트리를 먼저 써야 무효화 직후의 동작이 맞다.
</details>

<details><summary>힌트 4 — TLBI 네 종류의 "맞는 조건"</summary>

all: 전부. nonglobal: `!global`. asid: `!global && asid == a` (global 은 ASID 를 안 보니 남는다). va_asid: `(global || asid == a) && (va >> shift) == vpn` — global 은 **어떤 ASID 로 지워도** 지워져야 한다 (커널 매핑을 바꿨는데 "ASID 가 달라서" 안 지워지면 안 되니까). 지운 수는 `st.inval_entries` 에.
</details>

<details><summary>힌트 5 — 할당기: 세대가 ASID 의 "유효 기간"</summary>

`p->ctx = (gen << 16) | asid`. 스위치할 때 `ctx >> 16 == A->gen` 이면 그 ASID 는 이번 세대에 이 프로세스 것이 확실하다 → 아무것도 안 한다. 다르면 새로 받는다. 빈 게 없으면 세대를 올리고 비트맵을 비우고 **TLB 를 비운 뒤** 다시 찾는다 — 옛 세대의 엔트리가 TLB 에 남아 있으면 같은 번호를 받은 새 주인이 그것을 맞춘다 (Part B 버그 2 와 같은 모양).
</details>

## 5. 풀이

### Part A — TLB

**조회** 는 n 개 엔트리를 모두 비교한다. 하드웨어에선 CAM(content-addressable memory)의 병렬 비교라서 한 사이클이지만, 엔트리마다 비교기가 필요해서 L1 micro-TLB 가 수십 개로 작은 이유가 된다. 큰 L2 TLB 는 보통 set-associative 이고, 이때 크기가 다른 엔트리(4 KB / 2 MB)를 어떻게 인덱싱하느냐가 설계 문제가 된다(크기별 따로 두거나, 여러 번 조회) — 이 레슨은 완전 연관이라 그 문제가 없다.

```c
for (int i = 0; i < T->n; i++) {
    tlbe_t *e = &T->e[i];
    if (e->valid && (e->global || e->asid == asid) && (va >> e->shift) == e->vpn) {
        if (T->pol == R_LRU) e->stamp = ++T->clock;
        return i;
    }
}
return -1;
```

**접근** 은 세 갈래: VA 범위 밖이면 TLB 도 워크도 없이 ADDRESS_SIZE, 히트면 `entry_to_xlate`(PA = base | offset, perms 재검사), 미스면 `walk(..., &st.walk_reads)` 후 리프가 있으면 채움.

**워크 비용 (실제 출력)** — 미스 한 번의 디스크립터 읽기 수 = 리프까지의 단계 수:

| 구성 | 첫 접근 walk_reads | 둘째 접근 |
|---|---|---|
| 3-level 4 KB | 3 | 히트, 추가 0 |
| 4-level 4 KB | 4 | 히트 |
| 2-level 4 KB | 2 | 히트 |
| 3-level 2 MB 블록 | 2 | 히트 |
| 3-level 1 GB 블록 | 1 | 히트 |
| 4-level 1 GB 블록 | 2 | 히트 |

그리고 **엔트리 하나의 범위**: 2 MB 영역의 512 페이지를 한 번씩 → 4 KB 페이지면 `misses=512 hits=0 walk_reads=1536`, 2 MB 블록이면 `misses=1 hits=511 walk_reads=2`. 이게 huge page 가 TLB 도달 범위(reach)를 키우는 원리 그대로다 (연습 1: 8 MB 랜덤 접근 미스율 96.7% → 0.0%).

**교체 정책 (실제 출력)**:

```text
  N=4 LRU   : a b c d a e → 그 다음 b miss, a hit
  N=4 FIFO  : a b c d a e → 그 다음 b hit , a miss
  N=4 RANDOM: a b c d a e → 그 다음 b hit , a hit
  N=4, 5 페이지 순환 × 100: hits LRU=0 FIFO=0 RANDOM=288 (of 500)
```

LRU 는 a 를 다시 쓴 것을 기억해서 b 를 버리고, FIFO 는 a 가 가장 먼저 들어왔다는 이유로 버린다. 크기 N 에 N+1 페이지 순환은 LRU·FIFO 의 최악: 매번 "곧 쓸 것" 을 버려 히트 0. RANDOM 은 그 병적 패턴에 강해서 58% 히트. 실제 TLB 가 정확한 LRU 대신 pseudo-LRU 나 랜덤 계열을 쓰는 이유 중 하나다 (엔트리 많을 때 정확한 LRU 의 비용 + 이 병적 패턴, ARCH-M05 참고).

**랜덤 등가성** — 이 레슨의 첫 번째 불변식. user text(RX-U) · data(RW-U) · ro(R-U) 4 KB 페이지, 커널 2 MB 블록 2 개, 1 GB 블록(RX-G), 커널 global 4 KB 8 개, 구멍, VA 범위 밖 주소를 섞고, 접근 종류와 USER/KERNEL 도 랜덤:

```text
  random 3-level N=4  LRU: acc=20000 hits=2988 misses=16603 walk_reads=42677  faults T=3877 P=9168 AS=409
  random 3-level N=16 LRU: acc=20000 hits=7926 misses=11696 walk_reads=31835  faults T=3847 P=9118 AS=378
  random 3-level N=64 LRU: acc=20000 hits=15409 misses=4195 walk_reads=9777  faults T=3859 P=9273 AS=396
  equivalence: 18 configs, 360000 accesses, 153790 TLB hits (93636 permission faults from hits), mismatches=0
```

폴트 수(T·P·AS)가 TLB 크기와 무관하게 거의 같은 것 — 그 자체가 검사다. TLB 는 **성능만** 바꾸고 결과를 바꾸면 안 된다. 히트에서 나온 PERMISSION 폴트가 93636 번 있었다는 건 "권한을 캐시된 perms 로 다시 검사" 하는 경로가 실제로 많이 실행됐다는 커버리지 증거다(A04 의 "checker 를 믿으려면 커버리지도 본다" 와 같은 태도). 미스 수 + 히트 수 + AS 폴트 수 = 접근 수, walks = misses 도 매 구성 CHECK 한다.

### Part B — 무효화와 컨텍스트 스위치

**무효화 네 종류와 실제 아키텍처의 대응** (실제 명령의 세부 범위는 ARM ARM 의 TLBI 절을 확인할 것 — 아래는 개념 대응):

| 이 모델 | 지우는 것 | ARMv8-A 대응 (EL1&0) | x86 대응 |
|---|---|---|---|
| `tlbi_all` | 전부 | `TLBI VMALLE1(IS)` | CR4.PGE 토글 등 (global 까지) |
| `tlbi_nonglobal` | G=0 전부 | (ASID 없이 쓰는 것과 같음) | PCID 없는 `mov cr3` |
| `tlbi_asid(a)` | ASID a 의 G=0 | `TLBI ASIDE1(IS)` | `INVPCID` 단일 컨텍스트 |
| `tlbi_va_asid(va,a)` | va 를 덮는 (ASID a 또는 G=1) | `TLBI VAE1(IS)` (마지막 단계만이면 `VALE1`) | `INVLPG` (현재 PCID) |

실제 출력 — flush 범위:

```text
  B after A (ASID): user VA hit=0 pa=0x21000000 (own), kernel VA hit=1 (global)
```

같은 user VA 지만 B 는 자기 ASID 2 로 조회하므로 A 의 엔트리(ASID 1)가 안 맞고 자기 PA 를 워크한다. 커널 VA 는 A 가 채운 global 엔트리가 그대로 맞는다. 그 뒤 CHECK: `tlbi_asid(1)` → A 의 user 만 사라지고 global 은 남음, `tlbi_va_asid(KVA, 7)` → **무관한 ASID 7 로도 global 이 지워짐**, `tlbi_nonglobal` → global 하나만 남음, `tlbi_all` → 0, 지운 엔트리 총 4.

**버그 1 — unmap 뒤 TLBI 누락** (실제 출력):

```text
  [bug] unmap, no TLBI: access hit=1 fault=NONE pa=0x80000010 | fresh walk: TRANSLATION L3 | audit stale=1
  [fix] + TLBI VA,ASID:   access hit=0 fault=TRANSLATION L3 | audit stale=0
  remap 0x90000000→0xA0000000: before TLBI pa=0x90000000, after pa=0xa0000000
```

페이지 테이블은 "없다" 고 말하는데 TLB 는 "0x80000010 에 쓰세요" 라고 한다. OS 가 그 프레임을 다른 프로세스나 DMA 버퍼로 재사용했다면 **조용한 메모리 오염** 이다. 재매핑도 같다: 새 PA 를 써 넣어도 무효화 전까지는 옛 PA 로 간다. 올바른 순서 (ARM, 다중 코어 — ARM-M05 의 표와 같음):

```text
str  xzr, [pte]            ; 1. 테이블 수정 (break)
dsb  ishst                 ; 2. 그 store 가 다른 코어의 워커에게 보이게
tlbi vae1is, x0            ; 3. VA+ASID 무효화를 Inner Shareable 도메인 전체에 방송
dsb  ish                   ; 4. 모든 코어에서 무효화가 '완료' 될 때까지 대기
isb                        ; 5. 이 코어의 파이프라인을 새 컨텍스트로 (필요한 경우)
; 6. 이제야 프레임을 free 할 수 있다
```

x86 은 TLBI 방송 명령이 없어서 다른 코어에 IPI 를 보내 `INVLPG` 를 시키는 **TLB shootdown** 을 한다 — 코어 수가 많을수록 비싸다 (RISC-V 도 `sfence.vma` 가 로컬이라 IPI/SBI 로 원격 펜스, RISCV-A02). SMMU 는 커맨드 큐에 TLBI 명령을 넣고 `CMD_SYNC` 로 완료를 기다리는 모양이다 (SOCX-M12 에서 자세히).

**버그 2 — ASID 재사용 시 플러시 누락** (실제 출력):

```text
  [bug] ASID 3 reused, no flush: B VA 0x400000 → pa 0xa0000000 (want 0xb0000000), B VA 0x410000 → NONE pa 0xa0010000, stale=2
  [fix] TLBI ASID 3 before reuse: B → pa 0xb0000000, secret VA → TRANSLATION
```

A 가 종료하자 "순진한 할당기" 가 ASID 3 을 곧바로 B 에게 줬다. B 의 0x400000 은 A 의 프레임으로 가고, B 가 매핑한 적도 없는 0x410000 이 A 의 비밀 페이지를 읽는다 — **격리(isolation) 위반**, 보안 버그다. 고치는 법은 둘: 재사용 직전 `TLBI ASIDE1`, 또는 Part C 처럼 ASID 를 개별 반납하지 않고 세대가 넘어갈 때 한꺼번에 비우기.

**K 스윕 (실제 출력)** — 3-level, TLB 64 LRU, 65536 접근, 25% 커널(global 8 페이지):

| K | FLUSH_ALL 미스 | NONGLOBAL 미스 | ASID 미스 | ALL/ASID |
|---|---|---|---|---|
| ws=16 (합 40 ≤ 64) | | | | |
| 1 | 65536 | 48999 | 40 | 1638.4× |
| 4 | 61269 | 45582 | 40 | 1531.7× |
| 16 | 48130 | 35048 | 40 | 1203.2× |
| 64 | 22739 | 15635 | 40 | 568.5× |
| 256 | 6144 | 4104 | 40 | 153.6× |
| 1024 | 1536 | 1032 | 40 | 38.4× |
| ws=48 (합 104 > 64) | | | | |
| 1 | 65536 | 48999 | 21045 | 3.1× |
| 16 | 56750 | 43668 | 20833 | 2.7× |
| 64 | 38294 | 31190 | 22437 | 1.7× |
| 256 | 14133 | 12093 | 12010 | 1.2× |
| 1024 | 3584 | 3080 | **3081** | 1.2× |

walk_reads 는 모든 칸에서 정확히 미스 × 3 (전부 4 KB 페이지, 3-level) 이고, 4-level 로 바꾸면 미스 수는 같고(48130 = 48130) walk_reads 만 × 4 (192520 vs 144390).

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">TLB 미스 수 vs 스위치 간격 K (실제 출력, 로그 축, 65536 접근)</text>
  <g stroke="currentColor" stroke-width="1">
    <path d="M70 60 L70 295 L710 295"/>
    <path d="M70 70 L710 70 M70 125 L710 125 M70 180 L710 180 M70 235 L710 235" stroke-dasharray="2 4" opacity="0.4"/>
  </g>
  <g fill="currentColor">
    <text x="22" y="74">100k</text><text x="30" y="129">10k</text><text x="36" y="184">1k</text><text x="34" y="239">100</text><text x="42" y="294">10</text>
    <text x="86" y="312">1</text><text x="206" y="312">4</text><text x="324" y="312">16</text><text x="444" y="312">64</text>
    <text x="560" y="312">256</text><text x="678" y="312">1024</text><text x="360" y="332">K (접근 / 스위치)</text>
  </g>
  <g fill="none" stroke="currentColor" stroke-width="2">
    <polyline points="90,80 210,82 330,87 450,105 570,137 690,170"/>
    <polyline points="90,87 210,89 330,95 450,114 570,146 690,179" stroke-dasharray="6 4"/>
    <polyline points="90,257 210,257 330,257 450,257 570,257 690,257"/>
    <polyline points="90,107 210,107 330,107 450,106 570,121 690,153" stroke-dasharray="2 3"/>
  </g>
  <g stroke="currentColor">
    <circle cx="90" cy="80" r="4" class="f-warn"/><circle cx="210" cy="82" r="4" class="f-warn"/><circle cx="330" cy="87" r="4" class="f-warn"/>
    <circle cx="450" cy="105" r="4" class="f-warn"/><circle cx="570" cy="137" r="4" class="f-warn"/><circle cx="690" cy="170" r="4" class="f-warn"/>
    <circle cx="90" cy="257" r="4" class="f-ok"/><circle cx="210" cy="257" r="4" class="f-ok"/><circle cx="330" cy="257" r="4" class="f-ok"/>
    <circle cx="450" cy="257" r="4" class="f-ok"/><circle cx="570" cy="257" r="4" class="f-ok"/><circle cx="690" cy="257" r="4" class="f-ok"/>
    <rect x="86" y="103" width="8" height="8" class="f-acc"/><rect x="206" y="103" width="8" height="8" class="f-acc"/>
    <rect x="326" y="103" width="8" height="8" class="f-acc"/><rect x="446" y="102" width="8" height="8" class="f-acc"/>
    <rect x="566" y="117" width="8" height="8" class="f-acc"/><rect x="686" y="149" width="8" height="8" class="f-acc"/>
  </g>
  <g fill="currentColor">
    <text x="470" y="96">FLUSH_ALL ws=16 (실선, ●)</text>
    <text x="196" y="174">NONGLOBAL ws=16 (긴 점선) — 커널 global 이 살아남아 25–33% 적다</text>
    <text x="96" y="152">ASID ws=48 (짧은 점선, ■) — 둘이 TLB 에 안 들어가면 이득 1.2–3×</text>
    <text x="300" y="250">ASID ws=16 — 40 = 강제 미스(서로 다른 페이지 수)뿐</text>
  </g>
</svg>
```

읽는 법:

1. **ws=16, ASID = 40 (모든 K)**: 서로 다른 페이지 16 + 16 + 8 = 40 이 TLB 64 에 다 들어가므로 첫 접근(강제 미스)만 미스. 플러시가 없으니 스위치 횟수와 무관하다. 1638× 같은 비율은 "ASID 쪽이 0 에 가깝다" 는 뜻일 뿐 의미 있는 배수가 아니다.
2. **FLUSH_ALL, K=1 → 65536 = 100% 미스**: 매 접근 전에 비우니 당연. K 가 커지면 스위치당 "다시 채우는 비용" 이 K 접근에 나눠진다: K=1024 에서 64 번 스위치 × 약 24 페이지 ≈ 1536.
3. **NONGLOBAL 은 FLUSH_ALL 의 67–75%** (K=1 에서 48999/65536 = 75%, K=1024 에서 1032/1536 = 67%): 접근의 25% 가 커널이고 global 8 페이지가 살아남기 때문. 이게 x86 이 PCID 이전에 커널 페이지에 G 비트를 쓴 이유이고, Meltdown 대응(KPTI)이 user 모드에서 커널 매핑을 빼면서 이 이득을 잃었다가 PCID 로 되찾은 이야기의 숫자 버전이다.
4. **ws=48 (합 104 > 64)**: ASID 가 있어도 들어온 프로세스가 자기 페이지로 상대 엔트리를 밀어낸다. K 가 커질수록 "어차피 밀려날 것" 이라 이득이 1.2× 까지 줄고, K=1024 에서는 **ASID 가 NONGLOBAL 보다 1 개 더 많다** (3081 vs 3080). 플러시는 빈 엔트리를 만들어 주고, ASID 는 쓸모없어진 상대 엔트리를 LRU 가 밀어낼 때까지 들고 있다 — 미세하지만 "ASID 는 항상 이긴다" 가 거짓임을 보여 주는 결정적(deterministic) 반례다. 하네스는 이 1 건을 기대값으로 고정해 둔다.
5. ws=48 의 ASID 미스가 K=16(20833) 보다 K=64(22437) 에서 더 많은 비단조(non-monotonic) 구간도 있다. 해석(검증은 안 함): K≈64 는 한 퀀텀이 상대 엔트리를 다 밀어낼 만큼 길지만, 다시 채운 비용을 나눠 낼 만큼 길지는 않은 길이다.

**현실의 K**: 스케줄러 퀀텀(ms) 사이에는 수백만 번의 메모리 접근이 있으니 일반 스위치의 K 는 매우 크고, 이 표의 왼쪽은 **시스템 콜이 많은 워크로드 + KPTI**(시스템 콜마다 페이지 테이블 2 번 교체), 마이크로커널 IPC, 하이퍼바이저 world switch, 그리고 OS-M02 의 파이프 핑퐁 같은 경우다. OS-M02 에서 측정한 "프로세스 vs 스레드 왕복 차이 약 250 ns" 가 작았던 이유가 ASID 다.

**미스 한 번의 시간**: walk_reads 는 메모리 **읽기 횟수** 이지 시간이 아니다. 워커는 보통 데이터 캐시를 통해 디스크립터를 읽고(ARM 은 TCR 의 캐시 속성 설정에 따름), 상위 단계 디스크립터를 따로 캐시하는 **페이지 워크 캐시** 도 있다 (연습 2: 4-level 미스당 읽기 4.000 → 1.000). 그래서 실제 미스 비용은 수십 사이클(캐시 히트)부터 수백 ns(DRAM 까지)까지 넓다 — ARCH-A03 이 이 맥에서 측정한다.

### Part C — ASID 할당기와 세대

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">4 비트 ASID (1..15), RR 로 P=16 프로세스 — 15 스위치마다 롤오버 = TLB 전체 플러시</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="300" height="40" class="f-ok"/>
    <rect x="320" y="40" width="60" height="40" class="f-warn"/>
    <rect x="380" y="40" width="300" height="40" class="f-ok"/>
    <rect x="680" y="40" width="40" height="40" class="f-warn"/>
    <rect x="20" y="130" width="330" height="90" rx="6" class="f-mut"/>
    <rect x="380" y="130" width="340" height="90" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="28" y="58">세대 1: p0..p14 → ASID 1..15</text><text x="28" y="74" font-size="12">스위치 1–15, 할당만, 플러시 없음</text>
    <text x="326" y="58">#16</text><text x="326" y="74">p15</text>
    <text x="388" y="58">세대 2: p15→1, p0→2, …, p13→15</text><text x="388" y="74" font-size="12">p0 의 ctx 세대 1 ≠ 2 → 새로 할당</text>
    <text x="684" y="58">#31</text><text x="684" y="74">p14</text>
    <text x="320" y="100">빈 ASID 없음 → gen++ · 비트맵 비움 · TLBI ALL · 다시 찾기</text>
    <text x="30" y="150" font-weight="bold">빠른 경로 (같은 세대)</text>
    <text x="30" y="170">ctx &gt;&gt; 16 == gen → ASID 유지, 플러시 없음</text>
    <text x="30" y="188">P ≤ 2^b − 1 이면 처음 P 번 뒤로 영원히 이 경로</text>
    <text x="30" y="206">→ 롤오버 0 (표의 P = 4, 8, 15)</text>
    <text x="390" y="150" font-weight="bold">RR 최악 (P &gt; 2^b − 1)</text>
    <text x="390" y="170">돌아올 때마다 세대가 지나 있어 매번 할당</text>
    <text x="390" y="188">→ 롤오버 = floor((S − 1) / (2^b − 1))</text>
    <text x="390" y="206">4 비트 66.66, 8 비트 3.92 / 1000 스위치</text>
  </g>
</svg>
```

**알고리즘** (TODO 의 핵심 줄):

```c
uint32_t cap = (1u << A->bits) - 1u;                       /* ASID 0 예약 */
if (p->ctx == 0 || (p->ctx >> CTX_GEN_SHIFT) != A->gen) {   /* 옛 세대(또는 처음) → 새로 받기 */
    ... hint 부터 [1, cap] 에서 빈 것 ...
    if (!a) { A->gen++; memset(A->used, 0, ...); A->rollovers++; A->hint = 1; rollover_flush(M); }
    ...
    p->ctx = (A->gen << CTX_GEN_SHIFT) | a;
}
```

왜 개별 반납(free) 대신 세대인가: 프로세스가 끝날 때 ASID 를 반납하고 바로 재사용하려면 **재사용 직전마다** 그 ASID 의 TLB 엔트리를 지워야 한다(Part B 버그 2). 다중 코어에선 그게 방송 무효화 + 완료 대기다. 세대 방식은 번호가 다 떨어질 때 **한 번** 전체를 비우고, 그 사이 스위치는 비교 한 번(빠른 경로)으로 끝난다. Linux arm64 의 `arch/arm64/mm/context.c` 가 이 구조다 — 다만 실제 구현은 (a) CPU 마다 지금 돌고 있는 프로세스의 ASID 를 "예약" 해서 롤오버 뒤에도 유지하고, (b) 플러시를 즉시 방송하지 않고 CPU 마다 "다음 스위치 때 로컬 플러시" 로 미루며, (c) KPTI 가 켜지면 ASID 를 커널/유저 짝으로 써서 실제 수가 절반이 된다. 세부는 커널 버전마다 다르니 이 모델은 **단일 CPU 로 단순화** 했다고 말하자.

**단위 테스트 (실제 출력)**:

```text
  4-bit: 15 procs → asids 1..15, 16th proc → rollover=1 gen=2 asid=1 tlbi_all=1
```

**실제 변환으로 정확성** — P = 20, 4 비트, 3000 스위치 × 8 접근, TLB 256(L2 TLB 급, 대표값), 매 접근 TLB 결과 == 새 워크:

```text
  P=20 4-bit RR    : ASID alloc misses=13975 rollovers=199 mismatches=0 stale=0 | FLUSH_ALL misses=18004 | mutant(no flush) mismatches=13462 stale=112
  P=20 4-bit random: ASID alloc misses=9241 rollovers=103 mismatches=0 stale=0 | FLUSH_ALL misses=17926 | mutant(no flush) mismatches=16923 stale=15
```

- 롤오버 199 = floor(2999/15) — RR 공식 그대로. 롤오버가 15 스위치마다 있어도 미스는 FLUSH_ALL 의 78%(RR), 52%(랜덤).
- **돌연변이** `rollover_flush` 를 끄면(하네스의 주입 지점) 새 세대에서 같은 번호를 받은 프로세스가 옛 주인의 엔트리를 맞춘다: 불일치 13462 / 16923. TLB 가 커서(256) 옛 엔트리가 잘 살아남을수록 버그가 더 잘 보인다 — 작은 TLB 로만 테스트하면 이 버그는 숨는다.

**1000 스위치당 롤오버 (= 전체 플러시) 수 (실제 출력, 100000 스위치)**:

| P | 4 비트 RR | 4 비트 랜덤 | 8 비트 RR | 8 비트 랜덤 |
|---|---|---|---|---|
| 4 · 8 · 15 | 0 | 0 | 0 | 0 |
| 16 | 66.66 | 18.85 | 0 | 0 |
| 17 | 66.66 | 24.51 | 0 | 0 |
| 32 | 66.66 | 48.35 | 0 | 0 |
| 64 | 66.66 | 57.94 | 0 | 0 |
| 255 | 66.66 | 64.53 | 0 | 0 |
| 256 | 66.66 | 64.60 | 3.92 | 0.63 |
| 300 | 66.66 | 64.90 | 3.92 | 1.74 |
| 1000 | 66.66 | 66.13 | 3.92 | 3.39 |

비교 기준: ASID 없는 FLUSH_ALL 은 1000. 읽는 법: (1) **절벽**: P ≤ 2^b − 1 이면 0, 하나만 넘어도 RR 은 바로 최악값 1000/(2^b − 1). (2) RR 이 최악인 이유는 LRU 의 N+1 순환과 같다 — 돌아올 때면 항상 세대가 지나 있다. 랜덤 스케줄은 같은 세대 안에 다시 뽑힐 기회가 있어 적다. (3) 8 비트 + 프로세스 수천 개(서버)면 255 스위치마다 전체 플러시 → 16 비트 ASID(65535)가 필요한 이유. 반대로 ASID 가 16 비트여도 **가상 머신마다** 독립이라 VMID(8/16 비트)가 따로 있다 (VIRT 트랙).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so      # 149/149 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o san && ./san   # 깨끗
```

바뀐 줄은 TODO 9 개의 몸통뿐이다. 스텁에서 `(void)tlb_lookup;` 같은 줄은 "아직 안 쓰는 static 함수" 경고(-Werror)를 막기 위한 것이다.

## 7. 자주 하는 실수

1. **VPN 을 항상 4 KB 단위로 저장** → 블록 엔트리가 블록 안의 다른 페이지와 안 맞아 블록의 이점이 사라진다. 고침: 엔트리에 `shift` 저장, 비교 `(va >> shift) == vpn`.
2. **히트에서 권한 검사 생략** ("TLB 에 있으면 허용") → 읽기로 채운 엔트리에 쓰기가 통과, USER 가 커널 global 페이지 접근. 고침: `entry_to_xlate` 가 캐시된 perms 로 `perm_check`. 반대로 PERMISSION 폴트 결과를 **채우지 않으면** 결과는 맞지만 같은 페이지의 다음 정상 접근이 또 워크한다.
3. **`tlbi_asid` 가 global 까지 지움** → 결과는 맞지만 매 플러시마다 커널 엔트리를 잃는다(성능 버그, 테스트 B-1 이 잡음). 반대로 **`tlbi_va_asid` 가 ASID 가 다르다고 global 을 남김** → 커널 매핑 변경이 일부 코어/ASID 에서 안 보이는 정확성 버그.
4. **테이블 수정 전에 TLBI** (`tlbi; str pte`) → 무효화와 store 사이에 다른 코어(또는 투기적 워크)가 옛 PTE 로 다시 채울 수 있다. 순서는 항상 store → `dsb ishst` → `tlbi` → `dsb ish` → (`isb`) → 프레임 해제.
5. **스위치 때 TTBR 만 바꾸고 ASID 를 안 바꿈** (starter 의 `context_switch` 스텁) → 다른 프로세스의 엔트리가 그대로 맞는다. ARM 에서 TTBR0 과 ASID 는 같은 레지스터의 두 필드이므로 한 번의 `msr ttbr0_el1` 로 같이 바꾸는 게 자연스럽다 (TCR_EL1.A1 로 ASID 가 TTBR0/1 중 어디 있는지 고름).
6. **ASID 를 프로세스 종료 때 반납하고 바로 재사용** → Part B 버그 2. 반납하려면 재사용 전 `TLBI ASIDE1IS` + 완료 대기, 아니면 세대 방식.
7. **롤오버 테스트를 작은 TLB 로만 함** → 옛 엔트리가 이미 밀려나서 플러시 누락이 안 보인다. 돌연변이 검사는 TLB 를 크게(여기선 256) 해서 했다.

## 8. 면접 Q&A

<details><summary>Q1. TLB 미스 하나에 메모리 접근이 몇 번 생기나? 가상화에서는?</summary>

네이티브 4-level(4 KB): 디스크립터 4 번 + 원래 데이터 1 번 = 5 번 (이 모델의 walk_reads = 4). 3-level 이면 3, 2 MB 블록이면 한 단계 줄어든다. 가상화(2 단계 변환, Stage-1 게스트 + Stage-2 호스트)에선 게스트 테이블의 각 디스크립터 주소 자체가 게스트 물리 주소(IPA)라서 그것도 Stage-2 로 변환해야 한다 → 최악 (4+1)×(4+1) − 1 = **24 번** (VIRT-B05). 실제로는 페이지 워크 캐시(상위 단계 디스크립터 캐시, 연습 2 에서 4 → 1 읽기)와 데이터 캐시 히트 덕에 평균은 훨씬 적다.

꼬리질문: "그럼 미스 비용은 몇 ns?" → 읽기 횟수 × 그 읽기가 어디서 히트하느냐. 모두 L1/L2 히트면 수십 사이클, DRAM 까지 가면 수백 ns. 측정은 포인터 체이싱으로 페이지 수를 늘려 가며 TLB 도달 범위를 넘을 때의 계단을 본다 (ARCH-A03).
</details>

<details><summary>Q2. ASID 가 8 비트면 257 번째 프로세스는 어떻게 되나?</summary>

8 비트 = 256 개 번호. ASID 0 을 예약하는 이 모델에선 (Linux arm64 의 할당기도 0 번은 주지 않는 구조로 알고 있다 — 커널 버전별로 확인) 쓸 수 있는 게 255 개라 **256 번째** 서로 다른 프로세스가 첫 롤오버를 일으킨다 (연습 4 실측). 롤오버 = 세대++, 비트맵 비움, TLB 전체 플러시, 그 뒤 모든 프로세스는 다음 스위치 때 새 번호를 받는다. 번호를 전부 쓸 수 있는 설계라도 257 번째에서 같은 일이 생긴다. 프로세스가 아무리 많아도 **정확성은 유지** 되고 비용만 생긴다: RR 최악이면 255 스위치마다 전체 플러시(1000 스위치당 3.92), 동시에 도는 프로세스가 255 이하면 롤오버 후 다시 안정.

꼬리질문: "16 비트를 지원하면 왜 안 쓰나?" → 쓴다 (ARMv8 은 `ID_AA64MMFR0_EL1.ASIDBits` 로 16 비트 지원 여부를 알리고 TCR_EL1.AS 로 켠다). 8 비트만 구현한 코어가 있을 뿐. x86 PCID 는 12 비트(4096)지만 Linux 는 CPU 당 몇 개만 동적으로 돌려 쓰고 나머지 엔트리는 그때그때 무효화한다 — "번호 공간 크기" 와 "TLB 에 실제로 남아 있을 수 있는 주소 공간 수" 는 다른 문제다.
</details>

<details><summary>Q3. 페이지를 unmap 할 때 정확한 순서는? 멀티코어에선?</summary>

(1) PTE 를 지운다, (2) `dsb ishst` 로 그 store 를 다른 코어의 워커가 보게 한다, (3) `tlbi vae1is` (VA+ASID, Inner Shareable 방송), (4) `dsb ish` 로 **모든 코어에서 완료** 를 기다린다, (5) 필요하면 `isb`, (6) 이제야 프레임을 해제한다. 무효화보다 해제가 먼저면 Part B 버그 1 — 다른 코어가 아직 캐시된 변환으로 해제된 프레임에 쓴다. x86 은 방송 TLBI 가 없어 IPI 로 다른 코어에 `INVLPG` 를 시키고 응답을 기다린다(shootdown). RISC-V 는 `sfence.vma` 가 로컬이라 SBI 원격 펜스(RISCV-A02).

꼬리질문: "권한을 **늘릴** 때(읽기 전용 → 쓰기 가능)도 무효화가 필요한가?" → 옛 엔트리는 더 엄격할 뿐이라 최악은 "헛 폴트(spurious fault)" 한 번이다. 그래서 많은 OS 가 권한 완화 때는 무효화를 생략하고 폴트 핸들러에서 "테이블은 이미 허용" 이면 그냥 돌아간다 — 아키텍처·OS 마다 다르니 단정하지 말 것. 권한을 **줄일** 때(COW 를 위한 쓰기 금지, mprotect)는 반드시 무효화.
</details>

<details><summary>Q4. global 비트는 왜 있고, KPTI 는 그것과 무슨 관계인가?</summary>

커널 매핑은 모든 프로세스에서 같으니 ASID 마다 따로 캐시할 필요가 없다. G=1 이면 엔트리 하나가 모든 ASID 에 맞고, ASID 플러시나 (x86) CR3 교체에서도 살아남는다 — 표의 NONGLOBAL 이 FLUSH_ALL 보다 25–33% 적은 이유. Meltdown 이후 KPTI 는 user 모드 페이지 테이블에서 커널 대부분을 빼야 했으니 커널 페이지를 global 로 둘 수 없게 됐고(그러면 user 모드에서도 TLB 에 남는다), 시스템 콜마다 테이블을 바꾸는 비용이 커졌다. 그걸 줄인 게 PCID/ASID: 커널용·유저용 주소 공간에 서로 다른 태그를 줘서 바꿔도 비우지 않는다 (Linux arm64 는 ASID 짝으로).

꼬리질문: "global 엔트리를 지우려면?" → VA 기반 무효화(이 모델의 `tlbi_va_asid`, ARM `TLBI VAE1`)는 ASID 와 무관하게 global 을 맞춘다. ASID 기반 무효화로는 안 지워진다 — 테스트 B-1.
</details>

<details><summary>Q5. ASID 가 도움이 안 되는 경우는?</summary>

(1) 두 프로세스의 작업 집합 합이 TLB 보다 클 때: 들어온 쪽이 어차피 상대 엔트리를 밀어낸다 — ws=48 에서 이득 3× → 1.2×, K=1024 에선 ASID 가 1 개 **더** 많이 미스(3081 vs 3080): 플러시가 빈 자리를 만들어 주는 반면 ASID 는 죽은 엔트리를 LRU 가 밀어낼 때까지 들고 있다. (2) 스위치 간격이 아주 길 때: 재적재 비용이 퀀텀에 비해 무시할 만하다. (3) 프로세스 수 > ASID 수이고 RR 일 때: 2^b − 1 스위치마다 전체 플러시. (4) 작업 집합이 큰 단일 프로세스(huge page 가 답이다, 연습 1).

꼬리질문: "그럼 언제 결정적으로 이기나?" → 스위치가 잦고(시스템 콜 + KPTI, IPC, VM exit) 작업 집합이 작을 때. ws=16 에서 K=1 이면 65536 vs 40 미스.
</details>

<details><summary>Q6. 4 KB 페이지를 2 MB 블록으로 합칠 때 무엇을 조심하나? (break-before-make)</summary>

옛 4 KB 엔트리들을 무효화하지 않고 블록을 만들면, 한 VA 에 **4 KB 엔트리와 2 MB 엔트리가 동시에** TLB 에 있을 수 있다 (연습 3: 최대 2 개 매칭, 그리고 옛 4 KB 엔트리가 먼저 맞아 page 3 이 옛 PA 0x40003000 으로 간다). ARMv8 은 이런 경우 "TLB conflict abort" 를 낼 수 있거나 결과를 보장하지 않으므로, 크기·출력 주소를 바꿀 때 **break(무효 디스크립터) → TLBI → make(새 디스크립터)** 순서를 요구한다 (FEAT_BBM 이 일부 경우를 완화 — 세부는 ARM ARM 확인). 리눅스의 THP 합치기/쪼개기, 펌웨어의 early boot 블록 매핑을 페이지로 쪼갤 때(A01 Part B) 같은 순서가 필요하다.

꼬리질문: "break 와 make 사이에 다른 코어가 그 주소에 접근하면?" → 폴트가 난다. 그래서 커널은 그 구간을 잠그거나(page table lock), 폴트 핸들러가 "변경 중" 이면 재시도하게 만든다. 커널 자신의 텍스트처럼 폴트가 나면 안 되는 영역은 다른 경로(정지된 다른 코어 등)를 쓴다.
</details>

<details><summary>Q7. (실리콘/SoC) NPU 나 DMA 앞의 SMMU 에도 같은 문제가 있나?</summary>

있다. SMMU/IOMMU 는 장치용 MMU 이고 자기 TLB 를 가진다(태그: StreamID 로 찾은 컨텍스트의 ASID/VMID). 드라이버가 DMA 버퍼를 unmap 하면 SMMU TLB 무효화 명령을 커맨드 큐에 넣고 **동기화 명령으로 완료를 확인한 뒤** 메모리를 해제해야 한다 — Part B 버그 1 의 장치 버전이고, 장치는 계속 돌고 있으니 결과는 해제된 메모리에 대한 DMA 쓰기다. PCIe ATS 를 쓰면 장치 자체도 변환을 캐시하므로(ATC) 무효화가 장치까지 내려가야 한다. 검증 관점: "unmap → 무효화 → sync → 해제" 순서를 어기는 시나리오를 일부러 만들고, 해제된 영역에 대한 접근이 폴트로 보고되는지(스트림 폴트 이벤트) 확인한다. 명령 이름과 세부는 SMMUv3 스펙(SOCX-M12, PCIE-M15)에서 확인할 것.

꼬리질문: "IOMMU 무효화가 비싸서 성능이 나쁘면?" → 해제를 모아서 한 번에 무효화(배치/지연 해제, Linux 의 flush queue 류)하되, 그동안 그 IOVA 를 재사용하지 않는다 — 보안(장치가 잠시 옛 페이지에 접근 가능)과 성능의 절충이다.
</details>

## 9. 추가 연습

1. **TLB 도달 범위.** 8 MB 를 4 KB 페이지와 2 MB 블록으로 각각 매핑하고 50000 번 랜덤 읽기 (TLB 64 LRU). 미스율, walk_reads, 테이블 페이지 수를 비교하고 4 KB 미스율을 식으로 예측하라.
   <details><summary>답</summary>

   reach = 엔트리 수 × 엔트리 크기: 64 × 4 KB = 256 KB vs 64 × 2 MB = 128 MB. 4 KB: 2048 페이지 중 64 개만 TLB 에 있으니 균일 랜덤이면 히트율 ≈ 64/2048 = 3.1% → 예측 미스율 96.9%, 실측 **96.7%** (48371 미스, walk_reads 145113 = ×3, 테이블 5 개 = L2 1 + L3 4). 2 MB: 블록 4 개 → 미스 **4**, walk_reads 8, 테이블 1 개. 대가: 2 MB 단위로만 매핑·권한·스왑 가능(내부 단편화), 물리적으로 연속된 2 MB 를 찾아야 함, 쪼갤 때 break-before-make. 이 맥은 16 KB granule 이라 같은 구조에서 L2 블록이 32 MB 가 된다(16 KB granule 은 단계당 11 비트).
   </details>
2. **페이지 워크 캐시(PWC).** 4-level 에서 상위 단계(L0–L2) **테이블 디스크립터** 를 16 엔트리 LRU 로 캐시하는 `walk_pwc` 를 짜라. 태그는 `(ttbr, level, va >> lvl_shift(level))`. 결과가 `walk` 와 같아야 하고, 미스당 읽기 수를 비교하라.
   <details><summary>답</summary>

   단계 L 의 디스크립터는 `va >> lvl_shift(L)` 로 정해진다(그 아래 비트는 더 아래 단계의 인덱스와 오프셋). 가장 깊은 단계의 PWC 히트부터 워크를 이어 간다. 실측(8 MB 영역 + 먼 영역 하나, 48009 미스): 읽기/미스 **4.000 → 1.000**, PWC 히트 48007, 불일치 0. 필요한 상위 디스크립터가 9 개뿐이라 16 엔트리에 다 들어간다. 실제 코어도 이런 구조(Intel 의 paging-structure caches, ARM 코어의 walk cache — 세부는 비공개가 많음)를 갖고, 그래서 **TLB 무효화는 워크 캐시도 무효화** 해야 한다: 테이블 페이지를 해제할 때 마지막 단계만 지우는 `TLBI VALE1` 이 아니라 `VAE1` 을 쓰는 이유.
   </details>
3. **break-before-make.** 4 KB 페이지 512 개(2 MB)를 만들고 10 개를 접근해 TLB 에 채운 뒤, (a) 무효화 없이 unmap → L3 테이블 제거 → 같은 VA 에 **다른 PA** 의 2 MB 블록, (b) 사이에 TLBI 를 넣은 경우를 비교하라. 한 VA 에 맞는 엔트리 수의 최댓값과 page 3 의 PA 는?
   <details><summary>답</summary>

   실측: (a) 최대 **2 개** (옛 4 KB + 새 2 MB), page 3 → 0x40003000 (옛 PA — 모델의 조회가 앞 인덱스의 4 KB 엔트리를 먼저 맞춤), (b) 최대 1 개, 0x60003000. 실제 하드웨어에서 (a) 는 TLB conflict abort 이거나 어느 쪽이 쓰일지 보장되지 않는다. (b) 에서 VA 별 TLBI 512 번 대신 `TLBI ASIDE1` 한 번이 싸다 — 범위가 크면 전체/ASID 무효화로 바꾸는 임계값을 두는 것이 리눅스의 흔한 최적화다.
   </details>
4. **롤오버 공식과 "257 번째".** RR 스케줄, P > 2^b − 1 일 때 S 스위치의 롤오버 수 `floor((S−1)/(2^b−1))` 를 유도하고 b = 2..8, P ∈ {cap, cap+1, 2cap, 3cap+2}, S ∈ {1000, 12345} 에서 시뮬레이터와 비교하라. 8 비트에서 첫 롤오버는 몇 번째 서로 다른 프로세스인가?
   <details><summary>답</summary>

   스위치 1..cap 은 세대 1 에서 할당. cap+1 번째에서 첫 롤오버. P > cap 이면 어떤 프로세스든 다시 돌아오기 전에 cap 개 이상 할당이 일어나 세대가 바뀌어 있으므로 **매 스위치가 할당** → 롤오버는 스위치 1 + cap·m 에서. 개수 = floor((S−1)/cap). P ≤ cap 이면 0. 56/56 일치. 8 비트(ASID 0 예약, 255 개): 첫 롤오버는 **256 번째** 프로세스.
   </details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr      # solution.c 를 포함하므로 같은 폴더에서
```

실제 출력:

```text
== 연습 1: TLB reach — 8 MB 랜덤 접근, TLB 64 LRU, 3-level, 50000 접근 ==
  4KB pages : reach = 64 x 4 KB = 256 KB | misses=48371 (96.7%) walk_reads=145113 tables=5 frames
  2MB blocks: reach = 64 x 2 MB = 128 MB | misses=4 (0.0%) walk_reads=8 tables=1 frames
== 연습 2: 페이지 워크 캐시 — 4-level, 8 MB 4KB 랜덤, TLB 64 + PWC 16 ==
  TLB misses=48009  reads/miss: plain 4.000, with PWC 1.000 (PWC hits=48007), mismatches=0
== 연습 3: 4KB 페이지 512 개 → 2MB 블록 재매핑 (TLB 64) ==
  no TLBI (make only): max entries matching one VA = 2, page 3 → pa 0x40003000 (want 0x60003000)
  with TLBI (BBM)   : max entries matching one VA = 1, page 3 → pa 0x60003000 (want 0x60003000)
== 연습 4: RR 롤오버 공식 floor((S-1)/(2^b-1)) vs 시뮬레이터 ==
  56/56 (b = 2..8, P in {cap, cap+1, 2cap, 3cap+2}, S in {1000, 12345}) match
  8-bit ASID (ASID 0 reserved → 255 usable): first rollover at distinct process #256
12/12 passed
```

## 10. 핵심 정리

- TLB = 변환 **과 권한** 의 캐시. 엔트리는 리프 크기 단위(4 KB / 2 MB / 1 GB)로 덮는다 → huge page 가 reach 를 키운다 (512 미스 → 1).
- 미스 비용 = 리프까지의 단계 수만큼 디스크립터 읽기 (4-level 4, 블록이면 적게, 가상화 최악 24). 워크 캐시가 이걸 거의 1 로 줄인다.
- 불변식: **TLB 히트 == 새 워크**. 테이블을 바꾸고 무효화를 안 하면 깨진다 — unmap 후 해제된 프레임에 쓰기, ASID 재사용 후 남의 페이지 읽기.
- 순서: PTE store → `dsb ishst` → `tlbi …is` → `dsb ish` → `isb` → 프레임 해제. x86/RISC-V 는 IPI shootdown.
- 무효화 범위: all ⊃ non-global ⊃ ASID. global 은 ASID 무효화에서 살아남고, VA 무효화에선 ASID 와 무관하게 지워진다.
- ASID 는 작업 집합 합이 TLB 에 들어가고 스위치가 잦을 때 결정적(65536 → 40 미스). 안 들어가면 이득 1.2× 까지 줄고, 진 경우(3081 vs 3080)도 있다.
- ASID 할당기 = 비트맵 + 세대. P ≤ 2^b − 1 이면 롤오버 0, 넘으면 RR 최악 floor((S−1)/(2^b−1)) 번 전체 플러시. 8 비트(0 예약)는 256 번째 프로세스에서 첫 롤오버.

## 참고자료

- Arm, *Arm Architecture Reference Manual for A-profile architecture* (DDI 0487) — VMSAv8-64 변환(granule, 블록, nG 비트, ASID 와 TTBR/TCR_EL1.A1·AS), TLB 유지보수 명령 TLBI VMALLE1/ASIDE1/VAE1/VALE1 과 Inner Shareable 방송, break-before-make 요구 (장·절 번호는 판마다 다름 — 확인 필요).
- Linux 커널 `arch/arm64/mm/context.c` — ASID 할당기(세대, 비트맵, CPU 별 예약 ASID, 지연 플러시), `arch/arm64/include/asm/tlbflush.h` — `dsb ishst; tlbi …; dsb ish` 시퀀스 (버전마다 세부 다름).
- Intel, *Intel 64 and IA-32 Architectures Software Developer's Manual* Vol. 3A, "Paging" 장의 TLB·PCID·INVPCID·global 페이지 절, 그리고 "paging-structure caches" (절 번호 확인 필요).
- RISC-V Privileged Specification — `satp` 의 ASID 필드, `SFENCE.VMA rs1, rs2` (RISCV-A01, RISCV-A02).
- T. W. Barr, A. L. Cox, S. Rixner, "Translation Caching: Skip, Don't Walk (the Page Table)", ISCA 2010 — 페이지 워크 캐시 설계 비교.
- A. Bhattacharjee, D. Lustig, *Architectural and Operating System Support for Virtual Memory*, Morgan & Claypool, 2017 — TLB 구조, 2D 워크, shootdown.
- R. & A. Arpaci-Dusseau, *Operating Systems: Three Easy Pieces*, 19 장 TLB (ASID, 교체 정책): <https://pages.cs.wisc.edu/~remzi/OSTEP/>
- J. L. Hennessy, D. A. Patterson, *Computer Architecture: A Quantitative Approach*, 6th ed., Appendix B / 2 장 가상 메모리와 TLB.
- 이 노트의 관련 레슨: ARCH-A01 (페이지 테이블 워커 — 이 레슨의 0 절), ARCH-A03 (이 맥의 실제 TLB·페이지 폴트 비용), OS-B10 (선형 페이지 테이블), OS-M02 (프로세스 vs 스레드 스위치 실측 — ASID 덕분에 차이 작음), ARM-M05 (DSB/ISB 와 TLBI 시퀀스), ARCH-M05 (교체 정책), OS-M01 · RISCV-A01 · RISCV-A02 · VIRT-B05 · SOCX-M12 · PCIE-M15 · LLAT-A05 (다음 단계).
