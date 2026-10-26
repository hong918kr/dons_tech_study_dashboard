# 🛰️ R06 Companion(Jetson/ROS 2) · 영상 · 임베디드 Linux · RTOS 와 테스트

> FC(비행 제어기) 바깥에 있는 네 덩어리 — companion computer, 영상 링크, 임베디드 Linux 이미지, 작은 RTOS 보드 — 와 지상 소프트웨어(ATAK/QGC)가 각각 무엇이고 드론 어디에 있으며, 어떻게 망가지고, FTE가 어떻게 숫자로 검증하는지 정리한다. 핵심은 "한 번 재는 값"이 아니라 **분포 · 회귀 기준선 · 고장 주입**이다.

---

## 0. 이 단원을 읽는 법

- 앞 단원과의 관계: 드론 전체 배치도는 [R02](2026-10-24_R02_drone_system_and_open_source_map.md), FC 안쪽(Betaflight · PX4)은 [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md) · [R04](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md), MAVLink · CRSF 같은 링크 프로토콜은 [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md)에 있다.
- 이 단원은 그 **바깥 층**이다. FC가 "척수"라면 여기서 다루는 건 "눈(카메라 · 영상)", "대뇌(companion)", "운영체제(Linux 이미지)", "주변 장기(BMS · 무전기 · 헤드셋 · payload 보드)", 그리고 "지상의 지휘소(GCS · ATAK)"다.
- 테스트 기법 자체(피라미드, SITL/HITL/HIL)는 [R07](2026-10-24_R07_test_methodology_pyramid.md), 벤치 계측기와 배선은 [R08](2026-10-24_R08_hil_bench_design_instruments.md), 플랫폼 아키텍처는 [R09](2026-10-24_R09_scalable_reusable_test_platform.md)에서 깊게 다룬다. 여기서는 영역마다 "FTE가 무엇을 재고 어떻게 판정하나"까지만 구체적으로 쓴다.
- 표시 규칙: Neros 내부 사실은 공고 문구 외엔 모두 `[추정]`. Gemini가 말한 제품 이름은 `[Gemini 주장 · 미확인]`. 확인 못 한 명령어 · 숫자는 `[확인 필요]`.

---

## 1. 왜 중요한가 — FTE 관점

FW Test Engineer 공고의 업무 첫 줄은 "validate the Neros **drone & ground control software**"다 [확인됨 공고, Firmware Test Engineer]. 즉 테스트 대상은 FC 펌웨어만이 아니라 **지상 장비 소프트웨어와 그 아래 Linux/RTOS까지** 포함한다. 그리고 공고 여러 개가 이 영역을 구체적으로 말한다.

| 공고 (Greenhouse, 2026-10-23 스냅샷) | 이 단원과 관련된 문구 (짧게 인용) | FTE에게 의미 |
|---|---|---|
| Senior Firmware Engineer, Ground Software | "own the latency and reliability of the operator's control path", "telemetry and video receive on the ground side", "behavior when the link degrades", "partnering with our test organization on hardware-in-the-loop and field testing" | 지상 장비 · 링크 열화 동작이 HIL 대상. Test 조직이 파트너로 명시됨 |
| Senior Firmware Engineer, Ground Software (우대) | "Yocto or Buildroot exposure", "Video pipelines on embedded hardware — capture, decode, display, latency" | 지상 장비가 임베디드 Linux + 영상 파이프라인 [추정: 일부 모델] |
| Senior Embedded Linux Engineer | "Own our embedded Linux distribution and board support packages (Yocto/Buildroot)", "secure boot and update/recovery paths", 우대 "PREEMPT_RT, latency analysis", "Mender, RAUC, SWUpdate, OSTree, or homegrown equivalents" | 이미지 · 부트 · OTA · RT 지연이 모두 테스트 대상이 된다 |
| Senior Platform Engineer (Autonomy) | "end-to-end sensor-to-actuator latency measurement under sustained thermal load", "latency to be enforced as a release gate", "flight logs support deterministic replay", "ROS 2, DDS, Zenoh, LCM, or a proprietary equivalent" | **열 부하 하의 지연 분포를 릴리스 게이트로** — FTE가 만드는 자동 판정과 정확히 같은 말 |
| Autonomy Platform & Runtime Lead | "Yocto, Buildroot or containerized build environments, versioned and reproducible artifacts, and over the air or field software update" | 재현 가능한 빌드 + OTA |
| Perception Lead | "NVIDIA Jetson and TensorRT … quantization and characterization of the resulting accuracy and latency trade-offs", "compliance with end-to-end latency budgets" | 추론 정확도 · 지연 회귀 테스트 |
| Firmware Engineer - Tennessee Office | "headsets, handsets", "video conversions, corrections, storage, compression, streaming", 우대 "FreeRTOS, Zephyr", "Gstreamer or ffmpeg video pipeline" | 헤드셋/핸드셋 = FPGA + MCU + Linux 영상 장비. RTOS는 우대 |
| CODEC Video Compression Engineer | "low cost FPV digital video, similar to DJI O4", "low-latency advanced video compression" | 자체 디지털 FPV 영상 개발 중 [추정] → 영상 지연 · 화질 회귀 테스트 수요 |
| Ground Software Manager | "Low-latency video pipeline experience (RTSP/WebRTC/GStreamer or similar)", "ATAK/TAK, QGroundControl, Mission Planner" | GCS 영상 · 상호운용 |
| Senior Software Engineer | "ATAK plugins (Java/Kotlin), WinTAK plugins (C#), TAK Server, Cursor on Target" | CoT 메시지 상호운용 테스트 |
| UAV Systems Engineer (Ukraine) | "Linux, Raspberry Pi, GStreamer, OpenHD" | 현장 쪽은 OpenHD 계열도 씀 [추정] |

정리하면 FTE 입장에서 이 영역은 세 가지 이유로 중요하다.

1. **지연(latency)이 제품 사양이다.** FPV 조종에서 영상 지연은 조종 품질 그 자체다. Platform 공고는 지연을 아예 "release gate"로 만들겠다고 쓴다.
2. **Linux가 끼면 고장 모드가 늘어난다.** 부팅 실패, OTA 중 전원 차단, 열로 인한 클럭 저하, 스케줄링 지터 — MCU 펌웨어에는 없던 종류다.
3. **여러 팀의 경계가 여기 모인다.** FC ↔ companion(MAVLink/DDS), 기체 ↔ 지상(영상 · 텔레메트리), 지상 ↔ 외부 C2(ATAK/CoT). 경계는 테스트하지 않으면 반드시 깨진다.

---

## 2. 전체 그림 — 이 단원의 네 덩어리가 드론 어디에 있나

```text
 ┌──────────────────────────── AIR (기체) ─────────────────────────────────────┐
 │                                                                              │
 │  [Camera]──MIPI CSI/USB──┐                                                   │
 │                          ▼                                                   │
 │  ┌──────────────────────────────────────┐   UART / Ethernet                  │
 │  │ Companion computer (예: Jetson)       │◄──MAVLink 또는 uXRCE-DDS──►[FC]    │
 │  │  Linux (JetPack/L4T 또는 Yocto)       │                          (R03/R04) │
 │  │  ROS 2 nodes · DDS · TensorRT 추론     │                                   │
 │  │  mavlink-router · GStreamer encoder   │                                   │
 │  └──────────────┬───────────────────────┘                                   │
 │                 │ H.264/H.265 RTP/UDP                                        │
 │                 ▼                                                            │
 │  ┌──────────────────────┐        ┌──────────────────────────┐                │
 │  │ Video TX              │        │ 작은 RTOS/bare-metal 보드  │                │
 │  │ analog VTX 또는        │        │ BMS · ESC · payload ·    │                │
 │  │ digital (WFB-ng/자체)  │        │ radio bridge [추정]       │                │
 │  └──────────┬───────────┘        └──────────────────────────┘                │
 └─────────────┼────────────────────────────────────────────────────────────────┘
               │ RF (영상 down, 텔레메트리 양방향)
 ┌─────────────┼──────────────────── GROUND (지상) ────────────────────────────┐
 │             ▼                                                                │
 │  ┌──────────────────────────┐      ┌────────────────────────────────────┐   │
 │  │ Video RX / 헤드셋 · 고글     │      │ Handset / GCS 장비                    │   │
 │  │ FPGA + MCU + Linux [추정]  │──────│ Embedded Linux (Yocto/Buildroot)    │   │
 │  │ decode · display          │      │ 조종 입력 · 영상 표시 · 텔레메트리       │   │
 │  └──────────────────────────┘      └──────────────┬─────────────────────┘   │
 │                                                    │ IP (CoT XML/Protobuf)   │
 │                                                    ▼                         │
 │                                   [ATAK / WinTAK / TAK Server · QGC]          │
 └──────────────────────────────────────────────────────────────────────────────┘
```

- 위 그림의 **Jetson · Yocto · 헤드셋 구조**는 공고 문구에서 추론한 것이다 [추정]. Gemini는 기체 이름(Archer, Archer AI)과 지상 장비 이름(Crossbow GCS)을 들어 구조를 단정했지만 [Gemini 주장 · 미확인], 공개 공고에는 이런 제품 이름이 없다.
- FPV 자폭/전술 드론은 companion 없이 **analog 또는 digital VTX만** 다는 경우가 많다. Companion은 자율 · 인지 기능이 있는 기체에만 붙는다고 보는 게 안전하다 [추정].

---

## 3. Companion computer — Jetson + ROS 2 + DDS/uXRCE-DDS + MAVLink router

### 3.1 무엇인가

Companion computer는 FC 옆에 붙는 **고성능 Linux 컴퓨터**다. FC는 1 kHz~8 kHz 제어 루프를 돌리는 MCU이고, companion은 카메라 영상 처리 · 물체 인식 · 경로 계획 · 고수준 명령을 맡는다. 그리고 결과를 "이 방향 · 이 속도로 가라" 같은 setpoint로 FC에 내려준다.

| 부품 | 무엇 | 드론에서 하는 일 | 테스트에서 보는 것 |
|---|---|---|---|
| NVIDIA Jetson (Orin 계열 등) | GPU + DLA가 붙은 ARM SoM | 카메라 입력 → 신경망 추론 | 추론 지연 · 열 · 전력 · 클럭 |
| JetPack / L4T | NVIDIA가 주는 Ubuntu 기반 BSP | 기본 OS | 커널 버전 · 드라이버 · 부팅 |
| ROS 2 | 노드 · 토픽 · 서비스 미들웨어 | 인지 · 계획 모듈끼리 메시지 교환 | 토픽 주기 · 지연 · QoS · 누락 |
| DDS (Fast DDS, Cyclone DDS 등) | ROS 2 아래 publish/subscribe 전송층 | 프로세스 · 머신 사이 전달 | discovery 시간 · 대역폭 · 재전송 |
| Micro XRCE-DDS / PX4 uXRCE-DDS | MCU용 경량 DDS 클라이언트 + PC 쪽 agent | FC uORB 토픽을 ROS 2 토픽으로 | agent 재연결 · 메시지 정의 일치 |
| MAVLink + mavlink-router | FC 표준 메시지 + 라우터 데몬 | FC ↔ companion ↔ GCS 메시지 분배 | 메시지 rate · heartbeat · 링크 손실 |
| TensorRT | NVIDIA 추론 최적화 런타임 | ONNX 모델 → 엔진(FP16/INT8) | 정확도 회귀 · 지연 분포 |
| GStreamer (+ NVIDIA 플러그인) | 영상 파이프라인 | 카메라 → 인코더 → 네트워크 | 4장 참고 |

### 3.2 PX4 uXRCE-DDS가 실제로 어떻게 붙나 (공식 문서로 확인)

PX4 공식 문서 기준 사실들이다.

- uXRCE-DDS는 PX4 **v1.14**에서 들어왔고, v1.13의 **Fast-RTPS Bridge**를 대체했다.
- 구조: FC 안의 **client**(`uxrce_dds_client`)가 companion의 **agent**(`MicroXRCEAgent`)에 serial 또는 UDP로 붙는다. agent가 ROS 2 DDS 세계에 토픽을 대신 내보낸다.
- 어떤 uORB 토픽을 내보낼지는 PX4 저장소 `src/modules/uxrce_dds_client/dds_topics.yaml`에 정의된다.
- 기본 네임스페이스는 PX4가 내보내는 쪽 `/fmu/out/`, PX4가 받는 쪽 `/fmu/in/`.
- 기본 UDP 포트 8888(`UXRCE_DDS_PRT`), DDS domain 0(`UXRCE_DDS_DOM_ID`).
- ROS 2 쪽은 **같은 메시지 정의(`px4_msgs`)로 빌드해야** 한다. 버전 붙은 메시지는 translation node가 처리하지만, 버전 없는 메시지는 정확히 일치해야 한다 → **메시지 정의 불일치가 대표 회귀 원인**.
- 보안 경고(공식 문서): DDS 트래픽은 인증이 없고 uORB에 바로 닿는다. FC ↔ companion 링크를 격리하라고 쓴다.

```text
# companion (agent) — 시뮬레이터 / UDP
MicroXRCEAgent udp4 -p 8888
# companion (agent) — 직렬 (예: 921600 baud)
sudo MicroXRCEAgent serial --dev /dev/ttyAMA0 -b 921600
# FC 쪽 (NuttX shell) — Ethernet으로 원격 agent에 붙기
uxrce_dds_client start -t udp -p 8888 -h 192.168.0.100 -n drone
```

ArduPilot도 같은 방향으로 왔다. ArduPilot **4.5**부터 `AP_DDS`로 Micro XRCE-DDS 전송을 지원하고 ROS 2 서비스(arm, mode)를 쓸 수 있다. SITL에서는 `sim_vehicle.py ... --enable-dds`, agent는 `ros2 run micro_ros_agent micro_ros_agent udp4 -p 2019` 형태다(4.6 이후 문서 기준) [확인 필요: 버전별 프로파일 인자 차이].

> ⚠️ Gemini 정정: Gemini는 "microDDS"라는 이름을 쓰고, 비교표에서 PX4만 "최강 (MAVLink / microDDS 표준)", ArduPilot은 "최강 (MAVLink 표준)"처럼 DDS를 PX4 전용인 듯 썼다. 정식 이름은 **eProsima Micro XRCE-DDS**이고 PX4 쪽 모듈 이름은 **uXRCE-DDS**다. ArduPilot도 4.5부터 **AP_DDS**로 같은 Micro XRCE-DDS를 지원한다. "DDS = PX4만"은 틀린 말이다.

> ⚠️ Gemini 정정: Gemini 배치도는 Jetson을 "OS: Ubuntu Linux (Yocto)"로 적었다. Jetson 기본 OS는 NVIDIA **JetPack/L4T (Ubuntu 기반)**이고, Yocto로 Jetson 이미지를 만드는 건 커뮤니티 레이어(OE4T `meta-tegra`)를 쓰는 별도 선택이다. Neros가 어느 쪽인지는 모른다 [확인 필요]. 단, Platform/Embedded Linux 공고가 Yocto/Buildroot를 소유한다고 쓰므로 양산 기체는 자체 이미지일 가능성이 있다 [추정].

### 3.3 MAVLink router — FC 하나, 손님 여럿

FC의 MAVLink 포트는 보통 하나다. 그런데 손님은 여럿이다: companion의 자율 노드, 지상 GCS, 로그 기록기, 테스트 PC. 이걸 나눠 주는 게 `mavlink-routerd`다 (mavlink-router 공식 README 확인).

- 설정 파일 기본 위치 `/etc/mavlink-router/main.conf`, 추가 설정 `/etc/mavlink-router/config.d/`.
- 엔드포인트: UART, UDP(normal = 보내는 쪽 / server = 듣는 쪽), TCP. TCP 서버 기본 포트 5760.
- 로그: `Log` 설정 시 ArduPilot은 `.bin`, PX4는 `.ulg`로 FC 로그 저장. `LogTelemetry`(`-T`)는 원시 텔레메트리를 `.tlog`로.
- `SnifferSysID`로 지정한 system ID 쪽에 모든 트래픽을 복사 → **테스트 PC를 sniffer로 붙이면 FC ↔ companion 대화 전체를 녹화**할 수 있다.

```text
mavlink-routerd -e 192.168.7.1:14550 -e 127.0.0.1:14550 /dev/ttyS1:1500000
```

FTE 포인트: 라우터는 "보이지 않는 중간자"다. 라우터가 패킷을 늦게 넘기거나, 버퍼가 차서 버리거나, 재시작 중 heartbeat가 끊기면 FC는 **companion 링크 손실**로 판단할 수 있다. 라우터 자체를 테스트 대상 목록에 넣어야 한다.

### 3.4 Companion 영역의 고장 모드

| 고장 모드 | 어떻게 보이나 | 원인 예 | 잡는 테스트 |
|---|---|---|---|
| 토픽 주기 저하 | `/fmu/out/vehicle_odometry` 가 100 Hz → 60 Hz | CPU 포화, DDS QoS, 직렬 대역폭 부족 | 주기 측정 + 하한 assert (3.5.1) |
| 지연 꼬리(tail) 증가 | p50은 정상, p99가 수십 ms | GC/메모리 할당, 스케줄링, 열 throttling | 지연 분포 히스토그램, p99 기준선 비교 |
| 메시지 정의 불일치 | agent는 붙는데 토픽이 비거나 깨짐 | `px4_msgs` 와 FC 펌웨어 버전 차이 | 버전 조합 매트릭스 테스트 |
| FC ↔ companion 링크 손실 | setpoint 끊김 → FC failsafe | 케이블, 라우터 재시작, companion 커널 패닉 | 링크 끊기 주입 + failsafe 진입 시간 측정 (3.5.4) |
| companion 재부팅 후 복귀 실패 | agent 재연결 안 됨, 토픽 영구 공백 | 세션 상태 · discovery 문제 | 반복 재부팅 루프 테스트 |
| thermal throttling | 비행 10분 뒤 추론 fps 하락 | 냉각 부족, 고온 환경 | 챔버/열 부하 장기 테스트 (3.5.5) |
| 추론 정확도 회귀 | 새 모델 · 새 TensorRT 버전에서 탐지율 하락 | INT8 calibration, 전처리 변경 | 고정 데이터셋 회귀 (3.5.6) |
| 시간 동기 깨짐 | 로그 재생 시 센서 순서 뒤바뀜 | 클럭 도메인 미정렬, PTP 미동작 | 타임스탬프 단조성 · 오프셋 검사 |
| 전원 불안정 | 부팅 중 reset, 과전류 throttling | 배터리 전압 강하 시 SoM 전류 피크 | 전원 공급기로 전압 sag 주입 |

### 3.5 FTE는 어떻게 테스트하나

#### 3.5.1 메시지 주기 · 지연 측정

가장 기본은 ROS 2 CLI다.

```text
ros2 topic hz /fmu/out/vehicle_odometry          # 주기 (평균 · min · max · std)
ros2 topic bw /camera/image_raw                  # 대역폭
ros2 topic delay /detections                     # header.stamp 기준 지연 (메시지에 header가 있을 때)
```

하지만 CLI는 "사람이 보는 값"이다. 릴리스 게이트로 쓰려면 **자동으로 수집하고 판정**해야 한다. 설계 요점:

- 지연은 **두 시각의 차**다. 보내는 쪽 타임스탬프(메시지 header 또는 PX4 `timestamp` 필드)와 받는 쪽 수신 시각. 두 시계가 같은 머신이 아니면 **시간 동기(PTP/gPTP, PPS)**부터 확인해야 의미가 있다. Platform 공고가 "hardware time-stamping and time synchronization"을 따로 적은 이유다.
- 한 번 재지 말고 **수천 번 재서 분포**를 본다. 판정은 평균이 아니라 p99 · p99.9 · max, 그리고 "기준선 대비 몇 % 나빠졌나".
- **부하 조건을 같이 기록**한다: CPU 사용률, GPU 클럭, 온도, 다른 노드 실행 여부. 조건이 없는 지연 숫자는 비교할 수 없다.

#### 3.5.2 ROS 2 `launch_testing` — 노드 묶음을 띄워 놓고 assert

`launch_testing`은 ROS 2 공식 통합 테스트 도구다 (ros2/launch 저장소 README 확인).

- `generate_test_description()` 함수가 테스트할 시스템을 띄우는 `LaunchDescription`을 돌려준다.
- 그 안에 `launch_testing.actions.ReadyToTest()`를 넣어 "이제 테스트 시작해도 된다"를 알린다.
- 일반 `unittest.TestCase` 클래스는 **프로세스가 도는 동안** 실행된다 (active test).
- `@launch_testing.post_shutdown_test()`가 붙은 클래스는 **프로세스가 끝난 뒤** 실행된다 → 종료 코드 · 로그 검사.
- 주요 assert: `proc_output.assertWaitFor(msg, timeout=10)`, `launch_testing.asserts.assertExitCodes(proc_info)`.
- 실행: `launch_test my_test.py`, CMake에서는 `add_launch_test(test/my_test.py)` 로 `colcon test`에 편입.

```python
# test/test_odometry_rate.py — 개념 예시 (실제 노드 이름 · 토픽은 [추정])
import time, unittest
import launch, launch_ros.actions, launch_testing, launch_testing.actions
import rclpy
from nav_msgs.msg import Odometry

def generate_test_description():
    node = launch_ros.actions.Node(package="my_bridge", executable="odom_bridge", name="odom_bridge")
    return launch.LaunchDescription([node, launch_testing.actions.ReadyToTest()]), {"node": node}

class TestRate(unittest.TestCase):
    def test_rate_at_least_90hz(self):
        rclpy.init()
        n = rclpy.create_node("probe")
        stamps = []
        n.create_subscription(Odometry, "/odom", lambda m: stamps.append(time.monotonic()), 10)
        end = time.monotonic() + 5.0
        while time.monotonic() < end:
            rclpy.spin_once(n, timeout_sec=0.01)
        n.destroy_node(); rclpy.shutdown()
        self.assertGreater(len(stamps), 0, "no messages")
        rate = (len(stamps) - 1) / (stamps[-1] - stamps[0])
        self.assertGreaterEqual(rate, 90.0, f"rate={rate:.1f} Hz")

@launch_testing.post_shutdown_test()
class TestShutdown(unittest.TestCase):
    def test_exit_codes(self, proc_info):
        launch_testing.asserts.assertExitCodes(proc_info)
```

이 구조는 SITL과 잘 맞는다. PX4 SITL + agent + 우리 노드를 한 launch에 묶고, "arm 명령 → 3초 안에 `/fmu/out/vehicle_status`의 arming_state 변화" 같은 시나리오를 assert한다 [R04](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md).

#### 3.5.3 rosbag 재생 = 회귀 오라클

rosbag2(`ros2 bag record` / `ros2 bag play`)는 토픽을 그대로 녹화 · 재생한다. Jazzy 계열 기본 저장 형식은 **MCAP**이다 (rosbag2 changelog: "set MCAP as default plugin"). 재생 옵션 `--rate`, `--loop`, `--clock [Hz]`가 있다 [확인 필요: 배포판별 help 출력].

회귀 오라클로 쓰는 방법:

1. 기준이 되는 비행/벤치 데이터를 녹화한다(카메라 · IMU · odometry). 이걸 **golden input**으로 고정 · 버전 관리.
2. 기준 버전 소프트웨어로 재생 → 출력 토픽(예: 탐지 결과, 계획된 setpoint)을 녹화 → **golden output**.
3. 새 버전으로 같은 입력을 재생 → 출력 비교. 완전 일치가 아니라 **허용 오차 규칙**으로 비교(탐지 박스 IoU ≥ 0.9, setpoint 차 ≤ ε, 메시지 수 차 ≤ 1%).
4. 재생 결정성이 깨지는 원인을 먼저 없앤다: `use_sim_time` + `--clock`으로 시간을 bag에 맞추고, 스레드 수 · 난수 seed 고정.

Platform 공고의 "flight logs support **deterministic replay**"가 바로 이것이다. 로그가 재생 가능하려면 **클럭 도메인과 동기 상태가 로그에 같이 기록**되어야 한다는 것도 공고가 직접 적었다.

#### 3.5.4 FC ↔ companion 링크 손실 처리

| 주입 방법 | 무엇을 흉내 | 판정 |
|---|---|---|
| UART TX 선을 릴레이/아날로그 스위치로 끊기 | 케이블 빠짐 | FC가 N ms 안에 failsafe(예: hold/land/RTL) 진입, 재연결 시 복귀 규칙 |
| companion에서 agent · 라우터 프로세스 kill | 소프트웨어 크래시 | heartbeat/offboard 타임아웃 후 failsafe, 재시작 후 자동 재연결 |
| `tc netem`으로 Ethernet 지연 · 손실 주입 | 링크 품질 저하 | 지연 X ms, 손실 Y%에서 setpoint가 오래된 값으로 쓰이지 않는지 |
| companion 전원 차단 | 하드 크래시 | FC는 살아 있어야 함, 로그에 이벤트 기록 |
| 메시지 폭주(rate 10배) | 버그 노드 | FC CPU 부하 · 제어 루프 주기 유지 |

PX4는 offboard 모드에서 setpoint 스트림이 끊기면 failsafe로 간다. 관련 타임아웃 파라미터(`COM_OF_LOSS_T` 등)와 동작(`COM_OBL_RC_ACT` 등)이 있다 [확인 필요: 현재 버전 파라미터 이름 · 기본값]. FTE는 이 파라미터를 테스트 조건으로 명시하고, **"끊긴 시각 → FC 모드 변경 시각"**을 로직 분석기 GPIO + MAVLink 로그로 잰다.

```text
# companion Ethernet에 지연 50 ms ± 20 ms, 손실 5% 주입 (Linux)
sudo tc qdisc add dev eth0 root netem delay 50ms 20ms loss 5%
# 원복
sudo tc qdisc del dev eth0 root
```

#### 3.5.5 CPU/GPU 열 · throttling

Jetson은 온도가 오르면 클럭을 낮춘다. NVIDIA 포럼 답변 기준으로 throttling은 짧게 일어날 수 있어 기본 1초 간격 `tegrastats`로는 놓칠 수 있다. 간격을 줄이고 파일로 남긴다.

```text
sudo tegrastats --interval 500 --logfile /var/log/tegrastats.log &
```

- 볼 필드: 온도(`CPU@`, `GPU@` 등)와 **클럭(GR3D_FREQ, CPU 주파수)**. 온도보다 클럭 하락이 throttling의 더 확실한 신호다.
- Jetson Nano 사례에서 "over-current throttling" 메시지는 실제로 **입력 전압 부족**인 경우가 많다고 NVIDIA 직원이 답했다 → 열 문제로 단정하기 전에 전원부터.
- 시험 설계: **열 챔버(또는 히트건/밀폐 박스) + 고정 부하(추론 루프) + 30~60분** 동안 추론 지연 p99, fps, 클럭, 온도를 같은 시간축에 기록. "시작 5분 vs 마지막 5분"의 p99 비교를 판정 기준으로.
- Platform 공고가 "under **sustained thermal load**"라고 쓴 게 이 시험이다.

#### 3.5.6 TensorRT 추론 — 지연 · 정확도 회귀

| 축 | 측정 | 도구 | 판정 예 |
|---|---|---|---|
| 지연 | 엔진 단독 GPU 시간, H2D/D2H 포함 시간 | `trtexec --loadEngine=model.engine` (Latency min/mean/median/percentile 출력) | p99 ≤ 기준선 × 1.05 |
| 처리량 | fps | trtexec, 실제 파이프라인 fps | 목표 fps 이상 유지 |
| 정확도 | mAP, recall@IoU, 오탐률 | 고정 평가 데이터셋 + 평가 스크립트 | FP32 대비 INT8 mAP 하락 ≤ 1%p |
| 결정성 | 같은 입력 같은 출력? | 출력 텐서 해시/허용 오차 비교 | 허용 오차 내 |
| 종단 지연 | 카메라 프레임 → 탐지 결과 토픽 | 타임스탬프 체인, 4장 방법 | 지연 예산 내 |

- 데이터셋은 **고정 · 버전 관리**한다. 새 사진을 계속 추가하면 숫자가 왜 변했는지 알 수 없다. 추가는 "새 데이터셋 버전"으로.
- TensorRT 엔진은 **GPU 아키텍처 · TensorRT 버전에 묶인다**. JetPack을 올리면 엔진을 다시 만들어야 하고, 그때 정확도 · 지연이 변할 수 있다 → JetPack 업데이트 자체가 회귀 테스트 트리거.
- trtexec의 반복 · 평균 옵션(`--iterations`, `--avgRuns`, `--duration`, `--percentile`)은 버전마다 다르다 [확인 필요: 설치된 TensorRT의 `trtexec --help`].
- Perception 공고 문구 "characterization of the resulting accuracy and latency trade-offs"가 이 표 전체다.

---

## 4. 영상 파이프라인 — analog/digital FPV, GStreamer, OpenHD/WFB-ng, RTSP/WebRTC

### 4.1 analog vs digital FPV

| 항목 | Analog FPV | Digital FPV (DJI 계열, WFB-ng/OpenHD, 자체) |
|---|---|---|
| 신호 | 카메라 → 아날로그 비디오(NTSC/PAL) → FM 변조 VTX | 카메라 → 인코더(H.264/H.265) → 패킷 → 무선 |
| 지연 | 매우 낮고 일정(인코딩 · 버퍼 없음) | 인코드 · 패킷화 · FEC · 디코드 · 디스플레이 버퍼만큼 추가 |
| 신호 약해질 때 | 점점 노이즈가 늘지만 화면은 계속 나옴 | 어느 지점까지 깨끗하다가 **블록 깨짐 · 정지 · 끊김** (cliff effect) |
| 화질 | 낮음 | 높음 |
| 녹화 · 재전송 | 어려움 | 쉬움 (패킷이므로 지상 네트워크로 재배포) |
| 테스트 포인트 | 지연은 작고 고정, 주로 RF 세기 · 간섭 | 지연 분포 · 프레임 드롭 · 비트레이트 적응 · 복구 시간 |

CODEC 공고가 "low cost FPV digital video, similar to DJI O4"를 쓰므로 Neros는 **자체 디지털 영상**을 만들고 있을 가능성이 있다 [추정]. 그러면 FTE는 4.5~4.7의 지연 · 드롭 · 열화 시험을 릴리스마다 돌려야 한다.

### 4.2 GStreamer — 블록을 이어 붙이는 영상 프레임워크

GStreamer는 element(블록)를 `!`로 이어 파이프라인을 만든다. src → 변환 → 인코더 → payloader → 네트워크 sink, 받는 쪽은 그 반대.

```text
# 송신 (테스트 패턴 → H.264 → RTP/UDP)
gst-launch-1.0 videotestsrc is-live=true pattern=ball ! video/x-raw,width=1280,height=720,framerate=60/1 \
  ! timeoverlay ! x264enc tune=zerolatency speed-preset=ultrafast bitrate=4000 key-int-max=30 \
  ! rtph264pay config-interval=1 pt=96 ! udpsink host=127.0.0.1 port=5600

# 수신
gst-launch-1.0 udpsrc port=5600 caps="application/x-rtp,media=video,encoding-name=H264,payload=96" \
  ! rtpjitterbuffer latency=20 ! rtph264depay ! avdec_h264 ! videoconvert ! autovideosink sync=false
```

- Jetson에서는 소프트웨어 `x264enc` 대신 하드웨어 인코더 플러그인(`nvv4l2h264enc` 등)을 쓴다 [확인 필요: JetPack 버전별 element 이름].
- **지연을 키우는 숨은 기본값**이 많다. 예: `rtspsrc`의 `latency` 속성 기본값은 **2000 ms**(GStreamer 공식 문서 확인). 지터 버퍼 크기, 인코더 B-frame, `sync=true` 디스플레이 대기, `queue` 크기가 모두 지연이다. FTE가 파이프라인 문자열을 **설정 파일처럼 버전 관리 · diff**해야 하는 이유.
- 포트 5600은 PX4/QGC 영상 예제에서 흔히 쓰는 UDP 포트다 [확인 필요: QGC 기본값].

#### GStreamer 안쪽 지연 측정 도구

| 도구 | 무엇 | 사용 |
|---|---|---|
| 내장 `latency` tracer | source에서 sink까지 파이프라인 지연, element별 지연, element가 보고한 지연 | `GST_DEBUG="GST_TRACER:7" GST_TRACERS="latency(flags=pipeline+element+reported)" gst-launch-1.0 ...` |
| 내장 `stats` · `rusage` · `leaks` tracer | 버퍼/이벤트 통계, CPU 사용, 객체 누수 | `GST_TRACERS="stats;rusage"` (여러 개는 `;`) |
| GstShark (RidgeRun) | tracer 묶음: InterLatency, ProcTime, Framerate, ScheduleTime, CPUUsage, Graphic, Bitrate, Queue Level, Buffer + 그래프 스크립트 | `GST_TRACERS="interlatency;proctime;framerate"` 형태 [확인 필요: 정확한 tracer 문자열] |
| `GST_DEBUG_DUMP_DOT_DIR` | 실제 협상된 파이프라인 그래프(.dot) 덤프 | caps 협상 결과를 회귀 아티팩트로 저장 |

주의: tracer는 **파이프라인 안쪽** 지연만 잰다. 카메라 센서 노출 · 무선 구간 · 디스플레이 스캔아웃은 못 본다. 그래서 4.5의 **glass-to-glass** 측정이 따로 필요하다.

### 4.3 OpenHD · WFB-ng — Wi-Fi 칩으로 만든 단방향 방송 링크

WFB-ng 공식 README에서 확인한 것:

- Wi-Fi 카드를 **monitor mode**로 두고 패킷을 **주입(injection)**한다. 접속(association) · ACK 절차가 없다 → 재전송 대기가 없어 지연이 작다.
- RTP 패킷을 802.11 프레임에 **1:1**로 실어 보낸다(UDP 패킷 하나 = 무선 패킷 하나, 크기 가변).
- **FEC**: 기본 설정은 12개 블록 중 4개 손실까지 복구 (즉 k=8, n=12로 읽힌다). FEC 파라미터는 동작 중 바꿀 수 있다.
- 지원 칩: **RTL8812AU · RTL8812EU (패치된 드라이버 필요)**, 일부 Atheros 칩(README는 TX 전력 제어에 커널 패치 필요라고 씀) [확인 필요: 정확한 모델명]. 아무 Wi-Fi 칩이나 되는 게 아니다.
- libsodium으로 암호화 · 인증, `wfb_keygen`으로 키 쌍.
- MAVLink 양방향, IP 터널(고대역 영상에는 비권장), TX 다이버시티(RSSI 기반 송신 카드 선택).
- 링크 상태 보기: `wfb-cli gs`, 로그 `journalctl -xu wifibroadcast@gs`.
- 숫자 지연 주장은 README에 없다.

OpenHD는 이런 broadcast 방식 위에 영상 · 텔레메트리 · RC를 묶은 소프트웨어 모음이고, 지상 앱은 QOpenHD다(openhdfpv.org). OpenHD가 내부적으로 WFB-ng 코드를 얼마나 쓰는지는 버전마다 다르다 [확인 필요].

> ⚠️ Gemini 정정: Gemini는 OpenHD/WFB-ng를 "일반 Wi-Fi 칩의 모니터 모드를 해킹"한다고 썼다. 실제로는 **monitor mode + 패킷 injection을 지원하는 특정 칩셋과 패치된 드라이버**가 필요하다(WFB-ng README의 RTL8812AU/EU 등). 아무 노트북 Wi-Fi로는 안 된다. 또 "재전송 지연 없이"는 맞지만 그 대가로 **FEC가 복구 못 하는 손실은 그대로 화면 깨짐**이 된다 — 테스트할 지점이다.

### 4.4 RTSP · WebRTC — 지상 네트워크로 다시 나눠 줄 때

| 프로토콜 | 무엇 | 지연 특성 | 테스트 포인트 |
|---|---|---|---|
| RTP/UDP | 미디어 패킷 자체 | 가장 낮음, 손실 복구 없음 | 패킷 손실 · 순서 뒤바뀜 · 지터 버퍼 |
| RTSP | 세션 제어(PLAY/PAUSE) + 보통 RTP로 미디어 | 클라이언트 지터 버퍼 설정에 크게 좌우(`rtspsrc latency` 기본 2000 ms) | 연결 · 재연결, TCP 인터리브 시 지연 증가 |
| WebRTC | 브라우저 표준 실시간 미디어 (ICE/DTLS-SRTP, 혼잡 제어) | 낮음, 혼잡 시 비트레이트 자동 조절 | NAT · 방화벽, 비트레이트 적응, 브라우저 호환 |
| MPEG-TS over UDP/SRT | 방송 계열 | 설정에 따라 다름 | 컨테이너 호환 (CODEC 공고 "MPEG-TS") |

> ⚠️ Gemini 정정: Gemini는 "RTSP/WebRTC: 지상 장비 네트워크에서 쓰는 표준 저지연 프로토콜"로 묶었다. RTSP는 **세션 제어 프로토콜**이고, 미디어는 RTP가 나른다. 지연은 프로토콜보다 **수신 쪽 지터 버퍼 · 디코더 · 디스플레이 설정**이 좌우하며, GStreamer `rtspsrc` 기본 지터 버퍼는 2초다. "RTSP = 저지연"은 설정을 바꿔야만 성립한다.

### 4.5 Glass-to-glass 지연 — 어떻게 재나

"Glass-to-glass"는 **카메라 렌즈 유리 → 화면 유리**까지 전체 지연이다. 파이프라인 tracer로는 못 잰다. 방법은 크게 네 가지.

| 방법 | 장비 | 원리 | 해상도 | 장점 | 단점 |
|---|---|---|---|---|---|
| A. 스톱워치/타이머 촬영 | 고주사율 화면에 ms 타이머, 일반 카메라/폰 | 실제 타이머와 영상 속 타이머를 **한 사진에** 같이 찍고 차이 읽기 | 화면 주사율에 묶임(60 Hz면 ~16.7 ms 단위) | 공짜, 빠름 | 수동, 반복 어려움, 디스플레이 주사 위치 오차 |
| B. LED + 고속 카메라 | LED(GPIO로 깜빡), 240~1000 fps 카메라 | LED 실물과 화면 속 LED를 같은 고속 영상에 찍어 프레임 수 차이 | 고속 카메라 프레임 간격(240 fps ≈ 4.2 ms) | 직관적, 조종 입력까지 같이 찍을 수 있음 | 수동 분석, 샘플 수 적음 |
| C. LED + 포토다이오드 + 스코프 | GPIO LED, 화면에 붙인 포토다이오드, DSO 또는 로직 분석기 | **CH1 = LED 구동 신호, CH2 = 화면 밝기 변화**, 두 엣지 간 시간 | µs 단위 (스코프 기준) | **자동화 가능, 수천 번 반복 → 분포** | 화면 내 포토다이오드 위치에 따라 스캔아웃 시간 포함/제외가 달라짐 |
| D. 타임스탬프 오버레이 | 송신 측 `timeoverlay`/`clockoverlay` 또는 프레임 ID 바코드, 수신 측 캡처 + OCR/디코드 | 영상에 박힌 송신 시각 vs 수신 · 표시 시각 | 동기된 시계 기준 ms | 무인 장시간, 프레임별 지연 → 드롭 · 지터도 같이 | 카메라 센서 · 디스플레이 구간 제외(파이프라인 일부만), 시계 동기 필요 |

FTE가 **회귀 게이트로 쓰기 좋은 건 C**다. 이유는 R08과 같다 — 스코프 측정은 사람 없이 반복되고, 결과가 숫자 분포로 나온다.

```text
   [Test PC]─USB─[MCU/GPIO 보드]──GPIO──►[LED]  ← 카메라가 보는 위치
       │                    │                │
       │                    └──CH1──►[DSO / 로직 분석기]◄──CH2──[포토다이오드+비교기]
       │                                                          ▲ 화면/고글에 붙임
       └── 결과 수집: (CH2 엣지 − CH1 엣지) × N회 → 히스토그램 · p50/p99/max
```

측정 주의점:

- **디스플레이 스캔아웃**: 화면은 위에서 아래로 그린다. 포토다이오드를 화면 위쪽에 붙이면 아래쪽보다 짧게 나온다. 위치를 고정 · 기록한다.
- **카메라 노출 · 프레임 주기**: LED가 켜지는 순간이 카메라 프레임 노출의 어디인지에 따라 0~1 프레임(60 fps면 0~16.7 ms) 무작위 지연이 추가된다. 그래서 **분포가 넓게 나오는 게 정상**이고, LED 토글 시점을 일부러 랜덤화해야 편향이 없다.
- **인코더 GOP**: I-frame 직후와 P-frame 중간의 지연이 다를 수 있다.
- **RF 조건**: 감쇠기 0 dB / 링크 마진 근처 두 조건을 따로 잰다. 4.6 참고.
- K05 노트의 latency 파이프라인(stick → … → motor)과 합치면 **조종 입력부터 "조종사가 결과를 보는 순간"까지**의 전체 루프가 된다. Ground Software 공고 "latency … of the operator's control path"가 이 전체다.

### 4.6 프레임 드롭 · 지터 · RF 열화 하의 비트레이트

| 지표 | 정의 | 어떻게 얻나 |
|---|---|---|
| 프레임 드롭률 | 보낸 프레임 ID 중 표시되지 않은 비율 | 송신에 프레임 번호(바코드/오버레이) → 수신 캡처에서 누락 번호 카운트 |
| 프레임 간격 지터 | 표시된 프레임 사이 간격의 표준편차, p99 | 수신 타임스탬프 차이 분포 (60 fps면 이상값 16.7 ms) |
| 정지(freeze) 이벤트 | 간격 > T (예: 100 ms)인 구간 수 · 총 시간 | 위와 같음, 임계값 규칙 |
| 복구 시간 | 링크 끊김 해제 → 첫 정상 프레임까지 | RF 감쇠기로 끊었다 복구, I-frame 요청 · GOP 길이에 좌우 |
| 실효 비트레이트 | 시간당 수신 페이로드 | GstShark Bitrate, `wfb-cli` 통계 [확인 필요: 필드] |
| 패킷 손실 · FEC 복구율 | FEC 전/후 손실 | WFB-ng 통계 · 로그 [확인 필요: 필드] |
| 화질 | PSNR · SSIM (기준 영상 대비) | 같은 테스트 패턴을 송신, 수신 녹화와 비교 (ffmpeg `psnr`/`ssim` 필터) |

RF 열화 시험 설계 (R08 감쇠기 벤치 재사용):

1. TX · RX 안테나 포트를 **동축 케이블 + 프로그래머블 감쇠기**로 연결(전도 시험). 실제 안테나로 방사하지 않는다.
2. 감쇠를 0 dB에서 단계적으로 올리며 각 단계에서 60초씩: 드롭률, 지터, 비트레이트, PSNR, glass-to-glass p99 기록.
3. 결과는 **"감쇠(dB) vs 지표" 곡선**. 새 펌웨어가 이 곡선을 왼쪽으로 밀면(같은 감쇠에서 더 나쁨) 회귀.
4. 끊김 → 복구 시험: 감쇠를 링크 끊김 수준으로 올렸다가 내리고, 복구 시간 분포.
5. 간섭 시험(재밍 흉내)은 신호 발생기가 필요하고 **전파법 · 차폐 환경** 문제가 있다 — 사기/하기 전에 Don과 예산 · 환경 확인.

### 4.7 영상 영역 고장 모드 요약

| 고장 | 증상 | 잡는 시험 |
|---|---|---|
| 지터 버퍼 기본값 회귀 | 지연이 갑자기 +수백 ms | 파이프라인 설정 diff + glass-to-glass 게이트 |
| 인코더 설정 변경 (B-frame, GOP) | 지연 증가 또는 복구 느려짐 | 복구 시간 시험 |
| caps 협상 변경 | 해상도/포맷 바뀜, CPU 변환 추가 | `.dot` 그래프 diff |
| 하드웨어 인코더 드라이버 회귀 (JetPack 업데이트) | fps 하락, 크래시 | 장시간 soak + fps 하한 |
| 메모리 누수 | 수 시간 후 OOM | `leaks` tracer, RSS 추세 |
| 열로 인한 인코더 클럭 하락 | 장시간 후 드롭 증가 | 열 부하 soak (3.5.5) |
| RF 열화 시 cliff 위치 이동 | 같은 거리에서 먼저 끊김 | 감쇠 곡선 비교 |
| 지상 재배포(RTSP/WebRTC) 연결 실패 | ATAK/QGC 영상 안 뜸 | 클라이언트 연결 자동화 시험 (7장) |

---

## 5. 임베디드 Linux — Yocto · Buildroot · U-Boot · OTA · PREEMPT_RT

### 5.1 무엇이고 어디에 있나

임베디드 Linux는 companion(Jetson 등), 지상 장비(handset/GCS), 영상 수신 장비, 무전기 보드에 들어가는 **제품 전용 Linux 이미지**다. Embedded Linux 공고는 이 이미지를 "the distribution, kernel, bootloader, and SDK that our flight, ground-station, and autonomy software teams build on"이라고 쓴다 [확인됨 공고]. 즉 여러 제품의 공통 바닥이다. 바닥이 흔들리면 위의 모든 팀이 흔들린다 → FTE에게는 **가장 레버리지가 큰 회귀 대상** 중 하나.

Don은 EmbeddedLinux 자습 노트(모듈 A–S)에서 커널 · 디바이스 트리 · Buildroot · QEMU 랩을 이미 다뤘다. 이 장은 그 지식을 "**어떻게 자동으로 검증하나**" 쪽으로 돌린다.

```text
 전원 ON
   │
   ▼
 [Boot ROM] ──► [SPL / 1차 부트로더] ──► [U-Boot] ──► [Kernel + DTB] ──► [initramfs?] ──► [rootfs: systemd/busybox init] ──► [앱 서비스]
   │ (SoC 내장)     │ DRAM 초기화         │ 슬롯 선택(A/B)     │ 드라이버 probe     │                    │ mark-good (OTA)
   │                │                     │ bootcount          │                    │                    │
   └─ secure boot: 각 단계가 다음 단계 서명을 검증 (chain of trust) ──────────────────────────────────────────┘

 측정 지점:  T0 전원 ── T1 U-Boot 배너 ── T2 "Starting kernel" ── T3 init 시작 ── T4 서비스 ready (예: 영상 첫 프레임)
```

### 5.2 Yocto vs Buildroot — 테스트 관점 차이

| 항목 | Yocto (OpenEmbedded) | Buildroot |
|---|---|---|
| 단위 | 레이어 · 레시피 · 패키지 피드 | 하나의 `.config` → 하나의 rootfs 이미지 |
| 규모 | 크고 유연, 학습 곡선 큼 | 작고 단순 |
| 재현성 | `reproducible_build` 기본 지향, sstate 캐시, 버전 고정은 레이어 커밋 고정 | `BR2_REPRODUCIBLE` 옵션 [확인 필요: 현재 상태], defconfig + 다운로드 해시 |
| 내장 테스트 | **oeqa**: `testimage`(부팅된 이미지에 런타임 테스트), `testsdk`, `selftest`, **ptest**(패키지별 upstream 테스트를 타깃에서 실행) | **`support/testing/run-tests`**: 정해진 defconfig를 빌드 → (선택) 출력 검사 → (선택) QEMU 부팅 후 기능 검사 |
| 하드웨어에서 테스트 | `TEST_TARGET` = `simpleremote`(이미 부팅된 보드에 SSH) 또는 사용자 정의 controller | run-tests는 QEMU 중심, 하드웨어는 별도(LAVA 등) |
| FTE 포인트 | 레이어/레시피 변경 → 어떤 이미지가 영향받는지 추적 | defconfig diff가 곧 변경 범위 |

Neros 공고는 둘 다 "Yocto/Buildroot"로 나란히 쓴다. 제품마다 다를 수 있다 [추정].

### 5.3 이미지 빌드 재현성 — 같은 입력이면 같은 비트

재현성은 "같은 소스 + 같은 설정이면 같은 해시의 이미지"다. 방산 제품에서는 **현장에 나간 이미지를 나중에 똑같이 다시 만들 수 있어야** 버그를 재현하고 보안 감사에 답할 수 있다. Autonomy Platform 공고의 "versioned and reproducible artifacts"가 이것이다.

테스트 방법:

1. 같은 커밋을 **서로 다른 빌드 머신 · 다른 시각 · 다른 경로**에서 두 번 빌드.
2. 최종 이미지와 패키지별 해시 비교. 다르면 `diffoscope`로 어느 파일 · 어느 바이트가 다른지 찾는다.
3. 흔한 원인: 빌드 시각이 박힘(`SOURCE_DATE_EPOCH` 미설정), 빌드 경로가 디버그 정보에 박힘, 파일 순서(tar 정렬), 호스트 도구 버전 차이, 네트워크에서 "latest"를 받음.
4. CI에 "주 1회 이중 빌드 + 비교" 잡을 두면 재현성 회귀를 조기에 잡는다 [R09](2026-10-24_R09_scalable_reusable_test_platform.md).

Yocto는 공식적으로 재현 가능한 빌드를 목표로 `oe-selftest -r reproducible` 테스트를 갖고 있다 [확인 필요: 현재 릴리스의 정확한 이름]. 무거운 빌드라 Don의 방침대로 **가이드만** 둔다.

### 5.4 부팅 시간 측정

현장에서 "전원 넣고 영상이 나올 때까지"는 운용자가 직접 느끼는 숫자다. 회귀가 잘 생긴다(새 서비스 추가, 드라이버 probe 지연, fsck).

| 구간 | 측정 방법 |
|---|---|
| 전원 → U-Boot 배너 | 전원 공급기 출력 ON 신호(또는 전류 상승)와 UART 첫 바이트를 스코프/로직 분석기로 |
| U-Boot → kernel 시작 | UART 로그에 테스트 PC 수신 타임스탬프 (`grabserial` 류 또는 pyserial로 줄마다 시각) |
| kernel 내부 | `printk.time=1` 커널 로그 시각, `initcall_debug`로 느린 드라이버 찾기 |
| init → 서비스 ready | `systemd-analyze`, `systemd-analyze blame`, `systemd-analyze critical-chain` |
| 서비스 → "제품 ready" | 앱이 ready 순간 GPIO 토글 또는 로그 마커 → 영상이면 첫 프레임 시각 |

판정: 각 구간 p95가 기준선 대비 +X% 이내. **구간을 나눠 기록**해야 "어디서 늘었는지"가 바로 보인다. SSD에서 power-on → ready 시간을 단계별로 나눠 보던 것과 같은 사고방식이다(9장).

```python
# 부팅 로그 타임스탬프 수집 — 개념 (pyserial 필요, 표준 라이브러리 아님)
import serial, time
MARKERS = [b"U-Boot", b"Starting kernel", b"Run /sbin/init", b"APP_READY"]
def capture(port="/dev/ttyUSB0", baud=115200, timeout_s=120):
    s = serial.Serial(port, baud, timeout=0.1)
    t0 = time.monotonic(); seen = {}
    buf = b""
    while time.monotonic() - t0 < timeout_s and len(seen) < len(MARKERS):
        buf += s.read(4096)
        for m in MARKERS:
            if m not in seen and m in buf:
                seen[m] = time.monotonic() - t0
    return seen   # t0 = 전원 ON 명령 직후로 맞출 것 (전원 공급기 SCPI와 같은 스크립트에서)
```

### 5.5 ptest · oeqa · Buildroot run-tests

Yocto 공식 Test Environment Manual에서 확인한 것:

- 런타임 이미지 테스트는 `testimage` 클래스. `local.conf`에 `IMAGE_CLASSES += "testimage"`를 넣고 `bitbake <image> -c testimage`.
- 어떤 테스트를 돌릴지는 `TEST_SUITES` (예: `ping ssh df systemd ...`). 순서가 의존성을 만든다.
- 기본 타깃은 QEMU(`TEST_TARGET` 기본값 `"qemu"`). 하드웨어는 `simpleremote`(이미 부팅된 보드, `TEST_TARGET_IP` 필요) 또는 레이어의 `lib/oeqa/controllers/`에 **사용자 정의 controller**를 만든다 → 전원 · 플래시 · 부팅을 우리 벤치 코드로 제어하는 연결점.
- `testexport`: 테스트를 빌드 머신 밖으로 내보내 별도 테스트 서버에서 실행 (`TEST_SERVER_IP`, `TEST_TARGET_IP`).
- **ptest**: 패키지가 원래 가진 upstream 테스트를 타깃에서 `ptest-runner`로 실행. 보통 `DISTRO_FEATURES`에 `ptest`, 이미지에 `ptest-pkgs` [확인 필요: 사용 중 릴리스의 변수 이름].

Buildroot 공식 매뉴얼("Using the runtime tests framework"):

- `support/testing/run-tests -l` 로 목록, `support/testing/run-tests -d dl -o output_folder -k tests.init.test_busybox.TestInitSystemBusyboxRw` 로 하나 실행.
- 각 테스트는 정해진 설정을 빌드하고, 선택적으로 출력물을 검사하고, **QEMU로 부팅해 기능을 확인**한다. `-k`가 없으면 출력 폴더를 지운다.
- 우리 제품용 테스트를 같은 Python 구조로 추가할 수 있다 → "이 defconfig로 빌드하면 영상 서비스가 부팅 후 10초 안에 뜬다" 같은 테스트.

### 5.6 LAVA 보드 팜 — 실제 보드 수십 대에 자동 배포 · 부팅 · 테스트

LAVA(Linaro Automated Validation Architecture)는 실제/가상 보드에 OS를 배포하고 테스트를 돌리고 결과를 추적하는 CI 시스템이다(공식 문서). Linaro · KernelCI 계열이 쓴다.

| 개념 | 뜻 | 드론 벤치에 대응 |
|---|---|---|
| device type / device | 보드 종류와 실제 개체 | "handset rev B", "companion SoM" |
| job (YAML) | `deploy` → `boot` → `test` 액션 | 이미지 굽기 → 부팅 → 테스트 셸 |
| worker / dispatcher | 보드가 물린 PC에서 job 실행 | 벤치 PC |
| health check | 주기적 기본 job으로 보드 상태 확인, 실패 시 보드 offline | 벤치 자가 진단 |
| 전원 · 콘솔 제어 | PDU 명령 · 시리얼 콘솔 명령을 device 설정에 연결 [확인 필요: 설정 키 이름] | 프로그래머블 전원 · USB-UART |

FTE 판단: LAVA는 **Linux 보드 수가 많을 때** 의미가 있다. MCU · RF 계측이 섞인 HIL은 LAVA 모델에 잘 안 맞을 수 있어서, Neros 규모에서는 자체 Python 벤치 + CI가 현실적일 수 있다 [추정]. LAVA에서 가져올 개념은 **device health check, 보드 예약/잠금, job 정의를 데이터로 분리**다 [R09](2026-10-24_R09_scalable_reusable_test_platform.md).

### 5.7 커널 · 드라이버 테스트 — kselftest, LTP

| 도구 | 무엇 | 실행 (공식 문서 기준) | 드론 제품에서 쓰임 |
|---|---|---|---|
| kselftest | 커널 트리 안의 자체 테스트, TAP 출력 | `make -C tools/testing/selftests TARGETS="timers" run_tests`, 설치 후 `./run_kselftest.sh -c timers` | 커널 버전 업 · BSP 패치 후 타이머 · 네트워크 · gpio 회귀 |
| LTP | 시스템 콜 · 파일시스템 · 스트레스 대규모 묶음 | 러너는 **kirk** (`runltp-ng`는 2023, `runltp`는 2026년에 kirk로 대체 — LTP 메일링 리스트) | 이미지 안정성 기본선 |
| 제품 드라이버 테스트 | 우리 카메라 · 무전기 · GPIO 드라이버 | 자체 스크립트: probe 확인, sysfs/ioctl, 반복 load/unload | 드라이버 회귀 |
| 스트레스 | `stress-ng` 등 | CPU · 메모리 · IO 부하 | cyclictest의 배경 부하 (5.8) |

드라이버 테스트에서 FTE가 꼭 넣을 것: **probe 실패 시 동작**(카메라 없는 상태로 부팅해도 나머지 서비스가 뜨나), **반복 bind/unbind 1000회**(누수 · 크래시), **에러 경로**(I2C NACK, 타임아웃) — 하드웨어 고장 주입은 R08 벤치의 릴레이로.

### 5.8 PREEMPT_RT 와 cyclictest

**사실 확인:** PREEMPT_RT는 2024-09-20 Linus Torvalds가 병합해 **Linux 6.12부터 메인라인**에 들어왔다. 병합 커밋 메시지 기준 지원 아키텍처는 **x86(32/64), ARM64, RISC-V**. 설정에서 EXPERT를 켜야 고를 수 있다(heise, 위키 등). 단, 메인라인에 들어왔다고 모든 드라이버가 RT에서 좋은 지연을 보장하지는 않는다 — **보드 · 드라이버별 측정이 여전히 필요**하다.

> ⚠️ Gemini 정정: Gemini는 PREEMPT_RT를 "리눅스 커널에 실시간 패치를 입혀"라고 설명했다. 6.12부터는 **외부 패치가 아니라 메인라인 설정 옵션**이다(x86/arm64/riscv). 다만 제품이 6.12 이전 커널(예: 벤더 BSP 5.x)을 쓰면 여전히 외부 RT 패치가 필요하다 — Jetson BSP의 커널 버전은 JetPack 버전마다 다르다 [확인 필요].

cyclictest는 rt-tests 패키지의 표준 지연 측정 도구다. 주기 타이머로 깨어나서 "깨어나야 했던 시각 vs 실제 깨어난 시각" 차이를 잰다.

```text
# 흔히 쓰는 형태 (옵션 의미는 아래 표; 버전별 차이는 cyclictest --help로 확인)
sudo cyclictest --mlockall --smp --priority=80 --interval=200 --distance=0 \
     --duration=1h --histogram=400 --quiet > cyclictest_hist.txt
```

| 옵션 | 뜻 |
|---|---|
| `--mlockall` (`-m`) | 메모리 잠금, 페이지 폴트 제거 |
| `--smp` (`-S`) | CPU마다 측정 스레드 |
| `--priority=80` (`-p`) | SCHED_FIFO 우선순위 |
| `--interval=200` (`-i`) | 주기 200 µs |
| `--histogram=400` (`-h`) | 0~400 µs 히스토그램 출력 |
| `--duration=1h` (`-D`) | 측정 시간 |
| `--quiet` (`-q`) | 끝에 요약만 |

출력은 스레드마다 `Min / Act / Avg / Max` (µs). **판정은 Max와 히스토그램 꼬리**다. 평균은 거의 의미 없다.

시험 설계:

1. 기준: 같은 보드 · 같은 커널 설정에서 **무부하 1시간 + 부하 1시간**(stress-ng CPU/메모리/IO + 네트워크 + 영상 인코딩 실제 부하).
2. 산출: 히스토그램 그래프, Max, 99.999% 지점.
3. 회귀 판정: Max가 기준선 + 여유 이내, 꼬리 모양 변화.
4. 원인 추적 도구: `ftrace` + `--breaktrace`(임계 초과 시 추적 중단) [확인 필요: 옵션 이름], `rtla timerlat`(커널 내장 RT 분석 도구) [확인 필요: 대상 커널에 포함 여부].
5. 공고 연결: Embedded Linux "Real-time tuning — PREEMPT_RT, latency analysis", Platform "CPU affinity and isolation … jitter and deadline analysis".

### 5.9 OTA A/B 업데이트 — 전원 차단 · 롤백 · bootcount

A/B(dual copy)는 rootfs(와 커널)를 두 벌 두고, 안 쓰는 쪽에 새 이미지를 쓴 뒤 다음 부팅에서 바꾸는 방식이다. 새 쪽이 부팅에 실패하면 **bootcount**가 다 떨어져 이전 쪽으로 돌아간다(rollback).

RAUC 공식 문서(U-Boot 연동)에서 확인한 메커니즘:

- U-Boot 환경 변수 `BOOT_ORDER`(예: `A B`)와 슬롯별 남은 시도 횟수 `BOOT_A_LEFT`, `BOOT_B_LEFT`.
- 부팅 시도마다 카운터가 줄고, 부팅 성공 후 `rauc status mark-good`이 카운터를 리셋한다. 이 명령은 보통 **다른 서비스가 다 뜬 뒤** 실행되는 systemd 유닛으로 둔다.
- 사용자 공간은 `fw_printenv` / `fw_setenv`(`/etc/fw_env.config`)로 U-Boot 환경을 읽고 쓴다. 환경은 **이중(redundant) 저장**을 권장.
- 부팅된 슬롯은 커널 커맨드라인 `rauc.slot=<bootname>`으로 알려준다.

Mender도 같은 원리다: U-Boot `bootcount` · `bootlimit` · `upgrade_available` 조합, 새 이미지가 정상이면 commit, commit 전에 재부팅되면 롤백 (Mender 문서 · 포럼). SWUpdate도 dual-copy + 부트로더 변수 방식을 지원한다 [확인 필요: 변수 이름].

**FTE의 OTA 시험 매트릭스:**

| # | 시나리오 | 주입 방법 | 기대 결과 |
|---|---|---|---|
| 1 | 정상 업데이트 | 업데이트 → 재부팅 | 새 슬롯 부팅, mark-good, 버전 확인 |
| 2 | **다운로드/쓰기 중 전원 차단** | 쓰기 진행률 10/50/90%에서 프로그래머블 전원 OFF | 재부팅 시 **기존 슬롯** 정상, 업데이트 재시도 가능 |
| 3 | 슬롯 전환 직후 전원 차단 | 환경 변수 쓰기 순간 근처 OFF (타이밍 스윕) | 둘 중 하나로 반드시 부팅 (brick 금지) |
| 4 | 새 이미지 커널 패닉 | 일부러 깨진 이미지 서명해 배포 | bootcount 소진 → 자동 롤백, 이벤트 로그 |
| 5 | 새 이미지 부팅은 되지만 앱 실패 | 앱 서비스가 ready 안 됨 | mark-good 안 됨 → 다음 재부팅에 롤백 |
| 6 | 서명 없는/잘못된 이미지 | 다른 키로 서명 | 설치 거부, 기존 슬롯 유지 |
| 7 | 다운그레이드 | 이전 버전 이미지 | 정책대로(거부 또는 허용) — 정책 자체를 확인 |
| 8 | 저장 공간 부족 · 쓰기 오류 | 작은 파티션, 손상된 블록 흉내 | 깔끔한 실패 + 기존 슬롯 유지 |
| 9 | U-Boot 환경 손상 | 한쪽 환경 사본 덮어쓰기 | redundant 사본으로 복구 |
| 10 | 반복 업데이트 내구 | 1000회 업데이트/롤백 루프 | 플래시 마모 · 누수 · 실패율 기록 |

2 · 3번이 핵심이다. 전원 차단 타이밍은 **한 지점이 아니라 스윕**해야 한다(예: 업데이트 시작 후 0~T초를 100 ms 간격). 자동화 요소: 프로그래머블 전원(SCPI), UART 로그, 업데이트 진행률을 읽는 스크립트, 결과 DB. 이건 R08 벤치 그대로 재사용된다.

### 5.10 보안 — secure boot, 서명 이미지

Embedded Linux 공고는 "secure boot concepts and safe update/recovery design"을 필수로 적었다. Neros가 실제로 어떤 체인을 쓰는지는 모른다 [확인 필요 · 추정].

| 항목 | 무엇 | FTE 시험 |
|---|---|---|
| Chain of trust | Boot ROM → SPL → U-Boot → kernel(FIT 서명) → rootfs(dm-verity 등) 각 단계가 다음 단계 서명 검증 | 각 단계 이미지를 1바이트 바꿔 부팅 거부 확인 |
| 서명된 업데이트 번들 | RAUC/Mender/SWUpdate 번들 서명 | 잘못된 키 · 만료 인증서 · 변조 번들 거부 |
| 디버그 포트 잠금 | 양산품 JTAG/UART 콘솔 비활성 | 양산 이미지에서 콘솔 로그인 불가 확인 |
| 롤백 방지 | 취약한 옛 버전으로 내리기 방지 (anti-rollback 카운터) | 다운그레이드 시도 |
| 키 관리 | 개발 키 vs 양산 키 | **개발 키로 서명한 이미지가 양산 보드에서 거부되는지** |

주의: secure boot 퓨즈(OTP)는 **되돌릴 수 없다**. 벤치 보드에서 퓨즈를 굽는 시험은 하드웨어 팀 승인 없이 하지 않는다.

---

## 6. RTOS 보드 — Zephyr · FreeRTOS · NuttX · ChibiOS

### 6.1 어디에 RTOS가 있나 (그리고 없나)

| 보드 | 흔한 SW 형태 | Neros 근거 | 메모 |
|---|---|---|---|
| FC (PX4) | NuttX | Flight SW · FTE 공고에 PX4 | [R04](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md) |
| FC (ArduPilot) | ChibiOS | FTE 공고에 ArduPilot | 같음 |
| FC (Betaflight/INAV) | RTOS 없음(자체 스케줄러) | FTE 공고 | [R03](2026-10-24_R03_betaflight_inav_internals_and_testing.md) |
| ESC | 대부분 **bare-metal** (예: BLHeli_32, AM32, Bluejay) | 공고에 ESC 펌웨어 이름 없음 | RTOS일 이유가 적다 [추정] |
| BMS / 스마트 배터리 | bare-metal 또는 소형 RTOS | 공고 근거 없음 | [추정] |
| 헤드셋 · 핸드셋 · 주변 장치 | MCU + FPGA + Linux, RTOS 우대 | Tennessee 공고 "RTOS experience such as FreeRTOS, Zephyr" (우대) | 가장 직접적인 근거 |
| 무전기 · 링크 보드 | ELRS 계열(ESP32/STM32, Arduino/PlatformIO 기반) 또는 자체 | ELRS 공고 다수 | [R05](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md) |
| micro-ROS 노드 | Zephyr · FreeRTOS · NuttX 위에서 ROS 2 클라이언트 | 공고 근거 없음 | 가능성만 [추정] |

> ⚠️ Gemini 정정: Gemini는 "RTOS(FreeRTOS · Zephyr)는 변속기(ESC), 스마트 배터리, 통신 브리지 등에서 돈다"고 단정했다. 공개 공고에서 RTOS가 나온 곳은 **Tennessee Firmware Engineer의 우대 항목**(헤드셋 · 핸드셋 등 주변 장치)뿐이고, 흔한 FPV ESC 펌웨어는 bare-metal이다. ESC/BMS의 RTOS 사용은 **[추정]**으로만 둔다.

### 6.2 Zephyr — ztest + twister (가장 잘 갖춰진 테스트 체계)

Zephyr 공식 문서에서 확인한 것:

- **ztest**: 테스트 프레임워크. `CONFIG_ZTEST=y`, `ZTEST_SUITE(name, predicate, setup, before, after, teardown)`로 스위트, `ZTEST(suite, test)`로 테스트, `zassert_true` · `zassert_equal` 등. fixture용 `ZTEST_F`, 사용자 모드 `ZTEST_USER`, 파라미터 `ZTEST_P`.
- **twister**: 테스트 러너. 테스트 시나리오를 `testcase.yaml`/`sample.yaml`로 정의 (`platform_allow`, `integration_platforms`, `harness: ztest|console|pytest|robot` 등).
- QEMU · native_sim 같은 가상 플랫폼과 **실제 보드** 둘 다 같은 테스트를 돌린다.
- 결과는 `twister.json` 등 리포트 파일로 남는다(CI가 읽음).

```text
# 가상 플랫폼에서 커널 테스트 묶음
west twister -p qemu_x86 -T tests/kernel/common

# 실제 보드 한 대 (시리얼로 결과 수집)
west twister --device-testing --device-serial /dev/ttyACM0 --device-serial-baud 115200 \
     -p frdm_k64f -T tests/kernel

# 여러 보드: 하드웨어 맵 만들고 사용
west twister --generate-hardware-map map.yml
west twister --device-testing --hardware-map map.yml -T samples/hello_world/
```

```c
/* ztest 예 — 링 버퍼 오버플로 정책 (개념) */
#include <zephyr/ztest.h>
#include "ring.h"

ZTEST(ring_suite, test_overwrite_oldest)
{
    struct ring r; ring_init(&r, 4);
    for (int i = 0; i < 6; i++) ring_push(&r, i);
    zassert_equal(ring_pop(&r), 2, "oldest should be dropped");
}

ZTEST_SUITE(ring_suite, NULL, NULL, NULL, NULL, NULL);
```

FTE 관점의 장점: **같은 테스트가 QEMU(CI, 하드웨어 없음) → 실제 보드(HIL)로 그대로 올라간다.** 이게 R07 피라미드의 "unit → HIL" 연결을 프레임워크가 해 준 사례다. 하드웨어 맵(`map.yml`)은 보드 · 시리얼 · 프로브 ID를 데이터로 둔다 → R09의 "벤치 인벤토리를 코드 밖 데이터로" 원칙과 같다.

### 6.3 FreeRTOS · NuttX · ChibiOS 의 테스트

| RTOS | 자체 테스트 | 실행 | FTE가 가져올 것 |
|---|---|---|---|
| FreeRTOS | FreeRTOS 저장소의 데모 · 테스트 코드(커널 기능 데모 태스크), CMock 기반 유닛 테스트 [확인 필요: 현재 디렉터리 구조] | POSIX/Windows 시뮬레이터 포트, QEMU 데모 | 커널 자체보다 **우리 태스크 코드의 유닛 테스트 + 시뮬레이터 포트에서 통합 테스트** |
| NuttX | **ostest** — 릴리스 검증용 OS 기능 테스트(배리어, 라운드로빈 등). `CONFIG_TESTING_OSTEST_LOOPS`, `..._STACKSIZE` 등 설정 | NSH에 built-in 앱으로 넣고 실행 (`ostest`로 추정 [확인 필요]) | PX4 보드 BSP 바꿀 때 OS 기본선 |
| ChibiOS | 커널 테스트 스위트(test/rt, test/oslib 등) — 데모에 링크해 커널 · 포트 · 데모를 검사, 시퀀스별 결과 출력 | 보드 데모 펌웨어로 실행, 시리얼 출력 | ArduPilot 보드 포트 검증 기본선 [확인 필요: 현재 구조] |

### 6.4 스택 · 타이밍 측정

| 측정 | 방법 | 판정 |
|---|---|---|
| 스택 최고 사용량 | FreeRTOS `uxTaskGetStackHighWaterMark()`, Zephyr `CONFIG_THREAD_STACK_INFO` + `k_thread_stack_space_get()` 또는 shell `kernel stacks` [확인 필요], NuttX 스택 칠하기(coloration) | 남은 여유 ≥ 20~25% (팀 규칙으로) |
| 태스크 실행 시간 | DWT CYCCNT, GPIO 토글 + 로직 분석기, RTOS 트레이스(SEGGER SystemView, Percepio Tracealyzer) | 최악 실행 시간(WCET 관측치) ≤ 예산 |
| 인터럽트 지연 | 외부 엣지 → ISR GPIO 토글 시간 (함수 발생기 + 스코프) | 분포 · max |
| CPU 부하 | idle hook 카운터, 런타임 통계 | 최악 시나리오에서 ≤ X% |
| 우선순위 역전 | mutex 우선순위 상속 설정 검사, 트레이스로 관찰 | 고우선 태스크 대기 시간 상한 |

SSD FW에서 Cortex-R/M 태스크 타이밍을 보던 경험과 거의 같은 영역이다 (9장).

### 6.5 Watchdog 시험

| 시나리오 | 주입 | 기대 |
|---|---|---|
| 태스크 무한 루프 | 테스트 빌드의 디버그 명령으로 특정 태스크 정지 | WDT 리셋, 리셋 원인 레지스터에 WDT 기록 |
| 인터럽트 폭주 | 함수 발생기로 IRQ 핀에 고주파 | 메인 루프 굶음 → WDT 동작 |
| WDT 창(window) 위반 | 너무 일찍 kick | window WDT면 리셋 |
| 리셋 후 동작 | 위 이후 | 안전 상태로 시작, 원인 로그 보존(`.noinit` 영역 등), 반복 리셋 시 boot loop 방지 |
| 비행 중 리셋 복구 | (FC) 리셋 후 warm start | R03/R04 쪽 범위 — 여기선 주변 보드가 FC 없이도 안전한 상태로 가는지 |

판정에 꼭 넣을 것: **리셋까지 시간**(WDT 타임아웃 설정과 맞나), **리셋 원인 기록**, **리셋 후 첫 정상 출력까지 시간**.

---

## 7. 지상 — GCS · ATAK · QGroundControl

### 7.1 ATAK와 CoT

ATAK(Android Team Awareness Kit)은 미군 · 동맹국 · 공공안전 기관이 쓰는 지도 기반 상황 공유 앱이다. Windows판 WinTAK, 서버 TAK Server가 있다. Senior Software Engineer 공고가 "ATAK plugins (Java/Kotlin), WinTAK plugins (C#), TAK Server, Cursor on Target"을 우대로 적었다 [확인됨 공고]. Ground Software Manager는 "integration with external C2 and tools (e.g. ATAK)"를 적었다.

**CoT(Cursor on Target)** 는 MITRE가 만든 이벤트 메시지 형식이다. 기본 스키마(Event.xsd)는 MITRE 문서 기준이며, 기본 구조:

| 요소 | 속성 | 뜻 |
|---|---|---|
| `<event>` | `version`, `uid`, `type`, `time`, `start`, `stale`, `how` | 하나의 이벤트(유닛 · 표적 · 위치) |
| `type` | 계층 문자열, 예 `a-f-G-E-S`, `a-f-A-M-F-Q`(친군 공중 무인기 [확인 필요]) | atom-소속(f/h/n/u)-차원(G/A/S/U/P)-기능 |
| `time` · `start` · `stale` | ISO 8601 | 생성 시각, 유효 시작, **만료 시각** |
| `how` | 예 `m-g`(machine GPS) [확인 필요], `h-g-i-g-o` | 좌표가 어떻게 만들어졌나 |
| `<point>` | `lat`, `lon`, `hae`, `ce`, `le` | 위치, 높이(타원체 기준), 수평/수직 오차 |
| `<detail>` | 자유 확장 | 콜사인, 센서 시야, 영상 URL 등 |

```text
<event version="2.0" uid="NEROS-UAS-0001" type="a-f-A-M-F-Q" how="m-g"
       time="2026-10-24T18:00:00Z" start="2026-10-24T18:00:00Z" stale="2026-10-24T18:00:10Z">
  <point lat="33.8358" lon="-118.3406" hae="120.0" ce="5.0" le="10.0"/>
  <detail><contact callsign="TEST-UAS-1"/></detail>
</event>
```

(값은 예시. 이 `type` 문자열이 무인기에 맞는지는 [확인 필요].)

ATAK 기본 SA 멀티캐스트 주소/포트로 239.2.3.1:6969가 흔히 언급되지만 이번에 공식 문서로 확인하지 못했다 [확인 필요]. 최신 TAK는 XML 외에 **Protobuf 기반 TAK 프로토콜**도 쓴다 [확인 필요: 버전 협상 방식].

**FTE의 CoT 시험:**

| 시험 | 방법 | 판정 |
|---|---|---|
| 스키마 유효성 | 생성 메시지를 Event.xsd로 검증 | 100% 통과 |
| 필드 정확도 | 시뮬레이터/SITL 기체 위치 → CoT `point`와 비교 | 오차 ≤ ce, 시각 일치 |
| 갱신 주기 · 지연 | 기체 위치 변화 → ATAK 화면/수신 소켓까지 시간 | 주기 · p99 지연 |
| stale 처리 | 송신 중단 | stale 시각 후 ATAK에서 만료 표시 |
| uid 안정성 | 재부팅 · 재연결 | 같은 기체 = 같은 uid (중복 아이콘 금지) |
| 다수 기체 | N대 동시 송신 | 누락 · 섞임 없음 (멀티 기체 공고 "Multi-vehicle / swarm") |
| 깨진 입력 내성 | 잘린 XML, 이상한 type, 거대 detail | 수신 측 크래시 없음 (Jon 패킷 문제의 지상 버전) |
| 링크 열화 | `tc netem` 손실 · 지연 | 마지막 위치 유지 + stale 규칙 |

### 7.2 QGroundControl 테스트

QGC 공식 개발 가이드 기준: **디버그 빌드**에서 `--unittest`로 전체 유닛 테스트, `--unittest:RadioConfigTest`처럼 하나만, `--unittest-stress:name`으로 20회 반복. PR 병합 전 통과가 규칙이다. Neros가 QGC를 포크해 쓰는지는 모른다 [추정] — Flight SW 공고는 "QGroundControl tooling"만 적었다.

FTE 관점에서 QGC 류 GCS 시험:

- **MAVLink 상호운용**: SITL(PX4/ArduPilot) + GCS 연결 → 파라미터 다운로드 완료 시간, 미션 업로드/다운로드 왕복 일치, 모드 변경 명령 ACK.
- **링크 열화**: GCS ↔ 라우터 사이 netem → heartbeat 손실 경고 표시 시간, 재연결.
- **영상 수신**: UDP 5600/RTSP URL 설정 → 첫 프레임 시간, 끊김 후 복구.

### 7.3 UI 자동화 가능성

| 대상 | 방법 | 현실성 |
|---|---|---|
| QGC(Qt/QML) | 내장 유닛 테스트, Qt Test, Squish(상용) 같은 GUI 자동화 [확인 필요] | 유닛 · 통합은 쉬움, 픽셀 수준 UI는 비용 큼 |
| ATAK(Android) | 플러그인 단위 테스트, `adb` + UI Automator/Espresso [확인 필요: 플러그인 SDK 지원] | 에뮬레이터 가능 여부는 라이선스 · 빌드 조건에 따라 다름 |
| 자체 handset UI (임베디드 Linux) | 앱에 테스트용 소켓 API(명령 주입 · 상태 조회) 넣기, 화면 캡처 비교 | **가장 현실적 — 개발팀과 "테스트 훅"을 처음부터 합의** |
| 모든 UI | 화면을 카메라로 찍어 판정(최후 수단) | 느리고 깨지기 쉬움 |

원칙: UI를 눌러서 테스트하기보다 **UI 아래 계층(메시지 · 상태)을 테스트**하고, UI는 소수의 smoke 시나리오만 자동화한다.

### 7.4 상호운용(interop) 시험

경계는 공고가 직접 말한다: "Define the ground side of the aircraft-to-ground interface with the flight software team, including behavior when the link degrades" (Ground Software). 시험 구조:

1. **계약(contract) 문서화**: 메시지 목록, 주기, 단위, 좌표계, 버전 필드. 이걸 기계가 읽는 형태(스키마)로.
2. **양쪽 독립 검증**: 기체 쪽 출력이 계약을 지키나 / 지상 쪽 입력 처리가 계약을 지키나 — 상대 없이 시뮬레이터로.
3. **버전 매트릭스**: 기체 펌웨어 N, N-1 × 지상 소프트웨어 N, N-1 조합 smoke 테스트. 현장에서는 버전이 섞인다.
4. **외부 시스템**: ATAK/TAK Server 버전 몇 개에 CoT 송신 → 표시 확인.

---

## 8. 한 장으로 — companion · 영상 · Linux 테스트 벤치 구성

R08의 FC 중심 HIL 벤치 옆에 붙는 "Linux/영상 벤치"의 구성안이다 [추정 — Neros 실제 벤치는 모름]. 계측기 상세는 [R08](2026-10-24_R08_hil_bench_design_instruments.md).

```text
 ┌──────────────────────── CI (GitLab CI / Jenkins) ───────────────────────────┐
 │  이미지 빌드(Yocto/Buildroot) → 아티팩트 저장(해시 기록) → 벤치 job 예약        │
 └───────────────────────────────┬────────────────────────────────────────────┘
                                 │ job (YAML: 이미지 · 테스트 묶음 · 보드 종류)
                                 ▼
 ┌──────────────────────── Bench host PC (Linux, Python) ──────────────────────┐
 │ 전원 드라이버(SCPI) · 콘솔(pyserial) · 플래시(USB recovery/fastboot/UMS)      │
 │ 네트워크 주입(tc netem) · ROS 2 probe 노드 · GStreamer 수신 · DSO/LA 드라이버 │
 │ 결과 → DB(시계열 + 아티팩트) → 대시보드 / 기준선 비교 / PASS·FAIL            │
 └──┬──────────┬──────────────┬──────────────┬──────────────┬─────────────────┘
    │SCPI      │USB-UART      │Ethernet       │USB(DSO/LA)    │GPIO 보드
    ▼          ▼              ▼               ▼               ▼
 [Prog. PSU] [콘솔: U-Boot/ [스위치 ── companion ── FC(SITL 또는 실보드)]
  전압 sag ·  kernel 로그]    │  (Jetson 등)      │ UART/ETH (MAVLink/uXRCE-DDS)
  전원 차단                   │  카메라 ◄── LED (GPIO 보드가 깜빡)
  (OTA 시험)                  │     │ 인코드 → RTP/UDP
                              │     ▼
                              │  [Video TX]═══동축═══[RF 감쇠기]═══[Video RX]
                              │                                         │
                              │                                 [디스플레이/고글]
                              │                                         ▲ 포토다이오드
                              │                                         │ → DSO CH2
                              ▼                                  (LED 구동 = DSO CH1)
                         [GCS/ATAK 수신 PC 또는 에뮬레이터: CoT · 영상 재배포 확인]

 + 열 챔버(선택): companion을 넣고 tegrastats · 추론 지연 · 영상 fps 장시간 기록
```

이 벤치 하나로 도는 시험 묶음 예:

| 묶음 | 내용 | 주기 |
|---|---|---|
| smoke (10분) | 이미지 플래시 → 부팅 시간 → 서비스 ready → ROS 2 토픽 주기 → 영상 첫 프레임 → CoT 1건 | 커밋/머지마다 |
| nightly (2~4시간) | cyclictest 부하 1시간, glass-to-glass 1000회, 감쇠 스윕, 링크 손실 주입, kselftest 일부 | 매일 밤 |
| weekly | OTA 전원 차단 타이밍 스윕, 열 soak 60분, 재현성 이중 빌드, 버전 매트릭스 | 주 1회 |
| release gate | 위 전부 + 기준선 대비 판정 리포트 | 릴리스 전 |

---

## 9. SSD 검증에서는 이랬다 → 드론에서는 이렇다

Don의 경험 범위(데이터센터 SSD FW · 검증, 챔버 플랫폼 SDK, web 기반 test automation, NVMe telemetry 디버그 기능, error reporting/handling, Cortex-R/M · Xtensa FW, 최근 RF 칩셋 통합 · stress 기반 latent defect · DSO/protocol analyzer)에서 이 단원으로 이어지는 다리다. 공통 원리는 [R01](2026-10-24_R01_fte_vs_ssd_validation.md).

| 주제 | SSD 검증에서는 이랬다 | 드론(이 단원)에서는 이렇다 | 그대로 가져갈 것 |
|---|---|---|---|
| 온도 | 챔버 테스트 플랫폼 SDK로 온도 제어 · 스케줄링 · eSSD 상태 모니터링 | Jetson 열 throttling, 영상 인코더 열, 고온 환경 운용 — "sustained thermal load" 하 지연 게이트 | 온도 프로파일 + 성능 지표를 **같은 시간축**에 기록하는 습관 |
| 상태 수집 | NVMe telemetry 디버그 기능 | ROS 2 토픽, MAVLink 텔레메트리, tegrastats, wfb 통계, systemd 로그 | "증상이 나기 전 상태"를 남기는 telemetry 설계 감각 |
| 자동화 | UART 테스트 시퀀스 자동화, 다른 엔지니어가 쓰는 SDK | 콘솔 · 전원 · 플래시 · 네트워크 주입을 묶은 벤치 SDK | **남이 쓰는 도구**로 만드는 것 (API · 문서 · 안정성) |
| 결과 보기 | web 기반 test automation | 대시보드 · 기준선 비교 · 릴리스 리포트 | 결과를 웹에서 누구나 보는 구조 |
| 에러 처리 | error reporting/handling scheme | 링크 손실 failsafe, OTA 롤백, WDT 리셋 원인 기록 | 에러가 **기록되고 분류되는지**까지 시험 |
| 드문 결함 | stress 기반 latent defect 발굴 | 장시간 soak, 1000회 OTA 루프, 반복 bind/unbind, cyclictest 꼬리 | 평균이 아니라 **꼬리 · 드문 사건**을 보는 시험 설계 |
| 계측 | DSO · protocol analyzer로 루트코즈 | 포토다이오드 + DSO glass-to-glass, 부팅 구간 측정, UART/MAVLink 캡처 | 계측기를 **자동화 루프 안에** 넣기 |
| 펌웨어 | Cortex-R/M · Xtensa FW, health monitoring debug FW | RTOS 보드의 스택 · 타이밍 · WDT | 태스크 타이밍 · 스택 여유를 숫자로 관리 |
| 호스트 측 | 호스트 Linux에서 테스트 도구 실행 | **DUT 자체가 Linux** — 부팅 · 커널 · OTA가 시험 대상 | 새로 배울 부분. EmbeddedLinux 자습 노트가 바탕 |
| 버스 | PCIe 3/4/5 커버리지, I2C/SPMI/RFFE 루트코즈 | MIPI CSI 카메라, USB/Ethernet, UART(MAVLink), I2C 센서 | 버스 계층별 커버리지 표를 만드는 방식 |
| 무선 | (최근) RF 칩셋 통합 | 영상 링크 감쇠 스윕, cliff 위치, 복구 시간 | RF 계측 감각을 영상 지표와 연결 |

**새로 익혀야 할 것(경험 없음):** GitLab CI/Jenkins 실무, pytest 실무, 드론 비행, ROS 2 실무, GStreamer 실무, OTA 프레임워크 실무. 이 단원의 실습(11장)이 그 출발점이다.

---

## 10. 테스트 아이디어 체크리스트

### Companion

- [ ] ROS 2 핵심 토픽 목록과 **주기 하한 · 지연 p99 상한**이 문서화되어 있다
- [ ] FC ↔ companion 링크(serial/UDP)의 끊김 → FC failsafe 진입 시간을 자동으로 잰다
- [ ] agent/라우터 프로세스 kill → 자동 재연결을 반복(100회) 시험한다
- [ ] `px4_msgs`(또는 동등) 메시지 정의 버전 × FC 펌웨어 버전 조합을 시험한다
- [ ] rosbag golden input/output 세트가 버전 관리되고, 재생 비교 규칙(허용 오차)이 있다
- [ ] 열 soak(30~60분) 동안 추론 p99 · 클럭 · 온도를 같은 시간축에 기록한다
- [ ] TensorRT 엔진 재생성(JetPack/TensorRT 업데이트) 시 정확도 · 지연 회귀가 자동으로 돈다
- [ ] 시간 동기 상태(PTP/PPS)가 로그에 남고, 타임스탬프 단조성 검사가 있다

### 영상

- [ ] glass-to-glass를 포토다이오드 + DSO로 **N ≥ 1000회** 자동 측정, 분포로 보고
- [ ] 파이프라인 문자열 · 인코더 설정이 버전 관리되고 diff가 리뷰된다
- [ ] 프레임 드롭률 · 간격 지터 · freeze 이벤트를 프레임 ID로 계산한다
- [ ] 감쇠 스윕 곡선(dB vs 지표)을 기준선과 비교한다
- [ ] 링크 끊김 → 복구 시간 분포를 잰다
- [ ] 장시간 soak에서 메모리(RSS) 추세 · leaks tracer 결과를 확인한다

### 임베디드 Linux

- [ ] 이중 빌드 재현성 비교가 주기적으로 돈다
- [ ] 부팅 구간별(T0~T4) 시간이 매 빌드 기록된다
- [ ] testimage/ptest 또는 run-tests 형태의 런타임 테스트가 QEMU와 실보드 양쪽에 있다
- [ ] kselftest/LTP 일부가 커널 · BSP 변경 시 돈다
- [ ] cyclictest 부하 1시간 Max · 히스토그램이 기준선과 비교된다
- [ ] OTA 전원 차단 **타이밍 스윕**과 롤백 시나리오(표 5.9의 1~10)가 자동화되어 있다
- [ ] 변조 이미지 · 잘못된 키 · 다운그레이드 거부를 시험한다

### RTOS 보드

- [ ] 유닛/통합 테스트가 QEMU(또는 시뮬레이터 포트)와 실보드에서 같은 코드로 돈다 (twister 방식)
- [ ] 태스크별 스택 여유 · 최악 실행 시간 · CPU 부하가 기록된다
- [ ] WDT 시나리오(멈춤, IRQ 폭주, window 위반)와 리셋 원인 기록을 시험한다

### 지상

- [ ] CoT 출력이 스키마 검증 · 필드 정확도 · stale 규칙 시험을 통과한다
- [ ] 다수 기체 uid 충돌 · 깨진 메시지 내성 시험이 있다
- [ ] 기체 × 지상 소프트웨어 버전 매트릭스 smoke가 있다
- [ ] UI 아래 계층에 테스트 훅(소켓 API 등)이 합의되어 있다

---

## 11. 직접 해 볼 것 (Mac + Lima VM, 하드웨어 없이)

Don 방침: **무거운 Yocto/전체 Buildroot 빌드는 가이드만**. 아래는 Mac 또는 Lima VM `elinux`(Ubuntu)에서 돌릴 수 있는 것 위주다. 하드웨어 실습은 마지막에 따로 적고 "사기 전에 Don과 예산 확인".

| # | 실습 | 어디서 | 시간 | 이 단원 절 |
|---|---|---|---|---|
| L1 | GStreamer 송수신 + `timeoverlay` + `latency` tracer | Lima VM (또는 Mac Homebrew GStreamer) | 1시간 | 4.2 |
| L2 | 같은 파이프라인에 `tc netem` 손실/지연 → 드롭 · 지터 계산 (아래 코드) | Lima VM | 1~2시간 | 4.6 |
| L3 | cyclictest 무부하 vs `stress-ng` 부하 비교, 히스토그램 파싱 (아래 코드) | Lima VM (VM이라 숫자는 의미 작음 — **방법 연습**) | 1시간 | 5.8 |
| L4 | `systemd-analyze blame/critical-chain`으로 VM 부팅 구간 보기 | Lima VM | 30분 | 5.4 |
| L5 | kselftest 일부(`timers`) 빌드 · 실행 → TAP 출력 읽기 | Lima VM (커널 소스 필요) | 1~2시간 | 5.7 |
| L6 | Zephyr: `west twister -p qemu_x86 -T tests/kernel/common` + 내 ztest 하나 | Lima VM (Zephyr SDK 설치) | 반나절 | 6.2 |
| L7 | ROS 2(Docker 이미지) + PX4 SITL + `MicroXRCEAgent udp4 -p 8888` → `ros2 topic hz /fmu/out/...` | Lima VM + Docker | 반나절 | 3.2, 3.5 |
| L8 | L7에 `launch_testing` 테스트 1개 (토픽 주기 하한) | 같음 | 2시간 | 3.5.2 |
| L9 | rosbag 녹화 → 재생 → 출력 비교 스크립트 | 같음 | 2시간 | 3.5.3 |
| L10 | CoT 생성 · 검사 스크립트 (아래 코드) → UDP로 쏘고 받아 보기 | Mac | 1시간 | 7.1 |
| L11 | Buildroot `support/testing/run-tests -l` 목록만 읽고 테스트 1개 구조 읽기 (빌드는 선택) | Lima VM | 1시간 | 5.5 |
| G1 | (가이드만) Yocto `testimage` + `simpleremote`로 QEMU 이미지 테스트 | — | — | 5.5 |
| G2 | (가이드만) RAUC A/B + U-Boot QEMU에서 전원 차단 흉내(`kill -9 qemu`) 롤백 | — | — | 5.9 |

### L1 · L2 명령

```text
# Lima VM 안 (Ubuntu)
sudo apt-get install -y gstreamer1.0-tools gstreamer1.0-plugins-good gstreamer1.0-plugins-bad \
     gstreamer1.0-plugins-ugly gstreamer1.0-libav rt-tests stress-ng

# 송신 (loopback). 화면이 없으면 수신 쪽 sink를 fakesink로.
gst-launch-1.0 videotestsrc is-live=true ! video/x-raw,width=640,height=360,framerate=60/1 \
  ! timeoverlay ! x264enc tune=zerolatency speed-preset=ultrafast key-int-max=30 \
  ! rtph264pay config-interval=1 pt=96 ! udpsink host=127.0.0.1 port=5600

# 수신 + 파이프라인 지연 tracer
GST_DEBUG="GST_TRACER:7" GST_TRACERS="latency(flags=pipeline+element)" \
gst-launch-1.0 udpsrc port=5600 caps="application/x-rtp,media=video,encoding-name=H264,payload=96" \
  ! rtpjitterbuffer latency=20 ! rtph264depay ! avdec_h264 ! fakesink sync=false 2> latency.log

# loopback에 손실 · 지연 주입 (끝나면 반드시 삭제)
sudo tc qdisc add dev lo root netem delay 20ms 5ms loss 2%
sudo tc qdisc del dev lo root
```

프레임 ID를 얻는 가장 쉬운 방법: 송신에서 `identity` element의 시그널이나 RTP 시퀀스 번호를 쓰거나, 수신 쪽에서 `timeoverlay` 숫자를 OCR하는 대신 **fakesink `signal-handoffs`를 Python(PyGObject)으로 받아** 버퍼 PTS와 수신 시각을 기록한다 [확인 필요: Lima VM에 python3-gi 설치]. 기록한 `(frame_id, recv_time)`을 아래 `frame_stats()`에 넣는다.

### L2 · L3 · L10 공통 코드 (표준 라이브러리만, 끝에 자체 테스트)

```python
"""R06 실습용: 프레임 드롭/지터 분석 · cyclictest 히스토그램 파싱 · CoT 생성/검사 (표준 라이브러리만)."""
import statistics
import xml.etree.ElementTree as ET
from datetime import datetime, timedelta, timezone


def frame_stats(frames, nominal_fps=60.0, freeze_ms=100.0):
    """frames: [(frame_id, recv_time_s), ...] 수신 순서. 드롭 · 지터 · freeze 계산."""
    if len(frames) < 2:
        return {"received": len(frames), "dropped": 0, "drop_rate": 0.0,
                "jitter_ms": 0.0, "p99_interval_ms": 0.0, "freezes": 0}
    ids = [f for f, _ in frames]
    expected = ids[-1] - ids[0] + 1
    dropped = expected - len(set(ids))
    gaps = [(b - a) * 1000.0 for (_, a), (_, b) in zip(frames, frames[1:])]
    gaps_sorted = sorted(gaps)
    p99 = gaps_sorted[min(len(gaps_sorted) - 1, int(0.99 * len(gaps_sorted)))]
    return {
        "received": len(frames),
        "dropped": dropped,
        "drop_rate": dropped / expected,
        "jitter_ms": statistics.pstdev(gaps),
        "p99_interval_ms": p99,
        "freezes": sum(1 for g in gaps if g > freeze_ms),
        "nominal_ms": 1000.0 / nominal_fps,
    }


def parse_cyclictest_hist(text):
    """cyclictest --histogram 출력에서 (bucket_us -> 스레드별 count 합)과 Max 줄을 뽑는다."""
    hist, maxes = {}, []
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue
        if line.startswith("# Max Latencies:"):
            maxes = [int(x) for x in line.split(":", 1)[1].split()]
        elif line[0].isdigit():
            parts = line.split()
            hist[int(parts[0])] = sum(int(x) for x in parts[1:])
    return hist, maxes


def percentile_from_hist(hist, q):
    total = sum(hist.values())
    if total == 0:
        return None
    acc = 0
    for us in sorted(hist):
        acc += hist[us]
        if acc >= q * total:
            return us
    return max(hist)


def make_cot(uid, cot_type, lat, lon, hae, stale_s=10, now=None, callsign=None):
    now = now or datetime.now(timezone.utc)
    fmt = lambda t: t.strftime("%Y-%m-%dT%H:%M:%S.%fZ")[:-4] + "Z"
    ev = ET.Element("event", version="2.0", uid=uid, type=cot_type, how="m-g",
                    time=fmt(now), start=fmt(now), stale=fmt(now + timedelta(seconds=stale_s)))
    ET.SubElement(ev, "point", lat=f"{lat:.7f}", lon=f"{lon:.7f}", hae=f"{hae:.1f}", ce="5.0", le="10.0")
    det = ET.SubElement(ev, "detail")
    if callsign:
        ET.SubElement(det, "contact", callsign=callsign)
    return ET.tostring(ev, encoding="unicode")


def check_cot(xml_text):
    """최소 검사: 필수 속성 · 좌표 범위 · stale > time. 문제 목록을 돌려준다 (빈 목록 = 통과)."""
    problems = []
    try:
        ev = ET.fromstring(xml_text)
    except ET.ParseError as e:
        return [f"parse error: {e}"]
    if ev.tag != "event":
        problems.append("root is not <event>")
    for a in ("version", "uid", "type", "time", "start", "stale", "how"):
        if not ev.get(a):
            problems.append(f"missing attr {a}")
    pt = ev.find("point")
    if pt is None:
        problems.append("missing <point>")
    else:
        try:
            lat, lon = float(pt.get("lat")), float(pt.get("lon"))
            if not (-90 <= lat <= 90 and -180 <= lon <= 180):
                problems.append("lat/lon out of range")
        except (TypeError, ValueError):
            problems.append("bad lat/lon")
    try:
        p = lambda s: datetime.strptime(s, "%Y-%m-%dT%H:%M:%S.%fZ")
        if p(ev.get("stale")) <= p(ev.get("time")):
            problems.append("stale <= time")
    except (TypeError, ValueError):
        problems.append("bad timestamp format")
    return problems


def _self_test():
    # 60 fps, 프레임 5 · 6 누락, 프레임 21 앞에서 150 ms 정지
    frames, t = [], 0.0
    for fid in range(0, 40):
        if fid in (5, 6):
            t += 1 / 60
            continue
        t += 1 / 60 if fid != 21 else 0.150
        frames.append((fid, t))
    s = frame_stats(frames)
    assert s["dropped"] == 2, s
    assert s["freezes"] == 1, s
    assert s["p99_interval_ms"] >= 100, s

    sample = "\n".join(["# Histogram", "000000 000000 000000", "000005 000900 000950",
                        "000010 000090 000045", "000050 000010 000005",
                        "# Max Latencies: 00048 00051"])
    hist, maxes = parse_cyclictest_hist(sample)
    assert maxes == [48, 51], maxes
    assert percentile_from_hist(hist, 0.5) == 5
    assert percentile_from_hist(hist, 0.999) == 50

    now = datetime(2026, 10, 24, 18, 0, 0, tzinfo=timezone.utc)
    x = make_cot("TEST-UAS-1", "a-f-A", 33.8358, -118.3406, 120.0, now=now, callsign="T1")
    assert check_cot(x) == [], check_cot(x)
    bad = x.replace('lat="33.8358000"', 'lat="133.0"')
    assert "lat/lon out of range" in check_cot(bad)
    assert check_cot(x[:40])[0].startswith("parse error")
    print("self-test OK")


if __name__ == "__main__":
    _self_test()
```

- 이 코드는 2026-10-24 Mac의 `python3`에서 자체 테스트 통과(`self-test OK`)를 확인했다. 필요하면 `code/`로 옮겨 쓴다.
- cyclictest 히스토그램 줄 형식(`bucket count count ...`, `# Max Latencies:`)은 rt-tests 버전마다 조금 다를 수 있다 → 실제 출력으로 파서를 먼저 맞춘다 [확인 필요].
- CoT 검사는 **최소 검사**다. 진짜 시험은 MITRE Event.xsd로 스키마 검증을 해야 한다(표준 라이브러리 밖, 예: `xmllint --schema`).

### L3 cyclictest 실행

```text
# Lima VM — 무부하
sudo cyclictest --mlockall --smp --priority=80 --interval=200 --duration=5m --histogram=400 --quiet > idle.txt
# 부하 (다른 터미널)
stress-ng --cpu 0 --vm 2 --vm-bytes 256M --io 2 --timeout 6m
sudo cyclictest --mlockall --smp --priority=80 --interval=200 --duration=5m --histogram=400 --quiet > load.txt
```

VM 안의 지연은 하이퍼바이저 영향이 커서 **제품 판정용 숫자가 아니다**. 목적은 "측정 → 파싱 → p99.9/Max → 두 조건 비교 리포트" 흐름을 손에 익히는 것.

### L7 PX4 SITL + uXRCE-DDS (요약 절차)

1. Lima VM에 Docker, ROS 2(Humble/Jazzy) 컨테이너 준비.
2. PX4-Autopilot 클론 → `make px4_sitl` (시뮬레이터 없이 돌리는 옵션 확인 [확인 필요: headless 대상 이름]).
3. `MicroXRCEAgent udp4 -p 8888` 실행 → SITL이 client를 자동 시작(공식 문서).
4. `px4_msgs` 를 PX4 버전에 맞는 브랜치로 colcon 빌드.
5. `ros2 topic list | grep fmu` → `ros2 topic hz /fmu/out/vehicle_status` [확인 필요: 토픽 이름 · QoS 설정 필요 여부].
6. agent를 kill하고 다시 띄워 토픽이 돌아오는 데 걸리는 시간을 잰다 → 3.5.4 표의 소프트웨어 버전.

### 하드웨어 실습 (나중에, 사기 전에 Don과 예산 확인)

| 실습 | 필요한 것 | 비고 |
|---|---|---|
| H1 glass-to-glass (포토다이오드 + 스코프) | USB 카메라, 모니터, LED + MCU 보드, 포토다이오드 모듈, DSO 또는 로직 분석기 | Don은 DSO 사용 경험이 있다. analog FPV 카메라 · VTX · 고글로 확장 가능 |
| H2 analog vs digital FPV 지연 비교 | analog VTX/고글 + 디지털 시스템 | 전파 송신은 **면허 · 출력 규정** 확인 먼저 |
| H3 Raspberry Pi + RTL8812AU 두 장으로 WFB-ng | Pi 2대, 지원 Wi-Fi 어댑터 2개, (선택) 감쇠기 · 동축 | 실내에서는 감쇠기 · 동축 전도 시험 권장 |
| H4 Jetson 열 soak | Jetson 개발 키트 | 가장 비쌈 — 우선순위 낮음 |
| H5 OTA 전원 차단 | Pi/BeagleBone + RAUC 이미지, USB 릴레이 또는 프로그래머블 전원 | Yocto 빌드 필요 → 사전 빌드 이미지가 있는지 먼저 확인 |

안전: 영상 실습에 FC · 모터를 같이 붙일 때는 **프롭 제거**, 전류 제한 전원. LiPo는 충전 · 보관 규칙을 따른다.

---

## 12. 확인 안 된 것

| # | 항목 | 상태 |
|---|---|---|
| 1 | Neros companion이 Jetson인지, OS가 JetPack(Ubuntu)인지 Yocto인지 | [추정] — 공고에 Jetson/TensorRT는 Perception · Autonomy 쪽에 "such as"로만 나옴 |
| 2 | Neros 기체 이름 Archer / Archer AI, 지상 장비 Crossbow GCS, payload APEX | [Gemini 주장 · 미확인] |
| 3 | Neros가 자체 디지털 FPV 영상을 만드는지 | [추정] — CODEC 공고 "similar to DJI O4" 근거 |
| 4 | Neros OTA 프레임워크(Mender/RAUC/SWUpdate/OSTree/자체) | 공고는 "or homegrown equivalents" — 미확인 |
| 5 | secure boot 체인 · 키 관리 방식 | [확인 필요 for Neros] |
| 6 | ESC · BMS가 RTOS인지 bare-metal인지 | [추정] |
| 7 | ATAK 기본 멀티캐스트 239.2.3.1:6969, TAK Protobuf 협상 | [확인 필요] |
| 8 | 무인기용 CoT `type` 문자열(예: `a-f-A-M-F-Q`), `how` 값 의미 | [확인 필요] — MITRE 문서 · TAK 문서로 |
| 9 | trtexec 옵션 이름(`--percentile` 등) | [확인 필요] — 설치 버전의 `--help` |
| 10 | GstShark tracer의 정확한 `GST_TRACERS` 문자열 | [확인 필요] — RidgeRun 위키에는 tracer 이름만 확인 |
| 11 | PX4 offboard 손실 failsafe 파라미터 이름 · 기본값 | [확인 필요] |
| 12 | Yocto ptest 변수(`ptest-pkgs` 등), reproducible selftest 이름 | [확인 필요] — 사용 릴리스 문서로 |
| 13 | Buildroot `BR2_REPRODUCIBLE` 현재 지원 상태 | [확인 필요] |
| 14 | SWUpdate의 dual-copy 부트로더 변수 이름 | [확인 필요] |
| 15 | Zephyr 스택 사용량 shell 명령, NuttX `ostest` 실행 명령 | [확인 필요] |
| 16 | ChibiOS 테스트 스위트 현재 디렉터리 구조 · 생성 방식 | [확인 필요] |
| 17 | JetPack 버전별 커널 버전과 PREEMPT_RT 지원 방식 | [확인 필요] |
| 18 | OpenHD가 WFB-ng 코드를 어디까지 공유하는지, 지원 보드 목록 | [확인 필요] |
| 19 | LAVA device 설정의 전원/콘솔 명령 키 이름 | [확인 필요] |
| 20 | QGC 영상 기본 UDP 포트 5600 | [확인 필요] |

---

## 13. 참고 링크

공식 문서 · 저장소 (이번에 직접 열어 확인한 것은 ✔)

- ✔ PX4 uXRCE-DDS (ROS 2 bridge): https://docs.px4.io/main/en/middleware/uxrce_dds.html
- ✔ ArduPilot ROS 2 / DDS 설치 문서: https://ardupilot.org/dev/docs/ros2-install.html
- ✔ ArduPilot 4.5 릴리스 노트(MicroXRCEDDS 지원): https://discuss.ardupilot.org/t/rover-4-5-0-released/115615
- eProsima Micro XRCE-DDS: https://micro-xrce-dds.docs.eprosima.com/
- micro-ROS: https://micro.ros.org/
- ✔ ROS 2 launch_testing README: https://github.com/ros2/launch/tree/rolling/launch_testing
- ✔ rosbag2 (MCAP 기본 저장소 changelog): https://docs.ros.org/en/jazzy/p/ros2bag/__CHANGELOG.html
- ✔ mavlink-router: https://github.com/mavlink-router/mavlink-router
- ✔ NVIDIA trtexec/벤치마킹 문서: https://docs.nvidia.com/deeplearning/tensorrt/11.4.0/performance/benchmarking.html
- ✔ NVIDIA Jetson 전력 최적화(tegrastats): https://developer.nvidia.com/blog/power-optimization-with-nvidia-jetson
- ✔ GStreamer tracing 설계 문서(latency/stats/leaks tracer): https://gstreamer.freedesktop.org/documentation/additional/design/tracing.html
- ✔ GStreamer rtspsrc (latency 기본 2000 ms): https://gstreamer.freedesktop.org/documentation/rtsp/rtspsrc.html
- ✔ GstShark: https://github.com/RidgeRun/gst-shark · https://developer.ridgerun.com/wiki/index.php/GstShark
- ✔ WFB-ng: https://github.com/svpcom/wfb-ng
- ✔ OpenHD: https://openhdfpv.org/ · https://github.com/OpenHD/OpenHD
- ✔ Yocto Test Environment Manual — runtime testing: https://docs.yoctoproject.org/test-manual/runtime-testing.html
- ✔ Buildroot manual (runtime tests framework): https://nightly.buildroot.org/manual.html (원문: docs/manual/contribute.adoc)
- ✔ LAVA: https://docs.lavasoftware.org/lava/
- ✔ kselftest: https://docs.kernel.org/dev-tools/kselftest.html
- ✔ LTP kirk 전환 논의: https://lists.linux.it/pipermail/ltp/2023-September/034933.html · kirk: https://github.com/linux-test-project/kirk
- ✔ PREEMPT_RT 6.12 병합 보도: https://heise.de/-10061458 · https://en.wikipedia.org/wiki/PREEMPT_RT
- cyclictest (Linux Foundation RT wiki): https://wiki.linuxfoundation.org/realtime/documentation/howto/tools/cyclictest/start
- ✔ RAUC integration (U-Boot BOOT_ORDER/BOOT_x_LEFT, mark-good): https://rauc.readthedocs.io/en/latest/integration.html
- ✔ Mender U-Boot 수동 통합(bootcount): https://docs.mender.io/system-updates-yocto-project/board-integration/bootloader-support/u-boot/manual-u-boot-integration
- SWUpdate: https://sbabic.github.io/swupdate/
- ✔ Zephyr twister (원문 rst): https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/develop/twister/index.rst
- ✔ Zephyr ztest (원문 rst): https://github.com/zephyrproject-rtos/zephyr/blob/main/doc/develop/test/ztest.rst
- ✔ NuttX ostest: https://nuttx.apache.org/docs/latest/applications/testing/ostest/index.html
- FreeRTOS: https://github.com/FreeRTOS/FreeRTOS · https://github.com/FreeRTOS/FreeRTOS-Kernel
- ChibiOS: https://github.com/ChibiOS/ChibiOS · 포럼(테스트 스위트 출력): https://forum.chibios.org/viewtopic.php?p=41240
- ✔ MITRE CoT 문서: https://www.mitre.org/sites/default/files/pdf/09_4937.pdf
- ✔ CoT 메시지 구조 요약(FreeTAKServer, 3rd party): https://www.mintlify.com/FreeTAKTeam/FreeTakServer/api/cot/message-format
- ✔ QGroundControl unit tests: https://docs.qgroundcontrol.com/master/en/qgc-dev-guide/contribute/unit_tests.html · 명령줄 옵션: https://docs.qgroundcontrol.com/master/en/qgc-dev-guide/command_line_options.html

공고 근거

- `../research/job_board_snapshots/2026-10-23_neros_jobs.json` — Firmware Test Engineer, Senior Firmware Engineer Ground Software, Senior Embedded Linux Engineer, Senior Platform Engineer, Autonomy Platform & Runtime Lead, Perception Lead, Firmware Engineer - Tennessee Office, CODEC Video Compression Engineer, Ground Software Manager, Senior Software Engineer, UAV Systems Engineer (Ukraine)
- `../onsiteKit/plan/2026-10-23_K05_open_source_study_map.md` — 오픈소스 지도 · latency 파이프라인

다른 단원

- [R00 개요 · Gemini 정정 목록](2026-10-24_R00_overview_and_gemini_corrections.md)
- [R01 FTE vs SSD 검증](2026-10-24_R01_fte_vs_ssd_validation.md)
- [R02 드론 시스템 · 오픈소스 배치도](2026-10-24_R02_drone_system_and_open_source_map.md)
- [R03 Betaflight / INAV](2026-10-24_R03_betaflight_inav_internals_and_testing.md)
- [R04 PX4 / ArduPilot 테스트 생태계](2026-10-24_R04_px4_ardupilot_and_their_test_ecosystems.md)
- [R05 RC 링크 프로토콜](2026-10-24_R05_rc_link_protocols_elrs_edgetx.md)
- [R07 테스트 방법론 피라미드](2026-10-24_R07_test_methodology_pyramid.md)
- [R08 HIL 벤치 설계](2026-10-24_R08_hil_bench_design_instruments.md)
- [R09 스케일러블 테스트 플랫폼](2026-10-24_R09_scalable_reusable_test_platform.md)
- [R10 로드맵 · 랩 · 참고 자료](2026-10-24_R10_roadmap_labs_references.md)
