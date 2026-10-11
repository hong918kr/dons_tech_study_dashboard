# G1. 드라이버 종류 — char / block / network, 그리고 "대부분은 서브시스템 드라이버"

> **이 노트를 다 읽으면**: char·block·net 세 가지 고전 분류를 "유저가 무엇으로 접근하나 / 커널 안 어떤 구조체로 등록하나 / 데이터가 어떤 길로 오나"로 구분해 말할 수 있다 · 실제 시스템에서 `/dev` 노드 하나가 어느 드라이버·서브시스템에 붙어 있는지 sysfs로 추적할 수 있다 · char/block/net 드라이버를 각각 가장 작은 형태로 써서 QEMU 게스트에서 돌려 볼 수 있다 · "이 장치는 어느 프레임워크에 붙여야 하나"(IMU → IIO, 버튼 → input, CAN → SocketCAN …)를 근거와 함께 답할 수 있다
> **왜 필요한가**: 드라이버 면접의 첫 질문이 거의 항상 "What types of drivers are there?"이고, 이때 "char, block, net" 세 단어로 끝내면 주니어 답이 된다. 실무 첫 주에 받는 "이 센서 드라이버 어디에 붙이지?"도 같은 질문이다
> **Don 기준 난이도**: 장치 종류(스트림형 UART vs 블록형 NAND vs 패킷형 이더넷)의 차이는 펌웨어에서 이미 체감한다 / 새로 배울 부분은 Linux가 그 차이를 **유저 인터페이스와 커널 계층 구조**로 고정해 두었다는 점, 그리고 오늘날 드라이버 대부분은 그 위의 **프레임워크(서브시스템)** 에 붙는다는 점
> **선행 노트**: A3(파일 I/O와 장치 접근), E3(커널 모듈), F12(VFS 내부) — G2(디바이스 모델)·G3(platform driver)·G4(문자 디바이스)·I1(프레임워크 원칙)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 만든 SSD 펌웨어를 떠올려 보자. 호스트 쪽에서 보면 SSD는 **"LBA를 주면 512 B/4 KiB 블록을 읽고 쓰는 장치"** 다. 반면 같은 보드의 디버그 UART는 **"바이트가 하나씩 흘러나오는 장치"** 이고, Apple에서 다루는 RF 칩의 제어 버스는 **"레지스터 몇 개를 읽고 쓰는 장치"** 다. 펌웨어에서는 이 차이를 각자 알아서 처리했다. 장치마다 API가 제각각이었다.

Linux는 1990년대부터 이 차이를 **세 가지 틀**로 고정했다.

- **char(문자) 디바이스**: 바이트 스트림. `/dev/ttyAMA0`, `/dev/null`처럼 `/dev` 노드를 `open`/`read`/`write`/`ioctl` 한다. 드라이버는 `struct file_operations`를 채운다.
- **block(블록) 디바이스**: 고정 크기 블록 단위 랜덤 액세스 저장소. `/dev/vda`, `/dev/mmcblk0`. 그 위에 파일시스템을 올린다. 드라이버는 `struct blk_mq_ops`의 `.queue_rq`로 **요청(request)** 을 받는다.
- **network(네트워크) 디바이스**: 패킷. `eth0`, `can0` 같은 **인터페이스 이름**이 있고 `/dev` 노드가 없다. 유저는 소켓으로 쓴다. 드라이버는 `struct net_device_ops`의 `.ndo_start_xmit`으로 **패킷(sk_buff)** 을 받는다.

그런데 이 세 단어는 **지금의 드라이버를 설명하기엔 너무 거칠다.** 지금 커널에서 IMU 드라이버를 쓰는 사람은 char device를 직접 만들지 않는다. IIO 프레임워크에 `iio_dev`를 등록하면 IIO가 char 노드(`/dev/iio:device0`)와 sysfs 파일을 대신 만들어 준다. 버튼은 input, 온도는 hwmon, LED는 leds, 카메라는 V4L2, CAN은 netdev(SocketCAN)이다. 그리고 clock·pinctrl·regulator 공급자 드라이버는 유저에게 **아무것도 노출하지 않는다.** 그래서 면접에서 좋은 답은 이렇다.

> "고전적으로는 char, block, net 세 가지다. 하지만 실무에서 쓰는 드라이버 대부분은 서브시스템(프레임워크) 드라이버다 — 장치 종류에 맞는 프레임워크에 ops 구조체를 등록하면, 프레임워크가 표준 유저 인터페이스를 만들어 준다. 그리고 그 축과 별개로, 버스/컨트롤러 드라이버와 그 버스 위 장치(클라이언트) 드라이버를 구분한다."

```svg
<svg viewBox="0 0 680 490" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="240" y="12" width="200" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="34" text-anchor="middle" font-size="14" font-weight="bold">Linux 디바이스 드라이버</text>
  <line x1="340" y1="46" x2="74" y2="68" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="46" x2="206" y2="68" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="46" x2="338" y2="68" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="46" x2="470" y2="68" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="46" x2="602" y2="68" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="12" y="70" width="124" height="44" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="74" y="88" text-anchor="middle" font-size="13" font-weight="bold">char 디바이스</text>
  <text x="74" y="106" text-anchor="middle" font-size="12">/dev/X (c)</text>
  <rect x="144" y="70" width="124" height="44" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="206" y="88" text-anchor="middle" font-size="13" font-weight="bold">block 디바이스</text>
  <text x="206" y="106" text-anchor="middle" font-size="12">/dev/X (b)</text>
  <rect x="276" y="70" width="124" height="44" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="338" y="88" text-anchor="middle" font-size="13" font-weight="bold">net 디바이스</text>
  <text x="338" y="106" text-anchor="middle" font-size="12">eth0, can0 (소켓)</text>
  <rect x="408" y="70" width="124" height="44" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="470" y="88" text-anchor="middle" font-size="13" font-weight="bold">sysfs 속성만</text>
  <text x="470" y="106" text-anchor="middle" font-size="12">/sys/class/X</text>
  <rect x="540" y="70" width="124" height="44" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="602" y="88" text-anchor="middle" font-size="13" font-weight="bold">커널 내부 전용</text>
  <text x="602" y="106" text-anchor="middle" font-size="12">유저 노출 없음</text>
  <g font-size="12" text-anchor="middle">
    <rect x="12" y="126" width="124" height="24" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="74" y="143">tty / serial</text>
    <rect x="12" y="156" width="124" height="24" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="74" y="173">input (evdev)</text>
    <rect x="12" y="186" width="124" height="24" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="74" y="203">IIO</text>
    <rect x="12" y="216" width="124" height="24" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="74" y="233">rtc · watchdog</text>
    <rect x="12" y="246" width="124" height="24" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="74" y="263">gpiochip</text>
    <rect x="12" y="276" width="124" height="24" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="74" y="293">V4L2 (video)</text>
    <rect x="12" y="306" width="124" height="24" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="74" y="323">ALSA (snd)</text>
    <rect x="12" y="336" width="124" height="24" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="74" y="353">DRM (dri)</text>
    <rect x="12" y="366" width="124" height="24" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="74" y="383">MTD (mtdN)</text>
    <rect x="12" y="396" width="124" height="24" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="74" y="413">misc, 직접 cdev</text>
    <rect x="144" y="126" width="124" height="24" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="206" y="143">virtio-blk</text>
    <rect x="144" y="156" width="124" height="24" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="206" y="173">SCSI / SATA (sd)</text>
    <rect x="144" y="186" width="124" height="24" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="206" y="203">NVMe</text>
    <rect x="144" y="216" width="124" height="24" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="206" y="233">MMC / SD (eMMC)</text>
    <rect x="144" y="246" width="124" height="24" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="206" y="263">mtdblock</text>
    <rect x="144" y="276" width="124" height="24" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="206" y="293">loop · zram</text>
    <rect x="276" y="126" width="124" height="24" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="338" y="143">Ethernet NIC</text>
    <rect x="276" y="156" width="124" height="24" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="338" y="173">Wi-Fi</text>
    <rect x="276" y="186" width="124" height="24" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="338" y="203">SocketCAN</text>
    <rect x="276" y="216" width="124" height="24" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="338" y="233">virtio-net</text>
    <rect x="276" y="246" width="124" height="24" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="338" y="263">tun · dummy</text>
    <rect x="408" y="126" width="124" height="24" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="470" y="143">hwmon</text>
    <rect x="408" y="156" width="124" height="24" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="470" y="173">leds</text>
    <rect x="408" y="186" width="124" height="24" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="470" y="203">thermal</text>
    <rect x="408" y="216" width="124" height="24" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="470" y="233">power_supply</text>
    <rect x="408" y="246" width="124" height="24" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="470" y="263">backlight</text>
    <rect x="540" y="126" width="124" height="24" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="602" y="143">clk 공급자</text>
    <rect x="540" y="156" width="124" height="24" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="602" y="173">pinctrl</text>
    <rect x="540" y="186" width="124" height="24" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="602" y="203">regulator 공급자</text>
    <rect x="540" y="216" width="124" height="24" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="602" y="233">irqchip · reset</text>
    <rect x="540" y="246" width="124" height="24" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="602" y="263">dmaengine · PHY</text>
  </g>
  <rect x="144" y="320" width="520" height="100" rx="6" fill="none" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="404" y="344" text-anchor="middle" font-size="13" font-weight="bold">직교하는 두 번째 축 — 버스 위치</text>
  <text x="404" y="366" text-anchor="middle" font-size="12">버스/컨트롤러 드라이버: platform·I2C·SPI·PCI·USB 컨트롤러를 구동</text>
  <text x="404" y="386" text-anchor="middle" font-size="12">클라이언트(장치) 드라이버: 그 버스 위 칩 하나를 구동 (IMU, PMIC …)</text>
  <text x="404" y="406" text-anchor="middle" font-size="12">둘 다 위 다섯 칸 중 하나(또는 여러 개)로 유저에게 보인다</text>
  <text x="340" y="450" text-anchor="middle" font-size="13">위쪽 굵은 상자 = 유저가 보는 인터페이스 종류, 아래 상자 = 그 인터페이스를 만들어 주는 프레임워크</text>
  <text x="340" y="472" text-anchor="middle" font-size="12">(tty·input·IIO 등은 프레임워크가 cdev를 대신 등록한다 — 드라이버 작성자는 file_operations를 거의 안 쓴다)</text>
</svg>
```
그림 1 — 드라이버 분류를 "유저에게 무엇으로 보이나"로 다시 그린 지도. 고전 3분류(파랑·초록·주황)에 "sysfs 속성만"과 "커널 내부 전용"이 추가되고, 실제 드라이버는 그 아래 프레임워크 중 하나에 붙는다. 버스 위치(컨트롤러 vs 클라이언트)는 별개의 축이다.

펌웨어 비유로 말하면 이렇다. 고전 3분류는 "HAL의 세 가지 기본 인터페이스(stream / block / packet)"이고, 프레임워크는 "장치 클래스별 표준 HAL(센서 HAL, 오디오 HAL …)"이다. Linux의 차이는 **이 HAL을 커널이 강제한다**는 점이다. IMU를 IIO에 붙이면 세상의 모든 IIO 툴(`iio_readdev`, libiio, ROS 드라이버)이 수정 없이 그 IMU를 읽는다.

이 노트에서 실제로 해 보는 것:

| 절 | 내용 | 실습 |
|---|---|---|
| §0.1 | 실습 준비 — 모듈 3개 빌드, 부팅 스크립트 | 예제 1 |
| §1 | 고전 3분류의 정의와 비교표 | — |
| §2 | 랩 게스트 조사 — `/sys/class`, `/sys/bus`, `/proc/devices`, `/dev` → 드라이버 추적 | 예제 2–4 |
| §3 | 가장 작은 char 드라이버 | 예제 5 |
| §4 | RAM 기반 blk-mq block 드라이버 + vfat 마운트 | 예제 6 |
| §5 | 패킷을 세고 버리는 dummy `net_device` + ping | 예제 7 |
| §6 | 세 데이터 경로를 `dump_stack()`으로 실측 비교 | 예제 8 |
| §7 | "대부분은 서브시스템 드라이버" — 프레임워크가 만든 인터페이스 보기, 실제 IMU 드라이버 구조 | 예제 9–10 |
| §8 | 버스/컨트롤러 vs 클라이언트, 커널 내부 전용 드라이버 | — |
| §9 | 커널 소스 트리 측정 — 드라이버는 어디에 몰려 있나 | 예제 11 |
| §10 | "내 장치는 어느 프레임워크?" 결정표 | — |

> 실행 환경: 모든 출력은 Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`) 안의 공용 랩에서 QEMU `virt`(cortex-a57, 2 CPU, 512 MB, TCG)로 부팅한 **게스트 커널 `6.18.55`**(base `out/Image`, BusyBox 1.37 initramfs)에서 실제로 돌린 결과다. 소스 인용은 `/var/lab/src/linux-6.18.55`에서 확인했다. 타임스탬프·MAC 주소·모듈 주소는 실행마다 다르다.

### 0.1 실습 준비 — 모듈 3개와 부팅 스크립트

#### 예제 1 — G1 모듈 빌드와 modinfo

무엇을 확인하나: 이 노트의 모듈 세 개(`g1_chr`, `g1_blk`, `g1_net`)가 랩 커널 빌드 트리로 경고 없이 빌드되고 vermagic이 게스트 커널(`6.18.55`)과 맞는지.

```make
obj-m += g1_chr.o g1_blk.o g1_net.o
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/G1/mod && cp /Users/donh/.elinux_scratch/G1/{g1_chr.c,g1_blk.c,g1_net.c,Kbuild} /var/tmp/G1/mod/ \
  && cd /var/tmp/G1/mod && make -j2 -C /var/lab/out/linux-build M=$PWD modules \
  && for m in g1_chr g1_blk g1_net; do modinfo $m.ko | grep -E "^(name|vermagic|parm)"; done'
```

```text
  CC [M]  g1_chr.o
  CC [M]  g1_blk.o
  CC [M]  g1_net.o
  MODPOST Module.symvers
  CC [M]  g1_chr.mod.o
  CC [M]  .module-common.o
  CC [M]  g1_blk.mod.o
  CC [M]  g1_net.mod.o
  LD [M]  g1_chr.ko
  LD [M]  g1_blk.ko
  LD [M]  g1_net.ko
name:           g1_chr
vermagic:       6.18.55 SMP preempt mod_unload aarch64
parm:           stack:bool
name:           g1_blk
vermagic:       6.18.55 SMP preempt mod_unload aarch64
parm:           stack:bool
name:           g1_net
vermagic:       6.18.55 SMP preempt mod_unload aarch64
parm:           stack:bool
```

출력에서 볼 것: 경고 0개. 세 모듈 모두 `stack` 파라미터를 가진다 — `stack=1`로 넣으면 첫 요청에서 `dump_stack()`을 찍어 **유저 syscall부터 드라이버 콜백까지의 실제 호출 경로**를 보여 준다(§6). (빌드 로그는 처음 빌드한 뒤 `stack` 파라미터를 추가해 다시 빌드한 것을 합쳐 보였다. 이후의 모든 게스트 출력은 이 최종 `.ko`로 돌렸다.)

게스트 부팅 스크립트(`boot.sh`, VM에서 실행). 기본 랩 명령에 **virtio-blk 디스크**(`/dev/vda`가 되도록 `/var/tmp/G1/g1_vda.img` 16 MB)와 **virtio-net NIC**(`eth0`)를 붙였다. block과 net의 "진짜" 드라이버도 같이 보기 위해서다.

```sh
#!/bin/bash
# usage: boot.sh <guest-script.sh> -> boots lab base kernel (+virtio blk/net) with /g1/* and runs the script
set -e
G=$1; N=$(basename $G .sh); W=/var/tmp/G1/run_$N
rm -rf $W && mkdir -p $W/r
cp -a /var/lab/build/rootfs/. $W/r/
mkdir -p $W/r/g1
cp /var/tmp/G1/mod/*.ko /var/tmp/G1/bin/* $W/r/g1/
cp /Users/donh/.elinux_scratch/G1/$G $W/r/g1/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
[ -f /var/tmp/G1/g1_vda.img ] || truncate -s 16M /var/tmp/G1/g1_vda.img
timeout ${TMO:-180} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 512M -nographic -no-reboot \
  -kernel /var/lab/out/Image -initrd $W/i.gz \
  -drive file=/var/tmp/G1/g1_vda.img,if=none,id=g1d,format=raw -device virtio-blk-device,drive=g1d \
  -netdev user,id=g1n -device virtio-net-device,netdev=g1n \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/g1/t.sh" > $W/out.txt 2>&1 || echo "qemu rc=$?"
tr -d '\r' < $W/out.txt > /var/tmp/G1/out_$N.txt
```

랩 initramfs의 `/init`은 proc, sysfs, devtmpfs(`/dev`), debugfs를 mount 하고 `LAB_CMD`를 실행한 뒤 전원을 끈다. 각 예제의 "게스트 스크립트"는 이 스크립트로 돌린 것이다.

---

## 1. 고전 3분류 — char, block, net

### 1.1 직관 — "무엇을 단위로 주고받나"

- **char**: 수도꼭지. 물(바이트)이 흐르고, 되감기(seek)는 보통 의미가 없다. UART, 센서 FIFO, `/dev/random`.
- **block**: 번호 매긴 사물함. "37번 칸 내용 줘", "37번 칸에 이걸 넣어" — 칸 크기는 고정(512 B 이상)이고 아무 칸이나 순서 없이 접근한다. 디스크, eMMC, SSD. Don의 NVMe SSD가 정확히 이것이다.
- **net**: 우체국. 주소가 붙은 소포(패킷)가 들어오고 나간다. 소포는 **장치가 스스로 받는다**(유저가 읽기 전에 도착한다) — 그래서 "파일을 열어 읽는" 모델이 맞지 않는다.

### 1.2 정의 — 한 장 비교표

| 항목 | char | block | net |
|---|---|---|---|
| 데이터 단위 | 바이트 스트림 (크기 자유) | 고정 크기 섹터/블록, 랜덤 액세스 | 패킷 (프레임) |
| 유저가 보는 것 | `/dev/X` (`c`), `ls -l` 첫 글자 `c` | `/dev/X` (`b`), 보통 그 위에 mount | 인터페이스 이름 `eth0` (노드 없음) |
| 유저 API | `open`/`read`/`write`/`ioctl`/`poll`/`mmap` | 파일시스템을 통한 파일 I/O, 또는 `/dev/X` 직접 I/O | `socket`/`sendto`/`recvfrom`, netlink(`ip`) |
| 장치 번호 | major:minor (`/proc/devices` Character) | major:minor (`/proc/devices` Block) | 없음 — `ifindex` |
| 중간 계층 | 거의 없음 (VFS → 드라이버 직통) | page cache, bio, I/O 스케줄러, blk-mq | 소켓, TCP/IP 스택, neighbour, qdisc |
| 드라이버가 채우는 ops | `struct file_operations` | `struct blk_mq_ops` + `struct block_device_operations` | `struct net_device_ops` |
| 드라이버가 받는 단위 | 유저 버퍼 포인터 + 길이 | `struct request` (안에 bio·bio_vec) | `struct sk_buff` |
| 등록 API | `alloc_chrdev_region` + `cdev_add` (또는 `misc_register`) | `blk_mq_alloc_disk` + `add_disk` | `alloc_netdev` + `register_netdev` |
| 펌웨어 비유 | UART 드라이버의 rx/tx 함수 | NVMe 커맨드 큐 + LBA 매핑 | MAC 드라이버의 TX 디스크립터 링 |
| 이 노트 예제 | §3 `g1_chr` | §4 `g1_blk` | §5 `g1_net` |

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <g font-size="13" font-weight="bold" text-anchor="middle">
    <text x="86" y="24">유저가 하는 일</text><text x="252" y="24">유저 API</text><text x="418" y="24">커널 객체</text><text x="584" y="24">드라이버 콜백</text>
  </g>
  <g font-size="12">
    <text x="10" y="50">char — /dev 노드, 바이트 스트림</text>
    <rect x="10" y="58" width="152" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="86" y="79" text-anchor="middle">cat /dev/g1_chr</text>
    <rect x="176" y="58" width="152" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="252" y="79" text-anchor="middle">open / read / ioctl</text>
    <rect x="342" y="58" width="152" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="418" y="79" text-anchor="middle">struct file → cdev</text>
    <rect x="508" y="58" width="152" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="584" y="79" text-anchor="middle">fops.read</text>
    <text x="10" y="120">block — /dev 노드 + 파일시스템</text>
    <rect x="10" y="128" width="152" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="86" y="149" text-anchor="middle">mount /dev/vda</text>
    <rect x="176" y="128" width="152" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="252" y="149" text-anchor="middle">FS 경유 read/write</text>
    <rect x="342" y="128" width="152" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="418" y="149" text-anchor="middle">bio → request</text>
    <rect x="508" y="128" width="152" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="584" y="149" text-anchor="middle">mq_ops.queue_rq</text>
    <text x="10" y="190">net — 인터페이스 이름, /dev 없음</text>
    <rect x="10" y="198" width="152" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="86" y="219" text-anchor="middle">ping, socket()</text>
    <rect x="176" y="198" width="152" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="252" y="219" text-anchor="middle">sendto / recvfrom</text>
    <rect x="342" y="198" width="152" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="418" y="219" text-anchor="middle">sk_buff → Qdisc</text>
    <rect x="508" y="198" width="152" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="584" y="219" text-anchor="middle">ndo_start_xmit</text>
    <text x="10" y="260">프레임워크 예: IIO — sysfs + char 노드를 IIO가 만든다</text>
    <rect x="10" y="268" width="152" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="86" y="289" text-anchor="middle">cat in_accel_x_raw</text>
    <rect x="176" y="268" width="152" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="252" y="289" text-anchor="middle">/sys/bus/iio/...</text>
    <rect x="342" y="268" width="152" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="418" y="289" text-anchor="middle">iio_dev, channels</text>
    <rect x="508" y="268" width="152" height="32" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="584" y="289" text-anchor="middle">info.read_raw</text>
  </g>
  <g stroke="currentColor" marker-end="url(#ah2)">
    <line x1="162" y1="74" x2="174" y2="74"/><line x1="328" y1="74" x2="340" y2="74"/><line x1="494" y1="74" x2="506" y2="74"/>
    <line x1="162" y1="144" x2="174" y2="144"/><line x1="328" y1="144" x2="340" y2="144"/><line x1="494" y1="144" x2="506" y2="144"/>
    <line x1="162" y1="214" x2="174" y2="214"/><line x1="328" y1="214" x2="340" y2="214"/><line x1="494" y1="214" x2="506" y2="214"/>
    <line x1="162" y1="284" x2="174" y2="284"/><line x1="328" y1="284" x2="340" y2="284"/><line x1="494" y1="284" x2="506" y2="284"/>
  </g>
  <text x="340" y="322" text-anchor="middle" font-size="12">드라이버 작성자가 구현하는 것은 오른쪽 끝 열뿐이다 — 나머지는 커널(또는 프레임워크)이 만든다</text>
</svg>
```
그림 2 — 종류별 "유저가 보는 인터페이스"와 드라이버가 실제로 구현하는 콜백. 맨 아래 IIO 행처럼 프레임워크 드라이버는 `file_operations`조차 직접 안 쓰고 `read_raw` 같은 더 높은 수준의 콜백만 채운다.

### 1.3 흔한 오해

- **"char는 느리고 block은 빠르다"** — 아니다. 차이는 속도가 아니라 **중간 계층(캐시·스케줄러)의 유무**다. `/dev/zero`(char)는 메모리 속도로 읽힌다.
- **"모든 장치는 파일이다"** — 네트워크 인터페이스는 예외다. `/dev/eth0`은 없다(§5). "Everything is a file"은 "대부분의 장치가 파일 디스크립터로 다뤄진다"에 가깝다(소켓도 fd이긴 하다).
- **"block 장치는 반드시 디스크"** — `loop`(파일을 블록 장치로), `zram`(압축 RAM), 이 노트의 `g1_blk`(그냥 RAM)처럼 하드웨어가 없는 block 장치도 많다.
- **"NAND 플래시는 block 장치"** — raw NAND/NOR는 **MTD**다(지우기 블록·배드 블록·OOB가 있어서 섹터 덮어쓰기 모델이 안 맞는다). eMMC·SD·SSD는 컨트롤러가 FTL을 내장하므로 block이다. Don이 SSD에서 만든 FTL이 바로 "MTD를 block으로 바꾸는 계층"이다.

---

## 2. 실제 시스템 조사 — 랩 게스트에는 어떤 드라이버가 있나

이론보다 먼저 실물을 본다. 랩 게스트(QEMU virt + virtio 디스크·NIC)에서 sysfs와 procfs를 훑는다.

### 2.1 예제 2 — `/sys/class`, `/sys/bus`, `/proc/devices`, `/sys/block`, `/sys/class/net`

무엇을 확인하나: 클래스(=유저에게 보이는 장치 종류)별 장치 수, 버스별 등록 드라이버 수, char/block major 목록이 실제로 어떻게 생겼는지.

게스트 스크립트 `s1_survey.sh`:

```sh
echo "### uname"; uname -r
echo "### class count"; ls /sys/class | wc -l
echo "### class devices"
for c in /sys/class/*; do echo "$(ls $c | wc -l) $(basename $c)"; done | sort -rn
echo "### bus drivers/devices"
for b in /sys/bus/*; do echo "$(basename $b) drivers=$(ls $b/drivers | wc -l) devices=$(ls $b/devices | wc -l)"; done
echo "### proc devices"; cat /proc/devices
echo "### sys block"; ls --color=never /sys/block
echo "### sys class net"; ls --color=never /sys/class/net
echo "### net drivers"; for n in /sys/class/net/*; do echo "$(basename $n) -> $(readlink $n/device/driver 2>/dev/null)"; done
```

```text
lab initramfs: 6.18.55 up 1.48s
### uname
6.18.55
### class count
66
### class devices
104 tty
11 bdi
10 block
8 misc
8 mem
6 vc
2 wakeup
2 net
2 mtd
2 input
1 vtconsole
1 rtc
1 regulator
1 pci_bus
1 mdio_bus
1 graphics
1 firmware
1 devcoredump
0 watchdog
0 virtio-ports
0 vfio-dev
...
0 i2c-dev
0 hwmon
...
### bus drivers/devices
amba drivers=11 devices=3
auxiliary drivers=2 devices=0
clockevents drivers=0 devices=3
clocksource drivers=0 devices=1
container drivers=0 devices=0
cpu drivers=0 devices=2
edac drivers=0 devices=1
event_source drivers=0 devices=3
faux drivers=1 devices=1
fsl-mc drivers=5 devices=0
gadget drivers=0 devices=0
genpd drivers=0 devices=0
genpd_provider drivers=1 devices=0
gpio drivers=1 devices=1
hid drivers=13 devices=0
host1x-context drivers=0 devices=0
i2c drivers=30 devices=0
iio drivers=0 devices=0
mdio_bus drivers=109 devices=0
memory drivers=0 devices=4
memory_tiering drivers=0 devices=1
mipi-dsi drivers=0 devices=0
mmc drivers=1 devices=0
mmc_rpmb drivers=0 devices=0
node drivers=0 devices=1
nvmem drivers=0 devices=1
nvmem-layout drivers=1 devices=0
pci drivers=35 devices=1
pci-epf drivers=0 devices=0
pci_express drivers=3 devices=0
platform drivers=1074 devices=43
pnp drivers=2 devices=0
rpmsg drivers=3 devices=0
scmi_protocol drivers=12 devices=0
scsi drivers=2 devices=0
sdio drivers=0 devices=0
serial drivers=0 devices=0
serial-base drivers=2 devices=7
serio drivers=2 devices=0
soc drivers=0 devices=0
spi drivers=7 devices=0
spmi drivers=1 devices=0
sunxi-rsb drivers=1 devices=0
tee drivers=3 devices=0
ulpi drivers=0 devices=0
usb drivers=5 devices=0
virtio drivers=8 devices=2
workqueue drivers=0 devices=2
xen drivers=0 devices=0
xen-backend drivers=0 devices=0
### proc devices
Character devices:
  1 mem
  2 pty
  3 ttyp
  4 /dev/vc/0
  4 tty
  4 ttyS
  5 /dev/tty
  5 /dev/console
  5 /dev/ptmx
  7 vcs
 10 misc
 13 input
 29 fb
 89 i2c
 90 mtd
128 ptm
136 pts
180 usb
189 usb_device
204 ttyAMA
207 ttymxc
234 vfio
235 megaraid_sas_ioctl
236 ttyTHS
237 ttySTM
238 ttyHS
239 ttyMSM
240 ttyOWL
241 ttyMSM
242 ttyLP
243 ttyLF
244 bsg
245 watchdog
246 tee
247 iio
248 ptp
249 pps
250 rtc
251 tpm
252 pwm
253 ttyMV
254 gpiochip
511 rpmb

Block devices:
  7 loop
  8 sd
 31 mtdblock
 65 sd
 66 sd
 67 sd
 68 sd
 69 sd
 70 sd
 71 sd
128 sd
129 sd
130 sd
131 sd
132 sd
133 sd
134 sd
135 sd
179 mmc
254 virtblk
259 blkext
### sys block
loop0      loop2      loop4      loop6      mtdblock0
loop1      loop3      loop5      loop7      vda
### sys class net
eth0  lo
### net drivers
eth0 -> ../../../../bus/virtio/drivers/virtio_net
lo -> 
[   13.128116] reboot: Power down
```

(`### class devices`의 0개짜리 클래스 48개 중 일부만 남기고 `...`로 줄였다. 나머지 줄은 그대로다.)

출력에서 볼 것:

- **클래스가 66개**인데 실제 장치가 있는 것은 18개뿐이다. `hwmon`, `watchdog`, `i2c-dev`, `leds` 클래스는 **프레임워크 코드가 커널에 들어 있어서 클래스는 만들어졌지만 장치가 아직 없다**. 실제 보드라면 여기에 센서·LED가 채워진다. `/sys/class`의 디렉터리 이름 하나하나가 곧 "서브시스템(프레임워크)" 목록이다.
- `/proc/devices`의 **Character 42줄 대 Block 21줄**(그중 16줄이 SCSI `sd`의 major 묶음). 이름을 보면 `input`, `iio`, `rtc`, `watchdog`, `gpiochip`, `pwm`, `ptp`, `tee` — **char major 대부분은 개별 드라이버가 아니라 프레임워크가 등록한 것**이다. 예를 들어 `iio` 247은 IIO 코어가 하나 잡아 두고 모든 IIO 장치가 minor로 나눠 쓴다.
- block 쪽 `254 virtblk`는 우리가 붙인 virtio 디스크, `259 blkext`는 major가 모자랄 때 쓰는 확장 번호(파티션이 많은 NVMe 등)다.
- **`/sys/class/net`에는 `eth0`과 `lo`가 있지만 `/proc/devices`에는 net 항목이 없다.** 네트워크 장치는 장치 번호 체계 밖에 있다. `eth0`을 구동하는 드라이버는 `virtio_net`(virtio 버스 드라이버)이고, `lo`는 물리 장치가 없어서 `device` 링크 자체가 없다.
- `platform drivers=1074 devices=43`: 이 defconfig 커널에는 **platform 드라이버가 1074개 내장**되어 있지만 QEMU virt 보드에 실제로 있는 platform 장치는 43개다. 드라이버는 "이런 장치가 오면 맡겠다"는 등록일 뿐이고, 장치를 만나야 probe 된다(D3, G2). `i2c drivers=30 devices=0`도 같은 이야기 — I2C 클라이언트 드라이버는 30개 있지만 virt 머신에는 I2C 컨트롤러가 없어서 장치가 0이다.
- `mdio_bus drivers=109`: 이더넷 **PHY 칩 드라이버**들이다. 네트워크도 "MAC 드라이버(netdev) + PHY 드라이버(mdio 버스 클라이언트)"로 나뉜다 — §8의 컨트롤러/클라이언트 구분이 여기도 있다.

```svg
<svg viewBox="0 0 640 344" xmlns="http://www.w3.org/2000/svg">
  <text x="320" y="22" text-anchor="middle" font-size="14" font-weight="bold">게스트 커널에 등록된 드라이버 수 (버스별, /sys/bus/X/drivers)</text>
  <text x="142" y="55" text-anchor="end" font-size="12">platform</text>
  <rect x="150" y="43" width="430" height="16" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="586" y="55" font-size="12">1074</text>
  <text x="142" y="79" text-anchor="end" font-size="12">mdio_bus</text>
  <rect x="150" y="67" width="44" height="16" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="200" y="79" font-size="12">109</text>
  <text x="142" y="103" text-anchor="end" font-size="12">pci</text>
  <rect x="150" y="91" width="14" height="16" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="170" y="103" font-size="12">35</text>
  <text x="142" y="127" text-anchor="end" font-size="12">i2c</text>
  <rect x="150" y="115" width="12" height="16" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="168" y="127" font-size="12">30</text>
  <text x="142" y="151" text-anchor="end" font-size="12">hid</text>
  <rect x="150" y="139" width="5" height="16" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="161" y="151" font-size="12">13</text>
  <text x="142" y="175" text-anchor="end" font-size="12">scmi_protocol</text>
  <rect x="150" y="163" width="5" height="16" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="161" y="175" font-size="12">12</text>
  <text x="142" y="199" text-anchor="end" font-size="12">amba</text>
  <rect x="150" y="187" width="4" height="16" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="160" y="199" font-size="12">11</text>
  <text x="142" y="223" text-anchor="end" font-size="12">virtio</text>
  <rect x="150" y="211" width="3" height="16" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="159" y="223" font-size="12">8</text>
  <text x="142" y="247" text-anchor="end" font-size="12">spi</text>
  <rect x="150" y="235" width="3" height="16" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="159" y="247" font-size="12">7</text>
  <text x="142" y="271" text-anchor="end" font-size="12">fsl-mc</text>
  <rect x="150" y="259" width="2" height="16" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="158" y="271" font-size="12">5</text>
  <text x="142" y="295" text-anchor="end" font-size="12">usb</text>
  <rect x="150" y="283" width="2" height="16" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="158" y="295" font-size="12">5</text>
  <line x1="150" y1="36" x2="150" y2="304" stroke="currentColor"/>
  <text x="20" y="324" font-size="12">드라이버 수 (선형 축) — 6.18.55 defconfig 기반 랩 커널, 내장(built-in)만</text>
</svg>
```
그림 3 — 예제 2에서 측정한 버스별 드라이버 수(5개 이상인 버스만). SoC 보드에서는 **platform 버스가 압도적**이다 — SoC 내부 IP는 열거(enumeration)가 안 되는 장치라 DT로 기술되고 platform 드라이버로 붙기 때문이다(D1, G3). 이 그래프는 "어떤 종류의 드라이버(char/block/net)"가 아니라 "어느 버스에 붙나"라는 다른 축의 숫자다.

### 2.2 예제 3 — `/dev` 노드 → 드라이버 → 서브시스템을 걷는 정적 프로그램

무엇을 확인하나: 커널은 장치 번호마다 `/sys/dev/char/MAJ:MIN`, `/sys/dev/block/MAJ:MIN` 심볼릭 링크를 만든다. 이것을 따라가면 "이 노드는 어느 클래스(서브시스템)의 장치이고, 어느 드라이버가 묶여 있나"를 알 수 있다. BusyBox 게스트에서 돌도록 `-static`으로 빌드한다.

```c
/* g1_walk: for every device number in /sys/dev/{char,block}
 * print  MAJ:MIN  name  driver  subsystem  (static, runs in BusyBox guest) */
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* readlink <dir>/<rel> and return the last path component (or "-") */
static const char *link_base(const char *dir, const char *rel, char *out, size_t n)
{
	char path[PATH_MAX], tgt[PATH_MAX];
	ssize_t len;

	snprintf(path, sizeof(path), "%s/%s", dir, rel);
	len = readlink(path, tgt, sizeof(tgt) - 1);
	if (len < 0) {
		snprintf(out, n, "-");
		return out;
	}
	tgt[len] = '\0';
	snprintf(out, n, "%s", strrchr(tgt, '/') ? strrchr(tgt, '/') + 1 : tgt);
	return out;
}

static int walk(const char *type)
{
	char top[64], dir[PATH_MAX], name[PATH_MAX], drv[PATH_MAX], sub[PATH_MAX];
	struct dirent *e;
	int n = 0;
	DIR *d;

	snprintf(top, sizeof(top), "/sys/dev/%s", type);
	d = opendir(top);
	if (!d) {
		perror(top);
		exit(1);
	}
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.')
			continue;
		snprintf(dir, sizeof(dir), "%s/%s", top, e->d_name);
		link_base(top, e->d_name, name, sizeof(name));      /* class device name */
		link_base(dir, "device/driver", drv, sizeof(drv)); /* bound driver, if any */
		link_base(dir, "subsystem", sub, sizeof(sub));     /* class or bus */
		printf("%-5s %-8s %-16s %-14s %s\n", type, e->d_name, name, drv, sub);
		n++;
	}
	closedir(d);
	return n;
}

int main(void)
{
	int c = walk("char"), b = walk("block");

	printf("total: %d char, %d block\n", c, b);
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/G1/bin && gcc -std=gnu11 -Wall -Wextra -O2 -static /Users/donh/.elinux_scratch/G1/g1_walk.c -o /var/tmp/G1/bin/g1_walk'
```

(처음에는 `name`/`drv`/`sub`를 256바이트로 잡았다가 gcc 15가 `-Wformat-truncation` 경고를 냈다. `PATH_MAX`로 늘려 경고 0개로 만든 버전이 위 코드다.)

게스트 스크립트 `s2_walk.sh` — tty/vc 계열 110여 줄은 grep으로 빼고, 서브시스템별·드라이버별 개수를 센다:

```sh
echo "### walk"; /g1/g1_walk > /tmp/w.txt; cat /tmp/w.txt | grep -v -E " (tty|vc) *$|^char +4:|^char +7:"
echo "### per subsystem (all)"; awk '$1!="total:"{print $5}' /tmp/w.txt | sort | uniq -c | sort -rn
echo "### per driver (bound)"; awk '$1!="total:" && $4!="-"{print $4}' /tmp/w.txt | sort | uniq -c | sort -rn
echo "### readlink"
for n in ttyAMA0 vda rtc0 input/event0 mtd0 watchdog0; do
  [ -e /dev/$n ] || { echo "$n: no node"; continue; }
  mm=$(stat -c '%t:%T' /dev/$n); t=$(stat -c '%F' /dev/$n)
  M=$(printf '%d:%d' 0x${mm%:*} 0x${mm#*:})
  case "$t" in *char*) k=char;; *) k=block;; esac
  echo "/dev/$n $k $M -> $(readlink /sys/dev/$k/$M/device/driver)"
done
```

```text
lab initramfs: 6.18.55 up 0.97s
### walk
char  10:235   autofs           -              misc
char  1:9      urandom          -              mem
char  90:0     mtd0             -              mtd
char  10:231   snapshot         -              misc
char  1:7      full             -              mem
char  10:183   hw_random        -              misc
char  254:0    gpiochip0        -              gpio
char  1:5      zero             -              mem
char  10:256   vga_arbiter      -              misc
char  1:3      null             -              mem
char  250:0    rtc0             rtc-pl031      rtc
char  1:1      mem              -              mem
char  1:11     kmsg             -              mem
char  10:196   vfio             -              misc
char  90:1     mtd0ro           -              mtd
char  1:8      random           -              mem
char  13:64    event0           -              input
char  10:200   tun              -              misc
char  10:257   cpu_dma_latency  -              misc
char  1:4      port             -              mem
char  10:237   loop-control     -              misc
block 7:6      loop6            -              block
block 7:4      loop4            -              block
block 7:2      loop2            -              block
block 7:0      loop0            -              block
block 254:0    vda              virtio_blk     block
block 7:7      loop7            -              block
block 7:5      loop5            -              block
block 7:3      loop3            -              block
block 7:1      loop1            -              block
block 31:0     mtdblock0        -              block
total: 131 char, 10 block
### per subsystem (all)
    104 tty
     10 block
      8 misc
      8 mem
      6 vc
      2 mtd
      1 rtc
      1 input
      1 gpio
### per driver (bound)
      5 port
      1 virtio_blk
      1 rtc-pl031
### readlink
/dev/ttyAMA0 char 204:64 -> ../../../../../bus/serial-base/drivers/port
/dev/vda block 254:0 -> ../../../../bus/virtio/drivers/virtio_blk
/dev/rtc0 char 250:0 -> ../../../bus/amba/drivers/rtc-pl031
/dev/input/event0 char 13:64 -> 
/dev/mtd0 char 90:0 -> 
watchdog0: no node
[    1.471208] reboot: Power down
```

출력에서 볼 것:

- **장치 번호 141개 중 드라이버 링크가 바로 보이는 것은 7개뿐**이다. `event0`, `mtd0`, `gpiochip0`의 `device/driver`는 비어 있다. 이유: `/sys/dev/char/13:64`가 가리키는 것은 **클래스 장치**(`event0`)이고, 그 `device` 링크는 또 다른 **클래스 장치**(`input0`)를 가리킨다. 실제 하드웨어 드라이버는 부모를 몇 단계 더 올라가야 나온다(예제 4).
- `mem`(null, zero, random …)과 `misc`(tun, loop-control …)는 하드웨어가 없는 **소프트웨어 char 장치**다. `misc`는 major 10 하나를 여러 작은 드라이버가 minor로 나눠 쓰는 프레임워크다(G7).
- `/dev/ttyAMA0`의 드라이버가 `uart-pl011`이 아니라 `port`로 나온다. 6.x 커널의 serial core는 UART 하나를 `serial-base` 버스 위의 `ctrl`/`port` 장치로 한 번 더 감싼다. 진짜 UART 드라이버는 그 위 부모에 있다(예제 4).
- `watchdog0`이 없다 — QEMU virt에는 watchdog 하드웨어가 없다. 그래도 `/proc/devices`에는 `245 watchdog`이 있었다. **프레임워크는 장치가 없어도 major를 잡아 둔다.**

### 2.3 예제 4 — 클래스 장치에서 부모를 따라 올라가기

무엇을 확인하나: 클래스 장치 → 부모 → 부모 … 를 따라가며 각 단계의 `subsystem`과 `driver`를 찍으면, 하나의 `/dev` 노드 뒤에 **여러 계층의 드라이버**가 겹쳐 있는 것이 보인다.

게스트 스크립트 `s3_chain.sh`:

```sh
chain() { p=$(realpath $1); echo "== $1"; while [ "$p" != "/sys/devices" ] && [ "$p" != "/" ]; do
  d=$(readlink $p/driver 2>/dev/null); s=$(readlink $p/subsystem 2>/dev/null)
  echo "  ${p#/sys/devices/}  [subsys=${s##*/} driver=${d##*/}]"; p=$(dirname $p); done; }
chain /sys/dev/char/13:64
chain /sys/dev/char/90:0
chain /sys/dev/char/204:64
chain /sys/dev/block/254:0
chain /sys/class/net/eth0
chain /sys/class/regulator/regulator.0
echo "### regulator"; ls --color=never /sys/class/regulator/; cat /sys/class/regulator/regulator.0/name
echo "### clk/pinctrl in-kernel"; ls --color=never /sys/kernel/debug/clk 2>&1 | head -5; cat /sys/kernel/debug/clk/clk_summary 2>&1 | head -8; ls --color=never /sys/kernel/debug/pinctrl 2>&1 | head
```

```text
lab initramfs: 6.18.55 up 0.95s
== /sys/dev/char/13:64
  platform/gpio-keys/input/input0/event0  [subsys=input driver=]
  platform/gpio-keys/input/input0  [subsys=input driver=]
  platform/gpio-keys/input  [subsys= driver=]
  platform/gpio-keys  [subsys=platform driver=gpio-keys]
  platform  [subsys= driver=]
== /sys/dev/char/90:0
  virtual/mtd/mtd0  [subsys=mtd driver=]
  virtual/mtd  [subsys= driver=]
  virtual  [subsys= driver=]
== /sys/dev/char/204:64
  platform/9000000.pl011/9000000.pl011:0/9000000.pl011:0.0/tty/ttyAMA0  [subsys=tty driver=]
  platform/9000000.pl011/9000000.pl011:0/9000000.pl011:0.0/tty  [subsys= driver=]
  platform/9000000.pl011/9000000.pl011:0/9000000.pl011:0.0  [subsys=serial-base driver=port]
  platform/9000000.pl011/9000000.pl011:0  [subsys=serial-base driver=ctrl]
  platform/9000000.pl011  [subsys=amba driver=uart-pl011]
  platform  [subsys= driver=]
== /sys/dev/block/254:0
  platform/a003e00.virtio_mmio/virtio1/block/vda  [subsys=block driver=]
  platform/a003e00.virtio_mmio/virtio1/block  [subsys= driver=]
  platform/a003e00.virtio_mmio/virtio1  [subsys=virtio driver=virtio_blk]
  platform/a003e00.virtio_mmio  [subsys=platform driver=virtio-mmio]
  platform  [subsys= driver=]
== /sys/class/net/eth0
  platform/a003c00.virtio_mmio/virtio0/net/eth0  [subsys=net driver=]
  platform/a003c00.virtio_mmio/virtio0/net  [subsys= driver=]
  platform/a003c00.virtio_mmio/virtio0  [subsys=virtio driver=virtio_net]
  platform/a003c00.virtio_mmio  [subsys=platform driver=virtio-mmio]
  platform  [subsys= driver=]
== /sys/class/regulator/regulator.0
  faux/reg-dummy/regulator/regulator.0  [subsys=regulator driver=]
  faux/reg-dummy/regulator  [subsys= driver=]
  faux/reg-dummy  [subsys=faux driver=faux_driver]
  faux  [subsys= driver=]
### regulator
regulator.0
regulator-dummy
### clk/pinctrl in-kernel
clk24mhz
clk_dump
clk_orphan_dump
clk_orphan_summary
clk_summary
                                 enable  prepare  protect                                duty  hardware                            connection
   clock                          count    count    count        rate   accuracy phase  cycle    enable   consumer                         id
---------------------------------------------------------------------------------------------------------------------------------------------
 clk24mhz                            5       5        0        24000000    0          0     50000      Y   9010000.pl031                   apb_pclk                 
                                                                                                           9030000.pl061                   apb_pclk                 
                                                                                                           9000000.pl011                   no_connection_id         
                                                                                                           9000000.pl011                   apb_pclk                 
                                                                                                           deviceless                      no_connection_id         
pinctrl-devices
pinctrl-handles
pinctrl-maps
[    2.070499] reboot: Power down
```

출력에서 볼 것 — 이 출력 하나가 이 노트의 주제를 다 담고 있다:

- **`/dev/input/event0`**: 클래스 장치 `event0`(input 프레임워크의 evdev 핸들러가 만든 char 노드) → `input0`(input 코어의 `input_dev`) → `gpio-keys`(**platform 드라이버**). 드라이버 작성자(`gpio-keys`)는 `input_register_device()`만 불렀고, char 노드는 input 코어가 만들었다.
- **`/dev/ttyAMA0`**: `ttyAMA0`(tty 클래스) → `port`/`ctrl`(serial core가 만든 `serial-base` 버스 장치) → `9000000.pl011`(**amba 버스**의 `uart-pl011` 드라이버). UART 드라이버는 `uart_add_one_port()`만 부르고 tty 노드는 tty/serial 프레임워크가 만든다.
- **`/dev/vda`와 `eth0`**: 둘 다 **virtio-mmio(platform 드라이버, 버스/컨트롤러 역할) → virtio 버스 → virtio_blk / virtio_net(클라이언트 드라이버)** 의 2단 구조다. 같은 버스 드라이버 위에 block 드라이버와 net 드라이버가 따로 붙는다. "char/block/net"과 "버스 계층"이 직교한다는 그림 1의 실물이다.
- **`mtd0`**: 부모가 `virtual/mtd`라서 하드웨어 드라이버 링크가 끊겨 있다(이 커널에서 QEMU 플래시의 MTD 장치가 부모 없이 등록됨). sysfs 추적이 항상 하드웨어까지 이어지지는 않는다는 예다.
- **regulator와 clk**: `regulator-dummy` 하나가 `/sys/class/regulator`에 보이지만 `/dev` 노드는 없다. 클럭 `clk24mhz`(DT의 `apb-pclk` fixed-clock)는 **sysfs 클래스조차 없고 debugfs에서만** 보인다. 소비자 열을 보면 RTC(pl031), GPIO(pl061), UART(pl011)가 이 클럭을 `clk_get`으로 잡고 있다. 이것이 "커널 내부 전용 드라이버" — 유저가 아니라 **다른 드라이버에게 서비스를 제공**한다(§8).

---

## 3. char 디바이스 — 가장 작은 형태

깊은 설명(major/minor 할당 정책, `private_data`, `open`/`release`, 동시성)은 G4에서 한다. 여기서는 **"char 드라이버의 최소 부품"** 과 **유저에게 어떻게 보이는지**만 확인한다.

### 3.1 예제 5 — `g1_chr`: cdev + class + device_create

무엇을 확인하나: `alloc_chrdev_region` → `cdev_add` → `class_create` → `device_create` 네 단계로 `/dev/g1_chr`이 생기고, `read`/`write`가 우리 함수로 오는지.

```c
// SPDX-License-Identifier: GPL-2.0
/* G1 char device: the smallest "classic" char driver (G4 covers it in depth) */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>

static dev_t g1_devt;
static struct cdev g1_cdev;
static struct class *g1_class;
static const char g1_msg[] = "hello from g1_chr\n";
static bool stack;
module_param(stack, bool, 0444);	/* dump_stack() on first read */

static ssize_t g1_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
	if (stack && *off == 0)
		dump_stack();
	return simple_read_from_buffer(buf, len, off, g1_msg, sizeof(g1_msg) - 1);
}

static ssize_t g1_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
	pr_info("g1_chr: write %zu bytes\n", len);
	return len;		/* swallow everything, like /dev/null */
}

static const struct file_operations g1_fops = {
	.owner = THIS_MODULE,
	.read  = g1_read,
	.write = g1_write,
};
```

등록·해제 부분:

```c
static int __init g1_init(void)
{
	struct device *d;
	int ret = alloc_chrdev_region(&g1_devt, 0, 1, "g1_chr");

	if (ret)
		return ret;
	cdev_init(&g1_cdev, &g1_fops);
	ret = cdev_add(&g1_cdev, g1_devt, 1);
	if (ret)
		goto err_region;
	g1_class = class_create("g1_class");
	if (IS_ERR(g1_class)) {
		ret = PTR_ERR(g1_class);
		goto err_cdev;
	}
	d = device_create(g1_class, NULL, g1_devt, NULL, "g1_chr");
	if (IS_ERR(d)) {
		ret = PTR_ERR(d);
		class_destroy(g1_class);
		goto err_cdev;
	}
	pr_info("g1_chr: registered %u:%u\n", MAJOR(g1_devt), MINOR(g1_devt));
	return 0;
err_cdev:
	cdev_del(&g1_cdev);
err_region:
	unregister_chrdev_region(g1_devt, 1);
	return ret;
}

static void __exit g1_exit(void)
{
	device_destroy(g1_class, g1_devt);
	class_destroy(g1_class);
	cdev_del(&g1_cdev);
	unregister_chrdev_region(g1_devt, 1);
}
module_init(g1_init);
module_exit(g1_exit);
MODULE_DESCRIPTION("G1 minimal char device");
MODULE_LICENSE("GPL");
```

게스트 스크립트 `s4_chr.sh`:

```sh
cd /g1
echo "### mtd name"; cat /sys/class/mtd/mtd0/name /sys/class/mtd/mtd0/type
echo "### insmod"; insmod g1_chr.ko; dmesg | tail -1
echo "### node"; ls -l --color=never /dev/g1_chr; grep g1 /proc/devices
echo "### io"; cat /dev/g1_chr; echo abc > /dev/g1_chr; dmesg | tail -1
echo "### sysfs"; ls --color=never /sys/class/g1_class/g1_chr; cat /sys/class/g1_class/g1_chr/dev
M=$(cat /sys/class/g1_class/g1_chr/dev); echo "driver link: '$(readlink /sys/dev/char/$M/device/driver)'"
/g1/g1_walk | grep g1
echo "### rmmod"; rmmod g1_chr; ls /dev/g1_chr
```

```text
lab initramfs: 6.18.55 up 0.97s
### mtd name
0.flash
nor
### insmod
[    1.120427] g1_chr: registered 510:0
### node
crw-------    1 0        0         510,   0 Oct 11 01:36 /dev/g1_chr
510 g1_chr
### io
hello from g1_chr
[    1.192313] g1_chr: write 4 bytes
### sysfs
dev        power      subsystem  uevent
510:0
driver link: ''
char  510:0    g1_chr           -              g1_class
### rmmod
ls: /dev/g1_chr: No such file or directory
[    1.419992] reboot: Power down
```

출력에서 볼 것:

- **major가 510이다.** 동적 할당(`find_dynamic_major()`, fs/char_dev.c)은 먼저 254에서 234까지 빈 번호를 찾는데, 예제 2의 `/proc/devices`를 보면 234–254가 전부 프레임워크·드라이버로 차 있다. 그래서 확장 범위(511부터 384까지 내려감)로 넘어갔고, 511은 `rpmb`가 잡고 있어서 510이 됐다. **major 번호에 의미를 두지 말고 항상 `/proc/devices`나 sysfs에서 읽어야 하는 이유**다.
- `cat`이 우리 `g1_read`의 문자열을 받았고, `echo abc`(4바이트, 개행 포함)가 `g1_write`로 왔다. 중간에 아무 계층도 없다(§6에서 실측).
- `driver link: ''` — 이 장치는 **어떤 버스에도 속하지 않는 클래스 장치**라서 드라이버 링크가 없다. `device_create(..., parent=NULL, ...)`로 만들었기 때문이다. 실제 드라이버는 `probe()`에서 `device_create(class, &pdev->dev, ...)`처럼 부모를 넘겨 하드웨어 장치와 연결한다(G3, G4).
- devtmpfs가 노드를 만들었다(udev 없음). 권한이 `crw-------`인 것은 기본값이다 — 유저가 쓰게 하려면 udev rule(G9)이나 `class->devnode` 콜백으로 바꾼다.
- 첫 줄의 `0.flash` / `nor`는 예제 4의 `mtd0`이 QEMU virt의 NOR 플래시(pflash)라는 확인이다.

함정: 이 예제의 모듈은 **하드웨어와 연결이 없다.** 면접 화이트보드에서 이 코드를 쓰는 것은 괜찮지만, 실제 장치 드라이버는 이 코드를 `module_init`이 아니라 `probe()` 안에 두고, 장치 인스턴스마다 하나씩 만든다(G3). 그리고 대부분의 경우 이런 코드 자체를 쓰지 않고 프레임워크에 맡긴다(§7).

---

## 4. block 디바이스 — RAM 기반 blk-mq 디스크

### 4.1 직관 — 드라이버는 "요청 큐의 소비자"

block 드라이버는 유저와 직접 만나지 않는다. 유저의 `write()`는 page cache에 들어가고, 나중에(또는 `fsync` 때) 커널이 더러운 페이지를 **bio**(연속된 섹터 범위 + 메모리 조각 목록)로 만들고, block 계층이 bio들을 합치고 정렬해 **request**로 만든 뒤, **blk-mq**(multi-queue block layer)가 하드웨어 큐마다 드라이버의 `.queue_rq`를 부른다. Don의 NVMe 펌웨어 관점에서 보면, `.queue_rq`는 "호스트 드라이버가 SQ(submission queue)에 커맨드를 넣는 지점"이고, `blk_mq_end_request()`는 "CQ 엔트리를 보고 완료를 알리는 지점"이다. 실제로 Linux NVMe 드라이버(`drivers/nvme/host/pci.c`)의 `nvme_queue_rq()`가 바로 SQ에 커맨드를 쓴다.

blk-mq 용어:

- **tag set** (`struct blk_mq_tag_set`): 하드웨어 큐 수, 큐 깊이(동시에 날아갈 수 있는 요청 수), ops. NVMe라면 "SQ 개수 × SQ 깊이"에 해당한다.
- **hctx** (`struct blk_mq_hw_ctx`): 하드웨어 큐 하나.
- **request** (`struct request`): 시작 섹터(`blk_rq_pos`), 바이트 수(`blk_rq_bytes`), 연산(`req_op`: READ/WRITE/FLUSH/DISCARD …), 그리고 bio 체인.
- **gendisk** (`struct gendisk`): 디스크 하나 — 이름, 용량, major/minor, 파티션.

### 4.2 예제 6 — `g1_blk`: 4 MiB RAM 디스크

무엇을 확인하나: 6.18 API(`blk_mq_alloc_sq_tag_set`, `blk_mq_alloc_disk(&set, &lim, data)`, `add_disk`)로 block 장치를 만들고, 그 위에 vfat을 만들어 마운트·읽기·쓰기가 되는지. 그리고 512바이트 쓰기가 드라이버에 몇 번의 요청으로 오는지.

6.18 API 확인 (랩 소스 그대로):

```sh
limactl shell elinux -- bash -lc 'S=/var/lab/src/linux-6.18.55; sed -n 725,733p $S/include/linux/blk-mq.h; sed -n 743,745p $S/include/linux/blk-mq.h'
```

```text
struct gendisk *__blk_mq_alloc_disk(struct blk_mq_tag_set *set,
		struct queue_limits *lim, void *queuedata,
		struct lock_class_key *lkclass);
#define blk_mq_alloc_disk(set, lim, queuedata)				\
({									\
	static struct lock_class_key __key;				\
									\
	__blk_mq_alloc_disk(set, lim, queuedata, &__key);		\
})
int blk_mq_alloc_sq_tag_set(struct blk_mq_tag_set *set,
		const struct blk_mq_ops *ops, unsigned int queue_depth,
		unsigned int set_flags);
```

6.9 즈음부터 `blk_mq_alloc_disk`가 `struct queue_limits *`를 받도록 바뀌었고, 블록 크기·최대 전송 크기 같은 제약은 **디스크를 만들 때 한 번에** 넘긴다. 예전 코드의 `blk_queue_logical_block_size()` 같은 개별 setter는 이 방식으로 대체되고 있다. 옛 튜토리얼(LDD3, 5.x 블로그)의 block 코드는 그대로는 안 빌드된다.

요청 처리부:

```c
// SPDX-License-Identifier: GPL-2.0
/* G1 block device: 4 MiB RAM disk on blk-mq (6.18 API) */
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/blk-mq.h>
#include <linux/vmalloc.h>

#define G1_BYTES (4u << 20)
static int g1_major;
static u8 *g1_data;
static struct blk_mq_tag_set g1_set;
static struct gendisk *g1_disk;
static atomic_t g1_nr_rd, g1_nr_wr;
static bool stack;
module_param(stack, bool, 0444);	/* dump_stack() on the first write */

static blk_status_t g1_queue_rq(struct blk_mq_hw_ctx *hctx,
				const struct blk_mq_queue_data *bd)
{
	struct request *rq = bd->rq;
	loff_t pos = (loff_t)blk_rq_pos(rq) << SECTOR_SHIFT;
	struct req_iterator iter;
	struct bio_vec bv;

	blk_mq_start_request(rq);
	if ((req_op(rq) != REQ_OP_READ && req_op(rq) != REQ_OP_WRITE) ||
	    pos + blk_rq_bytes(rq) > G1_BYTES) {
		blk_mq_end_request(rq, BLK_STS_IOERR);
		return BLK_STS_OK;
	}
	if (stack && req_op(rq) == REQ_OP_WRITE && !atomic_read(&g1_nr_wr))
		dump_stack();
	atomic_inc(req_op(rq) == REQ_OP_WRITE ? &g1_nr_wr : &g1_nr_rd);
	rq_for_each_segment(bv, rq, iter) {
		void *p = bvec_kmap_local(&bv);

		if (req_op(rq) == REQ_OP_WRITE)
			memcpy(g1_data + pos, p, bv.bv_len);
		else
			memcpy(p, g1_data + pos, bv.bv_len);
		kunmap_local(p);
		pos += bv.bv_len;
	}
	blk_mq_end_request(rq, BLK_STS_OK);	/* RAM: complete inline */
	return BLK_STS_OK;
}

static const struct blk_mq_ops g1_mq_ops = { .queue_rq = g1_queue_rq };
static const struct block_device_operations g1_bdops = { .owner = THIS_MODULE };
```

등록·해제부:

```c
static int __init g1_init(void)
{
	struct queue_limits lim = { .logical_block_size = 512 };
	int ret;

	g1_data = vzalloc(G1_BYTES);
	if (!g1_data)
		return -ENOMEM;
	g1_major = register_blkdev(0, "g1_blk");	/* shows up in /proc/devices */
	if (g1_major < 0) {
		ret = g1_major;
		goto err_mem;
	}
	ret = blk_mq_alloc_sq_tag_set(&g1_set, &g1_mq_ops, 16, 0);
	if (ret)
		goto err_major;
	g1_disk = blk_mq_alloc_disk(&g1_set, &lim, NULL);
	if (IS_ERR(g1_disk)) {
		ret = PTR_ERR(g1_disk);
		goto err_set;
	}
	g1_disk->major = g1_major;
	g1_disk->first_minor = 0;
	g1_disk->minors = 1;
	g1_disk->fops = &g1_bdops;
	strscpy(g1_disk->disk_name, "g1_blk0", DISK_NAME_LEN);
	set_capacity(g1_disk, G1_BYTES >> SECTOR_SHIFT);
	ret = add_disk(g1_disk);
	if (ret)
		goto err_disk;
	pr_info("g1_blk: major %d, %u sectors\n", g1_major, G1_BYTES >> SECTOR_SHIFT);
	return 0;
err_disk:
	put_disk(g1_disk);
err_set:
	blk_mq_free_tag_set(&g1_set);
err_major:
	unregister_blkdev(g1_major, "g1_blk");
err_mem:
	vfree(g1_data);
	return ret;
}

static void __exit g1_exit(void)
{
	pr_info("g1_blk: served %d read rq, %d write rq\n",
		atomic_read(&g1_nr_rd), atomic_read(&g1_nr_wr));
	del_gendisk(g1_disk);
	put_disk(g1_disk);
	blk_mq_free_tag_set(&g1_set);
	unregister_blkdev(g1_major, "g1_blk");
	vfree(g1_data);
}
module_init(g1_init);
module_exit(g1_exit);
MODULE_DESCRIPTION("G1 RAM-backed blk-mq disk");
MODULE_LICENSE("GPL");
```

코드에서 볼 것:

- `.queue_rq`는 **즉시 완료**한다(RAM이니까). 실제 하드웨어 드라이버는 여기서 DMA 디스크립터를 채우고 doorbell을 울린 뒤 `BLK_STS_OK`를 반환하고, **완료는 인터럽트 핸들러에서** `blk_mq_end_request()`(또는 `blk_mq_complete_request()`)로 한다. 큐가 꽉 찼으면 `BLK_STS_RESOURCE`를 반환해 나중에 다시 받는다.
- `rq_for_each_segment`로 bio_vec(페이지 + 오프셋 + 길이) 조각을 돈다. 하드웨어 드라이버는 이 대신 `blk_rq_map_sg()`로 scatter-gather 목록을 만들어 `dma_map_sg()`에 넘긴다(H9).
- `register_blkdev(0, ...)`는 `/proc/devices`에 이름을 올리고 동적 major를 받기 위해 썼다. 현대 드라이버 중에는 이것을 생략하고 `disk->major = 0`으로 두어 확장 major(259 `blkext`)를 받는 것도 있다.
- `BLK_FEAT_WRITE_CACHE`를 켜지 않았으므로 FLUSH 요청은 오지 않는다. READ/WRITE 외의 연산은 에러로 끝낸다.

게스트 스크립트 `s5_blk.sh`:

```sh
cd /g1
echo "### insmod"; insmod g1_blk.ko; dmesg | tail -1
echo "### node"; ls -l --color=never /dev/g1_blk0; grep g1 /proc/devices
echo "### sysfs"; ls --color=never /sys/block/; cat /sys/block/g1_blk0/size /sys/block/g1_blk0/queue/logical_block_size /sys/block/g1_blk0/queue/nr_requests /sys/block/g1_blk0/queue/scheduler
ls --color=never /sys/block/g1_blk0/mq
echo "### raw io"; echo "sensorkit-log-0001" | dd of=/dev/g1_blk0 bs=512 seek=8 conv=sync 2>&1 | tail -1
dd if=/dev/g1_blk0 bs=512 skip=8 count=1 2>/dev/null | hexdump -C | head -2
cat /sys/block/g1_blk0/stat
echo "### mkfs+mount"; mkfs.vfat -n G1DISK /dev/g1_blk0 >/dev/null && echo mkfs ok
mkdir -p /mnt/g1 && mount -t vfat /dev/g1_blk0 /mnt/g1 && echo "imu,1,2,3" > /mnt/g1/imu.csv && sync
df -k /mnt/g1 | tail -1; ls -l --color=never /mnt/g1; cat /proc/mounts | grep g1
umount /mnt/g1; mount -t vfat /dev/g1_blk0 /mnt/g1 && cat /mnt/g1/imu.csv; umount /mnt/g1
cat /sys/block/g1_blk0/stat
echo "### rmmod"; rmmod g1_blk; dmesg | tail -1; ls /dev/g1_blk0
```

```text
lab initramfs: 6.18.55 up 1.50s
### insmod
[    1.764805] g1_blk: major 253, 8192 sectors
### node
brw-------    1 0        0         253,   0 Oct 11 01:37 /dev/g1_blk0
253 g1_blk
### sysfs
g1_blk0    loop1      loop3      loop5      loop7      vda
loop0      loop2      loop4      loop6      mtdblock0
8192
512
32
none [mq-deadline] kyber bfq 
0
### raw io
512 bytes (512B) copied, 0.006988 seconds, 71.6KB/s
00000000  73 65 6e 73 6f 72 6b 69  74 2d 6c 6f 67 2d 30 30  |sensorkit-log-00|
00000010  30 31 0a 00 00 00 00 00  00 00 00 00 00 00 00 00  |01..............|
       3        0       24        0        1        0        8        0        0        0        0        0        0        0        0        0        0
### mkfs+mount
mkfs ok
/dev/g1_blk0              4030         1      4029   0% /mnt/g1
total 1
-rwxr-xr-x    1 0        0               10 Oct 11 01:37 imu.csv
/dev/g1_blk0 /mnt/g1 vfat rw,relatime,fmask=0022,dmask=0022,codepage=437,iocharset=iso8859-1,shortname=mixed,errors=remount-ro 0 0
imu,1,2,3
      30       61      231        1       12       17      155        1        0        8        3        0        0        0        0        0        0
### rmmod
[    2.310243] g1_blk: served 30 read rq, 12 write rq
ls: /dev/g1_blk0: No such file or directory
[    2.473954] reboot: Power down
```

출력에서 볼 것:

- `ls -l` 첫 글자가 `b`, major 253. block 동적 major는 254부터 내려가며 찾는데 254는 `virtblk`가 쓰고 있었다.
- 큐 설정: `logical_block_size` 512(우리가 `lim`으로 준 값), `mq/` 아래 하드웨어 큐 `0` 하나(sq = single queue tag set), 스케줄러는 **mq-deadline이 기본 선택**됐다(단일 하드웨어 큐 장치의 기본). `nr_requests`가 32인 것은 우리가 준 큐 깊이 16의 두 배다 — 스케줄러가 붙으면 `blk_mq_default_nr_requests()`가 `2 * min(queue_depth, BLKDEV_DEFAULT_RQ)`로 잡는다(block/blk-mq.h).
- **512바이트 `dd` 한 번이 드라이버에 "읽기 3회(24섹터), 쓰기 1회(8섹터)"로 왔다** (`stat` 1·3·5·7번째 필드: read I/Os, read sectors, write I/Os, write sectors). 블록 장치 노드에 대한 `write()`도 **page cache를 거친다**. 512 B를 쓰려면 4 KiB 페이지 전체가 캐시에 있어야 하므로 먼저 읽고(read-modify-write), 나중에 페이지 단위(8섹터)로 쓴다. 나머지 읽기는 `add_disk()` 때의 파티션 테이블 스캔 등이다. 이것이 char와 결정적으로 다른 점이다 — char 드라이버는 유저가 준 바이트를 그대로 받는다.
- 그 위에 **vfat 파일시스템**이 그대로 올라갔다(`mkfs.vfat`, `mount`, 파일 쓰기, 다시 마운트해 읽기). 드라이버는 파일시스템을 전혀 모른다. 섹터만 안다. 이 분리 덕분에 같은 드라이버 위에 ext4·vfat·squashfs 무엇이든 올린다.
- 최종 카운트: 읽기 요청 30개, 쓰기 요청 12개. 이 수치는 `/sys/block/g1_blk0/stat`의 1번째(30), 5번째(12) 필드와 정확히 같다 — block 계층의 통계는 드라이버가 아니라 **block 계층이 대신 센다.**

함정: "RAM 디스크는 brd가 있지 않나?" — 맞다, `drivers/block/brd.c`가 커널의 RAM 디스크다. 다만 brd는 blk-mq를 쓰지 않고 bio를 직접 받는 **bio-based** 드라이버다(`submit_bio`를 구현). 고속 RAM/가상 장치에서는 스케줄러·request 단계를 건너뛰는 bio-based가 유리할 때가 있고, 실제 하드웨어 컨트롤러는 거의 다 blk-mq를 쓴다.

---

## 5. network 디바이스 — 패킷을 세고 버리는 dummy NIC

### 5.1 직관 — 왜 `/dev/eth0`이 없나

네트워크 장치에는 `/dev` 노드가 없다. 이유는 두 가지다.

1. **데이터가 장치 쪽에서 먼저 온다.** 디스크는 유저가 요청해야 데이터가 오지만, NIC는 아무도 안 읽어도 패킷이 쏟아진다. 커널은 그 패킷을 프로토콜 스택(IP, TCP)에 넘겨 **어느 소켓의 것인지** 정해야 한다. 그래서 유저와 만나는 지점이 장치가 아니라 **소켓**이다.
2. **하나의 장치를 수많은 연결이 공유한다.** `eth0` 하나에 TCP 연결 1000개가 동시에 흐른다. "장치 파일을 연다"는 모델로는 이것을 표현할 수 없다.

그래서 유저는 `socket()`으로 통신하고, 장치 자체의 설정(주소, up/down, MTU)은 **netlink**(`ip` 명령)나 `ioctl(SIOCSIFFLAGS)`(`ifconfig`)로 한다. 드라이버는 `struct net_device`를 등록하고 `.ndo_start_xmit`으로 **보낼 패킷(sk_buff)** 을 받으며, 받은 패킷은 `netif_rx()`나 NAPI(`netif_receive_skb()` 계열)로 스택에 **밀어 올린다**(I6).

### 5.2 예제 7 — `g1_net`: alloc_netdev + register_netdev

무엇을 확인하나: `alloc_netdev` → `register_netdev`만으로 `g1_net0` 인터페이스가 생기고, `/dev`에는 아무것도 생기지 않으며, `ping`이 보낸 패킷이 우리 `ndo_start_xmit`으로 오고 통계에 잡히는지.

```c
// SPDX-License-Identifier: GPL-2.0
/* G1 net device: a dummy NIC that counts and drops every TX packet */
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

static struct net_device *g1_ndev;
static bool stack;
module_param(stack, bool, 0444);	/* dump_stack() on the first packet */

static netdev_tx_t g1_xmit(struct sk_buff *skb, struct net_device *dev)
{
	if (stack && !DEV_STATS_READ(dev, tx_packets))
		dump_stack();
	DEV_STATS_INC(dev, tx_packets);
	DEV_STATS_ADD(dev, tx_bytes, skb->len);
	netdev_info(dev, "xmit len=%u proto=0x%04x\n", skb->len, ntohs(skb->protocol));
	dev_kfree_skb(skb);		/* a real NIC would DMA it out here */
	return NETDEV_TX_OK;
}

static int g1_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}

static int g1_stop(struct net_device *dev)
{
	netif_stop_queue(dev);
	return 0;
}

static const struct net_device_ops g1_ops = {
	.ndo_open       = g1_open,
	.ndo_stop       = g1_stop,
	.ndo_start_xmit = g1_xmit,
};

static void g1_setup(struct net_device *dev)
{
	ether_setup(dev);
	dev->netdev_ops = &g1_ops;
	dev->flags |= IFF_NOARP;
	eth_hw_addr_random(dev);
}

static int __init g1_init(void)
{
	int ret;

	g1_ndev = alloc_netdev(0, "g1_net%d", NET_NAME_ENUM, g1_setup);
	if (!g1_ndev)
		return -ENOMEM;
	ret = register_netdev(g1_ndev);
	if (ret) {
		free_netdev(g1_ndev);
		return ret;
	}
	netdev_info(g1_ndev, "registered, ifindex %d\n", g1_ndev->ifindex);
	return 0;
}

static void __exit g1_exit(void)
{
	unregister_netdev(g1_ndev);
	free_netdev(g1_ndev);
}
module_init(g1_init);
module_exit(g1_exit);
MODULE_DESCRIPTION("G1 dummy net_device");
MODULE_LICENSE("GPL");
```

코드에서 볼 것:

- `"g1_net%d"` — 이름의 `%d`는 커널이 빈 번호로 채운다(`eth%d`와 같은 방식).
- `ether_setup()`이 MTU 1500, 헤더 길이, 브로드캐스트 주소 등 이더넷 기본값을 채운다. CAN 드라이버라면 `alloc_candev()`가 CAN용 기본값을 채운다(I10).
- `IFF_NOARP`: ARP 응답을 받을 상대가 없으므로 ARP를 끄고 바로 IP 패킷을 보내게 했다.
- `DEV_STATS_INC/ADD`는 6.x에서 `net_device_stats` 필드를 atomic으로 갱신하는 매크로다(여러 CPU가 동시에 xmit 해도 안전). 커널의 `drivers/net/dummy.c`는 대신 per-CPU 통계(`dev_lstats_add`)를 쓴다.
- 실제 NIC는 `.ndo_start_xmit`에서 sk_buff를 DMA 매핑해 TX 링에 넣고, 링이 꽉 차면 `netif_stop_queue()`, TX 완료 인터럽트에서 `netif_wake_queue()`와 `dev_consume_skb_any()`를 한다.

게스트 스크립트 `s6_net.sh`:

```sh
cd /g1
echo "### insmod"; insmod g1_net.ko; dmesg | tail -1
echo "### no dev node"; ls /dev | grep -c g1; grep g1 /proc/devices; echo "(nothing above = no node, no major)"
echo "### sysfs"; ls --color=never /sys/class/net; readlink /sys/class/net/g1_net0; ls --color=never /sys/class/net/g1_net0/statistics | head -3
echo "### up"; ip addr add 10.1.1.1/24 dev g1_net0; ip link set g1_net0 up; ip link show g1_net0; ifconfig g1_net0
echo "### ping"; ping -c 3 -W 1 10.1.1.2 2>&1 | tail -2
echo "### counters"; cat /sys/class/net/g1_net0/statistics/tx_packets /sys/class/net/g1_net0/statistics/tx_bytes
dmesg | grep xmit
echo "### route"; ip route show dev g1_net0
echo "### rmmod"; ip link set g1_net0 down; rmmod g1_net; ls --color=never /sys/class/net
```

```text
lab initramfs: 6.18.55 up 1.79s
### insmod
[    2.067521] g1_net0: registered, ifindex 3
### no dev node
0
(nothing above = no node, no major)
### sysfs
eth0     g1_net0  lo
../../devices/virtual/net/g1_net0
collisions
multicast
rx_bytes
### up
3: g1_net0: <BROADCAST,MULTICAST,NOARP,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast qlen 1000
    link/ether 12:93:41:d2:af:b3 brd ff:ff:ff:ff:ff:ff
g1_net0   Link encap:Ethernet  HWaddr 12:93:41:D2:AF:B3  
          inet addr:10.1.1.1  Bcast:0.0.0.0  Mask:255.255.255.0
          UP BROADCAST RUNNING NOARP MULTICAST  MTU:1500  Metric:1
          RX packets:0 errors:0 dropped:0 overruns:0 frame:0
          TX packets:0 errors:0 dropped:0 overruns:0 carrier:0
          collisions:0 txqueuelen:1000 
          RX bytes:0 (0.0 B)  TX bytes:0 (0.0 B)

### ping
--- 10.1.1.2 ping statistics ---
3 packets transmitted, 0 packets received, 100% packet loss
### counters
3
294
[    2.467948] g1_net0: xmit len=98 proto=0x0800
[    3.472252] g1_net0: xmit len=98 proto=0x0800
[    4.474210] g1_net0: xmit len=98 proto=0x0800
### route
10.1.1.0/24 dev g1_net0 scope link  src 10.1.1.1 
### rmmod
eth0  lo
[    5.684249] reboot: Power down
```

출력에서 볼 것:

- `/dev`에 g1 노드 0개, `/proc/devices`에도 없다. 대신 `/sys/class/net/g1_net0`이 생겼고 `ifindex 3`(1=lo, 2=eth0)을 받았다. 부모가 없으므로 `devices/virtual/net/` 아래에 있다 — 실제 NIC는 `SET_NETDEV_DEV(ndev, &pdev->dev)`로 부모를 지정해 `platform/.../net/eth0`처럼 하드웨어 밑에 붙는다(예제 4의 `eth0`).
- `ip link set up`이 우리 `.ndo_open`을 불렀고, `qdisc pfifo_fast qlen 1000`이 자동으로 붙었다 — **드라이버 앞에 큐잉 계층(qdisc)이 있다.**
- ping이 보낸 ICMP 3개가 전부 `g1_xmit`으로 왔다. 길이 98 = 이더넷 헤더 14 + IP 헤더 20 + ICMP 헤더 8 + 데이터 56. `proto=0x0800`은 IPv4 EtherType이다. **이더넷 헤더까지 스택이 다 만들어서** 드라이버에 준다.
- `tx_packets 3`, `tx_bytes 294`(=98×3)가 sysfs `statistics/`에 바로 반영됐다. `ifconfig`의 0은 ping 전에 찍은 것이다.
- 응답은 당연히 없다(100% loss) — 우리 드라이버는 수신 경로가 없다. 수신을 흉내 내려면 sk_buff를 만들어 `eth_type_trans()`로 프로토콜을 정하고 `netif_rx()`로 올리면 된다(직접 해보기 4번).

---

## 6. 세 가지 데이터 경로 — `dump_stack()`으로 실측

### 6.1 예제 8 — 유저 syscall부터 드라이버 콜백까지

무엇을 확인하나: 세 모듈을 `stack=1`로 넣고 각각 한 번씩 I/O를 일으켜, 드라이버 콜백에서 찍은 **실제 콜 스택**으로 세 종류의 중간 계층 깊이를 비교한다.

게스트 스크립트(`s9_chrstack.sh`, `s8_stack.sh`):

```sh
cd /g1; insmod g1_chr.ko stack=1; dd if=/dev/g1_chr bs=64 count=1 2>/dev/null
dmesg | grep -E "^\[.*\]  [a-z_0-9.]+\+0x|Comm:|Call trace" | head -20
```

```sh
cd /g1
echo "### blk stack"; insmod g1_blk.ko stack=1; echo hi | dd of=/dev/g1_blk0 bs=512 conv=sync,fsync 2>/dev/null
dmesg | grep -A40 "CPU:" | grep -E "^\[.*\]  [a-z_0-9.]+\+0x|CPU:|Call trace" | head -30
rmmod g1_blk; dmesg -c > /dev/null
echo "### net stack"; insmod g1_net.ko stack=1; ip addr add 10.1.1.1/24 dev g1_net0; ip link set g1_net0 up
ping -c 1 -W 1 10.1.1.2 > /dev/null 2>&1
dmesg | grep -E "^\[.*\]  [a-z_0-9.]+\+0x|CPU:|Call trace" | head -40
```

char (`read` → `g1_read`):

```text
hello from g1_chr
[    1.516007] CPU: 1 UID: 0 PID: 71 Comm: dd Tainted: G           O        6.18.55 #1 PREEMPT 
[    1.516315] Call trace:
[    1.516449]  show_stack+0x28/0x7c (C)
[    1.516857]  dump_stack_lvl+0x60/0x80
[    1.516891]  dump_stack+0x18/0x24
[    1.516904]  g1_read+0x54/0x60 [g1_chr]
[    1.517186]  vfs_read+0xc4/0x33c
[    1.517200]  ksys_read+0x64/0x100
[    1.517210]  __arm64_sys_read+0x18/0x24
[    1.517220]  invoke_syscall.constprop.0+0x40/0xe0
[    1.517230]  el0_svc_common.constprop.0+0x38/0xe0
[    1.517239]  do_el0_svc+0x1c/0x28
[    1.517247]  el0_svc+0x38/0x118
[    1.517259]  el0t_64_sync_handler+0xa0/0xe4
[    1.517268]  el0t_64_sync+0x198/0x19c
```

block (`fsync` → `g1_queue_rq`):

```text
### blk stack
[    0.121785] CPU: All CPU(s) started at EL1
[    2.358144] CPU: 0 UID: 0 PID: 72 Comm: dd Tainted: G           O        6.18.55 #1 PREEMPT 
[    2.362890] Call trace:
[    2.363048]  show_stack+0x28/0x7c (C)
[    2.363499]  dump_stack_lvl+0x60/0x80
[    2.363726]  dump_stack+0x18/0x24
[    2.363769]  g1_queue_rq+0x210/0x224 [g1_blk]
[    2.364426]  blk_mq_dispatch_rq_list+0x100/0x6f0
[    2.364449]  __blk_mq_sched_dispatch_requests+0x47c/0x560
[    2.364461]  blk_mq_sched_dispatch_requests+0x30/0x84
[    2.364471]  blk_mq_run_hw_queue+0x270/0x2c8
[    2.364482]  blk_mq_dispatch_list+0xfc/0x340
[    2.364493]  blk_mq_flush_plug_list+0x50/0x190
[    2.364504]  __blk_flush_plug+0xe4/0x144
[    2.364517]  blk_finish_plug+0x2c/0x40
[    2.364528]  blkdev_writepages+0x8c/0xc4
[    2.364538]  do_writepages+0xa8/0x180
[    2.364549]  filemap_fdatawrite_wbc+0x64/0x8c
[    2.364560]  __filemap_fdatawrite_range+0x60/0x94
[    2.366040]  file_write_and_wait_range+0x50/0xbc
[    2.366074]  blkdev_fsync+0x34/0x58
[    2.366099]  vfs_fsync_range+0x40/0x98
[    2.366132]  do_fsync+0x38/0x98
[    2.366152]  __arm64_sys_fsync+0x18/0x28
[    2.366173]  invoke_syscall.constprop.0+0x40/0xe0
[    2.366196]  el0_svc_common.constprop.0+0x38/0xe0
[    2.366218]  do_el0_svc+0x1c/0x28
[    2.366238]  el0_svc+0x38/0x118
[    2.366272]  el0t_64_sync_handler+0xa0/0xe4
[    2.366301]  el0t_64_sync+0x198/0x19c
```

net (`sendto` → `g1_xmit`):

```text
### net stack
[    2.750964] CPU: 0 UID: 0 PID: 82 Comm: ping Tainted: G           O        6.18.55 #1 PREEMPT 
[    2.751053] Call trace:
[    2.751061]  show_stack+0x28/0x7c (C)
[    2.751099]  dump_stack_lvl+0x60/0x80
[    2.751112]  dump_stack+0x18/0x24
[    2.751121]  g1_xmit+0xc8/0xd4 [g1_net]
[    2.751141]  dev_hard_start_xmit+0xa0/0x11c
[    2.751157]  sch_direct_xmit+0x8c/0x37c
[    2.751170]  __dev_queue_xmit+0x9f8/0xec4
[    2.751181]  neigh_resolve_output+0xf0/0x1c0
[    2.751192]  ip_finish_output2+0x1f4/0x6bc
[    2.751202]  __ip_finish_output+0xa0/0x2a0
[    2.751212]  ip_finish_output+0x2c/0x140
[    2.751222]  ip_output+0x70/0x11c
[    2.751232]  ip_send_skb+0x44/0xe0
[    2.751242]  ip_push_pending_frames+0x30/0x60
[    2.751252]  raw_sendmsg+0x4b0/0xc64
[    2.751263]  inet_sendmsg+0x40/0x64
[    2.751274]  __sys_sendto+0x11c/0x190
[    2.751287]  __arm64_sys_sendto+0x24/0x40
[    2.751299]  invoke_syscall.constprop.0+0x40/0xe0
[    2.751309]  el0_svc_common.constprop.0+0x38/0xe0
[    2.751319]  do_el0_svc+0x1c/0x28
[    2.751328]  el0_svc+0x38/0x118
[    2.751339]  el0t_64_sync_handler+0xa0/0xe4
[    2.751349]  el0t_64_sync+0x198/0x19c
```

(세 출력은 같은 노트 실행에서 grep으로 콜 스택 줄만 남긴 것이다. 레지스터 덤프 등 나머지 줄은 grep이 걸러 냈다. block 출력 첫 줄 `CPU: All CPU(s) started at EL1`은 부팅 로그가 grep에 함께 걸린 것이다.)

출력에서 볼 것 — 아래쪽 `el0t_64_sync` → `el0_svc` → `invoke_syscall`은 셋 다 같다. 이것이 A1·F2에서 본 "SVC 예외로 커널 진입" 경로다. 차이는 그 위다.

- **char**: `__arm64_sys_read` → `ksys_read` → `vfs_read` → **`g1_read`**. 중간에 함수 2개. `vfs_read`가 `file->f_op->read`를 바로 부른다(F12 §4에서 본 `chrdev_open`이 `f_op`를 우리 fops로 바꿔 끼웠기 때문이다).
- **block**: `fsync` → VFS → **page cache writeback**(`filemap_fdatawrite_wbc`, `do_writepages`, `blkdev_writepages`) → **plug 해제**(`blk_finish_plug`: 모아 둔 요청을 한꺼번에 내려보냄) → **I/O 스케줄러**(`blk_mq_sched_dispatch_requests`: mq-deadline) → `blk_mq_dispatch_rq_list` → **`g1_queue_rq`**. 중간 16개. 그리고 이 경로는 **유저 `write()`가 아니라 `fsync()`** 에서 시작됐다 — `write()` 자체는 page cache에 복사하고 바로 돌아왔다.
- **net**: `sendto` → 소켓(`inet_sendmsg`, `raw_sendmsg` — BusyBox ping은 raw 소켓을 쓴다) → **IP 계층**(`ip_push_pending_frames` … `ip_finish_output2`) → **neighbour**(`neigh_resolve_output`: 목적지 MAC 결정) → **qdisc**(`__dev_queue_xmit`, `sch_direct_xmit`: pfifo_fast) → `dev_hard_start_xmit` → **`g1_xmit`**. 중간 13개.

```svg
<svg viewBox="0 0 640 170" xmlns="http://www.w3.org/2000/svg">
  <text x="320" y="22" text-anchor="middle" font-size="14" font-weight="bold">syscall 진입 함수와 드라이버 콜백 사이 커널 함수 수</text>
  <text x="202" y="55" text-anchor="end" font-size="12">char: read → g1_read</text>
  <rect x="210" y="43" width="41" height="22" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="257" y="55" font-size="12">2</text>
  <text x="202" y="85" text-anchor="end" font-size="12">net: sendto → g1_xmit</text>
  <rect x="210" y="73" width="268" height="22" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="484" y="85" font-size="12">13</text>
  <text x="202" y="115" text-anchor="end" font-size="12">block: fsync → g1_queue_rq</text>
  <rect x="210" y="103" width="330" height="22" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
  <text x="546" y="115" font-size="12">16</text>
  <line x1="210" y1="36" x2="210" y2="130" stroke="currentColor"/>
  <text x="20" y="150" font-size="12">예제 8의 dump_stack() 실측 — sys 진입 함수와 드라이버 함수 사이 프레임 수 (6.18.55, arm64)</text>
</svg>
```
그림 4 — 예제 8에서 센 "중간 계층의 두께". char는 VFS가 드라이버를 거의 직접 부르고, block은 page cache·plug·스케줄러를, net은 소켓·IP·neighbour·qdisc를 거친다. 함수 개수는 인라인 여부에 따라 커널 버전·설정마다 달라지므로 **절대값보다 상대 크기**를 본다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <g font-size="13" font-weight="bold" text-anchor="middle">
    <text x="114" y="22">char</text><text x="340" y="22">block</text><text x="566" y="22">net</text>
  </g>
  <line x1="0" y1="80" x2="680" y2="80" stroke="currentColor" stroke-dasharray="4 4"/>
  <text x="676" y="77" text-anchor="end" font-size="12">유저 / 커널 경계 (SVC)</text>
  <g font-size="12" text-anchor="middle">
    <rect x="10" y="34" width="208" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="114" y="54">read(fd) / write(fd)</text>
    <rect x="10" y="90" width="208" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="114" y="110">VFS: vfs_read → f_op</text>
    <rect x="10" y="130" width="208" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.24" stroke="#4a7bd0"/><text x="114" y="150">드라이버 fops.read</text>
    <rect x="10" y="390" width="208" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="114" y="410">HW: FIFO, 레지스터</text>

    <rect x="236" y="34" width="208" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="340" y="54">write(file) / fsync</text>
    <rect x="236" y="90" width="208" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="340" y="110">파일시스템 (vfat, ext4)</text>
    <rect x="236" y="130" width="208" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="340" y="150">page cache, writeback</text>
    <rect x="236" y="170" width="208" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="340" y="190">bio 생성, submit_bio</text>
    <rect x="236" y="210" width="208" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="340" y="230">plug: 요청 모으기·병합</text>
    <rect x="236" y="250" width="208" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="340" y="270">I/O 스케줄러 (mq-deadline)</text>
    <rect x="236" y="290" width="208" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="340" y="310">blk-mq: hctx, tag 할당</text>
    <rect x="236" y="330" width="208" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.24" stroke="#3f9a6b"/><text x="340" y="350">드라이버 queue_rq</text>
    <rect x="236" y="390" width="208" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="340" y="410">HW: DMA + 완료 IRQ</text>

    <rect x="462" y="34" width="208" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="566" y="54">sendto(sock)</text>
    <rect x="462" y="90" width="208" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="566" y="110">소켓 (inet, raw, udp, tcp)</text>
    <rect x="462" y="130" width="208" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="566" y="150">IP: 라우팅, ip_output</text>
    <rect x="462" y="170" width="208" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="566" y="190">neighbour: 목적지 MAC</text>
    <rect x="462" y="210" width="208" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="566" y="230">qdisc (pfifo_fast)</text>
    <rect x="462" y="250" width="208" height="30" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="566" y="270">dev_hard_start_xmit</text>
    <rect x="462" y="290" width="208" height="30" rx="4" fill="#e08a3c" fill-opacity="0.24" stroke="#e08a3c"/><text x="566" y="310">드라이버 ndo_start_xmit</text>
    <rect x="462" y="390" width="208" height="30" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="566" y="410">HW: TX 링 DMA</text>
  </g>
  <g stroke="currentColor" marker-end="url(#ah3)">
    <line x1="114" y1="64" x2="114" y2="88"/><line x1="114" y1="120" x2="114" y2="128"/><line x1="114" y1="160" x2="114" y2="388"/>
    <line x1="340" y1="64" x2="340" y2="88"/><line x1="340" y1="120" x2="340" y2="128"/><line x1="340" y1="160" x2="340" y2="168"/><line x1="340" y1="200" x2="340" y2="208"/><line x1="340" y1="240" x2="340" y2="248"/><line x1="340" y1="280" x2="340" y2="288"/><line x1="340" y1="320" x2="340" y2="328"/><line x1="340" y1="360" x2="340" y2="388"/>
    <line x1="566" y1="64" x2="566" y2="88"/><line x1="566" y1="120" x2="566" y2="128"/><line x1="566" y1="160" x2="566" y2="168"/><line x1="566" y1="200" x2="566" y2="208"/><line x1="566" y1="240" x2="566" y2="248"/><line x1="566" y1="280" x2="566" y2="288"/><line x1="566" y1="320" x2="566" y2="388"/>
  </g>
  <text x="340" y="446" text-anchor="middle" font-size="12">진한 상자 = 드라이버가 구현하는 지점. 수신(RX)은 반대 방향: block은 완료 IRQ → end_request,</text>
  <text x="340" y="464" text-anchor="middle" font-size="12">net은 RX IRQ → NAPI poll → netif_receive_skb → IP → 소켓 큐 (유저가 읽기 전에 커널이 먼저 받는다)</text>
</svg>
```
그림 5 — 세 종류의 송신(쓰기) 데이터 경로. 상자 이름은 예제 8의 콜 스택에서 실제로 본 계층을 묶은 것이다. char는 드라이버가 유저와 거의 맞닿아 있어서 **정책(버퍼링, 블로킹, 포맷)을 드라이버가 다 정해야 하고**, block·net은 커널이 정책을 대부분 갖고 있어서 **드라이버는 "하드웨어 큐에 넣기"만 한다.**

---

## 7. "대부분은 서브시스템 드라이버" — 고전 분류가 부족한 이유

### 7.1 직관 — 같은 char 노드라도 규칙이 다르다

예제 2의 `/proc/devices`에서 char major 대부분이 `input`, `iio`, `rtc`, `watchdog`, `gpiochip` 같은 **프레임워크 이름**이었다. 이것들은 전부 "char 디바이스"지만, 유저가 쓰는 규칙은 완전히 다르다.

- `/dev/input/event0`에서 `read()`하면 항상 24바이트(64비트에서) `struct input_event`가 나온다.
- `/dev/rtc0`에는 `ioctl(RTC_RD_TIME)`을 쓴다.
- `/dev/watchdog`에는 아무 바이트나 써서 "킥"하고, `V`를 쓰고 닫으면 멈춘다(magic close).
- `/dev/gpiochip0`에는 `ioctl(GPIO_V2_GET_LINE_IOCTL)`로 라인 fd를 받는다.

"char 디바이스입니다"라는 말은 이 차이를 하나도 설명하지 못한다. 그래서 실무 질문은 **"어느 서브시스템(프레임워크)에 붙였나"** 이다. 프레임워크는 세 가지를 준다.

1. **표준 유저 ABI** — 노드 이름, ioctl, sysfs 속성 이름, 이벤트 포맷이 정해져 있다. 유저 툴(evtest, hwclock, libiio, v4l2-ctl, candump)이 모든 칩에서 그대로 동작한다.
2. **공통 코드** — 버퍼링, 타임스탬프, 트리거, poll, 권한, 다중 open, 전원 관리. 드라이버는 칩 고유 부분(레지스터 읽기)만 쓴다.
3. **ops 구조체 계약** — 드라이버는 `struct iio_info`, `struct input_dev`, `struct rtc_class_ops`, `struct watchdog_ops`, `struct v4l2_ioctl_ops`, `struct can_priv` 같은 것을 채워 등록한다. 이 "ops 구조체를 채워 등록" 패턴이 커널 전체의 공통 설계다(I1).

펌웨어 비유: 고전 char 드라이버는 "UART 바이트를 읽고 쓰는 함수만 있는 HAL"이고, 프레임워크 드라이버는 "CMSIS-Driver나 Zephyr의 sensor API처럼 **장치 클래스별로 의미까지 표준화된** HAL"이다. Zephyr를 써 봤다면 `sensor_sample_fetch()`/`sensor_channel_get()`이 IIO의 `read_raw`와 거의 같은 발상이다.

### 7.2 프레임워크 한 장 정리

| 프레임워크 | 대상 장치 | 유저 인터페이스 | 드라이버가 등록하는 것 | 밑바닥 고전 타입 | 상세 노트 |
|---|---|---|---|---|---|
| tty / serial core | UART, USB-serial, 콘솔 | `/dev/ttyXXX`, termios | `uart_driver` + `uart_add_one_port()` | char | H8 |
| input | 버튼, 키, 터치, 조이스틱 | `/dev/input/eventN` (`struct input_event`) | `input_dev` + `input_register_device()` | char | I3 |
| IIO | IMU, ADC, DAC, 압력·광 센서 | `/sys/bus/iio/devices/iio:deviceN/`, `/dev/iio:deviceN` (버퍼) | `iio_dev` + `iio_info` + `iio_chan_spec` | char + sysfs | I2 |
| hwmon | 보드 온도·전압·팬 | `/sys/class/hwmon/hwmonN/temp1_input` | `hwmon_device_register_with_info()` | sysfs만 | I4 |
| thermal | thermal zone, cooling device | `/sys/class/thermal/` | `thermal_zone_device_register…` | sysfs만 | I4 |
| leds | LED, 트리거 | `/sys/class/leds/NAME/brightness` | `led_classdev` | sysfs만 | I9 |
| rtc | 실시간 시계 | `/dev/rtcN` + ioctl, sysfs | `rtc_device` + `rtc_class_ops` | char | I9 |
| watchdog | HW 워치독 | `/dev/watchdogN` | `watchdog_device` + `watchdog_ops` | char | I9 |
| GPIO (gpiolib) | GPIO 컨트롤러·확장기 | `/dev/gpiochipN` (libgpiod) | `gpio_chip` | char | H4 |
| I2C / SPI 코어 | 버스 컨트롤러, 클라이언트 | `/dev/i2c-N`, `/dev/spidevB.C` (선택) | `i2c_adapter` / `spi_controller`, 그리고 `i2c_driver` / `spi_driver` | char (선택) | H5, H7 |
| regulator | PMIC 출력, LDO | 거의 없음 (sysfs 읽기 전용) | `regulator_desc` + `regulator_ops` | 내부 전용 | H3 |
| clk (CCF) | PLL, 분주기, 게이트 | 없음 (debugfs만) | `clk_hw` + `clk_ops` | 내부 전용 | H3 |
| pinctrl | 핀 mux·설정 | 없음 (debugfs만) | `pinctrl_desc` | 내부 전용 | H4 |
| V4L2 / media | 카메라, ISP, 코덱 | `/dev/videoN`, `/dev/v4l-subdevN`, `/dev/mediaN` | `video_device`, `v4l2_subdev` | char | I5 |
| ALSA / ASoC | 오디오 코덱, I2S | `/dev/snd/pcmC0D0p` 등 | `snd_soc_component_driver` 등 | char | I7 |
| DRM / KMS | 디스플레이, GPU | `/dev/dri/cardN`, `renderD128` | `drm_driver` | char | I8 |
| MTD | raw NAND/NOR | `/dev/mtdN` (char), `/dev/mtdblockN` (block) | `mtd_info` | char + block | I11 |
| MMC | eMMC, SD 호스트 | `/dev/mmcblkN` | `mmc_host_ops` | block | I11 |
| netdev / SocketCAN | 이더넷, Wi-Fi, CAN | 인터페이스 + 소켓 | `net_device` (CAN은 `alloc_candev`) | net | I6, I10 |
| misc | 위 어디에도 안 맞는 작은 장치 | `/dev/NAME` (major 10) | `miscdevice` | char | G7 |

표를 읽는 법: 맨 오른쪽에서 두 번째 열(밑바닥 고전 타입)을 보면 **고전 3분류는 "프레임워크가 내부적으로 무엇을 쓰나"의 구현 세부**가 됐다. 드라이버 작성자가 고르는 것은 맨 왼쪽 열이다.

### 7.3 예제 9 — 랩 게스트에서 프레임워크가 만든 인터페이스 보기

무엇을 확인하나: 예제 4에서 찾은 RTC(pl031), 버튼(gpio-keys), GPIO(pl061), NOR 플래시가 **각 프레임워크의 표준 인터페이스**로 노출되어 있고, 같은 표준 툴로 읽히는지.

게스트 스크립트 `s7_fw.sh`:

```sh
echo "### rtc"; cat /sys/class/rtc/rtc0/name; hwclock -r -f /dev/rtc0; cat /sys/class/rtc/rtc0/since_epoch
echo "### input"; cat /proc/bus/input/devices
echo "### gpio"; cat /sys/kernel/debug/gpio | head -4
echo "### mtd"; cat /proc/mtd
echo "### misc"; cat /proc/misc
echo "### clk consumers (no /dev)"; ls --color=never /sys/kernel/debug/clk/clk24mhz/
```

```text
lab initramfs: 6.18.55 up 3.25s
### rtc
rtc-pl031 9010000.pl031
Sun Oct 11 01:35:44 2026  0.000000 seconds
1791682544
### input
I: Bus=0019 Vendor=0001 Product=0001 Version=0100
N: Name="gpio-keys"
P: Phys=gpio-keys/input0
S: Sysfs=/devices/platform/gpio-keys/input/input0
U: Uniq=
H: Handlers=kbd event0 
B: PROP=0
B: EV=3
B: KEY=10000000000000 0

### gpio
gpiochip0: 8 GPIOs, parent: amba/9030000.pl061, 9030000.pl061:
 gpio-3   (                    |GPIO Key Poweroff   ) in  lo IRQ 
### mtd
dev:    size   erasesize  name
mtd0: 08000000 00040000 "0.flash"
### misc
257 cpu_dma_latency
196 vfio
200 tun
237 loop-control
235 autofs
231 snapshot
183 hw_random
256 vga_arbiter
### clk consumers (no /dev)
clk_accuracy        clk_max_rate        clk_prepare_count
clk_duty_cycle      clk_min_rate        clk_protect_count
clk_enable_count    clk_notifier_count  clk_rate
clk_flags           clk_phase
[    4.421490] reboot: Power down
```

출력에서 볼 것:

- **rtc**: BusyBox `hwclock`은 pl031이라는 칩을 전혀 모른다. `/dev/rtc0`에 표준 `RTC_RD_TIME` ioctl을 쓸 뿐이다. 칩이 DS3231(I2C)로 바뀌어도 `hwclock`은 그대로다. 이것이 프레임워크의 가치다.
- **input**: `gpio-keys` 드라이버는 GPIO 3번 핀을 `KEY_POWER`(KEY 비트맵의 비트 116)로 보고하는 **범용 드라이버**다. DT만 바꾸면 어느 보드의 버튼이든 이 드라이버가 맡는다. `Handlers=kbd event0` — input 코어가 이 장치에 키보드 핸들러와 evdev 핸들러(= `/dev/input/event0`)를 붙였다.
- **gpio**: GPIO 3번이 `GPIO Key Poweroff`라는 소비자에게 IRQ 입력으로 잡혀 있다. gpio-keys(input 프레임워크)가 gpiolib(GPIO 프레임워크)의 **소비자**다 — 프레임워크끼리도 계층을 이룬다.
- **mtd**: 128 MiB(0x08000000) NOR, erase 블록 256 KiB(0x40000). block 장치처럼 섹터를 덮어쓸 수 없고 256 KiB 단위로 지워야 하는 장치라서 block이 아니라 MTD다(§1.3).
- **clk**: 클럭은 debugfs 파일만 있고 유저 ABI가 없다. 유저가 클럭을 직접 바꾸는 것은 의도된 사용법이 아니다.

### 7.4 예제 10 — 실제 IMU 드라이버는 어떻게 생겼나 (st_lsm6dsx)

무엇을 확인하나: 센서 키트의 IMU로 흔히 쓰는 ST LSM6DSx 계열의 메인라인 드라이버가 **IIO 코어 + I2C 글루 + SPI 글루** 세 모듈로 나뉘어 있는지, 랩 커널 빌드 산출물로 확인한다. (VM 호스트에서 `.ko` 파일만 조사, 게스트 로드 없음.)

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/build/linux/drivers/iio/imu/st_lsm6dsx && ls *.ko && for m in st_lsm6dsx st_lsm6dsx_i2c st_lsm6dsx_spi; do echo "== $m"; modinfo -F depends $m.ko; modinfo -F alias $m.ko | head -2; done'
```

```text
st_lsm6dsx.ko
st_lsm6dsx_i2c.ko
st_lsm6dsx_spi.ko
== st_lsm6dsx
industrialio-triggered-buffer,kfifo_buf
== st_lsm6dsx_i2c
st_lsm6dsx
of:N*T*Cst,asm330lhhxg1C*
of:N*T*Cst,asm330lhhxg1
== st_lsm6dsx_spi
st_lsm6dsx
of:N*T*Cst,asm330lhhxg1C*
of:N*T*Cst,asm330lhhxg1
```

I2C 글루의 `probe`(랩 소스 `drivers/iio/imu/st_lsm6dsx/st_lsm6dsx_i2c.c` 그대로 — 24–42행, 178–190행):

```c
static int st_lsm6dsx_i2c_probe(struct i2c_client *client)
{
	int hw_id;
	struct regmap *regmap;

	hw_id = (kernel_ulong_t)device_get_match_data(&client->dev);
	if (!hw_id)
		hw_id = i2c_client_get_device_id(client)->driver_data;
	if (!hw_id)
		return -EINVAL;

	regmap = devm_regmap_init_i2c(client, &st_lsm6dsx_i2c_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to register i2c regmap %ld\n", PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return st_lsm6dsx_probe(&client->dev, client->irq, hw_id, regmap);
}
MODULE_DEVICE_TABLE(i2c, st_lsm6dsx_i2c_id_table);

static struct i2c_driver st_lsm6dsx_driver = {
	.driver = {
		.name = "st_lsm6dsx_i2c",
		.pm = pm_sleep_ptr(&st_lsm6dsx_pm_ops),
		.of_match_table = st_lsm6dsx_i2c_of_match,
		.acpi_match_table = st_lsm6dsx_i2c_acpi_match,
	},
	.probe = st_lsm6dsx_i2c_probe,
	.id_table = st_lsm6dsx_i2c_id_table,
};
module_i2c_driver(st_lsm6dsx_driver);
```

출력에서 볼 것:

- 코어 모듈 `st_lsm6dsx`는 IIO의 `industrialio-triggered-buffer`와 `kfifo_buf`에 의존한다 — **IIO 프레임워크 드라이버**다. 소스에서 `devm_iio_device_alloc()`과 `devm_iio_device_register()`를 부른다(`st_lsm6dsx_core.c` 2397행, 2737행).
- `_i2c`와 `_spi` 모듈은 각각 **I2C 클라이언트 드라이버**(`module_i2c_driver`)와 **SPI 클라이언트 드라이버**(`module_spi_driver`)다. 하는 일은 `regmap`을 만들어 공통 `st_lsm6dsx_probe()`에 넘기는 것뿐이다. 칩 로직은 regmap 위에서 버스를 모른 채 동작한다(H2).
- 그러니 "IMU 드라이버는 무슨 종류인가?"의 정답은 "**I2C/SPI 클라이언트 드라이버이면서 IIO 프레임워크 드라이버**"다. char도 아니고 하나의 단어도 아니다. 이 노트 전체의 running example인 **sensorkit IMU**(가상의 I2C/SPI IMU)도 똑같이 설계한다: `sensorkit_core.c`(IIO) + `sensorkit_i2c.c` + `sensorkit_spi.c`.

---

## 8. 두 번째 축 — 버스/컨트롤러 드라이버 vs 클라이언트 드라이버, 그리고 커널 내부 전용

### 8.1 컨트롤러 드라이버와 클라이언트 드라이버

I2C를 예로 들면 드라이버가 **두 종류** 있다.

- **버스 컨트롤러(adapter) 드라이버**: SoC 안의 I2C 컨트롤러 IP(예: i.MX의 LPI2C, TI의 OMAP I2C)를 구동한다. 레지스터에 START/주소/데이터를 쓰고 인터럽트를 처리한다. 보통 **platform 드라이버**로 DT의 `i2c@30a20000` 노드에 붙고, `i2c_add_adapter()`로 `i2c_adapter`(`i2c-2` 같은 버스)를 등록한다. 유저 인터페이스는 선택 사항(`i2c-dev` 모듈을 넣으면 `/dev/i2c-2`)이다.
- **클라이언트(장치) 드라이버**: 그 버스 위의 칩 하나(IMU, PMIC, EEPROM)를 구동한다. `i2c_driver`로 등록하고, 칩과 통신할 때는 `i2c_transfer()`/regmap으로 **컨트롤러 드라이버에게 부탁**한다. 칩의 기능에 맞는 프레임워크(IIO, regulator, nvmem …)로 유저에게 노출된다.

Don의 경험으로 번역하면: 컨트롤러 드라이버는 "I2C 마스터 IP의 HAL", 클라이언트 드라이버는 "그 HAL 위에서 센서 레지스터를 읽는 센서 드라이버"다. Linux는 이 둘을 **다른 사람이 쓰고 다른 디렉터리에 둔다**(`drivers/i2c/busses/` vs `drivers/iio/imu/`). SoC 벤더가 컨트롤러 드라이버를, 센서 벤더나 보드 팀이 클라이언트 드라이버를 쓴다. 센서 키트 보드 bring-up에서 Don이 쓰게 될 것은 대부분 **클라이언트 드라이버**다.

같은 구조가 모든 버스에 있다: SPI(`spi_controller` vs `spi_driver`), USB(호스트 컨트롤러 xHCI vs `usb_driver`), PCI(호스트 브리지 vs `pci_driver`), MDIO(MAC의 MDIO 버스 vs PHY 드라이버 — 예제 2의 `mdio_bus drivers=109`), virtio(virtio-mmio vs virtio_blk/virtio_net — 예제 4).

```svg
<svg viewBox="0 0 680 450" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <g font-size="13" font-weight="bold" text-anchor="middle"><text x="150" y="22">장치 (struct device 계열)</text><text x="500" y="22">드라이버</text></g>
  <g font-size="12" text-anchor="middle">
    <rect x="20" y="36" width="260" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="150" y="53">platform_device 30a20000.i2c</text><text x="150" y="69">(DT 노드에서 생성, D3)</text>
    <rect x="380" y="36" width="280" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="520" y="53">i2c-imx (platform_driver)</text><text x="520" y="69">버스/컨트롤러 드라이버</text>
    <rect x="20" y="116" width="260" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="150" y="133">i2c_adapter "i2c-2"</text><text x="150" y="149">(컨트롤러 드라이버가 등록)</text>
    <rect x="20" y="196" width="260" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
    <text x="150" y="213">i2c_client 2-0068</text><text x="150" y="229">(DT 자식 노드 imu@68)</text>
    <rect x="380" y="196" width="280" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
    <text x="520" y="213">sensorkit_imu (i2c_driver)</text><text x="520" y="229">클라이언트(장치) 드라이버</text>
    <rect x="380" y="276" width="280" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
    <text x="520" y="293">IIO 코어</text><text x="520" y="309">devm_iio_device_register()</text>
    <rect x="380" y="356" width="280" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
    <text x="520" y="373">/sys/bus/iio/devices/iio:device0</text><text x="520" y="389">/dev/iio:device0 (버퍼 읽기)</text>
    <rect x="20" y="296" width="260" height="100" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
    <text x="150" y="318" font-weight="bold">커널 내부 공급자 (유저 노출 없음)</text>
    <text x="150" y="340">clk: clk_prepare_enable()</text>
    <text x="150" y="360">regulator: regulator_enable()</text>
    <text x="150" y="380">pinctrl: 기본 state 자동 적용</text>
  </g>
  <g stroke="currentColor" marker-end="url(#ah4)">
    <line x1="378" y1="56" x2="282" y2="56"/>
    <line x1="420" y1="76" x2="230" y2="114"/>
    <line x1="150" y1="156" x2="150" y2="194"/>
    <line x1="378" y1="216" x2="282" y2="216"/>
    <line x1="520" y1="236" x2="520" y2="274"/>
    <line x1="520" y1="316" x2="520" y2="354"/>
    <line x1="282" y1="320" x2="378" y2="240"/>
  </g>
  <g font-size="12">
    <text x="300" y="50">probe</text>
    <text x="226" y="100" text-anchor="end">i2c_add_adapter</text>
    <text x="160" y="180">자식 노드 열거</text>
    <text x="300" y="210">probe</text>
    <text x="530" y="260">iio_dev 등록</text>
    <text x="530" y="340">IIO가 생성</text>
  </g>
  <text x="340" y="428" text-anchor="middle" font-size="12">유저가 보는 것은 맨 아래 IIO 인터페이스뿐. 컨트롤러·공급자 드라이버는 그 밑에서 일한다.</text>
</svg>
```
그림 6 — 가상의 sensorkit 보드에서 I2C IMU 하나가 유저에게 보이기까지 관여하는 드라이버들. 주황 = 드라이버, 파랑 = 장치 객체, 초록 = 프레임워크, 빨강 = 커널 내부 공급자(클라이언트 드라이버뿐 아니라 컨트롤러 드라이버도 clk·pinctrl을 소비한다 — 화살표는 하나만 그렸다). 장치와 드라이버의 짝짓기(probe)는 D3·G2, 컨트롤러가 자식 노드를 `i2c_client`로 만드는 과정은 H5에서 다룬다.

### 8.2 커널 내부 전용 드라이버 — 유저에게 아무것도 안 보이는 드라이버

예제 4에서 본 클럭 `clk24mhz`처럼, 어떤 드라이버는 **다른 드라이버에게만** 서비스를 제공한다.

| 공급자 프레임워크 | 공급자 드라이버 예 | 소비자 API (다른 드라이버의 probe에서) | 유저가 볼 수 있는 것 |
|---|---|---|---|
| clk (common clock framework) | SoC CCM 드라이버, fixed-clock | `devm_clk_get_enabled()`, `clk_set_rate()` | debugfs `clk_summary`만 |
| regulator | PMIC 드라이버(PCA9450 등), fixed-regulator | `devm_regulator_get()`, `regulator_enable()` | `/sys/class/regulator` 읽기 전용 |
| pinctrl | SoC IOMUX 드라이버 | 대개 자동(`pinctrl-0` state), `pinctrl_select_state()` | debugfs `pinctrl/`만 |
| reset | SoC 리셋 컨트롤러 | `devm_reset_control_get()` | 없음 |
| irqchip | GIC, GPIO 컨트롤러의 IRQ 부분 | `platform_get_irq()` → `request_irq()` | `/proc/interrupts` |
| dmaengine | SoC DMA 컨트롤러 | `dma_request_chan()` | 없음 (debugfs) |
| PHY (generic PHY) | USB/PCIe/MIPI PHY | `devm_phy_get()`, `phy_power_on()` | 없음 |

bring-up에서 "센서가 응답을 안 한다"의 원인이 이 칸들에 몰려 있다 — 클럭이 안 켜졌거나, 레귤레이터가 꺼져 있거나, 핀 mux가 틀렸다(H3, H4). 이 드라이버들이 유저 인터페이스가 없다는 것은 **디버깅도 debugfs와 dmesg로만 한다**는 뜻이다. 그리고 이 공급자 드라이버가 아직 probe 되지 않았으면 소비자 드라이버는 `-EPROBE_DEFER`로 미뤄진다(D3 §10, G3).

---

## 9. 커널 소스 트리 측정 — 드라이버는 어디에 몰려 있나

### 9.1 예제 11 — `drivers/` 하위 디렉터리별 `.c` 파일 수와 랩 커널이 실제로 빌드한 `.o` 수

무엇을 확인하나: 커널 드라이버 코드가 고전 3분류 중 어디에 많은지, 그리고 그보다 프레임워크 디렉터리별로 어떻게 나뉘어 있는지. (VM 호스트에서 소스 트리와 빌드 트리를 센다.)

```sh
limactl shell elinux -- bash -lc 'S=/var/lab/src/linux-6.18.55/drivers; B=/var/lab/out/linux-build/drivers; echo "total src .c: $(find $S -name "*.c" | wc -l)  built .o: $(find $B -name "*.o" ! -name "*.mod.o" | wc -l)"; for d in net gpu media staging scsi iio clk pinctrl input usb soc hwmon power mtd tty regulator i2c spi rtc gpio leds watchdog char block thermal base; do printf "%-10s src_c=%5d built_o=%4d\n" $d $(find $S/$d -name "*.c" | wc -l) $(find $B/$d -name "*.o" ! -name "*.mod.o" 2>/dev/null | wc -l); done; echo; for d in $S/*/; do echo "$(find $d -name "*.c" | wc -l) $(basename $d)"; done | sort -rn | head -12; ls -d $S/*/ | wc -l'
```

```text
total src .c: 21695  built .o: 6603
net        src_c= 3506 built_o= 994
gpu        src_c= 3558 built_o=1435
media      src_c= 1605 built_o= 494
staging    src_c=  424 built_o=  23
scsi       src_c=  423 built_o=  48
iio        src_c=  652 built_o=  51
clk        src_c= 1090 built_o= 586
pinctrl    src_c=  464 built_o= 187
input      src_c=  418 built_o=  54
usb        src_c=  556 built_o= 252
soc        src_c=  176 built_o=  97
hwmon      src_c=  303 built_o=  14
power      src_c=  168 built_o=  28
mtd        src_c=  259 built_o=  99
tty        src_c=  188 built_o=  73
regulator  src_c=  212 built_o=  49
i2c        src_c=  172 built_o=  43
spi        src_c=  163 built_o=  41
rtc        src_c=  187 built_o=  47
gpio       src_c=  206 built_o=  50
leds       src_c=  144 built_o=  18
watchdog   src_c=  184 built_o=  28
char       src_c=  177 built_o=  66
block      src_c=   58 built_o=   4
thermal    src_c=  111 built_o=  48
base       src_c=   85 built_o=  64

3558 gpu
3506 net
1605 media
1090 clk
652 iio
556 usb
464 pinctrl
424 staging
423 scsi
418 input
362 video
344 infiniband
143
```

(`.o` 수에는 내장 객체와 모듈 객체가 섞여 있고, 하나의 드라이버가 여러 `.c`로 나뉘기도 하므로 "드라이버 개수"가 아니라 "코드 규모"의 대략적 지표다. 마지막 `143`은 `drivers/` 바로 아래 디렉터리 수다.)

```svg
<svg viewBox="0 0 660 548" xmlns="http://www.w3.org/2000/svg">
  <text x="330" y="20" text-anchor="middle" font-size="14" font-weight="bold">drivers/ 하위 디렉터리별 크기 (Linux 6.18.55)</text>
  <rect x="110" y="30" width="12" height="12" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <text x="128" y="41" font-size="12">소스 트리의 .c 파일 수</text>
  <rect x="310" y="30" width="12" height="12" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="328" y="41" font-size="12">랩 커널 빌드에서 만들어진 .o 수</text>
  <text x="102" y="69" text-anchor="end" font-size="12">gpu</text>
  <rect x="110" y="56" width="440" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="66" width="177" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="556" y="70" font-size="12">3558 / 1435</text>
  <text x="102" y="95" text-anchor="end" font-size="12">net</text>
  <rect x="110" y="82" width="434" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="92" width="123" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="550" y="96" font-size="12">3506 / 994</text>
  <text x="102" y="121" text-anchor="end" font-size="12">media</text>
  <rect x="110" y="108" width="198" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="118" width="61" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="314" y="122" font-size="12">1605 / 494</text>
  <text x="102" y="147" text-anchor="end" font-size="12">clk</text>
  <rect x="110" y="134" width="135" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="144" width="72" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="251" y="148" font-size="12">1090 / 586</text>
  <text x="102" y="173" text-anchor="end" font-size="12">iio</text>
  <rect x="110" y="160" width="81" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="170" width="6" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="197" y="174" font-size="12">652 / 51</text>
  <text x="102" y="199" text-anchor="end" font-size="12">usb</text>
  <rect x="110" y="186" width="69" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="196" width="31" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="185" y="200" font-size="12">556 / 252</text>
  <text x="102" y="225" text-anchor="end" font-size="12">pinctrl</text>
  <rect x="110" y="212" width="57" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="222" width="23" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="173" y="226" font-size="12">464 / 187</text>
  <text x="102" y="251" text-anchor="end" font-size="12">input</text>
  <rect x="110" y="238" width="52" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="248" width="7" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="168" y="252" font-size="12">418 / 54</text>
  <text x="102" y="277" text-anchor="end" font-size="12">hwmon</text>
  <rect x="110" y="264" width="37" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="274" width="2" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="153" y="278" font-size="12">303 / 14</text>
  <text x="102" y="303" text-anchor="end" font-size="12">mtd</text>
  <rect x="110" y="290" width="32" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="300" width="12" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="148" y="304" font-size="12">259 / 99</text>
  <text x="102" y="329" text-anchor="end" font-size="12">regulator</text>
  <rect x="110" y="316" width="26" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="326" width="6" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="142" y="330" font-size="12">212 / 49</text>
  <text x="102" y="355" text-anchor="end" font-size="12">gpio</text>
  <rect x="110" y="342" width="25" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="352" width="6" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="141" y="356" font-size="12">206 / 50</text>
  <text x="102" y="381" text-anchor="end" font-size="12">tty</text>
  <rect x="110" y="368" width="23" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="378" width="9" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="139" y="382" font-size="12">188 / 73</text>
  <text x="102" y="407" text-anchor="end" font-size="12">rtc</text>
  <rect x="110" y="394" width="23" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="404" width="6" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="139" y="408" font-size="12">187 / 47</text>
  <text x="102" y="433" text-anchor="end" font-size="12">char</text>
  <rect x="110" y="420" width="22" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="430" width="8" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="138" y="434" font-size="12">177 / 66</text>
  <text x="102" y="459" text-anchor="end" font-size="12">i2c</text>
  <rect x="110" y="446" width="21" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="456" width="5" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="137" y="460" font-size="12">172 / 43</text>
  <text x="102" y="485" text-anchor="end" font-size="12">spi</text>
  <rect x="110" y="472" width="20" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="482" width="5" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="136" y="486" font-size="12">163 / 41</text>
  <text x="102" y="511" text-anchor="end" font-size="12">block</text>
  <rect x="110" y="498" width="7" height="10" fill="#888" fill-opacity="0.5" stroke="#888"/>
  <rect x="110" y="508" width="1" height="10" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="123" y="512" font-size="12">58 / 4</text>
  <line x1="110" y1="50" x2="110" y2="522" stroke="currentColor"/>
  <text x="20" y="540" font-size="12">값 = 소스 .c / 빌드 .o (예제 11). 막대 길이는 소스 .c 최댓값(gpu 3558) 기준 선형</text>
</svg>
```
그림 7 — 예제 11의 측정값. 고전 분류 이름 그대로인 `drivers/char/`(177)와 `drivers/block/`(58)은 작다. 코드는 **프레임워크별 디렉터리**(gpu=DRM, media=V4L2, iio, clk, pinctrl, input, hwmon …)에 몰려 있다. "char 드라이버"는 `drivers/char/`에 있는 것이 아니라 그 프레임워크들 안에 흩어져 있다.

출력에서 볼 것:

- `drivers/block/`은 58개 `.c`뿐이다. 실제 스토리지 드라이버는 `drivers/nvme/`, `drivers/scsi/`, `drivers/mmc/`, `drivers/mtd/`에 있고, 그것들도 **각자의 서브시스템(NVMe 코어, SCSI 미드레이어, MMC 코어)** 에 붙어서 block 장치를 만들어 받는다. "block 드라이버를 직접 쓰는" 일은 드물다.
- `drivers/net/`(3506)은 거대하지만 대부분 이더넷·Wi-Fi·CAN 등 **NIC 하드웨어별** 드라이버다. 즉 net은 고전 분류 중 유일하게 "그 자체가 하나의 큰 프레임워크"다.
- 랩 커널(arm64 defconfig 기반)은 **clk 586, pinctrl 187** 개의 `.o`를 빌드했다 — 유저에게 아무것도 안 보이는 드라이버가 SoC 커널에서 큰 비중이라는 뜻이다(여러 SoC 벤더의 클럭 트리를 다 넣었기 때문). 반면 hwmon(14)·iio(51)는 대부분 모듈로도 안 켜져 있다. 보드 커널을 만들 때는 그 보드의 센서 드라이버만 켠다(K 모듈).

---

## 10. "내 장치는 어느 프레임워크?" — 결정표

### 10.1 결정 흐름

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <g font-size="12">
    <rect x="10" y="10" width="290" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="155" y="35" text-anchor="middle">패킷을 주고받는 통신 장치인가?</text>
    <rect x="10" y="70" width="290" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="155" y="95" text-anchor="middle">데이터를 저장하는 장치인가?</text>
    <rect x="10" y="130" width="290" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="155" y="155" text-anchor="middle">물리량을 측정하는 센서인가?</text>
    <rect x="10" y="190" width="290" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="155" y="215" text-anchor="middle">사람의 입력 이벤트인가?</text>
    <rect x="10" y="250" width="290" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="155" y="275" text-anchor="middle">영상·오디오·디스플레이인가?</text>
    <rect x="10" y="310" width="290" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="155" y="335" text-anchor="middle">작은 표준 기능인가?</text>
    <rect x="10" y="370" width="290" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="155" y="395" text-anchor="middle">다른 드라이버에 자원을 주는가?</text>
    <rect x="370" y="10" width="300" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="520" y="27" text-anchor="middle">netdev: Ethernet, Wi-Fi</text><text x="520" y="43" text-anchor="middle">CAN → SocketCAN (alloc_candev)</text>
    <rect x="370" y="70" width="300" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="520" y="87" text-anchor="middle">raw NAND/NOR → MTD</text><text x="520" y="103" text-anchor="middle">eMMC/SD → MMC, NVMe → nvme (block)</text>
    <rect x="370" y="130" width="300" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="520" y="147" text-anchor="middle">IMU·ADC·압력·광 → IIO</text><text x="520" y="163" text-anchor="middle">보드 헬스 온도·전압·팬 → hwmon</text>
    <rect x="370" y="190" width="300" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="520" y="215" text-anchor="middle">버튼·터치·조이스틱 → input</text>
    <rect x="370" y="250" width="300" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="520" y="267" text-anchor="middle">카메라 → V4L2 (+media), 오디오 → ALSA</text><text x="520" y="283" text-anchor="middle">디스플레이·GPU → DRM/KMS</text>
    <rect x="370" y="310" width="300" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="520" y="327" text-anchor="middle">LED → leds, RTC → rtc, WDT → watchdog</text><text x="520" y="343" text-anchor="middle">GPIO → gpiolib, PWM → pwm</text>
    <rect x="370" y="370" width="300" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="520" y="387" text-anchor="middle">clk, regulator, pinctrl, reset</text><text x="520" y="403" text-anchor="middle">irqchip, dmaengine (유저 노출 없음)</text>
    <rect x="10" y="426" width="660" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="340" y="448" text-anchor="middle">전부 아니오 → misc device + sysfs 속성 (G7, G8). 직접 cdev는 마지막 수단 (G4)</text>
  </g>
  <g stroke="currentColor" marker-end="url(#ah5)">
    <line x1="300" y1="30" x2="368" y2="30"/><line x1="300" y1="90" x2="368" y2="90"/><line x1="300" y1="150" x2="368" y2="150"/><line x1="300" y1="210" x2="368" y2="210"/><line x1="300" y1="270" x2="368" y2="270"/><line x1="300" y1="330" x2="368" y2="330"/><line x1="300" y1="390" x2="368" y2="390"/>
    <line x1="155" y1="50" x2="155" y2="68"/><line x1="155" y1="110" x2="155" y2="128"/><line x1="155" y1="170" x2="155" y2="188"/><line x1="155" y1="230" x2="155" y2="248"/><line x1="155" y1="290" x2="155" y2="308"/><line x1="155" y1="350" x2="155" y2="368"/><line x1="155" y1="410" x2="155" y2="424"/>
  </g>
  <g font-size="12"><text x="322" y="25">예</text><text x="322" y="85">예</text><text x="322" y="145">예</text><text x="322" y="205">예</text><text x="322" y="265">예</text><text x="322" y="325">예</text><text x="322" y="385">예</text></g>
</svg>
```
그림 8 — 장치 → 프레임워크 결정 흐름. 오른쪽 "예" 화살표, 아래쪽은 "아니오". 실제로는 한 칩이 여러 칸에 걸치기도 한다(PMIC = regulator + rtc + watchdog + power button input, MFD로 쪼갬).

### 10.2 센서 키트 장치별 정답표

| 장치 (센서 키트) | 버스 | 프레임워크 | 유저가 보는 것 | 왜 char 직접 구현이 아닌가 |
|---|---|---|---|---|
| IMU (sensorkit, 6축) | I2C 또는 SPI 클라이언트 | IIO (triggered buffer) | `iio:deviceN/in_accel_x_raw`, `/dev/iio:deviceN` 버퍼 | 타임스탬프·FIFO·트리거·스케일을 IIO가 표준화, ROS/libiio가 바로 읽음 |
| 기압·온습도 센서 | I2C | IIO | `in_pressure_input` 등 | 같은 이유 |
| 보드 온도·전압 모니터 | I2C | hwmon (+thermal) | `/sys/class/hwmon/hwmonN/temp1_input` | `sensors`(lm-sensors)·thermal 정책과 연동 |
| 비상정지 버튼, 모드 스위치 | GPIO | input (`gpio-keys`, DT만) | `/dev/input/eventN` | 드라이버를 쓸 필요조차 없다 — DT 노드만 추가 |
| 상태 LED | GPIO, PWM | leds (`gpio-leds`, `leds-pwm`) | `/sys/class/leds/NAME/brightness`, trigger | heartbeat·netdev 트리거 공짜 |
| 카메라 센서 | MIPI CSI-2 + I2C 제어 | V4L2 subdev + media controller | `/dev/videoN`, `/dev/v4l-subdevN` | GStreamer·libcamera가 V4L2만 안다 |
| GNSS 수신기 | UART | tty (serial core), 선택적으로 GNSS 서브시스템 | `/dev/ttymxcN` 또는 `/dev/gnss0` | gpsd가 tty를 읽음 |
| CAN 모터 컨트롤러 | CAN 컨트롤러 (SoC 내장 또는 SPI MCP251x) | netdev / SocketCAN | `can0`, `candump` | 소켓 API·필터·에러 상태 머신이 표준 |
| LiDAR | 이더넷 (UDP) | netdev (NIC 드라이버는 이미 있음) | `eth1` + UDP 소켓 | 장치 드라이버 없이 유저 공간에서 UDP 파싱 |
| 로그 저장 eMMC | SDHCI 컨트롤러 | MMC → block | `/dev/mmcblk0p3` + ext4 | 파일시스템·page cache를 공짜로 |
| 부트 SPI NOR | SPI 컨트롤러 | MTD (spi-nor) | `/dev/mtdN`, UBI/UBIFS | erase 블록·배드 블록 처리 |
| PMIC | I2C | MFD → regulator + rtc + watchdog + input | 거의 내부 전용 | 한 칩을 기능별 프레임워크로 쪼갬 |
| FPGA 커스텀 가속기 | PCIe 또는 AXI(platform) | 맞는 게 없으면 misc + sysfs, 또는 UIO/VFIO | `/dev/NAME` | 표준 프레임워크가 없는 경우에만 직접 구현 |

---

## 11. 커널 안에서는 무슨 일이 일어나나

### 11.1 세 종류의 핵심 구조체

| 구분 | 장치를 나타내는 구조체 | 드라이버가 채우는 ops | 데이터 단위 | 등록 → 유저에게 보이게 되는 함수 | 6.18 소스 위치 |
|---|---|---|---|---|---|
| char | `struct cdev` (+ 클래스 장치 `struct device`) | `struct file_operations` | 유저 버퍼 (`char __user *`) | `cdev_add()` → `chrdev_open()`이 `i_rdev`로 찾음, `device_create()` → devtmpfs 노드 | fs/char_dev.c |
| block | `struct gendisk` (+ `struct request_queue`) | `struct blk_mq_ops` (+ `struct block_device_operations`) | `struct request` → `struct bio` → `struct bio_vec` | `add_disk()` → `device_add_disk()` → 파티션 스캔, devtmpfs 노드 | block/genhd.c, block/blk-mq.c |
| net | `struct net_device` | `struct net_device_ops` (+ `ethtool_ops`) | `struct sk_buff` | `register_netdev()` → `register_netdevice()` → `/sys/class/net`, rtnetlink 알림 | net/core/dev.c |

공통점: 세 경우 모두 결국 **`struct device`가 하나 생겨 sysfs에 나타난다**(`/sys/class/g1_class/g1_chr`, `/sys/block/g1_blk0`, `/sys/class/net/g1_net0`). 이 `struct device`가 G2(디바이스 모델)의 주인공이다. 실제 하드웨어 드라이버에서는 이 장치의 **부모**가 probe된 하드웨어 장치(`&pdev->dev`, `&client->dev`)가 되어, 예제 4처럼 sysfs 트리에서 하드웨어 밑에 매달린다.

### 11.2 예제 8의 콜 스택을 커널 함수로 다시 읽기

- **char**: `__arm64_sys_read` → `ksys_read` → `vfs_read` → `file->f_op->read`. `f_op`는 open 때 `chrdev_open()`이 `cdev->ops`로 바꿔 끼운 것이다(F12 §4). 그래서 read 경로에는 char 전용 코드가 없다.
- **block**: `blkdev_fsync` → `file_write_and_wait_range` → … `blkdev_writepages` → (`submit_bio` → `blk_mq_submit_bio`가 bio를 request로 만들어 plug에 쌓음) → `blk_finish_plug` → `blk_mq_flush_plug_list` → `blk_mq_run_hw_queue` → `blk_mq_sched_dispatch_requests`(mq-deadline에서 꺼냄) → `blk_mq_dispatch_rq_list` → `q->mq_ops->queue_rq`. 괄호 안 단계는 콜 스택에는 안 보인다 — bio 제출은 **plug 이전에 이미 끝나서 스택에서 빠졌다**. 한 번에 보이지 않는 단계는 ftrace `function_graph`(M 모듈)로 본다.
- **net**: `raw_sendmsg` → `ip_push_pending_frames` → `ip_output` → `ip_finish_output2` → `neigh_resolve_output`(L2 헤더 붙임) → `__dev_queue_xmit`(qdisc에 enqueue) → `sch_direct_xmit` → `dev_hard_start_xmit` → `ops->ndo_start_xmit`. 수신은 반대로 IRQ → NAPI `poll` → `netif_receive_skb` → `ip_rcv` → 소켓 수신 큐 → 유저 `recvfrom`(I6).

### 11.3 드라이버를 쓸 때 다시 만나는 지점

- **G2 디바이스 모델**: 예제 4에서 본 `subsystem`/`driver`/`device` 링크와 class·bus의 차이.
- **G3 platform driver**: 실제 드라이버는 이 노트의 `module_init` 코드를 `probe()` 안으로 옮기고 `devm_*`로 정리한다.
- **G4 문자 디바이스, G5 ioctl/mmap, G6 poll, G7 misc, G8 sysfs**: §3의 char 드라이버를 깊게.
- **I1 프레임워크 원칙, I2 IIO, I3 input, I4 hwmon, I5 V4L2, I9 작은 프레임워크들**: §7의 표 각 줄.
- **I6 네트워크 드라이버, I10 CAN**: §5의 dummy NIC에 RX·NAPI·PHY를 더한 것.
- **I11 MTD·MMC·block, J 스토리지**: §4의 blk-mq와 그 위 파일시스템.
- **H2 regmap, H5 I2C, H7 SPI**: §7.4의 IMU 글루 코드와 §8의 컨트롤러/클라이언트 구분.

---

## 12. Embedded 실무에서 다시 보기

### 12.1 장면 1 — "IMU용 char 드라이버를 만들어 주세요"라는 요청

센서 키트 프로젝트에서 애플리케이션 팀이 "IMU 값을 `/dev/imu`에서 `read()`하면 구조체로 나오게 해 달라"고 요청한다. 쉽게 할 수 있다(§3의 코드에 I2C 읽기만 붙이면 된다). 하지만 그러면 (1) 타임스탬프·FIFO 워터마크·샘플링 주파수 설정을 전부 직접 설계해야 하고, (2) ioctl ABI를 영원히 유지해야 하고, (3) 메인라인에 올릴 수 없고, (4) 다음 보드에서 IMU가 바뀌면 애플리케이션도 바뀐다. 시니어 답은 **"IIO 드라이버로 쓰고, 앱은 libiio 또는 IIO 버퍼(`/dev/iio:deviceN`)를 읽게 하자"** 다. 메인라인에 이미 드라이버가 있는지부터 확인한다(`drivers/iio/imu/` — 예제 10의 st_lsm6dsx처럼 대부분의 상용 IMU가 이미 있다). 그러면 할 일은 **DT 노드 몇 줄**로 줄어든다.

### 12.2 장면 2 — 새 보드에서 "어느 드라이버가 이 노드를 쓰고 있나"

현장에서 `/dev/ttymxc2`가 열리지 않는다. 예제 3·4의 방법으로 노드 → 클래스 장치 → serial-base `port`/`ctrl` → `imx-uart` platform 장치까지 따라가면, 어느 계층에서 끊겼는지(드라이버 미바인딩, 클럭 defer) 바로 나온다.

```sh
M=$(stat -c '%t:%T' /dev/ttymxc2)        # 16진수 major:minor
ls -l /sys/dev/char/                     # 10진수로 바꿔 해당 링크를 찾는다
readlink -f /sys/class/tty/ttymxc2/device
readlink /sys/class/tty/ttymxc2/device/driver
```

(위는 i.MX 보드를 가정한 진단 순서다. 이 랩에서 실행하지 않았다 — 랩의 같은 절차 출력은 예제 3·4.)

### 12.3 장면 3 — CAN 모터 컨트롤러는 net이다

로보틱스 면접에서 "CAN 드라이버는 char인가 net인가?"가 나온다. Linux에서 CAN은 **netdev**다(SocketCAN). `ip link set can0 type can bitrate 1000000`, `candump can0`. 이유는 §5.1과 같다 — 프레임이 장치 쪽에서 비동기로 오고, 여러 프로세스가 필터로 나눠 받는다. 예제 7의 `g1_net`을 `alloc_candev()`로 바꾸고 `ether_setup` 대신 CAN 설정을 쓰면 가상 CAN 장치 뼈대가 된다(I10, 커널의 `vcan` 드라이버가 정확히 이것이다).

### 12.4 장면 4 — 로그용 eMMC 파티션과 쓰기 증폭

센서 로그를 eMMC의 ext4에 512바이트씩 기록하면, 예제 6에서 본 것처럼 page cache가 4 KiB 단위로 읽고-고치고-쓰며, 그 아래 eMMC FTL이 또 erase 블록 단위로 쓴다. Don이 SSD에서 다루던 쓰기 증폭이 block 계층 위아래에서 두 번 일어난다. 로그는 버퍼에 모아 크게 쓰고, `fsync` 빈도를 정책으로 정한다(J 모듈).

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 센서마다 직접 char 드라이버 작성 | 앱이 칩마다 다른 ioctl을 씀, 리뷰에서 반려 | 프레임워크를 모름 | IIO/hwmon/input 등 맞는 프레임워크로 등록 (§10) |
| major 번호를 하드코딩 (`register_chrdev(240, ...)`) | 다른 드라이버와 충돌, `-EBUSY` | 동적 major를 안 씀 | `alloc_chrdev_region()` 또는 misc, 번호는 sysfs에서 읽기 (예제 5의 510) |
| `/dev/eth0`을 찾음 | "No such file" | net은 노드가 없음 | `/sys/class/net`, `ip link`, 소켓 사용 (§5) |
| raw NAND를 block으로 다룸 | 데이터 깨짐, 배드 블록 미처리 | erase 블록 장치를 섹터 장치로 오해 | MTD + UBI/UBIFS, 또는 FTL 있는 eMMC (§1.3) |
| `/sys/dev/char/M:m/device/driver`가 비어서 "드라이버 없음"으로 판단 | 엉뚱한 디버깅 | 클래스 장치 위에 클래스 장치가 겹침 | 부모를 따라 올라가며 `driver` 확인 (예제 4) |
| 옛 튜토리얼 block 코드 복붙 (`blk_init_queue`, `blk_mq_alloc_disk(&set, data)`) | 빌드 에러 | blk-mq·queue_limits API 변경 | 6.9+ `blk_mq_alloc_disk(&set, &lim, data)` (예제 6) |
| block 드라이버에서 `write()` 한 번 = 요청 한 번이라고 가정 | 요청 수·크기 예측 틀림 | page cache, RMW, plug 병합 | `/sys/block/X/stat`, blktrace로 실측 (예제 6) |
| `ndo_start_xmit`에서 skb를 해제하지 않음 / 두 번 해제 | 메모리 누수 또는 크래시 | 소유권 규칙 오해 | `NETDEV_TX_OK` 반환 시 드라이버가 skb 소유 → 해제 또는 완료 시 해제, `NETDEV_TX_BUSY`면 해제 금지 |
| clk/regulator 문제를 유저 공간에서 찾음 | 아무 흔적도 없음 | 내부 전용 드라이버는 유저 ABI가 없음 | debugfs `clk_summary`, `regulator_summary`, `devices_deferred` (§8.2) |

---

## 14. 면접에서 이렇게 말한다

**Q.** What types of device drivers are there in Linux?

**A.** 고전적으로는 char(바이트 스트림, `/dev` 노드, `file_operations`), block(고정 크기 블록 랜덤 액세스, blk-mq `queue_rq`, 위에 파일시스템), net(패킷, `/dev` 노드 없이 인터페이스와 소켓, `ndo_start_xmit`) 세 가지다. 하지만 실무 드라이버 대부분은 서브시스템 드라이버다 — IMU는 IIO, 버튼은 input, 온도는 hwmon, 카메라는 V4L2, CAN은 SocketCAN에 ops 구조체를 등록하고, 프레임워크가 표준 유저 인터페이스를 만든다. 그와 별개 축으로 버스 컨트롤러 드라이버와 그 위의 클라이언트 드라이버가 있고, clk·pinctrl·regulator처럼 유저에게 아무것도 노출하지 않는 공급자 드라이버도 있다.

> Classically there are three: character drivers, which expose a byte stream through a /dev node and implement file_operations; block drivers, which expose fixed-size, randomly addressable blocks, receive requests through blk-mq's queue_rq, and usually have a filesystem on top; and network drivers, which have no /dev node at all — they register a net_device, get sk_buffs through ndo_start_xmit, and user space talks to them via sockets. In practice, though, most drivers today are subsystem drivers: an IMU goes into IIO, a button into input, a temperature sensor into hwmon, a camera into V4L2, CAN into SocketCAN. You fill in the framework's ops structure and the framework provides the standard user ABI. Orthogonal to that, you distinguish bus controller drivers from the client drivers sitting on that bus, and there are provider drivers like clocks, pinctrl and regulators that expose nothing to user space at all.

**꼬리 질문**: "그럼 char 드라이버를 직접 쓰는 경우는?" → 맞는 프레임워크가 없는 커스텀 하드웨어(FPGA 가속기 제어 등)일 때, 그것도 보통 misc device + sysfs로 최소화한다.

**Q.** What's the difference between a character device and a block device?

**A.** char는 드라이버가 유저 버퍼를 직접 받는다 — `read()`가 VFS를 지나 바로 드라이버 `.read`로 온다(실측 중간 함수 2개). block은 page cache, bio, plug, I/O 스케줄러, blk-mq를 거쳐 드라이버에 `struct request`로 온다(실측 16개). 그래서 block은 캐싱·병합·재정렬을 커널이 해 주고, 512 B 쓰기도 4 KiB 페이지 단위 읽기-수정-쓰기로 바뀐다. 그리고 block 장치 위에는 파일시스템을 올릴 수 있다.

> With a character device, the driver sees the user's buffer more or less directly: read goes through vfs_read straight into the driver's read method. With a block device there's a whole layer in between — the page cache, bios, plugging, an I/O scheduler, and blk-mq — and the driver receives struct requests describing sector ranges. That layer does caching, merging and reordering for you, which also means a 512-byte write turns into page-sized read-modify-write I/O. And block devices are what you put filesystems on.

**Q.** Why doesn't a network interface have a /dev node?

**A.** 데이터가 장치 쪽에서 비동기로 먼저 도착하고, 커널이 그것을 프로토콜 스택으로 올려 어느 소켓의 것인지 정해야 하기 때문이다. 하나의 NIC를 수많은 연결이 공유하므로 "장치 파일을 연다"는 모델이 맞지 않는다. 그래서 유저는 소켓으로 데이터를, netlink(`ip`)로 설정을 다룬다.

> Because packets arrive asynchronously from the wire, and the kernel has to push them up through the protocol stack to figure out which socket they belong to; one interface is shared by many connections at once. A file-per-device model doesn't fit, so user space uses sockets for data and netlink — the ip command — for configuration. The interface is identified by name and ifindex, not by a major and minor number.

**Q.** If you had to write a driver for a new I2C IMU, what kind of driver would it be?

**A.** I2C 클라이언트 드라이버(`i2c_driver`, DT `compatible`로 매칭)이면서 IIO 드라이버다. probe에서 regmap을 만들고 chip ID를 확인한 뒤 `iio_dev`에 채널(accel x/y/z, gyro x/y/z)을 정의해 `devm_iio_device_register()`를 부른다. 데이터 스트리밍은 IIO triggered buffer와 데이터 레디 IRQ로 한다. SPI 변형도 있으면 메인라인 st_lsm6dsx처럼 core + i2c + spi 모듈로 나눈다. 먼저 메인라인에 드라이버가 있는지 확인한다.

> It would be an I2C client driver — an i2c_driver matched through the device tree compatible — that registers with the IIO subsystem. In probe I'd set up a regmap, check the chip ID, describe the channels — three accel and three gyro axes — in an iio_chan_spec array and call devm_iio_device_register. For streaming I'd use an IIO triggered buffer driven by the data-ready interrupt. If the part also has an SPI interface I'd split it like the mainline st_lsm6dsx driver: a core module plus thin I2C and SPI glue that just create the regmap. And before writing anything I'd check whether mainline already supports it.

**Q.** What's the difference between a bus controller driver and a client driver?

**A.** 컨트롤러 드라이버는 SoC의 I2C/SPI 컨트롤러 IP를 구동하고 `i2c_adapter`/`spi_controller`를 등록한다(보통 platform 드라이버). 클라이언트 드라이버는 그 버스 위의 칩 하나를 구동하며, 전송은 코어 API(`i2c_transfer`, `spi_sync`, regmap)로 컨트롤러에 부탁한다. 덕분에 같은 센서 드라이버가 어떤 SoC의 I2C 컨트롤러 위에서도 동작한다.

> The controller driver owns the SoC's I2C or SPI master block — registers, interrupts, DMA — and registers an i2c_adapter or spi_controller; it's usually a platform driver bound from the device tree. A client driver handles one chip on that bus and never touches the controller's registers; it asks the core to do transfers through i2c_transfer, spi_sync or regmap. That separation is why the same sensor driver works unchanged on any SoC's I2C controller.

**Q.** Can you name drivers that expose nothing to user space?

**A.** clk 공급자(CCF), pinctrl, regulator 공급자, reset 컨트롤러, irqchip, dmaengine, generic PHY. 이들은 다른 드라이버가 probe에서 `devm_clk_get`, `devm_regulator_get` 등으로 소비하는 자원을 제공한다. 디버깅은 debugfs(`clk_summary`, `regulator_summary`)와 `devices_deferred`로 한다.

> Clock providers in the common clock framework, pin controllers, regulator providers, reset controllers, interrupt controllers, DMA engines and generic PHYs. They exist to serve other drivers — consumers grab them in probe with devm_clk_get, devm_regulator_get and so on. You debug them through debugfs, for example clk_summary and regulator_summary, and by checking devices_deferred when a consumer is stuck waiting for its provider.

**Q.** On a running system, how do you find which driver backs /dev/foo?

**A.** `stat`으로 major:minor를 얻고 `/sys/dev/char/M:m`(또는 block)를 따라간다. `device/driver` 링크가 있으면 그것이 드라이버고, 없으면 클래스 장치가 겹친 것이므로 `readlink -f`로 실제 경로를 얻은 뒤 부모 디렉터리를 올라가며 `driver` 링크를 찾는다. `/proc/devices`의 major 이름도 힌트가 된다.

> Get the major and minor with stat, then follow /sys/dev/char/major:minor. If it has a device/driver link, that's your driver. If not, it's usually a class device stacked on another class device — input's event0 on top of input0, for example — so resolve the real path with readlink -f and walk up the parent directories until you find a driver link. The name next to the major in /proc/devices is another hint.

---

## 15. 직접 해보기

1. (개념) `/dev/null`, `/dev/sda`, `eth0`, `/dev/input/event0`, `/dev/iio:device0`, `/dev/mtd0`을 고전 3분류와 프레임워크로 각각 분류하라. — 정답/힌트: char(mem), block(SCSI disk), net, char(input), char+sysfs(IIO), char(MTD — mtdblock은 block).
2. (관찰) 예제 3의 `g1_walk`를 고쳐, `device/driver`가 없으면 `realpath`로 부모를 올라가며 처음 만나는 `driver`를 찍게 하라. 게스트에서 `event0` → `gpio-keys`, `ttyAMA0` → `port`가 나오는지 확인하라. — 정답/힌트: `realpath()`로 절대 경로를 얻고 `strrchr(path, '/')`로 잘라 가며 `<path>/driver`를 `readlink` 한다. `/sys/devices`에 닿으면 멈춘다.
3. (코드) `g1_blk`에 `stack` 대신 요청 크기 히스토그램(`blk_rq_bytes`)을 모아 rmmod 때 찍게 하고, `dd bs=512`, `bs=4096`, `bs=1M`(O_DIRECT 유무)로 쓸 때 드라이버가 받는 요청 크기를 비교하라. — 정답/힌트: BusyBox `dd`는 `oflag=direct`를 지원하지 않을 수 있다 — 그러면 정적 C 프로그램으로 `O_DIRECT` + 4 KiB 정렬 버퍼(`posix_memalign`)를 쓴다. 기대: 버퍼드 I/O는 페이지 묶음, O_DIRECT는 유저 크기에 가까움.
4. (코드) `g1_net`에 수신 경로를 흉내 내라: `g1_xmit`에서 받은 skb를 해제하지 말고 `skb->protocol = eth_type_trans(skb, dev)` 후 `netif_rx(skb)`로 스택에 되돌리고, `rx_packets`를 직접 세라. ping 패킷이 스택의 어디까지 올라가서 어디서 버려지는지 ftrace(M 모듈)나 `/proc/net/snmp` 카운터로 추적하라. — 정답/힌트: (이 노트에서 실행하지 않은 과제다.) 목적지 MAC은 `IFF_NOARP`일 때 neighbour가 채운 값, 목적지 IP 10.1.1.2는 로컬 주소가 아니다 — 두 값을 보고 `eth_type_trans`의 `pkt_type`과 `ip_rcv` 이후 경로를 예측한 뒤 확인한다. 진짜 쌍방향 가상 링크는 커널의 `veth`(`drivers/net/veth.c`의 `veth_xmit`)가 하는 일이다.
5. (관찰) 랩 게스트의 `/sys/class` 66개 중 장치가 0개인 클래스 다섯 개(예: hwmon, leds, watchdog, i2c-dev, thermal)를 골라, 각 클래스에 장치를 생기게 하려면 어떤 하드웨어/DT/모듈이 필요한지 적어라. — 정답/힌트: hwmon·leds는 DT에 센서/`gpio-leds` 노드, watchdog은 워치독 하드웨어(예: QEMU의 PCI 워치독 `i6300esb` + 해당 드라이버 Kconfig — 이 랩에서 확인하지 않음), i2c-dev는 I2C 컨트롤러 + `i2c-dev` 모듈.
6. (설계) 센서 키트에 "FPGA 기반 타임스탬프 카운터(MMIO 레지스터 4개, IRQ 1개)"가 추가됐다. 어느 프레임워크에 붙일지 후보(PTP clock, counter 서브시스템, misc + sysfs, UIO)를 비교해 하나를 고르고 이유를 써라. — 정답/힌트: 시간 동기화가 목적이면 PTP(`ptp_clock_register`, `/dev/ptpN` — 예제 2의 `248 ptp`), 이벤트 카운트면 counter 서브시스템, 빠른 프로토타입이면 UIO.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| char device | 문자 디바이스 | 바이트 스트림 장치. `/dev` 노드 + `file_operations` |
| block device | 블록 디바이스 | 고정 크기 블록 랜덤 액세스. page cache·blk-mq 경유, 위에 파일시스템 |
| network device | 네트워크 디바이스 | `struct net_device`. `/dev` 노드 없음, 소켓으로 사용 |
| major / minor | 장치 번호 | major = 드라이버(또는 프레임워크) 선택, minor = 그 안의 인스턴스 |
| subsystem / framework | 서브시스템 | 장치 클래스별 공통 코드 + 표준 유저 ABI (IIO, input, V4L2 …) |
| class | 클래스 | sysfs `/sys/class/X` — 유저 관점의 장치 종류 묶음 |
| bus | 버스 | sysfs `/sys/bus/X` — 장치와 드라이버를 매칭하는 단위 (platform, i2c, spi …) |
| controller (adapter) driver | 컨트롤러 드라이버 | 버스 마스터 IP를 구동하고 버스를 등록하는 드라이버 |
| client driver | 클라이언트 드라이버 | 버스 위 칩 하나를 구동하는 드라이버 (`i2c_driver`, `spi_driver`) |
| provider driver | 공급자 드라이버 | clk·regulator·pinctrl처럼 다른 드라이버에 자원을 주는 드라이버 |
| blk-mq | multi-queue block layer | 하드웨어 큐 여러 개를 지원하는 block 계층. 드라이버는 `queue_rq` 구현 |
| request / bio | 요청 / bio | bio = 섹터 범위 + 메모리 조각 목록, request = 병합된 bio 묶음 |
| plug | 플러그 | 태스크가 모아 둔 bio/request를 한꺼번에 내려보내는 배치 장치 |
| sk_buff | 소켓 버퍼 | 패킷 하나와 메타데이터. net 드라이버의 데이터 단위 |
| qdisc | 큐잉 디시플린 | net 드라이버 앞의 송신 큐 (pfifo_fast, fq_codel …) |
| ifindex | 인터페이스 번호 | 네트워크 장치의 정수 ID (major:minor 대신) |
| devtmpfs | 장치 노드 FS | 커널이 `/dev` 노드를 직접 만드는 파일시스템 |
| MTD | Memory Technology Device | raw NAND/NOR용 서브시스템. erase 블록·OOB 인식 |
| misc device | 기타 장치 | major 10을 공유하는 간단한 char 장치 등록 방식 (G7) |
| MFD | Multi-Function Device | 한 칩(PMIC 등)을 기능별 자식 장치로 쪼개는 프레임워크 |

---

## 17. 요약 & 체크리스트

Linux 드라이버의 고전 분류는 char(바이트 스트림, `/dev` 노드, `file_operations`), block(블록 랜덤 액세스, page cache·스케줄러·blk-mq를 지나 `queue_rq`, 위에 파일시스템), net(패킷, 노드 없이 인터페이스와 소켓, `ndo_start_xmit`)이다. 랩 게스트에서 세 종류를 직접 만들어 보니 유저 syscall과 드라이버 콜백 사이의 커널 함수가 각각 2개, 16개, 13개였다 — 차이는 속도가 아니라 **중간 계층이 누구 책임이냐**다. 하지만 실제 시스템을 열어 보면 `/proc/devices`의 char major 대부분이 input·iio·rtc·watchdog·gpiochip 같은 **프레임워크** 이름이고, `/dev` 노드 뒤에는 클래스 장치 → 프레임워크 → 버스 → 컨트롤러로 이어지는 여러 층의 드라이버가 있다. 그래서 실무 질문은 "char냐 block이냐"가 아니라 **"어느 프레임워크에 붙이고, 어느 버스의 클라이언트인가"** 이며, 유저에게 아무것도 보이지 않는 clk·pinctrl·regulator 공급자 드라이버가 bring-up의 핵심이다. 센서 키트의 IMU는 I2C/SPI 클라이언트 + IIO, 버튼은 input, 온도는 hwmon, CAN은 SocketCAN이다.

- [ ] char / block / net의 데이터 단위, 유저 인터페이스, ops 구조체, 등록 API를 표로 말할 수 있다
- [ ] 네트워크 인터페이스에 `/dev` 노드가 없는 이유를 두 가지로 설명할 수 있다
- [ ] `/dev` 노드 하나에서 `/sys/dev/char/M:m`을 따라 드라이버·서브시스템을 찾을 수 있다
- [ ] 클래스 장치가 겹쳐서 `device/driver`가 비는 경우를 부모 추적으로 해결할 수 있다
- [ ] 최소 char 드라이버(`alloc_chrdev_region`, `cdev_add`, `class_create`, `device_create`)를 쓸 수 있다
- [ ] 6.18 API로 blk-mq RAM 디스크(`blk_mq_alloc_sq_tag_set`, `blk_mq_alloc_disk(&set, &lim, data)`, `add_disk`)를 쓸 수 있다
- [ ] 512 B 쓰기가 block 드라이버에 4 KiB 단위 읽기·쓰기로 오는 이유를 설명할 수 있다
- [ ] `alloc_netdev` + `register_netdev`로 dummy NIC를 만들고 `ndo_start_xmit`의 skb 소유권 규칙을 말할 수 있다
- [ ] "대부분은 서브시스템 드라이버"의 의미와 프레임워크가 주는 세 가지(표준 ABI, 공통 코드, ops 계약)를 설명할 수 있다
- [ ] 버스 컨트롤러 드라이버와 클라이언트 드라이버를 I2C 예로 구분할 수 있다
- [ ] 유저에게 아무것도 노출하지 않는 드라이버 예를 들고 디버깅 방법(debugfs)을 말할 수 있다
- [ ] 센서 키트의 각 장치(IMU, 버튼, 온도, LED, 카메라, CAN, 플래시)에 맞는 프레임워크를 고를 수 있다

---

## 참고 자료

- docs.kernel.org — Driver implementer's API guide: https://docs.kernel.org/driver-api/index.html (driver model, basics)
- docs.kernel.org — Multi-Queue Block IO Queueing Mechanism (blk-mq): https://docs.kernel.org/block/blk-mq.html
- docs.kernel.org — Industrial I/O: https://docs.kernel.org/driver-api/iio/index.html
- docs.kernel.org — Input subsystem: https://docs.kernel.org/input/index.html
- docs.kernel.org — Networking, net_device and netdev features: https://docs.kernel.org/networking/index.html
- docs.kernel.org — SocketCAN: https://docs.kernel.org/networking/can.html
- docs.kernel.org — Linux allocated devices (major 번호 목록): https://docs.kernel.org/admin-guide/devices.html
- Corbet, Rubini, Kroah-Hartman, "Linux Device Drivers, 3rd ed." 1장 "Classes of Devices and Modules" (분류 개념은 유효, block API는 낡음)
- Bootlin "Linux kernel and driver development" 교육 슬라이드 (https://bootlin.com/training/kernel/) — 디바이스 모델·프레임워크 장
- 랩 소스 (elixir.bootlin.com에서도 같은 경로): `fs/char_dev.c`(`find_dynamic_major`), `block/blk-mq.c`, `include/linux/blk-mq.h`(`blk_mq_alloc_disk`), `block/blk-mq.h`(`blk_mq_default_nr_requests`), `drivers/block/brd.c`, `drivers/net/dummy.c`, `net/core/dev.c`, `drivers/iio/imu/st_lsm6dsx/`, `drivers/tty/serial/serial_base_bus.c`
