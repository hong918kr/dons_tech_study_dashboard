# Ch.20 더 작은 페이지 테이블 — 큰 페이지, 하이브리드, 다단계, 역페이지 테이블

> 📖 원문: [20. Paging: Smaller Tables](../book-md/C20_paging_smaller_tables.md) · [PDF p.226](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=226) · ⏱️ 읽기 약 70분 · 🔗 선행: [Ch.18 페이징 입문](2026-09-30_C18_paging_intro.md), [Ch.19 TLB](2026-09-30_C19_tlb.md), [Ch.16 세그멘테이션](2026-09-30_C16_segmentation.md)

## 0. 한눈에 보기

> **CRUX — "Simple array-based page tables (usually called linear page tables) are too big, taking up far too much memory on typical systems. How can we make page tables smaller? What are the key ideas? What inefficiencies arise as a result of these new data structures?"**
> (선형 페이지 테이블은 너무 크다. 어떻게 작게 만들까? 핵심 아이디어는? 새 자료구조는 어떤 비효율을 낳나?)

- 32-bit/4KB/4B PTE 선형 테이블 = 프로세스당 **4MB**, 그런데 대부분은 **invalid PTE** (힙과 스택 사이 빈 공간).
- 해법 4가지: (1) **큰 페이지** — 단순하지만 내부 단편화, (2) **세그먼트+페이징 하이브리드** — 세그먼트마다 테이블, (3) **다단계 페이지 테이블** — 테이블을 페이지 단위로 자르고 빈 조각은 아예 안 만든다 (**x86, ARM 등 사실상 모든 현대 CPU**), (4) **역페이지 테이블** — 물리 프레임당 엔트리 하나, 시스템 전체 한 개.
- 공짜는 없다: 다단계는 **TLB 미스 때 메모리 접근이 레벨 수만큼** 늘어난다 (**시간-공간 트레이드오프**). 그래서 Ch.19의 TLB가 더 중요해진다.

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 선형 페이지 테이블 | VPN으로 인덱싱하는 PTE 배열 | 32-bit/4KB면 4MB |
| 내부 단편화 | 할당 단위(페이지) 안의 낭비 | 1MB 페이지에 10KB만 사용 |
| 다중 페이지 크기 | 일부 영역만 큰 페이지 | x86-64 4KB/2MB/1GB |
| 하이브리드 (paging + segments) | 세그먼트마다 페이지 테이블, base=테이블 주소, bounds=테이블 끝 | Multics (Jack Dennis) |
| 다단계 페이지 테이블 | 테이블을 페이지 크기로 쪼개 트리로 | x86-64 4-level, arm64 3~4-level |
| 페이지 디렉터리 (page directory) | "페이지 테이블의 페이지"들을 가리키는 상위 테이블 | 책의 목차 |
| PDE | 페이지 디렉터리 엔트리: valid + PFN | valid=0이면 그 범위 전체 없음 |
| PDIndex / PTIndex | VPN 상위/하위 비트 → 각 레벨 인덱스 | VPN 8bit = 4+4 |
| PDBR | 최상위 디렉터리의 물리 주소 레지스터 | x86 CR3, arm64 TTBR0/1 |
| 시간-공간 트레이드오프 | 작게 만들면 느려지고, 빠르게 하면 커진다 | 2단계 = 미스 시 2회 접근 |
| 역페이지 테이블 (inverted) | 물리 프레임마다 엔트리 (PID, VPN) | PowerPC 해시 페이지 테이블 |
| 해시 페이지 테이블 | (PID, VPN) 해시로 역테이블 검색 가속 | 선형 탐색 회피 |
| 페이지 테이블 스왑 | 테이블을 커널 가상 메모리에 두고 디스크로 | VAX/VMS (Ch.23) |

## 2. 문제 다시 보기

```text
32-bit 주소, 4KB 페이지, PTE 4B
VPN 비트 = 32 - 12 = 20  →  PTE 2^20 개  →  2^20 × 4B = 4MB / 프로세스
프로세스 100개 → 400MB
```

> **원문 각주의 TIP**: 해법으로 가기 전에 **문제를 정확히 이해하라.** 문제를 이해하면 해법을 스스로 유도할 수 있는 경우가 많다. 여기서 문제는 "선형 테이블이 너무 크다", 더 정확히는 "**쓰지 않는 영역까지 PTE 자리를 차지한다**"이다.

## 3. 간단한 해법: 큰 페이지 (20.1)

32-bit에서 페이지를 16KB로 키우면:

```text
offset 14bit, VPN 18bit → 2^18 × 4B = 1MB   (4KB 대비 1/4 — 페이지 크기 4배 증가와 정확히 반비례)
```

새 예제 — 극단으로 가 보자. 4MB 페이지면 VPN 10비트 → 2^10 × 4B = **4KB**. 테이블은 아주 작아진다. 하지만 프로세스가 코드 20KB, 힙 100KB, 스택 8KB만 쓴다면 최소 3페이지 × 4MB = **12MB를 할당하고 128KB만 사용** → 99%가 **내부 단편화**. 그래서 범용 기본값은 4KB(x86), 8KB(SPARCv9), 16KB(Apple arm64) 정도로 작게 둔다.

> **ASIDE — 다중 페이지 크기 (MULTIPLE PAGE SIZES)**
> MIPS, SPARC, x86-64 등은 여러 페이지 크기를 지원한다. 평소엔 작은 페이지, "똑똑한" 애플리케이션(DBMS 등)이 요청하면 큰 데이터 구조를 4MB 같은 **큰 페이지 하나**에 담는다. 주된 목적은 테이블 절약이 아니라 **TLB 압력 감소**(엔트리 하나로 넓게 커버). 단, OS VM 관리자는 훨씬 복잡해진다 [Navarro+02] — 그래서 리눅스도 hugetlbfs처럼 **명시적 인터페이스**로 먼저 제공했다.

## 4. 하이브리드: 페이징 + 세그먼트 (20.2)

> **TIP — 하이브리드를 써라 (USE HYBRIDS)**: 좋은데 서로 반대인 두 아이디어가 있으면 합쳐서 양쪽 장점을 얻을 수 있는지 보라. (단 Zeedonk(얼룩말×당나귀) 같은 하이브리드도 있으니 주의.)

16KB 주소 공간, 1KB 페이지의 선형 테이블 (Figure 20.2) — 16개 중 4개만 valid:

```text
VPN  PFN  valid prot  present dirty
 0    10    1   r-x     1      0     ← code
 1-3   -    0                         
 4    23    1   rw-     1      1     ← heap
 5-13  -    0           (9칸 낭비)
14    28    1   rw-     1      1     ← stack
15     4    1   rw-     1      1     ← stack
```

아이디어(Multics, Jack Dennis): **세그먼트(code/heap/stack)마다 페이지 테이블을 따로** 두자. MMU의 세그먼트 base 레지스터는 이제 **그 세그먼트 페이지 테이블의 물리 주소**를, bounds 레지스터는 **테이블의 끝(유효 페이지 수)** 을 담는다.

32-bit, 4KB 페이지, 상위 2비트 = 세그먼트 번호(00 미사용, 01 code, 10 heap, 11 stack):

```text
 31 30 | 29 ............................ 12 | 11 ........ 0
  Seg  |            VPN (18 bit)            |  offset (12)

SN           = (VA & SEG_MASK) >> SN_SHIFT       // SEG_MASK = 0xC0000000, SN_SHIFT = 30
VPN          = (VA & VPN_MASK) >> VPN_SHIFT      // VPN_MASK = 0x3FFFF000, VPN_SHIFT = 12
AddressOfPTE = Base[SN] + (VPN * sizeof(PTE))    // + VPN >= Bounds[SN] 이면 예외
```

새 예제: code 세그먼트가 3페이지만 쓴다 → `Bounds[1] = 3`, code 테이블은 PTE 3개(12B)뿐.

```text
VA 0x40002ABC: SN = 01 (code), VPN = 0x00002, offset = 0xABC
   2 < Bounds[1]=3 → OK → PTE 주소 = Base[1] + 2×4
VA 0x40005000: VPN = 5 ≥ 3 → 예외 (세그먼트 밖)
```

문제점:

1. **세그멘테이션의 경직성이 그대로**: 힙이 크고 듬성듬성(sparse)하면 힙 테이블 안에서 여전히 낭비.
2. **외부 단편화의 귀환**: 페이지 테이블 크기가 제각각(PTE 개수의 배수)이라 연속 물리 공간 찾기가 다시 어려워진다.

## 5. 다단계 페이지 테이블 (20.3)

### 5.1 아이디어

1. 선형 페이지 테이블을 **페이지 크기 조각**으로 자른다.
2. 한 조각(=PTE 한 페이지분) 전체가 invalid면 **그 조각은 아예 할당하지 않는다.**
3. 어떤 조각이 있는지/어디 있는지는 **페이지 디렉터리**가 기록한다.

```svg
<svg viewBox="0 0 700 330" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C20-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--accent)"/></marker></defs>
  <text x="120" y="20" text-anchor="middle" fill="currentColor" font-weight="600">선형 페이지 테이블</text>
  <text x="510" y="20" text-anchor="middle" fill="currentColor" font-weight="600">2단계 페이지 테이블</text>
  <g stroke="currentColor" fill="none">
    <rect x="60" y="35" width="120" height="65" style="fill:var(--accent-soft)"/>
    <rect x="60" y="100" width="120" height="65"/>
    <rect x="60" y="165" width="120" height="65"/>
    <rect x="60" y="230" width="120" height="65" style="fill:var(--accent-soft)"/>
  </g>
  <g fill="currentColor" font-size="11.5">
    <text x="66" y="52">1 rx 12</text><text x="66" y="67">1 rx 13</text><text x="66" y="82">0 - -</text><text x="66" y="97">1 rw 100</text>
    <text x="66" y="130">0 - -  (전부 invalid)</text>
    <text x="66" y="195">0 - -  (전부 invalid)</text>
    <text x="66" y="247">0 - -</text><text x="66" y="262">0 - -</text><text x="66" y="277">1 rw 86</text><text x="66" y="292">1 rw 15</text>
  </g>
  <g fill="currentColor" font-size="11" text-anchor="end">
    <text x="54" y="70">PFN 201</text><text x="54" y="135">PFN 202</text><text x="54" y="200">PFN 203</text><text x="54" y="265">PFN 204</text>
  </g>
  <text x="120" y="318" text-anchor="middle" fill="currentColor" font-size="12">4 페이지 모두 메모리에 있어야 함</text>
  <g stroke="currentColor" fill="none">
    <rect x="300" y="100" width="110" height="100" style="stroke:var(--accent)" stroke-width="1.6"/>
    <rect x="560" y="35" width="120" height="65" style="fill:var(--accent-soft)"/>
    <rect x="560" y="230" width="120" height="65" style="fill:var(--accent-soft)"/>
    <rect x="560" y="120" width="120" height="30" stroke-dasharray="4 3"/>
    <rect x="560" y="165" width="120" height="30" stroke-dasharray="4 3"/>
  </g>
  <text x="355" y="92" text-anchor="middle" fill="currentColor" font-size="12">페이지 디렉터리 (PFN 200)</text>
  <g fill="currentColor" font-size="12">
    <text x="310" y="122">1 → 201</text><text x="310" y="145">0  -</text><text x="310" y="168">0  -</text><text x="310" y="191">1 → 204</text>
  </g>
  <g fill="currentColor" font-size="11.5">
    <text x="566" y="52">1 rx 12</text><text x="566" y="67">1 rx 13</text><text x="566" y="82">0 - -</text><text x="566" y="97">1 rw 100</text>
    <text x="566" y="139" font-size="10.5">PT 1 페이지: 할당 안 함</text>
    <text x="566" y="184" font-size="10.5">PT 2 페이지: 할당 안 함</text>
    <text x="566" y="247">0 - -</text><text x="566" y="262">0 - -</text><text x="566" y="277">1 rw 86</text><text x="566" y="292">1 rw 15</text>
  </g>
  <g style="stroke:var(--accent)" stroke-width="1.6" fill="none" marker-end="url(#C20-arrow)">
    <path d="M375,118 C470,118 480,67 556,67"/>
    <path d="M375,187 C470,187 480,262 556,262"/>
  </g>
  <g fill="currentColor" font-size="11" text-anchor="end">
    <text x="554" y="30">PFN 201</text><text x="554" y="226">PFN 204</text>
  </g>
  <text x="510" y="318" text-anchor="middle" fill="currentColor" font-size="12">디렉터리 1 + 테이블 2 = 3 페이지 (나머지 2 페이지 절약)</text>
</svg>
```

**PDE** 는 PTE와 비슷하게 valid 비트 + PFN을 가진다. 의미는 조금 다르다: PDE valid = "가리키는 PT 페이지 안에 **적어도 하나의** valid PTE가 있다". PDE invalid면 나머지 필드는 의미 없음.

장점:

- **쓰는 만큼만** 테이블 공간 할당 → 희소 주소 공간에 강함.
- 테이블 조각이 모두 **페이지 크기**라 메모리 관리가 쉽다: 테이블이 커지면 아무 빈 프레임이나 하나 더 잡으면 된다. 반면 선형 테이블(4MB)은 **연속 물리 메모리**가 필요 — 큰 연속 공간 찾기는 어렵다. 간접 참조(indirection) 하나 덕분에 테이블 조각을 물리 메모리 아무 데나 둘 수 있다.

단점:

- **TLB 미스 시 메모리 접근 2회**(PDE + PTE) — 선형은 1회. 공간을 얻고 시간을 냈다.
- **복잡도**: HW든 OS든 조회 로직이 복잡해진다.

> **TIP — 시간-공간 트레이드오프를 이해하라**: 자료구조 접근을 빠르게 하려면 보통 공간을 더 내야 하고, 그 반대도 마찬가지.

### 5.2 자세한 예제: 16KB 주소 공간, 64B 페이지 (원문)

- 14-bit VA = **VPN 8비트 + offset 6비트**. 선형이면 PTE 2^8 = 256개 × 4B = **1KB** = 64B 페이지 **16개**.
- 한 페이지(64B)에 PTE 16개 → PT 페이지 16개 → 디렉터리 엔트리 16개 → **PDIndex 4비트**(VPN 상위), **PTIndex 4비트**(VPN 하위).
- 사용 중: VPN 0,1 (code), 4,5 (heap), 254,255 (stack).

```text
 13 12 11 10 | 9  8  7  6 | 5  4  3  2  1  0
   PDIndex   |  PTIndex   |      offset
 └────── VPN (8) ───────┘

PDEAddr = PageDirBase + PDIndex × sizeof(PDE)
PTEAddr = (PDE.PFN << SHIFT) + PTIndex × sizeof(PTE)      ← PDE의 PFN은 shift해서 "주소"로 만든다
PhysAddr = (PTE.PFN << SHIFT) + offset
```

테이블 내용 (Figure 20.5):

```text
Page Directory        PT 페이지 @PFN 100 (VPN 0–15)    PT 페이지 @PFN 101 (VPN 240–255)
[0]  PFN 100 valid 1   [0] 10 r-x   [1] 23 r-x          [14] 55 rw-  (VPN 254)
[1]~[14]  invalid      [4] 80 rw-   [5] 59 rw-          [15] 45 rw-  (VPN 255)
[15] PFN 101 valid 1   나머지 invalid                    나머지 invalid
```

16 페이지 대신 **3 페이지**(디렉터리 1 + PT 2)만 할당.

### 5.3 전체 변환 따라가기: VA 0x3F80

```svg
<svg viewBox="0 0 700 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C20-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--accent)"/></marker></defs>
  <text x="20" y="22" fill="currentColor" font-weight="600">VA 0x3F80 = 11 1111 1000 0000₂</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="35" width="160" height="34" style="fill:var(--accent-soft)"/>
    <rect x="180" y="35" width="160" height="34" style="fill:var(--accent-soft)"/>
    <rect x="340" y="35" width="240" height="34"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="100" y="57">PDIndex 1111 = 15</text><text x="260" y="57">PTIndex 1110 = 14</text><text x="460" y="57">offset 000000 = 0</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="110" width="160" height="110"/>
    <rect x="250" y="110" width="170" height="110"/>
  </g>
  <text x="100" y="104" text-anchor="middle" fill="currentColor" font-size="12">Page Directory</text>
  <text x="335" y="104" text-anchor="middle" fill="currentColor" font-size="12">PT 페이지 @ PFN 101</text>
  <g fill="currentColor" font-size="12">
    <text x="30" y="130">[0]  PFN 100 · V1</text><text x="30" y="152">[1..14] invalid</text>
    <text x="30" y="205" style="fill:var(--accent)" font-weight="600">[15] PFN 101 · V1</text>
    <text x="260" y="130">[0..13] invalid</text>
    <text x="260" y="183" style="fill:var(--accent)" font-weight="600">[14] PFN 55 · V1 rw-</text>
    <text x="260" y="205">[15] PFN 45 · V1 rw-</text>
  </g>
  <rect x="25" y="190" width="150" height="22" fill="none" style="stroke:var(--accent)"/>
  <rect x="255" y="168" width="160" height="22" fill="none" style="stroke:var(--accent)"/>
  <g style="stroke:var(--accent)" stroke-width="1.6" fill="none" marker-end="url(#C20-arrow2)">
    <path d="M100,70 L100,108"/>
    <path d="M176,201 C215,201 215,140 248,140"/>
    <path d="M260,70 C300,90 300,150 300,166"/>
    <path d="M418,179 C470,179 480,250 500,250"/>
  </g>
  <rect x="500" y="232" width="185" height="40" rx="6" fill="none" style="stroke:var(--accent)" stroke-width="1.6"/>
  <text x="592" y="250" text-anchor="middle" fill="currentColor" font-size="12">PA = (55 &lt;&lt; 6) | 0</text>
  <text x="592" y="266" text-anchor="middle" fill="currentColor" font-size="12" font-weight="600">= 0x0DC0</text>
  <text x="20" y="250" fill="currentColor" font-size="12">① PDE 읽기 (메모리 접근 1)</text>
  <text x="20" y="270" fill="currentColor" font-size="12">② PTE 읽기 (메모리 접근 2)</text>
  <text x="20" y="290" fill="currentColor" font-size="12">③ 실제 데이터 읽기 (메모리 접근 3) — TLB 미스 시 총 3회</text>
</svg>
```

```text
VA     = 0x3F80 = 11 1111 1000 0000₂ (14bit)
VPN    = 0x3F80 >> 6 = 0xFE = 254       offset = 0x3F80 & 0x3F = 0
PDIndex= 254 >> 4 = 15 (1111)           PTIndex = 254 & 0xF = 14 (1110)
PD[15] = PFN 101 (valid)
PT@101[14] = PFN 55 = 0x37 (valid, rw-)
PA     = (55 << 6) | 0 = 3520 = 0x0DC0 = 00 1101 1100 0000₂   ✔ 원문과 일치
```

새 예제 2개 (8절 C 프로그램 출력과 대조):

```text
VA 0x0145: VPN = 0x0145 >> 6 = 5, offset 5 → PDI 0, PTI 5 → PD[0]=PFN 100 → PT@100[5] = PFN 59
          PA = 59×64 + 5 = 3781 = 0x0EC5
VA 0x2000: VPN = 128 → PDI 8 → PD[8] invalid → 폴트 (PT 페이지를 읽지도 않음: 메모리 접근 1회)
```

### 5.4 두 단계로 부족할 때 (More Than Two Levels)

목표는 "**모든 조각이 한 페이지에 들어가게**". 30-bit VA, 512B 페이지, 4B PTE:

```text
offset = log2(512) = 9bit,  VPN = 30 - 9 = 21bit
한 페이지의 PTE 수 = 512 / 4 = 128 → PTIndex 7bit
남은 디렉터리 인덱스 = 21 - 7 = 14bit → 디렉터리 엔트리 2^14개 × 4B = 64KB = 128 페이지!  ✗
→ 디렉터리를 또 쪼갠다:  PD Index 0 (7) | PD Index 1 (7) | PT Index (7) | offset (9)
```

```svg
<svg viewBox="0 0 700 120" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <g fill="currentColor" font-size="11">
    <text x="20" y="22">29</text><text x="676" y="22" text-anchor="end">0</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="30" width="154" height="40" style="fill:var(--accent-soft)"/>
    <rect x="174" y="30" width="154" height="40" style="fill:var(--accent-soft)"/>
    <rect x="328" y="30" width="154" height="40" style="fill:var(--accent-soft)"/>
    <rect x="482" y="30" width="198" height="40"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="97" y="55">PD Index 0 (7)</text><text x="251" y="55">PD Index 1 (7)</text>
    <text x="405" y="55">PT Index (7)</text><text x="581" y="55">offset (9)</text>
  </g>
  <g fill="currentColor" font-size="11.5">
    <text x="20" y="92">각 7비트 = 128 엔트리 × 4B = 512B = 정확히 한 페이지 → 3단계 테이블</text>
    <text x="20" y="110">TLB 미스 시: 최상위 PDE → 2단계 PDE → PTE → 데이터 = 메모리 접근 4회</text>
  </g>
</svg>
```

일반화 — 레벨 수 공식:

```text
bits_per_level = log2(page_size / PTE_size)
levels         = ceil( (VA_bits - log2(page_size)) / bits_per_level )
```

현대 CPU에 적용 (8절 프로그램 출력 [2]):

- **x86-64**: 48-bit, 4KB, 8B → 9비트/레벨, VPN 36비트 → **4레벨** (PML4 → PDPT → PD → PT). 57-bit(LA57)면 5레벨.
- **arm64 + 16KB granule (Apple)**: 11비트/레벨(16KB/8B = 2048 엔트리). 47-bit VA면 VPN 33비트 → **정확히 3레벨**, 48-bit면 4레벨인데 최상위는 **엔트리 2개짜리**.
- 큰 페이지와의 연결: x86-64에서 PD 레벨의 엔트리가 PT를 가리키지 않고 바로 **2MB 페이지**를 가리킬 수 있다(PS 비트). 한 레벨을 건너뛰니 **워크도 짧아지고 TLB coverage도 커진다.**

### 5.5 TLB와 함께 본 전체 흐름 (Figure 20.6)

```text
 1  VPN = (VA & VPN_MASK) >> SHIFT
 2  (Success, TlbEntry) = TLB_Lookup(VPN)
 3  if (Success)  → 권한 검사 후 PA = (TlbEntry.PFN << SHIFT) | Offset   // 페이지 테이블 안 봄
10  else  // TLB Miss
12      PDIndex = (VPN & PD_MASK) >> PD_SHIFT
13      PDEAddr = PDBR + PDIndex * sizeof(PDE);   PDE = AccessMemory(PDEAddr)      // 접근 1
15      if (!PDE.Valid) SEGFAULT
19      PTIndex = (VPN & PT_MASK) >> PT_SHIFT
20      PTEAddr = (PDE.PFN << SHIFT) + PTIndex * sizeof(PTE);  PTE = AccessMemory(PTEAddr)  // 접근 2
22      if (!PTE.Valid) SEGFAULT  else if (!CanAccess) PROT_FAULT
27      else TLB_Insert(VPN, PTE.PFN, PTE.ProtectBits); RetryInstruction()
```

TLB hit이면 선형이든 다단계든 성능이 **똑같다**. 차이는 미스 때만 난다. 그래서 현실의 CPU는 **페이지 워크 캐시**(PDE 같은 중간 엔트리를 캐시: Intel PML4/PDPT 캐시, ARM walk cache)를 따로 둔다.

> **TIP — 복잡성을 경계하라 (BE WARY OF COMPLEXITY)**: 좋은 시스템 설계자는 일을 해내는 **가장 덜 복잡한** 시스템을 만든다. "더 이상 뺄 것이 없을 때 완벽해진다" (생텍쥐페리).

## 6. 역페이지 테이블 (20.4)

극단적 절약: 프로세스마다 테이블을 두지 말고, **시스템 전체에 하나**, **물리 프레임마다 엔트리 하나**. 엔트리 = "이 프레임을 쓰는 프로세스(PID)와 그 가상 페이지(VPN)".

- 변환하려면 (PID, VPN)을 가진 엔트리를 **찾아야** 한다 → 선형 탐색은 너무 느리므로 **해시 테이블**을 얹는다 (PowerPC의 hashed page table).
- 새 예제 — 크기 계산: 물리 메모리 16GB, 4KB 프레임 → 2^22 = 4M 엔트리. 엔트리 16B(PID 4B + VPN 6B + 해시 체인 포인터/플래그 6B) → **64MB, 시스템 전체 고정**. 프로세스가 1000개든 1개든 같다. 반면 선형 테이블(32-bit)은 프로세스당 4MB × 1000 = 4GB.
- 대가: 공유 페이지(여러 VPN → 한 PFN) 표현이 어렵고, 해시 충돌 체인 때문에 미스 처리 시간이 들쭉날쭉.

원문의 교훈: **페이지 테이블은 그냥 자료구조다.** 작게/크게, 빠르게/느리게 마음대로 설계할 수 있다. 특히 SW 관리 TLB라면 OS가 원하는 어떤 구조든 가능하다.

## 7. 페이지 테이블 자체를 스왑하기 (20.5) & 요약 (20.6)

지금까지는 테이블이 커널 물리 메모리에 상주한다고 가정했다. 그래도 너무 크면 테이블을 **커널 가상 메모리**에 두고 메모리가 빠듯할 때 **디스크로 스왑**할 수 있다 → [Ch.23 VAX/VMS](2026-09-30_C23_vax_vms.md)에서 자세히.

정리: 테이블이 크면 TLB 미스가 빠르고(선형: 접근 1회), 작으면 미스가 느리다(다단계: 레벨 수만큼). 메모리가 부족한 옛 시스템은 작은 구조, 메모리가 넉넉하고 많은 페이지를 쓰는 워크로드는 미스를 빨리 처리하는 큰 구조가 맞다.

## 8. 직접 해보기

### 8.1 C로 만든 2단계 페이지 워크

책의 Figure 20.5 테이블을 "물리 메모리" 배열에 그대로 만들고(페이지 디렉터리는 PFN 99에 둠), 실제로 PDE/PTE를 메모리에서 읽어 변환한다. 메모리 읽기 횟수도 센다. 이어서 레벨 수 공식과, 희소 32-bit 주소 공간에서 선형 vs 2단계 메모리 사용량을 비교한다.

```c
// C20_multilevel.c — 2단계 페이지 테이블 워크 (OSTEP Fig 20.5 예제) + 레벨 수 / 테이블 크기 계산
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// ---------------- 1) 책 예제: 16KB 주소공간, 64B 페이지, 4B PTE ----------------
// VA 14bit = VPN 8bit (PDIndex 4 | PTIndex 4) + offset 6bit
#define OFF_BITS 6
#define PAGE     (1u << OFF_BITS)          // 64 B
#define NFRAMES  128
static uint8_t  phys[NFRAMES * PAGE];      // "물리 메모리" 8KB
static int      mem_reads;                 // 페이지 테이블 워크 중 메모리 읽기 횟수

typedef struct { uint32_t pfn : 24, valid : 1, prot : 3; } entry_t;   // PDE/PTE 공통 (4B)

static entry_t *frame_entries(uint32_t pfn) { return (entry_t *)&phys[pfn * PAGE]; }

static entry_t read_entry(uint32_t addr)    // 물리 주소에서 4B 엔트리 읽기
{
    entry_t e; memcpy(&e, &phys[addr], sizeof e); mem_reads++; return e;
}

static int walk(uint32_t pdbr_pfn, uint32_t va)
{
    uint32_t vpn = va >> OFF_BITS, off = va & (PAGE - 1);
    uint32_t pdi = (vpn >> 4) & 0xF, pti = vpn & 0xF;
    mem_reads = 0;
    uint32_t pde_addr = (pdbr_pfn << OFF_BITS) + pdi * (uint32_t)sizeof(entry_t);
    entry_t pde = read_entry(pde_addr);
    printf("VA 0x%04x : VPN %3u (PDI %2u, PTI %2u) off %2u | PDEaddr 0x%04x valid %u",
           va, vpn, pdi, pti, off, pde_addr, pde.valid);
    if (!pde.valid) { printf(" -> FAULT (PDE invalid)  [reads %d]\n", mem_reads); return -1; }
    uint32_t pte_addr = ((uint32_t)pde.pfn << OFF_BITS) + pti * (uint32_t)sizeof(entry_t);
    entry_t pte = read_entry(pte_addr);
    printf(" -> PT@PFN %u, PTEaddr 0x%04x valid %u", (unsigned)pde.pfn, pte_addr, pte.valid);
    if (!pte.valid) { printf(" -> FAULT (PTE invalid)  [reads %d]\n", mem_reads); return -1; }
    uint32_t pa = ((uint32_t)pte.pfn << OFF_BITS) | off;
    printf(" -> PFN %u -> PA 0x%04x  [reads %d]\n", (unsigned)pte.pfn, pa, mem_reads);
    return (int)pa;
}

static void build_book_example(void)
{
    // 페이지 디렉터리: PFN 99 (아무 빈 프레임), PDE[0] -> PFN 100, PDE[15] -> PFN 101
    entry_t *pd = frame_entries(99);
    pd[0]  = (entry_t){ .pfn = 100, .valid = 1 };
    pd[15] = (entry_t){ .pfn = 101, .valid = 1 };
    entry_t *pt0 = frame_entries(100);       // VPN 0..15
    pt0[0] = (entry_t){ 10, 1, 5 };  pt0[1] = (entry_t){ 23, 1, 5 };   // code r-x
    pt0[4] = (entry_t){ 80, 1, 6 };  pt0[5] = (entry_t){ 59, 1, 6 };   // heap rw-
    entry_t *pt15 = frame_entries(101);      // VPN 240..255
    pt15[14] = (entry_t){ 55, 1, 6 }; pt15[15] = (entry_t){ 45, 1, 6 }; // stack rw-
}

// ---------------- 2) 레벨 수 계산 ----------------
static void levels(const char *name, int va_bits, int page_log2, int pte_bytes)
{
    int per_level = page_log2 - (pte_bytes == 8 ? 3 : 2);   // 한 페이지에 들어가는 엔트리 수의 log2
    int vpn_bits = va_bits - page_log2;
    int n = (vpn_bits + per_level - 1) / per_level;
    int top = vpn_bits - per_level * (n - 1);
    printf("  %-34s VPN %2d bits, %2d bits/level -> %d levels (top level uses %d bits = %d entries)\n",
           name, vpn_bits, per_level, n, top, 1 << top);
}

// ---------------- 3) 희소 주소공간에서 선형 vs 2단계 메모리 ----------------
static void sparse_compare(void)
{
    // 32-bit, 4KB 페이지, 4B PTE: 1024 PTE/page, PD 1024 엔트리
    // 사용 영역: code 0x00400000 4MB, heap 0x10000000 64MB, stack 0xBF800000 8MB
    struct { uint32_t base, size; } r[3] = {
        { 0x00400000u, 4u << 20 }, { 0x10000000u, 64u << 20 }, { 0xBF800000u, 8u << 20 } };
    static uint8_t pt_page_used[1024];
    long used_pages = 0;
    for (int i = 0; i < 3; i++)
        for (uint64_t va = r[i].base; va < (uint64_t)r[i].base + r[i].size; va += 4096) {
            pt_page_used[va >> 22] = 1; used_pages++;
        }
    int n = 0; for (int i = 0; i < 1024; i++) n += pt_page_used[i];
    printf("  used virtual pages: %ld (%.0f MB of 4096 MB)\n", used_pages, used_pages * 4.0 / 1024);
    printf("  linear table : 1024 pages of PTEs = %d KB\n", 1024 * 4);
    printf("  two-level    : 1 PD page + %d PT pages = %d KB  (%.1f%% of linear)\n",
           n, (1 + n) * 4, 100.0 * (1 + n) / 1024);
}

int main(void)
{
    printf("[1] two-level walk, book example (PDBR = PFN 99)\n");
    build_book_example();
    uint32_t tests[] = { 0x3F80, 0x3FFF, 0x0000, 0x0145, 0x0080, 0x2000 };
    for (unsigned i = 0; i < sizeof tests / sizeof tests[0]; i++) walk(99, tests[i]);

    printf("\n[2] how many levels so that every table fits in one page?\n");
    levels("book: 30-bit VA, 512B page, 4B PTE", 30, 9, 4);
    levels("x86 32-bit, 4KB, 4B PTE", 32, 12, 4);
    levels("x86-64 48-bit, 4KB, 8B PTE", 48, 12, 8);
    levels("x86-64 57-bit (LA57), 4KB, 8B", 57, 12, 8);
    levels("arm64 48-bit, 16KB granule, 8B", 48, 14, 8);
    levels("arm64 47-bit, 16KB granule, 8B", 47, 14, 8);
    levels("arm64 39-bit, 4KB granule, 8B", 39, 12, 8);

    printf("\n[3] sparse 32-bit address space: linear vs two-level\n");
    sparse_compare();
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 -pthread code/C20_multilevel.c -o .work/bin/C20_multilevel && .work/bin/C20_multilevel
[1] two-level walk, book example (PDBR = PFN 99)
VA 0x3f80 : VPN 254 (PDI 15, PTI 14) off  0 | PDEaddr 0x18fc valid 1 -> PT@PFN 101, PTEaddr 0x1978 valid 1 -> PFN 55 -> PA 0x0dc0  [reads 2]
VA 0x3fff : VPN 255 (PDI 15, PTI 15) off 63 | PDEaddr 0x18fc valid 1 -> PT@PFN 101, PTEaddr 0x197c valid 1 -> PFN 45 -> PA 0x0b7f  [reads 2]
VA 0x0000 : VPN   0 (PDI  0, PTI  0) off  0 | PDEaddr 0x18c0 valid 1 -> PT@PFN 100, PTEaddr 0x1900 valid 1 -> PFN 10 -> PA 0x0280  [reads 2]
VA 0x0145 : VPN   5 (PDI  0, PTI  5) off  5 | PDEaddr 0x18c0 valid 1 -> PT@PFN 100, PTEaddr 0x1914 valid 1 -> PFN 59 -> PA 0x0ec5  [reads 2]
VA 0x0080 : VPN   2 (PDI  0, PTI  2) off  0 | PDEaddr 0x18c0 valid 1 -> PT@PFN 100, PTEaddr 0x1908 valid 0 -> FAULT (PTE invalid)  [reads 2]
VA 0x2000 : VPN 128 (PDI  8, PTI  0) off  0 | PDEaddr 0x18e0 valid 0 -> FAULT (PDE invalid)  [reads 1]

[2] how many levels so that every table fits in one page?
  book: 30-bit VA, 512B page, 4B PTE VPN 21 bits,  7 bits/level -> 3 levels (top level uses 7 bits = 128 entries)
  x86 32-bit, 4KB, 4B PTE            VPN 20 bits, 10 bits/level -> 2 levels (top level uses 10 bits = 1024 entries)
  x86-64 48-bit, 4KB, 8B PTE         VPN 36 bits,  9 bits/level -> 4 levels (top level uses 9 bits = 512 entries)
  x86-64 57-bit (LA57), 4KB, 8B      VPN 45 bits,  9 bits/level -> 5 levels (top level uses 9 bits = 512 entries)
  arm64 48-bit, 16KB granule, 8B     VPN 34 bits, 11 bits/level -> 4 levels (top level uses 1 bits = 2 entries)
  arm64 47-bit, 16KB granule, 8B     VPN 33 bits, 11 bits/level -> 3 levels (top level uses 11 bits = 2048 entries)
  arm64 39-bit, 4KB granule, 8B      VPN 27 bits,  9 bits/level -> 3 levels (top level uses 9 bits = 512 entries)

[3] sparse 32-bit address space: linear vs two-level
  used virtual pages: 19456 (76 MB of 4096 MB)
  linear table : 1024 pages of PTEs = 4096 KB
  two-level    : 1 PD page + 19 PT pages = 80 KB  (2.0% of linear)
```

읽는 법:

- [1] **0x3F80 → 0x0DC0** 원문과 일치. 정상 변환은 페이지 테이블 읽기 **2회**, PDE가 invalid인 0x2000은 **1회**만에 폴트(PT 페이지를 읽을 필요조차 없음). PDE는 valid인데 PTE가 invalid인 0x0080(VPN 2, code와 heap 사이)은 2회 후 폴트.
- [2] 원문 30-bit/512B 예제 = **3레벨** 확인. arm64 16KB granule은 47-bit에서 정확히 3레벨로 떨어진다 — Apple이 16KB를 고른 이유 중 하나(워크가 한 단계 짧음).
- [3] code 4MB + heap 64MB + stack 8MB = 76MB를 쓰는 프로세스: 선형 4096KB vs 2단계 **80KB (2%)**. 이것이 "쓰는 만큼만" 할당의 위력.

### 8.2 OSTEP 시뮬레이터: `paging-multilevel-translate.py`

```text
cd .tools/ostep-homework/vm-smalltables
```

설정(README): 페이지 **32B**, 가상 공간 1024 페이지(32KB) → VA 15비트 = **PDIndex 5 | PTIndex 5 | offset 5**. 물리 128 페이지 → PA 12비트. PDE/PTE는 1바이트: `VALID(1) | PFN(7)`. 페이지 덤프는 페이지당 32바이트를 16진수로 보여 준다.

```text
$ python3 paging-multilevel-translate.py -s 0 -n 3
ARG seed 0
ARG allocated 64
ARG num 3

page   0:1b1d05051d0b19001e00121c1909190c0f0b0a1218151700100a061c06050514
...(128 페이지 덤프 중 필요한 줄만 발췌)...
page  33:7f7f7f7f7f7f7f7fb57f9d7f7f7f7f7f7f7f7f7f7f7f7f7f7f7ff6b17f7f7f7f
page  53:0f0c18090e121c0f081713071c1e191b09161b150e030d121c1d0e1a08181100
page  78:0e02171b1c1a1b1c100c1508191a1b121d110d141e1c1802120f131a07160306
page  84:7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f7f947f7f7f7f7fce
page  86:7f7f7f7f7f7f7fc57f7f7f7f7f7f7f7f7f7f7f7f7fca7f7fee7f7f7f7f7f7f7f7f
page 108:83fee0da7fd47febbe9ed5ade4ac90d692d8c1f89fe1ede9a1e8c7c2a9d1dbff
...
PDBR: 108  (decimal) [This means the page directory is held in this page]

Virtual Address 611c: Translates To What Physical Address (And Fetches what Value)? Or Fault?
Virtual Address 3da8: Translates To What Physical Address (And Fetches what Value)? Or Fault?
Virtual Address 17f5: Translates To What Physical Address (And Fetches what Value)? Or Fault?
```

손으로 0x611c 풀기:

```text
0x611c = 110 0001 0001 1100₂ (15bit)
PDIndex = 0x611c >> 10        = 11000₂ = 24
PTIndex = (0x611c >> 5) & 0x1f = 01000₂ = 8
offset  = 0x611c & 0x1f        = 11100₂ = 28 (0x1c)

page 108 의 byte[24]: 83 fe e0 da 7f d4 7f eb | be 9e d5 ad e4 ac 90 d6 | 92 d8 c1 f8 9f e1 ed e9 | a1 ...
                                                                                          ↑ 24번째 = 0xa1
  0xa1 = 1010 0001₂ → valid 1, PFN 0x21 = 33
page 33 의 byte[8]  = 0xb5 = 1011 0101₂ → valid 1, PFN 0x35 = 53
PA = (53 << 5) | 28 = 1696 + 28 = 1724 = 0x6bc
page 53 의 byte[28] = 0x08   → 값 0x08
```

`-c` 로 확인:

```text
$ python3 paging-multilevel-translate.py -s 0 -n 3 -c | tail -12
Virtual Address 0x611c:
  --> pde index:0x18 [decimal 24] pde contents:0xa1 (valid 1, pfn 0x21 [decimal 33])
    --> pte index:0x8 [decimal 8] pte contents:0xb5 (valid 1, pfn 0x35 [decimal 53])
      --> Translates to Physical Address 0x6bc --> Value: 0x08
Virtual Address 0x3da8:
  --> pde index:0xf [decimal 15] pde contents:0xd6 (valid 1, pfn 0x56 [decimal 86])
    --> pte index:0xd [decimal 13] pte contents:0x7f (valid 0, pfn 0x7f [decimal 127])
      --> Fault (page table entry not valid)
Virtual Address 0x17f5:
  --> pde index:0x5 [decimal 5] pde contents:0xd4 (valid 1, pfn 0x54 [decimal 84])
    --> pte index:0x1f [decimal 31] pte contents:0xce (valid 1, pfn 0x4e [decimal 78])
      --> Translates to Physical Address 0x9d5 --> Value: 0x1c
```

- 0x3da8: PDE는 valid(0xd6 → PT@86)인데 page 86의 byte[13] = 0x7f → **valid 0** → 폴트. 0x7f는 시뮬레이터가 "invalid"를 채우는 값.
- 0x17f5: PDE 0xd4 → PT@84, byte[31] = 0xce → PFN 0x4e=78, offset 0x15=21 → PA = 78×32 + 21 = 2517 = 0x9d5, page 78 byte[21] = 0x1c ✔.
- 숙제 Q2 "lookup당 메모리 참조 수": 정상 변환은 **PDE 1 + PTE 1 + 데이터 1 = 3회** (TLB가 없다는 가정).

## 9. 펌웨어 엔지니어의 눈으로

- **IOMMU/SMMU/DART의 IO 페이지 테이블도 다단계다.** Arm SMMUv3는 CPU와 같은 VMSAv8 형식(4KB/16KB/64KB granule, 3~4레벨)을 쓰고, Apple DART는 16KB 페이지에 (Asahi Linux 역공학 문서 기준) 주로 **2레벨**(L1 → L2 → 16KB 페이지) 구조다. 드라이버 디버깅 때 "IOVA → L1 index → L2 index → PA"를 손으로 걷는 능력이 바로 5.3절이다. 스트림(디바이스)마다 별도 테이블 = 프로세스별 테이블.
- **SMMU의 2단계 변환(stage 1 + stage 2)** 은 "다단계 × 다단계": 게스트 VA→IPA(stage1, 4레벨)의 각 테이블 접근이 또 IPA→PA(stage2, 4레벨) 변환을 필요로 해 최악 **24회 메모리 접근** (가상화 VMM, [부록 B](2026-09-30_C0B_virtual_machine_monitors.md)). 그래서 walk cache와 큰 페이지가 필수.
- **GPU/NPU 페이지 테이블**: NVIDIA GPU MMU는 5레벨급 계층에 **64KB/2MB 큰 페이지**를 섞고, 드라이버가 VRAM/시스템 메모리 할당 시 PDE/PTE를 직접 기록한다. 온디바이스 NPU는 펌웨어가 정적 할당한 영역을 **1단계 + 큰 블록**으로 매핑해 워크 비용을 없애는 경우가 많다.
- **SSD FTL의 2단계 매핑** — DFTL의 GTD(Global Translation Directory)는 "어느 NAND 페이지에 L2P 매핑 페이지가 있는지"를 기록하는 **페이지 디렉터리**이고, 매핑 페이지 자체는 NAND에 있다가 필요할 때만 올라온다. 20.5의 "페이지 테이블을 디스크로 스왑"과 같은 그림이다.
- **역페이지 테이블 ≈ FTL의 P2L(reverse map)**: GC가 "이 NAND 블록의 각 페이지는 어떤 LBA였나?"를 알아야 유효 페이지를 옮길 수 있다 — 물리 단위마다 (소유자, 논리 주소)를 기록하는 역매핑.

## 10. 면접 질문

### Q1. x86-64가 4단계 페이지 테이블인 이유를 비트 계산으로 설명하라.
<details>
<summary>답 보기</summary>

- 48-bit VA, 4KB 페이지 → offset 12비트, VPN **36비트**.
- PTE 8B → 한 페이지에 **512개 = 9비트**.
- 36 / 9 = **4레벨** (PML4, PDPT, PD, PT). 모든 테이블 조각이 정확히 4KB 한 페이지.
- 57-bit(LA57)면 45/9 = 5레벨. arm64 16KB granule은 레벨당 11비트라 47-bit에서 3레벨.

</details>

### Q2. 다단계 테이블의 장단점과, 단점을 하드웨어가 어떻게 보완하나?
<details>
<summary>답 보기</summary>

- 장점: **사용량에 비례한 크기**(희소 공간에 강함), 모든 조각이 페이지 크기라 **연속 물리 메모리 불필요**.
- 단점: TLB 미스 시 **레벨 수만큼 메모리 접근**, 구현 복잡도.
- 보완: **TLB**(대부분 hit), **페이지 워크 캐시**(상위 PDE 캐싱), PTE의 **데이터 캐시 상주**, **큰 페이지**(워크 단축 + coverage 증가), 2단계 TLB.

</details>

### Q3. 2단계 테이블에서 VA → PA를 손으로 변환하라 (14-bit VA, 64B 페이지, PDE/PTE 4B, PDBR=PFN 99, PD[15]=PFN 101, PT@101[14]=PFN 55, VA 0x3F80).
<details>
<summary>답 보기</summary>

- VPN = 0x3F80 >> 6 = 254, offset 0.
- PDIndex = 254 >> 4 = 15 → PDE 주소 = 99×64 + 15×4 = 0x18FC → PFN 101.
- PTIndex = 254 & 15 = 14 → PTE 주소 = 101×64 + 14×4 = 0x1978 → PFN 55.
- PA = 55×64 + 0 = **0x0DC0**. TLB 미스 시 메모리 접근 3회(PDE, PTE, 데이터).

</details>

### Q4. 역페이지 테이블은 언제 유리하고 왜 주류가 아닌가?
<details>
<summary>답 보기</summary>

- 크기가 **물리 메모리에 비례**, 프로세스 수·가상 공간 크기와 무관 → 64-bit 거대 가상 공간 + 많은 프로세스에 공간상 유리 (PowerPC, IA-64).
- 단점: (PID,VPN) **검색이 필요**(해시 + 충돌 체인), **공유 메모리 표현 곤란**(PFN당 하나), HW page walker 설계가 어려움, 캐시 지역성 나쁨.
- 다단계 + TLB + 큰 페이지 조합이 실용적으로 충분해서 x86/ARM이 채택.

</details>

### Q5. 하이브리드(세그먼트별 페이지 테이블)의 bounds 레지스터는 무엇을 가리키며, 왜 결국 버려졌나?
<details>
<summary>답 보기</summary>

- base = 그 세그먼트 **페이지 테이블의 물리 주소**, bounds = 테이블의 **유효 페이지 수(끝)**. VPN ≥ bounds면 예외.
- 버려진 이유: 세그먼트 안이 희소하면 여전히 낭비, 테이블 크기가 가변이라 **외부 단편화 재발**, 세그먼트 모델 자체의 경직성. 다단계 테이블이 같은 효과를 더 일반적으로 달성.

</details>

## 11. 자가 점검 & 숙제

### 퀴즈 1. 32-bit, 4KB 페이지, 4B PTE의 2단계 테이블에서 PDIndex/PTIndex/offset 비트 수는?
<details>
<summary>답 보기</summary>

offset 12, 한 페이지 PTE 1024개 → PTIndex **10**, 남은 **10** = PDIndex. 디렉터리도 1024 × 4B = 4KB 한 페이지. (고전 x86 32-bit 비PAE 구조.)

</details>

### 퀴즈 2. 위 구조에서 프로세스가 0x00000000~0x003FFFFF(4MB)만 쓴다면 테이블 메모리는?
<details>
<summary>답 보기</summary>

4MB = PT 한 페이지가 커버하는 범위(1024 × 4KB) 정확히 1개 → 디렉터리 4KB + PT 4KB = **8KB**. 선형이면 4MB.

</details>

### 퀴즈 3. 16KB 주소 공간/64B 페이지 예제에서, 스택이 VPN 239까지 자라면 페이지 테이블 메모리는 몇 페이지가 되나?
<details>
<summary>답 보기</summary>

VPN 239 = 1110 1111₂ → PDIndex 14. PD[14]가 새로 필요 → PT 페이지 1개 추가. 디렉터리 1 + PT 3 = **4페이지**.

</details>

### 퀴즈 4. arm64 16KB granule, 47-bit VA에서 TLB 미스 한 번의 최대 메모리 접근 수는 (데이터 포함)?
<details>
<summary>답 보기</summary>

3레벨 워크 3회 + 데이터 1회 = **4회** (워크 캐시 없이). 4KB granule 48-bit면 4레벨이라 5회.

</details>

### 원문 Homework 중 꼭 해볼 것

- **Q1**: 2단계/3단계 테이블을 찾는 데 필요한 레지스터 수 — 정답은 **최상위 디렉터리 주소 하나(PDBR)** 뿐, 나머지는 테이블 안의 PFN으로 따라간다는 점을 확인하는 문제.
- **Q2**: seed 0, 1, 2로 손 변환 후 `-c` 확인 — 8.2절에서 seed 0 수행. 1, 2도 해 볼 것.
- **Q3**: 페이지 테이블 접근이 캐시에서 어떻게 동작할지 — 상위 레벨 PDE는 많은 변환이 공유하므로 **캐시 hit가 잘 나고**, 말단 PTE는 접근 패턴에 따라 다르다는 점을 생각해 보는 문제.

## 12. 다음으로

- [Ch.21 스와핑 메커니즘](2026-09-30_C21_swapping_mechanisms.md): 주소 공간이 물리 메모리보다 클 때 — present 비트와 페이지 폴트.
- [Ch.23 VAX/VMS](2026-09-30_C23_vax_vms.md): 페이지 테이블을 커널 가상 메모리에 두고 스왑하는 실제 사례.
- 복습: [Ch.19 TLB](2026-09-30_C19_tlb.md).
