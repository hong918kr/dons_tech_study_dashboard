# F9. 커널 메모리 관리 — 물리/가상, 페이지, zone, buddy, slab/SLUB, kmalloc vs vmalloc vs alloc_pages, GFP

> **이 노트를 다 읽으면**: arm64 커널 가상 주소 지도와 `virt ↔ phys ↔ pfn ↔ struct page` 변환을 실제 주소로 설명할 수 있다 · buddy 할당자의 order·분할·병합·단편화를 `/proc/buddyinfo`로 읽을 수 있다 · `kmalloc`/`kmem_cache`/`vmalloc`/`alloc_pages` 중 무엇을 쓸지와 어떤 GFP 플래그를 줄지 근거를 대며 고를 수 있다 · `slab_debug`·`page_owner`로 메모리 버그와 누수를 잡을 수 있다
> **왜 필요한가**: "kmalloc vs vmalloc", "IRQ 핸들러에서 어떤 GFP?"는 커널 드라이버 면접의 단골이고, 실무에서는 "메모리가 남는데 할당이 실패한다(단편화)", "DMA 버퍼가 깨진다(vmalloc 주소를 DMA에 넘김)" 같은 장애로 돌아온다
> **Don 기준 난이도**: MMU·페이지 테이블·DMA·고정 크기 버퍼 풀은 이미 안다 / 새로 배울 것은 Linux가 그것들을 부르는 이름(zone, buddy, slab, GFP)과 "할당자가 실패하기 전에 무엇을 시도하는가(reclaim, compaction)"
> **선행 노트**: A5(유저 관점 가상 메모리), E3(커널 모듈), E4(커널 제약 — §6에서 할당 비용과 `KMALLOC_MAX_SIZE`를 이미 측정했다), F2(실행 컨텍스트)

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어에서 메모리는 보통 이렇게 나뉘어 있었다: 링커 스크립트로 고정된 영역, 부팅 때 한 번 잘라 두는 **고정 크기 버퍼 풀**(NAND 페이지 버퍼, 명령 슬롯), 그리고 가끔 쓰는 큰 연속 영역(DMA 디스크립터 링). `malloc`은 거의 쓰지 않았다. 런타임에 아무 크기나 잡고 풀면 언젠가 조각나서 큰 버퍼를 못 잡게 되는 걸 알기 때문이다.

Linux 커널도 똑같은 문제를 푼다. 다만 규모가 크고(수 GB, 수천 개 드라이버), 메모리 주인이 계속 바뀌고(프로세스, 페이지 캐시, 드라이버), 실패해도 시스템이 살아 있어야 한다. 그래서 층을 나눴다.

- **맨 아래 — 페이지 프레임**: 물리 RAM을 4 KiB 페이지로 자르고, 페이지마다 관리 구조체 `struct page`를 하나씩 둔다. 페이지 번호가 **pfn**(page frame number)이다.
- **buddy 할당자**: 페이지를 2의 거듭제곱 묶음(order 0 = 1장, order 10 = 1024장 = 4 MiB)으로 주고받는다. 이게 커널의 "진짜" 물리 메모리 할당자다. API는 `alloc_pages()`.
- **slab 할당자(SLUB)**: buddy에서 페이지를 받아 같은 크기 객체로 잘라 쓴다. 펌웨어의 고정 크기 버퍼 풀 그 자체다. `kmem_cache_alloc()`, 그리고 크기별 공용 풀을 쓰는 `kmalloc()`.
- **vmalloc**: buddy에서 페이지를 **낱장으로** 받아 커널 가상 주소 공간의 vmalloc 영역에 **이어 붙여 매핑**한다. 가상으로는 연속, 물리로는 흩어져 있다.
- **GFP 플래그**: 모든 할당에 "이걸 얻으려고 무엇까지 해도 되나(잠들기, 디스크 I/O, 비상 예비분 사용)"를 붙인다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">드라이버가 부르는 API → 할당자 층 → 물리 페이지</text>
  <rect x="10" y="36" width="125" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="72" y="58" text-anchor="middle" font-size="12">kmalloc / kzalloc</text>
  <rect x="145" y="36" width="130" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="58" text-anchor="middle" font-size="12">kmem_cache_alloc</text>
  <rect x="285" y="36" width="130" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="350" y="58" text-anchor="middle" font-size="12">vmalloc / kvmalloc</text>
  <rect x="425" y="36" width="100" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="475" y="58" text-anchor="middle" font-size="12">alloc_pages</text>
  <rect x="535" y="36" width="140" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="605" y="58" text-anchor="middle" font-size="12">dma_alloc_coherent</text>
  <rect x="10" y="110" width="265" height="50" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="142" y="131" text-anchor="middle" font-size="12">SLUB: kmalloc-8 … 8k + 전용 캐시</text>
  <text x="142" y="149" text-anchor="middle" font-size="12">객체 단위(바이트), 물리 연속</text>
  <rect x="285" y="110" width="130" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="350" y="131" text-anchor="middle" font-size="12">페이지 낱장 +</text>
  <text x="350" y="149" text-anchor="middle" font-size="12">vmalloc 영역 매핑</text>
  <rect x="535" y="110" width="140" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="605" y="131" text-anchor="middle" font-size="12">CMA 또는 buddy</text>
  <text x="605" y="149" text-anchor="middle" font-size="12">+ 캐시 속성 (H8)</text>
  <line x1="72" y1="70" x2="72" y2="108" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="210" y1="70" x2="210" y2="108" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="350" y1="70" x2="350" y2="108" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="605" y1="70" x2="605" y2="108" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="142" y1="160" x2="142" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="350" y1="160" x2="350" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="475" y1="70" x2="475" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="605" y1="160" x2="605" y2="198" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="150" y="186" font-size="12">slab용 페이지 (order 0~3)</text>
  <text x="358" y="186" font-size="12">order-0 × N</text>
  <text x="483" y="186" font-size="12">order N</text>
  <rect x="10" y="200" width="665" height="50" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="342" y="221" text-anchor="middle" font-size="12">buddy 할당자 — zone별 free_area[order 0..10], 블록 = 2^order 페이지</text>
  <text x="342" y="239" text-anchor="middle" font-size="12">(per-CPU 페이지 목록 pcp가 앞단 캐시 · 부족하면 reclaim / compaction / OOM)</text>
  <line x1="342" y1="250" x2="342" y2="276" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="10" y="278" width="665" height="50" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="342" y="299" text-anchor="middle" font-size="12">물리 RAM 512 MiB = 131072 페이지 (이 노트의 QEMU 게스트)</text>
  <text x="342" y="317" text-anchor="middle" font-size="12">페이지마다 struct page 1개 (64 B) → vmemmap 배열</text>
</svg>
```

그림 1 — 커널 메모리 할당자의 층. 모든 길은 결국 buddy 할당자의 페이지로 끝난다. `kmalloc`은 slab을 거치고(8 KiB 초과면 buddy 직행), `vmalloc`은 낱장 페이지를 가상 주소로 이어 붙이고, `alloc_pages`는 buddy를 직접 부른다. DMA 버퍼는 이 노트 범위 밖(F10, H8)이다.

이 노트는 그림 1을 아래에서 위로 올라간다: 물리 메모리 구성(§1) → 커널 가상 주소 지도(§2) → 주소 변환(§3) → buddy(§4, §5) → slab(§6) → API 비교(§7) → GFP와 메모리 압박(§8) → 디버깅(§9).

### 0.1 실습 환경

모든 커널 코드는 공용 랩의 **디버그 커널**(`/var/lab/out/Image-dbg`, Linux 6.18.55-dbg arm64, `PAGE_OWNER`·`DEBUG_VM`·`SLUB_DEBUG` 포함)을 QEMU `virt`(`-cpu cortex-a57 -smp 2 -m 512M`, TCG)로 부팅해서 돌렸다. 모듈은 `/var/lab/out/linux-build-dbg`로 빌드한다(vermagic `6.18.55-dbg`, 일반 `Image`와 섞지 말 것). VM 호스트 커널은 `uname -r` → `7.0.0-38-generic`이고, 여기에는 랩 모듈을 넣지 않는다.

```sh
grep -E "^(# )?CONFIG_(ARM64_4K_PAGES|ARM64_VA_BITS|PGTABLE_LEVELS|ARCH_FORCE_MAX_ORDER|ZONE_DMA|ZONE_DMA32|SPARSEMEM_VMEMMAP|SLUB|SLUB_DEBUG|SLUB_DEBUG_ON|SLAB_MERGE_DEFAULT|PAGE_OWNER|DEBUG_VM|DEBUG_VIRTUAL|COMPACTION|CMA|RANDOMIZE_BASE|PTDUMP_DEBUGFS|KASAN|KFENCE|DEBUG_KMEMLEAK)[= ]" \
    /var/lab/out/linux-build-dbg/.config
```

```text
CONFIG_PGTABLE_LEVELS=5
CONFIG_ARM64_4K_PAGES=y
CONFIG_ARM64_VA_BITS=52
CONFIG_ARCH_FORCE_MAX_ORDER=10
CONFIG_RANDOMIZE_BASE=y
CONFIG_SLUB=y
CONFIG_SLAB_MERGE_DEFAULT=y
CONFIG_SPARSEMEM_VMEMMAP=y
CONFIG_COMPACTION=y
CONFIG_CMA=y
CONFIG_ZONE_DMA=y
CONFIG_ZONE_DMA32=y
CONFIG_SLUB_DEBUG=y
# CONFIG_SLUB_DEBUG_ON is not set
CONFIG_PAGE_OWNER=y
# CONFIG_PTDUMP_DEBUGFS is not set
# CONFIG_DEBUG_KMEMLEAK is not set
CONFIG_DEBUG_VM=y
# CONFIG_DEBUG_VIRTUAL is not set
# CONFIG_KASAN is not set
# CONFIG_KFENCE is not set
```

출력에서 볼 것: 4 KiB 페이지, `MAX_PAGE_ORDER` = 10(최대 블록 4 MiB), `SLUB_DEBUG`는 **컴파일만** 되어 있고 켜려면 부팅 인자 `slab_debug=`가 필요하다(§9). `PTDUMP_DEBUGFS`가 꺼져 있어서 `/sys/kernel/debug/kernel_page_tables`는 없다 — 그래서 §2에서는 주소 지도를 모듈이 직접 출력한다.

### 0.2 예제 0 — 게스트 실행기와 빌드 스크립트

무엇을 확인하나: `.ko`와 테스트 스크립트를 initramfs 복사본에 넣고 `LAB_CMD=sh~/t.sh`로 실행 후 자동 종료하는 실행기(E4 §1.2와 같은 방식, 디버그 커널·가상 디스크 옵션 추가). 이 노트의 모든 게스트 출력은 이걸로 얻었다.

```sh
#!/bin/bash
# run_guest.sh <tag> <test.sh> [extra kernel args] ; env FILES, SMP, TMO, DISK
set -e
tag=$1; t=$2; extra=$3
W=/var/tmp/F9/run_$tag
rm -rf $W && mkdir -p $W/r
cp -a /var/lab/build/rootfs/. $W/r/
for f in $FILES; do cp $f $W/r/; done
cp $t $W/r/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
D=""
[ -n "$DISK" ] && D="-drive file=$DISK,if=virtio,format=raw"
timeout ${TMO:-180} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp ${SMP:-2} -m 512M -nographic -no-reboot $D \
  -kernel /var/lab/out/Image-dbg -initrd $W/i.gz -append "console=ttyAMA0 quiet $extra LAB_CMD=sh~/t.sh" 2>&1 | tr -d '\r'
```

```sh
#!/bin/bash
# build.sh <name> -> /var/tmp/F9/<name>/<name>.ko  (디버그 커널용)
set -e
n=$1
mkdir -p /var/tmp/F9/$n && cp /Users/donh/.elinux_scratch/F9/$n/$n.c /var/tmp/F9/$n/
cd /var/tmp/F9/$n && echo "obj-m += $n.o" > Kbuild
make -s -C /var/lab/out/linux-build-dbg M=$PWD modules -j2 2>&1
ls -l $n.ko | awk '{print $5, $9}'
```

`quiet` 부팅이라 `pr_info`는 콘솔에 안 나오므로, 각 테스트 스크립트는 `dmesg | grep`으로 로그를 꺼낸다. 이 노트의 모든 모듈은 `-Wall -Wextra`(Kbuild 기본 경고 집합) 경고 0개로 빌드됐다. 시간 값(`[ 12.34]`)과 주소는 부팅마다 다르다 — KASLR이 켜져 있어서 커널 이미지와 모듈 주소는 특히 매번 바뀐다.

---

## 1. 커널은 물리 메모리를 어떻게 나눠 보나 — node, zone, page

### 1.1 직관

펌웨어의 메모리 맵은 보통 "SRAM 0x2000_0000 ~, DDR 0x8000_0000 ~" 같은 표 하나다. Linux는 같은 물리 메모리를 세 단계로 나눠 본다.

- **node** (`pg_data_t`): NUMA 노드. 멀티 소켓 서버에서 "CPU에 가까운 메모리 묶음". 임베디드 SoC는 거의 항상 node 0 하나뿐이다.
- **zone** (`struct zone`): 한 노드 안에서 "**어떤 장치가 이 주소에 닿을 수 있나**"로 나눈 구간. 오래된 DMA 엔진이 32비트(또는 30비트) 주소만 낼 수 있으면, 그 장치용 버퍼는 낮은 주소에서 잡아야 한다. 그래서 낮은 구간을 `ZONE_DMA`/`ZONE_DMA32`로 따로 관리한다. 나머지가 `ZONE_NORMAL`, 메모리 핫플러그·CMA처럼 "옮길 수 있는 페이지만" 두는 구간이 `ZONE_MOVABLE`이다.
- **page** (`struct page`): 4 KiB 페이지 하나. 모든 물리 페이지에 하나씩 있다.

zone은 **할당 정책의 단위**다. 각 zone은 자기만의 free 목록(buddy), 워터마크(min/low/high), 통계를 가진다.

### 1.2 예제 1 — 부팅 로그와 /proc으로 zone 보기

무엇을 확인하나: 512 MiB QEMU 게스트에서 zone이 어떻게 잡혔는지, 페이지가 몇 장이고 워터마크가 얼마인지.

```sh
# 게스트 안 (t.sh)
uname -r
dmesg | grep -iE "memory layout|Zone ranges|  DMA|  Normal|Movable zone|Early memory|  node|Memory:|zonelists|Kernel command" | head -30
cat /proc/buddyinfo
cat /proc/meminfo | head -8
grep -E "^Node|pages free|^ +(min|low|high|managed|present|spanned)" /proc/zoneinfo
```

```text
lab initramfs: 6.18.55-dbg up 3.49s
6.18.55-dbg
[    0.000000] OF: reserved mem: Reserved memory: No reserved-memory node in the DT
[    0.000000] Zone ranges:
[    0.000000]   DMA      [mem 0x0000000040000000-0x000000005fffffff]
[    0.000000]   DMA32    empty
[    0.000000]   Normal   empty
[    0.000000] Movable zone start for each node
[    0.000000] Early memory node ranges
[    0.000000]   node   0: [mem 0x0000000040000000-0x000000005fffffff]
[    0.000000] Kernel command line: console=ttyAMA0 quiet  LAB_CMD=sh~/t.sh
[    0.000000] Unknown kernel command line parameters "LAB_CMD=sh~/t.sh", will be passed to user space.
[    0.000000] Built 1 zonelists, mobility grouping on.  Total pages: 131072
[    0.000000] Policy zone: DMA
[    0.137518] Memory: 406260K/524288K available (22912K kernel code, 7630K rwdata, 15320K rodata, 5888K init, 12023K bss, 80800K reserved, 32768K cma-reserved)
[    1.650140] Freeing initrd memory: 1184K
[    3.468878] Freeing unused kernel memory: 5888K
Node 0, zone      DMA      2      1      3      2      3      1      3      3      2      2     96 
MemTotal:         450560 kB
MemFree:          402084 kB
MemAvailable:     398860 kB
Buffers:               0 kB
Cached:             2200 kB
SwapCached:            0 kB
Active:             1868 kB
Inactive:            500 kB
Node 0, zone      DMA
  pages free     100512
        min      665
        low      831
        high     997
        spanned  131072
        present  131072
        managed  112640
              high:     4161
              high_min: 415
              high_max: 6929
              high:     4823
              high_min: 415
              high_max: 6929
Node 0, zone    DMA32
  pages free     0
        min      0
        low      0
        high     0
        spanned  0
        present  0
        managed  0
Node 0, zone   Normal
  pages free     0
        min      0
        low      0
        high     0
        spanned  0
        present  0
        managed  0
Node 0, zone  Movable
  pages free     0
        min      32
        low      32
        high     32
        spanned  0
        present  0
        managed  0
```

출력에서 볼 것:

- **RAM 전부가 `ZONE_DMA`다.** QEMU `virt`의 RAM은 물리 0x4000_0000부터 512 MiB라 전부 4 GiB 아래다. arm64는 `ZONE_DMA`의 상한을 부팅 때 정한다 — DT의 `dma-ranges`/ACPI IORT에서 "가장 제한이 심한 장치가 닿는 주소"를 찾고, 없으면 32비트(4 GiB)로 둔다. 그래서 여기서는 `ZONE_DMA`가 4 GiB까지 덮고 `DMA32`·`Normal`은 비었다. Raspberry Pi 4처럼 30비트 DMA 장치가 있는 보드라면 `ZONE_DMA`는 처음 1 GiB, `DMA32`는 4 GiB까지, 그 위가 `Normal`이 된다. "arm64에서 `ZONE_DMA`는 고정 16 MiB"(x86의 ISA DMA)라고 외우면 틀린다.
- `Total pages: 131072` = 512 MiB / 4 KiB. `spanned`/`present`가 131072, **`managed`는 112640**이다. 차이 18432 페이지(72 MiB)는 커널 이미지, `memblock`이 부팅 때 잡은 예약분(페이지 테이블, `struct page` 배열 자체 등)이라 buddy가 관리하지 않는다. `Memory:` 줄의 `80800K reserved`가 그 내역이고, `32768K cma-reserved`는 CMA 영역(F10)이다 — CMA는 managed에 포함되지만 일반 커널 할당은 못 쓴다(§4.4에서 실제로 부딪친다).
- 워터마크 `min 665 / low 831 / high 997`(페이지). free가 low 아래로 떨어지면 kswapd가 깨어나 회수하고, min 아래는 특별한 할당만 들어갈 수 있다(§8).
- `high: 4161 / high_min / high_max` 두 쌍은 CPU별 **pcp**(per-CPU pageset — 작은 order 페이지를 CPU마다 캐시해 두는 목록)의 상한이다(`-smp 2`라 두 개).
- `Virtual kernel memory layout` 줄은 **없다.** arm64는 이 출력을 오래전에 없앴다(6.18 소스에서 이 문자열은 `arch/riscv/mm/init.c`에만 남아 있다). §2에서 직접 찍는다.

### 1.3 함정

- `MemTotal`(450560 kB = managed 112640 × 4)은 RAM 크기(512 MiB)가 아니다. 예약분이 빠진 값이다. 보드 bring-up 때 "RAM이 덜 잡혔다"고 착각하기 쉬운 숫자다.
- `ZONE_DMA`/`GFP_DMA`는 "DMA에 쓸 메모리"라는 뜻이 아니다. "주소 제한이 있는 장치용 낮은 메모리"라는 뜻이다. 드라이버의 DMA 버퍼는 GFP 플래그가 아니라 DMA API(`dma_alloc_coherent`, `dma_map_single`)로 만든다(H8).

---

## 2. arm64 커널 가상 주소 지도

### 2.1 직관 — TTBR0과 TTBR1, 두 개의 주소 공간

A5에서 본 유저 프로세스 주소 공간은 `0x0000_xxxx_xxxx_xxxx`, 즉 아래쪽이었다. arm64 MMU는 가상 주소의 **bit 55**를 보고 번역 테이블 베이스를 고른다: 0이면 `TTBR0_EL1`(프로세스마다 다름 — context switch 때 바뀜), 1이면 `TTBR1_EL1`(커널 전용 `swapper_pg_dir`, 모든 프로세스가 공유). 그래서 커널 주소는 전부 `0xffff...`로 시작한다. Cortex-R의 MPU 영역 설정을 "유저용 표 하나 + 커널용 표 하나"로 나누고, 커널 표는 절대 안 바꾸는 구조라고 생각하면 된다.

커널 쪽 절반은 용도별 구역으로 나뉜다.

| 구역 | 무엇이 사나 | 주소 → 물리 관계 |
|---|---|---|
| linear map | 물리 RAM 전체를 1:1로 매핑. `kmalloc`, `alloc_pages`+`page_address`, 페이지 캐시 | **고정 오프셋** — 더하기 빼기로 변환 (`__pa`/`__va`) |
| modules | KASLR이 꺼져 있을 때 모듈 코드 | 페이지 테이블 (vmalloc과 같은 방식) |
| vmalloc | `vmalloc`, `ioremap`(MMIO), 커널 스택(`VMAP_STACK`), KASLR 시 커널 이미지와 모듈 | 페이지 테이블. 페이지마다 물리 위치가 제각각 |
| vmemmap | 모든 물리 페이지의 `struct page` 배열 | `pfn` → 배열 인덱스 |
| PCI I/O | PCI I/O 포트 공간 창 (16 MiB) | 고정 매핑 |
| fixmap | 컴파일 때 슬롯이 정해진 특수 매핑(early console, 임시 매핑) | 슬롯별 고정 |

### 2.2 문서와 소스로 확인

`Documentation/arch/arm64/memory.rst`는 6.18에서 더 이상 주소 표를 싣지 않고 원칙만 설명한다(실제로 랩 소스에서 읽었다). 핵심 문장 몇 개:

```text
TTBRx selection is given by bit 55 of the virtual address. The
swapper_pg_dir contains only kernel (global) mappings while the user pgd
contains only user (non-global) mappings.
...
In order to optimise phys_to_virt and virt_to_phys, the PAGE_OFFSET
is kept constant at 0xFFF0000000000000 (corresponding to 52-bit),
this obviates the need for an extra variable read. The physvirt
offset and vmemmap offsets are computed at early boot to enable
this logic.
```

실제 경계는 `arch/arm64/include/asm/memory.h`(발췌, 6.18.55):

```c
#define PAGE_OFFSET		(_PAGE_OFFSET(VA_BITS))      /* -(1 << 52) */
#define MODULES_VADDR		(_PAGE_END(VA_BITS_MIN))     /* VA_BITS_MIN = 48 */
#define MODULES_VSIZE		(SZ_2G)
#define KIMAGE_VADDR		(MODULES_END)
#define VMEMMAP_END		(-UL(SZ_1G))
#define PCI_IO_START		(VMEMMAP_END + SZ_8M)
#define FIXADDR_TOP		(-UL(SZ_8M))
#define _PAGE_END(va)		(-(UL(1) << ((va) - 1)))
```

이 랩 커널은 `VA_BITS=52`로 빌드됐지만, 52비트 커널 VA를 4 KiB 페이지로 쓰려면 CPU에 FEAT_LPA2가 있어야 한다. Cortex-A57에는 없으므로 부팅 초기에 **48비트로 내려간다**(`vabits_actual`). 하나의 바이너리가 둘 다 지원하려고 `PAGE_OFFSET`은 52비트 값으로 고정하고, 대신 `PHYS_OFFSET`(= `memstart_addr`)을 보정한다 — 아래 출력의 이상한 `PHYS_OFFSET` 값이 그 흔적이다.

### 2.3 예제 2 — 모듈이 직접 그리는 주소 지도 (`f9_layout`)

무엇을 확인하나: 각 구역의 시작·끝 매크로 값, 그리고 실제 `kmalloc`·`vmalloc`·스택·모듈 코드·커널 변수·`struct page`가 어느 구역에 떨어지는지.

```c
// SPDX-License-Identifier: GPL-2.0
/* f9_layout: arm64 커널 가상 주소 지도를 매크로에서 직접 출력 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/jiffies.h>
#include <asm/fixmap.h>
#include <asm/memory.h>

#define R(name, s, e) pr_info("%-8s %016lx - %016lx (%8lu MiB)\n", name, \
			      (unsigned long)(s), (unsigned long)(e),         \
			      ((unsigned long)(e) - (unsigned long)(s)) >> 20)

static int f9_static;			/* 모듈 .bss */

static int __init f9_layout_init(void)
{
	void *k = kmalloc(64, GFP_KERNEL);
	void *v = vmalloc(PAGE_SIZE);
	int on_stack;

	pr_info("VA_BITS=%d vabits_actual=%llu PAGE_SIZE=%lu PHYS_OFFSET=%llx\n",
		VA_BITS, (u64)vabits_actual, PAGE_SIZE, (u64)PHYS_OFFSET);
	R("linear", PAGE_OFFSET, PAGE_END);
	R("modules", MODULES_VADDR, MODULES_END);
	R("vmalloc", VMALLOC_START, VMALLOC_END);
	R("vmemmap", VMEMMAP_START, VMEMMAP_END);
	R("pci-io", PCI_IO_START, PCI_IO_END);
	R("fixmap", FIXADDR_START, FIXADDR_TOP);
	pr_info("kmalloc   %px  linear? %d\n", k, virt_addr_valid(k));
	pr_info("vmalloc   %px  vmalloc? %d\n", v, is_vmalloc_addr(v));
	pr_info("stack     %px  vmalloc? %d\n", &on_stack, is_vmalloc_addr(&on_stack));
	pr_info("mod .text %px  .bss %px\n", f9_layout_init, &f9_static);
	pr_info("jiffies   %px  (kernel image, kimage_voffset=%llx)\n",
		&jiffies, (u64)kimage_voffset);
	pr_info("page(k)   %px  pfn 0x%lx (struct page in vmemmap)\n",
		virt_to_page(k), page_to_pfn(virt_to_page(k)));
	kfree(k);
	vfree(v);
	return 0;
}

static void __exit f9_layout_exit(void) { }
module_init(f9_layout_init);
module_exit(f9_layout_exit);
MODULE_DESCRIPTION("F9: arm64 kernel VA layout");
MODULE_LICENSE("GPL");
```

```sh
# Lima VM: 빌드 후 두 번 부팅 — 기본(KASLR 켜짐)과 nokaslr
/Users/donh/.elinux_scratch/F9/build.sh f9_layout
FILES=/var/tmp/F9/f9_layout/f9_layout.ko ./run_guest.sh t1  t1.sh
FILES=/var/tmp/F9/f9_layout/f9_layout.ko ./run_guest.sh t1b t1.sh nokaslr
# t1.sh:  insmod /f9_layout.ko ; dmesg | grep f9_layout:
```

```text
[    5.826515] f9_layout: VA_BITS=52 vabits_actual=48 PAGE_SIZE=4096 PHYS_OFFSET=fff1000040000000
[    5.826621] f9_layout: linear   fff0000000000000 - ffff800000000000 (4160749568 MiB)
[    5.826724] f9_layout: modules  ffff800000000000 - ffff800080000000 (    2048 MiB)
[    5.826803] f9_layout: vmalloc  ffff800080000000 - fffffdffbf800000 (132117496 MiB)
[    5.826823] f9_layout: vmemmap  ffffc1ffc0000000 - ffffffffc0000000 (65011712 MiB)
[    5.826841] f9_layout: pci-io   ffffffffc0800000 - ffffffffc1800000 (      16 MiB)
[    5.826861] f9_layout: fixmap   ffffffffff3f2000 - ffffffffff800000 (       4 MiB)
[    5.826901] f9_layout: kmalloc   ffff000005c40380  linear? 1
[    5.826998] f9_layout: vmalloc   ffff80008008d000  vmalloc? 1
[    5.827016] f9_layout: stack     ffff800080893aa4  vmalloc? 1
[    5.827030] f9_layout: mod .text ffffb6c38d02f080  .bss ffffb6c38d02b020
[    5.827045] f9_layout: jiffies   ffffb6c39b34cac0  (kernel image, kimage_voffset=ffffb6c358600000)
[    5.827072] f9_layout: page(k)   fffffdffc0171000  pfn 0x45c40 (struct page in vmemmap)
```

`nokaslr`로 부팅한 두 번째 실행에서 달라진 줄만:

```text
[    6.822035] f9_layout: kmalloc   ffff0000053f17c0  linear? 1
[    6.822092] f9_layout: vmalloc   ffff800083f1d000  vmalloc? 1
[    6.822110] f9_layout: stack     ffff800084733aa4  vmalloc? 1
[    6.822124] f9_layout: mod .text ffff80007be96080  .bss ffff80007be92020
[    6.822139] f9_layout: jiffies   ffff800082b4cac0  (kernel image, kimage_voffset=ffff80003fe00000)
[    6.822160] f9_layout: page(k)   fffffdffc014fc40  pfn 0x453f1 (struct page in vmemmap)
```

출력에서 볼 것:

- **linear map**: 매크로상 시작은 52비트 기준 `fff0000000000000`이지만, 48비트로 내려간 이 CPU에서 실제로 쓰는 부분은 `ffff000000000000`부터다. `kmalloc` 주소 `ffff000005c40380`이 그 안에 있고, 물리 주소는 `0x40000000 + 0x05c40380` = `0x45c40380`이다 — DRAM 시작(0x4000_0000)이 linear map 시작(48비트)에 붙는다. `PHYS_OFFSET=fff1000040000000`은 이걸 52비트 `PAGE_OFFSET`과 맞추려고 보정한 값이다: `__pa(va) = va - PAGE_OFFSET + PHYS_OFFSET`을 64비트로 계산하면 위쪽 비트가 넘쳐서 사라지고 `0x45c40380`이 남는다.
- **vmalloc**: `vmalloc` 버퍼와 **커널 스택**(`VMAP_STACK`, E4 §4)이 `ffff8000_8xxx_xxxx`, 즉 vmalloc 구역 맨 앞에 있다.
- **KASLR**: 기본 부팅에서는 모듈 코드(`ffffb6c3...`)와 커널 이미지(`jiffies`)가 **vmalloc 구역 한가운데 무작위 위치**에 있다. `modules` 구역(`ffff8000_0000_0000 ~ ffff8000_8000_0000`)은 비어 있다. `nokaslr`이면 모듈이 `ffff80007be96080`(modules 구역 끝 부분)으로, 커널 이미지가 `KIMAGE_VADDR` = `ffff800080000000` 바로 뒤(`jiffies` = `ffff800082b4cac0`)로 돌아온다. 모듈은 가능하면 커널 코드에서 128 MiB 안(arm64 `BL` 직접 분기 범위)에, 안 되면 2 GiB 안(PLT 경유 분기)에 놓인다 — 그래서 커널 이미지 근처에 붙는다.
- **vmemmap**: `struct page`가 `fffffdffc0171000`에 있다. pfn 0x40000(DRAM 첫 페이지)의 `struct page`를 `fffffdffc0000000`이라 하면 `0x5c40 × 64 = 0x171000`으로 정확히 맞는다 — **`struct page`는 64바이트**다. 두 실행의 차이로도 확인된다: pfn 차 0x84f(2127) × 64 = 0x213c0 = `0x171000 - 0x14fc40`. 4 KiB 페이지당 64 B니 RAM의 1.56%가 `struct page`에 쓰인다(512 MiB → 8 MiB).
- 구간 크기 숫자가 비현실적으로 큰 것(linear 4160749568 MiB)은 52비트 매크로 기준이라서다. 실제로 쓰는 linear는 48비트의 절반인 128 TiB이고, 그중 RAM이 있는 512 MiB만 매핑된다.

```svg
<svg viewBox="0 0 680 500" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">arm64 커널 가상 주소 지도 (48비트 실사용, 축척 무시) — 예제 2 실측</text>
  <text x="180" y="44" text-anchor="end" font-size="12">ffffffffffffffff</text>
  <rect x="190" y="34" width="290" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="335" y="54" text-anchor="middle" font-size="12">fixmap (4 MiB)</text>
  <text x="180" y="68" text-anchor="end" font-size="12">ffffffffff3f2000</text>
  <rect x="190" y="72" width="290" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="335" y="92" text-anchor="middle" font-size="12">PCI I/O (16 MiB)</text>
  <text x="180" y="106" text-anchor="end" font-size="12">ffffffffc0800000</text>
  <rect x="190" y="110" width="290" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="335" y="128" text-anchor="middle" font-size="12">vmemmap — struct page 배열</text>
  <text x="335" y="146" text-anchor="middle" font-size="12">(pfn 0x40000 → fffffdffc0000000)</text>
  <text x="180" y="158" text-anchor="end" font-size="12">fffffdffc0000000</text>
  <rect x="190" y="162" width="290" height="110" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="335" y="186" text-anchor="middle" font-size="12">vmalloc 영역</text>
  <text x="335" y="206" text-anchor="middle" font-size="12">vmalloc · ioremap · 커널 스택</text>
  <text x="335" y="226" text-anchor="middle" font-size="12">KASLR이면 커널 이미지와 모듈도 여기</text>
  <text x="335" y="246" text-anchor="middle" font-size="12">(nokaslr: 커널 이미지가 맨 앞에서 시작)</text>
  <text x="180" y="276" text-anchor="end" font-size="12">ffff800080000000</text>
  <rect x="190" y="280" width="290" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="335" y="300" text-anchor="middle" font-size="12">modules 2 GiB (nokaslr일 때)</text>
  <text x="180" y="314" text-anchor="end" font-size="12">ffff800000000000</text>
  <rect x="190" y="318" width="290" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="335" y="340" text-anchor="middle" font-size="12">linear map — 물리 RAM 1:1</text>
  <text x="335" y="360" text-anchor="middle" font-size="12">__va(0x40000000) = ffff000000000000</text>
  <text x="180" y="378" text-anchor="end" font-size="12">ffff000000000000</text>
  <rect x="190" y="382" width="290" height="26" fill="none" stroke="#888" stroke-dasharray="4 3"/>
  <text x="335" y="400" text-anchor="middle" font-size="12">52비트 CPU에서만 쓰는 앞부분 (fff0…)</text>
  <line x1="190" y1="424" x2="480" y2="424" stroke="#d0564a" stroke-dasharray="6 3"/>
  <text x="335" y="418" text-anchor="middle" font-size="12">bit 55: 1 = TTBR1 (커널) / 0 = TTBR0 (유저)</text>
  <rect x="190" y="430" width="290" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="335" y="450" text-anchor="middle" font-size="12">유저 공간 (프로세스마다 다름, A5)</text>
  <text x="180" y="464" text-anchor="end" font-size="12">0000000000000000</text>
  <text x="492" y="138" font-size="11">page(k) fffffdffc0171000</text>
  <text x="492" y="190" font-size="11">jiffies ffffb6c39b34cac0</text>
  <text x="492" y="208" font-size="11">모듈  ffffb6c38d02f080</text>
  <text x="492" y="246" font-size="11">스택  ffff800080893aa4</text>
  <text x="492" y="264" font-size="11">vmalloc ffff80008008d000</text>
  <text x="492" y="300" font-size="11">모듈(nokaslr) ffff80007be9…</text>
  <text x="492" y="346" font-size="11">kmalloc ffff000005c40380</text>
  <text x="492" y="362" font-size="11">= 물리 0x45c40380</text>
  <text x="340" y="490" text-anchor="middle" font-size="12">위쪽일수록 높은 주소. 오른쪽 주소는 KASLR 켠 부팅의 실제 값</text>
</svg>
```

그림 2 — arm64 커널 가상 주소 지도. 경계 주소(왼쪽)는 예제 2가 매크로에서 찍은 값이고, 오른쪽은 실제 객체가 떨어진 위치다. linear map은 "더하기 빼기로 물리 주소가 나오는" 유일한 구역이다.

### 2.4 함정

- **"커널 주소는 물리 주소 + 상수"는 linear map에서만 맞다.** vmalloc 버퍼, 커널 스택(`VMAP_STACK`), 모듈의 전역 변수, `ioremap` 주소에 `virt_to_phys()`를 쓰면 엉뚱한 물리 주소가 나온다. 이 랩 커널은 `CONFIG_DEBUG_VIRTUAL`이 꺼져 있어서 경고조차 없다. 커널 스택 위의 지역 배열을 DMA 버퍼로 넘기는 오래된 드라이버 코드가 `VMAP_STACK` 도입 후 줄줄이 깨진 이유가 이것이다.
- 주소를 로그에 찍을 때 `%p`는 해시된 값을 찍는다. 디버깅용 실주소는 `%px`(이 노트처럼), 양산 코드에서는 `%p`를 쓴다.

---

## 3. struct page, pfn, 물리 주소, 가상 주소 — 네 가지 이름 사이 오가기

### 3.1 직관

같은 4 KiB 물리 페이지를 커널은 네 가지 이름으로 부른다.

- **물리 주소** `phys_addr_t`: 버스에 나가는 주소. DMA 엔진, 데이터시트, DT `reg`가 쓰는 숫자.
- **pfn** `unsigned long`: 물리 주소 >> 12. "몇 번째 페이지인가". 페이지 단위 장부의 인덱스.
- **`struct page *`**: 그 페이지의 관리 구조체 포인터. 참조 카운트, 플래그, 어느 slab/페이지 캐시에 속했는지, buddy 목록 링크가 들어 있다. buddy·slab·페이지 캐시 API는 전부 이걸 주고받는다.
- **커널 가상 주소** `void *`: CPU가 load/store할 때 쓰는 주소. linear map에 있으면 물리 주소와 상수 차이다.

`SPARSEMEM_VMEMMAP` 덕분에 `struct page` 배열은 가상 주소상 하나의 큰 배열(`vmemmap`)이라 pfn ↔ page 변환이 포인터 산술 한 번이다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">한 페이지의 네 가지 이름 — 예제 3 실측 값</text>
  <rect x="30" y="44" width="230" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="145" y="66" text-anchor="middle" font-size="12">커널 가상 주소 (linear map)</text>
  <text x="145" y="88" text-anchor="middle" font-size="12">ffff000005ea4000</text>
  <rect x="420" y="44" width="230" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="535" y="66" text-anchor="middle" font-size="12">물리 주소 (phys_addr_t)</text>
  <text x="535" y="88" text-anchor="middle" font-size="12">0x45ea4000</text>
  <rect x="420" y="230" width="230" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="535" y="252" text-anchor="middle" font-size="12">pfn</text>
  <text x="535" y="274" text-anchor="middle" font-size="12">0x45ea4</text>
  <rect x="30" y="230" width="230" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="145" y="252" text-anchor="middle" font-size="12">struct page * (vmemmap)</text>
  <text x="145" y="274" text-anchor="middle" font-size="12">fffffdffc017a900</text>
  <line x1="262" y1="62" x2="416" y2="62" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="340" y="56" text-anchor="middle" font-size="12">__pa / virt_to_phys</text>
  <line x1="418" y1="84" x2="264" y2="84" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="340" y="100" text-anchor="middle" font-size="12">__va / phys_to_virt</text>
  <line x1="510" y1="102" x2="510" y2="226" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="502" y="160" text-anchor="end" font-size="12">PHYS_PFN</text>
  <text x="502" y="176" text-anchor="end" font-size="12">(&gt;&gt; 12)</text>
  <line x1="560" y1="228" x2="560" y2="104" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="568" y="166" font-size="12">PFN_PHYS</text>
  <line x1="418" y1="248" x2="264" y2="248" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="340" y="242" text-anchor="middle" font-size="12">pfn_to_page</text>
  <line x1="262" y1="270" x2="416" y2="270" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="340" y="286" text-anchor="middle" font-size="12">page_to_pfn</text>
  <line x1="110" y1="228" x2="110" y2="104" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="102" y="166" text-anchor="end" font-size="12">page_address</text>
  <line x1="180" y1="102" x2="180" y2="226" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="188" y="166" font-size="12">virt_to_page</text>
  <rect x="30" y="316" width="620" height="66" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="338" text-anchor="middle" font-size="12">vmalloc · 스택 · 모듈 · ioremap 주소는 위 사각형 밖이다</text>
  <text x="340" y="358" text-anchor="middle" font-size="12">→ vmalloc_to_page / vmalloc_to_pfn (페이지 테이블을 걷는다)</text>
  <text x="340" y="374" text-anchor="middle" font-size="12">__pa / virt_to_phys / virt_to_page 를 쓰면 틀린 값</text>
</svg>
```

그림 3 — `virt ↔ phys ↔ pfn ↔ struct page`. 위 네 변환은 전부 덧셈·뺄셈·시프트라 비용이 거의 없다. 대신 **linear map 주소에만** 유효하다. 숫자는 예제 3의 실제 `kmalloc` 버퍼 값이다.

### 3.2 예제 3 — kmalloc / alloc_pages / vmalloc / kvmalloc 버퍼의 pfn (`f9_mem`)

무엇을 확인하나: (1) 네 변환이 서로 맞물리는지, (2) `kmalloc(16 KiB)`와 `alloc_pages(order 2)`는 물리적으로 연속인지, (3) `vmalloc(32 KiB)`의 페이지들은 물리적으로 어디에 있는지, (4) `kvmalloc`이 크기에 따라 어느 쪽으로 가는지.

```c
// SPDX-License-Identifier: GPL-2.0
/* f9_mem: kmalloc / alloc_pages / vmalloc / kvmalloc 의 virt ↔ phys ↔ pfn */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/gfp.h>
#include <linux/io.h>

static void *kbuf, *vbuf, *kv1, *kv8;
static struct page *pg;

static int __init f9_mem_init(void)
{
	phys_addr_t pa;
	int i;

	kbuf = kmalloc(16384, GFP_KERNEL);		/* 4 페이지 */
	pg = alloc_pages(GFP_KERNEL, 2);		/* order 2 = 4 페이지 */
	vbuf = vmalloc(8 * PAGE_SIZE);
	kv1 = kvmalloc(1 << 20, GFP_KERNEL);
	kv8 = kvmalloc(8 << 20, GFP_KERNEL);
	if (!kbuf || !pg || !vbuf || !kv1 || !kv8)
		return -ENOMEM;	/* 데모: 부분 해제 생략 */

	pa = virt_to_phys(kbuf);
	pr_info("kmalloc va=%px pa=%pa pfn=%#lx page=%px\n",
		kbuf, &pa, PHYS_PFN(pa), virt_to_page(kbuf));
	pr_info("  __va(pa)=%px  page_address(page)=%px\n",
		__va(pa), page_address(virt_to_page(kbuf)));
	for (i = 0; i < 4; i++)
		pr_info("  kmalloc +%d page pfn=%#lx\n", i,
			PHYS_PFN(virt_to_phys(kbuf + i * PAGE_SIZE)));
	pr_info("alloc_pages(order 2) page=%px pfn=%#lx va=%px\n",
		pg, page_to_pfn(pg), page_address(pg));
	pr_info("vmalloc va=%px (virt_addr_valid=%d)\n", vbuf, virt_addr_valid(vbuf));
	for (i = 0; i < 8; i++)
		pr_info("  vmalloc +%d page pfn=%#lx\n", i,
			vmalloc_to_pfn(vbuf + i * PAGE_SIZE));
	pr_info("kvmalloc 1MiB %px vmalloc?%d  8MiB %px vmalloc?%d\n",
		kv1, is_vmalloc_addr(kv1), kv8, is_vmalloc_addr(kv8));
	return 0;
}

static void __exit f9_mem_exit(void)
{
	kfree(kbuf);
	__free_pages(pg, 2);
	vfree(vbuf);
	kvfree(kv1);
	kvfree(kv8);
}
module_init(f9_mem_init);
module_exit(f9_mem_exit);
MODULE_DESCRIPTION("F9: virt/phys/pfn of kernel allocations");
MODULE_LICENSE("GPL");
```

```sh
# 게스트 안 (t.sh)
insmod /f9_mem.ko
dmesg | grep f9_mem:
rmmod f9_mem && echo "rmmod ok"
```

```text
lab initramfs: 6.18.55-dbg up 12.22s
[   14.279650] f9_mem: kmalloc va=ffff000005ea4000 pa=0x0000000045ea4000 pfn=0x45ea4 page=fffffdffc017a900
[   14.279828] f9_mem:   __va(pa)=ffff000005ea4000  page_address(page)=ffff000005ea4000
[   14.279861] f9_mem:   kmalloc +0 page pfn=0x45ea4
[   14.279887] f9_mem:   kmalloc +1 page pfn=0x45ea5
[   14.279903] f9_mem:   kmalloc +2 page pfn=0x45ea6
[   14.279915] f9_mem:   kmalloc +3 page pfn=0x45ea7
[   14.279942] f9_mem: alloc_pages(order 2) page=fffffdffc017aa00 pfn=0x45ea8 va=ffff000005ea8000
[   14.279968] f9_mem: vmalloc va=ffff800080895000 (virt_addr_valid=0)
[   14.280006] f9_mem:   vmalloc +0 page pfn=0x46124
[   14.280026] f9_mem:   vmalloc +1 page pfn=0x46123
[   14.280036] f9_mem:   vmalloc +2 page pfn=0x4611c
[   14.280046] f9_mem:   vmalloc +3 page pfn=0x4603c
[   14.280055] f9_mem:   vmalloc +4 page pfn=0x46011
[   14.280064] f9_mem:   vmalloc +5 page pfn=0x46029
[   14.280073] f9_mem:   vmalloc +6 page pfn=0x46036
[   14.280082] f9_mem:   vmalloc +7 page pfn=0x46104
[   14.280116] f9_mem: kvmalloc 1MiB ffff000006200000 vmalloc?0  8MiB ffff800080a00000 vmalloc?1
rmmod ok
```

출력에서 볼 것:

- 네 이름이 맞물린다: `va ffff000005ea4000` → `pa 0x45ea4000` → `pfn 0x45ea4` → `page fffffdffc017a900` → `page_address` = 다시 `ffff000005ea4000`. `struct page` 주소도 계산과 일치한다: `fffffdffc0000000 + (0x45ea4 - 0x40000) × 64` = `fffffdffc017a900`.
- `kmalloc(16 KiB)`의 4 페이지는 pfn `0x45ea4~0x45ea7`로 **물리 연속**이다. 16 KiB는 8 KiB를 넘으므로 slab이 아니라 buddy에서 order-2 블록을 직접 받은 것이고(§6.4), 바로 다음 `alloc_pages(order 2)`가 이웃 블록 `0x45ea8`을 받았다.
- `vmalloc(32 KiB)`의 8 페이지는 pfn이 `0x46124, 0x46123, 0x4611c, 0x4603c, …`로 **순서도 없고 흩어져 있다.** 가상 주소로만 연속이다. `virt_addr_valid=0` — linear map 주소가 아니라는 뜻이고, 그래서 `virt_to_phys`가 아니라 `vmalloc_to_pfn`(페이지 테이블 walk)으로 물었다.
- `kvmalloc`: 1 MiB는 `kmalloc`으로 성공해서 linear 주소(`ffff0000...`), 8 MiB는 `KMALLOC_MAX_SIZE`(4 MiB, E4 §6.2에서 측정)를 넘으므로 vmalloc으로 대체됐다(`ffff8000...`). 해제는 둘 다 `kvfree`.

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
<text x="320" y="22" text-anchor="middle" font-size="13" font-weight="bold">가상 페이지 번호 → 물리 pfn (예제 3 실측)</text>
<line x1="90" y1="231.4" x2="620" y2="231.4" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="84" y="235.4" text-anchor="end" font-size="12">0x45ea0</text>
<line x1="90" y1="214.1" x2="620" y2="214.1" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="84" y="218.1" text-anchor="end" font-size="12">0x45ee0</text>
<line x1="90" y1="196.8" x2="620" y2="196.8" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="84" y="200.8" text-anchor="end" font-size="12">0x45f20</text>
<line x1="90" y1="179.5" x2="620" y2="179.5" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="84" y="183.5" text-anchor="end" font-size="12">0x45f60</text>
<line x1="90" y1="162.3" x2="620" y2="162.3" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="84" y="166.3" text-anchor="end" font-size="12">0x45fa0</text>
<line x1="90" y1="145.0" x2="620" y2="145.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="84" y="149.0" text-anchor="end" font-size="12">0x45fe0</text>
<line x1="90" y1="127.7" x2="620" y2="127.7" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="84" y="131.7" text-anchor="end" font-size="12">0x46020</text>
<line x1="90" y1="110.5" x2="620" y2="110.5" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="84" y="114.5" text-anchor="end" font-size="12">0x46060</text>
<line x1="90" y1="93.2" x2="620" y2="93.2" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="84" y="97.2" text-anchor="end" font-size="12">0x460a0</text>
<line x1="90" y1="75.9" x2="620" y2="75.9" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="84" y="79.9" text-anchor="end" font-size="12">0x460e0</text>
<line x1="90" y1="58.6" x2="620" y2="58.6" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="84" y="62.6" text-anchor="end" font-size="12">0x46120</text>
<line x1="90" y1="240" x2="620" y2="240" stroke="currentColor"/><line x1="90" y1="50" x2="90" y2="240" stroke="currentColor"/>
<polyline points="120.0,57.6 187.1,57.8 254.3,59.7 321.4,120.2 388.6,131.8 455.7,125.3 522.9,121.8 590.0,66.2" fill="none" stroke="#e08a3c" stroke-width="1.5"/>
<circle cx="120.0" cy="57.6" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<circle cx="187.1" cy="57.8" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<circle cx="254.3" cy="59.7" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<circle cx="321.4" cy="120.2" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<circle cx="388.6" cy="131.8" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<circle cx="455.7" cy="125.3" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<circle cx="522.9" cy="121.8" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<circle cx="590.0" cy="66.2" r="4" fill="#e08a3c" stroke="#e08a3c"/>
<polyline points="120.0,230.3 187.1,230.0 254.3,229.7 321.4,229.5" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<circle cx="120.0" cy="230.3" r="4" fill="#4a7bd0" stroke="#4a7bd0"/>
<circle cx="187.1" cy="230.0" r="4" fill="#4a7bd0" stroke="#4a7bd0"/>
<circle cx="254.3" cy="229.7" r="4" fill="#4a7bd0" stroke="#4a7bd0"/>
<circle cx="321.4" cy="229.5" r="4" fill="#4a7bd0" stroke="#4a7bd0"/>
<text x="120.0" y="256" text-anchor="middle" font-size="12">+0</text>
<text x="187.1" y="256" text-anchor="middle" font-size="12">+1</text>
<text x="254.3" y="256" text-anchor="middle" font-size="12">+2</text>
<text x="321.4" y="256" text-anchor="middle" font-size="12">+3</text>
<text x="388.6" y="256" text-anchor="middle" font-size="12">+4</text>
<text x="455.7" y="256" text-anchor="middle" font-size="12">+5</text>
<text x="522.9" y="256" text-anchor="middle" font-size="12">+6</text>
<text x="590.0" y="256" text-anchor="middle" font-size="12">+7</text>
<text x="355" y="274" text-anchor="middle" font-size="12">버퍼 안의 페이지 번호 (가상 주소로는 연속)</text>
<circle cx="150" cy="288" r="5" fill="#4a7bd0" stroke="#4a7bd0"/><text x="160" y="292" font-size="12">kmalloc(16 KiB): pfn이 1씩 증가</text>
<circle cx="390" cy="288" r="5" fill="#e08a3c" stroke="#e08a3c"/><text x="400" y="292" font-size="12">vmalloc(32 KiB): pfn이 흩어짐</text>
</svg>
```

그림 4 — 예제 3에서 잰 "버퍼 안의 n번째 페이지 → 물리 pfn". `kmalloc`은 직선(연속), `vmalloc`은 들쭉날쭉(흩어짐). DMA 엔진은 물리 주소로 움직이므로, 주황색 버퍼를 하나의 DMA 디스크립터로 넘길 수 없다. scatter-gather 목록을 만들어야 한다(H8).

### 3.3 예제 4 — /proc/vmallocinfo로 vmalloc 할당 보기

무엇을 확인하나: 커널 전체의 vmalloc 영역 사용 내역에서 예제 3의 두 vmalloc 할당(32 KiB, 8 MiB)을 찾는다.

```sh
# 게스트 안 (t.sh) — 같은 f9_mem.ko, 새 부팅
insmod /f9_mem.ko
dmesg | grep f9_mem: | tail -1
echo "--- /proc/vmallocinfo"
grep -E "pages=(8|2048) " /proc/vmallocinfo
wc -l /proc/vmallocinfo
rmmod f9_mem && echo "rmmod ok"
```

```text
lab initramfs: 6.18.55-dbg up 19.56s
[   21.718140] f9_mem: kvmalloc 1MiB ffff000006200000 vmalloc?0  8MiB ffff800080a00000 vmalloc?1
--- /proc/vmallocinfo
0x00000000ded88af8-0x00000000ccff5f32 8392704 0xffffd3167b10e12c pages=2048 vmalloc vpages N0=2048
0x000000003f1c477f-0x00000000e08d38ae   36864 0xffffd3167b10e0fc pages=8 vmalloc N0=8
108 /proc/vmallocinfo
rmmod ok
```

출력에서 볼 것: 크기가 요청보다 **한 페이지 크다** — 32 KiB 요청이 36864(9 페이지), 8 MiB 요청이 8392704(2049 페이지). 각 vmalloc 영역 뒤에 매핑하지 않은 **guard page**를 하나 두어서, 버퍼를 넘쳐 쓰면 바로 page fault(Oops)가 나게 한 것이다(E4의 VMAP_STACK guard와 같은 원리). `vpages`는 2048개짜리 `struct page *` 배열 자체도 vmalloc으로 잡았다는 표시다. 시작 주소와 호출자가 해시값으로 나온 것은 `kptr_restrict` 정책 때문이다(이 게스트 설정 그대로 둠).

### 3.4 함정

- `virt_to_page(vmalloc_ptr)`는 **에러 없이 엉뚱한 `struct page`를 돌려준다.** `CONFIG_DEBUG_VIRTUAL`이 켜진 커널이면 경고가 나지만 양산 커널에선 조용히 메모리를 망가뜨린다. 버퍼가 어디서 왔는지 모르는 공용 함수라면 `is_vmalloc_addr()`로 갈라서 처리한다(`kvfree`가 내부에서 하는 일).
- `page_address()`는 linear map에 매핑된 페이지에만 의미가 있다. 32비트 ARM의 HIGHMEM 페이지는 linear map 밖이라 `kmap_local_page()`가 필요했다. arm64는 HIGHMEM이 없지만, 32비트 ARM SoC(i.MX6 등) 코드를 볼 때 이 차이를 기억한다.

---

## 4. buddy 할당자 — 2의 거듭제곱으로 쪼개고 합치기

### 4.1 직관

펌웨어의 고정 크기 풀은 크기가 하나라 단편화가 없다. 하지만 커널은 4 KiB부터 4 MiB까지 다양한 크기의 **물리 연속** 블록을 줘야 한다. buddy 할당자의 답은 단순하다.

- 블록 크기를 2^order 페이지로만 허용한다(order 0 = 4 KiB … order 10 = 4 MiB).
- order마다 free 블록 목록 `free_area[order]`를 둔다.
- order-n이 필요한데 그 목록이 비었으면, 더 큰 블록을 반으로 쪼개고(split) 남는 반쪽("버디")을 아래 order 목록에 넣는다.
- 해제할 때는 **버디**(pfn XOR 2^order인 이웃 블록)가 free인지 본다. free면 합쳐서(merge) order+1로 올리고 다시 반복한다.

버디의 위치가 계산(XOR)으로 바로 나오니 합치기가 O(1)이고, 블록은 항상 자기 크기로 정렬돼 있다(order-10 블록은 4 MiB 정렬). Don이 NAND 블록 관리에서 썼던 "정렬된 2의 거듭제곱 단위 + 비트맵" 감각 그대로다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">order-0 1장 요청 → order-3 블록이 쪼개지는 과정 (pfn 0x100~0x107 예시)</text>
  <text x="30" y="58" font-size="12">order 3</text>
  <rect x="100" y="42" width="320" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="260" y="60" text-anchor="middle" font-size="12">free 8 페이지 (0x100–0x107)</text>
  <text x="30" y="118" font-size="12">order 2</text>
  <rect x="100" y="102" width="160" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="180" y="120" text-anchor="middle" font-size="12">쪼갬 (0x100–0x103)</text>
  <rect x="260" y="102" width="160" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="120" text-anchor="middle" font-size="12">버디 → free[2]</text>
  <text x="30" y="178" font-size="12">order 1</text>
  <rect x="100" y="162" width="80" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="140" y="180" text-anchor="middle" font-size="12">쪼갬</text>
  <rect x="180" y="162" width="80" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="220" y="180" text-anchor="middle" font-size="12">→ free[1]</text>
  <text x="30" y="238" font-size="12">order 0</text>
  <rect x="100" y="222" width="40" height="26" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="120" y="240" text-anchor="middle" font-size="12">할당</text>
  <rect x="140" y="222" width="40" height="26" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="160" y="240" text-anchor="middle" font-size="12">free</text>
  <line x1="100" y1="68" x2="100" y2="100" stroke="currentColor" stroke-dasharray="3 3"/>
  <line x1="420" y1="68" x2="420" y2="100" stroke="currentColor" stroke-dasharray="3 3"/>
  <line x1="100" y1="128" x2="100" y2="160" stroke="currentColor" stroke-dasharray="3 3"/>
  <line x1="260" y1="128" x2="260" y2="160" stroke="currentColor" stroke-dasharray="3 3"/>
  <line x1="100" y1="188" x2="100" y2="220" stroke="currentColor" stroke-dasharray="3 3"/>
  <line x1="180" y1="188" x2="180" y2="220" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="450" y="42" width="215" height="94" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="462" y="62" font-size="12">free_area 개수 변화</text>
  <text x="462" y="84" font-size="12">전: [3]=1 [2]=0 [1]=0 [0]=0</text>
  <text x="462" y="104" font-size="12">후: [3]=0 [2]=1 [1]=1 [0]=1</text>
  <text x="462" y="124" font-size="12">할당된 것: pfn 0x100 한 장</text>
  <rect x="450" y="152" width="215" height="96" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="462" y="172" font-size="12">해제할 때 (역순)</text>
  <text x="462" y="192" font-size="12">buddy = pfn XOR (1 &lt;&lt; order)</text>
  <text x="462" y="212" font-size="12">0x100 의 order-0 버디 = 0x101</text>
  <text x="462" y="232" font-size="12">free면 합쳐 order+1, 반복</text>
  <text x="340" y="290" text-anchor="middle" font-size="12">버디 중 하나라도 사용 중이면 합칠 수 없다 → 단편화(§4.5)</text>
  <text x="340" y="312" text-anchor="middle" font-size="12">/proc/buddyinfo 의 열 = order 0..10 별 free 블록 개수</text>
</svg>
```

그림 5 — buddy 분할과 병합. 할당은 위에서 아래로 쪼개고, 해제는 아래에서 위로 버디를 찾아 합친다.

### 4.2 /proc/buddyinfo 읽기

예제 1의 `Node 0, zone DMA 2 1 3 2 3 1 3 3 2 2 96`은 order 0부터 10까지 **free 블록 개수**다. 마지막 96은 "4 MiB 블록 96개 = 384 MiB"로, 부팅 직후 메모리 대부분이 가장 큰 블록으로 뭉쳐 있다는 뜻이다. free 메모리 총량은 `Σ count × 4 KiB × 2^order`이다.

`MAX_PAGE_ORDER`(옛 이름 `MAX_ORDER - 1`)가 10이라 order 10이 최대다. 커널 버전마다 이 매크로 이름이 바뀌었다 — 6.4 무렵 `MAX_ORDER`의 의미가 "포함하는 최대 order"로 바뀌었고, 6.8에서 `MAX_PAGE_ORDER`로 이름이 바뀌었다. 오래된 드라이버 코드에서 `MAX_ORDER - 1`을 보면 같은 뜻이다.

### 4.3 예제 5 — order-N 블록을 잡아서 buddyinfo 바꾸기, 그리고 실패할 때까지 (`f9_buddy`)

무엇을 확인하나: (1) order-9 블록 3개를 잡으면 buddyinfo가 어떻게 변하나, (2) order-10(4 MiB)을 계속 잡으면 몇 개에서 실패하고 그 경고는 어떻게 생겼나, (3) order-0을 많이 잡고 절반만 돌려주면(단편화) 어떻게 되나.

```c
// SPDX-License-Identifier: GPL-2.0
/* f9_buddy: order-N 블록을 count개 잡고 rmmod까지 유지 (실패하면 멈춤) */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

static uint order = 10, count = 1;
static bool frag;		/* true: order-0 count개 잡고 홀수 번째만 해제 */
module_param(order, uint, 0444);
module_param(count, uint, 0444);
module_param(frag, bool, 0444);

static struct page **pages;
static uint got;

static int __init f9_buddy_init(void)
{
	uint i;

	pages = vcalloc(count, sizeof(*pages));
	if (!pages)
		return -ENOMEM;
	if (frag)
		order = 0;
	for (got = 0; got < count; got++) {
		pages[got] = alloc_pages(GFP_KERNEL, order);
		if (!pages[got])
			break;
	}
	pr_info("order=%u: got %u of %u blocks (%lu KiB each)\n",
		order, got, count, (PAGE_SIZE << order) >> 10);
	if (frag) {
		for (i = 1; i < got; i += 2) {
			__free_pages(pages[i], 0);
			pages[i] = NULL;
		}
		pr_info("frag: freed every other page -> %u pages back\n", got / 2);
	}
	return 0;
}

static void __exit f9_buddy_exit(void)
{
	uint i;

	for (i = 0; i < got; i++)
		if (pages[i])
			__free_pages(pages[i], order);
	vfree(pages);
}
module_init(f9_buddy_init);
module_exit(f9_buddy_exit);
MODULE_DESCRIPTION("F9: buddy allocator orders and failure");
MODULE_LICENSE("GPL");
```

```sh
# 게스트 안 (t.sh)
B() { echo "[$1]"; cat /proc/buddyinfo; }
B boot
insmod /f9_buddy.ko order=9 count=3
B "after 3 x order-9"
rmmod f9_buddy
B "after rmmod"
insmod /f9_buddy.ko order=10 count=200
B "after order-10 hog"
grep -E "MemFree" /proc/meminfo
rmmod f9_buddy
insmod /f9_buddy.ko frag=1 count=20000
B "after frag (20000 x order-0, every other freed)"
grep -E "MemFree" /proc/meminfo
rmmod f9_buddy
dmesg | grep -A40 "page allocation failure" | head -60
dmesg | grep "f9_buddy: order\|f9_buddy: frag"
```

```text
lab initramfs: 6.18.55-dbg up 13.57s
[boot]
Node 0, zone      DMA      6      3      2      2      6      6      4      6      4      4     98 
[after 3 x order-9]
Node 0, zone      DMA      6      3      2      2      6      6      4      6      4      4     96 
[after rmmod]
Node 0, zone      DMA     13      5      2      2      6      6      4      6      4      4     97 
[after order-10 hog]
Node 0, zone      DMA     13     15      8     11     15     11      5      8      4      5      7 
MemFree:           51312 kB
[after frag (20000 x order-0, every other freed)]
Node 0, zone      DMA   9187     18      8      6     12      7      4      6      4      5     79 
MemFree:          380892 kB
[   15.896492] insmod: page allocation failure: order:10, mode:0xcc0(GFP_KERNEL), nodemask=(null),cpuset=/,mems_allowed=0
[   15.899094] CPU: 1 UID: 0 PID: 78 Comm: insmod Tainted: G           O        6.18.55-dbg #1 PREEMPT 
[   15.900521] Tainted: [O]=OOT_MODULE
[   15.900569] Hardware name: linux,dummy-virt (DT)
[   15.900796] Call trace:
[   15.904227]  show_stack+0x30/0x90 (C)
[   15.904878]  dump_stack_lvl+0x70/0x98
[   15.904908]  dump_stack+0x18/0x24
[   15.904928]  warn_alloc+0x138/0x1d0
[   15.904962]  __alloc_frozen_pages_noprof+0x5d4/0x1178
[   15.904982]  alloc_pages_mpol+0xc8/0x1c0
[   15.905009]  alloc_frozen_pages_noprof+0x4c/0xd8
[   15.905030]  alloc_pages_noprof+0x18/0x70
[   15.905050]  f9_buddy_init+0x6c/0xfb0 [f9_buddy]
[   15.905481]  do_one_initcall+0x84/0x380
...
[   15.909501] Mem-Info:
[   15.909788] active_anon:455 inactive_anon:106 isolated_anon:6
[   15.909788]  active_file:0 inactive_file:0 isolated_file:0
[   15.909788]  unevictable:0 dirty:0 writeback:0
[   15.909788]  slab_reclaimable:406 slab_unreclaimable:2563
[   15.909788]  mapped:438 shmem:564 pagetables:49
[   15.909788]  sec_pagetables:0 bounce:0
[   15.909788]  kernel_misc_reclaimable:0
[   15.909788]  free:12943 free_pcp:128 free_cma:7936
...
[   15.911381] Node 0 DMA free:51772kB boost:0kB min:2660kB low:3324kB high:3988kB reserved_highatomic:0KB free_highatomic:0KB active_anon:1820kB inactive_anon:424kB active_file:0kB inactive_file:0kB unevictable:0kB writepending:0kB zspages:0kB present:524288kB managed:450560kB mlocked:0kB bounce:0kB free_pcp:512kB local_pcp:512kB free_cma:31744kB
[   15.916226] lowmem_reserve[]: 0 0 0 0
[   15.916383] Node 0 DMA: 41*4kB (UME) 31*8kB (UME) 16*16kB (UME) 11*32kB (UME) 15*64kB (UME) 11*128kB (UME) 5*256kB (UME) 8*512kB (UME) 4*1024kB (MC) 5*2048kB (MEC) 7*4096kB (C) = 51772kB
...
[   15.043169] f9_buddy: order=9: got 3 of 3 blocks (2048 KiB each)
[   15.917275] f9_buddy: order=10: got 91 of 200 blocks (4096 KiB each)
[   18.234609] f9_buddy: order=0: got 20000 of 20000 blocks (4 KiB each)
[   18.564543] f9_buddy: frag: freed every other page -> 10000 pages back
```

(경고 덩어리는 `...` 자리에서 콜 트레이스 하단·노드 상세·hugepages 줄을 생략했다.)

출력에서 볼 것:

- **order-9 × 3**: order-9 열(4)은 그대로인데 order-10이 98 → 96으로 두 개 줄었다. 기존 order-9 free 블록이 있는데도 order-10을 쪼갰다 — 다음 예제 6에서 이유(migratetype)를 본다.
- **order-10 고갈**: 200개를 요청해서 **91개(364 MiB)에서 실패**했다. 실패 순간 커널이 `warn_alloc()`으로 경고를 찍었다. 아래 세 줄이 읽는 법이다.
- 경고 첫 줄 `page allocation failure: order:10, mode:0xcc0(GFP_KERNEL)` — 누가(`insmod`), 몇 order를, 어떤 GFP로 요청했나. `0xcc0` = `__GFP_IO`(0x40) + `__GFP_FS`(0x80) + `__GFP_DIRECT_RECLAIM`(0x400) + `__GFP_KSWAPD_RECLAIM`(0x800) = `GFP_KERNEL`(6.18의 비트 배치, §8 표). 커널이 괄호 안에 이름으로 풀어 준다.
- 콜 트레이스의 `f9_buddy_init+0x6c` — 실패한 호출 지점. `Mem-Info` 아래는 실패 순간의 메모리 상태 스냅숏이다.
- `Node 0 DMA: 41*4kB (UME) … 7*4096kB (C) = 51772kB` — order별 free 블록 수와 그 블록들의 **migratetype** 문자(U=Unmovable, M=Movable, E=Reclaimable, C=CMA).
- 결정적인 단서: 실패한 순간에도 **4 MiB free 블록이 7개 있었다** — 전부 `(C)`, 즉 CMA 영역이다. `free_cma:31744kB`. `GFP_KERNEL`로 잡는 커널 메모리는 "옮길 수 없는(unmovable)" 페이지라 CMA 블록을 쓸 수 없다. CMA는 나중에 카메라 드라이버 같은 곳이 큰 연속 버퍼를 달라고 할 때 비워 줄 수 있어야 하므로 **옮길 수 있는 페이지(유저 페이지, 페이지 캐시)만** 빌려준다(F10).
- **frag**: order-0 2만 장(78 MiB)을 잡고 홀수 번째 1만 장을 돌려줬다. `MemFree`는 다시 380 MiB로 넉넉한데 order-0 free 블록이 **9187개**로 폭증했고 order-10은 79개다. 돌려준 페이지들의 버디(짝수 번째)가 아직 사용 중이라 합칠 수 없기 때문이다. 이 상태에서 order-1 이상 요청은 이 9187장을 하나도 쓸 수 없다. **이것이 외부 단편화(external fragmentation)다.**

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="22" text-anchor="middle" font-size="13" font-weight="bold">order별 free 메모리 (MiB, 로그 축) — /proc/buddyinfo 실측</text>
<line x1="70" y1="290.0" x2="660" y2="290.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="64" y="294.0" text-anchor="end" font-size="12">0.01</text>
<line x1="70" y1="242.0" x2="660" y2="242.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="64" y="246.0" text-anchor="end" font-size="12">0.1</text>
<line x1="70" y1="194.0" x2="660" y2="194.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="64" y="198.0" text-anchor="end" font-size="12">1</text>
<line x1="70" y1="146.0" x2="660" y2="146.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="64" y="150.0" text-anchor="end" font-size="12">10</text>
<line x1="70" y1="98.0" x2="660" y2="98.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="64" y="102.0" text-anchor="end" font-size="12">100</text>
<line x1="70" y1="50.0" x2="660" y2="50.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="64" y="54.0" text-anchor="end" font-size="12">1000</text>
<line x1="70" y1="290" x2="660" y2="290" stroke="currentColor"/><line x1="70" y1="40" x2="70" y2="290" stroke="currentColor"/>
<rect x="77.3" y="272.2" width="12" height="17.8" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="90.3" y="256.1" width="12" height="33.9" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="103.3" y="119.4" width="12" height="170.6" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="96.8" y="306" text-anchor="middle" font-size="12">0</text>
<rect x="131.0" y="272.2" width="12" height="17.8" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="144.0" y="238.7" width="12" height="51.3" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="157.0" y="234.9" width="12" height="55.1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="150.5" y="306" text-anchor="middle" font-size="12">1</text>
<rect x="184.6" y="266.2" width="12" height="23.8" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="197.6" y="237.3" width="12" height="52.7" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="210.6" y="237.3" width="12" height="52.7" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="204.1" y="306" text-anchor="middle" font-size="12">2</text>
<rect x="238.2" y="251.8" width="12" height="38.2" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="251.2" y="216.3" width="12" height="73.7" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="264.2" y="228.9" width="12" height="61.1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="257.7" y="306" text-anchor="middle" font-size="12">3</text>
<rect x="291.9" y="214.4" width="12" height="75.6" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="304.9" y="195.3" width="12" height="94.7" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="317.9" y="200.0" width="12" height="90.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="311.4" y="306" text-anchor="middle" font-size="12">4</text>
<rect x="345.5" y="200.0" width="12" height="90.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="358.5" y="187.4" width="12" height="102.6" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="371.5" y="196.8" width="12" height="93.2" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="365.0" y="306" text-anchor="middle" font-size="12">5</text>
<rect x="399.1" y="194.0" width="12" height="96.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="412.1" y="189.3" width="12" height="100.7" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="425.1" y="194.0" width="12" height="96.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="418.6" y="306" text-anchor="middle" font-size="12">6</text>
<rect x="452.8" y="171.1" width="12" height="118.9" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="465.8" y="165.1" width="12" height="124.9" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="478.8" y="171.1" width="12" height="118.9" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="472.3" y="306" text-anchor="middle" font-size="12">7</text>
<rect x="506.4" y="165.1" width="12" height="124.9" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="519.4" y="165.1" width="12" height="124.9" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="532.4" y="165.1" width="12" height="124.9" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="525.9" y="306" text-anchor="middle" font-size="12">8</text>
<rect x="560.0" y="150.7" width="12" height="139.3" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="573.0" y="146.0" width="12" height="144.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="586.0" y="146.0" width="12" height="144.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="579.5" y="306" text-anchor="middle" font-size="12">9</text>
<rect x="613.7" y="69.5" width="12" height="220.5" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<rect x="626.7" y="124.5" width="12" height="165.5" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<rect x="639.7" y="74.0" width="12" height="216.0" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="633.2" y="306" text-anchor="middle" font-size="12">10</text>
<text x="365" y="322" text-anchor="middle" font-size="12">order (블록 크기 = 4 KiB × 2^order)</text>
<rect x="150" y="340" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="168" y="350" font-size="12">부팅 직후</text>
<rect x="251" y="340" width="12" height="12" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="269" y="350" font-size="12">order-10 고갈 후</text>
<rect x="403" y="340" width="12" height="12" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="421" y="350" font-size="12">order-0 2만 장 중 절반 해제 후</text>
</svg>
```

그림 6 — 예제 5의 `/proc/buddyinfo`를 order별 free 메모리(MiB, 로그 축)로 환산했다. 파랑(부팅 직후)은 거의 전부 order-10에 몰려 있고, 주황(order-10 고갈 후)은 order-10이 CMA 7개(28 MiB)만 남았고, 초록(단편화 후)은 order-0에 36 MiB가 쌓였는데 큰 블록은 줄어든 채 돌아오지 않는다.

### 4.4 예제 6 — migratetype: 왜 order-9이 아니라 order-10을 쪼갰나

무엇을 확인하나: free 블록이 migratetype별로 따로 관리되는지, 그리고 `GFP_KERNEL`(Unmovable) 요청이 자기 타입에 블록이 없을 때 무엇을 하는지.

```sh
# 게스트 안 (t.sh)
P() { echo "[$1]"; cat /proc/buddyinfo; grep -E "^Node.*(Unmovable|Movable|CMA|Reclaimable) " /proc/pagetypeinfo | head -4; }
head -3 /proc/pagetypeinfo
P boot
insmod /f9_buddy.ko order=9 count=3
P "after 3 x order-9"
grep -A1 "Number of blocks type" /proc/pagetypeinfo
```

```text
lab initramfs: 6.18.55-dbg up 9.68s
Page block order: 9
Pages per block:  512

[boot]
Node 0, zone      DMA      2      2      2      3      1      2      2      3      3      4     96 
Node    0, zone      DMA, type    Unmovable      0      1      0      1      1      0      0      1      1      0      0 
Node    0, zone      DMA, type      Movable      1      1      2      1      0      1      1      0      1      2     89 
Node    0, zone      DMA, type  Reclaimable      1      0      0      1      0      0      1      1      0      1      0 
Node    0, zone      DMA, type          CMA      0      0      0      0      0      1      0      1      1      1      7 
[after 3 x order-9]
Node 0, zone      DMA     24      6      5      4      3      3      3      5      3      4     94 
Node    0, zone      DMA, type    Unmovable      0      1      0      1      2      0      0      1      1      0      0 
Node    0, zone      DMA, type      Movable      4      1      4      2      1      2      2      2      1      2     87 
Node    0, zone      DMA, type  Reclaimable      1      0      0      1      0      0      1      1      0      1      0 
Node    0, zone      DMA, type          CMA      0      0      0      0      0      1      0      1      1      1      7 
Number of blocks type     Unmovable      Movable  Reclaimable   HighAtomic          CMA      Isolate 
Node 0, zone      DMA           16          218            6            0           16            0
```

출력에서 볼 것:

- buddy의 free 목록은 실제로 `free_area[order].free_list[migratetype]` — order × migratetype 2차원이다. **Unmovable의 order-9 열은 0**이다. order-9 free 블록 4개는 Movable(2)·Reclaimable(1)·CMA(1) 소속이었다.
- 그래서 Unmovable 요청은 **fallback**으로 다른 타입의 블록을 "훔친다"(steal). 여기서는 Movable의 order-10이 89 → 87로 두 개 줄었다. 큰 블록을 통째로 가져와 pageblock(512 페이지 = order 9) 단위로 Unmovable로 바꿔 쓰는 것이 단편화를 덜 만든다는 정책이다.
- `Number of blocks type`: 512 페이지(2 MiB) pageblock 256개 중 Unmovable 16, Movable 218, CMA 16. 이 분리가 있어서 **compaction**(옮길 수 있는 페이지를 한쪽으로 몰아 큰 빈 블록을 만드는 일)이 Movable 블록 안에서 효과를 낼 수 있다.
- 작은 order 숫자(order-0 2 → 24 등)는 같은 시간에 다른 커널 활동이 쪼갠 결과라 매 실행 다르다.

### 4.5 예제 7 — 단편화는 compaction으로 고쳐지나?

무엇을 확인하나: 예제 5의 단편화 상태에서 수동 compaction(`/proc/sys/vm/compact_memory`)을 돌리면 큰 블록이 돌아오는지, 그리고 모듈이 전부 해제하면 어떻게 되는지.

```sh
# 게스트 안 (t.sh)
B() { echo "[$1]"; cat /proc/buddyinfo; }
B boot
insmod /f9_buddy.ko frag=1 count=20000
B "after frag"
echo 1 > /proc/sys/vm/compact_memory
B "after compact_memory"
grep -E "^compact_(stall|success|fail|migrate_scanned|free_scanned|isolated)" /proc/vmstat
rmmod f9_buddy
B "after rmmod (all freed, buddies merge back)"
```

```text
lab initramfs: 6.18.55-dbg up 14.21s
[boot]
Node 0, zone      DMA     18     11      4      5      2      5      6      6      4      4     97 
[after frag]
Node 0, zone      DMA   9929      3      2      2      1      8      5      7      4      2     78 
[after compact_memory]
Node 0, zone      DMA   9930      3      2      2      1      6      5      6      3      2     78 
compact_migrate_scanned 155648
compact_free_scanned 2035
compact_isolated 1592
compact_stall 0
compact_fail 0
compact_success 0
[after rmmod (all freed, buddies merge back)]
Node 0, zone      DMA   3330     19     18     10      5      7      8      8      5      4     90 
```

출력에서 볼 것:

- compaction은 15만 페이지를 훑었지만(`compact_migrate_scanned 155648`) order-0 9929장은 그대로다. 구멍 사이에 박힌 짝수 번째 페이지가 **커널이 `alloc_pages(GFP_KERNEL)`로 잡은 unmovable 페이지**라서 옮길 수 없다. compaction은 유저 페이지·페이지 캐시처럼 **페이지 테이블만 고치면 되는** 페이지만 옮긴다. 커널 포인터가 직접 가리키는 메모리는 주소가 바뀌면 안 되니까.
- `rmmod`로 전부 돌려주면 order-10이 90개로 회복되지만 order-0이 3330장 남는다. 해제된 페이지 일부가 CPU별 **pcp 목록**에 머물러 있어서 그 버디들이 아직 buddy에 안 돌아왔기 때문이다. pcp는 order-0 할당을 빠르게 하려고 CPU마다 페이지를 쌓아 두는 캐시이고, 나중에 일괄 반납된다.
- 교훈: **오래 도는 장비에서 큰 물리 연속 버퍼가 필요하면 부팅 직후(`probe()`) 미리 잡아 두거나 CMA/reserved-memory를 쓴다(F10).** "메모리는 충분한데 order-4 할당이 실패한다"는 현장 장애의 정체가 이것이다.

---

## 5. 페이지 할당자 API

| 함수 | 돌려주는 것 | 비고 |
|---|---|---|
| `alloc_pages(gfp, order)` | `struct page *` (2^order 연속 페이지의 첫 장) | 기본 API. 해제 `__free_pages(page, order)` |
| `alloc_page(gfp)` | `struct page *` (1장) | `alloc_pages(gfp, 0)` |
| `__get_free_pages(gfp, order)` | `unsigned long` 커널 가상 주소 | linear 주소를 바로 원할 때. 해제 `free_pages(addr, order)` |
| `__get_free_page(gfp)` / `get_zeroed_page(gfp)` | `unsigned long` | 1장, 0으로 채운 1장 |
| `alloc_pages_node(nid, gfp, order)` | `struct page *` | NUMA 노드 지정 (임베디드는 보통 불필요) |
| `get_order(size)` | 크기를 덮는 최소 order | `get_order(16384)` = 2 |

- 해제할 때 **할당 때와 같은 order**를 넘겨야 한다. 틀리면 buddy 장부가 망가진다(`DEBUG_VM` 커널은 `bad page state`로 잡기도 한다).
- order가 `PAGE_ALLOC_COSTLY_ORDER`(3, 즉 32 KiB)를 넘으면 할당자가 "비싼 요청"으로 보고 재시도·OOM 정책이 달라진다: order ≤ 3인 `GFP_KERNEL` 요청은 사실상 실패하지 않고(필요하면 OOM killer까지 부른다), order > 3은 reclaim·compaction을 해 보고 안 되면 **실패를 돌려준다** — 예제 5의 order-10이 그랬다.
- 페이지 단위 API를 쓰는 곳: 링버퍼처럼 페이지 크기 정렬이 필요한 버퍼, `vm_insert_page`/`remap_pfn_range`로 유저에게 mmap해 줄 버퍼, scatter-gather 목록의 원소.

---

## 6. slab 할당자 (SLUB) — 고정 크기 객체 풀

### 6.1 직관 — SSD 펌웨어의 버퍼 풀

NVMe 명령 컨텍스트 구조체를 매번 범용 힙에서 잡지 않고, 부팅 때 "명령 슬롯 256개" 배열을 만들어 free 목록으로 돌려 쓰던 것을 떠올리면 된다. 크기가 같으니 단편화가 없고, 할당·해제가 목록 머리에서 꺼내고 넣는 O(1)이다.

slab 할당자는 이것을 커널 전체에 일반화했다.

- **캐시** `struct kmem_cache`: "크기 X짜리 객체 풀" 하나. `task_struct`, `inode`, `dentry`, `skbuff_head_cache`처럼 커널의 주요 구조체마다 전용 캐시가 있다.
- **slab**: 캐시가 buddy에서 받아 온 페이지 묶음(order 0 이상). 그 안을 객체 크기로 잘라 쓴다. free 객체는 자기 몸 안에 "다음 free 객체 포인터"를 품는다(freelist) — Don이 펌웨어에서 쓰던 intrusive free list 그대로다.
- **per-CPU slab**: CPU마다 "지금 꺼내 쓰는 slab"을 하나씩 들고 있어서, 대부분의 할당·해제가 락 없이(인터럽트·선점만 막고 cmpxchg로) 끝난다.
- **partial 목록**: 일부만 찬 slab들의 목록(노드별). per-CPU slab이 다 차면 여기서 하나 가져온다. 여기도 없으면 buddy에서 새 slab을 받는다(`new_slab` → `alloc_pages`).
- **`kmalloc`**: 크기별 공용 캐시 묶음(`kmalloc-8`, `-16`, `-32`, `-64`, `-96`, `-128`, `-192`, `-256`, `-512`, `-1k`, `-2k`, `-4k`, `-8k`)에서 요청 크기를 덮는 가장 작은 캐시를 골라 꺼낸다. 8 KiB를 넘으면 slab을 거치지 않고 buddy로 간다.

Linux에는 한때 slab 구현이 셋(SLAB, SLUB, SLOB) 있었지만 SLOB은 6.4, SLAB은 6.8에서 제거됐다. 지금은 **SLUB 하나**다. 그래서 API 이름은 여전히 "slab"(`kmem_cache_*`, `/proc/slabinfo`, 부팅 인자 `slab_debug=`)이고 구현은 `mm/slub.c`다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">SLUB: 캐시 → slab(페이지) → 객체, 그리고 kmalloc 크기 등급</text>
  <rect x="20" y="36" width="380" height="58" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="56" text-anchor="middle" font-size="12">struct kmem_cache "f9_sample" (예제 8 실측)</text>
  <text x="210" y="78" text-anchor="middle" font-size="12">object_size 40 · objs_per_slab 102 · order 0</text>
  <rect x="20" y="114" width="180" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="110" y="134" text-anchor="middle" font-size="12">CPU 0: 현재 slab</text>
  <text x="110" y="153" text-anchor="middle" font-size="12">+ lockless freelist</text>
  <rect x="220" y="114" width="180" height="50" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="310" y="134" text-anchor="middle" font-size="12">CPU 1: 현재 slab</text>
  <text x="310" y="153" text-anchor="middle" font-size="12">+ lockless freelist</text>
  <rect x="20" y="184" width="380" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="210" y="209" text-anchor="middle" font-size="12">node 0 partial 목록 (일부만 찬 slab들, spinlock)</text>
  <line x1="110" y1="94" x2="110" y2="112" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="310" y1="94" x2="310" y2="112" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="210" y1="164" x2="210" y2="182" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="210" y1="224" x2="210" y2="246" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="218" y="240" font-size="12">없으면 buddy에서 새 slab</text>
  <rect x="20" y="250" width="380" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <rect x="20" y="250" width="36" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <rect x="56" y="250" width="36" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="92" y="250" width="36" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <rect x="128" y="250" width="36" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <rect x="164" y="250" width="36" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="270" y="277" text-anchor="middle" font-size="12">… 객체 102개 …</text>
  <rect x="384" y="250" width="16" height="44" fill="none" stroke="#888" stroke-dasharray="3 2"/>
  <path d="M74,294 C74,318 146,318 146,296" fill="none" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="210" y="324" text-anchor="middle" font-size="12">free 객체 안에 "다음 free" 포인터 (freelist)</text>
  <text x="210" y="346" text-anchor="middle" font-size="12">slab = 4 KiB 페이지 1장 = 40 B × 102 + 남는 16 B</text>
  <rect x="24" y="362" width="12" height="12" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="42" y="373" font-size="12">사용 중</text>
  <rect x="104" y="362" width="12" height="12" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="122" y="373" font-size="12">free</text>
  <rect x="430" y="36" width="230" height="330" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="545" y="56" text-anchor="middle" font-size="12">kmalloc 크기 등급 (예제 9 실측)</text>
  <text x="445" y="80" font-size="12">kmalloc-8</text><text x="560" y="80" font-size="12">1 … 8 B</text>
  <text x="445" y="100" font-size="12">kmalloc-16</text><text x="560" y="100" font-size="12">9 … 16</text>
  <text x="445" y="120" font-size="12">kmalloc-32</text><text x="560" y="120" font-size="12">17 … 32</text>
  <text x="445" y="140" font-size="12">kmalloc-64</text><text x="560" y="140" font-size="12">33 … 64</text>
  <text x="445" y="160" font-size="12">kmalloc-96</text><text x="560" y="160" font-size="12">65 … 96</text>
  <text x="445" y="180" font-size="12">kmalloc-128</text><text x="560" y="180" font-size="12">97 … 128</text>
  <text x="445" y="200" font-size="12">kmalloc-192</text><text x="560" y="200" font-size="12">129 … 192</text>
  <text x="445" y="220" font-size="12">kmalloc-256</text><text x="560" y="220" font-size="12">193 … 256</text>
  <text x="445" y="240" font-size="12">kmalloc-512</text><text x="560" y="240" font-size="12">257 … 512</text>
  <text x="445" y="260" font-size="12">kmalloc-1k … -8k (2배씩)</text>
  <line x1="440" y1="278" x2="650" y2="278" stroke="#888" stroke-dasharray="4 3"/>
  <text x="445" y="300" font-size="12">8 KiB 초과: slab 안 거침</text>
  <text x="445" y="320" font-size="12">→ buddy order 블록 직행</text>
  <text x="445" y="340" font-size="12">상한 KMALLOC_MAX_SIZE 4 MiB</text>
  <text x="340" y="404" text-anchor="middle" font-size="12">kmalloc(40)은 kmalloc-64에서 나온다 → 24 B 낭비. 전용 캐시는 40 B 그대로 쓴다</text>
</svg>
```

그림 7 — SLUB 구조. 왼쪽은 예제 8의 전용 캐시, 오른쪽은 예제 9에서 측정한 `kmalloc` 크기 등급이다. 대부분의 할당은 CPU별 freelist에서 락 없이 끝나고, 거기가 비었을 때만 partial 목록(락)과 buddy(더 비쌈)로 내려간다.

### 6.2 예제 8 — 센서 샘플용 전용 캐시 (`f9_cache`)

무엇을 확인하나: IMU 샘플 구조체용 `kmem_cache`를 만들고 1000개를 할당했을 때 `/proc/slabinfo`·`/sys/kernel/slab`에 어떻게 보이는지. 그리고 **slab merge**(크기·플래그가 같은 캐시를 하나로 합치는 기본 동작) 때문에 이름이 사라지는 현상.

```c
// SPDX-License-Identifier: GPL-2.0
/* f9_cache: 고정 크기 "센서 샘플" 객체용 kmem_cache */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/slab.h>

struct f9_sample {			/* IMU 샘플 하나 */
	u64 ts_ns;
	s32 accel[3];
	s32 gyro[3];
	u16 temp_cdeg;
	u8 sensor_id;
	u8 flags;
};

static bool nomerge;
static uint n = 1000;
module_param(nomerge, bool, 0444);
module_param(n, uint, 0444);

static struct kmem_cache *f9_cachep;
static struct f9_sample **objs;

static int __init f9_cache_init(void)
{
	uint i;

	f9_cachep = kmem_cache_create("f9_sample", sizeof(struct f9_sample), 0,
				      nomerge ? SLAB_NO_MERGE : 0, NULL);
	if (!f9_cachep)
		return -ENOMEM;
	objs = kcalloc(n, sizeof(*objs), GFP_KERNEL);
	if (!objs) {
		kmem_cache_destroy(f9_cachep);
		return -ENOMEM;
	}
	for (i = 0; i < n; i++)
		objs[i] = kmem_cache_zalloc(f9_cachep, GFP_KERNEL);
	pr_info("sizeof=%zu cache_size=%u obj[0]=%px obj[1]=%px obj[2]=%px\n",
		sizeof(struct f9_sample), kmem_cache_size(f9_cachep),
		objs[0], objs[1], objs[2]);
	return 0;
}

static void __exit f9_cache_exit(void)
{
	uint i;

	for (i = 0; i < n; i++)
		kmem_cache_free(f9_cachep, objs[i]);	/* NULL이면 무시 */
	kfree(objs);
	kmem_cache_destroy(f9_cachep);
}
module_init(f9_cache_init);
module_exit(f9_cache_exit);
MODULE_DESCRIPTION("F9: kmem_cache for fixed-size sensor samples");
MODULE_LICENSE("GPL");
```

```sh
# 게스트 안 (t.sh)
head -2 /proc/slabinfo
for m in 0 1; do
  echo "===== nomerge=$m"
  insmod /f9_cache.ko nomerge=$m
  dmesg | grep f9_cache: | tail -1
  grep -E "f9_sample" /proc/slabinfo || echo "(no f9_sample line in /proc/slabinfo)"
  ls -l /sys/kernel/slab | grep f9_sample | sed 's/.* f9_sample/f9_sample/'
  S=/sys/kernel/slab/f9_sample
  for a in object_size slab_size objs_per_slab order objects aliases; do echo "$a=$(cat $S/$a)"; done
  rmmod f9_cache
done
```

```text
lab initramfs: 6.18.55-dbg up 46.21s
slabinfo - version: 2.1
# name            <active_objs> <num_objs> <objsize> <objperslab> <pagesperslab> : tunables <limit> <batchcount> <sharedfactor> : slabdata <active_slabs> <num_slabs> <sharedavail>
===== nomerge=0
[   51.772596] f9_cache: sizeof=40 cache_size=40 obj[0]=ffff0000060d5000 obj[1]=ffff0000060d5028 obj[2]=ffff0000060d5050
(no f9_sample line in /proc/slabinfo)
f9_sample -> :0000040
object_size=40
slab_size=40
objs_per_slab=102
order=0
objects=1020 N0=1020
aliases=2
===== nomerge=1
[   55.713288] f9_cache: sizeof=40 cache_size=40 obj[0]=ffff000006104000 obj[1]=ffff000006104028 obj[2]=ffff000006104050
f9_sample           1020   1020     40  102    1 : tunables    0    0    0 : slabdata     10     10      0
f9_sample
object_size=40
slab_size=40
objs_per_slab=102
order=0
objects=1020 N0=1020
aliases=0
```

출력에서 볼 것:

- 구조체는 8 + 12 + 12 + 2 + 1 + 1 = 36 B인데 `sizeof=40` — `u64` 정렬 때문에 끝에 4 B 패딩이 붙었다. 객체 주소가 `…5000`, `…5028`, `…5050`으로 정확히 0x28(40) 간격이다. 페이지 정렬된 slab의 첫 객체부터 빈틈없이 깔린다.
- `objs_per_slab=102`, `order=0`: 4 KiB 페이지 하나에 40 B × 102 = 4080 B, 16 B만 남는다. 1000개를 잡으니 slab 10장, 객체 1020개(마지막 slab에 20개 여유).
- **`nomerge=0`(기본)**: `/proc/slabinfo`에 `f9_sample` 줄이 **없다.** sysfs에서는 `f9_sample -> :0000040` 심볼릭 링크다. 크기 40, 같은 플래그인 기존 캐시와 합쳐져서(`aliases=2`) 이름 없는 공용 캐시 `:0000040`의 별칭이 된 것이다. 메모리는 아끼지만 "내 드라이버가 객체를 몇 개 쓰고 있나"를 이름으로 볼 수 없다.
- **`SLAB_NO_MERGE`**(6.5부터): 독립 캐시가 되어 `/proc/slabinfo`에 `f9_sample 1020 1020 40 102 1`(활성 객체, 전체 객체, 객체 크기, slab당 객체, slab당 페이지)로 보인다. 부팅 인자 `slab_nomerge`로 전체 merge를 끌 수도 있고, `slab_debug`로 디버그 플래그가 붙은 캐시도 merge되지 않는다.
- 전용 캐시의 이점은 메모리만이 아니다: 객체 수를 이름으로 추적하고(누수 확인), 생성자(ctor)로 초기 상태를 보장하고, `SLAB_HWCACHE_ALIGN`으로 캐시 라인 정렬을 받고, `SLAB_TYPESAFE_BY_RCU` 같은 수명 정책을 줄 수 있다.

### 6.3 예제 9 — kmalloc 크기 등급과 ksize, slabtop처럼 보기 (`f9_ksize`)

무엇을 확인하나: `kmalloc(n)`이 실제로 몇 바이트를 주는지(`ksize()`), 그 경계가 어디인지, 그리고 시스템 전체에서 어느 캐시가 메모리를 가장 많이 쓰는지(BusyBox에는 `slabtop`이 없어서 `awk`로 흉내).

```c
// SPDX-License-Identifier: GPL-2.0
/* f9_ksize: kmalloc(n)이 실제로 몇 바이트를 주는가 (size class 반올림) */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/slab.h>

static const size_t req[] = {
	1, 8, 9, 16, 17, 24, 32, 33, 40, 64, 65, 96, 97, 128, 129, 192, 193,
	256, 257, 300, 512, 513, 1000, 1024, 1025, 2048, 2049, 4096, 4097,
	8192, 8193, 12288, 16384, 16385, 100000,
};

static int __init f9_ksize_init(void)
{
	int i;

	pr_info("KMALLOC_MIN_SIZE=%d KMALLOC_SHIFT_HIGH=%d (max slab size %lu)\n",
		KMALLOC_MIN_SIZE, KMALLOC_SHIFT_HIGH, 1UL << KMALLOC_SHIFT_HIGH);
	for (i = 0; i < ARRAY_SIZE(req); i++) {
		void *p = kmalloc(req[i], GFP_KERNEL);
		size_t got;

		if (!p)
			return -ENOMEM;
		got = ksize(p);
		pr_info("kmalloc(%6zu) -> ksize %6zu  waste %3zu%%  %s\n",
			req[i], got, (got - req[i]) * 100 / got,
			got > (1UL << KMALLOC_SHIFT_HIGH) ? "(page allocator)" : "");
		kfree(p);
	}
	return 0;
}

static void __exit f9_ksize_exit(void) { }
module_init(f9_ksize_init);
module_exit(f9_ksize_exit);
MODULE_DESCRIPTION("F9: kmalloc size classes via ksize()");
MODULE_LICENSE("GPL");
```

```sh
# 게스트 안 (t.sh)
insmod /f9_ksize.ko
dmesg | grep f9_ksize: | sed 's/^\[[ 0-9.]*\] //'
echo "--- kmalloc caches in /proc/slabinfo (name objsize objperslab pagesperslab)"
awk '/^kmalloc-[0-9]/ {printf "%-14s %5s %4s %3s\n", $1, $4, $5, $6}' /proc/slabinfo
echo "--- top 8 caches by memory (slabtop-like, KiB)"
awk 'NR>2 {kb=$15*$6*4; printf "%7d %-24s objs=%s size=%s\n", kb, $1, $3, $4}' /proc/slabinfo | sort -rn | head -8
echo "--- aliases of :0000040"
insmod /f9_cache.ko
ls -l /sys/kernel/slab | grep -E ":0000040" | awk '{print $9, $10, $11}'
```

```text
lab initramfs: 6.18.55-dbg up 11.00s
f9_ksize: KMALLOC_MIN_SIZE=8 KMALLOC_SHIFT_HIGH=13 (max slab size 8192)
f9_ksize: kmalloc(     1) -> ksize      8  waste  87%  
f9_ksize: kmalloc(     8) -> ksize      8  waste   0%  
f9_ksize: kmalloc(     9) -> ksize     16  waste  43%  
f9_ksize: kmalloc(    16) -> ksize     16  waste   0%  
f9_ksize: kmalloc(    17) -> ksize     32  waste  46%  
f9_ksize: kmalloc(    24) -> ksize     32  waste  25%  
f9_ksize: kmalloc(    32) -> ksize     32  waste   0%  
f9_ksize: kmalloc(    33) -> ksize     64  waste  48%  
f9_ksize: kmalloc(    40) -> ksize     64  waste  37%  
f9_ksize: kmalloc(    64) -> ksize     64  waste   0%  
f9_ksize: kmalloc(    65) -> ksize     96  waste  32%  
f9_ksize: kmalloc(    96) -> ksize     96  waste   0%  
f9_ksize: kmalloc(    97) -> ksize    128  waste  24%  
f9_ksize: kmalloc(   128) -> ksize    128  waste   0%  
f9_ksize: kmalloc(   129) -> ksize    192  waste  32%  
f9_ksize: kmalloc(   192) -> ksize    192  waste   0%  
f9_ksize: kmalloc(   193) -> ksize    256  waste  24%  
f9_ksize: kmalloc(   256) -> ksize    256  waste   0%  
f9_ksize: kmalloc(   257) -> ksize    512  waste  49%  
f9_ksize: kmalloc(   300) -> ksize    512  waste  41%  
f9_ksize: kmalloc(   512) -> ksize    512  waste   0%  
f9_ksize: kmalloc(   513) -> ksize   1024  waste  49%  
f9_ksize: kmalloc(  1000) -> ksize   1024  waste   2%  
f9_ksize: kmalloc(  1024) -> ksize   1024  waste   0%  
f9_ksize: kmalloc(  1025) -> ksize   2048  waste  49%  
f9_ksize: kmalloc(  2048) -> ksize   2048  waste   0%  
f9_ksize: kmalloc(  2049) -> ksize   4096  waste  49%  
f9_ksize: kmalloc(  4096) -> ksize   4096  waste   0%  
f9_ksize: kmalloc(  4097) -> ksize   8192  waste  49%  
f9_ksize: kmalloc(  8192) -> ksize   8192  waste   0%  
f9_ksize: kmalloc(  8193) -> ksize  16384  waste  49%  (page allocator)
f9_ksize: kmalloc( 12288) -> ksize  16384  waste  25%  (page allocator)
f9_ksize: kmalloc( 16384) -> ksize  16384  waste   0%  (page allocator)
f9_ksize: kmalloc( 16385) -> ksize  32768  waste  49%  (page allocator)
f9_ksize: kmalloc(100000) -> ksize 131072  waste  23%  (page allocator)
--- kmalloc caches in /proc/slabinfo (name objsize objperslab pagesperslab)
kmalloc-8k      8192    4   8
kmalloc-4k      4096    8   8
kmalloc-2k      2048   16   8
kmalloc-1k      1024   16   4
kmalloc-512      512   16   2
kmalloc-256      256   16   1
kmalloc-128      128   32   1
kmalloc-64        64   64   1
kmalloc-32        32  128   1
kmalloc-16        16  256   1
kmalloc-8          8  512   1
kmalloc-192      192   21   1
kmalloc-96        96   42   1
--- top 8 caches by memory (slabtop-like, KiB)
   3124 kernfs_node_cache        objs=17182 size=184
    960 shmem_inode_cache        objs=600 size=1608
    736 task_struct              objs=69 size=8384
    608 kmalloc-2k               objs=304 size=2048
    576 debugfs_inode_cache      objs=468 size=1232
    404 dentry                   objs=1212 size=328
    360 kmalloc-256              objs=1440 size=256
    352 names_cache              objs=88 size=4096
--- aliases of :0000040
:0000040  
ext4_system_zone -> :0000040
f9_sample -> :0000040
```

출력에서 볼 것:

- 등급은 2의 거듭제곱이 기본이고 **96과 192가 끼어 있다**(65~96 B, 129~192 B 요청이 많아서 낭비를 줄이려고). 최소 등급은 `kmalloc-8` — arm64는 6.5부터 `ARCH_KMALLOC_MINALIGN`이 8이다. 그 전에는 비코히어런트 DMA 안전을 위해 캐시 라인(128 B) 정렬이 최소였고 `kmalloc(1)`이 128 B를 먹었다. 6.5 이후 작은 `kmalloc` 버퍼를 DMA에 쓰면 필요할 때 swiotlb bounce buffer로 처리한다 — 그래서 **작은 kmalloc 버퍼를 DMA에 직접 쓰는 설계는 피한다**(H8).
- 최악의 낭비는 2의 거듭제곱 + 1 바이트(`257 → 512`, `4097 → 8192`)에서 ~50%다. 구조체 크기가 등급 경계를 살짝 넘으면 메모리가 두 배로 든다. **센서 샘플(40 B)을 `kmalloc`으로 잡으면 64 B 등급에서 37%가 버려진다**(그림 8). 전용 캐시(예제 8)는 40 B를 그대로 쓴다: 페이지당 102개 vs `kmalloc-64`의 64개.
- `8193` 이상은 `(page allocator)` — `ksize`가 16384, 32768, 131072로 **order 단위**(2^order 페이지)로 반올림된다. `kmalloc(100000)`은 order-5(128 KiB) 블록 하나를 통째로 받는다.
- `kmalloc-8k`는 slab당 8 페이지(order 3)를 쓴다. 큰 객체 캐시는 slab 하나에 객체 여러 개를 담으려고 높은 order 페이지를 받는다 — 그래서 단편화가 심하면 slab 확장 자체가 실패할 수 있다(그때는 더 낮은 order로 재시도한다).
- slabtop 흉내: 이 작은 게스트에서는 sysfs/kernfs 노드(`kernfs_node_cache` 3 MiB)가 1위다. 실제 장비에서 메모리 누수를 찾을 때 이 목록을 시간 간격을 두고 두 번 찍어 **계속 커지는 캐시**를 찾는다. `kmalloc-*`가 커지면 누가 그 크기로 잡는지는 §9의 도구로 좁힌다.
- `:0000040`의 별칭은 `ext4_system_zone`과 `f9_sample`이다 — 예제 8의 merge 상대가 ext4 내부 캐시였다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="22" text-anchor="middle" font-size="13" font-weight="bold">kmalloc(n) 낭비율 = (ksize − n) / ksize — 예제 9 실측</text>
<line x1="60" y1="250.0" x2="660" y2="250.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="54" y="254.0" text-anchor="end" font-size="12">0%</text>
<line x1="60" y1="200.0" x2="660" y2="200.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="54" y="204.0" text-anchor="end" font-size="12">25%</text>
<line x1="60" y1="150.0" x2="660" y2="150.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="54" y="154.0" text-anchor="end" font-size="12">50%</text>
<line x1="60" y1="100.0" x2="660" y2="100.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="54" y="104.0" text-anchor="end" font-size="12">75%</text>
<line x1="60" y1="50.0" x2="660" y2="50.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="54" y="54.0" text-anchor="end" font-size="12">100%</text>
<line x1="60" y1="250" x2="660" y2="250" stroke="currentColor"/><line x1="60" y1="50" x2="60" y2="250" stroke="currentColor"/>
<rect x="64.3" y="76.0" width="20" height="174.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="74.3" y="72.0" text-anchor="middle" font-size="11">87</text>
<text x="74.3" y="264" text-anchor="end" font-size="11" transform="rotate(-45 74.3 264)">1</text>
<rect x="92.9" y="164.0" width="20" height="86.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="102.9" y="160.0" text-anchor="middle" font-size="11">43</text>
<text x="102.9" y="264" text-anchor="end" font-size="11" transform="rotate(-45 102.9 264)">9</text>
<rect x="121.4" y="158.0" width="20" height="92.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="131.4" y="154.0" text-anchor="middle" font-size="11">46</text>
<text x="131.4" y="264" text-anchor="end" font-size="11" transform="rotate(-45 131.4 264)">17</text>
<rect x="150.0" y="200.0" width="20" height="50.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="160.0" y="196.0" text-anchor="middle" font-size="11">25</text>
<text x="160.0" y="264" text-anchor="end" font-size="11" transform="rotate(-45 160.0 264)">24</text>
<rect x="178.6" y="154.0" width="20" height="96.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="188.6" y="150.0" text-anchor="middle" font-size="11">48</text>
<text x="188.6" y="264" text-anchor="end" font-size="11" transform="rotate(-45 188.6 264)">33</text>
<rect x="207.1" y="176.0" width="20" height="74.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="217.1" y="172.0" text-anchor="middle" font-size="11">37</text>
<text x="217.1" y="264" text-anchor="end" font-size="11" transform="rotate(-45 217.1 264)">40</text>
<rect x="235.7" y="186.0" width="20" height="64.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="245.7" y="182.0" text-anchor="middle" font-size="11">32</text>
<text x="245.7" y="264" text-anchor="end" font-size="11" transform="rotate(-45 245.7 264)">65</text>
<rect x="264.3" y="202.0" width="20" height="48.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="274.3" y="198.0" text-anchor="middle" font-size="11">24</text>
<text x="274.3" y="264" text-anchor="end" font-size="11" transform="rotate(-45 274.3 264)">97</text>
<rect x="292.9" y="186.0" width="20" height="64.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="302.9" y="182.0" text-anchor="middle" font-size="11">32</text>
<text x="302.9" y="264" text-anchor="end" font-size="11" transform="rotate(-45 302.9 264)">129</text>
<rect x="321.4" y="202.0" width="20" height="48.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="331.4" y="198.0" text-anchor="middle" font-size="11">24</text>
<text x="331.4" y="264" text-anchor="end" font-size="11" transform="rotate(-45 331.4 264)">193</text>
<rect x="350.0" y="152.0" width="20" height="98.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="360.0" y="148.0" text-anchor="middle" font-size="11">49</text>
<text x="360.0" y="264" text-anchor="end" font-size="11" transform="rotate(-45 360.0 264)">257</text>
<rect x="378.6" y="168.0" width="20" height="82.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="388.6" y="164.0" text-anchor="middle" font-size="11">41</text>
<text x="388.6" y="264" text-anchor="end" font-size="11" transform="rotate(-45 388.6 264)">300</text>
<rect x="407.1" y="152.0" width="20" height="98.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="417.1" y="148.0" text-anchor="middle" font-size="11">49</text>
<text x="417.1" y="264" text-anchor="end" font-size="11" transform="rotate(-45 417.1 264)">513</text>
<rect x="435.7" y="246.0" width="20" height="4.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="445.7" y="242.0" text-anchor="middle" font-size="11">2</text>
<text x="445.7" y="264" text-anchor="end" font-size="11" transform="rotate(-45 445.7 264)">1000</text>
<rect x="464.3" y="152.0" width="20" height="98.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="474.3" y="148.0" text-anchor="middle" font-size="11">49</text>
<text x="474.3" y="264" text-anchor="end" font-size="11" transform="rotate(-45 474.3 264)">1025</text>
<rect x="492.9" y="152.0" width="20" height="98.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="502.9" y="148.0" text-anchor="middle" font-size="11">49</text>
<text x="502.9" y="264" text-anchor="end" font-size="11" transform="rotate(-45 502.9 264)">2049</text>
<rect x="521.4" y="152.0" width="20" height="98.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="531.4" y="148.0" text-anchor="middle" font-size="11">49</text>
<text x="531.4" y="264" text-anchor="end" font-size="11" transform="rotate(-45 531.4 264)">4097</text>
<rect x="550.0" y="152.0" width="20" height="98.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="560.0" y="148.0" text-anchor="middle" font-size="11">49</text>
<text x="560.0" y="264" text-anchor="end" font-size="11" transform="rotate(-45 560.0 264)">8193</text>
<rect x="578.6" y="200.0" width="20" height="50.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="588.6" y="196.0" text-anchor="middle" font-size="11">25</text>
<text x="588.6" y="264" text-anchor="end" font-size="11" transform="rotate(-45 588.6 264)">12288</text>
<rect x="607.1" y="152.0" width="20" height="98.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="617.1" y="148.0" text-anchor="middle" font-size="11">49</text>
<text x="617.1" y="264" text-anchor="end" font-size="11" transform="rotate(-45 617.1 264)">16385</text>
<rect x="635.7" y="204.0" width="20" height="46.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="645.7" y="200.0" text-anchor="middle" font-size="11">23</text>
<text x="645.7" y="264" text-anchor="end" font-size="11" transform="rotate(-45 645.7 264)">100k</text>
<text x="360" y="306" text-anchor="middle" font-size="12">요청 크기 n (바이트)</text>
<rect x="170" y="320" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="188" y="330" font-size="12">slab (kmalloc-8 … kmalloc-8k)</text>
<rect x="420" y="320" width="12" height="12" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/><text x="438" y="330" font-size="12">페이지 할당자 (8 KiB 초과)</text>
</svg>
```

그림 8 — 예제 9에서 측정한 `kmalloc` 낭비율. 등급 경계 바로 위(9, 17, 33, 257, 513, 1025, …)에서 낭비가 50%에 가깝다. 파랑은 slab 등급, 주황은 8 KiB를 넘어 페이지 할당자로 간 요청이다.

### 6.4 함정

- **`/proc/slabinfo`에서 내 캐시가 안 보인다** → merge됐다. sysfs 심볼릭 링크로 확인하거나 `SLAB_NO_MERGE`/`slab_nomerge`.
- 모듈 언로드 때 `kmem_cache_destroy` 전에 객체를 전부 돌려주지 않으면 "Objects remaining … on __kmem_cache_shutdown()" 경고가 뜬다(이 노트에서 실행하지 않음 — 메시지 문구는 `mm/slub.c` 기준). `kmem_cache_free`에 다른 캐시의 객체를 넘기는 실수도 디버그 옵션에서 잡힌다.
- `ksize()`로 얻은 "여분"을 쓰는 건 금지에 가깝다. 6.x에서는 `ksize`가 KASAN 등과 충돌해서 "할당 크기를 키우려면 `kmalloc_size_roundup()`으로 미리 반올림해서 요청하라"는 방향으로 바뀌었다. 이 노트는 관찰용으로만 썼다.

---

## 7. kmalloc vs kzalloc vs kvmalloc vs vmalloc vs alloc_pages — 고르는 법

| API | 물리 연속 | 최대 크기 | atomic 문맥 | DMA에 바로? | 해제 |
|---|---|---|---|---|---|
| `kmalloc(size, gfp)` | 예 | `KMALLOC_MAX_SIZE` 4 MiB (이 커널) | `GFP_ATOMIC`이면 가능 | 스트리밍 매핑(`dma_map_single`)으로는 가능. 작은 크기는 bounce 가능성 | `kfree` |
| `kzalloc` / `kcalloc(n, size)` / `kmalloc_array` | 예 | 같음 | 같음 | 같음 | `kfree` |
| `kmem_cache_alloc(cache, gfp)` | 예 (객체 단위) | 캐시 객체 크기 | 같음 | 같음 | `kmem_cache_free` |
| `alloc_pages(gfp, order)` | 예 | order 10 = 4 MiB | `GFP_ATOMIC`이면 가능 (큰 order는 거의 실패) | `dma_map_page`로 가능 | `__free_pages` |
| `vmalloc(size)` | **아니오** | vmalloc 영역·RAM 한도 | **불가** (내부에서 잠들 수 있다) | **불가** — 페이지별 SG 목록 필요 | `vfree` |
| `kvmalloc(size, gfp)` | 크기에 따라 다름 | 큼 | 불가 (`GFP_KERNEL` 계열 전용) | 불가 (어느 쪽인지 모름) | `kvfree` |
| `devm_kzalloc(dev, size, gfp)` | 예 | `kmalloc`과 같음 | 보통 `probe()`에서만 | `kmalloc`과 같음 | 자동 (드라이버 detach 때) |
| `dma_alloc_coherent(dev, size, &dma, gfp)` | 장치 기준 연속 | CMA·IOMMU에 따라 | 보통 아니오 (`GFP_ATOMIC`이면 작은 atomic pool에서) | **예 — 이것이 DMA용** | `dma_free_coherent` |

고르는 규칙(드라이버 기준):

1. 구조체 하나, 작은 버퍼 → `kzalloc`(0 초기화 기본으로). 드라이버의 장치별 상태 구조체는 `devm_kzalloc`.
2. 같은 크기 객체를 대량·고빈도로 → `kmem_cache`(예제 8). 패킷 디스크립터, 샘플 노드, 요청 구조체.
3. 배열 → `kcalloc(n, size, gfp)`/`kmalloc_array`. `kmalloc(n * size)`는 곱셈 오버플로를 검사하지 않는다.
4. 크고(수십 KiB 이상) CPU만 접근하는 테이블(룩업 테이블, 펌웨어 이미지 사본) → `kvmalloc`. 실패에 강하고 작을 땐 빠르다.
5. 장치가 접근하는 버퍼 → GFP·`kmalloc`이 아니라 DMA API(H8), 큰 연속 버퍼는 CMA(F10).
6. 유저에게 mmap해 줄 페이지 → `alloc_pages`(또는 `vmalloc_user` + `remap_vmalloc_range`).

`devm_kzalloc` 패턴 — 이 노트에서는 실행하지 않았다(플랫폼 드라이버 골격이 필요해서 G6에서 실제로 돌린다):

```c
/* 실행하지 않음 — G6 노트에서 실제 platform driver로 확인 */
static int imu_probe(struct platform_device *pdev)
{
	struct imu_priv *priv;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;		/* 경고는 할당자가 이미 찍는다 */
	priv->ring = devm_kcalloc(&pdev->dev, 1024, sizeof(*priv->ring), GFP_KERNEL);
	if (!priv->ring)
		return -ENOMEM;		/* priv는 devres가 알아서 해제 */
	platform_set_drvdata(pdev, priv);
	return 0;			/* remove()에서 kfree 불필요 */
}
```

devres는 장치에 "해제할 것 목록"을 달아 두었다가 `probe()` 실패나 드라이버 분리 때 역순으로 풀어 준다. 펌웨어의 "에러 경로마다 goto로 되감기"를 프레임워크가 대신 하는 것이다. 단, **장치보다 오래 살아야 하는 메모리**(예: 열린 파일이 계속 참조하는 버퍼)를 devm으로 잡으면 장치가 사라질 때 해제되어 use-after-free가 된다.

---

## 8. GFP 플래그와 메모리 압박 — "이 할당을 위해 무엇까지 해도 되나"

### 8.1 직관

할당자가 free 페이지가 부족할 때 할 수 있는 일은 여러 가지다: kswapd(백그라운드 회수 스레드)를 깨우기, **직접 회수**(direct reclaim — 페이지 캐시를 버리거나 dirty 페이지를 디스크에 쓰고 버리기), **compaction**(페이지를 옮겨 큰 블록 만들기), 비상 예비분 쓰기, OOM killer 부르기. 이 중 일부는 **잠들어야** 하고(디스크 I/O를 기다림), 일부는 **재귀 위험**이 있다(파일시스템이 메모리를 할당하다가 회수가 같은 파일시스템을 다시 부르면 데드락). GFP 플래그는 호출자가 이 중 무엇을 허락하는지 비트로 알리는 것이다.

6.18 기준 주요 비트와 조합(`include/linux/gfp_types.h`):

| 비트 | 값 | 뜻 |
|---|---|---|
| `__GFP_DMA` | 0x1 | `ZONE_DMA`에서만 |
| `__GFP_DMA32` | 0x4 | `ZONE_DMA32` 이하에서만 |
| `__GFP_MOVABLE` | 0x8 | 옮길 수 있는 페이지(유저 페이지 등) — CMA·ZONE_MOVABLE 사용 가능 |
| `__GFP_HIGH` | 0x20 | 높은 우선순위 — min 워터마크 아래 예비분 일부 사용 |
| `__GFP_IO` | 0x40 | 회수 중 블록 I/O 시작 가능 |
| `__GFP_FS` | 0x80 | 회수 중 파일시스템 호출 가능 |
| `__GFP_ZERO` | 0x100 | 0으로 채워 반환 |
| `__GFP_DIRECT_RECLAIM` | 0x400 | 직접 회수 가능 = **잠들 수 있음** |
| `__GFP_KSWAPD_RECLAIM` | 0x800 | low 워터마크 아래면 kswapd 깨움 |
| `__GFP_NOWARN` | 0x2000 | 실패해도 `page allocation failure` 경고 안 찍음 |
| `__GFP_RETRY_MAYFAIL` | 0x4000 | 열심히 재시도하되 OOM killer는 안 부르고 실패 반환 |
| `__GFP_NOFAIL` | 0x8000 | 절대 실패 안 함(무한 재시도) — 새 코드에서 지양 |
| `__GFP_NORETRY` | 0x10000 | 가볍게 한 번 시도하고 실패 반환 |

| 조합 | 구성 | 값 | 쓰는 곳 |
|---|---|---|---|
| `GFP_KERNEL` | DIRECT_RECLAIM + KSWAPD_RECLAIM + IO + FS | 0xcc0 | process context 기본값 — `probe()`, `open()`, `ioctl()`, workqueue |
| `GFP_NOFS` | KERNEL에서 FS 뺌 | 0xc40 | 파일시스템 내부 |
| `GFP_NOIO` | KERNEL에서 IO·FS 뺌 | 0xc00 | 블록 드라이버의 I/O 경로(스왑·쓰기 처리 중) |
| `GFP_ATOMIC` | HIGH + KSWAPD_RECLAIM | 0x820 | IRQ 핸들러, softirq, spinlock 보유 중 — 잠들 수 없고 실패 시 손해가 큼 |
| `GFP_NOWAIT` | KSWAPD_RECLAIM + NOWARN | 0x2800 | 잠들 수 없지만 실패해도 괜찮은 기회성 할당 |
| `GFP_DMA32` | `__GFP_DMA32` | 0x4 | 32비트 주소 제한 장치 (보통 DMA API가 알아서 고름) |

`GFP_NOWAIT`에 `__GFP_NOWARN`이 포함된 것은 최근(6.x 후반) 변경이다 — 예전 커널에서는 `GFP_NOWAIT` = `__GFP_KSWAPD_RECLAIM`만이었다. 이 노트의 값은 랩 6.18.55 소스에서 직접 계산했고, 예제 5·12의 커널 출력(`mode:0xcc0(GFP_KERNEL)`, `mask 0xdc0(GFP_KERNEL|__GFP_ZERO)`)과 일치한다.

### 8.2 워터마크 — 언제 무엇이 일어나나

예제 1의 zone 워터마크(min 665, low 831, high 997 페이지)는 `vm.min_free_kbytes`(여기서는 min × 4 KiB = 2660 kB)에서 계산된다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">zone 워터마크와 GFP별 바닥 (non-CMA free 페이지, 이 게스트 실측·계산)</text>
  <line x1="200" y1="40" x2="200" y2="320" stroke="currentColor"/>
  <text x="192" y="44" text-anchor="end" font-size="12">free 많음</text>
  <text x="192" y="324" text-anchor="end" font-size="12">0</text>
  <line x1="200" y1="80" x2="480" y2="80" stroke="#3f9a6b"/>
  <text x="192" y="84" text-anchor="end" font-size="12">high 997</text>
  <line x1="200" y1="130" x2="480" y2="130" stroke="#e08a3c"/>
  <text x="192" y="134" text-anchor="end" font-size="12">low 831</text>
  <line x1="200" y1="180" x2="480" y2="180" stroke="#d0564a"/>
  <text x="192" y="184" text-anchor="end" font-size="12">min 665</text>
  <line x1="200" y1="240" x2="480" y2="240" stroke="#d0564a" stroke-dasharray="5 3"/>
  <text x="192" y="244" text-anchor="end" font-size="12">333 (min/2)</text>
  <line x1="200" y1="268" x2="480" y2="268" stroke="#d0564a" stroke-dasharray="2 3"/>
  <text x="192" y="272" text-anchor="end" font-size="12">250 (ATOMIC)</text>
  <text x="210" y="62" font-size="12">fast path: 그냥 꺼내 준다</text>
  <text x="210" y="110" font-size="12">kswapd가 high까지 회수하고 잠든다</text>
  <text x="210" y="160" font-size="12">low 아래: kswapd 깨움 (모든 요청)</text>
  <text x="210" y="212" font-size="12">min 아래: 일반 요청은 여기서 막힘</text>
  <text x="210" y="258" font-size="12">__GFP_HIGH: min/2까지</text>
  <text x="210" y="290" font-size="12">GFP_ATOMIC: min/2 − min/8 까지</text>
  <rect x="495" y="150" width="175" height="66" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="505" y="170" font-size="12">GFP_KERNEL은 min에서</text>
  <text x="505" y="188" font-size="12">멈추지 않고 직접 회수·</text>
  <text x="505" y="206" font-size="12">compaction 후 재시도</text>
  <rect x="495" y="236" width="175" height="50" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="505" y="256" font-size="12">예제 10 실측: ATOMIC은</text>
  <text x="505" y="274" font-size="12">≈ 246에서 실패</text>
  <text x="340" y="346" text-anchor="middle" font-size="12">CMA free(7936 페이지)는 unmovable 요청에는 "없는 것"으로 친다</text>
</svg>
```

그림 9 — zone 워터마크(축척 무시). `__zone_watermark_ok()`(`mm/page_alloc.c`)는 `__GFP_HIGH`면 min의 절반을, 거기에 잠들 수 없는 요청(`GFP_ATOMIC`)이면 다시 1/4을 깎는다: 665 → 333 → 250. 예제 10에서 `GFP_ATOMIC`이 실제로 이 근처(≈246)까지 내려갔다.

### 8.3 예제 10 — 같은 할당, 다른 GFP: NOWAIT vs ATOMIC vs KERNEL (`f9_gfp`)

무엇을 확인하나: order-0 페이지를 플래그만 바꿔 실패할 때까지(또는 상한까지) 잡으면 몇 장을 받는지. (A) 회수할 것이 없는 상태와 (B) 깨끗한 버퍼 캐시 300 MiB가 있는 상태에서 비교한다. B 상태는 300 MiB 가상 디스크(`DISK=/var/tmp/F9/disk.img`, `truncate -s 300M`)를 `dd`로 읽어 만든다.

```c
// SPDX-License-Identifier: GPL-2.0
/* f9_gfp: 같은 order-0 할당을 GFP 플래그만 바꿔 실패할 때까지 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/mm.h>
#include <linux/vmstat.h>
#include <linux/vmalloc.h>
#include <linux/ktime.h>

static char *flag = "nowait";
static uint max = 200000;
module_param(flag, charp, 0444);
module_param(max, uint, 0444);

static int __init f9_gfp_init(void)
{
	gfp_t gfp = !strcmp(flag, "atomic") ? GFP_ATOMIC :
		    !strcmp(flag, "kernel") ? GFP_KERNEL : GFP_NOWAIT;
	unsigned long free0 = global_zone_page_state(NR_FREE_PAGES);
	struct page **pg = vmalloc(max * sizeof(*pg));
	u64 t0 = ktime_get_ns();
	uint n;

	if (!pg)
		return -ENOMEM;
	for (n = 0; n < max; n++) {
		pg[n] = alloc_page(gfp | __GFP_NOWARN);
		if (!pg[n])
			break;
	}
	pr_info("%-6s gfp=%#x: got %6u pages (%3u MiB) in %4llu ms, free %lu -> %lu pages\n",
		flag, (__force unsigned int)gfp, n, n >> 8,
		(ktime_get_ns() - t0) / 1000000, free0,
		global_zone_page_state(NR_FREE_PAGES));
	while (n--)
		__free_page(pg[n]);
	vfree(pg);
	return 0;
}

static void __exit f9_gfp_exit(void) { }
module_init(f9_gfp_init);
module_exit(f9_gfp_exit);
MODULE_DESCRIPTION("F9: GFP_NOWAIT vs GFP_ATOMIC vs GFP_KERNEL");
MODULE_LICENSE("GPL");
```

```sh
# 게스트 안 (t.sh) — QEMU에 -drive file=disk.img,if=virtio 를 붙여 부팅
M() { grep -E "^(MemFree|Buffers):" /proc/meminfo | tr -s ' ' | tr '\n' ' '; echo; }
G() { insmod /f9_gfp.ko flag=$1 max=${2:-200000}; rmmod f9_gfp; dmesg | grep "f9_gfp:" | tail -1 | sed 's/^\[[ 0-9.]*\] //'; }
grep -E "^ +(min|low|high) " /proc/zoneinfo | head -3 | tr -s ' ' | tr '\n' ' '; echo
echo "== A. page cache empty"; M
G nowait; G atomic
echo "== B. after reading 300 MiB of /dev/vda (clean buffer cache)"
exec 3</dev/vda   # 열어 둔 채로 유지: 마지막 close 때 bdev 캐시가 버려지지 않게
dd if=/dev/vda of=/dev/null bs=1M 2>/dev/null; M
G nowait; M
dd if=/dev/vda of=/dev/null bs=1M 2>/dev/null; M
G atomic; M
dd if=/dev/vda of=/dev/null bs=1M 2>/dev/null; M
G kernel 90000; M
grep -E "^(pgsteal_kswapd|pgsteal_direct|allocstall_dma|compact_stall) " /proc/vmstat
```

```text
lab initramfs: 6.18.55-dbg up 14.15s
 min 665  low 831  high 997 
== A. page cache empty
MemFree: 416136 kB Buffers: 0 kB 
f9_gfp: nowait gfp=0x2800: got  93123 pages (363 MiB) in 4574 ms, free 104027 -> 9132 pages
f9_gfp: atomic gfp=0x820: got  95192 pages (371 MiB) in 7062 ms, free 100485 -> 8182 pages
== B. after reading 300 MiB of /dev/vda (clean buffer cache)
MemFree: 89832 kB Buffers: 307200 kB 
f9_gfp: nowait gfp=0x2800: got  19831 pages ( 77 MiB) in 1160 ms, free 22455 -> 7779 pages
MemFree: 114900 kB Buffers: 306088 kB 
MemFree: 113412 kB Buffers: 307200 kB 
f9_gfp: atomic gfp=0x820: got  19111 pages ( 74 MiB) in 1514 ms, free 28386 -> 7623 pages
MemFree: 99644 kB Buffers: 307200 kB 
MemFree: 99584 kB Buffers: 307200 kB 
f9_gfp: kernel gfp=0xcc0: got  90000 pages (351 MiB) in 8710 ms, free 25019 -> 9154 pages
MemFree: 377552 kB Buffers: 25688 kB 
allocstall_dma 0
pgsteal_kswapd 46583
pgsteal_direct 24073
compact_stall 0
```

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="22" text-anchor="middle" font-size="13" font-weight="bold">실패(또는 상한)까지 받은 order-0 페이지 (MiB) — 예제 10 실측</text>
<line x1="70" y1="250.0" x2="660" y2="250.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="64" y="254.0" text-anchor="end" font-size="12">0</text>
<line x1="70" y1="200.0" x2="660" y2="200.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="64" y="204.0" text-anchor="end" font-size="12">100</text>
<line x1="70" y1="150.0" x2="660" y2="150.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="64" y="154.0" text-anchor="end" font-size="12">200</text>
<line x1="70" y1="100.0" x2="660" y2="100.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="64" y="104.0" text-anchor="end" font-size="12">300</text>
<line x1="70" y1="50.0" x2="660" y2="50.0" stroke="#888" stroke-opacity="0.35" stroke-dasharray="3 3"/>
<text x="64" y="54.0" text-anchor="end" font-size="12">400</text>
<line x1="70" y1="250" x2="660" y2="250" stroke="currentColor"/><line x1="70" y1="50" x2="70" y2="250" stroke="currentColor"/>
<rect x="131.5" y="68.5" width="52" height="181.5" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="157.5" y="63.5" text-anchor="middle" font-size="12">363</text>
<rect x="191.5" y="64.5" width="52" height="185.5" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="217.5" y="59.5" text-anchor="middle" font-size="12">371</text>
<text x="277.5" y="244" text-anchor="middle" font-size="12">(안 함)</text>
<text x="217.5" y="268" text-anchor="middle" font-size="12">A. 캐시 없음</text>
<rect x="426.5" y="211.5" width="52" height="38.5" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="452.5" y="206.5" text-anchor="middle" font-size="12">77</text>
<rect x="486.5" y="213.0" width="52" height="37.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="512.5" y="208.0" text-anchor="middle" font-size="12">74</text>
<rect x="546.5" y="74.5" width="52" height="175.5" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/>
<text x="572.5" y="69.5" text-anchor="middle" font-size="12">351</text>
<text x="512.5" y="268" text-anchor="middle" font-size="12">B. 버퍼 캐시 300 MiB</text>
<rect x="150" y="290" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="168" y="300" font-size="12">GFP_NOWAIT</text>
<rect x="300" y="290" width="12" height="12" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/><text x="318" y="300" font-size="12">GFP_ATOMIC</text>
<rect x="450" y="290" width="12" height="12" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/><text x="468" y="300" font-size="12">GFP_KERNEL (상한 351)</text>
<text x="340" y="322" text-anchor="middle" font-size="12">B에서 GFP_KERNEL만 버퍼 캐시를 직접 회수(direct reclaim)해서 351 MiB를 채웠다</text>
</svg>
```

그림 10 — 예제 10 결과. 회수할 캐시가 있을 때(B) 잠들 수 없는 두 플래그는 "지금 free인 것"만 가져가고 멈춘다. `GFP_KERNEL`은 직접 회수로 캐시를 밀어내고 상한(90000 페이지 = 351 MiB)을 다 채웠다.

출력에서 볼 것:

- **A (회수할 것 없음)**: NOWAIT 93123장, ATOMIC 95192장 — ATOMIC이 **2069장(8 MiB) 더** 받았다. 남은 free를 보면 NOWAIT은 9132, ATOMIC은 8182 페이지에서 멈췄다. 이 중 7936장은 CMA(unmovable 요청이 못 쓰는 영역, 예제 5)이므로 non-CMA 기준으로는 NOWAIT ≈ 1196, **ATOMIC ≈ 246**이다. 246은 그림 9의 계산값 250과 맞는다 — `GFP_ATOMIC`은 일반 요청이 못 들어가는 min 아래 예비분을 실제로 파먹는다. NOWAIT이 min(665)보다 위인 ≈1196에서 멈춘 정확한 조건은 이 실험으로 확정하지 않았다(pcp 목록에 남은 페이지와 검사 시점 차이로 추정).
- **B (버퍼 캐시 300 MiB)**: 같은 NOWAIT/ATOMIC이 **77 MiB, 74 MiB**밖에 못 받았다. free가 90 MiB뿐이었고, 둘 다 `__GFP_DIRECT_RECLAIM`이 없어서 캐시를 직접 버리지 못한다. 할 수 있는 건 kswapd를 깨우는 것뿐인데 kswapd가 회수하는 속도보다 루프가 빨랐다.
- **`GFP_KERNEL`**은 같은 상황에서 351 MiB를 다 받았다. `Buffers`가 307200 → 25688 kB로 줄었고, `pgsteal_direct 24073`(할당하던 스레드가 직접 회수한 페이지), `pgsteal_kswapd 46583`(kswapd가 회수한 페이지)이 그 증거다. 대신 시간이 들었다(8.7초, TCG 기준). 잠들어도 되는 문맥이라서 가능한 일이다.
- `compact_stall 0` — order-0이라 compaction이 필요 없었다. compaction은 order > 0 요청에서 회수만으로 연속 블록이 안 생길 때 끼어든다.
- 여기서 ATOMIC으로 바닥까지 판 것은 **실험이라서** 괜찮았다. 실제 장비에서 IRQ 경로가 예비분을 다 쓰면, 진짜로 예비분이 필요한 곳(네트워크 RX, 블록 I/O 완료 처리)이 실패한다.

### 8.4 IRQ 핸들러에서는 왜 GFP_ATOMIC인가

hardirq·softirq·spinlock 보유 구간은 **잠들 수 없다**(F2). `GFP_KERNEL`은 `__GFP_DIRECT_RECLAIM` 때문에 회수 중 I/O를 기다리며 `schedule()`할 수 있으므로, atomic 문맥에서 쓰면 "BUG: sleeping function called from invalid context"(디버그 커널의 `DEBUG_ATOMIC_SLEEP`, E4 §5에서 실제로 재현) 또는 데드락이 된다. 그래서:

- atomic 문맥에서 꼭 할당해야 하면 `GFP_ATOMIC`(실패 시 손해가 큰 경우 — 예비분 사용) 또는 `GFP_NOWAIT`(실패해도 되는 경우).
- 그리고 **반드시 실패를 처리한다.** 위 실험처럼 `GFP_ATOMIC`은 B 상태에서 메모리가 300 MiB "있는데도" 실패한다.
- 가장 좋은 답은 **IRQ 핸들러에서 할당하지 않는 것**이다: `probe()`에서 링버퍼를 미리 잡거나, `mempool`(최소 개수를 예약해 둔 풀)을 쓰거나, 할당이 필요한 일을 threaded IRQ나 workqueue(F6, F7)로 넘겨 `GFP_KERNEL`로 처리한다.

### 8.5 GFP_NOIO / GFP_NOFS와 범위 API

블록 드라이버가 쓰기 요청을 처리하다 `GFP_KERNEL`로 할당하면, 할당자가 메모리를 만들려고 dirty 페이지를 디스크에 쓰려 하고 → 그 쓰기가 같은 블록 드라이버로 들어와 → 자기 자신을 기다리는 데드락이 될 수 있다. 그래서 I/O 경로는 `GFP_NOIO`, 파일시스템 내부는 `GFP_NOFS`를 쓴다. 요즘 권장은 호출 경로 깊숙이까지 플래그를 전달하는 대신 **구간을 표시**하는 것이다:

```c
/* 실행하지 않음 — API 형태만. include/linux/sched/mm.h */
unsigned int flags = memalloc_noio_save();
/* 이 구간의 모든 할당은 GFP_KERNEL로 불러도 자동으로 __GFP_IO, __GFP_FS가 빠진다 */
do_something_that_allocates();
memalloc_noio_restore(flags);
```

### 8.6 할당 실패를 다루는 법

- 반환값은 항상 검사하고, `goto` 되감기(또는 devm)로 앞서 잡은 것을 푼다. 실패 메시지(`pr_err("out of memory")`)는 붙이지 않는다 — 할당자가 이미 예제 5 같은 경고를 찍고, `checkpatch`도 이걸 지적한다.
- 실패해도 되는 큰 할당(성능용 캐시 등)은 `__GFP_NOWARN | __GFP_NORETRY`로 가볍게 시도하고 작은 크기로 물러선다.
- 반드시 필요한 큰 할당은 `__GFP_RETRY_MAYFAIL`(열심히 하되 OOM killer는 안 부름) 또는 `kvmalloc`.
- `__GFP_NOFAIL`은 "실패를 처리할 방법이 정말 없을 때"만. 무한 재시도라 데드락·지연의 원인이 된다.
- 반드시 진행해야 하는 경로(블록 I/O 완료, 네트워크 스토리지)는 `mempool_create()`로 최소 개수를 예약해 둔다.

---

## 9. 메모리 버그 잡기 — slab_debug와 page_owner

### 9.1 예제 11 — slab_debug=FZPU로 overflow와 use-after-free 잡기 (`f9_bug`)

`SLUB_DEBUG`가 컴파일된 커널은 부팅 인자로 객체 주변에 감시 장치를 켤 수 있다(옛 이름 `slub_debug=`도 받는다). 플래그:

| 플래그 | 이름 | 하는 일 |
|---|---|---|
| `F` | sanity checks | 할당·해제 때 slab 메타데이터 일관성 검사 |
| `Z` | red zoning | 객체 앞뒤에 0xcc(사용 중)/0xbb(free) 패턴 영역을 두고 덮어쓰기 감지 |
| `P` | poisoning | free 객체를 0x6b(`k`)로 채우고 마지막 바이트 0xa5. 재할당 전에 바뀌었으면 UAF |
| `U` | user tracking | 각 객체의 마지막 할당·해제 스택 기록 |
| `T` | trace | 모든 할당·해제를 로그 (매우 시끄러움) |

요청은 `slab_debug=FZP`였지만 이 예제는 리포트에 "누가 할당·해제했나" 스택이 나오도록 `U`를 더해 **`slab_debug=FZPU`**로 부팅했다. `slab_debug=FZPU,kmalloc-64`처럼 캐시를 지정할 수도 있다.

무엇을 확인하나: `kmalloc(64)` 버퍼 끝 바로 뒤 4바이트를 쓰는 off-by-N 버그와, 해제한 버퍼에 쓰는 UAF가 각각 어떤 리포트로 잡히는지.

```c
// SPDX-License-Identifier: GPL-2.0
/* f9_bug: 일부러 slab 메모리를 망가뜨린다 (slab_debug=FZPU 부팅에서만!) */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/slab.h>

static char *bug = "overflow";
module_param(bug, charp, 0444);

static noinline void f9_overflow(void)
{
	u8 *p = kmalloc(64, GFP_KERNEL);	/* kmalloc-64 객체 하나 */

	if (!p)
		return;
	pr_info("overflow: p=%px, writing p[64..67]\n", p);
	memset(p + 64, 0x41, 4);		/* off-by-N: 객체 끝 바로 뒤 */
	kfree(p);				/* 해제 때 redzone 검사 */
}

static noinline void f9_uaf(void)
{
	u8 *p = kmalloc(64, GFP_KERNEL);

	if (!p)
		return;
	kfree(p);
	pr_info("uaf: p=%px freed, now writing p[8..15]\n", p);
	memset(p + 8, 0x42, 8);			/* use-after-free 쓰기 */
}

static int __init f9_bug_init(void)
{
	if (!strcmp(bug, "uaf"))
		f9_uaf();
	else
		f9_overflow();
	return 0;
}

static void __exit f9_bug_exit(void) { }
module_init(f9_bug_init);
module_exit(f9_bug_exit);
MODULE_DESCRIPTION("F9: SLUB debug catches overflow and use-after-free");
MODULE_LICENSE("GPL");
```

```sh
# Lima VM: 커널 인자에 slab_debug=FZPU
TMO=240 FILES=/var/tmp/F9/f9_bug/f9_bug.ko ./run_guest.sh t7 t7.sh slab_debug=FZPU
# t7.sh (게스트 안)
cat /sys/kernel/slab/kmalloc-64/sanity_checks /sys/kernel/slab/kmalloc-64/red_zone /sys/kernel/slab/kmalloc-64/poison /sys/kernel/slab/kmalloc-64/store_user | tr '\n' ' '; echo "(sanity red_zone poison store_user)"
grep kmalloc-64 /proc/slabinfo | awk '{print "kmalloc-64 objsize", $4, "objperslab", $5}'
insmod /f9_bug.ko bug=overflow
insmod /f9_bug.ko bug=uaf 2>/dev/null || { rmmod f9_bug; insmod /f9_bug.ko bug=uaf; }
echo 1 > /sys/kernel/slab/kmalloc-64/validate
echo "=== dmesg"
dmesg | sed -n '/f9_bug: overflow/,$p'
```

```text
lab initramfs: 6.18.55-dbg up 11.23s
1 1 1 1 (sanity red_zone poison store_user)
kmalloc-64 objsize 256 objperslab 16
=== dmesg
[   12.507172] f9_bug: overflow: p=ffff000004daef40, writing p[64..67]
[   12.507359] [Right Redzone overwritten] 0xffff000004daef80-0xffff000004daef83 @offset=3968. First byte 0x41 instead of 0xcc
[   12.507524] =============================================================================
[   12.507754] BUG kmalloc-64 (Tainted: G           O       ): Object corrupt
[   12.507774] -----------------------------------------------------------------------------
[   12.507774] 
[   12.507884] Allocated in f9_overflow+0x2c/0xf38 [f9_bug] age=0 cpu=1 pid=76
[   12.508696]  __kmalloc_cache_noprof+0x33c/0x608
[   12.509079]  f9_overflow+0x2c/0xf38 [f9_bug]
[   12.509120]  f9_bug_init+0x38/0xfb8 [f9_bug]
[   12.509171]  do_one_initcall+0x84/0x380
...
[   12.509474] Freed in apply_wqattrs_cleanup.part.0+0xe8/0x108 age=397 cpu=1 pid=1
...
[   12.509765] Slab 0xfffffdffc0136b80 objects=16 used=16 fp=0x0000000000000000 flags=0x3fffe0000000000(node=0|zone=0|lastcpupid=0x1ffff)
[   12.509941] Object 0xffff000004daef40 @offset=3904 fp=0x0000000000000000
[   12.509941] 
[   12.510265] Redzone  ffff000004daef00: cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc cc  ................
...
[   12.510382] Object   ffff000004daef40: 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b  kkkkkkkkkkkkkkkk
[   12.510394] Object   ffff000004daef50: 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b  kkkkkkkkkkkkkkkk
[   12.510404] Object   ffff000004daef60: 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b  kkkkkkkkkkkkkkkk
[   12.510414] Object   ffff000004daef70: 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b a5  kkkkkkkkkkkkkkk.
[   12.510438] Redzone  ffff000004daef80: 41 41 41 41 cc cc cc cc                          AAAA....
[   12.510488] Padding  ffff000004daefd8: 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a 5a  ZZZZZZZZZZZZZZZZ
...
[   12.510840] ------------[ cut here ]------------
[   12.510917] WARNING: CPU: 1 PID: 76 at mm/slub.c:1234 object_err+0x200/0x220
...
[   12.517042]  object_err+0x200/0x220 (P)
[   12.517179]  check_bytes_and_report+0x114/0x160
[   12.517200]  check_object+0x260/0x3c0
[   12.517217]  free_to_partial_list+0x1d0/0x570
[   12.517234]  __slab_free+0x218/0x380
[   12.517251]  kfree+0x3cc/0x4b0
[   12.517267]  f9_overflow+0x5c/0xf38 [f9_bug]
...
[   12.517976] FIX kmalloc-64: Restoring Right Redzone 0xffff000004daef80-0xffff000004daef83=0xcc
[   12.518063] FIX kmalloc-64: Object at 0xffff000004daef40 not freed
[   12.760544] f9_bug: uaf: p=ffff0000054d6b40 freed, now writing p[8..15]
[   12.771191] [Poison overwritten] 0xffff0000054d6b48-0xffff0000054d6b4f @offset=2888. First byte 0x42 instead of 0x6b
[   12.771293] =============================================================================
[   12.771316] BUG kmalloc-64 (Tainted: G    B   W  O       ): Object corrupt
[   12.771334] -----------------------------------------------------------------------------
[   12.771334] 
[   12.771345] Allocated in f9_uaf+0x2c/0x70 [f9_bug] age=3 cpu=0 pid=79
...
[   12.772132] Freed in f9_uaf+0x38/0x70 [f9_bug] age=3 cpu=0 pid=79
[   12.772169]  kfree+0x3cc/0x4b0
[   12.772190]  f9_uaf+0x38/0x70 [f9_bug]
...
[   12.772606] Object   ffff0000054d6b40: 6b 6b 6b 6b 6b 6b 6b 6b 42 42 42 42 42 42 42 42  kkkkkkkkBBBBBBBB
[   12.772619] Object   ffff0000054d6b50: 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b  kkkkkkkkkkkkkkkk
[   12.772632] Object   ffff0000054d6b60: 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b  kkkkkkkkkkkkkkkk
[   12.772645] Object   ffff0000054d6b70: 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b a5  kkkkkkkkkkkkkkk.
[   12.772659] Redzone  ffff0000054d6b80: bb bb bb bb bb bb bb bb                          ........
...
[   12.786837]  object_err+0x200/0x220 (P)
[   12.786868]  check_bytes_and_report+0x114/0x160
[   12.786890]  check_object+0x300/0x3c0
[   12.786909]  validate_slab+0x14c/0x1a0
[   12.786927]  validate_slab_cache+0xa8/0x1b8
[   12.786946]  validate_store+0x54/0x80
...
[   12.787425] FIX kmalloc-64: Restoring Poison 0xffff0000054d6b48-0xffff0000054d6b4f=0x6b
```

(리포트가 길어서 `...` 자리에서 콜 트레이스 일부, 레지스터 덤프, 반복 Redzone/Padding 줄을 생략했다. 남긴 줄은 실제 출력 그대로다.)

출력에서 볼 것:

- **`kmalloc-64 objsize 256`**: 디버그가 켜지면 64 B 객체 하나가 레드존(앞 64 B, 뒤 8 B)·추적 정보·패딩을 달고 256 B를 차지한다. slab당 객체도 64 → 16개. 그래서 양산 커널에서는 끈다. 반대로 말하면 **디버그를 켜면 메모리 배치가 바뀌어서 버그가 사라지거나 다른 데서 터질 수 있다.**
- **overflow**: `kfree` 시점의 검사(`free_to_partial_list` → `check_object`)가 `Right Redzone overwritten … First byte 0x41 instead of 0xcc`로 잡았다. 덤프에서 `Redzone ffff000004daef80: 41 41 41 41 cc cc cc cc` — 객체 끝(0x…ef80) 바로 뒤 4바이트가 정확히 `AAAA`다. 커널은 레드존을 복구하고(`FIX … Restoring`) 그 객체를 **해제하지 않고 격리**했다(`Object … not freed`). 망가진 객체를 재사용하지 않으려는 것이다.
- 이 리포트의 `Freed in apply_wqattrs_cleanup…`은 이 메모리 칸의 **이전 주인**이 해제한 기록이다(우리 해제는 검사에서 막혀 기록되지 않았다). `Allocated in f9_overflow+0x2c`가 범인이다.
- **UAF**: 해제 직후 쓴 8바이트가 `kkkkkkkkBBBBBBBB` — poison(0x6b) 한가운데 `0x42` 8개로 보인다. 쓰는 순간에는 아무 일도 없었다. `/sys/kernel/slab/kmalloc-64/validate`에 1을 써서 **전수 검사를 강제했을 때** 잡혔다(그냥 두면 이 객체가 다음에 할당될 때 잡힌다). `Allocated in f9_uaf+0x2c`와 `Freed in f9_uaf+0x38`이 둘 다 찍혀 "같은 함수가 잡고 풀고 또 썼다"가 바로 보인다 — `U` 플래그 덕분이다.
- `Tainted: G B W O`: `B`(bad page — 메모리 손상 감지), `W`(경고 발생), `O`(트리 밖 모듈). 버그 리포트를 받을 때 taint 문자로 "이 커널이 이미 오염됐는가"를 먼저 본다.
- 한계: slab_debug는 **사후 검사**다. 언제 망가뜨렸는지가 아니라 다음 검사 시점에 잡힌다. 쓰는 순간을 잡으려면 KASAN(컴파일러 계측, 1/8 섀도 메모리)이나 KFENCE(샘플링 기반 guard page)가 필요한데, 이 랩 커널에는 둘 다 꺼져 있어 실행하지 않았다(M 모듈).

### 9.2 예제 12 — page_owner로 "이 페이지 누가 잡았나" (`f9_owner`)

무엇을 확인하나: `page_owner=on`으로 부팅하면 모든 페이지 할당에 할당 스택이 기록되는지, 그리고 내 모듈이 잡은 order-2 블록을 덤프에서 찾을 수 있는지. 누수 추적의 기본 도구다.

```c
// SPDX-License-Identifier: GPL-2.0
/* f9_owner: 페이지를 잡아 두고 page_owner로 "누가 잡았나" 추적 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/mm.h>

static struct page *pg;

static noinline struct page *f9_grab_dma_ring(void)	/* 링버퍼용 16 KiB */
{
	return alloc_pages(GFP_KERNEL | __GFP_ZERO, 2);
}

static int __init f9_owner_init(void)
{
	pg = f9_grab_dma_ring();
	if (!pg)
		return -ENOMEM;
	pr_info("holding order-2 at pfn %#lx\n", page_to_pfn(pg));
	return 0;
}

static void __exit f9_owner_exit(void)
{
	__free_pages(pg, 2);
}
module_init(f9_owner_init);
module_exit(f9_owner_exit);
MODULE_DESCRIPTION("F9: page_owner tracking");
MODULE_LICENSE("GPL");
```

```sh
# 커널 인자 page_owner=on 으로 부팅. 게스트 안 (t.sh)
insmod /f9_owner.ko
dmesg | grep f9_owner: 
cat /sys/kernel/debug/page_owner > /po.txt
echo "page_owner records: $(grep -c '^Page allocated' /po.txt)"
awk 'BEGIN{RS=""} /f9_owner/' /po.txt
```

```text
lab initramfs: 6.18.55-dbg up 16.34s
[   18.044860] f9_owner: holding order-2 at pfn 0x446d0
page_owner records: 7365
Page allocated via order 2, mask 0xdc0(GFP_KERNEL|__GFP_ZERO), pid 74, tgid 74 (insmod), ts 18044772560 ns
PFN 0x446d0 type Unmovable Block 547 type Unmovable Flags 0x3fffe0000000000(node=0|zone=0|lastcpupid=0x1ffff)
 get_page_from_freelist+0x1080/0x1340
 __alloc_frozen_pages_noprof+0x124/0x1178
 alloc_pages_mpol+0xc8/0x1c0
 alloc_frozen_pages_noprof+0x4c/0xd8
 alloc_pages_noprof+0x18/0x70
 f9_grab_dma_ring+0x20/0x30 [f9_owner]
 0xffffda108cea1050
 do_one_initcall+0x84/0x380
 do_init_module+0x5c/0x268
 load_module+0x1a30/0x2080
 __do_sys_init_module+0x1a8/0x268
 __arm64_sys_init_module+0x20/0x40
 invoke_syscall.constprop.0+0x48/0xf0
 el0_svc_common.constprop.0+0x40/0xf0
 do_el0_svc+0x24/0x40
 el0_svc+0x50/0x2f8
```

출력에서 볼 것:

- 레코드 하나가 "**order, GFP 마스크(이름으로 풀어 줌), pid·프로세스, 타임스탬프, PFN, migratetype, 할당 스택**"이다. `mask 0xdc0` = `GFP_KERNEL`(0xcc0) + `__GFP_ZERO`(0x100). `PFN 0x446d0`이 모듈이 찍은 pfn과 같다. `Block 547`은 pageblock 번호(0x446d0 / 512 = 547).
- 스택에 `f9_grab_dma_ring+0x20 [f9_owner]`가 있다 — 일부러 `noinline` 함수로 감싼 이유다. 실제 드라이버에서도 "어느 할당 지점이 페이지를 쥐고 있나"를 함수 이름으로 바로 안다. 그 아래 `0xffffda108cea1050`은 이미 해제된 모듈 `init` 섹션 주소라 심볼이 없다.
- 7365개 레코드 = 지금 할당돼 있는 모든 페이지 블록. 누수 추적 실무는: 의심 동작 전후로 `/sys/kernel/debug/page_owner`를 떠서 스택별로 묶어 비교한다. 커널 소스의 `tools/mm/page_owner_sort.c`가 이 정렬을 해 준다(랩 initramfs에는 없어서 실행하지 않았다).
- 첫 시도에서 `awk`가 `/sys/kernel/debug/page_owner`를 직접 읽다가 `Cannot allocate memory`로 실패했다 — debugfs 파일을 큰 버퍼 한 번에 읽으려 해서다. `cat`으로 파일에 복사한 뒤 처리하면 된다.
- 비용: `page_owner`는 `CONFIG_PAGE_OWNER=y`여도 부팅 인자 없이는 꺼져 있다(그래서 양산 커널에 컴파일해 두고 필요할 때만 켜는 경우가 있다). 켜면 페이지마다 스택 핸들을 저장하는 `page_ext` 메모리와 할당 경로 비용이 든다.

---

## 10. 커널 안에서는 무슨 일이 일어나나

`kmalloc(64, GFP_KERNEL)` 한 줄이 실제로 지나가는 길이다. 함수 이름은 6.18 기준이다(예제 5·11·12의 콜 트레이스에 실제로 찍힌 이름: `__kmalloc_cache_noprof`, `alloc_pages_noprof`, `__alloc_frozen_pages_noprof`, `get_page_from_freelist`, `warn_alloc`). `_noprof` 접미사는 6.10에 들어온 메모리 할당 프로파일링(`CONFIG_MEM_ALLOC_PROFILING`) 래퍼 때문에 생긴 것으로, 드라이버는 계속 `kmalloc`/`alloc_pages`를 부른다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="340" y="20" text-anchor="middle" font-size="13" font-weight="bold">kmalloc → SLUB → 페이지 할당자 → (부족하면) 회수 · compaction · OOM</text>
  <rect x="20" y="36" width="300" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="58" text-anchor="middle" font-size="12">kmalloc(size, gfp)</text>
  <rect x="20" y="92" width="300" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="114" text-anchor="middle" font-size="12">size &gt; 8 KiB ?  (KMALLOC_MAX_CACHE_SIZE)</text>
  <rect x="20" y="148" width="300" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="166" text-anchor="middle" font-size="12">아니오 → 크기 등급 캐시 선택</text>
  <text x="170" y="184" text-anchor="middle" font-size="12">(kmalloc-8 … kmalloc-8k)</text>
  <rect x="20" y="214" width="300" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="232" text-anchor="middle" font-size="12">slab_alloc_node: CPU freelist에 객체?</text>
  <text x="170" y="250" text-anchor="middle" font-size="12">있으면 cmpxchg로 꺼내 반환 (fast path)</text>
  <rect x="20" y="280" width="300" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="298" text-anchor="middle" font-size="12">___slab_alloc: partial 목록에서 slab</text>
  <text x="170" y="316" text-anchor="middle" font-size="12">없으면 new_slab() → 페이지 요청</text>
  <line x1="170" y1="70" x2="170" y2="90" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="170" y1="126" x2="170" y2="146" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="170" y1="192" x2="170" y2="212" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="170" y1="258" x2="170" y2="278" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="350" y="36" width="310" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="505" y="54" text-anchor="middle" font-size="12">alloc_pages(gfp, order)</text>
  <text x="505" y="72" text-anchor="middle" font-size="12">→ __alloc_frozen_pages_noprof</text>
  <rect x="350" y="102" width="310" height="44" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="505" y="120" text-anchor="middle" font-size="12">get_page_from_freelist: 워터마크 OK?</text>
  <text x="505" y="138" text-anchor="middle" font-size="12">→ pcp 목록 또는 buddy free_area에서 꺼냄</text>
  <rect x="350" y="168" width="310" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="505" y="186" text-anchor="middle" font-size="12">__alloc_pages_slowpath: kswapd 깨움</text>
  <text x="505" y="204" text-anchor="middle" font-size="12">min 워터마크 (+ATOMIC 예비분)로 재시도</text>
  <rect x="350" y="234" width="310" height="44" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="505" y="252" text-anchor="middle" font-size="12">__GFP_DIRECT_RECLAIM 있으면 (잠듦)</text>
  <text x="505" y="270" text-anchor="middle" font-size="12">direct compaction · direct reclaim · 재시도</text>
  <rect x="350" y="300" width="310" height="62" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="505" y="320" text-anchor="middle" font-size="12">order ≤ 3 (GFP_KERNEL): OOM killer 후 재시도</text>
  <text x="505" y="338" text-anchor="middle" font-size="12">그 밖: NULL 반환 + warn_alloc() 경고</text>
  <text x="505" y="356" text-anchor="middle" font-size="12">(__GFP_NOWARN이면 경고 없음)</text>
  <line x1="320" y1="109" x2="348" y2="62" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="338" y="104" font-size="12">예</text>
  <line x1="320" y1="302" x2="348" y2="74" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
  <line x1="505" y1="80" x2="505" y2="100" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="505" y1="146" x2="505" y2="166" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="505" y1="212" x2="505" y2="232" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="505" y1="278" x2="505" y2="298" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="20" y="384" width="640" height="70" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="404" text-anchor="middle" font-size="12">예제 5의 order-10 실패 = 오른쪽 맨 아래 상자 (order &gt; 3 → OOM 없이 NULL + 경고)</text>
  <text x="340" y="424" text-anchor="middle" font-size="12">예제 10의 ATOMIC/NOWAIT = 오른쪽 셋째 상자에서 끝 (direct reclaim 없음)</text>
  <text x="340" y="444" text-anchor="middle" font-size="12">예제 10의 KERNEL = 넷째 상자에서 버퍼 캐시를 회수하고 성공</text>
</svg>
```

그림 11 — 할당 경로. 대부분의 `kmalloc`은 왼쪽 넷째 상자(CPU freelist)에서 끝난다(E4 §6.2에서 잰 수백 ns). 페이지가 필요할 때만 오른쪽으로 넘어가고, 오른쪽 아래로 갈수록 비싸고(µs → ms → 초) 잠들 가능성이 커진다.

주요 자료구조와 이 노트에서 만난 위치:

| 구조체 / 변수 | 어디 | 이 노트에서 |
|---|---|---|
| `pg_data_t` (`struct pglist_data`) | `include/linux/mmzone.h` | 노드 0 하나 (§1) |
| `struct zone` → `free_area[order].free_list[migratetype]`, `_watermark[]` | `include/linux/mmzone.h` | buddyinfo·pagetypeinfo·zoneinfo (§1, §4) |
| `struct per_cpu_pages` (pcp) | `include/linux/mmzone.h` | zoneinfo의 `high:` 줄, rmmod 후 남은 order-0 (§4.5) |
| `struct page` / `struct folio`, `vmemmap` | `include/linux/mm_types.h` | 64 B, `fffffdffc0000000`부터 (§2, §3) |
| `struct kmem_cache`, `struct slab` | `mm/slab.h` | `/sys/kernel/slab/*` (§6) |
| `struct vm_struct`, `struct vmap_area` | `include/linux/vmalloc.h` | `/proc/vmallocinfo` (§3.3) |
| `struct page_ext` + stack depot | `mm/page_ext.c`, `mm/page_owner.c` | page_owner (§9.2) |

드라이버를 쓸 때 이 지식이 다시 나오는 곳:

- **G6 (devres)**: `devm_kzalloc`/`devm_kcalloc` — §7의 패턴을 실제 platform driver로.
- **H8 (DMA API)**: `dma_alloc_coherent`, `dma_map_single`, scatter-gather. "vmalloc 버퍼는 왜 SG가 필요한가"(그림 4)의 답.
- **F10 (CMA, reserved-memory)**: 예제 5에서 본 `(C)` 블록의 정체와, 카메라 버퍼처럼 수십 MiB 연속 메모리를 얻는 법.
- **F11 (가상 메모리 심화)**: 페이지 테이블 walk(`vmalloc_to_page`가 하는 일), page fault, OOM killer의 희생자 선택.
- **F6/F7 (IRQ, deferred work)**: §8.4의 "IRQ에서 할당하지 말고 미리 잡거나 workqueue로".
- **A5 (mmap)**: 유저 `mmap` → 드라이버 `.mmap` → `remap_pfn_range(pfn)` — 여기서 다룬 pfn이 그대로 쓰인다.

---

## 11. Embedded 실무에서 다시 보기

### 11.1 센서 키트 — 1 kHz IMU 샘플을 어디에 담나

IMU 드라이버가 인터럽트마다 샘플 노드를 `kmalloc(sizeof(struct sample), GFP_ATOMIC)`로 잡아 리스트에 붙이는 설계를 흔히 본다. 문제가 셋이다: (1) IRQ 경로 할당은 실패할 수 있고(예제 10의 B 상태), 실패하면 샘플이 빠진다, (2) 40 B 구조체가 `kmalloc-64`에서 나와 37%를 버린다(예제 9), (3) 할당·해제가 초당 수천 번이라 slab 경로 비용이 누적된다.

더 나은 순서: **`probe()`에서 링버퍼를 한 번에 잡는다**(`devm_kcalloc`, 또는 `kfifo`). 샘플 객체를 동적으로 꼭 다뤄야 하면 전용 `kmem_cache`(예제 8) + `mempool`로 최소 개수를 보장한다. 할당은 IRQ가 아니라 threaded IRQ/workqueue에서 `GFP_KERNEL`로.

### 11.2 카메라 · LiDAR 버퍼 — "4 MiB면 kmalloc으로 되지 않나?"

1080p NV12 프레임은 약 3 MiB다. `kmalloc` 상한(4 MiB) 안이지만, 예제 5처럼 장비가 며칠 돌고 나면 order-10 블록이 남아 있으리라는 보장이 없다. 게다가 일반 커널 할당은 CMA를 못 쓴다. `vmalloc`은 물리적으로 흩어져 있어 IOMMU 없는 ISP/DMA 엔진이 못 쓴다. 정답은 DMA API(`dma_alloc_coherent`)가 CMA 영역에서 잡게 하는 것 — V4L2에서는 videobuf2의 `vb2_dma_contig`가 이걸 해 준다(F10, Q 모듈). 버퍼 개수는 스트리밍 시작 전에 정하고 한 번에 할당한다.

### 11.3 현장 장비의 `page allocation failure`

며칠 돈 장비의 `dmesg`에 이런 줄이 보이는 경우가 많다: `page allocation failure: order:3, mode:0x820(GFP_ATOMIC)` + 무선랜/이더넷 드라이버 스택. 읽는 법은 예제 5와 같다: order 3(32 KiB) 연속 블록을 softirq에서(ATOMIC) 요구했는데 단편화로 없었다. 대응:

- 경고의 `Node 0 DMA: …` 줄에서 order별 free 블록과 migratetype을 본다. order-0은 많은데 order-3 이상이 0이면 단편화다.
- 드라이버가 큰 RX 버퍼를 order-0 페이지 여러 장(page pool, SG)으로 받도록 바꾼다 — 가장 근본적인 해결.
- `vm.min_free_kbytes`를 올려 kswapd가 더 일찍, 더 많이 확보하게 한다(예비분이 늘어 ATOMIC이 버틴다). 대신 쓸 수 있는 메모리가 줄어든다.
- 점검 명령은 이 노트에서 실행한 것들의 조합이다:

```sh
# 예제 1·5·6·9에서 실제로 실행한 명령 — 현장 점검용으로 묶음
cat /proc/buddyinfo
grep -E "^Node.*(Unmovable|Movable|Reclaimable) " /proc/pagetypeinfo
grep -E "^(MemFree|MemAvailable|Slab|SUnreclaim):" /proc/meminfo
awk 'NR>2 {kb=$15*$6*4; printf "%7d %-24s objs=%s size=%s\n", kb, $1, $3, $4}' /proc/slabinfo | sort -rn | head -8
```

### 11.4 드라이버 누수 — 일주일에 10 MiB씩 줄어드는 MemAvailable

`/proc/meminfo`의 `SUnreclaim`(회수 불가 slab)이 꾸준히 늘면 커널 객체 누수다. (1) 시간 간격을 두고 위 slabtop 흉내를 두 번 떠서 커지는 캐시를 찾는다 — 전용 캐시를 썼다면(예제 8, `SLAB_NO_MERGE`) 이름으로 바로 보인다, `kmalloc-*`라면 (2) `slab_debug=U,kmalloc-128` 같은 캐시 한정 추적으로 `/sys/kernel/debug/slab/kmalloc-128/alloc_traces`에서 할당 지점별 개수를 본다(이 노트에서는 실행하지 않음), (3) 페이지 단위 누수면 `page_owner`(예제 12). `CONFIG_DEBUG_KMEMLEAK`이 있는 개발 커널이면 kmemleak가 "참조가 사라진 객체"를 직접 보고한다(이 랩 커널에는 없음).

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| IRQ 핸들러·spinlock 안에서 `GFP_KERNEL` | 디버그 커널: "sleeping function called from invalid context". 양산: 드물게 데드락·지연 | 회수 경로가 잠든다 | `GFP_ATOMIC`/`GFP_NOWAIT` + 실패 처리, 더 좋게는 미리 할당 |
| `vmalloc` 버퍼(또는 스택 버퍼)를 DMA에 넘김 | 데이터 깨짐, IOMMU fault, `virt_to_phys` 엉뚱한 값 | 물리 연속이 아니고 linear map 주소도 아님 (그림 4) | DMA API로 할당, SG 매핑 |
| `kmalloc(n * size)` | 큰 n에서 작은 버퍼 할당 → overflow | 곱셈 오버플로 | `kcalloc`/`kmalloc_array` |
| `kmalloc` 반환값 미검사 | 드물게 NULL 역참조 Oops | 커널 할당은 실패할 수 있다 | 검사 + `goto` 되감기 / devm |
| order-N 해제 때 다른 order를 넘김 | `bad page state`, buddy 손상 | `__free_pages`가 크기를 모름 | 할당 order를 구조체에 저장 |
| 장시간 운용 후 큰 연속 할당 | `page allocation failure: order:N` (메모리는 남음) | 외부 단편화 (예제 5·7) | `probe()` 때 미리, CMA, order-0 SG |
| 40 B 구조체를 고빈도 `kmalloc` | 메모리 37% 낭비, slabinfo에서 추적 불가 | 크기 등급 반올림 (예제 9) | 전용 `kmem_cache` (+`SLAB_NO_MERGE`) |
| 블록 드라이버 I/O 경로에서 `GFP_KERNEL` | 메모리 압박 때만 나는 데드락 | 회수가 같은 드라이버로 I/O를 보냄 | `GFP_NOIO` 또는 `memalloc_noio_save()` |
| 장치보다 오래 사는 버퍼를 `devm_` 으로 | 장치 제거 후 use-after-free | devres는 detach 때 해제 | 참조 카운트로 수명 관리, 일반 `kzalloc` |
| `kfree(vmalloc_ptr)` / `vfree(kmalloc_ptr)` | Oops 또는 조용한 손상 | 할당자 불일치 | 짝 맞추기, 모르면 `kvfree` |

---

## 13. 면접에서 이렇게 말한다

**Q.** What is the difference between kmalloc and vmalloc? When would you use each?

**A.** `kmalloc`은 slab(작으면)이나 buddy(8 KiB 초과)에서 **물리적으로 연속**인 메모리를 linear map 주소로 준다. 빠르고(수백 ns), atomic 문맥에서도 `GFP_ATOMIC`으로 쓸 수 있지만, 크기 상한(4 MiB)이 있고 큰 크기는 단편화 때문에 실패한다. `vmalloc`은 페이지를 낱장으로 모아 vmalloc 영역에 이어 붙이므로 **가상으로만 연속**이다. 큰 크기도 잘 되지만 느리고(페이지 테이블 작업 + TLB), 잠들 수 있고, DMA에 그대로 못 쓴다. 기본은 `kmalloc`, CPU만 쓰는 큰 테이블은 `kvmalloc`, 장치 버퍼는 둘 다 아니고 DMA API다.

> kmalloc returns physically contiguous memory from the slab caches, or straight from the buddy allocator above 8 KiB, mapped in the linear map. It's fast, usable in atomic context with GFP_ATOMIC, but capped at a few MiB and large requests can fail due to fragmentation. vmalloc stitches together individual pages in the vmalloc area, so it's only virtually contiguous: good for large CPU-only buffers, but slower, may sleep, and not directly usable for DMA. My default is kzalloc, kvmalloc for big tables, and the DMA API for anything a device touches.

**꼬리 질문**: "vmalloc 주소에 virt_to_phys를 쓰면?" → linear map 주소가 아니라서 틀린 값. `vmalloc_to_page`로 페이지마다 구해야 한다(예제 3).

**Q.** Which GFP flag do you use in an interrupt handler, and why?

**A.** `GFP_ATOMIC`. IRQ 문맥은 잠들 수 없는데 `GFP_KERNEL`은 direct reclaim 중 I/O를 기다리며 잠들 수 있다. `GFP_ATOMIC`은 잠들지 않고 대신 min 워터마크 아래 예비분 일부를 쓸 수 있다. 그래도 실패할 수 있으므로 반드시 처리해야 하고, 실제로는 IRQ에서 할당하지 않도록 미리 잡거나 mempool·workqueue를 쓰는 게 더 좋은 답이다. 제 실험에서는 버퍼 캐시가 300 MiB 있어도 `GFP_ATOMIC`은 free 90 MiB까지만 받고 실패했고, `GFP_KERNEL`은 캐시를 회수해서 351 MiB를 받았다.

> GFP_ATOMIC, because interrupt context cannot sleep, and GFP_KERNEL may enter direct reclaim and block. GFP_ATOMIC never sleeps and is allowed to dip into part of the reserve below the min watermark. It can still fail, so the error path is mandatory. Better still is not allocating in the IRQ at all: preallocate in probe, use a mempool, or defer to a threaded IRQ or workqueue where GFP_KERNEL is fine.

**꼬리 질문**: "GFP_NOWAIT과의 차이는?" → 둘 다 안 잠들지만 NOWAIT은 예비분을 안 쓰고(실패해도 되는 할당용), 6.18부터는 NOWARN도 기본 포함.

**Q.** How does the buddy allocator work, and what is external fragmentation?

**A.** 물리 페이지를 2^order 블록(order 0~10)으로 관리하고, order마다 free 목록이 있다. 요청 order가 비면 큰 블록을 반씩 쪼개고 남는 반쪽을 아래 목록에 넣는다. 해제할 때 버디(pfn XOR 2^order)가 free면 합친다. 외부 단편화는 free 메모리 총량은 충분한데 작은 블록으로 흩어져 큰 연속 블록이 없는 상태다 — 예제에서 380 MiB가 free인데 order-0 블록이 9천 개 넘게 쌓였다. 커널은 migratetype 분리와 compaction으로 완화하지만, 커널이 잡은 unmovable 페이지는 옮길 수 없다.

> It manages physical memory in power-of-two blocks, order 0 to 10 here, with a free list per order and migrate type. A request splits a larger block in halves, putting the unused buddy on the lower list; on free, if the buddy at pfn XOR 2^order is also free, they merge upward. External fragmentation is when plenty of memory is free but only in small blocks, so a high-order request fails. Migrate types and compaction help, but unmovable kernel pages can't be moved, so for big contiguous buffers I preallocate early or use CMA.

**Q.** Why does the kernel have a slab allocator, and when would you create your own kmem_cache?

**A.** 커널은 같은 크기 객체(inode, task_struct, skb)를 엄청나게 자주 만들고 버린다. slab은 페이지를 객체 크기로 미리 잘라 두고 CPU별 free 목록에서 락 없이 꺼내 주므로 빠르고 단편화가 없다. 고빈도·고정 크기 객체가 있으면 전용 캐시를 만든다: 크기 등급 반올림 낭비가 없고(40 B 객체가 `kmalloc-64`에서 37% 낭비), `/proc/slabinfo`에 이름으로 보여서 누수를 추적할 수 있고, ctor·정렬 옵션을 줄 수 있다. 단, 기본으로 merge되므로 이름으로 보려면 `SLAB_NO_MERGE`.

> Because the kernel allocates and frees the same fixed-size objects constantly. Slab pre-carves pages into objects and serves them from per-CPU freelists without locks, so it's fast and doesn't fragment. I create a dedicated cache for high-frequency fixed-size objects: no size-class rounding waste, it shows up by name in slabinfo for leak tracking, and I get constructor and alignment options. Note caches get merged by default, so I'd use SLAB_NO_MERGE if I want to observe it.

**Q.** kmalloc failed but /proc/meminfo shows plenty of free memory. What's going on?

**A.** 세 가지를 먼저 본다. (1) 크기: `KMALLOC_MAX_SIZE` 초과는 무조건 실패. (2) 단편화: 경고의 order와 `Node 0 DMA: …` 줄에서 해당 order 이상 블록이 있는지. (3) 타입: free가 CMA 블록뿐이면 unmovable 커널 할당은 못 쓴다 — 실제로 order-10 할당이 4 MiB CMA 블록 7개를 두고 실패했다. 그리고 GFP: ATOMIC이면 회수를 못 해서 캐시가 많아도 실패한다.

> I check the size first — anything above KMALLOC_MAX_SIZE always fails. Then the warning's order and the per-order free list line: plenty of order-0 pages but none at the requested order means fragmentation. Then the migrate type: if the only free high-order blocks are CMA, unmovable kernel allocations can't use them — I reproduced exactly that. And the GFP flags: an atomic allocation can't reclaim page cache, so it fails even when MemAvailable looks large.

**Q.** Can you DMA into a buffer on the kernel stack or from vmalloc?

**A.** 안 된다. arm64는 `VMAP_STACK`이라 스택도 vmalloc 영역에 있고, 둘 다 물리 연속이 아니며 linear map 밖이라 `virt_to_phys`/`dma_map_single`이 틀린 주소를 만든다. DMA 버퍼는 `dma_alloc_coherent`나 `kmalloc` 버퍼를 `dma_map_single`로 매핑해서 쓴다. vmalloc 버퍼를 꼭 써야 하면 페이지마다 SG 목록을 만든다.

> No. With VMAP_STACK the stack lives in the vmalloc area, and vmalloc memory is neither physically contiguous nor in the linear map, so virt_to_phys or dma_map_single would produce a wrong address. Use dma_alloc_coherent, or a kmalloc buffer mapped with the streaming DMA API; for a vmalloc buffer you'd have to build a scatter-gather list page by page.

**Q.** When do you use GFP_NOIO or GFP_NOFS?

**A.** 할당이 메모리 회수를 부를 때 그 회수가 지금 내가 처리 중인 I/O나 파일시스템으로 다시 들어오면 데드락이 될 수 있는 경로다. 블록 드라이버의 요청 처리·스왑 경로는 NOIO, 파일시스템 내부는 NOFS. 요즘은 플래그를 깊이 전달하는 대신 `memalloc_noio_save()`/`memalloc_nofs_save()`로 구간을 표시한다.

> When reclaim triggered by my allocation could recurse into the I/O or filesystem path I'm currently servicing and deadlock — block driver request handling or swap paths use NOIO, filesystem internals use NOFS. Modern code prefers marking the region with memalloc_noio_save or memalloc_nofs_save instead of threading flags through every call.

**Q.** How would you debug memory corruption or a leak in your driver?

**A.** 손상이면 `slab_debug=FZPU`(캐시 한정 가능)로 부팅해서 redzone·poison 리포트를 받는다 — 덮어쓴 바이트 위치, 할당·해제 스택이 나온다. 개발 커널이면 KASAN이 쓰는 순간을 잡는다. 누수면 `/proc/slabinfo`·`SUnreclaim` 추세로 캐시를 좁히고, 페이지 단위면 `page_owner`로 할당 스택을 본다. kmemleak도 있다.

> For corruption I boot with slab_debug=FZPU, optionally limited to one cache: red zones and poisoning report the exact overwritten bytes plus alloc and free stacks. On a dev kernel KASAN catches the bad access at the instruction. For leaks I trend slabinfo and SUnreclaim to find the growing cache, use page_owner for page-level leaks, and kmemleak where it's enabled.

---

## 14. 직접 해보기

1. (개념) 예제 2의 `kmalloc` 주소 `ffff000005c40380`의 물리 주소, pfn, `struct page` 주소를 손으로 계산하라. `struct page`는 64 B, pfn 0x40000의 `struct page`는 `fffffdffc0000000`이다. — 정답/힌트: 물리 0x45c40380, pfn 0x45c40, page = `fffffdffc0000000 + 0x5c40 × 64` = `fffffdffc0171000`(예제 2 출력과 같다).
2. (관찰) 예제 6을 `order=4 count=1`로 바꿔 돌리고 `/proc/pagetypeinfo`의 Unmovable/Movable order-4 열 변화를 설명하라. 같은 명령을 두 번 부팅해서 비교해 보라. — 정답/힌트: Unmovable order-4가 있으면 그걸 쓰고, 없으면 fallback으로 큰 블록을 훔친다. 작은 order는 다른 활동 때문에 실행마다 다르니 두 번 이상 본다.
3. (코드) `f9_cache`에 생성자(ctor)를 붙여 `sensor_id`를 0xff로 초기화하고, `kmem_cache_alloc`(zalloc 아님)으로 받은 객체의 값을 찍어라. ctor가 "할당마다"가 아니라 "slab이 만들어질 때 객체마다" 불린다는 것을 카운터로 확인하라. — 정답/힌트: ctor 호출 수는 1000이 아니라 slab 10장 × 102 = 1020번이다. `kmem_cache_zalloc`을 쓰면 ctor 결과가 0으로 덮인다는 점도 확인.
4. (코드) 예제 10에 `flag=noretry`(`GFP_KERNEL | __GFP_NORETRY`)를 추가해 B 상태에서 몇 페이지를 받는지 재 보라. KERNEL·ATOMIC 사이 어디쯤일 거라 예상하는가? — 정답/힌트: NORETRY도 direct reclaim은 한 번 시도하므로 ATOMIC보다는 많이 받을 가능성이 크다. 결과는 직접 측정해서 확인하라(이 노트에서는 돌리지 않았다).
5. (관찰) `slab_debug=FZPU,kmalloc-64`로 범위를 줄여 예제 11을 다시 돌리고, `kmalloc-128`의 `/proc/slabinfo` objsize가 디버그 없을 때와 같은지 확인하라. — 정답/힌트: 지정한 캐시만 메타데이터가 붙어 커지고 나머지는 원래 크기 그대로다.
6. (관찰) `page_owner=on`으로 부팅해서 `f9_buddy order=3 count=50`을 넣은 뒤, `/po.txt`에서 `f9_buddy_init`이 들어간 레코드 수를 세라. 50이 나오는가? — 정답/힌트: `awk 'BEGIN{RS=""} /f9_buddy_init/' /po.txt` 후 `grep -c "^Page allocated"`. 블록당 레코드 하나라 order-3 레코드는 50개다. 그런데 `vcalloc`이 잡은 페이지(포인터 배열, 그리고 vmalloc 페이지 테이블)도 스택에 `f9_buddy_init`이 있으므로 함께 걸린다 — `order 3`으로 한 번 더 걸러라.
7. (설계) 1 kHz IMU + 30 fps 카메라 + CAN을 다루는 드라이버 세트의 메모리 할당 계획을 표로 써라: 무엇을 언제(probe/open/IRQ), 어떤 API·GFP로. — 정답/힌트: 장치 상태 `devm_kzalloc`(probe), IMU 링 `devm_kcalloc`(probe), 카메라 버퍼 videobuf2 dma-contig(CMA, 스트림 시작 전), CAN 프레임은 미리 잡은 풀 또는 `GFP_ATOMIC` + 드롭 카운터.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| page frame | 물리 페이지 | 4 KiB 물리 메모리 한 칸 |
| pfn | page frame number | 물리 주소 >> 12. 페이지 번호 |
| `struct page` | 페이지 기술자 | 물리 페이지마다 하나, 64 B. 참조 수·플래그·소속 |
| folio | 페이지 묶음 타입 | 여러 페이지를 한 단위로 다루는 `struct page`의 상위 타입 (6.x mm 전환 중) |
| vmemmap | `struct page` 배열 | pfn으로 바로 인덱싱되는 가상 배열 |
| linear map | 직접 매핑 | 물리 RAM 전체를 고정 오프셋으로 매핑한 커널 구역 |
| node | NUMA 노드 | 메모리 묶음. 임베디드는 보통 1개 |
| zone | 메모리 구역 | 주소 제한별 구간: DMA, DMA32, NORMAL, MOVABLE |
| watermark | 워터마크 | zone의 min/low/high free 기준선 |
| buddy allocator | 버디 할당자 | 2^order 블록 분할·병합 방식의 페이지 할당자 |
| order | 차수 | 블록 크기 = 2^order 페이지 |
| migratetype | 이동 타입 | Unmovable/Movable/Reclaimable/CMA — 단편화 방지용 분류 |
| pageblock | 페이지 블록 | migratetype 단위 (여기선 512 페이지 = 2 MiB) |
| pcp | per-CPU pageset | CPU별 작은 order 페이지 캐시 |
| external fragmentation | 외부 단편화 | free는 많은데 큰 연속 블록이 없는 상태 |
| compaction | 압축 | movable 페이지를 옮겨 큰 free 블록을 만드는 일 |
| reclaim | 회수 | 페이지 캐시 등을 버리거나 써내서 free 페이지를 만드는 일 |
| kswapd | 백그라운드 회수 스레드 | low 아래면 깨어나 high까지 회수 |
| direct reclaim | 직접 회수 | 할당하는 스레드가 스스로 회수 (잠들 수 있음) |
| slab | 객체 슬랩 | 같은 크기 객체로 잘린 페이지 묶음 |
| SLUB | 현 slab 구현 | `mm/slub.c`. 6.8부터 유일한 구현 |
| `kmem_cache` | 객체 캐시 | 크기 하나짜리 객체 풀 |
| slab merge | 캐시 병합 | 크기·플래그 같은 캐시를 하나로 합치는 기본 동작 |
| size class | 크기 등급 | `kmalloc-8` … `kmalloc-8k` |
| GFP | Get Free Pages 플래그 | 할당 중 허용되는 행동(잠들기, I/O, 예비분)을 나타내는 비트 |
| CMA | Contiguous Memory Allocator | movable 페이지만 빌려줬다가 큰 연속 버퍼로 회수하는 영역 (F10) |
| guard page | 감시 페이지 | 매핑하지 않은 페이지. 넘치면 즉시 fault |
| redzone / poison | 레드존 / 독 | slab 디버그의 경계 패턴(0xcc/0xbb) / 해제 객체 채움(0x6b) |
| page_owner | 페이지 소유자 추적 | 모든 페이지 할당의 스택·GFP·pid 기록 |
| KASLR | 커널 주소 무작위화 | 커널 이미지·모듈 위치를 부팅마다 바꿈 |

---

## 16. 요약 & 체크리스트

커널 메모리는 층으로 되어 있다. 물리 RAM은 node → zone → 4 KiB 페이지로 나뉘고, 페이지마다 64 B `struct page`가 vmemmap 배열에 있다. arm64 커널 주소 공간에서 linear map만 물리와 고정 오프셋 관계이고(`__pa`/`__va`/`virt_to_page`는 여기서만 유효), vmalloc·스택·모듈·ioremap은 페이지 테이블로 매핑된다. 물리 페이지를 실제로 나눠 주는 것은 buddy 할당자로, 2^order 블록을 쪼개고 합치며, migratetype과 compaction으로 단편화를 줄이지만 커널이 잡은 unmovable 페이지는 옮길 수 없어 오래 돈 장비에서 큰 연속 할당이 실패한다. SLUB는 buddy 페이지를 고정 크기 객체로 잘라 CPU별 freelist에서 빠르게 내주며, `kmalloc`은 8 B~8 KiB 등급 캐시를 쓰고 그보다 크면 buddy로 간다. `vmalloc`은 가상으로만 연속이라 크게 잡을 수 있지만 느리고 DMA에 못 쓴다. 모든 할당은 GFP로 "잠들어도 되나, 예비분을 써도 되나"를 밝히며, process context는 `GFP_KERNEL`(회수까지 해서 성공시킴), atomic 문맥은 `GFP_ATOMIC`(예비분까지 쓰지만 회수 못 함)이다. 버그는 `slab_debug`(redzone·poison·추적)와 `page_owner`로 잡는다.

- [ ] node, zone, page의 관계와 arm64에서 `ZONE_DMA` 범위가 정해지는 방식을 설명할 수 있다
- [ ] arm64 커널 가상 주소 지도(linear, vmalloc, modules, vmemmap, fixmap)를 그리고 KASLR의 영향을 말할 수 있다
- [ ] `virt_to_phys`, `__va`, `page_to_pfn`, `pfn_to_page`, `page_address`, `vmalloc_to_page`를 언제 쓰는지 안다
- [ ] buddy 할당자의 분할·병합과 `/proc/buddyinfo`·`/proc/pagetypeinfo`를 읽을 수 있다
- [ ] "free 메모리는 있는데 할당이 실패한다"를 단편화·CMA·GFP로 진단할 수 있다
- [ ] `page allocation failure` 경고에서 order, GFP 마스크, 실패 지점, order별 free 블록을 읽어낼 수 있다
- [ ] `kmem_cache`를 만들고 `/proc/slabinfo`·`/sys/kernel/slab`으로 확인할 수 있다 (merge 포함)
- [ ] `kmalloc` 크기 등급과 낭비, 8 KiB 경계를 설명할 수 있다
- [ ] kmalloc / kzalloc / kcalloc / kvmalloc / vmalloc / alloc_pages / devm_kzalloc / dma_alloc_coherent 중 상황에 맞는 것을 고를 수 있다
- [ ] GFP_KERNEL, GFP_ATOMIC, GFP_NOWAIT, GFP_NOIO/NOFS의 차이를 워터마크와 회수 동작으로 설명할 수 있다
- [ ] IRQ 핸들러에서의 할당 전략(ATOMIC + 실패 처리, 또는 미리 할당·mempool·workqueue)을 코드로 짤 수 있다
- [ ] `slab_debug`와 `page_owner` 출력을 읽고 범인 코드를 찾을 수 있다

---

## 참고 자료

- docs.kernel.org — Memory Allocation Guide: https://docs.kernel.org/core-api/memory-allocation.html
- docs.kernel.org — Memory Management APIs (GFP 플래그 설명 포함): https://docs.kernel.org/core-api/mm-api.html
- docs.kernel.org — Short users guide for SLUB (`slab_debug` 플래그): https://docs.kernel.org/mm/slub.html
- docs.kernel.org — page owner: https://docs.kernel.org/mm/page_owner.html
- docs.kernel.org — Memory Layout on AArch64 Linux: https://docs.kernel.org/arch/arm64/memory.html
- docs.kernel.org — Physical Memory (node, zone): https://docs.kernel.org/mm/physical_memory.html
- 커널 소스 (랩 `/var/lab/src/linux-6.18.55`, 또는 elixir.bootlin.com): `include/linux/gfp_types.h`(GFP 비트), `mm/page_alloc.c`(`__zone_watermark_ok`, `__alloc_pages_slowpath`, `warn_alloc`), `mm/slub.c`, `mm/vmalloc.c`, `mm/page_owner.c`, `include/linux/slab.h`(`KMALLOC_*`), `arch/arm64/include/asm/memory.h`, `arch/arm64/include/asm/pgtable.h`
- Mel Gorman, "Understanding the Linux Virtual Memory Manager" (2.6 시절 책이지만 zone·buddy·slab 개념 설명은 여전히 유효)
- Bootlin, Linux Kernel and Driver Development 교육 슬라이드 — "Memory management" 장 (bootlin.com/training/kernel)
- OSTEP 17장 "Free-Space Management"(buddy allocation, slab), 18~20장(페이징) — OSTEP 노트와 연결
- 이 노트와 이어지는 노트: A5(유저 관점 VM), E4 §6(할당 비용 측정, `KMALLOC_MAX_SIZE`), F2(실행 컨텍스트), F10(CMA), F11(페이지 테이블·OOM), G6(devres), H8(DMA API)
