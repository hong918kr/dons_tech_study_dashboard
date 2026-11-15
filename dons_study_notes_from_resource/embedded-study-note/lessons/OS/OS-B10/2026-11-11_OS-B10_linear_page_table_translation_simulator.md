---
id: OS-B10
time: 60분
updated: 2026-11-11
---
# OS-B10 주소 변환 시뮬레이터: 단일 레벨 페이지 테이블

## 학습 목표

- 주소 공간 크기·페이지 크기·물리 메모리 크기에서 offset 비트 수, VPN 비트 수, 페이지 테이블 항목 수를 바로 계산하고, 가상 주소(VA)를 VPN 과 offset 으로 손으로 나눌 수 있다.
- PTE(page table entry) 한 칸을 valid / R / W / X / accessed / dirty / PFN 으로 설계하고, `translate(va, 접근 종류)` 가 **PA 또는 폴트 종류(범위 밖·미할당·권한 위반·잘못된 PFN)** 를 돌려주게 구현한다. 성공한 접근에서만 A/D 비트를 켠다.
- 내 시뮬레이터를 OSTEP 공식 숙제 시뮬레이터 `paging-linear-translate.py` 의 답과 **20개 설정, 144개 주소** 에서 대조해 모두 일치함을 보이고, 그 시뮬레이터의 한계(범위 밖 VA 에서 IndexError)를 설명한다.
- 시뮬레이터의 폴트 종류가 x86 / AArch64 / RISC-V 하드웨어 폴트, 그리고 macOS 의 실제 시그널(실측: 미할당 → SIGSEGV, 권한 위반 → SIGBUS)과 어떻게 대응하는지 말할 수 있다.
- 펌웨어 메모리 맵(NULL 가드, 코드 R-X, 데이터 RW-, 스택 가드 페이지, W^X)을 페이지 테이블로 만들고 테스트한다.

## 1. 문제

"VA 0x3229, 페이지 4 KB, 이 페이지 테이블이면 PA 는? 이 접근은 왜 폴트죠?" — OS 면접의 단골이고, 펌웨어에서는 그대로 실무다: Cortex-A/R·RISC-V 부트 코드가 MMU 를 켤 때 짜는 변환 테이블(ARM-A03, RISCV-A01), 폴트 핸들러에서 "미할당이냐 권한이냐" 를 가르는 코드(BM-M13), MMU 가 없는 Cortex-M 의 MPU 영역 설정(ARCH-M15, BM-A08)이 모두 이 레슨의 `translate()` 를 다른 모양으로 쓴 것이다. Don 이 SSD 에서 다룬 FTL 의 L2P 매핑 테이블(LBA → 물리 NAND 주소, "unmapped" 항목, 더티 세그먼트 플러시)도 구조가 똑같은 **선형 매핑 테이블** 이다 (MEM-A09).

페이징(paging)은 주소 공간을 같은 크기의 **페이지** 로 자르고, 물리 메모리를 같은 크기의 **프레임(page frame)** 으로 자른 뒤, "가상 페이지 번호(VPN) → 물리 프레임 번호(PFN)" 표를 둔다. 이 표가 **선형 페이지 테이블(linear page table)** — VPN 을 인덱스로 쓰는 그냥 배열이다 (OSTEP 18장). offset 은 바뀌지 않는다. 바뀌는 건 위쪽 비트뿐이다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b10a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">VA 0x3229 변환 — 16 KB 공간, 4 KB 페이지, 64 KB 물리 (OSTEP -s 0)</text>
  <text x="20" y="52" fill="currentColor">VA (14비트)</text>
  <rect x="20" y="60" width="70" height="36" class="f-warn" stroke="currentColor"/> <rect x="90" y="60" width="190" height="36" class="f-mut" stroke="currentColor"/>
  <text x="30" y="83" fill="currentColor">VPN=3</text> <text x="100" y="83" fill="currentColor">offset = 0x229 (12비트)</text>
  <text x="20" y="114" fill="currentColor" font-size="12">VPN = va &gt;&gt; 12, offset = va &amp; 0xFFF</text>
  <text x="340" y="52" fill="currentColor">페이지 테이블 (VPN 이 인덱스)</text>
  <g stroke="currentColor">
    <rect x="340" y="60" width="200" height="30" class="f-ok"/> <rect x="340" y="90" width="200" height="30" class="f-mut"/> <rect x="340" y="120" width="200" height="30" class="f-mut"/>
    <rect x="340" y="150" width="200" height="30" class="f-acc"/>
  </g>
  <text x="350" y="80" fill="currentColor">[0] V=1  PFN 12</text>
  <text x="350" y="110" fill="currentColor">[1] V=0  (미할당)</text>
  <text x="350" y="140" fill="currentColor">[2] V=0  (미할당)</text>
  <text x="350" y="170" fill="currentColor">[3] V=1  PFN 6</text>
  <line x1="55" y1="96" x2="335" y2="165" stroke="currentColor" marker-end="url(#b10a)"/>
  <text x="640" y="202" fill="currentColor">PA (16비트)</text>
  <rect x="560" y="210" width="60" height="36" class="f-acc" stroke="currentColor"/> <rect x="620" y="210" width="110" height="36" class="f-mut" stroke="currentColor"/>
  <text x="568" y="233" fill="currentColor">PFN=6</text> <text x="628" y="233" fill="currentColor">0x229</text>
  <line x1="540" y1="165" x2="588" y2="205" stroke="currentColor" marker-end="url(#b10a)"/>
  <path d="M185,96 C185,260 600,270 670,250" fill="none" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#b10a)"/>
  <text x="200" y="262" fill="currentColor" font-size="12">offset 은 그대로 내려간다</text>
  <text x="440" y="290" fill="currentColor" font-weight="bold">PA = (6 &lt;&lt; 12) | 0x229 = 0x6229</text>
  <text x="20" y="160" fill="currentColor" font-size="12">VA 0x1369 → VPN 1 → V=0</text>
  <text x="20" y="178" fill="currentColor" font-size="12">→ 폴트 (OSTEP: "Invalid")</text>
</svg>
```

### Part A — 파라미터와 주소 분해

- `log2_exact(x)`: x 가 2^k 이면 k, 아니면 -1 (0 도 -1).
- `mmu_init(m, asize, pgsize, psize, pt, cap)`: 세 크기가 모두 2의 거듭제곱인지, 페이지가 주소 공간·물리 메모리보다 크지 않은지, 항목 수 `asize/pgsize` 가 호출자 배열 `cap` 에 들어가는지, 프레임 수가 PTE 의 22비트 PFN 에 들어가는지 검사한다. 통과하면 `off_bits`, `vpn_bits`, `npages`, `nframes` 를 채우고 테이블을 전부 invalid(0) 로.
- `va_vpn`, `va_off`, `make_pa(pfn, off)`: 시프트와 마스크로.

### Part B — PTE 와 translate

PTE 는 32비트, 비트 위치는 RISC-V Sv32 의 leaf PTE 를 빌렸다 (아래 그림). `pte_make(pfn, flags)`, `pte_pfn`, `perm_ok(pte, 접근)` 을 만들고, 핵심 함수:

```c
xl_t mmu_translate(mmu_t *m, uint64_t va, acc_t acc, uint64_t *pa);
/* XL_OK | XL_RANGE | XL_INVALID | XL_PROT | XL_BADPFN */
```

- 검사 순서는 **RANGE → INVALID → PROT → BADPFN**. 성공하면 `*pa` 를 쓰고 A 비트를, 쓰기면 D 비트도 켠다. 폴트면 `*pa` 와 PTE 를 건드리지 않는다. 결과 종류별 횟수를 `m->stat[]` 에 센다.
- 손으로 계산한 경우(OSTEP 18장 예: VA 21 → PA 117) + 무작위 300 설정 × 50 접근을 `/`, `%` 로만 짠 기준 구현과 비교.

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">이 레슨의 PTE (RISC-V Sv32 leaf PTE 와 같은 비트 위치)</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="330" height="40" class="f-acc"/> <rect x="340" y="40" width="60" height="40" class="f-mut"/> <rect x="400" y="40" width="45" height="40" class="f-warn"/> <rect x="445" y="40" width="45" height="40" class="f-warn"/> <rect x="490" y="40" width="45" height="40" class="f-mut"/> <rect x="535" y="40" width="45" height="40" class="f-mut"/>
    <rect x="580" y="40" width="45" height="40" class="f-ok"/> <rect x="625" y="40" width="45" height="40" class="f-ok"/> <rect x="670" y="40" width="45" height="40" class="f-ok"/> <rect x="715" y="40" width="20" height="40" class="f-ok"/>
  </g>
  <text x="120" y="65" fill="currentColor">PFN [31:10] (22비트)</text> <text x="352" y="65" fill="currentColor">RSW</text> <text x="415" y="65" fill="currentColor">D</text> <text x="460" y="65" fill="currentColor">A</text> <text x="505" y="65" fill="currentColor">G</text> <text x="550" y="65" fill="currentColor">U</text> <text x="595" y="65" fill="currentColor">X</text>
  <text x="640" y="65" fill="currentColor">W</text> <text x="685" y="65" fill="currentColor">R</text> <text x="719" y="65" fill="currentColor">V</text>
  <text x="352" y="97" fill="currentColor" font-size="12">9:8</text> <text x="415" y="97" fill="currentColor" font-size="12">7</text> <text x="460" y="97" fill="currentColor" font-size="12">6</text> <text x="505" y="97" fill="currentColor" font-size="12">5</text> <text x="550" y="97" fill="currentColor" font-size="12">4</text> <text x="595" y="97" fill="currentColor" font-size="12">3</text>
  <text x="640" y="97" fill="currentColor" font-size="12">2</text> <text x="685" y="97" fill="currentColor" font-size="12">1</text> <text x="719" y="97" fill="currentColor" font-size="12">0</text>
  <text x="10" y="120" fill="currentColor" font-size="12">회색(RSW, G, U)은 안 쓴다. A/D 는 translate(=하드웨어)가 켜고 OS 가 지운다.</text>
  <text x="10" y="137" fill="currentColor" font-size="12">PA = PFN × 페이지 크기 + offset → Sv32 는 22 + 12 = 34비트 물리 주소</text>
  <text x="10" y="164" fill="currentColor" font-weight="bold">OSTEP 시뮬레이터의 PTE</text>
  <g stroke="currentColor">
    <rect x="10" y="172" width="45" height="30" class="f-ok"/> <rect x="55" y="172" width="400" height="30" class="f-acc"/>
  </g>
  <text x="22" y="192" fill="currentColor">V</text> <text x="180" y="192" fill="currentColor">PFN [30:0] — 보호 비트 없음</text> <text x="470" y="192" fill="currentColor" font-size="12">→ pte_from_ostep() 은 V|R|W|X 로 옮긴다</text>
</svg>
```

### Part C — OSTEP 대조 + 펌웨어 메모리 맵

1. `pte_from_ostep(raw)`: OSTEP PTE(bit31 = valid, 나머지 = PFN)를 내 형식으로. 하네스의 파서가 OSTEP 출력(`-c` 의 답 포함)을 읽어 테이블을 채우고, 모든 VA 를 `translate(ACC_READ)` 해 답과 비교한다. 출력 두 개(`-s 0 -c`, `-P 1k -a 16k -p 32k -v -u 75 -s 2 -c`)는 실제 출력을 `solution.c` 안에 넣어 두었고, 나머지 18개 설정은 `demo_ostep_check.sh` 가 시뮬레이터를 직접 돌려 비교한다.
2. `mmu_map_regions(m, regions, n)`: `{va, pa, len, prot}` 영역 목록을 페이지 단위 PTE 로 쓴다. **all-or-nothing** — 정렬 안 됨 / 범위 밖 / W+X / W 인데 R 없음 / 겹침이 하나라도 있으면 -1 이고 테이블은 그대로. 이걸로 64 KB 펌웨어 맵(NULL 가드, `.text` R-X, `.rodata` R--, `.data` RW-, 스택 가드, 스택 RW-)을 만들고, NULL 역참조·코드 덮어쓰기·데이터 실행·스택 오버플로가 각각 맞는 폴트가 되는지 테스트한다.

## 2. Problem (English)

Implement a single-level (linear) page table simulator.

```c
int      log2_exact(uint64_t x);                     /* k if x == 2^k, else -1 */
int      mmu_init(mmu_t *m, uint64_t asize, uint64_t pgsize, uint64_t psize,
                  uint32_t *pt, size_t cap);         /* 0 or -1 */
uint64_t va_vpn(const mmu_t *m, uint64_t va);
uint64_t va_off(const mmu_t *m, uint64_t va);
uint64_t make_pa(const mmu_t *m, uint64_t pfn, uint64_t off);
uint32_t pte_make(uint64_t pfn, uint32_t flags);     /* PFN in [31:10], flags in [9:0] */
uint64_t pte_pfn(uint32_t pte);
bool     perm_ok(uint32_t pte, acc_t acc);           /* ACC_READ / ACC_WRITE / ACC_EXEC */
xl_t     mmu_translate(mmu_t *m, uint64_t va, acc_t acc, uint64_t *pa);
uint32_t pte_from_ostep(uint32_t raw);               /* OSTEP: bit31 valid, rest PFN */
int      mmu_map_regions(mmu_t *m, const region_t *r, size_t n);
```

- All sizes are powers of two; page size ≤ address-space size and ≤ physical size; `asize/pgsize ≤ cap`; `psize/pgsize ≤ 2^22`.
- `mmu_translate` checks, in order: `va >= asize` → `XL_RANGE`; V clear → `XL_INVALID`; missing R/W/X for the access → `XL_PROT`; PFN ≥ number of frames → `XL_BADPFN`. On success set A (and D for writes), store the PA, return `XL_OK`. On a fault, leave `*pa` and the PTE unchanged. Count every outcome in `m->stat`.
- `mmu_map_regions` is all-or-nothing: reject zero length, misalignment, out-of-range VA/PA, W+X, W without R, overlap with a valid page or with another region in the same call.

Example (16 KB space, 4 KB pages, 64 KB physical; PT = `[V|RW pfn 12, R pfn 3, 0, V|RWX pfn 20]`):

| call | result |
|---|---|
| `translate(0x0123, READ)` | `XL_OK`, PA `0xC123`, PT[0] gets A |
| `translate(0x0456, WRITE)` | `XL_OK`, PA `0xC456`, PT[0] gets D |
| `translate(0x1000, WRITE)` | `XL_PROT` (read-only page) |
| `translate(0x2FFF, READ)` | `XL_INVALID` |
| `translate(0x3000, READ)` | `XL_BADPFN` (PFN 20 ≥ 16 frames) |
| `translate(0x4000, READ)` | `XL_RANGE` (VA == asize) |

Cross-check against `paging-linear-translate.py -c` from the OSTEP homework: every address it calls valid must translate to the same PA, every "Invalid" must be `XL_INVALID`.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 이 맥: 27.2 SDK 링크가 깨져 있다
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter     # 18/52 passed, exit 1
python3 -I paging-linear-translate.py -s 3 -c | ./starter --ostep -         # 대조 모드 (풀고 나면)
```

시작 코드 실행 결과 (앞부분과 끝):

```text
FAIL starter.c:291  log2_exact(1) == 0
FAIL starter.c:292  log2_exact(4096) == 12
FAIL starter.c:293  log2_exact(1ull << 40) == 40
FAIL starter.c:299  mmu_init(&m, 16384, 4096, 65536, pt, 16) == 0
...
random: 0 translations, 300 mismatches (OK=0 RANGE=0 INVALID=0 PROT=0 BADPFN=0)
...
FAIL starter.c:428  valid == 9 && wx == 0 && dirty == 1
18/52 passed
```

통과하는 18개는 "아무것도 안 하는 스텁" 과 우연히 맞는 것(잘못된 입력에 -1, 미할당 → INVALID 등)과 하네스 자체 검사(OSTEP 출력 파싱)다. 하네스 메모:

- 테스트는 PTE 를 `m.pt` 가 아니라 **호출자 배열 `pt[]`** 로 직접 쓴다. 그래서 `mmu_init` 스텁이 실패해도 NULL 을 밟지 않는다.
- `ostep_parse` / `ostep_crosscheck` 는 TODO 가 아니다. 비고정 형식(`  0x8000000c`)과 `-v` 형식(`  [       3]  0x80000017`) 둘 다 읽는다. `-c` 없이 뽑은 출력이면 "답이 없다" 고 알려 준다.
- 데이터 파일이 필요 없다 (OSTEP 출력 2개는 문자열로 들어 있다). 어느 cwd 에서 실행해도 같다. 실행 시간 0.01 초 미만.

관찰용 파일 (하네스와 별개):

```bash
sh demo_ostep_check.sh                     # OSTEP 시뮬레이터 20개 설정 vs solution.c (python3 -I, 읽기 전용)
cc -std=c11 -Wall -Wextra -Werror -O2 demo_fault_kinds.c -o dfk && ./dfk   # macOS 실제 폴트 → 시그널
```

## 4. 힌트

<details>
<summary>힌트 1 — 2의 거듭제곱 판정은 한 줄</summary>

`x & (x - 1)` 은 가장 낮은 1 비트를 지운다. 2^k 는 1 비트가 하나뿐이니 결과가 0. 단 `x == 0` 도 0 이 되므로 따로 막는다. k 는 오른쪽 시프트 횟수를 세면 된다 (`__builtin_ctzll` 도 되지만 TODO 에서는 쓰지 말자 — C-B 레슨 규칙).

</details>

<details>
<summary>힌트 2 — VPN 마스크가 필요 없는 이유</summary>

OSTEP 은 `VPN = (va & VPN_MASK) >> SHIFT` 라고 쓰지만, va 가 주소 공간 안이면 위쪽 비트는 이미 0 이라 `va >> off_bits` 로 충분하다. 대신 **범위 검사를 먼저** 해야 한다 — 안 하면 VPN 이 `npages` 이상이 되어 배열 밖을 읽는다 (OSTEP 시뮬레이터가 실제로 이렇게 죽는다, 5. 풀이 Part C).

</details>

<details>
<summary>힌트 3 — 언제 A/D 를 켜나</summary>

"접근이 실제로 일어났을 때" 만. 권한 위반으로 막힌 쓰기는 페이지를 바꾸지 않았으니 D 를 켜면 거짓말이고, A 도 마찬가지다. 그래서 모든 검사를 통과한 뒤 `*pte |= PTE_A` (쓰기면 `| PTE_D`). PTE 를 바꿔야 하니 `uint32_t *pte = &m->pt[vpn]` 처럼 포인터로 잡는다.

</details>

<details>
<summary>힌트 4 — all-or-nothing 은 두 번 돈다</summary>

region 3개 중 세 번째가 틀렸을 때 앞의 두 개가 이미 매핑되어 있으면 반쯤 바뀐 테이블이 남는다. 1차 루프에서 **검사만** 하고(이미 valid 인 페이지, 앞 region 과의 겹침 `a.va < b.va+b.len && b.va < a.va+a.len` 포함), 전부 통과했을 때 2차 루프에서 쓴다. 범위 검사는 `va + len > asize` 대신 `len > asize - va` 로 써야 덧셈 오버플로가 없다.

</details>

## 5. 풀이

### Part A — 숫자부터: 비트 수와 테이블 크기

| 파라미터 | offset 비트 | VPN 비트 | 항목 수 | 4 B PTE 테이블 |
|---|---|---|---|---|
| 64 B 공간, 16 B 페이지 (OSTEP 그림 18.1) | 4 | 2 | 4 | 16 B |
| 16 KB, 4 KB (OSTEP 기본값) | 12 | 2 | 4 | 16 B |
| 16 KB, 1 KB | 10 | 4 | 16 | 64 B |
| 32비트, 4 KB | 12 | 20 | 1,048,576 | **4 MB / 프로세스** |
| 32비트, 16 KB (Apple Silicon 페이지 크기) | 14 | 18 | 262,144 | 1 MB |
| 32비트, 1 MB (ARM 섹션 크기) | 20 | 12 | 4,096 | 16 KB |

Apple Silicon 맥의 페이지 크기는 실측으로 16 KB 다: `sysctl -n hw.pagesize` → `16384`, `getconf PAGESIZE` → `16384` (Apple M6). 32비트에 4 KB 면 프로세스마다 4 MB — OSTEP 18장이 "프로세스 100개면 400 MB" 라고 겁주는 그 숫자이고, 다단계 테이블(OS-M01, ARCH-A01)이 필요한 이유다.

손 계산 두 개 (테스트에 그대로 있다):

| VA | 이진수 | VPN | offset | PFN | PA |
|---|---|---|---|---|---|
| 21 (64 B 공간, 16 B 페이지) | `01 0101` | 1 | 5 | 7 | `111 0101` = 117 |
| 0x3229 (16 KB, 4 KB) | `11 0010 0010 1001` | 3 | 0x229 | 6 | 0x6229 |

`mmu_init` 의 검사 하나하나에는 이유가 있다.

| 검사 | 왜 |
|---|---|
| 셋 다 2의 거듭제곱 | 시프트/마스크 분해가 성립하려면. 12 KB 주소 공간은 VPN 이 "0, 1, 2" 만 쓰는 2비트가 되어 마스크로 범위를 못 잡는다. OSTEP 시뮬레이터도 거절한다 (`address space must be a power of 2`) |
| `pgsize ≤ asize`, `pgsize ≤ psize` | 페이지 하나도 안 들어가면 무의미 |
| `npages ≤ cap` | 선형 테이블은 **주소 공간 전체** 만큼 항목이 필요하다. 32비트 + 4 KB 는 2^20 항목 → cap 4096 배열로는 거절 (테스트) |
| `nframes - 1 ≤ 2^22 - 1` | PTE 의 PFN 필드가 22비트. 2^23 프레임은 PTE 에 못 담는다. 2^22 는 딱 맞는다 (경계 테스트 둘 다) |

OSTEP 시뮬레이터는 여기에 "물리 메모리가 주소 공간보다 **커야** 한다" 를 더 요구한다 (`-a 64k -p 32k` → `Error: physical memory size must be GREATER than address space size (for this simulation)`). 실제 시스템은 반대가 보통이다 (64비트 주소 공간 ≫ 물리 메모리). 이 시뮬레이터에서는 프레임을 겹치지 않게 랜덤 배정하려고 그렇게 했을 뿐이라, 내 `mmu_init` 은 그 제한을 두지 않는다 (OSTEP 숙제 Q4 가 묻는 "어디서 안 되나" 의 답).

### Part B — translate: 검사 순서가 곧 폴트 분류

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="b10b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">mmu_translate(va, acc): 위에서부터 처음 걸리는 것이 답</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="120" height="44" rx="6" class="f-acc"/> <rect x="160" y="40" width="120" height="44" rx="6" class="f-acc"/> <rect x="310" y="40" width="120" height="44" rx="6" class="f-acc"/> <rect x="460" y="40" width="120" height="44" rx="6" class="f-acc"/> <rect x="610" y="40" width="120" height="44" rx="6" class="f-ok"/>
    <rect x="10" y="140" width="120" height="44" rx="6" class="f-warn"/> <rect x="160" y="140" width="120" height="44" rx="6" class="f-warn"/> <rect x="310" y="140" width="120" height="44" rx="6" class="f-warn"/> <rect x="460" y="140" width="120" height="44" rx="6" class="f-warn"/>
  </g>
  <text x="22" y="60" fill="currentColor">va &gt;= asize ?</text>
  <text x="22" y="76" fill="currentColor" font-size="12">주소 공간 밖</text>
  <text x="172" y="60" fill="currentColor">PTE.V == 0 ?</text>
  <text x="172" y="76" fill="currentColor" font-size="12">PT[va &gt;&gt; off]</text>
  <text x="322" y="60" fill="currentColor">R/W/X 없음 ?</text>
  <text x="322" y="76" fill="currentColor" font-size="12">acc 에 맞는 비트</text>
  <text x="472" y="60" fill="currentColor">PFN &gt;= 프레임수 ?</text>
  <text x="472" y="76" fill="currentColor" font-size="12">물리 메모리 밖</text>
  <text x="622" y="60" fill="currentColor">A=1 (W 면 D=1)</text>
  <text x="622" y="76" fill="currentColor" font-size="12">*pa = PFN|off</text>
  <text x="40" y="167" fill="currentColor">RANGE</text> <text x="185" y="167" fill="currentColor">INVALID</text> <text x="345" y="167" fill="currentColor">PROT</text> <text x="490" y="167" fill="currentColor">BADPFN</text>
  <g stroke="currentColor" marker-end="url(#b10b)">
    <line x1="130" y1="62" x2="157" y2="62"/> <line x1="280" y1="62" x2="307" y2="62"/> <line x1="430" y1="62" x2="457" y2="62"/> <line x1="580" y1="62" x2="607" y2="62"/>
    <line x1="70" y1="84" x2="70" y2="137"/> <line x1="220" y1="84" x2="220" y2="137"/> <line x1="370" y1="84" x2="370" y2="137"/> <line x1="520" y1="84" x2="520" y2="137"/>
  </g>
  <text x="140" y="56" fill="currentColor" font-size="12">no</text>
  <text x="76" y="114" fill="currentColor" font-size="12">yes</text>
  <text x="10" y="212" fill="currentColor" font-size="12">폴트 4종: PTE·*pa 불변 · OK 만 A/D 를 켠다 · 결과는 stat[] 에 센다</text>
</svg>
```

**왜 이 순서인가.** 앞의 검사가 뒤의 검사를 가능하게 한다. 범위 밖이면 PT 인덱스 자체가 없고, V=0 이면 나머지 비트는 의미가 없다 (OS 가 swap 위치 같은 다른 정보를 넣어 둘 수 있다). PROT 를 BADPFN 보다 먼저 보는 건 하드웨어 흉내다: MMU 는 PFN 이 실제 메모리에 있는지 **모른다**. 변환과 권한 검사가 끝난 뒤 그 물리 주소로 버스 접근을 했을 때 비로소 실패한다 — RISC-V 는 PMP/PMA 검사의 **access fault**, AArch64 는 **external abort** 로, 페이지 폴트와는 다른 예외다 (RISCV-M11).

**폴트 종류 ↔ 실제 하드웨어** (각 아키텍처 매뉴얼 기준, 이 맥에서 실측한 것은 아래 macOS 행뿐):

| 이 레슨 | x86-64 | AArch64 (ESR 의 DFSC/IFSC) | RISC-V (scause) | OS 가 하는 일 |
|---|---|---|---|---|
| RANGE | 비정규(non-canonical) 주소 → #GP | 변환 범위(TxSZ) 밖 → translation fault | 부호 확장 안 된 VA → page fault | 보통 프로세스 종료 |
| INVALID | #PF, 에러코드 P=0 | translation fault (level n) | page fault (12/13/15) | VMA 를 보고: 요구 페이징·스택 확장·swap-in, 아니면 SIGSEGV |
| PROT | #PF, P=1 (+W/R, I/D 비트) | permission fault | page fault (같은 코드) | COW 면 복사(OS-M03), 아니면 SIGSEGV |
| BADPFN | (MMU 는 모름) 플랫폼 의존 | synchronous external abort | access fault (1/5/7) | 하드웨어/커널 버그 — 보통 패닉 |

RISC-V 는 "미할당" 과 "권한" 을 같은 page fault 코드(접근 종류별로 12/13/15)로 보내고, 커널이 VMA 와 PTE 를 보고 구분한다. x86 은 에러코드의 P 비트, ARM 은 DFSC 로 하드웨어가 나눠 준다. 셋 다 INVALID 와 PROT 를 구분할 수 있다는 점은 같다 — 그래야 OS 가 "아직 안 올린 페이지(정상)" 와 "읽기 전용에 쓰기(COW 또는 버그)" 를 다르게 처리한다.

**A/D 비트는 누가 켜나.** 이 레슨은 translate 가 켠다 = x86 처럼 하드웨어가 켜는 모델. AArch64 v8.0 은 AF(Access Flag) 가 0 인 페이지에 접근하면 **access flag fault** 를 내고 OS 가 소프트웨어로 켠다 (하드웨어 갱신은 FEAT_HAFDBS 가 있을 때). RISC-V 도 A/D 가 꺼져 있으면 page fault 를 내는 구현(Svade)과 하드웨어가 켜는 구현(Svadu)이 둘 다 허용된다 (RISCV-M12). 어느 쪽이든 의미는 같다: **A 는 "최근에 쓰였나"(교체 정책의 재료, OS-B11 Clock), D 는 "버리기 전에 써 줘야 하나"(writeback)**. 폴트에서 켜지 않는 이유도 같다 — 막힌 쓰기는 페이지를 바꾸지 않았다.

**무작위 기준 비교.** 테스트는 300개의 무작위 설정(페이지 16 B–16 KB, 1–256 페이지, 프레임 2–8배)마다 PTE 를 무작위로 채우고(가끔 PFN 을 프레임 밖으로, 가끔 VA 를 주소 공간 밖으로) 50번씩 접근한다. 기준 구현 `ref_translate` 는 시프트·AND 없이 `/`, `%` 로만 짰다. 실제 출력:

```text
random: 15000 translations, 0 mismatches (OK=2377 RANGE=1638 INVALID=6794 PROT=3407 BADPFN=784)
```

결과 종류, PA, 접근 뒤 PTE(A/D 가 켜진 모양)까지 모두 같고, 다섯 종류가 다 나왔다 (커버리지 체크).

### Part C — OSTEP 시뮬레이터와 대조

OSTEP 숙제 시뮬레이터는 Python `random` 으로 테이블을 만들기 때문에 C 에서 같은 테이블을 재현할 수 없다. 그래서 **출력을 읽는다**: `ARG ... size` 줄에서 파라미터를, `Page Table` 아래에서 PTE 를, `-c` 를 주면 나오는 `-->` 뒤에서 답을 얻는다. OSTEP PTE 는 bit31 = valid 이고 보호 비트가 없으니 `pte_from_ostep` 은 V|R|W|X 로 옮긴다 (그래서 대조는 READ 로만 한다).

실제 명령과 출력 (시뮬레이터는 복사본을 `python3 -I` 로 읽기만 했다, Python 3.9.6):

```text
$ python3 -I paging-linear-translate.py -s 0 -c
...
Page Table (from entry 0 down to the max size)
  0x8000000c
  0x00000000
  0x00000000
  0x80000006

Virtual Address Trace
  VA 0x00003229 (decimal:    12841) --> 00006229 (decimal    25129) [VPN 3]
  VA 0x00001369 (decimal:     4969) -->  Invalid (VPN 1 not valid)
  VA 0x00001e80 (decimal:     7808) -->  Invalid (VPN 1 not valid)
  VA 0x00002556 (decimal:     9558) -->  Invalid (VPN 2 not valid)
  VA 0x00003a1e (decimal:    14878) --> 00006a1e (decimal    27166) [VPN 3]

$ python3 -I paging-linear-translate.py -s 0 -c | ./solution --ostep -
asize=16384 pgsize=4096 psize=65536 ptes=4 vas=5
  VA 0x00003229  VPN 3     mine 0x00006229 ostep 0x00006229 match
  VA 0x00001369  VPN 1     mine INVALID    ostep Invalid    match
  VA 0x00001e80  VPN 1     mine INVALID    ostep Invalid    match
  VA 0x00002556  VPN 2     mine INVALID    ostep Invalid    match
  VA 0x00003a1e  VPN 3     mine 0x00006a1e ostep 0x00006a1e match
ostep cross-check: 5/5 match
```

`demo_ostep_check.sh` 는 기본 seed 0–9, 숙제 Q2 의 `-u 0..100`, 숙제 Q3 의 "이상한" 파라미터 셋, 256-PTE 테이블 + 50 주소, 페이지 경계 주소(`-A`)를 돌린다. 실제 출력 (cwd = `/` 에서 실행):

<!-- code: demo_ostep_check.sh -->

```text
-s 0                                           ostep cross-check: 5/5 match
...  (-s 1 .. -s 9 모두 5/5)
-P 1k -a 16k -p 32k -v -u 0 -s 0               ostep cross-check: 5/5 match
-P 1k -a 16k -p 32k -v -u 100 -s 0             ostep cross-check: 5/5 match
-P 8 -a 32 -p 1024 -v -s 1                     ostep cross-check: 5/5 match
-P 8k -a 32k -p 1m -v -s 2                     ostep cross-check: 5/5 match
-P 1m -a 256m -p 512m -v -s 3                  ostep cross-check: 5/5 match
-P 4k -a 1m -p 512m -n 50 -u 60 -s 7           ostep cross-check: 50/50 match
-A 0,4095,4096,16383 -s 0                      ostep cross-check: 4/4 match
configs: 20, failed: 0, translations: 144 (58 valid, 86 invalid)

--- out-of-range VA: OSTEP vs mine ---
    if pt[vpn] < 0:
IndexError: array index out of range
mine: VA 0x4000 in a 16k space -> XL_RANGE (solution.c Part B CHECK)
```

`-P 1m -a 256m -p 512m -s 3` 의 한 줄은 `VA 0x0308b24d --> 1f68b24d [VPN 48]` — offset 이 20비트(0x8b24d)라 아래 다섯 hex 자리가 그대로 내려온다. 대조기가 진짜로 틀린 걸 잡는지도 확인했다: 답 하나를 `00007229` 로 바꿔 넣으면 `MISMATCH` 줄과 `4/5 match`, exit 1. `-c` 없이 뽑은 출력은 `no answers in input: run the OSTEP simulator with -c`, exit 2.

**OSTEP 시뮬레이터와 다른 점** (모두 실행해서 확인):

| 상황 | OSTEP `paging-linear-translate.py` | 이 레슨 |
|---|---|---|
| `-A 16384` (16 KB 공간 밖) | 범위 검사가 없어 `pt[vpn]` 에서 **IndexError** 로 죽음 | `XL_RANGE` |
| 물리 ≤ 주소 공간 | 거절 (시뮬레이션 편의) | 허용 |
| 보호 비트 | 없음 (valid + PFN 만) | R/W/X/A/D |
| PFN 이 물리 메모리 밖 | 생길 수 없음 (자기가 배정) | `XL_BADPFN` |

**펌웨어 메모리 맵.** Part C 의 두 번째 절반은 "MMU 를 켜는 부트 코드" 가 하는 일 그대로다. 64 KB VA, 4 KB 페이지, 512 KB 물리:

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">mmu_map_regions 로 만든 펌웨어 맵 (VPN 0..15) 과 테스트</text>
  <g stroke="currentColor">
    <rect x="20" y="30" width="170" height="18" class="f-ok"/> <rect x="20" y="48" width="170" height="18" class="f-ok"/> <rect x="20" y="66" width="170" height="18" class="f-warn"/>
    <rect x="20" y="84" width="170" height="90" class="f-mut"/> <rect x="20" y="174" width="170" height="18" class="f-ok"/> <rect x="20" y="192" width="170" height="18" class="f-ok"/>
    <rect x="20" y="210" width="170" height="18" class="f-ok"/> <rect x="20" y="228" width="170" height="18" class="f-acc"/> <rect x="20" y="246" width="170" height="18" class="f-acc"/>
    <rect x="20" y="264" width="170" height="18" class="f-acc"/> <rect x="20" y="282" width="170" height="18" class="f-acc"/> <rect x="20" y="300" width="170" height="18" class="f-warn"/>
  </g>
  <text x="28" y="44" fill="currentColor" font-size="12">F stack RW-  → 0x49000</text>
  <text x="28" y="62" fill="currentColor" font-size="12">E stack RW-  → 0x48000</text>
  <text x="28" y="80" fill="currentColor" font-size="12">D 가드 (V=0)</text>
  <text x="28" y="134" fill="currentColor" font-size="12">8..C 미할당</text>
  <text x="28" y="188" fill="currentColor" font-size="12">7 .data/.bss RW-</text>
  <text x="28" y="206" fill="currentColor" font-size="12">6 .data/.bss RW-</text>
  <text x="28" y="224" fill="currentColor" font-size="12">5 .data RW- → 0x40000</text>
  <text x="28" y="242" fill="currentColor" font-size="12">4 .rodata R-- → 0x23000</text>
  <text x="28" y="260" fill="currentColor" font-size="12">3 .text R-X</text>
  <text x="28" y="278" fill="currentColor" font-size="12">2 .text R-X</text>
  <text x="28" y="296" fill="currentColor" font-size="12">1 .text R-X → 0x20000</text>
  <text x="28" y="314" fill="currentColor" font-size="12">0 NULL 가드 (V=0)</text>
  <text x="230" y="44" fill="currentColor">write 0xFFFC → OK, PA 0x49FFC</text>
  <text x="230" y="80" fill="currentColor">write 0xDFF8 (스택 오버플로) → INVALID</text>
  <text x="230" y="206" fill="currentColor">exec 0x5010 (데이터 실행) → PROT (XN)</text>
  <text x="230" y="242" fill="currentColor">write 0x4100 (const 쓰기) → PROT</text>
  <text x="230" y="270" fill="currentColor">exec 0x1234 → OK, PA 0x20234 · write 0x1234 → PROT</text>
  <text x="230" y="314" fill="currentColor">read 0x0010 (NULL 역참조) → INVALID</text>
  <text x="230" y="140" fill="currentColor" font-size="12">거절되는 region: 기존 .text 와 겹침 · 같은 호출 안에서 겹침 ·</text>
  <text x="230" y="158" fill="currentColor" font-size="12">0x8800 정렬 안 됨 · R+W+X (W^X 위반) · 0xF000+8 KB (VA 끝 넘음)</text>
</svg>
```

검사 결과는 테스트 그대로다: valid 9 페이지, W 와 X 가 같이 켜진 페이지 0, 스택 쓰기 한 번으로 dirty 1 페이지. 실패한 `mmu_map_regions` 호출 뒤에는 테이블이 바이트 단위로 이전과 같다 (`memcmp` 체크).

- **NULL 가드**: VPN 0 을 비워 두면 NULL(+작은 오프셋) 역참조가 즉시 폴트다. macOS 도 같은 일을 훨씬 크게 한다 — `otool -l` 로 본 이 레슨 바이너리의 `__PAGEZERO` 는 `vmaddr 0x0`, `vmsize 0x100000000` (4 GB), `maxprot 0x0` (OS-B08).
- **W^X**: 쓸 수 있는 페이지는 실행 불가. 공격자가 버퍼에 넣은 코드를 못 돌린다. Apple Silicon 은 사용자 공간 JIT 에도 이걸 강제한다 (`MAP_JIT`, VM-A01).
- **스택 가드 페이지**: 스택 아래 한 페이지를 비우면 오버플로가 조용한 메모리 오염 대신 즉시 폴트가 된다. Cortex-M 에는 MMU 가 없어서 같은 일을 MPU 영역(ARMv7-M 은 보통 8개 영역, 크기·정렬이 2의 거듭제곱)으로 하고, 폴트는 MemManage 예외로 온다 (BM-A08, ARCH-M15, RTOS-M09).

**진짜 OS 에서는 어떤 시그널이 오나 — macOS 실측** (`demo_fault_kinds.c`, 자식 프로세스에서 각각 일으키고 `SA_SIGINFO` 핸들러가 받은 값을 exit status 로 넘김, 두 번 실행해 같은 결과):

<!-- code: demo_fault_kinds.c -->

```text
page size = 16384
A. read unmapped page   (INVALID)  SIGSEGV si_code=2 (SEGV_ACCERR) si_addr in page: yes
B. write read-only page (PROT W)   SIGBUS  si_code=1 (BUS_ADRALN ) si_addr in page: yes
C. read PROT_NONE page  (PROT R)   SIGBUS  si_code=1 (BUS_ADRALN ) si_addr in page: yes
D. exec RW page         (PROT X)   SIGBUS  si_code=1 (BUS_ADRALN ) si_addr in page: yes
E. read *(char*)0x10    (NULL)     SIGSEGV si_code=2 (SEGV_ACCERR) si_addr in page: yes
```

예상과 다른 점이 둘 있다. ① macOS(이 맥, arm64)는 **권한 위반을 SIGBUS** 로 보냈다 (POSIX 교과서식 기대는 SIGSEGV + `SEGV_ACCERR`). ② 미할당 페이지인데 `si_code` 가 `SEGV_MAPERR`(1) 이 아니라 `SEGV_ACCERR`(2) 였다 — 일부러 먼 주소 4개(0x600000000000 등)를 더 찔러 봐도 모두 code=2. 반면 `si_addr` 는 매번 정확했다. 결론: **시그널 번호와 `si_code` 로 폴트 종류를 가르는 코드는 이식성이 없다.** 리눅스 man 페이지(sigaction(2))는 `SEGV_MAPERR` = "address not mapped", `SEGV_ACCERR` = "invalid permissions" 로 나누고 리눅스는 보통 그렇게 준다 (이 레슨에서는 리눅스 실측을 하지 않았다). 사용자 공간에서 폴트를 직접 처리하는 기법(mprotect + 핸들러)은 OS-A06 에서.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 52/52 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_asan && ./sol_asan   # 52/52, 경고 0
```

실제 출력:

```text
random: 15000 translations, 0 mismatches (OK=2377 RANGE=1638 INVALID=6794 PROT=3407 BADPFN=784)
OSTEP fixture 1 (-s 0 -c):
  ... (위 5줄과 같음)
OSTEP fixture 2 (-P 1k -a 16k -p 32k -v -u 75 -s 2 -c):
  VA 0x00000186  VPN 0     mine 0x00007986 ostep 0x00007986 match
  VA 0x000014cf  VPN 5     mine 0x00004ccf ostep 0x00004ccf match
  ... (5/5 match)
52/52 passed
```

7번 연속 실행 모두 52/52 (난수는 고정 seed xorshift32 라 매번 같은 숫자), 레슨 폴더와 `/` 두 곳에서 실행해도 같다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 범위 검사 없이 `pt[va >> off]` | VA 가 주소 공간 밖이면 배열 밖 읽기 (UB). OSTEP 시뮬레이터가 `-A 16384` 에서 정확히 이렇게 IndexError 로 죽는다 | `if (va >= asize) return XL_RANGE;` 를 맨 앞에 |
| `log2_exact(0)` 을 0 으로 | `0 & (0-1) == 0` 이라 "2의 거듭제곱" 으로 통과 → `pgsize 0` 으로 나누기/시프트 | `x == 0` 을 따로 거절 |
| offset 을 `va % pgsize`, PA 를 `pfn * pgsize + off` 로 하고 pgsize 검사 안 함 | 2의 거듭제곱이 아니면 시프트 버전과 결과가 달라지고, 하드웨어는 시프트로만 한다 | `mmu_init` 에서 거듭제곱 검사, 본문은 시프트/마스크 |
| 폴트에서도 A 를 켬 / 권한 위반 쓰기에서 D 를 켬 | 교체 정책이 "쓰인 페이지" 로 오판, 바뀌지 않은 페이지를 writeback | 모든 검사 통과 후에만 `|= PTE_A`, 쓰기면 `| PTE_D` |
| PTE 를 값으로 복사해서 A/D 를 켬 (`uint32_t e = pt[v]; e |= PTE_A;`) | 복사본만 바뀌고 테이블은 그대로 | `uint32_t *pte = &m->pt[v];` |
| `(uint32_t)pfn << 10` 으로 PA 를 만듦 | PTE 조립과 PA 조립을 헷갈림. PA 는 `pfn << off_bits` (페이지 크기만큼) | `make_pa` 와 `pte_make` 를 분리 |
| PA 를 `uint32_t` 로 | Sv32 도 22+12 = 34비트 PA. 32비트 CPU 라도 물리 주소는 더 넓을 수 있다 (ARM LPAE 40비트) | `uint64_t pa` |
| map 을 region 마다 바로 씀 | 3번째 region 이 틀리면 앞의 2개만 매핑된 반쪽 테이블 | 검사 루프와 쓰기 루프 분리 (all-or-nothing) |
| 범위 검사를 `va + len > asize` | `va + len` 이 오버플로하면 통과 | `len > asize - va` (앞에서 `va <= asize` 확인) |
| SIGSEGV 의 `si_code` 로 미할당/권한을 구분 | macOS 실측: 미할당도 `SEGV_ACCERR`, 권한 위반은 SIGBUS | 폴트 주소(`si_addr`)를 자기 매핑 정보와 대조 |

## 8. 면접 Q&A

<details>
<summary>Q1. 32비트 주소, 4 KB 페이지, PTE 4 바이트면 선형 페이지 테이블은 얼마나 크죠? 64비트면?</summary>

offset 12비트, VPN 20비트 → 2^20 항목 × 4 B = **4 MB, 프로세스마다**. 대부분 비어 있어도(주소 공간은 sparse) 배열이라 다 잡아야 한다. 48비트 VA + 8 B PTE 면 2^36 × 8 = 512 GB — 불가능. 해결: 다단계 테이블(빈 구간의 하위 테이블을 아예 안 만듦, OS-M01·ARCH-A01), 큰 페이지(16 KB 면 32비트에서 1 MB — Apple Silicon 이 16 KB), 역 페이지 테이블/해시. 꼬리: "페이지를 그냥 크게 하면?" → 내부 단편화(작은 할당도 한 페이지), COW/스왑 단위가 커짐, 대신 TLB 도달 범위(reach)가 커진다 (ARCH-A02).

</details>

<details>
<summary>Q2. VA 0x3229, 4 KB 페이지, PT[3] = PFN 6 이면 PA 는? 과정을 말로 설명해 보세요.</summary>

offset = 아래 12비트 = 0x229, VPN = 0x3229 >> 12 = 3. PT[3] 이 valid 이고 읽기 권한이 있으면 PFN 6. PA = (6 << 12) | 0x229 = **0x6229**. offset 은 변환되지 않는다 — 페이지 안의 위치는 가상이든 물리든 같다. 하드웨어는 PTBR(페이지 테이블 시작 물리 주소) + VPN × PTE 크기에서 PTE 를 읽는다 → 메모리 접근 1번당 PTE 읽기 1번이 더 붙는다 (OSTEP 18.5: 그래서 TLB). OSTEP 시뮬레이터 `-s 0 -c` 의 첫 줄이 정확히 이것이다 (`--> 00006229 [VPN 3]`).

</details>

<details>
<summary>Q3. "미할당(invalid)" 폴트와 "권한(protection)" 폴트를 OS 는 왜 구분하죠?</summary>

처리가 다르다. INVALID 인데 그 주소가 프로세스의 정상 영역(VMA) 안이면 **정상 상황**: 요구 페이징(첫 접근에 프레임 할당, 연습 5), 스택 자동 확장, swap-in, 파일 매핑 읽기. 영역 밖이면 SIGSEGV. PROT 는: 읽기 전용으로 공유 중인 COW 페이지에 쓰기면 복사해서 쓰기 가능하게 만들고 재실행(OS-M03), 진짜 읽기 전용에 쓰기면 SIGSEGV. 하드웨어가 둘을 나눠 준다 (x86 #PF 에러코드 P 비트, ARM DFSC translation vs permission fault, RISC-V 는 같은 page fault 라 커널이 PTE 를 다시 본다). 꼬리: "valid 비트와 present 비트는 같나?" → OSTEP 은 나눈다: valid = 주소 공간에 속함, present = 지금 물리 메모리에 있음. x86 하드웨어에는 P 하나뿐이고, "속하지만 swap 됨" 은 OS 가 P=0 인 PTE 의 나머지 비트(swap 위치)나 VMA 로 기억한다. 이 레슨의 INVALID 는 둘을 합친 것이다.

</details>

<details>
<summary>Q4. accessed / dirty 비트는 누가 켜고 누가 지우나요? 어디에 쓰죠?</summary>

켜기: x86 은 하드웨어가 페이지 워크 중에 원자적으로. AArch64 v8.0 은 AF=0 인 페이지 접근 시 access flag fault → OS 가 켬 (v8.1 FEAT_HAFDBS 부터 하드웨어 갱신 가능, dirty 는 DBM 비트 + 읽기 전용 표시를 이용). RISC-V 는 하드웨어 갱신(Svadu)이나 page fault(Svade) 둘 다 허용. 지우기: 항상 OS. 용도: A → 교체 정책(Clock 이 A 를 보고 지우며 돈다, OS-B11; 연습 3), working-set 추정. D → 페이지를 버리기 전에 디스크·플래시에 써야 하는지 (연습 4). 꼬리: "OS 가 A 를 지운 뒤 할 일은?" → 그 PTE 가 TLB 에 캐시돼 있으면 하드웨어가 A 를 다시 안 켤 수 있으니 TLB 무효화가 필요할 수 있다 (OS-M01). SSD 연결: FTL 의 매핑 테이블 캐시(DFTL)도 "더티 매핑 페이지" 를 추적해 플러시한다 (MEM-A09).

</details>

<details>
<summary>Q5. 왜 폴트가 나면 A/D 를 안 켜고, *pa 도 안 쓰나요?</summary>

폴트는 **접근이 일어나지 않았다** 는 뜻이다. 그 명령은 OS 가 고친 뒤 처음부터 다시 실행된다(precise exception, 재실행 가능). 그때 성공하면 그때 켜진다. 막힌 쓰기에 D 를 켜면 바뀌지 않은 페이지를 writeback 하고, A 를 켜면 교체 정책이 쓰이지 않은 페이지를 아낀다. `*pa` 를 안 쓰는 것도 같은 계약 — 호출자가 실패 결과를 써 버리는 실수를 막는다 (테스트: 센티널 0xDEAD 유지). 꼬리: "재실행은 어떻게?" → 페이지 폴트는 정밀(precise) 예외라 저장된 PC 가 폴트 난 명령 자체를 가리킨다. 핸들러가 매핑을 고치고 돌아가면 그 명령이 처음부터 다시 실행된다 (연습 5 의 마지막 `return mmu_translate(...)`).

</details>

<details>
<summary>Q6. MMU 가 없는 Cortex-M 에서는 이 중 무엇이 남나요?</summary>

변환이 없다 — VA = PA, 링커 스크립트가 주소를 정한다 (OS-B08 의 ASLR 도 없다). 남는 건 **보호** 다: MPU 가 영역별 R/W/XN 과 특권 수준을 검사해 위반이면 MemManage 폴트(MMFSR 의 DACCVIOL/IACCVIOL, MMFAR 에 주소). 이 레슨 Part C 의 NULL 가드·스택 가드·W^X 를 MPU 영역으로 그대로 옮길 수 있다 (BM-A08, SEC-M08). 차이: ARMv7-M MPU 는 영역 8개 정도에 크기·정렬이 2의 거듭제곱(최소 32 B), ARMv8-M 은 base/limit 방식(32 B 단위). 페이지 테이블처럼 "수천 개의 작은 매핑" 은 불가능하니 펌웨어는 큰 영역 몇 개로 설계한다. Cortex-R 도 MPU, Cortex-A 는 MMU (SYSD-B10, SYSD-B12).

</details>

<details>
<summary>Q7. 시뮬레이터가 맞는지 어떻게 검증했나요?</summary>

세 겹이다. ① 손 계산(OSTEP 그림 18.3 의 VA 21 → PA 117, 0x3229 → 0x6229)과 폴트 종류별 경우. ② 독립 구현과의 무작위 비교 — 기준 구현은 시프트/AND 를 안 쓰고 `/`, `%` 로만 짰고, 고정 seed xorshift32 로 300 설정 × 50 접근 = 15,000 번, 결과·PA·접근 뒤 PTE 까지 0 불일치, 다섯 종류 모두 출현. ③ 외부 기준 — OSTEP 공식 숙제 시뮬레이터의 `-c` 답을 파싱해서 20개 설정 144개 주소 모두 일치. 대조기 자체도 일부러 틀린 답을 넣어 MISMATCH 를 내는지 확인했다. 꼬리: "OSTEP 과 다른 점?" → OSTEP 은 보호 비트가 없고, 범위 밖 VA 를 검사하지 않아(`-A 16384`) IndexError 로 죽는다. 내 것은 `XL_RANGE`.

</details>

## 9. 추가 연습

### 연습 1 — 선형 페이지 테이블 크기 (OSTEP 숙제 Q1)

`pt_bytes(as_bits, pg_bits, pte_bytes, &entries)`. 32/12/4, 32/14/4, 48/12/8, 39/12/8 의 크기와, 숙제 Q1 의 `-P 1k -a 1m/2m/4m`, `-P 2k/4k -a 1m` 항목 수를 맞혀라.

<details>
<summary>답 보기</summary>

`entries = 1 << (as_bits - pg_bits)`, `bytes = entries * pte_bytes`. 4 MiB, 1 MiB, 512 GiB, 1 GiB(Sv39 를 평평하게 펼치면). Q1: 1024, 2048, 4096 (주소 공간이 2배면 항목 2배), 512, 256 (페이지가 2배면 항목 절반). OSTEP 시뮬레이터 `-v -n 0` 의 항목 줄 수를 세어 실제로 확인했다: `-P 1k -a 1m` → 1024, `-a 2m` → 2048, `-a 4m` → 4096, `-P 2k -a 1m` → 512, `-P 4k -a 1m` → 256.

</details>

### 연습 2 — Sv32 의 2단계 분해 (OS-M01 예고)

32비트 VA 를 `VPN[1]`(10) | `VPN[0]`(10) | offset(12) 로 나누는 `sv32_split` 과 되돌리는 `sv32_join`. 0x12345678 과 0xFFFFFFFF 를 나누고, 10만 개 값에서 join(split(x)) == x.

<details>
<summary>답 보기</summary>

`vpn1 = va >> 22`, `vpn0 = (va >> 12) & 0x3FF`, `off = va & 0xFFF`. 0x12345678 → 0x48, 0x345, 0x678. 0xFFFFFFFF → 0x3FF, 0x3FF, 0xFFF. 1단계 테이블(1024 항목 × 4 B = 4 KB = 딱 한 페이지)의 VPN[1] 칸이 2단계 테이블을 가리키고, 비어 있는 4 MB 구간은 2단계 테이블 자체를 안 만든다 — 이게 4 MB → 수 KB 로 줄어드는 이유다.

</details>

### 연습 3 — accessed 스캔 (Clock 의 재료)

`scan_accessed(m, cold, cap)`: valid 페이지 중 지난 스캔 이후 A 가 꺼진 VPN 목록을 돌려주고, 모든 A 를 지운다. VPN 0, 3 을 읽은 뒤 스캔 → {1} (VPN 2 는 invalid), VPN 1 을 읽은 뒤 스캔 → {0, 3}.

<details>
<summary>답 보기</summary>

valid 가 아니면 건너뛰고, A 가 0 이면 목록에 넣고, 어느 쪽이든 `*e &= ~PTE_A`. 두 번째 스캔에서 0, 3 이 "차가운" 이유: 첫 스캔이 A 를 지웠고 그 뒤 안 쓰였다. 이게 Clock 알고리즘의 "두 번째 기회" 이고 리눅스 kswapd 의 aging 도 같은 아이디어다 — 교체 정책 자체는 OS-B11.

</details>

### 연습 4 — dirty 페이지 기록 (writeback)

`writeback_dirty(m, out, cap)`: D 가 켜진 valid 페이지를 모아 D 를 지우고 개수를 돌려준다. VPN 0, 3 에 쓰기, 1 은 읽기 → {0, 3}; 다시 부르면 0; A 는 그대로.

<details>
<summary>답 보기</summary>

`(*e & (PTE_V|PTE_D)) == (PTE_V|PTE_D)` 면 기록 후 `*e &= ~PTE_D`. A 를 건드리지 않는 이유: "썼다(디스크와 같아졌다)" 와 "최근에 쓰였다" 는 다른 정보다. 순서가 중요하다: 내용을 다 쓴 **뒤에** D 를 지우면 그 사이에 들어온 새 쓰기의 흔적까지 지워진다. 그래서 D 를 먼저 지우고(필요하면 TLB 무효화) 그다음 쓴다. SSD 펌웨어의 매핑 테이블 플러시도 같은 순서 문제를 가진다.

</details>

### 연습 5 — 요구 페이징 (demand paging)

`access_demand(m, pool, may_map, va, acc, &pa, &faults)`: translate 가 INVALID 이고 그 VPN 이 할당 허용 영역(`may_map`)이면 프레임 풀에서 하나 받아 V|R|W 로 매핑하고 **재실행**. 허용 영역이 아니면 그냥 INVALID(segfault). 프레임이 없으면 INVALID (교체는 OS-B11). 4 페이지 중 0..2 허용, 프레임 2개로 시나리오를 테스트.

<details>
<summary>답 보기</summary>

```c
xl_t r = mmu_translate(m, va, acc, pa);
if (r != XL_INVALID || !may_map[va >> m->off_bits]) return r;
(*faults)++;
if (fp->next == fp->end) return XL_INVALID;
m->pt[va >> m->off_bits] = pte_make(fp->next++, PTE_V | PTE_R | PTE_W);
return mmu_translate(m, va, acc, pa);
```

결과: 첫 쓰기 → 폴트 1, PA 0x5100 (PFN 5). 같은 페이지 재접근 → 폴트 없음. VPN 1 → 폴트 2, PFN 6. VPN 3 → segfault (폴트 카운트 안 늘어남 — OS 의 "정상 폴트" 가 아님). VPN 2 → 폴트 3 인데 프레임 고갈. `may_map` 이 OS 의 VMA 목록 역할이고, Q3 의 "INVALID 를 정상/비정상으로 나누는 일" 이 바로 이 `if` 다. malloc 이 큰 영역을 받아도 RSS 가 바로 안 늘어나는 이유(OS-B08, OS-B09).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 20/20 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan   # 20/20, 경고 0
```

## 10. 핵심 정리

- 선형 페이지 테이블 = VPN 을 인덱스로 쓰는 배열. `VPN = va >> off_bits`, `offset = va & (pgsize-1)`, `PA = (PFN << off_bits) | offset`. offset 은 변환되지 않는다.
- 크기 = (주소 공간 / 페이지) × PTE 크기. 32비트·4 KB·4 B = 4 MB/프로세스 → 다단계 테이블(OS-M01). Apple Silicon 페이지는 16 KB (실측).
- translate 검사 순서 = 폴트 분류: **RANGE → INVALID(V=0) → PROT(R/W/X) → BADPFN(물리 밖, 하드웨어에선 버스/access fault)**. 폴트면 PTE·`*pa` 불변.
- A/D 는 성공한 접근에서만: A = 최근 사용(교체 정책), D = writeback 필요. 하드웨어가 켜거나(x86, Svadu, HAFDBS) 폴트로 OS 가 켠다(ARMv8.0 AF, Svade). 지우는 건 항상 OS.
- INVALID vs PROT 구분이 요구 페이징·스택 확장·COW·segfault 를 가른다. 그 판단의 나머지 절반은 OS 의 영역 정보(VMA).
- OSTEP `paging-linear-translate.py -c` 와 20개 설정 144 주소 일치. OSTEP 시뮬레이터는 범위 밖 VA 에서 IndexError, 보호 비트 없음, 물리 > 주소 공간 강제.
- macOS 실측: 미할당 → SIGSEGV(si_code 는 MAPERR 아닌 ACCERR), 권한 위반 → **SIGBUS**. si_code 로 폴트 종류를 가르지 말 것.
- 펌웨어: NULL 가드, W^X, 스택 가드 페이지를 매핑으로 표현하고 all-or-nothing 으로 설치. Cortex-M 은 변환 없이 MPU 로 보호만 (BM-A08).

## 참고자료

- OSTEP 18장 *Paging: Introduction* — 그림 18.1–18.3 (64 B 예, VA 21 → 117), 18.5 x86 PTE, valid/protection/present/dirty/reference 비트, 숙제 Q1–Q4: <https://pages.cs.wisc.edu/~remzi/OSTEP/vm-paging.pdf> (로컬: `operating-system-three-easy-pieces/book-md/C18_paging_introduction.md`, Don 노트 `notes/2026-09-30_C18_paging_intro.md`)
- OSTEP 19장 TLB, 20장 *Smaller Tables*(다단계), 21장 swap 메커니즘(present 비트, 페이지 폴트 처리): <https://pages.cs.wisc.edu/~remzi/OSTEP/>
- OSTEP 숙제 시뮬레이터 `vm-paging/paging-linear-translate.py` (로컬 `operating-system-three-easy-pieces/.tools/ostep-homework/vm-paging/`, 원본 <https://github.com/remzi-arpacidusseau/ostep-homework>)
- RISC-V Privileged Architecture — Sv32 PTE 형식(V/R/W/X/U/G/A/D, PPN), 가상 주소 변환 과정, page fault vs access fault(scause 1/5/7, 12/13/15), Svade/Svadu: <https://github.com/riscv/riscv-isa-manual>
- Arm Architecture Reference Manual for A-profile — VMSAv8-64 변환 테이블 디스크립터(AF, AP, XN, DBM), ESR_ELx DFSC(translation / access flag / permission fault, external abort): <https://developer.arm.com/documentation/ddi0487/latest>
- Intel SDM Vol. 3A 4장 *Paging* — PTE 의 P/RW/US/A/D, #PF 에러코드: <https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html>
- ARMv7-M Architecture Reference Manual — MPU(영역 크기·정렬), MemManage fault, MMFSR/MMFAR: <https://developer.arm.com/documentation/ddi0403/latest>
- POSIX `<signal.h>` — `SEGV_MAPERR`, `SEGV_ACCERR`, `BUS_*` si_code: <https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/signal.h.html>; Linux sigaction(2): <https://man7.org/linux/man-pages/man2/sigaction.2.html>; macOS 헤더 `MacOSX26.4.sdk/usr/include/sys/signal.h` (BUS_ADRERR/BUS_OBJERR 옆에 `-NOTIMP` 주석)
- 관련 레슨: OS-B08 (주소 공간·`__PAGEZERO`·ASLR), OS-B09 (mmap), OS-B11 (페이지 교체 — A 비트 사용), OS-M01 (다단계 테이블 map/unmap/protect + TLB 무효화), OS-M03 (COW 와 페이지 폴트 관찰), OS-A06 (mprotect + SIGSEGV 사용자 공간 폴트 처리), ARCH-A01 (MMU 테이블 워커), ARCH-A02 (TLB·ASID), ARCH-M15 (MPU 영역 계산기), ARM-A03 (AArch64 MMU 켜기), RISCV-M11 (PMP), RISCV-M12 (Sv39 워커·A/D), RISCV-A01 (satp·sfence.vma), BM-A08 (MPU: NULL 트랩·스택 가드·XN), BM-M13 (fault 처리 정책), RTOS-M09 (스택 오버플로 탐지), SEC-M08 (MPU 권한 분리), MEM-A09 (DFTL 매핑 캐시), VM-A01 (Apple Silicon JIT 와 W^X), SYSD-B10/B12 (코어 선택·격리 설계)
