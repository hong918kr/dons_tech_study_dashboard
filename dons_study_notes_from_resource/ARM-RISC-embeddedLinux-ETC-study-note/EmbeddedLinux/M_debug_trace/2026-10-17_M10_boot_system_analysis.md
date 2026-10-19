# M10. 부팅·시스템 분석 — 리셋부터 READY까지의 타임라인, 그리고 "느리다"를 숫자로 바꾸는 도구들

> **이 노트를 다 읽으면**: TF-A → U-Boot → 커널 → initcall → init → 앱 READY 전 구간을 한 시계로 재서 타임라인 그림을 만들 수 있다 · U-Boot `bootstage`, `initcall_debug` + `bootgraph.pl`, 부팅 시점 ftrace(`trace_event=`, `ftrace=`)로 "어디서 시간이 새는지"를 찾을 수 있다 · 느린 probe를 async probe·deferred probe로 다루는 법과 그 효과를 숫자로 말할 수 있다 · vmstat/iostat/mpstat/pidstat/PSI로 CPU·I/O·메모리 병목을 구별하고 "load average 8인데 CPU는 놀고 있다"를 설명할 수 있다
> **왜 필요한가**: "부팅 25 s를 5 s로 줄여라", "장비가 가끔 느려진다, 원인을 찾아라"는 Embedded Linux 현업 과제 1순위이고, 면접에서는 "어떻게 접근하나"를 순서대로 말할 수 있는지를 본다.
> **Don 기준 난이도**: 측정 → 가장 큰 덩어리부터 → 다시 측정이라는 성능 튜닝 태도, scope/GPIO 타이밍은 이미 몸에 있다 / 각 부팅 단계가 쓰는 시계와 로그, 커널 커맨드라인 트레이싱 옵션, Linux의 load average·PSI 정의는 새로 배울 부분
> **선행 노트**: C1(부팅 지도), C3·C4(U-Boot 사용·포팅, `initf_bootstage`), C5(TF-A), C6(커널 시작 경로), **C7(부팅 시간 측정 하네스, icount, `bootdelay`·`quiet`·`initcall_blacklist`·Image.gz·initramfs 압축 실험 — 이 노트는 그 숫자를 다시 재지 않는다)**, L1(`systemd-analyze blame/critical-chain/plot`, 서비스 정리 — 다시 하지 않는다), G3·D3(probe와 매칭), K(루트 FS 크기), M1(printk), M5(ftrace), M6(perf), M11(GPIO + scope)

---

## 0. 큰 그림 — 두 가지 "느리다"

Embedded Linux 장비에서 "느리다"는 두 종류다.

- **부팅이 느리다**: 전원 투입부터 "제품이 일할 준비가 된 순간(READY)"까지가 요구사항보다 길다. 한 번 일어나는 직렬 사건이라 **타임라인**으로 본다. 질문은 "어느 구간이 얼마나 먹나?"다.
- **돌다가 느려진다**: 정상 동작 중에 지연·처리량이 나빠진다. 계속 일어나는 일이라 **비율과 대기열**로 본다. 질문은 "CPU가 모자라나, 디스크를 기다리나, 메모리가 모자라나?"다.

SSD 펌웨어에 비유하면 앞쪽은 "파워온 후 Ready까지의 시퀀스 분석"(각 단계에 GPIO를 토글하고 scope로 찍던 그것), 뒤쪽은 "런타임 성능 카운터 분석"(큐 깊이, 대기 시간, 자원 사용률)이다. Linux는 이 두 가지를 위해 서로 다른 도구 세트를 준다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="14">① 부팅 분석 — 한 번 일어나는 직렬 타임라인</text>
  <rect x="20" y="36" width="80" height="36" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="60" y="59" font-size="12" text-anchor="middle">ROM·TF-A</text>
  <rect x="104" y="36" width="90" height="36" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="149" y="59" font-size="12" text-anchor="middle">U-Boot</text>
  <rect x="198" y="36" width="120" height="36" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="258" y="59" font-size="12" text-anchor="middle">커널·initcall</text>
  <rect x="322" y="36" width="100" height="36" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="372" y="59" font-size="12" text-anchor="middle">init·서비스</text>
  <rect x="426" y="36" width="80" height="36" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="466" y="59" font-size="12" text-anchor="middle">앱 READY</text>
  <line x1="20" y1="84" x2="506" y2="84" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="520" y="88" font-size="12">시간</text>
  <text x="20" y="106" font-size="12">도구: scope+GPIO, 시리얼 타임스탬프, bootstage,</text>
  <text x="20" y="124" font-size="12">printk.time, initcall_debug, bootgraph.pl, 부팅 ftrace, systemd-analyze</text>
  <line x1="20" y1="146" x2="660" y2="146" stroke="#888" stroke-dasharray="4 3"/>
  <text x="20" y="172" font-size="14">② 정상 상태 분석 — 계속 일어나는 경쟁과 대기</text>
  <rect x="20" y="186" width="200" height="76" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="206" font-size="12" text-anchor="middle">CPU 포화</text>
  <text x="120" y="226" font-size="12" text-anchor="middle">vmstat r · mpstat</text>
  <text x="120" y="246" font-size="12" text-anchor="middle">PSI cpu</text>
  <rect x="240" y="186" width="200" height="76" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="206" font-size="12" text-anchor="middle">I/O 대기</text>
  <text x="340" y="226" font-size="12" text-anchor="middle">vmstat b·wa · iostat -x</text>
  <text x="340" y="246" font-size="12" text-anchor="middle">PSI io</text>
  <rect x="460" y="186" width="200" height="76" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="206" font-size="12" text-anchor="middle">메모리 부족</text>
  <text x="560" y="226" font-size="12" text-anchor="middle">vmstat si·so · free</text>
  <text x="560" y="246" font-size="12" text-anchor="middle">PSI memory</text>
  <text x="20" y="288" font-size="12">공통 질문: "일하는 중인가(utilization), 줄 서 있나(saturation), 실패하나(errors)?"</text>
  <text x="20" y="310" font-size="12">— Brendan Gregg의 USE 방법. 자원마다 세 가지를 묻는다.</text>
</svg>
```
그림 1 — 이 노트의 두 축. 위는 직렬 타임라인(어느 구간이 긴가), 아래는 자원 경쟁(무엇을 기다리나).

이 노트의 구성은 이렇다.

| 절 | 내용 | 실행한 것 |
|---|---|---|
| 1 | 전 구간 타임라인을 한 시계로 | TF-A→U-Boot→커널→initramfs 전 체인을 `m10_ts.py`로 3회 |
| 2 | U-Boot `bootstage` | `CONFIG_BOOTSTAGE` U-Boot를 직접 빌드, `bootstage report`, DT로 넘긴 기록 읽기 |
| 3 | 커널 구간 — `initcall_debug`, bootgraph | Top 20 표, `bootgraph.pl`, 직접 그린 간트 차트 |
| 4 | 부팅 시점 ftrace | `trace_event=initcall:*`, `ftrace=function_graph ftrace_graph_filter=` |
| 5 | 느린 probe — sync / async / deferred | `m10_slow.ko`(probe에서 `msleep(500)`) 4가지 변형 |
| 6 | 커널 크기·압축·콘솔·rootfs | C7 실측 요약 + 판단 기준 |
| 7 | 유저 공간 — systemd-analyze, bootchart | systemd 게스트에서 `systemd-analyze` |
| 8 | fast boot 기법 목록 | (가이드) |
| 9 | 정상 상태 — vmstat, iostat, mpstat, pidstat, sar, top, PSI | CPU·I/O·메모리 hog를 하나씩 돌리며 신호 비교, D 상태와 load average |
| 10 | 증상 → 도구 지도 | |

### 0.1 실습 환경

- Mac의 Lima VM `elinux` (aarch64, 호스트 커널 `7.0.0-38-generic`, QEMU 10.2.1, KVM 없음 → TCG 에뮬레이션).
- 게스트 커널: 랩의 Linux 6.18.55 — `Image`(defconfig 계열), `Image-trace`(6.18.55-trace, ftrace·schedstats·PSI 등 전부 켬), `Image-sec`(6.18.55-sec, systemd 게스트용).
- 펌웨어: 랩 `/var/lab/out/flash.bin`(TF-A v2.15 BL1 + FIP)의 BL33을 **내가 빌드한 bootstage U-Boot로 바꾼 사본**.
- 유저 공간: BusyBox initramfs(빠른 커널 실험용), Ubuntu 24.04 + systemd 255 `rootfs-trace` 사본(정상 상태 실험용).

> **숫자를 읽을 때 꼭 기억할 것**: 이 VM은 다른 노트 작성자 16명이 동시에 쓰고 있어 호스트 load average가 7~15였다. TCG 에뮬레이션 + 공유 부하라서 **절대 시간은 실제 보드의 숫자가 아니다**(같은 커널이 1회차 5.6 s, 다른 회차 50 s에 부팅했다). 이 노트의 숫자는 "도구가 무엇을 보여 주는가"와 "같은 조건에서의 상대 비교"로 읽는다. 재현성이 필요한 비교는 C7처럼 QEMU `-icount`를 쓴다(C7 §4.3).

---

## 1. 전 구간 타임라인 — 한 시계로 잰다

### 1.1 직관 — 릴레이 경주를 한 개의 스톱워치로

부팅은 릴레이다. Boot ROM이 TF-A BL1에게, BL1이 BL2에게, BL2가 BL31과 BL33(U-Boot)에게, U-Boot가 커널에게, 커널이 PID 1에게 바통을 넘긴다. 문제는 **주자마다 자기 스톱워치를 들고 있다**는 것이다.

| 구간 | 그 구간이 쓰는 시계 | 0점 | 어떻게 보나 |
|---|---|---|---|
| Boot ROM, TF-A | 대개 없음 (TF-A는 `NOTICE:` 로그만) | — | scope + GPIO, 시리얼 줄 도착 시각 |
| U-Boot | `timer_get_us()` — arm64는 generic timer 카운터 | 카운터 0 = 리셋(대부분의 SoC) | `bootstage` (§2) |
| 커널 초기 | 없음 (`[    0.000000]`로 찍힘) | — | U-Boot `start_kernel` 마크와 printk 0점 사이의 틈 |
| 커널 | `local_clock()` = sched_clock | sched_clock 등록 순간 | `printk.time=1`, `initcall_debug` (§3) |
| 유저 공간 | `CLOCK_MONOTONIC`/`/proc/uptime`, systemd 타임스탬프 | 커널 timekeeping 시작 | `systemd-analyze`, 앱 로그 |
| 전 구간 | **arm64 `CNTVCT_EL0`** (리셋 후 계속 증가) | 리셋 | 유저 공간에서 직접 읽기 (C7 §4.2) |
| 전 구간 (외부) | 호스트 PC의 시리얼 수신 시각 | 캡처 시작 | `grabserial`, `ts`, 이 노트의 `m10_ts.py` |

진짜 보드에서 가장 믿을 만한 것은 **외부 시계**다. 리셋 핀과 "READY GPIO"를 로직 분석기로 같이 찍으면 어떤 소프트웨어에도 의존하지 않는다(M11). 그다음이 리셋 이후 멈추지 않는 하드웨어 카운터(`CNTVCT_EL0`)다. printk 시각은 "커널이 시계를 켠 뒤"부터라 그 앞이 통째로 빠진다는 점이 함정이다.

### 1.2 시리얼 줄마다 호스트 시각 찍기 — `m10_ts.py`

무엇을 확인하나: QEMU(실제 보드라면 USB-UART)의 출력 한 줄마다 "캡처 시작 후 경과 시간"을 붙이고, 필요하면 특정 프롬프트에 키 입력을 보내는 작은 캡처기. C7의 `c7_bt.py`는 마커 통계를 내는 하네스였고, 이것은 그보다 단순한 `ts`/`grabserial` 대용품이다. pty를 쓰는 이유는 U-Boot 프롬프트에 키를 보내기 위해서다.

```python
#!/usr/bin/env python3
"""m10_ts.py: run a command, prefix each serial line with host time since start (like grabserial/ts).
usage: m10_ts.py LOG [--send AFTER_REGEX TEXT]... -- cmd args...
"""
import os, pty, re, select, subprocess, sys, time

def main():
    a = sys.argv[1:]
    log = a.pop(0)
    sends = []
    while a and a[0] == "--send":
        sends.append((re.compile(a[1]), a[2].encode().decode("unicode_escape"))); a = a[3:]
    cmd = a[a.index("--") + 1:]
    m, s = pty.openpty()                      # pty so QEMU stdin works for --send
    t0 = time.monotonic()
    p = subprocess.Popen(cmd, stdin=s, stdout=s, stderr=s, close_fds=True)
    os.close(s)
    buf = b""
    with open(log, "w") as f:
        while True:
            r, _, _ = select.select([m], [], [], 0.5)
            if not r:
                if p.poll() is not None: break
                continue
            try: d = os.read(m, 4096)
            except OSError: break
            if not d: break
            buf += d
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                t = time.monotonic() - t0
                txt = line.decode(errors="replace").replace("\r", "")
                f.write(f"{t:9.3f} {txt}\n"); f.flush()
                if sends and sends[0][0].search(txt):
                    os.write(m, sends.pop(0)[1].encode())
            # prompts ("=> ", "Hit any key") have no newline: match the partial line, don't log it yet
            if sends and buf and sends[0][0].search(buf.decode(errors="replace").replace("\r", "")):
                os.write(m, sends.pop(0)[1].encode())
    p.wait()

if __name__ == "__main__":
    main()
```

첫 버전은 `--send`가 있을 때 미완성 줄까지 바로 로그에 써서, 한 줄이 `N` / `OTICE:` 처럼 조각나 기록됐다. 프롬프트처럼 개행이 없는 문자열은 **매칭만 하고 기록은 개행이 올 때** 하도록 고친 것이 위 코드다. 실제 보드의 `grabserial`도 같은 문제를 다룬다.

### 1.3 전 체인 부팅을 준비한다

체인은 `TF-A BL1 → BL2 → BL31 → BL33(U-Boot) → Linux → BusyBox initramfs`다. 랩의 `flash.bin`은 오프셋 0에 BL1, `0x40000`에 FIP가 있는 64 MiB pflash 이미지다(직접 확인했다). FIP 안의 BL33만 §2에서 빌드할 bootstage U-Boot로 갈아 끼운다.

```sh
cd /var/tmp/M10
cmp -n 22832 /var/lab/out/flash.bin /var/lab/out/bl1.bin && echo bl1@0      # BL1 = 앞 22832바이트
F=/var/lab/build/tfa/qemu/release/tools/fiptool/fiptool
cp /var/lab/out/fip.bin m10_fip.bin
$F update --nt-fw build/u-boot.bin m10_fip.bin          # BL33 교체
$F info m10_fip.bin
cp /var/lab/out/flash.bin m10_flash.bin
dd if=m10_fip.bin of=m10_flash.bin bs=1 seek=$((0x40000)) conv=notrunc status=none
```

```text
bl1@0
Trusted Boot Firmware BL2: offset=0xB0, size=0x61F0, cmdline="--tb-fw"
EL3 Runtime Firmware BL31: offset=0x62A0, size=0xC067, cmdline="--soc-fw"
Non-Trusted Firmware BL33: offset=0x12307, size=0x1706D0, cmdline="--nt-fw"
```

부팅 디스크는 MBR 파티션 1개짜리 ext4에 `/boot/Image`, `/boot/initrd.gz`, `/boot/extlinux/extlinux.conf`를 넣었다. 처음엔 파티션 없이 통째로 ext4를 만들었더니 U-Boot의 bootflow 스캔이 `virtio-blk#31.bootdev`를 지나치고 네트워크(TFTP)까지 갔다가 실패했다 — 표준 부팅(bootstd)은 **파티션 안의** extlinux를 찾는다.

```text
default m10
timeout 0
label m10
  kernel /boot/Image
  initrd /boot/initrd.gz
  append console=ttyAMA0 printk.time=1 LAB_CMD=/m10_bs.sh
```

`/m10_bs.sh`는 initramfs에 넣은 스크립트로, READY 시점에 `CNTVCT_EL0`(리셋 후 경과)와 `/proc/uptime`을 찍는 정적 바이너리 `m10_since_reset`을 부르고, U-Boot가 DT에 넣어 준 bootstage 기록(§2.4)을 읽는다.

```c
/* m10_since_reset: print time since reset (CNTVCT_EL0) and kernel uptime — one clock for the whole chain */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static inline uint64_t rd(void) { uint64_t v; __asm__ volatile("isb; mrs %0, cntvct_el0" : "=r"(v)); return v; }
static inline uint64_t frq(void) { uint64_t v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); return v; }

int main(void)
{
    double up = 0, t = (double)rd() / frq();
    FILE *f = fopen("/proc/uptime", "r");
    if (!f || fscanf(f, "%lf", &up) != 1) { perror("/proc/uptime"); exit(1); }
    fclose(f);
    printf("since_reset=%.3f uptime=%.3f pre_kernel_clock=%.3f\n", t, up, t - up);
    return 0;
}
```

VM에서 `gcc -static -O2 -Wall -Wextra`로 경고 없이 빌드했다(C7 §4.2의 `c7_cntvct`와 같은 원리를 한 줄 출력으로 줄인 것).

### 1.4 실행 — 리셋에서 READY까지

```sh
Q="qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -m 1057 -nographic -bios m10_flash.bin \
   -drive file=m10_disk.img,format=raw,if=none,id=d0 -device virtio-blk-device,drive=d0"
for i in 1 2 3; do timeout 150 python3 m10_ts.py m10_chain_r$i.log -- $Q; done
```

(`-m 1057`은 README의 검증값이다: TF-A가 BL33을 DRAM+512 MiB에 올리므로 512 MiB로는 BL2가 data abort를 낸다.) 1회차 로그의 핵심 줄(왼쪽 숫자 = 호스트 시각, 초):

```text
    0.158 NOTICE:  Booting Trusted Firmware
    0.159 NOTICE:  BL1: Booting BL2
    0.164 NOTICE:  BL2: v2.15.0(release):
    0.215 NOTICE:  BL1: Booting BL31
    0.219 NOTICE:  BL31: v2.15.0(release):
    0.245 U-Boot 2026.10 (Oct 17 2026 - 23:14:06 -0700)
    0.250 DRAM:  1 GiB
    0.600 Net:   eth0: virtio-net#32
    2.628 Hit any key to stop autoboot: 0
    2.629 Scanning for bootflows in all bootdevs
    2.807 ** Booting bootflow 'virtio-blk#31.bootdev.part_1' with extlinux
    2.810 Retrieving file: /boot/Image
    2.864 Retrieving file: /boot/initrd.gz
    2.906 Starting kernel ...
    3.776 [    0.000000] Booting Linux on physical CPU 0x0000000000 [0x411fd070]
    5.418 [    2.149356] Run /init as init process
    5.688 lab initramfs: 6.18.55 up 2.31s
    5.952 M10_READY since_reset=5.782 uptime=2.580 pre_kernel_clock=3.202
```

(카운트다운의 터미널 제어 문자는 지웠고 중간 줄은 생략했다.) 세 번 돌린 결과:

| 회차 | `Starting kernel` (호스트) | `Run /init` (printk) | READY `since_reset` | `pre_kernel_clock` |
|---|---|---|---|---|
| 1 | 2.906 | 2.149 | 5.782 | 3.202 |
| 2 | 3.505 | 2.182 | 6.557 | 3.777 |
| 3 | 3.000 | 2.941 | 6.466 | 3.386 |

출력에서 볼 것:

- **`Booting Linux`가 호스트 시각 3.776에 왔지만 printk는 `0.000000`이다.** 이 줄은 커널이 콘솔 드라이버를 등록한 뒤 버퍼에서 한꺼번에 내보낸 것이다. 커널 앞부분 줄의 "도착 시각"은 거짓말이다. 커널 구간은 printk 시각으로, 커널 이전은 bootstage와 `CNTVCT`로 잰다.
- 호스트 시각과 `CNTVCT`의 차이는 거의 일정하다: `start_kernel` 마크(§2, 카운터 2.754 s) ↔ 호스트 2.906 s, READY 5.782 ↔ 5.952. 즉 **CNTVCT ≈ 호스트 시각 − 0.16 s**(QEMU 시작 비용). 이걸로 두 시계를 하나로 맞춘다.
- printk 0점(= sched_clock 등록)은 호스트 기준 5.418 − 2.149 = 3.269 s → 리셋 기준 약 3.11 s다. U-Boot가 커널로 점프한 2.754 s와의 틈 **0.36 s가 "커널 초기, 시계 없음" 구간**이다(head.S, 페이지 테이블, `setup_arch`, 메모리 초기화 — C6). 부하가 심했던 다른 회차에서는 이 틈이 2.6 s까지 늘었다. 시계가 없는 구간이 부하에 가장 민감하다는 것도 교훈이다.
- 회차 사이 READY가 5.78~6.56 s로 0.8 s 흔들렸다. 공유 VM의 TCG 잡음이다.

### 1.5 타임라인 그림 (1회차 실측)

1회차 값을 리셋 기준으로 정리하면: TF-A 0~0.09 s, U-Boot 초기화 0.09~0.46 s(bootstage `main_loop` 455,901 us), `bootdelay` 카운트다운 0.46~2.48 s, bootflow 스캔·로드 2.48~2.75 s(`start_kernel` 2,754,563 us), 커널 초기(시계 없음) 2.75~3.11 s, 커널 initcall 구간 3.11~5.26 s(printk 2.149 s), initramfs → READY 5.26~5.78 s.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="40" y="20" font-size="13">리셋 → M10_READY 5.78 s (TF-A → U-Boot → Linux → initramfs, QEMU TCG 실측)</text>
<rect x="40.0" y="40" width="8.7" height="34" fill="#888" fill-opacity="0.35" stroke="#888"/>
<rect x="48.7" y="40" width="36.9" height="34" fill="#888" fill-opacity="0.35" stroke="#888"/>
<rect x="85.6" y="40" width="202.1" height="34" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<rect x="287.7" y="40" width="27.7" height="34" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<rect x="315.4" y="40" width="35.6" height="34" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="351.0" y="40" width="215.0" height="34" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<rect x="566.0" y="40" width="52.2" height="34" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="44.0" y1="74" x2="44.0" y2="88" stroke="currentColor" stroke-width="0.6"/>
<text x="44.0" y="100" font-size="12" text-anchor="start">TF-A 0.09</text>
<line x1="67.0" y1="74" x2="67.0" y2="110" stroke="currentColor" stroke-width="0.6"/>
<text x="67.0" y="122" font-size="12" text-anchor="start">U-Boot init 0.37</text>
<line x1="187.0" y1="74" x2="187.0" y2="88" stroke="currentColor" stroke-width="0.6"/>
<text x="187.0" y="100" font-size="12" text-anchor="middle">bootdelay 2.02</text>
<line x1="301.0" y1="74" x2="301.0" y2="110" stroke="currentColor" stroke-width="0.6"/>
<text x="301.0" y="122" font-size="12" text-anchor="middle">scan+load 0.28</text>
<line x1="333.0" y1="74" x2="333.0" y2="132" stroke="currentColor" stroke-width="0.6"/>
<text x="333.0" y="144" font-size="12" text-anchor="middle">kernel early 0.36</text>
<line x1="458.0" y1="74" x2="458.0" y2="88" stroke="currentColor" stroke-width="0.6"/>
<text x="458.0" y="100" font-size="12" text-anchor="middle">initcall 구간 2.15</text>
<line x1="592.0" y1="74" x2="592.0" y2="110" stroke="currentColor" stroke-width="0.6"/>
<text x="592.0" y="122" font-size="12" text-anchor="end">userspace 0.52</text>
<line x1="40.0" y1="160" x2="40.0" y2="165" stroke="currentColor"/><text x="40.0" y="180" font-size="12" text-anchor="middle">0</text>
<line x1="140.0" y1="160" x2="140.0" y2="165" stroke="currentColor"/><text x="140.0" y="180" font-size="12" text-anchor="middle">1</text>
<line x1="240.0" y1="160" x2="240.0" y2="165" stroke="currentColor"/><text x="240.0" y="180" font-size="12" text-anchor="middle">2</text>
<line x1="340.0" y1="160" x2="340.0" y2="165" stroke="currentColor"/><text x="340.0" y="180" font-size="12" text-anchor="middle">3</text>
<line x1="440.0" y1="160" x2="440.0" y2="165" stroke="currentColor"/><text x="440.0" y="180" font-size="12" text-anchor="middle">4</text>
<line x1="540.0" y1="160" x2="540.0" y2="165" stroke="currentColor"/><text x="540.0" y="180" font-size="12" text-anchor="middle">5</text>
<line x1="640.0" y1="160" x2="640.0" y2="165" stroke="currentColor"/><text x="640.0" y="180" font-size="12" text-anchor="middle">6</text>
<line x1="40" y1="160" x2="640" y2="160" stroke="currentColor"/><text x="652" y="180" font-size="12">s</text>
<line x1="40.0" y1="205" x2="640.0" y2="205" stroke="#888" stroke-width="3"/>
<text x="44.0" y="199" font-size="12">호스트 시각 (m10_ts.py)</text>
<line x1="40.0" y1="235" x2="315.4" y2="235" stroke="#e08a3c" stroke-width="3"/>
<text x="44.0" y="229" font-size="12">U-Boot bootstage (CNTPCT)</text>
<line x1="351.0" y1="265" x2="566.0" y2="265" stroke="#4a7bd0" stroke-width="3"/>
<text x="355.0" y="259" font-size="12">printk 시각 (0 = sched_clock 등록)</text>
<line x1="40.0" y1="295" x2="618.2" y2="295" stroke="#3f9a6b" stroke-width="3"/>
<text x="44.0" y="289" font-size="12">CNTVCT_EL0 (m10_since_reset)</text>
<text x="40" y="322" font-size="12">아래 네 줄 = 각 구간을 재는 시계. 시계마다 0점이 다르다 → 한 시계(CNTVCT)로 맞춘다</text>
</svg>
```
그림 2 — 실측 전 구간 타임라인(1회차). 가장 긴 막대는 아무 일도 하지 않는 `bootdelay` 2 s(빨강)이고, 그다음이 커널 initcall 구간이다. 아래 네 줄은 각 구간을 볼 수 있는 시계의 범위다.

이 그림 하나가 부팅 최적화 회의의 출발점이다. "어디부터 줄일까?"의 답이 막대 길이로 보인다: ① `bootdelay`(C7에서 −2.08 s 실측), ② 커널 initcall(§3), ③ U-Boot 초기화 안의 네트워크 초기화(§2.3에서 발견), ④ 유저 공간.

### 1.6 함정

- **"부팅 완료"의 정의를 먼저 합의한다**(C7 §4.1). 로그인 프롬프트인지, 카메라 첫 프레임인지, CAN 응답인지에 따라 critical path가 다르다.
- 커널 printk 시각만으로 "부팅 2 s"라고 보고하면 펌웨어 구간(여기서 2.75 s)이 통째로 빠진다.
- 시리얼 출력 자체가 시간을 먹는다. 115200 baud에서 1 KB ≈ 89 ms다(C7 §5.2의 `quiet` 계산). 측정하려고 로그를 늘리면 측정 대상이 바뀐다 — 그래서 마지막 확인은 GPIO + scope로 한다(M11).

---

## 2. U-Boot `bootstage` — 부트로더 안의 스톱워치

### 2.1 무엇인가

`bootstage`는 U-Boot에 내장된 **타임스탬프 기록기**다. 정해진 지점(`board_init_f`, `board_init_r`, `main_loop`, `bootm_start`, `start_kernel` …)에서 `bootstage_mark()`가 현재 `timer_get_us()` 값을 표에 적는다. 펌웨어 시절 Don이 "단계마다 타임스탬프를 SRAM 링버퍼에 적고 나중에 덤프"하던 것과 똑같다.

| Kconfig | 하는 일 |
|---|---|
| `CONFIG_BOOTSTAGE` | 기록 기능 자체 (`bootstage_mark`, `bootstage_start/accum`) |
| `CONFIG_BOOTSTAGE_REPORT` | 커널로 점프하기 직전에 표를 콘솔에 출력 |
| `CONFIG_CMD_BOOTSTAGE` | 프롬프트에서 `bootstage report` / `stash` / `unstash` 명령 |
| `CONFIG_BOOTSTAGE_FDT` | 기록을 커널에 넘기는 DT의 `/bootstage` 노드에 복사 |
| `CONFIG_BOOTSTAGE_STASH` | 기록을 정해진 메모리 주소에 저장 (SPL → U-Boot 본체, 또는 커널 드라이버가 읽게) |
| `CONFIG_BOOTSTAGE_RECORD_COUNT` | 기록 칸 수 |
| `CONFIG_SPL_BOOTSTAGE` | SPL에서도 기록 (SPL이 있는 보드에서 DDR 초기화 시간 등) |

U-Boot 초기화 순서 표(`common/board_f.c`)에 `initf_bootstage`가 있는 것은 C4에서 이미 봤다 — 자기 타이머를 쓰므로 driver model보다 먼저 시작할 수 있다.

### 2.2 직접 빌드 — 랩 U-Boot 설정 + bootstage

랩 U-Boot는 `CONFIG_BOOTSTAGE`가 꺼져 있다(`# CONFIG_BOOTSTAGE is not set`). 랩의 `.config`를 내 출력 디렉터리로 복사하고 옵션만 켜서 `O=`로 빌드한다(소스 트리 `/var/lab/src/u-boot`는 건드리지 않는다).

```sh
mkdir -p /var/tmp/M10/build && cd /var/tmp/M10
cp /var/lab/build/u-boot/.config build/.config
S=/var/lab/src/u-boot
$S/scripts/config --file build/.config -e BOOTSTAGE -e BOOTSTAGE_REPORT -e CMD_BOOTSTAGE \
    -e BOOTSTAGE_FDT --set-val BOOTSTAGE_RECORD_COUNT 50
make -s -C $S O=/var/tmp/M10/build olddefconfig && grep BOOTSTAGE build/.config
time make -s -C $S O=/var/tmp/M10/build -j2
```

```text
CONFIG_BOOTSTAGE=y
CONFIG_BOOTSTAGE_REPORT=y
CONFIG_BOOTSTAGE_RECORD_COUNT=50
CONFIG_BOOTSTAGE_FDT=y
# CONFIG_BOOTSTAGE_STASH is not set
CONFIG_CMD_BOOTSTAGE=y

real	3m11.992s
user	2m1.539s
sys	0m46.677s
```

`BOOTSTAGE_STASH`는 처음에 켰다가 껐다. 기본 주소가 `0x0`인데, QEMU virt에서 0번지는 pflash(부트 ROM 자리)라 쓰면 안 된다. 실제 보드에서는 커널이 덮어쓰지 않는 SRAM이나 reserved-memory 영역을 정해서 쓴다(F10).

### 2.3 `bootstage report` — 프롬프트에서, 그리고 점프 직전에

카운트다운에 키를 보내 프롬프트를 잡고, `bootstage report`를 친 뒤 `boot`로 계속 부팅시켰다.

```sh
python3 m10_ts.py m10_chain_i.log --send "Hit any key" " " --send "=> \$" "bootstage report\r" \
    --send "dm_r" "boot\r" -- $Q
```

```text
    1.346 => bootstage report
    1.346 Timer summary in microseconds (9 records):
    1.347        Mark    Elapsed  Stage
    1.348           0          0  reset
    1.348     141,096    141,096  board_init_f
    1.348     263,176    122,080  board_init_r
    1.348     774,709    511,533  eth_common_init
    1.348     795,031     20,322  eth_initialize
    1.348     805,922     10,891  main_loop
    1.348     822,871     16,949  cli_loop
    1.348 
    1.348 Accumulated time:
    1.348                 23,514  dm_f
    1.348                 38,962  dm_r
    1.349 => boot
```

`CONFIG_BOOTSTAGE_REPORT` 덕분에 키를 안 누른 1회차에서도 `Starting kernel ...` 직후 같은 표가 자동으로 나온다:

```text
    2.906 Starting kernel ...
    2.912 Timer summary in microseconds (12 records):
    2.912        Mark    Elapsed  Stage
    2.914           0          0  reset
    2.914      72,758     72,758  board_init_f
    2.915     134,154     61,396  board_init_r
    2.915     440,924    306,770  eth_common_init
    2.916     447,460      6,536  eth_initialize
    2.916     455,901      8,441  main_loop
    2.917   2,733,148  2,277,247  bootm_start
    2.917   2,737,248      4,100  boot_ramdisk_high
    2.917   2,753,731     16,483  boot_jump_linux
    2.918   2,754,563        832  start_kernel
    2.918 
    2.918 Accumulated time:
    2.918                  9,205  dm_f
    2.919                 31,988  dm_r
```

읽는 법:

- `Mark` = 리셋(카운터 0) 기준 절대 시각(us), `Elapsed` = 바로 앞 기록과의 차이. `reset`은 U-Boot가 0으로 넣는 기준점이라, U-Boot보다 먼저 돈 TF-A 시간(약 0.09 s)이 `board_init_f`의 72 ms 안에 섞여 있다. arm64 U-Boot의 `timer_get_us()`는 generic timer 카운터를 읽고, 이 카운터는 리셋에서 0부터 돈다(QEMU와 대부분의 SoC). SoC에 따라 카운터를 BL2가 켜는 경우도 있으니 보드마다 확인한다.
- `bootm_start`의 Elapsed 2.28 s는 **`main_loop` → `bootm_start` 사이 전부** — 즉 `bootdelay` 2 s + bootflow 스캔이다. bootstage는 "마크가 없는 구간"을 앞 마크에 몰아준다. 더 쪼개고 싶으면 마크를 추가한다(§2.5).
- **발견: `eth_common_init`가 306 ms(대화형 회차 512 ms, 부하가 심한 회차 826 ms).** 네트워크 부팅을 안 쓰는 장비에서 U-Boot의 네트워크 초기화는 순수한 낭비다. `CONFIG_NET`을 끄거나(또는 `CONFIG_NO_NET`), 최소한 부팅 경로에서 네트워크 장치를 probe하지 않게 한다. 이건 시리얼 로그(`Net:   eth0: virtio-net#32`가 0.6 s에 찍힘)만으로는 "몇 ms 걸렸는지" 보이지 않는다 — bootstage가 필요한 이유다.
- `Accumulated time`의 `dm_f`/`dm_r`은 `bootstage_start/accum`으로 누적한 시간: driver model 초기화가 pre-relocation에서 9 ms, post-relocation에서 32 ms.

### 2.4 커널로 넘어간 bootstage — `/proc/device-tree/bootstage`

`CONFIG_BOOTSTAGE_FDT`를 켜면 U-Boot는 커널에 넘기는 DT에 `/bootstage` 노드를 만들고 기록마다 `name`, `mark`(또는 `accum`) 속성을 넣는다. 커널은 이 노드를 해석하지 않지만 `/proc/device-tree`로 그대로 보인다. 즉 **유저 공간 READY 스크립트가 부트로더 시간까지 한 번에 보고할 수 있다.**

```sh
#!/bin/sh
# m10_bs.sh: dump the U-Boot bootstage records that U-Boot copied into the kernel's DT
echo "M10_READY $(/m10_since_reset)"
for n in /proc/device-tree/bootstage/*; do
  h=$(od -An -tx1 "$n/mark" 2>/dev/null | tr -d ' \n')
  [ -n "$h" ] && printf "bootstage-dt %-18s mark=%d us\n" "$(cat "$n/name")" "0x$h"
done
```

DT 속성은 big-endian 32비트이므로 바이트를 16진수로 이어 붙여 `printf %d 0x...`로 바꾼다. 대화형 회차의 출력:

```text
M10_READY since_reset=9.564 uptime=3.760 pre_kernel_clock=5.804
bootstage-dt start_kernel       mark=2120191 us
bootstage-dt boot_jump_linux    mark=2120052 us
bootstage-dt board_init_f       mark=162304 us
bootstage-dt reset              mark=0 us
bootstage-dt boot_ramdisk_high  mark=2096305 us
bootstage-dt bootm_start        mark=2092955 us
bootstage-dt cli_loop           mark=1351160 us
bootstage-dt main_loop          mark=1339889 us
bootstage-dt eth_initialize     mark=1316918 us
bootstage-dt eth_common_init    mark=1276966 us
board_init_r       mark=450581 us
```

(마지막 줄은 셸 출력이 커널 메시지와 섞이며 앞부분이 잘린 것을 그대로 두었다. 노드 순서는 DT 노드 이름(0, 1, 10, 11, 2 …)의 사전순이라 시간순이 아니다.) 이 회차는 VM 부하가 심해서 U-Boot 마크가 1회차보다 전부 크다. 그래도 `start_kernel` 2.12 s와 `since_reset` 9.56 s를 같은 카운터로 비교할 수 있다는 점이 핵심이다 — 실제 제품에서는 이 값을 텔레메트리로 올려 **필드 장비들의 부팅 시간 분포**를 모은다.

### 2.5 자기 마크 추가하기 (가이드)

보드 코드에서 느린 구간을 의심하면 마크를 넣는다(실행 안 함, API는 `include/bootstage.h` 기준).

```c
#include <bootstage.h>

int board_late_init(void)
{
	bootstage_mark_name(BOOTSTAGE_ID_USER + 1, "pmic_setup");   /* 이 지점 시각 기록 */
	pmic_configure_rails();
	bootstage_mark_name(BOOTSTAGE_ID_USER + 2, "pmic_done");

	bootstage_start(BOOTSTAGE_ID_ACCUM_LCD, "lcd");             /* 누적 타이머 시작 */
	splash_display();
	bootstage_accum(BOOTSTAGE_ID_ACCUM_LCD);                    /* 누적 */
	return 0;
}
```

SPL이 있는 보드(i.MX8, AM62)라면 `CONFIG_SPL_BOOTSTAGE` + `BOOTSTAGE_STASH`로 SPL의 기록(DDR 학습 시간이 보통 가장 크다)을 U-Boot 본체가 `bootstage unstash`로 이어받는다.

---

## 3. 커널 구간 — `printk.time`, `initcall_debug`, bootgraph

### 3.1 initcall이 뭐였나 (복습)

커널의 `kernel_init()` → `do_initcalls()`는 레벨(`early`, `core`, `postcore`, `arch`, `subsys`, `fs`, `device`, `late`) 순서로 `__initcall` 섹션의 함수 포인터를 하나씩 부른다(C6, F 모듈). 빌트인 드라이버의 `module_init()`이 device 레벨 initcall이 되고, 그 안의 `platform_driver_register()`가 매칭되는 장치마다 **probe를 그 자리에서(동기로)** 부른다. 그래서 느린 probe 하나가 부팅 전체를 멈춘다(§5).

`initcall_debug`를 커널 커맨드라인에 주면 `do_one_initcall()`이 각 initcall 앞뒤로 `calling ...`과 `initcall ... returned N after M usecs`를 찍는다.

### 3.2 실행 — Top 20

```sh
timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1G -nographic \
  -kernel /var/lab/out/Image -initrd /var/lab/out/initramfs.cpio.gz \
  -append "console=ttyAMA0 quiet initcall_debug printk.time=1 log_buf_len=4M LAB_CMD=dmesg" \
  2>&1 | tr -d '\r' > m10_icd.log
grep -oE "initcall [^ ]+ returned -?[0-9]+ after [0-9]+ usecs" m10_icd.log | sort -k6 -n -r | head -20
grep -oE "initcall [^ ]+ returned -?[0-9]+ after [0-9]+ usecs" m10_icd.log | awk '{s+=$6; n++} END {print n, "initcalls, sum usecs", s}'
grep -E "Run /init|Freeing unused" m10_icd.log
```

```text
initcall physmap_init+0x0/0x28 returned 0 after 367007 usecs
initcall jent_mod_init+0x0/0x100 returned 0 after 251648 usecs
initcall chr_dev_init+0x0/0xa8 returned 0 after 221866 usecs
initcall of_platform_default_populate_init+0x0/0x114 returned 0 after 172000 usecs
initcall loop_init+0x0/0x108 returned 0 after 77388 usecs
initcall inet_init+0x0/0x2ac returned 0 after 68121 usecs
initcall gen_pci_driver_init+0x0/0x28 returned 0 after 59351 usecs
initcall acpi_thermal_init+0x0/0x94 returned -19 after 59317 usecs
initcall param_sysfs_builtin_init+0x0/0x1c0 returned 0 after 54585 usecs
initcall virtio_net_driver_init+0x0/0xb8 returned 0 after 54034 usecs
initcall pty_init+0x0/0x324 returned 0 after 36526 usecs
initcall sock_init+0x0/0x108 returned 0 after 36000 usecs
initcall slab_late_init+0x0/0x138 returned 0 after 32972 usecs
initcall pl031_driver_init+0x0/0x28 returned 0 after 31165 usecs
initcall serial8250_init+0x0/0x110 returned 0 after 29802 usecs
initcall net_dev_init+0x0/0x354 returned 0 after 24000 usecs
initcall dma_atomic_pool_init+0x0/0x148 returned 0 after 24000 usecs
initcall init_nfs_fs+0x0/0x1bc returned 0 after 20498 usecs
initcall virtio_pci_driver_init+0x0/0x30 returned 0 after 19875 usecs
initcall armv8_pmu_driver_init+0x0/0x44 returned 0 after 19102 usecs
1887 initcalls, sum usecs 3020161
[    4.658731] Freeing unused kernel memory: 3264K
[    4.662905] Run /init as init process
```

```svg
<svg viewBox="0 0 680 480" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13">initcall_debug Top 15 — 1887개 합계 3.02 s / Run /init 4.66 s</text>
<text x="252" y="48" font-size="12" text-anchor="end">physmap_init</text>
<rect x="260" y="34" width="312.0" height="18" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="578.0" y="48" font-size="12">367.0 ms</text>
<text x="252" y="74" font-size="12" text-anchor="end">jent_mod_init</text>
<rect x="260" y="60" width="213.9" height="18" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="479.9" y="74" font-size="12">251.6 ms</text>
<text x="252" y="100" font-size="12" text-anchor="end">chr_dev_init</text>
<rect x="260" y="86" width="188.6" height="18" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="454.6" y="100" font-size="12">221.9 ms</text>
<text x="252" y="126" font-size="12" text-anchor="end">of_platform_default_populate_init</text>
<rect x="260" y="112" width="146.2" height="18" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="412.2" y="126" font-size="12">172.0 ms</text>
<text x="252" y="152" font-size="12" text-anchor="end">loop_init</text>
<rect x="260" y="138" width="65.8" height="18" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="331.8" y="152" font-size="12">77.4 ms</text>
<text x="252" y="178" font-size="12" text-anchor="end">inet_init</text>
<rect x="260" y="164" width="57.9" height="18" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="323.9" y="178" font-size="12">68.1 ms</text>
<text x="252" y="204" font-size="12" text-anchor="end">gen_pci_driver_init</text>
<rect x="260" y="190" width="50.4" height="18" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="316.4" y="204" font-size="12">59.4 ms</text>
<text x="252" y="230" font-size="12" text-anchor="end">acpi_thermal_init</text>
<rect x="260" y="216" width="50.4" height="18" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="316.4" y="230" font-size="12">59.3 ms</text>
<text x="252" y="256" font-size="12" text-anchor="end">param_sysfs_builtin_init</text>
<rect x="260" y="242" width="46.4" height="18" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="312.4" y="256" font-size="12">54.6 ms</text>
<text x="252" y="282" font-size="12" text-anchor="end">virtio_net_driver_init</text>
<rect x="260" y="268" width="45.9" height="18" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="311.9" y="282" font-size="12">54.0 ms</text>
<text x="252" y="308" font-size="12" text-anchor="end">pty_init</text>
<rect x="260" y="294" width="31.0" height="18" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="297.0" y="308" font-size="12">36.5 ms</text>
<text x="252" y="334" font-size="12" text-anchor="end">sock_init</text>
<rect x="260" y="320" width="30.6" height="18" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="296.6" y="334" font-size="12">36.0 ms</text>
<text x="252" y="360" font-size="12" text-anchor="end">slab_late_init</text>
<rect x="260" y="346" width="28.0" height="18" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="294.0" y="360" font-size="12">33.0 ms</text>
<text x="252" y="386" font-size="12" text-anchor="end">pl031_driver_init</text>
<rect x="260" y="372" width="26.5" height="18" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="292.5" y="386" font-size="12">31.2 ms</text>
<text x="252" y="412" font-size="12" text-anchor="end">serial8250_init</text>
<rect x="260" y="398" width="25.3" height="18" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="291.3" y="412" font-size="12">29.8 ms</text>
<line x1="260" y1="428" x2="600" y2="428" stroke="currentColor"/>
<line x1="260.0" y1="428" x2="260.0" y2="433" stroke="currentColor"/><text x="260.0" y="446" font-size="12" text-anchor="middle">0</text>
<line x1="345.0" y1="428" x2="345.0" y2="433" stroke="currentColor"/><text x="345.0" y="446" font-size="12" text-anchor="middle">100</text>
<line x1="430.0" y1="428" x2="430.0" y2="433" stroke="currentColor"/><text x="430.0" y="446" font-size="12" text-anchor="middle">200</text>
<line x1="515.0" y1="428" x2="515.0" y2="433" stroke="currentColor"/><text x="515.0" y="446" font-size="12" text-anchor="middle">300</text>
<line x1="600.0" y1="428" x2="600.0" y2="433" stroke="currentColor"/><text x="600.0" y="446" font-size="12" text-anchor="middle">400</text>
<text x="610" y="446" font-size="12">ms</text>
<text x="10" y="468" font-size="12">주황 = 이 장비(센서 게이트웨이)에 필요 없는 기능 → Kconfig에서 빼거나 모듈로</text>
</svg>
```
그림 3 — 실측 initcall Top 15(`Image`, `-smp 2`, VM 부하 중). 1887개의 합 3.02 s가 커널 구간(약 4.4 s)의 대부분이다.

출력에서 볼 것:

- **`log_buf_len=4M`이 필요했다.** 랩 커널은 `CONFIG_LOG_BUF_SHIFT=17`(128 KB)이다. initcall 1887개 × 2줄 × 약 80바이트 ≈ 300 KB라 기본 버퍼면 앞부분이 덮어써진다. (C7 §5.2가 같은 커널에서 840개를 셌던 것은 아마 이 때문이다 — 숫자가 이상하면 버퍼부터 의심한다.)
- `acpi_thermal_init returned -19`(-ENODEV): ACPI가 없는 보드에서 59 ms를 쓰고 "장치 없음"으로 끝났다. `returned -19`인 initcall은 **이 보드에 필요 없다는 신호**다.
- 순위는 측정 조건에 따라 바뀐다: C7의 icount 측정에서는 `param_sysfs_builtin_init`가 1위였고, 여기(실시간, 부하 중)는 MMIO가 많은 `physmap_init`(CFI NOR 탐색)과 `jent_mod_init`(jitter entropy — CPU 타이밍 지터를 모으느라 일부러 시간을 쓴다)가 위다. 실시간 TCG에서 MMIO 에뮬레이션은 호스트 시간을 쓰므로 부풀려진다. 실제 보드에서 다시 잰다.
- `chr_dev_init` 222 ms는 그 안에서 `/dev/mem`, `/dev/random` 등 문자 장치를 만들며 `random` 초기화를 기다린 것으로 보인다(추정 — 확인하려면 §4의 function_graph로 들여다본다).

### 3.3 `scripts/bootgraph.pl` — initcall을 시간축에

커널 소스의 `scripts/bootgraph.pl`은 `initcall_debug` + `printk.time`이 있는 dmesg를 읽어 SVG 간트 차트를 만든다.

```sh
perl /var/lab/src/linux-6.18.55/scripts/bootgraph.pl < m10_icd.log > m10_bootgraph.svg
ls -la m10_bootgraph.svg; grep -c "<rect" m10_bootgraph.svg
```

```text
-rw-rw-r-- 1 donh donh 42792 Oct 17 23:33 m10_bootgraph.svg
164
```

`bootgraph.pl`의 출력은 폭 2000픽셀, 3pt 글자, RGB 채우기라 이 노트의 다크 모드 규칙에 맞지 않는다. 그래서 같은 로그에서 `calling`(시작 시각)과 `returned ... after`(소요)를 짝지어, 20 ms 이상인 18개만 직접 그렸다. bootgraph가 하는 일이 정확히 이것이다.

```svg
<svg viewBox="0 0 680 414" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13">bootgraph 방식 — 20 ms 이상 initcall을 커널 시간축에 (printk 시각, 실측)</text>
<text x="214" y="42" font-size="12" text-anchor="end">sock_init</text>
<rect x="315.0" y="32" width="3.2" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="60" font-size="12" text-anchor="end">dma_atomic_pool_init</text>
<rect x="323.8" y="50" width="2.2" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="78" font-size="12" text-anchor="end">of_platform_default_popula</text>
<rect x="337.7" y="68" width="15.4" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="96" font-size="12" text-anchor="end">net_dev_init</text>
<rect x="386.4" y="86" width="2.1" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="114" font-size="12" text-anchor="end">chr_dev_init</text>
<rect x="398.9" y="104" width="19.9" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="132" font-size="12" text-anchor="end">inet_init</text>
<rect x="419.5" y="122" width="6.1" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="150" font-size="12" text-anchor="end">init_nfs_fs</text>
<rect x="432.8" y="140" width="1.8" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="168" font-size="12" text-anchor="end">jent_mod_init</text>
<rect x="437.7" y="158" width="22.5" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="186" font-size="12" text-anchor="end">gen_pci_driver_init</text>
<rect x="478.9" y="176" width="5.3" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="204" font-size="12" text-anchor="end">acpi_thermal_init</text>
<rect x="485.7" y="194" width="5.3" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="222" font-size="12" text-anchor="end">pty_init</text>
<rect x="518.5" y="212" width="3.3" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="240" font-size="12" text-anchor="end">serial8250_init</text>
<rect x="521.8" y="230" width="2.7" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="258" font-size="12" text-anchor="end">loop_init</text>
<rect x="532.2" y="248" width="6.9" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="276" font-size="12" text-anchor="end">physmap_init</text>
<rect x="543.3" y="266" width="32.9" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="294" font-size="12" text-anchor="end">virtio_net_driver_init</text>
<rect x="582.1" y="284" width="4.8" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="312" font-size="12" text-anchor="end">pl031_driver_init</text>
<rect x="599.1" y="302" width="2.8" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="330" font-size="12" text-anchor="end">param_sysfs_builtin_init</text>
<rect x="613.8" y="320" width="4.9" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="214" y="348" font-size="12" text-anchor="end">slab_late_init</text>
<rect x="619.7" y="338" width="3.0" height="13" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<line x1="220" y1="360" x2="650" y2="360" stroke="currentColor"/>
<line x1="220.0" y1="360" x2="220.0" y2="365" stroke="currentColor"/><text x="220.0" y="378" font-size="12" text-anchor="middle">0</text>
<line x1="309.6" y1="360" x2="309.6" y2="365" stroke="currentColor"/><text x="309.6" y="378" font-size="12" text-anchor="middle">1</text>
<line x1="399.2" y1="360" x2="399.2" y2="365" stroke="currentColor"/><text x="399.2" y="378" font-size="12" text-anchor="middle">2</text>
<line x1="488.8" y1="360" x2="488.8" y2="365" stroke="currentColor"/><text x="488.8" y="378" font-size="12" text-anchor="middle">3</text>
<line x1="578.3" y1="360" x2="578.3" y2="365" stroke="currentColor"/><text x="578.3" y="378" font-size="12" text-anchor="middle">4</text>
<line x1="637.7" y1="26" x2="637.7" y2="360" stroke="#d0564a" stroke-dasharray="4 3"/><text x="633.7" y="394" font-size="12" text-anchor="end">Run /init 4.66 s</text>
<text x="656" y="378" font-size="12">s</text>
</svg>
```
그림 4 — bootgraph 방식 간트 차트(실측). 모든 막대가 겹치지 않고 한 줄로 이어진다 — 이 커널의 initcall은 전부 PID 1(`@ 1`) 한 스레드에서 **직렬로** 돈다는 뜻이다.

`grep -oE "calling .* @ [0-9]+" m10_icd.log | awk '{print $NF}' | sort | uniq -c`의 결과는 `1888 1` — 1888개 전부가 PID 1이었다. 막대 사이의 빈 곳은 20 ms 미만 initcall 수백 개의 "긴 꼬리"다. 멀티코어 SoC에서 CPU 하나만 일하고 나머지는 노는 구간 — **async probe(§5)가 노리는 빈자리**가 이것이다.

### 3.4 initcall_debug를 읽고 나서 하는 일

| 발견 | 조치 | 비고 |
|---|---|---|
| 이 보드에 없는 HW의 드라이버 (`returned -19`, physmap, ACPI) | Kconfig에서 끈다 | 가장 큰 레버. `initcall_blacklist=`는 실험용 (C7 §5.2) |
| 필요하지만 부팅 직후엔 안 쓰는 기능 (NFS, loop, 사운드) | 모듈(`=m`)로 바꾸고 READY 뒤에 `modprobe` | 아래 표 |
| 필요하고 느린 probe (PHY, 카메라 센서, 펌웨어 로드) | async probe, 또는 일을 workqueue로 미룸 | §5 |
| 엔트로피·RNG 대기 (`jent`, `crng init`) | HW RNG 드라이버 빌트인, `random.trust_cpu`/`trust_bootloader` 검토 | 보안팀과 합의 |
| 디버그 옵션 자체 (kprobes, lockdep, KASAN) | 양산 커널에서 끔 | §4.2에서 실측 |

**모듈 vs 빌트인** — 부팅 시간 관점의 트레이드오프:

| 관점 | 빌트인 (`=y`) | 모듈 (`=m`) |
|---|---|---|
| READY 전 비용 | initcall에서 바로 probe (직렬) | 0 — 로드를 미룰 수 있다 |
| 로드 비용 | 없음 | `.ko` 읽기 + 재배치 + 심볼 해석 + (서명 검증), udev/modprobe 경로 |
| rootfs 의존 | 없음 — rootfs 장치 드라이버는 빌트인이어야 initramfs 없이 부팅 | rootfs 드라이버면 initramfs 필요 |
| 커널 이미지 크기 | 커짐 → 로드·해제 시간 | 작아짐 |
| 제어 | `initcall_blacklist`(디버그용)뿐 | 언제, 어떤 순서로 올릴지 마음대로 |

원칙: **READY의 critical path에 있는 드라이버는 빌트인, 나머지는 모듈로 READY 뒤에.** 센서 키트라면 rootfs(eMMC), 콘솔, IMU(SPI), CAN은 빌트인이고, Wi-Fi, USB 가젯, 사운드는 모듈이다.

---

## 4. 부팅 시점 ftrace — 유저 공간이 뜨기 전에 트레이스를 켠다

### 4.1 왜 커맨드라인으로 켜나

ftrace(M5)는 보통 `/sys/kernel/tracing`에 써서 켠다. 그런데 부팅 중 initcall을 보려면 **tracefs를 마운트할 유저 공간이 아직 없다.** 그래서 커널은 커맨드라인으로 트레이서를 켜는 옵션을 준다(`Documentation/admin-guide/kernel-parameters.txt`, `kernel/trace/trace.c`).

| 커맨드라인 | 뜻 |
|---|---|
| `trace_event=initcall:*,sched:sched_wakeup` | 부팅 초기부터 이벤트 켜기 (콤마로 여러 개) |
| `ftrace=function_graph` (또는 `function`) | 부팅 초기부터 트레이서 켜기 |
| `ftrace_filter=`, `ftrace_notrace=` | function 트레이서 필터 |
| `ftrace_graph_filter=pl031_probe` | function_graph를 이 함수 아래로만 |
| `ftrace_graph_max_depth=2` | 그래프 깊이 제한 |
| `trace_buf_size=8M` | CPU당 링버퍼 크기 |
| `trace_options=nofuncgraph-irqs` | 옵션 (인터럽트 안의 함수 숨기기 등) |
| `tp_printk` | 이벤트를 printk로도 내보냄 (트레이스를 못 읽고 죽는 경우) |
| `ftrace_dump_on_oops` | oops 때 트레이스 버퍼를 콘솔로 덤프 |
| `traceoff_on_warning` | WARN 순간 트레이스를 멈춤 (직전 기록 보존) |

트레이스는 부팅 내내 링버퍼에 쌓이고, 부팅이 끝난 뒤 `mount -t tracefs none /sys/kernel/tracing; cat .../trace`로 읽는다. 커맨드라인이 길어지면 **bootconfig**(`CONFIG_BOOT_CONFIG` + `CONFIG_BOOTTIME_TRACING`, initrd 끝에 붙이는 트리 구조 설정 — `ftrace.instance.foo.events = ...` 식으로 인스턴스·히스토그램·kprobe까지 부팅 때 설정)를 쓴다. 랩의 `Image-trace`는 `# CONFIG_BOOTTIME_TRACING is not set`이라 bootconfig 경로는 실행하지 못했다(가이드).

부팅 후 트레이스를 읽는 스크립트(initramfs에 넣었다):

```sh
#!/bin/sh
# m10_ft.sh: read the trace recorded during boot (trace_event= / ftrace= on the cmdline)
mount -t tracefs none /sys/kernel/tracing; T=/sys/kernel/tracing
echo "tracer=$(cat $T/current_tracer) events=$(cat $T/set_event | tr '\n' ' ')"
echo "entries: $(grep -v '^#' $T/trace | wc -l)"
if grep -q initcall_finish $T/trace; then
  grep -m2 initcall_ $T/trace
  # pair start/finish by func name -> duration (us), top 8
  awk '/initcall_start:/ {split($NF,a,"="); s[a[2]]=$4+0}
       /initcall_finish:/ {split($(NF-1),a,"="); if (a[2] in s) printf "%8.0f us %s\n", ($4-s[a[2]])*1e6, a[2]}' $T/trace |
    sort -rn | head -8
else
  grep -v '^#' $T/trace | grep -v unwind_find_stack | head -30
fi
```

### 4.2 `trace_event=initcall:*` — initcall_debug의 트레이스 버전

```sh
timeout 400 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1G -nographic \
  -kernel /var/lab/out/Image-trace -initrd m10_irfs.gz \
  -append "console=ttyAMA0 quiet trace_event=initcall:initcall_start,initcall:initcall_finish trace_buf_size=8M LAB_CMD=/m10_ft.sh"
```

```text
lab initramfs: 6.18.55-trace up 5.61s
tracer=nop events=initcall:initcall_finish initcall:initcall_start 
entries: 4036
          <idle>-0       [000] ...1.     0.018000: initcall_start: func=con_init+0x0/0x220
          <idle>-0       [000] ...1.     0.021292: initcall_finish: func=con_init+0x0/0x220 ret=0
 1436502 us init_kprobe_trace+0x0/0x1b8
  345369 us of_platform_default_populate_init+0x0/0x118
  192766 us nf_conntrack_standalone_init+0x0/0xc0
  174347 us physmap_init+0x0/0x38
  121362 us param_sysfs_builtin_init+0x0/0x1d0
  104212 us jent_mod_init+0x0/0x110
   77070 us chr_dev_init+0x0/0xb8
   76983 us slab_late_init+0x0/0x148
```

출력에서 볼 것:

- printk를 안 쓰므로 `quiet`이어도 기록되고, 콘솔 출력 비용이 없다. 링버퍼는 나노초 단위 타임스탬프 + CPU 번호 + 컨텍스트(`...1.` = preempt depth 등)를 같이 남긴다. 첫 이벤트는 `con_init`(console initcall, 0.018 s)으로, `initcall_debug`보다 이른 console 레벨부터 잡힌다.
- **디버그 커널 자체의 부팅 비용이 보인다**: `init_kprobe_trace` 1.44 s가 1위다. kprobe 이벤트 초기화가 커널 심볼 전체를 훑는(블랙리스트 구성 등) 비용으로 보인다. `nf_conntrack_standalone_init` 193 ms도 `Image`에는 없던 항목이다. 트레이싱·보안 기능을 다 켠 커널(`Image-trace`)은 initcall이 약 2000개(이벤트 4036개 ÷ 2)로 늘었다. 양산 커널과 디버그 커널의 부팅 시간을 섞어서 비교하면 안 된다.

### 4.3 `ftrace=function_graph` — 특정 probe 안을 들여다보기

Top 목록에 `pl031_driver_init`(RTC) 31 ms가 있었다. 그 probe 안에서 무엇이 오래 걸리나?

```sh
... -append "console=ttyAMA0 quiet ftrace=function_graph ftrace_graph_filter=pl031_probe \
             ftrace_graph_max_depth=2 trace_options=nofuncgraph-irqs LAB_CMD=/m10_ft.sh"
```

```text
lab initramfs: 6.18.55-trace up 29.84s
tracer=function_graph events=
entries: 16
 1)               |  pl031_probe() {
 1) ! 306.464 us  |    amba_request_regions();
 1) + 49.328 us   |    devm_kmalloc();
 1) + 39.168 us   |    devm_kmemdup();
 1)   7.136 us    |    __devres_alloc_node();
 1) ! 103.088 us  |    __ioremap_prot();
 1) + 31.664 us   |    devres_add();
 1) # 1041.840 us |    device_set_wakeup_capable();
 1) # 3982.064 us |    device_wakeup_enable();
 1) + 38.704 us   |    __devm_add_action();
 1) ! 425.872 us  |    devm_rtc_allocate_device();
 1)               |    __devm_rtc_register_device() {
 0) * 38855.90 us |    } /* __devm_rtc_register_device */
 0) ! 685.920 us  |    request_threaded_irq();
 0) ! 185.264 us  |    devm_pm_set_wake_irq();
 0) * 48720.84 us |  } /* pl031_probe */
```

출력에서 볼 것:

- `pl031_probe` 전체 48.7 ms 중 **`__devm_rtc_register_device`가 38.9 ms**다. 그 안에서 CPU 번호가 `1)` → `0)`으로 바뀌었다 — 함수 안에서 잠들었다가 다른 CPU에서 깨어났다는 뜻이다(RTC 등록 시 시간 읽기·sysfs·`/dev/rtc0` 생성 과정의 대기). 다음 단계는 `ftrace_graph_filter=__devm_rtc_register_device`로 한 단계 더 내려가는 것이다.
- 기호: `+` > 10 us, `!` > 100 us, `#` > 1 ms, `*` > 10 ms.
- **대가가 크다**: 같은 커널이 4.2절(이벤트만)에서는 5.6 s, function_graph를 켜면 29.8 s에 `/init`에 도달했다. 필터가 하나여도 동적 ftrace가 모든 함수 입구에 그래프 훅을 걸기 때문이다. 부팅 시점 function_graph는 "어디가 느린지 이미 좁힌 뒤"에 한 함수만 보는 도구다. 그 전 단계는 이벤트(`initcall:*`)나 `initcall_debug`로 한다.
- 처음 실행에서는 `unwind_find_stack.constprop.0()` 수십 줄이 끼어 있었다(필터 함수 바깥인데 기록됨). `nofuncgraph-irqs`를 주고 출력에서 그 이름을 거르니 깨끗해졌다. 이 커널 설정에서 왜 그 함수가 잡히는지는 확인하지 못했다.

---

## 5. 느린 probe — 동기, 비동기, deferred

### 5.1 직관

빌트인 드라이버의 probe는 initcall 안에서 동기로 돈다. 카메라 센서 리셋 대기 200 ms, Ethernet PHY 오토네고 2 s, Wi-Fi 펌웨어 로드 500 ms … 하나가 잠들면 **그동안 다른 모든 initcall이 줄을 선다**(그림 4의 한 줄 직렬 구조). 펌웨어로 치면 메인 루프 초기화 함수 안에서 `delay_ms(500)`을 부르는 것과 같다.

해결은 두 가지다.

- **비동기 probe**: 드라이버 코어가 probe를 async 작업(`async_schedule_dev()` → kworker)으로 던지고 바로 다음으로 간다. 멀티코어면 겹쳐서 돈다. 드라이버가 `.driver.probe_type = PROBE_PREFER_ASYNCHRONOUS`로 요청하거나, 커맨드라인 `driver_async_probe=drv1,drv2`(`*`이면 전부)로, 모듈이면 `insmod foo.ko async_probe=1`(또는 `module.async_probe`)로 켠다.
- **일을 probe 밖으로**: probe에서는 등록만 하고 느린 초기화는 workqueue로 미루거나 첫 `open()` 때 한다(F7).

**Deferred probe**는 다른 문제다. probe에 필요한 공급자(클럭, 레귤레이터, GPIO, PHY)가 아직 없으면 드라이버가 `-EPROBE_DEFER`를 돌려주고, 드라이버 코어가 그 장치를 "보류 목록"에 넣었다가 **다른 장치가 성공적으로 bind될 때마다** 다시 시도한다(`driver_deferred_probe_trigger()`). 순서를 몰라도 결국 붙게 해 주는 장치이지만, 공급자가 끝내 안 오면 영원히 대기한다(`/sys/kernel/debug/devices_deferred`로 확인, `deferred_probe_timeout=`으로 상한).

### 5.2 실험 모듈 — `m10_slow.ko`

무엇을 확인하나: probe마다 `msleep(500)`하는 platform 장치 4개. `async=1`이면 `PROBE_PREFER_ASYNCHRONOUS`, `defer=1`이면 장치 0이 "공급자가 아직 없다"며 처음 한 번 `-EPROBE_DEFER`를 돌려준다.

```c
// m10_slow: N platform devices whose probe sleeps -> measure sync vs async probe cost
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/ktime.h>

static int ndev = 4, delay_ms = 500, async, defer;
module_param(ndev, int, 0444);
module_param(delay_ms, int, 0444);
module_param(async, int, 0444);   /* 1 = PROBE_PREFER_ASYNCHRONOUS */
module_param(defer, int, 0444);   /* 1 = device 0 defers until another device bound */
static struct platform_device *pdevs[16];
static atomic_t bound = ATOMIC_INIT(0);
static ktime_t t0;

static int m10_probe(struct platform_device *pdev)
{
	if (defer && pdev->id == 0 && atomic_read(&bound) == 0) {
		dev_info(&pdev->dev, "supplier not ready -> -EPROBE_DEFER\n");
		return -EPROBE_DEFER;
	}
	msleep(delay_ms);                 /* stand-in for PHY link / firmware load / sensor reset */
	atomic_inc(&bound);
	dev_info(&pdev->dev, "probed on cpu%d (%s) t=+%lld ms\n", raw_smp_processor_id(),
		 current->comm, ktime_ms_delta(ktime_get(), t0));
	return 0;
}

static struct platform_driver m10_drv = {
	.probe = m10_probe,
	.driver = { .name = "m10_slow" },
};

static int __init m10_init(void)
{
	int i, ret;

	t0 = ktime_get();
	if (ndev > 16) ndev = 16;
	m10_drv.driver.probe_type = async ? PROBE_PREFER_ASYNCHRONOUS : PROBE_DEFAULT_STRATEGY;
	ret = platform_driver_register(&m10_drv);
	if (ret) return ret;
	for (i = 0; i < ndev; i++)
		pdevs[i] = platform_device_register_simple("m10_slow", i, NULL, 0);
	pr_info("m10_slow: init returns after %lld ms (async=%d)\n", ktime_ms_delta(ktime_get(), t0), async);
	return 0;
}

static void __exit m10_exit(void)
{
	int i;

	for (i = 0; i < ndev; i++)
		if (!IS_ERR_OR_NULL(pdevs[i])) platform_device_unregister(pdevs[i]);
	platform_driver_unregister(&m10_drv);
}
module_init(m10_init);
module_exit(m10_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("M10 slow probe demo");
```

`make -C /var/lab/out/linux-build-trace M=$PWD modules`로 경고 없이 빌드했고(vermagic `6.18.55-trace SMP preempt mod_unload aarch64`), initramfs의 `/m10_probe.sh`가 `insmod` 전후의 `/proc/uptime`과 4개 장치가 모두 `/sys/bus/platform/drivers/m10_slow/`에 붙는 시각을 잰다. 빌트인으로 만들려면 커널을 다시 빌드해야 해서(공유 VM에서 -j2로 수십 분) 모듈로 실험했다. 모듈의 `module_init` 안에서 장치를 등록하면 빌트인 initcall 안에서 일어나는 것과 같은 경로(`device_add` → `bus_probe_device` → `__device_attach`)를 탄다.

### 5.3 실행 — 네 가지 변형 (Image-trace, `-smp 4`)

```sh
for v in "sync:" "async:async=1" "cmdline:" "defer:defer=1"; do
  n=${v%%:*}; a=${v#*:}; extra=""; [ $n = cmdline ] && extra="driver_async_probe=m10_slow"
  echo "##### $n"
  timeout 200 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 4 -m 1G -nographic \
    -kernel /var/lab/out/Image-trace -initrd m10_irfs.gz \
    -append "console=ttyAMA0 quiet $extra LAB_CMD=/m10_probe.sh~$a" 2>&1 | tr -d "\r" | grep -E "M10|m10_slow"
done
```

```text
##### sync
M10 insmod  took 3.68 s
M10 all 4 bound after 5.74 s
[   52.743666][   T92] m10_slow m10_slow.0: probed on cpu3 (insmod) t=+579 ms
[   53.409287][   T92] m10_slow m10_slow.1: probed on cpu3 (insmod) t=+1245 ms
[   54.013469][   T92] m10_slow m10_slow.2: probed on cpu3 (insmod) t=+1849 ms
[   54.807530][   T92] m10_slow m10_slow.3: probed on cpu0 (insmod) t=+2643 ms
[   54.808704][   T92] m10_slow: init returns after 2644 ms (async=0)
##### async
M10 insmod async=1 took 3.05 s
M10 all 4 bound after 5.47 s
[   51.940972][   T91] m10_slow: init returns after 669 ms (async=1)
[   52.087587][   T35] m10_slow m10_slow.0: probed on cpu2 (kworker/u16:1) t=+816 ms
[   52.251331][   T59] m10_slow m10_slow.1: probed on cpu1 (kworker/u16:2) t=+979 ms
[   52.359404][   T61] m10_slow m10_slow.2: probed on cpu0 (kworker/u16:3) t=+1087 ms
[   52.584707][   T92] m10_slow m10_slow.3: probed on cpu1 (kworker/u16:4) t=+1313 ms
##### cmdline
M10 insmod  took 3.87 s
M10 all 4 bound after 5.61 s
[   56.085728][   T91] m10_slow: init returns after 807 ms (async=0)
[   56.211601][   T54] m10_slow m10_slow.0: probed on cpu3 (kworker/u16:2) t=+933 ms
[   56.447109][   T61] m10_slow m10_slow.1: probed on cpu3 (kworker/u16:3) t=+1168 ms
[   56.674237][   T41] m10_slow m10_slow.2: probed on cpu0 (kworker/u16:1) t=+1395 ms
[   56.775452][   T12] m10_slow m10_slow.3: probed on cpu2 (kworker/u16:0) t=+1497 ms
##### defer
M10 insmod defer=1 took 3.48 s
M10 all 4 bound after 5.29 s
[   61.379804][   T92] m10_slow m10_slow.0: supplier not ready -> -EPROBE_DEFER
[   61.933135][   T92] m10_slow m10_slow.1: probed on cpu2 (insmod) t=+727 ms
[   62.607142][   T36] m10_slow m10_slow.0: probed on cpu0 (kworker/u16:1) t=+1401 ms
[   62.636266][   T92] m10_slow m10_slow.2: probed on cpu1 (insmod) t=+1430 ms
[   63.479806][   T92] m10_slow m10_slow.3: probed on cpu3 (insmod) t=+2273 ms
[   63.480555][   T92] m10_slow: init returns after 2274 ms (async=0)
```

(`loading out-of-tree module taints kernel`, `module verification failed` 줄은 생략했다 — 서명 안 한 외부 모듈이라 나오는 정상 경고다. 이 회차 VM 부하가 심해 커널이 50 s 넘게 걸려 `/init`에 도달했다.)

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<text x="10" y="18" font-size="13">m10_slow: probe 4개 × msleep(500) — module init 기준 경과 ms (Image-trace, -smp 4, 실측)</text>
<text x="10" y="44" font-size="12">동기 (기본)</text>
<rect x="120.0" y="32" width="108.6" height="20" fill="#e08a3c" fill-opacity="0.4" stroke="#e08a3c"/><text x="174.8" y="46" font-size="12" text-anchor="middle">dev0</text>
<rect x="229.6" y="32" width="125.1" height="20" fill="#e08a3c" fill-opacity="0.4" stroke="#e08a3c"/><text x="292.6" y="46" font-size="12" text-anchor="middle">dev1</text>
<rect x="355.7" y="32" width="113.3" height="20" fill="#e08a3c" fill-opacity="0.4" stroke="#e08a3c"/><text x="412.8" y="46" font-size="12" text-anchor="middle">dev2</text>
<rect x="470.0" y="32" width="149.3" height="20" fill="#e08a3c" fill-opacity="0.4" stroke="#e08a3c"/><text x="545.1" y="46" font-size="12" text-anchor="middle">dev3</text>
<text x="616.3" y="68" font-size="12" text-anchor="end">init 반환 2644 ms (insmod·initcall이 여기까지 막힘)</text>
<text x="10" y="104" font-size="12">async=1</text>
<line x1="246.6" y1="84" x2="246.6" y2="196" stroke="#d0564a" stroke-dasharray="4 3"/><text x="250.6" y="94" font-size="12">init 반환 669 ms</text>
<rect x="179.8" y="104" width="94.6" height="18" fill="#3f9a6b" fill-opacity="0.4" stroke="#3f9a6b"/><text x="280.5" y="117" font-size="12">dev0 → 816 ms (kworker)</text>
<rect x="210.7" y="126" width="94.6" height="18" fill="#3f9a6b" fill-opacity="0.4" stroke="#3f9a6b"/><text x="311.3" y="139" font-size="12">dev1 → 979 ms (kworker)</text>
<rect x="231.1" y="148" width="94.6" height="18" fill="#3f9a6b" fill-opacity="0.4" stroke="#3f9a6b"/><text x="331.8" y="161" font-size="12">dev2 → 1087 ms (kworker)</text>
<rect x="273.9" y="170" width="94.6" height="18" fill="#3f9a6b" fill-opacity="0.4" stroke="#3f9a6b"/><text x="374.5" y="183" font-size="12">dev3 → 1313 ms (kworker)</text>
<line x1="120" y1="200" x2="650" y2="200" stroke="currentColor"/>
<line x1="120.0" y1="200" x2="120.0" y2="205" stroke="currentColor"/><text x="120.0" y="218" font-size="12" text-anchor="middle">0</text>
<line x1="214.6" y1="200" x2="214.6" y2="205" stroke="currentColor"/><text x="214.6" y="218" font-size="12" text-anchor="middle">500</text>
<line x1="309.3" y1="200" x2="309.3" y2="205" stroke="currentColor"/><text x="309.3" y="218" font-size="12" text-anchor="middle">1000</text>
<line x1="403.9" y1="200" x2="403.9" y2="205" stroke="currentColor"/><text x="403.9" y="218" font-size="12" text-anchor="middle">1500</text>
<line x1="498.6" y1="200" x2="498.6" y2="205" stroke="currentColor"/><text x="498.6" y="218" font-size="12" text-anchor="middle">2000</text>
<line x1="593.2" y1="200" x2="593.2" y2="205" stroke="currentColor"/><text x="593.2" y="218" font-size="12" text-anchor="middle">2500</text>
<text x="640" y="236" font-size="12" text-anchor="end">ms</text>
<text x="10" y="262" font-size="12">초록 막대의 시작점은 "끝 − 500 ms"로 그린 추정이다 (로그에는 끝 시각만 있다).</text>
<text x="10" y="284" font-size="12">동기: 4개가 줄 서서 2.6 s. 비동기: 4개가 서로 다른 CPU에서 겹쳐 1.3 s, 호출자는 0.67 s에 풀려난다.</text>
</svg>
```
그림 5 — 실측 sync vs async probe. 동기는 probe 4개가 insmod 스레드(T92) 하나에서 줄을 서고, 비동기는 서로 다른 kworker가 서로 다른 CPU에서 겹쳐 돈다.

출력에서 볼 것:

- **동기**: 모든 probe가 `(insmod)` 즉 호출한 스레드에서 돈다. `init returns after 2644 ms` — 이것이 빌트인이었다면 initcall 하나가 2.6 s를 잡아먹은 것이다. `msleep(500)`인데 장치당 600~800 ms인 것은 부하 중인 TCG에서 타이머가 늦게 깨운 탓이다.
- **async=1**: 모듈 init은 669 ms에 반환하고, probe는 `kworker/u16:N`들이 cpu0~2에서 나눠 돈다. 4개가 모두 끝난 시각 1313 ms ≈ 동기의 절반. 장치 등록 자체(`platform_device_register_simple` 4번 + sysfs)가 부하 중에 669 ms나 걸린 것이 남은 비용이다.
- **`driver_async_probe=m10_slow`**: 드라이버 코드를 안 바꾸고 커맨드라인만으로 같은 효과(`init returns after 807 ms`, 끝 1497 ms). 벤더 드라이버를 수정할 수 없을 때 쓴다.
- **defer**: 장치 0이 `-EPROBE_DEFER` → 장치 1이 bind되자 드라이버 코어가 보류 목록을 다시 돌려(`kworker/u16:1`, deferred probe 작업) 장치 0이 1401 ms에 붙었다. 순서 의존을 드라이버 코어가 풀어 준 것이다.
- **insmod 시간(3~3.9 s)은 네 경우 모두 비슷했다.** 셸의 `/proc/uptime` 측정에는 모듈 로딩 자체(파일 읽기, 재배치, 서명 검사 시도, kallsyms)와 부하 잡음이 섞였다. `insmod`가 `async_probe` 모듈에 대해 `async_synchronize_full()`을 건너뛰는 것은 맞지만(`kernel/module/main.c`의 `do_init_module()`), 이 측정에서는 그 차이가 잡음에 묻혔다. 정확한 비교는 커널 안의 `ktime` 값으로 한다.

### 5.4 async probe의 함정

- **순서에 기대던 코드가 깨진다**: `/dev/video0`, `eth0` 같은 이름 번호가 probe 순서로 정해지면 async에서 바뀔 수 있다. udev 규칙이나 DT `aliases`로 고정한다.
- **rootfs 장치를 async로 하면** 커널이 `rootwait` 없이 마운트를 시도해 실패할 수 있다. 커널은 `/init` 실행 전에 `async_synchronize_full()`과 `wait_for_device_probe()`로 기다리지만(C6), 그 대기 자체가 critical path가 된다.
- **효과는 "기다리는" probe에서만** 나온다. CPU를 계속 쓰는 probe를 async로 돌려도 단일 코어에서는 이득이 없다(C7 §5.2의 단일 코어 실험에서 잡음 수준이었던 이유).
- 드라이버 쪽 선언: 6.x 커널의 `enum probe_type`은 `PROBE_DEFAULT_STRATEGY`, `PROBE_PREFER_ASYNCHRONOUS`, `PROBE_FORCE_SYNCHRONOUS` 세 가지다. 마지막은 "이 드라이버는 절대 async 안 됨"이다.

---

## 6. 커널 크기·압축·콘솔·rootfs 읽기 속도

이 항목들은 C7에서 실측했다. 여기서는 결론과 판단 기준만 모은다(숫자는 C7 §5, icount).

| 항목 | C7 실측 | 판단 기준 |
|---|---|---|
| `Image`(42.7 MB) vs `Image.gz`(15.2 MB) | U-Boot의 gzip 해제 +4.6 s, 읽기는 86 ms | 저장장치 대역폭 대 CPU 해제 속도. 느린 QSPI NOR면 압축이 이길 수 있다 |
| initramfs 압축 7종 | lz4 1.76 s, gzip 3.68 s, xz 14 s, lzma 38 s (해제 구간) | 부팅 경로엔 lz4(또는 비압축), xz·lzma·bzip2는 두지 않는다 |
| `quiet` | QEMU에선 −40~60 ms (PL011이 무한히 빠름) | 동기 UART 콘솔이면 로그 17 KB @115200 = 1.47 s (계산) |
| `lpj=` | arm64에선 0 | 이미 타이머 주파수로 계산 |

추가로 기억할 것:

- **커널 압축 방식 Kconfig**: arm64의 `Image`는 커널 자체에 압축 해제기가 없다(arm32 `zImage`와 다르다). `Image.gz`/`Image.lz4`는 부트로더(U-Boot `booti` + `kernel_comp_addr_r`)가 푼다. 그래서 "커널을 LZ4로"라는 말은 arm64에서는 "부트로더가 LZ4를 지원하나"의 문제다. U-Boot는 `CONFIG_LZ4`가 있으면 lz4 FIT 이미지를 풀 수 있다.
- **콘솔 비용은 커널 버전마다 다르다**: 6.18의 PL011은 nbcon(비동기 프린터 스레드)라 printk가 부팅을 덜 막지만, 벤더 BSP의 4.x/5.x 커널은 동기 콘솔인 경우가 많다. 오래된 커널에서는 `quiet`이 초 단위 효과를 낸다.
- **rootfs 읽기 속도**: 커널 구간 뒤 유저 공간이 느리면 저장장치부터 잰다. `dd if=/dev/mmcblk0p2 of=/dev/null bs=1M count=100 iflag=direct`로 순차 읽기, `fio --rw=randread --bs=4k`로 랜덤 읽기(J 모듈). 부팅 때는 수천 개의 작은 파일(라이브러리, unit 파일)을 랜덤으로 읽으므로 **4K 랜덤 읽기 IOPS**가 순차 대역폭보다 중요하다. 읽기 전용 squashfs/erofs(압축 + 연속 배치)가 ext4보다 빠른 경우가 많다(J, K 모듈). 그리고 **rootfs가 작을수록** 페이지 캐시 미스가 줄어든다(K: rootfs 크기).

---

## 7. 유저 공간 구간 — systemd-analyze, bootchart

### 7.1 systemd-analyze가 보여 주는 것

`systemd-analyze blame / critical-chain / plot`과 서비스 정리는 L1에서 다뤘다. 여기서는 전체 타임라인과 이어지는 첫 줄만 본다. 정상 상태 실험(§9)용 systemd 게스트(`Image-sec`, Ubuntu 24.04 rootfs, 2 vCPU)의 실제 출력:

```text
Startup finished in 13.773s (kernel) + 2min 46.720s (userspace) = 3min 494ms 
graphical.target reached after 2min 45.145s in userspace.
```

읽는 법:

- 실제 보드에서는 이 줄이 `Startup finished in 1.2s (firmware) + 0.8s (loader) + 2.1s (kernel) + 4.5s (userspace)`처럼 나올 수 있다. **firmware·loader 칸은 부트로더가 EFI 변수 `LoaderTimeInitUSec`/`LoaderTimeExecUSec`(Boot Loader Interface)를 남겼을 때만** 나온다(systemd-boot 등). U-Boot의 `booti`나 QEMU `-kernel`로 부팅하면 칸이 없다 — 그래서 펌웨어 구간은 §2의 bootstage로 따로 잰다.
- 이 게스트의 userspace 2분 47초는 공유 VM 부하 + TCG + "장비에 필요 없는 서비스" 때문이다. 실험 첫 회차에서 `swupdate.service`가 반복 재시작하며 CPU를 59% 먹고 있었고(`top`으로 확인), `podman-*` 서비스 잡이 부팅을 5분 넘게 붙잡았다. 이 서비스들을 마스크(`ln -s /dev/null /etc/systemd/system/<unit>`)한 뒤의 숫자가 위 줄이다. **부팅을 줄이는 첫 단계는 "이 장비에 무엇이 떠 있나"의 목록화**라는 L1의 결론을 그대로 확인한 셈이다.
- 함정 하나: 측정 스크립트를 `Type=oneshot` + `After=multi-user.target` 서비스로 넣었더니, 스크립트 안의 `systemd-analyze`가 `Bootup is not yet finished`를 내고, `systemctl is-system-running --wait`는 영원히 기다렸다. **자기 자신이 부팅 트랜잭션의 일부**이기 때문이다. `Type=exec`로 바꿔 해결했다.

### 7.2 bootchart

`systemd-bootchart`(별도 패키지, `init=/usr/lib/systemd/systemd-bootchart`로 PID 1 앞에 끼워 `/proc`을 주기적으로 샘플링해 SVG를 만든다)와 그 원조 `bootchart2`는 **이 rootfs에 설치돼 있지 않아 실행하지 못했다**(공용 상태라 apt 설치도 하지 않았다). 개념만 정리한다.

| 도구 | 방식 | 보이는 것 | 대안 |
|---|---|---|---|
| systemd-bootchart | PID 1 전에 실행, `/proc/<pid>/stat`·`schedstat`을 수십 Hz로 샘플 | 프로세스별 CPU·I/O 대기 막대, 전체 CPU·디스크 사용률 | `systemd-analyze plot`(unit 단위, L1) |
| bootchart2 | 같은 원리, 사용자 공간 수집기 + 렌더러 | 같음 | |
| `perf timechart` | 커널 sched/power 이벤트 기록 | CPU별 실행·대기 구간 | M6 |

부팅 중 CPU가 놀고 있는지(I/O 대기) 바쁜지를 가르는 것이 bootchart의 가치다. bootchart가 없으면 §9의 `vmstat 1`을 부팅 초반 서비스로 돌려도 같은 질문에 답할 수 있다.

---

## 8. "fast boot" 기법 목록

측정으로 큰 덩어리를 찾은 뒤 꺼내는 도구 상자다. 앞 절에서 실측한 것은 표시했다.

| 단계 | 기법 | 효과의 크기 | 대가·주의 |
|---|---|---|---|
| 부트 ROM·SPL | 부팅 매체 순서 고정(strap), DDR 학습 결과 저장 | 수백 ms | SoC 의존 |
| TF-A | 불필요한 BL 단계·로그 제거 | 수십 ms (여기 0.09 s) | 보안 부팅과 함께 설계 (C5, L5) |
| U-Boot | `bootdelay=0` (C7 −2.08 s 실측) | 초 단위 | 디버그 진입 수단을 따로 (키, GPIO) |
| U-Boot | bootflow 스캔 대신 직접 `load`+`booti` (C7 −0.15 s) | 수백 ms | |
| U-Boot | 네트워크·USB·비디오 끄기 (§2.3: `eth_common_init` 306 ms) | 수백 ms | |
| U-Boot | **Falcon 모드**: SPL이 U-Boot 본체를 건너뛰고 커널 직접 (`CONFIG_SPL_OS_BOOT`, `spl export`) | U-Boot 전체 (0.5~2 s) | 환경·DT를 미리 준비, 복구 경로 |
| 커널 | 쓸모없는 드라이버 Kconfig에서 제거 (§3) | 수백 ms~초 | |
| 커널 | 필요 없는 기능 모듈화 + READY 뒤 로드 (§3.4) | | |
| 커널 | async probe, 느린 init을 workqueue로 (§5) | 기다리는 probe만큼 | 순서 의존 깨짐 |
| 커널 | `quiet` / `loglevel=` (§6) | 동기 콘솔이면 초 단위 | 현장 디버그는 `dmesg`로 |
| 커널 | 압축 방식·크기 (§6, C7) | 초 단위 | 매체 속도에 따라 방향이 바뀜 |
| 커널 | XIP(eXecute In Place) — NOR에서 커널을 복사 없이 실행 | 복사 시간 | 주로 MMU 없는/작은 시스템, arm64 메인라인 미지원 |
| 커널 | 디버그 옵션 끔 (§4.2: `init_kprobe_trace` 1.44 s) | 초 단위 | |
| rootfs | 읽기 전용 squashfs/erofs, 작게 (J, K) | | 쓰기 영역 분리 |
| 유저 공간 | 정적 장치 노드 / devtmpfs + 최소 mdev (udev 전체 대신) | 수백 ms | 핫플러그 처리 직접 |
| 유저 공간 | 서비스 정리·순서 재배치, READY에 필요 없는 건 뒤로 (L1) | 초 단위 | |
| 유저 공간 | 앱 정적 링크, prelink 효과, 라이브러리 수 줄이기 (B2·B3) | 수십~수백 ms | |
| 구조 | suspend-to-RAM을 "전원 버튼"으로 (C7 §6) | 부팅 자체 생략 | 대기 전력, resume 품질 (N 모듈) |
| 구조 | 스냅샷(hibernation 계열) 부팅 — 부팅 끝난 메모리 이미지를 복원 | 콜드 부팅 대비 큼 | 이미지 크기 × 매체 속도, 드라이버 상태 복원 |
| 측정 | CI의 HW 테스트에서 매 빌드 부팅 시간 기록 | 회귀 방지 | 없으면 반드시 다시 늘어난다 |

---

## 9. 정상 상태 분석 — "무엇을 기다리나"

### 9.1 직관 — 세 개의 줄

정상 동작 중의 느림은 거의 항상 **어떤 자원 앞에 줄이 생긴 것**이다. Linux에서 태스크가 일을 못 하는 이유는 셋이다.

- **CPU 줄**: 실행할 준비는 됐는데(runnable, 상태 `R`) CPU가 없다 — run queue에서 대기.
- **I/O 줄**: 블록 I/O가 끝나길 잠들어 기다린다 — 보통 uninterruptible sleep, 상태 `D`.
- **메모리 줄**: 메모리를 얻으려고 reclaim(페이지 회수)·swap-in·thrashing을 기다린다.

도구는 이 세 줄의 길이와 그 줄 때문에 잃은 시간을 다른 각도로 보여 준다. SSD 펌웨어로 치면 "명령 큐 깊이(r), NAND 대기 중인 명령 수(b), 버퍼 할당 대기"를 카운터로 보는 것이다.

실험 방법: systemd 게스트(`Image-sec`, 2 vCPU, 1 GiB RAM, swap 512 MiB)에 데이터 디스크 `vdb`를 붙이되 QEMU `throttling.bps-total=20971520`으로 **20 MB/s짜리 느린 저장장치**(eMMC·SD 카드 흉내)를 만들었다. 부팅 후 load average가 0.8 아래로 내려갈 때까지 기다린 다음, 부하를 하나씩 걸고 같은 신호 세트(loadavg, PSI, `vmstat`, `mpstat`/`iostat`/`pidstat`)를 찍는 스크립트를 systemd 서비스로 돌렸다. 스크립트의 측정 부분:

```sh
snap() {  # $1 = label: one-shot view of every signal
  echo "--- [$1] loadavg: $(cat /proc/loadavg)"
  for r in cpu io memory; do echo "PSI $r: $(tr '\n' ' ' < /proc/pressure/$r)"; done
}
snap idle; vmstat -w 1 3
echo "##### CPU hog"; stress-ng --cpu 2 --timeout 25 -q & P=$!; sleep 8
snap cpu; vmstat -w 1 4; mpstat -P ALL 1 1 | tail -4; pidstat -u 1 1 | tail -4
wait $P
echo "##### IO hog"; dd if=/dev/zero of=/dev/vdb bs=1M count=4000 oflag=direct status=none & P=$!; sleep 8
snap io; vmstat -w 1 4; iostat -x -d vdb 1 2 | tail -3; pidstat -d 1 1 | tail -3
kill $P; wait $P
```

(처음엔 `wait`를 인자 없이 썼다가 스크립트가 멈췄다. 출력을 `exec > >(tee ...)`로 돌렸기 때문에, bash의 인자 없는 `wait`가 **프로세스 치환(tee)까지** 기다린 것이다. 기다릴 PID를 명시해 고쳤다.)

### 9.2 `vmstat` — 한 줄에 세 줄이 다 있다

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="14">vmstat -w 1 — 칸별 의미 (첫 줄은 부팅 후 평균, 둘째 줄부터 1초 구간)</text>
  <rect x="20" y="40" width="80" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="60" y="58" font-size="12" text-anchor="middle">procs</text>
  <text x="60" y="74" font-size="12" text-anchor="middle">r   b</text>
  <rect x="104" y="40" width="180" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="194" y="58" font-size="12" text-anchor="middle">memory (KiB)</text>
  <text x="194" y="74" font-size="12" text-anchor="middle">swpd free buff cache</text>
  <rect x="288" y="40" width="66" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="321" y="58" font-size="12" text-anchor="middle">swap</text>
  <text x="321" y="74" font-size="12" text-anchor="middle">si  so</text>
  <rect x="358" y="40" width="66" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="391" y="58" font-size="12" text-anchor="middle">io</text>
  <text x="391" y="74" font-size="12" text-anchor="middle">bi  bo</text>
  <rect x="428" y="40" width="70" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="463" y="58" font-size="12" text-anchor="middle">system</text>
  <text x="463" y="74" font-size="12" text-anchor="middle">in  cs</text>
  <rect x="502" y="40" width="158" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="581" y="58" font-size="12" text-anchor="middle">cpu (%)</text>
  <text x="581" y="74" font-size="12" text-anchor="middle">us sy id wa st</text>
  <text x="20" y="110" font-size="12">r : 실행 중 + 실행 대기 태스크 수 → CPU 수보다 계속 크면 CPU 포화</text>
  <text x="20" y="132" font-size="12">b : I/O를 기다리며 잠든 태스크 수 (커널 nr_iowait) → 블록 I/O 줄</text>
  <text x="20" y="154" font-size="12">free / cache : 남은 메모리 / 페이지 캐시 — free가 작아도 cache가 크면 정상</text>
  <text x="20" y="176" font-size="12">si / so : 초당 swap-in / swap-out (KiB) → 0이 아니면 메모리 부족 신호</text>
  <text x="20" y="198" font-size="12">bi / bo : 초당 블록 장치 읽기 / 쓰기 (KiB)</text>
  <text x="20" y="220" font-size="12">in / cs : 초당 인터럽트 / context switch → cs 폭증은 lock 경쟁·잦은 wakeup</text>
  <text x="20" y="242" font-size="12">us / sy : 유저 / 커널 모드 CPU — sy가 크면 syscall·인터럽트·커널 경로를 의심</text>
  <text x="20" y="264" font-size="12">id / wa : 놀고 있음 / 놀면서 I/O를 기다리는 태스크가 있음 (wa도 사실상 idle)</text>
  <text x="20" y="286" font-size="12">st : steal — 하이퍼바이저가 다른 VM에 CPU를 준 시간 (VM 게스트에서만)</text>
  <text x="20" y="316" font-size="12">출처: /proc/stat (cpu, procs_running, procs_blocked, intr, ctxt),</text>
  <text x="20" y="338" font-size="12">/proc/vmstat (pswpin, pswpout, pgpgin, pgpgout), /proc/meminfo</text>
</svg>
```
그림 6 — `vmstat` 필드 안내. 왼쪽 두 칸(r, b)이 CPU 줄과 I/O 줄의 길이, si/so가 메모리 줄의 증상이다.

**기준선(idle)** 실측:

```text
--- [idle] loadavg: 0.78 1.10 0.50 1/82 384
PSI cpu: some avg10=0.57 avg60=8.82 avg300=13.18 total=66662924 full avg10=0.00 avg60=0.00 avg300=0.00 total=0 
PSI io: some avg10=0.00 avg60=0.07 avg300=0.13 total=1145423 full avg10=0.00 avg60=0.01 avg300=0.00 total=247477 
PSI memory: some avg10=0.00 avg60=0.00 avg300=0.00 total=0 full avg10=0.00 avg60=0.00 avg300=0.00 total=0 
--procs-- -----------------------memory---------------------- ---swap-- -----io---- -system-- ----------cpu----------
   r    b         swpd         free         buff        cache   si   so    bi    bo   in   cs  us  sy  id  wa  st  gu
   0    0            0       760504         7620       139780    0    0   558    27  262    1  12  29  59   0   0   0
   0    0            0       760504         7620       139780    0    0     0     0   17   12   0   1 100   0   0   0
   0    0            0       760504         7620       139780    0    0     0     0   33   26   0   2  98   0   0   0
```

첫 줄은 **부팅 이후 평균**이라 무시한다(`bi` 558은 부팅 때 읽은 것). 둘째 줄부터가 1초 구간이다. `cs`(context switch/s) 12~26, `in` 17~33: 2-vCPU 유휴 시스템의 정상 범위. `loadavg`의 네 번째 칸 `1/82` = 지금 runnable 1개 / 전체 태스크 82개, 다섯째 = 마지막 PID.

### 9.3 CPU hog — `r`가 CPU 수에 붙고, `us`가 꽉 찬다

```text
--- [cpu] loadavg: 0.90 1.11 0.51 3/85 396
   r    b         swpd         free         buff        cache   si   so    bi    bo   in   cs  us  sy  id  wa  st  gu
   3    0            0       742364         7628       152756    0    0   574    26  269    1  15  28  57   0   0   0
   2    0            0       742364         7628       152756    0    0     0     0  481   25  92   8   0   0   0   0
   2    0            0       742364         7628       152756    0    0     0     0  496   25  93   7   0   0   0   0
   2    0            0       742364         7628       152756    0    0     0     0  482   22  90  10   0   0   0   0
Average:     CPU    %usr   %nice    %sys %iowait    %irq   %soft  %steal  %guest  %gnice   %idle
Average:     all   91.58    0.00    0.00    0.00    8.42    0.00    0.00    0.00    0.00    0.00
Average:       0   92.55    0.00    0.00    0.00    7.45    0.00    0.00    0.00    0.00    0.00
Average:       1   90.62    0.00    0.00    0.00    9.38    0.00    0.00    0.00    0.00    0.00
Average:        0       394   73.38    0.00    0.00    7.14   73.38     -  stress-ng-cpu
Average:        0       395   72.08    0.00    0.00    9.09   72.08     -  stress-ng-cpu
Average:        0       406    3.90   10.39    0.00    8.44   14.29     -  pidstat
```

(`pidstat -u` 칸: UID, PID, `%usr`, `%system`, `%guest`, `%wait`, `%CPU`, CPU, Command. 헤더 줄은 `tail`로 잘렸다.)

출력에서 볼 것:

- `vmstat`: `r=2` = CPU 2개에 runnable 2개 → **포화 직전이지만 줄은 없다**. `us` 90~93, `id` 0.
- `mpstat -P ALL`: 두 CPU가 고르게 `%usr` 91%. 한 CPU만 100%면 단일 스레드 병목이나 IRQ 쏠림(한 CPU에 인터럽트가 몰림, `/proc/interrupts` 확인 — M2)을 의심한다. `%irq` 8%는 TCG 타이머 인터럽트 에뮬레이션 비용이다.
- `pidstat -u`: **`%wait` 칸**(7~9%)이 핵심이다. 그 태스크가 runnable인데 CPU를 못 받은 비율 — 즉 태스크 단위의 CPU 줄이다(`/proc/<pid>/schedstat`의 대기 시간 기반, 최근 sysstat).
- PSI cpu `avg10`이 아직 0.49인 것은 hog를 건 지 8초라 10초 평균이 덜 올라온 것과, runnable 2개 = CPU 2개라 **아무도 기다리지 않기 때문**이다. PSI cpu는 "CPU를 기다린 시간"이지 "CPU 사용률"이 아니다. 같은 부하에 태스크가 3개였다면 올랐을 것이다.
- load average 1분 값이 0.90 → 아직 낮다. load average는 지수 이동 평균이라 1분 값도 수십 초 뒤에야 따라온다.

### 9.4 I/O hog — `b`, `wa`, `%util`, 그리고 PSI io full

```text
--- [io] loadavg: 1.19 1.16 0.55 3/83 410
PSI io: some avg10=41.83 avg60=8.98 avg300=2.04 total=8324178 full avg10=41.77 avg60=8.90 avg300=1.91 total=7243715 
   r    b         swpd         free         buff        cache   si   so    bi    bo   in   cs  us  sy  id  wa  st  gu
   1    1            0       748656         7652       150912    0    0   515   735  276    1  19  27  53   2   0   0
   0    1            0       748656         7652       150912    0    0     0 21504  112   69   0  10  49  41   0   0
   0    1            0       748656         7652       150912    0    0     0 20480  118   69   1   7  49  44   0   0
   0    1            0       748656         7652       150912    0    0     0 19456   79   58   0   3  50  47   0   0
Average:      UID       PID   kB_rd/s   kB_wr/s kB_ccwr/s iodelay  Command
Average:        0       408      0.00  20078.43      0.00       0  dd
```

`iostat -x`의 `vdb` 줄(이 회차는 `tail`이 헤더를 잘라서, 같은 I/O hog를 다음 회차에서 헤더와 함께 다시 찍은 것):

```text
Device            r/s     rkB/s   rrqm/s  %rrqm r_await rareq-sz     w/s     wkB/s   wrqm/s  %wrqm w_await wareq-sz     d/s     dkB/s   drqm/s  %drqm d_await dareq-sz     f/s f_await  aqu-sz  %util
vdb              0.00      0.00     0.00   0.00    0.00     0.00   20.00  20480.00     0.00   0.00   48.20  1024.00    0.00      0.00     0.00   0.00    0.00     0.00    0.00    0.00    0.96  97.20
```

출력에서 볼 것:

- `vmstat`: `b=1`(dd 하나가 D 상태), `bo` ≈ 20,480 KiB/s = 정확히 QEMU 제한 20 MB/s. `wa` 41~47%: CPU 2개 중 하나는 "I/O를 기다리는 태스크가 있는 채로 노는" 중이고, 다른 하나는 그냥 논다(`id` 49%).
- **`wa`는 바쁨이 아니다.** idle의 한 종류다. CPU를 더 줘도 빨라지지 않는다. 그리고 CPU가 많을수록 같은 I/O 대기에서 `wa`는 작아 보인다(대기 태스크 1개 ÷ CPU 수) — 그래서 `wa`만 보고 판단하면 안 된다.
- `iostat -x`: `w_await`(쓰기 요청 하나가 큐 + 장치에서 보낸 평균 ms), `wareq-sz`(요청 크기 KiB), `aqu-sz`(평균 큐 길이), **`%util`**(장치가 요청을 하나라도 처리 중이던 시간 비율). `%util` 97.2% + `w_await` 48 ms(1 MiB 요청 하나에 48 ms ≈ 20 MB/s의 역수) = 이 장치는 포화. 단, NVMe·eMMC처럼 병렬 큐가 있는 장치는 `%util` 100%여도 더 받을 수 있다 — 그때는 `aqu-sz`와 `await`가 늘어나는지로 본다.
- `pidstat -d`: 누가 쓰는지 — `dd`가 20 MB/s. `iodelay`(블록 I/O 대기로 지연된 clock tick)는 delay accounting(`CONFIG_TASK_DELAY_ACCT`)이 없는 `Image-sec`이라 0으로 나왔다. 그 옵션이 있는 커널(`Image-trace`)이어도 5.14 이후는 기본 비활성이라 `delayacct` 커널 인자나 `sysctl kernel.task_delayacct=1`이 필요하다.
- **PSI io `full` avg10 = 41.77**: 지난 10초 중 약 42%의 시간 동안 "runnable할 수 있는 모든 태스크가 I/O 때문에 멈춰 있었다". dd 말고 할 일이 없는 시스템이라 some ≈ full이다.

### 9.5 메모리 hog — si/so와 PSI memory

첫 시도(`--vm 2 --vm-bytes 650M`)는 압력이 생기지 않았다. stress-ng는 `--vm-bytes`를 **인스턴스 수로 나눠** 쓰기 때문에 실제로는 325 MiB × 2였고, `free`가 70 MiB까지 줄었을 뿐 swap은 0, PSI memory도 0이었다. "메모리를 꽉 채웠다"와 "메모리가 모자라다"는 다르다는 좋은 예다 — 남은 페이지 캐시(`buff/cache`)를 회수할 수 있는 동안은 아무도 기다리지 않는다. 그래서 `--vm-bytes 1300M`(325×2의 두 배, RAM 953 MiB보다 큼)으로 다시 했다.

```text
##### MEM hog 1300M
--- [mem] loadavg: 1.56 1.27 0.58 4/86 389
PSI cpu: some avg10=34.55 avg60=15.03 avg300=12.96 total=67996532 full avg10=0.00 avg60=0.00 avg300=0.00 total=0 
PSI io: some avg10=6.52 avg60=7.06 avg300=2.28 total=8419616 full avg10=2.76 avg60=5.76 avg300=1.86 total=6592872 
PSI memory: some avg10=36.94 avg60=11.19 avg300=2.60 total=8064171 full avg10=33.14 avg60=9.97 avg300=2.31 total=7136649 
               total        used        free      shared  buff/cache   available
Mem:             953         942          33           2          23          10
Swap:            511         220         291
   r    b         swpd         free         buff        cache   si   so    bi    bo   in   cs  us  sy  id  wa  st  gu
   2    1       235548        38696         2540        22104  325 1459   925  2115  284    1  12  32  54   2   0   0
   3    0       251660        35076         2536        21416 5596 21704  5596 21704  677  383   9  87   1   3   0   0
   3    0       262332        33272         2528        20544 7008 17628  7008 17628  743  398  11  88   0   1   0   0
   3    0       276644        33104         2500        19496 3840 18140  3840 18140  615  344   9  90   0   1   0   0
Average:        0       387    193.85      9.74  690540  621352  63.67  stress-ng-vm
Average:        0       388   2706.15      0.00  690540  247796  25.39  stress-ng-vm
pswpin 25129
pswpout 113730
allocstall_normal 1
pgmajfault 3380
```

(`pidstat -r` 칸: UID, PID, `minflt/s`, `majflt/s`, VSZ, RSS(KiB), `%MEM`, Command.)

출력에서 볼 것:

- `free -m`: `available` 10 MiB, `buff/cache` 23 MiB — 페이지 캐시까지 다 빼앗겼다. swap 220 MiB 사용.
- `vmstat`: **`si`/`so`가 수천~2만 KiB/s**. swap-out 18~22 MB/s, swap-in 4~7 MB/s가 동시에 일어난다 — 내보낸 페이지를 곧바로 다시 읽어 오는 **thrashing**이다. `bi`/`bo`가 `si`/`so`와 같은 값인 것은 swap I/O도 블록 I/O로 세기 때문이다. `sy`가 87~90%로 뛴 것은 reclaim·페이지 폴트 처리가 커널 시간이기 때문이다(`us`는 오히려 9~11%로 떨어졌다 — 일은 못 하고 커널이 메모리를 옮기느라 바쁘다).
- `pidstat -r`: 두 stress-ng 프로세스의 RSS가 621 MiB / 248 MiB로 불균형하고, 두 번째는 `minflt/s` 2706 — 계속 쫓겨났다가 다시 폴트로 올라오는 쪽이다. `majflt/s`(디스크에서 읽어야 하는 폴트)는 swap-in으로 나타난다.
- `/proc/vmstat`의 `pswpin`/`pswpout`(누적 페이지 수)이 0에서 2.5만/11만으로 늘었다.
- **PSI memory full avg10 = 33.14**: 지난 10초 중 1/3은 모든 태스크가 메모리 때문에 멈췄다. 이 값이 OOM killer보다 먼저 울리는 경보다. 그리고 PSI cpu some도 34.55로 올랐다 — runnable 3개(`r=3`)가 CPU 2개를 다퉜기 때문이다. 메모리 문제가 CPU 문제처럼 보일 수 있으니 **세 PSI를 같이** 본다.
- swap이 없는 장비(대부분의 임베디드)라면 같은 상황에서 si/so 대신 **페이지 캐시 thrashing**(실행 파일의 코드 페이지가 쫓겨났다 다시 읽힘 → `bi`와 `pgmajfault` 증가)이 일어나고, 끝내 OOM killer가 뜬다(F9, F11). PSI memory는 swap이 없어도 그 thrashing을 잡는다(refault 감지).

### 9.6 D 상태 6개 — load average 높은데 CPU는 논다

무엇을 확인하나: CPU를 거의 안 쓰는 I/O 대기 태스크만으로 load average가 오르는 것. 20 MB/s 디스크에서 `O_DIRECT`로 읽는 `dd` 6개를 60초 돌렸다(swap은 끈 상태).

```sh
for i in 1 2 3 4 5 6; do
  dd if=/dev/vdb of=/dev/null bs=1M iflag=direct skip=$((i*100)) count=300 status=none & PS="$PS $!"; done
sleep 60; snap dstate; vmstat -w 1 3; mpstat 1 1 | tail -1; top -b -n1 -w 120 | head -5
ps -eo stat,pid,wchan:22,comm | awk '$1 ~ /^D/'
```

```text
--- [dstate] loadavg: 4.59 2.27 0.99 1/86 410
PSI cpu: some avg10=0.10 avg60=8.50 avg300=12.20 total=75970922 full avg10=0.00 avg60=0.00 avg300=0.00 total=0 
PSI io: some avg10=99.46 avg60=64.49 avg300=20.16 total=69654775 full avg10=96.43 avg60=62.01 avg300=19.12 total=65321764 
PSI memory: some avg10=0.07 avg60=5.77 avg300=3.38 total=13278847 full avg10=0.06 avg60=5.16 avg300=3.02 total=11851119 
   r    b         swpd         free         buff        cache   si   so    bi    bo   in   cs  us  sy  id  wa  st  gu
   1    6            0       863572         2488        41032  429 2013  4895  2508  265    1  10  29  41  20   0   0
   0    6            0       863640         2488        41032    0    0 20480     0  114   99   0   4   0  96   0   0
   0    6            0       863640         2488        41032    0    0 20480     0   69   63   0   2   0  98   0   0
Average:     all    0.50    0.00    0.50   97.49    1.01    0.50    0.00    0.00    0.00    0.00
top - 08:09:00 up 5 min,  0 user,  load average: 4.70, 2.33, 1.02
Tasks:  84 total,   1 running,  83 sleeping,   0 stopped,   0 zombie
%Cpu(s):  6.1 us, 27.5 sy,  0.0 ni, 11.5 id, 41.2 wa, 12.2 hi,  1.5 si,  0.0 st 
MiB Mem :    953.1 total,    843.7 free,    121.1 used,     43.3 buff/cache     
MiB Swap:      0.0 total,      0.0 free,      0.0 used.    832.0 avail Mem 
D        402 submit_bio_wait        dd
D        403 submit_bio_wait        dd
D        404 submit_bio_wait        dd
D        405 submit_bio_wait        dd
D        406 submit_bio_wait        dd
D        407 submit_bio_wait        dd
```

출력에서 볼 것:

- **load average 1분 4.59 → 4.70, 계속 상승 중** (2 vCPU 장비에서 "CPU 수의 2배 이상"). 그런데 `r=0`, `mpstat`의 `%usr` 0.5, `%iowait` 97.5. CPU 기준으로는 완전히 노는 시스템이다. 1분 값이 6에 못 미치는 것은 지수 평균이 60초 동안 아직 수렴하지 않았기 때문이다(1분 EMA는 60초 뒤 목표의 약 63%까지 간다; 4.59 / 6 ≈ 0.77 — 첫 수 초 동안은 이전 단계의 부하도 섞였다).
- `vmstat b=6`: D 상태 6개. `bi` 20,480 KiB/s = 디스크 한도 그대로. 6개가 1/6씩 나눠 쓴다.
- `ps ... wchan`: 6개 모두 `submit_bio_wait` — 블록 계층에 bio를 내고 완료를 기다리는 곳이다. 이것이 "어디서 자는가"의 답이다.
- **PSI io some 99.46 / full 96.43**: 지난 10초 동안 거의 항상 모든 태스크가 I/O에 막혀 있었다. load average(4.6)보다 이 숫자가 상황을 훨씬 직접적으로 말해 준다.
- `top`의 순간 값(`41.2 wa, 27.5 sy`)이 `mpstat` 1초 평균(97.5 iowait)과 다른 것은 `top`이 자기 자신을 실행하는 순간을 찍기 때문이다(TCG에서 `top` 자체가 무겁다 — 기준선에서도 `top`이 37~56% CPU로 1위였다). 한 번 찍는 `top -b -n1`은 순간 값이라 흔들린다. 비율은 `vmstat`/`mpstat`의 구간 평균으로 읽는다.

### 9.7 load average — Linux만의 정의

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="14">Linux load average = 지수 평균( R 태스크 수 + D 태스크 수 )</text>
  <rect x="20" y="40" width="190" height="90" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="64" font-size="13" text-anchor="middle">R: running</text>
  <text x="115" y="84" font-size="12" text-anchor="middle">CPU에서 실행 중</text>
  <text x="115" y="104" font-size="12" text-anchor="middle">+ run queue 대기</text>
  <rect x="236" y="40" width="190" height="90" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="331" y="64" font-size="13" text-anchor="middle">D: uninterruptible</text>
  <text x="331" y="84" font-size="12" text-anchor="middle">블록 I/O, 일부 lock,</text>
  <text x="331" y="104" font-size="12" text-anchor="middle">NFS, 드라이버 대기</text>
  <rect x="452" y="40" width="208" height="90" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="556" y="64" font-size="13" text-anchor="middle">S: interruptible</text>
  <text x="556" y="84" font-size="12" text-anchor="middle">read() on socket, poll,</text>
  <text x="556" y="104" font-size="12" text-anchor="middle">sleep → 포함 안 됨</text>
  <line x1="115" y1="130" x2="200" y2="176" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="331" y1="130" x2="250" y2="176" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="120" y="180" width="200" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="220" y="205" font-size="13" text-anchor="middle">calc_load (5초마다)</text>
  <line x1="320" y1="200" x2="380" y2="200" stroke="currentColor" marker-end="url(#ah7)"/>
  <text x="390" y="196" font-size="12">1분 · 5분 · 15분</text>
  <text x="390" y="214" font-size="12">지수 이동 평균</text>
  <text x="20" y="252" font-size="12">다른 Unix(BSD, Solaris)는 R만 센다. Linux는 1993년부터 D도 센다 → "시스템 수요" 지표</text>
  <text x="20" y="276" font-size="12">그래서 load 8 / CPU 4개 / CPU 30% idle 이 가능하다: 나머지는 D 상태에서 I/O를 기다리는 중</text>
</svg>
```
그림 7 — Linux load average의 구성. CPU 사용률과 다른 양이다. CPU 수로 나눠서 읽고, R과 D를 분리해서 본다.

- `/proc/loadavg`의 처음 세 숫자가 1·5·15분 평균이다. 커널은 `calc_global_load()`에서 약 5초마다 `nr_running + nr_uninterruptible`을 샘플해 지수 가중 평균을 갱신한다(`kernel/sched/loadavg.c`).
- "몇이면 높은가?"는 **CPU 수로 나눠서** 본다. 4코어에서 4.0은 "평균적으로 꽉 참"이다. 그리고 D가 섞이므로 load가 높다고 CPU가 바쁜 것은 아니다 — `vmstat`의 r과 b로 나눠 본다.
- D 상태가 많은 원인은 블록 I/O만이 아니다. `mutex_lock()`(uninterruptible), 드라이버의 `wait_for_completion()`(timeout 없는 것), 응답 없는 NFS 서버도 D를 만든다. `ps -eo stat,pid,wchan:22,comm`의 `wchan` 칸이나 `cat /proc/<pid>/stack`(root)으로 **어디서 자는지** 본다. 120초 넘게 D에 있으면 hung task detector(`kernel.hung_task_timeout_secs`)가 스택을 찍는다(M3, L7).

### 9.8 PSI — 현대적인 신호

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="14">PSI (/proc/pressure/cpu, io, memory) — "자원이 모자라서 잃은 시간의 비율"</text>
  <text x="20" y="52" font-size="12">태스크 A</text>
  <rect x="100" y="38" width="120" height="20" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
  <rect x="220" y="38" width="200" height="20" fill="#e08a3c" fill-opacity="0.3" stroke="#e08a3c"/>
  <rect x="420" y="38" width="220" height="20" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
  <text x="20" y="84" font-size="12">태스크 B</text>
  <rect x="100" y="70" width="220" height="20" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
  <rect x="320" y="70" width="180" height="20" fill="#e08a3c" fill-opacity="0.3" stroke="#e08a3c"/>
  <rect x="500" y="70" width="140" height="20" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
  <text x="20" y="122" font-size="12">some</text>
  <rect x="220" y="108" width="280" height="20" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
  <text x="510" y="122" font-size="12">하나라도 막힘</text>
  <text x="20" y="154" font-size="12">full</text>
  <rect x="320" y="140" width="100" height="20" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="430" y="154" font-size="12">모두 막힘 (일한 태스크 0)</text>
  <line x1="100" y1="172" x2="640" y2="172" stroke="currentColor"/>
  <text x="100" y="188" font-size="12">0</text>
  <text x="640" y="188" font-size="12" text-anchor="end">10 s</text>
  <rect x="100" y="204" width="14" height="12" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
  <text x="120" y="215" font-size="12">일하는 중</text>
  <rect x="210" y="204" width="14" height="12" fill="#e08a3c" fill-opacity="0.3" stroke="#e08a3c"/>
  <text x="230" y="215" font-size="12">자원 대기 (I/O, 메모리, CPU)</text>
  <text x="20" y="246" font-size="12">이 그림이면 some = 280/540 ≈ 52 %, full = 100/540 ≈ 19 % (avg10에 해당)</text>
  <text x="20" y="270" font-size="12">avg10 / avg60 / avg300 = 최근 10 · 60 · 300초 평균 %, total = 누적 us (차이로 직접 계산)</text>
  <text x="20" y="294" font-size="12">cpu의 full은 시스템 전체에선 정의상 0 (cgroup 단위에서만 의미), io·memory의 full이 핵심</text>
  <text x="20" y="318" font-size="12">CONFIG_PSI=y (+ 기본 꺼짐이면 psi=1), 4.20+ 커널</text>
</svg>
```
그림 8 — PSI의 some과 full. 사용률(얼마나 바쁜가)이 아니라 **지연(얼마나 기다렸나)**을 잰다.

네 실험의 PSI avg10을 한 그림에 모으면 자원별 신호가 깨끗하게 갈린다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="18" font-size="13">PSI avg10 (%) — 시나리오별 실측 (Image-sec 게스트, 2 vCPU, vdb 20 MB/s 제한)</text>
<line x1="60" y1="250.0" x2="660" y2="250.0" stroke="#888" stroke-width="0.4"/><text x="54" y="254.0" font-size="12" text-anchor="end">0</text>
<line x1="60" y1="200.0" x2="660" y2="200.0" stroke="#888" stroke-width="0.4"/><text x="54" y="204.0" font-size="12" text-anchor="end">25</text>
<line x1="60" y1="150.0" x2="660" y2="150.0" stroke="#888" stroke-width="0.4"/><text x="54" y="154.0" font-size="12" text-anchor="end">50</text>
<line x1="60" y1="100.0" x2="660" y2="100.0" stroke="#888" stroke-width="0.4"/><text x="54" y="104.0" font-size="12" text-anchor="end">75</text>
<line x1="60" y1="50.0" x2="660" y2="50.0" stroke="#888" stroke-width="0.4"/><text x="54" y="54.0" font-size="12" text-anchor="end">100</text>
<rect x="80" y="248.9" width="26" height="1.1" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="110" y="250.0" width="26" height="0.8" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<rect x="140" y="250.0" width="26" height="0.8" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="123" y="268" font-size="12" text-anchor="middle">idle</text>
<rect x="198" y="249.0" width="26" height="1.0" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="228" y="250.0" width="26" height="0.8" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<rect x="258" y="250.0" width="26" height="0.8" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="241" y="268" font-size="12" text-anchor="middle">CPU hog</text>
<rect x="316" y="245.5" width="26" height="4.5" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="329" y="241.5" font-size="12" text-anchor="middle">2</text>
<rect x="346" y="166.5" width="26" height="83.5" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="359" y="162.5" font-size="12" text-anchor="middle">42</text>
<rect x="376" y="250.0" width="26" height="0.8" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="359" y="268" font-size="12" text-anchor="middle">I/O hog</text>
<rect x="434" y="180.9" width="26" height="69.1" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<text x="447" y="176.9" font-size="12" text-anchor="middle">35</text>
<rect x="464" y="244.5" width="26" height="5.5" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="477" y="240.5" font-size="12" text-anchor="middle">3</text>
<rect x="494" y="183.7" width="26" height="66.3" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="507" y="179.7" font-size="12" text-anchor="middle">33</text>
<text x="477" y="268" font-size="12" text-anchor="middle">메모리 hog</text>
<rect x="552" y="249.8" width="26" height="0.8" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/>
<rect x="582" y="57.1" width="26" height="192.9" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/>
<text x="595" y="53.1" font-size="12" text-anchor="middle">96</text>
<rect x="612" y="249.9" width="26" height="0.8" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/>
<text x="595" y="268" font-size="12" text-anchor="middle">D 상태 6개</text>
<line x1="60" y1="250" x2="660" y2="250" stroke="currentColor"/>
<rect x="80" y="290" width="14" height="12" fill="#4a7bd0" fill-opacity="0.5" stroke="#4a7bd0"/><text x="100" y="301" font-size="12">cpu some</text>
<rect x="240" y="290" width="14" height="12" fill="#e08a3c" fill-opacity="0.5" stroke="#e08a3c"/><text x="260" y="301" font-size="12">io full</text>
<rect x="400" y="290" width="14" height="12" fill="#3f9a6b" fill-opacity="0.5" stroke="#3f9a6b"/><text x="420" y="301" font-size="12">memory full</text>
</svg>
```
그림 9 — 실측 PSI avg10. CPU hog는 runnable 수 = CPU 수라 cpu some이 0에 가깝고, 메모리 hog는 memory full과 함께 cpu some도 올리며, D 상태 6개는 io full 96%다.

PSI(Pressure Stall Information, 4.20, Johannes Weiner)는 load average와 `wa`의 약점 — "몇 개가 줄 서 있나"만 알고 "그래서 얼마나 시간을 잃었나"는 모름 — 을 고친 신호다. 스케줄러가 태스크 상태가 바뀔 때마다 CPU별로 "막힌 태스크가 있는 시간"과 "모두 막힌 시간"을 누적한다(`kernel/sched/psi.c`).

- 위 실측에서 I/O hog는 `io full avg10=41.77`, D 6개는 `io full avg10=57.98`이었다. 숫자가 바로 "지난 10초 중 그만큼은 시스템이 I/O 때문에 아무 일도 못 했다"로 읽힌다. load average보다 해석이 직접적이다.
- `cpu some`은 "runnable인데 CPU를 못 받은 태스크가 있던 시간"이다. 9.3절처럼 runnable 수 = CPU 수면 0에 가깝다.
- **트리거**: `/proc/pressure/memory`에 `some 150000 1000000`(1초 창에서 150 ms 이상 막히면)을 쓰고 `poll()`하면 이벤트로 깨어난다. Android의 lmkd, systemd-oomd, Facebook oomd가 이걸로 OOM 전에 미리 앱을 죽인다. 장비라면 "메모리 압력이 오르면 녹화 해상도를 낮춘다" 같은 정책에 쓴다.
- cgroup v2의 각 그룹에도 `cpu.pressure`, `io.pressure`, `memory.pressure`가 있다(L8) — 서비스별 압력을 따로 본다.

### 9.9 나머지 도구 — 언제 무엇을

| 도구 | 보는 것 | 언제 쓰나 | 출처 |
|---|---|---|---|
| `vmstat -w 1` | r, b, swap, io, cs, CPU 비율 | 첫 30초 — 어느 줄인가 | `/proc/stat`, `/proc/vmstat` |
| `mpstat -P ALL 1` | CPU별 usr/sys/irq/soft/iowait | 한 CPU 쏠림, IRQ·softirq 과다 | `/proc/stat` |
| `iostat -x 1` | 장치별 r/s, w/s, await, aqu-sz, %util | 저장장치 포화, 느린 요청 | `/proc/diskstats` |
| `pidstat -u -w -r -d 1` | 프로세스별 CPU·%wait, cswch/s·nvcswch/s, 메모리·page fault, I/O | 범인 찾기 | `/proc/<pid>/stat`, `status`, `io`, `schedstat` |
| `sar` | 위 전부의 **이력** (sysstat 서비스가 10분마다 기록) | "어젯밤 3시에 느렸다" | `/var/log/sysstat/` |
| `top` / `htop` | 실시간 프로세스 순위, 상태(R/S/D), load, 메모리 | 대화형 탐색 | `/proc` |
| PSI | 자원별 지연 비율 | 모니터링·알림·자동 대응 | `/proc/pressure/*` |
| `perf sched record/latency` | 스케줄링 지연(누가 얼마나 기다렸나) | 실시간성·지터 문제 | sched 트레이스포인트 (M6, N 모듈) |
| `/proc/schedstat`, `/proc/<pid>/schedstat` | 실행 시간·대기 시간·타임슬라이스 수 | 도구 없이 직접 | `CONFIG_SCHEDSTATS` |
| `iotop-c` | 프로세스별 I/O 실시간 | I/O 범인 | delay accounting |

`pidstat -w`의 `cswch/s`(자발적: 잠들어서 양보 — I/O·lock 대기)와 `nvcswch/s`(비자발적: 타임슬라이스를 뺏김 — CPU 경쟁)를 구별하는 것이 유용하다. 자발적 전환이 많으면 기다리는 태스크, 비자발적 전환이 많으면 CPU를 다투는 태스크다.

`top` 머리 줄 읽기(위 메모리 hog 회차의 실제 출력에서):

```text
top - 08:02:14 up 4 min,  0 user,  load average: 1.51, 1.25, 0.60
Tasks:  87 total,   3 running,  84 sleeping,   0 stopped,   0 zombie
%Cpu(s): 65.8 us, 21.1 sy,  0.0 ni,  0.0 id,  0.0 wa, 11.8 hi,  1.3 si,  0.0 st 
MiB Mem :    953.1 total,     70.1 free,    794.3 used,    157.3 buff/cache     
MiB Swap:    512.0 total,    512.0 free,      0.0 used.    158.8 avail Mem 
```

`hi`/`si` = 하드/소프트 인터럽트 처리 비율, `avail Mem` = 캐시를 비우면 쓸 수 있는 양(free보다 이 값을 본다). 프로세스 목록의 `S` 칸이 `R`/`S`/`D`/`Z`/`I`(idle 커널 스레드) 상태다. `htop`은 같은 정보를 CPU별 막대로 보여 주고 `F2`에서 D 상태 강조, I/O 칸 추가가 된다.

---

## 10. 증상 → 도구 지도

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah9" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="14">증상에서 시작해 도구로 — 첫 질문과 다음 도구</text>
  <rect x="20" y="40" width="220" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="130" y="65" font-size="12" text-anchor="middle">부팅이 요구보다 길다</text>
  <line x1="240" y1="60" x2="290" y2="60" stroke="currentColor" marker-end="url(#ah9)"/>
  <rect x="294" y="40" width="366" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="477" y="58" font-size="12" text-anchor="middle">GPIO·scope → bootstage → initcall_debug</text>
  <text x="477" y="74" font-size="12" text-anchor="middle">→ 부팅 ftrace → systemd-analyze</text>
  <rect x="20" y="96" width="220" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="130" y="121" font-size="12" text-anchor="middle">CPU 100%, 응답 느림</text>
  <line x1="240" y1="116" x2="290" y2="116" stroke="currentColor" marker-end="url(#ah9)"/>
  <rect x="294" y="96" width="366" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="477" y="114" font-size="12" text-anchor="middle">vmstat us/sy → mpstat -P ALL → pidstat -u</text>
  <text x="477" y="130" font-size="12" text-anchor="middle">→ perf top / record (M6)</text>
  <rect x="20" y="152" width="220" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="130" y="177" font-size="12" text-anchor="middle">load 높은데 CPU 한가</text>
  <line x1="240" y1="172" x2="290" y2="172" stroke="currentColor" marker-end="url(#ah9)"/>
  <rect x="294" y="152" width="366" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="477" y="170" font-size="12" text-anchor="middle">vmstat b → ps stat=D, wchan → iostat -x</text>
  <text x="477" y="186" font-size="12" text-anchor="middle">→ PSI io → /proc/PID/stack</text>
  <rect x="20" y="208" width="220" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="130" y="233" font-size="12" text-anchor="middle">점점 느려지다 멈춤</text>
  <line x1="240" y1="228" x2="290" y2="228" stroke="currentColor" marker-end="url(#ah9)"/>
  <rect x="294" y="208" width="366" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="477" y="226" font-size="12" text-anchor="middle">free avail → vmstat si/so → PSI memory</text>
  <text x="477" y="242" font-size="12" text-anchor="middle">→ pidstat -r → /proc/meminfo, slabtop (F9)</text>
  <rect x="20" y="264" width="220" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="130" y="289" font-size="12" text-anchor="middle">가끔 늦다 (지터)</text>
  <line x1="240" y1="284" x2="290" y2="284" stroke="currentColor" marker-end="url(#ah9)"/>
  <rect x="294" y="264" width="366" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="477" y="282" font-size="12" text-anchor="middle">pidstat -w → perf sched latency → cyclictest</text>
  <text x="477" y="298" font-size="12" text-anchor="middle">→ ftrace wakeup/irqsoff, rtla (M5, N1~N4)</text>
  <rect x="20" y="320" width="220" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="130" y="345" font-size="12" text-anchor="middle">sy 높음, cs·in 폭증</text>
  <line x1="240" y1="340" x2="290" y2="340" stroke="currentColor" marker-end="url(#ah9)"/>
  <rect x="294" y="320" width="366" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="477" y="338" font-size="12" text-anchor="middle">/proc/interrupts → mpstat %irq·%soft</text>
  <text x="477" y="354" font-size="12" text-anchor="middle">→ strace -c → perf (커널 심볼)</text>
  <text x="20" y="394" font-size="12">모든 줄의 공통 순서: 넓게(시스템 전체) → 좁게(CPU·장치) → 범인(프로세스) → 원인(스택·트레이스)</text>
</svg>
```
그림 10 — 증상 → 도구 지도. 처음부터 perf나 ftrace를 꺼내지 않는다. 60초짜리 넓은 도구로 어느 줄인지 먼저 정한다.

Brendan Gregg의 "Linux Performance Analysis in 60,000 Milliseconds" 체크리스트(`uptime`, `dmesg | tail`, `vmstat 1`, `mpstat -P ALL 1`, `pidstat 1`, `iostat -xz 1`, `free -m`, `sar -n DEV 1`, `sar -n TCP,ETCP 1`, `top`)가 이 지도의 왼쪽 열을 1분 안에 훑는 순서다. 장비에 sysstat이 없으면 `cat /proc/loadavg /proc/pressure/* ; head -1 /proc/stat` 같은 원시 파일을 두 번 읽고 차이를 보는 것으로도 대부분 답이 나온다.

---

## 11. 커널 안에서는 무슨 일이 일어나나

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah10" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="14">도구의 숫자는 어디서 오나</text>
  <rect x="20" y="40" width="200" height="150" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="120" y="62" font-size="13" text-anchor="middle">스케줄러</text>
  <text x="120" y="84" font-size="12" text-anchor="middle">rq->nr_running</text>
  <text x="120" y="104" font-size="12" text-anchor="middle">nr_uninterruptible</text>
  <text x="120" y="124" font-size="12" text-anchor="middle">calc_global_load()</text>
  <text x="120" y="144" font-size="12" text-anchor="middle">psi_task_change()</text>
  <text x="120" y="164" font-size="12" text-anchor="middle">kcpustat (us/sy/irq...)</text>
  <rect x="240" y="40" width="200" height="150" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="62" font-size="13" text-anchor="middle">블록 계층</text>
  <text x="340" y="84" font-size="12" text-anchor="middle">part_stat (ios, ticks,</text>
  <text x="340" y="104" font-size="12" text-anchor="middle">io_ticks, in_flight)</text>
  <text x="340" y="124" font-size="12" text-anchor="middle">blk-mq 완료 시 갱신</text>
  <text x="340" y="144" font-size="12" text-anchor="middle">io_schedule() → D 상태</text>
  <rect x="460" y="40" width="200" height="150" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="560" y="62" font-size="13" text-anchor="middle">메모리 관리</text>
  <text x="560" y="84" font-size="12" text-anchor="middle">vm_event_states</text>
  <text x="560" y="104" font-size="12" text-anchor="middle">(pswpin, pgmajfault)</text>
  <text x="560" y="124" font-size="12" text-anchor="middle">psi_memstall_enter()</text>
  <text x="560" y="144" font-size="12" text-anchor="middle">reclaim, refault</text>
  <line x1="120" y1="190" x2="120" y2="240" stroke="currentColor" marker-end="url(#ah10)"/>
  <line x1="340" y1="190" x2="340" y2="240" stroke="currentColor" marker-end="url(#ah10)"/>
  <line x1="560" y1="190" x2="560" y2="240" stroke="currentColor" marker-end="url(#ah10)"/>
  <rect x="20" y="244" width="200" height="60" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="268" font-size="12" text-anchor="middle">/proc/stat · loadavg</text>
  <text x="120" y="288" font-size="12" text-anchor="middle">/proc/pressure/cpu</text>
  <rect x="240" y="244" width="200" height="60" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="268" font-size="12" text-anchor="middle">/proc/diskstats</text>
  <text x="340" y="288" font-size="12" text-anchor="middle">/proc/pressure/io</text>
  <rect x="460" y="244" width="200" height="60" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="560" y="268" font-size="12" text-anchor="middle">/proc/vmstat, meminfo</text>
  <text x="560" y="288" font-size="12" text-anchor="middle">/proc/pressure/memory</text>
  <line x1="340" y1="304" x2="340" y2="340" stroke="currentColor" marker-end="url(#ah10)"/>
  <rect x="60" y="344" width="560" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="369" font-size="12" text-anchor="middle">vmstat · mpstat · iostat · pidstat · sar · top = 두 번 읽고 빼기</text>
</svg>
```
그림 11 — 사용자 도구는 모두 커널 카운터를 두 번 읽어 차이를 낸다. 카운터를 올리는 곳은 스케줄러, 블록 계층, 메모리 관리다.

- **CPU 비율**: 타이머 틱(또는 `CONFIG_VIRT_CPU_ACCOUNTING_GEN`이면 모드 전환 때)마다 `account_user_time()`/`account_system_time()`이 `kcpustat_cpu(cpu).cpustat[CPUTIME_USER/SYSTEM/IRQ/SOFTIRQ/IOWAIT/IDLE]`을 올린다. idle 중에 그 CPU의 run queue에 `nr_iowait`이 있으면 idle 대신 iowait으로 센다 — `wa`가 "idle의 한 종류"인 이유가 코드에 있다.
- **D 상태**: 블록 I/O를 제출한 태스크는 상태를 `TASK_UNINTERRUPTIBLE`로 바꾼 뒤 `io_schedule()`(`current->in_iowait = 1`을 세우고 `schedule()`)로 잔다. `/proc/stat`의 `procs_blocked`(= `vmstat`의 `b`)는 이 `in_iowait` 태스크 수(`nr_iowait()`)라서, I/O가 아닌 이유(mutex, `wait_for_completion`)로 D에 있는 태스크는 load average에는 들어가도 `b`에는 안 잡힌다 — 그때는 `ps`의 `D`로 찾는다. 9.6절의 `wchan`이 `submit_bio_wait`이었던 것은 `iflag=direct` 읽기가 bio를 내고 완료를 동기로 기다렸기 때문이다. 드라이버에서 `wait_for_completion()`을 쓰면 같은 D 상태가 되고 load average에 들어간다 — 드라이버가 인터럽트를 못 받아 완료가 안 오면 "load는 높은데 CPU는 노는" 장비가 된다(G 모듈, F6).
- **`/proc/diskstats`**: blk-mq가 요청을 완료할 때 `part_stat_add()`로 읽기·쓰기 횟수, 섹터, 소요 ms를 더하고, `io_ticks`(장치가 바빴던 ms)를 갱신한다. `iostat`의 `await` = Δ소요 ms / Δ요청 수, `%util` = Δio_ticks / Δ시간이다.
- **PSI**: `psi_task_change()`가 태스크 상태 전이(enqueue/dequeue, iowait 진입, memstall 진입)마다 CPU별 상태 시간을 누적하고, 2초 주기 작업이 avg10/60/300을 갱신한다. 메모리 쪽은 reclaim·compaction·thrashing 경로가 `psi_memstall_enter()/leave()`로 구간을 표시한다.
- **부팅 쪽**: `do_one_initcall()`이 `initcall_debug`면 `do_trace_initcall_start/finish`를 거쳐 printk를 하고, 트레이스포인트 `initcall:initcall_start/finish`는 항상 있다(§4.2). `driver_async_probe=`는 `drivers/base/dd.c`의 `driver_allows_async_probing()`이 확인하고, async면 `async_schedule_dev(__driver_attach_async_helper, dev)`로 던진다. deferred probe는 같은 파일의 `deferred_probe_pending_list`와 `driver_deferred_probe_trigger()`다.

---

## 12. Embedded 실무에서 다시 보기

**장면 1 — 센서 게이트웨이(i.MX8, eMMC, systemd) 부팅 25 s, 요구 5 s.** 첫날은 아무것도 고치지 않고 잰다. 리셋 핀과 앱의 READY GPIO를 로직 분석기로 찍어 25 s를 확인하고(M11), U-Boot에 bootstage를 켜서 펌웨어 구간을, `initcall_debug`로 커널 구간을, `systemd-analyze`로 유저 공간을 나눈다. 전형적인 결과는 "SPL+DDR 0.4 s, U-Boot 3 s(bootdelay 2 + 네트워크), 커널 4 s(벤더 defconfig 드라이버 1500개, 동기 콘솔), 유저 공간 17 s(서비스 60개, 그중 READY에 필요한 건 6개)"다. 그러면 순서가 정해진다: 유저 공간 critical path 정리(L1) → 커널 config 다이어트 + `quiet` → U-Boot `bootdelay=0` + 직접 부팅 + 네트워크 끔 → 필요하면 Falcon 모드. 매 단계 같은 하네스로 다시 잰다.

**장면 2 — "카메라 녹화가 가끔 끊긴다", 현장 장비, load average 9.** `vmstat 1`에서 `b`가 6~8, `wa` 40%, `r`은 1~2. `ps -eo stat,wchan,comm`에서 녹화 스레드와 로그 수집기가 `D`로 `jbd2`·`blk_mq_get_tag` 근처에서 잔다. `iostat -x`에서 SD 카드 `w_await` 800 ms, `%util` 100%. 원인은 로그 수집기의 `fsync` 폭주 + 싼 SD 카드. 조치: 로그를 tmpfs/journald 메모리 저장으로(L1 §8), 녹화 버퍼를 키우고 쓰기 크기를 정렬, PSI io 트리거로 압력이 오르면 로그 레벨을 낮춘다.

**장면 3 — 드라이버 probe가 부팅을 2 s 막는다.** `initcall_debug`에서 `xxx_phy_driver_init ... after 2010432 usecs`. 부팅 ftrace(`ftrace_graph_filter=xxx_phy_probe`)로 보면 `msleep`이 아니라 링크 업을 폴링하는 루프다. 링크 대기를 probe에서 빼서 `phylink`/workqueue로 넘기거나, 드라이버에 `PROBE_PREFER_ASYNCHRONOUS`를 준다(§5). 다른 드라이버가 이 PHY에 의존하면 `-EPROBE_DEFER` 경로가 제대로 되는지 `devices_deferred`로 확인한다.

**장면 4 — 장비 부팅 시간을 텔레메트리로.** READY 시점에 `CNTVCT_EL0`(리셋 후 경과), `/proc/device-tree/bootstage/*`(U-Boot 구간), `systemd-analyze`의 kernel/userspace 값을 한 줄 JSON으로 서버에 보낸다. 필드 장비 수천 대의 분포를 보면 "eMMC 특정 로트에서 부팅이 1.5 s 길다" 같은 것이 보인다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| printk 시각만으로 부팅 시간 보고 | "커널 2 s"인데 실측 6 s | printk 0점은 sched_clock 등록 시점, 펌웨어 구간 누락 | bootstage + `CNTVCT_EL0`, 외부 GPIO 측정 |
| 시리얼 로그 도착 시각으로 커널 초기 측정 | `Booting Linux`가 0.9 s 늦게 찍힘 | 콘솔 등록 전 메시지는 버퍼에 있다가 한꺼번에 출력 | 커널 구간은 printk 시각으로 |
| `initcall_debug` 기본 로그 버퍼 | initcall 수가 이상하게 적음 | 128 KB 링버퍼가 덮어씀 | `log_buf_len=4M` |
| 디버그 커널로 부팅 시간 측정 | 1~5 s 더 김 | `init_kprobe_trace` 1.44 s 등 디버그 기능 비용 | 양산 config로 측정 |
| function_graph를 넓게 켬 | 부팅 5 s → 30 s | 모든 함수에 그래프 훅 | 이벤트로 좁힌 뒤 `ftrace_graph_filter` 한 함수 |
| `wa`가 높으니 CPU 증설 | 변화 없음 | `wa`는 idle의 일종 | `iostat -x`, PSI io로 저장장치 확인 |
| load average를 CPU 사용률로 읽음 | "load 8인데 CPU 30%?" 혼란 | Linux load는 R + D | `vmstat` r·b 분리, `ps` D 상태 |
| 측정 스크립트를 부팅 트랜잭션 안에 둠 | `systemd-analyze` "Bootup is not yet finished", 무한 대기 | 자기 자신이 부팅 잡 | `Type=exec` 또는 부팅 후 timer |
| async probe 후 장치 이름 의존 | `eth0`/`video0` 번호가 바뀜 | probe 순서 비결정 | DT aliases, udev 규칙 |
| `vmstat` 첫 줄을 현재 값으로 읽음 | 부팅 후 평균을 지금 부하로 착각 | 첫 줄은 부팅 이후 누적 평균 | 둘째 줄부터 |

---

## 14. 면접에서 이렇게 말한다

**Q.** "Boot takes 25 seconds and the requirement is 5 seconds. How do you approach it?"

**A.** 먼저 "부팅 완료"를 정의하고(앱이 무엇을 할 수 있으면 READY인가), 아무것도 고치지 않고 구간별로 잰다. 외부 GPIO로 전체를, U-Boot bootstage로 펌웨어를, `initcall_debug`와 printk 시각으로 커널을, `systemd-analyze`로 유저 공간을 나눈다. 그다음 가장 큰 덩어리부터: 보통 유저 공간 서비스 정리, 커널 config 다이어트와 `quiet`, `bootdelay`와 부트로더 스캔 제거, 느린 probe의 async화 순이다. 5 s가 구조적으로 불가능하면 Falcon 모드, suspend-to-RAM, 스냅샷 부팅 같은 구조 변경을 제안한다. 모든 변경은 같은 하네스로 다시 재고, CI에 부팅 시간 회귀 테스트를 넣는다.

> First I pin down what "booted" means for the product — the moment the app can actually do its job — and I measure before changing anything. I split the 25 seconds into stages with one clock: an external GPIO for end-to-end, U-Boot bootstage for the firmware, printk timestamps plus initcall_debug for the kernel, and systemd-analyze for user space. Then I attack the biggest chunk first. Typically that's user-space services that aren't on the critical path, then a kernel config diet and a quiet console, then bootloader delays and device scanning, then slow driver probes made asynchronous or deferred to a workqueue. If 5 seconds is structurally out of reach, I'd propose Falcon mode, suspend-to-RAM or snapshot boot. Every change gets re-measured with the same harness, and boot time goes into CI so it can't regress.

**꼬리 질문**: "U-Boot 구간은 어떻게 쟀나?" → `CONFIG_BOOTSTAGE` + `BOOTSTAGE_REPORT`, 필요하면 `BOOTSTAGE_FDT`로 커널에 넘겨 READY 때 함께 보고.

**Q.** "Load average is 8 on a 4-core board but the CPU is 30% idle. Why?"

**A.** Linux의 load average는 runnable 태스크만이 아니라 uninterruptible sleep(D) 태스크도 센다. 그래서 CPU는 놀고 있는데 load가 높으면 D 상태 태스크가 많다는 뜻 — 대개 느린 저장장치 I/O, 가끔 응답 없는 드라이버나 NFS, mutex 대기다. `vmstat`의 `b` 칸과 `ps`의 `D` 상태 + `wchan`으로 누가 어디서 자는지 보고, `iostat -x`의 await와 %util, PSI io로 장치가 포화인지 확인한다. 실제로 랩에서 20 MB/s로 제한한 디스크에 dd 6개를 걸면 CPU는 98% iowait/idle인데 load가 계속 오른다.

> On Linux, load average counts not only runnable tasks but also tasks in uninterruptible sleep — the D state. So a high load with idle CPU means lots of tasks blocked, usually on slow block I/O, sometimes on a stuck driver completion, NFS, or a mutex. I'd look at vmstat's b column and ps for D-state tasks with their wchan to see where they sleep, then iostat -x for await and utilization, and PSI io to see how much time is actually lost. I reproduced it in a lab: six direct-I/O readers on a disk throttled to 20 MB/s give six D tasks, about 98 percent iowait, and the load climbs while the CPUs do nothing.

**Q.** "What's the difference between %iowait and real CPU usage?"

**A.** iowait은 CPU가 놀고 있는데 그 CPU에 I/O를 기다리는 태스크가 걸려 있는 시간이다. idle의 한 종류라 CPU를 더 줘도 줄지 않고, CPU 수가 늘면 같은 대기에서 더 작게 보인다. 그래서 저장장치 문제를 판단할 때는 iowait 대신 `iostat -x`의 await/aqu-sz와 PSI io를 본다.

> iowait is idle time during which some task on that CPU is waiting for I/O. It's a flavor of idle, not work — adding CPUs doesn't reduce it, and with more CPUs the same stall shows a smaller percentage. For storage problems I trust iostat's await and queue size and PSI io rather than iowait.

**Q.** "What is PSI and why is it better than load average?"

**A.** Pressure Stall Information은 자원(CPU, I/O, 메모리) 때문에 태스크가 멈춰 있던 시간의 비율이다. some은 하나라도 막힌 시간, full은 모두 막혀 아무 일도 못 한 시간이고 10·60·300초 평균과 누적값을 준다. load average는 줄의 길이만 섞어서 알려 주지만 PSI는 자원별로 "잃은 시간"을 바로 준다. 트리거를 걸어 poll로 알림을 받을 수 있어 lmkd·systemd-oomd 같은 선제적 OOM 처리나 장비의 품질 저하 정책에 쓴다.

> PSI reports, per resource, the share of time tasks were stalled waiting for CPU, I/O or memory. "Some" means at least one task was stalled, "full" means all non-idle tasks were, so no progress was made. Unlike load average, which mixes queue lengths, PSI directly tells you how much time you lost and on which resource, and you can set thresholds and poll on them — that's how lmkd and systemd-oomd act before the OOM killer.

**Q.** "A driver's probe takes 2 seconds during boot. What are your options?"

**A.** 먼저 부팅 ftrace(`ftrace_graph_filter`)나 `initcall_debug`로 정말 그 probe인지, 안에서 무엇을 기다리는지 본다. 하드웨어를 기다리는 것이면 probe에서는 등록만 하고 기다림은 workqueue나 첫 open으로 미룬다. 코드를 못 고치면 `PROBE_PREFER_ASYNCHRONOUS`나 `driver_async_probe=`로 다른 initcall과 겹치게 한다. 다른 장치에 의존하면 `-EPROBE_DEFER`로 돌려주고, 부팅 직후 필요 없는 기능이면 모듈로 만들어 READY 뒤에 로드한다.

> First I confirm it with initcall_debug or a boot-time function_graph filtered on that probe, to see what it's actually waiting for. If it's waiting on hardware, the probe should just register and push the wait to a workqueue or the first open. If I can't change the code, I mark it PROBE_PREFER_ASYNCHRONOUS or use driver_async_probe= so it overlaps other initcalls — in my lab, four 500 ms probes went from 2.6 s serialized to 1.3 s overlapped. If it depends on another device it should return -EPROBE_DEFER, and if it isn't needed for the product's ready state, it becomes a module loaded after ready.

**Q.** "How does U-Boot measure its own boot time, and how would you get that into your telemetry?"

**A.** `CONFIG_BOOTSTAGE`가 정해진 지점마다 generic timer 기반 타임스탬프를 기록하고, `BOOTSTAGE_REPORT`로 커널 점프 직전에 표를 찍는다. `BOOTSTAGE_FDT`를 켜면 그 기록이 커널 DT의 `/bootstage` 노드로 들어가 `/proc/device-tree/bootstage`에서 읽힌다. 거기에 유저 공간에서 읽은 `CNTVCT_EL0` 값을 더하면 리셋부터 READY까지 한 시계로 보고할 수 있다.

> U-Boot's bootstage records timer-based timestamps at fixed points — board_init_f, board_init_r, main_loop, bootm_start, start_kernel — and BOOTSTAGE_REPORT prints the table right before jumping to the kernel. With BOOTSTAGE_FDT the records are copied into the kernel's device tree, so user space can read them from /proc/device-tree/bootstage. Combined with the arm64 virtual counter read at ready time, I can report reset-to-ready on a single clock from every unit in the field.

**Q.** "vmstat shows high cs and sy but low us. What do you suspect?"

**A.** 커널 안에서 시간을 쓰고 있다: 잦은 syscall(작은 read/write 반복), 인터럽트 폭주, lock 경쟁으로 인한 잦은 sleep/wakeup. `mpstat`의 `%irq`·`%soft`, `/proc/interrupts` 증가량, `pidstat -w`의 자발/비자발 전환, `strace -c`, `perf top`(커널 심볼)으로 좁힌다.

> Time is going into the kernel: very frequent small syscalls, an interrupt storm, or lock contention causing constant sleep and wakeup. I'd check mpstat irq and softirq, the delta of /proc/interrupts, voluntary versus involuntary switches in pidstat -w, strace -c on the suspect, and perf top with kernel symbols.

---

## 15. 직접 해보기

1. **개념**: §1.4에서 printk 0점이 U-Boot `start_kernel`보다 0.36 s 늦었다. 그 사이 커널은 무엇을 하나? 그 구간을 재려면 어떻게 하나?
   정답/힌트: head.S(MMU 켜기, 초기 페이지 테이블) → `start_kernel` → `setup_arch`, 메모리 초기화 → `time_init`에서 sched_clock 등록. `earlycon` + `CNTVCT` 출력, 또는 GPIO 토글(M11)로 잰다.
2. **측정**: `m10_flash.bin` U-Boot에서 `setenv bootdelay 0; saveenv`가 안 되는 이유(pflash 환경, C7)를 우회해, `--send`로 `bootdelay`를 건너뛴 부팅과 기본 부팅의 bootstage `bootm_start`를 비교하라.
   정답/힌트: 카운트다운에 키를 보내고 `boot`를 치면 `main_loop` → `bootm_start` 구간에서 2 s가 빠진다. C7처럼 `mkenvimage`로 환경 pflash를 만들어도 된다.
3. **코드**: `m10_slow.c`에 `PROBE_FORCE_SYNCHRONOUS`를 주고 `driver_async_probe=m10_slow`를 함께 걸면 어떻게 되나? 예측하고 돌려 보라.
   정답/힌트: `driver_allows_async_probing()`은 `PROBE_FORCE_SYNCHRONOUS`면 커맨드라인보다 우선해 동기로 돈다.
4. **트레이스**: §4.3에서 `__devm_rtc_register_device` 38.9 ms의 정체를 `ftrace_graph_filter=__devm_rtc_register_device ftrace_graph_max_depth=3`으로 한 단계 더 내려가 찾아라.
   정답/힌트: 등록 중 RTC에서 시간을 읽고 `/dev/rtc0` 문자 장치·sysfs를 만드는 경로를 본다. CPU 번호가 바뀌는 줄이 잠든 곳이다.
5. **관찰**: 9.4절 실험에서 `throttling.bps-total`을 2 MB/s로 낮추면 `iostat`의 `w_await`, `aqu-sz`, PSI io가 어떻게 바뀌나?
   정답/힌트: 처리량은 1/10, 같은 요청 크기면 await는 약 10배, %util은 그대로 100% 근처. PSI full은 비슷하다(할 일이 dd뿐이므로).
6. **관찰**: 게스트에서 `cat /proc/pressure/io`를 1초 간격으로 두 번 읽어 `total`의 차이로 "지난 1초 중 몇 ms를 잃었나"를 직접 계산하라.
   정답/힌트: `total`은 누적 us. Δtotal / 1,000,000 = 잃은 시간 비율.
7. **설계**: 센서 키트의 READY 정의를 "IMU 첫 샘플을 ROS 토픽으로 발행"으로 잡을 때, critical path에 있어야 하는 커널 드라이버와 서비스를 나열하고 나머지를 어떻게 뒤로 미룰지 써 보라.
   정답/힌트: eMMC·SPI·IMU 드라이버 빌트인, 네트워크는 READY 뒤 혹은 병렬, 앱 서비스는 `Type=notify`로 READY 시점을 명시(L1).

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| bootstage | U-Boot 부팅 타임스탬프 기록기 | `CONFIG_BOOTSTAGE`, `bootstage report` |
| `CNTVCT_EL0` | arm64 가상 카운터 | 리셋부터 증가, EL0에서 읽기 가능 |
| printk.time | printk 줄 앞 시각 | sched_clock 기준, 0점 = 시계 등록 |
| `initcall_debug` | initcall별 시작·소요 출력 | `log_buf_len` 키울 것 |
| bootgraph.pl | initcall 간트 SVG 생성기 | `scripts/bootgraph.pl` |
| `trace_event=` | 부팅 시점 이벤트 켜기 | 예: `initcall:*` |
| `ftrace_graph_filter=` | function_graph 대상 함수 | 부팅 중 한 probe 들여다보기 |
| bootconfig | initrd에 붙이는 커널 설정 트리 | `CONFIG_BOOTTIME_TRACING`과 함께 |
| async probe | probe를 kworker로 비동기 실행 | `PROBE_PREFER_ASYNCHRONOUS`, `driver_async_probe=` |
| deferred probe | 공급자 없으면 나중에 재시도 | `-EPROBE_DEFER`, `devices_deferred` |
| Falcon 모드 | SPL이 커널을 직접 부팅 | `CONFIG_SPL_OS_BOOT` |
| load average | R + D 태스크 수의 지수 평균 | 1·5·15분, CPU 수로 나눠 읽기 |
| D 상태 | uninterruptible sleep | I/O·완료 대기, 시그널로 못 깨움 |
| iowait (`wa`) | I/O 대기 태스크가 있는 idle | 바쁨이 아님 |
| steal (`st`) | 하이퍼바이저에 뺏긴 시간 | VM 게스트에서만 |
| `%util` | 장치가 바빴던 시간 비율 | 병렬 장치에선 포화의 증거가 아님 |
| `await` | 요청 평균 응답 ms (큐 + 장치) | `r_await`, `w_await` |
| `aqu-sz` | 평균 큐 길이 | 늘면 줄이 생긴 것 |
| PSI | Pressure Stall Information | some/full, avg10/60/300 |
| USE 방법 | Utilization·Saturation·Errors | 자원마다 세 질문 |
| sar | sysstat 이력 기록·조회 | 지난 시간대 분석 |

---

## 17. 요약 & 체크리스트

부팅 분석은 **한 시계로 구간을 나누는 일**이다. 펌웨어는 bootstage와 하드웨어 카운터로, 커널은 printk 시각·`initcall_debug`·부팅 ftrace로, 유저 공간은 systemd-analyze로 재고, 전체는 GPIO와 scope로 확인한다. 이 랩의 실측에서 리셋 → READY 5.78 s 중 가장 큰 덩어리는 아무 일도 안 하는 `bootdelay` 2 s였고, 그다음이 직렬로 도는 initcall 1887개(합 3.0 s, 최대 367 ms)였으며, U-Boot 네트워크 초기화 306 ms는 bootstage가 없었으면 보이지 않았다. 느린 probe는 async로 겹치면 4 × 500 ms가 2.6 s → 1.3 s가 됐다. 정상 상태 분석은 **무엇을 기다리는지**를 가르는 일이다. `vmstat`의 r·b·si/so로 줄을 정하고, `mpstat`·`iostat -x`·`pidstat`로 좁히고, PSI로 잃은 시간을 잰다. Linux의 load average는 D 상태를 포함하므로 "load 높음 + CPU 한가"는 I/O(또는 드라이버) 대기의 신호다.

- [ ] 리셋부터 READY까지 구간과 각 구간의 시계(0점)를 표로 설명할 수 있다
- [ ] 시리얼 로그에 호스트 타임스탬프를 붙이는 캡처기를 짤 수 있다
- [ ] U-Boot에 `CONFIG_BOOTSTAGE`를 켜고 `bootstage report`의 Mark/Elapsed를 해석할 수 있다
- [ ] `BOOTSTAGE_FDT`로 넘어온 기록을 `/proc/device-tree/bootstage`에서 읽을 수 있다
- [ ] `initcall_debug` + `log_buf_len`으로 Top N initcall을 뽑고 조치(Kconfig, 모듈, async)를 고를 수 있다
- [ ] `trace_event=initcall:*`와 `ftrace=function_graph ftrace_graph_filter=`로 부팅 중 트레이스를 남기고 읽을 수 있다
- [ ] sync / async / deferred probe의 차이를 드라이버 코드와 측정값으로 설명할 수 있다
- [ ] 모듈 vs 빌트인, Image vs Image.gz, `quiet`의 부팅 영향을 판단 기준과 함께 말할 수 있다
- [ ] `vmstat`의 r, b, si/so, cs, us/sy/wa/st를 설명할 수 있다
- [ ] `iostat -x`의 await, aqu-sz, %util로 저장장치 포화를 판단할 수 있다
- [ ] Linux load average의 정의(R + D)와 "load 높음 + CPU 한가"의 원인을 설명할 수 있다
- [ ] PSI some/full과 트리거를 설명하고 `/proc/pressure/*`를 직접 해석할 수 있다

## 참고 자료

- Linux 커널 문서 — The kernel's command-line parameters (`initcall_debug`, `log_buf_len`, `driver_async_probe`, `trace_event`, `ftrace`, `ftrace_graph_filter`, `trace_buf_size`, `tp_printk`): https://docs.kernel.org/admin-guide/kernel-parameters.html
- Linux 커널 문서 — Boot-time tracing / Boot Configuration: https://docs.kernel.org/trace/boottime-trace.html , https://docs.kernel.org/admin-guide/bootconfig.html
- Linux 커널 문서 — ftrace: https://docs.kernel.org/trace/ftrace.html
- Linux 커널 문서 — PSI - Pressure Stall Information: https://docs.kernel.org/accounting/psi.html
- Linux 커널 문서 — I/O statistics fields (`/proc/diskstats`): https://docs.kernel.org/admin-guide/iostats.html
- 커널 소스(6.18.55): `init/main.c`(`do_one_initcall`), `drivers/base/dd.c`(async·deferred probe), `kernel/module/main.c`(`do_init_module`), `kernel/sched/loadavg.c`, `kernel/sched/psi.c`, `kernel/sched/cputime.c`, `scripts/bootgraph.pl` — https://elixir.bootlin.com/linux/v6.18/source
- U-Boot 문서 — Bootstage: https://docs.u-boot.org/en/latest/develop/bootstage.html , Falcon mode: https://docs.u-boot.org/en/latest/develop/falcon.html
- systemd — systemd-analyze(1): https://www.freedesktop.org/software/systemd/man/latest/systemd-analyze.html , Boot Loader Interface: https://systemd.io/BOOT_LOADER_INTERFACE/
- man7.org — vmstat(8), proc(5)(`/proc/loadavg`, `/proc/stat`, `/proc/pressure`): https://man7.org/linux/man-pages/man5/proc.5.html
- sysstat (iostat, mpstat, pidstat, sar) 문서: https://sysstat.github.io/
- Brendan Gregg — Linux Load Averages: Solving the Mystery: https://www.brendangregg.com/blog/2017-08-08/linux-load-averages.html , USE Method: https://www.brendangregg.com/usemethod.html
- Bootlin — Embedded Linux boot time optimization 교육 자료: https://bootlin.com/training/boot-time/
- 관련 노트: C1, C4, C5, C6, C7, L1, L8, F7, F9, G3, M5, M6, M11, N1~N4
