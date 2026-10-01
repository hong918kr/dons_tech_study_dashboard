# Ch.21 물리 메모리 너머로: 메커니즘 — 스왑 공간, present 비트, 페이지 폴트

> 📖 원문: [21. Beyond Physical Memory: Mechanisms](../book-md/C21_beyond_physical_memory_mechanisms.md) · [PDF p.241](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=241) · ⏱️ 읽기 약 40분 · 🔗 선행: [Ch.19 TLB](2026-09-30_C19_tlb.md), [Ch.20 더 작은 페이지 테이블](2026-09-30_C20_smaller_page_tables.md)

## 0. 한눈에 보기

> **CRUX — "How can the OS make use of a larger, slower device to transparently provide the illusion of a large virtual address space?"**
> (OS는 더 크고 느린 장치를 이용해 어떻게 큰 가상 주소 공간이라는 환상을 투명하게 제공할까?)

- 지금까지는 "모든 프로세스의 모든 페이지가 물리 메모리에 들어간다"고 가정했다. 이제 그 가정을 버린다.
- 디스크(또는 SSD)에 **스왑 공간(swap space)** 을 두고, 당장 안 쓰는 페이지를 거기로 내보낸다.
- PTE의 **present 비트**가 "메모리에 있나, 디스크에 있나"를 표시한다. present=0인 페이지를 건드리면 **페이지 폴트(page fault)** → OS의 **페이지 폴트 핸들러**가 디스크에서 읽어 오고 명령어를 재시도한다.
- 메모리가 꽉 차 있으면 먼저 누군가를 쫓아내야 한다(**교체 정책**, Ch.22). 실제 OS는 꽉 찰 때까지 기다리지 않고 **low/high watermark** 사이를 유지하는 **스왑 데몬**을 백그라운드로 돌린다.

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 메모리 계층 (memory hierarchy) | 빠르고 작은 것 위, 느리고 큰 것 아래 | 레지스터 → 캐시 → DRAM → SSD/HDD |
| 스왑 공간 (swap space) | 페이지를 내보내 두는 디스크 영역 | 리눅스 swap 파티션, macOS /System/Volumes/VM |
| swap out / swap in | 메모리 → 디스크 / 디스크 → 메모리 | page out / page in |
| 오버레이 (overlay) | 옛날 프로그래머가 손으로 코드/데이터를 갈아 끼우던 방식 | VM 이전의 고통 |
| present 비트 | 1 = 메모리에 있음, 0 = 디스크에 있음 | PTE의 한 비트 |
| 페이지 폴트 (page fault) | present=0 페이지에 접근 → OS로 트랩 | 사실은 "page miss" |
| 페이지 폴트 핸들러 | 폴트를 처리하는 OS 코드 | 프레임 확보 → 디스크 읽기 → PTE 갱신 → 재시도 |
| 디스크 주소 저장 | 스왑된 페이지의 위치를 PTE의 PFN 자리에 기록 | 리눅스 swap entry |
| minor / major fault | 디스크 I/O 없이 처리 / 디스크 I/O 필요 | zero-fill, 공유 페이지 vs 스왑 인 |
| 교체 정책 (page-replacement policy) | 메모리가 꽉 찼을 때 쫓아낼 페이지 선택 | Ch.22: LRU, clock |
| high/low watermark (HW/LW) | 빈 페이지가 LW 밑이면 데몬 시작, HW까지 비우고 잔다 | 리눅스 kswapd의 min/low/high |
| 스왑 데몬 (swap/page daemon) | 백그라운드로 페이지를 비우는 커널 스레드 | kswapd, macOS의 compressor/pageout |
| clustering | 여러 페이지를 모아 한 번에 디스크에 쓰기 | 탐색/회전 비용 감소 |

## 2. 왜 "물리 메모리보다 큰" 주소 공간인가

- **편리함**: 프로그래머는 "메모리에 자리가 있나?"를 걱정하지 않고 필요한 만큼 할당한다. 그 대안이었던 **메모리 오버레이(overlay)** 는 함수를 부르기 전에 그 코드를 직접 메모리에 올려 두는 방식 — 끔찍하다.
- **멀티프로그래밍**: 여러 프로세스를 동시에 돌리면 모두의 페이지를 메모리에 담을 수 없다. 일부를 내보낼 수 있어야 한다.

메모리 계층의 맨 아래에 **크지만 느린 장치**가 필요하다 — 전통적으로 하드디스크, 지금은 보통 **플래시 SSD**.

> **ASIDE — 저장 기술**: 느린 장치가 꼭 HDD일 필요는 없다. 플래시 SSD도 된다 (자세한 건 [Ch.37 HDD](2026-09-30_C37_hard_disk_drives.md), [부록 I Flash SSD](2026-09-30_C0I_flash_ssd.md)).

## 3. 스왑 공간 (21.1)

디스크에 **페이지 크기 단위로** 읽고 쓸 수 있는 영역을 예약한다. OS는 스왑된 각 페이지의 **디스크 주소**를 기억해야 한다. 스왑 공간의 크기는 "시스템이 동시에 쓸 수 있는 최대 페이지 수"를 결정한다.

```svg
<svg viewBox="0 0 700 220" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <text x="20" y="45" fill="currentColor" font-weight="600">물리 메모리</text>
  <text x="20" y="62" fill="currentColor" font-size="11">(4 프레임)</text>
  <g stroke="currentColor" fill="none">
    <rect x="120" y="25" width="90" height="46" style="fill:var(--accent-soft)"/><rect x="210" y="25" width="90" height="46" style="fill:var(--accent-soft)"/>
    <rect x="300" y="25" width="90" height="46" style="fill:var(--accent-soft)"/><rect x="390" y="25" width="90" height="46" style="fill:var(--accent-soft)"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="12">
    <text x="165" y="18" font-size="11">PFN 0</text><text x="255" y="18" font-size="11">PFN 1</text><text x="345" y="18" font-size="11">PFN 2</text><text x="435" y="18" font-size="11">PFN 3</text>
    <text x="165" y="45">Proc 0</text><text x="165" y="62">[VPN 0]</text>
    <text x="255" y="45">Proc 1</text><text x="255" y="62">[VPN 2]</text>
    <text x="345" y="45">Proc 1</text><text x="345" y="62">[VPN 3]</text>
    <text x="435" y="45">Proc 2</text><text x="435" y="62">[VPN 0]</text>
  </g>
  <text x="20" y="145" fill="currentColor" font-weight="600">스왑 공간</text>
  <text x="20" y="162" fill="currentColor" font-size="11">(8 블록, 디스크)</text>
  <g stroke="currentColor" fill="none">
    <rect x="120" y="125" width="70" height="46"/><rect x="190" y="125" width="70" height="46"/>
    <rect x="260" y="125" width="70" height="46" stroke-dasharray="4 3"/><rect x="330" y="125" width="70" height="46"/>
    <rect x="400" y="125" width="70" height="46"/><rect x="470" y="125" width="70" height="46" style="stroke:var(--accent)" stroke-width="2"/>
    <rect x="540" y="125" width="70" height="46"/><rect x="610" y="125" width="70" height="46" style="stroke:var(--accent)" stroke-width="2"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="11.5">
    <text x="155" y="118" font-size="10.5">Block 0</text><text x="225" y="118" font-size="10.5">1</text><text x="295" y="118" font-size="10.5">2</text><text x="365" y="118" font-size="10.5">3</text>
    <text x="435" y="118" font-size="10.5">4</text><text x="505" y="118" font-size="10.5">5</text><text x="575" y="118" font-size="10.5">6</text><text x="645" y="118" font-size="10.5">7</text>
    <text x="155" y="145">Proc 0</text><text x="155" y="161">[VPN 1]</text>
    <text x="225" y="145">Proc 0</text><text x="225" y="161">[VPN 2]</text>
    <text x="295" y="152">[Free]</text>
    <text x="365" y="145">Proc 1</text><text x="365" y="161">[VPN 0]</text>
    <text x="435" y="145">Proc 1</text><text x="435" y="161">[VPN 1]</text>
    <text x="505" y="145">Proc 3</text><text x="505" y="161">[VPN 0]</text>
    <text x="575" y="145">Proc 2</text><text x="575" y="161">[VPN 1]</text>
    <text x="645" y="145">Proc 3</text><text x="645" y="161">[VPN 1]</text>
  </g>
  <text x="20" y="200" fill="currentColor" font-size="12">Proc 0·1·2 는 일부 페이지만 메모리에 있고 나머지는 스왑에. Proc 3 은 전부 스왑 아웃 → 지금 실행 중일 수 없다.</text>
</svg>
```

스왑 공간만이 디스크 쪽 페이지 저장소는 아니다. 실행 파일(`ls` 등)의 **코드 페이지**는 원래 파일 시스템에 있으므로, 메모리가 부족하면 그 프레임을 그냥 **버리고** 나중에 실행 파일에서 다시 읽으면 된다 (쓰기가 필요 없음 — 리눅스의 "file-backed clean page").

## 4. present 비트 (21.2)

HW 관리 TLB를 가정하고 메모리 참조를 다시 따라가 보자.

1. VPN 추출 → TLB hit이면 끝 (흔한 경우, 빠름).
2. TLB miss → PTBR로 페이지 테이블에서 PTE를 찾는다.
3. PTE가 valid이고 **present**면 PFN을 TLB에 넣고 재시도.
4. **present = 0** 이면? 페이지는 디스크에 있다 → **페이지 폴트**.

> **ASIDE — 용어 정리**: "페이지 폴트"는 넓게는 페이지 테이블 관련 모든 폴트(불법 접근 포함)를 가리키기도 한다. 여기서 말하는 건 **합법적인** 접근인데 페이지가 메모리에 없을 뿐인 경우 — 정확히는 "page miss"라고 불러야 맞다. 그래도 "fault"라 부르는 건 처리 메커니즘(하드웨어가 모르는 일이 생기면 예외를 올리고 OS에 맡김)이 불법 접근과 똑같기 때문이다.

## 5. 페이지 폴트 처리 (21.3)

HW 관리 TLB든 SW 관리 TLB든, **페이지 폴트는 거의 모든 시스템에서 OS(소프트웨어)가 처리**한다.

스왑된 페이지는 디스크 어디에 있나? **PTE의 PFN 자리에 디스크 주소를 적어 둔다** — present=0이면 하드웨어는 나머지 비트를 해석하지 않으므로 OS가 자유롭게 쓸 수 있다. (리눅스 x86-64: P=0인 PTE에 "swap type + swap offset"을 인코딩.)

처리 순서:

1. 디스크 주소를 PTE에서 읽어 **I/O 요청**. 그동안 프로세스는 **blocked** 상태 → 다른 ready 프로세스가 CPU를 쓴다 (I/O와 계산의 겹침, 멀티프로그래밍의 이점).
2. I/O 완료 → PTE를 **present=1, PFN=새 프레임**으로 갱신.
3. 명령어 **재시도** → (TLB miss → PTE에서 찾아 TLB 채움) → 재시도 → TLB hit → 접근 성공. (폴트 처리 중 TLB를 바로 채우면 한 단계 생략 가능.)

> **ASIDE — 왜 하드웨어는 페이지 폴트를 처리하지 않나?**
> (1) **디스크가 워낙 느려서** OS가 명령어 수천 개를 더 실행해도 상대적으로 무시할 수준이다. (2) 처리하려면 하드웨어가 **스왑 공간 구조, 디스크 I/O 방법** 등 몰라도 되는 것을 다 알아야 한다. 성능과 단순성 모두를 위해 OS에 맡긴다.

## 6. 메모리가 꽉 차면? (21.4)

빈 프레임이 없으면 먼저 하나 이상을 **쫓아내야(page out)** 한다. 무엇을 쫓아낼지 고르는 것이 **페이지 교체 정책**이다. 잘못 고르면 프로그램이 메모리 속도가 아니라 디스크 속도로 돈다 — **1만~10만 배** 느림. 그래서 다음 장([Ch.22](2026-09-30_C22_swapping_policies.md)) 전체가 이 정책 이야기다.

### 6.1 새 예제 — 폴트율이 성능을 얼마나 망치나

메모리 접근 100ns, 페이지 폴트 처리 시간 HDD 10ms / NVMe SSD 100µs 라 하자. 폴트율 p:

```text
EAT = (1 - p) × 100ns + p × T_fault

HDD (T = 10ms = 10,000,000ns):
  p = 1/1,000      → EAT ≈ 100 + 10,000 = 10,100ns   (100배 느림!)
  10% 이내로 느려지려면: p × 10^7 < 10  →  p < 10^-6  (100만 번에 1번 미만)

NVMe SSD (T = 100µs = 100,000ns):
  p = 1/1,000      → EAT ≈ 100 + 100 = 200ns       (2배 느림)
  10% 이내: p < 10^-4
```

SSD가 스왑을 "쓸 만하게" 만들었지만, 여전히 폴트는 DRAM 접근의 1000배다.

## 7. 페이지 폴트 제어 흐름 (21.5)

"프로그램이 메모리에서 데이터를 읽을 때 무슨 일이 일어나나?" — 면접 단골 질문의 완전한 답이 이 두 그림이다.

**하드웨어 쪽** (Figure 21.2):

```text
 1  VPN = (VirtualAddress & VPN_MASK) >> SHIFT
 2  (Success, TlbEntry) = TLB_Lookup(VPN)
 3  if (Success == True)                          // TLB Hit
 4-9    권한 OK면 PA = (TlbEntry.PFN << SHIFT) | Offset; 접근,  아니면 PROTECTION_FAULT
10  else                                          // TLB Miss
11      PTEAddr = PTBR + (VPN * sizeof(PTE));  PTE = AccessMemory(PTEAddr)
13      if (PTE.Valid == False)            RaiseException(SEGMENTATION_FAULT)   // 경우 3: 버그
16      else if (!CanAccess(PTE.Prot))     RaiseException(PROTECTION_FAULT)
18      else if (PTE.Present == True)      TLB_Insert(...); RetryInstruction()  // 경우 1: 정상
22      else if (PTE.Present == False)     RaiseException(PAGE_FAULT)           // 경우 2: 스왑됨
```

**OS 쪽** (Figure 21.3):

```text
1  PFN = FindFreePhysicalPage()
2  if (PFN == -1)                 // 빈 프레임 없음
3      PFN = EvictPage()          // 교체 정책 실행 (더러우면 디스크에 써야 함)
4  DiskRead(PTE.DiskAddr, PFN)    // sleep (I/O 대기)
5  PTE.present = True             // 페이지 테이블 갱신
6  PTE.PFN     = PFN
7  RetryInstruction()
```

```svg
<svg viewBox="0 0 700 360" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C21-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <g fill="currentColor" text-anchor="middle" font-weight="600">
    <text x="90" y="22">프로세스</text><text x="250" y="22">MMU (HW)</text><text x="440" y="22">OS 폴트 핸들러</text><text x="620" y="22">디스크</text>
  </g>
  <g stroke="currentColor" stroke-dasharray="3 4" stroke-opacity="0.5">
    <line x1="90" y1="30" x2="90" y2="350"/><line x1="250" y1="30" x2="250" y2="350"/><line x1="440" y1="30" x2="440" y2="350"/><line x1="620" y1="30" x2="620" y2="350"/>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.4" marker-end="url(#C21-arrow)">
    <path d="M90,50 L246,50"/>
    <path d="M250,95 L436,95"/>
    <path d="M440,150 L616,150"/>
    <path d="M620,205 L444,205"/>
    <path d="M440,275 L94,275"/>
    <path d="M90,300 L246,300"/>
    <path d="M250,325 L94,325"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="100" y="44">① load VA</text>
    <text x="258" y="70" font-size="11.5">② TLB miss → PTE 읽기</text>
    <text x="258" y="86" font-size="11.5">valid=1, present=0</text>
    <text x="300" y="112" font-size="11.5">③ PAGE_FAULT 예외 (트랩)</text>
    <text x="448" y="122" font-size="11.5">④ 빈 프레임 찾기</text>
    <text x="448" y="137" font-size="11.5">없으면 EvictPage()</text>
    <text x="460" y="166" font-size="11.5">⑤ DiskRead(PTE.DiskAddr)</text>
    <text x="100" y="185" font-size="11.5">프로세스 BLOCKED</text>
    <text x="100" y="200" font-size="11.5">(다른 프로세스 실행)</text>
    <text x="470" y="222" font-size="11.5">⑥ I/O 완료 인터럽트</text>
    <text x="448" y="245" font-size="11.5">⑦ PTE.present=1, PTE.PFN=새 프레임</text>
    <text x="110" y="270" font-size="11.5">⑧ READY → 명령어 재시도</text>
    <text x="100" y="294" font-size="11.5">⑨ 같은 load 재실행</text>
    <text x="258" y="318" font-size="11.5">TLB miss→insert→retry→HIT</text>
    <text x="100" y="342" font-size="11.5">⑩ 데이터 도착 (총 수 ms 걸릴 수도)</text>
  </g>
  <rect x="610" y="140" width="20" height="65" style="fill:var(--accent-soft)" stroke="currentColor"/>
</svg>
```

TLB 미스 후 세 가지 경우를 구분할 줄 알아야 한다:

1. **valid & present** → PFN을 TLB에 넣고 재시도 (그냥 TLB 미스).
2. **valid & not present** → 페이지 폴트 → OS가 디스크에서 가져옴 (합법적 접근).
3. **invalid** → 버그 (예: 널 포인터) → OS가 보통 프로세스 종료 (SIGSEGV). 다른 비트는 의미 없음.

## 8. 교체는 실제로 언제 일어나나 (21.6)

메모리가 **완전히** 찰 때까지 기다렸다가 하나씩 쫓아내는 건 비현실적이다. 대부분의 OS는:

- 빈 페이지 수가 **low watermark(LW)** 밑으로 떨어지면 백그라운드 스레드 — **스왑 데몬(swap daemon) / 페이지 데몬(page daemon)** — 을 깨운다.
- 데몬은 빈 페이지가 **high watermark(HW)** 에 도달할 때까지 쫓아내고 다시 잠든다.

```svg
<svg viewBox="0 0 700 260" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <line x1="60" y1="220" x2="680" y2="220" stroke="currentColor"/>
  <line x1="60" y1="20" x2="60" y2="220" stroke="currentColor"/>
  <text x="370" y="250" text-anchor="middle" fill="currentColor" font-size="12">시간 →</text>
  <text x="18" y="120" transform="rotate(-90 18 120)" text-anchor="middle" fill="currentColor" font-size="12">빈 페이지 수</text>
  <line x1="60" y1="60" x2="680" y2="60" style="stroke:var(--accent)" stroke-dasharray="6 4"/>
  <line x1="60" y1="160" x2="680" y2="160" stroke="#d9534f" stroke-dasharray="6 4"/>
  <text x="676" y="54" text-anchor="end" fill="currentColor" font-size="12">HW (high watermark)</text>
  <text x="676" y="176" text-anchor="end" fill="currentColor" font-size="12">LW (low watermark)</text>
  <rect x="200" y="20" width="70" height="200" style="fill:var(--accent-soft)" fill-opacity="0.7"/>
  <rect x="470" y="20" width="60" height="200" style="fill:var(--accent-soft)" fill-opacity="0.7"/>
  <polyline points="60,70 100,95 150,130 200,162 270,60 320,85 370,110 420,135 470,162 530,60 600,90 680,120" fill="none" stroke="currentColor" stroke-width="2"/>
  <g fill="currentColor" font-size="11.5" text-anchor="middle">
    <text x="235" y="36">데몬 깨어남</text><text x="235" y="200">LW→HW 까지</text><text x="235" y="214">모아서 축출</text>
    <text x="500" y="36">다시 깨어남</text>
    <text x="130" y="200">앱이 메모리 소비</text>
    <text x="370" y="200">데몬은 잠듦</text>
  </g>
</svg>
```

한 번에 여러 페이지를 처리하면 생기는 이점:

- **클러스터링(clustering)**: 여러 더러운 페이지를 모아 스왑 영역에 **한 번에 연속 쓰기** → HDD의 탐색·회전 비용 감소 (VAX/VMS [LL82]).
- 폴트 경로가 짧아진다: 폴트 핸들러는 "빈 페이지가 있나?"만 보고, 없으면 데몬에게 알리고 기다린다. 데몬이 페이지를 비우면 원래 스레드를 깨운다.

> **TIP — 일은 백그라운드에서 (DO WORK IN THE BACKGROUND)**
> 백그라운드 처리는 효율을 높이고 작업을 묶을 수 있게 한다. 예: 파일 쓰기를 메모리에 버퍼링했다가 나중에 디스크로. 이점: (1) 디스크가 많은 쓰기를 받아 스케줄링을 더 잘 함, (2) 앱 입장에선 쓰기가 빨리 끝남(지연 감소), (3) 파일이 지워지면 **아예 안 써도 됨**(작업 감소), (4) **유휴 시간 활용**.

"daemon"은 Multics/Project MAC에서 온 말로, 물리학의 **맥스웰의 도깨비(Maxwell's daemon)** — 쉬지 않고 백그라운드에서 분자를 분류하는 상상의 존재 — 에서 따왔다.

## 9. 요약 (21.7)

- present 비트 하나가 추가되고, 없으면 OS 폴트 핸들러가 디스크에서 가져온다 (필요하면 먼저 다른 페이지를 쫓아냄).
- 이 모든 일이 프로세스에게 **투명**하다. 프로세스는 자기만의 연속된 가상 메모리에 접근한다고 믿지만, 뒤에서 페이지는 물리 메모리 여기저기 흩어져 있고, 때로는 아예 디스크에 있다.
- 최악의 경우 **명령어 하나가 수 밀리초**가 걸릴 수 있다.

## 10. 직접 해보기

### 10.1 present 비트를 사용자 공간에서 관찰하기

`mmap` 으로 64MB를 예약하면 커널은 **PTE만 약속**하고 프레임은 주지 않는다 (present=0 상태와 같다). 처음 건드릴 때 페이지 폴트가 나서 프레임이 할당(zero-fill)된다. `mincore()` 로 "이 페이지가 지금 메모리에 있나?"를, `getrusage()` 의 `ru_minflt` 로 폴트 횟수를 본다.

```c
// C21_demand_paging.c — "present bit" 와 페이지 폴트를 사용자 공간에서 관찰하기
//  - mmap 으로 큰 영역을 예약만 하면 물리 프레임은 아직 없다 (present = 0)
//  - 처음 건드릴 때마다 페이지 폴트 -> OS 가 프레임을 할당(zero-fill) -> present = 1
//  - mincore() 로 "지금 메모리에 올라와 있나?" (= present 비트의 사용자 버전) 를 물어본다
//  - getrusage() 의 ru_minflt 로 폴트 횟수를 센다
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/resource.h>

static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec / 1e3;
}

static long minflt(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_minflt;
}

static long resident(char *p, size_t len, long pagesize)
{
    size_t n = len / (size_t)pagesize;
    char vec[n];                                     // 페이지당 1바이트
    if (mincore(p, len, vec) != 0) { perror("mincore"); return -1; }
    long cnt = 0;
    for (size_t i = 0; i < n; i++) cnt += (vec[i] & MINCORE_INCORE) ? 1 : 0;
    return cnt;
}

int main(void)
{
    long pagesize = sysconf(_SC_PAGESIZE);
    size_t npages = 4096;                            // 4096 x 16KB = 64MB (Apple Silicon)
    size_t len = npages * (size_t)pagesize;
    printf("pagesize %ld B, region %zu pages = %zu MB\n", pagesize, npages, len >> 20);

    char *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); return 1; }
    printf("after mmap          : resident %5ld / %zu pages\n", resident(p, len, pagesize), npages);

    // 1) 절반만 첫 접근 (폴트 발생)
    long f0 = minflt(); double t0 = now_us();
    for (size_t i = 0; i < npages / 2; i++) p[i * (size_t)pagesize] = 1;
    double t1 = now_us(); long f1 = minflt();
    printf("touch 1st half      : resident %5ld, minor faults +%ld, %.0f us (%.2f us/page)\n",
           resident(p, len, pagesize), f1 - f0, t1 - t0, (t1 - t0) / (double)(npages / 2));

    // 2) 같은 절반을 다시 접근 (이미 present -> 폴트 없음)
    f0 = minflt(); t0 = now_us();
    for (size_t i = 0; i < npages / 2; i++) p[i * (size_t)pagesize] += 1;
    t1 = now_us(); f1 = minflt();
    printf("touch 1st half again: resident %5ld, minor faults +%ld, %.0f us (%.3f us/page)\n",
           resident(p, len, pagesize), f1 - f0, t1 - t0, (t1 - t0) / (double)(npages / 2));

    // 3) 읽기만 해도 폴트가 날까? (zero page 매핑 여부는 OS 마다 다름)
    f0 = minflt(); long sum = 0;
    for (size_t i = npages / 2; i < npages; i++) sum += p[i * (size_t)pagesize];
    f1 = minflt();
    printf("read 2nd half       : resident %5ld, minor faults +%ld (sum=%ld)\n",
           resident(p, len, pagesize), f1 - f0, sum);

    // 4) 프레임 반납: munmap 대신 madvise(MADV_FREE) — 커널에 "버려도 됨" 힌트
    madvise(p, len, MADV_FREE);
    printf("after MADV_FREE     : resident %5ld (lazy: kernel reclaims only under pressure)\n",
           resident(p, len, pagesize));

    // 5) PROT_NONE 영역 접근 = valid 하지만 권한 없음 -> SIGBUS/SIGSEGV (여기서는 시도하지 않음)
    munmap(p, len);
    printf("munmap done\n");
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 -pthread code/C21_demand_paging.c -o .work/bin/C21_demand_paging && .work/bin/C21_demand_paging
pagesize 16384 B, region 4096 pages = 64 MB
after mmap          : resident     0 / 4096 pages
touch 1st half      : resident  2048, minor faults +2048, 2044 us (1.00 us/page)
touch 1st half again: resident  2048, minor faults +0, 196 us (0.096 us/page)
read 2nd half       : resident  4096, minor faults +2048 (sum=0)
after MADV_FREE     : resident  4096 (lazy: kernel reclaims only under pressure)
munmap done
```

해석:

- `mmap` 직후 resident **0** — 64MB를 "할당"했는데 물리 메모리는 0 사용. 이것이 **demand paging(요구 페이징)**.
- 첫 절반(2048 페이지)을 쓰면 **minor fault 정확히 2048회**, 페이지당 **약 1µs**. 디스크 I/O가 없는 zero-fill 폴트(= minor fault)인데도 트랩 → 프레임 할당 → 16KB 0으로 채우기 → PTE 갱신 비용이 든다.
- 같은 페이지를 다시 쓰면 **폴트 0회**, 페이지당 0.1µs 수준(남은 비용은 TLB/캐시 미스). 폴트가 10배 비싸다.
- **읽기만 해도 폴트가 2048회** 나고 resident가 됐다. macOS는 읽기 폴트에도 실제 프레임을 붙인다 (리눅스는 읽기 폴트 시 공유 **zero page**를 read-only로 매핑해 프레임을 아끼고, 나중에 쓰면 COW 폴트를 한 번 더 낸다 — OS마다 다름).
- `MADV_FREE` 후에도 resident 4096 — 커널은 "필요하면 버려도 된다"는 힌트만 받고 메모리 압박이 올 때 회수한다 (8절의 "백그라운드에서 게으르게"와 같은 철학).
- 여기 폴트는 모두 **minor**(디스크 I/O 없음)다. 원문의 스왑 인은 **major fault** — 같은 경로에 DiskRead가 끼어 수십 µs~ms가 된다.

### 10.2 OSTEP `mem.c` + `vm_stat` (vmstat의 macOS 판)

```text
cd .tools/ostep-homework/vm-beyondphys
# Makefile 은 gcc 로 제자리 빌드 — 여기서는 소스를 건드리지 않으려고 .work/bin 으로 빌드
$ cc -o .work/bin/mem .tools/ostep-homework/vm-beyondphys/mem.c -Wall -O
$ .work/bin/mem 1024          # 1GB 배열을 계속 순회 (Ctrl-C 로 종료)
allocating 1073741824 bytes (1024.00 MB)
  number of integers in array: 268435456
loop 0 in 391.76 ms (bandwidth: 2613.85 MB/s)
loop 2 in 110.12 ms (bandwidth: 9299.04 MB/s)
loop 4 in 109.28 ms (bandwidth: 9370.68 MB/s)
loop 5 in 203.46 ms (bandwidth: 5032.93 MB/s)
loop 7 in 107.90 ms (bandwidth: 9490.28 MB/s)
loop 9 in 108.40 ms (bandwidth: 9446.32 MB/s)
loop 11 in 108.25 ms (bandwidth: 9459.76 MB/s)
loop 13 in 102.64 ms (bandwidth: 9976.81 MB/s)
loop 15 in 110.03 ms (bandwidth: 9306.96 MB/s)
loop 17 in 142.71 ms (bandwidth: 7175.49 MB/s)
loop 19 in 99.07 ms (bandwidth: 10335.72 MB/s)
loop 22 in 104.35 ms (bandwidth: 9813.23 MB/s)
loop 24 in 101.52 ms (bandwidth: 10086.89 MB/s)
loop 26 in 102.55 ms (bandwidth: 9985.09 MB/s)
loop 28 in 101.82 ms (bandwidth: 10057.18 MB/s)
loop 30 in 102.64 ms (bandwidth: 9976.72 MB/s)
^C
```

- **loop 0만 391ms (2.6GB/s)**, 이후 ~100ms (~10GB/s). 첫 루프는 1GB = **65,536개 16KB 페이지**를 처음 건드리며 전부 zero-fill 폴트를 낸다. 이후엔 폴트 없이 메모리 대역폭만큼.
- 가끔 느린 루프(loop 5: 203ms)는 다른 프로세스·코어 이동 노이즈.

같은 시간대에 다른 터미널에서 `vm_stat 1` (리눅스 `vmstat 1` 대응, 단위는 **16KB 페이지**). 아래는 별도 실행(mem 1024를 약 4초 돌리고 종료)에서 1초 간격으로 찍은 실제 출력의 **열 일부 발췌** (첫 줄은 부팅 후 누적값):

```text
$ vm_stat 1
free   active   inactive  wired   faults   0fill    cmprssed  cmprssor  dcomprs   comprs    pageins   pageout  swapins  swapouts
18866  425229   415657    190545  821889K  446378K  1010369   461916    21608315  27345498  11908208  191175   1955     2176
16895  438968   421427    172939  11521    4740     1010123   461851    221       0         67        0        0        0
4648   422396   420428    194082  91590    74713    1040105   479713    10974     41452     548       254      0        0
4363   439230   435722    161400  31859    6941     1038214   478361    6393      4694      331       30       0        0
4041   439234   432720    161417  33591    10311    1040427   479194    277       2523      872       4        0        0
6547   437389   431007    161384  13574    3857     1042269   478128    2963      4884      875       1        0        0
64599  397313   386627    185511  12565    6481     1039362   477836    2893      0         716       0        0        0
```

- 3번째 줄: **0fill 74,713** — 1초 동안 zero-fill 폴트로 약 1.1GB(≈ mem의 65,536 페이지 + 다른 프로세스) 할당.
- 같은 순간 **free 16,895 → 4,648** 페이지로 급감하고, **comprs 41,452 / pageout 254** — 빈 페이지가 바닥나자 커널이 **다른 프로세스 페이지를 압축하고 일부를 내보냈다**. 이것이 macOS판 "LW 아래로 떨어지면 데몬이 일한다" 장면이다 (macOS는 스왑 전에 먼저 **메모리 압축기(compressor)** 를 쓴다).
- 마지막 줄: mem 종료 → **free 64,599** 로 회복 (1GB 반납).
- swapins/swapouts는 이 구간 0 — 24GB 맥에서 1GB로는 디스크 스왑까지 가지 않았다. (`sysctl vm.swapusage` 당시: total 1024M, used 2M.) 원문 숙제처럼 **물리 메모리보다 큰** 크기로 돌리면 swapouts가 오르고 루프 대역폭이 디스크 수준으로 떨어지는 걸 볼 수 있지만, 시스템이 매우 느려지므로 여기선 실행하지 않았다.

## 11. 펌웨어 엔지니어의 눈으로

- **GPU/가속기 페이지 폴트 = 이 장의 디바이스 버전.** NVIDIA UVM(Unified Memory)에서 GPU가 present가 아닌 페이지를 건드리면 GPU MMU가 **replayable fault**를 폴트 버퍼에 기록하고, 드라이버가 페이지를 CPU→GPU로 옮긴 뒤 PTE를 갱신하고 **replay**(=RetryInstruction)를 지시한다. 폴트 처리 중 그 warp는 멈추고 다른 warp가 돈다 — "폴트 난 프로세스는 blocked, 다른 프로세스 실행"과 같은 구조.
- **IOMMU의 PRI(Page Request Interface) / Arm SMMU stall 모델**: PCIe 디바이스가 DMA 중 non-present 페이지를 만나면 **Page Request** 를 보내고 OS가 페이지를 올린 뒤 응답 → 디바이스 재시도. 대부분의 DMA는 이런 대기를 감당 못 해서 드라이버가 **pin(mlock/get_user_pages)** 해 두는 게 보통이다 — "DMA 버퍼는 스왑되면 안 된다"의 이유.
- **SSD 펌웨어 관점의 스왑**: 스왑 쓰기는 4KB~16KB **랜덤 쓰기**의 연속이라 WAF와 수명에 나쁘다. 8절의 **클러스터링**은 FTL 입장에서도 이득(순차 쓰기). 모바일 OS(iOS, Android)가 디스크 스왑 대신 **메모리 압축(zram, compressor)** 을 먼저 쓰는 이유 중 하나가 플래시 수명이다.
- **watermark ≈ FTL의 GC 임계값.** 빈 블록이 low threshold 밑이면 백그라운드 GC 시작, high threshold까지 확보하면 멈춘다. 호스트 쓰기 경로(=폴트 경로)에서 foreground GC가 일어나면 지연이 튀는 것도 "데몬이 못 따라가면 폴트 핸들러가 직접 축출해야 하는" 상황과 같다.
- **RTOS/베어메탈엔 스왑이 없다.** 결정적(deterministic) 지연이 필요하니까 — 명령어 하나가 수 ms 걸릴 수 있는 시스템은 하드 실시간에 못 쓴다. 리눅스 RT 앱도 `mlockall()` 로 페이지 폴트를 차단한다.

## 12. 면접 질문

### Q1. 프로그램이 `x = *p;` 를 실행할 때 일어날 수 있는 모든 경우를 설명하라.
<details>
<summary>답 보기</summary>

- **TLB hit** + 권한 OK → PA로 캐시/메모리 접근 (수 사이클~수십 ns).
- **TLB miss** → 페이지 테이블 워크(HW 또는 SW). PTE가 **valid & present** → TLB 채우고 재시도.
- **valid & not present** → **페이지 폴트** → OS: 빈 프레임 확보(없으면 교체, 더러우면 write-back) → 디스크 읽기(프로세스 blocked) → PTE 갱신 → 재시도. 수 ms까지.
- **invalid** 또는 **권한 위반** → SIGSEGV/SIGBUS (또는 COW 같은 의도된 보호 폴트면 OS가 복사 후 재시도).
- 그 위에 데이터 캐시 hit/miss 계층이 겹친다.

</details>

### Q2. minor fault와 major fault의 차이는? 예를 들어 보라.
<details>
<summary>답 보기</summary>

- **minor**: 디스크 I/O 없이 처리. 첫 접근 zero-fill(anonymous), 페이지 캐시에 이미 있는 파일 페이지 매핑, COW 복사, 다른 프로세스와 공유 중인 페이지.
- **major**: 디스크/SSD I/O 필요. 스왑 인, 페이지 캐시에 없는 mmap 파일 읽기.
- 비용: minor ~µs, major 수십 µs(NVMe)~ms(HDD). `getrusage` 의 `ru_minflt` / `ru_majflt`, 리눅스 `/proc/<pid>/stat`, `perf stat -e page-faults`.

</details>

### Q3. 스왑된 페이지의 디스크 위치는 어디에 저장하나? 왜 그게 가능한가?
<details>
<summary>답 보기</summary>

- **PTE 자체**(present=0일 때 PFN 필드 자리)에 스왑 장치 번호 + 오프셋을 인코딩.
- 가능한 이유: present=0이면 하드웨어는 **나머지 비트를 해석하지 않는다** → OS가 자유롭게 사용.
- 파일 매핑 페이지는 PTE를 비워 두고 VMA(파일 + 오프셋)로 위치를 안다.

</details>

### Q4. 왜 페이지 폴트는 하드웨어가 아니라 OS가 처리하나? 반대로 TLB 미스는 왜 HW가 처리하기도 하나?
<details>
<summary>답 보기</summary>

- 페이지 폴트: 디스크가 **너무 느려서** SW 오버헤드가 무시할 수준, 그리고 HW가 스왑 구조·I/O·교체 정책을 알 필요가 없게(**단순성**).
- TLB 미스: 매우 **잦고 짧은** 이벤트(수십 사이클)라 트랩 오버헤드가 비율상 크다 → HW page walker가 이득. 형식이 고정되는 대가를 감수.

</details>

### Q5. 스왑 데몬의 low/high watermark를 왜 두나? 둘 사이 간격이 너무 좁거나 넓으면?
<details>
<summary>답 보기</summary>

- 폴트 경로에서 축출을 기다리지 않게 **미리 빈 페이지를 확보**, 여러 페이지를 **모아서**(clustering) 효율적으로 쓰기, 유휴 시간 활용.
- 간격이 **좁으면** 데몬이 자주 깨고 매번 조금씩만 처리 → 오버헤드, 클러스터링 효과 감소.
- **넓으면** 한 번에 너무 많이 쫓아내 아직 쓸 페이지까지 내보냄 → 불필요한 재폴트, 메모리 낭비.
- 리눅스 kswapd는 min/low/high 3단계, min 아래에선 할당자가 **직접 회수(direct reclaim)** 해 지연이 튄다.

</details>

### Q6. GPU가 non-present 페이지를 건드리면 무슨 일이 일어나나? CPU 페이지 폴트와 다른 점은?
<details>
<summary>답 보기</summary>

- GPU MMU가 폴트를 **폴트 버퍼**에 기록하고 인터럽트, 해당 워프/채널은 정지. 드라이버(UVM)가 페이지를 이주/할당하고 GPU PTE를 갱신한 뒤 **replay** 명령.
- 차이: 폴트가 **배치로** 수백 개씩 오고, 처리 주체는 CPU 위의 드라이버, 페이지 이동은 **DMA(copy engine)**, 정밀한 명령어 재시작 대신 **replay** 단위. 비싸므로 실전에선 **prefetch/advise** 로 폴트를 피한다.
- 폴트 불가능한 엔진(옛 GPU, 많은 NPU)은 실행 전에 모든 버퍼를 **pin + 매핑**해야 한다.

</details>

## 13. 자가 점검 & 숙제

### 퀴즈 1. present=0 이고 valid=1 인 PTE에 접근하면 어떤 예외가 나고 결과는?
<details>
<summary>답 보기</summary>

PAGE_FAULT → OS가 스왑에서 읽어 들여 PTE를 present=1로 고친 뒤 명령어 재시도. 프로세스는 아무것도 모른다(투명).

</details>

### 퀴즈 2. 왜 페이지 폴트 처리 중 프로세스를 blocked로 만드나? 안 그러면?
<details>
<summary>답 보기</summary>

디스크 I/O는 ms 단위라, 그동안 CPU가 다른 ready 프로세스를 돌려야 활용률이 유지된다. busy-wait하면 수백만 사이클을 버린다.

</details>

### 퀴즈 3. 실행 파일의 코드 페이지를 쫓아낼 때 스왑 공간에 쓸 필요가 없는 이유는?
<details>
<summary>답 보기</summary>

코드 페이지는 수정되지 않았고(clean) 원본이 파일 시스템의 실행 파일에 있으므로, 프레임을 그냥 버리고 나중에 파일에서 다시 읽으면 된다.

</details>

### 퀴즈 4. 10.1의 출력에서 "같은 페이지 두 번째 접근"이 0.1µs/page 수준인데, 폴트도 없는데 왜 1ns가 아닌가?
<details>
<summary>답 보기</summary>

페이지마다 한 바이트만 건드리므로 매 접근이 새 캐시 라인(DRAM 접근)이고, 2048 페이지 = 32MB라 1단계 TLB 범위를 넘어 TLB 미스도 섞인다. 즉 남은 비용은 **캐시 미스 + TLB 미스**다 ([Ch.19](2026-09-30_C19_tlb.md) 측정과 연결).

</details>

### 원문 Homework 중 꼭 해볼 것 (v0.91 본문엔 문제 목록이 없고, `vm-beyondphys/README.md` 의 mem.c + vmstat 과제)

- **mem 크기 바꾸기**: 작은 크기(예: 512MB)와 물리 메모리에 근접한 크기로 돌리며 `vm_stat 1`(리눅스 `vmstat 1`)의 free, pageouts, swapouts가 어떻게 변하는지 관찰 — 메모리가 넘칠 때 **스왑이 시작되는 시점**을 확인하는 문제.
- **첫 루프 vs 이후 루프 대역폭**: loop 0이 느린 이유가 demand-zero 폴트임을 확인하는 문제 (10.2절에서 391ms vs 100ms).
- **스왑 중 성능**: 물리 메모리보다 큰 배열이면 대역폭이 몇 배 떨어지는지 측정 — 6.1절 EAT 계산과 대조 (리눅스 VM에서 안전하게 해볼 것).

## 14. 다음으로

- [Ch.22 스와핑 정책](2026-09-30_C22_swapping_policies.md): EvictPage() 안에서 **누구를** 쫓아낼 것인가 — OPT, FIFO, LRU, clock, thrashing.
- [Ch.23 VAX/VMS](2026-09-30_C23_vax_vms.md): 실제 VM 시스템이 이 메커니즘을 어떻게 조합했나 (clustering, 페이지 테이블 스왑).
- 복습: [Ch.18](2026-09-30_C18_paging_intro.md) present/dirty 비트, [Ch.19](2026-09-30_C19_tlb.md) TLB 미스 흐름.
