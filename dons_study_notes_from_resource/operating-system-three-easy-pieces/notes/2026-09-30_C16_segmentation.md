# Ch.16 세그먼트 — 세그먼트마다 base & bounds 를 하나씩

> 📖 원문: [16. Segmentation](../book-md/C16_segmentation.md) · [PDF p.166](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=166) · ⏱️ 읽기 약 40분 · 🔗 선행: [Ch.15](2026-09-30_C15_address_translation.md)

## 0. 한눈에 보기

- base & bounds 는 주소 공간 **전체** 를 연속으로 올려서, 힙과 스택 사이의 빈 공간까지 물리 메모리를 먹는다. 32비트(4GB) 주소 공간이면 아예 불가능.
- 해법: 논리적인 **세그먼트(segment)** — 코드, 힙, 스택 — 마다 base/bounds 쌍을 따로 둔다. 각 세그먼트를 물리 메모리 아무 데나 독립적으로 놓을 수 있으니 **희소(sparse) 주소 공간** 을 지원한다.
- 하드웨어가 "어느 세그먼트냐" 를 아는 방법: VA 의 **상위 비트**(명시적) 또는 주소가 만들어진 방식(암묵적).
- 스택은 **음의 방향으로 자라므로** "자라는 방향" 비트와 음수 오프셋 계산이 필요하다. 세그먼트별 **보호 비트** 로 코드 공유도 가능.
- 새 문제: 크기가 제각각인 세그먼트들 때문에 물리 메모리가 작은 구멍투성이가 된다 → **외부 단편화(external fragmentation)**. 압축(compaction)이나 free-list 알고리즘(Ch.17)으로 완화할 뿐 근본 해결은 아니다.

> **THE CRUX: HOW TO SUPPORT A LARGE ADDRESS SPACE** — "How do we support a large address space with (potentially) a lot of free space between the stack and the heap? ... Imagine, however, a 32-bit address space (4 GB in size); a typical program will only use megabytes of memory, but still would demand that the entire address space be resident in memory."
>
> → 스택과 힙 사이에 (잠재적으로) 큰 빈 공간이 있는 큰 주소 공간을 어떻게 지원할까? 4GB 주소 공간에서 프로그램은 몇 MB 만 쓰는데, 주소 공간 전체를 메모리에 올리라고 요구하면 곤란하다.

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 세그먼트(segment) | 주소 공간의 연속된 한 조각(특정 길이) | 코드, 힙, 스택 |
| 세그먼테이션(segmentation) | 세그먼트마다 base/bounds 쌍을 두는 방식 | MMU 에 레지스터 3쌍 |
| 희소 주소 공간(sparse address space) | 쓰는 부분보다 빈 부분이 훨씬 큰 주소 공간 | 64비트 프로세스 |
| 명시적 방식(explicit) | VA 상위 비트로 세그먼트 선택 | 14비트 VA 의 상위 2비트 |
| 암묵적 방식(implicit) | 주소가 만들어진 방식으로 판단 | PC 기반이면 코드, SP/BP 기반이면 스택 |
| 오프셋(offset) | 세그먼트 시작부터의 거리 | VA 4200 → heap 오프셋 104 |
| 음의 성장(grows negative) | 큰 주소에서 작은 주소로 자람 | 스택 |
| 음수 오프셋(negative offset) | 오프셋 − 최대 세그먼트 크기 | 3KB − 4KB = −1KB |
| 보호 비트(protection bits) | 세그먼트별 R/W/X 권한 | 코드 = Read-Execute |
| 코드 공유(code sharing) | 읽기 전용 세그먼트를 여러 프로세스가 공유 | 공유 라이브러리 |
| 거친/세밀한 세그먼트(coarse/fine-grained) | 몇 개의 큰 세그먼트 vs 수천 개의 작은 세그먼트 | Multics, Burroughs B5000 |
| 세그먼트 테이블(segment table) | 많은 세그먼트를 메모리에 저장하는 표 | x86 GDT/LDT |
| 외부 단편화(external fragmentation) | 빈 공간이 작게 조각나 큰 요청을 못 받음 | 24KB 비었는데 20KB 실패 |
| 압축(compaction) | 세그먼트를 옮겨 빈 공간을 한데 모음 | 비싼 memcpy + 레지스터 갱신 |

## 2. 세그먼테이션: 일반화된 base/bounds (16.1)

1960년대 초부터 있던 아이디어: MMU 에 base/bounds 를 **세그먼트마다** 하나씩. 각 세그먼트는 연속이지만 세그먼트끼리는 물리 메모리에서 떨어져 있어도 된다.

```svg
<svg viewBox="0 0 700 340" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C16-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" style="fill:var(--accent)"/>
    </marker>
  </defs>
  <text x="130" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">Fig 16.1 가상 주소 공간 (16KB)</text>
  <rect x="70" y="35" width="120" height="36" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="130" y="58" text-anchor="middle" fill="currentColor">Code</text>
  <rect x="70" y="71" width="120" height="18" fill="none" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="70" y="89" width="120" height="36" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="130" y="112" text-anchor="middle" fill="currentColor">Heap</text>
  <rect x="70" y="125" width="120" height="162" fill="none" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="130" y="210" text-anchor="middle" fill="currentColor">(free)</text>
  <rect x="70" y="287" width="120" height="36" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="130" y="310" text-anchor="middle" fill="currentColor">Stack</text>
  <g font-size="11" fill="currentColor" text-anchor="end">
    <text x="64" y="40">0KB</text><text x="64" y="75">2KB</text><text x="64" y="93">4KB</text>
    <text x="64" y="129">6KB</text><text x="64" y="291">14KB</text><text x="64" y="327">16KB</text>
  </g>
  <text x="530" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">Fig 16.2 물리 메모리 (64KB)</text>
  <rect x="470" y="35" width="120" height="72" fill="none" stroke="currentColor"/>
  <text x="530" y="75" text-anchor="middle" fill="currentColor">OS</text>
  <rect x="470" y="107" width="120" height="45" fill="none" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="470" y="152" width="120" height="9" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <rect x="470" y="161" width="120" height="18" fill="none" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="470" y="179" width="120" height="9" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <rect x="470" y="188" width="120" height="9" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <rect x="470" y="197" width="120" height="126" fill="none" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="530" y="265" text-anchor="middle" fill="currentColor">(not in use)</text>
  <g font-size="11" fill="currentColor">
    <text x="596" y="160">Stack 26–28KB</text>
    <text x="596" y="187">Code 32–34KB</text>
    <text x="596" y="199">Heap 34–36KB</text>
  </g>
  <g font-size="11" fill="currentColor" text-anchor="end">
    <text x="464" y="40">0KB</text><text x="464" y="111">16KB</text><text x="464" y="183">32KB</text>
    <text x="464" y="327">64KB</text>
  </g>
  <line x1="192" y1="53" x2="466" y2="183" style="stroke:var(--accent)" stroke-width="1.5" marker-end="url(#C16-arrow)"/>
  <line x1="192" y1="107" x2="466" y2="192" style="stroke:var(--accent)" stroke-width="1.5" marker-end="url(#C16-arrow)"/>
  <line x1="192" y1="305" x2="466" y2="157" style="stroke:var(--accent)" stroke-width="1.5" marker-end="url(#C16-arrow)"/>
  <text x="330" y="300" text-anchor="middle" fill="currentColor" font-size="12">사용 중인 세그먼트만 물리 메모리를 차지.</text>
  <text x="330" y="316" text-anchor="middle" fill="currentColor" font-size="12">2KB+2KB+2KB = 6KB (base&amp;bounds 였다면 16KB)</text>
</svg>
```

MMU 가 가진 값 (Figure 16.3):

| 세그먼트 | Base | Size |
|---|---|---|
| Code | 32K | 2K |
| Heap | 34K | 2K |
| Stack | 28K | 2K |

### 2.1 원문 변환 예제

**코드 VA 100**: 코드 세그먼트는 VA 0 에서 시작하므로 오프셋 = 100. `100 < 2KB` OK → PA = 32KB + 100 = 32768 + 100 = **32868**.

**힙 VA 4200**: 그냥 base 에 더하면 34KB + 4200 = 39016 — **틀렸다**. 먼저 **세그먼트 안 오프셋** 을 구해야 한다. 힙은 VA 4KB(4096)에서 시작하므로 오프셋 = 4200 − 4096 = 104. `104 < 2KB` OK → PA = 34KB + 104 = 34816 + 104 = **34920**.

**VA 7KB**(힙 끝 너머): 오프셋 = 7168 − 4096 = 3072 ≥ 2048 → 하드웨어가 범위 위반을 감지, OS 로 트랩 → 프로세스 종료. 이게 바로 **세그멘테이션 폴트(segmentation fault/violation)** 라는 이름의 기원이다.

> **ASIDE — 세그멘테이션 폴트**: 세그먼트 기반 기계에서 불법 주소에 접근하면 나던 오류. 세그먼트를 전혀 쓰지 않는 지금의 기계에서도 이름만 살아남았다.

## 3. 어느 세그먼트인지 어떻게 아나? (16.2)

### 3.1 명시적 방식 — 상위 비트

VAX/VMS 에서 쓴 방식. 세그먼트가 3개면 2비트가 필요하다. 14비트 VA 의 상위 2비트를 세그먼트 번호로, 하위 12비트를 오프셋으로 쓴다.

```svg
<svg viewBox="0 0 700 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <text x="350" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">14비트 VA 해부: 4200 (힙) 과 15KB (스택)</text>
  <g font-size="11" fill="currentColor" text-anchor="middle">
    <text x="85" y="45">13</text><text x="125" y="45">12</text><text x="165" y="45">11</text><text x="205" y="45">10</text>
    <text x="245" y="45">9</text><text x="285" y="45">8</text><text x="325" y="45">7</text><text x="365" y="45">6</text>
    <text x="405" y="45">5</text><text x="445" y="45">4</text><text x="485" y="45">3</text><text x="525" y="45">2</text>
    <text x="565" y="45">1</text><text x="605" y="45">0</text>
  </g>
  <rect x="65" y="52" width="80" height="34" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
  <rect x="145" y="52" width="480" height="34" fill="none" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="ui-monospace, monospace" font-size="15">
    <text x="85" y="75">0</text><text x="125" y="75">1</text><text x="165" y="75">0</text><text x="205" y="75">0</text>
    <text x="245" y="75">0</text><text x="285" y="75">0</text><text x="325" y="75">0</text><text x="365" y="75">1</text>
    <text x="405" y="75">1</text><text x="445" y="75">0</text><text x="485" y="75">1</text><text x="525" y="75">0</text>
    <text x="565" y="75">0</text><text x="605" y="75">0</text>
  </g>
  <text x="105" y="104" text-anchor="middle" fill="currentColor" font-size="12">seg = 01 (heap)</text>
  <text x="385" y="104" text-anchor="middle" fill="currentColor" font-size="12">offset = 0x068 = 104 → PA = 34KB + 104 = 34920</text>
  <rect x="65" y="140" width="80" height="34" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
  <rect x="145" y="140" width="480" height="34" fill="none" stroke="currentColor"/>
  <g fill="currentColor" text-anchor="middle" font-family="ui-monospace, monospace" font-size="15">
    <text x="85" y="163">1</text><text x="125" y="163">1</text><text x="165" y="163">1</text><text x="205" y="163">1</text>
    <text x="245" y="163">0</text><text x="285" y="163">0</text><text x="325" y="163">0</text><text x="365" y="163">0</text>
    <text x="405" y="163">0</text><text x="445" y="163">0</text><text x="485" y="163">0</text><text x="525" y="163">0</text>
    <text x="565" y="163">0</text><text x="605" y="163">0</text>
  </g>
  <text x="105" y="192" text-anchor="middle" fill="currentColor" font-size="12">seg = 11 (stack)</text>
  <text x="385" y="192" text-anchor="middle" fill="currentColor" font-size="12">offset = 0xC00 = 3072 (3KB)</text>
  <rect x="120" y="210" width="460" height="74" fill="none" style="stroke:var(--accent)" rx="8"/>
  <text x="350" y="232" text-anchor="middle" fill="currentColor">스택은 음의 방향: 음수 오프셋 = 3KB − 4KB(최대 세그먼트 크기) = −1KB</text>
  <text x="350" y="252" text-anchor="middle" fill="currentColor">범위 검사: |−1KB| ≤ 2KB(size) → OK</text>
  <text x="350" y="272" text-anchor="middle" style="fill:var(--accent)" font-weight="bold">PA = 28KB + (−1KB) = 27KB (27648)</text>
</svg>
```

하드웨어 의사 코드:

```text
// get top 2 bits of 14-bit VA
Segment = (VirtualAddress & SEG_MASK) >> SEG_SHIFT     // SEG_MASK = 0x3000, SEG_SHIFT = 12
// now get offset
Offset  = VirtualAddress & OFFSET_MASK                  // OFFSET_MASK = 0xFFF
if (Offset >= Bounds[Segment])
    RaiseException(PROTECTION_FAULT)
else
    PhysAddr = Base[Segment] + Offset
    Register = AccessMemory(PhysAddr)
```

오프셋을 쓰면 범위 검사도 쉽다: 오프셋 < bounds 만 보면 된다.

부작용: 2비트 = 4개 세그먼트인데 3개만 쓰므로 **주소 공간의 1/4 이 버려진다**. 또 세그먼트당 최대 크기가 4KB 로 제한된다. 그래서 어떤 시스템은 코드와 힙을 한 세그먼트에 넣고 1비트만 쓴다.

### 3.2 암묵적 방식

주소가 **어떻게 만들어졌는지** 로 판단: PC 에서 나온 주소(명령어 fetch) → 코드, 스택 포인터/베이스 포인터 기반 → 스택, 나머지 → 힙. x86 의 CS/SS/DS 기본 세그먼트 선택이 이 방식이다.

## 4. 스택은? (16.3)

스택은 물리 28KB 에서 시작해 **26KB 쪽으로 거꾸로** 자란다 (VA 16KB → 14KB 에 대응). 그래서 하드웨어가 하나 더 알아야 한다: **자라는 방향**.

| 세그먼트 | Base | Size | Grows Positive? |
|---|---|---|---|
| Code | 32K | 2K | 1 |
| Heap | 34K | 2K | 1 |
| Stack | 28K | 2K | 0 |

### 4.1 원문 예제: VA 15KB

1. VA 15KB = 15360 = `11 1100 0000 0000` (0x3C00).
2. 상위 2비트 `11` → 스택 세그먼트. 오프셋 = 0xC00 = 3KB.
3. 음수 오프셋 = 오프셋 − **최대 세그먼트 크기**(12비트 오프셋이므로 4KB) = 3KB − 4KB = **−1KB**.
4. 범위 검사: |−1KB| ≤ 2KB(size) → OK.
5. PA = base + 음수 오프셋 = 28KB − 1KB = **27KB** (27648).

### 4.2 경계 예제 (새로 계산, 아래 C 코드로 검증)

| VA | 세그먼트/오프셋 | 음수 오프셋 | 검사 | PA |
|---|---|---|---|---|
| 16383 (16KB−1, 0x3FFF) | 11 / 4095 | 4095 − 4096 = −1 | 1 ≤ 2048 | 28671 (스택 맨 위 바이트) |
| 14436 (14KB+100) | 11 / 2148 | 2148 − 4096 = −1948 | 1948 ≤ 2048 | 28672 − 1948 = **26724** |
| 14336 (14KB, 0x3800) | 11 / 2048 | −2048 | 2048 ≤ 2048 | 26624 = 26KB (스택 맨 아래 바이트) |
| 14335 (0x37FF) | 11 / 2047 | −2049 | 2049 > 2048 | **fault** |

> **주의 — "less than" vs "이하"**: 원문은 "음수 오프셋의 절댓값이 세그먼트 크기보다 **작은지(less than)**" 검사한다고 쓰지만, 그러면 VA 14KB(음수 오프셋 정확히 −2KB)가 거부되어 2KB 스택 중 1바이트를 못 쓴다. 음수 오프셋은 −1 ~ −size 범위이므로 **|neg| ≤ size** 가 맞다. 아래 OSTEP 시뮬레이터도 VA 108 (−20, limit 20)을 VALID 로 처리한다 — 즉 ≤ 를 쓴다.

## 5. 공유 지원 (16.4)

메모리를 아끼려고 세그먼트를 주소 공간끼리 **공유** 하고 싶다. 특히 코드. 이를 위해 세그먼트마다 **보호 비트** 를 둔다.

| 세그먼트 | Base | Size | Grows Positive? | Protection |
|---|---|---|---|---|
| Code | 32K | 2K | 1 | Read-Execute |
| Heap | 34K | 2K | 1 | Read-Write |
| Stack | 28K | 2K | 0 | Read-Write |

코드를 읽기 전용으로 하면 같은 물리 코드를 여러 프로세스의 주소 공간에 매핑해도 격리가 깨지지 않는다. 각 프로세스는 자기만의 메모리라고 믿지만 OS 는 몰래 공유한다.

하드웨어 알고리즘도 바뀐다: 범위 검사에 더해 **접근 종류 검사**. 읽기 전용 세그먼트에 쓰기, 실행 불가 세그먼트에서 실행 → 예외.

## 6. 거친 vs 세밀한 세그먼테이션 (16.5)

- **거친(coarse-grained)**: 코드/힙/스택처럼 몇 개의 큰 세그먼트. 레지스터로 충분.
- **세밀한(fine-grained)**: Multics, Burroughs B5000 처럼 수천 개의 작은 세그먼트. 메모리에 **세그먼트 테이블** 이 필요. 컴파일러가 코드와 데이터를 잘게 쪼개 주면 OS 가 어떤 세그먼트가 쓰이는지 더 잘 알고 메모리를 효율적으로 쓸 것이라는 기대였다.

## 7. OS 지원 (16.6)

1. **컨텍스트 스위치**: 세그먼트 레지스터들을 저장/복원.
2. **세그먼트 성장**: `malloc` 이 힙을 키워 달라고 하면(`sbrk`) OS 가 세그먼트 크기 레지스터를 늘려 준다(공간이 없으면 거절).
3. **빈 공간 관리** — 가장 중요. 이제 세그먼트 크기가 제각각이라 물리 메모리를 "슬롯 배열" 로 볼 수 없다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <text x="170" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">Not Compacted</text>
  <text x="490" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">Compacted</text>
  <g>
    <rect x="110" y="30" width="120" height="60" fill="none" stroke="currentColor"/>
    <text x="170" y="65" text-anchor="middle" fill="currentColor">OS</text>
    <rect x="110" y="90" width="120" height="30" fill="none" stroke="#d9534f" stroke-dasharray="4 3"/>
    <text x="170" y="110" text-anchor="middle" fill="#d9534f" font-size="11">free 8KB</text>
    <rect x="110" y="120" width="120" height="30" style="fill:var(--accent-soft)" stroke="currentColor"/>
    <text x="170" y="140" text-anchor="middle" fill="currentColor" font-size="11">Allocated</text>
    <rect x="110" y="150" width="120" height="30" fill="none" stroke="#d9534f" stroke-dasharray="4 3"/>
    <text x="170" y="170" text-anchor="middle" fill="#d9534f" font-size="11">free 8KB</text>
    <rect x="110" y="180" width="120" height="30" style="fill:var(--accent-soft)" stroke="currentColor"/>
    <text x="170" y="200" text-anchor="middle" fill="currentColor" font-size="11">Allocated</text>
    <rect x="110" y="210" width="120" height="30" fill="none" stroke="#d9534f" stroke-dasharray="4 3"/>
    <text x="170" y="230" text-anchor="middle" fill="#d9534f" font-size="11">free 8KB</text>
    <rect x="110" y="240" width="120" height="30" style="fill:var(--accent-soft)" stroke="currentColor"/>
    <text x="170" y="260" text-anchor="middle" fill="currentColor" font-size="11">Allocated</text>
    <g font-size="11" fill="currentColor" text-anchor="end">
      <text x="104" y="34">0KB</text><text x="104" y="94">16KB</text><text x="104" y="124">24KB</text>
      <text x="104" y="154">32KB</text><text x="104" y="184">40KB</text><text x="104" y="214">48KB</text>
      <text x="104" y="244">56KB</text><text x="104" y="274">64KB</text>
    </g>
  </g>
  <g>
    <rect x="430" y="30" width="120" height="60" fill="none" stroke="currentColor"/>
    <text x="490" y="65" text-anchor="middle" fill="currentColor">OS</text>
    <rect x="430" y="90" width="120" height="90" style="fill:var(--accent-soft)" stroke="currentColor"/>
    <text x="490" y="140" text-anchor="middle" fill="currentColor" font-size="11">Allocated ×3</text>
    <rect x="430" y="180" width="120" height="90" fill="none" style="stroke:var(--accent)" stroke-width="2" stroke-dasharray="4 3"/>
    <text x="490" y="222" text-anchor="middle" style="fill:var(--accent)" font-size="12" font-weight="bold">free 24KB</text>
    <text x="490" y="240" text-anchor="middle" fill="currentColor" font-size="11">(연속)</text>
    <g font-size="11" fill="currentColor" text-anchor="end">
      <text x="424" y="34">0KB</text><text x="424" y="94">16KB</text><text x="424" y="184">40KB</text><text x="424" y="274">64KB</text>
    </g>
  </g>
  <text x="170" y="292" text-anchor="middle" fill="currentColor" font-size="12">20KB 요청 → 실패 (빈 곳 합계 24KB)</text>
  <text x="490" y="292" text-anchor="middle" fill="currentColor" font-size="12">20KB 요청 → 성공 (대신 복사 비용)</text>
</svg>
```

이게 **외부 단편화**: 빈 공간 합계(24KB)는 충분한데 한 덩어리가 아니라서 20KB 요청을 못 들어준다.

대응책:

- **압축(compaction)**: 프로세스를 멈추고 세그먼트를 한쪽으로 복사해 모은 뒤 세그먼트 레지스터를 고친다. 효과는 확실하지만 **메모리 복사가 비싸고 CPU 를 많이 쓴다**. 게다가 압축해 놓으면 기존 세그먼트를 키우기가 오히려 어려워진다.
- **free-list 관리 알고리즘**: best-fit, worst-fit, first-fit, buddy 등으로 큰 빈 덩어리를 최대한 유지. 하지만 **어떤 알고리즘도 외부 단편화를 없애진 못하고 줄일 뿐이다** (Ch.17).

> **TIP — 해법이 1000개면 좋은 해법은 없다(IF 1000 SOLUTIONS EXIST, NO GREAT ONE DOES)**: 알고리즘이 그렇게 많다는 건 "최선" 이 없다는 증거. 진짜 해법은 문제를 피하는 것 — **가변 크기 할당을 아예 하지 않는 것**(= 페이징).

## 8. 요약 (16.7)

장점:

- 동적 재배치를 넘어 **희소 주소 공간** 을 지원 — 세그먼트 사이 빈 공간에 물리 메모리를 안 쓴다.
- 빠르다: 변환 산술이 단순해 하드웨어에 잘 맞는다.
- 덤으로 **코드 공유**.

단점:

- **외부 단편화** — 가변 크기 세그먼트의 숙명.
- **아직 충분히 유연하지 않다**: 크지만 듬성듬성 쓰는 힙이 한 세그먼트라면 그 힙 **전체** 가 메모리에 있어야 한다. 주소 공간 사용 방식이 세그먼트 설계와 딱 맞지 않으면 잘 안 된다.

## 9. 직접 해보기

### 9.1 세그먼트 번역기 — `code/C16_segmentation.c`

Figure 16.5 의 레지스터 값으로 14비트 VA 를 번역한다. 음수 성장 스택과 보호 비트까지.

```c
// C16_segmentation.c — 원문 Figure 16.5 의 세그먼트 레지스터로 14-bit VA 를 번역한다
// build: cc -Wall -Wextra -O0 code/C16_segmentation.c -o .work/bin/C16_segmentation
//  - 상위 2비트 = 세그먼트 번호 (00 code, 01 heap, 11 stack), 하위 12비트 = 오프셋
//  - stack 은 음의 방향으로 자람: 오프셋 - 최대 세그먼트 크기(4KB) = 음수 오프셋
//  - 보호 비트(R/W/X) 검사까지
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define KB 1024
#define SEG_MASK    0x3000
#define SEG_SHIFT   12
#define OFFSET_MASK 0x0FFF
#define MAX_SEG     (1 << SEG_SHIFT)        // 4KB: 세그먼트 하나가 가질 수 있는 최대 크기

enum { R = 4, W = 2, X = 1 };
struct seg { const char *name; int base, size, grows_pos, prot, valid; };

// Figure 16.5 (세그먼트 2 는 미사용)
static struct seg segs[4] = {
    {"code",  32 * KB, 2 * KB, 1, R | X, 1},
    {"heap",  34 * KB, 2 * KB, 1, R | W, 1},
    {"-",     0,       0,      1, 0,     0},
    {"stack", 28 * KB, 2 * KB, 0, R | W, 1},
};

static const char *acc_name(int a) { return a == R ? "read" : a == W ? "write" : "exec"; }

static void translate(int va, int access) {
    int s = (va & SEG_MASK) >> SEG_SHIFT;
    int off = va & OFFSET_MASK;
    struct seg *g = &segs[s];
    printf("  VA %5d (0x%04x, seg=%d%d off=%4d) %-5s: ", va, va, (s >> 1) & 1, s & 1, off, acc_name(access));
    if (!g->valid) { printf("FAULT (segment %d 미사용)\n", s); return; }
    int pa;
    if (g->grows_pos) {
        if (off >= g->size) { printf("FAULT %s: off %d >= size %d\n", g->name, off, g->size); return; }
        pa = g->base + off;
        printf("%s  PA = %d + %d", g->name, g->base, off);
    } else {
        int neg = off - MAX_SEG;                // 음수 오프셋
        if (abs(neg) > g->size) { printf("FAULT %s: |%d| > size %d\n", g->name, neg, g->size); return; }
        pa = g->base + neg;
        printf("%s  neg off = %d - %d = %d, PA = %d + (%d)", g->name, off, MAX_SEG, neg, g->base, neg);
    }
    if (!(g->prot & access)) { printf(" -> PROTECTION FAULT (%s 금지)\n", acc_name(access)); return; }
    printf(" = %d (%.2f KB)\n", pa, pa / 1024.0);
}

int main(void) {
    printf("[1] 원문 예제\n");
    translate(100, X);                           // code: 100 + 32KB = 32868
    translate(4200, R);                          // heap: 4200-4096=104, 34KB+104 = 34920
    translate(7 * KB, R);                        // heap 끝 너머 → fault
    translate(15 * KB, W);                       // stack: 3KB-4KB = -1KB, 28KB-1KB = 27KB

    printf("\n[2] 스택 경계 확인 (스택은 VA 14KB..16KB, PA 26KB..28KB)\n");
    translate(16 * KB - 1, R);                   // 가장 위 바이트: off 4095 → -1
    translate(14 * KB, R);                       // 가장 아래: off 2048 → -2048 (|−2048| == size, 허용)
    translate(14 * KB - 1, R);                   // 한 칸 더 아래 → fault

    printf("\n[3] 보호 비트\n");
    translate(100, W);                           // 코드 세그먼트에 쓰기 → protection fault
    translate(4200, X);                          // heap 에서 실행 → protection fault (NX 와 같은 발상)

    printf("\n[4] 새 예제: heap 이 1KB 커졌다 (OS 가 heap size 2K -> 3K, sbrk 흉내)\n");
    translate(4096 + 2500, R);                   // 성장 전: fault
    segs[1].size = 3 * KB;
    translate(4096 + 2500, R);                   // 성장 후: 34KB + 2500
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 code/C16_segmentation.c -o .work/bin/C16_segmentation && .work/bin/C16_segmentation
[1] 원문 예제
  VA   100 (0x0064, seg=00 off= 100) exec : code  PA = 32768 + 100 = 32868 (32.10 KB)
  VA  4200 (0x1068, seg=01 off= 104) read : heap  PA = 34816 + 104 = 34920 (34.10 KB)
  VA  7168 (0x1c00, seg=01 off=3072) read : FAULT heap: off 3072 >= size 2048
  VA 15360 (0x3c00, seg=11 off=3072) write: stack  neg off = 3072 - 4096 = -1024, PA = 28672 + (-1024) = 27648 (27.00 KB)

[2] 스택 경계 확인 (스택은 VA 14KB..16KB, PA 26KB..28KB)
  VA 16383 (0x3fff, seg=11 off=4095) read : stack  neg off = 4095 - 4096 = -1, PA = 28672 + (-1) = 28671 (28.00 KB)
  VA 14336 (0x3800, seg=11 off=2048) read : stack  neg off = 2048 - 4096 = -2048, PA = 28672 + (-2048) = 26624 (26.00 KB)
  VA 14335 (0x37ff, seg=11 off=2047) read : FAULT stack: |-2049| > size 2048

[3] 보호 비트
  VA   100 (0x0064, seg=00 off= 100) write: code  PA = 32768 + 100 -> PROTECTION FAULT (write 금지)
  VA  4200 (0x1068, seg=01 off= 104) exec : heap  PA = 34816 + 104 -> PROTECTION FAULT (exec 금지)

[4] 새 예제: heap 이 1KB 커졌다 (OS 가 heap size 2K -> 3K, sbrk 흉내)
  VA  6596 (0x19c4, seg=01 off=2500) read : FAULT heap: off 2500 >= size 2048
  VA  6596 (0x19c4, seg=01 off=2500) read : heap  PA = 34816 + 2500 = 37316 (36.44 KB)
```

(경고 0개.) 원문 숫자 32868, 34920, 7KB fault, 27KB 가 전부 일치. [4] 는 OS 가 세그먼트 크기 레지스터만 늘려 주면 힙이 자란다는 걸 보여 준다 — 단, 이 예에서 힙 뒤(36KB 이후)가 비어 있었기에 가능했다. 뒤에 다른 세그먼트가 있었다면 힙 전체를 옮기거나 거절해야 한다.

### 9.2 OSTEP 시뮬레이터 — segmentation.py

이 시뮬레이터는 세그먼트가 2개: **세그먼트 0**(양의 방향, VA 하단) 과 **세그먼트 1**(음의 방향, VA 상단). VA 의 최상위 비트로 고른다.

```text
cd .tools/ostep-homework/vm-segmentation
python3 segmentation.py -a 128 -p 512 -b 0 -l 20 -B 512 -L 20 -s 0 -c
```

```text
ARG seed 0
ARG address space size 128
ARG phys mem size 512

Segment register information:

  Segment 0 base  (grows positive) : 0x00000000 (decimal 0)
  Segment 0 limit                  : 20

  Segment 1 base  (grows negative) : 0x00000200 (decimal 512)
  Segment 1 limit                  : 20

Virtual Address Trace
  VA  0: 0x0000006c (decimal:  108) --> VALID in SEG1: 0x000001ec (decimal:  492)
  VA  1: 0x00000061 (decimal:   97) --> SEGMENTATION VIOLATION (SEG1)
  VA  2: 0x00000035 (decimal:   53) --> SEGMENTATION VIOLATION (SEG0)
  VA  3: 0x00000021 (decimal:   33) --> SEGMENTATION VIOLATION (SEG0)
  VA  4: 0x00000041 (decimal:   65) --> SEGMENTATION VIOLATION (SEG1)
```

손으로 풀기 (주소 공간 128 = 7비트, 최상위 비트 = 64 의 자리, 세그먼트 최대 크기 = 64):

| VA | 최상위 비트 → 세그먼트 | 오프셋 | 계산 | 결과 |
|---|---|---|---|---|
| 108 | 1 → SEG1 (음) | 108 − 64 = 44 | 44 − 64 = −20, abs(−20) = 20 ≤ 20 | PA = 512 − 20 = **492** ✔ |
| 97 | 1 → SEG1 | 33 | 33 − 64 = −31, 31 > 20 | violation ✔ |
| 53 | 0 → SEG0 (양) | 53 | 53 ≥ 20 | violation ✔ |
| 33 | 0 → SEG0 | 33 | 33 ≥ 20 | violation ✔ |
| 65 | 1 → SEG1 | 1 | 1 − 64 = −63, 63 > 20 | violation ✔ |

seed 1 도 같은 방법으로:

```text
python3 segmentation.py -a 128 -p 512 -b 0 -l 20 -B 512 -L 20 -s 1 -c
...
Virtual Address Trace
  VA  0: 0x00000011 (decimal:   17) --> VALID in SEG0: 0x00000011 (decimal:   17)
  VA  1: 0x0000006c (decimal:  108) --> VALID in SEG1: 0x000001ec (decimal:  492)
  VA  2: 0x00000061 (decimal:   97) --> SEGMENTATION VIOLATION (SEG1)
  VA  3: 0x00000020 (decimal:   32) --> SEGMENTATION VIOLATION (SEG0)
  VA  4: 0x0000003f (decimal:   63) --> SEGMENTATION VIOLATION (SEG0)
```

17 은 SEG0 의 17 < 20 → PA 0 + 17 = 17.

**숙제 Q2**: 같은 설정에서 SEG0 의 최고 합법 VA = **19**, SEG1 의 최저 합법 VA = 128 − 20 = **108**. 전체에서 최저 불법 = **20**, 최고 불법 = **107**. `-A` 로 확인:

```text
python3 segmentation.py -a 128 -p 512 -b 0 -l 20 -B 512 -L 20 -s 0 -A 19,20,107,108,127 -c
...
Virtual Address Trace
  VA  0: 0x00000013 (decimal:   19) --> VALID in SEG0: 0x00000013 (decimal:   19)
  VA  1: 0x00000014 (decimal:   20) --> SEGMENTATION VIOLATION (SEG0)
  VA  2: 0x0000006b (decimal:  107) --> SEGMENTATION VIOLATION (SEG1)
  VA  3: 0x0000006c (decimal:  108) --> VALID in SEG1: 0x000001ec (decimal:  492)
  VA  4: 0x0000007f (decimal:  127) --> VALID in SEG1: 0x000001ff (decimal:  511)
```

예측 그대로. VA 127(주소 공간 맨 끝)은 음수 오프셋 −1 → PA 511 (물리 메모리 맨 끝 바이트).

**숙제 Q3**: 16바이트 주소 공간, 128바이트 물리 메모리에서 결과가 "valid, valid, violation × 12, valid, valid" 가 되게 하려면? VA 0, 1 만 SEG0 에서 합법 → `l0 = 2`. VA 14, 15 만 SEG1 에서 합법 → 음수 오프셋 −2, −1 → `l1 = 2`. base 는 물리 메모리 안이면 아무거나(SEG1 은 끝 주소 기준이므로 128 로 두면 맨 끝 두 바이트).

```text
python3 segmentation.py -a 16 -p 128 -A 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 --b0 0 --l0 2 --b1 128 --l1 2 -c
...
  VA  0: 0x00000000 (decimal:    0) --> VALID in SEG0: 0x00000000 (decimal:    0)
  VA  1: 0x00000001 (decimal:    1) --> VALID in SEG0: 0x00000001 (decimal:    1)
  VA  2: 0x00000002 (decimal:    2) --> SEGMENTATION VIOLATION (SEG0)
  ...
  VA 13: 0x0000000d (decimal:   13) --> SEGMENTATION VIOLATION (SEG1)
  VA 14: 0x0000000e (decimal:   14) --> VALID in SEG1: 0x0000007e (decimal:  126)
  VA 15: 0x0000000f (decimal:   15) --> VALID in SEG1: 0x0000007f (decimal:  127)
```

(가운데 VA 3~12 는 모두 VIOLATION, 생략.) 정확히 원하는 패턴.

## 10. 펌웨어 엔지니어의 눈으로

- **ARM MPU 는 "변환 없는 세그먼트 + 보호 비트"** 다. SSD FW 에서 코드 영역 RX, 데이터 RW+XN, 공유 버퍼 영역 RW, 스택 아래 guard region no-access — 이 표가 Figure 16.5 의 Protection 열과 정확히 같은 모양이다. "heap 에서 실행 금지" 는 XN(eXecute Never) 비트.
- **스택 guard 와 음의 성장**: RTOS 태스크 스택이 아래로 자라 오버플로하면 옆 태스크 TCB 를 깨뜨린다. MPU region 을 스택 바닥에 두어 fault 로 잡는 건, 이 장의 "|음수 오프셋| > size → fault" 를 하드웨어에 맡기는 것과 같다.
- **외부 단편화는 펌웨어의 현실 문제**: 장시간 도는 FW 에서 가변 크기 DMA 버퍼를 할당/해제하면 SRAM 이 조각나 큰 버퍼(예: 한 NAND 페이지 16KB 이상) 할당이 실패한다. 그래서 고정 크기 블록 풀을 쓰거나 부팅 때 미리 잡는다 — TIP 의 "가변 크기 할당을 피하라" 그대로.
- **x86 세그먼트의 잔재**: 64비트 모드에서 CS/DS/SS 의 base 는 0 으로 고정되고(평탄 모델), FS/GS 만 base 가 살아서 **스레드 로컬 저장소(TLS)** 와 커널 per-CPU 데이터 포인터로 쓰인다. 면접에서 "세그먼트는 지금도 쓰이나?" 에 좋은 답.
- **PCIe/SoC 주소 창**: SoC 인터커넥트의 주소 디코더는 "주소 상위 비트 → 어느 슬레이브(DRAM, PCIe 창, 레지스터 블록)" 를 고르고, 그 안에서 오프셋으로 접근한다. 명시적 세그먼트 선택과 같은 구조다.

## 11. 면접 질문

### Q1. 세그먼테이션이 base & bounds 보다 나은 점과 남은 문제는?
<details>
<summary>답 보기</summary>

나은 점: 세그먼트(코드/힙/스택)마다 base/bounds 를 둬서 **사용하는 부분만 물리 메모리에 올린다** → 희소 주소 공간 지원, 힙-스택 사이 낭비 제거. 세그먼트별 **보호 비트** 로 코드 공유도 가능.
남은 문제: 세그먼트 크기가 제각각이라 **외부 단편화**. 또 한 세그먼트 안이 듬성듬성하면(큰 힙) 그 세그먼트 **전체** 를 올려야 한다. → 고정 크기 **페이징** 으로.

</details>

### Q2. 14비트 VA, 상위 2비트 세그먼트, 스택 base = 28KB, size = 2KB, 음의 성장. VA 0x3A00 의 PA 는?
<details>
<summary>답 보기</summary>

0x3A00 = 14848. 상위 2비트 = 11 → 스택. 오프셋 = 0xA00 = 2560.
음수 오프셋 = 2560 − 4096 = **−1536**. |−1536| ≤ 2048 → OK.
PA = 28672 − 1536 = **27136** (26.5KB).

</details>

### Q3. 외부 단편화와 내부 단편화의 차이는? 각각 어디서 생기나?
<details>
<summary>답 보기</summary>

**내부 단편화**: 할당된 단위 **안** 에서 안 쓰는 공간. base & bounds 의 힙-스택 사이, 버디 할당기의 2의 거듭제곱 반올림, 페이지의 마지막 남는 부분.
**외부 단편화**: 할당 단위들 **사이** 의 빈 공간이 잘게 쪼개져 합계는 충분한데 연속 공간이 없어 요청을 못 받음. 가변 크기 세그먼트, malloc 의 가변 크기 청크.
페이징은 외부 단편화를 없애는 대신 페이지 내부 단편화를 받아들인다.

</details>

### Q4. 압축(compaction)을 하면 외부 단편화가 해결되는데 왜 잘 안 쓰나?
<details>
<summary>답 보기</summary>

1. **비싸다**: 프로세스를 멈추고 MB~GB 단위로 메모리를 복사해야 한다(메모리 대역폭 + CPU 시간).
2. 세그먼트 레지스터(또는 포인터)를 모두 갱신해야 한다. 유저 레벨 malloc 은 포인터가 어디 저장됐는지 몰라서 **아예 불가능**.
3. 압축 후 기존 세그먼트를 키우기 어려워질 수 있다.
4. DMA 중인 메모리는 옮길 수 없다(펌웨어/드라이버 관점에서 중요).
그래서 OS 는 압축 대신 페이징을 택했고, Linux 는 huge page 확보 같은 특수 목적에만 페이지 단위 compaction 을 쓴다.

</details>

### Q5. 세그먼트를 고르는 명시적 방식과 암묵적 방식을 비교하라.
<details>
<summary>답 보기</summary>

**명시적**: VA 상위 비트가 세그먼트 번호. 단순하고 빠르지만 비트 수만큼 세그먼트 개수와 **세그먼트당 최대 크기가 고정** 되고, 안 쓰는 세그먼트 번호만큼 주소 공간이 낭비된다(2비트 중 3개만 쓰면 1/4).
**암묵적**: 주소를 만든 방식으로 판단(PC → 코드, SP/BP → 스택, 나머지 → 데이터). 주소 비트를 안 쓰지만 하드웨어가 주소 생성 경로를 추적해야 하고, 스택 변수의 주소를 일반 포인터로 넘기면 꼬인다. x86 의 기본 세그먼트 레지스터(CS/SS/DS)가 이 방식.

</details>

## 12. 자가 점검 & 숙제

### 퀴즈 1. Figure 16.4 설정에서 VA 5000 과 VA 3000 의 결과는?
<details>
<summary>답 보기</summary>

- 5000: 상위 2비트 01 → 힙. 오프셋 5000 − 4096 = 904 < 2048 → PA = 34816 + 904 = **35720**.
- 3000: 상위 2비트 00 → 코드. 오프셋 3000 ≥ 2048 → **fault** (VA 2KB~4KB 는 코드 세그먼트 번호지만 세그먼트 밖).

</details>

### 퀴즈 2. 왜 스택의 음수 오프셋을 구할 때 "세그먼트 크기(2KB)" 가 아니라 "최대 세그먼트 크기(4KB)" 를 빼나?
<details>
<summary>답 보기</summary>

스택 세그먼트의 **가상 주소 끝**(16KB)이 물리 base(28KB)에 대응하기 때문. 오프셋 필드가 12비트라 세그먼트의 가상 범위는 0 ~ 4KB 이고, 스택은 그 끝(4KB)에서 아래로 자란다. 따라서 "끝에서 얼마나 내려왔나" = 오프셋 − 4KB. 실제 크기(2KB)는 범위 검사에만 쓴다.

</details>

### 퀴즈 3. 코드 세그먼트를 공유하려면 무엇이 반드시 필요한가?
<details>
<summary>답 보기</summary>

**보호 비트로 읽기 전용(Read-Execute)** 설정. 쓰기가 가능하면 한 프로세스가 공유 코드를 바꿔 다른 프로세스에 영향을 준다(격리 붕괴).

</details>

### 퀴즈 4. segmentation.py 에서 `-a 128 -L 20 -B 512` 일 때 SEG1 의 합법 VA 범위와 PA 범위는?
<details>
<summary>답 보기</summary>

VA **108 ~ 127**, PA **492 ~ 511**. (VA 127 → −1 → 511, VA 108 → −20 → 492.)

</details>

### 원문 Homework 중 꼭 해볼 것

- **Q1/Q2**: 위에서 풀었다 → **음의 성장 세그먼트의 경계 계산** 을 손으로 할 수 있는지 확인하는 문제.
- **Q4 (약 90% 가 valid 가 되게)**: 합법 비율 ≈ (l0 + l1) / asize 이므로 두 limit 합을 asize 의 90% 로 잡는다. 실제로 `-a 128 -p 512 -b 0 -l 58 -B 512 -L 58 -s 0 -n 1000 -c` 를 돌리면 1000개 중 **910개** 가 VALID (이론 116/128 = 90.6%) → **어떤 파라미터가 비율을 결정하는지** 보는 문제.
- **Q5 (valid 가 하나도 없게)**: `-l 0 -L 0` → limit 0 이면 모든 접근이 위반(같은 설정으로 `-n 1000` 을 돌려 VALID 0개 확인). **bounds 의 의미** 를 극단값으로 확인.

## 13. 다음으로

- 다음 장: [Ch.17 빈 공간 관리](2026-09-30_C17_free_space_management.md) — 외부 단편화를 줄이는 free-list 알고리즘들(best/worst/first fit, buddy, slab).
- 그 다음: [Ch.18 페이징](2026-09-30_C18_paging_intro.md) — 가변 크기를 버리고 고정 크기 페이지로.
- 이전 장: [Ch.15 주소 변환](2026-09-30_C15_address_translation.md)
