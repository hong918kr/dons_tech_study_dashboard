# Q6. ROS 2 on embedded — 노드·토픽·QoS·DDS·타임스탬프·실시간 설정을 임베디드 엔지니어 눈으로

> **이 노트를 다 읽으면**: ROS 2 그래프(node·topic·service·action·parameter)를 bare-metal 개념으로 설명할 수 있다 · rclcpp → rcl → rmw → DDS → UDP 스택과 discovery·domain·QoS 매칭을 그림으로 그릴 수 있다 · `sensor_msgs/Imu`를 올바른 `header.stamp`로 내보내는 C++ 패키지를 colcon으로 빌드하고 end-to-end 지연·지터·손실을 잴 수 있다 · RMW·QoS·부하·우선순위가 숫자를 어떻게 바꾸는지 직접 잰 값으로 말할 수 있다
> **왜 필요한가**: 로보틱스·드론·국방 로봇 회사(Scout AI, Neros, Nimble 등)의 JD에 "ROS 2 experience"가 거의 빠지지 않는다. 임베디드 엔지니어에게 묻는 것은 "토픽이 뭐냐"가 아니라 "IMU 드라이버 노드의 타임스탬프를 어디서 찍나", "왜 센서 토픽은 best-effort인가", "DDS가 멀티캐스트를 못 쓰는 네트워크에서 discovery는 어떻게 하나", "1 kHz 제어 루프를 ROS 2 위에 올리려면?"이다.
> **Don 기준 난이도**: 메시지 큐·ISR→태스크 전달·링버퍼 overflow·타임스탬프 정합은 SSD/RF 경험으로 이미 안다 / 새로 배울 것은 ROS 2의 용어와 계층, DDS discovery와 QoS 매칭 규칙, executor 모델, ROS 시간(`use_sim_time`), 그리고 빌드·배포 도구(colcon, rosbag2, meta-ros, micro-ROS)
> **선행 노트**: A9(소켓 — UDP·멀티캐스트), A7(IPC — 공유 메모리), F8(시간·타이머 — CLOCK_REALTIME vs MONOTONIC), I2(IIO — IMU 드라이버와 버퍼 타임스탬프), I5(V4L2 — 카메라 버퍼 타임스탬프), N2(실시간 튜닝 — 우선순위 지도, mlockall), N3(지연 측정), N4(Linux vs MCU 분할), K4/K5(Yocto), L8(컨테이너 — `--network host`와 DDS). 같은 Q 모듈의 Q3(멀티 센서 동기화·PTP), Q5(IMU·GNSS·LiDAR), Q7(데이터 품질 툴)과 맞물린다.

---

## 0. 큰 그림 — 로봇 회사의 "공통어"

SSD 펌웨어를 떠올려 보자. 호스트 인터페이스 태스크, FTL 태스크, NAND 채널 태스크, 백그라운드 GC 태스크가 있고, 이들은 **메시지 큐**(커맨드 디스크립터를 담은 링버퍼)로 이야기한다. 누가 누구에게 무엇을 보내는지는 펌웨어 설계 문서의 "태스크 다이어그램"에 그려져 있다.

로봇 소프트웨어도 똑같은 모양이다. 다만 태스크가 **프로세스(또는 스레드)이고, 여러 컴퓨터에 흩어져 있고, 수십 개 팀이 따로 만든다**. 그래서 "메시지 큐의 이름·타입·전달 규칙"을 표준화한 미들웨어가 필요하고, 그 사실상 표준이 **ROS 2**(Robot Operating System 2)다. ROS 2는 OS가 아니다. Linux 위에서 도는 **라이브러리 + 빌드 도구 + 메시지 정의 모음 + CLI 도구 세트**다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="14">센서 키트의 ROS 2 그래프 — 노드(상자)와 토픽(화살표 위 이름)</text>
<rect x="20" y="45" width="150" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="65" font-size="13">imu_driver</text>
<text x="30" y="82" font-size="12">IIO /dev/iio:device0</text>
<rect x="20" y="115" width="150" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="135" font-size="13">camera_node</text>
<text x="30" y="152" font-size="12">V4L2 /dev/video0</text>
<rect x="20" y="185" width="150" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="205" font-size="13">gnss_driver</text>
<text x="30" y="222" font-size="12">UART + PPS</text>
<rect x="20" y="255" width="150" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="275" font-size="13">can_bridge</text>
<text x="30" y="292" font-size="12">SocketCAN can0</text>
<rect x="300" y="80" width="150" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="310" y="100" font-size="13">state_estimator</text>
<text x="310" y="117" font-size="12">EKF (IMU+GNSS+odom)</text>
<rect x="300" y="175" width="150" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="310" y="195" font-size="13">perception</text>
<text x="310" y="212" font-size="12">GPU/NPU 추론</text>
<rect x="510" y="125" width="150" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="520" y="145" font-size="13">planner/control</text>
<text x="520" y="162" font-size="12">명령 생성</text>
<rect x="510" y="255" width="150" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="520" y="275" font-size="13">rosbag2 recorder</text>
<text x="520" y="292" font-size="12">모든 토픽 기록</text>
<line x1="170" y1="67" x2="298" y2="95" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="180" y="66" font-size="12">/imu/data_raw</text>
<line x1="170" y1="207" x2="298" y2="112" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="185" y="175" font-size="12">/fix</text>
<line x1="170" y1="137" x2="298" y2="190" stroke="currentColor" marker-end="url(#ah1)"/>
<text x="180" y="136" font-size="12">/image_raw</text>
<line x1="450" y1="102" x2="508" y2="140" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="450" y1="197" x2="508" y2="155" stroke="currentColor" marker-end="url(#ah1)"/>
<line x1="510" y1="160" x2="172" y2="270" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah1)"/>
<text x="330" y="250" font-size="12">/cmd_vel → CAN 프레임</text>
<line x1="450" y1="210" x2="508" y2="262" stroke="#888" stroke-dasharray="2 3" marker-end="url(#ah1)"/>
</svg>
```
그림 1 — 센서 키트의 ROS 2 그래프. 파란 상자(드라이버 노드)가 Don 같은 임베디드 엔지니어의 영역이다. 커널 드라이버(I2·I5·I10)가 만든 데이터를 ROS 메시지로 바꾸는 "경계 노드"가 품질(타임스탬프·QoS·지연)을 결정한다.

임베디드 엔지니어가 ROS 2에서 맡는 일은 대략 넷이다.

| 일 | 구체적으로 | 이 노트 절 |
|---|---|---|
| 드라이버 노드 | IIO/V4L2/UART/CAN을 읽어 `sensor_msgs`로 publish, 올바른 `header.stamp`·`frame_id`·단위 | §5, §6, §7 |
| 통신 튜닝 | QoS 선택, RMW(DDS 구현) 선택·설정, 멀티캐스트 없는 망에서 discovery | §2, §3, §11 |
| 실시간·성능 | executor 구조, 스레드 우선순위, 할당 없는 경로, 지연·지터 측정 | §4, §8, §12, §13 |
| 배포 | colcon 빌드, Yocto(meta-ros)·컨테이너 이미지, MCU 쪽 micro-ROS | §7, §14, §15 |

### 0.1 이 노트의 실험 환경과 꼭 기억할 경고

- 호스트: Mac M2 → Lima VM `elinux`(aarch64) → QEMU TCG 게스트 `-machine virt -cpu cortex-a57 -smp 2 -m 1024M~1280M`, 커널 `6.18.55-dom`(Image-dom, `CONFIG_PREEMPT=y`, PREEMPT_RT 아님), rootfs-dom(Ubuntu 24.04.5 + systemd 255 + **ROS 2 Jazzy** 바이너리: ros-base, demo_nodes_cpp/py, rmw_fastrtps_cpp(기본)·rmw_cyclonedds_cpp, sensor_msgs, v4l2_camera, colcon).
- **TCG 경고**: KVM 없이 명령어를 하나하나 번역해 돌린다. 게다가 이 VM 하나를 19명의 작성자가 동시에 쓰고 있어 VM 호스트 load average가 30 근처였다(6코어). 게스트 부팅 한 번에 10분이 넘고 커널이 `rcu_preempt detected stalls`를 찍을 정도다. 그래서 **이 노트의 모든 지연 숫자(µs·ms)는 실제 하드웨어의 숫자가 아니다**. 실제 Cortex-A53/A72 보드에서 같은 측정은 같은 머신 안 토픽 지연이 대략 수십~수백 µs 수준이다(기억 기반 — 보드에서 직접 재서 확인할 것). 이 노트에서 의미 있는 것은 **절대값이 아니라 조건 사이의 차이(부하 유무, QoS, RMW, 우선순위)와 측정 방법**이다.
- `ros-jazzy-ros2-tracing`(`ros2 trace` CLI)은 랩 이미지에 설치되지 않았다(패키지 설치 실패). 대신 `tracetools`와 `lttng`는 있어서 §13에서 LTTng를 직접 썼다(CTF 리더가 없어 이벤트 해석은 가이드). micro-ROS(§14)와 Yocto meta-ros(§15)는 가이드(실행 안 함)다.
- 게스트는 systemd 부팅 + 커널 인자 `systemd.run=`으로 테스트 스크립트를 자동 실행했다(`apparmor=0`으로 부팅 시간을 줄임). 메모리 1~1.28 GB.

---

## 1. ROS 2의 기본 단위 — bare-metal 언어로 번역하기

### 1.1 다섯 가지 통신 방식

| ROS 2 개념 | 정의 | bare-metal/RTOS 비유 | 언제 쓰나 |
|---|---|---|---|
| node | 그래프의 참여자 하나. 이름이 있고 토픽·서비스·파라미터를 가진다 | RTOS 태스크 하나 (단, 프로세스 경계를 넘을 수 있다) | 드라이버 하나, 알고리즘 하나 |
| topic | 이름 + 메시지 타입이 붙은 **1:N 비동기 스트림** | 메시지 큐 / 링버퍼 + 브로드캐스트 | 센서 데이터, 상태, 명령 스트림 |
| service | 요청 → 응답 **1:1 RPC** | 메일박스 요청 + 완료 이벤트 대기 | "캘리브레이션 해", "모드 바꿔" |
| action | 오래 걸리는 목표: goal → feedback 스트림 → result, 취소 가능 | 긴 커맨드(포맷, GC)에 진행률 콜백 + abort | "여기로 이동해", "도킹해" |
| parameter | 노드별 이름 있는 설정값, 런타임 조회·변경 가능 | 설정 레지스터 / NVM config 테이블 | 샘플레이트, 장치 경로, 게인 |

action은 내부적으로 토픽 2개(feedback, status) + 서비스 3개(send_goal, get_result, cancel_goal)로 만들어진 조합이다. 즉 기본 전송은 **토픽과 서비스 둘뿐**이고, 서비스도 DDS 위에서는 request 토픽 + reply 토픽 한 쌍으로 구현된다. 결국 모든 것이 "토픽 = DDS의 topic" 위에 선다.

### 1.2 launch — 시스템 부팅 스크립트

여러 노드를 한 번에 띄우고, 파라미터·이름 재매핑(remap)·네임스페이스를 주는 것이 **launch 파일**(Python, XML, YAML)이다. 펌웨어로 치면 "어느 태스크를 어떤 우선순위·스택·설정으로 만들지 정하는 init 테이블"이다. 제품에서는 systemd 유닛(L1)이 `ros2 launch`를 띄우고, launch가 노드들을 띄우는 2단 구조가 흔하다.

```python
# 가이드 예시 (실행 안 함): sensors.launch.py
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        Node(package="q6_imu", executable="imu_pub", name="imu",
             parameters=[{"rate": 200.0, "iio_dir": "/sys/bus/iio/devices/iio:device0"}],
             remappings=[("imu/data_raw", "/sensors/imu0/data_raw")]),
        Node(package="v4l2_camera", executable="v4l2_camera_node", name="cam0",
             parameters=[{"video_device": "/dev/video0"}]),
    ])
```

### 1.3 예제 1 — talker/listener (C++, Python)과 그래프 들여다보기

ROS 2 설치를 확인하는 "hello world"다. 무엇을 확인하나: 두 프로세스가 discovery로 서로를 찾고 `/chatter` 토픽으로 이야기하는지, 그리고 CLI로 그래프를 볼 수 있는지.

```sh
source /opt/ros/jazzy/setup.bash
ros2 run demo_nodes_cpp talker &          # publisher
ros2 run demo_nodes_cpp listener          # subscriber (다른 터미널)
ros2 node list; ros2 topic list -t; ros2 topic info -v /chatter
```

```text
# (C++) listener / talker 로그
[INFO] [1792341879.489824992] [listener]: I heard: [Hello World: 1]
[INFO] [1792341880.464021328] [listener]: I heard: [Hello World: 2]
[INFO] [1792341881.465413936] [listener]: I heard: [Hello World: 3]
[INFO] [1792341882.452572800] [listener]: I heard: [Hello World: 4]
[INFO] [1792341879.447071216] [talker]: Publishing: 'Hello World: 1'
[INFO] [1792341880.445551088] [talker]: Publishing: 'Hello World: 2'
[INFO] [1792341881.453813936] [talker]: Publishing: 'Hello World: 3'

# ros2 node list; ros2 topic list -t; ros2 topic info -v /chatter
/talker
/chatter [std_msgs/msg/String]
/parameter_events [rcl_interfaces/msg/ParameterEvent]
/rosout [rcl_interfaces/msg/Log]
Type: std_msgs/msg/String

Publisher count: 1

Node name: talker
Node namespace: /
Topic type: std_msgs/msg/String
Topic type hash: RIHS01_df668c740482bbd48fb39d76a70dfd4bd59db1288021743503259e948f6b1a18
Endpoint type: PUBLISHER
GID: 01.0f.7f.01.d2.00.b1.7b.00.00.00.00.00.00.14.03
QoS profile:
  Reliability: RELIABLE
  History (Depth): UNKNOWN
  Durability: VOLATILE
  Lifespan: Infinite
  Deadline: Infinite
  Liveliness: AUTOMATIC
  Liveliness lease duration: Infinite

Subscription count: 0

# (Python) demo_nodes_py listener
[INFO] [1792342694.566481152] [listener]: I heard: [Hello World: 0]
[INFO] [1792342694.605722848] [listener]: I heard: [Hello World: 1]
[INFO] [1792342694.637607776] [listener]: I heard: [Hello World: 2]
```

출력에서 볼 것: talker의 publish 시각(…879.447)과 listener 수신 시각(…879.489)의 차이가 약 40 ms다 — TCG 게스트라서 크다. `ros2 node list`에 `/talker`만 보이는 것은 listener가 `head -4`로 이미 끝난 뒤이기 때문이고, 같은 이유로 `Subscription count: 0`이다. 기본 QoS는 RELIABLE + VOLATILE, `History (Depth): UNKNOWN`은 history가 discovery 정보에 실리지 않는 로컬 정책이라서다. 타임스탬프 `1792341879.4`는 Unix epoch 초(CLOCK_REALTIME) — ROS 로그 시각도 §5의 그 시계다. Python listener는 0, 1, 2번을 수십 ms 간격으로 몰아서 받았다(1 Hz talker인데) — 시작이 느린 TCG에서 초기 메시지가 몰린 것으로 보이며 원인은 따로 확인하지 않았다. 값(시각, GID)은 실행마다 다르다.

`ros2 topic info -v`가 보여 주는 QoS 줄을 기억해 두자. §3에서 이 값들이 왜 맞아야 하는지 본다.

### 1.4 rclcpp와 rclpy — 두 클라이언트 라이브러리

| | rclcpp (C++) | rclpy (Python) |
|---|---|---|
| 용도 | 드라이버·제어·고빈도 데이터 | 도구, 오케스트레이션, 프로토타입 |
| 메시지 비용 | 직렬화만 | 직렬화 + Python 객체 변환 (큰 메시지에서 비쌈) |
| 동시성 | 진짜 멀티스레드 executor | GIL 때문에 콜백이 사실상 하나씩 |
| 임베디드 판단 | 센서 경로는 C++ | 센서 경로에는 쓰지 않는다 |

---

## 2. 레이어 스택 — rclcpp에서 UDP 패킷까지

### 2.1 직관

`publisher->publish(msg)` 한 줄이 아래로 내려가는 길은 SSD에서 "NVMe 커맨드 → FTL → 채널 드라이버 → NAND 버스"처럼 계층이 분명하다.

```svg
<svg viewBox="0 0 660 400" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="14">ROS 2 레이어 스택 — publish() 한 번이 지나가는 길</text>
<rect x="20" y="40" width="300" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="65" font-size="13">사용자 코드 (노드, 콜백)</text>
<rect x="20" y="88" width="145" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="113" font-size="13">rclcpp (C++)</text>
<rect x="175" y="88" width="145" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="185" y="113" font-size="13">rclpy (Python)</text>
<rect x="20" y="136" width="300" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="30" y="161" font-size="13">rcl (C) — 공통 로직, 시간, 파라미터</text>
<rect x="20" y="184" width="300" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="30" y="209" font-size="13">rmw (C 인터페이스) — 교체 지점</text>
<rect x="20" y="232" width="145" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="257" font-size="13">Fast DDS</text>
<rect x="175" y="232" width="145" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="185" y="257" font-size="13">Cyclone DDS</text>
<rect x="20" y="280" width="300" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="305" font-size="13">RTPS 프로토콜 + CDR 직렬화</text>
<rect x="20" y="328" width="145" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="30" y="353" font-size="13">UDP 소켓 (A9)</text>
<rect x="175" y="328" width="145" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="185" y="353" font-size="13">공유 메모리 (A7)</text>
<line x1="340" y1="60" x2="340" y2="360" stroke="currentColor" marker-end="url(#ah2)"/>
<text x="355" y="65" font-size="12">ROS 개념: node, topic, QoS 프로파일</text>
<text x="355" y="113" font-size="12">언어 바인딩, executor, 콜백 그룹</text>
<text x="355" y="161" font-size="12">rcl_publish(), ROS 시간, 이름 규칙</text>
<text x="355" y="209" font-size="12">RMW_IMPLEMENTATION 환경변수로 선택</text>
<text x="355" y="257" font-size="12">DDS: participant, writer, reader</text>
<text x="355" y="305" font-size="12">DATA, HEARTBEAT, ACKNACK 서브메시지</text>
<text x="355" y="353" font-size="12">같은 호스트면 SHM, 아니면 UDP</text>
<text x="355" y="385" font-size="12">커널: socket → UDP/IP → netdev (I6)</text>
</svg>
```
그림 2 — ROS 2 레이어 스택. rmw가 "HAL" 역할을 해서 DDS 구현을 바꿔 끼울 수 있다. 사용자 코드는 한 줄도 바뀌지 않는다.

### 2.2 DDS와 RTPS — 정의

- **DDS**(Data Distribution Service): OMG 표준 publish/subscribe 미들웨어 API. 핵심 객체는 DomainParticipant(프로세스 안의 참여자; Jazzy는 기본으로 **프로세스(context)당 participant 하나**), DataWriter(= ROS publisher), DataReader(= ROS subscription), Topic.
- **RTPS**(Real-Time Publish-Subscribe, DDSI-RTPS): DDS 구현끼리 서로 말할 수 있게 하는 **와이어 프로토콜**. UDP 위에 DATA(샘플), HEARTBEAT(writer가 "나는 seq 1..N을 갖고 있다"), ACKNACK(reader가 "seq k가 빠졌다") 같은 서브메시지를 실어 보낸다. reliable QoS의 재전송이 바로 HEARTBEAT/ACKNACK이다. SSD 쪽으로 치면 링크 계층의 ACK/NAK + replay buffer(PCIe DLLP)와 똑같은 발상이다.
- **CDR**: 메시지를 바이트열로 바꾸는 직렬화 규칙. `.msg` 파일에서 생성된 타입 지원 코드(rosidl)가 이 일을 한다.

### 2.3 discovery — 서로를 어떻게 찾나

DDS에는 브로커(중앙 서버)가 없다. 각 participant가 스스로 알린다.

1. **SPDP**(participant discovery): 각 participant가 주기적으로 "나 여기 있다, 내 주소는 이것"을 **멀티캐스트**(기본 239.255.0.1)로 뿌린다.
2. **SEDP**(endpoint discovery): 서로 알게 된 participant끼리 유니캐스트로 "나는 `/imu/data_raw`를 이런 타입·QoS로 쓴다/읽는다"를 교환한다.
3. 토픽 이름·타입이 같고 **QoS가 호환되면** writer와 reader가 매칭되고 데이터가 흐른다.

UDP 포트는 도메인 ID로 정해진다(RTPS 규격 기본값): `PB=7400, DG=250, PG=2`일 때

```text
discovery multicast port = 7400 + 250 x domain          (domain 0 → 7400, domain 7 → 9150)
user multicast port      = 7400 + 250 x domain + 1
discovery unicast port   = 7400 + 250 x domain + 10 + 2 x participant_id
user unicast port        = 7400 + 250 x domain + 11 + 2 x participant_id
```

그래서 `ROS_DOMAIN_ID`가 다르면 포트가 달라 **서로 보이지 않는다** — 같은 망의 로봇 두 대를 분리하는 가장 간단한 방법이다. 도메인은 0~101 정도가 안전하다(participant가 많으면 포트가 65535를 넘거나 Linux ephemeral 포트 범위 32768~와 겹친다; ROS 2 문서가 이 범위를 권장한다).

Jazzy에서 discovery 범위를 바꾸는 표준 환경변수(Iron부터; 예전 `ROS_LOCALHOST_ONLY`는 deprecated):

| 변수 | 값 | 효과 |
|---|---|---|
| `ROS_DOMAIN_ID` | 0~101 | 포트 공간 분리 |
| `ROS_AUTOMATIC_DISCOVERY_RANGE` | `SUBNET`(기본), `LOCALHOST`, `OFF`, `SYSTEM_DEFAULT` | 자동 discovery를 어디까지 할지 |
| `ROS_STATIC_PEERS` | `192.168.1.10;robot2.local` | 멀티캐스트 없이 유니캐스트로 찾을 상대 |

### 2.4 RMW 선택 — Fast DDS vs Cyclone DDS (+ Zenoh)

| | Fast DDS (`rmw_fastrtps_cpp`) | Cyclone DDS (`rmw_cyclonedds_cpp`) |
|---|---|---|
| 만든 곳 | eProsima | Eclipse (ADLINK 출신) |
| Jazzy 기본값 | 예 (Tier 1 기본) | Tier 1 대안 |
| 같은 호스트 전송 | 공유 메모리(SHM) 전송이 기본으로 켜짐 (`/dev/shm/fastrtps_*`) | 기본은 UDP loopback; iceoryx 기반 SHM은 별도 설정 |
| 설정 | XML 프로파일 (`FASTRTPS_DEFAULT_PROFILES_FILE`) | XML (`CYCLONEDDS_URI`) |
| 멀티캐스트 없는 망 | Discovery Server(중앙 서버 모드) 지원 | 피어 목록 + `AllowMulticast=false` |
| 현장 평판 | 기능 많음, 기본값이 무거움 | 작고 단순, Nav2·많은 로봇 회사가 선호 (기억 기반) |

Jazzy에는 Tier 2로 `rmw_zenoh_cpp`도 들어왔다(라우터 기반, Wi-Fi·WAN에서 discovery 트래픽이 적다 — 기억 기반, 랩에 없음). 결론: **RMW는 런타임에 환경변수 하나로 바꿀 수 있고, 같은 시스템의 모든 노드는 같은 RMW를 쓰는 것이 원칙**이다(다른 DDS 구현끼리도 RTPS로 통하긴 하지만 세부 호환은 보장되지 않는다).

```sh
export RMW_IMPLEMENTATION=rmw_cyclonedds_cpp   # 이 셸에서 띄우는 노드 전부
ros2 doctor --report | grep middleware
```

### 2.5 shared memory · zero-copy — 정확히 무엇이 "zero"인가

- Fast DDS SHM 전송: 직렬화된 바이트를 `/dev/shm` 세그먼트에 쓰고 reader가 읽는다. 소켓 copy는 사라지지만 **직렬화 1회 + 복사**는 남는다.
- **loaned message** + Fast DDS data-sharing 또는 Cyclone + iceoryx: 고정 크기(POD) 메시지 타입이면 미들웨어가 공유 메모리 버퍼를 빌려 주고(`publisher->borrow_loaned_message()`), 사용자가 그 안에 직접 쓴다 → 직렬화도 복사도 없는 진짜 zero-copy. 조건: 타입에 가변 길이 필드(string, 동적 배열)가 없어야 한다. `sensor_msgs/Image`는 `data`가 동적 배열이라 그대로는 안 된다.
- **intra-process communication**(rclcpp): 같은 프로세스 안 노드끼리 `unique_ptr`로 포인터만 넘김. 컴포넌트(composable node)를 한 프로세스에 모으는 이유다.

카메라 4대 × 1080p를 같은 SoC 안에서 넘기는 로봇이라면 이 차이가 CPU 수십 %다. 하지만 GPU/ISP 버퍼까지 zero-copy하려면 결국 dma-buf(O4, I5) 핸들을 넘기는 별도 경로(예: NVIDIA NITROS, 기억 기반)가 필요하다.

### 2.6 임베디드 네트워크에서의 멀티캐스트

DDS 기본 discovery는 멀티캐스트에 기댄다. 임베디드 현장에서 이게 깨지는 곳:

- Wi-Fi: 멀티캐스트가 최저 속도로 전송되고 손실이 많다 → discovery 폭풍, 늦은 연결.
- 관리형 스위치의 IGMP snooping: 쿼리어가 없으면 멀티캐스트가 끊긴다.
- 컨테이너 브리지(L8): 기본 브리지 네트워크는 멀티캐스트를 잘 못 넘긴다 → `--network host`.
- 차량 이더넷(100BASE-T1 + VLAN): 멀티캐스트 정책이 엄격하다.
- 게스트·VM 사이 NAT(이 랩의 `-netdev user`): 멀티캐스트 없음.

처방: `ROS_STATIC_PEERS`/Cyclone 피어 목록(유니캐스트), Fast DDS Discovery Server, 인터페이스 고정(Cyclone `NetworkInterface`), 그리고 필요 없는 트래픽을 domain으로 분리. §11에서 두 게스트를 멀티캐스트 가능한 가상 허브(`-netdev socket,mcast=`)로 묶어 실제로 본다.

---

## 3. QoS — "전달 규칙"을 토픽마다 정한다

### 3.1 직관

SSD 펌웨어에서 "호스트 커맨드 큐"와 "온도 로그 큐"의 정책은 다르다. 커맨드는 절대 잃으면 안 되고(재시도), 온도 로그는 최신 값만 있으면 되고 오래된 건 덮어써도 된다(overwrite ring). ROS 2의 QoS가 바로 이 정책을 **토픽의 양쪽 끝(publisher, subscriber)마다** 선언하는 것이다.

### 3.2 정책 하나씩

| 정책 | 값 | 뜻 | bare-metal 비유 |
|---|---|---|---|
| reliability | `RELIABLE` / `BEST_EFFORT` | 빠진 샘플을 재전송하나 | ACK/NAK + replay buffer vs fire-and-forget |
| durability | `VOLATILE` / `TRANSIENT_LOCAL` | 늦게 들어온 subscriber에게 지난 샘플을 주나 | 없음 vs "마지막 설정값을 retained로 보관" |
| history | `KEEP_LAST` / `KEEP_ALL` | 큐가 차면 오래된 것을 버리나 | overwrite ring vs 백프레셔 |
| depth | 정수 | KEEP_LAST 큐 길이 | 링버퍼 엔트리 수 |
| deadline | 기간 | "적어도 이 주기마다 샘플이 와야 한다" | 워치독 주기 |
| liveliness | `AUTOMATIC` / `MANUAL_BY_TOPIC` + lease | publisher가 살아 있음을 어떻게 증명하나 | heartbeat 신호 + 타임아웃 |
| lifespan | 기간 | 이보다 오래된 샘플은 버린다 | 데이터 유효 기간 |

### 3.3 매칭 규칙 — "요청(subscriber) ≤ 제공(publisher)"

DDS는 **Request-vs-Offered** 모델이다. publisher는 "이만큼은 보장한다"를 제공(offer)하고, subscriber는 "적어도 이만큼은 필요하다"를 요청(request)한다. 제공이 요청보다 같거나 강해야 연결된다.

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="14">QoS 호환표 — 행: publisher 제공, 열: subscription 요청</text>
<text x="20" y="55" font-size="13">reliability</text>
<rect x="180" y="40" width="150" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="190" y="58" font-size="12">sub BEST_EFFORT</text>
<rect x="340" y="40" width="150" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="350" y="58" font-size="12">sub RELIABLE</text>
<rect x="20" y="72" width="150" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="28" y="92" font-size="12">pub BEST_EFFORT</text>
<rect x="180" y="72" width="150" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="190" y="92" font-size="12">연결 (best effort)</text>
<rect x="340" y="72" width="150" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="350" y="92" font-size="12">연결 안 됨</text>
<rect x="20" y="108" width="150" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="28" y="128" font-size="12">pub RELIABLE</text>
<rect x="180" y="108" width="150" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="190" y="128" font-size="12">연결 (best effort)</text>
<rect x="340" y="108" width="150" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="350" y="128" font-size="12">연결 (reliable)</text>
<text x="20" y="178" font-size="13">durability</text>
<rect x="180" y="163" width="150" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="190" y="181" font-size="12">sub VOLATILE</text>
<rect x="340" y="163" width="150" height="26" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="350" y="181" font-size="12">sub TRANSIENT_LOCAL</text>
<rect x="20" y="195" width="150" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="28" y="215" font-size="12">pub VOLATILE</text>
<rect x="180" y="195" width="150" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="190" y="215" font-size="12">연결</text>
<rect x="340" y="195" width="150" height="30" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="350" y="215" font-size="12">연결 안 됨</text>
<rect x="20" y="231" width="150" height="30" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="28" y="251" font-size="12">pub TRANSIENT_LOCAL</text>
<rect x="180" y="231" width="150" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="190" y="251" font-size="12">연결 (과거 안 받음)</text>
<rect x="340" y="231" width="150" height="30" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="350" y="251" font-size="12">연결 + 과거 샘플</text>
<text x="510" y="92" font-size="12">요청이 더 강함</text>
<text x="510" y="110" font-size="12">→ 매칭 실패</text>
<text x="510" y="215" font-size="12">같은 규칙</text>
<text x="20" y="292" font-size="12">deadline: pub 주기 ≤ sub 주기여야 연결. liveliness: pub 종류·lease가 sub보다</text>
<text x="20" y="312" font-size="12">같거나 강해야 연결. history·depth·lifespan은 매칭에 영향 없음 (각자 로컬 큐 정책).</text>
</svg>
```
그림 3 — QoS 호환표. 빨간 칸 두 개가 현장에서 "토픽은 보이는데 데이터가 안 온다"의 대부분이다. 실패해도 에러가 아니라 경고 로그 한 줄과 "incompatible QoS" 이벤트뿐이라 놓치기 쉽다.

### 3.4 미리 정의된 프로파일

| 프로파일 | reliability | durability | history/depth | 용도 |
|---|---|---|---|---|
| default (`rclcpp::QoS(10)`) | RELIABLE | VOLATILE | KEEP_LAST 10 | 일반 토픽 |
| sensor data (`rclcpp::SensorDataQoS()`) | BEST_EFFORT | VOLATILE | KEEP_LAST 5 | 고빈도 센서 |
| services | RELIABLE | VOLATILE | KEEP_LAST 10 | 서비스 |
| parameters | RELIABLE | VOLATILE | KEEP_LAST 1000 | 파라미터 서비스 |
| `/tf_static`, map (관례) | RELIABLE | TRANSIENT_LOCAL | KEEP_LAST 1 | "latched" 정적 데이터 |

**왜 센서는 best-effort인가**: 200 Hz IMU에서 샘플 하나를 잃으면 다음 샘플이 5 ms 뒤에 온다. 잃은 샘플을 재전송받는 데 그보다 오래 걸린다면 재전송은 해롭다 — 큐가 밀리고 **최신 데이터가 늦게 도착**한다(head-of-line blocking). 제어·추정은 "늦은 정확한 값"보다 "제때 온 최신 값"이 낫다. 반대로 명령(`/cmd_vel`의 모드 전환), 설정, 지도는 reliable이다.

**함정**: 드라이버를 best-effort로 내보내면 `ros2 topic echo`(Jazzy 기본값은 "이미 있는 publisher에 자동으로 맞춤")나 `ros2 bag record`(기본으로 publisher QoS에 맞춤)는 받아 주지만, 사용자가 만든 reliable subscription은 **아무것도 못 받는다**. §3.5에서 직접 본다.

### 3.5 예제 2 — QoS 불일치와 transient_local late joiner

무엇을 확인하나: (1) best-effort publisher에 reliable로 구독하면 데이터가 0개이고 경고 한 줄만 남는다, (2) transient_local publisher(20 s마다 publish)에 **같은 순간** 두 subscription(transient_local, volatile)을 붙이면, transient_local은 다음 publish를 기다리지 않고 보관된 마지막 값을 바로 받는다.

```sh
ros2 run q6_imu imu_pub --ros-args -p best_effort:=true &
timeout 60 ros2 topic echo --qos-reliability reliable    /imu/data_raw --field header.stamp   # 0개
ros2 topic echo --qos-reliability best_effort /imu/data_raw --field header.stamp
ros2 topic pub -r 0.05 --qos-durability transient_local --qos-depth 1 /q6_cfg std_msgs/msg/String "{data: mode=survey}" &
python3 late_joiner.py        # 아래 코드
```

```python
# Late joiner: subscribe with TRANSIENT_LOCAL and VOLATILE at the same instant,
# measure time to the first sample. Publisher publishes every 20 s.
import time
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, DurabilityPolicy, ReliabilityPolicy
from std_msgs.msg import String

rclpy.init()
node = Node("q6_late_joiner")
first = {}
t0 = time.monotonic()

def make(name, dur):
    qos = QoSProfile(depth=1, durability=dur, reliability=ReliabilityPolicy.RELIABLE)
    def cb(msg):
        if name not in first:
            first[name] = time.monotonic() - t0
            print(f"{name:16s} first sample after {first[name]:6.2f} s: {msg.data}")
    return node.create_subscription(String, "/q6_cfg", cb, qos)

subs = [make("transient_local", DurabilityPolicy.TRANSIENT_LOCAL),
        make("volatile", DurabilityPolicy.VOLATILE)]
while len(first) < 2 and time.monotonic() - t0 < 60:
    rclpy.spin_once(node, timeout_sec=0.1)
node.destroy_node()
rclpy.shutdown()
```

```text
# (1) pub best_effort — reliable echo (60 s): 경고 1줄, 데이터 0개 / best_effort echo: 즉시 수신
[WARN] [1792356569.143118080] [_ros2cli_491]: New publisher discovered on topic '/imu/data_raw', offering incompatible QoS. No messages will be received from it. Last incompatible policy: RELIABILITY
echo(reliable) timeout-exit=124 lines=1
sec: 1792356626
nanosec: 392165472
---
# (2) late joiner — 마지막 publish 후 약 45 s 시점에 두 subscription 동시 생성
transient_local  first sample after   1.93 s: mode=survey
volatile         first sample after  17.21 s: mode=survey
```

출력에서 볼 것: (1) reliable echo는 60 s 동안 **경고 한 줄**이 전부였다(timeout exit 124). 에러로 끝나지 않는다는 점이 함정이다. (2) transient_local subscription은 1.93 s 만에(대부분 TCG에서의 discovery 시간) publisher가 보관한 마지막 값을 받았고, volatile은 다음 publish(20 s 주기)까지 17.2 s를 기다렸다. 처음에는 `ros2 topic echo --once`를 두 번 돌려 시간을 쟀는데, TCG에서 CLI 시작에만 20~30 s가 걸려 차이가 묻혔다(transient 29.8 s, volatile 20.0 s로 오히려 거꾸로 나옴). 측정 도구 자체의 시작 비용이 결과보다 크면 측정이 아니다 — 그래서 한 프로세스 안에서 두 subscription을 같은 순간 만드는 스크립트로 바꿨다.
### 3.6 deadline · liveliness — QoS로 하는 헬스 모니터

deadline과 liveliness는 "데이터가 안 온다"를 미들웨어가 감지해 **이벤트 콜백**으로 알려 주는 기능이다. Q7(데이터 품질 툴)의 1차 방어선이다.

- deadline: subscriber가 `deadline(15 ms)`를 요청하면, 15 ms 동안 새 샘플이 없을 때마다 `deadline_callback`이 불린다(`QOSDeadlineRequestedInfo.total_count` 증가). publisher 쪽에도 offered deadline missed 이벤트가 있다.
- liveliness: publisher 프로세스가 죽었거나 멈췄음을 lease duration 안에 알린다. `AUTOMATIC`은 participant가 살아 있으면 OK(프로세스 단위), `MANUAL_BY_TOPIC`은 publisher가 직접 `assert_liveliness()`하거나 publish해야 OK(노드 로직이 멈춘 것까지 잡음).

이 노트의 subscriber(§7)는 deadline을 `3 x 주기`로 걸고 놓친 횟수를 센다. 부하를 걸었을 때 실제로 늘어난다(§8).

---

## 4. Executor와 callback group — "누가 콜백을 언제 부르나"

### 4.1 직관

RTOS에서 ISR은 데이터를 큐에 넣고, 태스크가 큐를 꺼내 처리한다. ROS 2에서 미들웨어 수신 스레드(DDS 내부)가 ISR 역할, **executor**가 그 큐를 꺼내 사용자 콜백(subscription, timer, service, 이벤트)을 부르는 태스크 역할이다. `rclcpp::spin(node)`은 "SingleThreadedExecutor 하나를 만들고 이 스레드에서 영원히 돈다"는 뜻이다.

```svg
<svg viewBox="0 0 660 270" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="14">executor 모델 — DDS 수신 스레드는 "ISR", executor는 "태스크"</text>
<rect x="20" y="45" width="170" height="60" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="68" font-size="13">DDS 수신 스레드</text>
<text x="30" y="88" font-size="12">소켓/SHM에서 샘플 수신</text>
<rect x="230" y="45" width="170" height="60" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="240" y="68" font-size="13">reader 큐 (depth)</text>
<text x="240" y="88" font-size="12">+ guard condition 신호</text>
<rect x="440" y="45" width="200" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="450" y="68" font-size="13">executor 스레드</text>
<text x="450" y="88" font-size="12">wait set → 준비된 콜백 실행</text>
<line x1="190" y1="75" x2="228" y2="75" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="400" y1="75" x2="438" y2="75" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="20" y="140" width="300" height="110" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="30" y="162" font-size="13">SingleThreadedExecutor</text>
<text x="30" y="186" font-size="12">콜백이 하나씩 순서대로 실행</text>
<text x="30" y="208" font-size="12">긴 콜백 하나가 나머지를 전부 막는다</text>
<text x="30" y="230" font-size="12">(RTOS에서 태스크 하나에 모든 일)</text>
<rect x="340" y="140" width="300" height="110" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="350" y="162" font-size="13">MultiThreadedExecutor + 그룹</text>
<text x="350" y="186" font-size="12">MutuallyExclusive 그룹: 그룹 안은 직렬</text>
<text x="350" y="208" font-size="12">Reentrant 그룹: 같은 콜백도 병렬</text>
<text x="350" y="230" font-size="12">그룹이 다르면 다른 스레드에서 동시에</text>
</svg>
```
그림 4 — executor 모델. 콜백 하나가 실행되는 동안 같은 executor 스레드의 다른 콜백은 기다린다. 이것이 ROS 2 노드 지연의 가장 흔한 원인이다.

### 4.2 종류

| executor | 특징 | 임베디드 판단 |
|---|---|---|
| `SingleThreadedExecutor` | 스레드 하나, 콜백 직렬 | 단순, 결정적 순서. 기본값 |
| `MultiThreadedExecutor(n)` | 스레드 풀, callback group으로 동시성 제어 | 스레드 우선순위를 콜백별로 못 준다 |
| `StaticSingleThreadedExecutor` | 엔티티 목록을 매번 재구성하지 않아 오버헤드↓ (Jazzy에서는 deprecated 예고, 기억 기반) | 엔티티가 고정된 노드 |
| `rclcpp::experimental::executors::EventsExecutor` | wait set 대신 이벤트 큐 — CPU 사용 크게 감소 (Jazzy에 포함, 기억 기반) | 고빈도 토픽이 많은 노드 |

**실시간 처방(N2와 같은 원리)**: RT 콜백만 가진 executor를 **별도 스레드**로 만들고 그 스레드에 `SCHED_FIFO` + CPU affinity를 준다. 로그·파라미터·진단 콜백은 다른 executor(SCHED_OTHER)로 보낸다. callback group을 executor에 따로 붙이는 API가 `executor.add_callback_group(group, node->get_node_base_interface())`다.

```c
// 가이드 스케치 (실행 안 함) — RT 콜백 전용 executor 스레드
// auto rt_group = node->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive, false);
// ... create_subscription(..., options.callback_group = rt_group)
// rclcpp::executors::SingleThreadedExecutor rt_exec;
// rt_exec.add_callback_group(rt_group, node->get_node_base_interface());
// std::thread t([&]{ sched_param p{.sched_priority = 80};
//                    pthread_setschedparam(pthread_self(), SCHED_FIFO, &p);
//                    rt_exec.spin(); });
```

### 4.3 예제 3 — 느린 콜백이 빠른 타이머를 굶기는 것, callback group으로 풀기

무엇을 확인하나: 10 ms 타이머(제어 루프 역할)와 1 s마다 300 ms 일하는 타이머(로그 압축·계획 역할)를 한 executor에 넣었을 때 빠른 타이머의 간격이 어떻게 깨지는지, MultiThreadedExecutor + 서로 다른 MutuallyExclusive 그룹이면 어떻게 되는지. rclpy로 짠 것은 빠르게 돌려 보기 위해서다(파이썬은 GIL이 있어 C++보다 효과가 약하게 나온다).

```python
# A slow callback starves a fast timer in one thread; callback groups fix it.
import sys, time
import rclpy
from rclpy.node import Node
from rclpy.executors import SingleThreadedExecutor, MultiThreadedExecutor
from rclpy.callback_groups import MutuallyExclusiveCallbackGroup

mode = sys.argv[1]                      # "single" or "multi"
rclpy.init()
node = Node("q6_exec")
g_fast = MutuallyExclusiveCallbackGroup() if mode == "multi" else None
g_slow = MutuallyExclusiveCallbackGroup() if mode == "multi" else None
stamps = []

def fast():                             # wants to run every 10 ms (control loop)
    stamps.append(time.monotonic())

def slow():                             # 300 ms of "work" every 1 s (logging, planning)
    t = time.monotonic()
    while time.monotonic() - t < 0.3:
        pass

node.create_timer(0.010, fast, callback_group=g_fast)
node.create_timer(1.0, slow, callback_group=g_slow)
ex = SingleThreadedExecutor() if mode == "single" else MultiThreadedExecutor(2)
ex.add_node(node)
end = time.monotonic() + 5.0
while time.monotonic() < end:
    ex.spin_once(timeout_sec=0.05)
gaps = sorted(b - a for a, b in zip(stamps, stamps[1:]))
print(f"{mode}: fast callbacks={len(stamps)} (ideal 500) "
      f"gap p50={gaps[len(gaps)//2]*1e3:.1f} ms max={gaps[-1]*1e3:.1f} ms")
ex.shutdown()
rclpy.shutdown()
```

```text
$ python3 executor_demo.py single; python3 executor_demo.py multi
single: fast callbacks=382 (ideal 500) gap p50=10.0 ms max=310.8 ms
multi: fast callbacks=386 (ideal 500) gap p50=10.5 ms max=63.4 ms
The following exception was never retrieved: cannot use Destroyable because destruction was requested
```

출력에서 볼 것: 한 스레드(single)에서는 1 s마다 300 ms짜리 콜백이 도는 동안 10 ms 타이머가 전혀 못 돈다 — 최대 간격 **310.8 ms**. 콜백 그룹을 나누고 MultiThreadedExecutor를 쓰면 최대 간격이 **63.4 ms**로 줄었다. 0이 아닌 이유는 GIL이다: 느린 콜백이 순수 Python 루프라 GIL을 쥐고 있고, 인터프리터가 5 ms마다 GIL을 넘겨도 TCG에서는 스케줄이 거칠다. C++ rclcpp라면 두 스레드가 진짜 병렬로 돌아 거의 10 ms를 지킨다(기대값 — 이 노트에서 C++로는 재지 않음). 두 경우 모두 콜백 수가 500이 아니라 382/386인 것은 TCG가 10 ms 타이머 자체를 못 따라가서다. 마지막 줄의 "exception was never retrieved"는 rclpy MultiThreadedExecutor를 shutdown할 때 아직 실행 중이던 콜백 future가 남아서 나오는 경고로, 결과와는 무관하다. 교훈: **같은 executor 스레드에 "긴 일"과 "빠른 루프"를 섞지 말 것.**

---

## 5. 메시지 타임스탬프 — `header.stamp`는 어느 시계, 언제 찍나

### 5.1 직관 — "도착 시각"이 아니라 "측정 시각"

센서 퓨전(EKF)은 "IMU가 가속도를 **측정한 순간**"과 "카메라가 **노출한 순간**"을 같은 시간축에 놓고 계산한다. ROS 메시지 헤더의 `stamp`는 그 **측정 시각**이어야 한다. 노드가 메시지를 publish한 시각, subscriber가 받은 시각이 아니다. SSD로 치면 "커맨드 완료 시각"이 아니라 "NAND 읽기가 실제로 일어난 시각"을 로그에 남겨야 디버깅이 되는 것과 같다.

```text
std_msgs/Header
  builtin_interfaces/Time stamp     # int32 sec + uint32 nanosec
  string frame_id                   # 좌표계 이름 (예: imu_link, camera_optical_frame)
```

### 5.2 ROS 시간 — 세 가지 시계

| 시계 (`rcl_clock_type_t`) | 기반 | 언제 |
|---|---|---|
| `RCL_SYSTEM_TIME` | `CLOCK_REALTIME` (wall clock) | `rclcpp::Clock(RCL_SYSTEM_TIME)` |
| `RCL_STEADY_TIME` | `CLOCK_MONOTONIC` | 주기 측정, 타임아웃 |
| `RCL_ROS_TIME` (노드 기본) | `use_sim_time=false`면 SYSTEM_TIME과 같다. `true`면 `/clock` 토픽 값 | `node->now()`, 메시지 stamp |

즉 **`node->now()`는 기본적으로 CLOCK_REALTIME이다**. 커널 드라이버 쪽 타임스탬프는 대개 CLOCK_MONOTONIC이다(V4L2 버퍼 `V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC`, IIO `current_timestamp_clock` 기본 realtime이지만 바꿀 수 있음, evdev, CAN `SO_TIMESTAMPING` 등 — F8, I2, I5). 그래서 드라이버 노드는 **커널 타임스탬프를 ROS 시간으로 변환**해야 한다.

```c
// 가이드: MONOTONIC으로 찍힌 커널 타임스탬프 → ROS(REALTIME) 시간축으로 변환
// offset = REALTIME_now - MONOTONIC_now   (두 번 연달아 읽어 근사, 주기적으로 갱신)
// ros_stamp = kernel_mono_ts + offset
// 주의: NTP/PTP가 REALTIME을 step하면 offset이 튄다 → 퓨전 쪽에서 "시간 역행" 처리 필요 (Q3)
```

### 5.3 언제 찍나 — 세 가지 선택과 오차

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="14">측정에서 subscriber 콜백까지 — 어디서 찍고 어디까지 재나</text>
<line x1="30" y1="200" x2="660" y2="200" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="610" y="220" font-size="12">시간</text>
<line x1="60" y1="60" x2="60" y2="200" stroke="#3f9a6b"/>
<text x="40" y="52" font-size="12">① 측정</text>
<line x1="160" y1="60" x2="160" y2="200" stroke="#3f9a6b"/>
<text x="130" y="52" font-size="12">② IRQ/커널 ts</text>
<line x1="270" y1="60" x2="270" y2="200" stroke="#4a7bd0"/>
<text x="240" y="52" font-size="12">③ 노드 read</text>
<line x1="360" y1="60" x2="360" y2="200" stroke="#4a7bd0"/>
<text x="335" y="52" font-size="12">④ publish</text>
<line x1="500" y1="60" x2="500" y2="200" stroke="#e08a3c"/>
<text x="470" y="52" font-size="12">⑤ DDS 수신</text>
<line x1="600" y1="60" x2="600" y2="200" stroke="#e08a3c"/>
<text x="570" y="52" font-size="12">⑥ 콜백 now()</text>
<rect x="60" y="80" width="100" height="24" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="68" y="97" font-size="12">변환·FIFO</text>
<rect x="160" y="80" width="110" height="24" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="168" y="97" font-size="12">스케줄 지연</text>
<rect x="270" y="80" width="90" height="24" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="278" y="97" font-size="12">채우기</text>
<rect x="360" y="80" width="140" height="24" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="368" y="97" font-size="12">직렬화·전송</text>
<rect x="500" y="80" width="100" height="24" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="508" y="97" font-size="12">executor 대기</text>
<line x1="160" y1="130" x2="600" y2="130" stroke="#3f9a6b" marker-end="url(#ah5)"/>
<text x="250" y="125" font-size="12">stamp=② (가장 좋음): 커널 타임스탬프 사용</text>
<line x1="270" y1="160" x2="600" y2="160" stroke="#4a7bd0" marker-end="url(#ah5)"/>
<text x="290" y="155" font-size="12">stamp=③ (이 노트의 랩): read 직전 now()</text>
<line x1="360" y1="190" x2="600" y2="190" stroke="#d0564a" marker-end="url(#ah5)"/>
<text x="380" y="185" font-size="12">stamp=④ (나쁨): 직렬화 직전</text>
<text x="20" y="250" font-size="12">lat_sub가 재는 값 = ⑥ - stamp. 같은 머신, 같은 시계일 때만 의미가 있다.</text>
<text x="20" y="272" font-size="12">다른 머신이면 두 시계의 offset이 그대로 더해진다 → PTP/chrony 동기화가 먼저 (Q3).</text>
<text x="20" y="294" font-size="12">①~② 사이(센서 내부 지연, 필터 group delay)는 데이터시트 값으로 보정한다.</text>
</svg>
```
그림 5 — 지연 측정 경로. 스탬프를 늦게 찍을수록 "측정된 지연"은 작아 보이지만 퓨전 오차는 커진다. 좋은 드라이버는 ②(커널/하드웨어 타임스탬프)를 쓴다.

세 선택의 오차 크기를 감으로 잡자. 가속도가 100 m/s³로 변하는(진동이 심한) 장비에서 1 ms 타임스탬프 오차는 0.1 m/s² 가속도 오차와 같다. 회전 1 rad/s의 카메라에서 5 ms 오차는 0.005 rad ≈ 640 px 폭·60° FOV 기준 약 3 px다. "ms 단위 스탬프 오차 = 퓨전 품질 저하"라는 말을 숫자로 할 수 있어야 한다.

### 5.4 `use_sim_time` — rosbag 재생과 시뮬레이터

`use_sim_time:=true`면 노드의 ROS 시계는 `/clock` 토픽을 따른다. `/clock`을 받기 전에는 **0**이다. rosbag2 재생(`ros2 bag play --clock`)이나 Gazebo가 `/clock`을 publish한다. 함정: 일부 노드만 sim time이면 타임스탬프가 1970년과 지금으로 섞여 TF가 "extrapolation into the future" 같은 에러를 낸다. 시스템 전체가 같은 설정이어야 한다.

### 5.5 예제 4 — `node.get_clock().now()`는 어느 시계인가

무엇을 확인하나: 기본 노드 시계가 CLOCK_REALTIME과 같고 MONOTONIC과는 다른지, `use_sim_time=true`이면 0인지.

```python
# Which clock is node.get_clock().now()?  Compare with REALTIME / MONOTONIC.
import time, sys
import rclpy
from rclpy.node import Node
from rclpy.parameter import Parameter

rclpy.init()
sim = len(sys.argv) > 1 and sys.argv[1] == "sim"
node = Node("q6_clock", parameter_overrides=[
    Parameter("use_sim_time", Parameter.Type.BOOL, sim)])
ros_ns = node.get_clock().now().nanoseconds
rt_ns = time.clock_gettime_ns(time.CLOCK_REALTIME)
mono_ns = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
print(f"use_sim_time={sim} clock_type={node.get_clock().clock_type}")
print(f"ros now      = {ros_ns/1e9:.6f} s")
print(f"REALTIME     = {rt_ns/1e9:.6f} s  (ros - realtime = {(ros_ns-rt_ns)/1e6:+.3f} ms)")
print(f"MONOTONIC    = {mono_ns/1e9:.6f} s  (= time since boot)")
node.destroy_node()
rclpy.shutdown()
```

```text
$ python3 clock_check.py; python3 clock_check.py sim
use_sim_time=False clock_type=1
ros now      = 1792342920.810048 s
REALTIME     = 1792342920.810318 s  (ros - realtime = -0.270 ms)
MONOTONIC    = 431.629671 s  (= time since boot)
use_sim_time=True clock_type=1
ros now      = 0.000000 s
REALTIME     = 1792342926.223482 s  (ros - realtime = -1792342926223.482 ms)
MONOTONIC    = 437.042783 s  (= time since boot)
```

출력에서 볼 것: `clock_type=1`은 `ClockType.ROS_TIME`이다. `use_sim_time=False`일 때 ROS 시간은 CLOCK_REALTIME과 0.27 ms 차이(두 번의 시계 읽기 사이에 걸린 시간 — 같은 시계다). MONOTONIC은 부팅 후 431 s로 완전히 다른 축이다. `use_sim_time=True`이면 `/clock`을 받기 전이라 ROS 시간이 **0**이다 — 이 상태에서 publish한 메시지의 stamp는 0(1970년)이 된다. 값은 실행마다 다르다.

---

## 6. sensor_msgs — 임베디드 엔지니어가 채우는 메시지들

필드를 외울 필요는 없지만 **단위·좌표계·"모름" 표시법**은 정확히 알아야 한다. 규약 문서: REP-103(단위·좌표계: SI 단위, 오른손 좌표, x 앞 y 왼쪽 z 위), REP-105(좌표 프레임 이름: map, odom, base_link), REP-145(IMU 드라이버 규약).

| 메시지 | 핵심 필드 | 단위·규칙 | 드라이버 쪽 함정 |
|---|---|---|---|
| `Imu` | `orientation`(quaternion), `angular_velocity`, `linear_acceleration`, 각각 3x3 `_covariance` | rad/s, m/s² (g 아님), 정지 시 z 가속도 ≈ +9.81 | 방향 추정이 없으면 `orientation_covariance[0] = -1`. 공분산 모르면 0이 아닌 합리적 대각값. IIO `mount_matrix` 적용(I2) |
| `Image` | `height`, `width`, `encoding`("rgb8", "yuv422", "mono16"…), `is_bigendian`, `step`(행 바이트), `data` | `step >= width x 바이트/픽셀` (패딩 포함 가능) | V4L2 `bytesperline`를 그대로 `step`에. 큰 메시지 → 복사 비용 |
| `CameraInfo` | K, D, R, P 행렬, `distortion_model` | 픽셀 단위 | 같은 `header.stamp`로 Image와 짝 |
| `PointCloud2` | `fields`(name/offset/datatype/count), `point_step`, `row_step`, `data`, `is_dense` | m | LiDAR UDP 패킷 → 포인트 배열 변환(Q5). 패딩·엔디언 기술을 `fields`로 |
| `NavSatFix` | `status`(fix 종류), `latitude`/`longitude`(deg), `altitude`(m, WGS84 타원체), `position_covariance` + type | deg, m | 고도는 해발(MSL)이 아니라 타원체 기준. PPS로 시간 동기(Q3) |
| `Temperature`, `FluidPressure`, `MagneticField` | 값 + variance | °C, Pa, Tesla | Gauss 아님 (1 G = 1e-4 T) |

`Imu`의 크기는 직렬화 기준 약 330바이트(헤더 + 쿼터니언 4 + 벡터 3x2 + 공분산 9x3, 모두 double)다. 예제 6의 `ros2 topic bw` 출력으로 확인한다.
---

## 7. 랩 — `sensor_msgs/Imu` 드라이버 노드와 지연 측정 subscriber를 colcon으로

### 7.1 colcon 워크스페이스 구조

colcon은 "여러 패키지를 의존 순서대로 빌드하는 메타 빌드 도구"다. 각 패키지는 자기 빌드 시스템(ament_cmake = CMake + ROS 매크로, 또는 ament_python)을 갖고, colcon은 그것을 순서대로 부른 뒤 결과를 `install/`에 모은다. Yocto의 bitbake가 레시피를 의존 순서로 돌리는 것의 축소판이다(K4).

```text
ws/
  src/q6_imu/
    package.xml          # 이름, 의존성 (rosdep·colcon·meta-ros가 읽는다)
    CMakeLists.txt       # ament_cmake 빌드
    src/imu_pub.cpp      # IIO -> sensor_msgs/Imu publisher
    src/lat_sub.cpp      # 지연·지터·손실 측정 subscriber
  build/ install/ log/   # colcon이 만든다
```

```text
<?xml version="1.0"?>
<package format="3">
  <name>q6_imu</name>
  <version>0.1.0</version>
  <description>Q6 IMU publisher + latency subscriber</description>
  <maintainer email="don@example.com">don</maintainer>
  <license>Apache-2.0</license>
  <buildtool_depend>ament_cmake</buildtool_depend>
  <depend>rclcpp</depend>
  <depend>sensor_msgs</depend>
  <export><build_type>ament_cmake</build_type></export>
</package>
```

```make
cmake_minimum_required(VERSION 3.16)
project(q6_imu CXX)
find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(sensor_msgs REQUIRED)
foreach(exe imu_pub lat_sub)
  add_executable(${exe} src/${exe}.cpp)
  target_compile_options(${exe} PRIVATE -Wall -Wextra -O2)
  ament_target_dependencies(${exe} rclcpp sensor_msgs)
  install(TARGETS ${exe} DESTINATION lib/${PROJECT_NAME})
endforeach()
ament_package()
```

`install(... DESTINATION lib/${PROJECT_NAME})`가 중요하다 — `ros2 run q6_imu imu_pub`은 `install/q6_imu/lib/q6_imu/imu_pub`을 찾는다.

### 7.2 publisher — IIO dummy에서 읽어 `sensor_msgs/Imu`로

무엇을 하는 코드인가: I2에서 본 `iio_dummy`(configfs로 만든 가짜 IIO 장치)의 `in_accel_x_raw`를 sysfs로 읽어 x 가속도에 넣고, 나머지 축은 합성값으로 채워 200 Hz로 publish한다. QoS(reliable/best-effort, depth, offered deadline)는 파라미터로 바꾼다. **stamp는 read 직전에** 찍는다(그림 5의 ③).

```c
// q6_imu/imu_pub: IIO dummy (or synthetic) -> sensor_msgs/Imu at <rate> Hz
#include <cmath>
#include <fstream>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/imu.hpp>
using namespace std::chrono_literals;

class ImuPub : public rclcpp::Node {
public:
  ImuPub() : Node("q6_imu_pub") {
    double rate = declare_parameter("rate", 200.0);
    iio_ = declare_parameter("iio_dir", std::string(""));
    bool be = declare_parameter("best_effort", false);
    int depth = declare_parameter("depth", 10);
    rclcpp::QoS qos(depth);                       // history KEEP_LAST(depth)
    if (be) qos.best_effort(); else qos.reliable();
    int dl_ms = declare_parameter("deadline_ms", 0);    // offered deadline
    if (dl_ms > 0) qos.deadline(std::chrono::milliseconds(dl_ms));
    pub_ = create_publisher<sensor_msgs::msg::Imu>("imu/data_raw", qos);
    timer_ = create_wall_timer(std::chrono::duration<double>(1.0 / rate),
                               [this] { tick(); });
    RCLCPP_INFO(get_logger(), "rate=%.0f Hz qos=%s depth=%d iio='%s'", rate,
                be ? "best_effort" : "reliable", depth, iio_.c_str());
  }
private:
  double read_accel_x() {                         // IIO sysfs: raw * scale
    if (iio_.empty()) return 0.0;
    std::ifstream f(iio_ + "/in_accel_x_raw");
    long raw = 0;
    f >> raw;
    return raw * 0.001;                           // dummy: assume 1 mg/LSB
  }
  void tick() {
    auto msg = sensor_msgs::msg::Imu();
    msg.header.stamp = now();      // stamp at acquisition, BEFORE the read
    msg.header.frame_id = "imu_link";
    double t = n_++ * 0.005;
    msg.linear_acceleration.x = read_accel_x();
    msg.linear_acceleration.y = 0.1 * std::sin(t);
    msg.linear_acceleration.z = 9.80665;
    msg.angular_velocity.z = 0.2 * std::cos(t);
    msg.orientation_covariance[0] = -1.0;         // "no orientation estimate"
    pub_->publish(msg);
  }
  std::string iio_;
  uint64_t n_ = 0;
  rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ImuPub>());
  rclcpp::shutdown();
  return 0;
}
```

읽을 점:
- `msg.header.stamp = now()`를 read 앞에 둔 이유: 측정 시각에 가장 가까운 순간. 실제 드라이버라면 IIO 버퍼의 timestamp 채널을 쓴다(§20 연습 3).
- `orientation_covariance[0] = -1.0`: REP-145 규약 — "방향 추정값 없음". 이걸 빼면 퓨전 노드가 기본 quaternion (0,0,0,1)을 진짜 자세로 믿는다.
- 단위: m/s². `iio_dummy`의 raw 값(34)에 가정한 스케일(1 mg/LSB → 0.001)을 곱했는데, 이것은 데모일 뿐이다. 실제 IIO 드라이버는 `in_accel_scale`을 곱하면 바로 m/s²가 나온다(IIO ABI가 가속도를 m/s²로 정의, I2).
- 메시지를 매 tick마다 새로 만든다 — RT 관점에서는 멤버로 두고 재사용하는 편이 낫다(§12).

### 7.3 subscriber — `now() - header.stamp`의 분포

무엇을 하는 코드인가: 같은 머신에서 publisher가 찍은 stamp와 수신 콜백의 `now()` 차이(end-to-end 지연), 수신 간격(지터), stamp 간격 구멍(손실 추정), deadline 이벤트 횟수를 `duration`초 동안 모아 백분위로 출력한다. 아무것도 안 오면 `3 x duration` 뒤 n=0으로 끝난다(QoS 불일치를 잡기 위해 일부러 넣었다 — §7.6).

```c
// q6_imu/lat_sub: end-to-end latency (now - header.stamp), jitter, gaps
#include <algorithm>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/imu.hpp>
using Imu = sensor_msgs::msg::Imu;

class LatSub : public rclcpp::Node {
public:
  LatSub() : Node("q6_lat_sub") {
    bool be = declare_parameter("best_effort", false);
    int depth = declare_parameter("depth", 10);
    dur_ = declare_parameter("duration", 20.0);
    period_ns_ = static_cast<int64_t>(1e9 / declare_parameter("rate", 200.0));
    int dl_ms = declare_parameter("deadline_ms", 0);   // 0 = no deadline
    rclcpp::QoS qos(depth);
    if (be) qos.best_effort(); else qos.reliable();
    rclcpp::SubscriptionOptions opt;
    if (dl_ms > 0) {
      qos.deadline(std::chrono::milliseconds(dl_ms));
      opt.event_callbacks.deadline_callback =
          [this](rclcpp::QOSDeadlineRequestedInfo &) { deadline_miss_++; };
    }
    sub_ = create_subscription<Imu>("imu/data_raw", qos,
        [this](Imu::ConstSharedPtr m) { on_msg(*m); }, opt);
    start_ = now().nanoseconds();
    timer_ = create_wall_timer(std::chrono::milliseconds(500), [this] {
      int64_t t = now().nanoseconds();
      if ((t0_ && t - t0_ > dur_ * 1e9) || t - start_ > 3 * dur_ * 1e9) {
        report(); rclcpp::shutdown(); } });
  }
private:
  void on_msg(const Imu &m) {
    int64_t now_ns = now().nanoseconds();
    int64_t st = rclcpp::Time(m.header.stamp).nanoseconds();
    if (t0_ == 0) t0_ = now_ns;   // measurement window starts at 1st sample
    lat_.push_back((now_ns - st) / 1000);                 // us
    if (last_st_ && st - last_st_ > period_ns_ * 3 / 2)   // gap in stamps
      lost_ += (st - last_st_ + period_ns_ / 2) / period_ns_ - 1;
    if (last_rx_) gap_.push_back((now_ns - last_rx_) / 1000);
    last_st_ = st; last_rx_ = now_ns;
  }
  static int64_t pct(std::vector<int64_t> v, double p) {   // percentile (copy)
    if (v.empty()) { return -1; } std::sort(v.begin(), v.end());
    return v[std::min(v.size() - 1, size_t(p * v.size()))];
  }
  void report() {
    RCLCPP_INFO(get_logger(), "n=%zu lost=%ld deadline_miss=%ld | latency us p50=%ld"
      " p99=%ld max=%ld | inter-arrival us p50=%ld p99=%ld max=%ld",
      lat_.size(), lost_, deadline_miss_, pct(lat_, .5), pct(lat_, .99),
      pct(lat_, 1.0), pct(gap_, .5), pct(gap_, .99), pct(gap_, 1.0));
  }
  double dur_; int64_t period_ns_, start_, t0_ = 0, last_st_ = 0, last_rx_ = 0, lost_ = 0, deadline_miss_ = 0;
  std::vector<int64_t> lat_, gap_;
  rclcpp::Subscription<Imu>::SharedPtr sub_; rclcpp::TimerBase::SharedPtr timer_;
};

int main(int argc, char **argv) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<LatSub>()); return 0;
}
```

### 7.4 예제 5 — colcon 빌드

```sh
cd /ws/ws && colcon build --parallel-workers 1 --cmake-args -DCMAKE_BUILD_TYPE=Release
source install/setup.bash && ls install/q6_imu/lib/q6_imu
```

```text
# (1) 첫 빌드 — VM 호스트 load average 15~30(다른 작성자 18명 동시 작업)일 때
real	11m14.386s
user	8m49.140s
sys	1m32.580s
colcon exit=0
Finished <<< q6_imu [10min 53s]
Summary: 1 package finished [10min 59s]
# (2) 다시 빌드 — VM 호스트가 거의 idle일 때
real	3m8.290s
user	2m36.738s
sys	0m27.259s
colcon exit=0
Finished <<< q6_imu [3min 2s]
Summary: 1 package finished [3min 4s]
-rwxr-xr-x 1 root root 271648 Oct 18 20:44 imu_pub
-rwxr-xr-x 1 root root 898264 Oct 18 20:46 lat_sub
# (3) 노트에 실린 최종 소스로 다시 빌드 + 경고 수 확인
real	2m21.377s
user	2m52.821s
sys	0m33.011s
colcon exit=0
Summary: 1 package finished [2min 16s]
warnings: 0
```

(2)의 빌드에서는 경고가 1개 있었다: 한 줄에 `if (v.empty()) return -1; std::sort(...)`를 쓴 것에 대한 `-Wmisleading-indentation`. 중괄호를 넣어 고친 것이 위 소스이고, (3)에서 경고 0개를 확인했다.

출력에서 볼 것: 파일 두 개짜리 패키지인데 TCG에서 3~11분이 걸렸다. rclcpp 헤더가 거대해서(템플릿) 실제 보드에서도 C++ 노드 하나 컴파일에 수십 초가 흔하다. 그래서 현장에서는 **보드에서 빌드하지 않고** x86 빌드 서버에서 크로스 빌드(Yocto SDK, 또는 arm64 컨테이너 + QEMU user / 네이티브 arm64 CI 러너)한다(B1, K5).

### 7.5 예제 6 — `ros2 topic hz / bw / delay / echo`로 드라이버 검증

무엇을 확인하나: 새 드라이버 노드를 올렸을 때 가장 먼저 보는 네 숫자 — 실제 주기, 대역폭, header stamp 기준 지연, 내용(단위·frame_id·공분산).

```sh
modprobe iio_dummy; mount -t configfs none /sys/kernel/config
mkdir /sys/kernel/config/iio/devices/dummy/q6imu          # I2와 같은 방법
ros2 run q6_imu imu_pub --ros-args -p iio_dir:=/sys/bus/iio/devices/iio:device0 &
ros2 topic hz /imu/data_raw;  ros2 topic bw /imu/data_raw
ros2 topic delay /imu/data_raw; ros2 topic echo --once /imu/data_raw
```

```text
$ ros2 run q6_imu imu_pub --ros-args -p iio_dir:=$D &
[INFO] [1792342767.011769456] [q6_imu_pub]: rate=200 Hz qos=reliable depth=10 iio='/sys/bus/iio/devices/iio:device0'
$ ros2 topic hz /imu/data_raw        (마지막 두 창만)
average rate: 196.487
	min: 0.001s max: 0.035s std dev: 0.00185s window: 9264
average rate: 196.562
	min: 0.001s max: 0.035s std dev: 0.00184s window: 9465
$ ros2 topic bw /imu/data_raw
	Message size mean: 0.32 KB min: 0.32 KB max: 0.32 KB
64.59 KB/s from 100 messages
	Message size mean: 0.32 KB min: 0.32 KB max: 0.32 KB
$ ros2 topic delay /imu/data_raw     (마지막 창만)
average delay: 0.008
	min: 0.002s max: 0.061s std dev: 0.00651s window: 8003
$ ros2 topic echo --once /imu/data_raw   (head -40에서 잘림, 0.0만 있는 공분산 줄 일부 생략 표시)
header:
  stamp:
    sec: 1792342962
    nanosec: 539899392
  frame_id: imu_link
orientation:
  x: 0.0
  y: 0.0
  z: 0.0
  w: 1.0
orientation_covariance:
- -1.0
... (나머지 8개 모두 0.0)
angular_velocity:
  x: 0.0
  y: 0.0
  z: -0.09552478061807507
angular_velocity_covariance:
- 0.0
... (나머지 8개도 0.0)
linear_acceleration:
  x: 0.034
  y: -0.08785644012801314
  z: 9.80665
linear_acceleration_covariance:
- 0.0
$ ros2 topic info -v /imu/data_raw
Type: sensor_msgs/msg/Imu
Publisher count: 1
Node name: q6_imu_pub
Node namespace: /
Topic type: sensor_msgs/msg/Imu
Topic type hash: RIHS01_7d9a00ff131080897a5ec7e26e315954b8eae3353c3f995c55faf71574000b5b
Endpoint type: PUBLISHER
GID: 01.0f.7f.01.de.00.f9.e3.00.00.00.00.00.00.14.03
QoS profile:
  Reliability: RELIABLE
  History (Depth): UNKNOWN
  Durability: VOLATILE
  Lifespan: Infinite
  Deadline: Infinite
  Liveliness: AUTOMATIC
  Liveliness lease duration: Infinite
Subscription count: 0
```

출력에서 볼 것:
- `hz` 평균 196.5 Hz — 200이 아니다. wall timer가 TCG에서 가끔 늦게 깨어나 tick을 건너뛰기 때문(최대 간격 35 ms). 실제 보드에서도 "타이머 기반 폴링 드라이버"는 이렇게 평균이 목표보다 살짝 낮게 나온다. 하드웨어 data-ready 인터럽트 + IIO 버퍼를 쓰면 주기는 센서 클럭이 결정한다.
- `bw` 메시지 크기 0.32 KB — §6에서 계산한 약 330바이트와 맞는다. 200 Hz면 약 64 KB/s.
- `delay`는 **수신 시각 - header.stamp**(ros2cli가 계산). 평균 8 ms는 Python CLI 자체의 처리 지연이 섞인 값이다(C++ lat_sub는 아래에서 2 ms). 도구 자체의 비용을 빼고 읽어야 한다.
- `echo`: `orientation.w: 1.0`(메시지 기본값), `orientation_covariance[0] = -1`, z 가속도 9.80665, x = 0.034(= raw 34 x 0.001).
- `topic info -v`: `Subscription count: 0`이 찍혔다 — 그 순간 구독자가 없었다. History(Depth)가 `UNKNOWN`으로 보이는 것은 DDS discovery 정보에 history가 실리지 않기 때문이다(history는 로컬 정책, 그림 3 아래 설명).

### 7.6 예제 7 — 지연 측정, 그리고 QoS 불일치가 실제로 어떻게 보이나

무엇을 확인하나: (1) 조용한 상태의 end-to-end 지연 분포, (2) subscriber가 deadline을 요청했는데 publisher가 제공하지 않으면, (3) publisher가 deadline 10 ms를 제공하면, (4) best-effort publisher에 reliable subscriber.

```sh
ros2 run q6_imu lat_sub --ros-args -p duration:=20.0
ros2 run q6_imu lat_sub --ros-args -p duration:=10.0 -p deadline_ms:=15        # pub: deadline 없음
ros2 run q6_imu imu_pub --ros-args -p deadline_ms:=10 &                          # pub: 10 ms 제공
ros2 run q6_imu lat_sub --ros-args -p duration:=20.0 -p deadline_ms:=15
ros2 run q6_imu imu_pub --ros-args -p best_effort:=true &
ros2 run q6_imu lat_sub --ros-args -p duration:=10.0                             # reliable 요청
```

```text
# (1) 조용한 상태, reliable, 20 s
[INFO] [1792356384.983051984] [q6_imu_pub]: rate=200 Hz qos=reliable depth=10 iio='/sys/bus/iio/devices/iio:device0'
[INFO] [1792356425.089599664] [q6_lat_sub]: n=4076 lost=29 deadline_miss=0 | latency us p50=2024 p99=4140 max=24708 | inter-arrival us p50=5015 p99=7118 max=28858

# (2) sub deadline 15 ms 요청, pub 제공 없음
[WARN] [1792356428.591794640] [q6_lat_sub]: New publisher discovered on topic '/imu/data_raw', offering incompatible QoS. No messages will be sent to it. Last incompatible policy: DEADLINE_QOS_POLICY
[INFO] [1792356458.590710304] [q6_lat_sub]: n=0 lost=0 deadline_miss=0 | latency us p50=-1 p99=-1 max=-1 | inter-arrival us p50=-1 p99=-1 max=-1

# (3) pub deadline 10 ms 제공, sub 15 ms 요청
[INFO] [1792356504.258373152] [q6_lat_sub]: n=4086 lost=20 deadline_miss=0 | latency us p50=1888 p99=3869 max=17944 | inter-arrival us p50=5102 p99=6899 max=24077

# (4) pub best_effort, sub reliable (lat_sub, 그다음 ros2 topic echo 두 번, topic info)
[WARN] [1792356529.259274928] [q6_lat_sub]: New publisher discovered on topic '/imu/data_raw', offering incompatible QoS. No messages will be sent to it. Last incompatible policy: RELIABILITY_QOS_POLICY
[INFO] [1792356559.258136464] [q6_lat_sub]: n=0 lost=0 deadline_miss=0 | latency us p50=-1 p99=-1 max=-1 | inter-arrival us p50=-1 p99=-1 max=-1
# ros2 topic echo --qos-reliability reliable (60 s): 출력 1줄 = 아래 WARN뿐
echo(reliable) timeout-exit=124 lines=1
[WARN] [1792356569.143118080] [_ros2cli_491]: New publisher discovered on topic '/imu/data_raw', offering incompatible QoS. No messages will be received from it. Last incompatible policy: RELIABILITY
# ros2 topic echo --qos-reliability best_effort --field header.stamp
sec: 1792356626
nanosec: 392165472
---
sec: 1792356626
# ros2 topic info -v (grep)
Node name: q6_imu_pub
Node namespace: /
Endpoint type: PUBLISHER
  Reliability: BEST_EFFORT
```

출력에서 볼 것:
- 조용한 상태(TCG): 지연 p50 ≈ 2.0 ms, p99 ≈ 4 ms, max ≈ 25 ms. 수신 간격 p50 = 5.0 ms(200 Hz)인데 max 29 ms.
- `lost=29`는 **네트워크 손실이 아니다**. reliable이라 DDS는 하나도 안 버렸다. publisher의 wall timer가 늦게 깨서 tick을 건너뛰어 stamp에 구멍이 생긴 것이다. "stamp 간격으로 손실을 추정"하는 도구는 **소스 쪽 누락과 전송 손실을 구분하지 못한다** — 구분하려면 메시지에 시퀀스 번호를 넣는다(커스텀 메시지, Q7).
- 이 노트를 쓰다 실제로 겪은 일: 처음 버전의 lat_sub는 deadline 15 ms를 무조건 요청했고, publisher는 deadline을 제공하지 않았다. 결과는 **아무 메시지도 안 오고 노드가 영원히 기다리는 것**이었다(첫 실험 게스트가 타임아웃으로 죽었다). deadline도 reliability와 똑같이 request-vs-offered로 매칭된다(제공 주기 ≤ 요청 주기). 경고는 `Last incompatible policy: DEADLINE_QOS_POLICY` 한 줄뿐이다.
- reliability 불일치도 같다: `RELIABILITY_QOS_POLICY` 경고, n=0. `ros2 topic echo --qos-reliability reliable`도 60 s 동안 0줄(exit 124 = timeout)이고, best_effort로 바꾸면 즉시 받는다.

---

## 8. 측정 — QoS × 부하 × RMW × 우선순위

### 8.1 실험 설계

N3의 원칙대로 **한 번에 하나만 바꾼다**. 200 Hz Imu, 같은 머신 두 프로세스, 각 20 s. 바꾸는 것:

- RMW: `rmw_fastrtps_cpp`(기본) vs `rmw_cyclonedds_cpp`
- QoS: reliable vs best-effort, depth 10 / 1 / 100
- 부하: 없음 vs `stress-ng --cpu 2`(게스트 vCPU 2개를 모두 점유하는 CPU 부하)
- 우선순위: 기본(SCHED_OTHER) vs `chrt -f 50`(publisher·subscriber 둘 다 SCHED_FIFO 50)

```sh
# runC의 핵심 (전체 스크립트는 셸 함수 한 개 + 루프)
RMW_IMPLEMENTATION=$rmw ros2 run q6_imu imu_pub --ros-args -p best_effort:=$be -p depth:=$d &
stress-ng --cpu 2 --timeout 45s &                     # load=stress일 때
chrt -f 50 ros2 run q6_imu lat_sub --ros-args -p best_effort:=$be -p depth:=$d -p duration:=20.0
```

### 8.2 예제 8 — 결과 (실제 출력, TCG)

이 측정을 돌리는 동안 내 다른 게스트는 모두 꺼 두었다(같은 VM에서 다른 작성자 몇 명의 작업은 계속 돌고 있었다). 각 줄은 20 s 측정 한 번이다.

```text
RES rmw=fastrtps_cpp be=false depth=10 load=idle prio=- :: n=4034 lost=79 deadline_miss=0 | latency us p50=1873 p99=5469 max=63178 | inter-arrival us p50=5020 p99=9162 max=68696
RES rmw=fastrtps_cpp be=false depth=10 load=stress prio=- :: n=4004 lost=249 deadline_miss=0 | latency us p50=1355 p99=10482 max=159472 | inter-arrival us p50=4929 p99=13376 max=162636
RES rmw=fastrtps_cpp be=true depth=10 load=idle prio=- :: n=4002 lost=108 deadline_miss=0 | latency us p50=1722 p99=6327 max=37254 | inter-arrival us p50=5004 p99=9828 max=50163
RES rmw=fastrtps_cpp be=true depth=10 load=stress prio=- :: n=3896 lost=436 deadline_miss=0 | latency us p50=1071 p99=14748 max=277521 | inter-arrival us p50=5059 p99=13934 max=281936
RES rmw=fastrtps_cpp be=false depth=10 load=stress prio=50 :: n=4047 lost=56 deadline_miss=0 | latency us p50=907 p99=1705 max=31568 | inter-arrival us p50=5025 p99=6216 max=44065
RES rmw=fastrtps_cpp be=true depth=1 load=stress prio=- :: n=3856 lost=361 deadline_miss=0 | latency us p50=936 p99=6206 max=75845 | inter-arrival us p50=5130 p99=11026 max=237677
RES rmw=fastrtps_cpp be=false depth=100 load=stress prio=- :: n=4049 lost=197 deadline_miss=0 | latency us p50=1281 p99=9395 max=48600 | inter-arrival us p50=5045 p99=12875 max=48390
RES rmw=cyclonedds_cpp be=false depth=10 load=idle prio=- :: n=4083 lost=20 deadline_miss=0 | latency us p50=1863 p99=3187 max=16970 | inter-arrival us p50=5035 p99=6454 max=21470
RES rmw=cyclonedds_cpp be=false depth=10 load=stress prio=- :: n=4078 lost=347 deadline_miss=0 | latency us p50=870 p99=5912 max=22811 | inter-arrival us p50=5161 p99=10863 max=28209
RES rmw=cyclonedds_cpp be=true depth=10 load=idle prio=- :: n=4083 lost=22 deadline_miss=0 | latency us p50=1870 p99=3517 max=17577 | inter-arrival us p50=5027 p99=6694 max=22750
RES rmw=cyclonedds_cpp be=true depth=10 load=stress prio=- :: n=4067 lost=363 deadline_miss=0 | latency us p50=790 p99=5834 max=17587 | inter-arrival us p50=5129 p99=11024 max=29733
RES rmw=cyclonedds_cpp be=false depth=10 load=stress prio=50 :: n=4075 lost=35 deadline_miss=0 | latency us p50=669 p99=1388 max=12503 | inter-arrival us p50=5172 p99=6675 max=37149
RES rmw=cyclonedds_cpp be=true depth=1 load=stress prio=- :: n=3936 lost=204 deadline_miss=0 | latency us p50=769 p99=6157 max=13898 | inter-arrival us p50=5117 p99=11083 max=33312
RES rmw=cyclonedds_cpp be=false depth=100 load=stress prio=- :: n=3999 lost=180 deadline_miss=0 | latency us p50=794 p99=6302 max=11625 | inter-arrival us p50=5102 p99=11139 max=29589
```

같은 결과를 ms로 정리한 표 (값은 위 출력에서 그대로 옮김):

| RMW | QoS | depth | 부하 | 우선순위 | n | stamp 구멍 | 지연 p50 (ms) | 지연 p99 (ms) | 지연 max (ms) | 간격 p99 (ms) | 간격 max (ms) |
|---|---|---|---|---|---|---|---|---|---|---|---|
| Fast DDS | reliable | 10 | 없음 | OTHER | 4034 | 79 | 1.87 | 5.47 | 63.18 | 9.16 | 68.70 |
| Fast DDS | reliable | 10 | stress-ng | OTHER | 4004 | 249 | 1.35 | 10.48 | 159.47 | 13.38 | 162.64 |
| Fast DDS | best-effort | 10 | 없음 | OTHER | 4002 | 108 | 1.72 | 6.33 | 37.25 | 9.83 | 50.16 |
| Fast DDS | best-effort | 10 | stress-ng | OTHER | 3896 | 436 | 1.07 | 14.75 | 277.52 | 13.93 | 281.94 |
| Fast DDS | reliable | 10 | stress-ng | FIFO 50 | 4047 | 56 | 0.91 | 1.71 | 31.57 | 6.22 | 44.06 |
| Fast DDS | best-effort | 1 | stress-ng | OTHER | 3856 | 361 | 0.94 | 6.21 | 75.84 | 11.03 | 237.68 |
| Fast DDS | reliable | 100 | stress-ng | OTHER | 4049 | 197 | 1.28 | 9.39 | 48.60 | 12.88 | 48.39 |
| Cyclone | reliable | 10 | 없음 | OTHER | 4083 | 20 | 1.86 | 3.19 | 16.97 | 6.45 | 21.47 |
| Cyclone | reliable | 10 | stress-ng | OTHER | 4078 | 347 | 0.87 | 5.91 | 22.81 | 10.86 | 28.21 |
| Cyclone | best-effort | 10 | 없음 | OTHER | 4083 | 22 | 1.87 | 3.52 | 17.58 | 6.69 | 22.75 |
| Cyclone | best-effort | 10 | stress-ng | OTHER | 4067 | 363 | 0.79 | 5.83 | 17.59 | 11.02 | 29.73 |
| Cyclone | reliable | 10 | stress-ng | FIFO 50 | 4075 | 35 | 0.67 | 1.39 | 12.50 | 6.67 | 37.15 |
| Cyclone | best-effort | 1 | stress-ng | OTHER | 3936 | 204 | 0.77 | 6.16 | 13.90 | 11.08 | 33.31 |
| Cyclone | reliable | 100 | stress-ng | OTHER | 3999 | 180 | 0.79 | 6.30 | 11.62 | 11.14 | 29.59 |

### 8.3 그림으로

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="14">end-to-end 지연 p99 (ms) — 200 Hz Imu, 같은 게스트, TCG</text>
<line x1="60" y1="280" x2="660" y2="280" stroke="currentColor"/>
<line x1="60" y1="50" x2="60" y2="280" stroke="currentColor"/>
<line x1="56" y1="280.0" x2="60" y2="280.0" stroke="currentColor"/>
<text x="30" y="284.0" font-size="12">0</text>
<line x1="56" y1="222.5" x2="60" y2="222.5" stroke="currentColor"/>
<text x="30" y="226.5" font-size="12">4</text>
<line x1="56" y1="165.0" x2="60" y2="165.0" stroke="currentColor"/>
<text x="30" y="169.0" font-size="12">8</text>
<line x1="56" y1="107.5" x2="60" y2="107.5" stroke="currentColor"/>
<text x="30" y="111.5" font-size="12">12</text>
<line x1="56" y1="50.0" x2="60" y2="50.0" stroke="currentColor"/>
<text x="30" y="54.0" font-size="12">16</text>
<rect x="72.0" y="201.4" width="26" height="78.6" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="70.0" y="197.4" font-size="12">5.5</text>
<rect x="102.0" y="234.2" width="26" height="45.8" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="100.0" y="230.2" font-size="12">3.2</text>
<text x="66.0" y="298" font-size="12">rel</text>
<text x="66.0" y="314" font-size="12">무부하</text>
<rect x="157.7" y="129.3" width="26" height="150.7" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="155.7" y="125.3" font-size="12">10.5</text>
<rect x="187.7" y="195.0" width="26" height="85.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="185.7" y="191.0" font-size="12">5.9</text>
<text x="151.7" y="298" font-size="12">rel</text>
<text x="151.7" y="314" font-size="12">부하</text>
<rect x="243.4" y="189.0" width="26" height="91.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="241.4" y="185.0" font-size="12">6.3</text>
<rect x="273.4" y="229.4" width="26" height="50.6" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="271.4" y="225.4" font-size="12">3.5</text>
<text x="237.4" y="298" font-size="12">BE</text>
<text x="237.4" y="314" font-size="12">무부하</text>
<rect x="329.1" y="68.0" width="26" height="212.0" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="327.1" y="64.0" font-size="12">14.7</text>
<rect x="359.1" y="196.1" width="26" height="83.9" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="357.1" y="192.1" font-size="12">5.8</text>
<text x="323.1" y="298" font-size="12">BE</text>
<text x="323.1" y="314" font-size="12">부하</text>
<rect x="414.9" y="255.5" width="26" height="24.5" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="412.9" y="251.5" font-size="12">1.7</text>
<rect x="444.9" y="260.0" width="26" height="20.0" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="442.9" y="256.0" font-size="12">1.4</text>
<text x="408.9" y="298" font-size="12">rel</text>
<text x="408.9" y="314" font-size="12">부하 FIFO</text>
<rect x="500.6" y="190.8" width="26" height="89.2" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="498.6" y="186.8" font-size="12">6.2</text>
<rect x="530.6" y="191.5" width="26" height="88.5" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="528.6" y="187.5" font-size="12">6.2</text>
<text x="494.6" y="298" font-size="12">BE</text>
<text x="494.6" y="314" font-size="12">d1 부하</text>
<rect x="586.3" y="144.9" width="26" height="135.1" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
<text x="584.3" y="140.9" font-size="12">9.4</text>
<rect x="616.3" y="189.4" width="26" height="90.6" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
<text x="614.3" y="185.4" font-size="12">6.3</text>
<text x="580.3" y="298" font-size="12">rel</text>
<text x="580.3" y="314" font-size="12">d100 부하</text>
<rect x="440" y="34" width="12" height="12" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/><text x="458" y="45" font-size="12">Fast DDS</text>
<rect x="540" y="34" width="12" height="12" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/><text x="558" y="45" font-size="12">Cyclone DDS</text>
<text x="20" y="350" font-size="12">rel = reliable, BE = best-effort, d1/d100 = depth</text>
<text x="20" y="370" font-size="12">부하 = stress-ng --cpu 2, FIFO = chrt -f 50 (pub, sub 둘 다)</text>
</svg>
```
그림 6 — QoS·부하·RMW·우선순위별 지연 p99. 절대값은 TCG 게스트라 실제 HW와 다르다. 비교만 의미가 있다.

### 8.4 읽는 법

1. **부하가 꼬리를 늘린다, 중앙값이 아니라.** 같은 RMW·QoS에서 stress-ng를 걸면 p99가 Fast DDS 5.5 → 10.5 ms, Cyclone 3.2 → 5.9 ms로 거의 두 배가 되고 max는 수십~수백 ms로 튄다. 반면 p50은 오히려 **줄었다**(1.9 → 0.9~1.4 ms). 이상해 보이지만 TCG에서 흔한 현상이다: 무부하일 때 게스트 vCPU가 WFI로 잠들었다가 깨어나는 비용이 매 샘플에 붙고, 부하가 있으면 vCPU가 늘 깨어 있어 그 비용이 사라진다. 실제 보드에서도 cpuidle 깊은 상태에서 깨는 비용 때문에 "한가할 때 오히려 느린" 현상이 있다(N5 — 대기 상태 지연). 그래서 **평균·중앙값으로 비교하면 안 되고** p99/max와 분포를 본다(N3).
2. **우선순위가 가장 큰 효과다.** 부하 중에 publisher·subscriber를 `chrt -f 50`으로 띄우면 p99가 Fast DDS 10.5 → **1.7 ms**, Cyclone 5.9 → **1.4 ms**로, 무부하보다도 좋아진다. stamp 구멍(타이머 tick 누락)도 249 → 56, 347 → 35로 줄었다. `chrt`로 띄운 프로세스의 모든 스레드(DDS 수신 스레드 포함 — §12.1의 스레드 지도)가 FIFO 50을 상속했기 때문이다. max는 여전히 12~32 ms — PREEMPT_RT가 아닌 커널 + TCG의 한계다(N1·N2의 결론과 같다).
3. **reliable vs best-effort는 같은 머신 무손실 경로에서는 큰 차이가 없다.** 같은 호스트에서는 샘플이 실제로 거의 안 사라지므로 재전송이 일어날 일이 없다. 차이는 손실이 있는 링크(Wi-Fi, 혼잡한 스위치)나 subscriber가 느려 큐가 밀릴 때 생긴다. 이 측정에서 best-effort가 이긴 칸이 없다는 것을 "best-effort가 소용없다"로 읽으면 안 된다 — 측정 조건이 그 차이를 만들지 않았을 뿐이다.
4. **depth**: Fast DDS에서 best-effort depth 1은 depth 10보다 p99·max가 작았고(14.7 → 6.2 ms, 277 → 76 ms), reliable depth 100은 max가 159 → 49 ms로 작았다. 이 정도 차이는 이 짧은(20 s) 측정 한 번의 잡음 범위일 수 있다(N3 §5: 짧은 측정은 max를 과소·과대 추정한다). depth의 진짜 효과 — 느린 subscriber 앞에서 depth가 크면 "오래된 샘플을 순서대로 늦게" 받고, 작으면 "중간을 버리고 최신만" 받는다 — 를 보려면 subscriber 콜백에 일부러 지연을 넣는 실험이 필요하다(§20 연습).
5. **Cyclone이 이 조건에서 꼬리가 짧았다.** 거의 모든 칸에서 p99·max가 Fast DDS보다 작다(부하 reliable: max 23 ms vs 159 ms). 같은 호스트에서 Fast DDS는 SHM 전송(+ 스레드 15개), Cyclone은 loopback UDP(스레드 8개)였다. 단, **TCG의 단일 측정 한 번**이고 Fast DDS의 SHM은 큰 메시지(이미지)에서 이득이 커지는 구조라 0.3 KB IMU로는 장점이 안 드러난다. "Cyclone이 더 빠르다"가 결론이 아니라 "**RMW는 우리 메시지·부하에서 재서 고른다**"가 결론이다.
6. `stamp 구멍`(lost)은 전송 손실이 아니라 대부분 publisher 타이머가 부하 때문에 tick을 놓친 것이다(§7.6). 부하 시 수백 개로 늘고 FIFO 50에서 수십 개로 줄어드는 것이 그 증거다 — 소스(드라이버 노드)의 스케줄링 품질이 데이터 품질이다.

---

## 9. rosbag2 — 기록과 재생

### 9.1 개념

rosbag2는 토픽을 **직렬화된 그대로** 파일에 기록하고 나중에 같은 토픽으로 다시 publish한다. Jazzy의 기본 저장 형식은 **MCAP**(구버전 기본은 SQLite3). 임베디드에서 쓰는 장면: 필드 데이터 수집(센서 드라이버 검증·알고리즘 개발용), 회귀 테스트(같은 bag을 재생해 알고리즘 출력 비교), 블랙박스(§17.4).

### 9.2 예제 9 — 기록, info, 재생

무엇을 확인하나: 200 Hz IMU 토픽을 기록해 메시지 수·크기를 보고, 재생할 때 **header.stamp는 원래 기록 시각 그대로**라는 것(그래서 재생 중 `now() - stamp`는 "몇 분 전"이 된다 — sim time이 필요한 이유).

```sh
ros2 bag record -o q6bag /imu/data_raw        # Ctrl-C로 종료
ros2 bag info q6bag
ros2 run q6_imu lat_sub --ros-args -p duration:=15.0 &
ros2 bag play q6bag
```

```text
$ timeout -s INT 60 ros2 bag record -o q6bag /imu/data_raw     (마지막 4줄)
[INFO] [1792357693.944997856] [rosbag2_recorder]: Pausing recording.
[INFO] [1792357693.947996976] [rosbag2_cpp]: Writing remaining messages from cache to the bag. It may take a while
[INFO] [1792357693.997680400] [rosbag2_recorder]: Recording stopped
[INFO] [1792357694.012666160] [rosbag2_recorder]: Event publisher thread: Exited
$ ros2 bag info q6bag
Files:             q6bag_0.mcap
Bag size:          3.8 MiB
Storage id:        mcap
ROS Distro:        jazzy
Duration:          53.618724080s
Start:             Oct 18 2026 21:07:20.311289840 (1792357640.311289840)
End:               Oct 18 2026 21:08:13.930013920 (1792357693.930013920)
Messages:          10629
Topic information: Topic: /imu/data_raw | Type: sensor_msgs/msg/Imu | Count: 10629 | Serialization Format: cdr
Service:           0
Service information: 
$ ls -la q6bag
-rw-r--r-- 1 root root    1281 Oct 18 21:08 metadata.yaml
-rw-r--r-- 1 root root 3952729 Oct 18 21:08 q6bag_0.mcap
$ ros2 run q6_imu lat_sub --ros-args -p duration:=15.0 &  ;  ros2 bag play q6bag
[INFO] [1792357742.886138288] [rosbag2_player]: Press CURSOR_UP for Increase Rate 10%
[INFO] [1792357742.886572752] [rosbag2_player]: Press CURSOR_DOWN for Decrease Rate 10%
[INFO] [1792357742.891363952] [rosbag2_player]: Playback until timestamp: -1
[INFO] [1792357746.421769184] [q6_lat_sub]: n=671 lost=9 deadline_miss=0 | latency us p50=102717376 p99=102748106 max=102769013 | inter-arrival us p50=4990 p99=19430 max=28358
```

출력에서 볼 것:
- 저장 형식 `mcap`(Jazzy 기본), 직렬화 형식 `cdr` — 와이어에서 받은 바이트를 그대로 저장한다. 10629개 x 약 330 B ≈ 3.5 MB + 인덱스 = 3.8 MiB.
- 53.6 s 동안 10629개 = 198 Hz. 기록기 자체도 subscriber라서 기록 중 CPU를 쓴다(TCG에서는 무시 못 할 만큼).
- 재생 중 lat_sub의 "지연"이 **102.7 s**다. 재생기는 메시지를 원래 간격(수신 간격 p50 4990 µs)으로 다시 publish하지만 **header.stamp는 기록 당시 값 그대로**다. 그래서 재생 데이터로 알고리즘을 돌릴 때는 `ros2 bag play --clock` + 모든 노드 `use_sim_time:=true`로 시계를 bag 시간에 맞춘다(§5.4). 그렇지 않으면 TF·동기화 노드가 "100 s 전 데이터"로 보고 버린다.

---

## 10. 카메라 — vivid + v4l2_camera

I5의 가상 카메라 드라이버 `vivid`를 `/dev/video0`으로 만들고, ROS 2 표준 V4L2 노드 `v4l2_camera`로 `sensor_msgs/Image`를 내보낸다. 실제 장비에서 USB UVC 카메라나 CSI 센서(Q1·Q2)를 ROS에 연결하는 첫 단계와 같다.

### 10.1 예제 10 — image 토픽의 hz와 bw

```sh
modprobe vivid n_devs=1
v4l2-ctl --list-devices
ros2 run v4l2_camera v4l2_camera_node --ros-args -p video_device:=/dev/video0 -p image_size:="[320,240]" &
ros2 topic hz /image_raw; ros2 topic echo --once /image_raw --field header; ros2 topic bw /image_raw
```

```text
$ v4l2-ctl --list-devices
vivid (platform:vivid-000):
	/dev/video0
	/dev/video1
	/dev/video2
	/dev/video3
	/dev/radio0
	/dev/radio1
	/dev/vbi0
$ ros2 run v4l2_camera v4l2_camera_node ...     (로그 앞부분)
[INFO] [1792357820.331362448] [v4l2_camera]: Driver: vivid
[INFO] [1792357820.333429616] [v4l2_camera]: Version: 397879
[INFO] [1792357820.333740320] [v4l2_camera]: Device: vivid
[INFO] [1792357820.333993984] [v4l2_camera]: Location: platform:vivid-000
[INFO] [1792357820.334222528] [v4l2_camera]: Capabilities:
[INFO] [1792357820.334686448] [v4l2_camera]:   Read/write: YES
[INFO] [1792357820.334918656] [v4l2_camera]:   Streaming: YES
[INFO] [1792357820.335579456] [v4l2_camera]: Current pixel format: YUYV @ 320x240
[INFO] [1792357820.390218176] [v4l2_camera]: Available pixel formats: 
[INFO] [1792357820.390672640] [v4l2_camera]:   YUYV - YUYV 4:2:2
[INFO] [1792357820.391018848] [v4l2_camera]:   UYVY - UYVY 4:2:2
[INFO] [1792357820.391262048] [v4l2_camera]:   YVYU - YVYU 4:2:2
$ ros2 topic hz /image_raw          (마지막 두 창)
average rate: 5.002
	min: 0.169s max: 0.224s std dev: 0.00919s window: 255
average rate: 5.001
	min: 0.169s max: 0.224s std dev: 0.00916s window: 261
$ ros2 topic echo --once /image_raw --field header
stamp:
  sec: 1792357941
  nanosec: 475676752
frame_id: camera
---
$ ros2 topic bw /image_raw
1.15 MB/s from 100 messages
	Message size mean: 0.23 MB min: 0.23 MB max: 0.23 MB
```

출력에서 볼 것:
- 커널 쪽 이름 `vivid-000`과 `/dev/video0`이 I5에서 본 그대로다. v4l2_camera는 V4L2 capability(`Streaming: YES` = MMAP 스트리밍 가능)를 확인하고 YUYV 320x240으로 연다.
- 이미지 한 장이 0.23 MB = 320 x 240 x **3** 바이트 = 230400 B. 센서는 YUYV(2 B/px = 153600 B)인데 노드가 기본 `output_encoding` rgb8로 **CPU에서 변환·복사**해서 커졌다. 실제 1080p 30 fps라면 1920 x 1080 x 3 x 30 ≈ 187 MB/s — §2.5의 zero-copy와 §17.3이 왜 필요한지 보여 주는 숫자다.
- 5.0 Hz로 매우 일정하다(표준편차 9 ms). 이게 노드가 느린 탓인지 드라이버 속도인지는 드라이버 단독으로 재 보면 가려진다. 별도 부팅에서 같은 포맷으로 잰 결과:

```text
$ v4l2-ctl -d /dev/video0 --get-parm
Streaming Parameters Video Capture:
	Capabilities     : timeperframe
	Frames per second: 5.000 (5/1)
	Read buffers     : 1
$ v4l2-ctl -d /dev/video0 --set-fmt-video=width=320,height=240,pixelformat=YUYV --stream-mmap --stream-count=100   (fps 줄만)
 5.00 fps
 5.00 fps
 5.00 fps
```

  vivid가 이 포맷에서 `timeperframe` = 1/5 s로 설정돼 있어 **드라이버가 5 fps를 낸다**. ROS 노드는 손실 없이 따라가고 있었다. "ROS 토픽 hz가 낮다"를 보면 항상 드라이버 단독 속도(`v4l2-ctl --stream-mmap`, `VIDIOC_G_PARM`)부터 확인하는 습관이 필요하다 — 노드를 의심하기 전에.
- `frame_id: camera`가 기본값이다. 제품에서는 `camera_optical_frame` 같은 REP-105/103 규약 이름으로 바꾸고 `CameraInfo`를 같이 내보낸다.

---

## 11. 두 머신 — DDS discovery와 `ROS_DOMAIN_ID` 격리

### 11.1 구성

QEMU 게스트 두 개(E1, E2)를 `-netdev socket,mcast=230.0.0.1:41800`으로 묶었다. 이 백엔드는 "모든 게스트의 이더넷 프레임을 UDP 멀티캐스트로 서로에게 뿌리는 가상 허브"라서 게스트 안에서 IP 멀티캐스트(DDS discovery)가 진짜 LAN처럼 동작한다. 게스트에는 이 NIC 하나만 두고(`-netdev user` 없음 — 두 게스트가 같은 10.0.2.15를 가지면 DDS 로케이터가 꼬인다) 정적 IP 10.18.6.1/2를 줬다.

```svg
<svg viewBox="0 0 660 260" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah6" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="14">두 게스트 실험 — 같은 망, 다른 domain</text>
<rect x="20" y="45" width="230" height="150" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="30" y="68" font-size="13">E1 (10.18.6.1)</text>
<text x="30" y="95" font-size="12">domain 0: imu_pub 50 Hz</text>
<text x="30" y="117" font-size="12">domain 7: talker (/chatter)</text>
<text x="30" y="150" font-size="12">SPDP → 239.255.0.1:7400</text>
<text x="30" y="172" font-size="12">SPDP → 239.255.0.1:9150</text>
<rect x="410" y="45" width="230" height="150" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="420" y="68" font-size="13">E2 (10.18.6.2)</text>
<text x="420" y="95" font-size="12">domain 0: topic list, lat_sub</text>
<text x="420" y="117" font-size="12">domain 7: echo /chatter</text>
<text x="420" y="139" font-size="12">domain 5: 아무것도 안 보임</text>
<text x="420" y="161" font-size="12">LOCALHOST 범위: 안 보임</text>
<rect x="270" y="95" width="120" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="280" y="116" font-size="12">QEMU socket</text>
<text x="280" y="134" font-size="12">mcast 허브</text>
<line x1="250" y1="120" x2="268" y2="120" stroke="currentColor" marker-end="url(#ah6)"/>
<line x1="390" y1="120" x2="408" y2="120" stroke="currentColor" marker-end="url(#ah6)"/>
<text x="20" y="225" font-size="12">VM 호스트 쪽에서는 230.0.0.1:41800 UDP 멀티캐스트 하나 (노트 ID별 고유 포트)</text>
<text x="20" y="247" font-size="12">게스트 안에서는 진짜 이더넷 LAN처럼 보인다</text>
</svg>
```
그림 7 — 두 게스트 실험 구성.

### 11.2 예제 11 — discovery, domain 격리, discovery range

```sh
# E2에서
tcpdump -i <if> -n udp port 7400                       # domain 0 SPDP
ROS_DOMAIN_ID=0 ros2 topic list; ros2 node list
ROS_DOMAIN_ID=0 ros2 run q6_imu lat_sub --ros-args -p rate:=50.0 -p duration:=15.0
ROS_DOMAIN_ID=7 ros2 topic echo --once /chatter
ROS_DOMAIN_ID=5 ros2 topic list --no-daemon
ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST ros2 topic list --no-daemon
```

```text
# E2: ROS_DOMAIN_ID=0 ros2 topic list; ros2 node list
/imu/data_raw
/parameter_events
/rosout
/q6_imu_pub
# E2: tcpdump -i enp0s1 -c 6 -n udp port 7400   (domain 0 SPDP)
21:04:35.008858 IP 10.18.6.1.56413 > 239.255.0.1.7400: UDP, length 500
21:04:38.006801 IP 10.18.6.1.56413 > 239.255.0.1.7400: UDP, length 500
21:04:39.088928 IP 10.18.6.2.47384 > 239.255.0.1.7400: UDP, length 500
21:04:39.091657 IP 10.18.6.2.47384 > 239.255.0.1.7400: UDP, length 500
21:04:39.197099 IP 10.18.6.2.47384 > 239.255.0.1.7400: UDP, length 500
21:04:39.197839 IP 10.18.6.2.47384 > 239.255.0.1.7400: UDP, length 500
6 packets captured
14 packets received by filter
0 packets dropped by kernel
# E2: ros2 topic hz /imu/data_raw   (E1의 50 Hz publisher, 마지막 창)
average rate: 49.942
	min: 0.002s max: 0.048s std dev: 0.00340s window: 1572
# E2: lat_sub rate=50 duration=15  (두 게스트 시계는 동기화하지 않음 → 지연에 시계 offset이 섞여 있다)
[INFO] [1792357550.973370288] [q6_lat_sub]: n=775 lost=4 deadline_miss=0 | latency us p50=8937 p99=16412 max=29152 | inter-arrival us p50=19991 p99=34273 max=43183
# E2: ROS_DOMAIN_ID=7 ros2 topic echo --once /chatter
domain7 echo exit=0
data: 'Hello World: 122'
---
# E2: tcpdump -c 4 -n udp port 9150   (domain 7 SPDP = 7400 + 250 x 7)
21:05:53.032291 IP 10.18.6.1.55320 > 239.255.0.1.9150: UDP, length 500
21:05:56.034166 IP 10.18.6.1.55320 > 239.255.0.1.9150: UDP, length 500
21:05:56.378269 IP 10.18.6.2.39602 > 239.255.0.1.9150: UDP, length 500
21:05:56.378665 IP 10.18.6.2.39602 > 239.255.0.1.9150: UDP, length 500
4 packets captured
6 packets received by filter
0 packets dropped by kernel
# E2: ROS_DOMAIN_ID=5 ros2 topic list --no-daemon ; ros2 topic echo --once /chatter (45 s)
/parameter_events
/rosout
domain5 echo exit=124 (124 = timeout, nothing received)
# E2: ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST, domain 0: topic list --no-daemon ; echo --once /imu/data_raw (45 s)
/parameter_events
/rosout
localhost-range echo exit=124
```

출력에서 볼 것:
- domain 0에서 E2는 E1의 `/imu/data_raw`와 노드 `/q6_imu_pub`을 본다. 같은 E1의 domain 7 talker는 domain 0 목록에 **없다**.
- SPDP 패킷: E1(10.18.6.1)이 **3 s 간격**으로 239.255.0.1:7400에 500바이트를 보낸다(Fast DDS 기본 announcement 주기). E2의 participant는 시작 직후 연달아 몇 번 보낸다(초기 burst). domain 7은 포트 **9150** = 7400 + 250 x 7 — §2.3 공식 그대로. (tcpdump의 "listening on …" 안내 줄은 생략했다.)
- 두 머신 사이 50 Hz 수신은 49.94 Hz로 안정적이다. lat_sub의 "지연" p50 8.9 ms에는 **두 게스트 시계의 차이가 섞여 있다**. 두 게스트는 같은 VM 호스트 RTC에서 시간을 받았을 뿐 PTP/NTP로 맞추지 않았다. 다른 머신 사이에서 `now() - stamp`를 지연이라고 부르려면 Q3의 시계 동기가 먼저다(또는 왕복 측정).
- `ROS_DOMAIN_ID=5`: 기본 토픽 두 개(자기 자신의 `/parameter_events`, `/rosout`)만 보이고 `/chatter` echo는 45 s 동안 아무것도 못 받았다(exit 124).
- `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST`: 같은 domain 0인데도 E1이 안 보인다. 로봇 내부 통신이 Wi-Fi 쪽으로 새지 않게 할 때 쓰는 설정이다(로봇 안 프로세스끼리는 계속 보인다).
## 12. 실시간 설정 — ROS 2 노드를 RT로 돌리는 체크리스트

N2(실시간 튜닝)의 원리를 ROS 2에 그대로 적용한다. ROS 2 자체는 실시간을 보장하지 않는다. **실시간이 가능하도록 설계된** 부분(rclcpp의 할당자 훅, executor 분리, QoS)이 있을 뿐이고, 나머지는 OS와 사용자의 몫이다.

| 층 | 할 일 | 왜 | 참조 |
|---|---|---|---|
| 커널 | PREEMPT_RT (`CONFIG_PREEMPT_RT=y`), `isolcpus`/`nohz_full`/`rcu_nocbs` 또는 cpuset으로 RT 코어 분리, IRQ affinity | 커널 안 비선점 구간 제거, 소음 차단 | N1, N2 |
| 프로세스 | `mlockall(MCL_CURRENT or MCL_FUTURE)` + 스택·힙 prefault, `RLIMIT_MEMLOCK`·`RLIMIT_RTPRIO` 허용 (systemd `LimitMEMLOCK=infinity`, `LimitRTPRIO=99`) | page fault = 수십 µs~ms 지연 | N2, A5 |
| 스레드 | RT executor 스레드만 `SCHED_FIFO` 우선순위 + affinity, 나머지는 SCHED_OTHER | 콜백별 우선순위는 executor 단위로만 줄 수 있다 | §4 |
| DDS | 수신·이벤트 스레드 우선순위·affinity를 RMW 설정 파일에서 지정 (Fast DDS XML의 thread settings, Cyclone `Threads` 설정) — 안 하면 SCHED_OTHER로 남아 RT executor가 데이터를 늦게 받는다 | 데이터 경로의 앞단 | §8.4 |
| 메모리 | 루프 안 할당 금지: 메시지 재사용, `std::vector::reserve`, `rclcpp` allocator 훅(TLSF 등), 로그 문자열 포맷 금지 | `malloc`은 lock + page fault 가능 | N2 |
| QoS | 제어 경로는 best-effort + 작은 depth, 또는 같은 프로세스면 intra-process | 재전송 대기·큐 지연 제거 | §3 |
| 측정 | 최악 부하에서 end-to-end 지연 히스토그램 + cyclictest 기준선 | "좋아졌다"가 아니라 "p99.99 = x" | N3, §8 |

한 가지 더: **진짜 1 kHz 이상의 hard real-time 루프는 ROS 2 그래프 밖**에 두는 것이 업계 다수 설계다. 모터 전류 루프는 MCU(N4, O3)에서, Linux 쪽은 수백 Hz 이하의 상위 루프와 상태 공유. ros2_control처럼 제어 루프를 하나의 RT 스레드(controller manager의 update 루프)에서 돌리고 ROS 토픽은 비RT 경로로만 쓰는 구조가 그 타협이다.

### 12.1 예제 12 — 노드 하나의 스레드 지도, SHM 세그먼트, 메모리 잠금 한도

```text
# Fast DDS(기본 RMW)로 imu_pub 하나 실행 중
$ ls -la /dev/shm
-rw-r--r--  1 root root 549408 Oct 18 21:06 fastrtps_4557b56cbff8fa94
-rw-r--r--  1 root root      0 Oct 18 21:06 fastrtps_4557b56cbff8fa94_el
-rw-r--r--  1 root root  52416 Oct 18 21:06 fastrtps_port7000
-rw-r--r--  1 root root      0 Oct 18 21:06 fastrtps_port7000_el
-rw-r--r--  1 root root  52416 Oct 18 21:06 fastrtps_port7411
-rw-r--r--  1 root root      0 Oct 18 21:06 fastrtps_port7411_el
-rw-rw-rw-  1 root root   4096 Oct 18 21:06 lttng-ust-wait-8
$ for t in /proc/$PID/task/*; do cat $t/comm; done | sort | uniq -c
threads: 15
      1 dds.asyn.0.0
      1 dds.ev.0
      1 dds.shm.7000
      1 dds.shm.7411
      1 dds.shm.wdog
      1 dds.udp.7000
      1 dds.udp.7400
      1 dds.udp.7410
      1 dds.udp.7411
      4 imu_pub
      2 imu_pub-ust
VmLck:	       0 kB
VmRSS:	   27968 kB
Threads:	15
# 같은 노드를 RMW_IMPLEMENTATION=rmw_cyclonedds_cpp로
      1 dq.builtins
      1 dq.user
      1 gc
      4 imu_pub
      2 imu_pub-ust
      1 recv
      1 recvUC
      1 tev
VmRSS:	   15600 kB
# Cyclone이 링크한 라이브러리 (일부)
	libiceoryx_binding_c.so => /opt/ros/jazzy/lib/aarch64-linux-gnu/libiceoryx_binding_c.so (0x0000ffffbebe0000)
	libiceoryx_posh.so => /opt/ros/jazzy/lib/aarch64-linux-gnu/libiceoryx_posh.so (0x0000ffffbe4c0000)
# tracetools / 메모리 잠금 한도 / 커널 선점 모델
$ ros2 run tracetools status
Tracing enabled
$ which lttng
/usr/bin/lttng
$ ulimit -l
8192
CONFIG_PREEMPT=y
```

출력에서 볼 것:
- **스레드 지도**: 노드 하나(사용자 스레드는 executor 하나)인데 Fast DDS는 스레드 15개를 만든다. `dds.udp.7400`(domain 0 discovery 멀티캐스트 수신), `dds.udp.7410/7411`(이 participant의 유니캐스트 discovery/user 포트 = 7400 + 10/11 + 2 x 0), `dds.shm.*`(공유 메모리 전송 수신; 7000번 포트 쌍의 용도는 이 노트에서 확인하지 않았다), `dds.ev.0`(타이머·이벤트), `dds.asyn.0.0`(비동기 송신). §2.3의 포트 공식이 스레드 이름에 그대로 보인다. `imu_pub-ust`는 LTTng-UST 리스너 스레드(tracetools가 링크돼 있어서).
- Cyclone은 `recv`/`recvUC`(수신), `tev`(타이머 이벤트), `dq.*`(전달 큐), `gc` — 더 적고 RSS도 15.6 MB vs 28 MB.
- **RT 설정의 핵심 함의**: 노드를 `chrt -f 80`으로 띄우면 이 스레드가 전부 FIFO 80을 상속한다(DDS가 스레드를 만들 때 부모 정책을 물려받는다). 반대로 executor 스레드만 나중에 `pthread_setschedparam`으로 올리면 DDS 수신 스레드는 SCHED_OTHER로 남는다. 어느 쪽도 원하는 것이 아니므로 RMW 설정 파일로 스레드별 우선순위·affinity를 명시한다(§12).
- `/dev/shm/fastrtps_*`: Fast DDS SHM 전송의 세그먼트와 포트별 링(§2.5). 같은 호스트 노드끼리는 UDP 대신 여기를 쓴다. 프로세스가 `kill -9`로 죽으면 이 파일이 남는다 — 장시간 운용 장비에서 `/dev/shm`이 차는 사례가 있다(Fast DDS는 다음 시작 때 정리를 시도한다, 기억 기반).
- Cyclone 바이너리는 **iceoryx를 링크**하고 있다. 즉 Cyclone의 SHM(zero-copy) 경로는 설정(`CYCLONEDDS_URI`의 SharedMemory 항목 + `iox-roudi` 데몬)만 하면 켤 수 있다(가이드, 실행 안 함).
- `ulimit -l` = 8192 KB: 일반 사용자는 mlockall로 8 MB까지만 잠글 수 있다. 이 노드 RSS가 28 MB이니 `mlockall(MCL_CURRENT or MCL_FUTURE)`는 실패한다(`ENOMEM`). root(`CAP_IPC_LOCK`)이거나 systemd `LimitMEMLOCK=infinity`가 필요하다. `VmLck: 0 kB` = 지금은 아무것도 잠겨 있지 않음.
- `CONFIG_PREEMPT=y`: 이 게스트 커널은 PREEMPT_RT가 아니다. 랩의 `Image-rt`(6.18.55-rt)에는 rootfs-dom용 모듈이 없어 이 노트에서는 RT 커널로 ROS를 돌리지 않았다(N2·N3이 RT 커널 측정을 다룬다).

---

## 13. ros2_tracing — "콜백이 언제 불렸나"를 LTTng로

ROS 2 코어(rcl, rclcpp, rmw)에는 `tracetools` 패키지의 **정적 tracepoint**가 이미 박혀 있다. LTTng(유저스페이스 tracer — ftrace의 유저 버전이라고 보면 된다)가 켜져 있으면 메시지 하나가 `rclcpp_publish` → `rcl_publish` → `rmw_publish` → (수신 쪽) `rmw_take` → `callback_start`/`callback_end`까지 지나는 시각을 노드 단위로 기록한다. M5/M7의 커널 이벤트(`sched_switch`)와 같은 LTTng 세션에 묶으면 "콜백이 늦은 이유가 스레드가 선점당해서인가"까지 본다.

이 랩 이미지에는 `ros2 trace` CLI(`ros-jazzy-ros2-tracing`)가 설치되지 않았지만, `tracetools`(tracepoint가 컴파일된 라이브러리)와 `lttng` 명령은 있다. 그래서 LTTng를 직접 써서 세션을 만들었다.

### 13.1 예제 13 — LTTng로 ROS 2 tracepoint 목록과 기록

무엇을 확인하나: 실행 중인 ROS 2 프로세스가 어떤 tracepoint를 등록했는지, 그리고 기록이 실제로 남는지.

```sh
ros2 run tracetools status                       # "Tracing enabled" = LTTng-UST 지원으로 빌드됨
lttng create q6 --output=/tmp/q6trace
lttng enable-event -u 'ros2:*'; lttng start
lttng list -u                                    # 지금 살아 있는 프로세스들이 등록한 UST tracepoint
ros2 run q6_imu lat_sub --ros-args -p duration:=3.0
lttng stop; lttng destroy
find /tmp/q6trace -type f; du -sh /tmp/q6trace
```

```text
$ ros2 run tracetools status
Tracing enabled
$ lttng list -u        (ros2:* 이름만 추려 정렬)
ros2:callback_end
ros2:callback_start
ros2:rcl_client_init
ros2:rcl_init
ros2:rcl_lifecycle_state_machine_init
ros2:rcl_lifecycle_transition
ros2:rcl_node_init
ros2:rcl_publish
ros2:rcl_publisher_init
ros2:rcl_service_init
ros2:rcl_subscription_init
ros2:rcl_take
ros2:rcl_timer_init
ros2:rclcpp_buffer_to_ipb
ros2:rclcpp_callback_register
ros2:rclcpp_construct_ring_buffer
ros2:rclcpp_executor_execute
ros2:rclcpp_executor_get_next_ready
ros2:rclcpp_executor_wait_for_work
ros2:rclcpp_intra_publish
ros2:rclcpp_ipb_to_subscription
ros2:rclcpp_publish
ros2:rclcpp_ring_buffer_clear
ros2:rclcpp_ring_buffer_dequeue
ros2:rclcpp_ring_buffer_enqueue
ros2:rclcpp_service_callback_added
ros2:rclcpp_subscription_callback_added
ros2:rclcpp_subscription_init
ros2:rclcpp_take
ros2:rclcpp_timer_callback_added
ros2:rclcpp_timer_link_node
ros2:rmw_publish
ros2:rmw_publisher_init
ros2:rmw_subscription_init
ros2:rmw_take
tracepoint names: 35
$ ros2 run q6_imu lat_sub --ros-args -p duration:=3.0      (세션이 켜진 상태)
[INFO] [1792359118.878237824] [q6_lat_sub]: n=684 lost=20 deadline_miss=0 | latency us p50=1778 p99=8269 max=23751 | inter-arrival us p50=5134 p99=12943 max=24926
$ find /tmp/q6trace -type f; du -sh /tmp/q6trace
/tmp/q6trace/ust/uid/0/64-bit/channel0_1
/tmp/q6trace/ust/uid/0/64-bit/channel0_0
/tmp/q6trace/ust/uid/0/64-bit/metadata
/tmp/q6trace/ust/uid/0/64-bit/index/channel0_0.idx
/tmp/q6trace/ust/uid/0/64-bit/index/channel0_1.idx
444K	/tmp/q6trace
$ python3 -c "import bt2"; which babeltrace2 babeltrace
ModuleNotFoundError: No module named 'bt2'
```

출력에서 볼 것: tracepoint 35종 — 초기화(`rcl_node_init`, `rcl_publisher_init`, `rclcpp_subscription_init` …)는 "어떤 핸들이 어느 노드·토픽의 것인가"를 기록해 나중에 이벤트를 이름으로 풀기 위한 것이고, 런타임 이벤트는 `rclcpp_publish`/`rcl_publish`/`rmw_publish`, `rmw_take`/`rcl_take`/`rclcpp_take`, `callback_start`/`callback_end`, executor 내부(`rclcpp_executor_wait_for_work`, `rclcpp_executor_get_next_ready`, `rclcpp_executor_execute`), intra-process 링버퍼(`rclcpp_ring_buffer_*`)다. 3 s 동안 publisher·subscriber 두 프로세스의 모든 이벤트가 CTF(Common Trace Format) 파일 444 KB로 남았다. 이 이미지에는 CTF 리더(babeltrace2, Python bt2)가 없어 내용을 풀어 보지는 못했다. 세션이 켜져 있어도 lat_sub 수치는 앞 절과 같은 범위였다(p50 1.8 ms).

다음 단계(가이드, 실행 안 함 — 리더가 없어서): `babeltrace2 /tmp/q6trace`로 이벤트를 텍스트로 보거나, `ros2_tracing`의 `ros2 trace -s q6 -k sched_switch -u 'ros2:*'`로 한 번에 세션을 만들고 `tracetools_analysis`로 콜백 실행 시간 분포를 뽑는다. 메시지 하나의 지연을 계층별로 쪼개는 법: 같은 메시지의 `rclcpp_publish`(publisher 쪽 시각)와 subscriber 쪽 `callback_start` 사이를 `rmw_take`로 나누면 "미들웨어에서 걸린 시간"과 "executor가 집어 들기까지 걸린 시간"이 분리된다.

tracepoint 비용: 세션이 없으면 LTTng-UST tracepoint는 거의 공짜(분기 하나)이고, `tracetools`를 빌드 옵션(`TRACETOOLS_DISABLED`)으로 아예 뺄 수도 있다. §12.1에서 본 `imu_pub-ust` 스레드 2개와 `/dev/shm/lttng-ust-wait-8`이 "LTTng 세션 데몬이 오면 붙을 준비가 된 상태"의 흔적이다.

---

## 14. micro-ROS — MCU를 ROS 2 그래프에 넣기 (가이드)

N4·O3에서 본 "Linux SoC + 외부 MCU" 구조에서 MCU를 ROS 2 노드처럼 보이게 하는 방법이다.

```svg
<svg viewBox="0 0 660 230" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah7" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="14">micro-ROS 구조 — MCU는 DDS를 직접 말하지 않는다</text>
<rect x="20" y="45" width="230" height="160" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="68" font-size="13">MCU (FreeRTOS, Zephyr, NuttX)</text>
<text x="30" y="95" font-size="12">사용자 코드 (rclc API)</text>
<text x="30" y="117" font-size="12">rcl + rmw_microxrcedds</text>
<text x="30" y="139" font-size="12">Micro XRCE-DDS Client</text>
<text x="30" y="161" font-size="12">정적 메모리, 수십 KB RAM</text>
<text x="30" y="183" font-size="12">UART / USB CDC / UDP / CAN FD</text>
<rect x="300" y="80" width="150" height="80" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="310" y="105" font-size="13">micro-ROS Agent</text>
<text x="310" y="127" font-size="12">Linux 프로세스</text>
<text x="310" y="147" font-size="12">XRCE ↔ DDS 대리</text>
<rect x="500" y="80" width="140" height="80" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="510" y="105" font-size="13">ROS 2 그래프</text>
<text x="510" y="127" font-size="12">일반 노드들</text>
<text x="510" y="147" font-size="12">(DDS/RTPS)</text>
<line x1="250" y1="120" x2="298" y2="120" stroke="currentColor" marker-end="url(#ah7)"/>
<line x1="450" y1="120" x2="498" y2="120" stroke="currentColor" marker-end="url(#ah7)"/>
</svg>
```
그림 8 — micro-ROS. MCU 쪽은 XRCE-DDS(DDS for eXtremely Resource Constrained Environments) 클라이언트이고, Linux 쪽 Agent가 대신 DDS participant가 되어 준다.

- MCU 쪽: `rclc`(C 편의 API) + `rcl` + `rmw_microxrcedds`. 모든 엔티티 개수·버퍼 크기를 **빌드 시점에 정적으로** 정한다(colcon.meta의 `RMW_UXRCE_MAX_PUBLISHERS` 등) — 펌웨어 엔지니어에게 익숙한 방식.
- 전송: 시리얼(UART, framing 포함), USB CDC, UDP, 사용자 정의 전송(CAN 등). Agent 실행 예: `ros2 run micro_ros_agent micro_ros_agent serial --dev /dev/ttyACM0 -b 921600` (가이드, 실행 안 함).
- 시간: MCU는 `rmw_uros_sync_session()`으로 Agent와 시각을 맞춰 stamp를 찍는다. 정밀도는 링크 지연 비대칭에 좌우된다 — 정밀 동기가 필요하면 PPS/트리거 신호를 같이 쓴다(Q3, N4 §7의 offset/drift 모델).
- PX4는 같은 XRCE-DDS를 써서 비행 제어기의 uORB 토픽을 ROS 2로 내보낸다(`uxrce_dds_client`, N4에서 언급).
- 판단 기준: MCU 데이터가 ROS 그래프의 여러 노드에 직접 보여야 하고 팀이 ROS 도구(echo, bag)를 MCU 데이터에 쓰고 싶으면 micro-ROS. 대역폭·결정성·인증이 중요하면 O3식 자체 프레임 프로토콜 + Linux 쪽 브리지 노드가 더 단순하고 검증하기 쉽다.

---

## 15. Yocto로 패키징 — meta-ros (가이드)

K4/K5의 Yocto 흐름에 ROS 2를 넣는 표준 레이어가 **meta-ros**(github.com/ros/meta-ros)다. `meta-ros-common` + 배포판별 `meta-ros2-jazzy` 같은 레이어를 bblayers에 추가하고, ROS 패키지는 `rosdep`/superflore로 **자동 생성된 레시피**(`ros-jazzy-...`가 아니라 `rclcpp`, `sensor-msgs` 같은 이름)로 들어온다. 자기 패키지는 `ament_cmake` 클래스를 상속한 레시피를 쓴다.

```text
# 가이드 (실행 안 함 — K3~K5와 같은 이유로 무거운 빌드는 생략)
# conf/bblayers.conf
BBLAYERS += "${TOPDIR}/../meta-ros/meta-ros-common ${TOPDIR}/../meta-ros/meta-ros2 ${TOPDIR}/../meta-ros/meta-ros2-jazzy"
# conf/local.conf
ROS_DISTRO = "jazzy"
IMAGE_INSTALL:append = " ros-core q6-imu"
# recipes-ros/q6-imu/q6-imu_0.1.0.bb
#   inherit ros_distro_jazzy ros_superflore_generated ros_ament_cmake    (클래스 이름은 meta-ros 버전에 따라 다름 — 레이어 README로 확인)
#   ROS_BUILD_DEPENDS = "rclcpp sensor-msgs"
#   SRC_URI = "git://...;branch=main;protocol=https"
```

실무 판단(K6과 연결): ROS 2의 유저스페이스는 거대해서(ros-base만 수백 개 패키지) Yocto 빌드 시간·레시피 유지 부담이 크다. Jetson처럼 Ubuntu 기반 BSP면 apt 바이너리 + 컨테이너(L8)가 흔하고, i.MX·TI 기반 양산 제품은 meta-ros로 이미지에 굽는 경우가 많다. 어느 쪽이든 **RMW와 그 설정 파일(XML), 환경변수(`RMW_IMPLEMENTATION`, `ROS_DOMAIN_ID`, `ROS_AUTOMATIC_DISCOVERY_RANGE`)를 이미지의 일부로 고정**하는 것이 핵심이다. systemd 유닛(L1) 예:

```text
# /etc/systemd/system/sensors.service (가이드)
[Service]
Environment=RMW_IMPLEMENTATION=rmw_cyclonedds_cpp ROS_DOMAIN_ID=12 CYCLONEDDS_URI=file:///etc/ros/cyclone.xml
ExecStart=/bin/bash -c 'source /opt/ros/jazzy/setup.bash && exec ros2 launch sensors sensors.launch.py'
LimitMEMLOCK=infinity
LimitRTPRIO=99
Restart=on-failure
```

---

## 16. 커널 안에서는 무슨 일이 일어나나

ROS 2는 순수 유저스페이스지만, 메시지 하나가 지나는 길은 이 스터디의 거의 모든 커널 모듈을 지난다.

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah8" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="20" y="22" font-size="14">IMU 샘플 하나가 커널을 지나는 길</text>
<rect x="20" y="40" width="190" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="30" y="60" font-size="13">IIO 드라이버 (I2)</text>
<text x="30" y="77" font-size="12">read_raw / 버퍼 + ts</text>
<rect x="240" y="40" width="190" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="250" y="60" font-size="13">imu_pub 프로세스</text>
<text x="250" y="77" font-size="12">sysfs read / chrdev read</text>
<rect x="460" y="40" width="180" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="470" y="60" font-size="13">DDS writer</text>
<text x="470" y="77" font-size="12">sendmsg() 또는 SHM</text>
<rect x="460" y="120" width="180" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="470" y="140" font-size="13">UDP/IP 스택 (A9)</text>
<text x="470" y="157" font-size="12">lo 또는 eth0 → qdisc</text>
<rect x="240" y="120" width="190" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="250" y="140" font-size="13">netdev 드라이버 (I6)</text>
<text x="250" y="157" font-size="12">NAPI, IRQ, DMA ring</text>
<rect x="20" y="120" width="190" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="30" y="140" font-size="13">소켓 수신 큐</text>
<text x="30" y="157" font-size="12">sk_buff, rcvbuf 한도</text>
<rect x="20" y="200" width="190" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="30" y="220" font-size="13">DDS 수신 스레드</text>
<text x="30" y="237" font-size="12">recvmsg / futex 깨움</text>
<rect x="240" y="200" width="190" height="44" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="250" y="220" font-size="13">executor 스레드 (F1)</text>
<text x="250" y="237" font-size="12">wait set → 콜백</text>
<line x1="210" y1="62" x2="238" y2="62" stroke="currentColor" marker-end="url(#ah8)"/>
<line x1="430" y1="62" x2="458" y2="62" stroke="currentColor" marker-end="url(#ah8)"/>
<line x1="550" y1="84" x2="550" y2="118" stroke="currentColor" marker-end="url(#ah8)"/>
<line x1="460" y1="142" x2="432" y2="142" stroke="currentColor" marker-end="url(#ah8)"/>
<line x1="240" y1="142" x2="212" y2="142" stroke="currentColor" marker-end="url(#ah8)"/>
<line x1="115" y1="164" x2="115" y2="198" stroke="currentColor" marker-end="url(#ah8)"/>
<line x1="210" y1="222" x2="238" y2="222" stroke="currentColor" marker-end="url(#ah8)"/>
<text x="20" y="280" font-size="12">같은 호스트 + loopback: netdev 대신 lo의 softirq. Fast DDS SHM: 소켓 대신 /dev/shm mmap +</text>
<text x="20" y="300" font-size="12">futex(또는 UDP 알림 포트). 각 화살표가 스케줄링 지점 = 지연·지터의 원천.</text>
</svg>
```
그림 9 — IMU 샘플이 커널을 지나는 길. 유저스페이스 경계를 최소 세 번(read, send, recv) 넘고 스레드를 최소 두 번 깨운다.

- **IIO 경로**(I2): `in_accel_x_raw` sysfs 읽기는 `iio_read_channel_info()` → 드라이버 `read_raw()`. 실제 제품은 sysfs 폴링이 아니라 버퍼 모드(`/dev/iio:deviceN` + trigger + kfifo, `iio_push_to_buffers_with_timestamp()`)를 쓰고, 커널이 찍은 타임스탬프를 stamp로 쓴다(그림 5의 ②).
- **소켓 경로**(A9, I6): DDS writer의 `sendmsg()` → `udp_sendmsg()` → IP → (loopback이면) `loopback_xmit()` → `netif_rx()` → softirq → `udp_rcv()` → 소켓 수신 큐 → 대기 중인 DDS 수신 스레드 깨움. 수신 큐가 넘치면 커널이 조용히 버린다 — `/proc/net/snmp`의 `Udp: RcvbufErrors`가 증가한다. 큰 메시지(이미지, 포인트클라우드)를 다루는 시스템은 `net.core.rmem_max`를 키우는 것이 DDS 벤더 공통 권고다(가이드: 이 랩에서는 전역 sysctl을 바꾸지 않았다).
- **조각화**: 64 KB가 넘는 RTPS 메시지는 DDS가 DATA_FRAG로 쪼갠다. UDP 데이터그램이 MTU를 넘으면 IP fragmentation까지 일어나 조각 하나만 잃어도 전체가 손실 — 이미지 토픽이 Wi-Fi에서 무너지는 이유.
- **공유 메모리 경로**(A7, A5): Fast DDS SHM은 `/dev/shm/fastrtps_*` 파일을 `mmap`한 링버퍼 + 알림. 커널 관점에서는 tmpfs 페이지 + futex다.
- **스케줄링**(F1, N2): DDS 수신 스레드와 executor 스레드가 깨어나는 순간이 `sched_wakeup` → `sched_switch`로 ftrace에 보인다(M5). 콜백 지연을 커널 이벤트로 설명할 때 쓰는 도구.
- **카메라**(I5): v4l2_camera는 V4L2 MMAP 버퍼를 `VIDIOC_DQBUF`로 받아 **복사해서** `sensor_msgs/Image`를 만든다. 버퍼의 `timestamp`(MONOTONIC)를 stamp로 쓰는지는 노드 구현에 달려 있다 — 드라이버 노드 코드를 반드시 확인할 것.

---

## 17. Embedded 실무에서 다시 보기

### 17.1 장면 1 — "IMU 토픽 hz는 200인데 EKF가 튄다"

`ros2 topic hz`는 **수신 시각** 간격을 잰다. 평균 200 Hz여도 stamp가 publish 시각이면(그림 5의 ④) 스케줄 지터가 stamp에 그대로 들어가 EKF가 "샘플 간격이 3 ms였다 7 ms였다"로 본다. 처방: 커널 버퍼 타임스탬프를 stamp로, `ros2 topic delay`와 stamp 간격의 분포(예제 7의 lat_sub 같은 도구)로 검증. Q7의 데이터 품질 툴이 바로 이걸 상시 감시한다.

### 17.2 장면 2 — "실험실에서는 되는데 로봇 Wi-Fi에서는 토픽이 안 보인다"

멀티캐스트 discovery 실패다. 진단 순서: `ros2 multicast receive`/`send`(ros2cli의 멀티캐스트 점검 명령) → tcpdump `udp port 7400`(domain 0) → 인터페이스 선택 확인. 처방: `ROS_STATIC_PEERS` 또는 Discovery Server, `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST`로 로봇 내부 트래픽을 Wi-Fi로 새지 않게, 원격 모니터링은 별도 domain + 브리지.

### 17.3 장면 3 — "카메라 4대를 붙였더니 CPU가 100%"

`sensor_msgs/Image` 직렬화·복사 + rosbag 기록 + 시각화 구독이 겹친다. 처방 순서: (1) 같은 프로세스 컴포넌트로 묶어 intra-process, (2) 압축 토픽(image_transport compressed)은 원격 시청용으로만, (3) 하드웨어 경로(dma-buf, NVIDIA/벤더 가속 전송) 검토, (4) 기록은 필요한 토픽만 + MCAP 압축 설정.

### 17.4 장면 4 — 필드 장비의 블랙박스

rosbag2를 링버퍼 모드(스냅샷)로 계속 기록하다가 이벤트(충돌, 안전 정지) 때 직전 N초를 저장하는 구조가 흔하다(`ros2 bag record --snapshot-mode` + 서비스 트리거, Jazzy 기능 — 기억 기반, 옵션 이름은 `ros2 bag record -h`로 확인). 저장 매체는 J5(전원 차단 안전)를 그대로 적용한다.

---

## 18. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 센서 publisher best-effort, 내 subscriber reliable | 토픽은 보이는데 콜백 0회 | QoS 요청 > 제공 (그림 3) | `SensorDataQoS()`로 맞추기, `ros2 topic info -v`로 확인 |
| `header.stamp = now()`를 publish 직전에 | 지연이 작아 보이는데 퓨전이 흔들림 | 측정 시각이 아닌 처리 시각 | 커널/HW 타임스탬프 사용, read 직전이라도 찍기 |
| 일부 노드만 `use_sim_time` | TF extrapolation 에러, stamp 0 | 시계 혼합 | launch에서 전 노드 일괄 설정 |
| 모든 콜백을 한 executor 스레드에 | 빠른 토픽이 주기적으로 끊김 | 긴 콜백이 막음 (예제 3) | callback group + executor 분리 |
| RT 스레드만 FIFO, DDS 스레드는 그대로 | 부하 시 지연이 여전히 큼 | 수신 스레드가 SCHED_OTHER | RMW XML로 DDS 스레드 우선순위 |
| 로봇 두 대가 같은 `ROS_DOMAIN_ID` | 남의 로봇 토픽이 섞임, 명령이 엉뚱한 로봇으로 | 같은 망, 같은 포트 | domain 분리 + discovery range 제한 + SROS2 |
| 노드마다 RMW가 다름 | 일부 조합에서 연결 불안정·서비스 실패 | 구현 간 세부 비호환 | 이미지 전체에 RMW 고정 |
| 컨테이너 기본 브리지 네트워크 | 컨테이너 안 노드가 서로/밖을 못 찾음 | 멀티캐스트 차단 | `--network host` 또는 static peers (L8) |
| 큰 메시지 + 작은 `rmem_max` | 이미지 토픽 hz 들쭉날쭉, 일부만 도착 | 소켓 수신 큐 overflow, 조각 손실 | rmem 상향, SHM, 압축 |
| reliable + depth 큼 + 느린 subscriber | 지연이 점점 커짐 (수백 ms) | 큐가 밀리고 재전송 대기 | best-effort + 작은 depth, 또는 처리 속도 확보 |

---

## 19. 면접에서 이렇게 말한다

**Q.** How does a ROS 2 publisher find its subscribers? What happens on a network without multicast?

**A.** 브로커가 없다. DDS의 SPDP가 participant 존재를 멀티캐스트로 알리고, SEDP가 유니캐스트로 각 토픽의 writer/reader와 QoS를 교환한 뒤, 이름·타입이 같고 QoS가 호환되면 매칭된다. 포트는 domain ID로 정해진다(7400 + 250 x domain). 멀티캐스트가 없으면 static peers(`ROS_STATIC_PEERS`, Cyclone 피어 목록)나 Fast DDS Discovery Server로 유니캐스트 discovery를 쓴다.

> There's no broker. DDS participants announce themselves with SPDP over multicast, then exchange their endpoints — topic, type and QoS — with SEDP over unicast. A writer and reader match when the topic name and type agree and the QoS is compatible. Ports are derived from the domain ID, 7400 plus 250 times the domain for discovery. Without multicast — Wi-Fi, some switches, container bridges — I configure static unicast peers, ROS_STATIC_PEERS or Cyclone's peer list, or run a Fast DDS discovery server.

**꼬리 질문**: "domain ID는 몇까지 쓸 수 있나?" → 0~101이 안전, 그 이상은 포트가 ephemeral 범위와 충돌.

**Q.** Why are sensor topics usually best-effort? When would you use reliable or transient-local?

**A.** 고빈도 센서는 잃은 샘플을 재전송받는 것보다 다음 샘플이 더 빨리 온다. reliable이면 재전송 대기와 큐 밀림으로 최신 데이터가 늦어진다. 그래서 `SensorDataQoS`(best-effort, depth 5). 반대로 명령·모드 변경·설정은 reliable, 지도·`/tf_static`·로봇 설명처럼 늦게 붙은 노드도 받아야 하는 값은 transient-local + depth 1. 매칭은 request-vs-offered라 best-effort publisher에 reliable subscriber는 연결이 안 된다.

> For a 200 Hz IMU, the next sample arrives sooner than a retransmission would, and reliable delivery causes head-of-line blocking, so you get stale data late. So sensors use the sensor-data profile: best effort, keep-last 5. Commands, mode changes and configuration are reliable, and latched data like maps or tf_static are reliable plus transient-local with depth one so late joiners get it. Matching is request-versus-offered: a reliable subscription won't match a best-effort publisher — it silently gets nothing, which is the most common QoS bug.

**Q.** Where do you take the timestamp for an IMU message, and which clock?

**A.** 측정 시각이다. 가장 좋은 것은 드라이버가 인터럽트에서 찍은 커널 타임스탬프(IIO 버퍼 timestamp 채널, V4L2 버퍼 timestamp) — 보통 CLOCK_MONOTONIC이라 ROS 시간(기본 CLOCK_REALTIME, sim이면 `/clock`)으로 offset 변환한다. publish 직전 `now()`는 스케줄 지터가 stamp에 들어가서 나쁘다. 여러 컴퓨터면 PTP/chrony 동기화가 전제이고, 센서 내부 지연(필터 group delay)은 데이터시트로 보정한다.

> The stamp should be the measurement time, not the publish time. Best is the kernel timestamp taken in the interrupt — the IIO buffer timestamp channel or the V4L2 buffer timestamp — which is usually CLOCK_MONOTONIC, so I convert it to ROS time, which is CLOCK_REALTIME by default or /clock in simulation. Stamping right before publish puts scheduling jitter into the data, and the fusion filter sees uneven sample spacing. Across machines the clocks must be synchronized with PTP first, and I correct for the sensor's internal filter delay from the datasheet.

**Q.** How would you make a ROS 2 control node real-time?

**A.** 커널은 PREEMPT_RT와 코어 격리, 프로세스는 mlockall과 prefault, RT 콜백만 가진 executor를 전용 스레드로 만들어 SCHED_FIFO와 affinity, DDS 수신 스레드 우선순위도 RMW 설정으로 올린다. 루프 안에서 할당·로그 포맷을 없애고 메시지를 재사용한다. 그리고 최악 부하에서 end-to-end 지연 히스토그램으로 검증한다. 1 kHz 이상 hard deadline이면 MCU나 ros2_control 같은 단일 RT 루프에 두고 ROS 토픽은 비RT 경로로 쓴다.

> PREEMPT_RT and an isolated core at the kernel level; mlockall and prefaulted stacks in the process; the real-time callbacks in their own callback group on a dedicated executor thread with SCHED_FIFO and CPU affinity, and the DDS receive threads raised too through the RMW config, otherwise they become the bottleneck. No allocation or log formatting in the loop, preallocated messages. Then I prove it with an end-to-end latency histogram under worst-case load. For a kilohertz hard loop I'd keep it on the MCU or in a single RT loop like ros2_control, and use topics only for the non-RT path.

**Q.** Fast DDS or Cyclone DDS — how would you choose?

**A.** 둘 다 Tier 1이고 RMW 교체는 환경변수 하나라, 우리 망·메시지 크기·부하에서 직접 재서 정한다. 판단 축은 같은 호스트 전송(Fast DDS는 SHM 기본, Cyclone은 iceoryx 별도), 멀티캐스트 없는 망 지원(Discovery Server vs 피어 목록), 설정 단순함, 스레드 우선순위 설정 방법, 팀의 기존 경험. 결정한 RMW와 XML 설정은 이미지에 고정한다.

> Both are tier-one and switching is one environment variable, so I measure on our network, message sizes and load. The axes are same-host transport — Fast DDS uses shared memory by default, Cyclone needs iceoryx configured — discovery without multicast, configuration complexity, how to set thread priorities, and the team's experience. Whatever we pick, the RMW and its XML config are pinned in the image so every node uses the same one.

**Q.** A node's topic shows up in `ros2 topic list` but the callback never fires. How do you debug it?

**A.** 첫째 `ros2 topic info -v`로 양쪽 QoS(reliability, durability, deadline, liveliness) 비교 — 대부분 여기서 끝난다. 둘째 타입 이름·해시가 같은지(다른 버전 메시지 패키지). 셋째 executor가 실제로 spin하는지, 콜백이 다른 긴 콜백에 막히는지. 넷째 다른 머신이면 discovery는 됐지만 데이터 포트(유니캐스트)가 방화벽·인터페이스에서 막혔는지 tcpdump로 본다.

> First, ros2 topic info -v to compare the QoS on both ends — reliability, durability, deadline, liveliness; an incompatible pair matches nothing and only logs a warning. Second, the type: same name but a different message definition version. Third, whether the executor actually spins and isn't blocked by a long callback in the same thread. If it's across machines, discovery can succeed over multicast while user data on the unicast ports is blocked by a firewall or the wrong interface, so I check with tcpdump.

**Q.** How do you measure end-to-end latency in a ROS 2 system?

**A.** 같은 머신이면 publisher가 측정 시각을 stamp에 넣고 subscriber가 `now() - stamp`의 분포(p50/p99/max)와 stamp 간격으로 손실을 본다. 부하·QoS·RMW·우선순위를 바꿔 같은 방법으로 비교한다. 다른 머신이면 시계 동기 품질이 측정 오차가 되므로 PTP를 먼저 맞추거나 왕복(ping-pong)으로 잰다. 원인 분석은 ros2_tracing + 커널 sched 이벤트.

> On one machine, the publisher puts the measurement time in the header and the subscriber records now minus stamp as a distribution — p50, p99, max — plus gaps in the stamps for loss. I repeat it under the same load while changing one thing: QoS, RMW, priority. Across machines the clock offset becomes the error, so I sync with PTP first or measure round-trip with a ping-pong pair. To find where the time goes I use ros2_tracing together with kernel scheduling events.

---

## 20. 직접 해보기

1. (개념) action은 어떤 토픽·서비스로 구성되는가? 왜 "기본 전송은 둘뿐"이라고 하나?
정답/힌트: feedback·status 토픽 + send_goal·get_result·cancel_goal 서비스. 서비스도 DDS에서는 request/reply 토픽 쌍.
2. (개념) domain 3에서 participant id 1의 discovery 유니캐스트 포트와 discovery 멀티캐스트 포트는?
정답/힌트: 7400 + 750 + 10 + 2 = 8162, 멀티캐스트는 7400 + 750 = 8150.
3. (코드) 예제 5의 `imu_pub`를 IIO **버퍼 모드**(`/dev/iio:deviceN`, hrtimer trigger, timestamp 채널)로 바꾸고, 커널 timestamp를 MONOTONIC→REALTIME offset으로 변환해 stamp에 넣어라. stamp 간격 분포가 sysfs 폴링 때보다 좁아지는지 lat_sub로 비교하라.
정답/힌트: I2의 버퍼 리더 코드 + `clock_gettime` 두 번으로 offset 계산. 커널 ts는 인터럽트 시각이라 스케줄 지터가 빠진다.
4. (코드) lat_sub에 `liveliness`(MANUAL_BY_TOPIC, lease 100 ms) 이벤트 콜백을 추가하고 publisher를 `kill -STOP`해서 이벤트가 오는지 확인하라.
정답/힌트: `opt.event_callbacks.liveliness_callback`, `QOSLivelinessChangedInfo.alive_count`. publisher 쪽도 같은 liveliness 종류여야 매칭된다.
5. (관찰) imu_pub이 돌 때 `strace -f -e trace=sendmsg,sendto,futex -p <pid>`로 Fast DDS와 Cyclone DDS의 syscall 패턴을 비교하라. 어느 쪽이 같은 호스트에서 소켓을 덜 쓰나?
정답/힌트: Fast DDS는 SHM 전송이라 데이터는 `/dev/shm` + futex/알림, Cyclone(iceoryx 미설정)은 loopback `sendmsg`.
6. (관찰) 두 게스트 실험(§11)에서 domain 0과 7의 discovery 패킷을 tcpdump로 잡고 목적지 포트가 식과 맞는지 확인하라.
정답/힌트: 239.255.0.1:7400, 239.255.0.1:9150.
7. (설계) 4코어 SoC에 IMU 400 Hz, 카메라 2대 30 fps, CAN 모터 브리지, EKF, rosbag 기록이 있다. executor·스레드·QoS·RMW 설정 지도를 그려라.
정답/힌트: IMU+EKF는 같은 프로세스(intra-process) 전용 RT executor + 격리 코어, IMU는 best-effort depth 5, 카메라는 컴포넌트로 묶고 기록은 별도 코어, CAN 명령 경로는 reliable + deadline 감시, DDS 수신 스레드 우선순위 지정.

---

## 21. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| node | 노드 | ROS 2 그래프의 참여자 하나 (이름 + 엔티티들) |
| topic | 토픽 | 이름·타입이 있는 1:N 비동기 메시지 스트림 |
| service / action | 서비스 / 액션 | 1:1 요청-응답 / 피드백·취소가 있는 긴 작업 |
| executor | 실행기 | 준비된 콜백을 골라 실행하는 루프 (스레드 단위) |
| callback group | 콜백 그룹 | 동시 실행 규칙 단위 (MutuallyExclusive / Reentrant) |
| rcl / rmw | ROS client library / ROS middleware interface | 언어 공통 C 계층 / 미들웨어 교체용 인터페이스 |
| DDS / RTPS | Data Distribution Service / Real-Time Publish-Subscribe | pub/sub 표준 API / 그 와이어 프로토콜 |
| participant | 참여자 | DDS의 도메인 참여 단위 (Jazzy: 프로세스 context당 1개) |
| SPDP / SEDP | participant / endpoint discovery | 멀티캐스트 존재 알림 / writer·reader 교환 |
| domain ID | 도메인 ID | 포트 공간을 나누는 번호 (`ROS_DOMAIN_ID`) |
| QoS | Quality of Service | reliability, durability, history, depth, deadline, liveliness, lifespan |
| request-vs-offered | 요청 대 제공 | subscriber 요청 ≤ publisher 제공일 때만 매칭 |
| sensor data QoS | 센서 프로파일 | best-effort + volatile + keep-last 5 |
| transient local | 과도 로컬 | publisher가 마지막 N개를 보관해 late joiner에게 줌 |
| `header.stamp` | 헤더 시각 | 메시지 데이터의 측정 시각 (ROS 시간) |
| `use_sim_time` | 시뮬레이션 시간 | 노드 시계를 `/clock` 토픽으로 |
| colcon | 빌드 도구 | 워크스페이스의 여러 패키지를 의존 순서로 빌드 |
| ament_cmake | 빌드 타입 | ROS 2 C++ 패키지의 CMake 매크로 모음 |
| rosbag2 / MCAP | 기록 도구 / 저장 형식 | 토픽 기록·재생 / Jazzy 기본 컨테이너 형식 |
| ros2_tracing | 추적 | LTTng 기반 ROS 2 tracepoint |
| micro-ROS / XRCE-DDS | MCU용 ROS 2 / 경량 DDS | MCU 클라이언트 + Linux Agent |
| meta-ros | Yocto 레이어 | ROS 2 패키지 레시피 모음 |
| loaned message | 대여 메시지 | 미들웨어 버퍼에 직접 써서 zero-copy |

---

## 22. 요약 & 체크리스트

ROS 2는 로봇 소프트웨어의 메시지 큐 표준이다. 노드는 토픽(1:N 스트림)과 서비스(1:1 RPC)로 이야기하고, 그 아래 rclcpp → rcl → rmw → DDS(RTPS) → UDP/공유 메모리 스택이 있다. 브로커 없이 SPDP/SEDP discovery로 서로를 찾으며 domain ID가 포트 공간을 나눈다. 토픽 양 끝은 QoS를 선언하고 "요청 ≤ 제공"일 때만 연결되므로, best-effort 센서 토픽에 reliable subscriber를 붙이면 조용히 아무것도 오지 않는다. 임베디드 엔지니어가 책임지는 품질은 드라이버 노드에서 결정된다: 측정 시각을 커널 타임스탬프로 찍고(`node->now()`는 기본 CLOCK_REALTIME), 단위·좌표계·공분산 규약을 지키고, 센서 QoS를 쓴다. 실시간은 ROS가 아니라 설계가 만든다: PREEMPT_RT, 코어 격리, mlockall, RT 전용 executor 스레드 + DDS 스레드 우선순위, 할당 없는 루프, 그리고 부하에서 잰 지연 분포. 이 노트의 랩에서 TCG 게스트로 이 모든 것을 직접 돌렸고(숫자는 실제 HW가 아님), 부하·QoS·RMW·우선순위에 따른 차이를 같은 도구로 비교했다.

- [ ] node/topic/service/action/parameter를 RTOS 개념에 대응시켜 설명할 수 있다
- [ ] rclcpp → rcl → rmw → DDS → UDP 스택을 그리고 각 층의 역할을 말할 수 있다
- [ ] SPDP/SEDP discovery와 domain → 포트 공식을 설명할 수 있다
- [ ] 멀티캐스트 없는 망에서 discovery를 구성하는 방법 두 가지를 말할 수 있다
- [ ] QoS 정책 7개와 매칭 규칙(요청 ≤ 제공)을 표로 그릴 수 있다
- [ ] 센서 토픽이 best-effort인 이유와 transient-local이 필요한 토픽을 말할 수 있다
- [ ] executor·callback group으로 콜백 굶김을 설명하고 고칠 수 있다
- [ ] `header.stamp`를 어디서 어떤 시계로 찍어야 하는지, MONOTONIC→ROS 시간 변환을 설명할 수 있다
- [ ] `sensor_msgs/Imu`를 단위·공분산 규약에 맞게 채우는 C++ 노드를 colcon으로 빌드할 수 있다
- [ ] end-to-end 지연·지터·손실을 재는 subscriber를 짜고 부하·QoS·RMW 비교를 할 수 있다
- [ ] ROS 2 노드를 RT로 만드는 체크리스트(커널·프로세스·스레드·DDS·메모리)를 말할 수 있다
- [ ] micro-ROS와 meta-ros가 시스템의 어디에 들어가는지 설명할 수 있다

## 참고 자료

- ROS 2 Jazzy 문서: [docs.ros.org/en/jazzy](https://docs.ros.org/en/jazzy/) — Concepts(About Quality of Service settings, About Executors, About Domain ID, About Different ROS 2 DDS/RTPS Vendors), Tutorials(Writing a simple publisher and subscriber (C++), Recording and playing back data), How-to(Improved Dynamic Discovery: `ROS_AUTOMATIC_DISCOVERY_RANGE`/`ROS_STATIC_PEERS`)
- ROS Enhancement Proposals: [REP-103](https://www.ros.org/reps/rep-0103.html)(단위·좌표 규약), [REP-105](https://www.ros.org/reps/rep-0105.html)(좌표 프레임), [REP-145](https://www.ros.org/reps/rep-0145.html)(IMU 드라이버)
- ROS 2 설계 문서: [design.ros2.org](https://design.ros2.org/) — ROS on DDS, ROS 2 middleware interface, Clock and Time
- OMG DDS / DDSI-RTPS 규격: [omg.org/spec/DDS](https://www.omg.org/spec/DDS/), [omg.org/spec/DDSI-RTPS](https://www.omg.org/spec/DDSI-RTPS/)
- Eclipse Cyclone DDS: [cyclonedds.io](https://cyclonedds.io/) / eProsima Fast DDS: [fast-dds.docs.eprosima.com](https://fast-dds.docs.eprosima.com/)
- ros2_tracing: [github.com/ros2/ros2_tracing](https://github.com/ros2/ros2_tracing)
- micro-ROS: [micro.ros.org](https://micro.ros.org/)
- meta-ros: [github.com/ros/meta-ros](https://github.com/ros/meta-ros)
- rosbag2: [github.com/ros2/rosbag2](https://github.com/ros2/rosbag2) / MCAP: [mcap.dev](https://mcap.dev/)
- 이 스터디의 관련 노트: A7, A9, F1, F8, I2, I5, I6, K4, K5, K6, L1, L8, M5, N2, N3, N4, O3, Q3, Q5, Q7
