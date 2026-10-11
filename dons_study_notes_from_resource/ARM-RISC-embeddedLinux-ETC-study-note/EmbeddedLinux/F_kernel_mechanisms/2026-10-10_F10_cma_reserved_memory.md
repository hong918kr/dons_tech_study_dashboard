# F10. 큰 연속 메모리 — CMA, reserved-memory, memblock, 카메라·GPU 버퍼

> **이 노트를 다 읽으면**: 카메라·디스플레이·코덱이 왜 "물리적으로 연속인" 수 MiB 버퍼를 요구하고, 오래 돈 시스템에서 버디 할당자가 왜 그걸 못 주는지 측정 결과로 설명할 수 있다 · DT `/reserved-memory`의 세 종류(`no-map`, 그냥 예약, `reusable`=CMA)를 골라 쓰고 `memory-region` + `of_reserved_mem_device_init()`으로 드라이버에 붙일 수 있다 · `dma_alloc_coherent()` → `dma_direct_alloc()` → `dma_alloc_contiguous()` → `cma_alloc()` → `alloc_contig_range()` 경로를 소스 위치와 함께 말할 수 있다 · `/proc/meminfo`, `/sys/kernel/debug/cma`, `/proc/pagetypeinfo`, `memblock` debugfs, `page_owner`로 CMA 문제를 진단할 수 있다
> **왜 필요한가**: 카메라 회사(Verkada)·로보틱스(Scout AI) 면접에서 "4K 프레임 버퍼를 어디서 할당하나요?", "CMA가 뭐죠? 왜 가끔 느리거나 실패하죠?", "reserved-memory의 no-map은 언제 쓰나요?"가 그대로 나오고, 입사 후 첫 카메라 bring-up에서 `cma: alloc failed`나 `-ENOMEM`을 만나게 된다
> **Don 기준 난이도**: DMA 엔진이 물리 주소를 쓴다는 것, 디스크립터·scatter-gather, 캐시 코히어런시는 SSD 컨트롤러에서 매일 하던 일이다 / 새로 배울 것은 "Linux가 부팅 후에도 수 MiB의 연속 물리 메모리를 어떻게 확보하나" — memblock, DT 예약, CMA의 migration, 그리고 DMA API가 그 사이를 어떻게 고르는가
> **선행 노트**: F9(커널 메모리 관리: 페이지, zone, 버디 할당자, GFP), D2·D3·D4(DT 문법, 매칭, `memory-region` 같은 phandle 자원) — H9(DMA API), I5(V4L2), I8(DRM/KMS)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

센서 키트의 카메라 하나를 생각해 보자. 1920×1080 해상도, NV12 포맷(Y 평면 1바이트/픽셀 + UV 평면 0.5바이트/픽셀)이면 한 프레임이 `1920 × 1080 × 1.5 = 3,110,400` 바이트, 약 **3 MiB**다. 30 fps로 받으려면 드라이버는 이런 버퍼를 보통 4~6개 돌려 쓴다 (하나는 DMA가 쓰는 중, 하나는 앱이 읽는 중, 나머지는 대기열). 카메라가 4대면 4 × 5 × 3 MiB = 60 MiB다. 4K(3840×2160) NV12는 한 장에 약 11.9 MiB다.

문제는 이 버퍼가 **물리적으로 연속**이어야 하는 경우가 많다는 것이다. 카메라 수신기(CSI-2 receiver)나 ISP 뒤의 DMA 엔진은 "시작 물리 주소 + 길이"(또는 "평면별 시작 주소 + stride")만 레지스터로 받는 단순한 구조가 흔하다. Don이 SSD 컨트롤러에서 다루던 DMA를 떠올려 보면, 디스크립터 하나에 "버스 주소 + 길이"를 적고 엔진이 그 범위를 쭉 읽거나 쓴다. scatter-gather 리스트를 지원하는 엔진이면 흩어진 4 KiB 페이지들을 이어 붙일 수 있지만, 지원하지 않거나 IOMMU가 없으면 **3 MiB를 한 덩어리로 줘야 한다**.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="40" width="110" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="65" y="63" text-anchor="middle" font-size="13">이미지 센서</text>
  <text x="65" y="83" text-anchor="middle" font-size="12">raw 또는 YUV</text>
  <rect x="142" y="40" width="110" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="197" y="63" text-anchor="middle" font-size="13">CSI-2 수신기</text>
  <text x="197" y="83" text-anchor="middle" font-size="12">MIPI D-PHY</text>
  <rect x="274" y="40" width="110" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="329" y="63" text-anchor="middle" font-size="13">ISP · DMA</text>
  <text x="329" y="83" text-anchor="middle" font-size="12">버스 마스터</text>
  <rect x="406" y="40" width="110" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="2"/>
  <text x="461" y="63" text-anchor="middle" font-size="13">CMA 버퍼</text>
  <text x="461" y="83" text-anchor="middle" font-size="12">NV12 ≈ 3 MiB</text>
  <rect x="538" y="40" width="130" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="603" y="63" text-anchor="middle" font-size="13">GPU · 인코더</text>
  <text x="603" y="83" text-anchor="middle" font-size="12">디스플레이</text>
  <line x1="120" y1="68" x2="140" y2="68" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="252" y1="68" x2="272" y2="68" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="384" y1="68" x2="404" y2="68" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="516" y1="68" x2="536" y2="68" stroke="currentColor" stroke-dasharray="5,4" marker-end="url(#ah1)"/>
  <text x="329" y="25" text-anchor="middle" font-size="12">물리 주소(dma_addr_t)로 씀</text>
  <rect x="290" y="170" width="200" height="50" rx="6" fill="#888" fill-opacity="0.08" stroke="#888"/>
  <text x="390" y="191" text-anchor="middle" font-size="13">유저 앱</text>
  <text x="390" y="210" text-anchor="middle" font-size="12">GStreamer, 추론 엔진 등</text>
  <line x1="450" y1="96" x2="420" y2="168" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="445" y="140" font-size="12">VIDIOC_EXPBUF → dma-buf fd</text>
  <line x1="490" y1="190" x2="590" y2="98" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="560" y="160" font-size="12">fd import</text>
  <text x="560" y="176" font-size="12">(zero-copy)</text>
  <line x1="274" y1="250" x2="516" y2="250" stroke="#4a7bd0" stroke-width="2"/>
  <line x1="274" y1="244" x2="274" y2="256" stroke="#4a7bd0" stroke-width="2"/>
  <line x1="516" y1="244" x2="516" y2="256" stroke="#4a7bd0" stroke-width="2"/>
  <text x="395" y="275" text-anchor="middle" font-size="12">IOMMU·SG 없는 DMA라면 이 구간의 버퍼는 물리적으로 연속이어야 한다</text>
</svg>
```
그림 1 — 카메라 버퍼 파이프라인. 센서 → CSI-2 → ISP/DMA가 프레임을 DRAM의 큰 버퍼에 직접 쓰고, 앱은 그 버퍼를 dma-buf fd로 받아 GPU·인코더·디스플레이에 복사 없이 넘긴다. 이 노트는 가운데 파란 상자(큰 연속 버퍼)를 어디서 어떻게 얻는지를 다룬다.

그런데 Linux 커널의 일반 할당자(F9의 버디 할당자)는 부팅 직후에는 큰 블록을 잘 주지만, **시스템이 몇 시간 돌고 나면 같은 크기를 못 준다**. 빈 메모리 총량은 충분한데 4 KiB 구멍들로 쪼개져 있기 때문이다(단편화, fragmentation). 그래서 커널은 세 가지 해법을 준비해 두었다.

| 해법 | 언제 확보하나 | 남는 시간에 그 메모리는? | 대표 사용처 |
|---|---|---|---|
| **carveout** (`/reserved-memory` + `no-map` 또는 그냥 예약) | 부팅 초기 (memblock 단계) | **놀고 있다** (커널이 못 씀) | 펌웨어·TEE·DSP 전용 영역, ramoops 로그 |
| **CMA** (`reusable` 또는 `cma=`) | 부팅 초기에 "자리"만 예약 | movable 페이지(유저 메모리·페이지 캐시)가 **빌려 쓴다** | 카메라·디스플레이·코덱 버퍼 |
| **IOMMU** (SMMU) | 실행 중 아무 때나 | 해당 없음 (흩어진 페이지를 IOVA로 이어 붙임) | IOMMU가 있는 SoC의 대부분 장치 |

이 노트의 결론을 미리 말하면 다섯 줄이다.

1. 버디 할당자의 한계는 `MAX_PAGE_ORDER = 10`(4 KiB 페이지에서 **4 MiB**)이고, 그 이하라도 단편화되면 실패한다. compaction이 movable 페이지를 옮겨 구멍을 메워 주지만, unmovable 커널 페이지가 박혀 있으면 소용없다 (§2, 실측).
2. **memblock**은 버디 할당자가 생기기 전의 부팅 초기 할당자이고, DT `/reserved-memory`와 CMA 예약이 모두 이 단계에서 일어난다 (§3).
3. `/reserved-memory`는 `no-map`(선형 매핑에서 제외), 속성 없음(매핑은 하되 할당 금지), `reusable`(CMA로 빌려 주기) 세 가지다 (§4).
4. **CMA**는 "평소엔 movable 페이지에게 빌려 주고, 드라이버가 달라고 하면 그 페이지들을 다른 곳으로 migration한 뒤 돌려받는" 영역이다. 그래서 단편화와 무관하게 큰 연속 버퍼를 보장하지만, 빌려 간 페이지가 많으면 **느려진다** (§5, §7 측정).
5. 드라이버는 CMA를 직접 부르지 않는다. `dma_alloc_coherent()`(H9)를 부르면 DMA API가 장치의 `memory-region`, 기본 CMA, 버디 순서로 고른다 (§6, §9).

> 실행 환경: 모든 출력은 Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`) 안의 공용 QEMU 랩에서 실제로 돌린 결과다. 게스트는 QEMU `virt` 머신(cortex-a57, **1 GiB, 2 CPU**, TCG 에뮬레이션) + 디버그 커널 **6.18.55-dbg**(`/var/lab/out/Image-dbg`: `CONFIG_CMA_DEBUGFS`, `CONFIG_PAGE_OWNER`, `CONFIG_DEBUG_VM` 포함) + BusyBox initramfs이고, 모듈은 `make -C /var/lab/out/linux-build-dbg M=$PWD`로 빌드했다. **시간 값은 KVM 없는 에뮬레이션 + 디버그 옵션 + 다른 작업과 CPU를 나눠 쓴 상태의 값이라 실제 보드보다 수~수십 배 느리다.** 비교(상대값)만 의미가 있다. 타임스탬프·가상 주소는 실행마다 다르다.

---

## 1. 왜 "물리적으로 연속"이어야 하나

### 1.1 직관 — DMA 엔진은 MMU를 거치지 않는다

CPU는 가상 주소를 쓴다. 커널이 `vmalloc()`으로 받은 4 MiB는 가상 주소로는 연속이지만, 실제 물리 페이지는 DRAM 여기저기 흩어져 있다. CPU의 MMU가 페이지 테이블로 이어 붙여 줄 뿐이다.

DMA 엔진(버스 마스터)은 CPU의 MMU를 거치지 않고 **버스 주소**로 DRAM에 직접 접근한다. 그래서 엔진이 "시작 주소 + 길이"만 안다면, 그 범위는 물리적으로(정확히는 버스 주소로) 연속이어야 한다. Don의 SSD 펌웨어 경험으로 말하면, NVMe의 PRP 리스트·SGL이 바로 "흩어진 호스트 메모리를 장치에게 설명하는 방법"이었다. 그런 리스트를 지원하지 않는 단순한 엔진에게는 한 덩어리 버퍼가 필요하다.

### 1.2 정의 — 세 가지 장치 유형

| 장치 유형 | 버퍼를 어떻게 보나 | Linux에서 필요한 것 | 예 |
|---|---|---|---|
| 단순 DMA (base + length) | 물리 연속 한 덩어리 | **연속 물리 메모리** (CMA, carveout) | 많은 CSI-2 수신기 DMA, 오래된 디스플레이 컨트롤러, 간단한 코덱 |
| scatter-gather DMA | 디스크립터 체인으로 흩어진 페이지 | `sg_table` + `dma_map_sg()` (H9) | 네트워크 MAC, USB, 대부분의 스토리지 컨트롤러 |
| IOMMU(SMMU) 뒤의 장치 | 장치용 가상 주소(IOVA)가 연속 | IOMMU가 흩어진 페이지를 IOVA로 매핑 | IOMMU를 갖춘 SoC의 GPU·ISP·코덱 |

IOMMU가 있으면 큰 연속 물리 메모리가 필요 없다. 하지만 임베디드 SoC에서는 (1) IOMMU가 아예 없거나, (2) 특정 블록(예: 카메라 수신기)만 IOMMU 밖에 있거나, (3) 성능·전력·지연 때문에 IOMMU를 끄거나 바이패스하는 경우가 흔하다. 그래서 카메라·디스플레이 BSP에는 거의 항상 CMA 설정이 들어 있다.

### 1.3 얼마나 큰가 — 대표 버퍼 크기

| 버퍼 | 계산 | 크기 |
|---|---|---|
| 1080p NV12 프레임 | 1920 × 1080 × 1.5 | 3,110,400 B ≈ 2.97 MiB |
| 1080p YUYV(4:2:2) 프레임 | 1920 × 1080 × 2 | 4,147,200 B ≈ 3.96 MiB |
| 4K NV12 프레임 | 3840 × 2160 × 1.5 | 12,441,600 B ≈ 11.87 MiB |
| 1080p XRGB8888 프레임버퍼 | 1920 × 1080 × 4 | 8,294,400 B ≈ 7.91 MiB (더블 버퍼면 ×2) |
| 버디 할당자 최대 블록 | 4 KiB × 2^10 | 4 MiB (`MAX_PAGE_ORDER = 10`, arm64 4K 페이지) |

마지막 줄이 중요하다. **4K 프레임 하나(11.9 MiB)는 버디 할당자가 원리적으로 한 번에 줄 수 없는 크기**다. 단편화 이전의 문제다. 8 MiB 프레임버퍼도 마찬가지다. 랩 커널에서 확인해 보면 이렇다.

```sh
limactl shell elinux -- bash -lc 'grep -E "CONFIG_(CMA|DMA_CMA|CMA_SIZE_MBYTES|CMA_DEBUGFS|CMA_AREAS|DMABUF_HEAPS|PAGE_OWNER|OF_RESERVED_MEM|ARCH_KEEP_MEMBLOCK|IOMMU_DMA)[ =]" /var/lab/out/linux-build-dbg/.config'
```

```text
CONFIG_ARCH_KEEP_MEMBLOCK=y
CONFIG_CMA=y
CONFIG_CMA_DEBUGFS=y
CONFIG_CMA_AREAS=20
CONFIG_OF_RESERVED_MEM=y
# CONFIG_DMABUF_HEAPS is not set
CONFIG_IOMMU_DMA=y
CONFIG_DMA_CMA=y
CONFIG_CMA_SIZE_MBYTES=32
CONFIG_PAGE_OWNER=y
```

출력에서 볼 것: arm64 defconfig 기반이라 `CONFIG_CMA`, `CONFIG_DMA_CMA`가 켜져 있고 기본 CMA 크기가 **32 MiB**(`CONFIG_CMA_SIZE_MBYTES=32`)다. `CONFIG_ARCH_KEEP_MEMBLOCK=y`라서 부팅 후에도 memblock 표를 debugfs로 볼 수 있다(§3). `CONFIG_DMABUF_HEAPS`는 꺼져 있어서 `/dev/dma_heap/`은 이 랩에서 볼 수 없다(§8).

### 1.4 흔한 함정

- "`kmalloc(4 MiB)`나 `vmalloc()`으로 받으면 되지 않나?" — `kmalloc`은 내부적으로 버디에서 연속 페이지를 받으므로 4 MiB가 상한이고 단편화에 약하다. `vmalloc`은 물리적으로 연속이 아니다. DMA에는 둘 다 답이 아니다 (F9).
- "주소가 연속인지 `virt_to_phys(va)` 한 번으로 확인" — 첫 페이지만 본 것이다. §6의 모듈처럼 **페이지마다** pfn을 확인해야 연속성을 검증한 것이다.

---

## 2. 버디 할당자는 왜 큰 블록에서 실패하나 — 단편화

### 2.1 직관 — 체스판이 된 메모리

F9에서 본 버디 할당자는 2^order 페이지 단위의 블록을 관리한다. order 10 블록(4 MiB)을 주려면 **4 MiB로 정렬된 1024개 페이지가 전부 비어 있어야** 한다. 그런데 커널이 오래 돌면 슬랩 페이지, 네트워크 버퍼, 페이지 테이블 같은 **옮길 수 없는(unmovable) 커널 페이지**가 메모리 곳곳에 하나씩 박힌다. 빈 페이지 총량은 수백 MiB인데, 4 MiB 정렬 구간마다 하나씩만 박혀 있어도 order 10은 0개가 된다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <text x="10" y="50" font-size="13">부팅 직후</text>
  <rect x="150" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="180" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="210" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="240" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="270" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="300" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="330" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="360" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="390" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="420" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="450" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="480" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="510" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="540" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="570" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="600" y="30" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="150" y="80" font-size="12">빈 페이지 16개 = 정렬된 4페이지 블록 4개, 16페이지 블록 1개도 가능</text>
  <text x="10" y="135" font-size="13">몇 시간 후</text>
  <rect x="150" y="115" width="30" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="165" y="135" text-anchor="middle" font-size="12">U</text>
  <rect x="180" y="115" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="210" y="115" width="30" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="225" y="135" text-anchor="middle" font-size="12">M</text>
  <rect x="240" y="115" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="270" y="115" width="30" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="285" y="135" text-anchor="middle" font-size="12">U</text>
  <rect x="300" y="115" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="330" y="115" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="360" y="115" width="30" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="375" y="135" text-anchor="middle" font-size="12">M</text>
  <rect x="390" y="115" width="30" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="405" y="135" text-anchor="middle" font-size="12">U</text>
  <rect x="420" y="115" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="450" y="115" width="30" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="465" y="135" text-anchor="middle" font-size="12">M</text>
  <rect x="480" y="115" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="510" y="115" width="30" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="525" y="135" text-anchor="middle" font-size="12">U</text>
  <rect x="540" y="115" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="570" y="115" width="30" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="585" y="135" text-anchor="middle" font-size="12">M</text>
  <rect x="600" y="115" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="150" y="165" font-size="12">빈 페이지 8개 (절반이 비었다) — 그러나 정렬된 4페이지 빈 블록은 0개</text>
  <text x="10" y="220" font-size="13">compaction 후</text>
  <rect x="150" y="200" width="30" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="165" y="220" text-anchor="middle" font-size="12">U</text>
  <rect x="180" y="200" width="30" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="195" y="220" text-anchor="middle" font-size="12">M</text>
  <rect x="210" y="200" width="30" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="225" y="220" text-anchor="middle" font-size="12">M</text>
  <rect x="240" y="200" width="30" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="255" y="220" text-anchor="middle" font-size="12">M</text>
  <rect x="270" y="200" width="30" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="285" y="220" text-anchor="middle" font-size="12">U</text>
  <rect x="300" y="200" width="30" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="315" y="220" text-anchor="middle" font-size="12">M</text>
  <rect x="330" y="200" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="360" y="200" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="390" y="200" width="30" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="405" y="220" text-anchor="middle" font-size="12">U</text>
  <rect x="420" y="200" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="450" y="200" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="480" y="200" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="510" y="200" width="30" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="525" y="220" text-anchor="middle" font-size="12">U</text>
  <rect x="540" y="200" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="570" y="200" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="600" y="200" width="30" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="150" y="250" font-size="12">M은 앞으로 모았지만 4칸마다 U가 박혀 있어 정렬된 4페이지 블록은 여전히 0개</text>
  <rect x="150" y="285" width="16" height="16" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="172" y="298" font-size="12">U = unmovable (슬랩, 페이지 테이블, 드라이버)</text>
  <rect x="460" y="285" width="16" height="16" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="482" y="298" font-size="12">M = movable (유저)</text>
  <rect x="150" y="308" width="16" height="16" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="172" y="321" font-size="12">빈 페이지</text>
</svg>
```
그림 2 — 단편화. 16칸짜리 축소판이다(실제로는 order 10 = 1024칸). 빈 페이지의 "총량"이 아니라 "정렬된 연속 구간"이 문제다. compaction은 movable 페이지를 옮겨 구멍을 메우지만, unmovable 페이지는 옮길 수 없으므로 그 구간은 끝내 비지 않는다.

### 2.2 정의 — migrate type, pageblock, compaction

- **migrate type**: 버디 할당자는 빈 블록을 용도별 리스트로 나눠 둔다. `Unmovable`(커널 객체), `Movable`(유저 페이지·페이지 캐시처럼 페이지 테이블만 고치면 옮길 수 있는 것), `Reclaimable`, `HighAtomic`, 그리고 **`CMA`**. 같은 종류끼리 모아 두면 단편화가 덜 퍼진다.
- **pageblock**: migrate type이 붙는 단위. arm64 4K 페이지 + THP에서는 order 9 = 512페이지 = **2 MiB**다(랩 출력 `Page block order: 9`, §4.6). CMA 영역의 정렬 최소 단위(`CMA_MIN_ALIGNMENT_BYTES`)도 이 값이다.
- **compaction**: movable 페이지를 한쪽으로 옮겨 빈 구간을 만드는 작업. `GFP_KERNEL`처럼 잠들 수 있는 할당은 실패 직전에 **direct compaction**을 시도한다. `GFP_ATOMIC`은 잠들 수 없으니 compaction 없이 지금 있는 블록만 본다.
- **watermark**: zone마다 남겨 두는 예비 페이지 수(min/low/high). 보통 할당은 low 아래로 내려가면 실패하거나 reclaim을 시작한다. 그래서 "메모리를 다 썼다"고 해도 예비분에는 큰 블록이 남아 있을 수 있다.

### 2.3 예제 1 — 메모리를 체스판으로 만드는 모듈 `f10_frag`

무엇을 확인하나: 단일 페이지를 할당 가능한 만큼 잡고 **하나 걸러 반납**해서 메모리를 체스판으로 만든 뒤, (1) `GFP_ATOMIC`으로 order 0~10을 시도하고 (2) `GFP_KERNEL`로 4 MiB(order 10) 블록을 실패할 때까지 계속 잡아 본다. `deep=1`이면 `__GFP_MEMALLOC`으로 워터마크 예비분까지 먹고, 큰 블록부터 잡아 `split_page()`로 쪼갠다. (`__GFP_MEMALLOC`은 메모리 회수 경로 전용 플래그다. 실험 장치로만 쓴다.)

앞부분 — 파라미터와 시험 함수:

```c
// f10_frag.c — 단일 페이지를 잔뜩 잡고 하나 걸러 풀어 메모리를 "체스판"으로 만든 뒤
//              order 0..10 연속 할당을 시도한다
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/ktime.h>

static unsigned long maxpages = 400000;
module_param(maxpages, ulong, 0444);
static bool deep;	/* 1: __GFP_MEMALLOC로 워터마크 예비분까지 먹는다 (데모 전용!) */
module_param(deep, bool, 0444);
static struct page **pg;
static unsigned long n;

static void f10_try_orders(void)
{	/* (1) GFP_ATOMIC: 잠들 수 없음 → compaction 없이 지금 있는 블록만 */
	struct page *held[32];
	int order, k = 0;

	for (order = 0; order <= MAX_PAGE_ORDER; order++) {
		struct page *p = alloc_pages(GFP_ATOMIC | __GFP_NOWARN, order);

		pr_info("f10_frag: ATOMIC order %2d (%5lu KiB): %s\n", order,
			(PAGE_SIZE << order) >> 10, p ? "ok" : "FAIL");
		if (p)
			__free_pages(p, order);
	}
	/* (2) GFP_KERNEL: direct compaction 허용. 4 MiB를 실패할 때까지 계속 잡는다 */
	while (k < 32) {
		u64 t = ktime_get_ns();
		struct page *p = alloc_pages(GFP_KERNEL | __GFP_NOWARN, MAX_PAGE_ORDER);

		t = ktime_get_ns() - t;
		pr_info("f10_frag: KERNEL order 10 #%d: %s pfn %#lx %llu us\n", k,
			p ? "ok" : "FAIL", p ? page_to_pfn(p) : 0, t / 1000);
		if (!p)
			break;
		held[k++] = p;
	}
	while (k--)
		__free_pages(held[k], MAX_PAGE_ORDER);
}
```

뒷부분 — 잡기, 체스판 만들기, 정리:

```c
static void f10_grab(void)
{	/* deep=0: 보통 할당(워터마크에서 멈춤) / deep=1: 큰 블록부터 예비분까지 전부 */
	gfp_t gfp = deep ? GFP_NOWAIT | __GFP_MEMALLOC
			 : GFP_KERNEL | __GFP_NORETRY | __GFP_NOWARN;
	int order;

	for (order = deep ? MAX_PAGE_ORDER : 0; order >= 0; order--) {
		struct page *p;
		unsigned long i;

		while (n + (1UL << order) <= maxpages && (p = alloc_pages(gfp, order))) {
			if (order)
				split_page(p, order);	/* 2^order개의 독립 4 KiB 페이지로 */
			for (i = 0; i < (1UL << order); i++)
				pg[n++] = p + i;
		}
	}
}

static int __init f10_frag_init(void)
{
	unsigned long i;

	pg = vzalloc(array_size(maxpages, sizeof(*pg)));
	if (!pg)
		return -ENOMEM;
	f10_grab();
	pr_info("f10_frag: after grab: free %lu pages (CMA %lu)\n",
		global_zone_page_state(NR_FREE_PAGES), global_zone_page_state(NR_FREE_CMA_PAGES));
	for (i = 0; i < n; i += 2) {	/* 하나 걸러 반납 → 4 KiB 구멍만 남는다 */
		__free_page(pg[i]);
		pg[i] = NULL;
	}
	pr_info("f10_frag: grabbed %lu pages (%lu MiB), holding every 2nd (%lu MiB)\n",
		n, n >> (20 - PAGE_SHIFT), (n / 2) >> (20 - PAGE_SHIFT));
	f10_try_orders();
	return 0;
}

static void __exit f10_frag_exit(void)
{
	unsigned long i;

	for (i = 1; i < n; i += 2)
		__free_page(pg[i]);
	vfree(pg);
}
module_init(f10_frag_init);
module_exit(f10_frag_exit);
MODULE_DESCRIPTION("F10 fragmentation demo");
MODULE_LICENSE("GPL");
```

요점 세 가지.

- `__GFP_NORETRY`: 메모리가 바닥나면 OOM killer를 부르지 않고 바로 `NULL`을 돌려준다. 실험용 "다 먹기" 루프에 꼭 필요하다.
- `split_page(p, order)`: order N 블록을 독립된 4 KiB 페이지 2^N개로 바꾼다. 그래야 하나씩 `__free_page()`할 수 있다.
- 모듈은 `GFP_KERNEL`(unmovable)로 잡기 때문에 **CMA 영역의 빈 페이지는 하나도 못 가져간다.** CMA는 movable 할당만 빌려 쓸 수 있기 때문이다. 이게 §7 실험의 핵심이다.

빌드(경고 0개):

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/F10/mod && make -C /var/lab/out/linux-build-dbg M=$PWD clean >/dev/null && make -j2 -C /var/lab/out/linux-build-dbg M=$PWD modules 2>&1 | grep -v "^make"'
```

```text
  CC [M]  f10_cma.o
  CC [M]  f10_frag.o
  MODPOST Module.symvers
  CC [M]  f10_cma.mod.o
  CC [M]  .module-common.o
  CC [M]  f10_frag.mod.o
  LD [M]  f10_cma.ko
  LD [M]  f10_frag.ko
```

(`Kbuild` 파일은 `obj-m := f10_cma.o f10_frag.o` 한 줄이다. `f10_cma.c`는 §6에서 본다.)

### 2.4 예제 2 — 부팅 스크립트와 게스트 관찰 스크립트

무엇을 확인하나: 모든 실험이 쓰는 공통 부팅 스크립트다. 랩 rootfs를 복사해 `.ko`·스크립트·유저 프로그램을 넣고, 디버그 커널을 1 GiB·2 CPU로 부팅한다. 두 번째 인자는 커널 명령행에 붙는다(`cma=`, `memblock=debug` 같은 실험 인자). `f10.xxx=`로 시작하는 인자는 게스트 스크립트가 읽어 `insmod` 파라미터로 넘긴다.

```sh
#!/bin/sh
# boot.sh <dtb경로 또는 -> <추가 커널 인자> <게스트 스크립트>  — dbg 커널, 1 GB, 2 CPU
set -e
W=/var/tmp/F10; S=/Users/donh/.elinux_scratch/F10
rm -rf $W/r && mkdir -p $W/r && cp -a /var/lab/build/rootfs/. $W/r/
cp $S/*.sh $W/r/ 2>/dev/null; cp $W/mod/*.ko $W/r/ 2>/dev/null || true; cp $W/f10_hog $W/r/ 2>/dev/null || true
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
DTB=""; [ "$1" != "-" ] && DTB="-dtb $1"
timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 1024M -smp 2 -nographic \
  -kernel /var/lab/out/Image-dbg -initrd $W/i.gz $DTB \
  -append "console=ttyAMA0 quiet $2 LAB_CMD=sh~/$3"
```

```sh
#!/bin/sh
# ex3_frag.sh — 메모리를 체스판으로 만든 뒤 buddy vs CMA
echo "=== before ==="; grep -E "MemFree|CmaFree" /proc/meminfo; cat /proc/buddyinfo
insmod /f10_frag.ko $(cat /proc/cmdline | tr " " "\n" | grep "^f10\." | sed "s/^f10\.//")
echo "=== after f10_frag ==="; dmesg | grep "f10_frag:"
grep -E "MemFree|CmaFree" /proc/meminfo; cat /proc/buddyinfo
echo "=== pagetypeinfo (Normal/DMA, free blocks per order) ==="
sed -n '/Free pages count per migrate type/,/^$/p' /proc/pagetypeinfo | grep -E "Order|zone +DMA,|Unmovable|Movable|CMA"
echo "=== same 4 MiB from CMA ==="
insmod /f10_cma.ko sizes_mb=4
dmesg | grep "f10_cma " | grep MiB
```

`/proc/buddyinfo`의 숫자 열은 왼쪽부터 order 0, 1, …, 10의 **빈 블록 개수**다. `/proc/pagetypeinfo`는 같은 정보를 migrate type별로 나눠 보여 준다.

### 2.5 예제 3 — 보통 할당으로 다 먹기 (`deep=0`)

```sh
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/F10/boot.sh /var/tmp/F10/f10_dt.dtb "" ex3_frag.sh'
```

```text
=== after f10_frag ===
[    6.411882] f10_frag: loading out-of-tree module taints kernel.
[    9.458399] f10_frag: after grab: free 18231 pages (CMA 12192)
[    9.943233] f10_frag: grabbed 204853 pages (800 MiB), holding every 2nd (400 MiB)
[    9.943874] f10_frag: ATOMIC order  0 (    4 KiB): ok
[    9.943980] f10_frag: ATOMIC order  1 (    8 KiB): ok
[    9.944012] f10_frag: ATOMIC order  2 (   16 KiB): ok
[    9.944039] f10_frag: ATOMIC order  3 (   32 KiB): ok
[    9.944089] f10_frag: ATOMIC order  4 (   64 KiB): ok
[    9.944159] f10_frag: ATOMIC order  5 (  128 KiB): ok
[    9.944219] f10_frag: ATOMIC order  6 (  256 KiB): ok
[    9.944259] f10_frag: ATOMIC order  7 (  512 KiB): ok
[    9.944299] f10_frag: ATOMIC order  8 ( 1024 KiB): ok
[    9.944341] f10_frag: ATOMIC order  9 ( 2048 KiB): ok
[    9.944391] f10_frag: ATOMIC order 10 ( 4096 KiB): ok
[    9.944539] f10_frag: KERNEL order 10 #0: ok pfn 0x7a000 61 us
[    9.944619] f10_frag: KERNEL order 10 #1: ok pfn 0x7a400 38 us
[    9.944746] f10_frag: KERNEL order 10 #2: ok pfn 0x7a800 107 us
[    9.944805] f10_frag: KERNEL order 10 #3: ok pfn 0x7ac00 39 us
[    9.944860] f10_frag: KERNEL order 10 #4: ok pfn 0x7b000 37 us
[    9.989535] f10_frag: KERNEL order 10 #5: ok pfn 0x42800 44649 us
[   10.155418] f10_frag: KERNEL order 10 #6: FAIL pfn 0x0 165804 us
MemFree:          512776 kB
```

(출력 앞뒤의 `before`, pagetypeinfo 부분은 다음 예제와 같은 모양이라 생략했다.)

출력에서 볼 것:

- `after grab: free 18231 pages (CMA 12192)` — 보통 할당은 **CMA가 아닌 빈 페이지가 6039개(약 23.6 MiB) 남은 시점에 멈췄다**. 이게 워터마크 예비분이다. 그리고 CMA의 12192페이지(약 47.6 MiB)는 손도 못 댔다.
- 800 MiB를 잡고 절반을 돌려줬으니 빈 메모리는 400 MiB 이상이다. 그런데 order 10은 **딱 6개**만 나왔다. 처음 5개(#0~#4, 수십 µs)는 예비분 안에 원래 있던 큰 블록이고, #5는 **44.6 ms가 걸렸다** — direct compaction이 movable 페이지를 옮겨 하나를 만들어 낸 것이다. #6은 166 ms 동안 애쓰다가 실패했다.
- 결론: **빈 메모리 400 MiB가 있어도 4 MiB 연속은 24 MiB어치밖에 안 나온다.**

### 2.6 예제 4 — 예비분까지 다 먹기 (`deep=1`)

```sh
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/F10/boot.sh /var/tmp/F10/f10_dt.dtb "f10.deep=1" ex3_frag.sh'
```

```text
lab initramfs: 6.18.55-dbg up 26.12s
=== before ===
MemFree:          901316 kB
CmaFree:           48768 kB
Node 0, zone      DMA      3      4      4      1      3      1      1      1      7      7    215 
=== after f10_frag ===
[   31.051705] f10_frag: loading out-of-tree module taints kernel.
[   31.137890] f10_frag: after grab: free 12206 pages (CMA 12192)
[   34.902537] f10_frag: grabbed 213207 pages (832 MiB), holding every 2nd (416 MiB)
[   34.903190] f10_frag: ATOMIC order  0 (    4 KiB): ok
[   34.910538] f10_frag: ATOMIC order  1 (    8 KiB): ok
[   34.910620] f10_frag: ATOMIC order  2 (   16 KiB): ok
[   34.910668] f10_frag: ATOMIC order  3 (   32 KiB): ok
[   34.910724] f10_frag: ATOMIC order  4 (   64 KiB): ok
[   34.910824] f10_frag: ATOMIC order  5 (  128 KiB): ok
[   34.910902] f10_frag: ATOMIC order  6 (  256 KiB): ok
[   34.911162] f10_frag: ATOMIC order  7 (  512 KiB): ok
[   34.911313] f10_frag: ATOMIC order  8 ( 1024 KiB): ok
[   34.911446] f10_frag: ATOMIC order  9 ( 2048 KiB): ok
[   34.911573] f10_frag: ATOMIC order 10 ( 4096 KiB): ok
[   34.911879] f10_frag: KERNEL order 10 #0: ok pfn 0x45800 121 us
[   35.134038] f10_frag: KERNEL order 10 #1: FAIL pfn 0x0 222051 us
MemFree:          496484 kB
CmaFree:           48768 kB
Node 0, zone      DMA 106425     16     12     14     14     22      5      3      4      5     12 
=== pagetypeinfo (Normal/DMA, free blocks per order) ===
Node    0, zone      DMA, type    Unmovable >100000     10      7      4      7      8      2      0      0      1      1 
Node    0, zone      DMA, type      Movable      0      1      1      1      1      3      1      1      3      2      0 
Node    0, zone      DMA, type  Reclaimable      2      5      4      2      6     10      2      1      0      1      0 
Node    0, zone      DMA, type   HighAtomic      0      0      0      0      0      0      0      0      0      0      0 
Node    0, zone      DMA, type          CMA      0      0      0      0      0      1      0      1      1      1     11 
Node    0, zone      DMA, type      Isolate      0      0      0      0      0      0      0      0      0      0      0 
=== same 4 MiB from CMA ===
[   37.059086] f10_cma f10-cma:   4 MiB va=ffff00003f000000 dma=0x000000007f000000 pfn 0x7f000-0x7f3ff contig=1 36669 us
[   37.087158] f10_cma f10-coh:   4 MiB va=ffff800081000000 dma=0x0000000070000000 pfn 0x70000-0x703ff contig=1 15908 us
[   37.121040] f10_cma f10-dflt:   4 MiB va=ffff00003bd00000 dma=0x000000007bd00000 pfn 0x7bd00-0x7c0ff contig=1 30056 us
```

출력에서 볼 것:

- `after grab: free 12206 pages (CMA 12192)` — CMA가 아닌 빈 페이지가 **14개**만 남았다. 정말로 다 먹었다.
- 절반(416 MiB)을 돌려준 뒤 buddyinfo의 order 0 열이 106425개로 폭증했다. 빈 메모리는 496 MiB인데 대부분 4 KiB 구멍이다.
- `GFP_KERNEL` order 10은 #0 하나만 성공하고(그 사이 누군가 반납한 블록이 있었다 — 아래 설명) **#1은 222 ms 동안 compaction·reclaim을 시도하다 실패**했다.
- pagetypeinfo의 맨 오른쪽 열(order 10)을 보라. 빈 order-10 블록 12개 중 **11개는 `CMA` 타입**이다. 버디 할당자 입장에서는 4 MiB 블록이 11개나 있는데, `GFP_KERNEL`(unmovable) 요청에는 CMA 블록을 줄 수 없어서 실패한 것이다. 반면 같은 4 MiB를 CMA에서 달라고 한 마지막 세 줄(§6의 `f10_cma` 모듈)은 **세 장치 모두 성공**했다.

> 왜 #0 하나는 성공했나: `after grab` 시점에 CMA가 아닌 빈 페이지는 14개뿐이었는데, 그 뒤 하나 걸러 반납하는 1초 남짓 동안 커널의 다른 부분(메모리 압박으로 깨어난 kswapd·kcompactd 등)이 `pfn 0x45800`부터 4 MiB를 비워 냈다. 어떤 페이지가 비워졌는지는 이 실험에서 추적하지 않았다. 중요한 건 "두 번째부터는 없다"는 점이다.

### 2.7 흔한 함정

- **`/proc/buddyinfo`에 order 10 블록이 보이는데 할당이 실패한다** — CMA 블록일 수 있다. `/proc/pagetypeinfo`로 migrate type을 확인한다. 위 실측처럼 11개가 전부 CMA였다.
- **`GFP_ATOMIC`으로 큰 블록** — compaction을 못 하므로 단편화에 가장 약하다. 인터럽트 컨텍스트에서 큰 버퍼를 할당하는 설계 자체가 잘못이다 (F2).
- **"메모리가 남는데 왜 실패?"를 OOM으로 착각** — 큰 order 실패는 OOM killer를 부르지 않는다(`PAGE_ALLOC_COSTLY_ORDER = 3`을 넘는 요청은 조용히 실패한다). 로그에 `page allocation failure: order:10` 경고만 남는다(`__GFP_NOWARN`이면 그것도 없다).

---

## 3. memblock — 버디 할당자가 생기기 전의 할당자

### 3.1 직관 — 부트로더 시절의 "메모리 지도"

전원이 들어오고 커널이 막 시작한 순간에는 버디 할당자도, `struct page` 배열도 없다. 그런데 커널은 그 `struct page` 배열(1 GiB에 16 MiB)과 초기 페이지 테이블을 **어딘가에 할당해야** 한다. 이 닭과 달걀 문제를 푸는 게 **memblock**이다.

memblock은 아주 단순한 두 개의 표다.

- `memblock.memory`: "여기가 RAM이다" (DT의 `memory` 노드에서 온다)
- `memblock.reserved`: "여기는 이미 쓰는 중이다" (커널 이미지, initrd, DTB, 초기 할당)

할당 요청이 오면 memory에는 있고 reserved에는 없는 구간을 찾아 reserved에 추가한다. 끝이다. Don이 bare-metal에서 링커 스크립트로 `.bss` 끝 이후를 힙으로 쓰던 것, 또는 부트로더가 "이 범위는 펌웨어 로그용" 하고 표에 적어 두던 것과 같은 수준의 도구다.

부팅이 진행되면 `memblock_free_all()`이 reserved에 없는 모든 페이지를 버디 할당자에게 넘긴다. **그 순간 이전에 확보해야 하는 것**이 이 노트의 주제다. 부팅 후에는 4 MiB 이상의 연속 메모리를 보장할 방법이 없으니, 큰 영역은 memblock 단계에서 미리 잡아 두는 것이다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="13" font-weight="bold">부팅 단계 (arm64)</text>
  <text x="350" y="22" font-size="13" font-weight="bold">관련 함수</text>
  <rect x="20" y="35" width="310" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="175" y="62" text-anchor="middle" font-size="13">① DT memory 노드 → memblock.memory</text>
  <text x="350" y="62" font-size="12">early_init_dt_scan_memory()</text>
  <rect x="20" y="95" width="310" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="175" y="122" text-anchor="middle" font-size="13">② 커널·initrd 영역 reserve</text>
  <text x="350" y="122" font-size="12">arm64_memblock_init()</text>
  <rect x="20" y="155" width="310" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="175" y="182" text-anchor="middle" font-size="13">③ /reserved-memory: reg 고정 → size 동적</text>
  <text x="350" y="175" font-size="12">fdt_scan_reserved_mem()</text>
  <text x="350" y="191" font-size="12">memblock_mark_nomap / memblock_phys_alloc_range</text>
  <rect x="20" y="215" width="310" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="175" y="242" text-anchor="middle" font-size="13">④ 기본 CMA 영역 (cma= 또는 Kconfig)</text>
  <text x="350" y="235" font-size="12">dma_contiguous_reserve()</text>
  <text x="350" y="251" font-size="12">→ cma_declare_contiguous()</text>
  <rect x="20" y="275" width="310" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="175" y="302" text-anchor="middle" font-size="13">⑤ 남은 페이지를 buddy에 넘김</text>
  <text x="350" y="302" font-size="12">memblock_free_all()</text>
  <rect x="20" y="335" width="310" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="175" y="362" text-anchor="middle" font-size="13">⑥ CMA 영역을 MIGRATE_CMA로 buddy에 합류</text>
  <text x="350" y="355" font-size="12">cma_init_reserved_areas() (core_initcall)</text>
  <text x="350" y="371" font-size="12">→ init_cma_reserved_pageblock()</text>
  <line x1="175" y1="79" x2="175" y2="93" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="175" y1="139" x2="175" y2="153" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="175" y1="199" x2="175" y2="213" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="175" y1="259" x2="175" y2="273" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="175" y1="319" x2="175" y2="333" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="20" y="405" font-size="12">①~④는 dmesg 타임스탬프 0.000000 구간, ⑤ 이후부터 버디 할당자(alloc_pages)가 동작한다</text>
</svg>
```
그림 3 — 부팅 시 메모리 확보 순서. 빨강(③)은 DT `/reserved-memory`, 파랑(④⑥)은 CMA다. CMA는 ④에서 memblock으로 "자리"를 잡아 두었다가 ⑥에서 버디 할당자에게 "movable만 빌려 쓸 수 있는 블록"으로 넘긴다.

### 3.2 예제 5 — `memblock=debug`로 예약 순간 보기

무엇을 확인하나: `memblock=debug` 커널 인자를 주면 모든 memblock 호출이 로그로 남는다. 다만 부팅 초기 페이지 테이블 할당 로그가 수백 줄이라 커널 로그 버퍼가 넘쳐 앞부분이 잘린다. 그래서 `earlycon`으로 **콘솔에 실시간 출력**하고 호스트 쪽에서 캡처했다. 같은 DT(§4의 `f10_dt.dtb`)를 쓴다.

```sh
printf '#!/bin/sh\ntrue\n' > ~/.elinux_scratch/F10/ex0_true.sh
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/F10/boot.sh /var/tmp/F10/f10_dt.dtb "earlycon=pl011,0x9000000 memblock=debug loglevel=8" ex0_true.sh 2>&1 | tr -d "\r"' > mbdbg_console.txt
wc -l mbdbg_console.txt; sed -n 4,12p mbdbg_console.txt
```

```text
    1395 mbdbg_console.txt
[    0.000000] memblock_add: [0x0000000048000000-0x000000004817dfff] arm64_memblock_init+0x228/0x2c8
[    0.000000] __memblock_reserve: [0x0000000048000000-0x000000004817dfff] nid=-1 flags=0 arm64_memblock_init+0x248/0x2c8
[    0.000000] __memblock_reserve: [0x0000000040200000-0x000000004408ffff] nid=-1 flags=0 arm64_memblock_init+0x260/0x2c8
[    0.000000] __memblock_reserve: [0x0000000071400000-0x00000000714fffff] nid=-1 flags=0 fdt_scan_reserved_mem+0x22c/0x750
[    0.000000] memblock_phys_alloc_range: 16777216 bytes align=0x400000 from=0x0000000040000000 max_addr=0x0000000080000000 early_init_dt_alloc_reserved_memory_arch+0x40/0xa0
[    0.000000] __memblock_reserve: [0x000000007f000000-0x000000007fffffff] nid=-1 flags=20 memblock_alloc_range_nid+0x110/0x1b8
[    0.000000] Reserved memory: created CMA memory pool at 0x000000007f000000, size 16 MiB
[    0.000000] OF: reserved mem: initialized node f10-cma-pool, compatible id shared-dma-pool
[    0.000000] OF: reserved mem: 0x000000007f000000..0x000000007fffffff (16384 KiB) map reusable f10-cma-pool
```

출력에서 볼 것: 순서가 그림 3 그대로다. `0x48000000`(initrd) → `0x40200000`(커널 이미지) reserve → `/reserved-memory`의 고정 `reg` 노드(`f10-log@71400000`) reserve → `size`만 준 동적 노드(`f10-cma-pool`)를 위해 `memblock_phys_alloc_range(16 MiB, align 4 MiB, 0x40000000~0x80000000)` → 결과는 RAM 맨 위 `0x7f000000`(memblock은 기본적으로 위에서부터 찾는다). `flags=20`은 `MEMBLOCK_RSRV_KERN`(memblock 할당 API로 잡은 것)이다.

같은 캡처의 뒷부분, 기본 CMA 예약과 memblock 요약(일부 발췌, 중간 생략):

```text
[    0.000000] __memblock_reserve: [0x000000007bc00000-0x000000007dbfffff] nid=-1 flags=20 memblock_alloc_range_nid+0x110/0x1b8
[    0.000000] cma: Reserved 32 MiB at 0x000000007bc00000
[    0.000000] MEMBLOCK configuration:
[    0.000000]  memory size = 0x0000000040000000 reserved size = 0x00000000083367e3
[    0.000000]  memory.cnt  = 0x5
[    0.000000]  memory[0x0]	[0x0000000040000000-0x000000006fffffff], 0x0000000030000000 bytes on node 0 flags: 0x0
[    0.000000]  memory[0x1]	[0x0000000070000000-0x00000000707fffff], 0x0000000000800000 bytes on node 0 flags: 0x4
[    0.000000]  memory[0x2]	[0x0000000070800000-0x0000000070ffffff], 0x0000000000800000 bytes on node 0 flags: 0x0
[    0.000000]  memory[0x3]	[0x0000000071000000-0x00000000711fffff], 0x0000000000200000 bytes on node 0 flags: 0x4
[    0.000000]  memory[0x4]	[0x0000000071200000-0x000000007fffffff], 0x000000000ee00000 bytes on node 0 flags: 0x0
[    0.000000]  reserved.cnt  = 0xc
[    0.000000]  reserved[0x0]	[0x0000000040200000-0x000000004408ffff], 0x0000000003e90000 bytes on node 0 flags: 0x0
[    0.000000]  reserved[0x1]	[0x0000000048000000-0x000000004817dfff], 0x000000000017e000 bytes on node 0 flags: 0x0
[    0.000000]  reserved[0x2]	[0x0000000048200000-0x0000000048208feb], 0x0000000000008fec bytes on node 0 flags: 0x0
[    0.000000]  reserved[0x3]	[0x0000000071400000-0x00000000714fffff], 0x0000000000100000 bytes on node 0 flags: 0x0
[    0.000000]  reserved[0x4]	[0x000000007bc00000-0x000000007dbfffff], 0x0000000002000000 bytes flags: 0x20
...
```

출력에서 볼 것: `no-map` 영역(`0x70000000` 8 MiB, `0x71000000` 2 MiB)은 reserved가 아니라 **memory 표에서 `flags: 0x4`(`MEMBLOCK_NOMAP`)** 로 표시된다. "RAM이긴 하지만 커널 선형 매핑에서 빼라"는 뜻이다. 반면 그냥 예약(`0x71400000`)과 CMA(`0x7bc00000`)는 reserved 표에 들어간다. `memblock_mark_nomap()`은 디버그 로그를 남기지 않아서 이 요약에서만 보인다.

### 3.3 예제 6 — 부팅 후 memblock 표 (debugfs)

무엇을 확인하나: `CONFIG_ARCH_KEEP_MEMBLOCK=y`면 `/sys/kernel/debug/memblock/{memory,reserved}`로 부팅 시 표를 다시 볼 수 있다. 다음 절의 `/proc/iomem`과 한 번에 본다.

```sh
#!/bin/sh
# ex5_iomem.sh — no-map / map / reusable 영역이 /proc/iomem, memblock에 어떻게 보이나
echo "=== /proc/iomem (RAM part) ==="; grep -E "System RAM|reserved|Kernel" /proc/iomem
echo "=== DT /reserved-memory ==="; ls --color=never /sys/firmware/devicetree/base/reserved-memory/
echo "=== debugfs memblock/memory ==="; cat /sys/kernel/debug/memblock/memory
echo "=== debugfs memblock/reserved ==="; cat /sys/kernel/debug/memblock/reserved
```

출력은 §4.5에서 `/proc/iomem`과 함께 본다.

### 3.4 흔한 함정

- **memblock 표를 "현재 상태"로 오해** — debugfs의 memblock 표는 부팅 때 기록이다. CMA 영역은 ⑥에서 버디로 넘어간 뒤에도 표에 `RSV_KERN`으로 남아 있다. 실제 사용량은 `/proc/meminfo`, `/sys/kernel/debug/cma`로 본다.
- **`memblock=debug` 출력이 앞에서 잘린다** — 로그 버퍼가 부팅 초기에 넘친다(`** N printk messages dropped **`). `earlycon`으로 콘솔에 실시간 출력하거나, 필요한 줄만 노린다.

---

## 4. DT `/reserved-memory` — 세 종류의 예약

### 4.1 직관 — "이 RAM은 커널 너 혼자 쓰지 마"

보드 설계자는 DRAM의 일부를 커널 아닌 누군가에게 주고 싶을 때가 있다. 예를 들어 센서 키트 SoC라면

- Cortex-M 보조 코어(O 모듈)의 펌웨어가 올라갈 2 MiB — **커널이 절대 건드리면 안 된다**. 캐시 속성이 다른 매핑이 생기는 것조차 위험하다.
- 패닉 직전 로그를 재부팅 너머로 넘기는 1 MiB(ramoops) — 커널이 읽고 쓰긴 하지만 **할당자에게 주면 안 된다**.
- 카메라 프레임용 64 MiB — 평소에는 놀리기 아까우니 **빌려 주되, 카메라가 켜지면 돌려받고 싶다**.

이 세 가지가 `/reserved-memory`의 세 종류다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <rect x="20" y="20" width="90" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="65" y="43" text-anchor="middle" font-size="12">커널</text>
  <rect x="110" y="20" width="160" height="36" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="190" y="43" text-anchor="middle" font-size="12">일반 RAM</text>
  <rect x="270" y="20" width="90" height="36" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="315" y="43" text-anchor="middle" font-size="12">no-map 8M</text>
  <rect x="360" y="20" width="70" height="36" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="395" y="43" text-anchor="middle" font-size="12">no-map 2M</text>
  <rect x="430" y="20" width="70" height="36" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5,4"/>
  <text x="465" y="43" text-anchor="middle" font-size="12">map 1M</text>
  <rect x="500" y="20" width="40" height="36" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="540" y="20" width="60" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="570" y="43" text-anchor="middle" font-size="12">CMA 32M</text>
  <rect x="600" y="20" width="60" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="630" y="43" text-anchor="middle" font-size="12">CMA 16M</text>
  <text x="20" y="74" font-size="12">0x40000000</text>
  <text x="270" y="74" text-anchor="middle" font-size="12">0x70000000</text>
  <text x="540" y="74" text-anchor="middle" font-size="12">0x7bc00000</text>
  <text x="660" y="74" text-anchor="end" font-size="12">0x80000000</text>
  <rect x="20" y="100" width="200" height="200" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="120" y="124" text-anchor="middle" font-size="13" font-weight="bold">no-map</text>
  <text x="32" y="152" font-size="12">선형 매핑에서 제외</text>
  <text x="32" y="174" font-size="12">커널·buddy 모두 손 안 댐</text>
  <text x="32" y="196" font-size="12">접근: memremap / ioremap</text>
  <text x="32" y="218" font-size="12">캐시 속성 충돌 없음</text>
  <text x="32" y="250" font-size="12">예: 펌웨어, TEE, DSP</text>
  <text x="32" y="272" font-size="12">랩: f10-fw, f10-coh-pool</text>
  <rect x="240" y="100" width="200" height="200" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5,4"/>
  <text x="340" y="124" text-anchor="middle" font-size="13" font-weight="bold">속성 없음 (map)</text>
  <text x="252" y="152" font-size="12">선형 매핑 유지</text>
  <text x="252" y="174" font-size="12">buddy에는 안 줌 (예약)</text>
  <text x="252" y="196" font-size="12">접근: phys_to_virt</text>
  <text x="252" y="218" font-size="12">놀고 있어도 회수 불가</text>
  <text x="252" y="250" font-size="12">예: ramoops 로그</text>
  <text x="252" y="272" font-size="12">랩: f10-log</text>
  <rect x="460" y="100" width="200" height="200" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="560" y="124" text-anchor="middle" font-size="13" font-weight="bold">reusable (CMA)</text>
  <text x="472" y="152" font-size="12">선형 매핑 유지</text>
  <text x="472" y="174" font-size="12">평소엔 movable이 빌려 씀</text>
  <text x="472" y="196" font-size="12">요청 시 migration 후 반환</text>
  <text x="472" y="218" font-size="12">접근: dma_alloc_coherent</text>
  <text x="472" y="250" font-size="12">예: 카메라, GPU, 코덱</text>
  <text x="472" y="272" font-size="12">랩: f10-cma-pool 등</text>
</svg>
```
그림 4 — 이 노트 랩의 물리 메모리(비율 무시)와 `/reserved-memory`의 세 종류. 빨강은 커널이 아예 못 보는 영역, 회색 점선은 보이지만 할당하지 않는 영역, 파랑은 평소엔 빌려 주는 영역이다.

### 4.2 정의 — 바인딩 속성 정리

`/reserved-memory`는 루트 바로 아래의 특별한 노드다. 바인딩 문서는 `Documentation/devicetree/bindings/reserved-memory/` 아래에 있다(일부는 dt-schema 저장소로 옮겨졌다).

| 속성 | 위치 | 뜻 |
|---|---|---|
| `#address-cells`, `#size-cells`, `ranges` | `/reserved-memory` | 루트와 같은 셀 수(64비트면 2/2), `ranges;`는 비워 둔다 |
| `reg = <주소 크기>` | 자식 노드 | **고정 주소** 예약 (static). 노드 이름에 `@주소`를 붙인다 |
| `size = <크기>` | 자식 노드 | **동적** 예약 — 커널이 memblock에서 자리를 골라 준다 |
| `alignment = <정렬>` | 자식 노드 (`size`와 함께) | 동적 예약의 정렬. CMA면 최소 pageblock(2 MiB)으로 올려진다 |
| `alloc-ranges = <시작 크기 ...>` | 자식 노드 (`size`와 함께) | 동적 예약이 들어가도 되는 범위. 예: 장치가 32비트 주소만 쓰면 4 GiB 아래로 |
| `no-map` | 자식 노드 | 선형 매핑에서 뺀다 (`MEMBLOCK_NOMAP`) |
| `reusable` | 자식 노드 | OS가 빌려 써도 된다 → `shared-dma-pool`과 함께면 CMA |
| `compatible = "shared-dma-pool"` | 자식 노드 | DMA API용 풀. `reusable`이면 CMA, `no-map`이면 per-device 코히어런트 풀 |
| `linux,cma-default` | 자식 노드 (`reusable` 풀) | 이 풀을 **기본 CMA**로 (`cma=`와 충돌, §5.4) |
| `memory-region = <&풀>` | **장치 노드** | 이 장치는 저 풀에서 DMA 메모리를 받는다 |
| `memory-region-names` | 장치 노드 | 풀이 여러 개일 때 이름 (`of_reserved_mem_device_init_by_name`) |

`no-map`과 `reusable`은 **같이 쓰면 안 된다**(§5.5에서 실측). 하나는 "커널은 보지도 마", 다른 하나는 "커널이 빌려 써도 돼"라서 서로 모순이다.

### 4.3 예제 7 — 랩 DT에 네 가지 예약 + 장치 세 개 붙이기

무엇을 확인하나: QEMU `virt`(1 GiB, 2 CPU)의 DT를 덤프하고, 끝에 `/reserved-memory`와 장치 노드를 `#include`한다. 공용 랩의 `out/virt.dts`는 512 MiB로 덤프한 것이라 `memory` 노드가 다르므로 **직접 덤프**한다(README의 주의 사항: `-dtb`를 줄 때는 `-m`·`-cpu`·`-smp`를 덤프할 때와 같게).

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/F10 && cd /var/tmp/F10 && qemu-system-aarch64 -machine virt,dumpdtb=f10_base.dtb -cpu cortex-a57 -m 1024M -smp 2 -nographic; dtc -I dtb -O dts -o f10_base.dts f10_base.dtb 2>/dev/null; grep -n "memory@" -A1 f10_base.dts; sed -n 3,5p f10_base.dts'
```

```text
20:	memory@40000000 {
21-		reg = <0x00 0x40000000 0x00 0x40000000>;
/ {
	interrupt-parent = <0x8003>;
	dma-coherent;
```

출력에서 볼 것: RAM은 `0x40000000`부터 1 GiB(`0x40000000` 바이트)다. 그리고 루트 노드에 **`dma-coherent;`** 가 있다. 최근 QEMU `virt`는 모든 장치를 캐시 코히어런트로 선언한다. 그래서 루트 아래에 붙인 우리 장치도 코히어런트로 취급된다(§6.3에서 이걸 지운 변형도 본다).

DT 조각:

```dts
/* f10_nodes.dtsi — f10_virt.dts 끝에 #include 되는 F10 데모 노드 */
/ {
	reserved-memory {
		#address-cells = <2>;
		#size-cells = <2>;
		ranges;

		f10_pool: f10-cma-pool {		/* 동적 배치 CMA (per-device) */
			compatible = "shared-dma-pool";
			reusable;
			size = <0x0 0x01000000>;		/* 16 MiB */
			alignment = <0x0 0x00400000>;		/* 4 MiB */
			alloc-ranges = <0x0 0x40000000 0x0 0x40000000>;
#ifdef F10_BOTH
			no-map;					/* 실수 실험: reusable + no-map */
#endif
#ifdef F10_DEFAULT
			linux,cma-default;			/* 이 풀을 기본 CMA로 */
#endif
		};

		f10_coh: f10-coh-pool@70000000 {	/* 고정 주소 no-map 코히어런트 풀 */
			compatible = "shared-dma-pool";
			no-map;
			reg = <0x0 0x70000000 0x0 0x00800000>;	/* 8 MiB */
		};

		f10-fw@71000000 {			/* 펌웨어 전용 영역 (드라이버 없음) */
			no-map;
			reg = <0x0 0x71000000 0x0 0x00200000>;	/* 2 MiB */
		};

		f10-log@71400000 {			/* map은 하되 할당 금지 (ramoops류) */
			reg = <0x0 0x71400000 0x0 0x00100000>;	/* 1 MiB */
		};
	};

	f10-cma {
		compatible = "dons,f10-cma";
		memory-region = <&f10_pool>;
	};

	f10-coh {
		compatible = "dons,f10-cma";
		memory-region = <&f10_coh>;
	};

	f10-dflt {
		compatible = "dons,f10-cma";		/* memory-region 없음 → 기본 CMA */
	};
};
```

빌드 스크립트(실패 실험용 변형은 cpp `-D`로, 루트 속성 변경은 `sed`로):

```sh
#!/bin/sh
# mkdtb.sh <이름> [sed 식] [cpp 플래그]  → /var/tmp/F10/<이름>.dtb   (1 GiB, 2 CPU로 덤프한 virt DT 기준)
set -e
W=/var/tmp/F10; N=$1; SED=$2; CPPF=$3
cp $W/f10_base.dts $W/$N.dts
[ -n "$SED" ] && sed -i "$SED" $W/$N.dts
echo '#include "/Users/donh/.elinux_scratch/F10/f10_nodes.dtsi"' >> $W/$N.dts
cpp -nostdinc -undef -x assembler-with-cpp $CPPF $W/$N.dts > $W/$N.pp.dts
dtc -q -I dts -O dtb -o $W/$N.dtb $W/$N.pp.dts
echo "built $W/$N.dtb"
```

```sh
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/F10/mkdtb.sh f10_dt && sh /Users/donh/.elinux_scratch/F10/mkdtb.sh f10_noncoh "/^\tdma-coherent;/d"'
```

```text
built /var/tmp/F10/f10_dt.dtb
built /var/tmp/F10/f10_noncoh.dtb
```

요점: 장치 노드 세 개는 같은 `compatible`이라 같은 드라이버(§6의 `f10_cma`)가 세 번 probe된다. 차이는 `memory-region`뿐이다 — CMA 풀 / no-map 코히어런트 풀 / 없음(기본 CMA). `reg`가 없는 루트 직속 노드도 `compatible`만 있으면 platform device가 된다(D3).

### 4.4 예제 8 — 부팅 로그로 예약 결과 확인

무엇을 확인하나: 커널이 각 노드를 어떻게 해석했는지 `OF: reserved mem:` 로그로 본다. (§6의 예제 12와 같은 부팅에서 나온 앞부분이다.)

```sh
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/F10/boot.sh /var/tmp/F10/f10_dt.dtb "" ex2_dt.sh'
```

```text
[    0.000000] Reserved memory: created CMA memory pool at 0x000000007f000000, size 16 MiB
[    0.000000] OF: reserved mem: initialized node f10-cma-pool, compatible id shared-dma-pool
[    0.000000] OF: reserved mem: 0x000000007f000000..0x000000007fffffff (16384 KiB) map reusable f10-cma-pool
[    0.000000] Reserved memory: created DMA memory pool at 0x0000000070000000, size 8 MiB
[    0.000000] OF: reserved mem: initialized node f10-coh-pool@70000000, compatible id shared-dma-pool
[    0.000000] OF: reserved mem: 0x0000000070000000..0x00000000707fffff (8192 KiB) nomap non-reusable f10-coh-pool@70000000
[    0.000000] OF: reserved mem: 0x0000000071000000..0x00000000711fffff (2048 KiB) nomap non-reusable f10-fw@71000000
[    0.000000] OF: reserved mem: 0x0000000071400000..0x00000000714fffff (1024 KiB) map non-reusable f10-log@71400000
[    0.000000] cma: Reserved 32 MiB at 0x000000007bc00000
```

출력에서 볼 것:

- 같은 `compatible = "shared-dma-pool"`인데 `reusable`이면 **"created CMA memory pool"**(`kernel/dma/contiguous.c`의 `rmem_cma_setup()`), `no-map`이면 **"created DMA memory pool"**(`kernel/dma/coherent.c`의 `rmem_dma_setup()`)이 됐다. 두 함수 모두 `RESERVEDMEM_OF_DECLARE(…, "shared-dma-pool", …)`로 등록돼 있고, 각자 조건에 맞지 않으면 `-EINVAL`을 돌려 다음 후보에게 넘긴다.
- 로그 끝의 `map`/`nomap`, `reusable`/`non-reusable`이 그림 4의 세 종류를 그대로 보여 준다.
- `compatible`이 없는 `f10-fw`, `f10-log`는 "initialized node" 줄이 없다. **예약만 하고 아무 드라이버도 붙지 않는 영역**이다. 펌웨어 영역은 보통 remoteproc 드라이버가 `memory-region`으로 가리킨다(O 모듈).
- 기본 CMA 32 MiB는 DT 처리 뒤에 `0x7bc00000`에 잡혔다(그림 3의 ④). DT 풀이 RAM 맨 위를 먼저 차지했으므로 그 아래로 밀렸다.

### 4.5 예제 9 — `/proc/iomem`과 memblock debugfs에서 본 세 종류

```sh
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/F10/boot.sh /var/tmp/F10/f10_dt.dtb "" ex5_iomem.sh'
```

```text
lab initramfs: 6.18.55-dbg up 3.42s
=== /proc/iomem (RAM part) ===
40000000-6fffffff : System RAM
  40200000-4277ffff : Kernel code
  42780000-42d3ffff : reserved
  42d40000-4408ffff : Kernel data
  48000000-4817dfff : reserved
  48200000-48208fff : reserved
70000000-707fffff : reserved
70800000-70ffffff : System RAM
71000000-711fffff : reserved
71200000-7fffffff : System RAM
  71400000-714fffff : reserved
  7b738000-7ebfffff : reserved
  7ec5a000-7ec5cfff : reserved
  7ec5d000-7edddfff : reserved
  7eddf000-7eddffff : reserved
  7ede0000-7ede2fff : reserved
  7ede3000-7ee06fff : reserved
  7ee07000-7fffffff : reserved
=== DT /reserved-memory ===
#address-cells         f10-coh-pool@70000000  name
#size-cells            f10-fw@71000000        ranges
f10-cma-pool           f10-log@71400000
=== debugfs memblock/memory ===
   0: 0x0000000040000000..0x000000006fffffff    0 NONE
   1: 0x0000000070000000..0x00000000707fffff    0 NOMAP
   2: 0x0000000070800000..0x0000000070ffffff    0 NONE
   3: 0x0000000071000000..0x00000000711fffff    0 NOMAP
   4: 0x0000000071200000..0x000000007fffffff    0 NONE
=== debugfs memblock/reserved ===
   0: 0x0000000040200000..0x000000004277ffff    0 NONE
   1: 0x0000000042d40000..0x000000004408ffff    0 NONE
   2: 0x0000000048200000..0x0000000048208feb    0 NONE
   3: 0x0000000071400000..0x00000000714fffff    0 NONE
   4: 0x000000007b738000..0x000000007ebfffff    0 RSV_KERN
   5: 0x000000007ec5a000..0x000000007ec5cfff    0 RSV_KERN
...
  19: 0x000000007ee067b8..0x000000007fffffff    0 RSV_KERN
```

(memblock reserved의 6~18번 줄은 수십~수백 바이트짜리 초기 할당이라 생략했다.)

출력에서 볼 것:

- **`no-map` 영역은 `System RAM`을 쪼갠다.** `70000000-707fffff : reserved`와 `71000000-711fffff : reserved`가 들여쓰기 없이 최상위에 나오고, 그 사이 RAM이 `System RAM` 세 덩어리로 나뉘었다. 커널 입장에서 이 범위는 "RAM이 아닌 것"처럼 취급된다. 그래서 드라이버는 `phys_to_virt()`가 아니라 `memremap()`으로 접근해야 한다.
- **속성 없는 예약(`f10-log`)은 `System RAM` 아래의 자식** `71400000-714fffff : reserved`로 나온다. RAM이고 선형 매핑도 있지만 할당자에게 주지 않은 것이다.
- **CMA 영역은 따로 표시되지 않는다.** 기본 CMA(`0x7bc00000`~)와 `f10-cma-pool`(`0x7f000000`~)은 `7b738000-7ebfffff`, `7ee07000-7fffffff` 같은 "reserved" 덩어리 안에 섞여 있다. `/proc/iomem`의 reserved 목록이 부팅 때 memblock reserved 표로 만들어지기 때문이다. CMA 실사용은 §5의 `/proc/meminfo`, `/sys/kernel/debug/cma`로 본다.
- DT는 `/sys/firmware/devicetree/base/reserved-memory/`로 그대로 노출된다(D2).

### 4.6 예제 10 — 실수 실험: `reusable` + `no-map`

무엇을 확인하나: 모순되는 두 속성을 같이 쓰면 어떻게 되는지, 그리고 드라이버가 그걸 눈치채는지.

```sh
#!/bin/sh
# ex7_both.sh
head -3 /proc/pagetypeinfo
dmesg | grep -iE "f10-cma-pool|cma:"
insmod /f10_cma.ko sizes_mb=4
dmesg | grep "f10_cma f10-cma"
```

```sh
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/F10/mkdtb.sh f10_both "" -DF10_BOTH && sh /Users/donh/.elinux_scratch/F10/boot.sh /var/tmp/F10/f10_both.dtb "" ex7_both.sh'
```

```text
built /var/tmp/F10/f10_both.dtb
lab initramfs: 6.18.55-dbg up 3.86s
Page block order: 9
Pages per block:  512

[    0.000000] OF: reserved mem: node f10-cma-pool compatible matching fail
[    0.000000] cma: Reserved 32 MiB at 0x000000007bc00000
[    4.258706] f10_cma f10-cma: rmem init=-22 coherent=1
[    4.276555] f10_cma f10-cma:   4 MiB va=ffff00003bd00000 dma=0x000000007bd00000 pfn 0x7bd00-0x7c0ff contig=1 17660 us
```

출력에서 볼 것:

- `compatible matching fail` — CMA 쪽(`rmem_cma_setup`)은 "no-map이면 안 됨", 코히어런트 풀 쪽(`rmem_dma_setup`)은 "reusable이면 안 됨"으로 둘 다 거절했다.
- **더 나쁜 것**: 드라이버의 `of_reserved_mem_device_init()`은 `-22`(`-EINVAL`)을 돌려줬지만, 이후 `dma_alloc_coherent()`는 **조용히 기본 CMA(`0x7bd00000`)에서 성공**했다. 우리 모듈이 반환값을 로그만 찍고 무시했기 때문이다. 실제 드라이버라면 "내 전용 풀에서 받는 줄 알았는데 공용 CMA를 갉아먹는" 버그가 된다. **반환값을 확인하고, 전용 풀이 필수라면 probe를 실패시켜야 한다.**
- `Page block order: 9` — 이 커널의 pageblock은 512페이지 = 2 MiB다. CMA 영역의 최소 정렬 단위다.

---

## 5. CMA — 빌려 주고, 필요하면 돌려받는 메모리

### 5.1 직관 — 주차장의 "예약석"

CMA(Contiguous Memory Allocator)는 백화점 주차장의 "VIP 예약석"과 같다. VIP(카메라 드라이버)가 안 올 때는 일반 차량도 세울 수 있다. 단 조건이 있다: **견인 가능한 차(movable 페이지)만** 세울 수 있다. VIP가 오면 그 구역의 차들을 다른 빈자리로 견인(migration)하고 구역 전체를 비워 준다.

- 견인 가능한 차 = 유저 프로세스의 익명 메모리, 페이지 캐시. 페이지 테이블만 고치면 다른 물리 페이지로 옮길 수 있다.
- 견인 불가능한 차 = 커널 슬랩, 커널 스택, DMA 중인 버퍼. 커널이 물리 주소(선형 매핑 주소)를 직접 들고 있으니 옮길 수 없다. 이들은 **CMA 블록에 아예 못 들어간다**(§2.6에서 `GFP_KERNEL` 모듈이 CMA 페이지를 하나도 못 가져간 이유).

그래서 CMA는 두 마리 토끼를 잡는다. 평소엔 메모리를 놀리지 않고(carveout의 단점 해결), 필요할 땐 단편화와 무관하게 큰 연속 영역을 보장한다(버디의 단점 해결). 대가는 **할당 시간**이다. 빌려 간 페이지가 많으면 그만큼 복사해서 옮겨야 한다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="30" font-size="13">① 부팅 직후</text>
  <rect x="150" y="12" width="320" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="310" y="32" text-anchor="middle" font-size="12">CMA 영역 — 전부 빈 MIGRATE_CMA 블록</text>
  <text x="20" y="100" font-size="13">② 평소</text>
  <rect x="150" y="82" width="40" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="190" y="82" width="40" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="210" y="102" text-anchor="middle" font-size="12">M</text>
  <rect x="230" y="82" width="40" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="250" y="102" text-anchor="middle" font-size="12">M</text>
  <rect x="270" y="82" width="40" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="310" y="82" width="40" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="330" y="102" text-anchor="middle" font-size="12">M</text>
  <rect x="350" y="82" width="40" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="390" y="82" width="40" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="410" y="102" text-anchor="middle" font-size="12">M</text>
  <rect x="430" y="82" width="40" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="490" y="94" font-size="12">유저 메모리·페이지 캐시가</text>
  <text x="490" y="110" font-size="12">빈자리를 빌려 씀</text>
  <text x="20" y="170" font-size="13">③ cma_alloc()</text>
  <rect x="150" y="152" width="160" height="30" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5,4"/>
  <text x="230" y="172" text-anchor="middle" font-size="12">MIGRATE_ISOLATE</text>
  <rect x="310" y="152" width="40" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="330" y="172" text-anchor="middle" font-size="12">M</text>
  <rect x="350" y="152" width="40" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="390" y="152" width="40" height="30" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="410" y="172" text-anchor="middle" font-size="12">M</text>
  <rect x="430" y="152" width="40" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <rect x="520" y="200" width="140" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="590" y="220" text-anchor="middle" font-size="12">CMA 밖 빈 페이지</text>
  <text x="590" y="236" text-anchor="middle" font-size="12">(migration 대상)</text>
  <path d="M 230 184 C 260 225, 420 240, 516 222" fill="none" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="300" y="232" font-size="12">M 페이지 복사 + 페이지 테이블 수정</text>
  <text x="20" y="290" font-size="13">④ 반환</text>
  <rect x="150" y="272" width="160" height="30" fill="#4a7bd0" fill-opacity="0.45" stroke="#4a7bd0" stroke-width="2"/>
  <text x="230" y="292" text-anchor="middle" font-size="12">장치 버퍼 (연속)</text>
  <text x="330" y="292" font-size="12">→ struct page + dma_addr_t를 드라이버에게</text>
</svg>
```
그림 5 — CMA의 생애. ② 평소엔 movable 페이지(M)가 빈자리를 빌려 쓴다. ③ `cma_alloc()`은 요청 구간을 격리(`MIGRATE_ISOLATE`)해 새 할당이 들어오지 못하게 하고, 안에 있던 M 페이지를 CMA 밖 빈 페이지로 옮긴다. ④ 비워진 구간을 통째로 돌려준다.

### 5.2 CMA 영역을 만드는 세 가지 방법

| 방법 | 어디에 | 결과 | 우선순위 |
|---|---|---|---|
| Kconfig | `CONFIG_CMA_SIZE_MBYTES=32` (또는 `_PERCENTAGE`) | 기본 CMA 영역 "reserved" | 가장 낮음 |
| DT | `/reserved-memory`의 `reusable` 풀 + `linux,cma-default` | 기본 CMA 영역 = 그 DT 노드 | Kconfig보다 높음 |
| 커널 명령행 | `cma=nn[MG]@[start[MG][-end[MG]]]` | 기본 CMA 영역 "reserved" | 가장 높음 (DT 기본값을 **무시**) |
| DT (per-device) | `reusable` 풀 + 장치의 `memory-region` | 그 장치 전용 CMA | 기본 CMA와 별개 |

`cma=` 문법은 커널 소스의 `Documentation/admin-guide/kernel-parameters.txt`에 있다(`cma=nn[MG]@[start[MG][-end[MG]]]`). 영역 개수 상한은 `CONFIG_CMA_AREAS`(랩: 20)다.

### 5.3 예제 11 — 기본 CMA 관찰 (DT 없이)

무엇을 확인하나: 아무 DT 수정 없이 부팅했을 때 Kconfig 기본값(32 MiB)으로 잡힌 CMA를 dmesg, `/proc/meminfo`, debugfs에서 본다.

```sh
#!/bin/sh
# ex1_base.sh — 게스트 안: 기본 CMA 영역 관찰
echo "=== uname ==="; uname -r; cat /proc/cmdline
echo "=== dmesg (cma / reserved / memblock) ==="; dmesg | grep -iE "cma|reserved mem|Memory:" 
echo "=== /proc/meminfo ==="; grep -E "MemTotal|MemFree|CmaTotal|CmaFree" /proc/meminfo
echo "=== /sys/kernel/debug/cma ==="; ls --color=never /sys/kernel/debug/cma/
for d in /sys/kernel/debug/cma/*/; do echo "[$d]"; for f in base_pfn count order_per_bit used maxchunk; do printf "  %-14s %s\n" $f "$(cat $d$f)"; done; done
```

```sh
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/F10/boot.sh - "" ex1_base.sh 2>&1 | tr -d "\r"'
```

```text
lab initramfs: 6.18.55-dbg up 3.37s
=== uname ===
6.18.55-dbg
console=ttyAMA0 quiet  LAB_CMD=sh~/ex1_base.sh
=== dmesg (cma / reserved / memblock) ===
[    0.000000] OF: reserved mem: Reserved memory: No reserved-memory node in the DT
[    0.000000] cma: Reserved 32 MiB at 0x000000007cc00000
[    0.138772] Memory: 917948K/1048576K available (22912K kernel code, 7630K rwdata, 15320K rodata, 5888K init, 12023K bss, 91640K reserved, 32768K cma-reserved)
[    1.717891] Freeing initrd memory: 1524K
[    3.459024] Freeing unused kernel memory: 5888K
=== /proc/meminfo ===
MemTotal:         964348 kB
MemFree:          904848 kB
CmaTotal:          32768 kB
CmaFree:           32384 kB
=== /sys/kernel/debug/cma ===
reserved
[/sys/kernel/debug/cma/reserved/]
  base_pfn       510976
  count          8192
  order_per_bit  0
  used           96
  maxchunk       8096
```

출력에서 볼 것:

- `cma: Reserved 32 MiB at 0x7cc00000` — `CONFIG_CMA_SIZE_MBYTES=32` 그대로. DT에 `/reserved-memory`가 없으니 RAM 맨 위 근처에 잡혔다. `Memory:` 줄의 `32768K cma-reserved`도 같은 이야기다.
- `MemTotal`에 CMA가 **포함된다**(964348 kB). CMA는 "놀고 있는 메모리"가 아니라 평소엔 일반 메모리처럼 쓰이기 때문이다. carveout(`no-map`)은 `MemTotal`에서 빠진다.
- 기본 CMA 영역의 이름은 `reserved`다. `base_pfn 510976` = `0x7cc00`, × 4 KiB = `0x7cc00000`. `count 8192`페이지 = 32 MiB.
- `used 96`: 부팅 중에 이미 96페이지(384 KiB)가 CMA에서 나갔다. 같은 랩의 부팅 로그에 `DMA: preallocated 128 KiB GFP_KERNEL pool for atomic allocations`가 GFP_KERNEL, GFP_DMA, GFP_DMA32용으로 세 번 찍히는데(128 KiB × 3 = 384 KiB), `kernel/dma/pool.c`가 이 atomic pool을 `dma_alloc_from_contiguous()`로 CMA에서 받는다. `CmaFree`가 32384 kB로 384 kB 적은 것도 같은 이유다.
- `maxchunk 8096` — 지금 한 번에 줄 수 있는 최대 연속 페이지 수. `count - used`다.

### 5.4 예제 12 — `linux,cma-default`와 `cma=`의 충돌

무엇을 확인하나: DT 풀을 기본 CMA로 지정한 경우와, 거기에 `cma=64M`을 덧붙인 경우.

```sh
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/F10/mkdtb.sh f10_def "" -DF10_DEFAULT && sh /Users/donh/.elinux_scratch/F10/boot.sh /var/tmp/F10/f10_def.dtb "" ex1_base.sh; sh /Users/donh/.elinux_scratch/F10/boot.sh /var/tmp/F10/f10_def.dtb "cma=64M" ex1_base.sh'
```

첫 번째 부팅(`linux,cma-default`만, 일부 발췌):

```text
[    0.000000] Reserved memory: created CMA memory pool at 0x000000007f000000, size 16 MiB
[    0.000000] OF: reserved mem: initialized node f10-cma-pool, compatible id shared-dma-pool
[    0.000000] OF: reserved mem: 0x000000007f000000..0x000000007fffffff (16384 KiB) map reusable f10-cma-pool
...
[    0.240308] Memory: 924064K/1048576K available (22912K kernel code, 7630K rwdata, 15320K rodata, 5888K init, 12023K bss, 101908K reserved, 16384K cma-reserved)
=== /proc/meminfo ===
MemTotal:         954080 kB
MemFree:          895352 kB
CmaTotal:          16384 kB
CmaFree:           16000 kB
=== /sys/kernel/debug/cma ===
f10-cma-pool
[/sys/kernel/debug/cma/f10-cma-pool/]
  base_pfn       520192
  count          4096
  order_per_bit  0
  used           96
  maxchunk       4000
```

두 번째 부팅(`cma=64M` 추가, 일부 발췌):

```text
[    0.000000] OF: reserved mem: Skipping dt linux,cma-default for "cma=" kernel param.
[    0.000000] Reserved memory: created DMA memory pool at 0x0000000070000000, size 8 MiB
...
[    0.000000] cma: Reserved 64 MiB at 0x000000007ac00000
[    0.213097] Memory: 874912K/1048576K available (22912K kernel code, 7630K rwdata, 15320K rodata, 5888K init, 12023K bss, 101908K reserved, 65536K cma-reserved)
=== /proc/meminfo ===
MemTotal:         954080 kB
MemFree:          902048 kB
CmaTotal:          65536 kB
CmaFree:           65152 kB
=== /sys/kernel/debug/cma ===
reserved
[/sys/kernel/debug/cma/reserved/]
  base_pfn       502784
  count          16384
  order_per_bit  0
  used           96
  maxchunk       16288
```

출력에서 볼 것:

- 첫 번째: `cma: Reserved 32 MiB` 줄이 **사라졌다**. DT 풀이 기본 CMA가 됐으므로 Kconfig 기본값은 만들지 않는다. atomic pool의 96페이지도 이제 `f10-cma-pool`에서 나갔다.
- 두 번째: `Skipping dt linux,cma-default for "cma=" kernel param.` — 명령행이 이겼다. 그런데 결과를 보면 **`f10-cma-pool` 노드 자체가 통째로 사라졌다**(debugfs에 `reserved`만 있다). 그 노드를 `memory-region`으로 가리키는 장치가 있었다면 `of_reserved_mem_device_init()`이 실패한다. "부트로더에서 `cma=`를 바꿨더니 카메라가 안 된다"의 전형적인 원인이다.

### 5.5 예제 13 — 셸에서 CMA 직접 할당하기 (`CONFIG_CMA_DEBUGFS`)

무엇을 확인하나: debugfs의 `alloc`/`free` 파일로 드라이버 없이 CMA를 할당·반납한다. 크기 단위는 **페이지**다.

```sh
#!/bin/sh
# ex6_dbgfs.sh — 셸에서 CMA를 직접 할당/반납 (CONFIG_CMA_DEBUGFS)
C=/sys/kernel/debug/cma/reserved
echo "before: used=$(cat $C/used) maxchunk=$(cat $C/maxchunk)"
echo 2048 > $C/alloc        # 2048 페이지 = 8 MiB
echo "after alloc 2048: used=$(cat $C/used) maxchunk=$(cat $C/maxchunk)"
grep -E "CmaFree" /proc/meminfo
echo 2048 > $C/free
echo "after free 2048:  used=$(cat $C/used) maxchunk=$(cat $C/maxchunk)"
grep -E "cma_alloc_(success|fail)" /proc/vmstat
```

```text
lab initramfs: 6.18.55-dbg up 7.08s
before: used=96 maxchunk=8096
after alloc 2048: used=2144 maxchunk=6048
CmaFree:           24192 kB
after free 2048:  used=96 maxchunk=8096
cma_alloc_success 4
cma_alloc_fail 0
```

출력에서 볼 것: 8 MiB를 잡자 `used`가 2048 늘고 `CmaFree`가 8 MiB 줄었다. `/proc/vmstat`의 `cma_alloc_success 4` = 부팅 때 atomic pool 3번 + 방금 1번. 현장에서 "CMA가 충분한가"를 드라이버를 고치지 않고 시험할 때 쓰는 도구다. (`free`는 가장 최근 `alloc`부터 되돌리는 식으로 동작한다 — 디버그 전용.)

### 5.6 흔한 함정

- **`CmaFree`와 debugfs `used`를 같은 것으로 착각** — `used`는 `cma_alloc()`으로 나간 페이지(비트맵), `CmaFree`는 버디 할당자의 CMA 빈 페이지 수다. movable 페이지가 CMA를 빌려 쓰면 `used`는 그대로인데 `CmaFree`만 준다(§7.2에서 실측).
- **CMA를 너무 크게** — CMA는 movable 할당만 받으므로, 시스템 메모리의 대부분을 CMA로 잡으면 커널(unmovable) 할당이 쓸 곳이 부족해져 OOM이 일찍 온다.
- **`cma=`와 DT 기본값 혼용** — §5.4처럼 DT 노드가 통째로 무시된다.

---

## 6. 드라이버에서 쓰기 — `memory-region` + `dma_alloc_coherent()`

### 6.1 예제 14 — `f10_cma` 플랫폼 드라이버

무엇을 확인하나: probe에서 `of_reserved_mem_device_init()`으로 `memory-region`을 장치에 연결하고, `dma_alloc_coherent()`로 4/8/12 MiB를 받아 CPU 주소·DMA 주소·pfn 범위·**페이지별 연속성**·걸린 시간을 찍는다. `hold=1`이면 버퍼를 `remove()`까지 들고 있는다.

앞부분 — 파라미터와 연속성 검사:

```c
// f10_cma.c — memory-region(reserved-memory) + dma_alloc_coherent 데모
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of_reserved_mem.h>
#include <linux/dma-mapping.h>
#include <linux/dma-direct.h>	/* dma_to_phys */
#include <linux/vmalloc.h>
#include <linux/ktime.h>

static int sizes_mb[8] = { 4, 8, 12 };
static int nsizes = 3;
module_param_array(sizes_mb, int, &nsizes, 0444);
static bool hold;		/* 1이면 remove()까지 버퍼를 들고 있는다 */
module_param(hold, bool, 0444);

struct f10_buf { void *va; dma_addr_t dma; size_t sz; };
struct f10 { struct f10_buf b[8]; int n; };

static bool f10_contig(void *va, phys_addr_t pa, size_t sz)
{	/* CPU 쪽 매핑의 페이지마다 pfn을 구해 pa부터 1씩 증가하는지 확인 */
	size_t off;

	for (off = 0; off < sz; off += PAGE_SIZE) {
		unsigned long pfn = is_vmalloc_addr(va) ? vmalloc_to_pfn(va + off)
						      : PHYS_PFN(virt_to_phys(va + off));
		if (pfn != PHYS_PFN(pa) + off / PAGE_SIZE)
			return false;
	}
	return true;
}
```

뒷부분 — probe, remove, 드라이버 등록:

```c
static int f10_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct f10 *f = devm_kzalloc(dev, sizeof(*f), GFP_KERNEL);
	int i, ret;

	if (!f)
		return -ENOMEM;
	ret = of_reserved_mem_device_init(dev);	/* memory-region → dev->cma_area / dma_mem */
	dev_info(dev, "rmem init=%d coherent=%d\n", ret, dev_is_dma_coherent(dev));
	for (i = 0; i < nsizes; i++) {
		struct f10_buf *b = &f->b[f->n];
		size_t sz = (size_t)sizes_mb[i] << 20;
		u64 t = ktime_get_ns();
		phys_addr_t pa;

		b->va = dma_alloc_coherent(dev, sz, &b->dma, GFP_KERNEL);
		t = ktime_get_ns() - t;
		if (!b->va) {
			dev_info(dev, "%3d MiB FAIL (%llu us)\n", sizes_mb[i], t / 1000);
			continue;
		}
		pa = dma_to_phys(dev, b->dma);
		dev_info(dev, "%3d MiB va=%px dma=%pad pfn %#llx-%#llx contig=%d %llu us\n",
			 sizes_mb[i], b->va, &b->dma, (u64)PHYS_PFN(pa),
			 (u64)PHYS_PFN(pa + sz - 1), f10_contig(b->va, pa, sz), t / 1000);
		b->sz = sz;
		if (hold)
			f->n++;
		else
			dma_free_coherent(dev, sz, b->va, b->dma);
	}
	platform_set_drvdata(pdev, f);
	return 0;
}

static void f10_remove(struct platform_device *pdev)
{
	struct f10 *f = platform_get_drvdata(pdev);

	while (f->n--)
		dma_free_coherent(&pdev->dev, f->b[f->n].sz, f->b[f->n].va, f->b[f->n].dma);
	of_reserved_mem_device_release(&pdev->dev);
}

static const struct of_device_id f10_ids[] = { { .compatible = "dons,f10-cma" }, { } };
MODULE_DEVICE_TABLE(of, f10_ids);
static struct platform_driver f10_drv = {
	.probe = f10_probe, .remove = f10_remove,
	.driver = { .name = "f10_cma", .of_match_table = f10_ids },
};
module_platform_driver(f10_drv);
MODULE_DESCRIPTION("F10 reserved-memory + CMA demo");
MODULE_LICENSE("GPL");
```

요점 다섯 가지.

- `of_reserved_mem_device_init(dev)`는 `of_reserved_mem_device_init_by_idx(dev, dev->of_node, 0)`의 줄임이다. 장치 노드의 `memory-region` 0번을 따라가 그 풀의 `device_init` 콜백을 부른다. CMA 풀이면 `dev->cma_area`를, no-map 코히어런트 풀이면 `dev->dma_mem`을 채운다(§9). `memory-region`이 없으면 `-ENODEV`(-19)다.
- `dma_alloc_coherent()`는 **CPU 주소(반환값)와 DMA 주소(`dma_handle`) 두 개**를 준다. 장치 레지스터에는 DMA 주소를 쓰고, CPU는 반환된 포인터로 읽고 쓴다(H9).
- `dma_to_phys()`: IOMMU가 없는 direct 매핑에서는 DMA 주소에서 물리 주소로 바꾼다(이 랩에서는 같은 값이다). `<linux/dma-direct.h>`의 인라인 함수라서 IOMMU가 있는 장치에는 의미가 없다.
- `f10_contig()`: CPU 주소가 vmalloc 영역(재매핑)이면 `vmalloc_to_pfn()`, 선형 매핑이면 `virt_to_phys()`로 **페이지마다** pfn을 구해 연속인지 본다.
- `.remove`가 `void`를 반환하는 형태는 6.11 이후의 platform driver API다(이전엔 `int`).

### 6.2 예제 15 — 세 장치의 probe 결과 (코히어런트 DT)

```sh
#!/bin/sh
# ex2_dt.sh — reserved-memory 부팅 로그 + f10_cma probe 결과
dmesg | grep -iE "reserved mem|cma:"
echo "=== insmod f10_cma.ko ==="
insmod /f10_cma.ko $(cat /proc/cmdline | tr " " "\n" | grep "^f10\." | sed "s/^f10\.//")
dmesg | grep "f10_cma "
echo "=== debugfs cma ==="
for d in /sys/kernel/debug/cma/*/; do printf "%-22s count=%-6s used=%-5s base_pfn=%s\n" $(basename $d) $(cat $d/count) $(cat $d/used) $(cat $d/base_pfn); done
grep -E "CmaTotal|CmaFree" /proc/meminfo
```

```sh
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/F10/boot.sh /var/tmp/F10/f10_dt.dtb "" ex2_dt.sh'
```

부팅 로그 부분은 §4.4에서 봤다. 그 뒤(첫 실행은 `grep f10`으로 걸러서 부팅 로그가 섞였기에, 아래는 `insmod` 이후 줄만 남겼다):

```text
[   19.184180] f10_cma: loading out-of-tree module taints kernel.
[   19.238671] f10_cma f10-cma: assigned reserved memory node f10-cma-pool
[   19.238769] f10_cma f10-cma: rmem init=0 coherent=1
[   19.319965] f10_cma f10-cma:   4 MiB va=ffff00003f000000 dma=0x000000007f000000 pfn 0x7f000-0x7f3ff contig=1 80999 us
[   19.358913] f10_cma f10-cma:   8 MiB va=ffff00003f000000 dma=0x000000007f000000 pfn 0x7f000-0x7f7ff contig=1 26581 us
[   19.409969] f10_cma f10-cma:  12 MiB va=ffff00003f000000 dma=0x000000007f000000 pfn 0x7f000-0x7fbff contig=1 35605 us
[   19.449312] f10_cma f10-coh: assigned reserved memory node f10-coh-pool@70000000
[   19.449363] f10_cma f10-coh: rmem init=0 coherent=1
[   19.460888] f10_cma f10-coh:   4 MiB va=ffff800081000000 dma=0x0000000070000000 pfn 0x70000-0x703ff contig=1 11239 us
[   19.492105] f10_cma f10-coh:   8 MiB va=ffff800081000000 dma=0x0000000070000000 pfn 0x70000-0x707ff contig=1 30730 us
[   19.492260] f10_cma f10-coh:  12 MiB FAIL (35 us)
[   19.498903] f10_cma f10-dflt: rmem init=-19 coherent=1
[   19.555817] f10_cma f10-dflt:   4 MiB va=ffff00003bd00000 dma=0x000000007bd00000 pfn 0x7bd00-0x7c0ff contig=1 56795 us
[   19.614393] f10_cma f10-dflt:   8 MiB va=ffff00003bd00000 dma=0x000000007bd00000 pfn 0x7bd00-0x7c4ff contig=1 42503 us
[   19.687075] f10_cma f10-dflt:  12 MiB va=ffff00003bd00000 dma=0x000000007bd00000 pfn 0x7bd00-0x7c8ff contig=1 51130 us
=== debugfs cma ===
f10-cma-pool           count=4096   used=0     base_pfn=520192
reserved               count=8192   used=96    base_pfn=506880
CmaTotal:          49152 kB
CmaFree:           36480 kB
```

출력에서 볼 것:

- **`f10-cma`**(CMA 풀): DMA 주소가 풀 시작 `0x7f000000`, CPU 주소 `ffff00003f000000`은 **선형 매핑 주소**다(arm64 선형 매핑은 `ffff0000…`에서 시작하고 `0x7f000000 - 0x40000000 = 0x3f000000`). 장치가 코히어런트(`coherent=1`)라서 재매핑 없이 `page_address()`를 그대로 준다. 12 MiB도 성공 — **버디가 원리적으로 못 주는 크기**다. `contig=1`로 3072페이지 전부 연속임을 확인했다.
- **`f10-coh`**(no-map 코히어런트 풀): CPU 주소가 `ffff800081000000` — vmalloc/ioremap 영역이다. no-map이라 선형 매핑이 없으니 `kernel/dma/coherent.c`가 부팅 때 `memremap(…, MEMREMAP_WC)`로 매핑해 둔 주소를 준다. 12 MiB는 **35 µs 만에 FAIL** — 풀이 8 MiB뿐이고, 이 풀은 비트맵으로 2^order 단위만 내주므로(`bitmap_find_free_region`) 12 MiB 요청은 16 MiB 블록을 찾다 바로 실패한다.
- **`f10-dflt`**(memory-region 없음): `rmem init=-19`(`-ENODEV`) — 이상한 게 아니라 "전용 풀 없음"이다. 기본 CMA `0x7bd00000`(기본 영역 `0x7bc00000` + atomic pool 뒤 1 MiB 정렬 자리)에서 받았다.
- 마지막 `CmaFree`가 `CmaTotal`보다 12.4 MiB 적은데 debugfs `used`는 96뿐이다. 버퍼는 다 반납했으니, 그 차이는 **movable 페이지(이 시점의 페이지 캐시·유저 메모리)가 CMA를 빌려 쓰는 중**인 것이다. §5.6의 함정을 실측으로 본 셈이다.
- 시간: 4 MiB에 11~81 ms. 이 대부분은 **0으로 채우기**(`dma_direct_alloc()`과 `__dma_alloc_from_coherent()` 모두 `memset(ret, 0, size)`)와 TCG 에뮬레이션 비용이다.

### 6.3 예제 16 — 비코히어런트 장치 + `hold=1`

무엇을 확인하나: 루트의 `dma-coherent`를 지운 DT(`f10_noncoh.dtb`)로 부팅하면 CPU 주소가 어떻게 바뀌는지, 그리고 버퍼를 들고 있으면(`hold=1`) 풀이 어떻게 차는지.

```sh
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/F10/boot.sh /var/tmp/F10/f10_noncoh.dtb "f10.hold=1" ex2_dt.sh 2>&1 | tr -d "\r" | grep -v "reserved mem\|Reserved memory"'
```

```text
lab initramfs: 6.18.55-dbg up 33.18s
[    0.000000] cma: Reserved 32 MiB at 0x000000007bc00000
=== insmod f10_cma.ko ===
[   37.178051] cma: __cma_alloc: f10-cma-pool: alloc failed, req-size: 3072 pages, ret: -16
[   36.874832] f10_cma f10-cma: rmem init=0 coherent=0
[   36.997248] f10_cma f10-cma:   4 MiB va=ffff8000808c7000 dma=0x000000007f000000 pfn 0x7f000-0x7f3ff contig=1 121883 us
[   37.177674] f10_cma f10-cma:   8 MiB va=ffff800080cc8000 dma=0x000000007f400000 pfn 0x7f400-0x7fbff contig=1 176531 us
[   37.273152] f10_cma f10-cma:  12 MiB FAIL (95327 us)
[   37.306550] f10_cma f10-coh: rmem init=0 coherent=0
[   37.399300] f10_cma f10-coh:   4 MiB va=ffff800081800000 dma=0x0000000070000000 pfn 0x70000-0x703ff contig=1 92395 us
[   37.399487] f10_cma f10-coh:   8 MiB FAIL (76 us)
[   37.399542] f10_cma f10-coh:  12 MiB FAIL (17 us)
[   37.435292] f10_cma f10-dflt: rmem init=-19 coherent=0
[   37.596475] f10_cma f10-dflt:   4 MiB va=ffff800082001000 dma=0x000000007bd00000 pfn 0x7bd00-0x7c0ff contig=1 160476 us
[   37.766236] f10_cma f10-dflt:   8 MiB va=ffff800082402000 dma=0x000000007c100000 pfn 0x7c100-0x7c8ff contig=1 169120 us
[   38.062403] f10_cma f10-dflt:  12 MiB va=ffff800082c03000 dma=0x000000007c900000 pfn 0x7c900-0x7d4ff contig=1 294446 us
=== debugfs cma ===
f10-cma-pool           count=4096   used=3072  base_pfn=520192
reserved               count=8192   used=6240  base_pfn=506880
CmaTotal:          49152 kB
CmaFree:           11904 kB
```

출력에서 볼 것:

- `coherent=0`이 되자 CMA 버퍼의 CPU 주소가 `ffff8000…`(vmalloc 영역)으로 바뀌었다. `dma_direct_alloc()`이 비코히어런트 장치를 위해 **캐시를 끈(non-cacheable) 별도 매핑**을 `dma_common_contiguous_remap()`으로 만든 것이다. 같은 물리 페이지가 선형 매핑(캐시 가능)과 재매핑(캐시 불가) 두 군데에 보이는 상태라, arm64는 그 전에 `arch_dma_prep_coherent()`로 캐시를 비운다. 그래도 `vmalloc_to_pfn()`으로 따라가 보면 `contig=1` — 물리적으로는 여전히 한 덩어리다.
- `hold=1`: `f10-cma` 풀(16 MiB)에서 4 + 8 = 12 MiB를 들고 있으니 남은 4 MiB로는 12 MiB를 못 준다. 커널이 `cma: __cma_alloc: f10-cma-pool: alloc failed, req-size: 3072 pages, ret: -16`(`-EBUSY`)을 남겼다. **전용 풀은 다른 장치와 나누지 않는 대신, 크기를 넘으면 기본 CMA로 넘어가지 않고 그냥 실패한다**(`dma_alloc_contiguous()`가 `dev->cma_area`가 있으면 그것만 본다, §9). 그 뒤 `dma_direct_alloc()`은 버디(`alloc_pages`)로 재시도하지만 12 MiB는 order 10을 넘으니 역시 실패한다.
- `f10-coh`: 4 MiB를 들고 있으니 8 MiB 블록이 없다.
- `f10-dflt`: 4 + 8 + 12 = 24 MiB를 기본 CMA 32 MiB에서 받았다. DMA 주소가 1 MiB 정렬로 이어 붙는다(`cma_alloc_aligned()`가 정렬을 `min(get_order(size), CONFIG_CMA_ALIGNMENT=8)` = 최대 1 MiB로 제한).
- `cma: __cma_alloc …` 줄이 `=== insmod` 바로 뒤, probe 로그보다 먼저 나온 것은 그 메시지가 `pr_err` 수준이라 `quiet`에서도 **콘솔에 즉시** 찍혔고, 나머지는 그 뒤 `dmesg | grep`으로 출력했기 때문이다.

---

## 7. 조각난 시스템에서 CMA는 왜 성공하나 — migration과 그 비용

### 7.1 같은 4 MiB, 다른 결과

§2.6의 마지막 세 줄을 다시 보자. 메모리를 체스판으로 만든 뒤 `GFP_KERNEL` order 10(4 MiB)은 두 번째부터 222 ms 만에 실패했는데, **같은 부팅에서 같은 4 MiB를 `dma_alloc_coherent()`로 요청하자 세 장치 모두 성공**했다.

```text
[   35.134038] f10_frag: KERNEL order 10 #1: FAIL pfn 0x0 222051 us
...
[   37.059086] f10_cma f10-cma:   4 MiB va=ffff00003f000000 dma=0x000000007f000000 pfn 0x7f000-0x7f3ff contig=1 36669 us
[   37.087158] f10_cma f10-coh:   4 MiB va=ffff800081000000 dma=0x0000000070000000 pfn 0x70000-0x703ff contig=1 15908 us
[   37.121040] f10_cma f10-dflt:   4 MiB va=ffff00003bd00000 dma=0x000000007bd00000 pfn 0x7bd00-0x7c0ff contig=1 30056 us
```

이유는 간단하다. CMA 블록에는 unmovable 페이지가 **들어갈 수 없으므로** 체스판이 될 수 없다. 기껏해야 movable 페이지가 빌려 쓰고 있을 뿐이고, 그건 옮기면 된다. no-map 풀(`f10-coh`)은 애초에 아무도 못 쓰니 당연히 비어 있다.

그런데 이 실험에서 CMA는 비어 있었다(`CmaFree` 48768 kB 그대로). 진짜 문제는 **movable 페이지가 CMA를 빌려 쓰고 있을 때**다. 그때는 migration 비용을 낸다.

### 7.2 예제 17 — 유저 메모리로 CMA를 채운 뒤 할당 (`f10_hog`)

무엇을 확인하나: 유저 프로그램이 익명 메모리를 크게 잡으면 그 페이지(movable)가 CMA까지 들어가는지, 그 상태에서 큰 CMA 할당이 migration을 일으키는지, 프로그램은 멀쩡히 살아 있는지.

```c
// f10_hog.c — 익명 메모리 N MiB를 잡고 만져서(movable 페이지) 들고 잠든다
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

int main(int argc, char **argv)
{
	size_t mb = argc > 1 ? strtoul(argv[1], NULL, 0) : 100;
	size_t len = mb << 20;
	char *p = mmap(NULL, len, PROT_READ | PROT_WRITE,
		       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (p == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	memset(p, 0xa5, len);	/* page fault → 페이지마다 실제 할당 */
	printf("f10_hog: %zu MiB touched (pid %d)\n", mb, getpid());
	fflush(stdout);
	pause();		/* 메모리를 쥔 채로 대기 */
	return 0;
}
```

```sh
#!/bin/sh
# ex4_mig.sh — (옵션) hog로 CMA를 movable 페이지로 채운 뒤 큰 CMA 할당 시간 측정
A=$(cat /proc/cmdline | tr " " "\n" | grep "^f10\." | sed "s/^f10\.//")
HOG=$(echo "$A" | grep "^hog=" | cut -d= -f2)
echo "=== boot ==="; grep -E "MemFree|CmaTotal|CmaFree" /proc/meminfo
if [ -n "$HOG" ]; then
  /f10_hog $HOG & sleep 2
  echo "=== after hog $HOG MiB ==="; grep -E "MemFree|CmaFree" /proc/meminfo
  cat /sys/kernel/debug/cma/reserved/used
fi
insmod /f10_cma.ko hold=1 sizes_mb=1,2,4,8,16,32,64
dmesg | grep "f10_cma f10-dflt"
dmesg | grep -E "cma: .*alloc failed" | head -3
echo "=== after CMA allocs ==="; grep -E "MemFree|CmaFree" /proc/meminfo
echo "reserved used=$(cat /sys/kernel/debug/cma/reserved/used)"
[ -n "$HOG" ] && { kill -0 $! && echo "hog alive, VmRSS: $(grep VmRSS /proc/$!/status)"; }
grep -E "compact_|pgmigrate_|cma_alloc" /proc/vmstat
```

`f10_hog`는 VM에서 정적 링크로 빌드해 initramfs에 넣었다(경고 0개, 출력 없음). 기본 CMA를 `cma=256M`으로 키우고 hog 650 MiB를 돌렸다.

```sh
limactl shell elinux -- bash -lc 'gcc -static -O2 -Wall -Wextra /Users/donh/.elinux_scratch/F10/f10_hog.c -o /var/tmp/F10/f10_hog'
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/F10/boot.sh /var/tmp/F10/f10_dt.dtb "cma=256M f10.hog=650" ex4_mig.sh 2>&1 | tr -d "\r" | grep -v "f10-cma-pool"'
```

```text
lab initramfs: 6.18.55-dbg up 12.73s
=== boot ===
MemFree:          907108 kB
CmaTotal:         278528 kB
CmaFree:          278144 kB
f10_hog: 650 MiB touched (pid 84)
=== after hog 650 MiB ===
MemFree:          239460 kB
CmaFree:          120448 kB
96
[   17.197375] f10_cma f10-dflt: rmem init=-19 coherent=1
[   17.200803] f10_cma f10-dflt:   1 MiB va=ffff000020100000 dma=0x0000000060100000 pfn 0x60100-0x601ff contig=1 3322 us
[   17.225375] f10_cma f10-dflt:   2 MiB va=ffff000020200000 dma=0x0000000060200000 pfn 0x60200-0x603ff contig=1 24362 us
[   17.232949] f10_cma f10-dflt:   4 MiB va=ffff000020400000 dma=0x0000000060400000 pfn 0x60400-0x607ff contig=1 7305 us
[   17.250930] f10_cma f10-dflt:   8 MiB va=ffff000020800000 dma=0x0000000060800000 pfn 0x60800-0x60fff contig=1 17785 us
[   17.277706] f10_cma f10-dflt:  16 MiB va=ffff000021000000 dma=0x0000000061000000 pfn 0x61000-0x61fff contig=1 26345 us
[   17.329741] f10_cma f10-dflt:  32 MiB va=ffff000022000000 dma=0x0000000062000000 pfn 0x62000-0x63fff contig=1 51280 us
[   17.590279] f10_cma f10-dflt:  64 MiB va=ffff000024000000 dma=0x0000000064000000 pfn 0x64000-0x67fff contig=1 254958 us
=== after CMA allocs ===
MemFree:          112884 kB
CmaFree:            1664 kB
reserved used=32608
hog alive, VmRSS: VmRSS:	  666216 kB
pgmigrate_success 6656
pgmigrate_fail 0
compact_migrate_scanned 0
compact_free_scanned 0
compact_isolated 44640
compact_stall 0
compact_fail 0
compact_success 0
compact_daemon_wake 0
compact_daemon_migrate_scanned 0
compact_daemon_free_scanned 0
cma_alloc_success 14
cma_alloc_fail 3
```

(`grep -v`로 걸러 낸 줄은 같은 크기 목록을 받은 `f10-cma`·`f10-coh` 장치가 16 MiB 이상에서 낸 `alloc failed` 로그다. `cma_alloc_fail 3`이 그것이다.)

출력에서 볼 것:

- `CmaTotal: 278528 kB` = `cma=256M` + DT의 `f10-cma-pool` 16 MiB.
- hog가 650 MiB를 잡자 `CmaFree`가 278 MB → 120 MB로 줄었다. **유저 페이지 약 158 MB가 CMA 블록을 빌려 쓰고 있다.** 그런데 debugfs `used`는 여전히 96 — 빌려 쓰는 건 `cma_alloc()`이 아니기 때문이다.
- 그 상태에서 1~64 MiB(합 127 MiB)를 전부 받았다. `pgmigrate_success 6656` — hog의 페이지 6656개(26 MiB)가 CMA 밖으로 **옮겨졌다**. `compact_isolated`도 늘었다(`alloc_contig_range()`가 compaction 코드로 페이지를 격리하기 때문). `compact_stall 0`이니 이건 일반 direct compaction이 아니다.
- `hog alive, VmRSS 666216 kB` — 페이지가 이사 갔지만 프로세스는 모른다. 페이지 테이블만 새 물리 페이지를 가리키게 바뀌었다. **이게 "movable"의 의미다.**
- 64 MiB가 255 ms. 같은 크기를 빈 CMA에서 받으면 얼마일까? 다음 측정이다.

### 7.3 예제 18 — CMA 할당 시간: 빈 CMA vs 빌려 쓰는 CMA (측정 차트)

무엇을 확인하나: 같은 스크립트를 hog 없이 3회, hog 720 MiB로 3회 돌려 크기별 시간의 **중앙값**을 비교한다(TCG라 한 번 값은 흔들린다).

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/F10; for c in "cma=256M" "cma=256M f10.hog=720"; do for r in 1 2 3; do echo "### $c run $r"; sh /Users/donh/.elinux_scratch/F10/boot.sh /var/tmp/F10/f10_dt.dtb "$c" ex4_mig.sh 2>&1 | tr -d "\r" | grep -E "f10-dflt: +[0-9]+ MiB|pgmigrate_success|CmaFree|hog alive|FAIL" ; done; done' > timing.txt
```

원자료(µs, 각 줄의 마지막 숫자)를 표로 옮겼다. 줄 전체는 위 예제와 같은 모양이라 생략한다.

| 크기 | 빈 CMA run1 / run2 / run3 | hog run1 / run2 / run3 | 중앙값 (빈 → hog) |
|---|---|---|---|
| 1 MiB | 11165 / 3016 / 2011 | 15337 / 7031 / 11131 | 3.0 → 11.1 ms |
| 2 MiB | 4213 / 3407 / 3584 | 60704 / 26784 / 33829 | 3.6 → 33.8 ms |
| 4 MiB | 5155 / 4697 / 5458 | 62238 / 9604 / 18407 | 5.2 → 18.4 ms |
| 8 MiB | 11833 / 8275 / 8738 | 37464 / 16089 / 28179 | 8.7 → 28.2 ms |
| 16 MiB | 19140 / 14103 / 16686 | 44187 / 17131 / 23067 | 16.7 → 23.1 ms |
| 32 MiB | 43881 / 23344 / 30995 | 64186 / 43796 / 43932 | 31.0 → 43.9 ms |
| 64 MiB | 70067 / 57433 / 57612 | 276231 / 502406 / 263997 | 57.6 → 276.2 ms |
| `pgmigrate_success` | 0 / 0 / 0 | 16384 / 15872 / 16896 | 0 → 약 64 MiB 이주 |
| hog 직후 `CmaFree` | — | 114304 / 83584 / 79488 kB | — |

```svg
<svg viewBox="0 0 660 345" xmlns="http://www.w3.org/2000/svg">
  <line x1="80" y1="290.0" x2="630" y2="290.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="72" y="294.0" text-anchor="end" font-size="12">1</text>
  <line x1="80" y1="206.7" x2="630" y2="206.7" stroke="#888" stroke-opacity="0.3"/>
  <text x="72" y="210.7" text-anchor="end" font-size="12">10</text>
  <line x1="80" y1="123.3" x2="630" y2="123.3" stroke="#888" stroke-opacity="0.3"/>
  <text x="72" y="127.3" text-anchor="end" font-size="12">100</text>
  <line x1="80" y1="40.0" x2="630" y2="40.0" stroke="#888" stroke-opacity="0.3"/>
  <text x="72" y="44.0" text-anchor="end" font-size="12">1000</text>
  <line x1="80" y1="40" x2="80" y2="290" stroke="currentColor"/>
  <line x1="80" y1="290" x2="630" y2="290" stroke="currentColor"/>
  <text x="22" y="165" font-size="12" transform="rotate(-90 22 165)" text-anchor="middle">할당 시간 (ms, log)</text>
  <rect x="93.3" y="250.0" width="24" height="40.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="105.3" y="246.0" text-anchor="middle" font-size="12">3</text>
  <rect x="121.3" y="202.8" width="24" height="87.2" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="133.3" y="198.8" text-anchor="middle" font-size="12">11</text>
  <text x="119.3" y="308" text-anchor="middle" font-size="12">1 MiB</text>
  <rect x="171.9" y="243.8" width="24" height="46.2" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="183.9" y="239.8" text-anchor="middle" font-size="12">4</text>
  <rect x="199.9" y="162.6" width="24" height="127.4" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="211.9" y="158.6" text-anchor="middle" font-size="12">34</text>
  <text x="197.9" y="308" text-anchor="middle" font-size="12">2 MiB</text>
  <rect x="250.4" y="230.6" width="24" height="59.4" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="262.4" y="226.6" text-anchor="middle" font-size="12">5</text>
  <rect x="278.4" y="184.6" width="24" height="105.4" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="290.4" y="180.6" text-anchor="middle" font-size="12">18</text>
  <text x="276.4" y="308" text-anchor="middle" font-size="12">4 MiB</text>
  <rect x="329.0" y="211.5" width="24" height="78.5" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="341.0" y="207.5" text-anchor="middle" font-size="12">9</text>
  <rect x="357.0" y="169.2" width="24" height="120.8" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="369.0" y="165.2" text-anchor="middle" font-size="12">28</text>
  <text x="355.0" y="308" text-anchor="middle" font-size="12">8 MiB</text>
  <rect x="407.6" y="188.1" width="24" height="101.9" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="419.6" y="184.1" text-anchor="middle" font-size="12">17</text>
  <rect x="435.6" y="176.4" width="24" height="113.6" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="447.6" y="172.4" text-anchor="middle" font-size="12">23</text>
  <text x="433.6" y="308" text-anchor="middle" font-size="12">16 MiB</text>
  <rect x="486.1" y="165.7" width="24" height="124.3" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="498.1" y="161.7" text-anchor="middle" font-size="12">31</text>
  <rect x="514.1" y="153.1" width="24" height="136.9" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="526.1" y="149.1" text-anchor="middle" font-size="12">44</text>
  <text x="512.1" y="308" text-anchor="middle" font-size="12">32 MiB</text>
  <rect x="564.7" y="143.3" width="24" height="146.7" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="576.7" y="139.3" text-anchor="middle" font-size="12">58</text>
  <rect x="592.7" y="86.6" width="24" height="203.4" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="604.7" y="82.6" text-anchor="middle" font-size="12">276</text>
  <text x="590.7" y="308" text-anchor="middle" font-size="12">64 MiB</text>
  <text x="355" y="328" text-anchor="middle" font-size="12">dma_alloc_coherent 요청 크기 (기본 CMA 256 MiB, hold=1로 누적)</text>
  <rect x="100" y="12" width="14" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="120" y="22" font-size="12">CMA 비어 있음 (migration 0)</text>
  <rect x="340" y="12" width="14" height="12" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="360" y="22" font-size="12">CMA를 hog가 점유 (migration 발생)</text>
</svg>
```
그림 6 — 기본 CMA 256 MiB에서 1→64 MiB를 차례로 받아 들고 있을 때(`hold=1`) 각 `dma_alloc_coherent()` 시간의 중앙값(3회, 로그 축). 파랑은 CMA가 비어 있는 경우, 주황은 hog 720 MiB의 페이지가 CMA를 빌려 쓰고 있는 경우다. 값은 TCG 에뮬레이션 + 디버그 커널 기준이다.

출력에서 볼 것:

- **빈 CMA는 크기에 거의 비례한다**(3 → 58 ms). 이 시간의 대부분은 0으로 채우기(`memset`)다. 실제 보드라면 64 MiB를 지우는 데 수 ms 수준일 것이다.
- **빌려 쓰는 CMA는 2~5배 느리고, 64 MiB에서 약 5배**(58 → 276 ms, 한 번은 502 ms). 3회 모두 약 16k 페이지(64 MiB)를 이주시켰다. 페이지마다 "격리 → 새 페이지 할당 → 복사 → 페이지 테이블 수정 → TLB 무효화"를 하니 비싸다.
- 2 MiB 막대가 4 MiB보다 높은 건 측정 순서 탓이다. 할당이 CMA 시작부터 차례로 채워지는데, 그 구간에 hog 페이지가 얼마나 있었는지가 매번 다르다. 즉 **CMA 할당 시간은 크기보다 "그 구간에 빌려 간 페이지가 몇 개냐"에 좌우된다**. 실시간성이 필요한 경로(카메라 스트림 시작 직후 등)에서 CMA를 할당하면 안 되는 이유다. 버퍼는 `open()`·`REQBUFS` 때 미리 받아 두고 스트리밍 중에는 재사용한다(I5의 videobuf2가 이렇게 한다).

### 7.4 흔한 함정

- **"CMA는 항상 성공한다"** — 빌려 간 페이지를 못 옮기면 실패한다. 대표 원인: 다른 경로가 그 페이지를 잠깐 고정(pin)하고 있음(예: 진행 중인 `O_DIRECT` I/O), 옮길 곳(CMA 밖 빈 페이지)이 부족함. `cma_alloc()`은 다른 구간으로 재시도하다가 결국 `-EBUSY`/`-ENOMEM`을 낸다. 오래 고정하는 `FOLL_LONGTERM` pin(예: RDMA, VFIO)은 커널이 pin하기 전에 페이지를 CMA 밖으로 옮겨 두도록 되어 있다(`mm/gup.c`의 `check_and_migrate_movable_pages_or_folios()`).
- **atomic 컨텍스트에서 CMA** — `dma_alloc_contiguous()`는 `gfpflags_allow_blocking(gfp)`가 거짓이면 바로 `NULL`이다(§9). migration은 잠들어야 하기 때문이다.

---

## 8. 사용자 공간과 프레임워크에서 다시 보기 — dma-buf heaps, V4L2, DRM

### 8.1 dma-buf heaps — 유저가 CMA 버퍼를 직접 받는 길

예전 Android는 ION이라는 별도 할당기로 유저 공간에 CMA·carveout 버퍼를 나눠 줬다. 메인라인은 이를 **dma-buf heaps**로 대체했다(ION은 5.11에서 staging에서 제거). 유저는 `/dev/dma_heap/<이름>`을 열고 `DMA_HEAP_IOCTL_ALLOC`으로 크기를 주면 **dma-buf fd**를 받는다. 그 fd를 V4L2(`VIDIOC_QBUF`의 `V4L2_MEMORY_DMABUF`)나 DRM(PRIME import)에 넘기면 복사 없이 공유된다.

랩 커널 소스 기준으로 heap 이름은 이렇다(`drivers/dma-buf/heaps/`).

| heap | 소스 | 버퍼 성질 |
|---|---|---|
| `system` | `system_heap.c` (`exp_info.name = "system"`) | 흩어진 페이지 (IOMMU·SG 장치용) |
| `default_cma_region` | `cma_heap.c` (`DEFAULT_CMA_NAME`) | 기본 CMA에서 연속 |
| 레거시 이름 (`reserved` 또는 DT 노드 이름) | `cma_heap.c`, `CONFIG_DMABUF_HEAPS_CMA_LEGACY` | 같은 기본 CMA를 옛 이름으로도 노출 |

> 이 절은 **실행하지 않았다.** 랩 커널은 `# CONFIG_DMABUF_HEAPS is not set`(§1.3)이라 `/dev/dma_heap/`이 없다. 커널을 다시 빌드하는 대신 소스만 확인했다. 예전 자료에서 흔히 보는 `/dev/dma_heap/linux,cma`는 DT 기본 CMA 노드 이름이 `linux,cma`일 때의 레거시 이름이다. 6.18 소스에서는 `default_cma_region`이 새 표준 이름이고 레거시 이름은 옵션으로 추가된다.

### 8.2 V4L2 — videobuf2-dma-contig

카메라 드라이버(I5)는 버퍼를 직접 할당하지 않고 videobuf2에게 맡긴다. 연속 버퍼가 필요한 드라이버는 `vb2_dma_contig_memops`를 고르고, 그 안에서 결국 DMA API를 부른다(랩 소스 `drivers/media/common/videobuf2/videobuf2-dma-contig.c`).

```c
/* videobuf2-dma-contig.c 발췌 (랩 소스 195행 부근) */
	struct vb2_queue *q = buf->vb->vb2_queue;

	buf->cookie = dma_alloc_attrs(buf->dev,
				      buf->size,
				      &buf->dma_addr,
				      GFP_KERNEL | q->gfp_flags,
				      buf->attrs);
```

`dma_alloc_coherent()`는 `dma_alloc_attrs(…, 0)`의 줄임이다. 카메라 버퍼는 CPU가 픽셀을 거의 안 만지므로 `DMA_ATTR_NO_KERNEL_MAPPING`을 줘서 커널 가상 매핑(특히 비코히어런트 장치의 vmalloc 재매핑)을 생략하기도 한다. 장치에 `memory-region`이 있으면 이 경로가 그 풀을, 없으면 기본 CMA를 쓴다 — §6의 `f10_cma`와 똑같다. 앱에서 `VIDIOC_REQBUFS`로 버퍼 4개를 요청하는 순간이 곧 "CMA에서 3 MiB × 4"가 나가는 순간이다.

### 8.3 DRM — GEM DMA helper

디스플레이 컨트롤러(I8) 중 스캔아웃 엔진이 연속 버퍼를 요구하는 드라이버는 `drm_gem_dma_helper`를 쓴다. 랩 소스 `drivers/gpu/drm/drm_gem_dma_helper.c`는 `dma_alloc_wc()`(write-combine 속성의 코히어런트 할당) 또는 `dma_alloc_noncoherent()`로 버퍼를 받는다. 1080p XRGB8888 더블 버퍼 = 약 16 MiB가 부팅 직후 CMA에서 나간다.

---

## 9. 커널 안에서는 무슨 일이 일어나나

### 9.1 `dma_alloc_coherent()`의 길

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="180" y="10" width="320" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="35" text-anchor="middle" font-size="12">dma_alloc_coherent(dev, size, &amp;dma, gfp)</text>
  <rect x="180" y="70" width="320" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="95" text-anchor="middle" font-size="12">dma_alloc_attrs()  kernel/dma/mapping.c</text>
  <rect x="10" y="140" width="210" height="70" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="115" y="160" text-anchor="middle" font-size="12">① 전용 no-map 풀 (dma_mem)</text>
  <text x="115" y="180" text-anchor="middle" font-size="12">dma_alloc_from_dev_coherent</text>
  <text x="115" y="200" text-anchor="middle" font-size="12">비트맵 + memremap 주소</text>
  <rect x="235" y="140" width="210" height="70" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5,4"/>
  <text x="340" y="160" text-anchor="middle" font-size="12">② IOMMU 있음</text>
  <text x="340" y="180" text-anchor="middle" font-size="12">iommu_dma_alloc()</text>
  <text x="340" y="200" text-anchor="middle" font-size="12">흩어진 페이지 + IOVA 연속</text>
  <rect x="460" y="140" width="210" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="565" y="160" text-anchor="middle" font-size="12">③ direct 매핑</text>
  <text x="565" y="180" text-anchor="middle" font-size="12">dma_direct_alloc()</text>
  <text x="565" y="200" text-anchor="middle" font-size="12">kernel/dma/direct.c</text>
  <rect x="460" y="240" width="210" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="565" y="263" text-anchor="middle" font-size="12">dma_alloc_contiguous()</text>
  <text x="565" y="283" text-anchor="middle" font-size="12">kernel/dma/contiguous.c</text>
  <rect x="460" y="330" width="210" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="565" y="350" text-anchor="middle" font-size="12">dev->cma_area 있으면 그것만</text>
  <text x="565" y="370" text-anchor="middle" font-size="12">없으면 기본 CMA</text>
  <text x="565" y="390" text-anchor="middle" font-size="12">(잠들 수 있을 때만)</text>
  <rect x="235" y="330" width="210" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-width="2"/>
  <text x="340" y="350" text-anchor="middle" font-size="12">cma_alloc()  mm/cma.c</text>
  <text x="340" y="370" text-anchor="middle" font-size="12">alloc_contig_range()</text>
  <text x="340" y="390" text-anchor="middle" font-size="12">격리 → migration → 획득</text>
  <rect x="10" y="240" width="210" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="115" y="263" text-anchor="middle" font-size="12">CMA가 NULL이면 buddy</text>
  <text x="115" y="283" text-anchor="middle" font-size="12">alloc_pages_node(order)</text>
  <line x1="340" y1="50" x2="340" y2="68" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="260" y1="110" x2="130" y2="138" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="340" y1="110" x2="340" y2="138" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="420" y1="110" x2="550" y2="138" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="565" y1="210" x2="565" y2="238" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="565" y1="296" x2="565" y2="328" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="460" y1="365" x2="447" y2="365" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="460" y1="268" x2="222" y2="268" stroke="currentColor" stroke-dasharray="5,4" marker-end="url(#ah4)"/>
  <text x="20" y="435" font-size="12">③의 끝: 코히어런트면 page_address(), 아니면 캐시 끈 vmalloc 재매핑 → memset 0</text>
  <text x="20" y="455" font-size="12">랩 결과: f10-coh = ①, f10-cma·f10-dflt = ③ → CMA, 4 MiB 이하 실패 시 buddy</text>
</svg>
```
그림 7 — `dma_alloc_coherent()`가 메모리를 고르는 순서. 먼저 장치 전용 no-map 풀(①), 다음 IOMMU(②), 마지막으로 direct 매핑(③)에서 CMA → buddy 순서다.

랩 소스에서 핵심 줄을 확인했다(6.18.55).

```c
/* kernel/dma/mapping.c — dma_alloc_attrs() 발췌 */
	if (dma_alloc_from_dev_coherent(dev, size, dma_handle, &cpu_addr)) {
	...
		cpu_addr = dma_direct_alloc(dev, size, dma_handle, flag, attrs);
	...
		cpu_addr = iommu_dma_alloc(dev, size, dma_handle, flag, attrs);
```

```c
/* kernel/dma/contiguous.c — dma_alloc_contiguous() 발췌 */
	/* CMA can be used only in the context which permits sleeping */
	if (!gfpflags_allow_blocking(gfp))
		return NULL;
	if (dev->cma_area)
		return cma_alloc_aligned(dev->cma_area, size, gfp);
	if (size <= PAGE_SIZE)
		return NULL;
	...
	return cma_alloc_aligned(dma_contiguous_default_area, size, gfp);
```

```c
/* kernel/dma/direct.c — __dma_direct_alloc_pages() 발췌 */
	page = dma_alloc_contiguous(dev, size, gfp);
	...
	while ((page = alloc_pages_node(node, gfp, get_order(size)))
	       && !dma_coherent_ok(dev, page_to_phys(page), size)) {
```

읽는 법:

- `dev->cma_area`가 있으면 **그것만** 본다. 실패해도 기본 CMA로 넘어가지 않는다(§6.3의 `f10-cma` 12 MiB 실패). 그 뒤 버디로 재시도하지만 큰 크기는 거기서도 실패한다.
- 한 페이지 이하 요청은 기본 CMA를 쓰지 않는다(CMA를 잘게 쪼개지 않으려고).
- `cma_alloc_aligned()`는 정렬을 `min(get_order(size), CONFIG_CMA_ALIGNMENT)`로 잡는다. 랩은 `CONFIG_CMA_ALIGNMENT=8`이라 최대 2^8 페이지 = 1 MiB 정렬이다(§6.3의 1 MiB 단위 DMA 주소).

### 9.2 `cma_alloc()` → `alloc_contig_range()` 안쪽

`mm/cma.c`의 `__cma_alloc()`은 영역의 비트맵에서 요청 크기만큼 비어 있는 비트 구간을 찾고(`used`가 이 비트맵이다), 그 pfn 범위로 `alloc_contig_range(pfn, pfn + count, ACR_FLAGS_CMA, gfp)`를 부른다. `mm/page_alloc.c`의 `alloc_contig_range_noprof()`는

1. `start_isolate_page_range()` — 해당 pageblock들을 `MIGRATE_ISOLATE`로 바꿔 새 할당이 못 들어오게 한다 (그림 5의 ③).
2. `__alloc_contig_migrate_range()` — 범위 안의 사용 중 페이지를 compaction 코드로 격리하고(`compact_isolated` 증가) 다른 곳으로 migration한다(`pgmigrate_success` 증가).
3. 범위가 전부 비면 버디에서 그 페이지들을 떼어 낸다.

실패하면(옮길 수 없는 페이지 발견) `-EBUSY`를 돌려주고, `__cma_alloc()`은 비트맵에서 다음 후보 구간으로 재시도한다. 끝까지 안 되면 §6.3에서 본 `cma: __cma_alloc: …: alloc failed, req-size: N pages, ret: -16`을 남긴다.

### 9.3 부팅 때의 연결 — `RESERVEDMEM_OF_DECLARE`

`/reserved-memory`의 `compatible`을 해석하는 코드는 `RESERVEDMEM_OF_DECLARE(이름, "compatible", 초기화 함수)`로 등록된다. `shared-dma-pool`에는 두 개가 있다.

| 등록 위치 | 조건 | 결과 | 장치 연결 시 채우는 필드 |
|---|---|---|---|
| `kernel/dma/contiguous.c` `rmem_cma_setup()` | `reusable` 있고 `no-map` 없음, 2 MiB 정렬 | `cma_init_reserved_mem()` → CMA 영역 | `dev->cma_area` |
| `kernel/dma/coherent.c` `rmem_dma_setup()` | `no-map` 있고 `reusable` 없음 | per-device 코히어런트 풀 | `dev->dma_mem` |

`of_reserved_mem_device_init()`은 장치의 `memory-region` phandle → `struct reserved_mem` → 그 `ops->device_init`(`rmem_cma_device_init`이면 `dev->cma_area = rmem->priv`)을 부른다. 드라이버가 이걸 부르지 않으면 DT에 `memory-region`이 있어도 **아무 일도 일어나지 않고** 기본 CMA를 쓴다. 일부 버스(예: 플랫폼 장치의 `of_dma_configure()` 경로에서 restricted DMA pool)는 자동으로 처리하지만, CMA·코히어런트 풀은 드라이버가 직접 부르는 것이 관례다.

### 9.4 드라이버를 쓸 때 다시 만나는 곳

- H9(DMA): `dma_alloc_coherent` vs `dma_map_single`, DMA mask(`dma_set_mask_and_coherent`), 캐시 관리 — 이 노트의 ③ 경로 뒷부분.
- I5(V4L2): `vb2_dma_contig_memops`, `VIDIOC_REQBUFS`/`EXPBUF` — §8.2.
- I8(DRM): `drm_gem_dma_helper`, 스캔아웃 버퍼 — §8.3.
- O 모듈(이종 코어): remoteproc가 `memory-region`의 no-map 영역에 펌웨어를 올린다 — §4의 `f10-fw`.
- F11(가상 메모리): migration은 결국 페이지 테이블 수정 + TLB 무효화다.

---

## 10. Embedded 실무에서 다시 보기

### 10.1 센서 키트의 CMA 크기 정하기

카메라 4대(1080p NV12, 버퍼 5개씩) + 디스플레이(1080p XRGB, 더블 버퍼) + H.264 인코더(참조 프레임·비트스트림 버퍼, 대략 프레임 몇 장 분량)를 생각하면

| 사용처 | 계산 | 합 |
|---|---|---|
| 카메라 | 4 × 5 × 2.97 MiB | 59.3 MiB |
| 디스플레이 | 2 × 7.91 MiB | 15.8 MiB |
| 인코더 (가정) | 프레임 4장 분량 | 약 12 MiB |
| 여유 (단편화·정렬 1 MiB) | 약 30% | 약 26 MiB |
| 합계 | | **약 113 MiB → `cma=128M` 또는 DT 128 MiB 풀** |

CMA는 평소 movable 메모리로 쓰이므로 넉넉히 잡아도 손해가 적다. 다만 §7처럼 스트림 시작 시 할당이 느려질 수 있으니, **버퍼는 스트리밍 전에 할당하고 재사용**한다. 장치가 32비트 DMA만 된다면 DT 풀의 `alloc-ranges`로 4 GiB 아래를 강제한다.

### 10.2 "카메라가 가끔 안 켜진다" 디버깅 순서

1. `dmesg | grep -i cma` — `cma: … alloc failed, req-size: N pages, ret: -16`이 있나? 몇 페이지를 요청했나?
2. `cat /sys/kernel/debug/cma/<영역>/{count,used,maxchunk}` — 영역 자체가 꽉 찼나(`used`), 쪼개졌나(`maxchunk` < 요청)?
3. `grep -E "CmaTotal|CmaFree" /proc/meminfo`, `grep -E "pgmigrate|cma_alloc" /proc/vmstat` — migration이 실패(`pgmigrate_fail`)하고 있나?
4. 장치가 의도한 풀을 쓰나? probe 로그의 `assigned reserved memory node …` 확인(§6.2). 안 보이면 `of_reserved_mem_device_init()` 반환값.
5. 빌려 간 페이지의 주인 찾기 — `page_owner` (아래 예제).

### 10.3 예제 19 — `page_owner`로 "누가 이 페이지를 잡았나" 찾기

무엇을 확인하나: `CONFIG_PAGE_OWNER=y` 커널에 `page_owner=on`을 주면 할당된 페이지마다 할당 스택이 기록된다. §2의 체스판 모듈이 잡은 페이지를 찾아본다. (실제 현장에서는 CMA 구간 안의 pfn을 골라 같은 방법으로 주인을 찾는다.)

```sh
#!/bin/sh
# ex8_owner.sh — page_owner: 조각낸 페이지의 "주인"을 스택으로 찾기
insmod /f10_frag.ko deep=1 >/dev/null
cat /sys/kernel/debug/page_owner > /tmp/po.txt
echo "records: $(grep -c '^Page allocated' /tmp/po.txt)"
echo "MEMALLOC records: $(grep -c 'MEMALLOC' /tmp/po.txt)"
grep -m1 -A14 "MEMALLOC" /tmp/po.txt
```

```sh
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/F10/boot.sh /var/tmp/F10/f10_dt.dtb "page_owner=on" ex8_owner.sh'
```

```text
lab initramfs: 6.18.55-dbg up 3.46s
records: 127564
MEMALLOC records: 103866
Page allocated via order 0, mask 0x22800(GFP_NOWAIT|__GFP_MEMALLOC), pid 74, tgid 74 (insmod), ts 3881245392 ns
PFN 0x40000 type Unmovable Block 512 type Unmovable Flags 0x3fffe0000000000(node=0|zone=0|lastcpupid=0x1ffff)
 get_page_from_freelist+0x1080/0x1340
 __alloc_frozen_pages_noprof+0x708/0x1178
 alloc_pages_mpol+0xc8/0x1c0
 alloc_frozen_pages_noprof+0x4c/0xd8
 alloc_pages_noprof+0x18/0x70
 0xffffbb56727df13c
 do_one_initcall+0x84/0x380
 do_init_module+0x5c/0x268
 load_module+0x1a30/0x2080
 __do_sys_init_module+0x1a8/0x268
 __arm64_sys_init_module+0x20/0x40
 invoke_syscall.constprop.0+0x48/0xf0
 el0_svc_common.constprop.0+0x40/0xf0
```

출력에서 볼 것:

- 살아 있는 페이지 기록 127564개 중 103866개가 `GFP_NOWAIT|__GFP_MEMALLOC` — 우리 모듈이 들고 있는 "하나 걸러" 페이지 수(약 213k의 절반)와 맞는다. 할당자가 `insmod` 프로세스(pid 74)이고, `PFN … type Unmovable Block … type Unmovable`로 이 페이지가 unmovable 블록에 있다는 것도 보인다.
- `split_page()`로 쪼갠 페이지라 `order 0`으로 기록됐다.
- 모듈 함수 자리가 심볼이 아니라 `0xffffbb56727df13c`로 나온다. 할당한 코드가 `__init` 함수(`f10_frag_init` → 인라인된 `f10_grab`)라서 **모듈 로드가 끝난 뒤 init 섹션이 해제**됐기 때문이다. 처음엔 `grep f10_frag_init`으로 찾다가 0건이 나와서 GFP 마스크로 찾았다. 현장에서도 "스택에 이름 없는 주소 + `do_init_module`"은 "모듈 init에서 잡은 페이지"라는 단서다.
- `/sys/kernel/debug/page_owner` 덤프는 TCG에서 약 50초가 걸렸다(부팅 3.5 s → 종료 51.8 s). 실제 보드에서도 무겁다 — 문제 재현 시점에 한 번만 뜬다.

### 10.4 실제 보드 DT에서 보는 모양 (예시, 실행 안 함)

벤더 BSP에서 흔히 보는 기본 CMA 노드와, 보조 코어 펌웨어 carveout의 전형적인 형태다. 주소·크기는 예시이며 이 노트에서 컴파일하거나 부팅하지 않았다.

```dts
/ {
	reserved-memory {
		#address-cells = <2>;
		#size-cells = <2>;
		ranges;

		linux,cma {
			compatible = "shared-dma-pool";
			reusable;
			size = <0 0x8000000>;			/* 128 MiB */
			alloc-ranges = <0 0x40000000 0 0x80000000>;	/* 4 GiB 아래 */
			linux,cma-default;
		};

		m4_fw: m4-firmware@80000000 {
			reg = <0 0x80000000 0 0x1000000>;	/* 16 MiB, 보조 코어 전용 */
			no-map;
		};
	};
};
```

이 경우 기본 CMA 영역의 이름은 `linux,cma`가 되고, `CONFIG_DMABUF_HEAPS_CMA_LEGACY`가 켜진 커널이면 `/dev/dma_heap/linux,cma`라는 레거시 heap 이름도 생긴다(§8.1).

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 큰 버퍼를 `kmalloc`/`alloc_pages`로 | 부팅 직후엔 되다가 며칠 뒤 `page allocation failure: order:10` | 단편화 + 4 MiB 상한 (§2) | `dma_alloc_coherent()` + CMA |
| `of_reserved_mem_device_init()` 호출 안 함 / 반환값 무시 | 전용 풀이 있는데 기본 CMA가 줄어듦 | `memory-region`은 드라이버가 연결해야 함 (§4.6, §9.3) | 호출하고, 전용 풀이 필수면 실패 시 probe 실패 |
| `reusable` + `no-map` 같이 씀 | `compatible matching fail`, 풀이 안 생김 | 두 속성이 모순 (§4.6) | 하나만: CMA면 `reusable`, carveout이면 `no-map` |
| `cma=`와 `linux,cma-default` 혼용 | `Skipping dt linux,cma-default`, DT 풀 노드가 통째로 없음 | 명령행이 우선 (§5.4) | 한쪽으로 통일 (부트로더 `bootargs` 확인) |
| CMA 크기 부족 / 전용 풀 크기 부족 | `cma: … alloc failed, ret: -16` | `used`가 꽉 참, 기본 CMA로 넘어가지 않음 (§6.3) | 크기 재계산(§10.1), `maxchunk` 확인 |
| 스트리밍 중에 CMA 할당 | 프레임 드롭, 수백 ms 지연 | migration 비용 (§7.3) | REQBUFS 때 미리 할당하고 재사용 |
| atomic 컨텍스트에서 `dma_alloc_coherent` | 큰 크기는 항상 실패 | CMA는 잠들어야 함 (§9.1) | process context로 옮김 (F2, F7) |
| no-map 영역을 `phys_to_virt()`로 접근 | 커널 oops (translation fault) | 선형 매핑이 없음 (§4.5) | `memremap()` / `devm_memremap()` |
| 고정 `reg`가 커널·initrd와 겹침 | `failed to reserve memory for node` 또는 부팅 실패 | 주소 계획 충돌 | `size`+`alloc-ranges` 동적 배치, 또는 메모리 맵 재설계 |
| CMA 정렬 무시 (2 MiB 미만 정렬의 고정 `reg`) | `Reserved memory: incorrect alignment of CMA region` | pageblock 단위(§2.2) | `reg`를 2 MiB 정렬로 |

---

## 12. 면접에서 이렇게 말한다

**Q.** Why do camera and display drivers need CMA? Isn't the buddy allocator enough?

**A.** DMA 엔진이 IOMMU나 scatter-gather 없이 "시작 주소 + 길이"만 받으면 버퍼가 물리적으로 연속이어야 한다. 1080p NV12가 3 MiB, 4K가 12 MiB인데 버디의 상한은 4 MiB이고, 오래 돈 시스템은 unmovable 페이지가 박혀 4 MiB도 못 준다. 실제로 메모리를 체스판으로 만들어 보니 빈 메모리가 400 MiB인데 order-10은 하나만 나오고 실패했다. CMA는 부팅 때 영역을 잡아 두고 평소엔 movable 페이지만 빌려 주니까 그런 상황에서도 성공했다.

> Many camera and display DMA engines take just a base address and a length, with no IOMMU or scatter-gather, so the buffer has to be physically contiguous. A 1080p NV12 frame is about 3 MiB, 4K is about 12 MiB, and the buddy allocator tops out at order 10, 4 MiB, and after uptime fragmentation by unmovable kernel pages makes even that fail. I reproduced it: with 400 MiB free, only one 4 MiB block was available. CMA reserves a region at boot and lends it only to movable pages, so the same 4 MiB request from CMA succeeded.

**꼬리 질문**: "그럼 CMA는 항상 성공하나요?" → 빌려 간 페이지가 pin돼 있거나 옮길 곳이 없으면 `-EBUSY`로 실패하고, 성공해도 migration 때문에 느리다.

**Q.** What's the difference between `no-map`, a plain reserved region, and `reusable` in `/reserved-memory`?

**A.** `no-map`은 선형 매핑에서 빼서 커널이 아예 못 보게 한다. 보조 코어 펌웨어나 TEE처럼 캐시 속성 충돌도 막아야 하는 곳에 쓰고, 접근은 `memremap`. 속성이 없으면 매핑은 유지하되 할당자에 안 준다 — ramoops 같은 것. `reusable`은 OS가 빌려 써도 된다는 뜻이고 `shared-dma-pool`과 같이 쓰면 CMA가 된다. 둘을 같이 쓰면 모순이라 풀이 안 생긴다.

> `no-map` removes the range from the kernel's linear mapping, so nothing in Linux touches it; that's for coprocessor firmware or secure memory, and you access it with `memremap`. A plain reserved region stays mapped but is never handed to the page allocator, for example a ramoops log. `reusable` means the OS may borrow it; with `compatible = "shared-dma-pool"` it becomes a CMA area. `no-map` and `reusable` together are contradictory, and the pool simply isn't created.

**Q.** Walk me through what happens when a driver calls `dma_alloc_coherent()`.

**A.** `dma_alloc_attrs()`가 먼저 장치 전용 no-map 코히어런트 풀을 보고, IOMMU가 있으면 `iommu_dma_alloc`으로 흩어진 페이지를 IOVA로 이어 붙인다. 없으면 `dma_direct_alloc` → `dma_alloc_contiguous`에서 장치의 `cma_area`, 없으면 기본 CMA에서 `cma_alloc`, 그게 안 되면 버디로 간다. 장치가 코히어런트가 아니면 캐시를 끈 매핑을 따로 만들고, 마지막에 0으로 채운다. CPU 포인터와 `dma_addr_t` 두 개를 돌려준다.

> `dma_alloc_attrs()` first checks for a per-device coherent pool from a `no-map` reserved region. If the device sits behind an IOMMU it goes to `iommu_dma_alloc`, which maps scattered pages into a contiguous IOVA range. Otherwise it's `dma_direct_alloc`, which calls `dma_alloc_contiguous`: the device's own CMA area if it has one, else the default CMA area, then the buddy allocator as a fallback. For non-coherent devices it builds an uncached mapping, then zeroes the buffer and returns both a CPU pointer and a `dma_addr_t`.

**꼬리 질문**: "atomic 컨텍스트에서 부르면?" → CMA는 migration 때문에 잠들어야 하므로 건너뛰고, 큰 크기는 실패한다.

**Q.** How does CMA give memory back to a device if other pages are using it?

**A.** CMA 블록은 `MIGRATE_CMA` 타입이라 movable 할당만 들어온다. `cma_alloc`이 비트맵에서 구간을 고르면 `alloc_contig_range`가 그 pageblock들을 isolate하고, 안에 있는 페이지를 다른 곳으로 migration한다 — 새 페이지에 복사하고 페이지 테이블을 바꾸는 것. 유저 프로세스는 모른다. 실험에서 650 MiB를 잡은 프로세스가 CMA를 158 MB 빌려 쓰고 있었는데, 127 MiB를 할당하자 6656페이지가 옮겨졌고 프로세스는 그대로 살아 있었다.

> CMA pageblocks are typed `MIGRATE_CMA`, so only movable allocations land there. `cma_alloc` picks a range from its bitmap, then `alloc_contig_range` isolates those pageblocks and migrates every page inside: copy to a new page, fix up the page tables. The owning process never notices. In my lab a process holding 650 MiB had about 158 MB sitting in CMA; allocating 127 MiB migrated 6656 pages and the process kept running.

**Q.** Why is CMA allocation sometimes slow, and how do you design around it?

**A.** 시간은 크기보다 그 구간에 빌려 간 페이지 수에 좌우된다. 측정해 보니 빈 CMA에서 64 MiB가 58 ms인데, 유저 메모리가 차 있으면 276 ms였다(에뮬레이터라 절대값보다는 비율을 본다). 그래서 버퍼는 스트리밍 전에, `REQBUFS` 같은 설정 단계에서 할당하고 재사용한다. 실시간 경로나 인터럽트에서는 절대 할당하지 않는다.

> The cost depends less on size than on how many borrowed pages sit in the target range, because each one has to be migrated. In my measurements 64 MiB took 58 ms from an empty CMA area but 276 ms when user memory had spilled into it, and the ratio is what matters since this was an emulator. So you allocate buffers at setup time, `REQBUFS` for V4L2, and recycle them. You never allocate on the streaming path or in interrupt context.

**Q.** What is memblock and why does it matter for CMA?

**A.** 버디 할당자와 `struct page`가 생기기 전, 부팅 초기의 단순한 할당자다. memory 표와 reserved 표 두 개뿐이다. DT `/reserved-memory`와 CMA 영역은 전부 이 단계에서 잡아야 한다. 버디가 시작되고 나면 큰 연속 구간을 보장할 방법이 없기 때문이다. `memblock=debug`나 debugfs의 `memblock/` 디렉터리로 볼 수 있다.

> memblock is the early boot allocator that exists before the buddy allocator and `struct page` are set up. It's just two tables, memory and reserved. Reserved-memory regions and CMA areas must be carved out at this stage, because once memory is handed to the buddy allocator there's no way to guarantee a large contiguous range. You can trace it with `memblock=debug` or inspect it under debugfs `memblock/`.

**Q.** If the SoC has an IOMMU, do you still need CMA?

**A.** IOMMU 뒤의 장치는 흩어진 페이지를 IOVA로 이어 붙이니 보통 필요 없다. 다만 IOMMU 밖에 있는 블록(예: 일부 카메라 수신기나 부트 초기 장치), 성능 때문에 IOMMU를 바이패스한 장치, 그리고 다른 장치와 dma-buf로 공유하는데 상대 장치가 IOMMU가 없는 경우에는 여전히 연속 버퍼가 필요하다. 공유 버퍼는 가장 제약이 큰 장치에 맞춰 할당해야 한다.

> Devices behind an IOMMU usually don't need it, since scattered pages get mapped into a contiguous IOVA range. You still need contiguous memory for blocks outside the IOMMU, for devices that bypass it for performance, and for dma-bufs shared with a device that has no IOMMU. A shared buffer has to satisfy the most constrained device that touches it.

**Q.** A camera fails to start intermittently with `-ENOMEM`. How do you debug it?

**A.** `dmesg`에서 `cma: alloc failed, req-size`로 요청 크기를 보고, debugfs `cma/<영역>/used`, `maxchunk`로 영역이 찼는지 쪼개졌는지 본다. `/proc/vmstat`의 `pgmigrate_fail`, `cma_alloc_fail`도 본다. probe 로그에서 장치가 의도한 풀(`assigned reserved memory node`)을 쓰는지 확인하고, `cma=` 같은 부트 인자가 DT 풀을 무시하게 만들지 않았는지 본다. 빌려 간 페이지를 못 옮기는 거라면 `page_owner`로 주인을 찾는다.

> I'd start with `dmesg` for `cma: ... alloc failed, req-size` to see the request size, then debugfs `cma/<area>/used` and `maxchunk` to tell whether the area is full or fragmented, plus `pgmigrate_fail` and `cma_alloc_fail` in `/proc/vmstat`. I'd confirm from the probe log that the device was attached to its intended pool, and check the bootargs for a `cma=` that overrides the DT pool. If pages can't be migrated, `page_owner` shows who allocated them.

---

## 13. 직접 해보기

1. **개념**: 1 GiB RAM 보드에서 빈 메모리가 600 MiB인데 `alloc_pages(GFP_KERNEL, 10)`이 실패할 수 있는 이유를, `/proc/pagetypeinfo`의 어느 열을 보면 확인되는지까지 포함해 세 문장으로 설명하라.
정답/힌트: 단편화(unmovable 페이지가 정렬 구간마다 박힘) + compaction 불가, 그리고 남은 order-10 블록이 `CMA` 타입일 수 있음 — pagetypeinfo의 order 10 열을 migrate type별로 본다(§2.6).

2. **코드 과제**: `f10_cma.c`의 probe를 고쳐, 전용 풀이 필수인 장치라면 `of_reserved_mem_device_init()`이 실패할 때 `dev_err_probe()`로 probe를 실패시켜라. `F10_BOTH` DT로 부팅해 probe가 실패하는지 확인하라.
정답/힌트: `if (ret) return dev_err_probe(dev, ret, "no reserved pool\n");` — `/sys/bus/platform/devices/f10-cma/driver` 링크가 없어야 한다.

3. **관찰 과제**: `cma=128M@0x50000000`처럼 위치를 고정해 부팅하고, `cma: Reserved` 로그와 debugfs `base_pfn`이 일치하는지 확인하라. 그 범위가 커널 이미지와 겹치게(`@0x40000000`) 주면 무슨 일이 생기나?
정답/힌트: 고정 위치 예약은 겹치면 실패하고 `Failed to reserve` 계열 로그가 남는다. `mm/cma.c`의 `pr_err` 문자열을 먼저 읽고 예측한 뒤 확인한다.

4. **코드 과제**: `f10_cma`에 `DMA_ATTR_NO_KERNEL_MAPPING`으로 `dma_alloc_attrs()`를 부르는 모드를 추가하라. 비코히어런트 DT(`f10_noncoh.dtb`)에서 반환값이 무엇이 되는지, 시간이 줄어드는지 측정하라.
정답/힌트: 반환값은 커널이 쓸 수 있는 가상 주소가 아니라 쿠키다(`dma_direct_alloc_no_mapping()`은 `struct page` 포인터를 돌려준다). CPU로 접근하면 안 된다.

5. **관찰 과제**: hog 실행 전후로 `/proc/pagetypeinfo`의 `CMA` 행과 `/sys/kernel/debug/cma/reserved/used`를 함께 찍어, "buddy에서 본 CMA 사용"과 "cma_alloc 비트맵"의 차이를 숫자로 설명하라.
정답/힌트: hog 후 pagetypeinfo의 CMA 빈 블록과 `CmaFree`는 줄지만 `used`는 96 그대로다(§7.2).

6. **설계 과제**: 카메라 2대(4K NV12, 버퍼 4개씩) + 1080p 디스플레이 더블 버퍼 장비의 DT `/reserved-memory`를 써라. 32비트 DMA 장치가 있다고 가정하고 `alloc-ranges`를 넣어라.
정답/힌트: 2 × 4 × 11.87 + 2 × 7.91 ≈ 110.8 MiB → 여유 포함 약 144~160 MiB, `alloc-ranges = <0 0x40000000 0 0xc0000000>`처럼 4 GiB 아래.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| CMA | Contiguous Memory Allocator | movable 페이지에 빌려 주다가 필요 시 migration해서 돌려받는 연속 메모리 영역 |
| memblock | 부팅 초기 할당자 | buddy 이전에 memory/reserved 두 표로 물리 메모리를 관리 |
| `/reserved-memory` | DT 예약 노드 | OS가 일반 할당에 쓰면 안 되는 RAM 구간을 기술 |
| `no-map` | 예약 속성 | 커널 선형 매핑에서 제외 (`MEMBLOCK_NOMAP`) |
| `reusable` | 예약 속성 | OS가 빌려 써도 됨 → `shared-dma-pool`이면 CMA |
| `shared-dma-pool` | compatible | DMA API용 풀 (reusable → CMA, no-map → 코히어런트 풀) |
| `memory-region` | 장치 속성 | 장치가 쓸 예약 풀을 가리키는 phandle |
| `linux,cma-default` | 예약 속성 | 이 풀을 기본 CMA로 (`cma=`가 있으면 무시) |
| carveout | 전용 예약 영역 | 부팅 때 떼어 내 특정 용도에만 쓰는 메모리 (평소엔 놀림) |
| migrate type | 블록 분류 | Unmovable / Movable / Reclaimable / HighAtomic / CMA / Isolate |
| pageblock | migrate type 단위 | arm64 4K + THP에서 512페이지 = 2 MiB |
| compaction | 압축 | movable 페이지를 옮겨 큰 빈 블록을 만듦 |
| migration | 페이지 이주 | 내용을 새 물리 페이지로 복사하고 페이지 테이블을 고침 |
| `alloc_contig_range` | 커널 함수 | pfn 구간을 격리·이주시켜 통째로 확보 (CMA의 엔진) |
| dma-buf | 버퍼 공유 객체 | 장치·프로세스 간 fd로 버퍼를 복사 없이 공유 |
| dma-buf heap | `/dev/dma_heap/*` | 유저가 system/CMA 버퍼를 dma-buf로 할당받는 인터페이스 (ION 후속) |
| `page_owner` | 디버그 기능 | 페이지별 할당 스택 기록 (`page_owner=on`) |

---

## 15. 요약 & 체크리스트

카메라·디스플레이·코덱의 DMA는 수 MiB의 물리 연속 버퍼를 원하지만, 버디 할당자는 4 MiB가 상한이고 시간이 지나면 unmovable 페이지 때문에 그것도 못 준다(실측: 빈 메모리 400 MiB에서 4 MiB 하나). 그래서 큰 영역은 버디가 생기기 전 memblock 단계에서 잡는다. DT `/reserved-memory`는 `no-map`(커널이 못 봄, 펌웨어·TEE), 속성 없음(매핑만, ramoops), `reusable`(CMA) 세 종류이고, 장치는 `memory-region` + `of_reserved_mem_device_init()`으로 풀에 연결한다. CMA는 평소엔 movable 페이지에 빌려 주다가 `cma_alloc()` → `alloc_contig_range()`가 구간을 격리하고 페이지를 이주시켜 돌려받으므로 단편화와 무관하게 성공하지만, 빌려 간 페이지가 많을수록 느려진다(실측: 64 MiB 58 → 276 ms). 드라이버는 `dma_alloc_coherent()`만 부르고, DMA API가 전용 풀 → IOMMU → 장치 CMA/기본 CMA → buddy 순서로 고른다. 진단은 `/proc/meminfo`(CmaTotal/CmaFree), `/sys/kernel/debug/cma/*`, `/proc/pagetypeinfo`, `/proc/vmstat`(pgmigrate, cma_alloc), memblock debugfs, `page_owner`로 한다.

- [ ] 1080p NV12·4K NV12·1080p XRGB 버퍼 크기를 즉석에서 계산하고, 왜 물리 연속이 필요한지 DMA 엔진 관점에서 설명할 수 있다
- [ ] 단편화로 order-10이 실패하는 이유를 unmovable 페이지·pageblock·compaction으로 설명하고 `/proc/pagetypeinfo`로 확인할 수 있다
- [ ] memblock의 역할과 부팅 순서(DT memory → reserve → reserved-memory → CMA → buddy 이양)를 그릴 수 있다
- [ ] `/reserved-memory`의 `no-map`/속성 없음/`reusable` 차이와 `/proc/iomem`에서의 모양을 설명할 수 있다
- [ ] `reg` 고정 예약과 `size`+`alignment`+`alloc-ranges` 동적 예약을 DT로 쓸 수 있다
- [ ] `memory-region` + `of_reserved_mem_device_init()` + `dma_alloc_coherent()` 드라이버를 짜고 반환값을 올바르게 처리할 수 있다
- [ ] `dma_alloc_coherent()` → `dma_alloc_attrs()` → `dma_direct_alloc()` → `dma_alloc_contiguous()` → `cma_alloc()` 경로를 소스 파일과 함께 말할 수 있다
- [ ] CMA의 movable 대여와 migration, 그리고 할당 시간이 늘어나는 이유를 측정값으로 설명할 수 있다
- [ ] `cma=`, `CONFIG_CMA_SIZE_MBYTES`, `linux,cma-default`의 우선순위와 충돌 증상을 설명할 수 있다
- [ ] `CmaFree`와 debugfs `used`의 차이를 설명하고, CMA 실패를 dmesg·debugfs·vmstat·page_owner로 진단할 수 있다
- [ ] dma-buf heaps(`system`, `default_cma_region`)와 V4L2 vb2-dma-contig, DRM GEM DMA helper가 이 메커니즘 위에 있다는 것을 설명할 수 있다

## 참고 자료

- 커널 문서 — DMA API: https://docs.kernel.org/core-api/dma-api.html , https://docs.kernel.org/core-api/dma-api-howto.html
- 커널 문서 — Boot time memory management (memblock): https://docs.kernel.org/core-api/boot-time-mm.html
- 커널 문서 — CMA debugfs: https://docs.kernel.org/admin-guide/mm/cma_debugfs.html
- 커널 문서 — page_owner: https://docs.kernel.org/mm/page_owner.html
- 커널 파라미터 `cma=`, `memblock=debug`, `page_owner=`: 소스의 `Documentation/admin-guide/kernel-parameters.txt`
- reserved-memory 바인딩: 소스의 `Documentation/devicetree/bindings/reserved-memory/` (일부는 dt-schema 저장소 `dtschema/schemas/reserved-memory/`로 이전)
- LWN, "A deep dive into CMA" (2012): https://lwn.net/Articles/486301/
- 랩 소스(Linux 6.18.55, `/var/lab/src/linux-6.18.55/`): `kernel/dma/mapping.c`(`dma_alloc_attrs`), `kernel/dma/direct.c`(`dma_direct_alloc`), `kernel/dma/contiguous.c`(`dma_alloc_contiguous`, `rmem_cma_setup`, `cma=` 파싱), `kernel/dma/coherent.c`(`rmem_dma_setup`), `kernel/dma/pool.c`(atomic pool), `mm/cma.c`, `mm/cma_debug.c`, `mm/page_alloc.c`(`alloc_contig_range_noprof`), `mm/memblock.c`, `drivers/of/of_reserved_mem.c`, `drivers/dma-buf/heaps/`, `drivers/media/common/videobuf2/videobuf2-dma-contig.c`, `drivers/gpu/drm/drm_gem_dma_helper.c` — 같은 파일을 https://elixir.bootlin.com/linux/ 에서 버전을 골라 볼 수 있다
- Bootlin, "Linux kernel and driver development" 교육 슬라이드 (DMA·메모리 관리 장): https://bootlin.com/training/kernel/
