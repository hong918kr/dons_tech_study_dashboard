# N4. Linux vs RTOS vs MCU 분담 — 무엇을 MCU로 내리나, 아키텍처 판단 기준

> **이 노트를 다 읽으면**: 기능 하나하나를 "deadline · 놓쳤을 때 결과 · 인증 · Linux가 죽어도 살아야 하나 · 부팅 시간 · 전원 · 생태계"로 따져서 Linux / RT 코어 / MCU / safety MCU 중 어디에 둘지 근거를 대며 정할 수 있다 · 다섯 가지 분할 패턴(PREEMPT_RT 단독, 외부 MCU, 이종 SoC + remoteproc/rpmsg, hypervisor 분할, dual-kernel)의 장단점을 표로 비교할 수 있다 · 두 프로세서 사이 인터페이스(프레임 · CRC · seq · 타임스탬프 · 시간 동기 · heartbeat · failsafe · 버전/OTA)를 설계하고 C로 짤 수 있다 · "Linux가 죽으면 모터는?"에 실제 측정 타임라인으로 답할 수 있다
> **왜 필요한가**: 로보틱스 · 드론 · 국방 · 카메라 회사의 Embedded 면접에서 거의 반드시 나오는 설계 질문이다 — "우리 로봇에서 뭘 MCU에 두고 뭘 Linux에 두겠나?", "Linux가 crash하면 모터는 어떻게 되나?", "Linux와 MCU 시간을 어떻게 맞추나?". 실무에서는 첫 보드 설계 전에 이 경계가 정해지고, 잘못 정하면 보드 리스핀이나 인증 실패로 돌아온다
> **Don 기준 난이도**: MCU/RTOS 펌웨어 · 인터럽트 지연 · watchdog · 링버퍼 · 버스 프로토콜은 Don이 이미 잘 안다 (SSD 컨트롤러 = Cortex-R 여러 개가 역할을 나눠 갖는 이종 시스템 그 자체) / 새로 배울 부분은 **Linux 쪽에서 본 경계** — Linux가 무엇을 보장하지 못하는지(N1), 경계를 넘는 통신과 시간을 어떻게 다루는지, remoteproc/rpmsg 같은 Linux 프레임워크, 그리고 이걸 면접에서 "판단 기준 → 패턴 → 숫자" 순서로 말하는 법
> **선행 노트**: N1(Linux는 실시간인가 — 지연 원인), N2(실시간 튜닝 — SCHED_FIFO · isolcpus), N3(지연 측정 — cyclictest). 같이 보면 좋은 노트: L1(systemd watchdog), L4(OTA — MCU 펌웨어 업데이트도 번들에 넣기), L7(crash 수집), C7(부팅 시간), H-모듈(UART/SPI/CAN 드라이버). 이종 코어(remoteproc/rpmsg)의 깊은 내용은 **O 모듈에서 다룸**, CAN/로보틱스 도메인 스택은 **Q 모듈에서 다룸**

---

## 0. 큰 그림 — "뇌 · 척수 · 반사신경"

Don이 SSD 컨트롤러에서 일하던 구조를 떠올려 보자. 컨트롤러 안에는 Cortex-R 코어가 여러 개 있었고, 역할이 갈려 있었다. 호스트 명령(NVMe)을 받는 front-end 코어, FTL을 돌리는 코어, NAND 채널을 직접 두드리는 back-end 코어. NAND 타이밍처럼 **마이크로초 단위로 틀리면 데이터가 깨지는 일**은 하드웨어 시퀀서와 back-end 코어가 맡았고, 큰 판단(GC 정책, wear leveling)은 위쪽 코어가 했다. 아무도 "FTL 코어가 NAND 신호 타이밍까지 직접 만들자"고 하지 않았다.

로봇이나 드론도 똑같다. 차이는 위쪽 "뇌"가 **Linux**라는 점이다. Linux는 카메라 · 네트워크 · ML · 파일시스템 · OTA를 공짜로 주지만, N1에서 본 것처럼 **최악 지연을 보장하지 않는다**. 그래서 설계는 언제나 이렇게 쪼개진다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13">실행 주체</text>
  <text x="200" y="22" font-size="13">맡는 일 (로봇 예)</text>
  <text x="470" y="22" font-size="13">주기 · deadline 놓치면</text>
  <rect x="20" y="34" width="640" height="62" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="32" y="60" font-size="13">Linux (Cortex-A)</text>
  <text x="32" y="80" font-size="12">"뇌"</text>
  <text x="200" y="60" font-size="13">인지 · 계획 · UI · 네트워크</text>
  <text x="200" y="80" font-size="12">카메라, SLAM, ML, 로그, OTA</text>
  <text x="470" y="60" font-size="13">10 ~ 100 Hz</text>
  <text x="470" y="80" font-size="12">놓치면 성능 저하</text>
  <rect x="20" y="104" width="640" height="62" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="32" y="130" font-size="13">RT 코어 / RTOS</text>
  <text x="32" y="150" font-size="12">"척수"</text>
  <text x="200" y="130" font-size="13">관절 제어 · 센서 융합 · 궤적 보간</text>
  <text x="200" y="150" font-size="12">Cortex-R5F / M7, 또는 PREEMPT_RT 격리 코어</text>
  <text x="470" y="130" font-size="13">500 Hz ~ 2 kHz</text>
  <text x="470" y="150" font-size="12">놓치면 진동 · 추종 오차</text>
  <rect x="20" y="174" width="640" height="62" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="32" y="200" font-size="13">MCU + 타이머 HW</text>
  <text x="32" y="220" font-size="12">"반사신경"</text>
  <text x="200" y="200" font-size="13">FOC 전류 루프 · PWM · 엔코더</text>
  <text x="200" y="220" font-size="12">ADC 트리거와 PWM이 HW로 묶임</text>
  <text x="470" y="200" font-size="13">10 ~ 40 kHz</text>
  <text x="470" y="220" font-size="12">놓치면 과전류 · 파손</text>
  <rect x="20" y="244" width="640" height="62" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="32" y="270" font-size="13">Safety MCU / HW</text>
  <text x="32" y="290" font-size="12">"차단기"</text>
  <text x="200" y="270" font-size="13">E-stop · watchdog · STO · 브레이크</text>
  <text x="200" y="290" font-size="12">lockstep 코어, 인증된 코드, 전원 상시</text>
  <text x="470" y="270" font-size="13">항상 · 수 ms 안 반응</text>
  <text x="470" y="290" font-size="12">놓치면 사람이 다침</text>
</svg>
```

그림 1 — 로봇 기능을 주기와 "놓쳤을 때의 결과"로 네 층으로 나눈다. 아래로 갈수록 deadline이 짧고 결과가 심각하며, 그만큼 단순하고 검증 가능한 실행 주체에 둔다.

핵심 원칙은 세 줄로 요약된다.

1. **제어 루프는 그 루프의 deadline을 보장할 수 있는 가장 위쪽 층에서 닫는다.** FOC 전류 루프를 Linux 유저스페이스에서 닫지 않는다. 반대로 경로 계획을 MCU에 억지로 넣지도 않는다.
2. **위층이 죽어도 아래층은 안전한 상태로 간다.** 아래층은 위층을 heartbeat로 감시하고, 끊기면 스스로 safe state(PWM off, 브레이크)로 간다. 위층의 "정상 종료"를 기다리지 않는다.
3. **층 사이 인터페이스는 "명령(setpoint)"이지 "제어 신호"가 아니다.** Linux는 "관절 3을 이 궤적으로" 를 보내고, 1 kHz 보간과 20 kHz 전류 제어는 아래층이 한다. 그래야 경계를 넘는 링크의 지터(이 노트 §6에서 측정)가 루프에 들어가지 않는다.

이 노트의 흐름은 이렇다. §1 판단 기준 → §2 판단 흐름도와 기능별 매트릭스 → §3 다섯 가지 분할 패턴 → §4 실제 제품 구조 → §5~§9 인터페이스 설계와 측정(프레임 · RTT · 시간 동기 · heartbeat · 버전/OTA) → §10 커널 안(remoteproc/rpmsg) → 실무 · 함정 · 면접.

### 실습 환경 한 줄

- VM 호스트: Lima `elinux`, `uname -r` = `7.0.0-38-generic` (aarch64). 이 노트에서 **"MCU" 역할을 하는 프로그램은 VM 호스트에서 일반 프로세스로** 돈다.
- 게스트: QEMU `virt` (Cortex-A57 x2, TCG), 커널 `6.18.55` (`/var/lab/out/Image`, PREEMPT, HZ=250), BusyBox initramfs. **"Linux 쪽"** 프로그램은 여기서 돈다.
- 둘을 잇는 "UART": QEMU `virt`의 두 번째 PL011(`/dev/ttyAMA1`, MMIO `0x9040000`)을 호스트 pty에 연결했다 (`-chardev pty,id=c1,path=... -serial chardev:c1`).
- **주의 — 숫자의 의미**: QEMU는 KVM 없이 TCG(명령어 번역)로 돌고, 측정 시점 VM은 다른 노트 작성자 16명과 공유되어 load average가 6~13이었다. 그리고 QEMU PL011은 baud rate를 흉내 내지 않는다(바이트가 즉시 전달). 그래서 **절대값은 실제 보드 숫자가 아니다**. 이 노트가 측정으로 보여 주려는 것은 절대값이 아니라 **분포의 모양** — "중앙값은 괜찮은데 꼬리(p99.9, max)가 수십~수백 배"라는 Linux 경로의 성질, 그리고 heartbeat/failsafe가 **설계대로 동작하는지**다.

---

## 1. 판단 기준 — 무엇을 따지나

"이 기능을 어디에 두나?"는 감으로 정하면 안 된다. 면접에서도 실무에서도 아래 기준을 하나씩 짚으면 답이 나온다.

### 1.1 deadline과 지터 — "얼마나 빨리"보다 "얼마나 확실히"

- **deadline**: 이벤트(인터럽트, 샘플)부터 반응(출력)까지 허용되는 최대 시간.
- **jitter**: 주기 작업의 실제 실행 시점이 이상적 시점에서 흔들리는 정도.
- **hard vs soft**: 한 번이라도 놓치면 실패(hard) / 가끔 놓치면 품질 저하(soft). 자세한 정의는 N1.

Linux의 문제는 평균이 아니라 **꼬리**다. N1 · N3에서 본 것처럼 PREEMPT_RT + 격리 코어 + 튜닝된 보드에서 cyclictest 최악값은 보통 수십 µs 수준까지 내려간다(일반적인 보고 범위, 보드마다 다름 — 기억 기반). 그런데 그건 **커널 스케줄링 지연**만이다. 실제 루프는 "센서 읽기(드라이버 · 버스) → 유저스페이스 계산 → 출력(드라이버 · 버스)"를 다 거친다. 각 단계의 꼬리가 더해진다. MCU의 타이머 인터럽트는 수십 ~ 수백 ns 단위로 결정론적이고, PWM · ADC 트리거처럼 **하드웨어가 직접 묶은 경로**는 지터가 사실상 0이다.

| deadline 범위 | 대표 기능 | 현실적인 실행 주체 |
|---|---|---|
| < 10 µs | PWM 데드타임, 과전류 차단, ADC-PWM 동기 | 타이머/비교기 **하드웨어** (MCU 주변장치, FPGA) |
| 10 ~ 100 µs | FOC 전류 루프 (10~40 kHz) | MCU ISR (Cortex-M4F/M7/M33, DSP, R5F) |
| 100 µs ~ 1 ms | 관절 위치/속도 루프 (1 kHz), IMU 융합 | MCU/RTOS 또는 RT 코어, 조건부로 PREEMPT_RT 격리 코어 |
| 1 ~ 10 ms | 보행/균형 제어 (수백 Hz), EtherCAT 마스터 | PREEMPT_RT (튜닝 + 측정 필수) 또는 RT 코어 |
| 10 ~ 100 ms | 인지 · 계획 · 로컬라이제이션 | Linux 일반 스레드 |
| > 100 ms | UI, 로깅, 네트워크, OTA | Linux 프로세스 |

### 1.2 놓쳤을 때의 결과 — 위험 분석이 먼저

같은 1 kHz라도 "놓치면 로그 한 줄이 늦다"와 "놓치면 팔이 사람 쪽으로 튄다"는 다르다. 실무에서는 FMEA나 hazard analysis 결과가 기능별로 "안전 관련 여부"를 정해 주고, 그게 실행 주체를 강하게 제약한다.

### 1.3 안전 인증 — Linux는 (아직) 인증하기 어렵다

| 표준 | 분야 | 등급 |
|---|---|---|
| IEC 61508 | 산업 일반 기능 안전 (모체 표준) | SIL 1~4 |
| ISO 26262 | 자동차 | ASIL A~D |
| ISO 13849 / IEC 62061 | 기계 안전 (로봇 셀, AMR) | PL a~e / SIL |
| DO-178C | 항공 소프트웨어 | DAL A~E |
| IEC 62304 | 의료기기 소프트웨어 | Class A~C |

인증은 "코드가 요구사항에서 추적되고, 커버리지(예: DO-178C DAL A의 MC/DC)가 나오고, 개발 프로세스가 문서화되었는가"를 묻는다. 수천만 줄, 수천 명 기여자의 Linux 커널은 이 방식으로 증명하기가 현실적으로 매우 어렵다. Linux Foundation의 ELISA 프로젝트처럼 안전 시스템에 Linux를 쓰는 방법을 연구하는 활동이 있지만(기억 기반), 오늘날 실무 표준 답은 **"안전 기능은 인증된 RTOS(SafeRTOS, QNX, VxWorks Cert, PikeOS, Zephyr의 안전 인증 추진 등 — 기억 기반, 제품별 확인 필요) 또는 bare-metal로 safety MCU에 두고, Linux는 non-safety 쪽에 둔다"**다. 이른바 "mixed-criticality"를 칩/코어 경계로 분리하는 것이다.

### 1.4 부팅 시간 — MCU는 ms, Linux는 s

MCU는 리셋 후 수 ms 안에 `main()`에 들어가고 PWM을 안전값으로 잡는다. Linux는 부트로더 → 커널 → userspace까지 최적화해도 보통 1~수 초, systemd 풀 시스템은 더 걸린다(C7, L1에서 측정). 이 노트 랩에서도 BusyBox initramfs 게스트의 첫 셸까지 `lab initramfs: 6.18.55 up 8.91s`가 찍혔다(TCG + 부하 상황이라 실제 보드보다 훨씬 느리다). 의미는 이렇다.

- 전원이 들어오고 **Linux가 뜨기 전 수 초 동안 모터 드라이버 · 브레이크 · 히터 · 레이저의 상태를 누가 잡고 있나?** → MCU(또는 하드웨어 기본 상태: pull-down된 enable 핀).
- 차량 후방 카메라처럼 "전원 후 2초 안에 화면"이 법규인 기능은 Linux 부팅을 기다릴 수 없다 → 이종 SoC의 M 코어가 먼저 띄우는 early display 같은 패턴(기억 기반, 일부 NXP/TI 레퍼런스에서 제안).

### 1.5 전원 — always-on 도메인

배터리 장비는 대부분 시간 동안 Cortex-A를 끄고(suspend 또는 전원 차단) 작은 코어만 깨어 있다. 센서 임계값 감시, 버튼 · 무선 wake, 배터리 게이지는 **mW 이하 always-on 도메인의 MCU**가 하고, 필요할 때 Linux를 깨운다. Jetson의 SPE(always-on 클러스터의 Cortex-R5), i.MX8M의 M 코어, STM32MP의 M4/M33이 이 역할을 한다(기억 기반 — 데이터시트 확인 필요). 자세한 Linux 쪽 전원 관리는 N5.

### 1.6 장애 격리 — Linux가 죽어도 모터는

Linux는 커널 panic, OOM killer, 드라이버 deadlock, 파일시스템 read-only 전환, OTA 재부팅 등 **정상적인 이유로도** 멈춘다. 그 순간에도 모터 루프가 같은 커널/같은 메모리/같은 전원 위에 있으면 같이 멈춘다. 그래서 질문은 "Linux가 안 죽게 하자"가 아니라 **"Linux가 죽으면 무엇이, 몇 ms 안에, 어떤 상태로 가나"**다. §8에서 실제로 Linux 앱을 SIGSTOP/SIGKILL하고 커널 panic을 일으켜 MCU 쪽 반응 시간을 잰다.

### 1.7 복잡도와 생태계 — Linux를 쓰는 이유

거꾸로, MCU에 두면 안 되는 것도 분명하다.

- 카메라 파이프라인(V4L2, ISP, 인코더), GPU/NPU 추론, OpenCV, ROS 2.
- TCP/IP 스택 전체, TLS, Wi-Fi/LTE, 원격 관리, 컨테이너.
- 파일시스템, 대용량 로그, 데이터베이스.
- 보안 업데이트가 계속 나오는 소프트웨어 공급망(L4 OTA, L6 보안).

이걸 MCU에서 하려면 코드가 커지고, 커진 MCU 코드는 더 이상 "단순해서 검증 가능한" 장점을 잃는다. **MCU에 내리는 코드는 작고 단순하게 유지하는 것 자체가 설계 목표**다.

### 1.8 비용 · BOM · OTA · 개발 조직

- 외부 MCU 하나는 BOM 몇 달러 + 보드 면적 + 펌웨어 한 벌 + 업데이트 경로 하나다. 이종 SoC는 칩 하나로 해결하지만 툴체인 · 디버그 · 메모리 분할이 복잡하다.
- **펌웨어가 두 벌이면 버전 조합도 두 벌**이다. "Linux 2.3 + MCU 1.7"이 검증된 조합인지 누가 보장하나 → §9.
- 팀 구조도 영향을 준다. MCU 팀과 Linux 팀이 따로면 인터페이스 문서(ICD)가 계약서가 된다.

### 1.9 기준 요약 매트릭스

| 기준 | Linux (일반) | Linux + PREEMPT_RT | RT 코어 (이종 SoC 내 R5F/M7) | 외부 MCU | Safety MCU |
|---|---|---|---|---|---|
| 최악 지연 | 수 ms 이상, 보장 없음 | 수십 µs~ (튜닝·측정 시), 증명 어려움 | 수 µs, 결정론적 | 수 µs, 결정론적 | 결정론적 |
| 인증 | 사실상 불가 | 사실상 불가 | 가능 (칩/RTOS 따라) | 가능 | 목적이 인증 |
| 부팅 | 초 단위 | 초 단위 | Linux가 올릴 때까지 대기 (또는 early boot 지원 칩) | ms | ms |
| Linux 죽을 때 | 같이 죽음 | 같이 죽음 | 대체로 생존 (공유 전원·클럭·리셋 도메인 확인 필요) | 생존 | 생존 |
| 생태계 | 최고 | 최고 | 작음 | 작음 | 최소 |
| 비용 | 0 | 0 (튜닝 비용) | SoC 선택 제약 | 칩 + 보드 | 칩 + 인증 |
| OTA | L4 그대로 | L4 그대로 | 펌웨어가 rootfs 안 → 같이 업데이트 | 별도 경로 필요 | 별도 · 엄격 |

---

## 2. 판단 흐름도와 기능별 배치

### 2.1 질문 다섯 개로 정하기

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="370" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="205" y="41" text-anchor="middle" font-size="13">Q1. deadline이 100 µs 미만이거나</text>
  <text x="205" y="60" text-anchor="middle" font-size="13">놓치면 하드웨어가 망가지나?</text>
  <rect x="20" y="100" width="370" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="205" y="121" text-anchor="middle" font-size="13">Q2. 안전 기능인가?</text>
  <text x="205" y="140" text-anchor="middle" font-size="13">(SIL · ASIL · PL · DAL 대상)</text>
  <rect x="20" y="180" width="370" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="205" y="201" text-anchor="middle" font-size="13">Q3. Linux가 죽거나 부팅 중이어도</text>
  <text x="205" y="220" text-anchor="middle" font-size="13">계속 돌아야 하나? (always-on 포함)</text>
  <rect x="20" y="260" width="370" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="205" y="281" text-anchor="middle" font-size="13">Q4. deadline 100 µs ~ 수 ms이고,</text>
  <text x="205" y="300" text-anchor="middle" font-size="13">드물게 놓치면 성능만 떨어지나?</text>
  <rect x="20" y="340" width="370" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="205" y="361" text-anchor="middle" font-size="13">그 밖 (10 ms 이상, soft)</text>
  <text x="205" y="380" text-anchor="middle" font-size="13">카메라 · ML · 네트워크 · UI</text>
  <rect x="430" y="20" width="230" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="545" y="41" text-anchor="middle" font-size="13">MCU 주변장치 HW</text>
  <text x="545" y="60" text-anchor="middle" font-size="12">타이머 · PWM · 비교기 · ISR</text>
  <rect x="430" y="100" width="230" height="50" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="545" y="121" text-anchor="middle" font-size="13">Safety MCU</text>
  <text x="545" y="140" text-anchor="middle" font-size="12">lockstep · 인증 RTOS/bare-metal</text>
  <rect x="430" y="180" width="230" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="545" y="201" text-anchor="middle" font-size="13">별도 코어 / 칩</text>
  <text x="545" y="220" text-anchor="middle" font-size="12">외부 MCU 또는 SoC 내 M/R 코어</text>
  <rect x="430" y="260" width="230" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="545" y="281" text-anchor="middle" font-size="13">PREEMPT_RT 격리 코어</text>
  <text x="545" y="300" text-anchor="middle" font-size="12">또는 RT 코어 (N2 · N3로 검증)</text>
  <rect x="430" y="340" width="230" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="545" y="361" text-anchor="middle" font-size="13">Linux 일반 스레드</text>
  <text x="545" y="380" text-anchor="middle" font-size="12">프로세스 · 컨테이너</text>
  <line x1="390" y1="45" x2="428" y2="45" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="390" y1="125" x2="428" y2="125" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="390" y1="205" x2="428" y2="205" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="390" y1="285" x2="428" y2="285" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="390" y1="365" x2="428" y2="365" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="409" y="38" text-anchor="middle" font-size="12">예</text>
  <text x="409" y="118" text-anchor="middle" font-size="12">예</text>
  <text x="409" y="198" text-anchor="middle" font-size="12">예</text>
  <text x="409" y="278" text-anchor="middle" font-size="12">예</text>
  <line x1="205" y1="70" x2="205" y2="98" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="205" y1="150" x2="205" y2="178" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="205" y1="230" x2="205" y2="258" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="205" y1="310" x2="205" y2="338" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="215" y="89" font-size="12">아니오</text>
  <text x="215" y="169" font-size="12">아니오</text>
  <text x="215" y="249" font-size="12">아니오</text>
  <text x="215" y="329" font-size="12">아니오</text>
  <text x="20" y="418" font-size="12">위에서부터 처음 "예"가 나오는 곳이 그 기능의 자리다. 아래로 갈수록 생태계는 크고 보장은 약하다.</text>
</svg>
```

그림 2 — 기능 배치 판단 흐름. 질문 순서가 중요하다: 하드웨어 파손 → 안전 → 생존성 → 지연 → 나머지.

두 가지를 덧붙인다.

- **"PREEMPT_RT로 충분한가?"는 측정으로 답한다.** 목표 보드에서, 실제 부하(카메라 · 네트워크 · 스토리지 · GPU)를 걸고, 몇 시간~며칠 cyclictest/rtla를 돌려 최악값 분포를 본 뒤 margin(보통 deadline의 몇 분의 1)이 남는지 확인한다(N3). 측정하지 않은 PREEMPT_RT는 "아마 될 것"일 뿐이다.
- **흐름도는 기능 단위로 적용한다, 제품 단위가 아니다.** 한 로봇 안에 다섯 칸이 다 쓰인다.

### 2.2 기능별 배치 예 — 로봇 팔 (6축 협동로봇)

| 기능 | 주기/deadline | 놓치면 | 배치 | 근거 |
|---|---|---|---|---|
| 모터 전류 루프 (FOC) | 20 kHz / 50 µs | 과전류, 소음, 파손 | 관절마다 드라이버 MCU (예: STM32G4) | HW 타이머-ADC 동기, ISR 결정론 |
| 관절 위치/속도 루프 | 1~4 kHz | 진동, 추종 오차 | 관절 MCU 또는 중앙 RT 코어 | 엔코더와 가까울수록 지연 적음 |
| 궤적 보간 · 동역학 보상 | 1 kHz | 진동 | RT 코어 또는 PREEMPT_RT 격리 코어 | 부동소수 연산 많음, 버스(EtherCAT/CAN-FD)로 setpoint 배포 |
| 충돌 감지 · 속도/힘 제한 (안전) | 수 ms | 사람 부상 | Safety MCU (이중화) | ISO 10218 / ISO 13849 PL d 류 요구 (기억 기반) |
| E-stop · STO | 즉시 | 사람 부상 | **하드와이어 회로 + safety relay**, 소프트웨어는 보조 | 소프트웨어 경로 없이 전원 차단 |
| 경로 계획 · 역기구학 | 10~100 Hz | 느려짐 | Linux | 라이브러리 (MoveIt 등) |
| 비전 (물체 인식) | 10~30 Hz | 느려짐 | Linux + GPU/NPU | V4L2 · CUDA · ML |
| 티치 펜던트 UI · 웹 | 사람 속도 | 불편 | Linux | 생태계 |
| 로그 · OTA · 원격 진단 | 비실시간 | 없음 | Linux | L4 · L7 |

### 2.3 기능별 배치 예 — AMR (자율 이동 로봇), 짐벌 카메라, 드론

| 제품 | MCU / RTOS 쪽 | Linux 쪽 | 경계 인터페이스 |
|---|---|---|---|
| AMR (창고 로봇) | 바퀴 모터 FOC, 휠 오도메트리 적분(1 kHz), 범퍼 · 안전 스캐너 입력 → 정지, 배터리 BMS | LiDAR SLAM, 경로 계획, 장애물 회피(10~20 Hz), 함대 관리 통신 | CAN 또는 UART: 속도 명령(v, ω) 50~100 Hz ↓, 오도메트리 · 상태 ↑ |
| 짐벌 카메라 | IMU 읽기(1~8 kHz), 자세 추정, 브러시리스 모터 3축 안정화 루프 | 영상 인코딩 · 스트리밍, 피사체 추적(목표 각도 생성), 앱 연결 | UART/SPI: 목표 자세 30~100 Hz ↓, 실제 자세 + IMU 타임스탬프 ↑ |
| 멀티콥터 드론 | 비행 제어기(FMU): 자세 · 각속도 루프(수백 Hz~kHz), ESC 출력, failsafe(RTL/착륙) | 컴패니언 컴퓨터: VIO, 매핑, 객체 인식, 미션 로직, 영상 링크 | MAVLink(UART/Ethernet) 또는 DDS 브리지: 위치/속도 setpoint ↓, 자세 · 상태 ↑ |
| 차량용 카메라 박스 | 전원 시퀀스, CAN 웨이크업, 저전력 감시 | 영상 처리 · 녹화 · 연결 | 이종 SoC 내부 rpmsg 또는 외부 MCU UART |

드론 줄의 핵심: **컴패니언 컴퓨터(Linux)가 죽으면 FMU는 자기 failsafe 규칙(예: offboard 신호 끊김 → 호버 또는 귀환)으로 간다.** Linux는 "목표"만 주고, 비행 안정성은 FMU가 끝까지 책임진다. 이 구조가 이 노트 전체의 원형이다.

---

## 3. 다섯 가지 분할 패턴

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <text x="72" y="20" text-anchor="middle" font-size="13">(a) Linux 단독</text>
  <text x="72" y="36" text-anchor="middle" font-size="12">PREEMPT_RT</text>
  <rect x="12" y="50" width="120" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="72" y="74" text-anchor="middle" font-size="12">일반 앱</text>
  <rect x="12" y="96" width="120" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="72" y="114" text-anchor="middle" font-size="12">SCHED_FIFO</text>
  <text x="72" y="129" text-anchor="middle" font-size="12">격리 CPU</text>
  <rect x="12" y="142" width="120" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="72" y="166" text-anchor="middle" font-size="12">RT 커널</text>
  <rect x="12" y="188" width="120" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="72" y="210" text-anchor="middle" font-size="12">Cortex-A</text>
  <text x="206" y="20" text-anchor="middle" font-size="13">(b) 외부 MCU</text>
  <text x="206" y="36" text-anchor="middle" font-size="12">UART/SPI/CAN/USB</text>
  <rect x="146" y="50" width="120" height="70" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="206" y="80" text-anchor="middle" font-size="12">Linux SoC</text>
  <text x="206" y="98" text-anchor="middle" font-size="12">(칩 1)</text>
  <line x1="206" y1="120" x2="206" y2="152" stroke="currentColor" stroke-dasharray="4,3"/>
  <text x="214" y="141" font-size="12">링크</text>
  <rect x="146" y="152" width="120" height="70" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="206" y="182" text-anchor="middle" font-size="12">MCU + RTOS</text>
  <text x="206" y="200" text-anchor="middle" font-size="12">(칩 2)</text>
  <text x="340" y="20" text-anchor="middle" font-size="13">(c) 이종 SoC</text>
  <text x="340" y="36" text-anchor="middle" font-size="12">remoteproc + rpmsg</text>
  <rect x="280" y="46" width="120" height="180" fill="none" stroke="currentColor" stroke-dasharray="6,3"/>
  <rect x="290" y="56" width="100" height="60" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="82" text-anchor="middle" font-size="12">Cortex-A</text>
  <text x="340" y="100" text-anchor="middle" font-size="12">Linux</text>
  <text x="340" y="136" text-anchor="middle" font-size="12">공유 메모리</text>
  <text x="340" y="151" text-anchor="middle" font-size="12">+ mailbox</text>
  <rect x="290" y="158" width="100" height="60" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="184" text-anchor="middle" font-size="12">Cortex-M/R</text>
  <text x="340" y="202" text-anchor="middle" font-size="12">RTOS</text>
  <text x="474" y="20" text-anchor="middle" font-size="13">(d) Hypervisor</text>
  <text x="474" y="36" text-anchor="middle" font-size="12">정적 분할</text>
  <rect x="414" y="50" width="58" height="86" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="443" y="90" text-anchor="middle" font-size="12">Linux</text>
  <text x="443" y="106" text-anchor="middle" font-size="12">VM</text>
  <rect x="476" y="50" width="58" height="86" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="505" y="90" text-anchor="middle" font-size="12">RTOS</text>
  <text x="505" y="106" text-anchor="middle" font-size="12">VM</text>
  <rect x="414" y="142" width="120" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="474" y="160" text-anchor="middle" font-size="12">Jailhouse / Xen</text>
  <text x="474" y="175" text-anchor="middle" font-size="12">/ Bao</text>
  <rect x="414" y="188" width="120" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="474" y="210" text-anchor="middle" font-size="12">Cortex-A 코어들</text>
  <text x="608" y="20" text-anchor="middle" font-size="13">(e) Dual-kernel</text>
  <text x="608" y="36" text-anchor="middle" font-size="12">Xenomai</text>
  <rect x="548" y="50" width="58" height="86" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="577" y="90" text-anchor="middle" font-size="12">Linux</text>
  <text x="577" y="106" text-anchor="middle" font-size="12">앱</text>
  <rect x="610" y="50" width="58" height="86" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="639" y="90" text-anchor="middle" font-size="12">RT</text>
  <text x="639" y="106" text-anchor="middle" font-size="12">앱</text>
  <rect x="548" y="142" width="120" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="608" y="160" text-anchor="middle" font-size="12">Linux 커널 +</text>
  <text x="608" y="175" text-anchor="middle" font-size="12">Cobalt 코어</text>
  <rect x="548" y="188" width="120" height="34" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="608" y="210" text-anchor="middle" font-size="12">IRQ 먼저 가로챔</text>
  <text x="12" y="258" font-size="13">격리 수준 (Linux 장애가 RT 쪽에 번지지 않는 정도)</text>
  <line x1="12" y1="272" x2="668" y2="272" stroke="currentColor"/>
  <text x="72" y="292" text-anchor="middle" font-size="12">없음</text>
  <text x="206" y="292" text-anchor="middle" font-size="12">최고 (칩 분리)</text>
  <text x="340" y="292" text-anchor="middle" font-size="12">높음 (공유 자원 주의)</text>
  <text x="474" y="292" text-anchor="middle" font-size="12">중간 (캐시 · 버스)</text>
  <text x="608" y="292" text-anchor="middle" font-size="12">낮음 (같은 커널)</text>
  <text x="12" y="326" font-size="13">대표 예</text>
  <text x="72" y="346" text-anchor="middle" font-size="12">EtherCAT 마스터</text>
  <text x="206" y="346" text-anchor="middle" font-size="12">Jetson + STM32</text>
  <text x="340" y="346" text-anchor="middle" font-size="12">i.MX8M, AM64x</text>
  <text x="474" y="346" text-anchor="middle" font-size="12">산업 PC, 차량</text>
  <text x="608" y="346" text-anchor="middle" font-size="12">기존 CNC/로봇</text>
  <text x="206" y="364" text-anchor="middle" font-size="12">PX4 FMU + 컴패니언</text>
  <text x="340" y="364" text-anchor="middle" font-size="12">STM32MP1/2, Zynq</text>
</svg>
```

그림 3 — 다섯 가지 분할 패턴. 왼쪽에서 오른쪽이 "복잡도 순서"는 아니다 — 격리 수준과 통합 비용이 서로 다른 축으로 움직인다.

### 3.1 (a) Linux 단독 + PREEMPT_RT

PREEMPT_RT는 6.12부터 mainline에 들어왔다(N1). `CONFIG_PREEMPT_RT=y` 커널에서 RT 스레드를 `SCHED_FIFO`로 올리고, `isolcpus` · `nohz_full` · IRQ affinity로 CPU 하나를 비우고, `mlockall`로 page fault를 막는다(N2).

- **맞는 경우**: 1 kHz 수준 루프, 실패가 안전 문제가 아닌 경우, 제어기가 이미 표준 버스(EtherCAT, CAN-FD)로 지능형 드라이브에 setpoint만 보내는 경우. EtherCAT 마스터(IgH EtherLab, SOEM 등)를 PREEMPT_RT Linux에서 돌리는 산업용 구성이 대표적이다.
- **안 맞는 경우**: 수십 µs 루프, 안전 기능, 부팅 중에도 출력이 정의돼야 하는 경우.
- **함정**: "커널 지연"만 RT가 되고 드라이버(예: USB-시리얼, Wi-Fi, GPU 드라이버의 긴 IRQ off 구간)는 그대로다. RT 스레드가 쓰는 드라이버 경로 전체를 측정해야 한다.

### 3.2 (b) Linux SoC + 외부 MCU

가장 흔하고 가장 튼튼한 패턴이다. 칩이 다르니 **전원 · 클럭 · 리셋 · 메모리가 다 분리**된다. Linux가 panic해도 MCU의 PWM 타이머는 계속 돈다 — 그래서 MCU가 heartbeat로 Linux를 감시해야 한다(§8).

| 링크 | 대역폭 (대략) | 지연/지터 특징 | 장점 | 주의 |
|---|---|---|---|---|
| UART | 115200 ~ 수 Mbaud | 바이트 단위, Linux 쪽 tty 계층 + FIFO 임계값 + 드라이버 지연 | 단순, 디버그 쉬움, 어디든 있음 | 프레이밍 · CRC 직접, tty `low_latency`, DMA 설정 |
| SPI | 수~수십 Mbit/s | Linux가 master면 전송 시작 시점이 Linux 스케줄 의존 | 빠름, 전이중 | MCU가 slave면 "데이터 준비" IRQ 핀 필요, `spidev` 지연 |
| CAN / CAN-FD | 1 / 5~8 Mbit/s | 우선순위 중재로 결정론적, SocketCAN | 다중 노드, 견고, 자동차 · 로봇 표준 | 페이로드 8/64 B, 버스 부하 설계 (Q 모듈) |
| USB (CDC/HID/bulk) | 12 / 480 Mbit/s | 1 ms(FS) / 125 µs(HS) 폴링 프레임 | 대역폭 큼, 전원 공급 | 호스트 스택 지연, 재열거(re-enumeration) 시 끊김 |
| Ethernet (UDP, PTP) | 100M ~ 1G | 네트워크 스택 경유, PTP로 µs 이하 시간 동기 가능 | 대역폭, 시간 동기, 표준 도구 | MCU 쪽 스택(lwIP 등) 부담 |

### 3.3 (c) 이종 SoC — Cortex-A + Cortex-M/R on-die

한 칩 안에 Linux용 Cortex-A 클러스터와 실시간용 Cortex-M/R 코어가 같이 있다. Linux가 **remoteproc**으로 M/R 코어의 펌웨어를 올리고 시작/정지하며, **rpmsg**(공유 메모리의 virtio ring + mailbox 인터럽트)로 메시지를 주고받는다. 깊은 내용은 O 모듈에서 다룸, 이 노트 §10에서는 경로만 본다.

| SoC (기억 기반 — 데이터시트로 확인할 것) | 응용 코어 | 실시간 코어 | 비고 |
|---|---|---|---|
| NXP i.MX8M Mini / Plus | Cortex-A53 x4 | Cortex-M4 / Cortex-M7 | Linux `imx_rproc`, MU(Messaging Unit) mailbox |
| ST STM32MP1 / STM32MP2 | Cortex-A7 x1~2 / Cortex-A35 x1~2 | Cortex-M4 / Cortex-M33 | `stm32_rproc`, IPCC mailbox, CubeIDE 펌웨어 |
| TI AM62x / AM64x | Cortex-A53 x1~4 / x2 | Cortex-M4F / R5F x4 + M4F | `ti_k3_m4_remoteproc`, `ti_k3_r5_remoteproc` |
| AMD(Xilinx) Zynq UltraScale+ MPSoC (Kria K26) | Cortex-A53 x4 | Cortex-R5F x2 (lockstep 가능) | `xlnx_r5_remoteproc`, FPGA 로직까지 |
| NVIDIA Jetson (Xavier/Orin) | Carmel / Cortex-A78AE | SPE(Cortex-R5, always-on), 안전 관련 클러스터 | SPE는 NVIDIA 제공 펌웨어 커스터마이즈, 문서 확인 필요 |

랩 커널(`/var/lab/out/Image`, arm64 defconfig 기반)에도 이 드라이버들이 이미 켜져 있다. 실제로 확인해 보자.

```sh
grep -E "^CONFIG_(REMOTEPROC|RPMSG|RPMSG_CHAR|RPMSG_CTRL|RPMSG_VIRTIO|IMX_REMOTEPROC|TI_K3_M4_REMOTEPROC|TI_K3_R5_REMOTEPROC|MAILBOX)=" /var/lab/out/linux-build/.config
ls /var/lab/src/linux-6.18.55/drivers/remoteproc/ | grep -E "imx|stm32|k3|xlnx"
```

```text
CONFIG_MAILBOX=y
CONFIG_REMOTEPROC=y
CONFIG_IMX_REMOTEPROC=y
CONFIG_TI_K3_M4_REMOTEPROC=m
CONFIG_TI_K3_R5_REMOTEPROC=m
CONFIG_RPMSG=y
CONFIG_RPMSG_CHAR=m
CONFIG_RPMSG_CTRL=m
CONFIG_RPMSG_VIRTIO=y
imx_dsp_rproc.c
imx_rproc.c
imx_rproc.h
stm32_rproc.c
ti_k3_common.c
ti_k3_common.h
ti_k3_dsp_remoteproc.c
ti_k3_m4_remoteproc.c
ti_k3_r5_remoteproc.c
xlnx_r5_remoteproc.c
```

출력에서 볼 것: 이종 SoC 지원은 "특수한 BSP 패치"가 아니라 mainline 6.18에 벤더별 remoteproc 드라이버로 들어와 있다. QEMU `virt`에는 원격 코어가 없어서 이 드라이버들이 probe되지는 않는다(실제 보드 실습은 O 모듈).

**외부 MCU 대비 장단점**

- 장점: 칩 하나(BOM · 면적), 공유 메모리로 대역폭 큼, 펌웨어가 Linux rootfs의 `/lib/firmware`에 있어 **OTA가 Linux와 한 번에** 된다(§9).
- 단점: **완전한 격리가 아니다.** 전원 레일, 클럭, 리셋, DDR, 인터커넥트를 공유할 수 있다. Linux가 M 코어의 메모리를 실수로 덮어쓰거나, Linux 드라이버가 공유 클럭을 꺼 버리거나(clock gating), SoC 리셋이 같이 걸리면 같이 죽는다. M 코어의 코드를 **TCM/OCRAM에 두고**, 공유 자원은 리소스 도메인 컨트롤러(예: NXP RDC, ST RIF/ETZPC 류 — 기억 기반)로 막고, 리셋 도메인을 확인해야 "Linux가 죽어도 산다"가 성립한다.
- 기본 동작 주의: 많은 보드에서 M 코어는 **Linux가 부팅한 뒤** remoteproc으로 시작된다. 그러면 "부팅 중 출력 정의"는 못 한다. 부트로더(U-Boot `bootaux` 같은 명령 — 기억 기반)에서 먼저 띄우고 Linux는 **attach**만 하는 방식(remoteproc의 attach/detach 지원)을 써야 한다.

### 3.4 (d) Hypervisor 분할

같은 Cortex-A 코어들을 정적으로 나눠서 일부 코어에 Linux, 나머지에 RTOS(또는 bare-metal, Zephyr)를 돌린다.

- **Jailhouse**(Siemens 시작, 기억 기반): Linux가 먼저 부팅한 뒤 hypervisor를 올리고 CPU · 메모리 · 장치를 "cell"로 떼어 준다. 스케줄링하지 않는 정적 분할.
- **Xen**: 범용 hypervisor, Arm에서 dom0less(부트 때 여러 VM 직접 시작)와 cache coloring 같은 RT 기능이 있다(기억 기반).
- **Bao**: 정적 분할 전용 경량 hypervisor(학계/오픈소스, 기억 기반).
- 문제는 **공유 하드웨어 간섭**: L2/L3 캐시, DDR 대역폭, 인터커넥트를 공유하므로 Linux 쪽 메모리 폭주가 RTOS 쪽 지연을 늘린다. cache coloring, 메모리 대역폭 제한(MPAM 등) 같은 대책이 필요하다. 인증 사례는 상용 hypervisor(PikeOS 등, 기억 기반) 쪽이 많다.

### 3.5 (e) Dual-kernel — Xenomai

Linux 아래에 작은 실시간 코어(Xenomai의 Cobalt)를 두고, 인터럽트를 먼저 가로채서 RT 태스크를 Linux보다 먼저 실행한다. Linux 전체가 RT 코어 입장에서는 "가장 낮은 우선순위 태스크"다. 커널 패치(I-pipe, 이후 Dovetail — 기억 기반)가 필요하고 커널 버전을 따라가는 비용이 크다. PREEMPT_RT가 mainline에 들어온 뒤로 신규 설계에서는 덜 고르지만, 기존 CNC/로봇 제어기에는 여전히 많다. **같은 커널 주소 공간**이라 Linux 커널 panic이면 같이 멈춘다는 점에서 격리는 약하다.

### 3.6 패턴 비교표

| 항목 | (a) PREEMPT_RT | (b) 외부 MCU | (c) 이종 SoC | (d) Hypervisor | (e) Xenomai |
|---|---|---|---|---|---|
| 최악 지연 | 수십 µs~ (보드 의존) | µs 이하~수 µs | µs 이하~수 µs | 수 µs~ (간섭 의존) | 수 µs~수십 µs |
| Linux panic 시 | 같이 정지 | 생존 | 대체로 생존 (공유 자원 확인) | 생존 (hypervisor가 살아 있으면) | 대체로 정지 |
| 부팅 중 출력 | 불가 | 가능 | 조건부 (early boot) | 조건부 | 불가 |
| 인증 경로 | 매우 어려움 | 좋음 | 칩 따라 가능 | 상용 HV로 가능 | 어려움 |
| 통신 | 같은 프로세스/공유 메모리 | 버스 (프레이밍 필요) | rpmsg | 가상 장치/공유 메모리 | 같은 커널 |
| 개발 난이도 | 낮음 | 중간 (2 펌웨어) | 높음 (2 툴체인 · 메모리 맵) | 높음 | 중간~높음 |
| OTA | 하나 | 두 경로 | 하나 (rootfs 안) | 이미지 여러 개 | 하나 |

---

## 4. 실제 제품 구조 — 무엇을 어디에 뒀나

(이 절의 제품 구조는 공개 자료와 일반적인 지식에 바탕한 **기억 기반** 설명이다. 세부 사양은 각 제품 문서로 확인할 것.)

- **PX4 드론 = FMU + 컴패니언 컴퓨터**: Pixhawk 계열 FMU(STM32F7/H7 + NuttX RTOS)가 센서 읽기 · 상태 추정(EKF) · 자세/속도 제어 · 모터 출력 · failsafe를 한다. Jetson이나 Raspberry Pi 같은 Linux 컴패니언이 MAVLink(UART/Ethernet) 또는 ROS 2 브리지(uXRCE-DDS)로 "offboard" setpoint를 보낸다. 컴패니언 신호가 끊기면 FMU가 설정된 failsafe(홀드/귀환/착륙)로 간다. 이 노트의 heartbeat 실험이 바로 이 구조의 축소판이다.
- **Jetson + STM32/ESP32 로봇 보드**: 많은 연구용 · 스타트업 로봇이 Jetson(인지 · ML)과 STM32(모터 · IMU · 전원)를 UART/USB/CAN으로 묶는다. ESP32는 무선 · 저전력 감시 쪽에 자주 쓰인다.
- **자동차 zonal 아키텍처**: 기능별 ECU 수십 개 대신, 차량 영역(zone)마다 zone controller(실시간 MCU, AUTOSAR Classic)가 센서 · 액추에이터를 모으고, 중앙 고성능 컴퓨터(Linux/QNX/AUTOSAR Adaptive, hypervisor 분할 흔함)가 상위 기능을 한다. Tesla가 이런 방향의 대표 사례로 자주 언급된다. 브레이크 · 조향 같은 ASIL D 기능은 zone/전용 ECU의 lockstep MCU에 남는다.
- **산업 PLC + HMI**: PLC 런타임(IEC 61131-3)은 RTOS 또는 PREEMPT_RT/hypervisor 분할 코어에서 1~10 ms 사이클로 돌고, HMI · 웹 · OPC UA · 클라우드 연결은 Linux가 한다. 안전 PLC는 별도 인증 하드웨어다.
- **협동로봇**: 관절마다 서보 드라이브(MCU, 전류/속도 루프), 중앙 제어기(PREEMPT_RT Linux 또는 RTOS가 EtherCAT 마스터로 1 kHz 이상 setpoint 배포), 안전 컨트롤러(이중화 MCU, 속도 · 힘 감시), 티치 펜던트(Linux/Android UI).

공통점: **안전 · 고속 루프는 끝까지 MCU, Linux는 "목표"를 준다, 경계에는 heartbeat와 failsafe가 있다.**

---

## 5. 경계 인터페이스 설계 — 프레임, CRC, seq, 타임스탬프

경계를 넘는 링크를 설계할 때 정해야 하는 것들이다. Don이 SSD에서 호스트-디바이스 프로토콜(NVMe 큐 · doorbell · completion · phase bit)을 다뤄 본 경험이 그대로 쓰인다.

| 설계 항목 | 결정할 것 | 흔한 선택 |
|---|---|---|
| 프레이밍 | 바이트 스트림에서 메시지 경계 찾기 | SOF + 길이 + CRC, 또는 COBS/SLIP(구분자 0x00/0xC0) |
| 무결성 | 비트 오류 검출 | CRC16-CCITT (짧은 프레임), CRC32 (긴 프레임) |
| 순서 · 손실 | 빠진 메시지, 중복, 재전송 | 16-bit seq, 손실 카운터, 명령은 멱등(idempotent)하게 |
| 신선도 | 오래된 명령 실행 방지 | 송신 시각 타임스탬프 + 유효 기간(TTL), 늦은 명령은 버림 |
| 시간 | 서로 다른 시계 | 오프셋/드리프트 추정(§7) 또는 HW 동기 |
| 생존 감시 | 상대가 살아 있나 | 양방향 heartbeat + deadline (§8) |
| 버전 | 프로토콜 · 펌웨어 호환 | 시작 시 HELLO(fw ver, proto ver, build id), 불일치면 ARM 거부 (§9) |
| 흐름 제어 | 수신 버퍼 넘침 | 수신 측 크레딧, 또는 최신값만 유지(덮어쓰기) |

**명령 설계 원칙 하나 더**: 제어 명령은 "1도 더 돌려라(증분)"가 아니라 "이 각도로 가라(절대값) + 유효 시각"으로 보낸다. 프레임 하나가 빠지거나 두 번 와도 결과가 같게(멱등) 만들면 재전송 로직이 단순해진다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="13">프레임 (n4_proto.h)</text>
  <rect x="20" y="32" width="60" height="40" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="50" y="50" text-anchor="middle" font-size="12">SOF</text>
  <text x="50" y="65" text-anchor="middle" font-size="12">0xA5</text>
  <rect x="80" y="32" width="60" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="50" text-anchor="middle" font-size="12">type</text>
  <text x="110" y="65" text-anchor="middle" font-size="12">1 B</text>
  <rect x="140" y="32" width="60" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="50" text-anchor="middle" font-size="12">len</text>
  <text x="170" y="65" text-anchor="middle" font-size="12">1 B</text>
  <rect x="200" y="32" width="80" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="240" y="50" text-anchor="middle" font-size="12">seq</text>
  <text x="240" y="65" text-anchor="middle" font-size="12">2 B LE</text>
  <rect x="280" y="32" width="200" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="380" y="50" text-anchor="middle" font-size="12">payload</text>
  <text x="380" y="65" text-anchor="middle" font-size="12">0 ~ 32 B</text>
  <rect x="480" y="32" width="90" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="525" y="50" text-anchor="middle" font-size="12">CRC16</text>
  <text x="525" y="65" text-anchor="middle" font-size="12">2 B LE</text>
  <line x1="80" y1="84" x2="480" y2="84" stroke="#e08a3c"/>
  <text x="280" y="100" text-anchor="middle" font-size="12">CRC 계산 범위 (type ~ payload)</text>
  <text x="20" y="138" font-size="13">MCU 쪽 heartbeat 상태 기계</text>
  <rect x="20" y="160" width="130" height="50" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="182" text-anchor="middle" font-size="13">BOOT/SAFE</text>
  <text x="85" y="200" text-anchor="middle" font-size="12">PWM off, 브레이크</text>
  <rect x="290" y="160" width="130" height="50" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="355" y="182" text-anchor="middle" font-size="13">RUN</text>
  <text x="355" y="200" text-anchor="middle" font-size="12">명령 수행</text>
  <rect x="520" y="160" width="140" height="50" rx="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="590" y="182" text-anchor="middle" font-size="13">SAFE (latched)</text>
  <text x="590" y="200" text-anchor="middle" font-size="12">램프다운 → 정지</text>
  <line x1="150" y1="175" x2="288" y2="175" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="219" y="168" text-anchor="middle" font-size="12">연속 정상 HB N개</text>
  <line x1="420" y1="175" x2="518" y2="175" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="469" y="168" text-anchor="middle" font-size="12">HB 끊김</text>
  <text x="469" y="196" text-anchor="middle" font-size="12">> deadline</text>
  <path d="M590,210 C590,270 85,270 85,212" fill="none" stroke="currentColor" marker-end="url(#ah4)"/>
  <text x="337" y="290" text-anchor="middle" font-size="12">원인 기록 + 명시적 re-arm 명령 (자동 복귀 금지가 원칙)</text>
</svg>
```

그림 4 — 위: 이 노트에서 쓰는 프레임. 아래: MCU 쪽 failsafe 상태 기계. 이 노트 실험 코드는 단순화를 위해 "정상 HB 20개면 RUN 복귀"로 자동 복귀시켰다 — §8에서 그게 왜 위험한지 측정으로 본다.

### 예제 1 — 양쪽이 공유하는 프레이밍 코드 (C, 51줄)

무엇을 확인하나: MCU에도 그대로 들어갈 수 있게 malloc 없이, 바이트 하나씩 먹는 상태 기계로 프레임을 인코딩/디코딩한다. 같은 헤더를 Linux 쪽과 "MCU" 쪽이 둘 다 include한다.

```c
/* n4_proto.h — framing shared by the "Linux" side and the "MCU" side.
 * Frame: 0xA5 | type | len | seq_lo | seq_hi | payload[len] | crc_lo | crc_hi
 * CRC16-CCITT (poly 0x1021, init 0xFFFF) over type..payload. No malloc, byte-wise RX. */
#include <stdint.h>
#include <string.h>
#define N4_SOF  0xA5
#define N4_MAXP 32
enum { N4_PING = 1, N4_PONG = 2, N4_HB = 3, N4_STATE = 4 };
typedef struct { uint8_t type, len; uint16_t seq; uint8_t p[N4_MAXP]; } n4_msg;
typedef struct { int st, n; uint8_t buf[N4_MAXP + 6]; } n4_rx;

static uint16_t n4_crc16(const uint8_t *d, int n)
{
    uint16_t c = 0xFFFF;
    while (n--) {
        c ^= (uint16_t)(*d++ << 8);
        for (int i = 0; i < 8; i++)
            c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
    }
    return c;
}

static int n4_encode(const n4_msg *m, uint8_t *out)      /* returns frame length */
{
    out[0] = N4_SOF; out[1] = m->type; out[2] = m->len;
    out[3] = (uint8_t)(m->seq & 0xFF); out[4] = (uint8_t)(m->seq >> 8);
    memcpy(out + 5, m->p, m->len);
    uint16_t c = n4_crc16(out + 1, 4 + m->len);
    out[5 + m->len] = (uint8_t)(c & 0xFF); out[6 + m->len] = (uint8_t)(c >> 8);
    return 7 + m->len;
}

/* Feed one received byte. 1 = valid frame in *m, -1 = bad len/CRC (resync), 0 = need more. */
static int n4_feed(n4_rx *r, uint8_t b, n4_msg *m)
{
    if (r->st == 0) {                       /* hunting for start-of-frame */
        if (b == N4_SOF) { r->st = 1; r->n = 0; }
        return 0;
    }
    r->buf[r->n++] = b;                     /* buf: type len seqL seqH payload.. crcL crcH */
    if (r->n == 2 && r->buf[1] > N4_MAXP) { r->st = 0; return -1; }
    if (r->n < 2 || r->n < 6 + r->buf[1]) return 0;
    r->st = 0;
    int L = r->buf[1];
    uint16_t c = n4_crc16(r->buf, 4 + L);
    if ((uint16_t)(r->buf[4 + L] | r->buf[5 + L] << 8) != c) return -1;
    m->type = r->buf[0]; m->len = (uint8_t)L;
    m->seq = (uint16_t)(r->buf[2] | r->buf[3] << 8);
    memcpy(m->p, r->buf + 4, (size_t)L);
    return 1;
}
```

설계 메모: 이 디코더는 일부러 단순하다. 실제 제품이라면 (1) CRC 오류 시 버퍼 안에서 **다음 0xA5부터 다시 스캔**(지금은 그 바이트들을 버린다), (2) 수신 타임아웃으로 반쯤 받은 프레임 폐기, (3) 페이로드에 SOF 값이 나와도 문제없게 길이 기반으로 처리(지금 방식) 또는 COBS로 0x00을 아예 없애는 방식을 고른다.

### 예제 2 — CRC가 정말 잡나: 비트 오류 주입 시험

무엇을 확인하나: 프레임 100만 개에 무작위 비트 반전을 0/1/2/3/8개 넣고, 디코더가 "정상 수락 / 거부 / **틀린 걸 수락(미검출)**" 중 무엇을 하는지 센다. 그리고 프레임 사이에 잡음 바이트를 끼워도 재동기화되는지 본다.

```c
/* Bit-error injection test for n4_proto.h: does CRC16 catch corrupted frames? */
#include <stdio.h>
#include <stdlib.h>
#include "n4_proto.h"

static int run(int flips, int trials, int *ok, int *bad, int *wrong)
{
    *ok = *bad = *wrong = 0;
    for (int t = 0; t < trials; t++) {
        n4_msg m = { .type = N4_HB, .len = 12, .seq = (uint16_t)t }, out;
        for (int i = 0; i < m.len; i++) m.p[i] = (uint8_t)rand();
        uint8_t f[64];
        int n = n4_encode(&m, f);
        for (int k = 0; k < flips; k++) {           /* flip random bits after SOF */
            int bit = 8 + rand() % ((n - 1) * 8);
            f[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        }
        n4_rx rx = { 0 };
        int r = 0;
        for (int i = 0; i < n && r == 0; i++) r = n4_feed(&rx, f[i], &out);
        if (r == 1 && memcmp(&out, &m, 5 + m.len) == 0) (*ok)++;
        else if (r == 1) (*wrong)++;                /* accepted but corrupted = undetected */
        else (*bad)++;                              /* rejected or never completed */
    }
    return 0;
}

int main(void)
{
    srand(42);
    int ok, bad, wrong;
    int flips[] = { 0, 1, 2, 3, 8 };
    printf("%-6s %8s %8s %8s %10s\n", "flips", "ok", "reject", "UNDETECT", "trials");
    for (unsigned i = 0; i < sizeof flips / sizeof flips[0]; i++) {
        run(flips[i], 1000000, &ok, &bad, &wrong);
        printf("%-6d %8d %8d %8d %10d\n", flips[i], ok, bad, wrong, 1000000);
    }
    /* stream with garbage between frames: decoder must resync on next SOF */
    uint8_t s[4096]; int n = 0, got = 0;
    for (int k = 0; k < 100; k++) {
        n4_msg m = { .type = N4_PING, .len = 8, .seq = (uint16_t)k };
        n += n4_encode(&m, s + n);
        for (int g = 0; g < 3; g++) s[n++] = (uint8_t)(k & 1 ? 0x00 : 0x11); /* line noise */
    }
    n4_rx rx = { 0 }; n4_msg o;
    for (int i = 0; i < n; i++) got += n4_feed(&rx, s[i], &o) == 1;
    printf("stream: 100 frames + noise -> decoded %d\n", got);
    return 0;
}
```

```sh
limactl shell elinux -- bash -c 'cd /var/tmp/N4 && gcc -std=gnu11 -Wall -Wextra -O2 \
  /Users/donh/.elinux_scratch/N4/n4_ptest.c -I/Users/donh/.elinux_scratch/N4 -o n4_ptest && ./n4_ptest'
```

```text
flips        ok   reject UNDETECT     trials
0       1000000        0        0    1000000
1             0  1000000        0    1000000
2          6993   993006        1    1000000
3             0  1000000        0    1000000
8             0   999970       30    1000000
stream: 100 frames + noise -> decoded 100
```

출력에서 볼 것:

- 1비트, 3비트 오류는 100만 번 모두 거부됐다. CRC16-CCITT는 짧은 메시지에서 홀수 개 비트 오류와 2비트 오류를 다 잡는 성질이 있다.
- 2비트에서 `ok` 6993개는 같은 비트를 두 번 뒤집어 원상복구된 경우다(실제 오류 아님). 그런데 **미검출 1건**이 나왔다. CRC의 보장은 "CRC 범위가 고정된 프레임"에 대한 것인데, 이 프레임은 **len 바이트가 CRC 범위 계산에 쓰인다**. len이 깨지면 디코더는 다른 바이트 범위로 CRC를 계산하고, 우연히 맞을 수 있다. 실무 대책: 헤더 전용 CRC(헤더 CRC8 + 페이로드 CRC16), 고정 길이 메시지 타입, 또는 type별 허용 길이 검사.
- 8비트 무작위 오류에서 미검출 30/100만 ≈ 3×10⁻⁵. 16비트 CRC의 이론적 우연 일치 확률 2⁻¹⁶ ≈ 1.5×10⁻⁵와 같은 자릿수다. **"CRC16이 있으니 안전"이 아니라 "링크 비트 오류율 × 이 확률 × 초당 프레임 수"로 시간당 미검출 오류를 계산**해야 한다. 안전 관련 메시지는 그래서 CRC32 + seq + 타임아웃 + 이중 채널 같은 "black channel" 원칙(통신 채널을 믿지 않고 끝단에서 검증 — IEC 61784-3 계열 개념, 기억 기반)을 쓴다.

---

## 6. 링크 지연 측정 — 왜 루프를 Linux 유저스페이스로 닫지 않나

### 6.1 실험 구성

"Linux 앱이 MCU에게 묻고, 답을 받고, 다시 명령한다" — 이게 루프를 Linux를 거쳐 닫는 구조다. 이 왕복의 지연 분포를 재 보자. PING에 Linux 시각 `t1`을 넣어 보내면, MCU가 받은 시각 `t2`, 보내는 시각 `t3`(MCU 자기 시계)를 붙여 PONG으로 돌려준다. Linux가 받은 시각이 `t4`다.

- RTT(왕복 지연, MCU 처리 시간 제외) = `(t4 - t1) - (t3 - t2)`
- offset(두 시계 차 추정) = `((t2 - t1) + (t3 - t4)) / 2` — NTP와 같은 4-타임스탬프 공식, §7에서 다시 쓴다.

"MCU" 프로그램은 독립된 수정 발진기를 흉내 내기 위해 자기 시계를 `호스트 CLOCK_MONOTONIC + 7.3 s + 50 ppm 드리프트`로 둔다. 그리고 heartbeat 감시자(§8)를 겸한다.

### 예제 3 — 공용 도우미와 "Linux 쪽" 프로그램

무엇을 확인하나: tty를 raw 모드로 열고(`cfmakeraw` — 줄 단위 처리 · 에코 · 특수문자 해석을 끈다, A3), PING/PONG RTT를 CSV로 찍거나, 주기적 heartbeat를 보내거나, 실험 마커를 보낸다. 주기는 `clock_nanosleep(TIMER_ABSTIME)`로 절대 시각 기준이다(드리프트 누적 방지 — 그런데 §8에서 이게 함정이 된다).

```c
/* helpers shared by n4_linux.c and n4_mcu.c (n4_link.h) */
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
static uint64_t now_ns(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
static int open_raw(const char *path)
{
    int fd = open(path, O_RDWR | O_NOCTTY);
    if (fd < 0) { perror(path); exit(1); }
    struct termios tio;
    if (tcgetattr(fd, &tio) == 0) { cfmakeraw(&tio); cfsetspeed(&tio, B115200); tcsetattr(fd, TCSANOW, &tio); }
    return fd;
}
static void send_msg(int fd, n4_msg *m)
{
    uint8_t f[N4_MAXP + 8];
    int n = n4_encode(m, f);
    if (write(fd, f, (size_t)n) != n) { perror("write"); exit(1); }
}
```

```c
/* "Linux side": rtt <dev> <count> <period_us>  -> PING/PONG RTT + offset estimate (CSV on stdout)
 *               hb  <dev> <period_ms>          -> heartbeat frames forever
 *               mark <dev> <code>              -> one STATE frame (experiment marker) */
#include <poll.h>
#include <string.h>
#include "n4_proto.h"
#include "n4_link.h"

static int wait_pong(int fd, n4_rx *rx, n4_msg *m, uint16_t seq)
{
    struct pollfd p = { .fd = fd, .events = POLLIN };
    uint8_t b[64];
    while (poll(&p, 1, 200) > 0) {
        ssize_t n = read(fd, b, sizeof b);
        for (ssize_t i = 0; i < n; i++)
            if (n4_feed(rx, b[i], m) == 1 && m->type == N4_PONG && m->seq == seq) return 0;
    }
    return -1;                                       /* 200 ms timeout = lost */
}

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s rtt|hb dev arg [period_us]\n", argv[0]); return 1; }
    int fd = open_raw(argv[2]);
    n4_rx rx = { 0 }; n4_msg m;
    struct timespec next; clock_gettime(CLOCK_MONOTONIC, &next);
    if (!strcmp(argv[1], "hb")) {
        long per = atol(argv[3]) * 1000000L;
        for (uint16_t s = 0;; s++) {
            m = (n4_msg){ .type = N4_HB, .len = 0, .seq = s };
            send_msg(fd, &m);
            next.tv_nsec += per;
            while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
            clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
        }
    }
    if (!strcmp(argv[1], "mark")) {                 /* event marker, timestamped by the MCU */
        m = (n4_msg){ .type = N4_STATE, .len = 1, .seq = 0 }; m.p[0] = (uint8_t)atoi(argv[3]);
        send_msg(fd, &m); return 0;
    }
    int cnt = atoi(argv[3]); long per = argc > 4 ? atol(argv[4]) * 1000L : 1000000L;
    printf("seq,t1_ns,rtt_ns,offset_ns\n");
    for (int s = 0; s < cnt; s++) {
        uint64_t t1 = now_ns(), t2, t3;
        m = (n4_msg){ .type = N4_PING, .len = 8, .seq = (uint16_t)s };
        memcpy(m.p, &t1, 8);
        send_msg(fd, &m);
        if (wait_pong(fd, &rx, &m, (uint16_t)s)) { printf("%d,%llu,-1,0\n", s, (unsigned long long)t1); continue; }
        uint64_t t4 = now_ns();
        memcpy(&t2, m.p + 8, 8); memcpy(&t3, m.p + 16, 8);
        int64_t rtt = (int64_t)(t4 - t1) - (int64_t)(t3 - t2);
        int64_t off = ((int64_t)(t2 - t1) + (int64_t)(t3 - t4)) / 2;   /* NTP 4-timestamp */
        printf("%d,%llu,%lld,%lld\n", s, (unsigned long long)t1, (long long)rtt, (long long)off);
        next.tv_nsec += per;
        while (next.tv_nsec >= 1000000000L) { next.tv_nsec -= 1000000000L; next.tv_sec++; }
        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
    }
    return 0;
}
```

### 예제 4 — "MCU 쪽" 프로그램 (PONG 응답 + heartbeat watchdog)

무엇을 확인하나: 실제 MCU 펌웨어의 메인 루프 모양 그대로다 — 1 ms "SysTick"(여기서는 `poll` 타임아웃)마다 수신 바이트를 프레임 디코더에 먹이고, PING이면 자기 시계로 PONG을 답하고, HB면 watchdog을 갱신하고, deadline이 지나면 SAFE로 간다.

```c
/* "MCU side" (runs on the VM host): n4_mcu <tty> <deadline_ms> <hb.csv>
 * - answers PING with PONG(t1,t2,t3) using its OWN clock (offset 7.3 s, +50 ppm drift)
 * - heartbeat watchdog: no HB for deadline_ms -> SAFE (motors off); 20 good HBs -> RUN */
#include <poll.h>
#include "n4_proto.h"
#include "n4_link.h"

static uint64_t t0;
static uint64_t mcu_ns(void)                 /* independent oscillator model */
{
    uint64_t h = now_ns();
    return h + 7300000000ull + (h - t0) / 20000;          /* 1/20000 = 50 ppm */
}

int main(int argc, char **argv)
{
    if (argc != 4) { fprintf(stderr, "usage: %s tty deadline_ms hb.csv\n", argv[0]); return 1; }
    int fd = open_raw(argv[1]);
    uint64_t dl = (uint64_t)atol(argv[2]) * 1000000ull;
    FILE *csv = fopen(argv[3], "w");
    if (!csv) { perror(argv[3]); return 1; }
    t0 = now_ns(); setvbuf(stdout, NULL, _IOLBF, 0);
    printf("MCU t0_host_ns=%llu deadline=%s ms\n", (unsigned long long)t0, argv[2]); fflush(stdout);
    n4_rx rx = { 0 }; n4_msg m;
    uint64_t last_hb = 0; int safe = 1, good = 0, crcerr = 0;
    struct pollfd p = { .fd = fd, .events = POLLIN };
    uint8_t b[256];
    for (;;) {
        int r = poll(&p, 1, 1);                            /* 1 ms "SysTick" */
        uint64_t now = now_ns();
        if (r > 0) {
            ssize_t n = read(fd, b, sizeof b);
            if (n <= 0) { printf("link closed at %.1f ms\n", (now - t0) / 1e6); return 0; }
            uint64_t t2 = mcu_ns();
            for (ssize_t i = 0; i < n; i++) {
                int k = n4_feed(&rx, b[i], &m);
                if (k < 0) crcerr++;
                if (k != 1) continue;
                if (m.type == N4_PING) {
                    m.type = N4_PONG; m.len = 24;
                    memcpy(m.p + 8, &t2, 8);
                    uint64_t t3 = mcu_ns(); memcpy(m.p + 16, &t3, 8);
                    send_msg(fd, &m);
                } else if (m.type == N4_HB) {
                    fprintf(csv, "%u,%.3f\n", m.seq, (now - t0) / 1e6);
                    if (safe && ++good >= 20) { safe = 0; printf("%9.1f ms RUN  (20 good HB)\n", (now - t0) / 1e6); }
                    last_hb = now;
                } else if (m.type == N4_STATE) printf("%9.1f ms MARK %u\n", (now - t0) / 1e6, m.p[0]);
            }
        }
        if (!safe && last_hb && now - last_hb > dl) {
            safe = 1; good = 0;
            printf("%9.1f ms SAFE (last HB %.1f ms ago, crcerr=%d)\n", (now - t0) / 1e6, (now - last_hb) / 1e6, crcerr);
            fflush(stdout); fflush(csv);
        }
    }
}
```

세 파일(`n4_linux.c`, `n4_mcu.c`, `n4_ptest.c`) 모두 `gcc -std=gnu11 -Wall -Wextra -O2`로 경고 0개다. 게스트용 `n4_linux`는 `-static`으로 빌드해 BusyBox initramfs에 넣었다.

### 예제 5 — 기준선: VM 호스트에서 pty 쌍으로 재기

무엇을 확인하나: 에뮬레이션이 없는 경우(VM 호스트 커널 7.0, 일반 프로세스 두 개)에도 Linux 유저스페이스 왕복의 꼬리가 얼마나 긴지. `socat`으로 pty 두 개를 서로 이어 "가상 UART 케이블"을 만든다. 두 번째 측정은 VM 호스트에 바쁜 루프 8개(내 프로세스, nice 0)를 걸고 한다.

```sh
# run_native.sh 핵심 (VM 호스트에서 실행)
socat -d pty,raw,echo=0,link=/var/tmp/N4/n4_pa pty,raw,echo=0,link=/var/tmp/N4/n4_pb &
./n4_mcu /var/tmp/N4/n4_pb 30 n4_hb_native.csv > n4_mcu_native.log &
./n4_linux_host rtt /var/tmp/N4/n4_pa 5000 1000 > n4_rtt_native_idle.csv
for i in 1 2 3 4 5 6 7 8; do timeout 8 sh -c 'while :; do :; done' & done
./n4_linux_host rtt /var/tmp/N4/n4_pa 5000 1000 > n4_rtt_native_load.csv
```

분석 스크립트(Python, `ana.py`): RTT 백분위수, 그리고 "MCU" 시계의 참값(`7.3 s + 50 ppm × 경과`)을 알고 있으니 offset 추정 오차까지 계산한다.

```python
import sys, csv, statistics as st
def load(p):
    r=[];o=[];t=[];lost=0
    for row in csv.DictReader(open(p)):
        if int(row['rtt_ns'])<0: lost+=1; continue
        r.append(int(row['rtt_ns'])/1000); o.append(int(row['offset_ns'])); t.append(int(row['t1_ns']))
    return r,o,t,lost
def pct(a,q):
    s=sorted(a); return s[min(len(s)-1,int(q*len(s)))]
t0=int(sys.argv[2]) if len(sys.argv)>2 else None
r,o,t,lost=load(sys.argv[1])
print(f"{sys.argv[1].split('/')[-1]}: n={len(r)} lost={lost} RTT us min={min(r):.1f} p50={pct(r,.5):.1f} p99={pct(r,.99):.1f} p99.9={pct(r,.999):.1f} max={max(r):.1f}")
if t0:
    err=[ (oo-(7300000000+(tt-t0)/20000))/1000 for oo,tt in zip(o,t)]
    print(f"  offset error us: all  median|err|={st.median([abs(e) for e in err]):.1f} max|err|={max(abs(e) for e in err):.1f}")
    th=pct(r,.1)
    fe=[abs(e) for e,rr in zip(err,r) if rr<=th]
    print(f"  offset error us: RTT<=p10({th:.1f}) median={st.median(fe):.1f} max={max(fe):.1f}")
else:
    sp=max(o)-min(o); print(f"  offset spread (max-min) all = {sp/1000:.1f} us")
```

```text
MCU t0_host_ns=300533541571808 deadline=30 ms
n4_rtt_native_idle.csv: n=5000 lost=0 RTT us min=17.9 p50=373.5 p99=10339.8 p99.9=53343.8 max=147438.2
  offset error us: all  median|err|=57.7 max|err|=39539.3
  offset error us: RTT<=p10(111.8) median=6.8 max=34.9
n4_rtt_native_load.csv: n=5000 lost=0 RTT us min=18.0 p50=177.4 p99=5378.3 p99.9=27810.8 max=49726.7
  offset error us: all  median|err|=28.0 max|err|=24831.1
  offset error us: RTT<=p10(24.6) median=0.5 max=2.6
```

출력에서 볼 것: 최솟값은 18 µs인데 **p99는 5~10 ms, 최댓값은 50~147 ms**다. 중앙값 대비 꼬리가 수백 배다. 측정 시점 VM 자체가 다른 작성자 16명의 빌드/QEMU로 load average 6 이상이었기 때문에 "idle"이 "load"보다 오히려 나쁘게 나왔다 — 즉 **내가 거는 부하보다 내가 통제 못 하는 부하가 꼬리를 정한다**. 실제 제품에서도 똑같다: 카메라 드라이버, 네트워크 버스트, 로그 플러시, GC 같은 "남의 부하"가 RT 경로의 꼬리를 만든다.

### 예제 6 — QEMU 게스트(Linux 6.18) ↔ "MCU": 실제 UART 드라이버 경로

무엇을 확인하나: 게스트 안 앱이 진짜 PL011 UART 드라이버(`/dev/ttyAMA1`, tty 계층, IRQ)를 거쳐 호스트의 "MCU"와 왕복할 때의 분포. 게스트 안에서 두 vCPU를 바쁜 루프 4개로 채운 경우도 잰다.

```sh
# run_qemu.sh 핵심 — 두 번째 PL011을 호스트 pty에 연결하고, 그 pty를 "MCU"가 연다
qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m 512M -nographic -serial mon:stdio \
  -chardev pty,id=c1,path=/var/tmp/N4/n4_qpty -serial chardev:c1 \
  -kernel /var/lab/out/Image -initrd n4_i.gz -append "console=ttyAMA0 quiet LAB_CMD=/n4_guest.sh" > n4_console.raw 2>&1 &
./n4_mcu /var/tmp/N4/n4_qpty 30 n4_hb_qemu.csv > n4_mcu_qemu.log &
```

```sh
# n4_guest.sh 첫 번째 판(실행 1) 중 RTT 부분 — 게스트 안에서 실행
D=/dev/ttyAMA1
/bin/n4_linux rtt $D 3000 1000 > /tmp/idle.csv
for i in 1 2 3 4; do sh -c 'while :; do :; done' & done; L=$(jobs -p)
/bin/n4_linux rtt $D 3000 1000 > /tmp/load.csv
kill $L; wait 2>/dev/null
echo CSV_IDLE; cat /tmp/idle.csv; echo CSV_LOAD; cat /tmp/load.csv; echo CSV_END
```

게스트 부팅 로그에서 두 번째 UART가 잡힌 것을 먼저 확인했다(별도 부팅, `dmesg` 일부).

```text
[    1.180943] 9000000.pl011: ttyAMA0 at MMIO 0x9000000 (irq = 13, base_baud = 0) is a PL011 rev1
[    1.255276] 9040000.pl011: ttyAMA1 at MMIO 0x9040000 (irq = 14, base_baud = 0) is a PL011 rev1
```

콘솔에서 CSV를 잘라 내 같은 `ana.py`로 분석했다(게스트 시계와 호스트 시계는 다른 시계라 참값 비교는 못 하고 offset 퍼짐만 본다).

```text
q_idle.csv: n=3000 lost=0 RTT us min=376.5 p50=1338.2 p99=12158.2 p99.9=33186.5 max=43909.4
  offset spread (max-min) all = 28430.7 us
q_load.csv: n=2997 lost=3 RTT us min=481.1 p50=3318.4 p99=37179.1 p99.9=224045.2 max=267647.8
  offset spread (max-min) all = 217003.7 us
```

출력에서 볼 것: 게스트 부하 상태에서 **p99.9가 224 ms, 응답 3개는 200 ms 타임아웃으로 잃어버렸다.** TCG라서 절대값은 과장되어 있지만, 실제 하드웨어에서도 같은 구조 — "앱 스케줄링 → tty 계층 → UART 드라이버 → IRQ → 다시 tty → 앱 깨우기" — 에 꼬리가 있다. 이 경로 위에 20 kHz는 물론 1 kHz 루프도 놓을 수 없다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="250" x2="660" y2="250" stroke="currentColor"/><line x1="70" y1="40" x2="70" y2="250" stroke="currentColor"/><text x="64" y="254.0" font-size="12" text-anchor="end">10us</text><line x1="70" y1="250.0" x2="660" y2="250.0" stroke="#888" stroke-opacity="0.3"/><text x="64" y="212.0" font-size="12" text-anchor="end">100us</text><line x1="70" y1="208.0" x2="660" y2="208.0" stroke="#888" stroke-opacity="0.3"/><text x="64" y="170.0" font-size="12" text-anchor="end">1ms</text><line x1="70" y1="166.0" x2="660" y2="166.0" stroke="#888" stroke-opacity="0.3"/><text x="64" y="128.0" font-size="12" text-anchor="end">10ms</text><line x1="70" y1="124.0" x2="660" y2="124.0" stroke="#888" stroke-opacity="0.3"/><text x="64" y="86.0" font-size="12" text-anchor="end">100ms</text><line x1="70" y1="82.0" x2="660" y2="82.0" stroke="#888" stroke-opacity="0.3"/>
<text x="64" y="44.0" font-size="12" text-anchor="end">1s</text><line x1="70" y1="40.0" x2="660" y2="40.0" stroke="#888" stroke-opacity="0.3"/><rect x="90" y="239.4" width="18" height="10.6" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><rect x="112" y="184.0" width="18" height="66.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><rect x="134" y="123.4" width="18" height="126.6" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/><rect x="156" y="93.5" width="18" height="156.5" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/><rect x="178" y="74.9" width="18" height="175.1" fill="#888" fill-opacity="0.7" stroke="#888"/><text x="145" y="268" font-size="13" text-anchor="middle">native idle</text><rect x="235" y="239.2" width="18" height="10.8" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
<rect x="257" y="197.5" width="18" height="52.5" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><rect x="279" y="135.3" width="18" height="114.7" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/><rect x="301" y="105.3" width="18" height="144.7" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/><rect x="323" y="94.7" width="18" height="155.3" fill="#888" fill-opacity="0.7" stroke="#888"/><text x="290" y="268" font-size="13" text-anchor="middle">native load</text><rect x="380" y="183.8" width="18" height="66.2" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><rect x="402" y="160.7" width="18" height="89.3" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><rect x="424" y="120.4" width="18" height="129.6" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/><rect x="446" y="102.1" width="18" height="147.9" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
<rect x="468" y="97.0" width="18" height="153.0" fill="#888" fill-opacity="0.7" stroke="#888"/><text x="435" y="268" font-size="13" text-anchor="middle">QEMU idle</text><rect x="525" y="179.3" width="18" height="70.7" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><rect x="547" y="144.1" width="18" height="105.9" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><rect x="569" y="100.0" width="18" height="150.0" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/><rect x="591" y="67.3" width="18" height="182.7" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/><rect x="613" y="64.0" width="18" height="186.0" fill="#888" fill-opacity="0.7" stroke="#888"/><text x="580" y="268" font-size="13" text-anchor="middle">QEMU load</text><rect x="150" y="286" width="12" height="12" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/><text x="166" y="297" font-size="12">min</text>
<rect x="240" y="286" width="12" height="12" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/><text x="256" y="297" font-size="12">p50</text><rect x="330" y="286" width="12" height="12" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/><text x="346" y="297" font-size="12">p99</text><rect x="420" y="286" width="12" height="12" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/><text x="436" y="297" font-size="12">p99.9</text><rect x="510" y="286" width="12" height="12" fill="#888" fill-opacity="0.7" stroke="#888"/><text x="526" y="297" font-size="12">max</text>
<text x="70" y="30" font-size="12">PING→PONG RTT (MCU 처리 시간 제외), 로그 축</text></svg>
```

그림 5 — 네 조건의 RTT 분포(로그 축, 실측). 최솟값과 최댓값 사이가 3~4자리 차이다. 루프 설계는 최솟값이 아니라 **최댓값**으로 한다.

### 6.2 이 측정이 설계에 주는 결론

- **루프를 링크 너머로 닫지 않는다.** MCU 안에서 센서 → 제어 → 출력이 닫히고, 링크로는 setpoint와 상태만 오간다. 링크 지연이 늘어나면 "명령이 늦게 도착"할 뿐 루프 안정성은 영향받지 않는다.
- **늦은 명령은 버린다.** 명령에 타임스탬프(공통 시간축, §7)와 유효 기간을 넣고, MCU는 유효 기간이 지난 명령을 무시하고 마지막 안전한 동작(감속 · 유지)을 한다.
- **실제 보드라면 꼬리를 줄이는 손잡이**: RT 스레드 우선순위(N2), UART IRQ 스레드 우선순위(PREEMPT_RT에서 IRQ도 스레드), tty `low_latency`/FIFO 트리거 레벨, DMA, `isolcpus`. 그래도 "보장"은 아니고 "줄임"이다.

---

## 7. 시간 동기 — Linux와 MCU의 시계 맞추기

### 7.1 왜 필요한가

센서 융합은 "이 IMU 샘플과 이 카메라 프레임이 같은 순간인가"에 달려 있다. MCU가 IMU를 1 kHz로 읽어 보내고, Linux가 카메라를 30 Hz로 받는다. Linux가 IMU 샘플에 **"도착 시각"**을 붙이면, 방금 본 것처럼 링크 지연이 수 ms ~ 수백 ms 흔들리므로 타임스탬프가 엉망이 된다. 원칙은 하나다: **샘플은 생긴 곳에서(MCU 타이머로) 타임스탬프를 찍고, Linux는 MCU 시각을 자기 시각으로 변환한다.** 그 변환식(offset + drift)을 구하는 것이 시간 동기다.

### 7.2 방법 네 가지

| 방법 | 원리 | 정확도 (일반적 범위, 기억 기반) | 필요한 것 |
|---|---|---|---|
| 메시지 교환 (NTP식 4-타임스탬프) | PING/PONG의 t1~t4로 offset 추정, 최소 RTT 필터 + 직선 피팅 | 링크 지터의 비대칭에 좌우 — UART면 수십 µs ~ 수백 µs | 추가 하드웨어 없음 (이 노트 실험) |
| 동기 펄스 (PPS · GPIO) | 한쪽이 펄스를 내고 양쪽이 HW로 그 순간을 캡처 (MCU 타이머 input capture, Linux `pps-gpio` 또는 GPIO 타임스탬프) | µs 이하 ~ 수 µs | GPIO 선 하나, 펄스 번호는 메시지로 |
| 공유 타이머 (이종 SoC) | 같은 SoC 시스템 카운터를 A 코어와 M/R 코어가 같이 읽음 | 수십 ns ~ (카운터 분해능) | SoC가 지원해야 함 (데이터시트 확인) |
| PTP (IEEE 1588 / gPTP 802.1AS) | Ethernet 프레임의 HW 타임스탬프로 offset/지연 측정 | 수십 ns ~ 1 µs 이하 | PTP 지원 MAC (Linux `ptp4l`/`phc2sys`, MCU 측 PTP 스택) |

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="120" y="24" text-anchor="middle" font-size="13">Linux 시계</text>
  <text x="380" y="24" text-anchor="middle" font-size="13">MCU 시계</text>
  <line x1="120" y1="34" x2="120" y2="270" stroke="#4a7bd0" stroke-width="2"/>
  <line x1="380" y1="34" x2="380" y2="270" stroke="#e08a3c" stroke-width="2"/>
  <line x1="120" y1="60" x2="378" y2="120" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="112" y="64" text-anchor="end" font-size="13">t1</text>
  <text x="388" y="124" font-size="13">t2</text>
  <text x="240" y="78" text-anchor="middle" font-size="12">PING(t1)</text>
  <line x1="380" y1="150" x2="122" y2="230" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="388" y="154" font-size="13">t3</text>
  <text x="112" y="234" text-anchor="end" font-size="13">t4</text>
  <text x="270" y="216" text-anchor="middle" font-size="12">PONG(t1, t2, t3)</text>
  <text x="410" y="60" font-size="12">RTT = (t4 - t1) - (t3 - t2)</text>
  <text x="410" y="200" font-size="12">offset = ((t2 - t1) + (t3 - t4)) / 2</text>
  <text x="410" y="220" font-size="12">가정: 가는 지연 = 오는 지연</text>
  <text x="410" y="240" font-size="12">오차 ≤ RTT / 2 (비대칭이 클수록)</text>
  <text x="20" y="290" font-size="12">그래서 RTT가 가장 작은 샘플(대칭에 가까운 샘플)만 골라 쓰면 오차 상한이 같이 작아진다.</text>
</svg>
```

그림 6 — 4-타임스탬프 교환. 오프셋 추정의 유일한 가정은 "왕복 지연이 대칭"이고, 오차의 상한은 RTT의 절반이다.

### 예제 7 — 최소 RTT 필터 + 직선 피팅으로 offset · drift 구하기

무엇을 확인하나: 예제 5·6에서 모은 샘플로 (1) 모든 샘플을 그대로 직선 피팅한 경우와 (2) 100개 창마다 RTT 최소 샘플 하나만 골라 피팅한 경우를 비교한다. 기울기가 드리프트(ppm), 잔차가 시간 동기 오차다. "MCU" 시계의 참 드리프트는 **50 ppm**이다.

```python
import sys, csv
rows=[(int(r['t1_ns']),int(r['rtt_ns']),int(r['offset_ns'])) for r in csv.DictReader(open(sys.argv[1])) if int(r['rtt_ns'])>0]
def fit(pts):
    n=len(pts); mx=sum(p[0] for p in pts)/n; my=sum(p[1] for p in pts)/n
    b=sum((p[0]-mx)*(p[1]-my) for p in pts)/sum((p[0]-mx)**2 for p in pts)
    res=[p[1]-(my+b*(p[0]-mx)) for p in pts]
    return b, max(abs(r) for r in res)
t0=rows[0][0]
allp=[((t-t0)/1e9,o/1e3) for t,r,o in rows]
rs=sorted(r for _,r,_ in rows); th=rs[len(rs)//10]
# per 100-sample window keep the min-RTT sample (classic NTP/PTP-style filter)
win=[min(rows[i:i+100],key=lambda x:x[1]) for i in range(0,len(rows),100)]
fp=[((t-t0)/1e9,o/1e3) for t,r,o in win]
b1,e1=fit(allp); b2,e2=fit(fp)
print(f"{sys.argv[1]}: all samples  drift={b1:.1f} ppm  max residual={e1:.1f} us (n={len(allp)})")
print(f"{sys.argv[1]}: min-RTT/100  drift={b2:.1f} ppm  max residual={e2:.1f} us (n={len(fp)})")
```

```text
n4_rtt_native_idle.csv: all samples  drift=49.9 ppm  max residual=39539.0 us (n=5000)
n4_rtt_native_idle.csv: min-RTT/100  drift=48.5 ppm  max residual=38.9 us (n=50)
q_idle.csv: all samples  drift=78.6 ppm  max residual=20355.1 us (n=3000)
q_idle.csv: min-RTT/100  drift=58.4 ppm  max residual=139.4 us (n=30)
q_load.csv: all samples  drift=97.9 ppm  max residual=130323.1 us (n=2997)
q_load.csv: min-RTT/100  drift=46.4 ppm  max residual=311.5 us (n=30)
```

(단위 주의: x를 초, y를 µs로 피팅했으니 기울기 µs/s = ppm이다.)

출력에서 볼 것:

- 모든 샘플을 쓰면 최대 잔차가 20~130 **ms**다. 최소 RTT 필터를 쓰면 같은 데이터에서 39~311 **µs**로 줄어든다 — 2~3자리 개선. 예제 5의 참값 비교에서도 RTT 하위 10% 샘플만 쓰면 오차 중앙값이 57.7 → 6.8 µs, 28.0 → 0.5 µs였다.
- QEMU 게스트의 부하 상태(가장 나쁜 링크)에서도 필터를 쓰면 드리프트 46.4 ppm으로 참값 50 ppm 근처가 나온다. 필터 없이 쓰면 97.9 ppm — 두 배 틀린다.
- 실제 시스템은 이 offset/drift 모델을 계속 갱신하고(칼만 필터나 PI 루프, `phc2sys`가 하는 일과 같은 구조), MCU가 보낸 샘플 타임스탬프 `t_mcu`를 `t_linux = (t_mcu - offset(t))`로 변환해 쓴다. 정확도가 더 필요하면 메시지 대신 HW 펄스나 PTP로 간다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="240.0" x2="650" y2="240.0" stroke="currentColor"/><line x1="70" y1="50.0" x2="70" y2="240.0" stroke="currentColor"/><text x="64" y="220.2" font-size="12" text-anchor="end">-10ms</text><text x="64" y="160.9" font-size="12" text-anchor="end">-5ms</text><text x="64" y="101.5" font-size="12" text-anchor="end">0ms</text><text x="64" y="54.0" font-size="12" text-anchor="end">4ms</text><circle cx="70.0" cy="136.3" r="1.3" fill="#888"/><circle cx="72.9" cy="115.4" r="1.3" fill="#888"/><circle cx="74.3" cy="130.4" r="1.3" fill="#888"/><circle cx="77.2" cy="116.4" r="1.3" fill="#888"/><circle cx="80.3" cy="125.2" r="1.3" fill="#888"/><circle cx="83.8" cy="130.4" r="1.3" fill="#888"/><circle cx="86.5" cy="129.3" r="1.3" fill="#888"/><circle cx="88.8" cy="103.1" r="1.3" fill="#888"/><circle cx="90.4" cy="119.7" r="1.3" fill="#888"/><circle cx="92.3" cy="105.1" r="1.3" fill="#888"/>
<circle cx="94.2" cy="103.5" r="1.3" fill="#888"/><circle cx="95.2" cy="99.2" r="1.3" fill="#888"/><circle cx="96.3" cy="102.9" r="1.3" fill="#888"/><circle cx="98.2" cy="115.6" r="1.3" fill="#888"/><circle cx="100.3" cy="110.7" r="1.3" fill="#888"/><circle cx="102.3" cy="113.2" r="1.3" fill="#888"/><circle cx="104.1" cy="109.8" r="1.3" fill="#888"/><circle cx="105.8" cy="111.6" r="1.3" fill="#888"/><circle cx="107.2" cy="113.0" r="1.3" fill="#888"/><circle cx="108.2" cy="107.8" r="1.3" fill="#888"/><circle cx="109.8" cy="114.2" r="1.3" fill="#888"/><circle cx="113.8" cy="104.5" r="1.3" fill="#888"/><circle cx="118.7" cy="102.1" r="1.3" fill="#888"/><circle cx="122.2" cy="111.1" r="1.3" fill="#888"/><circle cx="127.1" cy="106.2" r="1.3" fill="#888"/><circle cx="130.2" cy="137.6" r="1.3" fill="#888"/><circle cx="133.3" cy="106.6" r="1.3" fill="#888"/>
<circle cx="135.7" cy="105.9" r="1.3" fill="#888"/><circle cx="137.7" cy="99.2" r="1.3" fill="#888"/><circle cx="139.7" cy="108.3" r="1.3" fill="#888"/><circle cx="141.2" cy="112.7" r="1.3" fill="#888"/><circle cx="143.1" cy="101.1" r="1.3" fill="#888"/><circle cx="144.6" cy="108.5" r="1.3" fill="#888"/><circle cx="146.4" cy="103.1" r="1.3" fill="#888"/><circle cx="149.1" cy="106.7" r="1.3" fill="#888"/><circle cx="151.1" cy="107.7" r="1.3" fill="#888"/><circle cx="152.6" cy="102.7" r="1.3" fill="#888"/><circle cx="153.8" cy="98.7" r="1.3" fill="#888"/><circle cx="156.1" cy="147.8" r="1.3" fill="#888"/><circle cx="158.4" cy="113.1" r="1.3" fill="#888"/><circle cx="162.6" cy="102.0" r="1.3" fill="#888"/><circle cx="164.3" cy="87.9" r="1.3" fill="#888"/><circle cx="165.4" cy="98.9" r="1.3" fill="#888"/><circle cx="166.4" cy="100.6" r="1.3" fill="#888"/>
<circle cx="167.4" cy="97.7" r="1.3" fill="#888"/><circle cx="168.2" cy="95.7" r="1.3" fill="#888"/><circle cx="168.9" cy="108.3" r="1.3" fill="#888"/><circle cx="170.1" cy="85.5" r="1.3" fill="#888"/><circle cx="171.6" cy="98.5" r="1.3" fill="#888"/><circle cx="172.9" cy="83.0" r="1.3" fill="#888"/><circle cx="174.9" cy="95.7" r="1.3" fill="#888"/><circle cx="179.4" cy="124.1" r="1.3" fill="#888"/><circle cx="182.1" cy="108.7" r="1.3" fill="#888"/><circle cx="184.1" cy="119.0" r="1.3" fill="#888"/><circle cx="187.0" cy="132.2" r="1.3" fill="#888"/><circle cx="190.6" cy="130.3" r="1.3" fill="#888"/><circle cx="195.6" cy="151.6" r="1.3" fill="#888"/><circle cx="199.4" cy="108.4" r="1.3" fill="#888"/><circle cx="201.4" cy="122.8" r="1.3" fill="#888"/><circle cx="204.0" cy="102.3" r="1.3" fill="#888"/><circle cx="205.0" cy="97.8" r="1.3" fill="#888"/>
<circle cx="207.6" cy="94.5" r="1.3" fill="#888"/><circle cx="209.5" cy="134.4" r="1.3" fill="#888"/><circle cx="211.3" cy="96.9" r="1.3" fill="#888"/><circle cx="212.3" cy="121.8" r="1.3" fill="#888"/><circle cx="216.7" cy="101.1" r="1.3" fill="#888"/><circle cx="217.6" cy="108.1" r="1.3" fill="#888"/><circle cx="218.7" cy="96.3" r="1.3" fill="#888"/><circle cx="219.7" cy="96.8" r="1.3" fill="#888"/><circle cx="220.5" cy="98.4" r="1.3" fill="#888"/><circle cx="221.3" cy="104.4" r="1.3" fill="#888"/><circle cx="222.3" cy="106.7" r="1.3" fill="#888"/><circle cx="223.5" cy="99.0" r="1.3" fill="#888"/><circle cx="224.6" cy="100.8" r="1.3" fill="#888"/><circle cx="226.3" cy="114.9" r="1.3" fill="#888"/><circle cx="227.5" cy="103.8" r="1.3" fill="#888"/><circle cx="228.3" cy="101.4" r="1.3" fill="#888"/><circle cx="229.3" cy="101.2" r="1.3" fill="#888"/>
<circle cx="230.3" cy="100.8" r="1.3" fill="#888"/><circle cx="231.0" cy="98.3" r="1.3" fill="#888"/><circle cx="231.9" cy="95.2" r="1.3" fill="#888"/><circle cx="232.9" cy="99.4" r="1.3" fill="#888"/><circle cx="233.8" cy="115.9" r="1.3" fill="#888"/><circle cx="235.3" cy="105.2" r="1.3" fill="#888"/><circle cx="236.8" cy="97.5" r="1.3" fill="#888"/><circle cx="238.1" cy="98.8" r="1.3" fill="#888"/><circle cx="239.1" cy="100.4" r="1.3" fill="#888"/><circle cx="240.0" cy="99.4" r="1.3" fill="#888"/><circle cx="241.1" cy="97.3" r="1.3" fill="#888"/><circle cx="241.7" cy="101.2" r="1.3" fill="#888"/><circle cx="242.8" cy="101.6" r="1.3" fill="#888"/><circle cx="243.9" cy="98.8" r="1.3" fill="#888"/><circle cx="244.8" cy="100.7" r="1.3" fill="#888"/><circle cx="246.3" cy="98.2" r="1.3" fill="#888"/><circle cx="247.5" cy="96.8" r="1.3" fill="#888"/>
<circle cx="248.4" cy="102.2" r="1.3" fill="#888"/><circle cx="249.9" cy="99.2" r="1.3" fill="#888"/><circle cx="250.8" cy="109.0" r="1.3" fill="#888"/><circle cx="252.0" cy="100.1" r="1.3" fill="#888"/><circle cx="252.9" cy="100.2" r="1.3" fill="#888"/><circle cx="253.5" cy="104.0" r="1.3" fill="#888"/><circle cx="255.0" cy="98.2" r="1.3" fill="#888"/><circle cx="256.1" cy="100.8" r="1.3" fill="#888"/><circle cx="258.3" cy="102.0" r="1.3" fill="#888"/><circle cx="259.4" cy="97.7" r="1.3" fill="#888"/><circle cx="260.5" cy="98.0" r="1.3" fill="#888"/><circle cx="261.5" cy="101.1" r="1.3" fill="#888"/><circle cx="262.5" cy="100.9" r="1.3" fill="#888"/><circle cx="263.9" cy="113.9" r="1.3" fill="#888"/><circle cx="265.9" cy="106.4" r="1.3" fill="#888"/><circle cx="267.2" cy="97.7" r="1.3" fill="#888"/><circle cx="267.8" cy="116.0" r="1.3" fill="#888"/>
<circle cx="269.3" cy="107.1" r="1.3" fill="#888"/><circle cx="270.9" cy="100.9" r="1.3" fill="#888"/><circle cx="271.7" cy="102.8" r="1.3" fill="#888"/><circle cx="272.8" cy="100.8" r="1.3" fill="#888"/><circle cx="273.6" cy="100.2" r="1.3" fill="#888"/><circle cx="274.3" cy="97.4" r="1.3" fill="#888"/><circle cx="274.8" cy="106.0" r="1.3" fill="#888"/><circle cx="275.8" cy="96.7" r="1.3" fill="#888"/><circle cx="276.4" cy="99.1" r="1.3" fill="#888"/><circle cx="277.2" cy="100.1" r="1.3" fill="#888"/><circle cx="278.0" cy="98.2" r="1.3" fill="#888"/><circle cx="278.5" cy="98.0" r="1.3" fill="#888"/><circle cx="279.1" cy="99.5" r="1.3" fill="#888"/><circle cx="279.6" cy="93.7" r="1.3" fill="#888"/><circle cx="280.0" cy="98.6" r="1.3" fill="#888"/><circle cx="280.4" cy="95.5" r="1.3" fill="#888"/><circle cx="280.9" cy="101.2" r="1.3" fill="#888"/>
<circle cx="281.8" cy="96.4" r="1.3" fill="#888"/><circle cx="282.4" cy="101.1" r="1.3" fill="#888"/><circle cx="282.9" cy="96.7" r="1.3" fill="#888"/><circle cx="283.4" cy="102.4" r="1.3" fill="#888"/><circle cx="283.9" cy="96.1" r="1.3" fill="#888"/><circle cx="284.3" cy="97.7" r="1.3" fill="#888"/><circle cx="285.0" cy="99.5" r="1.3" fill="#888"/><circle cx="285.6" cy="100.8" r="1.3" fill="#888"/><circle cx="286.3" cy="98.2" r="1.3" fill="#888"/><circle cx="286.8" cy="99.4" r="1.3" fill="#888"/><circle cx="287.3" cy="98.9" r="1.3" fill="#888"/><circle cx="287.7" cy="97.3" r="1.3" fill="#888"/><circle cx="288.4" cy="99.3" r="1.3" fill="#888"/><circle cx="289.0" cy="98.8" r="1.3" fill="#888"/><circle cx="289.5" cy="99.7" r="1.3" fill="#888"/><circle cx="290.0" cy="103.4" r="1.3" fill="#888"/><circle cx="290.6" cy="100.0" r="1.3" fill="#888"/>
<circle cx="291.4" cy="98.3" r="1.3" fill="#888"/><circle cx="292.1" cy="98.2" r="1.3" fill="#888"/><circle cx="292.5" cy="95.4" r="1.3" fill="#888"/><circle cx="292.9" cy="95.7" r="1.3" fill="#888"/><circle cx="293.3" cy="96.4" r="1.3" fill="#888"/><circle cx="293.7" cy="95.7" r="1.3" fill="#888"/><circle cx="294.4" cy="100.7" r="1.3" fill="#888"/><circle cx="295.1" cy="94.8" r="1.3" fill="#888"/><circle cx="295.6" cy="96.8" r="1.3" fill="#888"/><circle cx="296.1" cy="96.2" r="1.3" fill="#888"/><circle cx="296.5" cy="98.2" r="1.3" fill="#888"/><circle cx="296.9" cy="98.3" r="1.3" fill="#888"/><circle cx="297.3" cy="97.4" r="1.3" fill="#888"/><circle cx="297.7" cy="98.6" r="1.3" fill="#888"/><circle cx="298.4" cy="99.6" r="1.3" fill="#888"/><circle cx="298.9" cy="96.4" r="1.3" fill="#888"/><circle cx="299.5" cy="97.8" r="1.3" fill="#888"/>
<circle cx="300.0" cy="95.7" r="1.3" fill="#888"/><circle cx="300.4" cy="98.2" r="1.3" fill="#888"/><circle cx="301.0" cy="98.5" r="1.3" fill="#888"/><circle cx="301.5" cy="110.6" r="1.3" fill="#888"/><circle cx="302.2" cy="95.7" r="1.3" fill="#888"/><circle cx="302.7" cy="96.5" r="1.3" fill="#888"/><circle cx="303.8" cy="97.7" r="1.3" fill="#888"/><circle cx="304.4" cy="97.8" r="1.3" fill="#888"/><circle cx="304.9" cy="98.5" r="1.3" fill="#888"/><circle cx="305.5" cy="99.5" r="1.3" fill="#888"/><circle cx="306.2" cy="98.4" r="1.3" fill="#888"/><circle cx="307.7" cy="92.1" r="1.3" fill="#888"/><circle cx="309.1" cy="102.9" r="1.3" fill="#888"/><circle cx="310.0" cy="99.7" r="1.3" fill="#888"/><circle cx="311.0" cy="96.8" r="1.3" fill="#888"/><circle cx="311.8" cy="96.5" r="1.3" fill="#888"/><circle cx="312.5" cy="100.0" r="1.3" fill="#888"/>
<circle cx="313.1" cy="98.7" r="1.3" fill="#888"/><circle cx="313.7" cy="97.9" r="1.3" fill="#888"/><circle cx="314.4" cy="104.1" r="1.3" fill="#888"/><circle cx="315.0" cy="101.0" r="1.3" fill="#888"/><circle cx="316.1" cy="98.8" r="1.3" fill="#888"/><circle cx="316.8" cy="98.1" r="1.3" fill="#888"/><circle cx="317.4" cy="97.3" r="1.3" fill="#888"/><circle cx="318.1" cy="96.1" r="1.3" fill="#888"/><circle cx="318.6" cy="98.6" r="1.3" fill="#888"/><circle cx="319.2" cy="98.9" r="1.3" fill="#888"/><circle cx="319.9" cy="97.9" r="1.3" fill="#888"/><circle cx="320.6" cy="97.9" r="1.3" fill="#888"/><circle cx="321.2" cy="97.5" r="1.3" fill="#888"/><circle cx="322.3" cy="102.9" r="1.3" fill="#888"/><circle cx="322.8" cy="100.8" r="1.3" fill="#888"/><circle cx="323.4" cy="95.4" r="1.3" fill="#888"/><circle cx="324.0" cy="97.5" r="1.3" fill="#888"/>
<circle cx="324.7" cy="100.3" r="1.3" fill="#888"/><circle cx="325.5" cy="103.2" r="1.3" fill="#888"/><circle cx="326.4" cy="99.0" r="1.3" fill="#888"/><circle cx="326.9" cy="97.8" r="1.3" fill="#888"/><circle cx="327.5" cy="99.3" r="1.3" fill="#888"/><circle cx="328.2" cy="96.8" r="1.3" fill="#888"/><circle cx="328.9" cy="98.4" r="1.3" fill="#888"/><circle cx="329.5" cy="104.4" r="1.3" fill="#888"/><circle cx="330.2" cy="97.6" r="1.3" fill="#888"/><circle cx="330.7" cy="100.2" r="1.3" fill="#888"/><circle cx="331.4" cy="101.2" r="1.3" fill="#888"/><circle cx="332.4" cy="102.4" r="1.3" fill="#888"/><circle cx="333.3" cy="95.6" r="1.3" fill="#888"/><circle cx="333.9" cy="98.3" r="1.3" fill="#888"/><circle cx="334.8" cy="100.1" r="1.3" fill="#888"/><circle cx="335.7" cy="106.0" r="1.3" fill="#888"/><circle cx="336.5" cy="99.4" r="1.3" fill="#888"/>
<circle cx="337.3" cy="102.7" r="1.3" fill="#888"/><circle cx="337.9" cy="101.7" r="1.3" fill="#888"/><circle cx="339.0" cy="101.7" r="1.3" fill="#888"/><circle cx="339.8" cy="102.3" r="1.3" fill="#888"/><circle cx="340.8" cy="104.1" r="1.3" fill="#888"/><circle cx="341.7" cy="102.6" r="1.3" fill="#888"/><circle cx="342.8" cy="96.9" r="1.3" fill="#888"/><circle cx="343.6" cy="97.8" r="1.3" fill="#888"/><circle cx="344.3" cy="96.6" r="1.3" fill="#888"/><circle cx="344.9" cy="99.5" r="1.3" fill="#888"/><circle cx="345.6" cy="97.0" r="1.3" fill="#888"/><circle cx="346.3" cy="99.1" r="1.3" fill="#888"/><circle cx="347.1" cy="98.4" r="1.3" fill="#888"/><circle cx="347.8" cy="100.5" r="1.3" fill="#888"/><circle cx="348.6" cy="98.5" r="1.3" fill="#888"/><circle cx="349.3" cy="100.5" r="1.3" fill="#888"/><circle cx="350.1" cy="98.4" r="1.3" fill="#888"/>
<circle cx="350.9" cy="98.4" r="1.3" fill="#888"/><circle cx="351.5" cy="99.5" r="1.3" fill="#888"/><circle cx="353.0" cy="128.4" r="1.3" fill="#888"/><circle cx="356.3" cy="170.6" r="1.3" fill="#888"/><circle cx="359.8" cy="139.2" r="1.3" fill="#888"/><circle cx="362.6" cy="127.1" r="1.3" fill="#888"/><circle cx="365.7" cy="124.5" r="1.3" fill="#888"/><circle cx="370.7" cy="104.0" r="1.3" fill="#888"/><circle cx="372.5" cy="98.3" r="1.3" fill="#888"/><circle cx="373.4" cy="112.8" r="1.3" fill="#888"/><circle cx="374.1" cy="96.5" r="1.3" fill="#888"/><circle cx="375.2" cy="97.1" r="1.3" fill="#888"/><circle cx="375.7" cy="99.9" r="1.3" fill="#888"/><circle cx="376.4" cy="106.1" r="1.3" fill="#888"/><circle cx="377.0" cy="99.5" r="1.3" fill="#888"/><circle cx="377.8" cy="98.6" r="1.3" fill="#888"/><circle cx="378.5" cy="98.9" r="1.3" fill="#888"/>
<circle cx="379.1" cy="98.9" r="1.3" fill="#888"/><circle cx="379.9" cy="98.0" r="1.3" fill="#888"/><circle cx="380.6" cy="99.7" r="1.3" fill="#888"/><circle cx="381.3" cy="99.6" r="1.3" fill="#888"/><circle cx="382.2" cy="98.9" r="1.3" fill="#888"/><circle cx="382.8" cy="100.7" r="1.3" fill="#888"/><circle cx="383.6" cy="98.6" r="1.3" fill="#888"/><circle cx="384.3" cy="97.3" r="1.3" fill="#888"/><circle cx="384.6" cy="97.2" r="1.3" fill="#888"/><circle cx="385.0" cy="97.8" r="1.3" fill="#888"/><circle cx="385.4" cy="97.7" r="1.3" fill="#888"/><circle cx="386.1" cy="99.4" r="1.3" fill="#888"/><circle cx="386.6" cy="96.7" r="1.3" fill="#888"/><circle cx="387.0" cy="96.9" r="1.3" fill="#888"/><circle cx="387.3" cy="98.0" r="1.3" fill="#888"/><circle cx="387.9" cy="99.4" r="1.3" fill="#888"/><circle cx="388.3" cy="97.0" r="1.3" fill="#888"/>
<circle cx="388.6" cy="96.9" r="1.3" fill="#888"/><circle cx="389.8" cy="121.1" r="1.3" fill="#888"/><circle cx="392.2" cy="152.9" r="1.3" fill="#888"/><circle cx="394.5" cy="116.6" r="1.3" fill="#888"/><circle cx="395.7" cy="103.0" r="1.3" fill="#888"/><circle cx="396.5" cy="102.2" r="1.3" fill="#888"/><circle cx="397.3" cy="94.0" r="1.3" fill="#888"/><circle cx="398.3" cy="100.7" r="1.3" fill="#888"/><circle cx="398.9" cy="100.2" r="1.3" fill="#888"/><circle cx="399.7" cy="99.2" r="1.3" fill="#888"/><circle cx="400.6" cy="101.0" r="1.3" fill="#888"/><circle cx="401.2" cy="98.2" r="1.3" fill="#888"/><circle cx="401.8" cy="94.6" r="1.3" fill="#888"/><circle cx="402.4" cy="98.6" r="1.3" fill="#888"/><circle cx="403.0" cy="99.4" r="1.3" fill="#888"/><circle cx="403.7" cy="101.0" r="1.3" fill="#888"/><circle cx="404.2" cy="97.5" r="1.3" fill="#888"/>
<circle cx="404.6" cy="99.7" r="1.3" fill="#888"/><circle cx="405.2" cy="101.1" r="1.3" fill="#888"/><circle cx="405.7" cy="99.8" r="1.3" fill="#888"/><circle cx="406.2" cy="101.7" r="1.3" fill="#888"/><circle cx="406.8" cy="99.2" r="1.3" fill="#888"/><circle cx="407.5" cy="96.1" r="1.3" fill="#888"/><circle cx="408.6" cy="97.7" r="1.3" fill="#888"/><circle cx="409.2" cy="98.0" r="1.3" fill="#888"/><circle cx="409.6" cy="97.8" r="1.3" fill="#888"/><circle cx="410.5" cy="100.6" r="1.3" fill="#888"/><circle cx="411.4" cy="99.5" r="1.3" fill="#888"/><circle cx="412.5" cy="104.8" r="1.3" fill="#888"/><circle cx="413.9" cy="107.9" r="1.3" fill="#888"/><circle cx="415.5" cy="139.2" r="1.3" fill="#888"/><circle cx="419.0" cy="101.5" r="1.3" fill="#888"/><circle cx="421.9" cy="99.9" r="1.3" fill="#888"/><circle cx="423.2" cy="101.8" r="1.3" fill="#888"/>
<circle cx="423.8" cy="98.7" r="1.3" fill="#888"/><circle cx="425.1" cy="100.9" r="1.3" fill="#888"/><circle cx="426.1" cy="103.6" r="1.3" fill="#888"/><circle cx="426.7" cy="99.5" r="1.3" fill="#888"/><circle cx="427.8" cy="100.3" r="1.3" fill="#888"/><circle cx="428.4" cy="98.7" r="1.3" fill="#888"/><circle cx="429.1" cy="108.2" r="1.3" fill="#888"/><circle cx="430.1" cy="99.2" r="1.3" fill="#888"/><circle cx="431.5" cy="98.8" r="1.3" fill="#888"/><circle cx="432.4" cy="100.7" r="1.3" fill="#888"/><circle cx="433.7" cy="103.1" r="1.3" fill="#888"/><circle cx="434.5" cy="103.8" r="1.3" fill="#888"/><circle cx="435.2" cy="100.3" r="1.3" fill="#888"/><circle cx="436.4" cy="97.5" r="1.3" fill="#888"/><circle cx="437.6" cy="102.9" r="1.3" fill="#888"/><circle cx="439.1" cy="104.7" r="1.3" fill="#888"/><circle cx="440.3" cy="106.6" r="1.3" fill="#888"/>
<circle cx="441.2" cy="101.2" r="1.3" fill="#888"/><circle cx="442.2" cy="99.4" r="1.3" fill="#888"/><circle cx="443.4" cy="103.7" r="1.3" fill="#888"/><circle cx="444.6" cy="101.3" r="1.3" fill="#888"/><circle cx="446.1" cy="99.3" r="1.3" fill="#888"/><circle cx="447.3" cy="94.8" r="1.3" fill="#888"/><circle cx="448.0" cy="103.4" r="1.3" fill="#888"/><circle cx="449.3" cy="99.5" r="1.3" fill="#888"/><circle cx="450.3" cy="105.5" r="1.3" fill="#888"/><circle cx="451.8" cy="102.6" r="1.3" fill="#888"/><circle cx="453.1" cy="104.6" r="1.3" fill="#888"/><circle cx="454.6" cy="98.7" r="1.3" fill="#888"/><circle cx="455.4" cy="99.4" r="1.3" fill="#888"/><circle cx="456.3" cy="105.6" r="1.3" fill="#888"/><circle cx="457.3" cy="98.6" r="1.3" fill="#888"/><circle cx="458.1" cy="102.4" r="1.3" fill="#888"/><circle cx="459.3" cy="99.5" r="1.3" fill="#888"/>
<circle cx="460.3" cy="96.9" r="1.3" fill="#888"/><circle cx="461.4" cy="103.2" r="1.3" fill="#888"/><circle cx="462.5" cy="102.2" r="1.3" fill="#888"/><circle cx="464.3" cy="116.6" r="1.3" fill="#888"/><circle cx="465.6" cy="99.4" r="1.3" fill="#888"/><circle cx="466.2" cy="96.8" r="1.3" fill="#888"/><circle cx="467.0" cy="96.8" r="1.3" fill="#888"/><circle cx="467.5" cy="99.2" r="1.3" fill="#888"/><circle cx="468.2" cy="98.3" r="1.3" fill="#888"/><circle cx="468.7" cy="98.0" r="1.3" fill="#888"/><circle cx="469.2" cy="100.0" r="1.3" fill="#888"/><circle cx="469.8" cy="97.0" r="1.3" fill="#888"/><circle cx="470.3" cy="96.3" r="1.3" fill="#888"/><circle cx="470.8" cy="104.5" r="1.3" fill="#888"/><circle cx="471.8" cy="92.2" r="1.3" fill="#888"/><circle cx="472.6" cy="98.1" r="1.3" fill="#888"/><circle cx="473.1" cy="98.0" r="1.3" fill="#888"/>
<circle cx="473.6" cy="100.3" r="1.3" fill="#888"/><circle cx="474.1" cy="98.1" r="1.3" fill="#888"/><circle cx="474.6" cy="98.0" r="1.3" fill="#888"/><circle cx="475.1" cy="103.3" r="1.3" fill="#888"/><circle cx="475.7" cy="98.2" r="1.3" fill="#888"/><circle cx="476.3" cy="97.1" r="1.3" fill="#888"/><circle cx="476.9" cy="97.9" r="1.3" fill="#888"/><circle cx="477.9" cy="109.8" r="1.3" fill="#888"/><circle cx="478.6" cy="96.1" r="1.3" fill="#888"/><circle cx="479.4" cy="103.5" r="1.3" fill="#888"/><circle cx="480.1" cy="97.9" r="1.3" fill="#888"/><circle cx="481.2" cy="99.1" r="1.3" fill="#888"/><circle cx="482.0" cy="102.4" r="1.3" fill="#888"/><circle cx="482.9" cy="101.8" r="1.3" fill="#888"/><circle cx="483.5" cy="97.5" r="1.3" fill="#888"/><circle cx="484.1" cy="98.8" r="1.3" fill="#888"/><circle cx="484.9" cy="98.4" r="1.3" fill="#888"/>
<circle cx="485.4" cy="100.5" r="1.3" fill="#888"/><circle cx="486.3" cy="97.6" r="1.3" fill="#888"/><circle cx="486.8" cy="98.1" r="1.3" fill="#888"/><circle cx="487.5" cy="104.8" r="1.3" fill="#888"/><circle cx="488.1" cy="97.3" r="1.3" fill="#888"/><circle cx="488.7" cy="99.7" r="1.3" fill="#888"/><circle cx="489.1" cy="97.9" r="1.3" fill="#888"/><circle cx="489.7" cy="102.6" r="1.3" fill="#888"/><circle cx="490.4" cy="99.3" r="1.3" fill="#888"/><circle cx="491.0" cy="98.6" r="1.3" fill="#888"/><circle cx="491.7" cy="99.8" r="1.3" fill="#888"/><circle cx="492.5" cy="98.8" r="1.3" fill="#888"/><circle cx="493.2" cy="97.3" r="1.3" fill="#888"/><circle cx="493.9" cy="98.5" r="1.3" fill="#888"/><circle cx="494.6" cy="104.7" r="1.3" fill="#888"/><circle cx="495.2" cy="94.6" r="1.3" fill="#888"/><circle cx="495.9" cy="98.4" r="1.3" fill="#888"/>
<circle cx="496.9" cy="103.1" r="1.3" fill="#888"/><circle cx="498.5" cy="99.3" r="1.3" fill="#888"/><circle cx="499.1" cy="103.2" r="1.3" fill="#888"/><circle cx="500.2" cy="97.4" r="1.3" fill="#888"/><circle cx="500.9" cy="99.8" r="1.3" fill="#888"/><circle cx="501.5" cy="101.3" r="1.3" fill="#888"/><circle cx="502.1" cy="97.8" r="1.3" fill="#888"/><circle cx="502.6" cy="101.2" r="1.3" fill="#888"/><circle cx="505.6" cy="110.0" r="1.3" fill="#888"/><circle cx="507.6" cy="127.0" r="1.3" fill="#888"/><circle cx="509.6" cy="98.5" r="1.3" fill="#888"/><circle cx="510.6" cy="103.4" r="1.3" fill="#888"/><circle cx="512.3" cy="105.7" r="1.3" fill="#888"/><circle cx="513.4" cy="100.3" r="1.3" fill="#888"/><circle cx="514.4" cy="101.4" r="1.3" fill="#888"/><circle cx="515.4" cy="101.4" r="1.3" fill="#888"/><circle cx="516.7" cy="105.3" r="1.3" fill="#888"/>
<circle cx="518.9" cy="206.9" r="1.3" fill="#888"/><circle cx="520.7" cy="103.1" r="1.3" fill="#888"/><circle cx="521.5" cy="111.8" r="1.3" fill="#888"/><circle cx="522.7" cy="106.7" r="1.3" fill="#888"/><circle cx="526.3" cy="109.4" r="1.3" fill="#888"/><circle cx="528.4" cy="130.6" r="1.3" fill="#888"/><circle cx="531.1" cy="102.5" r="1.3" fill="#888"/><circle cx="531.9" cy="99.1" r="1.3" fill="#888"/><circle cx="533.3" cy="114.1" r="1.3" fill="#888"/><circle cx="534.4" cy="126.2" r="1.3" fill="#888"/><circle cx="536.2" cy="130.6" r="1.3" fill="#888"/><circle cx="539.0" cy="128.6" r="1.3" fill="#888"/><circle cx="543.7" cy="137.4" r="1.3" fill="#888"/><circle cx="546.2" cy="98.9" r="1.3" fill="#888"/><circle cx="547.5" cy="99.4" r="1.3" fill="#888"/><circle cx="548.4" cy="99.1" r="1.3" fill="#888"/><circle cx="548.9" cy="124.4" r="1.3" fill="#888"/>
<circle cx="550.0" cy="105.2" r="1.3" fill="#888"/><circle cx="552.8" cy="103.0" r="1.3" fill="#888"/><circle cx="555.3" cy="96.6" r="1.3" fill="#888"/><circle cx="556.5" cy="98.0" r="1.3" fill="#888"/><circle cx="557.8" cy="97.8" r="1.3" fill="#888"/><circle cx="559.3" cy="102.2" r="1.3" fill="#888"/><circle cx="560.1" cy="110.7" r="1.3" fill="#888"/><circle cx="561.2" cy="104.2" r="1.3" fill="#888"/><circle cx="562.0" cy="108.1" r="1.3" fill="#888"/><circle cx="562.6" cy="98.7" r="1.3" fill="#888"/><circle cx="563.3" cy="99.7" r="1.3" fill="#888"/><circle cx="563.9" cy="99.7" r="1.3" fill="#888"/><circle cx="564.5" cy="98.3" r="1.3" fill="#888"/><circle cx="565.1" cy="98.1" r="1.3" fill="#888"/><circle cx="565.5" cy="106.2" r="1.3" fill="#888"/><circle cx="566.3" cy="99.3" r="1.3" fill="#888"/><circle cx="566.9" cy="100.2" r="1.3" fill="#888"/>
<circle cx="567.4" cy="101.1" r="1.3" fill="#888"/><circle cx="568.0" cy="99.7" r="1.3" fill="#888"/><circle cx="568.8" cy="100.9" r="1.3" fill="#888"/><circle cx="569.4" cy="101.1" r="1.3" fill="#888"/><circle cx="570.3" cy="101.9" r="1.3" fill="#888"/><circle cx="570.9" cy="100.8" r="1.3" fill="#888"/><circle cx="571.5" cy="98.6" r="1.3" fill="#888"/><circle cx="572.1" cy="101.2" r="1.3" fill="#888"/><circle cx="573.0" cy="99.3" r="1.3" fill="#888"/><circle cx="573.6" cy="99.4" r="1.3" fill="#888"/><circle cx="574.3" cy="130.7" r="1.3" fill="#888"/><circle cx="576.7" cy="127.9" r="1.3" fill="#888"/><circle cx="580.1" cy="147.0" r="1.3" fill="#888"/><circle cx="583.0" cy="105.7" r="1.3" fill="#888"/><circle cx="585.3" cy="102.6" r="1.3" fill="#888"/><circle cx="586.7" cy="101.4" r="1.3" fill="#888"/><circle cx="587.5" cy="100.7" r="1.3" fill="#888"/>
<circle cx="588.5" cy="100.1" r="1.3" fill="#888"/><circle cx="589.4" cy="96.7" r="1.3" fill="#888"/><circle cx="590.4" cy="98.4" r="1.3" fill="#888"/><circle cx="591.1" cy="98.0" r="1.3" fill="#888"/><circle cx="591.8" cy="97.8" r="1.3" fill="#888"/><circle cx="592.4" cy="97.0" r="1.3" fill="#888"/><circle cx="593.4" cy="106.8" r="1.3" fill="#888"/><circle cx="594.5" cy="101.6" r="1.3" fill="#888"/><circle cx="596.0" cy="126.4" r="1.3" fill="#888"/><circle cx="598.5" cy="122.9" r="1.3" fill="#888"/><circle cx="601.4" cy="154.7" r="1.3" fill="#888"/><circle cx="604.7" cy="103.9" r="1.3" fill="#888"/><circle cx="605.7" cy="102.6" r="1.3" fill="#888"/><circle cx="606.6" cy="98.8" r="1.3" fill="#888"/><circle cx="607.8" cy="101.6" r="1.3" fill="#888"/><circle cx="609.3" cy="117.9" r="1.3" fill="#888"/><circle cx="610.9" cy="114.6" r="1.3" fill="#888"/>
<circle cx="613.7" cy="69.4" r="1.3" fill="#888"/><circle cx="614.9" cy="99.1" r="1.3" fill="#888"/><circle cx="615.5" cy="105.5" r="1.3" fill="#888"/><circle cx="617.2" cy="102.6" r="1.3" fill="#888"/><circle cx="617.7" cy="98.6" r="1.3" fill="#888"/><circle cx="618.6" cy="109.3" r="1.3" fill="#888"/><circle cx="621.1" cy="107.7" r="1.3" fill="#888"/><circle cx="622.2" cy="127.2" r="1.3" fill="#888"/><circle cx="623.0" cy="103.3" r="1.3" fill="#888"/><circle cx="624.0" cy="168.8" r="1.3" fill="#888"/><circle cx="625.8" cy="100.3" r="1.3" fill="#888"/><circle cx="626.3" cy="99.0" r="1.3" fill="#888"/><circle cx="627.4" cy="101.8" r="1.3" fill="#888"/><circle cx="629.5" cy="98.4" r="1.3" fill="#888"/><circle cx="630.7" cy="99.4" r="1.3" fill="#888"/><circle cx="631.4" cy="101.6" r="1.3" fill="#888"/><circle cx="633.0" cy="100.3" r="1.3" fill="#888"/>
<circle cx="634.4" cy="102.6" r="1.3" fill="#888"/><circle cx="635.5" cy="108.1" r="1.3" fill="#888"/><circle cx="638.5" cy="102.1" r="1.3" fill="#888"/><circle cx="639.6" cy="102.4" r="1.3" fill="#888"/><circle cx="641.5" cy="101.1" r="1.3" fill="#888"/><circle cx="642.4" cy="99.0" r="1.3" fill="#888"/><circle cx="643.1" cy="100.6" r="1.3" fill="#888"/><circle cx="644.4" cy="99.0" r="1.3" fill="#888"/><circle cx="645.1" cy="103.4" r="1.3" fill="#888"/><circle cx="645.8" cy="98.5" r="1.3" fill="#888"/><circle cx="646.3" cy="99.2" r="1.3" fill="#888"/><circle cx="647.1" cy="108.4" r="1.3" fill="#888"/><circle cx="648.4" cy="99.9" r="1.3" fill="#888"/><circle cx="649.0" cy="100.7" r="1.3" fill="#888"/><circle cx="95.2" cy="99.2" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="110.1" cy="98.4" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="153.7" cy="97.6" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="211.3" cy="96.9" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="220.8" cy="97.5" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="249.9" cy="96.9" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="259.4" cy="97.7" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="279.9" cy="96.6" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="289.4" cy="97.3" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="297.5" cy="97.2" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="302.1" cy="97.4" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="322.6" cy="96.8" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="333.4" cy="97.0" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="344.4" cy="97.4" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="375.6" cy="97.3" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="388.5" cy="96.4" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="404.2" cy="97.9" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="409.5" cy="97.5" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="440.8" cy="97.6" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="457.3" cy="98.6" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="470.4" cy="96.9" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="485.1" cy="97.1" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/>
<circle cx="495.4" cy="97.1" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="501.4" cy="98.2" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="548.2" cy="98.5" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="565.2" cy="97.4" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="573.3" cy="97.9" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="591.0" cy="97.3" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="622.7" cy="98.1" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><circle cx="644.6" cy="97.6" r="3.5" fill="#4a7bd0" fill-opacity="0.8" stroke="#4a7bd0"/><text x="360" y="258.0" font-size="12" text-anchor="middle">게스트 시간 (0 ~ 7.9 s)</text><text x="80" y="20" font-size="12">회색: 모든 샘플의 offset 추정 잔차 (1/6만 표시, -12 ~ +4 ms로 잘림)</text>
<text x="80" y="38" font-size="12">파랑: 100개마다 RTT 최소 샘플 1개 — 직선 피팅 기울기 = drift</text>
</svg>
```

그림 7 — QEMU 게스트(idle) 측정의 offset 추정 잔차(실측). 회색은 개별 샘플 — 링크 지연 비대칭 때문에 거의 다 아래쪽(음수)으로 수 ms씩 튄다. 파랑(창마다 최소 RTT 샘플)은 0 근처에 붙어 있다.

---

## 8. Heartbeat와 failsafe — "Linux가 죽으면 모터는?"

### 8.1 설계 원칙

- **감시는 아래층이 한다.** MCU가 Linux의 heartbeat를 기다린다. Linux가 "나 이제 죽는다"를 보내 주길 기대하지 않는다 — panic, 전원 브라운아웃, deadlock은 예고 없이 온다.
- **deadline = 주기 × k + 지터 여유.** heartbeat 주기 T, 정상 상태 최대 간격 J를 측정하고, deadline > J (여유 포함), 그리고 "Linux가 죽은 뒤 안전 상태 도달까지 허용 시간(위험 분석에서 나옴)" 미만이어야 한다. 두 조건이 충돌하면 Linux 쪽 지터를 줄이거나(N2) 기능을 MCU로 더 내려야 한다.
- **heartbeat는 "살아 있음"이 아니라 "건강함"을 증명해야 한다.** 별도 스레드가 무조건 보내는 heartbeat는 메인 제어 스레드가 멈춰도 계속 간다. heartbeat를 메인 루프 끝에서 보내거나, 각 하위 기능의 진행 카운터를 묶어 보낸다(L1의 systemd watchdog `WATCHDOG=1`과 같은 원칙).
- **SAFE는 래치(latch)한다.** 다시 RUN으로 가는 것은 원인 기록 + 명시적 re-arm 명령(사람 또는 상위 로직의 판단)으로. 자동 복귀는 §8.4에서 보듯 위험하다.
- **safe state가 무엇인지는 기구가 정한다.** 모터 PWM off만으로 충분한가(관성으로 돌아감), 능동 제동이 필요한가, 수직축은 브레이크를 물려야 하는가(중력으로 떨어짐), 드론은 "끄기"가 곧 추락이다(그래서 드론의 failsafe는 "자율 착륙"). 

### 예제 8 — 실행 1: deadline 30 ms (heartbeat 10 ms 주기)

무엇을 확인하나: deadline을 주기의 3배로 빠듯하게 잡으면 어떻게 되나. 게스트에서 heartbeat를 10 ms로 보내면서 SIGSTOP → SIGCONT → SIGKILL → 재시작 → 게스트 부하 → `chrt -f 50` 재시작 → 커널 panic(`echo c > /proc/sysrq-trigger`)을 차례로 일으킨다. 이 실행에서 마커는 게스트 콘솔 출력에 호스트 시각을 붙였다(결과적으로 콘솔 경로 지연 때문에 수십 ms 늦게 찍혀서, 실행 2에서는 마커도 링크로 보내도록 바꿨다).

```text
MCU t0_host_ns=300626568064660 deadline=30 ms
  34919.4 ms RUN  (20 good HB)
  36640.0 ms SAFE (last HB 30.5 ms ago, crcerr=0)
  36809.3 ms RUN  (20 good HB)
  37759.6 ms SAFE (last HB 30.7 ms ago, crcerr=0)
  38779.9 ms RUN  (20 good HB)
  39701.7 ms SAFE (last HB 42.6 ms ago, crcerr=0)
  39862.1 ms RUN  (20 good HB)
  39921.0 ms SAFE (last HB 30.3 ms ago, crcerr=0)
  40088.9 ms RUN  (20 good HB)
  41809.8 ms SAFE (last HB 30.9 ms ago, crcerr=0)
  43029.4 ms RUN  (20 good HB)
  44890.7 ms SAFE (last HB 31.6 ms ago, crcerr=0)
  45059.3 ms RUN  (20 good HB)
  47984.0 ms SAFE (last HB 33.1 ms ago, crcerr=0)
  48149.0 ms RUN  (20 good HB)
  48587.1 ms SAFE (last HB 58.2 ms ago, crcerr=0)
  48746.5 ms RUN  (20 good HB)
  50797.9 ms SAFE (last HB 30.9 ms ago, crcerr=0)
  50962.3 ms RUN  (20 good HB)
  51109.3 ms SAFE (last HB 30.0 ms ago, crcerr=0)
  51399.6 ms RUN  (20 good HB)
  54880.7 ms SAFE (last HB 31.1 ms ago, crcerr=0)
  55051.3 ms RUN  (20 good HB)
  56279.8 ms SAFE (last HB 30.3 ms ago, crcerr=0)
link closed at 57608.6 ms
```

```text
   37782.5 ms CONSOLE MARK hb_sigstop
   38758.8 ms CONSOLE MARK hb_sigcont
   41783.8 ms CONSOLE MARK hb_sigkill
   45844.0 ms CONSOLE MARK load4_nice0
   51150.2 ms CONSOLE MARK load4_fifo50
   56243.0 ms CONSOLE MARK panic
   56258.6 ms CONSOLE [   55.173414] Kernel panic - not syncing: sysrq triggered crash
```

이 실행의 heartbeat 간격 통계(같은 CSV, 2035개 간격): `gaps ms p50 9.9 p99 23.8 max 1060.1 >30: 14`.

출력에서 볼 것: SAFE가 12번 들어갔는데, 일부러 일으킨 고장(STOP, KILL, 부하 구간, FIFO 전환 때의 KILL, panic)으로 설명되지 않는 SAFE가 **5번**(36640, 39701, 39921, 44890, 54880) 있다. 장애 없이 그냥 공유 VM의 지터만으로 30 ms를 넘은 것이다. 실제 로봇이라면 **"이유 없이 가끔 서는 로봇"** — 현장에서 가장 미움받는 버그다. deadline은 측정된 정상 지터 분포(p99가 아니라 max, 그리고 여유)를 보고 정해야 한다.

### 예제 9 — 실행 2: deadline 50 ms, 마커를 링크로

무엇을 확인하나: deadline을 50 ms로 넓히고, 실험 마커를 heartbeat와 같은 링크로 보내 MCU 시계로 찍는다. 그래서 "고장 주입 → MCU가 알아챔"의 간격을 같은 시계에서 잴 수 있다.

```sh
#!/bin/sh
# n4_guest.sh (실행 2) — runs inside the QEMU guest. Markers go over the link (MCU timestamps them).
D=/dev/ttyAMA1; N=/bin/n4_linux
mk() { $N mark $D $1; }   # 1=sigstop 2=sigcont 3=sigkill 4=restart 5=load 6=fifo 7=panic
$N hb $D 10 & H=$!; sleep 3
mk 1; kill -STOP $H; sleep 1
mk 2; kill -CONT $H; sleep 3
mk 3; kill -9 $H; sleep 1
mk 4; $N hb $D 10 & H=$!; sleep 3
mk 5; for i in 1 2 3 4 5 6 7 8; do sh -c 'while :; do :; done' & done; sleep 5
kill -9 $H; mk 6; chrt -f 50 $N hb $D 10 & H=$!; sleep 5
mk 7; echo c > /proc/sysrq-trigger
```

```text
MCU t0_host_ns=300720195320693 deadline=50 ms
   3000.7 ms RUN  (20 good HB)
   5806.5 ms MARK 1
   5862.1 ms SAFE (last HB 50.1 ms ago, crcerr=0)
   6850.0 ms MARK 2
   6895.7 ms RUN  (20 good HB)
   9922.6 ms MARK 3
   9971.2 ms SAFE (last HB 50.7 ms ago, crcerr=0)
  10972.5 ms MARK 4
  11198.3 ms RUN  (20 good HB)
  14026.7 ms MARK 5
  17164.8 ms SAFE (last HB 51.0 ms ago, crcerr=0)
  17524.7 ms RUN  (20 good HB)
  17969.3 ms SAFE (last HB 50.1 ms ago, crcerr=0)
  18129.4 ms RUN  (20 good HB)
  19320.1 ms SAFE (last HB 50.8 ms ago, crcerr=0)
  19419.9 ms MARK 6
  19823.8 ms RUN  (20 good HB)
  24738.3 ms MARK 7
  24833.8 ms SAFE (last HB 50.2 ms ago, crcerr=0)
link closed at 26143.7 ms
```

```text
  24794.6 ms CONSOLE [   24.347363] Kernel panic - not syncing: sysrq triggered crash
```

(측정 시작 시 VM load average 12.83 — `uptime` 출력 `load average: 12.83, 10.82, 6.14`.)

출력에서 볼 것 — 고장별 반응:

| 고장 | 주입 (MCU 시계) | MCU SAFE | 마지막 HB 이후 | 해석 |
|---|---|---|---|---|
| SIGSTOP (앱 정지, 프로세스는 살아 있음) | 5806.5 ms | 5862.1 ms | 50.1 ms | 감지 = deadline + 1 ms tick 이내 |
| SIGKILL (앱 종료) | 9922.6 ms | 9971.2 ms | 50.7 ms | 동일 |
| 커널 panic | 24738.3 ms (마커), panic 출력 24794.6 ms | 24833.8 ms | 50.2 ms | panic 출력 후 39 ms 만에 SAFE |
| 부하 (바쁜 루프 8개, HB는 nice 0) | 14026.7 ms | 17164.8 / 17969.3 / 19320.1 ms | 50~51 ms | **장애가 아니라 "굶주림"도 SAFE를 일으킨다** |
| 부하 유지 + HB를 `chrt -f 50`으로 | 19419.9 ms | 없음 (5 s 동안) | — | RT 우선순위면 같은 부하에서도 간격 유지 |

세 가지 고장 모두에서 **"마지막 HB 이후 50.1~50.7 ms"** 에 SAFE가 걸렸다. 즉 감지 시간은 Linux 쪽 상태와 무관하게 MCU의 deadline + tick으로 결정된다 — 이게 이 구조의 핵심 가치다. Linux가 panic하든, 앱이 멈추든, 전원이 나가든, MCU의 반응 시간은 MCU가 정한다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<rect x="195.3" y="50" width="23.9" height="200" fill="#d0564a" fill-opacity="0.12" stroke="none"/><rect x="290.1" y="50" width="28.3" height="200" fill="#d0564a" fill-opacity="0.12" stroke="none"/><rect x="456.1" y="50" width="8.3" height="200" fill="#d0564a" fill-opacity="0.12" stroke="none"/><rect x="474.7" y="50" width="3.7" height="200" fill="#d0564a" fill-opacity="0.12" stroke="none"/><rect x="505.8" y="50" width="11.6" height="200" fill="#d0564a" fill-opacity="0.12" stroke="none"/><rect x="633.1" y="50" width="26.9" height="200" fill="#d0564a" fill-opacity="0.12" stroke="none"/><line x1="60" y1="250" x2="660" y2="250" stroke="currentColor"/><line x1="60" y1="50" x2="60" y2="250" stroke="currentColor"/><text x="54" y="254.0" font-size="12" text-anchor="end">0</text><text x="54" y="191.5" font-size="12" text-anchor="end">50</text>
<text x="54" y="129.0" font-size="12" text-anchor="end">100</text><text x="54" y="66.5" font-size="12" text-anchor="end">150</text><text x="60.0" y="266" font-size="12" text-anchor="middle">0s</text><text x="175.4" y="266" font-size="12" text-anchor="middle">5s</text><text x="290.8" y="266" font-size="12" text-anchor="middle">10s</text><text x="406.2" y="266" font-size="12" text-anchor="middle">15s</text><text x="521.5" y="266" font-size="12" text-anchor="middle">20s</text><text x="636.9" y="266" font-size="12" text-anchor="middle">25s</text><line x1="60" y1="187.5" x2="660" y2="187.5" stroke="#d0564a" stroke-dasharray="5,4"/><text x="656" y="182.5" font-size="12" text-anchor="end">deadline 50 ms</text><line x1="125.8" y1="250" x2="125.8" y2="225.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="128.1" y1="250" x2="128.1" y2="235.9" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="130.4" y1="250" x2="130.4" y2="236.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="132.7" y1="250" x2="132.7" y2="233.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="135.0" y1="250" x2="135.0" y2="231.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="137.3" y1="250" x2="137.3" y2="236.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="139.6" y1="250" x2="139.6" y2="230.1" stroke="#4a7bd0" stroke-width="1.6"/><line x1="141.9" y1="250" x2="141.9" y2="234.4" stroke="#4a7bd0" stroke-width="1.6"/><line x1="144.2" y1="250" x2="144.2" y2="234.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="146.5" y1="250" x2="146.5" y2="234.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="148.8" y1="250" x2="148.8" y2="234.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="151.2" y1="250" x2="151.2" y2="236.7" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="153.5" y1="250" x2="153.5" y2="235.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="155.8" y1="250" x2="155.8" y2="235.4" stroke="#4a7bd0" stroke-width="1.6"/><line x1="158.1" y1="250" x2="158.1" y2="235.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="160.4" y1="250" x2="160.4" y2="237.1" stroke="#4a7bd0" stroke-width="1.6"/><line x1="162.7" y1="250" x2="162.7" y2="237.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="165.0" y1="250" x2="165.0" y2="237.1" stroke="#4a7bd0" stroke-width="1.6"/><line x1="167.3" y1="250" x2="167.3" y2="234.6" stroke="#4a7bd0" stroke-width="1.6"/><line x1="169.6" y1="250" x2="169.6" y2="231.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="171.9" y1="250" x2="171.9" y2="230.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="174.2" y1="250" x2="174.2" y2="236.9" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="176.5" y1="250" x2="176.5" y2="236.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="178.8" y1="250" x2="178.8" y2="236.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="181.2" y1="250" x2="181.2" y2="236.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="183.5" y1="250" x2="183.5" y2="233.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="185.8" y1="250" x2="185.8" y2="236.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="188.1" y1="250" x2="188.1" y2="237.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="190.4" y1="250" x2="190.4" y2="237.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="192.7" y1="250" x2="192.7" y2="236.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="195.0" y1="250" x2="195.0" y2="235.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="218.1" y1="250" x2="218.1" y2="50.0" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="220.4" y1="250" x2="220.4" y2="237.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="222.7" y1="250" x2="222.7" y2="229.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="225.0" y1="250" x2="225.0" y2="224.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="227.3" y1="250" x2="227.3" y2="235.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="229.6" y1="250" x2="229.6" y2="227.6" stroke="#4a7bd0" stroke-width="1.6"/><line x1="231.9" y1="250" x2="231.9" y2="237.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="234.2" y1="250" x2="234.2" y2="231.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="236.5" y1="250" x2="236.5" y2="223.6" stroke="#4a7bd0" stroke-width="1.6"/><line x1="238.8" y1="250" x2="238.8" y2="236.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="241.2" y1="250" x2="241.2" y2="237.2" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="243.5" y1="250" x2="243.5" y2="236.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="245.8" y1="250" x2="245.8" y2="236.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="248.1" y1="250" x2="248.1" y2="236.6" stroke="#4a7bd0" stroke-width="1.6"/><line x1="250.4" y1="250" x2="250.4" y2="236.6" stroke="#4a7bd0" stroke-width="1.6"/><line x1="252.7" y1="250" x2="252.7" y2="236.4" stroke="#4a7bd0" stroke-width="1.6"/><line x1="255.0" y1="250" x2="255.0" y2="216.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="257.3" y1="250" x2="257.3" y2="237.1" stroke="#4a7bd0" stroke-width="1.6"/><line x1="259.6" y1="250" x2="259.6" y2="237.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="261.9" y1="250" x2="261.9" y2="234.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="264.2" y1="250" x2="264.2" y2="235.1" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="266.5" y1="250" x2="266.5" y2="237.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="268.8" y1="250" x2="268.8" y2="237.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="271.2" y1="250" x2="271.2" y2="237.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="273.5" y1="250" x2="273.5" y2="227.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="275.8" y1="250" x2="275.8" y2="237.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="278.1" y1="250" x2="278.1" y2="237.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="280.4" y1="250" x2="280.4" y2="236.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="282.7" y1="250" x2="282.7" y2="237.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="285.0" y1="250" x2="285.0" y2="237.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="287.3" y1="250" x2="287.3" y2="229.5" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="289.6" y1="250" x2="289.6" y2="233.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="315.0" y1="250" x2="315.0" y2="50.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="317.3" y1="250" x2="317.3" y2="235.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="319.6" y1="250" x2="319.6" y2="237.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="321.9" y1="250" x2="321.9" y2="220.4" stroke="#4a7bd0" stroke-width="1.6"/><line x1="324.2" y1="250" x2="324.2" y2="232.4" stroke="#4a7bd0" stroke-width="1.6"/><line x1="326.5" y1="250" x2="326.5" y2="237.1" stroke="#4a7bd0" stroke-width="1.6"/><line x1="328.8" y1="250" x2="328.8" y2="236.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="331.2" y1="250" x2="331.2" y2="236.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="333.5" y1="250" x2="333.5" y2="235.9" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="335.8" y1="250" x2="335.8" y2="229.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="338.1" y1="250" x2="338.1" y2="234.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="340.4" y1="250" x2="340.4" y2="231.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="342.7" y1="250" x2="342.7" y2="236.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="345.0" y1="250" x2="345.0" y2="213.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="347.3" y1="250" x2="347.3" y2="235.6" stroke="#4a7bd0" stroke-width="1.6"/><line x1="349.6" y1="250" x2="349.6" y2="235.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="351.9" y1="250" x2="351.9" y2="236.1" stroke="#4a7bd0" stroke-width="1.6"/><line x1="354.2" y1="250" x2="354.2" y2="236.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="356.5" y1="250" x2="356.5" y2="236.6" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="358.8" y1="250" x2="358.8" y2="237.1" stroke="#4a7bd0" stroke-width="1.6"/><line x1="361.2" y1="250" x2="361.2" y2="230.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="363.5" y1="250" x2="363.5" y2="233.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="365.8" y1="250" x2="365.8" y2="236.4" stroke="#4a7bd0" stroke-width="1.6"/><line x1="368.1" y1="250" x2="368.1" y2="237.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="370.4" y1="250" x2="370.4" y2="230.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="372.7" y1="250" x2="372.7" y2="235.4" stroke="#4a7bd0" stroke-width="1.6"/><line x1="375.0" y1="250" x2="375.0" y2="228.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="377.3" y1="250" x2="377.3" y2="236.1" stroke="#4a7bd0" stroke-width="1.6"/><line x1="379.6" y1="250" x2="379.6" y2="237.2" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="381.9" y1="250" x2="381.9" y2="236.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="384.2" y1="250" x2="384.2" y2="233.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="386.5" y1="250" x2="386.5" y2="227.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="388.8" y1="250" x2="388.8" y2="227.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="391.2" y1="250" x2="391.2" y2="226.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="393.5" y1="250" x2="393.5" y2="223.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="395.8" y1="250" x2="395.8" y2="224.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="398.1" y1="250" x2="398.1" y2="226.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="400.4" y1="250" x2="400.4" y2="232.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="402.7" y1="250" x2="402.7" y2="228.5" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="405.0" y1="250" x2="405.0" y2="233.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="407.3" y1="250" x2="407.3" y2="230.6" stroke="#4a7bd0" stroke-width="1.6"/><line x1="409.6" y1="250" x2="409.6" y2="233.1" stroke="#4a7bd0" stroke-width="1.6"/><line x1="411.9" y1="250" x2="411.9" y2="232.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="414.2" y1="250" x2="414.2" y2="224.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="416.5" y1="250" x2="416.5" y2="232.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="418.8" y1="250" x2="418.8" y2="232.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="421.2" y1="250" x2="421.2" y2="232.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="423.5" y1="250" x2="423.5" y2="233.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="425.8" y1="250" x2="425.8" y2="233.1" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="428.1" y1="250" x2="428.1" y2="232.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="430.4" y1="250" x2="430.4" y2="233.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="432.7" y1="250" x2="432.7" y2="229.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="435.0" y1="250" x2="435.0" y2="232.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="437.3" y1="250" x2="437.3" y2="232.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="439.6" y1="250" x2="439.6" y2="232.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="441.9" y1="250" x2="441.9" y2="233.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="444.2" y1="250" x2="444.2" y2="232.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="446.5" y1="250" x2="446.5" y2="232.6" stroke="#4a7bd0" stroke-width="1.6"/><line x1="448.8" y1="250" x2="448.8" y2="220.0" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="451.2" y1="250" x2="451.2" y2="223.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="453.5" y1="250" x2="453.5" y2="75.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="455.8" y1="250" x2="455.8" y2="224.4" stroke="#4a7bd0" stroke-width="1.6"/><line x1="458.1" y1="250" x2="458.1" y2="84.1" stroke="#4a7bd0" stroke-width="1.6"/><line x1="460.4" y1="250" x2="460.4" y2="123.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="462.7" y1="250" x2="462.7" y2="185.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="465.0" y1="250" x2="465.0" y2="196.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="469.6" y1="250" x2="469.6" y2="50.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="471.9" y1="250" x2="471.9" y2="116.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="474.2" y1="250" x2="474.2" y2="176.8" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="478.8" y1="250" x2="478.8" y2="60.4" stroke="#4a7bd0" stroke-width="1.6"/><line x1="481.2" y1="250" x2="481.2" y2="135.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="483.5" y1="250" x2="483.5" y2="232.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="485.8" y1="250" x2="485.8" y2="232.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="488.1" y1="250" x2="488.1" y2="224.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="490.4" y1="250" x2="490.4" y2="221.4" stroke="#4a7bd0" stroke-width="1.6"/><line x1="492.7" y1="250" x2="492.7" y2="226.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="495.0" y1="250" x2="495.0" y2="225.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="497.3" y1="250" x2="497.3" y2="233.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="499.6" y1="250" x2="499.6" y2="231.4" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="501.9" y1="250" x2="501.9" y2="233.1" stroke="#4a7bd0" stroke-width="1.6"/><line x1="504.2" y1="250" x2="504.2" y2="230.4" stroke="#4a7bd0" stroke-width="1.6"/><line x1="513.5" y1="250" x2="513.5" y2="50.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="515.8" y1="250" x2="515.8" y2="234.6" stroke="#4a7bd0" stroke-width="1.6"/><line x1="518.1" y1="250" x2="518.1" y2="233.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="520.4" y1="250" x2="520.4" y2="233.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="522.7" y1="250" x2="522.7" y2="233.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="525.0" y1="250" x2="525.0" y2="235.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="527.3" y1="250" x2="527.3" y2="235.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="529.6" y1="250" x2="529.6" y2="235.9" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="531.9" y1="250" x2="531.9" y2="235.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="534.2" y1="250" x2="534.2" y2="234.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="536.5" y1="250" x2="536.5" y2="233.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="538.8" y1="250" x2="538.8" y2="235.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="541.2" y1="250" x2="541.2" y2="235.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="543.5" y1="250" x2="543.5" y2="201.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="545.8" y1="250" x2="545.8" y2="236.4" stroke="#4a7bd0" stroke-width="1.6"/><line x1="548.1" y1="250" x2="548.1" y2="236.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="550.4" y1="250" x2="550.4" y2="233.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="552.7" y1="250" x2="552.7" y2="235.3" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="555.0" y1="250" x2="555.0" y2="233.3" stroke="#4a7bd0" stroke-width="1.6"/><line x1="557.3" y1="250" x2="557.3" y2="230.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="559.6" y1="250" x2="559.6" y2="235.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="561.9" y1="250" x2="561.9" y2="237.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="564.2" y1="250" x2="564.2" y2="236.6" stroke="#4a7bd0" stroke-width="1.6"/><line x1="566.5" y1="250" x2="566.5" y2="237.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="568.8" y1="250" x2="568.8" y2="235.2" stroke="#4a7bd0" stroke-width="1.6"/><line x1="571.2" y1="250" x2="571.2" y2="236.6" stroke="#4a7bd0" stroke-width="1.6"/><line x1="573.5" y1="250" x2="573.5" y2="236.6" stroke="#4a7bd0" stroke-width="1.6"/><line x1="575.8" y1="250" x2="575.8" y2="234.9" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="578.1" y1="250" x2="578.1" y2="235.6" stroke="#4a7bd0" stroke-width="1.6"/><line x1="580.4" y1="250" x2="580.4" y2="236.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="582.7" y1="250" x2="582.7" y2="235.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="585.0" y1="250" x2="585.0" y2="236.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="587.3" y1="250" x2="587.3" y2="235.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="589.6" y1="250" x2="589.6" y2="237.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="591.9" y1="250" x2="591.9" y2="236.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="594.2" y1="250" x2="594.2" y2="233.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="596.5" y1="250" x2="596.5" y2="236.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="598.8" y1="250" x2="598.8" y2="236.4" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="601.2" y1="250" x2="601.2" y2="236.7" stroke="#4a7bd0" stroke-width="1.6"/><line x1="603.5" y1="250" x2="603.5" y2="236.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="605.8" y1="250" x2="605.8" y2="236.6" stroke="#4a7bd0" stroke-width="1.6"/><line x1="608.1" y1="250" x2="608.1" y2="236.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="610.4" y1="250" x2="610.4" y2="235.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="612.7" y1="250" x2="612.7" y2="236.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="615.0" y1="250" x2="615.0" y2="236.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="617.3" y1="250" x2="617.3" y2="233.9" stroke="#4a7bd0" stroke-width="1.6"/><line x1="619.6" y1="250" x2="619.6" y2="237.0" stroke="#4a7bd0" stroke-width="1.6"/><line x1="621.9" y1="250" x2="621.9" y2="236.9" stroke="#4a7bd0" stroke-width="1.6"/>
<line x1="624.2" y1="250" x2="624.2" y2="236.5" stroke="#4a7bd0" stroke-width="1.6"/><line x1="626.5" y1="250" x2="626.5" y2="232.6" stroke="#4a7bd0" stroke-width="1.6"/><line x1="628.8" y1="250" x2="628.8" y2="235.8" stroke="#4a7bd0" stroke-width="1.6"/><line x1="631.2" y1="250" x2="631.2" y2="236.4" stroke="#4a7bd0" stroke-width="1.6"/><line x1="194.0" y1="44" x2="194.0" y2="250" stroke="#888" stroke-dasharray="2,3"/><text x="194.0" y="38" font-size="12" text-anchor="middle">STOP</text><line x1="218.1" y1="44" x2="218.1" y2="250" stroke="#888" stroke-dasharray="2,3"/><text x="218.1" y="24" font-size="12" text-anchor="middle">CONT</text><line x1="289.0" y1="44" x2="289.0" y2="250" stroke="#888" stroke-dasharray="2,3"/><text x="289.0" y="38" font-size="12" text-anchor="middle">KILL</text><line x1="313.2" y1="44" x2="313.2" y2="250" stroke="#888" stroke-dasharray="2,3"/>
<text x="313.2" y="24" font-size="12" text-anchor="middle">restart</text><line x1="383.7" y1="44" x2="383.7" y2="250" stroke="#888" stroke-dasharray="2,3"/><text x="383.7" y="38" font-size="12" text-anchor="middle">load</text><line x1="508.2" y1="44" x2="508.2" y2="250" stroke="#888" stroke-dasharray="2,3"/><text x="508.2" y="24" font-size="12" text-anchor="middle">FIFO</text><line x1="630.9" y1="44" x2="630.9" y2="250" stroke="#888" stroke-dasharray="2,3"/><text x="630.9" y="38" font-size="12" text-anchor="middle">panic</text><text x="60" y="292" font-size="12">파랑: 100 ms 구간별 최대 HB 간격(ms, 160에서 잘림) · 빨강 띠: MCU SAFE 상태</text><text x="60" y="310" font-size="12">세로축 = HB 간격 ms, 가로축 = MCU 시계(호스트 기준) 경과 시간</text>
</svg>
```

그림 8 — 실행 2의 heartbeat 타임라인(실측). 파랑 막대 = 100 ms 구간별 최대 HB 간격, 빨간 띠 = MCU SAFE 구간, 점선 = 주입 시점. 부하 구간(load)에서만 정상 동작 중에 간격이 deadline을 넘었고, FIFO로 올린 뒤에는 사라졌다.

### 예제 10 — 구간별 heartbeat 간격과 "따라잡기 폭주"

무엇을 확인하나: 실행 2의 HB 도착 CSV를 마커로 구간을 나눠 간격 분포를 보고, SIGCONT 직후에 무슨 일이 생기는지 본다.

```python
import re
marks={}
for l in open("n4_mcu_hb50.log"):
    m=re.match(r"\s*([\d.]+) ms MARK (\d)",l)
    if m: marks[int(m.group(2))]=float(m.group(1))
hb=[(int(l.split(",")[0]),float(l.split(",")[1])) for l in open("n4_hb50.csv")]
t=[x[1] for x in hb]
ph=[("normal",3000,marks[1]),("after CONT",marks[2],marks[3]),("restarted",marks[4]+300,marks[5]),("load nice0",marks[5],marks[6]),("load FIFO50",marks[6]+500,marks[7])]
for name,a,b in ph:
    tt=[x for x in t if a<=x<b]; g=sorted(y-x for x,y in zip(tt,tt[1:]))
    n=len(g); print(f"{name:12s} n={n:4d} gap p50={g[n//2]:.1f} p99={g[int(.99*n)]:.1f} max={g[-1]:.1f} >50ms={sum(x>50 for x in g)}")
after=[x for x in t if marks[2]<=x<marks[2]+60]
print("first HB after CONT at +%.1f ms, count within 60ms: %d"%(after[0]-marks[2],len(after)))
print("last HB before panic mark %.1f ms before"%(marks[7]-max(x for x in t if x<marks[7])))
```

```text
normal       n= 280 gap p50=10.0 p99=15.0 max=16.0 >50ms=0
after CONT   n= 410 gap p50=9.8 p99=17.9 max=27.1 >50ms=0
restarted    n= 274 gap p50=10.0 p99=17.2 max=29.0 >50ms=0
load nice0   n= 524 gap p50=7.7 p99=101.2 max=160.0 >50ms=10
load FIFO50  n= 481 gap p50=10.0 p99=13.6 max=39.0 >50ms=0
first HB after CONT at +18.9 ms, count within 60ms: 42
last HB before panic mark 4.5 ms before
```

출력에서 볼 것:

- **따라잡기 폭주(catch-up burst)**: SIGCONT 직후 60 ms 안에 heartbeat가 **42개** 도착했다(정상이면 6개). `clock_nanosleep(TIMER_ABSTIME)` 루프는 멈춰 있던 1초 동안의 "지난 마감 시각"들을 즉시 통과하면서 밀린 heartbeat를 몰아서 보낸다. 그 결과 MCU의 "정상 HB 20개면 RUN" 규칙이 **SIGCONT 후 45.7 ms 만에**(6850.0 → 6895.7 ms) 충족돼 버렸다. 1초 동안 얼어 있던 프로세스가 무슨 상태인지 확인도 안 했는데 모터가 다시 켜지는 셈이다. 대책: (1) Linux 쪽은 마감을 놓치면 밀린 주기를 건너뛰고 현재 시각 기준으로 다시 잡는다, (2) MCU 쪽은 HB 간격이 너무 짧은 것도 이상으로 보거나(윈도우 watchdog의 "너무 이른 kick" 규칙과 같다), heartbeat에 송신 시각을 넣어 검사, (3) 무엇보다 **자동 복귀를 하지 말고 래치 + 명시적 re-arm**.
- **부하에서 nice 0 heartbeat는 p99 101 ms, 최대 160 ms**. 같은 부하에서 `SCHED_FIFO 50`은 최대 39 ms. 단, FIFO로 올리면 heartbeat는 살아남지만 **정작 제어 로직(nice 0)이 굶고 있어도 heartbeat는 정상**으로 보일 수 있다 — §8.1의 "건강함 증명" 원칙이 그래서 필요하다.
- panic 직전 마지막 HB는 마커 4.5 ms 전, SAFE는 그 50 ms 뒤. panic 경로에서 Linux는 아무것도 보내지 않았다 — 보낼 수 없다. 감시는 아래층 몫이다.

### 8.2 실제 제품의 heartbeat/failsafe 체크리스트

- 양방향: Linux도 MCU heartbeat를 감시한다. MCU가 리셋되면(브라운아웃, MCU watchdog) Linux는 이를 알아채고 재동기화(HELLO, 버전 확인, 상태 재설정)한다.
- MCU 자신도 독립 watchdog(IWDG 같은 별도 클럭의 HW watchdog)으로 감시한다. MCU 펌웨어가 멈추면 HW가 리셋하고, 리셋 직후 출력은 하드웨어 기본값(pull-down)으로 안전해야 한다.
- 안전 등급 기능이면 소프트웨어 heartbeat와 별개로 **하드와이어 경로**(E-stop 회로, STO 입력)가 모터 전원을 끊는다.
- Linux 쪽에서는 heartbeat 송신을 systemd watchdog과 묶는다(L1): 앱이 건강할 때만 `sd_notify("WATCHDOG=1")`과 MCU heartbeat를 같이 보내고, 둘 중 하나가 끊기면 각자의 감시자가 대응한다. 커널 자체 hang은 SoC HW watchdog(`/dev/watchdog`)이 리셋한다(L7에서 crash 수집).

---

## 9. 버전 관리와 MCU 펌웨어 OTA

펌웨어가 두 벌이면 다음 질문에 답해야 한다.

1. **지금 어떤 조합이 돌고 있나?** → 부팅 때 MCU가 HELLO 프레임으로 `fw_version`, `proto_version`, `build_id`(git hash), `hw_rev`를 보낸다. Linux는 이를 로그와 원격 진단(텔레메트리)에 남긴다.
2. **호환되지 않으면?** → Linux는 `proto_version`이 지원 범위 밖이면 MCU를 **ARM하지 않는다**(모터 명령 금지). 그리고 업데이트 절차로 들어간다.
3. **MCU 펌웨어는 어떻게 바꾸나?**

| 방식 | 구조 | 장점 | 주의 |
|---|---|---|---|
| ROM 부트로더 (예: STM32 시스템 부트로더 USART/CAN/USB DFU) | BOOT 핀/옵션 바이트로 진입, 칩 벤더 프로토콜 | 별도 부트로더 개발 불필요 | 진입에 GPIO 제어 필요, 서명 · 롤백 없음 — 쓰는 도중 끊기면 벽돌 가능 |
| 자체/오픈소스 부트로더 + A/B 슬롯 (예: MCUboot) | 플래시를 두 슬롯으로, 서명 검증 후 교체 · 시험 부팅 · 확정 | 전원 차단 내성, 서명, 롤백 (L4와 같은 원리) | 플래시 두 배, 부트로더 자체 업데이트는 어렵다 |
| 이종 SoC: rootfs의 `/lib/firmware/<name>.elf` | Linux가 remoteproc으로 로드 | **rootfs OTA(L4)와 원자적으로 같이 바뀜** — 버전 조합 문제가 사라짐 | M 코어를 Linux 전에 띄우는 구성이면 부트로더 쪽 이미지도 관리해야 함 |

외부 MCU의 실무 흐름(L4의 RAUC/SWUpdate와 연결):

1. MCU 펌웨어 이미지(서명됨)를 Linux rootfs 번들 안에 넣는다. 즉 **"검증된 조합"을 번들 단위로 배포**한다.
2. 새 rootfs로 부팅한 뒤, 서비스가 MCU HELLO를 받아 버전을 비교한다. 다르면 모터를 SAFE에 둔 채 MCU를 부트로더 모드로 넣고(전용 명령 또는 BOOT/RESET GPIO) 이미지를 보낸다.
3. MCU 부트로더가 서명 · CRC 확인 후 새 슬롯으로 시험 부팅 → HELLO가 새 버전이면 확정, 아니면 이전 슬롯으로.
4. 그 결과까지 확인된 뒤에야 Linux 쪽 rootfs도 `mark-good`(L4)을 한다. MCU 업데이트 실패는 rootfs 롤백 사유다.
5. 함정: rootfs는 롤백됐는데 MCU는 새 버전에 남는 "반쪽 롤백". 그래서 새 Linux는 **이전 MCU 펌웨어 버전과도 동작**하도록(하위 호환 프로토콜) 만들고, 롤백된 Linux가 다시 MCU를 다운그레이드할 수 있게 한다(anti-rollback 정책과 충돌하지 않게 — L4 §13).

---

## 10. 커널 안에서는 무슨 일이 일어나나

### 10.1 외부 MCU over UART — 이 노트 실험의 경로

`write(fd, frame, n)` 한 번이 MCU에 도착하기까지:

1. 유저 `write()` → `tty_write()` → line discipline(`n_tty`; raw 모드라 가공 없음) → `uart_write()`(serial core, `drivers/tty/serial/serial_core.c`)가 바이트를 `uart_state`의 circular buffer(`xmit` kfifo)에 넣는다.
2. serial core가 드라이버의 `uart_ops->start_tx()`를 부른다. PL011이면 `pl011_start_tx()` → TX FIFO에 채우거나 DMA를 건다. FIFO가 비면 TX 인터럽트가 다시 채운다.
3. 수신은 반대: RX 인터럽트(또는 RX timeout 인터럽트 — FIFO 임계값 아래로 바이트가 멈춰 있을 때) → `pl011_int()` → `tty_insert_flip_char()`로 flip buffer에 넣고 `tty_flip_buffer_push()` → **workqueue**(`flush_to_ldisc`)가 line discipline으로 넘긴다 → `n_tty`가 읽기 대기 중인 프로세스를 깨운다(`wake_up_interruptible`) → 스케줄러가 앱을 돌린다 → `poll()`/`read()` 반환.

꼬리 지연이 생기는 지점: RX FIFO 임계값/timeout(수 바이트 시간), flip buffer → workqueue(커널 스레드 스케줄링), 앱 깨우기(스케줄링 지연, N1), 그리고 PREEMPT_RT에서는 IRQ 핸들러 자체도 스레드다. tty `low_latency` 플래그가 workqueue 단계를 줄이려던 역사적 손잡이였고 지금은 드라이버/커널 버전에 따라 의미가 다르다 — 쓰기 전에 해당 커널 소스(`drivers/tty/tty_buffer.c`)를 확인할 것. 드라이버를 직접 쓸 때 이 구조는 H 모듈(UART/serdev)에서 다시 나온다. MCU를 tty 대신 커널 안에서 다루고 싶으면 **serdev**(`drivers/tty/serdev/`) — UART에 붙은 장치를 커널 드라이버로 묶는 프레임워크 — 를 쓴다(Bluetooth HCI UART, GNSS가 이걸 쓴다).

### 10.2 이종 SoC — remoteproc + rpmsg 경로

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah9" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" font-size="13">Cortex-A (Linux)</text>
  <rect x="20" y="34" width="190" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="58" text-anchor="middle" font-size="12">앱: /dev/rpmsgN, ttyRPMSG</text>
  <rect x="20" y="90" width="190" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="114" text-anchor="middle" font-size="12">rpmsg_char / rpmsg_tty</text>
  <rect x="20" y="146" width="190" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="170" text-anchor="middle" font-size="12">rpmsg core · virtio bus</text>
  <rect x="20" y="202" width="190" height="40" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="226" text-anchor="middle" font-size="12">remoteproc · mailbox</text>
  <line x1="115" y1="74" x2="115" y2="88" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="115" y1="130" x2="115" y2="144" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="115" y1="186" x2="115" y2="200" stroke="currentColor" marker-end="url(#ah9)"/>
  <rect x="250" y="120" width="180" height="90" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="144" text-anchor="middle" font-size="12">공유 메모리 (DDR/SRAM)</text>
  <text x="340" y="164" text-anchor="middle" font-size="12">vring TX · vring RX</text>
  <text x="340" y="184" text-anchor="middle" font-size="12">+ 메시지 버퍼</text>
  <text x="340" y="202" text-anchor="middle" font-size="12">(resource table로 합의)</text>
  <rect x="250" y="250" width="180" height="44" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="270" text-anchor="middle" font-size="12">mailbox HW (MU · IPCC)</text>
  <text x="340" y="286" text-anchor="middle" font-size="12">"kick" 인터럽트</text>
  <text x="470" y="22" font-size="13">Cortex-M/R (RTOS)</text>
  <rect x="470" y="34" width="190" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="565" y="58" text-anchor="middle" font-size="12">제어 태스크 · ISR</text>
  <rect x="470" y="146" width="190" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="565" y="170" text-anchor="middle" font-size="12">OpenAMP / RPMsg-Lite</text>
  <line x1="565" y1="74" x2="565" y2="144" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="210" y1="166" x2="248" y2="166" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="470" y1="166" x2="432" y2="166" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="210" y1="232" x2="248" y2="266" stroke="currentColor" marker-end="url(#ah9)"/>
  <line x1="432" y1="266" x2="520" y2="188" stroke="currentColor" marker-end="url(#ah9)"/>
  <rect x="20" y="320" width="640" height="60" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="32" y="342" font-size="12">관리: echo fw.elf > /sys/class/remoteproc/remoteproc0/firmware; echo start > state</text>
  <text x="32" y="364" font-size="12">→ rproc_boot(): ELF 로드 (/lib/firmware), resource table 해석, vdev(virtio) 생성, 코어 시작</text>
</svg>
```

그림 9 — 이종 SoC에서 Linux ↔ M/R 코어 메시지 경로. 데이터는 공유 메모리의 virtio ring으로, "새 데이터 있음" 알림은 mailbox 인터럽트로 간다. Linux 쪽 펌웨어 관리는 remoteproc sysfs.

핵심 구조체 · 함수(6.x 기준, 이름은 `include/linux/remoteproc.h`, `include/linux/rpmsg.h`에서 확인 가능):

- `struct rproc` + `struct rproc_ops`(`start`, `stop`, `attach`, `kick`, `da_to_va`, `parse_fw` 등) — 벤더 드라이버(`imx_rproc.c`, `stm32_rproc.c`, `ti_k3_r5_remoteproc.c`)가 이걸 채운다. 코어 쪽은 `rproc_alloc()` → `rproc_add()`, 부팅은 `rproc_boot()`.
- **resource table**: 펌웨어 ELF의 `.resource_table` 섹션. 원격 코어가 필요로 하는 carveout 메모리, trace 버퍼, vdev(rpmsg용 virtio 장치와 vring 크기)를 Linux에게 알려 주는 "계약서"다. Don의 SSD 경험으로 비유하면, 호스트와 컨트롤러가 큐 주소 · 크기를 합의하는 Admin Queue 설정과 같은 역할이다.
- **vring + kick**: 메시지는 virtio ring(공유 메모리의 descriptor ring)에 넣고, 상대에게 mailbox 인터럽트로 "kick"한다. NVMe의 SQ + doorbell과 같은 모양이다.
- rpmsg 쪽: 원격 코어가 name service 메시지로 채널(예: `rpmsg-tty`, 벤더 샘플 채널)을 알리면 Linux에서 `struct rpmsg_driver`의 `probe`가 불리고, 데이터는 `callback`으로 들어온다. 보내기는 `rpmsg_send()`. 유저스페이스는 `rpmsg_char`(`/dev/rpmsg_ctrlN`에 `RPMSG_CREATE_EPT_IOCTL`로 endpoint 생성 → `/dev/rpmsgN`)나 `rpmsg_tty`(`/dev/ttyRPMSGN`)로 쓴다.
- 원격 코어 crash 처리: remoteproc에는 recovery(원격 코어 crash 감지 시 재시작)와 coredump 기능이 있다(`/sys/kernel/debug/remoteproc/remoteprocN/` 아래 recovery · coredump 설정 — 드라이버 지원 여부 확인). 반대 방향 — **Linux가 죽을 때 M 코어가 알아채는 것** — 은 프레임워크가 해 주지 않는다. 이 노트 §8의 heartbeat를 rpmsg 위에서 똑같이 구현해야 한다.

QEMU `virt`에는 원격 코어가 없어서 이 경로를 실제로 돌려 보는 실습은 O 모듈에서 다룸(실제 보드 또는 remoteproc을 흉내 내는 별도 구성 필요).

### 10.3 PREEMPT_RT / Xenomai / hypervisor 쪽 연결

- PREEMPT_RT가 바꾸는 커널 내부(스핀락 → rt_mutex, IRQ 스레드화, softirq 처리)는 N1, 튜닝 손잡이는 N2, 측정은 N3.
- Xenomai의 Dovetail은 인터럽트 파이프라인을 커널에 넣어 "out-of-band" 단계에서 RT 코어가 먼저 처리하게 한다(기억 기반) — mainline 커널에는 없는 패치다.
- Jailhouse는 Linux 커널 모듈(`jailhouse.ko`)로 hypervisor를 EL2에 올리고, cell 설정 파일로 CPU · 메모리 · 장치를 떼어 준다(기억 기반). Device Tree에서 그 자원을 Linux가 쓰지 않도록 빼는 작업이 따라온다(D 모듈 지식).

---

## 11. Embedded 실무에서 다시 보기

### 11.1 센서 키트: Jetson + STM32 IMU/모터 보드

Jetson(인지)과 STM32(IMU 1 kHz, 바퀴 모터 2개 FOC)를 UART 3 Mbaud로 묶는 구성.

- STM32: TIM 인터럽트로 IMU를 SPI로 읽고, 샘플마다 **MCU 타이머 시각**을 붙여 링버퍼에 쌓는다. 10개씩 묶어 100 Hz 프레임으로 보낸다(1 kHz 프레임은 Linux 쪽 wakeup만 늘린다).
- Linux: 수신 스레드(`SCHED_FIFO` 중간 우선순위)가 프레임을 풀고, §7의 offset/drift 모델로 MCU 시각을 `CLOCK_MONOTONIC`으로 변환해 ROS 2 메시지 헤더에 넣는다. 카메라 프레임은 V4L2 버퍼의 타임스탬프(드라이버가 찍은 `CLOCK_MONOTONIC`)를 쓴다 → 같은 시간축에서 융합.
- 모터 명령: Linux → `(v, ω, valid_until)` 50 Hz. MCU는 `valid_until`을 지난 명령이면 감속 정지. heartbeat는 명령 프레임 자체가 겸한다(명령이 heartbeat — 별도 HB보다 "건강함"에 가깝다).

### 11.2 "Linux에서 바로 PWM 쓰면 안 되나요?"

Linux에는 PWM 서브시스템(`/sys/class/pwm`)과 GPIO가 있으니 "MCU 없이 SoC 핀으로 모터를 돌리자"는 제안이 나온다. 답: 서보 위치 명령이나 LED 밝기처럼 **PWM 파형 자체는 하드웨어가 만들고 Linux는 듀티만 가끔 바꾸는** 경우는 괜찮다. 하지만 FOC처럼 PWM 주기마다 전류를 재고 듀티를 다시 계산해야 하는 경우, 그리고 Linux가 죽었을 때 그 PWM 핀이 마지막 듀티로 계속 나가는 경우는 안 된다 — 후자는 특히 위험하다(드라이버가 멈춰도 PWM 하드웨어는 계속 돈다).

### 11.3 부팅 순서와 전원 시퀀스

전원 ON → MCU가 수 ms 안에 모든 출력을 안전 상태로 → Linux 부팅(수 초) → Linux 서비스가 MCU와 HELLO/버전 확인 → 사용자/상위 로직이 ARM → RUN. 반대로 종료 시에는 Linux가 먼저 MCU에 "정지 후 SAFE" 명령 → 확인 → Linux 종료. OTA 재부팅 중에도 MCU는 SAFE에서 기다린다. 이 시퀀스를 상태도로 문서화하는 것이 ICD(Interface Control Document)의 핵심이다.

### 11.4 디버깅: 경계에서 일어나는 버그를 어떻게 잡나

- 프레임 단위 로그를 양쪽에 seq와 함께 남기면, "Linux가 seq 1000을 보냈는데 MCU 로그에는 999 다음 1001"처럼 손실 위치가 바로 보인다.
- 로직 분석기로 UART TX/RX 선 + MCU GPIO(루프 시작/끝 토글) + Linux GPIO(앱 루프 토글)를 같이 찍으면 시간축 하나에서 경계의 지연이 보인다(M11). Don의 장비 디버그 경험이 가장 직접적으로 쓰이는 곳이다.
- Linux 쪽 지연은 ftrace(M5)로 `irq_handler_entry` · `sched_wakeup` · `sched_switch`를 앱 PID 기준으로 보면 "IRQ는 제때 왔는데 앱이 늦게 깼다" 같은 원인 분리가 된다.

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 전류/속도 루프를 Linux 앱 ↔ MCU 왕복으로 닫음 | 평소엔 되는데 가끔 진동 · 오버슛, 부하 걸리면 발산 | 링크 RTT 꼬리(이 노트 p99.9 수십~수백 ms) | 루프는 MCU 안에서 닫고 링크로는 setpoint만 |
| heartbeat deadline을 주기의 2~3배로 빠듯하게 | "이유 없이" 가끔 정지 | 정상 지터가 deadline 초과 (실행 1에서 23 s에 5번) | 정상 간격 분포의 max + 여유로 설정, Linux 쪽 HB 우선순위 상향 |
| SAFE에서 자동 복귀 | 멈췄던 앱이 깨어나자마자 모터 재가동 | catch-up burst로 "정상 HB N개" 즉시 충족 (실행 2: 45.7 ms) | SAFE 래치 + 명시적 re-arm, 너무 이른 HB도 이상으로 처리 |
| heartbeat를 별도 스레드가 무조건 송신 | 제어 스레드가 deadlock인데 MCU는 정상으로 앎 | liveness만 증명, health 미증명 | 메인 루프 진행에 묶어서 송신, 하위 기능 카운터 포함 |
| 샘플에 Linux 도착 시각을 붙임 | 센서 융합 결과가 흔들림, 카메라-IMU 정렬 실패 | 링크 지연 지터가 그대로 타임스탬프 오차 | MCU에서 타임스탬프, offset/drift 모델로 변환 (최소 RTT 필터) |
| 모든 샘플 평균으로 offset 추정 | 시간 오차 수 ms, 드리프트 추정 2배 틀림 | 비대칭 지연이 섞임 (예제 7: 97.9 ppm vs 참값 50) | 창마다 최소 RTT 샘플만 사용, 직선/PI 필터 |
| 길이 필드를 CRC로만 보호 | 아주 드문 미검출 오류 | 깨진 len이 CRC 범위를 바꿈 (예제 2의 1건) | 헤더 CRC 분리, type별 길이 검사, 안전 메시지는 CRC32 + 이중화 |
| MCU 펌웨어 버전 확인 없이 ARM | 필드에서만 재현되는 이상 동작 | Linux/MCU 프로토콜 불일치 | HELLO 버전 핸드셰이크, 불일치 시 ARM 거부, 번들 단위 배포 |
| 이종 SoC의 M 코어가 "Linux와 독립"이라고 가정 | Linux 드라이버 로드/OTA 재부팅 때 M 코어 같이 멈춤 | 공유 클럭 · 전원 · 리셋 · DDR | TCM 실행, 리소스 도메인 보호, 리셋 도메인 확인, 클럭을 Linux가 끄지 못하게 DT에서 관리 |
| E-stop을 소프트웨어 명령으로만 구현 | 소프트웨어 장애 시 정지 불가 | 안전 기능이 비인증 경로에 있음 | 하드와이어 E-stop/STO 회로 + safety MCU |

---

## 13. 면접에서 이렇게 말한다

**Q.** "What would you put on the MCU versus Linux in our robot?"

**A.** 기능별로 deadline, 놓쳤을 때 결과, 안전 등급, Linux가 죽어도 살아야 하는지를 본다. 전류 루프 · PWM · 엔코더 · E-stop 감시 · 부팅 직후 출력 상태는 MCU, 1 kHz 관절 제어는 MCU나 RT 코어(측정해서 margin이 있으면 PREEMPT_RT 격리 코어), 인지 · 계획 · 네트워크 · OTA는 Linux. 경계로는 setpoint와 상태만 보내고 루프는 경계를 넘지 않게 한다.

> I go function by function and ask four questions: what's the deadline, what happens if we miss it, is it safety-rated, and does it have to keep running when Linux is down or still booting. Current loops, PWM, encoder capture, the E-stop path and the power-on safe state go to the MCU. The 1 kHz joint loop goes to the MCU or a real-time core — or an isolated PREEMPT_RT core only if we've measured worst-case latency under real load with margin. Perception, planning, networking, logging and OTA stay on Linux. And I never close a loop across the link: Linux sends setpoints with a validity time, the MCU closes the loop locally.

**꼬리 질문**: "Why not just use PREEMPT_RT for everything?" → 최악값 보장이 아니라 측정 기반 확률이고, 드라이버 경로 전체의 꼬리 · 부팅 시간 · Linux 장애 시 출력 · 인증 문제가 남는다.

**Q.** "Linux crashes — what happens to the motors?"

**A.** MCU가 Linux heartbeat를 deadline으로 감시하다가 끊기면 스스로 safe state(램프다운 후 PWM off, 브레이크)로 가고 래치한다. 감지 시간은 Linux 상태와 무관하게 MCU의 deadline이 정한다. 랩에서 앱 SIGSTOP, SIGKILL, 커널 panic 세 경우 모두 마지막 heartbeat 후 50~51 ms(deadline 50 ms)에 SAFE로 갔다. 그리고 안전 등급이 필요한 정지는 하드와이어 E-stop/STO가 따로 있다.

> The MCU owns the safe state. It watches a heartbeat from Linux — sent from the main control loop so it proves health, not just liveness — and if it misses the deadline it ramps down, disables the PWM, engages the brake and latches the fault until an explicit re-arm. Detection time is set by the MCU, not by how Linux dies: in a lab test I froze the app, killed it, and panicked the kernel, and all three tripped the safe state 50 ms after the last heartbeat with a 50 ms deadline. For safety-rated stops there's also a hard-wired E-stop/STO path that doesn't depend on any software.

**꼬리 질문**: "How do you pick the deadline?" → 정상 heartbeat 간격 분포의 최대값 + 여유보다 크고, 위험 분석에서 나온 허용 반응 시간보다 작게. 둘이 충돌하면 Linux 쪽 지터를 줄이거나 기능을 내린다. 빠듯하게 잡았을 때 오탐(실행 1의 5회)을 예로 든다.

**Q.** "How do you keep time in sync between Linux and the MCU?"

**A.** 샘플은 MCU 타이머로 찍고 Linux에서 변환한다. 변환식은 PING/PONG 4-타임스탬프로 offset을 추정하되, 창마다 RTT 최소 샘플만 써서 직선(또는 PI 필터)으로 offset과 drift를 추적한다. 정확도가 더 필요하면 PPS/GPIO 동기 펄스를 양쪽이 HW 캡처하거나, Ethernet이면 PTP, 이종 SoC면 공유 시스템 카운터.

> Timestamp at the source with the MCU's timer, then map MCU time to Linux CLOCK_MONOTONIC with an offset-plus-drift model. I estimate it NTP-style with four timestamps per ping, but only keep the minimum-RTT sample in each window, because the error is bounded by half the round trip. In my test the naive fit was off by tens of milliseconds and got the drift wrong by 2x; the min-RTT filter brought it to tens to hundreds of microseconds and recovered the 50 ppm drift. If we need microseconds, I'd add a sync pulse captured in hardware on both sides, or use PTP if there's Ethernet with hardware timestamping.

**Q.** "External MCU or a heterogeneous SoC like i.MX8M or AM64x?"

**A.** 이종 SoC는 BOM · 대역폭 · 업데이트(펌웨어가 rootfs 안이라 원자적)에서 유리하지만, 전원 · 클럭 · 리셋 · DDR 공유 때문에 격리가 완전하지 않고, 보통 Linux가 부팅한 뒤 remoteproc으로 시작된다. 안전 기능이나 부팅 직후 출력이 필요하면 외부 MCU(또는 early boot + 자원 격리를 검증한 이종 SoC), 그렇지 않으면 이종 SoC가 깔끔하다.

> A heterogeneous SoC saves a chip, gives shared-memory bandwidth through rpmsg, and the M-core firmware lives in the rootfs so it updates atomically with Linux. But the isolation isn't free: power rails, clocks, resets and DDR may be shared, and by default remoteproc starts the core after Linux boots. If the function is safety-related or must own outputs from power-on, I prefer an external MCU — or I verify that the SoC can boot the M-core first and that resource domains really protect it.

**Q.** "Design the protocol between Linux and the motor controller."

**A.** 프레임(SOF · type · len · seq · payload · CRC), 절대값 명령 + 유효 기간(멱등), seq로 손실 감지, 샘플 타임스탬프는 MCU 시계, 양방향 heartbeat, 시작 시 HELLO로 펌웨어/프로토콜 버전 확인 후 ARM, 실패 시 래치된 SAFE와 명시적 re-arm. CRC는 길이 필드 문제까지 고려(헤더 검사).

> Length-prefixed frames with a sequence number and a CRC — and I validate the header separately, because a corrupted length field changes what the CRC covers; I actually saw one undetected error in a million two-bit-flip trials from exactly that. Commands are absolute setpoints with a valid-until time so a lost or duplicated frame is harmless. Telemetry carries MCU timestamps. Both sides exchange heartbeats; at startup the MCU sends its firmware and protocol version and Linux refuses to arm on a mismatch. Faults latch into a safe state that needs an explicit re-arm.

**Q.** "How do you update the MCU firmware in the field?"

**A.** MCU 이미지를 Linux OTA 번들에 넣어 "검증된 조합"으로 배포, MCU는 서명 검증 + A/B 슬롯 부트로더(예: MCUboot), 업데이트 중 모터는 SAFE, 새 버전 HELLO 확인 후 Linux 쪽 commit. 롤백 시 반쪽 롤백을 피하려고 프로토콜 하위 호환.

> The MCU image ships inside the Linux update bundle so we only ever deploy tested combinations. After the new rootfs boots, a service compares versions, holds the motors in safe state, and pushes the signed image to an A/B bootloader on the MCU — something like MCUboot — which verifies, trial-boots and confirms. Only when the MCU reports the new version do we mark the Linux slot good. And the protocol stays backward compatible so a rollback on one side doesn't brick the pair.

**Q.** "When is Linux alone with PREEMPT_RT good enough?"

**A.** 1 kHz 이하, 실패가 안전 문제가 아니고, 대상 보드에서 실제 부하로 장시간 측정한 최악값이 deadline의 작은 비율일 때. EtherCAT 마스터처럼 지능형 드라이브에 setpoint만 보내는 구조면 흔히 그렇게 한다. 부팅 중 출력과 Linux 장애 시 동작은 드라이브 쪽이 책임지게 설계한다.

> When the loop is around 1 kHz or slower, missing a cycle isn't a safety event, and I've measured worst-case latency on the target hardware under realistic load for hours with good margin. A typical example is an EtherCAT master on PREEMPT_RT talking to smart drives — the drives still own the current loop and their own fault reaction if the master stops.

---

## 14. 직접 해보기

1. **(개념)** 짐벌 카메라의 기능 목록(IMU 읽기, 3축 안정화, 피사체 추적, 영상 인코딩, 앱 연결, 배터리 감시, 전원 버튼)을 그림 2의 흐름도로 배치하고 각 근거를 한 줄씩 써라.
   정답/힌트: IMU · 안정화 · 배터리 감시 · 전원 버튼은 MCU(always-on 포함), 추적 · 인코딩 · 앱은 Linux, 경계는 "목표 자세 + 유효 기간".
2. **(코드)** `n4_feed()`를 고쳐 CRC 오류 시 버퍼 안의 다음 `0xA5`부터 다시 스캔하게 하고, 헤더(type, len, seq)에 CRC8을 따로 붙여라. 예제 2를 다시 돌려 2비트 미검출이 사라지는지 확인하라.
   정답/힌트: 재스캔은 `memmove`로 버퍼를 한 칸씩 당기거나 원형 버퍼 인덱스로 구현, 헤더 CRC는 len을 믿기 전에 검사.
3. **(코드)** `n4_linux hb`에서 마감을 놓쳤을 때 밀린 주기를 건너뛰도록(`next`가 현재보다 과거면 `now + period`로 재설정) 고치고, 실행 2의 SIGSTOP/SIGCONT를 다시 해서 SIGCONT 직후 60 ms 안의 heartbeat 수가 42개에서 몇 개로 주는지 재라.
   정답/힌트: 6개 근처가 나와야 한다. MCU 쪽도 "간격 < 주기/2"를 이상으로 세 보라.
4. **(코드)** MCU 시뮬레이터에 SAFE 래치를 넣어라 — SAFE 후에는 `N4_STATE` 프레임의 re-arm 코드(예: 0x55)를 받고, 그 뒤 정상 HB 20개가 **정상 간격으로** 와야 RUN.
   정답/힌트: re-arm 플래그와 "마지막 간격이 [주기/2, deadline] 안"인 연속 카운트를 같이 본다.
5. **(측정)** 게스트 안에서 RTT 측정을 `chrt -f 50`으로 돌리고 부하 4개를 걸어, 예제 6의 `q_load` 분포(p99.9 224 ms)와 비교하라. 무엇이 줄고 무엇이 안 주는가?
   정답/힌트: 앱 스케줄링 지연은 줄지만 tty flip buffer → workqueue 경로와 TCG 지연은 남는다 — 커널 경로의 우선순위(IRQ 스레드, workqueue)도 같이 봐야 한다.
6. **(관찰)** 게스트에서 `cat /proc/interrupts`를 RTT 측정 전후로 찍어 ttyAMA1(irq 14)의 인터럽트 수가 프레임 수와 어떤 관계인지 보라.
   정답/힌트: 프레임당 RX/TX 인터럽트가 몇 번인지 — FIFO 임계값과 RX timeout 인터럽트를 생각하라.
7. **(설계)** 협동로봇 회사 면접에서 "E-stop을 눌렀을 때 일어나는 일을 전원 차단까지 순서대로 설명하라"를 받았다고 가정하고, 하드와이어 경로 · safety MCU · 드라이브 MCU · Linux가 각각 무엇을 하는지 시간 순 표로 써라.
   정답/힌트: 하드와이어 STO가 먼저(수 ms), safety MCU가 상태 확인과 브레이크, 드라이브는 출력 차단 확인, Linux는 맨 마지막에 이벤트 기록과 UI 표시 — Linux는 그 경로에 없어야 한다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| deadline | 마감 시간 | 이벤트부터 반응까지 허용되는 최대 시간 |
| jitter | 지터 | 주기 작업 실행 시점의 흔들림 |
| hard / soft real-time | 경성 / 연성 실시간 | 한 번 놓치면 실패 / 가끔 놓치면 품질 저하 |
| FOC | Field-Oriented Control | BLDC/PMSM 모터의 전류를 회전 좌표계에서 제어하는 기법, 10~40 kHz 루프 |
| safe state | 안전 상태 | 고장 시 가야 하는 정의된 상태 (PWM off, 브레이크 등) |
| heartbeat | 생존 신호 | 주기적으로 보내는 "나 정상" 메시지, 끊기면 상대가 failsafe |
| failsafe | 고장 안전 | 고장 시 안전 쪽으로 가도록 하는 설계 |
| latch | 래치 | 한 번 들어간 고장 상태를 명시적 해제 전까지 유지 |
| catch-up burst | 따라잡기 폭주 | 절대 시각 주기 루프가 멈췄다 풀리면 밀린 주기를 몰아서 실행하는 현상 |
| SIL / ASIL / PL / DAL | 안전 무결성 등급 | IEC 61508 / ISO 26262 / ISO 13849 / DO-178C의 등급 |
| lockstep | 락스텝 | 같은 코어 두 개가 같은 코드를 돌려 결과를 비교하는 안전 MCU 구조 |
| STO | Safe Torque Off | 드라이브의 토크 발생을 하드웨어적으로 끊는 안전 기능 |
| remoteproc | remote processor framework | Linux가 다른 코어의 펌웨어를 로드 · 시작 · 정지하는 프레임워크 |
| rpmsg | remote processor messaging | 공유 메모리 virtio ring + mailbox로 코어 간 메시지를 주고받는 프레임워크 |
| resource table | 리소스 테이블 | 원격 코어 펌웨어 ELF 안에서 메모리 · vring을 Linux와 합의하는 섹션 |
| mailbox | 메일박스 | 코어 간 인터럽트 + 작은 레지스터를 제공하는 HW 블록 (NXP MU, ST IPCC) |
| AMP / SMP | 비대칭 / 대칭 다중처리 | 코어마다 다른 OS / 한 OS가 모든 코어 관리 |
| Jailhouse / Xen / Bao | 파티셔닝 hypervisor | 코어 · 메모리 · 장치를 정적으로 나눠 여러 OS를 돌림 |
| Xenomai (Cobalt) | dual-kernel RT | Linux 아래 RT 코어가 인터럽트를 먼저 처리 |
| offset / drift | 시계 차 / 시계 속도 차 | 두 시계의 현재 차이와 그 차이가 변하는 속도(ppm) |
| PTP | Precision Time Protocol (IEEE 1588) | HW 타임스탬프 기반 네트워크 시간 동기 |
| PPS | Pulse Per Second | 1초마다 나오는 동기 펄스, HW 캡처로 정밀 동기 |
| ICD | Interface Control Document | 두 시스템 사이 인터페이스 계약 문서 |
| serdev | serial device bus | UART에 붙은 장치를 커널 드라이버로 묶는 프레임워크 |
| black channel | 블랙 채널 원칙 | 통신 채널을 믿지 않고 끝단에서 안전 검증 |

---

## 16. 요약 & 체크리스트

Linux는 생태계(카메라 · ML · 네트워크 · 파일시스템 · OTA)를 주지만 최악 지연, 부팅 시간, 장애 격리, 안전 인증을 주지 못한다. 그래서 기능마다 deadline · 놓쳤을 때 결과 · 안전 등급 · 생존성을 따져 하드웨어 → safety MCU → MCU/RT 코어 → PREEMPT_RT → Linux 중 처음 "예"가 나오는 곳에 둔다. 분할 패턴은 PREEMPT_RT 단독, 외부 MCU, 이종 SoC(remoteproc/rpmsg), hypervisor, dual-kernel 다섯 가지이고 격리 수준과 통합 비용이 서로 다르다. 경계는 setpoint와 상태만 넘고 루프는 넘지 않는다 — 측정해 보니 Linux 유저스페이스 왕복은 최솟값 18 µs인데 꼬리가 수십~수백 ms였다. 시간은 MCU에서 찍고 최소 RTT 필터로 offset/drift를 추정해 변환한다. MCU는 heartbeat로 Linux를 감시하고, 앱 정지 · 종료 · 커널 panic 모두 deadline(50 ms) + tick 안에 SAFE로 갔다. deadline을 빠듯하게 잡으면 오탐, SAFE 자동 복귀는 catch-up burst 때문에 위험하다. MCU 펌웨어는 버전 핸드셰이크와 번들 단위 OTA로 관리한다.

- [ ] 기능 하나를 deadline · 결과 · 인증 · 생존성 · 부팅 · 전원 기준으로 배치하고 근거를 말할 수 있다
- [ ] 로봇 팔 / AMR / 짐벌 / 드론의 기능 배치표를 그릴 수 있다
- [ ] 다섯 가지 분할 패턴의 격리 수준 · 지연 · 인증 · OTA 차이를 설명할 수 있다
- [ ] 이종 SoC가 "완전히 독립"이 아닌 이유(공유 전원 · 클럭 · 리셋 · DDR, 부팅 순서)를 말할 수 있다
- [ ] SOF · len · seq · CRC 프레임과 바이트 단위 디코더를 C로 짤 수 있다
- [ ] CRC16 미검출 확률과 길이 필드 문제를 설명할 수 있다
- [ ] 4-타임스탬프 offset 공식과 최소 RTT 필터가 왜 효과적인지 설명할 수 있다
- [ ] heartbeat deadline을 정상 지터 분포와 허용 반응 시간으로 정할 수 있다
- [ ] liveness vs health, SAFE 래치, catch-up burst를 실험 결과로 설명할 수 있다
- [ ] MCU 펌웨어 OTA 흐름(번들 포함 · 버전 확인 · A/B 부트로더 · 반쪽 롤백 방지)을 설명할 수 있다
- [ ] UART 수신 경로(IRQ → flip buffer → workqueue → n_tty → 앱)와 rpmsg 경로(vring + mailbox kick)를 그릴 수 있다
- [ ] "Linux가 죽으면 모터는?"에 측정 숫자를 넣어 30초 안에 답할 수 있다

## 참고 자료

- Linux kernel docs — Remote Processor Framework: https://docs.kernel.org/staging/remoteproc.html
- Linux kernel docs — Remote Processor Messaging (rpmsg) Framework: https://docs.kernel.org/staging/rpmsg.html
- Linux kernel docs — Real-time preemption (PREEMPT_RT): https://docs.kernel.org/core-api/real-time/index.html
- 커널 소스(elixir.bootlin.com): `drivers/remoteproc/` (`remoteproc_core.c`, `imx_rproc.c`, `stm32_rproc.c`, `ti_k3_r5_remoteproc.c`, `xlnx_r5_remoteproc.c`), `drivers/rpmsg/` (`virtio_rpmsg_bus.c`, `rpmsg_char.c`), `drivers/tty/serial/amba-pl011.c`, `drivers/tty/tty_buffer.c`, `drivers/tty/serdev/`
- man7.org: `termios(3)`(cfmakeraw), `clock_nanosleep(2)`, `poll(2)`, `sched(7)`
- OpenAMP 프로젝트: https://www.openampproject.org/
- PX4 문서 — Companion Computers / Offboard mode: https://docs.px4.io/
- MCUboot: https://docs.mcuboot.com/
- LinuxPTP (`ptp4l`, `phc2sys`): https://linuxptp.sourceforge.net/
- Xenomai: https://xenomai.org/ · Jailhouse: https://github.com/siemens/jailhouse · Bao: https://github.com/bao-project/bao-hypervisor
- ELISA (Enabling Linux in Safety Applications): https://elisa.tech/
- 관련 노트: N1 · N2 · N3(실시간), L1(systemd watchdog), L4(OTA), L7(crash 수집), M5(ftrace), M11(HW 연계 디버깅), O 모듈(이종 코어 — 예정), Q 모듈(CAN · 로보틱스 — 예정)
