# Ch.18 페이징 입문 — 주소 공간을 고정 크기 조각으로 자르기

> 📖 원문: [18. Paging: Introduction](../book-md/C18_paging_introduction.md) · [PDF p.195](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=195) · ⏱️ 읽기 약 60분 · 🔗 선행: [Ch.15 주소 변환](2026-09-30_C15_address_translation.md), [Ch.16 세그멘테이션](2026-09-30_C16_segmentation.md), [Ch.17 빈 공간 관리](2026-09-30_C17_free_space_management.md)

## 0. 한눈에 보기

> **CRUX — "How can we virtualize memory with pages, so as to avoid the problems of segmentation? What are the basic techniques? How do we make those techniques work well, with minimal space and time overheads?"**
> (세그멘테이션의 문제를 피하면서 페이지로 메모리를 가상화하려면 어떻게 해야 할까? 기본 기법은 무엇이고, 공간·시간 오버헤드를 최소로 하려면?)

- 세그멘테이션은 **가변 크기** 조각이라 외부 단편화가 생긴다. 페이징은 주소 공간을 **고정 크기 페이지(page)**, 물리 메모리를 같은 크기의 **페이지 프레임(page frame)** 으로 자른다.
- 가상 주소 = **VPN(가상 페이지 번호) + offset**. 변환은 "VPN → PFN 으로 바꾸고 offset 은 그대로 붙이기"가 전부다.
- 이 매핑을 담는 프로세스별 자료구조가 **페이지 테이블(page table)**. 가장 단순한 형태는 VPN으로 인덱싱하는 배열(**선형 페이지 테이블**).
- 하지만 순진하게 하면 두 가지 문제가 생긴다: (1) **너무 크다** (32-bit/4KB면 프로세스당 4MB), (2) **너무 느리다** (모든 메모리 접근마다 페이지 테이블을 읽는 추가 접근 1회). 다음 두 장(Ch.19 TLB, Ch.20 작은 테이블)이 각각을 해결한다.

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 페이지 (page) | 가상 주소 공간을 자른 고정 크기 단위 | x86 4KB, Apple Silicon 16KB |
| 페이지 프레임 (page frame) | 물리 메모리를 같은 크기로 자른 칸 | 페이지가 들어갈 "주차 칸" |
| VPN (virtual page number) | 가상 주소의 상위 비트 = 몇 번째 페이지인가 | 64B 공간/16B 페이지면 상위 2비트 |
| offset | 가상 주소의 하위 비트 = 페이지 안 몇 번째 바이트 | 변환하지 않고 그대로 복사 |
| PFN / PPN | 물리 프레임 번호 | VPN 1 → PFN 7 |
| 페이지 테이블 (page table) | VPN → PFN 매핑을 담는 프로세스별 자료구조 | 주소록: "1번 페이지는 7번 칸에" |
| 선형 페이지 테이블 (linear page table) | VPN을 인덱스로 쓰는 PTE 배열 | `pte = table[vpn]` |
| PTE (page table entry) | 페이지 테이블의 한 칸: PFN + 여러 비트 | x86 32-bit PTE = 4바이트 |
| valid 비트 | 이 VPN이 할당된 영역인가 | 힙과 스택 사이 빈 공간은 invalid |
| protection 비트 | 읽기/쓰기/실행 권한 | 코드 r-x, 힙 rw- |
| present 비트 | 지금 물리 메모리에 있나 (아니면 디스크) | Ch.21 스왑의 핵심 |
| dirty 비트 | 메모리에 올라온 뒤 수정됐나 | 쫓아낼 때 디스크에 써야 하는지 |
| reference(accessed) 비트 | 최근에 접근됐나 | 교체 정책(Ch.22)이 사용 |
| PTBR (page-table base register) | 현재 프로세스 페이지 테이블의 물리 주소 | x86 CR3, arm64 TTBR0_EL1 |
| 내부 단편화 (internal fragmentation) | 페이지 안에서 안 쓰고 버려지는 공간 | 100B만 쓰는데 4KB 할당 |

## 2. 왜 페이징인가 — 가변 크기 vs 고정 크기 (18 도입부)

공간 관리 문제를 풀 때 OS가 택하는 길은 크게 두 가지다.

1. **가변 크기 조각으로 자르기** — Ch.16 세그멘테이션. 코드/힙/스택처럼 논리 단위로 자른다. 문제는 크기가 제각각이라 시간이 지나면 메모리가 구멍투성이가 되는 **외부 단편화(external fragmentation)** 다. Ch.17의 할당 알고리즘(best-fit, buddy 등)이 아무리 똑똑해도 근본적으로 없어지지 않는다.
2. **고정 크기 조각으로 자르기** — 이것이 **페이징(paging)**. 1962년 영국 맨체스터의 **Atlas** 가 처음 했다.

고정 크기면 "아무 빈 칸에나 넣으면 된다". 빈 프레임 목록(free list)에서 필요한 개수만큼 꺼내 쓰면 끝이므로 외부 단편화가 원천적으로 없다. 또 힙이 위로, 스택이 아래로 자란다는 식의 **사용 패턴 가정이 필요 없다** — 이것이 원문이 말하는 가장 큰 장점 "유연성(flexibility)"이다.

## 3. 아주 작은 예제와 변환 (18.1)

원문 예제: **64바이트 주소 공간**, **16바이트 페이지** → 가상 페이지 4개(VP 0~3). 물리 메모리는 **128바이트 = 프레임 8개**. OS가 프레임 0을 자기용으로 쓰고, 나머지에 다음처럼 배치했다.

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C18-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--accent)"/></marker></defs>
  <text x="90" y="22" text-anchor="middle" fill="currentColor" font-weight="600">가상 주소 공간 (64B)</text>
  <text x="520" y="22" text-anchor="middle" fill="currentColor" font-weight="600">물리 메모리 (128B)</text>
  <g stroke="currentColor" fill="none">
    <rect x="30" y="40" width="120" height="50" style="fill:var(--accent-soft)"/>
    <rect x="30" y="90" width="120" height="50" style="fill:var(--accent-soft)"/>
    <rect x="30" y="140" width="120" height="50" style="fill:var(--accent-soft)"/>
    <rect x="30" y="190" width="120" height="50" style="fill:var(--accent-soft)"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="90" y="70">VP 0 (0–15)</text><text x="90" y="120">VP 1 (16–31)</text>
    <text x="90" y="170">VP 2 (32–47)</text><text x="90" y="220">VP 3 (48–63)</text>
  </g>
  <g stroke="currentColor" fill="none">
    <rect x="460" y="40" width="120" height="30"/><rect x="460" y="70" width="120" height="30"/>
    <rect x="460" y="100" width="120" height="30" style="fill:var(--accent-soft)"/><rect x="460" y="130" width="120" height="30" style="fill:var(--accent-soft)"/>
    <rect x="460" y="160" width="120" height="30"/><rect x="460" y="190" width="120" height="30" style="fill:var(--accent-soft)"/>
    <rect x="460" y="220" width="120" height="30"/><rect x="460" y="250" width="120" height="30" style="fill:var(--accent-soft)"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="520" y="60">OS 예약</text><text x="520" y="90">(빈 칸)</text>
    <text x="520" y="120">VP 3</text><text x="520" y="150">VP 0</text>
    <text x="520" y="180">(빈 칸)</text><text x="520" y="210">VP 2</text>
    <text x="520" y="240">(빈 칸)</text><text x="520" y="270">VP 1</text>
  </g>
  <g fill="currentColor" text-anchor="end" font-size="11">
    <text x="454" y="60">PF 0</text><text x="454" y="90">PF 1</text><text x="454" y="120">PF 2</text><text x="454" y="150">PF 3</text>
    <text x="454" y="180">PF 4</text><text x="454" y="210">PF 5</text><text x="454" y="240">PF 6</text><text x="454" y="270">PF 7</text>
  </g>
  <g style="stroke:var(--accent)" stroke-width="1.6" fill="none" marker-end="url(#C18-arrow)">
    <path d="M150,65 C300,65 300,145 410,145"/>
    <path d="M150,115 C300,115 300,265 410,265"/>
    <path d="M150,165 C300,165 300,205 410,205"/>
    <path d="M150,215 C300,215 300,115 410,115"/>
  </g>
  <text x="300" y="292" text-anchor="middle" fill="currentColor" font-size="12">페이지 테이블 = { VP0→PF3, VP1→PF7, VP2→PF5, VP3→PF2 } (프레임 1·4·6 은 free list 에)</text>
</svg>
```

이 기록, 즉 **(VP 0 → PF 3), (VP 1 → PF 7), (VP 2 → PF 5), (VP 3 → PF 2)** 가 페이지 테이블이다. 중요한 점: 페이지 테이블은 **프로세스마다 하나**다 (예외: Ch.20의 inverted page table). 다른 프로세스는 같은 VP 번호를 전혀 다른 프레임에 매핑한다.

### 3.1 비트로 쪼개기: VPN과 offset

- 주소 공간 64B = 2^6 → 가상 주소는 **6비트**.
- 페이지 16B = 2^4 → 하위 **4비트가 offset**.
- 남은 상위 **2비트가 VPN** (페이지 4개를 고르려면 2비트).

일반식: `offset 비트 = log2(페이지 크기)`, `VPN 비트 = 주소 비트 - offset 비트`.

`movl 21, %eax` 를 변환해 보자. 21 = `01 0101`₂.

```text
가상 주소 21  =   0 1 | 0 1 0 1
                  VPN   offset        VPN = 01₂ = 1,  offset = 0101₂ = 5
페이지 테이블[1] = PFN 7 = 111₂
물리 주소     = 1 1 1 | 0 1 0 1  = 1110101₂ = 117
                  PFN    offset (그대로 복사)
```

```svg
<svg viewBox="0 0 620 210" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C18-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" style="fill:var(--accent)"/></marker></defs>
  <text x="20" y="45" fill="currentColor">가상 주소 (VA=21)</text>
  <g stroke="currentColor" fill="none">
    <rect x="170" y="25" width="36" height="30" style="fill:var(--accent-soft)"/><rect x="206" y="25" width="36" height="30" style="fill:var(--accent-soft)"/>
    <rect x="242" y="25" width="36" height="30"/><rect x="278" y="25" width="36" height="30"/><rect x="314" y="25" width="36" height="30"/><rect x="350" y="25" width="36" height="30"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="188" y="45">0</text><text x="224" y="45">1</text><text x="260" y="45">0</text><text x="296" y="45">1</text><text x="332" y="45">0</text><text x="368" y="45">1</text>
  </g>
  <text x="206" y="18" text-anchor="middle" fill="currentColor" font-size="11">VPN (2bit)</text>
  <text x="314" y="18" text-anchor="middle" fill="currentColor" font-size="11">offset (4bit)</text>
  <rect x="420" y="70" width="170" height="56" rx="6" fill="none" style="stroke:var(--accent)"/>
  <text x="505" y="92" text-anchor="middle" fill="currentColor">페이지 테이블</text>
  <text x="505" y="112" text-anchor="middle" fill="currentColor" font-size="12">PT[1] → PFN 7 (111₂)</text>
  <path d="M206,56 C206,95 330,98 418,98" fill="none" style="stroke:var(--accent)" stroke-width="1.6" marker-end="url(#C18-arrow2)"/>
  <path d="M505,127 C505,150 200,140 188,158" fill="none" style="stroke:var(--accent)" stroke-width="1.6" marker-end="url(#C18-arrow2)"/>
  <path d="M332,56 L332,158" fill="none" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#C18-arrow2)"/>
  <text x="340" y="110" fill="currentColor" font-size="11">offset 은 그대로</text>
  <text x="20" y="183" fill="currentColor">물리 주소 (PA=117)</text>
  <g stroke="currentColor" fill="none">
    <rect x="134" y="162" width="36" height="30" style="fill:var(--accent-soft)"/><rect x="170" y="162" width="36" height="30" style="fill:var(--accent-soft)"/><rect x="206" y="162" width="36" height="30" style="fill:var(--accent-soft)"/>
    <rect x="242" y="162" width="36" height="30"/><rect x="278" y="162" width="36" height="30"/><rect x="314" y="162" width="36" height="30"/><rect x="350" y="162" width="36" height="30"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="152" y="182">1</text><text x="188" y="182">1</text><text x="224" y="182">1</text><text x="260" y="182">0</text><text x="296" y="182">1</text><text x="332" y="182">0</text><text x="368" y="182">1</text>
  </g>
  <text x="188" y="206" text-anchor="middle" fill="currentColor" font-size="11">PFN (3bit)</text>
  <text x="314" y="206" text-anchor="middle" fill="currentColor" font-size="11">offset (4bit)</text>
</svg>
```

핵심 직관: **offset은 "페이지 안에서 몇 번째 바이트"이므로 번역할 필요가 없다.** 페이지가 통째로 다른 프레임으로 옮겨질 뿐 내부 배치는 그대로다. 그래서 하드웨어는 상위 비트만 갈아 끼우면 된다.

### 3.2 새 예제 1 — 손으로 해보기

같은 페이지 테이블에서 VA 45와 VA 60을 변환해 보자.

```text
VA 45 = 10 1101₂ → VPN 2, offset 13 → PT[2] = PFN 5 = 101₂ → PA = 101 1101₂ = 93
VA 60 = 11 1100₂ → VPN 3, offset 12 → PT[3] = PFN 2 = 010₂ → PA = 010 1100₂ = 44
```

아래 6절의 C 프로그램 출력과 일치하는지 확인해 보라.

### 3.3 새 예제 2 — 실제 크기 (Apple Silicon 16KB 페이지)

Don의 맥(M2)은 페이지가 **16KB = 2^14** 다. 48-bit 가상 주소라면:

- offset = 14비트, VPN = 48 − 14 = **34비트**
- VA `0x0000_0001_0000_4A30` → offset = `0x4A30 & 0x3FFF` = `0x0A30`, VPN = `0x1_0000_4A30 >> 14` = `0x40001`
- 같은 VA를 x86-64(4KB) 기준으로 자르면 offset = `0xA30`(12비트), VPN = `0x100004`. **같은 주소라도 페이지 크기에 따라 VPN/offset 경계가 다르다** — 4KB 가정으로 짠 드라이버/펌웨어 코드가 Apple Silicon에서 깨지는 전형적 원인이다.

## 4. 페이지 테이블은 어디에 두나? 얼마나 큰가? (18.2)

32-bit 주소, 4KB 페이지를 생각하자.

```text
offset  = log2(4096) = 12 bit
VPN     = 32 - 12    = 20 bit   → 2^20 = 1,048,576 개의 PTE
PTE 1개 = 4 B
테이블  = 2^20 × 4 B = 4 MB  (프로세스 하나당!)
프로세스 100개 → 400 MB 가 오직 주소 변환용
```

64-bit는 더 끔찍하다. 48-bit VA, 4KB, 8B PTE를 선형으로 만들면 2^36 × 8B = **512 GB** (6절 출력 [2] 참고). 그래서 MMU 안에 페이지 테이블을 넣는 건 불가능하고, **페이지 테이블은 (OS가 관리하는) 메모리에 둔다.** 지금은 물리 메모리에 있다고 가정하지만, 나중에는 커널 가상 메모리에 두고 심지어 디스크로 스왑할 수도 있다(Ch.20 20.5, Ch.23 VAX/VMS).

> **ASIDE — 자료구조: 페이지 테이블**
> 현대 OS 메모리 관리에서 가장 중요한 자료구조 중 하나. 주소 공간마다 하나씩. 구조는 하드웨어가 정하거나(옛 시스템, 그리고 x86/ARM처럼 HW page walker가 있는 시스템) OS가 자유롭게 정한다(소프트웨어 관리 TLB 시스템).

## 5. 페이지 테이블 안에는 무엇이? — PTE 비트 (18.3)

가장 단순한 구조가 **선형 페이지 테이블**: VPN을 인덱스로 쓰는 배열. PTE 하나에는 PFN 외에 이런 비트들이 있다.

- **valid**: 이 페이지가 할당됐는가. 힙과 스택 사이 거대한 빈 공간은 전부 invalid로 표시하고 **프레임을 아예 주지 않는다** → 희소(sparse) 주소 공간 지원의 핵심. invalid 접근 → trap → 보통 프로세스 종료(segfault).
- **protection**: 읽기/쓰기/실행 가능 여부. 위반 시 trap.
- **present**: 메모리에 있나, 디스크로 스왑 아웃됐나 (Ch.21).
- **dirty**: 메모리에 올라온 뒤 쓰였나 (쫓아낼 때 write-back 필요 여부).
- **reference / accessed**: 최근 접근 여부 (교체 정책의 단서).

x86 32-bit PTE (Figure 18.5) 를 비트 단위로 그리면:

```svg
<svg viewBox="0 0 710 150" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <g stroke="currentColor" fill="none">
    <rect x="20" y="40" width="400" height="40" style="fill:var(--accent-soft)"/>
    <rect x="420" y="40" width="75" height="40"/>
    <rect x="495" y="40" width="22.8" height="40"/><rect x="517.8" y="40" width="22.8" height="40"/><rect x="540.6" y="40" width="22.8" height="40"/>
    <rect x="563.4" y="40" width="22.8" height="40"/><rect x="586.2" y="40" width="22.8" height="40"/><rect x="609" y="40" width="22.8" height="40"/>
    <rect x="631.8" y="40" width="22.8" height="40"/><rect x="654.6" y="40" width="22.8" height="40" style="fill:var(--accent-soft)"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="11">
    <text x="20" y="32">31</text><text x="415" y="32">12</text><text x="457" y="32">11–9</text>
    <text x="506" y="32">8</text><text x="529" y="32">7</text><text x="552" y="32">6</text><text x="575" y="32">5</text>
    <text x="598" y="32">4</text><text x="620" y="32">3</text><text x="643" y="32">2</text><text x="666" y="32">1</text>
  </g>
  <text x="220" y="65" text-anchor="middle" fill="currentColor" font-weight="600">PFN (20 bit)</text>
  <text x="457" y="65" text-anchor="middle" fill="currentColor" font-size="11">OS 자유</text>
  <g fill="currentColor" text-anchor="middle" font-size="10.5">
    <text x="506" y="64">G</text><text x="529" y="64">PAT</text><text x="552" y="64">D</text><text x="575" y="64">A</text>
    <text x="598" y="64">PCD</text><text x="620" y="64">PWT</text><text x="643" y="64">U/S</text><text x="666" y="64">R/W</text>
  </g>
  <text x="689" y="64" text-anchor="middle" fill="currentColor" font-size="10.5">P</text>
  <rect x="677.4" y="40" width="22.6" height="40" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="689" y="32" text-anchor="middle" fill="currentColor" font-size="11">0</text>
  <g fill="currentColor" font-size="11.5">
    <text x="20" y="105">P = present (x86 은 valid 와 present 를 한 비트로 겸함) · R/W = 쓰기 허용 · U/S = 유저 모드 접근 허용</text>
    <text x="20" y="123">PWT/PCD/PAT = 캐시 정책 (write-through / cache-disable — MMIO 매핑에 필수) · G = global (컨텍스트 스위치에도 TLB 유지)</text>
    <text x="20" y="141">A = accessed, D = dirty (하드웨어가 세팅, OS 가 지움) · PFN 20bit × 4KB = 4GB 물리 주소</text>
  </g>
</svg>
```

> **주의 — x86에는 별도 valid 비트가 없다.** P(present)=0 이면 페이지 폴트가 나고, 그게 "진짜 할당 안 된 주소(invalid)"인지 "스왑된 페이지(not present)"인지는 **OS가 자기 자료구조(리눅스의 VMA 등)를 보고 판단**한다. OSTEP의 valid/present 구분은 개념상 구분이고, 실제 HW는 하나로 합친 경우가 많다.

Don에게 익숙한 표현으로: PCD/PWT는 **MMIO 레지스터 영역을 uncached로 매핑**할 때 쓰는 비트다. arm64에선 같은 역할을 PTE의 `AttrIndx` → MAIR 레지스터의 Device-nGnRE 같은 메모리 타입이 한다.

## 6. 페이징은 느리기도 하다 (18.4)

`movl 21, %eax` 하나를 실행하려면 하드웨어가 이렇게 해야 한다 (Figure 18.6 요약).

```text
VPN     = (VA & VPN_MASK) >> SHIFT          // 예: VPN_MASK=0x30 (110000), SHIFT=4
PTEAddr = PTBR + VPN * sizeof(PTE)          // 페이지 테이블에서 PTE 위치 계산
PTE     = AccessMemory(PTEAddr)             // ★ 추가 메모리 접근
if (!PTE.Valid)                 → SEGMENTATION_FAULT
else if (!CanAccess(PTE.Prot))  → PROTECTION_FAULT
else  PA = (PTE.PFN << SHIFT) | (VA & OFFSET_MASK);  Register = AccessMemory(PA)
```

VA 21(`010101`)에 VPN_MASK `110000`을 AND → `010000`, 4비트 shift → `01` = VPN 1. 

**모든 메모리 참조(명령어 fetch 포함)마다 PTE를 읽는 메모리 접근이 1번 더** 붙는다. 메모리 접근이 2배 → 대략 2배 이상 느려진다. 이것이 페이징의 두 번째 문제("too slow")이고, Ch.19 TLB가 답이다.

## 7. 메모리 트레이스 (18.5)

C 코드 `for (i = 0; i < 1000; i++) array[i] = 0;` 가 실제로 어떤 메모리 접근을 만드는지 따라가 보자. 컴파일된 루프 (x86, 각 명령어 4바이트라고 가정):

```text
0x1024  movl $0x0,(%edi,%eax,4)   ; array[i] = 0   (%edi = array 시작, %eax = i)
0x1028  incl %eax                 ; i++
0x102c  cmpl $0x03e8,%eax         ; i == 1000 ?
0x1030  jne  0x1024               ; 아니면 반복
```

> 원문의 주소 `0x1024` 등은 실제로는 **10진수 1024, 1028, 1032, 1036** 으로 해석해야 아래 계산(VPN 1)과 맞는다 (원문 표기 혼동).

가정:

- 가상 주소 공간 **64KB**, 페이지 **1KB** → offset 10비트, VPN 6비트(64개 페이지).
- 선형 페이지 테이블은 **물리 주소 1024** 에 있다. PTE 4바이트.
- 코드: VA 1024~ → **VPN 1 → PFN 4** (PA 4096~).
- 배열: 4000바이트, VA 40000~43999 → **VPN 39~42 → PFN 7~10**.

계산해 보면:

```text
코드 VA 1024  : VPN = 1024/1024 = 1, offset 0   → PTE 주소 = 1024 + 1×4  = 1028 → PFN 4 → PA 4096
배열 VA 40000 : VPN = 40000/1024 = 39 (39×1024=39936), offset = 64
                PTE 주소 = 1024 + 39×4 = 1180 → PFN 7 → PA = 7×1024 + 64 = 7232
```

반복 한 번당 접근 수: 명령어 fetch 4회 + 배열 store 1회 = 실제 접근 5회, 그리고 각각을 변환하는 **PTE 읽기 5회** → **총 10회**. 첫 5번 반복을 그림으로 다시 그리면 (Figure 18.7 재구성; 위: PTE 읽기, 가운데: 배열, 아래: 코드):

```svg
<svg viewBox="0 0 700 330" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
<rect x="70" y="20" width="610" height="75" fill="none" stroke="currentColor" stroke-opacity="0.4"/>
<text x="76" y="34" fill="currentColor" font-size="12">Page Table (PA)</text>
<text x="64" y="88.6" text-anchor="end" fill="currentColor" font-size="11">1028</text>
<text x="64" y="43.0" text-anchor="end" fill="currentColor" font-size="11">1180</text>
<circle cx="78.0" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="102.2" cy="39.0" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="126.5" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="150.7" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="175.0" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="199.2" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="223.5" cy="39.0" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="247.7" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="272.0" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="296.2" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="320.4" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="344.7" cy="39.0" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="368.9" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="393.2" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="417.4" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="441.7" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="465.9" cy="39.0" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="490.2" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="514.4" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="538.7" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="562.9" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="587.1" cy="39.0" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="611.4" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="635.6" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<circle cx="659.9" cy="84.6" r="3.2" fill="none" stroke="currentColor"/>
<rect x="70" y="120" width="610" height="75" fill="none" stroke="currentColor" stroke-opacity="0.4"/>
<text x="76" y="134" fill="currentColor" font-size="12">Array (PA)</text>
<text x="64" y="182.5" text-anchor="end" fill="currentColor" font-size="11">7232</text>
<text x="64" y="148.5" text-anchor="end" fill="currentColor" font-size="11">7248</text>
<rect x="110.9" y="175.0" width="7" height="7" style="fill:var(--accent)"/>
<rect x="232.1" y="166.5" width="7" height="7" style="fill:var(--accent)"/>
<rect x="353.3" y="158.0" width="7" height="7" style="fill:var(--accent)"/>
<rect x="474.5" y="149.5" width="7" height="7" style="fill:var(--accent)"/>
<rect x="595.8" y="141.0" width="7" height="7" style="fill:var(--accent)"/>
<rect x="70" y="220" width="610" height="75" fill="none" stroke="currentColor" stroke-opacity="0.4"/>
<text x="76" y="234" fill="currentColor" font-size="12">Code (PA)</text>
<text x="64" y="280.8" text-anchor="end" fill="currentColor" font-size="11">4096</text>
<text x="64" y="250.2" text-anchor="end" fill="currentColor" font-size="11">4108</text>
<circle cx="90.1" cy="276.8" r="3.2" fill="currentColor"/>
<circle cx="138.6" cy="266.6" r="3.2" fill="currentColor"/>
<circle cx="162.9" cy="256.4" r="3.2" fill="currentColor"/>
<circle cx="187.1" cy="246.2" r="3.2" fill="currentColor"/>
<circle cx="211.3" cy="276.8" r="3.2" fill="currentColor"/>
<circle cx="259.8" cy="266.6" r="3.2" fill="currentColor"/>
<circle cx="284.1" cy="256.4" r="3.2" fill="currentColor"/>
<circle cx="308.3" cy="246.2" r="3.2" fill="currentColor"/>
<circle cx="332.6" cy="276.8" r="3.2" fill="currentColor"/>
<circle cx="381.1" cy="266.6" r="3.2" fill="currentColor"/>
<circle cx="405.3" cy="256.4" r="3.2" fill="currentColor"/>
<circle cx="429.6" cy="246.2" r="3.2" fill="currentColor"/>
<circle cx="453.8" cy="276.8" r="3.2" fill="currentColor"/>
<circle cx="502.3" cy="266.6" r="3.2" fill="currentColor"/>
<circle cx="526.5" cy="256.4" r="3.2" fill="currentColor"/>
<circle cx="550.8" cy="246.2" r="3.2" fill="currentColor"/>
<circle cx="575.0" cy="276.8" r="3.2" fill="currentColor"/>
<circle cx="623.5" cy="266.6" r="3.2" fill="currentColor"/>
<circle cx="647.8" cy="256.4" r="3.2" fill="currentColor"/>
<circle cx="672.0" cy="246.2" r="3.2" fill="currentColor"/>
<line x1="193.2" y1="20" x2="193.2" y2="295" stroke="currentColor" stroke-dasharray="3 4" stroke-opacity="0.5"/>
<line x1="314.4" y1="20" x2="314.4" y2="295" stroke="currentColor" stroke-dasharray="3 4" stroke-opacity="0.5"/>
<line x1="435.6" y1="20" x2="435.6" y2="295" stroke="currentColor" stroke-dasharray="3 4" stroke-opacity="0.5"/>
<line x1="556.8" y1="20" x2="556.8" y2="295" stroke="currentColor" stroke-dasharray="3 4" stroke-opacity="0.5"/>
<text x="132.6" y="312" text-anchor="middle" fill="currentColor" font-size="11">iter 0</text>
<text x="253.8" y="312" text-anchor="middle" fill="currentColor" font-size="11">iter 1</text>
<text x="375.0" y="312" text-anchor="middle" fill="currentColor" font-size="11">iter 2</text>
<text x="496.2" y="312" text-anchor="middle" fill="currentColor" font-size="11">iter 3</text>
<text x="617.4" y="312" text-anchor="middle" fill="currentColor" font-size="11">iter 4</text>
<text x="375.0" y="327" text-anchor="middle" fill="currentColor" font-size="12">메모리 접근 순서 (반복당 10회: PT 읽기 5 + 명령어 fetch 4 + 배열 store 1)</text>
</svg>
```

관찰:

- **PTE 1028 (코드 페이지 PTE)** 은 반복마다 4번 읽힌다 — 같은 값을 계속! 이걸 캐시하면 얼마나 좋을까? → **TLB의 동기**.
- 배열 주소는 반복마다 4씩 증가. 256번 반복(=1024B)마다 다음 페이지로 넘어가 **PTE 주소가 1180 → 1184 → 1188 → 1192** 로 바뀐다 (원문 질문 "루프가 계속되면 어떤 새 주소가 접근되나?"의 답).
- 1000번 반복 전체: PTE 읽기 5000 + 실제 접근 5000 = **10,000회**. 페이징이 없었다면 5000회.

## 8. 직접 해보기

### 8.1 C로 만든 선형 페이지 테이블 MMU

Figure 18.6의 알고리즘을 그대로 C로 옮기고, (1) 책의 VA 21 → 117, (2) 페이지 테이블 크기 계산, (3) Figure 18.7 트레이스의 접근 횟수를 확인한다.

```c
// C18_linear_translate.c — 선형 페이지 테이블 주소 변환 + 메모리 트레이스 카운터
// (OSTEP Ch.18 의 Figure 18.2/18.3 예제와 Figure 18.7 array 루프 트레이스를 C로 재현)
#include <stdio.h>
#include <stdint.h>

// ---- PTE 포맷 (교육용, 32-bit) : [31]=Valid [30]=R/W [29]=U/S [28:0]=PFN
#define PTE_VALID  (1u << 31)
#define PTE_RW     (1u << 30)
#define PTE_USER   (1u << 29)
#define PTE_PFN(x) ((x) & 0x1FFFFFFFu)

typedef struct {
    unsigned offset_bits;       // 페이지 크기 = 2^offset_bits
    unsigned vpn_bits;          // VPN 비트 수
    const uint32_t *table;      // 선형 페이지 테이블 (PTBR 가 가리키는 배열)
    uint32_t ptbr;              // 페이지 테이블의 "물리 주소" (트레이스용)
} mmu_t;

static long pt_accesses, data_accesses;   // 메모리 접근 카운터

// Figure 18.6 그대로: VPN 추출 → PTE 주소 → PTE 읽기 → 검사 → PA 조립
static int translate(const mmu_t *m, uint32_t va, uint32_t *pa, int verbose)
{
    uint32_t offset_mask = (1u << m->offset_bits) - 1;
    uint32_t vpn_mask    = ((1u << m->vpn_bits) - 1) << m->offset_bits;
    uint32_t vpn    = (va & vpn_mask) >> m->offset_bits;
    uint32_t pteaddr = m->ptbr + vpn * (uint32_t)sizeof(uint32_t);
    uint32_t pte    = m->table[vpn];
    pt_accesses++;                                   // 페이지 테이블 접근 1회
    if (!(pte & PTE_VALID)) {
        if (verbose) printf("  VA %5u -> VPN %u : INVALID -> SEGMENTATION_FAULT\n", va, vpn);
        return -1;
    }
    uint32_t off = va & offset_mask;
    *pa = (PTE_PFN(pte) << m->offset_bits) | off;
    if (verbose)
        printf("  VA %5u (0x%04x) -> VPN %u, off %2u | PTEaddr %u | PFN %u -> PA %u (0x%04x)\n",
               va, va, vpn, off, pteaddr, PTE_PFN(pte), *pa, *pa);
    return 0;
}

static void print_bits(uint32_t v, int nbits)
{
    for (int b = nbits - 1; b >= 0; b--) putchar((v >> b) & 1 ? '1' : '0');
}

int main(void)
{
    // ---------- 1) 책 예제: 64B 주소공간, 16B 페이지, 128B 물리메모리 ----------
    // VP0->PF3, VP1->PF7, VP2->PF5, VP3->PF2
    const uint32_t tiny_pt[4] = {
        PTE_VALID | PTE_RW | PTE_USER | 3, PTE_VALID | PTE_RW | PTE_USER | 7,
        PTE_VALID | PTE_RW | PTE_USER | 5, PTE_VALID | PTE_RW | PTE_USER | 2 };
    mmu_t tiny = { .offset_bits = 4, .vpn_bits = 2, .table = tiny_pt, .ptbr = 0 };

    printf("[1] 64B AS / 16B page (book Fig 18.3)\n");
    uint32_t pa;
    translate(&tiny, 21, &pa, 1);
    printf("      VA 21 = "); print_bits(21, 6);
    printf("  ->  PA %u = ", pa); print_bits(pa, 7); printf("\n");
    for (uint32_t va = 0; va < 64; va += 15) translate(&tiny, va, &pa, 1);

    // ---------- 2) 페이지 테이블 크기 계산 ----------
    printf("\n[2] linear page table size = 2^(VA bits - offset bits) * PTE size\n");
    struct { int va_bits; int page_log2; int pte_bytes; const char *name; } cfg[] = {
        {32, 12, 4, "32-bit, 4KB page, 4B PTE"},
        {32, 14, 4, "32-bit, 16KB page, 4B PTE"},
        {48, 12, 8, "48-bit, 4KB page, 8B PTE (x86-64 if linear!)"},
        {48, 14, 8, "48-bit, 16KB page, 8B PTE (arm64 Apple, if linear!)"},
    };
    for (unsigned i = 0; i < sizeof cfg / sizeof cfg[0]; i++) {
        unsigned long long entries = 1ULL << (cfg[i].va_bits - cfg[i].page_log2);
        unsigned long long bytes = entries * (unsigned long long)cfg[i].pte_bytes;
        printf("  %-52s : %llu entries x %dB = %llu bytes (%.1f MB)\n", cfg[i].name,
               entries, cfg[i].pte_bytes, bytes, bytes / (1024.0 * 1024.0));
    }

    // ---------- 3) Figure 18.7 메모리 트레이스 : 64KB AS, 1KB page ----------
    // PT 는 물리주소 1024 에 있음. 코드 VPN1->PFN4, 배열 VPN39..42 -> PFN7..10
    static uint32_t pt64[64];
    pt64[1] = PTE_VALID | PTE_USER | 4;
    for (int v = 39; v <= 42; v++) pt64[v] = PTE_VALID | PTE_RW | PTE_USER | (uint32_t)(v - 32);
    mmu_t m64 = { .offset_bits = 10, .vpn_bits = 6, .table = pt64, .ptbr = 1024 };

    printf("\n[3] array[1000]=0 loop trace (book Fig 18.7), first iteration in detail\n");
    const uint32_t code_va[4] = { 1024, 1028, 1032, 1036 };   // mov, inc, cmp, jne
    const char *name[4] = { "mov", "inc", "cmp", "jne" };
    pt_accesses = data_accesses = 0;
    for (int i = 0; i < 1000; i++) {
        int v = (i == 0);
        for (int k = 0; k < 4; k++) {
            if (v) printf("  fetch %-3s:", name[k]);
            translate(&m64, code_va[k], &pa, v);     // 명령어 fetch 의 변환
            data_accesses++;                         // 명령어 fetch 자체
            if (k == 0) {                            // mov 의 배열 store
                uint32_t ava = 40000 + 4u * (uint32_t)i;
                if (v) printf("  store a[0]:");
                translate(&m64, ava, &pa, v);
                data_accesses++;
            }
        }
    }
    printf("  total over 1000 iterations: page-table reads=%ld, real accesses=%ld, sum=%ld (%.1f per iter)\n",
           pt_accesses, data_accesses, pt_accesses + data_accesses,
           (pt_accesses + data_accesses) / 1000.0);
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 -pthread code/C18_linear_translate.c -o .work/bin/C18_linear_translate && .work/bin/C18_linear_translate
[1] 64B AS / 16B page (book Fig 18.3)
  VA    21 (0x0015) -> VPN 1, off  5 | PTEaddr 4 | PFN 7 -> PA 117 (0x0075)
      VA 21 = 010101  ->  PA 117 = 1110101
  VA     0 (0x0000) -> VPN 0, off  0 | PTEaddr 0 | PFN 3 -> PA 48 (0x0030)
  VA    15 (0x000f) -> VPN 0, off 15 | PTEaddr 0 | PFN 3 -> PA 63 (0x003f)
  VA    30 (0x001e) -> VPN 1, off 14 | PTEaddr 4 | PFN 7 -> PA 126 (0x007e)
  VA    45 (0x002d) -> VPN 2, off 13 | PTEaddr 8 | PFN 5 -> PA 93 (0x005d)
  VA    60 (0x003c) -> VPN 3, off 12 | PTEaddr 12 | PFN 2 -> PA 44 (0x002c)

[2] linear page table size = 2^(VA bits - offset bits) * PTE size
  32-bit, 4KB page, 4B PTE                             : 1048576 entries x 4B = 4194304 bytes (4.0 MB)
  32-bit, 16KB page, 4B PTE                            : 262144 entries x 4B = 1048576 bytes (1.0 MB)
  48-bit, 4KB page, 8B PTE (x86-64 if linear!)         : 68719476736 entries x 8B = 549755813888 bytes (524288.0 MB)
  48-bit, 16KB page, 8B PTE (arm64 Apple, if linear!)  : 17179869184 entries x 8B = 137438953472 bytes (131072.0 MB)

[3] array[1000]=0 loop trace (book Fig 18.7), first iteration in detail
  fetch mov:  VA  1024 (0x0400) -> VPN 1, off  0 | PTEaddr 1028 | PFN 4 -> PA 4096 (0x1000)
  store a[0]:  VA 40000 (0x9c40) -> VPN 39, off 64 | PTEaddr 1180 | PFN 7 -> PA 7232 (0x1c40)
  fetch inc:  VA  1028 (0x0404) -> VPN 1, off  4 | PTEaddr 1028 | PFN 4 -> PA 4100 (0x1004)
  fetch cmp:  VA  1032 (0x0408) -> VPN 1, off  8 | PTEaddr 1028 | PFN 4 -> PA 4104 (0x1008)
  fetch jne:  VA  1036 (0x040c) -> VPN 1, off 12 | PTEaddr 1028 | PFN 4 -> PA 4108 (0x100c)
  total over 1000 iterations: page-table reads=5000, real accesses=5000, sum=10000 (10.0 per iter)
```

읽는 법:

- [1] VA 21 → PA 117, 비트열 `010101 → 1110101` 이 원문 그림 18.3과 정확히 같다. VA 45 → 93, VA 60 → 44 (3.2절 손계산과 일치).
- [2] 48-bit 주소를 **선형** 테이블로 만들면 프로세스당 수백 GB — 64-bit 시스템에서 선형 테이블은 불가능하다는 걸 숫자로 확인. (16KB 페이지면 4배 줄지만 여전히 128 GB.)
- [3] PTE 주소 1028/1180, PA 4096/7232 가 원문과 일치. 1000번 반복에 PTE 읽기 5000회 = 실제 접근 수와 같다 → **메모리 트래픽 2배**.

### 8.2 OSTEP 시뮬레이터: `paging-linear-translate.py`

```text
cd .tools/ostep-homework/vm-paging
```

**(Q1) 페이지 테이블 크기 추세** — `-v -n 0` 으로 PTE 개수만 센다 (아래는 출력의 `[  N]` 줄 수를 센 결과):

```text
$ ./paging-linear-translate.py  -a 1m  -P 1k : 1024 PTEs
$ ./paging-linear-translate.py  -a 2m  -P 1k : 2048 PTEs
$ ./paging-linear-translate.py  -a 4m  -P 1k : 4096 PTEs
$ ./paging-linear-translate.py  -a 1m  -P 1k : 1024 PTEs
$ ./paging-linear-translate.py  -a 1m  -P 2k : 512 PTEs
$ ./paging-linear-translate.py  -a 1m  -P 4k : 256 PTEs
```

- 주소 공간이 2배 → PTE 2배 (선형).
- 페이지가 2배 → PTE 절반. 그럼 "그냥 큰 페이지 쓰면 되지 않나?" → 내부 단편화로 메모리 낭비 (Ch.20 20.1).

**(Q2) 실제 변환** — 16KB 주소 공간, 1KB 페이지, 32KB 물리 메모리, 50% 사용:

```text
$ python3 paging-linear-translate.py -P 1k -a 16k -p 32k -v -u 50
Page Table (from entry 0 down to the max size)
  [       0]  0x80000018
  [       1]  0x00000000
  [       2]  0x00000000
  [       3]  0x8000000c
  [       4]  0x80000009
  [       5]  0x00000000
  [       6]  0x8000001d
  [       7]  0x80000013
  [       8]  0x00000000
  [       9]  0x8000001f
  [      10]  0x8000001c
  [      11]  0x00000000
  [      12]  0x8000000f
  [      13]  0x00000000
  [      14]  0x00000000
  [      15]  0x80000008

Virtual Address Trace
  VA 0x00003385 (decimal:    13189) --> PA or invalid address?
  VA 0x0000231d (decimal:     8989) --> PA or invalid address?
  VA 0x000000e6 (decimal:      230) --> PA or invalid address?
  VA 0x00002e0f (decimal:    11791) --> PA or invalid address?
  VA 0x00001986 (decimal:     6534) --> PA or invalid address?

For each virtual address, write down the physical address it translates to
OR write down that it is an out-of-bounds address (e.g., segfault).
```

손으로 풀기: 1KB 페이지 → offset 10비트, 16KB → VA 14비트 → VPN 4비트.

```text
0x3385 = 11 0011 1000 0101₂ → VPN = 0x3385 >> 10 = 12, offset = 0x385
         PT[12] = 0x8000000f → valid, PFN 0xf → PA = (0xf << 10) | 0x385 = 0x3c00 | 0x385 = 0x3f85
0x231d → VPN 8  → PT[8]  = 0x00000000 → invalid
0x00e6 → VPN 0  → PT[0]  = 0x80000018 → PFN 0x18 → 0x6000 | 0xe6 = 0x60e6
0x2e0f → VPN 11 → PT[11] = 0 → invalid
0x1986 → VPN 6  → PT[6]  = 0x8000001d → PFN 0x1d → 0x7400 | 0x186 = 0x7586
```

`-c` 로 정답 확인:

```text
$ python3 paging-linear-translate.py -P 1k -a 16k -p 32k -v -u 50 -c
Virtual Address Trace
  VA 0x00003385 (decimal:    13189) --> 00003f85 (decimal    16261) [VPN 12]
  VA 0x0000231d (decimal:     8989) -->  Invalid (VPN 8 not valid)
  VA 0x000000e6 (decimal:      230) --> 000060e6 (decimal    24806) [VPN 0]
  VA 0x00002e0f (decimal:    11791) -->  Invalid (VPN 11 not valid)
  VA 0x00001986 (decimal:     6534) --> 00007586 (decimal    30086) [VPN 6]
```

`-u` 를 바꿔 가며 (같은 seed 0):

```text
-u 0 : valid PTEs 0 / 16, invalid VAs 5 / 5
-u 25 : valid PTEs 6 / 16, invalid VAs 4 / 5
-u 50 : valid PTEs 9 / 16, invalid VAs 2 / 5
-u 75 : valid PTEs 16 / 16, invalid VAs 0 / 5
-u 100 : valid PTEs 16 / 16, invalid VAs 0 / 5
```

사용률이 올라갈수록 valid PTE가 늘고 invalid 주소(=segfault)가 줄어든다. 그런데 **선형 테이블의 크기는 -u와 상관없이 항상 16개** — 안 쓰는 영역도 PTE 자리를 차지한다. 이 낭비가 Ch.20의 출발점.

**(Q4) 한계**: 주소 공간 > 물리 메모리면?

```text
$ python3 paging-linear-translate.py -P 1k -a 64k -p 32k -v
Error: physical memory size must be GREATER than address space size (for this simulation)
```

이 시뮬레이터는 스왑이 없으니 막아 둔 것이다. 실제 OS는 Ch.21의 스왑으로 이 제약을 깬다.

## 9. 펌웨어 엔지니어의 눈으로

- **SSD FTL의 L2P 테이블 = 거대한 선형 페이지 테이블.** LBA(=VPN) → NAND 물리 페이지(=PFN). 4KB 매핑 단위면 1TB SSD에 PTE 2^28개 × 4B = **1GB DRAM** — "SSD DRAM은 용량의 1/1000" 경험칙이 바로 18.2절 계산이다. DRAM-less SSD가 HMB(Host Memory Buffer)나 매핑 캐시를 쓰는 것은 Ch.19(TLB)·Ch.20(계층 테이블) 아이디어 그대로다.
- **valid 비트 ≈ FTL의 unmapped/trimmed 상태.** TRIM된 LBA를 읽으면 NAND에 가지 않고 0을 돌려주는 것은 "invalid PTE에는 프레임을 할당하지 않는다"와 같은 원리.
- **IOMMU/SMMU/DART 도 페이지 테이블이다.** 디바이스가 내는 주소(IOVA)를 VPN/offset으로 자르고 IO 페이지 테이블로 PA를 찾는다. Apple DART는 페이지가 **16KB**라서, 4KB 정렬만 맞춘 DMA 버퍼는 매핑 시 앞뒤 페이지까지 디바이스에 노출된다 — 보안·정렬 버그 단골.
- **PCD/PWT, arm64 MAIR**: 펌웨어가 MMIO 레지스터를 cacheable로 잘못 매핑하면 레지스터 쓰기가 캐시에 머물러 디바이스에 안 간다. "PTE의 캐시 속성 비트"가 PCIe BAR 디버깅에서 실제로 문제가 되는 지점.
- **베어메탈/RTOS (Cortex-M)는 MMU가 없고 MPU만** 있다 → 페이징 없이 영역(region) 단위 보호만. 이 장의 VPN→PFN 개념은 Cortex-A/R급 코어, GPU, NPU의 MMU에서 등장한다.

## 10. 면접 질문

### Q1. 페이징이 세그멘테이션보다 나은 점과 대가는?
<details>
<summary>답 보기</summary>

- **외부 단편화가 없다**: 모든 단위가 같은 크기라 아무 빈 프레임에나 넣으면 된다. free list에서 꺼내기만 하면 됨.
- **유연성**: 힙/스택 성장 방향 같은 사용 패턴 가정이 필요 없고, **희소 주소 공간**을 valid 비트로 싸게 표현.
- 대가: (1) **페이지 테이블이 크다** (32-bit/4KB에서 프로세스당 4MB), (2) **모든 접근에 PTE 읽기가 추가**되어 느리다, (3) 페이지 단위 할당이라 **내부 단편화**.
- 그래서 실제 시스템은 **TLB + 다단계 페이지 테이블**을 함께 쓴다.

</details>

### Q2. 32-bit 주소, 4KB 페이지, 4B PTE일 때 선형 페이지 테이블 크기는? 48-bit/8B PTE면?
<details>
<summary>답 보기</summary>

- offset 12비트, VPN 20비트 → 2^20 × 4B = **4MB/프로세스**. 100개 프로세스면 400MB.
- 48-bit/4KB/8B → 2^36 × 8B = **512GB**. 불가능 → **다단계(4-level) 테이블**이 필수.
- 일반식: `2^(VA비트 − log2(page)) × PTE크기`. 면접관은 **비트 계산 속도**를 본다.

</details>

### Q3. offset은 왜 변환하지 않나? 페이지 크기는 왜 2의 거듭제곱인가?
<details>
<summary>답 보기</summary>

- 페이지는 통째로 프레임에 매핑되므로 **페이지 내부 배치는 바뀌지 않는다** → offset 그대로.
- 2의 거듭제곱이면 VPN/offset 분리가 **비트 마스크와 shift** 로 끝난다 (나눗셈 불필요). PA도 `(PFN << shift) | offset` 으로 **덧셈 없이 OR**. 하드웨어가 싸고 빠르다.

</details>

### Q4. PTE의 valid, present, dirty, accessed 비트는 각각 누가 세팅/클리어하나?
<details>
<summary>답 보기</summary>

- **valid/present, PFN, 권한**: **OS**가 세팅 (매핑 생성, 스왑 인/아웃).
- **accessed(A), dirty(D)**: x86/arm64(v8.1 HAFDBS)에서는 **하드웨어가 자동 세팅**, OS가 주기적으로 **클리어**해서 최근 사용/수정 여부를 샘플링 (교체 정책, write-back 판단).
- x86은 valid와 present가 **P 비트 하나**. P=0이면 폴트 후 OS가 "할당 안 된 주소인지 스왑된 페이지인지"를 자기 자료구조로 판정.

</details>

### Q5. Apple Silicon(16KB 페이지)에서 4KB 가정 코드가 깨지는 예를 들어 보라.
<details>
<summary>답 보기</summary>

- `#define PAGE_SIZE 4096` 하드코딩 → `mmap`/`mprotect` 정렬 오류(EINVAL), 가드 페이지 크기 착오.
- 메모리 사용량 계산(RSS, 공유 메모리 크기)이 4배 어긋남.
- **DMA/IOMMU 매핑**: DART는 16KB 단위라 4KB 정렬 버퍼를 매핑하면 같은 16KB 안의 **이웃 데이터까지 디바이스에 노출**.
- 정답: `sysconf(_SC_PAGESIZE)` / `getpagesize()` / 커널의 `PAGE_SIZE` 매크로 사용.

</details>

## 11. 자가 점검 & 숙제

### 퀴즈 1. 128KB 주소 공간, 2KB 페이지면 VPN과 offset은 몇 비트?
<details>
<summary>답 보기</summary>

128KB = 2^17 → 17비트. 2KB = 2^11 → offset 11비트, VPN 6비트 (페이지 64개).

</details>

### 퀴즈 2. 위 공간에서 PT[3] = PFN 0x1A 일 때 VA 0x1A2C 의 PA는?
<details>
<summary>답 보기</summary>

0x1A2C = 0b1_1010_0010_1100. VPN = 0x1A2C >> 11 = 3, offset = 0x1A2C & 0x7FF = 0x22C. PA = (0x1A << 11) | 0x22C = 0xD000 | 0x22C = **0xD22C**.

</details>

### 퀴즈 3. Figure 18.7 루프에서 i = 300일 때 배열 store가 읽는 PTE의 물리 주소는?
<details>
<summary>답 보기</summary>

VA = 40000 + 4×300 = 41200. VPN = 41200/1024 = 40 (40×1024=40960). PTE 주소 = 1024 + 40×4 = **1184**. (PFN 8 → PA = 8192 + 240 = 8432.)

</details>

### 퀴즈 4. 선형 페이지 테이블에서 "valid 비트로 희소 공간을 지원한다"는데, 그럼 메모리가 절약되는 건 무엇이고 절약되지 않는 건 무엇인가?
<details>
<summary>답 보기</summary>

절약되는 것: invalid 페이지에 **물리 프레임**을 주지 않음. 절약되지 않는 것: invalid 페이지의 **PTE 자체**는 여전히 테이블에 자리를 차지 → 테이블 크기는 주소 공간 크기에 비례. 이것을 줄이는 게 Ch.20.

</details>

### 원문 Homework 중 꼭 해볼 것

- **Q1 (크기 추세)**: 주소 공간·페이지 크기에 따라 선형 테이블이 어떻게 커지는지 — 8.2절에서 실행함. "왜 큰 페이지만 쓰지 않나?"까지 답해 보기.
- **Q2 (-u 변화)**: 사용률이 바뀌어도 테이블 크기는 그대로라는 점을 확인하는 문제.
- **Q3 (이상한 파라미터)**: `-P 8 -a 32 -p 1024`(페이지 8바이트), `-P 1m -a 256m -p 512m` 등을 돌려 보고 **어떤 조합이 비현실적인지**(PTE가 페이지보다 큰 상황, 1MB 페이지의 내부 단편화) 판단하는 문제.

## 12. 다음으로

- 느린 문제 → [Ch.19 TLB](2026-09-30_C19_tlb.md): PTE를 캐시해서 추가 접근을 없앤다.
- 큰 문제 → [Ch.20 더 작은 페이지 테이블](2026-09-30_C20_smaller_page_tables.md): 다단계 테이블.
- 메모리보다 큰 주소 공간 → [Ch.21 스와핑 메커니즘](2026-09-30_C21_swapping_mechanisms.md).
