---
id: OS-M01
time: 90분
updated: 2026-11-11
---
# OS-M01 페이지 테이블 관리자: map/unmap/protect와 TLB 무효화

## 학습 목표

- 4단계 페이지 테이블에 `map_range / unmap_range / protect_range` 를 직접 짠다. 중간 테이블은 필요할 때 할당하고, unmap 으로 빈 테이블은 **아래에서 위로** 회수해서 프레임 수가 처음 값으로 돌아오는 것을 테스트로 확인한다.
- TLB 무효화를 **언제, 얼마나** 하는지 정책으로 정한다. 33 페이지 이하는 VA 별(`tlbi_va`), 넘으면 ASID 전체(`tlbi_asid`), 전역 페이지가 섞였으면 전부(`tlbi_all`). 테이블을 풀었으면 walk cache 까지 지운다. 기준값 33 이 Linux x86 의 `tlb_single_page_flush_ceiling` 이라는 것도 확인한다.
- 권한을 **늘릴** 때는 무효화하지 않아도 된다는 것(낡은 엔트리로 생긴 폴트를 *spurious fault* 로 처리)과, **줄이거나 지울** 때는 프레임을 재사용하기 **전에** 반드시 무효화해야 한다는 것(mmu_gather 순서)을 설명하고 테스트로 보인다.
- 무효화를 빠뜨렸을 때 실제로 무슨 일이 나는지 mock TLB 로 재현한다. 다른 주소 공간의 비밀을 읽는 경우와, 해제된 테이블을 walk cache 가 계속 따라가는 경우다.
- 펌웨어/드라이버 상황(ioremap/iounmap, DMA 버퍼 넘기기, 페이지 이동, 프로세스 종료)에서 무엇을 무효화해야 하는지 말한다. ARM 의 break-before-make 와 SMP 의 TLB shootdown 까지.

## 1. 문제

"munmap 하면 커널은 무엇을, 어떤 순서로 하나요?" 커널·하이퍼바이저·Cortex-A 위 RTOS 면접에서 자주 나오는 질문이다. 페이지 테이블 **포맷**과 **워커**는 하드웨어가 정한다(ARCH-A01). 하지만 테이블을 언제 만들고, 언제 지우고, TLB 를 언제 비울지는 전부 **OS 소프트웨어의 책임**이다. 여기서 순서를 하나라도 틀리면 버그가 된다. 해제한 페이지에 다른 프로세스의 데이터가 올라온 뒤에도 옛 TLB 엔트리로 그 페이지를 읽게 되고, 이런 버그는 재현이 거의 안 된다. 드라이버가 DMA 버퍼나 MMIO 창을 매핑하고 해제하는 코드에도 같은 규칙이 적용된다.

이 레슨은 **ARCH-A01 모델을 축소해 넣었다**: 4단계(L0→L3), 4 KB 페이지, 레벨당 9 비트, 디스크립터 bit0 V · bit1 T · bit2 R · bit3 W · bit4 X · bit5 U · bit7 G · bits 12..47 OA. 블록 매핑과 split 은 ARCH-A01 에서 다뤘으므로 여기서는 쓰지 않는다. 대신 OS 쪽 연산과 bit55 `SW`(이 매핑이 프레임을 소유함, unmap 때 반납) 를 더했다. 실제 ARMv8 디스크립터도 55..58 비트를 소프트웨어용으로 비워 둔다. TLB 모델은 ARCH-A02 에서 ASID 와 교체 정책을 다뤘다. 여기서는 그 TLB 를 **무효화를 받는 쪽**으로만 쓴다. 대신 ARCH-A02 에 없던 **walk cache**(중간 테이블 포인터 캐시)를 더했다.

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">unmap_range 한 번의 순서: 지우고 → 무효화하고 → 그다음에 반납</text>
<rect x="10" y="40" width="200" height="120" rx="4" class="f-mut" stroke="currentColor"/>
<text x="20" y="60" fill="currentColor">페이지 테이블 (메모리)</text>
<rect x="25" y="70" width="50" height="22" class="f-acc" stroke="currentColor"/><text x="33" y="86" fill="currentColor" font-size="12">L0</text>
<rect x="85" y="70" width="50" height="22" class="f-acc" stroke="currentColor"/><text x="93" y="86" fill="currentColor" font-size="12">L1</text>
<rect x="145" y="70" width="50" height="22" class="f-acc" stroke="currentColor"/><text x="153" y="86" fill="currentColor" font-size="12">L2</text>
<rect x="145" y="100" width="50" height="22" class="f-warn" stroke="currentColor"/><text x="153" y="116" fill="currentColor" font-size="12">L3</text>
<text x="20" y="145" fill="currentColor" font-size="12">① set_desc(PTE, 0), live--</text>
<rect x="270" y="40" width="200" height="120" rx="4" class="f-mut" stroke="currentColor"/>
<text x="280" y="60" fill="currentColor">mmu_gather (소프트웨어)</text>
<text x="280" y="82" fill="currentColor" font-size="12">va[] : 무효화할 VA 들</text>
<text x="280" y="100" fill="currentColor" font-size="12">freed_tables, global, fullmm</text>
<text x="280" y="118" fill="currentColor" font-size="12">frames[] : 반납 대기 프레임</text>
<text x="280" y="145" fill="currentColor" font-size="12">② 빈 테이블도 여기에 (prune)</text>
<rect x="530" y="40" width="200" height="120" rx="4" class="f-mut" stroke="currentColor"/>
<text x="540" y="60" fill="currentColor">하드웨어 캐시</text>
<rect x="545" y="70" width="170" height="24" class="f-ok" stroke="currentColor"/><text x="553" y="87" fill="currentColor" font-size="12">TLB: (ASID,VPN)→PA,권한</text>
<rect x="545" y="102" width="170" height="24" class="f-ok" stroke="currentColor"/><text x="553" y="119" fill="currentColor" font-size="12">walk cache: VA[47:21]→L3</text>
<text x="540" y="145" fill="currentColor" font-size="12">메모리를 바꿔도 안 바뀐다</text>
<path d="M210 100 L268 100" stroke="currentColor" fill="none" stroke-width="1.5"/><path d="M261 95 L269 100 L261 105" stroke="currentColor" fill="none"/>
<path d="M470 100 L528 100" stroke="currentColor" fill="none" stroke-width="1.5"/><path d="M521 95 L529 100 L521 105" stroke="currentColor" fill="none"/>
<text x="476" y="92" fill="currentColor" font-size="12">③ TLBI</text>
<rect x="10" y="190" width="720" height="60" rx="4" class="f-warn" stroke="currentColor"/>
<text x="20" y="210" fill="currentColor">③ 정책: n ≤ 33 → 페이지마다 VA_LEAF (테이블을 풀었으면 VA = walk cache 까지)</text>
<text x="20" y="228" fill="currentColor">     n &gt; 33 → ASID 한 번 · 전역(G) 페이지가 섞였으면 ALL · 프로세스 종료(fullmm) → ASID 한 번</text>
<text x="20" y="244" fill="currentColor">④ 그다음에야 free_frame — 순서가 바뀌면 다른 주소 공간이 그 프레임을 받는 순간 옛 TLB 로 읽힌다</text>
<text x="10" y="280" fill="currentColor" font-size="12">권한 확대(R→RW)는 ③ 을 건너뛴다. 낡은 R 엔트리로 쓰기 폴트가 나면 PTE 는 이미 W 다</text>
<text x="10" y="298" fill="currentColor" font-size="12">→ spurious fault → 그 VA 만 무효화하고 재시도.</text>
<text x="10" y="322" fill="currentColor" font-size="12">새 매핑(무효 → 유효)도 ③ 이 필요 없다. 무효 엔트리는 TLB 에 들어가지 않는다</text>
<text x="10" y="340" fill="currentColor" font-size="12">(이 모델의 규칙. ARM 도 그렇게 보장한다고 알고 있음).</text>
<text x="10" y="364" fill="currentColor" font-size="12">검사기: 지운 엔트리가 가리키던 프레임 = pending. 덮는 TLBI 가 오면 풀린다.</text>
<text x="10" y="382" fill="currentColor" font-size="12">pending 인 채로 free_frame → early_free++.</text>
</svg>
```

| 구성 요소 | 이 레슨 (단일 코어 모델) | 실제 (Linux/ARM64) |
|---|---|---|
| 프레임 할당기 | LIFO 스택 1024 프레임 (4 MB), 테이블·데이터 공용 | buddy allocator, 테이블은 `pte_alloc` 등 |
| 테이블 회수 | 테이블마다 `live` 카운트, 0 이 되면 즉시 위로 회수 | `free_pgtables()` 가 VMA 경계(floor/ceiling) 기준으로 통째로 (기억 기준) |
| 묶음 무효화 | `gather_t` → `tlb_finish` | `struct mmu_gather` → `tlb_finish_mmu()` |
| 무효화 명령 | `TLBI_VA_LEAF / VA / ASID / ALL` 로그 | `TLBI VALE1IS / VAE1IS / ASIDE1IS / VMALLE1IS` |
| 스트레스 검사 | `tlb_audit()` = TLB 엔트리 vs 새 워크 | 없음 (그래서 버그가 숨는다) |

### Part A — 매핑 관리: 테이블 즉석 할당, 회수, 롤백

- **A-1** `walk_alloc(as, va)` — L0..L2 에서 엔트리가 무효면 테이블 프레임을 할당해 붙이고 부모 `live++`. L3 슬롯 주소를 돌려준다.
- **A-2** `map_range(as, va, pa, size, perms, flags)` — `pa == PA_ANON` 이면 페이지마다 새 프레임(0 채움, `SW`), 아니면 연속 물리 주소(MMIO). 검사 순서: `E_ALIGN → E_RANGE → E_PERM → E_EXIST`(겹치면 아무것도 안 바꿈). 도중에 메모리가 모자라면 **이번에 만든 것 전부 롤백** 후 `E_NOMEM`.
- **A-3** `prune_path(as, va, g)` — va 경로의 가장 깊은 테이블부터 `live == 0` 이면 부모 엔트리를 지우고 gather 에 넣는다. 루트는 풀지 않는다.
- **A-4** `unmap_range` — 빈 하위 트리는 통째로 건너뛰고, 지운 페이지 수를 돌려준다(멱등). **A-5** `protect_range` — 구멍이 있으면 `E_NOENT`, 권한을 **잃은** 페이지만 gather 에 넣는다.
- 검증: 프레임 수(데이터 3 + 테이블 3), 2 MB 경계, 겹침·정렬·범위·권한 오류, ENOMEM 두 경우(데이터에서 실패 / L3 테이블에서 실패) 뒤 프레임 수가 처음 값, MMIO 는 데이터 프레임 0, 그리고 **3000 연산 랜덤 스트레스**: 매 연산 뒤 그림자 모델과 비교하고, 테이블 수 = 공식(연습 2) 인지 본다.

### Part B — 무효화 정책: ceiling, ASID vs ALL, walk cache, spurious fault

- **B-1** `tlb_finish(g)` — 위 그림 ③ 의 정책을 적용한 뒤 프레임을 반납한다.
- **B-2** `mmu_access` 안의 spurious fault — TLB 의 권한으로는 거부되지만 새 워크로는 허용되면, `M.spurious++`, 그 VA 만 무효화하고 다시 시도한다.
- 검증: 33 페이지 → `VA_LEAF` 33 번, 이어서 7 페이지(테이블 반납) → `VA` 7 번, 34 페이지 → `ASID` 1 번. 다른 ASID 의 엔트리는 살아남는다. 전역 40 페이지 → `ALL`. 권한 축소 8 페이지 → 8 번, 확대 → **0 번**, 그리고 첫 쓰기에서 spurious 1 번.
- **버그 주입 3종**(하네스 `M.bug`): ① 무효화 생략 → 다른 주소 공간이 쓴 비밀을 읽음, ② 반납 먼저·무효화 나중 → 결과는 멀쩡해 보이지만 `early_free` 로 잡힘, ③ 테이블을 풀었는데 leaf 만 무효화 → walk cache 가 **남의 데이터 페이지를 테이블로** 읽음.

### Part C — 드라이버·OS 시나리오

- **C-1/C-2** `teardown_level`, `as_destroy` — 프로세스 종료. 트리 전체를 해체하고 `fullmm` gather 로 ASID 를 한 번만 무효화한 뒤 반납한다. **ASID 를 놓아주는 것은 무효화 뒤**다. 같은 ASID 로 새 프로세스를 만들어도 낡은 변환이 없어야 한다.
- **C-3** `migrate_page` — 페이지 이동(압축·NUMA): break → 무효화 → 복사 → make → 옛 프레임 반납. `bbm_viol == 0`.
- 시나리오 테스트: 커널 주소 공간에 UART MMIO 를 `ioremap` 처럼 전역으로 매핑했다가 해제한다(데이터 프레임 0, `VA` 4 번). DMA 버퍼 40 페이지를 장치에 넘길 때 RW→R 로 바꾸면 40 &gt; 33 이므로 `ASID` 1 번이고, 그 뒤 CPU 쓰기는 PERMISSION 이다. 돌려받을 때 R→RW 는 무효화 0 번이다.

## 2. Problem (English)

> You are writing the page-table manager of a small OS for a 4-level, 4 KB-page MMU (descriptor format given: V, T, R, W, X, U, G, OA[47:12], plus a software "owned frame" bit). Physical frames for both page tables and data come from one allocator. The CPU has an ASID-tagged TLB and a walk cache (VA[47:21] → last-level table). Invalidation primitives are `tlbi_va_leaf(asid, va)`, `tlbi_va(asid, va)` (also drops walk-cache entries for that VA), `tlbi_asid(asid)` (non-global entries only) and `tlbi_all()`.
>
> Implement:
> - `int map_range(as, va, pa, size, perms, flags)` — allocate intermediate tables on demand; `pa == PA_ANON` allocates and owns one zeroed frame per page; fail with `E_EXIST` if any page is already mapped, and roll back completely on `E_NOMEM`.
> - `int unmap_range(as, va, size)` — returns the number of pages removed; frees owned frames and **every page-table page that becomes empty** (root excepted).
> - `int protect_range(as, va, size, prot)` — `E_NOENT` if any page in the range is unmapped.
> - All three batch their work in a gather. When the gather finishes, it must invalidate **before** it returns any frame to the allocator. Use per-page invalidation for at most `ceiling` pages (default 33) and an ASID-wide flush above that (`tlbi_all` if a global page is involved). Use walk-cache invalidation when page tables were freed. Do not invalidate for new mappings or for pure permission increases; handle the resulting stale-permission fault as a spurious fault.
> - `as_destroy(as)` (one ASID flush, then free everything, then release the ASID) and `migrate_page(as, va, &new_pa)` using break-before-make.
>
> Example: after 40 pages are mapped and touched, `unmap_range(33 pages)` issues 33 leaf invalidations; unmapping the remaining 7 frees the L3/L2/L1 tables and issues 7 non-leaf invalidations; the frame count returns to its value before the map.
>
> Follow-ups: why 33? Why must the free come after the flush, even on a single CPU? What changes on SMP (IPIs vs broadcast TLBI)? What is break-before-make and when is it required?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter      # 11/69 passed, exit 1 (0.01 s)
```

처음 실행 결과 (실제 출력 일부):

```text
-- Part A: tables on demand, map/unmap/protect, reclaim to baseline, ENOMEM rollback
FAIL starter.c:476  map_range(&a, 0x400000, PA_ANON, 3 * PG, RWU, 0) == E_OK && M.nfree == base - 6
FAIL starter.c:478  w0.fault == F_NONE && w1.fault == F_NONE && w2.fault == F_NONE && w0.pa != w1.pa && w1.pa != w2.pa && w0.pa != w2.pa
…
-- Part A: random map/unmap/protect/access vs shadow model (3000 ops, 12 chunks, seed 2026)
  ops=3000 accesses=21540 unmapped_pages=0 E_EXIST=0 E_NOENT=624 E_NOMEM=0 spurious=0 reclaimed_states=3000 bad=1554
FAIL starter.c:608  bad == 0 && nomem > 0 && M.spurious > 0 && empty_states > 0
-- Part B: flush policy (ceiling 33), ASID vs ALL, spurious fault, injected bugs
FAIL starter.c:642  unmap_range(&a, 0x400000, 33 * PG) == 33 && M.nlog == 33 && log_count(TLBI_VA_LEAF) == 33
…
FAIL starter.c:685  bug.early > 0 && bug.r.fault == F_NONE && bug.r.tlb_hit && peek(bug.r.pa & ~7ull) == SECRET && bug.audit > 0
…
FAIL starter.c:766  M.nlog == 1 && M.log[0].kind == TLBI_ALL && M.nfree == base
11/69 passed
```

TODO 는 A-1 … A-5, B-1, B-2, C-1 … C-3 이다. 스텁의 동작은 이렇다. `map_range` 는 `E_NOSYS`, `unmap_range` 는 0 을 돌려준다. `tlb_finish` 는 **무효화 없이 프레임만 반납**하는데, 이것이 바로 버그 ① 이다. `as_destroy` 는 ASID 만 놓아준다. 지나가는 11 개는 "아무것도 매핑하지 않았으니 낡은 것도 없다" 는 공허한 참(vacuous truth)이다. 검사기(`tlb_audit`, `early_free`, `live_errors`)와 버그 주입 경로(`gather_done`)는 주어진 코드라서 손대지 않는다.

## 4. 힌트

<details><summary>힌트 1 — live 카운트는 "부모" 에서 센다</summary>

테이블 T 의 `live[T]` = T 안의 유효 엔트리 수다. `walk_alloc` 이 새 테이블을 레벨 lv 엔트리에 붙이면 **그 엔트리를 담은 테이블**(부모)의 live 가 1 늘어난다. leaf 를 설치하면 L3 테이블의 live 가 는다. 검증은 `live_errors()` 가 테이블마다 512 엔트리를 세어 비교한다(ARCH-A01 의 `table_empty` 스캔이 이것이다). 카운트를 쓰면 "비었나?" 를 O(1) 에 답할 수 있다.
</details>

<details><summary>힌트 2 — prune 은 walk_path 의 결과를 거꾸로 따라간다</summary>

`walk_path` 가 멈춘 레벨이 lv 이면 `tbl[0..lv]` 가 존재한다. `t = lv` 부터 1 까지 내려가며 `live[tbl[t]] == 0` 이면 `ea[t-1]`(부모 엔트리)를 지우고, `live[tbl[t-1]]--`, `gather_table`. 0 이 아니면 그 위는 볼 필요가 없다. map 이 L3 테이블을 만든 뒤 데이터 프레임에서 실패하면, 그 빈 테이블도 같은 함수로 치운다.
</details>

<details><summary>힌트 3 — 없는 하위 트리는 크기만큼 건너뛴다</summary>

`unmap_range(as, 0, 1 GB)` 를 페이지마다 돌면 262144 번 워크한다. `walk_path` 가 lv < 3 에서 멈췄다면 그 레벨 엔트리가 덮는 `level_size(lv)` 전체가 비어 있다. `va = (va & ~(sz-1)) + sz` 로 넘어간다. 주어진 `count_mapped` 가 같은 루프다.
</details>

<details><summary>힌트 4 — 정책 표를 위에서부터</summary>

`fullmm` → `npages > ceiling` → `npages > 0` → `ntables > 0` 순서. `ASID` 무효화는 **비전역** 엔트리만 지우므로 `g->global` 이면 `ALL`. 페이지 단위일 때 종류는 `g->freed_tables ? tlbi_va : tlbi_va_leaf`. 이번에 비게 된 테이블에는 반드시 이번에 지운 leaf 가 하나 이상 있다. 그래서 leaf VA 들만 무효화해도 테이블 쪽 walk cache 까지 덮인다. 예외는 map 롤백에서 생긴 "한 번도 안 쓴 빈 테이블" 뿐이고, 그것이 `tva[]` 경로다. 그리고 **반납은 맨 마지막**.
</details>

<details><summary>힌트 5 — spurious 판정은 소프트웨어 워크로</summary>

하드웨어(TLB)가 PERMISSION 을 냈다. 폴트 핸들러는 PTE 를 직접 읽는다(`walk`). PTE 가 이미 허용하면 TLB 만 낡은 것이다. 그 VA 만 무효화(`tlbi_va_leaf`)하고 `mmu_access` 를 다시 부르면 미스 → 워크 → 새 권한으로 채워진다. PTE 도 거부하면 진짜 PERMISSION 폴트다.
</details>

<details><summary>힌트 6 — migrate 에서 복사는 언제?</summary>

break 직후에 복사하면 안 된다. 무효화 **전**이라 다른 CPU 가 옛 TLB 엔트리로 옛 프레임에 계속 쓸 수 있고, 그 쓰기는 복사본에 없다. break → 무효화 → 복사 → make → 반납. 반납은 이미 무효화한 뒤라 `free_frame` 을 바로 불러도 된다. pending 이 풀려 있기 때문이다.
</details>

## 5. 풀이

### Part A — 테이블 즉석 할당과 프레임 계산

`map_range(0x400000, 3 페이지)` 를 빈 주소 공간에 하면 루트(L0) 아래에 L1, L2, L3 테이블이 하나씩 생기고 데이터 프레임이 3 개 생긴다. 프레임은 6 개 줄어든다(테스트 `M.nfree == base - 6`). 이어서 `0x5FF000` 부터 2 페이지를 매핑하면 `0x5FF000` 은 기존 L3 에 들어가고 `0x600000` 은 VA[29:21] 이 3 이 되어 **새 L3** 이 필요하다. 그래서 테이블 1 + 데이터 2 = 3 프레임이 더 든다(`base - 9`).

| VA | L0 idx | L1 idx | L2 idx | L3 idx | 새로 할당 |
|---|---|---|---|---|---|
| 0x400000 | 0 | 0 | 2 | 0 | L1, L2, L3, 데이터 |
| 0x401000 | 0 | 0 | 2 | 1 | 데이터 |
| 0x5FF000 | 0 | 0 | 2 | 511 | 데이터 |
| 0x600000 | 0 | 0 | 3 | 0 | L3, 데이터 |
| 0x70000000 (MMIO) | 0 | 1 | 384 | 0 | L2, L3 (데이터 0) |

일반화하면 페이지 집합 S 에 필요한 테이블 수는 `1 + |{va>>39}| + |{va>>30}| + |{va>>21}|` 이다(연습 2). 스트레스 테스트는 매 연산 뒤 실제 테이블 프레임 수가 이 공식과 **정확히** 같은지 본다. 하나라도 덜 회수하면 다음 검사에서 바로 걸린다.

### Part A — 회수: 카운트 vs 스캔, 그리고 아래에서 위로

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">unmap 0x400000 (마지막 페이지): prune_path 가 아래에서 위로</text>
<rect x="20" y="40" width="130" height="50" rx="4" class="f-acc" stroke="currentColor"/><text x="30" y="60" fill="currentColor">L0 (루트)</text><text x="30" y="78" fill="currentColor" font-size="12">live 1 → 0</text>
<rect x="190" y="40" width="130" height="50" rx="4" class="f-warn" stroke="currentColor"/><text x="200" y="60" fill="currentColor">L1</text><text x="200" y="78" fill="currentColor" font-size="12">live 1 → 0 → 반납</text>
<rect x="360" y="40" width="130" height="50" rx="4" class="f-warn" stroke="currentColor"/><text x="370" y="60" fill="currentColor">L2</text><text x="370" y="78" fill="currentColor" font-size="12">live 1 → 0 → 반납</text>
<rect x="530" y="40" width="130" height="50" rx="4" class="f-warn" stroke="currentColor"/><text x="540" y="60" fill="currentColor">L3</text><text x="540" y="78" fill="currentColor" font-size="12">live 1 → 0 → 반납</text>
<path d="M150 65 L188 65" stroke="currentColor" fill="none"/><path d="M181 60 L189 65 L181 70" stroke="currentColor" fill="none"/>
<path d="M320 65 L358 65" stroke="currentColor" fill="none"/><path d="M351 60 L359 65 L351 70" stroke="currentColor" fill="none"/>
<path d="M490 65 L528 65" stroke="currentColor" fill="none"/><path d="M521 60 L529 65 L521 70" stroke="currentColor" fill="none"/>
<text x="540" y="115" fill="currentColor" font-size="12">① PTE 0, live[L3]--</text>
<text x="370" y="135" fill="currentColor" font-size="12">② live[L3]==0 → L2 엔트리 0, live[L2]--</text>
<text x="200" y="155" fill="currentColor" font-size="12">③ live[L2]==0 → L1 엔트리 0, live[L1]--</text>
<text x="30" y="175" fill="currentColor" font-size="12">④ live[L1]==0 → L0 엔트리 0, live[L0]-- (루트는 남김)</text>
<rect x="10" y="195" width="720" height="95" rx="4" class="f-mut" stroke="currentColor"/>
<text x="20" y="215" fill="currentColor" font-size="12">반납 순서 (gather.frames): 데이터 → L3 → L2 → L1, 무효화가 끝난 뒤 free_frame.</text>
<text x="20" y="235" fill="currentColor" font-size="12">LIFO 라 다음 할당은 L1 부터 나간다. 그래서 버그 ①·③ 시나리오에서</text>
<text x="20" y="255" fill="currentColor" font-size="12">"b 가 4 페이지를 매핑하면 a 의 옛 프레임 4 개를 전부 데이터로 받는다" 가</text>
<text x="20" y="275" fill="currentColor" font-size="12">매번 똑같이 일어난다 (결정적).</text>
</svg>
```

빈 테이블을 알아내는 방법은 두 가지다. ① **스캔**: 엔트리를 지울 때마다 그 테이블 512 개를 훑는다(ARCH-A01 `table_empty`). 메모리는 추가로 들지 않지만 unmap 한 번이 최대 512 × 3 읽기다. ② **카운트**: 테이블마다 유효 엔트리 수를 따로 둔다. 이 레슨은 프레임 번호로 인덱싱한 `uint16_t live[]` 를 쓴다(프레임당 2 B). O(1) 이지만 **모든 쓰기 경로**(walk_alloc, map, unmap, prune, teardown)가 카운트를 맞춰야 한다. 하나라도 빠뜨리면 영영 안 비거나 너무 일찍 풀린다. `live_errors()` 가 매 스트레스 연산 뒤 스캔으로 대조하는 이유다.

Linux 는 둘 다 아니다. munmap 이 VMA 를 지운 뒤 `free_pgtables()` 가 "이 VMA 범위에 **완전히** 들어가는 테이블" 을 이웃 VMA 경계(floor/ceiling) 기준으로 한꺼번에 푼다. 부분적으로 빈 테이블은 남는다(기억 기준, 세부는 버전마다 다를 수 있음). 펌웨어 RTOS 는 대개 매핑이 고정이라 회수를 아예 안 한다.

### Part A — unmap_range 와 protect_range 의 한 바퀴

`unmap_range` 의 루프 한 바퀴는 이렇다.

1. `walk_path(va)` 가 lv < 3 에서 멈추면 그 레벨 엔트리가 덮는 `level_size(lv)` 전체가 비어 있다. 그만큼 건너뛴다(1 GB 빈 범위 unmap 이 워크 몇 번으로 끝난다. 테스트 `unmap_range(&a, 0x400000, 1ull << 30) == 0 && M.nlog == 0`).
2. L3 슬롯이 유효하면 `set_desc(…, 0)` 으로 지운다. 이 순간 검사기가 옛 프레임을 pending 으로 표시한다. 그리고 `live[L3]--`.
3. `gather_page(va, G?)` 로 무효화 대상에 넣고, `SW` 면 `gather_data` 로 반납 대상에 넣는다. MMIO 는 소유가 아니므로 반납하지 않는다.
4. `prune_path(va)` 로 비게 된 테이블을 위로 회수해 `gather_table` 에 넣는다.
5. 루프가 끝나면 `gather_done` → `tlb_finish` 를 부른다. 무효화가 먼저고 반납은 그다음이다.

`protect_range` 는 회수가 없는 대신 "이 페이지를 무효화해야 하나" 를 페이지마다 따진다. `old & ~new & (R|W|X)` 가 0 이 아니면(잃은 비트가 있으면) gather 에 넣는다. 아무것도 안 바뀐 페이지는 쓰지도 않는다. 그래서 RW 를 다시 RW 로 바꾸면 무효화 0 번이다(테스트 `M.nlog == 2` 유지).

### Part A — 롤백과 오류 의미

`E_NOMEM` 은 두 지점에서 난다. (1) 데이터 프레임 할당 실패: `budget = 5` 면 L1·L2·L3·데이터·데이터 다음 세 번째 페이지에서 실패한다. 이미 설치한 2 페이지는 `unmap_range` 로, 실패한 페이지를 위해 만든 테이블은 `prune_path` 로 치운다. (2) 테이블 할당 실패: `budget = 2` 면 L1·L2 는 생기고 L3 에서 실패한다. 설치한 페이지는 0 이고 `prune_path` 가 L2·L1 을 회수한다. 두 경우 모두 `M.nfree == base` 다. 롤백의 unmap 은 무효화를 하는데, 아직 아무도 접근하지 않았으니 엄밀히는 필요 없다. 그래도 Linux 처럼 일반 경로를 재사용하는 편이 단순하다. 스트레스 테스트의 "map 은 무효화 0 번" 검사도 그래서 성공한 map 에만 건다.

`protect_range` 는 범위에 구멍이 있으면 **아무것도 바꾸지 않고** `E_NOENT` 를 돌려준다. Linux `mprotect(2)` 는 매핑 안 된 페이지가 있으면 `ENOMEM` 이다(man7.org mprotect(2)). 다만 그 경우 앞부분이 이미 바뀌었을 수 있다고 man page 가 경고하는 것으로 기억한다. 이 모델은 미리 검사(`count_mapped`)해서 원자적으로 만들었다.

### Part B — 정책: 왜 33, 왜 ASID, 왜 ALL

| 상황 | 이 레슨 | Linux x86 | Linux arm64 (master, 2026-11-11 확인) |
|---|---|---|---|
| user 범위, n 페이지 | n ≤ 33 → VA 별, 아니면 ASID | `(end-start)>>shift > tlb_single_page_flush_ceiling(33)` 이면 전체 | `pages >= MAX_DVM_OPS(=PTRS_PER_PTE=512)` 이면 `flush_tlb_mm` (range TLBI 없을 때) |
| 테이블을 풀었나 | `VA`(walk cache 포함) vs `VA_LEAF` | `freed_tables` 가 flush 정보에 들어감 | `freed_tables` 면 `vae1is`, 아니면 `TLBF_NOWALKCACHE` → `vale1is` |
| 전역(커널) 범위 | 많으면 `ALL` | `flush_tlb_kernel_range` 도 같은 ceiling | 넘으면 `flush_tlb_all()` |
| 주소 공간 해체 | `fullmm` → ASID 한 번 | `fullmm` | `fullmm` 이면 walk cache 만 신경 씀(ASID 재할당 시 전체 무효화) |

x86 `arch/x86/mm/tlb.c` 의 주석(2026-11-11 torvalds/master 에서 직접 읽음)은 이렇다. 33 은 "할당의 대다수(95 % 이상)를 덮을 만큼 크고", 단일 무효화가 "약 100 ns" 라 최대 오버헤드가 약 3,000 ns 로 묶인다. 즉 **VA 별 무효화 비용 × n** 과 **전체 무효화 비용 + 남은 엔트리를 다시 채우는 비용**의 손익분기를 경험적으로 정한 값이다. arm64 의 512 는 성능 기준이 아니다. 주석에 따르면 "큰 범위에서 soft lock-up 을 피하려는" 상한이다. 아키텍처마다 기준이 다르다는 것 자체가 면접 포인트다(연습 3).

`demo_flush_policy.c` 가 그 손익을 결정적으로 보여 준다. TLB 64 엔트리를 a 48 + b 16 으로 채우고 a 의 앞 k 페이지를 unmap 한 뒤 남은 것을 다시 만진다.

<!-- code: demo_flush_policy.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_flush_policy.c -o demo && ./demo     # solution.c 와 같은 폴더에서
```

```text
TLB = 64 entries: a(ASID 1) 48 pages + b(ASID 2) 16 pages, then unmap the first k pages of a
ceiling   k  tlbi ops (kind)      a refill misses  b refill misses  desc reads  audit
      0   1    1 (ASID   )                     47                0          50      0
      0   8    1 (ASID   )                     40                0          43      0
      0  32    1 (ASID   )                     16                0          19      0
      0  33    1 (ASID   )                     15                0          18      0
      0  34    1 (ASID   )                     14                0          17      0
      0  47    1 (ASID   )                      1                0           4      0
      0  48    1 (ASID   )                      0                0           0      0
     33   1    1 (VA_LEAF)                      0                0           0      0
     33   8    8 (VA_LEAF)                      0                0           0      0
     33  32   32 (VA_LEAF)                      0                0           0      0
     33  33   33 (VA_LEAF)                      0                0           0      0
     33  34    1 (ASID   )                     14                0          17      0
     33  47    1 (ASID   )                      1                0           4      0
     33  48    1 (ASID   )                      0                0           0      0
     64   1    1 (VA_LEAF)                      0                0           0      0
     64   8    8 (VA_LEAF)                      0                0           0      0
     64  32   32 (VA_LEAF)                      0                0           0      0
     64  33   33 (VA_LEAF)                      0                0           0      0
     64  34   34 (VA_LEAF)                      0                0           0      0
     64  47   47 (VA_LEAF)                      0                0           0      0
     64  48   48 (VA     )                      0                0           0      0
```

(결정적 시뮬레이션이라 실행마다 같다.) 해석은 이렇다.
- **ceiling 0**(늘 ASID): 명령은 1 개지만 1 페이지만 지워도 남은 47 개를 다시 채운다(미스 47, 디스크립터 읽기 50 = 첫 미스 전체 워크 4 + walk cache 히트 46). 이 비용은 **unmap 을 한 쪽이 아니라 나중에 그 프로세스가** 낸다.
- **ceiling 64**(늘 VA): 다시 채울 일은 0 이지만 명령이 47 개다. 실제 하드웨어에서는 무효화 하나하나가 비싸고, SMP 에서는 브로드캐스트 트래픽이나 IPI 까지 붙는다.
- b(다른 ASID)의 미스는 어느 정책이든 0 이다. **ASID 무효화는 남의 주소 공간을 건드리지 않는다**(Part B 테스트 `tlb_count(2) == 4`). 전역 범위의 `ALL` 은 b 까지 날린다(`tlb_count(2) == 0`, "전역 플러시의 대가").
- k = 48 은 테이블까지 풀리는 경우라 종류가 `VA` 다. 같은 페이지 수라도 명령의 종류가 달라진다.

Part B 테스트가 남긴 무효화 로그를 한 표로 모으면 이렇다(전부 실제 CHECK 가 확인한 값).

| 연산 | 페이지 | 테이블 반납? | 로그 |
|---|---|---|---|
| unmap (40 중 앞 33) | 33 | 아니오 | `VA_LEAF` × 33 |
| unmap (나머지 7) | 7 | L3·L2·L1 | `VA` × 7 |
| unmap 34 | 34 | 예 | `ASID` × 1 |
| unmap 40 (b 는 4 엔트리 보유) | 40 | 예 | `ASID` × 1, b 엔트리 4 개 생존 |
| unmap 전역 40 | 40 | 예 | `ALL` × 1, b 엔트리 0 개 |
| protect 8 페이지 RW→R | 8 | — | `VA_LEAF` × 8 |
| protect 8 페이지 R→RW | 8 | — | 없음 (첫 쓰기에서 spurious → `VA_LEAF` × 1) |
| protect 1 페이지 RW→RX / RW→0 / RW→RW | 1 | — | 1 / 1 / 0 |

### Part B — 권한 확대는 무효화하지 않는다 (spurious fault)

무효화가 필요한지는 "**이전에 허용되던 접근 중 하나라도 이제 거부되나?**" 하나로 판단한다(연습 1 이 user/kernel × R/W/X 6 가지 능력으로 계산한다).
- R → RW: 잃은 능력 없음 → 무효화 0. 낡은 R 엔트리로 쓰기를 하면 PERMISSION 폴트가 나고, 핸들러가 PTE 를 보니 W 가 있다 → spurious. 테스트 출력에서 첫 쓰기는 `spurious == sp + 1` 이고 무효화 로그에 `VA_LEAF` 가 1 개다. 두 번째 쓰기는 TLB 히트다.
- RW → RX: W 를 잃었다 → 무효화 필요(X 를 얻었어도 상관없다).
- kernel 전용 RX 페이지에 U 를 더하기: user 는 능력을 얻지만 kernel 은 **X 를 잃는다**(SMEP/PXN 규칙, ARCH-A01). 그래서 "U 를 더하는 것 = 확대" 가 아니다. 이 레슨의 `protect_range` 는 그래서 U·G 를 바꾸지 못하게 막았다.

왜 이렇게 하나? 권한 확대는 흔하고(COW 해제 후 쓰기 허용, `mprotect(PROT_READ|PROT_WRITE)`, 스택 확장), 확대 뒤 그 페이지를 실제로 다시 쓰는 경우는 일부다. 모든 확대에 무효화를 걸면 SMP 에서는 매번 브로드캐스트가 나간다. 확대를 게으르게 처리하면 비용은 "실제로 낡은 엔트리를 밟은 CPU 의 폴트 한 번" 이고, 그 CPU 에서 **로컬로만** 무효화한다. arm64 `pgtable.h`(master, 직접 읽음)의 `flush_tlb_fix_spurious_fault` 가 정확히 `__flush_tlb_page(..., TLBF_NOBROADCAST | TLBF_NONOTIFY)` 이다. 주석에는 쓰기 보호 폴트 처리에서 로컬 무효화를 쓰므로 "stale read-only TLB 엔트리가 있으면 spurious fault 가 난다" 고 적혀 있다. x86 의 정의는 확인하지 않았다.

### Part B — 무효화 → 반납, 이 순서가 전부다

버그 3종을 하네스에서 주입한 실제 출력이다.

```text
  skip-TLBI: fault=NONE tlb_hit=1 read=0x5ec12e7da7a early_free=4 audit=2
  leaf-only: old L3 table frame is now b's DATA, a reads pa=0x40005000 (b's page) via stale walk cache
```

1. **무효화 생략**: a 가 unmap 한 데이터 프레임과 L3·L2·L1 은 반납되고, b 가 4 페이지를 매핑하면서 그 4 프레임을 데이터로 받아 비밀(`0x5EC12E7DA7A`)을 쓴다. a 가 같은 VA 를 읽으면 **TLB 히트**로 `fault=NONE` 이 나오고 b 의 비밀이 읽힌다. 새 워크는 TRANSLATION 이다. 검사기 세 가지가 모두 잡는다. `early_free=4`(pending 인 채로 반납), `audit=2`(TLB 1 + walk cache 1), 그리고 읽은 값.
2. **반납 먼저, 무효화 나중**: 같은 함수 안에서 순서만 바꿨다. 끝나고 보면 TLB 는 깨끗하고(`audit == 0`), a 의 접근도 TRANSLATION 으로 정상이다. 그런데 `early_free > 0` 이다. 단일 코어에서는 둘 사이에 아무도 끼어들지 않아 결과가 같지만, SMP 에서는 반납과 무효화 사이에 다른 CPU 가 그 프레임을 할당받아 쓰고, 또 다른 CPU 는 낡은 TLB 로 그 프레임을 계속 읽는다. **최종 상태 검사로는 안 보이는 버그**라서 순서 검사기(pending)가 필요하다. Linux 의 `mmu_gather` 가 페이지를 모아 두었다가 `tlb_flush_mmu` 에서 **무효화 후** 해제하는 이유가 이것이다(함수 이름은 기억 기준).
3. **테이블을 풀었는데 leaf 만**: TLB 는 비웠지만 walk cache 엔트리 `(ASID 1, VA[47:21]) → 옛 L3` 가 남는다. 그 프레임이 b 의 데이터 페이지가 되고, b 가 평범하게 쓴 8 바이트(`V|T|R|U|b 의 페이지`)가 a 에게는 **디스크립터**로 읽힌다. TLB 미스(`!tlb_hit`) → walk cache 히트 → 가짜 디스크립터 → b 의 페이지. 공격자가 자기 데이터로 남의 페이지 테이블을 만드는 셈이다. arm64 가 `freed_tables` 일 때 `vale1is` 대신 `vae1is` 를 쓰는 이유다.

### Part C — 드라이버: ioremap / DMA 버퍼 넘기기

- **ioremap/iounmap**: MMIO 는 RAM 이 아니라 프레임 할당기 밖의 PA 다(`0x09000000`, `SW` 없음). `iounmap` 에서 데이터 프레임은 반납하지 않지만 무효화는 똑같이 필요하다. 해제한 VA 에 다른 장치나 RAM 이 매핑되면 낡은 엔트리가 엉뚱한 레지스터를 건드린다. 테스트: 커널 AS(ASID 0)에 전역 RW, user 접근은 PERMISSION, 해제 시 `VA` 4 번(테이블 반납 포함), `count_state(FR_DATA) == 0`.
- **DMA 버퍼 넘기기**: 장치가 읽는 동안 CPU 가 쓰지 못하게 RW → R 로 바꾸는 패턴이다(디버그 빌드의 버퍼 보호, 보안 경계). 40 페이지 &gt; 33 이라 `ASID` 1 번. 돌려받을 때(R → RW)는 0 번. 캐시 일관성(clean/invalidate)은 **데이터 캐시** 문제로 TLB 와 별개다(ARCH-M12). IOMMU/SMMU 쪽 무효화(장치 TLB)는 PCIE-M15, SOCX-M12 에서 다룬다.

### Part C — break-before-make (ARM) 와 페이지 이동

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">migrate_page: 유효 → 유효 로 OA 를 바로 바꾸지 않는다</text>
<rect x="10" y="40" width="130" height="50" rx="4" class="f-ok" stroke="currentColor"/><text x="20" y="60" fill="currentColor">① 새 프레임</text><text x="20" y="78" fill="currentColor" font-size="12">실패 → 그대로</text>
<rect x="155" y="40" width="130" height="50" rx="4" class="f-warn" stroke="currentColor"/><text x="165" y="60" fill="currentColor">② break</text><text x="165" y="78" fill="currentColor" font-size="12">PTE = 0</text>
<rect x="300" y="40" width="130" height="50" rx="4" class="f-warn" stroke="currentColor"/><text x="310" y="60" fill="currentColor">②' TLBI VA</text><text x="310" y="78" fill="currentColor" font-size="12">(+ SMP: 전 CPU)</text>
<rect x="445" y="40" width="130" height="50" rx="4" class="f-acc" stroke="currentColor"/><text x="455" y="60" fill="currentColor">③ 복사</text><text x="455" y="78" fill="currentColor" font-size="12">아무도 못 씀</text>
<rect x="590" y="40" width="140" height="50" rx="4" class="f-ok" stroke="currentColor"/><text x="600" y="60" fill="currentColor">④ make + ⑤ 반납</text><text x="600" y="78" fill="currentColor" font-size="12">PTE = 새 OA</text>
<path d="M140 65 L153 65" stroke="currentColor" fill="none"/><path d="M285 65 L298 65" stroke="currentColor" fill="none"/>
<path d="M430 65 L443 65" stroke="currentColor" fill="none"/><path d="M575 65 L588 65" stroke="currentColor" fill="none"/>
<text x="10" y="120" fill="currentColor" font-size="12">②~④ 사이에 접근하면 TRANSLATION 폴트 → Linux 는 migration entry 를 보고 끝날 때까지 기다린다 (기억 기준).</text>
<text x="10" y="140" fill="currentColor" font-size="12">유효→유효 직접 교체: 옛·새 TLB 엔트리가 함께 있을 수 있다.</text>
<text x="10" y="158" fill="currentColor" font-size="12">ARM 은 이것을 "TLB conflict" 같은 예측 불가 동작으로 둔다.</text>
<text x="10" y="180" fill="currentColor" font-size="12">검사기: set_desc 가 valid→valid OA 변경을 보면 bbm_viol++ (연습 4 는 이벤트 로그로 같은 검사).</text>
<rect x="10" y="195" width="720" height="55" rx="4" class="f-mut" stroke="currentColor"/>
<text x="20" y="217" fill="currentColor" font-size="12">테스트 결과: newpa ≠ oldpa, 내용 0xC0FFEE 복사, 무효화 로그 = [VA_LEAF] 1 개,</text>
<text x="20" y="237" fill="currentColor" font-size="12">bbm_viol 0, early_free 0, 프레임 수 그대로</text>
</svg>
```

ARM ARM(DDI 0487)의 VMSAv8-64 장에는 "Translation table entry 를 바꿀 때 break-before-make 가 필요한 경우" 가 정리되어 있다. 출력 주소, 블록 크기(블록 ↔ 테이블), 메모리 타입 등을 바꿀 때다. 이 레슨에서는 정확한 절 번호를 확인하지 못했다. 판마다 D5/D8 장 번호가 달라서다. Linux arm64 `pgtable.h` 주석은 직접 읽었다: "arm64 mandates break-before-make: a cleared kernel PTE must have its TLB invalidated before a different page is installed in its place." ARCH-A01 의 블록 split(블록 → 테이블) 도 같은 규칙이다. x86 은 BBM 을 아키텍처로 강제하지 않는 것으로 알지만, 커널은 PA 를 바꿀 때 어차피 무효화한다.

### Part C — 프로세스 종료와 ASID 재사용

`as_destroy` 는 트리 전체를 재귀로 해체한다. 엔트리를 지우는 이유는 검사기가 pending 을 추적하게 하기 위해서다. Linux 의 `fullmm` 경로는 엔트리를 일일이 무효화하지 않는다. 그다음 **ASID 무효화 1 번**(전역 페이지가 있었으면 `ALL`), 프레임과 루트를 반납하고, 마지막에 ASID 를 놓아준다. 테스트 결과는 로그 = `[ASID asid=1]` 하나, `M.nfree == base`, `tlb_count(1) == 0`, `wc_count(1) == 0` 이다. 이어서 `as_create(&a2, 1)` 로 **같은 ASID** 를 새 프로세스에 주고 옛 VA 를 읽으면 TRANSLATION 이다.

ASID 를 무효화 **전에** 놓아주면 새 프로세스가 같은 ASID 로 옛 프로세스의 엔트리에 히트한다. ARCH-A02 Part B 의 두 번째 버그가 이것이다. Linux arm64 는 한 걸음 더 나간다. `tlb_flush()` 의 `fullmm` 주석(직접 읽음)대로 "ASID 할당기가 TLB 전체를 무효화하지 않고서는 우리 ASID 를 다시 주지 않으므로" 종료 시 walk cache 만 신경 쓴다. ASID 세대(generation) 방식이다(ARCH-A02 Part C).

### Part C — SMP: TLB shootdown

이 모델은 단일 코어다. 실제 SMP 에서 달라지는 점은 이렇다.
- **x86**: 다른 CPU 의 TLB 를 지우는 명령이 (전통적으로) 없다. 그래서 `mm_cpumask` 의 다른 CPU 에 **IPI** 를 보내고, 각 CPU 가 인터럽트 핸들러에서 로컬 `INVLPG`/CR3 재적재를 한 뒤 응답할 때까지 기다린다. 이것이 TLB shootdown 이다. 비용은 IPI 왕복 × CPU 수라서 batching(gather 로 모아 한 번에)이 중요하다(연습 5: 1 페이지 munmap 16 번 = IPI 32 개, 16 페이지 한 번 = 2 개). 최근 커널에 AMD `INVLPGB`(브로드캐스트 무효화) 지원이 들어간 것으로 아는데, 확인하지 않았다.
- **arm64**: `TLBI ...IS`(Inner Shareable) 가 하드웨어로 같은 shareability 도메인의 모든 코어에 전달된다. IPI 가 없고, `DSB ISH` 로 완료를 기다린다. pgtable.h 주석 "The broadcast TLBI is an instruction, not an IPI" (직접 읽음). 대신 범위가 크면 DVM 메시지가 많아져서 512 상한이 생겼다.
- **RISC-V**: `sfence.vma` 는 로컬 명령이라 원격 무효화는 SBI 호출이나 IPI 로 한다(RISCV-A01, RISCV-A02).
- 어느 쪽이든 순서는 같다. **PTE 변경 → (모든 CPU에서) 무효화 완료 → 프레임 반납.** 이 레슨의 pending 검사기는 그 마지막 화살표만 단일 코어로 줄여 놓은 것이다.

### Part C — 펌웨어: MMU 가 있는 RTOS, MPU 만 있는 MCU

- Cortex-A/R 에서 MMU 를 켠 RTOS(Zephyr 의 user mode, 사내 RTOS)는 보통 매핑을 부팅 때 정적으로 만든다(ARM-A03). 그래도 태스크 생성·종료, 공유 메모리 창, DMA 버퍼 노출에서 이 레슨과 같은 map/unmap 이 일어난다. 실수 패턴도 같다. 태스크 종료 뒤 ASID 를 재사용하면서 무효화를 빠뜨리거나, 블록 매핑을 페이지로 쪼개면서 BBM 을 빼먹는다. Zephyr 의 MMU 코드 세부는 확인하지 않았다.
- Cortex-M 은 MMU·TLB 가 없고 **MPU** 다(ARCH-M15, BM-A08). 영역 레지스터를 바꾸는 것이 곧 즉시 반영되는 "매핑 변경" 이라 무효화는 없다. 대신 `DSB; ISB` 로 이후 접근이 새 설정을 보게 해야 한다. "권한을 줄이면 무효화 전에 재사용 금지" 는 MPU 에서는 "줄이기 → 배리어 → 재사용" 으로 바뀐다.
- 리눅스 드라이버에서 직접 TLB 를 만질 일은 드물다. `vunmap`/`iounmap`/`set_memory_ro` 같은 API 가 이 레슨의 gather 와 정책을 대신한다. 그래도 "왜 `set_memory_*` 가 비싼가(커널 전역 매핑 → 큰 범위면 전부 무효화)", "왜 매핑 해제 직후 그 물리 페이지를 바로 재사용해도 되나" 를 설명할 수 있어야 한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 69/69 passed, exit 0 (5번 반복, 출력 md5 동일, 다른 cwd 에서도 동일)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o s_asan && ./s_asan   # 69/69, 보고 0
```

스트레스 줄(실제 출력):

```text
  ops=3000 accesses=21540 unmapped_pages=5099 E_EXIST=497 E_NOENT=515 E_NOMEM=40 spurious=18 reclaimed_states=1634 bad=0
```

3000 연산 중 E_NOMEM 롤백 40 번, spurious fault 18 번이 나왔고, 1634 번은 어떤 청크가 비어 테이블이 회수된 상태였다. 매 연산 뒤 `tlb_audit()`, `early_free`, `bbm_viol`, `live_errors()`, 그림자 비교 768 페이지, 테이블 수 공식을 모두 확인했다. 실행 시간은 약 0.02 s(user)다. 테스트가 무엇을 잡는지 확인하려고 정답을 일부러 망가뜨려 봤다(작업 폴더 /tmp/OS-M01_work). walk cache 무효화를 빼면 `bad=2993`, 회수를 빼면 1589, 권한 축소 시 무효화를 빼면 1048, L3 live-- 를 빼면 4580 이다. spurious 처리를 PERMISSION 으로 바꾸면 38 이 나오고 B 테스트 3 개가 실패한다. 전역 페이지에 ASID 무효화를 쓰면 B·C 테스트 4 개가 실패한다(스트레스는 전역을 안 써서 통과).

## 7. 자주 하는 실수

**1. 반납 먼저, 무효화 나중.** `free_frame(f); tlbi_va(...)` 로 쓰면 단일 코어 테스트의 최종 상태는 깨끗하다(버그 ② 실측: `audit == 0`). SMP 에서는 그 틈에 프레임이 재사용된다. → gather 에 모아 두고 `tlb_finish` 의 **마지막 줄**에서 반납한다.

**2. 테이블을 풀었는데 leaf 만 무효화.** TLB 는 비어도 walk cache 가 해제된 테이블을 계속 따라간다(버그 ③ 실측: b 의 데이터를 디스크립터로 읽음). → `freed_tables` 면 `TLBI_VA`(VAE). 테이블을 안 풀었으면 `VA_LEAF` 로 walk cache 를 살려 두는 것이 최적화다.

**3. 전역 페이지에 ASID 무효화.** `TLBI_ASID` 는 G 엔트리를 못 지운다. 커널 매핑(ioremap, vmalloc)은 G 다. → `g->global` 이면 `ALL`(또는 VA 별. VA 기반 무효화는 G 엔트리에도 맞는다).

**4. 권한 확대에도 무효화 / 축소에 무효화 누락.** 확대에 무효화하면 정확하지만 낭비고, 축소에 빠뜨리면 보안 버그다(W 를 뺐는데 낡은 엔트리로 계속 씀). → "잃은 능력이 있나" 로 판단(연습 1). RX → RW 는 X 를 잃었으니 축소다.

**5. live 카운트를 한 경로에서만 갱신.** map 에서 `live++` 는 했는데 롤백·teardown·prune 의 부모 `live--` 를 빼먹으면, 테이블이 영원히 안 비거나 **아직 쓰는 테이블을 반납**한다(뮤턴트 실측 `bad=4580`). → 엔트리를 쓰는 모든 자리에서 짝을 맞추고 스캔(`live_errors`)으로 대조한다.

**6. ASID 를 먼저 놓아준다.** `as_by_asid[asid] = NULL` 뒤에 무효화하면, 그 사이 같은 ASID 를 받은 새 프로세스가 옛 엔트리에 히트한다. → 무효화 → 반납 → ASID 해제.

**7. 유효 → 유효로 OA 교체.** 페이지 이동이나 블록 split 에서 PTE 를 한 번에 덮어쓴다. ARM 에서는 아키텍처 위반(BBM)이고, 복사 도중 다른 CPU 의 쓰기를 잃는다. → break → 무효화 → 복사 → make.

## 8. 면접 Q&A

<details><summary>Q1. munmap 을 호출하면 커널 안에서 무슨 일이 어떤 순서로 일어나나요?</summary>

VMA 를 찾아 나누거나 지운다. 그 범위의 PTE 를 지우면서 해제할 페이지를 `mmu_gather` 에 모은다. 비게 된 페이지 테이블도 떼어 내서 모은다. 범위 크기에 따라 VA 별 또는 mm 전체 TLB 무효화를 하고(x86 은 33 페이지 기준, SMP 면 IPI), 무효화가 **끝난 뒤에** 페이지와 테이블을 해제한다. 이 레슨의 `unmap_range → gather → tlb_finish` 가 그 축소판이다. 꼬리: "왜 해제가 마지막인가?" → 다른 CPU 가 낡은 TLB 로 그 프레임에 접근할 수 있는데, 그사이 프레임이 다른 프로세스에 가면 정보 유출이나 메모리 손상이 된다. 실측: 무효화를 생략하면 a 가 b 의 비밀을 읽었다.
</details>

<details><summary>Q2. 왜 33 페이지에서 전체 무효화로 바꾸나요? 그 숫자는 어디서 왔나요?</summary>

VA 별 무효화 n 번의 비용과, 전체 무효화 1 번 + 그 뒤 남은 엔트리를 다시 채우는 비용을 비교한 경험값이다. x86 `tlb.c` 주석: 할당의 95 % 이상을 덮고, 단일 무효화가 약 100 ns 라 최대 약 3 µs 로 묶인다. `/sys/kernel/debug/x86/tlb_single_page_flush_ceiling` 으로 바꿀 수 있는 것으로 안다(경로는 기억 기준). arm64 는 기준이 다르다. range TLBI 가 없으면 512 페이지(= `MAX_DVM_OPS`)부터 mm 전체를 무효화하고, 이는 soft lock-up 방지용 상한이다. 데모 실측: 늘 ASID 를 쓰면 1 페이지 unmap 뒤 47 번 다시 채우고, 늘 VA 를 쓰면 47 페이지에 명령 47 개다. 꼬리: "다른 ASID 는?" → ASID 무효화는 남의 엔트리를 건드리지 않는다(실측 b 미스 0). 전역 무효화만 남까지 날린다.
</details>

<details><summary>Q3. mprotect 로 권한을 늘릴 때와 줄일 때 TLB 처리가 왜 다른가요?</summary>

줄이면 낡은 엔트리가 **더 많은** 권한을 준다. 무효화하지 않으면 금지된 쓰기·실행이 성공하므로 반드시 무효화한다. 늘리면 낡은 엔트리가 **더 적은** 권한을 준다. 최악이라야 불필요한 폴트 한 번이고, 핸들러가 PTE 를 확인해 이미 허용됐으면 로컬로 그 페이지만 무효화하고 돌아간다(spurious fault). arm64 의 `flush_tlb_fix_spurious_fault` 가 브로드캐스트 없는 로컬 페이지 무효화다(직접 읽음). 실측: R→RW 에 무효화 0, 첫 쓰기에 spurious 1 + `VA_LEAF` 1, 두 번째 쓰기는 TLB 히트. 꼬리: "U 를 더하는 건 확대인가?" → kernel 은 U 페이지를 실행할 수 없으므로(PXN/SMEP), kernel X 를 잃는 축소일 수 있다.
</details>

<details><summary>Q4. 페이지 테이블 페이지를 해제할 때 TLB 말고 또 무엇을 무효화해야 하나요?</summary>

**walk cache**(중간 레벨 엔트리 캐시, Intel 의 paging-structure cache)다. 하드웨어는 "VA 상위 비트 → 다음 레벨 테이블 주소" 를 캐시한다. 테이블을 해제·재사용했는데 이 캐시가 남으면, 하드웨어가 남의 데이터를 디스크립터로 해석한다. 실측(버그 ③): b 가 자기 페이지에 쓴 8 바이트가 a 의 디스크립터가 되어 a 가 b 의 페이지를 읽었다. arm64 는 `freed_tables` 면 `TLBI VAE1IS`, 아니면 `VALE1IS`(last level only)로 walk cache 를 살린다(master 의 `TLBF_NOWALKCACHE`, 직접 읽음). 꼬리: "그럼 늘 VAE 를 쓰면?" → 정확하지만, 테이블을 안 풀었을 때 walk cache 를 쓸데없이 버린다.
</details>

<details><summary>Q5. 빈 페이지 테이블은 언제, 어떻게 회수하나요? 안 하면?</summary>

안 하면 sparse 한 주소 공간(mmap/munmap 반복, 큰 VA 를 듬성듬성 쓰는 JIT·GC)에서 테이블만 쌓인다. 연습 2: 5 페이지(20 KB)를 흩어 놓으면 테이블 10 장(40 KB)이 든다. 방법은 테이블마다 유효 엔트리 수를 세서 0 이 되면 즉시 부모에서 떼는 것(이 레슨, O(1), 모든 경로에서 카운트를 맞춰야 함), 또는 비었는지 스캔하는 것(ARCH-A01, O(512))이다. Linux 는 VMA 경계 기준으로 그 범위에 완전히 들어가는 테이블만 `free_pgtables` 로 푼다(기억 기준). 회수한 테이블은 walk cache 무효화 **뒤에** 반납한다. 꼬리: "루트는?" → 주소 공간이 살아 있는 한 안 푼다. TTBR 이 가리키고 있다.
</details>

<details><summary>Q6. ARM 의 break-before-make 는 무엇이고 언제 필요한가요?</summary>

유효한 엔트리를 다른 유효한 값(다른 출력 주소, 블록 ↔ 테이블, 메모리 타입 등)으로 바꿀 때 바로 덮어쓰지 않는다. ① 무효로 쓰고 ② TLBI + DSB 로 모든 코어에서 지운 뒤 ③ 새 값을 쓰는 규칙이다. 안 지키면 같은 VA 에 옛·새 변환이 TLB 에 함께 있을 수 있고(TLB conflict), 결과는 아키텍처상 예측할 수 없다. 대가는 ①~③ 사이에 그 VA 에 접근한 코어가 폴트를 받는다는 것이라, OS 는 그 폴트를 "잠깐 기다려" 로 처리한다. 실측: `migrate_page` 의 로그가 `[VA_LEAF]` 하나, `bbm_viol 0`. 권한만 바꾸는 경우는 일반적으로 BBM 없이 된다(정확한 목록은 ARM ARM 참조, 절 번호는 확인 못 함). 꼬리: "x86 은?" → 아키텍처 규칙으로 강제하지는 않는 것으로 알지만, PA 를 바꾸면 커널이 어차피 무효화한다.
</details>

<details><summary>Q7. 멀티코어에서 TLB shootdown 은 어떻게 동작하고, 왜 비싼가요?</summary>

x86: 이 mm 을 실행했을 수 있는 CPU(`mm_cpumask`)에 IPI 를 보내고, 각자 로컬 무효화 후 응답할 때까지 시작한 CPU 가 기다린다. 비용은 인터럽트 진입 + 캐시 라인 핑퐁 + 대기이고 CPU 수에 비례한다. 그래서 gather 로 모아 한 번에 보내고, 한 번도 그 mm 을 실행하지 않은 CPU 는 건너뛴다. arm64: `TLBI ...IS` 가 인터커넥트(DVM)로 브로드캐스트되어 IPI 가 없고, `DSB ISH` 로 완료를 기다린다. RISC-V: `sfence.vma` 는 로컬이라 SBI/IPI 로 원격 요청한다(RISCV-A02). 연습 5: 4 코어 중 3 개에서 돈 mm 의 1 페이지 munmap 16 번은 IPI 32 개, 16 페이지 한 번이면 2 개, arm64 는 0 개다. 꼬리: "단일 스레드 프로세스는?" → 다른 CPU 가 mask 에 없으면 로컬 무효화만 한다.
</details>

<details><summary>Q8. MMU 가 없는 Cortex-M(MPU)에서는 이 이야기가 어떻게 바뀌나요?</summary>

TLB 도 walk cache 도 없으니 무효화가 없다. MPU 영역 레지스터가 곧 "매핑" 이고 쓰면 반영된다. 다만 파이프라인과 버퍼에 이전 접근이 남아 있을 수 있어서, 영역을 바꾼 뒤 `DSB` + `ISB` 로 이후 명령이 새 설정을 보게 한다. "권한을 줄인 뒤 재사용" 의 순서 규칙은 MPU 에서도 같다. 태스크 A 의 스택 영역을 B 에게 넘기기 전에 A 의 영역을 끄고 배리어를 친다. 페이지 테이블을 회수할 일은 없다. 영역 수가 8~16 개로 고정이라 오히려 "어떤 영역을 재사용하나" 가 문제다(ARCH-M15, BM-A08, SEC-M08). 꼬리: "Cortex-A 위 RTOS 는?" → 이 레슨 그대로다. 정적 매핑이라도 태스크 종료 시 ASID 무효화와 블록 split 의 BBM 은 피할 수 없다.
</details>

## 9. 추가 연습

### 연습 1 — 무효화가 필요한가? (needs_flush)

leaf 엔트리가 old → new 로 바뀔 때 `none / flush / BBM` 을 돌려주는 함수를 써라. 기준은 "user/kernel × R/W/X 6 가지 능력 중 잃은 것이 있나", 그리고 OA 변경이다. 다음 표를 채워라: 무효 → 유효, R → RW, RW → R, RX → RW, kernel RX → +U, kernel R → +U, PROT_NONE → R, 유효 → 무효, OA 변경.

<details><summary>답</summary>

none, none, flush, flush, **flush**, none, none, flush, BBM. kernel RX 에 U 를 더하면 user 는 R·X 를 얻지만 kernel 이 X 를 잃는다(U 페이지 실행 금지 = PXN/SMEP 개념, ARCH-A01). 그래서 비트가 "늘었다" 고 확대인 것이 아니다. 능력 집합을 비교해야 한다. kernel R 에 U 를 더하는 것은 X 가 원래 없었으므로 잃는 것이 없다. practice.c `needs_flush`, `abilities`.
</details>

### 연습 2 — 테이블이 몇 장 필요한가 (tables_needed)

4 KB 페이지 VA 집합에 필요한 페이지 테이블 수(루트 포함)를 공식으로 구하라. `{0x400000, 0x600000, 0x40000000, 1<<39, (1<<39)+0x1000}` 은?

<details><summary>답</summary>

`1 + |{va>>39}| + |{va>>30}| + |{va>>21}|` = 1 + 2 + 3 + 4 = **10** 장, 즉 데이터 20 KB 에 테이블 40 KB 다. 같은 2 MB 안의 페이지는 L3 을 공유하고, 같은 1 GB 안은 L2 를, 같은 512 GB 안은 L1 을 공유한다. 본문 스트레스 테스트의 `tables_expected()` 가 같은 공식으로 회수가 정확한지 확인한다. sparse 매핑이 비싼 이유와, 큰 페이지(2 MB 블록, ARCH-A01)가 테이블을 한 단계 줄이는 이유가 같다. practice.c `tables_needed`.
</details>

### 연습 3 — Linux 의 결정 재현 (x86 vs arm64)

n 페이지 user 범위 무효화에서 x86 과 arm64(range TLBI 없음)가 페이지 단위 명령을 몇 개 내는지, 언제 전체로 바꾸는지 함수로 써라. n = 1, 33, 34, 511, 512.

<details><summary>답</summary>

```text
  npages=1    x86=1    arm64=1
  npages=33   x86=33   arm64=33
  npages=34   x86=-1   arm64=34
  npages=511  x86=-1   arm64=511
  npages=512  x86=-1   arm64=-1
```

(-1 = 전체.) x86: `> 33` (`tlb_single_page_flush_ceiling`, 성능 손익분기). arm64: `>= MAX_DVM_OPS` = `PTRS_PER_PTE` = 512 (4 KB granule, soft lock-up 방지 상한). FEAT_TLBIRANGE 가 있으면 range 명령 몇 개로 훨씬 큰 범위를 덮는다(`MAX_TLBI_RANGE_PAGES`). 두 상수 모두 2026-11-11 torvalds/master 에서 확인했다. practice.c `linux_flush_choice`.
</details>

### 연습 4 — 이벤트 로그 검사기 (BBM · 반납 순서)

`WRITE(slot, val) / TLBI(slot | 전부) / FREE(pa)` 이벤트 열을 받아 BBM 위반과 "무효화 전 반납" 을 찾는 함수를 써라. 다음 다섯 로그를 판정하라: 정상 migrate, 직접 교체, 반납 먼저, 다른 슬롯 무효화, 무효화를 쓰기보다 먼저.

<details><summary>답</summary>

정상 = 0, 직접 교체(유효 → 유효 OA 변경 후 무효화, 반납) = **BBM 만**(반납은 무효화 뒤라 괜찮다), 반납 먼저 = EARLY_FREE, 다른 슬롯(2 번) 무효화 = EARLY_FREE(1 번 슬롯은 안 덮임), 무효화 → 지우기 → 반납 = EARLY_FREE(지우기 **전**의 무효화는 소용없다). 전부 무효화(-1)면 0. 규칙은 "지운(바꾼) 순간 pending, 덮는 무효화가 와야 해제" 하나다. 본문 `set_desc` + `pend_hit` 이 같은 검사를 하드웨어 모델 안에서 한다. 실제 커널 검증 도구로 쓰려면 로그를 SMP 이벤트 순서로 확장하면 된다. practice.c `bbm_check`.
</details>

### 연습 5 — shootdown IPI 세기

4 코어, mm 이 CPU 0·1·3 에서 돌았다(mask 0b1011). CPU 0 이 16 페이지를 지운다. 1 페이지 munmap 16 번 vs 16 페이지 한 번에, x86(IPI)과 arm64(브로드캐스트 TLBI)의 IPI 수는? 단일 스레드 프로세스(mask = 자기만)면?

<details><summary>답</summary>

x86: 다른 CPU 2 개 × 라운드 수 → 16 번이면 **32**, 한 번이면 **2**. arm64: 0(명령이 하드웨어로 퍼짐. 대신 DVM 메시지와 `DSB ISH` 대기). 단일 스레드: 0(로컬 무효화만). 그래서 "munmap 을 모아서 하라", "free 한 메모리를 바로 OS 에 돌려주지 말라"(jemalloc/glibc 의 지연 반납)는 조언이 나온다. 이 계산은 개념 모델이다. 실제 x86 은 lazy TLB 모드의 CPU 를 건너뛰는 등 최적화가 더 있다(세부는 확인 안 함). practice.c `ipi_count`.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 20/20 passed, exit 0 (ASan+UBSan 빌드도 20/20)
```

실제 출력 일부:

```text
-- 연습 1: needs_flush (abilities lost? OA changed?)
  RX -> RW (X lost even though W gained)           -> flush
  kernel RX -> +U (kernel loses X: SMEP/PXN rule)  -> flush
  OA change (migration)                            -> BBM
…
  5 pages spread over 2 L0 slots -> 10 table pages = 40 KB of page tables for 20 KB of data
…
  x86: 16 munmap(1 page) -> 32 IPIs, one munmap(16 pages) -> 2 IPIs; arm64 -> 0 IPIs
20/20 passed
```

## 10. 핵심 정리

- 하드웨어는 페이지 테이블을 **읽기만** 한다. 만들고, 지우고, 회수하고, TLB 를 비우는 것은 전부 OS 의 일이다. 메모리를 바꿔도 TLB 와 walk cache 는 저절로 바뀌지 않는다.
- 순서는 하나다: **PTE 변경 → (모든 CPU 에서) 무효화 완료 → 프레임·테이블 반납 → ASID 해제.** 순서가 바뀌면 최종 상태가 멀쩡해 보여도 버그다(실측: 반납 먼저는 `audit 0`, `early_free > 0`).
- 무효화가 필요한 경우: 유효 → 무효, 능력이 줄어듦, OA 변경(BBM). 필요 없는 경우: 무효 → 유효, 순수 확대(spurious fault 로 게으르게, 로컬 무효화).
- 정책: n ≤ 33 이면 VA 별, 넘으면 ASID 한 번(x86 `tlb_single_page_flush_ceiling`). arm64 는 512(`MAX_DVM_OPS`). 전역(G) 페이지가 섞이면 ASID 로는 못 지우므로 ALL 이고, 남의 엔트리까지 날아간다.
- 테이블을 풀었으면 walk cache 까지(`VAE`), 안 풀었으면 leaf 만(`VALE`). 안 지키면 해제된 테이블 자리에 들어온 남의 데이터가 디스크립터로 쓰인다(실측).
- 빈 테이블 회수: 테이블별 live 카운트(O(1), 모든 경로에서 짝 맞추기) 또는 스캔. 필요한 테이블 수 = 1 + |va>>39| + |va>>30| + |va>>21|.
- 프로세스 종료 = fullmm: ASID 무효화 한 번. 세대 방식 ASID 할당기라면 재할당할 때 전체 무효화로 대신한다.
- SMP: x86 은 IPI shootdown(모아서 보내기), arm64 는 `TLBI ...IS` 브로드캐스트 + `DSB ISH`, RISC-V 는 SBI/IPI. MPU 만 있는 Cortex-M 은 무효화 대신 `DSB; ISB`.

## 참고자료

- OSTEP 19장 "Paging: Faster Translations (TLBs)" (ASID, 컨텍스트 스위치와 TLB), 20장 "Paging: Smaller Tables" (다단계 테이블과 테이블 메모리) — 로컬 `book-md/C19_…`, `C20_…`.
- Linux `arch/x86/mm/tlb.c` — `tlb_single_page_flush_ceiling = 33` 과 주석, `init_flush_tlb_info` 의 비교 (2026-11-11 torvalds/master 에서 읽음). `Documentation/arch/x86/tlb.rst`.
- Linux `arch/arm64/include/asm/tlbflush.h` — `MAX_DVM_OPS = PTRS_PER_PTE`, `__flush_tlb_range_limit_excess`, `TLBF_NOWALKCACHE` → `vale1is` (같은 날 읽음).
- Linux `arch/arm64/include/asm/tlb.h` — `tlb_flush()`: `freed_tables` 에 따른 walk cache 처리, `fullmm` 주석 (같은 날 읽음).
- Linux `arch/arm64/include/asm/pgtable.h` — `flush_tlb_fix_spurious_fault` (로컬, 브로드캐스트 없음), "arm64 mandates break-before-make" 주석 (같은 날 읽음).
- Linux `mm/mmu_gather.c`, `include/asm-generic/tlb.h` (`struct mmu_gather`, `tlb_finish_mmu`, `tlb_flush_mmu`), `mm/memory.c` `free_pgtables` — 이름과 역할은 기억 기준.
- Arm Architecture Reference Manual for A-profile (DDI 0487), VMSAv8-64 장: TLB maintenance, break-before-make 요구 사항 — 절 번호는 판마다 달라 확인 못 함.
- `mprotect(2)`, `munmap(2)` — https://man7.org/linux/man-pages/man2/mprotect.2.html , POSIX https://pubs.opengroup.org/onlinepubs/9799919799/functions/mprotect.html
- 같은 트랙·이웃: ARCH-A01 (워커·포맷·블록 split), ARCH-A02 (TLB·ASID·세대), ARCH-A03 (실측 TLB 미스), OS-B10 (단일 레벨 변환), OS-M03 (COW fork), OS-A06 (mprotect + SIGSEGV), OS-A10 (xv6 COW), ARM-A03 (MMU 켜기), RISCV-A01/A02 (sfence.vma, 원격 펜스), PCIE-M15 / SOCX-M12 (IOMMU/SMMU 무효화), ARCH-M12 (DMA 캐시 일관성), ARCH-M15 / BM-A08 / SEC-M08 (MPU).
