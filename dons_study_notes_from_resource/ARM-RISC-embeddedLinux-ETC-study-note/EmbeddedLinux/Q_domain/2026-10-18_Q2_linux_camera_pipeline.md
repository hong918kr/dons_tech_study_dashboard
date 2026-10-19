# Q2. Linux 카메라 파이프라인 — media-ctl · v4l2-ctl · libcamera · GStreamer로 "카메라가 안 나온다"를 푸는 실무 디버깅

> **이 노트를 다 읽으면**: `media-ctl -p` 출력을 보고 센서 → (CSI-2 수신기) → ISP → video node 그래프를 머릿속에 그리고 어느 링크가 켜져 있는지 말할 수 있다 · `media-ctl -V`/`-l`과 `v4l2-ctl --set-fmt-video`로 파이프라인 전체 포맷을 맞추고, STREAMON `Broken pipe`(-EPIPE)가 어느 링크에서 났는지 dmesg로 찾아 고칠 수 있다 · `v4l2-ctl --stream-mmap --verbose`의 sequence·timestamp·flags로 fps와 드롭 프레임을 측정하고, 버퍼 수·소비자 속도·드라이버 드롭을 구분할 수 있다 · libcamera(`cam`)와 GStreamer(`v4l2src`, caps 협상, `fpsdisplaysink`, `io-mode=dmabuf`)가 같은 커널 객체를 어떻게 쓰는지, 그리고 그 층에서만 생기는 고장(IPA 없음, udev 없음, not-negotiated)을 진단할 수 있다
> **왜 필요한가**: 로봇·드론·차량 카메라 bring-up의 실제 하루는 "드라이버는 probe됐는데 프레임이 안 나온다", "STREAMON: Broken pipe", "30 fps라더니 27 fps", "GStreamer가 not-negotiated", "libcamera가 카메라를 못 찾는다"이고, 면접은 "카메라가 안 나올 때 무엇부터 보나"를 묻는다
> **Don 기준 난이도**: 링버퍼 소유권, 프레임 카운터로 드롭 잡기, 레지스터 설정 순서가 틀리면 HW가 멈추는 것 — SSD·RF bring-up에서 이미 하던 사고방식이다 / 새로 배울 것은 **도구의 문법**(media-ctl의 entity:pad 표기, v4l2-ctl 옵션, GStreamer caps), **그래프 상태가 전역이라는 사실**, 그리고 libcamera·GStreamer 층의 고장 모드
> **선행 노트**: I5(V4L2·media controller 객체 모델, uAPI를 C로 직접 부르기, vb2 내부, DT 그래프·async 코드 — 이 노트는 I5의 "도구판·실무판"이다), G5(ioctl·mmap), H9(DMA·dma-buf), F10(CMA), M1(dynamic debug), L2(udev) — Q1(CSI-2 HW), Q3(멀티 센서 동기화·타임스탬프), Q6(ROS 2), Q7(데이터 품질 툴), O4(dma-buf로 가속기에 넘기기)로 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 I5와 무엇이 다른가

I5는 BusyBox 게스트에 도구가 없어서 V4L2·MC ioctl을 **C로 직접** 불렀다. 덕분에 `MEDIA_IOC_G_TOPOLOGY`, `VIDIOC_SUBDEV_S_FMT`, `VIDIOC_QBUF`가 무슨 일을 하는지는 이미 안다. 하지만 실무 보드에서 쓰는 것은 그 C 코드가 아니라 **`media-ctl`, `v4l2-ctl`, `cam`, `gst-launch-1.0`** 이다. 버그 리포트에 붙는 것도 이 도구들의 출력이고, 현장 엔지니어끼리 말하는 언어도 이것이다.

이 노트는 공용 랩의 새 rootfs(`rootfs-dom`: Ubuntu 24.04 + v4l-utils 1.26.1 + GStreamer 1.24.2 + libcamera 0.2.0)를 Image-dom(6.18.55-dom) 커널로 부팅해서, **같은 vimc·vivid 가상 카메라를 실제 도구로** 다룬다. 목표는 "도구 출력 한 줄을 보고 어느 층이 고장인지 아는 눈"이다.

### 0.2 실제 SoC와 vimc를 나란히

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" font-size="13">실제 SoC (예: IMX219 → i.MX8MP)</text>
  <rect x="10" y="32" width="120" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="70" y="54" text-anchor="middle" font-size="13">센서 subdev</text>
  <text x="70" y="73" text-anchor="middle" font-size="12">I2C + MIPI</text>
  <rect x="150" y="32" width="120" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="210" y="54" text-anchor="middle" font-size="13">CSI-2 수신기</text>
  <text x="210" y="73" text-anchor="middle" font-size="12">subdev</text>
  <rect x="290" y="32" width="120" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="350" y="54" text-anchor="middle" font-size="13">ISP / crossbar</text>
  <text x="350" y="73" text-anchor="middle" font-size="12">subdev</text>
  <rect x="430" y="32" width="120" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="490" y="54" text-anchor="middle" font-size="13">DMA writer</text>
  <text x="490" y="73" text-anchor="middle" font-size="12">/dev/videoN</text>
  <line x1="130" y1="58" x2="148" y2="58" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="270" y1="58" x2="288" y2="58" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="410" y1="58" x2="428" y2="58" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="10" y="128" font-size="13">vimc (이 노트의 랩)</text>
  <rect x="10" y="140" width="120" height="52" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="70" y="162" text-anchor="middle" font-size="13">Sensor A</text>
  <text x="70" y="181" text-anchor="middle" font-size="12">v4l-subdev0</text>
  <rect x="150" y="140" width="120" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
  <text x="210" y="162" text-anchor="middle" font-size="13">(없음)</text>
  <text x="210" y="181" text-anchor="middle" font-size="12">CSI 단계 생략</text>
  <rect x="290" y="140" width="120" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="350" y="162" text-anchor="middle" font-size="12">Debayer, Scaler</text>
  <text x="350" y="181" text-anchor="middle" font-size="12">subdev2 · 5</text>
  <rect x="430" y="140" width="120" height="52" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="490" y="162" text-anchor="middle" font-size="12">RGB/YUV Capture</text>
  <text x="490" y="181" text-anchor="middle" font-size="12">/dev/video2</text>
  <line x1="130" y1="166" x2="288" y2="166" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="410" y1="166" x2="428" y2="166" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="10" y="232" width="190" height="80" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="105" y="254" text-anchor="middle" font-size="13">media-ctl (/dev/mediaN)</text>
  <text x="105" y="274" text-anchor="middle" font-size="12">링크 켜기, pad 포맷</text>
  <text x="105" y="294" text-anchor="middle" font-size="12">-p, -l, -V, -r</text>
  <rect x="245" y="232" width="190" height="80" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="254" text-anchor="middle" font-size="13">v4l2-ctl (/dev/videoN)</text>
  <text x="340" y="274" text-anchor="middle" font-size="12">pixelformat, 컨트롤</text>
  <text x="340" y="294" text-anchor="middle" font-size="12">버퍼 돌리기, fps 측정</text>
  <rect x="480" y="232" width="190" height="80" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="575" y="254" text-anchor="middle" font-size="13">libcamera · GStreamer</text>
  <text x="575" y="274" text-anchor="middle" font-size="12">위 둘을 자동으로 하는</text>
  <text x="575" y="294" text-anchor="middle" font-size="12">앱 쪽 프레임워크</text>
  <line x1="105" y1="230" x2="210" y2="196" stroke="currentColor" stroke-dasharray="3 3" marker-end="url(#ah1)"/>
  <line x1="340" y1="230" x2="480" y2="196" stroke="currentColor" stroke-dasharray="3 3" marker-end="url(#ah1)"/>
  <line x1="575" y1="230" x2="560" y2="196" stroke="currentColor" stroke-dasharray="3 3" marker-end="url(#ah1)"/>
</svg>
```
그림 1 — 실제 SoC 카메라와 vimc의 대응, 그리고 각 층을 만지는 도구. media-ctl은 그래프(subdev·pad·link)를, v4l2-ctl은 끝단 video node를, libcamera·GStreamer는 그 둘을 앱 대신 해 준다. vimc에는 CSI-2 수신기가 없지만 "센서 → 처리 블록 → DMA 노드"라는 구조와 고장 모드는 같다.

bare-metal로 번역하면: 카메라 파이프라인은 **체인으로 연결된 DMA 엔진들**이다. 각 블록(센서, CSI-2 RX, ISP, DMA)은 자기 입력·출력 포트 설정 레지스터(= pad 포맷)를 갖고, 체인의 앞뒤 설정이 다르면 HW는 이상한 데이터를 내거나 멈춘다. Linux는 그걸 STREAMON 순간에 소프트웨어로 미리 검사해서 `-EPIPE`로 거절한다 — **링크 검증(link validation)** 이다.

### 0.3 디버깅의 층

| 층 | 무엇이 고장나나 | 보는 도구 |
|---|---|---|
| probe | 센서가 I2C에 안 보임, async 매칭 실패, `/dev/v4l-subdevN` 없음 | dmesg, `media-ctl -p`, debugfs `v4l2-async` |
| 링크 | 원하는 경로의 링크가 꺼져 있음, 다른 프로세스가 바꿈 | `media-ctl -p`, `-l`, `-r` |
| 포맷 | 링크 양끝의 mbus code·크기 불일치 → `-EPIPE` | `media-ctl -V`, `v4l2-ctl -v`, dyndbg |
| 스트리밍 | STREAMON은 되는데 DQBUF가 안 옴 (CSI 에러, 센서 stream off) | `v4l2-ctl --stream-mmap`, IRQ 카운터, 드라이버 로그 |
| 버퍼 | 버퍼가 모자라거나 소비자가 느려서 sequence가 건너뜀 | `--verbose`의 seq, 이 노트의 `q2_seqmon` |
| 타임스탬프 | flags가 기대와 다름(SoE/EoF), 간격 지터 | `--verbose`의 ts·flags |
| 프레임워크 | libcamera IPA 없음, udev 없음, GStreamer caps 협상 실패 | `LIBCAMERA_LOG_LEVELS`, `GST_DEBUG`, `gst-launch -v` |

이 표가 §11의 진단 체크리스트와 §12의 결정 트리로 다시 나온다.

---

## 1. 랩 준비 — rootfs-dom을 부팅하고 vimc·vivid 올리기

### 1.1 부팅

공용 랩의 `rootfs-dom.ext4`는 읽기 전용 원본이다. qcow2 오버레이를 만들어 그 위에 쓴다(원본 복사 금지 — 디스크가 빠듯하다). 시리얼은 유닉스 소켓으로 빼서 Python 스크립트로 명령을 보내고 출력을 받았다.

```sh
# VM(elinux) 안에서 — /var/tmp/Q2 는 이 노트 전용 작업 폴더
qemu-img create -q -f qcow2 -F raw -b /var/lab/out/rootfs-dom.ext4 /var/tmp/Q2/q2.qcow2
qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 768M -display none \
  -kernel /var/lab/out/Image-dom -drive file=/var/tmp/Q2/q2.qcow2,if=virtio,format=qcow2 \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -serial unix:/var/tmp/Q2/s.sock,server,nowait -monitor none \
  -append "console=ttyAMA0 root=/dev/vda rw rootwait init=/bin/bash"
```

랩에서 실제로 겪은 일 두 가지를 먼저 적는다.

- 처음에는 systemd로 정상 부팅했다(`-m 1536M`). VM에서 다른 노트 작성자 18명이 동시에 QEMU를 돌리는 중이라(load average 19) 로그인까지 **약 14분**이 걸렸고, 몇 분 뒤 VM 호스트의 **OOM killer가 이 QEMU를 죽였다**(`Out of memory: Killed process 2420035 (qemu-system-aar) ... anon-rss:1323152kB`). §2~§3의 vimc 출력은 그 첫 부팅(systemd + udev 동작 중)에서 얻은 것이다.
- 두 번째부터는 `init=/bin/bash`로 systemd를 건너뛰고 `-m 768M`으로 줄였다. 7초 만에 셸이 뜬다. 대신 `/proc`, `/sys`, debugfs를 손으로 마운트하고(devtmpfs는 커널이 자동 마운트), **udev가 돌지 않는다** — 이 차이가 §8에서 libcamera 고장의 원인으로 다시 나온다.

이 VM은 KVM 없이 TCG(소프트웨어 CPU 에뮬레이션)로 돌고 호스트가 매우 붐빈다. 이 노트의 fps·지연·드롭 숫자는 **실제 HW 수치가 아니라** "도구로 이렇게 측정한다"를 보여 주는 값이다.

### 1.2 예제 1 — 모듈을 올리면 생기는 노드와 도구 버전

무엇을 확인하는 명령인지: vimc(media controller 파이프라인 흉내)와 vivid(단일 노드 테스트 카메라)를 올리고, 어떤 장치 노드가 생기는지 `v4l2-ctl --list-devices`로 본다.

```sh
uname -r; modprobe vimc; modprobe vivid
v4l2-ctl --list-devices
ls /dev/media* /dev/v4l-subdev*
v4l2-ctl --version; media-ctl --version
```

```text
6.18.55-dom
vimc (platform:vimc.0):
	/dev/video0
	/dev/video1
	/dev/video2
	/dev/media0

vivid (platform:vivid-000):
	/dev/video3
	/dev/video4
	/dev/video5
	/dev/video6
	/dev/radio0
	/dev/radio1
	/dev/vbi0
	/dev/vbi1
	/dev/swradio0
	/dev/v4l-touch0
	/dev/media1

/dev/media0       /dev/v4l-subdev1  /dev/v4l-subdev4  /dev/v4l-subdev7
/dev/media1       /dev/v4l-subdev2  /dev/v4l-subdev5
/dev/v4l-subdev0  /dev/v4l-subdev3  /dev/v4l-subdev6
v4l2-ctl 1.26.1
media-ctl 1.26.1
```

출력에서 볼 것: `/dev/v4l-subdevN`이 8개인데 전부 vimc 것이다(vivid는 subdev가 없는 video-centric 드라이버 — I5 §2.3). `/dev/videoN` 번호는 **모듈 로드 순서**로 정해진다. vivid를 먼저 올리면 vimc가 video4~6이 된다. 그래서 스크립트에서 `/dev/video2`를 하드코딩하면 안 되고, 이름(`/sys/class/video4linux/videoN/name`)이나 media 그래프의 entity 이름으로 찾아야 한다(§10.1).

```sh
for d in 3 4 5 6; do echo video$d: $(cat /sys/class/video4linux/video$d/name); done
```

```text
video3: vivid-000-vid-cap
video4: vivid-000-vid-out
video5: vivid-000-meta-cap
video6: vivid-000-meta-out
```

---

## 2. media controller 그래프 읽기 — `media-ctl -p`

### 2.1 직관

`media-ctl -p`는 SoC 카메라 블록도의 **텍스트 덤프**다. entity(블록) → pad(입출력 포트) → link(배선)의 3단 구조이고, pad마다 지금 설정된 포맷이 대괄호로 찍힌다. Don이 보드 회로도를 보면서 "이 핀이 저 핀에 붙었나"를 확인하던 것을 소프트웨어 그래프로 하는 것이다.

### 2.2 예제 2 — vimc 토폴로지 전체

무엇을 확인하는 명령인지: vimc media 장치의 모든 entity, pad, link, pad 포맷.

```sh
media-ctl -d /dev/media0 -p
```

```text
Media controller API version 6.18.55

Media device information
------------------------
driver          vimc
model           VIMC MDEV
serial          
bus info        platform:vimc.0
hw revision     0x0
driver version  6.18.55

Device topology
- entity 1: Sensor A (1 pad, 2 links, 0 routes)
            type V4L2 subdev subtype Sensor flags 0
            device node name /dev/v4l-subdev0
	pad0: Source
		[stream:0 fmt:RGB888_1X24/640x480 field:none colorspace:srgb]
		-> "Debayer A":0 [ENABLED,IMMUTABLE]
		-> "Raw Capture 0":0 [ENABLED,IMMUTABLE]

- entity 3: Sensor B (1 pad, 2 links, 0 routes)
            type V4L2 subdev subtype Sensor flags 0
            device node name /dev/v4l-subdev1
	pad0: Source
		[stream:0 fmt:RGB888_1X24/640x480 field:none colorspace:srgb]
		-> "Debayer B":0 [ENABLED,IMMUTABLE]
		-> "Raw Capture 1":0 [ENABLED,IMMUTABLE]

- entity 5: Debayer A (2 pads, 2 links, 0 routes)
            type V4L2 subdev subtype Unknown flags 0
            device node name /dev/v4l-subdev2
	pad0: Sink
		[stream:0 fmt:SRGGB8_1X8/640x480 field:none colorspace:srgb]
		<- "Sensor A":0 [ENABLED,IMMUTABLE]
	pad1: Source
		[stream:0 fmt:RGB888_1X24/640x480 field:none colorspace:srgb]
		-> "Scaler":0 []

- entity 8: Debayer B (2 pads, 2 links, 0 routes)
            type V4L2 subdev subtype Unknown flags 0
            device node name /dev/v4l-subdev3
	pad0: Sink
		[stream:0 fmt:SRGGB8_1X8/640x480 field:none colorspace:srgb]
		<- "Sensor B":0 [ENABLED,IMMUTABLE]
	pad1: Source
		[stream:0 fmt:RGB888_1X24/640x480 field:none colorspace:srgb]
		-> "Scaler":0 [ENABLED]

- entity 11: Raw Capture 0 (1 pad, 1 link)
             type Node subtype V4L flags 0
             device node name /dev/video0
	pad0: Sink
		<- "Sensor A":0 [ENABLED,IMMUTABLE]

- entity 15: Raw Capture 1 (1 pad, 1 link)
             type Node subtype V4L flags 0
             device node name /dev/video1
	pad0: Sink
		<- "Sensor B":0 [ENABLED,IMMUTABLE]

- entity 19: RGB/YUV Input (1 pad, 1 link, 0 routes)
             type V4L2 subdev subtype Sensor flags 0
             device node name /dev/v4l-subdev4
	pad0: Source
		[stream:0 fmt:RGB888_1X24/640x480 field:none colorspace:srgb]
		-> "Scaler":0 []

- entity 21: Scaler (2 pads, 4 links, 0 routes)
             type V4L2 subdev subtype Unknown flags 0
             device node name /dev/v4l-subdev5
	pad0: Sink
		[stream:0 fmt:RGB888_1X24/640x480 field:none colorspace:srgb
		 crop.bounds:(0,0)/640x480
		 crop:(0,0)/640x480]
		<- "Debayer A":1 []
		<- "Debayer B":1 [ENABLED]
		<- "RGB/YUV Input":0 []
	pad1: Source
		[stream:0 fmt:RGB888_1X24/640x480 field:none colorspace:srgb]
		-> "RGB/YUV Capture":0 [ENABLED,IMMUTABLE]

- entity 24: RGB/YUV Capture (1 pad, 1 link)
             type Node subtype V4L flags 0
             device node name /dev/video2
	pad0: Sink
		<- "Scaler":1 [ENABLED,IMMUTABLE]

- entity 28: Lens A (0 pad, 0 link, 0 routes)
             type V4L2 subdev subtype Lens flags 0
             device node name /dev/v4l-subdev6

- entity 29: Lens B (0 pad, 0 link, 0 routes)
             type V4L2 subdev subtype Lens flags 0
             device node name /dev/v4l-subdev7
```

### 2.3 출력 읽는 법 — 줄 단위 해설

| 출력 조각 | 뜻 |
|---|---|
| `entity 1: Sensor A (1 pad, 2 links, 0 routes)` | entity ID 1, 이름 "Sensor A". media-ctl에서 이 블록은 `"Sensor A"` 또는 숫자 `1`로 부른다 |
| `type V4L2 subdev subtype Sensor` | `v4l2_subdev`이고 function이 센서. libcamera가 센서를 찾을 때 이 subtype을 본다 |
| `type Node subtype V4L` | `video_device` — 버퍼가 나오는 DMA 끝점. 포맷은 pad가 아니라 `v4l2-ctl -d /dev/videoN`으로 본다 |
| `pad0: Source` / `pad0: Sink` | 출력 포트 / 입력 포트 |
| `[stream:0 fmt:SRGGB8_1X8/640x480 field:none colorspace:srgb]` | 그 pad의 active 포맷. `SRGGB8_1X8`은 **media bus code**(배선 위의 픽셀 형식), `RGB3` 같은 fourcc(메모리 배치)와 다르다(I5 §3.3) |
| `crop.bounds`, `crop` | selection API. 스케일러 입력에서 잘라낼 영역 |
| `-> "Debayer A":0 [ENABLED,IMMUTABLE]` | 이 pad에서 Debayer A의 pad 0으로 가는 링크, 켜져 있고 끌 수 없음 |
| `-> "Scaler":0 []` | 링크는 있지만 꺼져 있음 |
| `0 routes` | 한 entity 안 sink pad → source pad 경로(streams API, CSI-2 가상 채널 다중화에 쓰임). vimc는 안 씀 |

### 2.4 그림으로

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="40" width="140" height="48" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="80" y="60" text-anchor="middle" font-size="13">Sensor A</text>
  <text x="80" y="78" text-anchor="middle" font-size="12">/dev/v4l-subdev0</text>
  <rect x="10" y="100" width="140" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
  <text x="80" y="122" text-anchor="middle" font-size="12">Lens A (subdev6)</text>
  <rect x="10" y="200" width="140" height="48" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="80" y="220" text-anchor="middle" font-size="13">Sensor B</text>
  <text x="80" y="238" text-anchor="middle" font-size="12">/dev/v4l-subdev1</text>
  <rect x="10" y="260" width="140" height="34" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="4 3"/>
  <text x="80" y="282" text-anchor="middle" font-size="12">Lens B (subdev7)</text>
  <rect x="10" y="330" width="140" height="48" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="80" y="350" text-anchor="middle" font-size="13">RGB/YUV Input</text>
  <text x="80" y="368" text-anchor="middle" font-size="12">/dev/v4l-subdev4</text>
  <rect x="200" y="40" width="140" height="48" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="270" y="60" text-anchor="middle" font-size="13">Debayer A</text>
  <text x="270" y="78" text-anchor="middle" font-size="12">/dev/v4l-subdev2</text>
  <rect x="200" y="120" width="140" height="48" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="270" y="140" text-anchor="middle" font-size="13">Raw Capture 0</text>
  <text x="270" y="158" text-anchor="middle" font-size="12">/dev/video0</text>
  <rect x="200" y="200" width="140" height="48" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="270" y="220" text-anchor="middle" font-size="13">Debayer B</text>
  <text x="270" y="238" text-anchor="middle" font-size="12">/dev/v4l-subdev3</text>
  <rect x="200" y="280" width="140" height="48" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="270" y="300" text-anchor="middle" font-size="13">Raw Capture 1</text>
  <text x="270" y="318" text-anchor="middle" font-size="12">/dev/video1</text>
  <rect x="385" y="180" width="125" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="447" y="204" text-anchor="middle" font-size="13">Scaler</text>
  <text x="447" y="224" text-anchor="middle" font-size="12">/dev/v4l-subdev5</text>
  <rect x="535" y="180" width="140" height="60" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="605" y="204" text-anchor="middle" font-size="13">RGB/YUV Capture</text>
  <text x="605" y="224" text-anchor="middle" font-size="12">/dev/video2</text>
  <line x1="150" y1="64" x2="198" y2="64" stroke="currentColor" stroke-width="2.5" marker-end="url(#ah2)"/>
  <line x1="150" y1="78" x2="198" y2="140" stroke="currentColor" stroke-width="2.5" marker-end="url(#ah2)"/>
  <line x1="150" y1="224" x2="198" y2="224" stroke="currentColor" stroke-width="2.5" marker-end="url(#ah2)"/>
  <line x1="150" y1="238" x2="198" y2="300" stroke="currentColor" stroke-width="2.5" marker-end="url(#ah2)"/>
  <line x1="340" y1="64" x2="383" y2="194" stroke="currentColor" stroke-dasharray="5 4" marker-end="url(#ah2)"/>
  <line x1="340" y1="224" x2="383" y2="212" stroke="#d0564a" stroke-width="1.5" marker-end="url(#ah2)"/>
  <line x1="150" y1="354" x2="383" y2="230" stroke="currentColor" stroke-dasharray="5 4" marker-end="url(#ah2)"/>
  <line x1="510" y1="210" x2="533" y2="210" stroke="currentColor" stroke-width="2.5" marker-end="url(#ah2)"/>
  <text x="560" y="275" font-size="12">RGB888 640x480</text>
  <line x1="20" y1="402" x2="60" y2="402" stroke="currentColor" stroke-width="2.5"/>
  <text x="66" y="406" font-size="12">ENABLED,IMMUTABLE</text>
  <line x1="220" y1="402" x2="260" y2="402" stroke="#d0564a" stroke-width="1.5"/>
  <text x="266" y="406" font-size="12">ENABLED (바꿀 수 있음)</text>
  <line x1="440" y1="402" x2="480" y2="402" stroke="currentColor" stroke-dasharray="5 4"/>
  <text x="486" y="406" font-size="12">꺼진 링크 []</text>
  <text x="10" y="22" font-size="13">vimc media graph — 예제 2의 media-ctl -p 출력 그대로</text>
</svg>
```
그림 2 — 예제 2 출력을 그대로 그린 vimc 그래프. Scaler의 sink pad 하나에 세 소스(Debayer A, Debayer B, RGB/YUV Input)가 붙어 있고 지금은 **Debayer B**만 켜져 있다. 렌즈 entity는 pad가 0개라 데이터 링크가 없다(커널 안에서는 센서와 ancillary 링크로 묶이지만 media-ctl의 이 출력에는 안 찍혔다). 주의: Debayer B가 켜진 것은 vimc의 기본값이 아니다 — §3.6에서 범인을 찾는다.

### 2.5 흔한 함정

- entity 이름에 공백·슬래시가 있다(`"RGB/YUV Capture"`). media-ctl 인자는 **작은따옴표로 전체를 감싸고 entity 이름은 큰따옴표**로 쓴다: `'"Scaler":1[fmt:...]'`.
- `media-ctl`은 기본으로 `/dev/media0`을 연다. 보드에 media 장치가 여러 개(ISP, 코덱, USB 카메라)면 반드시 `-d`로 지정한다. 위 랩에서도 vivid가 `/dev/media1`이었다.
- video node(`type Node`)에는 pad 포맷이 찍히지 않는다. 끝단 포맷 불일치는 `media-ctl -p`만 봐서는 안 보이고 `v4l2-ctl --get-fmt-video`를 같이 봐야 한다(§3의 2·3단계가 정확히 그 경우).

---

## 3. 포맷 맞추기와 `-EPIPE` — 고장을 일부러 내고 고치기

### 3.1 규칙 세 줄

1. STREAMON 순간 커널이 video node에서 출발해 **켜진 링크를 따라 그래프를 거꾸로 걸으며** 파이프라인에 들어갈 pad를 모은다.
2. 모은 링크마다 **sink 쪽 entity의 `link_validate`** 를 부른다. subdev ↔ subdev는 기본 구현(`v4l2_subdev_link_validate_default`)이 width·height·mbus code·field를 비교하고, subdev → video node는 드라이버가 직접 비교한다(vimc는 `vimc_vdev_link_validate`).
3. 하나라도 다르면 STREAMON이 `-EPIPE`(errno 32, `Broken pipe`)로 실패한다. 포맷은 **subdev 안에서만**(sink → source) 전파되고 **링크를 건너서는 전파되지 않는다**. 그래서 사람이(또는 libcamera가) 상류부터 하류로 하나씩 맞춰야 한다.

### 3.2 예제 3 — 기본 상태에서 스트리밍 → Broken pipe, 그리고 dyndbg로 원인 찾기

무엇을 확인하는 명령인지: vimc를 새로 올린 기본 상태(Debayer A → Scaler 링크가 켜짐)에서 `/dev/video2`를 스트리밍하면 어디서 막히는지. `v4l2-subdev.c`의 dev_dbg를 dynamic debug(M1)로 켜서 링크 검증 로그를 본다.

```sh
rmmod vimc; modprobe vimc        # 깨끗한 기본 상태
dmesg -C
echo 'file drivers/media/v4l2-core/v4l2-subdev.c +p' > /proc/dynamic_debug/control
v4l2-ctl -d /dev/video2 --stream-mmap --stream-count=3
dmesg | cut -c1-200
```

```text
		VIDIOC_STREAMON returned -1 (Broken pipe)

[ 1141.870562] vimc link validate: Scaler:src:640x480 (0x33424752, 8, 0, 0, 0) RGB/YUV Capture:snk:640x480 (0x33424752, 8, 0, 0, 0)
[ 1141.871166] vimc vimc.0: validating link "Debayer A":1 -> "Scaler":0
[ 1141.874981] vimc vimc.0: validating stream "Debayer A":1:0 -> "Scaler":0:0
[ 1141.878540] vimc vimc.0: validating link "Sensor A":0 -> "Debayer A":0
[ 1141.878580] vimc vimc.0: validating stream "Sensor A":0:0 -> "Debayer A":0:0
[ 1141.878620] vimc vimc.0: v4l2_subdev_link_validate_default: media bus code does not match (source 0x0000100a, sink 0x00003014)
[ 1141.878695] vimc vimc.0: v4l2_subdev_link_validate_default: link was "Sensor A":0 -> "Debayer A":0
```

출력에서 볼 것: 검증은 **하류에서 상류로** 진행됐다(Capture ← Scaler 통과 → Scaler ← Debayer A 통과 → Debayer A ← Sensor A 실패). 실패 이유는 mbus code: 센서가 `0x100a`(`MEDIA_BUS_FMT_RGB888_1X24`)를 내보내는데 Debayer의 sink는 `0x3014`(`MEDIA_BUS_FMT_SRGGB8_1X8`, Bayer)를 기대한다. **실제 센서라면 RGB를 내보낼 리 없지만**, "센서 출력 포맷 ≠ 수신기 입력 포맷"은 실보드에서 제일 흔한 `-EPIPE` 원인이다(예: 센서는 10-bit `SRGGB10_1X10`, CSI-2 수신기는 8-bit로 설정).

`mc-entity.c`의 dev_dbg까지 켜면(`file drivers/media/mc/mc-entity.c +p`) 그래프 걷기 자체가 보인다. 첫 부팅에서 켰을 때의 로그 일부(약 80줄 중 앞뒤만 남기고 `...`로 생략):

```text
[ 1108.038329] vimc vimc.0: media pipeline: added pad 'RGB/YUV Capture':0
[ 1108.038657] vimc vimc.0: media pipeline: pushed entry 0: 'RGB/YUV Capture':0
...
[ 1108.044277] vimc vimc.0: media pipeline: skipping link (disabled)
...
[ 1108.050876] vimc vimc.0: media pipeline populated, found pads:
[ 1108.050969] vimc vimc.0: - 'RGB/YUV Capture':0
[ 1108.051016] vimc vimc.0: - 'Scaler':1
[ 1108.051039] vimc vimc.0: - 'Scaler':0
[ 1108.051062] vimc vimc.0: - 'Debayer B':1
[ 1108.051085] vimc vimc.0: - 'Debayer B':0
[ 1108.051108] vimc vimc.0: - 'Sensor B':0
[ 1108.051138] vimc vimc.0: - 'Raw Capture 1':0
[ 1108.051234] vimc vimc.0: Validating pad 'RGB/YUV Capture':0
[ 1108.060238] vimc link validate: Scaler:src:640x480 (0x33424752, 8, 0, 0, 0) RGB/YUV Capture:snk:160x120 (0x33424752, 8, 0, 0, 0)
[ 1108.064940] vimc vimc.0: Link 'Scaler':1 -> 'RGB/YUV Capture':0 failed validation: -32
```

출력에서 볼 것: "found pads" 목록에 **Raw Capture 1** 이 들어 있다. 우리는 RGB/YUV Capture만 스트리밍하려 했지만 Sensor B와 IMMUTABLE로 붙어 있는 Raw Capture 1도 파이프라인에 들어온다. 그리고 이 로그에서는 캡처 노드 크기가 **160x120** 이었다 — vimc 기본값(640x480, `vimc-capture.c`의 `fmt_default`)이 아니다. 누가 바꿨는지는 §3.6.

### 3.3 예제 4 — 센서를 Bayer로 맞추면 또 Broken pipe

무엇을 확인하는 명령인지: 센서 source pad를 `SRGGB8_1X8`로 바꾸고 다시 스트리밍. 이번엔 다른 링크가 걸린다.

```sh
M='media-ctl -d /dev/media0'
$M -V '"Sensor A":0[fmt:SRGGB8_1X8/640x480]'
$M --get-v4l2 '"Sensor A":0'
v4l2-ctl -d /dev/video2 --stream-mmap --stream-count=3
dmesg | grep -E 'does not match|failed validation|link was|vimc link validate' | cut -c1-200
```

```text
		[stream:0 fmt:SRGGB8_1X8/640x480 field:none]
		VIDIOC_STREAMON returned -1 (Broken pipe)

[ 1193.717847] vimc link validate: Scaler:src:640x480 (0x33424752, 8, 0, 0, 0) RGB/YUV Capture:snk:640x480 (0x33424752, 8, 0, 0, 0)
[ 1193.726941] vimc link validate: Sensor A:src:640x480 (0x42474752, 0, 0, 0, 0) Raw Capture 0:snk:640x480 (0x42474752, 8, 0, 0, 0)
```

출력에서 볼 것: 두 번째 줄이 범인이다. 센서 쪽 fourcc가 `0x42474752`(= 'RGGB')인데 Raw Capture 0 쪽은 `0x33424752`(= 'RGB3'). Raw Capture 0은 쓰지도 않는데, Sensor A에 IMMUTABLE 링크로 붙어 있어서 파이프라인에 포함되고 검증된다(예제 3에서 본 "found pads"). 실제 SoC에서도 센서 하나에 "raw 덤프 노드"와 "ISP 출력 노드"가 같이 달린 구조가 흔하고, 이 함정에 똑같이 빠진다. 또 하나: `-V`에 colorspace를 안 줬더니 센서의 colorspace가 0(DEFAULT)이 되어 `colorspace:srgb`가 사라졌다 — vimc 검증은 한쪽이 DEFAULT면 colorimetry 비교를 건너뛰어서 문제가 안 되지만, 엄격한 드라이버라면 이것도 실패 원인이 된다.

센서 하나 바꿨더니 이번에 Debayer 쪽 code 불일치가 안 나온 이유: Debayer A의 sink 기본값이 원래 `SRGGB8_1X8`이었기 때문이다(예제 2).

### 3.4 예제 5 — raw 노드도 맞추면 성공, 프레임 바이트 확인

```sh
v4l2-ctl -d /dev/video0 --set-fmt-video=width=640,height=480,pixelformat=RGGB
v4l2-ctl -d /dev/video0 --get-fmt-video | sed -n 2,6p
v4l2-ctl -d /dev/video2 --stream-mmap=4 --stream-count=20 --stream-to=/root/a.rgb
ls -l /root/a.rgb; od -An -tx1 -N12 /root/a.rgb
```

```text
	Width/Height      : 640/480
	Pixel Format      : 'RGGB' (8-bit Bayer RGRG/GBGB)
	Field             : None
	Bytes per Line    : 640
	Size Image        : 307200
<<<<< 2.32 fps
< 2.04 fps
< 1.99 fps
<< 1.95 fps
<<< 2.13 fps
<< 2.00 fps
< 1.98 fps
<< 1.99 fps
<<< 2.06 fps


-rw-r--r-- 1 root root 18432000 Oct 18 16:39 /root/a.rgb
 bf bf bf bf bf bf bf bf bf bf bf bf
```

출력에서 볼 것: 파일 크기 18432000 = 20 프레임 × 640 × 480 × 3바이트(RGB24). 첫 픽셀 `bf bf bf` = 191 = 75% 흰색, vimc 센서 기본 패턴인 75% 컬러바의 첫 막대다. `<` 하나가 DQBUF 한 번, 줄 끝 숫자가 v4l2-ctl이 계산한 fps다. 2 fps는 vimc가 debayer·스케일을 **CPU로** 계산하는 데다 TCG + 붐비는 호스트라서다(뒤의 §5에서 VM이 한가할 때 다시 재면 4.5 fps). 실제 SoC는 ISP HW가 하므로 이 숫자는 의미가 없다.

### 3.5 예제 6 — 스케일러: source만 키우면 실패, 캡처 노드까지 맞추면 성공

무엇을 확인하는 명령인지: 스케일러는 sink와 source 크기가 달라도 되는(그게 배율이다) 유일한 블록이다. source를 1280x720으로 올리면 하류인 캡처 노드도 따라 바꿔야 한다.

```sh
$M -V '"Scaler":1[fmt:RGB888_1X24/1280x720]'
$M --get-v4l2 '"Scaler":0'; $M --get-v4l2 '"Scaler":1'
v4l2-ctl -d /dev/video2 --stream-mmap --stream-count=2
dmesg | grep 'link validate' | grep Scaler | cut -c1-200
v4l2-ctl -d /dev/video2 --set-fmt-video=width=1280,height=720,pixelformat=RGB3
v4l2-ctl -d /dev/video2 --stream-mmap=4 --stream-count=5 --stream-to=/root/b.rgb
ls -l /root/b.rgb
```

```text
		[stream:0 fmt:RGB888_1X24/640x480 field:none colorspace:srgb
		 crop.bounds:(0,0)/640x480
		 crop:(0,0)/640x480]
		[stream:0 fmt:RGB888_1X24/1280x720 field:none colorspace:srgb]
		VIDIOC_STREAMON returned -1 (Broken pipe)

[ 1225.274594] vimc link validate: Scaler:src:1280x720 (0x33424752, 8, 0, 0, 0) RGB/YUV Capture:snk:640x480 (0x33424752, 8, 0, 0, 0)
<<<<< 1.53 fps


-rw-r--r-- 1 root root 13824000 Oct 18 16:40 /root/b.rgb
```

출력에서 볼 것: sink pad는 640x480 그대로, source만 1280x720 — 스케일러 안에서는 크기가 달라도 된다. 하지만 Scaler:1 → Capture 링크는 양끝이 1280x720 vs 640x480이라 실패. 캡처 노드를 맞추자 13824000 = 5 × 1280 × 720 × 3. **포맷 설정 순서는 항상 상류 → 하류**: 센서 → (수신기) → ISP sink → ISP source → video node.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="30" width="120" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="70" y="52" text-anchor="middle" font-size="13">Sensor A:0</text>
  <text x="70" y="70" text-anchor="middle" font-size="12">source</text>
  <rect x="180" y="30" width="130" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="245" y="52" text-anchor="middle" font-size="13">Debayer A 0→1</text>
  <text x="245" y="70" text-anchor="middle" font-size="12">Bayer → RGB</text>
  <rect x="360" y="30" width="130" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="425" y="52" text-anchor="middle" font-size="13">Scaler 0→1</text>
  <text x="425" y="70" text-anchor="middle" font-size="12">크기 변경 가능</text>
  <rect x="540" y="30" width="130" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="605" y="52" text-anchor="middle" font-size="13">video2</text>
  <text x="605" y="70" text-anchor="middle" font-size="12">RGB/YUV Capture</text>
  <rect x="180" y="110" width="130" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="245" y="135" text-anchor="middle" font-size="13">video0 (Raw 0)</text>
  <line x1="130" y1="55" x2="178" y2="55" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="130" y1="72" x2="178" y2="128" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="310" y1="55" x2="358" y2="55" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="490" y1="55" x2="538" y2="55" stroke="currentColor" marker-end="url(#ah3)"/>
  <circle cx="154" cy="55" r="11" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="154" y="60" text-anchor="middle" font-size="13">1</text>
  <circle cx="160" cy="112" r="11" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="160" y="117" text-anchor="middle" font-size="13">2</text>
  <circle cx="514" cy="55" r="11" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="514" y="60" text-anchor="middle" font-size="13">3</text>
  <text x="10" y="188" font-size="13">1 (예제 3) mbus code: RGB888_1X24 0x100a ≠ SRGGB8_1X8 0x3014</text>
  <text x="10" y="210" font-size="12">고침: media-ctl -V '"Sensor A":0[fmt:SRGGB8_1X8/640x480]'</text>
  <text x="10" y="236" font-size="13">2 (예제 4) fourcc: 센서 RGGB ≠ Raw Capture 0 의 RGB3</text>
  <text x="10" y="258" font-size="12">고침: v4l2-ctl -d /dev/video0 --set-fmt-video=...,pixelformat=RGGB</text>
  <text x="10" y="284" font-size="13">3 (예제 6) 크기: Scaler:1 1280x720 ≠ video2 640x480 → video2도 1280x720</text>
</svg>
```
그림 3 — 예제 3~6에서 실제로 실패한 세 지점. 1은 subdev ↔ subdev(커널 기본 검증), 2·3은 subdev → video node(드라이버 검증). 실보드에서도 `-EPIPE`의 원인은 거의 항상 이 셋 중 하나다: mbus code, 끝단 pixelformat, 크기.

### 3.6 예제 7 — 범인은 libcamera: 그래프 상태는 전역이다

예제 2의 그래프는 Debayer B가 켜져 있었고, 예제 3의 첫 로그에서는 캡처 노드가 160x120이었다. vimc 기본값은 Debayer A + 640x480이다. 그 사이에 한 일은 `cam -l`(libcamera로 카메라 목록 보기)뿐이었다. 확인:

```sh
rmmod vimc; modprobe vimc; sleep 3
media-ctl -d /dev/media0 -p | sed -n '/entity 21/,/pad1/p' | grep '<-'
v4l2-ctl -d /dev/video2 --get-fmt-video | sed -n 2p
cam -l 2>&1 | tail -2
media-ctl -d /dev/media0 -p | sed -n '/entity 21/,/pad1/p' | grep '<-'
v4l2-ctl -d /dev/video2 --get-fmt-video | sed -n 2p
```

```text
		<- "Debayer A":1 [ENABLED]
		<- "Debayer B":1 []
		<- "RGB/YUV Input":0 []
	Width/Height      : 640/480
[0:19:30.989200560] [589] ERROR VIMC vimc.cpp:469 no matching IPA found
Available cameras:
		<- "Debayer A":1 []
		<- "Debayer B":1 [ENABLED]
		<- "RGB/YUV Input":0 []
	Width/Height      : 160/120
```

출력에서 볼 것: `cam -l`은 카메라를 하나도 못 만들었는데도(`Available cameras:` 뒤가 비었다 — 이유는 §8) 그 과정에서 libcamera의 vimc pipeline handler가 **링크를 Debayer B로 바꾸고 캡처 노드 포맷을 160x120으로 바꿔 놓고** 나갔다. media 그래프와 subdev active 포맷은 **장치 전역 상태**다. 파일을 닫아도 원래대로 돌아가지 않는다.

실무 교훈: "어제 media-ctl 스크립트로는 됐는데 오늘 `-EPIPE`" — 그 사이에 libcamera 앱, 다른 서비스, 다른 엔지니어의 테스트 스크립트가 그래프를 만졌을 수 있다. 그래서 캡처 스크립트는 **매번 `media-ctl -r`(리셋)부터 전체 링크·포맷을 다시 설정**하는 것이 정석이다. 실제로 이 노트의 마지막 측정(§5) 직전에도 `/dev/video2`가 또 `STREAMON: Broken pipe`로 실패했고, 원인은 그 사이에 돌린 `cam -l`이었다.

### 3.7 예제 8 — 링크 바꾸기: `-l`, `-r`, IMMUTABLE

```sh
M='media-ctl -d /dev/media0'
$M -l '"Debayer A":1->"Scaler":0[0], "Debayer B":1->"Scaler":0[1]'; echo rc=$?
$M -p | sed -n '/entity 21/,/pad1/p' | grep '<-'
$M -l '"Debayer A":1->"Scaler":0[1]'; echo rc=$?
$M -p | sed -n '/entity 21/,/pad1/p' | grep '<-'
$M -l '"Sensor A":0 -> "Raw Capture 0":0 [0]'; echo rc=$?
$M -r; echo reset rc=$?; $M -p | grep -c ENABLED
```

```text
rc=0
		<- "Debayer A":1 []
		<- "Debayer B":1 [ENABLED]
		<- "RGB/YUV Input":0 []
rc=0
		<- "Debayer A":1 [ENABLED]
		<- "Debayer B":1 [ENABLED]
		<- "RGB/YUV Input":0 []
Unable to parse link: Invalid argument (22)
rc=1
reset rc=0
10
```

(두 번에 나눠 실행한 출력을 이어 붙였다. 사이에 실행한 `dmesg | tail -2`는 뺐다.)

출력에서 볼 것:

- `-l`은 쉼표로 여러 링크를 한 번에 바꾼다. `[1]` 켜기, `[0]` 끄기.
- vimc는 **한 sink pad에 두 소스 링크가 동시에 켜지는 것을 막지 않았다**. 실제 SoC 드라이버는 보통 `link_setup` 콜백에서 이런 조합을 `-EBUSY`로 거절한다. 도구가 받아 줬다고 HW가 그 구성을 지원한다는 뜻은 아니다.
- IMMUTABLE 링크를 끄려 하자 `Invalid argument`(메시지는 "Unable to parse link"로 헷갈리게 나온다).
- `-r`은 IMMUTABLE이 아닌 모든 링크를 끈다. 남은 ENABLED 10개 = IMMUTABLE 링크 5개가 양쪽 entity에서 한 번씩 찍힌 것. 리셋 뒤에는 Scaler로 가는 링크가 하나도 없으니 원하는 경로를 다시 켜야 한다.

`media-ctl --print-dot`은 같은 그래프를 Graphviz dot으로 내보낸다(이 노트에서는 실행 안 함). 보드 문서에 그래프 그림을 넣을 때 쓴다.

---

## 4. vivid로 보는 video node 기본기 — 포맷 열거, 컨트롤, 테스트 패턴

### 4.1 예제 9 — 장치 정보와 포맷·해상도·프레임 간격 열거

무엇을 확인하는 명령인지: 앱(GStreamer, OpenCV, ROS 2 `v4l2_camera`)이 시작할 때 묻는 것과 같은 질문을 v4l2-ctl로 한다.

```sh
v4l2-ctl -d /dev/video3 --info | head -12
v4l2-ctl -d /dev/video3 --list-formats-ext | head -30
```

```text
Driver Info:
	Driver name      : vivid
	Card type        : vivid
	Bus info         : platform:vivid-000
	Driver version   : 6.18.55
	Capabilities     : 0x9dbf0df3
		Video Capture
		Video Output
		VBI Capture
		VBI Output
		Sliced VBI Capture
		Sliced VBI Output
ioctl: VIDIOC_ENUM_FMT
	Type: Video Capture

	[0]: 'YUYV' (YUYV 4:2:2, csc-colorspace, csc-ycbcr, csc-quantization, csc-xfer-func)
		Size: Discrete 320x180
			Interval: Discrete 1.000s (1.000 fps)
			Interval: Discrete 0.500s (2.000 fps)
			Interval: Discrete 0.250s (4.000 fps)
			Interval: Discrete 0.200s (5.000 fps)
			Interval: Discrete 0.100s (10.000 fps)
			Interval: Discrete 0.080s (12.500 fps)
			Interval: Discrete 0.067s (15.000 fps)
			Interval: Discrete 0.040s (25.000 fps)
			Interval: Discrete 0.033s (30.000 fps)
			Interval: Discrete 0.025s (40.000 fps)
			Interval: Discrete 0.020s (50.000 fps)
			Interval: Discrete 0.017s (60.000 fps)
			Interval: Discrete 0.008s (120.000 fps)
		Size: Discrete 320x240
			Interval: Discrete 1.000s (1.000 fps)
...
```

(`head -30`의 나머지 줄은 같은 간격 목록의 반복이라 `...`로 생략했다.)

출력에서 볼 것: `--list-formats-ext`는 `VIDIOC_ENUM_FMT` → `VIDIOC_ENUM_FRAMESIZES` → `VIDIOC_ENUM_FRAMEINTERVALS` 3단 열거다. **여기 없는 조합을 앱이 요구하면** 드라이버는 가까운 값으로 고치거나(S_FMT는 실패하지 않고 값을 바꿔서 돌려준다) GStreamer는 `not-negotiated`로 멈춘다(§9.2). MC 센서 subdev는 이 열거를 video node가 아니라 `v4l2-ctl -d /dev/v4l-subdevN --list-subdev-mbus-codes`, `--list-subdev-framesizes`로 한다.

### 4.2 예제 10 — 컨트롤 목록과 테스트 패턴

```sh
v4l2-ctl -d /dev/video3 --list-ctrls | wc -l
v4l2-ctl -d /dev/video3 --list-ctrls-menus | sed -n '/test_pattern/,/^[[:space:]]*[a-z_]* 0x/p' | head -26
v4l2-ctl -d /dev/video3 --list-ctrls | grep -E 'brightness|percentage|timestamp_source|wrap_sequence|inject_v4l2'
```

```text
83
                   test_pattern 0x00f0f000 (menu)   : min=0 max=21 default=0 value=0 (75% Colorbar) flags=0x00001000
				0: 75% Colorbar
				1: 100% Colorbar
				2: CSC Colorbar
				3: Horizontal 100% Colorbar
				4: 100% Color Squares
				5: 100% Black
				6: 100% White
				7: 100% Red
				8: 100% Green
				9: 100% Blue
				10: 16x16 Checkers
				11: 2x2 Checkers
				12: 1x1 Checkers
				13: 2x2 Red/Green Checkers
				14: 1x1 Red/Green Checkers
				15: Alternating Hor Lines
				16: Alternating Vert Lines
				17: One Pixel Wide Cross
				18: Two Pixels Wide Cross
				19: Ten Pixels Wide Cross
				20: Gray Ramp
				21: Noise
                  osd_text_mode 0x00f0f001 (menu)   : min=0 max=2 default=0 value=0 (All) flags=0x00001000
                     brightness 0x00980900 (int)    : min=0 max=255 step=1 default=128 value=128 flags=slider, 0x00001000
               timestamp_source 0x00f0f018 (menu)   : min=0 max=1 default=0 value=0 (End of Frame) flags=0x00001000
           wrap_sequence_number 0x00f0f026 (bool)   : default=0 value=0 flags=0x00001000
       fill_percentage_of_frame 0x00f0f029 (int)    : min=0 max=100 step=1 default=100 value=100 flags=0x00001000
  percentage_of_dropped_buffers 0x00f0f040 (int)    : min=0 max=100 step=1 default=0 value=0 flags=0x00001000
     inject_v4l2_buf_flag_error 0x00f0f042 (button) : value=0 flags=write-only, execute-on-write
```

출력에서 볼 것: 컨트롤은 이름(`test_pattern`), ID(`0x00f0f000`), 타입, 범위, 현재값이 한 줄이다. `0x0098xxxx`는 표준 User 클래스(brightness 등), `0x00f0xxxx`는 vivid 전용. 실제 센서에서 같은 자리에 오는 것이 `exposure`, `analogue_gain`, `vertical_blanking`, `test_pattern`(센서 내장 컬러바) — 센서 bring-up 첫날 "I2C는 되는데 이미지가 이상하다"면 **센서 test pattern을 켜서 CSI-2 경로와 센서 광학을 분리**한다. MC 파이프라인에서는 이 컨트롤이 센서 subdev 노드(`/dev/v4l-subdevN`)에 있다(I5 §4.3).

vivid의 `percentage_of_dropped_buffers`, `inject_v4l2_buf_flag_error`, `timestamp_source`는 **고장 주입용** 컨트롤이다. §5에서 드롭·에러 검출기를 시험하는 데 쓴다.

### 4.3 예제 11 — 100% Red 패턴을 캡처해서 바이트로 검증

무엇을 확인하는 명령인지: 포맷 설정 → 프레임 레이트 → 패턴 → 캡처 → 프레임 중앙 픽셀 값 확인. "파이프라인이 도는지"만이 아니라 "**데이터가 맞는지**"까지 보는 습관.

```sh
D='-d /dev/video3'
v4l2-ctl $D --set-fmt-video=width=640,height=480,pixelformat=YUYV
v4l2-ctl $D --set-parm=30
v4l2-ctl $D --set-ctrl=test_pattern=7
v4l2-ctl $D --stream-mmap=4 --stream-count=3 --stream-to=/root/r.yuv
ls -l /root/r.yuv
od -An -tu1 -j $((614400+240*1280+640)) -N8 /root/r.yuv   # 2번째 프레임, 240번째 줄, x=320
```

```text
Frame rate set to 30.000 fps
<<<

-rw-r--r-- 1 root root 1843200 Oct 18 16:41 /root/r.yuv
  81  90  81 239  81  90  81 239
```

출력에서 볼 것: YUYV는 2픽셀당 4바이트(Y0 U Y1 V). 빨강의 BT.601 limited range 값은 Y = 16 + 219 × 0.299 ≈ 81, Cb ≈ 90, Cr ≈ 240 — 그대로 나왔다(239는 반올림 차이). 크기 1843200 = 3 × 614400 = 3 × 640 × 480 × 2. 실보드에서 "색이 이상하다"(초록·보라 화면)는 대부분 **Bayer 순서(RGGB vs BGGR) 착오** 아니면 **YUYV vs UYVY 바이트 순서 착오**이고, 이렇게 패턴 하나 캡처해서 바이트를 보면 1분 안에 갈린다.

---

## 5. 버퍼 큐 — 버퍼 수, 드롭, sequence, timestamp

### 5.1 직관 — SSD의 completion queue와 같다

videobuf2(vb2)의 버퍼 하나는 늘 셋 중 한 곳에 있다: **유저 손**(DQBUF로 받아서 처리 중), **queued**(QBUF로 넘겼고 드라이버가 DMA를 걸 차례), **done**(DMA 완료, DQBUF를 기다림). 드라이버가 새 프레임을 쓸 빈 버퍼가 queued 쪽에 하나도 없으면, HW는 그 프레임을 버린다(또는 마지막 버퍼에 덮어쓴다 — 드라이버마다 다름). 그러면 다음 버퍼의 **sequence 번호가 건너뛴다**. SSD 펌웨어에서 host가 CQ를 늦게 비우면 SQ에 새 명령을 못 받는 것과 같은 back-pressure다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="30" width="200" height="64" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="56" text-anchor="middle" font-size="13">유저 소유 (dequeued)</text>
  <text x="120" y="78" text-anchor="middle" font-size="12">앱이 픽셀 처리 중</text>
  <rect x="440" y="30" width="220" height="64" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="550" y="56" text-anchor="middle" font-size="13">queued (빈 버퍼 대기열)</text>
  <text x="550" y="78" text-anchor="middle" font-size="12">드라이버의 다음 DMA 대상</text>
  <rect x="440" y="200" width="220" height="64" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="550" y="226" text-anchor="middle" font-size="13">active (HW가 쓰는 중)</text>
  <text x="550" y="248" text-anchor="middle" font-size="12">CSI/ISP DMA 진행</text>
  <rect x="20" y="200" width="200" height="64" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="120" y="226" text-anchor="middle" font-size="13">done 리스트</text>
  <text x="120" y="248" text-anchor="middle" font-size="12">seq, ts, flags 채워짐</text>
  <line x1="222" y1="55" x2="438" y2="55" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="330" y="47" text-anchor="middle" font-size="12">VIDIOC_QBUF</text>
  <line x1="550" y1="96" x2="550" y2="198" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="542" y="152" text-anchor="end" font-size="12">buf_queue → 프레임 시작</text>
  <line x1="438" y1="232" x2="222" y2="232" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="330" y="224" text-anchor="middle" font-size="12">vb2_buffer_done (IRQ)</text>
  <line x1="120" y1="198" x2="120" y2="96" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="128" y="152" font-size="12">VIDIOC_DQBUF (poll로 깨어남)</text>
  <rect x="250" y="280" width="410" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="455" y="305" text-anchor="middle" font-size="12">queued가 비면 그 프레임은 버려짐 → 다음 seq가 건너뜀</text>
</svg>
```
그림 4 — vb2 버퍼 한 개의 생애. 앱이 처리(왼쪽 위)에 오래 붙잡고 있거나 버퍼 수가 적으면 queued가 비고, 드라이버는 그 프레임을 버린다. 드롭은 에러 코드로 오지 않고 **sequence 번호의 틈**으로만 보인다.

### 5.2 예제 12 — `--verbose`로 sequence, timestamp, flags 보기

```sh
v4l2-ctl -d /dev/video3 --stream-mmap=4 --stream-count=8 --verbose 2>&1 | grep -v '^\s*$' | head -30
```

```text
		VIDIOC_REQBUFS returned 0 (Success)
		VIDIOC_QUERYBUF returned 0 (Success)
		VIDIOC_QUERYBUF returned 0 (Success)
		VIDIOC_QUERYBUF returned 0 (Success)
		VIDIOC_QUERYBUF returned 0 (Success)
		VIDIOC_G_FMT returned 0 (Success)
		VIDIOC_QBUF returned 0 (Success)
		VIDIOC_QBUF returned 0 (Success)
		VIDIOC_QBUF returned 0 (Success)
		VIDIOC_QBUF returned 0 (Success)
		VIDIOC_STREAMON returned 0 (Success)
cap dqbuf: 0 seq:      0 bytesused: 614400 ts: 1306.185127 field: None (ts-monotonic, ts-src-eof)
cap dqbuf: 1 seq:      2 bytesused: 614400 ts: 1306.250632 delta: 65.505 ms dropped: 1 field: None (ts-monotonic, ts-src-eof)
cap dqbuf: 2 seq:      3 bytesused: 614400 ts: 1306.280836 delta: 30.204 ms field: None (ts-monotonic, ts-src-eof)
cap dqbuf: 3 seq:      4 bytesused: 614400 ts: 1306.327612 delta: 46.776 ms field: None (ts-monotonic, ts-src-eof)
cap dqbuf: 0 seq:      6 bytesused: 614400 ts: 1306.374670 delta: 47.058 ms fps: 31.66 dropped: 1 field: None (ts-monotonic, ts-src-eof)
cap dqbuf: 1 seq:      7 bytesused: 614400 ts: 1306.416702 delta: 42.032 ms fps: 30.23 field: None (ts-monotonic, ts-src-eof)
cap dqbuf: 2 seq:      8 bytesused: 614400 ts: 1306.457052 delta: 40.350 ms fps: 29.42 field: None (ts-monotonic, ts-src-eof)
cap dqbuf: 3 seq:      9 bytesused: 614400 ts: 1306.491701 delta: 34.649 ms fps: 29.36 field: None (ts-monotonic, ts-src-eof)
VIDIOC_QUERYCAP: ok
```

출력에서 볼 것 (값은 실행마다 다르다 — 이 실행은 OOM 전 붐비는 첫 부팅):

- `cap dqbuf: 1` — 버퍼 **인덱스**(0~3을 돌아가며 쓴다). `seq:` — 드라이버가 매기는 **프레임 번호**. 둘은 다른 것이다. 인덱스는 0,1,2,3,0…으로 정상인데 seq는 0 → 2, 4 → 6으로 건너뛰었다. v4l2-ctl이 `dropped: 1`로 알려 준다.
- `ts:` — 버퍼 timestamp(초). `(ts-monotonic, ts-src-eof)` = flags의 두 필드: 시계는 `CLOCK_MONOTONIC`, 찍은 시점은 **프레임 끝(End of Frame)**. 센서 퓨전에서 이 차이(SoE vs EoF)는 노출 시간 + readout 시간만큼의 오프셋이다(Q3).
- `delta:` — 이전 버퍼와의 timestamp 차이. 30 fps면 33.3 ms여야 하는데 30~65 ms로 흔들린다 — TCG + 붐비는 호스트에서 vivid의 kthread가 제때 못 돈 결과다.
- 시작 전 `REQBUFS` → `QUERYBUF` × 4 → `QBUF` × 4 → `STREAMON` 순서가 모든 V4L2 캡처 앱의 공통 골격이다(I5 §5.2의 C 코드가 그대로 이것).

### 5.3 예제 13 — 드롭을 숫자로: 버퍼 수, 느린 소비자, 드라이버 드롭

무엇을 확인하는 명령인지: `--verbose`의 seq 열을 awk로 모아 손실률을 계산하고, 세 원인을 일부러 만들어 비교한다. `--stream-sleep count=30,sleep=300`은 v4l2-ctl이 30버퍼마다 300 ms 잠드는 "느린 소비자" 흉내이고, `percentage_of_dropped_buffers=20`은 드라이버가 20% 확률로 프레임을 버리는 고장 주입이다.

```sh
cat > /tmp/seq.awk <<'EOF'
/^cap dqbuf/{s=$5+0; if(n++ && s!=p+1) {d+=s-p-1; g++}; p=s; if(n==1)f=s}
END{printf "frames=%d seq=%d..%d gaps=%d lost=%d loss=%.1f%%\n", n, f, p, g, d, 100*d/(p-f+1)}
EOF
for b in 2 4 8; do printf 'bufs=%s ' $b
  v4l2-ctl -d /dev/video3 --stream-mmap=$b --stream-count=150 --verbose 2>&1 | awk -f /tmp/seq.awk; done
for b in 2 8; do printf 'slow consumer(sleep 300ms every 30) bufs=%s ' $b
  v4l2-ctl -d /dev/video3 --stream-mmap=$b --stream-count=150 --stream-sleep count=30,sleep=300 \
    --verbose 2>&1 | awk -f /tmp/seq.awk; done
v4l2-ctl -d /dev/video3 -c percentage_of_dropped_buffers=20
printf 'inject 20%% bufs=8 '
v4l2-ctl -d /dev/video3 --stream-mmap=8 --stream-count=150 --verbose 2>&1 | awk -f /tmp/seq.awk
v4l2-ctl -d /dev/video3 -c percentage_of_dropped_buffers=0
```

```text
bufs=2 frames=150 seq=0..149 gaps=0 lost=0 loss=0.0%
bufs=4 frames=150 seq=0..150 gaps=1 lost=1 loss=0.7%
bufs=8 frames=150 seq=0..150 gaps=1 lost=1 loss=0.7%
slow consumer(sleep 300ms every 30) bufs=2 frames=150 seq=0..180 gaps=7 lost=31 loss=17.1%
slow consumer(sleep 300ms every 30) bufs=8 frames=150 seq=0..153 gaps=4 lost=4 loss=2.6%
inject 20% bufs=8 frames=150 seq=0..194 gaps=34 lost=45 loss=23.1%
```

(위 세 줄 기본 측정은 아래 세 줄과 다른 실행이다 — 같은 명령으로 처음 돌렸을 때는 bufs=2/4/8이 1.3%/1.3%/0.0%였다. 게스트가 한가해도 VM 호스트가 붐비면 기본 손실이 실행마다 0~1% 수준으로 흔들린다.)

```svg
<svg viewBox="0 0 640 310" xmlns="http://www.w3.org/2000/svg">
  <line x1="70" y1="240" x2="620" y2="240" stroke="currentColor"/>
  <line x1="70" y1="40" x2="70" y2="240" stroke="currentColor"/>
  <text x="62" y="244" text-anchor="end" font-size="12">0</text>
  <text x="62" y="204" text-anchor="end" font-size="12">5</text>
  <text x="62" y="164" text-anchor="end" font-size="12">10</text>
  <text x="62" y="124" text-anchor="end" font-size="12">15</text>
  <text x="62" y="84" text-anchor="end" font-size="12">20</text>
  <text x="62" y="44" text-anchor="end" font-size="12">25</text>
  <line x1="70" y1="200" x2="620" y2="200" stroke="#888" stroke-opacity="0.4"/>
  <line x1="70" y1="160" x2="620" y2="160" stroke="#888" stroke-opacity="0.4"/>
  <line x1="70" y1="120" x2="620" y2="120" stroke="#888" stroke-opacity="0.4"/>
  <line x1="70" y1="80" x2="620" y2="80" stroke="#888" stroke-opacity="0.4"/>
  <line x1="70" y1="40" x2="620" y2="40" stroke="#888" stroke-opacity="0.4"/>
  <text x="14" y="140" font-size="12" transform="rotate(-90 14 140)" text-anchor="middle">손실률 %</text>
  <rect x="85.8" y="239" width="60" height="1" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <rect x="177.5" y="234.4" width="60" height="5.6" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <rect x="269.2" y="234.4" width="60" height="5.6" fill="#4a7bd0" fill-opacity="0.6" stroke="#4a7bd0"/>
  <rect x="360.8" y="103.2" width="60" height="136.8" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <rect x="452.5" y="219.2" width="60" height="20.8" fill="#e08a3c" fill-opacity="0.6" stroke="#e08a3c"/>
  <rect x="544.2" y="55.2" width="60" height="184.8" fill="#d0564a" fill-opacity="0.6" stroke="#d0564a"/>
  <text x="115.8" y="230" text-anchor="middle" font-size="12">0.0</text>
  <text x="207.5" y="228" text-anchor="middle" font-size="12">0.7</text>
  <text x="299.2" y="228" text-anchor="middle" font-size="12">0.7</text>
  <text x="390.8" y="97" text-anchor="middle" font-size="12">17.1</text>
  <text x="482.5" y="213" text-anchor="middle" font-size="12">2.6</text>
  <text x="574.2" y="49" text-anchor="middle" font-size="12">23.1</text>
  <text x="115.8" y="258" text-anchor="middle" font-size="12">기본 b=2</text>
  <text x="207.5" y="258" text-anchor="middle" font-size="12">기본 b=4</text>
  <text x="299.2" y="258" text-anchor="middle" font-size="12">기본 b=8</text>
  <text x="390.8" y="258" text-anchor="middle" font-size="12">느림 b=2</text>
  <text x="482.5" y="258" text-anchor="middle" font-size="12">느림 b=8</text>
  <text x="574.2" y="258" text-anchor="middle" font-size="12">주입 20%</text>
  <text x="345" y="285" text-anchor="middle" font-size="12">vivid 640x480 YUYV 30 fps, 150 프레임 (TCG — 실HW 수치 아님)</text>
  <text x="345" y="303" text-anchor="middle" font-size="12">b = REQBUFS 버퍼 수, 느림 = 30버퍼마다 300 ms 정지</text>
</svg>
```
그림 5 — 예제 13의 측정값. 느린 소비자의 300 ms 정지는 약 9 프레임에 해당한다. 버퍼가 2개면 그 9 프레임 대부분을 버리지만(17.1%), 8개면 버퍼가 완충해서 대부분 살아남는다(2.6%). 반대로 드라이버가 버리는 프레임(주입 20%)은 버퍼를 늘려도 그대로다.

이 그래프가 드롭 진단의 핵심 논리다:

| 관찰 | 해석 | 대응 |
|---|---|---|
| 버퍼를 늘리면 손실이 줄어든다 | 소비자(앱)가 가끔 늦다 — 버퍼가 완충 | 버퍼 수 증가, 처리 스레드 분리, 우선순위(N 모듈), 복사 제거(DMABUF) |
| 버퍼를 늘려도 그대로 | 생산자(센서·CSI·ISP·드라이버)가 버린다 | CSI-2 에러 카운터, ISP overflow IRQ, 대역폭(DDR·IOMMU), 드라이버 로그 |
| 손실은 0인데 delta가 들쭉날쭉 | 타임스탬프를 찍는 IRQ 지연 또는 SW timestamp | SoE/EoF flags 확인, IRQ 스레드 우선순위, HW timestamp 사용 |
| seq가 0으로 돌아감 | sequence wrap(32-bit) 또는 STREAMOFF/ON 재시작 | 비교는 `(u32)(s - p)`로, 재시작 이벤트 로깅 |

버퍼를 늘리는 것은 공짜가 아니다: 메모리(1080p NV12 한 장 ≈ 3 MB, CMA에서 연속 할당 — F10)와 **지연**(완충된 만큼 오래된 프레임을 처리)을 산다. 로봇 제어 루프처럼 최신 프레임이 중요하면 버퍼를 적게 두고 오래된 프레임은 일부러 버리는 쪽이 낫다.

### 5.4 예제 14 — 직접 쓰는 드롭·지터 모니터 `q2_seqmon`

무엇을 확인하는 코드인지: v4l2-ctl + awk로 하던 것을 50줄 C로. 버퍼 내용은 안 보고(그래서 mmap도 안 한다) sequence 틈, timestamp 간격 최소/최대, timestamp flags, `V4L2_BUF_FLAG_ERROR` 개수만 센다. Q7(센서 데이터 품질 툴)의 카메라 버전 뼈대다. 게스트(Ubuntu 24.04 rootfs-dom) 안의 gcc로 `-Wall -Wextra` 경고 0개 빌드.

```c
/* q2_seqmon.c <dev> <nbufs> <frames> — sequence gap / timestamp jitter monitor */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <linux/videodev2.h>
#define die(m) do { perror(m); exit(1); } while (0)
int main(int argc, char **argv)
{
	if (argc < 4) { fprintf(stderr, "usage: %s dev nbufs frames\n", argv[0]); return 2; }
	int fd = open(argv[1], O_RDWR);
	if (fd < 0) die("open");
	struct v4l2_requestbuffers rb = { .count = atoi(argv[2]),
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE, .memory = V4L2_MEMORY_MMAP };
	if (ioctl(fd, VIDIOC_REQBUFS, &rb)) die("REQBUFS");
	printf("REQBUFS asked %s got %u\n", argv[2], rb.count);
	for (unsigned i = 0; i < rb.count; i++) {
		struct v4l2_buffer b = { .index = i, .type = rb.type, .memory = rb.memory };
		if (ioctl(fd, VIDIOC_QBUF, &b)) die("QBUF");
	}
	int type = rb.type, n = atoi(argv[3]);
	if (ioctl(fd, VIDIOC_STREAMON, &type)) die("STREAMON");
	unsigned first = 0, prev = 0, gaps = 0, lost = 0, errs = 0;
	double t0 = 0, tp = 0, dmin = 1e9, dmax = 0;
	for (int k = 0; k < n; k++) {
		struct v4l2_buffer b = { .type = rb.type, .memory = rb.memory };
		if (ioctl(fd, VIDIOC_DQBUF, &b)) die("DQBUF");
		double t = b.timestamp.tv_sec + b.timestamp.tv_usec / 1e6;
		if (k == 0) {
			first = b.sequence; t0 = t;
			printf("flags 0x%08x: ts-type=%s ts-src=%s\n", b.flags,
			       (b.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) ==
			       V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC ? "monotonic" : "other",
			       (b.flags & V4L2_BUF_FLAG_TSTAMP_SRC_MASK) ==
			       V4L2_BUF_FLAG_TSTAMP_SRC_SOE ? "SOE" : "EOF");
		} else {
			double d = (t - tp) * 1e3;
			if (d < dmin) dmin = d;
			if (d > dmax) dmax = d;
			if (b.sequence != prev + 1) { gaps++; lost += b.sequence - prev - 1; }
		}
		if (b.flags & V4L2_BUF_FLAG_ERROR) errs++;
		prev = b.sequence; tp = t;
		if (ioctl(fd, VIDIOC_QBUF, &b)) die("QBUF2");
	}
	ioctl(fd, VIDIOC_STREAMOFF, &type);
	printf("frames=%d seq %u..%u gaps=%u lost=%u err=%u\n", n, first, prev, gaps, lost, errs);
	printf("fps(ts)=%.2f delta min/max = %.1f/%.1f ms\n", (n - 1) / (tp - t0), dmin, dmax);
	return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 /root/q2_seqmon.c -o /root/q2_seqmon
D=/dev/video3
v4l2-ctl -d $D -v width=640,height=480,pixelformat=YUYV; v4l2-ctl -d $D -p 30 >/dev/null
echo '# baseline'; /root/q2_seqmon $D 4 150
echo '# 20% injected drop'; v4l2-ctl -d $D -c percentage_of_dropped_buffers=20
/root/q2_seqmon $D 4 150; v4l2-ctl -d $D -c percentage_of_dropped_buffers=0
echo '# timestamp source = SoE'; v4l2-ctl -d $D -c timestamp_source=1
/root/q2_seqmon $D 4 30; v4l2-ctl -d $D -c timestamp_source=0
echo '# asking 64 buffers'; /root/q2_seqmon $D 64 5
```

```text
# baseline
REQBUFS asked 4 got 4
flags 0x00002000: ts-type=monotonic ts-src=EOF
frames=150 seq 0..149 gaps=0 lost=0 err=0
fps(ts)=29.97 delta min/max = 23.4/48.0 ms
# 20% injected drop
REQBUFS asked 4 got 4
flags 0x00002000: ts-type=monotonic ts-src=EOF
frames=150 seq 0..191 gaps=35 lost=42 err=0
fps(ts)=23.38 delta min/max = 24.7/135.9 ms
# timestamp source = SoE
REQBUFS asked 4 got 4
flags 0x00012000: ts-type=monotonic ts-src=SOE
frames=30 seq 0..29 gaps=0 lost=0 err=0
fps(ts)=29.98 delta min/max = 23.7/44.3 ms
# asking 64 buffers
REQBUFS asked 64 got 64
flags 0x00002000: ts-type=monotonic ts-src=EOF
frames=5 seq 0..4 gaps=0 lost=0 err=0
fps(ts)=30.27 delta min/max = 26.8/45.8 ms
```

출력에서 볼 것:

- flags `0x00002000` = `V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC`, `0x00012000` = 거기에 `V4L2_BUF_FLAG_TSTAMP_SRC_SOE`(0x00010000)가 더해진 것. 같은 드라이버도 설정에 따라 SoE/EoF가 바뀐다. 퓨전 코드는 **매 버퍼의 flags를 확인**해야지 "우리 카메라는 EoF"라고 하드코딩하면 안 된다.
- 드롭 주입 시 fps(ts)가 23.38 — "fps"를 DQBUF 개수로 재면 드롭이 fps 저하로 보이고, seq로 재면 원인(드롭 42개)이 보인다. 최대 delta 135.9 ms ≈ 4 프레임 간격 = 연속 3개 드롭.
- `REQBUFS`는 요청한 수를 **드라이버가 고쳐서** 돌려준다(`rb.count`). 여기서는 64를 그대로 줬지만, 실제 SoC 드라이버는 최소 개수(`min_queued_buffers`)를 올리거나 상한으로 깎는다. 받은 값을 반드시 다시 읽는다.

`V4L2_BUF_FLAG_ERROR` 검출도 시험했다. 스트리밍 중에 다른 셸에서 에러 주입 버튼을 누른다:

```sh
(sleep 1; v4l2-ctl -d /dev/video3 -c inject_v4l2_buf_flag_error=1) & /root/q2_seqmon /dev/video3 4 60; wait
```

```text
REQBUFS asked 4 got 4
flags 0x00002000: ts-type=monotonic ts-src=EOF
frames=60 seq 0..59 gaps=0 lost=0 err=1
fps(ts)=30.03 delta min/max = 25.8/40.1 ms
```

출력에서 볼 것: 드롭은 0인데 `err=1`. ERROR 플래그 버퍼는 **도착은 했지만 내용이 깨졌을 수 있는** 프레임이다(실HW: CSI-2 CRC/ECC 에러, FIFO overflow). seq만 보는 모니터는 이걸 놓친다. 앱은 이 버퍼를 버리고 다시 QBUF해야 한다.

### 5.5 메모리 타입 — MMAP, USERPTR, DMABUF

| 타입 | 버퍼를 누가 할당하나 | 언제 쓰나 | v4l2-ctl / GStreamer |
|---|---|---|---|
| MMAP | 드라이버(vb2 allocator: dma-contig/CMA, sg, vmalloc) | 기본. 앱이 mmap해서 읽음 | `--stream-mmap=N` / `io-mode=mmap` |
| USERPTR | 앱(malloc) | 레거시. 장치가 sg DMA를 못 하면 사실상 불가 | `--stream-user=N` / `io-mode=userptr` |
| DMABUF export | 이 장치가 할당하고 fd로 내보냄 | 다른 장치(인코더·GPU·NPU)로 zero-copy | `VIDIOC_EXPBUF` / `io-mode=dmabuf` |
| DMABUF import | 다른 장치·DMA heap이 할당한 fd를 받음 | 디스플레이·NPU 버퍼에 바로 캡처 | `--stream-dmabuf` / `io-mode=dmabuf-import` |

### 5.6 예제 15 — USERPTR, 그리고 DMABUF로 캡처 → 출력 장치 zero-copy

무엇을 확인하는 명령인지: vivid 캡처 버퍼를 EXPBUF로 dma-buf fd로 내보내고 vivid 출력 장치가 그 fd를 import해서 쓴다. 스트리밍 중에 debugfs의 dma-buf 목록과 프로세스의 fd를 본다. O4(가속기로 dma-buf 넘기기)의 가장 작은 버전이다.

```sh
v4l2-ctl -d /dev/video3 --stream-user=4 --stream-count=60 2>&1 | tail -2
v4l2-ctl -d /dev/video4 --set-fmt-video-out=width=640,height=480,pixelformat=YUYV
(v4l2-ctl -d /dev/video3 --stream-mmap=4 --out-device=/dev/video4 --stream-out-dmabuf \
   --stream-count=90 >/tmp/o.txt 2>&1 &)
sleep 1.5; cat /sys/kernel/debug/dma_buf/bufinfo | head -30
P=$(pidof v4l2-ctl); ls -l /proc/$P/fd | grep -i dmabuf | head -4
sleep 3; tail -c 300 /tmp/o.txt
```

```text
<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< 29.91 fps
<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
Dma-buf Objects:
size    	flags   	mode    	count   	exp_name	ino     	name
00614400	00000002	02080007	00000003	videobuf2_vmalloc	00000016	<none>
	Attached Devices:
Total 0 devices attached

00614400	00000002	02080007	00000003	videobuf2_vmalloc	00000015	<none>
	Attached Devices:
Total 0 devices attached

00614400	00000002	02080007	00000003	videobuf2_vmalloc	00000014	<none>
	Attached Devices:
Total 0 devices attached

00614400	00000002	02080007	00000003	videobuf2_vmalloc	00000013	<none>
	Attached Devices:
Total 0 devices attached


Total 4 objects, 2457600 bytes
lrwx------ 1 root root 64 Oct 18 16:46 6 -> /dmabuf:
lrwx------ 1 root root 64 Oct 18 16:46 7 -> /dmabuf:
lrwx------ 1 root root 64 Oct 18 16:46 8 -> /dmabuf:
lrwx------ 1 root root 64 Oct 18 16:46 9 -> /dmabuf:
><><><><><><><><><><><><><><><><><><><><><><><><>< 30.40 fps, dropped buffers: 4
><><><><><><><><><><><><><><><><><><><><><><><>< 30.18 fps, dropped buffers: 6
><><><><><><><><><><><><><><><><><><><><><><><><>< 30.11 fps, dropped buffers: 5
><><><><><><><><><><><><><>handle out -2
handle out2in -2
```

출력에서 볼 것:

- USERPTR도 vivid에서는 그대로 30 fps(vivid는 vmalloc allocator라 아무 유저 메모리나 받는다). 실SoC의 dma-contig 드라이버는 USERPTR을 거의 지원하지 않는다.
- `exp_name videobuf2_vmalloc` — 누가 내보냈는지(exporter). 614400 바이트 버퍼 4개, `count 3`은 참조 수(내보낸 fd, vb2 쪽 참조, 출력 장치 import). 실HW에서 importer가 DMA 장치면 "Attached Devices"에 그 장치 이름이 나온다. vivid 출력은 CPU로 읽는 가상 장치라 0개다.
- 프로세스 fd 6~9가 `/dmabuf:` — dma-buf는 그냥 파일 디스크립터다(A3). 그래서 `SCM_RIGHTS`로 다른 프로세스(예: 인코더 서비스)에 넘길 수 있다.
- `><`가 번갈아 찍힘 = 캡처 DQBUF(`<`) → 같은 버퍼를 출력에 QBUF(`>`). 복사 없이 한 메모리가 두 장치를 오간다. `dropped buffers`는 출력 쪽 기준 수치이고, 마지막의 `handle out -2`는 `--stream-count`로 스트림을 끝낼 때 찍혔다(원인 코드까지는 확인 안 함).

---

## 6. 진단 도구 두 개 더 — `dev_debug`와 v4l2-async debugfs

### 6.1 예제 16 — `dev_debug`: 앱이 어떤 ioctl을 어떤 인자로 불렀나

무엇을 확인하는 명령인지: GStreamer나 ROS 노드처럼 소스를 바로 못 보는 앱이 실제로 무슨 포맷을 요구했는지, V4L2 core가 대신 로그로 찍게 한다. 비트: 0x1 ioctl 이름, 0x2 인자, 0x4 file ops, 0x8 스트리밍 ioctl(QBUF/DQBUF — 매 프레임 찍혀서 시끄럽다), 0x10 poll, 0x20 컨트롤(`include/media/v4l2-ioctl.h`).

```sh
dmesg -C; echo 3 > /sys/class/video4linux/video3/dev_debug
v4l2-ctl -d /dev/video3 --stream-mmap=2 --stream-count=2 >/dev/null 2>&1
echo 0 > /sys/class/video4linux/video3/dev_debug
dmesg | grep -E 'S_FMT|G_FMT|REQBUFS|STREAMO|DQBUF|VIDIOC_QBUF' | cut -c 1-175 | head -12
```

```text
[  381.029894] video3: VIDIOC_QUERY_EXT_CTRL: id=0xf0f043, type=4, name=Inject VIDIOC_REQBUFS Error, min/max=0/0, step=0, default=0, flags=0x00000240, elem_size=4, elems=1, nr
[  381.030031] video3: VIDIOC_QUERY_EXT_CTRL: id=0xf0f044, type=4, name=Inject VIDIOC_QBUF Error, min/max=0/0, step=0, default=0, flags=0x00000240, elem_size=4, elems=1, nr_of
[  381.030174] video3: VIDIOC_QUERY_EXT_CTRL: id=0xf0f045, type=4, name=Inject VIDIOC_STREAMON Error, min/max=0/0, step=0, default=0, flags=0x00000240, elem_size=4, elems=1, n
[  381.035253] video3: VIDIOC_REQBUFS: count=2, type=vid-cap, memory=mmap
[  381.037508] video3: VIDIOC_G_FMT: type=vid-cap, width=640, height=480, pixelformat=NV12 little-endian (0x3231564e), field=none, bytesperline=640, sizeimage=460800, colorspa
[  381.037888] video3: VIDIOC_G_FMT: type=vid-cap, width=640, height=480, pixelformat=NV12 little-endian (0x3231564e), field=none, bytesperline=640, sizeimage=460800, colorspa
[  381.040532] video3: VIDIOC_STREAMON: type=vid-cap
[  381.081927] video3: VIDIOC_STREAMOFF: type=vid-cap
[  381.082470] video3: VIDIOC_STREAMOFF: type=vid-cap
[  381.089221] video3: VIDIOC_REQBUFS: count=0, type=vid-cap, memory=mmap
```

출력에서 볼 것: v4l2-ctl은 포맷을 안 바꿨으니 G_FMT만 했는데, 그 값이 **NV12**다. 직전에 돌린 GStreamer 파이프라인(§9)이 NV12로 S_FMT하고 나갔기 때문이다. video node 포맷도 media 그래프처럼 **마지막으로 설정한 프로세스의 값이 남는다**. "v4l2-ctl로 캡처한 파일이 이상한 크기"의 흔한 원인이다. QBUF/DQBUF는 비트 0x8을 안 켜서 안 찍혔다(첫 세 줄은 grep 패턴의 `REQBUFS`·`QBUF`가 컨트롤 이름에 걸린 것).

### 6.2 예제 17 — v4l2-async: "센서 probe는 됐는데 그래프에 안 나온다"

실보드에서 센서 드라이버와 CSI-2 수신기 드라이버는 따로 probe된다. 수신기는 DT의 `port`/`endpoint`/`remote-endpoint`를 읽고 "이 fwnode에 붙은 subdev를 기다린다"고 **async notifier**에 등록한다. 센서가 `v4l2_async_register_subdev()`로 등록되면 fwnode가 매칭되어 수신기의 `.bound` 콜백이 불리고, 기다리던 것이 다 모이면 `.complete`가 불려서 그때 `/dev/v4l-subdevN` 노드와 링크가 생긴다(코드는 I5 §11.3~§11.4). 하나라도 안 오면 그래프가 **영원히 미완성**이고, 이게 dmesg에 에러 없이 조용히 일어난다. 그때 보는 곳:

```sh
ls /sys/kernel/debug/v4l2-async/ && cat /sys/kernel/debug/v4l2-async/pending_async_subdevices
```

```text
pending_async_subdevices
```

출력에서 볼 것: 랩(QEMU virt)에는 async를 쓰는 카메라가 없어서 목록이 비어 있다 — 이것이 "기다리는 것 없음"의 정상 출력이다. 실보드에서 여기에 수신기 이름과 `fwnode /soc/i2c@.../camera@10` 같은 줄이 남아 있으면, 그 센서가 probe에 실패했거나(I2C NACK, regulator·clock 없음, `-EPROBE_DEFER` 반복) DT의 `remote-endpoint`가 서로를 가리키지 않는 것이다(가이드, 실행 안 함 — 줄 형식은 커널 버전마다 조금 다르다). 함께 보는 것: `/sys/kernel/debug/devices_deferred`(G3), `dmesg | grep -i -E 'imx|csi|probe'`, `i2cdetect`(H5).

---

## 7. vimc를 다시 측정 — 같은 C 모니터로 MC 파이프라인 보기

§3.6에서 말한 대로 `cam -l` 뒤에는 링크가 또 바뀌어 있었다. 실제 실행 순서 그대로 적는다.

```sh
/root/q2_seqmon /dev/video2 4 12
```

```text
REQBUFS asked 4 got 4
STREAMON: Broken pipe
```

```sh
M='media-ctl -d /dev/media0'
$M -l '"Debayer B":1->"Scaler":0[0], "Debayer A":1->"Scaler":0[1]'
v4l2-ctl -d /dev/video2 -v width=640,height=480,pixelformat=RGB3
$M -V '"Scaler":1[fmt:RGB888_1X24/640x480]'
/root/q2_seqmon /dev/video2 4 12; cat /proc/loadavg
```

```text
REQBUFS asked 4 got 4
flags 0x00002000: ts-type=monotonic ts-src=EOF
frames=12 seq 0..11 gaps=0 lost=0 err=0
fps(ts)=4.51 delta min/max = 201.8/260.1 ms
0.27 0.30 0.14 1/66 415
```

출력에서 볼 것: REQBUFS는 성공했는데 STREAMON에서 실패한다 — **링크 검증은 STREAMON 때만** 한다. 링크를 Debayer A로 되돌리고 캡처 노드를 640x480으로 다시 맞추자 동작. vimc는 sensor → debayer → scaler를 kthread에서 CPU로 계산하므로 4.5 fps, 드롭 없음(소비자가 생산자보다 빠르다). 게스트 load average가 0.3인데도 느린 것은 TCG 자체 비용이다.

---

## 8. libcamera — pipeline handler와 IPA, 그리고 고장 두 가지

### 8.1 개념 정리 (I5 §13의 요약 + 실행으로 확인한 것)

libcamera는 "media-ctl + v4l2-ctl 스크립트 + 3A 루프"를 라이브러리로 만든 것이다. 구성:

| 요소 | 하는 일 | 이 랩에서 확인한 것 |
|---|---|---|
| DeviceEnumerator | `/dev/mediaN`을 찾고, 그 entity의 devnode가 다 생길 때까지 기다림 | **udev**로 찾는다(`device_enumerator_udev.cpp`) |
| Pipeline handler | SoC별 C++ 코드. media 그래프를 보고 "내 하드웨어인가"를 match, 링크·포맷 설정 | Vimc, UVC, Simple, Vc4(RPi), RkISP1, ISI(i.MX8) 6개 등록 |
| IPA 모듈 | 3A 알고리즘(AE/AWB/AF), 센서 튜닝. 별도 `.so`, 보통 격리 프로세스에서 실행 | 이 rootfs에는 **없음** (`/usr/lib/aarch64-linux-gnu/libcamera`에 `v4l2-compat.so`만 있음) |
| 도구 | `cam`(CLI), `qcam`, GStreamer `libcamerasrc`, V4L2 호환 레이어 | `cam`, `libcamerasrc` 있음 |

설치된 버전: `libcamera0.2 0.2.0-3fakesync1build6`, `libcamera-tools`, `gstreamer1.0-libcamera`, `libcamera-v4l2`. IPA 패키지(Ubuntu의 `libcamera-ipa`)는 이 rootfs에 들어 있지 않다.

### 8.2 예제 18 — 고장 1: udev가 없으면 카메라가 0개

무엇을 확인하는 명령인지: `init=/bin/bash` 부팅(udev 없음)에서 `cam -l`과 libcamera 디버그 로그.

```sh
cam -l 2>&1 | cut -c1-150
LIBCAMERA_LOG_LEVELS='*:DEBUG' cam -l 2>&1 | grep -v IPA | cut -c30-200 | head -30
```

```text
[0:05:17.259052384] [286] ERROR IPAModule ipa_module.cpp:172 Symbol ipaModuleInfo not found
[0:05:17.262615968] [286] ERROR IPAModule ipa_module.cpp:292 v4l2-compat.so: IPA module has no valid info
[0:05:17.270141328] [286]  WARN IPAManager ipa_manager.cpp:154 No IPA found in '/usr/lib/aarch64-linux-gnu/libcamera'
[0:05:17.271682912] [286]  INFO Camera camera_manager.cpp:284 libcamera v0.2.0
Available cameras:
```

```text
;32m INFO Camera camera_manager.cpp:284 libcamera v0.2.0
;36mDEBUG Camera camera_manager.cpp:69 Starting camera manager
;36mDEBUG Camera camera_manager.cpp:113 Found registered pipeline handler 'PipelineHandlerVimc'
;36mDEBUG Camera camera_manager.cpp:113 Found registered pipeline handler 'PipelineHandlerUVC'
;36mDEBUG Camera camera_manager.cpp:113 Found registered pipeline handler 'SimplePipelineHandler'
;36mDEBUG Camera camera_manager.cpp:113 Found registered pipeline handler 'PipelineHandlerVc4'
;36mDEBUG RPI vc4.cpp:179 Unable to acquire a Unicam instance
;36mDEBUG RPI vc4.cpp:179 Unable to acquire a Unicam instance
;36mDEBUG Camera camera_manager.cpp:113 Found registered pipeline handler 'PipelineHandlerRkISP1'
;36mDEBUG Camera camera_manager.cpp:113 Found registered pipeline handler 'PipelineHandlerISI'
```

(`;32m` 등은 색 이스케이프 코드가 `cut`에 잘려 남은 것이다.)

출력에서 볼 것: pipeline handler 6개가 차례로 "내 장치 있나?"를 묻지만 **DeviceEnumerator 줄이 하나도 없다** — media 장치를 하나도 못 찾았다. `/dev/media0`은 있는데도. libcamera의 udev enumerator는 udev 데이터베이스(`/run/udev/data`)를 보고, udevd가 없으면 장치가 "준비 안 됨"이다. 최소 rootfs(BusyBox, 커스텀 init)에서 libcamera를 쓸 때 실제로 만나는 문제다(L2). udevd를 손으로 띄운다:

```sh
mkdir -p /run/udev; /usr/lib/systemd/systemd-udevd --daemon
udevadm trigger --subsystem-match=media --subsystem-match=video4linux; udevadm settle -t 60
ls /run/udev/data | grep -c c81
LIBCAMERA_LOG_LEVELS='*:DEBUG' cam -l 2>&1 | grep -v IPA \
  | grep -E 'DeviceEnum|VIMC|vimc|Available|Pipeline' | cut -c30-200 | head -20
```

```text
Starting systemd-udevd version 255.4-1ubuntu8.18
21
;36mDEBUG DeviceEnumerator device_enumerator.cpp:230 New media device "vimc" created from /dev/media0
;36mDEBUG DeviceEnumerator device_enumerator_udev.cpp:96 Defer media device /dev/media0 due to 11 missing dependencies
;36mDEBUG DeviceEnumerator device_enumerator_udev.cpp:322 All dependencies for media device /dev/media0 found
;36mDEBUG DeviceEnumerator device_enumerator.cpp:258 Added device /dev/media0: vimc
;36mDEBUG DeviceEnumerator device_enumerator.cpp:230 New media device "vivid" created from /dev/media1
;36mDEBUG DeviceEnumerator device_enumerator_udev.cpp:96 Defer media device /dev/media1 due to 4 missing dependencies
;36mDEBUG DeviceEnumerator device_enumerator_udev.cpp:322 All dependencies for media device /dev/media1 found
;36mDEBUG DeviceEnumerator device_enumerator.cpp:258 Added device /dev/media1: vivid
;36mDEBUG Camera camera_manager.cpp:113 Found registered pipeline handler 'PipelineHandlerVimc'
;36mDEBUG DeviceEnumerator device_enumerator.cpp:318 Successful match for media device "vimc"
;36mDEBUG MediaDevice media_device.cpp:830 /dev/media0[vimc]: Debayer A[1] -> Scaler[0]: 0
;36mDEBUG MediaDevice media_device.cpp:830 /dev/media0[vimc]: Debayer B[1] -> Scaler[0]: 0
;36mDEBUG MediaDevice media_device.cpp:830 /dev/media0[vimc]: RGB/YUV Input[0] -> Scaler[0]: 0
;36mDEBUG MediaDevice media_device.cpp:830 /dev/media0[vimc]: Debayer B[1] -> Scaler[0]: 1
;36mDEBUG V4L2 v4l2_videodevice.cpp:632 /dev/video2[17:cap]: Opened device platform:vimc.0: vimc: vimc
;36mDEBUG V4L2 v4l2_videodevice.cpp:632 /dev/video1[18:cap]: Opened device platform:vimc.0: vimc: vimc
;36mDEBUG Camera camera_manager.cpp:113 Found registered pipeline handler 'PipelineHandlerUVC'
;36mDEBUG Camera camera_manager.cpp:113 Found registered pipeline handler 'SimplePipelineHandler'
;36mDEBUG Camera camera_manager.cpp:113 Found registered pipeline handler 'PipelineHandlerVc4'
;36mDEBUG Camera camera_manager.cpp:113 Found registered pipeline handler 'PipelineHandlerRkISP1'
```

출력에서 볼 것:

- "Defer media device /dev/media0 due to 11 missing dependencies" → "All dependencies found": libcamera는 media 그래프의 **모든 devnode가 udev에 나타날 때까지** 기다린다(vimc는 video 3 + subdev 8 = 11). 실보드에서 async가 미완성이면 노드가 덜 생겨 여기서 영원히 defer된다.
- vimc handler가 match에 성공하자마자 **링크 세 개를 끄고(`: 0`) Debayer B → Scaler를 켠다(`: 1`)**. §3.6의 범인이 정확히 이 네 줄이다. 그리고 video2와 video1(= Sensor B 경로의 Raw Capture 1)을 연다 — libcamera의 vimc 카메라는 Sensor B 경로를 쓴다.

### 8.3 예제 19 — 고장 2: IPA가 없으면 카메라를 만들지 않는다

```sh
cam -l 2>&1 | grep -E 'VIMC|Available' | cut -c30-200
cam -c1 --capture=5 2>&1 | grep -v -E 'IPAModule|IPAManager|CameraSensor' | cut -c1-160
gst-launch-1.0 libcamerasrc ! fakesink 2>&1 | grep -E 'ERROR|reason|Could not' | cut -c1-200 | head -4
```

```text
;31mERROR VIMC vimc.cpp:469 no matching IPA found

[0:05:02.450384256] [267]  INFO Camera camera_manager.cpp:284 libcamera v0.2.0
Camera 1 not found
Failed to create camera session
[0:05:19.009117008] [295] ERROR IPAModule ipa_module.cpp:172 Symbol ipaModuleInfo not found
[0:05:19.010359712] [295] ERROR IPAModule ipa_module.cpp:292 v4l2-compat.so: IPA module has no valid info
ERROR: from element /GstPipeline:pipeline0/GstLibcameraSrc:libcamerasrc0: Could not find any supported camera on this system.
ERROR: pipeline doesn't want to preroll.
```

(첫 줄 뒤의 빈 줄은 `Available cameras:`가 30자보다 짧아 `cut -c30-`에 잘린 것이다. 두 번째·세 번째 명령은 udev를 띄우기 전에 실행했다.)

출력에서 볼 것: vimc pipeline handler는 match까지 하고 나서 `no matching IPA found`로 카메라 등록을 포기한다. 그래서 `cam -c1`은 "Camera 1 not found", `libcamerasrc`는 "Could not find any supported camera". **V4L2 경로(v4l2-ctl, GStreamer `v4l2src`)는 멀쩡히 되는데 libcamera만 안 되는** 전형적인 상황이고, 원인은 커널이 아니라 rootfs 패키징(IPA `.so` 누락, 또는 IPA 서명·경로 불일치)이다. 실보드에서는 같은 증상이 "센서 튜닝 파일(`<sensor>.yaml`)이 없다", "pipeline handler가 빌드에서 빠졌다(Yocto/Buildroot의 `pipelines=` 옵션)"로도 온다(K3·K4). 이 랩에서 vimc용 libcamera 캡처(`cam -c1 --capture`)는 IPA가 없어서 **실행 불가**였고, 패키지 설치는 하지 않았다(규칙).

정상이면 이렇게 쓴다(가이드, 실행 안 함): `cam -l`로 카메라 ID 확인 → `cam -c1 --capture=10 --file=frame-#.bin` → `cam -c1 -p`로 속성 보기. GStreamer는 `libcamerasrc camera-name=<id> ! video/x-raw,width=1280,height=720 ! ...`.

---

## 9. GStreamer — `v4l2src`, caps 협상, fps 측정, dmabuf

### 9.1 예제 20 — caps 협상 결과와 fps를 `-v`와 `fpsdisplaysink`로 보기

무엇을 확인하는 명령인지: GStreamer가 v4l2src와 하류 사이에 합의한 포맷(caps)과 실제 fps. 화면이 없는 장비에서는 `fpsdisplaysink video-sink=fakesink text-overlay=false`가 표준 측정법이다.

```sh
gst-launch-1.0 -v v4l2src device=/dev/video3 num-buffers=90 io-mode=mmap \
  ! video/x-raw,format=YUY2,width=640,height=480,framerate=30/1 \
  ! fpsdisplaysink video-sink=fakesink text-overlay=false signal-fps-measurements=true 2>&1 \
  | grep -E 'caps =|fps-measurements|last-message|Execution|ERROR|WARN' | cut -c1-260 | head -20
```

```text
/GstPipeline:pipeline0/GstV4l2Src:v4l2src0.GstPad:src: caps = video/x-raw, format=(string)YUY2, width=(int)640, height=(int)480, framerate=(fraction)30/1, pixel-aspect-ratio=(fraction)1/1, interlace-mode=(string)progressive, colorimetry=(string)2:4:7:1
/GstPipeline:pipeline0/GstCapsFilter:capsfilter0.GstPad:src: caps = video/x-raw, format=(string)YUY2, width=(int)640, height=(int)480, framerate=(fraction)30/1, pixel-aspect-ratio=(fraction)1/1, interlace-mode=(string)progressive, colorimetry=(string)2:4:7:1
/GstPipeline:pipeline0/GstFPSDisplaySink:fpsdisplaysink0.GstGhostPad:sink.GstProxyPad:proxypad0: caps = video/x-raw, format=(string)YUY2, width=(int)640, height=(int)480, framerate=(fraction)30/1, pixel-aspect-ratio=(fraction)1/1, interlace-mode=(string)progre
/GstPipeline:pipeline0/GstFPSDisplaySink:fpsdisplaysink0/GstFakeSink:fakesink0.GstPad:sink: caps = video/x-raw, format=(string)YUY2, width=(int)640, height=(int)480, framerate=(fraction)30/1, pixel-aspect-ratio=(fraction)1/1, interlace-mode=(string)progressive
/GstPipeline:pipeline0/GstFPSDisplaySink:fpsdisplaysink0.GstGhostPad:sink: caps = video/x-raw, format=(string)YUY2, width=(int)640, height=(int)480, framerate=(fraction)30/1, pixel-aspect-ratio=(fraction)1/1, interlace-mode=(string)progressive, colorimetry=(st
/GstPipeline:pipeline0/GstCapsFilter:capsfilter0.GstPad:sink: caps = video/x-raw, format=(string)YUY2, width=(int)640, height=(int)480, framerate=(fraction)30/1, pixel-aspect-ratio=(fraction)1/1, interlace-mode=(string)progressive, colorimetry=(string)2:4:7:1
/GstPipeline:pipeline0/GstFPSDisplaySink:fpsdisplaysink0: last-message = rendered: 17, dropped: 0, current: 32.52, average: 32.52
/GstPipeline:pipeline0/GstFPSDisplaySink:fpsdisplaysink0: last-message = rendered: 32, dropped: 0, current: 29.92, average: 31.24
/GstPipeline:pipeline0/GstFPSDisplaySink:fpsdisplaysink0: last-message = rendered: 48, dropped: 0, current: 29.93, average: 30.79
/GstPipeline:pipeline0/GstFPSDisplaySink:fpsdisplaysink0: last-message = rendered: 63, dropped: 0, current: 29.54, average: 30.49
/GstPipeline:pipeline0/GstFPSDisplaySink:fpsdisplaysink0: last-message = rendered: 78, dropped: 0, current: 29.85, average: 30.36
Execution ended after 0:00:03.653122384
```

출력에서 볼 것: GStreamer의 `YUY2` = V4L2의 `YUYV`(이름이 다르다). `colorimetry=2:4:7:1`은 range·matrix·transfer·primaries 코드(GStreamer 표기). **`fpsdisplaysink`의 `dropped`는 싱크가 늦어서 버린(QoS) 프레임**이지 V4L2 드라이버 드롭이 아니다. 드라이버 드롭은 다음 예제처럼 v4l2src 경고로 봐야 한다.

### 9.2 예제 21 — `not-negotiated`와 "lost frames detected"

무엇을 확인하는 명령인지: (1) 장치가 못 주는 caps를 요구하면 무슨 에러가 나나 (2) 드라이버가 프레임을 버리면 GStreamer는 어떻게 알려 주나. 동시에 `io-mode=dmabuf`(v4l2src가 EXPBUF한 dma-buf로 하류에 넘김)도 쓴다.

```sh
gst-launch-1.0 v4l2src device=/dev/video3 num-buffers=10 \
  ! video/x-raw,format=NV12,width=640,height=480,framerate=1000/1 ! fakesink 2>&1 \
  | grep -E 'reason|not-negotiated|streaming stopped' | cut -c1-220
v4l2-ctl -d /dev/video3 --list-formats | grep -E "NV12|YUYV"
gst-launch-1.0 v4l2src device=/dev/video3 num-buffers=10 \
  ! video/x-raw,format=NV12,width=640,height=480,framerate=30/1 ! fakesink 2>&1 | grep -E 'Execution|reason'
```

```text
streaming stopped, reason not-negotiated (-4)
	[0]: 'YUYV' (YUYV 4:2:2, csc-colorspace, csc-ycbcr, csc-quantization, csc-xfer-func)
	[7]: 'NV12' (Y/UV 4:2:0, csc-colorspace, csc-ycbcr, csc-quantization, csc-xfer-func)
Execution ended after 0:00:01.003869792
```

출력에서 볼 것: NV12 640x480은 지원 목록에 있고(30/1이면 된다), 실패 원인은 `framerate=1000/1`이었다(예제 9의 열거 목록에서 640x480 최대가 120 fps). GStreamer 에러 메시지는 "어느 필드가" 안 맞는지 말해 주지 않는다. 그래서 `not-negotiated`를 보면 caps 필터를 하나씩 빼 보거나(`video/x-raw` 만 남기고 `-v`로 실제 협상값 확인), `v4l2-ctl --list-formats-ext`와 대조한다. `GST_DEBUG=v4l2src:5,GST_CAPS:5`로 협상 과정을 볼 수도 있다(출력이 매우 길다).

```sh
v4l2-ctl -d /dev/video3 -c percentage_of_dropped_buffers=30
GST_DEBUG=v4l2src:3 gst-launch-1.0 v4l2src device=/dev/video3 num-buffers=60 io-mode=dmabuf \
  ! video/x-raw,format=YUY2,width=640,height=480,framerate=30/1 ! fakesink 2>&1 \
  | grep -E 'lost|WARN|dmabuf|Execution' | cut -c1-200 | head -8
v4l2-ctl -d /dev/video3 -c percentage_of_dropped_buffers=0
```

```text
0:00:01.304004672   225 0xffff84000b70 WARN                 v4l2src gstv4l2src.c:1260:gst_v4l2src_create:<v4l2src0> Timestamp does not correlate with any clock, ignori
0:00:01.430213728   225 0xffff84000b70 WARN                 v4l2src gstv4l2src.c:1343:gst_v4l2src_create:<v4l2src0> lost frames detected: count = 1 - ts: 0:00:00.65100
0:00:01.766045552   225 0xffff84000b70 WARN                 v4l2src gstv4l2src.c:1343:gst_v4l2src_create:<v4l2src0> lost frames detected: count = 1 - ts: 0:00:00.98661
0:00:01.832468016   225 0xffff84000b70 WARN                 v4l2src gstv4l2src.c:1343:gst_v4l2src_create:<v4l2src0> lost frames detected: count = 1 - ts: 0:00:01.05330
0:00:02.197418864   225 0xffff84000b70 WARN                 v4l2src gstv4l2src.c:1343:gst_v4l2src_create:<v4l2src0> lost frames detected: count = 2 - ts: 0:00:01.41798
0:00:02.297842480   225 0xffff84000b70 WARN                 v4l2src gstv4l2src.c:1343:gst_v4l2src_create:<v4l2src0> lost frames detected: count = 2 - ts: 0:00:01.51888
0:00:02.904663552   225 0xffff84000b70 WARN                 v4l2src gstv4l2src.c:1343:gst_v4l2src_create:<v4l2src0> lost frames detected: count = 1 - ts: 0:00:02.12525
0:00:03.035483120   225 0xffff84000b70 WARN                 v4l2src gstv4l2src.c:1343:gst_v4l2src_create:<v4l2src0> lost frames detected: count = 2 - ts: 0:00:02.25642
```

출력에서 볼 것:

- `lost frames detected: count = N` — v4l2src가 버퍼의 **sequence 틈**을 보고 찍는 경고다. 기본 GST_DEBUG 레벨에서는 안 보이므로 현장 파이프라인은 `GST_DEBUG=v4l2src:3`(WARNING)을 켜 두는 것이 좋다. `fpsdisplaysink`의 `dropped: 0`과는 별개다.
- 첫 줄 "Timestamp does not correlate with any clock, ignoring driver timestamps" — v4l2src가 드라이버 timestamp를 버리고 자기 시계로 찍었다는 뜻이다. vivid timestamp는 `ktime_get_ns()`(MONOTONIC) 기반인데도(`vivid-kthread-cap.c`) 이 경고가 떴고, 정확한 판정 조건은 GStreamer 소스로 확인하지 않았다. 센서 퓨전에서 GStreamer를 쓸 때는 이 경고가 뜨는지 반드시 확인한다 — 뜨면 버퍼 PTS가 **노출 시각이 아니라 유저 공간 도착 시각**이다(Q3).
- `io-mode=dmabuf`는 오류 없이 동작했다. 하류가 dma-buf를 받을 수 있는 요소(HW 인코더 `v4l2h264enc`, `glupload`, NPU 추론 요소)면 복사 없이 넘어간다. `fakesink`는 그냥 받기만 한다.

### 9.3 예제 22 — vimc(MC 파이프라인)도 `v4l2src`로: 그래프 설정은 GStreamer 바깥에서

```sh
M='media-ctl -d /dev/media0'
$M -r; $M -l '"Debayer A":1->"Scaler":0[1]'
$M -V '"Sensor A":0[fmt:SRGGB8_1X8/640x480], "Scaler":1[fmt:RGB888_1X24/640x480]'
v4l2-ctl -d /dev/video0 -v width=640,height=480,pixelformat=RGGB
gst-launch-1.0 -v v4l2src device=/dev/video2 num-buffers=6 ! video/x-raw,format=RGB,width=640,height=480 \
  ! fakesink 2>&1 | grep -E 'v4l2src0.GstPad:src: caps|Execution|reason' | cut -c1-200
```

```text
/GstPipeline:pipeline0/GstV4l2Src:v4l2src0.GstPad:src: caps = video/x-raw, format=(string)RGB, width=(int)640, height=(int)480, pixel-aspect-ratio=(fraction)1/1, framerate=(fraction)120/1, interlace-mode=(string)progressive, colorimetry=(string)sRGB
Execution ended after 0:00:01.299657952
```

출력에서 볼 것: `v4l2src`는 **video node만** 안다. 링크·subdev 포맷은 미리 media-ctl로 맞춰 놔야 하고, 안 맞으면 GStreamer에서는 "Internal data stream error" 정도로만 보인다(원인 `-EPIPE`는 dmesg). caps의 `framerate=120/1`은 vimc 캡처 노드가 보고한 값일 뿐, 실제로는 예제 5처럼 몇 fps밖에 안 나온다 — **caps의 framerate를 측정값으로 믿지 말 것**. 실제 SoC에서도 MC 카메라를 `v4l2src`로 쓰는 제품은 보통 부팅 스크립트(systemd 유닛)에서 media-ctl을 먼저 돌린다. 이 일을 자동화한 것이 libcamera(`libcamerasrc`)다.

---

## 10. 커널 안에서는 무슨 일이 일어나나

### 10.1 STREAMON 한 번의 경로

```text
VIDIOC_STREAMON (유저)
 → v4l2_ioctl → vb2_ioctl_streamon → vb2_streamon → vb2_core_streamon
     → 드라이버 .start_streaming(vq, count)            (vb2_ops — I5 §12.3)
         → video_device_pipeline_start(vdev, pipe)     (drivers/media/v4l2-core/v4l2-dev.c)
             → __media_pipeline_start(pad, pipe)        (drivers/media/mc/mc-entity.c)
                 → media_pipeline_populate()            ← "media pipeline: added pad ..." 로그
                 → 각 링크: sink entity->ops->link_validate(link)
                      subdev 쪽: v4l2_subdev_link_validate()
                                 → v4l2_subdev_link_validate_default()  ← "media bus code does not match"
                      video 쪽 : 드라이버 구현 (vimc_vdev_link_validate)  ← "vimc link validate: ..."
                 → 실패 시 "Link '...' failed validation: -32" → -EPIPE 반환
         → 상류 subdev들 s_stream(1) / v4l2_subdev_enable_streams()  (센서가 실제로 MIPI 출력 시작)
     → 실패하면 queued 버퍼를 전부 QUEUED 상태로 돌려주고 에러
```

주의할 점 두 개. (1) 링크 검증 실패는 드라이버의 `start_streaming` 안에서 나므로 vb2 규칙상 드라이버는 받은 버퍼를 `vb2_buffer_done(..., VB2_BUF_STATE_QUEUED)`로 돌려줘야 한다 — 이걸 빼먹은 드라이버는 `-EPIPE` 뒤에 WARN을 찍는다(I5 §12). (2) 실제 센서는 `s_stream(1)`에서야 I2C로 streaming 레지스터를 켠다. 그래서 "STREAMON 성공인데 DQBUF가 안 온다"는 **센서 레지스터 시퀀스·CSI-2 PHY 설정·lane 수·link frequency** 쪽이다(Q1). 그때 볼 것은 수신기 드라이버의 에러 IRQ 카운터(많은 CSI-2 수신기 드라이버가 debugfs나 `v4l2-ctl --log-status`로 노출)다.

### 10.2 드롭이 생기는 커널 지점

vivid는 `vivid-kthread-cap.c`에서 프레임 시점마다 `vid_cap_active` 리스트(= 그림 4의 queued)에서 버퍼를 하나 꺼낸다. 리스트가 비어 있거나 드롭 주입 확률에 걸리면 그 프레임은 건너뛰고, sequence 카운터는 시간 기준으로 계속 올라간다 — 그래서 유저는 틈으로 본다. 실제 SoC 드라이버도 같은 모양이다: 프레임 시작(또는 끝) IRQ에서 다음 DMA 주소를 쓸 버퍼가 없으면 (a) 그 프레임을 버리거나 (b) 내부 scratch 버퍼에 쓰게 하고 버린다. 어느 쪽이든 `vb2_buffer_done()`에 넘기는 `vbuf->sequence`가 HW 프레임 카운터를 따라가야 유저가 드롭을 알 수 있다. **sequence를 "완료한 버퍼 수"로 매기는 드라이버는 드롭을 숨긴다** — 드라이버 리뷰에서 꼭 보는 부분이다.

### 10.3 async와 fwnode — 그래프가 생기는 시점

수신기 probe: `v4l2_async_nf_init(&nf, &v4l2_dev)` → DT endpoint마다 `v4l2_fwnode_endpoint_alloc_parse()`(data-lanes, link-frequencies 읽기) → `v4l2_async_nf_add_fwnode_remote(&nf, ep, struct ...)` → `nf.ops = { .bound, .complete, .unbind }` → `v4l2_async_nf_register(&nf)`. 센서 probe: I2C로 chip ID 확인 → `v4l2_async_register_subdev(sd)`. 매칭되면 `.bound`에서 `media_create_pad_link()`(센서 → 수신기 링크), 모두 모이면 `.complete`에서 `v4l2_device_register_subdev_nodes()` → `/dev/v4l-subdevN` 생성. 이 순서는 I5 §11에서 imx219와 i.MX8MP CSI-2 드라이버 코드로 따라갔다. 이 노트 §6.2의 `pending_async_subdevices`가 이 대기 목록을 보여 주는 창이다.

### 10.4 Don 경험과 연결

- link validation = SSD 컨트롤러에서 DMA 체인을 시작하기 전에 각 스테이지 디스크립터의 transfer size·포맷이 일치하는지 펌웨어가 assert하는 것. 차이: Linux는 이걸 **런타임 설정**으로 바꿀 수 있게 열어 두었고, 그래서 설정이 어긋날 수 있다.
- sequence 틈 = NVMe command ID나 NAND page 카운터의 연속성 검사. 카운터를 HW에서 받아야 의미가 있다는 것도 같다.
- 그래프 상태가 전역 = 공유 레지스터 블록을 여러 펌웨어 태스크가 건드리는 상황. 소유권(누가 설정하나)을 정하지 않으면 마지막에 쓴 쪽이 이긴다(§3.6).

---

## 11. Embedded 실무에서 다시 보기 — "카메라가 안 나와요" 진단 체크리스트

예를 들어 센서 키트(로봇 전방 카메라 2대, i.MX8MP + IMX219 × 2)에서 "카메라 2번이 안 나온다"는 티켓을 받았다면 아래를 **순서대로** 본다. 각 단계는 앞 단계가 통과해야 의미가 있다.

1. **probe** — `dmesg | grep -i -E 'imx219|csi|isi|probe'`, `ls /dev/v4l-subdev* /dev/media*`, `cat /sys/kernel/debug/v4l2-async/pending_async_subdevices`, `cat /sys/kernel/debug/devices_deferred`. 센서 entity가 그래프에 없으면 I2C(`i2cdetect -y N`), 전원 시퀀스(regulator, reset GPIO, MCLK), DT `remote-endpoint` 짝을 본다(P4, D3).
2. **링크** — `media-ctl -d /dev/mediaN -p`에서 센서 → 수신기 → ISP → 원하는 video node 경로가 전부 `[ENABLED]`인지. 다른 프로세스(libcamera 앱, 이전 스크립트)가 바꿨을 수 있으니 `media-ctl -r` 후 다시 설정.
3. **포맷** — 상류부터 `media-ctl -V`로 pad 포맷을 맞추고 끝단은 `v4l2-ctl -v`. STREAMON `Broken pipe`면 `echo 'file drivers/media/v4l2-core/v4l2-subdev.c +p' > /proc/dynamic_debug/control` 후 다시 실행해서 dmesg의 "does not match" 줄과 "link was" 줄을 본다. 같은 센서에 IMMUTABLE로 붙은 다른 노드도 맞춘다.
4. **스트리밍** — `v4l2-ctl -d /dev/videoN --stream-mmap --stream-count=30 --verbose`. STREAMON은 되는데 `cap dqbuf`가 안 나오면(멈춤) 센서 streaming 레지스터, CSI-2 lane 수·link-frequency·D-PHY 설정, 수신기 에러 카운터(`v4l2-ctl -d /dev/v4l-subdevN --log-status`)를 본다(Q1). 센서 `test_pattern`을 켜서 광학 문제와 분리.
5. **버퍼** — `--verbose`의 `seq` 틈, 또는 `q2_seqmon`. 버퍼를 늘려서 줄면 소비자 문제, 그대로면 생산자 문제(그림 5의 논리). `V4L2_BUF_FLAG_ERROR` 버퍼 수도 센다.
6. **타임스탬프** — flags(`ts-monotonic`, `ts-src-eof`/`soe`) 확인, delta의 최소/최대. GStreamer면 "Timestamp does not correlate" 경고 여부. 두 카메라 간 동기는 Q3.
7. **프레임워크** — libcamera면 `LIBCAMERA_LOG_LEVELS='*:DEBUG' cam -l`로 enumerator(udev)·pipeline handler match·IPA 로딩 각각 확인. GStreamer면 `gst-launch-1.0 -v`로 caps, `GST_DEBUG=v4l2src:3`로 lost frames.
8. **데이터 정확성** — 테스트 패턴 한 프레임을 `--stream-to`로 떠서 바이트 확인(예제 11). 색이 틀리면 Bayer 순서·YUV 바이트 순서.

```svg
<svg viewBox="0 0 680 560" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="10" width="300" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="31" text-anchor="middle" font-size="13">1 센서 entity가 그래프에 있나?</text>
  <text x="170" y="50" text-anchor="middle" font-size="12">media-ctl -p, v4l2-async pending</text>
  <rect x="380" y="10" width="280" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="520" y="31" text-anchor="middle" font-size="13">I2C, 전원·리셋·MCLK, DT 짝</text>
  <text x="520" y="50" text-anchor="middle" font-size="12">-EPROBE_DEFER, devices_deferred</text>
  <rect x="20" y="100" width="300" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="121" text-anchor="middle" font-size="13">2 경로의 링크가 전부 ENABLED?</text>
  <text x="170" y="140" text-anchor="middle" font-size="12">media-ctl -p</text>
  <rect x="380" y="100" width="280" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="520" y="121" text-anchor="middle" font-size="13">media-ctl -r 후 -l 로 다시 설정</text>
  <text x="520" y="140" text-anchor="middle" font-size="12">누가 바꿨나 (libcamera 등)</text>
  <rect x="20" y="190" width="300" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="211" text-anchor="middle" font-size="13">3 STREAMON 성공?</text>
  <text x="170" y="230" text-anchor="middle" font-size="12">v4l2-ctl --stream-mmap</text>
  <rect x="380" y="190" width="280" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="520" y="211" text-anchor="middle" font-size="13">-EPIPE: dyndbg로 실패 링크 찾기</text>
  <text x="520" y="230" text-anchor="middle" font-size="12">code, fourcc, 크기 → -V, -v</text>
  <rect x="20" y="280" width="300" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="301" text-anchor="middle" font-size="13">4 DQBUF가 오나?</text>
  <text x="170" y="320" text-anchor="middle" font-size="12">--verbose 에 cap dqbuf 줄</text>
  <rect x="380" y="280" width="280" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="520" y="301" text-anchor="middle" font-size="13">센서 stream 레지스터, CSI-2</text>
  <text x="520" y="320" text-anchor="middle" font-size="12">lane, link-freq, 에러 IRQ (Q1)</text>
  <rect x="20" y="370" width="300" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="391" text-anchor="middle" font-size="13">5 seq 틈이 없나?</text>
  <text x="170" y="410" text-anchor="middle" font-size="12">q2_seqmon, ERROR 플래그</text>
  <rect x="380" y="370" width="280" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="520" y="391" text-anchor="middle" font-size="13">버퍼 늘려 줄면 소비자 문제</text>
  <text x="520" y="410" text-anchor="middle" font-size="12">그대로면 생산자·대역폭 문제</text>
  <rect x="20" y="460" width="300" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="481" text-anchor="middle" font-size="13">6 ts flags·간격이 기대대로?</text>
  <text x="170" y="500" text-anchor="middle" font-size="12">SoE/EoF, delta min/max</text>
  <rect x="380" y="460" width="280" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="520" y="481" text-anchor="middle" font-size="13">IRQ 지연, SW timestamp</text>
  <text x="520" y="500" text-anchor="middle" font-size="12">GStreamer 경고 확인 (Q3)</text>
  <line x1="320" y1="35" x2="378" y2="35" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="320" y1="125" x2="378" y2="125" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="320" y1="215" x2="378" y2="215" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="320" y1="305" x2="378" y2="305" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="320" y1="395" x2="378" y2="395" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="320" y1="485" x2="378" y2="485" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="349" y="28" text-anchor="middle" font-size="12">아니오</text>
  <text x="349" y="118" text-anchor="middle" font-size="12">아니오</text>
  <text x="349" y="208" text-anchor="middle" font-size="12">아니오</text>
  <text x="349" y="298" text-anchor="middle" font-size="12">아니오</text>
  <text x="349" y="388" text-anchor="middle" font-size="12">아니오</text>
  <text x="349" y="478" text-anchor="middle" font-size="12">아니오</text>
  <line x1="170" y1="60" x2="170" y2="98" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="170" y1="150" x2="170" y2="188" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="170" y1="240" x2="170" y2="278" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="170" y1="330" x2="170" y2="368" stroke="currentColor" marker-end="url(#ah5)"/>
  <line x1="170" y1="420" x2="170" y2="458" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="178" y="84" font-size="12">예</text>
  <text x="178" y="174" font-size="12">예</text>
  <text x="178" y="264" font-size="12">예</text>
  <text x="178" y="354" font-size="12">예</text>
  <text x="178" y="444" font-size="12">예</text>
  <rect x="20" y="525" width="640" height="30" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="545" text-anchor="middle" font-size="12">다 통과 → 앱 층: libcamera(udev·handler·IPA), GStreamer(caps), 패턴 확인</text>
  <line x1="170" y1="510" x2="170" y2="523" stroke="currentColor" marker-end="url(#ah5)"/>
</svg>
```
그림 6 — "카메라가 안 나온다" 결정 트리. 왼쪽 기둥을 위에서부터 내려가며 첫 "아니오"에서 오른쪽 상자의 조치를 한다. 이 노트의 예제들이 각 상자의 실제 출력이다: 1은 예제 17, 2는 예제 7·8, 3은 예제 3~6, 5는 예제 13·14, 6은 예제 12·14, 맨 아래는 §8·§9.

### 11.1 두 대 이상의 카메라 — 이름으로 찾기

번호가 바뀌는 문제(예제 1) 때문에 제품 코드는 entity 이름으로 노드를 찾는다. `media-ctl -e`가 그 일을 한다(가이드, 이 랩에서는 실행 안 함):

```sh
# entity 이름 → /dev 노드
media-ctl -d /dev/media0 -e 'RGB/YUV Capture'      # 출력 예: /dev/video2
```

udev 규칙으로 `/dev/cam-front` 같은 고정 심볼릭 링크를 만드는 방법도 있다(L2). ROS 2 `v4l2_camera` 노드에는 이 고정 경로를 준다(Q6).

### 11.2 부팅 시 파이프라인 설정을 서비스로

MC 카메라를 `v4l2src`나 자체 앱으로 쓰는 제품은 부팅 때 그래프를 고정한다. 예제 3~6의 명령을 그대로 셸 스크립트로 만들고 systemd oneshot 유닛(L1)으로 앱 앞에 둔다. 스크립트 첫 줄은 `media-ctl -r`, 마지막 줄은 짧은 `--stream-count=5` 자가 테스트로 하면 "부팅했는데 카메라가 안 된다"를 서비스 실패로 바로 드러낼 수 있다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 센서 포맷만 바꾸고 하류 sink를 안 맞춤 | STREAMON `Broken pipe` | 포맷은 링크를 건너 전파되지 않음 | 상류 → 하류로 `media-ctl -V` 전체 설정, dyndbg로 실패 링크 확인 |
| 쓰지 않는 raw 노드 포맷 방치 | 원하는 노드만 열었는데 `-EPIPE` | IMMUTABLE 링크로 붙은 노드도 파이프라인에 포함 | 그 노드도 `v4l2-ctl -v`로 맞춤 |
| 스크립트가 이전 상태를 가정 | 어제 되던 스크립트가 오늘 `-EPIPE` | 그래프·포맷은 전역 상태, libcamera 등이 변경 | 매번 `media-ctl -r`부터 전체 재설정 |
| `/dev/video0` 하드코딩 | 다른 장치가 열리거나 ENOTTY | 노드 번호는 probe 순서 | entity 이름(`media-ctl -e`)이나 udev 고정 링크 |
| 드롭을 fps로만 봄 | "27 fps"라는데 원인 모름 | DQBUF 수는 드롭과 지연을 구분 못 함 | sequence 틈과 버퍼 수 실험으로 생산자/소비자 구분 |
| fpsdisplaysink의 dropped를 드라이버 드롭으로 해석 | "dropped 0"인데 영상이 끊김 | 그건 싱크 QoS 드롭 | `GST_DEBUG=v4l2src:3`의 lost frames, 또는 seq 직접 확인 |
| timestamp 출처 가정 | 퓨전에서 수십 ms 오프셋 | SoE/EoF, 또는 GStreamer가 드라이버 ts를 버림 | 매 버퍼 flags 확인, GStreamer 경고 확인 |
| libcamera 최소 rootfs | "Available cameras:" 비어 있음 | udev 없음 또는 IPA `.so` 없음 | udevd 실행, IPA 패키지·pipeline handler 빌드 옵션 확인 |
| GStreamer caps에 장치가 못 주는 값 | `not-negotiated (-4)` | 해상도·fps 조합이 열거 목록에 없음 | `--list-formats-ext`와 대조, caps를 줄여 `-v`로 확인 |
| `V4L2_BUF_FLAG_ERROR` 무시 | 가끔 깨진 프레임이 추론에 들어감 | seq만 보고 flags 안 봄 | ERROR 버퍼는 버리고 카운트 |

---

## 13. 면접에서 이렇게 말한다

**Q.** "A camera on a new board probes fine but `/dev/video0` gives `VIDIOC_STREAMON: Broken pipe`. What do you do?"

**A.** Broken pipe는 media controller 링크 검증 실패(-EPIPE)다. `media-ctl -p`로 경로의 링크와 pad 포맷을 나란히 보고, v4l2-subdev.c dynamic debug를 켜서 dmesg의 "does not match" 줄로 어느 링크의 mbus code·크기가 다른지 확인한 뒤, 센서부터 하류로 `media-ctl -V`와 끝단 `v4l2-ctl -v`로 맞춘다. 같은 센서에 붙은 다른 video node도 파이프라인에 들어간다는 점까지 확인한다.

> EPIPE from STREAMON means media-controller link validation failed. I print the graph with `media-ctl -p`, enable dynamic debug for v4l2-subdev.c and re-run, so dmesg tells me exactly which link has mismatched media bus code, size or field. Then I set formats from the sensor downstream with `media-ctl -V`, and the video node with `v4l2-ctl`. A common trap is a second node, like a raw capture node, attached by an immutable link — it is part of the pipeline and gets validated too.

**꼬리 질문**: "Who propagates the format?" → subdev 안에서 sink → source로만, 링크를 건너서는 사람이나 libcamera가 한다.

**Q.** "How do you detect and diagnose dropped frames?"

**A.** DQBUF 개수가 아니라 `v4l2_buffer.sequence`의 틈으로 센다. 그다음 버퍼 수를 늘려 본다: 줄어들면 소비자가 늦은 것이고, 그대로면 센서·CSI·ISP·대역폭 쪽에서 버리는 것이다. ERROR 플래그 버퍼도 따로 센다. 이 VM에서 느린 소비자 실험을 했을 때 버퍼 2개로는 17%, 8개로는 2.6%였고, 드라이버 쪽 드롭은 버퍼와 무관했다.

> I count gaps in the buffer sequence number, not the number of dequeued buffers. Then I vary the buffer count: if more buffers reduce the loss, the consumer is occasionally late; if not, the producer side — sensor, CSI-2 receiver, ISP overflow or memory bandwidth — is dropping. I also count buffers flagged V4L2_BUF_FLAG_ERROR, because those arrive but may be corrupt.

**꼬리 질문**: "Why not just use lots of buffers?" → 메모리(CMA)와 지연. 제어 루프는 최신 프레임이 중요하다.

**Q.** "What does libcamera add over plain V4L2?"

**A.** MC 카메라에서 링크·포맷 설정을 SoC별 pipeline handler가 하고, ISP 통계를 읽어 노출·게인·화이트밸런스를 매 프레임 돌리는 IPA(3A)를 제공한다. 대가로 udev, IPA 모듈, 튜닝 파일 같은 의존성이 생긴다. 실제로 IPA가 없는 rootfs에서 vimc 핸들러가 match는 하고 "no matching IPA found"로 카메라를 안 만든 것을 봤다.

> libcamera moves the per-SoC graph configuration into pipeline handlers and adds IPA modules that run the 3A loop from ISP statistics. It needs udev for enumeration, IPA shared objects and sensor tuning files; when one is missing, plain V4L2 still works while libcamera reports no cameras — I reproduced exactly that with the vimc handler.

**Q.** "MMAP vs DMABUF — when do you use which?"

**A.** MMAP은 드라이버가 할당하고 앱이 매핑해서 읽는 기본형. 프레임을 인코더·GPU·NPU로 넘길 때는 EXPBUF로 dma-buf fd를 내보내거나(export), 상대가 할당한 버퍼를 받아(import) 복사를 없앤다. 디버그할 때는 `/sys/kernel/debug/dma_buf/bufinfo`로 exporter와 attach된 장치를 본다.

> MMAP is the default: the driver allocates and the application maps. When frames go to an encoder, GPU or NPU, I export the capture buffers as dma-buf file descriptors, or import buffers allocated by the consumer, so there is no copy. The dma_buf debugfs file shows the exporter and attached devices, which is the first thing I check when zero-copy silently falls back to copying.

**Q.** "GStreamer pipeline fails with not-negotiated. How do you debug it?"

**A.** 장치가 실제로 주는 조합을 `v4l2-ctl --list-formats-ext`로 보고, caps 필터를 줄여 `gst-launch-1.0 -v`로 실제 합의값을 확인한다. 이름 차이(GStreamer YUY2 = V4L2 YUYV)와 framerate 범위가 흔한 원인이다. 필요하면 `GST_DEBUG=GST_CAPS:5`.

> I compare the requested caps with what the device enumerates via `v4l2-ctl --list-formats-ext`, then relax the caps filter and run with `-v` to see what was actually negotiated. Usually it is an unsupported resolution and framerate combination or a naming mismatch, like YUY2 versus YUYV.

**Q.** "Where does the buffer timestamp come from, and why does it matter?"

**A.** 드라이버가 IRQ 시점에 `CLOCK_MONOTONIC`으로 찍고, flags가 프레임 시작(SoE)인지 끝(EoF)인지 알려 준다. 퓨전에서는 노출 중심 시각이 필요하므로 이 차이와 readout 시간을 보정한다. GStreamer가 드라이버 timestamp를 버리고 자기 시계를 쓰는 경우도 있으니 경고를 확인한다.

> The driver stamps buffers with CLOCK_MONOTONIC in the interrupt path, and the flags say whether that is start or end of frame. For sensor fusion I need the exposure midpoint, so I correct for that and for readout time. I also check whether the framework kept the driver timestamp — GStreamer's v4l2src can ignore it and stamp arrival time instead.

**Q.** "The sensor driver probed, but the camera doesn't show up in the media graph."

**A.** async 매칭이 안 끝난 것이다. `v4l2-async/pending_async_subdevices`와 `devices_deferred`를 보고, DT의 endpoint가 서로를 `remote-endpoint`로 가리키는지, 센서 probe가 I2C·전원·클럭 단계에서 실패하지 않았는지 확인한다.

> That is usually an async notifier that never completed. I check the v4l2-async pending list in debugfs and the deferred-probe list, then verify that both endpoints point at each other with remote-endpoint in the device tree and that the sensor actually passed its chip-ID read over I2C.

---

## 14. 직접 해보기

1. 개념: vimc에서 `"RGB/YUV Input":0 -> "Scaler":0`을 켜고 Debayer 링크를 다 끄면, `/dev/video2` 스트리밍 때 파이프라인에 들어가는 pad 목록이 어떻게 바뀌나? 예측한 뒤 mc-entity.c dyndbg로 확인하라. 정답/힌트: Sensor·Debayer·Raw Capture가 빠지고 Input → Scaler → Capture만 남는다. Raw Capture 포맷 문제도 사라진다.
2. 명령 과제: Scaler sink에 `crop:(160,120)/320x240`을 주고(`media-ctl -V '"Scaler":0[crop:(160,120)/320x240]'`) source를 640x480으로 하면 무슨 영상이 나오나? 한 프레임을 떠서 첫 픽셀 값을 예제 5와 비교하라. 정답/힌트: 가운데 1/4 영역을 2배 확대한 것. 첫 픽셀이 75% 흰색이 아닐 수 있다(크롭 시작이 다른 막대).
3. 측정 과제: `q2_seqmon`에 "연속 드롭 최대 길이"와 "delta가 1.5 × 기대 간격을 넘은 횟수"를 추가하고, vivid `percentage_of_dropped_buffers`를 0/10/30/50으로 바꿔 그래프를 그려라. 정답/힌트: 기대 간격은 `VIDIOC_G_PARM`의 `timeperframe`에서 읽는다.
4. 관찰 과제: `echo 0x8 > /sys/class/video4linux/video3/dev_debug`로 스트리밍 ioctl만 켜고 `--stream-mmap=2`로 10 프레임 받아라. QBUF와 DQBUF의 index 순서가 어떻게 도나? 정답/힌트: 2개를 번갈아 쓴다. 드롭이 있어도 index 순서는 깨지지 않는다.
5. GStreamer 과제: `v4l2src ! videoconvert ! fakesink`와 `v4l2src io-mode=dmabuf ! fakesink`의 게스트 CPU 사용률을 `top`이나 `perf stat`(M6)으로 비교하라. 정답/힌트: videoconvert가 실제 복사·변환을 하므로 차이가 크다. TCG라 절대값은 의미 없다.
6. libcamera 과제: 이 노트의 고장 1(udev)을 다시 만들고, `LIBCAMERA_LOG_LEVELS=DeviceEnumerator:0`만 켜서 어떤 줄이 빠지는지 확인하라. 정답/힌트: "New media device ... created from /dev/media0"가 udev 없이는 안 나온다.
7. 설계 문제: 전방 카메라 2대(같은 센서)가 같은 CSI-2 수신기 하나에 가상 채널로 들어오는 보드라면 `media-ctl -p`에서 무엇이 달라 보일까? 정답/힌트: 수신기 entity의 `routes`가 0이 아니고, pad 포맷이 `stream:0`, `stream:1`로 나뉜다(streams API, `media-ctl -R`). Q1·Q3에서 이어진다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| media controller (MC) | `/dev/mediaN` API | 장치 안 블록 그래프(entity·pad·link)를 유저에게 보여 주고 설정하게 하는 프레임워크 |
| entity | 그래프의 노드 | 센서, CSI-2 수신기, ISP, DMA video node 하나하나 |
| pad | entity의 포트 | Sink(입력) 또는 Source(출력). subdev는 pad마다 포맷을 가짐 |
| link | pad ↔ pad 배선 | ENABLED / IMMUTABLE 플래그. `media-ctl -l`로 켜고 끔 |
| media bus code | 배선 위 픽셀 형식 | `SRGGB10_1X10`, `RGB888_1X24` — subdev pad 포맷 |
| fourcc (pixelformat) | 메모리 배치 형식 | `YUYV`, `NV12`, `RGGB` — video node 포맷 |
| link validation | STREAMON 때 링크 양끝 검사 | 불일치면 `-EPIPE`(Broken pipe) |
| subdev | `v4l2_subdev` | 버퍼 없이 설정만 갖는 블록. `/dev/v4l-subdevN` |
| async notifier | v4l2-async | 따로 probe되는 센서·수신기를 DT fwnode로 짝지어 그래프를 완성 |
| vb2 | videobuf2 | 버퍼 큐 프레임워크. MMAP/USERPTR/DMABUF, QBUF/DQBUF 상태 기계 |
| sequence | 버퍼의 프레임 번호 | 틈 = 드롭. 버퍼 index와 다르다 |
| SoE / EoF | 타임스탬프 시점 | Start of Exposure / End of Frame (`V4L2_BUF_FLAG_TSTAMP_SRC_*`) |
| EXPBUF | 버퍼 내보내기 ioctl | vb2 버퍼를 dma-buf fd로 만들어 다른 장치와 공유 |
| pipeline handler | libcamera의 SoC별 코드 | 그래프 match, 링크·포맷 설정, 버퍼 흐름 |
| IPA | Image Processing Algorithms | libcamera의 3A 모듈. 없으면 해당 카메라를 만들지 않음 |
| caps | GStreamer capabilities | 요소 사이에 합의한 포맷(형식·크기·fps·colorimetry) |
| not-negotiated | GStreamer 협상 실패 | 상류가 줄 수 있는 caps와 하류 요구가 교집합이 없음 |
| dev_debug | video node sysfs 속성 | V4L2 core가 ioctl과 인자를 dmesg에 찍게 함 |

---

## 16. 요약 & 체크리스트

카메라 파이프라인 디버깅은 층을 하나씩 내려가는 일이다. probe(그래프에 센서가 있나) → 링크(경로가 켜져 있나, 누가 바꿨나) → 포맷(링크 양끝이 같은가, `-EPIPE`면 dyndbg로 실패 링크를 찾는다) → 스트리밍(DQBUF가 오나) → 버퍼(sequence 틈, 버퍼 수 실험으로 생산자/소비자 구분, ERROR 플래그) → 타임스탬프(flags와 간격) → 프레임워크(libcamera의 udev·handler·IPA, GStreamer의 caps·lost frames). 도구는 `media-ctl`(그래프), `v4l2-ctl`(노드·버퍼·측정), `cam`, `gst-launch-1.0 -v`, 그리고 dynamic debug·`dev_debug`·debugfs. 이 랩에서 실제로 본 함정은 IMMUTABLE로 붙은 안 쓰는 노드의 포맷, libcamera가 몰래 바꿔 놓은 링크·포맷, 마지막 앱이 남긴 video node 포맷, udev 없는 rootfs, IPA 없는 libcamera였다.

- [ ] `media-ctl -p` 출력의 entity·pad·link·포맷 줄을 해석하고 그래프로 그릴 수 있다
- [ ] `media-ctl -V`, `-l`, `-r`, `--get-v4l2`의 문법(따옴표 포함)을 쓸 수 있다
- [ ] STREAMON `-EPIPE`의 원인 링크를 dynamic debug로 찾고, 상류 → 하류 순서로 고칠 수 있다
- [ ] IMMUTABLE 링크로 붙은 노드가 파이프라인 검증에 포함되는 이유를 설명할 수 있다
- [ ] 그래프·포맷 상태가 전역이라 다른 프로세스가 바꿀 수 있음을 알고, 스크립트를 리셋부터 쓸 수 있다
- [ ] `v4l2-ctl --list-formats-ext`, `--list-ctrls-menus`, `--set-fmt-video`, `--set-parm`, `--stream-mmap --verbose`를 쓸 수 있다
- [ ] sequence 틈으로 드롭을 세고, 버퍼 수 실험으로 소비자/생산자 원인을 구분할 수 있다
- [ ] timestamp flags(MONOTONIC, SoE/EoF)와 `V4L2_BUF_FLAG_ERROR`를 확인하는 모니터를 C로 짤 수 있다
- [ ] MMAP/USERPTR/DMABUF의 차이와 dma-buf debugfs 확인법을 말할 수 있다
- [ ] libcamera의 enumerator·pipeline handler·IPA 역할과 "카메라 0개"의 원인 후보를 말할 수 있다
- [ ] GStreamer `-v`로 caps를 보고, `not-negotiated`와 `lost frames detected`를 해석할 수 있다
- [ ] "카메라가 안 나온다" 체크리스트 8단계를 순서대로 말할 수 있다

## 참고 자료

- docs.kernel.org — Media subsystem userspace API: V4L2(`userspace-api/media/v4l/v4l2.html`), Media Controller(`userspace-api/media/mediactl/media-controller.html`), Buffers(`userspace-api/media/v4l/buffer.html` — sequence, timestamp flags)
- docs.kernel.org — Driver API: V4L2 sub-devices, async(`driver-api/media/v4l2-subdev.html`), videobuf2(`driver-api/media/v4l2-videobuf2.html`), vimc·vivid 문서(`admin-guide/media/vimc.html`, `admin-guide/media/vivid.html`)
- 랩 소스(검증): `drivers/media/test-drivers/vimc/vimc-core.c`(토폴로지), `vimc-common.c`(`vimc_vdev_link_validate`), `vimc-streamer.c`, `drivers/media/test-drivers/vivid/vivid-kthread-cap.c`(드롭 주입·timestamp), `drivers/media/mc/mc-entity.c`(`__media_pipeline_start`), `drivers/media/v4l2-core/v4l2-subdev.c`(`v4l2_subdev_link_validate_default`), `include/media/v4l2-ioctl.h`(`V4L2_DEV_DEBUG_*`), `include/media/v4l2-async.h`
- v4l-utils 매뉴얼: `man v4l2-ctl`, `man media-ctl` (linuxtv.org v4l-utils)
- libcamera 문서: https://libcamera.org/ (Getting started, Pipeline handler writers guide, `cam` 사용법)
- GStreamer 문서: https://gstreamer.freedesktop.org/documentation/video4linux2/v4l2src.html , `fpsdisplaysink`
- Bootlin — "Embedded Linux camera" 교육 슬라이드(bootlin.com/docs)
