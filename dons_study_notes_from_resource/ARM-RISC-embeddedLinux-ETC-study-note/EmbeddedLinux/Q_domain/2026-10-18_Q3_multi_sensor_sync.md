# Q3. 멀티 센서 동기화 — HW trigger, 타임스탬프, PPS, PTP/gPTP로 "같은 순간"을 맞춘다

> **이 노트를 다 읽으면**: 센서 퓨전의 시간 오차 예산을 숫자로 계산 · HW trigger/frame sync 배선과 SerDes 동기 전달을 설계 · 타임스탬프가 어디서 찍히고 어떤 시계(`CLOCK_MONOTONIC`/`TAI`/PHC)로 표현되는지 추적 · `ptp4l`/`phc2sys`/`pmc`/`ppstest`로 시간 동기를 세우고 상태를 읽고 고장 낼 수 있다
> **왜 필요한가**: 로봇·드론·차량 센서 키트 면접에서 "카메라와 IMU, LiDAR를 어떻게 시간 정렬하나?", "PTP는 어떻게 오프셋을 계산하나?", "HW timestamping이 왜 필요한가?"는 거의 반드시 나오고, 실무에서는 "퓨전이 가끔 튄다"의 절반이 시간 문제다
> **Don 기준 난이도**: 클럭·카운터·인터럽트 지연·트리거 배선은 이미 익숙(RF 칩 bring-up, SSD 타임스탬프 로그) / Linux의 시계 종류, PHC, linuxptp 도구, servo 상태, 시계 도메인 사이 변환은 새로 배울 부분
> **선행 노트**: F8(시간·타이머, 시계 종류), I2(IIO 타임스탬프), I5(V4L2 타임스탬프·sequence), I6 §9(SO_TIMESTAMPING·PHC), A9(소켓), N3(지연 측정), O3(외부 MCU 타임스탬프) — Q1(카메라 HW), Q5(GNSS·IMU·LiDAR), Q6(ROS 2 타임스탬프), Q7(데이터 품질 툴)과 같이 읽는다

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 1 ms = 2 cm — 시간 오차는 곧 위치 오차다

센서 퓨전 알고리즘(EKF, VIO, LiDAR-camera 투영)은 모든 측정값에 **"이 값은 시각 t의 세상이다"** 라는 꼬리표가 붙어 있다고 가정한다. 꼬리표가 틀리면 알고리즘은 틀린 위치의 세상을 본다. 계산은 단순하다.

- 위치 오차 = 속도 × 시간 오차. 20 m/s(72 km/h) 차량에서 1 ms 오차 → **2 cm**.
- 회전 오차 = 각속도 × 시간 오차. 드론 yaw rate 180 °/s에서 1 ms → 0.18°. 카메라 화각 90°·가로 1920 px면 1 px ≈ 0.047°이니 **약 4 px**가 밀린다.
- LiDAR 점을 카메라 영상에 투영할 때 10 ms 어긋나면, 20 m/s에서 20 cm — 보행자 한 명의 폭이 어긋난다.

그래서 시스템 설계 첫 단계에 **시간 오차 예산(error budget)** 을 잡는다. 예를 들어 센서 키트라면 "카메라-IMU 상대 오차 < 0.5 ms, LiDAR-카메라 < 1 ms, 장비 간 절대 시각 < 10 µs" 같은 식이다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <line x1="40" y1="200" x2="640" y2="200" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="600" y="222" font-size="12">시간 t</text>
  <line x1="200" y1="60" x2="200" y2="200" stroke="#4a7bd0" stroke-dasharray="4 3"/>
  <line x1="320" y1="60" x2="320" y2="200" stroke="#e08a3c" stroke-dasharray="4 3"/>
  <rect x="120" y="30" width="160" height="28" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="49" font-size="13" text-anchor="middle">카메라 노출 (진짜 t0)</text>
  <rect x="250" y="70" width="150" height="28" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="325" y="89" font-size="13" text-anchor="middle">찍힌 타임스탬프 t0+Δ</text>
  <circle cx="200" cy="160" r="9" fill="#3f9a6b"/>
  <circle cx="320" cy="160" r="9" fill="#3f9a6b" fill-opacity="0.4"/>
  <line x1="210" y1="160" x2="308" y2="160" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="260" y="148" font-size="12" text-anchor="middle">v·Δ</text>
  <text x="200" y="190" font-size="12" text-anchor="middle">실제 위치</text>
  <text x="320" y="190" font-size="12" text-anchor="middle">퓨전이 믿는 위치</text>
  <rect x="440" y="120" width="220" height="70" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="550" y="142" font-size="13" text-anchor="middle">Δ = 1 ms, v = 20 m/s</text>
  <text x="550" y="162" font-size="13" text-anchor="middle">→ 오차 2 cm</text>
  <text x="550" y="181" font-size="12" text-anchor="middle">Δ = 10 ms → 20 cm</text>
  <text x="340" y="252" font-size="12" text-anchor="middle">시간 꼬리표가 Δ만큼 틀리면 퓨전은 v·Δ만큼 틀린 장소를 본다</text>
</svg>
```
그림 1 — 시간 오차 Δ는 속도 v를 곱해 그대로 위치 오차가 된다

### 0.2 동기화의 세 층

"동기화"라는 한 단어에 실제로는 세 가지 다른 문제가 섞여 있다. 면접에서 이걸 구분해서 말하면 바로 경험자로 보인다.

| 층 | 질문 | 수단 | 이 노트 |
|---|---|---|---|
| 1. 샘플링 순간을 맞춘다 | 두 카메라가 **같은 순간에** 노출하나? | HW trigger, frame sync(FSIN/XVS), SerDes FSYNC 전달 | §2 |
| 2. 순간에 정확한 꼬리표를 붙인다 | 그 노출이 **언제였는지** 정확히 기록했나? | 하드웨어 타임스탬프, SOF/EOF IRQ, IIO/V4L2 타임스탬프 | §3, §4 |
| 3. 꼬리표의 시계를 맞춘다 | 장비 A의 "t=100.000"과 장비 B의 "t=100.000"이 같은 순간인가? | PPS, PTP/gPTP, GNSS, `phc2sys`, chrony | §5~§9 |

Don의 경험으로 번역하면: 1층은 RF 칩 여러 개에 같은 trigger 라인을 뿌리는 것, 2층은 SSD 펌웨어에서 이벤트 로그에 free-running 타이머 값을 찍는 것, 3층은 여러 칩의 타이머 값을 하나의 기준 시각으로 환산하는 것(예: 호스트 시각과 펌웨어 tick의 대응표)이다.

### 0.3 MCU 개념 ↔ Linux 개념

| bare-metal에서 하던 것 | Linux에서의 이름 | 비고 |
|---|---|---|
| free-running 타이머 카운터 (SysTick, DWT CYCCNT) | clocksource (`arch_sys_counter`), `CLOCK_MONOTONIC_RAW` | F8 §3 |
| 타이머 캡처 입력(input capture)으로 엣지 시각 기록 | PPS 소스, PHC의 extts(external timestamp) 채널 | §5, §8 |
| 타이머 compare 출력으로 펄스 생성 | PHC의 perout(periodic output), PWM | §2, §8 |
| 호스트 시각과 펌웨어 tick 대응표 | `phc2sys`, `PTP_SYS_OFFSET`, servo | §8 |
| 크리스탈 ppm 보정 (trim) | `adjtimex` freq, PHC `adjfine` | §1, §8 |

### 0.4 이 노트에서 직접 해 보는 것 (실행 환경)

- VM 호스트: Lima Ubuntu `elinux` (aarch64, `uname -r` = `7.0.0-38-generic`) — 유저 공간 예제.
- QEMU 게스트: Linux `6.18.55-dom` (공용 랩 Image-dom, TCG 에뮬레이션) + Ubuntu 24.04 rootfs-dom(linuxptp 4.0, pps-tools, ethtool). 게스트 두 대를 `-netdev dgram`(호스트 UDP 127.0.0.1:41501↔41502)으로 직결해 PTP를 돌렸다.
- **주의(TCG)**: 이 VM은 19명이 같이 쓰는 M2 위의 TCG 에뮬레이션이라 지연·지터 숫자는 실제 하드웨어 값이 아니다(수십~수천 배 나쁘다). 이 노트의 숫자는 "메커니즘이 이렇게 동작한다"의 증거이지 성능 기준이 아니다. 실제 SoC 숫자는 "기억 기반" 표시와 함께 따로 적는다.

```sh
# 게스트 부팅 패턴 (두 대: a = 01/41501→41502, b = 02/41502→41501). rootfs는 qcow2 overlay
qemu-img create -q -f qcow2 -F raw -b /var/lab/out/rootfs-dom.ext4 /var/tmp/Q3/q3_a.qcow2
qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1536M -nographic -monitor none \
  -kernel /var/lab/out/Image-dom -drive file=/var/tmp/Q3/q3_a.qcow2,if=virtio,format=qcow2 \
  -netdev dgram,id=p0,local.type=inet,local.host=127.0.0.1,local.port=41501,remote.type=inet,remote.host=127.0.0.1,remote.port=41502 \
  -device virtio-net-pci,netdev=p0,mac=52:54:00:00:35:01 \
  -virtfs local,path=/var/tmp/Q3/share,mount_tag=q3share,security_model=none,readonly=on \
  -serial unix:/var/tmp/Q3/a.sock,server,nowait \
  -append "console=ttyAMA0 root=/dev/vda rw rootwait systemd.default_device_timeout_sec=900"
```

예제 바이너리는 VM에서 `-static`으로 빌드해 9p 공유(`mount -t 9p -o trans=virtio,ro q3share /mnt/s`)로 게스트에 넘겼다. 시리얼 콘솔은 작은 Python 스크립트로 명령을 보내고 출력을 받았다.

---

## 1. 시계부터 고른다 — Linux의 시계 종류

### 1.1 직관

Linux 안에는 하드웨어 카운터 하나(arm64라면 generic timer `CNTVCT_EL0`, F8 §2)에서 파생된 **여러 "관점"의 시계**가 있다. 같은 카운터를 다른 기준점·다른 보정 규칙으로 읽는 것이다. 센서 동기화에서 중요한 질문은 두 가지다: **뒤로 가거나 점프하나?** 그리고 **누가 속도를 보정하나?**

| clockid | 기준점 | 점프? | 속도 보정 | 센서 동기에서의 쓰임 |
|---|---|---|---|---|
| `CLOCK_REALTIME` | 1970-01-01 UTC | **예** (`date -s`, NTP step, PTP step, 윤초) | NTP/PTP가 slew | 로그의 사람용 시각. 센서 간격 계산 금지 |
| `CLOCK_TAI` | REALTIME + TAI 오프셋(현재 37 s) | REALTIME과 함께 점프 | 동일 | PTP 시간축, TSN qdisc(`taprio`/`etf`) |
| `CLOCK_MONOTONIC` | 부팅 시점 근처 | 아니오 | **NTP/PTP가 주파수를 slew** | **센서 타임스탬프 기본값** (V4L2, ROS 2 steady) |
| `CLOCK_MONOTONIC_RAW` | 부팅 | 아니오 | 없음 (카운터 원본) | 크리스털 드리프트 관찰, 보정 전후 비교 |
| `CLOCK_BOOTTIME` | 부팅 | 아니오 | MONOTONIC과 같음 + suspend 시간 포함 | suspend하는 장비(Android 센서 HAL) |
| PHC (`/dev/ptpN`) | 보통 TAI | 설정 가능 | `adjfine`/`adjtime` | NIC의 하드웨어 시계, 패킷 타임스탬프 |

핵심 한 줄: **센서 데이터의 시간축은 `CLOCK_MONOTONIC`, 장비 사이의 공통 시간축은 PTP의 TAI(PHC), 둘 사이의 변환은 `phc2sys`가 맡는다.** 자세한 커널 함수 대응(`ktime_get()` 등)은 F8 §8에 있다.

### 1.2 예제 1 — 모든 시계를 한 번에 읽고 관계 보기

무엇을 확인하는 코드인지: 여섯 시계를 연속으로 읽어 서로의 차이(TAI−UTC, suspend 시간, NTP slew 누적)와 커널의 `adjtimex` 상태를 본다.

```c
/* q3_clocks: read every POSIX clock once, show their relation + kernel TAI offset */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/timex.h>

static long double rd(clockid_t c)
{
	struct timespec t;
	if (clock_gettime(c, &t)) { perror("clock_gettime"); exit(1); }
	return t.tv_sec + t.tv_nsec / 1e9L;
}

int main(void)
{
	struct { const char *n; clockid_t id; } c[] = {
		{ "CLOCK_REALTIME", CLOCK_REALTIME }, { "CLOCK_TAI", CLOCK_TAI },
		{ "CLOCK_MONOTONIC", CLOCK_MONOTONIC }, { "CLOCK_MONOTONIC_RAW", CLOCK_MONOTONIC_RAW },
		{ "CLOCK_BOOTTIME", CLOCK_BOOTTIME }, { "CLOCK_MONOTONIC_COARSE", CLOCK_MONOTONIC_COARSE },
	};
	long double v[6];
	for (int i = 0; i < 6; i++) v[i] = rd(c[i].id);	/* back-to-back reads */
	for (int i = 0; i < 6; i++) {
		struct timespec r; clock_getres(c[i].id, &r);
		printf("%-23s %20.9Lf s  res=%ld ns\n", c[i].n, v[i], r.tv_nsec);
	}
	printf("TAI - REALTIME         = %+.3Lf s\n", v[1] - v[0]);
	printf("BOOTTIME - MONOTONIC   = %+.6Lf s (time spent in suspend)\n", v[4] - v[2]);
	printf("MONOTONIC - MONO_RAW   = %+.6Lf s (accumulated NTP/PTP slewing)\n", v[2] - v[3]);
	struct timex tx = { 0 };
	int st = adjtimex(&tx);
	if (st < 0) { perror("adjtimex"); return 1; }
	printf("adjtimex: state=%d tai=%d freq=%+.3f ppm offset=%ld status=0x%x%s\n",
	       st, tx.tai, tx.freq / 65536.0, tx.offset, tx.status,
	       (tx.status & STA_UNSYNC) ? " (STA_UNSYNC)" : "");
	return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 q3_clocks.c -o q3_clocks && ./q3_clocks      # VM 호스트 (7.0, chrony/timesyncd 동기됨)
```

```text
CLOCK_REALTIME          1792339203.837594656 s  res=1 ns
CLOCK_TAI               1792339240.837596490 s  res=1 ns
CLOCK_MONOTONIC             335646.433101435 s  res=1 ns
CLOCK_MONOTONIC_RAW         335603.984533945 s  res=1 ns
CLOCK_BOOTTIME              335646.433101768 s  res=1 ns
CLOCK_MONOTONIC_COARSE      335646.431571436 s  res=1000000 ns
TAI - REALTIME         = +37.000 s
BOOTTIME - MONOTONIC   = +0.000000 s (time spent in suspend)
MONOTONIC - MONO_RAW   = +42.448567 s (accumulated NTP/PTP slewing)
adjtimex: state=0 tai=37 freq=-0.498 ppm offset=0 status=0x0
```

같은 바이너리(정적 빌드)를 갓 부팅한 QEMU 게스트(6.18.55-dom, 시간 동기 데몬 없음)에서:

```text
CLOCK_REALTIME          1792340322.932443376 s  res=1 ns
CLOCK_TAI               1792340322.933996368 s  res=1 ns
CLOCK_MONOTONIC                 19.284872576 s  res=1 ns
CLOCK_MONOTONIC_RAW             19.284894912 s  res=1 ns
CLOCK_BOOTTIME                  19.284896608 s  res=1 ns
CLOCK_MONOTONIC_COARSE          19.282866240 s  res=4000000 ns
TAI - REALTIME         = +0.002 s
BOOTTIME - MONOTONIC   = +0.000024 s (time spent in suspend)
MONOTONIC - MONO_RAW   = -0.000022 s (accumulated NTP/PTP slewing)
adjtimex: state=5 tai=0 freq=+0.000 ppm offset=0 status=0x40 (STA_UNSYNC)
```

출력에서 볼 것:

- 호스트는 `tai=37`이라 TAI − REALTIME = +37 s다. 게스트는 **`tai=0`** — 아무도 커널에 TAI 오프셋을 알려 주지 않았으니 `CLOCK_TAI`가 REALTIME과 같다(+0.002 s는 TCG에서 두 번 읽는 사이의 시간). PTP를 쓰는 장비에서 `CLOCK_TAI`를 쓰려면 `ptp4l`/`phc2sys`(또는 chrony `leapsectz`)가 이 값을 설정해 줘야 한다. 흔한 함정이다.
- 호스트의 MONOTONIC − MONO_RAW = +42.4 s: 약 3.9일(335,646 s) 동안 NTP가 MONOTONIC의 **속도**를 꾸준히 보정해 온 누적량이다(평균 약 126 ppm, VM이라 크다). MONOTONIC은 점프하지 않지만 **속도는 보정된다**는 증거. 게스트는 보정 주체가 없어 RAW와 같다.
- `state=5`는 `TIME_ERROR`, `STA_UNSYNC`(0x40) — "이 시계는 동기된 적 없음". 센서 키트 health 모니터(Q7)가 이 비트를 보면 된다.
- 게스트 COARSE 해상도 4 ms → 이 커널은 `HZ=250`.
- 값(시각)은 실행마다 다르다.

### 1.3 함정 — 어떤 시계를 쓸지 섞지 마라

- IIO의 기본 타임스탬프 시계는 **`CLOCK_REALTIME`** 이다(I2 §10에서 6.18 소스로 확인). evdev도 기본이 REALTIME. bring-up 체크리스트에 `echo monotonic > /sys/bus/iio/devices/iio:deviceN/current_timestamp_clock`을 넣는다.
- V4L2는 거의 항상 `V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC`(I5 §6). 즉 기본값 그대로 두면 **IMU는 REALTIME, 카메라는 MONOTONIC** — 두 축이 부팅 시각(약 1.79e9 s)만큼 다르다. 정렬 코드가 "IMU 샘플이 하나도 안 맞는다"며 버린다.
- REALTIME을 퓨전에 쓰면 PTP/NTP가 첫 동기 때 수 초를 step 하는 순간 dt가 음수 또는 수 초가 된다.

---

## 2. 1층 — 샘플링 순간을 맞춘다: HW trigger와 frame sync

### 2.1 왜 타임스탬프만으로는 부족한가

카메라 두 대가 각자 30 fps로 free-run 하면, 두 셔터 사이의 위상차는 0~33 ms 사이 아무 값이고 천천히 흘러간다(두 크리스털의 ppm 차이 때문에). 타임스탬프를 완벽하게 찍어도 **두 영상은 다른 순간의 세상**이다. 스테레오 깊이 계산은 같은 순간의 두 영상이 필요하니, 보간으로 해결이 안 된다(영상은 보간할 수 없다). 그래서 **노출 자체를 같은 엣지에 묶는다**.

### 2.2 방식

| 방식 | 동작 | 어디서 보나 |
|---|---|---|
| master/slave 센서 | master 센서가 자기 VSYNC를 출력(OmniVision `FSIN` 계열 핀, Sony `XVS`/`XHS`), slave 센서가 그 신호에 프레임 시작을 맞춘다 | 센서 레지스터 설정(드라이버의 레지스터 테이블). 6.18 `imx274.c`의 `{0x3018, 0xA2}, /* output XVS, HVS */`, `imx283.c`의 `IMX283_SYNCDRV_XHS_XVS` |
| 외부 trigger | MCU/FPGA/SoC가 주기 펄스(GPIO, PWM, PHC perout)를 만들어 모든 센서의 trigger 핀에 뿌린다. 센서는 "trigger mode"(외부 trigger 1회 = 노출 1회) | 산업용·글로벌 셔터 센서. Linux 드라이버에서는 vendor 컨트롤이나 DT 속성으로 켬 (mainline 공통 API는 없다) |
| SerDes FSYNC 전달 | GMSL/FPD-Link 디시리얼라이저가 FSYNC를 만들거나(내부) 외부 입력을 받아, back-channel로 각 시리얼라이저의 GPIO에 전달 → 원격 카메라 센서의 FSIN | 6.18 `max9286.c`의 `MAX9286_FSYNCMODE_*`, `MAX9286_REG_FSYNC_PERIOD_*`, `ds90ub960.c`의 `UB960_SR_FS_CTL`/`FS_HIGH_TIME`/`FS_LOW_TIME` 레지스터 (Q1) |
| IMU sync 입력 | IMU의 FSYNC/SYNC 핀에 카메라 VSYNC를 넣으면 IMU가 그 엣지를 샘플에 플래그로 남기거나 샘플링 순간을 맞춘다 | 칩마다 다르다 (데이터시트 확인. 기억 기반: InvenSense 계열 `FSYNC`, Bosch 계열 data sync 기능) |
| LiDAR 위상 고정 | LiDAR의 회전 위상을 PPS/PTP 시각에 고정(예: "0°가 매 초 정각") → 카메라 trigger를 LiDAR가 그 방향을 볼 때 쏜다 | 기억 기반: Ouster·Hesai 등 다수 LiDAR가 PTP와 PPS+NMEA 입력, 위상 고정 설정을 제공 — 제품 매뉴얼로 확인 필요 |

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="150" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="95" y="44" font-size="13" text-anchor="middle">GNSS 수신기</text>
  <text x="95" y="62" font-size="12" text-anchor="middle">PPS + NMEA/UBX</text>
  <rect x="20" y="130" width="150" height="70" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="95" y="155" font-size="13" text-anchor="middle">MCU / FPGA</text>
  <text x="95" y="173" font-size="12" text-anchor="middle">PPS에 위상 고정한</text>
  <text x="95" y="190" font-size="12" text-anchor="middle">30 Hz trigger 생성</text>
  <rect x="260" y="20" width="150" height="56" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="335" y="44" font-size="13" text-anchor="middle">Linux SoC</text>
  <text x="335" y="62" font-size="12" text-anchor="middle">pps-gpio, PHC</text>
  <rect x="260" y="130" width="150" height="70" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="335" y="155" font-size="13" text-anchor="middle">디시리얼라이저</text>
  <text x="335" y="173" font-size="12" text-anchor="middle">FSYNC 입력 →</text>
  <text x="335" y="190" font-size="12" text-anchor="middle">back-channel 전달</text>
  <rect x="500" y="110" width="160" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="580" y="135" font-size="12" text-anchor="middle">SER → 카메라 1 FSIN</text>
  <rect x="500" y="165" width="160" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="580" y="190" font-size="12" text-anchor="middle">SER → 카메라 2 FSIN</text>
  <rect x="260" y="250" width="150" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="335" y="279" font-size="13" text-anchor="middle">IMU FSYNC 핀</text>
  <rect x="500" y="250" width="160" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="580" y="270" font-size="13" text-anchor="middle">LiDAR</text>
  <text x="580" y="288" font-size="12" text-anchor="middle">PTP 또는 PPS+NMEA</text>
  <line x1="170" y1="48" x2="256" y2="48" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="213" y="40" font-size="12" text-anchor="middle">PPS</text>
  <line x1="95" y1="76" x2="95" y2="126" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="170" y1="165" x2="256" y2="165" stroke="#e08a3c" marker-end="url(#ah2)"/>
  <text x="213" y="157" font-size="12" text-anchor="middle">trigger</text>
  <line x1="410" y1="155" x2="496" y2="132" stroke="#e08a3c" marker-end="url(#ah2)"/>
  <line x1="410" y1="175" x2="496" y2="185" stroke="#e08a3c" marker-end="url(#ah2)"/>
  <polyline points="120,200 120,275 256,275" fill="none" stroke="#e08a3c" marker-end="url(#ah2)"/>
  <line x1="410" y1="40" x2="580" y2="40" stroke="#3f9a6b"/>
  <line x1="580" y1="40" x2="580" y2="106" stroke="#3f9a6b" stroke-dasharray="4 3"/>
  <polyline points="410,60 470,60 470,275 496,275" fill="none" stroke="#3f9a6b" marker-end="url(#ah2)"/>
  <text x="530" y="34" font-size="12">PTP 시각 (이더넷)</text>
  <text x="340" y="322" font-size="12" text-anchor="middle">주황 = 노출 순간을 묶는 trigger, 초록 = 시각(꼬리표)을 맞추는 경로</text>
</svg>
```
그림 2 — 전형적인 trigger 토폴로지: GNSS PPS에 위상 고정한 MCU가 trigger를 만들고, SerDes가 원격 카메라까지 전달한다

### 2.3 Linux SoC가 trigger를 직접 만들 때

- **PWM**(I9): `/sys/class/pwm/pwmchipN/pwm0/{period,duty_cycle,enable}`. 간단하지만 PWM 클럭은 시스템 시각과 묶여 있지 않아 **위상이 시각과 무관**하고 드리프트한다.
- **PHC perout**: NIC의 PTP 하드웨어 시계가 "PTP 시각 정각마다" 펄스를 낸다(`PTP_PEROUT_REQUEST2` ioctl, `testptp -p`, 또는 sysfs `/sys/class/ptp/ptpN/period`). 펄스가 **PTP로 맞춘 시각에 위상 고정**되므로, 여러 장비가 같은 순간에 trigger한다. 차량·로봇에서 가장 깔끔한 방법이다. 핀 할당은 `/sys/class/ptp/ptpN/pins/` 또는 `PTP_PIN_SETFUNC`.
- **GPIO 토글을 유저 공간 루프로**: 하지 마라. 스케줄링 지터(N3에서 측정한 수십 µs~ms)가 그대로 trigger 지터가 된다. 꼭 SoC가 해야 하면 PREEMPT_RT + hrtimer 커널 모듈, 또는 보조 Cortex-M 코어(O1/O3)에 맡긴다.

> 확인 범위: perout sysfs·ioctl 이름은 6.18 `drivers/ptp/ptp_chardev.c`·`ptp_sysfs.c`·`include/uapi/linux/ptp_clock.h`에 있다. 이 랩의 mock PHC는 `n_per_out=0`이라 perout을 실제로 돌려 보지는 못했다(§8 출력).

### 2.4 함정

- trigger 모드 센서에서 trigger 주기가 "노출 + readout"보다 짧으면 프레임이 조용히 건너뛰어진다 → V4L2 `sequence` 틈(I5 §7)으로 잡는다.
- SerDes 링크로 전달된 FSYNC에는 링크 고정 지연(수 µs, 기억 기반)이 있다. 모든 카메라가 같은 SerDes 구성이면 상쇄되지만, 카메라마다 케이블·칩이 다르면 측정해서 보정한다.
- trigger로 노출을 맞춰도 **타임스탬프를 소프트웨어로 찍으면** 2층에서 다시 흔들린다. 1층과 2층은 별개다.

---

## 3. 2층 — 타임스탬프는 어디서 찍히나

### 3.1 파이프라인 위의 다섯 지점

카메라 프레임 하나가 유저 공간에 도착하기까지 타임스탬프를 찍을 수 있는 지점은 여럿이다. **늦게 찍을수록 지연이 크고, 지연이 일정하지 않으니(지터) 보정도 안 된다.**

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="40" width="118" height="60" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="69" y="65" font-size="13" text-anchor="middle">① 센서 내부</text>
  <text x="69" y="84" font-size="12" text-anchor="middle">노출 시작/중심</text>
  <rect x="145" y="40" width="118" height="60" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="204" y="65" font-size="13" text-anchor="middle">② CSI 수신기</text>
  <text x="204" y="84" font-size="12" text-anchor="middle">SOF/EOF HW 캡처</text>
  <rect x="280" y="40" width="118" height="60" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="339" y="65" font-size="13" text-anchor="middle">③ IRQ 핸들러</text>
  <text x="339" y="84" font-size="12" text-anchor="middle">ktime_get_ns()</text>
  <rect x="415" y="40" width="118" height="60" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="474" y="65" font-size="13" text-anchor="middle">④ 스레드/WQ</text>
  <text x="474" y="84" font-size="12" text-anchor="middle">bottom half</text>
  <rect x="550" y="40" width="118" height="60" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="609" y="65" font-size="13" text-anchor="middle">⑤ 유저 공간</text>
  <text x="609" y="84" font-size="12" text-anchor="middle">DQBUF 후 now()</text>
  <line x1="128" y1="70" x2="141" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="263" y1="70" x2="276" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="398" y1="70" x2="411" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="533" y1="70" x2="546" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="69" y="130" font-size="12" text-anchor="middle">지터 ~ns</text>
  <text x="204" y="130" font-size="12" text-anchor="middle">지터 ~ns</text>
  <text x="339" y="130" font-size="12" text-anchor="middle">IRQ 지연 µs</text>
  <text x="474" y="130" font-size="12" text-anchor="middle">스케줄링 µs~ms</text>
  <text x="609" y="130" font-size="12" text-anchor="middle">µs~수십 ms</text>
  <line x1="20" y1="160" x2="660" y2="160" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="340" y="182" font-size="12" text-anchor="middle">오른쪽으로 갈수록 늦고, 흔들리고, 보정 불가</text>
  <rect x="60" y="205" width="560" height="70" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="228" font-size="13" text-anchor="middle">IMU(IIO): FIFO watermark IRQ의 hardirq에서 시각 → 샘플 수·ODR로 역산</text>
  <text x="340" y="248" font-size="13" text-anchor="middle">LiDAR(UDP): 패킷 안 센서 시각(PTP 동기) 또는 NIC HW RX 타임스탬프</text>
  <text x="340" y="267" font-size="12" text-anchor="middle">GNSS: PPS 엣지는 IRQ/캡처로, NMEA 문장은 "어느 초인지"만 알려 준다</text>
</svg>
```
그림 3 — 카메라 프레임 경로 위의 타임스탬프 지점과 각 지점의 지터 규모 (규모는 대략적 감각, 기억 기반)

| 지점 | 예 | 장점 | 단점 |
|---|---|---|---|
| ① 센서 내부 시계 | LiDAR 패킷 안 타임스탬프, 일부 카메라의 frame counter/embedded data | 가장 정확 | 센서 시계를 시스템 시계로 변환해야 함(§5) |
| ② 수신 HW 캡처 | CSI 수신기·ISP가 SOF 순간의 카운터를 레지스터에 래치, NIC HW timestamp | ns급, 지터 없음 | 하드웨어가 지원해야 함 |
| ③ hardirq 진입 | 대부분의 V4L2 드라이버(SOF/EOF IRQ에서 `ktime_get_ns()`), IIO `iio_pollfunc_store_time` | 쉬움, MONOTONIC | IRQ 지연(µs) + IRQ 끄인 구간만큼 흔들림 |
| ④ threaded IRQ / workqueue | I2C 센서를 스레드에서 읽은 뒤 찍기 | — | 스케줄링 지연 그대로 |
| ⑤ 유저 공간 | 앱이 `read()`/`DQBUF` 후 `clock_gettime()` | — | 최악. 버퍼링·스케줄링·GC가 다 들어감 |

### 3.2 V4L2 — SOE인가 EOF인가

V4L2 버퍼의 `flags`에는 시계 종류와 **프레임의 어느 순간인지**가 들어 있다(6.18 `include/uapi/linux/videodev2.h`).

| 플래그 | 값 | 뜻 |
|---|---|---|
| `V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC` | 0x2000 | `CLOCK_MONOTONIC` 기준 |
| `V4L2_BUF_FLAG_TIMESTAMP_COPY` | 0x4000 | m2m: 입력 버퍼 시각을 복사했을 뿐 |
| `V4L2_BUF_FLAG_TSTAMP_SRC_EOF` | 0x00000000 | 프레임 **마지막** 바이트를 받은 순간 (기본값) |
| `V4L2_BUF_FLAG_TSTAMP_SRC_SOE` | 0x00010000 | 노출 시작(Start Of Exposure) 순간 |

6.18 소스에서 실제로 `TSTAMP_SRC_SOE`를 선언하는 드라이버는 `uvc_queue.c`(`V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC | V4L2_BUF_FLAG_TSTAMP_SRC_SOE`)와 vivid뿐이었다(`grep -rln TSTAMP_SRC_SOE drivers/media`). 재미있는 사례: Raspberry Pi 5의 `rp1-cfe.c`는 `cfe_sof_isr()`에서 `node->ts = ktime_get_ns()`로 **프레임 시작**에 찍지만, 큐의 `timestamp_flags`는 `V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC`뿐이라 플래그상으로는 EOF(0)다. **플래그를 믿지 말고 드라이버 코드를 읽어라** — 면접에서 쓸 만한 이야기다.

실무 공식(롤링 셔터, 기억 기반 일반식): 노출 중심 시각 ≈ SOF − 노출 시간 + 노출 시간/2 + 줄 번호 × 라인 시간. SOF가 "첫 줄 readout 시작"이라면 첫 줄 노출은 그보다 노출 시간만큼 먼저 시작했다. 정의는 센서·드라이버마다 다르니 **LED 깜빡임 실험(§11.3)으로 실측**한다.

vivid로 SOE/EOF 차이를 직접 보는 실험은 I5 §6에 있다(이 노트에서는 반복하지 않는다).

### 3.3 IIO, 네트워크

- IIO: 트리거 핸들러의 top half `iio_pollfunc_store_time()`이 `pf->timestamp = iio_get_time_ns()`를 찍는다(I2 §5, §10). FIFO 센서는 watermark IRQ 한 번에 N개 샘플이 오니, 마지막 샘플 시각에서 ODR 주기로 거꾸로 계산하고, 칩 내부 타이머(예: `st_lsm6dsx`의 HW timestamp)를 쓰는 드라이버도 있다(I2 §11).
- 네트워크(LiDAR, PTP): `SO_TIMESTAMPING`으로 SW/HW 타임스탬프를 받는다(A9, I6 §9). HW RX 타임스탬프는 PHC 시간축이고, SW는 REALTIME이다.

---

## 4. 예제 — 늦게 찍은 타임스탬프가 퓨전을 얼마나 망치나

### 4.1 실험 설계

가상 센서 두 개를 스레드로 만든다: IMU 200 Hz, 카메라 30 Hz. 물체가 x(t) = A·sin(2πt)로 움직인다(최대 속도 20 m/s). 각 스레드는 `clock_nanosleep(TIMER_ABSTIME)`으로 "샘플링 순간"(= HW trigger라면 HW 타임스탬프가 줄 값)까지 자고, 깨어난 뒤 `clock_gettime(CLOCK_MONOTONIC)`을 찍는다(= 늦게 찍는 SW 타임스탬프). 그 다음 정렬 프로그램이 각 카메라 프레임 시각에서 IMU 값을 **nearest-neighbor** 또는 **선형 보간**으로 찾아, 진짜 위치와의 차이를 cm로 보고한다.

### 4.2 예제 2 — 두 센서 스트림 생성기

무엇을 확인하는 코드인지: 두 주기 스레드의 "예정 시각"과 "실제로 찍힌 MONOTONIC 시각"을 CSV로 남긴다. 두 값의 차이가 SW 타임스탬프 지연이다.

```c
/* q3_streams: two fake sensors (IMU 200 Hz, camera 30 Hz) on their own threads.
 * true = scheduled sample instant (what a HW timestamp would give)
 * sw   = clock_gettime(CLOCK_MONOTONIC) after wake-up (what a late SW stamp gives)
 * output CSV: stream,true_ns,sw_ns   (stdout) */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

static int64_t now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000000LL + t.tv_nsec; }
static int64_t t0;
static double secs = 3;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
struct sensor { const char *name; int64_t period_ns; };

static void *run(void *arg)
{
	struct sensor *s = arg;
	for (int64_t k = 0; k * s->period_ns < (int64_t)(secs * 1e9); k++) {
		int64_t due = t0 + k * s->period_ns;
		struct timespec ts = { due / 1000000000LL, due % 1000000000LL };
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
		int64_t sw = now();			/* "driver" stamps here */
		pthread_mutex_lock(&mu);
		printf("%s,%lld,%lld\n", s->name, (long long)due, (long long)sw);
		pthread_mutex_unlock(&mu);
	}
	return NULL;
}

int main(int argc, char **argv)
{
	struct sensor imu = { "imu", 5000000 }, cam = { "cam", 33333333 };
	pthread_t a, b;
	if (argc > 1) secs = atof(argv[1]);
	t0 = now() + 10000000;			/* common start 10 ms from now */
	if (pthread_create(&a, NULL, run, &imu) || pthread_create(&b, NULL, run, &cam)) {
		perror("pthread_create"); return 1;
	}
	pthread_join(a, NULL);
	pthread_join(b, NULL);
	return 0;
}
```

### 4.3 예제 3 — 정렬과 오차 측정

무엇을 확인하는 코드인지: HW 스탬프 vs SW 스탬프, 주입한 가우시안 지터(σ), 보정 안 한 고정 오프셋 각각에서 NN과 보간의 위치 오차.

```c
/* q3_align < streams.csv : align camera frames to IMU samples, report error in cm.
 * object moves x(t) = A sin(2 pi t), peak speed 20 m/s.  NN vs linear interpolation,
 * HW (true) vs SW stamps, plus injected gaussian jitter and an uncompensated cam offset. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define N 4096
static double it[N], is[N], ct[N], cs[N], A = 20.0 / (2 * M_PI); static int ni, nc;
static double x(double t) { return A * sin(2 * M_PI * t); }
static double gauss(void) { double u = (rand() + 1.0) / (RAND_MAX + 2.0), v = rand() / (RAND_MAX + 1.0);
	return sqrt(-2 * log(u)) * cos(2 * M_PI * v); }

/* q: camera stamp; st[]: IMU stamps used for lookup; values come from the true instants */
static double lookup(const double *st, double q, int interp)
{
	int k = 1;
	while (k < ni - 1 && st[k] < q) k++;
	double a = st[k - 1], b = st[k], va = x(it[k - 1]), vb = x(it[k]);
	if (!interp) return (q - a < b - q) ? va : vb;
	return va + (vb - va) * (q - a) / (b - a);
}

static void run(const char *label, int sw, double jit_ms, double cam_off_ms)
{
	double s[2] = { 0, 0 }, mx[2] = { 0, 0 }, js[N];
	srand(42);
	for (int i = 0; i < ni; i++) js[i] = (sw ? is[i] : it[i]) + jit_ms * 1e-3 * gauss();
	for (int c = 0; c < nc; c++) {
		double q = (sw ? cs[c] : ct[c]) + jit_ms * 1e-3 * gauss() + cam_off_ms * 1e-3;
		if (q < js[0] || q > js[ni - 1]) continue;
		for (int m = 0; m < 2; m++) {
			double e = (lookup(js, q, m) - x(ct[c])) * 100;	/* cm */
			s[m] += e * e; if (fabs(e) > mx[m]) mx[m] = fabs(e);
		}
	}
	printf("%-28s NN rms %6.2f max %6.2f cm | interp rms %6.2f max %6.2f cm\n", label,
	       sqrt(s[0] / nc), mx[0], sqrt(s[1] / nc), mx[1]);
}

int main(void)
{
	char n[8]; long long t, w; double t00 = -1;
	while (scanf("%7[^,],%lld,%lld\n", n, &t, &w) == 3) {
		if (t00 < 0) t00 = t;
		double a = (t - t00) / 1e9, b = (w - t00) / 1e9;
		if (!strcmp(n, "imu") && ni < N) { it[ni] = a; is[ni++] = b; }
		else if (nc < N) { ct[nc] = a; cs[nc++] = b; }
	}
	double lat = 0;
	for (int i = 0; i < ni; i++) lat += is[i] - it[i];
	printf("imu=%d cam=%d  mean SW stamp latency %.1f us\n", ni, nc, lat / ni * 1e6);
	run("HW stamps", 0, 0, 0);
	run("SW stamps (wake-up)", 1, 0, 0);
	double j[] = { 0.1, 0.5, 1, 2, 5 };
	for (int k = 0; k < 5; k++) { char l[40]; snprintf(l, 40, "HW + jitter %.1f ms", j[k]); run(l, 0, j[k], 0); }
	run("HW + cam offset 1 ms", 0, 0, 1);
	run("HW + cam offset 5 ms", 0, 0, 5);
	return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 q3_streams.c -o q3_streams -lpthread
gcc -std=gnu11 -Wall -Wextra -O2 q3_align.c -o q3_align -lm
./q3_streams 10 > s10.csv && ./q3_align < s10.csv          # VM 호스트 (7.0, 공유 부하 load ≈ 6)
```

```text
imu=2000 cam=301  mean SW stamp latency 639.5 us
HW stamps                    NN rms   1.92 max   3.26 cm | interp rms   0.02 max   0.03 cm
SW stamps (wake-up)          NN rms   5.45 max  79.67 cm | interp rms   5.25 max  82.42 cm
HW + jitter 0.1 ms           NN rms   1.92 max   3.26 cm | interp rms   0.18 max   0.63 cm
HW + jitter 0.5 ms           NN rms   2.10 max   6.53 cm | interp rms   0.89 max   3.23 cm
HW + jitter 1.0 ms           NN rms   2.56 max  10.00 cm | interp rms   1.80 max   6.99 cm
HW + jitter 2.0 ms           NN rms   3.89 max  15.04 cm | interp rms   3.53 max  13.73 cm
HW + jitter 5.0 ms           NN rms   8.59 max  32.56 cm | interp rms   8.63 max  34.25 cm
HW + cam offset 1 ms         NN rms   3.04 max   6.54 cm | interp rms   1.41 max   2.00 cm
HW + cam offset 5 ms         NN rms   7.32 max  12.98 cm | interp rms   7.06 max  10.00 cm
```

같은 실험을 QEMU 게스트(6.18.55-dom, TCG) 안에서 3초:

```text
imu=600 cam=91  mean SW stamp latency 2662.0 us
HW stamps                    NN rms   1.91 max   3.26 cm | interp rms   0.02 max   0.03 cm
SW stamps (wake-up)          NN rms  15.87 max 109.48 cm | interp rms  15.62 max 106.36 cm
```

(게스트 출력의 나머지 줄 — HW 스탬프에 지터·오프셋을 주입한 7줄 — 은 고정 시드 난수와 예정 시각만으로 계산되므로 같은 3초 길이의 호스트 실행과 숫자가 똑같아 생략했다.)

출력에서 볼 것:

- **HW 스탬프 + 보간 = 0.02 cm.** 정확한 꼬리표가 있으면 200 Hz IMU를 30 Hz 카메라 시각으로 보간하는 오차는 거의 0이다. NN은 1.9 cm — IMU 주기 5 ms의 절반(최대 2.5 ms × 20 m/s ≈ 5 cm, sin이라 평균은 작다)만큼 양자화 오차가 생긴다. **보간은 공짜 정확도다.**
- **SW 스탬프는 평균 지연 0.64 ms인데 최대 오차 80 cm.** 평균 지연은 두 스트림에 공통이라 상쇄되지만, 가끔 한쪽만 수십 ms 늦게 깨는 순간(호스트 부하)이 오차를 폭발시킨다. TCG 게스트에서는 평균 2.7 ms, 최대 1.1 m. "평균은 괜찮은데 가끔 튄다" — 실제 장비에서 보는 증상 그대로다.
- **보간의 이득은 지터가 IMU 주기보다 작을 때만 있다.** σ = 2 ms 이상이면 NN과 보간이 같아진다. 지터를 먼저 줄여야 알고리즘이 의미가 있다.
- **고정 오프셋 1 ms → 보간 최대 2.00 cm.** 정확히 "1 ms at 20 m/s = 2 cm"이다. 고정 오프셋은 지터와 달리 **측정해서 빼면** 사라진다(카메라-IMU 캘리브레이션 도구들이 시간 오프셋을 추정 변수로 넣는 이유. 기억 기반: Kalibr의 `timeshift_cam_imu`).

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<line x1="80" y1="240" x2="640" y2="240" stroke="currentColor"/><line x1="80" y1="40" x2="80" y2="240" stroke="currentColor"/>
<line x1="76" y1="240.0" x2="640" y2="240.0" stroke="#888" stroke-opacity="0.3"/><text x="72" y="244.0" font-size="12" text-anchor="end">0</text>
<line x1="76" y1="200.0" x2="640" y2="200.0" stroke="#888" stroke-opacity="0.3"/><text x="72" y="204.0" font-size="12" text-anchor="end">2</text>
<line x1="76" y1="160.0" x2="640" y2="160.0" stroke="#888" stroke-opacity="0.3"/><text x="72" y="164.0" font-size="12" text-anchor="end">4</text>
<line x1="76" y1="120.0" x2="640" y2="120.0" stroke="#888" stroke-opacity="0.3"/><text x="72" y="124.0" font-size="12" text-anchor="end">6</text>
<line x1="76" y1="80.0" x2="640" y2="80.0" stroke="#888" stroke-opacity="0.3"/><text x="72" y="84.0" font-size="12" text-anchor="end">8</text>
<line x1="76" y1="40.0" x2="640" y2="40.0" stroke="#888" stroke-opacity="0.3"/><text x="72" y="44.0" font-size="12" text-anchor="end">10</text>
<rect x="96.7" y="201.6" width="28" height="38.4" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<rect x="128.7" y="239.6" width="28" height="1.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="110.7" y="197.6" font-size="11" text-anchor="middle">1.92</text>
<text x="142.7" y="235.6" font-size="11" text-anchor="middle">0.02</text>
<text x="126.7" y="258" font-size="12" text-anchor="middle">0 ms</text>
<rect x="190.0" y="201.6" width="28" height="38.4" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<rect x="222.0" y="236.4" width="28" height="3.6" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="204.0" y="197.6" font-size="11" text-anchor="middle">1.92</text>
<text x="236.0" y="232.4" font-size="11" text-anchor="middle">0.18</text>
<text x="220.0" y="258" font-size="12" text-anchor="middle">0.1 ms</text>
<rect x="283.3" y="198.0" width="28" height="42.0" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<rect x="315.3" y="222.2" width="28" height="17.8" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="297.3" y="194.0" font-size="11" text-anchor="middle">2.10</text>
<text x="329.3" y="218.2" font-size="11" text-anchor="middle">0.89</text>
<text x="313.3" y="258" font-size="12" text-anchor="middle">0.5 ms</text>
<rect x="376.7" y="188.8" width="28" height="51.2" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<rect x="408.7" y="204.0" width="28" height="36.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="390.7" y="184.8" font-size="11" text-anchor="middle">2.56</text>
<text x="422.7" y="200.0" font-size="11" text-anchor="middle">1.80</text>
<text x="406.7" y="258" font-size="12" text-anchor="middle">1 ms</text>
<rect x="470.0" y="162.2" width="28" height="77.8" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<rect x="502.0" y="169.4" width="28" height="70.6" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="484.0" y="158.2" font-size="11" text-anchor="middle">3.89</text>
<text x="516.0" y="165.4" font-size="11" text-anchor="middle">3.53</text>
<text x="500.0" y="258" font-size="12" text-anchor="middle">2 ms</text>
<rect x="563.3" y="68.2" width="28" height="171.8" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
<rect x="595.3" y="67.4" width="28" height="172.6" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="577.3" y="64.2" font-size="11" text-anchor="middle">8.59</text>
<text x="609.3" y="63.4" font-size="11" text-anchor="middle">8.63</text>
<text x="593.3" y="258" font-size="12" text-anchor="middle">5 ms</text>
<text x="360.0" y="280" font-size="12" text-anchor="middle">타임스탬프 지터 σ (양쪽 스트림에 주입, HW 스탬프 기준)</text>
<text x="18" y="140.0" font-size="12" text-anchor="middle" transform="rotate(-90 18 140.0)">RMS 위치 오차 (cm)</text>
<rect x="100" y="14" width="12" height="10" fill="#e08a3c" fill-opacity="0.7"/><text x="118" y="24" font-size="12">nearest-neighbor</text>
<rect x="250" y="14" width="12" height="10" fill="#4a7bd0" fill-opacity="0.7"/><text x="268" y="24" font-size="12">linear interpolation</text>
</svg>
```
그림 4 — 타임스탬프 지터(σ) vs 카메라-IMU 정렬 오차 (VM 호스트에서 직접 측정, 10초, 20 m/s 최대 속도)

### 4.4 교훈 → 설계 규칙

1. 타임스탬프는 **가능한 한 앞(그림 3의 ①~③)** 에서 찍는다.
2. 정렬은 보간으로. 단, 지터 < 샘플 주기일 때만 의미가 있다.
3. 고정 지연은 측정하고 뺀다(LED/IMU 충격 실험, 캘리브레이션 도구).
4. 평균이 아니라 **최대값·p99**를 본다(Q7의 품질 지표).

---

## 5. 3층 — 시계 도메인과 변환

### 5.1 한 시스템 안에도 시계가 여럿이다

센서 키트 하나에 독립된 발진기가 몇 개 있는지 세어 보면: SoC 카운터(→ `CLOCK_MONOTONIC`), NIC의 PHC(별도 크리스털 또는 같은 PLL), LiDAR 내부 시계, IMU 내부 ODR 발진기, 카메라 센서 픽셀 클럭, GNSS 수신기의 원자시 기반 시계. 각자 수~수십 ppm씩 다르게 흐른다. 20 ppm 차이면 **1초에 20 µs, 1분에 1.2 ms** 벌어진다 — 몇 분이면 오차 예산을 넘는다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="170" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="105" y="54" font-size="13" text-anchor="middle">GNSS (UTC/GPS)</text>
  <text x="105" y="72" font-size="12" text-anchor="middle">PPS + NMEA</text>
  <rect x="20" y="130" width="170" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="105" y="154" font-size="13" text-anchor="middle">네트워크 GM</text>
  <text x="105" y="172" font-size="12" text-anchor="middle">PTP grandmaster</text>
  <rect x="255" y="80" width="170" height="70" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="106" font-size="13" text-anchor="middle">PHC /dev/ptp0</text>
  <text x="340" y="125" font-size="12" text-anchor="middle">NIC 하드웨어 시계</text>
  <text x="340" y="142" font-size="12" text-anchor="middle">(TAI)</text>
  <rect x="490" y="80" width="170" height="70" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="575" y="106" font-size="13" text-anchor="middle">시스템 시계</text>
  <text x="575" y="125" font-size="12" text-anchor="middle">REALTIME(UTC)</text>
  <text x="575" y="142" font-size="12" text-anchor="middle">MONOTONIC (같은 속도)</text>
  <rect x="255" y="230" width="170" height="60" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="255" font-size="13" text-anchor="middle">LiDAR / 원격 센서</text>
  <text x="340" y="274" font-size="12" text-anchor="middle">PTP slave (자기 시계)</text>
  <rect x="490" y="230" width="170" height="60" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="575" y="255" font-size="13" text-anchor="middle">카메라·IMU 타임스탬프</text>
  <text x="575" y="274" font-size="12" text-anchor="middle">MONOTONIC</text>
  <line x1="190" y1="58" x2="251" y2="95" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="222" y="62" font-size="12" text-anchor="middle">ts2phc</text>
  <line x1="190" y1="158" x2="251" y2="130" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="222" y="168" font-size="12" text-anchor="middle">ptp4l</text>
  <line x1="425" y1="115" x2="486" y2="115" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="455" y="107" font-size="12" text-anchor="middle">phc2sys</text>
  <line x1="340" y1="150" x2="340" y2="226" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="300" y="195" font-size="12" text-anchor="middle">ptp4l (master)</text>
  <line x1="575" y1="150" x2="575" y2="226" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="620" y="195" font-size="12" text-anchor="middle">같은 커널</text>
  <line x1="190" y1="70" x2="486" y2="88" stroke="#888" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
  <text x="400" y="60" font-size="12" text-anchor="middle">chrony + gpsd (PHC 없을 때)</text>
  <text x="340" y="318" font-size="12" text-anchor="middle">PHC = TAI, 시스템 = UTC: phc2sys가 37 s(UTC 오프셋)를 빼서 맞춘다</text>
</svg>
```
그림 5 — 시계 도메인: 외부 기준 → PHC → 시스템 시계 → 센서 타임스탬프. 화살표 위가 그 변환을 맡는 프로그램

### 5.2 변환의 원리 — "같은 순간에 두 시계를 읽는다"

두 시계 A, B 사이를 변환하려면 **같은 순간의 (A값, B값) 쌍**이 필요하다. 그런데 두 시계를 동시에 읽을 수는 없으니 **샌드위치**로 읽는다: B를 읽고(b1) → A를 읽고(a) → B를 다시 읽는다(b2). 그러면 a는 대략 (b1+b2)/2 순간의 값이고, 불확실성은 (b2−b1)/2다. 여러 번 해서 **창(window)이 가장 짧은 샘플**을 쓴다. `phc2sys`가 PHC와 시스템 시계 사이에서 하는 일이 정확히 이것이고(§8의 예제 7), PTP가 네트워크 너머 두 시계 사이에서 하는 일도 같은 생각을 패킷으로 한 것이다(§6).

하드웨어가 도와주는 방법도 있다(기억 기반·커널 API는 6.18에서 확인): `PTP_SYS_OFFSET_PRECISE`(`getcrosststamp`) — Intel의 ART(Always Running Timer) 같은 공통 카운터로 PHC와 시스템 카운터를 **한 번에** 래치한다. 창이 0이다.

---

## 6. PTP / IEEE 1588 — 패킷으로 두 시계를 맞춘다

### 6.1 메시지 교환과 공식

PTP(Precision Time Protocol)의 기본 아이디어: master가 자기 시각을 보내고, 왕복 시간으로 선로 지연을 재서 뺀다. 네 개의 타임스탬프 t1~t4가 핵심이다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="140" y="24" font-size="13" text-anchor="middle">master (기준 시계)</text>
  <text x="440" y="24" font-size="13" text-anchor="middle">slave (맞출 시계)</text>
  <line x1="140" y1="34" x2="140" y2="300" stroke="currentColor"/>
  <line x1="440" y1="34" x2="440" y2="300" stroke="currentColor"/>
  <line x1="140" y1="60" x2="436" y2="100" stroke="#4a7bd0" marker-end="url(#ah5)"/>
  <text x="290" y="70" font-size="12" text-anchor="middle">Sync</text>
  <text x="130" y="64" font-size="12" text-anchor="end">t1</text>
  <text x="450" y="104" font-size="12">t2</text>
  <line x1="140" y1="110" x2="436" y2="150" stroke="#4a7bd0" stroke-dasharray="5 3" marker-end="url(#ah5)"/>
  <text x="290" y="120" font-size="12" text-anchor="middle">Follow_Up (t1 값을 실어 보냄)</text>
  <line x1="440" y1="180" x2="144" y2="220" stroke="#e08a3c" marker-end="url(#ah5)"/>
  <text x="290" y="190" font-size="12" text-anchor="middle">Delay_Req</text>
  <text x="450" y="184" font-size="12">t3</text>
  <text x="130" y="224" font-size="12" text-anchor="end">t4</text>
  <line x1="140" y1="235" x2="436" y2="275" stroke="#e08a3c" stroke-dasharray="5 3" marker-end="url(#ah5)"/>
  <text x="290" y="245" font-size="12" text-anchor="middle">Delay_Resp (t4 값)</text>
  <rect x="480" y="60" width="190" height="170" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="575" y="84" font-size="12" text-anchor="middle">t2 − t1 = d + θ</text>
  <text x="575" y="104" font-size="12" text-anchor="middle">t4 − t3 = d − θ</text>
  <text x="575" y="130" font-size="12" text-anchor="middle">θ (offset) =</text>
  <text x="575" y="148" font-size="12" text-anchor="middle">((t2−t1) − (t4−t3)) / 2</text>
  <text x="575" y="174" font-size="12" text-anchor="middle">d (meanPathDelay) =</text>
  <text x="575" y="192" font-size="12" text-anchor="middle">((t2−t1) + (t4−t3)) / 2</text>
  <text x="575" y="216" font-size="12" text-anchor="middle">가정: 두 방향 지연이 같다</text>
  <text x="340" y="326" font-size="12" text-anchor="middle">t1, t4는 master 시계로, t2, t3는 slave 시계로 찍힌다 (θ = slave − master)</text>
  <text x="340" y="348" font-size="12" text-anchor="middle">two-step: t1을 Sync에 못 실으니 Follow_Up으로 나중에 보낸다</text>
</svg>
```
그림 6 — PTP delay request-response(E2E) 교환과 offset·delay 공식

### 6.2 예제 4 — 공식을 손으로 돌려 보기 (그리고 비대칭의 대가)

무엇을 확인하는 코드인지: 진짜 오프셋 1500 ns를 두고, 두 방향 선로 지연이 같을 때와 다를 때 PTP 공식이 얼마나 틀리는지.

```c
/* q3_ptpcalc: the IEEE 1588 delay request-response math, and what asymmetry does to it */
#include <stdio.h>
#include <stdlib.h>

/* true slave clock = master clock + true_off.  d_ms: master->slave wire delay, d_sm: back */
static void exchange(double true_off, double d_ms, double d_sm)
{
	double t1 = 1000.0;			/* master sends Sync      (master clock) */
	double t2 = t1 + d_ms + true_off;	/* slave receives Sync    (slave clock)  */
	double t3 = t2 + 50000.0;		/* slave sends Delay_Req  (slave clock)  */
	double t4 = t3 - true_off + d_sm;	/* master receives it     (master clock) */
	double off = ((t2 - t1) - (t4 - t3)) / 2;
	double mpd = ((t2 - t1) + (t4 - t3)) / 2;
	printf("d_ms=%6.0f d_sm=%6.0f -> meanPathDelay=%7.1f offset=%8.1f  (true %6.0f, error %+7.1f ns)\n",
	       d_ms, d_sm, mpd, off, true_off, off - true_off);
}

int main(int argc, char **argv)
{
	double off = argc > 1 ? atof(argv[1]) : 1500.0;	/* ns */
	exchange(off, 1000, 1000);	/* symmetric link: exact            */
	exchange(off, 1000, 1400);	/* 400 ns asymmetry -> 200 ns error */
	exchange(off, 1000, 3000);	/* e.g. queueing in one direction   */
	exchange(off, 5000, 1000);	/* SW timestamp latency on RX path  */
	return 0;
}
```

```text
d_ms=  1000 d_sm=  1000 -> meanPathDelay= 1000.0 offset=  1500.0  (true   1500, error    +0.0 ns)
d_ms=  1000 d_sm=  1400 -> meanPathDelay= 1200.0 offset=  1300.0  (true   1500, error  -200.0 ns)
d_ms=  1000 d_sm=  3000 -> meanPathDelay= 2000.0 offset=   500.0  (true   1500, error -1000.0 ns)
d_ms=  5000 d_sm=  1000 -> meanPathDelay= 3000.0 offset=  3500.0  (true   1500, error +2000.0 ns)
```

출력에서 볼 것: 오프셋 오차 = **(d_ms − d_sm) / 2**. 대칭이면 정확하고, 비대칭 400 ns면 200 ns 틀린다. PTP는 비대칭을 **측정할 수 없다** — 방정식 두 개에 미지수 세 개(θ, d_ms, d_sm)이기 때문이다. 그래서 정확도는 (1) 타임스탬프를 선로에 가까이(HW) 찍어 비대칭 원인(큐, 소프트웨어 지연)을 없애고, (2) 남는 고정 비대칭(PHY 지연, 케이블)은 설정값(linuxptp의 `delayAsymmetry`, `egressLatency`/`ingressLatency`)으로 보정하는 데서 나온다.

### 6.3 PTP 구성 요소

| 개념 | 뜻 |
|---|---|
| Grandmaster(GM) | 도메인 전체의 기준 시계. 보통 GNSS를 받는 장비 |
| Ordinary Clock(OC) | 포트 1개. master 또는 slave |
| Boundary Clock(BC) | 포트 여러 개. 위에서 slave로 맞추고 아래에 master로 다시 내보냄 (스위치) |
| Transparent Clock(TC) | 패킷이 스위치 안에 머문 시간(residence time)을 `correctionField`에 더해 줌 |
| BMCA | Best Master Clock Algorithm. Announce 메시지의 priority1 → clockClass → clockAccuracy → offsetScaledLogVariance → priority2 → clockIdentity 순으로 비교해 GM을 뽑는다 |
| one-step / two-step | Sync 안에 t1을 즉석으로 넣으면 one-step(HW가 송신 순간 패킷 수정), 못 하면 Follow_Up으로 나중에 (two-step) |
| E2E / P2P | delay request-response(종단 간) vs peer delay(이웃 링크마다 Pdelay_Req/Resp로 측정) |
| 전송 | UDPv4/IPv6(이벤트 포트 319, 일반 포트 320, 멀티캐스트 224.0.1.129) 또는 L2 이더넷(EtherType 0x88F7) |

### 6.4 gPTP / IEEE 802.1AS — 자동차·TSN 프로파일

gPTP는 PTP의 한 프로파일로, 차량 이더넷·TSN에서 표준이다. 차이점(기억 기반 요약, 표준 문서로 확인 권장):

- **L2 전용**, **peer delay(P2P) 필수**, 모든 브리지가 time-aware(BC 비슷하게 동작).
- two-step이 기본, 링크마다 neighborRateRatio(이웃과의 주파수 비)를 계산해 누적한다.
- 목표 정확도: 7홉 이내에서 GM 대비 1 µs 이하.
- linuxptp에서는 `ptp4l -f /usr/share/doc/linuxptp/configs/gPTP.cfg`(배포판마다 경로 다름) 같은 프로파일 설정 파일로 켠다. 이 랩은 SW 타임스탬프라 gPTP를 돌리지 않았다(가이드).

### 6.5 HW vs SW 타임스탬핑 — 정확도를 결정하는 단 하나

| | SW (`ptp4l -S`) | HW (`ptp4l -H`) |
|---|---|---|
| 어디서 | 커널 네트워크 스택(I6 §9: TX는 `skb_tx_timestamp()`, RX는 NAPI 경로) | MAC/PHY가 SFD 순간 PHC를 래치 |
| 시간축 | `CLOCK_REALTIME` | PHC |
| ptp4l이 맞추는 시계 | 시스템 시계 직접 | PHC → `phc2sys`가 시스템 시계를 PHC에 |
| 흔들림 원인 | IRQ 지연, coalescing, 스케줄링, qdisc | 거의 없음 (PHY 고정 지연, 크리스털 잡음) |
| 정확도 감각 | 실제 HW에서 수~수십 µs (기억 기반) | 수십 ns 이하 (기억 기반: i210·stmmac급 NIC에서 흔히 보고되는 값) |
| 확인 | `ethtool -T`의 `software-transmit/receive` | `hardware-transmit/receive`, `PTP Hardware Clock: N` |

---

## 7. 실습 — QEMU 게스트 두 대 사이에 PTP (SW 타임스탬핑)

### 7.1 구성

게스트 a(10.35.0.1)를 master, 게스트 b(10.35.0.2)를 slave로 두고 `-netdev dgram`으로 직결했다. virtio-net은 하드웨어 타임스탬프가 없다.

```sh
# 게스트 b
ethtool -T enp0s1
```

```text
Time stamping parameters for enp0s1:
Capabilities:
	software-transmit
	software-receive
	software-system-clock
PTP Hardware Clock: none
Hardware Transmit Timestamp Modes: none
Hardware Receive Filter Modes: none
```

`PTP Hardware Clock: none` — 그러니 `-S`(software timestamping)밖에 선택지가 없다. 연결 확인:

```text
root@labdom:~# ping -c 3 10.35.0.2
64 bytes from 10.35.0.2: icmp_seq=1 ttl=64 time=15.6 ms
64 bytes from 10.35.0.2: icmp_seq=2 ttl=64 time=2.84 ms
64 bytes from 10.35.0.2: icmp_seq=3 ttl=64 time=3.11 ms
```

RTT 3 ms — TCG 에뮬레이션 두 대가 호스트 UDP 루프백을 건너는 비용이다(실제 1 GbE 직결은 수십 µs 이하). 이 숫자가 아래 PTP 정확도의 바닥을 정한다.

### 7.2 예제 5 — master와 slave 띄우기

일부러 slave 시계를 망가뜨려 놓고 시작한다: 주파수 +50 ppm, 시각 +0.3 s. `phc_ctl`은 PHC용 도구지만 `CLOCK_REALTIME`에도 쓸 수 있다.

```sh
# 게스트 a (master): priority1을 기본 128보다 낮춰 BMCA에서 이기게 한다
ptp4l -i enp0s1 -S -m --priority1=10 > /tmp/m.log 2>&1 &
# 게스트 b (slave only)
phc_ctl CLOCK_REALTIME freq 50000 adj 0.3
ptp4l -i enp0s1 -S -m -s > /tmp/s.log 2>&1 &
```

master 로그:

```text
ptp4l[1351.880]: port 1 (enp0s1): INITIALIZING to LISTENING on INIT_COMPLETE
ptp4l[1351.895]: port 0 (/var/run/ptp4l): INITIALIZING to LISTENING on INIT_COMPLETE
ptp4l[1351.913]: port 0 (/var/run/ptp4lro): INITIALIZING to LISTENING on INIT_COMPLETE
ptp4l[1359.562]: port 1 (enp0s1): LISTENING to MASTER on ANNOUNCE_RECEIPT_TIMEOUT_EXPIRES
ptp4l[1359.568]: selected local clock 525400.fffe.003501 as best master
ptp4l[1359.569]: port 1 (enp0s1): assuming the grand master role
```

slave 로그 (앞부분, s0 줄 일부는 `...`로 생략):

```text
phc_ctl[1592.578]: adjusted clock frequency offset to 50000.000000ppb
phc_ctl[1592.588]: adjusted clock by 0.300000 seconds
ptp4l[1592.863]: port 1 (enp0s1): INITIALIZING to LISTENING on INIT_COMPLETE
ptp4l[1593.258]: port 1 (enp0s1): new foreign master 525400.fffe.003501-1
ptp4l[1597.262]: selected best master clock 525400.fffe.003501
ptp4l[1597.266]: foreign master not using PTP timescale
ptp4l[1597.268]: port 1 (enp0s1): LISTENING to UNCALIBRATED on RS_SLAVE
ptp4l[1599.461]: master offset  300422905 s0 freq  -50000 path delay   1776544
ptp4l[1600.466]: master offset  299652455 s0 freq  -50000 path delay   1776544
...
ptp4l[1614.500]: master offset  300064565 s0 freq  -50000 path delay   1466710
ptp4l[1615.513]: master offset  300488801 s1 freq  -45890 path delay   1436480
ptp4l[1616.501]: master offset    -101539 s2 freq  -56146 path delay   1436480
ptp4l[1616.503]: port 1 (enp0s1): UNCALIBRATED to SLAVE on MASTER_CLOCK_SELECTED
ptp4l[1617.502]: master offset    -291540 s2 freq  -75437 path delay   1436480
ptp4l[1618.505]: master offset    1431445 s2 freq  +98293 path delay   1261487
ptp4l[1619.508]: master offset    1796205 s2 freq +136565 path delay   1261487
ptp4l[1620.507]: master offset     562260 s2 freq  +13733 path delay   1261487
```

한 줄 읽는 법: `master offset`(ns) = slave − master 추정값(§6의 θ), `s0/s1/s2` = servo 상태, `freq`(ppb) = servo가 건 주파수 보정, `path delay`(ns) = meanPathDelay.

| 단계 | 로그 | 의미 |
|---|---|---|
| 포트 상태 | INITIALIZING → LISTENING → UNCALIBRATED → SLAVE | Announce를 듣고(LISTENING), BMCA로 master를 고르고, servo가 잠기면 SLAVE |
| `s0` (unlocked) | offset ≈ +300 ms가 16초간 그대로 | servo가 주파수 차를 추정하는 중. 아직 시계를 건드리지 않는다 |
| `s1` (step) | offset 300488801 → 다음 줄 −101539 | `first_step_threshold`(기본 20 µs)를 넘으므로 **한 번 step**(`clock_settime`)해서 0.3 s를 단번에 없앴다 |
| `s2` (locked) | offset이 ±수 ms에서 흔들림 | PI servo가 주파수만 조정(slew)한다. 이후에는 기본 설정에서 step하지 않는다(`step_threshold` 0) |

출력에서 볼 것:

- **freq −50000**: 우리가 `phc_ctl`로 건 +50000 ppb를 ptp4l은 **부호를 반대로** 표시한다(ptp4l은 시작할 때 현재 커널 주파수 보정값을 읽어 servo 초기값으로 쓰고, 로그의 `freq`는 커널 `adjtimex` freq와 부호가 반대다 — 이 실행에서 관찰한 사실). 우리가 일부러 넣은 50 ppm 오차를 servo가 "현재 상태"로 이어받은 것이다.
- **path delay ≈ 1.2~1.8 ms**: ping RTT의 절반 정도. SW 타임스탬프가 TCG 게스트 두 대의 네트워크 스택 처리 시간을 다 포함한다.
- **foreign master not using PTP timescale**: master가 PTP(TAI) 시간축 플래그를 광고하지 않는다는 경고. SW 모드 master는 시스템 시계(UTC)를 그대로 쓰기 때문이다(linuxptp 동작에 대한 내 해석). 실제 GM은 GNSS 기반 TAI를 광고한다.

### 7.3 예제 6 — `pmc`로 상태 읽기

`pmc`(PTP management client)는 ptp4l에 UDS(`/var/run/ptp4l`)로 관리 메시지를 보낸다. 모니터링 스크립트·health check가 쓰는 공식 창구다.

```sh
pmc -u -b 0 'GET TIME_STATUS_NP' 'GET CURRENT_DATA_SET' 'GET PORT_DATA_SET' 'GET PARENT_DATA_SET'
```

```text
	525400.fffe.003502-0 seq 0 RESPONSE MANAGEMENT TIME_STATUS_NP 
		master_offset              302198
		ingress_time               1792341962823459553
		cumulativeScaledRateOffset +0.000000000
		scaledLastGmPhaseChange    0
		gmTimeBaseIndicator        0
		lastGmPhaseChange          0x0000'0000000000000000.0000
		gmPresent                  true
		gmIdentity                 525400.fffe.003501
	525400.fffe.003502-0 seq 1 RESPONSE MANAGEMENT CURRENT_DATA_SET 
		stepsRemoved     1
		offsetFromMaster 302198.0
		meanPathDelay    1273597.0
	525400.fffe.003502-1 seq 2 RESPONSE MANAGEMENT PORT_DATA_SET 
		portIdentity            525400.fffe.003502-1
		portState               SLAVE
		logMinDelayReqInterval  0
		peerMeanPathDelay       0
		logAnnounceInterval     1
		announceReceiptTimeout  3
		logSyncInterval         0
		delayMechanism          1
		logMinPdelayReqInterval 0
		versionNumber           2
	525400.fffe.003502-0 seq 3 RESPONSE MANAGEMENT PARENT_DATA_SET 
		parentPortIdentity                    525400.fffe.003501-1
		parentStats                           0
		observedParentOffsetScaledLogVariance 0xffff
		observedParentClockPhaseChangeRate    0x7fffffff
		grandmasterPriority1                  10
		gm.ClockClass                         248
		gm.ClockAccuracy                      0xfe
		gm.OffsetScaledLogVariance            0xffff
		grandmasterPriority2                  128
		grandmasterIdentity                   525400.fffe.003501
```

(이 출력은 §7.5의 첫 시도 때 받은 것이다. 깨끗한 재실행의 `TIME_STATUS_NP`/`CURRENT_DATA_SET`도 같은 모양이고 값만 `master_offset -1665247`, `meanPathDelay 1391661.0`이었다.)

출력에서 볼 것:

- `gmPresent true`, `portState SLAVE`, `stepsRemoved 1`(GM과 직결) — health check는 이 세 값과 `|master_offset|`의 임계값을 보면 된다.
- clockIdentity `525400.fffe.003501` = MAC `52:54:00:00:35:01`의 가운데에 `fffe`를 끼운 EUI-64.
- `logSyncInterval 0` = 2^0 = 1초마다 Sync, `logAnnounceInterval 1` = 2초마다 Announce, `delayMechanism 1` = E2E.
- `gm.ClockClass 248` = "기본값, 외부 기준 없음". GNSS에 잠긴 GM은 6을 광고한다(IEEE 1588 clockClass 값, 기억 기반). BMCA는 priority1을 먼저 보므로 여기서는 priority1=10이 이겼다.

### 7.4 수렴과 통계

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="250" x2="640" y2="250" stroke="currentColor"/>
<line x1="70" y1="40" x2="70" y2="250" stroke="currentColor"/>
<line x1="66" y1="250.0" x2="640" y2="250.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="254.0" font-size="12" text-anchor="end">-12</text>
<line x1="66" y1="197.5" x2="640" y2="197.5" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="201.5" font-size="12" text-anchor="end">-6</text>
<line x1="66" y1="145.0" x2="640" y2="145.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="149.0" font-size="12" text-anchor="end">+0</text>
<line x1="66" y1="92.5" x2="640" y2="92.5" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="96.5" font-size="12" text-anchor="end">+6</text>
<line x1="66" y1="40.0" x2="640" y2="40.0" stroke="#888" stroke-opacity="0.3"/>
<text x="62" y="44.0" font-size="12" text-anchor="end">+12</text>
<text x="70.0" y="268" font-size="12" text-anchor="middle">0</text>
<text x="184.0" y="268" font-size="12" text-anchor="middle">30</text>
<text x="298.0" y="268" font-size="12" text-anchor="middle">60</text>
<text x="412.0" y="268" font-size="12" text-anchor="middle">90</text>
<text x="526.0" y="268" font-size="12" text-anchor="middle">120</text>
<text x="640.0" y="268" font-size="12" text-anchor="middle">150</text>
<text x="355.0" y="290" font-size="12" text-anchor="middle">ptp4l 시작 후 시간 (s)</text>
<text x="16" y="145.0" font-size="12" text-anchor="middle" transform="rotate(-90 16 145.0)">master offset (ms)</text>
<circle cx="70.0" cy="40.0" r="2.5" fill="#d0564a"/><circle cx="73.8" cy="40.0" r="2.5" fill="#d0564a"/><circle cx="77.6" cy="40.0" r="2.5" fill="#d0564a"/><circle cx="81.4" cy="40.0" r="2.5" fill="#d0564a"/><circle cx="85.2" cy="40.0" r="2.5" fill="#d0564a"/><circle cx="89.0" cy="40.0" r="2.5" fill="#d0564a"/><circle cx="92.8" cy="40.0" r="2.5" fill="#d0564a"/><circle cx="96.6" cy="40.0" r="2.5" fill="#d0564a"/>
<circle cx="100.4" cy="40.0" r="2.5" fill="#d0564a"/><circle cx="104.2" cy="40.0" r="2.5" fill="#d0564a"/><circle cx="108.1" cy="40.0" r="2.5" fill="#d0564a"/><circle cx="111.9" cy="40.0" r="2.5" fill="#d0564a"/><circle cx="115.7" cy="40.0" r="2.5" fill="#d0564a"/><circle cx="119.5" cy="40.0" r="2.5" fill="#d0564a"/><circle cx="123.3" cy="40.0" r="2.5" fill="#d0564a"/><circle cx="127.1" cy="40.0" r="2.5" fill="#d0564a"/>
<circle cx="131.0" cy="40.0" r="2.5" fill="#e08a3c"/><circle cx="134.8" cy="145.9" r="2.5" fill="#4a7bd0"/><circle cx="138.6" cy="147.6" r="2.5" fill="#4a7bd0"/><circle cx="142.4" cy="132.5" r="2.5" fill="#4a7bd0"/><circle cx="146.2" cy="129.3" r="2.5" fill="#4a7bd0"/><circle cx="150.0" cy="140.1" r="2.5" fill="#4a7bd0"/><circle cx="153.8" cy="131.6" r="2.5" fill="#4a7bd0"/><circle cx="157.6" cy="138.3" r="2.5" fill="#4a7bd0"/>
<circle cx="161.4" cy="147.6" r="2.5" fill="#4a7bd0"/><circle cx="165.2" cy="146.0" r="2.5" fill="#4a7bd0"/><circle cx="169.0" cy="148.8" r="2.5" fill="#4a7bd0"/><circle cx="172.8" cy="137.9" r="2.5" fill="#4a7bd0"/><circle cx="176.6" cy="150.8" r="2.5" fill="#4a7bd0"/><circle cx="180.4" cy="145.1" r="2.5" fill="#4a7bd0"/><circle cx="184.2" cy="150.5" r="2.5" fill="#4a7bd0"/><circle cx="188.0" cy="143.4" r="2.5" fill="#4a7bd0"/>
<circle cx="191.8" cy="120.9" r="2.5" fill="#4a7bd0"/><circle cx="195.6" cy="145.2" r="2.5" fill="#4a7bd0"/><circle cx="199.4" cy="138.5" r="2.5" fill="#4a7bd0"/><circle cx="203.3" cy="134.1" r="2.5" fill="#4a7bd0"/><circle cx="207.1" cy="125.2" r="2.5" fill="#4a7bd0"/><circle cx="210.9" cy="114.6" r="2.5" fill="#4a7bd0"/><circle cx="214.7" cy="136.7" r="2.5" fill="#4a7bd0"/><circle cx="218.5" cy="121.0" r="2.5" fill="#4a7bd0"/>
<circle cx="222.3" cy="147.3" r="2.5" fill="#4a7bd0"/><circle cx="226.1" cy="152.6" r="2.5" fill="#4a7bd0"/><circle cx="229.9" cy="145.5" r="2.5" fill="#4a7bd0"/><circle cx="233.7" cy="141.6" r="2.5" fill="#4a7bd0"/><circle cx="237.5" cy="152.2" r="2.5" fill="#4a7bd0"/><circle cx="241.3" cy="144.7" r="2.5" fill="#4a7bd0"/><circle cx="245.1" cy="149.5" r="2.5" fill="#4a7bd0"/><circle cx="249.0" cy="141.5" r="2.5" fill="#4a7bd0"/>
<circle cx="252.8" cy="141.7" r="2.5" fill="#4a7bd0"/><circle cx="256.6" cy="128.9" r="2.5" fill="#4a7bd0"/><circle cx="260.4" cy="142.0" r="2.5" fill="#4a7bd0"/><circle cx="264.2" cy="151.2" r="2.5" fill="#4a7bd0"/><circle cx="268.0" cy="134.6" r="2.5" fill="#4a7bd0"/><circle cx="271.8" cy="149.6" r="2.5" fill="#4a7bd0"/><circle cx="275.6" cy="152.3" r="2.5" fill="#4a7bd0"/><circle cx="279.4" cy="128.6" r="2.5" fill="#4a7bd0"/>
<circle cx="283.2" cy="152.4" r="2.5" fill="#4a7bd0"/><circle cx="287.0" cy="151.6" r="2.5" fill="#4a7bd0"/><circle cx="290.8" cy="133.2" r="2.5" fill="#4a7bd0"/><circle cx="294.6" cy="144.7" r="2.5" fill="#4a7bd0"/><circle cx="298.4" cy="142.7" r="2.5" fill="#4a7bd0"/><circle cx="302.2" cy="133.4" r="2.5" fill="#4a7bd0"/><circle cx="306.1" cy="147.9" r="2.5" fill="#4a7bd0"/><circle cx="309.9" cy="145.7" r="2.5" fill="#4a7bd0"/>
<circle cx="313.7" cy="138.2" r="2.5" fill="#4a7bd0"/><circle cx="317.5" cy="151.9" r="2.5" fill="#4a7bd0"/><circle cx="321.3" cy="136.8" r="2.5" fill="#4a7bd0"/><circle cx="325.1" cy="151.1" r="2.5" fill="#4a7bd0"/><circle cx="328.9" cy="149.2" r="2.5" fill="#4a7bd0"/><circle cx="332.7" cy="127.1" r="2.5" fill="#4a7bd0"/><circle cx="336.5" cy="149.1" r="2.5" fill="#4a7bd0"/><circle cx="340.3" cy="137.9" r="2.5" fill="#4a7bd0"/>
<circle cx="344.1" cy="149.7" r="2.5" fill="#4a7bd0"/><circle cx="347.9" cy="135.4" r="2.5" fill="#4a7bd0"/><circle cx="351.8" cy="149.4" r="2.5" fill="#4a7bd0"/><circle cx="355.6" cy="151.9" r="2.5" fill="#4a7bd0"/><circle cx="359.4" cy="149.6" r="2.5" fill="#4a7bd0"/><circle cx="363.2" cy="149.2" r="2.5" fill="#4a7bd0"/><circle cx="367.0" cy="149.7" r="2.5" fill="#4a7bd0"/><circle cx="370.8" cy="145.7" r="2.5" fill="#4a7bd0"/>
<circle cx="374.6" cy="140.9" r="2.5" fill="#4a7bd0"/><circle cx="378.4" cy="138.7" r="2.5" fill="#4a7bd0"/><circle cx="382.2" cy="147.0" r="2.5" fill="#4a7bd0"/><circle cx="386.0" cy="140.0" r="2.5" fill="#4a7bd0"/><circle cx="389.8" cy="132.9" r="2.5" fill="#4a7bd0"/><circle cx="393.6" cy="139.6" r="2.5" fill="#4a7bd0"/><circle cx="397.5" cy="131.0" r="2.5" fill="#4a7bd0"/><circle cx="401.3" cy="148.4" r="2.5" fill="#4a7bd0"/>
<circle cx="405.1" cy="145.6" r="2.5" fill="#4a7bd0"/><circle cx="408.9" cy="136.7" r="2.5" fill="#4a7bd0"/><circle cx="412.7" cy="150.2" r="2.5" fill="#4a7bd0"/><circle cx="416.5" cy="150.0" r="2.5" fill="#4a7bd0"/><circle cx="420.3" cy="137.8" r="2.5" fill="#4a7bd0"/><circle cx="424.1" cy="145.1" r="2.5" fill="#4a7bd0"/><circle cx="427.9" cy="139.5" r="2.5" fill="#4a7bd0"/><circle cx="431.7" cy="132.7" r="2.5" fill="#4a7bd0"/>
<circle cx="435.5" cy="139.8" r="2.5" fill="#4a7bd0"/><circle cx="439.4" cy="154.3" r="2.5" fill="#4a7bd0"/><circle cx="443.2" cy="139.5" r="2.5" fill="#4a7bd0"/><circle cx="447.0" cy="133.5" r="2.5" fill="#4a7bd0"/><circle cx="450.8" cy="138.3" r="2.5" fill="#4a7bd0"/><circle cx="454.6" cy="72.4" r="2.5" fill="#4a7bd0"/><circle cx="459.7" cy="40.0" r="2.5" fill="#4a7bd0"/><circle cx="462.2" cy="236.2" r="2.5" fill="#4a7bd0"/>
<circle cx="466.0" cy="228.8" r="2.5" fill="#4a7bd0"/><circle cx="470.2" cy="160.8" r="2.5" fill="#4a7bd0"/><circle cx="473.9" cy="219.9" r="2.5" fill="#4a7bd0"/><circle cx="477.7" cy="219.4" r="2.5" fill="#4a7bd0"/><circle cx="481.5" cy="165.5" r="2.5" fill="#4a7bd0"/><circle cx="485.3" cy="212.8" r="2.5" fill="#4a7bd0"/><circle cx="489.1" cy="208.1" r="2.5" fill="#4a7bd0"/><circle cx="493.0" cy="180.7" r="2.5" fill="#4a7bd0"/>
<circle cx="496.8" cy="187.9" r="2.5" fill="#4a7bd0"/><circle cx="500.6" cy="176.7" r="2.5" fill="#4a7bd0"/><circle cx="504.4" cy="175.4" r="2.5" fill="#4a7bd0"/><circle cx="508.2" cy="170.7" r="2.5" fill="#4a7bd0"/><circle cx="512.0" cy="169.4" r="2.5" fill="#4a7bd0"/><circle cx="515.8" cy="164.0" r="2.5" fill="#4a7bd0"/><circle cx="519.6" cy="149.4" r="2.5" fill="#4a7bd0"/><circle cx="523.4" cy="164.0" r="2.5" fill="#4a7bd0"/>
<circle cx="527.3" cy="164.9" r="2.5" fill="#4a7bd0"/><circle cx="531.1" cy="128.2" r="2.5" fill="#4a7bd0"/><circle cx="534.8" cy="160.9" r="2.5" fill="#4a7bd0"/><circle cx="538.7" cy="148.1" r="2.5" fill="#4a7bd0"/><circle cx="542.5" cy="162.4" r="2.5" fill="#4a7bd0"/><circle cx="546.3" cy="167.9" r="2.5" fill="#4a7bd0"/><circle cx="550.1" cy="127.2" r="2.5" fill="#4a7bd0"/><circle cx="553.9" cy="156.9" r="2.5" fill="#4a7bd0"/>
<circle cx="557.7" cy="168.6" r="2.5" fill="#4a7bd0"/><circle cx="561.5" cy="164.2" r="2.5" fill="#4a7bd0"/><circle cx="565.3" cy="161.2" r="2.5" fill="#4a7bd0"/><circle cx="569.1" cy="155.6" r="2.5" fill="#4a7bd0"/><circle cx="572.9" cy="142.7" r="2.5" fill="#4a7bd0"/><circle cx="576.7" cy="156.4" r="2.5" fill="#4a7bd0"/><circle cx="580.6" cy="105.9" r="2.5" fill="#4a7bd0"/><circle cx="584.4" cy="149.9" r="2.5" fill="#4a7bd0"/>
<circle cx="588.2" cy="135.6" r="2.5" fill="#4a7bd0"/><circle cx="592.1" cy="156.2" r="2.5" fill="#4a7bd0"/><circle cx="595.9" cy="142.6" r="2.5" fill="#4a7bd0"/><circle cx="599.7" cy="141.9" r="2.5" fill="#4a7bd0"/><circle cx="603.5" cy="159.8" r="2.5" fill="#4a7bd0"/><circle cx="607.3" cy="147.1" r="2.5" fill="#4a7bd0"/><circle cx="611.1" cy="155.2" r="2.5" fill="#4a7bd0"/><circle cx="614.9" cy="133.0" r="2.5" fill="#4a7bd0"/>
<circle cx="618.7" cy="159.6" r="2.5" fill="#4a7bd0"/>
<polyline points="134.8,145.9 138.6,147.6 142.4,132.5 146.2,129.3 150.0,140.1 153.8,131.6 157.6,138.3 161.4,147.6 165.2,146.0 169.0,148.8 172.8,137.9 176.6,150.8 180.4,145.1 184.2,150.5 188.0,143.4 191.8,120.9 195.6,145.2 199.4,138.5 203.3,134.1 207.1,125.2 210.9,114.6 214.7,136.7 218.5,121.0 222.3,147.3 226.1,152.6 229.9,145.5 233.7,141.6 237.5,152.2 241.3,144.7 245.1,149.5 249.0,141.5 252.8,141.7 256.6,128.9 260.4,142.0 264.2,151.2 268.0,134.6 271.8,149.6 275.6,152.3 279.4,128.6 283.2,152.4 287.0,151.6 290.8,133.2 294.6,144.7 298.4,142.7 302.2,133.4 306.1,147.9 309.9,145.7 313.7,138.2 317.5,151.9 321.3,136.8 325.1,151.1 328.9,149.2 332.7,127.1 336.5,149.1 340.3,137.9 344.1,149.7 347.9,135.4 351.8,149.4 355.6,151.9 359.4,149.6 363.2,149.2 367.0,149.7 370.8,145.7 374.6,140.9 378.4,138.7 382.2,147.0 386.0,140.0 389.8,132.9 393.6,139.6 397.5,131.0 401.3,148.4 405.1,145.6 408.9,136.7 412.7,150.2 416.5,150.0 420.3,137.8 424.1,145.1 427.9,139.5 431.7,132.7 435.5,139.8 439.4,154.3 443.2,139.5 447.0,133.5 450.8,138.3 454.6,72.4 459.7,40.0 462.2,236.2 466.0,228.8 470.2,160.8 473.9,219.9 477.7,219.4 481.5,165.5 485.3,212.8 489.1,208.1 493.0,180.7 496.8,187.9 500.6,176.7 504.4,175.4 508.2,170.7 512.0,169.4 515.8,164.0 519.6,149.4 523.4,164.0 527.3,164.9 531.1,128.2 534.8,160.9 538.7,148.1 542.5,162.4 546.3,167.9 550.1,127.2 553.9,156.9 557.7,168.6 561.5,164.2 565.3,161.2 569.1,155.6 572.9,142.7 576.7,156.4 580.6,105.9 584.4,149.9 588.2,135.6 592.1,156.2 595.9,142.6 599.7,141.9 603.5,159.8 607.3,147.1 611.1,155.2 614.9,133.0 618.7,159.6" fill="none" stroke="#4a7bd0" stroke-width="1" stroke-opacity="0.6"/>
<text x="74.0" y="32" font-size="12">s0: +300 ms (위로 잘림)</text>
<text x="453.7" y="32" font-size="12" text-anchor="end">호스트 부하 스파이크 +116.6 ms</text>
<rect x="460" y="6" width="12" height="8" fill="#d0564a"/><text x="476" y="14" font-size="12">s0</text>
<rect x="510" y="6" width="12" height="8" fill="#e08a3c"/><text x="526" y="14" font-size="12">s1 (step)</text>
<rect x="590" y="6" width="12" height="8" fill="#4a7bd0"/><text x="606" y="14" font-size="12">s2</text>
</svg>
```
그림 7 — 게스트 b의 `master offset` 전체 기록 (±12 ms에서 잘림). s0 구간은 +300 ms, t≈103 s의 점은 +116.6 ms

slave 로그 145줄(2.5분)을 짧은 Python 스크립트로 집계했다(s2 줄만, |offset| 정렬 후 백분위. 첫 줄 = 개수·중앙값·p90·p99·최대, 둘째 줄 = 5 ms 넘는 이상치를 뺀 개수·RMS·평균, 단위 ns):

```text
128 median 828342 p90 3473958 p99 10420830 max 116617002
excluding >5ms: 120 rms 1498443 mean -120177
```

- 이 TCG 환경에서 SW PTP의 정확도는 **중앙값 0.8 ms, RMS 1.5 ms**다. 이는 PTP의 한계가 아니라 SW 타임스탬프 + 에뮬레이션 지연의 한계다(§6.5 표: 실제 하드웨어 SW 모드는 µs급, HW 모드는 수십 ns급 — 기억 기반).
- **t ≈ 1702 s의 +116.6 ms 스파이크**: 공유 VM의 다른 작업 때문에 게스트가 잠깐 멈춘 순간이다. servo는 이것을 진짜 오프셋으로 믿고 freq를 +11.77 ppm(+11771583 ppb)까지 당겼고, 그 대가로 이후 약 15초 동안 −10 ms ~ −3 ms 쪽으로 끌려갔다:

```text
ptp4l[1700.669]: master offset    8295607 s2 freq +822826 path delay   1626468
ptp4l[1702.003]: master offset  116617002 s2 freq +11771583 path delay   1871269
ptp4l[1702.679]: master offset  -10420830 s2 freq -942621 path delay   2257883
ptp4l[1703.679]: master offset   -9575058 s2 freq -867619 path delay   2257883
...
ptp4l[1714.780]: master offset   -2937488 s2 freq -259046 path delay    780620
```

교훈: 한 번의 나쁜 타임스탬프가 PI servo를 십수 초 흔든다. 실무 대응은 (1) HW 타임스탬프, (2) linuxptp의 이상치 필터(`tsproc_mode`, `delay_filter`/`delay_filter_length` — path delay용 이동 중앙값 필터), (3) 퓨전 쪽에서는 `TIME_STATUS_NP`를 감시해 큰 오프셋 구간의 데이터를 표시하는 것.

### 7.5 실수에서 배운 것 — ptp4l 두 개가 한 시계를 잡으면

첫 시도에서 스크립트 실수로 게스트 b에 ptp4l이 **두 개** 떠 있었다(둘 다 `/tmp/s.log`에 썼다). 로그가 이랬다(발췌):

```text
ptp4l[1471.223]: master offset   -2632369 s1 freq -144853 path delay   2082114
ptp4l[1472.333]: clockcheck: clock frequency changed unexpectedly!
ptp4l[1472.334]: master offset    2553292 s2 freq +113030 path delay   2082114
ptp4l[1472.335]: port 1 (enp0s1): UNCALIBRATED to SLAVE on MASTER_CLOCK_SELECTED
ptp4l[1473.229]: master offset    -123426 s2 freq -154766 path delay   2082114
ptp4l[1474.224]: clockcheck: clock frequency changed unexpectedly!
ptp4l[1474.227]: master offset     199974 s2 freq -122226 path delay   1628673
```

`clockcheck: clock frequency changed unexpectedly!` — ptp4l이 "내가 설정한 주파수와 커널이 돌려준 주파수가 다르다"를 감지했다. 누군가(다른 ptp4l, chronyd, systemd-timesyncd, 또는 `phc2sys`)가 같은 시계를 만지고 있다는 신호다. 실제 장비에서 가장 흔한 동기 사고 중 하나다: **한 시계에는 하나의 servo만.** 확인 명령: `pgrep -a 'ptp4l|phc2sys|chronyd|ntpd|timesyncd'`, `systemctl is-active systemd-timesyncd chrony`.

---

## 8. 실습 — PHC 다루기: netdevsim + mock PTP clock

### 8.1 PHC가 없으면 만든다

QEMU의 virtio-net에는 PHC가 없다. 커널 6.18에는 테스트용 `netdevsim` 네트워크 장치가 있고, 이 장치는 `ptp_mock`(`drivers/ptp/ptp_mock.c`)이 만드는 **가짜 PHC**를 붙인다. mock PHC는 `CLOCK_MONOTONIC_RAW`에 해당하는 카운터를 `timecounter`로 감싼 것이라 `adjfine`(주파수)·`adjtime`(오프셋)·`settime64`가 전부 동작한다 — servo 연습에 충분하다. 패킷 HW 타임스탬핑은 안 된다.

### 8.2 예제 7 — `ethtool -T`, `phc_ctl`

```sh
modprobe netdevsim && echo "1 1 4" > /sys/bus/netdevsim/new_device     # id 1, 포트 1개, 큐 4개
N=$(ls /sys/bus/netdevsim/devices/netdevsim1/net/); echo "netdev=$N"; ip link set $N up
for p in /sys/class/ptp/ptp*; do echo "$p: $(cat $p/clock_name)"; done
ethtool -T $N
phc_ctl /dev/ptp0 get cmp
phc_ctl /dev/ptp0 set
phc_ctl /dev/ptp0 get cmp
phc_ctl /dev/ptp0 freq 20000 wait 10 cmp freq 0 cmp
```

```text
netdev=eni1np1
/sys/class/ptp/ptp0: Mock-up PTP clock
Time stamping parameters for eni1np1:
Capabilities:
	software-receive
	software-system-clock
PTP Hardware Clock: 0
Hardware Transmit Timestamp Modes: none
Hardware Receive Filter Modes: none
phc_ctl[1786.666]: clock time is 2.042247904 or Thu Jan  1 00:00:02 1970
phc_ctl[1786.674]: offset from CLOCK_REALTIME is 1792342180241223240ns
phc_ctl[1786.799]: set clock time to 1792342182.415194832 or Sun Oct 18 16:49:42 2026
phc_ctl[1786.955]: clock time is 1792342182.568862256 or Sun Oct 18 16:49:42 2026
phc_ctl[1786.966]: offset from CLOCK_REALTIME is 601744ns
phc_ctl[1787.111]: adjusted clock frequency offset to 20000.000000ppb
phc_ctl[1797.125]: process slept for 10.000000 seconds
phc_ctl[1797.134]: offset from CLOCK_REALTIME is 401273ns
phc_ctl[1797.135]: adjusted clock frequency offset to 0.000000ppb
phc_ctl[1797.138]: offset from CLOCK_REALTIME is 401203ns
```

(빈 줄은 지웠다.) 출력에서 볼 것:

- `PTP Hardware Clock: 0`인데 hardware-transmit/receive 모드는 none — **PHC는 있는데 패킷 HW 타임스탬핑은 없다**. 둘은 별개다. `ethtool -T`의 `software-receive`, `software-system-clock`은 netdevsim 드라이버가 채운 게 아니라 6.18 ethtool 코어(`net/ethtool/common.c`)가 기본으로 넣어 주는 값이다.
- mock PHC는 생성 순간 0에서 시작(1970년 1월 1일 + 2초). `set`이 시스템 시각을 복사하면 차이가 0.6 ms로 줄었다(TCG에서 두 번 읽는 사이 시간).
- **+20000 ppb로 10초** → `cmp` 값이 601744 → 401273, 약 **200 µs** 줄었다. 20 ppm × 10 s = 200 µs. `cmp`는 "REALTIME − PHC"이고, PHC가 빨라졌으니 줄어든 것이다. `adjfine`이 정확히 동작한다는 확인이다.

### 8.3 예제 8 — PHC를 직접 읽는 C 코드 (phc2sys가 하는 일)

무엇을 확인하는 코드인지: PHC 능력(`PTP_CLOCK_GETCAPS`), dynamic clockid로 PHC 시각 읽기, `PTP_SYS_OFFSET_EXTENDED`(없으면 `PTP_SYS_OFFSET`)의 시스템-PHC-시스템 샌드위치로 오프셋 계산.

```c
/* q3_phc /dev/ptpN : capabilities of a PTP hardware clock and its offset to CLOCK_REALTIME
 * measured the way phc2sys does it: PTP_SYS_OFFSET_EXTENDED (sys-phc-sys sandwich) */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>
#include <linux/ptp_clock.h>
#define FD_TO_CLOCKID(fd) ((~(clockid_t)(fd) << 3) | 3)

static long long ns(struct ptp_clock_time t) { return t.sec * 1000000000LL + t.nsec; }

int main(int argc, char **argv)
{
	struct ptp_clock_caps caps;
	struct ptp_sys_offset_extended ext;
	struct timespec ts;
	int fd;

	if (argc < 2) { fprintf(stderr, "usage: %s /dev/ptpN\n", argv[0]); return 1; }
	if ((fd = open(argv[1], O_RDWR)) < 0) { perror("open"); return 1; }
	if (ioctl(fd, PTP_CLOCK_GETCAPS, &caps) < 0) { perror("GETCAPS"); return 1; }
	printf("max_adj=%d ppb n_alarm=%d n_ext_ts=%d n_per_out=%d pps=%d n_pins=%d\n",
	       caps.max_adj, caps.n_alarm, caps.n_ext_ts, caps.n_per_out, caps.pps, caps.n_pins);
	if (clock_gettime(FD_TO_CLOCKID(fd), &ts)) { perror("clock_gettime(phc)"); return 1; }
	printf("PHC time via dynamic clockid: %lld.%09ld\n", (long long)ts.tv_sec, ts.tv_nsec);

	memset(&ext, 0, sizeof(ext));
	ext.n_samples = 5;
	if (ioctl(fd, PTP_SYS_OFFSET_EXTENDED, &ext) < 0) {
		struct ptp_sys_offset so = { .n_samples = 5 };	/* fallback: sys,phc,sys,... */
		perror("SYS_OFFSET_EXTENDED (falling back to PTP_SYS_OFFSET)");
		if (ioctl(fd, PTP_SYS_OFFSET, &so) < 0) { perror("PTP_SYS_OFFSET"); return 1; }
		for (unsigned i = 0; i < so.n_samples; i++) {
			ext.ts[i][0] = so.ts[2 * i]; ext.ts[i][1] = so.ts[2 * i + 1]; ext.ts[i][2] = so.ts[2 * i + 2];
		}
	}
	long long best = -1, off = 0;
	for (unsigned i = 0; i < ext.n_samples; i++) {
		long long t1 = ns(ext.ts[i][0]), tp = ns(ext.ts[i][1]), t2 = ns(ext.ts[i][2]);
		long long win = t2 - t1, o = tp - (t1 + win / 2);	/* phc - sys at the midpoint */
		printf("sample %u: window %6lld ns  phc-sys %+lld ns\n", i, win, o);
		if (best < 0 || win < best) { best = win; off = o; }
	}
	printf("best (shortest window %lld ns): phc - CLOCK_REALTIME = %+lld ns\n", best, off);
	close(fd);
	return 0;
}
```

```text
max_adj=32000000 ppb n_alarm=0 n_ext_ts=0 n_per_out=0 pps=0 n_pins=0
PHC time via dynamic clockid: 1792342193.225437285
SYS_OFFSET_EXTENDED (falling back to PTP_SYS_OFFSET): Operation not supported
sample 0: window  13136 ns  phc-sys -400275 ns
sample 1: window   3376 ns  phc-sys -401203 ns
sample 2: window   2912 ns  phc-sys -401211 ns
sample 3: window   2704 ns  phc-sys -401187 ns
sample 4: window   2832 ns  phc-sys -401251 ns
best (shortest window 2704 ns): phc - CLOCK_REALTIME = -401187 ns
```

출력에서 볼 것:

- `n_ext_ts=0 n_per_out=0 pps=0` — mock PHC에는 외부 타임스탬프 입력(extts)도, 주기 출력(perout)도, PPS 이벤트도 없다. 진짜 NIC(예: i210, stmmac 일부, TI CPTS)는 이 값이 1 이상이고, 그 핀이 §2의 trigger와 §9의 PPS 입력이 된다.
- `PTP_SYS_OFFSET_EXTENDED`는 `EOPNOTSUPP` — 드라이버에 `gettimex64`가 없어서다(6.18 `ptp_chardev.c`: `if (!gettimex_fn) return -EOPNOTSUPP;`). `PTP_SYS_OFFSET`은 코어가 `ktime_get_real_ts64()` → `gettime64()` → `ktime_get_real_ts64()`로 샌드위치를 만들어 준다.
- 첫 샘플 창 13 µs(캐시·TLB 냉각), 이후 2.7~3.4 µs. 가장 짧은 창의 오프셋 −401187 ns는 예제 7의 마지막 `cmp`(+401203 ns, 부호 반대 정의)와 일치한다.
- PHC를 `clock_gettime()`으로 바로 읽을 수 있다: fd → clockid 변환 매크로 `FD_TO_CLOCKID`(linuxptp와 커널 문서에 나오는 정의 그대로).

### 8.4 예제 9 — `phc2sys`로 PHC를 시스템 시계에 맞추기

실제 장비에서는 보통 반대 방향(`phc2sys -s /dev/ptp0 -c CLOCK_REALTIME`: PTP로 맞춘 PHC를 기준으로 시스템 시계를 끌고 감)이다. 여기서는 게스트 시스템 시계를 건드리지 않으려고 **PHC를 slave로** 썼다(GM 장비가 GNSS로 맞춘 시스템 시계를 PHC로 내보내는 방향과 같다). 먼저 PHC를 망가뜨린다: +100 ppm, +5 ms.

```sh
phc_ctl /dev/ptp0 freq 100000 adj 0.005 cmp
timeout 25 phc2sys -s CLOCK_REALTIME -c /dev/ptp0 -O 0 -m -R 2
phc_ctl /dev/ptp0 cmp freq
```

```text
phc_ctl[1811.445]: adjusted clock frequency offset to 100000.000000ppb
phc_ctl[1811.451]: adjusted clock by 0.005000 seconds
phc_ctl[1811.462]: offset from CLOCK_REALTIME is -4600447ns
phc2sys[1812.182]: /dev/ptp0 sys offset   4672150 s0 freq -100000 delay   2624
phc2sys[1812.690]: /dev/ptp0 sys offset   4723096 s1 freq     -15 delay   4208
phc2sys[1813.199]: /dev/ptp0 sys offset       210 s2 freq    +195 delay   2752
phc2sys[1813.701]: /dev/ptp0 sys offset       112 s2 freq    +160 delay   2752
phc2sys[1814.204]: /dev/ptp0 sys offset        16 s2 freq     +98 delay   2784
phc2sys[1814.706]: /dev/ptp0 sys offset       -17 s2 freq     +69 delay   2496
...
phc2sys[1835.886]: /dev/ptp0 sys offset       189 s2 freq    +187 delay   3712
phc2sys[1836.389]: /dev/ptp0 sys offset       -25 s2 freq     +29 delay   2800
phc2sys[1836.636]: /dev/ptp0 sys offset      -104 s2 freq     -57 delay   2752
phc_ctl[1836.803]: offset from CLOCK_REALTIME is 111ns
phc_ctl[1836.816]: clock frequency offset is 57.128906ppb
```

출력에서 볼 것:

- `-O 0`: 두 시계 사이 목표 오프셋 0(같은 시간축). PHC가 TAI이고 시스템이 UTC면 `-O -37`이 아니라 `-w`(ptp4l에서 UTC 오프셋을 받아 옴)를 쓰는 게 정석이다.
- **s0 → s1 → s2를 1초 만에**: s0에서 주파수 차를 재고(s0의 freq −100000 = 우리가 넣은 +100 ppm을 부호 반대로 읽은 것), s1에서 4.7 ms를 step, s2부터는 **±100 ns 안팎**. ptp4l(SW, 네트워크 너머) ±1~2 ms와 비교하면 4자리 차이다. 같은 기계 안 두 시계 사이 샌드위치(`delay` 2.5~4.3 µs = 창 길이)는 네트워크보다 훨씬 깨끗하다.
- 끝난 뒤 PHC freq 보정은 +57 ppb — 우리가 넣은 +100 ppm(100000 ppb)을 servo가 거의 정확히 상쇄했다. mock PHC와 시스템 시계는 같은 카운터에서 나오므로 진짜 주파수 차는 0이 정답이다.

### 8.5 예제 10 — HW 모드를 요구하면

```sh
timeout 10 ptp4l -i eni1np1 -H -m
```

```text
ptp4l[1797.797]: ioctl SIOCETHTOOL failed: Operation not supported
ptp4l[1797.801]: interface 'eni1np1' does not support requested timestamping mode
failed to create a clock
```

PHC가 있어도 드라이버가 `ndo_hwtstamp_set`과 `get_ts_info`의 HW 모드를 제공하지 않으면 ptp4l `-H`는 시작조차 안 한다. 새 보드 bring-up에서 이 메시지가 나오면 확인 순서: `ethtool -T`(HW 모드가 있나) → DT에서 PTP 클럭 입력(`clocks`, `clock-names = "ptp_ref"` 같은 MAC 바인딩 속성)이 켜졌나 → 커널 config(`CONFIG_PTP_1588_CLOCK`과 MAC 드라이버 쪽 PTP 관련 옵션).

---

## 9. PPS — 1초에 한 번, 정확한 엣지

### 9.1 개념

PPS(Pulse Per Second)는 GNSS 수신기·원자 시계·GM이 내보내는 **매 초 정각의 엣지**다. 엣지 자체에는 "몇 시 몇 분 몇 초"가 없다 — 그건 NMEA/UBX 문장이나 PTP가 알려 준다. PPS는 **위상(초의 경계)** 을 ns~수십 ns 정확도로, NMEA는 **어느 초인지**를 ms~100 ms 지터로 알려 준다. 둘을 합치면 정확한 절대 시각이 된다.

Linux의 PPS 서브시스템(`drivers/pps/`, RFC 2783 API):

| 소스 드라이버 | 엣지를 어디서 받나 | 정확도 감각 |
|---|---|---|
| `pps-gpio` | GPIO IRQ, DT `compatible = "pps-gpio"` | IRQ 지연만큼 흔들림(µs, 기억 기반) |
| `pps-ldisc` | 시리얼 포트 DCD 핀 (line discipline) | 시리얼 IRQ 지연 |
| PHC extts → PPS | NIC 타이머 캡처 입력 (`ptp_clock_event` PTP_CLOCK_PPS) | HW 캡처, ns급 |
| `pps-ktimer` | 커널 timer_list (테스트용) | jiffies 해상도 — 정확하지 않음 |

유저 공간: `/dev/ppsN`의 ioctl(`PPS_GETPARAMS`, `PPS_SETPARAMS`, `PPS_GETCAP`, `PPS_FETCH`), sysfs `/sys/class/pps/ppsN/{assert,clear,mode,name,path}`, 도구 `ppstest`/`ppswatch`(pps-tools), 소비자 chrony(`refclock PPS`), gpsd, `ts2phc`.

### 9.2 예제 11 — `pps-ktimer`와 `ppstest`

```sh
modprobe pps-ktimer && ls -l /dev/pps*
timeout 7 ppstest /dev/pps0
```

```text
crw------- 1 root root 247, 0 Oct 18 16:24 /dev/pps0
trying PPS source "/dev/pps0"
found PPS source "/dev/pps0"
ok, found 1 source(s), now start fetching data...
source 0 - assert 1792340653.473206576, sequence: 4 - clear  0.000000000, sequence: 0
source 0 - assert 1792340654.500248880, sequence: 5 - clear  0.000000000, sequence: 0
source 0 - assert 1792340655.522920272, sequence: 6 - clear  0.000000000, sequence: 0
source 0 - assert 1792340656.544311712, sequence: 7 - clear  0.000000000, sequence: 0
source 0 - assert 1792340657.601085136, sequence: 8 - clear  0.000000000, sequence: 0
source 0 - assert 1792340658.628350320, sequence: 9 - clear  0.000000000, sequence: 0
source 0 - assert 1792340659.649245504, sequence: 10 - clear  0.000000000, sequence: 0
```

출력에서 볼 것: assert 시각의 **소수점 부분이 .473 → .500 → .522 → … → .649로 매번 20~60 ms씩 밀린다.** 진짜 GNSS PPS라면 소수점이 매번 같은 값(시스템 시계가 맞아 있으면 .000000xxx 근처)이어야 한다. 이유는 소스 코드에 있다:

```c
/* drivers/pps/clients/pps-ktimer.c (6.18.55, 발췌) */
static void pps_ktimer_event(struct timer_list *unused)
{
	struct pps_event_time ts;

	/* First of all we get the time stamp... */
	pps_get_ts(&ts);

	pps_event(pps, &ts, PPS_CAPTUREASSERT, NULL);

	mod_timer(&ktimer, jiffies + HZ);
}
```

- 타이머 콜백이 실행된 **그 순간부터** 다시 `jiffies + HZ` 뒤로 예약한다(상대 재무장). 초 경계에 고정된 게 아니다.
- `timer_list`는 timer wheel 위에 있어서 1초(250 tick) 뒤 만료는 레벨 1 버킷(8 tick = 32 ms 단위)으로 반올림돼 **늦게** 터진다(F8 §6.5). 그래서 주기가 1.016~1.031 s가 되고 위상이 계속 밀린다.
- 타임스탬프는 `pps_get_ts()` → `ktime_get_snapshot()`의 REALTIME 값이다. 실제 `pps-gpio`도 IRQ 핸들러 맨 앞에서 같은 함수를 부른다 — 차이는 엣지가 정확하냐뿐이다.

즉 pps-ktimer는 **API·도구 연습용**이지 시간 기준이 아니다. 모듈 설명 자체가 `"dummy PPS source by using a kernel timer (just for debug)"`다.

### 9.3 예제 12 — PPS API를 직접 (ioctl `PPS_FETCH`)

무엇을 확인하는 코드인지: pps-tools의 `timepps.h` 없이 커널 uapi(`linux/pps.h`)만으로 엣지를 블로킹으로 기다리고, 엣지 간격이 1 s에서 얼마나 벗어나는지 잰다.

```c
/* q3_pps /dev/pps0 N : fetch N assert events with the raw kernel PPS API (PPS_FETCH)
 * and show how far each edge is from the previous one (should be exactly 1 s) */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/pps.h>

int main(int argc, char **argv)
{
	int fd, n = argc > 2 ? atoi(argv[2]) : 5, mode;
	struct pps_kparams p;
	double prev = 0, mn = 1e9, mxv = -1e9;

	if (argc < 2) { fprintf(stderr, "usage: %s /dev/ppsN [n]\n", argv[0]); return 1; }
	if ((fd = open(argv[1], O_RDWR)) < 0) { perror("open"); return 1; }
	if (ioctl(fd, PPS_GETCAP, &mode) < 0) { perror("PPS_GETCAP"); return 1; }
	if (ioctl(fd, PPS_GETPARAMS, &p) < 0) { perror("PPS_GETPARAMS"); return 1; }
	printf("caps=0x%x params.mode=0x%x (CAPTUREASSERT=%d CANWAIT=%d)\n", mode, p.mode,
	       !!(mode & PPS_CAPTUREASSERT), !!(mode & PPS_CANWAIT));
	for (int i = 0; i < n; i++) {
		struct pps_fdata f = { .timeout = { .sec = 3, .nsec = 0, .flags = 0 } };
		if (ioctl(fd, PPS_FETCH, &f) < 0) { perror("PPS_FETCH"); return 1; }
		double t = f.info.assert_tu.sec + f.info.assert_tu.nsec / 1e9;
		printf("assert #%u  %lld.%09d", f.info.assert_sequence,
		       (long long)f.info.assert_tu.sec, f.info.assert_tu.nsec);
		if (prev > 0) {
			double d = (t - prev - 1.0) * 1e6;	/* deviation from 1 s, us */
			printf("  interval-1s = %+9.1f us", d);
			if (d < mn) mn = d;
			if (d > mxv) mxv = d;
		}
		printf("\n");
		prev = t;
	}
	if (n > 1) printf("interval error range: %+.1f .. %+.1f us\n", mn, mxv);
	close(fd);
	return 0;
}
```

BusyBox initramfs 게스트에서(`modprobe pps-ktimer` 직후, sysfs와 함께. `ls`의 색 코드는 지웠다):

```text
pps0
name=ktimer
path=
mode=1151
caps=0x1151 params.mode=0x11 (CAPTUREASSERT=1 CANWAIT=1)
assert #1  1792340324.130961840
assert #2  1792340325.154010272  interval-1s =  +23048.4 us
assert #3  1792340326.177539920  interval-1s =  +23529.5 us
assert #4  1792340327.201964272  interval-1s =  +24424.6 us
assert #5  1792340328.226037456  interval-1s =  +24073.1 us
assert #6  1792340329.249533648  interval-1s =  +23496.2 us
interval error range: +23048.4 .. +24424.6 us
assert=1792340329.249533648#6
```

systemd 게스트에서(`ppstest`를 먼저 돌린 뒤라 `params.mode`가 바뀌어 있다):

```text
caps=0x1151 params.mode=0x1111 (CAPTUREASSERT=1 CANWAIT=1)
assert #12  1792340661.697316880
assert #13  1792340662.728736448  interval-1s =  +31419.5 us
assert #14  1792340663.745169712  interval-1s =  +16433.2 us
assert #15  1792340664.768429152  interval-1s =  +23259.4 us
interval error range: +16433.2 .. +31419.5 us
```

출력에서 볼 것:

- caps 0x1151 = `PPS_CAPTUREASSERT`(0x01) + `PPS_OFFSETASSERT`(0x10) + `PPS_ECHOASSERT`(0x40) + `PPS_CANWAIT`(0x100) + `PPS_TSFMT_TSPEC`(0x1000) — 드라이버의 `pps_source_info.mode`와 정확히 같다.
- 간격 오차 +16~+31 ms — 위에서 설명한 timer wheel 반올림(최대 32 ms) 그대로다. 진짜 GNSS PPS + `pps-gpio`라면 이 열이 수 µs 이내여야 한다(기억 기반). **PPS 소스를 새로 붙였을 때 이 프로그램(또는 `ppstest`)으로 간격의 흔들림을 먼저 본다** — 수 ms가 나오면 IRQ 경로나 소스가 틀린 것이다.
- sysfs `assert`는 `초.나노초#sequence` 형식이다. 모니터링 스크립트는 sequence가 매초 1씩 오르는지(엣지 누락)를 본다.

### 9.4 `pps-gpio` Device Tree (가이드, 실행 안 함)

실제 보드에서는 GNSS 모듈의 PPS 핀을 SoC GPIO에 연결하고 DT에 이렇게 쓴다(바인딩: 6.18 `Documentation/devicetree/bindings/pps/pps-gpio.yaml`).

```dts
pps {
	compatible = "pps-gpio";
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_gnss_pps>;
	gpios = <&gpio3 12 GPIO_ACTIVE_HIGH>;
	/* assert-falling-edge;   PPS가 하강 엣지로 정의된 모듈이면 */
};
```

함정: GPIO 컨트롤러가 IRQ를 **엣지**로 잡을 수 있어야 하고, IRQ가 threaded가 아니어야 지연이 작다. 레벨 시프터·옵토커플러가 엣지를 수백 ns 늦출 수 있다(측정 후 chrony `offset`이나 ts2phc 설정으로 보정).

### 9.5 PPS를 PHC에 직접 — `ts2phc`

NIC에 extts 입력이 있으면 GNSS PPS를 **NIC 핀**에 꽂고 `ts2phc`가 PHC를 PPS에 맞춘다. PPS 엣지가 하드웨어 캡처(ns급)로 찍히고, 그 PHC가 `ptp4l` GM으로 네트워크에 시각을 뿌린다. 차량·로봇 GM의 정석 구성이다(가이드, 실행 안 함 — mock PHC는 `n_ext_ts=0`).

```text
# ts2phc.cfg (예시) — 실행: ts2phc -f ts2phc.cfg -s nmea -m
[global]
use_syslog             0
ts2phc.nmea_serialport /dev/ttyS1
ts2phc.pulsewidth      100000000
[eth0]
ts2phc.extts_polarity  rising
```

> 확인 범위: 설정 키 이름(`ts2phc.nmea_serialport`, `ts2phc.pulsewidth`, `ts2phc.extts_polarity`, `-s nmea` 소스)은 rootfs-dom의 linuxptp 4.0 `ts2phc` 바이너리 문자열로 확인했다(이 rootfs에는 man 페이지가 없다). 같은 바이너리에 `UTC-TAI offset not set in system! Trying to revert to leapfile`이라는 메시지가 있다 — §1의 `tai=0` 문제를 ts2phc도 신경 쓴다는 뜻이다. 실행은 하지 않았다.

---

## 10. GNSS로 시스템 시계를 맞추기 — chrony + gpsd (Q5 연결)

PHC가 없는 장비(작은 SoC, USB GNSS)에서는 chrony가 gpsd의 NMEA 시각(대략적인 초)과 커널 PPS(정확한 위상)를 결합한다. gpsd 쪽 설정·UBX 프로토콜·`gpsfake`는 Q5에서 다룬다. 이 노트는 시간 쪽만 본다(가이드, 실행 안 함 — 이 랩에는 실제 GNSS 엣지가 없고 pps-ktimer는 위상이 흘러가서 chrony가 거부한다).

```text
# /etc/chrony/chrony.conf 발췌
refclock SHM 0 refid NMEA precision 1e-1 offset 0.0 delay 0.2 noselect
refclock PPS /dev/pps0 lock NMEA refid PPS precision 1e-7
makestep 1.0 3
rtcsync
```

- `SHM 0`: gpsd가 공유 메모리 세그먼트 0에 NMEA 기반 시각을 써 준다. 지터가 크니 `noselect`로 직접 쓰지 않고,
- `PPS ... lock NMEA`: PPS 엣지가 "어느 초"인지를 NMEA에서 빌려 와 결합한다.
- `makestep 1.0 3`: 부팅 직후 처음 3번은 1 s 넘게 틀리면 step. 그 뒤로는 slew만 — **센서 녹화 중에 시계가 점프하지 않게** 하는 설정이다.
- PHC가 있으면 chrony가 PHC를 직접 refclock으로 쓸 수도 있다: `refclock PHC /dev/ptp0 poll 0 dpoll -2 offset 37`(PHC가 TAI일 때 37 s 보정. 기억 기반 설정 예시 — chrony.conf(5)로 확인).
- 확인 명령: `chronyc sources -v`, `chronyc tracking`(System time, Frequency, Skew), `gpspipe -r`.

---

## 11. TSN qdisc 맛보기 — `taprio`와 `etf`

시간 동기가 서면 "언제 보낼지"도 시각으로 정할 수 있다. TSN(Time-Sensitive Networking)의 두 qdisc:

- `taprio`(802.1Qbv): 시간 슬롯마다 어떤 트래픽 클래스의 게이트를 열지 정하는 **주기 스케줄**. 예: 1 ms 주기 중 앞 300 µs는 제어 트래픽만.
- `etf`(Earliest TxTime First): 패킷마다 `SO_TXTIME`으로 "이 시각에 보내라"를 지정. 소켓이 `SCM_TXTIME` cmsg로 시각을 붙인다.

둘 다 `clockid CLOCK_TAI`를 쓴다 — §1의 `tai=0` 함정이 여기서 다시 나온다. 하드웨어 오프로드(`offload on`, igc·stmmac·enetc 등)가 없으면 소프트웨어로 동작해 정확도가 µs~ms 수준이다.

### 11.1 예제 13 — netdevsim(4 큐)에 설정해 보기

```sh
modprobe sch_taprio; modprobe sch_etf; ls /sys/class/net/$N/queues/
tc qdisc replace dev $N parent root handle 100 taprio num_tc 2 map 1 1 1 1 1 1 1 0 1 1 1 1 1 1 1 1 \
   queues 1@0 3@1 base-time 1000000000 sched-entry S 01 300000 sched-entry S 02 700000 clockid CLOCK_TAI
tc qdisc show dev $N
tc qdisc del dev $N root; tc qdisc add dev $N root handle 1: mq
tc qdisc replace dev $N parent 1:1 etf clockid CLOCK_TAI delta 300000; tc qdisc show dev $N
```

```text
rx-0  rx-1  rx-2  rx-3  tx-0  tx-1  tx-2  tx-3
Warning: sch_taprio: Size table not specified, frame length estimations may be inaccurate.
qdisc taprio 100: root refcnt 5 tc 2 map 1 1 1 1 1 1 1 0 1 1 1 1 1 1 1 1
queues offset 0 count 1 offset 1 count 3
clockid TAI	base-time 1000000000 cycle-time 1000000 cycle-time-extension 0
	index 0 cmd S gatemask 0x1 interval 300000
	index 1 cmd S gatemask 0x2 interval 700000
qdisc pfifo 0: parent 100:4 limit 1000p
qdisc pfifo 0: parent 100:3 limit 1000p
qdisc pfifo 0: parent 100:2 limit 1000p
qdisc pfifo 0: parent 100:1 limit 1000p
qdisc mq 1: root 
qdisc etf 8001: parent 1:1 clockid TAI delta 300000 offload off deadline_mode off skip_sock_check off
qdisc pfifo_fast 0: parent 1:4 bands 3 priomap 1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1
qdisc pfifo_fast 0: parent 1:3 bands 3 priomap 1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1
qdisc pfifo_fast 0: parent 1:2 bands 3 priomap 1 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1
```

출력에서 볼 것: priority 7(map의 8번째 값 0)을 TC0 = 큐 0에, 나머지를 TC1 = 큐 1~3에 매핑했다. cycle-time 1 ms(300 µs + 700 µs)가 자동 계산됐다. `etf`는 `offload off` — 소프트웨어 모드. 이 노트에서는 설정 문법과 시간축(TAI)만 확인했고, 실제 전송 시각 정확도는 측정하지 않았다(가상 NIC라 의미가 없다).

---

## 12. 커널 안에서는 무슨 일이 일어나나

### 12.1 PTP 하드웨어 시계 드라이버의 모양

NIC 드라이버가 PHC를 노출하는 구조는 6.18 `include/linux/ptp_clock_kernel.h`의 `struct ptp_clock_info`가 중심이다. 드라이버가 채우는 콜백:

| 콜백 | 하는 일 | 유저 공간에서 부르는 경로 |
|---|---|---|
| `adjfine(info, scaled_ppm)` | 주파수 보정 (scaled_ppm = ppm × 2^16) | `clock_adjtime(FD_TO_CLOCKID(fd))` ADJ_FREQUENCY → `phc_ctl freq`, ptp4l/phc2sys servo |
| `adjtime(info, delta)` | 오프셋 한 번에 더하기 | ADJ_SETOFFSET → `phc_ctl adj`, servo step |
| `gettime64`/`gettimex64` | 시각 읽기 (`x`는 읽기 직전·직후 시스템 시각도 기록 → 샌드위치가 짧아짐) | `clock_gettime`, `PTP_SYS_OFFSET(_EXTENDED)` |
| `getcrosststamp` | PHC와 시스템 카운터 동시 래치 | `PTP_SYS_OFFSET_PRECISE` |
| `settime64` | 시각 설정 | `clock_settime` → `phc_ctl set` |
| `enable(info, rq, on)` | extts(외부 엣지 캡처), perout(주기 출력), PPS 이벤트 켜기 | `PTP_EXTTS_REQUEST`, `PTP_PEROUT_REQUEST2`, `PTP_ENABLE_PPS` |
| `verify(info, pin, func, chan)` | 핀 기능 할당 검증 | `PTP_PIN_SETFUNC` |
| `do_aux_work` | 주기 작업(카운터 wrap 방지 등) | 커널 워커 (`ptp_schedule_worker`) |

등록은 `ptp_clock_register(&info, dev)` → `/dev/ptpN` + `/sys/class/ptp/ptpN/`. mock PHC(`ptp_mock.c`)는 이 중 `adjfine`/`adjtime`/`gettime64`/`settime64`/`do_aux_work`만 채운 최소 예다 — 새 NIC의 PHC를 bring-up할 때 읽기 좋은 템플릿이다. 대부분의 MAC PHC는 내부 카운터 + `cyclecounter`/`timecounter`(`include/linux/timecounter.h`)로 구현하고, mock도 같은 방식(`timecounter_read`, `cc.mult` 조정으로 adjfine)이다.

패킷 타임스탬프 쪽(I6 §9): 드라이버는 `get_ts_info`에 PHC 번호와 HW 모드를 알리고, `ndo_hwtstamp_set`으로 켜고, TX 완료 시 `skb_tstamp_tx()`, RX에서 `skb_hwtstamps(skb)->hwtstamp`를 채운다. 소켓의 `SO_TIMESTAMPING` cmsg로 ptp4l이 받는다.

### 12.2 PPS의 경로

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="30" width="140" height="60" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="80" y="55" font-size="13" text-anchor="middle">GPIO IRQ</text>
  <text x="80" y="74" font-size="12" text-anchor="middle">pps-gpio hardirq</text>
  <rect x="180" y="30" width="150" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="255" y="55" font-size="13" text-anchor="middle">pps_get_ts()</text>
  <text x="255" y="74" font-size="12" text-anchor="middle">ktime_get_snapshot</text>
  <rect x="360" y="30" width="140" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="430" y="55" font-size="13" text-anchor="middle">pps_event()</text>
  <text x="430" y="74" font-size="12" text-anchor="middle">assert_tu, seq++</text>
  <rect x="530" y="30" width="140" height="60" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="600" y="55" font-size="13" text-anchor="middle">wake_up</text>
  <text x="600" y="74" font-size="12" text-anchor="middle">PPS_FETCH 반환</text>
  <line x1="150" y1="60" x2="176" y2="60" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="330" y1="60" x2="356" y2="60" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="500" y1="60" x2="526" y2="60" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="180" y="140" width="150" height="56" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="255" y="164" font-size="13" text-anchor="middle">hardpps()</text>
  <text x="255" y="182" font-size="12" text-anchor="middle">CONFIG_NTP_PPS</text>
  <rect x="360" y="140" width="140" height="56" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="430" y="164" font-size="13" text-anchor="middle">sysfs assert</text>
  <text x="430" y="182" font-size="12" text-anchor="middle">초.나노초#seq</text>
  <rect x="530" y="140" width="140" height="56" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="600" y="164" font-size="13" text-anchor="middle">chrony / gpsd</text>
  <text x="600" y="182" font-size="12" text-anchor="middle">ppstest, ts2phc</text>
  <line x1="400" y1="90" x2="300" y2="136" stroke="#888" stroke-dasharray="4 3" marker-end="url(#ah6)"/>
  <line x1="430" y1="90" x2="430" y2="136" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="600" y1="90" x2="600" y2="136" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="340" y="232" font-size="12" text-anchor="middle">hardpps(커널 안 PLL)는 NO_HZ 커널에서 못 켠다 → 실무에서는 chrony가 유저 공간에서 PPS를 소비한다</text>
</svg>
```
그림 8 — PPS 이벤트 경로: IRQ에서 시각을 찍고(`pps_get_ts`), 시퀀스를 올리고, 기다리던 유저를 깨운다

- 6.18 `drivers/pps/Kconfig`: `config NTP_PPS ... depends on !NO_HZ_COMMON`. 이 랩 커널(NO_HZ)에서도 `# CONFIG_NTP_PPS`가 꺼져 있다. 그래서 커널 안에서 PPS로 시계를 직접 끄는 `hardpps()` 경로는 대부분의 현대 커널에서 쓸 수 없고, chrony가 `PPS_FETCH`로 엣지를 받아 `adjtimex`로 보정한다.
- `pps_event()`는 IRQ 컨텍스트에서 불려도 되게 spinlock + wait queue로 되어 있다(G6의 `poll`/wait queue와 같은 패턴).

### 12.3 시스템 시계 보정의 끝 — `adjtimex`

ptp4l(SW 모드), phc2sys(`-c CLOCK_REALTIME`), chronyd는 결국 모두 `clock_adjtime(CLOCK_REALTIME, ...)`(= `adjtimex`)로 커널 timekeeping의 **주파수(`freq`)** 를 바꾸거나 **오프셋을 한 번에 더한다(`ADJ_SETOFFSET`)**. timekeeping 코어(`kernel/time/timekeeping.c`, `ntp.c`)는 clocksource의 `mult`를 조금씩 바꿔 REALTIME과 MONOTONIC의 **속도를 함께** 바꾼다. 그래서:

- PTP가 slew하면 MONOTONIC도 같이 빨라지거나 느려진다(예제 1의 MONOTONIC − RAW = 42 s가 증거). 센서 타임스탬프(MONOTONIC)가 PTP 시간축과 **같은 속도**로 흐르게 된다 — 이게 "MONOTONIC으로 찍고 REALTIME/TAI로 변환"이 가능한 이유다.
- step(`ADJ_SETOFFSET`, `clock_settime`)은 REALTIME만 점프시키고 MONOTONIC은 건드리지 않는다.

### 12.4 드라이버를 쓸 때 다시 만나는 지점

- V4L2 캡처 드라이버(I5 §12): SOF IRQ에서 `ktime_get_ns()`를 찍고 `q->timestamp_flags`에 **실제 찍는 위치**(`TSTAMP_SRC_SOE` 또는 기본 EOF)를 정직하게 적는다.
- IIO 드라이버(I2): `iio_pollfunc_store_time`을 top half로 쓰고, FIFO 센서는 하드웨어 타임스탬프나 ODR 역산을 쓴다. `iio_get_time_ns()`를 써서 사용자가 고른 시계를 존중한다.
- 네트워크 드라이버(I6): `get_ts_info`, `ndo_hwtstamp_set/get`, `ptp_clock_register`, `skb_tstamp_tx`.
- 외부 MCU 프로토콜(O3): MCU가 자기 타이머로 찍은 시각을 보내면, Linux 쪽에서 §5.2의 샌드위치(요청-응답 왕복)로 MCU 시계 ↔ MONOTONIC 대응을 추정한다 — 미니 PTP다.

---

## 13. Embedded 실무에서 다시 보기

### 13.1 센서 키트의 시간 아키텍처 (예를 들어 로봇이라면)

| 요소 | 결정 |
|---|---|
| 시간 기준 | GNSS 수신기의 PPS + UBX → 메인 SoC NIC의 extts 핀 → `ts2phc`로 PHC를 GNSS에 고정 |
| 네트워크 | 메인 SoC가 `ptp4l` GM(clockClass 6) → 스위치(BC/TC 지원) → LiDAR들(PTP slave) |
| 시스템 시계 | `phc2sys -s eth0 -c CLOCK_REALTIME -w` → MONOTONIC도 같은 속도 |
| 카메라 | PHC perout 30 Hz(정각에 위상 고정) → 디시리얼라이저 FSYNC 입력 → 모든 카메라 FSIN. V4L2 SOF 타임스탬프(MONOTONIC) |
| IMU | 같은 30 Hz 펄스를 IMU FSYNC에 → 샘플에 sync 플래그. IIO `current_timestamp_clock = monotonic` |
| 퓨전 입력 | 모든 타임스탬프를 MONOTONIC으로 통일, LiDAR 패킷 시각(TAI)은 phc2sys 오프셋으로 변환 |
| GNSS 없을 때 | 메인 SoC가 free-run GM. 장비 내부 상대 동기는 유지되고 절대 시각만 없다 |

### 13.2 ROS 2에서 (Q6 연결)

`sensor_msgs/msg/Image`의 `header.stamp`에 **드라이버가 준 하드웨어 타임스탬프**를 넣어야 한다. 흔한 실수는 노드가 메시지를 만들 때 `now()`를 넣는 것(그림 3의 ⑤). ROS 2의 `rclcpp::Clock`은 기본이 `RCL_ROS_TIME`(= 시뮬레이션 아니면 system time = REALTIME)이라, MONOTONIC 타임스탬프를 넣으려면 변환(REALTIME = MONOTONIC + (REALTIME − MONOTONIC), 같은 순간 읽기)이 필요하다. `message_filters`의 ApproximateTime 동기화도 결국 이 stamp만 본다.

### 13.3 LED 깜빡임 실험 — 카메라-IMU 지연 실측

HW trigger가 없는 장비의 지연 측정 방법(Don이 scope로 하던 일과 같은 정신):

1. MCU가 LED를 켜는 순간 GPIO 엣지를 SoC에 보내 `pps-gpio`나 GPIO 이벤트(`gpiod` 이벤트 타임스탬프, H4)로 MONOTONIC 시각을 찍는다.
2. 카메라가 LED가 처음 밝아진 프레임의 V4L2 타임스탬프를 본다.
3. 차이 = 카메라 파이프라인 지연(+ 노출 위치). 수백 번 반복해 평균(고정 지연)과 분산(지터)을 구한다.
4. 고정 지연은 퓨전 설정에 오프셋으로 넣고, 지터는 Q7 대시보드로 감시한다.

### 13.4 health 모니터 (Q7 연결)

```sh
# 30초마다 동기 상태를 로그로 — 녹화 데이터에 같이 저장해 두면 사후 분석이 쉽다
while sleep 30; do
  pmc -u -b 0 'GET TIME_STATUS_NP' | awk '/master_offset/{o=$2} /gmPresent/{g=$2} END{print "ptp", o, g}'
  phc_ctl /dev/ptp0 cmp 2>&1 | awk '/offset/{print "phc-sys", $(NF)}'
  cat /sys/class/pps/pps0/assert
done
```

(가이드: 이 루프 자체는 실행하지 않았다. 구성 요소인 `pmc`, `phc_ctl cmp`, sysfs `assert`의 출력 형식은 §7~§9에서 확인했다.)

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| IIO/evdev 기본 시계(REALTIME)를 그대로 둠 | IMU와 카메라 타임스탬프가 수십 년 차이, 정렬 실패 | IIO `clock_id` 기본 REALTIME (I2 §10) | `current_timestamp_clock`에 `monotonic`, evdev는 `EVIOCSCLOCKID` |
| 퓨전 시간축으로 REALTIME 사용 | 부팅 직후 또는 GNSS 잡는 순간 dt 음수, 필터 발산 | NTP/PTP/chrony의 step | MONOTONIC으로 찍고, 절대 시각은 메타데이터로 |
| ptp4l·chronyd·timesyncd가 동시에 한 시계를 잡음 | `clockcheck: clock frequency changed unexpectedly!`, offset 진동 | 두 servo의 싸움 (§7.5) | 시계 하나에 servo 하나. 다른 데몬 disable |
| `ptp4l -S`로 µs 정확도를 기대 | offset이 수십 µs~ms에서 흔들림, 가끔 스파이크 | SW 타임스탬프의 스택·IRQ 지연 (§7.4) | HW 타임스탬핑 NIC, `-H` + `phc2sys` |
| `-H`인데 `phc2sys`를 안 띄움 | ptp4l은 s2인데 앱 타임스탬프는 틀림 | PHC만 맞고 시스템 시계는 free-run | `phc2sys -s ethX -c CLOCK_REALTIME -w` (또는 `-a -r`) |
| `CLOCK_TAI` 사용인데 TAI 오프셋 미설정 | TSN 스케줄·`etf` 송신 시각이 37 s 어긋남 | 커널 `tai = 0` (§1 예제 1) | `phc2sys -w`, ptp4l/chrony `leapsectz` 설정 확인 |
| 유저 공간 루프로 trigger GPIO 토글 | 카메라 간 노출 위상이 ms 단위로 흔들림 | 스케줄링 지터 | PHC perout, PWM, MCU/FPGA trigger |
| V4L2 타임스탬프를 노출 중심으로 가정 | 빠른 회전에서 일정한 오프셋 | SOF/EOF 정의를 안 확인, 플래그와 실제 위치 불일치(rp1-cfe 사례) | 드라이버 코드 확인 + LED 실험으로 실측 |
| pps-ktimer로 chrony 테스트 | chrony가 PPS를 계속 거부 | 위상이 매초 20~30 ms 흘러감 (§9.2) | 테스트는 `ppstest`까지만, 진짜 PPS는 GNSS + `pps-gpio` |
| 비대칭 링크(스위치 큐, 미디어 컨버터) 무시 | 장비 간 고정 오프셋 수백 ns~µs, 측정해도 안 사라짐 | PTP는 비대칭을 볼 수 없다 (§6.2) | TC/BC 스위치, `delayAsymmetry`/latency 보정, 1PPS 출력끼리 scope 비교 |

---

## 15. 면접에서 이렇게 말한다

**Q.** How do you time-synchronize a camera, an IMU and a LiDAR on a robot?

**A.** 세 층으로 나눠 답한다. 먼저 샘플링 순간 — 카메라끼리는 HW trigger/frame sync로 노출을 같은 엣지에 묶고, IMU는 그 펄스를 sync 입력으로 받는다. 둘째, 타임스탬프 — 드라이버가 SOF IRQ나 FIFO IRQ에서 CLOCK_MONOTONIC으로 찍고, 앱이 `now()`를 찍지 않게 한다. 셋째, 시계 정렬 — LiDAR는 PTP slave로 두고, SoC는 GNSS PPS로 PHC를 맞춰 GM이 되고, `phc2sys`로 시스템 시계를 PHC에 맞춘다. 그러면 모든 타임스탬프를 하나의 시간축으로 변환할 수 있다.

> I split it into three layers. First, make the samples happen at the same instant: the cameras share a hardware trigger or frame-sync line, ideally generated by the NIC's PHC as a periodic output so it's phase-locked to PTP time, and the IMU takes the same pulse on its sync input. Second, timestamp as early as possible: the camera driver stamps at the start-of-frame interrupt and the IMU driver at the FIFO interrupt, both in CLOCK_MONOTONIC, never in the application. Third, align the clocks: the LiDAR is a PTP slave, the SoC disciplines its PHC to GNSS PPS with ts2phc and acts as grandmaster, and phc2sys steers the system clock to the PHC. Then every timestamp can be mapped to one timeline, and I verify the residual offset with an LED or PPS test.

**꼬리 질문**: "Why not just timestamp in user space when the frame arrives?" → 지연이 일정하지 않아서(스케줄링·버퍼링) 보정이 안 된다. 내 측정에서 SW 스탬프는 평균 0.64 ms인데 최대 오차가 80 cm였다(20 m/s).

**Q.** Explain how PTP computes the clock offset.

**A.** master가 Sync를 t1에 보내고 slave가 t2에 받는다. slave가 Delay_Req를 t3에 보내고 master가 t4에 받는다. 오프셋 = ((t2−t1) − (t4−t3))/2, 경로 지연 = ((t2−t1) + (t4−t3))/2. 두 방향 지연이 같다는 가정이 있어서, 비대칭의 절반이 그대로 오프셋 오차가 된다. two-step이면 t1은 Follow_Up으로 온다.

> The master sends Sync at t1 and the slave receives it at t2; the slave sends Delay_Req at t3 and the master receives it at t4 and returns t4 in Delay_Resp. With a two-step clock t1 comes in a Follow_Up. Offset is ((t2 minus t1) minus (t4 minus t3)) over two and mean path delay is the sum over two. The key assumption is a symmetric path: half of any asymmetry shows up directly as offset error, and PTP can't detect it, so you remove it with hardware timestamps and transparent or boundary clocks, and calibrate the remaining fixed asymmetry.

**Q.** What's the difference between hardware and software timestamping, and how do you check what a NIC supports?

**A.** SW는 커널 네트워크 스택에서 찍어서 IRQ·coalescing·스케줄링 지연이 들어가고, HW는 MAC/PHY가 SFD 순간 PHC를 래치한다. `ethtool -T`로 hardware-transmit/receive 모드와 PHC 번호를 본다. 내 QEMU 랩에서 SW PTP는 오프셋 중앙값 0.8 ms에 116 ms 스파이크까지 났고, 같은 기계 안 PHC-시스템 동기(phc2sys)는 ±100 ns였다.

> Software timestamps are taken in the network stack, so they include interrupt latency, coalescing and scheduling noise; hardware timestamps are latched by the MAC or PHY when the start-of-frame delimiter crosses the wire, in the PHC's timebase. ethtool -T shows the capabilities and the PHC index. In my emulated lab, software PTP between two VMs had a median offset of about 0.8 ms with a 116 ms spike from a host stall, while phc2sys between a PHC and the system clock in the same machine held about plus or minus 100 ns — the timestamp location dominates the accuracy.

**Q.** Which clock should sensor timestamps use, and why not CLOCK_REALTIME?

**A.** MONOTONIC. REALTIME은 NTP/PTP의 step이나 `date -s`로 점프해서 dt가 음수가 될 수 있다. MONOTONIC은 점프하지 않지만 NTP/PTP가 속도를 같이 보정하므로 PTP 시간축과 같은 속도로 흐른다. 절대 시각이 필요하면 MONOTONIC→REALTIME/TAI 오프셋을 같이 기록한다. IIO 기본이 REALTIME이라는 함정도 말한다.

> CLOCK_MONOTONIC. REALTIME can step when NTP or PTP first locks or someone sets the date, which gives negative dt to the fusion filter. MONOTONIC never jumps, but the kernel slews its rate together with REALTIME, so it runs at the disciplined rate. If I need absolute time I log the MONOTONIC-to-TAI offset alongside. One gotcha: IIO and evdev default to REALTIME, so the bring-up checklist switches them to monotonic.

**Q.** What do ptp4l's s0, s1, s2 mean, and what does "clock frequency changed unexpectedly" tell you?

**A.** s0은 servo unlocked(주파수 추정 중), s1은 step 한 번, s2는 locked(주파수만 slew). clockcheck 경고는 다른 프로세스가 같은 시계를 조정한다는 뜻 — chronyd나 timesyncd, 두 번째 ptp4l. 랩에서 실수로 ptp4l 두 개를 띄웠을 때 정확히 이 메시지가 매초 나왔다.

> s0 is unlocked while the servo estimates the frequency, s1 is the one-time step, and s2 is locked, where it only slews frequency. The clockcheck warning means something else is adjusting the same clock — usually chronyd, timesyncd or a second ptp4l or phc2sys instance. I reproduced it by accident with two ptp4l instances on one VM; the rule is one servo per clock.

**Q.** How would you add a PPS input from a GNSS module to a Linux board?

**A.** 가능하면 NIC의 extts 핀에 넣어 `ts2phc`로 PHC를 맞춘다(HW 캡처). 아니면 GPIO에 넣고 DT에 `pps-gpio` 노드를 쓴 다음 `ppstest`로 간격 흔들림을 확인하고, chrony에서 gpsd의 NMEA(SHM)와 `lock`으로 묶는다. 엣지 극성, 레벨 시프터 지연, NO_HZ라 hardpps가 없다는 점도 챙긴다.

> Best is to route it to a PHC external-timestamp pin and run ts2phc, so the edge is captured in hardware. Otherwise I put it on a GPIO with a pps-gpio device tree node, check the interval jitter with ppstest, and feed chrony a PPS refclock locked to gpsd's NMEA time through SHM. I check the edge polarity, any level-shifter delay, and that the kernel's hardpps path isn't available on NO_HZ kernels, so chrony does the disciplining in user space.

**Q.** Two cameras are free-running at 30 fps with perfect timestamps. Is that good enough for stereo?

**A.** 아니다. 타임스탬프가 정확해도 두 노출은 0~33 ms 다른 순간이고 영상은 보간할 수 없다. 노출 자체를 trigger/frame sync로 묶어야 한다. IMU처럼 연속 신호는 보간이 되지만 영상은 안 된다.

> No. Perfect timestamps only tell you how far apart the exposures were; with free-running sensors the phase drifts anywhere within 33 ms, and you can't interpolate an image. For stereo the exposures themselves must be locked with a shared trigger or master-slave frame sync. Interpolation works for continuous signals like IMU data, not for pixels.

**Q.** What accuracy can you expect from PTP, and what limits it?

**A.** HW 타임스탬핑 + PTP 인식 스위치면 수십 ns 이하, SW면 µs~수십 µs(실제 HW 기준, 기억 기반). 한계는 타임스탬프 위치, 경로 비대칭, 스위치 큐(TC/BC 없을 때), 발진기 잡음과 servo 대역폭, sync 주기. gPTP는 7홉에서 1 µs 목표.

> With hardware timestamping and PTP-aware switches, tens of nanoseconds or better; with software timestamps, microseconds to tens of microseconds on real hardware. The limits are where the timestamp is taken, path asymmetry, queuing in switches that aren't transparent or boundary clocks, oscillator noise versus servo bandwidth, and the sync interval. 802.1AS targets about a microsecond across seven hops.

---

## 16. 직접 해보기

1. **오차 예산 계산**: 드론이 15 m/s, yaw rate 120 °/s로 난다. 카메라(화각 80°, 가로 1280 px)와 IMU 사이 허용 시간 오차는 위치 1 cm, 회전 1 px 기준으로 각각 얼마인가?
   정답/힌트: 위치 1 cm / 15 m/s ≈ 0.67 ms. 1 px = 80/1280 = 0.0625°, 0.0625 / 120 ≈ 0.52 ms. 더 빡빡한 쪽(0.52 ms)이 예산이다.
2. **비대칭**: `q3_ptpcalc`를 고쳐 Sync 경로에만 0~2000 ns 균일 분포 지연(큐잉)을 넣고 1000번 반복해 offset 오차의 평균과 표준편차를 출력하라. 평균은 왜 0이 아닌가?
   정답/힌트: 오차 = (d_ms − d_sm)/2이므로 평균 지연 1000 ns의 절반인 +500 ns가 바이어스로 남는다. 최소값 필터(가장 작은 지연 샘플 사용)를 쓰면 줄어든다.
3. **ptp4l 설정 실험**: 게스트 두 대 실습에서 slave에 `--step_threshold=0.002`를 주고 다시 돌려 보라. §7.4의 116 ms 스파이크 같은 상황에서 무엇이 달라지나? 왜 센서 녹화 중에는 위험한가?
   정답/힌트: s2에서도 2 ms를 넘으면 step한다. 스파이크(가짜 오프셋)에 반응해 시계를 점프시킬 수 있어서 REALTIME 기반 데이터에 불연속이 생긴다. 녹화 중에는 slew만 하는 편이 안전하다.
4. **phc2sys 방향 바꾸기**: netdevsim mock PHC를 기준으로 `phc2sys -s /dev/ptp0 -c CLOCK_REALTIME -O 0 -m`을 돌리고, 동시에 `q3_clocks`로 MONOTONIC − MONO_RAW가 변하는지 보라.
   정답/힌트: phc2sys가 시스템 시계 주파수를 조정하면 MONOTONIC도 같은 비율로 바뀐다(§12.3). PHC에 `phc_ctl freq 100000`을 걸면 시스템 시계가 따라가며 MONOTONIC − RAW가 초당 약 100 µs씩 변한다.
5. **IIO 시계 확인**: `iio_dummy` + hrtimer trigger(I2 §5)로 버퍼 캡처를 하면서 `current_timestamp_clock`을 realtime → monotonic으로 바꾸고, `q3_align`처럼 MONOTONIC 기준 스트림과 섞어 정렬이 깨지는/고쳐지는 것을 보여라.
   정답/힌트: I2 §10의 `i2clk` 프로그램으로 샘플 ts가 어느 시계와 같은지 먼저 확인한다.
6. **PPS 간격 관찰**: `pps-ktimer`를 올린 상태에서 `q3_pps /dev/pps0 30`을 돌리면서 다른 터미널에서 `stress-ng --cpu 2 --timeout 20`을 걸어라. 간격 오차 분포가 어떻게 변하나? timer wheel 반올림(최대 32 ms)과 부하 영향을 구분할 수 있나?
   정답/힌트: 기본 분포는 +16~+31 ms(반올림)이고, 부하는 그 위에 꼬리를 더한다. `HZ=250`에서 1 s 타이머의 granularity는 8 tick = 32 ms다(F8 §6.5).
7. **관찰 과제**: 실제 장비나 노트북에서 `ethtool -T <iface>`, `ls /sys/class/ptp/`, `cat /sys/class/ptp/ptp*/clock_name`을 확인하고 HW 타임스탬핑 지원 여부를 표로 정리하라.
   정답/힌트: 유선 Intel/Realtek NIC 다수는 PHC가 있고, Wi-Fi는 보통 SW만 있다. VM의 virtio-net은 PHC가 없다(이 노트 §7.1). KVM 게스트라면 `ptp_kvm`이 호스트 시계를 PHC로 노출할 수 있다(이 랩은 TCG라 해당 없음).

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| HW trigger | hardware trigger | 외부 엣지 한 번 = 센서 노출 한 번 |
| frame sync (FSIN/XVS) | 프레임 동기 입력 | master 센서의 VSYNC에 slave 센서가 프레임 시작을 맞춤 |
| SOF / EOF | Start/End Of Frame | CSI 수신기가 프레임 시작·끝에 내는 이벤트(IRQ) |
| SOE | Start Of Exposure | V4L2 `TSTAMP_SRC_SOE`: 노출 시작 순간 타임스탬프 |
| PPS | Pulse Per Second | 매 초 정각 엣지, 위상 기준 |
| PHC | PTP Hardware Clock | NIC 안 하드웨어 시계, `/dev/ptpN` |
| PTP | Precision Time Protocol (IEEE 1588) | 패킷 타임스탬프로 시계를 맞추는 프로토콜 |
| gPTP | generalized PTP (IEEE 802.1AS) | TSN·차량용 PTP 프로파일, L2 + peer delay |
| GM | Grandmaster | PTP 도메인의 기준 시계 |
| BMCA | Best Master Clock Algorithm | Announce 속성을 비교해 GM을 고르는 규칙 |
| BC / TC | Boundary / Transparent Clock | 스위치에서 시각을 재생성 / 체류 시간을 보정 |
| E2E / P2P | end-to-end / peer-to-peer delay | Delay_Req/Resp vs Pdelay_Req/Resp |
| two-step | 2단계 | t1을 Follow_Up으로 나중에 보냄 |
| servo | 서보 | 오프셋 측정값으로 주파수를 제어하는 PI 루프 (s0/s1/s2) |
| extts / perout | external timestamp / periodic output | PHC 핀의 엣지 캡처 / 주기 펄스 출력 |
| `phc2sys` | — | PHC ↔ 시스템 시계 동기 도구 |
| `ts2phc` | — | PPS(+NMEA)로 PHC를 맞추는 도구 |
| `pmc` | PTP management client | ptp4l 상태 조회·설정 |
| TAI | International Atomic Time | 윤초 없는 시간축, 현재 UTC + 37 s |
| slew / step | — | 주파수를 바꿔 천천히 맞춤 / 시각을 한 번에 점프 |
| taprio / etf | — | TSN 시간 슬롯 게이트 qdisc / 지정 시각 송신 qdisc |

---

## 18. 요약 & 체크리스트

멀티 센서 동기화는 세 층이다. (1) **샘플링 순간**은 HW trigger와 frame sync(FSIN/XVS, SerDes FSYNC 전달, PHC perout)로 묶는다 — 영상은 보간할 수 없기 때문이다. (2) **타임스탬프**는 파이프라인의 가능한 한 앞(센서·수신 HW·hardirq)에서 `CLOCK_MONOTONIC`으로 찍는다 — 늦게 찍은 SW 스탬프는 평균은 괜찮아도 가끔 수십 ms 튀어 cm~m 오차가 된다(예제 3). (3) **시계 정렬**은 PPS(위상) + NMEA(초)로 외부 기준을 잡고, PTP(t1~t4, 대칭 가정)로 장비 사이를 맞추고, `phc2sys`로 PHC와 시스템 시계를 잇는다. 정확도는 프로토콜보다 **타임스탬프 위치**가 정한다: 이 랩에서 SW PTP는 ms급이었고, 같은 기계 안 PHC-시스템은 ±100 ns였다. 운영에서는 시계 하나에 servo 하나, `pmc`로 상태 감시, TAI 오프셋 설정을 지킨다.

- [ ] 속도·각속도와 시간 오차로 위치·픽셀 오차를 계산해 오차 예산을 세울 수 있다
- [ ] HW trigger, master/slave frame sync, SerDes FSYNC 전달, PHC perout의 차이를 설명할 수 있다
- [ ] 타임스탬프가 찍히는 다섯 지점과 각 지점의 지연·지터를 설명할 수 있다
- [ ] V4L2 `TIMESTAMP_MONOTONIC`, `TSTAMP_SRC_SOE/EOF`의 뜻과 "플래그를 믿지 말고 코드를 읽어라"를 설명할 수 있다
- [ ] REALTIME/TAI/MONOTONIC/RAW/BOOTTIME 중 센서 타임스탬프에 쓸 시계와 이유를 말할 수 있다
- [ ] PTP offset·delay 공식을 유도하고 비대칭이 오차가 되는 이유를 설명할 수 있다
- [ ] BMCA, BC/TC, E2E/P2P, one-step/two-step, gPTP의 특징을 말할 수 있다
- [ ] `ptp4l -S/-H`, `pmc`, `phc_ctl`, `phc2sys`로 동기를 세우고 s0/s1/s2와 포트 상태를 읽을 수 있다
- [ ] `PTP_SYS_OFFSET` 샌드위치로 PHC-시스템 오프셋을 재는 C 코드를 짤 수 있다
- [ ] `ppstest`/`PPS_FETCH`로 PPS 소스를 검증하고 pps-ktimer가 왜 기준이 못 되는지 설명할 수 있다
- [ ] chrony + gpsd(SHM + PPS lock)와 ts2phc 구성의 차이를 설명할 수 있다
- [ ] "clock frequency changed unexpectedly"와 `tai=0` 같은 운영 함정을 진단할 수 있다

## 참고 자료

- IEEE 1588-2019 (PTP), IEEE 802.1AS-2020 (gPTP) — 표준 문서
- linuxptp 프로젝트: https://linuxptp.sourceforge.net/ (ptp4l(8), phc2sys(8), pmc(8), phc_ctl(8), ts2phc(8) man 페이지)
- 커널 문서: https://docs.kernel.org/driver-api/ptp.html (PTP hardware clock infrastructure), https://docs.kernel.org/driver-api/pps.html (PPS), https://docs.kernel.org/networking/timestamping.html (SO_TIMESTAMPING)
- V4L2 버퍼 플래그: https://docs.kernel.org/userspace-api/media/v4l/buffer.html
- IIO 타임스탬프 ABI: 6.18 `Documentation/ABI/testing/sysfs-bus-iio`의 `current_timestamp_clock`
- 커널 소스(6.18.55): `drivers/ptp/ptp_chardev.c`, `drivers/ptp/ptp_mock.c`, `include/linux/ptp_clock_kernel.h`, `include/uapi/linux/ptp_clock.h`, `drivers/pps/clients/pps-ktimer.c`, `drivers/pps/clients/pps-gpio.c`, `include/uapi/linux/pps.h`, `drivers/media/usb/uvc/uvc_queue.c`, `drivers/media/platform/raspberrypi/rp1-cfe/cfe.c`, `drivers/media/i2c/max9286.c`, `drivers/media/i2c/ds90ub960.c`, `net/sched/sch_taprio.c`, `net/sched/sch_etf.c` — https://elixir.bootlin.com/linux/v6.18/source
- chrony 문서: https://chrony-project.org/documentation.html (chrony.conf `refclock`)
- RFC 2783 — Pulse-Per-Second API for UNIX-like Operating Systems
- 관련 노트: F8(시계·타이머), I2(IIO), I5(V4L2), I6 §9(SO_TIMESTAMPING), A9(소켓), N3(지연 측정), O3(외부 MCU), Q1(카메라 HW), Q5(GNSS·IMU·LiDAR), Q6(ROS 2), Q7(품질 툴)
