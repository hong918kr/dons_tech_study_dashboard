# O4. 가속기와 Linux — DRM accel, GEM, dma-buf, fence, 그리고 NPU 드라이버의 실제 구조

> **이 노트를 다 읽으면**: NPU/GPU 드라이버가 유저에게 주는 4가지(버퍼·제출·대기·공유)를 ioctl 수준에서 설명 · dma-buf heap·sw_sync·vgem·udmabuf로 버퍼와 fence를 직접 다뤄 봄 · `/dev/accel/accel0`을 만드는 장난감 accel 드라이버를 빌드해 실행 · in-tree NPU 드라이버 5개(rocket, ivpu, qaic, amdxdna, habanalabs)를 읽고 비교
> **왜 필요한가**: AI 가속기 칩 회사의 "NPU kernel driver / runtime / SoC integration" 면접은 결국 "버퍼를 어떻게 잡고, 작업을 어떻게 넣고, 끝난 걸 어떻게 알고, 카메라 프레임을 복사 없이 어떻게 넘기나"를 묻는다
> **Don 기준 난이도**: 링 버퍼·doorbell·DMA·완료 인터럽트·펌웨어 부팅은 SSD 펌웨어에서 이미 아는 것 / DRM GEM 객체 모델, dma-buf와 fence의 커널 객체, drm_sched, Linux 쪽 메모리 공유 규칙이 새로 배울 부분
> **선행 노트**: H9(DMA API·IOMMU·dma-buf 기초), I8(DRM/KMS·GEM·PRIME·accel 개요), F10(CMA·reserved-memory·dma-buf heaps 소개), A7(SCM_RIGHTS fd 전달), G5(ioctl·mmap), G6(poll), G10(firmware 로딩), N6(runtime PM) — 이어서 O5(SoC 플랫폼 지형), Q2(카메라 파이프라인)

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 SoC 안의 NPU는 "DMA 엔진 달린 주변장치 + 작은 펌웨어 CPU"다

Don이 SSD 컨트롤러에서 본 구조를 떠올려 보자. 호스트(PCIe)가 **submission queue**에 명령을 쓰고 **doorbell** 레지스터를 두드리면, 컨트롤러 안의 Cortex-R 펌웨어가 명령을 꺼내 NAND/DMA 엔진을 돌리고, 끝나면 **completion queue**에 결과를 쓰고 MSI를 쏜다. 호스트 쪽 NVMe 드라이버는 큐 메모리를 DMA 가능하게 잡고, 완료 인터럽트에서 대기 중인 요청을 깨운다.

NPU(Neural Processing Unit)도 거의 같다. 차이는 세 가지다.

1. **명령의 내용**이 "LBA 읽기"가 아니라 "이 텐서들로 이 레이어(합성곱·행렬곱)를 계산" — 명령 버퍼를 만드는 컴파일러가 유저 공간에 있다.
2. **메모리가 크고 공유된다**: 모델 가중치(수 MB~수 GB), 입력 프레임, 중간 텐서를 CPU·카메라 ISP·GPU·NPU가 같은 DRAM에서 나눠 쓴다. 복사하면 대역폭이 바닥난다.
3. **여러 프로세스가 동시에 쓴다**: 그래서 장치 MMU(IOMMU/SMMU)로 프로세스마다 주소 공간을 나누고, 작업 사이 의존성을 커널이 관리한다.

Linux는 이 문제를 GPU에서 이미 20년 가까이 풀어 왔다. 그래서 NPU용 서브시스템을 새로 만들지 않고 **DRM(Direct Rendering Manager)의 갈래로 `drivers/accel/`**을 만들었다(I8 §11). GPU에서 쓰던 부품 — **GEM**(버퍼 객체), **PRIME/dma-buf**(버퍼 공유), **dma-fence/sync_file**(완료 신호), **drm_sched**(작업 스케줄러), **drm_gpuvm**(장치 가상 주소 관리) — 을 그대로 쓴다.

```svg
<svg viewBox="0 0 680 520" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="660" height="36" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="33" text-anchor="middle" font-size="13">앱 · 프레임워크: TFLite, ONNX Runtime, PyTorch, GStreamer, ROS 2 노드</text>
  <rect x="10" y="58" width="200" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="81" text-anchor="middle" font-size="13">Mesa Teflon delegate</text>
  <rect x="240" y="58" width="200" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="81" text-anchor="middle" font-size="13">벤더 SDK (RKNN, TIDL 등)</text>
  <rect x="470" y="58" width="200" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="570" y="81" text-anchor="middle" font-size="13">OpenCL / Vulkan compute</text>
  <rect x="10" y="106" width="660" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="129" text-anchor="middle" font-size="13">UMD (유저 모드 드라이버): 모델 → 명령 버퍼 컴파일, BO 할당, 제출, 대기</text>
  <line x1="10" y1="160" x2="670" y2="160" stroke="currentColor" stroke-dasharray="6 4"/>
  <text x="20" y="153" font-size="12">user</text>
  <text x="20" y="177" font-size="12">kernel</text>
  <text x="360" y="177" font-size="12">ioctl · mmap · poll · fd 전달(SCM_RIGHTS)</text>
  <rect x="10" y="190" width="155" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="87" y="213" text-anchor="middle" font-size="13">/dev/accel/accelN</text>
  <rect x="178" y="190" width="155" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="255" y="213" text-anchor="middle" font-size="13">/dev/dri/renderD*</text>
  <rect x="346" y="190" width="155" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="423" y="213" text-anchor="middle" font-size="13">/dev/dma_heap/*</text>
  <rect x="514" y="190" width="156" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="592" y="213" text-anchor="middle" font-size="13">/dev/video* (V4L2)</text>
  <rect x="10" y="244" width="390" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="205" y="266" text-anchor="middle" font-size="13">DRM core (drivers/gpu/drm, drivers/accel)</text>
  <text x="205" y="288" text-anchor="middle" font-size="12">GEM · PRIME · drm_sched · drm_gpuvm · fdinfo · debugfs</text>
  <rect x="414" y="244" width="256" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="542" y="266" text-anchor="middle" font-size="13">drivers/dma-buf</text>
  <text x="542" y="288" text-anchor="middle" font-size="12">dma_buf · dma_resv · dma_fence</text>
  <rect x="10" y="318" width="390" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="205" y="340" text-anchor="middle" font-size="13">KMD (커널 모드 드라이버)</text>
  <text x="205" y="362" text-anchor="middle" font-size="12">rocket · ivpu · qaic · amdxdna · habanalabs · etnaviv</text>
  <rect x="414" y="318" width="256" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="542" y="340" text-anchor="middle" font-size="13">IOMMU · runtime PM</text>
  <text x="542" y="362" text-anchor="middle" font-size="12">SMMU/IOMMU 드라이버, clk, reset</text>
  <line x1="10" y1="392" x2="670" y2="392" stroke="currentColor" stroke-dasharray="2 4"/>
  <text x="20" y="409" font-size="12">hardware</text>
  <rect x="10" y="420" width="200" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="110" y="442" text-anchor="middle" font-size="13">NPU 코어</text>
  <text x="110" y="464" text-anchor="middle" font-size="12">+ 제어용 MCU 펌웨어</text>
  <rect x="240" y="420" width="200" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="442" text-anchor="middle" font-size="13">IOMMU / SMMU</text>
  <text x="340" y="464" text-anchor="middle" font-size="12">장치 주소 → 물리 주소</text>
  <rect x="470" y="420" width="200" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="570" y="442" text-anchor="middle" font-size="13">공유 DRAM</text>
  <text x="570" y="464" text-anchor="middle" font-size="12">CPU · ISP · GPU · NPU</text>
  <line x1="210" y1="448" x2="236" y2="448" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="440" y1="448" x2="466" y2="448" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="142" x2="340" y2="186" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="205" y1="226" x2="205" y2="240" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="205" y1="300" x2="205" y2="314" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="110" y1="374" x2="110" y2="416" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="340" y="505" text-anchor="middle" font-size="12">유저 공간에는 "fd 몇 개 + ioctl 몇 개"만 보인다 — 나머지는 전부 커널 공용 부품</text>
</svg>
```

그림 1 — 가속기 소프트웨어 스택. 위 세 줄이 유저 공간(런타임·UMD), 가운데가 커널 공용 부품(DRM core, dma-buf), 아래가 장치별 KMD와 하드웨어다. 벤더마다 다른 것은 주로 UMD(컴파일러)와 KMD 아래쪽 절반이다.

### 0.2 가속기 드라이버가 유저에게 주는 것은 딱 네 가지

어떤 NPU 드라이버를 열어 봐도 uAPI는 결국 아래 네 가지로 정리된다. 이 노트는 이 네 가지를 하나씩 손으로 만져 본다.

| 기능 | 유저가 보는 것 | 커널 객체 | 이 노트의 실습 |
|---|---|---|---|
| 버퍼 | `CREATE_BO` ioctl → handle, `mmap(fd, offset)` | `drm_gem_object` (+ shmem 페이지, IOMMU 매핑) | §2 GEM, §8 장난감 accel |
| 제출 | `SUBMIT`/`EXECUTE` ioctl (명령 버퍼 주소 + 입출력 BO 목록) | job, 링 버퍼, `drm_sched_job` | §8, §9 |
| 대기 | `poll()`/`WAIT_BO`/sync_file fd | `dma_fence`, `dma_resv` | §5 sw_sync, §6 implicit fence |
| 공유 | dma-buf fd (PRIME export/import, heap, udmabuf) | `struct dma_buf` | §3 heap + SCM_RIGHTS, §6 PRIME, §7 udmabuf |

### 0.3 이 노트의 랩 환경

- VM 호스트: Lima Ubuntu `elinux` (aarch64, `uname -r` = `7.0.0-38-generic`). C 예제는 여기서 `gcc -static`으로 빌드한다.
- 게스트 커널: 공용 랩의 `/var/lab/out/Image-dom` (Linux 6.18.55-dom). DRM accel, vgem, vkms, dma-buf heaps(system, cma), udmabuf, sw_sync, `DMABUF_DEBUG`이 켜진 빌드다. 게스트는 QEMU `virt` + TCG(하드웨어 가속 없음)이고, BusyBox initramfs에 내 바이너리와 필요한 `.ko`만 넣어 부팅한다(§1.2).
- **진짜 NPU는 없다.** 그래서 "NPU 역할"은 vgem(가짜 GPU), sw_sync(소프트웨어 fence), 그리고 직접 만든 `o4_accel` 모듈이 맡는다. 실제 NPU 드라이버는 랩 커널 소스(`/var/lab/src/linux-6.18.55`)를 읽어서 정리한다 — 소스에서 인용한 것은 "소스 확인", 기억에 의존한 것은 "기억 기반"이라고 표시한다.
- **TCG 경고**: 이 노트의 시간 측정값은 다른 작성자 18명과 공유하는 VM 위의 소프트웨어 에뮬레이션 결과다. 절대값은 실제 보드와 전혀 다르고, **상대 비교(복사 vs 무복사)만** 의미가 있다.

## 1. DRM vs compute accel — 노드 하나 차이, 철학은 같다

### 1.1 직관

GPU 드라이버는 두 종류의 노드를 만든다: 화면을 다루는 `/dev/dri/cardN`(KMS, 권한 높음)과 계산만 하는 `/dev/dri/renderD128`(I8 §9.2). NPU는 화면이 없으니 render 노드만 있으면 될 것 같지만, 그러면 Mesa·Xorg 같은 그래픽 스택이 "새 GPU다!" 하고 NPU를 열어 보려 한다. 그래서 커널은 **같은 `struct drm_device`를 쓰되 노드 이름과 major 번호만 다른** 장치 종류를 만들었다. 랩 소스 `Documentation/accel/introduction.rst`의 규칙을 정리하면 이렇다(소스 확인).

| 항목 | GPU render | compute accel |
|---|---|---|
| 드라이버 위치 | `drivers/gpu/drm/<drv>` | `drivers/accel/<drv>` |
| feature 플래그 | `DRIVER_RENDER` (+ `DRIVER_MODESET`) | `DRIVER_COMPUTE_ACCEL` — RENDER·MODESET과 **동시에 켤 수 없다** |
| 문자 장치 | `/dev/dri/renderD12x`, major 226 | `/dev/accel/accelN`, major 261 (`ACCEL_MAJOR`) |
| sysfs / debugfs | `/sys/class/drm/` , `/sys/kernel/debug/dri/N` | `/sys/class/accel/accelN/`, `/sys/kernel/debug/accel/N/` |
| fops | `DEFINE_DRM_GEM_FOPS` | `DEFINE_DRM_ACCEL_FOPS` (`.open = accel_open`) |
| 공용 부품 | GEM, PRIME, drm_sched, drm_gpuvm, syncobj, fdinfo | 똑같이 사용 |

그래픽과 계산 노드를 둘 다 내야 하는 장치(예: GPU 안의 NPU 블록)는 드라이버를 둘로 나누고 auxiliary bus로 잇는다고 문서에 적혀 있다.

### 1.2 예제 1 — 랩 게스트에서 보이는 버퍼·fence 인프라

무엇을 확인하나: Image-dom 커널에서 이 노트가 쓸 장치 노드(`/dev/dma_heap/*`, `/dev/udmabuf`)와 debugfs(`dma_buf/bufinfo`, `sync/sw_sync`)가 실제로 있는지. 먼저 VM에서 커널 설정을 확인하고, BusyBox initramfs에 예제 바이너리를 넣어 부팅한다.

```sh
# (VM) 이 노트가 의존하는 커널 옵션
grep -E "^(# )?CONFIG_(DRM_ACCEL|DRM_ACCEL_ROCKET|DRM_VGEM|DRM_VKMS|DRM_GEM_SHMEM_HELPER|DMABUF_HEAPS|DMABUF_HEAPS_SYSTEM|DMABUF_HEAPS_CMA|UDMABUF|SYNC_FILE|SW_SYNC|DMABUF_DEBUG|CMA_SIZE_MBYTES|DRM_SCHED|ARM_SMMU_V3)[ =]" /var/lab/out/linux-build-dom/.config
```

```text
CONFIG_DRM_GEM_SHMEM_HELPER=m
CONFIG_DRM_SCHED=m
CONFIG_DRM_VGEM=m
CONFIG_DRM_VKMS=m
CONFIG_DRM_ACCEL=y
CONFIG_DRM_ACCEL_ROCKET=m
CONFIG_SYNC_FILE=y
CONFIG_SW_SYNC=y
CONFIG_UDMABUF=y
CONFIG_DMABUF_DEBUG=y
CONFIG_DMABUF_HEAPS=y
CONFIG_DMABUF_HEAPS_SYSTEM=y
CONFIG_DMABUF_HEAPS_CMA=y
CONFIG_ARM_SMMU_V3=y
CONFIG_CMA_SIZE_MBYTES=32
```

랩 스크립트 세 개(모든 예제가 공유). `mkinitrd.sh`는 랩 BusyBox rootfs를 복사하고, 정적 빌드한 예제와 필요한 `.ko`만 골라 넣는다(모듈 트리 전체 97 MB를 넣지 않는다). `run.sh`는 게스트 안에서 예제 이름별로 명령을 실행한다.

```sh
#!/bin/sh
# mkinitrd.sh — /var/tmp/O4/rootfs (BusyBox 랩 rootfs 복사본) + O4 바이너리 + 필요한 .ko → o4.cpio.gz
set -e
R=/var/tmp/O4/rootfs; M=/var/lab/out/modules-dom/lib/modules/6.18.55-dom; S=/Users/donh/.elinux_scratch/O4
rm -rf $R; mkdir -p $R; cp -a /var/lab/build/rootfs/. $R/
mkdir -p $R/o4 $R/lib/modules/6.18.55-dom
cp $M/modules.* $R/lib/modules/6.18.55-dom/
for k in drivers/gpu/drm/drm_kms_helper drivers/gpu/drm/drm_shmem_helper drivers/gpu/drm/vgem/vgem \
         drivers/gpu/drm/vkms/vkms drivers/gpu/drm/clients/drm_client_lib drivers/gpu/drm/scheduler/gpu-sched \
         drivers/accel/rocket/rocket; do
  mkdir -p $R/lib/modules/6.18.55-dom/kernel/$(dirname $k); cp $M/kernel/$k.ko $R/lib/modules/6.18.55-dom/kernel/$k.ko
done
cp /tmp/O4/heap_share /tmp/O4/swsync /tmp/O4/prime /tmp/O4/implicit /tmp/O4/udmabuf /tmp/O4/copybench /tmp/O4/accel_user $R/o4/
cp /var/tmp/O4/mod/o4_accel.ko $S/run.sh $R/o4/
(cd $R && find . | cpio -o -H newc --quiet | gzip -1) > /var/tmp/O4/o4.cpio.gz
ls -la /var/tmp/O4/o4.cpio.gz
```

```sh
#!/bin/sh
# boot.sh <ex> [extra kernel args] — Image-dom + o4 initramfs로 예제 하나 실행 후 poweroff
EX=$1; shift
timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1024M -nographic \
  -kernel /var/lab/out/Image-dom -initrd /var/tmp/O4/o4.cpio.gz \
  -append "console=ttyAMA0 quiet $* LAB_CMD=sh~/o4/run.sh~$EX" 2>&1 | tr -d '\r' | sed 's/\x1b\[[0-9;]*m//g' | grep -v 'reboot: Power down'
```

```sh
#!/bin/sh
# run.sh <ex> — 게스트 안에서 O4 예제 하나를 실행 (initramfs의 /o4)
cd /o4
case "$1" in
env)
  uname -r
  ls -l /dev/dma_heap/ /dev/udmabuf
  ls /sys/kernel/debug/dma_buf/ /sys/kernel/debug/sync/
  grep -i cma /proc/meminfo
  ;;
heap)  ./heap_share ;;
cma)   ./heap_share /dev/dma_heap/default_cma_region ;;
swsync) ./swsync ;;
prime) modprobe vgem && modprobe vkms && ls -l /dev/dri && ./prime ;;
implicit) modprobe vgem && ./implicit ;;
udma)  ./udmabuf ;;
bench) for r in 1 2 3; do for s in 65536 1048576 3110400 8294400; do ./copybench $s 20; done; done ;;
accel)
  modprobe drm_shmem_helper && insmod /o4/o4_accel.ko latency_ms=200
  dmesg | grep -i "o4_accel\|drm" | tail -3
  ls -l /dev/accel/
  ls /sys/class/accel/ ; ls -l /sys/class/accel/accel0/device
  ls /sys/kernel/debug/accel/
  ./accel_user
  ;;
*) sh -c "$*" ;;
esac
```

C 예제는 모두 VM에서 `gcc -std=gnu11 -Wall -Wextra -O2 -static -pthread <파일>.c -o /tmp/O4/<이름>`으로 빌드했고 경고는 0개였다. 첫 예제 실행:

```sh
sh boot.sh env
```

```text
lab initramfs: 6.18.55-dom up 7.42s
6.18.55-dom
crw-------    1 0        0          10, 257 Jan  1  1970 /dev/udmabuf

/dev/dma_heap/:
total 0
crw-------    1 0        0         250,   1 Jan  1  1970 default_cma_region
crw-------    1 0        0         250,   2 Jan  1  1970 reserved
crw-------    1 0        0         250,   0 Jan  1  1970 system
/sys/kernel/debug/dma_buf/:
bufinfo

/sys/kernel/debug/sync/:
info     sw_sync
CmaTotal:          32768 kB
CmaFree:           32384 kB
```

출력에서 볼 것:

- heap이 세 개다. `system`(흩어진 페이지), `default_cma_region`(기본 CMA에서 물리 연속), 그리고 `reserved` — F10 §8.1에서 소스로만 봤던 **레거시 이름**(`CONFIG_DMABUF_HEAPS_CMA_LEGACY`)이 실제로 같은 CMA를 한 번 더 노출한 것이다. 예전 BSP 문서의 `/dev/dma_heap/linux,cma`가 이 자리에 있던 이름이다.
- `/dev/udmabuf`는 misc 장치(major 10), heap은 자기 major(250, 동적)를 받는다.
- 이 랩 이미지의 CMA는 32 MiB(`CONFIG_CMA_SIZE_MBYTES=32`). 실제 카메라+NPU 보드는 수백 MiB를 잡는다(F10 §10.1).

### 1.3 흔한 함정

- 컨테이너나 서비스 계정에 `/dev/accel/accel0`, `/dev/dma_heap/*` 권한이 없다. 위 출력처럼 기본 권한이 `crw-------`(root만)이다. 실제 제품은 udev 규칙으로 그룹(`render` 또는 전용 그룹)을 준다(L2).
- "NPU 드라이버가 안 뜬다"를 커널 설정부터 의심하지 않는다: accel 드라이버는 `CONFIG_DRM_ACCEL`이 꺼져 있으면 Kconfig에서 아예 보이지 않는다(`depends on DRM_ACCEL`). I8 §11.2의 랩 커널(Image-sub)은 실제로 이 옵션이 꺼져 있었다.

---

## 2. GEM — 가속기 버퍼 객체

### 2.1 직관 — "handle은 fd 안에서만 통하는 번호표"

GEM(Graphics Execution Manager)은 DRM의 버퍼 객체 `struct drm_gem_object`다(I8 §9.4). Don이 SSD 펌웨어에서 쓰던 "버퍼 디스크립터 테이블"과 비슷하다. 차이는 다음과 같다.

- 유저는 객체를 **handle**(32비트 정수)로 부른다. handle은 `struct drm_file`(= 그 장치 fd 하나)의 idr 테이블 인덱스라서, **다른 fd나 다른 프로세스에서는 의미가 없다** — fd 번호가 프로세스마다 다른 것과 같다.
- CPU 매핑은 handle → "가짜 mmap 오프셋"(`drm_vma_node_offset_addr`) → `mmap(dev_fd, offset)` 순서다. 예제 8에서 오프셋이 `0x100000000`처럼 큰 값으로 나오는 이유다.
- 장치가 보는 주소는 드라이버가 정한다: IOMMU가 있으면 프로세스별 장치 주소 공간(IOVA)에 매핑하고 그 주소를 돌려준다(rocket의 `dma_address`, §9.1).
- 메모리 성격은 헬퍼로 고른다: `drm_gem_shmem_helper`(흩어진 shmem 페이지 + sg_table, IOMMU 있는 NPU), `drm_gem_dma_helper`(CMA 연속, IOMMU 없는 장치), TTM(전용 VRAM).

### 2.2 in-tree NPU 드라이버의 ioctl 목록 (랩 소스 확인)

랩 커널 트리에서 `DRM_IOCTL_DEF_DRV` 줄만 뽑으면 각 드라이버 uAPI의 전체 모양이 보인다(명령: `grep -rhn DRM_IOCTL_DEF_DRV drivers/accel/<drv>/*.c`, 실행 결과를 표로 정리).

| 드라이버 | 버퍼 | 제출 | 대기·동기화 | 그 밖 |
|---|---|---|---|---|
| rocket (Rockchip RK3588) | `CREATE_BO` | `SUBMIT` | `PREP_BO`(CPU 접근 전 대기+캐시), `FINI_BO` | — |
| ivpu (Intel NPU) | `BO_CREATE`, `BO_INFO` | `SUBMIT`, `CMDQ_CREATE/DESTROY/SUBMIT` | `BO_WAIT` | `GET/SET_PARAM`, `METRIC_STREAMER_*` |
| qaic (Qualcomm Cloud AI 100) | `CREATE_BO`, `MMAP_BO`, `ATTACH_SLICE_BO` | `EXECUTE_BO`, `PARTIAL_EXECUTE_BO` | `WAIT_BO` | `MANAGE`(펌웨어 제어 채널), `PERF_STATS_BO` |
| amdxdna (AMD Ryzen AI) | `CREATE_BO`, `GET_BO_INFO`, `SYNC_BO` | `EXEC_CMD` | syncobj(timeline) | `CREATE/DESTROY/CONFIG_HWCTX`, `GET_INFO`, `SET_STATE`(root) |
| habanalabs (Intel Gaudi) | 자체 ioctl 테이블 (`HL_IOCTL_DEF`) | CS(command submission) | CS wait | 자체 info |

읽는 법:

- 다섯 개 모두 **버퍼 / 제출 / 대기**의 세 축으로 나뉜다. 이름만 다르다.
- qaic의 "slice"는 BO 하나를 여러 DMA 전송 조각으로 나눈 것이다. 데이터센터 카드라서 호스트 메모리 → 카드 DRAM 전송을 BO 단위로 설명한다.
- amdxdna는 `DRIVER_SYNCOBJ | DRIVER_SYNCOBJ_TIMELINE`을 켠다(소스 `amdxdna_pci_drv.c:244`). Vulkan식 **timeline syncobj**(§5.4)로 대기한다.
- habanalabs는 DRM 이전부터 있던 드라이버라 `driver_features = DRIVER_COMPUTE_ACCEL`만 켜고 GEM도 쓰지 않으며, ioctl 디스패치를 직접 한다(`habanalabs_ioctl.c`의 `_hl_ioctl`). "accel 노드 이름만 빌린 레거시 드라이버"의 예다.

### 2.3 흔한 함정

- GEM handle을 정수로 다른 프로세스에 보낸다: 그쪽 fd 테이블에선 없는 handle이다. **dma-buf fd로 export해서 `SCM_RIGHTS`로** 넘긴다(§3).
- handle을 닫지 않는다(`DRM_IOCTL_GEM_CLOSE`): 프로세스가 오래 살면(추론 데몬) BO가 쌓인다. 드라이버가 `drm_show_memory_stats`를 지원하면 `/proc/<pid>/fdinfo/<fd>`의 `drm-memory-*`로 추적한다.

---

## 3. dma-buf — 드라이버 사이의 버퍼 계약

### 3.1 직관

dma-buf는 "**버퍼 하나 = 파일 하나**"다. 커널이 버퍼를 `struct file`로 감싸 주니 유저 공간에선 fd로 보이고, fd라서 `dup`, `SCM_RIGHTS`, `close`, `mmap`, `poll`이 전부 그대로 통한다. 버퍼를 만든 쪽(**exporter**: heap, V4L2, GPU, NPU)이 `struct dma_buf_ops`를 구현하고, 빌려 쓰는 쪽(**importer**)은 `dma_buf_attach(buf, 자기 dev)` → `dma_buf_map_attachment()`로 **자기 장치 입장의** sg_table(DMA 주소 목록)을 받는다. H9 §11에서 한 모듈 안에 exporter와 importer를 만들어 봤다. 여기서는 유저 공간에서 보이는 모습을 본다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="200" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="33" text-anchor="middle" font-size="13">카메라 프로세스</text>
  <text x="110" y="54" text-anchor="middle" font-size="12">fd 5 = dma-buf</text>
  <rect x="470" y="10" width="200" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="570" y="33" text-anchor="middle" font-size="13">NPU 런타임 프로세스</text>
  <text x="570" y="54" text-anchor="middle" font-size="12">fd 3 = 같은 dma-buf</text>
  <line x1="214" y1="38" x2="466" y2="38" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="340" y="30" text-anchor="middle" font-size="12">sendmsg(SCM_RIGHTS)</text>
  <line x1="10" y1="84" x2="670" y2="84" stroke="currentColor" stroke-dasharray="6 4"/>
  <rect x="200" y="100" width="280" height="96" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="122" text-anchor="middle" font-size="13">struct file → struct dma_buf</text>
  <text x="340" y="144" text-anchor="middle" font-size="12">size, exp_name, ops (exporter 구현)</text>
  <text x="340" y="164" text-anchor="middle" font-size="12">resv: dma_resv (fence 목록)</text>
  <text x="340" y="184" text-anchor="middle" font-size="12">attachments: 장치별 연결</text>
  <line x1="110" y1="66" x2="230" y2="98" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="570" y1="66" x2="450" y2="98" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="10" y="230" width="200" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="110" y="253" text-anchor="middle" font-size="13">exporter</text>
  <text x="110" y="274" text-anchor="middle" font-size="12">heap · V4L2 · GPU · NPU</text>
  <rect x="240" y="230" width="200" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="253" text-anchor="middle" font-size="13">importer A (ISP)</text>
  <text x="340" y="274" text-anchor="middle" font-size="12">attach → map → sg_table</text>
  <rect x="470" y="230" width="200" height="56" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="570" y="253" text-anchor="middle" font-size="13">importer B (NPU)</text>
  <text x="570" y="274" text-anchor="middle" font-size="12">자기 IOMMU 주소로 매핑</text>
  <line x1="250" y1="196" x2="150" y2="226" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="340" y1="196" x2="340" y2="226" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="430" y1="196" x2="530" y2="226" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="10" y="320" width="660" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="343" text-anchor="middle" font-size="13">물리 페이지 (system heap: 흩어짐 · CMA heap: 연속)</text>
  <text x="340" y="364" text-anchor="middle" font-size="12">CPU는 mmap으로, 장치는 각자의 DMA 주소로 — 데이터는 한 번도 복사되지 않는다</text>
  <line x1="110" y1="286" x2="110" y2="316" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="340" y1="286" x2="340" y2="316" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="570" y1="286" x2="570" y2="316" stroke="currentColor" marker-end="url(#ah2)"/>
</svg>
```

그림 2 — dma-buf 공유. 두 프로세스의 fd는 번호가 달라도 같은 `struct file`을 가리킨다. 각 importer 장치는 attach할 때 자기 입장의 DMA 주소(IOMMU가 있으면 IOVA)를 받는다. 동기화 정보(fence)는 버퍼에 붙은 `dma_resv`에 모인다.

### 3.2 예제 2 — system heap에서 할당하고, fd를 다른 프로세스로 넘긴다

무엇을 확인하나: `DMA_HEAP_IOCTL_ALLOC`으로 4 MiB 버퍼를 받아 부모가 채우고, **fork 뒤에 할당한** fd를 `SCM_RIGHTS`로 자식에게 보내 자식이 mmap으로 같은 내용을 읽는다(자식은 fd를 상속받을 수 없으니 소켓이 유일한 경로). CPU 접근은 `DMA_BUF_IOCTL_SYNC`로 감싼다. 공용 헤더부터:

```c
// o4.h — O4 예제 공용: 에러 처리, fd 전달(SCM_RIGHTS), 시간, 파일 덤프
#pragma once
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>

#define DIE(msg) do { perror(msg); exit(1); } while (0)

static inline uint64_t now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

/* fd 하나를 UNIX 소켓으로 보낸다: 커널이 같은 struct file을 상대 fd 테이블에 설치 */
static inline void send_fd(int sock, int fd)
{
	char c = 'F';
	struct iovec iov = { .iov_base = &c, .iov_len = 1 };
	union { char buf[CMSG_SPACE(sizeof(int))]; struct cmsghdr al; } u;
	struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1,
			    .msg_control = u.buf, .msg_controllen = sizeof(u.buf) };
	struct cmsghdr *cm = CMSG_FIRSTHDR(&m);
	cm->cmsg_level = SOL_SOCKET; cm->cmsg_type = SCM_RIGHTS;
	cm->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(cm), &fd, sizeof(int));
	if (sendmsg(sock, &m, 0) != 1) DIE("sendmsg");
}

static inline int recv_fd(int sock)
{
	char c;
	struct iovec iov = { .iov_base = &c, .iov_len = 1 };
	union { char buf[CMSG_SPACE(sizeof(int))]; struct cmsghdr al; } u;
	struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1,
			    .msg_control = u.buf, .msg_controllen = sizeof(u.buf) };
	if (recvmsg(sock, &m, 0) != 1) DIE("recvmsg");
	struct cmsghdr *cm = CMSG_FIRSTHDR(&m);
	if (!cm || cm->cmsg_type != SCM_RIGHTS) { fprintf(stderr, "no fd\n"); exit(1); }
	int fd;
	memcpy(&fd, CMSG_DATA(cm), sizeof(int));
	return fd;
}
```

```c
// heap_share.c — system heap에서 dma-buf를 받고, fd를 다른 프로세스로 넘겨 같은 메모리를 본다
#include "o4.h"
#include <sys/wait.h>
#include <linux/dma-heap.h>
#include <linux/dma-buf.h>

static void cpu_sync(int fd, uint64_t flags)
{
	struct dma_buf_sync s = { .flags = flags };
	if (ioctl(fd, DMA_BUF_IOCTL_SYNC, &s) < 0) DIE("DMA_BUF_IOCTL_SYNC");
}

int main(int argc, char **argv)
{
	const char *heap = argc > 1 ? argv[1] : "/dev/dma_heap/system";
	size_t len = 4 << 20;                        /* 4 MiB "프레임" */
	int sv[2];
	if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) < 0) DIE("socketpair");
	/* fork를 먼저: 자식은 dma-buf fd를 상속받지 않고, 소켓으로만 받는다 */
	pid_t pid = fork();
	if (pid < 0) DIE("fork");
	if (pid == 0) {                              /* 소비자 프로세스 ("NPU 런타임") */
		close(sv[0]);
		int fd = recv_fd(sv[1]);
		uint8_t *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		if (p == MAP_FAILED) DIE("mmap child");
		cpu_sync(fd, DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ);
		printf("[child ] got fd=%d, first bytes: %02x %02x %02x, last=%02x\n",
		       fd, p[0], p[1], p[2], p[len - 1]);
		cpu_sync(fd, DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ);
		char cmd[192];
		snprintf(cmd, sizeof(cmd), "cat "
			 "/sys/kernel/debug/dma_buf/bufinfo; ls -l /proc/%d/fd/%d", getpid(), fd);
		if (system(cmd) < 0) DIE("system");
		_exit(0);
	}
	close(sv[1]);
	int hfd = open(heap, O_RDONLY | O_CLOEXEC);
	if (hfd < 0) DIE(heap);
	struct dma_heap_allocation_data a = { .len = len, .fd_flags = O_RDWR | O_CLOEXEC };
	if (ioctl(hfd, DMA_HEAP_IOCTL_ALLOC, &a) < 0) DIE("DMA_HEAP_IOCTL_ALLOC");
	int buf = a.fd;
	if (ioctl(buf, DMA_BUF_SET_NAME_B, "o4_frame0") < 0) DIE("SET_NAME");
	printf("[parent] heap=%s dmabuf fd=%d size=%zu lseek(END)=%ld\n", heap, buf, len,
	       (long)lseek(buf, 0, SEEK_END));

	uint8_t *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, buf, 0);
	if (p == MAP_FAILED) DIE("mmap parent");
	cpu_sync(buf, DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE);  /* CPU가 쓰기 시작 */
	for (size_t i = 0; i < len; i++) p[i] = (uint8_t)(i * 7);
	cpu_sync(buf, DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE);    /* 장치에게 넘김 */
	send_fd(sv[0], buf);
	waitpid(pid, NULL, 0);
	return 0;
}
```

```sh
sh boot.sh heap
```

```text
lab initramfs: 6.18.55-dom up 5.92s
[parent] heap=/dev/dma_heap/system dmabuf fd=5 size=4194304 lseek(END)=4194304
[child ] got fd=3, first bytes: 00 07 0e, last=f9

Dma-buf Objects:
size    	flags   	mode    	count   	exp_name	ino     	name
04194304	00000002	02080007	00000006	system	00000001	o4_frame0
	Attached Devices:
Total 0 devices attached


Total 1 objects, 4194304 bytes
lrwx------    1 0        0               64 Oct 18 16:19 /proc/78/fd/3 -> /dmabuf:o4_frame0
```

출력에서 볼 것:

- 부모의 fd 5와 자식의 fd 3은 **번호가 다르지만 같은 버퍼**다. 자식이 읽은 `00 07 0e ... f9`는 부모가 쓴 `i * 7` 패턴 그대로다(`0x3fffff * 7`의 하위 바이트 = `f9`).
- `lseek(fd, 0, SEEK_END)`가 버퍼 크기를 돌려준다 — 크기를 모르는 importer가 쓰는 표준 방법이다.
- `bufinfo`의 `count 6`은 이 `struct file`의 참조 수다: 부모 fd, 부모 mmap(VMA도 file 참조를 잡는다), 자식 fd, 자식 mmap, 그리고 `system()`이 띄운 셸과 `cat`이 상속한 fd. 받은 fd에 `MSG_CMSG_CLOEXEC`를 주지 않아서 자식 프로세스들까지 들고 간 것이다 — 실제 데몬에서는 이런 누수가 "버퍼가 안 풀린다"의 흔한 원인이다.
- `exp_name system`은 exporter(heap), `name o4_frame0`은 `DMA_BUF_SET_NAME`으로 붙인 디버그 이름이다. `/proc/<pid>/fd`에서도 `/dmabuf:o4_frame0`으로 보인다. 메모리 누수 추적 때 이름을 붙이는 습관이 큰 도움이 된다.
- `Attached Devices: 0` — 아직 어떤 장치도 attach하지 않았다. CPU만 쓴 버퍼다(§6에서 장치가 붙는 모습을 본다).

같은 프로그램을 CMA heap으로:

```sh
sh boot.sh cma        # ./heap_share /dev/dma_heap/default_cma_region
```

```text
lab initramfs: 6.18.55-dom up 6.83s
[parent] heap=/dev/dma_heap/default_cma_region dmabuf fd=5 size=4194304 lseek(END)=4194304
[child ] got fd=3, first bytes: 00 07 0e, last=f9

Dma-buf Objects:
size    	flags   	mode    	count   	exp_name	ino     	name
04194304	00000002	02080007	00000006	default_cma_region	00000001	o4_frame0
	Attached Devices:
Total 0 devices attached


Total 1 objects, 4194304 bytes
lrwx------    1 0        0               64 Oct 18 16:19 /proc/78/fd/3 -> /dmabuf:o4_frame0
```

유저 코드는 heap 경로만 다르다. 차이는 커널 안에 있다: `default_cma_region`은 `cma_alloc()`으로 **물리적으로 연속인** 4 MiB를 잡는다(F10 §5). IOMMU가 없는 장치(오래된 ISP, 디스플레이 엔진, 작은 NPU)는 연속 버퍼만 받을 수 있으므로, 파이프라인에 그런 장치가 하나라도 있으면 **가장 까다로운 장치에 맞춰 heap을 고른다**.

### 3.3 heap 고르기 — 표로

| heap | 할당 | 장점 | 단점 | 언제 |
|---|---|---|---|---|
| `system` | 페이지 단위로 흩어진 메모리 (큰 order부터 시도) | 단편화에 강함, 크기 제한 거의 없음 | IOMMU 없는 장치는 못 씀 | 모든 importer가 SMMU 뒤에 있을 때 |
| `default_cma_region` / `reserved` | CMA에서 물리 연속 | IOMMU 없는 장치도 OK | CMA 크기 한계, 할당 지연(migration, F10 §7) | 디스플레이·구형 ISP·연속 메모리 NPU |
| 벤더 heap (예: secure, carveout) | DT reserved-memory | 보안 영역(TrustZone) 버퍼 등 | 메인라인 밖일 때가 많음 | DRM(콘텐츠 보호), 보안 카메라 |

### 3.4 흔한 함정

- `DMA_HEAP_IOCTL_ALLOC`의 `fd_flags`에 `O_RDWR`를 안 주면 mmap(PROT_WRITE)이 `EACCES`로 실패한다.
- dma-buf fd를 `close()`해도 mmap이 살아 있으면 버퍼는 안 풀린다(위의 count). "fd는 다 닫았는데 CMA가 안 돌아온다"면 mmap과 자식 프로세스를 의심한다.

---

## 4. 캐시 일관성 — `DMA_BUF_IOCTL_SYNC`가 하는 일

### 4.1 직관

Don에게 익숙한 문제다: Cortex-R 펌웨어에서 DMA 버퍼를 CPU가 쓴 뒤 **clean**(write-back), 장치가 쓴 뒤 CPU가 읽기 전에 **invalidate**. Linux 커널 드라이버는 이것을 `dma_sync_*_for_cpu/for_device`로 한다(H9 §3~§4). 그런데 dma-buf는 **유저 공간이 mmap으로 직접 만진다**. 그래서 유저용 버전이 필요하고, 그게 `DMA_BUF_IOCTL_SYNC`다.

| 유저 호출 | 커널 경로 | 비코히어런트 SoC에서 실제 일 |
|---|---|---|
| `SYNC_START` + READ/WRITE | `dma_buf_begin_cpu_access()` → ① 버퍼의 implicit fence 대기 ② exporter `.begin_cpu_access` | attach·map된 장치마다 `dma_sync_sgtable_for_cpu` (invalidate) |
| `SYNC_END` + READ/WRITE | `dma_buf_end_cpu_access()` → exporter `.end_cpu_access` | `dma_sync_sgtable_for_device` (clean) |

두 가지를 기억한다.

1. **START는 "장치가 다 쓸 때까지 기다리기"도 한다.** 랩 소스 `drivers/dma-buf/dma-buf.c`의 `__dma_buf_begin_cpu_access()`가 `dma_resv_wait_timeout(resv, ..., MAX_SCHEDULE_TIMEOUT)`을 부른다(소스 확인). §6.2와 §8.3에서 이 대기를 직접 측정한다.
2. 캐시 작업 대상은 **attach된 장치들**이다. system heap의 `begin_cpu_access`는 attachment 목록을 돌며 **map까지 된** 장치마다 `dma_sync_sgtable_for_cpu`를 부른다(랩 소스 `heaps/system_heap.c`, 소스 확인). attach된 장치가 없으면(예제 2) 할 일이 없다 — 그래도 ioctl을 빼먹으면 장치가 붙는 순간 버그가 된다.

QEMU `virt`의 장치는 모두 코히어런트로 취급되므로 이 랩에서는 캐시 문제 자체를 재현할 수 없다(H9 §3.6과 같은 한계). 실제 보드에서는 SoC마다 다르다: 많은 Arm SoC의 카메라·NPU 마스터는 CCI/CMN 인터커넥트를 통해 **I/O coherent**이거나 아니거나 섞여 있고, DT의 `dma-coherent` 속성(H9 §4.4)으로 커널에 알린다.

### 4.2 IOMMU / SMMU — NPU 관점에서 다시

H9 §7에서 SMMU가 장치 DMA를 격리하는 것을 봤다. 가속기 드라이버에서 IOMMU는 세 가지 일을 더 한다.

- **프로세스별 장치 주소 공간**: rocket은 파일(`drm_file`)마다 `iommu_domain`을 만들고, BO를 `drm_mm`으로 고른 IOVA에 `iommu_map_sgtable()`로 매핑한다. job을 실행하기 직전에 `iommu_attach_group(job->domain->domain, core->iommu_group)`으로 **그 job 소유자의 주소 공간으로 갈아 끼운다**(소스 `rocket_job.c` `rocket_job_run`). CPU의 context switch 때 TTBR을 바꾸는 것과 같은 일을 NPU에 해 주는 것이다.
- **흩어진 메모리를 연속으로 보이게**: system heap의 흩어진 페이지가 NPU에게는 연속 IOVA로 보인다. 그래서 IOMMU 있는 NPU는 CMA가 필요 없다.
- **장치 자체 MMU**: Intel ivpu는 SoC IOMMU 대신 NPU 내부 MMU를 직접 프로그래밍한다(`ivpu_mmu.c`, `ivpu_mmu_context.c`; §9 조사에서 ivpu의 `iommu_` 호출이 0인 이유). GPU(Mali의 panfrost/panthor)도 같은 방식이다.

```dts
/* 랩 소스 arch/arm64/boot/dts/rockchip/rk3588-base.dtsi 발췌 (소스 확인) */
rknn_core_0: npu@fdab0000 {
	compatible = "rockchip,rk3588-rknn-core";
	reg = <0x0 0xfdab0000 0x0 0x1000>, <0x0 0xfdab1000 0x0 0x1000>, <0x0 0xfdab3000 0x0 0x1000>;
	reg-names = "pc", "cna", "core";
	interrupts = <GIC_SPI 110 IRQ_TYPE_LEVEL_HIGH 0>;
	clocks = <&cru ACLK_NPU0>, <&cru HCLK_NPU0>, <&scmi_clk SCMI_CLK_NPU>, <&cru PCLK_NPU_ROOT>;
	clock-names = "aclk", "hclk", "npu", "pclk";
	resets = <&cru SRST_A_RKNN0>, <&cru SRST_H_RKNN0>;
	power-domains = <&power RK3588_PD_NPUTOP>;
	iommus = <&rknn_mmu_0>;
	status = "disabled";
};
rknn_mmu_0: iommu@fdab9000 {
	compatible = "rockchip,rk3588-iommu", "rockchip,rk3568-iommu";
	#iommu-cells = <0>;
	power-domains = <&power RK3588_PD_NPUTOP>;
	status = "disabled";
};
```

NPU 노드 하나에 Don이 bring-up에서 확인할 것이 다 들어 있다: 레지스터 블록 3개(`pc` = 명령 처리기, `cna`, `core`), 인터럽트, 클럭 4개(그중 하나는 SCMI = 펌웨어가 관리하는 클럭), 리셋 2개, 전원 도메인, 그리고 `iommus` 연결. 보드 DT(`rk3588-rock-5b-5bp-5t.dtsi`)가 `&rknn_core_0 { status = "okay"; ... }`처럼 켠다(P4).

---

## 5. dma-fence, sync_file, timeline — "언제 끝났나"를 객체로

### 5.1 직관 — 완료 큐 엔트리를 커널 객체로 만든 것

SSD 펌웨어에서 명령 완료는 completion queue 엔트리와 phase bit였다. 가속기에서는 작업 하나가 끝났다는 사실이 **`struct dma_fence`**라는 커널 객체다. 성질은 단순하다.

- 상태는 두 개뿐: unsignaled → signaled(한 번 signal되면 되돌릴 수 없다). 에러로 끝났으면 `error` 필드에 errno가 붙는다.
- 모든 fence는 **context(타임라인)** 와 **seqno**를 가진다. 같은 타임라인의 fence는 seqno 순서대로 signal된다고 약속한다 — 링 버퍼 하나에서 명령이 순서대로 끝나는 것과 같다. 그래서 "seqno 5가 끝났으면 3도 끝났다"를 비교 한 번으로 안다.
- 누구나 콜백을 달 수 있다(`dma_fence_add_callback`). drm_sched, 디스플레이, 다른 장치 드라이버가 여기 매달린다.

fence를 유저 공간에 꺼내는 방법이 두 가지 있다. **sync_file**(fence를 fd로 감싼 것, `poll()` 가능, `SCM_RIGHTS`로 전달 가능)과 **drm_syncobj**(DRM 장치 안의 handle, timeline syncobj는 64비트 포인트를 가진다 — Vulkan timeline semaphore의 커널 쪽). 이 절은 sync_file을 다룬다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="10" y="24" font-size="13">타임라인 A (context 1)</text>
  <line x1="10" y1="60" x2="660" y2="60" stroke="currentColor" marker-end="url(#ah3)"/>
  <circle cx="160" cy="60" r="9" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
  <text x="160" y="88" text-anchor="middle" font-size="12">seq 1 camera_done</text>
  <circle cx="420" cy="60" r="9" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
  <text x="420" y="88" text-anchor="middle" font-size="12">seq 3 npu_done</text>
  <text x="10" y="134" font-size="13">타임라인 B (context 2)</text>
  <line x1="10" y1="170" x2="660" y2="170" stroke="currentColor" marker-end="url(#ah3)"/>
  <circle cx="560" cy="170" r="9" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
  <text x="560" y="198" text-anchor="middle" font-size="12">seq 2 isp_done</text>
  <rect x="380" y="232" width="280" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="520" y="255" text-anchor="middle" font-size="13">merged sync_file "all_inputs"</text>
  <text x="520" y="276" text-anchor="middle" font-size="12">A:3 그리고 B:2 가 모두 signal되면 ready</text>
  <line x1="420" y1="70" x2="470" y2="228" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
  <line x1="560" y1="180" x2="560" y2="228" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
  <rect x="10" y="232" width="330" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="175" y="255" text-anchor="middle" font-size="13">SW_SYNC_IOC_INC(A, 1): seq 1 signal</text>
  <text x="175" y="276" text-anchor="middle" font-size="12">INC(A, 2): seq 3 signal, INC(B, 2): seq 2</text>
  <text x="340" y="318" text-anchor="middle" font-size="12">카운터가 seqno 이상이 되는 순간 그 fence가 signal된다 — 타임라인 = 단조 증가 카운터</text>
</svg>
```

그림 3 — sw_sync 타임라인 두 개와 merge. 각 타임라인은 단조 증가 카운터이고, fence는 "카운터가 seqno에 도달하면"이라는 약속이다. merge는 AND 조건으로 묶은 새 sync_file이다.

### 5.2 예제 3 — sw_sync로 fence를 만들고 기다리기

`sw_sync`는 테스트용 **소프트웨어 타임라인**이다(`CONFIG_SW_SYNC`, 랩 소스 `drivers/dma-buf/sw_sync.c`). debugfs의 `sync/sw_sync`를 열 때마다 새 타임라인이 생기고, `SW_SYNC_IOC_CREATE_FENCE`(seqno를 주면 sync_file fd를 돌려줌)와 `SW_SYNC_IOC_INC`(카운터 증가)로 "가짜 하드웨어"를 흉내 낸다. 이 ioctl 구조체는 uAPI 헤더에 없고 `sw_sync.c` 안에만 정의돼 있어서(소스 확인) 예제에 직접 적었다. 무엇을 확인하나: fence 상태(`SYNC_IOC_FILE_INFO`), `poll()`로 기다리기, 서로 다른 타임라인의 fence를 `SYNC_IOC_MERGE`로 묶기.

```c
// swsync.c — sw_sync 타임라인으로 fence(sync_file)를 만들고, poll/merge/상태를 본다
#include "o4.h"
#include <poll.h>
#include <linux/sync_file.h>

struct sw_sync_create_fence_data { uint32_t value; char name[32]; int32_t fence; };
#define SW_SYNC_IOC_CREATE_FENCE _IOWR('W', 0, struct sw_sync_create_fence_data)
#define SW_SYNC_IOC_INC          _IOW('W', 1, uint32_t)

static int mk_fence(int tl, uint32_t seq, const char *name)
{
	struct sw_sync_create_fence_data d = { .value = seq };
	snprintf(d.name, sizeof(d.name), "%s", name);
	if (ioctl(tl, SW_SYNC_IOC_CREATE_FENCE, &d) < 0) DIE("CREATE_FENCE");
	return d.fence;                       /* 이 fd가 sync_file */
}

static void show(const char *tag, int f)
{
	struct sync_fence_info fi[4];
	struct sync_file_info info = { .num_fences = 4, .sync_fence_info = (uintptr_t)fi };
	if (ioctl(f, SYNC_IOC_FILE_INFO, &info) < 0) DIE("FILE_INFO");
	struct pollfd p = { .fd = f, .events = POLLIN };
	int r = poll(&p, 1, 0);               /* timeout 0: 지금 signaled 인가? */
	printf("%-7s name=%-12s status=%d fences=%u poll=%d :", tag, info.name,
	       info.status, info.num_fences, r);
	for (unsigned i = 0; i < info.num_fences; i++)
		printf(" [%s %s st=%d]", fi[i].driver_name, fi[i].obj_name, fi[i].status);
	printf("\n");
}

int main(void)
{
	int tl = open("/sys/kernel/debug/sync/sw_sync", O_RDWR);  /* 열 때마다 새 타임라인 */
	if (tl < 0) DIE("open sw_sync");
	int f1 = mk_fence(tl, 1, "camera_done"), f3 = mk_fence(tl, 3, "npu_done");
	int tl2 = open("/sys/kernel/debug/sync/sw_sync", O_RDWR);
	if (tl2 < 0) DIE("open sw_sync 2");
	int g2 = mk_fence(tl2, 2, "isp_done");
	struct sync_merge_data m = { .fd2 = g2 };
	snprintf(m.name, sizeof(m.name), "all_inputs");
	if (ioctl(f3, SYNC_IOC_MERGE, &m) < 0) DIE("MERGE");     /* f3 AND g2 */
	show("t=0", f1); show("", f3); show("", m.fence);
	uint32_t one = 1, two = 2;
	if (ioctl(tl, SW_SYNC_IOC_INC, &one) < 0) DIE("INC");      /* timeline A: 0 -> 1 */
	show("A=1", f1); show("", f3);
	if (ioctl(tl, SW_SYNC_IOC_INC, &two) < 0) DIE("INC");      /* A: 1 -> 3 */
	show("A=3", m.fence);
	uint64_t t0 = now_ns();
	struct pollfd p = { .fd = m.fence, .events = POLLIN };
	int r = poll(&p, 1, 200);             /* B가 아직 0이라 200 ms 타임아웃 */
	printf("poll(merged, 200ms) = %d after %.1f ms\n", r, (now_ns() - t0) / 1e6);
	if (ioctl(tl2, SW_SYNC_IOC_INC, &two) < 0) DIE("INC");     /* B: 0 -> 2 */
	show("B=2", m.fence);
	if (system("cat /sys/kernel/debug/sync/info") < 0) DIE("system");
	return 0;
}
```

```sh
sh boot.sh swsync
```

```text
lab initramfs: 6.18.55-dom up 5.67s
t=0     name=sw_sync-swsync1-1 status=0 fences=1 poll=0 : [sw_sync swsync st=0]
        name=sw_sync-swsync1-3 status=0 fences=1 poll=0 : [sw_sync swsync st=0]
        name=all_inputs   status=0 fences=2 poll=0 : [sw_sync swsync st=0] [sw_sync swsync st=0]
A=1     name=detached-driver-signaled-timeli status=1 fences=1 poll=1 : [detached-driver signaled-timeline st=1]
        name=sw_sync-swsync1-3 status=0 fences=1 poll=0 : [sw_sync swsync st=0]
A=3     name=all_inputs   status=0 fences=2 poll=0 : [detached-driver signaled-timeline st=1] [sw_sync swsync st=0]
poll(merged, 200ms) = 0 after 201.7 ms
B=2     name=all_inputs   status=1 fences=2 poll=1 : [detached-driver signaled-timeline st=1] [detached-driver signaled-timeline st=1]
objs:
--------------
swsync: 3

swsync: 2

fences:
--------------
```

출력에서 볼 것:

- `status`는 0 = 아직(active), 1 = signaled(음수면 에러). `poll(..., 0)`의 결과(0/1)와 정확히 같이 움직인다. **sync_file fd를 poll하는 것 = fence를 기다리는 것**이다.
- 타임라인 A를 1로 올리면 seq 1 fence만 signal되고 seq 3은 그대로다. 2를 더해 3이 되면 seq 3도 signal된다. 하지만 merge된 `all_inputs`는 B가 0이라 여전히 0이고, 200 ms poll이 타임아웃(0)으로 끝난다. B를 2로 올리는 순간 `status=1`.
- 이름 규칙: 활성 fence의 이름은 `드라이버-타임라인 context-seqno`(`sw_sync-swsync1-3`)이고 타임라인 이름은 이 프로세스 이름(`swsync`)이다. signal된 fence는 `detached-driver` / `signaled-timeline`으로 바뀐다. 랩 소스 `drivers/dma-buf/dma-fence.c`의 `dma_fence_driver_name()`/`dma_fence_timeline_name()`이 SIGNALED 비트가 서 있으면 드라이버 콜백을 부르지 않고 이 고정 문자열을 돌려준다(소스 확인). 주석에 적힌 계약은 "**signal 이후에는 드라이버가 fence의 부속 데이터(lock, ops가 보는 메모리)를 해제해도 된다**, 그러니 signal된 fence의 드라이버 데이터는 건드리지 마라"다 — 드라이버를 쓸 때 그대로 지켜야 하는 규칙이다(§8의 장난감 드라이버가 job 메모리를 fence와 같이 해제하는 이유). 디버깅할 때 "이름이 detached면 이미 끝난 fence"로 읽으면 된다.
- `sync/info`의 `objs`는 살아 있는 sw_sync 타임라인과 현재 카운터 값(3, 2)이다.

### 5.3 implicit vs explicit sync

| | implicit | explicit |
|---|---|---|
| fence가 사는 곳 | 버퍼의 `dma_resv` (버퍼마다 하나) | 유저가 들고 다니는 sync_file / syncobj |
| 누가 기다리나 | 커널(importer 드라이버, `DMA_BUF_IOCTL_SYNC`, `poll(dmabuf)`) | 유저가 다음 제출에 in-fence로 넘김 |
| 장점 | 유저 코드가 단순, 구형 API와 호환 | 의존성이 명시적, 불필요한 대기 없음, 파이프라인 깊이 조절 쉬움 |
| 단점 | 언제 기다리는지 불투명, 과잉 동기화 | 모든 단계가 fence를 전달해야 함 |
| 쓰는 곳 | X11/GL 시절, V4L2 대부분, rocket | Android, Vulkan, KMS `IN_FENCE_FD`(I8 §7.4), amdxdna |

둘 사이 다리도 있다: `DMA_BUF_IOCTL_EXPORT_SYNC_FILE`(버퍼의 implicit fence들 → sync_file)과 `DMA_BUF_IOCTL_IMPORT_SYNC_FILE`(sync_file → 버퍼의 dma_resv에 넣기). 다음 예제가 앞쪽 다리를 쓴다.

### 5.4 timeline syncobj 한 문단

drm_syncobj는 DRM 장치 fd 안의 handle로 된 fence 컨테이너다. **timeline syncobj**는 64비트 포인트를 가진 syncobj로, "포인트 N이 signal될 때까지 기다려라"를 표현한다(`DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT`). 내부적으로는 `dma_fence_chain`(랩 소스 `drivers/dma-buf/dma-fence-chain.c`)이 포인트마다 fence를 사슬로 잇는다. 그림 3의 sw_sync 타임라인을 장치 쪽에서 제대로 만든 것이라고 보면 된다. 이 랩에서는 syncobj를 지원하는 드라이버(amdxdna 등)가 없어 실행하지 않았다.

---

## 6. PRIME과 implicit fence — 드라이버 사이 공유

### 6.1 예제 4 — vgem BO를 export하고 vkms가 import

`vgem`은 "하드웨어 없는 GEM 드라이버"다(랩 소스 `drivers/gpu/drm/vgem/`). 실제 장치가 없는 테스트에서 **exporter 역할**을 한다. `vkms`는 가상 디스플레이(I8). 무엇을 확인하나: GPU(vgem) 버퍼를 PRIME으로 dma-buf fd로 꺼내 CPU로 채우고, 다른 드라이버(vkms)가 그 fd를 import해 framebuffer로 쓸 수 있는지, 그리고 그때 `bufinfo`에 attach가 나타나는지. DRM 공용 헬퍼부터:

```c
// o4_drm.h — 이름으로 DRM 장치 노드 찾기 + dumb BO 만들기 (libdrm 없이 ioctl만)
#pragma once
#include "o4.h"
#include <glob.h>
#include <drm/drm.h>
#include <drm/drm_mode.h>

/* pattern 예: "/dev/dri/card*", "/dev/accel/accel*" — 드라이버 이름이 name인 첫 노드 */
static inline int drm_open_by_name(const char *pattern, const char *name)
{
	glob_t g;
	if (glob(pattern, 0, NULL, &g) != 0) return -1;
	for (size_t i = 0; i < g.gl_pathc; i++) {
		int fd = open(g.gl_pathv[i], O_RDWR | O_CLOEXEC);
		if (fd < 0) continue;
		char n[32] = "";
		struct drm_version v = { .name = n, .name_len = sizeof(n) - 1 };
		if (ioctl(fd, DRM_IOCTL_VERSION, &v) == 0 && strcmp(n, name) == 0) {
			printf("%s -> driver '%s'\n", g.gl_pathv[i], n);
			globfree(&g);
			return fd;
		}
		close(fd);
	}
	globfree(&g);
	return -1;
}

/* width x height x 32bpp dumb buffer → GEM handle */
static inline uint32_t dumb_create(int fd, uint32_t w, uint32_t h, uint64_t *size)
{
	struct drm_mode_create_dumb c = { .width = w, .height = h, .bpp = 32 };
	if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &c) < 0) DIE("CREATE_DUMB");
	*size = c.size;
	return c.handle;
}

static inline int prime_export(int fd, uint32_t handle)
{
	struct drm_prime_handle p = { .handle = handle, .flags = DRM_CLOEXEC | DRM_RDWR };
	if (ioctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &p) < 0) DIE("PRIME_HANDLE_TO_FD");
	return p.fd;
}

static inline uint32_t prime_import(int fd, int dmabuf)
{
	struct drm_prime_handle p = { .fd = dmabuf };
	if (ioctl(fd, DRM_IOCTL_PRIME_FD_TO_HANDLE, &p) < 0) DIE("PRIME_FD_TO_HANDLE");
	return p.handle;
}
```

```c
// prime.c — vgem에서 BO를 만들고 PRIME으로 export → 다른 드라이버(vkms)가 import
#include "o4_drm.h"
#include <linux/dma-buf.h>

int main(void)
{
	int vg = drm_open_by_name("/dev/dri/card*", "vgem");
	int vk = drm_open_by_name("/dev/dri/card*", "vkms");
	if (vg < 0 || vk < 0) { fprintf(stderr, "need vgem + vkms\n"); return 1; }

	uint64_t size;
	uint32_t h = dumb_create(vg, 640, 480, &size);   /* 640x480 XRGB8888 */
	printf("vgem: GEM handle=%u size=%llu\n", h, (unsigned long long)size);

	int dmabuf = prime_export(vg, h);                /* handle -> 전역에서 통하는 fd */
	printf("PRIME export: dma-buf fd=%d\n", dmabuf);

	uint32_t *px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, dmabuf, 0);
	if (px == MAP_FAILED) DIE("mmap dmabuf");
	struct dma_buf_sync s = { DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE };
	if (ioctl(dmabuf, DMA_BUF_IOCTL_SYNC, &s) < 0) DIE("SYNC start");
	for (size_t i = 0; i < size / 4; i++) px[i] = 0x00ff8800;  /* 주황 */
	s.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
	if (ioctl(dmabuf, DMA_BUF_IOCTL_SYNC, &s) < 0) DIE("SYNC end");

	uint32_t h2 = prime_import(vk, dmabuf);          /* vkms가 같은 메모리를 import */
	uint32_t h3 = prime_import(vg, dmabuf);          /* 같은 장치로 재-import */
	printf("vkms import: handle=%u   vgem self-import: handle=%u (same as %u)\n",
	       h2, h3, h);

	/* vkms 쪽에서 이 BO로 framebuffer를 만들 수 있으면 디스플레이 경로도 연결된 것 */
	struct drm_mode_fb_cmd2 fb = { .width = 640, .height = 480,
		.pixel_format = 0x34325258 /* 'XR24' */, .handles = { h2 }, .pitches = { 640 * 4 } };
	if (ioctl(vk, DRM_IOCTL_MODE_ADDFB2, &fb) < 0) perror("ADDFB2");
	else printf("vkms ADDFB2 on imported buffer: fb_id=%u\n", fb.fb_id);

	if (system("cat /sys/kernel/debug/dma_buf/bufinfo") < 0) DIE("system");
	return 0;
}
```

```sh
sh boot.sh prime      # modprobe vgem && modprobe vkms && ls -l /dev/dri && ./prime
```

```text
lab initramfs: 6.18.55-dom up 9.95s
total 0
crw-------    1 0        0         226,   0 Oct 18 16:19 card0
crw-------    1 0        0         226,   1 Oct 18 16:19 card1
crw-------    1 0        0         226, 128 Oct 18 16:19 renderD128
/dev/dri/card0 -> driver 'vgem'
/dev/dri/card1 -> driver 'vkms'
vgem: GEM handle=1 size=1228800
PRIME export: dma-buf fd=5
vkms import: handle=1   vgem self-import: handle=1 (same as 1)
vkms ADDFB2 on imported buffer: fb_id=54

Dma-buf Objects:
size    	flags   	mode    	count   	exp_name	ino     	name
01228800	00000002	02080007	00000007	drm	00000001	<none>
	Attached Devices:
	vkms
Total 1 devices attached


Total 1 objects, 1228800 bytes
```

출력에서 볼 것:

- vgem은 `DRIVER_RENDER`라 `renderD128`을 만들고, vkms(KMS 전용)는 `card1`만 만든다. 640x480x4 = 1,228,800바이트.
- **다른 드라이버로 import하면 attach가 생긴다**: `Attached Devices: vkms`. vkms가 이 버퍼를 자기 장치 입장의 sg_table로 매핑했다는 뜻이다. 실제 SoC라면 여기에 `fdab0000.npu`, `fde40000.isp` 같은 장치 이름이 줄줄이 나온다 — **이 줄이 "누가 이 버퍼를 잡고 있나"를 찾는 첫 도구**다.
- **같은 드라이버로 re-import하면 새 객체가 아니라 원래 handle(1)** 이 돌아온다. DRM이 "이 dma-buf는 내가 export한 것"을 알아보고 원래 GEM 객체를 돌려주기 때문이다(`drm_gem_prime_fd_to_handle`의 self-import 경로).
- `exp_name drm`: GEM PRIME export는 exporter 이름이 `drm`으로 찍힌다(드라이버 이름이 아니다).
- vkms가 import한 버퍼로 `ADDFB2`가 성공했다 → 카메라/GPU/NPU 버퍼를 그대로 화면에 띄우는 경로(I8 §10.2)가 열린 것이다.

### 6.2 예제 5 — implicit fence: "GPU가 쓰는 중"인 버퍼를 CPU가 읽으면

vgem에는 테스트용 fence ioctl 두 개가 있다(랩 uAPI `include/uapi/drm/vgem_drm.h`): `VGEM_FENCE_ATTACH`는 BO의 `dma_resv`에 unsignaled fence를 붙이고(= "GPU가 지금 이 버퍼에 쓰는 중"), `VGEM_FENCE_SIGNAL`이 그것을 signal한다. 붙인 fence는 10초 안에 signal하지 않으면 타이머가 강제로 signal한다(`VGEM_FENCE_TIMEOUT (10*HZ)`, 소스 확인). 무엇을 확인하나: dma-buf fd에 대한 `poll()`, `DMA_BUF_IOCTL_EXPORT_SYNC_FILE`, `DMA_BUF_IOCTL_SYNC(START)`가 모두 이 implicit fence를 존중하는지.

```c
// implicit.c — vgem "가짜 GPU 작업" fence를 BO의 dma_resv에 걸고 implicit sync를 관찰
#include "o4_drm.h"
#include <poll.h>
#include <pthread.h>
#include <linux/dma-buf.h>
#include <linux/sync_file.h>
#include <drm/vgem_drm.h>

static int vg; static uint32_t fence_id;

static void *signaler(void *arg)                 /* 300 ms 뒤 "GPU 작업 끝" */
{
	(void)arg;
	usleep(300 * 1000);
	struct drm_vgem_fence_signal sig = { .fence = fence_id };
	if (ioctl(vg, DRM_IOCTL_VGEM_FENCE_SIGNAL, &sig) < 0) DIE("FENCE_SIGNAL");
	return NULL;
}

int main(void)
{
	vg = drm_open_by_name("/dev/dri/card*", "vgem");
	if (vg < 0) { fprintf(stderr, "no vgem\n"); return 1; }
	uint64_t size;
	uint32_t h = dumb_create(vg, 256, 256, &size);
	int dmabuf = prime_export(vg, h);

	/* "GPU가 이 BO에 쓰는 중": 쓰기 fence를 BO의 예약 객체(dma_resv)에 붙인다 */
	struct drm_vgem_fence_attach at = { .handle = h, .flags = VGEM_FENCE_WRITE };
	if (ioctl(vg, DRM_IOCTL_VGEM_FENCE_ATTACH, &at) < 0) DIE("FENCE_ATTACH");
	fence_id = at.out_fence;

	struct pollfd p = { .fd = dmabuf, .events = POLLIN };   /* POLLIN = 읽어도 되나? */
	printf("poll(dmabuf, POLLIN, 0) = %d   (0 = 아직 쓰는 중)\n", poll(&p, 1, 0));

	/* implicit fence → explicit sync_file 로 꺼내기 */
	struct dma_buf_export_sync_file ex = { .flags = DMA_BUF_SYNC_READ, .fd = -1 };
	if (ioctl(dmabuf, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &ex) < 0) DIE("EXPORT_SYNC_FILE");
	struct sync_fence_info fi;
	struct sync_file_info info = { .num_fences = 1, .sync_fence_info = (uintptr_t)&fi };
	if (ioctl(ex.fd, SYNC_IOC_FILE_INFO, &info) < 0) DIE("FILE_INFO");
	printf("exported sync_file: driver=%s timeline=%s status=%d\n",
	       fi.driver_name, fi.obj_name, fi.status);

	pthread_t t;
	pthread_create(&t, NULL, signaler, NULL);
	uint64_t t0 = now_ns();                         /* CPU 접근 시작 = fence 대기 */
	struct dma_buf_sync s = { DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ };
	if (ioctl(dmabuf, DMA_BUF_IOCTL_SYNC, &s) < 0) DIE("SYNC start");
	printf("DMA_BUF_IOCTL_SYNC(START|READ) returned after %.1f ms\n", (now_ns() - t0) / 1e6);
	s.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
	if (ioctl(dmabuf, DMA_BUF_IOCTL_SYNC, &s) < 0) DIE("SYNC end");
	pthread_join(t, NULL);
	printf("poll(dmabuf, POLLIN, 0) = %d   poll(sync_file) = %d\n", poll(&p, 1, 0),
	       poll(&(struct pollfd){ .fd = ex.fd, .events = POLLIN }, 1, 0));
	return 0;
}
```

```sh
sh boot.sh implicit
```

```text
lab initramfs: 6.18.55-dom up 18.76s
/dev/dri/card0 -> driver 'vgem'
poll(dmabuf, POLLIN, 0) = 0   (0 = 아직 쓰는 중)
exported sync_file: driver=vgem timeline=unbound status=0
DMA_BUF_IOCTL_SYNC(START|READ) returned after 309.8 ms
poll(dmabuf, POLLIN, 0) = 1   poll(sync_file) = 1
```

출력에서 볼 것:

- dma-buf fd의 `POLLIN`은 "읽어도 되나 = 쓰기 fence가 다 끝났나"다. fence가 붙어 있으니 0.
- `EXPORT_SYNC_FILE`이 버퍼 안의 implicit fence를 **explicit sync_file로 꺼냈다**(driver=vgem). 이 fd를 KMS `IN_FENCE_FD`나 다른 장치의 in-fence로 넘길 수 있다.
- `DMA_BUF_IOCTL_SYNC(START)`가 **약 300 ms 동안 블록**됐다가 signaler 스레드가 fence를 signal한 직후에 돌아왔다(309.8 ms; 스레드는 300 ms sleep). §4.1에서 말한 "START = fence 대기 + 캐시 작업"을 직접 본 것이다. 같은 프로그램의 첫 실행에서는 5173.0 ms가 나왔는데, 그때 VM load가 20을 넘어 게스트 스레드가 제때 스케줄되지 못한 것이다(TCG 공유 VM의 한계 — 재실행에서 309.8 ms).
- 이게 implicit sync의 양면이다: CPU 코드가 아무 것도 몰라도 안전하지만, 이 ioctl이 **언제 얼마나 기다릴지 호출자는 모른다**. 실시간 파이프라인에서 "가끔 300 ms 튄다"의 원인이 될 수 있다.

### 6.3 흔한 함정

- importer가 dma-buf를 attach할 때 버퍼가 **자기 장치의 제약**(연속성, 주소 폭, 정렬)을 만족하지 않으면 attach나 map이 실패한다. 예: IOMMU 없는 디스플레이에 system heap 버퍼를 넣으면 `ADDFB2`는 되더라도 commit에서 실패하거나 연속 버퍼를 요구하는 드라이버가 import 자체를 거절한다.
- self-import가 원래 handle을 돌려준다는 것을 모르고 handle을 두 번 `GEM_CLOSE`하면 다른 쪽 사용자가 쓰던 handle이 닫힌다.

---

## 7. udmabuf — 유저 메모리를 dma-buf로

### 7.1 직관과 용도

heap은 커널이 새 메모리를 잡아 준다. 반대로 **이미 유저 공간에 있는 메모리**(예: 소프트웨어 디코더가 쓰는 memfd 링, QEMU 게스트 RAM)를 장치에 넘기고 싶을 때가 있다. `udmabuf`(랩 소스 `drivers/dma-buf/udmabuf.c`)는 **memfd의 페이지를 그대로 감싸 dma-buf로** 만든다. 조건이 하나 있다: memfd에 `F_SEAL_SHRINK`가 걸려 있어야 한다(장치가 쓰는 중에 파일이 줄어 페이지가 사라지면 안 되니까). `F_SEAL_WRITE`는 거꾸로 금지다(소스의 `SEALS_WANTED`/`SEALS_DENIED`). 크기 한계는 모듈 파라미터 `size_limit_mb`(기본 64)다.

### 7.2 예제 6 — memfd → udmabuf

무엇을 확인하나: seal 없이 만들면 거절되는지, seal을 걸면 dma-buf fd가 나오는지, memfd로 쓴 내용이 dma-buf mmap으로 보이는지, seal 때문에 memfd를 줄일 수 없게 되는지.

```c
// udmabuf.c — 평범한 memfd 메모리를 dma-buf로 감싸기 (유저가 만든 버퍼를 장치에 넘기는 길)
#include "o4.h"
#include <linux/udmabuf.h>

int main(void)
{
	size_t len = 2 << 20;
	int mfd = memfd_create("o4_cam_ring", MFD_ALLOW_SEALING);
	if (mfd < 0) DIE("memfd_create");
	if (ftruncate(mfd, len) < 0) DIE("ftruncate");

	int dev = open("/dev/udmabuf", O_RDWR);
	if (dev < 0) DIE("/dev/udmabuf");
	struct udmabuf_create c = { .memfd = mfd, .flags = UDMABUF_FLAGS_CLOEXEC,
				    .offset = 0, .size = len };
	int r = ioctl(dev, UDMABUF_CREATE, &c);
	printf("UDMABUF_CREATE without F_SEAL_SHRINK -> %d (%s)\n", r, r < 0 ? strerror(errno) : "ok");

	if (fcntl(mfd, F_ADD_SEALS, F_SEAL_SHRINK) < 0) DIE("F_ADD_SEALS");
	int buf = ioctl(dev, UDMABUF_CREATE, &c);       /* 성공하면 반환값이 dma-buf fd */
	if (buf < 0) DIE("UDMABUF_CREATE");
	printf("UDMABUF_CREATE with F_SEAL_SHRINK  -> dma-buf fd=%d\n", buf);

	char *a = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);  /* memfd 쪽 */
	char *b = mmap(NULL, len, PROT_READ, MAP_SHARED, buf, 0);               /* dma-buf 쪽 */
	if (a == MAP_FAILED || b == MAP_FAILED) DIE("mmap");
	snprintf(a + 4096, 64, "frame #42 written via memfd");
	printf("read via dma-buf mapping: \"%s\"\n", b + 4096);
	printf("same physical page? memfd va=%p dma-buf va=%p (다른 VA, 같은 페이지)\n",
	       (void *)a, (void *)b);

	r = ftruncate(mfd, len / 2);
	printf("ftruncate(memfd, smaller) -> %d (%s)\n", r, r < 0 ? strerror(errno) : "ok");
	if (system("grep -B1 -A3 udmabuf /sys/kernel/debug/dma_buf/bufinfo") < 0) DIE("system");
	return 0;
}
```

```sh
sh boot.sh udma
```

```text
lab initramfs: 6.18.55-dom up 50.17s
UDMABUF_CREATE without F_SEAL_SHRINK -> -1 (Invalid argument)
UDMABUF_CREATE with F_SEAL_SHRINK  -> dma-buf fd=5
read via dma-buf mapping: "frame #42 written via memfd"
same physical page? memfd va=0xffffbc4ad000 dma-buf va=0xffffbc2ad000 (다른 VA, 같은 페이지)
ftruncate(memfd, smaller) -> -1 (Operation not permitted)
size    	flags   	mode    	count   	exp_name	ino     	name
02097152	00000002	02080007	00000002	udmabuf	00000001	<none>
	Attached Devices:
Total 0 devices attached
```

출력에서 볼 것:

- seal 없이는 `EINVAL`, `F_SEAL_SHRINK` 뒤에는 성공. `UDMABUF_CREATE`는 **ioctl의 반환값 자체가 새 fd**다(heap처럼 구조체에 넣어 주지 않는다).
- memfd 매핑으로 쓴 문자열이 dma-buf 매핑으로 그대로 읽힌다. 가상 주소는 다르지만(값은 실행마다 다르다) 같은 페이지다 — 복사가 없다.
- seal이 걸렸으니 `ftruncate`로 줄이기는 `EPERM`. 장치가 쓰는 동안 메모리가 사라질 수 없다는 보장이다.
- 이 udmabuf 줄의 `count 2`는 fd 하나 + mmap 하나다. 부팅 시각(`up 50.17s`)이 다른 예제보다 긴 것은 이 순간 VM 부하 때문이다.

---

## 8. 장난감 NPU 드라이버 — `/dev/accel/accel0`을 직접 만든다

### 8.1 설계

진짜 NPU 드라이버의 뼈대를 200줄로 줄였다. 하드웨어 대신 workqueue가 "실행"하고, 연산은 `y = 2x + 1` 하나뿐이다. 하지만 유저가 보는 계약은 실제 드라이버(rocket, ivpu)와 같다.

| 부품 | `o4_accel` | 실제 드라이버에서 |
|---|---|---|
| 장치 | `faux_device` (6.18의 가짜 버스 장치, vgem과 같은 방식) | platform 장치(DT) 또는 PCI 장치 |
| 등록 | `drm_dev_alloc` + `drm_dev_register`, 플래그 `DRIVER_GEM` + `DRIVER_COMPUTE_ACCEL` | 같음 (`devm_drm_dev_alloc`이 보통) |
| 버퍼 | `CREATE_BO` → `drm_gem_shmem_create`, handle + mmap offset | + IOMMU 매핑, 장치 주소 반환 |
| 제출 | `SUBMIT`(입력 BO, 출력 BO, 개수) → job → ordered workqueue | job → drm_sched 또는 펌웨어 링 + doorbell |
| 완료 | `dma_fence` 1개: 출력 BO의 `dma_resv`에 WRITE로 걸고(implicit), sync_file fd로도 반환(explicit) | 같음 (+ syncobj) |
| 실행 | `delayed_work`가 `latency_ms` 뒤 vmap → 계산 → `dma_fence_signal` | 완료 IRQ 핸들러가 signal |

uAPI 헤더(커널과 유저가 같이 include):

```c
/* o4_accel_uapi.h — 장난감 NPU의 uAPI (커널 모듈과 유저 프로그램이 같이 include) */
#ifndef O4_ACCEL_UAPI_H
#define O4_ACCEL_UAPI_H
#include <drm/drm.h>

struct drm_o4_create_bo {
	__u32 size;		/* in: 바이트 */
	__u32 handle;		/* out: GEM handle (이 fd 안에서만 유효) */
	__u64 offset;		/* out: mmap(accel_fd, offset) 용 가짜 오프셋 */
};

struct drm_o4_submit {
	__u32 in_handle;	/* in: 입력 텐서 BO */
	__u32 out_handle;	/* in: 출력 텐서 BO */
	__u32 count;		/* in: int32 원소 개수 */
	__s32 out_fence_fd;	/* out: 작업 완료 sync_file fd */
};

#define DRM_O4_CREATE_BO	0x00
#define DRM_O4_SUBMIT		0x01
#define DRM_IOCTL_O4_CREATE_BO	DRM_IOWR(DRM_COMMAND_BASE + DRM_O4_CREATE_BO, struct drm_o4_create_bo)
#define DRM_IOCTL_O4_SUBMIT	DRM_IOWR(DRM_COMMAND_BASE + DRM_O4_SUBMIT, struct drm_o4_submit)
#endif
```

커널 모듈 — ① include, job 구조체, fence ops:

```c
// o4_accel.c (1/3) — 장난감 NPU: 작업(job)과 완료 fence
#include <linux/module.h>
#include <linux/device/faux.h>
#include <linux/dma-mapping.h>
#include <linux/dma-fence.h>
#include <linux/dma-resv.h>
#include <linux/sync_file.h>
#include <linux/file.h>
#include <linux/workqueue.h>
#include <linux/iosys-map.h>
#include <drm/drm_accel.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_ioctl.h>
#include "o4_accel_uapi.h"

static unsigned int latency_ms = 20;	/* "NPU 실행 시간" 흉내 */
module_param(latency_ms, uint, 0644);

struct o4_job {
	struct dma_fence fence;		/* 첫 멤버: 마지막 put에서 job 전체가 해제된다 */
	spinlock_t lock;
	struct delayed_work work;
	struct drm_gem_object *in, *out;
	u32 count;
};

static struct workqueue_struct *o4_wq;	/* ordered = 코어 1개짜리 NPU */
static u64 o4_ctx;			/* fence timeline(context) 번호 */
static atomic_t o4_seq;

static const char *o4_drv_name(struct dma_fence *f) { return "o4_accel"; }
static const char *o4_tl_name(struct dma_fence *f) { return "o4_npu_ring0"; }
static const struct dma_fence_ops o4_fence_ops = {
	.get_driver_name = o4_drv_name,
	.get_timeline_name = o4_tl_name,
};
```

② "하드웨어 실행"(work 함수)과 BO 생성 ioctl:

```c
static void o4_job_run(struct work_struct *w)	/* "하드웨어"가 실행 → 완료 IRQ 흉내 */
{
	struct o4_job *job = container_of(to_delayed_work(w), struct o4_job, work);
	struct iosys_map mi, mo;
	int err = drm_gem_vmap(job->in, &mi);

	if (!err) {
		err = drm_gem_vmap(job->out, &mo);
		if (!err) {
			const s32 *x = mi.vaddr;
			s32 *y = mo.vaddr;

			for (u32 i = 0; i < job->count; i++)
				y[i] = 2 * x[i] + 1;		/* 우리 NPU의 유일한 연산 */
			drm_gem_vunmap(job->out, &mo);
		}
		drm_gem_vunmap(job->in, &mi);
	}
	if (err)
		dma_fence_set_error(&job->fence, err);
	drm_gem_object_put(job->in);
	drm_gem_object_put(job->out);
	dma_fence_signal(&job->fence);	/* 기다리던 poll/sync_file/dma_resv 모두 깨어남 */
	dma_fence_put(&job->fence);	/* work가 들고 있던 참조 */
}

// o4_accel.c (2/3) — ioctl: BO 생성, 작업 제출
static int o4_create_bo(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_o4_create_bo *a = data;
	struct drm_gem_shmem_object *shmem;
	int ret;

	if (!a->size)
		return -EINVAL;
	shmem = drm_gem_shmem_create(dev, a->size);	/* shmem 페이지 + mmap offset */
	if (IS_ERR(shmem))
		return PTR_ERR(shmem);
	ret = drm_gem_handle_create(file, &shmem->base, &a->handle);
	a->offset = drm_vma_node_offset_addr(&shmem->base.vma_node);
	drm_gem_object_put(&shmem->base);		/* 이제 handle이 참조를 가진다 */
	return ret;
}
```

③ 제출 ioctl — fence를 만들어 implicit(dma_resv)과 explicit(sync_file) 양쪽으로 내보낸다:

```c
static int o4_submit(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_o4_submit *a = data;
	struct sync_file *sf;
	struct o4_job *job;
	int fd, ret = -ENOENT;

	job = kzalloc(sizeof(*job), GFP_KERNEL);
	if (!job)
		return -ENOMEM;
	job->in = drm_gem_object_lookup(file, a->in_handle);
	job->out = drm_gem_object_lookup(file, a->out_handle);
	if (!job->in || !job->out)
		goto err_put;
	ret = -EINVAL;
	if (!a->count || a->count > job->in->size / 4 || a->count > job->out->size / 4)
		goto err_put;
	job->count = a->count;
	spin_lock_init(&job->lock);
	dma_fence_init(&job->fence, &o4_fence_ops, &job->lock, o4_ctx,
		       atomic_inc_return(&o4_seq));

	/* implicit sync: 출력 BO의 dma_resv에 "쓰는 중" fence를 건다 */
	dma_resv_lock(job->out->resv, NULL);
	ret = dma_resv_reserve_fences(job->out->resv, 1);
	if (!ret)
		dma_resv_add_fence(job->out->resv, &job->fence, DMA_RESV_USAGE_WRITE);
	dma_resv_unlock(job->out->resv);

	/* explicit sync: 같은 fence를 sync_file fd로 돌려준다 */
	fd = ret ? ret : get_unused_fd_flags(O_CLOEXEC);
	sf = fd < 0 ? NULL : sync_file_create(&job->fence);
	if (!sf) {
		if (fd >= 0)
			put_unused_fd(fd);
		dma_fence_signal(&job->fence);	/* 대기자가 영원히 기다리지 않게 */
		drm_gem_object_put(job->in);
		drm_gem_object_put(job->out);
		dma_fence_put(&job->fence);
		return fd < 0 ? fd : -ENOMEM;
	}
	fd_install(fd, sf->file);
	a->out_fence_fd = fd;
	INIT_DELAYED_WORK(&job->work, o4_job_run);
	queue_delayed_work(o4_wq, &job->work, msecs_to_jiffies(latency_ms));
	return 0;

err_put:
	if (job->in)
		drm_gem_object_put(job->in);
	if (job->out)
		drm_gem_object_put(job->out);
	kfree(job);
	return ret;
}
```

④ ioctl 테이블과 `drm_driver`:

```c
// o4_accel.c (3/3) — drm_driver 등록: DRIVER_COMPUTE_ACCEL → /dev/accel/accelN
static const struct drm_ioctl_desc o4_ioctls[] = {
	DRM_IOCTL_DEF_DRV(O4_CREATE_BO, o4_create_bo, 0),
	DRM_IOCTL_DEF_DRV(O4_SUBMIT, o4_submit, 0),
};

DEFINE_DRM_ACCEL_FOPS(o4_fops);		/* .open = accel_open, .mmap = drm_gem_mmap ... */

static const struct drm_driver o4_drv = {
	.driver_features = DRIVER_GEM | DRIVER_COMPUTE_ACCEL,
	.ioctls = o4_ioctls,
	.num_ioctls = ARRAY_SIZE(o4_ioctls),
	.fops = &o4_fops,
	.gem_prime_import_sg_table = drm_gem_shmem_prime_import_sg_table,
	.name = "o4_accel",
	.desc = "O4 toy NPU (y = 2x + 1)",
	.major = 0,
	.minor = 1,
};

static struct faux_device *o4_fdev;	/* 진짜 NPU라면 platform/pci 장치 */
static struct drm_device *o4_ddev;
```

⑤ 모듈 init/exit:

```c
static int __init o4_init(void)
{
	int ret;

	o4_wq = alloc_ordered_workqueue("o4_npu", 0);
	if (!o4_wq)
		return -ENOMEM;
	o4_fdev = faux_device_create("o4_accel", NULL, NULL);
	if (!o4_fdev) {
		ret = -ENODEV;
		goto err_wq;
	}
	dma_coerce_mask_and_coherent(&o4_fdev->dev, DMA_BIT_MASK(64));
	o4_ddev = drm_dev_alloc(&o4_drv, &o4_fdev->dev);
	if (IS_ERR(o4_ddev)) {
		ret = PTR_ERR(o4_ddev);
		goto err_faux;
	}
	o4_ctx = dma_fence_context_alloc(1);
	ret = drm_dev_register(o4_ddev, 0);	/* 여기서 /dev/accel/accel0 생성 */
	if (ret)
		goto err_put;
	return 0;
err_put:
	drm_dev_put(o4_ddev);
err_faux:
	faux_device_destroy(o4_fdev);
err_wq:
	destroy_workqueue(o4_wq);
	return ret;
}

static void __exit o4_exit(void)
{
	drm_dev_unregister(o4_ddev);
	destroy_workqueue(o4_wq);		/* 남은 job을 다 실행·signal 한 뒤 해제 */
	drm_dev_put(o4_ddev);
	faux_device_destroy(o4_fdev);
}
module_init(o4_init);
module_exit(o4_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("O4 toy DRM accel driver");
MODULE_IMPORT_NS("DMA_BUF");
```

```make
obj-m := o4_accel.o
```

설계에서 짚을 점:

- **fence가 job의 첫 멤버**다. `dma_fence_ops`에 `.release`를 주지 않으면 마지막 `dma_fence_put`이 `dma_fence_free()` → `kfree_rcu(fence)`로 그 주소를 해제한다. fence가 첫 멤버라 job 전체가 함께 풀린다. §5.2에서 본 "signal 뒤 드라이버 데이터 접근 금지 + RCU grace period" 계약을 이 구조가 만족한다(lock도 job 안에 있다).
- **참조 수**: fence는 `dma_fence_init`에서 1(work 몫), `dma_resv_add_fence`가 +1, `sync_file_create`가 +1을 가진다. work가 끝에 자기 몫을 put한다. 유저가 sync_file을 닫고 BO가 해제되면 0이 된다.
- **에러 경로에서도 fence를 signal한다**. unsignaled fence를 버리면 그것을 기다리던 누군가가 영원히 잔다 — 실제 드라이버 리뷰에서 가장 많이 지적되는 버그 종류다. (이 예제는 `dma_resv_add_fence` 전에 실패하면 resv에 fence가 걸리지 않으니 signal 후 put만 하면 된다.)
- **ordered workqueue**는 "코어 하나짜리 NPU"를 흉내 낸다: job이 제출 순서대로 하나씩 끝나니, 같은 context(타임라인)의 seqno 순서 약속(§5.1)이 자동으로 지켜진다. 실제 드라이버는 drm_sched가 이 역할을 한다.
- 일부러 뺀 것: **입력 BO의 implicit fence 대기**(앞 job이 아직 쓰는 BO를 입력으로 받으면 기다려야 한다 — rocket은 `drm_sched_job_add_implicit_dependencies`로 처리, §9.1), 타임아웃/리셋, runtime PM, IOMMU. 각각 §9에서 실제 드라이버가 어떻게 하는지 본다.

### 8.2 예제 7 — 빌드

```sh
cp -r ~/.elinux_scratch/O4/mod /var/tmp/O4/mod && cd /var/tmp/O4/mod
make -C /var/lab/out/linux-build-dom M=$PWD modules
modinfo -F depends o4_accel.ko; modinfo -F vermagic o4_accel.ko
```

```text
  CC [M]  o4_accel.o
  MODPOST Module.symvers
  CC [M]  o4_accel.mod.o
  CC [M]  .module-common.o
  LD [M]  o4_accel.ko
make[1]: Leaving directory '/var/tmp/O4/mod'
make: Leaving directory '/var/lab/build/linux-dom'
drm_shmem_helper
6.18.55-dom SMP preempt mod_unload aarch64
```

경고 없이 빌드됐고(출력의 앞부분 `make[1]: Entering ...`은 생략), 의존 모듈은 `drm_shmem_helper` 하나다. DRM core와 accel core(`CONFIG_DRM_ACCEL=y`), sync_file은 커널에 내장돼 있다.

### 8.3 예제 8 — 유저 프로그램: BO 두 개, 제출, 두 가지 방법으로 대기

무엇을 확인하나: `/dev/accel/accel0`을 드라이버 이름으로 찾고, BO를 mmap해 입력을 채우고, 제출 직후(아직 계산 전)와 fence 뒤의 상태를 비교한다. 두 번째 제출에서는 sync_file을 쓰지 않고 `DMA_BUF_IOCTL_SYNC`로 implicit 대기를 측정한다.

```c
// accel_user.c — /dev/accel/accel0 (o4_accel) 사용: BO 2개 → 입력 채움 → submit → fence 대기
#include "o4_drm.h"
#include <poll.h>
#include <linux/dma-buf.h>
#include "mod/o4_accel_uapi.h"

static uint32_t create_bo(int fd, uint32_t size, int32_t **map)
{
	struct drm_o4_create_bo c = { .size = size };
	if (ioctl(fd, DRM_IOCTL_O4_CREATE_BO, &c) < 0) DIE("O4_CREATE_BO");
	*map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, c.offset);
	if (*map == MAP_FAILED) DIE("mmap BO");
	printf("BO handle=%u mmap offset=0x%llx\n", c.handle, (unsigned long long)c.offset);
	return c.handle;
}

int main(void)
{
	int fd = drm_open_by_name("/dev/accel/accel*", "o4_accel");
	if (fd < 0) { fprintf(stderr, "no o4_accel\n"); return 1; }
	enum { N = 1024 };
	int32_t *x, *y;
	uint32_t hin = create_bo(fd, N * 4, &x), hout = create_bo(fd, N * 4, &y);
	for (int i = 0; i < N; i++) { x[i] = i; y[i] = -1; }

	int ob = prime_export(fd, hout);            /* 출력 BO를 dma-buf로: 다른 장치/프로세스용 */
	struct drm_o4_submit s = { .in_handle = hin, .out_handle = hout, .count = N };
	uint64_t t0 = now_ns();
	if (ioctl(fd, DRM_IOCTL_O4_SUBMIT, &s) < 0) DIE("O4_SUBMIT");
	uint64_t t1 = now_ns();
	struct pollfd pd = { .fd = ob, .events = POLLIN };
	int y3 = y[3], ip = poll(&pd, 1, 0);        /* 출력(printf)보다 먼저 관찰 */
	printf("submit returned in %.3f ms, out_fence_fd=%d, y[3]=%d\n",
	       (t1 - t0) / 1e6, s.out_fence_fd, y3);
	printf("implicit: poll(out dma-buf, 0) = %d\n", ip);

	struct pollfd pf = { .fd = s.out_fence_fd, .events = POLLIN };
	int r = poll(&pf, 1, 1000);                 /* explicit: sync_file을 기다린다 */
	printf("explicit: poll(sync_file) = %d after %.1f ms\n", r, (now_ns() - t1) / 1e6);
	printf("implicit: poll(out dma-buf, 0) = %d\n", poll(&pd, 1, 0));
	int bad = 0;
	for (int i = 0; i < N; i++) bad += (y[i] != 2 * x[i] + 1);
	printf("y[0..3] = %d %d %d %d, mismatches=%d\n", y[0], y[1], y[2], y[3], bad);

	/* 두 번째 job: 끝나기 전에 CPU가 DMA_BUF_IOCTL_SYNC로 접근하면 커널이 기다려 준다 */
	if (ioctl(fd, DRM_IOCTL_O4_SUBMIT, &s) < 0) DIE("O4_SUBMIT 2");
	t0 = now_ns();
	struct dma_buf_sync sy = { DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ };
	if (ioctl(ob, DMA_BUF_IOCTL_SYNC, &sy) < 0) DIE("SYNC");
	printf("DMA_BUF_IOCTL_SYNC(START) waited %.1f ms for job 2\n", (now_ns() - t0) / 1e6);
	sy.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
	if (ioctl(ob, DMA_BUF_IOCTL_SYNC, &sy) < 0) DIE("SYNC end");
	return 0;
}
```

```sh
sh boot.sh accel     # modprobe drm_shmem_helper && insmod o4_accel.ko latency_ms=200 → 노드 확인 → ./accel_user
```

```text
lab initramfs: 6.18.55-dom up 16.22s
[   20.190298][   T80] o4_accel: loading out-of-tree module taints kernel.
[   20.192101][   T80] o4_accel: module verification failed: signature and/or required key missing - tainting kernel
[   20.214907][   T80] [drm] Initialized o4_accel 0.1.0 for o4_accel on minor 0
total 0
crw-------    1 0        0         261,   0 Oct 18 16:25 accel0
accel0
lrwxrwxrwx    1 0        0                0 Oct 18 16:25 /sys/class/accel/accel0/device -> ../../../o4_accel
o4_accel
/dev/accel/accel0 -> driver 'o4_accel'
BO handle=1 mmap offset=0x100000000
BO handle=2 mmap offset=0x100001000
submit returned in 0.796 ms, out_fence_fd=5, y[3]=-1
implicit: poll(out dma-buf, 0) = 0
explicit: poll(sync_file) = 1 after 215.5 ms
implicit: poll(out dma-buf, 0) = 1
y[0..3] = 1 3 5 7, mismatches=0
DMA_BUF_IOCTL_SYNC(START) waited 221.4 ms for job 2
```

출력에서 볼 것:

- `drm_dev_register`가 `Initialized o4_accel 0.1.0 ... on minor 0`을 찍고 **`/dev/accel/accel0`, major 261**이 생겼다. `/sys/class/accel/accel0/device`는 부모인 faux 장치 `o4_accel`을 가리키고, debugfs에는 `/sys/kernel/debug/accel/o4_accel/`이 생겼다(마지막 `ls`의 `o4_accel`). 실제 NPU면 `device` 링크가 `fdab0000.npu` 같은 platform 장치나 PCI 주소를 가리킨다.
- 같은 `DRM_IOCTL_VERSION`이 accel 노드에서도 통한다 — accel 장치는 DRM 장치다.
- mmap 오프셋은 `0x100000000`부터 시작하는 가짜 오프셋이고, BO마다 한 페이지씩 다르다(GEM의 `drm_vma_offset_manager`).
- 제출은 0.8 ms 만에 **비동기로** 돌아왔고 그때 `y[3]`는 아직 -1, 출력 BO의 implicit fence 때문에 `poll(dma-buf)`도 0이다. 약 200 ms(`latency_ms=200`) 뒤 sync_file이 ready가 되고, 그때는 dma-buf poll도 1, 결과는 `2x+1`로 1024개 모두 맞다.
- 두 번째 job에서 CPU가 `DMA_BUF_IOCTL_SYNC(START)`를 부르자 **커널이 job 완료까지 221 ms를 대신 기다렸다**. 우리 드라이버는 `dma_resv_add_fence`만 했을 뿐인데, dma-buf core의 `begin_cpu_access`가 그 fence를 존중한 것이다. **드라이버가 공용 부품 규칙을 따르면 대기·공유가 공짜로 따라온다** — 이것이 vendor 전용 misc 드라이버 대신 accel 서브시스템을 쓰는 실질적인 이유다.
- 첫 시도에서는 `latency_ms=20`으로 돌렸는데, 직렬 콘솔 출력(`printf`)이 TCG에서 20 ms보다 오래 걸려 "제출 직후" 관찰 전에 job이 끝나 버렸다. 그래서 관찰을 printf 앞으로 옮기고 지연을 200 ms로 늘렸다 — 측정 대상이 짧을수록 관찰 도구의 비용이 결과를 바꾼다는, Don이 scope로 늘 겪는 문제와 같다.

---

## 9. 실제 NPU 드라이버 읽기 — rocket, ivpu, qaic, amdxdna, habanalabs

### 9.1 예제 9 — 랩 트리에서 다섯 드라이버의 "모양" 재기

무엇을 확인하나: 각 드라이버의 크기와, 펌웨어 로딩·drm_sched·runtime PM·IOMMU API·`dma_resv_add_fence`를 쓰는 `.c` 파일 수. 숫자는 grep으로 센 파일 수라 대략적인 지도일 뿐이다.

```sh
#!/bin/sh
# survey.sh — 랩 커널 트리의 accel 드라이버 5개: 크기, 펌웨어, 스케줄러, PM, IOMMU 사용 여부
cd /var/lab/src/linux-6.18.55/drivers/accel
printf "%-11s %6s %5s %6s %6s %7s %6s\n" driver lines fw sched rpm iommu resv
for d in rocket ivpu qaic amdxdna habanalabs; do
  L=$(find $d -name '*.[ch]' | xargs cat | wc -l)
  c() { grep -rl "$1" $d --include='*.c' | wc -l; }
  printf "%-11s %6s %5s %6s %6s %7s %6s\n" $d $L "$(c request_firmware)" "$(c drm_sched_init)" \
    "$(c pm_runtime_)" "$(c iommu_)" "$(c dma_resv_add_fence)"
done
grep -rhn "MODULE_FIRMWARE" ivpu amdxdna qaic habanalabs | sed 's/^[0-9]*://' | sort -u | head -6
grep -rn "set_autosuspend_delay" rocket ivpu qaic amdxdna | sed 's/(.*//'
```

```text
driver       lines    fw  sched    rpm   iommu   resv
rocket        5933     0      1      3       4      1
ivpu         14098     1      0      7       0      1
qaic          7890     0      0      0       0      0
amdxdna       8756     1      1      1       1      1
habanalabs  337290     2      0      0       2      0
MODULE_FIRMWARE("amdnpu/1502_00/npu.sbin");
MODULE_FIRMWARE("amdnpu/17f0_10/npu.sbin");
MODULE_FIRMWARE("amdnpu/17f0_11/npu.sbin");
MODULE_FIRMWARE("amdnpu/17f0_20/npu.sbin");
MODULE_FIRMWARE("intel/vpu/vpu_37xx_v1.bin");
MODULE_FIRMWARE("intel/vpu/vpu_40xx_v1.bin");
```

```text
rocket/rocket_core.c:76:	pm_runtime_set_autosuspend_delay
ivpu/ivpu_pm.c:414:	pm_runtime_set_autosuspend_delay
amdxdna/amdxdna_pci_drv.c:316:	pm_runtime_set_autosuspend_delay
```

출력에서 볼 것:

- **rocket은 펌웨어가 없다**(fw 0). 커널이 NPU 레지스터를 직접 프로그래밍하고 drm_sched로 job을 돌리는 "GPU식" 드라이버다. rocket의 5933줄 중 상당 부분은 `rocket_registers.h`(레지스터 정의)다.
- **ivpu·amdxdna는 펌웨어를 로드**하고(`intel/vpu/*.bin`, `amdnpu/*/npu.sbin`), 장치 안의 마이크로컨트롤러가 job을 스케줄한다. ivpu는 drm_sched를 쓰지 않는다(sched 0) — 펌웨어가 스케줄러다.
- **qaic는 request_firmware도 runtime PM도 0**이다. PCIe 카드라 펌웨어 이미지를 장치가 MHI 위의 Sahara 프로토콜로 요청하면 보내 주는 구조이고(`sahara.c`의 `qcom/aic200/*.mbn` 목록, 소스 확인), 데이터센터 카드라 runtime PM을 하지 않는다.
- **habanalabs는 33만 줄**로 나머지 넷을 합친 것의 여덟 배가 넘는다. Gaudi 세대별 하드웨어 정의가 대부분이다.
- ivpu의 iommu 0은 §4.2에서 말한 "NPU 내부 MMU를 직접 다룬다"와 맞는다.

### 9.2 rocket — 커널이 스케줄하는 임베디드 NPU (RK3588)

job 제출 경로(랩 소스 `drivers/accel/rocket/rocket_job.c`, 핵심 줄만 발췌 — 소스 확인):

```c
/* rocket_job_push(): 입출력 BO의 dma_resv를 잠그고, 의존성과 출력 fence를 건다 (발췌) */
ret = drm_gem_lock_reservations(bos, bo_count, &acquire_ctx);
scoped_guard(mutex, &rdev->sched_lock) {
	drm_sched_job_arm(&job->base);
	job->inference_done_fence = dma_fence_get(&job->base.s_fence->finished);
	ret = rocket_acquire_object_fences(job->in_bos, job->in_bo_count, &job->base, false);
	ret = rocket_acquire_object_fences(job->out_bos, job->out_bo_count, &job->base, true);
	drm_sched_entity_push_job(&job->base);
}
rocket_attach_object_fences(job->out_bos, job->out_bo_count, job->inference_done_fence);
drm_gem_unlock_reservations(bos, bo_count, &acquire_ctx);
```

```c
/* rocket_job_run(): drm_sched가 의존성이 다 풀린 job을 하드웨어에 넣을 때 (발췌) */
ret = pm_runtime_resume_and_get(core->dev);                  /* NPU 전원/클럭 켜기 */
ret = iommu_attach_group(job->domain->domain, core->iommu_group);  /* 이 프로세스 주소 공간 */
scoped_guard(mutex, &core->job_lock) {
	core->in_flight_job = job;
	rocket_job_hw_submit(core, job);   /* PC 레지스터: 명령 버퍼 주소, 개수, 인터럽트 마스크, OP_EN */
}
return fence;
```

```c
/* rocket_job_handle_irq(): 완료 인터럽트 — 다음 task가 남았으면 이어서, 아니면 fence signal (발췌) */
rocket_pc_writel(core, OPERATION_ENABLE, 0x0);
rocket_pc_writel(core, INTERRUPT_CLEAR, 0x1ffff);
if (core->in_flight_job->next_task_idx < core->in_flight_job->task_count) {
	rocket_job_hw_submit(core, core->in_flight_job);
	return;
}
iommu_detach_group(NULL, iommu_group_get(core->dev));
dma_fence_signal(core->in_flight_job->done_fence);
pm_runtime_put_autosuspend(core->dev);                       /* 50 ms 뒤 자동 절전 */
```

읽는 법 — 장난감 드라이버와 비교:

- `drm_gem_lock_reservations`는 **여러 BO의 dma_resv를 ww_mutex로 한꺼번에** 잠근다(데드락 회피 알고리즘이 들어 있다). 입력 BO는 읽기, 출력 BO는 쓰기로 의존성을 추가하고(`drm_sched_job_add_implicit_dependencies`), 출력 BO에는 이 job의 finished fence를 WRITE로 건다. 장난감 드라이버가 뺀 "입력 대기"가 여기 있다.
- drm_sched가 의존성이 다 풀린 job에 대해 `run_job`을 부르고, 그 안에서 runtime PM get → IOMMU 도메인 교체 → 레지스터 쓰기. 완료 IRQ에서 반대 순서로 푼다. `rocket_core.c`는 `pm_runtime_set_autosuspend_delay(dev, 50)`(소스 확인)로 마지막 job 뒤 50 ms 동안 켜 둔다 — 연속 추론 사이마다 전원을 껐다 켜는 비용을 피하는 표준 패턴(N6).
- 타임아웃: `.timedout_job = rocket_job_timedout` → `drm_sched_stop` → 코어 리셋 → `drm_sched_start`. GPU 드라이버의 TDR(timeout detection and recovery)과 같다.
- 유저 공간: Mesa의 rocket Gallium 드라이버 + Teflon delegate가 TFLite 모델을 레지스터 명령 버퍼로 컴파일한다(기억 기반 — Mesa 문서로 확인 필요).

### 9.3 ivpu — 펌웨어가 스케줄하는 NPU (Intel Core Ultra)

ivpu는 SSD를 아는 Don에게 가장 익숙한 구조다. 제출 경로(`ivpu_job.c`, 발췌 — 소스 확인):

```c
/* ivpu_cmdq_push_job(): 공유 메모리 job queue에 엔트리를 쓰고 tail을 민다 (발췌) */
u32 tail = READ_ONCE(header->tail);
u32 next_entry = (tail + 1) % cmdq->entry_count;
if (next_entry == header->head)
	return -EBUSY;                                  /* 링이 가득 참 */
entry = &cmdq->jobq->slot[tail].job;
entry->batch_buf_addr = job->cmd_buf_vpu_addr;  /* NPU 주소 공간의 명령 버퍼 */
entry->job_id = job->job_id;
```

```c
/* ivpu_job_submit(): 엔트리를 넣고 doorbell을 울린다 (발췌) */
ret = ivpu_cmdq_push_job(cmdq, job);
ivpu_start_job_timeout_detection(vdev);
ivpu_cmdq_ring_db(vdev, cmdq);                  /* doorbell: 펌웨어에게 "새 일 있음" */
```

- **job queue(링) + head/tail + doorbell** — NVMe의 SQ/SQ tail doorbell과 같은 구조다. 링 메모리는 호스트와 NPU가 공유하고, 펌웨어(JSM, job scheduling manager)가 head를 민다. 완료는 IPC 메시지(`ivpu_ipc.c`)로 오고 `ivpu_job_signal_and_destroy()`가 fence를 signal한다.
- **펌웨어 로딩**: `ivpu_fw.c`가 하드웨어 세대별 이름(`intel/vpu/vpu_37xx_v1.bin` 등)을 차례로 `firmware_request_nowarn`으로 시도한다(G10).
- **행(hang) 감지**: TDR 타이머가 만료되면 펌웨어 **heartbeat 카운터**가 늘었는지 본다(`ivpu_pm.c`). 늘지 않았으면 "펌웨어가 죽었다"로 보고 recovery(장치 리셋 + 펌웨어 재부팅)를 한다. 늘었으면 "오래 걸리는 추론"으로 보고 다시 기다린다(최대 재시도 횟수 있음). SSD 펌웨어의 watchdog/heartbeat와 같은 발상이다.
- **전원**: runtime suspend 콜백이 펌웨어에 D0i3 진입 메시지(`ivpu_jsm_pwr_d0i3_enter`)를 보낸 뒤 전원을 내린다. 장치 안 펌웨어가 있는 가속기는 "커널이 클럭을 끈다"가 아니라 "**펌웨어와 협상해서** 끈다"가 된다.

### 9.4 qaic, amdxdna, habanalabs — 한 줄씩

- **qaic**(Qualcomm Cloud AI 100, PCIe): 제어는 **MHI**(Modem Host Interface, Qualcomm의 PCIe 채널 프로토콜) 위의 메시지(`QAIC_MANAGE`), 데이터는 **DBC**(DMA Bridge Channel)라는 하드웨어 큐. 문서 `Documentation/accel/qaic/qaic.rst`에는 **IRQ storm 대책**이 적혀 있다: 응답 FIFO가 빨리 차고 비는 워크로드(lprnet)는 초당 10만 개 넘는 MSI를 만들어 호스트를 죽이므로, IRQ가 오면 라인을 끄고 FIFO를 비운 뒤 잠깐 폴링하다가 조용하면 다시 켠다 — 문서 기준으로 5분 동안 약 64개 IRQ로 줄었다(소스 문서 확인). NAPI(I6)와 같은 생각이다.
- **amdxdna**(AMD Ryzen AI NPU, PCIe 내장): 문서 `Documentation/accel/amdxdna/amdnpu.rst` 기준으로 2D 타일 배열(compute tile + memory tile), 열(column) 단위 파티션을 hardware context(`CREATE_HWCTX`)에 묶는다. 펌웨어는 **PSP**(보안 프로세서)가 서명을 검증해 로드하고, 명령은 **mailbox** 링으로 마이크로컨트롤러의 ERT(명령 처리기)에 보낸다. 대기는 timeline syncobj.
- **habanalabs**(Intel Gaudi, 데이터센터 학습): DRM 이전부터 있던 드라이버를 accel로 옮긴 것이라 GEM을 안 쓰고 자체 ioctl·메모리 관리자를 가진다. "accel에 들어왔다고 다 같은 모양은 아니다"의 예.

### 9.5 다섯 개 비교

| | rocket | ivpu | qaic | amdxdna | habanalabs |
|---|---|---|---|---|---|
| 연결 | SoC 내장(DT, platform) | SoC 내장(PCI 함수) | PCIe 카드 | APU 내장(PCI 함수) | PCIe/OAM 카드 |
| 스케줄 | 커널 drm_sched | NPU 펌웨어 | 장치 펌웨어 | 펌웨어(ERT) + drm_sched | 장치 큐 |
| 펌웨어 로드 | 없음 | `request_firmware` | 장치가 MHI/Sahara로 요청 | PSP가 서명 검증 후 로드 | `request_firmware` |
| 장치 주소 | Rockchip IOMMU, 파일별 domain | NPU 내부 MMU | 호스트 DMA + 카드 DRAM | IOMMU/SVA | 장치 MMU |
| 제출 단위 | job = task 목록 + in/out BO | cmdq 엔트리 + doorbell | BO 실행(slice) | 명령 BO + hwctx | CS |
| 완료 | IRQ → fence | IPC 메시지 → fence | DBC 응답 FIFO | mailbox → fence/syncobj | CS 완료 |
| 전원 | runtime PM, autosuspend 50 ms | runtime PM + 펌웨어 D0i3 | 없음 | runtime PM | 없음 |

표의 "연결", "스케줄", "펌웨어 로드", "전원"은 §9.1~§9.4의 소스 확인 결과다. amdxdna의 SVA, habanalabs의 OAM 폼팩터는 기억 기반이다.

---

## 10. 메인라인 밖의 스택 — Jetson과 벤더 NPU (기억 기반)

> 이 절의 내용은 **랩 소스로 확인할 수 없는 기억 기반 정리**다. 제품·버전마다 다르니 해당 벤더 문서로 확인해야 한다. 확인된 사실만 "소스 확인"으로 표시했다.

### 10.1 NVIDIA Jetson (L4T)

- 커널은 NVIDIA가 배포하는 L4T(Linux for Tegra) 커널이다. GPU는 메인라인 nouveau가 아니라 **out-of-tree `nvgpu`** 드라이버, 메모리 할당·공유는 **`nvmap`**(자체 메모리 관리자, 예전에는 dma-buf와 별도 handle 체계)이다. DLA(딥러닝 가속기)와 PVA(비전 가속기)도 out-of-tree 드라이버 + 펌웨어 구조다.
- 메인라인 커널에도 Tegra234(Orin) 메모리 컨트롤러 표에 **NVDLA 메모리 클라이언트**(`dla0rdb` 등, `drivers/memory/tegra/tegra234.c`)와 host1x의 NVDLA 스트림 ID 항목(`drivers/gpu/host1x/dev.c`)은 있다(소스 확인). 하지만 DLA를 실제로 쓰는 메인라인 드라이버는 없다.
- 유저 공간: 카메라·코덱·추론이 **`NvBufSurface`**(멀티미디어 버퍼 구조체 — 내부적으로 dma-buf fd를 담는다)로 버퍼를 주고받고, DeepStream/GStreamer 플러그인(`nvarguscamerasrc`, `nvvidconv`, `nvinfer`)과 TensorRT가 그 위에 있다. 유저가 dma-buf fd를 다루는 모양은 이 노트와 같지만 할당자와 sync 방식은 NVIDIA 전용이다.
- 면접 포인트: "Jetson에서 V4L2 카메라 프레임을 TensorRT로 넘길 때 복사가 몇 번 생기나?"에 답하려면 버퍼가 `NvBufSurface`(=dma-buf)로 유지되는지, 중간에 CPU 메모리로 `memcpy`되는 플러그인이 끼는지를 본다.

### 10.2 그 밖의 벤더 NPU (기억 기반)

| SoC/제품 | NPU 스택 | 메인라인 상태 |
|---|---|---|
| Rockchip RK3588 | 벤더 `rknpu` 드라이버 + RKNN 런타임(blob) | **rocket이 메인라인**(6.18 트리에 있음, 소스 확인) — Mesa Teflon과 함께 |
| NXP i.MX 8M Plus | Vivante VIP8000 NPU, 벤더 `galcore` + NNAPI/TFLite delegate | **etnaviv**가 NPU에서도 뜬다(소스: `etnaviv_gpu.c`가 nn_core가 있으면 "UAPI is still experimental" 경고, i.MX8MP DT에 `npu@38500000 compatible = "vivante,gc"`) |
| NXP i.MX 93/95 | Arm Ethos-U65 (i.MX93), 벤더 Neutron NPU (i.MX95) | VM 호스트 7.0 커널 uAPI에 `ethosu_accel.h`가 있다(I8 §11.2) — 6.18 이후 메인라인 |
| TI AM62A/AM68A/TDA4 | C7x DSP + MMA, TIDL 런타임, remoteproc로 펌웨어 로드(O2) | DSP 펌웨어 + rpmsg 구조 |
| Qualcomm | Hexagon DSP/NPU, FastRPC | `drivers/misc/fastrpc.c`가 메인라인(소스 확인) — misc 장치 + ioctl |
| Hailo-8 (M.2/PCIe) | `hailo_pci` 드라이버 + HailoRT | out-of-tree |

공통 패턴: **"벤더 커널 드라이버 + 바이너리 런타임"이 기본이고, 메인라인 accel 드라이버가 서서히 생기는 중**이다. 벤더 스택을 받는 회사에서 Don이 맡을 일은 대개 ① 벤더 커널 모듈을 우리 커널 버전으로 포팅(P2, P5), ② DT/전원/클럭 bring-up(P1, P4), ③ 카메라 → NPU 버퍼 경로에서 복사 제거(§12), ④ 펌웨어 로딩·리셋·hang 복구 정책이다.

---

## 11. 유저 공간 — 런타임과 API (기억 기반)

| 계층 | 예 | 커널과 만나는 곳 |
|---|---|---|
| 프레임워크 | TensorFlow Lite, ONNX Runtime, PyTorch(ExecuTorch) | delegate / execution provider로 가속기 선택 |
| delegate / EP | **Mesa Teflon**(TFLite external delegate, etnaviv·rocket용), 벤더 delegate(RKNN, NNAPI, TIDL, TensorRT EP) | UMD 호출 |
| GPU compute | OpenCL(Mesa rusticl, 벤더), Vulkan compute | `/dev/dri/renderD*` |
| UMD | Mesa Gallium 드라이버(rocket, etnaviv), 벤더 `.so` | `/dev/accel/*` 또는 벤더 노드 ioctl |
| 버퍼 공유 | GStreamer `io-mode=dmabuf`, EGL `EGL_EXT_image_dma_buf_import`, Vulkan `VK_EXT_external_memory_dma_buf`, libcamera `FrameBuffer`(dma-buf fd) | dma-buf fd |

Teflon의 사용 모양은 대략 이렇다(기억 기반 — Mesa 문서 `docs/teflon.rst`로 확인 필요): TFLite 인터프리터를 만들 때 external delegate로 `libteflon.so`를 지정하면, 지원되는 연산(합성곱 등)만 NPU로 보내고 나머지는 CPU에서 돈다. 그래서 **"모델의 어떤 레이어가 NPU에 올라갔나"**가 성능을 좌우하고, 지원 안 되는 연산 하나 때문에 텐서가 CPU ↔ NPU를 오가며 복사되는 것이 흔한 성능 함정이다.

---

## 12. 카메라 → NPU zero-copy (Q2와 연결)

### 12.1 파이프라인

센서 키트에서 1080p 카메라 프레임을 NPU 추론에 넣는 경로를 버퍼 관점으로 쓰면 이렇다(Q2가 V4L2/libcamera 쪽을 자세히 다룬다).

1. 버퍼 할당: V4L2가 직접(`VIDIOC_REQBUFS` MMAP → `VIDIOC_EXPBUF`로 dma-buf fd) 또는 **heap에서 받아 V4L2에 import**(`V4L2_MEMORY_DMABUF`). 파이프라인의 모든 장치가 받을 수 있는 heap을 고른다(§3.3).
2. 캡처: `VIDIOC_DQBUF` — 버퍼 인덱스와 타임스탬프(Q3)를 받는다. V4L2는 대부분 implicit/fence 없이 DQBUF 자체가 "다 썼다"는 신호다.
3. 전처리: ISP/2D 엔진(리사이즈, NV12 → RGB)이 같은 fd를 import.
4. 추론: NPU UMD가 fd를 import(PRIME `FD_TO_HANDLE`) → 입력 BO로 제출 → out-fence.
5. 반환: 추론이 입력 BO를 다 읽은 뒤(fence) `VIDIOC_QBUF`로 캡처 큐에 돌려준다. **이 반환 프로토콜이 없으면** 카메라가 NPU가 읽는 중인 버퍼에 다음 프레임을 덮어쓴다.

### 12.2 예제 10 — 복사 vs fd 전달, 프레임 크기별

무엇을 확인하나: 프로세스 사이에 프레임을 넘길 때 (a) 소켓으로 바이트를 복사하는 경우와 (b) 미리 할당한 dma-buf 링(4장)의 fd만 `SCM_RIGHTS`로 넘기고 받는 쪽이 mmap해 한 바이트 읽는 경우의 프레임당 시간. (b)에는 매번 mmap/munmap 비용이 들어 있다(실제 파이프라인은 링 버퍼를 한 번 mmap해 두고 인덱스만 넘기므로 더 싸다).

```c
// copybench.c — 프레임을 "복사해서 보내기"(socket) vs "dma-buf fd만 보내기"(SCM_RIGHTS)
#include "o4.h"
#include <sys/wait.h>
#include <linux/dma-heap.h>

#define NBUF 4

static int heap_alloc(size_t len)
{
	int h = open("/dev/dma_heap/system", O_RDONLY);
	if (h < 0) DIE("heap");
	struct dma_heap_allocation_data a = { .len = len, .fd_flags = O_RDWR };
	if (ioctl(h, DMA_HEAP_IOCTL_ALLOC, &a) < 0) DIE("ALLOC");
	close(h);
	return a.fd;
}

int main(int argc, char **argv)
{
	size_t len = (argc > 1 ? atoi(argv[1]) : 3110400);   /* 1920x1080 NV12 = 3,110,400 B */
	int frames = argc > 2 ? atoi(argv[2]) : 30;
	int sv[2];
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) DIE("socketpair");
	int sz = 4 << 20;
	setsockopt(sv[0], SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
	pid_t pid = fork();
	if (pid == 0) {                                  /* 소비자 */
		char *rx = malloc(len);
		volatile uint64_t sum = 0;
		for (int f = 0; f < frames; f++) {           /* 1) 복사 경로: len 바이트를 다 받는다 */
			size_t got = 0;
			while (got < len) { ssize_t n = read(sv[1], rx + got, len - got); if (n <= 0) DIE("read"); got += n; }
			sum += (uint8_t)rx[len - 1];
		}
		for (int f = 0; f < frames; f++) {           /* 2) zero-copy: fd만 받아 첫 접근 */
			int fd = recv_fd(sv[1]);
			uint8_t *p = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
			if (p == MAP_FAILED) DIE("mmap");
			sum += p[len - 1];
			munmap(p, len); close(fd);
		}
		_exit(sum == 12345678 ? 1 : 0);
	}
	char *tx = malloc(len);
	memset(tx, 0x5a, len);
	uint64_t t0 = now_ns();
	for (int f = 0; f < frames; f++)
		for (size_t off = 0; off < len; ) { ssize_t n = write(sv[0], tx + off, len - off); if (n <= 0) DIE("write"); off += n; }
	uint64_t t1 = now_ns();
	int bufs[NBUF];
	for (int i = 0; i < NBUF; i++) bufs[i] = heap_alloc(len);   /* 링 버퍼 4장 미리 할당 */
	uint64_t t2 = now_ns();
	for (int f = 0; f < frames; f++) send_fd(sv[0], bufs[f % NBUF]);
	waitpid(pid, NULL, 0);
	uint64_t t3 = now_ns();
	printf("frame=%zu B x %d\n", len, frames);
	printf("copy (socket write/read) : %8.3f ms/frame\n", (t1 - t0) / 1e6 / frames);
	printf("zero-copy (fd + mmap)    : %8.3f ms/frame\n", (t3 - t2) / 1e6 / frames);
	return 0;
}
```

```sh
sh boot.sh bench     # 크기 4종 x 20프레임, 3회 반복
```

```text
lab initramfs: 6.18.55-dom up 7.46s
frame=65536 B x 20
copy (socket write/read) :    4.412 ms/frame
zero-copy (fd + mmap)    :    1.542 ms/frame
frame=1048576 B x 20
copy (socket write/read) :   88.786 ms/frame
zero-copy (fd + mmap)    :    5.239 ms/frame
frame=3110400 B x 20
copy (socket write/read) :   17.001 ms/frame
zero-copy (fd + mmap)    :    7.725 ms/frame
frame=8294400 B x 20
copy (socket write/read) :  113.567 ms/frame
zero-copy (fd + mmap)    :   16.371 ms/frame
frame=65536 B x 20
copy (socket write/read) :    1.578 ms/frame
zero-copy (fd + mmap)    :    1.376 ms/frame
frame=1048576 B x 20
copy (socket write/read) :   10.285 ms/frame
zero-copy (fd + mmap)    :    4.086 ms/frame
frame=3110400 B x 20
copy (socket write/read) :    7.752 ms/frame
zero-copy (fd + mmap)    :    5.998 ms/frame
frame=8294400 B x 20
copy (socket write/read) :   95.590 ms/frame
zero-copy (fd + mmap)    :   13.072 ms/frame
frame=65536 B x 20
copy (socket write/read) :    0.365 ms/frame
zero-copy (fd + mmap)    :    2.287 ms/frame
frame=1048576 B x 20
copy (socket write/read) :   10.362 ms/frame
zero-copy (fd + mmap)    :    4.737 ms/frame
frame=3110400 B x 20
copy (socket write/read) :   21.347 ms/frame
zero-copy (fd + mmap)    :    4.542 ms/frame
frame=8294400 B x 20
copy (socket write/read) :   33.138 ms/frame
zero-copy (fd + mmap)    :   19.193 ms/frame
```

측정 당시 VM load average는 약 23이었다(다른 작성자들의 QEMU가 동시에 돎). 그래서 같은 측정이 실행마다 몇 배씩 흔들린다. 각 크기의 3회 중앙값을 그리면:

```svg
<svg viewBox="0 0 640 330" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="270" x2="620" y2="270" stroke="currentColor"/>
<line x1="70" y1="270" x2="70" y2="40" stroke="currentColor"/>
<text x="62" y="274" text-anchor="end" font-size="12">0</text>
<text x="62" y="219" text-anchor="end" font-size="12">25</text>
<line x1="70" y1="215" x2="620" y2="215" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="164" text-anchor="end" font-size="12">50</text>
<line x1="70" y1="160" x2="620" y2="160" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="109" text-anchor="end" font-size="12">75</text>
<line x1="70" y1="105" x2="620" y2="105" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="54" text-anchor="end" font-size="12">100</text>
<line x1="70" y1="50" x2="620" y2="50" stroke="#888" stroke-opacity="0.3"/>
<text x="16" y="160" font-size="12" transform="rotate(-90 16 160)" text-anchor="middle">ms / frame (중앙값, 3회)</text>
<rect x="95" y="267" width="42" height="3" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="116" y="262" text-anchor="middle" font-size="12">1.6</text>
<rect x="141" y="267" width="42" height="3" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="162" y="262" text-anchor="middle" font-size="12">1.5</text>
<text x="139" y="288" text-anchor="middle" font-size="12">64 KiB</text>
<rect x="232" y="247" width="42" height="23" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="253" y="242" text-anchor="middle" font-size="12">10.4</text>
<rect x="278" y="260" width="42" height="10" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="299" y="255" text-anchor="middle" font-size="12">4.7</text>
<text x="276" y="288" text-anchor="middle" font-size="12">1 MiB</text>
<rect x="370" y="233" width="42" height="37" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="391" y="228" text-anchor="middle" font-size="12">17.0</text>
<rect x="416" y="257" width="42" height="13" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="437" y="252" text-anchor="middle" font-size="12">6.0</text>
<text x="414" y="288" text-anchor="middle" font-size="12">1080p NV12</text>
<rect x="507" y="60" width="42" height="210" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="528" y="55" text-anchor="middle" font-size="12">95.6</text>
<rect x="553" y="234" width="42" height="36" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="574" y="229" text-anchor="middle" font-size="12">16.4</text>
<text x="551" y="288" text-anchor="middle" font-size="12">4K NV12</text>
<rect x="90" y="12" width="14" height="14" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="110" y="24" font-size="12">복사 (socket write/read)</text>
<rect x="300" y="12" width="14" height="14" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="320" y="24" font-size="12">무복사 (dma-buf fd 전달 + mmap)</text>
<text x="320" y="318" text-anchor="middle" font-size="12">QEMU TCG, 공유 VM load ~23 — 절대값 무의미, 상대 비교만</text>
</svg>
```

그림 4 — 프레임 크기별 프레임당 시간(중앙값 3회, 단위 ms). 주황 = 소켓으로 바이트 복사, 초록 = dma-buf fd만 전달 + mmap. QEMU TCG + 과부하 VM 결과라 **절대값은 실제 보드와 무관**하고, 크기에 따른 경향만 본다.

출력과 그림에서 볼 것:

- 64 KiB에서는 둘이 비슷하거나 복사가 더 빠를 때도 있다(3회차 0.365 vs 2.287 ms). **작은 데이터는 복사가 싸다** — fd 전달·mmap·page fault의 고정 비용이 더 크다. IMU 샘플 같은 작은 메시지에 dma-buf를 쓰는 것은 과잉이다(A7의 공유 메모리 링이 맞다).
- 1 MiB 이상에서는 복사 비용이 크기에 비례해 커지고(중앙값 10.4 → 17.0 → 95.6 ms), fd 전달은 훨씬 완만하다(4.7 → 6.0 → 16.4 ms). 무복사 쪽이 커지는 이유는 크기에 비례하는 mmap/munmap(페이지 테이블 설정·해제) 비용이다 — 한 번 mmap해 두면 이것도 사라진다.
- 실제 SoC에서 더 중요한 것은 시간보다 **DRAM 대역폭**이다. 1080p NV12 30 fps를 한 번 복사하면 읽기+쓰기로 약 187 MB/s(3.1 MB x 2 x 30)를 더 쓴다. 카메라 4대에 단계마다 복사하면 LPDDR4 대역폭의 상당 부분이 memcpy로 사라지고, 그 대역폭을 NPU가 가중치 읽기에 써야 했다.

---

## 13. 커널 안에서는 무슨 일이 일어나나

### 13.1 객체 관계

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="200" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="33" text-anchor="middle" font-size="13">accel fd (struct file)</text>
  <text x="110" y="54" text-anchor="middle" font-size="12">private_data = drm_file</text>
  <rect x="10" y="100" width="200" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="123" text-anchor="middle" font-size="13">drm_file.object_idr</text>
  <text x="110" y="144" text-anchor="middle" font-size="12">handle 1, 2 → GEM 객체</text>
  <line x1="110" y1="66" x2="110" y2="96" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="240" y="100" width="200" height="76" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="123" text-anchor="middle" font-size="13">drm_gem_object</text>
  <text x="340" y="144" text-anchor="middle" font-size="12">size, funcs, vma_node(mmap)</text>
  <text x="340" y="164" text-anchor="middle" font-size="12">resv → dma_resv</text>
  <line x1="210" y1="128" x2="236" y2="128" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="470" y="10" width="200" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="33" text-anchor="middle" font-size="13">dma-buf fd (struct file)</text>
  <text x="570" y="54" text-anchor="middle" font-size="12">private_data = dma_buf</text>
  <rect x="470" y="100" width="200" height="76" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="570" y="123" text-anchor="middle" font-size="13">struct dma_buf</text>
  <text x="570" y="144" text-anchor="middle" font-size="12">ops, priv = GEM 객체</text>
  <text x="570" y="164" text-anchor="middle" font-size="12">resv = 같은 dma_resv</text>
  <line x1="570" y1="66" x2="570" y2="96" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="466" y1="138" x2="444" y2="138" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="455" y="92" text-anchor="middle" font-size="12">PRIME export</text>
  <rect x="240" y="220" width="200" height="76" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="243" text-anchor="middle" font-size="13">dma_resv</text>
  <text x="340" y="264" text-anchor="middle" font-size="12">ww_mutex lock</text>
  <text x="340" y="284" text-anchor="middle" font-size="12">fences: WRITE · READ</text>
  <line x1="340" y1="176" x2="340" y2="216" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="470" y="220" width="200" height="76" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="570" y="243" text-anchor="middle" font-size="13">dma_fence</text>
  <text x="570" y="264" text-anchor="middle" font-size="12">context, seqno, flags</text>
  <text x="570" y="284" text-anchor="middle" font-size="12">cb_list (대기자 콜백)</text>
  <line x1="440" y1="258" x2="466" y2="258" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="470" y="330" width="200" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="570" y="353" text-anchor="middle" font-size="13">sync_file fd</text>
  <text x="570" y="374" text-anchor="middle" font-size="12">같은 fence를 fd로 감쌈</text>
  <line x1="570" y1="326" x2="570" y2="300" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="10" y="330" width="430" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="225" y="353" text-anchor="middle" font-size="13">job (drm_sched_job 또는 드라이버 job)</text>
  <text x="225" y="374" text-anchor="middle" font-size="12">완료 IRQ에서 dma_fence_signal → cb_list 실행</text>
  <line x1="440" y1="358" x2="520" y2="300" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah5)"/>
  <text x="340" y="408" text-anchor="middle" font-size="12">poll(dma-buf)은 resv의 fence에, poll(sync_file)은 fence 하나에 콜백을 단다</text>
</svg>
```

그림 5 — 가속기 드라이버의 커널 객체 관계. GEM 객체와 그것을 export한 dma-buf는 **같은 `dma_resv`**를 공유하므로, 드라이버가 resv에 건 fence가 dma-buf를 import한 다른 드라이버와 `DMA_BUF_IOCTL_SYNC`에도 보인다. sync_file은 같은 fence를 fd로 감싼 또 하나의 출구다.

### 13.2 호출 경로 요약

| 유저 동작 | 커널 경로 (6.18) | 이 노트 |
|---|---|---|
| `open("/dev/accel/accel0")` | `accel_stub_fops` → `accel_open()` → `drm_open_helper()` → `drm_file` 생성, 드라이버 `.open` | §8 |
| `ioctl(CREATE_BO)` | `drm_ioctl()` → `drm_ioctls`/드라이버 ioctl 테이블 → `drm_gem_shmem_create()` → `drm_gem_handle_create()` | §8 ② |
| `mmap(accel_fd, offset)` | `drm_gem_mmap()` → offset으로 `drm_vma_offset_lookup` → 객체 `funcs->mmap` → shmem fault 핸들러 | §8 |
| `PRIME_HANDLE_TO_FD` | `drm_gem_prime_handle_to_fd()` → `drm_gem_prime_export()` → `dma_buf_export()` (resv 공유) | §6 |
| `PRIME_FD_TO_HANDLE` | `dma_buf_get()` → self-import면 원래 객체, 아니면 `dma_buf_attach()` + `dma_buf_map_attachment()` → `gem_prime_import_sg_table` | §6 |
| `DMA_HEAP_IOCTL_ALLOC` | `dma_heap_ioctl()` → heap `.allocate` (system: 페이지 order 8/4/0, cma: `cma_alloc`) → `dma_buf_export()` + `dma_buf_fd()` | §3 |
| `sendmsg(SCM_RIGHTS)` | `scm_send` → 받는 쪽 `receive_fd()` → 같은 `struct file`에 새 fd 번호 | §3 |
| `DMA_BUF_IOCTL_SYNC` | `dma_buf_ioctl()` → `dma_buf_begin_cpu_access()` → `dma_resv_wait_timeout()` + exporter `.begin_cpu_access` → `dma_sync_sgtable_for_cpu` | §4, §6.2, §8.3 |
| `poll(dma-buf)` | `dma_buf_poll()` → resv의 fence에 콜백 등록 | §6.2 |
| `poll(sync_file)` | `sync_file_poll()` → `dma_fence_add_callback()` | §5 |
| `ioctl(SUBMIT)` | 드라이버: BO lookup → `drm_gem_lock_reservations` → 의존성 → `drm_sched_entity_push_job` 또는 링 + doorbell → fence를 resv와 sync_file로 | §8 ③, §9 |
| 완료 IRQ | 드라이버 IRQ → `dma_fence_signal()` → 콜백(대기자 깨우기, drm_sched 다음 job, 디스플레이 flip) → `pm_runtime_put_autosuspend()` | §9.2 |

### 13.3 드라이버를 쓸 때 다시 만나는 곳

- 장치 등록·probe는 G3(platform driver), H11(PCIe), D8(DT 읽기). NPU 노드의 clock/reset/power-domain은 H3, 전원 도메인과 runtime PM은 N6.
- 펌웨어 로딩은 G10(`request_firmware`, `firmware_request_nowarn`, 검색 경로), 장치 안 MCU를 Linux가 직접 부팅하는 구조면 O2(remoteproc).
- 링 버퍼·doorbell·완료 IRQ·threaded IRQ는 F6, F7, H6. 메모리 순서(`READ_ONCE` tail, `wmb()` 후 doorbell)는 F5 — ivpu의 `wmb(); /* Flush WC buffer for jobq header */` 같은 줄이 그 예다.
- DMA 주소·IOMMU·캐시는 H9, CMA는 F10.
- 디버깅: `bufinfo`(§3, §6), `/sys/kernel/debug/sync/info`(§5), 드라이버 debugfs(`/sys/kernel/debug/accel/<n>/`), ftrace의 `dma_fence` 이벤트와 `gpu_scheduler` 이벤트(M5).

---

## 14. Embedded 실무에서 다시 보기

### 14.1 센서 키트의 추론 데몬 설계

카메라 2대 + NPU 하나인 로봇에서 프로세스를 이렇게 나누는 것이 흔하다.

- `camd`(카메라 데몬): V4L2/libcamera로 캡처, 각 버퍼를 dma-buf fd로 export, 프레임마다 `{인덱스, 타임스탬프, 시퀀스}` 메시지를 보낸다. fd 자체는 시작할 때 한 번만 `SCM_RIGHTS`로 넘기고(링 4~8장), 이후에는 인덱스만 보낸다(§12.2의 mmap 비용 제거).
- `inferd`(추론 데몬): fd를 NPU에 한 번 import해 BO handle 표를 만든다. 프레임 메시지가 오면 해당 BO로 제출, out-fence가 signal되면 결과를 publish(ROS 2 토픽, Q6)하고 "버퍼 i 반납" 메시지를 `camd`에 보낸다.
- 반납 전에는 `camd`가 그 버퍼를 `QBUF`하지 않는다. 반납이 늦으면 camd는 **가장 오래된 프레임을 드롭**하고 카운터를 올린다(Q7의 드롭 측정).

```c
/* inferd의 메인 루프 모양 (설계 스케치 — 실행 안 함, 이 노트의 장난감 uAPI 사용) */
for (;;) {
	struct frame_msg m;
	recv(cam_sock, &m, sizeof(m), 0);                  /* {idx, ts_ns, seq} */
	struct drm_o4_submit s = { .in_handle = bo[m.idx], .out_handle = out_bo, .count = n };
	ioctl(accel_fd, DRM_IOCTL_O4_SUBMIT, &s);
	struct pollfd p = { .fd = s.out_fence_fd, .events = POLLIN };
	if (poll(&p, 1, 100) == 0)                         /* 100 ms 데드라인 */
		count_npu_timeout();                           /* 리셋 정책은 드라이버 TDR에 맡김 */
	close(s.out_fence_fd);
	publish_result(out_map, m.ts_ns);                  /* 타임스탬프는 캡처 시각 그대로 */
	send(cam_sock, &m.idx, sizeof(m.idx), 0);          /* 버퍼 반납 */
}
```

### 14.2 "NPU 추론이 가끔 수백 ms 튄다" 디버깅 순서

1. 유저 쪽: 추론 시간 분포(p50/p99)를 out-fence 기준으로 잰다(제출 → signal). §8.3처럼 관찰 코드 자체의 비용을 빼고 본다.
2. implicit 대기를 의심: 어디선가 `DMA_BUF_IOCTL_SYNC`나 importer 드라이버가 앞 단계의 fence를 기다리고 있지 않은가(§6.2). `bufinfo`의 resv 출력(`dma_resv_describe`)에 unsignaled fence가 남아 있는지 본다.
3. 전원: runtime PM autosuspend 지연이 프레임 간격보다 짧으면 매 프레임 NPU가 꺼졌다 켜진다(rocket은 50 ms, 10 fps 카메라 = 100 ms 간격이면 매번 resume). `/sys/devices/.../power/runtime_status`, `runtime_suspended_time`으로 확인(N6).
4. 클럭/열: NPU devfreq·thermal throttling(I4, `&npu` cooling device — i.MX8MP DT의 `#cooling-cells`). 
5. 메모리: CMA 할당 지연(F10 §7.3)이 런타임 중에 생기면(버퍼를 매 프레임 새로 할당) 수십 ms가 튄다. 버퍼는 시작할 때 한 번만 할당한다.
6. 펌웨어 hang/recovery 로그(ivpu의 "Job timeout detected, heartbeat not progressed" 같은 메시지).

### 14.3 bring-up 체크리스트 — 새 보드의 NPU

- [ ] DT: `status = "okay"`, 전원 레일(`npu-supply`), 클럭 레이트, 리셋, power-domain, `iommus`, `memory-region`(carveout이 필요한 NPU) 확인 (P4, D4)
- [ ] 펌웨어 파일이 rootfs의 `/lib/firmware/<vendor>/...`에 있고 initramfs 필요 여부 확인 (G10, K1)
- [ ] `dmesg`에 probe 성공, `/dev/accel/accel0` 또는 벤더 노드, 권한(udev) (L2)
- [ ] 작은 테스트 job(벤더 샘플, 또는 identity 모델)로 fence가 signal되는지 → 타임아웃 테스트(잘못된 명령 버퍼)로 리셋 경로 확인
- [ ] 카메라 → NPU 버퍼 import 테스트, `bufinfo`에 두 장치가 attach되는지
- [ ] runtime PM: idle 후 suspend, 다음 job에서 resume 지연 측정

---

## 15. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| GEM handle 정수를 다른 프로세스에 보냄 | 상대 쪽 ioctl이 `ENOENT` 또는 엉뚱한 버퍼 | handle은 `drm_file`마다 따로 매겨짐 | PRIME export한 dma-buf fd를 `SCM_RIGHTS`로 |
| CPU가 mmap으로 쓴 뒤 `DMA_BUF_IOCTL_SYNC(END)` 생략 | 실제 보드에서만 NPU 입력이 가끔 깨짐(QEMU에선 재현 안 됨) | 비코히어런트 SoC에서 캐시 clean 누락 | START/END로 감싸기, 또는 coherent 경로 확인 |
| 에러 경로에서 fence를 signal하지 않음 | 프로세스가 `poll`/`DMA_BUF_IOCTL_SYNC`에서 영원히 멈춤, `hung_task` 경고 | unsignaled fence가 resv에 남음 | 모든 경로에서 `dma_fence_set_error` + `dma_fence_signal` |
| signal 뒤 fence의 드라이버 데이터 접근 | 드물게 use-after-free, rmmod 후 oops | signal 후 드라이버 데이터 해제 허용 계약 | RCU grace period 지키기, `dma_fence_driver_name` 계열 API 사용 |
| IOMMU 없는 장치에 system heap 버퍼 | import/attach 실패 또는 화면 깨짐 | 장치가 연속 메모리 요구 | 가장 까다로운 장치 기준으로 CMA heap |
| 받은 fd에 CLOEXEC 없음 | 버퍼가 안 풀림, `bufinfo` count가 기대보다 큼 | `system()`/`exec`한 자식이 fd 상속 | `MSG_CMSG_CLOEXEC`, `O_CLOEXEC` |
| 매 프레임 heap 할당/해제 | 주기적 지연 튐, CMA 단편화 | `cma_alloc` migration 비용 | 시작 시 링 할당 후 재사용 |
| 카메라 버퍼 반납 프로토콜 없음 | NPU가 읽는 중 다음 프레임이 덮어씀(찢어진 입력) | QBUF가 추론 완료보다 빠름 | fence signal 뒤 반납, 늦으면 드롭 |
| udmabuf에 seal 누락 | `UDMABUF_CREATE`가 `EINVAL` | `F_SEAL_SHRINK` 필수 | `memfd_create(MFD_ALLOW_SEALING)` + `F_ADD_SEALS` |
| autosuspend 지연 < 프레임 간격 | 추론마다 수 ms~수십 ms 추가 | 매 job runtime resume | 지연을 프레임 간격 이상으로, 또는 스트리밍 중 PM get 유지 |

---

## 16. 면접에서 이렇게 말한다

**Q.** Why does Linux put NPU drivers under DRM (drivers/accel) instead of a new subsystem?

**A.** NPU가 필요로 하는 것 — 버퍼 객체, 장치 주소 공간, 작업 제출과 스케줄링, 완료 fence, 다른 장치와 버퍼 공유 — 이 GPU와 같다. 그래서 GEM, PRIME/dma-buf, drm_sched, drm_gpuvm, fdinfo 통계를 재사용하고, 그래픽 스택이 NPU를 GPU로 착각하지 않도록 노드만 `/dev/accel/accelN`(major 261)로 분리했다. 드라이버는 `DRIVER_COMPUTE_ACCEL`을 켜고 accel fops를 쓰면 된다.

> Because an NPU needs exactly what a GPU driver already has: buffer objects, a device address space, job submission and scheduling, completion fences, and buffer sharing with other devices. The accel subsystem reuses GEM, PRIME and dma-buf, the DRM GPU scheduler, gpuvm and fdinfo accounting, and only differs in the device node — /dev/accel/accelN with major 261 — so the graphics stack never mistakes it for a GPU. A driver sets DRIVER_COMPUTE_ACCEL and uses the accel file operations.

**꼬리 질문**: "그럼 벤더 misc 드라이버보다 뭐가 좋은가?" → 공용 dma-buf/fence 규칙 덕분에 카메라·디스플레이와의 zero-copy와 implicit 대기가 공짜로 따라오고(§8.3), upstream 리뷰·도구(fdinfo, debugfs, tracepoint)를 그대로 쓴다.

**Q.** Walk me through how a camera frame reaches the NPU without a copy.

**A.** 캡처 버퍼를 dma-buf로 만든다(V4L2 EXPBUF 또는 heap 할당 후 V4L2에 import). fd를 추론 프로세스에 SCM_RIGHTS로 한 번 넘기고, NPU 드라이버에 PRIME import해서 BO로 만든다. 프레임마다 인덱스만 보내고, NPU가 import 시점에 자기 IOMMU 주소로 매핑한 그 메모리를 직접 읽는다. 끝나면 out-fence가 signal되고, 그때 버퍼를 캡처 큐에 반납한다. 핵심 제약은 모든 장치가 받을 수 있는 메모리(연속성·정렬·포맷)를 고르는 것과 반납 프로토콜이다.

> The capture buffers are dma-bufs — either exported from V4L2 with EXPBUF or allocated from a DMA heap and imported into V4L2. The fds are passed once to the inference process over a Unix socket and imported into the NPU driver as buffer objects, which maps them into the NPU's IOMMU space. Per frame we only send an index; the NPU reads the same physical pages. When the out-fence signals we return the buffer to the capture queue. The real constraints are picking memory every device can use — contiguity, alignment, format — and a clear buffer return protocol.

**Q.** What's the difference between implicit and explicit synchronization?

**A.** implicit은 fence가 버퍼의 dma_resv에 붙어서 커널이 알아서 기다리는 방식이고, explicit은 fence를 sync_file이나 syncobj로 유저가 들고 다니며 다음 단계에 in-fence로 넘기는 방식이다. implicit은 쓰기 쉽지만 어디서 기다리는지 불투명해서 지연 튐의 원인이 되고, explicit은 의존성이 분명하다. 둘은 DMA_BUF_IOCTL_EXPORT/IMPORT_SYNC_FILE로 오갈 수 있다.

> With implicit sync, fences live in the buffer's reservation object and the kernel waits on them automatically — in importers, in poll on the dma-buf, in DMA_BUF_IOCTL_SYNC. With explicit sync, userspace carries the fence as a sync_file or syncobj and hands it to the next stage as an in-fence. Implicit is easy but opaque about where you block; explicit makes dependencies visible. The dma-buf export and import sync_file ioctls bridge the two.

**Q.** What does DMA_BUF_IOCTL_SYNC actually do?

**A.** 두 가지다. START는 먼저 버퍼의 implicit fence를 기다리고(장치가 아직 쓰는 중이면 블록), 그다음 exporter의 begin_cpu_access로 attach·map된 장치마다 for_cpu 캐시 동기화를 한다. END는 for_device 동기화다. 즉 유저 공간판 dma_sync_for_cpu/for_device + 대기다. 코히어런트 시스템에서는 대기만 남는다.

> START first waits for the implicit fences on the buffer — it blocks if a device is still writing — and then calls the exporter's begin_cpu_access, which does a for-CPU cache sync for every attached and mapped device. END does the for-device sync. So it's the userspace version of dma_sync_for_cpu and for_device plus a fence wait. On a coherent system only the wait remains.

**Q.** How would you structure job submission and completion in an NPU kernel driver?

**A.** submit ioctl에서 BO를 lookup하고 모든 BO의 resv를 ww_mutex로 잠근 뒤, 입력 BO의 쓰기 fence와 출력 BO의 기존 fence를 의존성으로 넣고, job의 finished fence를 출력 BO에 WRITE로 건다. 스케줄러(drm_sched)가 의존성이 풀리면 run_job에서 runtime PM get, IOMMU 도메인 전환, 링에 쓰고 doorbell. 완료 IRQ에서 fence signal, PM put autosuspend. 타임아웃 콜백에서 리셋하고 스케줄러를 재시작한다. 펌웨어 스케줄 장치라면 링 + doorbell + 완료 메시지, 그리고 heartbeat로 hang을 판정한다.

> The submit ioctl looks up the buffer objects, locks all their reservation objects with a ww_mutex acquire context, adds the input buffers' write fences as dependencies, and attaches the job's finished fence to the outputs as a write fence. The DRM scheduler calls run_job once dependencies are met; there we take a runtime PM reference, switch the IOMMU domain to the submitting process, write the ring and ring the doorbell. The completion interrupt signals the fence and drops PM with autosuspend. The timeout handler resets the core and restarts the scheduler. With firmware-scheduled hardware it's a shared ring plus doorbell and completion messages, with a firmware heartbeat to tell a hang from a long job.

**꼬리 질문**: "fence를 signal하지 않고 job을 버리면?" → 대기자가 영원히 블록한다. 에러 경로에서도 `dma_fence_set_error` 후 signal.

**Q.** System heap or CMA heap for camera buffers?

**A.** 파이프라인에서 가장 제약이 큰 장치가 정한다. 모든 importer가 IOMMU/SMMU 뒤에 있으면 system heap(흩어진 페이지, 단편화에 강함), 하나라도 연속 메모리를 요구하면(IOMMU 없는 디스플레이·ISP) CMA heap. CMA라면 크기를 부팅 때 잡고, 버퍼는 시작할 때 한 번 할당해 재사용한다 — 런타임 cma_alloc은 migration 때문에 지연이 크다.

> The most constrained device in the pipeline decides. If every importer sits behind an IOMMU, the system heap is better — scattered pages, robust against fragmentation. If any device needs physically contiguous memory, like a display controller or ISP without an IOMMU, use a CMA heap, size the CMA area at boot, and allocate the ring once at startup, because runtime CMA allocation can take tens of milliseconds due to page migration.

**Q.** Tell me about NPU drivers you've looked at.

**A.** rocket(RK3588)은 펌웨어 없이 커널이 레지스터를 프로그래밍하고 drm_sched로 job을 돌리며, 파일마다 IOMMU 도메인을 만들어 job 실행 전에 attach한다. Intel ivpu는 공유 메모리 job queue에 엔트리를 쓰고 doorbell을 울리면 NPU 펌웨어가 스케줄하는 구조라 NVMe SQ/doorbell과 거의 같다 — TDR에서 펌웨어 heartbeat로 hang을 판정하고 runtime suspend 전에 펌웨어와 D0i3를 협상한다. Qualcomm qaic는 MHI 위 제어 채널과 DMA bridge 큐를 쓰고, IRQ storm을 IRQ 끄고 폴링하는 방식으로 막는다.

> I've read the in-tree ones. Rocket, for RK3588, has no firmware: the kernel programs the NPU registers, uses the DRM scheduler, and keeps a per-file IOMMU domain that it attaches before each job. Intel's ivpu is firmware-scheduled — the driver writes an entry into a shared job queue and rings a doorbell, very much like an NVMe submission queue, which maps directly to my SSD firmware background; it detects hangs by checking a firmware heartbeat on timeout and negotiates a D0i3 state with firmware before runtime suspend. Qualcomm's qaic uses MHI for control and DMA bridge channels for data, and mitigates MSI storms by disabling the IRQ and polling until the FIFO goes quiet.

---

## 17. 직접 해보기

1. 개념: GEM handle, dma-buf fd, sync_file fd를 각각 "무엇의 이름이며 어디서 유효한가"로 한 줄씩 설명하라. — 정답/힌트: handle은 `drm_file` 안 idr 인덱스(그 fd 안에서만), dma-buf fd와 sync_file fd는 프로세스 fd 테이블의 `struct file`(fd 전달로 어디든).
2. 코드: 예제 2를 고쳐 자식이 `recvmsg`에 `MSG_CMSG_CLOEXEC`를 주게 하고 `bufinfo`의 count가 얼마나 줄어드는지 확인하라. — 정답/힌트: `system()`의 셸과 `cat`이 상속하던 참조 2개가 빠진다.
3. 코드: 예제 3에 `SYNC_IOC_MERGE`를 같은 타임라인의 fence 두 개(seq 1, seq 3)로 해 보고 `num_fences`가 몇인지 보라. — 정답/힌트: 같은 context면 나중 seqno 하나로 합쳐질 수 있다(`dma_fence_unwrap_merge`가 context별로 최신만 남김) — 직접 확인할 것.
4. 코드: 장난감 드라이버에 "입력 BO의 implicit fence를 기다린 뒤 실행"을 추가하라. 두 job을 체인(job1 출력 = job2 입력)으로 제출해 결과가 `2(2x+1)+1`인지 확인. — 정답/힌트: submit에서 `dma_resv_wait_timeout(in->resv, DMA_RESV_USAGE_WRITE, ...)`을 work 안에서 호출하거나(간단), fence 콜백으로 work를 큐잉(비동기).
5. 관찰: 예제 8 실행 중 `cat /sys/kernel/debug/dma_buf/bufinfo`를 추가해 출력 BO의 resv 줄(`dma_resv_describe`)에 unsignaled fence가 보이는지 확인하라. — 정답/힌트: job 진행 중에 찍어야 한다(`latency_ms`를 키울 것).
6. 소스 읽기: 랩 트리의 `drivers/accel/rocket/rocket_job.c`에서 `rocket_job_timedout`부터 따라가 리셋 뒤 다음 job이 어떻게 재개되는지 순서도를 그려라. — 정답/힌트: `drm_sched_stop` → 코어 리셋 → `drm_sched_start`, in-flight job의 PM 참조는 `pm_runtime_put_noidle`로 정리.
7. 설계: 카메라 30 fps, 추론 평균 25 ms·p99 60 ms일 때 캡처 링 버퍼는 몇 장이 필요한가? 드롭 정책은? — 정답/힌트: 캡처 중 1 + 추론 중 최대 2(60 ms = 약 2프레임) + 여유 1 = 4장 이상, 반납이 늦으면 최신 프레임 우선으로 오래된 프레임 드롭.

---

## 18. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| DRM | Direct Rendering Manager | GPU·디스플레이·가속기 공용 커널 서브시스템 |
| accel | compute accelerator 서브시스템 | `drivers/accel`, `/dev/accel/accelN`, major 261 |
| `DRIVER_COMPUTE_ACCEL` | drm_driver feature 플래그 | 이 장치를 accel 노드로 노출 (RENDER/MODESET과 배타) |
| GEM | Graphics Execution Manager | DRM 버퍼 객체 `drm_gem_object`와 handle 체계 |
| BO | buffer object | GEM 객체를 부르는 일반 이름 |
| PRIME | DRM의 dma-buf 공유 | handle ↔ dma-buf fd 변환 ioctl |
| dma-buf | 공유 버퍼 객체 | 버퍼를 `struct file`로 감싼 것, exporter/importer 계약 |
| exporter / importer | 버퍼 제공자 / 사용자 | `dma_buf_ops` 구현 쪽 / attach해서 매핑하는 쪽 |
| DMA heap | `/dev/dma_heap/*` | 유저가 dma-buf를 할당하는 곳 (system, CMA) |
| udmabuf | `/dev/udmabuf` | memfd 페이지를 dma-buf로 감쌈 |
| `dma_resv` | reservation object | 버퍼별 ww_mutex + fence 목록 (implicit sync의 저장소) |
| dma_fence | 완료 신호 객체 | context + seqno, 한 번 signal되면 끝 |
| sync_file | fence의 fd | poll 가능, merge 가능, fd 전달 가능 |
| syncobj | DRM fence 컨테이너 | handle 기반, timeline syncobj는 64비트 포인트 |
| sw_sync | 소프트웨어 타임라인 | 테스트용 fence 생성기 (debugfs) |
| vgem | virtual GEM | 하드웨어 없는 GEM 드라이버 (테스트 exporter) |
| drm_sched | DRM GPU scheduler | job 의존성·실행·타임아웃 관리 |
| TDR | timeout detection and recovery | job 타임아웃 → 리셋 → 재개 |
| IOVA | I/O virtual address | IOMMU 뒤에서 장치가 보는 주소 |
| UMD / KMD | user / kernel mode driver | 컴파일러·런타임 / 커널 드라이버 |
| Teflon | Mesa의 TFLite delegate | 오픈 NPU 드라이버(etnaviv, rocket)용 유저 스택 |
| doorbell | 알림 레지스터 | "링에 새 엔트리 있음"을 장치에 알리는 MMIO 쓰기 |
| MHI | Modem Host Interface | Qualcomm PCIe 채널 프로토콜 (qaic) |

---

## 19. 요약 & 체크리스트

가속기 드라이버는 유저에게 **버퍼·제출·대기·공유** 네 가지를 준다. Linux는 이것을 GPU에서 쓰던 부품으로 해결한다: 버퍼는 GEM(handle, mmap offset, IOMMU 매핑), 공유는 dma-buf(fd 하나, exporter/importer, attach), 대기는 dma_fence(implicit은 버퍼의 dma_resv, explicit은 sync_file/syncobj), 제출과 스케줄은 드라이버 ioctl + drm_sched 또는 펌웨어 링 + doorbell. NPU용 노드는 `DRIVER_COMPUTE_ACCEL`로 `/dev/accel/accelN`에 따로 생긴다. 이 노트에서 heap 할당과 fd 전달, sw_sync 타임라인, vgem PRIME과 implicit fence, udmabuf를 직접 돌렸고, 200줄짜리 accel 드라이버를 만들어 dma-buf core가 우리 fence를 자동으로 존중하는 것을 확인했다. 실제 드라이버는 rocket(커널 스케줄), ivpu(펌웨어 스케줄, NVMe식 링), qaic(PCIe, MHI), amdxdna(PSP+mailbox), habanalabs(레거시 대형)로 나뉜다. 큰 프레임에서는 fd 전달이 복사보다 확실히 유리하고, 작은 데이터는 복사가 낫다.

- [ ] DRM render 노드와 accel 노드의 차이(플래그, major, sysfs/debugfs 위치)를 설명할 수 있다
- [ ] GEM handle과 dma-buf fd의 유효 범위 차이를 설명하고, 다른 프로세스에 버퍼를 넘기는 코드를 짤 수 있다
- [ ] `DMA_HEAP_IOCTL_ALLOC`으로 버퍼를 받고 `SCM_RIGHTS`로 넘기는 코드를 짤 수 있다
- [ ] system heap과 CMA heap 중 무엇을 쓸지 장치 제약으로 판단할 수 있다
- [ ] `DMA_BUF_IOCTL_SYNC` START/END가 커널에서 하는 두 가지 일(fence 대기, 캐시 동기화)을 설명할 수 있다
- [ ] dma_fence의 context/seqno 의미와 sync_file poll/merge를 코드로 보여 줄 수 있다
- [ ] implicit과 explicit sync의 차이와 둘을 잇는 ioctl을 설명할 수 있다
- [ ] `bufinfo`를 읽고 누가 버퍼를 잡고 있는지(count, attached devices) 찾을 수 있다
- [ ] `DRIVER_COMPUTE_ACCEL` 드라이버의 뼈대(BO 생성, 제출, fence를 resv와 sync_file로 내보내기)를 화이트보드에 쓸 수 있다
- [ ] rocket과 ivpu의 제출·완료·리셋·전원 경로를 비교해 말할 수 있다
- [ ] 카메라 → NPU zero-copy 파이프라인과 버퍼 반납 프로토콜을 설계할 수 있다

## 참고 자료

- 랩 커널 소스(6.18.55, `/var/lab/src/linux-6.18.55`): `Documentation/accel/introduction.rst`, `Documentation/accel/qaic/qaic.rst`, `Documentation/accel/amdxdna/amdnpu.rst`, `Documentation/accel/rocket/index.rst`, `drivers/accel/{rocket,ivpu,qaic,amdxdna,habanalabs}/`, `drivers/dma-buf/{dma-buf.c,dma-fence.c,sw_sync.c,sync_file.c,udmabuf.c,heaps/}`, `drivers/gpu/drm/vgem/`, `include/uapi/linux/{dma-buf.h,dma-heap.h,sync_file.h,udmabuf.h}`, `include/drm/drm_accel.h`
- 커널 문서: [Compute Accelerators](https://docs.kernel.org/accel/index.html), [Buffer Sharing and Synchronization (dma-buf)](https://docs.kernel.org/driver-api/dma-buf.html), [DRM Memory Management](https://docs.kernel.org/gpu/drm-mm.html), [DRM Internals](https://docs.kernel.org/gpu/drm-internals.html), [DMA API](https://docs.kernel.org/core-api/dma-api.html)
- 소스 탐색: [elixir.bootlin.com — drivers/accel](https://elixir.bootlin.com/linux/latest/source/drivers/accel), [drivers/dma-buf](https://elixir.bootlin.com/linux/latest/source/drivers/dma-buf)
- man 페이지: [unix(7) — SCM_RIGHTS](https://man7.org/linux/man-pages/man7/unix.7.html), [memfd_create(2)](https://man7.org/linux/man-pages/man2/memfd_create.2.html), [poll(2)](https://man7.org/linux/man-pages/man2/poll.2.html)
- Mesa 문서: [Teflon](https://docs.mesa3d.org/teflon.html)
- 선행·연관 노트: H9(DMA·IOMMU·dma-buf exporter/importer), I8(DRM/KMS·GEM·PRIME·accel 개요), F10(CMA·heaps), A7(fd 전달), G5(ioctl·mmap), G6(poll), G10(펌웨어), N6(runtime PM), O2(remoteproc), O5(SoC 지형), Q2(카메라 파이프라인), Q7(드롭 측정)
