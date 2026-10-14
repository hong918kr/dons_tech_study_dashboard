# I6. 네트워크 드라이버 — net_device, sk_buff, NAPI, TX/RX 링, PHY/MDIO, ethtool

> **이 노트를 다 읽으면**: `net_device`/`net_device_ops`/`sk_buff`/NAPI/BQL/page_pool/phylib/ethtool_ops로 이루어진 NIC 드라이버의 뼈대를 직접 짜고 설명할 수 있다 · 패킷 하나가 `sendto()`에서 TX 링과 완료 IRQ를 거쳐 회수되기까지, 그리고 DMA 버퍼에서 소켓까지 올라가는 길을 함수 이름으로 말할 수 있다 · "링이 꽉 차면?", "NAPI가 왜 필요한가?", "패킷이 어디서 버려지나?"를 카운터(ethtool -S, qdisc, BQL, softnet_stat, snmp)로 증명할 수 있다 · MAC/PHY/MDIO 분리와 DT(`phy-handle`, `phy-mode`, `fixed-link`), 하드웨어 타임스탬프가 로보틱스·차량 이더넷에서 왜 중요한지 말할 수 있다
> **왜 필요한가**: LiDAR(UDP 수백 Mbps)·카메라(GigE)·차량 이더넷(100BASE-T1)·PTP 시간 동기 장비에서 "패킷이 샌다/지연이 튄다/링크가 안 올라온다"는 매주 나오는 이슈이고, 면접에서는 "Explain NAPI", "What happens when the TX ring is full?"이 단골이다
> **Don 기준 난이도**: 디스크립터 링·OWN 비트·doorbell·완료 인터럽트·인터럽트 coalescing은 NVMe SQ/CQ와 같은 개념이라 이미 안다 / 새로 배울 부분은 Linux가 그것을 `sk_buff`·qdisc·NAPI(softirq)·BQL·page_pool·phylib라는 **정해진 틀**로 감싼 방식, 그리고 각 단계의 카운터를 읽는 법
> **선행 노트**: A9(소켓, LiDAR UDP 손실), F5(softirq·tasklet·workqueue), F6(타이머), G1(dummy net_device), G3(platform driver), H6(IRQ), H9(DMA) — Q3(시간 동기)·I10(CAN)으로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

센서 키트를 생각해 보자. Jetson Orin이나 i.MX8M Plus 보드에 LiDAR가 100BASE-T1이나 1000BASE-T 이더넷으로 붙어 있고, 1초에 수만 개의 UDP 패킷을 쏟아낸다(A9 §5). 앱이 `recvfrom()`으로 받기 전에 그 패킷은 **PHY → MAC → DMA → RX 링 → 드라이버 → 프로토콜 스택 → 소켓 큐**를 지나 온다. 이 중 "MAC → 소켓 큐" 구간이 이 노트의 주제, **네트워크 드라이버**다.

Don에게 익숙한 말로 바꾸면 NIC 드라이버는 **NVMe 드라이버와 같은 모양**이다.

| NVMe (Don이 아는 것) | 이더넷 NIC (이 노트) |
|---|---|
| Submission Queue + doorbell | TX 디스크립터 링 + tail 레지스터 |
| Completion Queue + MSI-X | TX 완료 / RX 완료 + MSI-X |
| PRP/SGL로 호스트 버퍼 지정 | 디스크립터에 버퍼 DMA 주소 + 길이 |
| interrupt coalescing (aggregation time/threshold) | `ethtool -C rx-usecs / rx-frames` |
| 명령은 호스트가 먼저 보낸다 | **RX는 상대가 먼저 보낸다** — 빈 버퍼를 미리 깔아 둬야 한다 |
| 큐 깊이 = 동시 명령 수 | 링 크기 = 흡수할 수 있는 burst |

마지막 두 줄이 차이다. 디스크는 "요청해야 데이터가 오지만" NIC는 아무도 요청하지 않아도 패킷이 온다. 그래서 RX 링에는 항상 빈 버퍼를 깔아 두어야 하고, 패킷이 몰려올 때 인터럽트 폭풍을 막는 **NAPI**라는 장치가 필요하다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 520"><defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">보내기(TX)는 위에서 아래로, 받기(RX)는 아래에서 위로</text><rect x="150" y="40" width="380" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="340.0" y="66.5" font-size="13" text-anchor="middle">앱: sendto() / recvfrom()  (A9)</text><rect x="150" y="96" width="380" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="340.0" y="122.5" font-size="13" text-anchor="middle">소켓 + 프로토콜 (UDP / IP / ARP)</text><rect x="150" y="152" width="380" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="340.0" y="178.6" font-size="13" text-anchor="middle">qdisc (pfifo_fast) + BQL</text><rect x="150" y="208" width="380" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="340.0" y="234.6" font-size="13" text-anchor="middle">드라이버: ndo_start_xmit / NAPI poll</text><rect x="150" y="264" width="380" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="340.0" y="290.6" font-size="13" text-anchor="middle">DRAM: TX 링 · RX 링 (디스크립터 + 버퍼)</text><rect x="150" y="320" width="380" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="340.0" y="346.6" font-size="13" text-anchor="middle">MAC (DMA 엔진, 통계, 필터)</text><rect x="150" y="376" width="380" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="340.0" y="402.6" font-size="13" text-anchor="middle">PHY (아날로그, 링크 협상)</text><rect x="150" y="432" width="380" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="340.0" y="458.6" font-size="13" text-anchor="middle">선로: RJ45 / 100BASE-T1</text><polyline points="120,60 120,470" fill="none" stroke="#4a7bd0" stroke-width="1.5" marker-end="url(#ah1)"/><text x="40.0" y="270.0" font-size="13" text-anchor="middle">TX</text><polyline points="560,470 560,60" fill="none" stroke="#e08a3c" stroke-width="1.5" marker-end="url(#ah1)"/><text x="640.0" y="270.0" font-size="13" text-anchor="middle">RX</text><text x="340.0" y="510.0" font-size="12" text-anchor="middle">커널(파랑~주황) · 메모리(초록) · 하드웨어(초록~빨강)</text></svg>
```
그림 1 — 네트워크 스택의 층. 드라이버는 위로는 qdisc·프로토콜과, 아래로는 메모리 속 디스크립터 링을 통해 MAC과 대화한다. PHY는 별도의 관리 버스(MDIO)로 설정한다(§7).

이 노트의 줄기는 다음과 같다.

| 절 | 내용 | 핵심 API |
|---|---|---|
| §1 | 장치 등록 | `alloc_etherdev`, `register_netdev`, `net_device_ops` |
| §2 | 패킷 그릇 | `sk_buff`, `skb_put/push/pull/reserve`, frags |
| §3 | 디스크립터 링과 소유권 | OWN 비트, doorbell, `dma_wmb` |
| §4 | TX 경로와 역압력 | `ndo_start_xmit`, `netif_stop/wake_queue`, BQL, watchdog |
| §5 | RX 경로와 NAPI | `napi_schedule`, `poll(budget)`, `napi_complete_done`, page_pool, GRO |
| §6 | 멀티큐·오프로드·실제 NIC | RSS, features, virtio-net/e1000e/igb 비교, XDP |
| §7 | PHY / MDIO | phylib, phylink, DT `phy-handle`/`fixed-link` |
| §8 | ethtool | `ethtool_ops`, SIOCETHTOOL, netdevsim |
| §9 | 타임스탬프·차량 이더넷 | `skb_tx_timestamp`, SO_TIMESTAMPING, PHC, T1 PHY, TSN |

### 0.1 실습 준비 — `i6_eth`: 메모리 속 가짜 NIC 한 쌍

QEMU `virt` 머신에는 우리가 레지스터를 만질 수 있는 단순한 NIC이 없다. 그래서 **NIC 하드웨어를 소프트웨어로 흉내 내고, 드라이버 쪽은 진짜 NIC 드라이버와 똑같은 API로** 짰다.

| 구성 요소 | 진짜 NIC | `i6_eth`에서는 |
|---|---|---|
| TX/RX 디스크립터 링 | DMA 가능한 메모리의 배열 | `struct i6_txd tx[]`, `struct i6_rxd rx[]` (OWN 비트 포함) |
| DMA 엔진 + 선로 | MAC 하드웨어 | 포트마다 hrtimer 하나. hardirq 문맥에서 TX 링의 HW 소유 칸을 읽어 **상대 포트**의 RX 버퍼에 복사 |
| 인터럽트 + 마스크 | MSI-X 벡터 + IMS/IMC 레지스터 | `irq_masked` 플래그. 마스크가 아니면 "IRQ"를 세고 `napi_schedule()` |
| interrupt coalescing | `ethtool -C rx-usecs` | 엔진 주기 `tick_us` (ethtool `-C`로 바꿀 수 있게 연결) |
| 시스템 버스 | AXI / PCIe | 전역 spinlock `i6_bus` 하나 (아래 함정 참조) |
| PHY | MDIO에 붙은 칩 | 커널의 **fixed PHY**(가상 MDIO 버스 `fixed-0`)에 phylib로 연결 |
| 케이블 | RJ45 | `i6_eth0` ↔ `i6_eth1` 두 포트가 서로의 상대 |

두 포트를 서로 다른 **network namespace**에 넣으면 진짜 두 대의 장비가 케이블로 연결된 것처럼 ping과 UDP가 우리 드라이버를 지나간다. 같은 namespace에 두면 커널이 "내 주소로 가는 패킷"을 `lo`로 보내 버려서 드라이버를 거치지 않는다.

> 함정 — 에뮬레이션의 한계: 진짜 NIC은 디스크립터 소유권을 **OWN 비트 + 메모리 배리어**(`dma_wmb()`/`dma_rmb()`)로 주고받고 락을 쓰지 않는다. `i6_eth`는 하드웨어가 CPU 코어 위에서 돌기 때문에 전역 spinlock으로 그 "버스"를 흉내 냈다. 드라이버 쪽 순서(채우고 → 소유권 넘기고 → doorbell)는 진짜와 같다.

실습 환경: Mac의 Lima VM `elinux`(호스트 커널 `7.0.0-38-generic`) 안에서 QEMU로 랩의 **SUBSYSTEM 커널 `Image-sub`(6.18.55-sub)** 를 부팅했다. 이 커널에는 virtio-net·e1000e·igb가 내장이고 netdevsim·dummy·veth는 모듈이다. BusyBox 1.37에는 `ethtool`·`tc`가 없어서 **SIOCETHTOOL ioctl과 rtnetlink를 직접 부르는 작은 C 도구**를 만들어 썼다(§8, §4.4).

#### 예제 1 — 모듈 빌드와 부팅 스크립트

무엇을 확인하나: 이 노트의 모듈 두 개(`i6_eth`, `i6_skb`)가 `Image-sub`의 빌드 트리로 경고 없이 빌드되고 vermagic이 `6.18.55-sub`인지.

```make
obj-m += i6_eth.o i6_skb.o
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/I6/mod && cp /Users/donh/.elinux_scratch/I6/{i6_eth.c,i6_skb.c,Kbuild} /var/tmp/I6/mod/ \
  && cd /var/tmp/I6/mod && make -j2 -C /var/lab/out/linux-build-sub M=$PWD modules \
  && for m in i6_eth i6_skb; do modinfo $m.ko | grep -E "^(name|vermagic|parm|depends)"; done'
```

```text
  CC [M]  i6_eth.o
  CC [M]  i6_skb.o
  MODPOST Module.symvers
  CC [M]  i6_eth.mod.o
  CC [M]  .module-common.o
  CC [M]  i6_skb.mod.o
  LD [M]  i6_eth.ko
  LD [M]  i6_skb.ko
depends:        
name:           i6_eth
vermagic:       6.18.55-sub SMP preempt mod_unload aarch64
parm:           ring:uint
parm:           tick_us:uint
parm:           burst:uint
parm:           weight:uint
parm:           stack:bool
parm:           stall:bool
depends:        
name:           i6_skb
vermagic:       6.18.55-sub SMP preempt mod_unload aarch64
```

출력에서 볼 것: 경고 0개. `i6_eth`의 파라미터 여섯 개가 실험 손잡이다 — `ring`(링 크기), `tick_us`(엔진 주기 = coalescing), `burst`(틱당 최대 전송 프레임 = 선로 속도), `weight`(NAPI 예산), `stack`(첫 패킷에서 `dump_stack()`), `stall`(엔진 멈춤 → TX timeout 재현).

유저 도구 다섯 개(`i6_ethtool`, `i6_ethfeat`, `i6_udp`, `i6_qdisc`, `i6_ts`)는 VM의 gcc 15로 정적 링크했다(`gcc -std=gnu11 -Wall -Wextra -O2 -static`, 경고 0개). 소스는 해당 절에 있다.

게스트 부팅 스크립트(`boot.sh`, VM에서 실행). 랩 rootfs를 복사해 `/i6/`에 모듈·도구·게스트 스크립트를 넣고, `LAB_CMD`로 스크립트 하나를 실행한 뒤 전원을 끈다.

```sh
#!/bin/bash
# usage: [QX="extra qemu args"] [TMO=sec] boot.sh <guest-script.sh>
# boots Image-sub with /i6/{i6_eth.ko,netdevsim.ko,ipv6.ko,tools} and runs the script
set -e
G=$1; N=$(basename $G .sh); W=/var/tmp/I6/run_$N
M=/var/lab/out/modules-sub/lib/modules/6.18.55-sub/kernel
rm -rf $W && mkdir -p $W/r
cp -a /var/lab/build/rootfs/. $W/r/
mkdir -p $W/r/i6
cp /var/tmp/I6/mod/i6_eth.ko /var/tmp/I6/mod/i6_skb.ko /var/tmp/I6/bin/* $M/drivers/net/netdevsim/netdevsim.ko $M/net/ipv6/ipv6.ko $W/r/i6/
cp /Users/donh/.elinux_scratch/I6/$G $W/r/i6/t.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout ${TMO:-240} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp ${SMP:-2} -m 512M -nographic -no-reboot \
  -kernel /var/lab/out/Image-sub -initrd $W/i.gz $QX \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/i6/t.sh" > $W/out.txt 2>&1 || echo "qemu rc=$?"
tr -d '\r' < $W/out.txt > /var/tmp/I6/out_$N.txt
```

---

## 1. `net_device`와 `net_device_ops` — 장치 등록

### 1.1 직관

G1 §5에서 만든 `g1_net`은 "받은 패킷을 세고 버리는" 장치였다. 진짜 NIC 드라이버는 그 위에 세 가지를 더한다: **하드웨어 자원(링, IRQ, DMA)을 `ndo_open`에서 잡고 `ndo_stop`에서 놓는다**, **TX 완료와 RX를 NAPI로 처리한다**, **PHY·ethtool·통계로 상태를 보여 준다**.

`struct net_device`는 "인터페이스 하나"를 나타내는 커널 객체다. 이름(`eth0`), MAC 주소, MTU, 플래그(UP, RUNNING), 큐(`netdev_queue` 배열), features(오프로드 비트), 통계, 그리고 드라이버 함수 표 `netdev_ops`·`ethtool_ops`를 가진다. 드라이버 자기 상태는 `alloc_netdev(sizeof(priv), …)`로 **같은 메모리 덩어리 뒤쪽**에 붙어 오고 `netdev_priv(dev)`로 꺼낸다. bare-metal로 치면 "장치 핸들 구조체 + 드라이버 컨텍스트"를 한 번에 malloc 하는 것이다.

### 1.2 정의 — 할당과 등록

| 함수 | 하는 일 | 짝 |
|---|---|---|
| `alloc_etherdev(sizeof_priv)` | 이름 `eth%d`, `ether_setup()`으로 이더넷 기본값(MTU 1500, 헤더 14, 브로드캐스트) | `free_netdev()` |
| `alloc_etherdev_mqs(sz, txq, rxq)` | 멀티큐 버전 | `free_netdev()` |
| `alloc_netdev(sz, "i6_eth%d", NET_NAME_ENUM, ether_setup)` | 이름 패턴을 직접 지정 | `free_netdev()` |
| `SET_NETDEV_DEV(ndev, &pdev->dev)` | sysfs 부모 지정 (`/sys/devices/platform/…/net/eth0`) | — |
| `netif_napi_add(_weight)(ndev, &napi, poll, w)` | NAPI 인스턴스 등록 | `netif_napi_del()` |
| `register_netdev(ndev)` | 이름 확정, sysfs·netlink에 공개. **이 순간부터 `ndo_open`이 불릴 수 있다** | `unregister_netdev()` |
| `devm_alloc_etherdev()` / `devm_register_netdev()` | probe에서 쓰는 managed 버전 | 자동 |

규칙 하나: `register_netdev()`는 **probe의 마지막**에 부른다. 등록 직후 udev나 NetworkManager가 곧바로 `ip link set up`을 해서 `ndo_open`이 불릴 수 있으므로, 그 전에 링·PHY·NAPI 같은 모든 준비가 끝나 있어야 한다.

### 1.3 `net_device_ops` — 드라이버가 채우는 표

| 콜백 | 언제 불리나 | 문맥 | 실제 드라이버가 하는 일 |
|---|---|---|---|
| `ndo_open` | `ip link set dev X up` | 프로세스, RTNL 락 | 링 할당, RX 버퍼 채우기, IRQ 요청, `napi_enable`, `phy_start`/`phylink_start`, `netif_start_queue` |
| `ndo_stop` | `ip link set dev X down` | 프로세스, RTNL | 위의 역순. 링에 남은 skb 해제 |
| `ndo_start_xmit` | 보낼 skb가 있을 때 | softirq 또는 프로세스(BH 꺼짐), TX 큐 락 | 디스크립터 채우기 + doorbell. **잠들면 안 된다** |
| `ndo_get_stats64` | `ip -s link`, `/proc/net/dev` | 아무 데서나 | 카운터를 `rtnl_link_stats64`에 복사 |
| `ndo_set_rx_mode` | 멀티캐스트 가입, promisc 변경 | 원자 문맥(addr 락) | MAC 주소 필터 / 해시 테이블 프로그래밍 |
| `ndo_change_mtu` | `ip link set mtu` | RTNL | RX 버퍼 크기 재조정(대개 링 재생성) |
| `ndo_tx_timeout` | TX 큐가 `watchdog_timeo` 넘게 멈춤 | timer softirq | 하드웨어 리셋 (보통 workqueue로 미룸) |
| `ndo_set_mac_address` | `ip link set address` | RTNL | MAC 주소 레지스터 갱신 (`eth_mac_addr` 헬퍼) |
| `ndo_eth_ioctl` / `ndo_hwtstamp_set` | SIOCSHWTSTAMP 등 | 프로세스 | PHY ioctl, 하드웨어 타임스탬프 설정 (§9) |
| `ndo_bpf` / `ndo_xdp_xmit` | XDP 프로그램 붙이기 | RTNL | §6.4 |

### 1.4 `i6_eth` 코드 1 — 파라미터, 디스크립터, 통계, 드라이버 상태

전체 소스는 531줄이고 이 노트에 순서대로 조각내어 싣는다(조각 1~13, 이어 붙이면 그대로 빌드된다).

```c
// SPDX-License-Identifier: GPL-2.0
/* I6: software NIC pair. Two net_devices joined by a "virtual cable".
 * Each port has a TX ring and an RX ring of descriptors with an OWN bit.
 * An hrtimer per port plays the DMA engine + wire: it takes TX descriptors
 * owned by HW, copies the frame into the peer's posted RX buffer, hands both
 * descriptors back to SW and raises an (emulated, maskable) IRQ.
 * The driver side is a normal NAPI driver: IRQ -> mask -> napi_schedule ->
 * poll(budget) cleans TX + receives RX -> napi_complete_done -> unmask. */
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/hrtimer.h>
#include <linux/phy.h>
#include <linux/phy_fixed.h>
#include <linux/platform_device.h>
#include <net/page_pool/helpers.h>

#define I6_RING_MAX 256
#define I6_HEADROOM (NET_SKB_PAD + NET_IP_ALIGN)

static unsigned int ring = 64;     module_param(ring, uint, 0444);
static unsigned int tick_us = 20;  module_param(tick_us, uint, 0444);
static unsigned int burst = 64;    module_param(burst, uint, 0444);
static unsigned int weight = 64;   module_param(weight, uint, 0444);
static bool stack;                 module_param(stack, bool, 0644);
static bool stall;                 module_param(stall, bool, 0644);

struct i6_txd { struct sk_buff *skb; u32 len; u32 own; };   /* own=1: HW */
struct i6_rxd { struct page *page;   u32 len; u32 own; };

struct i6_stats {	/* every field is u64: ethtool -S copies it as an array */
	u64 irqs, polls, poll_full, rx_packets, rx_bytes, rx_missed, rx_dropped;
	u64 tx_packets, tx_bytes, tx_stops, tx_wakes, tx_timeouts, hw_ticks;
	u64 hist[8];	/* polls by work: 0,1,2-3,4-7,8-15,16-31,32-63,64+ */
};
static const char i6_stat_names[][ETH_GSTRING_LEN] = {
	"irqs", "polls", "poll_full_budget", "rx_packets", "rx_bytes",
	"rx_missed_no_desc", "rx_dropped", "tx_packets", "tx_bytes",
	"tx_queue_stops", "tx_queue_wakes", "tx_timeouts", "hw_ticks",
	"poll_w0", "poll_w1", "poll_w2_3", "poll_w4_7", "poll_w8_15",
	"poll_w16_31", "poll_w32_63", "poll_w64",
};
#define I6_NSTATS ARRAY_SIZE(i6_stat_names)

struct i6_priv {
	struct net_device *dev;
	struct i6_priv *peer;
	struct napi_struct napi;
	struct hrtimer hw;		/* the "DMA engine + wire" */
	struct page_pool *pp;
	struct phy_device *phy;
	bool up, irq_masked;
	u32 tick_ns;
	u32 tx_head, tx_hw, tx_clean;	/* SW producer, HW consumer, SW cleaner */
	u32 rx_hw, rx_next;		/* HW producer, SW consumer */
	struct i6_txd tx[I6_RING_MAX];
	struct i6_rxd rx[I6_RING_MAX];
	struct i6_stats st;
};

static DEFINE_SPINLOCK(i6_bus);	/* one lock = the emulated system bus */
static struct net_device *i6_ndev[2];
#define MASK (ring - 1)
```

코드에서 볼 것:

- 디스크립터는 `own` 하나로 "지금 이 칸이 누구 것인가"를 표시한다. 진짜 NIC은 이것이 디스크립터의 상태 비트(예: Intel의 DD bit, FEC의 `BD_ENET_TX_READY`)다.
- 인덱스 다섯 개: TX는 `tx_head`(드라이버가 다음에 쓸 칸 — 하드웨어 tail 레지스터에 해당), `tx_hw`(하드웨어가 다음에 읽을 칸 — head 레지스터), `tx_clean`(드라이버가 다음에 회수할 칸). RX는 `rx_hw`(하드웨어가 다음에 채울 칸), `rx_next`(드라이버가 다음에 꺼낼 칸). 모두 계속 증가하는 `u32`이고 칸 번호는 `& MASK`로 구한다 — wrap-around 비교가 필요 없는 링 버퍼 관용구다.
- 통계를 전부 `u64`로 둔 이유: `ethtool -S`는 `u64` 배열을 원하므로 구조체를 통째로 `memcpy` 할 수 있다(§8).

### 1.5 `i6_eth` 코드 2 — 포트 생성: alloc → ops → NAPI → PHY → register

```c
static int i6_create(struct device *parent, int idx)
{
	struct fixed_phy_status fs = { .link = 1, .speed = SPEED_1000, .duplex = DUPLEX_FULL };
	struct net_device *dev;
	struct i6_priv *p;
	int ret;

	dev = alloc_netdev(sizeof(*p), "i6_eth%d", NET_NAME_ENUM, ether_setup);
	if (!dev)
		return -ENOMEM;
	p = netdev_priv(dev);
	SET_NETDEV_DEV(dev, parent);		/* sysfs parent; phylib needs it */
	p->dev = dev;
	p->tick_ns = tick_us * 1000;
	dev->dev_id = idx;
	dev->netdev_ops = &i6_ops;
	dev->ethtool_ops = &i6_ethtool_ops;
	dev->watchdog_timeo = 2 * HZ;
	dev->min_mtu = ETH_MIN_MTU;
	dev->max_mtu = ETH_DATA_LEN;
	eth_hw_addr_random(dev);
	netif_napi_add_weight(dev, &p->napi, i6_poll, weight);
	hrtimer_setup(&p->hw, i6_hw_tick, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	p->phy = fixed_phy_register(&fs, NULL);
	if (IS_ERR(p->phy)) {
		ret = PTR_ERR(p->phy);
		goto err_free;
	}
	ret = phy_connect_direct(dev, p->phy, i6_adjust_link, PHY_INTERFACE_MODE_GMII);
	if (ret)
		goto err_phy;
	netif_carrier_off(dev);
	ret = register_netdev(dev);
	if (ret)
		goto err_disc;
	i6_ndev[idx] = dev;
	return 0;
err_disc:
	phy_disconnect(p->phy);
err_phy:
	fixed_phy_unregister(p->phy);
err_free:
	free_netdev(dev);
	return ret;
}
```

코드에서 볼 것:

- `SET_NETDEV_DEV(dev, parent)`가 없으면 phylib가 터진다. 처음에는 부모 없이 만들었더니 `phy_connect_direct()` 안에서 NULL 역참조가 났다(`phy_attach_direct+0x3c`). phylib는 `dev->dev.parent->driver->owner`로 MAC 드라이버 모듈의 참조를 잡기 때문이다. 그래서 `i6_eth`를 **platform driver**로 만들고(조각 12) 부모 장치를 붙였다 — 진짜 SoC NIC이 DT로 probe 되는 구조와 같다.
- `netif_carrier_off()` 후 등록: 링크 상태는 드라이버가 아니라 **phylib가** 켠다(§7).
- `watchdog_timeo = 2 * HZ`: TX 큐가 2초 넘게 멈추면 `ndo_tx_timeout`이 불린다(§4.5).

### 1.6 `i6_eth` 코드 3 — 나머지 ndo 콜백과 ops 표

```c
static void i6_tx_timeout(struct net_device *dev, unsigned int txq)
{
	struct i6_priv *p = netdev_priv(dev);

	p->st.tx_timeouts++;
	netdev_err(dev, "TX timeout on queue %u: head=%u hw=%u clean=%u -> reset HW\n",
		   txq, p->tx_head, p->tx_hw, p->tx_clean);
	stall = false;				/* "reset" the stuck engine */
	hrtimer_start(&p->hw, ns_to_ktime(p->tick_ns), HRTIMER_MODE_REL);
}

static void i6_get_stats64(struct net_device *dev, struct rtnl_link_stats64 *s)
{
	struct i6_priv *p = netdev_priv(dev);

	s->rx_packets = p->st.rx_packets;
	s->rx_bytes = p->st.rx_bytes;
	s->rx_missed_errors = p->st.rx_missed;
	s->rx_dropped = p->st.rx_dropped;
	s->tx_packets = p->st.tx_packets;
	s->tx_bytes = p->st.tx_bytes;
}

static void i6_set_rx_mode(struct net_device *dev)
{	/* a real MAC would program its unicast/multicast filter here */
	netdev_dbg(dev, "rx_mode: promisc=%d allmulti=%d mc=%d\n",
		   !!(dev->flags & IFF_PROMISC), !!(dev->flags & IFF_ALLMULTI),
		   netdev_mc_count(dev));
}

static int i6_change_mtu(struct net_device *dev, int new_mtu)
{
	netdev_info(dev, "mtu %u -> %d (a real NIC resizes RX buffers here)\n",
		    dev->mtu, new_mtu);
	WRITE_ONCE(dev->mtu, new_mtu);
	return 0;
}

static const struct net_device_ops i6_ops = {
	.ndo_open		= i6_open,
	.ndo_stop		= i6_stop,
	.ndo_start_xmit		= i6_xmit,
	.ndo_get_stats64	= i6_get_stats64,
	.ndo_set_rx_mode	= i6_set_rx_mode,
	.ndo_change_mtu		= i6_change_mtu,
	.ndo_tx_timeout		= i6_tx_timeout,
	.ndo_set_mac_address	= eth_mac_addr,
	.ndo_validate_addr	= eth_validate_addr,
};

```

코드에서 볼 것: `ndo_get_stats64`는 드라이버 카운터를 표준 구조체로 옮길 뿐이다. 여기서 `rx_missed_errors`에 넣은 "RX 링에 빈칸이 없어서 하드웨어가 버린 프레임"은 `/proc/net/dev`의 `fifo` 열과 `ip -s link`의 `missed`로 나온다. 실제 드라이버는 64비트 카운터를 32비트 CPU에서도 찢어지지 않게 읽기 위해 `u64_stats_sync`(`u64_stats_update_begin/end`, `u64_stats_fetch_begin/retry`)로 감싼다. `i6_eth`는 arm64 전용이라 생략했다.

### 1.7 `i6_eth` 코드 4 — platform driver로 감싸기

```c

static void i6_destroy(int idx)
{
	struct net_device *dev = i6_ndev[idx];
	struct i6_priv *p;

	if (!dev)
		return;
	p = netdev_priv(dev);
	unregister_netdev(dev);
	phy_disconnect(p->phy);
	fixed_phy_unregister(p->phy);
	free_netdev(dev);
}

static int i6_probe(struct platform_device *pdev)
{
	int ret;

	if (!is_power_of_2(ring) || ring > I6_RING_MAX || ring < 4)
		return -EINVAL;
	ret = i6_create(&pdev->dev, 0);
	if (ret)
		return ret;
	ret = i6_create(&pdev->dev, 1);
	if (ret) {
		i6_destroy(0);
		return ret;
	}
	((struct i6_priv *)netdev_priv(i6_ndev[0]))->peer = netdev_priv(i6_ndev[1]);
	((struct i6_priv *)netdev_priv(i6_ndev[1]))->peer = netdev_priv(i6_ndev[0]);
	dev_info(&pdev->dev, "cable %s <-> %s ring=%u tick=%uus burst=%u weight=%u\n",
		 i6_ndev[0]->name, i6_ndev[1]->name, ring, tick_us, burst, weight);
	return 0;
}

static void i6_remove(struct platform_device *pdev)
{
	i6_destroy(1);
	i6_destroy(0);
}

static struct platform_driver i6_pdrv = {
	.probe = i6_probe,
	.remove = i6_remove,
	.driver = { .name = "i6_eth" },
};
static struct platform_device *i6_pdev;

static int __init i6_init(void)
{
	int ret = platform_driver_register(&i6_pdrv);

	if (ret)
		return ret;
	/* on a real SoC the device comes from DT; here we create it ourselves */
	i6_pdev = platform_device_register_simple("i6_eth", -1, NULL, 0);
	if (IS_ERR(i6_pdev)) {
		platform_driver_unregister(&i6_pdrv);
		return PTR_ERR(i6_pdev);
	}
	return 0;
}

static void __exit i6_exit(void)
{
	platform_device_unregister(i6_pdev);
	platform_driver_unregister(&i6_pdrv);
}
module_init(i6_init);
module_exit(i6_exit);
MODULE_DESCRIPTION("I6 software NIC pair with NAPI, BQL, page_pool, fixed PHY");
MODULE_LICENSE("GPL");
```

진짜 보드라면 `platform_device_register_simple()` 대신 DT 노드(`ethernet@30be0000 { compatible = "fsl,imx8mp-fec"; … }`)가 platform device를 만들고, `.of_match_table`로 이 드라이버의 probe가 불린다(G3).

### 1.8 예제 2 — 등록, sysfs, PHY 링크, ethtool 정보

무엇을 확인하나: 모듈을 넣으면 두 인터페이스가 platform 장치 밑에 생기고, `up` 하면 phylib가 fixed PHY로 링크를 올리며, 우리 ethtool 콜백이 SIOCETHTOOL로 읽히는지.

게스트 스크립트 `t1_up.sh`:

```sh
cd /i6
echo "### insmod"; insmod i6_eth.ko; dmesg | grep -E "i6_eth|Fixed" | tail -3
echo "### links"; ip -o link show | grep i6_eth
echo "### sysfs"; readlink /sys/class/net/i6_eth0; ls --color=never /sys/class/net/i6_eth0/queues; ls --color=never /sys/bus/mdio_bus/devices; readlink /sys/class/net/i6_eth0/phydev; cat /sys/bus/mdio_bus/devices/fixed-0:00/phy_id; readlink /sys/bus/mdio_bus/devices/fixed-0:00/driver
echo "### up"; ip link set i6_eth0 up; ip link set i6_eth1 up; sleep 2; dmesg | grep -E "Link is|i6_eth" | tail -4
cat /sys/class/net/i6_eth0/carrier /sys/class/net/i6_eth0/operstate /sys/class/net/i6_eth0/speed /sys/class/net/i6_eth0/duplex
echo "### ethtool -i"; ./i6_ethtool -i i6_eth0
echo "### ethtool -g"; ./i6_ethtool -g i6_eth0
echo "### ethtool -c"; ./i6_ethtool -c i6_eth0
echo "### link"; ./i6_ethfeat -l i6_eth0
echo "### features"; ./i6_ethfeat -k i6_eth0
echo "### ts"; ./i6_ethfeat -T i6_eth0
echo "### bql"; ls --color=never /sys/class/net/i6_eth0/queues/tx-0/byte_queue_limits | tr "\n" " "; echo; cat /sys/class/net/i6_eth0/queues/tx-0/byte_queue_limits/limit
echo "### napi threaded"; cat /sys/class/net/i6_eth0/threaded /sys/class/net/i6_eth0/napi_defer_hard_irqs /sys/class/net/i6_eth0/gro_flush_timeout
```

```text
lab initramfs: 6.18.55-sub up 0.78s
### insmod
[    0.896008] i6_eth: loading out-of-tree module taints kernel.
[    0.918078] i6_eth i6_eth: cable i6_eth0 <-> i6_eth1 ring=64 tick=20us burst=64 weight=64
### links
3: i6_eth0: <BROADCAST,MULTICAST> mtu 1500 qdisc noop qlen 1000\    link/ether 42:18:6b:37:3c:c0 brd ff:ff:ff:ff:ff:ff
4: i6_eth1: <BROADCAST,MULTICAST> mtu 1500 qdisc noop qlen 1000\    link/ether 9a:28:1f:92:92:f7 brd ff:ff:ff:ff:ff:ff
### sysfs
../../devices/platform/i6_eth/net/i6_eth0
rx-0  tx-0
fixed-0:00  fixed-0:01
0x00000000
../../../../../bus/mdio_bus/drivers/Generic PHY
### up
[    0.896008] i6_eth: loading out-of-tree module taints kernel.
[    0.918078] i6_eth i6_eth: cable i6_eth0 <-> i6_eth1 ring=64 tick=20us burst=64 weight=64
[    1.015565] i6_eth i6_eth i6_eth0: Link is Up - 1Gbps/Full - flow control off
[    1.022304] i6_eth i6_eth i6_eth1: Link is Up - 1Gbps/Full - flow control off
1
up
1000
full
### ethtool -i
driver: i6_eth
version: 0.1
bus-info: cable:end-a
n_stats: 21
### ethtool -g
rx 64/256  tx 64/256 (current/max)
### ethtool -c
rx-usecs: 20  rx-frames: 0  tx-usecs: 0
### link
speed 1000 duplex full autoneg on port 2 phyad 0
### features
active: rx-gro
### ts
so_timestamping 0x1a (hw-tx 0 hw-rx 0 sw-tx 1) phc -1 tx_types 0x0 rx_filters 0x0
### bql
hold_time inflight limit limit_max limit_min stall_cnt stall_max stall_thrs 
0
### napi threaded
0
0
0
[    3.148302] reboot: Power down
```

출력에서 볼 것:

- `insmod` 직후 두 포트는 `<BROADCAST,MULTICAST>`에 `qdisc noop`이다. 아직 `ndo_open`이 불리지 않았다.
- `../../devices/platform/i6_eth/net/i6_eth0`: `SET_NETDEV_DEV` 덕분에 platform 장치 밑에 붙었다. 큐는 `rx-0`, `tx-0` 하나씩(단일 큐 장치).
- MDIO 버스 장치 `fixed-0:00`, `fixed-0:01`: "버스 이름 `fixed-0`, PHY 주소 0과 1". 진짜 보드라면 `30be0000.ethernet-1:01`처럼 MAC 이름 + PHY 주소가 된다. 드라이버는 `Generic PHY`(PHY ID 0이라 전용 드라이버가 없다).
- `up` 후 약 0.1초 뒤 `Link is Up - 1Gbps/Full`: 우리 `i6_adjust_link()`의 `phy_print_status()`가 찍은 줄이다. `carrier=1`, `operstate=up`, `speed=1000`, `duplex=full` — sysfs 값도 PHY에서 왔다.
- `features`는 `rx-gro` 하나뿐이다. 커널은 등록 때 모든 장치에 GSO와 GRO를 켜 주지만(`NETIF_F_SOFT_FEATURES`), scatter-gather(`NETIF_F_SG`)가 없는 장치에서는 `netdev_fix_features()`가 "Dropping NETIF_F_GSO since no SG feature"로 GSO를 끈다(§6.3).
- 타임스탬프 능력 `0x1a` = `TX_SOFTWARE | RX_SOFTWARE | SOFTWARE`, PHC 없음(`phc -1`) — `ethtool_op_get_ts_info`가 주는 기본값이다(§9).
- BQL 디렉터리(`byte_queue_limits/`)가 큐마다 자동으로 생긴다. `limit 0`은 아직 학습 전이라는 뜻이다(§4.4).
- `threaded 0`: NAPI poll이 softirq에서 돈다. 1로 쓰면 장치별 커널 스레드 `napi/<dev>-<id>`로 옮겨진다(§5.6).

> 함정: `fixed-0:00`의 PHY ID가 `0x00000000`이라고 이상하게 볼 필요는 없다. fixed PHY는 레지스터 에뮬레이션(`swphy`)이 만든 가짜이고, 진짜 PHY는 레지스터 2·3에서 OUI가 든 ID(예: Realtek `0x001cc916`)를 읽어 맞는 드라이버를 고른다.

---

## 2. `sk_buff` 해부 — 패킷 그릇

### 2.1 직관

`struct sk_buff`(줄여서 skb)는 **패킷 하나의 메타데이터 + 데이터 버퍼를 가리키는 포인터 네 개**다. 펌웨어에서 버퍼 디스크립터에 "버퍼 시작, 유효 데이터 시작, 길이"를 들고 다니던 것과 같은데, Linux는 여기에 **앞쪽 여유 공간(headroom)** 을 둔다. TX에서는 UDP → IP → 이더넷 순으로 헤더가 **앞에** 붙기 때문에, 데이터를 복사하지 않고 `data` 포인터만 앞으로 당기면 헤더 자리가 생긴다. RX에서는 반대로 헤더를 읽고 포인터만 뒤로 민다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 380"><defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="20" y="30" width="170" height="150" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="105.0" y="75.5" font-size="13" text-anchor="middle">struct sk_buff</text><text x="105.0" y="92.5" font-size="13" text-anchor="middle">(메타데이터)</text><text x="105.0" y="109.5" font-size="13" text-anchor="middle">head · data</text><text x="105.0" y="126.5" font-size="13" text-anchor="middle">tail · end</text><text x="105.0" y="143.6" font-size="13" text-anchor="middle">len · data_len</text><rect x="210" y="60" width="80" height="50" rx="0" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="250.0" y="89.2" font-size="12" text-anchor="middle">헤드룸</text><rect x="290" y="60" width="55" height="50" rx="0" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="317.5" y="89.2" font-size="12" text-anchor="middle">eth</text><rect x="345" y="60" width="55" height="50" rx="0" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="372.5" y="89.2" font-size="12" text-anchor="middle">ip</text><rect x="400" y="60" width="50" height="50" rx="0" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="425.0" y="89.2" font-size="12" text-anchor="middle">udp</text><rect x="450" y="60" width="90" height="50" rx="0" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="495.0" y="89.2" font-size="12" text-anchor="middle">payload</text><rect x="540" y="60" width="70" height="50" rx="0" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="575.0" y="89.2" font-size="12" text-anchor="middle">테일룸</text><rect x="610" y="60" width="60" height="50" rx="0" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="640.0" y="89.2" font-size="12" text-anchor="middle">shinfo</text><text x="210.0" y="140.0" font-size="12" text-anchor="middle">head</text><polyline points="210,128 210,112" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/><text x="290.0" y="140.0" font-size="12" text-anchor="middle">data</text><polyline points="290,128 290,112" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/><text x="540.0" y="140.0" font-size="12" text-anchor="middle">tail</text><polyline points="540,128 540,112" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/><text x="610.0" y="140.0" font-size="12" text-anchor="middle">end</text><polyline points="610,128 610,112" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/><line x1="290" y1="48" x2="540" y2="48" stroke="#e08a3c" stroke-width="1"/><text x="415.0" y="42.0" font-size="12" text-anchor="middle">len (선형 부분 = skb_headlen)</text><text x="340.0" y="175.0" font-size="12" text-anchor="middle">skb_push: data를 앞으로(헤더 추가) · skb_pull: data를 뒤로(헤더 벗기기)</text><text x="340.0" y="195.0" font-size="12" text-anchor="middle">skb_put: tail을 뒤로(데이터 추가) · skb_reserve: data와 tail을 함께 뒤로</text><polyline points="640,112 640,240 522,240" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/><rect x="330" y="222" width="190" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="425.0" y="246.2" font-size="12" text-anchor="middle">skb_shinfo: frags[ ]</text><rect x="60" y="300" width="150" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="135.0" y="326.2" font-size="12" text-anchor="middle">page A (frag 0)</text><rect x="260" y="300" width="150" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="335.0" y="326.2" font-size="12" text-anchor="middle">page B (frag 1)</text><rect x="460" y="300" width="190" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="555.0" y="326.2" font-size="12" text-anchor="middle">frag_list (다른 skb)</text><polyline points="380,262 135,298" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/><polyline points="410,262 335,298" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/><polyline points="470,262 555,298" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="5,4" marker-end="url(#ah2)"/><text x="340.0" y="372.0" font-size="12" text-anchor="middle">len = 선형 부분 + data_len(조각들의 합)</text></svg>
```
그림 2 — `sk_buff`의 선형 버퍼. `head`~`end`가 할당된 영역, `data`~`tail`이 지금 유효한 바이트(len의 선형 부분)다. 끝에는 `skb_shared_info`가 붙어 페이지 조각(frags)과 frag_list를 가리킨다.

### 2.2 정의

| 이름 | 뜻 |
|---|---|
| `head`, `end` | 할당된 선형 버퍼의 시작과 끝 (`end` 바로 뒤에 `skb_shared_info`) |
| `data`, `tail` | 유효 데이터의 시작과 끝 |
| `len` | 패킷 전체 길이 = 선형 부분(`tail - data`) + `data_len` |
| `data_len` | 페이지 조각(frags)과 frag_list에 든 바이트 수. 0이면 선형(linear) skb |
| `skb_headlen(skb)` | 선형 부분 길이 = `len - data_len` |
| `skb_shinfo(skb)->frags[]` | `{page, offset, size}` 배열 (최대 `MAX_SKB_FRAGS`, 기본 17) |
| `truesize` | 이 skb가 실제로 먹은 메모리. 소켓 버퍼 한도(`SO_RCVBUF`)는 이 값으로 계산된다 |
| `protocol` | EtherType (`eth_type_trans()`가 채움) |
| `ip_summed` | 체크섬 상태: `CHECKSUM_NONE` / `UNNECESSARY` / `COMPLETE` / `PARTIAL` |

| 함수 | 포인터 변화 | 쓰는 곳 |
|---|---|---|
| `skb_reserve(skb, n)` | `data`, `tail` 둘 다 +n (빈 skb에서만) | 할당 직후 헤드룸 확보 |
| `skb_put(skb, n)` | `tail` +n, `len` +n, 새 영역 포인터 반환 | 데이터 덧붙이기 (RX 프레임 길이 설정) |
| `skb_push(skb, n)` | `data` -n, `len` +n | TX: 헤더 앞에 붙이기 |
| `skb_pull(skb, n)` | `data` +n, `len` -n | RX: 헤더 벗기기 |
| `skb_add_rx_frag(skb, i, page, off, size, truesize)` | frags에 페이지 추가, `len`/`data_len` 증가 | 점보·멀티 버퍼 RX |
| `skb_linearize(skb)` | 조각을 선형 버퍼로 모음 (복사!) | 조각을 못 다루는 하드웨어 |

### 2.3 예제 3 — 포인터를 한 단계씩 찍어 보기

무엇을 확인하나: `skb_reserve/put/push/pull/add_rx_frag/linearize`가 정말로 포인터만 옮기는지, 각 단계의 `len`, `data_len`, headroom, tailroom.

```c
// SPDX-License-Identifier: GPL-2.0
/* I6: sk_buff pointer arithmetic, printed step by step */
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>

static void show(const char *step, struct sk_buff *skb)
{
	pr_info("%-23s head=+0 data=+%-3ld tail=+%-3ld end=+%-3u len=%-4u data_len=%-4u headroom=%-3u tailroom=%u nr_frags=%u\n",
		step, (long)(skb->data - skb->head), (long)(skb_tail_pointer(skb) - skb->head),
		skb_end_offset(skb), skb->len, skb->data_len, skb_headroom(skb),
		skb_tailroom(skb), skb_shinfo(skb)->nr_frags);
}

static int __init i6_skb_init(void)
{
	struct sk_buff *skb = alloc_skb(256, GFP_KERNEL);
	struct page *pg = alloc_page(GFP_KERNEL);
	u8 *p;

	if (!skb || !pg)
		goto out;
	show("alloc_skb(256)", skb);
	skb_reserve(skb, NET_SKB_PAD + NET_IP_ALIGN);	/* room for headers */
	show("skb_reserve(PAD+ALIGN)", skb);
	p = skb_put(skb, 100);				/* payload (e.g. UDP data) */
	memset(p, 0xab, 100);
	show("skb_put(100)", skb);
	skb_push(skb, 8);				/* UDP header */
	show("skb_push(8)  udp", skb);
	skb_push(skb, 20);				/* IPv4 header */
	show("skb_push(20) ip", skb);
	skb_push(skb, ETH_HLEN);			/* Ethernet header */
	show("skb_push(14) eth", skb);
	skb_pull(skb, ETH_HLEN);			/* RX: eth_type_trans() does this */
	show("skb_pull(14)", skb);
	skb_add_rx_frag(skb, 0, pg, 0, 1000, PAGE_SIZE);	/* page fragment */
	pg = NULL;
	show("skb_add_rx_frag(1000)", skb);
	pr_info("skb_headlen=%u  truesize=%u  cloned=%d  is_nonlinear=%d\n",
		skb_headlen(skb), skb->truesize, skb_cloned(skb), skb_is_nonlinear(skb));
	if (skb_linearize(skb) == 0)
		show("skb_linearize()", skb);
out:
	if (pg)
		__free_page(pg);
	kfree_skb(skb);
	return -EAGAIN;		/* demo only: do not stay loaded */
}
module_init(i6_skb_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("I6 sk_buff layout demo");
```

```sh
# t9_skb.sh (게스트) 첫 줄
cd /i6; insmod i6_skb.ko; dmesg | grep -E "skb_|alloc_skb|truesize" | sed 's/^\[[ 0-9.]*\] //'
```

```text
### i6_skb
insmod: can't insert 'i6_skb.ko': Resource temporarily unavailable
alloc_skb(256)          head=+0 data=+0   tail=+0   end=+384 len=0    data_len=0    headroom=0   tailroom=384 nr_frags=0
skb_reserve(PAD+ALIGN)  head=+0 data=+64  tail=+64  end=+384 len=0    data_len=0    headroom=64  tailroom=320 nr_frags=0
skb_put(100)            head=+0 data=+64  tail=+164 end=+384 len=100  data_len=0    headroom=64  tailroom=220 nr_frags=0
skb_push(8)  udp        head=+0 data=+56  tail=+164 end=+384 len=108  data_len=0    headroom=56  tailroom=220 nr_frags=0
skb_push(20) ip         head=+0 data=+36  tail=+164 end=+384 len=128  data_len=0    headroom=36  tailroom=220 nr_frags=0
skb_push(14) eth        head=+0 data=+22  tail=+164 end=+384 len=142  data_len=0    headroom=22  tailroom=220 nr_frags=0
skb_pull(14)            head=+0 data=+36  tail=+164 end=+384 len=128  data_len=0    headroom=36  tailroom=220 nr_frags=0
skb_add_rx_frag(1000)   head=+0 data=+36  tail=+164 end=+384 len=1128 data_len=1000 headroom=36  tailroom=0 nr_frags=1
skb_headlen=128  truesize=5056  cloned=0  is_nonlinear=1
skb_linearize()         head=+0 data=+36  tail=+1164 end=+1728 len=1128 data_len=0    headroom=36  tailroom=564 nr_frags=0
```

출력에서 볼 것:

- `alloc_skb(256)`인데 `end=+384`: 커널이 크기를 slab 객체 크기에 맞춰 키운다(`kmalloc_size_roundup`). 남는 공간은 tailroom이 된다.
- `skb_reserve(NET_SKB_PAD + NET_IP_ALIGN)`이 64였다. arm64에서 `NET_IP_ALIGN`은 **0**이다(x86도 0). 2로 두는 아키텍처는 "14바이트 이더넷 헤더 뒤의 IP 헤더를 4바이트 정렬시키려고" 2바이트를 더 민다 — 비정렬 접근이 비싼 CPU를 위한 것이다. 처음 레이블을 66으로 적었다가 출력을 보고 고쳤다.
- `push(8)` → `push(20)` → `push(14)`: `data`가 64 → 56 → 36 → 22로 앞으로 오고, `tail`은 그대로다. **복사 0바이트로 헤더 세 개를 붙였다.** 헤드룸이 모자라면 `skb_push`는 `skb_under_panic`으로 커널을 멈춘다 — 그래서 TX 드라이버가 자기 헤더(예: 하드웨어 TX 헤더 8바이트)를 붙여야 하면 `dev->needed_headroom`을 설정해 스택이 미리 공간을 만들게 한다.
- `skb_add_rx_frag(1000)` 후 `len=1128 = 128(선형) + 1000(data_len)`, `nr_frags=1`, `truesize=5056`(페이지 4096 포함). 조각이 생기면 `skb_tailroom()`은 0을 돌려준다(비선형 skb에 `skb_put` 금지).
- `skb_linearize()`는 1000바이트를 새 버퍼로 **복사**해 선형으로 만들었다(`end=+1728`). 고속 경로에서는 피해야 하는 비용이다.
- 모듈이 `-EAGAIN`을 돌려주게 해서 insmod가 실패한 것은 의도다(데모만 하고 남지 않게).

### 2.4 해제 함수 — 어떤 걸 언제 쓰나

| 함수 | 문맥 | 의미 |
|---|---|---|
| `kfree_skb(skb)` | 프로세스/softirq | **드롭**으로 해제 (drop monitor·`kfree_skb` tracepoint에 잡힘) |
| `consume_skb(skb)` | 프로세스/softirq | **정상 소비**로 해제 (드롭 통계에 안 잡힘) |
| `dev_kfree_skb_irq(skb)` / `dev_consume_skb_irq` | hardirq | 해제를 softirq로 미룸 |
| `dev_kfree_skb_any(skb)` / `dev_consume_skb_any` | 어디서든 | 문맥을 보고 위 둘 중 하나 |
| `napi_consume_skb(skb, budget)` | NAPI poll 안 | per-CPU 캐시에 모아 일괄 해제 (가장 빠름). `budget == 0`(netpoll)이면 `dev_consume_skb_any`로 대체 |

TX 완료 처리에서 `kfree_skb`를 쓰면 정상 전송된 패킷이 전부 "드롭"으로 집계되어 `dropwatch`·`perf trace`가 거짓 경보를 낸다. 실제로 잘 보낸 패킷은 consume 계열을 쓴다. `i6_eth`는 poll 안에서 `napi_consume_skb`, `ndo_stop` 정리에서는 `dev_kfree_skb_any`(못 보낸 것 = 드롭)를 썼다.

> 함정 — `skb->data`를 DMA 하기 전에: TX skb의 선형 부분은 `dma_map_single(dev, skb->data, skb_headlen(skb), DMA_TO_DEVICE)`, 각 frag는 `skb_frag_dma_map()`으로 매핑한다(H9 §3). 매핑 실패(`dma_mapping_error`)를 검사하지 않으면 IOMMU가 있는 SoC에서 조용히 쓰레기가 나간다.

---

## 3. 디스크립터 링과 소유권 — Don이 이미 아는 것을 Linux 말로

### 3.1 직관

NIC과 CPU는 **메모리 속 원형 배열**을 같이 본다. 칸 하나(디스크립터)에는 버퍼의 DMA 주소, 길이, 상태 비트가 있다. 핵심 규칙은 하나다: **한 칸은 한순간에 한 주인만 가진다.** 드라이버가 채우고 OWN 비트를 하드웨어에 넘기면 그때부터 드라이버는 그 칸을 만지지 않는다. 하드웨어가 일을 마치고 OWN을 돌려주면(write-back) 드라이버가 다시 가진다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 330"><defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">TX 링 (8칸) — 누가 이 칸을 소유하나</text><rect x="60" y="80" width="64" height="56" rx="2" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="92.0" y="104.2" font-size="12" text-anchor="middle">[0]</text><text x="92.0" y="120.2" font-size="12" text-anchor="middle">빈칸</text><rect x="130" y="80" width="64" height="56" rx="2" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="162.0" y="104.2" font-size="12" text-anchor="middle">[1]</text><text x="162.0" y="120.2" font-size="12" text-anchor="middle">빈칸</text><rect x="200" y="80" width="64" height="56" rx="2" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="232.0" y="104.2" font-size="12" text-anchor="middle">[2]</text><text x="232.0" y="120.2" font-size="12" text-anchor="middle">완료</text><rect x="270" y="80" width="64" height="56" rx="2" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="302.0" y="104.2" font-size="12" text-anchor="middle">[3]</text><text x="302.0" y="120.2" font-size="12" text-anchor="middle">완료</text><rect x="340" y="80" width="64" height="56" rx="2" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="372.0" y="104.2" font-size="12" text-anchor="middle">[4]</text><text x="372.0" y="120.2" font-size="12" text-anchor="middle">HW</text><rect x="410" y="80" width="64" height="56" rx="2" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="442.0" y="104.2" font-size="12" text-anchor="middle">[5]</text><text x="442.0" y="120.2" font-size="12" text-anchor="middle">HW</text><rect x="480" y="80" width="64" height="56" rx="2" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="512.0" y="104.2" font-size="12" text-anchor="middle">[6]</text><text x="512.0" y="120.2" font-size="12" text-anchor="middle">HW</text><rect x="550" y="80" width="64" height="56" rx="2" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="582.0" y="104.2" font-size="12" text-anchor="middle">[7]</text><text x="582.0" y="120.2" font-size="12" text-anchor="middle">빈칸</text><text x="232.0" y="186.0" font-size="12" text-anchor="middle">tx_clean</text><polyline points="232,170 232,140" fill="none" stroke="#3f9a6b" stroke-width="1.5" marker-end="url(#ah3)"/><text x="372.0" y="226.0" font-size="12" text-anchor="middle">tx_hw</text><polyline points="372,210 372,140" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/><text x="582.0" y="186.0" font-size="12" text-anchor="middle">tx_head</text><polyline points="582,170 582,140" fill="none" stroke="#4a7bd0" stroke-width="1.5" marker-end="url(#ah3)"/><text x="340.0" y="60.0" font-size="12" text-anchor="middle">인덱스는 계속 증가, 칸 번호 = 인덱스 &amp; (ring-1)</text><rect x="20" y="248" width="200" height="66" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="120.0" y="277.2" font-size="12" text-anchor="middle">드라이버(xmit)가 채움</text><text x="120.0" y="293.2" font-size="12" text-anchor="middle">own=1 → doorbell</text><rect x="240" y="248" width="200" height="66" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="340.0" y="277.2" font-size="12" text-anchor="middle">HW가 읽고 보냄</text><text x="340.0" y="293.2" font-size="12" text-anchor="middle">own=0 으로 돌려줌</text><rect x="460" y="248" width="200" height="66" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="560.0" y="277.2" font-size="12" text-anchor="middle">NAPI poll이 회수</text><text x="560.0" y="293.2" font-size="12" text-anchor="middle">skb free, BQL 완료</text><polyline points="220,281 238,281" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/><polyline points="440,281 458,281" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah3)"/></svg>
```
그림 3 — TX 링 8칸의 소유권. `tx_head`(드라이버가 다음에 쓸 칸) 앞의 칸들은 HW 소유([4]~[6], 보내는 중), 그 앞은 HW가 끝내고 돌려준 칸([2]~[3], 회수 대기), 나머지는 빈칸이다. 빈칸 수 = ring − (tx_head − tx_clean).

### 3.2 진짜 드라이버의 순서 — 배리어와 doorbell

TX 디스크립터 하나를 넘길 때 실제 드라이버는 이렇게 쓴다(개념 코드, 실행하지 않음 — 레지스터 이름은 가상).

```c
/* TX: fill descriptor, then hand ownership, then ring the doorbell */
dma = dma_map_single(dev, skb->data, skb_headlen(skb), DMA_TO_DEVICE);
if (dma_mapping_error(dev, dma))
	goto drop;
desc->addr = cpu_to_le64(dma);
desc->len  = cpu_to_le16(skb_headlen(skb));
dma_wmb();				/* addr/len visible before OWN */
desc->flags = cpu_to_le16(TX_OWN | TX_LAST);
ring->head++;
netdev_tx_sent_queue(txq, skb->len);	/* BQL */
writel(ring->head & mask, regs + TX_TAIL);	/* doorbell (writel has a wmb) */

/* RX poll: read status first, then the rest */
if (!(le16_to_cpu(desc->flags) & RX_DONE))
	break;				/* still owned by HW */
dma_rmb();				/* status read before len/data */
len = le16_to_cpu(desc->len);
```

| 단계 | 왜 필요한가 | Don의 경험으로 |
|---|---|---|
| `dma_wmb()` 후 OWN 설정 | 하드웨어가 OWN만 보고 주소/길이는 옛 값을 읽는 일을 막는다 | NVMe SQ 엔트리를 다 쓰고 나서 doorbell |
| `writel()` doorbell | MMIO 쓰기는 앞선 메모리 쓰기 뒤에 보이게 정렬된다(`writel`에 배리어 포함) | SQ tail doorbell 레지스터 |
| `dma_rmb()` 후 길이 읽기 | 상태 비트를 본 뒤의 읽기가 앞당겨지지 않게 | CQ phase bit 확인 후 엔트리 읽기 |
| doorbell 묶기(`netdev_xmit_more()`) | 스택이 "뒤에 더 온다"고 알려 주면 doorbell을 마지막에 한 번만 | SQ doorbell coalescing |

`i6_eth`는 하드웨어가 같은 CPU 위의 코드라서 이 배리어 대신 전역 spinlock을 쓴다(§0.1 함정). 하지만 **채우기 → 소유권 → doorbell**의 순서는 같다.

### 3.3 `i6_eth` 코드 5 — "하드웨어": IRQ 라인과 DMA 엔진

```c

/* "HW" asserts the port's IRQ line unless the driver masked it */
static void i6_raise_irq(struct i6_priv *p)
{
	if (!p->up || p->irq_masked)
		return;
	p->irq_masked = true;			/* like writing IMC in the ISR */
	p->st.irqs++;
	napi_schedule(&p->napi);		/* hardirq -> NET_RX softirq */
}

static bool i6_pending(struct i6_priv *p)
{
	return p->tx_clean != p->tx_hw || !p->rx[p->rx_next & MASK].own;
}

/* ---------- the hardware: runs in hardirq context (hrtimer) ---------- */
static enum hrtimer_restart i6_hw_tick(struct hrtimer *t)
{
	struct i6_priv *p = container_of(t, struct i6_priv, hw), *q = p->peer;
	bool tx_ev = false, rx_ev = false, more;
	unsigned int n;

	spin_lock(&i6_bus);
	p->st.hw_ticks++;
	for (n = 0; !stall && n < burst && p->tx_hw != p->tx_head; n++) {
		struct i6_txd *d = &p->tx[p->tx_hw & MASK];
		struct i6_rxd *r = &q->rx[q->rx_hw & MASK];

		if (q->up && r->own) {		/* peer posted an empty buffer */
			memcpy(page_address(r->page) + I6_HEADROOM, d->skb->data, d->len);
			r->len = d->len;
			r->own = 0;			/* write-back: now SW owns it */
			q->rx_hw++;
			rx_ev = true;
		} else {
			q->st.rx_missed++;		/* no descriptor: frame lost */
		}
		d->own = 0;
		p->tx_hw++;
		tx_ev = true;
	}
	if (tx_ev)
		i6_raise_irq(p);
	if (rx_ev)
		i6_raise_irq(q);
	more = !stall && p->tx_hw != p->tx_head;
	spin_unlock(&i6_bus);
	if (!more)
		return HRTIMER_NORESTART;
	hrtimer_forward_now(t, ns_to_ktime(p->tick_ns));
	return HRTIMER_RESTART;
}

```

코드에서 볼 것:

- `i6_raise_irq()`는 진짜 NIC의 "인터럽트 원인 레지스터 + 마스크 레지스터"를 한 줄로 흉내 낸다. 마스크가 아니면 IRQ 카운터를 올리고 곧바로 **마스크한 뒤** `napi_schedule()`. 진짜 드라이버의 ISR이 하는 일과 정확히 같다(§5).
- `i6_hw_tick()`은 hrtimer 콜백이라 **hardirq 문맥**에서 돈다. 진짜 MAC의 DMA 엔진처럼: TX 칸을 하나씩 읽어 상대 RX 버퍼(미리 깔린 페이지)에 복사하고, RX 칸을 SW에 넘기고(`own = 0`), TX 칸도 돌려준다.
- 상대 RX 링에 빈 버퍼가 없으면(`!r->own`) 프레임을 버리고 `rx_missed`를 올린다. 이것이 진짜 NIC의 **rx_missed_errors / rx_fifo_errors** — 드라이버가 RX 버퍼를 제때 다시 깔아 주지 못하면 하드웨어 단계에서 패킷이 사라진다.
- `burst`는 한 틱에 옮길 최대 프레임 수 = 선로 속도. `stall`이면 아무것도 안 한다(§4.5에서 TX timeout 재현용).

---

## 4. TX 경로 — `sendto()`에서 TX 완료까지

### 4.1 직관

TX의 큰 흐름은 "스택이 skb를 만들고 → qdisc에 줄 세우고 → 드라이버가 링에 넣고 → 하드웨어가 보내고 → 완료되면 회수"다. 중요한 것은 **드라이버가 스택에 '그만 보내'라고 말하는 방법**(`netif_stop_queue`)과 다시 '보내도 돼'라고 말하는 방법(`netif_wake_queue`)이다. 이 둘이 TX 쪽 흐름 제어의 전부다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 560"><defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">TX 경로와 역압력(backpressure)</text><rect x="40" y="40" width="330" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="205.0" y="64.2" font-size="12" text-anchor="middle">sendto() → udp_sendmsg → ip_output</text><rect x="40" y="92" width="330" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="205.0" y="116.2" font-size="12" text-anchor="middle">neigh_resolve_output (ARP 해결)</text><polyline points="205,80 205,90" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/><rect x="40" y="144" width="330" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="205.0" y="168.2" font-size="12" text-anchor="middle">__dev_queue_xmit → qdisc enqueue</text><polyline points="205,132 205,142" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/><rect x="40" y="196" width="330" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="205.0" y="220.2" font-size="12" text-anchor="middle">qdisc_run → sch_direct_xmit</text><polyline points="205,184 205,194" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/><rect x="40" y="248" width="330" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="205.0" y="272.2" font-size="12" text-anchor="middle">dev_hard_start_xmit → ndo_start_xmit</text><polyline points="205,236 205,246" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/><rect x="40" y="300" width="330" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="205.0" y="324.2" font-size="12" text-anchor="middle">디스크립터 기록 + own=HW + doorbell</text><polyline points="205,288 205,298" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/><rect x="40" y="352" width="330" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="205.0" y="376.2" font-size="12" text-anchor="middle">HW: DMA로 읽어 선로로 송신</text><polyline points="205,340 205,350" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/><rect x="40" y="404" width="330" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="205.0" y="428.2" font-size="12" text-anchor="middle">TX 완료 IRQ → napi_schedule</text><polyline points="205,392 205,402" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/><rect x="40" y="456" width="330" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="205.0" y="480.2" font-size="12" text-anchor="middle">poll: skb free + BQL 완료 + wake</text><polyline points="205,444 205,454" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah4)"/><rect x="410" y="196" width="250" height="64" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="535.0" y="224.2" font-size="12" text-anchor="middle">링 가득 → netif_stop_queue</text><text x="535.0" y="240.2" font-size="12" text-anchor="middle">(드라이버가 큐를 멈춤)</text><rect x="410" y="100" width="250" height="64" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="535.0" y="128.2" font-size="12" text-anchor="middle">qdisc에 쌓임(backlog)</text><text x="535.0" y="144.2" font-size="12" text-anchor="middle">→ 넘치면 drop</text><rect x="410" y="40" width="250" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="535.0" y="66.2" font-size="12" text-anchor="middle">소켓 sndbuf 가득 → sendto 대기</text><polyline points="410,228 372,228" fill="none" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="5,4" marker-end="url(#ah4)"/><polyline points="535,196 535,166" fill="none" stroke="#d0564a" stroke-width="1.5" marker-end="url(#ah4)"/><polyline points="535,100 535,86" fill="none" stroke="#d0564a" stroke-width="1.5" marker-end="url(#ah4)"/><rect x="410" y="456" width="250" height="64" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="535.0" y="484.2" font-size="12" text-anchor="middle">netif_wake_queue</text><text x="535.0" y="500.2" font-size="12" text-anchor="middle">(빈칸이 1/4 이상이면)</text><polyline points="372,480 408,480" fill="none" stroke="#3f9a6b" stroke-width="1.5" marker-end="url(#ah4)"/><polyline points="535,456 535,262" fill="none" stroke="#3f9a6b" stroke-width="1.5" stroke-dasharray="5,4" marker-end="url(#ah4)"/><text x="340.0" y="548.0" font-size="12" text-anchor="middle">BQL은 링이 차기 전에 '바이트 기준'으로 먼저 멈춘다 (§4.4)</text></svg>
```
그림 4 — TX 경로(왼쪽)와 역압력(오른쪽). 링이 차면 드라이버가 큐를 멈추고, 그러면 패킷은 qdisc에 쌓이고, qdisc가 차거나 소켓 송신 버퍼(sndbuf)가 차면 결국 `sendto()`가 잠든다.

### 4.2 `i6_eth` 코드 6 — `ndo_start_xmit`

```c
/* ---------- TX: called by the qdisc layer with the TX queue lock held ---------- */
static netdev_tx_t i6_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct i6_priv *p = netdev_priv(dev);
	unsigned long f;
	struct i6_txd *d;

	if (skb_put_padto(skb, ETH_ZLEN)) {	/* frees skb on failure */
		dev_core_stats_tx_dropped_inc(dev);
		return NETDEV_TX_OK;
	}
	if (stack && !p->st.tx_packets)
		dump_stack();
	skb_tx_timestamp(skb);			/* SW TX timestamp point */
	spin_lock_irqsave(&i6_bus, f);
	d = &p->tx[p->tx_head & MASK];
	d->skb = skb;
	d->len = skb->len;
	p->st.tx_packets++;
	p->st.tx_bytes += skb->len;
	netdev_sent_queue(dev, skb->len);	/* BQL: bytes now in flight */
	d->own = 1;				/* hand the descriptor to HW */
	p->tx_head++;
	if (ring - (p->tx_head - p->tx_clean) == 0) {	/* ring full */
		netif_stop_queue(dev);
		p->st.tx_stops++;
	}
	if (!hrtimer_is_queued(&p->hw))		/* "doorbell": write tail reg */
		hrtimer_start(&p->hw, ns_to_ktime(p->tick_ns), HRTIMER_MODE_REL);
	spin_unlock_irqrestore(&i6_bus, f);
	return NETDEV_TX_OK;
}

```

코드에서 볼 것:

- `ndo_start_xmit`은 **TX 큐 락(`HARD_TX_LOCK`)을 잡은 채, BH가 꺼진 상태**에서 불린다. 잠들 수 없다(`GFP_KERNEL` 할당, mutex 금지).
- 반환값은 거의 항상 `NETDEV_TX_OK`다. skb를 받았으면 보내든 버리든 **드라이버가 책임진다**. `NETDEV_TX_BUSY`는 "skb를 안 받았으니 다시 줘"라는 뜻인데, 큐를 미리 멈추지 않아서 생긴 버그 상황용이고 커널 문서도 쓰지 말라고 한다. 올바른 패턴은 **"넣은 다음 남은 칸을 보고 미리 멈추기"** — 위 코드의 `if (ring - … == 0) netif_stop_queue()`, FEC 드라이버의 `if (entries_free <= txq->tx_stop_threshold) netif_tx_stop_queue(nq)`가 그것이다.
- `skb_put_padto(skb, ETH_ZLEN)`: 60바이트보다 짧은 프레임을 0으로 채운다. 많은 MAC이 자동 패딩을 하지만 안 하는 MAC도 있다. 실패하면 skb를 이미 해제했으므로 그냥 OK를 돌려준다.
- `skb_tx_timestamp(skb)`: 소켓이 SW TX 타임스탬프를 요청했으면 **여기서** 찍힌다(§9). 하드웨어로 넘기기 직전이 원칙이다.
- `netdev_sent_queue()`: BQL에 "이만큼 바이트가 하드웨어로 갔다"고 알린다(§4.4).

실제 i.MX8 FEC 드라이버의 xmit은 이렇게 생겼다(6.18 `drivers/net/ethernet/freescale/fec_main.c`에서 발췌).

```c
fec_enet_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	...
	queue = skb_get_queue_mapping(skb);
	txq = fep->tx_queue[queue];
	nq = netdev_get_tx_queue(ndev, queue);

	if (skb_is_gso(skb))
		ret = fec_enet_txq_submit_tso(txq, skb, ndev);
	else
		ret = fec_enet_txq_submit_skb(txq, skb, ndev);
	if (ret)
		return ret;

	entries_free = fec_enet_get_free_txdesc_num(txq);
	if (entries_free <= txq->tx_stop_threshold)
		netif_tx_stop_queue(nq);

	return NETDEV_TX_OK;
}
```

멀티큐 장치는 `skb_get_queue_mapping()`으로 스택이 고른 큐 번호를 받아 그 큐의 링에 넣고, 큐 단위로 `netif_tx_stop_queue(nq)`를 부른다는 점만 다르다.

### 4.3 `i6_eth` 코드 7 — TX 완료(NAPI poll의 앞부분)

```c
/* ---------- NAPI poll: softirq context ---------- */
static int i6_poll(struct napi_struct *napi, int budget)
{
	struct i6_priv *p = container_of(napi, struct i6_priv, napi);
	struct net_device *dev = p->dev;
	unsigned int pkts = 0, bytes = 0;
	u32 hw, i;
	int work = 0;

	/* 1) TX completion: descriptors HW gave back -> free skbs */
	spin_lock_irq(&i6_bus);
	hw = p->tx_hw;
	spin_unlock_irq(&i6_bus);
	for (i = p->tx_clean; i != hw; i++) {
		struct i6_txd *d = &p->tx[i & MASK];

		pkts++;
		bytes += d->len;
		napi_consume_skb(d->skb, budget);
		d->skb = NULL;
	}
	netdev_completed_queue(dev, pkts, bytes);	/* BQL */
	spin_lock_irq(&i6_bus);
	p->tx_clean = hw;
	if (netif_queue_stopped(dev) && ring - (p->tx_head - p->tx_clean) >= ring / 4) {
		netif_wake_queue(dev);
		p->st.tx_wakes++;
	}
	spin_unlock_irq(&i6_bus);

```

코드에서 볼 것:

- TX 완료는 RX와 같은 NAPI poll 안에서 처리한다(e1000e, FEC, stmmac 모두 이렇게 한다). TX 완료 작업은 `budget`에 세지 않는 것이 관례다.
- 하드웨어가 돌려준 칸까지(`hw` 스냅샷) skb를 `napi_consume_skb()`로 해제하고, BQL에 완료를 알리고(`netdev_completed_queue`), 큐가 멈춰 있으면 **빈칸이 1/4 이상 생겼을 때** 깨운다. 한 칸 비자마자 깨우면 stop/wake가 패킷마다 반복되는 핑퐁이 생긴다(hysteresis).
- `netif_wake_queue()`는 qdisc를 다시 돌리도록 `NET_TX_SOFTIRQ`를 예약한다.

> 함정 — stop/wake 경쟁: 진짜 드라이버는 락 없이 xmit(CPU A)과 완료(CPU B)가 동시에 돈다. "A가 링이 찼다고 보고 멈추기 직전에 B가 전부 회수하고 깨우기 검사를 끝내 버리는" 경쟁으로 큐가 영원히 멈출 수 있다. 그래서 커널은 `netif_txq_maybe_stop()` / `__netif_txq_completed_wake()`(`include/net/netdev_queues.h`) 같은 배리어가 들어간 헬퍼를 제공한다 — 새 드라이버는 이것을 쓴다.

### 4.4 BQL과 역압력 — 예제 7

**BQL(Byte Queue Limits)** 은 "하드웨어 링에 몇 바이트까지 넣어 둘지"를 동적으로 정한다. 링이 1024칸이라도 그걸 다 채우면 지연(bufferbloat)이 커지므로, BQL은 TX 완료 속도를 보고 "이 정도면 하드웨어가 놀지 않는다"는 최소 바이트만 허용하고 나머지는 qdisc에 두게 한다. qdisc에 있어야 우선순위·공정성 스케줄링(fq_codel, mqprio, taprio)이 효과를 낸다. 드라이버가 할 일은 `netdev_tx_sent_queue()`(xmit)와 `netdev_tx_completed_queue()`(완료) 두 줄, 그리고 링을 비울 때 `netdev_tx_reset_queue()`다.

실험: 링 16칸, 선로 1000 pps(`tick_us=1000 burst=1`)인 `i6_eth`에 UDP를 최대 속도로 2000개 보낸다. 1단계는 BQL 기본값, 2단계는 `limit_min`을 1 MB로 올려 BQL을 사실상 끈다. qdisc 통계는 BusyBox에 `tc`가 없어 rtnetlink `RTM_GETQDISC`로 직접 읽었다.

```c
/* i6_qdisc: dump qdisc stats over rtnetlink (what `tc -s qdisc` shows) */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/pkt_sched.h>

int main(int argc, char **argv)
{
	int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	struct { struct nlmsghdr n; struct tcmsg t; } req = {
		.n = { .nlmsg_len = sizeof(req), .nlmsg_type = RTM_GETQDISC,
		       .nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP },
		.t = { .tcm_family = AF_UNSPEC } };
	static char buf[32768];
	int want = argc > 1 ? (int)if_nametoindex(argv[1]) : 0;
	if (fd < 0 || send(fd, &req, sizeof(req), 0) < 0) { perror("netlink"); return 1; }
	for (;;) {
		int len = recv(fd, buf, sizeof(buf), 0);
		if (len < 0) { perror("recv"); return 1; }
		for (struct nlmsghdr *h = (void *)buf; NLMSG_OK(h, len); h = NLMSG_NEXT(h, len)) {
			if (h->nlmsg_type == NLMSG_DONE) return 0;
			if (h->nlmsg_type == NLMSG_ERROR) { fprintf(stderr, "nlmsg error\n"); return 1; }
			struct tcmsg *t = NLMSG_DATA(h);
			if (want && t->tcm_ifindex != want) continue;
			char name[IF_NAMESIZE] = "?", kind[16] = "?";
			struct tc_stats st = { 0 };
			if_indextoname(t->tcm_ifindex, name);
			int alen = h->nlmsg_len - NLMSG_LENGTH(sizeof(*t));
			for (struct rtattr *a = TCA_RTA(t); RTA_OK(a, alen); a = RTA_NEXT(a, alen)) {
				if (a->rta_type == TCA_KIND) snprintf(kind, sizeof(kind), "%s", (char *)RTA_DATA(a));
				if (a->rta_type == TCA_STATS) memcpy(&st, RTA_DATA(a), sizeof(st));
			}
			printf("qdisc %s dev %s parent %x:%x: sent %llu pkt %u dropped %u overlimits %u backlog %ub %up\n",
			       kind, name, t->tcm_parent >> 16, t->tcm_parent & 0xffff,
			       (unsigned long long)st.bytes, st.packets, st.drops, st.overlimits, st.backlog, st.qlen);
		}
	}
}
```

UDP 송수신 도구(이후 예제에서도 계속 쓴다):

```c
/* i6_udp: paced UDP sender / counting receiver
 *   i6_udp tx <ip> <port> <pps|0=flat out> <count> [len]
 *   i6_udp rx <port> <idle_ms>          (exits after idle_ms without packets) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static double now(void)
{
	struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

int main(int argc, char **argv)
{
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in a = { .sin_family = AF_INET };
	static char buf[2048];
	if (fd < 0) { perror("socket"); return 1; }
	if (argc >= 6 && !strcmp(argv[1], "tx")) {
		long pps = atol(argv[4]), n = atol(argv[5]), len = argc > 6 ? atol(argv[6]) : 64, ok = 0, err = 0;
		inet_pton(AF_INET, argv[2], &a.sin_addr); a.sin_port = htons(atoi(argv[3]));
		double t0 = now();
		for (long i = 0; i < n; i++) {
			if (pps) {
				double due = t0 + (double)i / pps, d = due - now();
				if (d > 0) { struct timespec ts = { (time_t)d, (long)((d - (time_t)d) * 1e9) }; nanosleep(&ts, NULL); }
			}
			memcpy(buf, &i, sizeof(i));
			if (sendto(fd, buf, len, 0, (struct sockaddr *)&a, sizeof(a)) < 0) err++; else ok++;
		}
		double dt = now() - t0;
		printf("tx: sent %ld err %ld in %.3f s = %.0f pps (target %ld)\n", ok, err, dt, ok / dt, pps);
	} else if (argc == 4 && !strcmp(argv[1], "rx")) {
		int ms = atoi(argv[3]);
		struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
		a.sin_port = htons(atoi(argv[2]));
		if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { perror("bind"); return 1; }
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		long got = 0, last = -1, gaps = 0; double t0 = 0;
		for (;;) {
			ssize_t r = recv(fd, buf, sizeof(buf), 0);
			if (r < 0) { if (errno == EAGAIN && got) break; if (errno == EAGAIN) continue; perror("recv"); return 1; }
			if (!got++) t0 = now();
			long seq; memcpy(&seq, buf, sizeof(seq));
			if (seq != last + 1) gaps++;
			last = seq;
		}
		printf("rx: got %ld last_seq %ld gaps %ld in %.3f s\n", got, last, gaps, now() - t0);
	} else { fprintf(stderr, "bad args\n"); return 1; }
	return 0;
}
```

게스트 스크립트 `t4_bp.sh`:

```sh
cd /i6
insmod i6_eth.ko ring=16 tick_us=1000 burst=1
unshare -n sleep 600 & NS=$!
sleep 0.3
ip link set i6_eth1 netns $NS
ip addr add 10.6.0.1/24 dev i6_eth0; ip link set i6_eth0 up
nsenter -t $NS -n sh -c 'ip link set lo up; ip addr add 10.6.0.2/24 dev i6_eth1; ip link set i6_eth1 up'
sleep 2; ping -c 1 10.6.0.2 > /dev/null
Q=/sys/class/net/i6_eth0/queues/tx-0/byte_queue_limits
phase() {
  nsenter -t $NS -n ./i6_udp rx 5000 1500 > /tmp/rx & R=$!; sleep 0.3
  ./i6_udp tx 10.6.0.2 5000 0 2000 64 > /tmp/tx & T=$!
  for i in 1 2 3; do
    sleep 0.5
    echo "--- t=${i}x0.5s"; ./i6_qdisc i6_eth0
    echo "bql limit $(cat $Q/limit) inflight $(cat $Q/inflight); sender wchan=$(cat /proc/$T/wchan)"
    ./i6_ethtool -S i6_eth0 | grep -E "tx_queue|tx_packets" | tr '\n' ' '; echo
  done
  wait $T; cat /tmp/tx; wait $R; cat /tmp/rx
}
echo "### phase 1: BQL active (default)"; phase
echo "### phase 2: BQL limit_min=1000000 (BQL effectively off)"; echo 1000000 > $Q/limit_min; phase
echo "### after"; ./i6_qdisc i6_eth0
kill $NS
```

```text
### phase 1: BQL active (default)
--- t=1x0.5s
qdisc pfifo_fast dev i6_eth0 parent ffff:ffff: sent 52504 pkt 496 dropped 0 overlimits 0 backlog 11554b 109p
bql limit 318 inflight 424; sender wchan=sock_alloc_send_pskb
  tx_packets: 598   tx_queue_stops: 0   tx_queue_wakes: 0 
--- t=2x0.5s
qdisc pfifo_fast dev i6_eth0 parent ffff:ffff: sent 119284 pkt 1126 dropped 0 overlimits 0 backlog 13038b 124p
bql limit 318 inflight 424; sender wchan=sock_alloc_send_pskb
  tx_packets: 1191   tx_queue_stops: 0   tx_queue_wakes: 0 
--- t=3x0.5s
qdisc pfifo_fast dev i6_eth0 parent ffff:ffff: sent 183096 pkt 1728 dropped 0 overlimits 0 backlog 13144b 124p
bql limit 318 inflight 424; sender wchan=sock_alloc_send_pskb
  tx_packets: 1785   tx_queue_stops: 0   tx_queue_wakes: 0 
tx: sent 2000 err 0 in 1.970 s = 1015 pps (target 0)
rx: got 2000 last_seq 1999 gaps 0 in 3.623 s
### phase 2: BQL limit_min=1000000 (BQL effectively off)
--- t=1x0.5s
qdisc pfifo_fast dev i6_eth0 parent ffff:ffff: sent 268850 pkt 2537 dropped 0 overlimits 0 backlog 11236b 106p
bql limit 1000000 inflight 1590; sender wchan=sock_alloc_send_pskb
  tx_packets: 2554   tx_queue_stops: 134   tx_queue_wakes: 133 
--- t=2x0.5s
qdisc pfifo_fast dev i6_eth0 parent ffff:ffff: sent 329630 pkt 3111 dropped 0 overlimits 0 backlog 13992b 132p
bql limit 1000000 inflight 1378; sender wchan=sock_alloc_send_pskb
  tx_packets: 3130   tx_queue_stops: 278   tx_queue_wakes: 277 
--- t=3x0.5s
qdisc pfifo_fast dev i6_eth0 parent ffff:ffff: sent 390686 pkt 3687 dropped 0 overlimits 0 backlog 17172b 162p
bql limit 1000000 inflight 1378; sender wchan=sock_alloc_send_pskb
  tx_packets: 3718   tx_queue_stops: 425   tx_queue_wakes: 424 
tx: sent 2000 err 0 in 1.866 s = 1072 pps (target 0)
rx: got 2000 last_seq 1999 gaps 0 in 3.558 s
### after
qdisc pfifo_fast dev i6_eth0 parent ffff:ffff: sent 424182 pkt 4003 dropped 0 overlimits 0 backlog 0b 0p
[   11.851929] reboot: Power down
```

출력에서 볼 것:

- **1단계(BQL 켬)**: `inflight 424` = 106바이트 프레임 4개. BQL이 하드웨어에 4개만 허용하고 `limit 318`을 넘으면 큐를 멈췄다(`__QUEUE_STATE_STACK_XOFF`). 링 16칸 중 4칸만 쓰므로 **드라이버의 `netif_stop_queue`는 한 번도 불리지 않았다**(`tx_queue_stops: 0`). 나머지 100~124개는 qdisc에 쌓였다(`backlog 13038b 124p`).
- **2단계(BQL 끔)**: `inflight 1590` ≈ 15개, 링이 꽉 차서 드라이버가 큐를 멈춘다 — 0.5초 사이 `tx_queue_stops`가 134 → 278 → 425로 늘고 wake도 같은 수만큼 늘었다(빈칸 1/4 = 4칸 생길 때마다 깨움).
- 두 단계 모두 송신 프로세스는 `sock_alloc_send_pskb`에서 잠들어 있다. UDP 소켓의 송신 버퍼(`SO_SNDBUF`, skb `truesize` 합)가 차서 `sendto()`가 블록된 것이다. 그래서 **qdisc는 한도(1000개)까지 가지 않았고 `dropped 0`**, 2000개 모두 상대에게 도착했다(`gaps 0`). 역압력이 링 → BQL/큐 정지 → qdisc → 소켓까지 거꾸로 전달된 것을 숫자로 본 것이다.
- 송신 속도는 두 단계 모두 약 1000 pps = 선로 속도. 아무리 빨리 보내도 병목(선로)보다 빠를 수 없다.

> 함정: 블로킹이 아닌 소켓(`O_NONBLOCK`)이나 여러 소켓이 한 장치를 같이 쓰면 qdisc가 넘쳐 `dropped`가 생기고, `sendto()`는 기본적으로 오류 없이 성공한다. "보낸 쪽은 성공했는데 상대가 못 받았다"면 `tc -s qdisc`(여기서는 `i6_qdisc`)의 dropped부터 본다.

### 4.5 TX timeout — 예제 8

TX 큐가 멈춘 채 `watchdog_timeo`(우리는 2초)가 지나면 커널의 `dev_watchdog`(타이머)가 `ndo_tx_timeout`을 부른다. "하드웨어가 디스크립터를 안 가져간다 = DMA 엔진이 멈췄다"는 신호이고, 실제 드라이버는 workqueue에서 MAC을 리셋하고 링을 다시 만든다.

게스트 스크립트 `t5_timeout.sh`: 엔진을 멈추고(`stall=1`) 40개를 보낸 뒤 기다린다.

```sh
cd /i6
insmod i6_eth.ko ring=16
unshare -n sleep 600 & NS=$!
sleep 0.3
ip link set i6_eth1 netns $NS
ip addr add 10.6.0.1/24 dev i6_eth0; ip link set i6_eth0 up
nsenter -t $NS -n sh -c 'ip link set lo up; ip addr add 10.6.0.2/24 dev i6_eth1; ip link set i6_eth1 up'
sleep 2; ping -c 1 10.6.0.2 > /dev/null
echo "### stall the engine"; echo 1 > /sys/module/i6_eth/parameters/stall
./i6_udp tx 10.6.0.2 5000 0 40 64
./i6_ethtool -S i6_eth0 | grep -E "tx_|irqs"
./i6_qdisc i6_eth0
echo "### wait for watchdog"; sleep 6
dmesg | grep -iE "watchdog|timeout|i6_eth0" | tail -6
cat /sys/module/i6_eth/parameters/stall
./i6_ethtool -S i6_eth0 | grep -E "tx_|irqs"
./i6_qdisc i6_eth0
cat /sys/class/net/i6_eth0/queues/tx-0/tx_timeout
kill $NS
```

```text
### stall the engine
tx: sent 40 err 0 in 0.004 s = 9866 pps (target 0)
  irqs: 3
  tx_packets: 4
  tx_bytes: 370
  tx_queue_stops: 0
  tx_queue_wakes: 0
  tx_timeouts: 0
qdisc pfifo_fast dev i6_eth0 parent ffff:ffff: sent 458 pkt 5 dropped 0 overlimits 0 backlog 4028b 38p
### wait for watchdog
[    5.891177] i6_eth i6_eth i6_eth0: NETDEV WATCHDOG: CPU: 0: transmit queue 0 timed out 2340 ms
[    5.891530] i6_eth i6_eth i6_eth0: TX timeout on queue 0: head=4 hw=2 clean=2 -> reset HW
[    1.071370] i6_eth i6_eth: cable i6_eth0 <-> i6_eth1 ring=16 tick=20us burst=64 weight=64
[    1.467533] i6_eth i6_eth i6_eth0: Link is Up - 1Gbps/Full - flow control off
[    5.891177] i6_eth i6_eth i6_eth0: NETDEV WATCHDOG: CPU: 0: transmit queue 0 timed out 2340 ms
[    5.891530] i6_eth i6_eth i6_eth0: TX timeout on queue 0: head=4 hw=2 clean=2 -> reset HW
N
  irqs: 13
  tx_packets: 43
  tx_bytes: 4458
  tx_queue_stops: 1
  tx_queue_wakes: 1
  tx_timeouts: 1
qdisc pfifo_fast dev i6_eth0 parent ffff:ffff: sent 4422 pkt 43 dropped 0 overlimits 0 backlog 0b 0p
1
[    9.690880] reboot: Power down
```

출력에서 볼 것:

- 40개를 보냈는데 드라이버에 도착한 것은 4개(`tx_packets 4`, ping 포함)이고 38개는 qdisc에 있다(`backlog 4028b 38p`). 엔진이 멈췄으니 BQL 완료가 없어 큐가 멈췄다. 링(16칸)은 아직 여유가 있는데도 큐가 멈춘 것 — 이번에도 BQL이 먼저 막았다.
- 약 2.3초 후 커널이 `NETDEV WATCHDOG: CPU: 0: transmit queue 0 timed out 2340 ms`를 찍고 우리 `i6_tx_timeout`이 불렸다(`head=4 hw=2 clean=2`: 2칸이 HW 소유인 채 멈춰 있었다). 우리는 "리셋"으로 `stall`을 풀고 엔진을 다시 돌렸다.
- 이후 `tx_packets 43`, `backlog 0`, sysfs `tx_timeout` 카운터 1. 멈췄던 큐가 풀리며 쌓였던 패킷이 모두 나갔다.

> 함정: NETDEV WATCHDOG 메시지가 보이면 "드라이버 버그"부터 의심하기 쉽지만, 실무에서는 **PHY 링크가 내려갔는데 carrier를 끄지 않은 경우**(watchdog은 carrier가 꺼져 있으면 검사하지 않는다), 클록 게이팅으로 MAC이 멈춘 경우, IOMMU fault로 DMA가 막힌 경우가 흔하다. `dmesg`의 IOMMU 오류와 PHY 상태를 같이 본다.

### 4.6 TX 경로를 콜 스택으로 확인 — 예제 4의 일부

`stack=1`로 넣으면 포트마다 첫 TX와 첫 RX에서 `dump_stack()`을 찍는다. ping의 첫 패킷은 ARP 요청이다(예제 4 전체는 §5.4).

```text
[    3.269489] Call trace:
[    3.269606]  show_stack+0x28/0x7c (C)
[    3.269959]  dump_stack_lvl+0x60/0x80
[    3.269982]  dump_stack+0x18/0x24
[    3.269991]  i6_xmit+0x148/0x340 [i6_eth]
[    3.270237]  dev_hard_start_xmit+0xa0/0x11c
[    3.270249]  sch_direct_xmit+0x8c/0x37c
[    3.270258]  __dev_queue_xmit+0x9f8/0xec4
[    3.270266]  arp_xmit+0x2c/0x70
[    3.270276]  arp_send_dst+0x70/0xb0
[    3.270287]  arp_solicit+0xec/0x258
[    3.270297]  neigh_probe+0x58/0xc0
[    3.270305]  __neigh_event_send+0x2ec/0x370
[    3.270327]  neigh_resolve_output+0x110/0x1c0
[    3.270337]  ip_finish_output2+0x1f4/0x6bc
[    3.270345]  __ip_finish_output+0xa0/0x2a0
[    3.270353]  ip_finish_output+0x2c/0x140
[    3.270361]  ip_output+0x70/0x11c
[    3.270368]  ip_send_skb+0x44/0xe0
[    3.270375]  ip_push_pending_frames+0x30/0x60
[    3.270382]  raw_sendmsg+0x4b0/0xc64
[    3.270390]  inet_sendmsg+0x40/0x64
[    3.270398]  __sys_sendto+0x11c/0x190
[    3.270409]  __arm64_sys_sendto+0x24/0x40
[    3.270420]  invoke_syscall.constprop.0+0x40/0xe0
[    3.270429]  el0_svc_common.constprop.0+0x38/0xe0
[    3.270436]  do_el0_svc+0x1c/0x28
[    3.270443]  el0_svc+0x38/0x118
[    3.270452]  el0t_64_sync_handler+0xa0/0xe4
[    3.270459]  el0t_64_sync+0x198/0x19c
```

출력에서 볼 것(아래에서 위로): `sendto` syscall → `raw_sendmsg`(ping은 raw 소켓) → `ip_output` → `neigh_resolve_output`(MAC 주소를 모르니 ARP부터) → `arp_solicit` → `__dev_queue_xmit` → `sch_direct_xmit`(qdisc가 비어 있어 바로 드라이버로: "bypass") → `dev_hard_start_xmit` → **`i6_xmit`**. G1 §6에서 본 경로와 같고, ARP를 먼저 보내는 단계가 추가됐다.

---

## 5. RX 경로와 NAPI

### 5.1 직관 — 왜 NAPI인가

패킷마다 인터럽트를 받으면 초당 수십만 번의 IRQ 진입·탈출 비용이 CPU를 다 먹는다(receive livelock: 인터럽트 처리만 하다가 실제 패킷 처리를 못 하는 상태). **NAPI**(New API)는 이렇게 푼다.

1. 첫 패킷이 오면 IRQ가 뜬다. ISR은 **그 장치의 IRQ를 끄고(mask)** `napi_schedule()`만 한다.
2. softirq(`NET_RX_SOFTIRQ`)에서 드라이버의 `poll(napi, budget)`이 불린다. poll은 RX 링에서 **최대 budget개(기본 64)** 를 꺼내 스택에 올린다.
3. budget보다 적게 처리했으면 "링이 비었다" → `napi_complete_done()` 후 IRQ를 다시 켠다.
4. budget을 다 썼으면 "아직 남았다" → IRQ는 꺼 둔 채 다음 차례에 또 poll 된다.

부하가 낮으면 인터럽트 방식, 부하가 높으면 폴링 방식으로 **자동 전환**되는 것이다. Don의 펌웨어 경험으로는 "ISR에서는 플래그만 세우고, 메인 루프가 큐를 비운 뒤에 인터럽트를 다시 enable"하는 패턴과 같다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 420"><defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">NAPI — 인터럽트로 깨우고, 폴링으로 비운다</text><rect x="30" y="60" width="190" height="64" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="125.0" y="88.0" font-size="13" text-anchor="middle">대기: IRQ 켜짐</text><text x="125.0" y="105.0" font-size="13" text-anchor="middle">(NAPI 미예약)</text><rect x="260" y="60" width="190" height="64" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="355.0" y="88.2" font-size="12" text-anchor="middle">IRQ 핸들러:</text><text x="355.0" y="104.2" font-size="12" text-anchor="middle">IRQ mask + napi_schedule</text><rect x="490" y="60" width="170" height="64" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="575.0" y="88.2" font-size="12" text-anchor="middle">NET_RX softirq</text><text x="575.0" y="104.2" font-size="12" text-anchor="middle">net_rx_action</text><rect x="400" y="200" width="260" height="64" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="530.0" y="228.2" font-size="12" text-anchor="middle">poll(napi, budget=64)</text><text x="530.0" y="244.2" font-size="12" text-anchor="middle">TX 회수 + RX 최대 budget개</text><rect x="30" y="200" width="300" height="64" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="180.0" y="228.2" font-size="12" text-anchor="middle">work &lt; budget:</text><text x="180.0" y="244.2" font-size="12" text-anchor="middle">napi_complete_done → IRQ unmask</text><rect x="400" y="320" width="260" height="64" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="530.0" y="348.2" font-size="12" text-anchor="middle">work == budget:</text><text x="530.0" y="364.2" font-size="12" text-anchor="middle">IRQ 끈 채로 다시 poll</text><rect x="30" y="320" width="300" height="64" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="180.0" y="348.2" font-size="12" text-anchor="middle">softirq 예산 소진(300개 / 8ms)</text><text x="180.0" y="364.2" font-size="12" text-anchor="middle">→ ksoftirqd 스레드가 이어서</text><polyline points="220,92 258,92" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah5)"/><text x="239.0" y="82.0" font-size="12" text-anchor="middle">패킷</text><polyline points="450,92 488,92" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah5)"/><polyline points="575,124 575,198" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah5)"/><polyline points="400,232 332,232" fill="none" stroke="#3f9a6b" stroke-width="1.5" marker-end="url(#ah5)"/><polyline points="125,200 125,126" fill="none" stroke="#3f9a6b" stroke-width="1.5" marker-end="url(#ah5)"/><polyline points="530,264 530,318" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah5)"/><polyline points="620,318 620,266" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah5)"/><text x="642.0" y="296.0" font-size="12" text-anchor="middle">재poll</text><polyline points="400,352 332,352" fill="none" stroke="currentColor" stroke-width="1.5" stroke-dasharray="5,4" marker-end="url(#ah5)"/></svg>
```
그림 5 — NAPI 상태 전이. 핵심은 "IRQ로 깨우고, 끈 채로 폴링하다가, 비면 다시 켠다". `net_rx_action`은 한 번에 모든 장치 합쳐 `netdev_budget`(300개)이나 `netdev_budget_usecs`(이 커널은 HZ=250이라 2 jiffy = 8000 µs)를 넘기면 멈추고 나머지를 다음 softirq 또는 `ksoftirqd`로 넘긴다(그때 `/proc/net/softnet_stat`의 time_squeeze가 오른다).

### 5.2 NAPI API 정리

| 함수 | 어디서 | 하는 일 |
|---|---|---|
| `netif_napi_add(_weight)(dev, napi, poll, w)` | probe | 등록 (weight 기본 `NAPI_POLL_WEIGHT` = 64) |
| `napi_enable` / `napi_disable` | open / stop | `napi_disable`은 진행 중인 poll이 끝날 때까지 기다린다 |
| `napi_schedule(napi)` | ISR | `napi_schedule_prep()`(SCHED 비트 test-and-set) + per-CPU poll 목록에 추가 + `NET_RX_SOFTIRQ` 올림 |
| `napi_complete_done(napi, work)` | poll 끝 | GRO flush, SCHED 해제. **false를 돌려주면 IRQ를 켜지 말 것**(busy poll이 가져갔거나 그사이 다시 예약됨) |
| `napi_gro_receive(napi, skb)` | poll | GRO로 같은 흐름의 패킷을 합친 뒤 스택에 올림 |
| `netif_receive_skb(skb)` | poll | GRO 없이 바로 올림 |
| `netif_rx(skb)` | NAPI 없는 드라이버 | per-CPU backlog 큐에 넣고 softirq로 처리 (`netdev_max_backlog` 초과 시 드롭) |
| `napi_build_skb(data, frag_size)` | poll | 이미 있는 버퍼(page) 위에 skb를 씌움 — **복사 없음** |

poll 함수의 계약: (1) 반환값 ≤ budget, (2) budget을 다 썼으면 `napi_complete_done()`을 부르지 말고 budget을 그대로 반환, (3) budget이 0이면(netpoll) RX는 하지 말고 TX 완료만.

### 5.3 `i6_eth` 코드 8 — RX 처리와 완료

```c
	/* 2) RX: at most 'budget' frames */
	while (work < budget) {
		struct i6_rxd *r = &p->rx[p->rx_next & MASK];
		struct page *page, *fresh;
		struct sk_buff *skb;
		u32 len;

		spin_lock_irq(&i6_bus);
		if (r->own) {				/* still owned by HW: empty */
			spin_unlock_irq(&i6_bus);
			break;
		}
		page = r->page;
		len = r->len;
		spin_unlock_irq(&i6_bus);

		fresh = page_pool_dev_alloc_pages(p->pp);
		skb = fresh ? napi_build_skb(page_address(page), PAGE_SIZE) : NULL;
		if (!skb) {				/* recycle: repost old page */
			if (fresh)
				page_pool_put_full_page(p->pp, fresh, true);
			fresh = page;
			p->st.rx_dropped++;
		} else {
			skb_reserve(skb, I6_HEADROOM);
			skb_put(skb, len);
			skb_mark_for_recycle(skb);
			skb->protocol = eth_type_trans(skb, dev);
			p->st.rx_packets++;
			p->st.rx_bytes += len;
			if (stack && p->st.rx_packets == 1)
				dump_stack();
			napi_gro_receive(napi, skb);
		}
		spin_lock_irq(&i6_bus);
		r->page = fresh;			/* refill + give back to HW */
		r->len = 0;
		r->own = 1;
		p->rx_next++;
		spin_unlock_irq(&i6_bus);
		work++;
	}

	p->st.polls++;
	p->st.hist[work ? min(fls(work), 7) : 0]++;
	if (work == budget) {
		p->st.poll_full++;
		return budget;			/* stay in polling mode */
	}
	if (napi_complete_done(napi, work)) {
		spin_lock_irq(&i6_bus);
		p->irq_masked = false;		/* unmask */
		if (i6_pending(p))		/* events that arrived while masked */
			i6_raise_irq(p);
		spin_unlock_irq(&i6_bus);
	}
	return work;
}

```

코드에서 볼 것:

- RX 버퍼는 **page_pool**에서 온 페이지다. 하드웨어가 페이지의 `I6_HEADROOM`(64) 뒤에 프레임을 쓰고, 드라이버는 `napi_build_skb()`로 그 페이지 위에 skb를 씌운 뒤 `skb_reserve`·`skb_put`으로 포인터만 맞춘다. 데이터 복사는 0번이다(예전 드라이버는 `netdev_alloc_skb` + `memcpy` 하는 "copybreak"를 작은 패킷에만 쓴다).
- **새 페이지를 먼저 받고** 성공해야 옛 페이지를 스택에 올린다. 할당 실패 시 패킷을 버리고 옛 페이지를 그대로 다시 깐다 — RX 링에 구멍이 생기지 않게 하는 정석 패턴이다.
- `skb_mark_for_recycle(skb)`: 스택이 이 skb를 해제할 때 페이지를 page_pool로 돌려보낸다. 할당자를 거치지 않으니 빠르고, `page_pool_params.dma_dir`와 `PP_FLAG_DMA_MAP`을 쓰면 **DMA 매핑까지 재사용**한다(H9의 streaming DMA map/unmap 비용이 사라진다).
- `eth_type_trans()`는 이더넷 헤더를 `skb_pull`로 벗기고 `skb->protocol`, `pkt_type`(내 주소/브로드캐스트/멀티캐스트/남의 것)을 정한다.
- 끝에서 `poll_w*` 히스토그램을 올리고, `work < budget`이면 `napi_complete_done()` → **IRQ unmask** → "마스크 동안 도착한 일이 있는지" 다시 확인해서 있으면 IRQ를 다시 띄운다. 이 재확인이 없으면 unmask 직전에 도착한 패킷이 다음 패킷이 올 때까지 링에 갇힌다(진짜 하드웨어는 원인 레지스터가 남아 있어 unmask 순간 IRQ가 다시 뜨므로 대개 자동으로 해결된다).

실제 FEC 드라이버의 poll도 같은 모양이다(6.18 `fec_main.c` 발췌).

```c
static int fec_enet_rx_napi(struct napi_struct *napi, int budget)
{
	struct net_device *ndev = napi->dev;
	struct fec_enet_private *fep = netdev_priv(ndev);
	int done = 0;

	do {
		done += fec_enet_rx(ndev, budget - done);
		fec_enet_tx(ndev, budget);
	} while ((done < budget) && fec_enet_collect_events(fep));

	if (done < budget) {
		napi_complete_done(napi, done);
		writel(FEC_DEFAULT_IMASK, fep->hwp + FEC_IMASK);
	}

	return done;
}
```

`writel(FEC_DEFAULT_IMASK, … FEC_IMASK)`가 우리 `p->irq_masked = false`에 해당한다.

### 5.4 `i6_eth` 코드 9 — open과 stop

```c
/* ---------- open / stop ---------- */
static int i6_open(struct net_device *dev)
{
	struct i6_priv *p = netdev_priv(dev);
	struct page_pool_params pp = {
		.order = 0, .pool_size = ring, .nid = NUMA_NO_NODE,
		.napi = &p->napi, .netdev = dev,
	};
	unsigned int i;

	p->pp = page_pool_create(&pp);
	if (IS_ERR(p->pp))
		return PTR_ERR(p->pp);
	for (i = 0; i < ring; i++) {		/* post empty RX buffers */
		p->rx[i].page = page_pool_dev_alloc_pages(p->pp);
		if (!p->rx[i].page)
			return -ENOMEM;		/* (leak on error: demo code) */
		p->rx[i].own = 1;
	}
	p->tx_head = p->tx_hw = p->tx_clean = p->rx_hw = p->rx_next = 0;
	netdev_reset_queue(dev);
	napi_enable(&p->napi);
	spin_lock_irq(&i6_bus);
	p->up = true;
	p->irq_masked = false;
	spin_unlock_irq(&i6_bus);
	phy_start(p->phy);			/* phylib turns the carrier on */
	netif_start_queue(dev);
	return 0;
}

static int i6_stop(struct net_device *dev)
{
	struct i6_priv *p = netdev_priv(dev);
	unsigned int i;

	spin_lock_irq(&i6_bus);
	p->up = false;
	spin_unlock_irq(&i6_bus);
	netif_tx_disable(dev);
	phy_stop(p->phy);
	hrtimer_cancel(&p->hw);
	napi_disable(&p->napi);
	for (i = p->tx_clean; i != p->tx_head; i++)
		dev_kfree_skb_any(p->tx[i & MASK].skb);
	netdev_reset_queue(dev);
	for (i = 0; i < ring; i++)
		page_pool_put_full_page(p->pp, p->rx[i].page, false);
	page_pool_destroy(p->pp);
	return 0;
}

```

코드에서 볼 것: `ndo_open`의 순서 — page_pool 만들기 → RX 링을 빈 버퍼로 채우기 → 인덱스·BQL 리셋 → `napi_enable` → "IRQ 허용"(`up = true`) → `phy_start` → `netif_start_queue`. `ndo_stop`은 그 역순이며 핵심은 **새 일이 안 들어오게 막은 뒤(`up = false`, `netif_tx_disable`) 엔진과 NAPI를 멈추고(`hrtimer_cancel`, `napi_disable`) 나서 메모리를 푼다**는 것이다. 순서를 바꾸면 해제된 페이지에 하드웨어가 DMA 하는 use-after-free가 된다(진짜 하드웨어에서는 메모리 손상, IOMMU가 있으면 fault).

### 5.5 예제 4 — 두 namespace 사이의 ping과 UDP, RX 콜 스택

무엇을 확인하나: `i6_eth1`을 다른 network namespace로 옮겨 "두 장비"를 만들고, ping과 UDP 2000개가 우리 드라이버의 TX·RX·NAPI를 모두 지나는지. 그리고 RX 콜 스택에서 hardirq → softirq 전환을 본다.

BusyBox에는 `ip netns`가 없지만 `unshare -n`(새 netns에서 프로세스 실행) + `ip link set dev X netns <PID>` + `nsenter -t <PID> -n`(그 netns에서 명령 실행) 조합이 된다.

```sh
cd /i6
insmod i6_eth.ko stack=1
echo "### netns"
unshare -n sleep 600 & NS=$!
sleep 0.3
ip link set i6_eth1 netns $NS
ip addr add 10.6.0.1/24 dev i6_eth0; ip link set i6_eth0 up
nsenter -t $NS -n sh -c 'ip link set lo up; ip addr add 10.6.0.2/24 dev i6_eth1; ip link set i6_eth1 up'
echo "root ns:"; ip -o link | cut -d: -f2 | tr "\n" " "; echo; echo "peer ns:"; nsenter -t $NS -n sh -c "ip -o link | cut -d: -f2 | tr \"\\n\" \" \""; echo
sleep 2
echo "### ping"; ping -c 3 10.6.0.2
echo "### udp"
nsenter -t $NS -n ./i6_udp rx 5000 500 & R=$!
sleep 0.5
./i6_udp tx 10.6.0.2 5000 1000 2000 200
wait $R
echo "### stats eth0"; ./i6_ethtool -S i6_eth0
echo "### stats eth1"; nsenter -t $NS -n ./i6_ethtool -S i6_eth1
echo "### ifconfig"; ifconfig i6_eth0; cat /proc/net/dev | grep -E "Inter|face|i6"
echo "### stacks"; dmesg | grep -E "Call trace|^\[.*\]  [a-z_0-9.]+\+0x" | head -140
kill $NS
```

```text
lab initramfs: 6.18.55-sub up 0.70s
### netns
root ns:
 lo  eth0  i6_eth0 
peer ns:
 lo  i6_eth1 
### ping
PING 10.6.0.2 (10.6.0.2): 56 data bytes
64 bytes from 10.6.0.2: seq=0 ttl=64 time=14.602 ms
64 bytes from 10.6.0.2: seq=1 ttl=64 time=0.852 ms
64 bytes from 10.6.0.2: seq=2 ttl=64 time=1.021 ms

--- 10.6.0.2 ping statistics ---
3 packets transmitted, 3 packets received, 0% packet loss
round-trip min/avg/max = 0.852/5.491/14.602 ms
### udp
tx: sent 2000 err 0 in 2.000 s = 1000 pps (target 1000)
rx: got 2000 last_seq 1999 gaps 0 in 2.514 s
### stats eth0
  irqs: 1979
  polls: 1979
  poll_full_budget: 0
  rx_packets: 4
  rx_bytes: 354
  rx_missed_no_desc: 0
  rx_dropped: 0
  tx_packets: 2004
  tx_bytes: 484354
  tx_queue_stops: 0
  tx_queue_wakes: 0
  tx_timeouts: 0
  hw_ticks: 1976
  poll_w0: 1975
  poll_w1: 4
  poll_w2_3: 0
  poll_w4_7: 0
  poll_w8_15: 0
  poll_w16_31: 0
  poll_w32_63: 0
  poll_w64: 0
### stats eth1
  irqs: 1979
  polls: 1979
  poll_full_budget: 0
  rx_packets: 2004
  rx_bytes: 484354
  rx_missed_no_desc: 0
  rx_dropped: 0
  tx_packets: 4
  tx_bytes: 354
  tx_queue_stops: 0
  tx_queue_wakes: 0
  tx_timeouts: 0
  hw_ticks: 4
  poll_w0: 3
  poll_w1: 1956
  poll_w2_3: 17
  poll_w4_7: 3
  poll_w8_15: 0
  poll_w16_31: 0
  poll_w32_63: 0
  poll_w64: 0
```

출력에서 볼 것:

- 이동 후 root ns에는 `i6_eth0`, 상대 ns에는 `i6_eth1`만 보인다. ping 3/3 성공 — 첫 RTT(약 17 ms)가 큰 것은 ARP와 첫 경로 탐색 때문이다.
- UDP 1000 pps × 2초 = 2000개 모두 도착(`gaps 0`).
- `i6_eth0`(송신 쪽): `irqs 1958`, `polls 1958`, 그중 `poll_w0 1954` — 거의 모든 poll이 **RX 0개, TX 완료만** 처리했다. TX 완료도 IRQ → NAPI로 온다.
- `i6_eth1`(수신 쪽): `rx_packets 2004`(UDP 2000 + ping 3 + ARP 1), `irqs 1957`, `poll_w1 1927`. 1000 pps에서는 거의 **패킷 하나당 IRQ 하나, poll 하나**다. NAPI가 일을 줄여 주는 것은 패킷이 몰릴 때다(§5.6).

나머지 콜 스택 세 개(첫 번째 TX는 §4.6에서 봤다). 각 스택에서 `__neigh_event_send` 아래의 syscall 부분(첫 스택과 같다)은 `...`로 생략했다:

```text
[    3.271890] Call trace:
[    3.271895]  show_stack+0x28/0x7c (C)
[    3.271926]  dump_stack_lvl+0x60/0x80
[    3.271936]  dump_stack+0x18/0x24
[    3.271946]  i6_poll+0x4b0/0x4c0 [i6_eth]
[    3.271962]  __napi_poll+0x38/0x1ec
[    3.271974]  net_rx_action+0x118/0x270
[    3.271982]  handle_softirqs+0xfc/0x240
[    3.271991]  __do_softirq+0x14/0x20
[    3.271999]  ____do_softirq+0x10/0x20
[    3.272010]  call_on_irq_stack+0x30/0x48
[    3.272020]  do_softirq_own_stack+0x1c/0x40
[    3.272028]  do_softirq+0x50/0x70
[    3.272036]  __local_bh_enable_ip+0x8c/0xa0
[    3.272044]  __neigh_event_send+0xc0/0x370
[    3.272052]  neigh_resolve_output+0x110/0x1c0
...
[    3.273791] Call trace:
[    3.273794]  show_stack+0x28/0x7c (C)
[    3.273808]  dump_stack_lvl+0x60/0x80
[    3.273816]  dump_stack+0x18/0x24
[    3.273824]  i6_xmit+0x148/0x340 [i6_eth]
[    3.273835]  dev_hard_start_xmit+0xa0/0x11c
[    3.273846]  sch_direct_xmit+0x8c/0x37c
[    3.273856]  __dev_queue_xmit+0x9f8/0xec4
[    3.273867]  arp_xmit+0x2c/0x70
[    3.273879]  arp_send_dst+0x70/0xb0
[    3.273889]  arp_process+0x824/0x92c
[    3.273899]  arp_rcv+0x118/0x170
[    3.273910]  __netif_receive_skb_list_core+0x1f0/0x220
[    3.273921]  netif_receive_skb_list_internal+0x1a8/0x2c4
[    3.273932]  napi_complete_done+0x7c/0x1b0
[    3.273940]  i6_poll+0x378/0x4c0 [i6_eth]
[    3.273950]  __napi_poll+0x38/0x1ec
[    3.273961]  net_rx_action+0x118/0x270
[    3.273971]  handle_softirqs+0xfc/0x240
[    3.273982]  __do_softirq+0x14/0x20
[    3.273989]  ____do_softirq+0x10/0x20
[    3.273997]  call_on_irq_stack+0x30/0x48
[    3.274008]  do_softirq_own_stack+0x1c/0x40
[    3.274018]  do_softirq+0x50/0x70
[    3.274029]  __local_bh_enable_ip+0x8c/0xa0
...
[    3.274568] Call trace:
[    3.274572]  show_stack+0x28/0x7c (C)
[    3.274583]  dump_stack_lvl+0x60/0x80
[    3.274592]  dump_stack+0x18/0x24
[    3.274601]  i6_poll+0x4b0/0x4c0 [i6_eth]
[    3.274615]  __napi_poll+0x38/0x1ec
[    3.274626]  net_rx_action+0x118/0x270
[    3.274635]  handle_softirqs+0xfc/0x240
[    3.274644]  __do_softirq+0x14/0x20
[    3.274651]  ____do_softirq+0x10/0x20
[    3.274660]  call_on_irq_stack+0x30/0x48
[    3.274669]  do_softirq_own_stack+0x1c/0x40
[    3.274679]  do_softirq+0x50/0x70
[    3.274687]  __local_bh_enable_ip+0x8c/0xa0
[    3.274696]  __neigh_event_send+0xc0/0x370
```

출력에서 볼 것:

- 두 번째 스택(`i6_eth1`의 첫 RX = ARP 요청 수신): `i6_poll` ← `__napi_poll` ← `net_rx_action` ← `handle_softirqs` ← `do_softirq` ← **`__local_bh_enable_ip`** ← `__neigh_event_send` ← … ← `sendto`. RX softirq가 **ping 프로세스의 문맥에서** 돌았다. ping이 ARP를 보내며 BH를 껐다 켜는 순간(`local_bh_enable`), 그사이 hardirq(우리 hrtimer)가 올려 둔 `NET_RX_SOFTIRQ`가 그 자리에서 처리된 것이다. softirq는 "IRQ에서 돌아갈 때" 또는 "BH를 다시 켤 때" 또는 `ksoftirqd`에서 돈다(F5).
- 세 번째 스택: `i6_eth1`이 ARP 응답을 보내는 xmit인데, 그 아래에 `arp_rcv` ← `__netif_receive_skb_list_core` ← `netif_receive_skb_list_internal` ← **`napi_complete_done`** ← `i6_poll`이 있다. GRO가 모아 둔 패킷을 `napi_complete_done()`이 flush 하면서 스택에 올렸고, ARP 계층이 **같은 softirq 안에서** 곧바로 응답을 xmit 했다. RX 처리 중에 TX가 일어나는 것은 흔하다(TCP ACK도 그렇다).
- 네 번째 스택: `i6_eth0`이 ARP 응답을 받는 RX. 역시 같은 ping 프로세스의 BH enable 지점이다. 2 vCPU에서 hrtimer가 같은 CPU에 걸려 있으면 이렇게 한 문맥 안에서 왕복이 끝난다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 520"><defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">RX 경로 — 하드IRQ → softirq → 소켓</text><rect x="30" y="44" width="450" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="255.0" y="71.2" font-size="12" text-anchor="middle">HW: RX 버퍼(page_pool 페이지)에 DMA 쓰기 + own=SW</text><rect x="510" y="44" width="140" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="580.0" y="71.2" font-size="12" text-anchor="middle">하드웨어</text><rect x="30" y="104" width="450" height="46" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="255.0" y="131.2" font-size="12" text-anchor="middle">IRQ → 핸들러: mask + napi_schedule</text><rect x="510" y="104" width="140" height="46" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="580.0" y="131.2" font-size="12" text-anchor="middle">hardirq</text><polyline points="255,90 255,102" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/><rect x="30" y="164" width="450" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="255.0" y="191.2" font-size="12" text-anchor="middle">net_rx_action → __napi_poll → 드라이버 poll</text><rect x="510" y="164" width="140" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="580.0" y="191.2" font-size="12" text-anchor="middle">softirq</text><polyline points="255,150 255,162" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/><rect x="30" y="224" width="450" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="255.0" y="251.2" font-size="12" text-anchor="middle">napi_build_skb + skb_reserve + skb_put</text><rect x="510" y="224" width="140" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="580.0" y="251.2" font-size="12" text-anchor="middle">softirq</text><polyline points="255,210 255,222" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/><rect x="30" y="284" width="450" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="255.0" y="311.2" font-size="12" text-anchor="middle">eth_type_trans → napi_gro_receive</text><rect x="510" y="284" width="140" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="580.0" y="311.2" font-size="12" text-anchor="middle">softirq</text><polyline points="255,270 255,282" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/><rect x="30" y="344" width="450" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="255.0" y="371.2" font-size="12" text-anchor="middle">netif_receive_skb_list → ip_rcv → udp_rcv</text><rect x="510" y="344" width="140" height="46" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="580.0" y="371.2" font-size="12" text-anchor="middle">softirq</text><polyline points="255,330 255,342" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/><rect x="30" y="404" width="450" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="255.0" y="431.2" font-size="12" text-anchor="middle">소켓 수신 큐 → recvfrom() 깨움</text><rect x="510" y="404" width="140" height="46" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="580.0" y="431.2" font-size="12" text-anchor="middle">프로세스</text><polyline points="255,390 255,402" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah6)"/><text x="340.0" y="480.0" font-size="12" text-anchor="middle">버퍼 재공급(refill): poll이 새 페이지를 own=HW로 돌려놓는다</text><text x="340.0" y="504.0" font-size="12" text-anchor="middle">드롭 지점: 링 빈칸 없음 · backlog · 소켓 rcvbuf</text></svg>
```
그림 6 — RX 경로와 실행 문맥. 드라이버 코드의 대부분은 softirq 문맥(NAPI poll)에서 돈다. 잠들 수 없고, 같은 CPU의 다른 softirq와 경쟁하며, 오래 걸리면 `ksoftirqd`로 밀린다.

### 5.6 예제 5 — NAPI를 숫자로 보기: 부하와 coalescing

무엇을 확인하나: 송신 속도와 interrupt coalescing(`rx-usecs`, 우리 엔진 주기)에 따라 **IRQ 수, poll 수, poll당 패킷 수**가 어떻게 바뀌는지. 수신 쪽(`i6_eth1`)의 ethtool 통계를 실험 전후로 찍어 차이를 계산한다.

```sh
cd /i6
insmod i6_eth.ko
unshare -n sleep 600 & NS=$!
sleep 0.3
ip link set i6_eth1 netns $NS
ip addr add 10.6.0.1/24 dev i6_eth0; ip link set i6_eth0 up
nsenter -t $NS -n sh -c 'ip link set lo up; ip addr add 10.6.0.2/24 dev i6_eth1; ip link set i6_eth1 up'
sleep 2; ping -c 1 10.6.0.2 > /dev/null
S() { nsenter -t $NS -n ./i6_ethtool -S i6_eth1 | tr -d ' ' > /tmp/$1; }
run() { # $1=rx-usecs $2=pps $3=count
  nsenter -t $NS -n ./i6_ethtool -C $1 i6_eth1 > /dev/null
  S a; nsenter -t $NS -n ./i6_udp rx 5000 400 > /tmp/rx & R=$!; sleep 0.3
  ./i6_udp tx 10.6.0.2 5000 $2 $3 64 > /tmp/tx; wait $R; S b
  awk -F: -v us=$1 -v pps=$2 'NR==FNR{a[$1]=$2;next}{d[$1]=$2-a[$1]} END{
    printf "usecs=%-3d target=%-6d irqs=%-6d polls=%-6d rx=%-6d pkt/poll=%5.2f irq/pkt=%4.2f full=%d hist=%d/%d/%d/%d/%d/%d/%d/%d\n",
    us,pps,d["irqs"],d["polls"],d["rx_packets"],d["rx_packets"]/(d["polls"]?d["polls"]:1),d["irqs"]/(d["rx_packets"]?d["rx_packets"]:1),d["poll_full_budget"],
    d["poll_w0"],d["poll_w1"],d["poll_w2_3"],d["poll_w4_7"],d["poll_w8_15"],d["poll_w16_31"],d["poll_w32_63"],d["poll_w64"]}' /tmp/a /tmp/b
  cat /tmp/tx /tmp/rx
}
echo "### sweep rate, rx-usecs=20"
for r in 1000 5000 20000 50000 0; do run 20 $r 20000; done
echo "### sweep rx-usecs at flat out"
for u in 1 50 200 1000; do run $u 0 20000; done
echo "### udp snmp"; nsenter -t $NS -n grep Udp: /proc/net/snmp
echo "### interrupts (timer = our HW)"; grep -E "arch_timer|IPI0|IPI1" /proc/interrupts
kill $NS
```

```text
### sweep rate, rx-usecs=20
usecs=20  target=1000   irqs=19588  polls=19588  rx=20001  pkt/poll= 1.02 irq/pkt=0.98 full=0 hist=1/19393/162/23/6/2/1/0
tx: sent 20000 err 0 in 20.000 s = 1000 pps (target 1000)
rx: got 20000 last_seq 19999 gaps 0 in 20.422 s
usecs=20  target=5000   irqs=16988  polls=16988  rx=20000  pkt/poll= 1.18 irq/pkt=0.85 full=0 hist=0/15249/1594/86/39/17/3/0
tx: sent 20000 err 0 in 4.001 s = 4999 pps (target 5000)
rx: got 20000 last_seq 19999 gaps 0 in 4.430 s
usecs=20  target=20000  irqs=12119  polls=12119  rx=20000  pkt/poll= 1.65 irq/pkt=0.61 full=0 hist=0/9411/2147/318/152/67/24/0
tx: sent 20000 err 0 in 3.462 s = 5777 pps (target 20000)
rx: got 19949 last_seq 19999 gaps 7 in 3.881 s
usecs=20  target=50000  irqs=13555  polls=13560  rx=20000  pkt/poll= 1.47 irq/pkt=0.68 full=5 hist=2/10526/2776/142/56/32/21/5
tx: sent 20000 err 0 in 3.599 s = 5557 pps (target 50000)
rx: got 19846 last_seq 19999 gaps 18 in 4.006 s
usecs=20  target=0      irqs=17109  polls=17110  rx=20000  pkt/poll= 1.17 irq/pkt=0.86 full=1 hist=1/15191/1850/48/12/4/3/1
tx: sent 20000 err 0 in 3.250 s = 6154 pps (target 0)
rx: got 19959 last_seq 19999 gaps 4 in 3.660 s
### sweep rx-usecs at flat out
usecs=1   target=0      irqs=12036  polls=12037  rx=20000  pkt/poll= 1.66 irq/pkt=0.60 full=1 hist=1/9845/1646/264/160/85/35/1
tx: sent 20000 err 0 in 3.409 s = 5866 pps (target 0)
rx: got 19889 last_seq 19999 gaps 14 in 3.826 s
usecs=50  target=0      irqs=9524   polls=9528   rx=20000  pkt/poll= 2.10 irq/pkt=0.48 full=4 hist=3/2379/6788/228/96/23/7/4
tx: sent 20000 err 0 in 2.863 s = 6987 pps (target 0)
rx: got 19900 last_seq 19999 gaps 6 in 3.280 s
usecs=200 target=0      irqs=3465   polls=3467   rx=20001  pkt/poll= 5.77 irq/pkt=0.17 full=2 hist=3/155/656/2257/282/87/25/2
tx: sent 20000 err 0 in 2.275 s = 8793 pps (target 0)
rx: got 19778 last_seq 19999 gaps 12 in 2.695 s
usecs=1000 target=0      irqs=874    polls=876    rx=20000  pkt/poll=22.83 irq/pkt=0.04 full=2 hist=2/0/5/8/97/714/48/2
tx: sent 20000 err 0 in 1.374 s = 14558 pps (target 0)
rx: got 19936 last_seq 19999 gaps 6 in 1.803 s
### udp snmp
Udp: InDatagrams NoPorts InErrors OutDatagrams RcvbufErrors SndbufErrors InCsumErrors IgnoredMulti MemErrors
Udp: 179257 0 743 0 743 0 0 0 0
### interrupts (timer = our HW)
 11:      68641      75253 GIC-0  27 Level     arch_timer
IPI0:       295        314       Rescheduling interrupts
IPI1:     48438      44783       Function call interrupts
[   55.266855] reboot: Power down
```

출력에서 볼 것:

- **1000 pps**: `irq/pkt 0.98`, `pkt/poll 1.02`. 패킷이 띄엄띄엄 오면 NAPI는 그냥 "패킷마다 인터럽트"다.
- 송신 속도를 올려도 이 TCG 환경에서는 송신이 약 5.5~6.2k pps에서 포화한다(목표 20000, 50000, 최대 모두 실제로는 6k 근처). 그 구간에서 IRQ/패킷은 0.6~0.86, poll당 1.2~1.7개 — 약간 모이기 시작한다.
- **rx-usecs를 키우면**(엔진이 1, 50, 200, 1000 µs마다 한 번씩 몰아서 전달): IRQ/패킷이 0.60 → 0.48 → 0.17 → **0.04**로, poll당 패킷이 1.66 → 2.10 → 5.77 → **22.83**으로 바뀐다. 1000 µs에서는 poll 876번에 20000개 — 16~31개짜리 poll이 714번.
- 놀라운 부분은 **처리량**이다. rx-usecs 1000에서 송신이 14558 pps까지 올라갔다(1 µs에서 5866 pps). IRQ와 softirq 진입 비용이 줄어서 같은 CPU로 더 많은 패킷을 처리한 것이다 — 인터럽트 mitigation의 존재 이유를 직접 측정했다.
- 반대로 지연은 커진다(§9의 예제 11: rx-usecs 20 → 1000에서 송신 → RX 타임스탬프 평균 319 µs → 1363 µs). **처리량과 지연의 트레이드오프**가 coalescing의 본질이다. LiDAR 수신처럼 처리량이 중요하면 키우고, 제어 루프처럼 지연이 중요하면 줄인다.
- 마지막 줄 `Udp: … RcvbufErrors 743`: 드라이버는 모든 프레임을 올렸는데(`rx=20000`) 수신 앱이 다 못 읽어 **소켓 버퍼에서** 743개가 버려졌다(`gaps`). A9 §5에서 본 그 카운터다. 드롭 위치는 드라이버(rx_missed), 스택(softnet dropped), 소켓(RcvbufErrors) 세 곳을 따로 봐야 한다.
- `arch_timer` 인터럽트가 CPU당 7만 번 안팎인 것은 우리 "하드웨어"(hrtimer)가 많이 돌았기 때문이다. 진짜 NIC이라면 이 자리가 MSI-X 벡터다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 360"><defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="340.0" y="20.0" font-size="14" text-anchor="middle" font-weight="bold">i6_eth 실측: rx-usecs(엔진 주기)를 키우면</text><text x="195.0" y="46.0" font-size="13" text-anchor="middle">IRQ / 패킷</text><line x1="60" y1="300" x2="330" y2="300" stroke="currentColor" stroke-width="1"/><line x1="60" y1="300" x2="60" y2="80" stroke="currentColor" stroke-width="1"/><line x1="56" y1="300.0" x2="60" y2="300.0" stroke="currentColor" stroke-width="1"/><text x="54.0" y="304.0" font-size="11" text-anchor="end">0.00</text><line x1="56" y1="245.0" x2="60" y2="245.0" stroke="currentColor" stroke-width="1"/><text x="54.0" y="249.0" font-size="11" text-anchor="end">0.25</text><line x1="56" y1="190.0" x2="60" y2="190.0" stroke="currentColor" stroke-width="1"/><text x="54.0" y="194.0" font-size="11" text-anchor="end">0.50</text><line x1="56" y1="135.0" x2="60" y2="135.0" stroke="currentColor" stroke-width="1"/><text x="54.0" y="139.0" font-size="11" text-anchor="end">0.75</text><line x1="56" y1="80.0" x2="60" y2="80.0" stroke="currentColor" stroke-width="1"/><text x="54.0" y="84.0" font-size="11" text-anchor="end">1.00</text><rect x="70.8" y="168.0" width="32.4" height="132.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="87.0" y="163.0" font-size="11" text-anchor="middle">0.60</text><text x="87.0" y="316.0" font-size="12" text-anchor="middle">1</text><rect x="124.8" y="110.8" width="32.4" height="189.2" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="141.0" y="105.8" font-size="11" text-anchor="middle">0.86</text><text x="141.0" y="316.0" font-size="12" text-anchor="middle">20</text><rect x="178.8" y="194.4" width="32.4" height="105.6" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="195.0" y="189.4" font-size="11" text-anchor="middle">0.48</text><text x="195.0" y="316.0" font-size="12" text-anchor="middle">50</text><rect x="232.8" y="262.6" width="32.4" height="37.4" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="249.0" y="257.6" font-size="11" text-anchor="middle">0.17</text><text x="249.0" y="316.0" font-size="12" text-anchor="middle">200</text><rect x="286.8" y="291.2" width="32.4" height="8.8" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="303.0" y="286.2" font-size="11" text-anchor="middle">0.04</text><text x="303.0" y="316.0" font-size="12" text-anchor="middle">1000</text><text x="525.0" y="46.0" font-size="13" text-anchor="middle">수신 처리량 (pps)</text><line x1="390" y1="300" x2="660" y2="300" stroke="currentColor" stroke-width="1"/><line x1="390" y1="300" x2="390" y2="80" stroke="currentColor" stroke-width="1"/><line x1="386" y1="300.0" x2="390" y2="300.0" stroke="currentColor" stroke-width="1"/><text x="384.0" y="304.0" font-size="11" text-anchor="end">0.0k</text><line x1="386" y1="245.0" x2="390" y2="245.0" stroke="currentColor" stroke-width="1"/><text x="384.0" y="249.0" font-size="11" text-anchor="end">4.0k</text><line x1="386" y1="190.0" x2="390" y2="190.0" stroke="currentColor" stroke-width="1"/><text x="384.0" y="194.0" font-size="11" text-anchor="end">8.0k</text><line x1="386" y1="135.0" x2="390" y2="135.0" stroke="currentColor" stroke-width="1"/><text x="384.0" y="139.0" font-size="11" text-anchor="end">12.0k</text><line x1="386" y1="80.0" x2="390" y2="80.0" stroke="currentColor" stroke-width="1"/><text x="384.0" y="84.0" font-size="11" text-anchor="end">16.0k</text><rect x="400.8" y="219.3" width="32.4" height="80.7" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/><text x="417.0" y="214.3" font-size="11" text-anchor="middle">5.9k</text><text x="417.0" y="316.0" font-size="12" text-anchor="middle">1</text><rect x="454.8" y="215.4" width="32.4" height="84.6" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/><text x="471.0" y="210.4" font-size="11" text-anchor="middle">6.2k</text><text x="471.0" y="316.0" font-size="12" text-anchor="middle">20</text><rect x="508.8" y="203.9" width="32.4" height="96.1" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/><text x="525.0" y="198.9" font-size="11" text-anchor="middle">7.0k</text><text x="525.0" y="316.0" font-size="12" text-anchor="middle">50</text><rect x="562.8" y="179.1" width="32.4" height="120.9" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/><text x="579.0" y="174.1" font-size="11" text-anchor="middle">8.8k</text><text x="579.0" y="316.0" font-size="12" text-anchor="middle">200</text><rect x="616.8" y="99.8" width="32.4" height="200.2" fill="#3f9a6b" fill-opacity="0.75" stroke="#3f9a6b"/><text x="633.0" y="94.8" font-size="11" text-anchor="middle">14.6k</text><text x="633.0" y="316.0" font-size="12" text-anchor="middle">1000</text><text x="340.0" y="340.0" font-size="12" text-anchor="middle">x축: rx-usecs (µs) · 송신은 최대 속도, 20000개, TCG 에뮬레이션</text></svg>
```
그림 7 — 위 출력의 rx-usecs 스윕을 그린 것. rx-usecs를 키울수록 패킷당 인터럽트가 줄고(왼쪽) 같은 CPU로 처리하는 패킷 수가 늘어난다(오른쪽). 20 µs 막대는 앞의 속도 스윕의 "최대 속도" 행이다. TCG 에뮬레이션 수치라 절대값은 의미가 없고 경향만 본다.

### 5.7 예제 6 — budget을 다 쓰면: weight 8과 softnet_stat

무엇을 확인하나: poll 예산(weight)을 8로 줄이면 "budget을 다 써서 IRQ 없이 재poll" 되는 경우가 생기는지, 그리고 그때 IRQ 수와 poll 수가 갈라지는지.

```sh
cd /i6
insmod i6_eth.ko weight=8
unshare -n sleep 600 & NS=$!
sleep 0.3
ip link set i6_eth1 netns $NS
ip addr add 10.6.0.1/24 dev i6_eth0; ip link set i6_eth0 up
nsenter -t $NS -n sh -c 'ip link set lo up; ip addr add 10.6.0.2/24 dev i6_eth1; ip link set i6_eth1 up'
sleep 2; ping -c 1 10.6.0.2 > /dev/null
nsenter -t $NS -n ./i6_ethtool -C 1000 i6_eth1
nsenter -t $NS -n ./i6_udp rx 5000 400 & R=$!; sleep 0.3
./i6_udp tx 10.6.0.2 5000 0 20000 64; wait $R
nsenter -t $NS -n ./i6_ethtool -S i6_eth1 | grep -E "irqs|polls|poll_full|rx_packets|poll_w"
grep -E "NET_RX|TIMER" /proc/softirqs
kill $NS
```

```text
rx-usecs: 1000  rx-frames: 0  tx-usecs: 0
tx: sent 20000 err 0 in 2.916 s = 6859 pps (target 0)
rx: got 19566 last_seq 19999 gaps 44 in 3.342 s
  irqs: 1390
  polls: 3340
  poll_full_budget: 1950
  rx_packets: 20002
  poll_w0: 222
  poll_w1: 254
  poll_w2_3: 298
  poll_w4_7: 616
  poll_w8_15: 1950
  poll_w16_31: 0
  poll_w32_63: 0
  poll_w64: 0
       TIMER:        144        102
      NET_RX:       1801       1544
     HRTIMER:          0          0
```

출력에서 볼 것: `polls 3340`인데 `irqs 1390`. 차이 1950번은 `poll_full_budget 1950`과 정확히 같다 — budget(8)을 다 써서 `napi_complete_done()`을 부르지 않고 반환했고, 커널이 IRQ 없이 다시 poll 한 횟수다. 히스토그램에서 `poll_w8_15 1950`이 그것이다(8개 = budget). weight 64일 때(예제 5의 1000 µs 행)는 poll 876번, IRQ 874번으로 거의 같았다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 340"><defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="340.0" y="20.0" font-size="14" text-anchor="middle" font-weight="bold">poll 한 번에 처리한 패킷 수 분포 (rx-usecs=1000)</text><line x1="70" y1="270" x2="650" y2="270" stroke="currentColor" stroke-width="1"/><line x1="70" y1="270" x2="70" y2="60" stroke="currentColor" stroke-width="1"/><line x1="66" y1="270.0" x2="70" y2="270.0" stroke="currentColor" stroke-width="1"/><text x="64.0" y="274.0" font-size="11" text-anchor="end">0</text><line x1="66" y1="217.5" x2="70" y2="217.5" stroke="currentColor" stroke-width="1"/><text x="64.0" y="221.5" font-size="11" text-anchor="end">500</text><line x1="66" y1="165.0" x2="70" y2="165.0" stroke="currentColor" stroke-width="1"/><text x="64.0" y="169.0" font-size="11" text-anchor="end">1000</text><line x1="66" y1="112.5" x2="70" y2="112.5" stroke="currentColor" stroke-width="1"/><text x="64.0" y="116.5" font-size="11" text-anchor="end">1500</text><line x1="66" y1="60.0" x2="70" y2="60.0" stroke="currentColor" stroke-width="1"/><text x="64.0" y="64.0" font-size="11" text-anchor="end">2000</text><rect x="78.0" y="269.8" width="28.2" height="0.2" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="92.1" y="265.8" font-size="10" text-anchor="middle">2</text><rect x="106.2" y="246.7" width="28.2" height="23.3" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/><text x="120.4" y="242.7" font-size="10" text-anchor="middle">222</text><text x="106.2" y="286.0" font-size="12" text-anchor="middle">0</text><rect x="150.5" y="270.0" width="28.2" height="0.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="164.6" y="266.0" font-size="10" text-anchor="middle">0</text><rect x="178.8" y="243.3" width="28.2" height="26.7" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/><text x="192.9" y="239.3" font-size="10" text-anchor="middle">254</text><text x="178.8" y="286.0" font-size="12" text-anchor="middle">1</text><rect x="223.0" y="269.5" width="28.2" height="0.5" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="237.1" y="265.5" font-size="10" text-anchor="middle">5</text><rect x="251.2" y="238.7" width="28.2" height="31.3" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/><text x="265.4" y="234.7" font-size="10" text-anchor="middle">298</text><text x="251.2" y="286.0" font-size="12" text-anchor="middle">2-3</text><rect x="295.5" y="269.2" width="28.2" height="0.8" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="309.6" y="265.2" font-size="10" text-anchor="middle">8</text><rect x="323.8" y="205.3" width="28.2" height="64.7" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/><text x="337.9" y="201.3" font-size="10" text-anchor="middle">616</text><text x="323.8" y="286.0" font-size="12" text-anchor="middle">4-7</text><rect x="368.0" y="259.8" width="28.2" height="10.2" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="382.1" y="255.8" font-size="10" text-anchor="middle">97</text><rect x="396.2" y="65.2" width="28.2" height="204.8" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/><text x="410.4" y="61.2" font-size="10" text-anchor="middle">1950</text><text x="396.2" y="286.0" font-size="12" text-anchor="middle">8-15</text><rect x="440.5" y="195.0" width="28.2" height="75.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="454.6" y="191.0" font-size="10" text-anchor="middle">714</text><rect x="468.8" y="270.0" width="28.2" height="0.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/><text x="482.9" y="266.0" font-size="10" text-anchor="middle">0</text><text x="468.8" y="286.0" font-size="12" text-anchor="middle">16-31</text><rect x="513.0" y="265.0" width="28.2" height="5.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="527.1" y="261.0" font-size="10" text-anchor="middle">48</text><rect x="541.2" y="270.0" width="28.2" height="0.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/><text x="555.4" y="266.0" font-size="10" text-anchor="middle">0</text><text x="541.2" y="286.0" font-size="12" text-anchor="middle">32-63</text><rect x="585.5" y="269.8" width="28.2" height="0.2" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="599.6" y="265.8" font-size="10" text-anchor="middle">2</text><rect x="613.8" y="270.0" width="28.2" height="0.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/><text x="627.9" y="266.0" font-size="10" text-anchor="middle">0</text><text x="613.8" y="286.0" font-size="12" text-anchor="middle">64</text><rect x="100" y="40" width="14" height="14" rx="6" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="120.0" y="52.0" font-size="12" text-anchor="start">weight 64: poll 876번, IRQ 874번</text><rect x="100" y="62" width="14" height="14" rx="6" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/><text x="120.0" y="74.0" font-size="12" text-anchor="start">weight 8: poll 3340번, IRQ 1390번</text><text x="360.0" y="310.0" font-size="12" text-anchor="middle">x축: 한 poll이 처리한 RX 패킷 수 구간</text><text x="360.0" y="330.0" font-size="12" text-anchor="middle">weight 8은 예산을 다 써서(8-15 칸 = 정확히 8) IRQ 없이 재poll</text></svg>
```
그림 8 — 같은 부하(rx-usecs=1000, 최대 속도)에서 poll당 처리량 분포. weight 64는 대부분 16~31개를 한 번에, weight 8은 8개에서 잘려 재poll 된다.

softnet_stat과 관련 sysctl(같은 부하, weight 64, `t9_skb.sh`의 뒷부분):

```text
### softnet_stat before/after flood
00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
00000004 00000000 00000001 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000001 00000000 00000000
net.core.netdev_budget = 300
net.core.netdev_budget_usecs = 8000
net.core.dev_weight = 64
net.core.netdev_max_backlog = 1000
net.core.busy_poll = 0
net.core.busy_read = 0
tx: sent 20000 err 0 in 3.769 s = 5307 pps (target 0)
00002752 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
000026d2 00000000 00000001 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000001 00000000 00000000
   14 0         0:00 [ksoftirqd/0]
   22 0         0:00 [ksoftirqd/1]
[    8.516955] reboot: Power down
```

출력에서 볼 것:

- `/proc/net/softnet_stat`은 CPU마다 한 줄이고 6.18 기준 열은 processed, dropped, time_squeeze, (0 다섯 개), (예전 cpu_collision 0), received_rps, flow_limit_count, backlog 길이, **CPU 번호**, input_qlen, process_qlen이다(`net/core/net-procfs.c`). 부하 후 processed가 0x2752 + 0x26d2 = 20004로 두 CPU에 나뉘었다.
- `dropped`(backlog 초과)와 time_squeeze(예산 소진으로 softirq를 끊은 횟수)는 부하 중에 늘지 않았다(CPU1의 1은 부팅 때 이미 있던 값). 실제 장비에서 time_squeeze가 계속 오르면 `netdev_budget`을 올리거나 IRQ를 다른 CPU로 분산하거나 threaded NAPI를 검토한다.
- `netdev_budget = 300`, `netdev_budget_usecs = 8000`, `dev_weight = 64`, `netdev_max_backlog = 1000`(`netif_rx` 경로의 per-CPU 큐 한도), `busy_poll = 0`.

### 5.8 NAPI 주변 기능 — busy poll, threaded NAPI, defer

| 기능 | 켜는 법 | 효과 | 언제 |
|---|---|---|---|
| interrupt coalescing | `ethtool -C eth0 rx-usecs N rx-frames M` | 하드웨어가 IRQ를 모아서 낸다 | 처리량 우선 |
| busy poll | `SO_BUSY_POLL` 소켓 옵션, `net.core.busy_read/busy_poll` | 소켓을 읽는 **앱 스레드가 직접** 드라이버 poll을 돌린다 (IRQ·softirq 대기 없음) | 지연 µs 단위가 중요한 트레이딩·제어 |
| threaded NAPI | `echo 1 > /sys/class/net/eth0/threaded` | poll이 softirq 대신 커널 스레드 `napi/eth0-N`에서 돈다 → 우선순위·CPU 고정 가능 | PREEMPT_RT, 실시간 시스템(N 모듈) |
| defer hard irqs | `napi_defer_hard_irqs`, `gro_flush_timeout` (sysfs) | poll이 비어도 바로 IRQ를 켜지 않고 hrtimer로 몇 번 더 poll | 고부하 서버 |
| RPS / RFS | `/sys/class/net/eth0/queues/rx-0/rps_cpus` | 단일 큐 NIC의 프로토콜 처리를 다른 CPU로 분산 (소프트웨어 RSS) | 큐 하나뿐인 임베디드 NIC |

> 함정 — PREEMPT_RT: RT 커널에서는 softirq가 스레드로 돌고, 네트워크 RX 부하가 높으면 RT 태스크 우선순위 설계에 NAPI 스레드가 끼어든다. 로봇 제어 루프와 LiDAR 수신이 같은 보드에 있으면 threaded NAPI + `chrt`로 우선순위를 명시하는 것이 일반적이다(N 모듈).

---

## 6. 멀티큐, 오프로드, 그리고 진짜 NIC 드라이버

### 6.1 멀티큐와 RSS — 짧게

1 Gbps 이상 NIC은 TX/RX 큐가 여러 개이고 큐마다 링, MSI-X 벡터, NAPI 인스턴스가 따로 있다. 수신 쪽은 하드웨어가 패킷 헤더(IP 주소·포트)의 해시로 큐를 고르는 **RSS(Receive Side Scaling)** 로 흐름을 CPU에 나눈다. 같은 흐름은 같은 큐로 가므로 순서가 지켜진다. 송신 쪽은 스택이 **XPS**(CPU → TX 큐 매핑)나 해시로 큐를 고르고(`skb_get_queue_mapping`), 드라이버는 큐 단위로 stop/wake 한다(`netif_tx_stop_queue(txq)`).

| 개념 | 커널 쪽 | 확인 |
|---|---|---|
| 큐 수 | `alloc_etherdev_mqs()`, `netif_set_real_num_tx/rx_queues()` | `/sys/class/net/X/queues/` |
| 큐별 IRQ | `pci_alloc_irq_vectors(…, PCI_IRQ_MSIX)` + `request_irq` 큐마다 | `/proc/interrupts`의 `eth0-rx-0` 등 |
| RSS 키·표 | `ethtool_ops.get/set_rxfh` | `ethtool -x` |
| 채널 수 | `ethtool_ops.get/set_channels` | `ethtool -l / -L` |

### 6.2 예제 9 — virtio-net vs e1000e vs igb

무엇을 확인하나: 같은 커널에서 세 종류의 "진짜" 드라이버가 링 크기, coalescing, features, 타임스탬프 능력, 큐 수, MSI-X 벡터를 어떻게 다르게 보이는지. QEMU에 PCIe NIC 세 개를 붙였다(`-device e1000e,netdev=n1 -netdev user,id=n1 -device virtio-net-pci,netdev=n2 -netdev user,id=n2 -device igb,netdev=n3 -netdev user,id=n3`). 트래픽은 NIC마다 별도 netns에서 QEMU user 네트워크의 게이트웨이(10.0.2.2)로 ping 3개와 UDP 300개를 보냈다. 하드웨어 타임스탬프 요청은 §9의 `i6_ts hw`로 했다.

기능 비트·링크·타임스탬프를 읽는 도구(`i6_ethfeat`):

```c
/* i6_ethfeat: offload features (-k), link settings (-l), timestamping caps (-T) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <linux/net_tstamp.h>

static int fd; static struct ifreq ifr;
static int eio(void *cmd) { ifr.ifr_data = cmd; return ioctl(fd, SIOCETHTOOL, &ifr); }
#define DIE(m) do { perror(m); exit(1); } while (0)

int main(int argc, char **argv)
{
	if (argc != 3) { fprintf(stderr, "usage: %s -k|-l|-T dev\n", argv[0]); return 1; }
	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) DIE("socket");
	strncpy(ifr.ifr_name, argv[2], IFNAMSIZ - 1);
	if (!strcmp(argv[1], "-k")) {		/* names come from ETH_SS_FEATURES */
		struct { struct ethtool_sset_info h; __u32 n; } si = { 0 };
		si.h.cmd = ETHTOOL_GSSET_INFO; si.h.sset_mask = 1ULL << ETH_SS_FEATURES;
		if (eio(&si) < 0) DIE("GSSET_INFO");
		unsigned n = si.n, w = (n + 31) / 32;
		struct ethtool_gstrings *s = calloc(1, sizeof(*s) + n * ETH_GSTRING_LEN);
		struct ethtool_gfeatures *f = calloc(1, sizeof(*f) + w * sizeof(f->features[0]));
		if (!s || !f) DIE("calloc");
		s->cmd = ETHTOOL_GSTRINGS; s->string_set = ETH_SS_FEATURES; s->len = n;
		f->cmd = ETHTOOL_GFEATURES; f->size = w;
		if (eio(s) < 0 || eio(f) < 0) DIE("GFEATURES");
		printf("active:");
		for (unsigned i = 0; i < n; i++)
			if (f->features[i / 32].active & (1U << (i % 32)))
				printf(" %s", (char *)s->data + i * ETH_GSTRING_LEN);
		printf("\n");
	} else if (!strcmp(argv[1], "-l")) {	/* 2-step handshake for mask size */
		struct { struct ethtool_link_settings s; __u32 m[3 * 127]; } ls = { 0 };
		ls.s.cmd = ETHTOOL_GLINKSETTINGS;
		if (eio(&ls) < 0) DIE("GLINKSETTINGS");
		ls.s.link_mode_masks_nwords = -ls.s.link_mode_masks_nwords;
		if (eio(&ls) < 0) DIE("GLINKSETTINGS");
		printf("speed %u duplex %s autoneg %s port %u phyad %u\n", ls.s.speed,
		       ls.s.duplex == DUPLEX_FULL ? "full" : ls.s.duplex == DUPLEX_HALF ? "half" : "unknown",
		       ls.s.autoneg ? "on" : "off", ls.s.port, ls.s.phy_address);
	} else {
		struct ethtool_ts_info t = { .cmd = ETHTOOL_GET_TS_INFO };
		if (eio(&t) < 0) DIE("GET_TS_INFO");
		printf("so_timestamping 0x%x (hw-tx %d hw-rx %d sw-tx %d) phc %d tx_types 0x%x rx_filters 0x%x\n",
		       t.so_timestamping, !!(t.so_timestamping & SOF_TIMESTAMPING_TX_HARDWARE),
		       !!(t.so_timestamping & SOF_TIMESTAMPING_RX_HARDWARE),
		       !!(t.so_timestamping & SOF_TIMESTAMPING_TX_SOFTWARE),
		       t.phc_index, t.tx_types, t.rx_filters);
	}
	return 0;
}
```

게스트 스크립트 `t6_nics.sh`:

```sh
cd /i6
echo "### pci"; lspci -k 2>/dev/null | grep -v "^$" | head -20
for i in eth0 eth1 eth2 eth3; do [ -e /sys/class/net/$i ] || continue
  echo "===== $i ($(basename $(readlink /sys/class/net/$i/device/driver)))"
  ./i6_ethtool -i $i | grep -E "driver|bus"; ./i6_ethtool -g $i; ./i6_ethtool -c $i 2>&1
  ./i6_ethfeat -k $i; ./i6_ethfeat -T $i
  echo "queues: $(ls /sys/class/net/$i/queues | tr '\n' ' ')"
done
echo "### MSI-X vectors"; grep -E "eth|virtio|ens|enp" /proc/interrupts
echo "### traffic: 300 UDP + ping per NIC, each in its own netns"
for i in eth0 eth1 eth2 eth3; do [ -e /sys/class/net/$i ] || continue
  unshare -n sleep 100 & P=$!; sleep 0.2; ip link set $i netns $P
  nsenter -t $P -n sh -c "ip addr add 10.0.2.15/24 dev $i; ip link set $i up; sleep 3; ping -c 3 -W 2 10.0.2.2 | tail -2; /i6/i6_udp tx 10.0.2.2 9 1000 300 200; grep $i: /proc/net/dev; /i6/i6_ts hw $i; grep -E \"$i|virtio\" /proc/interrupts"
  kill $P; sleep 0.3
done
echo "### MSI-X vectors after"; grep -E "eth|virtio" /proc/interrupts
```

```text
### pci
00:01.0 Class 0200: 8086:10d3 e1000e
00:00.0 Class 0600: 1b36:0008
00:03.0 Class 0200: 8086:10c9 igb
00:02.0 Class 0200: 1af4:1000 virtio-pci
===== eth0 (virtio_net)
driver: virtio_net
bus-info: 0000:00:02.0
rx 256/256  tx 256/256 (current/max)
rx-usecs: 0  rx-frames: 1  tx-usecs: 0
active: highdma rx-vlan-filter rx-gro rx-checksum
so_timestamping 0x1a (hw-tx 0 hw-rx 0 sw-tx 1) phc -1 tx_types 0x0 rx_filters 0x0
queues: rx-0 tx-0 
===== eth1 (e1000e)
driver: e1000e
bus-info: 0000:00:01.0
rx 256/4096  tx 256/4096 (current/max)
rx-usecs: 3  rx-frames: 0  tx-usecs: 0
active: tx-scatter-gather tx-checksum-ip-generic highdma tx-vlan-hw-insert rx-vlan-hw-parse rx-vlan-filter tx-generic-segmentation rx-gro tx-tcp-segmentation tx-tcp6-segmentation rx-hashing rx-checksum
so_timestamping 0x5f (hw-tx 1 hw-rx 1 sw-tx 1) phc 0 tx_types 0x3 rx_filters 0x7db3
queues: rx-0 tx-0 
===== eth2 (igb)
driver: igb
bus-info: 0000:00:03.0
rx 256/4096  tx 256/4096 (current/max)
rx-usecs: 3  rx-frames: 0  tx-usecs: 3
active: tx-scatter-gather tx-checksum-ip-generic highdma tx-vlan-hw-insert rx-vlan-hw-parse rx-vlan-filter tx-generic-segmentation rx-gro tx-tcp-segmentation tx-tcp6-segmentation tx-gre-segmentation tx-gre-csum-segmentation tx-ipxip4-segmentation tx-ipxip6-segmentation tx-udp_tnl-segmentation tx-udp_tnl-csum-segmentation tx-gso-partial tx-udp-segmentation tx-checksum-sctp rx-hashing rx-checksum
so_timestamping 0x5f (hw-tx 1 hw-rx 1 sw-tx 1) phc 1 tx_types 0x3 rx_filters 0x1031
queues: rx-0 rx-1 rx-2 rx-3 rx-4 rx-5 rx-6 rx-7 tx-0 tx-1 tx-2 tx-3 tx-4 tx-5 tx-6 tx-7 
### MSI-X vectors
 16:          0          0 GICv2m-PCI-MSIX-0000:00:02.0   0 Edge      virtio0-config
 17:          0          0 GICv2m-PCI-MSIX-0000:00:02.0   1 Edge      virtio0-input.0
 18:          0          0 GICv2m-PCI-MSIX-0000:00:02.0   2 Edge      virtio0-output.0
### traffic: 300 UDP + ping per NIC, each in its own netns
3 packets transmitted, 3 packets received, 0% packet loss
round-trip min/avg/max = 0.925/5.034/12.743 ms
tx: sent 300 err 0 in 0.300 s = 1001 pps (target 1000)
  eth0:   81358     304    0    0    0     0          0         0    72936     304    0    0    0     0       0          0
SIOCSHWTSTAMP: Operation not supported
 16:          0          0 GICv2m-PCI-MSIX-0000:00:02.0   0 Edge      virtio0-config
 17:        291          0 GICv2m-PCI-MSIX-0000:00:02.0   1 Edge      virtio0-input.0
 18:        239          0 GICv2m-PCI-MSIX-0000:00:02.0   2 Edge      virtio0-output.0
3 packets transmitted, 3 packets received, 0% packet loss
round-trip min/avg/max = 0.772/1.714/3.394 ms
tx: sent 300 err 0 in 0.299 s = 1002 pps (target 1000)
  eth1:   82574     304    0    0    0     0          0         0    74152     304    0    0    0     0       0          0
eth1: hw timestamping on, rx_filter granted=1
 16:          0          0 GICv2m-PCI-MSIX-0000:00:02.0   0 Edge      virtio0-config
 17:        291          0 GICv2m-PCI-MSIX-0000:00:02.0   1 Edge      virtio0-input.0
 18:        239          0 GICv2m-PCI-MSIX-0000:00:02.0   2 Edge      virtio0-output.0
 20:        614          0 GICv2m-PCI-MSIX-0000:00:01.0   0 Edge      eth1-rx-0
 21:        607          0 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      eth1-tx-0
 22:          4          0 GICv2m-PCI-MSIX-0000:00:01.0   2 Edge      eth1
3 packets transmitted, 3 packets received, 0% packet loss
round-trip min/avg/max = 1.001/4.781/8.698 ms
tx: sent 300 err 0 in 0.300 s = 998 pps (target 1000)
  eth2:   81358     304    0    0    0     0          0         0    72936     304    0    0    0     0       0          0
SIOCSHWTSTAMP: Numerical result out of range
 16:          0          0 GICv2m-PCI-MSIX-0000:00:02.0   0 Edge      virtio0-config
 17:        291          0 GICv2m-PCI-MSIX-0000:00:02.0   1 Edge      virtio0-input.0
 18:        239          0 GICv2m-PCI-MSIX-0000:00:02.0   2 Edge      virtio0-output.0
 24:          1          0 GICv2m-PCI-MSIX-0000:00:03.0   0 Edge      eth2
 25:         39          0 GICv2m-PCI-MSIX-0000:00:03.0   1 Edge      eth2-rx-0
 26:        377          0 GICv2m-PCI-MSIX-0000:00:03.0   2 Edge      eth2-rx-1
 27:          0          0 GICv2m-PCI-MSIX-0000:00:03.0   3 Edge      eth2-tx-0
 28:        380          0 GICv2m-PCI-MSIX-0000:00:03.0   4 Edge      eth2-tx-1
```

출력에서 볼 것:

- **링 크기**: virtio-net은 256/256(virtqueue 크기가 장치에서 고정), e1000e·igb는 현재 256, 최대 4096 — `ethtool -G`로 키울 수 있다. LiDAR burst 손실이 rx_missed로 보이면 **첫 번째 처방이 RX 링 키우기**다.
- **coalescing**: e1000e·igb는 `rx-usecs 3`. Intel 드라이버에서 작은 값(e1000e 0~4, igb 0~3)은 µs가 아니라 모드 번호이고 3은 동적(adaptive) 인터럽트 조절 모드다(드라이버의 `InterruptThrottleRate`/`rx_itr_setting` 처리). virtio-net은 `rx-frames 1`(패킷마다 알림)이다.
- **features**: e1000e·igb는 scatter-gather, 체크섬 오프로드, TSO, VLAN 태그 삽입/제거, `rx-hashing`(RSS 해시를 skb에 실어 줌)을 켠다. igb는 터널 GSO·SCTP 체크섬까지 있다. 이 virtio-net은 체크섬·TSO를 협상하지 않아 `rx-checksum`, `rx-gro` 정도뿐이다(QEMU user 네트워크 백엔드의 협상 결과).
- **타임스탬프**: e1000e(PHC 0)·igb(PHC 1)는 `so_timestamping 0x5f`로 하드웨어 TX/RX 타임스탬프와 **PTP 하드웨어 클록**(`/dev/ptp0`, `/dev/ptp1`)이 있다. virtio-net은 소프트웨어만. `SIOCSHWTSTAMP`(모든 패킷 RX 타임스탬프 요청)는 e1000e는 허락(`rx_filter granted=1` = `HWTSTAMP_FILTER_ALL`), igb는 `ERANGE`(이 82576 모델이 지원하는 필터 목록 `rx_filters 0x1031`에 ALL이 없다), virtio-net은 `EOPNOTSUPP`.
- **큐와 벡터**: virtio-net은 `virtio0-input.0`/`output.0` + config 벡터(장치 probe 때 할당되어 open 전에도 보인다). e1000e는 `eth1-rx-0`, `eth1-tx-0`, `eth1`(링크 등 기타) 세 벡터 — open 할 때 IRQ를 요청한다. igb는 sysfs 큐가 8쌍인데 사용 중인 MSI-X 벡터는 `rx-0/1`, `tx-0/1` 4개와 기타 1개였다(2 vCPU라 RSS 큐를 2개만 쓰는 것으로 보인다 — 소스로 확인하지 않은 추정). UDP가 `rx-1`/`tx-1`로만 갔다: **한 흐름은 한 큐**.
- 맨 처음 `/proc/interrupts`에는 virtio 벡터만 있다. e1000e·igb는 장치가 down이라 IRQ가 아직 요청되지 않았다. 마지막 "after" 목록에서 e1000e가 사라진 것도 같은 이유다(netns를 지우면 장치가 root ns로 돌아오며 닫힌다). igb 줄이 남은 것은 netns 정리가 비동기(`cleanup_net` 워크큐)라 아직 안 끝났을 때 찍혔기 때문으로 보인다.
- RX 304 = ping 응답 3 + ARP 1 + 300. QEMU user 네트워크(slirp)가 닫힌 UDP 포트 9에 대해 ICMP port unreachable을 돌려준 것으로 보인다(확인하지 않은 추정 — 패킷을 캡처하지 않았다).

### 6.3 오프로드와 features 플래그

| 오프로드 | feature 이름 (ethtool -k) | `NETIF_F_` | 드라이버가 할 일 |
|---|---|---|---|
| TX 체크섬 | tx-checksum-ip-generic | `HW_CSUM` | `skb->ip_summed == CHECKSUM_PARTIAL`이면 `csum_start`/`csum_offset`을 디스크립터에 써서 하드웨어가 계산하게 |
| RX 체크섬 | rx-checksum | `RXCSUM` | 하드웨어가 검증했으면 `CHECKSUM_UNNECESSARY`, 합을 주면 `CHECKSUM_COMPLETE` + `skb->csum` |
| scatter-gather | tx-scatter-gather | `SG` | frags를 디스크립터 여러 개로 (없으면 스택이 선형화) |
| TSO | tx-tcp-segmentation | `TSO` | 64 KB TCP skb를 하드웨어가 MSS 단위로 자름 (`skb_shinfo->gso_size`) |
| GSO | tx-generic-segmentation | `GSO` | 하드웨어 대신 **드라이버 직전에** 소프트웨어로 자름 (SG 필요) |
| GRO | rx-gro | `GRO` | NAPI가 같은 흐름 패킷을 합침 (`napi_gro_receive`만 쓰면 됨) |
| VLAN | rx-vlan-hw-parse / tx-vlan-hw-insert | `HW_VLAN_CTAG_RX/TX` | 태그를 하드웨어가 떼고/붙이고 `__vlan_hwaccel_put_tag()` |
| RSS 해시 | rx-hashing | `RXHASH` | `skb_set_hash()`로 하드웨어 해시 전달 |

드라이버는 probe에서 `ndev->hw_features`(바꿀 수 있는 것)와 `ndev->features`(지금 켤 것)를 설정한다. `ndo_fix_features`/`ndo_set_features`로 조합 제약을 처리한다. 우리 `i6_eth`가 GSO를 잃은 이유(§1.8)는 커널의 이 규칙이다.

```c
	if ((features & NETIF_F_GSO) && !(features & NETIF_F_SG)) {
		netdev_dbg(dev, "Dropping NETIF_F_GSO since no SG feature.\n");
		features &= ~NETIF_F_GSO;
	}
```

(6.18 `net/core/dev.c`의 `netdev_fix_features()` 발췌)

> 함정 — 체크섬 오프로드와 tcpdump: 송신 쪽에서 tcpdump로 보면 TCP/UDP 체크섬이 "incorrect"로 나온다. 체크섬이 아직 계산되지 않은 `CHECKSUM_PARTIAL` 상태의 skb를 캡처했기 때문이다(하드웨어가 나중에 채운다). 버그가 아니다.

### 6.4 XDP와 AF_XDP — 한 단락

**XDP**(eXpress Data Path)는 드라이버의 RX poll 안, **skb를 만들기 전에** eBPF 프로그램을 실행해 `XDP_DROP`/`XDP_PASS`/`XDP_TX`/`XDP_REDIRECT`를 결정한다. 드라이버는 `ndo_bpf`로 프로그램을 받고, RX 버퍼를 page_pool의 페이지로 두고(XDP는 헤드룸 `XDP_PACKET_HEADROOM` 256바이트를 요구), poll에서 `bpf_prog_run_xdp()`를 부른다. **AF_XDP**는 `XDP_REDIRECT`로 패킷을 유저 공간이 공유하는 UMEM 링에 직접 넣는 소켓이다 — DPDK처럼 커널 스택을 우회하지만 드라이버는 커널에 그대로 남는다. 로보틱스에서는 고속 카메라·LiDAR 수신을 zero-copy로 받는 용도로 쓰인다. `i6_eth`는 XDP를 구현하지 않았다(실행하지 않음). 이 커널의 netdevsim에는 `bpf_xdpdrv_accept` 같은 XDP 테스트 손잡이가 있다(예제 10 debugfs 목록).

---

## 7. PHY, MDIO, phylib, phylink

### 7.1 직관 — MAC과 PHY는 다른 칩이다

이더넷 포트는 둘로 나뉜다. **MAC**(SoC 안)은 프레임을 만들고 DMA 하는 디지털 블록이고, **PHY**(보드 위 별도 칩, 예: Realtek RTL8211F, Microchip KSZ9031, 차량용 NXP TJA1100)는 그 비트를 선로의 아날로그 신호로 바꾸고 상대와 속도·duplex를 협상한다. 둘 사이에는 두 가지 길이 있다.

- **데이터 길**: MII(100M), RMII, **RGMII**(1G, 4비트 DDR), SGMII(직렬) 등. DT의 `phy-mode`가 이것이다.
- **관리 길**: **MDIO**(데이터) + **MDC**(클록) 2선 버스. I2C처럼 주소(0~31)로 PHY를 골라 16비트 레지스터를 읽고 쓴다. Clause 22(표준 레지스터 0~31: BMCR, BMSR, PHYID…)와 Clause 45(10G 이상, MMD 장치별 레지스터)가 있다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 470"><defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">MAC · PHY · MDIO — 데이터 길과 관리 길이 다르다</text><rect x="20" y="40" width="300" height="230" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0" stroke-dasharray="5,4"/><text x="170.0" y="60.0" font-size="13" text-anchor="middle">SoC (예: i.MX8M Plus)</text><rect x="40" y="76" width="260" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170.0" y="100.2" font-size="12" text-anchor="middle">MAC + DMA</text><text x="170.0" y="116.2" font-size="12" text-anchor="middle">(FEC / EQOS 드라이버)</text><rect x="40" y="150" width="260" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="170.0" y="176.2" font-size="12" text-anchor="middle">MDIO 컨트롤러 (mii_bus)</text><rect x="40" y="210" width="260" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="170.0" y="236.2" font-size="12" text-anchor="middle">phylink (MAC↔PHY 협상 관리)</text><rect x="400" y="76" width="260" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="530.0" y="100.2" font-size="12" text-anchor="middle">PHY 칩 (addr 1)</text><text x="530.0" y="116.2" font-size="12" text-anchor="middle">예: RTL8211 / TJA1100</text><rect x="400" y="168" width="260" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="530.0" y="194.2" font-size="12" text-anchor="middle">자기 소자 / 커넥터</text><rect x="400" y="232" width="260" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="530.0" y="258.2" font-size="12" text-anchor="middle">케이블 → 상대 PHY</text><polyline points="300,96 398,96" fill="none" stroke="#3f9a6b" stroke-width="2" marker-end="url(#ah7)"/><polyline points="398,112 300,112" fill="none" stroke="#3f9a6b" stroke-width="2" marker-end="url(#ah7)"/><text x="349.0" y="88.0" font-size="12" text-anchor="middle">RGMII</text><polyline points="300,172 398,120" fill="none" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="5,4" marker-end="url(#ah7)"/><text x="352.0" y="160.0" font-size="12" text-anchor="middle">MDC/MDIO</text><polyline points="530,132 530,166" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah7)"/><polyline points="530,212 530,230" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah7)"/><text x="340.0" y="300.0" font-size="13" text-anchor="middle" font-weight="bold">Linux 쪽 객체</text><rect x="20" y="316" width="200" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="120.0" y="340.2" font-size="12" text-anchor="middle">net_device</text><text x="120.0" y="356.2" font-size="12" text-anchor="middle">(MAC 드라이버)</text><rect x="240" y="316" width="200" height="56" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="340.0" y="340.2" font-size="12" text-anchor="middle">phylink / phylib</text><text x="340.0" y="356.2" font-size="12" text-anchor="middle">phy_device · 상태 머신</text><rect x="460" y="316" width="200" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="560.0" y="340.2" font-size="12" text-anchor="middle">PHY 드라이버</text><text x="560.0" y="356.2" font-size="12" text-anchor="middle">(Generic PHY / 벤더)</text><polyline points="220,344 238,344" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah7)"/><polyline points="440,344 458,344" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah7)"/><text x="340.0" y="400.0" font-size="12" text-anchor="middle">DT: phy-handle = &lt;&amp;ethphy1&gt;; phy-mode = "rgmii-id";</text><text x="340.0" y="424.0" font-size="12" text-anchor="middle">PHY 없음(스위치 직결): fixed-link { speed = &lt;1000&gt;; full-duplex; };</text><text x="340.0" y="448.0" font-size="12" text-anchor="middle">링크 변화 → adjust_link / mac_link_up → netif_carrier_on</text></svg>
```
그림 9 — MAC·PHY·MDIO. 데이터는 RGMII로, 설정과 링크 상태는 MDIO로 오간다. Linux에서는 MAC 드라이버(`net_device`) — phylink/phylib(`phy_device`) — PHY 드라이버로 나뉜다.

### 7.2 Linux 쪽 구조

| 객체 | 역할 |
|---|---|
| `struct mii_bus` | MDIO 버스 컨트롤러. MAC 드라이버가 `.read/.write`를 채워 `of_mdiobus_register()`로 등록 (FEC는 MAC 안에 MDIO 컨트롤러가 있다) |
| `struct phy_device` | 버스의 주소 하나에 있는 PHY. `/sys/bus/mdio_bus/devices/<bus>:<addr>` |
| `struct phy_driver` | PHY 칩별 드라이버. PHY ID(레지스터 2·3)로 매칭. 없으면 `Generic PHY`(C22 표준 레지스터만) |
| phylib | PHY 상태 머신(`PHY_DOWN` → `PHY_READY` → `PHY_UP` → `PHY_RUNNING` / `PHY_NOLINK`, `PHY_HALTED`, `PHY_ERROR`, `PHY_CABLETEST`), 1초 폴링 또는 PHY 인터럽트로 링크 감시, 링크가 바뀌면 `netif_carrier_on/off` + MAC의 `adjust_link` 콜백 |
| phylink | phylib 위의 층. SFP 모듈, PCS(SGMII/1000BASE-X), `fixed-link`, in-band 협상을 일관되게 다룬다. MAC 드라이버는 `phylink_mac_ops`(`mac_config`, `mac_link_up`, `mac_link_down`)를 채운다. **새 MAC 드라이버는 phylink를 쓰는 것이 권장**된다(stmmac — i.MX8M Plus EQOS, Tegra MGBE — 와 TI AM62의 am65-cpsw가 phylink를 쓴다) |
| fixed-link | PHY 없이 고정 속도로 연결(이더넷 스위치 칩에 MAC끼리 직결). 커널이 가짜 PHY를 만든다 |

MAC 드라이버가 하는 일은 (1) DT에서 `phy-handle`, `phy-mode`를 읽어 PHY에 연결(`of_phy_connect()` 또는 `phylink_of_phy_connect()`), (2) `ndo_open`에서 `phy_start()`/`phylink_start()`, (3) 링크 콜백에서 협상된 속도·duplex·pause를 MAC 레지스터에 쓰기(예: RGMII 클록 125/25/2.5 MHz 전환)다. `i6_eth`는 (1)을 `fixed_phy_register()` + `phy_connect_direct()`로 했다(§1.5).

### 7.3 `i6_eth` 코드 10 — ethtool_ops 표와 adjust_link

```c
static const struct ethtool_ops i6_ethtool_ops = {
	.supported_coalesce_params = ETHTOOL_COALESCE_RX_USECS,
	.get_drvinfo		= i6_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_link_ksettings	= phy_ethtool_get_link_ksettings,
	.get_ringparam		= i6_get_ringparam,
	.get_sset_count		= i6_get_sset_count,
	.get_strings		= i6_get_strings,
	.get_ethtool_stats	= i6_get_ethtool_stats,
	.get_coalesce		= i6_get_coalesce,
	.set_coalesce		= i6_set_coalesce,
	.get_ts_info		= ethtool_op_get_ts_info,
};

/* ---------- PHY: a fixed-link PHY on the fixed MDIO bus ---------- */
static void i6_adjust_link(struct net_device *dev)
{
	phy_print_status(dev->phydev);		/* MAC would set speed/duplex regs */
}

```

`.get_link_ksettings = phy_ethtool_get_link_ksettings`: 속도·duplex·autoneg는 MAC이 아니라 **PHY가 안다**. 그래서 phylib 헬퍼에 그대로 넘긴다. 예제 2의 `speed 1000 duplex full autoneg on port 2 phyad 0`(port 2 = `PORT_MII`)이 이 경로로 나왔다.

### 7.4 Device Tree — 실제 보드 예

i.MX8M Plus EVK의 FEC(6.18 `arch/arm64/boot/dts/freescale/imx8mp-evk.dts`):

```dts
&fec {
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_fec>;
	phy-mode = "rgmii-id";
	phy-handle = <&ethphy1>;
	fsl,magic-packet;
	status = "okay";

	mdio {
		#address-cells = <1>;
		#size-cells = <0>;

		ethphy1: ethernet-phy@1 {
			compatible = "ethernet-phy-ieee802.3-c22";
			reg = <1>;
			eee-broken-1000t;
			reset-gpios = <&gpio4 2 GPIO_ACTIVE_LOW>;
			reset-assert-us = <10000>;
			reset-deassert-us = <80000>;
			realtek,clkout-disable;
		};
	};
};
```

PHY 없이 스위치에 직결한 EQOS(같은 버전 `imx8mp-skov-reva.dtsi`):

```dts
&eqos {
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_eqos>;
	phy-mode = "rgmii-rxid";
	status = "okay";

	fixed-link {
		speed = <1000>;
		full-duplex;
	};
};
```

| 속성 | 뜻 | bring-up에서 자주 틀리는 점 |
|---|---|---|
| `phy-mode = "rgmii-id"` | RGMII, TX·RX 클록 지연(약 2 ns)을 **PHY가** 넣음 | `rgmii`/`-id`/`-rxid`/`-txid` 선택이 PCB 배선·MAC 지연과 맞아야 한다. 틀리면 링크는 Up인데 패킷이 CRC 에러로 전부 버려진다 |
| `phy-handle` | MDIO 버스 위 PHY 노드 | 주소 `reg`가 PHY 스트랩 핀 설정과 달라 "PHY not found" |
| `reset-gpios` + `reset-*-us` | PHY 리셋 핀과 타이밍 | 데이터시트보다 짧으면 PHY ID가 `0xffffffff`로 읽힌다 |
| `mdio { }` | MAC 안의 MDIO 컨트롤러에 PHY 나열 | MDIO가 다른 MAC 쪽에 있는 보드(두 MAC이 MDIO 하나 공유)도 있다 |
| `fixed-link` | PHY 없음, 고정 속도 | 상대 스위치 포트 설정(속도, RGMII 지연)과 맞출 것 |

> 함정 — "링크는 올라오는데 핑이 안 된다": 대부분 RGMII 지연 설정이다. `ethtool -S`의 `rx_crc_errors`나 MAC의 alignment 에러 카운터가 오르는지 본다. 지연을 MAC과 PHY가 **둘 다** 넣거나 **둘 다** 안 넣으면 이렇게 된다. Don이 RF/PCIe 버스에서 보던 setup/hold 문제의 이더넷 판이다.

---

## 8. ethtool — 드라이버의 진단 창구

### 8.1 두 가지 통로

`ethtool` 명령은 예전에는 `ioctl(SIOCETHTOOL)` 하나로 모든 것을 했고, 5.x부터는 **ethtool netlink**(`ETHTOOL_NETLINK=y`, genetlink 패밀리 `ethtool`)를 우선 쓴다. 드라이버 입장에서는 둘 다 같은 `struct ethtool_ops` 콜백으로 들어온다. BusyBox에는 ethtool이 없으므로 이 노트는 ioctl 통로로 직접 불렀다.

| ethtool 명령 | ioctl 상수 | `ethtool_ops` 콜백 |
|---|---|---|
| `ethtool -i` | `ETHTOOL_GDRVINFO` | `get_drvinfo` (+ `get_sset_count`로 n_stats) |
| `ethtool eth0` (링크) | `ETHTOOL_GLINKSETTINGS` | `get_link_ksettings` |
| `ethtool -g / -G` | `ETHTOOL_GRINGPARAM / SRINGPARAM` | `get/set_ringparam` |
| `ethtool -c / -C` | `ETHTOOL_GCOALESCE / SCOALESCE` | `get/set_coalesce` + `supported_coalesce_params` |
| `ethtool -S` | `ETHTOOL_GSTRINGS` + `ETHTOOL_GSTATS` | `get_strings`, `get_sset_count`, `get_ethtool_stats` |
| `ethtool -k / -K` | `ETHTOOL_GFEATURES / SFEATURES` | 코어가 `dev->features`로 처리 (`ndo_set_features`) |
| `ethtool -T` | `ETHTOOL_GET_TS_INFO` | `get_ts_info` |
| `ethtool -l / -L` | `ETHTOOL_GCHANNELS` | `get/set_channels` |
| `ethtool -d`, `-e` | `GREGS`, `GEEPROM` | `get_regs`, `get_eeprom` |

### 8.2 `i6_eth` 코드 11 — ethtool 콜백

```c
/* ---------- ethtool ---------- */
static void i6_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *i)
{
	strscpy(i->driver, "i6_eth", sizeof(i->driver));
	strscpy(i->version, "0.1", sizeof(i->version));
	strscpy(i->bus_info, dev->dev_id ? "cable:end-b" : "cable:end-a", sizeof(i->bus_info));
}

static void i6_get_ringparam(struct net_device *dev, struct ethtool_ringparam *r,
			     struct kernel_ethtool_ringparam *kr, struct netlink_ext_ack *e)
{
	r->rx_max_pending = r->tx_max_pending = I6_RING_MAX;
	r->rx_pending = r->tx_pending = ring;
}

static int i6_get_sset_count(struct net_device *dev, int sset)
{
	return sset == ETH_SS_STATS ? I6_NSTATS : -EOPNOTSUPP;
}

static void i6_get_strings(struct net_device *dev, u32 sset, u8 *data)
{
	if (sset == ETH_SS_STATS)
		memcpy(data, i6_stat_names, sizeof(i6_stat_names));
}

static void i6_get_ethtool_stats(struct net_device *dev, struct ethtool_stats *s, u64 *data)
{
	struct i6_priv *p = netdev_priv(dev);

	memcpy(data, &p->st, sizeof(p->st));
}

static int i6_get_coalesce(struct net_device *dev, struct ethtool_coalesce *c,
			   struct kernel_ethtool_coalesce *kc, struct netlink_ext_ack *e)
{
	struct i6_priv *p = netdev_priv(dev);

	c->rx_coalesce_usecs = p->peer->tick_ns / 1000;	/* engine feeding our RX */
	return 0;
}

static int i6_set_coalesce(struct net_device *dev, struct ethtool_coalesce *c,
			   struct kernel_ethtool_coalesce *kc, struct netlink_ext_ack *e)
{
	struct i6_priv *p = netdev_priv(dev);

	p->peer->tick_ns = max(c->rx_coalesce_usecs, 1U) * 1000;
	return 0;
}

```

코드에서 볼 것:

- `-S` 통계는 세 콜백의 약속이다: `get_sset_count`가 개수 N, `get_strings`가 이름 N개(각 `ETH_GSTRING_LEN` = 32바이트), `get_ethtool_stats`가 값 N개(`u64`). 순서가 맞아야 한다. 이름과 구조체 필드 순서를 하나의 표로 관리하는 것이 실무 관용구다.
- coalesce는 `supported_coalesce_params`에 지원 비트를 선언해야 한다. 선언 안 한 항목을 바꾸려 하면 코어가 `-EOPNOTSUPP`로 막는다.
- `get_coalesce`/`set_coalesce`가 **상대 포트의 엔진 주기**(`p->peer->tick_ns`)를 다루는 이유: 우리 RX 인터럽트를 늦추는 것은 우리에게 프레임을 배달하는 엔진이기 때문이다.

### 8.3 SIOCETHTOOL을 직접 부르는 도구 — `i6_ethtool`

```c
/* i6_ethtool: tiny ethtool via SIOCETHTOOL (-i drvinfo, -g ring, -c/-C coalesce, -S stats) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>

static int fd; static struct ifreq ifr;
static void eio(void *cmd, const char *what)
{
	ifr.ifr_data = cmd;
	if (ioctl(fd, SIOCETHTOOL, &ifr) < 0) { perror(what); exit(1); }
}

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s -i|-g|-c|-C usecs|-S dev\n", argv[0]); return 1; }
	const char *dev = argv[argc - 1], *op = argv[1];
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) { perror("socket"); return 1; }
	strncpy(ifr.ifr_name, dev, IFNAMSIZ - 1);
	if (!strcmp(op, "-i")) {
		struct ethtool_drvinfo d = { .cmd = ETHTOOL_GDRVINFO };
		eio(&d, "GDRVINFO");
		printf("driver: %s\nversion: %s\nbus-info: %s\nn_stats: %u\n",
		       d.driver, d.version, d.bus_info, d.n_stats);
	} else if (!strcmp(op, "-g")) {
		struct ethtool_ringparam r = { .cmd = ETHTOOL_GRINGPARAM };
		eio(&r, "GRINGPARAM");
		printf("rx %u/%u  tx %u/%u (current/max)\n",
		       r.rx_pending, r.rx_max_pending, r.tx_pending, r.tx_max_pending);
	} else if (!strcmp(op, "-c") || !strcmp(op, "-C")) {
		struct ethtool_coalesce c = { .cmd = ETHTOOL_GCOALESCE };
		eio(&c, "GCOALESCE");
		if (op[1] == 'C') {
			c.cmd = ETHTOOL_SCOALESCE;
			c.rx_coalesce_usecs = atoi(argv[2]);
			eio(&c, "SCOALESCE");
		}
		printf("rx-usecs: %u  rx-frames: %u  tx-usecs: %u\n",
		       c.rx_coalesce_usecs, c.rx_max_coalesced_frames, c.tx_coalesce_usecs);
	} else if (!strcmp(op, "-S")) {
		struct ethtool_drvinfo d = { .cmd = ETHTOOL_GDRVINFO };
		eio(&d, "GDRVINFO");
		struct ethtool_gstrings *s = calloc(1, sizeof(*s) + d.n_stats * ETH_GSTRING_LEN);
		struct ethtool_stats *v = calloc(1, sizeof(*v) + d.n_stats * 8);
		if (!s || !v) { perror("calloc"); return 1; }
		s->cmd = ETHTOOL_GSTRINGS; s->string_set = ETH_SS_STATS; s->len = d.n_stats;
		eio(s, "GSTRINGS");
		v->cmd = ETHTOOL_GSTATS; v->n_stats = d.n_stats;
		eio(v, "GSTATS");
		for (unsigned i = 0; i < d.n_stats; i++)
			printf("  %s: %llu\n", (char *)s->data + i * ETH_GSTRING_LEN,
			       (unsigned long long)v->data[i]);
	}
	return 0;
}
```

예제 2(§1.8)의 `-i`, `-g`, `-c`와 예제 4·5의 `-S`, `-C`가 이 프로그램의 출력이다. 구조는 단순하다: `struct ifreq`의 `ifr_data`에 `struct ethtool_*`를 가리키게 하고 첫 필드 `cmd`로 명령을 고른다.

### 8.4 예제 10 — netdevsim: 하드웨어 없이 드라이버 인터페이스 시험

`netdevsim`은 커널 셀프테스트용 가짜 장치다. sysfs에 `"<id> <포트 수> <큐 수>"`를 쓰면 포트가 생기고, ethtool 링·coalesce·features·devlink·XDP 오프로드 같은 **제어 경로**를 하드웨어 없이 시험할 수 있다(데이터 경로는 netdevsim끼리만 연결된다).

```sh
# t7_misc.sh 뒷부분 (게스트)
insmod ipv6.ko; insmod netdevsim.ko
echo "6 1 4" > /sys/bus/netdevsim/new_device; sleep 1
D=$(ls /sys/bus/netdevsim/devices/netdevsim6/net/); echo "netdev: $D"
readlink /sys/class/net/$D
./i6_ethtool -i $D; ./i6_ethtool -g $D; ./i6_ethtool -c $D; ./i6_ethfeat -k $D
echo "queues: $(ls /sys/class/net/$D/queues | tr '\n' ' ')"
ls /sys/kernel/debug/netdevsim/netdevsim6/
ls /sys/kernel/debug/netdevsim/netdevsim6/ports/0/ 2>/dev/null
```

```text
### netdevsim
netdev: eth1
../../devices/netdevsim6/net/eth1
driver: netdevsim
version: 6.18.55-sub
bus-info: netdevsim6
n_stats: 0
rx 512/4096  tx 512/4096 (current/max)
rx-usecs: 0  rx-frames: 0  tx-usecs: 0
active: tx-scatter-gather tx-checksum-ip-generic highdma tx-scatter-gather-fraglist tx-generic-segmentation rx-gro rx-lro tx-tcp-segmentation rx-udp_tunnel-port-offload
queues: rx-0 rx-1 rx-2 rx-3 tx-0 tx-1 tx-2 tx-3 
bpf_bind_accept                health
bpf_bind_verifier_accept       hwstats
bpf_bind_verifier_delay        max_macs
bpf_bound_progs                max_vfs
dont_allow_reload              ports
fail_reload                    rate_nodes
fail_trap_drop_counter_get     take_snapshot
fail_trap_group_set            test1
fail_trap_policer_counter_get  trap_flow_action_cookie
fail_trap_policer_set          udp_ports_ipv4_only
fib                            udp_ports_open_only
fw_update_flash_chunk_time_ms  udp_ports_shared
fw_update_overwrite_mask       udp_ports_static_iana_vxlan
fw_update_status               udp_ports_sync_all
bpf_map_accept           bpf_xdpdrv_accept        pp_hold
bpf_offloaded_id         bpf_xdpoffload_accept    queue_reset
bpf_tc_accept            dev                      udp_ports
bpf_tc_non_bound_accept  ethtool
```

출력에서 볼 것: 장치 번호 6, 포트 1개, 큐 4쌍(`rx-0..3`, `tx-0..3`)이 생겼다. 이름이 `eth1`인 것은 `eth0`(기본 virtio NIC)이 이미 있어서다. 링 512/4096, features는 SG·TSO·LRO까지 "있는 척"한다. debugfs에는 `fail_reload`, `bpf_xdpdrv_accept`, `pp_hold`(page_pool 페이지를 붙잡아 누수 검사), `queue_reset` 같은 **오류 주입 손잡이**가 있다. 실무에서 ethtool/devlink 관련 유저 공간 도구(NetworkManager 플러그인, 진단 데몬)를 하드웨어 없이 테스트할 때 쓴다.

---

## 9. 타임스탬프, PTP, 그리고 자동차·로보틱스 이더넷

### 9.1 타임스탬프는 어디서 찍히나

`SO_TIMESTAMPING` 소켓 옵션으로 패킷의 송수신 시각을 받을 수 있다(A9 §6). 찍는 위치가 정확도를 정한다.

| 종류 | 찍는 곳 | 요청 플래그 | 정확도 감각 |
|---|---|---|---|
| SW TX | 드라이버의 `skb_tx_timestamp()` (`ndo_start_xmit` 안) | `SOF_TIMESTAMPING_TX_SOFTWARE` | qdisc 이후, 링 대기 이전 — 수 µs ~ 수백 µs 흔들림 |
| SW RX | `netif_receive_skb` 경로(`net_timestamp_check`) | `SOF_TIMESTAMPING_RX_SOFTWARE` | IRQ + coalescing + NAPI 지연 포함 |
| HW TX | MAC/PHY가 선로에 내보낸 순간 → 드라이버가 완료 시 `skb_tstamp_tx()` | `SOF_TIMESTAMPING_TX_HARDWARE` | 수 ns ~ 수십 ns |
| HW RX | MAC/PHY가 SFD를 본 순간 → 디스크립터에 실림 → 드라이버가 `skb_hwtstamps(skb)->hwtstamp`에 기록 | `SOF_TIMESTAMPING_RX_HARDWARE` | 수 ns ~ 수십 ns |

하드웨어 타임스탬프를 쓰려면 드라이버가 (1) `get_ts_info`로 능력과 PHC 번호를 알리고, (2) `ndo_hwtstamp_set/get`(예전에는 `ndo_eth_ioctl`의 SIOCSHWTSTAMP)으로 켜고 끄며, (3) TX 완료와 RX poll에서 디스크립터의 시각을 skb에 붙이고, (4) MAC의 시계를 **PTP 하드웨어 클록(PHC)** 으로 `ptp_clock_register()`해 `/dev/ptpN`으로 노출한다. 그 시계를 `ptp4l`(IEEE 1588 / 802.1AS gPTP)이 네트워크 마스터에 맞추고, `phc2sys`가 시스템 시계를 PHC에 맞춘다 — Q3(시간 동기)의 주제다.

### 9.2 예제 11 — SW 타임스탬프 위치와 coalescing의 지연 비용

무엇을 확인하나: `i6_eth`의 `skb_tx_timestamp()`가 실제로 TX 타임스탬프를 만드는지, RX SW 타임스탬프가 coalescing(rx-usecs)에 따라 얼마나 늦어지는지, 하드웨어 타임스탬프 요청이 어떻게 거절되는지.

```c
/* i6_ts: where are timestamps taken?  tx <ip> <port> <n> | rx <port> <n> | hw <dev> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/net_tstamp.h>
#include <linux/sockios.h>
#include <linux/errqueue.h>

static double ts2us(struct timespec t) { return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
static double now_us(void) { struct timespec t; clock_gettime(CLOCK_REALTIME, &t); return ts2us(t); }
/* read one message (or error-queue entry) and return its SCM_TIMESTAMPING ts[0] */
static double get_ts(int fd, int flags, double *payload)
{
	char data[256], ctl[512]; struct iovec io = { data, sizeof(data) };
	struct msghdr m = { .msg_iov = &io, .msg_iovlen = 1, .msg_control = ctl, .msg_controllen = sizeof(ctl) };
	while (recvmsg(fd, &m, flags) < 0) usleep(10);		/* errqueue is non-blocking */
	if (payload) memcpy(payload, data, sizeof(*payload));
	for (struct cmsghdr *c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c))
		if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SO_TIMESTAMPING)
			return ts2us(((struct scm_timestamping *)CMSG_DATA(c))->ts[0]);
	return -1;
}

int main(int argc, char **argv)
{
	int fd = socket(AF_INET, SOCK_DGRAM, 0), n = argc > 4 ? atoi(argv[4]) : (argc > 3 ? atoi(argv[3]) : 0);
	struct sockaddr_in a = { .sin_family = AF_INET };
	if (fd < 0 || argc < 3) { perror("socket/args"); return 1; }
	if (!strcmp(argv[1], "hw")) {			/* ask the driver for HW timestamping */
		struct hwtstamp_config cfg = { .tx_type = HWTSTAMP_TX_ON, .rx_filter = HWTSTAMP_FILTER_ALL };
		struct ifreq ifr = { .ifr_data = (char *)&cfg };
		strncpy(ifr.ifr_name, argv[2], IFNAMSIZ - 1);
		if (ioctl(fd, SIOCSHWTSTAMP, &ifr) < 0) { perror("SIOCSHWTSTAMP"); return 1; }
		printf("%s: hw timestamping on, rx_filter granted=%d\n", argv[2], cfg.rx_filter);
		return 0;
	}
	int f = SOF_TIMESTAMPING_SOFTWARE | (argv[1][0] == 't' ? SOF_TIMESTAMPING_TX_SOFTWARE : SOF_TIMESTAMPING_RX_SOFTWARE);
	if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &f, sizeof(f)) < 0) { perror("SO_TIMESTAMPING"); return 1; }
	double s1 = 0, s2 = 0;
	if (!strcmp(argv[1], "tx")) {
		inet_pton(AF_INET, argv[2], &a.sin_addr); a.sin_port = htons(atoi(argv[3]));
		for (int i = 0; i < n; i++) {
			double t0 = now_us();
			if (sendto(fd, &t0, sizeof(t0), 0, (struct sockaddr *)&a, sizeof(a)) < 0) { perror("sendto"); return 1; }
			s1 += get_ts(fd, MSG_ERRQUEUE, NULL) - t0;	/* skb_tx_timestamp() in driver */
			usleep(10000);
		}
		printf("tx: sendto() -> driver xmit (SW TX stamp): avg %.1f us over %d\n", s1 / n, n);
	} else {
		a.sin_port = htons(atoi(argv[2]));
		if (bind(fd, (struct sockaddr *)&a, sizeof(a)) < 0) { perror("bind"); return 1; }
		for (int i = 0; i < n; i++) {
			double t0, rx = get_ts(fd, 0, &t0), app = now_us();
			s1 += rx - t0; s2 += app - rx;
		}
		printf("rx: sendto() -> RX SW stamp avg %.1f us, RX stamp -> app avg %.1f us over %d\n", s1 / n, s2 / n, n);
	}
	return 0;
}
```

```sh
# t7_misc.sh 앞부분 (게스트) — netns 준비는 예제 4와 같다
for us in 20 1000; do
  echo "### timestamps, rx-usecs=$us"
  nsenter -t $NS -n ./i6_ethtool -C $us i6_eth1
  nsenter -t $NS -n ./i6_ts rx 6000 50 & R=$!; sleep 0.3
  ./i6_ts tx 10.6.0.2 6000 50; wait $R
done
echo "### hw timestamp request on i6_eth0"; ./i6_ts hw i6_eth0
```

```text
### timestamps, rx-usecs=20
rx-usecs: 20  rx-frames: 0  tx-usecs: 0
rx: sendto() -> RX SW stamp avg 318.8 us, RX stamp -> app avg 246.5 us over 50
tx: sendto() -> driver xmit (SW TX stamp): avg 134.2 us over 50
### timestamps, rx-usecs=1000
rx-usecs: 1000  rx-frames: 0  tx-usecs: 0
rx: sendto() -> RX SW stamp avg 1362.9 us, RX stamp -> app avg 161.4 us over 50
tx: sendto() -> driver xmit (SW TX stamp): avg 79.7 us over 50
### hw timestamp request on i6_eth0
SIOCSHWTSTAMP: Operation not supported
```

출력에서 볼 것:

- TX: `sendto()` 호출부터 드라이버 `skb_tx_timestamp()`까지 평균 80~134 µs(TCG라 느리다; 실제 SoC에서는 수 µs). 이 값은 UDP/IP/qdisc 처리 시간이다. **이 타임스탬프는 링 대기와 선로 시간을 포함하지 않는다** — PTP에 SW 타임스탬프를 쓰면 안 되는 이유다.
- RX: rx-usecs 20에서 송신 → RX SW 스탬프 평균 319 µs, 1000으로 올리면 1363 µs. coalescing이 지연에 **그대로 더해졌다**(약 +1000 µs). 그 뒤 앱이 읽기까지 160~250 µs가 더 걸렸다.
- `SIOCSHWTSTAMP: Operation not supported`: `i6_eth`에 `ndo_hwtstamp_set`이 없으니 코어가 거절했다. 예제 9에서 e1000e는 같은 요청을 허락했다.

> 함정: 같은 머신 안의 두 netns라 송수신 시각을 같은 `CLOCK_REALTIME`으로 바로 뺄 수 있었다. 두 장비 사이라면 시계가 맞아 있지 않으므로 이런 단방향 지연 측정은 PTP 동기가 먼저다.

### 9.3 자동차·로보틱스 이더넷 메모

| 주제 | 내용 | Linux 쪽 |
|---|---|---|
| 100BASE-T1 / 1000BASE-T1 | 꼬임쌍선 **한 쌍**으로 100M/1G 전이중, 15 m, 차량·로봇 배선 경량화. 자동 협상 대신 **master/slave를 설정**해야 링크가 선다 | PHY 드라이버: `nxp-tja11xx.c`(TJA1100/1101), `nxp-c45-tja11xx.c`(TJA1103 등, PTP 포함), `dp83tc811.c`(100BASE-T1), `dp83tg720.c`(1000BASE-T1), `marvell-88q2xxx.c`, `microchip_t1.c`. master/slave는 `ethtool -s eth0 master-slave forced-master` |
| 10BASE-T1S / T1L | 멀티드롭(여러 노드가 한 선) 10M, CAN 대체 후보. PLCA로 충돌 회피 | `microchip_t1s.c`, `dp83td510.c`(T1L), PLCA 설정은 ethtool netlink |
| TSN | 시간 민감 네트워킹: 802.1AS(gPTP 시간 동기), 802.1Qbv(시간 슬롯 게이트), 802.1Qav(CBS 대역폭 예약) | qdisc `taprio`(Qbv), `cbs`, `etf`(전송 시각 지정) + 하드웨어 오프로드. stmmac(EQOS)·igc·enetc 등이 지원 |
| PTP 하드웨어 타임스탬프 | 센서 데이터(LiDAR, 카메라 트리거)에 공통 시간축 | `ptp4l -i eth0 -H`, `phc2sys`, `SO_TIMESTAMPING` + `SOF_TIMESTAMPING_RAW_HARDWARE`, PHY가 타임스탬프를 찍는 구조(`struct mii_timestamper`)도 있다 |
| 링크 품질 | 케이블 진단(TDR), SQI(signal quality index) | `ethtool --cable-test`, `ethtool` SQI(T1 PHY 다수 지원) |

> 확인 범위: 위 표의 PHY 드라이버 파일 이름은 6.18 소스 트리(`drivers/net/phy/`)에서 확인했다. ethtool의 `master-slave`, `--cable-test`, SQI 옵션은 이 BusyBox 게스트에 ethtool이 없어 **실행하지 않았다**(Ubuntu 등의 ethtool 5.x 이상에 있다).

---

## 10. 커널 안에서는 무슨 일이 일어나나

### 10.1 구조체 지도

| 구조체 | 하나가 나타내는 것 | 주요 필드 / 연결 |
|---|---|---|
| `struct net_device` | 인터페이스 하나 | `netdev_ops`, `ethtool_ops`, `features`, `_tx` (큐 배열), `phydev`, `mtu`, `dev.parent` |
| `struct netdev_queue` | TX 큐 하나 | `state`(DRV_XOFF / STACK_XOFF), `dql`(BQL), `_xmit_lock`, `trans_start` (watchdog) |
| `struct Qdisc` | 큐 앞의 스케줄러 | `enqueue`/`dequeue`, `q.qlen`, `qstats.backlog/drops` (`i6_qdisc`가 읽은 값) |
| `struct napi_struct` | poll 단위 하나 (큐 하나에 하나가 보통) | `poll`, `weight`, `state`(SCHED, MISSED, …), `gro` |
| `struct softnet_data` | CPU당 하나 | `poll_list`, `processed`, `time_squeeze`, backlog 큐 |
| `struct sk_buff` | 패킷 하나 | §2 |
| `struct page_pool` | RX 버퍼 재활용 캐시 (큐 하나에 하나) | per-CPU 없이 NAPI 문맥 직접 재활용 |
| `struct phy_device` / `struct mii_bus` | PHY / MDIO 버스 | §7 |

### 10.2 함수 경로 — 실측 콜 스택으로 다시 읽기

- **TX**: `__sys_sendto` → `inet_sendmsg` → `raw_sendmsg`/`udp_sendmsg` → `ip_output` → `ip_finish_output2` → `neigh_resolve_output` → `__dev_queue_xmit` → (qdisc가 비어 있고 `TCQ_F_CAN_BYPASS`면) `sch_direct_xmit` → `dev_hard_start_xmit` → `ndo_start_xmit`. qdisc가 차 있으면 `q->enqueue` 후 `__qdisc_run`이 `dequeue_skb` → `sch_direct_xmit`. 드라이버가 큐를 멈춰 있으면 skb는 qdisc에 남고, `netif_wake_queue` → `__netif_schedule` → `NET_TX_SOFTIRQ`(`net_tx_action`)가 다시 qdisc를 돌린다.
- **RX**: IRQ → ISR → `napi_schedule` → `____napi_schedule`(poll_list에 추가, `NET_RX_SOFTIRQ` raise) → `handle_softirqs` → `net_rx_action` → `__napi_poll` → 드라이버 poll → `napi_gro_receive` → (`napi_complete_done` 또는 GRO 배치가 차면) `netif_receive_skb_list_internal` → `__netif_receive_skb_list_core` → `ip_list_rcv`/`arp_rcv` → `udp_rcv` → `__udp_enqueue_schedule_skb`(소켓 큐, 여기서 rcvbuf 초과면 RcvbufErrors) → `sk_data_ready`로 `recvfrom()` 깨움.
- **watchdog**: `dev_watchdog`(timer) → 멈춘 큐의 `trans_start`가 `watchdog_timeo`보다 오래면 → `ndo_tx_timeout`.
- **링크**: phylib `phy_state_machine`(workqueue, 1초 폴링 또는 PHY IRQ) → `phy_link_change` → `netif_carrier_on/off` + `adjust_link`. 캐리어가 꺼지면 qdisc가 `noop`처럼 막히고 watchdog도 쉰다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 400"><defs><marker id="ah10" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="340.0" y="22.0" font-size="14" text-anchor="middle" font-weight="bold">드라이버 콜백은 어느 문맥에서 도나</text><rect x="20" y="44" width="190" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="115.0" y="78.5" font-size="13" text-anchor="middle">프로세스 + RTNL 락</text><rect x="220" y="44" width="130" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="285.0" y="78.2" font-size="12" text-anchor="middle">잠들 수 있음</text><rect x="360" y="44" width="300" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="510.0" y="70.2" font-size="12" text-anchor="middle">ndo_open / ndo_stop</text><text x="510.0" y="86.2" font-size="12" text-anchor="middle">ndo_change_mtu, ethtool_ops</text><rect x="20" y="114" width="190" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="115.0" y="148.6" font-size="13" text-anchor="middle">softirq (NET_RX)</text><rect x="220" y="114" width="130" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="285.0" y="148.2" font-size="12" text-anchor="middle">잠들 수 없음</text><rect x="360" y="114" width="300" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="510.0" y="140.2" font-size="12" text-anchor="middle">NAPI poll: RX + TX 완료</text><text x="510.0" y="156.2" font-size="12" text-anchor="middle">napi_gro_receive</text><rect x="20" y="184" width="190" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="115.0" y="218.6" font-size="13" text-anchor="middle">BH 꺼짐 + TX 큐 락</text><rect x="220" y="184" width="130" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="285.0" y="218.2" font-size="12" text-anchor="middle">잠들 수 없음</text><rect x="360" y="184" width="300" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="510.0" y="210.2" font-size="12" text-anchor="middle">ndo_start_xmit</text><text x="510.0" y="226.2" font-size="12" text-anchor="middle">(softirq 또는 프로세스)</text><rect x="20" y="254" width="190" height="60" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="115.0" y="288.6" font-size="13" text-anchor="middle">hardirq</text><rect x="220" y="254" width="130" height="60" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="285.0" y="288.2" font-size="12" text-anchor="middle">최소한만</text><rect x="360" y="254" width="300" height="60" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="510.0" y="280.2" font-size="12" text-anchor="middle">ISR: IRQ mask</text><text x="510.0" y="296.2" font-size="12" text-anchor="middle">+ napi_schedule</text><rect x="20" y="324" width="190" height="60" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="115.0" y="358.6" font-size="13" text-anchor="middle">timer softirq</text><rect x="220" y="324" width="130" height="60" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="285.0" y="358.2" font-size="12" text-anchor="middle">잠들 수 없음</text><rect x="360" y="324" width="300" height="60" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="510.0" y="350.2" font-size="12" text-anchor="middle">ndo_tx_timeout</text><text x="510.0" y="366.2" font-size="12" text-anchor="middle">→ 리셋은 workqueue로</text></svg>
```
그림 10 — 드라이버 콜백의 실행 문맥. `ndo_start_xmit`과 NAPI poll은 잠들 수 없으므로 `GFP_ATOMIC` 할당·spinlock만 쓴다. 느린 일(리셋, 펌웨어 로드, PHY 재설정)은 workqueue로 넘긴다(F5).

### 10.3 드라이버를 쓸 때 다시 만나는 곳

| 지식 | 어디서 다시 나오나 |
|---|---|
| platform driver probe + DT (`phy-handle`, `phy-mode`, `mdio`) | G3, D 모듈 — SoC MAC 드라이버의 probe |
| IRQ 요청, 스레드 IRQ, MSI-X | H6, H11 — 큐별 벡터 |
| streaming DMA, coherent 링 메모리 | H9 — 링은 `dma_alloc_coherent`, 버퍼는 `dma_map_single` 또는 page_pool DMA 매핑 |
| softirq, workqueue, hrtimer | F5, F6 — NAPI와 리셋 작업 |
| SocketCAN | I10 — CAN 컨트롤러도 `net_device` + NAPI(`can_rx_offload`) 구조다 |
| PTP/PHC | Q3 — `ptp_clock_info`, ptp4l |
| 실시간 | N 모듈 — threaded NAPI, IRQ affinity |

---

## 11. Embedded 실무에서 다시 보기

### 11.1 장면 1 — "LiDAR 포인트가 가끔 빈다"

센서 키트가 100 Mbps LiDAR UDP를 받는데 프레임 일부가 비어 있다. 드롭 위치를 **아래에서부터** 하나씩 확인한다(명령은 일반 Linux 기준; 이 게스트에서는 `i6_ethtool`·`i6_qdisc`로 대신했다).

| 순서 | 볼 곳 | 오르면 뜻 | 처방 |
|---|---|---|---|
| 1 | `ethtool -S eth0`의 `rx_crc_errors`, `rx_align_errors` | 선로·RGMII 타이밍 문제 | 케이블, `phy-mode` 지연 |
| 2 | `ethtool -S`의 `rx_missed_errors`/`rx_fifo_errors`, `ip -s link`의 missed | RX 링에 빈칸 없음(드라이버가 늦음) | `ethtool -G rx 4096`, coalescing 조정, IRQ affinity |
| 3 | `/proc/net/softnet_stat` 2열(dropped), 3열(time_squeeze) | backlog 초과 / softirq 예산 부족 | `netdev_budget`, RPS, threaded NAPI |
| 4 | `/proc/net/snmp`의 `Udp: RcvbufErrors` | 앱이 늦게 읽음 | `SO_RCVBUF` 키우기(A9), 수신 스레드 우선순위 |

예제 5에서 우리는 4번(RcvbufErrors 743)만 오르고 1~3은 0인 상황을 직접 만들었다.

### 11.2 장면 2 — 새 보드 이더넷 bring-up 체크리스트

1. DT: MAC 노드 `status = "okay"`, 클록·핀(pinctrl)·`phy-mode`·`phy-handle`·PHY `reg`·`reset-gpios` 확인.
2. 부팅 로그: MDIO 버스 등록, `PHY [30be0000.ethernet-1:01] driver [RTL8211F Gigabit Ethernet]` 같은 줄(PHY ID가 읽혔고 전용 드라이버가 붙었는지).
3. `ip link set eth0 up` 후 `Link is Up - 1Gbps/Full`.
4. `ping` + `ethtool -S` 에러 카운터 0인지. CRC 에러가 있으면 RGMII 지연부터.
5. `iperf3` 처리량, `ethtool -C` 조정, `/proc/interrupts`로 IRQ가 한 CPU에 몰리는지.
6. PTP가 필요하면 `ethtool -T`에서 PHC 번호 확인 → `ptp4l -H`.

### 11.3 장면 3 — 차량 이더넷 T1 PHY가 링크를 안 올린다

100BASE-T1은 자동 협상이 없어서 **양쪽 중 하나는 master, 하나는 slave**여야 한다. 둘 다 slave면 링크가 영원히 안 선다. PHY 스트랩 핀 기본값을 확인하고, 필요하면 `ethtool -s eth0 master-slave forced-master`나 DT/드라이버 설정으로 정한다(실행하지 않음 — §9.3 확인 범위 참조). 링크 품질은 SQI로 본다.

### 11.4 장면 4 — 제어 루프 지연이 튄다

로봇 제어기가 1 kHz로 이더넷 모터 드라이버(EtherCAT·UDP)에 명령을 보내는데 가끔 수 ms가 튄다. 예제 5·11이 보여 준 대로 coalescing(rx-usecs)과 softirq 경로가 지연을 더한다. 처방 후보: 해당 큐의 `rx-usecs 0`(또는 적응형 끄기), threaded NAPI + RT 우선순위, IRQ affinity로 제어 루프와 다른 CPU에 배치, 필요하면 busy poll(`SO_BUSY_POLL`), TSN `taprio`로 송신 슬롯 보장.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| xmit에서 링이 찬 뒤에야 `NETDEV_TX_BUSY` 반환 | CPU 100%, requeue 폭증, 로그 경고 | 큐를 미리 멈추지 않음 | 넣은 직후 남은 칸 검사 → `netif_stop_queue`, 완료 시 hysteresis로 wake |
| TX 완료에서 wake를 빼먹음 | 한동안 잘 되다 TX가 영원히 멈춤, NETDEV WATCHDOG | stop만 하고 wake 없음 / stop-wake 경쟁 | `netif_txq_maybe_stop` / `__netif_txq_completed_wake` 헬퍼 |
| poll이 budget을 다 쓰고도 `napi_complete_done` 호출 | 경고 `napi->poll` 위반, 패킷 정체 | NAPI 계약 위반 | `work == budget`이면 그대로 반환 |
| `napi_complete_done` 반환값 무시하고 IRQ unmask | 드물게 같은 NAPI가 두 CPU에서 돌아 링 손상 | busy poll·재예약 상황 | `if (napi_complete_done(...)) unmask` |
| BQL sent/completed 짝이 안 맞음, reset 누락 | `ifdown/up` 후 TX가 안 나감, inflight 고정 | `netdev_tx_reset_queue` 누락 | 링 비울 때 reset, 바이트 수 일치 |
| RX 버퍼 할당 실패 시 칸을 비워 둠 | 메모리 압박 후 RX가 점점 줄다 멈춤 | 링에 구멍 | 새 버퍼 먼저 확보, 실패하면 패킷 드롭 + 옛 버퍼 재사용 |
| OWN 설정 전에 `dma_wmb()` 없음 | arm64에서만 드물게 쓰레기 프레임 송신 | 디스크립터 쓰기 재정렬 | `dma_wmb()` / `dma_rmb()` |
| stop에서 DMA 멈추기 전에 버퍼 해제 | 랜덤 메모리 손상, IOMMU fault | 순서 오류 | 막기 → 엔진·NAPI 정지 → 해제 |
| `phy-mode` RGMII 지연 불일치 | 링크 Up, ping 실패, rx_crc_errors 증가 | 클록 skew | `rgmii-id`/`-rxid`/`-txid`를 PCB에 맞게 |
| netdev 부모 없이 phylib 연결 | `phy_attach_direct`에서 NULL 역참조 Oops (이 노트에서 실제로 겪음) | `dev->dev.parent->driver` 사용 | `SET_NETDEV_DEV(ndev, &pdev->dev)` |

---

## 13. 면접에서 이렇게 말한다

**Q.** Explain NAPI. Why does Linux need it?

**A.** NAPI는 인터럽트와 폴링을 섞는 RX 처리 방식이다. 첫 패킷의 IRQ에서 드라이버는 장치 IRQ를 끄고 `napi_schedule`만 한다. 그러면 NET_RX softirq가 드라이버의 poll을 budget(보통 64)만큼 부른다. budget보다 적게 처리하면 링이 빈 것이니 `napi_complete_done` 후 IRQ를 다시 켜고, budget을 다 쓰면 IRQ를 끈 채로 계속 poll 한다. 그래서 부하가 낮으면 지연이 짧은 인터럽트 방식이고, 높으면 인터럽트 폭풍(receive livelock) 없이 배치 처리한다. GRO도 이 poll 단위에서 일어난다. 제 소프트웨어 NIC 실험에서 coalescing을 1000 µs로 하자 패킷당 IRQ가 0.6에서 0.04로 줄고 처리량은 약 2.5배가 됐다.

> NAPI is Linux's hybrid of interrupts and polling for receive. On the first packet the ISR masks the device interrupt and calls napi_schedule. The NET_RX softirq then calls the driver's poll function with a budget, typically 64 packets. If poll finishes with less than the budget, the ring is empty, so it calls napi_complete_done and re-enables the interrupt; if it used the whole budget, it stays in polling mode with the interrupt off. So at low load you get interrupt latency, and at high load you get batching without an interrupt storm or receive livelock. GRO and buffer recycling also happen at that poll granularity. In my own emulated NIC, raising the coalescing interval to one millisecond cut interrupts per packet from about 0.6 to 0.04 and more than doubled throughput, at the cost of about a millisecond of extra latency.

**꼬리 질문**: "What does napi_complete_done returning false mean?" → busy poll이 NAPI를 가져갔거나 처리 중 다시 예약(MISSED)되어서 IRQ를 켜면 안 된다는 뜻.

**Q.** What happens when the TX ring is full?

**A.** 좋은 드라이버는 링이 꽉 찬 상태로 skb를 받지 않는다. xmit에서 디스크립터를 넣은 직후 남은 칸이 임계값 아래면 `netif_stop_queue`로 큐를 멈춘다. 그러면 스택은 그 큐에 더 이상 xmit을 부르지 않고 패킷을 qdisc에 쌓는다. TX 완료 인터럽트 → NAPI poll에서 skb를 회수하고 충분한 칸(예: 1/4)이 생기면 `netif_wake_queue`로 다시 연다. qdisc도 차면 drop 하거나, 소켓 송신 버퍼가 차서 `sendto`가 블록된다. 그 앞에 BQL이 있어서 실제로는 링이 차기 전에 바이트 기준으로 먼저 멈추는 경우가 많다 — 제 실험에서 BQL을 켜면 16칸 링 중 4칸만 쓰고 큐가 멈췄고 끄면 링이 차서 드라이버가 수백 번 stop/wake 했다. 오래 멈춰 있으면 watchdog이 `ndo_tx_timeout`을 불러 리셋한다.

> A well-behaved driver never gets an skb it cannot place. Right after queueing a descriptor, xmit checks the free slots and calls netif_stop_queue if it is below a threshold. The stack then stops calling xmit for that queue and packets accumulate in the qdisc. When TX completions are cleaned in NAPI poll and enough slots are free, the driver calls netif_wake_queue and the qdisc drains. If the qdisc overflows it drops, and for a blocking socket the send buffer fills up and sendto blocks, so backpressure goes all the way to the application. In practice BQL usually stops the queue earlier, based on bytes in flight, to keep latency low. If the queue stays stopped longer than watchdog_timeo, the netdev watchdog calls ndo_tx_timeout and the driver resets the hardware. Returning NETDEV_TX_BUSY is the anti-pattern.

**꼬리 질문**: "What is BQL and why not just use a huge ring?" → 큰 링은 qdisc의 스케줄링(fq_codel, 우선순위)을 무력화하고 지연을 키운다(bufferbloat). BQL은 하드웨어가 놀지 않을 만큼만 넣는다.

**Q.** Walk me through the life of a received packet in a NIC driver.

**A.** 드라이버는 open에서 RX 링에 빈 버퍼(보통 page_pool 페이지)를 깔아 OWN을 하드웨어에 준다. 패킷이 오면 MAC이 DMA로 버퍼에 쓰고 디스크립터에 길이·상태를 쓰고 OWN을 돌려준 뒤 IRQ를 낸다. ISR은 마스크 + `napi_schedule`. poll은 상태 비트를 확인하고 `dma_rmb` 후 길이를 읽고, `napi_build_skb`로 버퍼 위에 skb를 만들고, 체크섬·VLAN·해시·타임스탬프 같은 메타데이터를 채우고, `eth_type_trans` 후 `napi_gro_receive`로 올린다. 그리고 새 버퍼로 그 칸을 다시 채워 하드웨어에 준다. 이후는 softirq 안에서 IP·UDP를 거쳐 소켓 큐에 들어가고 앱을 깨운다.

> At open the driver posts empty buffers, usually page_pool pages, into the RX ring and gives ownership to the hardware. When a frame arrives the MAC DMAs it into a buffer, writes length and status into the descriptor, flips ownership back and raises an interrupt. The ISR masks it and schedules NAPI. In poll the driver checks the done bit, does a dma_rmb, reads the length, wraps the buffer with napi_build_skb without copying, fills in checksum status, VLAN tag, RSS hash and timestamps, calls eth_type_trans and hands it to napi_gro_receive. Then it refills that slot with a fresh buffer. The rest — IP, UDP, the socket queue and waking the reader — runs in the same softirq.

**Q.** Where can a packet be dropped on receive, and how do you find out which?

**A.** 네 군데를 아래부터 본다. 선로·MAC(CRC, alignment 에러 — ethtool -S), RX 링 빈칸 부족(rx_missed/fifo — 드라이버가 못 따라감), 스택 backlog·예산(softnet_stat의 dropped, time_squeeze), 소켓 버퍼(snmp RcvbufErrors — 앱이 못 따라감). 위치에 따라 처방이 다르다: 링 크기, coalescing·affinity, netdev_budget·RPS, SO_RCVBUF·스레드 우선순위.

> I check bottom-up. Wire and MAC errors like CRC and alignment show in ethtool -S. Ring overruns — no free RX descriptor — show as rx_missed or fifo errors, meaning the driver or CPU can't keep up. Then the stack: softnet_stat shows backlog drops and time squeeze. Finally the socket: RcvbufErrors in /proc/net/snmp means the application isn't reading fast enough. Each has a different fix: ring size, coalescing and IRQ affinity, netdev budget or RPS, or a bigger SO_RCVBUF and a higher-priority reader thread.

**Q.** What is the split between MAC and PHY, and how does Linux model it?

**A.** MAC은 SoC 안의 프레이밍·DMA 블록이고 PHY는 아날로그 송수신과 협상을 하는 별도 칩이다. 데이터는 RGMII 같은 MII 계열로, 관리는 MDIO 버스로 오간다. Linux는 MDIO를 `mii_bus`, PHY를 `phy_device` + PHY 드라이버로 모델링하고, phylib 상태 머신이 링크를 감시해 carrier와 MAC 콜백을 부른다. phylink는 그 위에서 SFP·PCS·fixed-link까지 일관되게 다룬다. DT에서는 `phy-handle`, `phy-mode`, `mdio` 하위 노드, 또는 `fixed-link`로 기술한다.

> The MAC is the digital block in the SoC that frames packets and does DMA; the PHY is a separate chip that handles the analog line and autonegotiation. Data flows over an MII-family interface like RGMII, and management goes over the two-wire MDIO bus. Linux models the bus as mii_bus, each PHY as a phy_device bound to a PHY driver, and phylib runs a state machine that tracks the link, toggles carrier and calls the MAC's link callback. Phylink sits on top to handle SFPs, PCS and fixed links uniformly. In device tree it's phy-handle, phy-mode and an mdio subnode, or a fixed-link node for a switch connection. The classic bring-up bug is wrong RGMII delay: link comes up but frames fail CRC.

**Q.** How do hardware timestamps work and why do they matter for robots?

**A.** MAC이나 PHY가 프레임의 SFD가 선로를 지나는 순간 자기 PTP 클록 값을 찍어 디스크립터에 넣고, 드라이버가 그 값을 skb의 hwtstamp로 옮긴다. TX는 완료 시 `skb_tstamp_tx`로 소켓 에러 큐에 돌려준다. 소프트웨어 타임스탬프는 qdisc·링·coalescing·softirq 지연이 섞여 수십~수백 µs 흔들리지만 하드웨어는 ns 단위다. 이걸로 ptp4l이 PHC를 맞추면 LiDAR·카메라·IMU 데이터에 공통 시간축이 생겨 센서 융합이 맞는다.

> The MAC or PHY latches its PTP hardware clock when the frame's start delimiter crosses the wire, puts it in the descriptor, and the driver attaches it to the skb as hwtstamp; for TX the driver returns it on completion via skb_tstamp_tx to the socket error queue. Software timestamps include qdisc, ring, coalescing and softirq delays, so they jitter by tens to hundreds of microseconds — I measured coalescing adding a full millisecond to the RX software stamp. Hardware stamps are nanosecond-level. With ptp4l disciplining the PHC and phc2sys syncing the system clock, LiDAR, camera and IMU data share one timebase, which sensor fusion depends on.

**Q.** What is a page_pool and why do modern drivers use it?

**A.** RX 버퍼용 페이지 재활용 캐시다. NAPI 문맥에서 락 없이 페이지를 꺼내고, 스택이 skb를 다 쓰면(`skb_mark_for_recycle`) 페이지가 할당자 대신 풀로 돌아온다. DMA 매핑도 풀에 붙여 두므로 패킷마다 map/unmap 비용이 사라진다. XDP와 AF_XDP도 이 구조를 전제로 한다.

> page_pool is a recycling allocator for RX buffers. Pages come out of it lock-free in NAPI context, and when the stack frees an skb marked for recycle the page goes back to the pool instead of the page allocator. The pool can also keep the DMA mapping, so per-packet map and unmap disappears, which matters a lot on non-coherent Arm SoCs. XDP and AF_XDP build on it.

---

## 14. 직접 해보기

1. (개념) `i6_eth`의 `ring=16`에서 BQL을 끄고 `tick_us=1000 burst=1`로 보냈을 때 wake 임계값을 `ring/4`에서 1로 바꾸면 `tx_queue_stops`가 어떻게 변할지 예측하고 실행해 확인하라.
   정답/힌트: 빈칸 하나마다 wake → 곧바로 다시 stop. stop/wake 수가 패킷 수에 가까워진다(hysteresis가 사라짐).
2. (코드) `i6_eth`에 `ETHTOOL_COALESCE_RX_MAX_FRAMES`를 추가해 "rx-frames개가 쌓이면 tick을 기다리지 않고 즉시 전달"을 구현하고 예제 5처럼 측정하라.
   정답/힌트: `i6_hw_tick`에서 전달 수를 세다가 임계값에서 IRQ, `supported_coalesce_params`에 비트 추가, `get/set_coalesce`에 `rx_max_coalesced_frames`.
3. (코드) `i6_eth`에 `NETIF_F_SG`를 켜고 xmit에서 frags를 복사하도록 바꾼 뒤 `i6_ethfeat -k`로 GSO가 살아나는지 확인하라.
   정답/힌트: `dev->hw_features = dev->features = NETIF_F_SG;` 그리고 hw tick에서 `skb_copy_bits(skb, 0, dst, skb->len)`로 비선형 skb를 복사.
4. (관찰) `stack=1` 대신 `t2_ping.sh`에서 UDP를 먼저 보내 첫 RX 스택에 `udp_rcv`가 보이는지 확인하라. 왜 i6_poll에서 찍은 스택에는 `udp_rcv`가 안 보이나?
   정답/힌트: `dump_stack()`은 호출 시점의 스택만 보인다. `napi_gro_receive` 이후 단계는 아직 호출 전이다 — 상위 단계를 보려면 GRO flush 이후(예: `napi_complete_done` 안쪽)에서 찍히는 xmit 스택처럼 "그 아래에서" 일어나는 일을 찍어야 한다.
5. (관찰) 예제 9의 QEMU 명령에 `-device virtio-net-pci,netdev=n2,mq=on,vectors=6`과 `-netdev tap,…,queues=2`를 쓰면 큐가 몇 개가 되는가? user 백엔드로는 왜 안 되나?
   정답/힌트: 멀티큐는 tap(vhost) 백엔드가 지원한다. user(slirp)는 단일 큐다(이 노트에서는 실행하지 않음).
6. (설계) 1 kHz 로봇 제어 패킷과 100 Mbps LiDAR가 같은 NIC을 쓴다. coalescing, 큐, qdisc를 어떻게 나누겠는가?
   정답/힌트: 멀티큐 NIC이면 제어 흐름을 별도 큐(ntuple/flow steering)로, 그 큐는 rx-usecs 0 + threaded NAPI 고우선순위, LiDAR 큐는 큰 링 + coalescing. 송신은 `mqprio`/`taprio`로 우선순위.
7. (/proc 관찰) 예제 5를 돌리며 다른 터미널(또는 백그라운드)에서 `cat /proc/net/softnet_stat`과 `/proc/interrupts`를 0.5초마다 찍어 time_squeeze와 IRQ 증가율을 비교하라.
   정답/힌트: 이 TCG 환경에서는 time_squeeze가 거의 0이다. `netdev_budget`을 20으로 낮추면(`sysctl -w net.core.netdev_budget=20`) 오르기 시작한다 — 게스트 전용 sysctl이므로 공용 상태를 바꾸지 않는다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| `net_device` | 네트워크 인터페이스 객체 | 이름·MAC·MTU·큐·features·ops 표를 가진다 |
| `net_device_ops` | 드라이버 콜백 표 | open/stop/start_xmit/get_stats64 등 |
| `sk_buff` (skb) | 패킷 그릇 | head/data/tail/end 포인터 + 메타데이터 + frags |
| headroom / tailroom | 앞·뒤 여유 공간 | 복사 없이 헤더를 붙이고 데이터를 늘리는 자리 |
| linear / nonlinear skb | 선형 / 조각 있는 skb | `data_len > 0`이면 frags나 frag_list가 있다 |
| 디스크립터 링 | NIC과 공유하는 원형 배열 | 칸마다 버퍼 주소·길이·상태(OWN) |
| doorbell | 새 일을 알리는 MMIO 쓰기 | TX tail 레지스터 |
| qdisc | 큐잉 규칙 | 드라이버 앞의 스케줄러 (pfifo_fast, fq_codel, taprio) |
| BQL | Byte Queue Limits | 하드웨어 큐에 넣을 바이트 수를 동적으로 제한 |
| NAPI | 인터럽트 + 폴링 RX 방식 | IRQ로 깨우고 budget만큼 poll |
| budget / weight | poll 한 번의 최대 처리 수 | 기본 64 |
| softirq | 지연된 인터럽트 처리 문맥 | NET_RX, NET_TX. 잠들 수 없음 |
| GRO | Generic Receive Offload | 같은 흐름 패킷을 합쳐 스택 비용을 줄임 |
| TSO / GSO | 송신 분할 오프로드 | 큰 skb를 하드웨어 / 드라이버 직전 소프트웨어가 MSS로 자름 |
| page_pool | RX 페이지 재활용 캐시 | DMA 매핑까지 재사용 |
| RSS | Receive Side Scaling | 해시로 RX 큐(=CPU)를 고름 |
| interrupt coalescing | 인터럽트 모으기 | `rx-usecs`, `rx-frames` |
| busy poll | 앱 스레드가 직접 poll | 지연 최소화 |
| MAC / PHY | 디지털 프레이밍 블록 / 아날로그 송수신 칩 | RGMII로 데이터, MDIO로 관리 |
| MDIO / MDC | PHY 관리 2선 버스 | 주소 0~31, C22·C45 레지스터 |
| phylib / phylink | PHY 프레임워크 / MAC-PHY 연결 층 | 상태 머신, SFP·PCS·fixed-link |
| fixed-link | PHY 없는 고정 링크 | 스위치 직결용 가짜 PHY |
| ethtool_ops | 진단·설정 콜백 표 | drvinfo, ring, coalesce, stats, ts_info |
| PHC | PTP Hardware Clock | NIC의 시계, `/dev/ptpN` |
| SO_TIMESTAMPING | 타임스탬프 소켓 옵션 | SW/HW, TX/RX 시각 받기 |
| 100BASE-T1 | 단일 쌍 차량 이더넷 | master/slave 설정 필요 |
| TSN | Time-Sensitive Networking | gPTP, Qbv(taprio), CBS |
| XDP / AF_XDP | skb 전 eBPF 처리 / 유저 공간 zero-copy 소켓 | 드라이버 RX 경로의 가장 앞 |

---

## 16. 요약 & 체크리스트

네트워크 드라이버는 `net_device`를 등록하고 `net_device_ops`로 스택과 대화한다. 송신은 qdisc → `ndo_start_xmit` → TX 디스크립터 + doorbell → 완료 IRQ → NAPI poll에서 회수이고, 링이 차면 `netif_stop_queue`/`netif_wake_queue`, 바이트 기준으로는 BQL이 역압력을 만든다. 수신은 미리 깔아 둔 page_pool 버퍼에 하드웨어가 DMA → IRQ에서 마스크 + `napi_schedule` → softirq의 poll이 budget만큼 `napi_build_skb` + `napi_gro_receive` → 비면 `napi_complete_done` + unmask다. `sk_buff`는 headroom을 둔 포인터 네 개로 복사 없이 헤더를 붙이고 벗긴다. PHY는 MDIO로 관리되는 별도 칩이고 phylib/phylink가 링크를 관리하며 DT의 `phy-mode`·`phy-handle`·`fixed-link`로 기술된다. ethtool_ops는 링·coalescing·통계·타임스탬프 능력을 보여 주는 진단 창구다. `i6_eth` 실험으로 coalescing이 처리량을 올리고 지연을 늘리는 것, BQL이 링보다 먼저 큐를 멈추는 것, 드롭이 드라이버·스택·소켓 중 어디서 일어나는지를 숫자로 확인했다.

- [ ] `net_device_ops`의 주요 콜백 7개와 각각의 실행 문맥을 말할 수 있다
- [ ] `alloc_etherdev` → (NAPI, PHY 준비) → `register_netdev` 순서와 이유를 설명할 수 있다
- [ ] `skb_reserve/put/push/pull`의 포인터 변화와 headroom의 이유를 그림으로 그릴 수 있다
- [ ] TX 디스크립터를 채우고 `dma_wmb` → OWN → doorbell 하는 코드를 쓸 수 있다
- [ ] TX 링이 찼을 때의 stop/wake와 BQL, qdisc, 소켓까지의 역압력을 설명할 수 있다
- [ ] NAPI poll 함수를 계약(budget, `napi_complete_done` 반환값)에 맞게 쓸 수 있다
- [ ] page_pool + `napi_build_skb` + `skb_mark_for_recycle`로 복사 없는 RX를 구현할 수 있다
- [ ] interrupt coalescing의 처리량·지연 트레이드오프를 측정으로 보일 수 있다
- [ ] RX 드롭 위치 네 곳과 각 카운터를 안다
- [ ] MAC/PHY/MDIO 분리, phylib/phylink, DT의 `phy-mode`/`phy-handle`/`fixed-link`를 설명할 수 있다
- [ ] `ethtool -S/-g/-c/-k/-T`가 어떤 `ethtool_ops` 콜백으로 가는지 안다
- [ ] SW와 HW 타임스탬프가 찍히는 위치와 PTP·T1 PHY·TSN이 로보틱스에서 왜 필요한지 말할 수 있다

## 참고 자료

- docs.kernel.org — Networking: "NAPI" (`Documentation/networking/napi.rst`), "Softnet Driver Issues" (`networking/driver.rst`), "Scaling in the Linux Networking Stack" (`networking/scaling.rst`, RSS/RPS/XPS), "Timestamping" (`networking/timestamping.rst`), "Page Pool API" (`networking/page_pool.rst`), "PHY Abstraction Layer" (`networking/phy.rst`), "phylink" (`networking/sfp-phylink.rst`), "ethtool netlink" (`networking/ethtool-netlink.rst`), "Segmentation Offloads" (`networking/segmentation-offloads.rst`), "Checksum Offloads" (`networking/checksum-offloads.rst`)
- man7.org — `socket(7)`의 `SO_BUSY_POLL`, `SO_TIMESTAMPING` 관련 절, `netdevice(7)`, `packet(7)`
- elixir.bootlin.com (v6.18) — `include/linux/netdevice.h`, `include/linux/skbuff.h`, `include/net/netdev_queues.h`, `net/core/dev.c`(`__dev_queue_xmit`, `net_rx_action`, `netdev_fix_features`), `net/core/net-procfs.c`, `drivers/net/ethernet/freescale/fec_main.c`, `drivers/net/ethernet/stmicro/stmmac/stmmac_main.c`, `drivers/net/phy/phy.c`, `drivers/net/phy/fixed_phy.c`, `drivers/net/netdevsim/`
- Kerrisk, "The Linux Programming Interface" — 56~61장 (소켓, 유저 쪽 배경)
- Bootlin, "Linux kernel and driver development" 교육 자료 (bootlin.com/training/kernel) — 네트워크 드라이버 개요
- 선행·연결 노트: A9(소켓), F5(softirq/workqueue), F6(타이머), G1(dummy net_device), G3(platform driver), H6(IRQ), H9(DMA), I10(CAN), Q3(시간 동기)
