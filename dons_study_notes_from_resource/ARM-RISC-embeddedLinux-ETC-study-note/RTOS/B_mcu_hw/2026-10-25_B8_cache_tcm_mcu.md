# B8. 캐시 · TCM이 있는 MCU — Cortex-M7/M85의 L1 캐시, MPU 속성, TCM, 플래시 wait state, 그리고 DMA 일관성 버그

> **이 노트를 다 읽으면**: 캐시 라인 · set · way · write-back/write-through · allocate 정책을 처음부터 설명하고 측정으로 확인할 수 있다 · Cortex-M7/M85에서 `SCB_EnableDCache`, `SCB_CleanDCache_by_Addr`, `SCB_InvalidateDCache_by_Addr`를 언제 어떤 순서로 부르는지, 32 B 정렬을 왜 지켜야 하는지 말할 수 있다 · MPU로 DMA 버퍼를 non-cacheable로 만들고, ISR · rate loop를 ITCM에, 스택을 DTCM에 두는 링커 스크립트를 쓸 수 있다 · "DMA로 받은 데이터가 가끔 옛날 값" 버그를 보자마자 원인 3가지와 해결책 3가지를 댈 수 있다
> **왜 필요한가**: STM32H7 · i.MX RT · Cortex-M85급 칩으로 드론 FC · 모터 컨트롤러를 만들면 **"캐시 켜니까 SPI DMA 데이터가 깨진다"**가 bring-up 1주 차의 단골 버그다. 면접에서도 "DMA and cache coherency on Cortex-M7"은 고성능 MCU 포지션의 거의 필수 질문이다
> **Don 기준 난이도**: SSD 컨트롤러에서 DMA · 캐시 일관성은 이미 몸으로 안다(Cortex-R의 `SCB_*_by_Addr`) / 새로 배울 부분은 M7 고유의 기본 메모리 맵 속성 · MPU TEX/C/B 인코딩 · "shareable이면 캐시 안 됨" 규칙 · TCM과 DMA 마스터의 접근 가능 여부(칩마다 다름) · RTOS(FreeRTOS · Zephyr)가 캐시를 어디까지 책임지는가
> **선행 노트**: B1(프로그래머 모델), B5(리셋부터 main까지, 링커 스크립트), B7(Cortex-M 세대 지도), EL H9(Linux DMA API · coherent vs streaming). 이어지는 노트: E6(DMA 드라이버 전체), N7(WCET와 캐시), B9(Cortex-R의 TCM · ECC)
> 🔁 **Bare-Metal 겹침**: BM B3(캐시 원리) · BM B9(플래시 · SRAM 기술: wait state, prefetch/ART, ECC, SRAM 뱅크, TCM) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 "RTOS 없이 startup 코드에서 캐시 · MPU · 플래시 WS를 켜는 순서와 클록 트리 설정"만 더한다 (MMU · TLB는 Bare-Metal 쪽 주인)

---

## 0. 큰 그림 — 이게 왜 필요한가

Cortex-M3/M4 시절 MCU에는 CPU 캐시가 없었다. SRAM은 CPU와 같은 속도로 돌았고, 플래시가 느린 문제는 칩 회사가 붙인 작은 가속기(ST의 ART 같은 것)가 덮었다. 그래서 DMA가 SRAM에 쓴 값을 CPU가 바로 읽어도 아무 문제가 없었다. **"메모리에 쓰면 모두가 그 값을 본다"**가 당연했다.

Cortex-M7(2014년 발표)부터 이 당연함이 깨진다. 코어 클록이 400~600 MHz로 올라가자 SRAM · 플래시 · 외부 SDRAM이 코어를 따라가지 못했고, Arm은 Cortex-A처럼 **L1 I-cache와 D-cache**를 코어 안에 넣었다. 동시에 "캐시의 불확실성 없이 항상 1사이클에 접근되는 전용 RAM"인 **TCM(Tightly Coupled Memory)**도 넣었다. Cortex-M55, M85도 같은 구조다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="20" width="160" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="100" y="50" font-size="13" text-anchor="middle" font-weight="bold">Cortex-M7 코어</text>
<rect x="20" y="105" width="75" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="57" y="130" font-size="13" text-anchor="middle">I-cache</text>
<rect x="105" y="105" width="75" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="142" y="130" font-size="13" text-anchor="middle">D-cache</text>
<line x1="57" y1="70" x2="57" y2="103" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="142" y1="70" x2="142" y2="103" stroke="currentColor" marker-end="url(#ah1)"/>
<rect x="230" y="20" width="130" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="295" y="43" font-size="13" text-anchor="middle">ITCM 64 KB</text>
<rect x="230" y="64" width="130" height="36" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="295" y="87" font-size="13" text-anchor="middle">DTCM 128 KB</text>
<line x1="180" y1="38" x2="228" y2="38" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="180" y1="60" x2="228" y2="80" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="295" y="120" font-size="12" text-anchor="middle">0 wait · 캐시를 안 거침</text>
<rect x="470" y="95" width="180" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="560" y="125" font-size="13" text-anchor="middle" font-weight="bold">DMA 컨트롤러</text>
<text x="560" y="80" font-size="12" text-anchor="middle">D-cache 내용을 모른다</text>
<line x1="20" y1="200" x2="660" y2="200" stroke="currentColor" stroke-width="3"/>
<text x="340" y="190" font-size="12" text-anchor="middle">AXI / AHB 버스 매트릭스</text>
<line x1="57" y1="145" x2="57" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="142" y1="145" x2="142" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="560" y1="145" x2="560" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="470" y1="120" x2="364" y2="88" stroke="#d0564a" stroke-dasharray="4,3"/>
<text x="400" y="150" font-size="12" text-anchor="middle">TCM 접근은 칩마다 다름</text>
<rect x="20" y="245" width="140" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="90" y="267" font-size="13" text-anchor="middle">Flash</text>
<text x="90" y="285" font-size="12" text-anchor="middle">wait state · ART</text>
<rect x="180" y="245" width="140" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="250" y="275" font-size="13" text-anchor="middle">AXI SRAM</text>
<rect x="340" y="245" width="140" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="410" y="267" font-size="13" text-anchor="middle">D2 SRAM</text>
<text x="410" y="285" font-size="12" text-anchor="middle">DMA 버퍼용</text>
<rect x="500" y="245" width="160" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="580" y="275" font-size="13" text-anchor="middle">UART · SPI · ADC</text>
<line x1="90" y1="202" x2="90" y2="243" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="250" y1="202" x2="250" y2="243" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="410" y1="202" x2="410" y2="243" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="580" y1="202" x2="580" y2="243" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="340" y="325" font-size="12" text-anchor="middle">CPU는 D-cache를 통해 보고, DMA는 RAM을 직접 본다 → 둘이 다른 값을 볼 수 있다</text>
</svg>
```
그림 1 — Cortex-M7 MCU(STM32H7 비슷한 구성)의 메모리 경로. CPU의 load/store는 D-cache를 거치고, DMA는 버스 매트릭스로 RAM에 직접 간다. TCM은 캐시를 거치지 않는 별도 경로다.

이 그림 하나에서 이 노트의 모든 주제가 나온다.

- **캐시**(1~2절): CPU와 느린 메모리 사이의 작은 복사본. 빨라지지만 "복사본과 원본이 다를 수 있다".
- **MPU 속성**(3절): 어느 주소를 캐시할지, write-back으로 할지 write-through로 할지는 MPU(또는 기본 메모리 맵)가 정한다.
- **TCM**(4절): 캐시의 불확실성이 싫은 코드 · 데이터를 위한 전용 RAM. ISR, rate loop, 태스크 스택.
- **플래시 wait state · ART**(5절): 플래시는 코어보다 수 배 느리다. 그 차이를 메우는 방법.
- **DMA 일관성**(6절): CPU와 DMA가 다른 값을 보는 버그와 세 가지 해결책.

Don에게 익숙한 말로 바꾸면 이렇다. SSD 컨트롤러에서 호스트 DMA 엔진이 NVMe 명령을 SRAM에 쓰고, Cortex-R 코어가 그걸 파싱하기 전에 `SCB_InvalidateDCache_by_Addr`를 불렀다. 쓰기 데이터 버퍼를 NAND 채널 DMA에 넘기기 전에는 clean을 불렀다. **이 노트는 그 경험의 Cortex-M판 번역**이다. 다른 점은 (1) M7은 MPU 속성의 기본값과 "shareable" 규칙이 R5와 조금 다르고, (2) RTOS(FreeRTOS)는 이 일을 전혀 해 주지 않으며, (3) DMA가 TCM에 닿는지가 칩마다 다르다는 것이다.

### 실습 환경 한 줄

- **QEMU `mps2-an500`(Cortex-M7)** — bare-metal과 FreeRTOS V11.3.2(ARM_CM3 포트를 soft-float로 M7에 올림). **QEMU는 캐시를 모델링하지 않는다.** 캐시 유지 코드는 실행되고 레지스터 쓰기도 일어나지만, 캐시 효과(stale 데이터)는 보이지 않는다. 이 노트는 그런 예제마다 **"실행되지만 캐시 효과는 QEMU에서 안 보임"**이라고 표시한다.
- **QEMU `mps3-an547`(Cortex-M55) + Zephyr v4.4.2** — Zephyr 캐시 API와 `__nocache`가 만드는 MPU 영역 확인.
- **VM 호스트(Lima `elinux`, Apple M2 위의 arm64 Linux VM)** — 진짜 캐시 현상(라인 크기, 캐시 크기 무릎, false sharing, 선점 뒤 캐시 냉각)을 측정. **호스트 측정**이라고 표시한다. VM이므로 절대값은 참고만.
- **Python 캐시 시뮬레이터**(직접 작성, 라인 32 B) — QEMU로 못 보여 주는 DMA stale 버그와 정렬 안 된 invalidate 버그를 재현.
- 타이밍 숫자(QEMU)는 `ICOUNT=1` 기준이다. **QEMU icount 기준이고 실제 칩과 절대값은 다르다(캐시 · 플래시 wait state · 버스 미모델링).**

---

## 1. 캐시 원리 — 처음부터

> 🔁 이 절은 Bare-Metal BM B3(캐시 원리)와 겹친다 (주인: 이 노트).

### 1.1 직관 — 책상과 서고

도서관 서고(메인 메모리)는 크지만 책 한 권 가져오는 데 5분이 걸린다. 책상(캐시)에는 몇 권밖에 못 올리지만 손만 뻗으면 된다. 그래서 **방금 본 책은 또 볼 확률이 높고(시간 지역성)**, **어떤 책을 봤으면 옆 칸 책도 볼 확률이 높다(공간 지역성)**는 두 가지 관찰에 기대어, 한 번 가져올 때 한 권이 아니라 **옆 칸 몇 권을 묶음으로** 가져와 책상에 둔다. 이 묶음이 **캐시 라인(cache line)**이다.

그리고 책상 위 책에 메모를 하면(쓰기), 서고의 원본과 책상 위 복사본이 달라진다. 이 메모를 언제 서고에 반영하느냐가 **write-back vs write-through**다. 다른 사람(DMA)이 서고의 원본을 고쳐도 내 책상 위 복사본은 그대로다 — 이게 **일관성(coherency) 문제의 근원**이다.

### 1.2 정의 — 라인, set, way, 주소 분해

- **라인(line)**: 캐시가 메모리와 주고받는 최소 단위. Cortex-M7/M55/M85의 L1은 **32 B**(워드 8개)로 고정이다. 1바이트를 읽어도 그 바이트를 포함한 32 B 전체를 가져온다(**line fill**).
- **set**: 주소의 일부 비트(index)로 정해지는 "자리 묶음". 어떤 주소의 라인은 정해진 set 하나에만 들어갈 수 있다.
- **way**: 한 set 안의 자리 수. way가 1이면 **direct-mapped**(주소마다 들어갈 자리가 딱 하나), 4면 **4-way set-associative**(4자리 중 아무 데나).
- **tag**: 그 자리에 지금 들어 있는 라인이 원래 어느 주소였는지 표시하는 상위 비트.
- **캐시 크기 = 라인 크기 × way 수 × set 수**. 예: M7의 16 KB D-cache = 32 B × 4 way × 128 set.

주소 32비트는 이렇게 쪼개진다 (16 KB, 4-way, 32 B 라인 기준).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="40" y="20" width="340" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="210" y="45" font-size="13" text-anchor="middle">tag [31:12] — 20비트</text>
<rect x="380" y="20" width="150" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="455" y="45" font-size="13" text-anchor="middle">index [11:5]</text>
<rect x="530" y="20" width="110" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="585" y="45" font-size="13" text-anchor="middle">offset [4:0]</text>
<text x="455" y="78" font-size="12" text-anchor="middle">128개 set 중 하나</text>
<text x="585" y="78" font-size="12" text-anchor="middle">라인 안 32 B</text>
<line x1="455" y1="84" x2="300" y2="138" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="40" y="128" font-size="12">set</text>
<text x="150" y="128" font-size="12" text-anchor="middle">way 0</text>
<text x="280" y="128" font-size="12" text-anchor="middle">way 1</text>
<text x="410" y="128" font-size="12" text-anchor="middle">way 2</text>
<text x="540" y="128" font-size="12" text-anchor="middle">way 3</text>
<text x="40" y="160" font-size="12">0</text>
<rect x="90" y="142" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<rect x="220" y="142" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<rect x="350" y="142" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<rect x="480" y="142" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="40" y="192" font-size="12">5</text>
<rect x="90" y="174" width="120" height="26" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/>
<rect x="220" y="174" width="120" height="26" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/>
<rect x="350" y="174" width="120" height="26" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/>
<rect x="480" y="174" width="120" height="26" fill="#e08a3c" fill-opacity="0.18" stroke="#e08a3c"/>
<text x="150" y="192" font-size="12" text-anchor="middle">tag, 32 B, V, D</text>
<text x="280" y="192" font-size="12" text-anchor="middle">tag, 32 B, V, D</text>
<text x="410" y="192" font-size="12" text-anchor="middle">tag, 32 B, V, D</text>
<text x="540" y="192" font-size="12" text-anchor="middle">tag, 32 B, V, D</text>
<text x="40" y="224" font-size="12">127</text>
<rect x="90" y="206" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<rect x="220" y="206" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<rect x="350" y="206" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<rect x="480" y="206" width="120" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<rect x="140" y="252" width="420" height="32" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="350" y="273" font-size="13" text-anchor="middle">tag 비교기 4개가 동시에 비교 → 같으면 hit</text>
<line x1="350" y1="232" x2="350" y2="250" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="350" y="310" font-size="12" text-anchor="middle">V = valid, D = dirty(이 라인을 고쳤고 아직 메모리에 안 썼다)</text>
</svg>
```
그림 2 — 16 KB · 4-way · 32 B 라인 D-cache의 주소 분해. index 비트가 set을 고르고, 그 set의 4개 way의 tag를 동시에 비교한다. 각 라인은 valid · dirty 비트를 가진다.

계산을 한 번 해 보자. 주소 `0x2000_0124`:

- offset = `0x124 & 0x1F` = `0x04` (라인 안 4번째 바이트)
- index = `(0x124 >> 5) & 0x7F` = `9`
- tag = `0x2000_0124 >> 12` = `0x20000`
- 이 라인은 **주소 `0x2000_0120`~`0x2000_013F`의 32 B 전체**다.

**direct-mapped vs N-way**: direct-mapped는 비교기가 하나라 단순하고 빠르지만, 같은 index를 쓰는 두 주소(정확히 캐시 크기만큼 떨어진 두 배열)를 번갈아 쓰면 서로 계속 쫓아낸다(**conflict miss**). way를 늘리면 이 충돌이 줄어든다. 1.5절에서 시뮬레이터로 확인한다.

### 1.3 hit · miss의 비용 — 호스트에서 진짜로 재 보기

캐시는 "평균은 빠르지만 가끔 느리다"는 장치다. 얼마나 차이 나는지 직접 재 보자. QEMU에는 캐시가 없으므로 **VM 호스트(Apple M2 위 arm64 Linux)**에서 잰다. MCU와 숫자 규모는 다르지만, 계단 모양은 똑같은 원리에서 나온다.

#### 예제 H1 — stride로 라인 크기 찾기 (호스트 측정)

큰 배열(256 MB)을 stride s 바이트 간격으로 한 바이트씩 읽어서, 접근 1회당 시간을 잰다. 같은 라인 안의 접근은 거의 공짜이므로, stride가 라인 크기에 이를 때까지 시간이 늘다가 그 뒤로는 평평해진다.

```c
/* h1_stride.c — 큰 배열을 stride s로 훑을 때 접근 1회당 시간 → 캐시 라인 크기 (VM 호스트 arm64) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#define SZ (256u << 20)                       /* 256 MB: 모든 캐시보다 크다 */
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

int main(void)
{
  uint64_t ctr; __asm volatile("mrs %0, ctr_el0" : "=r"(ctr));
  printf("CTR_EL0=0x%llx DminLine=%u B IminLine=%u B\n", (unsigned long long)ctr,
         4u << ((ctr >> 16) & 0xF), 4u << (ctr & 0xF));
  volatile uint8_t *a = aligned_alloc(4096, SZ);
  for (size_t i = 0; i < SZ; i += 4096) a[i] = 1;          /* 페이지 미리 할당 */
  for (unsigned s = 4; s <= 1024; s <<= 1) {
    double best = 1e9;
    for (int rep = 0; rep < 3; rep++) {
      size_t n = 0; uint32_t sum = 0;
      double t0 = now();
      for (size_t i = 0; i < SZ && n < (8u << 20); i += s, n++) sum += a[i];
      double ns = (now() - t0) * 1e9 / n;
      if (ns < best) best = ns;
      if (sum == 0xFFFFFFFF) puts("");
    }
    printf("stride %4u B : %6.2f ns/access\n", s, best);
  }
  return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/B8/host && gcc -O2 -Wall -Wextra -o h1 /Users/donh/.rtos_scratch/B8/host/h1_stride.c && ./h1'
```

```text
CTR_EL0=0x9444c004 DminLine=64 B IminLine=64 B
stride    4 B :   0.47 ns/access
stride    8 B :   0.49 ns/access
stride   16 B :   0.51 ns/access
stride   32 B :   0.82 ns/access
stride   64 B :   1.58 ns/access
stride  128 B :   3.72 ns/access
stride  256 B :   3.92 ns/access
stride  512 B :   4.05 ns/access
stride 1024 B :   3.97 ns/access
```

출력에서 볼 것: stride 32 → 64 → 128에서 시간이 두 배씩 늘고, **128 B부터 평평**하다. 그런데 `CTR_EL0.DminLine`(아키텍처가 알려 주는 "가장 작은 D-cache 라인")은 **64 B**다. 두 숫자가 다르다는 게 교훈이다.

- `CTR_EL0.DminLine`(Cortex-M에서는 `SCB->CTR`)은 **캐시 유지 연산을 몇 바이트씩 건너뛰며 해야 안전한지**를 알려 주는 값이다. 유지 루프는 이 값을 쓴다.
- 메모리 쪽에서 실제로 한 번에 가져오는 단위는 더 클 수 있다. 여기서는 128 B(Apple 칩의 하위 캐시 라인이 128 B이거나, 인접 라인 prefetch가 짝으로 가져오는 것 — 이 실험만으로는 구별할 수 없다. macOS의 `sysctl hw.cachelinesize`도 128을 보고한다).

```svg
<svg viewBox="0 0 640 280" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="225" x2="620" y2="225" stroke="currentColor"/>
<line x1="60" y1="25" x2="60" y2="225" stroke="currentColor"/>
<text x="54" y="184.6" font-size="12" text-anchor="end">1 ns</text>
<line x1="60" y1="180.6" x2="620" y2="180.6" stroke="#888" stroke-opacity="0.4" stroke-dasharray="3,3"/>
<text x="54" y="140.1" font-size="12" text-anchor="end">2 ns</text>
<line x1="60" y1="136.1" x2="620" y2="136.1" stroke="#888" stroke-opacity="0.4" stroke-dasharray="3,3"/>
<text x="54" y="95.7" font-size="12" text-anchor="end">3 ns</text>
<line x1="60" y1="91.7" x2="620" y2="91.7" stroke="#888" stroke-opacity="0.4" stroke-dasharray="3,3"/>
<text x="54" y="51.2" font-size="12" text-anchor="end">4 ns</text>
<line x1="60" y1="47.2" x2="620" y2="47.2" stroke="#888" stroke-opacity="0.4" stroke-dasharray="3,3"/>
<text x="60.0" y="243" font-size="12" text-anchor="middle">4</text>
<text x="130.0" y="243" font-size="12" text-anchor="middle">8</text>
<text x="200.0" y="243" font-size="12" text-anchor="middle">16</text>
<text x="270.0" y="243" font-size="12" text-anchor="middle">32</text>
<text x="340.0" y="243" font-size="12" text-anchor="middle">64</text>
<text x="410.0" y="243" font-size="12" text-anchor="middle">128</text>
<text x="480.0" y="243" font-size="12" text-anchor="middle">256</text>
<text x="550.0" y="243" font-size="12" text-anchor="middle">512</text>
<text x="620.0" y="243" font-size="12" text-anchor="middle">1024</text>
<text x="340" y="265" font-size="13" text-anchor="middle">stride (바이트)</text>
<polyline points="60.0,204.1 130.0,203.2 200.0,202.3 270.0,188.6 340.0,154.8 410.0,59.7 480.0,50.8 550.0,45.0 620.0,48.6" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="60.0" cy="204.1" r="3" fill="#4a7bd0"/>
<circle cx="130.0" cy="203.2" r="3" fill="#4a7bd0"/>
<circle cx="200.0" cy="202.3" r="3" fill="#4a7bd0"/>
<circle cx="270.0" cy="188.6" r="3" fill="#4a7bd0"/>
<circle cx="340.0" cy="154.8" r="3" fill="#4a7bd0"/>
<circle cx="410.0" cy="59.7" r="3" fill="#4a7bd0"/>
<circle cx="480.0" cy="50.8" r="3" fill="#4a7bd0"/>
<circle cx="550.0" cy="45.0" r="3" fill="#4a7bd0"/>
<circle cx="620.0" cy="48.6" r="3" fill="#4a7bd0"/>
<line x1="410.0" y1="25" x2="410.0" y2="225" stroke="#e08a3c" stroke-dasharray="4,3"/>
<text x="404.0" y="65" font-size="12" text-anchor="end">128 B부터 평평 = 메모리 쪽 전송 단위</text>
<text x="60.0" y="190.8" font-size="12">한 라인 안 여러 번 = 거의 공짜</text>
</svg>
```
그림 3 — 예제 H1 측정값(호스트, ns/access). stride가 라인보다 작으면 한 번 가져온 라인을 여러 번 쓰므로 싸다. 128 B 이상에서는 접근마다 새 라인이 필요해 평평해진다.

#### 예제 H2 — 작업 집합 크기로 L1 · L2 · DRAM 계단 보기 (호스트 측정)

이번에는 **pointer chasing**이다. 라인 하나에 노드 하나를 두고, 무작위 순환 순서로 연결해서 `p = *p`를 반복한다. 다음 주소가 앞의 load 결과에 의존하므로 CPU가 미리 가져올(prefetch) 수 없고, **load 하나의 순수한 지연**이 나온다. 작업 집합(working set) 크기를 4 KB에서 256 MB까지 키운다.

```c
/* h2_wset.c — 작업 집합 크기별 의존 load 지연(pointer chasing) → L1/L2/SLC/DRAM 무릎 (VM 호스트) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#define NODE 128                               /* 노드 간격 = 128 B (한 노드 = 한 라인) */
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static double chase(size_t bytes)
{
  size_t n = bytes / NODE;
  char *buf = aligned_alloc(4096, bytes);
  size_t *perm = malloc(n * sizeof *perm);
  for (size_t i = 0; i < n; i++) perm[i] = i;
  for (size_t i = n - 1; i > 0; i--) {                 /* 무작위 순환 → prefetcher 무력화 */
    size_t j = (size_t)rand() % (i + 1), t = perm[i]; perm[i] = perm[j]; perm[j] = t;
  }
  for (size_t i = 0; i < n; i++)
    *(void **)(buf + perm[i] * NODE) = buf + perm[(i + 1) % n] * NODE;
  void **p = (void **)(buf + perm[0] * NODE);
  size_t steps = 20u << 20;
  for (size_t i = 0; i < n; i++) p = *p;               /* 예열 */
  double best = 1e9;
  for (int rep = 0; rep < 3; rep++) {
    double t0 = now();
    for (size_t i = 0; i < steps; i++) p = *p;
    double ns = (now() - t0) * 1e9 / steps;
    if (ns < best) best = ns;
  }
  if (p == NULL) puts("");
  free(perm); free(buf);
  return best;
}

int main(void)
{
  srand(1);
  for (size_t kb = 4; kb <= 256 * 1024; kb *= 2) {
    printf("%8zu KB : %6.2f ns/load\n", kb, chase(kb * 1024));
    if (kb >= 32 && kb < 64 * 1024) printf("%8zu KB : %6.2f ns/load\n", kb * 3 / 2, chase(kb * 1536));
  }
  return 0;
}
```

```text
       4 KB :   0.89 ns/load
       8 KB :   0.91 ns/load
      16 KB :   0.89 ns/load
      32 KB :   0.89 ns/load
      48 KB :   0.90 ns/load
      64 KB :   0.92 ns/load
      96 KB :   0.90 ns/load
     128 KB :   0.90 ns/load
     192 KB :   5.82 ns/load
     256 KB :   6.15 ns/load
     384 KB :   6.20 ns/load
     512 KB :   6.26 ns/load
     768 KB :   6.30 ns/load
    1024 KB :   6.33 ns/load
    1536 KB :   6.85 ns/load
    2048 KB :   7.20 ns/load
    3072 KB :   7.27 ns/load
    4096 KB :   7.49 ns/load
    6144 KB :   8.13 ns/load
    8192 KB :  10.12 ns/load
   12288 KB :  37.68 ns/load
   16384 KB :  96.27 ns/load
   24576 KB : 104.50 ns/load
   32768 KB : 109.34 ns/load
   49152 KB : 108.12 ns/load
   65536 KB : 115.58 ns/load
  131072 KB : 130.46 ns/load
  262144 KB : 129.14 ns/load
```

```svg
<svg viewBox="0 0 640 320" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="260" x2="620" y2="260" stroke="currentColor"/>
<line x1="60" y1="30" x2="60" y2="260" stroke="currentColor"/>
<line x1="60" y1="233.4" x2="620" y2="233.4" stroke="#888" stroke-opacity="0.4" stroke-dasharray="3,3"/>
<text x="54" y="237.4" font-size="12" text-anchor="end">1 ns</text>
<line x1="60" y1="145.0" x2="620" y2="145.0" stroke="#888" stroke-opacity="0.4" stroke-dasharray="3,3"/>
<text x="54" y="149.0" font-size="12" text-anchor="end">10 ns</text>
<line x1="60" y1="56.6" x2="620" y2="56.6" stroke="#888" stroke-opacity="0.4" stroke-dasharray="3,3"/>
<text x="54" y="60.6" font-size="12" text-anchor="end">100 ns</text>
<text x="60.0" y="278" font-size="12" text-anchor="middle">4K</text>
<text x="200.0" y="278" font-size="12" text-anchor="middle">64K</text>
<text x="340.0" y="278" font-size="12" text-anchor="middle">1M</text>
<text x="480.0" y="278" font-size="12" text-anchor="middle">16M</text>
<text x="620.0" y="278" font-size="12" text-anchor="middle">256M</text>
<text x="340" y="300" font-size="13" text-anchor="middle">작업 집합 크기 (바이트, log)</text>
<polyline points="60.0,237.9 95.0,237.0 130.0,237.9 165.0,237.9 185.5,237.4 200.0,236.6 220.5,237.4 235.0,237.4 255.5,165.8 270.0,163.7 290.5,163.4 305.0,163.0 325.5,162.7 340.0,162.6 360.5,159.5 375.0,157.6 395.5,157.2 410.0,156.1 430.5,152.9 445.0,144.5 465.5,94.1 480.0,58.1 500.5,54.9 515.0,53.2 535.5,53.6 550.0,51.1 585.0,46.4 620.0,46.8" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="60.0" cy="237.9" r="2.5" fill="#4a7bd0"/>
<circle cx="95.0" cy="237.0" r="2.5" fill="#4a7bd0"/>
<circle cx="130.0" cy="237.9" r="2.5" fill="#4a7bd0"/>
<circle cx="165.0" cy="237.9" r="2.5" fill="#4a7bd0"/>
<circle cx="185.5" cy="237.4" r="2.5" fill="#4a7bd0"/>
<circle cx="200.0" cy="236.6" r="2.5" fill="#4a7bd0"/>
<circle cx="220.5" cy="237.4" r="2.5" fill="#4a7bd0"/>
<circle cx="235.0" cy="237.4" r="2.5" fill="#4a7bd0"/>
<circle cx="255.5" cy="165.8" r="2.5" fill="#4a7bd0"/>
<circle cx="270.0" cy="163.7" r="2.5" fill="#4a7bd0"/>
<circle cx="290.5" cy="163.4" r="2.5" fill="#4a7bd0"/>
<circle cx="305.0" cy="163.0" r="2.5" fill="#4a7bd0"/>
<circle cx="325.5" cy="162.7" r="2.5" fill="#4a7bd0"/>
<circle cx="340.0" cy="162.6" r="2.5" fill="#4a7bd0"/>
<circle cx="360.5" cy="159.5" r="2.5" fill="#4a7bd0"/>
<circle cx="375.0" cy="157.6" r="2.5" fill="#4a7bd0"/>
<circle cx="395.5" cy="157.2" r="2.5" fill="#4a7bd0"/>
<circle cx="410.0" cy="156.1" r="2.5" fill="#4a7bd0"/>
<circle cx="430.5" cy="152.9" r="2.5" fill="#4a7bd0"/>
<circle cx="445.0" cy="144.5" r="2.5" fill="#4a7bd0"/>
<circle cx="465.5" cy="94.1" r="2.5" fill="#4a7bd0"/>
<circle cx="480.0" cy="58.1" r="2.5" fill="#4a7bd0"/>
<circle cx="500.5" cy="54.9" r="2.5" fill="#4a7bd0"/>
<circle cx="515.0" cy="53.2" r="2.5" fill="#4a7bd0"/>
<circle cx="535.5" cy="53.6" r="2.5" fill="#4a7bd0"/>
<circle cx="550.0" cy="51.1" r="2.5" fill="#4a7bd0"/>
<circle cx="585.0" cy="46.4" r="2.5" fill="#4a7bd0"/>
<circle cx="620.0" cy="46.8" r="2.5" fill="#4a7bd0"/>
<line x1="235.0" y1="30" x2="235.0" y2="260" stroke="#e08a3c" stroke-dasharray="4,3"/>
<line x1="445.0" y1="30" x2="445.0" y2="260" stroke="#e08a3c" stroke-dasharray="4,3"/>
<text x="229.0" y="44" font-size="12" text-anchor="end">L1D 128 KB 끝</text>
<text x="439.0" y="44" font-size="12" text-anchor="end">L2 끝 (8~12 MB)</text>
<text x="120.0" y="227.4" font-size="12">0.9 ns ≈ 3 cycle</text>
<text x="270.0" y="153.4" font-size="12">6~8 ns</text>
<text x="420.1" y="39.2" font-size="12">DRAM 100~130 ns</text>
</svg>
```
그림 4 — 예제 H2 측정값(호스트, 세로축 log). 128 KB까지 0.9 ns(약 3 cycle, M2 P-core L1D 128 KB), 그 뒤 6~8 ns(L2), 12~16 MB에서 DRAM 100~130 ns로 뛴다. 큰 크기에서는 TLB miss도 섞여 있다.

출력에서 볼 것: **같은 load 명령 하나가 0.9 ns에서 130 ns까지 140배 차이 난다.** 이 비율이 캐시의 본질이다. 계단이 Apple M2 P-core의 문서상 L1D 128 KB(`sysctl hw.perflevel0.l1dcachesize: 131072`)와 정확히 맞는다. L2는 16 MB(`hw.perflevel0.l2cachesize`)지만 무릎은 8~12 MB에서 시작한다 — 무작위 배치에서 4 KB 페이지가 L2 set에 고르게 퍼지지 않고, 다른 vCPU · 호스트 프로세스와 L2를 나눠 쓰기 때문이다(추정).

MCU로 옮기면 이렇다. STM32H7(480 MHz)에서 D-cache hit은 1 cycle(약 2 ns), AXI SRAM miss는 수 cycle~십여 cycle, 외부 SDRAM miss는 수십 cycle 이상이다(정확한 값은 칩 · 버스 클록 · 설정마다 다르고 이 랩에서 잴 수 없다). 계단의 높이는 호스트보다 낮지만, **"캐시에 들어가느냐"가 실행 시간의 분산을 만든다**는 점은 같다. 이 분산이 실시간 시스템에서 WCET를 어렵게 만든다(8.4절, N7).

### 1.4 쓰기 정책 — write-back, write-through, allocate

읽기는 단순하다. miss면 라인을 채운다(**read-allocate**). 쓰기는 선택지가 두 축이다.

| 축 | 선택 | 뜻 | 장점 | 단점 |
|---|---|---|---|---|
| hit일 때 | **write-back (WB)** | 캐시 라인만 고치고 dirty 표시. 쫓겨날 때(evict) 또는 clean할 때 메모리에 씀 | 같은 변수를 1000번 써도 버스 쓰기는 1번 | 메모리와 캐시가 다를 수 있다 → DMA TX 전에 clean 필수 |
| hit일 때 | **write-through (WT)** | 캐시와 메모리에 동시에 씀 | 메모리가 항상 최신 → TX용 clean 불필요 | 쓸 때마다 버스 트래픽 |
| miss일 때 | **write-allocate (WA)** | 라인을 먼저 채우고 캐시에 씀 | 이어지는 쓰기 · 읽기가 hit | 한 번 쓰고 끝나는 데이터가 캐시를 오염 |
| miss일 때 | **no-write-allocate** | 캐시를 건너뛰고 메모리에 바로 씀 | 스트리밍 쓰기에 좋다 | 곧 다시 읽으면 miss |

Cortex-M7은 MPU 속성으로 영역마다 고른다. 흔한 조합은 **WB + read/write-allocate**(일반 SRAM), **WT + read-allocate**(플래시 · 코드 영역 기본값), **non-cacheable**(DMA 버퍼)이다.

중요한 사실 하나: **write-through라도 RX 쪽 일관성 문제는 그대로 있다.** write-through는 "CPU → 메모리" 방향만 맞춰 줄 뿐, DMA가 메모리를 고쳐도 캐시의 옛 복사본은 그대로다. 그래서 WT 영역의 RX 버퍼도 invalidate가 필요하다.

#### 예제 S3 — write-back vs write-through 버스 트래픽, direct-mapped vs 4-way (시뮬레이터)

QEMU에는 캐시가 없으므로 직접 만든 작은 Python 캐시 시뮬레이터 `cachesim.py`(6.4절에 전체 코드)를 쓴다. 라인 32 B, way 수와 WB/WT를 고를 수 있고, 라인 채우기(`bus_rd`)와 메모리 쓰기(`bus_wr`)를 센다.

```python
# s3_policy.py — write-back vs write-through 버스 트래픽, direct-mapped vs 4-way 충돌 미스 (M7형 16 KB, 32 B)
from cachesim import Cache
KB = 1024

def ctrl_loop(wb):                     # 제어 상태 64 B(2라인)를 1 kHz × 1000번 갱신 + 매번 읽기
    mem = bytearray(64 * KB); c = Cache(mem, size=16 * KB, ways=4, wb=wb)
    for t in range(1000):
        for off in range(0, 64, 4):
            c.write(0x1000 + off, (c.read(0x1000 + off) + 1) & 0xFF)
    c.clean(0x1000, 64)                # (WB) 마지막에 한 번 내려 쓴다
    return c.bus_rd, c.bus_wr

for wb in (True, False):
    rd, wr = ctrl_loop(wb)
    print(f"{'write-back   ' if wb else 'write-through'}: line fills {rd:5d}  bus writes {wr:6d}")

def ping_pong(ways, gap):              # 두 배열 a[i], b[i]를 번갈아 읽기 (DSP 필터 계수 × 샘플)
    mem = bytearray(64 * KB); c = Cache(mem, size=16 * KB, ways=ways)
    a, b, n = 0x0000, gap, 2 * KB
    for rep in range(4):
        for i in range(0, n, 4):
            c.read(a + i); c.read(b + i)
    return c.bus_rd

print("two 2 KB arrays read alternately, 4 passes (ideal = 128 fills):")
for gap in (2 * KB, 16 * KB):
    print(f"  b = a + {gap // KB:2d} KB : direct-mapped fills {ping_pong(1, gap):5d}   "
          f"4-way fills {ping_pong(4, gap):5d}")
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B8/sim && python3 -B s3_policy.py'
```

```text
write-back   : line fills     2  bus writes      2
write-through: line fills     2  bus writes  16000
two 2 KB arrays read alternately, 4 passes (ideal = 128 fills):
  b = a +  2 KB : direct-mapped fills   128   4-way fills   128
  b = a + 16 KB : direct-mapped fills  4096   4-way fills   128
```

출력에서 볼 것:

- 제어 상태를 16,000번 쓰는 동안 **WB는 버스 쓰기 2번**(마지막 clean), **WT는 16,000번**이다. WT는 단순하지만 버스 대역폭을 먹고, 같은 버스를 쓰는 DMA를 느리게 만든다.
- 두 배열이 **정확히 캐시 크기(16 KB)만큼 떨어져** 있으면 direct-mapped는 매 접근이 miss(4096번 채우기)다. 4-way는 두 라인이 같은 set의 다른 way에 들어가므로 이상적인 128번이다. DSP 필터 계수와 샘플 버퍼를 링커가 우연히 16 KB 간격으로 놓았을 때 생기는 성능 절벽이 이것이다.

### 1.5 일관성 문제의 근원 — 관찰자가 둘 이상일 때

캐시는 "메모리의 복사본"이다. 복사본이 문제가 되는 건 **같은 메모리를 보는 관찰자(observer)가 둘 이상**일 때뿐이다.

| 관찰자 조합 | 하드웨어가 맞춰 주나 | 예 |
|---|---|---|
| CPU 하나 혼자 | 문제없음 (자기 캐시를 자기가 봄) | 일반 변수 |
| CPU 여러 개 (멀티코어 SoC) | Cortex-A, Apple 칩: **예**(MESI 계열 snoop) | Linux SMP |
| CPU + DMA (Cortex-M7/M85 MCU) | **아니오** — 소프트웨어가 clean/invalidate | UART · SPI · ADC DMA |
| CPU + DMA (Cortex-A + ACE/CCI 포트) | 장치가 coherent 포트에 붙어 있으면 예 | EL H9의 `dma-coherent` |
| 두 개의 M7 (예: 듀얼 코어 MCU의 M7 + M4) | **아니오** — 공유 메모리는 non-cacheable 또는 유지 연산 | STM32H745 M7 ↔ M4 |
| CPU + 자기 자신의 I-cache | **아니오** — 코드를 RAM에 쓰면 D-cache clean + I-cache invalidate | 부트로더가 앱을 복사, 펌웨어 업데이트 |

Cortex-M7/M85 MCU에는 **하드웨어 일관성(snoop)이 없다.** 그래서 이 노트의 6절 전체가 존재한다. Linux는 이 일을 DMA API(`dma_map_single` 등)가 대신 해 준다 — **자세한 내용은 EL H9**. RTOS · bare-metal에서는 드라이버 작성자가 직접 해야 한다.

#### 예제 H3 — 하드웨어 일관성이 있어도 생기는 비용: false sharing (호스트 측정)

하드웨어가 일관성을 맞춰 주는 멀티코어(호스트)에서도, 일관성의 단위가 **라인**이라는 사실은 비용으로 드러난다. 두 스레드가 **서로 다른 변수**를 올리는데 그 두 변수가 같은 라인에 있으면, 라인 소유권이 코어 사이를 계속 오가며 느려진다. 이걸 **false sharing**이라 한다.

```c
/* h3_false_share.c — 두 스레드가 "서로 다른" 카운터를 올린다: 같은 라인 vs 다른 라인 (VM 호스트) */
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#define N 50000000ul
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static _Alignas(256) uint64_t cnt[64];        /* 512 B, 256 B 정렬 */
struct arg { volatile uint64_t *p; };

static void *worker(void *v)
{
  volatile uint64_t *p = ((struct arg *)v)->p;
  for (uint64_t i = 0; i < N; i++) (*p)++;       /* 평범한 load/add/store (원자 연산 아님) */
  return NULL;
}

static double run(unsigned gap_bytes, int threads)
{
  struct arg a0 = { &cnt[0] }, a1 = { &cnt[gap_bytes / 8] };
  pthread_t t0, t1;
  cnt[0] = cnt[gap_bytes / 8] = 0;
  double s = now();
  pthread_create(&t0, NULL, worker, &a0);
  if (threads == 2) pthread_create(&t1, NULL, worker, &a1);
  pthread_join(t0, NULL);
  if (threads == 2) pthread_join(t1, NULL);
  return (now() - s) * 1e9 / N;
}

int main(void)
{
  printf("1 thread alone           : %5.2f ns/inc\n", run(0, 1));
  unsigned gaps[] = { 8, 32, 64, 128, 256 };
  for (unsigned i = 0; i < 5; i++) {
    double best = 1e9;
    for (int r = 0; r < 3; r++) { double t = run(gaps[i], 2); if (t < best) best = t; }
    printf("2 threads, gap %3u B     : %5.2f ns/inc\n", gaps[i], best);
  }
  return 0;
}
```

```text
1 thread alone           :  0.32 ns/inc
2 threads, gap   8 B     :  1.70 ns/inc
2 threads, gap  32 B     :  1.72 ns/inc
2 threads, gap  64 B     :  0.32 ns/inc
2 threads, gap 128 B     :  0.32 ns/inc
2 threads, gap 256 B     :  0.36 ns/inc
```

```svg
<svg viewBox="0 0 640 270" xmlns="http://www.w3.org/2000/svg">
<line x1="150" y1="20" x2="150" y2="240" stroke="currentColor"/>
<text x="142" y="43.3" font-size="12" text-anchor="end">1 스레드 혼자</text>
<rect x="150" y="24.0" width="67.2" height="28.7" fill="#888" fill-opacity="0.75"/>
<text x="223.2" y="43.3" font-size="12">0.32 ns/inc</text>
<text x="142" y="80.0" font-size="12" text-anchor="end">2 스레드 간격 8 B</text>
<rect x="150" y="60.7" width="357.0" height="28.7" fill="#d0564a" fill-opacity="0.75"/>
<text x="513.0" y="80.0" font-size="12">1.7 ns/inc</text>
<text x="142" y="116.7" font-size="12" text-anchor="end">간격 32 B</text>
<rect x="150" y="97.3" width="361.2" height="28.7" fill="#d0564a" fill-opacity="0.75"/>
<text x="517.2" y="116.7" font-size="12">1.72 ns/inc</text>
<text x="142" y="153.3" font-size="12" text-anchor="end">간격 64 B</text>
<rect x="150" y="134.0" width="67.2" height="28.7" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="223.2" y="153.3" font-size="12">0.32 ns/inc</text>
<text x="142" y="190.0" font-size="12" text-anchor="end">간격 128 B</text>
<rect x="150" y="170.7" width="67.2" height="28.7" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="223.2" y="190.0" font-size="12">0.32 ns/inc</text>
<text x="142" y="226.7" font-size="12" text-anchor="end">간격 256 B</text>
<rect x="150" y="207.3" width="75.6" height="28.7" fill="#3f9a6b" fill-opacity="0.75"/>
<text x="231.6" y="226.7" font-size="12">0.36 ns/inc</text>
</svg>
```
그림 5 — 예제 H3 측정값(호스트). 두 카운터가 64 B 안에 있으면 5배 이상 느리다. 64 B 이상 떨어지면 혼자 돌 때와 같다. 일관성의 단위 = L1 라인 64 B(`CTR_EL0.DminLine`과 일치).

출력에서 볼 것: 간격 8 B · 32 B(같은 64 B 라인)는 1.7 ns, 64 B 이상은 0.32 ns. **일관성 단위는 L1 라인(64 B)**이다. H1의 128 B는 메모리 쪽 전송 단위였고, 코어 사이 일관성은 64 B 단위로 움직인다는 걸 두 실험을 합쳐서 알 수 있다.

MCU로 옮기면: 단일 코어 M7에는 false sharing이 없지만, **같은 현상의 MCU판이 "DMA 버퍼와 다른 변수가 한 라인을 공유"하는 문제**다(6.5절). 하드웨어 일관성이 없으니 느려지는 대신 **데이터가 깨진다**. 그리고 듀얼 코어 MCU(STM32H745의 M7 + M4)에서 공유 구조체를 둘 때 32 B 단위로 떼어 놓는 이유도 같다.

### 1.6 흔한 함정

- **"캐시는 투명하다"고 믿기**: 단일 CPU 혼자일 때만 투명하다. DMA · 다른 코어 · 자기 I-cache가 끼면 투명하지 않다.
- **write-through면 DMA 걱정 끝이라고 믿기**: TX는 해결되지만 RX(invalidate)는 여전히 필요하다.
- **라인 크기를 하드코딩**: M7/M55/M85는 32 B로 고정이지만, Cortex-A(64 B)나 다른 MCU 캐시(벤더 캐시 컨트롤러)는 다르다. 이식 코드는 `CTR` 레지스터나 `sys_cache_data_line_size_get()`을 쓴다.
- **측정 없이 "캐시 덕분에 빠를 것"**: 작업 집합이 캐시보다 크면(그림 4의 계단) 캐시는 지연의 분산만 키운다.

---

## 2. Cortex-M7 / M55 / M85의 L1 캐시

### 2.1 사양 한눈에

| 코어 | I-cache | D-cache | 라인 | TCM | 비고 |
|---|---|---|---|---|---|
| Cortex-M0+/M3/M4/M33 | 없음 | 없음 | — | 없음 | 칩 회사가 플래시 가속기(ART, 벤더 캐시)를 따로 붙이기도 한다 |
| Cortex-M7 | 0~64 KB, 2-way | 0~64 KB, 4-way | 32 B | ITCM · DTCM 각각 0~16 MB | 크기는 칩 회사가 구현 시 고름. D-cache는 ECC 옵션 |
| Cortex-M55 | 0~64 KB | 0~64 KB | 32 B | ITCM · DTCM | Armv8.1-M, Helium |
| Cortex-M85 | 0~64 KB | 0~64 KB | 32 B | ITCM · DTCM | Armv8.1-M, Helium, PACBTI |

(코어별 최대 크기 · way 수는 각 코어의 Technical Reference Manual 기준. M55/M85의 way 수는 이 노트에서 확인 못 함 — 아래 실제 칩 표처럼 칩의 RM을 보면 된다.)

실제 칩 예:

| 칩 | 코어 · 클록 | I-cache / D-cache | TCM |
|---|---|---|---|
| STM32F746 | M7 216 MHz | 4 KB / 4 KB | ITCM 16 KB, DTCM 64 KB |
| STM32F767 | M7 216 MHz | 16 KB / 16 KB | ITCM 16 KB, DTCM 128 KB |
| STM32H743 | M7 480 MHz | 16 KB / 16 KB | ITCM 64 KB @ `0x0000_0000`, DTCM 128 KB @ `0x2000_0000` |
| NXP i.MX RT1062 | M7 600 MHz | 32 KB / 32 KB | FlexRAM 512 KB를 ITCM/DTCM/OCRAM으로 나눠 씀 |

H743의 TCM 주소 · 크기는 Zephyr v4.4.2의 `dts/arm/st/h7/stm32h7.dtsi`(`itcm: memory@0`, `dtcm: memory@20000000`)에서 확인했다.

### 2.2 캐시 ID 레지스터 — 칩에게 직접 물어보기

Cortex-M7은 Armv7-M의 캐시 식별 레지스터를 SCB(System Control Block)에 둔다.

| 레지스터 | 주소 | 뜻 |
|---|---|---|
| `CLIDR` | `0xE000ED78` | 어떤 레벨에 어떤 캐시가 있나 |
| `CTR` | `0xE000ED7C` | 최소 I/D 라인 크기(유지 루프 간격) |
| `CCSIDR` | `0xE000ED80` | `CSSELR`이 고른 캐시의 라인 크기 · way 수 · set 수 |
| `CSSELR` | `0xE000ED84` | 0 = L1 D-cache, 1 = L1 I-cache 선택 |
| `CCR` | `0xE000ED14` | bit 16 `DC` = D-cache 켜짐, bit 17 `IC` = I-cache 켜짐 |

`CCSIDR` 해석: `LineSize`(비트 2:0) = log2(라인 워드 수) − 2, `Associativity`(12:3) = way 수 − 1, `NumSets`(27:13) = set 수 − 1. 16 KB · 4-way · 32 B D-cache라면 LineSize = 1, Associativity = 3, NumSets = 127이다.

#### 예제 1 — M7의 캐시 ID를 읽고 캐시를 켠다 (QEMU mps2-an500, 실행되지만 캐시 효과는 QEMU에서 안 보임)

M7용 템플릿은 없으므로 공용 템플릿의 startup · board 코드를 그대로 쓰고(`mps2-an500`은 AN385와 같은 메모리 맵 · UART · 타이머를 쓴다), CMSIS 6의 `core_cm7.h`를 쓰기 위해 최소 "디바이스 헤더"를 만든다.

```c
/* b8_cm7.h — QEMU mps2-an500(Cortex-M7)용 최소 CMSIS "디바이스 헤더" */
#ifndef B8_CM7_H
#define B8_CM7_H
typedef enum {
  NonMaskableInt_IRQn = -14, HardFault_IRQn = -13, MemoryManagement_IRQn = -12,
  BusFault_IRQn = -11, UsageFault_IRQn = -10, SVCall_IRQn = -5,
  DebugMonitor_IRQn = -4, PendSV_IRQn = -2, SysTick_IRQn = -1,
  UART0RX_IRQn = 0, TIMER0_IRQn = 8, TIMER1_IRQn = 9
} IRQn_Type;
#define __CM7_REV              0x0101U
#define __MPU_PRESENT          1U
#define __VTOR_PRESENT         1U
#define __NVIC_PRIO_BITS       3U
#define __Vendor_SysTickConfig 0U
#define __FPU_PRESENT          0U   /* 이 랩은 soft-float로 빌드 */
#define __ICACHE_PRESENT       1U
#define __DCACHE_PRESENT       1U
#define __DTCM_PRESENT         1U
#include "core_cm7.h"
#endif
```

```c
/* ex1_probe.c — M7의 캐시 ID 레지스터를 읽고 캐시를 켠다 (QEMU mps2-an500) */
#include <stdio.h>
#include "board.h"
#include "b8_cm7.h"

static void show_level1(const char *name, uint32_t csselr)
{
  SCB->CSSELR = csselr;            /* 0 = L1 D-cache, 1 = L1 I-cache */
  __DSB();
  uint32_t ccsidr = SCB->CCSIDR;
  uint32_t line  = 1u << ((ccsidr & 7u) + 4);          /* LineSize: log2(words)-2 */
  uint32_t ways  = ((ccsidr >> 3) & 0x3FFu) + 1;
  uint32_t sets  = ((ccsidr >> 13) & 0x7FFFu) + 1;
  printf("%s CCSIDR=0x%08lx line=%lu B ways=%lu sets=%lu -> %lu KB\n", name,
         (unsigned long)ccsidr, (unsigned long)line, (unsigned long)ways,
         (unsigned long)sets, (unsigned long)(line * ways * sets / 1024));
}

int main(void)
{
  printf("CPUID=0x%08lx (partno 0x%03lx)\n", (unsigned long)SCB->CPUID,
         (unsigned long)((SCB->CPUID >> 4) & 0xFFFu));
  printf("CLIDR=0x%08lx CTR=0x%08lx\n", (unsigned long)SCB->CLIDR,
         (unsigned long)SCB->CTR);
  printf("CTR DminLine=%lu B IminLine=%lu B\n",
         (unsigned long)(4u << ((SCB->CTR >> 16) & 0xFu)),
         (unsigned long)(4u << (SCB->CTR & 0xFu)));
  show_level1("L1-D", 0);
  show_level1("L1-I", 1);
  printf("before: CCR=0x%08lx IC=%lu DC=%lu\n", (unsigned long)SCB->CCR,
         (unsigned long)((SCB->CCR >> SCB_CCR_IC_Pos) & 1u),
         (unsigned long)((SCB->CCR >> SCB_CCR_DC_Pos) & 1u));
  SCB_EnableICache();
  SCB_EnableDCache();               /* 내부에서 set/way 전체 invalidate 후 CCR.DC=1 */
  printf("after : CCR=0x%08lx IC=%lu DC=%lu\n", (unsigned long)SCB->CCR,
         (unsigned long)((SCB->CCR >> SCB_CCR_IC_Pos) & 1u),
         (unsigned long)((SCB->CCR >> SCB_CCR_DC_Pos) & 1u));
  return 0;
}
```

빌드 스크립트(`mk_m7.sh`)는 공용 bare-metal Makefile에 `-mcpu=cortex-m7 -mfloat-abi=soft`와 CMSIS 6 include 경로를 덧붙이고(뒤에 오는 `-mcpu`가 이긴다), 실행 스크립트(`run_m7.sh`)는 `rtos-run-mps2`와 같은 옵션에 `-machine mps2-an500 -cpu cortex-m7`만 바꾼다.

```sh
# mk_m7.sh <name> <srcs...>
make -s -B -f /var/rtoslab/templates/baremetal-mps2/Makefile SRCS="$*" OUT=/var/tmp/rtos/B8/$N \
  EXTRA_CFLAGS="-mcpu=cortex-m7 -mfloat-abi=soft -I/var/rtoslab/zephyrproject/modules/hal/cmsis_6/CMSIS/Core/Include"
# run_m7.sh app.elf
timeout --foreground 20 qemu-system-arm -machine mps2-an500 -cpu cortex-m7 -nographic -monitor none \
  -serial stdio -semihosting-config enable=on,target=native [-icount shift=5,align=off,sleep=off] -kernel app.elf
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B8 && ./mk_m7.sh ex1 /Users/donh/.rtos_scratch/B8/ex1_probe.c && ./run_m7.sh /var/tmp/rtos/B8/ex1/app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
   8260	     96	    328	   8684	   21ec	/var/tmp/rtos/B8/ex1/app.elf
CPUID=0x411fc272 (partno 0xc27)
CLIDR=0x00000000 CTR=0x00000000
CTR DminLine=4 B IminLine=4 B
L1-D CCSIDR=0x00000000 line=16 B ways=1 sets=1 -> 0 KB
L1-I CCSIDR=0x00000000 line=16 B ways=1 sets=1 -> 0 KB
before: CCR=0x00000200 IC=0 DC=0
after : CCR=0x00000200 IC=0 DC=0
[board_exit] code=0
```

출력에서 볼 것 — **QEMU가 캐시를 모델링하지 않는다는 직접 증거**다.

- `CPUID=0x411fc272`: part number `0xC27` = Cortex-M7, variant 1 · revision 2 = **r1p2**. CPU는 진짜 M7로 흉내 낸다.
- 그런데 `CLIDR` · `CTR` · `CCSIDR`가 전부 0이고, `SCB_EnableDCache()` 뒤에도 `CCR.DC`가 0이다. QEMU의 M7은 **캐시가 0 KB인 M7**처럼 행동한다(캐시 레지스터는 RAZ/WI — 읽으면 0, 쓰면 무시).
- 그래서 CMSIS의 `SCB_EnableDCache()`는 CCSIDR=0에서 set 1개 · way 1개만 돌고 끝난다. 실제 H743이라면 D-cache 16 KB · 4-way · 32 B → 위 프로그램은 `line=32 B ways=4 sets=128 -> 16 KB`를 찍고 `DC=1`이 된다(실칩에서 확인하지 못했다 — CCSIDR 해석 규칙으로 계산한 예상값).
- `CCR=0x200`은 bit 9 `STKALIGN`(예외 진입 시 8바이트 스택 정렬)이다.

### 2.3 켜는 순서 — `SCB_EnableDCache()`의 내부

CMSIS 6 `m-profile/armv7m_cachel1.h`의 `SCB_EnableDCache()`는 이렇게 동작한다(랩의 소스에서 그대로 확인).

1. `CCR.DC`가 이미 1이면 바로 리턴.
2. `CSSELR = 0`(L1 D 선택) → `DSB` → `CCSIDR` 읽기.
3. 모든 set × 모든 way에 `DCISW`(set/way로 invalidate)를 쓴다 — **리셋 직후 캐시 RAM 내용은 쓰레기**이므로 valid 비트를 모두 지운다.
4. `DSB` → `CCR.DC = 1` → `DSB` → `ISB`.

I-cache는 `SCB_EnableICache()`가 `ICIALLU`(I-cache 전체 invalidate) 후 `CCR.IC = 1`.

**순서 규칙** (bring-up 체크리스트):

1. 클록 · 플래시 wait state 설정(5절)
2. **MPU 설정**(DMA 버퍼를 non-cacheable로, 3절) → `ARM_MPU_Enable`
3. `SCB_EnableICache()` → `SCB_EnableDCache()`
4. 그 다음에 DMA를 시작한다

MPU보다 캐시를 먼저 켜면, 그 사이에 DMA 버퍼 주소가 캐시에 들어갈 수 있다. 그 상태에서 영역을 non-cacheable로 바꾸면 **이미 캐시에 있던 dirty 라인은 그대로 남는다**(속성을 바꿔도 캐시 내용이 자동으로 정리되지 않는다). 순서를 지키거나, 속성을 바꾸기 전에 그 영역을 clean+invalidate한다.

### 2.4 유지 연산 — by address와 by set/way

| 연산 | 레지스터 (Armv7-M) | CMSIS 함수 | 하는 일 | 언제 |
|---|---|---|---|---|
| D clean by address | `DCCMVAC` `0xE000EF68` | `SCB_CleanDCache_by_Addr(addr, size)` | 그 라인이 dirty면 메모리에 쓴다. 라인은 남는다 | **DMA가 메모리를 읽기 전(TX)** |
| D invalidate by address | `DCIMVAC` `0xE000EF5C` | `SCB_InvalidateDCache_by_Addr(addr, size)` | 그 라인을 버린다 (dirty여도 안 쓰고 버림) | **DMA가 메모리에 쓴 뒤 CPU가 읽기 전(RX)** |
| D clean+invalidate by address | `DCCIMVAC` `0xE000EF70` | `SCB_CleanInvalidateDCache_by_Addr(addr, size)` | 쓰고 나서 버린다 | RX 버퍼를 DMA에 넘기기 전, 양방향 버퍼 |
| D invalidate/clean/both by set/way | `DCISW` / `DCCSW` / `DCCISW` | `SCB_InvalidateDCache()` / `SCB_CleanDCache()` / `SCB_CleanInvalidateDCache()` | 캐시 전체를 set · way 순회 | 켜기 전, 끄기 전, 큰 버퍼 |
| I invalidate all | `ICIALLU` `0xE000EF50` | `SCB_InvalidateICache()` | I-cache 전체 버림 | 코드를 RAM에 복사 · 수정한 뒤 |

용어 두 개:

- **PoC(Point of Coherency)**: CPU와 DMA 같은 모든 관찰자가 같은 값을 보게 되는 지점. M7에서는 L1 캐시 바로 바깥(버스)이다. M7의 by-address 연산은 PoC까지 간다.
- **by address vs by set/way**: by address는 "이 주소의 라인"만, set/way는 "캐시 안의 자리" 단위로 전부. 작은 버퍼는 by address, 캐시 크기보다 큰 버퍼는 set/way가 싸다(예제 2).

**배리어**: CMSIS 함수는 앞뒤로 `__DSB()`(앞선 메모리 접근 · 유지 연산이 끝날 때까지 기다림)와 `__ISB()`를 넣는다. 직접 레지스터를 쓰면 이 배리어를 빠뜨리기 쉽다. DMA 시작 레지스터를 쓰기 전에 clean이 **완료**돼야 하므로 `DSB`가 핵심이다.

**CMSIS의 정렬 처리** — 소스를 보면 이렇다.

```c
/* CMSIS 6 m-profile/armv7m_cachel1.h — SCB_InvalidateDCache_by_Addr (발췌) */
       int32_t op_size = dsize + (((uint32_t)addr) & (__SCB_DCACHE_LINE_SIZE - 1U));
      uint32_t op_addr = (uint32_t)addr /* & ~(__SCB_DCACHE_LINE_SIZE - 1U) */;
      __DSB();
      do {
        SCB->DCIMVAC = op_addr;             /* register accepts only 32byte aligned values, only bits 31..5 are valid */
        op_addr += __SCB_DCACHE_LINE_SIZE;
        op_size -= __SCB_DCACHE_LINE_SIZE;
      } while ( op_size > 0 );
      __DSB();
      __ISB();
```

주소를 내림하지 않고 넘기지만 레지스터가 하위 5비트를 무시하고, 크기에 어긋난 만큼을 더하므로 결과적으로 **버퍼가 걸친 모든 라인**을 처리한다. 즉 **CMSIS 함수는 정렬 안 된 버퍼도 "받아 준다". 그래서 위험하다** — 버퍼와 라인을 공유하는 이웃 변수까지 invalidate된다(6.5절).

#### 예제 2 — 정렬 계산과 유지 연산의 "명령 수" 비용 (QEMU mps2-an500, 실행되지만 캐시 효과는 QEMU에서 안 보임)

무엇을 확인하나: (1) 임의 주소 · 길이가 라인 몇 개에 걸치는지, (2) by-address와 set/way 전체 연산이 **명령을 몇 개나 실행하는지**. QEMU에는 캐시가 없으니 dirty 라인 write-back 시간은 빠져 있고, 오직 "유지 루프 자체의 명령 수"만 잰다 — 그래도 by-address와 set/way의 손익분기점은 이것으로 정해지는 부분이 크다.

```c
/* ex2_maint.c — by-address 캐시 유지 함수의 정렬 처리와 "명령 수" 비용 (QEMU: 캐시 효과 없음) */
#include <stdio.h>
#include "board.h"
#include "b8_cm7.h"
#define FPGA_CNT REG32(0x40028018u)          /* 25 MHz 카운터 (ICOUNT=1이면 결정적) */
#define LINE 32u

static uint8_t rx_ok[512]  __attribute__((aligned(32)));   /* 32 B 정렬 + 32의 배수 */
static uint8_t big[64 * 1024] __attribute__((aligned(32)));

/* 임의 주소·길이를 라인 경계로 넓힌다 — CMSIS 함수도 내부에서 같은 일을 한다 */
static void span(const void *p, uint32_t n, uint32_t *a, uint32_t *len)
{
  uint32_t s = (uint32_t)p & ~(LINE - 1u);
  uint32_t e = ((uint32_t)p + n + LINE - 1u) & ~(LINE - 1u);
  *a = s; *len = e - s;
}

/* 실제 M7 16 KB 4-way 32 B 라인 기하(128 sets)로 set/way 전체 clean+invalidate */
static void clean_inv_all_sw(void)
{
  __DSB();
  for (uint32_t set = 0; set < 128; set++)
    for (uint32_t way = 0; way < 4; way++)
      SCB->DCCISW = (way << 30) | (set << 5);
  __DSB(); __ISB();
}

#define MEASURE(label, stmt) do { uint32_t t0 = FPGA_CNT; stmt; uint32_t t1 = FPGA_CNT; \
  printf("%-34s %6lu ticks\n", label, (unsigned long)(t1 - t0)); } while (0)

int main(void)
{
  uint32_t a, n;
  span((void *)0x2000011A, 10, &a, &n);
  printf("span(0x2000011A,10) -> addr=0x%08lx len=%lu (lines=%lu)\n",
         (unsigned long)a, (unsigned long)n, (unsigned long)(n / LINE));
  span(rx_ok, sizeof rx_ok, &a, &n);
  printf("span(rx_ok,512)     -> addr=0x%08lx len=%lu (lines=%lu)\n",
         (unsigned long)a, (unsigned long)n, (unsigned long)(n / LINE));

  SCB_EnableDCache();                       /* QEMU에서는 CCR.DC가 0으로 남는다 */
  MEASURE("Clean_by_Addr 512 B", SCB_CleanDCache_by_Addr(rx_ok, sizeof rx_ok));
  MEASURE("Invalidate_by_Addr 512 B", SCB_InvalidateDCache_by_Addr(rx_ok, sizeof rx_ok));
  MEASURE("CleanInvalidate_by_Addr 64 KB", SCB_CleanInvalidateDCache_by_Addr(big, sizeof big));
  MEASURE("set/way all (128x4, M7 16KB)", clean_inv_all_sw());
  MEASURE("CMSIS SCB_CleanInvalidateDCache", SCB_CleanInvalidateDCache());
  for (uint32_t n = 512; n <= sizeof big; n *= 2) {
    uint32_t t0 = FPGA_CNT; SCB_CleanInvalidateDCache_by_Addr(big, (int32_t)n); uint32_t t1 = FPGA_CNT;
    printf("sweep by_Addr %6lu B: %5lu ticks\n", (unsigned long)n, (unsigned long)(t1 - t0));
  }
  printf("CCR.DC=%lu  (QEMU: RAZ/WI -> no cache modeled)\n",
         (unsigned long)((SCB->CCR >> SCB_CCR_DC_Pos) & 1u));
  return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B8 && ./mk_m7.sh ex2 /Users/donh/.rtos_scratch/B8/ex2_maint.c && ICOUNT=1 ./run_m7.sh /var/tmp/rtos/B8/ex2/app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
   8572	     96	  66376	  75044	  12524	/var/tmp/rtos/B8/ex2/app.elf
span(0x2000011A,10) -> addr=0x20000100 len=64 (lines=2)
span(rx_ok,512)     -> addr=0x20010060 len=512 (lines=16)
Clean_by_Addr 512 B                    58 ticks
Invalidate_by_Addr 512 B               57 ticks
CleanInvalidate_by_Addr 64 KB        6561 ticks
set/way all (128x4, M7 16KB)         1031 ticks
CMSIS SCB_CleanInvalidateDCache        20 ticks
sweep by_Addr    512 B:    57 ticks
sweep by_Addr   1024 B:   108 ticks
sweep by_Addr   2048 B:   211 ticks
sweep by_Addr   4096 B:   415 ticks
sweep by_Addr   8192 B:   824 ticks
sweep by_Addr  16384 B:  1644 ticks
sweep by_Addr  32768 B:  3282 ticks
sweep by_Addr  65536 B:  6559 ticks
CCR.DC=0  (QEMU: RAZ/WI -> no cache modeled)
[board_exit] code=0
```

출력에서 볼 것:

- **10바이트가 라인 2개에 걸친다.** `0x2000011A`부터 10바이트는 `0x11A~0x123` → 라인 `0x100`과 `0x120`. "작은 버퍼니까 라인 하나"가 아니다.
- by-address 비용은 **라인 수에 정비례**(라인당 약 3.2 tick = 128 ns, icount 기준). set/way 전체는 크기와 무관하게 1031 tick. **약 10 KB를 넘으면 set/way 전체가 더 싸다.** 캐시(16 KB)보다 큰 버퍼를 by-address로 도는 건 낭비다.
- CMSIS `SCB_CleanInvalidateDCache()`가 20 tick인 이유: QEMU의 CCSIDR=0이라 set 1개 · way 1개만 돌았다. 실칩에서는 위 `clean_inv_all_sw`와 비슷해진다.
- 실칩에서는 여기에 **dirty 라인을 메모리에 써 내는 시간**이 더해진다(라인당 버스 쓰기 한 번). QEMU로는 잴 수 없다.
- ICOUNT 실행에서도 두 번 돌리면 1 tick 정도 흔들린다(FPGAIO 카운터와 icount 시계의 반올림 차이).

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="240" x2="620" y2="240" stroke="currentColor"/>
<line x1="70" y1="25" x2="70" y2="240" stroke="currentColor"/>
<text x="64" y="244.0" font-size="12" text-anchor="end">10</text>
<line x1="70" y1="240.0" x2="620" y2="240.0" stroke="#888" stroke-opacity="0.4" stroke-dasharray="3,3"/>
<text x="64" y="172.3" font-size="12" text-anchor="end">100</text>
<line x1="70" y1="168.3" x2="620" y2="168.3" stroke="#888" stroke-opacity="0.4" stroke-dasharray="3,3"/>
<text x="64" y="100.7" font-size="12" text-anchor="end">1000</text>
<line x1="70" y1="96.7" x2="620" y2="96.7" stroke="#888" stroke-opacity="0.4" stroke-dasharray="3,3"/>
<text x="64" y="29.0" font-size="12" text-anchor="end">10000</text>
<line x1="70" y1="25.0" x2="620" y2="25.0" stroke="#888" stroke-opacity="0.4" stroke-dasharray="3,3"/>
<text x="70.0" y="258" font-size="12" text-anchor="middle">512</text>
<text x="148.6" y="258" font-size="12" text-anchor="middle">1K</text>
<text x="227.1" y="258" font-size="12" text-anchor="middle">2K</text>
<text x="305.7" y="258" font-size="12" text-anchor="middle">4K</text>
<text x="384.3" y="258" font-size="12" text-anchor="middle">8K</text>
<text x="462.9" y="258" font-size="12" text-anchor="middle">16K</text>
<text x="541.4" y="258" font-size="12" text-anchor="middle">32K</text>
<text x="620.0" y="258" font-size="12" text-anchor="middle">64K</text>
<text x="345" y="280" font-size="13" text-anchor="middle">버퍼 크기 (바이트) — 세로축: FPGAIO 25 MHz tick (log)</text>
<polyline points="70.0,185.8 148.6,165.9 227.1,145.1 305.7,124.0 384.3,102.7 462.9,81.2 541.4,59.7 620.0,38.1" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="70.0" cy="185.8" r="3" fill="#4a7bd0"/>
<circle cx="148.6" cy="165.9" r="3" fill="#4a7bd0"/>
<circle cx="227.1" cy="145.1" r="3" fill="#4a7bd0"/>
<circle cx="305.7" cy="124.0" r="3" fill="#4a7bd0"/>
<circle cx="384.3" cy="102.7" r="3" fill="#4a7bd0"/>
<circle cx="462.9" cy="81.2" r="3" fill="#4a7bd0"/>
<circle cx="541.4" cy="59.7" r="3" fill="#4a7bd0"/>
<circle cx="620.0" cy="38.1" r="3" fill="#4a7bd0"/>
<line x1="70" y1="95.7" x2="620" y2="95.7" stroke="#e08a3c" stroke-width="2"/>
<text x="78" y="87.7" font-size="12">set/way 전체 (128 set × 4 way) = 1031 tick</text>
<text x="148.6" y="187.9" font-size="12">by_Addr: 라인당 약 3.2 tick</text>
<line x1="409.6" y1="25" x2="409.6" y2="240" stroke="#d0564a" stroke-dasharray="4,3"/>
<text x="415.6" y="37" font-size="12">교차 ≈ 10 KB</text>
</svg>
```
그림 6 — 예제 2 측정값(QEMU icount, 명령 수만 반영). by-address는 크기에 비례하고 set/way 전체는 상수다. 교차점 약 10 KB. 실칩의 write-back 시간은 빠져 있다.

#### 예제 H4 — 진짜 하드웨어에서 "clean by address" 한 번의 비용 (호스트 측정)

QEMU에서 못 재는 걸 호스트에서 잰다. arm64 Linux는 사용자 모드에서 `dc cvac`(clean by VA to PoC)와 `dc civac`(clean+invalidate)를 허용한다(`SCTLR_EL1.UCI`). M7의 `DCCMVAC` · `DCCIMVAC`와 같은 연산이다. dirty 라인을 clean할 때와, 이미 clean한 라인을 또 clean할 때를 비교한다.

```c
/* h4_clean_cost.c — "clean by address"(dc cvac) 비용: dirty 라인 vs clean 라인 (VM 호스트 arm64, EL0) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#define LINE 64u                                /* CTR_EL0.DminLine */
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static void clean_range(void *p, size_t n, int inval)   /* SCB_Clean(Invalidate)DCache_by_Addr의 arm64판 */
{
  uintptr_t a = (uintptr_t)p & ~(uintptr_t)(LINE - 1), e = (uintptr_t)p + n;
  __asm volatile("dsb ish" ::: "memory");
  for (; a < e; a += LINE) {
    if (inval) __asm volatile("dc civac, %0" :: "r"(a) : "memory");
    else       __asm volatile("dc cvac, %0"  :: "r"(a) : "memory");
  }
  __asm volatile("dsb ish" ::: "memory");
}

int main(void)
{
  size_t sizes[] = { 4096, 32768, 262144, 2097152 };
  uint8_t *b = aligned_alloc(4096, 2097152);
  memset(b, 1, 2097152);
  for (int k = 0; k < 4; k++) {
    size_t n = sizes[k], lines = n / LINE;
    double td = 1e9, tc = 1e9, ti = 1e9;
    for (int r = 0; r < 5; r++) {
      memset(b, r, n);                          /* 모든 라인을 dirty로 */
      double t0 = now(); clean_range(b, n, 0); double t1 = now();
      clean_range(b, n, 0);                     /* 이미 clean → 두 번째 clean */
      double t2 = now();
      memset(b, r, n);
      double t3 = now(); clean_range(b, n, 1); double t4 = now();
      if (t1 - t0 < td) td = t1 - t0;
      if (t2 - t1 < tc) tc = t2 - t1;
      if (t4 - t3 < ti) ti = t4 - t3;
    }
    printf("%7zu B (%5zu lines): clean dirty %6.2f ns/line  clean already-clean %5.2f  "
           "clean+inval dirty %6.2f  | total dirty clean %7.1f us\n", n, lines,
           td * 1e9 / lines, tc * 1e9 / lines, ti * 1e9 / lines, td * 1e6);
  }
  return 0;
}
```

```text
   4096 B (   64 lines): clean dirty   1.95 ns/line  clean already-clean  1.95  clean+inval dirty   1.95  | total dirty clean     0.1 us
  32768 B (  512 lines): clean dirty   1.63 ns/line  clean already-clean  1.63  clean+inval dirty   1.63  | total dirty clean     0.8 us
 262144 B ( 4096 lines): clean dirty   1.57 ns/line  clean already-clean  1.56  clean+inval dirty   1.57  | total dirty clean     6.4 us
2097152 B (32768 lines): clean dirty   1.58 ns/line  clean already-clean  1.58  clean+inval dirty   1.56  | total dirty clean    51.9 us
```

출력에서 볼 것: 라인당 약 1.6 ns로 **dirty든 아니든 같다.** 이건 Apple 칩의 특성이다 — 시스템 전체가 하드웨어 일관성을 가진 SoC라서 PoC가 DRAM이 아니라 칩 안쪽(공유 캐시 · 패브릭)에 있고, dirty 데이터를 DRAM까지 써 낼 필요가 없는 것으로 보인다(추정, 공개 문서로 확인 못 함). **결론: 이 호스트에서는 write-back 비용이 clean 시간으로 드러나지 않는다.** 정직하게 말하면 "clean 명령 한 번 = 몇 cycle"까지만 보였고, MCU에서 dirty 라인 clean이 버스 쓰기를 일으키는 비용은 이 랩에서 측정할 수 없다. 대신 크기 비례성(2 MB = 52 µs)은 그대로 보인다 — **큰 버퍼를 by-address로 도는 비용은 실하드웨어에서도 크기에 비례한다.** WB가 줄여 주는 버스 쓰기 횟수는 예제 S3(시뮬레이터)에서 셌다.

### 2.5 흔한 함정

- **I-cache 잊기**: 부트로더가 앱 이미지를 RAM에 복사하거나, `.ramfunc`/ITCM이 아닌 캐시 가능 RAM으로 코드를 복사하고 실행하면 → `SCB_CleanDCache_by_Addr`(코드가 D-cache에 dirty로 남아 있으므로) + `SCB_InvalidateICache()` + `__ISB()`가 필요하다. 빠뜨리면 옛 코드 · 쓰레기를 실행한다.
- **`SCB_DisableDCache()` 없이 다른 이미지로 점프**: 부트로더가 캐시를 켠 채 앱으로 점프하면 dirty 라인이 남아 있다. 부트로더는 점프 전에 clean+invalidate 후 끄거나, 앱이 켜진 상태를 가정하지 않게 한다.
- **set/way 연산을 "다른 코어나 DMA와 동기화"로 착각**: set/way는 "내 캐시의 자리 전부"를 처리하는 유지 · 초기화용이다. DMA 버퍼 하나에는 by-address가 맞다.
- **`dsize`를 `int32_t`로 넘기는 것**: CMSIS 함수의 크기 인자는 부호 있는 `int32_t`이고 `dsize > 0`일 때만 동작한다. 0이나 음수면 조용히 아무것도 안 한다.

---

## 3. MPU 메모리 속성 — 캐시 정책은 MPU가 정한다

### 3.1 직관 — 주소마다 붙는 "취급 주의" 라벨

Cortex-M7의 캐시는 "무조건 캐시"가 아니다. 주소마다 **메모리 타입**과 **캐시 정책** 라벨이 붙어 있고, 캐시는 그 라벨을 보고 행동한다. 라벨은 (1) MPU가 꺼져 있거나 MPU 영역 밖이면 **아키텍처 기본 메모리 맵**이, (2) MPU 영역 안이면 **그 영역의 속성**이 정한다.

메모리 타입 세 가지:

| 타입 | 뜻 | 캐시 | 순서 · 투기 | 용도 |
|---|---|---|---|---|
| **Normal** | 평범한 메모리 | WB/WT/non-cacheable 중 선택 | CPU가 순서를 바꾸고 미리 읽을(speculative read) 수 있다 | SRAM, 플래시, SDRAM |
| **Device** | 읽기 · 쓰기 자체가 부작용인 레지스터 | 캐시 안 됨 | 투기 읽기 없음, 같은 장치 접근 순서 유지 | 주변장치 레지스터 |
| **Strongly-ordered** | 가장 엄격 | 캐시 안 됨 | 모든 접근이 순서대로, 쓰기 버퍼도 안 씀 | 시스템 영역(`0xE000_0000` PPB) |

그리고 **Shareable** 속성: "다른 관찰자와 공유하는 메모리"라는 표시다. 여기에 M7 특유의 함정이 있다. **Cortex-M7은 기본적으로 Normal · Shareable 영역을 D-cache에 넣지 않는다**(M7 TRM의 `CACR.SIWT` 설명 — SIWT = 1로 바꾸면 shareable 영역을 write-through로 캐시한다). 즉 MPU에서 S = 1을 주면 그 영역은 사실상 non-cacheable처럼 동작한다. 의도치 않게 S = 1을 줘서 "캐시를 켰는데 성능이 안 오른다"는 경우가 있고, 반대로 이 성질을 DMA 버퍼에 일부러 쓰는 코드도 있다. 명시적으로 non-cacheable(TEX = 1, C = 0, B = 0)로 쓰는 편이 읽는 사람에게 분명하다.

### 3.2 Armv7-M 기본 메모리 맵의 속성 (MPU가 꺼졌거나 `PRIVDEFENA`로 기본 맵을 쓸 때)

| 주소 범위 | 이름 | 기본 타입 · 캐시 |
|---|---|---|
| `0x0000_0000`~`0x1FFF_FFFF` | Code | Normal, **write-through** |
| `0x2000_0000`~`0x3FFF_FFFF` | SRAM | Normal, **write-back, write-allocate** |
| `0x4000_0000`~`0x5FFF_FFFF` | Peripheral | Device |
| `0x6000_0000`~`0x7FFF_FFFF` | External RAM | Normal, WB-WA |
| `0x8000_0000`~`0x9FFF_FFFF` | External RAM | Normal, WT |
| `0xA000_0000`~`0xDFFF_FFFF` | External device | Device |
| `0xE000_0000`~`0xFFFF_FFFF` | System (PPB 등) | Strongly-ordered / Device |

(Armv7-M Architecture Reference Manual의 기본 메모리 맵 표 기준.)

여기서 바로 나오는 결론: **STM32H7의 AXI SRAM(`0x2400_0000`)과 D2 SRAM(`0x3000_0000`)은 MPU를 안 건드려도 WB-WA 캐시 대상이다.** 캐시를 켜는 순간 이 영역의 DMA 버퍼는 전부 일관성 문제를 갖는다. 그리고 `0x2000_0000`의 DTCM은 SRAM 영역 주소지만 **TCM은 애초에 캐시를 거치지 않는다**(4절).

### 3.3 Armv7-M MPU의 TEX · C · B · S 인코딩

| TEX | C | B | 의미 |
|---|---|---|---|
| 000 | 0 | 0 | Strongly-ordered |
| 000 | 0 | 1 | Device (shareable) |
| 000 | 1 | 0 | Normal, WT, no write-allocate |
| 000 | 1 | 1 | Normal, WB, no write-allocate |
| 001 | 0 | 0 | **Normal, non-cacheable** ← DMA 버퍼 |
| 001 | 1 | 1 | **Normal, WB, read/write-allocate** ← 일반 SRAM |
| 010 | 0 | 0 | Device (non-shareable) |

(FreeRTOS V11.3.2 `portable/GCC/ARM_CM4_MPU/portmacro.h`의 주석 표에도 같은 표가 있다.) Armv7-M MPU 영역의 규칙: **크기는 2의 거듭제곱(최소 32 B), 시작 주소는 크기에 정렬**, 번호가 큰 영역이 겹치는 부분에서 이긴다, 8개의 sub-region을 끌 수 있다(SRD).

#### 예제 3 — MPU로 DMA 버퍼를 non-cacheable로, guard는 no-access로 (QEMU mps2-an500, MPU는 진짜로 동작 · 캐시 효과는 안 보임)

무엇을 확인하나: CMSIS `ARM_MPU_RASR` 매크로가 만드는 레지스터 값, 그리고 QEMU가 MPU 권한 검사는 진짜로 모델링한다는 것(guard 접근 → MemManage).

```c
/* ex3_mpu.c — MPU로 DMA 버퍼를 Normal non-cacheable로, guard는 no-access로 (QEMU an500 M7) */
#include <stdio.h>
#include "board.h"
#include "b8_cm7.h"

static uint8_t dma_buf[1024] __attribute__((aligned(1024)));   /* MPU 영역은 크기에 정렬 */
static uint8_t guard[32]     __attribute__((aligned(32)));

void MemManage_Handler(void)
{
  printf("MemManage! CFSR=0x%08lx MMFAR=0x%08lx (guard=%p)\n",
         (unsigned long)SCB->CFSR, (unsigned long)SCB->MMFAR, (void *)guard);
  board_exit(0);
}

int main(void)
{
  printf("MPU TYPE=0x%08lx -> %lu regions\n", (unsigned long)MPU->TYPE,
         (unsigned long)((MPU->TYPE >> 8) & 0xFFu));
  ARM_MPU_Disable();
  /* 0: 전체 SRAM 4 MB = Normal, WB-WA (TEX=1 C=1 B=1), 실행 금지 */
  ARM_MPU_SetRegion(ARM_MPU_RBAR(0, 0x20000000u),
      ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 1, 0, 1, 1, 0, ARM_MPU_REGION_SIZE_4MB));
  /* 1: DMA 버퍼 1 KB = Normal non-cacheable (TEX=1 C=0 B=0), shareable */
  ARM_MPU_SetRegion(ARM_MPU_RBAR(1, (uint32_t)dma_buf),
      ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 1, 1, 0, 0, 0, ARM_MPU_REGION_SIZE_1KB));
  /* 2: guard 32 B = no access (스택 끝 보호 같은 용도) */
  ARM_MPU_SetRegion(ARM_MPU_RBAR(2, (uint32_t)guard),
      ARM_MPU_RASR(1, ARM_MPU_AP_NONE, 0, 0, 0, 0, 0, ARM_MPU_REGION_SIZE_32B));
  ARM_MPU_Enable(MPU_CTRL_PRIVDEFENA_Msk);      /* 나머지 주소는 기본 맵 */
  SCB->SHCSR |= SCB_SHCSR_MEMFAULTENA_Msk;

  for (uint32_t r = 0; r < 3; r++) {
    MPU->RNR = r;
    uint32_t rasr = MPU->RASR;
    printf("R%lu RBAR=0x%08lx RASR=0x%08lx TEX=%lu S=%lu C=%lu B=%lu AP=%lu\n",
           (unsigned long)r, (unsigned long)MPU->RBAR, (unsigned long)rasr,
           (unsigned long)((rasr >> 19) & 7u), (unsigned long)((rasr >> 18) & 1u),
           (unsigned long)((rasr >> 17) & 1u), (unsigned long)((rasr >> 16) & 1u),
           (unsigned long)((rasr >> 24) & 7u));
  }
  dma_buf[0] = 0xA5;                            /* non-cacheable 영역: 정상 접근 */
  printf("dma_buf[0]=0x%02x ok, now touching guard...\n", dma_buf[0]);
  volatile uint8_t *g = guard;
  g[4] = 1;                                     /* → MemManage (DACCVIOL) */
  printf("not reached\n");
  return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B8 && ./mk_m7.sh ex3 /Users/donh/.rtos_scratch/B8/ex3_mpu.c && ./run_m7.sh /var/tmp/rtos/B8/ex3/app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
   8340	     96	   2376	  10812	   2a3c	/var/tmp/rtos/B8/ex3/app.elf
MPU TYPE=0x00001000 -> 16 regions
R0 RBAR=0x20000000 RASR=0x130b002b TEX=1 S=0 C=1 B=1 AP=3
R1 RBAR=0x20000801 RASR=0x130c0013 TEX=1 S=1 C=0 B=0 AP=3
R2 RBAR=0x20000402 RASR=0x10000009 TEX=0 S=0 C=0 B=0 AP=0
dma_buf[0]=0xa5 ok, now touching guard...
MemManage! CFSR=0x00000082 MMFAR=0x20000404 (guard=0x20000400)
[board_exit] code=0
```

출력에서 볼 것:

- QEMU의 M7은 MPU 영역 **16개**(`TYPE.DREGION = 0x10`). 실칩은 구현에 따라 8 또는 16이다(예: STM32F7은 8, H7은 16).
- `RBAR`의 하위 4비트에 영역 번호가 보인다(`...801` = 영역 1). `RASR=0x130c0013` 해석: XN = 1(bit 28), AP = 3, TEX = 1, S = 1, C = 0, B = 0, SIZE 필드 = 9 → 2^(9+1) = 1 KB, ENABLE = 1.
- guard에 쓰자 `CFSR=0x82` = `MMARVALID`(bit 7) + `DACCVIOL`(bit 1), `MMFAR=0x20000404` = 정확히 `guard+4`. **QEMU는 MPU 권한을 진짜로 검사한다** — 캐시 속성(C/B/TEX)은 레지스터에 저장만 되고 효과는 없다.
- `.bss`가 2376 B로 커졌다. 1 KB 영역을 1 KB에 정렬하느라 생긴 빈틈이다. Armv7-M MPU의 "크기 정렬" 규칙은 RAM을 낭비한다 — 그래서 DMA 버퍼를 **한 섹션에 모아서 영역 하나로** 덮는 게 정석이다(4.3절의 `.dma_nocache`).

### 3.4 Armv8-M MPU (M33 · M55 · M85)는 표현이 다르다

Armv8-M은 TEX/C/B 대신 **MAIR**(Memory Attribute Indirection Register) 방식이다. `MAIR0/1`에 속성 바이트 8개를 미리 등록하고, 각 영역은 `RLAR.AttrIndx`(0~7)로 그중 하나를 고른다. 영역은 `RBAR`(시작) · `RLAR`(끝)으로 **32 B 단위 아무 크기**나 되고 2의 거듭제곱 정렬이 필요 없다. 대신 **영역이 겹치면 안 된다**(겹치면 fault). 속성 바이트 예: `0xFF` = Normal WB 읽기/쓰기 allocate, `0xAA` = Normal WT 읽기 allocate, `0x44` = Normal non-cacheable, `0x00` = Device-nGnRnE. 8.2절의 Zephyr 예제에서 실제 값을 본다.

### 3.5 흔한 함정

- **속성만 바꾸고 캐시 정리를 안 함**: 영역을 non-cacheable로 바꿔도 그 전에 캐시에 들어간 라인은 남는다. 바꾸기 전에 clean+invalidate, 바꾼 뒤 `DSB` + `ISB`.
- **Device 영역에 `memcpy`**: Device 메모리에 정렬 안 된 접근을 하면 fault가 날 수 있다. DMA 버퍼를 Device로 만들지 말고 Normal non-cacheable로.
- **S = 1을 줬는데 캐시가 안 된다**: M7의 shareable = 캐시 안 됨 규칙(3.1절).
- **v7-M 영역 정렬**: 1 KB 영역의 시작 주소가 1 KB 정렬이 아니면 하위 비트가 잘려 엉뚱한 주소를 덮는다. 링커 스크립트에서 `ALIGN`으로 보장한다.

> 🔄 2026-11-01 정정: F4 §2.3 실측에서 QEMU(mps2-an385)는 정렬 안 된 v7-M 리전을 "하위 비트를 잘라 덮는" 대신 **리전 전체를 무시**했다(`-d guest_errors`: `DRBAR[7]: 0x20030300 misaligned to DRSR region size`). Arm 문서는 정렬을 소프트웨어 책임으로만 적고 있고 실칩 동작은 확인하지 못했다. 결론은 같다 — 보호가 엉뚱한 곳에 걸리거나 아예 안 걸리는 조용한 버그이므로 `ALIGN(크기)`로 보장하고 부팅 때 `(base & (size - 1)) == 0`을 assert한다.
- **v8-M에서 영역 겹침**: v7-M 습관대로 "큰 영역 위에 작은 영역"을 겹치면 v8-M에서는 fault. Zephyr가 SRAM 영역을 `__nocache` 앞뒤로 쪼개는 이유다(예제 Z1 출력).

---

## 4. TCM — 캐시가 아닌 "코어 옆 전용 RAM"

> 🔁 이 절은 Bare-Metal BM B9(플래시 · SRAM 기술의 TCM · SRAM 뱅크 · ECC)와 겹친다 (주인: 이 노트).

### 4.1 직관 — 책상 서랍

캐시가 "자동으로 바뀌는 책상 위 복사본"이라면, TCM은 **내 책상 서랍**이다. 크기는 작지만 (1) 항상 손이 닿고(0 wait state, 코어 클록 그대로), (2) 누가 대신 넣었다 뺐다 하지 않는다(캐시가 아니므로 miss가 없다), (3) 넣을 물건은 내가 정한다(링커 스크립트).

| 비교 | L1 캐시 | TCM |
|---|---|---|
| 누가 내용을 정하나 | 하드웨어(최근 쓴 라인) | 프로그래머(링커 섹션) |
| 접근 시간 | hit이면 빠름, miss면 느림 → **분산 있음** | 항상 같음 → **결정적** |
| 일관성 문제 | 있음 (DMA와) | 없음 (캐시가 아님) — 단, DMA가 TCM에 닿아야 쓸 수 있다 |
| 크기 | M7 최대 64 KB씩 | M7 최대 16 MB씩 (실칩 수십~수백 KB) |
| 용도 | 큰 코드 · 데이터 전반 | ISR, rate loop, 스택, 자주 쓰는 작은 테이블 |

Cortex-M7 TCM 인터페이스: ITCM은 64비트 폭 하나, DTCM은 32비트 폭 두 개(짝수 · 홀수 워드를 동시에)로 붙는다(M7 TRM 기준). 그래서 ITCM에서는 명령 페치가, DTCM에서는 load/store가 거의 1 cycle에 된다.

Don의 SSD 경험으로 번역하면: Cortex-R5의 ATCM/BTCM에 FTL의 핫 경로(명령 디스패치, 매핑 테이블 캐시)와 스택을 두고, 대용량 버퍼는 DRAM에 두던 것과 같은 설계다. 차이는 R5가 TCM에 ECC를 기본으로 쓰고(리셋 뒤 TCM 초기화 필수 — **B9**에서), M7은 칩마다 다르다는 점이다(H7의 TCM에는 ECC가 있다고 ST 문서에 나오지만 이 노트에서 RM 원문은 확인 못 함).

### 4.2 STM32H743 메모리 맵 — 누가 어디에 닿나

TCM의 가장 큰 함정은 **"DMA가 TCM에 닿지 않는 칩이 있다"**는 것이다. H7이 대표적이다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13" font-weight="bold">영역 (주소, 크기)</text>
<text x="380" y="22" font-size="13" text-anchor="middle" font-weight="bold">D-cache</text>
<text x="470" y="22" font-size="13" text-anchor="middle" font-weight="bold">MDMA</text>
<text x="560" y="22" font-size="13" text-anchor="middle" font-weight="bold">DMA1/2</text>
<text x="640" y="22" font-size="13" text-anchor="middle" font-weight="bold">BDMA</text>
<rect x="20" y="34" width="300" height="38" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="58" font-size="13">ITCM 0x0000_0000, 64 KB</text>
<text x="380" y="58" font-size="13" text-anchor="middle">안 거침</text>
<text x="470" y="58" font-size="13" text-anchor="middle">가능</text>
<text x="560" y="58" font-size="13" text-anchor="middle" font-weight="bold">불가</text>
<text x="640" y="58" font-size="13" text-anchor="middle">불가</text>
<rect x="20" y="80" width="300" height="38" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="30" y="104" font-size="13">Flash 0x0800_0000, 2 MB</text>
<text x="380" y="104" font-size="13" text-anchor="middle">I/D 캐시</text>
<text x="470" y="104" font-size="13" text-anchor="middle">가능</text>
<text x="560" y="104" font-size="13" text-anchor="middle">가능</text>
<text x="640" y="104" font-size="13" text-anchor="middle">불가</text>
<rect x="20" y="126" width="300" height="38" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="150" font-size="13">DTCM 0x2000_0000, 128 KB</text>
<text x="380" y="150" font-size="13" text-anchor="middle">안 거침</text>
<text x="470" y="150" font-size="13" text-anchor="middle">가능</text>
<text x="560" y="150" font-size="13" text-anchor="middle" font-weight="bold">불가</text>
<text x="640" y="150" font-size="13" text-anchor="middle">불가</text>
<rect x="20" y="172" width="300" height="38" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="196" font-size="13">AXI SRAM 0x2400_0000, 512 KB</text>
<text x="380" y="196" font-size="13" text-anchor="middle">WB-WA</text>
<text x="470" y="196" font-size="13" text-anchor="middle">가능</text>
<text x="560" y="196" font-size="13" text-anchor="middle">가능</text>
<text x="640" y="196" font-size="13" text-anchor="middle">불가</text>
<rect x="20" y="218" width="300" height="38" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="30" y="242" font-size="13">SRAM1-3 0x3000_0000, 288 KB</text>
<text x="380" y="242" font-size="13" text-anchor="middle">WB-WA</text>
<text x="470" y="242" font-size="13" text-anchor="middle">가능</text>
<text x="560" y="242" font-size="13" text-anchor="middle">가능</text>
<text x="640" y="242" font-size="13" text-anchor="middle">불가</text>
<rect x="20" y="264" width="300" height="38" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="30" y="288" font-size="13">SRAM4 0x3800_0000, 64 KB</text>
<text x="380" y="288" font-size="13" text-anchor="middle">WB-WA</text>
<text x="470" y="288" font-size="13" text-anchor="middle">가능</text>
<text x="560" y="288" font-size="13" text-anchor="middle">가능</text>
<text x="640" y="288" font-size="13" text-anchor="middle">가능</text>
<text x="340" y="322" font-size="12" text-anchor="middle">D-cache 칸은 MPU 미설정 시 기본 맵 속성 · DMA 칸은 RM0433 버스 구조 기준 (원문 재확인 권장)</text>
</svg>
```
그림 7 — STM32H743의 메모리와 버스 마스터별 접근 가능 여부. CPU는 전부 닿는다. 범용 DMA1/2는 TCM에 못 닿고, D3 도메인의 BDMA는 SRAM4만 닿는다. MDMA는 M7의 AHBS 포트를 통해 TCM에 닿는다.

주소 · 크기(ITCM · DTCM · AXI SRAM · SRAM1~3)는 Zephyr v4.4.2의 `stm32h7.dtsi` · `stm32h743.dtsi`에서 확인했다(`sram1: memory@30000000` 128 KB, `sram2: memory@30020000` 128 KB, `sram3: memory@30040000` 32 KB). 버스 마스터별 접근 칸은 ST RM0433의 버스 매트릭스 구조를 기억에 기대어 옮긴 것이라 **이 노트에서 원문을 다시 확인하지 못했다**. 실제 프로젝트에서는 반드시 RM의 "bus-master-to-slave interconnect" 표를 본다.

설계 결론(H7):

- **UART · SPI DMA 버퍼 → SRAM1~3(D2)** 또는 AXI SRAM. DTCM에 두면 DMA1/2가 못 닿아서 **전송이 아예 실패**한다(DMA 전송 오류 플래그). 이 버그는 캐시 일관성 버그와 증상이 다르다 — 데이터가 "가끔 옛날 값"이 아니라 "항상 안 옴"이다.
- **LPUART1 · SPI6 · I2C4 같은 D3 주변장치의 BDMA 버퍼 → SRAM4**.
- **태스크 스택 · 제어 상태 → DTCM**. DMA가 닿을 필요가 없고, 캐시 miss가 없어 제어 루프 지터가 줄어든다.

STM32F7은 다르다: F7의 DTCM은 AHBS를 통해 일반 DMA도 접근할 수 있다(그래서 F7 예제 코드는 DMA 버퍼를 DTCM에 두기도 한다). **"TCM에 DMA 버퍼를 둬도 되나"의 답은 칩마다 다르다** — 칩 RM의 버스 그림을 보고 정한다.

### 4.3 예제 4 — 링커 스크립트로 ITCM · DTCM · DMA 섹션 만들기 (H7 맵은 빌드 · map 확인, QEMU 맵은 실행)

무엇을 확인하나: (1) `section` 속성으로 함수 · 변수를 원하는 메모리에 놓는 법, (2) ITCM 코드의 LMA(플래시에 실리는 주소)와 VMA(실행 주소)가 다르므로 복사가 필요하다는 것, (3) 플래시(`0x0800_0000`)에서 ITCM(`0x0`)으로의 호출이 Thumb `BL`의 ±16 MB 범위를 넘어 **링커가 veneer를 만든다**는 것.

메모리 맵 두 개(H7형, QEMU형)와 공통 SECTIONS를 나눴다.

```text
/* mem_h7.ld — STM32H743 비슷한 메모리 맵 (빌드만) */
MEMORY {
  FLASH  (rx)  : ORIGIN = 0x08000000, LENGTH = 2M
  ITCM   (rwx) : ORIGIN = 0x00000000, LENGTH = 64K
  DTCM   (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
  AXISRAM(rwx) : ORIGIN = 0x24000000, LENGTH = 512K
  SRAM_D2(rw)  : ORIGIN = 0x30000000, LENGTH = 288K
}
/* mem_qemu.ld — QEMU mps2-an500에서 실행되도록 같은 이름의 영역을 실제 RAM 위에 흉내 */
MEMORY {
  FLASH  (rx)  : ORIGIN = 0x00000000, LENGTH = 1M
  ITCM   (rwx) : ORIGIN = 0x00200000, LENGTH = 64K
  DTCM   (rw)  : ORIGIN = 0x20000000, LENGTH = 128K
  AXISRAM(rwx) : ORIGIN = 0x20100000, LENGTH = 512K
  SRAM_D2(rw)  : ORIGIN = 0x20200000, LENGTH = 288K
}
```

```text
/* sections.ld — 핫 코드는 ITCM, 스택·제어 데이터는 DTCM, DMA 버퍼는 D2 SRAM(MPU로 non-cacheable) */
ENTRY(Reset_Handler)
_Main_Stack_Size = 0x1000;
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) . = ALIGN(4); } > FLASH
  .text   : { *(.text*) *(.rodata*) KEEP(*(.init)) KEEP(*(.fini)) . = ALIGN(4); } > FLASH
  .ARM.exidx : { *(.ARM.exidx*) } > FLASH
  .itcm_text : { . = ALIGN(8); _sitcm = .; *(.itcm_text*) . = ALIGN(8); _eitcm = .; } > ITCM AT > FLASH
  _siitcm = LOADADDR(.itcm_text);
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > DTCM AT > FLASH
  .bss (NOLOAD) : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON) *(.dtcm_bss*) . = ALIGN(4); _ebss = .; } > DTCM
  .dma_nocache (NOLOAD) : { . = ALIGN(32); _sdma = .; *(.dma_nocache*) . = ALIGN(32); _edma = .; } > SRAM_D2
  .heap (NOLOAD) : { . = ALIGN(8); end = .; _end = .; __heap_start = .; } > AXISRAM
  __heap_end = ORIGIN(AXISRAM) + LENGTH(AXISRAM);
  _estack = ORIGIN(DTCM) + LENGTH(DTCM);           /* MSP(ISR 스택)도 DTCM 끝 */
}
```

(`h7.ld` = `INCLUDE mem_h7.ld` + `INCLUDE sections.ld`, `qemu.ld`도 같은 식.) 공용 템플릿의 `startup.c`가 `.data` 복사 · `.bss` 0 채우기를 하므로 그대로 쓰고, ITCM 복사만 `main` 앞부분에서 한다(실제 제품에서는 Reset_Handler의 `.data` 복사 옆에 둔다 — **B5**).

```c
/* ex4_tcm.c — ISR 핫 코드 → .itcm_text, 태스크 스택 → DTCM, DMA 버퍼 → .dma_nocache */
#include <stdio.h>
#include <string.h>
#include "board.h"
#define ITCM_FN  __attribute__((section(".itcm_text"), noinline))
#define DTCM_BSS __attribute__((section(".dtcm_bss")))
#define DMA_BUF  __attribute__((section(".dma_nocache"), aligned(32)))

extern uint32_t _sitcm, _eitcm, _siitcm, _sdma, _edma;
static uint32_t task_stack[2][512] DTCM_BSS;      /* 태스크 2개 × 2 KB */
static uint8_t  uart_rx_dma[256] DMA_BUF;
static int32_t  integ;                             /* 일반 .bss = DTCM */

ITCM_FN int32_t rate_loop_step(int32_t err)        /* "1 kHz rate loop" 흉내 */
{
  integ += err;
  return 3 * err + integ / 8;
}

int main(void)
{
  /* ITCM 복사: 원래는 Reset_Handler에서 .data 복사 옆에 둔다 */
  memcpy(&_sitcm, &_siitcm, (uint32_t)&_eitcm - (uint32_t)&_sitcm);
  __asm volatile("dsb\n isb");
  printf("rate_loop_step @ %p (LMA 0x%08lx, %lu B copied)\n", (void *)rate_loop_step,
         (unsigned long)(uint32_t)&_siitcm,
         (unsigned long)((uint32_t)&_eitcm - (uint32_t)&_sitcm));
  printf("task_stack @ %p  uart_rx_dma @ %p  dma region 0x%08lx..0x%08lx\n",
         (void *)task_stack, (void *)uart_rx_dma, (unsigned long)(uint32_t)&_sdma,
         (unsigned long)(uint32_t)&_edma);
  int32_t u = 0;
  for (int i = 0; i < 4; i++) u = rate_loop_step(10 - i);
  printf("u=%ld (code ran from the ITCM-like address)\n", (long)u);
  uart_rx_dma[0] = 1; task_stack[0][0] = 2;
  return 0;
}
```

```sh
# build.sh h7|qemu (발췌)
arm-none-eabi-gcc -mthumb -mcpu=cortex-m7 -mfloat-abi=soft -O2 -g -std=gnu11 -Wall -Wextra \
  -ffunction-sections -fdata-sections -I$T ex4_tcm.c $T/startup.c $T/board.c \
  -T $V.ld -L. -nostartfiles --specs=nano.specs -Wl,--gc-sections -Wl,-Map=$O/app.map -o $O/app.elf
arm-none-eabi-size -A $O/app.elf | grep -E "isr|text|data|bss|dma|heap"
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B8/ex4 && ./build.sh h7 && arm-none-eabi-nm -n /var/tmp/rtos/B8/ex4_h7/app.elf | grep -E "rate_loop|veneer|task_stack|uart_rx_dma|_estack|integ|g_vectors"; ./build.sh qemu && /Users/donh/.rtos_scratch/B8/run_m7.sh /var/tmp/rtos/B8/ex4_qemu/app.elf'
```

```text
.isr_vector          192   134217728
.text               7776   134217920
.itcm_text            32           0
.data                 96   536870912
.bss                4428   536871008
.dma_nocache         256   805306368
.heap                  0   603979776
00000000 T rate_loop_step
08000000 R g_vectors
08001db8 t __rate_loop_step_veneer
20000060 b integ
200001ac b task_stack
20020000 B _estack
30000000 b uart_rx_dma
.isr_vector          192           0
.text               7768         192
.itcm_text            32     2097152
.data                 96   536870912
.bss                4428   536871008
.dma_nocache         256   538968064
.heap                  0   537919488
rate_loop_step @ 0x200001 (LMA 0x00001f20, 32 B copied)
task_stack @ 0x200001ac  uart_rx_dma @ 0x20200000  dma region 0x20200000..0x20200100
u=25 (code ran from the ITCM-like address)
[board_exit] code=0
```

출력에서 볼 것 (`size -A`의 두 번째 열은 10진수 주소다: 134217728 = `0x0800_0000`, 536870912 = `0x2000_0000`, 805306368 = `0x3000_0000`):

- H7 빌드: `rate_loop_step`은 `0x0000_0000`(ITCM), `task_stack` · `integ`는 `0x2000_xxxx`(DTCM), `uart_rx_dma`는 `0x3000_0000`(D2 SRAM), MSP 시작(`_estack`)은 DTCM 끝 `0x2002_0000`.
- **`__rate_loop_step_veneer`가 `0x0800_1db8`에 생겼다.** 플래시와 ITCM은 128 MB 떨어져 있어 `BL`(±16 MB)이 닿지 않으므로, 링커가 "주소를 PC에 로드하는" 작은 점프대를 끼웠다. 아래 디스어셈블에서 확인한다. QEMU 빌드(`0x0` → `0x0020_0000`, 2 MB 차이)에는 veneer가 없다.
- QEMU 빌드는 실제로 돈다: ITCM 흉내 주소 `0x0020_0000`으로 32 B를 복사했고, 함수 포인터가 `0x200001`(Thumb 비트 1)이며, 결과 `u=25`가 나왔다. **QEMU의 M7은 TCM을 모델링하지 않으므로**(그냥 RAM 주소일 뿐) 속도 차이는 보이지 않는다.

```sh
limactl shell elinux -- bash -lc 'arm-none-eabi-objdump -d /var/tmp/rtos/B8/ex4_h7/app.elf | grep -A4 "<__rate_loop_step_veneer>:"; arm-none-eabi-objdump -d /var/tmp/rtos/B8/ex4_h7/app.elf | grep -B1 -A0 "rate_loop_step_veneer>$" | head -6'
```

```text
08001db8 <__rate_loop_step_veneer>:
 8001db8:	f85f f000 	ldr.w	pc, [pc]	@ 8001dbc <__rate_loop_step_veneer+0x4>
 8001dbc:	00000001 	.word	0x00000001
 8001dc0:	65746172 	.word	0x65746172
 8001dc4:	6f6f6c5f 	.word	0x6f6f6c5f
 80000f6:	200a      	movs	r0, #10
 80000f8:	f001 fe5e 	bl	8001db8 <__rate_loop_step_veneer>
 80000fc:	2009      	movs	r0, #9
 80000fe:	f001 fe5b 	bl	8001db8 <__rate_loop_step_veneer>
 8000102:	2008      	movs	r0, #8
 8000104:	f001 fe58 	bl	8001db8 <__rate_loop_step_veneer>
```

출력에서 볼 것: veneer는 `ldr.w pc, [pc]` + 목적지 주소 `0x00000001`(= `0x0` | Thumb 비트) 두 줄이다(그 뒤 `.word`들은 다음 심볼 문자열 데이터를 objdump가 명령처럼 보여 준 것). 호출할 때마다 **플래시에 있는 veneer를 한 번 거친다.** 즉 "ITCM에 넣었으니 0 wait"는 함수 본체 얘기이고, 호출 경로의 veneer는 여전히 플래시(또는 I-cache)에서 페치된다. 1 kHz rate loop라면 무시할 수준이지만, 20 kHz FOC ISR이라면 **ISR 자체를 ITCM에 두고 벡터 테이블(VTOR)도 RAM/ITCM으로 옮겨** 플래시를 아예 안 거치게 하는 편이 낫다.

### 4.4 무엇을 어디에 둘까 — 배치 표

| 대상 | 추천 위치 | 이유 |
|---|---|---|
| 벡터 테이블 | ITCM 또는 DTCM으로 복사 후 `SCB->VTOR` | 예외 진입 시 벡터 페치가 플래시 wait state를 안 탄다 |
| 고속 ISR (FOC 20 kHz, IMU DRDY) | ITCM | 페치 지터 0, I-cache miss 없음 |
| rate loop · 필터 핵심 함수 | ITCM | 같은 이유. 단 veneer 주의 |
| 태스크 스택 · ISR 스택(MSP) | DTCM | 컨텍스트 스위치 push/pop이 캐시 miss를 안 겪는다 |
| 제어 상태 · 작은 룩업 테이블 | DTCM | 결정적 접근 |
| DMA 버퍼 (H7) | D2 SRAM, MPU non-cacheable 또는 캐시 + 유지 연산 | DMA1/2가 TCM에 못 닿는다 |
| 큰 버퍼 (영상 프레임, 로그) | AXI SRAM · 외부 SDRAM, 캐시 가능 | 크기 |
| 나머지 코드 | 플래시 + I-cache | 기본 |

### 4.5 SRAM 뱅크 · 리텐션 · ECC — 알아 둘 것

- **뱅크**: 큰 MCU의 SRAM은 여러 덩어리(H7의 SRAM1/2/3, AXI SRAM)로 나뉘고 각각 버스 매트릭스의 다른 slave 포트에 붙는다. CPU가 SRAM1에서 계산하는 동안 DMA가 SRAM2에 쓰면 **버스 충돌 없이 동시에** 진행된다. DMA 버퍼와 CPU 작업 데이터를 다른 뱅크에 두는 게 성능 튜닝의 기본이다(SSD 펌웨어에서 SRAM 뱅크를 채널별로 나누던 것과 같은 생각).
- **리텐션**: 저전력 모드에서 어떤 SRAM은 내용을 유지하고(retention) 어떤 것은 잃는다. H7은 D3 도메인의 SRAM4와 backup SRAM이 깊은 저전력에서도 남을 수 있게 설계됐다(세부 조건은 RM 확인). 저전력에서 살아남아야 하는 상태(부팅 사유, 크래시 로그)를 어디 둘지 정할 때 본다.
- **ECC**: H7은 플래시와 RAM에 ECC를 둔다고 ST 데이터시트가 밝힌다(이 노트에서 RM 세부는 확인 못 함). ECC가 있는 RAM은 **처음 쓰기 전에 읽으면 ECC 오류**가 날 수 있어, 부팅 초기에 영역 전체를 한 번 쓰는(초기화) 코드가 필요한 칩이 있다 — Cortex-R의 TCM ECC 초기화와 같은 이야기(**B9**, BM D9). M7의 L1 캐시 자체도 ECC 옵션이 있고 `SCB->CACR.ECCDIS`로 끌 수 있다(CMSIS 6 `core_cm7.h`에 `SCB_CACR_ECCDIS_Msk` 정의 확인).

### 4.6 흔한 함정

- **ITCM 함수 안에서 플래시 상수 참조**: `const` 테이블이 `.rodata`(플래시)에 남아 있으면 ITCM 코드도 결국 플래시를 읽는다. 테이블도 DTCM으로.
- **ITCM 복사 전에 ITCM 함수 호출**: Reset_Handler 초반(복사 전)에 부르는 함수를 ITCM에 두면 쓰레기를 실행한다 → HardFault.
- **`-mlong-calls` 없이 veneer에 놀라기**: veneer는 정상이다. 다만 성능을 따질 때는 veneer까지 세야 한다.
- **DMA 버퍼를 DTCM에 (H7)**: DMA 전송 오류 · 무응답. 캐시 버그로 오해하고 invalidate만 추가하면 영원히 안 고쳐진다.
- **TCM을 MPU로 non-cacheable 지정**: 의미 없다(원래 캐시 안 됨). 해롭지는 않지만 MPU 영역 낭비.

---

## 5. 플래시 wait state · prefetch · ART

> 🔁 이 절은 Bare-Metal BM B9(플래시 읽기 wait state, 프리페치 · ART 가속기, 플래시 수명 · ECC)와 겹친다 (주인: 이 노트).

### 5.1 직관 — 코어는 168 MHz, 플래시는 30 MHz

내장 NOR 플래시의 읽기 접근 시간은 수십 ns 수준이라, 코어 클록을 올리면 **한 번 읽는 데 여러 클록**이 걸린다. 그 기다리는 클록 수가 **wait state(WS)**다. 설정은 플래시 컨트롤러의 `LATENCY` 필드다. STM32F4(RM0090)의 표는 전원 2.7~3.6 V에서 **30 MHz마다 WS 1개**다: 168 MHz면 5 WS — 플래시 한 번 읽기에 6 클록.

대신 플래시는 한 번에 넓게 읽는다(F4는 128비트 = 16 B, 명령 4~8개). 그리고 칩 회사가 두 가지 장치를 붙인다.

- **prefetch 버퍼**: 지금 라인을 실행하는 동안 **다음 순차 라인**을 미리 읽는다. 직선 코드는 WS가 거의 가려진다. 분기(taken branch)하면 소용없다.
- **ART accelerator**(ST의 이름, Adaptive Real-Time): 플래시 앞의 작은 명령 캐시. F4는 128비트 라인 64개(1 KB)의 명령 캐시와 8개의 데이터 캐시(RM0090 기준). 분기 목적지와 작은 루프를 덮는다.

Cortex-M7 칩은 구조가 다르다. STM32F7은 플래시를 두 경로로 본다 — ITCM 인터페이스(`0x0020_0000`, ART 사용)와 AXI(`0x0800_0000`, M7 L1 I-cache 사용). STM32H7은 플래시가 AXI에 붙고 **M7의 L1 I-cache가 ART의 역할**을 한다(H7에는 M7용 ART가 없다). 그래서 M7 칩에서 "플래시가 느리다"의 해법은 I-cache를 켜는 것과 핫 코드를 ITCM에 두는 것이다.

### 5.2 예제 S4 — wait state · prefetch · ART 효과 모델 (Python 계산, 측정 아님)

QEMU는 플래시 wait state를 모델링하지 않는다. 그래서 단순한 모델로 감을 잡는다. 가정: 라인 16 B = 명령 8개 = 8 cycle, 32 B마다 taken branch 하나(분기 뒤 첫 라인은 prefetch가 못 가림), ART는 64라인 LRU.

```python
# s4_flash_ws.py — 플래시 wait state와 prefetch/ART의 효과를 "모델"로 계산 (측정 아님)
# 가정: STM32F4형 — 플래시 한 번 읽기 = 128비트(16 B) 라인, (WS+1) 사이클. RM0090 표: 2.7–3.6 V에서 30 MHz당 1 WS
import math
def ws_f4(mhz): return max(0, math.ceil(mhz / 30) - 1)
for f in (24, 48, 84, 120, 168):
    print(f"HCLK {f:3d} MHz -> {ws_f4(f)} WS")

def run(body_bytes, iters, ws, prefetch, art_lines, blk=2):
    """루프 본체를 iters번 실행. 라인(16 B) = 명령 8개 = 8 cycle. blk 라인마다 taken branch(비순차 페치)"""
    cyc, art = 0, []                                       # art: ART 캐시 라인 (LRU)
    for it in range(iters):
        for k, ln in enumerate(range(0, body_bytes, 16)):
            if ln in art:
                art.remove(ln); art.append(ln); stall = 0      # ART hit: 0 WS
            else:
                if art_lines: art.append(ln); art = art[-art_lines:]
                sequential = (k % blk) != 0
                stall = 0 if (prefetch and sequential and ws + 1 <= 8) else ws + 1
            cyc += stall + 8
    return cyc

ws = ws_f4(168)
print(f"168 MHz, {ws} WS, taken branch every 32 B:")
for name, pf, art in (("no prefetch, no ART", False, 0), ("prefetch only", True, 0),
                      ("prefetch + ART 64x16B", True, 64)):
    for body in (512, 4096):
        c = run(body, 100, ws, pf, art); ideal = 100 * (body // 16) * 8
        print(f"  {name:22s} loop {body:4d} B: {c / ideal:4.2f}x of 0-WS ideal"
              f"  (= {168 * ideal / c:5.1f} MHz effective)")
```

```text
HCLK  24 MHz -> 0 WS
HCLK  48 MHz -> 1 WS
HCLK  84 MHz -> 2 WS
HCLK 120 MHz -> 3 WS
HCLK 168 MHz -> 5 WS
168 MHz, 5 WS, taken branch every 32 B:
  no prefetch, no ART    loop  512 B: 1.75x of 0-WS ideal  (=  96.0 MHz effective)
  no prefetch, no ART    loop 4096 B: 1.75x of 0-WS ideal  (=  96.0 MHz effective)
  prefetch only          loop  512 B: 1.38x of 0-WS ideal  (= 122.2 MHz effective)
  prefetch only          loop 4096 B: 1.38x of 0-WS ideal  (= 122.2 MHz effective)
  prefetch + ART 64x16B  loop  512 B: 1.00x of 0-WS ideal  (= 167.4 MHz effective)
  prefetch + ART 64x16B  loop 4096 B: 1.38x of 0-WS ideal  (= 122.2 MHz effective)
```

출력에서 볼 것 (모델이므로 숫자 자체보다 모양이 중요하다):

- 아무것도 없으면 168 MHz 칩이 **96 MHz처럼** 돈다. prefetch는 직선 부분만 가린다(122 MHz).
- ART에 **루프 전체가 들어가면(512 B < 1 KB)** 거의 이상치(167 MHz). 루프가 ART보다 크면(4 KB) ART는 LRU로 계속 쫓겨나 도움이 안 된다 — 1.4절의 캐시 계단과 같은 이야기.
- 그래서 **"핫 루프의 크기를 가속기 · 캐시 크기 안에 넣는다"**, 안 되면 **RAM/ITCM으로 옮긴다**가 실무 규칙이다. 그리고 ART · I-cache의 hit/miss는 **실행 시간 분산**을 만들므로, 결정성이 필요한 ISR은 ITCM이 정답이다.

### 5.3 클록을 올리는 순서 — bring-up 함정

- **클록 올리기 전에 WS를 먼저 올린다.** 순서를 바꾸면 그 사이에 플래시가 규격보다 빠르게 읽혀 **쓰레기 명령**을 페치한다 → 랜덤 HardFault. 클록을 내릴 때는 반대로 클록 먼저, WS 나중.
- `LATENCY`를 쓴 뒤 **다시 읽어서 반영됐는지 확인**하고 나서 클록을 바꾼다(ST 레퍼런스 매뉴얼이 권하는 순서).
- 전원 전압(VOS, 전압 스케일링)에 따라 같은 클록에서도 WS 표가 달라진다. 저전압 · 저전력 모드로 갈 때 다시 본다.

### 5.4 플래시 수명 · ECC — 펌웨어가 신경 쓸 것

- **쓰기 · 지우기 수명**: STM32 내장 플래시는 보통 섹터당 1만 회 수준의 program/erase 보증이다(칩 데이터시트의 endurance 항목 — 칩마다 확인). NAND(Don의 SSD)와 비교하면 셀 하나는 오래 가지만, **wear leveling이 없다.** 파라미터 · 로그를 매번 같은 섹터에 쓰면 몇 년 안에 닳는다. 그래서 EEPROM 에뮬레이션(여러 페이지를 돌아가며 쓰기), 또는 외부 FRAM/EEPROM을 쓴다.
- **쓰기 단위 · ECC**: H7 플래시는 256비트 "flash word" 단위로 프로그램하고 그 단위에 ECC를 붙인다(RM0433 기준으로 알고 있으나 이 노트에서 원문 재확인 못 함). 같은 flash word를 두 번 나눠 쓸 수 없다 → 로그 구조를 32 B 정렬로 설계.
- **ECC 오류 처리**: 단일 비트 오류는 정정되고 플래그만 서며, 이중 비트 오류는 버스 fault/인터럽트로 올라온다. 부트로더가 "이미지 검증"에서 CRC와 별도로 이 플래그를 본다.
- **플래시를 쓰는 동안 같은 뱅크에서 코드 실행 불가**: 단일 뱅크 칩에서 플래시를 지우는 동안 그 플래시에서 페치하려는 CPU는 멈춘다(stall). RTOS 시스템이면 **그동안 모든 태스크 · ISR이 멈춘다** — 그래서 플래시 쓰기 루틴과 그동안 돌아야 하는 ISR을 RAM/ITCM에 둔다(듀얼 뱅크 칩은 다른 뱅크에서 실행 가능).

---

## 6. DMA 일관성 버그 — 고성능 MCU 버그 1순위

### 6.1 직관 — Don의 SSD 경험을 번역하면

SSD 컨트롤러에서 이런 순서로 일했을 것이다.

1. 호스트 쓰기: PCIe DMA가 호스트 데이터를 컨트롤러 SRAM/DRAM 버퍼에 쓴다 → 펌웨어가 그 버퍼의 헤더를 읽기 전에 **invalidate**.
2. 펌웨어가 메타데이터를 버퍼에 쓴다 → NAND 채널 DMA가 읽어 가기 전에 **clean**.
3. 버퍼를 재사용하기 전에, 그 버퍼가 캐시에 dirty로 남아 있지 않게 한다 — 그렇지 않으면 **나중에 evict되며 DMA가 쓴 데이터를 덮는다**.

Cortex-M7/M85 MCU의 UART · SPI · ADC DMA는 정확히 이 세 규칙을 따른다. 다른 점은 SSD 펌웨어에서는 버퍼 관리자가 이 규칙을 한 곳에서 강제했겠지만, **MCU 프로젝트에서는 드라이버마다(그리고 벤더 HAL마다) 제각각**이라는 것이다.

### 6.2 규칙 — 방향별로 무엇을 언제

| 방향 | DMA 시작 전 | DMA 완료 후 (CPU가 읽기 전) | 왜 |
|---|---|---|---|
| **TX** (메모리 → 장치) | **clean** (`SCB_CleanDCache_by_Addr`) | 할 것 없음 | CPU가 쓴 값이 아직 캐시에만 있다 |
| **RX** (장치 → 메모리) | **invalidate** 또는 clean+invalidate | **invalidate** (`SCB_InvalidateDCache_by_Addr`) | 시작 전: dirty 라인이 DMA 도중 evict되어 덮는 것 방지. 완료 후: 옛 라인 · 투기 읽기로 다시 채워진 라인 제거 |
| **양방향** (같은 버퍼를 보내고 받기) | clean+invalidate | invalidate | 두 규칙을 합친 것 |

"완료 후에도 또 invalidate해야 하나? 시작 전에 했는데?" — **해야 한다.** Cortex-M7은 Normal 메모리에 대해 **투기적 읽기(speculative read)**를 할 수 있다. DMA가 진행되는 동안 CPU가 근처 코드를 실행하다가(예: 분기 예측, 배열 경계 밖 prefetch) 그 버퍼의 라인을 미리 캐시에 채워 넣을 수 있다. 그 라인은 DMA가 쓰기 전의 옛 값이다. Linux가 `dma_unmap_single`(RX)에서 다시 invalidate하는 이유와 같다(**EL H9** 3절의 "소유권" 이야기). 시작 전에 invalidate만으로 충분하다고 믿는 코드는 대부분 동작하다가 가끔 깨진다.

### 6.3 그림으로 보는 두 버그

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker>
<marker id="ah3r" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#d0564a"/></marker></defs>
<text x="20" y="20" font-size="13" font-weight="bold">RX 버그: 옛 라인을 읽는다</text>
<text x="110" y="42" font-size="13" text-anchor="middle">CPU + D-cache</text>
<text x="360" y="42" font-size="13" text-anchor="middle">RAM</text>
<text x="600" y="42" font-size="13" text-anchor="middle">DMA (UART RX)</text>
<line x1="110" y1="50" x2="110" y2="200" stroke="currentColor" stroke-dasharray="3,3"/>
<line x1="360" y1="50" x2="360" y2="200" stroke="currentColor" stroke-dasharray="3,3"/>
<line x1="600" y1="50" x2="600" y2="200" stroke="currentColor" stroke-dasharray="3,3"/>
<line x1="358" y1="75" x2="114" y2="75" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="235" y="69" font-size="12" text-anchor="middle">① 지난 프레임 읽기: line fill (00 00)</text>
<line x1="598" y1="115" x2="364" y2="115" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="480" y="109" font-size="12" text-anchor="middle">② 새 프레임 11 22 → RAM</text>
<rect x="40" y="140" width="140" height="28" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="110" y="159" font-size="12" text-anchor="middle">③ 읽기 = hit → 00</text>
<text x="235" y="190" font-size="12" text-anchor="middle">고침: ③ 전에 invalidate → miss → 11 22</text>
<text x="20" y="235" font-size="13" font-weight="bold">TX 버그: DMA가 옛 메모리를 보낸다</text>
<text x="110" y="257" font-size="13" text-anchor="middle">CPU + D-cache</text>
<text x="360" y="257" font-size="13" text-anchor="middle">RAM</text>
<text x="600" y="257" font-size="13" text-anchor="middle">DMA (SPI TX)</text>
<line x1="110" y1="265" x2="110" y2="390" stroke="currentColor" stroke-dasharray="3,3"/>
<line x1="360" y1="265" x2="360" y2="390" stroke="currentColor" stroke-dasharray="3,3"/>
<line x1="600" y1="265" x2="600" y2="390" stroke="currentColor" stroke-dasharray="3,3"/>
<rect x="40" y="276" width="140" height="28" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="110" y="295" font-size="12" text-anchor="middle">① "PING" 쓰기 (dirty)</text>
<line x1="362" y1="330" x2="596" y2="330" stroke="#d0564a" marker-end="url(#ah3r)"/>
<text x="480" y="324" font-size="12" text-anchor="middle">② DMA가 읽음 = 00 00 00 00</text>
<text x="235" y="370" font-size="12" text-anchor="middle">고침: ② 전에 clean → RAM = "PING"</text>
</svg>
```
그림 8 — DMA 일관성 버그 두 종류. RX: CPU가 캐시의 옛 라인을 읽는다. TX: CPU가 쓴 값이 캐시에만 있고 DMA는 RAM의 옛 값을 보낸다.

### 6.4 예제 S1 — 시뮬레이터로 세 가지 버그 재현 (Python, QEMU로는 불가능)

먼저 시뮬레이터 전체 코드다. 라인 32 B, N-way, LRU 교체, WB/WT, write-allocate. DMA는 캐시를 거치지 않고 `mem`(RAM)을 직접 읽고 쓴다 — Cortex-M7 MCU의 DMA와 같다.

```python
# cachesim.py — 아주 작은 D-cache 시뮬레이터 (라인 32 B, N-way, WB/WT, LRU) + DMA는 RAM 직접 접근
LINE = 32

class Cache:
    def __init__(self, mem, size=1024, ways=1, wb=True, name="D$"):
        self.mem, self.ways, self.wb, self.name = mem, ways, wb, name
        self.sets = size // (LINE * ways)
        self.s = [[] for _ in range(self.sets)]          # set마다 [tag, data(bytearray), dirty] 리스트, 앞=LRU
        self.log, self.bus_rd, self.bus_wr = [], 0, 0

    def _split(self, a):
        ln = a // LINE
        return ln % self.sets, ln // self.sets, a % LINE

    def _find(self, a, alloc):
        idx, tag, off = self._split(a)
        st = self.s[idx]
        for e in st:
            if e[0] == tag:
                st.remove(e); st.append(e); return e, off          # hit → MRU
        if not alloc:
            return None, off
        if len(st) == self.ways:                                   # 교체
            v = st.pop(0)
            if v[2]:
                base = (v[0] * self.sets + idx) * LINE
                self.mem[base:base + LINE] = v[1]; self.bus_wr += 1
                self.log.append(f"  evict dirty line 0x{base:04x} -> write-back to RAM")
        base = (a // LINE) * LINE
        e = [tag, bytearray(self.mem[base:base + LINE]), False]; self.bus_rd += 1
        self.log.append(f"  miss  0x{a:04x}: line fill 0x{base:04x} from RAM")
        st.append(e); return e, off

    def read(self, a):
        e, off = self._find(a, True); return e[1][off]

    def write(self, a, v):
        e, off = self._find(a, True)                               # write-allocate
        e[1][off] = v
        if self.wb: e[2] = True
        else: self.mem[a] = v; self.bus_wr += 1                    # write-through

    def _range(self, a, n):
        start = a - a % LINE
        return range(start, a + n, LINE)

    def clean(self, a, n):
        for la in self._range(a, n):
            e, _ = self._find(la, False)
            if e and e[2]:
                self.mem[la:la + LINE] = e[1]; e[2] = False; self.bus_wr += 1
                self.log.append(f"  clean 0x{la:04x}: dirty -> RAM")

    def invalidate(self, a, n):
        for la in self._range(a, n):
            idx, tag, _ = self._split(la)
            for e in list(self.s[idx]):
                if e[0] == tag:
                    self.s[idx].remove(e)
                    self.log.append(f"  inval 0x{la:04x}{' (DIRTY data discarded!)' if e[2] else ''}")

    def clean_inv(self, a, n):
        self.clean(a, n); self.invalidate(a, n)

    def flush_log(self, title):
        print(title); [print(l) for l in self.log]; self.log.clear()

def dma_write(mem, a, data):     # 장치 → RAM (캐시를 거치지 않음)
    mem[a:a + len(data)] = data

def dma_read(mem, a, n):         # RAM → 장치
    return bytes(mem[a:a + n])
```

시나리오 스크립트. D-cache 1 KB direct-mapped(eviction을 쉽게 일으키려고 작게), write-back.

```python
# s1_dma_bugs.py — DMA 일관성 버그 3종을 시뮬레이터로 재현하고 고친다 (D-cache 1 KB direct-mapped, WB)
from cachesim import Cache, dma_write, dma_read
RX, TX = 0x0100, 0x0200

def fresh():
    mem = bytearray(4096); return mem, Cache(mem, size=1024, ways=1, wb=True)

print("=== 1) RX: DMA가 RAM에 새 프레임을 썼는데 CPU는 캐시의 옛 값을 읽는다 ===")
mem, c = fresh()
for i in range(4): c.read(RX + i)                       # 이전 프레임을 읽어서 라인이 캐시에 있음
dma_write(mem, RX, b"\x11\x22\x33\x44")                 # 새 프레임 도착 (RAM만 바뀜)
c.flush_log("trace:")
print(" CPU reads :", [hex(c.read(RX + i)) for i in range(4)], "<- stale (00)")
c.invalidate(RX, 32)                                    # FIX: 읽기 전에 invalidate
print(" after invalidate:", [hex(c.read(RX + i)) for i in range(4)])
c.flush_log("trace:")

print("=== 2) TX: CPU가 쓴 프레임이 캐시에만 있고 DMA는 RAM의 옛 값을 보낸다 ===")
mem, c = fresh()
for i, b in enumerate(b"PING"): c.write(TX + i, b)
print(" DMA sends :", dma_read(mem, TX, 4), "<- RAM still zeros")
c.clean(TX, 4)                                          # FIX: DMA 시작 전에 clean
print(" after clean, DMA sends:", dma_read(mem, TX, 4))
c.flush_log("trace:")

print("=== 3) RX 준비 중 memset으로 dirty된 라인이, DMA 도중 evict되어 새 데이터를 덮는다 ===")
mem, c = fresh()
for i in range(32): c.write(RX + i, 0)                  # 드라이버가 버퍼를 0으로 초기화 (dirty)
dma_write(mem, RX, bytes(range(1, 33)))                 # DMA가 RAM에 씀
c.read(RX + 1024)                                       # 다른 변수 접근 → 같은 set 충돌 → evict
c.flush_log("trace:")
print(" RAM after eviction:", list(mem[RX:RX + 4]), "<- DMA data overwritten by zeros")
c.invalidate(RX, 32)
print(" CPU reads after invalidate:", [c.read(RX + i) for i in range(4)], "<- too late")
print(" FIX: clean+invalidate BEFORE starting DMA ->", end=" ")
mem, c = fresh()
for i in range(32): c.write(RX + i, 0)
c.clean_inv(RX, 32)                                     # DMA에 넘기기 전에
dma_write(mem, RX, bytes(range(1, 33)))
c.read(RX + 1024); c.invalidate(RX, 32)                 # 완료 후 invalidate
print([c.read(RX + i) for i in range(4)])
```

```sh
limactl shell elinux -- bash -lc 'cd /Users/donh/.rtos_scratch/B8/sim && python3 -B s1_dma_bugs.py'
```

```text
=== 1) RX: DMA가 RAM에 새 프레임을 썼는데 CPU는 캐시의 옛 값을 읽는다 ===
trace:
  miss  0x0100: line fill 0x0100 from RAM
 CPU reads : ['0x0', '0x0', '0x0', '0x0'] <- stale (00)
 after invalidate: ['0x11', '0x22', '0x33', '0x44']
trace:
  inval 0x0100
  miss  0x0100: line fill 0x0100 from RAM
=== 2) TX: CPU가 쓴 프레임이 캐시에만 있고 DMA는 RAM의 옛 값을 보낸다 ===
 DMA sends : b'\x00\x00\x00\x00' <- RAM still zeros
 after clean, DMA sends: b'PING'
trace:
  miss  0x0200: line fill 0x0200 from RAM
  clean 0x0200: dirty -> RAM
=== 3) RX 준비 중 memset으로 dirty된 라인이, DMA 도중 evict되어 새 데이터를 덮는다 ===
trace:
  miss  0x0100: line fill 0x0100 from RAM
  evict dirty line 0x0100 -> write-back to RAM
  miss  0x0500: line fill 0x0500 from RAM
 RAM after eviction: [0, 0, 0, 0] <- DMA data overwritten by zeros
 CPU reads after invalidate: [0, 0, 0, 0] <- too late
 FIX: clean+invalidate BEFORE starting DMA -> [1, 2, 3, 4]
```

출력에서 볼 것:

- **버그 1(RX stale)**: 첫 read에서 라인이 채워진 뒤 DMA가 RAM을 바꿔도, 다음 read는 hit이라 `00`을 돌려준다. invalidate 뒤에는 miss → 새로 채움 → `11 22 33 44`.
- **버그 2(TX not cleaned)**: CPU가 쓴 `PING`은 dirty 라인에만 있고 DMA는 RAM의 `00`을 보낸다. clean이 RAM에 내려 쓴다.
- **버그 3(dirty eviction)**: 가장 고약하다. 드라이버가 RX 버퍼를 `memset(0)`으로 "깨끗이" 만든 행동이 라인을 dirty로 만들었다. DMA가 쓴 뒤, **전혀 관계없는 변수**(`RX + 1024`, 같은 set)를 읽는 순간 dirty 라인이 evict되며 0을 RAM에 써서 DMA 데이터를 덮는다. 이후 invalidate는 이미 늦었다. **증상은 "아주 가끔, 다른 코드를 고치면 사라지는" 데이터 손상**이다 — eviction이 메모리 배치와 타이밍에 달려 있기 때문이다. 고침은 DMA에 넘기기 **전**에 clean+invalidate(또는 invalidate).

### 6.5 정렬 안 된 invalidate가 이웃을 지운다

> 🔁 이 절의 "라인 단위 유지" 개념은 Bare-Metal BM B3(캐시 원리)와 겹친다 (주인: 이 노트).

invalidate는 라인 단위다. 버퍼가 라인 경계에 맞지 않으면, 버퍼 앞뒤의 **부분 라인(partial line)**에 같이 사는 변수도 함께 버려진다. 그 변수가 CPU가 막 고친(dirty) 값이면 **그 수정이 사라진다**.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<rect x="60" y="60" width="280" height="40" fill="#888" fill-opacity="0.08" stroke="currentColor"/>
<rect x="340" y="60" width="280" height="40" fill="#888" fill-opacity="0.08" stroke="currentColor"/>
<text x="200" y="50" font-size="12" text-anchor="middle">라인 0x100~0x11F</text>
<text x="480" y="50" font-size="12" text-anchor="middle">라인 0x120~0x13F</text>
<rect x="130" y="66" width="350" height="28" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="305" y="85" font-size="12" text-anchor="middle">rx_buf[40] 0x108~0x12F (DMA가 씀)</text>
<rect x="480" y="66" width="35" height="28" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="497" y="125" font-size="12" text-anchor="middle">rx_count</text>
<text x="497" y="141" font-size="12" text-anchor="middle">0x130</text>
<text x="60" y="118" font-size="12">0x100</text>
<text x="340" y="118" font-size="12" text-anchor="middle">0x120</text>
<text x="620" y="118" font-size="12" text-anchor="end">0x140</text>
<line x1="60" y1="160" x2="620" y2="160" stroke="#d0564a" stroke-width="2"/>
<line x1="60" y1="152" x2="60" y2="168" stroke="#d0564a" stroke-width="2"/>
<line x1="620" y1="152" x2="620" y2="168" stroke="#d0564a" stroke-width="2"/>
<text x="340" y="185" font-size="12" text-anchor="middle">invalidate(rx_buf, 40)이 실제로 버리는 범위 = 두 라인 전체 (64 B)</text>
<text x="340" y="210" font-size="12" text-anchor="middle">→ CPU가 rx_count에 써 둔 dirty 값 7도 함께 버려진다</text>
<text x="340" y="235" font-size="12" text-anchor="middle">고침: rx_buf를 32 B 정렬 + 크기를 32의 배수로 → 라인을 혼자 쓴다</text>
</svg>
```
그림 9 — 정렬 안 된 RX 버퍼(40 B, `0x108`)가 두 라인에 걸치고, 두 번째 라인에 `rx_count`가 같이 산다. invalidate는 라인 전체를 버린다.

#### 예제 S2 — 정렬 안 된 invalidate 재현 (Python 시뮬레이터)

```python
# s2_misaligned.py — 정렬 안 된 RX 버퍼를 invalidate하면 같은 라인의 이웃 변수가 사라진다
from cachesim import Cache, dma_write, LINE
mem = bytearray(4096); c = Cache(mem, size=1024, ways=4, wb=True)   # 4-way, 32 B 라인
# 링커가 이렇게 배치했다고 하자: rx_buf[40] @ 0x0108 (정렬 안 됨), 그 뒤 rx_count @ 0x0130
RXB, RXN, CNT = 0x0108, 40, 0x0130
lines = sorted({a - a % LINE for a in range(RXB, RXB + RXN)})
print(f"rx_buf 0x{RXB:04x}..0x{RXB+RXN-1:04x} touches lines", [hex(l) for l in lines],
      f"; rx_count @0x{CNT:04x} is in line 0x{CNT - CNT % LINE:04x}")
c.write(CNT, 7)                                  # 태스크가 카운터를 7로 갱신 (캐시에만, dirty)
dma_write(mem, RXB, bytes([0xAB] * RXN))         # DMA가 프레임을 RAM에 씀
c.invalidate(RXB, RXN)                           # "정석"대로 invalidate... 라인 단위로!
c.flush_log("trace (invalidate rx_buf only):")
print(" rx_buf[0] =", hex(c.read(RXB)), " rx_count =", c.read(CNT), "<- 7 was lost (RAM had 0)")
c.flush_log("trace:")

print("--- workaround A: clean+invalidate the boundary lines ---")
mem = bytearray(4096); c = Cache(mem, size=1024, ways=4, wb=True)
c.write(CNT, 7)
dma_write(mem, RXB, bytes([0xAB] * RXN))
c.clean_inv(RXB, RXN)                            # 경계 라인을 clean → RAM에 쓰는데...
print(" rx_count =", c.read(CNT), " rx_buf[last] =", hex(c.read(RXB + RXN - 1)),
      "<- counter ok, but clean wrote CPU's stale copy over DMA bytes in line 0x120")
c.flush_log("trace:")

print("--- real fix: buffer aligned to 32 B and size rounded up to 32 B ---")
mem = bytearray(4096); c = Cache(mem, size=1024, ways=4, wb=True)
RXB, RXN, CNT = 0x0100, 64, 0x0140               # __attribute__((aligned(32))) + 64 B
c.write(CNT, 7)
dma_write(mem, RXB, bytes([0xAB] * 40))
c.invalidate(RXB, RXN)
print(" rx_buf[0] =", hex(c.read(RXB)), " rx_count =", c.read(CNT))
```

```text
rx_buf 0x0108..0x012f touches lines ['0x100', '0x120'] ; rx_count @0x0130 is in line 0x0120
trace (invalidate rx_buf only):
  miss  0x0130: line fill 0x0120 from RAM
  inval 0x0120 (DIRTY data discarded!)
 rx_buf[0] = 0xab  rx_count = 0 <- 7 was lost (RAM had 0)
trace:
  miss  0x0108: line fill 0x0100 from RAM
  miss  0x0130: line fill 0x0120 from RAM
--- workaround A: clean+invalidate the boundary lines ---
 rx_count = 7  rx_buf[last] = 0x0 <- counter ok, but clean wrote CPU's stale copy over DMA bytes in line 0x120
trace:
  miss  0x0130: line fill 0x0120 from RAM
  clean 0x0120: dirty -> RAM
  inval 0x0120
  miss  0x0130: line fill 0x0120 from RAM
--- real fix: buffer aligned to 32 B and size rounded up to 32 B ---
 rx_buf[0] = 0xab  rx_count = 7
```

출력에서 볼 것:

- **invalidate만**: `inval 0x0120 (DIRTY data discarded!)` — 태스크가 쓴 `rx_count = 7`이 사라지고 RAM의 옛 값 0으로 돌아갔다. 증상은 "카운터가 가끔 뒤로 간다", "플래그가 가끔 안 선다" 같은 **엉뚱한 변수의 손상**이다. DMA 버퍼를 아무리 들여다봐도 안 보인다.
- **우회책 A(경계 라인 clean+invalidate)**: 카운터는 살렸지만, clean이 CPU의 **옛 복사본 전체 라인**(DMA 바이트 자리는 옛 0)을 RAM에 써서 **DMA가 쓴 rx_buf의 끝 바이트들(`0x120~0x12F`)을 덮었다** (`rx_buf[last] = 0x0`). 부분 라인은 **어느 쪽을 택해도 한쪽이 손해**다. 이것이 "partial line 딜레마"다.
- **진짜 해결**: 버퍼를 32 B 정렬하고 크기를 32의 배수로 올려서 **라인을 혼자 쓰게** 한다. 그러면 invalidate가 남의 데이터를 건드릴 일이 없다.

C로 쓰면 이렇다.

```c
#define DCACHE_LINE 32u
#define DMA_ALIGNED __attribute__((aligned(DCACHE_LINE)))
#define ROUND_UP_LINE(n) (((n) + DCACHE_LINE - 1u) & ~(DCACHE_LINE - 1u))

static uint8_t uart_rx[ROUND_UP_LINE(40)] DMA_ALIGNED;   /* 64 B, 라인 2개를 혼자 쓴다 */
_Static_assert((sizeof uart_rx % DCACHE_LINE) == 0, "DMA buffer must be a multiple of the line");
uint8_t *get(void) { return uart_rx; }                   /* 확인용 */
```

```sh
limactl shell elinux -- bash -lc 'arm-none-eabi-gcc -mcpu=cortex-m7 -mthumb -O2 -Wall -Wextra -c /Users/donh/.rtos_scratch/B8/snip_align.c -o /var/tmp/rtos/B8/snip.o && arm-none-eabi-nm -S /var/tmp/rtos/B8/snip.o && arm-none-eabi-readelf -S /var/tmp/rtos/B8/snip.o | grep -A1 " .bss"'
```

```text
00000000 00000008 T get
00000000 00000040 b uart_rx
  [ 4] .bss              NOBITS          00000000 00003c 000040 00  WA  0   0 32
  [ 5] .comment          PROGBITS        00000000 00003c 000027 01  MS  0   0  1
```

출력에서 볼 것: `uart_rx`의 크기가 `0x40`(64 B), `.bss` 섹션의 정렬(Al 열)이 32다. 경고 0개.

구조체 안에 DMA 버퍼를 넣을 때는 **버퍼 멤버 앞뒤를 모두 라인 경계로** 만들어야 한다(구조체 전체를 정렬해도 버퍼 뒤의 멤버가 같은 라인에 붙는다). 그래서 "DMA 버퍼는 구조체에 넣지 말고 별도 섹션 배열로"가 실무 규칙이다.

### 6.6 세 가지 해결책 — 무엇을 고를까

| 해결책 | 방법 | 장점 | 단점 | 어울리는 곳 |
|---|---|---|---|---|
| **① 유지 연산** | 캐시 가능 버퍼 + TX 전 clean, RX 전후 invalidate, 32 B 정렬 | CPU가 데이터를 많이 계산할 때 빠르다(캐시 hit) | 드라이버마다 정확히 해야 함, 큰 버퍼면 유지 시간 비용(예제 2) | 영상 · 오디오 프레임, FFT 입력, 큰 블록 전송 |
| **② non-cacheable 영역** | MPU로 DMA 섹션을 Normal non-cacheable(TEX=1,C=0,B=0), Zephyr `__nocache` | 유지 연산을 잊을 수 없다, 코드 단순 | CPU 접근이 매번 버스로 감(느림), MPU 영역 하나 소모 | UART 링 버퍼, DMA 디스크립터, 작은 제어 패킷 |
| **③ TCM / 캐시 안 되는 SRAM에 배치** | DMA가 닿는 TCM(칩 확인!) 또는 MPU 없이도 캐시 안 되는 영역 | 일관성 문제 자체가 없음 + 빠름 | **DMA가 TCM에 닿아야** 함(H7 DMA1/2는 불가, MDMA는 가능, F7은 가능) | F7 DTCM, H7 MDMA 경로, i.MX RT의 DTCM(FlexRAM) |

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="240" y="10" width="200" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="32" font-size="13" text-anchor="middle">DMA 버퍼가 필요하다</text>
<line x1="340" y1="44" x2="340" y2="66" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="190" y="68" width="300" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="90" font-size="13" text-anchor="middle">이 DMA 마스터가 TCM에 닿나? (RM 확인)</text>
<line x1="490" y1="85" x2="548" y2="85" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="519" y="78" font-size="12" text-anchor="middle">예</text>
<rect x="550" y="68" width="120" height="34" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="610" y="90" font-size="13" text-anchor="middle">③ TCM 배치</text>
<line x1="340" y1="102" x2="340" y2="134" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="356" y="123" font-size="12">아니오</text>
<rect x="160" y="136" width="360" height="34" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="158" font-size="13" text-anchor="middle">CPU가 그 데이터를 많이 계산하나? (FFT, 영상)</text>
<line x1="520" y1="153" x2="548" y2="153" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="534" y="146" font-size="12" text-anchor="middle">예</text>
<rect x="550" y="136" width="120" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="610" y="158" font-size="13" text-anchor="middle">① 캐시 가능</text>
<text x="610" y="176" font-size="12" text-anchor="middle">+ 유지 연산</text>
<text x="610" y="191" font-size="12" text-anchor="middle">+ 32 B 정렬</text>
<line x1="340" y1="170" x2="340" y2="202" stroke="currentColor" marker-end="url(#ah4)"/>
<text x="356" y="191" font-size="12">아니오 (작고 자주)</text>
<rect x="190" y="204" width="300" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="225" font-size="13" text-anchor="middle">② non-cacheable 섹션</text>
<text x="340" y="243" font-size="12" text-anchor="middle">MPU 영역 하나 · Zephyr __nocache</text>
<text x="340" y="285" font-size="12" text-anchor="middle">어느 경우든: 버퍼는 전용 섹션에 모으고, 32 B 정렬 · 32의 배수 크기</text>
<text x="340" y="305" font-size="12" text-anchor="middle">혼합도 흔하다: 디스크립터는 ②, 큰 데이터는 ①</text>
</svg>
```
그림 10 — DMA 버퍼 일관성 해결책 고르기. TCM 접근 가능 여부(칩마다 다름) → CPU 계산량 → 기본은 non-cacheable 섹션.

### 6.7 Linux와 무엇이 다른가 (EL H9 참조)

Linux에서는 이 모든 것을 **DMA API가 방향(`DMA_TO_DEVICE` 등)을 보고 대신** 한다 — coherent 할당(`dma_alloc_coherent`, ②에 해당), streaming 매핑(`dma_map_single` / `dma_sync_single_for_cpu`, ①에 해당), 작은 `kmalloc` 버퍼의 bounce까지. **자세한 내용은 EL H9**(2~4절). RTOS · MCU에서 달라지는 점만 정리한다.

- **API가 없다**: FreeRTOS에는 DMA API도 캐시 API도 없다. 드라이버가 CMSIS를 직접 부른다. Zephyr에는 `sys_cache_*`와 `__nocache`가 있지만 **드라이버가 직접 불러야** 한다(Linux처럼 매핑 함수가 숨겨 주지 않음).
- **정렬을 보장해 주는 할당자가 없다**: Linux `kmalloc`은 `ARCH_DMA_MINALIGN`을 보장하지만, FreeRTOS `pvPortMalloc`은 8 B 정렬만 보장한다(`portBYTE_ALIGNMENT`). **DMA 버퍼를 `pvPortMalloc`으로 받으면 라인 정렬이 안 된다** → 정적 배열 + `aligned(32)` 또는 전용 풀.
- **물리 = 가상**: MMU가 없으니 `dma_addr_t` 변환도 IOMMU도 없다. 주소 그대로 DMA 레지스터에 쓴다. 대신 칩별 "DMA가 닿는 주소" 제한(4.2절)이 IOMMU 대신 버그를 만든다.

### 6.8 흔한 함정

- **완료 후 invalidate를 빼먹음**(시작 전에만 함) → 투기 읽기로 가끔 stale.
- **`memset`으로 RX 버퍼 초기화 후 clean 없이 DMA 시작** → 버그 3(dirty eviction).
- **HAL이 해 줄 거라 믿음**: STM32 HAL의 UART/SPI DMA 함수는 버전에 따라 캐시 유지를 하지 않는다. 드라이버 코드를 직접 확인한다(이 노트에서 특정 HAL 버전의 동작은 확인 못 함).
- **유지 연산 크기에 요청 길이가 아니라 실제 수신 길이를 넘김**: idle line 감지로 짧게 끝난 UART 수신에서 "받은 만큼만" invalidate하면, 다음 수신에서 나머지 영역의 옛 라인이 남는다. **버퍼 전체(정렬된 크기)**를 invalidate한다.
- **ISR 안에서 큰 버퍼 invalidate**: 64 KB를 by-address로 돌면 수백 µs 동안 그 ISR이 CPU를 쥔다. 큰 버퍼는 태스크에서, 또는 set/way 전체(예제 2의 교차점).
- **DMA 디스크립터(링크드 리스트)를 캐시 가능 영역에**: 디스크립터도 DMA가 읽고 쓰는 메모리다. ②(non-cacheable)가 정석.

---

## 7. QEMU가 보여 주지 못하는 것 — 정직한 정리

이 노트의 실험을 "무엇이 진짜이고 무엇이 아닌가"로 정리한다.

| 현상 | QEMU mps2-an500/an547 | 이 노트에서 보인 방법 |
|---|---|---|
| CPU 모델(M7 r1p2, MPU 16영역, MemManage) | **진짜로 동작** | 예제 1, 3 |
| 캐시 ID 레지스터 · `CCR.DC/IC` | 0 / 무시 (캐시 0 KB인 M7처럼) | 예제 1 |
| 캐시 유지 함수 실행 | 실행됨(레지스터 쓰기), **효과 없음** | 예제 2, 5 |
| stale 데이터 · dirty eviction | **재현 불가** (캐시가 없으니) | Python 시뮬레이터 S1 · S2 |
| hit/miss 지연, 라인 크기, 캐시 크기 | 없음 | 호스트 측정 H1 · H2 |
| false sharing · 일관성 단위 | 없음 (단일 코어) | 호스트 측정 H3 |
| 선점 뒤 캐시 냉각(CRPD) | 없음 | 호스트 측정 H6 (8.4절) |
| TCM 속도 · DMA의 TCM 접근 제한 | TCM은 그냥 RAM 주소, 제한 없음 | 링커 배치만 예제 4, 접근 제한은 RM 기반 표(그림 7) |
| 플래시 wait state · ART | 없음 | Python 모델 S4 |
| 유지 연산의 명령 수 | icount로 결정적 | 예제 2 (명령 수만, write-back 시간 제외) |

**결론**: QEMU에서 캐시 코드가 "잘 돈다"는 것은 **문법과 레지스터 접근이 맞다는 뜻일 뿐**이다. 캐시 버그의 유무는 실칩에서만 판정된다. 실칩 검증 방법은 9.1절.

---

## 8. RTOS 커널 안에서는 무슨 일이 일어나나

한 줄 요약: **RTOS 커널은 캐시를 거의 모른다.** 컨텍스트 스위치는 레지스터만 바꾸고 캐시는 건드리지 않는다(캐시는 물리 주소 기반이고 MCU에는 주소 공간 전환이 없으므로 flush가 필요 없다). 캐시가 RTOS와 만나는 곳은 세 군데다 — (1) 드라이버의 DMA 일관성, (2) MPU 속성(RTOS가 MPU를 관리할 때), (3) 실행 시간의 분산(WCET).

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="20" y="20" width="200" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="45" font-size="13" text-anchor="middle" font-weight="bold">FreeRTOS V11.3.2</text>
<text x="120" y="65" font-size="12" text-anchor="middle">캐시 API 없음</text>
<line x1="120" y1="80" x2="120" y2="118" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="20" y="120" width="200" height="60" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="120" y="145" font-size="13" text-anchor="middle">드라이버 / 벤더 HAL</text>
<text x="120" y="165" font-size="12" text-anchor="middle">CMSIS SCB_*를 직접 호출</text>
<rect x="250" y="20" width="410" height="60" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="455" y="45" font-size="13" text-anchor="middle" font-weight="bold">Zephyr v4.4.2: sys_cache_data_flush_range()</text>
<text x="455" y="65" font-size="12" text-anchor="middle">include/zephyr/cache.h → z_impl_ → cache_data_flush_range</text>
<line x1="455" y1="80" x2="455" y2="118" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="250" y="120" width="410" height="60" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="455" y="145" font-size="13" text-anchor="middle">arch_dcache_flush_range()</text>
<text x="455" y="165" font-size="12" text-anchor="middle">arch/arm/core/cortex_m/cache.c</text>
<line x1="120" y1="180" x2="300" y2="226" stroke="currentColor" marker-end="url(#ah5)"/>
<line x1="455" y1="180" x2="380" y2="226" stroke="currentColor" marker-end="url(#ah5)"/>
<rect x="190" y="228" width="300" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="340" y="252" font-size="13" text-anchor="middle">CMSIS SCB_CleanDCache_by_Addr()</text>
<text x="340" y="272" font-size="12" text-anchor="middle">→ SCB->DCCMVAC, DSB, ISB</text>
</svg>
```
그림 11 — 캐시 유지 호출 경로. FreeRTOS는 커널에 캐시 API가 없어 드라이버가 CMSIS를 직접 부르고, Zephyr는 `sys_cache_*` → `arch_dcache_*` → CMSIS로 내려간다. 결국 둘 다 같은 레지스터에 쓴다.

### 8.1 FreeRTOS — 커널은 캐시를 모른다

#### 예제 G1 — FreeRTOS V11.3.2 소스에서 "cache"를 찾아본다 (분석)

```sh
limactl shell elinux -- bash -lc 'K=/var/rtoslab/src/FreeRTOS-Kernel; ls $K/portable/GCC/ | grep -i CM7; ls $K/portable/GCC/ARM_CM7/*; grep -rli "dcache\|SCB_Clean\|SCB_Invalidate\|InvalidateDCache" $K --include=*.c --include=*.h | head; echo "--- grep cache in include/:"; grep -rni "cache" $K/include/*.h | wc -l; grep -n "r0p1\|erratum\|837070" $K/portable/GCC/ARM_CM7/r0p1/port.c | head -8'
```

```text
ARM_CM7
/var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM7/ReadMe.txt

/var/rtoslab/src/FreeRTOS-Kernel/portable/GCC/ARM_CM7/r0p1:
port.c
portmacro.h
--- grep cache in include/:
0
513:        "   cpsid i                             \n" /* ARM Cortex-M7 r0p1 Errata 837070 workaround. */
517:        "   cpsie i                             \n" /* ARM Cortex-M7 r0p1 Errata 837070 workaround. */
```

출력에서 볼 것:

- 커널 전체에서 `dcache` · `SCB_Clean` · `SCB_Invalidate`를 쓰는 파일이 **0개**, `include/`의 공개 헤더에 "cache"라는 단어가 **0번**. FreeRTOS는 캐시 유지 함수를 제공하지 않는다. **벤더 HAL(STM32Cube의 CMSIS `SCB_*`, NXP MCUXpresso의 `L1CACHE_*`/`DCACHE_*` 등)이나 직접 CMSIS를 쓴다.**
- M7 전용 포트 `ARM_CM7/r0p1`의 차이는 **캐시와 무관**하다. Cortex-M7 r0p1의 erratum 837070 회피(BASEPRI 쓰기를 `cpsid i`/`cpsie i`로 감쌈)뿐이다. `ReadMe.txt`는 "r0p1이 아니면 ARM_CM4F 포트를 권장"한다고 쓴다. 이 노트의 예제 5는 FPU를 안 쓰는 soft-float라 ARM_CM3 포트로 M7(r1p2)에 올렸다.
- "cache"가 나오는 곳은 MPU 포트(`ARM_CM4_MPU/portmacro.h`의 `portMPU_REGION_CACHEABLE_BUFFERABLE`, TEX/C/B 표 주석)와 Armv8-M 포트의 MPU 속성 매크로뿐이다. 즉 FreeRTOS-MPU를 쓰면 **태스크별 MPU 영역의 캐시 속성**은 사용자가 정하는 값으로 커널이 프로그램한다.

컨텍스트 스위치(`xPortPendSVHandler`, **C2**)에 캐시 연산이 없는 이유: Cortex-M에는 MMU · ASID가 없고, 모든 태스크가 같은 물리 주소 공간을 쓰므로 캐시 내용이 태스크 전환 후에도 그대로 유효하다. Linux가 프로세스 전환 때 TLB · 캐시를 신경 쓰는 것과 대조적이다(**EL**의 MMU 이야기). 대가는 8.4절의 "다른 태스크가 내 캐시를 식혀 놓는" 현상이다.

### 8.2 Zephyr — `sys_cache_*` API와 `__nocache`

Zephyr v4.4.2의 공개 API(`include/zephyr/cache.h`, 소스에서 확인):

| 함수 | 하는 일 | Cortex-M 구현 |
|---|---|---|
| `sys_cache_data_enable()` / `sys_cache_instr_enable()` | 켜기 | `SCB_EnableDCache()` / `SCB_EnableICache()` |
| `sys_cache_data_flush_range(addr, size)` | clean (TX 전) | `SCB_CleanDCache_by_Addr` |
| `sys_cache_data_invd_range(addr, size)` | invalidate (RX 후) | `SCB_InvalidateDCache_by_Addr` |
| `sys_cache_data_flush_and_invd_range(addr, size)` | clean+invalidate | `SCB_CleanInvalidateDCache_by_Addr` |
| `sys_cache_data_flush_all()` 등 `_all` 계열 | 전체 | `SCB_CleanDCache()` 등 |
| `sys_cache_instr_invd_all()` | I-cache 전체 invalidate | `SCB_InvalidateICache()` |
| `sys_cache_data_line_size_get()` | 라인 크기 | `CONFIG_DCACHE_LINE_SIZE` 또는 런타임 감지 |

Zephyr 용어 주의: Zephyr의 **"flush" = Arm의 "clean"**(메모리에 써 냄)이다. "flush"를 "버린다"로 읽으면 반대로 쓴다.

`sys_cache_data_invd_range`의 공식 주석이 6.5절의 함정을 그대로 경고한다(소스 인용): "When multiple data structures share the same cache line being invalidated, all the portions of the non-read-only data structures sharing the same line will be invalidated as well. This is a destructive process that could lead to data loss and/or corruption. When addr is not aligned to the cache line and/or size is not a multiple of the cache line size the behaviour is undefined."

`__nocache`(`include/zephyr/linker/section_tags.h`): `CONFIG_NOCACHE_MEMORY=y`이면 변수를 `.nocache` 섹션에 넣고, `arch/arm/core/mpu/arm_core_mpu.c`의 `static_regions[]`가 `_nocache_ram_start`~`_nocache_ram_size`를 `K_MEM_PARTITION_P_RW_U_NA_NOCACHE` 속성의 MPU 영역으로 등록한다. `CONFIG_NOCACHE_MEMORY`는 `ARCH_HAS_NOCACHE_MEMORY_SUPPORT`에 의존하고, Cortex-M에서는 `ARM_MPU && CPU_HAS_ARM_MPU && CPU_HAS_DCACHE`일 때 선택된다(`arch/arm/core/Kconfig`). 즉 **D-cache가 있는 코어(M7 · M52 · M55 · M85)에서만** 쓸 수 있다. 그 밖의 TCM 관련 매크로: `__itcm_section`, `__dtcm_data_section`, `__dtcm_bss_section`(DT `chosen`의 `zephyr,itcm` · `zephyr,dtcm`이 있을 때), 그리고 CMake의 `zephyr_code_relocate(FILES ... LOCATION ITCM_TEXT)`로 파일 단위 재배치.

#### 예제 Z1 — Zephyr 캐시 API와 `__nocache`가 실제로 만드는 MPU 영역 (QEMU mps3/corstone300/an547 = Cortex-M55, 실행되지만 캐시 효과는 QEMU에서 안 보임)

M7 보드는 이 랩에 STM32 HAL 모듈이 없어 빌드할 수 없으므로, **D-cache가 있는 M55 보드(an547)**를 쓴다.

```text
# prj.conf
CONFIG_CACHE_MANAGEMENT=y
CONFIG_NOCACHE_MEMORY=y
CONFIG_PRINTK=y
```

```c
/* main.c — Zephyr 캐시 API와 __nocache가 실제로 무엇을 만드는지 (mps3/corstone300/an547, M55) */
#include <zephyr/kernel.h>
#include <zephyr/cache.h>
#include <zephyr/linker/linker-defs.h>
#include <cmsis_core.h>

static uint8_t dma_rx[64] __aligned(32);       /* 캐시 가능 + 유지 함수로 관리 */
static uint8_t __nocache dma_nc[256];          /* MPU가 non-cacheable로 만든 섹션 */

int main(void)
{
	printk("CONFIG_DCACHE_LINE_SIZE=%d, sys_cache_data_line_size_get()=%u\n",
	       CONFIG_DCACHE_LINE_SIZE, (unsigned)sys_cache_data_line_size_get());
	printk("CCR.DC=%u IC=%u  CCSIDR=0x%08x (QEMU)\n",
	       (unsigned)((SCB->CCR >> SCB_CCR_DC_Pos) & 1), (unsigned)((SCB->CCR >> SCB_CCR_IC_Pos) & 1),
	       (unsigned)SCB->CCSIDR);
	printk("dma_rx @%p  dma_nc @%p  nocache section %p..%p\n", dma_rx, dma_nc,
	       (void *)&_nocache_ram_start, (void *)&_nocache_ram_end);

	dma_rx[0] = 1;
	int a = sys_cache_data_flush_range(dma_rx, sizeof(dma_rx));      /* TX 전: clean */
	int b = sys_cache_data_invd_range(dma_rx, sizeof(dma_rx));       /* RX 후: invalidate */
	int c = sys_cache_data_flush_and_invd_range(dma_rx, sizeof(dma_rx));
	printk("flush=%d invd=%d flush_and_invd=%d\n", a, b, c);

	printk("MAIR0=0x%08x\n", (unsigned)MPU->MAIR0);
	for (uint32_t i = 0; i < 8; i++) {
		MPU->RNR = i;
		uint32_t rbar = MPU->RBAR, rlar = MPU->RLAR;
		if (rlar & 1u) {
			uint32_t idx = (rlar >> 1) & 7u;
			printk("MPU r%u %08x..%08x AttrIndx=%u attr=0x%02x XN=%u\n", (unsigned)i,
			       (unsigned)(rbar & ~0x1Fu), (unsigned)(rlar | 0x1Fu), (unsigned)idx,
			       (unsigned)((idx < 4 ? MPU->MAIR0 >> (8 * idx) : MPU->MAIR1 >> (8 * (idx - 4))) & 0xFF),
			       (unsigned)(rbar & 1u));
		}
	}
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc '/var/rtoslab/bin/rtos-zbuild mps3/corstone300/an547 /Users/donh/.rtos_scratch/B8/zcache /var/tmp/rtos/B8/zcache && grep -E "CONFIG_(DCACHE|ICACHE|CACHE_MANAGEMENT|NOCACHE_MEMORY|ARCH_CACHE|DCACHE_LINE_SIZE|ARM_MPU)=" /var/tmp/rtos/B8/zcache/zephyr/.config && /var/rtoslab/bin/rtos-zrun /var/tmp/rtos/B8/zcache 10'
```

```text
           FLASH:       16200 B       512 KB      3.09%
             RAM:        4560 B       512 KB      0.87%
            SRAM:           0 B         2 MB      0.00%
           ISRAM:           0 B         4 MB      0.00%
CONFIG_CACHE_MANAGEMENT=y
CONFIG_DCACHE_LINE_SIZE=32
CONFIG_ICACHE=y
CONFIG_DCACHE=y
CONFIG_NOCACHE_MEMORY=y
CONFIG_ARM_MPU=y
CONFIG_ARCH_CACHE=y
qemu-system-arm: warning: nic lan9118.0 has no peer
*** Booting Zephyr OS build v4.4.2 ***
CONFIG_DCACHE_LINE_SIZE=32, sys_cache_data_line_size_get()=32
CCR.DC=0 IC=0  CCSIDR=0x00000000 (QEMU)
dma_rx @0x30000200  dma_nc @0x30000000  nocache section 0x30000000..0x30000100
flush=0 invd=0 flush_and_invd=0
MAIR0=0x0044ffaa
MPU r0 10000000..1007ffff AttrIndx=0 attr=0xaa XN=0
MPU r1 30000100..3007ffff AttrIndx=1 attr=0xff XN=1
MPU r2 00000000..000003ff AttrIndx=0 attr=0xaa XN=0
MPU r3 30000000..300000ff AttrIndx=2 attr=0x44 XN=1
```

(빌드 경고 0개 — 같은 빌드를 `west build`로 다시 돌려 `warning:`을 세어 0을 확인했다. `nic lan9118.0 has no peer`는 QEMU의 네트워크 장치 경고로 앱과 무관.)

출력에서 볼 것:

- Kconfig: `CONFIG_DCACHE`(코어가 `CPU_HAS_DCACHE`이므로 기본 y), `CONFIG_ARCH_CACHE`(코어 내장 캐시 → `arch_dcache_*` 사용), 라인 크기는 DT의 `d-cache-line-size`에서 온 **32**.
- QEMU M55도 M7과 똑같이 `CCR.DC=0`, `CCSIDR=0`이다. API 호출은 모두 0(성공)을 돌려주지만 캐시가 없으니 아무 효과가 없다.
- `__nocache` 변수 `dma_nc`는 RAM 맨 앞 `0x3000_0000`에, 섹션은 정확히 256 B(`..0x3000_0100`).
- MPU: Zephyr가 `MAIR0=0x0044ffaa`로 속성 3개를 등록했다 — 인덱스 0 = `0xAA`(Normal WT, 플래시용), 1 = `0xFF`(Normal WB-RWA, SRAM용), 2 = `0x44`(**Normal non-cacheable**). Zephyr 소스 `arm_mpu_v8.h`의 `MPU_CACHE_ATTRIBUTES_FLASH`(`NORMAL_OUTER_INNER_WRITE_THROUGH_READ_ALLOCATE_NON_TRANS`), `..._SRAM`(`WRITE_BACK_WRITE_READ_ALLOCATE`), `..._SRAM_NOCACHE`(`NON_CACHEABLE`)와 일치한다.
- **r3 = `0x3000_0000~0x3000_00FF`가 `attr=0x44` · XN** — `__nocache` 섹션 그대로다. r1(SRAM 나머지)은 `0x3000_0100`부터 시작한다. **Armv8-M MPU는 영역 겹침을 허용하지 않으므로 Zephyr가 SRAM 영역을 nocache 앞뒤로 쪼갰다**(3.5절). r2(`0x0~0x3FF`)는 이 보드 설정이 둔 다른 정적 영역으로, 출처는 확인하지 않았다.

### 8.3 드라이버 패턴 — DMA 완료 → 태스크 통지 → invalidate → 읽기

DMA 드라이버의 전체 설계(디스크립터, 더블 버퍼, idle line, 에러 처리)는 **E6**에서 한다. 여기서는 캐시 유지가 들어가는 자리만 본다.

#### 예제 5 — FreeRTOS on M7: DMA 완료 IRQ → `xTaskNotifyFromISR` → invalidate → 파싱 (QEMU mps2-an500, 실행되지만 캐시 효과는 QEMU에서 안 보임)

QEMU MPS2에는 DMA 컨트롤러가 없으므로 TIMER0 ISR이 "DMA 엔진 + 완료 인터럽트"를 흉내 낸다(2 kHz로 64 B 프레임을 핑퐁 버퍼에 쓰고 통지). **실칩에서는 DMA가 캐시를 우회해 RAM에 쓰지만, 이 흉내 ISR은 CPU가 쓰므로 일관성 상황이 다르다** — 이 예제는 코드의 **자리와 순서**를 보여 주는 것이다.

```c
/* ex5_dma_task.c — DMA RX 드라이버 패턴: 완료 IRQ → task notify → invalidate → 읽기 (FreeRTOS, M7) */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "b8_cm7.h"
#define FPGA_CNT REG32(0x40028018u)
#define FRAME 64u                              /* 32의 배수 */
static uint8_t rx_buf[2][FRAME] __attribute__((aligned(32)));  /* 핑퐁 */
static TaskHandle_t drv;
static volatile uint32_t t_irq, seq;

/* TIMER0 ISR = "DMA 엔진 + 완료 인터럽트" 흉내. 실칩에서는 DMA가 캐시를 우회해 RAM에 쓴다 */
void TIMER0_IRQHandler(void)
{
  TIMER_INTCLR(TIMER0_BASE) = 1;
  uint8_t *dst = rx_buf[seq & 1];
  for (uint32_t i = 0; i < FRAME; i++) dst[i] = (uint8_t)(seq + i);
  t_irq = FPGA_CNT;
  BaseType_t woken = pdFALSE;
  xTaskNotifyFromISR(drv, seq & 1, eSetValueWithOverwrite, &woken);   /* 어느 버퍼인지 전달 */
  seq++;
  portYIELD_FROM_ISR(woken);
}

static void drv_task(void *arg)
{
  (void)arg;
  uint32_t which, lat_max = 0, inv_max = 0, bad = 0;
  for (int n = 0; n < 200; n++) {
    xTaskNotifyWait(0, 0, &which, portMAX_DELAY);
    uint32_t t0 = FPGA_CNT;
    SCB_InvalidateDCache_by_Addr(rx_buf[which], FRAME);   /* CPU 캐시의 옛 라인 버리기 */
    uint32_t t1 = FPGA_CNT;
    if (t0 - t_irq > lat_max) lat_max = t0 - t_irq;
    if (t1 - t0 > inv_max) inv_max = t1 - t0;
    if (rx_buf[which][1] != (uint8_t)(rx_buf[which][0] + 1)) bad++;
  }
  printf("200 frames: bad=%lu  irq->task max %lu ticks (%lu us)  invalidate max %lu ticks\n",
         (unsigned long)bad, (unsigned long)lat_max, (unsigned long)(lat_max / 25),
         (unsigned long)inv_max);
  board_exit(0);
}

int main(void)
{
  SCB_EnableICache(); SCB_EnableDCache();      /* QEMU: 무시됨 (RAZ/WI) */
  printf("rx_buf[0]=%p rx_buf[1]=%p (32 B aligned: %s)\n", (void *)rx_buf[0],
         (void *)rx_buf[1], ((uint32_t)rx_buf & 31u) ? "NO" : "yes");
  xTaskCreate(drv_task, "uart_drv", 512, NULL, 3, &drv);
  TIMER_RELOAD(TIMER0_BASE) = 25000000u / 2000u;     /* 2 kHz "프레임 완료" */
  TIMER_VALUE(TIMER0_BASE) = TIMER_RELOAD(TIMER0_BASE);
  TIMER_CTRL(TIMER0_BASE) = 1u | 8u;
  NVIC_IPR(8) = 0x40;                          /* configMAX_SYSCALL(0x20)보다 낮은 우선순위 */
  NVIC_ISER0 = 1u << 8;
  vTaskStartScheduler();
  return 1;
}
```

```sh
limactl shell elinux -- bash -lc 'make -s -B -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/B8/ex5/ex5_dma_task.c OUT=/var/tmp/rtos/B8/ex5 EXTRA_CFLAGS="-mcpu=cortex-m7 -mfloat-abi=soft -I/Users/donh/.rtos_scratch/B8 -I/var/rtoslab/zephyrproject/modules/hal/cmsis_6/CMSIS/Core/Include" && ICOUNT=1 /Users/donh/.rtos_scratch/B8/run_m7.sh /var/tmp/rtos/B8/ex5/app.elf'
```

```text
   text	   data	    bss	    dec	    hex	filename
  16432	    100	 135560	 152092	  2521c	/var/tmp/rtos/B8/ex5/app.elf
rx_buf[0]=0x200000a0 rx_buf[1]=0x200000e0 (32 B aligned: yes)
200 frames: bad=0  irq->task max 216 ticks (8 us)  invalidate max 7 ticks
[board_exit] code=0
```

출력에서 볼 것:

- 핑퐁 버퍼 두 개가 각각 32 B 정렬이고 크기 64 B(라인 2개)라서 **서로 라인을 공유하지 않는다.** ISR이 버퍼 1을 채우는 동안 태스크가 버퍼 0을 invalidate해도 서로 망가뜨리지 않는다.
- IRQ → 태스크 깨어남 최대 216 tick(8 µs, QEMU icount 기준 — **A7**에서 잰 IRQ → 태스크 지연과 같은 종류), invalidate는 라인 2개라 7 tick. 64 B 프레임에서 유지 비용은 통지 비용의 3%로 무시할 수준이다.
- 설계 포인트 세 가지: (1) invalidate는 **ISR이 아니라 태스크**에서(큰 버퍼에서 ISR을 길게 만들지 않으려고), (2) 통지 값으로 **어느 버퍼인지** 전달(`eSetValueWithOverwrite`), (3) 재무장(다음 DMA 시작) 전에 그 버퍼를 다시 invalidate(또는 clean+invalidate) — 이 예제에서는 ISR이 계속 쓰므로 생략했지만, 실제 드라이버는 6.2절 표대로 한다.

Zephyr라면 같은 자리에 `sys_cache_data_invd_range(buf, len)`이 들어가고, 통지는 `k_sem_give` / `k_poll_signal` / `k_event_post` 등을 쓴다(**A5**의 API 대응표).

### 8.4 컨텍스트 스위치와 캐시 — WCET에 미치는 영향

컨텍스트 스위치는 캐시를 지우지 않지만, **선점한 태스크가 자기 데이터로 캐시를 채우면** 돌아온 태스크는 식은 캐시에서 다시 시작한다. 이 추가 시간을 **CRPD(Cache-Related Preemption Delay)**라 한다. WCET 분석에서는 "선점 한 번당 최대 몇 µs가 더 드나"를 응답 시간 분석(RTA, **N3**)에 더해야 한다. 자세한 분석 기법은 **N7**.

#### 예제 H6 — 선점 뒤 캐시 냉각 비용 (호스트 측정)

제어 태스크 1회 실행 = 64 KB 테이블에 의존적 조회 2000번. 실행 직전에 "선점한 태스크"가 0 B ~ 32 MB 메모리를 훑는다. 같은 CPU에 고정(`taskset -c 3`)하고 301번씩 재서 min · median · p99를 본다.

```c
/* h6_crpd.c — "선점 뒤 캐시가 식는 비용"(CRPD): 같은 작업을 따뜻한 캐시 vs 다른 태스크가 캐시를 덮은 뒤 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
#define TBL (64 * 1024 / 4)                     /* 64 KB 룩업 테이블 (L1 128 KB 안에 들어감) */
static uint32_t table[TBL];
static uint8_t *other;                          /* "다른 태스크"의 작업 집합 */
static int cmp(const void *a, const void *b) { double x = *(const double *)a, y = *(const double *)b; return (x > y) - (x < y); }

static uint32_t job(uint32_t seed)              /* 제어 태스크 1회: 의존적 테이블 조회 2000번 */
{
  uint32_t idx = seed % TBL;
  for (int i = 0; i < 2000; i++) idx = (table[idx] * 2654435761u + (uint32_t)i) % TBL;
  return idx;
}

static void pollute(size_t bytes)               /* 선점한 태스크가 bytes만큼 메모리를 훑는다 */
{
  for (size_t i = 0; i < bytes; i += 64) other[i]++;
}

int main(void)
{
  other = malloc(64u << 20);
  for (size_t i = 0; i < (64u << 20); i += 4096) other[i] = 1;
  for (int i = 0; i < TBL; i++) table[i] = (uint32_t)i * 7u;
  size_t pol[] = { 0, 64u << 10, 256u << 10, 4u << 20, 32u << 20 };
  volatile uint32_t sink = 0;
  enum { REPS = 301 };
  static double t[REPS];
  for (int k = 0; k < 5; k++) {
    for (int r = 0; r < REPS; r++) {
      pollute(pol[k]);
      double t0 = now(); sink += job((uint32_t)r); t[r] = (now() - t0) * 1e6;
    }
    qsort(t, REPS, sizeof t[0], cmp);
    printf("preempter touched %6zu KB : job min %5.2f  median %5.2f  p99 %5.2f us\n",
           pol[k] >> 10, t[0], t[REPS / 2], t[REPS * 99 / 100]);
  }
  printf("(sink=%u)\n", (unsigned)sink);
  return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/B8/host && gcc -O2 -Wall -Wextra -o h6 /Users/donh/.rtos_scratch/B8/host/h6_crpd.c && taskset -c 3 ./h6'
```

```text
preempter touched      0 KB : job min  5.33  median  5.42  p99  6.46 us
preempter touched     64 KB : job min  5.46  median  5.54  p99  8.87 us
preempter touched    256 KB : job min  8.33  median 10.42  p99 30.54 us
preempter touched   4096 KB : job min  8.21  median 10.54  p99 55.71 us
preempter touched  32768 KB : job min 14.13  median 30.13  p99 247.63 us
(sink=12393150)
```

```svg
<svg viewBox="0 0 640 270" xmlns="http://www.w3.org/2000/svg">
<line x1="150" y1="20" x2="150" y2="240" stroke="currentColor"/>
<text x="142" y="43.3" font-size="12" text-anchor="end">선점 없음 min</text>
<rect x="150" y="24.0" width="65.8" height="28.7" fill="#888" fill-opacity="0.75"/>
<text x="221.8" y="43.3" font-size="12">5.33 us</text>
<text x="142" y="80.0" font-size="12" text-anchor="end">선점 없음 median</text>
<rect x="150" y="60.7" width="67.0" height="28.7" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="223.0" y="80.0" font-size="12">5.42 us</text>
<text x="142" y="116.7" font-size="12" text-anchor="end">64 KB 덮음 median</text>
<rect x="150" y="97.3" width="68.4" height="28.7" fill="#4a7bd0" fill-opacity="0.75"/>
<text x="224.4" y="116.7" font-size="12">5.54 us</text>
<text x="142" y="153.3" font-size="12" text-anchor="end">256 KB 덮음 median</text>
<rect x="150" y="134.0" width="128.7" height="28.7" fill="#e08a3c" fill-opacity="0.75"/>
<text x="284.7" y="153.3" font-size="12">10.42 us</text>
<text x="142" y="190.0" font-size="12" text-anchor="end">4 MB 덮음 median</text>
<rect x="150" y="170.7" width="130.2" height="28.7" fill="#e08a3c" fill-opacity="0.75"/>
<text x="286.2" y="190.0" font-size="12">10.54 us</text>
<text x="142" y="226.7" font-size="12" text-anchor="end">32 MB 덮음 median</text>
<rect x="150" y="207.3" width="372.2" height="28.7" fill="#d0564a" fill-opacity="0.75"/>
<text x="528.2" y="226.7" font-size="12">30.13 us</text>
</svg>
```
그림 12 — 예제 H6 측정값(호스트, µs). 같은 작업이 선점자가 L1을 덮으면(256 KB) 약 2배, 캐시 전체를 덮으면(32 MB) 약 5.6배(median) 느려진다.

출력에서 볼 것:

- 선점자가 64 KB만 쓰면 거의 영향 없음(L1 128 KB에 둘 다 들어감). 256 KB를 쓰면 L1에서 쫓겨나 L2에서 다시 읽느라 **median 5.4 → 10.4 µs**. 32 MB를 쓰면 DRAM까지 가서 **30 µs**.
- p99는 VM 잡음(다른 vCPU, 호스트 스케줄링)이 섞여 크게 흔들린다 — 같은 프로그램을 두 번 돌리면 32 MB 행의 p99가 248 µs와 1066 µs로 달랐다. **min과 median이 캐시 효과, p99는 VM 효과**로 읽는다.
- MCU로 옮기면: 16 KB D-cache인 H7에서 고우선 태스크가 큰 버퍼(로그, 영상)를 훑으면 저우선 제어 태스크의 다음 실행이 느려진다. 대책은 (1) 제어 태스크의 데이터 · 스택을 **DTCM**에(캐시 냉각 자체가 없음), (2) 코드를 **ITCM**에, (3) 큰 스트리밍 데이터는 non-cacheable 또는 캐시 lockdown이 있는 칩이면 그것을 쓰는 것이다. **TCM은 CRPD를 0으로 만든다** — 이것이 실시간 시스템에서 TCM이 캐시보다 나은 진짜 이유다.

### 8.5 Don 경험과 연결

SSD 컨트롤러의 FTL 핫 경로를 TCM에 두던 이유도 "빠르니까"만이 아니라 "명령 처리 지연의 꼬리(p99.9)를 줄이니까"였을 것이다. 엔터프라이즈 SSD의 QoS 스펙(99.99% 지연)과 드론 FC의 "rate loop 최악 지연"은 같은 종류의 요구다. 면접에서 "캐시를 켜면 평균은 빨라지지만 꼬리가 길어진다, 그래서 결정성이 필요한 경로는 TCM에 둔다 — SSD에서 QoS 때문에 그렇게 했다"고 말하면 경험이 그대로 번역된다.

---

## 9. 실무에서 다시 보기

### 9.1 드론 비행 제어기 (STM32H743, 480 MHz)

예를 들어 드론 비행 제어기라면:

- **IMU(SPI, 8 kHz)**: SPI DMA RX 버퍼(14 B 프레임)는 `.dma_nocache` 섹션(D2 SRAM1, MPU non-cacheable)에 32 B 정렬로. 작은 패킷이 자주 오므로 유지 연산보다 non-cacheable이 단순하고 안전하다.
- **rate loop · 필터(1~8 kHz)**: 함수는 ITCM, 필터 상태 · PID 상태 · 태스크 스택은 DTCM. CRPD와 I-cache miss가 없으므로 루프 지터가 줄어든다.
- **블랙박스 로그(SD/플래시, 수 MB/s)**: 512 B~4 KB 블록 버퍼는 AXI SRAM에 캐시 가능으로 두고, 쓰기 DMA 전에 `SCB_CleanDCache_by_Addr`. 블록이 크면 clean 비용이 커지므로 블록 크기를 캐시 크기(16 KB) 근처 이하로.
- **OSD · 영상(있다면)**: 프레임 버퍼는 캐시 가능 + 유지 연산(CPU가 그리기 계산을 많이 함).
- **실칩 검증 방법**: (1) `SCB_EnableDCache()`를 켜고 끈 두 빌드로 같은 DMA 데이터 CRC를 비교, (2) 유지 연산을 일부러 빼 보고 깨지는지(깨져야 테스트가 의미 있다), (3) DWT CYCCNT로 rate loop 실행 시간 분포를 캐시 on/off · ITCM 배치 전후로 비교. 이 랩의 QEMU에는 DWT가 없어 실칩에서만 가능하다.

### 9.2 로봇 관절 모터 컨트롤러 (FOC 20 kHz)

- 전류 루프 ISR(ADC 완료 → Clarke/Park → PI → SVPWM)을 **ITCM**, 벡터 테이블도 RAM으로 옮겨 `VTOR` 설정. 20 kHz = 50 µs 주기에서 I-cache miss 몇 번(각 수십 ns)은 작아 보여도, **최악의 경우가 같은 주기에 몰리면** 지터가 된다.
- ADC 변환 결과를 DMA로 받는 버퍼는 DTCM(DMA가 닿는 칩이면) 또는 non-cacheable. FOC 계산은 ADC 값 몇 개만 읽으므로 non-cacheable의 느린 읽기도 문제없다.
- CAN 메시지 RAM은 대개 주변장치 전용 RAM(Device/non-cacheable 영역)이라 캐시와 무관하지만, CAN-FD 프레임을 DMA로 SRAM에 옮긴다면 같은 규칙.

### 9.3 센서 허브 MCU (Linux SoC 옆)

Linux SoC와 공유 메모리(또는 SPI/UART 링크)로 IMU 타임스탬프 샘플을 넘기는 MCU. 공유 메모리라면 **양쪽 모두 캐시 정책을 맞춰야** 한다 — Linux 쪽은 `dma_alloc_coherent`/`memremap` 등(**EL H9**, Linux ↔ RTOS 공유 메모리 전체는 **P5** · EL O1–O3), MCU 쪽은 MPU non-cacheable. 한쪽만 캐시하면 1.5절 표의 "관찰자 둘, 하드웨어 일관성 없음" 상황이 된다. 링 버퍼의 head/tail 인덱스는 **서로 다른 32 B(그리고 SoC 쪽 64 B) 라인**에 둔다 — 한 라인에 두면 한쪽의 쓰기가 다른 쪽 인덱스를 덮을 수 있다(6.5절과 같은 원리, 예제 H3의 MCU판).

### 9.4 SSD 컨트롤러 펌웨어 (Don)

Don이 이미 한 일을 이 노트의 용어로 정리하면: 호스트 DMA 수신 버퍼 = RX 규칙(시작 전 · 완료 후 invalidate), NAND 프로그램 버퍼 = TX 규칙(clean), 명령 큐 · 완료 큐 디스크립터 = non-cacheable 영역, FTL 핫 경로 · 스택 = TCM. 새로운 것은 MCU에서는 **이 정책을 RTOS가 아니라 각 드라이버가 개별로 구현한다**는 점과, **칩마다 DMA가 닿는 메모리가 다르다**는 점뿐이다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| RX 완료 후 invalidate 없음 | 받은 데이터가 가끔 직전 프레임 값 | CPU가 캐시의 옛 라인을 읽음 (투기 읽기 포함) | 완료 후 `SCB_InvalidateDCache_by_Addr` (버퍼 전체) |
| TX 전 clean 없음 | 보낸 데이터가 가끔 이전 내용 · 0 | CPU가 쓴 값이 dirty 라인에만 있음 | DMA 시작 전 `SCB_CleanDCache_by_Addr` + DSB |
| RX 버퍼를 `memset` 후 바로 DMA | 아주 가끔, 코드 배치를 바꾸면 사라지는 손상 | dirty 라인이 DMA 도중 evict되어 DMA 데이터를 덮음 | DMA 시작 전 clean+invalidate (또는 invalidate) |
| DMA 버퍼가 32 B 정렬 아님 | DMA 버퍼가 아닌 **이웃 변수**가 가끔 옛 값으로 돌아감 | invalidate가 라인 전체를 버림 | `aligned(32)` + 크기를 32의 배수로, 전용 섹션 |
| H7에서 DMA1/2 버퍼를 DTCM에 | DMA가 아예 동작 안 함, 전송 오류 플래그 | DMA1/2는 TCM에 접근 불가 | D2 SRAM/AXI SRAM으로, 또는 MDMA |
| MPU에서 S=1로 설정 | 캐시를 켰는데 성능이 그대로 | M7은 shareable Normal을 캐시하지 않음 (CACR.SIWT=0) | 의도한 영역은 S=0, DMA 영역은 명시적 non-cacheable |
| 캐시를 MPU보다 먼저 켬 | 부팅 직후 첫 DMA만 깨짐 | non-cacheable 지정 전에 라인이 캐시에 들어감 | MPU → 캐시 순서, 또는 속성 변경 전 clean+invalidate |
| 코드를 RAM에 복사 후 I-cache 정리 없음 | 부트로더 점프 후 HardFault · 이상 동작 | D-cache에 남은 코드, I-cache의 옛 명령 | D clean + I invalidate + ISB |
| 클록을 올린 뒤 플래시 WS 설정 | 클록 변경 직후 랜덤 HardFault | 플래시가 규격보다 빠르게 읽힘 | WS 먼저 올리고 확인 후 클록 |
| `pvPortMalloc`으로 DMA 버퍼 할당 | 정렬 버그가 재현될 때만 재현 | 8 B 정렬만 보장 | 정적 배열 `aligned(32)` 또는 정렬 풀 |

---

## 11. 면접에서 이렇게 말한다

**Q.** "You enabled the D-cache on a Cortex-M7 and now SPI DMA data is occasionally stale. What's going on and how do you fix it?"

**A.** M7의 D-cache는 DMA와 하드웨어 일관성이 없다. RX라면 CPU가 캐시의 옛 라인을 읽고, TX라면 DMA가 캐시에만 있는 새 값을 못 본다. 고치는 방법은 세 가지 — 유지 연산(TX 전 clean, RX 전후 invalidate, 32 B 정렬), MPU로 DMA 섹션을 non-cacheable, DMA가 닿는 TCM에 배치. 작은 SPI 프레임이면 non-cacheable이 가장 단순하고 안전하다.

> The M7's L1 D-cache isn't coherent with DMA masters, so the CPU and the DMA can see different copies. For RX, the CPU may hit a stale line — or a line refilled by a speculative read during the transfer; for TX, the DMA reads RAM while the new data sits dirty in the cache. Three fixes: cache maintenance — clean before TX, invalidate before and after RX, with buffers 32-byte aligned and sized; or put DMA buffers in a dedicated section that the MPU marks Normal non-cacheable; or place them in TCM if that DMA master can reach TCM — on an H7, DMA1/2 can't. For a small SPI IMU frame I'd use the non-cacheable section; for big frames the CPU crunches on, cacheable plus maintenance.

**꼬리 질문**: "Why invalidate after the transfer if you already invalidated before?" → 투기 읽기와 DMA 도중의 접근이 라인을 다시 채울 수 있어서.

**Q.** "Why must DMA buffers be cache-line aligned?"

**A.** 유지 연산은 라인(32 B) 단위다. 버퍼가 다른 변수와 라인을 공유하면 invalidate가 그 변수의 dirty 수정을 버리고, clean으로 피하면 이번엔 DMA 데이터를 덮는다. 어느 쪽도 정답이 없으니 버퍼가 라인을 혼자 쓰게 정렬 · 패딩한다.

> Maintenance operates on whole 32-byte lines. If a buffer shares a line with another variable, invalidating the buffer also throws away that neighbour's dirty update; cleaning the shared line instead writes the CPU's stale copy over bytes the DMA just wrote. There's no correct choice for a partial line, so the buffer must own its lines: aligned(32), size rounded up to a multiple of 32, ideally in its own linker section. Zephyr's cache.h even documents unaligned invalidate as undefined behaviour.

**Q.** "What's the difference between TCM and cache, and when would you use each?"

**A.** 캐시는 하드웨어가 내용을 정하고 hit/miss로 시간이 흔들린다. TCM은 프로그래머가 링커로 내용을 정하고 항상 1 cycle이며 캐시 일관성 문제도 없다. 고속 ISR · rate loop · 스택은 TCM, 큰 코드 · 데이터는 캐시.

> Cache is hardware-managed and probabilistic — great average, but miss latency and preemption-related cache refill make the worst case hard to bound. TCM is a software-managed scratchpad: single-cycle, deterministic, not cached, so no coherency issue and no cache-related preemption delay. I put the vector table, the high-rate ISR, the control loop and task stacks in ITCM/DTCM, and leave bulk code and data in flash/SRAM behind the caches. One caveat: check which bus masters can reach TCM on your part. And calls from flash into ITCM need a long-branch veneer, which the linker inserts.

**Q.** "How does an RTOS handle caches? Does a context switch flush the cache?"

**A.** 안 한다. Cortex-M은 MMU가 없고 모든 태스크가 같은 물리 주소 공간이라 캐시 내용은 전환 후에도 유효하다. FreeRTOS 커널에는 캐시 API가 아예 없고, Zephyr는 `sys_cache_*`와 `__nocache`를 주지만 드라이버가 직접 불러야 한다. 비용은 CRPD로 나타난다.

> No. On Cortex-M there's no MMU or ASID; every task shares one physical address space, so cache contents stay valid across a switch and the PendSV handler only swaps registers. FreeRTOS has no cache API at all — drivers call CMSIS or the vendor HAL. Zephyr provides sys_cache_data_flush_range / invd_range and a __nocache section that it maps non-cacheable via the MPU, but drivers still have to call them. The RTOS-level cost is cache-related preemption delay: a preempting task evicts your working set, so your next job runs slower. That has to be bounded in WCET, or avoided by putting critical data in TCM.

**Q.** "Walk me through bringing up caches on a new M7 board."

**A.** 클록과 플래시 WS → MPU(기본 맵 + DMA non-cacheable 섹션 + 필요하면 guard) → I-cache → D-cache → 그 다음 DMA 드라이버. 검증은 캐시 on/off CRC 비교와 유지 연산을 일부러 빼서 깨지는지 보기.

> Order matters. First set flash wait states for the target clock, then raise the clock. Then program the MPU: background region, the non-cacheable DMA section, maybe a stack guard — then enable it with DSB/ISB. Then SCB_EnableICache and SCB_EnableDCache, which invalidate by set/way before turning the caches on. Only then start DMA. To validate, I compare CRCs of DMA'd data with the cache on and off, deliberately remove a maintenance call to confirm the test can catch it, and use DWT CYCCNT to see the control-loop timing distribution before and after moving hot code to ITCM.

**Q.** "Clean vs invalidate vs clean-and-invalidate — when do you use each?"

**A.** clean = dirty를 메모리로(라인은 남음) — TX 전. invalidate = 라인 버림 — RX 후. clean+invalidate = 둘 다 — 양방향 버퍼나 RX 버퍼를 DMA에 넘기기 전. 큰 버퍼면 by-address보다 set/way 전체가 싸질 수 있다.

> Clean writes dirty lines back to memory and keeps them — use it before the device reads memory. Invalidate discards lines without writing — use it after the device wrote memory and before the CPU reads. Clean-and-invalidate does both — for bidirectional buffers, or when handing an RX buffer the CPU has touched to the DMA, so no dirty line can be evicted over the incoming data. By-address cost scales with size; once the buffer is bigger than roughly the cache size, whole-cache set/way maintenance is cheaper. In Zephyr, note that "flush" means clean.

**Q.** "Your SSD background: how does this compare?"

**A.** 같은 문제다. SSD 컨트롤러에서 호스트 DMA 버퍼는 invalidate, NAND 쓰기 버퍼는 clean, 큐 디스크립터는 non-cacheable, FTL 핫 경로는 TCM. 다른 점은 MCU에서는 RTOS가 정책을 강제하지 않고 드라이버마다 구현하며, DMA가 닿는 메모리가 칩마다 다르다는 것.

> It's the same problem I solved in SSD controller firmware on Cortex-R: host DMA landing buffers were invalidated before parsing, NAND program buffers were cleaned before the channel DMA, queue descriptors lived in non-cacheable memory, and the FTL fast path and stacks lived in TCM for tail latency. What's different on an MCU with an RTOS is that nothing enforces the policy centrally — each driver or vendor HAL does it, so I'd put the rules in one small DMA-buffer module with aligned allocation and assertions. And bus-master reachability varies by chip, so I check the reference manual's bus matrix first.

---

## 12. 직접 해보기

1. **주소 분해**: 32 KB, 4-way, 32 B 라인 D-cache(i.MX RT1062)에서 주소 `0x2020_1234`의 offset · index · tag를 구하라. 같은 set에 들어가는 다른 주소 하나를 대라.
   정답/힌트: set = 32768 / (32 × 4) = 256 → index 8비트 [12:5], offset [4:0]. offset = `0x14`, index = `(0x1234 >> 5) & 0xFF` = `0x91`, tag = `0x2020_1234 >> 13`. 8 KB(= set 수 × 라인) 떨어진 `0x2020_3234`가 같은 set.
2. **MPU 인코딩**: Armv7-M MPU로 `0x3000_0000`부터 32 KB를 Normal non-cacheable, shareable, 실행 금지, full access로 만드는 `RASR` 값을 계산하고, 예제 3을 고쳐 QEMU에서 읽어 확인하라.
   정답/힌트: `ARM_MPU_RASR(1, ARM_MPU_AP_FULL, 1, 1, 0, 0, 0, ARM_MPU_REGION_SIZE_32KB)` → SIZE 필드 14, `0x130C001D`. QEMU an500의 `0x3000_0000`에는 RAM이 없으니 읽기만 확인하거나 주소를 `0x2010_0000`으로 바꾼다.
3. **시뮬레이터 확장**: `cachesim.py`에 "투기 읽기"를 흉내 내는 함수(DMA 도중 임의 시점에 버퍼 라인 하나를 `read`)를 넣고, "시작 전에만 invalidate"한 드라이버가 깨지는 것을 재현하라. 완료 후 invalidate를 추가해 고쳐라.
   정답/힌트: `dma_write` 앞부분만 쓴 상태에서 `c.read(RX)` → 그 뒤 `dma_write` 나머지 → `c.read`가 hit으로 옛 값. 완료 후 `c.invalidate(RX, n)`.
4. **교차점 다시 재기**: 예제 2의 `clean_inv_all_sw`를 i.MX RT1062 기하(32 KB · 4-way → 256 set)로 바꾸면 by-address와의 교차점이 어디로 가나? 예측 후 QEMU ICOUNT=1로 재라.
   정답/힌트: set/way 루프가 2배(1024번 쓰기)라 약 2060 tick → 교차점도 약 20 KB로 2배. "캐시 크기의 약 60% 이상이면 전체 연산"이 대략적인 규칙.
5. **링커 배치 확인**: 예제 4에 `__attribute__((section(".itcm_text")))`인 ISR 하나를 더 넣고, H7 빌드의 map 파일(`app.map`)에서 `.itcm_text`의 VMA · LMA와 veneer를 찾아라. 그다음 벡터 테이블을 DTCM으로 복사하고 `SCB->VTOR`를 설정하는 코드를 써서 QEMU 빌드에서 TIMER0 인터럽트가 그 테이블로 들어오는지 확인하라.
   정답/힌트: map의 `.itcm_text 0x00000000 ... load address 0x0800xxxx`. VTOR은 테이블 크기 이상의 2의 거듭제곱(48엔트리 = 192 B → 256 B)에 정렬.
6. **호스트 측정 과제**: 예제 H3에서 두 스레드를 같은 vCPU에 묶으면(`taskset -c 2`) 간격 8 B에서도 느려지지 않는다. 직접 확인하고 이유를 설명하라.
   정답/힌트: 한 코어 안에서는 라인 소유권 이동이 없다. 두 스레드가 번갈아 도니 ns/inc 자체는 늘지만, 간격 8 B와 64 B의 차이가 사라진다(직접 돌려 보니 모든 간격이 1.1~1.6 ns로 비슷했다). 단일 코어 MCU에 false sharing 성능 문제가 없는 이유와 같다.
7. **Zephyr**: 예제 Z1에서 `dma_rx`를 `__aligned(32)` 없이 `uint8_t dma_rx[40]`로 바꾸고 바로 앞뒤에 다른 변수를 둔 뒤, nm으로 같은 32 B 라인에 걸치는지 확인하라. 그리고 Zephyr의 어떤 매크로로 막을 수 있는지 찾아라.
   정답/힌트: `__aligned(CONFIG_DCACHE_LINE_SIZE)` 또는 `__nocache`. Zephyr 드라이버들은 `CONFIG_DCACHE_LINE_SIZE`로 정렬 · 크기를 맞춘다.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| cache line | 캐시 라인 | 캐시와 메모리가 주고받는 최소 단위. M7/M55/M85 L1은 32 B |
| set / way | 집합 / 자리 | 주소의 index가 set을 고르고, 그 set 안의 way 중 하나에 들어간다 |
| direct-mapped | 직접 사상 | way가 1개. 같은 index 주소끼리 충돌한다 |
| tag | 태그 | 그 자리에 든 라인의 원래 주소 상위 비트 |
| hit / miss | 적중 / 실패 | 캐시에 있으면 hit, 없으면 miss → line fill |
| write-back (WB) | 나중 쓰기 | 캐시만 고치고 dirty 표시, evict · clean 때 메모리에 씀 |
| write-through (WT) | 즉시 쓰기 | 캐시와 메모리에 동시에 씀 |
| write-allocate | 쓰기 할당 | 쓰기 miss 때 라인을 채운 뒤 캐시에 씀 |
| dirty | 더러움 | 캐시 라인이 메모리보다 새로움 |
| clean | 클린 | dirty 라인을 메모리에 써 내는 유지 연산 (Zephyr에서는 "flush") |
| invalidate | 무효화 | 라인을 쓰지 않고 버리는 유지 연산 |
| PoC | Point of Coherency | 모든 관찰자가 같은 값을 보는 지점. DMA용 유지는 여기까지 |
| coherency | 일관성 | 여러 관찰자가 같은 주소에서 같은 값을 보는 성질 |
| false sharing | 거짓 공유 | 다른 변수인데 같은 라인이라 일관성 비용이 생김 |
| speculative read | 투기적 읽기 | CPU가 필요할지도 모르는 Normal 메모리를 미리 읽음 → 라인이 채워짐 |
| TCM (ITCM/DTCM) | 밀결합 메모리 | 코어 옆 전용 RAM. 0 wait, 캐시 아님, 링커로 배치 |
| MPU | 메모리 보호 장치 | 영역별 권한과 메모리 타입 · 캐시 속성을 정함 |
| TEX / C / B / S | MPU 속성 비트 | Armv7-M의 메모리 타입 · 캐시 정책 · shareable 인코딩 |
| MAIR | 속성 간접 레지스터 | Armv8-M MPU의 속성 바이트 표. 영역은 인덱스로 참조 |
| Normal / Device / Strongly-ordered | 메모리 타입 | 캐시 · 순서 · 투기 허용 정도가 다름 |
| wait state (WS) | 대기 상태 | 플래시 한 번 읽기에 추가로 기다리는 클록 수 |
| ART accelerator | ST 플래시 가속기 | 플래시 앞의 작은 명령 캐시 + prefetch |
| veneer | 점프대 | `BL` 범위를 넘는 호출을 위해 링커가 넣는 작은 코드 |
| CRPD | 선점 관련 캐시 지연 | 선점한 태스크가 캐시를 덮어 생기는 추가 실행 시간 |
| `__nocache` | Zephyr 섹션 매크로 | MPU가 non-cacheable로 매핑하는 섹션에 변수를 둔다 |

---

## 14. 요약 & 체크리스트

캐시는 느린 메모리 앞의 작은 복사본이고, 라인(M7/M85에서 32 B) 단위로 움직이며, 같은 load 하나가 hit이면 1 cycle, miss면 수십~수백 배가 걸린다(호스트에서 0.9 ns ~ 130 ns를 쟀다). Cortex-M7/M55/M85 MCU의 D-cache는 DMA와 하드웨어 일관성이 없어서, TX 전에는 clean, RX 전후에는 invalidate를 하고, 버퍼는 32 B 정렬 · 32의 배수 크기로 라인을 혼자 쓰게 해야 한다 — 아니면 MPU로 DMA 섹션을 non-cacheable로 만들거나, DMA가 닿는 TCM에 둔다. 캐시 정책은 MPU 속성(Armv7-M TEX/C/B, Armv8-M MAIR)과 기본 메모리 맵이 정하고, M7은 shareable 영역을 캐시하지 않는다. TCM은 결정적이고 캐시 문제도 CRPD도 없어서 ISR · rate loop · 스택에 쓰지만, H7처럼 범용 DMA가 TCM에 못 닿는 칩이 있다. 플래시는 wait state 때문에 느리고 prefetch · ART · I-cache가 그 차이를 메운다 — 클록을 올리기 전에 WS를 먼저 올린다. RTOS는 캐시를 거의 모른다: FreeRTOS에는 캐시 API가 없고, Zephyr는 `sys_cache_*`와 `__nocache`를 주지만 드라이버가 불러야 한다. QEMU는 캐시를 모델링하지 않으므로, 캐시 코드가 QEMU에서 돈다는 건 레지스터 접근이 맞다는 뜻일 뿐이다.

- [ ] 주소를 tag · index · offset으로 나누고, 캐시 크기 = 라인 × way × set을 계산할 수 있다
- [ ] write-back과 write-through의 버스 트래픽 차이, write-through로도 RX invalidate가 필요한 이유를 설명할 수 있다
- [ ] `SCB_EnableDCache()`가 내부에서 하는 일(set/way invalidate → CCR.DC)과 MPU → 캐시 → DMA 순서를 말할 수 있다
- [ ] TX/RX/양방향별로 clean · invalidate를 언제 하는지 표 없이 말할 수 있다
- [ ] "완료 후에도 invalidate"가 필요한 이유(투기 읽기)를 설명할 수 있다
- [ ] 정렬 안 된 invalidate가 이웃 변수를 지우는 과정과 partial line 딜레마를 그림으로 그릴 수 있다
- [ ] MPU로 DMA 섹션을 Normal non-cacheable(TEX=1, C=0, B=0)로 만드는 코드를 쓸 수 있다
- [ ] M7의 "shareable = 캐시 안 됨" 규칙과 Armv8-M MPU의 영역 겹침 금지를 알고 있다
- [ ] ITCM · DTCM · DMA 섹션을 링커 스크립트로 만들고, veneer가 왜 생기는지 설명할 수 있다
- [ ] 쓰는 칩에서 어떤 DMA가 TCM에 닿는지 RM에서 확인하는 습관이 있다
- [ ] 플래시 WS와 클록 변경 순서, ART/I-cache가 루프 크기에 따라 효과가 달라지는 이유를 말할 수 있다
- [ ] FreeRTOS · Zephyr에서 캐시 유지 코드가 어디에 들어가는지, CRPD가 WCET에 왜 들어가는지 말할 수 있다

---

## Bare-Metal 노트와의 관계

| BM ID | BM 주제 | 이 노트의 해당 절 | Bare-Metal 쪽에 남길 내용 |
|---|---|---|---|
| BM B3 | 캐시 원리 (line · set-associative, WB/WT, allocate, hit/miss 비용, 일관성 문제의 근원) | 1절 전체(1.2 주소 분해, 1.3 측정, 1.4 쓰기 정책, 1.5 일관성), 2절(M7 유지 연산), 6절(DMA 일관성) | 이 노트 링크 + Cortex-A의 다단 캐시(L1/L2, PoU vs PoC 구분)와 MMU 페이지 속성으로 정하는 캐시 정책 — MMU · TLB 쪽 관점 |
| BM B9 | 플래시 · SRAM 기술 (wait state, prefetch · ART, 플래시 수명 · ECC, SRAM 뱅크 · 리텐션, TCM) | 4절(TCM, SRAM 뱅크 · 리텐션 · ECC), 5절(WS, prefetch · ART, 수명 · ECC) | 이 노트 링크 + RTOS 없이 startup에서 클록 트리 · WS · ECC RAM 초기화를 하는 코드 순서, 플래시 프로그래밍 드라이버(지우기 · 쓰기 시퀀스) |
| BM D9 (참고) | Cortex-R5/R52 bare-metal (TCM ECC 초기화, MPU) | 4.1 · 4.5절에서 비교로만 언급 | Cortex-R TCM ECC 초기화 상세는 RTOS B9 · BM D9 |

---

## 참고 자료

- Arm, Cortex-M7 Processor Technical Reference Manual (Arm DDI 0489) — L1 캐시 구성(I 2-way, D 4-way, 32 B 라인), TCM 인터페이스, `CACR`(SIWT, ECCDIS, FORCEWT). https://developer.arm.com/documentation/ddi0489/latest/
- Arm, Armv7-M Architecture Reference Manual (Arm DDI 0403) — 기본 메모리 맵 속성, MPU TEX/C/B/S, 캐시 유지 레지스터(`DCIMVAC`, `DCCMVAC`, `DCCIMVAC`, `DCISW` …), `CCSIDR`/`CLIDR`/`CTR`. https://developer.arm.com/documentation/ddi0403/latest/
- Arm, Armv8-M Architecture Reference Manual (Arm DDI 0553) — MAIR · RBAR · RLAR MPU.
- CMSIS 6 Core 소스: `CMSIS/Core/Include/m-profile/armv7m_cachel1.h`(`SCB_EnableDCache`, `SCB_*DCache_by_Addr`), `m-profile/armv7m_mpu.h`(`ARM_MPU_RASR`), `core_cm7.h` — 랩 `/var/rtoslab/zephyrproject/modules/hal/cmsis_6/`에서 확인. https://github.com/ARM-software/CMSIS_6
- FreeRTOS Kernel V11.3.2 소스: `portable/GCC/ARM_CM7/ReadMe.txt`, `portable/GCC/ARM_CM7/r0p1/port.c`(erratum 837070), `portable/GCC/ARM_CM4_MPU/portmacro.h`(TEX/C/B 표) — 랩 `/var/rtoslab/src/FreeRTOS-Kernel`.
- Zephyr v4.4.2 소스: `include/zephyr/cache.h`(`sys_cache_*`), `arch/arm/core/cortex_m/cache.c`, `include/zephyr/linker/section_tags.h`(`__nocache`, `__itcm_section`, `__dtcm_*_section`), `arch/arm/core/mpu/arm_core_mpu.c`(`static_regions[]`), `include/zephyr/arch/arm/mpu/arm_mpu_v8.h`(MAIR 속성), `arch/arm/core/Kconfig`(`ARCH_HAS_NOCACHE_MEMORY_SUPPORT`), `dts/arm/st/h7/stm32h7.dtsi` · `stm32h743.dtsi`(H7 메모리 맵).
- Zephyr 문서, Cache Interface — https://docs.zephyrproject.org/latest/hardware/cache/index.html
- ST, RM0433 STM32H742/743/750/753 reference manual — 버스 매트릭스, DMA 마스터별 접근 가능 메모리, 플래시 wait state · ECC (이 노트의 H7 DMA 접근 표는 원문 재확인 필요).
- ST, RM0090 STM32F405/407/415/417/427/437/429/439 reference manual — 플래시 wait state 표(2.7~3.6 V, 30 MHz당 1 WS), ART accelerator.
- ST, AN4839 Level 1 cache on STM32F7 Series and STM32H7 Series — M7 캐시와 DMA 일관성, MPU 설정 예. https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf
- ST, AN4838 Managing memory protection unit in STM32 MCUs — MPU 속성과 캐시.
- EL H9(이 사이트의 Embedded Linux 노트) — Linux DMA API, coherent vs streaming, arm64 캐시 유지(`dcache_clean_poc` 등).
