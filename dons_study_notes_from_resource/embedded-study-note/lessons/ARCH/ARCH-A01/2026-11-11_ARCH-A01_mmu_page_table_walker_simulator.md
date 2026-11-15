---
id: ARCH-A01
time: 110분
updated: 2026-11-11
---
# ARCH-A01 MMU 페이지 테이블 워커 시뮬레이터

## 학습 목표

- 4 KB 페이지 · 레벨당 9 비트 · 2/3/4 단계(VA 30/39/48 비트) 페이지 테이블에서 VA 를 인덱스 4 개 + offset 으로 손으로 나누고, **TLB 미스 한 번에 디스크립터를 몇 번 읽는지**(페이지 4 · 2 MB 블록 3 · 1 GB 블록 2, 4 단계 기준)를 즉답한다.
- 워커(`mmu_translate`)를 짜서 폴트 4 종 — ADDRESS_SIZE / TRANSLATION / PERMISSION / 성공 — 을 **감지한 레벨**과 함께 보고하고, 권한 규칙(R/W/X × user/kernel, PXN·PAN 개념)을 96 경우 전수로 검증한다.
- 매핑 API(`map` = 가장 큰 정렬 블록 자동 선택, `unmap` = 부분 unmap 이면 **블록 split**, 빈 테이블 반납)를 만들고, 그림자 모델과 대조하는 랜덤 스트레스(3 설정 × 1500 연산 × 36000 프로브)로 불일치 0 을 확인한다.
- 커널 RX / 데이터 RW-XN / 유저 text·heap·stack+가드 / MMIO / 부팅 identity map 레이아웃을 직접 만들고, 23 개 접근의 결과(폴트 종류·레벨)를 예측한 뒤 시뮬레이터로 맞춰 본다. 1 GB 를 4 KB 로 매핑하면 테이블 515 장(2 MB), 2 MB 블록이면 3 장(12 KB)이라는 것을 숫자로 말한다.

## 1. 문제

"TLB 미스 하나에 메모리 접근이 몇 번이죠? 2 MB 페이지면요? 그 폴트는 몇 단계에서 났나요?" — Cortex-A / RISC-V 부트 코드에서 MMU 를 켜는 펌웨어 엔지니어, 커널의 `set_memory_ro` 와 huge page 를 다루는 시스템 엔지니어, 그리고 SMMU/IOMMU·NPU 의 주소 변환 유닛을 검증하는 실리콘 엔지니어가 모두 받는 질문이다. OS-B10 에서 **선형(1 단계) 페이지 테이블**의 `translate()` 를 짰다. 이 레슨은 그것을 **다단계 트리 + 블록 매핑 + 매핑 API** 로 키운다 — 실제 칩의 하드웨어 워커가 하는 일과 커널의 페이지 테이블 코드가 하는 일을 한 번에 C 로 만든다.

Don 의 SSD 경험과 연결: FTL 의 L2P 맵이 DRAM 에 다 안 들어갈 때 쓰는 **다단계 매핑 테이블 + 캐시**(DFTL 류의 "매핑 페이지의 매핑")가 구조적으로 이것과 같다. 매핑 테이블 자체가 저장 공간을 먹고(Part C 의 515 장), 그 테이블을 찾아가는 데 읽기가 더 든다(walk_reads). 차이는 하드웨어가 걸어 준다는 것뿐이다.

모델은 **"ARMv8-A VMSA 4KB granule 을 단순화한 교육용 포맷"** 이다. 레벨 구조·블록 크기·"L3 에서 블록 인코딩은 무효" 규칙은 ARMv8 을 따르지만, 디스크립터의 **비트 위치는 우리 것**이다(실제 ARM 의 AP[2:1]·UXN·PXN·AttrIndx 위치가 아님). ARCH-A02 가 이 포맷을 그대로 축소해 TLB 를 얹는다.

```svg
<svg viewBox="0 0 740 350" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="a01a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">4 단계 walk — VA 0x1234_5678_9ABC (48 bit), 실제 실행 결과의 인덱스</text>
  <g stroke="currentColor">
    <rect x="20" y="36" width="60" height="34" class="f-mut"/>
    <rect x="80" y="36" width="130" height="34" class="f-acc"/>
    <rect x="210" y="36" width="130" height="34" class="f-acc"/>
    <rect x="340" y="36" width="130" height="34" class="f-acc"/>
    <rect x="470" y="36" width="130" height="34" class="f-acc"/>
    <rect x="600" y="36" width="130" height="34" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="26" y="58" font-size="12">63..48=0</text>
    <text x="88" y="52" font-size="12">L0 idx [47:39]</text><text x="88" y="66" font-size="12">= 36</text>
    <text x="218" y="52" font-size="12">L1 idx [38:30]</text><text x="218" y="66" font-size="12">= 209</text>
    <text x="348" y="52" font-size="12">L2 idx [29:21]</text><text x="348" y="66" font-size="12">= 179</text>
    <text x="478" y="52" font-size="12">L3 idx [20:12]</text><text x="478" y="66" font-size="12">= 393</text>
    <text x="608" y="52" font-size="12">offset [11:0]</text><text x="608" y="66" font-size="12">= 0xABC</text>
  </g>
  <g stroke="currentColor">
    <rect x="20" y="120" width="80" height="40" class="f-warn"/>
    <rect x="130" y="110" width="120" height="150" class="f-mut"/>
    <rect x="270" y="110" width="120" height="150" class="f-mut"/>
    <rect x="410" y="110" width="120" height="150" class="f-mut"/>
    <rect x="550" y="110" width="120" height="150" class="f-mut"/>
    <rect x="130" y="160" width="120" height="18" class="f-acc"/>
    <rect x="270" y="200" width="120" height="18" class="f-acc"/>
    <rect x="410" y="180" width="120" height="18" class="f-acc"/>
    <rect x="550" y="220" width="120" height="18" class="f-ok"/>
    <rect x="690" y="215" width="44" height="28" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="32" y="145">TTBR</text>
    <text x="140" y="128">L0 테이블</text><text x="140" y="173">[36] table</text>
    <text x="280" y="128">L1 테이블</text><text x="280" y="213">[209] table</text>
    <text x="420" y="128">L2 테이블</text><text x="420" y="193">[179] table</text>
    <text x="560" y="128">L3 테이블</text><text x="560" y="233">[393] page</text>
    <text x="694" y="233">PA</text>
    <text x="140" y="250">512 x 8 B</text><text x="280" y="250">= 4 KB</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5" fill="none" marker-end="url(#a01a)">
    <path d="M100 140 L128 140"/>
    <path d="M250 169 L268 120"/>
    <path d="M390 209 L408 120"/>
    <path d="M530 189 L548 120"/>
    <path d="M670 229 L688 229"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="286">디스크립터 읽기 = 레벨당 1 번 → 페이지 4 번 (walk_reads=4).</text>
    <text x="20" y="306">2 MB 블록은 L2 에서 멈춰 3 번, 1 GB 블록은 L1 에서 2 번.</text>
    <text x="20" y="326">3 단계(39 bit)는 L1 부터 3/2/1 번, 2 단계(30 bit)는 L2 부터 2/1 번.</text>
    <text x="20" y="346">마지막 레벨은 항상 L3 이다.</text>
  </g>
</svg>
```

**디스크립터 포맷 (이 레슨 고정, A02 와 공유):**

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">64-bit 디스크립터 (교육용 — 실제 ARM 비트 위치 아님)</text>
  <g stroke="currentColor">
    <rect x="10" y="32" width="110" height="36" class="f-mut"/>
    <rect x="120" y="32" width="300" height="36" class="f-acc"/>
    <rect x="420" y="32" width="60" height="36" class="f-mut"/>
    <rect x="480" y="32" width="30" height="36" class="f-ok"/><rect x="510" y="32" width="30" height="36" class="f-ok"/>
    <rect x="540" y="32" width="30" height="36" class="f-warn"/><rect x="570" y="32" width="30" height="36" class="f-warn"/>
    <rect x="600" y="32" width="30" height="36" class="f-warn"/><rect x="630" y="32" width="30" height="36" class="f-warn"/>
    <rect x="660" y="32" width="30" height="36" class="f-acc"/><rect x="690" y="32" width="30" height="36" class="f-acc"/>
  </g>
  <g fill="currentColor">
    <text x="18" y="55">63..48 예약 0</text><text x="190" y="55">47..12 출력 주소 OA</text><text x="425" y="55">11..8</text>
    <text x="490" y="55">G</text><text x="520" y="55">A</text><text x="550" y="55">U</text><text x="580" y="55">X</text>
    <text x="610" y="55">W</text><text x="640" y="55">R</text><text x="670" y="55">T</text><text x="700" y="55">V</text>
    <text x="486" y="84">7</text><text x="516" y="84">6</text><text x="546" y="84">5</text><text x="576" y="84">4</text>
    <text x="606" y="84">3</text><text x="636" y="84">2</text><text x="666" y="84">1</text><text x="696" y="84">0</text>
    <text x="10" y="110">V=0 → 무효 (TRANSLATION).  V=1, T=1 → L0–L2: 다음 테이블 / L3: 페이지.</text>
    <text x="10" y="130">V=1, T=0 → L1·L2: 블록 (1 GB · 2 MB) / L0·L3: 잘못된 인코딩 (TRANSLATION).</text>
    <text x="10" y="150">R W X U = leaf 권한. 테이블 디스크립터의 권한 비트는 무시 (APTable 없음).</text>
    <text x="10" y="170">A = access flag (map 이 1 로, 워커는 검사 안 함).  G = global (A02 TLB 용, 저장만).</text>
    <text x="10" y="190">블록 OA 는 블록 크기로 정렬 (map 이 보장, 워커는 하위 비트 무시). 페이지 OA = 4 KB 프레임.</text>
    <text x="10" y="210">실제 ARMv8 도 bit0 = valid, bit1 = table/page vs block 은 같다.</text>
    <text x="10" y="230">권한은 AP[2:1]·UXN·PXN, 메모리 속성은 AttrIndx(MAIR) — 이 레슨에선 생략.</text>
  </g>
</svg>
```

### Part A — 워커와 매핑 설치 (4 단계 · 3 단계)

- `va_index(m, va, lv)`: 레벨 lv 의 9 비트 인덱스. 레벨 번호가 절대값이라 2 단계의 루트(L2)도 VA[29:21].
- `perm_ok(d, acc)`: leaf 권한 규칙 (아래 Part B 표). 16 조합 × 3 종류 × 2 권한 = 96 경우 전수.
- `mmu_translate(m, va, acc)` → `xlate_t {fault, level, va, acc, pa, size, perms, walk_reads}`. 디스크립터는 반드시 `rd_desc()` 로 읽는다 — 하네스가 `walk_reads` 가 실제 읽은 횟수와 같은지 매번 확인한다(정직성 검사).
- `map_level(...)`: `mmu_map(va, pa, size, perms, flags)` 의 몸통. 가장 큰 정렬 블록을 자동 선택(2 MB / 1 GB), `MAP_PAGES` 면 4 KB 만. [1 MB, 7 MB) 를 매핑하면 "페이지 256 + 2 MB 블록 2 + 페이지 256" 이 나와야 한다.

### Part B — 블록의 경계 사례, 폴트 레벨, split

- 폴트 4 종이 **맞는 레벨**에서: ADDRESS_SIZE(시작 레벨, 읽기 0), TRANSLATION(무효 엔트리를 읽은 레벨), PERMISSION(leaf 레벨). 4 단계에서 VA 를 한 비트씩 바꿔 L0/L1/L2/L3 에서 각각 TRANSLATION 이 나는 "사다리" 를 확인.
- 블록 규칙: `mmu_map_block` 이 미정렬 va/pa → EALIGN, L0·L3 블록 → ELEVEL. 자동 선택에서 pa 가 정렬 안 되면 조용히 페이지로 내려간다. **블록 vs 테이블 충돌**: 범위 안에 무엇이든 매핑돼 있으면 EEXIST 이고 아무것도 안 바뀐다(테이블 수 그대로). W-only → EPERM. 테이블이 모자라면 ENOMEM + **롤백**.
- `split_block` + `unmap_level`: 블록 일부를 unmap 하면 split(1 GB 블록에서 4 KB 하나 = split 2 번), 하위 테이블이 비면 반납. **정책: 부분 unmap 은 split 으로 지원, 매핑 위에 다시 매핑은 EEXIST 로 거부**(remap = unmap 후 map). ARCH-A02 에 축소해 넣은 워커는 더 단순한 정책 — 블록의 부분 unmap 을 오류(E_SPLIT)로 거부 — 을 쓴다. split 은 이 레슨의 몫이다.
- 2 단계 설정: 루트가 L2 → 블록은 2 MB 뿐, `map_block(L1)` → ELEVEL.
- 잘못된 인코딩(손으로 쓴 L0 블록, L3 의 T=0) → TRANSLATION, 그리고 트리 감사(`pt_audit`)가 정확히 2 건을 찾는다.
- 랜덤 스트레스: map/unmap 을 섞고 매 연산 뒤 프로브 24 개를 **4 KB 단위 그림자 배열**과 대조 (pa · 권한 · 폴트 종류 · `walk_reads == leaf 레벨 − 시작 레벨 + 1`).

### Part C — 펌웨어/OS 레이아웃

- 3 단계(VA 39 bit, arm64 Linux 의 4K/39-bit 구성과 같은 단수)에 10 개 영역: 유저 text(R-X) / heap(RW-) / stack(RW-, 아래 가드 페이지 비움), 부팅 identity map, 커널 .text(RX, 2 MB 블록) / .rodata(R) / .data(RW, XN), DRAM linear map(1 GB 블록), MMIO UART·GIC. 물리 맵은 "QEMU `virt` 보드 비슷한 가정"(RAM 0x4000_0000, UART 0x0900_0000, GIC 0x0800_0000).
- 접근 23 개의 기대 결과 표 → CHECK. 부팅 끝에 identity map 을 지우면 폴트 레벨이 L2 → **L1** 로 바뀐다(빈 테이블 반납 때문).
- MAIR / 메모리 속성(Device-nGnRE vs Normal cacheable)은 **범위 밖**: 이 포맷엔 속성 인덱스가 없다. MMIO 를 Normal 로 매핑하면 실제 칩에선 투기적 읽기·병합이 생긴다는 것만 기억 (ARM-A03 다음 단계).
- 1 GB 를 4 KB 페이지 / 2 MB 블록 / 1 GB 블록으로 매핑할 때의 테이블 장수와 walk_reads 표.

## 2. Problem (English)

> **Multi-level page-table walker.** Implement a simplified ARMv8-style translation scheme with a 4 KB granule: 512 eight-byte descriptors per table, 9 index bits per level, configurable `levels ∈ {2,3,4}` (VA width 30/39/48). The last level is always L3. Descriptor: bit0 V, bit1 T (table / page), bit2 R, bit3 W, bit4 X, bit5 U, bit6 A, bit7 G, bits[47:12] output address. Blocks are allowed at L1 (1 GB) and L2 (2 MB) only.
>
> ```c
> xlate_t mmu_translate(mmu_t *m, uint64_t va, access_t acc);   /* acc = {READ|WRITE|EXEC, user?} */
> int mmu_map(mmu_t *m, uint64_t va, uint64_t pa, uint64_t size, uint64_t perms, unsigned flags);
> int mmu_unmap(mmu_t *m, uint64_t va, uint64_t size);
> ```
>
> A. Walk the tables from TTBR, reading one descriptor per level from simulated physical memory, and return either `{pa, leaf level, size, perms}` or a fault `{type, level, va, access}` with type ∈ {ADDRESS_SIZE, TRANSLATION, PERMISSION}. Count descriptor reads. `mmu_map` must pick the largest aligned block that fits unless `MAP_PAGES` is given.
>
> B. Handle the edges: VA above the configured width; invalid encodings (block at L0, T=0 at L3); misaligned block requests; mapping over an existing mapping (reject atomically); partial unmap of a block (split it); freeing tables that become empty; running out of table memory (roll back). Prove it with a randomized test against a flat shadow map.
>
> C. Build a kernel + user + MMIO + identity-map layout and tabulate what each access does. How much page-table memory does mapping 1 GB cost with 4 KB pages vs 2 MB vs 1 GB blocks?
>
> Example: 4 levels, `map(0x1234_5678_9000, 0x8_0000_5000, 4K, R|W|U)`; `translate(0x1234_5678_9ABC, user read)` → `pa = 0x8_0000_5ABC, L3, 4K, reads = 4`; user exec → `PERMISSION @ L3`; `translate(1<<48)` → `ADDRESS_SIZE`, 0 reads.
>
> Follow-ups: How many memory accesses on a TLB miss with 4 levels — and under virtualization with a 4-level stage 2? Why must the block be aligned? What does splitting a live block require on real ARM hardware? Why do kernels map the linear map with blocks, and why do some configurations refuse to?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o st && ./st
```

TODO 는 6 개: `va_index`, `perm_ok`, `mmu_translate`, `map_level` (Part A), `split_block`, `unmap_level` (Part B). 시뮬레이션 물리 메모리·프레임 할당기·`mmu_map/unmap/map_block` 의 인자 검사와 겹침 검사(`range_free`)·롤백·트리 감사(`pt_audit`)·출력은 제공된다. 처음 실행 (실제 출력 일부, exit 1):

```text
== Part A: 워커 + 매핑 API (4 단계 / 3 단계) ==
  VA 0x123456789abc -> L0 0  L1 0  L2 0  L3 0  offset 0xabc
FAIL starter.c:399  badidx == 0
FAIL starter.c:401  va_index(&m4, VA1, 0) == 36 && va_index(&m4, VA1, 1) == 209 && va_index(&m4, VA1, 2) == 179 && va_index(&m4, VA1, 3) == 393
FAIL starter.c:406  m4.tables == 4
  walk 4-level va=0x123456789abc U:R
    => ok  pa=0x0 L0 ? ----- reads=0
...
  stress 3-level: ops=1500 maps=652 eexist=0 unmaps=534 splits=0 peak_tables=1 probes=36000 hits L1/L2/L3=0/0/0 perm=11903 trans=17269 mismatches=36342 audit_bad=0 final_tables=1
...
75/161 passed
```

스텁 워커는 아무것도 안 읽고 "성공, pa=0" 을 돌려준다. 통과하는 75 개는 "빈 트리면 테이블이 1 장" 처럼 스텁이 우연히 맞히는 것들이다 — 스트레스의 `mismatches=36342` 가 진짜 상태를 보여 준다.

## 4. 힌트

<details><summary>힌트 1 — 인덱스와 크기는 레벨 하나로 정해진다</summary>

`level_shift(lv) = 12 + 9·(3 − lv)` → L3 12, L2 21, L1 30, L0 39. 인덱스 = `(va >> shift) & 511`, 엔트리 하나가 덮는 크기 = `1 << shift` (L3 4 KB, L2 2 MB, L1 1 GB, L0 512 GB). 설정(levels)이 바꾸는 것은 **시작 레벨**(4 − levels)과 VA 폭뿐이다.
</details>

<details><summary>힌트 2 — 워커 루프의 다섯 갈래</summary>

읽고 나서 순서대로: ① V=0 → TRANSLATION. ② L0–L2 이고 T=1 → 다음 테이블로. ③ L0 인데 T=0, 또는 L3 인데 T=0 → TRANSLATION(잘못된 인코딩). ④ 남은 것은 leaf(L1/L2 블록, L3 페이지) → 권한 검사 → PERMISSION 또는 성공. `walk_reads++` 와 `level = lv` 는 **읽자마자** — 그래야 폴트 레벨과 읽기 횟수가 정확하다. ADDRESS_SIZE 는 루프 전에, 읽기 0 번.
</details>

<details><summary>힌트 3 — pa 계산</summary>

leaf 크기 `sz` 에 대해 `pa = (OA & ~(sz−1)) | (va & (sz−1))`. 블록이면 VA 의 하위 21/30 비트가 그대로 offset 이 된다 — 블록 OA 가 정렬돼야 하는 이유가 바로 이것(OR 로 붙이니까 OA 의 하위 비트가 있으면 섞인다).
</details>

<details><summary>힌트 4 — map_level 은 "청크" 단위 재귀</summary>

`chunk = min(size, 이 엔트리의 끝 − va)`. 블록을 놓을 조건은 네 개: 블록 허용 레벨, `MAP_PAGES` 아님, chunk 가 엔트리 전체(= va 정렬 + 크기 충분), **pa 도** 같은 크기로 정렬. 하나라도 아니면 하위 테이블(없으면 할당)로 같은 청크를 넘긴다. L3 에서는 무조건 페이지(T=1).
</details>

<details><summary>힌트 5 — unmap 은 map 의 거울 + split + 반납</summary>

같은 청크 순회. leaf 를 통째로 덮으면 0 을 쓴다. 일부만 덮으면 `split_block` 으로 같은 매핑을 한 레벨 아래 512 개로 펼친 다음 테이블처럼 내려간다. 내려갔다 온 뒤 `table_empty(child)` 면 엔트리를 0 으로, `free_table(child)`. split 은 하위 512 엔트리에 **같은 속성(LEAF_ATTR)** 과 `base + i·child_size` 를 쓰고, 하위가 L3 이면 T=1 을 잊지 말 것 (안 그러면 잘못된 인코딩이다).
</details>

## 5. 풀이

### Part A — 워커

```c
if (va >> m->va_bits) { r.fault = F_ADDRESS_SIZE; return r; }
uint64_t table = m->ttbr;
for (int lv = start_level(m); lv <= 3; lv++) {
    uint64_t d = rd_desc(m, table + 8u * va_index(m, va, lv));
    r.walk_reads++;  r.level = lv;
    if (!(d & D_V)) { r.fault = F_TRANSLATION; return r; }
    if (lv < 3 && (d & D_T)) { table = d & OA_MASK; continue; }
    if (lv == 0 || (lv == 3 && !(d & D_T))) { r.fault = F_TRANSLATION; return r; }
    uint64_t sz = level_size(lv);
    r.size = sz;  r.perms = d & PERM_MASK;
    if (!perm_ok(d, acc)) { r.fault = F_PERMISSION; return r; }
    r.pa = (d & OA_MASK & ~(sz - 1)) | (va & (sz - 1));
    return r;
}
```

하드웨어 워커도 이 루프다. 차이는 (1) 각 `rd_desc` 가 진짜 메모리 읽기(캐시에 있을 수도 있다)라는 것, (2) 상위 레벨 결과를 **page-walk cache**(ARM 문서의 intermediate table walk cache 류)에 저장해 다음 walk 를 L3 부터 시작하기도 한다는 것 — 그래서 "TLB 미스 = 4 번 읽기" 는 최악값이다.

**실제 walk 트레이스** (solution 출력, 4 단계 페이지와 1 GB 블록):

```text
  walk 4-level va=0x123456789abc U:R
    L0  table 0x48000000 [ 36] = 0x0000000048001003  table -> 0x48001000
    L1  table 0x48001000 [209] = 0x0000000048002003  table -> 0x48002000
    L2  table 0x48002000 [179] = 0x0000000048003003  table -> 0x48003000
    L3  table 0x48003000 [393] = 0x000000080000506f  page rw-u- oa=0x800005000
    => ok  pa=0x800005abc L3 4K rw-u- reads=4
  walk 4-level va=0x8012345678 K:X
    L0  table 0x48000000 [  1] = 0x0000000048004003  table -> 0x48004000
    L1  table 0x48004000 [  0] = 0x0000000100000055  block r-x-- oa=0x100000000
    => ok  pa=0x112345678 L1 1G r-x-- reads=2
```

디스크립터를 비트로 읽어 보기: `0x...03` = V|T (테이블), `0x...6f` = V|T|R|W|U|A (0x01+0x02+0x04+0x08+0x20+0x40 = 0x6F, L3 페이지), `0x...55` = V|R|X|A (T=0 → L1 블록). 테이블 풀이 PA 0x4800_0000 부터 프레임 순서대로 나가는 것도 보인다(프레임 할당기 = 스택, 0 번 프레임이 루트).

**자동 블록 선택** — 3 단계에 [0x10_0000, 0x70_0000) 6 MB, pa = 0x4010_0000:

```text
  3-level map [0x100000, 0x700000): tables=4  2M blocks=2  4K pages=512
  walk 3-level va=0x1ffff8 K:R
    L1  table 0x48000000 [  0] = 0x0000000048001003  table -> 0x48001000
    L2  table 0x48001000 [  0] = 0x0000000048002003  table -> 0x48002000
    L3  table 0x48002000 [511] = 0x00000000401ff04f  page rw--- oa=0x401ff000
    => ok  pa=0x401ffff8 L3 4K rw--- reads=3
  walk 3-level va=0x200008 K:R
    L1  table 0x48000000 [  0] = 0x0000000048001003  table -> 0x48001000
    L2  table 0x48001000 [  1] = 0x000000004020004d  block rw--- oa=0x40200000
    => ok  pa=0x40200008 L2 2M rw--- reads=2
```

| VA 구간 | L2 엔트리 | 결과 | 이유 |
|---|---|---|---|
| [0x10_0000, 0x20_0000) | [0] | L3 테이블, 페이지 256 | 청크가 2 MB 엔트리의 일부 |
| [0x20_0000, 0x60_0000) | [1], [2] | 2 MB 블록 2 개 | va·pa 모두 2 MB 정렬, 청크 = 엔트리 전체 |
| [0x60_0000, 0x70_0000) | [3] | L3 테이블, 페이지 256 | 일부 |

1536 페이지 전부를 번역해 pa 와 leaf 레벨(가운데 4 MB 는 L2 / 읽기 2, 양끝은 L3 / 읽기 3)을 CHECK 한다. 같은 3 단계 트리에 1 GB 를 VA 0x40_0000_0000 에 매핑하면 **루트(L1) 엔트리 자체가 블록** → 읽기 1 번, 테이블 추가 0 장.

**권한 표** (`perm_ok`, 실제 출력 — W-only 4 행은 map 이 거부하므로 생략):

```text
  perm_ok (o = allowed)    K:R K:W K:X  U:R U:W U:X
    ----                   .   .   .    .   .   . 
    r---                   o   .   .    .   .   . 
    rw--                   o   o   .    .   .   . 
    --x-                   .   .   o    .   .   . 
    r-x-                   o   .   o    .   .   . 
    rwx-                   o   o   o    .   .   . 
    ---u                   .   .   .    .   .   . 
    r--u                   o   .   .    o   .   . 
    rw-u                   o   o   .    o   o   . 
    --xu                   .   .   .    .   .   o 
    r-xu                   o   .   .    o   .   o 
    rwxu                   o   o   .    o   o   o 
```

세 규칙이 표에 그대로 보인다: (1) user 열은 U=1 행에서만 o (커널 전용 페이지). (2) **K:X 열은 U=1 행에서 전부 .** — 커널이 유저 메모리의 코드를 실행하는 것은 금지(x86 SMEP, ARM PXN 의 개념. 실제 ARMv8 에는 "EL0 가 쓸 수 있는 영역은 EL1 에서 PXN 으로 취급" 하는 규칙도 있다고 알려져 있다 — 이 모델은 더 강하게 U=1 이면 모두 금지). (3) K:R/K:W 는 U 와 무관 — **PAN 이 없는** 모델이다. ARMv8.1 PAN(x86 SMAP)이 켜지면 커널의 U=1 데이터 접근도 권한 폴트가 되고, `copy_from_user` 같은 곳에서만 잠깐 끈다. 오라클(`oracle_allows`)은 같은 규칙을 "필요 비트 · 금지 비트" 로 다르게 써서 96 경우 대조한다.

### Part B — 폴트 레벨, 블록 경계, split

**폴트 레벨 사다리** (4 단계, 페이지 1 장 R|U 만 매핑, 실제 출력):

```text
  4-level (VA 48 bits): va=0x1000000000000 -> ADDRESS_SIZE@L0 reads=0
  3-level (VA 39 bits): va=0x8000000000 -> ADDRESS_SIZE@L1 reads=0
  2-level (VA 30 bits): va=0x40000000 -> ADDRESS_SIZE@L2 reads=0
  다른 L0 엔트리 (VA^bit39)    U:R 0x012b456789000 -> TRANSLATION@L0 reads=1
  같은 L0, 다른 L1 (^bit30)    U:R 0x0123416789000 -> TRANSLATION@L1 reads=2
  같은 L1, 다른 L2 (^bit21)    U:R 0x0123456589000 -> TRANSLATION@L2 reads=3
  같은 L2, 다른 L3 (^bit12)    U:R 0x0123456788000 -> TRANSLATION@L3 reads=4
  매핑된 페이지에 쓰기         U:W 0x0123456789008 -> PERMISSION@L3 reads=4
  매핑된 페이지 읽기           U:R 0x0123456789008 -> ok  pa=0x9000008 L3 4K r--u- reads=4
  VA[48] = 1                   U:R 0x1123456789000 -> ADDRESS_SIZE@L0 reads=0
```

폴트 레벨 = "트리가 어디까지 지어져 있나" 의 지문이다. 커널 폴트 핸들러는 이 정보(ARM 이면 ESR 의 fault status code 가 종류+레벨을 담는다)로 "미할당이라 요구 페이징할 곳인가 / 권한이라 COW 나 SIGSEGV 인가" 를 가른다 (OS-M03, OS-A06 다음 단계). **ADDRESS_SIZE 주의**: 이 모델은 shared rule 대로 "VA 폭 밖" 을 ADDRESS_SIZE 라고 부른다. 실제 ARMv8 은 TxSZ 범위 밖 VA 를 보통 (시작 레벨 쪽) **Translation fault** 로 보고하고, Address size fault 는 테이블 주소나 출력 주소가 설정한 PA 크기(IPS)를 넘을 때 쓴다 — 세부는 ARM ARM 확인 필요. 또 실제 AArch64 는 VA 상위 비트가 전부 1 이면 TTBR1(커널 절반)로 간다. 이 모델은 TTBR 하나라 `0xFFFF8000_00000000` 도 ADDRESS_SIZE 다.

**블록 API 경계와 충돌** — 테스트가 고정하는 정책:

| 상황 | 결과 | 상태 변화 |
|---|---|---|
| `map_block(L2)` pa 또는 va 가 2 MB 미정렬 | EALIGN | 없음 |
| `map_block(L0)` / `map_block(L3)` / 2 단계에서 `map_block(L1)` | ELEVEL | 없음 |
| 자동 `map` 2 MB, va 정렬 / pa 는 +4 KB | OK — 페이지 512 장, L3 테이블 1 장 | 블록 대신 페이지 |
| 4 KB 가 있는 2 MB 구간에 블록 | EEXIST | 테이블 수 그대로 |
| 블록 위에 4 KB, 1 GB 안에 2 MB 블록이 있는데 1 GB 블록 | EEXIST | 없음 |
| perms = W 만 / 권한 아닌 비트(V) | EPERM | 없음 |
| va + size 가 VA 폭 밖, pa + size ≥ 2^48 | ERANGE | 없음 |
| 매핑 안 된 범위 unmap | OK (no-op) | 없음 |
| 프레임 3 장으로 4 단계 페이지 1 장 | ENOMEM | **롤백**: tables=1, free frames=2 |

"전부 아니면 무" 를 쉽게 만든 방법: 겹침은 **설치 전에** `range_free` 로 검사하고, 설치 중 ENOMEM 이 나면 같은 범위를 `unmap` — 원래 비어 있던 범위라 지우면 원상복구, 중간에 할당된 빈 테이블도 unmap 의 반납 규칙이 회수한다. 커널 코드는 보통 테이블을 미리 할당해 두고 실패 경로를 없애는 쪽을 택한다(할당 실패를 다룰 수 없는 맥락이 많아서).

**split** — 1 GB 블록에서 4 KB 한 장을 빼기 (3 단계, 실제 출력):

```text
  1G block - 1 page: splits=2 tables=3
  walk 3-level va=0x40123008 K:R
    L1  table 0x48000000 [  1] = 0x0000000048001003  table -> 0x48001000
    L2  table 0x48001000 [  0] = 0x0000000048002003  table -> 0x48002000
    L3  table 0x48002000 [291] = 0x0000000000000000  invalid
    => TRANSLATION@L3 reads=3
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="a01b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">unmap(0x4012_3000, 4K) on a 1 GB block — split 2 번, 테이블 1 → 3 장</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="170" height="60" class="f-acc"/>
    <rect x="270" y="40" width="200" height="190" class="f-mut"/>
    <rect x="280" y="64" width="180" height="22" class="f-warn"/>
    <rect x="280" y="92" width="180" height="22" class="f-acc"/>
    <rect x="280" y="120" width="180" height="22" class="f-acc"/>
    <rect x="540" y="40" width="190" height="190" class="f-mut"/>
    <rect x="550" y="64" width="170" height="22" class="f-ok"/>
    <rect x="550" y="120" width="170" height="22" class="f-warn"/>
    <rect x="550" y="148" width="170" height="22" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="28" y="60">L1[1] (루트)</text><text x="28" y="78">전: 1 GB 블록 RW</text><text x="28" y="94">후: table → L2</text>
    <text x="278" y="56">L2 테이블 (split ①)</text>
    <text x="286" y="80">[0] table → L3 (split ②)</text>
    <text x="286" y="108">[1] 2 MB 블록 0x8020_0000</text>
    <text x="286" y="136">[2] 2 MB 블록 0x8040_0000</text>
    <text x="286" y="166">… [511] 2 MB 블록</text>
    <text x="286" y="190">한 장 = 같은 매핑을 512 개로</text>
    <text x="286" y="210">권한·A·G 그대로 물려줌</text>
    <text x="548" y="56">L3 테이블</text>
    <text x="556" y="80">[0..290] 페이지</text>
    <text x="556" y="108">…</text>
    <text x="556" y="136">[291] 0 (지운 페이지)</text>
    <text x="556" y="164">[292..511] 페이지</text>
    <text x="548" y="200">0x4020_0000: reads 2 (L2)</text>
    <text x="548" y="218">0x4010_0000: reads 3 (L3)</text>
  </g>
  <g stroke="currentColor" stroke-width="1.5" fill="none" marker-end="url(#a01b)">
    <path d="M190 85 L268 70"/>
    <path d="M460 75 L538 70"/>
  </g>
</svg>
```

split 의 대가는 셋: 테이블 메모리(+4 KB 씩), **walk 가 길어짐**(이 1 GB 안의 모든 접근이 1 → 2 또는 3 번), 그리고 TLB 도달 범위(reach) 감소 — 1 GB 를 TLB 엔트리 1 개로 덮던 것이 수백 개가 필요해진다 (ARCH-A02 에서 측정). 실제 ARMv8 에서 살아 있는 블록을 테이블로 바꿀 때는 **break-before-make**(BBM)가 필요하다: 엔트리를 무효로 쓰고 → TLBI → DSB → 새 테이블 디스크립터. 두 크기의 TLB 엔트리가 동시에 존재하면 TLB conflict abort 나 예측 불가 동작이 생길 수 있어서다(ARMv8.4 의 FEAT_BBM 이 일부 완화 — 세부 확인 필요). 그 사이 다른 코어가 그 영역을 접근하면 폴트가 나므로, 커널은 split 을 피하거나(아래 Part C 의 linear map 이야기) 조심스럽게 한다. 이 시뮬레이터에는 TLB 가 없어서 제자리 교체가 안전하다 — A02 의 "unmap 후 TLB 무효화 누락" 버그가 바로 이 간극이다.

**블록 두 개에 걸친 부분 unmap** [0x11F_F000, 0x120_1000) → 양쪽 2 MB 블록이 각각 split (+2 장), 그리고 정렬된 2 MB 를 1 GB 블록 안에서 지우면 split 은 L1 에서 한 번뿐, L2 엔트리는 통째로 0 (그 VA 는 TRANSLATION@L2). 마지막에 VA 공간 전체를 unmap 하면 루트만 남는다(tables=1, 감사 0).

**2 단계 설정**: 루트가 L2 라 1 GB 매핑 = 루트 엔트리 512 개가 전부 2 MB 블록(`tables=1 2M blocks=512`), 읽기 1 번. 4 KB 하나를 빼면 split 1 번, 그 2 MB 안만 읽기 2 번. 이것이 VA 30 bit 짜리 작은 시스템(예: MMU 가 있는 마이크로컨트롤러급 Cortex-R/A 의 작은 주소 공간)에서 단수를 줄이는 이유다 — walk 가 짧고 테이블이 적다.

**잘못된 인코딩**: 손으로 L0 에 블록(V=1, T=0), L3 에 T=0 을 쓰면 워커는 TRANSLATION@L0 (읽기 1), TRANSLATION@L3 (읽기 4) 를 보고하고, 감사기는 `audit problems=2 (last: L3[3]: L3 entry with T=0)` 를 낸다. T=1 로 고치면 같은 엔트리가 정상 페이지가 된다. ARMv8 이 L3 의 bit1=0 을 무효로 둔 것은 L3 에선 "블록" 이 의미가 없기 때문(페이지가 이미 가장 작은 단위).

**walk_reads 표** (실제 출력):

```text
  walk_reads  levels VAbits  4K-page 2M-block 1G-block empty-root
              4      48     4        3        2        1
              3      39     3        2        1        1
              2      30     2        1        -        1
```

공식: `walk_reads = leaf 레벨 − 시작 레벨 + 1`, 폴트면 감지 레벨까지. 스트레스는 모든 프로브에서 이 식을 CHECK 한다.

**랜덤 스트레스** (고정 시드 xorshift64, 실제 출력):

```text
  stress 2-level: ops=1500 maps=550 eexist=124 unmaps=539 splits=45 peak_tables=513 probes=36000 hits L1/L2/L3=0/1615/2274 perm=6440 trans=25671 mismatches=0 audit_bad=0 final_tables=1
  stress 3-level: ops=1500 maps=542 eexist=110 unmaps=534 splits=28 peak_tables=55 probes=36000 hits L1/L2/L3=215/1229/1997 perm=6169 trans=26390 mismatches=0 audit_bad=0 final_tables=1
  stress 4-level: ops=1500 maps=506 eexist=177 unmaps=517 splits=73 peak_tables=518 probes=36000 hits L1/L2/L3=174/1983/2231 perm=8133 trans=23479 mismatches=0 audit_bad=0 final_tables=1
```

- 창: 2 단계 [0, 1 GB), 3 단계 [0, 2 GB), 4 단계 [511 GB, 513 GB) — 마지막은 **L0 경계를 가로지른다**.
- 연산: 45% map(4 KB × 1–96 / 2 MB × 1–3 / 1 GB; pa 를 1/8 확률로 +4 KB 해 블록 불가로, 1 GB 는 +2 MB 로 2 MB 블록 강제; 1/6 은 `MAP_PAGES`), 32% unmap(4 KB 단위 임의 범위 = 블록 일부 → split), 3% 창 전체 비우기(그래야 1 GB 블록이 들어갈 자리가 생긴다 — 처음 버전엔 이게 없어서 L1 히트가 0 이었고 커버리지 CHECK 가 그걸 잡았다).
- 그림자 = 4 KB 단위 배열 `{pa, perms}`. map 의 기대 결과(OK vs EEXIST)도 그림자로 예측한다.
- 프로브마다: 폴트 종류, pa(바이트 offset 포함), 권한, `size == level_size(level)`, `walk_reads == level − start + 1`. 50 연산마다 `pt_audit`(빈 비루트 테이블 없음 = 반납 규칙, 테이블 수 == 카운터, 미정렬 블록 없음).
- **커버리지도 CHECK**: split > 0, EEXIST > 0, PERMISSION > 0, L2·L3(3·4 단계는 L1 도) 히트 > 0. "불일치 0" 은 그 경로를 실제로 지났을 때만 의미가 있다 (ARCH-A04 와 같은 원칙).

### Part C — 펌웨어/OS 레이아웃

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">VA 39 bit 레이아웃 (축척 아님) — 루트 L1 엔트리 하나 = 1 GB 슬롯</text>
  <g stroke="currentColor">
    <rect x="20" y="34" width="700" height="44" class="f-ok"/>
    <rect x="20" y="84" width="700" height="28" class="f-warn"/>
    <rect x="20" y="118" width="700" height="44" class="f-acc"/>
    <rect x="20" y="168" width="700" height="28" class="f-acc"/>
    <rect x="20" y="202" width="700" height="28" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="28" y="52">L1[0] user: .text 0x40_0000 R-X·U (4K) | heap 0x100_0000 RW-·U (4K) | stack 0x3FFF_0000 RW-·U (4K)</text>
    <text x="28" y="70">가드 0x3FFE_F000 은 비움 → TRANSLATION@L3,  NULL → TRANSLATION@L2 (L2[0] 무효)</text>
    <text x="28" y="102">L1[1] boot identity VA 0x4000_0000 = PA 0x4000_0000, R-X 2M 블록 — 부팅 후 제거 → 폴트 L1</text>
    <text x="28" y="136">L1[256] kernel 0x40_0000_0000: .text R-X·G 2M 블록 | .rodata R·G (4K) | .data/.bss RW·G (4K)</text>
    <text x="28" y="154">→ 같은 PA 0x4000_0000 을 identity · kernel · linear map 세 VA 가 가리킨다 (별칭)</text>
    <text x="28" y="186">L1[384] linear map 0x60_0000_0000 → DRAM 0x4000_0000, RW·G, 1G 블록 (L1 블록 = 읽기 1 번)</text>
    <text x="28" y="220">L1[511] MMIO 0x7F_C000_0000: UART → 0x0900_0000, GIC → 0x0800_0000 (RW·G, 4K)</text>
    <text x="20" y="252">테이블 10 장 = 40 KB: 루트 1 + user (L2 1 + L3 3) + identity L2 1</text>
    <text x="20" y="272">+ kernel (L2 1 + L3 1) + MMIO (L2 1 + L3 1). linear map 은 루트의 블록이라 0 장.</text>
    <text x="20" y="292">물리 맵은 "QEMU virt 비슷한 가정". MMIO 속성(Device)은 생략.</text>
    <text x="20" y="312">arm64 Linux 는 커널을 TTBR1(상위 절반)에 두지만 이 모델은 TTBR 하나다.</text>
  </g>
</svg>
```

**접근 표** (solution 실제 출력, 23 개 모두 기대와 일치 — CHECK):

```text
  page tables: 10 (= 40 KB)  leaves 1G=1 2M=2 4K=497
   1 커널 코드 실행                   K:X 0x4000001234 -> ok  pa=0x40001234 L2 2M r-x-g reads=2
   2 커널 코드에 쓰기 (W^X)           K:W 0x4000001234 -> PERMISSION@L2 reads=2
   3 rodata 에 쓰기                   K:W 0x4000200010 -> PERMISSION@L3 reads=3
   4 data 실행 (XN)                   K:X 0x4000300100 -> PERMISSION@L3 reads=3
   5 data 쓰기                        K:W 0x4000300100 -> ok  pa=0x40300100 L3 4K rw--g reads=3
   6 linear map 읽기                  K:R 0x6012345678 -> ok  pa=0x52345678 L1 1G rw--g reads=1
   7 linear map 실행                  K:X 0x6012345678 -> PERMISSION@L1 reads=1
   8 linear map 으로 커널 text 쓰기   K:W 0x6000001234 -> ok  pa=0x40001234 L1 1G rw--g reads=1
   9 user 가 커널 data 읽기           U:R 0x4000300100 -> PERMISSION@L3 reads=3
  10 user text 실행                   U:X 0x0000400100 -> ok  pa=0x41000100 L3 4K r-xu- reads=3
  11 user text 에 쓰기                U:W 0x0000400100 -> PERMISSION@L3 reads=3
  12 user heap 쓰기                   U:W 0x0001000040 -> ok  pa=0x41100040 L3 4K rw-u- reads=3
  13 user heap 실행                   U:X 0x0001000040 -> PERMISSION@L3 reads=3
  14 stack 맨 위 쓰기                 U:W 0x003ffffff8 -> ok  pa=0x4120fff8 L3 4K rw-u- reads=3
  15 stack 가드 페이지 쓰기           U:W 0x003ffefff8 -> TRANSLATION@L3 reads=3
  16 NULL 읽기                        U:R 0x0000000000 -> TRANSLATION@L2 reads=2
  17 커널이 user text 실행 (PXN)      K:X 0x0000400100 -> PERMISSION@L3 reads=3
  18 커널이 user heap 읽기 (PAN 없음) K:R 0x0001000040 -> ok  pa=0x41100040 L3 4K rw-u- reads=3
  19 user 가 UART 접근                U:R 0x7fc0000000 -> PERMISSION@L3 reads=3
  20 커널 UART 쓰기                   K:W 0x7fc0000004 -> ok  pa=0x9000004 L3 4K rw--g reads=3
  21 빈 1 GB 슬롯 (L1[128])           K:R 0x2000000000 -> TRANSLATION@L1 reads=1
  22 VA bit39 = 1                     K:R 0x8000000000 -> ADDRESS_SIZE@L1 reads=0
  23 boot identity 실행               K:X 0x0040000100 -> ok  pa=0x40000100 L2 2M r-x-- reads=2
  after boot: unmap identity -> tables 10 -> 9, kernel X 0x40000100 -> TRANSLATION@L1 reads=1
```

읽을거리:

- **#8 이 이 레이아웃의 구멍이다.** 커널 .text 는 RX 로 잘 매핑했지만, 같은 물리 페이지가 linear map 에서 **RW** 별칭으로 보인다 → 공격자가 커널 쓰기 프리미티브를 얻으면 linear map 주소로 커널 코드를 고칠 수 있다. Linux arm64 는 그래서 linear map 의 커널 text/rodata 별칭을 읽기 전용으로 바꾼다고 알려져 있고(`rodata=full` 류 설정), 그러려면 linear map 을 1 GB/2 MB 블록이 아닌 더 작은 단위로 만들어야 한다 — 블록 하나 안에서 일부만 권한을 바꿀 수 없으니까. 연습 2 가 이 split 을 직접 한다 (세부 동작은 커널 버전마다 다름 — 확인 필요).
- **가드 페이지 vs NULL**: 둘 다 TRANSLATION 이지만 레벨이 다르다. 스택 가드는 이웃 페이지 때문에 L3 테이블이 이미 있어서 L3 에서, NULL 은 첫 2 MB 에 아무것도 없어서 L2 에서 걸린다. 가드를 "매핑 안 함" 대신 "perms = 0 으로 매핑"(PROT_NONE) 하면 PERMISSION 이 된다 — 핸들러가 둘을 구별할 수 있다는 점이 설계 선택지다.
- **#17/#18**: PXN 개념은 있고 PAN 은 없다. 실제 커널은 둘 다 켠다.
- **identity map**: MMU 를 켜는 `msr sctlr_el1` 다음 명령어 fetch 는 이미 번역을 거친다. 그래서 PC 가 있는 물리 주소를 같은 VA 로 매핑(VA == PA)해 두고 켠 뒤, 커널 VA 로 점프하고, 끝나면 지운다. 지운 뒤 L1[1] 아래 L2 테이블이 비어 반납되므로 폴트가 L1 에서 난다(테이블 10 → 9). arm64 Linux 는 identity map 을 별도 테이블(idmap)로 TTBR0 에 잠깐 거는 방식으로 알려져 있다 (세부 확인 필요, ARM-A03 / RISCV-A01 다음 단계).
- **MMIO**: 실제로는 Device-nGnRE 같은 속성(MAIR 인덱스)과 XN 이 핵심이다. Normal-cacheable 로 매핑하면 투기적 읽기가 FIFO 레지스터를 비우는 식의 사고가 난다. 이 포맷엔 속성 필드가 없어 X 만 끈다 — **범위 밖**.

**1 GB 를 매핑하는 비용** (실제 출력):

```text
  map 1 GB      levels  tables  table-bytes  %of-1GB  reads/access
  4K pages        4      515      2060 KB    0.196%   4
  4K pages        3      514      2056 KB    0.196%   3
  4K pages        2      513      2052 KB    0.196%   2
  2M blocks       4        3        12 KB    0.001%   3
  2M blocks       3        2         8 KB    0.001%   2
  2M blocks       2        1         4 KB    0.000%   1
  1G block        4        2         8 KB    0.001%   2
  1G block        3        1         4 KB    0.000%   1
  1G block        2       -  (2 단계엔 L1 이 없음)
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">1 GB 매핑의 테이블 장수 (4 단계, 로그 축) — 위 실제 출력에서</text>
  <g stroke="currentColor"><path d="M150 40 L150 170"/><path d="M150 170 L720 170"/></g>
  <g stroke="currentColor" class="f-acc">
    <rect x="150" y="50" width="545" height="26" class="f-warn"/>
    <rect x="150" y="90" width="98" height="26" class="f-acc"/>
    <rect x="150" y="130" width="62" height="26" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="68">4K 페이지</text><text x="20" y="108">2M 블록</text><text x="20" y="148">1G 블록</text>
    <text x="470" y="68">515 장 · 2 MB · 읽기 4</text><text x="256" y="108">3 장 · 12 KB · 읽기 3</text><text x="220" y="148">2 장 · 8 KB · 읽기 2</text>
    <text x="146" y="186">1</text><text x="208" y="186">2</text><text x="244" y="186">3</text><text x="346" y="186">10</text><text x="546" y="186">100</text><text x="688" y="186">515</text>
    <text x="20" y="210">막대 길이 ∝ log2(장수): 1 장 = 0, 515 장 ≈ 9 → 545 px.</text>
    <text x="20" y="230">4K 매핑의 515 장 중 512 장이 L3 — 1 GB / 4 KB × 8 B = 2 MB.</text>
  </g>
</svg>
```

메모리만 보면 0.196% 로 작다. 진짜 비용은 **walk 와 TLB** 다: 4 KB 로 1 GB 를 덮으려면 TLB 엔트리 262144 개, 2 MB 면 512 개, 1 GB 면 1 개. 그래서 커널의 linear map·큰 DMA 버퍼·NPU 가중치 영역은 블록(huge page)을 원하고, 권한을 잘게 나눠야 하는 곳(커널 이미지 섹션, 유저 공간)은 페이지를 쓴다. 이 줄다리기가 A02(TLB reach) 와 A03(실측: macOS 16 KB 페이지) 의 주제다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o so && ./so          # 161/161 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_san && ./so_san   # 깨끗, 161/161
```

(Apple M6, Apple clang 21.0.0. 단일 스레드 결정적 모델이라 타이밍 없음 · TSan 해당 없음. 두 번 실행한 출력이 바이트 단위로 같다 — `cmp` 확인.)

설계 메모:

- **워커와 매퍼는 같은 "레벨 → shift → 인덱스" 함수를 쓴다.** 둘이 다른 식을 쓰면 매퍼가 쓴 엔트리를 워커가 못 찾는 버그가 생기는데, 하드웨어/소프트웨어 공동 설계에서 실제로 흔한 종류다(커널 매크로 vs RTL 의 비트 슬라이스). 스트레스 테스트가 이 둘의 일관성을 무작위로 확인한다.
- **"빈 비루트 테이블 없음" 을 불변식으로** 둔 덕에 블록 설치 조건이 단순해졌다: 범위가 비어 있으면 해당 엔트리는 무효이거나(블록 가능) 다른 매핑이 있는 테이블이다. 빈 테이블이 남아 있으면 그 자리에 블록을 못 놓고 페이지로 내려가는 미묘한 성능 버그가 생긴다 — 감사기가 50 연산마다 확인.
- **walk_reads 를 하네스가 독립적으로 센다** (`rd_desc` 카운터의 차이). 학생 코드가 `walk_reads = level + 1` 처럼 계산해 넣으면 정직성 검사에 걸린다.
- **I/O 는 하나의 물리 메모리 배열**: 테이블은 PA 0x4800_0000 의 풀에서 4 KB 프레임으로 나간다. 데이터 페이지의 PA 는 실제 저장소가 없는 "주소" 일 뿐이다(이 레슨은 변환만 본다). 테이블 풀이 DRAM linear map 안(0x4000_0000 + 128 MB)에 있으니, 커널이 linear map 주소로 자기 페이지 테이블을 고치는 실제 구조와 같다.

## 7. 자주 하는 실수

1. **walk_reads 를 폴트 전에 안 센다** — `if (!(d & D_V)) return` 을 `walk_reads++` 앞에 두면 무효 엔트리를 읽은 횟수가 빠진다(사다리의 TRANSLATION@L0 이 reads=0 이 됨). 읽기는 일어났다 — 메모리 버스는 그 읽기를 했다. 고친 코드: 읽자마자 `r.walk_reads++; r.level = lv;`.
2. **L3 의 T 비트를 블록/페이지와 혼동** — split 에서 하위가 L3 인데 `D_V` 만 쓰면 512 개 전부 잘못된 인코딩 → 모든 접근이 TRANSLATION@L3. `split_block 단독` 테스트가 512 페이지를 전부 확인하는 이유. 고친 코드: `kind = (lv + 1 == 3) ? (D_V | D_T) : D_V;`.
3. **블록 조건에서 pa 정렬을 빼먹는다** — va 만 보면 pa = 0x4020_1000 으로 2 MB 블록을 만들고, 워커가 `OA & ~(2M−1)` 로 내려서 **4 KB 어긋난 물리 주소**를 돌려준다. 조용한 데이터 손상. 고친 코드: `((va | pa) & (esz - 1)) == 0`.
4. **split 때 권한을 잃는다** — 새 엔트리에 `D_R | D_W` 같은 기본값을 넣으면 RX 블록 일부를 unmap 하는 순간 나머지가 실행 불가가 된다(또는 RO 가 RW 가 된다 — 보안 버그). `d & LEAF_ATTR` 를 그대로 물려줘야 한다. B-4 가 split 후 `perms == R|W` 를 CHECK.
5. **빈 테이블을 반납 안 한다** — 기능은 맞는데 (a) 메모리 누수, (b) 그 자리에 나중에 블록을 못 놓음, (c) 폴트 레벨이 바뀜(identity 제거 후 L1 이 아니라 L2 에서 남). `final_tables == 1` 과 감사기가 잡는다.
6. **겹침을 설치하면서 검사한다** — 반쯤 매핑한 뒤 EEXIST 를 돌려주면 부분 매핑이 남는다. "검사 먼저, 설치 나중, 실패면 롤백" 이 원자성을 준다.
7. **권한 검사를 테이블 디스크립터에도 한다(이 모델에서)** — 테이블 디스크립터의 R/W 비트는 0 이므로 모든 접근이 PERMISSION 이 된다. 이 포맷은 leaf 만 본다. 실제 ARM 의 APTable/UXNTable/PXNTable 이나 x86 의 "모든 레벨 AND" 는 **추가 제한**이고, 그러면 테이블 디스크립터에 허용 비트를 켜 둬야 한다.

## 8. 면접 Q&A

<details><summary>Q1. 4 단계 페이지 테이블에서 TLB 미스 한 번에 메모리 접근이 몇 번인가? 2 MB 페이지면? 가상화에서는?</summary>

데이터 접근을 빼고 디스크립터 읽기만 4 번(L0–L3), 2 MB 블록이면 3 번, 1 GB 면 2 번 — 이 레슨의 `walk_reads` 표. 그 뒤 데이터 접근 1 번. 실제로는 디스크립터가 캐시에 있을 수 있고, page-walk cache 가 상위 레벨 결과를 기억해 L3 하나만 읽기도 하므로 4 는 최악값이다.

꼬리질문: "2 단계 변환(가상화)에서는?" → 게스트의 테이블 주소 자체가 IPA(게스트 물리)라 매번 호스트 테이블로 번역해야 한다. 게스트 4 단계 × 호스트 4 단계면 최악 (4+1)×(4+1) − 1 = 24 번의 메모리 접근이 널리 인용되는 숫자다 (VIRT-B05 다음 단계). 그래서 호스트 쪽에 큰 블록을 쓰는 게 가상화 성능에 특히 중요하다.
</details>

<details><summary>Q2. 왜 블록(huge page)의 물리 주소는 블록 크기로 정렬돼야 하나?</summary>

워커가 pa 를 `OA | (va 의 하위 21 비트)` 로 **붙이기** 때문이다 — 더하기가 아니다. 하위 비트가 VA 에서 오니 OA 의 그 비트는 0 이어야 한다. 하드웨어 입장에서 덧셈기 없이 비트 연결만으로 번역하는 게 싸고 빠르며, TLB 엔트리도 "VA 상위 비트 → PA 상위 비트" 만 저장하면 된다. 이 레슨의 map 은 pa 가 정렬 안 되면 블록을 포기하고 페이지로 내려간다(4 KB 어긋난 2 MB → 페이지 512 장).

꼬리질문: "그럼 물리 메모리가 단편화되면?" → 2 MB 연속·정렬된 물리 공간을 못 구하면 huge page 를 못 쓴다. Linux 는 compaction 으로 모으거나(THP), 부팅 때 예약(hugetlbfs, CMA)한다. 펌웨어/NPU 드라이버는 부팅 직후 큰 정렬 영역을 미리 잡아 두는 게 정석이다.
</details>

<details><summary>Q3. 블록 매핑 일부의 권한을 바꾸거나 unmap 하려면? 실제 ARM 에서 주의할 점은?</summary>

블록을 다음 레벨 테이블로 split 한다 — 같은 매핑을 512 개로 펼치고 권한·속성을 그대로 물려준 뒤 필요한 엔트리만 바꾼다. 1 GB 블록 안의 4 KB 하나면 split 2 번, 테이블 +2 장(레슨 B-4). 실제 ARMv8 에서는 블록 → 테이블 교체에 **break-before-make**: 무효 디스크립터 쓰기 → TLBI(해당 VA) → DSB → 새 디스크립터 → (필요하면) ISB. 안 그러면 같은 VA 에 크기가 다른 TLB 엔트리 두 개가 생겨 TLB conflict abort 등이 날 수 있다(FEAT_BBM 레벨에 따라 완화 — 세부 확인 필요).

꼬리질문: "BBM 동안 다른 코어가 접근하면?" → 무효 구간이라 폴트가 난다. 그래서 커널 자신이 쓰는 영역(linear map)을 실행 중에 split 하는 것은 위험하고, Linux arm64 는 처음부터 페이지 단위로 만들거나(일부 설정) split 을 피하는 쪽을 택하는 것으로 알려져 있다.
</details>

<details><summary>Q4. 폴트가 "어느 레벨에서" 났는지가 왜 중요한가? 펌웨어 폴트 핸들러는 무엇을 보나?</summary>

종류(미할당 vs 권한)가 핸들러의 분기를 정하고, 레벨은 "트리가 어디까지 있나" 를 알려 준다. TRANSLATION@L1 이면 그 1 GB 슬롯에 테이블조차 없다 — 잘못된 포인터(보통 버그)일 가능성이 크고, TRANSLATION@L3 이면 이웃 페이지가 있는 곳의 구멍 — 가드 페이지 접근이나 요구 페이징 대상. PERMISSION 은 COW(쓰기) 나 W^X 위반. 레슨에서 스택 가드는 L3, NULL 은 L2, 빈 슬롯은 L1 이었다. ARM 은 ESR_ELx 의 fault status code 에 종류+레벨을, FAR 에 VA 를 담는다(정확한 인코딩은 ARM ARM).

꼬리질문: "가드 페이지를 PROT_NONE 으로 매핑하는 것과 비워 두는 것의 차이는?" → 비우면 TRANSLATION, PROT_NONE 이면 PERMISSION. 전자는 테이블 엔트리가 0 이라 공짜, 후자는 핸들러가 "의도된 가드" 를 확실히 구별할 수 있다. 메모리는 둘 다 4 KB 물리 페이지를 쓰지 않는다.
</details>

<details><summary>Q5. 권한을 leaf 에서만 보면 무엇을 놓치나? ARM APTable 과 x86 은?</summary>

leaf-only 는 단순하지만 "이 1 GB 슬롯 전체는 유저 접근 금지" 같은 계층적 정책을 표현 못 한다 — 아래 leaf 하나라도 U=1 을 잘못 쓰면 뚫린다. ARM 은 테이블 디스크립터에 APTable / UXNTable / PXNTable 을 두어 **아래 전체에 제한을 덧씌운다**(허용을 넓히지는 못함). x86-64 는 모든 레벨의 U/S·R/W 를 AND, NX 는 OR 로 합쳐 같은 효과를 낸다. 커널은 이걸로 "커널 절반 전체 PXN 금지" 같은 방어를 싸게 건다.

꼬리질문: "그럼 이 레슨의 워커에 APTable 을 넣으려면?" → walk 중 테이블 디스크립터를 지날 때마다 제한 마스크를 누적(AND/OR)하고 leaf 에서 함께 검사. walk_reads 는 그대로, 권한 함수의 입력만 늘어난다.
</details>

<details><summary>Q6. 커널은 왜 linear map 을 큰 블록으로 만들고 싶어 하나? 왜 어떤 설정은 거부하나?</summary>

DRAM 전체를 커널 VA 로 보는 linear map 은 GB 단위다 — 4 KB 로 매핑하면 테이블 수백 장~수 MB, 그리고 TLB 미스 폭증(1 GB 에 엔트리 262144 개 vs 1 개). 그래서 1 GB/2 MB 블록. 그러나 linear map 은 커널 text·rodata 의 **별칭**을 포함한다 — 레슨 #8 처럼 RW 별칭으로 커널 코드를 쓸 수 있다. 별칭을 RO 로 바꾸려면 블록을 쪼개야 하고, 살아 있는 블록 split 은 BBM 문제(Q3)가 있다. 그래서 보안 우선 설정은 linear map 을 처음부터 페이지 단위로 만들고 TLB 비용을 감수하는 것으로 알려져 있다(Linux arm64 `rodata=full`, `debug_pagealloc` 등 — 버전별 세부 확인 필요).

꼬리질문: "TLB 비용을 줄이면서 페이지 단위를 유지하는 방법은?" → ARM 의 contiguous bit: 연속·정렬된 16 개(4 KB granule 기준) 엔트리가 같은 속성이면 TLB 가 하나로 합칠 수 있다는 힌트(64 KB). 세부 조건은 ARM ARM.
</details>

<details><summary>Q7. MMU 없는 Cortex-M 에서 이 레슨의 무엇이 남나? SMMU/IOMMU 는?</summary>

Cortex-M 은 변환이 없고 MPU 의 영역 권한 검사만 있다(ARCH-M15): "VA = PA, 권한 표만" 이라 walk_reads 0 의 MMU 라고 볼 수 있다. 영역 수가 8–16 개로 제한돼 Part C 처럼 세밀한 레이아웃은 못 하고, 스택 가드는 MPU 영역 하나로 만든다. 반대로 SoC 의 DMA/NPU 앞에는 SMMU(ARM)·IOMMU 가 같은 페이지 테이블 워커를 하드웨어로 갖고 있어 장치가 VA(IOVA)로 접근하게 한다 — 장치 격리(NPU 가 커널 메모리를 못 건드리게)가 목적이고, 장치 쪽 TLB 미스는 DMA 지연을 크게 흔든다 (SOCX-M12, PCIE-M15 다음 단계).

꼬리질문: "IOMMU 에서 페이지 폴트가 나면?" → CPU 처럼 재시도할 수 없는 장치가 많아 보통 트랜잭션이 abort 되고 장치 오류가 된다. 그래서 DMA 버퍼는 미리 매핑·고정(pin)한다. PCIe PRI 같은 장치 페이지 폴트 메커니즘도 있지만 지원이 제한적이다.
</details>

<details><summary>Q8. 이 워커가 맞는지 어떻게 검증했나? (실리콘 검증 관점)</summary>

세 층: (1) directed 테스트 — 폴트 레벨 사다리, 블록 경계, 손으로 쓴 잘못된 인코딩, ENOMEM 롤백. (2) 참조 모델 대조 — 4 KB 단위 평면 그림자 배열을 "정답" 으로, map/unmap 을 무작위로 섞고 매 연산 뒤 프로브 24 개를 비교 (3 설정 × 36000 프로브, 불일치 0). 트리 구조가 아무리 복잡해져도 평면 모델은 단순해서 믿을 수 있다. (3) 구조 불변식 — 50 연산마다 트리 감사(빈 테이블, 미정렬 블록, 카운터 일치). 그리고 커버리지(split·EEXIST·L1/L2/L3 히트 > 0)를 CHECK 해서 "통과" 가 공허하지 않음을 보였다 — 처음 버전은 L1 히트 0 이라 커버리지 CHECK 가 실패했고, 생성기에 "창 비우기" 를 넣어 고쳤다.

꼬리질문: "RTL 워커라면?" → 같은 구조: 참조 모델(이 C 코드 같은 것)을 scoreboard 로, 랜덤 테이블 생성 + 랜덤 VA 스티뮬러스, 디스크립터 읽기 횟수·순서를 모니터로 비교. 추가로 동시성(walk 중 테이블 갱신, 여러 outstanding walk)이 RTL 에서만 생기는 버그원이다.
</details>

## 9. 추가 연습

1. **테이블 장수 공식.** 빈 트리에 [va, va+size) 를 leaf 레벨 Lf 로만 매핑할 때 테이블 수를 식으로 쓰고, 3 설정 × 60 개의 결정적 랜덤 범위(페이지만 / 2 MB 블록만)에서 시뮬레이터와 비교하라. 4 단계에서 [0x7FFF_F000, 0x8000_1000) 페이지 2 장은 몇 장인가?
   <details><summary>답</summary>

   `tables = 1 + Σ_{lv = 시작+1}^{Lf} span(level_size(lv−1))`, `span(S) = ⌊(end−1)/S⌋ − ⌊va/S⌋ + 1` — 레벨 lv 의 테이블 1 장은 "레벨 lv−1 엔트리 하나가 덮는 정렬 구간" 하나에 대응한다. 180/180 일치. 예제: L1 테이블은 512 GB 구간 1 개 → 1, L2 테이블은 1 GB 구간 2 개(0x7FFF_F000 은 GB 인덱스 1, 0x8000_0000 = 2 GB 는 GB 인덱스 2) → 2, L3 테이블은 2 MB 구간 2 개 → 2. 합 1 + 1 + 2 + 2 = **6**. 페이지 2 장에 테이블 6 장(24 KB) — 경계에 걸친 작은 매핑이 가장 비싸다.
   </details>
2. **`mmu_protect(va, size, perms)`.** 범위가 전부 매핑돼 있는지 먼저 확인(아니면 ENOENT, 아무것도 안 바꿈)하고, leaf 를 통째로 덮으면 권한만 바꾸고 일부면 split 하라. Part C 레이아웃에서 linear map 의 커널 text 별칭 [0x60_0000_0000, +2 MB) 를 R 로 만들면 테이블과 walk 가 어떻게 바뀌나?
   <details><summary>답</summary>

   실제 출력: `protect [0x6000000000, +2M) R: tables 10 -> 11, splits +1, K:W 0x6000001234 -> PERMISSION@L2 reads=2`. 1 GB 블록이 L2 테이블(2 MB 블록 512 개)로 split 1 번, 첫 2 MB 만 R. 접근 #8 의 구멍이 막혔다. 대가: linear map 의 **모든** 접근이 읽기 1 → 2 번(이웃 0x6012345678 도 L2 / reads 2). 4 KB 하나를 더 R 로 하면 그 2 MB 가 또 split(테이블 12, 그 페이지 L3/reads 3). 실제 칩에선 같은 leaf 의 권한만 바꿀 때는 TLBI 로 충분하고, 블록 split 은 BBM(Q3)이 필요하다.
   </details>
3. **ptdump.** 트리를 VA 순서로 훑어 "연속 VA · 연속 PA · 같은 권한 · 같은 leaf 크기" 인 leaf 를 범위로 합쳐 출력하라 (Linux `kernel_page_tables` debugfs 비슷하게). W+X 범위와 user+global 범위를 감사 항목으로 표시하라.
   <details><summary>답</summary>

   Part C 레이아웃은 정확히 10 범위(= 영역 10 개, 위반 0)로 나온다. 커널 .text(2M 블록)와 .rodata(4K) 는 VA·PA 가 연속이어도 권한이 달라 합쳐지지 않는다. RWX + U + G 매핑(JIT 실수)을 추가하면 `audit flags = 2`. user+global 이 위험한 이유: G=1 엔트리는 ASID 와 무관하게 TLB 에 남아 **다른 프로세스에서도 보인다** — A02 의 주제. 실제 출력 일부:

   ```text
     0x0000400000-0x0000410000      64K r-xu- 4K -> pa 0x41000000
     0x4000000000-0x4000200000    2048K r-x-g 2M -> pa 0x40000000
     0x6000000000-0x6040000000 1048576K rw--g 1G -> pa 0x40000000
     + 0x0002000000-0x0002002000 rwxug  -> audit flags = 2 (W+X, user+global)
   ```
   </details>
4. **collapse.** 2 MB 정렬 VA 의 L2 엔트리가 L3 테이블을 가리키고, 512 페이지가 모두 유효 · 같은 속성 · PA 가 2 MB 정렬 base 부터 연속이면 블록으로 바꾸고 L3 테이블을 반납하라 (khugepaged 의 아이디어). 접을 수 없는 세 경우를 구별해 보고하라.
   <details><summary>답</summary>

   `MAP_PAGES` 로 만든 2 MB → collapse → `tables 3 -> 2, reads 2, leaf 2M`. 실패: 구멍(한 페이지 unmap) → COL_HOLE, 한 페이지 권한 다름 → COL_ATTR, PA 가 2 MB 정렬 아님 → COL_PA. 실제 커널은 PA 가 흩어져 있으면 새 2 MB 물리 영역을 할당해 **복사**한 뒤 collapse 한다(그래서 khugepaged 가 백그라운드 작업이다). 교체는 split 과 마찬가지로 BBM + TLBI 가 필요하다.
   </details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o pr && ./pr      # solution.c 를 포함하므로 같은 폴더에서
```

실제 출력:

```text
== 연습 1: 테이블 장수 공식 vs 시뮬레이터 ==
  180/180 random ranges match
  4-level, 2 pages across the 2 GB line (0x7FFFF000..0x80001000): tables = 6
== 연습 2: mmu_protect — linear map 의 커널 text 별칭을 RO 로 ==
  protect [0x6000000000, +2M) R: tables 10 -> 11, splits +1, K:W 0x6000001234 -> PERMISSION@L2 reads=2
== 연습 3: ptdump + W^X / user+global 감사 ==
  0x0000400000-0x0000410000      64K r-xu- 4K -> pa 0x41000000
  ... (10 범위)
  + 0x0002000000-0x0002002000 rwxug  -> audit flags = 2 (W+X, user+global)
== 연습 4: collapse — 페이지 512 장을 2 MB 블록으로 ==
  collapse 0x800000: tables 3 -> 2, reads 2, leaf 2M
  hole -> -11 (COL_HOLE), different perms -> -12 (COL_ATTR), PA not 2M-aligned -> -13 (COL_PA)
31/31 passed
```

## 10. 핵심 정리

- 4 KB granule: 레벨당 9 비트, 테이블 1 장 = 512 × 8 B = 4 KB. levels 2/3/4 → VA 30/39/48 bit, 마지막 레벨은 항상 L3, 시작 레벨 = 4 − levels.
- walk_reads = leaf 레벨 − 시작 레벨 + 1: 4 단계 페이지 4 · 2 MB 3 · 1 GB 2. 가상화 2 단계면 최악 24.
- 워커 5 갈래: V=0 → TRANSLATION / T=1 → 내려감 / L0 블록·L3 T=0 → TRANSLATION / leaf → 권한 → PERMISSION or `pa = OA_aligned | va_low`. ADDRESS_SIZE 는 walk 전, 읽기 0.
- 폴트 레벨 = 트리가 어디까지 있나: 가드 L3, NULL L2, 빈 슬롯 L1. 핸들러는 종류 + 레벨 + VA 로 분기.
- 블록 = va·pa 모두 정렬 + 엔트리 전체. 부분 unmap/권한 변경 → split(1 GB 에서 4 KB = 2 번), 실제 칩은 break-before-make + TLBI. 빈 테이블은 반납.
- 1 GB: 4K 515 장(2 MB) vs 2M 3 장 vs 1G 2 장. 진짜 비용은 메모리보다 walk 와 TLB reach.
- 같은 PA 의 별칭(kernel text vs linear map)은 권한이 따로다 — linear map 의 RW 별칭은 보안 구멍, 막으려면 split 이 필요.

## 참고자료

- Arm, *Arm Architecture Reference Manual for A-profile architecture* (DDI 0487) — VMSAv8-64: 4KB granule 변환 테이블 형식, 레벨별 블록 크기, 디스크립터 bit[1:0], APTable/UXNTable/PXNTable, Access flag, break-before-make, fault status code (장·절 번호는 판마다 다름 — 확인 필요).
- Arm Developer, *Learn the architecture — AArch64 memory management* 가이드 — TTBR0/TTBR1, TxSZ, 레벨/granule 표, BBM 설명.
- Linux 커널 문서 `Documentation/arch/arm64/memory.rst` — 4K/39-bit(3 단계)·4K/48-bit(4 단계) 레이아웃, TTBR1 커널 절반 (경로는 버전마다 다름).
- R. H. Arpaci-Dusseau, A. C. Arpaci-Dusseau, *Operating Systems: Three Easy Pieces* — 19 장 TLB, 20 장 Paging: Smaller Tables(다단계 페이지 테이블).
- J. L. Hennessy, D. A. Patterson, *Computer Architecture: A Quantitative Approach*, 6th ed., 2 장·부록 B — 가상 메모리와 TLB.
- Intel, *64 and IA-32 Architectures SDM* Vol. 3A, 4 장 Paging — 4-level/5-level, 모든 레벨 권한 AND, SMEP/SMAP 비교.
- QEMU 소스 `hw/arm/virt.c` 의 memory map — Part C 물리 주소 가정(RAM 0x4000_0000, UART 0x0900_0000, GIC 0x0800_0000).
- 이 노트의 관련 레슨: OS-B10 (선형 페이지 테이블 — 이 레슨의 1 단계 버전), ARCH-M15 (MPU), ARCH-A04 (커버리지·불변식 검증 방식). 다음 단계: **ARCH-A02** (이 포맷 위의 TLB · ASID · 플러시), ARCH-A03 (macOS 16 KB 페이지 실측), OS-M01 (map/unmap/protect + TLB 무효화), ARM-A03 (AArch64 MMU 켜기: MAIR · TCR), RISCV-M12 (Sv39/Sv48 워커), VIRT-B05 (2 단계 변환), SOCX-M12 (SMMUv3), PCIE-M15 (IOMMU).
