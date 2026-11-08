# IPC 학습 가이드 (2026-11-08)

IPC(Inter-Process Communication, 프로세스 간 통신)는 "서로 메모리를 공유하지 않는 실행 단위끼리 데이터와 신호를 주고받는 방법"이다. 리눅스 프로세스 사이뿐 아니라 **RTOS 태스크 사이, 멀티코어 SoC의 코어 사이(메일박스·rpmsg), 호스트와 GPU 펌웨어 사이**까지 같은 질문이 반복된다: 복사를 몇 번 하나, 누가 기다리나, 상대가 죽으면 어떻게 아나.

중심 아이템은 **OS-B14 IPC 비교**다. 이번에 빠져 있던 주제 7개(POSIX 메시지 큐, fd 전달, 프로세스 공유 락, 알림용 fd, 견고한 공유 메모리 프로토콜, 미니 RPC, macOS Mach/XPC)를 OS 트랙에 🆕로 추가하고, 다른 트랙에 흩어진 IPC 아이템과 함께 8개 층으로 묶었다.

> **쓰는 법**
> - 층을 위에서부터 내려간다. 층마다 **면접 체크**를 말로 설명할 수 있으면 다음 층으로 간다.
> - ⭐ 표시 15개가 **핵심 경로**다. 시간이 없으면 ⭐만 순서대로 푼다.
> - 🆕 표시는 이 가이드를 만들며 새로 추가한 아이템이다 (7개).
> - 아이템 ID를 누르면 트랙 표의 그 줄로 간다. 레슨이 써졌는지(학습가능/준비중)는 그 표에서 실시간으로 보인다.
> - env가 `linux`면 Lima VM `elinux`, `qemu`면 QEMU, `gpu`면 NVIDIA GPU가 필요하다. 나머지는 Mac에서 바로 된다.

## 한눈에: 8개 층

| 층 | 주제 | 주로 나오는 트랙 | 아이템 수 | ⭐ |
|---|---|---|---|---|
| 1. POSIX 기본기 | 파이프 · 시그널 · mmap · 소켓 · 비교 측정 | OS · NET | 6 | 4 |
| 2. 메시지 · 소켓 심화 | POSIX MQ · fd 전달 · RPC · 직렬화 | OS · DIST | 5 | 2 |
| 3. 공유 메모리와 동기화 | 프로세스 공유 락 · lock-free 링 · 장애 감지 · 제로카피 | ROS · OS · DIST · DS | 6 | 3 |
| 4. 알림 · 이벤트 | eventfd · signalfd · pidfd · epoll/kqueue | OS · VIRT | 4 | 1 |
| 5. RTOS · MCU | 메시지 큐 · 세마포어 신호 · 태스크 알림 | RTOS | 3 | 1 |
| 6. 멀티코어 SoC · 디바이스 IPC | 메일박스 · rpmsg/vring · 펌웨어 RPC · PF↔VF | PLAT · GPUSYS · ARCH · NETHW | 5 | 2 |
| 7. 리눅스 시스템 미들웨어 | mmap 드라이버 · D-Bus · pub/sub · DDS | ROS · BMC · ELX · WCONN | 7 | 1 |
| 8. 플랫폼별 IPC | macOS Mach · XPC · Android Binder 비교 | OS | 1 | 1 |

합계 37개 · ⭐ 15개. 층 안의 순서가 권장 순서다.

## 1. POSIX 기본기

모든 IPC의 출발점이다. OS-B14에서 같은 하네스로 파이프 · FIFO · Unix 소켓 · 공유 메모리의 지연과 처리량을 직접 비교한다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [OS-B02](../tracks/OS.html#OS-B02) ⭐ | Basic · mac | 파일 디스크립터·dup2·파이프: 파이프라인과 리다이렉션 | fd 테이블 상속과 dup2로 셸 파이프라인이 만들어지는 원리를 구현한다 |
| [OS-B07](../tracks/OS.html#OS-B07) | Basic · mac | 시그널: 핸들러, async-signal-safe, self-pipe | 시그널 핸들러에서 할 수 있는 일의 제약과 안전한 처리 패턴을 익힌다 |
| [OS-B09](../tracks/OS.html#OS-B09) ⭐ | Basic · mac | mmap: 파일 매핑, 익명 매핑, MAP_SHARED vs PRIVATE | 메모리 매핑 I/O와 COW private 매핑의 의미를 실험으로 확인한다 |
| [OS-B14](../tracks/OS.html#OS-B14) ⭐ | Basic · mac | IPC 비교: 파이프·FIFO·Unix 소켓·공유 메모리 | IPC 수단별 지연·처리량·사용 패턴의 차이를 측정한다 |
| [OS-M02](../tracks/OS.html#OS-M02) ⭐ | Mid · mac | 컨텍스트 스위치 비용 측정: 파이프 핑퐁 | 프로세스·스레드 간 문맥 교환 비용을 측정하고 측정 오차 요인을 통제한다 |
| [NET-B09](../tracks/NET.html#NET-B09) | Basic · mac | 소켓 API 수명주기: listen backlog, TIME_WAIT, half-close | TCP 소켓 시스템콜이 커널 상태와 어떻게 대응하는지 실험으로 확인한다. |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ 파이프 · Unix 소켓 · 공유 메모리의 복사 횟수와 커널 개입 차이
- ☐ `MAP_SHARED`와 `MAP_PRIVATE`의 차이 (COW)
- ☐ 시그널 핸들러에서 할 수 있는 일이 제한되는 이유와 self-pipe 트릭
- ☐ IPC 왕복 지연에 문맥 교환 비용이 얼마나 들어가나

## 2. 메시지 · 소켓 심화

"바이트 스트림" 위에 "메시지"와 "요청/응답"을 얹는 단계다. 프레이밍과 버전 호환이 핵심이다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [OS-M15](../tracks/OS.html#OS-M15) 🆕 | Mid · linux | POSIX 메시지 큐: mq_open·우선순위·mq_notify | 커널이 메시지 경계와 우선순위를 관리하는 IPC의 동작과 한계를 안다. |
| [OS-M16](../tracks/OS.html#OS-M16) ⭐ 🆕 | Mid · mac | Unix 소켓 심화: fd 전달(SCM_RIGHTS)과 피어 자격 확인 | Unix 도메인 소켓만 가능한 기능인 파일 디스크립터 전달과 피어 인증을 구현한다. |
| [DIST-B06](../tracks/DIST.html#DIST-B06) | Basic · mac | 직렬화 기초: Varint·ZigZag·TLV | protobuf 와이어 포맷의 핵심 인코딩을 직접 구현한다 |
| [OS-A13](../tracks/OS.html#OS-A13) ⭐ 🆕 | Adv · mac | 미니 RPC 계층: 요청/응답·타임아웃·취소·스트리밍 | IPC 위에 RPC를 얹을 때 필요한 프레이밍·상관 ID·타임아웃·버전 호환을 설계한다. |
| [DIST-B08](../tracks/DIST.html#DIST-B08) | Basic · mac | Pub/Sub 브로커와 QoS | 토픽 기반 발행/구독과 전달 보장 수준을 구현한다 |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ 바이트 스트림(SOCK_STREAM)에서 메시지 경계를 지키는 방법 (길이 prefix)
- ☐ SCM_RIGHTS로 fd를 넘기면 받는 쪽에서는 무엇이 생기나 (같은 open file description)
- ☐ POSIX 메시지 큐와 파이프의 차이 (메시지 경계, 우선순위, 영속성)
- ☐ RPC에서 요청 ID · 타임아웃 · 취소가 필요한 이유, 필드 추가 시 하위 호환

## 3. 공유 메모리와 동기화

가장 빠른 IPC지만 가장 위험하다. 동기화와 "상대가 죽었을 때"를 직접 설계해야 한다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [OS-M17](../tracks/OS.html#OS-M17) ⭐ 🆕 | Mid · linux | 프로세스 간 공유 동기화: PROCESS_SHARED 뮤텍스와 robust mutex | 공유 메모리 위의 락이 프로세스 경계를 넘을 때와 락 보유자가 죽었을 때의 처리를 안다. |
| [DS-A01](../tracks/DS.html#DS-A01) ⭐ | Adv · mac | Lock-free SPSC 큐: C11 atomics | 단일 생산자/소비자 큐를 acquire/release만으로 정확하고 빠르게 만든다 |
| [OS-A12](../tracks/OS.html#OS-A12) ⭐ 🆕 | Adv · mac | 견고한 공유 메모리 IPC: 버전·백프레셔·상대 프로세스 장애 감지 | 빠른 공유 메모리 채널을 실서비스에서 쓸 수 있게 만드는 프로토콜 설계 요소를 익힌다. |
| [ROS-A05](../tracks/ROS.html#ROS-A05) | Adv · mac | 미니 미들웨어 4: shared-memory 전송 | 프로세스 간 zero-copy 전송(iceoryx 방식)을 구현하고 비교한다. |
| [ROS-M06](../tracks/ROS.html#ROS-M06) | Mid · linux | Intra-process & zero-copy 통신 | 같은 프로세스 내 복사 없는 메시지 전달과 그 조건을 이해한다. |
| [DIST-B07](../tracks/DIST.html#DIST-B07) | Basic · mac | Zero-copy 직렬화 (FlatBuffers 개념) | 파싱 없이 버퍼에서 바로 읽는 포맷의 구조와 trade-off를 이해한다 |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ 공유 메모리에 일반 뮤텍스를 두면 안 되는 이유 (PROCESS_SHARED)
- ☐ 락을 쥔 프로세스가 죽으면? robust mutex의 EOWNERDEAD
- ☐ 프로세스 간 SPSC 링에서 head/tail의 memory order
- ☐ 공유 메모리 채널에 버전 · 하트비트 · 시퀀스 번호가 필요한 이유
- ☐ 제로카피가 "공짜"가 아닌 경우 (소유권 관리, 캐시 일관성)

## 4. 알림 · 이벤트

데이터 없이 "일어났다"만 알리는 IPC다. 공유 메모리와 짝을 이뤄 "데이터는 공유 메모리로, 알림은 fd로"가 표준 패턴이다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [OS-M18](../tracks/OS.html#OS-M18) ⭐ 🆕 | Mid · linux | 알림용 fd IPC: eventfd·signalfd·pidfd를 epoll 하나로 | 데이터 대신 '일어났다'만 전달하는 알림 IPC를 이벤트 루프에 통합한다. |
| [OS-M12](../tracks/OS.html#OS-M12) | Mid · mac | I/O 모델 비교: blocking·non-blocking·select·kqueue 에코 서버 | 스레드-per-연결과 이벤트 루프 모델의 차이를 구현과 부하 테스트로 체감한다 |
| [OS-M13](../tracks/OS.html#OS-M13) | Mid · linux | epoll 에코 서버: level vs edge triggered | 리눅스 epoll의 LT/ET 의미와 ET에서 EAGAIN까지 읽어야 하는 이유를 확인한다 |
| [VIRT-A04](../tracks/VIRT.html#VIRT-A04) | Adv · linux | ioeventfd·irqfd: VMM을 거치지 않는 알림 경로 | virtio 알림이 사용자공간 VMM 왕복 없이 커널 안에서 처리되는 구조를 구현하고 지연을 비교한다. |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ eventfd 카운터 모드와 EFD_SEMAPHORE의 차이
- ☐ 시그널을 이벤트 루프에서 안전하게 받는 방법 (signalfd vs self-pipe)
- ☐ ioeventfd/irqfd가 VM에서 VMM 왕복을 없애는 원리

## 5. RTOS · MCU

펌웨어에서는 같은 주소 공간 안의 태스크끼리 통신하지만, 질문은 똑같다: 복사 vs 포인터, 블로킹, 큐가 가득 차면?

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [RTOS-B11](../tracks/RTOS.html#RTOS-B11) ⭐ | Basic · mac | 메시지 큐: 생산자-소비자, 타임아웃, 복사 vs 포인터 | 큐 기반 태스크 간 통신 설계와 데이터 복사 비용·소유권 문제를 이해한다 |
| [RTOS-B08](../tracks/RTOS.html#RTOS-B08) | Basic · mac | 바이너리 세마포어: ISR → 태스크 신호 | 이벤트 신호용 바이너리 세마포어의 의미와 이벤트 유실 가능성을 이해한다 |
| [RTOS-M01](../tracks/RTOS.html#RTOS-M01) | Mid · mac | 태스크 알림: 세마포어·이벤트 그룹의 경량 대체 | task notification의 동작(값 덮어쓰기/증가/비트 OR)과 성능 이점·제약(수신자 1명)을 안다 |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ 큰 데이터를 큐로 보낼 때 복사 vs 포인터 + 풀
- ☐ ISR에서 태스크로 알릴 때 세마포어 vs 태스크 알림

## 6. 멀티코어 SoC · 디바이스 IPC

펌웨어 · 실리콘 면접에서 가장 직접적으로 쓰이는 층이다. 코어 사이에는 OS가 없으니 공유 메모리 + doorbell 인터럽트로 직접 만든다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [PLAT-M05](../tracks/PLAT.html#PLAT-M05) ⭐ | Mid · mac | 멀티코어 메일박스 IPC: 공유 메모리 + doorbell | 이기종 코어(AP↔MCU/DSP) 간 메시지 프로토콜을 설계한다. |
| [PLAT-M06](../tracks/PLAT.html#PLAT-M06) ⭐ | Mid · mac | remoteproc/rpmsg 개념과 virtio vring 구현 | 리눅스가 보조 코어 펌웨어를 로드·통신하는 표준 구조를 이해한다. |
| [ARCH-M12](../tracks/ARCH.html#ARCH-M12) | Mid · mac | DMA와 캐시 일관성: Clean/Invalidate 시나리오 | non-coherent DMA에서 stale 데이터 버그와 올바른 캐시 유지보수 순서를 이해한다 |
| [GPUSYS-M05](../tracks/GPUSYS.html#GPUSYS-M05) | Mid · mac | GPU 펌웨어 마이크로컨트롤러 RPC (GSP 스타일) | 드라이버 기능 다수를 GPU 내부 펌웨어로 옮긴 구조와 호스트-펌웨어 메시지 큐를 구현한다 |
| [NETHW-A03](../tracks/NETHW.html#NETHW-A03) | Adv · mac | SR-IOV eswitch: VF representor와 PF↔VF mailbox | SR-IOV NIC 내부 스위치와 PF/VF 드라이버 간 제어 프로토콜을 구현한다. |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ 메일박스 프로토콜에서 메모리 배리어가 들어가야 하는 위치 (데이터 쓰기 → 배리어 → doorbell)
- ☐ virtio vring의 desc · avail · used 세 링이 각각 누구 소유인가
- ☐ non-coherent 공유 버퍼에서 clean/invalidate 순서
- ☐ 호스트 드라이버 ↔ GPU 펌웨어 RPC에서 타임아웃과 복구

## 7. 리눅스 시스템 미들웨어

실제 제품에서 쓰는 IPC 프레임워크다. BMC(D-Bus), 로봇(ROS 2 / DDS), 블루투스(BlueZ D-Bus)가 대표다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [ELX-M03](../tracks/ELX.html#ELX-M03) | Mid · linux | mmap 드라이버: 커널 버퍼를 사용자공간에 매핑 | remap_pfn_range/vm_fault 방식 차이와 캐시 속성을 이해한다. |
| [BMC-M04](../tracks/BMC.html#BMC-M04) ⭐ | Mid · linux | OpenBMC D-Bus 아키텍처: 센서 객체와 ObjectMapper | OpenBMC가 데몬 간 통신을 D-Bus 객체/인터페이스/프로퍼티 신호로 하는 구조를 직접 다룬다 |
| [BMC-A06](../tracks/BMC.html#BMC-A06) | Adv · linux | OpenBMC 스타일 센서 데몬 개발 (sdbusplus) | 실제 OpenBMC 데몬처럼 C++ sdbusplus로 하드웨어 센서를 D-Bus에 게시하고 Redfish까지 이어지는 경로를 이해한다 |
| [WCONN-M08](../tracks/WCONN.html#WCONN-M08) | Mid · linux | BlueZ D-Bus GATT 서버: 프로비저닝 서비스 | BlueZ의 D-Bus API(GattManager1, LEAdvertisingManager1)로 주변기기 역할 GATT 서비스를 등록·광고한다. |
| [ROS-B01](../tracks/ROS.html#ROS-B01) | Basic · mac | 미니 pub/sub 1: 인프로세스 타입 안전 토픽 | publish/subscribe 패턴의 핵심(토픽 레지스트리, 콜백 디스패치)을 구현한다. |
| [ROS-B03](../tracks/ROS.html#ROS-B03) | Basic · mac | 미니 pub/sub 2: UDP 멀티캐스트 & discovery | 프로세스 간 전송과 참여자 자동 발견 원리를 구현한다. |
| [ROS-B08](../tracks/ROS.html#ROS-B08) | Basic · linux | ROS 2 계층 구조: rclcpp → rcl → rmw → DDS | publish 호출이 어떤 계층을 거쳐 네트워크로 나가는지 추적한다. |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ D-Bus의 객체 경로 · 인터페이스 · 시그널 모델
- ☐ pub/sub와 요청/응답(RPC)은 언제 어느 쪽을 쓰나
- ☐ 커널 버퍼를 사용자 공간에 mmap으로 노출할 때의 주의점

## 8. 플랫폼별 IPC

Apple 면접이라면 특히 알아 둘 층이다. 포트 "권한(right)"을 주고받는 모델이 POSIX와 어떻게 다른지 본다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [OS-A14](../tracks/OS.html#OS-A14) ⭐ 🆕 | Adv · mac | macOS Mach 메시지와 XPC: 포트 권한 기반 IPC | Apple 플랫폼 IPC의 기반인 Mach 포트(권한·메시지)와 XPC 서비스 구조를 이해한다. |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ Mach 포트의 receive right와 send right 차이
- ☐ launchd가 서비스 이름을 포트에 연결하는 방식
- ☐ Binder의 단일 복사(single copy)가 가능한 이유

## ⭐ 핵심 경로 15개

임베디드 리눅스 · 펌웨어 · 플랫폼 면접 기준. 하루 2개면 약 8일.

| # | 아이템 | 제목 | 층 |
|---|---|---|---|
| 1 | [OS-B02](../tracks/OS.html#OS-B02) | 파일 디스크립터·dup2·파이프: 파이프라인과 리다이렉션 | 1층 |
| 2 | [OS-B14](../tracks/OS.html#OS-B14) | IPC 비교: 파이프·FIFO·Unix 소켓·공유 메모리 | 1층 |
| 3 | [OS-B09](../tracks/OS.html#OS-B09) | mmap: 파일 매핑, 익명 매핑, MAP_SHARED vs PRIVATE | 1층 |
| 4 | [OS-M02](../tracks/OS.html#OS-M02) | 컨텍스트 스위치 비용 측정: 파이프 핑퐁 | 1층 |
| 5 | [OS-M16](../tracks/OS.html#OS-M16) | Unix 소켓 심화: fd 전달(SCM_RIGHTS)과 피어 자격 확인 | 2층 |
| 6 | [OS-M17](../tracks/OS.html#OS-M17) | 프로세스 간 공유 동기화: PROCESS_SHARED 뮤텍스와 robust mutex | 3층 |
| 7 | [DS-A01](../tracks/DS.html#DS-A01) | Lock-free SPSC 큐: C11 atomics | 3층 |
| 8 | [OS-A12](../tracks/OS.html#OS-A12) | 견고한 공유 메모리 IPC: 버전·백프레셔·상대 프로세스 장애 감지 | 3층 |
| 9 | [OS-M18](../tracks/OS.html#OS-M18) | 알림용 fd IPC: eventfd·signalfd·pidfd를 epoll 하나로 | 4층 |
| 10 | [RTOS-B11](../tracks/RTOS.html#RTOS-B11) | 메시지 큐: 생산자-소비자, 타임아웃, 복사 vs 포인터 | 5층 |
| 11 | [PLAT-M05](../tracks/PLAT.html#PLAT-M05) | 멀티코어 메일박스 IPC: 공유 메모리 + doorbell | 6층 |
| 12 | [PLAT-M06](../tracks/PLAT.html#PLAT-M06) | remoteproc/rpmsg 개념과 virtio vring 구현 | 6층 |
| 13 | [OS-A13](../tracks/OS.html#OS-A13) | 미니 RPC 계층: 요청/응답·타임아웃·취소·스트리밍 | 2층 |
| 14 | [BMC-M04](../tracks/BMC.html#BMC-M04) | OpenBMC D-Bus 아키텍처: 센서 객체와 ObjectMapper | 7층 |
| 15 | [OS-A14](../tracks/OS.html#OS-A14) | macOS Mach 메시지와 XPC: 포트 권한 기반 IPC | 8층 |

## 직무별로 더할 것

| 목표 직무 | 더할 아이템 | 이유 |
|---|---|---|
| 펌웨어 · 멀티코어 SoC | [PLAT-M05](../tracks/PLAT.html#PLAT-M05) · [PLAT-M06](../tracks/PLAT.html#PLAT-M06) · [ARCH-M12](../tracks/ARCH.html#ARCH-M12) · [RTOS-M01](../tracks/RTOS.html#RTOS-M01) · [GPUSYS-M05](../tracks/GPUSYS.html#GPUSYS-M05) | 코어 간 메일박스와 공유 버퍼 일관성 |
| 임베디드 리눅스 · BSP · BMC | [ELX-M03](../tracks/ELX.html#ELX-M03) · [OS-M18](../tracks/OS.html#OS-M18) · [BMC-M04](../tracks/BMC.html#BMC-M04) · [BMC-A06](../tracks/BMC.html#BMC-A06) · [OS-M15](../tracks/OS.html#OS-M15) | 드라이버 mmap, 이벤트 루프, D-Bus 서비스 |
| 로봇 · 자율 시스템 | [ROS-B01](../tracks/ROS.html#ROS-B01) · [ROS-B03](../tracks/ROS.html#ROS-B03) · [ROS-M06](../tracks/ROS.html#ROS-M06) · [ROS-A05](../tracks/ROS.html#ROS-A05) · [OS-A12](../tracks/OS.html#OS-A12) | pub/sub와 제로카피, 장애에 강한 공유 메모리 |
| Apple 플랫폼 | [OS-A14](../tracks/OS.html#OS-A14) · [OS-M16](../tracks/OS.html#OS-M16) · [OS-A13](../tracks/OS.html#OS-A13) | Mach/XPC, fd 전달, RPC 설계 |

## 같은 개념이 여러 트랙에 나오는 이유

중복처럼 보이지만 계층이나 도메인이 달라서 일부러 둔 것들이다.

| 개념 | 아이템 | 무엇이 다른가 |
|---|---|---|
| 공유 메모리 링 | [DS-A01](../tracks/DS.html#DS-A01) · [OS-A12](../tracks/OS.html#OS-A12) · [PLAT-M06](../tracks/PLAT.html#PLAT-M06) · [ROS-A05](../tracks/ROS.html#ROS-A05) | 자료구조 정확성 → 장애 감지 프로토콜 → SoC vring → 로봇 제로카피 |
| 메시지 큐 | [RTOS-B11](../tracks/RTOS.html#RTOS-B11) · [OS-M15](../tracks/OS.html#OS-M15) · [DIST-B08](../tracks/DIST.html#DIST-B08) | RTOS 태스크 → POSIX 커널 큐 → 분산 브로커 |
| RPC | [OS-A13](../tracks/OS.html#OS-A13) · [GPUSYS-M05](../tracks/GPUSYS.html#GPUSYS-M05) · [BMC-M04](../tracks/BMC.html#BMC-M04) | 직접 구현 → 펌웨어 RPC → D-Bus 프레임워크 |
| 알림 | [OS-B07](../tracks/OS.html#OS-B07) · [OS-M18](../tracks/OS.html#OS-M18) · [VIRT-A04](../tracks/VIRT.html#VIRT-A04) · [RTOS-M01](../tracks/RTOS.html#RTOS-M01) | self-pipe → eventfd/signalfd → VM ioeventfd → RTOS 태스크 알림 |

## 참고자료

- Michael Kerrisk, *The Linux Programming Interface* — 43장(IPC 개요)부터 파이프·FIFO·POSIX IPC·공유 메모리·소켓 장까지
- W. Richard Stevens, *UNIX Network Programming Vol. 2: Interprocess Communications*
- man7.org: `mq_overview(7)`, `unix(7)`, `eventfd(2)`, `pthread_mutexattr_setrobust(3)` — https://man7.org/linux/man-pages/
- OASIS virtio 명세 (split virtqueue) — https://docs.oasis-open.org/virtio/virtio/
- Apple, *Daemons and Services Programming Guide* (launchd · XPC)
- 동시성 연습 가이드 — 공유 메모리 위의 원자 연산 · 메모리 순서는 그쪽 문서에서
