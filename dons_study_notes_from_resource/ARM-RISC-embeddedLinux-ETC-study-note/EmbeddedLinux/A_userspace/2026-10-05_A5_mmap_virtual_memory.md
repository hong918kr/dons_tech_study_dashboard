# A5. 메모리 맵핑과 가상 메모리 (유저 관점) — 주소는 "약속"이고, 페이지는 만질 때 생긴다

> **이 노트를 다 읽으면**: `/proc/self/maps`로 프로세스 주소 공간을 읽을 수 있다 · minor/major page fault를 측정하고 RT 루프에서 없앨 수 있다 · `mmap`의 SHARED/PRIVATE·파일/익명 조합을 골라 쓸 수 있다 · 카메라 버퍼와 MMIO가 어떻게 유저 공간에 zero-copy로 보이는지 드라이버 쪽까지 설명할 수 있다
> **왜 필요한가**: "malloc은 언제 실제 메모리를 쓰나?", "mmap vs read?", "카메라 프레임을 복사 없이 넘기려면?", "RT 스레드에서 page fault를 어떻게 막나?"는 Embedded Linux 면접 단골이고, 실무에선 메모리 누수·OOM·지터 디버깅의 출발점이다
> **Don 기준 난이도**: MMU·캐시 속성·DMA는 이미 안다 / 새로 배울 것은 "Linux가 페이지를 **언제** 붙이는가"(demand paging, COW, overcommit)와 그걸 보는 도구(`/proc/<pid>/maps`, `smaps`, `/proc/meminfo`)
> **선행 노트**: A1, A2, A3

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어에서 쓰던 Cortex-R/M에는 MMU가 없거나 꺼져 있었다. 코드가 `0x2000_0000`에 쓰면 그게 곧 SRAM의 `0x2000_0000`이었다. MPU는 "이 영역은 읽기 전용", "여기선 실행 금지" 같은 **권한 검사만** 했지 주소를 바꾸지 않았다.

Linux가 도는 Cortex-A에서는 MMU가 켜져 있고, 프로세스가 보는 모든 주소는 **가상 주소(virtual address, VA)** 다. MMU가 page table을 따라가 **물리 주소(physical address, PA)** 로 바꾼다. 그래서 두 프로세스가 똑같이 `0xaaaa_aaaa_0000`을 써도 서로 다른 RAM을 만진다. 반대로 커널이 마음먹으면 두 프로세스의 서로 다른 VA를 **같은 물리 페이지**에 연결할 수도 있다. 공유 메모리, 카메라 버퍼 zero-copy, `/dev/mem`이 전부 이 "연결"을 이용한 것이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="170" y="24" font-size="14" text-anchor="middle">bare-metal: 주소 = 물리 주소</text>
<text x="510" y="24" font-size="14" text-anchor="middle">Linux: 주소 = 가상 주소</text>
<line x1="340" y1="10" x2="340" y2="290" stroke="#888" stroke-dasharray="4 4"/>
<rect x="20" y="60" width="120" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="80" y="85" font-size="13" text-anchor="middle">CPU 코어</text>
<rect x="20" y="140" width="120" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="80" y="165" font-size="13" text-anchor="middle">MPU (권한만)</text>
<line x1="80" y1="100" x2="80" y2="138" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="180" y="50" width="145" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="252" y="72" font-size="13" text-anchor="middle">SRAM 0x2000_0000</text>
<rect x="180" y="110" width="145" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="252" y="132" font-size="13" text-anchor="middle">Flash 0x0800_0000</text>
<rect x="180" y="170" width="145" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="252" y="192" font-size="13" text-anchor="middle">Periph 0x4000_0000</text>
<line x1="140" y1="155" x2="178" y2="70" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="140" y1="160" x2="178" y2="127" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="140" y1="165" x2="178" y2="187" stroke="currentColor" marker-end="url(#ah1)"/><text x="170" y="245" font-size="13" text-anchor="middle">주소를 그대로 버스에 낸다</text><text x="170" y="265" font-size="13" text-anchor="middle">모든 태스크가 같은 주소 공간</text>
<rect x="355" y="50" width="130" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="420" y="73" font-size="13" text-anchor="middle">프로세스 A의 VA</text>
<rect x="355" y="100" width="130" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="420" y="123" font-size="13" text-anchor="middle">프로세스 B의 VA</text>
<rect x="355" y="160" width="130" height="50" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="420" y="181" font-size="13" text-anchor="middle">MMU + TLB</text><text x="420" y="200" font-size="12" text-anchor="middle">page table walk</text>
<line x1="420" y1="136" x2="420" y2="158" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="490" y1="68" x2="505" y2="68" stroke="currentColor"/>
<line x1="505" y1="68" x2="505" y2="150" stroke="currentColor"/>
<line x1="505" y1="150" x2="440" y2="158" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="530" y="140" width="135" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="597" y="162" font-size="13" text-anchor="middle">RAM 페이지</text>
<rect x="530" y="186" width="135" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="597" y="208" font-size="13" text-anchor="middle">DMA 버퍼</text>
<rect x="530" y="232" width="135" height="34" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="597" y="254" font-size="13" text-anchor="middle">MMIO 레지스터</text>
<line x1="485" y1="180" x2="528" y2="158" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="485" y1="190" x2="528" y2="203" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="485" y1="200" x2="528" y2="248" stroke="currentColor" marker-end="url(#ah1)"/><text x="420" y="245" font-size="13" text-anchor="middle">같은 VA라도</text><text x="420" y="265" font-size="13" text-anchor="middle">프로세스마다 다른 PA</text>
</svg>
```

그림 1 — bare-metal(왼쪽)은 주소가 곧 물리 주소이고 MPU는 권한만 본다. Linux(오른쪽)는 프로세스마다 page table이 따로 있고, MMU가 VA를 RAM·DMA 버퍼·MMIO 어디로든 연결한다.

Embedded Linux 엔지니어의 일에서 이 주제가 나오는 장면은 이렇다.

- **카메라 파이프라인**: 1080p 프레임 하나가 약 3 MB다. 30 fps를 `read()`로 복사하면 초당 90 MB를 CPU가 memcpy한다. V4L2는 드라이버가 할당한 DMA 버퍼를 `mmap`으로 유저에게 보여 줘서 복사를 없앤다.
- **RT 제어 루프**: 1 kHz 모터 제어 스레드가 처음 만지는 스택 페이지에서 page fault가 나면 수 µs가 튄다. `mlockall()`로 미리 다 붙여 둔다.
- **센서 데몬 간 공유**: IMU 데몬이 최신 샘플을 공유 메모리에 게시하고 여러 프로세스가 읽는다.
- **장비가 갑자기 죽음**: 로그에 `Out of memory: Killed process ...`. overcommit과 OOM killer를 모르면 원인을 못 찾는다.
- **FPGA/레지스터 직접 접근**: 드라이버 없이 UIO나 `/dev/mem`으로 레지스터를 `mmap`해서 bring-up 테스트를 한다.

이 노트의 실행 환경은 Lima VM `elinux`(aarch64, Ubuntu 26.04)다. 커널 버전과 페이지 크기부터 확인한다.

```sh
uname -r
getconf PAGESIZE
free -m
```

```text
7.0.0-34-generic
4096
               total        used        free      shared  buff/cache   available
Mem:            3895         533        1454           1        2079        3362
Swap:              0           0           0
```

출력에서 볼 것: 페이지 크기는 4096 바이트(4 KiB)다. arm64 커널은 4K/16K/64K 페이지 중 하나로 빌드할 수 있는데 Ubuntu 기본은 4K다. 이 VM은 RAM 약 4 GB에 **swap이 없다** — 뒤의 overcommit·OOM 이야기에서 중요하다.

---

## 1. 가상 주소와 물리 주소 — Don이 아는 MMU로 다시 보기

### 1.1 직관: "주소 번역표 + 권한표"를 합친 것

MPU를 설정할 때 Don은 "영역 시작·크기·권한·캐시 속성"을 레지스터에 썼다. MMU의 page table 엔트리도 똑같은 정보를 담는다. 다른 점은 두 가지다.

1. 단위가 **고정 크기 페이지**(4 KiB)이고 엔트리가 수십만 개다. 그래서 레지스터가 아니라 **메모리에 있는 표**(page table)로 둔다.
2. 엔트리에 **출력 주소(PA)** 가 들어 있다. 즉 "권한 검사"에 더해 "주소 바꾸기"를 한다.

**page(페이지)** 는 가상 주소 공간을 자르는 단위, **page frame(페이지 프레임)** 은 물리 RAM을 같은 크기로 자른 칸이다. page table은 "가상 페이지 번호 → 물리 프레임 번호 + 속성"의 표다.

### 1.2 arm64의 4단계 page table walk

4 KiB granule, 48비트 VA 기준으로 VA는 이렇게 잘린다. 각 단계 표는 512개 엔트리 × 8바이트 = 정확히 4 KiB(한 페이지)다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle">가상 주소 (48-bit VA, 4 KiB granule)</text>
<rect x="30" y="32" width="80" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="70" y="54" font-size="12" text-anchor="middle">63..48</text>
<rect x="110" y="32" width="100" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="160" y="54" font-size="12" text-anchor="middle">47..39 L0</text>
<rect x="210" y="32" width="100" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="260" y="54" font-size="12" text-anchor="middle">38..30 L1</text>
<rect x="310" y="32" width="100" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="360" y="54" font-size="12" text-anchor="middle">29..21 L2</text>
<rect x="410" y="32" width="100" height="34" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="460" y="54" font-size="12" text-anchor="middle">20..12 L3</text>
<rect x="510" y="32" width="120" height="34" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="570" y="54" font-size="12" text-anchor="middle">11..0 offset</text>
<rect x="20" y="140" width="90" height="70" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="65" y="170" font-size="12" text-anchor="middle">TTBR0_EL1</text><text x="65" y="188" font-size="12" text-anchor="middle">(프로세스별)</text>
<rect x="130" y="130" width="80" height="90" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170" y="170" font-size="13" text-anchor="middle">L0 표</text><text x="170" y="188" font-size="12" text-anchor="middle">512 x 8B</text>
<rect x="230" y="130" width="80" height="90" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="270" y="170" font-size="13" text-anchor="middle">L1 표</text><text x="270" y="188" font-size="12" text-anchor="middle">512 x 8B</text>
<rect x="330" y="130" width="80" height="90" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="370" y="170" font-size="13" text-anchor="middle">L2 표</text><text x="370" y="188" font-size="12" text-anchor="middle">512 x 8B</text>
<rect x="430" y="130" width="80" height="90" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="470" y="170" font-size="13" text-anchor="middle">L3 표</text><text x="470" y="188" font-size="12" text-anchor="middle">PTE</text>
<rect x="530" y="130" width="145" height="90" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="602" y="165" font-size="13" text-anchor="middle">물리 프레임</text><text x="602" y="185" font-size="12" text-anchor="middle">4 KiB</text><text x="602" y="205" font-size="12" text-anchor="middle">PA = 프레임+offset</text>
<line x1="110" y1="175" x2="128" y2="175" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="210" y1="175" x2="228" y2="175" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="310" y1="175" x2="328" y2="175" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="410" y1="175" x2="428" y2="175" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="510" y1="175" x2="528" y2="175" stroke="currentColor" marker-end="url(#ah2)"/>
<line x1="160" y1="66" x2="165" y2="128" stroke="#888" stroke-dasharray="3 3" marker-end="url(#ah2)"/>
<line x1="260" y1="66" x2="265" y2="128" stroke="#888" stroke-dasharray="3 3" marker-end="url(#ah2)"/>
<line x1="360" y1="66" x2="365" y2="128" stroke="#888" stroke-dasharray="3 3" marker-end="url(#ah2)"/>
<line x1="460" y1="66" x2="465" y2="128" stroke="#888" stroke-dasharray="3 3" marker-end="url(#ah2)"/>
<line x1="570" y1="66" x2="595" y2="128" stroke="#888" stroke-dasharray="3 3" marker-end="url(#ah2)"/><text x="340" y="250" font-size="13" text-anchor="middle">각 엔트리 = 다음 표(또는 프레임)의 PA + 권한(AP, UXN/PXN) + 메모리 속성(AttrIndx)</text><text x="340" y="272" font-size="13" text-anchor="middle">L2에서 멈추는 block 엔트리 = 2 MiB huge page (walk 1단계 절약)</text>
<rect x="120" y="295" width="440" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="340" y="315" font-size="13" text-anchor="middle">TLB: 최근 변환 결과 캐시 — 맞으면 walk 생략</text><text x="340" y="332" font-size="12" text-anchor="middle">ASID 태그 덕분에 context switch 때 전부 비우지 않아도 된다</text>
</svg>
```

그림 2 — VA의 비트 필드가 각 단계 표의 인덱스가 된다. 최악의 경우 메모리를 4번 읽어야 PA가 나오므로 TLB가 핵심이다.

정리하면:

- **TTBR0_EL1**은 유저 주소(아래쪽 절반)용 표의 시작 PA다. 프로세스를 바꿀 때 커널이 이 레지스터를 바꾼다. **TTBR1_EL1**은 커널 주소(위쪽 절반, `0xffff...`)용이고 모든 프로세스가 공유한다.
- 엔트리의 메모리 속성은 **MAIR_EL1**의 8개 슬롯 중 하나를 가리키는 인덱스(AttrIndx)다. Normal(캐시 됨), Normal-NC(캐시 안 됨, write-combine), Device-nGnRE, Device-nGnRnE 등이다. 12장에서 MMIO를 `mmap`할 때 다시 나온다.
- 엔트리가 "없음(invalid)"이면 MMU는 **translation fault**를 낸다. bare-metal에선 abort 핸들러로 가서 보통 리셋했다. Linux에선 이게 **정상 경로**다. 커널이 "아, 아직 안 붙인 페이지구나" 하고 페이지를 붙인 다음 그 명령을 다시 실행한다. 이게 3장의 page fault다.

### 1.3 흔한 함정

- "4 KiB 페이지인데 바이너리의 세그먼트는 64 KiB 간격"이 보일 수 있다(2장 출력의 `ex1` 매핑 주소 참고). arm64 링커는 16K/64K 페이지 커널에서도 돌도록 세그먼트를 64 KiB(max-page-size)로 정렬한다. 페이지 크기는 반드시 `sysconf(_SC_PAGESIZE)`로 물어본다. 하드코딩한 `4096`은 Apple Silicon(16K)이나 일부 서버(64K)에서 깨진다.
- 커널 공간 주소(`0xffff...`)를 유저가 읽으면 page table 엔트리가 있어도 권한(EL0 접근 불가) 때문에 SIGSEGV다.

---

## 2. 프로세스 주소 공간 레이아웃

### 2.1 직관

프로세스 하나의 가상 주소 공간은 "구역이 정해진 큰 빈 땅"이다. 아래쪽엔 프로그램 코드와 전역 변수, 그 위로 자라는 heap, 위쪽엔 아래로 자라는 stack, 그 사이 넓은 곳에 공유 라이브러리와 `mmap` 매핑이 놓인다. 각 구역 하나하나가 커널에서는 **VMA(virtual memory area, `struct vm_area_struct`)** 하나다. `/proc/<pid>/maps`의 한 줄이 VMA 하나다.

```svg
<svg viewBox="0 0 660 430" xmlns="http://www.w3.org/2000/svg">
<rect x="40" y="20" width="230" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="155" y="43" font-size="13" text-anchor="middle">커널 공간 (EL0 접근 불가)</text>
<rect x="40" y="66" width="230" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="155" y="91" font-size="13" text-anchor="middle">[stack] 아래로 자람</text>
<rect x="40" y="106" width="230" height="30" fill="none" stroke="#888" stroke-dasharray="4 4"/><text x="155" y="126" font-size="12" text-anchor="middle">빈 공간 (매핑 없음)</text>
<rect x="40" y="136" width="230" height="64" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="155" y="160" font-size="13" text-anchor="middle">mmap 영역 아래로 자람</text><text x="155" y="182" font-size="12" text-anchor="middle">libc · 큰 malloc · 익명 매핑</text>
<rect x="40" y="200" width="230" height="40" fill="none" stroke="#888" stroke-dasharray="4 4"/><text x="155" y="225" font-size="12" text-anchor="middle">빈 공간 (매우 넓음)</text>
<rect x="40" y="240" width="230" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="155" y="265" font-size="13" text-anchor="middle">[heap] 위로 자람 (brk)</text>
<rect x="40" y="290" width="230" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="155" y="310" font-size="13" text-anchor="middle">.bss (0으로 초기화된 전역)</text>
<rect x="40" y="320" width="230" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="155" y="340" font-size="13" text-anchor="middle">.data (초기값 있는 전역)</text>
<rect x="40" y="350" width="230" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="155" y="375" font-size="13" text-anchor="middle">.text + .rodata (코드·상수)</text><text x="290" y="43" font-size="12">0xffff_0000_0000_0000 이상 (TTBR1)</text><text x="290" y="91" font-size="12">&amp;local = 0xffffce6f0764</text><text x="290" y="160" font-size="12">libc r-xp = 0xe686786a0000</text>
<text x="290" y="182" font-size="12">malloc(1M) = 0xe6867859f010</text>
<text x="290" y="265" font-size="12">malloc(64) = 0xb72fa659e010</text>
<text x="290" y="310" font-size="12">&amp;g_zero = 0xb72fa1670018</text>
<text x="290" y="340" font-size="12">&amp;g_init = 0xb72fa1670010</text>
<text x="290" y="375" font-size="12">main = 0xb72fa1650a40</text>
<text x="40" y="415" font-size="12">위 = 높은 주소, 아래 = 낮은 주소. 오른쪽 값은 ex1 실행 1회 (ASLR로 매번 바뀜)</text>
</svg>
```

그림 3 — 64비트 arm64 프로세스의 주소 공간. 각 상자가 VMA이고 `/proc/self/maps`의 한 줄이다.

### 2.2 예제 1: 각 영역의 주소 찍고 maps와 맞춰 보기

전역·지역·heap·mmap 변수 주소를 찍은 뒤 자기 자신의 `/proc/self/maps`를 출력한다.

```c
// ex1: 프로세스 주소 공간 — 각 영역의 주소를 찍고 /proc/self/maps와 맞춰 본다
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

int g_init = 42;          // .data
int g_zero;               // .bss
const char g_ro[] = "ro"; // .rodata

static void show_maps(void)
{
    FILE *f = fopen("/proc/self/maps", "r");
    if (!f) { perror("fopen"); exit(1); }
    char line[512];
    while (fgets(line, sizeof line, f))
        fputs(line, stdout);
    fclose(f);
}

int main(void)
{
    int local = 0;                                   // stack
    void *small = malloc(64);                        // heap (brk)
    void *big = malloc(1 << 20);                     // 1 MiB -> mmap 영역
    void *anon = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (!small || !big || anon == MAP_FAILED) { perror("alloc"); exit(1); }

    printf("text   main      = %p\n", (void *)main);
    printf("rodata g_ro      = %p\n", (void *)g_ro);
    printf("data   g_init    = %p\n", (void *)&g_init);
    printf("bss    g_zero    = %p\n", (void *)&g_zero);
    printf("heap   malloc(64)= %p\n", small);
    printf("mmap   malloc(1M)= %p\n", big);
    printf("mmap   anon 4K   = %p\n", anon);
    printf("libc   printf    = %p\n", (void *)printf);
    printf("stack  &local    = %p\n", (void *)&local);
    printf("---- /proc/self/maps ----\n");
    show_maps();
    return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 ex1_layout.c -o /tmp/A5/ex1 && /tmp/A5/ex1
```

```text
text   main      = 0xb72fa1650a40
rodata g_ro      = 0xb72fa1650f40
data   g_init    = 0xb72fa1670010
bss    g_zero    = 0xb72fa1670018
heap   malloc(64)= 0xb72fa659e010
mmap   malloc(1M)= 0xe6867859f010
mmap   anon 4K   = 0xe686788a7000
libc   printf    = 0xe686786ef8a0
stack  &local    = 0xffffce6f0764
---- /proc/self/maps ----
b72fa1650000-b72fa1652000 r-xp 00000000 00:25 310                        /tmp/A5/ex1
b72fa166f000-b72fa1670000 r--p 0000f000 00:25 310                        /tmp/A5/ex1
b72fa1670000-b72fa1671000 rw-p 00010000 00:25 310                        /tmp/A5/ex1
b72fa659e000-b72fa6600000 rw-p 00000000 00:00 0                          [heap]
e6867859f000-e686786a0000 rw-p 00000000 00:00 0 
e686786a0000-e68678849000 r-xp 00000000 fd:01 5341                       /usr/lib/aarch64-linux-gnu/libc.so.6
e68678849000-e6867885d000 ---p 001a9000 fd:01 5341                       /usr/lib/aarch64-linux-gnu/libc.so.6
e6867885d000-e68678860000 r--p 001ad000 fd:01 5341                       /usr/lib/aarch64-linux-gnu/libc.so.6
e68678860000-e68678862000 rw-p 001b0000 fd:01 5341                       /usr/lib/aarch64-linux-gnu/libc.so.6
e68678862000-e6867886e000 rw-p 00000000 00:00 0 
e68678872000-e6867889c000 r-xp 00000000 fd:01 5338                       /usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1
e686788a0000-e686788a2000 rw-p 00000000 00:00 0 
e686788a7000-e686788aa000 rw-p 00000000 00:00 0 
e686788aa000-e686788ae000 r--p 00000000 00:00 0                          [vvar]
e686788ae000-e686788b0000 r-xp 00000000 00:00 0                          [vdso]
e686788b0000-e686788b2000 r--p 0002e000 fd:01 5338                       /usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1
e686788b2000-e686788b3000 rw-p 00030000 fd:01 5338                       /usr/lib/aarch64-linux-gnu/ld-linux-aarch64.so.1
e686788b3000-e686788b4000 rw-p 00000000 00:00 0 
ffffce6d0000-ffffce6f1000 rw-p 00000000 00:00 0                          [stack]
```

출력에서 볼 것 (주소는 실행마다 다르다):

- `main`(text)과 `g_ro`(rodata)는 `r-xp` 줄 안에, `g_init`(data)과 `g_zero`(bss)는 `rw-p` 줄 안에 있다. 가운데 `r--p` 줄은 RELRO(재배치 후 읽기 전용으로 바뀌는 GOT 등)다.
- `.bss`는 파일에 내용이 없지만 `.data`와 같은 페이지에 들어갔다. `.bss`가 크면 그 뒤에 이름 없는 익명 `rw-p` 매핑이 붙는다.
- `malloc(64)`는 `[heap]`에, `malloc(1M)`는 이름 없는 `rw-p` 매핑(`e6867859f000-e686786a0000`, 정확히 1 MiB + 4 KiB)에 있다. 5장에서 이유를 본다.
- libc는 한 파일이 권한별로 4개 VMA로 쪼개져 있다. `---p`는 정렬용 구멍(guard)이다.
- `[vdso]`는 커널이 매핑해 준 작은 코드 페이지다. `clock_gettime()`이 syscall 없이 도는 이유다(A1 참고).

### 2.3 maps 한 줄 읽는 법

| 필드 | 예 | 뜻 |
|---|---|---|
| 주소 범위 | `b72fa1670000-b72fa1671000` | VMA의 시작(포함)과 끝(미포함). 항상 페이지 정렬 |
| 권한 | `rw-p` | r/w/x + `p`(private, COW) 또는 `s`(shared) |
| offset | `00010000` | 파일 매핑일 때 파일 안 시작 위치 |
| dev | `fd:01` | 파일이 있는 장치 major:minor (`00:00` = 익명) |
| inode | `5341` | 파일 inode 번호 (`0` = 익명) |
| 경로 | `/usr/lib/.../libc.so.6` | 파일 경로 또는 `[heap]` `[stack]` `[vdso]` 같은 특수 이름 |

### 2.4 ASLR — 실행마다 주소가 바뀐다

**ASLR(Address Space Layout Randomization)** 은 공격자가 코드·스택 주소를 미리 알 수 없게 매 실행마다 시작 주소를 무작위로 바꾸는 기능이다. `setarch -R`로 끌 수 있다(디버깅 재현용).

```sh
cat /proc/sys/kernel/randomize_va_space
./ex1 | head -3
setarch -R ./ex1 | head -3
setarch -R ./ex1 | head -3
```

```text
2
text   main      = 0xae8fd9960a40
rodata g_ro      = 0xae8fd9960f40
data   g_init    = 0xae8fd9980010
text   main      = 0xaaaaaaaa0a40
rodata g_ro      = 0xaaaaaaaa0f40
data   g_init    = 0xaaaaaaac0010
text   main      = 0xaaaaaaaa0a40
rodata g_ro      = 0xaaaaaaaa0f40
data   g_init    = 0xaaaaaaac0010
```

출력에서 볼 것: `randomize_va_space=2`(stack·mmap·heap 전부 무작위)라서 첫 실행은 `0xae8f...`였지만, `setarch -R`로는 두 번 다 `0xaaaaaaaa0a40`으로 같다. gdb도 기본으로 ASLR을 끄고 실행한다. "gdb 안에선 재현되는데 밖에선 안 된다"의 흔한 원인이다.

---

## 3. Demand paging과 page fault

### 3.1 직관: 땅 문서만 먼저 주고, 집은 들어갈 때 짓는다

`mmap()`이나 `malloc()`이 돌려주는 것은 **주소 범위에 대한 약속**(VMA)이다. 이 시점엔 page table 엔트리도, 물리 페이지도 없다. 프로그램이 그 주소를 처음 만지는 순간 MMU가 translation fault를 내고, 커널의 page fault handler가

1. 그 주소가 어느 VMA에 속하는지 찾고 (없으면 SIGSEGV),
2. 권한이 맞는지 보고 (안 맞으면 SIGSEGV),
3. 물리 페이지를 하나 할당해 0으로 채우거나(익명) 파일 내용을 가져와서(파일),
4. PTE를 쓰고 돌아가서 그 명령을 다시 실행한다.

이걸 **demand paging**이라 한다. 두 종류의 fault가 있다.

| 종류 | 언제 | 비용 (대략) | 예 |
|---|---|---|---|
| **minor fault** | 필요한 데이터가 이미 RAM에 있음. PTE만 채우면 됨 | 수백 ns ~ 수 µs | 처음 쓰는 익명 페이지(0 페이지 할당), page cache에 있는 파일 페이지 |
| **major fault** | 데이터를 디스크(또는 swap)에서 읽어 와야 함 | 수십 µs ~ ms (저장장치에 따라) | 캐시에 없는 파일을 mmap해서 처음 읽음 |

Don의 SSD 펌웨어 경험으로 말하면, major fault 하나 = 호스트가 NVMe read 커맨드 하나를 기다리는 것이다.

### 3.2 예제 2: 만진 페이지 수 vs fault 수 vs 시간

익명 매핑을 만들고 페이지마다 1바이트씩 쓴다. `getrusage()`의 `ru_minflt`로 fault 수를, `clock_gettime()`으로 시간을 잰다. 인자로 세 모드를 비교한다: `plain`(그냥), `populate`(`MAP_POPULATE`로 mmap 안에서 미리 다 붙이기), `thp`(`madvise(MADV_HUGEPAGE)`로 2 MiB 페이지 요청).

```c
// ex2: demand paging — 페이지를 처음 만질 때마다 minor fault가 1번씩 난다
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>

static long minflt(void)
{
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) < 0) { perror("getrusage"); exit(1); }
    return ru.ru_minflt;
}

static double now_us(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e6 + t.tv_nsec / 1e3;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "plain";   // plain / populate / thp
    long psz = sysconf(_SC_PAGESIZE);
    printf("mode=%s page=%ld\n%8s %10s %10s %10s\n", mode, psz,
           "pages", "minflt", "total_us", "ns/page");
    for (long n = 1; n <= 65536; n *= 4) {
        size_t len = n * psz;
        int flags = MAP_PRIVATE | MAP_ANONYMOUS;
        if (strcmp(mode, "populate") == 0) flags |= MAP_POPULATE;
        long f0 = minflt();
        double t0 = now_us();                 // mmap 호출 비용까지 포함해서 잰다
        char *p = mmap(NULL, len, PROT_READ | PROT_WRITE, flags, -1, 0);
        if (p == MAP_FAILED) { perror("mmap"); exit(1); }
        if (strcmp(mode, "thp") == 0 && madvise(p, len, MADV_HUGEPAGE) < 0) {
            perror("madvise"); exit(1);
        }
        for (size_t off = 0; off < len; off += psz)
            p[off] = 1;                       // 페이지당 1바이트만 쓴다
        double t1 = now_us();
        long f1 = minflt();
        printf("%8ld %10ld %10.1f %10.1f\n", n, f1 - f0, t1 - t0,
               (t1 - t0) * 1000 / n);
        munmap(p, len);
    }
    return 0;
}
```

```sh
./ex2 && ./ex2 populate && ./ex2 thp
```

```text
mode=plain page=4096
   pages     minflt   total_us    ns/page
       1          3        1.8     1792.0
       4          4        2.4      604.2
      16         16        8.4      523.4
      64         64       39.1      610.7
     256        256      146.2      571.1
    1024       1024      538.7      526.1
    4096       4096     2014.2      491.8
   16384      16384     8703.1      531.2
   65536      65536    52545.8      801.8
mode=populate page=4096
   pages     minflt   total_us    ns/page
       1          3        3.3     3333.0
       4          4        2.1      531.2
      16         16        6.8      424.5
      64         64       22.5      351.6
     256        256       96.2      376.0
    1024       1024      325.2      317.5
    4096       4096     1300.8      317.6
   16384      16384     4812.7      293.7
   65536      65536    23262.0      355.0
mode=thp page=4096
   pages     minflt   total_us    ns/page
       1          3        4.1     4084.0
       4          4        3.0      760.5
      16         16        9.7      606.8
      64         64       30.8      480.5
     256        256      117.4      458.5
    1024          2       55.5       54.2
    4096          8      185.6       45.3
   16384         32      894.5       54.6
   65536        128    11464.1      174.9
```

출력에서 볼 것:

- `plain`: **만진 페이지 수 = minor fault 수**가 정확히 1:1이다. 페이지당 약 0.5 µs. 256 MiB(65536 페이지)를 처음 만지는 데 52 ms가 들었다. 1 kHz 제어 루프의 주기가 1 ms라는 걸 생각하면 엄청난 시간이다.
- `populate`: fault 수는 같지만(커널이 mmap 안에서 대신 fault를 일으킨다) 페이지당 약 0.3 µs로 싸다. 예외 진입/복귀 비용이 없어서다. 대신 **mmap 호출 자체가 그만큼 오래 걸린다** — 비용을 "시작 시점"으로 옮긴 것이다.
- `thp`: 1024 페이지(4 MiB)부터 fault가 512분의 1로 줄었다(1024 → 2). 2 MiB짜리 huge page 하나가 fault 한 번에 붙는다. 페이지당 약 50 ns. 64K 페이지 행에서 다시 오른 건 2 MiB 연속 물리 메모리를 구하느라(compaction 등) 시간이 든 것으로 보인다.
- 1페이지 행은 mmap syscall 비용이 지배해서 ns/page가 의미 없다.

```svg
<svg viewBox="0 0 640 330" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="270" x2="490" y2="270" stroke="currentColor"/>
<line x1="70" y1="30" x2="70" y2="270" stroke="currentColor"/>
<line x1="66" y1="270.0" x2="70" y2="270.0" stroke="currentColor"/>
<line x1="70" y1="270.0" x2="490" y2="270.0" stroke="#888" stroke-opacity="0.3"/><text x="62" y="274.0" font-size="12" text-anchor="end">0</text>
<line x1="66" y1="230.0" x2="70" y2="230.0" stroke="currentColor"/>
<line x1="70" y1="230.0" x2="490" y2="230.0" stroke="#888" stroke-opacity="0.3"/><text x="62" y="234.0" font-size="12" text-anchor="end">150</text>
<line x1="66" y1="190.0" x2="70" y2="190.0" stroke="currentColor"/>
<line x1="70" y1="190.0" x2="490" y2="190.0" stroke="#888" stroke-opacity="0.3"/><text x="62" y="194.0" font-size="12" text-anchor="end">300</text>
<line x1="66" y1="150.0" x2="70" y2="150.0" stroke="currentColor"/>
<line x1="70" y1="150.0" x2="490" y2="150.0" stroke="#888" stroke-opacity="0.3"/><text x="62" y="154.0" font-size="12" text-anchor="end">450</text>
<line x1="66" y1="110.0" x2="70" y2="110.0" stroke="currentColor"/>
<line x1="70" y1="110.0" x2="490" y2="110.0" stroke="#888" stroke-opacity="0.3"/><text x="62" y="114.0" font-size="12" text-anchor="end">600</text>
<line x1="66" y1="70.0" x2="70" y2="70.0" stroke="currentColor"/>
<line x1="70" y1="70.0" x2="490" y2="70.0" stroke="#888" stroke-opacity="0.3"/><text x="62" y="74.0" font-size="12" text-anchor="end">750</text>
<line x1="66" y1="30.0" x2="70" y2="30.0" stroke="currentColor"/>
<line x1="70" y1="30.0" x2="490" y2="30.0" stroke="#888" stroke-opacity="0.3"/><text x="62" y="34.0" font-size="12" text-anchor="end">900</text>
<line x1="70.0" y1="270" x2="70.0" y2="274" stroke="currentColor"/><text x="70.0" y="288" font-size="12" text-anchor="middle">4</text>
<line x1="130.0" y1="270" x2="130.0" y2="274" stroke="currentColor"/><text x="130.0" y="288" font-size="12" text-anchor="middle">16</text>
<line x1="190.0" y1="270" x2="190.0" y2="274" stroke="currentColor"/><text x="190.0" y="288" font-size="12" text-anchor="middle">64</text>
<line x1="250.0" y1="270" x2="250.0" y2="274" stroke="currentColor"/><text x="250.0" y="288" font-size="12" text-anchor="middle">256</text>
<line x1="310.0" y1="270" x2="310.0" y2="274" stroke="currentColor"/><text x="310.0" y="288" font-size="12" text-anchor="middle">1K</text>
<line x1="370.0" y1="270" x2="370.0" y2="274" stroke="currentColor"/><text x="370.0" y="288" font-size="12" text-anchor="middle">4K</text>
<line x1="430.0" y1="270" x2="430.0" y2="274" stroke="currentColor"/><text x="430.0" y="288" font-size="12" text-anchor="middle">16K</text>
<line x1="490.0" y1="270" x2="490.0" y2="274" stroke="currentColor"/><text x="490.0" y="288" font-size="12" text-anchor="middle">64K</text><text x="280" y="318" font-size="13" text-anchor="middle">만진 페이지 수 (4 KiB 단위, 로그 축)</text><text x="18" y="150" font-size="13" text-anchor="middle" transform="rotate(-90 18 150)">ns / page</text>
<polyline points="70.0,108.9 130.0,130.4 190.0,107.1 250.0,117.7 310.0,129.7 370.0,138.9 430.0,128.3 490.0,56.2" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="70.0" cy="108.9" r="3" fill="#4a7bd0"/><circle cx="130.0" cy="130.4" r="3" fill="#4a7bd0"/><circle cx="190.0" cy="107.1" r="3" fill="#4a7bd0"/><circle cx="250.0" cy="117.7" r="3" fill="#4a7bd0"/><circle cx="310.0" cy="129.7" r="3" fill="#4a7bd0"/>
<circle cx="370.0" cy="138.9" r="3" fill="#4a7bd0"/><circle cx="430.0" cy="128.3" r="3" fill="#4a7bd0"/><circle cx="490.0" cy="56.2" r="3" fill="#4a7bd0"/>
<polyline points="70.0,128.3 130.0,156.8 190.0,176.2 250.0,169.7 310.0,185.3 370.0,185.3 430.0,191.7 490.0,175.3" fill="none" stroke="#e08a3c" stroke-width="2"/><circle cx="70.0" cy="128.3" r="3" fill="#e08a3c"/><circle cx="130.0" cy="156.8" r="3" fill="#e08a3c"/><circle cx="190.0" cy="176.2" r="3" fill="#e08a3c"/><circle cx="250.0" cy="169.7" r="3" fill="#e08a3c"/><circle cx="310.0" cy="185.3" r="3" fill="#e08a3c"/>
<circle cx="370.0" cy="185.3" r="3" fill="#e08a3c"/><circle cx="430.0" cy="191.7" r="3" fill="#e08a3c"/><circle cx="490.0" cy="175.3" r="3" fill="#e08a3c"/>
<polyline points="70.0,67.2 130.0,108.2 190.0,141.9 250.0,147.7 310.0,255.5 370.0,257.9 430.0,255.4 490.0,223.4" fill="none" stroke="#3f9a6b" stroke-width="2"/><circle cx="70.0" cy="67.2" r="3" fill="#3f9a6b"/><circle cx="130.0" cy="108.2" r="3" fill="#3f9a6b"/><circle cx="190.0" cy="141.9" r="3" fill="#3f9a6b"/><circle cx="250.0" cy="147.7" r="3" fill="#3f9a6b"/><circle cx="310.0" cy="255.5" r="3" fill="#3f9a6b"/>
<circle cx="370.0" cy="257.9" r="3" fill="#3f9a6b"/><circle cx="430.0" cy="255.4" r="3" fill="#3f9a6b"/><circle cx="490.0" cy="223.4" r="3" fill="#3f9a6b"/>
<line x1="502" y1="40" x2="520" y2="40" stroke="#4a7bd0" stroke-width="3"/><text x="524" y="44" font-size="12">plain (4K fault)</text>
<line x1="502" y1="62" x2="520" y2="62" stroke="#e08a3c" stroke-width="3"/><text x="524" y="66" font-size="12">MAP_POPULATE</text>
<line x1="502" y1="84" x2="520" y2="84" stroke="#3f9a6b" stroke-width="3"/><text x="524" y="88" font-size="12">MADV_HUGEPAGE</text>
</svg>
```

그림 4 — 예제 2의 측정값(페이지 수 4 이상). 4 KiB fault는 페이지당 약 0.5 µs로 일정하다. huge page가 적용되는 4 MiB 이상에서 THP가 한 자릿수 빠르다.

### 3.3 예제 3: major fault — page cache에 없을 때

디스크에 있는 64 MiB 파일을 `mmap`해서 페이지마다 1바이트씩 읽는다. 이번엔 `/proc/self/stat`의 10번째(minflt)·12번째(majflt) 필드로 센다. 캐시를 비운 상태(cold)와 채운 상태(warm), 그리고 `MADV_RANDOM`으로 readahead를 끈 경우를 비교한다.

```c
// ex3: file-backed mmap — page cache에 없으면 major fault, 있으면 minor fault
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

// /proc/self/stat 의 10번째(minflt), 12번째(majflt) 필드
static void faults(long *mn, long *mj)
{
    FILE *f = fopen("/proc/self/stat", "r");
    if (!f) { perror("fopen"); exit(1); }
    if (fscanf(f, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %ld %*u %ld",
               mn, mj) != 2) { fprintf(stderr, "parse\n"); exit(1); }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: %s file [random]\n", argv[0]); exit(1); }
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { perror("open"); exit(1); }
    struct stat st;
    if (fstat(fd, &st) < 0) { perror("fstat"); exit(1); }
    const volatile char *p = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(1); }
    close(fd);                         // 매핑은 fd를 닫아도 살아 있다
    if (argc > 2 && madvise((void *)p, st.st_size, MADV_RANDOM) < 0) {
        perror("madvise"); exit(1);    // readahead 끄기
    }

    long mn0, mj0, mn1, mj1;
    unsigned long sum = 0;
    faults(&mn0, &mj0);
    for (off_t off = 0; off < st.st_size; off += 4096)
        sum += p[off];
    faults(&mn1, &mj1);
    printf("size=%lld MiB  minflt=%ld  majflt=%ld  (sum=%lu)\n",
           (long long)st.st_size >> 20, mn1 - mn0, mj1 - mj0, sum);
    return 0;
}
```

```sh
dd if=/dev/urandom of=/var/tmp/A5/data.bin bs=1M count=64 status=none && sync
for m in "" random; do
  echo "== cold $m"; sudo sh -c "echo 3 > /proc/sys/vm/drop_caches"
  /usr/bin/time -f "%e s elapsed" ./ex3 /var/tmp/A5/data.bin $m
  echo "== warm $m"; /usr/bin/time -f "%e s elapsed" ./ex3 /var/tmp/A5/data.bin $m
done
sudo cat /sys/kernel/debug/fault_around_bytes
```

```text
== cold 
size=64 MiB  minflt=436  majflt=1  (sum=2088852)
0.01 s elapsed
== warm 
size=64 MiB  minflt=437  majflt=0  (sum=2088852)
0.00 s elapsed
== cold random
size=64 MiB  minflt=1  majflt=16384  (sum=2088852)
0.64 s elapsed
== warm random
size=64 MiB  minflt=1025  majflt=0  (sum=2088852)
0.00 s elapsed
65536
```

출력에서 볼 것:

- 64 MiB = 16384 페이지인데 기본 모드에선 fault가 437번뿐이다. 두 가지 최적화 때문이다. **readahead**: 순차 접근을 감지하면 커널이 앞으로 읽을 부분을 미리 디스크에서 page cache로 올린다. 그래서 cold인데도 major fault는 첫 1번뿐이다. **fault-around**: 파일 페이지 fault 한 번에 주변 64 KiB(`fault_around_bytes=65536`)의, 이미 캐시에 있는 페이지들의 PTE를 한꺼번에 채운다. 최근 커널은 page cache가 큰 folio(여러 페이지 묶음)를 쓰기 때문에 한 번에 더 많이 매핑되기도 한다.
- `MADV_RANDOM`은 readahead와 fault-around를 끈다. cold에서 **16384번 전부 major fault**, 0.64초. 페이지당 약 39 µs — 디스크(여기선 Mac SSD 위 virtio 디스크) 왕복 비용이다. warm에서는 major 0, minor 1025다.
- `close(fd)` 후에도 매핑은 살아 있다. VMA가 `struct file`의 참조를 따로 잡고 있기 때문이다(A3의 fd → open file 구조 참고).

### 3.4 함정

- "메모리를 할당했으니 빠르겠지"는 틀렸다. 첫 접근이 진짜 비용이다. 벤치마크 첫 회차가 느린 흔한 이유다.
- 임베디드 eMMC/SD 카드에서 major fault는 ms 단위다. 실행 파일 자체도 파일 매핑이라 **코드 페이지도 major fault로 읽힌다**. 부팅 직후 첫 동작이 느린 이유 중 하나다.
- `ru_minflt`는 스레드 전체(프로세스)의 합이다. 스레드별은 `/proc/<pid>/task/<tid>/stat`.

---

## 4. Overcommit과 OOM killer

### 4.1 직관: 은행 대출

demand paging 때문에 커널은 "약속(가상 메모리)"과 "실제 지급(물리 페이지)"을 분리할 수 있다. 은행이 예금보다 많이 대출해 주는 것처럼, 커널도 RAM보다 많은 가상 메모리를 약속한다. 이걸 **overcommit**이라 한다. 대부분의 프로그램은 약속받은 걸 다 쓰지 않으니 보통은 잘 굴러간다. 모두가 한꺼번에 인출하면? 물리 페이지를 줄 수 없는 순간 커널은 **OOM(Out Of Memory) killer**를 불러 프로세스 하나를 골라 SIGKILL로 죽인다.

`/proc/sys/vm/overcommit_memory` 값에 따라 정책이 다르다.

| 값 | 이름 | 동작 |
|---|---|---|
| 0 | heuristic (기본) | 한 번의 요청이 "RAM + swap 전체보다 명백히 큰" 경우만 거절. 나머지는 허용 |
| 1 | always | 무조건 허용 (거대한 sparse 배열 쓰는 과학 계산용) |
| 2 | never (strict) | 총 약속량(`Committed_AS`)이 `CommitLimit` = swap + RAM × `overcommit_ratio`% 를 넘으면 거절 |

### 4.2 예제 4: 4 GB VM에서 8 GiB 약속받기 (안전한 데모)

실제로 메모리를 다 쓰지는 않는다. 약속만 받고, 64 MiB만 만진다.

```c
// ex4: overcommit — 만지지 않은 메모리는 "약속"일 뿐, 물리 RAM을 쓰지 않는다
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void status(const char *tag)
{
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) { perror("fopen"); exit(1); }
    char line[256];
    printf("[%s]", tag);
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "VmSize", 6) || !strncmp(line, "VmRSS", 5)) {
            line[strcspn(line, "\n")] = 0;
            printf("  %s", line);
        }
    printf("\n");
    fclose(f);
}

int main(void)
{
    const size_t GiB = 1UL << 30;
    status("start");

    // 1) 16 GiB 한 방: 모드 0 휴리스틱은 "RAM+swap보다 명백히 큰" 요청을 거절
    void *huge = malloc(16 * GiB);
    printf("malloc(16 GiB) at once -> %p %s\n", huge, huge ? "OK" : "(refused)");
    free(huge);

    // 2) 1 GiB씩 8번 = 8 GiB (VM RAM 4 GiB의 2배) — 각각은 통과
    void *blk[8];
    for (int i = 0; i < 8; i++) {
        blk[i] = malloc(GiB);
        if (!blk[i]) { printf("block %d failed\n", i); exit(1); }
    }
    printf("8 x malloc(1 GiB) -> all OK\n");
    status("8 GiB reserved");

    // 3) 그중 64 MiB만 실제로 만진다 -> RSS가 그만큼만 는다
    memset(blk[0], 0xA5, 64UL << 20);
    printf("blk[0]=%p last byte=0x%02x\n", blk[0],
           ((volatile unsigned char *)blk[0])[(64UL << 20) - 1]);
    status("touched 64 MiB");

    for (int i = 0; i < 8; i++) free(blk[i]);
    status("freed");
    return 0;
}
```

```sh
./ex4 && grep -E "^(MemTotal|CommitLimit|Committed_AS)" /proc/meminfo
```

```text
[start]  VmSize:	    4048 kB  VmRSS:	    1220 kB
malloc(16 GiB) at once -> (nil) (refused)
8 x malloc(1 GiB) -> all OK
[8 GiB reserved]  VmSize:	 8392688 kB  VmRSS:	    1252 kB
blk[0]=0xe5165d26f010 last byte=0xa5
[touched 64 MiB]  VmSize:	 8392688 kB  VmRSS:	   68452 kB
[freed]  VmSize:	    4048 kB  VmRSS:	    1284 kB
MemTotal:        3988828 kB
CommitLimit:     1994412 kB
Committed_AS:     945912 kB
```

출력에서 볼 것:

- `malloc(16 GiB)` 한 방은 거절됐다(RAM 4 GB + swap 0보다 명백히 크다). 하지만 1 GiB씩 8번은 **전부 성공**했다. 휴리스틱은 요청 하나씩만 본다.
- `VmSize`(가상 크기)는 8 GiB인데 `VmRSS`(실제 물리 메모리)는 1.2 MB 그대로다. 64 MiB를 만진 뒤에야 RSS가 약 67 MB로 늘었다.
- `CommitLimit`(1.9 GB)은 모드 2일 때만 강제되는 한도라 모드 0에선 숫자만 보여 준다.

**함정 하나**: 처음 이 코드를 짰을 때는 `malloc(16 GiB)`가 "OK"로 나왔다. gcc `-O2`가 "결과를 안 쓰는 malloc+free 쌍"을 통째로 지워 버렸기 때문이다. 그래서 포인터를 `printf`로 출력하고 `volatile`로 읽게 고쳤다. 메모리 실험 코드는 컴파일러 최적화를 꼭 의심한다.

모드 2(strict)로 잠깐 바꿔서 같은 프로그램을 돌리고 바로 되돌려 보았다.

```sh
sudo sysctl -q vm.overcommit_memory=2; ./ex4; sudo sysctl -q vm.overcommit_memory=0
```

```text
[start]  VmSize:	    3408 kB  VmRSS:	    1212 kB
malloc(16 GiB) at once -> (nil) (refused)
block 0 failed
```

출력에서 볼 것: strict 모드에선 1 GiB 하나도 거절된다. 이미 약 0.95 GB가 약속돼 있고 한도가 1.99 GB이기 때문이다. swap 없는 임베디드 장비에서 모드 2는 "fork가 실패한다" 같은 부작용이 크다. 실제 장비에선 모드 0을 두고 cgroup 메모리 한도(L8)로 서비스별 상한을 거는 게 일반적이다.

### 4.3 OOM killer가 누구를 고르나

커널은 프로세스마다 **badness 점수**(대략 "RSS + swap + page table 크기"를 RAM 대비 비율로)를 매기고 가장 큰 것을 죽인다. 유저가 조절하는 손잡이가 `/proc/<pid>/oom_score_adj`(-1000 ~ 1000)다. -1000이면 절대 안 죽고, 1000이면 1순위다. 읽기 전용 `/proc/<pid>/oom_score`는 그걸 반영한 결과값이다.

```sh
echo "self(bash): score=$(cat /proc/self/oom_score) adj=$(cat /proc/self/oom_score_adj)"
for n in systemd-journal sshd; do P=$(pgrep -o $n); echo "$n($P): score=$(cat /proc/$P/oom_score) adj=$(cat /proc/$P/oom_score_adj)"; done
sh -c "echo 500 > /proc/self/oom_score_adj; echo after adj=500: score=\$(cat /proc/self/oom_score)"
```

```text
self(bash): score=668 adj=0
systemd-journal(618): score=502 adj=-250
sshd(1255): score=0 adj=-1000
after adj=500: score=1000
```

출력에서 볼 것: 작은 프로세스인데 점수가 668이다. 최근 커널은 표시값을 `(1000 + 정규화된 badness) × 2/3` 꼴로 보여 주기 때문에 adj=0이면 기본이 약 667이다. sshd는 -1000이라 0(면제), journald는 systemd 유닛 설정 `OOMScoreAdjust=-250`이 적용돼 있다. 장비에서 **제어 데몬은 낮추고**(예: -900), 로그 업로더·웹 UI 같은 부가 서비스는 높여서 OOM 시 무엇이 먼저 죽을지 설계한다.

OOM이 나면 커널 로그에 `Out of memory: Killed process <pid> (<name>) total-vm:...kB, anon-rss:...kB` 형태의 줄과 당시 메모리 상태 덤프가 남는다. `dmesg`나 `journalctl -k`로 찾는다. 이 노트에서는 VM을 일부러 OOM시키지 않았다.

---

## 5. malloc의 속사정 — brk와 mmap

### 5.1 정의

glibc `malloc`은 커널에서 메모리를 두 가지 방법으로 받아 온다.

- **`brk`/`sbrk`**: `[heap]` VMA의 끝(program break)을 위로 밀어 늘린다. 작은 할당들을 그 안에서 잘라 쓴다(arena).
- **`mmap`(익명)**: 큰 할당은 독립 매핑으로 받는다. `free()`하면 바로 `munmap`되어 OS에 반납된다.

기준이 `M_MMAP_THRESHOLD`(기본 128 KiB)다. 단, 정확히는 "heap 꼭대기(top chunk)에 남은 공간으로 못 줄 때, 요청이 threshold 이상이면 mmap"이다. 그리고 **동적 threshold**: mmap으로 받은 블록을 `free`하면 threshold가 그 크기로 올라간다(최대 32 MiB, 64비트 기준). 같은 크기 할당/해제를 반복할 때 매번 mmap/munmap하지 않으려는 장치다.

### 5.2 예제 5: strace로 brk/mmap 보기

```c
// ex5: malloc은 언제 brk(heap)를, 언제 mmap을 쓰나 — strace로 함께 본다
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void *heap_start;

static void *alloc(size_t n)
{
    void *p = malloc(n);
    if (!p) { perror("malloc"); exit(1); }
    char *brk_now = sbrk(0);
    const char *where = ((char *)p >= (char *)heap_start && (char *)p < brk_now)
                        ? "heap(brk)" : "mmap";
    fprintf(stderr, ">> malloc(%7zu) = %p  %s\n", n, p, where);
    return p;
}

int main(void)
{
    heap_start = sbrk(0);
    void *a = alloc(100);
    void *b = alloc(64 * 1024);
    void *c = alloc(127 * 1024);
    void *d = alloc(129 * 1024);        // 기본 M_MMAP_THRESHOLD(128 KiB)보다 크다
    void *e = alloc(1024 * 1024);
    fprintf(stderr, ">> free(1 MiB)  -> 동적 threshold가 1 MiB로 올라간다\n");
    free(e);
    void *f = alloc(1024 * 1024);       // 이번엔 heap에서
    free(a); free(b); free(c); free(d); free(f);
    return 0;
}
```

```sh
setarch -R strace -e trace=brk,mmap,munmap ./ex5 2>&1 | tail -13
```

```text
brk(NULL)                               = 0xaaaaaaac1000
brk(0xaaaaaac00000)                     = 0xaaaaaac00000
>> malloc(    100) = 0xaaaaaaac1010  heap(brk)
>> malloc(  65536) = 0xaaaaaaac1080  heap(brk)
>> malloc( 130048) = 0xaaaaaaad1090  heap(brk)
>> malloc( 132096) = 0xaaaaaaaf0ca0  heap(brk)
mmap(NULL, 1052672, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0xfffff7cdf000
>> malloc(1048576) = 0xfffff7cdf010  mmap
>> free(1 MiB)  -> 동적 threshold가 1 MiB로 올라간다
munmap(0xfffff7cdf000, 1052672)         = 0
brk(0xaaaaaae00000)                     = 0xaaaaaae00000
>> malloc(1048576) = 0xaaaaaab110b0  heap(brk)
+++ exited with 0 +++
```

(앞부분의 동적 링커 mmap 줄들은 생략했다. ASLR을 끄고 돌린 결과다.)

출력에서 볼 것:

- 첫 `brk`가 heap을 `0xaaaaaac00000`, 즉 **2 MiB 경계까지** 한 번에 늘렸다(약 1.25 MB). 이 VM의 glibc 2.43은 THP를 고려해 heap 끝을 2 MiB에 맞추는 것으로 보인다(10장 참고).
- 그래서 129 KiB 할당은 threshold(128 KiB)보다 큰데도 **heap에서** 나왔다. top chunk에 공간이 남아 있었기 때문이다. "128 KiB 넘으면 무조건 mmap"은 틀린 상식이다. ASLR을 켜고 여러 번 돌리면 heap 시작 위치에 따라 결과가 바뀌기도 한다.
- 1 MiB는 top에 공간이 없어서 `mmap(1052672)`(1 MiB + 헤더, 페이지 올림)으로 받았다.
- `free(1 MiB)` → 즉시 `munmap`. 그리고 다음 `malloc(1 MiB)`는 threshold가 올라가서 `brk`를 늘려 heap에서 나왔다.

### 5.3 함정

- **"free 했는데 RSS가 안 줄어요"**: heap 중간이 비어도 꼭대기가 아니면 `brk`를 줄일 수 없다(fragmentation). glibc는 `malloc_trim(0)`으로 빈 페이지에 `MADV_DONTNEED`를 걸어 돌려줄 수 있다.
- 멀티스레드 프로그램은 스레드마다 arena를 만들고, 그 arena는 `mmap`으로 받은 64 MiB 단위 영역이다. `/proc/<pid>/maps`에 64 MiB짜리 `---p` 예약 영역이 여러 개 보이는 이유다. 임베디드에선 `MALLOC_ARENA_MAX=2` 같은 환경 변수로 줄이기도 한다.
- RT 코드에서 `malloc`은 언제 syscall + page fault를 일으킬지 모른다. RT 루프 전에 미리 할당·touch하고 루프 안에선 할당하지 않는다(8장).

---

## 6. mmap 완전 정리

### 6.1 시그니처와 조합

```c
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int   munmap(void *addr, size_t length);
int   mprotect(void *addr, size_t len, int prot);
int   msync(void *addr, size_t length, int flags);
```

- `addr`: 보통 `NULL`(커널이 고름). `MAP_FIXED`와 같이 주면 그 주소에 강제(기존 매핑을 덮어쓰므로 위험; `MAP_FIXED_NOREPLACE`가 안전한 버전).
- `prot`: `PROT_READ`, `PROT_WRITE`, `PROT_EXEC`, `PROT_NONE`의 OR. PTE 권한 비트가 된다.
- `offset`: 페이지 크기의 배수여야 한다.
- 실패하면 `NULL`이 아니라 **`MAP_FAILED`**(`(void *)-1`)를 돌려준다.

매핑은 두 축의 조합 네 가지다.

| | MAP_SHARED | MAP_PRIVATE |
|---|---|---|
| **파일** (`fd` 있음) | 쓰면 page cache가 바뀌고 결국 파일에 반영. 다른 프로세스도 봄. 예: 파일 공유 편집, 장치 버퍼 | 쓰면 COW로 내 사본만 바뀜. 파일 불변. 예: 실행 파일·라이브러리 로딩 |
| **익명** (`MAP_ANONYMOUS`, `fd=-1`) | fork한 자식과 공유. 예: 부모-자식 공유 메모리 | 0으로 채운 나만의 메모리. 예: 큰 malloc, 스레드 스택 |

**COW(copy-on-write)** 는 "쓰기 전까지는 원본 페이지를 같이 쓰다가, 쓰는 순간 그 페이지만 복사해서 내 것으로 만드는" 방식이다. 커널은 PTE를 읽기 전용으로 만들어 두고, 쓰기 시 나는 permission fault에서 복사를 한다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="170" y="22" font-size="14" text-anchor="middle">MAP_SHARED</text>
<text x="510" y="22" font-size="14" text-anchor="middle">MAP_PRIVATE (B가 쓴 뒤)</text>
<line x1="340" y1="8" x2="340" y2="292" stroke="#888" stroke-dasharray="4 4"/>
<rect x="30" y="45" width="120" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="90" y="68" font-size="13" text-anchor="middle">프로세스 A</text>
<rect x="190" y="45" width="120" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="250" y="68" font-size="13" text-anchor="middle">프로세스 B</text>
<rect x="85" y="140" width="170" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="170" y="165" font-size="13" text-anchor="middle">page cache 페이지</text>
<rect x="85" y="240" width="170" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="170" y="263" font-size="13" text-anchor="middle">디스크의 파일</text>
<line x1="90" y1="81" x2="150" y2="138" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="250" y1="81" x2="190" y2="138" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="170" y1="180" x2="170" y2="238" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah3)"/><text x="180" y="214" font-size="12">writeback / msync</text><text x="170" y="118" font-size="12" text-anchor="middle">둘 다 같은 물리 페이지</text>
<rect x="365" y="45" width="120" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="425" y="68" font-size="13" text-anchor="middle">프로세스 A</text>
<rect x="530" y="45" width="120" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="590" y="68" font-size="13" text-anchor="middle">프로세스 B</text>
<rect x="365" y="140" width="150" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="440" y="165" font-size="13" text-anchor="middle">page cache 페이지</text>
<rect x="535" y="140" width="125" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="597" y="158" font-size="13" text-anchor="middle">B 전용 사본</text><text x="597" y="174" font-size="12" text-anchor="middle">(익명 페이지)</text>
<rect x="365" y="240" width="150" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="440" y="263" font-size="13" text-anchor="middle">파일 (불변)</text>
<line x1="425" y1="81" x2="435" y2="138" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="590" y1="81" x2="595" y2="138" stroke="#d0564a" stroke-width="2" marker-end="url(#ah3)"/>
<line x1="570" y1="81" x2="480" y2="138" stroke="#888" stroke-dasharray="3 3"/><text x="560" y="115" font-size="12" text-anchor="middle">쓰기 전엔 여기</text>
<line x1="440" y1="180" x2="440" y2="238" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah3)"/><text x="545" y="220" font-size="12">B의 쓰기는 파일로</text><text x="545" y="236" font-size="12">절대 안 간다</text>
</svg>
```

그림 5 — SHARED는 모든 매핑이 같은 page cache 페이지를 가리킨다. PRIVATE는 쓰기 전까지 공유하다가 쓰는 순간 그 페이지만 복사(COW)된다.

### 6.2 예제 6: 같은 파일을 SHARED와 PRIVATE로 동시에 매핑

```c
// ex6: 같은 파일을 MAP_SHARED / MAP_PRIVATE로 매핑하고 써 본다
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define DIE(m) do { perror(m); exit(1); } while (0)

static void show_file(int fd, const char *tag)
{
    char buf[17] = {0};
    if (pread(fd, buf, 16, 0) != 16) DIE("pread");
    printf("%-28s file = \"%s\"\n", tag, buf);
}

int main(void)
{
    int fd = open("/tmp/A5/ex6.dat", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) DIE("open");
    if (write(fd, "AAAAAAAAAAAAAAAA", 16) != 16) DIE("write");
    if (ftruncate(fd, 4096) < 0) DIE("ftruncate");   // 매핑은 페이지 단위

    char *sh = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    char *pv = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (sh == MAP_FAILED || pv == MAP_FAILED) DIE("mmap");
    show_file(fd, "initial");

    memcpy(sh, "SHARED", 6);              // page cache 페이지를 직접 수정
    show_file(fd, "after write via SHARED");
    printf("%-28s pv   = \"%.16s\"  (아직 COW 전이라 같이 보임)\n", "", pv);

    memcpy(pv, "PRIV", 4);                // 여기서 COW: 내 전용 사본 생성
    show_file(fd, "after write via PRIVATE");
    printf("%-28s pv   = \"%.16s\"  sh = \"%.16s\"\n", "", pv, sh);

    memcpy(sh + 8, "XY", 2);              // shared 쪽을 다시 바꿔도
    printf("%-28s pv   = \"%.16s\"  (COW 이후엔 안 보임)\n", "after SHARED write #2", pv);

    if (msync(sh, 4096, MS_SYNC) < 0) DIE("msync");   // dirty 페이지를 디스크로
    if (munmap(sh, 4096) < 0 || munmap(pv, 4096) < 0) DIE("munmap");
    close(fd);
    return 0;
}
```

```sh
./ex6 && head -c 16 /tmp/A5/ex6.dat; echo
```

```text
initial                      file = "AAAAAAAAAAAAAAAA"
after write via SHARED       file = "SHAREDAAAAAAAAAA"
                             pv   = "SHAREDAAAAAAAAAA"  (아직 COW 전이라 같이 보임)
after write via PRIVATE      file = "SHAREDAAAAAAAAAA"
                             pv   = "PRIVEDAAAAAAAAAA"  sh = "SHAREDAAAAAAAAAA"
after SHARED write #2        pv   = "PRIVEDAAAAAAAAAA"  (COW 이후엔 안 보임)
SHAREDAAXYAAAAAA
```

출력에서 볼 것:

- SHARED로 쓰자마자 `pread()`로 읽은 파일 내용이 바뀌었다. `write()`를 안 했는데도. SHARED 매핑은 page cache 페이지 그 자체이고, `read()`도 같은 page cache에서 읽기 때문이다.
- PRIVATE 매핑은 **쓰기 전까지는** SHARED의 변경이 보였다(`pv = "SHARED..."`). 같은 page cache 페이지를 읽기 전용으로 가리키고 있었기 때문이다. POSIX는 이 부분을 "정의되지 않음"으로 두지만 Linux는 이렇게 동작한다.
- PRIVATE로 쓰는 순간 COW가 일어나 이후 SHARED 쪽 변경(`XY`)이 안 보인다. 파일엔 `XY`가 들어갔다.

### 6.3 msync — SHARED 매핑의 dirty 페이지를 언제 디스크에 쓰나

SHARED 매핑에 쓰면 page cache 페이지가 **dirty**(디스크와 내용이 다름)가 된다. 커널의 writeback 스레드가 나중에(기본 약 30초 이내) 디스크에 쓴다. 지금 당장 쓰고 싶으면 `msync(MS_SYNC)`를 부른다. `write()` 경로의 `fsync()`에 해당한다.

### 6.4 예제 12: Dirty가 쌓였다가 msync로 빠지는 것 보기

64 MiB 파일을 SHARED로 매핑해 채운 뒤 `/proc/meminfo`의 `Dirty`를 본다.

```c
// ex12: MAP_SHARED에 쓰면 page cache가 Dirty가 되고, msync가 디스크로 내린다
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

#define LEN (64UL << 20)

static long meminfo(const char *key)
{
    FILE *f = fopen("/proc/meminfo", "r");
    char k[64]; long v = -1, x;
    if (!f) { perror("fopen"); exit(1); }
    while (fscanf(f, "%63s %ld kB", k, &x) == 2)
        if (!strncmp(k, key, strlen(key)) && k[strlen(key)] == ':') { v = x; break; }
    fclose(f);
    return v;
}

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

int main(void)
{
    int fd = open("/var/tmp/A5/dirty.bin", O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("open"); exit(1); }
    if (ftruncate(fd, LEN) < 0) { perror("ftruncate"); exit(1); }
    char *p = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(1); }
    close(fd);

    printf("before write : Dirty=%6ld kB\n", meminfo("Dirty"));
    memset(p, 0x5A, LEN);
    printf("after memset : Dirty=%6ld kB  Writeback=%ld kB\n",
           meminfo("Dirty"), meminfo("Writeback"));
    double t0 = now_ms();
    if (msync(p, LEN, MS_SYNC) < 0) { perror("msync"); exit(1); }
    printf("after msync  : Dirty=%6ld kB  (msync took %.1f ms)\n",
           meminfo("Dirty"), now_ms() - t0);
    munmap(p, LEN);
    return 0;
}
```

```sh
sync && ./ex12
```

```text
before write : Dirty=    36 kB
after memset : Dirty= 65536 kB  Writeback=0 kB
after msync  : Dirty=     0 kB  (msync took 38.2 ms)
```

출력에서 볼 것: memset 직후 시스템 전체 Dirty가 정확히 64 MiB(65536 kB) 늘었다. `msync(MS_SYNC)`가 38 ms 동안 디스크에 쓰고 나서 0으로 돌아왔다. 전원이 갑자기 꺼질 수 있는 장비(드론, 차량)에서 **msync 없이 SHARED 매핑에 설정·로그를 쓰면 최대 수십 초치 데이터가 날아간다**. `munmap`이나 `close`도 디스크 쓰기를 보장하지 않는다.

### 6.5 예제 7: mprotect와 SIGSEGV

`mprotect`로 페이지를 읽기 전용으로 바꾸고 쓰기를 시도한다. SIGSEGV 핸들러에서 `si_code`와 `si_addr`를 꺼내고 `siglongjmp`로 탈출한다(데모용; 시그널은 A6에서 자세히).

```c
// ex7: mprotect로 페이지 권한을 바꾸고, 위반하면 SIGSEGV가 어떻게 오는지 본다
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

static sigjmp_buf env;
static volatile sig_atomic_t code;
static void *volatile addr;

static void on_segv(int sig, siginfo_t *si, void *uc)
{
    (void)sig; (void)uc;
    code = si->si_code;          // SEGV_MAPERR: 매핑 없음 / SEGV_ACCERR: 권한 위반
    addr = si->si_addr;          // fault 난 가상 주소 (ARM의 FAR_EL1 값)
    siglongjmp(env, 1);
}

static void try_write(volatile char *p, const char *what)
{
    if (sigsetjmp(env, 1) == 0) {
        p[0] = 'x';
        printf("%-24s write OK\n", what);
    } else {
        printf("%-24s SIGSEGV si_code=%s si_addr=%p\n", what,
               code == SEGV_ACCERR ? "SEGV_ACCERR" :
               code == SEGV_MAPERR ? "SEGV_MAPERR" : "other", addr);
    }
}

int main(void)
{
    struct sigaction sa = {0};
    sa.sa_sigaction = on_segv;
    sa.sa_flags = SA_SIGINFO;
    if (sigaction(SIGSEGV, &sa, NULL) < 0) { perror("sigaction"); exit(1); }

    long ps = sysconf(_SC_PAGESIZE);
    char *p = mmap(NULL, 3 * ps, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(1); }
    printf("region %p .. %p (3 pages)\n", (void *)p, (void *)(p + 3 * ps));

    try_write(p, "page0 RW");
    if (mprotect(p, ps, PROT_READ) < 0) { perror("mprotect"); exit(1); }
    try_write(p, "page0 after PROT_READ");
    if (munmap(p + 2 * ps, ps) < 0) { perror("munmap"); exit(1); }
    try_write(p + 2 * ps, "page2 after munmap");
    try_write(p + ps + 100, "page1 still RW");
    return 0;
}
```

```text
region 0xe4f1c3412000 .. 0xe4f1c3415000 (3 pages)
page0 RW                 write OK
page0 after PROT_READ    SIGSEGV si_code=SEGV_ACCERR si_addr=0xe4f1c3412000
page2 after munmap       SIGSEGV si_code=SEGV_MAPERR si_addr=0xe4f1c3414000
page1 still RW           write OK
```

출력에서 볼 것:

- **`SEGV_ACCERR`**: VMA는 있는데 권한 위반. **`SEGV_MAPERR`**: 그 주소에 VMA 자체가 없음. 크래시 덤프에서 이 둘을 구분하면 "읽기 전용 영역에 씀(예: 문자열 리터럴 수정)"과 "해제된/쓰레기 포인터"를 구분할 수 있다.
- `si_addr`는 ARM의 **FAR_EL1**(Fault Address Register)에 들어 있던 값이다. 커널이 abort 처리 중 읽어서 시그널에 넣어 준다. bare-metal에서 HardFault 핸들러가 MMFAR/BFAR를 읽던 것과 같다.
- `mprotect`는 페이지 단위다. 3페이지 VMA 중 1페이지만 바꾸면 VMA가 둘로 쪼개진다(maps에서 확인 가능).
- 이 기법의 실제 쓰임: 스택 끝 **guard page**(`PROT_NONE`), 스레드 스택 오버플로 감지, JIT, 그리고 디버깅용 "이 버퍼에 누가 쓰나" 함정 놓기.

### 6.6 munmap의 규칙

- 범위 일부만 해제해도 된다(VMA가 쪼개진다). 매핑 안 된 범위를 해제해도 에러가 아니다.
- 해제 후 그 주소를 쓰면 `SEGV_MAPERR`(위 page2). 단, 그 사이 다른 `mmap`이 같은 주소를 받았다면 **조용히 남의 메모리를 망가뜨린다**. use-after-munmap은 use-after-free보다 찾기 어렵다.

---

## 7. 프로세스 간 메모리 공유

### 7.1 예제 8: fork 전에 만든 익명 매핑

`MAP_SHARED | MAP_ANONYMOUS`를 fork 전에 만들면 부모와 자식이 같은 물리 페이지를 본다. 비교용으로 `MAP_PRIVATE` 매핑도 하나 만든다.

```c
// ex8: fork 전에 만든 익명 매핑 — SHARED는 부모·자식이 같은 물리 페이지, PRIVATE는 COW
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

static long *map_counter(int flags)
{
    long *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   flags | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(1); }
    return p;
}

int main(void)
{
    long *shared = map_counter(MAP_SHARED);
    long *priv = map_counter(MAP_PRIVATE);
    *shared = 0;
    *priv = 0;

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid == 0) {                                // child
        for (int i = 0; i < 1000000; i++)
            atomic_fetch_add((_Atomic long *)shared, 1);
        *priv = 777;                               // COW: 자식 전용 사본에 씀
        printf("child : shared=%ld priv=%ld (vaddr shared=%p)\n",
               *shared, *priv, (void *)shared);
        fflush(stdout);                            // _exit은 stdio 버퍼를 안 비운다
        _exit(0);
    }
    for (int i = 0; i < 1000000; i++)              // 부모도 동시에 증가
        atomic_fetch_add((_Atomic long *)shared, 1);
    if (waitpid(pid, NULL, 0) < 0) { perror("waitpid"); exit(1); }
    printf("parent: shared=%ld priv=%ld (vaddr shared=%p)\n",
           *shared, *priv, (void *)shared);
    return 0;
}
```

```text
child : shared=1861167 priv=777 (vaddr shared=0xf2a63e382000)
parent: shared=2000000 priv=0 (vaddr shared=0xf2a63e382000)
```

출력에서 볼 것:

- 부모와 자식이 각각 100만 번 원자적으로 더한 결과가 정확히 200만이다. 같은 물리 페이지에서 `atomic_fetch_add`(arm64에선 LSE `ldadd` 계열 명령)가 경쟁했다. 자식의 값(1861167)은 자식이 끝난 시점의 중간값이다.
- PRIVATE 매핑은 자식이 777을 써도 부모는 0이다. fork는 모든 PRIVATE 페이지를 COW로 공유하다가 쓰는 쪽만 복사한다. **fork가 빠른 이유**이기도 하다(A2).
- 가상 주소가 두 프로세스에서 같지만, 같은 이유는 "fork가 주소 공간을 복제했기 때문"이다. 관계없는 프로세스끼리는 주소가 다를 수 있으니 공유 메모리 안에 **포인터를 저장하면 안 된다**. 오프셋을 저장한다.
- 첫 시도에서는 자식 줄이 안 나왔다. 출력이 파이프일 때 stdout은 full buffering이고 `_exit()`는 stdio 버퍼를 비우지 않는다. 그래서 `fflush(stdout)`을 넣었다.

### 7.2 예제 9: shm_open — 이름으로 만나는 공유 메모리

관계없는 프로세스는 이름이 필요하다. `shm_open("/name")`은 `/dev/shm`(tmpfs)에 파일을 만들고 fd를 준다. 나머지는 파일 mmap과 같다. 예를 들어 센서 키트라면 IMU 데몬이 최신 샘플을 게시하고, 제어·로깅 프로세스가 읽는 구조다. 찢어진 읽기(쓰는 도중 읽기)를 막으려고 간단한 **seqlock**(F4)을 썼다.

```c
// ex9: shm_open — 관계없는 두 프로세스가 이름으로 같은 메모리를 공유한다
#include <fcntl.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>

struct imu_shm {                 // 예: IMU 데몬이 최신 샘플을 게시
    _Atomic unsigned seq;        // 홀수 = 쓰는 중 (seqlock)
    float ax, ay, az;
    long  t_ns;
};

int main(int argc, char **argv)
{
    int writer = argc > 1 && strcmp(argv[1], "w") == 0;
    int fd = shm_open("/a5_imu", writer ? O_CREAT | O_RDWR : O_RDONLY, 0644);
    if (fd < 0) { perror("shm_open"); exit(1); }
    if (writer && ftruncate(fd, sizeof(struct imu_shm)) < 0) { perror("ftruncate"); exit(1); }
    struct imu_shm *s = mmap(NULL, sizeof *s, writer ? PROT_READ | PROT_WRITE : PROT_READ,
                             MAP_SHARED, fd, 0);
    if (s == MAP_FAILED) { perror("mmap"); exit(1); }
    close(fd);

    for (int i = 0; i < 5; i++) {
        if (writer) {
            atomic_fetch_add(&s->seq, 1);              // odd: writing
            s->ax = 0.01f * i; s->ay = 0.0f; s->az = 9.81f; s->t_ns = i * 1000000L;
            atomic_fetch_add(&s->seq, 1);              // even: done
            usleep(100000);
        } else {
            unsigned a, b;
            float ax, az; long t;
            do {
                a = atomic_load(&s->seq);
                ax = s->ax; az = s->az; t = s->t_ns;
                b = atomic_load(&s->seq);
            } while ((a & 1) || a != b);              // 쓰는 중이면 다시 읽기
            printf("reader: seq=%u ax=%.2f az=%.2f t=%ld\n", a, ax, az, t);
            usleep(100000);
        }
    }
    if (writer) shm_unlink("/a5_imu");                 // 이름 제거 (매핑은 유지)
    return 0;
}
```

```sh
(./ex9 w &) ; sleep 0.05; ls -l /dev/shm/a5_imu; ./ex9 r; sleep 0.2; ls /dev/shm/
```

```text
-rw-r--r-- 1 donh donh 24 Oct  5 22:44 /dev/shm/a5_imu
reader: seq=2 ax=0.00 az=9.81 t=0
reader: seq=4 ax=0.01 az=9.81 t=1000000
reader: seq=6 ax=0.02 az=9.81 t=2000000
reader: seq=8 ax=0.03 az=9.81 t=3000000
reader: seq=10 ax=0.04 az=9.81 t=4000000
```

출력에서 볼 것: `/dev/shm/a5_imu`가 24바이트 파일로 보인다(구조체 크기). reader는 항상 짝수 seq만 받았다. 마지막 `ls /dev/shm/`가 빈 줄인 것은 writer가 `shm_unlink`로 이름을 지웠기 때문이다. 이 seqlock은 개념 데모라서 데이터 필드를 일반 변수로 읽는다. 엄밀한 C11에선 data race이고, 제대로 하려면 필드도 relaxed atomic으로 읽거나 배리어를 둔다(F5).

| 방법 | 이름 | 수명 | 쓰는 곳 |
|---|---|---|---|
| `MAP_SHARED`+`MAP_ANONYMOUS` 후 fork | 없음 | 마지막 매핑이 사라질 때 | 부모-자식 워커 |
| `shm_open` + `mmap` | `/dev/shm/name` | `shm_unlink`까지 (재부팅 시 소멸) | 관계없는 데몬 간 |
| 일반 파일 + `MAP_SHARED` | 파일 경로 | 파일이 지워질 때까지 | 영속 데이터 공유 |
| `memfd_create` + fd 전달 | 없음 (fd만) | fd·매핑이 없어질 때 | UNIX socket으로 fd 넘기기 (A7) |
| SysV `shmget` | 정수 key | `IPC_RMID`까지 | 레거시 코드 |

---

## 8. mlock / mlockall — RT 루프에서 page fault 없애기

### 8.1 직관

RT 스레드의 적은 "예측 못 하는 지연"이다. page fault는 그 대표다. major fault는 ms 단위이고, minor fault도 수 µs에 메모리 할당(최악엔 reclaim)까지 끌어들인다. 해결책은 "루프 시작 전에 필요한 페이지를 전부 붙이고, 커널이 빼앗아 가지 못하게 잠그는 것"이다.

- `mlock(addr, len)`: 그 범위를 RAM에 붙이고(fault를 미리 일으키고) 잠근다. swap·reclaim 대상에서 빠진다.
- `mlockall(MCL_CURRENT)`: 지금 있는 모든 매핑을 잠근다.
- `mlockall(MCL_FUTURE)`: **앞으로 생길 매핑도** 만들어질 때 바로 붙이고 잠근다. mmap이 곧 MAP_POPULATE처럼 동작한다.
- 한도: 일반 유저는 `RLIMIT_MEMLOCK`(`ulimit -l`)까지만. root(정확히는 `CAP_IPC_LOCK`)는 제한 없음.

### 8.2 예제 10: mlockall 전후 비교, 그리고 RLIMIT 함정

```c
// ex10: mlockall(MCL_CURRENT|MCL_FUTURE) — RT 루프에서 page fault를 없앤다
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>

static long minflt(void)
{
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_minflt;
}

static void vmlck(void)
{
    FILE *f = fopen("/proc/self/status", "r");
    char line[256];
    if (!f) { perror("fopen"); exit(1); }
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, "VmLck", 5) || !strncmp(line, "VmRSS", 5))
            printf("   %s", line);
    fclose(f);
}

static void touch_buf(const char *tag, size_t len)
{
    long f0 = minflt();
    char *b = malloc(len);                  // 큰 버퍼 → mmap
    if (!b) { printf("%s: malloc(%zu MiB) failed\n", tag, len >> 20); return; }
    long f1 = minflt();
    memset(b, 1, len);
    long f2 = minflt();
    printf("%s: faults in malloc=%ld, in first touch=%ld (b[last]=%d)\n",
           tag, f1 - f0, f2 - f1, ((volatile char *)b)[len - 1]);
    free(b);
}

int main(void)
{
    struct rlimit rl;
    getrlimit(RLIMIT_MEMLOCK, &rl);
    printf("RLIMIT_MEMLOCK soft=%llu KiB\n", (unsigned long long)rl.rlim_cur >> 10);

    touch_buf("before mlockall", 16 << 20);
    if (mlockall(MCL_CURRENT | MCL_FUTURE) < 0) { perror("mlockall"); exit(1); }
    printf("mlockall OK\n");
    vmlck();
    touch_buf("after  mlockall", 16 << 20);
    return 0;
}
```

```sh
echo "== as user" && ./ex10; echo "== as root (CAP_IPC_LOCK)" && sudo ./ex10
```

```text
== as user
RLIMIT_MEMLOCK soft=8192 KiB
before mlockall: faults in malloc=1, in first touch=519 (b[last]=1)
mlockall OK
   VmLck:	    4260 kB
   VmRSS:	    4188 kB
after  mlockall: malloc(16 MiB) failed
== as root (CAP_IPC_LOCK)
RLIMIT_MEMLOCK soft=8192 KiB
before mlockall: faults in malloc=1, in first touch=519 (b[last]=1)
mlockall OK
   VmLck:	    3244 kB
   VmRSS:	    3172 kB
after  mlockall: faults in malloc=4096, in first touch=0 (b[last]=1)
```

출력에서 볼 것:

- root에서 mlockall 이후에는 fault가 **malloc 안에서 4096번** 일어나고 첫 touch에선 0번이다. 지연을 "할당 시점"으로 옮겼다. RT 루프 안에서 접근할 때는 fault가 없다.
- 일반 유저로는 `mlockall` 자체는 성공했지만(현재 사용량 4 MB < 8 MB), 그 뒤 16 MiB `malloc`이 **NULL**을 돌려줬다. MCL_FUTURE 상태에선 새 매핑이 한도를 넘으면 mmap이 `EAGAIN`으로 실패한다. strace로 확인했다:

```text
mlockall(MCL_CURRENT|MCL_FUTURE)        = 0
mmap(NULL, 16912384, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = -1 EAGAIN (Resource temporarily unavailable)
```

- mlockall 전 16 MiB(4096 페이지) 첫 touch의 fault가 4096이 아니라 519다. 같은 strace에서 `madvise(..., MADV_HUGEPAGE)`가 보였다. 이 glibc는 큰 mmap 청크에 THP를 요청하고, 2 MiB 정렬된 안쪽 7개는 huge page로 7번, 양 끝 정렬 안 된 부분 512개는 4K로 fault가 났다(7 + 512 = 519).
- 그래서 실무 RT 앱은 systemd 유닛에 `LimitMEMLOCK=infinity`를 주거나 `CAP_IPC_LOCK`을 준다. 이걸 빼먹고 "장비에서만 malloc이 NULL"을 겪는 경우가 많다.

### 8.3 RT 앱의 메모리 초기화 관례 (N2에서 다시)

1. `mlockall(MCL_CURRENT | MCL_FUTURE)`.
2. 스택 prefault: 예상 최대 스택 크기만큼 지역 배열을 잡고 memset하는 함수를 한 번 호출한다. 스택 페이지는 처음 닿을 때 붙기 때문이다.
3. 모든 버퍼·풀을 미리 할당하고 touch. glibc가 free한 메모리를 OS에 돌려주지 않게 `mallopt(M_TRIM_THRESHOLD, -1)`과 `mallopt(M_MMAP_MAX, 0)`을 쓰기도 한다(rt-wiki의 전통적인 권고).
4. 그 다음에 `SCHED_FIFO`로 올리고 루프 시작. 루프 안에선 malloc·파일 열기·printf 금지.

---

## 9. 메모리 사용량 제대로 읽기 — RSS, PSS, /proc/meminfo

### 9.1 RSS의 함정

**RSS(Resident Set Size)** 는 "이 프로세스의 page table에 붙어 있는 물리 페이지 합"이다. libc처럼 여러 프로세스가 공유하는 페이지를 **각자 전부 셈**하므로, 모든 프로세스의 RSS를 더하면 실제 RAM보다 커진다. **PSS(Proportional Set Size)** 는 공유 페이지를 공유하는 프로세스 수로 나눠서 센다. 시스템 전체 합이 맞는 값이다. **USS**는 나 혼자만 쓰는 페이지(Private_Clean + Private_Dirty)다.

### 9.2 예제 11: 32 MiB를 4개 프로세스가 공유하면

```c
// ex11: RSS vs PSS — 4개 프로세스가 32 MiB를 공유하면 각자 PSS는 1/4
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

#define LEN (32UL << 20)

static void smaps_entry(const char *who, void *addr)
{
    char key[32], line[256];
    snprintf(key, sizeof key, "%lx-", (unsigned long)addr);
    FILE *f = fopen("/proc/self/smaps", "r");
    if (!f) { perror("fopen"); exit(1); }
    int in = 0;
    printf("[%s pid=%d]", who, getpid());
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, key, strlen(key))) { in = 1; continue; }
        if (in && (!strncmp(line, "Rss", 3) || !strncmp(line, "Pss:", 4) ||
                   !strncmp(line, "Shared_Dirty", 12))) {
            line[strcspn(line, "\n")] = 0;
            printf("  %s", line);
        }
        if (in && !strncmp(line, "VmFlags", 7)) break;
    }
    printf("\n");
    fclose(f);
}

int main(void)
{
    char *p = mmap(NULL, LEN, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { perror("mmap"); exit(1); }
    memset(p, 7, LEN);                       // 부모가 전부 채움
    smaps_entry("parent alone", p);
    fflush(stdout);

    for (int i = 0; i < 3; i++) {
        if (fork() == 0) {
            volatile long sum = 0;
            for (size_t o = 0; o < LEN; o += 4096) sum += p[o];  // 매핑 연결
            usleep(700000);                  // 4개가 동시에 살아 있는 동안 측정
            if (i == 0) smaps_entry("child", p);
            fflush(stdout);
            usleep(500000);
            fflush(stdout);
            _exit(0);
        }
    }
    usleep(500000);
    smaps_entry("parent + 3 kids", p);
    fflush(stdout);
    while (wait(NULL) > 0) ;
    return 0;
}
```

```text
[parent alone pid=187021]  Rss:               32768 kB  Pss:               32768 kB  Shared_Dirty:          0 kB
[parent + 3 kids pid=187021]  Rss:               32768 kB  Pss:                8192 kB  Shared_Dirty:      32768 kB
[child pid=187022]  Rss:               32768 kB  Pss:                8192 kB  Shared_Dirty:      32768 kB
```

출력에서 볼 것: 4개 프로세스 모두 RSS는 32 MiB로 보이지만 PSS는 정확히 1/4인 8 MiB다. 혼자일 땐 같은 페이지가 `Private_Dirty`로 분류되다가(그래서 `Shared_Dirty`가 0), 공유가 시작되자 `Shared_Dirty`로 바뀌었다. 장비의 "메모리 예산표"를 만들 때는 PSS를 쓴다. 첫 시도에서는 측정 시점에 형제 프로세스 일부가 이미 끝나 자식의 PSS가 8901 kB로 나왔다. 동시에 살아 있게 sleep 순서를 조정했다.

### 9.3 smaps_rollup과 pmap

프로세스 전체 합계는 `/proc/<pid>/smaps_rollup` 한 파일로 본다.

```sh
cat /proc/self/smaps_rollup | head -12
```

```text
b2d418b10000-ffffc2d02000 ---p 00000000 00:00 0                          [rollup]
Rss:                6132 kB
Pss:                2277 kB
Pss_Dirty:          1284 kB
Pss_Anon:           1284 kB
Pss_File:            993 kB
Pss_Shmem:             0 kB
Shared_Clean:       4720 kB
Shared_Dirty:          0 kB
Private_Clean:       128 kB
Private_Dirty:      1284 kB
Referenced:         6132 kB
```

출력에서 볼 것: `cat` 하나의 RSS는 6.1 MB지만 PSS는 2.3 MB다. 차이는 대부분 `Shared_Clean`(libc 코드 같은 공유 파일 페이지)이다. `pmap -x <pid>`는 VMA별 RSS/Dirty를 표로 보여 준다.

```sh
sudo pmap -x $(pgrep -o systemd-journal) | tail -4
```

```text
0000f9fbd68c7000       4       4       4 rw---   [ anon ]
0000ffffd0abc000     132      20      20 rw---   [ stack ]
---------------- ------- ------- ------- 
total kB           35196   12296    3624
```

출력에서 볼 것: 열은 가상 크기(Kbytes), RSS, Dirty다. 스택은 132 KiB 가상 공간 중 20 KiB만 실제로 붙어 있다 — demand paging이 스택에도 그대로 적용된다.

### 9.4 /proc/meminfo 주요 필드

```sh
grep -E "^(MemTotal|MemFree|MemAvailable|Buffers|Cached|Dirty|Writeback|AnonPages|Mapped|Shmem|Mlocked|Committed_AS|CommitLimit|AnonHugePages|PageTables):" /proc/meminfo
```

```text
MemTotal:        3988828 kB
MemFree:         3176140 kB
MemAvailable:    3429056 kB
Buffers:            8792 kB
Cached:           379340 kB
Mlocked:           16752 kB
Dirty:               464 kB
Writeback:             0 kB
AnonPages:        216152 kB
Mapped:           108588 kB
Shmem:             11040 kB
PageTables:         5232 kB
CommitLimit:     1994412 kB
Committed_AS:     962364 kB
AnonHugePages:     90112 kB
```

| 필드 | 뜻 | 볼 때 주의 |
|---|---|---|
| MemFree | 아무도 안 쓰는 페이지 | 작아도 정상. Linux는 남는 RAM을 캐시로 쓴다 |
| **MemAvailable** | 스왑 없이 새 앱에 줄 수 있는 추정치 (Free + 회수 가능한 캐시) | **"메모리 남았나?"는 이 값으로 판단** |
| Cached | page cache (파일 내용) + Shmem | 대부분 회수 가능. 단 Shmem(tmpfs, `/dev/shm`)은 회수 불가 |
| Dirty / Writeback | 디스크에 아직 안 쓴 / 지금 쓰는 중인 페이지 | 갑작스런 전원 차단 시 잃을 수 있는 양 |
| AnonPages | 익명 페이지 (heap, stack, 익명 mmap) | 누수 감시 대상 |
| Mapped | 프로세스가 mmap한 파일 페이지 | 실행 파일·라이브러리 포함 |
| Mlocked | mlock된 페이지 | 회수 불가 |
| Committed_AS | 지금까지 약속한 총량 | RAM을 크게 넘으면 OOM 위험 신호 |
| AnonHugePages | THP로 붙은 익명 메모리 | 10장 |

`free`의 `available` 열이 MemAvailable이다. 0장 `free -m` 출력에서 `free`는 1454 MB인데 `available`은 3362 MB였던 이유가 Cached다.

---

## 10. Huge page 간단히

TLB 엔트리 수는 정해져 있다. 4 KiB 페이지로 1 GiB를 덮으려면 26만 개 엔트리가 필요하지만 2 MiB 페이지면 512개다. 큰 버퍼를 다루는 프로그램(카메라 프레임 풀, NPU 텐서 버퍼)은 huge page로 TLB miss와 fault 수를 줄인다. 두 방식이 있다.

| | THP (Transparent Huge Pages) | hugetlbfs (`MAP_HUGETLB`) |
|---|---|---|
| 준비 | 자동. `/sys/kernel/mm/transparent_hugepage/enabled` | 미리 풀 예약: `vm.nr_hugepages` |
| 요청 | `madvise(MADV_HUGEPAGE)` (모드가 madvise일 때) | `mmap(..., MAP_HUGETLB)` 또는 hugetlbfs 파일 |
| 보장 | 없음. 연속 물리 메모리가 없으면 4K로 대체 | 예약분 안에서 보장. 없으면 mmap 실패 |
| 단점 | compaction 지연, 메모리 낭비 가능 | 미리 잡아 둔 만큼 다른 용도로 못 씀 |

이 VM은 `[madvise]` 모드다(0장 확인). 3장 예제 2의 `thp` 행이 THP 효과다.

### 예제 15: MAP_HUGETLB

```c
// ex15: MAP_HUGETLB — 미리 예약한 2 MiB hugetlb 풀에서 페이지를 받는다
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>

int main(void)
{
    size_t len = 4UL << 20;                              // 2 MiB x 2
    char *p = mmap(NULL, len, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (p == MAP_FAILED) { perror("mmap(MAP_HUGETLB)"); return 1; }
    struct rusage a, b;
    getrusage(RUSAGE_SELF, &a);
    memset(p, 1, len);
    getrusage(RUSAGE_SELF, &b);
    printf("hugetlb %p: 4 MiB touched with %ld faults (2 MiB align: %s)\n",
           (void *)p, b.ru_minflt - a.ru_minflt,
           ((unsigned long)p & ((2UL << 20) - 1)) ? "no" : "yes");
    munmap(p, len);
    return 0;
}
```

```sh
./ex15; sudo sysctl -w vm.nr_hugepages=4 && ./ex15; grep -E "^HugePages_(Total|Free)" /proc/meminfo; sudo sysctl -w vm.nr_hugepages=0
```

```text
mmap(MAP_HUGETLB): Cannot allocate memory
vm.nr_hugepages = 4
hugetlb 0xfa95e6000000: 4 MiB touched with 2 faults (2 MiB align: yes)
HugePages_Total:       4
HugePages_Free:        4
vm.nr_hugepages = 0
```

출력에서 볼 것: 풀이 0이면 `ENOMEM`으로 바로 실패한다(THP처럼 조용히 4K로 대체하지 않는다). 풀을 4개 예약하자 4 MiB를 fault 2번으로 붙였고 주소가 2 MiB 정렬이다. 실행이 끝난 뒤라 Free가 다시 4다. 실험 후 풀은 0으로 되돌렸다.

---

## 11. read() vs mmap — 언제 무엇이 빠른가

### 11.1 직관

`read()`는 page cache → 유저 버퍼로 **CPU가 복사**한다(커널의 `copy_to_user`). `mmap`은 page cache 페이지를 내 주소 공간에 **직접 매핑**하므로 복사가 없다. 대신 page fault와 page table 관리 비용, 그리고 `munmap` 때 TLB flush 비용이 있다. "mmap이 무조건 빠르다"는 틀렸고, 측정해야 한다.

### 11.2 예제 13: 256 MiB 파일을 읽어 합계 내기

```c
// ex13: page cache에 있는 파일을 read()로 복사해 읽기 vs mmap으로 직접 읽기
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}

static uint64_t sum(const uint64_t *w, size_t n)
{
    uint64_t s = 0;
    for (size_t i = 0; i < n; i++) s += w[i];
    return s;
}

static long minflt(void)
{
    struct rusage ru; getrusage(RUSAGE_SELF, &ru); return ru.ru_minflt;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s file read<bufKiB>/mmap/populate\n", argv[0]); exit(1); }
    int fd = open(argv[1], O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) < 0) { perror("open"); exit(1); }
    uint64_t s = 0;
    long f0 = minflt();
    double t0 = now_s();
    if (argv[2][0] == 'r') {                         // "read64" -> 64 KiB 버퍼
        size_t bs = (size_t)atol(argv[2] + 4) << 10;
        uint64_t *buf = aligned_alloc(4096, bs);
        ssize_t n;
        if (!buf) { perror("alloc"); exit(1); }
        while ((n = read(fd, buf, bs)) > 0) s += sum(buf, n / 8);
        if (n < 0) { perror("read"); exit(1); }
    } else {
        int fl = MAP_PRIVATE | (argv[2][0] == 'p' ? MAP_POPULATE : 0);
        const uint64_t *m = mmap(NULL, st.st_size, PROT_READ, fl, fd, 0);
        if (m == MAP_FAILED) { perror("mmap"); exit(1); }
        s = sum(m, st.st_size / 8);
        munmap((void *)m, st.st_size);
    }
    double dt = now_s() - t0;
    printf("%-9s %7.2f GB/s  %6.1f ms  minflt=%ld  (sum=%llx)\n", argv[2],
           st.st_size / dt / 1e9, dt * 1e3, minflt() - f0, (unsigned long long)s);
    return 0;
}
```

```sh
dd if=/dev/urandom of=/var/tmp/A5/big.bin bs=1M count=256 status=none
cat /var/tmp/A5/big.bin > /dev/null    # page cache에 올려 둠
for r in 1 2 3; do for m in read4 read64 read1024 mmap populate; do ./ex13 /var/tmp/A5/big.bin $m; done; echo; done
```

```text
read4        8.21 GB/s    32.7 ms  minflt=7  (sum=7e4a295426aeddeb)
read64      13.68 GB/s    19.6 ms  minflt=22  (sum=7e4a295426aeddeb)
read1024    13.61 GB/s    19.7 ms  minflt=261  (sum=7e4a295426aeddeb)
mmap        19.40 GB/s    13.8 ms  minflt=273  (sum=7e4a295426aeddeb)
populate    16.82 GB/s    16.0 ms  minflt=273  (sum=7e4a295426aeddeb)

read4        8.18 GB/s    32.8 ms  minflt=7  (sum=7e4a295426aeddeb)
read64      13.91 GB/s    19.3 ms  minflt=22  (sum=7e4a295426aeddeb)
read1024    13.71 GB/s    19.6 ms  minflt=261  (sum=7e4a295426aeddeb)
mmap        19.68 GB/s    13.6 ms  minflt=273  (sum=7e4a295426aeddeb)
populate    17.22 GB/s    15.6 ms  minflt=273  (sum=7e4a295426aeddeb)

read4        8.20 GB/s    32.7 ms  minflt=7  (sum=7e4a295426aeddeb)
read64      13.88 GB/s    19.3 ms  minflt=22  (sum=7e4a295426aeddeb)
read1024     4.56 GB/s    58.9 ms  minflt=261  (sum=7e4a295426aeddeb)
mmap        18.80 GB/s    14.3 ms  minflt=273  (sum=7e4a295426aeddeb)
populate    17.10 GB/s    15.6 ms  minflt=273  (sum=7e4a295426aeddeb)
```

```svg
<svg viewBox="0 0 640 320" xmlns="http://www.w3.org/2000/svg">
<text x="140" y="42" font-size="13" text-anchor="end">read 4K buf</text>
<rect x="150" y="20" width="164.0" height="34" fill="#888" fill-opacity="0.7" stroke="#888"/><text x="322.0" y="42" font-size="13">8.20 GB/s</text><text x="140" y="92" font-size="13" text-anchor="end">read 64K buf</text>
<rect x="150" y="70" width="277.6" height="34" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="435.6" y="92" font-size="13">13.88 GB/s</text><text x="140" y="142" font-size="13" text-anchor="end">read 1M buf</text>
<rect x="150" y="120" width="272.2" height="34" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="430.2" y="142" font-size="13">13.61 GB/s</text><text x="140" y="192" font-size="13" text-anchor="end">mmap</text>
<rect x="150" y="170" width="388.0" height="34" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><text x="546.0" y="192" font-size="13">19.40 GB/s</text><text x="140" y="242" font-size="13" text-anchor="end">mmap+POPULATE</text>
<rect x="150" y="220" width="342.0" height="34" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/><text x="500.0" y="242" font-size="13">17.10 GB/s</text>
<line x1="150" y1="15" x2="150" y2="270" stroke="currentColor"/>
<line x1="150" y1="270" x2="550" y2="270" stroke="currentColor"/>
<line x1="150" y1="270" x2="150" y2="274" stroke="currentColor"/><text x="150" y="288" font-size="12" text-anchor="middle">0</text>
<line x1="250" y1="270" x2="250" y2="274" stroke="currentColor"/><text x="250" y="288" font-size="12" text-anchor="middle">5</text>
<line x1="350" y1="270" x2="350" y2="274" stroke="currentColor"/><text x="350" y="288" font-size="12" text-anchor="middle">10</text>
<line x1="450" y1="270" x2="450" y2="274" stroke="currentColor"/><text x="450" y="288" font-size="12" text-anchor="middle">15</text>
<line x1="550" y1="270" x2="550" y2="274" stroke="currentColor"/><text x="550" y="288" font-size="12" text-anchor="middle">20</text><text x="350" y="308" font-size="13" text-anchor="middle">처리량 GB/s (256 MiB, page cache warm, 3회 중앙값)</text>
</svg>
```

그림 6 — 예제 13의 3회 중앙값. warm cache에서 순차로 한 번 훑는 경우 mmap이 가장 빠르다. read는 버퍼가 작으면 syscall 횟수(4K 버퍼면 65536번)가 비용을 지배한다.

출력에서 볼 것:

- `read4`(4 KiB 버퍼)는 syscall이 65536번이라 가장 느리다. 64 KiB부터는 syscall 비용이 묻혀 13.9 GB/s에서 평평해진다. 남은 차이는 **복사 비용**이다.
- `mmap`은 65536 페이지를 fault **273번**으로 매핑했다. fault-around와 page cache의 큰 folio 덕분이다. 복사가 없어 약 40% 빠르다.
- `populate`가 오히려 조금 느린 건, 미리 전부 매핑하는 동안 캐시가 식고 그 뒤 다시 데이터를 읽기 때문으로 보인다(측정 결과만 확실하다).
- `read1024`의 세 번째 4.56 GB/s는 이상치다(1 MiB 버퍼를 새로 받는 fault 261번 + 그 순간 VM 호스트 간섭으로 추정). 그래서 3회 이상 돌려 중앙값을 쓴다.

언제 무엇을:

| 상황 | 추천 | 이유 |
|---|---|---|
| 큰 파일을 여러 번, 무작위로 접근 (DB, 지도 타일, 모델 가중치) | mmap | 복사 없음, 필요한 페이지만 로드, 여러 프로세스가 page cache 공유 |
| 스트리밍으로 한 번 읽고 버림 (로그, 녹화 파일) | read (64 KiB 이상 버퍼) | 단순, 에러가 리턴값으로 옴 |
| 파이프·소켓·문자 장치 | read | mmap 불가 (드라이버가 `.mmap`을 안 주면 `ENODEV`) |
| 장치 DMA 버퍼 (카메라, FPGA) | mmap | zero-copy (다음 장) |
| 파일이 잘릴 수 있음 | read | mmap 영역 중 파일 끝 넘어 페이지 접근 시 **SIGBUS** |

mmap의 I/O 에러는 리턴값이 아니라 **SIGBUS**로 온다. SD 카드가 빠진 상태에서 mmap한 파일을 읽으면 프로세스가 시그널로 죽는다. 이게 임베디드에서 mmap을 조심하는 이유 중 하나다.

---

## 12. 장치 메모리 mmap과 zero-copy — 카메라, /dev/mem, UIO

### 12.1 직관: DMA 버퍼에 창문 내기

Don이 SSD 펌웨어에서 호스트 메모리로 DMA할 때, 데이터는 컨트롤러가 직접 쓰고 CPU는 손대지 않았다. Linux 카메라도 같다. ISP의 DMA가 프레임을 RAM의 버퍼에 쓴다. 문제는 그 버퍼가 **커널이 할당한 물리 메모리**라는 점이다. 유저 앱이 프레임을 받는 방법은 두 가지다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="20" y="24" font-size="14">read() 방식: 프레임마다 CPU가 복사</text>
<rect x="20" y="40" width="140" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="90" y="67" font-size="13" text-anchor="middle">카메라 ISP DMA</text>
<rect x="230" y="40" width="150" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="305" y="67" font-size="13" text-anchor="middle">커널 DMA 버퍼</text>
<rect x="500" y="40" width="150" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="575" y="67" font-size="13" text-anchor="middle">유저 버퍼</text>
<line x1="160" y1="62" x2="228" y2="62" stroke="currentColor" marker-end="url(#ah4)"/><text x="194" y="54" font-size="12" text-anchor="middle">DMA</text>
<line x1="380" y1="62" x2="498" y2="62" stroke="#d0564a" stroke-width="2" marker-end="url(#ah4)"/><text x="440" y="100" font-size="12" text-anchor="middle">copy_to_user</text><text x="440" y="116" font-size="12" text-anchor="middle">(1080p 약 3 MB/프레임)</text>
<line x1="20" y1="140" x2="660" y2="140" stroke="#888" stroke-dasharray="4 4"/><text x="20" y="166" font-size="14">mmap 방식 (V4L2 MMAP): 같은 물리 페이지를 유저도 매핑</text>
<rect x="20" y="185" width="140" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="90" y="212" font-size="13" text-anchor="middle">카메라 ISP DMA</text>
<rect x="230" y="185" width="170" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="315" y="212" font-size="13" text-anchor="middle">DMA 버퍼 (물리 RAM)</text>
<rect x="500" y="185" width="150" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="575" y="212" font-size="13" text-anchor="middle">유저 VA 매핑</text>
<line x1="160" y1="207" x2="228" y2="207" stroke="currentColor" marker-end="url(#ah4)"/><text x="194" y="199" font-size="12" text-anchor="middle">DMA</text>
<line x1="498" y1="207" x2="402" y2="207" stroke="#3f9a6b" stroke-width="2" stroke-dasharray="6 3" marker-end="url(#ah4)"/><text x="450" y="248" font-size="12" text-anchor="middle">page table만 연결</text><text x="450" y="264" font-size="12" text-anchor="middle">(복사 0번)</text>
<rect x="20" y="282" width="640" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="340" y="305" font-size="13" text-anchor="middle">유저와 드라이버는 ioctl(VIDIOC_QBUF / VIDIOC_DQBUF)로 "버퍼 번호"만 주고받는다</text>
</svg>
```

그림 7 — 위: read() 방식은 프레임마다 CPU가 복사한다. 아래: mmap 방식은 드라이버가 DMA 버퍼의 물리 페이지를 유저 page table에 연결해서 복사가 없다.

### 12.2 V4L2 MMAP 흐름 (코드 스케치)

이 VM에는 카메라(`/dev/video*`)가 없어서 아래 코드는 **실행하지 않은 스케치**다. 흐름만 본다(자세한 건 I5, Q2).

```c
/* 실행하지 않은 스케치: V4L2 MMAP 스트리밍의 뼈대 */
struct v4l2_requestbuffers req = { .count = 4,
    .type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
ioctl(fd, VIDIOC_REQBUFS, &req);              /* 드라이버가 DMA 버퍼 4개 할당 */

for (unsigned i = 0; i < req.count; i++) {
    struct v4l2_buffer b = { .type = req.type, .memory = V4L2_MEMORY_MMAP, .index = i };
    ioctl(fd, VIDIOC_QUERYBUF, &b);           /* 버퍼 i의 mmap offset·길이 */
    buf[i] = mmap(NULL, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
    ioctl(fd, VIDIOC_QBUF, &b);               /* "여기에 채워 줘" */
}
ioctl(fd, VIDIOC_STREAMON, &req.type);
for (;;) {
    poll(&pfd, 1, -1);                        /* A4: 프레임 준비되면 깨어남 */
    struct v4l2_buffer b = { .type = req.type, .memory = V4L2_MEMORY_MMAP };
    ioctl(fd, VIDIOC_DQBUF, &b);              /* 다 찬 버퍼 번호 b.index */
    process(buf[b.index], b.bytesused);       /* 복사 없이 바로 처리 */
    ioctl(fd, VIDIOC_QBUF, &b);               /* 다시 드라이버에 돌려줌 */
}
```

여기서 `b.m.offset`은 파일 offset이 아니라 **"몇 번 버퍼인지"를 알려 주는 쿠키**다. 드라이버(videobuf2)의 `.mmap`이 이 값을 보고 어느 버퍼를 매핑할지 고른다. 같은 프레임을 GPU·NPU·인코더로 넘길 때는 `VIDIOC_EXPBUF`로 **dma-buf fd**를 받아 넘긴다(H9). 이것이 Jetson·i.MX8 카메라 파이프라인의 zero-copy 표준 경로다.

### 12.3 예제 14: /dev/mem으로 MMIO 레지스터 읽기

`/dev/mem`은 "파일 offset = 물리 주소"인 특수 장치다. `mmap`하면 그 물리 주소가 내 VA에 매핑된다. bring-up 때 드라이버 없이 레지스터를 보는 도구(`devmem2`, `busybox devmem`)가 이걸 쓴다. VM의 `/proc/iomem`에서 ARM PL061 GPIO 컨트롤러(`ARMH0061`)를 찾아 **읽기 전용**으로 ID 레지스터만 읽었다.

```sh
sudo cat /proc/iomem | head -4
grep -E "STRICT_DEVMEM" /boot/config-$(uname -r)
```

```text
10000000-1000ffff : GICD
10010000-1008ffff : GICR
20060000-20060fff : ARMH0061:00
  20060000-20060fff : ARMH0061:00 ARMH0061:00
CONFIG_STRICT_DEVMEM=y
# CONFIG_IO_STRICT_DEVMEM is not set
```

```c
// ex14: /dev/mem mmap으로 MMIO 레지스터 읽기 (PL061 GPIO의 ID 레지스터, 읽기 전용)
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>

static void *map_phys(int fd, off_t phys, size_t len)
{
    void *v = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, phys);  // offset = 물리 주소
    if (v == MAP_FAILED) {
        printf("mmap phys 0x%llx -> ", (unsigned long long)phys);
        fflush(stdout);
        perror("");
        return NULL;
    }
    return v;
}

int main(void)
{
    int fd = open("/dev/mem", O_RDONLY | O_SYNC);   // O_SYNC: uncached 매핑 요청
    if (fd < 0) { perror("open /dev/mem"); exit(1); }

    // /proc/iomem: 20060000-20060fff : ARMH0061:00  (ARM PL061 GPIO)
    volatile const uint32_t *r = map_phys(fd, 0x20060000, 4096);
    if (r) {
        printf("PL061 @ phys 0x20060000 -> user VA %p\n", (const void *)r);
        printf("PeriphID0..3 :");
        for (int off = 0xFE0; off <= 0xFEC; off += 4)
            printf(" 0x%02x", r[off / 4] & 0xFF);
        printf("\nPCellID0..3  :");
        for (int off = 0xFF0; off <= 0xFFC; off += 4)
            printf(" 0x%02x", r[off / 4] & 0xFF);
        printf("\n");
        munmap((void *)r, 4096);
    }
    // System RAM 영역은 CONFIG_STRICT_DEVMEM이 막는다
    map_phys(fd, 0x80000000, 4096);
    close(fd);
    return 0;
}
```

```sh
./ex14; sudo ./ex14
```

```text
open /dev/mem: Permission denied
PL061 @ phys 0x20060000 -> user VA 0xfc6edda3e000
PeriphID0..3 : 0x61 0x10 0x04 0x00
PCellID0..3  : 0x0d 0xf0 0x05 0xb1
mmap phys 0x80000000 -> Operation not permitted
```

출력에서 볼 것:

- 일반 유저는 open부터 거절된다(`CAP_SYS_RAWIO` 필요).
- PeriphID `0x61 0x10 0x04 0x00` = 부품 번호 0x061(PL061), 설계자 코드 0x41(ARM; PeriphID1 상위 4비트와 PeriphID2 하위 4비트를 합친 값), revision 0. PrimeCell ID `0x0d 0xf0 0x05 0xb1`은 모든 ARM PrimeCell 주변장치의 고정 서명이다. TRM에서 보던 값을 유저 프로그램이 MMU를 거쳐 읽은 것이다.
- 물리 `0x80000000`은 `/proc/iomem`상 System RAM이라 `CONFIG_STRICT_DEVMEM`이 `EPERM`으로 막았다. 커널이 쓰는 RAM을 유저가 망치지 못하게 하는 장치다. `CONFIG_IO_STRICT_DEVMEM`까지 켜면 **드라이버가 이미 잡은 MMIO 영역**도 막힌다. 이 VM은 꺼져 있어서 PL061(드라이버가 잡고 있음)을 읽을 수 있었다.
- 읽기만 한 이유: 드라이버가 쓰고 있는 장치의 레지스터에 유저가 쓰면 드라이버 상태와 HW가 어긋난다. `/dev/mem`이 "bring-up 도구로는 OK, 제품 코드로는 금지"인 이유다(H1).

### 12.4 UIO — /dev/mem의 정식 버전

**UIO(Userspace I/O)** 는 작은 커널 드라이버(예: `uio_pdrv_genirq`, Device Tree로 붙임)가 레지스터 영역과 IRQ만 유저에게 넘겨주는 프레임워크다. 이 VM 커널에도 모듈로 있다(`CONFIG_UIO=m`, `CONFIG_UIO_PDRV_GENIRQ=m`). 다만 이 VM엔 붙일 장치 노드가 없어 아래는 실행하지 않은 스케치다.

```c
/* 실행하지 않은 스케치: UIO로 FPGA 레지스터 + 인터럽트 다루기 */
int fd = open("/dev/uio0", O_RDWR | O_SYNC);
/* map N은 offset = N * pagesize 로 고른다. 크기는 /sys/class/uio/uio0/maps/map0/size */
volatile uint32_t *regs = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0 * getpagesize());
uint32_t on = 1, count;
for (;;) {
    write(fd, &on, 4);              /* IRQ 다시 켜기 (irqcontrol) */
    read(fd, &count, 4);            /* 인터럽트가 올 때까지 block, 누적 횟수 반환 */
    uint32_t st = regs[0x10 / 4];   /* 상태 레지스터 읽고 처리 */
    regs[0x14 / 4] = st;            /* W1C 클리어 */
}
```

Don에게 익숙한 구조다: "레지스터 포인터 + IRQ 대기 루프". 장점은 커널 드라이버 없이 빠르게 bring-up할 수 있다는 것, 단점은 DMA·전원 관리·여러 프로세스 공유가 어렵고 IRQ 지연이 커널 드라이버보다 크다는 것이다.

### 12.5 캐시 속성 — MMIO를 캐시된 메모리로 매핑하면 안 되는 이유

Don이 bare-metal에서 MPU 영역 속성으로 "Device / Strongly-ordered / Normal-NC / Normal WB"를 고르던 것과 같은 문제가 Linux `mmap`에도 있다. 드라이버(또는 `/dev/mem`)가 매핑할 때 `vma->vm_page_prot`에 속성을 정하고, 그게 PTE의 AttrIndx로 들어간다.

| 커널 헬퍼 | arm64 메모리 타입 | 쓰는 곳 |
|---|---|---|
| (기본) | Normal, Write-Back cacheable | 일반 RAM, page cache |
| `pgprot_writecombine()` | Normal Non-Cacheable | 프레임버퍼, 큰 DMA 버퍼에 순차 쓰기 (쓰기를 모아서 버스로) |
| `pgprot_device()` | Device-nGnRE | MMIO 레지스터 |
| `pgprot_noncached()` | Device-nGnRnE | MMIO (가장 엄격, 순서·완료 보장) |

- MMIO를 Normal-cacheable로 매핑하면 읽기가 캐시에서 나와 상태 레지스터 변화를 못 보고, 쓰기가 캐시에 머물러 장치에 안 간다. speculative 읽기로 read-to-clear 레지스터가 지워질 수도 있다.
- arm64의 `/dev/mem`은 RAM이 아닌 주소를 매핑하면 항상 `pgprot_noncached`(Device-nGnRnE)를 쓴다. `O_SYNC`는 RAM 영역일 때 의미가 있다(이 경우 write-combine). 다른 아키텍처는 다르게 동작하므로 습관적으로 `O_SYNC`를 붙인다.
- 카메라 DMA 버퍼를 유저가 CPU로 읽을 때, 버퍼가 cacheable이면 DMA가 쓴 뒤 **캐시 invalidate**가 필요하다(하드웨어 coherency가 없다면). `dma_alloc_coherent` 버퍼는 보통 non-cacheable(또는 HW coherent)로 매핑돼서 이 문제가 없지만, CPU가 읽기에 느리다. 그래서 V4L2/dma-buf에는 `DMA_BUF_IOCTL_SYNC` 같은 캐시 동기화 ioctl이 있다(H9). SSD 펌웨어에서 DMA 전후로 D-cache clean/invalidate하던 그것이다.

### 12.6 드라이버 쪽 `.mmap` 미리보기 (G5, H9)

유저의 `mmap(fd)`는 드라이버의 `file_operations.mmap`을 부른다. 드라이버가 하는 일은 "이 VMA의 PTE를 내 메모리로 채우는 것"이다. 아래는 커널 코드라 이 노트에선 실행하지 않았다.

```c
/* 실행하지 않은 커널 코드 스케치 (G5에서 직접 빌드) */
static int mydev_mmap(struct file *f, struct vm_area_struct *vma)
{
    struct mydev *d = f->private_data;
    unsigned long len = vma->vm_end - vma->vm_start;

    if (len > d->reg_size)
        return -EINVAL;
    /* (1) MMIO 레지스터: 물리 주소의 PFN을 PTE로 바로 채운다 */
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
    return remap_pfn_range(vma, vma->vm_start, d->reg_phys >> PAGE_SHIFT,
                           len, vma->vm_page_prot);
}

static int mybuf_mmap(struct file *f, struct vm_area_struct *vma)
{
    struct mydev *d = f->private_data;
    /* (2) dma_alloc_coherent()로 받은 버퍼: 아키텍처에 맞는 속성으로 매핑 */
    return dma_mmap_coherent(d->dev, vma, d->buf_cpu, d->buf_dma,
                             vma->vm_end - vma->vm_start);
}
```

- `remap_pfn_range`는 `VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP` 플래그를 VMA에 세운다. "struct page가 없는(또는 관리하지 않는) 물리 메모리이니 fork/core dump/swap 때 건드리지 말라"는 뜻이다. MMIO에는 `io_remap_pfn_range`, 간단한 경우 `vm_iomap_memory`도 있다.
- `dma_mmap_coherent`는 `dma_alloc_coherent`가 고른 캐시 속성과 같은 속성으로 매핑해 준다. 직접 `remap_pfn_range`로 DMA 버퍼를 매핑하면 커널 쪽 매핑과 유저 쪽 매핑의 캐시 속성이 달라지는(aliasing) 버그가 생길 수 있다.
- 다른 방식은 `.mmap`에서 PTE를 안 채우고 `vma->vm_ops->fault`를 등록해 두는 것이다. 유저가 접근할 때 page fault 경로에서 페이지를 하나씩 넘긴다(lazy). videobuf2와 dma-buf exporter 상당수가 내부적으로 이런 헬퍼들을 쓴다.

---

## 13. 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="170" y="22" font-size="14" text-anchor="middle">mmap() 호출 시</text>
<text x="510" y="22" font-size="14" text-anchor="middle">처음 접근할 때 (page fault)</text>
<rect x="20" y="40" width="300" height="38" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170" y="64" font-size="12" text-anchor="middle">유저: mmap(addr, len, prot, flags, fd)</text>
<rect x="20" y="100" width="300" height="38" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="170" y="124" font-size="13" text-anchor="middle">ksys_mmap_pgoff → do_mmap</text>
<rect x="20" y="160" width="300" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="170" y="182" font-size="13" text-anchor="middle">vm_area_struct 생성 · mm 트리에 삽입</text><text x="170" y="200" font-size="12" text-anchor="middle">vm_start · vm_end · vm_flags · vm_ops</text>
<rect x="20" y="232" width="300" height="38" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="170" y="256" font-size="13" text-anchor="middle">파일이면 file-&gt;f_op-&gt;mmap(file, vma)</text>
<rect x="20" y="292" width="140" height="56" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="90" y="315" font-size="12" text-anchor="middle">remap_pfn_range</text><text x="90" y="333" font-size="12" text-anchor="middle">PTE 즉시 채움</text>
<rect x="180" y="292" width="140" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="250" y="315" font-size="12" text-anchor="middle">vm_ops-&gt;fault 등록</text><text x="250" y="333" font-size="12" text-anchor="middle">나중에 채움</text>
<line x1="170" y1="78" x2="170" y2="98" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="170" y1="138" x2="170" y2="158" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="170" y1="210" x2="170" y2="230" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="120" y1="270" x2="95" y2="290" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="220" y1="270" x2="245" y2="290" stroke="currentColor" marker-end="url(#ah5)"/><text x="170" y="378" font-size="12" text-anchor="middle">익명 매핑은 VMA만 만들고 끝 (PTE 없음)</text>
<line x1="340" y1="35" x2="340" y2="390" stroke="#888" stroke-dasharray="4 4"/>
<rect x="360" y="40" width="300" height="38" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="510" y="64" font-size="13" text-anchor="middle">load/store → MMU translation fault</text>
<rect x="360" y="100" width="300" height="38" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="510" y="124" font-size="13" text-anchor="middle">do_mem_abort → do_page_fault (arm64)</text>
<rect x="360" y="160" width="300" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="510" y="182" font-size="13" text-anchor="middle">VMA 찾기 → 권한 검사 → handle_mm_fault</text><text x="510" y="200" font-size="12" text-anchor="middle">VMA 없음/권한 위반 → SIGSEGV</text>
<rect x="360" y="232" width="300" height="74" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="510" y="254" font-size="12" text-anchor="middle">익명 첫 접근: do_anonymous_page</text><text x="510" y="272" font-size="12" text-anchor="middle">파일: vm_ops-&gt;fault (filemap_fault 등)</text><text x="510" y="290" font-size="12" text-anchor="middle">COW 쓰기: do_wp_page</text>
<rect x="360" y="326" width="300" height="38" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="510" y="350" font-size="13" text-anchor="middle">PTE 설치 → 예외 복귀 → 명령 재실행</text>
<line x1="510" y1="78" x2="510" y2="98" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="510" y1="138" x2="510" y2="158" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="510" y1="210" x2="510" y2="230" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="510" y1="306" x2="510" y2="324" stroke="currentColor" marker-end="url(#ah5)"/>
</svg>
```

그림 8 — 왼쪽: `mmap()`은 VMA(약속)만 만들고, 장치 파일이면 드라이버의 `.mmap`이 PTE를 채울지 결정한다. 오른쪽: 처음 접근하면 arm64 abort 핸들러에서 page fault 경로로 들어가 PTE를 채운다.

API와 커널 객체의 대응을 정리한다.

| 유저가 보는 것 | 커널 객체·함수 | 드라이버에서 다시 만나는 곳 |
|---|---|---|
| 프로세스의 주소 공간 전체 | `struct mm_struct` (`task_struct->mm`), page table 루트(pgd) | 커널 스레드는 `mm`이 없다 (F2) |
| `/proc/<pid>/maps`의 한 줄 | `struct vm_area_struct` (VMA). 6.1부터 maple tree로 관리 | `.mmap(file, vma)`의 인자 (G5) |
| `mmap()` | `ksys_mmap_pgoff()` → `vm_mmap_pgoff()` → `do_mmap()` → `mmap_region()` | `file_operations.mmap` 호출 지점 |
| page fault | arm64 `do_mem_abort()` → `do_page_fault()` → `handle_mm_fault()` | `vm_operations_struct.fault` |
| 파일 페이지 | page cache (`address_space`, folio) | 파일시스템·블록 드라이버 (J) |
| MMIO/DMA 매핑 | `remap_pfn_range()`, `io_remap_pfn_range()`, `dma_mmap_coherent()` | H1, H9 |
| `mlock` | VMA의 `VM_LOCKED`, `Mlocked` 카운터 | 커널 쪽 대응 없음 (커널 메모리는 원래 swap 안 됨, F9) |
| 커널 → 유저 복사 | `copy_to_user()` (page fault를 낼 수 있음) | `.read` 구현 (G4 계열). 그래서 spinlock 잡고 호출 금지 (F2, F4) |

드라이버를 쓸 때 핵심 두 가지:

1. **`copy_to_user`/`copy_from_user`는 잠들 수 있다.** 유저 버퍼가 아직 안 붙은 페이지면 page fault가 나고, major fault면 디스크 I/O를 기다린다. 그래서 atomic context(spinlock, IRQ 핸들러)에서 부르면 안 된다. 3장의 demand paging이 이 규칙의 이유다.
2. **커널 메모리는 demand paging이 없다.** `kmalloc`은 즉시 물리 메모리를 주고(F9), 커널 코드가 잘못된 주소를 만지면 "page fault 처리"가 아니라 **Oops**다. 유저의 SIGSEGV에 해당하는 것이 커널에선 Oops/panic이다(M 모듈).

---

## 14. Embedded 실무에서 다시 보기

### 14.1 카메라 4대를 돌리는 로봇: 메모리 예산

예를 들어 센서 키트에 1080p NV12 카메라 4대, 각각 버퍼 4개라면 3.1 MB × 4 × 4 ≈ 50 MB가 DMA 버퍼로 묶인다. 이건 `MemAvailable`에서 빠지고 OOM 계산에도 들어간다. 이 버퍼가 CMA(연속 물리 메모리 예약 영역)에서 나오는지, IOMMU로 흩어진 페이지를 모아 쓰는지는 SoC마다 다르다(H9, P 모듈). 프레임을 NPU로 넘길 때 `memcpy`가 하나라도 끼면 30 fps × 4대 × 3 MB = 초당 360 MB 복사다. "zero-copy 경로가 끊긴 곳 찾기"가 실무 성능 튜닝의 단골 과제다.

### 14.2 1 kHz 모터 제어 루프의 지터

`cyclictest`(N 모듈)로 측정한 최대 지연이 가끔 수백 µs로 튀면 첫 용의자는 page fault다. 확인 방법:

```sh
# 제어 프로세스의 fault 카운터가 루프 동안 증가하는지 본다 (10, 12번째 필드)
watch -n1 "awk '{print \"minflt=\"\$10, \"majflt=\"\$12}' /proc/$(pidof motor_ctl)/stat"
```

루프가 도는 동안 숫자가 늘면 8장의 관례(mlockall + prefault + 루프 안 할당 금지)를 적용한다. `perf record -e page-faults -g -p <pid>`로 어느 코드에서 fault가 나는지도 볼 수 있다(M6).

### 14.3 "메모리 누수 같아요"

장비가 며칠 돌면 죽는다는 보고. 순서:

1. `journalctl -k`에서 `Out of memory` 줄과 죽은 프로세스 확인.
2. 의심 프로세스의 `/proc/<pid>/smaps_rollup`의 `Pss_Anon`을 1시간 간격으로 기록. 계속 늘면 누수.
3. `/proc/<pid>/maps`에서 어느 VMA가 커지는지 비교(`[heap]`인지, 이름 없는 익명 매핑 개수가 느는지 — 후자는 스레드 arena나 mmap 누수).
4. 시스템 전체라면 `/proc/meminfo`의 `AnonPages`, `Shmem`(tmpfs에 로그가 쌓이는 경우가 많다!), `Slab`(커널 쪽 누수, 드라이버 의심).

### 14.4 설정 파일을 mmap으로 쓰다 전원 차단

드론 비행 로그를 SHARED mmap 링버퍼로 쓰는 설계를 봤다면 질문할 것: "msync는 언제 하나? 전원 차단 시 최대 몇 초 손실을 허용하나?" 예제 12에서 본 대로 Dirty 페이지는 msync 전까지 RAM에만 있다. 파일시스템의 저널링(J 모듈)은 메타데이터를 지켜 줄 뿐, 내 데이터가 디스크에 갔다는 보장이 아니다.

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `mmap` 실패를 `NULL`과 비교 | 실패해도 계속 진행하다 SIGSEGV | 실패값은 `MAP_FAILED`(-1) | `if (p == MAP_FAILED)` |
| offset을 페이지 배수가 아니게 줌 | `EINVAL` | mmap은 페이지 단위 | offset을 내림 정렬하고 포인터에 나머지를 더함 |
| 파일 크기보다 큰 범위를 매핑하고 접근 | **SIGBUS** | 파일 끝 넘어 페이지엔 데이터가 없음 | `ftruncate`로 먼저 늘리기 |
| SHARED 매핑에 쓰고 msync 안 함 | 전원 차단 후 데이터 손실 | Dirty 페이지가 RAM에만 있음 | 중요 지점에서 `msync(MS_SYNC)` |
| 공유 메모리에 포인터 저장 | 다른 프로세스에서 쓰레기 값 | 프로세스마다 매핑 주소가 다름 | 베이스 기준 오프셋 저장 |
| RT 앱에서 mlockall 안 함 | 가끔 수백 µs 지터 | 첫 접근 page fault, reclaim | `mlockall` + 스택·버퍼 prefault |
| MCL_FUTURE 후 큰 malloc (일반 유저) | 장비에서만 malloc이 NULL | `RLIMIT_MEMLOCK` 초과 → mmap `EAGAIN` | `LimitMEMLOCK=infinity` 또는 `CAP_IPC_LOCK` |
| RSS 합으로 메모리 예산 계산 | 합이 RAM보다 큼, 판단 틀림 | 공유 페이지 중복 계산 | PSS(`smaps_rollup`) 사용 |
| MMIO를 일반 캐시 속성으로 매핑 | 레지스터 값이 안 바뀌어 보임, 쓰기 유실 | 캐시가 끼어듦 | `pgprot_noncached`/`pgprot_device`, `/dev/mem`은 `O_SYNC` |
| `MemFree`가 작다고 메모리 부족 판단 | 멀쩡한데 경보 | 캐시가 RAM을 채운 정상 상태 | `MemAvailable`로 판단 |

---

## 16. 면접에서 이렇게 말한다

**Q.** What happens when you call malloc(1 GB) on a system with 512 MB of RAM?

**A.** 기본 overcommit 모드(0)에선 "RAM+swap보다 명백히 큰" 요청만 거절하므로 512 MB 장비에서 1 GB 한 번은 보통 거절된다. 하지만 더 작은 덩어리 여러 개로 나누면 전부 성공한다. 성공해도 받은 건 가상 주소 범위(VMA)뿐이고 물리 메모리는 처음 접근하는 페이지마다 page fault로 붙는다. 실제로 다 쓰면 OOM killer가 badness 점수가 가장 큰 프로세스를 죽인다.

> Under the default heuristic overcommit, a single request obviously larger than RAM plus swap is refused, but the same amount split into smaller chunks succeeds. Either way malloc only reserves virtual address space — for big sizes glibc uses an anonymous mmap — and physical pages are attached lazily on first touch through page faults. If the process actually touches more than the system can supply, the OOM killer picks the task with the highest badness score and kills it. With overcommit_memory=2 the allocation would fail up front instead.

**꼬리 질문**: "그럼 OOM에서 내 제어 데몬은 어떻게 보호하나?" → `oom_score_adj`를 낮추고, 부가 서비스는 cgroup 메모리 한도로 묶는다.

**Q.** What is the difference between a minor and a major page fault?

**A.** 둘 다 PTE가 없어서 MMU가 예외를 낸 것이다. minor는 데이터가 이미 RAM에 있어(0 페이지 할당, page cache 히트) PTE만 채우면 되는 경우로 수백 ns~수 µs. major는 디스크나 swap에서 읽어야 해서 수십 µs~ms다. VM에서 측정해 보니 4K 익명 페이지 minor fault가 약 0.5 µs, readahead를 끈 cold 파일 major fault가 약 39 µs였다.

> Both are MMU translation faults handled by the kernel. A minor fault is satisfied from memory — a freshly zeroed anonymous page or a page already in the page cache — so the kernel only installs a PTE, costing hundreds of nanoseconds. A major fault requires I/O from storage or swap, so it costs tens of microseconds to milliseconds. On my arm64 VM I measured about 0.5 µs per minor fault and about 39 µs per major fault with readahead disabled.

**Q.** MAP_SHARED vs MAP_PRIVATE?

**A.** SHARED는 모든 매핑이 같은 물리 페이지(파일이면 page cache)를 가리켜서, 쓰기가 다른 프로세스와 파일에 보인다. PRIVATE는 COW다. 쓰기 전엔 공유하다가 쓰는 순간 그 페이지만 복사되어 내 것이 되고 파일엔 절대 반영되지 않는다. 실행 파일 로딩이 PRIVATE, 공유 메모리와 장치 버퍼가 SHARED다.

> With MAP_SHARED every mapping points at the same physical pages — the page cache for files — so stores are visible to other processes and eventually written back to the file. MAP_PRIVATE is copy-on-write: pages are shared read-only until the first store, then the kernel copies that one page for the writer, and changes never reach the file. Shared libraries are mapped private; shared memory and device buffers are mapped shared.

**Q.** How would you get camera frames to user space without copying?

**A.** V4L2 MMAP 스트리밍을 쓴다. REQBUFS로 드라이버가 DMA 버퍼를 할당하고, QUERYBUF로 각 버퍼의 offset을 받아 mmap하면 유저 page table이 그 물리 페이지를 직접 가리킨다. 그 뒤에는 QBUF/DQBUF로 버퍼 번호만 주고받는다. 다른 장치(GPU, NPU, 인코더)로 넘길 땐 EXPBUF로 dma-buf fd를 내보낸다. 버퍼가 cacheable이면 CPU 접근 전후 캐시 동기화가 필요하다.

> I would use V4L2 streaming I/O with V4L2_MEMORY_MMAP. REQBUFS makes the driver allocate DMA-capable buffers, QUERYBUF gives each buffer's offset cookie, and mmap maps those physical pages straight into my address space. After STREAMON the loop is just poll, DQBUF, process in place, QBUF — only buffer indices cross the boundary. To hand frames to an encoder or NPU I would export them as dma-buf file descriptors, and I would watch cache maintenance if the buffers are cacheable.

**Q.** How do you prevent page faults in a real-time thread?

**A.** 초기화 단계에서 `mlockall(MCL_CURRENT | MCL_FUTURE)`로 지금과 앞으로의 모든 매핑을 RAM에 붙이고 잠근다. 스택은 처음 닿을 때 붙으므로 예상 최대 크기만큼 미리 touch하고, 버퍼는 전부 미리 할당한다. 루프 안에서는 malloc·파일 I/O를 안 한다. 일반 유저면 `RLIMIT_MEMLOCK` 때문에 MCL_FUTURE 이후 mmap이 EAGAIN으로 실패할 수 있으니 `CAP_IPC_LOCK`이나 `LimitMEMLOCK`을 설정한다. 확인은 `/proc/<pid>/stat`의 fault 카운터가 루프 중 안 느는지로 한다.

> At startup I call mlockall with MCL_CURRENT and MCL_FUTURE so every existing and future mapping is faulted in and pinned. I pre-fault the stack by touching its expected maximum depth, preallocate all buffers, and keep malloc and file I/O out of the loop. As a non-root service I make sure RLIMIT_MEMLOCK is raised or the process has CAP_IPC_LOCK, otherwise later mmaps fail with EAGAIN. Then I verify the minflt and majflt counters in /proc/pid/stat stay flat while the loop runs.

**Q.** How does a driver implement mmap?

**A.** `file_operations.mmap(file, vma)`에서 VMA 범위를 검사하고, MMIO나 예약 메모리면 `vm_page_prot`를 noncached 등으로 정한 뒤 `remap_pfn_range`로 PTE를 바로 채운다. `dma_alloc_coherent` 버퍼면 캐시 속성을 맞춰 주는 `dma_mmap_coherent`를 쓴다. 아니면 `vm_ops->fault`를 등록해서 접근할 때 페이지를 넘긴다. `remap_pfn_range`는 VMA에 `VM_IO`, `VM_PFNMAP`을 세운다.

> The driver's mmap file operation receives the VMA. It validates the requested size and offset, sets vm_page_prot — noncached or device for registers, writecombine for framebuffers — and for MMIO or reserved memory calls remap_pfn_range to populate the page tables immediately, which marks the VMA VM_IO and VM_PFNMAP. For coherent DMA buffers it uses dma_mmap_coherent so the user mapping has the same attributes as the kernel mapping. Alternatively it installs a vm_ops fault handler and supplies pages lazily.

**꼬리 질문**: "왜 `/dev/mem`을 제품에서 쓰면 안 되나?" → 권한(root, CAP_SYS_RAWIO) 필요, 드라이버와 HW 상태 충돌, 인터럽트·전원 관리·DMA 불가, STRICT_DEVMEM 설정에 따라 막힘. UIO나 정식 드라이버로 간다.

**Q.** RSS vs PSS — which would you use to budget memory on a device?

**A.** PSS. RSS는 공유 페이지를 각 프로세스에 전부 세서 합이 RAM보다 커질 수 있다. PSS는 공유 페이지를 공유하는 프로세스 수로 나눠 센다. 4개 프로세스가 32 MiB를 공유하는 실험에서 RSS는 각 32 MiB, PSS는 각 8 MiB였다. `/proc/<pid>/smaps_rollup`에서 읽는다.

> PSS. RSS counts every shared page fully in each process, so summing RSS overcounts libraries and shared buffers. PSS divides each shared page by the number of processes mapping it, so the sum matches real usage. In a test with four processes sharing 32 MiB, each showed 32 MiB RSS but 8 MiB PSS. I read it from /proc/pid/smaps_rollup, and use Pss_Anon over time to spot leaks.

---

## 17. 직접 해보기

1. **개념**: 4 KiB 페이지, 48비트 VA에서 page table은 왜 4단계인가? 64 KiB 페이지라면 offset은 몇 비트이고 몇 단계가 되나?
   정답/힌트: 48 − 12 = 36비트를 9비트씩(512 엔트리) 나누면 4단계. 64K 페이지는 offset 16비트, 한 표에 8192 엔트리(13비트) → 32비트를 13+13+6으로 3단계.
2. **관찰**: `sleep 100 &`을 띄우고 `/proc/$!/maps`, `/proc/$!/smaps_rollup`을 본 뒤, 같은 `sleep`을 하나 더 띄워 두 프로세스의 Pss가 어떻게 변하는지 보라.
   정답/힌트: libc·sleep 바이너리 페이지가 공유되므로 두 번째를 띄우면 각자 Pss_File이 줄어든다.
3. **코드**: 예제 2를 고쳐서 같은 영역을 두 번째로 touch할 때 fault가 0인지 확인하라. 그 다음 `madvise(MADV_DONTNEED)` 후 다시 touch하면?
   정답/힌트: 두 번째는 0. `MADV_DONTNEED`는 익명 private 페이지를 버리므로 다시 페이지 수만큼 fault가 나고 내용은 0이 된다.
4. **코드**: 1 KiB 파일을 4 KiB 매핑하고 오프셋 2000을 읽어 보라. 그 다음 8 KiB를 매핑해 오프셋 5000을 읽어 보라. 각각 무엇이 나오나?
   정답/힌트: 첫 페이지 안의 파일 끝 너머는 0으로 읽힌다. 파일이 전혀 없는 두 번째 페이지 접근은 SIGBUS.
5. **strace 관찰**: `strace -e trace=mmap,mprotect,munmap /bin/true`의 출력에서 동적 링커가 libc를 매핑하는 순서를 해석하라.
   정답/힌트: 전체 크기만큼 한 번 예약 → `MAP_FIXED`로 세그먼트별 덮어쓰기 → bss용 익명 매핑 → RELRO 영역 `mprotect(PROT_READ)`.
6. **설계**: IMU 데몬(1 kHz)과 로거·제어 두 소비자가 있다. 예제 9의 seqlock 구조 대신 링버퍼를 shm에 두려면 무엇을 저장해야 하나? 소비자가 늦을 때 어떻게 알 수 있나?
   정답/힌트: head 인덱스(단조 증가 64비트)와 고정 크기 슬롯 배열, 각 슬롯의 seq. 소비자는 자기 tail과 head 차이가 슬롯 수를 넘으면 overrun.
7. **측정**: 예제 13을 `drop_caches` 직후(cold)에도 돌려 보라. 이번엔 read와 mmap 중 무엇이 빠른가? 왜?
   정답/힌트: 디스크 I/O가 지배해 차이가 줄어든다. mmap은 readahead 패턴에 따라 major fault가 섞이고, read는 큰 버퍼로 readahead를 잘 탄다.

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| VA / PA | virtual / physical address | 프로그램이 쓰는 주소 / 버스에 나가는 실제 주소 |
| MMU | Memory Management Unit | page table로 VA→PA 변환과 권한 검사를 하는 HW |
| page / page frame | 가상 / 물리 메모리의 고정 크기 단위 | 이 VM에선 4 KiB |
| page table, PTE | 변환 표와 그 엔트리 | 다음 단계 PA + 권한 + 메모리 속성 |
| TLB | Translation Lookaside Buffer | 변환 결과 캐시. miss면 table walk |
| VMA | `vm_area_struct` | 주소 공간의 연속 구간 하나. maps의 한 줄 |
| demand paging | 요구 페이징 | 처음 접근할 때 물리 페이지를 붙이는 방식 |
| minor / major fault | | RAM에서 해결 / 디스크 I/O 필요 |
| COW | copy-on-write | 쓰는 순간 그 페이지만 복사 |
| page cache | 파일 내용의 RAM 캐시 | read/write/mmap이 모두 공유 |
| dirty page | 디스크와 내용이 다른 캐시 페이지 | writeback 또는 msync로 내려감 |
| overcommit | 과다 약속 | RAM보다 많은 가상 메모리를 허용 |
| OOM killer | Out-Of-Memory killer | 메모리가 바닥나면 프로세스를 골라 죽임 |
| RSS / PSS / USS | resident / proportional / unique set size | 붙은 전부 / 공유분 나눠서 / 나만의 것 |
| THP | Transparent Huge Pages | 커널이 자동으로 2 MiB 페이지 사용 |
| hugetlbfs | 예약형 huge page | `MAP_HUGETLB`, `vm.nr_hugepages` |
| mlock | memory lock | 페이지를 RAM에 고정, fault·reclaim 방지 |
| ASLR | 주소 공간 무작위화 | 실행마다 시작 주소가 바뀜 |
| zero-copy | 복사 없는 전달 | 같은 물리 페이지를 여러 쪽이 매핑 |
| UIO | Userspace I/O | 레지스터와 IRQ를 유저에 넘기는 작은 프레임워크 |
| `remap_pfn_range` | 커널 함수 | 물리 프레임 번호로 유저 PTE를 채움 |
| dma-buf | 커널 버퍼 공유 프레임워크 | DMA 버퍼를 fd로 장치 간 전달 |

---

## 19. 요약 & 체크리스트

Linux에서 프로세스가 보는 주소는 전부 가상 주소이고, `mmap`과 `malloc`이 주는 것은 VMA라는 "약속"이다. 물리 페이지는 처음 만질 때 page fault로 붙는다(minor는 약 0.5 µs, major는 디스크 왕복). 그래서 커널은 RAM보다 많이 약속할 수 있고(overcommit), 정말 모자라면 OOM killer가 정리한다. `mmap`은 파일/익명 × SHARED/PRIVATE의 네 조합이고, SHARED는 같은 물리 페이지 공유, PRIVATE는 COW다. 이 "같은 물리 페이지를 여러 page table이 가리키게 한다"는 원리가 프로세스 간 공유 메모리, V4L2 카메라 버퍼 zero-copy, `/dev/mem`·UIO 레지스터 접근의 공통 기반이고, 드라이버 쪽에선 `.mmap` + `remap_pfn_range`/`dma_mmap_coherent`와 캐시 속성 선택으로 구현된다. RT 코드는 `mlockall`과 prefault로 fault를 루프 밖으로 몰아내고, 메모리 예산은 RSS가 아니라 PSS와 `MemAvailable`로 판단한다.

- [ ] arm64 4단계 page table walk와 TLB의 역할을 MPU와 비교해 설명할 수 있다
- [ ] `/proc/<pid>/maps` 한 줄의 각 필드를 읽고 text/data/bss/heap/mmap/stack을 찾을 수 있다
- [ ] demand paging과 minor/major fault를 설명하고 `getrusage`나 `/proc/<pid>/stat`으로 셀 수 있다
- [ ] overcommit 모드 0/1/2와 OOM killer, `oom_score_adj`를 설명할 수 있다
- [ ] glibc malloc이 brk와 mmap을 언제 쓰는지, "128 KiB 규칙"의 예외까지 말할 수 있다
- [ ] MAP_SHARED/PRIVATE, 파일/익명 조합을 상황에 맞게 고르고 COW를 설명할 수 있다
- [ ] `msync`, `munmap`, `mprotect`의 동작과 SIGSEGV(`SEGV_ACCERR`/`SEGV_MAPERR`)·SIGBUS를 구분할 수 있다
- [ ] fork 전 공유 매핑과 `shm_open`으로 프로세스 간 공유 메모리를 짤 수 있다
- [ ] RT 루프를 위해 `mlockall(MCL_CURRENT | MCL_FUTURE)`와 prefault를 적용하고 `RLIMIT_MEMLOCK` 함정을 피할 수 있다
- [ ] RSS/PSS와 `/proc/meminfo`의 MemAvailable·Cached·Dirty를 해석할 수 있다
- [ ] V4L2 MMAP zero-copy 흐름과 드라이버 `.mmap`(`remap_pfn_range`, `dma_mmap_coherent`)을 설명할 수 있다
- [ ] MMIO를 매핑할 때 필요한 캐시 속성(Device, Normal-NC)과 그 이유를 말할 수 있다

---

## 참고 자료

- man7.org: [mmap(2)](https://man7.org/linux/man-pages/man2/mmap.2.html), [mprotect(2)](https://man7.org/linux/man-pages/man2/mprotect.2.html), [msync(2)](https://man7.org/linux/man-pages/man2/msync.2.html), [mlock(2)](https://man7.org/linux/man-pages/man2/mlock.2.html), [madvise(2)](https://man7.org/linux/man-pages/man2/madvise.2.html), [shm_open(3)](https://man7.org/linux/man-pages/man3/shm_open.3.html), [mallopt(3)](https://man7.org/linux/man-pages/man3/mallopt.3.html), [getrusage(2)](https://man7.org/linux/man-pages/man2/getrusage.2.html), [proc_pid_maps(5)](https://man7.org/linux/man-pages/man5/proc_pid_maps.5.html), [proc_pid_smaps(5)](https://man7.org/linux/man-pages/man5/proc_pid_smaps.5.html), [proc_meminfo(5)](https://man7.org/linux/man-pages/man5/proc_meminfo.5.html)
- Michael Kerrisk, "The Linux Programming Interface" — 6장(프로세스 메모리 레이아웃), 49장(Memory Mappings), 50장(Virtual Memory Operations), 54장(POSIX Shared Memory)
- OSTEP — 13~23장 (Address Spaces, Paging, TLBs, Multi-level Page Tables, Swapping)
- docs.kernel.org: [Overcommit Accounting](https://docs.kernel.org/mm/overcommit-accounting.html), [Transparent Hugepage Support](https://docs.kernel.org/admin-guide/mm/transhuge.html), [HugeTLB Pages](https://docs.kernel.org/admin-guide/mm/hugetlbpage.html), [The Userspace I/O HOWTO](https://docs.kernel.org/driver-api/uio-howto.html), [V4L2 Streaming I/O (Memory Mapping)](https://docs.kernel.org/userspace-api/media/v4l/mmap.html), [Dynamic DMA mapping using the generic device](https://docs.kernel.org/core-api/dma-api.html)
- elixir.bootlin.com: `mm/mmap.c`(`do_mmap`), `mm/memory.c`(`handle_mm_fault`, `do_anonymous_page`, `do_wp_page`, `remap_pfn_range`), `arch/arm64/mm/fault.c`(`do_mem_abort`, `do_page_fault`), `drivers/char/mem.c`(`/dev/mem`), `drivers/uio/uio.c`
- Bootlin, "Linux kernel and driver development training" 슬라이드 — "Memory management", "DMA" 부분
