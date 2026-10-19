# Q5. IMU · GNSS · LiDAR on Linux — 세 센서의 bring-up과 데이터 경로를 "시스템"으로 보기

> **이 노트를 다 읽으면**: 업스트림 IMU 드라이버(bmi160·inv_mpu6050·st_lsm6dsx)를 가짜 I2C 칩(i2c-stub)에 붙여 raw·scale·sampling_frequency를 읽고 버퍼 캡처의 실제 샘플률·지터를 타임스탬프로 잴 수 있다 · FIFO 배치로 온 IMU 샘플에 샘플별 타임스탬프를 보간하는 법을 코드로 보일 수 있다 · NMEA/UBX 로그를 직접 만들어 gpsfake → gpsd → JSON 클라이언트로 흘리고 PPS·chrony 연결을 설계할 수 있다 · LiDAR UDP 스트림의 손실을 azimuth 갭으로 잡고 `SO_RCVBUF`·`recvmmsg`·RPS·busy poll이 어느 단계를 고치는지 말할 수 있다
> **왜 필요한가**: 로봇·드론·차량 센서 키트 면접에서 "IMU를 Linux에 붙여 봤나", "GNSS 시각을 시스템 시계에 어떻게 넣나", "LiDAR 패킷이 가끔 빠지면 어디부터 보나"는 거의 항상 나온다. 입사 첫 달에 실제로 하는 일도 이 셋이다
> **Don 기준 난이도**: 센서 레지스터·인터럽트·FIFO·DMA·링버퍼는 이미 안다 / 새로 배울 것은 Linux 쪽 표준 인터페이스(IIO ABI, gpsd JSON, 소켓 수신 경로)와 "어느 층의 카운터를 보면 되나"
> **선행 노트**: I2(IIO 프레임워크 — 이 노트는 그 위에서 "시스템 관점"만 다룬다), H5(I2C 서브시스템), H2(regmap), H8(UART·TTY·GNSS 서브시스템), A9(소켓·UDP 손실), A4(I/O 멀티플렉싱), F8(시간·타이머), P3(데이터시트 → 드라이버 워크플로), Q3(멀티 센서 동기화·PPS·PTP), Q7(데이터 품질 툴)

---

## 0. 큰 그림 — 이게 왜 필요한가

예를 들어 센서 키트라면 Linux SoC(i.MX8, AM62, Jetson Orin) 하나에 IMU, GNSS, LiDAR, 카메라, CAN 모터 컨트롤러가 붙는다. 카메라는 Q1·Q2, CAN은 Q4가 다룬다. 이 노트는 나머지 셋이다. 세 센서는 **커널 인터페이스가 서로 완전히 다르다**는 점이 핵심이다.

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="80" y="22" font-size="13" text-anchor="middle" font-weight="bold">층</text><text x="215" y="22" font-size="13" text-anchor="middle" font-weight="bold">IMU</text><text x="420" y="22" font-size="13" text-anchor="middle" font-weight="bold">GNSS</text><text x="590" y="22" font-size="13" text-anchor="middle" font-weight="bold">LiDAR</text><text x="70" y="62" font-size="12" text-anchor="middle">센서 HW</text>
<text x="70" y="142" font-size="12" text-anchor="middle">커널</text><text x="70" y="227" font-size="12" text-anchor="middle">장치 노드</text><text x="70" y="307" font-size="12" text-anchor="middle">유저 데몬</text><text x="70" y="387" font-size="12" text-anchor="middle">소비자</text><rect x="130" y="38" width="170" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="215" y="56" font-size="12" text-anchor="middle">BMI160 / ICM / LSM6</text><text x="215" y="73" font-size="12" text-anchor="middle">I2C·SPI + INT 핀</text><rect x="320" y="38" width="200" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="420" y="56" font-size="12" text-anchor="middle">u-blox M8/F9 등</text><text x="420" y="73" font-size="12" text-anchor="middle">UART(NMEA/UBX) + PPS 핀</text><rect x="540" y="38" width="130" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="605" y="56" font-size="12" text-anchor="middle">VLP-16 / OS1</text><text x="605" y="73" font-size="12" text-anchor="middle">Ethernet UDP</text><rect x="130" y="110" width="170" height="64" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="215" y="130" font-size="12" text-anchor="middle">i2c/spi core + regmap</text><text x="215" y="147" font-size="12" text-anchor="middle">IIO core: trigger,</text>
<text x="215" y="164" font-size="12" text-anchor="middle">kfifo buffer, ts</text><rect x="320" y="110" width="200" height="64" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="420" y="130" font-size="12" text-anchor="middle">serial core → tty</text><text x="420" y="147" font-size="12" text-anchor="middle">또는 GNSS 서브시스템</text><text x="420" y="164" font-size="12" text-anchor="middle">PPS core (pps-gpio)</text><rect x="540" y="110" width="130" height="64" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="605" y="130" font-size="12" text-anchor="middle">NIC + NAPI</text><text x="605" y="147" font-size="12" text-anchor="middle">IP → UDP</text>
<text x="605" y="164" font-size="12" text-anchor="middle">socket rcvbuf</text><rect x="130" y="205" width="170" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="215" y="223" font-size="12" text-anchor="middle">/dev/iio:device0</text><text x="215" y="240" font-size="12" text-anchor="middle">+ sysfs 속성</text><rect x="320" y="205" width="200" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="420" y="223" font-size="12" text-anchor="middle">/dev/ttyS1 · /dev/gnss0</text><text x="420" y="240" font-size="12" text-anchor="middle">/dev/pps0</text>
<rect x="540" y="205" width="130" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="605" y="223" font-size="12" text-anchor="middle">UDP socket</text><text x="605" y="240" font-size="12" text-anchor="middle">port 2368 등</text><rect x="130" y="285" width="170" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="215" y="303" font-size="12" text-anchor="middle">libiio / 자체 리더</text><text x="215" y="320" font-size="12" text-anchor="middle">IMU 노드</text><rect x="320" y="285" width="200" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="420" y="303" font-size="12" text-anchor="middle">gpsd → JSON :2947</text><text x="420" y="320" font-size="12" text-anchor="middle">chrony (SHM/SOCK + PPS)</text><rect x="540" y="285" width="130" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="605" y="303" font-size="12" text-anchor="middle">driver 노드</text><text x="605" y="320" font-size="12" text-anchor="middle">패킷 → 스캔</text><rect x="130" y="365" width="540" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="400" y="390" font-size="12" text-anchor="middle">센서 퓨전 · SLAM · ROS 2 (Q6) — 모든 샘플에 같은 시계의 타임스탬프가 필요 (Q3)</text>
<line x1="215" y1="82" x2="215" y2="108" stroke="currentColor" marker-end="url(#ah1)"/><line x1="215" y1="174" x2="215" y2="203" stroke="currentColor" marker-end="url(#ah1)"/><line x1="215" y1="249" x2="215" y2="283" stroke="currentColor" marker-end="url(#ah1)"/><line x1="215" y1="329" x2="215" y2="363" stroke="currentColor" marker-end="url(#ah1)"/><line x1="420" y1="82" x2="420" y2="108" stroke="currentColor" marker-end="url(#ah1)"/><line x1="420" y1="174" x2="420" y2="203" stroke="currentColor" marker-end="url(#ah1)"/><line x1="420" y1="249" x2="420" y2="283" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="420" y1="329" x2="420" y2="363" stroke="currentColor" marker-end="url(#ah1)"/><line x1="605" y1="82" x2="605" y2="108" stroke="currentColor" marker-end="url(#ah1)"/><line x1="605" y1="174" x2="605" y2="203" stroke="currentColor" marker-end="url(#ah1)"/><line x1="605" y1="249" x2="605" y2="283" stroke="currentColor" marker-end="url(#ah1)"/><line x1="605" y1="329" x2="605" y2="363" stroke="currentColor" marker-end="url(#ah1)"/><text x="345" y="430" font-size="12" text-anchor="middle">세 센서는 커널 인터페이스가 전부 다르다: IIO 문자 장치 · tty 바이트 스트림 · UDP 소켓</text></svg>
```
그림 1 — 센서 키트의 세 데이터 경로. IMU는 IIO 문자 장치, GNSS는 tty(또는 `/dev/gnss0`) 바이트 스트림을 gpsd가 해석, LiDAR는 UDP 소켓. 맨 아래 소비자는 셋 모두에 "같은 시계로 찍힌 타임스탬프"를 요구한다.

bare-metal로 비유하면 이렇다. Don이 SSD 펌웨어에서 하던 "센서 = 레지스터 + IRQ + DMA 링"은 IMU 쪽에 그대로 있다. 다만 Linux에서는 그 링을 **IIO 코어가 대신 관리**하고(I2), 유저는 `read()`로 꺼낸다. GNSS는 "UART RX 인터럽트 → 링버퍼 → 프레임 파서"에서 **파서를 유저 공간 데몬(gpsd)으로 뺀** 구조다. LiDAR는 "DMA 디스크립터 링 → 프로토콜 스택 → 소켓 큐"로, 손실 지점이 다섯 군데 이상이다.

| | IMU | GNSS | LiDAR |
|---|---|---|---|
| 물리 버스 | I2C(400 kHz–1 MHz) / SPI(수 MHz) + INT 핀 | UART 9600–921600 baud(또는 USB) + PPS 핀 | 100BASE-TX / 1000BASE-T Ethernet |
| 데이터율 | 100 Hz–수 kHz × 12–20 B | 1–20 Hz × 수백 B | VLP-16 기준 약 754 pps × 1206 B ≈ 7 Mbit/s (기억 기반 — 매뉴얼 확인 필요) |
| 커널 인터페이스 | IIO: sysfs + `/dev/iio:deviceN` | tty `/dev/ttyS1` 또는 GNSS `/dev/gnss0`, PPS `/dev/pps0` | UDP 소켓 |
| 파싱은 어디서 | 커널 드라이버(레지스터 → scan) | 유저(gpsd) | 유저(드라이버 노드) |
| 타임스탬프 출처 | IRQ 시각 또는 칩 HW 타임스탬프 | 위성 시각(UTC) + PPS 엣지 | 센서 내부 시계(보통 PTP/PPS로 동기) |
| 대표 고장 | FIFO overflow, 잘못된 scale·mount matrix, ts 지터 | 반쪽 문장, baud 불일치, 시각 오프셋 | 소켓 버퍼 넘침, 패킷 순서·갭 |

### 0.1 이 노트의 실습 환경

모든 실행은 Lima VM `elinux`(aarch64) 안의 QEMU 게스트에서 했다. 커널은 랩의 `Image-dom`(6.18.55-dom), 루트 파일시스템은 `rootfs-dom.ext4`(Ubuntu 24.04 + systemd, libiio-utils·gpsd·gpsd-clients/tools 포함) 위의 qcow2 overlay다. 예제 바이너리와 스크립트는 작은 ext4 이미지(`share.img`)에 담아 두 번째 virtio 디스크로 붙였다.

```sh
# VM: overlay와 공유 디스크 (rootfs를 복사하지 않는다)
qemu-img create -q -f qcow2 -F raw -b /var/lab/out/rootfs-dom.ext4 /var/tmp/Q5/q5.qcow2
mkfs.ext4 -q -d /var/tmp/Q5/share /var/tmp/Q5/share.img 32M
qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 1536M -nographic \
  -kernel /var/lab/out/Image-dom \
  -drive file=/var/tmp/Q5/q5.qcow2,if=virtio,format=qcow2 \
  -drive file=/var/tmp/Q5/share.img,if=virtio,format=raw \
  -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
  -append "console=ttyAMA0 root=/dev/vda rw rootwait systemd.default_device_timeout_sec=900"
# 게스트: mount /dev/vdb /mnt/q5 && bash /mnt/q5/g_all.sh
```

pexpect 스크립트가 로그인 프롬프트를 기다렸다가 `g_all.sh`를 실행하고 출력을 로그로 받는다. 게스트 커널은 `uname -r` = `6.18.55-dom`이다.

> **TCG 주의**: KVM 없이 TCG(명령어 번역)로 돌고, 같은 VM을 19명이 함께 쓰는 중이라(load average 20 이상) 첫 부팅이 14분 걸렸다. 이 노트의 **모든 시간 수치(샘플률, 지터, 패킷 손실 한계)는 실제 HW 값이 아니다.** 보여 주려는 것은 "측정하는 방법"과 "어느 쪽으로 움직이는가"다.

## 1. IMU (1) — 업스트림 IMU 드라이버 세 개를 "시스템 관점"에서 고르기

I2에서 IIO의 객체(`iio_dev`, `iio_chan_spec`), sysfs ABI, kfifo, 트리거, 이벤트를 직접 드라이버를 짜면서 배웠다. P3는 "데이터시트 → 기존 드라이버 찾기 → regmap" 루틴을 다룬다. 여기서는 그 다음 질문을 다룬다: **이미 있는 업스트림 드라이버를 보드에 붙일 때, 그 드라이버가 데이터를 어떤 방식으로 내 주는지가 시스템 설계를 결정한다.**

### 1.1 IMU 하나가 내놓는 것 — 채널과 속성 복습

IMU(Inertial Measurement Unit)는 가속도계(accelerometer) 3축 + 자이로(gyroscope) 3축(+ 때로 자력계, 온도)이다. IIO에서는 이렇게 보인다.

| sysfs 이름 | 뜻 | 예 (bmi160) |
|---|---|---|
| `in_accel_x_raw` | 가속도 X 원시값(LSB) | `-1000` |
| `in_accel_scale` | raw × scale = m/s² | `0.000598` (±2 g) |
| `in_accel_offset` | (raw + offset) × scale (있을 때만) | bmi160엔 없음 |
| `in_anglvel_x_raw`, `in_anglvel_scale` | 자이로, rad/s | `0.001065` (±2000 °/s) |
| `in_accel_sampling_frequency` | ODR(Output Data Rate), Hz | `100` |
| `in_mount_matrix` / `mount_matrix` | 보드에 붙은 방향 → 기체 좌표 회전 행렬 | DT `mount-matrix`에서 |
| `scan_elements/in_timestamp_en` | 버퍼 scan에 s64 타임스탬프 포함 | 버퍼 캡처 때 |
| `buffer/length`, `buffer/watermark` | kfifo 크기, 깨우는 기준 | I2 7절 |

단위는 IIO ABI가 고정한다: 가속도 m/s², 각속도 rad/s, 자기장 gauss, 온도 milli°C. 그래서 **유저는 칩 종류를 몰라도 `raw × scale`만 하면 된다.** 이것이 IIO를 쓰는 가장 큰 이유다(I2 0.1절).

### 1.2 세 드라이버의 데이터 경로 차이

무엇을 확인하나: 랩 소스(`/var/lab/src/linux-6.18.55/drivers/iio/imu/`)에서 세 드라이버가 어떤 버퍼·트리거·타임스탬프 API를 쓰는지 함수 이름 개수로 비교한다.

```sh
cd /var/lab/src/linux-6.18.55/drivers/iio/imu
for d in bmi160 inv_mpu6050 st_lsm6dsx; do printf "%-12s " $d
  for k in devm_iio_triggered_buffer_setup devm_iio_kfifo_buffer_setup devm_iio_trigger_alloc \
           hwfifo_set_watermark inv_sensors_timestamp ts_gain iio_read_mount_matrix; do
    printf "%s=%s " $k $(cat $d/*.c | grep -c $k); done; echo; done
```

```text
bmi160       devm_iio_triggered_buffer_setup=1 devm_iio_kfifo_buffer_setup=0 devm_iio_trigger_alloc=1 hwfifo_set_watermark=0 inv_sensors_timestamp=0 ts_gain=0 iio_read_mount_matrix=1 
inv_mpu6050  devm_iio_triggered_buffer_setup=1 devm_iio_kfifo_buffer_setup=0 devm_iio_trigger_alloc=1 hwfifo_set_watermark=0 inv_sensors_timestamp=10 ts_gain=0 iio_read_mount_matrix=1 
st_lsm6dsx   devm_iio_triggered_buffer_setup=1 devm_iio_kfifo_buffer_setup=1 devm_iio_trigger_alloc=0 hwfifo_set_watermark=3 inv_sensors_timestamp=0 ts_gain=4 iio_read_mount_matrix=1 
```

출력에서 볼 것: 세 드라이버가 세 가지 다른 설계다. 소스를 읽어 정리하면 이렇다.

| | `bmi160` | `inv_mpu6050` | `st_lsm6dsx` |
|---|---|---|---|
| HW FIFO 사용 | **안 함** (`bmi160_core.c` 머리 주석: "TODO: magnetometer, hardware FIFO") | 함 — pollfunc에서 FIFO를 통째로 읽음 | 함 — watermark IRQ, `hwfifo_set_watermark` |
| 트리거 | 자체 DRDY 트리거 `"%s-dev%d"`, `IRQF_NO_THREAD` 하드 IRQ에서 `iio_trigger_generic_data_rdy_poll` | 자체 트리거 | IRQ가 있으면 트리거 없이 kfifo에 직접 push, 없으면 외부 트리거(sw buffer) |
| 샘플당 버스 트랜잭션 | 활성 채널마다 `regmap_bulk_read` 2바이트 | FIFO 블록 읽기 | FIFO 블록 읽기 |
| 타임스탬프 | 트리거 시각(`pf->timestamp`) 1개 = 샘플 1개 | IRQ 시각 + 칩 주기 추정으로 보간(`inv_sensors_timestamp_*`) | 칩 내부 HW 타임스탬프 × `ts_gain` + 기준 |
| 고 ODR(≥1 kHz)에서 | IRQ·I2C 트랜잭션이 샘플마다 → CPU·버스 비용 큼 | 배치라 효율적 | 배치 + 정확한 ts |

시스템 설계로 번역하면: **1 kHz 이상 IMU를 쓸 거면 HW FIFO + watermark를 지원하는 드라이버(칩)를 고른다.** bmi160 업스트림 드라이버를 1.6 kHz로 돌리면 초당 1600번 IRQ + 1600 × (활성 채널 수)번의 I2C 트랜잭션이 생긴다. Don이 bare-metal에서 "DRDY마다 SPI 읽기 vs FIFO watermark DMA"를 고르던 판단과 똑같은데, Linux에서는 **그 선택이 이미 드라이버 코드에 굳어 있다**는 것이 차이다. 드라이버를 고르는 것이 곧 아키텍처를 고르는 것이다.

### 1.3 DT에 IMU 넣기 (가이드 — 실행 안 함)

실제 보드에서는 DT 노드 하나로 끝난다. 바인딩 문서는 `Documentation/devicetree/bindings/iio/imu/bosch,bmi160.yaml`이다.

```dts
&i2c3 {
    imu@68 {
        compatible = "bosch,bmi160";
        reg = <0x68>;
        vdd-supply = <&reg_3v3>;
        vddio-supply = <&reg_1v8>;
        interrupt-parent = <&gpio4>;
        interrupts = <12 IRQ_TYPE_EDGE_RISING>;
        interrupt-names = "INT1";
        mount-matrix = "0", "1", "0",
                       "-1", "0", "0",
                       "0", "0", "1";
    };
};
```

`interrupt-names`로 INT1/INT2 중 어느 핀이 연결됐는지 알려 준다(`bmi160_get_irq`). `mount-matrix`는 IMU가 보드에 90° 돌아 붙었을 때 유저가 기체 좌표로 회전하는 데 쓰는 행렬이다. 커널은 값을 바꾸지 않고 `in_mount_matrix`로 노출만 한다. 회로도에서 핀과 레귤레이터를 추적하는 방법은 P4, 바인딩 검증은 D7.

## 2. IMU (2) — 업스트림 `bmi160` 드라이버를 i2c-stub 위의 가짜 칩에 붙이기

### 2.1 i2c-stub — "레지스터만 있는 칩"

`i2c-stub`은 커널이 제공하는 가짜 SMBus 어댑터다. `chip_addr=0x68,0x69`로 주소를 주면 그 주소에 **256바이트 레지스터 배열만 있는 칩**이 생긴다. 쓰면 저장되고 읽으면 돌려준다. 그 이상의 행동(읽으면 지워지는 상태 레지스터, soft reset, 측정값 갱신)은 없다. 행동까지 흉내 내는 가짜 칩은 H2·H5·I2에서 직접 만들었다. 여기서는 반대로 **업스트림 드라이버를 그대로 쓰는 것**이 목적이라 i2c-stub으로 충분하다. P3는 같은 i2c-stub으로 "데이터시트 → regmap" 루틴을 다룬다.

bring-up 관점에서 이 실험은 "보드가 오기 전에 드라이버·DT·유저 공간 스택을 미리 맞춰 보기"다. Don이 실리콘 전에 FPGA/모델로 펌웨어를 먼저 돌리던 것과 같은 자리다.

### 2.2 레지스터를 미리 채우는 도구 — `I2C_SMBUS` ioctl

무엇을 확인하나: `/dev/i2c-N`에 SMBus byte-data 트랜잭션을 직접 보내 레지스터를 쓰고 다시 읽는다(`i2cset -f -y`가 하는 일). i2c-stub은 순수 I2C 전송(`I2C_FUNC_I2C`)을 지원하지 않으므로 `write()`가 아니라 `I2C_SMBUS` ioctl이어야 한다.

```c
/* q5_regset.c: poke byte registers of an SMBus-only chip (e.g. i2c-stub)
 * through /dev/i2c-N with the I2C_SMBUS ioctl (what i2cset -f -y does).
 * I2C_SLAVE_FORCE: works even when a kernel driver owns the address.
 * usage: q5_regset <bus> <addr> <reg>[=<val>] ...   (no =val: read only) */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

static int smbus(int fd, char rw, unsigned char reg, union i2c_smbus_data *d)
{
    struct i2c_smbus_ioctl_data a = { .read_write = rw, .command = reg,
                                      .size = I2C_SMBUS_BYTE_DATA, .data = d };
    return ioctl(fd, I2C_SMBUS, &a);
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s bus addr reg[=val]...\n", argv[0]); exit(1); }
    char path[32];
    snprintf(path, sizeof path, "/dev/i2c-%s", argv[1]);
    int fd = open(path, O_RDWR);
    if (fd < 0) { perror(path); exit(1); }
    if (ioctl(fd, I2C_SLAVE_FORCE, strtol(argv[2], NULL, 0)) < 0) { perror("I2C_SLAVE_FORCE"); exit(1); }
    for (int i = 3; i < argc; i++) {
        unsigned reg, val;
        int k = sscanf(argv[i], "%x=%x", &reg, &val);  /* "reg" = read only */
        if (k < 1) { fprintf(stderr, "bad %s\n", argv[i]); exit(1); }
        union i2c_smbus_data d = { .byte = val };
        if (k == 2 && smbus(fd, I2C_SMBUS_WRITE, reg, &d) < 0) { perror("write"); exit(1); }
        if (smbus(fd, I2C_SMBUS_READ, reg, &d) < 0) { perror("read"); exit(1); }
        if (k == 2) printf("0x%02x <- 0x%02x (readback 0x%02x)\n", reg, val, d.byte);
        else        printf("0x%02x = 0x%02x\n", reg, d.byte);
    }
    close(fd);
    return 0;
}
```

bmi160 데이터시트(그리고 `bmi160_core.c`의 `#define`)를 보고 채울 값을 정했다.

| 레지스터 | 값 | 뜻 |
|---|---|---|
| `0x00` CHIP_ID | `0xD1` | `BMI160_CHIP_ID_VAL` — probe가 확인한다 |
| `0x41` ACC_RANGE | `0x03` | ±2 g |
| `0x43` GYR_RANGE | `0x00` | ±2000 °/s |
| `0x40`/`0x42` ACC/GYR_CONF | `0x28` | ODR 100 Hz (하위 4비트 `0x8`) |
| `0x0C..0x0D` GYR_X | `0x00A4` = 164 | 164 / 16.4 LSB/(°/s) = 10 °/s |
| `0x14..0x15` ACC_Y | `0xFC18` = −1000 | |
| `0x16..0x17` ACC_Z | `0x4000` = 16384 | 1 g (±2 g에서 16384 LSB/g) |

### 2.3 예제 — probe하고 단위 복원

무엇을 확인하나: 레지스터를 채운 뒤 `new_device`로 client를 만들면 업스트림 `bmi160_i2c`가 probe되고, sysfs에서 IIO 표준 단위로 읽히는지.

```sh
# g_bmi.sh (앞부분) — 게스트
modprobe i2c-stub chip_addr=0x68,0x69
BUS=$(for b in /sys/bus/i2c/devices/i2c-*; do grep -q "SMBus stub" $b/name && echo ${b##*-}; done)
$B/regset $BUS 0x68 00=d1 41=03 43=00 40=28 42=28 0c=a4 0d=00 16=00 17=40 15=fc 14=18
modprobe bmi160_i2c
echo bmi160 0x68 > /sys/bus/i2c/devices/i2c-$BUS/new_device
sleep 1; dmesg | grep -iE "bmi160|i2c-stub|stub" | tail -5
D=$(grep -l bmi160 /sys/bus/iio/devices/iio:device*/name | head -1 | xargs dirname)
for f in in_accel_x_raw in_accel_y_raw in_accel_z_raw in_accel_scale in_accel_scale_available \
         in_accel_sampling_frequency in_anglvel_x_raw in_anglvel_scale in_mount_matrix; do
  printf "%-28s %s\n" $f "$(cat $D/$f 2>&1)"; done
awk -v z=$(cat $D/in_accel_z_raw) -v y=$(cat $D/in_accel_y_raw) -v s=$(cat $D/in_accel_scale) \
    -v g=$(cat $D/in_anglvel_x_raw) -v gs=$(cat $D/in_anglvel_scale) \
  'BEGIN{printf "accel_y = %.4f m/s^2, accel_z = %.4f m/s^2, gyro_x = %.4f rad/s = %.2f deg/s\n", y*s, z*s, g*gs, g*gs*57.29578}'
echo 0.002394 > $D/in_accel_scale; cat $D/in_accel_scale      # ±8 g
$B/regset $BUS 0x68 00 40 41 42 43 7e                           # read back
```

```text
### stub bus = i2c-0 (SMBus stub driver)
### preset registers: CHIP_ID, ranges, ODR, gyro X, accel Z
0x00 <- 0xd1 (readback 0xd1)
0x41 <- 0x03 (readback 0x03)
0x43 <- 0x00 (readback 0x00)
0x40 <- 0x28 (readback 0x28)
0x42 <- 0x28 (readback 0x28)
0x0c <- 0xa4 (readback 0xa4)
0x0d <- 0x00 (readback 0x00)
0x16 <- 0x00 (readback 0x00)
0x17 <- 0x40 (readback 0x40)
0x15 <- 0xfc (readback 0xfc)
0x14 <- 0x18 (readback 0x18)
[  110.144059] i2c-stub: Virtual chip at 0x69
[  111.376102] bmi160_i2c 0-0068: supply vdd not found, using dummy regulator
[  111.383394] bmi160_i2c 0-0068: supply vddio not found, using dummy regulator
[  111.384584] bmi160_i2c 0-0068: mounting matrix not found: using identity...
[  111.482117] i2c i2c-0: new_device: Instantiated device bmi160 at 0x68
### /sys/bus/iio/devices/iio:device1
in_accel_x_raw               0
in_accel_y_raw               -1000
in_accel_z_raw               16384
in_accel_scale               0.000598
in_accel_scale_available     0.000598 0.001197 0.002394 0.004788
in_accel_sampling_frequency  100.000000
in_anglvel_x_raw             164
in_anglvel_scale             0.001065
in_mount_matrix              1, 0, 0; 0, 1, 0; 0, 0, 1
### physical units
accel_y = -0.5980 m/s^2, accel_z = 9.7976 m/s^2, gyro_x = 0.1747 rad/s = 10.01 deg/s
### change range to +-8g: write scale
0.002394
### registers as the driver left them (i2c-stub just stores writes)
0x00 = 0xd1
0x40 = 0x28
0x41 = 0x08
0x42 = 0x28
0x43 = 0x00
0x7e = 0x15
```

출력에서 볼 것:

- probe 로그 세 줄이 **실제 보드 bring-up에서도 똑같이 나오는 경고**다. `supply vdd not found, using dummy regulator`는 DT에 `vdd-supply`가 없다는 뜻이다. 실제 보드에서 이 줄이 보이면 레귤레이터를 드라이버가 못 켜는 것이니 "가끔 IMU가 안 잡힌다"의 후보가 된다. `mounting matrix not found: using identity`는 DT에 `mount-matrix`가 없다는 뜻이다.
- 16384 × 0.000598 = 9.7976 m/s²(1 g), 164 × 0.001065 = 0.1747 rad/s = 10 °/s. 유저는 칩을 몰라도 `raw × scale`로 끝난다.
- scale을 `0.002394`(±8 g)로 쓰자 드라이버가 `0x41`에 `0x08`을 썼다(bmi160의 ±8 g 코드). `0x7e = 0x15`는 probe 마지막에 쓴 명령 레지스터 값(자이로 normal mode)이다. 드라이버가 **어떤 순서로 무엇을 썼는지**를 레지스터로 거꾸로 확인할 수 있다.
- 함정: 실제 칩이라면 범위를 바꾼 순간 raw 값이 1/4로 줄어야 한다. i2c-stub은 그런 행동이 없어서 raw가 그대로 16384다. **stub으로는 "드라이버가 맞는 레지스터에 맞는 값을 쓰는가"까지만 검증된다.**
- 함정 2: 드라이버가 0x68을 잡고 있으면 일반 `I2C_SLAVE`는 `EBUSY`다(첫 실행에서 실제로 `I2C_SLAVE: Device or resource busy`가 났다). 디버그용 읽기는 `I2C_SLAVE_FORCE`(= `i2cget -f`)로 한다. 운영 중인 장치에 강제 접근은 드라이버 상태와 엇갈릴 수 있으니 디버그에서만 쓴다.

같은 stub의 0x69에 `inv_mpu6050`도 붙여 봤다(WHO_AM_I `0x75 = 0x68`, ACCEL_ZOUT `0x3F/0x40` big-endian에 `0x4000`).

```text
[  135.931534] inv-mpu6050-i2c 0-0069: mounting matrix not found: using identity...
[  135.943237] inv-mpu6050-i2c 0-0069: supply vdd not found, using dummy regulator
[  135.944943] inv-mpu6050-i2c 0-0069: supply vddio not found, using dummy regulator
[  136.434726] i2c i2c-0: new_device: Instantiated device mpu6050 at 0x69
/sys/bus/iio/devices/iio:device2: mpu6050
in_accel_z_raw       16384
in_accel_scale       0.000598
sampling_frequency   50
```

출력에서 볼 것: 칩도, 바이트 순서(bmi160은 LE, MPU-6050은 BE)도, 속성 이름(`in_accel_sampling_frequency` vs `sampling_frequency`)도 다른데 **유저가 보는 단위는 같다**. 다만 속성 이름이 드라이버마다 조금씩 다르므로(`IIO_SHARED_BY_TYPE` vs `IIO_SHARED_BY_ALL`) 유저 코드는 이름을 하드코딩하지 말고 glob으로 찾거나 libiio를 쓴다.

## 3. IMU (3) — 버퍼 캡처와 "설정값 vs 실측" 

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="20" y="24" font-size="13" font-weight="bold">센서 칩</text><rect x="20" y="36" width="150" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="95" y="56" font-size="12" text-anchor="middle">MEMS + ADC</text><text x="95" y="74" font-size="12" text-anchor="middle">ODR 클럭(칩 내부)</text><rect x="210" y="36" width="190" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="305" y="56" font-size="12" text-anchor="middle">HW FIFO (예: 1–4 KB)</text><text x="305" y="74" font-size="12" text-anchor="middle">watermark W개 차면</text><rect x="440" y="36" width="110" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="495" y="56" font-size="12" text-anchor="middle">INT1 핀</text><text x="495" y="74" font-size="12" text-anchor="middle">→ GPIO IRQ</text><line x1="170" y1="61" x2="208" y2="61" stroke="currentColor" marker-end="url(#ah2)"/><line x1="400" y1="61" x2="438" y2="61" stroke="currentColor" marker-end="url(#ah2)"/><text x="20" y="124" font-size="13" font-weight="bold">커널</text>
<rect x="440" y="136" width="220" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="550" y="156" font-size="12" text-anchor="middle">hardirq: 타임스탬프 t_irq</text><text x="550" y="174" font-size="12" text-anchor="middle">iio_pollfunc_store_time</text><rect x="210" y="136" width="190" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="305" y="156" font-size="12" text-anchor="middle">IRQ 스레드: FIFO W개</text><text x="305" y="174" font-size="12" text-anchor="middle">regmap_bulk_read</text><rect x="20" y="136" width="150" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="95" y="156" font-size="12" text-anchor="middle">샘플마다 ts 계산</text><text x="95" y="174" font-size="12" text-anchor="middle">push_to_buffers</text><line x1="495" y1="86" x2="525" y2="134" stroke="currentColor" marker-end="url(#ah2)"/><line x1="440" y1="161" x2="402" y2="161" stroke="currentColor" marker-end="url(#ah2)"/><line x1="210" y1="161" x2="172" y2="161" stroke="currentColor" marker-end="url(#ah2)"/><rect x="20" y="216" width="380" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="210" y="236" font-size="12" text-anchor="middle">IIO kfifo: buffer/length 개의 scan (+ s64 ts)</text>
<text x="210" y="254" font-size="12" text-anchor="middle">buffer/watermark 개 모이면 poll 깨움</text><line x1="95" y1="186" x2="95" y2="214" stroke="currentColor" marker-end="url(#ah2)"/><text x="20" y="304" font-size="13" font-weight="bold">유저</text><rect x="20" y="316" width="380" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="210" y="336" font-size="12" text-anchor="middle">poll() + read(/dev/iio:deviceN) → scan 해석</text><text x="210" y="354" font-size="12" text-anchor="middle">(libiio iio_readdev, ROS IMU 노드, 자체 데몬)</text><line x1="210" y1="266" x2="210" y2="314" stroke="currentColor" marker-end="url(#ah2)"/>
<rect x="440" y="216" width="220" height="150" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="550" y="238" font-size="12" text-anchor="middle" font-weight="bold">잃는 곳 · 틀리는 곳</text><text x="452" y="262" font-size="12">1. IRQ 스레드가 늦으면</text><text x="452" y="280" font-size="12">   HW FIFO overflow</text><text x="452" y="304" font-size="12">2. 유저가 늦으면</text><text x="452" y="322" font-size="12">   kfifo 가득 → 새 샘플 버림</text><text x="452" y="346" font-size="12">3. t_irq ≠ 샘플 시각</text><text x="452" y="362" font-size="12">   (지연 + 배치 W개)</text>
<text x="340" y="392" font-size="12" text-anchor="middle">bmi160 업스트림 드라이버는 HW FIFO를 안 쓴다 — DRDY마다 1샘플 (st_lsm6dsx·inv_mpu6050은 FIFO)</text></svg>
```
그림 2 — IMU 데이터 경로. 센서 FIFO → INT 핀 → 하드 IRQ에서 타임스탬프 → IRQ 스레드가 FIFO를 읽어 샘플마다 ts를 붙여 kfifo에 push → 유저가 `poll()`+`read()`. 빨간 상자는 샘플을 잃거나 시각이 틀리는 지점이다.

### 3.1 libiio 도구 — `iio_info`, `iio_attr`, `iio_readdev`

I2에서는 sysfs와 커널 `tools/iio`로 다뤘다. 실제 제품에서는 **libiio**(Analog Devices가 시작한 유저 라이브러리, Ubuntu `libiio-utils`)를 많이 쓴다. 같은 API로 로컬 sysfs, 네트워크(`iiod`), USB 장치를 다룬다.

무엇을 확인하나: configfs로 만든 `iio_dummy`를 libiio 도구로 들여다본다.

```sh
modprobe iio_dummy; modprobe iio-trig-hrtimer
mount -t configfs none /sys/kernel/config 2>/dev/null
mkdir /sys/kernel/config/iio/devices/dummy/q5_dummy
mkdir /sys/kernel/config/iio/triggers/hrtimer/q5_t
iio_info 2>&1 | grep -E "IIO context has|iio:device|trigger[0-9]|voltage0:|timestamp:" | head -20
iio_attr -c q5_dummy voltage0
```

```text
IIO context has 2 attributes:
IIO context has 3 devices:
	iio:device0: q5_dummy (buffer capable)
			voltage0:  (input, index: 0, format: le:u13/16>>0)
			timestamp:  (input, index: 4, format: le:S64/64>>0)
			voltage0:  (output)
	trigger0: q5_t
dev 'q5_dummy', channel 'voltage0' (input), attr 'offset', value '7'
dev 'q5_dummy', channel 'voltage0' (input), attr 'raw', value '73'
dev 'q5_dummy', channel 'voltage0' (input), attr 'scale', value '0.001333'
dev 'q5_dummy', channel 'voltage0' (output), attr 'raw', value '0'
```

출력에서 볼 것: `le:u13/16>>0`는 "little endian, unsigned 13비트, 16비트 저장, 시프트 0" — `scan_elements/in_voltage0_type`과 같은 정보다. 버퍼 scan 배치를 손으로 계산하는 규칙은 I2 4.2절. 이 장치는 voltage0·voltage1-2·voltage3-4·accel_x(각 2 B) + 정렬 패딩 없이 8 B 다음 s64 timestamp → **scan 16 B, ts 오프셋 8**이다.

### 3.2 측정 도구 — scan 스트림에서 샘플률·지터·갭

무엇을 확인하나: 버퍼에서 읽은 raw scan 스트림(stdin)을 받아 타임스탬프만 꺼내 실제 샘플률, 간격의 평균·표준편차·최소·최대, 1.5 주기보다 긴 갭과 빠진 샘플 수를 센다. Q7(데이터 품질 툴)의 가장 작은 형태다.

```c
/* q5_tsstat.c: read raw IIO scans from stdin (e.g. iio_readdev output),
 * pull the s64 timestamp at a given offset, report rate / jitter / gaps.
 * usage: q5_tsstat <scan_bytes> <ts_offset> <expected_hz> [skip] */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s scan off hz [skip]\n", argv[0]); exit(1); }
    size_t scan = strtoul(argv[1], NULL, 0), off = strtoul(argv[2], NULL, 0);
    double hz = atof(argv[3]), T = 1e9 / hz;
    long skip = argc > 4 ? atol(argv[4]) : 0;
    unsigned char buf[256];
    if (scan > sizeof buf || off + 8 > scan) { fprintf(stderr, "bad layout\n"); exit(1); }
    long n = 0, gaps = 0, lost = 0;
    int64_t first = 0, prev = 0;
    double s = 0, s2 = 0, mn = 1e18, mx = 0;
    while (fread(buf, scan, 1, stdin) == 1) {
        int64_t ts;
        memcpy(&ts, buf + off, 8);
        if (skip > 0) { skip--; continue; }
        if (n == 0) first = ts;
        else {
            double dt = (double)(ts - prev);
            s += dt; s2 += dt * dt;
            if (dt < mn) mn = dt;
            if (dt > mx) mx = dt;
            if (dt > 1.5 * T) { gaps++; lost += (long)(dt / T + 0.5) - 1; }
        }
        prev = ts; n++;
    }
    if (n < 3) { fprintf(stderr, "too few samples (%ld)\n", n); exit(1); }
    double m = s / (n - 1), sd = sqrt(s2 / (n - 1) - m * m);
    printf("samples=%ld span=%.3fs rate=%.1fHz (set %.0f) dt_mean=%.1fus "
           "dt_std=%.1fus dt_min=%.1fus dt_max=%.1fus gaps=%ld est_lost=%ld\n",
           n, (prev - first) / 1e9, (n - 1) / ((prev - first) / 1e9), hz,
           m / 1e3, sd / 1e3, mn / 1e3, mx / 1e3, gaps, lost);
    return 0;
}
```

### 3.3 예제 — hrtimer 트리거를 올려 가며 실제 샘플률 재기

무엇을 확인하나: 같은 장치에서 트리거 주파수를 100 → 4000 Hz로 올리면서 5초씩 캡처하고, 타임스탬프로 잰 실제 샘플률을 비교한다.

```sh
cd /sys/bus/iio/devices/iio:device0
for f in scan_elements/*_en; do echo 1 > $f; done
echo q5_t > trigger/current_trigger; echo 1024 > buffer/length
for hz in 100 500 1000 2000 4000; do
  echo $hz > $T/sampling_frequency          # $T = q5_t 트리거의 sysfs 디렉터리
  echo 1 > buffer/enable
  timeout 5 cat /dev/iio:device0 > /tmp/d.bin
  echo 0 > buffer/enable
  $B/tsstat 16 8 $hz 10 < /tmp/d.bin
done
cat /proc/loadavg
# (위는 핵심만 — 실제 스크립트에는 `### ...` 머리줄을 찍는 echo가 더 있다)
```

```text
### manual capture: hrtimer set=100.000000 Hz, cat /dev/iio:device0 for 5 s
samples=486 span=5.016s rate=96.7Hz (set 100) dt_mean=10342.5us dt_std=3005.0us dt_min=306.7us dt_max=33584.8us gaps=21 est_lost=24
### manual capture: hrtimer set=500.000000 Hz, cat /dev/iio:device0 for 5 s
samples=1802 span=5.211s rate=345.6Hz (set 500) dt_mean=2893.5us dt_std=2588.6us dt_min=133.3us dt_max=55583.2us gaps=562 est_lost=942
### manual capture: hrtimer set=1000.000000 Hz, cat /dev/iio:device0 for 5 s
samples=1687 span=5.167s rate=326.3Hz (set 1000) dt_mean=3064.6us dt_std=3755.3us dt_min=109.7us dt_max=50415.1us gaps=934 est_lost=3586
### manual capture: hrtimer set=2000.000000 Hz, cat /dev/iio:device0 for 5 s
samples=1355 span=5.281s rate=256.4Hz (set 2000) dt_mean=3900.0us dt_std=4728.2us dt_min=31.2us dt_max=52861.3us gaps=1100 est_lost=9236
### manual capture: hrtimer set=4000.000000 Hz, cat /dev/iio:device0 for 5 s
samples=1932 span=5.363s rate=360.1Hz (set 4000) dt_mean=2777.3us dt_std=4219.9us dt_min=51.6us dt_max=59890.9us gaps=1525 est_lost=19558
5.38 2.10 0.77 2/104 424
### first 2 scans (hex)
0000000 07 00 df ff fe ff 58 01 e0 6d 1a 96 25 ad df 18
0000016 07 00 df ff fe ff 58 01 e0 ec 24 96 25 ad df 18
```

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg"><text x="340" y="24" font-size="14" text-anchor="middle">iio_dummy + hrtimer 트리거: 설정 주파수 vs 타임스탬프로 잰 실제 샘플률 (TCG)</text><line x1="90" y1="260.0" x2="640" y2="260.0" stroke="currentColor" stroke-opacity="0.15"/><text x="82" y="264.0" font-size="12" text-anchor="end">0 Hz</text><line x1="90" y1="207.5" x2="640" y2="207.5" stroke="currentColor" stroke-opacity="0.15"/><text x="82" y="211.5" font-size="12" text-anchor="end">1000 Hz</text><line x1="90" y1="155.0" x2="640" y2="155.0" stroke="currentColor" stroke-opacity="0.15"/><text x="82" y="159.0" font-size="12" text-anchor="end">2000 Hz</text>
<line x1="90" y1="102.5" x2="640" y2="102.5" stroke="currentColor" stroke-opacity="0.15"/><text x="82" y="106.5" font-size="12" text-anchor="end">3000 Hz</text><line x1="90" y1="50.0" x2="640" y2="50.0" stroke="currentColor" stroke-opacity="0.15"/><text x="82" y="54.0" font-size="12" text-anchor="end">4000 Hz</text><rect x="110" y="254.8" width="40" height="5.2" fill="#888" fill-opacity="0.5" stroke="#888"/><rect x="154" y="254.9" width="40" height="5.1" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><text x="174" y="249.9" font-size="12" text-anchor="middle">97</text><text x="152" y="278" font-size="12" text-anchor="middle">100 Hz</text>
<rect x="218" y="233.8" width="40" height="26.2" fill="#888" fill-opacity="0.5" stroke="#888"/><rect x="262" y="241.9" width="40" height="18.1" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><text x="282" y="236.9" font-size="12" text-anchor="middle">346</text><text x="260" y="278" font-size="12" text-anchor="middle">500 Hz</text><rect x="326" y="207.5" width="40" height="52.5" fill="#888" fill-opacity="0.5" stroke="#888"/><rect x="370" y="242.9" width="40" height="17.1" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><text x="390" y="237.9" font-size="12" text-anchor="middle">326</text><text x="368" y="278" font-size="12" text-anchor="middle">1000 Hz</text>
<rect x="434" y="155.0" width="40" height="105.0" fill="#888" fill-opacity="0.5" stroke="#888"/><rect x="478" y="246.5" width="40" height="13.5" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><text x="498" y="241.5" font-size="12" text-anchor="middle">256</text><text x="476" y="278" font-size="12" text-anchor="middle">2000 Hz</text><rect x="542" y="50.0" width="40" height="210.0" fill="#888" fill-opacity="0.5" stroke="#888"/><rect x="586" y="241.1" width="40" height="18.9" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><text x="606" y="236.1" font-size="12" text-anchor="middle">360</text><text x="584" y="278" font-size="12" text-anchor="middle">4000 Hz</text>
<line x1="90" y1="260" x2="640" y2="260" stroke="currentColor"/><line x1="90" y1="50" x2="90" y2="260" stroke="currentColor"/><rect x="150" y="290" width="12" height="12" fill="#888" fill-opacity="0.5" stroke="#888"/><text x="168" y="300" font-size="12">설정값 (sampling_frequency)</text><rect x="390" y="290" width="12" height="12" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><text x="408" y="300" font-size="12">실측 (scan 타임스탬프)</text></svg>
```
그림 3 — 설정한 트리거 주파수(회색)와 타임스탬프로 잰 실제 샘플률(초록). 이 TCG 게스트(공유 VM 부하 높음)에서는 약 350 Hz에서 포화됐다. 실제 HW 수치가 아니다.

출력에서 볼 것:

- **설정값과 실측은 다르다.** 100 Hz는 거의 맞지만(96.7) 500 Hz부터는 약 350 Hz에서 멈췄다. 게스트 load average 5.38(2 vCPU)이 증거다 — 트리거 처리 자체가 CPU를 다 먹었다. 실제 SoC에서도 같은 현상이 "1 kHz로 설정했는데 ROS 토픽이 800 Hz"처럼 나온다. **설정값을 믿지 말고 scan 타임스탬프로 잰다.**
- `dt_min`이 31–300 µs로 매우 짧다. 늦게 깨어난 hrtimer가 밀린 만큼 빨리 연달아 쏘기 때문이다. 평균 샘플률만 보면 이 지터가 안 보인다 — 반드시 분포(std, min, max, 갭)를 같이 본다.
- 첫 scan의 마지막 8바이트 `e0 6d 1a 96 25 ad df 18`는 little-endian s64 = 약 1.79 × 10^18 ns = 2026년의 **realtime** 시각이다. `current_timestamp_clock` 기본값이 `realtime`이라서다. NTP/chrony가 시계를 계단으로 움직이면 IMU 시각도 튄다. 제품에서는 `monotonic`으로 바꾼다(I2 10.1절, Q3).

### 3.4 함정 — `iio_readdev -t`는 트리거 주파수를 100 Hz로 되돌린다

처음에는 `iio_readdev -t q5_t -s N`으로 캡처했는데, 설정이 500·1000 Hz여도 결과가 늘 92–98 Hz였다. 의심이 가서 앞뒤로 트리거 값을 찍어 봤다.

```sh
echo 300 > $T/sampling_frequency; echo "### before iio_readdev: $(cat $T/sampling_frequency)"
timeout 60 iio_readdev -t q5_t -b 64 -s 600 q5_dummy voltage0 timestamp > /tmp/d3.bin
echo "### after  iio_readdev: $(cat $T/sampling_frequency)"
$B/tsstat 16 8 300 10 < /tmp/d3.bin
iio_readdev -V 2>&1 | head -2
```

```text
### before iio_readdev: 300.000000
WARNING: High-speed mode not enabled
### after  iio_readdev: 100.000000
samples=590 span=5.959s rate=98.8Hz (set 300) dt_mean=10117.8us dt_std=2405.1us dt_min=1533.7us dt_max=37297.8us gaps=580 est_lost=1199
iio_readdev version: 0.25 (git tag:v0.25)
Libiio version: 0.25 (git tag: v0.25) backends: local xml ip usb
```

출력에서 볼 것: libiio 0.25의 `iio_readdev`는 `-t`로 트리거를 지정하면 그 트리거의 `sampling_frequency`를 100으로 써 버린다(실행 전 300 → 실행 후 100). 측정 도구가 측정 대상을 바꾼 것이다. 도구를 믿기 전에 **"도구가 무엇을 건드리는가"**를 한 번은 확인한다. 이 노트의 나머지 캡처는 sysfs + `cat /dev/iio:deviceN`으로 했다. (`voltage0 timestamp` 두 채널만 골랐는데도 tsstat에 16 B를 줬다 — u16 + 패딩 6 B + s64 = 16 B라 우연히 맞는다.)

### 3.5 예제 — 업스트림 bmi160의 버퍼 캡처 (hrtimer 트리거)

이 가짜 칩에는 INT 핀(DT `interrupts`)이 없어서 bmi160 드라이버가 자체 DRDY 트리거를 만들지 않았다(`bmi160_core_probe`의 `irq > 0` 분기를 건너뛰고 `Not setting up IRQ trigger`를 dev_info로 찍는 경로). 그래서 hrtimer 트리거를 붙였다. 트리거 처리기 `bmi160_trigger_handler`는 활성 채널마다 `regmap_bulk_read` 2바이트 → I2C(여기서는 stub) 트랜잭션이 샘플당 3번이다.

```sh
cd $D; for c in accel_x accel_y accel_z timestamp; do echo 1 > scan_elements/in_${c}_en; done
echo q5_bt > trigger/current_trigger; echo 1024 > buffer/length
for hz in 100 400 1000; do
  echo $hz > $T/sampling_frequency
  echo 1 > buffer/enable; timeout 5 cat /dev/iio:device1 > /tmp/b.bin; echo 0 > buffer/enable
  $B/tsstat 16 8 $hz 10 < /tmp/b.bin
done
od -A d -t d2 -N 8 /tmp/b.bin; od -A d -t d8 -j 8 -N 8 /tmp/b.bin
```

```text
### bmi160 accel xyz + ts @ hrtimer 100 Hz
samples=501 span=5.060s rate=98.8Hz (set 100) dt_mean=10120.0us dt_std=1871.8us dt_min=2701.7us dt_max=24612.8us gaps=9 est_lost=9
### bmi160 accel xyz + ts @ hrtimer 400 Hz
samples=1716 span=5.107s rate=335.8Hz (set 400) dt_mean=2978.1us dt_std=1481.7us dt_min=335.1us dt_max=19073.9us gaps=286 est_lost=370
### bmi160 accel xyz + ts @ hrtimer 1000 Hz
samples=2144 span=5.122s rate=418.4Hz (set 1000) dt_mean=2390.0us dt_std=3157.6us dt_min=79.1us dt_max=121755.4us gaps=1383 est_lost=3012
### first scan: x y z (s16) + pad + ts (s64)
0000000      0  -1000  16384      0
0000008
0000008  1792342594388376624
0000016
```

출력에서 볼 것: scan은 x, y, z(s16 × 3) + 패딩 2 B + s64 타임스탬프 = 16 B다(I2 4.2절의 정렬 규칙). 값은 미리 채운 0, −1000, 16384 그대로. 1 kHz 설정에서 418 Hz, 최대 갭 122 ms. 그리고 하나 더: **bmi160의 ODR 레지스터는 100 Hz인데 hrtimer는 1 kHz로 읽었다.** 실제 칩이라면 같은 샘플을 10번씩 다시 읽은 것이다(중복 샘플). 외부 트리거를 쓸 때는 트리거 주파수 ≤ ODR로 맞추거나, 칩의 DRDY 트리거를 쓴다. I2 6.4절의 "STATUS로 새 데이터 확인"이 같은 문제의 방어책이다.

## 4. IMU (4) — FIFO 배치에 타임스탬프 붙이기

### 4.1 문제 — IRQ는 W개에 한 번인데 샘플마다 시각이 필요하다

HW FIFO + watermark를 쓰면 IRQ가 W개 샘플마다 한 번 온다. 커널이 아는 시각은 **"IRQ가 들어온 순간" 하나뿐**이다. 그런데 센서 퓨전(EKF, VIO)은 샘플마다 정확한 시각을 원한다. 1 kHz IMU에서 1 ms 틀리면, 10 rad/s로 도는 드론에서 자세 오차 0.01 rad(약 0.6°)다.

오차의 출처는 셋이다.

| 출처 | 크기 | 성질 |
|---|---|---|
| IRQ 지연 (핀 → 하드 IRQ 진입) | 수 µs–수백 µs | 랜덤 지터 + 평균 바이어스 |
| 배치 안의 위치 | 0–(W−1) × T | 결정적 — 계산으로 지울 수 있다 |
| 칩 내부 시계 오차 | MEMS RC 발진기는 % 단위로 틀릴 수 있다 (칩마다 다름 — 데이터시트 확인 필요) | 느린 드리프트 — 추정해야 한다 |

Don의 SSD 경험으로 말하면, "타임스탬프 없는 이벤트 로그를 나중에 시각 순서로 정렬해야 할 때" 쓰던 보간과 같은 문제다.

### 4.2 예제 — 네 가지 방법 비교 (시뮬레이션)

무엇을 확인하나: 진짜 시각을 아는 시뮬레이션에서 W=32, 1 kHz, 칩 시계 1% 빠름, IRQ 지연 20–220 µs 균등 분포일 때, 샘플별 타임스탬프를 네 방법으로 만들고 오차를 잰다.

```c
/* q5_fifo_ts.c: a sensor FIFO delivers samples in batches of W; we only get
 * one IRQ timestamp per batch (taken when the watermark IRQ fires, with latency
 * jitter). Compare ways of assigning per-sample timestamps against truth. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define N   32000       /* samples (32 s) */
#define W   32          /* FIFO watermark */
#define T   1000.0      /* true ODR period in us (1 kHz)... */
#define PPM 10000.0     /* ...but sensor clock is 1 % fast */

static double urand(void) { return rand() / (double)RAND_MAX; }

int main(void)
{
    static double truth[N], a[N], b[N], c[N], d[N];
    double Tr = T / (1.0 + PPM * 1e-6);        /* real period of sensor clock */
    for (int i = 0; i < N; i++) truth[i] = 5000.0 + i * Tr;
    double prev_irq = -1, est_T = T, anc = -1;  /* est_T starts at nominal */
    for (int k = 0; k < N / W; k++) {
        int last = k * W + W - 1;
        /* IRQ timestamp = time last sample landed + 20..220 us latency */
        double irq = truth[last] + 20 + 200 * urand();
        if (prev_irq > 0)                       /* measured period, smoothed */
            est_T = 0.9 * est_T + 0.1 * (irq - prev_irq) / W;
        /* D: anchor = predicted (prev anchor + W*est_T) pulled 10% toward irq */
        anc = anc < 0 ? irq : 0.9 * (anc + W * est_T) + 0.1 * irq;
        for (int j = 0; j < W; j++) {
            int i = k * W + j;
            a[i] = irq;                               /* A: all = IRQ ts      */
            b[i] = irq - (W - 1 - j) * T;             /* B: back-fill nominal */
            c[i] = irq - (W - 1 - j) * est_T;         /* C: back-fill measured */
            d[i] = anc - (W - 1 - j) * est_T;         /* D: C + smoothed anchor */
        }
        prev_irq = irq;
    }
    double *m[4] = { a, b, c, d };
    const char *name[4] = { "A all=irq_ts", "B back-fill T_nominal", "C back-fill T_measured", "D C + smoothed anchor" };
    for (int v = 0; v < 4; v++) {
        double s = 0, s2 = 0, mx = 0, d0 = 0, d2 = 0;
        for (int i = W * 50; i < N; i++) {     /* skip warm-up batches */
            double e = m[v][i] - truth[i];
            s += e; s2 += e * e; if (fabs(e) > mx) mx = fabs(e);
            if (i > W * 50) { double dd = (m[v][i] - m[v][i-1]) - Tr; d0 += dd; d2 += dd * dd; }
        }
        int cnt = N - W * 50;
        printf("%-28s err_mean=%7.1fus err_rms=%7.1fus err_max=%7.1fus dt_rms_err=%7.1fus\n",
               name[v], s / cnt, sqrt(s2 / cnt), mx, sqrt(d2 / (cnt - 1)));
    }
    return 0;
}
```

```sh
gcc -std=gnu11 -Wall -Wextra -O2 q5_fifo_ts.c -o fifo_ts -lm && ./fifo_ts
```

```text
A all=irq_ts                 err_mean=15467.8us err_rms=17967.4us err_max=30913.1us dt_rms_err= 5509.9us
B back-fill T_nominal        err_mean=  -32.2us err_rms=  112.3us err_max=  286.7us dt_rms_err=   56.9us
C back-fill T_measured       err_mean=  121.3us err_rms=  132.8us err_max=  220.0us dt_rms_err=   13.4us
D C + smoothed anchor        err_mean=  122.3us err_rms=  124.2us err_max=  214.7us dt_rms_err=    1.0us
```

(VM 호스트 유저 공간에서 실행 — 커널과 무관한 순수 계산이다. `rand()` 시드를 고정하지 않았지만 glibc 기본 시드라 매번 같은 값이 나온다.)

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><text x="340" y="24" font-size="14" text-anchor="middle">FIFO 배치(W=32, 1 kHz, 칩 시계 +1%)에서 샘플별 타임스탬프 오차</text><line x1="90" y1="270.0" x2="640" y2="270.0" stroke="currentColor" stroke-opacity="0.15"/><text x="82" y="274.0" font-size="12" text-anchor="end">0 µs</text><line x1="90" y1="196.7" x2="640" y2="196.7" stroke="currentColor" stroke-opacity="0.15"/><text x="82" y="200.7" font-size="12" text-anchor="end">50 µs</text><line x1="90" y1="123.3" x2="640" y2="123.3" stroke="currentColor" stroke-opacity="0.15"/><text x="82" y="127.3" font-size="12" text-anchor="end">100 µs</text>
<line x1="90" y1="50.0" x2="640" y2="50.0" stroke="currentColor" stroke-opacity="0.15"/><text x="82" y="54.0" font-size="12" text-anchor="end">150 µs</text><rect x="110" y="105.3" width="50" height="164.7" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="135" y="100.3" font-size="12" text-anchor="middle">112.3</text><rect x="170" y="186.5" width="50" height="83.5" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/><text x="195" y="181.5" font-size="12" text-anchor="middle">56.9</text><text x="165" y="288" font-size="12" text-anchor="middle">B 공칭 주기</text><rect x="280" y="75.2" width="50" height="194.8" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
<text x="305" y="70.2" font-size="12" text-anchor="middle">132.8</text><rect x="340" y="250.3" width="50" height="19.7" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/><text x="365" y="245.3" font-size="12" text-anchor="middle">13.4</text><text x="335" y="288" font-size="12" text-anchor="middle">C 측정 주기</text><rect x="450" y="87.8" width="50" height="182.2" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="475" y="82.8" font-size="12" text-anchor="middle">124.2</text><rect x="510" y="268.5" width="50" height="1.5" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/><text x="535" y="263.5" font-size="12" text-anchor="middle">1.0</text>
<text x="505" y="288" font-size="12" text-anchor="middle">D C+앵커 평활</text><line x1="90" y1="270" x2="640" y2="270" stroke="currentColor"/><line x1="90" y1="50" x2="90" y2="270" stroke="currentColor"/><rect x="120" y="296" width="12" height="12" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="138" y="306" font-size="12">절대 오차 RMS (대부분 평균 IRQ 지연 ≈ 120 µs)</text><rect x="440" y="296" width="12" height="12" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/><text x="458" y="306" font-size="12">샘플 간격 오차 RMS</text><text x="340" y="324" font-size="12" text-anchor="middle">A(배치 전체에 IRQ 시각 하나)는 절대 오차 17967 µs, 간격 오차 5510 µs라 축 밖</text></svg>
```
그림 4 — 방법별 오차. 간격 오차(주황)는 B → C → D로 57 → 13 → 1 µs로 줄어든다. 절대 오차(파랑)에 남은 약 120 µs는 평균 IRQ 지연이라, 지연을 한 번 측정해 상수로 빼면 사라진다.

출력에서 볼 것:

- **A (배치 전체에 IRQ 시각 하나)**: 최악이다. 32개 샘플이 같은 시각을 가지므로 간격이 0, 0, …, 32 ms처럼 된다. 실무에서 "IMU 적분이 계단처럼 튄다"의 흔한 원인이다.
- **B (공칭 주기로 거꾸로 채우기)**: 칩 시계가 1% 틀리면 배치 앞쪽 샘플일수록 31 × 10 µs = 310 µs까지 틀린다(err_max 286.7).
- **C (IRQ 간격으로 실제 주기 추정)**: 시계 오차는 잡았지만, 각 배치의 앵커(IRQ 시각)가 지연 지터만큼 흔들린다. 배치 경계에서 간격이 튄다(13.4 µs RMS).
- **D (앵커도 평활)**: "이전 앵커 + W × 추정 주기"를 예측값으로 쓰고 측정값 쪽으로 10%만 당긴다. 간단한 PLL(위상 고정 루프)이다. 간격 오차가 1 µs로 떨어진다. 남은 평균 오차 122 µs는 평균 지연(20 + 200/2 = 120)이다.

### 4.3 커널 드라이버는 실제로 어떻게 하나

- `inv_mpu6050`: 공용 모듈 `drivers/iio/common/inv_sensors/inv_sensors_timestamp.c`가 이 일을 한다. `inv_sensors_timestamp_interrupt(ts, sample_nb, timestamp)`가 IRQ 간격 ÷ 샘플 수로 칩 주기를 추정하고(`inv_update_chip_period` — 허용 지터 범위 밖의 값은 버림), `inv_align_timestamp_it`가 마지막 샘플 시각을 IRQ 쪽으로 **한 번에 조금씩만** 당긴다. 그리고 샘플마다 `inv_sensors_timestamp_pop()` = `ts->timestamp += ts->period`. 위 D 방법과 같은 생각이다.
- `st_lsm6dsx`: 칩이 FIFO에 **HW 타임스탬프 태그**(`ST_LSM6DSX_TS_TAG`)를 함께 넣는다. 드라이버는 `ts *= hw->ts_gain` 후 기준값을 더한다. 칩 시계로 찍은 시각이라 IRQ 지연과 무관하다. 대신 칩 시계 ↔ 시스템 시계 오프셋·드리프트 관리가 필요하다.
- `bmi160`(업스트림): FIFO를 안 쓰므로 하드 IRQ에서 찍은 `pf->timestamp`가 샘플 하나의 시각이다. 방법 A·B 문제는 없지만 IRQ 지연 지터가 그대로 들어간다.

**흔한 함정**: 유저 공간에서 `read()`한 시각을 샘플 시각으로 쓰는 것. 그것은 "커널 kfifo에서 꺼낸 시각"이지 측정 시각이 아니다. 반드시 scan 안의 `in_timestamp`를 쓰고, `current_timestamp_clock`을 `monotonic`으로 바꿔 둔다(기본 realtime — I2 10.1절, Q3).

## 5. GNSS (1) — NMEA vs UBX, tty vs `/dev/gnss0`, 그리고 gpsd

### 5.1 두 가지 말 — NMEA 0183과 u-blox UBX

GNSS 수신기(GPS/Galileo/GLONASS/BeiDou)는 보통 두 프로토콜을 동시에 말한다.

| | NMEA 0183 | UBX (u-blox 바이너리) |
|---|---|---|
| 형식 | ASCII 줄: `$GPGGA,...*HH\r\n` | `0xB5 0x62` + class + id + len(LE u16) + payload + 체크섬 2 B |
| 체크섬 | `$`와 `*` 사이 바이트 XOR (16진 2자리) | 8비트 Fletcher (CK_A, CK_B) — class부터 payload까지 |
| 한 메시지 정보량 | 문장마다 일부(GGA 위치, RMC 속도·날짜, GSA DOP, GSV 위성) | NAV-PVT(class 0x01, id 0x07, 92 B) 하나에 시각·위치·속도·정확도·fix 상태 전부 |
| 정밀도 | 문자열 자릿수에 묶임(ddmm.mmmm → 약 0.2 m) | lat/lon 1e-7°, 높이 mm, 속도 mm/s, `tAcc` ns |
| 설정 | 벤더 확장 문장(`$PMTK…`, `$PUBX…`) | `UBX-CFG-*` (요즘 칩은 CFG-VALSET 키-값) |
| 장점 | 사람이 읽힘, 어디서나 지원 | 효율·정밀·정확도 추정치(`hAcc`, `vAcc`, `sAcc`) |

H8에서 NMEA 문장 구조, raw 모드 termios, 체크섬 검증 리더를 직접 짰다. 여기서는 그 위의 **시스템 층**을 본다. 실무 규칙 하나: UBX를 쓰면 tty는 반드시 raw 모드다. `ICRNL` 같은 기본 설정이 `0x0D` 바이트를 바꿔 UBX 체크섬이 깨진다(H8 6.3절).

### 5.2 예제 — NMEA·UBX 로그를 직접 만든다

무엇을 확인하나: 시작점(37.8716 N, 122.2727 W)에서 북쪽으로 1.2 m/s로 걷는 30초 궤적을 1 Hz NMEA(GGA+RMC+GSA)와 UBX NAV-PVT 두 형식으로 만든다. 실제 수신기 없이 gpsd 스택 전체를 검증하는 재료다.

```python
#!/usr/bin/env python3
"""q5_gnss_gen.py: write a fake GNSS log for gpsfake.
nmea: 1 Hz GGA+RMC+GSA, walking north at 1.2 m/s.  ubx: same track as UBX NAV-PVT.
usage: q5_gnss_gen.py nmea|ubx seconds > file"""
import struct, sys, math

LAT0, LON0, ALT = 37.871600, -122.272700, 52.0   # start point (deg, m)
V = 1.2                                          # m/s, heading north

def nmea(body):
    cs = 0
    for ch in body.encode():
        cs ^= ch
    return "$%s*%02X\r\n" % (body, cs)

def dm(deg, is_lat):                              # deg -> ddmm.mmmm,H
    h = ("N" if deg >= 0 else "S") if is_lat else ("E" if deg >= 0 else "W")
    d = abs(deg); D = int(d); m = (d - D) * 60
    return ("%02d%07.4f,%s" if is_lat else "%03d%07.4f,%s") % (D, m, h)

def ubx(cls, mid, payload):
    frame = struct.pack("<BBH", cls, mid, len(payload)) + payload
    a = b = 0
    for x in frame:
        a = (a + x) & 0xFF; b = (b + a) & 0xFF
    return b"\xb5\x62" + frame + bytes([a, b])

def main():
    mode, secs = sys.argv[1], int(sys.argv[2])
    out = sys.stdout.buffer
    for t in range(secs):
        lat = LAT0 + V * t / 111_320.0            # metres -> degrees north
        hh, mm, ss = 18, 0, t                     # 2026-10-18 18:00:ss UTC
        hms = "%02d%02d%02d.00" % (hh, mm, ss)
        if mode == "nmea":
            kn = V / 0.514444
            for s in ("GPGGA,%s,%s,%s,1,08,0.9,%.1f,M,-32.0,M,," % (hms, dm(lat, 1), dm(LON0, 0), ALT),
                      "GPRMC,%s,A,%s,%s,%.2f,0.0,181026,,,A" % (hms, dm(lat, 1), dm(LON0, 0), kn),
                      "GPGSA,A,3,02,05,12,15,19,24,25,29,,,,,1.6,0.9,1.3"):
                out.write(nmea(s).encode())
        else:
            p = struct.pack("<IHBBBBBBIiBBBBiiiiIIiiiiiIIH6xihH",
                (18 * 3600 + ss + 18) * 1000,  # iTOW: Sunday 18:00:ss UTC + 18 leap s
                2026, 10, 18, hh, mm, ss, 0x07, 50, 0,       # date/time, valid, tAcc ns, nano
                3, 0x01, 0, 8,                               # 3D fix, gnssFixOK, numSV
                round(LON0 * 1e7), round(lat * 1e7),
                round((ALT - 32) * 1000), round(ALT * 1000), # height, hMSL (mm)
                1500, 2500, round(V * 1000), 0, 0,           # hAcc vAcc, velN velE velD
                round(V * 1000), 0, 200, 50000, 160,         # gSpeed, headMot, sAcc, headAcc, pDOP
                0, 0, 0)                                     # headVeh, magDec, magAcc
            out.write(ubx(0x01, 0x07, p))

if __name__ == "__main__":
    main()
```

```sh
python3 q5_gnss_gen.py nmea 2
python3 q5_gnss_gen.py ubx 1 | xxd | head -3
```

```text
$GPGGA,180000.00,3752.2960,N,12216.3620,W,1,08,0.9,52.0,M,-32.0,M,,*62
$GPRMC,180000.00,A,3752.2960,N,12216.3620,W,2.33,0.0,181026,,,A*74
$GPGSA,A,3,02,05,12,15,19,24,25,29,,,,,1.6,0.9,1.3*3C
$GPGGA,180001.00,3752.2966,N,12216.3620,W,1,08,0.9,52.0,M,-32.0,M,,*65
$GPRMC,180001.00,A,3752.2966,N,12216.3620,W,2.33,0.0,181026,,,A*73
$GPGSA,A,3,02,05,12,15,19,24,25,29,,,,,1.6,0.9,1.3*3C
00000000: b562 0107 5c00 500b dd03 ea07 0a12 1200  .b..\.P.........
00000010: 0007 3200 0000 0000 0000 0301 0008 a8aa  ..2.............
00000020: 1eb7 60bf 9216 204e 0000 20cb 0000 dc05  ..`... N.. .....
```

(VM 호스트에서 실행. 생성기는 순수 Python이라 어디서 돌려도 같다.)

출력에서 볼 것: `3752.2960,N` = 37° 52.2960′ = 37.871600°. NMEA 위도는 **도·분** 형식이라 그대로 소수점 도로 쓰면 틀린다(흔한 버그). UBX는 `b5 62`(sync) `01 07`(NAV-PVT) `5c 00`(길이 92, LE) 다음에 payload가 온다. 첫 4바이트 `500b dd03` = 0x03dd0b50 = 64,818,000 ms = 일요일 18:00:00 UTC + 윤초 18 s를 GPS 주 안의 ms로 센 iTOW다. `ea07` = 2026, `0a` = 10월, `12` = 18일.

### 5.3 커널 쪽 — tty로 둘까, GNSS 서브시스템으로 둘까

H8 8.3절에서 `/dev/gnss0`을 직접 만들어 봤다. 결정 기준만 정리한다.

| | tty (`/dev/ttymxc1` 등) | GNSS 서브시스템 (`/dev/gnss0`) |
|---|---|---|
| DT | UART 노드 `status = "okay"`만 | UART 노드 아래 `gnss { compatible = "u-blox,neo-m8"; ... }` |
| baud·termios | 유저(gpsd)가 설정·자동 탐지 | 드라이버가 DT `current-speed`로 설정, termios 없음 |
| 전원 | 유저가 GPIO로 따로 | open/close에 맞춰 드라이버가 `vcc-supply` 켜고 끔 |
| 프로토콜 표시 | 없음 | `type` 속성(NMEA, SiRF, UBX, MTK) |
| 언제 | 대부분의 로봇·개발 보드 | 저전력, 장치 수명 관리가 필요한 제품 |

이 랩 커널(Image-dom)은 `CONFIG_GNSS_MTK_SERIAL=m`만 있고 `CONFIG_GNSS_UBX_SERIAL`은 꺼져 있다(`.config`에서 확인). 실제 u-blox 보드라면 이 옵션을 켠다.

### 5.4 gpsd 구조

```svg
<svg viewBox="0 0 680 392" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="20" y="30" width="150" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="95" y="48" font-size="12" text-anchor="middle">/dev/ttyS1 (tty)</text><text x="95" y="65" font-size="12" text-anchor="middle">raw 모드 termios</text><rect x="20" y="90" width="150" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="95" y="108" font-size="12" text-anchor="middle">/dev/gnss0</text>
<text x="95" y="125" font-size="12" text-anchor="middle">GNSS 서브시스템 (H8)</text><rect x="20" y="150" width="150" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="95" y="168" font-size="12" text-anchor="middle">gpsfake: pty로</text><text x="95" y="185" font-size="12" text-anchor="middle">로그 파일 재생</text><rect x="20" y="230" width="150" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="95" y="248" font-size="12" text-anchor="middle">/dev/pps0</text><text x="95" y="265" font-size="12" text-anchor="middle">pps-gpio (1PPS 핀)</text><rect x="220" y="30" width="230" height="164" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="335" y="52" font-size="13" text-anchor="middle" font-weight="bold">gpsd</text><text x="335" y="76" font-size="12" text-anchor="middle">packet sniffer: NMEA/UBX/</text><text x="335" y="93" font-size="12" text-anchor="middle">RTCM/SiRF… 자동 판별</text><text x="335" y="117" font-size="12" text-anchor="middle">드라이버별 파서 → 통합 모델</text><text x="335" y="134" font-size="12" text-anchor="middle">(TPV · SKY · PPS · TOFF)</text><text x="335" y="158" font-size="12" text-anchor="middle">cycle 끝마다 JSON 보고</text><text x="335" y="180" font-size="12" text-anchor="middle">-n: 클라이언트 없어도 읽기</text><line x1="170" y1="52" x2="218" y2="70" stroke="currentColor" marker-end="url(#ah3)"/>
<line x1="170" y1="112" x2="218" y2="112" stroke="currentColor" marker-end="url(#ah3)"/><line x1="170" y1="172" x2="218" y2="160" stroke="currentColor" marker-end="url(#ah3)"/><line x1="170" y1="252" x2="260" y2="196" stroke="currentColor" marker-end="url(#ah3)"/><rect x="500" y="30" width="160" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="580" y="48" font-size="12" text-anchor="middle">TCP :2947 JSON</text><text x="580" y="65" font-size="12" text-anchor="middle">?WATCH / ?POLL</text><rect x="500" y="100" width="160" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="580" y="118" font-size="12" text-anchor="middle">SHM 0/1 (NTP SHM)</text><text x="580" y="135" font-size="12" text-anchor="middle">또는 chrony SOCK</text><line x1="450" y1="60" x2="498" y2="52" stroke="currentColor" marker-end="url(#ah3)"/><line x1="450" y1="130" x2="498" y2="122" stroke="currentColor" marker-end="url(#ah3)"/><rect x="500" y="200" width="160" height="80" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="580" y="220" font-size="12" text-anchor="middle">클라이언트</text><text x="580" y="238" font-size="12" text-anchor="middle">gpspipe -w · cgps</text><text x="580" y="255" font-size="12" text-anchor="middle">libgps / Python</text>
<text x="580" y="272" font-size="12" text-anchor="middle">ROS gpsd_client</text><line x1="620" y1="74" x2="620" y2="198" stroke="currentColor" marker-end="url(#ah3)"/><rect x="290" y="230" width="160" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/><text x="370" y="248" font-size="12" text-anchor="middle">chronyd</text><text x="370" y="265" font-size="12" text-anchor="middle">refclock SHM + PPS</text><line x1="540" y1="144" x2="440" y2="228" stroke="currentColor" marker-end="url(#ah3)"/><line x1="170" y1="262" x2="288" y2="256" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah3)"/>
<text x="20" y="310" font-size="12">gpsd는 장치를 하나 열고 여러 클라이언트에 나눠 준다: 장치 1개 ↔ 소비자 N개.</text><text x="20" y="330" font-size="12">시각은 두 갈래: NMEA/UBX 시각(ms 단위로 흔들림) → SHM 0,</text><text x="20" y="350" font-size="12">PPS 엣지(µs 이하) → SHM 1, 또는 chrony가 /dev/pps0을 직접 읽는다.</text><text x="20" y="370" font-size="12">점선: chrony refclock PPS /dev/pps0 — gpsd를 거치지 않는 경로.</text></svg>
```
그림 5 — gpsd 구조. 입력(tty, `/dev/gnss0`, gpsfake의 pty, 네트워크 소스)을 packet sniffer가 프로토콜별로 갈라 파싱하고, 통합된 보고(TPV = 시각·위치·속도, SKY = 위성·DOP)를 TCP 2947의 JSON으로 여러 클라이언트에 나눠 준다. 시각은 SHM을 통해 chrony로 간다.

gpsd(GPS daemon)는 **장치 하나를 열어 여러 소비자에게 나눠 주는 브로커**다. Don의 말로 하면 "UART 링버퍼 + 프레임 파서 + publish/subscribe 서버를 유저 공간 프로세스 하나로 묶은 것"이다. 핵심 개념:

- **packet sniffer**: 바이트 스트림에서 NMEA·UBX·RTCM 등을 자동 판별한다. 그래서 같은 포트에 NMEA와 UBX가 섞여 와도 된다.
- **cycle**: 수신기는 매 측위 주기(1 Hz 등)에 여러 문장을 보낸다. gpsd는 cycle 끝에서 TPV를 보고한다.
- **JSON 프로토콜**: 클라이언트가 `?WATCH={"enable":true,"json":true};`를 보내면 스트림이 시작된다. 한 줄에 JSON 객체 하나. `?POLL;`은 마지막 상태를 한 번에 받는다.
- 클라이언트 도구: `gpspipe -w`(JSON 그대로), `cgps`(curses 화면 — 이 노트에서는 실행 안 함), `gpsmon`(장치 직접 모니터), `gpsdecode`(데몬 없이 로그 디코드), `gpsfake`(로그를 pty로 재생하며 테스트용 gpsd를 띄움).

## 6. GNSS (2) — gpsdecode · gpsfake · gpsd JSON 클라이언트

### 6.1 예제 — 데몬 없이 로그 디코드 (`gpsdecode`)

무엇을 확인하나: 만든 두 로그를 gpsd의 파서로 바로 디코드해 JSON TPV가 나오는지. 현장에서 받은 수신기 덤프를 분석할 때 가장 먼저 쓰는 도구다.

```sh
# 게스트 (gpsd 3.25)
python3 q5_gnss_gen.py nmea 30 > nmea.log; python3 q5_gnss_gen.py ubx 30 > ubx.log
gpsdecode < nmea.log | head -2
gpsdecode < ubx.log | head -2
```

```text
{"class":"TPV","device":"stdin","mode":3,"time":"2026-10-18T18:00:01.000Z","ept":0.005,"lat":37.871610000,"lon":-122.272700000,"altHAE":20.0000,"altMSL":52.0000,"alt":52.0000,"epv":29.900,"track":0.0000,"magtrack":13.2591,"magvar":13.3,"speed":1.199,"climb":0.000,"epc":59.80,"geoidSep":-32.000,"eph":17.100,"sep":30.400}
{"class":"TPV","device":"stdin","mode":3,"time":"2026-10-18T18:00:01.000Z","ept":0.005,"lat":37.871610000,"lon":-122.272700000,"altHAE":20.0000,"altMSL":52.0000,"alt":52.0000,"epv":29.900,"track":0.0000,"magtrack":13.2591,"magvar":13.3,"speed":1.199,"climb":0.000,"epc":59.80,"geoidSep":-32.000,"eph":17.100,"sep":30.400}
{"class":"TPV","device":"stdin","mode":3,"time":"2026-10-18T18:00:02.000Z","ept":0.005,"lat":37.871621600,"lon":-122.272700000,"altHAE":20.0000,"altMSL":52.0000,"alt":52.0000,"epv":2.500,"track":0.0000,"magtrack":13.2591,"magvar":13.3,"speed":1.200,"climb":0.000,"eps":0.20,"epc":5.00,"geoidSep":-31.710,"eph":1.500}
{"class":"TPV","device":"stdin","mode":3,"time":"2026-10-18T18:00:03.000Z","ept":0.005,"lat":37.871632300,"lon":-122.272700000,"altHAE":20.0000,"altMSL":52.0000,"alt":52.0000,"epv":2.500,"track":0.0000,"magtrack":13.2591,"magvar":13.3,"speed":1.200,"climb":0.000,"eps":0.20,"epc":5.00,"geoidSep":-31.710,"eph":1.500}
```

(위 두 줄은 NMEA, 아래 두 줄은 UBX.)

출력에서 볼 것:

- `mode:3` = 3D fix. `altHAE`(타원체 높이) 20 m = `altMSL` 52 m + `geoidSep` −32 m. NMEA GGA의 고도는 **평균 해수면(MSL)** 기준이고, 센서 퓨전·지도는 보통 **타원체(WGS84) 높이**를 원한다. 32 m 차이는 "드론 고도가 이상하다"의 단골 원인이다.
- 정확도 추정치가 다르다. NMEA에서는 HDOP 0.9로부터 gpsd가 `eph` 17.1 m를 **추정**했고, UBX에서는 수신기가 보낸 `hAcc` 1500 mm가 그대로 `eph` 1.5 m가 됐다. 정확도가 필요하면 UBX(또는 NMEA GST 문장)를 쓴다.
- NMEA 로그의 첫 TPV가 두 번 나왔다(GGA와 RMC가 같은 시각을 각각 보고). 클라이언트는 `time`으로 중복을 거른다.

### 6.2 gpsfake가 이 게스트에서 실패한 이유 (기록)

계획은 `gpsfake -1 -c 0.5 -P 2947 nmea.log`로 로그를 pty에 재생하는 것이었다. 세 번 시도했고 모두 실패했다.

```text
gpsd:ERROR: /dev/pts/1: device activation failed, freeing device.
Test timed out: maybe increase WRITE_PAD (= 0.05)
SHM: shmctl(12) for IPC_RMID failed, Operation not permitted(1)
```

`nobody` 사용자로 돌려도 `gps.fake.TestSessionError: daemon died`. 게스트에서 확인한 사실은 이것뿐이다.

```text
uid=103(gpsd) gid=20(dialout) groups=20(dialout)
/usr/sbin/gpsd (enforce)
```

즉 root로 띄운 gpsd는 `gpsd` 사용자로 권한을 내리고, `/usr/sbin/gpsd`에 AppArmor 프로파일이 enforce 모드로 걸려 있다. root가 만든 pty(`/dev/pts/N`, 소유 root:tty)를 권한을 내린 gpsd가 못 열었거나 AppArmor가 막았을 것으로 **추정**한다 — 커널 로그에서 DENIED 줄은 찾지 못했고, 프로파일을 풀어 보는 실험은 공용 랩 보안 설정을 약하게 하는 일이라 하지 않았다. 실무 교훈: **데몬이 "device activation failed"를 내면 (1) 장치 노드 권한·그룹(dialout), (2) AppArmor/SELinux 프로파일, (3) 다른 프로세스가 포트를 잡고 있는지(ModemManager가 GNSS tty를 찌르는 일이 흔하다)**를 차례로 본다.

### 6.3 예제 — gpsd를 UDP 소스로 띄우고 JSON 클라이언트로 받기

gpsd는 장치 경로 대신 `udp://host:port`·`tcp://host:port`도 입력으로 받는다(네트워크로 NMEA를 뿌리는 수신기·RTK 보정기에서 쓰는 기능). pty가 필요 없어서 이 길로 같은 로그를 재생했다.

무엇을 확인하나: 의존성 없는 소켓 클라이언트가 `?WATCH`를 보내고 TPV를 받아, 첫 fix 대비 북쪽 이동 거리를 계산하는지.

```python
#!/usr/bin/env python3
"""q5_gpsd_client.py: minimal gpsd JSON client (no gps module needed).
Sends ?WATCH, reads line-delimited JSON, prints TPV/SKY summaries and checks
fix age + distance travelled.  usage: q5_gpsd_client.py [port] [n_tpv]"""
import json, math, socket, sys, time

port = int(sys.argv[1]) if len(sys.argv) > 1 else 2947
want = int(sys.argv[2]) if len(sys.argv) > 2 else 5
s = socket.create_connection(("127.0.0.1", port), timeout=30)
s.sendall(b'?WATCH={"enable":true,"json":true};\n')
f = s.makefile("r", encoding="utf-8", newline="\n")
first = None; n = 0
for line in f:
    msg = json.loads(line)
    cls = msg.get("class")
    if cls in ("VERSION", "DEVICES", "WATCH"):
        if cls == "VERSION":
            print("VERSION release=%s proto=%s.%s" % (msg["release"], msg["proto_major"], msg["proto_minor"]))
        if cls == "DEVICES":
            for d in msg["devices"]:
                print("DEVICE path=%s driver=%s" % (d.get("path"), d.get("driver", "?")))
        continue
    if cls != "TPV" or msg.get("mode", 0) < 2:
        continue
    lat, lon = msg["lat"], msg["lon"]
    if first is None:
        first = (lat, lon)
    dn = (lat - first[0]) * 111_320.0            # metres north of first fix
    print("TPV time=%s mode=%d lat=%.7f lon=%.7f alt=%s speed=%.2f north=%.1fm"
          % (msg.get("time"), msg["mode"], lat, lon, msg.get("altHAE", msg.get("alt")),
             msg.get("speed", float("nan")), dn))
    n += 1
    if n >= want:
        break
s.close()
```

```sh
# 게스트: feed.py는 로그를 1초 cycle 단위로 0.5초마다 udp 127.0.0.1:41720에 보낸다
gpsd -N -n -S 2949 udp://127.0.0.1:41720 & GP=$!
sleep 2; python3 /tmp/feed.py /tmp/nmea.log nmea &
timeout 60 python3 q5_gpsd_client.py 2949 4
timeout 20 gpspipe -w -n 5 127.0.0.1:2949 | cut -c1-260
```

```text
### gpsd -N -n udp://127.0.0.1:41720 fed with nmea
VERSION release=3.25 proto=3.15
DEVICE path=udp://127.0.0.1:41720 driver=NMEA0183
TPV time=2026-10-18T18:00:01.000Z mode=3 lat=37.8716100 lon=-122.2727000 alt=20.0 speed=1.20 north=0.0m
TPV time=2026-10-18T18:00:01.000Z mode=3 lat=37.8716100 lon=-122.2727000 alt=20.0 speed=1.20 north=0.0m
TPV time=2026-10-18T18:00:02.000Z mode=3 lat=37.8716217 lon=-122.2727000 alt=20.0 speed=1.20 north=1.3m
TPV time=2026-10-18T18:00:02.000Z mode=3 lat=37.8716217 lon=-122.2727000 alt=20.0 speed=1.20 north=1.3m
### gpspipe -w -n 5 (nmea)
{"class":"VERSION","release":"3.25","rev":"3.25","proto_major":3,"proto_minor":15}
{"class":"DEVICES","devices":[{"class":"DEVICE","path":"udp://127.0.0.1:41720","driver":"NMEA0183","activated":"2026-10-18T20:44:30.614Z","flags":1}]}
{"class":"WATCH","enable":true,"json":true,"nmea":false,"raw":0,"scaled":false,"timing":false,"split24":false,"pps":false}
{"class":"SKY","device":"udp://127.0.0.1:41720","hdop":0.90,"pdop":1.60,"vdop":1.30,"uSat":8}
{"class":"TPV","device":"udp://127.0.0.1:41720","mode":3,"time":"2026-10-18T18:00:03.000Z","ept":0.005,"lat":37.871631667,"lon":-122.272700000,"altHAE":20.0000,"altMSL":52.0000,"alt":52.0000,"epv":29.900,"track":0.0000,"magtrack":13.2591,"magvar":13.3,"speed":
### gpsd -N -n udp://127.0.0.1:41720 fed with ubx
VERSION release=3.25 proto=3.15
DEVICE path=udp://127.0.0.1:41720 driver=u-blox
TPV time=2026-10-18T18:00:02.000Z mode=3 lat=37.8716216 lon=-122.2727000 alt=20.0 speed=1.20 north=0.0m
TPV time=2026-10-18T18:00:03.000Z mode=3 lat=37.8716323 lon=-122.2727000 alt=20.0 speed=1.20 north=1.2m
TPV time=2026-10-18T18:00:04.000Z mode=3 lat=37.8716431 lon=-122.2727000 alt=20.0 speed=1.20 north=2.4m
TPV time=2026-10-18T18:00:05.000Z mode=3 lat=37.8716539 lon=-122.2727000 alt=20.0 speed=1.20 north=3.6m
```

출력에서 볼 것:

- `DEVICE ... driver=NMEA0183` vs `driver=u-blox` — 같은 포트에 다른 바이트를 흘렸을 뿐인데 gpsd의 packet sniffer가 프로토콜을 스스로 판별했다.
- UBX 쪽은 TPV가 초마다 하나, 이동 거리가 1.2 m씩 정확히 늘었다. NMEA 쪽은 같은 시각 TPV가 두 번씩(6.1절과 같은 현상), 거리 1.3 m는 NMEA 위도 자릿수(0.0001′ ≈ 0.19 m) 반올림 때문이다.
- `"activated":"2026-10-18T20:44:30Z"`(게스트 시스템 시계)와 `"time":"…18:00:03Z"`(위성 시각)가 다르다. **GNSS 시각과 시스템 시계는 별개다.** 둘을 맞추는 것이 다음 절이다.
- 메시지 클래스: `VERSION`(접속 직후 1회), `DEVICES`, `WATCH`(설정 에코), `SKY`(위성·DOP), `TPV`(Time-Position-Velocity). PPS를 켜면(`"pps":true`) `PPS`·`TOFF` 클래스도 온다.

### 6.4 PPS + chrony — GNSS로 시스템 시계 맞추기 (가이드, 실행 안 함)

깊은 내용(PPS 원리, PTP, `phc2sys`)은 Q3에 있다. 여기서는 gpsd와의 연결만 본다.

NMEA/UBX 문장이 "지금은 18:00:03"이라고 말해도 그 문장이 **언제 도착했는지**는 UART 전송 시간·tty 지연(H8 예제 13) 때문에 수~수십 ms 흔들린다. 그래서 수신기는 매 초 정각에 **PPS(Pulse Per Second)** 핀을 토글한다. PPS 엣지를 GPIO 인터럽트로 받아 커널이 시각을 찍는다(`pps-gpio` → `/dev/pps0`). 역할 분담은 "문장 = 몇 초인지(초 단위 레이블), PPS = 그 초가 정확히 언제 시작했는지(µs 이하)"다.

```dts
pps {
    compatible = "pps-gpio";
    gpios = <&gpio3 7 GPIO_ACTIVE_HIGH>;
};
```

```text
# /etc/chrony/chrony.conf (발췌 — 실행 안 함)
# gpsd가 NMEA/UBX 시각을 SHM 0에 쓴다. 지연을 모르니 offset으로 보정, 단독 선택 금지
refclock SHM 0 refid GNSS offset 0.12 delay 0.2 noselect
# PPS 엣지는 chrony가 /dev/pps0을 직접 읽고, 초 레이블은 GNSS에서 빌린다
refclock PPS /dev/pps0 refid PPS lock GNSS precision 1e-7
```

- gpsd 쪽: `gpsd -n /dev/ttymxc1 /dev/pps0` 처럼 띄우면 SHM 0(문장 시각)과 SHM 1(PPS)을 채운다. chrony를 먼저 띄우고 gpsd를 나중에 띄우면 `refclock SOCK /run/chrony.ttymxc1.sock`도 쓸 수 있다.
- `offset 0.12`는 "문장이 정각보다 평균 120 ms 늦게 도착한다"는 측정값이다. 보드마다 `chronyc sourcestats`로 재서 정한다.
- `lock GNSS`: PPS는 초 단위 정체성이 없으므로 GNSS 소스와 짝지어 몇 초인지를 정한다.
- 확인: `chronyc sources -v`에서 PPS 줄 앞에 `*`(선택됨), `ppstest /dev/pps0`으로 엣지가 1초마다 오는지.

이 랩 게스트에는 `pps-ktimer`(커널 타이머로 가짜 PPS 생성)도 있어 `/dev/pps0` 경로까지는 확인할 수 있다 — Q3에서 다룬다.

## 7. LiDAR (1) — UDP 패킷 스트림이라는 센서

### 7.1 패킷 모양

회전형 LiDAR는 레이저 발사 결과를 **작은 UDP 패킷의 끝없는 스트림**으로 보낸다. 연결도, 재전송도, 흐름 제어도 없다. 놓치면 그 각도의 점들이 영원히 사라진다.

| | Velodyne VLP-16 계열 | Ouster OS 계열 |
|---|---|---|
| 페이로드 | 1206 B = 12 block × 100 B + 4 B 타임스탬프 + 2 B factory | 프로파일에 따라 다름, column마다 헤더 |
| block / column | `0xFFEE` flag + azimuth u16(0.01°) + 32 × (거리 u16 + 반사율 u8) | measurement ID(회전 안 몇 번째 column), frame ID, encoder count |
| 시퀀스 번호 | **없음** — azimuth와 타임스탬프로 갭을 판단 | frame ID + measurement ID로 판단 |
| 타임스탬프 | 정각 이후 µs (u32), PPS/GPS 또는 PTP로 동기 | ns 단위, PTP(IEEE 1588) 동기 지원 |
| 기본 포트 | 데이터 2368, 위치(GPS) 8308 | 7502(lidar), 7503(IMU) |
| 패킷률 | 약 754 pps(단일 return) | 해상도·회전수에 따라 수백~수천 pps |

(표의 숫자는 기억 기반 — 각 제품 사용자 매뉴얼로 확인 필요. 이 노트의 시뮬레이터는 VLP-16 배치를 흉내 낸다.)

데이터율 계산: 754 pps × 1206 B × 8 = 7.3 Mbit/s. 100 Mbit 포트로 충분해 보이지만, 문제는 평균이 아니라 **burst와 수신 측 CPU 스케줄링**이다(A9 5.5절의 "burst × truesize" 규칙).

Don의 SSD 경험과 연결하면: NVMe 완료 큐(CQ)를 놓치면 호스트가 재요청하지만, LiDAR는 "phase bit도 doorbell도 없는 CQ"다. 생산자는 기다려 주지 않는다.

### 7.2 수신 경로와 잃는 곳

```svg
<svg viewBox="0 0 680 444" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="125" y="24" font-size="13" text-anchor="middle" font-weight="bold">조정 손잡이</text><text x="340" y="24" font-size="13" text-anchor="middle" font-weight="bold">수신 경로</text><text x="575" y="24" font-size="13" text-anchor="middle" font-weight="bold">버려지면 여기 찍힌다</text><rect x="235" y="40" width="210" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="65" font-size="12" text-anchor="middle">NIC RX 링 (DMA 디스크립터)</text><text x="225" y="65" font-size="12" text-anchor="end">ethtool -G rx 4096</text><rect x="470" y="46" width="200" height="28" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="570" y="65" font-size="12" text-anchor="middle">ethtool -S rx_missed 등</text><line x1="445" y1="60" x2="468" y2="60" stroke="#d0564a" stroke-dasharray="3 2"/><line x1="340" y1="80" x2="340" y2="91" stroke="currentColor" marker-end="url(#ah4)"/><rect x="235" y="92" width="210" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="117" font-size="12" text-anchor="middle">IRQ → NAPI poll (softirq)</text><text x="225" y="117" font-size="12" text-anchor="end">IRQ affinity, coalescing</text><rect x="470" y="98" width="200" height="28" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="570" y="117" font-size="12" text-anchor="middle">softnet_stat 3열 squeeze</text><line x1="445" y1="112" x2="468" y2="112" stroke="#d0564a" stroke-dasharray="3 2"/><line x1="340" y1="132" x2="340" y2="143" stroke="currentColor" marker-end="url(#ah4)"/><rect x="235" y="144" width="210" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="169" font-size="12" text-anchor="middle">RPS/RFS → CPU별 backlog</text><text x="225" y="169" font-size="12" text-anchor="end">rps_cpus, netdev_max_backlog</text><rect x="470" y="150" width="200" height="28" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="570" y="169" font-size="12" text-anchor="middle">softnet_stat 2열 dropped</text><line x1="445" y1="164" x2="468" y2="164" stroke="#d0564a" stroke-dasharray="3 2"/><line x1="340" y1="184" x2="340" y2="195" stroke="currentColor" marker-end="url(#ah4)"/><rect x="235" y="196" width="210" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="221" font-size="12" text-anchor="middle">udp_rcv → 소켓 찾기</text><text x="225" y="221" font-size="12" text-anchor="end">(포트·소켓 수)</text><rect x="470" y="202" width="200" height="28" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="570" y="221" font-size="12" text-anchor="middle">Udp NoPorts, InErrors</text><line x1="445" y1="216" x2="468" y2="216" stroke="#d0564a" stroke-dasharray="3 2"/><line x1="340" y1="236" x2="340" y2="247" stroke="currentColor" marker-end="url(#ah4)"/><rect x="235" y="248" width="210" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="273" font-size="12" text-anchor="middle">소켓 수신 큐 (sk_rcvbuf)</text><text x="225" y="273" font-size="12" text-anchor="end">SO_RCVBUF, rmem_max</text><rect x="470" y="254" width="200" height="28" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="570" y="273" font-size="12" text-anchor="middle">Udp RcvbufErrors, ss -u d</text><line x1="445" y1="268" x2="468" y2="268" stroke="#d0564a" stroke-dasharray="3 2"/><line x1="340" y1="288" x2="340" y2="299" stroke="currentColor" marker-end="url(#ah4)"/><rect x="235" y="300" width="210" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="325" font-size="12" text-anchor="middle">앱: recv / recvmmsg</text><text x="225" y="325" font-size="12" text-anchor="end">recvmmsg, busy_read, SCHED_FIFO</text><rect x="470" y="306" width="200" height="28" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="570" y="325" font-size="12" text-anchor="middle">내 seq/azimuth 갭 카운터</text><line x1="445" y1="320" x2="468" y2="320" stroke="#d0564a" stroke-dasharray="3 2"/><line x1="340" y1="340" x2="340" y2="351" stroke="currentColor" marker-end="url(#ah4)"/><rect x="235" y="352" width="210" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="340" y="377" font-size="12" text-anchor="middle">앱 링버퍼 → 스캔 조립 스레드</text><text x="225" y="377" font-size="12" text-anchor="end">스레드 분리, CPU 고립</text><rect x="470" y="358" width="200" height="28" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/><text x="570" y="377" font-size="12" text-anchor="middle">링 overflow 카운터</text><line x1="445" y1="372" x2="468" y2="372" stroke="#d0564a" stroke-dasharray="3 2"/><text x="340" y="424" font-size="12" text-anchor="middle">loopback 실험(7.6절)은 위 세 칸을 건너뛴다 — 소켓 단계 이후만 측정된다</text></svg>
```
그림 6 — LiDAR 수신 경로와 손실 지점. 가운데가 패킷이 지나는 층, 왼쪽이 그 층을 고치는 손잡이, 오른쪽이 버려질 때 증가하는 카운터. 파란 상자는 커널, 초록 상자는 앱.

| 손잡이 | 무엇을 고치나 | 어디서 설정 | 주의 |
|---|---|---|---|
| `SO_RCVBUF` | 소켓 큐 크기 (burst 흡수) | 앱 `setsockopt` | `net.core.rmem_max`에서 잘린다, 커널은 요청값의 2배를 기록 |
| `net.core.rmem_max` | `SO_RCVBUF` 상한 | `sysctl`, `/etc/sysctl.d/` | 시스템 전역 — **이 노트에서는 게스트 안에서만 바꿨다** |
| `recvmmsg()` | syscall 1번에 여러 패킷 → 패킷당 CPU 감소 | 앱 | 블로킹 소켓에서 `MSG_WAITFORONE` 없으면 vlen개 찰 때까지 기다린다 |
| busy poll (`SO_BUSY_POLL`, `net.core.busy_read`) | 인터럽트·softirq 대신 앱이 NIC 큐를 직접 돌림 → 지연·지터 감소 | 앱 / sysctl | CPU를 태운다, NAPI를 지원하는 NIC에서만 의미 |
| IRQ affinity (`/proc/irq/N/smp_affinity`) | NIC IRQ를 특정 CPU에 고정 | 시스템 | 수신 앱과 같은 CPU(캐시)냐 다른 CPU(병렬)냐를 의도적으로 고른다 |
| RPS/RFS (`/sys/class/net/eth0/queues/rx-0/rps_cpus`) | 단일 큐 NIC에서 프로토콜 처리를 다른 CPU로 분산 | 시스템 | 큐가 여러 개인 NIC면 RSS가 HW로 한다 |
| `netdev_max_backlog` | RPS backlog 큐 길이 | sysctl | `softnet_stat` 2열이 늘 때만 |
| `ethtool -G eth0 rx N` | NIC RX 디스크립터 링 길이 | 시스템 | `ethtool -S`의 `rx_missed`/`rx_fifo` 계열이 늘 때 |
| 수신·처리 스레드 분리 | 평균 처리율 부족 | 앱 설계 | 버퍼는 시간만 벌어 준다 (A9 5.4절) |
| `SO_TIMESTAMPNS` / HW 타임스탬프 | 도착 시각을 커널(또는 NIC)이 찍음 | 앱 / `SIOCSHWTSTAMP` | 앱이 `recv` 후 찍은 시각은 스케줄링 지연을 포함 |

### 7.3 예제 — VLP-16 형 시뮬레이터 (송신기)

무엇을 확인하나: 1206 B 패킷을 설정한 pps로 보낸다. azimuth는 10 Hz 회전에 맞춰 block마다 증가하고, 타임스탬프는 정각 이후 µs. `drop_every`를 주면 "선로에서 잃은" 패킷을 흉내 내어 수신기의 갭 검출을 검증한다.

```c
/* q5_lidar_tx.c: VLP-16-style UDP simulator. 1206-byte packets: 12 blocks x
 * (0xFFEE, azimuth u16 in 0.01 deg, 32 x (dist u16, refl u8)) + u32 usec-past-hour
 * + 2 factory bytes. 10 Hz rotation.  usage: tx ip port pps secs [drop_every] */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: %s ip port pps secs [drop_every]\n", argv[0]); exit(1); }
    long pps = atol(argv[3]), secs = atol(argv[4]), drop = argc > 5 ? atol(argv[5]) : 0;
    struct sockaddr_in d = { .sin_family = AF_INET, .sin_port = htons(atoi(argv[2])) };
    if (inet_pton(AF_INET, argv[1], &d.sin_addr) != 1) { fprintf(stderr, "bad ip\n"); exit(1); }
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) { perror("socket"); exit(1); }
    uint8_t pkt[1206];
    memset(pkt, 0, sizeof pkt);
    pkt[1204] = 0x37; pkt[1205] = 0x22;             /* strongest return, VLP-16 */
    double blk_step = 36000.0 * 10 / (pps * 12.0);  /* 0.01 deg per block @10 Hz */
    long total = pps * secs, sent = 0, skipped = 0, per_ms = pps / 1000 ? pps / 1000 : 1;
    long tick_ns = per_ms * 1000000000L / pps;      /* send per_ms pkts per tick */
    struct timespec next, t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &next); t0 = next;
    for (long k = 0; k < total; k++) {
        for (int b = 0; b < 12; b++) {
            uint16_t az = (uint16_t)((long)((k * 12 + b) * blk_step) % 36000);
            pkt[b * 100] = 0xFF; pkt[b * 100 + 1] = 0xEE;
            memcpy(pkt + b * 100 + 2, &az, 2);       /* little endian on arm64 */
        }
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        uint32_t us = (uint32_t)((now.tv_sec % 3600) * 1000000L + now.tv_nsec / 1000);
        memcpy(pkt + 1200, &us, 4);
        if (drop && k % drop == drop - 1) skipped++;  /* fake loss on the wire */
        else if (sendto(s, pkt, sizeof pkt, 0, (struct sockaddr *)&d, sizeof d) == sizeof pkt) sent++;
        if (k % per_ms == per_ms - 1) {
            next.tv_nsec += tick_ns;
            while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double el = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("tx: pps=%ld generated=%ld sent=%ld fake_dropped=%ld actual=%.0f pps over %.2fs\n",
           pps, total, sent, skipped, total / el, el);
    return 0;
}
```

### 7.4 예제 — 수신기: azimuth 갭으로 손실 추정

무엇을 확인하나: 시퀀스 번호가 없는 스트림에서 연속 패킷의 azimuth 차이를 보고 빠진 패킷 수를 추정한다. 처음 32개 패킷에서 최소 차이를 "패킷당 azimuth step"으로 배운다(손실은 차이를 키우기만 하므로 최소값이 정답). `recv`와 `recvmmsg` 모드, `SO_RCVBUF` 요청값을 바꿀 수 있고, CPU 시간도 잰다.

```c
/* q5_lidar_rx.c: receive VLP-16-style packets, detect loss from azimuth gaps.
 * usage: rx port mode(0=recvfrom,1=recvmmsg) rcvbuf_bytes(0=default) */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/resource.h>

#define VLEN 64
static uint8_t bufs[VLEN][1500];

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s port mode rcvbuf\n", argv[0]); exit(1); }
    int mode = atoi(argv[2]), req = atoi(argv[3]), eff; socklen_t el = sizeof eff;
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(atoi(argv[1])) };
    if (s < 0 || bind(s, (struct sockaddr *)&a, sizeof a) < 0) { perror("socket/bind"); exit(1); }
    if (req && setsockopt(s, SOL_SOCKET, SO_RCVBUF, &req, sizeof req) < 0) { perror("SO_RCVBUF"); exit(1); }
    getsockopt(s, SOL_SOCKET, SO_RCVBUF, &eff, &el);
    struct timeval tv = { 2, 0 };                     /* stop 2 s after traffic ends */
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    struct mmsghdr mm[VLEN]; struct iovec io[VLEN];
    for (int i = 0; i < VLEN; i++) {
        io[i] = (struct iovec){ bufs[i], sizeof bufs[i] };
        mm[i] = (struct mmsghdr){ .msg_hdr = { .msg_iov = &io[i], .msg_iovlen = 1 } };
    }
    long got = 0, calls = 0, lost = 0, gaps = 0, bad = 0;
    int prev = -1, step = 0;                           /* step: az delta per packet */
    for (;;) {
        int n = mode ? recvmmsg(s, mm, VLEN, MSG_WAITFORONE, NULL) : 0;
        if (!mode) { ssize_t r = recv(s, bufs[0], sizeof bufs[0], 0); n = r < 0 ? -1 : 1; mm[0].msg_len = r; }
        if (n < 0) break;                              /* timeout -> done */
        calls++;
        for (int i = 0; i < n; i++, got++) {
            uint8_t *p = bufs[i];
            if (mm[i].msg_len != 1206 || p[0] != 0xFF || p[1] != 0xEE) { bad++; continue; }
            uint16_t az; memcpy(&az, p + 2, 2);
            if (prev >= 0) {
                int d = (az - prev + 36000) % 36000;   /* wraps at 360.00 deg */
                if (got < 32 && (step == 0 || d < step)) step = d;   /* learn min delta */
                else if (step && d > step + step / 2) { gaps++; lost += (d + step / 2) / step - 1; }
            }
            prev = az;
        }
    }
    struct rusage ru; getrusage(RUSAGE_SELF, &ru);
    double cpu = ru.ru_utime.tv_sec * 1e3 + ru.ru_utime.tv_usec / 1e3 + ru.ru_stime.tv_sec * 1e3 + ru.ru_stime.tv_usec / 1e3;
    printf("rx: mode=%s rcvbuf_req=%d eff=%d got=%ld syscalls=%ld pkts/call=%.1f cpu=%.0fms "
           "az_step=%d gaps=%ld est_lost=%ld bad=%ld\n", mode ? "recvmmsg" : "recv", req, eff,
           got, calls, calls ? (double)got / calls : 0, cpu, step, gaps, lost, bad);
    return 0;
}
```

### 7.5 실행 1 — 갭 검출이 맞는가, `SO_RCVBUF`는 어디서 잘리나

```sh
# g_lidar.sh (발췌) — 게스트, 송수신 모두 loopback
udp_err() { awk '/^Udp:/{n++; if(n==2) print $6}' /proc/net/snmp; }   # RcvbufErrors
run() { e0=$(udp_err); $B/lidar_rx 41750 $2 $3 > /tmp/rx.out & rp=$!; sleep 0.5
        $B/lidar_tx 127.0.0.1 41750 $1 3 > /tmp/tx.out; wait $rp
        echo "pps=$1 $(cut -d' ' -f2- /tmp/rx.out) RcvbufErrors+=$(( $(udp_err) - e0 ))"; }
sysctl net.core.rmem_default net.core.rmem_max net.core.netdev_max_backlog net.core.busy_read net.core.busy_poll
cat /sys/class/net/*/queues/rx-0/rps_cpus
$B/lidar_rx 41750 1 0 & sleep 0.5; $B/lidar_tx 127.0.0.1 41750 1000 3 100; wait   # 100개마다 1개 버림
run 1000 0 16777216                                  # rmem_max(4 MB)보다 큰 요청
sysctl -w net.core.rmem_max=8388608                 # 게스트 안에서만
```

```text
### guest nproc=2, defaults
net.core.rmem_default = 212992
net.core.rmem_max = 4194304
net.core.netdev_max_backlog = 1000
net.core.busy_read = 0
net.core.busy_poll = 0
0
0
### loss detection check: fake drop every 100th packet
tx: pps=1000 generated=3000 sent=2970 fake_dropped=30 actual=1000 pps over 3.00s
rx: mode=recvmmsg rcvbuf_req=0 eff=212992 got=2970 syscalls=2931 pkts/call=1.0 cpu=511ms az_step=360 gaps=29 est_lost=29 bad=0
### SO_RCVBUF clamp: request 16 MB with default rmem_max (4 MB)
pps=1000 mode=recv rcvbuf_req=16777216 eff=8388608 got=3000 syscalls=3000 pkts/call=1.0 cpu=429ms az_step=360 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
net.core.rmem_max = 8388608
```

출력에서 볼 것:

- **갭 검출**: 30개를 버렸는데 29개를 찾았다. 놓친 1개는 마지막 패킷(k = 2999)이다 — **꼬리 손실은 다음 패킷이 와야 보인다.** 회전 단위로 스캔을 닫는 코드는 "다음 회전의 첫 패킷" 또는 타임아웃으로 마지막 갭을 판단해야 한다.
- `az_step=360`: 1000 pps, 10 Hz → 회전당 100 패킷 → 패킷당 3.60°. 손실이 한 바퀴(36000) 이상 연속되면 azimuth만으로는 몇 바퀴를 잃었는지 모른다. 그때는 패킷 타임스탬프 차이(÷ 패킷 주기)로 판단한다.
- **clamp**: 16 MB를 요청했는데 실효값은 8 MB = 2 × `rmem_max`(4 MB). 커널은 요청을 `rmem_max`로 자르고 bookkeeping 오버헤드 몫으로 2배를 기록한다(`man 7 socket`). 에러는 나지 않는다 — **조용히 잘리므로 반드시 `getsockopt`로 읽어서 로그에 남긴다.** (`SO_RCVBUFFORCE`는 `CAP_NET_ADMIN`으로 상한을 무시한다 — A9.)
- `rps_cpus = 0`: virtio-net 게스트에서 RPS가 꺼져 있다. busy poll도 0(꺼짐)이다.

### 7.6 실행 2 — rate를 올리며 4가지 조합 비교

같은 게스트에서 `rmem_max`를 8 MB로 올린 뒤, 설정 pps마다 {recv, recvmmsg} × {기본 rcvbuf 208 KB, 4 MB 요청 → 실효 8 MB}를 3초씩 돌렸다. 두 번 실행했다.

1차 실행(공유 VM 부하 높음, load average 20 이상, 송신기 실측률 기록 기능 없던 버전)의 rate 10000·20000 발췌:

```text
### rate 10000 pps
pps=10000 mode=recv rcvbuf_req=0 eff=212992 got=29931 syscalls=29931 pkts/call=1.0 az_step=36 gaps=4 est_lost=69 bad=0 RcvbufErrors+=69
pps=10000 mode=recv rcvbuf_req=4194304 eff=8388608 got=30000 syscalls=30000 pkts/call=1.0 az_step=36 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
pps=10000 mode=recvmmsg rcvbuf_req=0 eff=212992 got=30000 syscalls=6979 pkts/call=4.3 az_step=36 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
pps=10000 mode=recvmmsg rcvbuf_req=4194304 eff=8388608 got=30000 syscalls=6324 pkts/call=4.7 az_step=36 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
### rate 20000 pps
pps=20000 mode=recv rcvbuf_req=0 eff=212992 got=59899 syscalls=59899 pkts/call=1.0 az_step=18 gaps=4 est_lost=101 bad=0 RcvbufErrors+=101
pps=20000 mode=recv rcvbuf_req=4194304 eff=8388608 got=60000 syscalls=60000 pkts/call=1.0 az_step=18 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
pps=20000 mode=recvmmsg rcvbuf_req=0 eff=212992 got=60000 syscalls=12032 pkts/call=5.0 az_step=18 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
pps=20000 mode=recvmmsg rcvbuf_req=4194304 eff=8388608 got=60000 syscalls=21329 pkts/call=2.8 az_step=18 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
```

2차 실행(같은 시점 부하가 조금 낮음, 송신기 실측률 포함)의 일부:

```text
### rate 10000 pps
pps=10000 mode=recv rcvbuf_req=0 eff=212992 got=29968 syscalls=29968 pkts/call=1.0 cpu=4132ms az_step=36 gaps=3 est_lost=32 bad=0 RcvbufErrors+=32
pps=10000 mode=recv rcvbuf_req=4194304 eff=8388608 got=30000 syscalls=30000 pkts/call=1.0 cpu=3224ms az_step=36 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
pps=10000 mode=recvmmsg rcvbuf_req=0 eff=212992 got=30000 syscalls=27414 pkts/call=1.1 cpu=3059ms az_step=36 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
pps=10000 mode=recvmmsg rcvbuf_req=4194304 eff=8388608 got=30000 syscalls=27105 pkts/call=1.1 cpu=3079ms az_step=36 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
tx: pps=10000 generated=30000 sent=30000 fake_dropped=0 actual=4775 pps over 6.28s
### rate 40000 pps
pps=40000 mode=recv rcvbuf_req=0 eff=212992 got=120000 syscalls=120000 pkts/call=1.0 cpu=14811ms az_step=9 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
pps=40000 mode=recv rcvbuf_req=4194304 eff=8388608 got=120000 syscalls=120000 pkts/call=1.0 cpu=12552ms az_step=9 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
pps=40000 mode=recvmmsg rcvbuf_req=0 eff=212992 got=120000 syscalls=111310 pkts/call=1.1 cpu=10812ms az_step=9 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
pps=40000 mode=recvmmsg rcvbuf_req=4194304 eff=8388608 got=120000 syscalls=112480 pkts/call=1.1 cpu=10650ms az_step=9 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
tx: pps=40000 generated=120000 sent=120000 fake_dropped=0 actual=6282 pps over 19.10s
### rate 80000 pps
pps=80000 mode=recv rcvbuf_req=0 eff=212992 got=240000 syscalls=240000 pkts/call=1.0 cpu=19146ms az_step=4 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
pps=80000 mode=recvmmsg rcvbuf_req=0 eff=212992 got=240000 syscalls=229524 pkts/call=1.0 cpu=19033ms az_step=4 gaps=0 est_lost=0 bad=0 RcvbufErrors+=0
tx: pps=80000 generated=240000 sent=240000 fake_dropped=0 actual=7204 pps over 33.31s
```

(2차 실행의 20000 pps 줄과 80000 pps의 4 MB 줄은 같은 경향이라 생략했다. `tx:` 줄은 해당 rate의 마지막 조합 실행 것이다.)

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><text x="340" y="22" font-size="14" text-anchor="middle">LiDAR UDP 수신 (게스트 loopback, 1차 실행): 설정 pps별</text><text x="195" y="46" font-size="13" text-anchor="middle">recvmmsg: 호출 1번에 받은 패킷 수</text><line x1="60" y1="260.0" x2="330" y2="260.0" stroke="currentColor" stroke-opacity="0.15"/><text x="54" y="264.0" font-size="12" text-anchor="end">0.0</text><line x1="60" y1="210.0" x2="330" y2="210.0" stroke="currentColor" stroke-opacity="0.15"/><text x="54" y="214.0" font-size="12" text-anchor="end">1.5</text><line x1="60" y1="160.0" x2="330" y2="160.0" stroke="currentColor" stroke-opacity="0.15"/>
<text x="54" y="164.0" font-size="12" text-anchor="end">3.0</text><line x1="60" y1="110.0" x2="330" y2="110.0" stroke="currentColor" stroke-opacity="0.15"/><text x="54" y="114.0" font-size="12" text-anchor="end">4.5</text><line x1="60" y1="60.0" x2="330" y2="60.0" stroke="currentColor" stroke-opacity="0.15"/><text x="54" y="64.0" font-size="12" text-anchor="end">6.0</text><rect x="75" y="166.7" width="40" height="93.3" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="95" y="161.7" font-size="12" text-anchor="middle">2.8</text><text x="95" y="276" font-size="12" text-anchor="middle">2000</text>
<rect x="140" y="136.7" width="40" height="123.3" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="160" y="131.7" font-size="12" text-anchor="middle">3.7</text><text x="160" y="276" font-size="12" text-anchor="middle">5000</text><rect x="205" y="116.7" width="40" height="143.3" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="225" y="111.7" font-size="12" text-anchor="middle">4.3</text><text x="225" y="276" font-size="12" text-anchor="middle">10000</text><rect x="270" y="93.3" width="40" height="166.7" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="290" y="88.3" font-size="12" text-anchor="middle">5.0</text>
<text x="290" y="276" font-size="12" text-anchor="middle">20000</text><line x1="60" y1="260" x2="330" y2="260" stroke="currentColor"/><line x1="60" y1="60" x2="60" y2="260" stroke="currentColor"/><text x="195" y="294" font-size="12" text-anchor="middle">설정 pps (recv는 항상 1.0)</text><text x="515" y="46" font-size="13" text-anchor="middle">recv + 기본 rcvbuf: 잃은 패킷</text><line x1="380" y1="260.0" x2="650" y2="260.0" stroke="currentColor" stroke-opacity="0.15"/><text x="374" y="264.0" font-size="12" text-anchor="end">0</text><line x1="380" y1="210.0" x2="650" y2="210.0" stroke="currentColor" stroke-opacity="0.15"/><text x="374" y="214.0" font-size="12" text-anchor="end">30</text>
<line x1="380" y1="160.0" x2="650" y2="160.0" stroke="currentColor" stroke-opacity="0.15"/><text x="374" y="164.0" font-size="12" text-anchor="end">60</text><line x1="380" y1="110.0" x2="650" y2="110.0" stroke="currentColor" stroke-opacity="0.15"/><text x="374" y="114.0" font-size="12" text-anchor="end">90</text><line x1="380" y1="60.0" x2="650" y2="60.0" stroke="currentColor" stroke-opacity="0.15"/><text x="374" y="64.0" font-size="12" text-anchor="end">120</text><rect x="395" y="260.0" width="40" height="0.0" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/><text x="415" y="255.0" font-size="12" text-anchor="middle">0</text>
<text x="415" y="276" font-size="12" text-anchor="middle">2000</text><rect x="460" y="260.0" width="40" height="0.0" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/><text x="480" y="255.0" font-size="12" text-anchor="middle">0</text><text x="480" y="276" font-size="12" text-anchor="middle">5000</text><rect x="525" y="145.0" width="40" height="115.0" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/><text x="545" y="140.0" font-size="12" text-anchor="middle">69</text><text x="545" y="276" font-size="12" text-anchor="middle">10000</text><rect x="590" y="91.7" width="40" height="168.3" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<text x="610" y="86.7" font-size="12" text-anchor="middle">101</text><text x="610" y="276" font-size="12" text-anchor="middle">20000</text><line x1="380" y1="260" x2="650" y2="260" stroke="currentColor"/><line x1="380" y1="60" x2="380" y2="260" stroke="currentColor"/><text x="515" y="294" font-size="12" text-anchor="middle">설정 pps (나머지 3조합은 0)</text><text x="340" y="318" font-size="12" text-anchor="middle">TCG 게스트 · 부하 높은 공유 VM — 송신기가 설정 pps에 못 미칠 수 있다 (2차 실행 실측 4.7k–7.2k pps)</text></svg>
```
그림 7 — 1차 실행 요약. 왼쪽: recvmmsg가 호출당 받은 패킷 수(부하가 오를수록 커진다). 오른쪽: recv + 기본 버퍼 조합에서만 손실이 났다. TCG 수치다.

출력에서 볼 것:

- **손실은 "recv + 기본 버퍼" 조합에서만** 났고, 손실 수(`est_lost`)는 커널 카운터 `RcvbufErrors` 증가분과 **정확히 같다**(69 = 69, 101 = 101, 32 = 32). 앱의 azimuth 갭 카운터가 맞다는 검증이자, 손실이 소켓 큐 단계(그림 6의 다섯째 칸)에서 났다는 증거다.
- 버퍼를 8 MB로 키우면 recv로도 손실 0. recvmmsg는 기본 버퍼로도 손실 0 — 수신기가 밀렸을 때 한 번에 여러 개(4.3–5.0개)를 퍼내서 큐가 넘치기 전에 비웠다.
- **recvmmsg의 배치 크기는 수신기가 얼마나 밀렸는지를 보여 준다.** 부하가 낮았던 2차 실행에서는 1.1(거의 매번 1개), 부하가 높았던 1차 실행에서는 5.0. `MSG_WAITFORONE`이라 "와 있는 만큼만" 가져오기 때문이다. 이 값은 공짜 헬스 지표다 — 평소 1이던 것이 커지면 수신 스레드가 늦어지고 있다.
- **송신기가 병목이었다.** 2차 실행에서 설정 10000–80000 pps에 실측 4775–7204 pps. TCG에서 `sendto()` 한 번이 비싸서다. 그래서 설정 pps가 올라가도 실제 부하는 거의 같았고, 40000 이상에서 손실 0인 것은 "수신이 강해서"가 아니다. **송신기의 실측률을 함께 찍지 않았다면 잘못된 결론을 냈을 것이다.** 1차 실행 표의 "설정 pps"도 실제 도착률이 아님에 주의.
- loopback이라 그림 6의 위 세 칸(NIC 링, NAPI, RPS)은 이 실험에 없다. 실제 보드에서는 `ethtool -S`와 `/proc/net/softnet_stat`도 같이 본다.

## 8. 커널 안에서는 무슨 일이 일어나나

세 센서의 데이터가 유저의 `read()`/`recv()`까지 오는 커널 함수 경로를 나란히 놓으면 이렇다. 함수 이름은 랩 소스 6.18.55에서 확인했다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><defs><marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="120" y="24" font-size="13" text-anchor="middle" font-weight="bold">IMU</text><rect x="20" y="36" width="200" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="120" y="59" font-size="12" text-anchor="middle">hardirq: store_time (ts)</text><line x1="120" y1="72" x2="120" y2="90" stroke="currentColor" marker-end="url(#ah8)"/>
<rect x="20" y="92" width="200" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="120" y="115" font-size="12" text-anchor="middle">thread: regmap_bulk_read</text><line x1="120" y1="128" x2="120" y2="146" stroke="currentColor" marker-end="url(#ah8)"/><rect x="20" y="148" width="200" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="120" y="171" font-size="12" text-anchor="middle">push_to_buffers_with_ts</text><line x1="120" y1="184" x2="120" y2="202" stroke="currentColor" marker-end="url(#ah8)"/><rect x="20" y="204" width="200" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="120" y="227" font-size="12" text-anchor="middle">store_to_kfifo → wake_up</text><line x1="120" y1="240" x2="120" y2="258" stroke="currentColor" marker-end="url(#ah8)"/><rect x="20" y="260" width="200" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="120" y="283" font-size="12" text-anchor="middle">iio_buffer_read → 유저</text><text x="340" y="24" font-size="13" text-anchor="middle" font-weight="bold">GNSS</text><rect x="240" y="36" width="200" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="340" y="59" font-size="12" text-anchor="middle">uart_insert_char (IRQ)</text>
<line x1="340" y1="72" x2="340" y2="90" stroke="currentColor" marker-end="url(#ah8)"/><rect x="240" y="92" width="200" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="340" y="115" font-size="12" text-anchor="middle">tty_flip_buffer_push</text><line x1="340" y1="128" x2="340" y2="146" stroke="currentColor" marker-end="url(#ah8)"/><rect x="240" y="148" width="200" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="340" y="171" font-size="12" text-anchor="middle">n_tty receive_buf (워크)</text><line x1="340" y1="184" x2="340" y2="202" stroke="currentColor" marker-end="url(#ah8)"/>
<rect x="240" y="204" width="200" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="340" y="227" font-size="12" text-anchor="middle">n_tty_read → 유저</text><line x1="340" y1="240" x2="340" y2="258" stroke="currentColor" marker-end="url(#ah8)"/><rect x="240" y="260" width="200" height="36" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/><text x="340" y="283" font-size="12" text-anchor="middle">gpsd: 파싱 → JSON</text><text x="560" y="24" font-size="13" text-anchor="middle" font-weight="bold">LiDAR</text><rect x="460" y="36" width="200" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="560" y="59" font-size="12" text-anchor="middle">virtio-net NAPI poll</text><line x1="560" y1="72" x2="560" y2="90" stroke="currentColor" marker-end="url(#ah8)"/><rect x="460" y="92" width="200" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="560" y="115" font-size="12" text-anchor="middle">ip_rcv → __udp4_lib_rcv</text><line x1="560" y1="128" x2="560" y2="146" stroke="currentColor" marker-end="url(#ah8)"/><rect x="460" y="148" width="200" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="560" y="171" font-size="12" text-anchor="middle">__udp_queue_rcv_skb</text>
<line x1="560" y1="184" x2="560" y2="202" stroke="currentColor" marker-end="url(#ah8)"/><rect x="460" y="204" width="200" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="560" y="227" font-size="12" text-anchor="middle">__udp_enqueue_schedule_skb</text><line x1="560" y1="240" x2="560" y2="258" stroke="currentColor" marker-end="url(#ah8)"/><rect x="460" y="260" width="200" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/><text x="560" y="283" font-size="12" text-anchor="middle">udp_recvmsg → 유저</text>
<text x="340" y="322" font-size="12" text-anchor="middle">LiDAR 손실 지점: __udp_enqueue_schedule_skb에서 rmem + truesize &gt; sk_rcvbuf → UDP_MIB_RCVBUFERRORS</text></svg>
```
그림 8 — 세 경로의 커널 함수. 모두 "IRQ 문맥에서 최소 일 → 지연 문맥(스레드 IRQ, 워크, softirq)에서 처리 → 큐에 넣고 wait queue 깨움 → 유저 `read` 계열이 `copy_to_user`"라는 같은 뼈대다.

- **IMU**: 하드 IRQ에서 `iio_pollfunc_store_time`이 `pf->timestamp`를 찍고(F6 하드/스레드 IRQ), 드라이버의 트리거 핸들러(스레드 문맥, 그래서 I2C 같은 잠자는 버스 접근이 가능 — F2)가 `regmap_bulk_read`로 데이터를 읽어 `iio_push_to_buffers_with_ts`(인라인, `include/linux/iio/buffer.h`)로 넣는다. `iio_push_to_buffer` → `iio_store_to_kfifo` → `wake_up(&buffer->pollq)`. 유저의 `read(/dev/iio:deviceN)`은 `iio_buffer_read`가 watermark만큼 찰 때까지 기다렸다가 복사한다. 드라이버를 쓸 때 다시 만나는 지점: `iio_trigger_notify_done`을 빠뜨리면 트리거가 멈춘다(I2 5.2절).
- **GNSS**: UART RX IRQ → `uart_insert_char`로 flip buffer에 넣고 → `tty_flip_buffer_push`가 워크를 예약 → 워크가 line discipline(`n_tty`)의 receive_buf를 호출 → gpsd의 `read()`가 `n_tty_read`에서 꺼낸다(H8). GNSS 서브시스템을 쓰면 n_tty 대신 serdev → `gnss_insert_raw` → `/dev/gnss0`의 kfifo로 간다. 이 경로의 지연 지터가 6.4절에서 PPS가 필요한 이유다.
- **LiDAR**: NIC IRQ → NAPI poll(softirq) → `netif_receive_skb` → `ip_rcv` → `__udp4_lib_rcv`가 포트로 소켓을 찾고 → `__udp_queue_rcv_skb` → `__udp_enqueue_schedule_skb`. 여기서 `rmem + skb->truesize > sk->sk_rcvbuf`이면 버리고 `UDP_MIB_RCVBUFERRORS`를 올린다 — 7.6절의 `RcvbufErrors+=69`가 바로 이 줄이다(`net/ipv4/udp.c`, 6.18에서는 NUMA 노드별 `udp_prod_queue`의 메모리까지 합산한다). 유저의 `recvmmsg`는 `udp_recvmsg`를 패킷 수만큼 반복 호출한다 — syscall 진입·탈출(E6)을 한 번만 하는 것이 이득의 전부다.

## 9. Embedded 실무에서 다시 보기

### 9.1 센서 키트 bring-up 체크리스트 (IMU)

1. `i2cdetect -y N`(또는 SPI 로직 분석기)으로 주소가 보이는가. 드라이버가 잡으면 `UU`(H5).
2. `dmesg | grep -i <드라이버>`: `supply ... dummy regulator`, `mounting matrix not found`, `Chip id not found` 같은 경고가 없는가(2.3절 — stub에서 본 바로 그 줄들).
3. `cat in_accel_z_raw` × `in_accel_scale` ≈ +9.8 m/s²(평평한 책상, Z 위). 부호·축이 기체 좌표와 맞는지 → `mount-matrix`.
4. 버퍼 캡처로 **실측 샘플률과 지터**를 잰다(3.3절 도구). 설정값과 비교.
5. 오래 돌리며 갭·중복(같은 값 연속)·타임스탬프 역행을 센다. → Q7.

### 9.2 IMU 데몬의 뼈대 (실행 안 함)

```c
/* poll()로 IIO 버퍼를 기다리고, 배치를 한 번에 읽어 scan마다 처리 */
struct pollfd p = { .fd = open("/dev/iio:device1", O_RDONLY | O_NONBLOCK), .events = POLLIN };
uint8_t buf[64 * 16];                       /* watermark 64 x scan 16 B */
for (;;) {
    if (poll(&p, 1, 1000) == 0) { report_stall(); continue; }   /* 1 s 무소식 = 센서 멈춤 */
    ssize_t n = read(p.fd, buf, sizeof buf);
    for (ssize_t off = 0; off + 16 <= n; off += 16) {
        int16_t ax, ay, az; int64_t ts;
        memcpy(&ax, buf + off, 2); memcpy(&ay, buf + off + 2, 2); memcpy(&az, buf + off + 4, 2);
        memcpy(&ts, buf + off + 8, 8);
        publish(ax * scale, ay * scale, az * scale, ts);          /* scale은 시작 시 sysfs에서 1번 */
    }
}
```

`buffer/watermark`를 64로 두면 `poll`이 64개마다 깨어나 CPU 깨움 횟수가 줄고(I2 8절), 지연은 64 / ODR만큼 는다. 제어 루프가 원하는 지연에서 watermark를 거꾸로 정한다.

### 9.3 GNSS 시각 품질 감시

gpsd가 PPS를 받으면 JSON에 `PPS`/`TOFF` 클래스가 온다. `TOFF`는 "문장이 말한 초 vs 시스템 시계로 그 문장을 받은 시각"의 차이다. 이 값의 평균이 6.4절 chrony `offset`의 근거이고, 분산이 커지면 UART/tty 쪽 지연이 흔들린다는 신호다. `chronyc tracking`의 `System time` 오프셋과 함께 헬스 모니터(Q7)에 넣는다.

### 9.4 LiDAR 수신 노드 설정 순서

1. 앱 시작 시 `SO_RCVBUF`를 "최대 burst × truesize(페이로드의 약 2배)"로 요청하고 `getsockopt`로 실효값을 로그에 남긴다. 잘렸으면 `/etc/sysctl.d/`에서 `rmem_max`를 올린다.
2. 수신 스레드는 `recvmmsg(MSG_WAITFORONE)`로 받아 링버퍼에 넣기만 한다. 회전 조립·좌표 변환은 다른 스레드.
3. 수신 스레드를 `SCHED_FIFO`로 올리고 NIC IRQ와 같은/다른 CPU를 의도적으로 고정한다(N 모듈 RT 노트).
4. 헬스 지표: azimuth 갭 수, `RcvbufErrors` 증가율, recvmmsg 평균 배치 크기, 패킷 타임스탬프 − 도착 시각(PTP로 동기돼 있을 때 네트워크 지연 + 스케줄링 지연).
5. 멀티 LiDAR면 PTP grandmaster 하나로 전부 동기(Q3), 각 센서 포트·IP를 분리.

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `read()`한 시각을 IMU 샘플 시각으로 씀 | 퓨전 결과가 부하에 따라 흔들림 | 유저 스케줄링 지연이 시각에 섞임 | scan의 `in_timestamp` 사용, 클럭은 monotonic |
| FIFO 배치에 IRQ 시각 하나만 붙임 | IMU 적분이 계단처럼 튐, 간격 0과 W×T 반복 | 배치 내 위치를 무시 | 주기 추정 + 거꾸로 채우기 + 앵커 평활(4절) |
| 외부 트리거 주파수 > ODR | 같은 값이 연속, 실제보다 높은 샘플률 | 같은 출력 레지스터를 여러 번 읽음 | 트리거 ≤ ODR, DRDY 트리거, STATUS 확인 |
| 설정 샘플률을 믿음 | 1 kHz 설정인데 하류는 수백 Hz | CPU·버스 포화(3.3절) | 타임스탬프로 실측해 알람 |
| 측정 도구가 설정을 바꿈 | 늘 100 Hz 근처 | `iio_readdev -t`가 트리거를 100 Hz로 씀(3.4절) | 캡처 후 설정 재확인, sysfs로 직접 캡처 |
| NMEA 위도를 소수점 도로 바로 씀 | 위치가 수십 km 틀림 | ddmm.mmmm 형식 | 도 + 분/60 변환 |
| MSL과 타원체 높이 혼동 | 고도가 수십 m 어긋남 | GGA는 MSL, 퓨전은 HAE | `altHAE` 사용, geoid 분리 확인 |
| GNSS tty를 cooked 모드로 | UBX 체크섬 오류, 반쪽 문장 | `ICRNL`, `ECHO` 등 | raw 모드(H8), gpsd에 맡김 |
| NMEA 시각만으로 시계 동기 | 시스템 시계가 ms 단위로 흔들림 | UART·tty 지연 지터 | PPS + chrony `lock` |
| `SO_RCVBUF`만 크게 요청 | burst 때 여전히 손실, 에러는 없음 | `rmem_max`에서 조용히 잘림 | `getsockopt`로 확인, `rmem_max` 조정 |
| 블로킹 `recvmmsg`에 플래그 0 | 패킷이 수십 ms씩 늦게 처리됨 | vlen개 찰 때까지 대기(VM 호스트 smoke test에서 호출당 63.2개) | `MSG_WAITFORONE` |
| 시퀀스 없는 스트림에서 손실 안 셈 | 포인트 클라우드에 부채꼴 구멍 | 갭 검출 없음 | azimuth/타임스탬프 갭 카운터 + `RcvbufErrors` 대조 |

## 11. 면접에서 이렇게 말한다

**Q.** How would you bring up a new IMU on an embedded Linux board?

**A.** 먼저 업스트림에 드라이버가 있는지 `compatible`로 찾고, DT에 버스 주소·INT 핀·레귤레이터·mount-matrix를 넣는다. probe 로그의 dummy regulator·mount matrix 경고를 없애고, `raw × scale`로 중력 9.8을 확인한 뒤, 버퍼 캡처로 실제 샘플률과 지터를 타임스탬프로 잰다. 보드가 오기 전에는 i2c-stub에 레지스터를 채워 드라이버와 유저 스택을 미리 맞춰 둔다.

> First I check whether mainline already has a driver by grepping the compatible string, then describe it in the device tree: bus address, interrupt pin, supplies and mount matrix. I clear the probe warnings like dummy regulator or missing mount matrix, sanity-check raw times scale against gravity, and then run a buffered capture and measure the actual sample rate and jitter from the IIO timestamps rather than trusting the configured rate. Before hardware arrives I can even probe the real driver against i2c-stub with preset registers to validate the DT and the user-space stack.

**꼬리 질문**: "1 kHz가 필요하면 무엇을 보겠나?" → HW FIFO + watermark를 지원하는 드라이버인지(bmi160 업스트림은 FIFO 미사용), 샘플당 버스 트랜잭션 수.

**Q.** The IMU FIFO gives you 32 samples per interrupt. How do you timestamp each sample?

**A.** IRQ 시각 하나를 배치 마지막 샘플의 시각으로 보고, IRQ 간격 ÷ 샘플 수로 실제 칩 주기를 추정해 거꾸로 채운다. IRQ 지연 지터가 앵커를 흔드니 예측값과 측정값을 섞는 PLL처럼 평활한다. 칩이 HW 타임스탬프를 주면(st_lsm6dsx) 그것을 쓰고 칩 시계와 시스템 시계의 관계만 추적한다. 시뮬레이션에서 간격 오차가 57 µs → 1 µs로 줄었다.

> I treat the interrupt timestamp as the time of the last sample in the batch, estimate the real sensor period from the interval between interrupts divided by the sample count, and back-fill. Because interrupt latency jitters the anchor, I smooth it like a PLL: predict the next anchor from the previous one plus W times the period and only nudge it toward the measurement. The InvenSense helper in the kernel does essentially that. If the chip provides hardware timestamps, like the ST LSM6DSx, I use those and just track the chip-to-system clock relationship.

**Q.** What does gpsd do, and why not parse NMEA yourself?

**A.** gpsd는 장치를 하나 열고 NMEA·UBX 등을 자동 판별해 파싱한 뒤 TPV·SKY를 JSON으로 여러 클라이언트에 나눠 주고, 시각을 SHM으로 chrony에 넘긴다. 직접 파싱하면 프로토콜 판별, 수신기 설정, 시계 동기를 다 다시 만들어야 한다. 정밀도가 필요하면 UBX NAV-PVT를 쓰고, 정확도 추정치(hAcc)를 퓨전에 넘긴다.

> gpsd owns the device, sniffs and parses whatever protocol it speaks, NMEA, UBX or RTCM, and republishes a unified model as JSON on port 2947 to any number of clients, plus it feeds time to chrony over shared memory. Writing my own parser means re-implementing protocol detection, receiver configuration and the time path. When precision matters I switch the receiver to UBX NAV-PVT, which carries accuracy estimates I can pass to the fusion filter.

**Q.** How do you discipline the system clock from GNSS?

**A.** NMEA 시각은 UART·tty 지연 때문에 ms 단위로 흔들리므로 초 레이블로만 쓰고, 정확한 초 경계는 PPS 핀을 pps-gpio로 받아 커널이 찍게 한다. chrony에 `refclock SHM 0`(noselect, offset 보정)과 `refclock PPS /dev/pps0 lock GNSS`를 둔다. 센서끼리는 PTP로 그 시계를 퍼뜨린다(Q3).

> NMEA time arrives with millisecond-level jitter from the UART and tty layers, so I use it only to label the second. The PPS edge goes to a GPIO handled by pps-gpio, so the kernel timestamps it in interrupt context. In chrony I configure the NMEA shared-memory source as noselect with a measured offset, and the PPS refclock locked to it. Then PTP distributes that time to the LiDARs and cameras.

**Q.** LiDAR point clouds occasionally have missing wedges. How do you debug it?

**A.** 먼저 앱에서 azimuth(또는 frame/measurement ID) 갭을 세서 손실인지 확인하고, 층별 카운터를 아래에서 위로 본다: `ethtool -S`, `/proc/net/softnet_stat`, `nstat`의 `UdpRcvbufErrors`. 실험에서 앱 갭 수가 `RcvbufErrors` 증가분과 정확히 같아 소켓 큐 단계로 좁혀졌다. 그러면 `SO_RCVBUF`(+`rmem_max`), `recvmmsg`, 수신·처리 스레드 분리, 스케줄링 우선순위 순으로 고친다.

> I first prove it's packet loss by counting azimuth or measurement-ID gaps in the receiver, then walk the counters bottom-up: ethtool -S for ring drops, softnet_stat for backlog drops, UdpRcvbufErrors for socket queue overflow. In my test the app's gap count matched the RcvbufErrors delta exactly, which pins it to the socket queue. The fixes are a bigger SO_RCVBUF with rmem_max raised, recvmmsg, a receive thread that only enqueues, and real-time priority for that thread.

**꼬리 질문**: "recvmmsg가 왜 도움이 되나?" → syscall 진입 비용을 여러 패킷이 나눠 낸다. 그리고 호출당 배치 크기 자체가 "수신기가 얼마나 밀렸나"의 지표다.

**Q.** What is the difference between the kernel GNSS subsystem and just using the tty?

**A.** tty는 유저가 baud·termios·전원을 관리한다. GNSS 서브시스템은 serdev 위에서 DT로 붙고, `/dev/gnss0`을 열 때 레귤레이터를 켜고 닫을 때 끄며, 프로토콜 종류를 `type`으로 알려 준다. 파싱은 둘 다 유저(gpsd) 몫이다.

> With a plain tty, user space owns the baud rate, termios and power control. The GNSS subsystem binds through serdev from the device tree, powers the receiver on open and off on close, and exposes the protocol type, but it still passes raw bytes; parsing stays in gpsd either way. I'd pick it for power-sensitive products.

**Q.** Why does a large SO_RCVBUF request sometimes not help?

**A.** 일반 프로세스의 요청은 `net.core.rmem_max`에서 에러 없이 잘린다(16 MB 요청 → 실효 8 MB = 2 × 4 MB). 그리고 평균 처리율이 도착률보다 낮으면 버퍼는 손실 시점을 늦출 뿐이다.

> The request is silently clamped to net.core.rmem_max and the kernel reports double that for bookkeeping, so you must read it back with getsockopt. And if the average processing rate is below the arrival rate, any buffer only delays the loss; you have to make the receive path faster or decouple processing.

## 12. 직접 해보기

1. 개념: bmi160 업스트림 드라이버로 1.6 kHz × 6채널을 받으면 초당 I2C 트랜잭션은 몇 번인가? 400 kHz I2C에서 한 트랜잭션(주소 + 레지스터 + repeated start + 2바이트)이 약 50비트라면 버스 점유율은?
   정답/힌트: 1600 × 6 = 9600회/s, 9600 × 50 / 400000 ≈ 1.2 → 120%, 불가능. FIFO 지원 칩·드라이버 또는 SPI가 필요하다.
2. 코드: `q5_tsstat`에 "같은 raw 값이 N번 연속" 카운터를 추가하고 bmi160 + hrtimer 1 kHz 캡처에 돌려 보라(stub이라 전부 중복으로 나올 것이다 — 왜 그런지 설명).
   정답/힌트: i2c-stub의 데이터 레지스터는 바뀌지 않는다. 실제 칩이라면 ODR(100 Hz)보다 빠른 트리거에서 약 10개씩 중복된다.
3. 코드: `q5_fifo_ts.c`의 D 방법에서 평활 계수 0.1을 0.01과 0.5로 바꿔 수렴 속도와 간격 오차를 비교하라.
   정답/힌트: 작을수록 지터는 줄고 시계 변화(온도 드리프트) 추종은 느려진다 — PLL 대역폭 트레이드오프.
4. 관찰: `current_timestamp_clock`을 `monotonic`으로 바꾸고 3.3절 캡처를 다시 하라. 첫 scan의 ts가 `/proc/uptime`과 비슷한 크기가 되는가?
   정답/힌트: monotonic은 부팅 후 ns. 1.79e18(realtime) 대신 수천억 ns대.
5. 코드: `q5_gnss_gen.py`에 GST 문장(위치 오차 표준편차)을 추가하고 gpsd TPV의 `eph`가 HDOP 추정에서 GST 값으로 바뀌는지 확인하라.
   정답/힌트: `$GPGST,hhmmss.ss,rms,smaj,smin,orient,lat_err,lon_err,alt_err*HH`. gpsd 3.25는 GST를 `GST` 클래스로도 보고한다.
6. 코드: `q5_lidar_rx.c`에 `SO_TIMESTAMPNS`를 켜고 cmsg로 커널 도착 시각을 받아, 패킷 안의 µs 타임스탬프와의 차이 분포를 출력하라.
   정답/힌트: `setsockopt(SOL_SOCKET, SO_TIMESTAMPNS)`, `msg_control` 버퍼, `CMSG_FIRSTHDR` → `SCM_TIMESTAMPNS` → `struct timespec`.
7. 관찰: 게스트에서 `cat /proc/net/softnet_stat`과 `nstat -az UdpRcvbufErrors`를 LiDAR 실험 전후로 찍어 어느 열이 움직이는지 보라.
   정답/힌트: loopback이라 softnet 2열(dropped)은 거의 안 움직이고 `UdpRcvbufErrors`만 늘어난다.

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| IMU | Inertial Measurement Unit | 가속도계 + 자이로(+ 자력계) |
| ODR | Output Data Rate | 센서가 새 샘플을 만드는 주기 |
| HW FIFO / watermark | 센서 내부 샘플 큐 / 인터럽트 기준 개수 | W개 차면 INT |
| DRDY | Data Ready | 새 샘플 1개가 준비됐다는 인터럽트 |
| mount matrix | 장착 회전 행렬 | 센서 축 → 기체 축 |
| i2c-stub | 가짜 SMBus 칩 | 레지스터 저장만 하는 테스트 어댑터 |
| libiio | IIO 유저 라이브러리 | `iio_info`, `iio_attr`, `iio_readdev` |
| NMEA 0183 | GNSS ASCII 프로토콜 | `$..*HH\r\n`, XOR 체크섬 |
| UBX | u-blox 바이너리 프로토콜 | `B5 62` + class/id/len, Fletcher 체크섬 |
| NAV-PVT | UBX 0x01 0x07 | 시각·위치·속도·정확도 한 메시지 |
| TPV / SKY | gpsd JSON 클래스 | 시각·위치·속도 / 위성·DOP |
| HAE / MSL | 타원체 높이 / 평균 해수면 높이 | 차이 = geoid separation |
| PPS | Pulse Per Second | 매 초 정각 엣지, `/dev/pps0` |
| refclock | chrony 기준 시계 소스 | SHM, SOCK, PPS |
| azimuth | 회전각 | LiDAR block마다 0.01° 단위 |
| `SO_RCVBUF` / `rmem_max` | 소켓 수신 버퍼 / 그 상한 | 실효값은 요청의 2배로 기록 |
| `recvmmsg` | 다중 메시지 수신 syscall | `MSG_WAITFORONE`과 함께 |
| RPS / RSS | 소프트웨어 / 하드웨어 수신 분산 | 프로토콜 처리를 여러 CPU로 |
| busy poll | 앱이 NIC 큐를 직접 폴링 | 지연↓, CPU↑ |
| `RcvbufErrors` | `/proc/net/snmp` Udp 카운터 | 소켓 큐 넘침으로 버린 수 |

## 14. 요약 & 체크리스트

세 센서는 커널 인터페이스가 다르다. IMU는 IIO(드라이버가 파싱, 커널 kfifo, scan 타임스탬프), GNSS는 tty 또는 `/dev/gnss0`의 바이트 스트림을 gpsd가 해석해 JSON과 시각으로 내보내고, LiDAR는 UDP 소켓이다. 어느 쪽이든 실무의 핵심은 같다: **설정값을 믿지 말고 타임스탬프와 카운터로 잰다.** 이 노트에서는 업스트림 bmi160·inv_mpu6050을 i2c-stub에 붙여 단위를 확인했고, 트리거 설정 4000 Hz가 실제 약 350 Hz에 머무는 것과 `iio_readdev -t`가 트리거를 100 Hz로 되돌리는 것을 측정으로 찾았다. FIFO 배치 타임스탬프는 주기 추정 + 앵커 평활로 간격 오차를 1 µs까지 줄였고, gpsd는 NMEA와 UBX를 스스로 판별했으며, LiDAR 수신기의 azimuth 갭 카운트가 커널 `RcvbufErrors`와 정확히 일치했다. TCG 수치 자체는 실제 HW 값이 아니다.

- [ ] bmi160·inv_mpu6050·st_lsm6dsx의 버퍼/트리거/타임스탬프 방식 차이를 설명할 수 있다
- [ ] i2c-stub에 레지스터를 채워 업스트림 IIO 드라이버를 probe하고 `raw × scale`을 확인할 수 있다
- [ ] probe 로그의 dummy regulator·mount matrix 경고가 DT의 무엇을 뜻하는지 말할 수 있다
- [ ] 버퍼 캡처의 실측 샘플률·지터·갭을 scan 타임스탬프로 재는 도구를 짤 수 있다
- [ ] FIFO 배치에 샘플별 타임스탬프를 보간하는 방법(주기 추정 + 앵커 평활)을 코드로 보일 수 있다
- [ ] NMEA와 UBX의 형식·체크섬·정밀도 차이를 설명하고 로그를 생성할 수 있다
- [ ] gpsd JSON 프로토콜(`?WATCH`, TPV, SKY)로 클라이언트를 짤 수 있다
- [ ] tty vs GNSS 서브시스템 선택 기준과 PPS + chrony 설정을 설명할 수 있다
- [ ] LiDAR 수신 경로의 손실 지점과 각 지점의 카운터를 그릴 수 있다
- [ ] 시퀀스 없는 UDP 스트림에서 azimuth 갭으로 손실을 셀 수 있다
- [ ] `SO_RCVBUF` clamp, `recvmmsg(MSG_WAITFORONE)`, busy poll, RPS/IRQ affinity가 각각 무엇을 고치는지 말할 수 있다
- [ ] 측정 도구 자신(송신기 실측률, iio_readdev)을 검증해야 하는 이유를 예로 설명할 수 있다

## 참고 자료

- docs.kernel.org — Industrial I/O: https://docs.kernel.org/driver-api/iio/index.html , IIO configfs: https://docs.kernel.org/iio/iio_configfs.html
- 커널 소스(랩 6.18.55): `drivers/iio/imu/bmi160/`, `drivers/iio/imu/inv_mpu6050/`, `drivers/iio/imu/st_lsm6dsx/`, `drivers/iio/common/inv_sensors/inv_sensors_timestamp.c`, `drivers/i2c/i2c-stub.c`, `Documentation/i2c/i2c-stub.rst`, `drivers/gnss/`, `drivers/pps/`, `net/ipv4/udp.c` — elixir.bootlin.com에서 같은 경로로 볼 수 있다
- DT 바인딩: `Documentation/devicetree/bindings/iio/imu/bosch,bmi160.yaml`, `Documentation/devicetree/bindings/pps/pps-gpio.yaml`
- libiio: https://github.com/analogdevicesinc/libiio
- gpsd: https://gpsd.io/ (gpsd_json(5), gpsfake(1), gpspipe(1) man 페이지)
- chrony: https://chrony-project.org/documentation.html (chrony.conf `refclock`)
- man7.org: socket(7) (`SO_RCVBUF`), udp(7), recvmmsg(2), i2c-dev 관련은 docs.kernel.org `Documentation/i2c/dev-interface.rst`
- docs.kernel.org — Scaling in the Linux Networking Stack (RPS/RFS): https://docs.kernel.org/networking/scaling.html
- 이 사이트: I2(IIO), H2(regmap), H5(I2C), H8(UART·GNSS 서브시스템), A9(UDP 손실), F6(인터럽트), P3, P4, Q3, Q6, Q7
