# 임베디드 스터디 노트 — 커리큘럼 아이템 목록

생성: `python3 review_items.py` (curriculum/tracks/*.json 에서 생성. 손으로 고치지 말 것)

총 68개 트랙 · 2149개 아이템

| 단계 | 트랙 | 이름 | B | M | A | 합계 | 외부환경 |
|---|---|---|---|---|---|---|---|
| 1 | C | 임베디드 C 핵심 | 15 | 15 | 11 | 41 | 0 |
| 1 | ARCH | 컴퓨터 구조 & 하드웨어 기초 | 14 | 15 | 12 | 41 | 0 |
| 1 | ARM | ARM 어셈블리 | 14 | 14 | 11 | 39 | 11 |
| 1 | TOOL | 툴체인 · 빌드 · 디버깅 | 14 | 14 | 11 | 39 | 2 |
| 1 | DS | 자료구조 | 14 | 14 | 11 | 39 | 0 |
| 1 | ALGO | 알고리즘 | 15 | 15 | 12 | 42 | 0 |
| 2 | CPP | 임베디드 C++ | 14 | 14 | 10 | 38 | 0 |
| 2 | BM | 베어메탈 프로그래밍 | 14 | 13 | 11 | 38 | 12 |
| 2 | COMM | 통신 프로토콜 & 드라이버 | 14 | 15 | 11 | 40 | 1 |
| 2 | RTOS | RTOS | 13 | 14 | 10 | 37 | 2 |
| 2 | OS | 운영체제 & Core OS | 14 | 14 | 11 | 39 | 5 |
| 2 | RUST | 임베디드 Rust | 10 | 13 | 7 | 30 | 4 |
| 3 | ELX | 임베디드 리눅스 | 13 | 15 | 10 | 38 | 35 |
| 3 | PLAT | 플랫폼 소프트웨어 | 11 | 12 | 9 | 32 | 3 |
| 3 | PCIE | PCIe · NVMe | 13 | 15 | 10 | 38 | 4 |
| 3 | MEM | 메모리 시스템 | 12 | 14 | 10 | 36 | 0 |
| 4 | TEST | 테스트 · 안전 · 신뢰성 | 10 | 12 | 8 | 30 | 0 |
| 4 | SEC | 임베디드 보안 | 10 | 12 | 8 | 30 | 2 |
| 4 | INFRA | 임베디드 인프라 · DevOps | 9 | 10 | 7 | 26 | 1 |
| 4 | LPERF | 리눅스 성능 · 관측 | 10 | 12 | 7 | 29 | 27 |
| 5 | SIV | 실리콘 검증 | 12 | 13 | 9 | 34 | 0 |
| 5 | HWLAB | 보드 브링업 · 계측 | 10 | 12 | 8 | 30 | 2 |
| 5 | EMU | 에뮬레이션 · 프로토타이핑 | 9 | 10 | 7 | 26 | 8 |
| 5 | SIM | 시뮬레이션 · 모델링 | 9 | 10 | 7 | 26 | 0 |
| 5 | DSP | 신호처리 · 고정소수점 | 12 | 12 | 9 | 33 | 1 |
| 5 | SENS | 센서 · 측정 | 10 | 10 | 8 | 28 | 0 |
| 5 | RF | RF · 무선 펌웨어 | 11 | 10 | 8 | 29 | 1 |
| 5 | WCONN | 리눅스 무선 연결성 | 11 | 11 | 8 | 30 | 14 |
| 5 | RCTL | 로봇 제어 알고리즘 | 11 | 12 | 9 | 32 | 0 |
| 5 | DIST | 분산 시스템 | 10 | 10 | 7 | 27 | 0 |
| 6 | CAM | 카메라 · 이미지 신호처리 | 10 | 12 | 7 | 29 | 2 |
| 6 | DISP | 디스플레이 | 9 | 9 | 7 | 25 | 2 |
| 6 | MEDIA | 비디오 · 오디오 미디어 | 10 | 12 | 8 | 30 | 2 |
| 6 | GFX | 그래픽스 · 3D 자료구조 | 9 | 10 | 7 | 26 | 0 |
| 6 | GPGPU | GPU 연산 | 9 | 10 | 7 | 26 | 1 |
| 6 | GPUSYS | GPU 시스템 | 8 | 10 | 8 | 26 | 2 |
| 7 | EML | 디바이스 ML 추론 | 10 | 12 | 8 | 30 | 1 |
| 7 | AIPERF | AI 워크로드 · 성능 지표 | 9 | 12 | 9 | 30 | 0 |
| 7 | NPU | AI 가속기 (NPU) | 9 | 12 | 12 | 33 | 1 |
| 8 | DRONE | 드론 비행 제어 | 9 | 10 | 8 | 27 | 0 |
| 8 | AUTO | 차량용 임베디드 | 9 | 10 | 8 | 27 | 0 |
| 8 | AERO | 항공우주 · 방산 | 9 | 10 | 8 | 27 | 0 |
| 8 | AUTON | 자율 시스템 | 9 | 10 | 8 | 27 | 0 |
| 8 | BMS | 배터리 관리 시스템 | 9 | 10 | 8 | 27 | 0 |
| 8 | BMC | 서버 관리 컨트롤러 | 9 | 10 | 8 | 27 | 4 |
| 9 | MCPP | 모던 C++ 심화 | 13 | 13 | 10 | 36 | 0 |
| 9 | COMP | 컴파일러 | 9 | 10 | 9 | 28 | 0 |
| 9 | VM | 언어 런타임 · VM | 8 | 9 | 8 | 25 | 0 |
| 9 | ROS | 로봇 소프트웨어 스택 | 8 | 9 | 6 | 23 | 13 |
| 9 | CV | 컴퓨터 비전 | 9 | 10 | 8 | 27 | 0 |
| 9 | REND | 렌더링 | 7 | 8 | 7 | 22 | 0 |
| 9 | PHYS | 물리 엔진 | 8 | 9 | 7 | 24 | 0 |
| 9 | GAME | 게임 엔진 구조 | 8 | 9 | 7 | 24 | 0 |
| 9 | LLAT | 저지연 시스템 | 8 | 9 | 7 | 24 | 5 |
| 9 | STOR | 스토리지 엔진 · DB | 7 | 10 | 8 | 25 | 0 |
| 10 | RISCV | RISC-V ISA · 특권 구조 | 12 | 13 | 11 | 36 | 14 |
| 10 | RTLDV | RTL 설계 · 검증 | 11 | 13 | 11 | 35 | 35 |
| 10 | SOCX | SoC 인터커넥트 · 칩렛 | 12 | 13 | 10 | 35 | 0 |
| 11 | NET | 리눅스 네트워크 스택 | 12 | 13 | 11 | 36 | 12 |
| 11 | DCN | 데이터센터 네트워크 · AI 패브릭 | 11 | 13 | 11 | 35 | 6 |
| 11 | NETHW | SmartNIC · DPU · 스위치 ASIC | 12 | 12 | 12 | 36 | 2 |
| 11 | VIRT | 가상화 · 컨테이너 | 11 | 13 | 10 | 34 | 9 |
| 11 | AISYS | AI 학습 · 서빙 시스템 | 11 | 13 | 11 | 35 | 0 |
| 12 | QPROB | 확률 · 퀀트 브레인티저 | 11 | 12 | 10 | 33 | 0 |
| 12 | QFIN | 시장 미시구조 · 트레이딩 시스템 | 11 | 13 | 9 | 33 | 0 |
| 12 | NUM | 수치해석 · 선형대수 | 12 | 13 | 9 | 34 | 0 |
| 13 | SYSD | 시스템 설계 | 16 | 20 | 16 | 52 | 0 |
| 13 | CAP | 종합 프로젝트 & 면접 대비 | 8 | 8 | 7 | 23 | 2 |

## 실습환경

호스트: Apple M6 MacBook · macOS 27.2 · RAM 32GB · Lima 설치됨(/opt/homebrew/bin/limactl) · C 빌드는 SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk (27.2 SDK 링크 깨짐)

| env | 이름 | 상태 | 아이템 | 어디서 |
|---|---|---|---|---|
| mac | 맥 로컬 | 바로 가능 | 1901 | 호스트 맥 그대로 |
| linux | Linux VM (Lima) | 일부 준비됨 | 130 | Lima VM 'elinux' (vz, aarch64, 6 CPU / 12GiB / 100GiB) — 현재 Stopped |
| qemu | QEMU + 크로스 툴체인 | 준비 필요 | 69 | 맥 호스트에는 아직 없음 (qemu-system-*, arm-none-eabi-gcc 미설치). Linux VM 안의 /var/lab QEMU 랩은 ELX 항목에 재사용 가능 |
| fpga | HDL 시뮬레이터 (Verilator · iverilog · yosys · cocotb) | 준비 필요 | 41 | 맥 호스트 (brew) — FPGA 보드는 필요 없음 |
| board | 실제 MCU 보드 | 선택 | 2 | 구매 필요 (2개 항목뿐 — 나중에) |
| gpu | NVIDIA GPU (CUDA) | 선택 | 3 | 클라우드 GPU 인스턴스 (3개 항목뿐) |
| lab | 계측·RF 장비 | 선택 | 3 | 구매 필요 (3개 항목) |

### mac — 맥 로컬 (바로 가능)

- 설명: 맥(Apple Silicon)에서 clang/c++/python3로 바로 빌드·실행·검증
- 어디서: 호스트 맥 그대로
- 현재 있는 것: clang(/usr/bin/clang), lldb, python3(/usr/local/bin/python3)

준비:

```sh
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
clang -std=c11 -Wall -Wextra -O2 -g -fsanitize=address,undefined x.c && ./a.out
Rust(RUST 트랙): curl https://sh.rustup.rs -sSf | sh && rustup target add thumbv7em-none-eabihf thumbv7m-none-eabi && cargo install cargo-binutils
수치·퀀트·AI(NUM/QPROB/QFIN/AISYS): python3 -m pip install numpy scipy mpmath torch  (torch는 AISYS — CPU/MPS, 분산은 gloo)
미디어(MEDIA): brew install ffmpeg   # 비트스트림 파서 정답 오라클
계측 분석(HWLAB): brew install sigrok-cli   # LA 캡처(.sr) 변환·디코드
```

확인: `clang --version && echo 'int main(){return 0;}' | clang -x c - -o /tmp/t && /tmp/t && echo OK`

- macOS 27.2 SDK 링크가 깨져 있어 SDKROOT를 26.4로 지정해야 링크됨
- ASan/UBSan은 맥에서 바로 동작, valgrind는 없음(필요하면 Lima VM)
- VIRT Hypervisor.framework 미니 VMM: com.apple.security.hypervisor entitlement 필요 → codesign --sign - --entitlements hv.plist --force ./vmm (없으면 hv_vm_create = HV_DENIED)
- NET utun 항목은 sudo 필요 (맥에는 TAP 없음 — L2/ARP는 시뮬레이션 링크에서)

해당 아이템: 1901개 (전체 목록은 아래 트랙별 표)

### linux — Linux VM (Lima) (일부 준비됨)

- 설명: Linux 전용 API·커널 필요 → Lima VM 등 리눅스 환경
- 어디서: Lima VM 'elinux' (vz, aarch64, 6 CPU / 12GiB / 100GiB) — 현재 Stopped
- 현재 있는 것: EmbeddedLinux 스터디 때 만든 VM. 커널 Image/-dbg/-kasan/-rt 등과 QEMU 랩 /var/lab, RTOS 랩 /var/rtoslab 이 VM 안에 있음

준비:

```sh
limactl start elinux && limactl shell elinux
sudo apt install -y build-essential linux-headers-$(uname -r) gdb strace ltrace perf-tools-unstable can-utils v4l-utils libdrm-dev ipmitool
ROS 트랙: Ubuntu 24.04에 ros-jazzy-desktop (apt, ROS 2 공식 저장소 추가 후)
가상 장치 모듈: sudo modprobe vcan / vivid / vimc / vkms / vgem / i2c-stub / gpio-sim
NET/LPERF: sudo apt install -y iproute2 nftables ethtool tcpdump bpftrace bpfcc-tools libbpf-dev clang llvm linux-tools-$(uname -r)
DCN RDMA(Soft-RoCE): sudo apt install -y rdma-core ibverbs-utils perftest && sudo rdma link add rxe0 type rxe netdev eth0
WCONN: sudo apt install -y iw wpasupplicant hostapd bluez linux-modules-extra-$(uname -r) && sudo modprobe mac80211_hwsim radios=2 (BlueZ btvirt·wmediumd는 소스 빌드)
NETHW P4: p4c + bmv2(simple_switch) — 패키지 또는 소스 빌드
MEDIA: sudo modprobe snd-aloop vicodec
```

확인: `limactl shell elinux -- uname -a`

- VM 측정치(지연·지터)는 하이퍼바이저 노이즈 포함 → 수치보다 경향 비교로 기록
- Yocto/Buildroot 풀 이미지 빌드는 guide-only (Don 결정 10-17)
- 모듈이 없으면(CONFIG_GPIO_SIM 등) 커널 config 확인 후 모듈만 빌드
- Lima vz VM에는 /dev/kvm 없음 → VIRT KVM 항목(M09/A03–A05)은 arm64 베어메탈·클라우드 호스트, 또는 VM 안 QEMU TCG(-M virt,virtualization=on)로 대체
- VM은 하드웨어 PMU 카운터가 없을 수 있음 → LPERF PMU 항목은 software event로 대체하거나 Graviton 등 실머신
- rdma_rxe 성능 수치는 상대 비교용으로만 기록

| 트랙 | 아이템 | 환경 메모 |
|---|---|---|
| COMM | COMM-M09 SocketCAN 실습: vcan, candump, C 소켓 프로그램 | Lima VM 등 리눅스(vcan 커널 모듈, can-utils 설치) |
| OS | OS-M13 epoll 에코 서버: level vs edge triggered | Lima VM 등 리눅스(epoll은 리눅스 전용) |
| OS | OS-A01 futex 기반 뮤텍스 구현 (Drepper 방식) | Lima VM 등 리눅스(futex는 리눅스 전용; macOS 대응물은 os_unfair_lock/__ulock_wait로 비교만) |
| OS | OS-A02 디버거는 어떻게 동작하나: ptrace로 미니 디버거 | ptrace 기능이 macOS에서 제한적 → Lima VM(Linux)에서 실습 |
| ELX | ELX-B02 크로스 컴파일 기초: aarch64 툴체인과 sysroot | Lima VM에 gcc-aarch64-linux-gnu, qemu-user 설치 (x86 크로스 경험용으로 x86_64 타깃도 선택) |
| ELX | ELX-B03 커널 빌드와 Kconfig: defconfig, 모듈 vs built-in | Lima VM에서 arm64 커널 소스 빌드(수십 분) — 이미 빌드된 Image가 있으면 재사용 |
| ELX | ELX-B06 커널 모듈 Hello: init/exit, module_param, Kbuild | Lima VM(aarch64 Ubuntu)에서 커널 헤더/모듈 빌드 환경 필요 |
| ELX | ELX-B07 문자 디바이스 드라이버: file_operations read/write | Lima VM(aarch64 Ubuntu)에서 커널 헤더/모듈 빌드 환경 필요 |
| ELX | ELX-B08 ioctl 설계: _IO/_IOR/_IOW 번호와 구조체 ABI | Lima VM(aarch64 Ubuntu)에서 커널 헤더/모듈 빌드 환경 필요 |
| ELX | ELX-B09 sysfs 속성과 kobject: DEVICE_ATTR | Lima VM(aarch64 Ubuntu)에서 커널 헤더/모듈 빌드 환경 필요 |
| ELX | ELX-B10 사용자공간 GPIO: libgpiod와 gpio-sim | Lima VM 커널에 CONFIG_GPIO_SIM 필요 (없으면 모듈로 빌드) |
| ELX | ELX-B11 사용자공간 I2C/SPI: i2c-dev와 spidev | Lima VM에서 i2c-stub, i2c-dev 모듈 사용 (spidev는 실제 보드에서만 실측) |
| ELX | ELX-B13 systemd 유닛 작성: 의존성, 재시작, watchdog | Lima VM(systemd 기반 Ubuntu) |
| ELX | ELX-M02 Blocking I/O: wait queue와 poll/select 지원 | Lima VM(aarch64 Ubuntu)에서 커널 헤더/모듈 빌드 환경 필요 |
| ELX | ELX-M03 mmap 드라이버: 커널 버퍼를 사용자공간에 매핑 | Lima VM(aarch64 Ubuntu)에서 커널 헤더/모듈 빌드 환경 필요 |
| ELX | ELX-M04 드라이버 동기화: spinlock vs mutex vs atomic | lockdep이 켜진 디버그 커널(Lima VM 또는 QEMU) 필요 |
| ELX | ELX-M05 인터럽트 처리: hard IRQ와 threaded IRQ | Lima VM 커널에 CONFIG_GPIO_SIM 필요 |
| ELX | ELX-M06 Bottom half 비교: tasklet vs workqueue vs threaded IRQ | Lima VM(aarch64 Ubuntu)에서 커널 헤더/모듈 빌드 환경 필요 |
| ELX | ELX-M07 커널 타이머와 hrtimer: 주기 샘플링 드라이버 | Lima VM(aarch64 Ubuntu)에서 커널 헤더/모듈 빌드 환경 필요 |
| ELX | ELX-M08 I2C 클라이언트 드라이버: i2c_driver + regmap + hwmon | Lima VM에서 i2c-stub 모듈로 가상 장치 생성 |
| ELX | ELX-M09 IIO 드라이버: 가상 ADC 채널, buffer, trigger | CONFIG_IIO, IIO_SW_TRIGGER, IIO_HRTIMER_TRIGGER 필요 |
| ELX | ELX-M11 uevent와 udev 규칙: 핫플러그 이벤트 처리 | Lima VM(aarch64 Ubuntu)에서 커널 헤더/모듈 빌드 환경 필요 |
| ELX | ELX-M13 Buildroot: defconfig 분석과 커스텀 패키지 | 풀 이미지 빌드는 무거움 → guide-only, 패키지 단위만 실습 |
| ELX | ELX-M14 Yocto 개념: layer, recipe, bbappend, bitbake | Yocto 풀 이미지 빌드는 guide-only (bitbake -e/파싱 수준까지만 실행) |
| ELX | ELX-M15 커널 디버깅 1: printk, dynamic_debug, oops 디코드 | 디버그 심볼(CONFIG_DEBUG_INFO)이 포함된 커널 권장 |
| ELX | ELX-A01 ftrace·trace-cmd와 커스텀 tracepoint | Lima VM(aarch64 Ubuntu)에서 커널 헤더/모듈 빌드 환경 필요 |
| ELX | ELX-A03 KASAN·kmemleak으로 커널 메모리 버그 잡기 | KASAN 커널(Image-kasan 등) 필요 |
| ELX | ELX-A04 PREEMPT_RT와 실시간 지연 측정: cyclictest | PREEMPT_RT 커널(-rt) 필요, VM 측정치는 참고용 |
| ELX | ELX-A07 드라이버 전원 관리: runtime PM과 system suspend | Lima VM(aarch64 Ubuntu)에서 커널 헤더/모듈 빌드 환경 필요 |
| ELX | ELX-A09 커널 패치 워크플로: checkpatch, sparse, format-patch | 커널 소스 트리(Lima VM) |
| PCIE | PCIE-B06 lspci·setpci 실전 해석: -tv, -vvv, -xxxx | Lima VM의 virtio PCI 장치로 충분, 물리 링크 정보(LnkSta)는 실제 HW 샘플 출력으로 학습 |
| INFRA | INFRA-A01 Yocto/Buildroot CI: sstate·download 캐시와 증분 빌드 | 풀 이미지 빌드는 guide-only; 설정·워크플로 작성과 소규모 패키지 빌드만 실습 |
| LPERF | LPERF-B01 60초 성능 점검: uptime·vmstat·mpstat·iostat·sar | Lima VM 'elinux' (aarch64); sysstat·stress-ng 설치(apt); VM 수치는 하이퍼바이저 노이즈 포함 → 경향 비교로 기록 |
| LPERF | LPERF-B02 USE 방법론 도구: /proc 파싱과 PSI | Lima VM 'elinux' (aarch64); 커널 4.20+ PSI(/proc/pressure) 필요, Ubuntu 기본 커널에서 활성 |
| LPERF | LPERF-B03 perf 시작하기: perf stat과 소프트웨어 이벤트 | Lima VM 'elinux' (aarch64); linux-tools-$(uname -r) 또는 linux-perf 설치; 하드웨어 이벤트는 PMU 노출 여부에 따라 <not supported>일 수 있음 |
| LPERF | LPERF-B04 perf_event_open으로 미니 perf stat 구현 | Lima VM 'elinux' (aarch64); Lima vz VM은 하드웨어 PMU를 노출하지 않을 수 있음(perf list에 cycles/instructions가 없으면 미지원) → 소프트웨어 이벤트(cpu-clock, task-clock, page-faults)로 진행하고, 하드웨어 카운터는 arm64 bare-metal/클라우드 인스턴스(예: AWS Graviton)에서 재실행 |
| LPERF | LPERF-B05 샘플링 프로파일링: perf record·report·annotate | Lima VM 'elinux' (aarch64); perf + debug 심볼(-g) 빌드; VM에서 PMU가 없으면 cpu-clock 기반 샘플링으로 자동 대체됨 |
| LPERF | LPERF-B07 시스템 콜 관측: strace -c·-T vs perf trace | Lima VM 'elinux' (aarch64); strace, linux-perf 설치; syscalls tracepoint는 tracefs 접근 권한 필요(sudo) |
| LPERF | LPERF-B08 메모리 사용량 지표: VSZ·RSS·PSS·USS와 page cache | Lima VM 'elinux' (aarch64); procfs만 사용, 추가 패키지 불필요 |
| LPERF | LPERF-B09 Page fault 프로파일링: minor vs major | Lima VM 'elinux' (aarch64); echo 3 > /proc/sys/vm/drop_caches 를 위해 sudo 필요 |
| LPERF | LPERF-B10 스레드 시간 분해: on-CPU·런큐 대기·sleep | Lima VM 'elinux' (aarch64); CONFIG_SCHEDSTATS(또는 schedstat 필드) 확인; 오버서브스크립션 재현용 stress-ng |
| LPERF | LPERF-M01 PMU 하드웨어 이벤트와 IPC 해석 | Lima VM 'elinux' (aarch64); Lima vz VM은 하드웨어 PMU를 노출하지 않을 수 있음(perf list에 cycles/instructions가 없으면 미지원) → 소프트웨어 이벤트(cpu-clock, task-clock, page-faults)로 진행하고, 하드웨어 카운터는 arm64 bare-metal/클라우드 인스턴스(예: AWS Graviton)에서 재실행; 맥 대안: Instruments CPU Counters 템플릿으로 같은 벤치 비교 가능 |
| LPERF | LPERF-M03 심볼 해석 문제 해결: stripped·JIT·컨테이너 | Lima VM 'elinux' (aarch64); perf + elfutils; 네임스페이스 실습은 unshare(또는 podman) 사용 |
| LPERF | LPERF-M04 bpftrace 입문: 프로브 종류와 one-liner 10선 | Lima VM 'elinux' (aarch64); bpftrace 설치(apt), BTF(/sys/kernel/btf/vmlinux) 지원 커널, sudo |
| LPERF | LPERF-M05 스케줄러 지연 측정: runqlat 직접 구현 | Lima VM 'elinux' (aarch64); bpftrace + perf sched(sched tracepoint), sudo; VM 스케줄링 노이즈 포함 |
| LPERF | LPERF-M06 Off-CPU 분석: 블로킹 시간 flame graph와 wakeup 체인 | Lima VM 'elinux' (aarch64); bpftrace 또는 perf record -e sched:sched_switch -g; folded 결과 시각화는 B06 도구 재사용 |
| LPERF | LPERF-M07 락 경합 프로파일링: futex 대기와 holder 찾기 | Lima VM 'elinux' (aarch64); bpftrace, perf 6.x(perf lock contention -b 지원), sudo |
| LPERF | LPERF-M08 libbpf CO-RE 도구 작성: ring buffer와 skeleton | Lima VM 'elinux' (aarch64); clang/llvm, libbpf-dev, bpftool(vmlinux.h 생성), BTF 지원 커널, sudo |
| LPERF | LPERF-M09 블록 I/O 지연: iostat 해석과 biolatency | Lima VM 'elinux' (aarch64); fio, sysstat, bpftrace; Lima 가상 디스크(virtio) 지연은 실제 SSD와 다름을 기록 |
| LPERF | LPERF-M10 메모리 할당 프로파일링: 핫스팟과 누수 스택 | Lima VM 'elinux' (aarch64); bpftrace, heaptrack, valgrind 설치; uprobe 대상 libc 심볼 확인 |
| LPERF | LPERF-M11 메모리 압박 관측: reclaim·PSI·OOM in cgroup v2 | Lima VM 'elinux' (aarch64); systemd-run --scope -p MemoryHigh=... 또는 /sys/fs/cgroup 직접 조작, sudo |
| LPERF | LPERF-M12 CPU 스로틀링과 꼬리 지연: cpu.max·nr_throttled | Lima VM 'elinux' (aarch64); cgroup v2(cpu 컨트롤러), cpu.max.burst는 커널 5.14+, sudo |
| LPERF | LPERF-A01 OS 노이즈 측정: osnoise·timerlat tracer와 IRQ 시간 | Lima VM 'elinux' (aarch64); CONFIG_OSNOISE_TRACER/TIMERLAT_TRACER 활성 커널(5.14+)과 rtla 도구; VM은 호스트 노이즈가 섞이므로 상대 비교 |
| LPERF | LPERF-A02 USDT·uprobe로 애플리케이션 정적 트레이싱 | Lima VM 'elinux' (aarch64); systemtap-sdt-dev(sys/sdt.h), bpftrace, sudo |
| LPERF | LPERF-A03 perf c2c로 false sharing 찾기 | 실제 PMU 메모리 샘플링 필요: x86 PEBS load-latency 또는 Arm SPE가 있는 bare-metal/클라우드(예: Graviton metal, Intel 서버); Lima vz VM에서는 동작하지 않음 → VM에서는 결과 파일 해석만 수행 |
| LPERF | LPERF-A04 임베디드 타깃 프로파일링: 크로스 perf와 오프라인 심볼화 | Lima VM 'elinux' (aarch64); VM 안 QEMU 랩(/var/lab) 재사용; QEMU TCG는 PMU가 없어 cpu-clock(hrtimer) 샘플링만 가능 — 실보드(Cortex-A + PMU)면 하드웨어 이벤트까지 확장 |
| LPERF | LPERF-A05 CI 성능 회귀 감지: instructions 카운트와 차등 flame graph | Lima VM 'elinux' (aarch64); perf/valgrind(cachegrind); instructions 하드웨어 카운터가 없으면 cachegrind Ir로 대체 |
| LPERF | LPERF-A06 Continuous profiling 설계: fleet 상시 프로파일러 | Lima VM 'elinux' (aarch64); 에이전트는 VM에서 실행, 집계 서버는 맥/VM 어디서나 가능; 여러 호스트는 프로세스 여러 개로 모사 |
| LPERF | LPERF-A07 종합: 꼬리 지연 미스터리 사후 분석 | Lima VM 'elinux' (aarch64); B01~A06에서 만든 도구 재사용; 주입 시나리오 시드 고정으로 재현성 확보 |
| WCONN | WCONN-B02 리눅스 무선 스택 지도: SoftMAC vs FullMAC | Lima VM 'elinux'에서 sudo modprobe mac80211_hwsim radios=2(또는 3) — 모듈이 없으면 linux-modules-extra 설치; apt install iw hostapd wpasupplicant tcpdump; AP/STA는 별도 netns에 배치; ftrace 기본은 ELX-A01 참고 |
| WCONN | WCONN-B04 Generic Netlink로 nl80211 직접 호출 | Lima VM 'elinux'에서 sudo modprobe mac80211_hwsim radios=2(또는 3) — 모듈이 없으면 linux-modules-extra 설치; apt install iw hostapd wpasupplicant tcpdump; AP/STA는 별도 netns에 배치; sudo modprobe nlmon 후 ip link add nlmon0 type nlmon으로 netlink 캡처 |
| WCONN | WCONN-B05 mac80211_hwsim 랩: hostapd AP + wpa_supplicant STA | Lima VM 'elinux'에서 sudo modprobe mac80211_hwsim radios=2(또는 3) — 모듈이 없으면 linux-modules-extra 설치; apt install iw hostapd wpasupplicant tcpdump; AP/STA는 별도 netns에 배치 |
| WCONN | WCONN-B10 BlueZ 랩: 가상 컨트롤러와 btmon | Lima VM 'elinux'에 실제 BT 컨트롤러가 없으므로 sudo modprobe hci_vhci + BlueZ 소스의 emulator/btvirt(빌드 필요)로 가상 LE 컨트롤러 2개 생성; apt install bluez bluez-tools; btmon으로 캡처 |
| WCONN | WCONN-B11 커널 펌웨어 로딩: request_firmware 경로 | Lima VM 'elinux'(aarch64 Ubuntu)에서 커널 헤더/모듈 빌드 환경 필요 |
| WCONN | WCONN-M04 wpa_supplicant 제어 인터페이스 클라이언트 | Lima VM 'elinux'에서 sudo modprobe mac80211_hwsim radios=2(또는 3) — 모듈이 없으면 linux-modules-extra 설치; apt install iw hostapd wpasupplicant tcpdump; AP/STA는 별도 netns에 배치 |
| WCONN | WCONN-M07 Raw HCI 소켓으로 LE 스캔 구현 | Lima VM 'elinux'에 실제 BT 컨트롤러가 없으므로 sudo modprobe hci_vhci + BlueZ 소스의 emulator/btvirt(빌드 필요)로 가상 LE 컨트롤러 2개 생성; apt install bluez bluez-tools; btmon으로 캡처 |
| WCONN | WCONN-M08 BlueZ D-Bus GATT 서버: 프로비저닝 서비스 | Lima VM 'elinux'에 실제 BT 컨트롤러가 없으므로 sudo modprobe hci_vhci + BlueZ 소스의 emulator/btvirt(빌드 필요)로 가상 LE 컨트롤러 2개 생성; apt install bluez bluez-tools; btmon으로 캡처; python3-dbus/python3-gi 또는 sd-bus(C) 사용 |
| WCONN | WCONN-M10 연결 관리 데몬: 다중 링크 정책과 failover | Lima VM 'elinux'에서 ip netns/veth/tc netem으로 eth·wifi·lte 링크를 흉내 냄 (정책 엔진 로직은 맥에서 단위테스트 가능) |
| WCONN | WCONN-A01 엔터프라이즈 Wi-Fi: 802.1X/EAP-TLS | Lima VM 'elinux'에서 sudo modprobe mac80211_hwsim radios=2(또는 3) — 모듈이 없으면 linux-modules-extra 설치; apt install iw hostapd wpasupplicant tcpdump; AP/STA는 별도 netns에 배치; openssl로 테스트 CA 생성 |
| WCONN | WCONN-A02 hwsim + wmediumd 로밍·장애 주입 통합 테스트 | Lima VM 'elinux'에서 sudo modprobe mac80211_hwsim radios=2(또는 3) — 모듈이 없으면 linux-modules-extra 설치; apt install iw hostapd wpasupplicant tcpdump; AP/STA는 별도 netns에 배치; wmediumd 소스 빌드 필요 |
| WCONN | WCONN-A04 무선 펌웨어 크래시 감지·덤프·복구 | Lima VM 'elinux'(aarch64 Ubuntu)에서 커널 헤더/모듈 빌드 환경 필요 |
| WCONN | WCONN-A05 디바이스 프로비저닝 종합: BLE 온보딩 → Wi-Fi 연결 | Lima VM 'elinux'에 실제 BT 컨트롤러가 없으므로 sudo modprobe hci_vhci + BlueZ 소스의 emulator/btvirt(빌드 필요)로 가상 LE 컨트롤러 2개 생성; apt install bluez bluez-tools; btmon으로 캡처; Lima VM 'elinux'에서 sudo modprobe mac80211_hwsim radios=2(또는 3) — 모듈이 없으면 linux-modules-extra 설치; apt install iw hostapd wpasupplicant tcpdump; AP/STA는 별도 netns에 배치 |
| WCONN | WCONN-A08 Wi-Fi 연결 장애 트리아지 케이스 스터디 | Lima VM 'elinux'에서 sudo modprobe mac80211_hwsim radios=2(또는 3) — 모듈이 없으면 linux-modules-extra 설치; apt install iw hostapd wpasupplicant tcpdump; AP/STA는 별도 netns에 배치 |
| CAM | CAM-A01 V4L2 mmap 스트리밍 캡처 앱 | Lima VM 등 리눅스에서 modprobe vivid 필요 (v4l-utils 설치) |
| CAM | CAM-A02 Media Controller와 V4L2 Subdev 파이프라인 구성 | Lima VM에서 modprobe vimc, v4l-utils(media-ctl) 필요 |
| DISP | DISP-A01 DRM/KMS Dumb Buffer 모드셋과 페이지 플립 | Lima VM에서 modprobe vkms, libdrm-dev 필요 |
| DISP | DISP-A02 Atomic Modesetting과 Plane 속성 | Lima VM, vkms (overlay plane 지원 커널 버전 확인) |
| MEDIA | MEDIA-M12 ALSA PCM 재생/캡처: hw_params·Period·XRUN 복구 | Lima VM elinux: sudo modprobe snd-aloop, sudo apt install libasound2-dev alsa-utils (aplay/arecord로 사전 확인) |
| MEDIA | MEDIA-A01 V4L2 Stateful M2M 코덱 API: vicodec 인코더/디코더 | Lima VM elinux: sudo modprobe vicodec (CONFIG_VIDEO_VICODEC 없으면 모듈만 빌드), v4l-utils 설치, v4l2-ctl --list-devices로 확인 |
| GPUSYS | GPUSYS-A04 리눅스 GEM·dma-buf 버퍼 공유와 sync_file | Lima VM에서 modprobe vgem, libdrm 필요 |
| BMC | BMC-M04 OpenBMC D-Bus 아키텍처: 센서 객체와 ObjectMapper | Lima VM의 Ubuntu에서 session/system D-Bus와 busctl 사용. |
| BMC | BMC-M07 IPMI over LAN (RMCP+)과 인증 세션 | Lima VM에 OpenIPMI(ipmi_sim)·ipmitool·tcpdump 설치. |
| BMC | BMC-A06 OpenBMC 스타일 센서 데몬 개발 (sdbusplus) | Lima VM에서 meson으로 sdbusplus 빌드. QEMU OpenBMC 이미지에 올리는 것은 선택(SDK 크로스 빌드는 가이드만). |
| ROS | ROS-B04 ROS 2 시작: rclcpp 노드·토픽 | ROS 2 Jazzy는 macOS 지원이 제한적 → Lima Ubuntu 24.04 VM에 ros-jazzy 설치 |
| ROS | ROS-B05 Services & Actions | ROS 2 Jazzy는 macOS 지원이 제한적 → Lima Ubuntu 24.04 VM에 ros-jazzy 설치 |
| ROS | ROS-B06 Parameters & launch | ROS 2 Jazzy는 macOS 지원이 제한적 → Lima Ubuntu 24.04 VM에 ros-jazzy 설치 |
| ROS | ROS-B07 커스텀 메시지 & rosidl 인터페이스 | ROS 2 Jazzy는 macOS 지원이 제한적 → Lima Ubuntu 24.04 VM에 ros-jazzy 설치 |
| ROS | ROS-B08 ROS 2 계층 구조: rclcpp → rcl → rmw → DDS | ROS 2 Jazzy는 macOS 지원이 제한적 → Lima Ubuntu 24.04 VM에 ros-jazzy 설치 |
| ROS | ROS-M02 ROS 2 QoS 불일치 & 손실 링크 실험 | ROS 2 Jazzy는 macOS 지원이 제한적 → Lima Ubuntu 24.04 VM에 ros-jazzy 설치; tc netem 사용 |
| ROS | ROS-M03 Executor & callback group | ROS 2 Jazzy는 macOS 지원이 제한적 → Lima Ubuntu 24.04 VM에 ros-jazzy 설치 |
| ROS | ROS-M06 Intra-process & zero-copy 통신 | ROS 2 Jazzy는 macOS 지원이 제한적 → Lima Ubuntu 24.04 VM에 ros-jazzy 설치 |
| ROS | ROS-M08 Lifecycle(managed) 노드 | ROS 2 Jazzy는 macOS 지원이 제한적 → Lima Ubuntu 24.04 VM에 ros-jazzy 설치 |
| ROS | ROS-M09 ros2_control 하드웨어 인터페이스 | ROS 2 Jazzy는 macOS 지원이 제한적 → Lima Ubuntu 24.04 VM에 ros-jazzy 설치 |
| ROS | ROS-A01 실시간 ROS 2: 지터 측정 & 할당 제거 | ROS 2 Jazzy는 macOS 지원이 제한적 → Lima Ubuntu 24.04 VM에 ros-jazzy 설치; PREEMPT_RT 커널 권장 |
| ROS | ROS-A02 ros2_tracing으로 end-to-end 지연 분석 | ROS 2 Jazzy는 macOS 지원이 제한적 → Lima Ubuntu 24.04 VM에 ros-jazzy 설치; lttng-tools 설치 |
| ROS | ROS-A03 micro-ROS: MCU ↔ agent | Linux VM에서 micro-ROS agent + Zephyr native_sim, 또는 ESP32/STM32 보드 |
| LLAT | LLAT-M07 CPU 격리 & 피닝: isolcpus·nohz_full·IRQ affinity | Lima Ubuntu VM (Linux 커널 기능 필요); 부팅 파라미터 변경 가능한 VM, VM 내 측정은 하이퍼바이저 노이즈 포함됨을 명시 |
| LLAT | LLAT-A02 커널 바이패스: DPDK 폴링 모드 | Lima Ubuntu VM (Linux 커널 기능 필요); hugepage 설정, DPDK 빌드(meson/ninja), 가상 NIC 사용 |
| LLAT | LLAT-A03 io_uring 기반 저지연 I/O | Lima Ubuntu VM (Linux 커널 기능 필요); 커널 6.x + liburing |
| LLAT | LLAT-A04 타임스탬핑: HW vs SW, SO_TIMESTAMPING, PTP 개념 | Lima Ubuntu VM (Linux 커널 기능 필요); 가상 NIC는 HW 타임스탬프 미지원 → 개념·SW 타임스탬프 위주 |
| LLAT | LLAT-A05 꼬리 지연 원인 분석: page fault·TLB·인터럽트 | Lima Ubuntu VM (Linux 커널 기능 필요); perf/ftrace 사용 |
| NET | NET-M10 netns·veth·bridge·VLAN 토폴로지 구성 | Lima VM 'elinux' (root 필요, ip/bridge/tcpdump 기본 도구) |
| NET | NET-M11 netfilter/nftables: 훅, conntrack, NAT | Lima VM 'elinux' (nftables, conntrack-tools 설치) |
| NET | NET-M12 tc qdisc: netem·tbf·fq_codel과 bufferbloat | Lima VM 'elinux' (iproute2 tc, iperf3; 수치는 VM 노이즈 포함) |
| NET | NET-M13 소켓 버퍼 튜닝: BDP, autotuning, keepalive, user timeout | Lima VM 'elinux' (iproute2 tc/ss, iperf3) |
| NET | NET-A02 수신 경로 추적: IRQ → NAPI poll → GRO → 프로토콜 | Lima VM 'elinux' (bpftrace, virtio-net; 실 NIC 대신 virtio 동작 기준) |
| NET | NET-A03 GSO/TSO/GRO 오프로드 효과 측정 | Lima VM 'elinux' (ethtool, iperf3) |
| NET | NET-A05 XDP 패킷 필터와 BPF 맵 (eBPF) | Lima VM 'elinux' (clang/llvm, libbpf-dev, bpftool, 커널 BTF 필요) |
| NET | NET-A06 tc-BPF L4 로드밸런서 (Katran 스타일) | Lima VM 'elinux' (clang/llvm, libbpf, iproute2 tc clsact) |
| NET | NET-A07 accept 확장성: SO_REUSEPORT, EPOLLEXCLUSIVE, thundering herd | Lima VM 'elinux' (wrk 또는 자체 부하 생성기) |
| NET | NET-A08 제로카피 송수신: sendfile, splice, MSG_ZEROCOPY | Lima VM 'elinux' |
| NET | NET-A09 가상 netdev 드라이버: ndo_start_xmit과 NAPI | Lima VM 'elinux' (linux-headers, 커널 모듈 로드 권한) |
| NET | NET-A10 패킷 드롭 디버깅 방법론 (프로덕션 면접형) | Lima VM 'elinux' (bpftrace, conntrack-tools, nftables) |
| DCN | DCN-B09 RDMA 개념과 Verbs 객체: PD·MR·QP·CQ | Lima VM 'elinux'에서 Soft-RoCE 사용: sudo apt install rdma-core ibverbs-utils perftest librdmacm-dev; sudo modprobe rdma_rxe && sudo rdma link add rxe0 type rxe netdev eth0 (커널에 CONFIG_RDMA_RXE 없으면 모듈만 빌드). rxe는 CPU 에뮬레이션이라 지연·대역폭 절대값은 실 RNIC와 무관 → 상대 비교로 기록 |
| DCN | DCN-B10 RC QP 상태 전이와 SEND/RECV Ping-pong | Lima VM 'elinux'에서 Soft-RoCE 사용: sudo apt install rdma-core ibverbs-utils perftest librdmacm-dev; sudo modprobe rdma_rxe && sudo rdma link add rxe0 type rxe netdev eth0 (커널에 CONFIG_RDMA_RXE 없으면 모듈만 빌드). rxe는 CPU 에뮬레이션이라 지연·대역폭 절대값은 실 RNIC와 무관 → 상대 비교로 기록 |
| DCN | DCN-B11 One-sided RDMA WRITE/READ vs SEND | Lima VM 'elinux'에서 Soft-RoCE 사용: sudo apt install rdma-core ibverbs-utils perftest librdmacm-dev; sudo modprobe rdma_rxe && sudo rdma link add rxe0 type rxe netdev eth0 (커널에 CONFIG_RDMA_RXE 없으면 모듈만 빌드). rxe는 CPU 에뮬레이션이라 지연·대역폭 절대값은 실 RNIC와 무관 → 상대 비교로 기록 |
| DCN | DCN-M01 RDMA 전송 유형: RC·UC·UD와 연결 확장성 | Lima VM 'elinux'에서 Soft-RoCE 사용: sudo apt install rdma-core ibverbs-utils perftest librdmacm-dev; sudo modprobe rdma_rxe && sudo rdma link add rxe0 type rxe netdev eth0 (커널에 CONFIG_RDMA_RXE 없으면 모듈만 빌드). rxe는 CPU 에뮬레이션이라 지연·대역폭 절대값은 실 RNIC와 무관 → 상대 비교로 기록 |
| DCN | DCN-M02 RDMA 성능 패턴: Unsignaled·Inline·Doorbell Batching | Lima VM 'elinux'에서 Soft-RoCE 사용: sudo apt install rdma-core ibverbs-utils perftest librdmacm-dev; sudo modprobe rdma_rxe && sudo rdma link add rxe0 type rxe netdev eth0 (커널에 CONFIG_RDMA_RXE 없으면 모듈만 빌드). rxe는 CPU 에뮬레이션이라 지연·대역폭 절대값은 실 RNIC와 무관 → 상대 비교로 기록 |
| NETHW | NETHW-B09 Linux에서 NIC 관찰: ethtool -l/-g/-k/-c/-S | Lima VM 'elinux' (aarch64 Linux); ethtool, iperf3 설치. 가상 NIC라 지원 기능이 제한적이므로 수치보다 경향과 플래그 의미 위주로 기록 |
| NETHW | NETHW-M07 P4_16 L3 라우터: bmv2 simple_switch와 P4Runtime | Lima VM 'elinux' (aarch64 Linux); p4c·bmv2(behavioral-model)·p4runtime 파이썬 패키지 설치(패키지 또는 소스 빌드), scapy, netns/veth 권한(sudo) 필요 |
| VIRT | VIRT-B09 리눅스 네임스페이스: clone/unshare로 격리 관찰 | Lima VM 'elinux' (root 권한, cgroup v2 unified 계층 마운트 확인: mount | grep cgroup2) |
| VIRT | VIRT-B10 cgroup v2: CPU·메모리·PID 제한 걸기 | Lima VM 'elinux' (root 권한, cgroup v2 unified 계층 마운트 확인: mount | grep cgroup2) |
| VIRT | VIRT-B11 컨테이너 이미지 레이어: overlayfs와 whiteout | Lima VM 'elinux' (root 권한, cgroup v2 unified 계층 마운트 확인: mount | grep cgroup2) |
| VIRT | VIRT-M09 KVM API 미니 VMM (arm64) | Lima vz VM(M2)은 nested virt 미지원이라 /dev/kvm 없음 → arm64 bare-metal(예: Graviton *.metal) 또는 nested virt 되는 x86 클라우드 Linux 호스트 필요. 대안: Lima VM 안에서 qemu-system-aarch64 -M virt,virtualization=on (TCG, EL2 에뮬)으로 CONFIG_KVM 켠 게스트 커널을 띄워 그 안의 /dev/kvm 사용(매우 느림) |
| VIRT | VIRT-M12 미니 컨테이너 런타임: namespaces + pivot_root + cgroup | Lima VM 'elinux' (root 권한, cgroup v2 unified 계층 마운트 확인: mount | grep cgroup2) |
| VIRT | VIRT-M13 seccomp-BPF 필터 직접 작성 | Lima VM 'elinux' (root 권한, cgroup v2 unified 계층 마운트 확인: mount | grep cgroup2) |
| VIRT | VIRT-A03 KVM 성능 분석: Exit 프로파일과 Halt-Polling | Lima vz VM(M2)은 nested virt 미지원이라 /dev/kvm 없음 → arm64 bare-metal(예: Graviton *.metal) 또는 nested virt 되는 x86 클라우드 Linux 호스트 필요. 대안: Lima VM 안에서 qemu-system-aarch64 -M virt,virtualization=on (TCG, EL2 에뮬)으로 CONFIG_KVM 켠 게스트 커널을 띄워 그 안의 /dev/kvm 사용(매우 느림) |
| VIRT | VIRT-A04 ioeventfd·irqfd: VMM을 거치지 않는 알림 경로 | Lima vz VM(M2)은 nested virt 미지원이라 /dev/kvm 없음 → arm64 bare-metal(예: Graviton *.metal) 또는 nested virt 되는 x86 클라우드 Linux 호스트 필요. 대안: Lima VM 안에서 qemu-system-aarch64 -M virt,virtualization=on (TCG, EL2 에뮬)으로 CONFIG_KVM 켠 게스트 커널을 띄워 그 안의 /dev/kvm 사용(매우 느림) |
| VIRT | VIRT-A05 Firecracker 스타일 microVM과 Jailer | Lima vz VM(M2)은 nested virt 미지원이라 /dev/kvm 없음 → arm64 bare-metal(예: Graviton *.metal) 또는 nested virt 되는 x86 클라우드 Linux 호스트 필요. 대안: Lima VM 안에서 qemu-system-aarch64 -M virt,virtualization=on (TCG, EL2 에뮬)으로 CONFIG_KVM 켠 게스트 커널을 띄워 그 안의 /dev/kvm 사용(매우 느림) |
| CAP | CAP-A05 종합 프로젝트 8: 엣지 카메라 → 검출 → ROS 2 퍼블리시 | ROS 2 Jazzy가 설치된 Lima Ubuntu VM; 카메라 대신 녹화 영상 사용 가능 |

### qemu — QEMU + 크로스 툴체인 (준비 필요)

- 설명: QEMU·크로스 툴체인 필요 (Cortex-M/R/A, RISC-V 에뮬레이션)
- 어디서: 맥 호스트에는 아직 없음 (qemu-system-*, arm-none-eabi-gcc 미설치). Linux VM 안의 /var/lab QEMU 랩은 ELX 항목에 재사용 가능
- 현재 있는 것: Lima VM 안 QEMU aarch64 virt 랩(/var/lab) — ELX·PCIE·PLAT 항목용

준비:

```sh
brew install qemu
brew install --cask gcc-arm-embedded   # arm-none-eabi-gcc/gdb (또는 clang --target=thumbv7m-none-eabi + ld.lld)
brew tap riscv-software-src/riscv && brew install riscv-tools   # OS-A09/A10 xv6, EMU-M04
Cortex-M 기본 머신: qemu-system-arm -M mps2-an385 (M3) / mps2-an386 (M4, FPU) -nographic -semihosting -kernel app.elf
디버그: QEMU에 -s -S 추가 후 arm-none-eabi-gdb app.elf -ex 'target remote :1234'
Zephyr(RTOS-A07): pip install west && Zephyr SDK 설치 → west build -b qemu_cortex_m3 -t run
RISCV 트랙: brew tap riscv-software-src/riscv && brew install riscv-tools (riscv64-unknown-elf-gcc, spike) + qemu-system-riscv64 -M virt -bios none -nographic -kernel x.elf
RUST 트랙 QEMU 항목: qemu-system-arm -M lm3s6965evb -nographic -semihosting-config enable=on,target=native -kernel target/thumbv7m-none-eabi/debug/app
```

확인: `qemu-system-arm --version && arm-none-eabi-gcc --version | head -1`

- QEMU는 기능 에뮬레이션 — 사이클 수·타이밍·폴트 레지스터 세부값은 실칩과 다를 수 있음
- OP-TEE/TF-A/OVMF 등 무거운 빌드는 Lima VM 안에서, 오래 걸리면 guide-only
- QEMU 소스 빌드(EMU-M05/M06)는 맥에서 가능, 수십 분

| 트랙 | 아이템 | 환경 메모 |
|---|---|---|
| ARM | ARM-B13 Thumb-2 어셈블리 작성과 IT 블록 | brew install qemu + arm-none-eabi 툴체인(또는 clang+lld), qemu-system-arm -M mps2-an385 -semihosting |
| ARM | ARM-B14 Cortex-M 레지스터 모델: MSP/PSP, xPSR, CONTROL | qemu-system-arm -M mps2-an385 -s -S + arm-none-eabi-gdb(또는 lldb gdb-remote) |
| ARM | ARM-M08 Cortex-M 벡터 테이블과 Thumb 비트 | qemu-system-arm -M mps2-an385 + gdb |
| ARM | ARM-M09 Cortex-M 예외 진입/복귀: 스택 프레임과 EXC_RETURN | qemu-system-arm -M mps2-an385 (FPU 프레임 확인은 mps2-an386/Cortex-M4) |
| ARM | ARM-M10 PRIMASK·BASEPRI로 크리티컬 섹션 구현 | BASEPRI는 ARMv7-M(Cortex-M3/M4) 머신 필요: mps2-an385/an386 |
| ARM | ARM-M11 Cortex-M LDREX/STREX와 Exclusive Monitor | qemu-system-arm -M mps2-an385 + gdb |
| ARM | ARM-M12 HardFault 분석기: CFSR/HFSR 디코딩과 PC 복원 | QEMU의 폴트 레지스터 구현은 실칩과 일부 다를 수 있음 |
| ARM | ARM-M13 AArch64 예외 레벨과 시스템 레지스터 | qemu-system-aarch64 -M virt -cpu cortex-a53 + 크로스 링크(ld.lld/aarch64-elf) |
| ARM | ARM-A01 PendSV 컨텍스트 스위치 어셈블리 (Cortex-M) | qemu-system-arm -M mps2-an385 -semihosting |
| ARM | ARM-A02 AArch64 예외 벡터와 IRQ 컨텍스트 저장 | qemu-system-aarch64 -M virt,gic-version=2 |
| ARM | ARM-A03 AArch64 MMU 켜기: 변환 테이블·MAIR·TCR | qemu-system-aarch64 -M virt -cpu cortex-a53 |
| TOOL | TOOL-A03 GDB 원격 디버깅: QEMU gdbstub로 OpenOCD 흐름 익히기 | qemu-system-arm + arm-none-eabi-gdb; 실보드 시 ST-Link + OpenOCD |
| TOOL | TOOL-A04 세미호스팅과 트레이스: SWO/ITM/RTT 개념 | qemu-system-arm -semihosting-config enable=on |
| BM | BM-B06 부트로더+앱 플래시 분할 레이아웃 설계 | arm-none-eabi-gcc/objdump만 필요(실행은 BM-A03에서 QEMU) |
| BM | BM-B08 C로 쓴 startup으로 첫 부팅: QEMU에서 main까지 | arm-none-eabi-gcc(brew) + qemu-system-arm -M mps2-an385(Cortex-M3) 또는 lm3s6965evb, 디버깅은 -s -S + arm-none-eabi-gdb/lldb |
| BM | BM-B09 printf 리타겟팅: semihosting vs UART _write | arm-none-eabi-gcc + qemu-system-arm -M mps2-an385 |
| BM | BM-M02 시스템 IRQ 우선순위 설계와 선점 검증 | arm-none-eabi-gcc + qemu-system-arm -M mps2-an385 (NVIC STIR/ISPR로 IRQ 발생) |
| BM | BM-M13 Fault 처리 정책: 세분 fault 활성화와 안전 상태 진입 | arm-none-eabi-gcc + qemu-system-arm -M mps2-an385 |
| BM | BM-A01 SystemInit 순서: FPU 활성화, VTOR 재배치, 클럭 전환 | arm-none-eabi-gcc + qemu-system-arm -M mps2-an386(Cortex-M4) 또는 netduinoplus2 |
| BM | BM-A03 부트로더 → 앱 점프: VTOR, MSP, 주변장치 정리 | arm-none-eabi-gcc + qemu-system-arm -M mps2-an385 (-kernel에 합친 바이너리/ELF 로드) |
| BM | BM-A07 스택 오버플로 탐지: 스택 페인팅과 high-water mark | arm-none-eabi-gcc + qemu-system-arm -M mps2-an385 |
| BM | BM-A08 MPU 설정: NULL 트랩, 스택 가드, 실행 금지 RAM | arm-none-eabi-gcc + qemu-system-arm -M mps2-an385 (QEMU는 v7-M MPU 에뮬레이션 지원) |
| BM | BM-A09 크래시 덤프: .noinit RAM에 fault 기록, 리셋 후 보고 | arm-none-eabi-gcc + qemu-system-arm -M mps2-an385 (system_reset은 AIRCR SYSRESETREQ) |
| BM | BM-A11 종합: 베어메탈 데이터 로거 | arm-none-eabi-gcc + qemu-system-arm -M mps2-an385 |
| RTOS | RTOS-A01 Cortex-M 미니 RTOS 완성: 우선순위 스케줄러 + 동기화 | arm-none-eabi-gcc + qemu-system-arm -M mps2-an385(Cortex-M3), gdb로 PSP/레지스터 확인 |
| RTOS | RTOS-A07 Zephyr 기초: west 빌드, 스레드·k_sem·k_msgq | Zephyr SDK + west(macOS 지원) 설치, west build -b qemu_cortex_m3 && west build -t run |
| OS | OS-A09 xv6 커널 해킹 1: 시스템 콜 추가 (trace, sysinfo) | xv6-riscv + riscv64 툴체인(brew riscv-tools 또는 riscv64-elf-gcc) + qemu-system-riscv64 |
| OS | OS-A10 xv6 커널 해킹 2: COW fork 구현 | xv6-riscv + riscv64 툴체인(brew riscv-tools 또는 riscv64-elf-gcc) + qemu-system-riscv64 |
| RUST | RUST-M11 bare-metal 부팅: cortex-m-rt·memory.x·#[entry] | brew install qemu, rustup target add thumbv7m-none-eabi. 실행: qemu-system-arm -M lm3s6965evb -nographic -semihosting-config enable=on,target=native -kernel <elf> (Embedded Rust Book 예제 구성). |
| RUST | RUST-M12 #[exception]/#[interrupt]: SysTick ISR와 HardFault | RUST-M11 환경 재사용 (qemu-system-arm lm3s6965evb, thumbv7m-none-eabi). |
| RUST | RUST-M13 defmt 로깅: 지연 포맷팅과 바이너리 크기 | RUST-M11 QEMU 환경 + cargo install defmt-print. 실보드에선 probe-rs(RTT)로 동일 확인 가능(선택). |
| RUST | RUST-A02 RTIC v2: 우선순위 천장(SRP)과 컴파일타임 락 | RTIC 공식 예제처럼 lm3s6965 PAC + qemu-system-arm -M lm3s6965evb, thumbv7m-none-eabi. cargo install cargo-expand. |
| ELX | ELX-B04 QEMU virt에서 커널+initramfs 부팅 | QEMU aarch64 virt + 크로스 툴체인 (Lima VM 또는 /var/lab QEMU 랩) |
| ELX | ELX-B12 rootfs 구성: FHS, BusyBox, 동적 라이브러리 | QEMU aarch64 virt + 크로스 툴체인 (Lima VM 또는 /var/lab QEMU 랩) |
| ELX | ELX-M01 Platform driver와 DT 바인딩: probe, of_match | QEMU aarch64 virt + 크로스 툴체인 (Lima VM 또는 /var/lab QEMU 랩) |
| ELX | ELX-M10 DMA API: coherent vs streaming mapping | QEMU x86_64 또는 aarch64 virt에 -device edu 추가 |
| ELX | ELX-M12 U-Boot 다루기: env, bootcmd, FIT 이미지 | QEMU aarch64 virt + 크로스 툴체인 (Lima VM 또는 /var/lab QEMU 랩) |
| ELX | ELX-A02 KGDB/QEMU gdbstub로 커널 소스 레벨 디버깅 | CONFIG_GDB_SCRIPTS, DEBUG_INFO 커널 + QEMU 랩 |
| ELX | ELX-A06 UIO/VFIO 사용자공간 드라이버 | QEMU에 -device edu, VFIO는 vIOMMU 필요 |
| ELX | ELX-A08 부트 시간 최적화: initcall_debug, systemd-analyze | QEMU aarch64 virt + 크로스 툴체인 (Lima VM 또는 /var/lab QEMU 랩) |
| ELX | ELX-A10 종합: 가상 센서 보드 BSP 엔드투엔드 | QEMU 랩 + 이전 항목 산출물, Buildroot 풀빌드는 guide-only 가능 |
| PLAT | PLAT-M11 UEFI 기초: Boot/Runtime Services와 Hello UEFI 앱 | QEMU + OVMF/AAVMF 펌웨어 이미지 + gnu-efi 크로스 빌드 |
| PLAT | PLAT-M12 TF-A on QEMU: BL31 런타임 서비스와 커스텀 SiP SMC | TF-A + U-Boot/kernel 크로스 빌드와 QEMU 랩 필요 (빌드 수십 분) |
| PLAT | PLAT-A08 Linux 클럭 provider 드라이버(CCF) | QEMU virt + 커스텀 DT 노드 (ELX 랩 재사용) |
| PCIE | PCIE-M13 Linux PCI 드라이버: probe, BAR iomap, MSI, DMA | QEMU(x86_64 q35 또는 aarch64 virt) 게스트 + 크로스/네이티브 커널 모듈 빌드 (Lima VM 안에서 QEMU 실행 가능) |
| PCIE | PCIE-M14 Linux NVMe 툴링: nvme-cli와 sysfs | QEMU(x86_64 q35 또는 aarch64 virt) 게스트 + 크로스/네이티브 커널 모듈 빌드 (Lima VM 안에서 QEMU 실행 가능) |
| PCIE | PCIE-A01 사용자공간 NVMe 드라이버: VFIO로 BAR 매핑 (SPDK 스타일) | QEMU q35 + intel-iommu(또는 aarch64 virt + virtio-iommu)로 vIOMMU 구성 필요 |
| SEC | SEC-M07 TrustZone-A와 TEE: OP-TEE Trusted Application | OP-TEE QEMUv8 빌드는 무거움 → 가이드 중심, 가능하면 Lima VM에서 빌드 |
| SEC | SEC-M08 MPU 권한 분리: 비특권 태스크 샌드박스와 SVC 게이트 | QEMU(mps2-an385 등 Cortex-M) + arm-none-eabi 툴체인 |
| EMU | EMU-B08 QEMU에서 베어메탈 펌웨어 실행과 디버그 | brew install qemu + arm-none-eabi 또는 riscv64 툴체인 |
| EMU | EMU-M05 QEMU 디바이스 모델 작성 1: MMIO 레지스터 | QEMU 소스 빌드 필요(맥에서 가능, 수십 분) |
| EMU | EMU-M06 QEMU 디바이스 모델 작성 2: IRQ와 DMA | M05 환경 재사용 |
| DSP | DSP-A02 CMSIS-DSP 함수로 Cortex-M 파이프라인 구성 | arm-none-eabi 툴체인 + QEMU mps2-an386 (사이클 수는 QEMU에서 정확하지 않음) |
| EML | EML-A04 MCU 배포: Cortex-M에서 TFLM 실행 | arm-none-eabi-gcc + QEMU(mps2/mps3 머신); 정확한 사이클은 실제 보드(STM32 등) 권장 |
| BMC | BMC-B01 BMC의 역할과 OpenBMC를 QEMU에서 부팅하기 | brew install qemu 후 qemu-system-arm -M ast2600-evb/romulus-bmc. OpenBMC 이미지는 공개 빌드 산출물 사용(Yocto 직접 빌드는 가이드만). |
| RISCV | RISCV-B11 컴파일러 출력 읽기: RISC-V gcc/clang -O2 관용구 | brew install riscv64-elf-gcc riscv64-elf-binutils (또는 brew llvm의 clang --target=riscv64 + ld.lld) — 툴체인만 필요, QEMU 불필요 (또는 Compiler Explorer로 대체 가능) |
| RISCV | RISCV-B12 QEMU virt 베어메탈 첫 부팅: 링커 스크립트·start.S·UART | brew install qemu riscv64-elf-gcc riscv64-elf-gdb; qemu-system-riscv64 -M virt -nographic -bios none -kernel app.elf (-s -S로 gdb attach) |
| RISCV | RISCV-M01 ISS 적합성 검증: riscv-tests ELF 로더와 tohost | brew install riscv64-elf-gcc riscv64-elf-binutils (또는 brew llvm의 clang --target=riscv64 + ld.lld) — 툴체인만 필요, QEMU 불필요; git clone --recursive riscv-software-src/riscv-tests |
| RISCV | RISCV-M07 트랩 핸들러 어셈블리: mscratch 스왑과 컨텍스트 저장 | brew install qemu riscv64-elf-gcc riscv64-elf-gdb; qemu-system-riscv64 -M virt -nographic -bios none -kernel app.elf (-s -S로 gdb attach) |
| RISCV | RISCV-M08 CLINT 타이머 인터럽트: mtime/mtimecmp와 경쟁 없는 갱신 | brew install qemu riscv64-elf-gcc riscv64-elf-gdb; qemu-system-riscv64 -M virt -nographic -bios none -kernel app.elf (-s -S로 gdb attach) |
| RISCV | RISCV-M09 PLIC 외부 인터럽트: priority·threshold·claim/complete | brew install qemu riscv64-elf-gcc riscv64-elf-gdb; qemu-system-riscv64 -M virt -nographic -bios none -kernel app.elf (-s -S로 gdb attach) |
| RISCV | RISCV-M10 특권 모드 전환과 위임: M→S→U, medeleg/mideleg | brew install qemu riscv64-elf-gcc riscv64-elf-gdb; qemu-system-riscv64 -M virt -nographic -bios none -kernel app.elf (-s -S로 gdb attach) |
| RISCV | RISCV-M13 SBI와 OpenSBI: S-mode 커널에서 펌웨어 서비스 호출 | brew install qemu riscv64-elf-gcc riscv64-elf-gdb; qemu-system-riscv64 -M virt -nographic -kernel kernel.elf (기본 내장 OpenSBI가 M-mode, 커널은 S-mode 0x80200000) |
| RISCV | RISCV-A01 S-mode에서 MMU 켜기: satp, 상위 절반 커널, sfence.vma | brew install qemu riscv64-elf-gcc riscv64-elf-gdb; qemu-system-riscv64 -M virt -nographic -kernel kernel.elf (기본 내장 OpenSBI가 M-mode, 커널은 S-mode 0x80200000) |
| RISCV | RISCV-A02 원격 펜스: IPI·TLB shootdown과 FENCE.I | brew install qemu riscv64-elf-gcc riscv64-elf-gdb; qemu-system-riscv64 -M virt -nographic -kernel kernel.elf (기본 내장 OpenSBI가 M-mode, 커널은 S-mode 0x80200000) -smp 4 |
| RISCV | RISCV-A05 RVV 1.0 기초: vsetvli, SEW/LMUL, strip-mining | brew install qemu riscv64-elf-gcc riscv64-elf-gdb; qemu-system-riscv64 -M virt -nographic -bios none -kernel app.elf (-s -S로 gdb attach) -cpu rv64,v=true,vlen=256; 컴파일은 -march=rv64gcv |
| RISCV | RISCV-A06 RVV int8 내적·GEMM 커널: vwmacc, 리덕션, segment load | brew install qemu riscv64-elf-gcc riscv64-elf-gdb; qemu-system-riscv64 -M virt -nographic -bios none -kernel app.elf (-s -S로 gdb attach) -cpu rv64,v=true,vlen=256 -plugin libinsn.so (QEMU 소스의 contrib/plugins 빌드 필요) |
| RISCV | RISCV-A10 Lockstep 공동 시뮬레이션: ISS 커밋 로그 vs Spike 비교 | brew install riscv64-elf-gcc + Spike(riscv-software-src/riscv-isa-sim 소스 빌드, dtc 필요) — QEMU 불필요 |
| RISCV | RISCV-A11 종합: 자작 ISS로 xv6-riscv 부팅 | brew install riscv64-elf-gcc riscv64-elf-binutils (또는 brew llvm의 clang --target=riscv64 + ld.lld) — 툴체인만 필요, QEMU 불필요; git clone mit-pdos/xv6-riscv로 kernel·fs.img 빌드 (실행은 자작 ISS, 비교용으로만 QEMU) |

### fpga — HDL 시뮬레이터 (Verilator · iverilog · yosys · cocotb) (준비 필요)

- 설명: FPGA 보드 또는 HDL 시뮬레이터(Verilator 등) 필요
- 어디서: 맥 호스트 (brew) — FPGA 보드는 필요 없음
- 현재 있는 것: 없음 (verilator, gtkwave 미설치)

준비:

```sh
brew install verilator icarus-verilog yosys
brew install gtkwave   # 파형 보기 (안 되면 surfer)
python3 -m pip install cocotb cocotb-coverage pyuvm pyvsc peakrdl
형식검증·nextpnr·gate-level(RTLDV M09/A04–A07): YosysHQ oss-cad-suite 릴리스 tarball 받아 PATH에 추가 (sby, smtbmc, eqy 포함)
verilator --cc --exe --build -Wall top.sv tb.cpp && ./obj_dir/Vtop
```

확인: `verilator --version`

- 41개 항목(EMU·NPU·RTLDV) 모두 맥 시뮬레이터로 진행 — FPGA 보드는 선택
- 오픈소스 도구로는 SV UVM·covergroup 실행 불가 → RTLDV는 pyuvm·cocotb-coverage로 같은 구조를 익힘
- EMU-M04는 riscv64-unknown-elf 툴체인도 필요 (QEMU 환경과 공유)

| 트랙 | 아이템 | 환경 메모 |
|---|---|---|
| EMU | EMU-B06 Verilator 입문: 카운터·레지스터 블록 시뮬레이션 | brew install verilator gtkwave로 맥에서 가능(FPGA 보드 불필요) |
| EMU | EMU-M01 Verilator C++ 테스트벤치로 AXI-lite 레지스터 구동 | Verilator만 있으면 맥에서 가능 |
| EMU | EMU-M02 Transactor 구현: 트랜잭션 ↔ 신호 변환 | Verilator(맥 brew) |
| EMU | EMU-M03 DPI-C로 C 참조모델 호출 | Verilator(맥 brew) |
| EMU | EMU-M04 RTL CPU 위에서 펌웨어 실행 (PicoRV32) | Verilator + riscv64-unknown-elf 툴체인(맥 brew) |
| NPU | NPU-A09 Systolic Array RTL과 Verilator 공동 시뮬레이션 | Verilator(brew install verilator)로 맥에서도 시뮬레이션 가능; FPGA 보드는 선택 |
| RTLDV | RTLDV-B01 RTL 툴체인 셋업: iverilog·Verilator·GTKWave와 자기검사 TB | brew install icarus-verilog verilator gtkwave — 맥에서 HDL 시뮬레이터만 사용, FPGA 보드 불필요 |
| RTLDV | RTLDV-B02 Blocking vs Non-blocking과 래치 추론 함정 | brew install yosys icarus-verilog — 맥에서 합성·시뮬레이션만 수행, FPGA 보드 불필요 |
| RTLDV | RTLDV-B03 조합 논리 RTL: 디코더·우선순위 인코더·One-hot MUX | brew install verilator gtkwave — 맥에서 Verilator만 사용, FPGA 보드 불필요 |
| RTLDV | RTLDV-B04 순차 로직 RTL: 카운터·LFSR·에지 검출·÷3 클럭 분주 | brew install icarus-verilog verilator gtkwave — 맥에서 HDL 시뮬레이터만 사용, FPGA 보드 불필요 |
| RTLDV | RTLDV-B05 FSM 코딩 스타일: Moore/Mealy·인코딩 선택 | brew install yosys icarus-verilog — 맥에서 합성·시뮬레이션만 수행, FPGA 보드 불필요 |
| RTLDV | RTLDV-B06 리셋 전략: Sync/Async 리셋과 리셋 동기화기 | brew install yosys icarus-verilog — 맥에서 합성·시뮬레이션만 수행, FPGA 보드 불필요 |
| RTLDV | RTLDV-B07 SystemVerilog 구조화: parameter·generate·struct·interface | brew install icarus-verilog verilator gtkwave — 맥에서 HDL 시뮬레이터만 사용, FPGA 보드 불필요 |
| RTLDV | RTLDV-B08 동기 FIFO RTL: Full/Empty·Count·FWFT | brew install icarus-verilog verilator gtkwave — 맥에서 HDL 시뮬레이터만 사용, FPGA 보드 불필요 |
| RTLDV | RTLDV-B09 cocotb 입문: Python 테스트벤치로 FIFO 구동 | brew install icarus-verilog verilator + pip install cocotb — 맥에서 시뮬레이터로만 실행, FPGA 보드 불필요 |
| RTLDV | RTLDV-B10 Valid/Ready 핸드셰이크 RTL 규칙과 레지스터 슬라이스 | brew install icarus-verilog verilator + pip install cocotb — 맥에서 시뮬레이터로만 실행, FPGA 보드 불필요 |
| RTLDV | RTLDV-B11 합성 기초: yosys로 셀·면적·메모리 추론 읽기 | brew install yosys icarus-verilog — 맥에서 합성·시뮬레이션만 수행, FPGA 보드 불필요 |
| RTLDV | RTLDV-M01 Skid Buffer: 등록된 Ready로 풀 스루풋 유지 | brew install icarus-verilog verilator + pip install cocotb — 맥에서 시뮬레이터로만 실행, FPGA 보드 불필요 |
| RTLDV | RTLDV-M02 파이프라인 데이터패스: Stall·Flush·Bubble Collapse | brew install icarus-verilog verilator + pip install cocotb — 맥에서 시뮬레이터로만 실행, FPGA 보드 불필요 |
| RTLDV | RTLDV-M03 아비터 RTL: Fixed·Round-Robin·Weighted RR | brew install icarus-verilog verilator + pip install cocotb — 맥에서 시뮬레이터로만 실행, FPGA 보드 불필요 |
| RTLDV | RTLDV-M04 고정소수점 데이터패스 RTL: Signed·확장·포화 | brew install icarus-verilog verilator + pip install cocotb — 맥에서 시뮬레이터로만 실행, FPGA 보드 불필요 |
| RTLDV | RTLDV-M05 CDC 1: 2-FF 동기화기·펄스 동기화·Req/Ack 핸드셰이크 | brew install icarus-verilog verilator + pip install cocotb — 맥에서 시뮬레이터로만 실행, FPGA 보드 불필요 |
| RTLDV | RTLDV-M06 CDC 2: 비동기 FIFO RTL (Gray 포인터) | brew install icarus-verilog verilator + pip install cocotb — 맥에서 시뮬레이터로만 실행, FPGA 보드 불필요 |
| RTLDV | RTLDV-M07 Lint와 정적 CDC 검사: Verilator -Wall + 넷리스트 스크립트 | brew install verilator yosys + python3 — 맥에서 정적 분석만, 보드 불필요 |
| RTLDV | RTLDV-M08 SVA 기초: Immediate·Concurrent Assertion으로 프로토콜 체크 | brew install verilator gtkwave — 맥에서 Verilator만 사용, FPGA 보드 불필요; Verilator 5.x의 SVA 지원 범위 내에서 작성 |
| RTLDV | RTLDV-M09 Formal 입문: SymbiYosys BMC·k-Induction | YosysHQ oss-cad-suite(darwin-arm64 tarball: yosys+sby+yices/boolector) 압축 해제만으로 맥에서 실행 — 보드 불필요. 오픈소스 yosys는 SVA 일부만 지원(immediate assert/assume/cover + 단순 concurrent)이므로 그 범위로 작성 |
| RTLDV | RTLDV-M10 cocotb 검증 구조화: Driver·Monitor·Scoreboard | brew install icarus-verilog verilator + pip install cocotb — 맥에서 시뮬레이터로만 실행, FPGA 보드 불필요 |
| RTLDV | RTLDV-M11 Constrained Random 자극: 제약·분포·시드 재현 | brew install icarus-verilog + pip install cocotb pyvsc (또는 cocotb-coverage의 crv) — 맥 시뮬레이션만, 보드 불필요 |
| RTLDV | RTLDV-M12 기능·코드 커버리지: Covergroup·Cross·Hole 분석 | brew install icarus-verilog verilator + pip install cocotb cocotb-coverage — 맥 시뮬레이션만, 보드 불필요 |
| RTLDV | RTLDV-M13 레지스터 블록 RTL 생성: SystemRDL → APB CSR | brew install icarus-verilog + pip install cocotb peakrdl peakrdl-regblock — 맥 시뮬레이션만, 보드 불필요 |
| RTLDV | RTLDV-A01 UVM 아키텍처 실습: pyuvm Agent·Env·Sequence | brew install icarus-verilog verilator + pip install cocotb pyuvm — 맥에서 Python UVM으로 실행(상용 SV UVM 시뮬레이터 불필요), 보드 불필요 |
| RTLDV | RTLDV-A02 UVM RAL: 레지스터 모델·Predictor·Mirror 검사 | brew install icarus-verilog verilator + pip install cocotb pyuvm — 맥에서 Python UVM으로 실행(상용 SV UVM 시뮬레이터 불필요), 보드 불필요 |
| RTLDV | RTLDV-A03 회귀 인프라: 시드·테스트 리스트·커버리지 병합·Triage | brew install icarus-verilog verilator + pip install cocotb cocotb-coverage — 맥 시뮬레이션만, 보드 불필요 |
| RTLDV | RTLDV-A04 AXI4 버스트 슬레이브 RTL과 프로토콜 체커 | brew install icarus-verilog verilator + pip install cocotb — 맥에서 시뮬레이터로만 실행, FPGA 보드 불필요; Verilator --assert로 SVA 체커 실행 |
| RTLDV | RTLDV-A05 Formal 심화: AXI4-Lite 슬레이브 인터페이스 증명 | YosysHQ oss-cad-suite(darwin-arm64 tarball: yosys+sby+yices/boolector) 압축 해제만으로 맥에서 실행 — 보드 불필요. 오픈소스 yosys는 SVA 일부만 지원(immediate assert/assume/cover + 단순 concurrent)이므로 그 범위로 작성 |
| RTLDV | RTLDV-A06 Gate-level 시뮬레이션과 X-전파 문제 | brew install yosys icarus-verilog — 맥에서 합성·시뮬레이션만 수행, FPGA 보드 불필요 |
| RTLDV | RTLDV-A07 타이밍 클로저: 파이프라이닝·리타이밍과 등가성 검사 | YosysHQ oss-cad-suite(darwin-arm64: yosys + nextpnr-ice40/ecp5) — 보드 없이 P&R 타이밍 리포트(Fmax)만 사용 |
| RTLDV | RTLDV-A08 저전력 RTL: Clock Gating·Operand Isolation·Toggle 측정 | brew install icarus-verilog verilator + pip install cocotb — 맥에서 시뮬레이터로만 실행, FPGA 보드 불필요 |
| RTLDV | RTLDV-A09 저지연 스트리밍 패킷 파서 RTL (64-bit AXI-Stream) | brew install icarus-verilog verilator + pip install cocotb — 맥에서 시뮬레이터로만 실행, FPGA 보드 불필요 (scapy도 pip) |
| RTLDV | RTLDV-A10 버그 헌팅: 파형·Assertion·Formal Cover로 RTL 결함 국소화 | YosysHQ oss-cad-suite(darwin-arm64 tarball: yosys+sby+yices/boolector) 압축 해제만으로 맥에서 실행 — 보드 불필요. 오픈소스 yosys는 SVA 일부만 지원(immediate assert/assume/cover + 단순 concurrent)이므로 그 범위로 작성; iverilog/cocotb도 함께 사용 |
| RTLDV | RTLDV-A11 캡스톤: CSR·스트림·인터럽트를 갖춘 가속기 블록 설계·검증 | brew install icarus-verilog verilator + pip install cocotb pyuvm — 맥에서 Python UVM으로 실행(상용 SV UVM 시뮬레이터 불필요), 보드 불필요; sby는 oss-cad-suite |

### board — 실제 MCU 보드 (선택)

- 설명: 실제 MCU/SBC 보드 필요 (STM32, nRF, Raspberry Pi 등)
- 어디서: 구매 필요 (2개 항목뿐 — 나중에)
- 현재 있는 것: 없음

준비:

```sh
STM32 Nucleo-F446RE 또는 -H743ZI (Cortex-M4/M7, ST-Link 내장) — BM-A10 사이클 측정
CAP-A01: Nucleo 또는 nRF52 DK + IMU 브레이크아웃 + CAN 트랜시버(SN65HVD230)
brew install openocd stlink && openocd -f board/st_nucleo_f4.cfg
선택: 저가 로직 애널라이저(Saleae 호환 8ch) + sigrok/PulseView
```

확인: `st-info --probe`

- QEMU로 못 하는 것: 정확한 사이클 측정(DWT CYCCNT), 실제 버스 신호, 전력
- 보드 없이도 나머지 커리큘럼 진도에는 지장 없음

| 트랙 | 아이템 | 환경 메모 |
|---|---|---|
| BM | BM-A10 인터럽트 지연 측정: DWT CYCCNT, tail-chaining, late arrival | 사이클 정확한 측정이 필요해 QEMU 불가: Cortex-M4/M7 보드(예: STM32 Nucleo) + 선택적으로 로직 애널라이저 |
| CAP | CAP-A01 종합 프로젝트 4: 센서 노드 실보드 이식 | STM32 Nucleo 또는 nRF52 DK + IMU 브레이크아웃 + CAN 트랜시버(예: SN65HVD230), 로직 애널라이저 권장 |

### gpu — NVIDIA GPU (CUDA) (선택)

- 설명: NVIDIA GPU + CUDA 등 맥에 없는 GPU 환경 필요
- 어디서: 클라우드 GPU 인스턴스 (3개 항목뿐)
- 현재 있는 것: 없음 (Apple GPU는 CUDA 불가)

준비:

```sh
클라우드 GPU 1개 인스턴스(예: L4/T4/A10G) 시간 단위 대여 — Ubuntu + CUDA Toolkit 이미지
nvidia-smi 확인 → nvcc 빌드 → ncu(Nsight Compute)로 프로파일
DCN-A04: 4–8 GPU 인스턴스에서 nccl-tests (all_reduce_perf) — 링크 토폴로지별 busbw 비교
작업 끝나면 인스턴스 중지/삭제 (과금 주의)
```

확인: `nvidia-smi && nvcc --version`

- GPGPU-A01, GPUSYS-A06(1 GPU), DCN-A04(멀티 GPU) 세 항목만 해당 — 몰아서 한 번에 진행
- 개념 실습 대부분은 맥 Metal/CPU로 대체되어 있음

| 트랙 | 아이템 | 환경 메모 |
|---|---|---|
| GPGPU | GPGPU-A01 CUDA 포팅과 Nsight Compute 분석 | NVIDIA GPU + CUDA Toolkit + Nsight Compute (클라우드 GPU 인스턴스 가능) |
| GPUSYS | GPUSYS-A06 nvidia-smi·NVML 실측과 모델 대조 | NVIDIA GPU + 드라이버(nvidia-smi, NVML) 필요, 클라우드 인스턴스 가능 |
| DCN | DCN-A04 nccl-tests 실측과 α-β 모델 대조 | NVIDIA GPU 4~8개가 NVLink/PCIe로 연결된 클라우드 인스턴스(예: 8×H100/A100) 1~2대, CUDA + NCCL + nccl-tests 빌드; 비용 때문에 짧은 세션으로 스윕 스크립트를 미리 준비 |

### lab — 계측·RF 장비 (선택)

- 설명: 계측 장비 필요 (오실로스코프, 로직 애널라이저, RF 장비, 배터리 셀 등)
- 어디서: 구매 필요 (3개 항목)
- 현재 있는 것: 없음

준비:

```sh
RF-A06: RTL-SDR v4 동글 + 안테나 (또는 HackRF One)
brew install librtlsdr && rtl_sdr -f 433.92e6 -s 2.048e6 cap.bin  → 분석은 맥 python(numpy)
HWLAB-A06: fx2 기반 저가 LA(~$10–15, sigrok 호환) + Raspberry Pi Pico를 신호원으로 → sigrok-cli/PulseView
HWLAB-A07: 엔트리 DSO(Rigol DHO804 / Siglent SDS1104X-E급) + 벅 컨버터 평가보드 + PyVISA 원격 제어
```

확인: `rtl_test -t`

- 캡처만 장비가 필요하고 분석은 맥에서 — 공개 IQ 샘플 파일로 먼저 연습 가능
- HWLAB 나머지 28개는 합성 캡처(CSV/VCD/.sr)로 맥에서 분석 — 장비는 마지막에

| 트랙 | 아이템 | 환경 메모 |
|---|---|---|
| HWLAB | HWLAB-A06 저가 LA 실캡처: fx2 LA + sigrok로 실보드 버스 분석 | fx2 기반 저가 LA(~$10–15, sigrok fx2lafw 지원) 또는 Saleae 호환 LA + 신호원 MCU 보드(Raspberry Pi Pico ~$5 등); brew install sigrok-cli. 분석은 맥 |
| HWLAB | HWLAB-A07 보급형 DSO 자동화: SCPI 파형 획득과 실측 검증 | 엔트리 DSO(Rigol DHO804 / Siglent SDS1104X-E급, 70–100MHz, LAN/USB) + 10x 프로브·스프링 팁 + 벅 eval 보드와 스텝 부하(MOSFET+저항); 장비 전에는 mock VISA 백엔드로 개발 |
| RF | RF-A06 실제 SDR로 신호 수신·디코드 | RTL-SDR 동글 또는 HackRF + 안테나 필요(캡처 후 분석은 맥) |

## 1단계 · 기초체력 (Foundations)

### C — 임베디드 C 핵심 (Embedded C Core)

임베디드 펌웨어 면접의 절반은 C 언어의 '정확한' 이해를 확인한다: 비트 연산, volatile/const, 정수 승격, 구조체 레이아웃, 메모리 맵 레지스터 접근, UB. 이 트랙은 이미 아는 것을 직접 구현·검증하며 다시 다지고, 드라이버·ISR 코드에서 실제로 터지는 함정을 손으로 재현한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| C-B01 | 비트연산 기초: Set Clear Toggle Test | uint32_t 레지스터 값에 대한 BIT_SET/BIT_CLR/BIT_TOGGLE/BIT_TEST(다중 비트 마스크 포함)를 구현하고, bit 0·31·마스크 조합을 assert 테이블 테스트로 검증한다 (1 << 31 의 signed 함정 포함). | mac |
| C-B02 | 고정 폭 정수와 정수 승격의 함정 | uint8_t의 ~ 연산, uint16_t 곱셈 오버플로, signed/unsigned 비교 등 10개 스니펫의 출력을 먼저 예측해 적고 실행으로 확인한 뒤, -Wconversion -Wsign-compare 경고와 함께 올바른 캐스팅으로 고친다. | mac |
| C-B03 | Shift 연산의 함정: 폭 초과·음수·산술 시프트 | x << 32, -1 >> 1, 1 << 31, uint8_t << 8 등을 실행해 결과와 -fsanitize=undefined 보고를 기록하고, 안전한 shl32()/sar32() 헬퍼를 작성해 경계값 테스트한다. | mac |
| C-B04 | 비트 필드 추출·삽입: Mask와 Shift로 GET/SET_FIELD | FIELD_GET(reg, MASK, SHIFT)/FIELD_SET 매크로(또는 Linux FIELD_PREP 스타일)를 구현하고, 가상의 CTRL 레지스터(모드 3비트, 분주 4비트 등)에 대해 다른 필드를 건드리지 않음을 테이블 테스트로 확인한다. | mac |
| C-B05 | 포인터 기초와 포인터 산술 | uint8_t*/uint32_t*/void*로 같은 버퍼를 순회하며 주소 증가량을 출력하고, 이중 포인터로 연결 리스트 head를 수정하는 함수와 포인터 swap 함수를 구현해 테스트한다. | mac |
| C-B06 | 배열 vs 포인터: Decay, sizeof, 다차원 배열 | 함수 인자로 넘긴 배열의 sizeof 함정을 재현하고, 포인터를 넣으면 컴파일 에러가 나는 ARRAY_SIZE 매크로(__builtin_types_compatible_p 활용)와 2차원 배열/배열 포인터를 받는 함수를 구현한다. | mac |
| C-B07 | 문자열 기본 구현: strlen strcpy strcmp strncpy | my_strlen/my_strcpy/my_strcmp/my_strncpy/my_strlcpy를 구현하고 libc 결과와 대조하는 테스트(빈 문자열, 정확히 n 길이, 버퍼 끝 경계)를 ASan과 함께 돌린다. | mac |
| C-B08 | const 정확히 쓰기: const T* vs T* const | const 조합 4가지에 대해 컴파일 실패해야 하는 코드를 만들어 에러를 확인하고, 큰 lookup 테이블을 const 유무로 바꿔가며 size/nm으로 .rodata↔.data 이동을 관찰한다. | mac |
| C-B09 | volatile의 의미와 한계 | 플래그 폴링 루프를 volatile 유무로 -O2 컴파일해 clang -S 결과에서 루프가 사라지는 것을 확인하고, volatile 카운터 증가가 원자적이지 않음을 두 스레드 테스트로 보인다. | mac |
| C-B10 | static과 Linkage: 파일 스코프·함수 내 static·extern | 3개 파일 프로젝트에서 static 전역, extern 선언, 함수 내 static 카운터, tentative definition을 만들고 nm으로 심볼 가시성(t/T/b/D)을 확인하며 이름 충돌을 재현·해결한다. | mac |
| C-B11 | 변수는 어디에 사는가: text data bss rodata stack heap | 전역 초기화/비초기화/const/static/지역/malloc 변수의 주소를 출력해 영역을 분류하고, size로 섹션 크기 변화를 측정해 예측과 비교한다. | mac |
| C-B12 | 구조체 기초: 패딩·정렬·sizeof·offsetof | 필드 순서가 다른 구조체 6개의 sizeof/offsetof를 먼저 손으로 계산한 뒤 _Static_assert로 고정하고, 필드 재배열로 크기를 최소화한 버전을 만든다. | mac |
| C-B13 | 엔디안 판별과 바이트 스왑 | 런타임·컴파일타임 엔디안 판별 함수와 bswap16/32/64를 시프트 방식으로 구현하고, __builtin_bswap32 결과 및 메모리 덤프와 대조 테스트한다. | mac |
| C-B14 | 연산자 우선순위와 평가 순서 함정 | if (reg & MASK == VAL), a << 2 + 1, i = i++ 등 12개 퀴즈 스니펫의 동작을 예측·실행·경고(-Wparentheses, -Wunsequenced)로 확인하고 올바른 형태로 수정한다. | mac |
| C-B15 | 매크로 기초: 괄호·do-while(0)·다중 평가 | SQUARE, MAX, SWAP, 다중 문장 매크로를 순진하게 작성해 버그(우선순위, if-else 깨짐, MAX(i++, j))를 재현하고, 괄호·do{}while(0)·GNU statement expression으로 수정해 테스트한다. | mac |
| C-M01 | 메모리 맵 레지스터 접근: volatile 포인터와 레지스터 구조체 | 가상 UART(DR, SR, CR1, BRR, 예약 영역 포함)를 #define 주소 방식과 volatile 구조체 오버레이 방식으로 정의하고, 호스트에서는 정적 배열을 베이스로 써서 offsetof/_Static_assert로 오프셋을 검증한다. | mac |
| C-M02 | Read-Modify-Write 위험과 W1C 레지스터 | 쓰기 의미론(W1C, RO, RW)을 흉내 내는 레지스터 모델 함수(reg_write 훅)를 만들고, 상태 레지스터를 |= 로 갱신했을 때 다른 pending 플래그가 지워지는 버그를 재현·수정한다. | mac |
| C-M03 | 비트필드 vs 마스크/시프트: 이식성 분석 | 같은 레지스터를 bitfield 구조체와 mask/shift로 정의해 clang(호스트)과 --target=armv7em-none-eabi 에서 바이트 레이아웃을 비교하고, bitfield 쓰기가 생성하는 RMW 어셈블리를 확인해 사용 기준을 정리한다. | mac |
| C-M04 | Packed 구조체와 비정렬 접근 | 바이트 스트림에서 프로토콜 헤더(1+2+4바이트 필드)를 packed 구조체 캐스팅과 memcpy 방식으로 각각 파싱하고, -fsanitize=alignment 보고와 생성 어셈블리(비정렬 로드)를 비교한다. | mac |
| C-M05 | 엔디안 독립 직렬화: get_le16 / put_be32 | get/put_{le,be}{16,32,64} 헬퍼로 텔레메트리 패킷(헤더·페이로드·CRC 자리)을 직렬화/역직렬화하고, 고정 바이트 벡터와 왕복(round-trip) 테스트로 검증한다. | mac |
| C-M06 | 함수 포인터와 콜백 테이블 | 문자열 명령 → 핸들러 디스패치 테이블과, Linux file_operations 스타일의 driver_ops{init, read, write} 구조체로 두 개의 mock 디바이스를 같은 인터페이스로 구동하는 코드를 작성해 테스트한다. | mac |
| C-M07 | 테이블 기반 상태 머신: State × Event | 버튼 디바운스(또는 간단한 프레임 수신기) FSM을 state×event 전이 테이블과 entry/exit 액션으로 구현하고, 이벤트 시퀀스 입력 → 기대 상태/출력 테이블 테스트로 검증한다. | mac |
| C-M08 | memcpy / memmove / memset 직접 구현 | 바이트 버전 → 정렬 맞춘 워드 단위 버전으로 memcpy/memmove/memset을 구현하고, 랜덤 길이·오프셋·겹침 방향 퍼즈 테스트로 libc와 대조, 벤치마크로 속도를 비교한다. | mac |
| C-M09 | strtol / atoi 구현: 오버플로와 에러 처리 | my_strtol(s, &end, base)을 base 0/2/8/10/16, 앞 공백, 0x 접두사, LONG_MIN/MAX 경계와 ERANGE까지 구현하고 libc strtol과 수백 케이스 대조 테스트한다. | mac |
| C-M10 | itoa와 미니 printf: UART 로깅용 | INT_MIN을 올바로 처리하는 itoa(임의 base)와 %d %u %x %s %c %% 및 폭/0패딩을 지원하는 mini_snprintf를 va_list로 구현하고, snprintf 결과와 대조 테스트한다. | mac |
| C-M11 | 매크로 vs inline 함수 vs _Generic | C-B15의 매크로들을 static inline과 _Generic 타입별 디스패치로 재작성하고, -O0/-O2 어셈블리와 코드 크기를 비교해 언제 매크로가 여전히 필요한지(문자열화, __LINE__) 정리한다. | mac |
| C-M12 | 정의되지 않은 동작(UB) 카탈로그 | signed overflow 루프, NULL 체크 제거, 초기화 안 된 변수, 배열 범위 밖 접근 등 8가지를 -O0/-O2 결과 차이와 -fsanitize=undefined,address 보고로 재현하고 각각 안전한 코드로 고친다. | mac |
| C-M13 | Strict Aliasing과 Type Punning | float 비트 추출과 버퍼→uint32_t 캐스팅을 포인터 캐스트·union·memcpy로 각각 구현해 -O2 -fstrict-aliasing에서 깨지는 예제를 만들고, memcpy 버전이 동일 성능으로 컴파일됨을 어셈블리로 확인한다. | mac |
| C-M14 | 인터럽트 안전 코드: 공유 변수와 원자성 | SIGALRM 핸들러를 ISR로 보고 메인 루프와 64비트 타임스탬프·구조체를 공유해 찢어진 읽기를 재현한 뒤, volatile sig_atomic_t, 시그널 마스킹(critical section), 재읽기 루프로 각각 고쳐 검증한다. | mac |
| C-M15 | 드라이버 스타일 에러 처리: 반환 코드와 goto cleanup | 클럭→GPIO→DMA→IRQ 4단계 mock 초기화 함수를 goto 언와인딩 패턴과 일관된 에러 코드 enum으로 작성하고, 각 단계 실패를 주입하는 테스트로 자원 누수가 없음을 확인한다. | mac |
| C-A01 | 호스트 테스트 가능한 레지스터 접근 계층 설계 | REG_READ/REG_WRITE 추상화를 빌드 옵션으로 전환되게 만들고, 호스트 mock이 쓰기 시퀀스를 기록하도록 해 SPI 드라이버 init이 기대 레지스터 순서·값을 쓰는지 단위 테스트한다. | mac |
| C-A02 | 컴파일러 배리어와 메모리 순서: volatile만으론 부족한 이유 | DMA 디스크립터에 데이터를 채우고 OWN 비트를 세우는 코드에서 asm volatile("":::"memory"), atomic_signal_fence, atomic_thread_fence 유무에 따른 -O2 명령 순서를 비교하고 올바른 버전을 근거와 함께 작성한다. | mac |
| C-A03 | C11 Atomics: 플래그·카운터·acquire/release | atomic_flag 스핀락, fetch_add 카운터, release-store/acquire-load 데이터 게시(publish)를 pthread로 구현하고 TSan으로 relaxed 오용 버전의 레이스가 검출됨을 확인한다. | mac |
| C-A04 | 재진입성과 스레드 안전: strtok vs strtok_r | 정적 상태를 쓰는 my_strtok과 재진입 가능한 my_strtok_r을 구현해 중첩 토크나이징 테스트로 차이를 보이고, 제시된 함수 목록(printf, malloc, 정적 버퍼 itoa 등)을 ISR-safe 여부로 분류한다. | mac |
| C-A05 | 정렬 보장 할당: aligned_malloc과 DMA 버퍼 | malloc 과할당 + 원래 포인터 저장 방식으로 aligned_malloc/aligned_free를 구현하고 정렬 2^k(8~4096) 랜덤 테스트, 4KB 경계를 넘지 않는 DMA 버퍼 할당 조건까지 검증한다. | mac |
| C-A06 | offsetof와 container_of 구현 | 표준 offsetof를 쓰지 않는 my_offsetof와 타입 검사가 포함된 container_of 매크로를 구현하고, 여러 멤버 위치에서 원래 구조체 주소가 복원되는지 테스트한다 (DS 트랙 intrusive list의 기반). | mac |
| C-A07 | X-Macro로 에러코드·레지스터 테이블 생성 | 에러 코드 목록과 레지스터 맵(이름, 오프셋, 리셋값, 접근속성)을 X-macro로 정의해 enum, err_to_str(), 레지스터 덤프 함수를 생성하고, 항목 추가 시 모든 테이블이 동기화됨을 테스트한다. | mac |
| C-A08 | 컴파일 타임 계약: _Static_assert와 BUILD_BUG_ON | 와이어 프로토콜 구조체·레지스터 블록·enum 범위·배열 길이 일치에 대한 정적 검사를 작성하고, 일부러 필드를 추가/재배열해 빌드가 의도대로 실패하는지 확인한다. | mac |
| C-A09 | 고정소수점 맛보기: Q15 곱셈과 포화 연산 | Q15 변환, 곱셈(반올림 포함), 포화 덧셈을 구현하고 double 기준값과의 오차·포화 경계(-1 × -1) 테스트를 작성한다. 심화는 DSP 트랙으로 넘긴다. | mac |
| C-A10 | MISRA 스타일 정적 검사와 리팩터링 | 암시적 변환, 부작용 있는 매크로 인자, switch fall-through, 재귀 등이 섞인 300줄 드라이버를 -Weverything과 clang-tidy(bugprone/cert 체크)로 분석해 주요 MISRA 규칙 위반을 분류·수정하고 동작 동일성을 테스트로 보장한다. | mac |
| C-A11 | 면접형 코드 리뷰: ISR·드라이버 버그 찾기 | volatile 누락, ISR 내 printf/malloc, 비원자적 공유 플래그, 부호 있는 shift, 버퍼 오버런, 잘못된 RMW가 숨은 스니펫 5개를 15분 타이머로 리뷰하고, 각 버그를 재현하는 테스트와 수정본을 작성한다. | mac |

### ARCH — 컴퓨터 구조 & 하드웨어 기초 (Computer Architecture & HW Basics)

펌웨어 엔지니어는 CPU·캐시·버스·인터럽트·DMA가 실제로 어떻게 움직이는지 알아야 성능 문제와 '가끔 깨지는' 버그를 설명할 수 있다. 이 트랙은 수 체계부터 파이프라인, 캐시, AXI, MMU, 전력까지 각 개념을 작은 시뮬레이터나 M2 실측 실험으로 직접 만들어 확인한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| ARCH-B01 | 수 체계: 2진·16진·2의 보수 변환기 | n비트(1~64) 값을 2/10/16진으로 변환하고 sign_extend(value, bits)를 구현해, 경계값(최소/최대/-1) 테이블 테스트와 손계산 문제 10개로 검증한다. | mac |
| ARCH-B02 | ALU 플래그 계산기: N Z C V | 8/32비트 ADD/SUB/ADC 결과와 NZCV를 계산하는 함수를 작성하고, 부호/무부호 비교 조건(LT, LO 등)을 플래그 조합으로 판정하는 테이블을 만들어 검증한다. | mac |
| ARCH-B03 | IEEE 754 부동소수점 분해기 | float32와 FP16 비트 패턴을 분해/조립하는 함수와 float→FP16 변환(round-to-nearest-even)을 구현하고, 0.1 표현 오차·비정규수·NaN 케이스를 하드웨어 변환 결과와 대조한다. | mac |
| ARCH-B04 | 게이트 수준 가산기 시뮬레이터 | 게이트 함수만으로 8비트 리플 캐리 가산기와 캐리 룩어헤드 가산기를 만들고, 전수(65536쌍) 테스트로 검증하며 게이트 지연 단계 수를 비교한다. | mac |
| ARCH-B05 | 조합 논리: MUX·디코더·우선순위 인코더 | 4:1 MUX, 3-to-8 디코더, 8-input 우선순위 인코더를 진리표 기반으로 구현하고 전수 테스트하며, 우선순위 인코더가 CLZ 명령과 같은 역할임을 확인한다. | mac |
| ARCH-B06 | 순차 논리: 플립플롭·카운터·LFSR | D 플립플롭 기반 4비트 동기 카운터, 시프트 레지스터, 16비트 LFSR(최대 주기 다항식)을 클럭 루프로 시뮬레이션하고 LFSR 주기가 65535임을 검증한다. | mac |
| ARCH-B07 | 하드웨어 FSM: Moore vs Mealy 시퀀스 검출기 | 입력 비트열에서 1011(겹침 허용)을 찾는 Moore/Mealy FSM을 각각 구현해 사이클별 출력 파형을 텍스트로 출력하고, 출력 지연 1사이클 차이를 테스트로 확인한다. | mac |
| ARCH-B08 | 미니 ISA 설계: 명령어 인코딩·디코딩 | 레지스터 8개, 16비트 고정 길이 toy ISA(ADD, ADDI, LD, ST, BEQ, JAL)를 설계해 어셈블러(텍스트→기계어)와 디스어셈블러를 작성하고 왕복 테스트한다. | mac |
| ARCH-B09 | 단일 사이클 CPU 시뮬레이터 | ARCH-B08 ISA를 실행하는 인터프리터를 만들어 배열 합·피보나치 프로그램을 돌리고, 레지스터/메모리 최종 상태를 기대값과 비교하며 명령어 수를 계측한다. | mac |
| ARCH-B10 | 평균 메모리 접근 시간(AMAT) 계산기 | L1/L2/L3/DRAM 지연과 계층별 히트율을 입력받아 AMAT와 CPI 영향을 계산하는 도구를 작성하고, 히트율 1% 변화가 어느 계층에서 가장 큰 차이를 만드는지 민감도 표를 만든다(포인터 체이싱 실측은 see:MEM-B10). | mac |
| ARCH-B11 | 메모리 맵과 주소 디코더 모델 | Flash·SRAM·GPIO·UART 영역을 가진 메모리 맵 테이블과, 주소를 받아 디바이스 모델의 read/write 콜백으로 라우팅하는 디코더를 구현하고 미매핑·정렬 위반 접근 시 bus fault를 보고하게 테스트한다. | mac |
| ARCH-B12 | 버스 트랜잭션 기초: 대기 상태와 처리량 | 주소/데이터 단계와 wait state를 가진 단순 동기 버스를 사이클 시뮬레이션해 Flash(3 WS)·SRAM(0 WS) 접근의 대역폭을 계산하고, 플래시 프리페치 버퍼 유무를 비교한다. | mac |
| ARCH-B13 | 타이밍 기초: setup/hold와 최대 클럭 주파수 | Tclk-q, 조합 지연(min/max), setup, hold, 클럭 스큐를 입력받아 setup/hold slack과 Fmax를 계산하는 함수를 작성하고, 스큐가 setup은 돕고 hold는 해치는 사례를 테스트로 보인다(MCU 클럭 트리 설정은 see:BM-B10). | mac |
| ARCH-B14 | 리셋 종류와 리셋 원인 디코딩 | 리셋 원인 레지스터 모델(sticky 비트, 클리어 방식 포함)을 만들고 부팅 시 원인을 판별·로그·클리어하는 코드를 작성해 여러 리셋 시나리오 시퀀스로 테스트한다. | mac |
| ARCH-M01 | 5단 파이프라인 시뮬레이터: 데이터 해저드와 스톨 | ARCH-B09 CPU를 5단 파이프라인으로 확장해 RAW 해저드를 탐지하고 스톨을 삽입하며, 사이클별 파이프라인 다이어그램 출력과 CPI 측정으로 단일 사이클 결과와 일치함을 검증한다. | mac |
| ARCH-M02 | 포워딩·로드-유즈 해저드·분기 예측기 | M01 시뮬레이터에 EX/MEM 포워딩, load-use 1사이클 스톨, 정적 not-taken vs 2비트 포화 카운터 예측기를 추가하고 벤치 프로그램 3개의 CPI를 표로 비교한다. | mac |
| ARCH-M03 | 분기 예측 실측: 정렬 데이터 vs 랜덤 데이터 | 임계값 필터 루프를 정렬/랜덤 입력으로 M2에서 측정하고, CSEL/마스크 기반 branchless 버전과 비교해 미스당 비용(ns)을 추정한다 (clang -S로 분기 여부 확인). | mac |
| ARCH-M04 | 직접 사상 캐시 시뮬레이터 | 라인 크기·용량을 파라미터로 받는 direct-mapped 캐시 시뮬레이터를 작성해 주소 트레이스(순차, stride, 행렬 순회)를 넣고 hit rate와 conflict miss 사례를 분석한다. | mac |
| ARCH-M05 | 집합 연관 캐시와 교체 정책 | M04를 N-way 집합 연관으로 확장하고 교체 정책 4종을 구현해 같은 트레이스에서 3C(compulsory/capacity/conflict) 미스 분류와 함께 비교 표를 만든다. | mac |
| ARCH-M06 | 쓰기 정책: Write-back vs Write-through | 캐시 시뮬레이터에 WB/WT × allocate/no-allocate 4조합과 write buffer를 구현해, memset·로그 버퍼 쓰기 트레이스에서 DRAM 쓰기 바이트 수를 비교한다. | mac |
| ARCH-M07 | 캐시 친화 코드: 루프 순서와 타일링 | 행렬 곱(512~2048)을 ijk/ikj/타일링 버전으로 구현해 M2에서 GFLOP/s를 측정하고, M04/M05 시뮬레이터에 축소 트레이스를 넣어 미스율 차이로 결과를 설명한다. | mac |
| ARCH-M08 | False Sharing 측정 | 두 스레드가 인접 카운터를 증가시키는 벤치를 패딩 없음/64B/128B 정렬로 측정해 M2 캐시 라인 크기(128B)를 실험적으로 추정하고, 구조체 설계 가이드를 정리한다. | mac |
| ARCH-M09 | 인터럽트 컨트롤러 모델: 우선순위·마스킹·Pending | IRQ 32개, 우선순위, 전역 마스크, pending/active 비트를 가진 NVIC 유사 모델을 만들어 중첩 인터럽트 시나리오를 이벤트 시뮬레이션하고 핸들러 실행 순서를 기대 로그와 비교한다. | mac |
| ARCH-M10 | 예외와 트랩: 벡터 테이블과 복귀 주소 | toy CPU에 잘못된 명령·0 나누기·미정렬 접근 예외와 소프트웨어 트랩(SYSCALL)을 추가하고, 벡터 테이블 → 핸들러 → 복귀(재실행 vs 다음 명령) 동작을 테스트 프로그램으로 검증한다. | mac |
| ARCH-M11 | DMA 컨트롤러 모델: 디스크립터 체인 | src/dst/len/next/flags 디스크립터 연결 리스트를 처리하는 DMA 엔진 모델을 만들어 mem→mem, 주변장치→mem 전송과 완료/에러 인터럽트를 시뮬레이션하고 결과 버퍼를 검증한다. | mac |
| ARCH-M12 | DMA와 캐시 일관성: Clean/Invalidate 시나리오 | write-back 캐시 + DMA 모델에서 TX(clean 누락)·RX(invalidate 누락)·비정렬 버퍼로 인한 인접 데이터 손상 버그를 재현하고, 올바른 clean/invalidate 순서와 라인 정렬 규칙으로 수정한다. | mac |
| ARCH-M13 | AHB/APB 개념: 파이프라인 버스와 브리지 | AHB(HTRANS, HREADY)와 APB(PSEL/PENABLE/PREADY) 신호를 사이클 단위로 모델링해 연속 접근의 파형을 출력하고, AHB→APB 브리지를 거친 레지스터 접근 지연을 계산한다. | mac |
| ARCH-M14 | AXI 핸드셰이크 모델: 5채널과 VALID/READY | AW/W/B/AR/R 채널 VALID/READY 핸드셰이크와 INCR 버스트, 다중 ID out-of-order 응답을 시뮬레이션하고, 프로토콜 규칙(VALID 유지, WLAST, 같은 ID 순서) 위반을 잡는 체커를 작성한다. | mac |
| ARCH-M15 | MPU 영역 계산기와 접근 검사 | 영역 8개(2^n 크기, 크기 정렬, 서브리전 disable, AP/XN)를 받아 주소·접근 종류별 허용 여부를 판정하는 검사기를 구현하고, 스택 가드 영역과 겹침 우선순위 테스트로 검증한다. | mac |
| ARCH-A01 | MMU 페이지 테이블 워커 시뮬레이터 | 4KB 페이지, 2~4단계 테이블(블록 매핑 포함)을 가진 VA→PA 변환기를 작성해 R/W/X·user/kernel 권한 위반과 미매핑 폴트를 구분하여 보고하고, 매핑 생성 API로 테스트한다. | mac |
| ARCH-A02 | TLB 모델과 ASID·플러시 비용 | A01에 완전 연관 TLB(크기·교체 정책 파라미터)와 ASID를 추가해 두 프로세스 교대 실행 트레이스에서 전체 플러시 vs ASID 태깅의 TLB 미스 수와 페이지 워크 횟수를 비교한다. | mac |
| ARCH-A03 | 가상 메모리 실측: 페이지 폴트와 TLB 미스 비용 | mmap 영역을 페이지 단위 stride로 첫 접근/재접근 시간을 측정해 폴트 비용을 추정하고, 페이지 수가 TLB 커버리지를 넘을 때의 지연 증가를 그래프로 확인한다. | mac |
| ARCH-A04 | MESI 캐시 일관성 프로토콜 시뮬레이터 | 코어 4개 + 공유 버스 MESI 시뮬레이터를 작성해 읽기/쓰기 트레이스의 상태 전이와 버스 메시지를 출력하고, 'M 상태 복사본은 최대 1개' 같은 불변식을 매 사이클 검사한다. | mac |
| ARCH-A05 | 메모리 순서 리트머스 테스트 on Apple M2 | SB/MP/LB 리트머스 테스트를 두 스레드로 수백만 회 반복 실행해 '불가능해 보이는' 결과의 빈도를 집계하고, C11 fence/acquire-release 추가 후 사라짐을 확인한다. | mac |
| ARCH-A06 | 비순차 실행 개념 모델: 스코어보드와 ROB | 간소화한 Tomasulo(예약 스테이션 + ROB + 리네이밍) 시뮬레이터로 지연이 다른 명령 시퀀스를 실행해 issue/execute/commit 타임라인을 출력하고, 예외 발생 시 commit 순서가 보존됨을 확인한다. | mac |
| ARCH-A07 | 클럭 도메인 교차: 동기화기와 Gray 코드 FIFO | 서로 다른 클럭 비율의 두 도메인을 이벤트 시뮬레이션해 이진 포인터 직접 전달 시 잘못된 full/empty 판정을 재현하고, Gray 코드 + 2-FF 동기화 포인터로 수정한 비동기 FIFO를 검증한다. | mac |
| ARCH-A08 | 전력 모델: DVFS와 Race-to-Idle 에너지 계산 | P = αCV²f + 누설 모델과 OPP 테이블로 동일 작업의 '고클럭 후 슬립' vs '저클럭 지속' 에너지를 계산하는 도구를 만들고, 듀티 사이클 센서 노드의 배터리 수명을 추정한다. | mac |
| ARCH-A09 | 파워·클럭·리셋 도메인 시퀀싱 설계 | 레일 → PLL lock → 클럭 enable → 리셋 해제 → 주변장치 init 의존성 그래프를 정의하고 이를 따르는 power-up/down 시퀀서 FSM과 순서 위반 검출 체커를 작성해 오류 주입 테스트를 한다. | mac |
| ARCH-A10 | 버스 중재: 고정 우선순위 vs 라운드로빈 | CPU·DMA·NPU 3마스터 요청 트래픽을 생성해 고정 우선순위·라운드로빈·가중 RR 중재기를 시뮬레이션하고, 마스터별 평균/최악 지연과 대역폭 점유를 표로 비교한다. | mac |
| ARCH-A11 | 성능 모델: Amdahl과 Roofline | M2의 피크 FLOP/s와 메모리 대역폭(STREAM 유사 벤치로 측정)으로 루프라인을 만들고, 벡터 덧셈·행렬 곱·conv1d의 측정 성능을 찍어 메모리/연산 바운드를 판정한다. | mac |
| ARCH-A12 | SoC 설계 연습: 메모리 맵과 인터커넥트 | 엣지 AI SoC 요구사항(카메라 입력, NPU, 2개 CPU 클러스터)을 받아 주소 맵·AXI/APB 계층·일관성 도메인을 설계 문서로 작성하고, B11 디코더 모델로 주소 맵 겹침/정렬을 자동 검사한다. | mac |

### ARM — ARM 어셈블리 (ARM Assembly)

ARM은 MCU(Cortex-M)부터 애플리케이션 프로세서·AI SoC(AArch64)까지 임베디드의 기본 ISA다. 면접에서는 어셈블리 판독, 호출 규약, 예외 진입, 원자 연산·배리어 같은 질문이 자주 나온다. M2 맥에서 AArch64를 네이티브로 작성·실행하고, Cortex-M 고유 동작은 QEMU로 확인한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| ARM-B01 | 첫 AArch64 함수: C에서 호출하는 .S 파일 | add3, max, sum_array를 .S 어셈블리로 작성해 C 테스트 하네스에서 호출·검증하고, Apple ARM64 ABI가 표준 AAPCS64와 다른 점을 목록으로 정리한다. | mac |
| ARM-B02 | clang -S로 C→AArch64 어셈블리 읽기 | 루프·switch·구조체 접근·함수 호출이 있는 C 함수 6개를 -O0/-O2로 컴파일해 각 명령에 주석을 달고, -O2에서 사라지거나 바뀐 부분을 설명한다. | mac |
| ARM-B03 | 데이터 이동과 즉시값: MOV, MOVZ/MOVK, 논리 즉시값 | 임의 64비트 상수를 MOVZ/MOVK 시퀀스로 만드는 어셈블리를 작성하고, AND/ORR 논리 즉시값으로 인코딩 가능한 패턴인지 판정하는 C 함수를 만들어 어셈블러 에러와 대조한다. | mac |
| ARM-B04 | 로드/스토어와 주소 지정 모드 | 배열 합·배열 복사·구조체 배열 필드 합을 각 주소 지정 모드로 구현하고, 같은 결과를 내는 C 버전과 랜덤 테스트로 대조한다. | mac |
| ARM-B05 | 조건 플래그와 분기: CMP, B.cond, CSEL | max/min/abs/clamp/sign 함수를 분기 버전과 CSEL/CSINC/CSNEG branchless 버전으로 작성해 경계값 테스트하고, 부호/무부호 비교(GT vs HI)를 잘못 쓴 버그를 재현한다. | mac |
| ARM-B06 | 비트 연산 명령: BIC, UBFX, BFI, CLZ, RBIT | 레지스터 필드 추출/삽입, 비트 반전, 최상위 비트 위치, 니블 역순을 UBFX/BFI/RBIT/CLZ/REV로 구현하고 C-B04 결과와 대조 테스트한다. | mac |
| ARM-B07 | 곱셈·나눗셈: MADD, SMULL, UMULH, SDIV | UMULH/MUL로 128비트 곱을 구하는 함수와 MSUB를 이용한 나머지 연산을 작성해 __int128 결과와 대조하고, SDIV의 0 나누기·INT_MIN/-1 결과를 측정해 x86과의 차이를 정리한다. | mac |
| ARM-B08 | 스택 프레임과 재귀: FP/LR 저장 | 재귀 factorial·fibonacci와 지역 배열을 쓰는 함수를 어셈블리로 작성하고, lldb로 각 프레임의 FP 체인을 따라가며 스택 레이아웃 그림과 일치함을 확인한다. | mac |
| ARM-B09 | lldb로 명령어 단위 실행과 레지스터 관찰 | 일부러 off-by-one 버그를 넣은 어셈블리 strlen을 lldb에서 명령어 단위로 실행하며 레지스터·플래그·메모리 변화를 기록해 버그를 찾아 고친다. | mac |
| ARM-B10 | 어셈블리로 strlen·memcpy 구현 | 바이트 단위와 정렬 후 16바이트 단위 memcpy, 바이트 단위 strlen을 어셈블리로 작성해 랜덤 길이·정렬 퍼즈 테스트와 libc 대비 벤치마크를 수행한다. | mac |
| ARM-B11 | 부동소수점·SIMD 레지스터 기초: V/D/S, FCVT | float 인자를 받는 dot3, lerp, float→int 포화 변환 함수를 어셈블리로 작성해 C 결과와 대조하고, FCVTZS의 NaN/오버플로 처리 결과를 확인한다. | mac |
| ARM-B12 | Thumb-2 인코딩 관찰: 16비트 vs 32비트 명령 | 같은 C 파일을 --target=thumbv7em-none-eabi, armv7a(ARM 모드), aarch64로 -Os 컴파일해 objdump로 명령 길이 분포와 .text 크기를 비교 표로 정리한다. | mac |
| ARM-B13 | Thumb-2 어셈블리 작성과 IT 블록 | max, abs, 배열 합을 Thumb-2 어셈블리(IT 블록 포함)로 작성해 qemu-system-arm(mps2-an385) + 세미호스팅에서 실행하고 결과를 출력으로 검증한다. | **qemu** |
| ARM-B14 | Cortex-M 레지스터 모델: MSP/PSP, xPSR, CONTROL | MRS/MSR로 MSP·PSP·xPSR·CONTROL을 읽는 함수를 작성하고 Thread 모드에서 PSP로 전환하는 코드를 QEMU에서 실행해 gdb로 레지스터 변화를 확인한다. | **qemu** |
| ARM-M01 | AAPCS64 심화: 많은 인자·구조체 반환·callee-saved | 인자 10개, 16바이트 초과 구조체 반환, float 4개짜리 HFA 인자를 다루는 어셈블리 함수를 작성하고, x19–x28/d8–d15 보존 여부를 검사하는 C 하네스로 검증한다 (Apple 가변인자 차이 포함). | mac |
| ARM-M02 | C 인라인 어셈블리: 제약조건과 Clobber | rbit, clz, 캐리 포함 128비트 덧셈(ADDS/ADC), CNTVCT_EL0 읽기를 인라인 asm 래퍼로 작성하고, clobber 누락 시 -O2에서 값이 깨지는 사례를 재현해 고친다. | mac |
| ARM-M03 | 시스템 카운터 기반 마이크로벤치 하네스 | ISB로 직렬화한 카운터 읽기 하네스를 만들고 의존성 체인으로 ADD/MUL/SDIV/LDR(L1 hit) 지연을, 독립 명령으로 처리량을 측정해 표로 정리한다. | mac |
| ARM-M04 | LDXR/STXR로 원자 연산 구현 (AArch64) | LDAXR/STLXR 루프로 atomic_add·CAS·스핀락을 구현하고 pthread 스트레스 테스트로 정확성을 확인한 뒤, LSE의 LDADDAL/CASAL 버전과 경합 상황 성능을 비교한다. | mac |
| ARM-M05 | 메모리 배리어 명령: DMB, DSB, ISB, LDAR/STLR | Message Passing 리트머스 테스트를 일반 LDR/STR, DMB ISH, LDAR/STLR 버전으로 어셈블리 작성해 M2에서 재배치 관찰 빈도를 비교하고, DSB/ISB가 필요한 상황(시스템 레지스터·코드 수정)을 정리한다. | mac |
| ARM-M06 | NEON 기초: 벡터 덧셈·내적·리덕션 | float32 벡터 덧셈·내적과 int8 내적(SDOT)을 스칼라/intrinsic/어셈블리 3버전으로 작성해 정확성 테스트 후 처리량(GOPS)을 측정한다. | mac |
| ARM-M07 | NEON 바이트 처리: memchr·히스토그램·체크섬 | NEON으로 memchr(CMEQ + 마스크 추출), 바이트 합 체크섬, 16비트 ADC 샘플 최소/최대 탐색을 구현해 스칼라 대비 속도와 꼬리(tail) 처리 정확성을 검증한다. | mac |
| ARM-M08 | Cortex-M 벡터 테이블과 Thumb 비트 | 초기 SP·Reset·NMI·HardFault·SysTick 엔트리를 가진 벡터 테이블을 어셈블리로 작성해 QEMU에서 부팅하고, Thumb 비트를 일부러 지워 발생하는 UsageFault(INVSTATE)를 관찰한다. | **qemu** |
| ARM-M09 | Cortex-M 예외 진입/복귀: 스택 프레임과 EXC_RETURN | SysTick과 SVC 예외를 발생시켜 gdb로 스택된 r0–r3,r12,LR,PC,xPSR 프레임을 덤프하고, MSP/PSP·FPU 사용 여부에 따른 EXC_RETURN 값을 디코딩하는 함수를 작성한다. | **qemu** |
| ARM-M10 | PRIMASK·BASEPRI로 크리티컬 섹션 구현 | PRIMASK 저장/복원 방식의 중첩 가능 enter/exit_critical과 BASEPRI 기반 우선순위 마스킹을 작성하고, QEMU에서 SysTick ISR과 공유 카운터 테스트로 lost update가 사라짐을 확인한다. | **qemu** |
| ARM-M11 | Cortex-M LDREX/STREX와 Exclusive Monitor | LDREX/STREX로 atomic_inc·CAS·뮤텍스 try_lock을 Thumb-2로 구현하고, ISR이 끼어들 때 STREX가 실패·재시도하는 경로를 gdb 브레이크포인트로 강제해 검증한다. | **qemu** |
| ARM-M12 | HardFault 분석기: CFSR/HFSR 디코딩과 PC 복원 | MSP/PSP를 판별해 스택 프레임을 C 함수에 넘기는 HardFault 어셈블리 진입부와 CFSR/HFSR/MMFAR/BFAR 디코더를 작성하고, 0 나누기·미정렬·잘못된 주소·undefined 명령 폴트를 유발해 원인과 PC가 맞게 보고되는지 확인한다. | **qemu** |
| ARM-M13 | AArch64 예외 레벨과 시스템 레지스터 | qemu-system-aarch64 virt에서 EL1로 부팅하는 최소 코드에서 CurrentEL을 출력하고, VBAR_EL1 벡터 테이블을 설정해 SVC 동기 예외를 받아 ESR_EL1을 디코딩한 뒤 ERET으로 복귀한다. | **qemu** |
| ARM-M14 | 크래시 레지스터 덤프로 원인 역추적 | NULL 구조체 멤버 접근, 스택 오버런으로 LR 손상, 해제 후 사용 크래시의 레지스터 덤프와 디스어셈블리를 만들어(lldb) 각각 원인 소스 라인을 역추적하는 풀이를 작성한다. | mac |
| ARM-A01 | PendSV 컨텍스트 스위치 어셈블리 (Cortex-M) | SysTick이 PendSV를 pend하고 PendSV 핸들러가 r4–r11 저장·PSP 교체·EXC_RETURN 복귀를 수행하는 최소 스위처를 작성해 QEMU에서 두 태스크의 교대 출력과 레지스터 보존을 검증한다. | **qemu** |
| ARM-A02 | AArch64 예외 벡터와 IRQ 컨텍스트 저장 | EL1 벡터 테이블에서 sync/IRQ 진입 시 전체 레지스터 프레임을 저장하는 매크로를 작성하고, 제네릭 타이머 IRQ(GIC 설정 포함)를 받아 틱을 세는 프로그램을 QEMU virt에서 검증한다. | **qemu** |
| ARM-A03 | AArch64 MMU 켜기: 변환 테이블·MAIR·TCR | QEMU virt에서 RAM은 Normal WB, UART는 Device-nGnRE로 2MB 블록 매핑 테이블을 만들고 MAIR/TCR/TTBR0/SCTLR을 설정해 MMU를 켠 뒤, 실행 금지 영역 접근 시 발생하는 폴트를 ESR로 확인한다. | **qemu** |
| ARM-A04 | I-cache 일관성: JIT 코드 생성과 캐시 유지보수 | MAP_JIT + pthread_jit_write_protect_np로 작은 함수(ADD 즉시값 변경)를 런타임에 생성·실행하고, sys_icache_invalidate 또는 직접 DC/IC 명령 시퀀스를 생략했을 때와의 차이를 기록한다. | mac |
| ARM-A05 | int8 GEMM 마이크로커널: SDOT·SMMLA | int8×int8→int32 GEMM을 스칼라 → SDOT 4×4 타일 → SMMLA(I8MM) 커널로 구현해 정확성 테스트 후 GOPS와 이론 피크 대비 효율을 측정한다. | mac |
| ARM-A06 | ARMv8 CRC32 명령으로 CRC 가속 | CRC32B/W/X 및 CRC32C 명령(intrinsic과 asm)으로 CRC-32/CRC-32C를 구현해 ALGO 트랙 테이블 구현과 결과를 대조하고, 1KB~1MB 버퍼 처리량을 비교한다. | mac |
| ARM-A07 | 포화·고정소수점 명령: SQADD, SQDMULH, SSAT | Q15 FIR 필터를 C 기준 구현, NEON SQDMULH/SQADD 버전, Thumb-2 SSAT/SMLABB(QEMU) 버전으로 작성해 비트 단위 결과 일치와 포화 경계를 테스트한다. | mac |
| ARM-A08 | Cortex-M 인터럽트 지연 분석 | 우선순위·실행시간이 주어진 IRQ 세트와 크리티컬 섹션 길이로 최악 인터럽트 지연을 계산하는 모델을 작성하고, 각 최적화(tail-chaining 등) 적용 전후 수치를 근거와 함께 표로 제시한다. | mac |
| ARM-A09 | PIC와 리로케이션: ADRP/ADD, GOT, @PAGE | 전역 접근 어셈블리를 ADRP+ADD(@PAGE/@PAGEOFF)로 작성하고, 정적/동적 라이브러리에서의 GOT 경유 접근을 otool/objdump로 비교하며 리로케이션 엔트리를 해석한다. | mac |
| ARM-A10 | 포인터 인증과 BTI: PACIASP, AUTIASP | -mbranch-protection=standard와 arm64e 타깃으로 컴파일해 PACIASP/AUTIASP/BTI 삽입 위치를 확인하고, 스택의 반환 주소를 조작했을 때 인증 실패로 크래시하는 것을 재현한다. | mac |
| ARM-A11 | 컴파일러 최적화 출력 판독: 벡터화와 언롤 | saxpy·히스토그램·조건부 합 루프를 -O2/-O3/-Os로 컴파일해 -Rpass=loop-vectorize 리포트와 어셈블리를 대조하고, 벡터화가 실패한 이유(별칭, 의존성)를 restrict 등으로 해결한다. | mac |

### TOOL — 툴체인 · 빌드 · 디버깅 (Toolchain, Build & Debugging)

펌웨어 엔지니어의 생산성은 툴체인을 얼마나 깊이 아느냐로 갈린다: 링크 에러 해석, 링커 스크립트와 맵 파일, 코드 크기 다이어트, 디버거·sanitizer·코어 덤프로 버그를 좁히는 능력. 이 트랙은 컴파일 단계부터 JTAG/SWD까지 각 도구를 실제 문제 상황에 적용해 본다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| TOOL-B01 | 컴파일 4단계 관찰: -E -S -c 링크 | 헤더·매크로·두 개 소스로 된 프로그램을 -E(.i), -S(.s), -c(.o), 링크 단계로 나눠 산출물을 만들고, 각 단계에서만 발생하는 에러(매크로/문법/심볼 미정의)를 하나씩 일부러 만들어 분류한다. | mac |
| TOOL-B02 | 경고 플래그 기준선: -Wall -Wextra -Werror 외 | 의도적 버그 12개(미초기화, 부호 비교, 섀도잉, 포맷 문자열, 암시적 변환 등)가 든 파일에 플래그를 하나씩 추가하며 어떤 경고가 어떤 버그를 잡는지 표로 만들고 권장 플래그 세트를 확정한다. | mac |
| TOOL-B03 | 최적화 레벨 비교: -O0 -O1 -O2 -O3 -Os -Oz | 벤치 프로그램을 6개 레벨로 빌드해 .text 크기와 실행시간을 표로 만들고, -O2에서 변수가 <optimized out>이 되는 디버깅 경험과 UB 코드의 동작 변화를 기록한다. | mac |
| TOOL-B04 | 오브젝트 파일 해부: nm objdump readelf otool | 같은 소스를 Mach-O(호스트)와 ELF(--target=aarch64-linux-gnu -c)로 만들어 nm/objdump -d -r/readelf -S -s/otool -l 출력에서 섹션 목록, 심볼 종류, 미해결 리로케이션을 찾아 설명한다. | mac |
| TOOL-B05 | 링크 에러 해결: undefined, multiple definition, weak | undefined reference, multiple definition(헤더 정의 변수), C/C++ 이름 맹글링 불일치, weak 심볼 기본 핸들러 덮어쓰기를 각각 재현·해결하고 nm 출력으로 근거를 남긴다. | mac |
| TOOL-B06 | size로 섹션 크기 읽기: text data bss | 전역 배열 초기화 여부·const·문자열 리터럴·printf 사용을 바꿔가며 size(-A/-m) 결과 변화를 기록해 Flash = text+data, RAM = data+bss 공식을 확인한다 (thumbv7em 크로스 빌드 포함). | mac |
| TOOL-B07 | 전처리기 활용: 조건부 컴파일과 빌드 변형 | BOARD_A/BOARD_B, DEBUG/RELEASE 조합을 -D로 빌드해 각 변형의 -E 출력과 -dM 매크로 덤프로 활성 코드 경로를 확인하고, #error로 잘못된 조합을 막는다. | mac |
| TOOL-B08 | Make 기초: 패턴 규칙·자동 변수·-MMD 의존성 | src/, include/, build/ 구조에 패턴 규칙과 -MMD -MP 의존성 자동 생성을 쓰는 Makefile을 작성하고, 헤더 하나를 touch했을 때 필요한 파일만 재컴파일되는지 make -n으로 검증한다. | mac |
| TOOL-B09 | 정적 라이브러리와 링크 순서 | 두 개의 상호 의존 .a를 만들고 ld64와 GNU ld(ld.lld 또는 Lima)에서 링크 순서·--start-group 유무에 따른 결과 차이를 재현하며, 아카이브에서 필요한 .o만 끌려옴을 nm/맵으로 확인한다. | mac |
| TOOL-B10 | 디버거 기초: breakpoint·step·backtrace·print | 연결 리스트 삭제 버그로 크래시하는 프로그램을 lldb에서 breakpoint, step/next/finish, bt, frame variable로 추적해 원인을 찾고, gdb 동등 명령표를 함께 정리한다. | mac |
| TOOL-B11 | AddressSanitizer로 메모리 버그 잡기 | heap-buffer-overflow, stack-buffer-overflow, use-after-free, double-free, 메모리 누수 5종을 만들어 -fsanitize=address 리포트의 스택 트레이스와 섀도 메모리 정보를 해석하고 수정한다. | mac |
| TOOL-B12 | UBSan으로 정의되지 않은 동작 잡기 | signed overflow, shift, 정렬, 범위 밖 enum/bool, NULL 역참조를 -fsanitize=undefined로 검출하고, -fsanitize-trap=undefined 최소 런타임 모드의 코드 크기 비용을 측정한다. | mac |
| TOOL-B13 | 컴파일 타임 제거 가능한 로그 매크로 | LOG_LEVEL에 따라 컴파일 시 완전히 사라지는 LOG_E/W/I/D 매크로와 printf 포맷 검사(__attribute__((format)))를 구현하고, 레벨별 바이너리 크기와 문자열 테이블 변화를 비교한다. | mac |
| TOOL-B14 | 맥에서 크로스 컴파일 첫걸음 | 같은 C 파일을 aarch64-linux-gnu, thumbv7em-none-eabi(-mcpu=cortex-m4 -mfloat-abi=hard), riscv32로 컴파일해 readelf -h의 ABI/플래그와 디스어셈블리를 비교하고 sysroot/libc 부재로 링크가 실패하는 이유를 설명한다. | mac |
| TOOL-M01 | 링커 스크립트 기초: MEMORY·SECTIONS·VMA/LMA | FLASH 512K/RAM 128K MEMORY와 .isr_vector/.text/.rodata/.data(AT>FLASH)/.bss 섹션을 정의한 스크립트로 Cortex-M 오브젝트를 링크하고, readelf -l/-S로 .data의 VMA≠LMA를 확인한다. | mac |
| TOOL-M02 | 맵 파일 분석기: 누가 메모리를 먹는가 | -Wl,-Map으로 생성한 맵 파일을 파싱해 섹션별·오브젝트별·심볼별 Top-N 크기 보고서를 출력하는 Python 스크립트를 작성하고, 두 빌드 간 크기 diff 기능을 추가한다. | mac |
| TOOL-M03 | 커스텀 섹션으로 자동 등록 테이블 만들기 | 매크로 하나로 등록되는 셸 명령 테이블을 커스텀 섹션에 모으고, ELF(__start_/__stop_ 또는 링커 스크립트 KEEP)와 Mach-O(section$start$) 양쪽에서 순회하는 코드를 작성해 테스트한다. | mac |
| TOOL-M04 | CMake 기초: 타깃·라이브러리·옵션 | TOOL-B08 프로젝트를 CMake로 옮겨 라이브러리/앱/테스트 타깃, PUBLIC/PRIVATE include, option()으로 기능 플래그를 정의하고 CTest로 단위 테스트를 실행한다. | mac |
| TOOL-M05 | CMake 툴체인 파일로 크로스 빌드 | arm-none-eabi 툴체인 파일(컴파일러, CPU 플래그, 링커 스크립트, try_compile 설정)을 작성해 build-host/build-target 두 디렉터리로 빌드하고, 타깃 빌드에서 .elf/.bin/.map과 size 출력을 자동 생성한다. | mac |
| TOOL-M06 | 공유 라이브러리: -fPIC, dylib/so, rpath, 가시성 | dylib를 빌드해 otool -L/install_name_tool로 rpath를 조정하고 -fvisibility=hidden + export 매크로로 공개 API만 내보낸 뒤, Lima에서 .so 버전(ldd, readelf -d, SONAME)과 비교한다. | mac |
| TOOL-M07 | 디버거 심화: 조건부 BP·워치포인트·스크립팅 | 특정 조건에서만 값이 깨지는 버그를 하드웨어 워치포인트와 조건부 브레이크포인트로 잡고, 링 버퍼 구조체를 보기 좋게 출력하는 lldb Python 명령(또는 gdb pretty-printer)을 작성한다. | mac |
| TOOL-M08 | git bisect run으로 회귀 커밋 자동 탐색 | 100개 커밋 중 하나가 성능/기능 회귀를 넣은 실습 저장소를 스크립트로 생성하고, 종료 코드 규약(0/1/125 skip)을 지키는 테스트 스크립트로 git bisect run을 돌려 원인 커밋을 찾는다. | mac |
| TOOL-M09 | ThreadSanitizer로 데이터 레이스 찾기 | 공유 카운터 레이스, 락 없이 읽는 플래그, 락 순서 역전(데드락 가능성) 3가지를 pthread로 만들어 -fsanitize=thread 리포트를 해석하고 뮤텍스/atomic으로 고친다. | mac |
| TOOL-M10 | Valgrind 대안: leaks·MallocStackLogging·ASan | 누수와 해제 후 쓰기가 있는 프로그램을 macOS leaks(MallocStackLogging=1), Guard Malloc(libgmalloc), ASan으로 각각 분석하고, Lima에서 valgrind memcheck 결과와 탐지 범위를 비교한다. | mac |
| TOOL-M11 | 코어 덤프 생성과 사후 분석 | ulimit -c와 get-task-allow 서명으로 macOS 코어 덤프를 생성해 lldb -c로 스레드별 백트레이스·지역 변수·힙 객체를 조사하고, Lima에서 gdb core 분석 흐름과 비교한다. | mac |
| TOOL-M12 | 정적 분석: clang-tidy·scan-build·cppcheck | 버그 10개가 숨은 소규모 펌웨어 코드에 세 도구를 돌려 각 도구가 잡은/놓친 버그 매트릭스를 만들고, .clang-tidy 설정과 오탐 억제 방침을 정한다. | mac |
| TOOL-M13 | 코드 크기 다이어트: gc-sections·LTO·-Os | Cortex-M 타깃 데모 펌웨어에 -Os, -ffunction-sections -fdata-sections + --gc-sections, -flto, newlib-nano, printf 제거를 차례로 적용해 단계별 .text/.data 크기 표와 맵 파일 diff를 만든다. | mac |
| TOOL-M14 | 디버그 정보와 심볼화: DWARF, strip, addr2line/atos | -g 레벨별 바이너리 크기를 비교하고, 심볼을 분리(dSYM/objcopy --only-keep-debug)한 뒤 스트립된 바이너리의 크래시 PC 목록을 atos/llvm-addr2line으로 함수:라인으로 심볼화하는 스크립트를 작성한다. | mac |
| TOOL-A01 | JTAG TAP 상태 머신 시뮬레이터 | TAP 16상태 FSM과 IR/BYPASS/IDCODE 레지스터를 가진 디바이스 2개 체인을 모델링하고, TMS 비트열로 리셋 → IDCODE 읽기 → BYPASS 스캔을 수행하는 호스트 드라이버를 작성해 체인 길이·IDCODE를 자동 검출한다. | mac |
| TOOL-A02 | SWD 프로토콜 모델: 요청·ACK·패리티·DP/AP | SWD 요청 바이트(APnDP, RnW, A[2:3], 패리티) 인코더와 ACK(OK/WAIT/FAULT) 처리, DP SELECT → MEM-AP TAR/DRW로 대상 메모리를 읽는 시퀀스를 타깃 모델과 함께 구현하고 WAIT 재시도·패리티 오류를 테스트한다. | mac |
| TOOL-A03 | GDB 원격 디버깅: QEMU gdbstub로 OpenOCD 흐름 익히기 | QEMU(-s -S)로 Cortex-M 펌웨어를 띄워 gdb에서 load, monitor 명령, 하드웨어 브레이크포인트, 레지스터/메모리 조작을 수행하고, 같은 작업을 실보드 OpenOCD(stlink + target cfg)로 할 때의 설정 파일을 작성해 대응표로 정리한다. | **qemu** |
| TOOL-A04 | 세미호스팅과 트레이스: SWO/ITM/RTT 개념 | QEMU에서 세미호스팅 printf(BKPT 0xAB)를 직접 구현해 동작시키고, ITM 스티뮬러스 포트·SEGGER RTT 링 버퍼 방식의 구조와 지연/침습성을 비교하는 표와 RTT 유사 메모리 버퍼 구현을 작성한다. | **qemu** |
| TOOL-A05 | 펌웨어 크래시 덤프 포맷 설계와 호스트 디코더 | 레지스터·스택 일부·빌드 ID·폴트 레지스터를 담는 compact 덤프 구조(CRC 포함)를 정의하고, 덤프 바이너리를 ELF 심볼로 해석해 백트레이스와 폴트 원인을 출력하는 Python 디코더를 작성한다. | mac |
| TOOL-A06 | 재현 가능한 빌드와 빌드 ID 임베딩 | git 해시·빌드 ID를 바이너리에 임베딩하고, __DATE__/절대경로 등 비결정 요소를 SOURCE_DATE_EPOCH·-ffile-prefix-map으로 제거해 두 디렉터리에서의 빌드 결과가 sha256 일치함을 확인한다. | mac |
| TOOL-A07 | 링커 스크립트 고급: RAM 함수·정렬·KEEP·ASSERT | .ramfunc 섹션(LMA=Flash, VMA=RAM)과 복사 코드, 4KB 정렬 DMA 버퍼 섹션, 벡터 테이블 KEEP, 스택+힙 크기 ASSERT를 추가한 링커 스크립트를 작성하고 맵 파일과 의도적 초과 빌드 실패로 검증한다. | mac |
| TOOL-A08 | 스택 사용량 분석: -fstack-usage와 콜 그래프 | -fstack-usage .su 파일과 콜 그래프(-fcallgraph-info 또는 objdump 파싱)로 태스크별 최악 스택을 계산하는 스크립트를 작성하고, 실행 시 스택 페인팅(0xDEADBEEF) 측정값과 비교한다. | mac |
| TOOL-A09 | 계측 프로파일러: -finstrument-functions | __cyg_profile_func_enter/exit 훅으로 호출 깊이·누적 시간을 기록하는 경량 프로파일러를 만들고, 주소를 심볼화해 상위 함수 표를 출력한 뒤 샘플링 방식(Instruments/sample)과 결과를 비교한다. | mac |
| TOOL-A10 | 최적화 리포트 읽기: -Rpass와 인라이닝·벡터화 | -Rpass=inline/loop-vectorize와 -Rpass-missed로 핫 루프 3개의 리포트를 모아 aliasing·함수 경계 때문에 벡터화가 막힌 원인을 restrict·static inline 등으로 해결하고, 어셈블리와 속도로 확인한다(퍼징은 see:TEST-M02). | mac |
| TOOL-A11 | 미니 readelf 구현: ELF 헤더·섹션·심볼 파싱 | ELF32/ELF64 파일의 헤더, 섹션 헤더, 프로그램 헤더, 심볼 테이블을 출력하는 C 프로그램을 작성해 llvm-readelf 출력과 diff로 검증하고, 로더가 PT_LOAD를 메모리에 올리는 과정을 설명한다. | mac |

### DS — 자료구조 (Data Structures)

임베디드 자료구조는 '동적 할당 없이, 예측 가능한 시간으로, 인터럽트와 공존하며' 동작해야 한다는 점에서 일반 자료구조와 다르다. 링 버퍼·침입형 리스트·메모리 풀·비트맵·타이머 휠·lock-free 큐는 펌웨어 면접과 실무의 단골이다. 이 트랙은 각 구조를 C로 직접 구현하고 불변식 검사·스트레스 테스트로 검증한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| DS-B01 | 고정 용량 벡터: malloc 없는 동적 배열 | 용량을 컴파일 타임에 정하는 fixed_vec(uint32_t)을 정적 배열로 구현하고 insert/erase 시 원소 이동, 가득 참 처리, 범위 밖 인덱스를 경계 테스트와 ASan으로 검증한다. | mac |
| DS-B02 | 배열 기반 스택과 괄호·명령 검사 | 배열 스택을 구현하고 이를 이용해 괄호 균형 검사기와 후위표기식 계산기(RPN)를 작성해 테스트 케이스 20개로 검증한다. | mac |
| DS-B03 | 링 버퍼 기초: Full/Empty 판별 3가지 방식 | UART 수신용 바이트 링 버퍼를 세 가지 full/empty 판별 방식으로 구현하고, 무작위 push/pop 시퀀스를 참조 모델(큐)과 대조하는 랜덤 테스트로 검증한다. | mac |
| DS-B04 | 2의 거듭제곱 링 버퍼: 마스크 인덱스와 자유 증가 카운터 | 크기 2^n, uint32_t head/tail을 계속 증가시키고 & (N-1)로 인덱싱하는 링 버퍼를 구현해, head가 UINT32_MAX를 넘는 경계에서도 len = head - tail이 맞음을 테스트로 확인한다. | mac |
| DS-B05 | 단일 연결 리스트: 삽입·삭제·뒤집기·사이클 검출 | 단일 연결 리스트의 head 삽입·정렬 삽입·값 삭제(이중 포인터 기법)·반복/재귀 뒤집기·Floyd 사이클 검출·중간 노드 찾기를 구현하고 ASan 하에 테스트한다. | mac |
| DS-B06 | 이중 연결 리스트와 Sentinel 노드 | 원형 이중 연결 리스트를 sentinel 노드로 구현해 add_head/add_tail/remove/splice를 작성하고, 각 연산 후 prev/next 일관성 불변식 검사 함수로 랜덤 테스트한다. | mac |
| DS-B07 | 인덱스 링크 리스트: 정적 노드 풀 | 노드 64개 정적 배열과 uint8_t next 인덱스, free list를 가진 리스트를 구현해 할당/해제/순회를 테스트하고, 포인터 버전 대비 RAM 사용량을 sizeof로 비교한다. | mac |
| DS-B08 | 비트맵(Bitset): 할당 상태 관리 | N비트 비트맵의 set/clear/test/find_first_zero/find_next_set/연속 k개 빈 비트 찾기를 구현하고, __builtin_ctz 활용 버전과 단순 루프 버전을 대조 테스트한다. | mac |
| DS-B09 | 해시 함수 기초: FNV-1a, djb2, 분포 측정 | FNV-1a, djb2, 단순 합 해시를 구현해 센서 이름·명령어·순차 정수 키 집합에서 버킷 분포(카이제곱)와 충돌 수를 측정하고 결과를 표로 비교한다. | mac |
| DS-B10 | 해시 테이블: 분리 체이닝 | 문자열 키 → uint32_t 값 해시 테이블을 정적 노드 풀 기반 체이닝으로 구현하고, 같은 버킷 충돌·삭제 후 재삽입 시나리오와 참조 모델 대조 랜덤 테스트를 수행한다. | mac |
| DS-B11 | 해시 테이블: 오픈 어드레싱과 Tombstone | 선형 탐사 해시 테이블을 구현해 tombstone 없이 삭제할 때 조회가 실패하는 버그를 재현한 뒤 tombstone 방식으로 고치고, 부하율별 평균 탐사 길이를 측정한다. | mac |
| DS-B12 | 이진 힙과 우선순위 큐 | (만료시각, 콜백 id) 원소의 min-heap 우선순위 큐를 고정 배열로 구현해 push/pop/peek/heapify를 작성하고, 매 연산 후 힙 속성 검사와 정렬 결과 비교로 검증한다. | mac |
| DS-B13 | 이진 탐색 트리: 삽입·삭제·순회 | 정수 키 BST의 insert/find/delete(자식 0/1/2개)와 inorder/preorder/level-order 순회, 유효성 검사(min/max 범위)를 구현해 랜덤 삽입·삭제 테스트로 정렬 순서 유지를 확인한다. | mac |
| DS-B14 | 트라이(Trie): 명령어 파서와 자동완성 | 디버그 셸 명령어 집합을 정적 노드 풀 트라이에 넣고 정확 일치, 고유 접두사 축약("st" → "status"), 후보 목록 출력(탭 자동완성)을 구현해 테스트한다. | mac |
| DS-M01 | 침입형 리스트: Linux list_head 스타일 | list_head, list_add/del/move/splice, list_for_each_entry(_safe)를 구현하고, 하나의 task 객체가 ready 리스트와 all 리스트에 동시에 속하는 예제와 순회 중 삭제 테스트를 작성한다. | mac |
| DS-M02 | 고정 크기 메모리 풀: O(1) 할당/해제 | 블록 크기·개수 고정 풀을 구현하고 이중 해제·범위 밖 포인터 검출(디버그 모드), 사용량 하이워터마크 통계를 추가한 뒤 랜덤 할당/해제 스트레스 테스트와 ASan poisoning 연동을 한다. | mac |
| DS-M03 | 간소화 슬랩 할당기 | 16/32/64/128B 크기 클래스와 4KB 슬랩, 슬랩별 free list와 partial/full/empty 리스트를 가진 할당기를 구현해 내부 단편화율과 할당 속도를 malloc 대비 측정한다. | mac |
| DS-M04 | 버디 할당기: 분할과 병합 | order 0~10 버디 할당기를 free list 배열과 비트맵으로 구현하고, 랜덤 할당/해제 후 모든 블록 해제 시 최대 블록 하나로 복원되는지와 단편화 지표를 검증한다. | mac |
| DS-M05 | LRU 캐시: 해시 + 이중 연결 리스트 | FTL 매핑 캐시를 가정한 LBA → PBA LRU 캐시를 침입형 이중 리스트 + 해시 테이블로 구현하고, 접근 트레이스(순차/랜덤/핫스팟)에서 hit rate와 참조 모델 일치를 검증한다. | mac |
| DS-M06 | 해시 타이머 휠 | 슬롯 256개 단일 레벨 + 라운드 카운터 방식 타이머 휠을 침입형 리스트로 구현하고, 수천 개 타이머 등록/취소/만료 순서를 min-heap 기반 참조 구현과 대조한다. | mac |
| DS-M07 | AVL 트리: 회전과 균형 유지 | AVL 삽입/삭제와 LL/RR/LR/RL 회전을 구현하고, 정렬된 순서 삽입에서도 높이가 O(log n)임을 측정하며 매 연산 후 균형 인자 검사 함수로 검증한다. | mac |
| DS-M08 | 해시 테이블 리사이즈와 Robin Hood 해싱 | B11 테이블에 부하율 기반 리사이즈와 Robin Hood 삽입(backward-shift 삭제)을 추가하고, 부하율 0.5~0.95에서 평균/최대 탐사 길이를 선형 탐사와 비교한다. | mac |
| DS-M09 | 계층 비트맵: O(1) 최고 우선순위 탐색 | 1024개 우선순위/블록을 32비트 상위 요약 + 32×32비트 하위 비트맵으로 관리하고 CLZ/CTZ로 최고 우선순위 set 비트를 O(1)에 찾는 구조를 구현해 단순 선형 탐색과 대조한다. | mac |
| DS-M10 | 가변 길이 레코드 로그 링 버퍼 | [len|type|timestamp|payload] 레코드를 바이트 링 버퍼에 기록하며 공간 부족 시 가장 오래된 레코드를 통째로 버리는 로거를 구현하고, 래핑 경계에 걸친 레코드 읽기와 덤프 파서를 테스트한다. | mac |
| DS-M11 | 인덱스 우선순위 큐: Decrease-key와 취소 | id → heap 위치 배열을 유지하는 indexed min-heap에 decrease_key/increase_key/remove(id)를 구현하고, 타이머 재설정·취소 시나리오와 Dijkstra(ALGO 트랙) 입력으로 검증한다. | mac |
| DS-M12 | 희소 행렬 CSR과 SpMV | COO → CSR 변환, CSR 희소 행렬-벡터 곱(SpMV), CSR 전치를 구현하고 밀집 행렬 결과와 대조하며, 희소도별 메모리/시간을 밀집 버전과 비교한다. | mac |
| DS-M13 | 기수 트리와 최장 접두사 매칭(LPM) | IPv4 prefix/len → next-hop 엔트리를 이진 트라이 → 경로 압축(Patricia) 순으로 구현하고, 무작위 주소에 대한 LPM 결과를 선형 탐색 참조 구현과 대조한다. | mac |
| DS-M14 | 블룸 필터: 확률적 집합 | k개 해시(더블 해싱)와 m비트 비트맵 블룸 필터를 구현해 n, m, k 조합별 실측 거짓 양성률을 이론식 (1-e^{-kn/m})^k 와 비교하고, 최적 k 선택을 검증한다. | mac |
| DS-A01 | Lock-free SPSC 큐: C11 atomics | head/tail을 캐시 라인 분리한 SPSC 링 큐를 memory_order_acquire/release로 구현해 두 스레드로 수억 개 메시지 순서·무손실을 검증(TSan 포함)하고, 인덱스 캐싱 최적화 전후 처리량을 측정한다. | mac |
| DS-A02 | ISR↔태스크 링 버퍼와 Bip Buffer(DMA 연속 영역) | 단일 코어 ISR 생산자용 링 버퍼의 배리어 요건을 분석·주석화하고, DMA가 연속 메모리를 요구할 때 쓰는 bip buffer(reserve/commit/read/release)를 구현해 경계 래핑 시나리오로 검증한다. | mac |
| DS-A03 | Bounded MPMC 큐: Vyukov 시퀀스 방식 | 슬롯별 sequence 카운터와 CAS로 enqueue/dequeue하는 MPMC 큐를 구현해 4×4 스레드 스트레스에서 모든 메시지가 정확히 한 번 소비됨을 검증하고, 뮤텍스 큐 대비 처리량을 측정한다. | mac |
| DS-A04 | Lock-free 스택과 ABA 문제 | CAS 기반 Treiber 스택을 구현해 노드 재사용 풀과 함께 ABA로 리스트가 깨지는 상황을 강제 재현하고, (인덱스, 세대 태그) 64비트 packing 또는 128비트 CAS로 수정해 스트레스 테스트한다. | mac |
| DS-A05 | 침입형 레드블랙 트리 (Linux rbtree 스타일) | rb_node 내장형 레드블랙 트리의 insert/erase/next를 구현하고, 루트 검정·빨강 연속 금지·검정 높이 동일 불변식 검사기와 랜덤 연산 100만 회 테스트로 검증한다. | mac |
| DS-A06 | TLSF 할당기: 실시간 O(1) malloc | FL/SL 2단계 인덱스와 비트맵, 물리적 인접 블록 병합을 가진 TLSF를 고정 힙 위에 구현하고, 랜덤 크기 스트레스에서 최악 연산 시간 분포와 단편화를 메모리 풀/malloc과 비교한다. | mac |
| DS-A07 | Cuckoo 해싱: 최악 O(1) 조회 | 두 테이블·두 해시 함수의 cuckoo 해시를 구현해 삽입 시 축출 체인과 사이클 감지 후 리해시를 처리하고, 조회가 항상 2회 탐사 이내임과 부하율 한계를 측정한다. | mac |
| DS-A08 | B+ 트리: 페이지 단위 인덱스 | 노드 = 4KB 페이지인 B+ 트리를 메모리 페이지 풀 위에 구현해 insert/search/delete(재분배·병합)/range scan을 작성하고, 무작위 연산 후 정렬·리프 연결 불변식을 검증한다. | mac |
| DS-A09 | FTL 매핑 테이블과 GC 대상 선정 구조 | 페이지 매핑 L2P 배열, 블록별 valid 비트맵·valid count, greedy/cost-benefit GC victim을 고르는 indexed heap을 가진 FTL 모델을 구현해 랜덤 쓰기 워크로드에서 쓰기 증폭(WAF)을 측정한다. | mac |
| DS-A10 | 플래시 로그 구조 KV 저장소 | 섹터 소거·쓰기 횟수를 모사한 플래시 모델 위에 append-only KV(레코드 CRC, 섹터 상태 헤더, 컴팩션)를 구현하고, 임의 시점 전원 차단 주입 후 재마운트 시 일관성 복구를 검증한다. | mac |
| DS-A11 | 캐시 친화 레이아웃: AoS vs SoA와 정렬 | 파티클/센서 레코드 배열을 AoS·SoA·AoSoA로 구현해 일부 필드만 읽는 루프와 전체 갱신 루프의 처리량을 M2에서 측정하고, 연결 리스트 vs 배열 순회 비용까지 비교해 설계 지침을 정리한다. | mac |

### ALGO — 알고리즘 (Algorithms)

임베디드 회사 코딩 면접도 결국 알고리즘 문제를 풀게 하지만, 비트 조작·CRC·스트림 파싱·제한된 메모리·실시간 제약 같은 '펌웨어 맛'이 섞여 나온다. 이 트랙은 정렬·탐색·그래프·DP 같은 정석 패턴을 C로 직접 구현하고, 각 문제를 센서·통신·로봇 상황으로 바꿔 풀어 본다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| ALGO-B01 | 복잡도 분석: Big-O를 실측으로 확인 | 같은 문제(중복 원소 찾기)를 O(n²)/O(n log n)/O(n) 해시 풀이로 구현해 n=10³~10⁷에서 시간·메모리를 측정·로그-로그 그래프로 그리고 기울기로 복잡도를 추정한다. | mac |
| ALGO-B02 | 이진 탐색과 경계: lower_bound / upper_bound | lower_bound/upper_bound/정확 일치/회전 정렬 배열 탐색을 구현하고 모든 길이 0~50 배열·모든 타깃에 대해 선형 탐색과 전수 대조 테스트한다. | mac |
| ALGO-B03 | 보정 테이블 조회: 이진 탐색 + 선형 보간 | ADC 코드 → 온도(°C) 비균등 보정 테이블에서 이진 탐색으로 구간을 찾고 정수 연산 선형 보간하는 함수를 작성해, 테이블 끝 범위 밖 포화와 오차를 double 기준으로 검증한다. | mac |
| ALGO-B04 | 답을 이분 탐색: Parametric Search | '패킷 n개를 순서대로 D 프레임 안에 보내기 위한 최소 프레임 용량'과 '정수 제곱근'을 판정 함수 + 이분 탐색으로 풀고 브루트포스와 랜덤 대조한다. | mac |
| ALGO-B05 | 기본 정렬 3종과 안정성: 삽입·선택·버블 | 세 정렬을 구현해 (키, 원래 순서) 쌍으로 안정성을 검사하고, 랜덤/역순/거의 정렬 입력에서 비교·교환 횟수를 세어 표로 비교한다. | mac |
| ALGO-B06 | 퀵소트: Lomuto·Hoare 파티션과 최악 케이스 | Lomuto/Hoare/3-way 파티션 퀵소트를 구현해 정렬된·중복 많은 입력에서 최악 성능을 재현하고, median-of-3 + 작은 쪽 먼저 재귀로 스택 깊이를 O(log n)으로 제한함을 측정한다. | mac |
| ALGO-B07 | 머지소트: Top-down·Bottom-up·연결 리스트 | 배열 재귀 머지소트, 반복 bottom-up 머지소트, 연결 리스트 머지소트(O(1) 추가 메모리)를 구현해 qsort 결과와 대조하고 안정성을 확인한다. | mac |
| ALGO-B08 | 비교 없는 정렬: 계수 정렬·기수 정렬 | 12비트 ADC 샘플 배열을 계수 정렬로, 32비트 타임스탬프를 8비트 LSD 기수 정렬로 구현해 qsort 대비 속도와 메모리를 측정한다. | mac |
| ALGO-B09 | 투 포인터: 쌍 합·중복 제거·병합 | 정렬 배열 두 수의 합, in-place 중복 제거, 두 정렬 배열 병합, 3-sum을 투 포인터로 구현하고 브루트포스와 랜덤 대조 테스트한다. | mac |
| ALGO-B10 | 슬라이딩 윈도우: 이동 평균과 최장 구간 | 센서 스트림의 N샘플 이동 평균(누적합 링 버퍼, O(1)/샘플)과 '합이 S 이하인 최장 구간', '중복 없는 최장 부분 문자열'을 구현해 브루트포스와 대조한다. | mac |
| ALGO-B11 | 재귀와 스택 깊이: 재귀를 반복으로 | flood fill과 트리 순회를 재귀로 구현해 큰 입력에서 스택 오버플로를 재현(ulimit -s)하고, 명시적 스택/큐 기반 반복 버전으로 바꿔 최대 보조 메모리를 측정한다. | mac |
| ALGO-B12 | 비트 트릭 1: popcount·최하위 비트·2의 거듭제곱 | popcount(Kernighan/SWAR), x & -x, 2의 거듭제곱 판정, 다음 2의 거듭제곱 올림, 정렬 올림 ALIGN_UP을 구현해 __builtin 결과와 전 범위(또는 랜덤) 대조한다. | mac |
| ALGO-B13 | 비트 트릭 2: 비트 반전·니블 스왑·패리티 | 8/32비트 reverse bits(루프, 마스크-시프트 분할정복, 256 LUT), 니블 역순, 패리티 계산, 홀짝 비트 교환을 구현해 상호 대조하고 방식별 속도·메모리를 비교한다. | mac |
| ALGO-B14 | 체크섬: 합 체크섬·인터넷 체크섬·Fletcher | 8비트 합/XOR 체크섬, RFC 1071 인터넷 체크섬, Fletcher-16/32를 구현해 표준 예제와 대조하고, 1~2비트 오류·바이트 순서 바뀜 검출률을 랜덤 주입 실험으로 비교한다. | mac |
| ALGO-B15 | 명령 문자열 파싱 기초: 토큰화와 검증 | "SET LED 3 ON", "READ 0x4000_0010 4" 같은 명령을 공백/대소문자/숫자 형식 오류에 견고하게 파싱해 구조체로 변환하는 함수를 작성하고 정상·비정상 입력 30개로 테스트한다. | mac |
| ALGO-M01 | CRC 원리: 비트 단위 CRC-8·CRC-16-CCITT | 파라미터화된 비트 단위 CRC 엔진을 구현해 CRC-8(SMBus), CRC-16/CCITT-FALSE, CRC-16/MODBUS의 "123456789" check 값이 카탈로그와 일치함을 검증한다. | mac |
| ALGO-M02 | CRC 가속: 테이블 방식과 CRC-32 Slicing | CRC-32(reflected)를 비트 → 256엔트리 테이블 → slicing-by-8로 구현하고 테이블 생성 코드까지 작성해 zlib crc32 결과와 대조하며 처리량(MB/s)과 테이블 메모리를 비교한다. | mac |
| ALGO-M03 | 백트래킹: 부분집합·순열·N-Queens | 부분집합 합, 순열(중복 원소 포함), N-Queens(비트마스크 가지치기)를 구현해 결과 개수를 알려진 값과 대조하고 가지치기 전후 노드 방문 수를 비교한다. | mac |
| ALGO-M04 | BFS/DFS: 점유 격자 지도 탐색 | 로봇 점유 격자(장애물/빈칸)에서 BFS 최단 경로와 경로 복원, 장애물 덩어리 개수(DFS/BFS), 시작점에서 도달 가능한 영역 크기를 구현해 손으로 만든 맵 10개로 검증한다. | mac |
| ALGO-M05 | 위상 정렬: 드라이버 초기화 순서 | 클럭·전원·I2C·센서 드라이버 간 의존성 목록을 받아 Kahn 알고리즘으로 초기화 순서를 출력하고, 순환 의존 시 사이클 경로를 DFS로 찾아 보고하는 도구를 작성한다. | mac |
| ALGO-M06 | 다익스트라: 가중 그래프 최단 경로 | 통신 노드 간 지연(ms) 가중 그래프에서 힙(DS-M11 indexed PQ) 기반 다익스트라와 경로 복원을 구현하고, 플로이드-워셜 결과와 랜덤 그래프 대조로 검증한다. | mac |
| ALGO-M07 | 동적 계획법 1D: 계단·동전 교환·LIS | 계단 오르기 변형, 동전 교환(최소 개수/경우의 수), 최장 증가 부분수열(O(n log n))을 하향식과 상향식으로 구현해 브루트포스와 대조하고 공간 최적화 버전을 만든다. | mac |
| ALGO-M08 | 동적 계획법 2D: 편집 거리·LCS·0/1 배낭 | 편집 거리(펌웨어 설정 diff), LCS, 0/1 배낭(Flash 용량 내 기능 모듈 선택 최대 가치)을 구현해 선택 결과 역추적까지 출력하고 공간 O(W) 버전과 결과를 대조한다. | mac |
| ALGO-M09 | 그리디: 구간 스케줄링과 최소 자원 수 | 최대 비겹침 구간 선택, 동시 DMA 전송 구간들에 필요한 최소 채널 수(회의실 문제), 구간 병합을 구현하고 각 그리디의 정당성을 짧게 증명하며 브루트포스와 대조한다. | mac |
| ALGO-M10 | 스케줄링 문제: 쿨다운 태스크·마감시간 작업 선택 | 같은 종류 작업 사이 쿨다운 n이 필요한 태스크 스케줄러(최소 시간)와 마감시간·이익이 있는 작업 선택(job sequencing)을 힙으로 구현하고 실제 실행 순서를 출력·검증한다. | mac |
| ALGO-M11 | 힙 활용: Top-K와 스트리밍 중앙값 | 센서 스트림의 실시간 중앙값(최대힙+최소힙)과 윈도우 중앙값(삭제 지원), 상위 K개 이상치 추적을 구현해 정렬 기반 참조와 대조하고 샘플당 시간을 측정한다. | mac |
| ALGO-M12 | 상태 기계 파서: NMEA 문장과 체크섬 | UART 바이트를 하나씩 받는 NMEA($GPGGA 등) 파서를 상태 기계로 구현해 *XX 체크섬 검증, 필드 분리, 쓰레기 바이트·잘린 문장 복구를 처리하고 실제 로그 + 퍼즈 입력으로 테스트한다. | mac |
| ALGO-M13 | 누적합과 차분 배열: 1D·2D | 1D 누적합 구간 합, 차분 배열로 다수 구간 증가 후 결과 복원, 2D 누적합(이미지 영역 합/integral image)을 구현해 브루트포스와 대조하고 정수 오버플로 범위를 분석한다. | mac |
| ALGO-M14 | Union-Find: 연결성과 그룹 병합 | Union-Find를 구현해 센서 메시 네트워크 링크 추가 시 연결 요소 수 추적, 그래프 사이클 검출, Kruskal MST를 풀고 BFS 기반 참조와 대조한다. | mac |
| ALGO-M15 | 정수 산술 알고리즘: isqrt·거듭제곱·GCD·상수 나눗셈 | 비트 단위 정수 제곱근, 모듈러 빠른 거듭제곱, 이진 GCD, 상수 나눗셈을 곱셈+시프트로 바꾸는 매직 넘버 계산(÷10)을 구현해 전 범위/랜덤으로 표준 연산과 대조한다. | mac |
| ALGO-A01 | A* 경로 계획: 휴리스틱과 장애물 팽창 | 8방향 격자에서 맨해튼/옥타일 휴리스틱 A*를 구현해 다익스트라와 경로 비용 동일성·확장 노드 수를 비교하고, 로봇 반경만큼 장애물 팽창 후 경로를 출력한다. | mac |
| ALGO-A02 | 벨만-포드와 플로이드-워셜 | 벨만-포드로 음수 사이클을 검출하고(환율 차익 문제 형태), 플로이드-워셜로 노드 간 지연 행렬과 경로 복원 테이블을 만들어 다익스트라 반복 결과와 대조한다. | mac |
| ALGO-A03 | 비트마스크 DP: 테스트 포인트 방문 순서(TSP) | 프로브가 n≤16개 보드 테스트 포인트를 모두 방문하는 최소 이동 거리를 비트마스크 DP로 구현해 순열 브루트포스(n≤9)와 대조하고 최적 순서를 역추적한다. | mac |
| ALGO-A04 | 스트리밍 패턴 매칭: KMP와 Rabin-Karp | UART 바이트를 하나씩 넣는 KMP 기반 프리앰블 검출기(상태 1개 유지)와 롤링 해시 Rabin-Karp를 구현해 경계에 걸친 패턴·겹치는 패턴 케이스를 naive 검색과 대조한다. | mac |
| ALGO-A05 | LZ77 압축: 슬라이딩 윈도 인코더/디코더 | 윈도 크기와 최소 일치 길이를 파라미터로 하는 LZ77(LZSS) 인코더/디코더를 구현해 펌웨어 이미지·로그 데이터에서 압축률과 디코더 RAM 사용량을 측정하고, 왕복 랜덤 테스트로 검증한다(COBS/SLIP 프레이밍은 see:COMM-B11/B12). | mac |
| ALGO-A06 | 센서 로그 압축: 델타·RLE·허프만 | 온도·가속도 로그에 델타 인코딩 → zigzag varint → RLE → 정적 허프만 코딩을 단계적으로 적용해 각 단계 압축률을 측정하고, 디코딩 왕복 일치를 검증한다. | mac |
| ALGO-A07 | 비트 트릭 고급: Gray 코드·SWAR·부분집합 순회 | Gray 코드 변환/역변환(엔코더 판독), SWAR로 바이트 내 0 검출(haszero), 마스크의 모든 부분집합 순회, 다음 같은 popcount 수(Gosper's hack)를 구현해 브루트포스와 대조한다. | mac |
| ALGO-A08 | 스트리밍 통계: Welford 분산과 저수지 샘플링 | Welford 온라인 평균/분산(수치 안정성 비교 포함), 지수 이동 평균의 정수 구현, 저수지 샘플링(k개)을 구현해 전체 저장 방식 결과와 비교하고 균일성 카이제곱 검정을 한다. | mac |
| ALGO-A09 | 스위프 라인: 메모리 영역 겹침과 최대 동시 이벤트 | 펌웨어 메모리 맵 영역 목록의 모든 겹침 쌍 검출, 인터럽트 구간 로그에서 최대 동시 활성 수와 그 시각, 직사각형 합집합 넓이(1D 축소)를 스위프 라인으로 구현해 브루트포스와 대조한다. | mac |
| ALGO-A10 | 강한 연결 요소: 락 의존 그래프 데드락 검출 | 스레드별 락 획득 순서 로그로 락 순서 그래프를 만들고 Tarjan SCC로 데드락 가능 사이클을 찾아 보고하는 도구를 구현해, Kosaraju 결과와 대조 테스트한다. | mac |
| ALGO-A11 | Rate Limiter: 토큰 버킷과 슬라이딩 로그 | 정수 틱 기반 토큰 버킷과 고정 크기 슬라이딩 윈도우 카운터를 구현해 버스트 허용량·평균 속도를 테스트 트래픽으로 검증하고, 메모리/정확도 트레이드오프를 비교한다. | mac |
| ALGO-A12 | 메모리 제약 외부 정렬: 4KB RAM으로 대용량 정렬 | RAM 버퍼 4KB 제한(코드로 강제) 하에 파일로 모사한 Flash의 1M 레코드를 정렬된 런 생성 → 힙 기반 k-way 병합으로 정렬하고, 읽기/쓰기 바이트 수와 패스 수를 이론값과 비교한다. | mac |

## 2단계 · 시스템 프로그래밍 (Systems Programming)

### CPP — 임베디드 C++ (Embedded C++)

펌웨어에서 C++는 '더 안전한 C'로 쓰인다: RAII로 락·인터럽트·칩셀렉트 해제를 보장하고, constexpr·템플릿으로 런타임 비용 없이 타입 안전 레지스터 접근과 HAL을 만든다. 예외·RTTI·힙 없이 빌드하는 제약 속에서 어떤 C++ 기능이 공짜이고 어떤 기능이 코드 크기·스택·사이클을 먹는지 직접 측정하는 것이 이 트랙의 핵심이며, Apple·Tesla·Anduril 등 C++ 펌웨어 팀 면접의 단골 주제다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| CPP-B01 | C에서 C++로: 펌웨어용 C++ 부분집합 | 주어진 C 링버퍼·GPIO 드라이버를 clang++ -std=c++17 -fno-exceptions -fno-rtti로 빌드되게 고치고, C 빌드 대비 size(1) 출력과 objdump 결과가 같은지 비교한다. | mac |
| CPP-B02 | extern "C"와 name mangling: C HAL과 링크 | C로 된 mock HAL(.c)과 C++ 앱(.cpp)을 함께 링크하고, extern "C" 유무에 따른 링크 에러를 재현한 뒤 nm / c++filt로 맹글링된 심볼을 해석한다. | mac |
| CPP-B03 | enum class와 고정 기저 타입으로 레지스터 필드 명명 | UART 모드/패리티 필드를 enum class : uint32_t로 정의하고 잘못된 enum 혼용이 컴파일 에러가 되는지, -O2 어셈블리가 C 버전과 동일한지 godbolt로 비교한다. | mac |
| CPP-B04 | constexpr 기초: 컴파일타임 주소·마스크 계산 | constexpr mask(offset,width)와 주변장치 base+offset 상수를 작성하고 static_assert로 값을 검증, objdump에서 런타임 계산 코드가 사라졌음을 확인한다. | mac |
| CPP-B05 | const·volatile·참조로 레지스터 접근 모델링 | volatile const uint32_t& / volatile uint32_t&를 반환하는 접근자를 만들고, 읽기 전용 레지스터에 쓰기 시 컴파일 에러, 루프 내 volatile 읽기가 최적화로 사라지지 않음을 어셈블리로 검증한다. | mac |
| CPP-B06 | RAII 기초: 인터럽트 비활성화 CriticalSection 가드 | mock PRIMASK 변수를 저장/복원하는 CriticalSection 클래스를 구현하고, 중첩·조기 return·여러 분기 경로에서 상태가 항상 복원됨을 단위 테스트로 검증한다. | mac |
| CPP-B07 | RAII 스코프 가드: SPI 칩셀렉트와 락 | ScopedChipSelect와 BusLock을 구현(copy ctor = delete)하고, mock GPIO 로그로 assert→전송→deassert 순서가 모든 경로에서 지켜지는지 테스트한다. | mac |
| CPP-B08 | 클래스 레이아웃과 sizeof: 패딩·빈 클래스·EBO | 멤버 순서·virtual 유무·빈 베이스에 따라 sizeof/alignof/offsetof가 어떻게 바뀌는지 표로 예측한 뒤 static_assert와 std::is_standard_layout으로 검증한다. | mac |
| CPP-B09 | 정적 초기화: 전역 생성자와 .init_array | 두 번역 단위의 전역 객체 초기화 순서 문제를 재현하고 constinit/constexpr 생성자로 정적 초기화(.data)로 옮겨 .init_array 엔트리가 사라지는 것을 objdump로 확인한다. | mac |
| CPP-B10 | std::array로 C 배열 대체하기 | DMA 버퍼·룩업 테이블을 std::array로 바꾸고 at() 대신 static 범위 검증을 쓰는 이유를 설명, -O2 어셈블리를 C 배열 버전과 비교한다. | mac |
| CPP-B11 | std::span으로 버퍼+길이 인터페이스 정리 | uart_write(const uint8_t*, size_t) 계열 API를 std::span<const std::byte>로 바꾸고 subspan으로 프레임 헤더/페이로드를 파싱, 정적 extent span의 sizeof가 포인터 1개임을 검증한다. | mac |
| CPP-B12 | 템플릿 기초: 폭별 레지스터 read/write | template<typename T> T reg_read(uintptr_t)를 작성하되 T가 unsigned integral이 아니면 컴파일 에러가 나게 하고, mock 메모리 배열로 동작을 테스트한다. | mac |
| CPP-B13 | -fno-exceptions / -fno-rtti 빌드의 실제 영향 | 같은 프로그램을 플래그 조합 4가지로 빌드해 size/섹션(.eh_frame, .gcc_except_table, typeinfo) 차이를 표로 만들고, throw/dynamic_cast/typeid가 각각 어떤 에러를 내는지 확인한다. | mac |
| CPP-B14 | 힙 금지 강제: operator new 삭제와 링크 에러 | operator new/delete를 정의하지 않거나 =delete/abort 스텁으로 대체하고, std::vector·std::function·std::string 사용이 어떻게 걸리는지 테스트한다(호스트에서는 malloc 훅으로 할당 횟수 0 검증). | mac |
| CPP-M01 | 타입 안전 레지스터 맵: Reg<Addr, T, Access> | constexpr 주소 템플릿 Reg<Addr, uint32_t, RW>를 구현하고 테스트용으로 base 주소를 mock 배열로 치환 가능하게 설계, RO 레지스터 write가 컴파일 실패하는지 확인한다. | mac |
| CPP-M02 | 비트필드 대체: Field<Reg, Offset, Width> RMW | Field 템플릿의 read/write/modify를 구현하고 범위 밖 값 static_assert, 여러 필드를 한 번의 RMW로 묶는 write(Field1=a, Field2=b)를 만든 뒤 mock 레지스터로 결과값을 검증한다. | mac |
| CPP-M03 | enum class 플래그 연산자: Flags<E> | Flags<IrqFlag>에 |, &, ~, test()를 구현하고 다른 enum 플래그와 섞으면 컴파일 에러, 생성 코드가 uint32_t 연산과 동일함을 godbolt로 확인한다. | mac |
| CPP-M04 | 제로 코스트 GPIO HAL: Pin<Port, N> 템플릿 | Pin<PortA,5>::set()/toggle()을 구현해 mock 포트로 테스트하고, --target=thumbv7em-none-eabi -O2 오브젝트를 디스어셈블해 C의 직접 레지스터 쓰기와 명령 수가 같은지 비교한다. | mac |
| CPP-M05 | CRTP 정적 다형성 드라이버 인터페이스 | UartBase<Derived> CRTP 인터페이스와 MockUart/Stm32Uart 구현을 작성하고, 같은 API의 virtual 버전과 코드 크기·호출 어셈블리(인라인 여부)를 비교한다. | mac |
| CPP-M06 | virtual의 실제 비용: vtable·간접호출·devirtualization | 드라이버 3종을 virtual로 구현해 vtable·typeinfo 심볼과 크기를 nm/size로 측정하고, final 및 -flto 적용 시 간접 호출이 직접 호출로 바뀌는지 어셈블리로 확인한다. | mac |
| CPP-M07 | placement new와 정적 저장소 수명 관리 | alignas(T) std::byte buf[sizeof(T)] 위에 placement new로 드라이버 객체를 지연 생성하는 StaticInstance<T>를 구현하고, 생성/소멸 카운터 타입으로 이중 생성·누락 파괴를 테스트한다. | mac |
| CPP-M08 | 클래스 전용 operator new + 정적 메모리 풀 | Message 클래스에 operator new/delete를 오버로드해 정적 풀(N블록)에서 할당하고, 고갈 시 nullptr 반환(nothrow) 동작과 재사용을 단위 테스트로 검증한다. | mac |
| CPP-M09 | move semantics의 비용: 고정 용량 버퍼에선 공짜가 아니다 | 복사/이동 횟수를 세는 계측 타입으로 std::vector 이동과 인라인 저장 버퍼(std::array 멤버) 이동 비용을 비교하고, noexcept 이동 생성자 유무가 재배치에 주는 영향을 확인한다. | mac |
| CPP-M10 | ETL 스타일 static_vector<T, N> 구현 | push_back/emplace_back/pop_back/erase/iterator를 갖춘 static_vector를 placement new로 구현하고, 생성/소멸 카운터 타입과 용량 초과 처리(assert/에러 반환)를 테스트한다. | mac |
| CPP-M11 | 예외 없는 에러 처리: Result<T, E> (expected 스타일) | [[nodiscard]] Result<T,E>를 구현(또는 C++23 std::expected 사용)해 I2C 센서 읽기 API에 적용하고, 에러 전파 체인과 반환값 무시 시 경고가 나는지 테스트한다. | mac |
| CPP-M12 | 무힙 콜백: std::function 대신 delegate / function_ref | 객체 포인터+함수 포인터 기반 Delegate를 구현해 타이머 콜백 테이블에 등록하고, std::function 버전과 sizeof·할당 횟수(malloc 훅)·호출 어셈블리를 비교한다. | mac |
| CPP-M13 | constexpr 테이블 생성: CRC·sin LUT를 .rodata로 | constexpr 함수로 CRC-32 테이블과 Q15 sin LUT를 생성해 static_assert로 몇몇 값을 검증하고, objdump -s로 .rodata에 들어갔고 .init_array/.bss 초기화 코드가 없음을 확인한다. | mac |
| CPP-M14 | static_assert·type traits로 HAL 오용 차단 | 핀별 허용 AF(alternate function) 테이블을 constexpr로 정의하고 configure<Pin, AF>()가 허용되지 않은 조합에서 읽기 쉬운 static_assert 메시지로 실패하게 만든다(개념적으로 concepts로 바꾸는 법은 see:MCPP). | mac |
| CPP-A01 | Typestate 패턴: 설정 순서를 타입으로 강제 | SPI 드라이버를 Spi<Disabled>.configure()->Spi<Configured>.enable()->Spi<Enabled> 형태로 설계하고, enable 전 transfer 호출이 컴파일되지 않음과 런타임 비용 0을 검증한다. | mac |
| CPP-A02 | ISR과 C++ 객체 연결: 정적 트램펄린 디스패치 | IRQ 번호→객체 포인터 테이블과 extern "C" 트램펄린을 구현하고, 호스트에서 가짜 IRQ 발생 루프로 여러 UART 인스턴스의 핸들러가 올바르게 호출되는지 테스트한다. | mac |
| CPP-A03 | C++ 런타임 의존성 최소화: __cxa_* 심볼 추적 | clang --target=thumbv7em-none-eabi -ffreestanding으로 오브젝트를 만들어 nm으로 미정의 심볼(__cxa_guard_acquire, __cxa_pure_virtual, __cxa_atexit 등)을 찾고, -fno-threadsafe-statics·스텁 정의로 각각 제거한다. | mac |
| CPP-A04 | std::atomic과 Cortex-M: lock-free 여부와 ISR 공유 | std::atomic<uint32_t>/<uint64_t>의 fetch_add를 thumbv6m(M0)과 thumbv7m(M3)로 컴파일해 LDREX/STREX vs libatomic 호출 차이를 확인하고, is_always_lock_free를 static_assert로 검사하는 정책을 만든다. | mac |
| CPP-A05 | 템플릿 bloat 다이어트: thin template 기법 | static_vector<T,N>을 5개 타입×3개 N으로 인스턴스화해 size 증가를 측정한 뒤 바이트 단위 비템플릿 코어+얇은 템플릿 래퍼로 리팩터링하고 감소량을 보고한다(-ffunction-sections, --gc-sections 효과 포함). | mac |
| CPP-A06 | 컴파일타임 클럭 트리·보레이트 검증기 | HSE·목표 SYSCLK로 PLL M/N/P를 탐색하는 constexpr 함수와, 주어진 클럭에서 UART BRR 오차가 2% 넘으면 static_assert 실패하는 템플릿을 작성하고 정상/실패 케이스를 확인한다. | mac |
| CPP-A07 | 정적 할당 액티브 오브젝트 프레임워크 | 고정 크기 이벤트 풀, AO별 이벤트 큐, 계층 없는 상태 머신 디스패치를 구현하고 버튼→LED 블링크 시나리오를 호스트에서 이벤트 시퀀스로 테스트한다. | mac |
| CPP-A08 | 테스트 가능한 C++ 드라이버 설계: 의존성 주입 vs 링크 심 | 같은 센서 드라이버를 (1) 템플릿 파라미터 주입 (2) 인터페이스(virtual) 주입 (3) 링크 타임 대체로 각각 만들고 GoogleTest/doctest로 테스트, 코드 크기·테스트 편의성 트레이드오프를 정리한다. | mac |
| CPP-A09 | MISRA C++/AUTOSAR 규칙 적용 리뷰 | 의도적으로 위반이 섞인 펌웨어 코드에 clang-tidy(cppcoreguidelines-*, cert-*, hicpp-*) 검사를 돌려 위반을 분류하고, 각 위반을 규칙 근거와 함께 수정한 diff를 만든다. | mac |
| CPP-A10 | 종합: 정적 할당 C++ 센서 파이프라인 | mock I2C 버스→IMU 드라이버→고정소수점 필터→텔레메트리 직렬화까지 힙 0·예외 0으로 구현하고, 단위 테스트와 함께 size 예산(예: .text 8KB) 준수를 보고한다. | mac |

### BM — 베어메탈 프로그래밍 (Bare-Metal Programming)

베어메탈은 OS 없이 리셋 벡터부터 main, 주변장치 레지스터, 인터럽트, 부트로더까지 펌웨어가 직접 책임지는 영역이다. '전원 켜면 main 전까지 무슨 일이 일어나나', 'HardFault 났을 때 어떻게 원인을 찾나', 'A/B 업데이트 중 전원이 나가면?' 같은 질문은 모든 펌웨어 면접의 핵심이며, 이 트랙은 mock 레지스터 맵으로 호스트에서 드라이버를 검증하고 실제 Cortex-M 부팅은 QEMU에서 확인한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| BM-B01 | 벤더 헤더 읽기: CMSIS 디바이스 헤더와 _Pos/_Msk 관례 | STM32 CMSIS 디바이스 헤더 발췌(구조체·_Pos/_Msk 매크로·주변장치 베이스 주소)를 읽고 offsetof static_assert로 레퍼런스 매뉴얼 오프셋 표와 일치를 확인한 뒤, 매크로만 써서 GPIO/USART 설정 함수를 작성해 mock 레지스터 값으로 검증한다(헤더 자동 생성은 see:SIV-B01). | mac |
| BM-B02 | Mock 주변장치 프레임워크: read/write 훅 | REG_READ/REG_WRITE 매크로를 테스트 빌드에서 함수 훅으로 바꾸고, 주소별 콜백으로 TXE 플래그 자동 세트·RX 데이터 큐를 모델링하는 mock 프레임워크를 만들어 이후 드라이버 아이템에서 재사용한다. | mac |
| BM-B03 | RCC: 주변장치 클럭 게이팅과 리셋 제어 | mock RCC+GPIO 모델에서 클럭이 꺼진 주변장치 레지스터 쓰기가 무시되도록 만들고, periph_clock_enable/reset_pulse API와 enable 직후 더미 읽기(버스 동기화) 필요성을 테스트로 보인다. | mac |
| BM-B04 | RMW 함정: BSRR set/reset 레지스터와 W1C 상태 비트 | mock 모델에 W1C 상태 레지스터와 BSRR을 구현하고, |= 로 W1C를 지우다 다른 플래그까지 지우는 버그, ISR과 메인의 ODR RMW 레이스를 테스트로 재현한 뒤 올바른 접근으로 고친다. | mac |
| BM-B05 | MCU 메모리 맵과 섹션 배치: map 파일 읽기 | 예제 펌웨어 .map/size 출력을 읽어 .text/.rodata/.data/.bss/스택/힙 예산표를 만들고, 큰 const 테이블이 RAM에 들어가는 실수(const 누락)를 찾아 고친다. | mac |
| BM-B06 | 부트로더+앱 플래시 분할 레이아웃 설계 | 부트로더/앱 각각의 링커 스크립트를 공통 레이아웃 헤더(.ld include)에서 파생시키고, 공유 .noinit RAM 영역(부트 사유 전달)과 이미지 헤더 위치를 고정한 뒤 objdump -h와 맵 파일로 영역 겹침이 없음을 검증한다. | **qemu** |
| BM-B07 | Reset handler 구현 (호스트 시뮬레이션): .data 복사·.bss 제로화 | 가짜 FLASH 이미지/RAM 배열과 링커 심볼 역할 포인터를 주고 reset_init()이 .data 복사·.bss 제로화·init_array 호출을 수행하도록 구현, 길이 0·홀수 길이·정렬 경계 케이스를 단위 테스트한다. | mac |
| BM-B08 | C로 쓴 startup으로 첫 부팅: QEMU에서 main까지 | C 배열 벡터 테이블(__attribute__((section(".isr_vector"))))과 weak alias 기본 핸들러, BM-B07 reset_init으로 QEMU 부팅에 성공하고, gdb로 .data 초기값·.bss 0을 확인한 뒤 CMSIS startup_*.s/system_*.c와 차이를 정리한다. | **qemu** |
| BM-B09 | printf 리타겟팅: semihosting vs UART _write | QEMU에서 (1) semihosting(-semihosting)과 (2) UART0 데이터 레지스터 폴링 _write로 각각 printf를 동작시키고, 코드 크기·디버거 없을 때 semihosting이 멈추는 이유를 정리한다. | **qemu** |
| BM-B10 | 클럭 트리 계산기: PLL M/N/P와 버스 프리스케일러 | HSE와 목표 SYSCLK를 받아 STM32F4 스타일 제약(VCO 범위, P∈{2,4,6,8}, APB1 ≤ 42MHz)을 만족하는 설정을 모두 찾는 C/python 함수를 작성하고, 플래시 wait state까지 결정해 테스트 벡터로 검증한다. | mac |
| BM-B11 | GPIO 드라이버 (레지스터 직접): 모드·풀업·출력·입력 | gpio_init(port, pin, mode, pull, speed)/write/read/toggle을 mock GPIO 모델 위에 구현하고, 다른 핀 설정을 건드리지 않는지·클럭 enable 선행 여부를 테스트한다. | mac |
| BM-B12 | 폴링 UART 드라이버: baud divisor와 상태 플래그 | BRR 정수/소수부 계산 함수와 putc/getc/write를 mock UART 모델(TX 바이트 캡처, RX 주입)로 구현·테스트하고, TXE와 TC의 차이(전원 차단 전 대기)를 설명한다. | mac |
| BM-B13 | SysTick: reload 계산, ms tick, wrap-safe delay | 주어진 코어 클럭에서 1ms reload 값을 계산하는 함수와 tick ISR·delay_ms·timeout 체크((int32_t)(now-deadline) >= 0)를 구현하고, tick을 0xFFFFFFF0 근처에서 시작시켜 wrap 테스트를 한다. | mac |
| BM-B14 | 하드웨어 타이머: PSC/ARR 계산과 주기 인터럽트 | 타이머 클럭과 목표 주기를 받아 오차 최소 PSC/ARR을 찾는 함수와, mock 타이머 모델(카운트 진행·UIF 세트)로 업데이트 인터럽트 주기를 검증하는 테스트를 작성한다. | mac |
| BM-M01 | NVIC 기초: ISER/ICER/IPR 비트 계산과 ISR 규칙 | NVIC_EnableIRQ/DisableIRQ/SetPriority/SetPending을 mock NVIC 레지스터 배열 위에 구현해 IRQ 0~95 전체를 테스트하고, ISR에서 플래그 클리어 위치(늦은 클리어로 인한 재진입)를 설명한다. | mac |
| BM-M02 | 시스템 IRQ 우선순위 설계와 선점 검증 | 모터 PWM·UART·ADC·SysTick·PendSV가 있는 시스템의 우선순위 표와 그룹 설정을 설계하고, QEMU에서 소프트웨어 pending으로 IRQ를 겹쳐 발생시켜 선점/비선점 순서가 설계대로인지 로그로 검증한다. | **qemu** |
| BM-M03 | 외부 인터럽트(EXTI)와 버튼 디바운스 | 채터링이 섞인 입력 타임라인(샘플 배열)을 주입해 인터럽트+타이머 재확인 방식과 주기 샘플링 방식 디바운서를 구현하고, 짧은 글리치 무시·롱프레스 검출을 테스트한다. | mac |
| BM-M04 | 인터럽트 구동 UART: ISR + 링버퍼 TX/RX | mock UART 모델에서 RXNE/TXE 인터럽트를 시뮬레이션하는 이벤트 루프를 만들고, TXE 인터럽트 enable/disable 타이밍, 오버런 처리, 버퍼 가득 참 정책을 구현·테스트한다. | mac |
| BM-M05 | PWM: 주파수·듀티 계산, edge/center-aligned, 데드타임 | 타이머 모델에 CCR 비교 출력을 추가해 한 주기 파형 배열을 생성하고, 듀티 0%/100% 경계, center-aligned 시 주파수 절반, 상보 출력 데드타임 삽입을 파형으로 검증한다. | mac |
| BM-M06 | 입력 캡처: 주파수·펄스폭 측정과 오버플로 처리 | 16비트 캡처 값과 오버플로 인터럽트 이벤트 시퀀스를 주입해 주기/듀티를 계산하는 드라이버를 구현하고, 캡처와 오버플로가 동시에 pending인 경계 케이스를 테스트한다. | mac |
| BM-M07 | ADC 드라이버: 스캔 변환, 샘플링 시간, 보정 | mock ADC 모델(채널별 입력 전압→코드, EOC 플래그)을 만들고 스캔 시퀀스 설정·변환 결과 → mV 변환(Vref 보정 포함)·오버샘플링 평균을 구현해 기대값과 비교한다. | mac |
| BM-M08 | DMA 엔진 드라이버: 채널 설정, circular, HT/TC 인터럽트 | 메모리↔주변장치 전송을 단계별로 진행시키는 DMA 모델을 만들고 normal/circular 모드, half-transfer/transfer-complete 플래그, NDTR 감소를 드라이버와 함께 테스트한다. | mac |
| BM-M09 | ADC + DMA 더블 버퍼링 파이프라인 | ADC 모델이 DMA circular 버퍼를 채우는 동안 HT/TC 콜백에서 반쪽씩 평균·필터링하도록 구현하고, 처리 지연을 인위적으로 늘려 데이터 덮어쓰기(overrun)를 감지하는 로직을 테스트한다. | mac |
| BM-M10 | 워치독 하드웨어: IWDG/WWDG 타임아웃·윈도우 계산 | LSI 클럭·프리스케일러·reload로 IWDG 타임아웃 범위를, WWDG 윈도우 값으로 '너무 이른 킥' 리셋 조건을 계산하는 함수와 mock 워치독 모델을 작성하고, 디버그 정지(DBGMCU freeze)와 리셋 원인 플래그 확인까지 테스트한다(태스크 체크인 설계는 see:TEST-B09). | mac |
| BM-M11 | 저전력 모드: WFI/WFE, Stop/Standby, 슬립 진입 레이스 | 전력 상태 머신 시뮬레이터로 duty cycle별 평균 전류를 계산하고, 인터럽트 비활성→조건 확인→WFI→활성 패턴이 웨이크업 이벤트 유실을 막는 이유를 이벤트 타이밍 테스트로 보인다. | mac |
| BM-M12 | RTC와 저전력 타임키핑: 알람 웨이크업·백업 도메인 | BCD 시간 레지스터·알람·쓰기 보호 해제 시퀀스를 가진 mock RTC 모델로 드라이버(set/get, epoch 변환, 알람 웨이크업)를 구현하고, 32.768kHz 프리스케일러 계산과 섀도 레지스터 읽기 일관성(두 번 읽기) 문제를 테스트한다. | mac |
| BM-M13 | Fault 처리 정책: 세분 fault 활성화와 안전 상태 진입 | SHCSR로 MemManage/BusFault/UsageFault를 분리 활성화하고 CCR의 DIV_0_TRP/UNALIGN_TRP를 켠 뒤, 각 핸들러가 출력 핀을 안전 상태로 두고 리셋 사유를 기록·리셋하도록 QEMU에서 구현해 재부팅 후 리셋 원인을 보고한다. | **qemu** |
| BM-A01 | SystemInit 순서: FPU 활성화, VTOR 재배치, 클럭 전환 | CPACR로 FPU를 켜기 전 float 연산 시 fault가 나는 것을 Cortex-M4 타깃 QEMU에서 재현하고, 벡터 테이블을 RAM으로 복사해 VTOR를 바꾼 뒤 런타임 핸들러 교체가 동작함을 확인한다. | **qemu** |
| BM-A02 | 플래시 드라이버 시뮬레이터: 섹터 erase/program 규칙 | erase 전 재프로그램 시 비트가 AND되는 플래시 모델과 erase/program/verify 드라이버를 구현하고, 비정렬 쓰기·섹터 경계 넘는 쓰기·erase 중 전원 차단(일부 비트 미정) 케이스를 테스트한다. | mac |
| BM-A03 | 부트로더 → 앱 점프: VTOR, MSP, 주변장치 정리 | 부트로더와 앱 두 이미지를 별도 링커 스크립트로 빌드해 하나의 플래시 이미지로 합치고, 인터럽트 비활성·SysTick 정지·pending 클리어·VTOR/MSP 설정 후 앱 reset vector로 점프하는 것을 QEMU에서 검증한다. | **qemu** |
| BM-A04 | 펌웨어 이미지 헤더 + CRC/SHA-256 검증 | python 이미지 패키징 도구(헤더+페이로드+CRC32/SHA-256)와 C 검증 코드를 작성하고, 비트 플립·잘림·버전 롤백 이미지를 거부하는지 테스트한다(서명 검증은 see:SEC). | mac |
| BM-A05 | 시리얼 부트로더: YMODEM 수신과 플래시 기록 | YMODEM(128/1024 블록, CRC-16, ACK/NAK/CAN) 수신 상태 머신을 A02 플래시 시뮬레이터와 연결해 블록 단위 erase/program·재전송·중단 처리를 구현하고, 노이즈·타임아웃 주입 테스트로 검증한다(A/B 슬롯 결정 로직은 see:PLAT-M07). | mac |
| BM-A06 | 전원 차단 내성 업데이트: 모든 쓰기 지점 power-cut 주입 | MCUboot 식 scratch 스왑(또는 이중 메타데이터+시퀀스 번호)을 플래시 모델에 구현하고, 플래시 쓰기/erase 호출 N번째마다 전원 차단을 주입하는 전수 테스트로 항상 유효 이미지로 부팅됨을 검증한다. | mac |
| BM-A07 | 스택 오버플로 탐지: 스택 페인팅과 high-water mark | 스택을 0xDEADBEEF로 칠해 최대 사용량을 측정하는 함수와 재귀 함수로 오버플로를 일으키는 테스트를 QEMU에서 실행하고, -fstack-usage 정적 추정치와 비교한다. | **qemu** |
| BM-A08 | MPU 설정: NULL 트랩, 스택 가드, 실행 금지 RAM | 주소 0 근처 no-access 영역, 스택 하단 32B 가드, RAM XN 영역을 설정하는 코드를 작성하고 QEMU에서 각각 위반 시 MemManage fault와 MMFAR 값이 기대대로 나오는지 확인한다. | **qemu** |
| BM-A09 | 크래시 덤프: .noinit RAM에 fault 기록, 리셋 후 보고 | fault 핸들러가 레지스터·스택 일부·magic+CRC를 .noinit 섹션에 저장 후 리셋하고, 재부팅 시 유효성 검사 후 UART로 리포트하는 흐름을 QEMU에서 구현·검증한다(콜드 부트 쓰레기 값 구분 포함). | **qemu** |
| BM-A10 | 인터럽트 지연 측정: DWT CYCCNT, tail-chaining, late arrival | GPIO 토글+DWT CYCCNT로 IRQ 발생→ISR 첫 명령까지의 사이클과 연속 IRQ의 tail-chaining 절감을 측정하고, 플래시 wait state·FPU lazy stacking이 지연에 주는 영향을 표로 정리한다. | **board** |
| BM-A11 | 종합: 베어메탈 데이터 로거 | 자작 startup·링커 스크립트 위에 타이머 주기 샘플링, 인터럽트 UART 출력, 워치독, HardFault 덤프, 이미지 헤더 검증을 통합한 로거를 QEMU에서 동작시키고 메모리 사용량·모듈 구조를 문서화한다. | **qemu** |

### COMM — 통신 프로토콜 & 드라이버 (Comm Protocols & Drivers)

펌웨어 엔지니어 업무의 절반은 칩과 칩, 보드와 보드를 잇는 통신이다: UART·SPI·I2C로 센서를 읽고, CAN으로 차량·로봇 노드를 잇고, 바이트 스트림 위에 프레이밍·CRC·재전송을 얹는다. 이 트랙은 버스를 비트 수준까지 소프트웨어로 시뮬레이션해 프로토콜 동작과 오류 상황(클럭 스트레칭, 버스 hang, 중재 패배, bus-off)을 맥에서 재현하고, 견고한 파서·드라이버 계층을 직접 설계한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| COMM-B01 | UART 프레임: start/data/parity/stop 비트 직렬화 | 바이트+설정(8N1/7E1/8O2)을 비트 타임라인 배열로 바꾸는 송신기와 역변환 수신기를 작성하고, 패리티 계산과 여러 설정 조합을 왕복 테스트한다. | mac |
| COMM-B02 | UART 수신기 모델: 16x 오버샘플링과 에러 검출 | 16배 샘플 스트림을 입력받는 수신기 상태 머신을 구현하고, 글리치·stop 비트 0(framing error)·break 조건·읽기 지연으로 인한 overrun을 주입해 각 에러 플래그를 검증한다. | mac |
| COMM-B03 | Baud rate 오차 계산과 허용 한계 | 클럭·목표 baud로 실제 baud와 % 오차를 계산하는 함수를 만들고, 앞의 수신기 모델에 클럭 편차를 넣어 몇 % 부터 마지막 비트 샘플링이 실패하는지 실험해 이론(약 ±2~5%)과 비교한다. | mac |
| COMM-B04 | SPI 4가지 모드(CPOL/CPHA): 비트뱅 마스터+슬레이브 시뮬레이터 | SCLK/MOSI/MISO/CS 신호를 시간 스텝으로 시뮬레이션하는 비트뱅 마스터와 슬레이브 모델을 구현해 4개 모드 전이중 교환을 테스트하고, 모드 불일치 시 1비트 밀림을 재현한다. | mac |
| COMM-B05 | SPI 디바이스 접근: 명령/주소 포맷, burst, CS 타이밍 | R/W 비트+주소+데이터 프로토콜을 가진 mock IMU 슬레이브를 만들고 단일/burst 읽기·쓰기 드라이버를 작성해, CS를 트랜잭션 중간에 해제하는 버그를 재현·수정한다. | mac |
| COMM-B06 | I2C 비트 수준: START/STOP, 주소+R/W, ACK/NACK | SDA/SCL을 여러 노드의 AND로 계산하는 버스 시뮬레이터와 비트뱅 마스터·슬레이브를 만들고, 주소 NACK·데이터 NACK·SCL high 중 SDA 변화(START/STOP) 규칙을 테스트한다. | mac |
| COMM-B07 | I2C 레지스터 읽기: repeated START와 센서 드라이버 | mock 온도 센서 슬레이브(레지스터 맵 포함)를 버스 시뮬레이터에 붙이고 i2c_write_read() 기반 드라이버로 ID 확인·설정·연속 읽기를 구현, STOP 후 START와 repeated START의 차이를 로그로 보인다. | mac |
| COMM-B08 | 오류 검출 능력 실험: 체크섬 vs CRC | 랜덤 1~4비트 오류와 길이 L 버스트 오류를 수백만 회 주입해 합 체크섬, Fletcher-16, CRC-8, CRC-16, CRC-32의 미검출률을 측정하고 해밍 거리 이론과 비교한다(알고리즘 구현은 see:ALGO-B14/M01). | mac |
| COMM-B09 | 하드웨어 CRC 유닛과 소프트웨어 CRC 맞추기 | STM32 CRC 유닛 동작(32비트 워드 입력, 초기값, 입력/출력 반사 옵션)을 흉내낸 mock과 표준 CRC-32 소프트웨어 구현이 같은 값을 내도록 설정·바이트 순서를 찾는 테스트를 작성한다(테이블/slicing 가속은 see:ALGO-M02). | mac |
| COMM-B10 | 길이 prefix + 체크섬 프레임 인코더/파서 | [SOF][LEN][TYPE][PAYLOAD][CRC16] 포맷의 인코더와 바이트 단위 파서 상태 머신을 작성하고, 잘린 프레임·CRC 오류·LEN 과대값·스트림 중간 시작을 테스트한다. | mac |
| COMM-B11 | SLIP 인코더/디코더 (RFC 1055) | SLIP encode/decode를 구현하고 END·ESC 바이트가 섞인 페이로드, 연속 END, 잘못된 이스케이프 시퀀스를 테스트하며 최악 2배 오버헤드를 측정한다. | mac |
| COMM-B12 | COBS 인코더/디코더 + 랜덤 왕복 테스트 | COBS encode/decode를 구현해 길이 0~1000 랜덤 버퍼 왕복 테스트, 254/255바이트 경계 케이스, 손상 입력 거부를 검증하고 SLIP과 오버헤드를 비교한다. | mac |
| COMM-B13 | Modbus RTU 프레임: 함수 코드, CRC-16, 3.5문자 침묵 | 타임스탬프가 붙은 바이트 스트림에서 3.5문자 침묵으로 프레임을 자르고 CRC-16/MODBUS를 검증하는 슬레이브를 구현해 Read Holding Registers(0x03)/Write Single Register(0x06)와 예외 응답을 테스트한다. | mac |
| COMM-B14 | UART 커맨드 라인/AT 커맨드 파서 | 백스페이스·CR/LF 혼용·최대 길이 초과를 처리하는 라인 에디터와 명령 테이블 디스패처(인자 토큰화, 숫자 범위 검사)를 구현하고 악성 입력 테스트를 작성한다. | mac |
| COMM-M01 | I2C 클럭 스트레칭 지원 마스터 | 버스 시뮬레이터의 슬레이브가 ACK 후 SCL을 N스텝 붙잡게 만들고, SCL을 놓은 뒤 실제 high인지 읽어 확인하는 마스터와 스트레칭 최대 시간 초과 시 에러 반환을 구현·테스트한다. | mac |
| COMM-M02 | I2C 멀티마스터 중재 (arbitration lost) | 두 마스터가 같은 시점에 다른 주소로 START하도록 시뮬레이션해 1을 보냈는데 0을 읽은 쪽이 물러나는지, 패배 마스터의 재시도 로직이 올바른지 검증한다. | mac |
| COMM-M03 | I2C 버스 복구: SDA stuck-low에서 9클럭 + STOP | 읽기 도중 마스터가 리셋된 상황(슬레이브가 데이터 0 비트 출력 중)을 시뮬레이션하고, SCL 최대 9펄스 토글→SDA 해제 확인→STOP 생성 복구 루틴을 구현해 hang이 풀림을 테스트한다. | mac |
| COMM-M04 | CAN 프레임 구조: 비트 스터핑과 CRC-15 | ID·DLC·데이터로 SOF~EOF 비트열을 생성(CRC-15 포함)하고 5연속 동일 비트 후 스터프 비트를 삽입·제거하는 인코더/디코더를 작성해 알려진 프레임 비트열과 대조한다. | mac |
| COMM-M05 | CAN 중재 시뮬레이션: 낮은 ID가 이기는 이유 | 여러 노드가 동시에 송신을 시작하는 버스 시뮬레이터를 만들어 중재 패배 노드가 수신 모드로 전환·재시도하는 것을 보이고, 표준 vs 확장 ID(SRR/IDE) 경쟁 시 결과를 검증한다. | mac |
| COMM-M06 | CAN 비트 타이밍 계산기: TQ, 세그먼트, 샘플 포인트 | CAN 클럭·목표 비트레이트·목표 샘플 포인트(예: 87.5%)로 가능한 BRP/TSEG 조합을 열거하고 오차 최소 설정을 고르는 계산기를 만들어 500k/1M 표준 설정과 비교한다. | mac |
| COMM-M07 | CAN 에러 처리: TEC/REC와 error-passive·bus-off | 에러 이벤트(송신 에러, 수신 에러, 성공 송신)에 따른 TEC/REC 증감과 error-active→passive→bus-off→(128×11 recessive) 복구 상태 머신을 구현하고 시나리오 테스트를 작성한다. | mac |
| COMM-M08 | CAN FD: BRS, DLC→64바이트 매핑, 데이터 위상 비트레이트 | DLC↔길이(0~8,12,16,20,24,32,48,64) 변환, FD 플래그(FDF/BRS/ESI) 처리, 중재/데이터 위상 비트레이트별 프레임 전송 시간과 버스 효율 계산기를 작성해 Classic CAN과 비교한다. | mac |
| COMM-M09 | SocketCAN 실습: vcan, candump, C 소켓 프로그램 | vcan0을 만들고 C로 raw CAN 소켓 송신/수신 프로그램과 ID 필터(CAN_RAW_FILTER)를 작성해 candump/cansend로 검증하고, CAN FD 소켓 옵션도 시험한다. | **linux** |
| COMM-M10 | 견고한 패킷 파서: 재동기화·타임아웃·통계 | MAVLink/UBX 유사 포맷 파서를 바이트 단위 상태 머신으로 구현하고, 프레임 간 바이트 타임아웃, 잘못된 SOF 후 버퍼 내 재검색, 에러 종류별 카운터를 추가해 랜덤 손상 스트림으로 복원율을 측정한다. | mac |
| COMM-M11 | DMA 기반 UART 수신: circular DMA + idle line 감지 | DMA circular 버퍼 모델(NDTR 감소)과 IDLE/HT/TC 이벤트를 시뮬레이션해, 마지막 처리 위치부터 현재 쓰기 위치까지를 파서로 넘기는 로직(랩어라운드 포함)을 구현·테스트한다. | mac |
| COMM-M12 | 드라이버 계층화: 버스 추상화로 I2C/SPI 겸용 센서 드라이버 | read_regs/write_regs 함수 포인터 기반 bus_ops 인터페이스를 정의해 같은 IMU 드라이버가 I2C와 SPI mock 버스 양쪽에서 동작하게 만들고, 계층별 에러 코드 변환 규칙을 문서화한다. | mac |
| COMM-M13 | 공유 버스 트랜잭션 큐: 비동기 요청과 완료 콜백 | 트랜잭션 디스크립터(CS, tx/rx 버퍼, 콜백) 큐와 완료 인터럽트 시뮬레이션으로 다음 트랜잭션을 자동 시작하는 비동기 SPI 드라이버를 구현하고, 큐 포화·취소·타임아웃을 테스트한다. | mac |
| COMM-M14 | 재시도·타임아웃·백오프: stop-and-wait ARQ | 패킷 손실·중복·지연을 주입하는 링크 시뮬레이터 위에 시퀀스 번호+ACK+타임아웃 재전송 프로토콜을 구현하고, 중복 수신 제거와 최대 재시도 후 실패 보고를 검증한다. | mac |
| COMM-M15 | USB 기초: 디스크립터 파싱과 enumeration 흐름 | 실제 장치 디스크립터 바이트 덤프(예시 제공 또는 system_profiler/lsusb -v에서 확보)를 파싱해 사람이 읽을 수 있게 출력하는 툴을 작성하고, 엔드포인트 타입(control/bulk/interrupt/iso)별 용도를 정리한다. | mac |
| COMM-A01 | USB 컨트롤 전송 상태 머신 + CDC-ACM 클래스 요청 | 호스트 요청 시퀀스(GET_DESCRIPTOR, SET_ADDRESS, SET_CONFIGURATION, SET_LINE_CODING)를 주입하는 시뮬레이터로 EP0 상태 머신과 표준·CDC 요청 핸들러를 구현하고, wLength보다 짧은 응답·ZLP 처리를 테스트한다. | mac |
| COMM-A02 | 슬라이딩 윈도 신뢰 전송: Go-Back-N vs Selective Repeat | 손실률·RTT를 조절 가능한 링크 시뮬레이터에서 GBN과 SR을 구현하고, 윈도 크기·손실률별 처리량 곡선을 측정해 시퀀스 번호 공간 요구조건(SR: 2W)을 검증한다. | mac |
| COMM-A03 | Ethernet PHY와 MDIO: Clause 22 레지스터와 링크 상태 | MDIO 프레임(ST/OP/PHYAD/REGAD/TA) 비트뱅과 mock PHY(BMCR/BMSR/PHYID/ANAR/ANLPAR)를 구현하고, 소프트 리셋→오토네고 시작→링크업 폴링→협상 결과 해석 드라이버를 테스트한다. | mac |
| COMM-A04 | Ethernet/IPv4/UDP 프레임 파서와 체크섬 | pcap 파일을 읽어 Ethernet(VLAN 태그 포함)→IPv4(헤더 체크섬 검증)→UDP(의사 헤더 체크섬)까지 파싱하는 C 툴을 작성하고 Wireshark 출력과 대조한다. | mac |
| COMM-A05 | I3C 기초: 동적 주소 할당과 In-Band Interrupt | I3C 버스 시뮬레이터에서 ENTDAA로 48비트 Provisioned ID 기반 동적 주소 할당과 IBI 중재를 구현하고, 레거시 I2C 디바이스가 섞인 버스에서 주소 충돌 없이 동작함을 테스트한다(MIPI CSI-2는 see:CAM-B05). | mac |
| COMM-A06 | SPI NOR 플래시 드라이버: JEDEC ID, WEL, 페이지 프로그램 | 명령(0x9F, 0x06, 0x02, 0x20, 0x05, 0x03)을 해석하는 mock SPI NOR 모델(페이지 경계 wrap, busy 시간)을 만들고, 페이지 경계 분할 쓰기·erase·WIP 폴링·쓰기 보호 해제 드라이버를 테스트한다. | mac |
| COMM-A07 | ISO-TP (ISO 15765-2): CAN 위 분할·재조립과 흐름 제어 | 송신·수신 ISO-TP 상태 머신(BlockSize, STmin, 시퀀스 번호 랩, N_Cr 타임아웃)을 구현해 4095바이트 메시지 왕복과 프레임 손실·FC Wait/Overflow 시나리오를 테스트한다(진단 UDS는 see:AUTO). | mac |
| COMM-A08 | 제로카피 DMA 드라이버 설계: 디스크립터 링과 캐시 일관성 | OWN 비트를 가진 RX/TX 디스크립터 링과 DMA 엔진 모델을 구현하고, 버퍼 재활용·링 가득 참·캐시 라인 정렬 요구를 반영한 드라이버 코드 경로를 작성해 언제 clean/invalidate가 필요한지 주석으로 정당화한다. | mac |
| COMM-A09 | 버스 트랜잭션 녹화·재생으로 드라이버 회귀 테스트 | M12 버스 추상화에 트랜잭션 로거를 끼워 실보드(또는 합성) 로그를 저장하고, 재생 모드에서 드라이버가 같은 순서의 트랜잭션을 내는지와 응답 변형(NACK, 지연)에 견디는지 테스트한다(파서 퍼징은 see:TEST-M02). | mac |
| COMM-A10 | 로직 애널라이저 디코더: 샘플 캡처 → UART/SPI/I2C 디코드 | CSV/VCD 형태 디지털 샘플(타임스탬프+채널 값)을 읽어 UART(자동 baud 추정)·SPI(모드 지정)·I2C(START/STOP/ACK) 트랜잭션을 출력하는 디코더를 작성하고, 앞서 만든 시뮬레이터 출력으로 검증한다. | mac |
| COMM-A11 | 종합: 센서 허브 통신 스택 + 장애 주입 | I2C 센서 2개+SPI NOR 로그 저장+UART COBS 텔레메트리(ACK/재전송)로 된 허브를 시뮬레이터 위에 구현하고, 버스 hang·CRC 오류·센서 무응답을 주입하는 테스트로 복구 동작과 에러 통계를 보고한다. | mac |

### RTOS — RTOS (Real-Time OS)

RTOS는 '빠른' OS가 아니라 '예측 가능한' OS다: 우선순위 선점 스케줄링, 동기화 프리미티브, ISR 지연 처리로 데드라인을 보장한다. 펌웨어 면접에서는 컨텍스트 스위치 원리, 우선순위 역전과 상속, 세마포어 vs 뮤텍스, 스케줄 가능성 분석, 스택 크기 산정이 반복해서 나오므로, 이 트랙은 ucontext 기반 미니 커널을 직접 만들고 FreeRTOS POSIX 포트·QEMU Cortex-M으로 실제 커널 동작을 확인한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| RTOS-B01 | 슈퍼루프에서 협력형 스케줄러로 | 주기가 다른 작업 3개를 슈퍼루프와 타임스탬프 기반 협력형 스케줄러(작업 테이블+주기+다음 실행 시각)로 각각 구현하고, 긴 작업 하나가 다른 작업의 지터를 얼마나 늘리는지 측정·비교한다. | mac |
| RTOS-B02 | TCB 설계와 태스크 상태 전이 | TCB 구조체(스택 포인터, 우선순위, 상태, 지연 만료 시각, 대기 객체, 리스트 노드)와 상태 전이 함수들을 구현하고, 불법 전이(Blocked→Running 직행 등)를 assert로 막는 테스트를 작성한다. | mac |
| RTOS-B03 | ucontext로 컨텍스트 스위치 체험 | getcontext/makecontext/swapcontext로 별도 스택을 가진 두 태스크가 yield로 번갈아 실행되게 만들고, 각 태스크의 지역 변수·스택 주소가 보존되는지 출력으로 검증한다. | mac |
| RTOS-B04 | 미니 커널 1: 우선순위 비트맵 ready list (CLZ) | 32 우선순위 ready 비트맵과 우선순위별 FIFO 리스트를 구현해 ucontext 미니 커널의 스케줄러로 쓰고, 동일 우선순위 FIFO 순서·최고 우선순위 선택을 단위 테스트한다. | mac |
| RTOS-B05 | 선점과 라운드 로빈: SIGALRM tick 시뮬레이션 | setitimer(SIGALRM)를 tick 인터럽트로 삼아 미니 커널에 선점과 time slice를 추가하고, 무한 루프 태스크가 있어도 다른 태스크가 실행되는지·slice 길이별 문맥 교환 횟수를 측정한다. | mac |
| RTOS-B06 | 태스크 지연: Delay vs DelayUntil, tick wrap 안전 비교 | 미니 커널에 delayed list와 delay(ticks)/delay_until(&last, period)을 추가하고, 작업 시간이 들쭉날쭉할 때 두 방식의 주기 드리프트를 측정, tick 카운터 32비트 wrap 근처에서도 동작함을 테스트한다. | mac |
| RTOS-B07 | FreeRTOS POSIX 포트 첫 실행: 태스크·우선순위·스택 | FreeRTOSConfig.h를 직접 작성해 우선순위가 다른 태스크 3개를 실행하고, vTaskList/uxTaskGetSystemState로 상태·스택 여유를 출력해 우선순위 변경(vTaskPrioritySet) 효과를 관찰한다. | mac |
| RTOS-B08 | 바이너리 세마포어: ISR → 태스크 신호 | 타이머 스레드를 ISR 대용으로 써서 xSemaphoreGiveFromISR→처리 태스크 패턴을 구현하고, 처리보다 빠르게 신호가 올 때 이벤트가 합쳐져 유실되는 것을 재현해 카운팅 세마포어와 비교한다. | mac |
| RTOS-B09 | 카운팅 세마포어: 리소스 풀과 이벤트 카운트 | 미니 커널에 카운팅 세마포어(대기 리스트 포함)를 구현해 DMA 채널 3개를 5개 태스크가 공유하는 시나리오와 이벤트 카운트 시나리오를 테스트한다. | mac |
| RTOS-B10 | 뮤텍스 vs 세마포어: 소유권과 재귀 뮤텍스 | FreeRTOS에서 다른 태스크가 뮤텍스를 give할 때의 동작, 재귀 뮤텍스 중첩 take/give, 세마포어로 상호배제했을 때의 문제를 각각 실험하고 결과를 표로 정리한다. | mac |
| RTOS-B11 | 메시지 큐: 생산자-소비자, 타임아웃, 복사 vs 포인터 | 센서 태스크→처리 태스크→로깅 태스크 파이프라인을 xQueue로 만들고, 큐 깊이·블로킹 타임아웃·큐 가득 참 정책을 실험하며, 큰 버퍼는 포인터+풀로 넘기는 버전과 비교한다. | mac |
| RTOS-B12 | 소프트웨어 타이머: one-shot·auto-reload·타이머 태스크 | LED 블링크(auto-reload)와 통신 타임아웃(one-shot, 수신 시 reset)을 소프트웨어 타이머로 구현하고, 콜백에서 블로킹 호출 시 다른 타이머가 지연되는 문제와 타이머 태스크 우선순위 영향을 실험한다. | mac |
| RTOS-B13 | 이벤트 그룹: wait-all/any와 clear-on-exit | 미니 커널에 이벤트 그룹(set/clear/wait with all|any, clear on exit)을 구현하고, 3개 초기화 태스크가 모두 끝나야 메인 태스크가 시작되는 시나리오와 경쟁 조건을 테스트한다. | mac |
| RTOS-M01 | 태스크 알림: 세마포어·이벤트 그룹의 경량 대체 | 앞의 ISR→태스크 신호와 이벤트 그룹 예제를 xTaskNotify/ulTaskNotifyTake로 다시 구현해 RAM 사용량·호출 횟수당 시간을 비교하고, 수신자가 둘 이상 필요한 경우 왜 안 되는지 설명한다. | mac |
| RTOS-M02 | ISR 지연 처리: FromISR API와 yield 요청 | 미니 커널에 'ISR 컨텍스트' 플래그와 give_from_isr(&higher_woken)을 구현해 ISR 종료 시점에만 스위치가 일어나게 하고, 블로킹 API를 ISR에서 호출하면 assert되게 만든다. | mac |
| RTOS-M03 | 우선순위 역전 재현: Mars Pathfinder 시나리오 | 미니 커널에서 H/M/L 태스크와 일반 세마포어로 unbounded priority inversion을 만들어 실행 타임라인(tick별 실행 태스크)을 출력하고, High의 대기 시간이 Medium 실행 길이에 비례함을 보인다. | mac |
| RTOS-M04 | 우선순위 상속 구현과 연쇄 상속 | 미니 커널 뮤텍스에 priority inheritance를 구현(다중 뮤텍스 보유 시 복원 우선순위 계산, A→B→C 연쇄 상속 포함)하고, 앞 시나리오의 High 대기 시간이 bounded됨을 타임라인으로 검증한다. | mac |
| RTOS-M05 | 우선순위 천장 프로토콜(PCP/IPCP)과 교착 방지 | Immediate Priority Ceiling 뮤텍스를 구현하고 두 뮤텍스를 반대 순서로 잡는 교착 시나리오가 상속 방식에선 교착, IPCP에선 회피됨을 보이며, 최대 블로킹 시간이 임계구역 1개로 제한됨을 설명한다. | mac |
| RTOS-M06 | 블로킹 큐 구현: 대기 리스트와 타임아웃 | 미니 커널에 고정 크기 메시지 큐를 구현해 가득 참/빔 시 블로킹, 대기자 중 최고 우선순위부터 깨우기, 타임아웃 시 대기 리스트 제거를 구현하고 경쟁 시나리오를 테스트한다. | mac |
| RTOS-M07 | Tick 처리와 지연 리스트: 정렬 리스트 vs 타이머 휠 | 만료 시각 정렬 리스트(FreeRTOS 방식, overflow 리스트 포함)와 hashed timing wheel을 구현해 타이머 1만 개에서 tick당 처리 비용과 삽입 비용을 측정·비교한다. | mac |
| RTOS-M08 | 태스크 스택 예산: ISR 중첩·예외 프레임 포함 최악 스택 | TOOL-A08의 함수별 스택 사용량에 컨텍스트 저장 프레임(FPU 지연 스태킹 포함)과 최대 ISR 중첩 깊이를 더한 태스크별 스택 예산표를 계산하고, FreeRTOS POSIX 포트의 uxTaskGetStackHighWaterMark 실측과 비교해 여유율 정책을 정한다. | mac |
| RTOS-M09 | 스택 오버플로 탐지: 카나리와 패턴 검사 | 미니 커널 태스크 스택 끝에 패턴을 채우고 컨텍스트 스위치마다 SP 범위 검사(방법1)+끝 16바이트 패턴 검사(방법2)를 넣어, 큰 지역 배열 태스크의 오버플로를 잡는지와 놓치는 경우(가드 건너뛰기)를 보인다. | mac |
| RTOS-M10 | RTOS 힙 전략: heap_1~heap_5 비교와 heap_4 coalescing | heap_4 방식(주소 정렬 free list + 인접 블록 병합) 할당기를 구현하고, 무작위 할당/해제 워크로드에서 heap_2 방식(병합 없음) 대비 단편화·최대 할당 실패 시점을 측정한다(일반 malloc 설계는 see:OS). | mac |
| RTOS-M11 | Rate Monotonic 분석: Liu-Layland 경계와 RTA | (C, T, D) 태스크 세트를 입력받아 RM 우선순위 할당, U ≤ n(2^(1/n)-1) 판정, 반복식 응답시간 분석 R = C + Σ⌈R/Tj⌉Cj를 계산하는 도구를 작성하고, 경계는 실패지만 RTA는 통과하는 예를 찾는다. | mac |
| RTOS-M12 | EDF 스케줄링 시뮬레이터와 RM 비교 | 주기 태스크 세트를 하이퍼피리어드 동안 RM과 EDF로 시뮬레이션해 데드라인 미스·선점 횟수를 비교하고, U > 1 과부하에서 EDF의 연쇄 미스(domino effect)를 보인다. | mac |
| RTOS-M13 | FreeRTOS 내부 읽기: xTaskCreate부터 vTaskSwitchContext까지 | FreeRTOS-Kernel의 tasks.c/list.c/queue.c에서 태스크 생성→ready 리스트 삽입→xTaskIncrementTick→taskSELECT_HIGHEST_PRIORITY_TASK 경로를 따라가 호출 다이어그램을 그리고, 미니 커널과의 차이 5가지를 코드 근거로 정리한다. | mac |
| RTOS-M14 | 재진입성과 스레드 안전: 공유 라이브러리 함수 보호 | 미니 커널에서 정적 버퍼를 쓰는 포맷 함수와 strtok을 두 태스크가 선점 하에 공유해 출력이 섞이는 버그를 재현하고, 재진입 버전(_r)·뮤텍스 래핑·태스크별 컨텍스트(newlib __malloc_lock/reent 개념)로 각각 수정한다. | mac |
| RTOS-A01 | Cortex-M 미니 RTOS 완성: 우선순위 스케줄러 + 동기화 | ARM-A01의 PendSV 스위처를 바탕으로 태스크 초기 스택 프레임 구성·SVC 첫 태스크 시작을 추가하고, SysTick·비트맵 스케줄러·세마포어·뮤텍스(상속)·delay를 QEMU에서 동작시키고, 우선순위 역전 시나리오와 크리티컬 섹션(BASEPRI) 처리를 데모로 검증한다. | **qemu** |
| RTOS-A02 | Tickless idle: 다음 만료 계산과 tick 보정 | 미니 커널 idle에서 다음 지연 만료까지 tick을 억제하고 단발 타이머로 깨어난 뒤 경과 tick을 보정하는 로직을 시뮬레이션 시간으로 구현하고, 조기 인터럽트 웨이크업·최대 억제 길이 제한 케이스를 테스트한다. | mac |
| RTOS-A03 | WCET 측정과 지터 분석 | 필터/제어 루프 함수를 다양한 입력으로 수만 회 실행하며 사이클(호스트는 mach_absolute_time, 보드는 DWT CYCCNT)을 측정해 히스토그램·최대값·백분위를 내고, 캐시 플러시·입력 경로 차이가 최악값에 주는 영향을 분석한다. | mac |
| RTOS-A04 | 확장 RTA: 블로킹 시간·릴리즈 지터·ISR 부하 반영 | RTA 도구에 블로킹 항 B(천장 프로토콜 기준), 릴리즈 지터 J, ISR을 최고 우선순위 태스크로 모델링하는 기능을 추가하고, 시뮬레이터 결과와 대조해 분석이 보수적(안전)인지 확인한다. | mac |
| RTOS-A05 | 비주기 작업 서버: polling server vs sporadic server | 스케줄링 시뮬레이터에 background 처리, polling server, sporadic server(예산 보충 규칙)를 구현해 비주기 요청 평균/최대 응답 시간과 주기 태스크 데드라인 준수를 비교한다. | mac |
| RTOS-A06 | RTOS 트레이싱: 훅으로 이벤트 기록 → 타임라인 | FreeRTOS traceTASK_SWITCHED_IN/OUT 등 트레이스 매크로로 타임스탬프 이벤트를 링버퍼에 기록하고, python으로 태스크별 실행 타임라인(SVG/HTML)과 CPU 점유율을 생성한다. | mac |
| RTOS-A07 | Zephyr 기초: west 빌드, 스레드·k_sem·k_msgq | Zephyr SDK로 qemu_cortex_m3 보드 대상 앱을 빌드해 생산자/소비자 스레드(k_msgq), k_sem, k_timer를 구현하고, 같은 앱의 FreeRTOS 버전과 API·설정 방식·이미지 크기를 비교한다. | **qemu** |
| RTOS-A08 | SMP RTOS: 코어 친화성, 스핀락, 공유 ready list | pthread 2개를 '코어'로 삼은 스케줄러 시뮬레이터에 전역 ready list+스핀락, 코어 친화성 마스크, 크로스 코어 yield(IPI 대용 시그널)를 구현하고, 단일 코어 가정(인터럽트 끄기=상호배제)이 깨지는 사례를 보인다. | mac |
| RTOS-A09 | 데드라인 미스 사후 분석: 트레이스 케이스 스터디 | 의도적으로 버그(우선순위 역전, ISR 과다 실행, 큐 포화로 인한 블로킹, 잘못된 우선순위 할당)를 심은 시스템 4개를 실행해 트레이스를 얻고, 각 원인을 트레이스 근거로 진단한 리포트와 수정을 작성한다. | mac |
| RTOS-A10 | RTOS 시스템 설계: 모터 제어 + 통신 + 로깅 | 10kHz 전류 루프(ISR), 1kHz 속도 루프, CAN 명령 수신, 플래시 로깅, 워치독이 있는 컨트롤러의 태스크/ISR 구조, 우선순위, 큐 깊이, 스택 예산을 설계하고 RTA로 스케줄 가능성을 보인 뒤 핵심 경로를 미니 커널로 구현한다. | mac |

### OS — 운영체제 & Core OS (Operating Systems & Core OS)

범용 OS의 핵심 개념(프로세스, 가상 메모리, 동시성, 파일 시스템, I/O)은 임베디드 리눅스·플랫폼·드라이버 면접의 공통 언어다. OSTEP으로 개념을 익혔다면 이 트랙에서는 셸·malloc·락·페이지 테이블·파일 시스템·스케줄러를 직접 구현하고, Apple Silicon의 약한 메모리 모델 위에서 메모리 오더링을 실험하며, xv6 커널을 수정해 개념을 손에 붙인다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| OS-B01 | 프로세스 생성: fork/exec/wait로 미니 셸 | 명령 파싱→fork→execvp→waitpid로 동작하는 미니 셸을 작성하고 종료 코드/시그널 종료 구분(WIFEXITED/WIFSIGNALED), 백그라운드 실행(&)과 SIGCHLD로 좀비 회수를 구현한다. | mac |
| OS-B02 | 파일 디스크립터·dup2·파이프: 파이프라인과 리다이렉션 | 미니 셸에 a | b | c 다단 파이프와 <, >, 2>&1 리다이렉션을 추가하고, 닫지 않은 파이프 fd 때문에 EOF가 오지 않아 hang되는 버그를 재현·수정한다. | mac |
| OS-B03 | 시스템 콜 비용 측정: 사용자↔커널 경계 | 빈 함수 호출, getpid, read(0바이트), clock_gettime을 각각 수백만 회 반복해 호출당 ns를 측정하고(OSTEP 측정 과제), 결과 차이를 모드 전환·캐시 효과로 설명한다(리눅스에선 strace -c로 집계해 보기). | mac |
| OS-B04 | 스레드 기초: pthread 생성과 공유 카운터 레이스 | 스레드 N개가 공유 카운터를 각 100만 번 증가시키는 프로그램으로 레이스를 재현하고, counter++의 ldr/add/str 어셈블리를 보며 손실 원인을 설명한 뒤 ThreadSanitizer로 탐지한다. | mac |
| OS-B05 | 뮤텍스: 레이스 해결과 경합 비용 측정 | 공유 카운터를 전역 락, 스레드 로컬 누적 후 합산(sloppy counter, OSTEP), atomic fetch_add로 각각 구현해 스레드 수 1~8에서 처리량을 비교하고 결과를 그래프로 정리한다. | mac |
| OS-B06 | 조건 변수: bounded buffer 생산자-소비자 | pthread_cond로 bounded buffer를 구현하고, if로 검사하는 버전과 조건 변수 1개만 쓰는 버전이 생산자·소비자 여럿일 때 깨지는 시나리오를 재현(OSTEP 30장)한 뒤 올바른 버전으로 고친다. | mac |
| OS-B07 | 시그널: 핸들러, async-signal-safe, self-pipe | SIGINT/SIGTERM/SIGCHLD 핸들러를 sigaction으로 설치하고, 핸들러에서 printf/malloc 호출이 위험한 이유를 설명한 뒤 volatile sig_atomic_t 플래그와 self-pipe trick으로 메인 루프에 이벤트를 전달하도록 구현한다. | mac |
| OS-B08 | 주소 공간 탐색: 텍스트·데이터·힙·스택과 ASLR | 전역/정적/힙/스택/함수/공유 라이브러리 주소를 출력하는 프로그램을 여러 번 실행해 배치와 무작위화를 관찰하고, vmmap(macOS) 또는 /proc/self/maps(리눅스) 출력과 대조한다. | mac |
| OS-B09 | mmap: 파일 매핑, 익명 매핑, MAP_SHARED vs PRIVATE | 파일을 MAP_SHARED/MAP_PRIVATE로 매핑해 쓰기가 파일에 반영되는지, fork 후 부모·자식 간 공유되는지를 표로 검증하고, mmap 기반 파일 복사와 read/write 복사의 속도를 비교한다. | mac |
| OS-B10 | 주소 변환 시뮬레이터: 단일 레벨 페이지 테이블 | 주소 공간 크기·페이지 크기를 파라미터로 받는 선형 페이지 테이블 시뮬레이터를 작성해 가상→물리 변환·권한 위반·미할당 접근을 판정하고, OSTEP paging-linear-translate 결과와 대조한다. | mac |
| OS-B11 | 페이지 교체 시뮬레이터: FIFO/LRU/Clock/OPT | 참조 문자열과 프레임 수를 입력받는 교체 시뮬레이터를 작성해 정책별 히트율 곡선을 그리고, FIFO에서 프레임을 늘렸는데 폴트가 늘어나는 Belady 사례와 Clock의 LRU 근사 정도를 보인다. | mac |
| OS-B12 | CPU 스케줄링 시뮬레이터: FCFS/SJF/RR/MLFQ | 작업 목록(도착, 실행 길이, I/O 패턴)을 입력받아 FCFS·SJF·STCF·RR·MLFQ(우선순위 부스트 포함)를 시뮬레이션하고 평균 반환/응답 시간을 비교한다(실시간 스케줄링은 see:RTOS). | mac |
| OS-B13 | 파일 API와 원자적 저장: write→fsync→rename | 임시 파일 쓰기→fsync(파일)→rename→디렉터리 fsync 패턴으로 설정 저장 함수를 만들고, 각 단계 직후 프로세스를 kill하는 테스트로 원본 또는 새 버전만 남음을 검증한다(macOS는 F_FULLFSYNC 차이도 정리). | mac |
| OS-B14 | IPC 비교: 파이프·FIFO·Unix 소켓·공유 메모리 | 같은 메시지 교환(핑퐁 지연, 대량 전송)을 pipe, named FIFO, Unix domain socket, shm_open+세마포어로 구현해 왕복 지연과 MB/s를 측정·비교하고 용도별 선택 기준을 정리한다. | mac |
| OS-M01 | 페이지 테이블 관리자: map/unmap/protect와 TLB 무효화 | 4레벨 페이지 테이블에 map_range/unmap_range/protect_range를 구현하고 빈 하위 테이블 회수, unmap·권한 축소 시 필요한 TLB invalidate 호출(단일 VA vs ASID 전체)을 기록하는 mock TLB로 누락 없는지 테스트한다. | mac |
| OS-M02 | 컨텍스트 스위치 비용 측정: 파이프 핑퐁 | 두 프로세스(그리고 두 스레드)가 파이프로 1바이트를 주고받는 핑퐁을 수십만 회 실행해 왕복당 시간을 측정하고(OSTEP 6장 과제), 시스템 콜 비용을 빼서 스위치 비용을 추정하며 코어 고정 불가(macOS) 등 오차 요인을 정리한다. | mac |
| OS-M03 | COW fork와 페이지 폴트 관찰 | 큰 버퍼를 할당한 부모가 fork한 뒤 자식이 일부 페이지만 쓰도록 하고 getrusage의 ru_minflt 변화, 쓰기 페이지 수와의 비례 관계를 측정하며, vfork/posix_spawn을 쓰는 이유를 설명한다. | mac |
| OS-M04 | malloc 구현 1: 암시적/명시적 free list, split·coalesce | sbrk 대신 큰 mmap 영역 위에서 boundary tag 명시적 free list 할당기(malloc/free/realloc)를 구현하고, 트레이스 기반 테스트로 정확성·처리량·공간 활용률(utilization)을 측정한다(CS:APP malloc lab 방식). | mac |
| OS-M05 | malloc 구현 2: segregated free lists와 단편화 측정 | 이전 할당기를 크기 클래스 segregated list로 개선해 같은 트레이스에서 처리량·utilization을 비교하고, 내부 단편화와 외부 단편화를 각각 측정하는 계측 코드를 추가한다. | mac |
| OS-M06 | 스레드별 캐시 할당기: arena + tcache | M05 할당기에 스레드별 소형 객체 캐시(tcache)와 다중 arena를 추가해 8스레드 할당/해제 벤치에서 전역 락 버전 대비 처리량을 측정하고, 다른 스레드가 해제하는 경로(remote free)를 구현·테스트한다(slab/buddy 자체는 see:DS-M03/M04). | mac |
| OS-M07 | 스핀락 구현: TAS, TTAS, ticket lock | C11 atomics로 test-and-set, test-and-test-and-set(+백오프), ticket lock을 구현해 경합 스레드 수별 처리량과 공정성(스레드별 획득 횟수 분산)을 측정하고, M2에서 생성되는 LDAXR/STLXR 또는 LSE(CAS/SWP) 명령을 확인한다. | mac |
| OS-M08 | C11 atomics 동기화 패턴: 메시지 패싱·DCLP | relaxed 플래그로 데이터를 넘기는 코드와 double-checked locking 지연 초기화의 버그를 M2에서 스트레스 테스트로 재현하고, release/acquire와 call_once 방식으로 고친 뒤 TSan으로 검증한다(하드웨어 리트머스 자체는 see:ARCH-A05). | mac |
| OS-M09 | reader-writer lock 구현과 writer starvation | mutex+condvar로 reader-preferring과 writer-preferring rwlock을 구현해 읽기 95%/쓰기 5% 워크로드에서 처리량과 writer 최대 대기 시간을 측정하고, starvation을 재현·완화한다. | mac |
| OS-M10 | 교착 상태: 식사하는 철학자와 4가지 조건 | 식사하는 철학자를 그대로 구현해 교착을 재현한 뒤 (1) 전역 락 순서 (2) trylock+백오프 (3) 동시 착석 제한 세마포어로 각각 해결하고, 각 해법이 깨는 조건과 처리량 차이를 정리한다. | mac |
| OS-M11 | 교착 탐지기: 락 순서 그래프 (mini lockdep) | pthread_mutex 래퍼로 '보유 중인 락 → 새로 잡는 락' 간선을 전역 그래프에 기록하고, 사이클 생성 시 두 경로의 호출 스택과 함께 경고하는 탐지기를 만들어 의도적 ABBA 버그로 검증한다. | mac |
| OS-M12 | I/O 모델 비교: blocking·non-blocking·select·kqueue 에코 서버 | 같은 TCP 에코 서버를 (1) 연결당 스레드 (2) select (3) kqueue 이벤트 루프로 구현하고, 동시 연결 1000개 부하 생성기로 처리량·메모리·지연을 비교한다. | mac |
| OS-M13 | epoll 에코 서버: level vs edge triggered | kqueue 서버를 epoll로 포팅해 LT와 ET(EPOLLET) 모드로 각각 동작시키고, ET에서 한 번만 read해 데이터가 남아 멈추는 버그와 EPOLLONESHOT 멀티스레드 패턴을 실험한다. | **linux** |
| OS-M14 | 파일 시스템 구현: VSFS (inode·비트맵·디렉터리) | 블록 디바이스를 파일로 흉내 낸 디스크 이미지에 superblock·inode/데이터 비트맵·inode 테이블·디렉터리 엔트리를 갖춘 VSFS를 구현해 mkfs/create/write/read/ls/unlink를 지원하고 fsck 비슷한 일관성 검사기를 작성한다. | mac |
| OS-A01 | futex 기반 뮤텍스 구현 (Drepper 방식) | FUTEX_WAIT/WAKE syscall로 Drepper 'Futexes Are Tricky'의 상태 0/1/2 뮤텍스를 구현하고, 순진한 버전의 lost wakeup·불필요한 syscall을 재현한 뒤 pthread_mutex와 경합별 성능을 비교한다. | **linux** |
| OS-A02 | 디버거는 어떻게 동작하나: ptrace로 미니 디버거 | ptrace(PTRACE_TRACEME/PEEKTEXT/POKETEXT/SINGLESTEP)로 자식 프로세스를 실행·정지시키고, 지정 주소에 BRK/int3 명령을 심어 브레이크포인트·레지스터 출력·continue를 지원하는 미니 디버거를 작성해 테스트 프로그램에서 동작을 확인한다(lock-free ABA는 see:DS-A04). | **linux** |
| OS-A03 | RCU 개념 구현: epoch 기반 메모리 회수 | 읽기 다수 설정 테이블을 포인터 교체로 갱신하고, 리더의 epoch 진입/이탈 기록으로 grace period를 판단해 구 버전을 해제하는 EBR을 구현한 뒤 rwlock 대비 읽기 처리량과 ASan으로 use-after-free 부재를 검증한다. | mac |
| OS-A04 | CFS 개념 구현: vruntime과 nice 가중치 | nice→weight 테이블, vruntime 증가율, min-vruntime 선택(레드블랙트리 또는 min-heap), sched_latency/min_granularity를 갖춘 시뮬레이터를 작성해 nice 값별 CPU 점유 비율과 깨어난 태스크의 vruntime 보정 효과를 검증한다(EEVDF로의 변화는 개념 정리). | mac |
| OS-A05 | work-stealing 스레드 풀: Chase-Lev deque | 워커별 Chase-Lev deque(소유자 push/pop, 도둑 steal CAS)와 스레드 풀을 구현해 재귀 병렬 합·fib 워크로드에서 전역 큐 대비 확장성을 측정하고, TSan과 스트레스 테스트로 오더링 버그가 없음을 검증한다. | mac |
| OS-A06 | 사용자 공간 페이지 폴트 처리: mprotect + SIGSEGV | PROT_NONE으로 예약한 영역 접근 시 SIGSEGV(macOS는 SIGBUS 포함) 핸들러에서 해당 페이지를 mprotect로 열어 지연 할당하고, 쓰기 감지로 dirty 페이지만 체크포인트하는 증분 스냅샷 기능을 구현한다. | mac |
| OS-A07 | 크래시 일관성: write-ahead 저널링 + 크래시 주입 | VSFS에 트랜잭션 begin/log/commit/checkpoint 저널을 추가하고, 블록 쓰기 N번째마다 크래시를 주입해 재시작 시 replay 후 fsck가 항상 통과함을 전수 테스트로 검증한다(데이터 vs 메타데이터 저널링 비교 포함). | mac |
| OS-A08 | 로그 구조 FS와 FTL: 세그먼트 클리닝 시뮬레이터 | 세그먼트 기반 로그 구조 저장소 시뮬레이터에 greedy와 cost-benefit 클리닝 정책을 구현해 균일/hot-cold 워크로드에서 쓰기 증폭과 클리닝 비용을 측정하고, SSD FTL 경험과 연결해 정리한다(see:STOR). | mac |
| OS-A09 | xv6 커널 해킹 1: 시스템 콜 추가 (trace, sysinfo) | xv6-riscv에 trace(mask) 시스템 콜(호출 추적 출력, fork 상속)과 sysinfo(빈 메모리·프로세스 수)를 추가하고 user/ 테스트 프로그램과 make grade 스타일 테스트로 검증한다. | **qemu** |
| OS-A10 | xv6 커널 해킹 2: COW fork 구현 | xv6의 uvmcopy를 COW로 바꾸고 RSW 비트로 COW 페이지를 표시, 저장 페이지 폴트(scause 15) 핸들러에서 복사, 물리 페이지 참조 카운트를 구현해 cowtest와 usertests를 통과시킨다. | **qemu** |
| OS-A11 | 종합: 멀티스레드 서비스 데몬 | kqueue 이벤트 루프+워커 스레드 풀(bounded queue, condvar)+자체 슬랩 할당기+SIGTERM 그레이스풀 셧다운+원자적 설정 리로드(SIGHUP)를 갖춘 키-값 서버를 작성하고, TSan/ASan·부하 테스트·크래시 후 재시작 시나리오로 검증한다. | mac |

### RUST — 임베디드 Rust (Embedded Rust)

임베디드 Rust는 C 펌웨어에서 코드 리뷰·정적 분석으로 막던 버그(aliasing, UAF, ISR 데이터 레이스, DMA 버퍼 수명)를 타입 시스템과 소유권으로 컴파일 타임에 막는다. 이 트랙은 일반 Rust 튜토리얼이 아니라 'C/C++로 이미 아는 펌웨어 패턴(링 버퍼, 레지스터 접근, ISR 공유 상태, DMA 버퍼, 에러 처리) → Rust 방식'으로 1:1 번역하며 no_std, embedded-hal, PAC, typestate, RTIC/Embassy, C FFI까지 다룬다. 방산·로보틱스·자동차(Anduril, Rivian, Oxide 등) 펌웨어 팀의 Rust 채택이 늘어 면접에서 'C 대비 무엇이 안전해지고 무엇이 unsafe로 남는가'를 설명할 수 있어야 한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| RUST-B01 | 툴체인 셋업: rustup·cargo·thumbv7em 크로스 빌드 | rustup으로 stable + thumbv7em-none-eabihf 타깃, cargo-binutils(llvm-tools)를 설치하고 #![no_std] 라이브러리 하나(CRC-16 함수)를 호스트와 타깃으로 각각 빌드해 `cargo size`/`cargo objdump`로 .text 크기와 Thumb-2 명령을 확인, 같은 C 함수의 arm-none-eabi 빌드 결과와 비교한다. | mac |
| RUST-B02 | 소유권·이동·빌림: C 포인터 버그를 컴파일 에러로 | C로 작성한 버퍼 API의 고전 버그 3개(free 후 사용, 두 번 해제, 읽는 중인 버퍼를 다른 포인터로 덮어쓰기)를 Rust로 그대로 옮겨 각각의 컴파일 에러(E0382, E0499, E0502)를 기록하고, 올바른 소유권 설계(move, &mut 독점 대여, 스코프 종료 시 Drop)로 고친 뒤 cargo test로 검증한다. | mac |
| RUST-B03 | 에러 처리: errno·out 포인터 → Result·Option·? | C 센서 읽기 함수(int read_temp(int16_t *out) → -EIO/-ETIMEDOUT)를 fn read_temp() -> Result<i16, SensorError>로 이식하고 ? 연산자·map_err·From 변환으로 3단 호출 체인을 구성, `#![deny(clippy::unwrap_used, clippy::panic)]`으로 panic 경로가 없음을 검사한다 (C++ 대응은 see:CPP-M11). | mac |
| RUST-B04 | const generics 링 버퍼: 힙 없는 RingBuffer<T, N> | DS-B04의 2의 거듭제곱 마스크 링 버퍼를 struct RingBuffer<T: Copy, const N: usize>로 이식하고 N이 2의 거듭제곱이 아니면 컴파일 에러가 나게 const assert를 넣은 뒤, push/pop/len/is_full을 경계값 테스트로 검증하고 heapless::spsc::Queue와 API를 비교한다 (기초는 see:DS-B04). | mac |
| RUST-B05 | #![no_std]: core·alloc·std 경계와 heapless | COBS 인코더/디코더를 #![no_std] 라이브러리로 작성하고(#![cfg_attr(not(test), no_std)]) heapless::Vec<u8, 64>로 출력 버퍼를 다루며, 호스트 cargo test와 thumbv7em 빌드가 모두 통과하는지, 타깃 바이너리에 std/alloc 심볼이 없는지 nm으로 확인한다. | mac |
| RUST-B06 | enum + match 상태 기계: 바이트 단위 프레임 파서 | SOF | LEN | PAYLOAD | CRC8 형식 프레임을 1바이트씩 받는 파서를 enum State { Idle, Len, Payload { len, idx }, Crc { .. } }로 구현하고, 상태 추가 시 match 누락이 컴파일 에러가 됨을 확인한 뒤 잘림·CRC 오류·재동기 시나리오를 테이블 기반 테스트로 검증한다 (프레이밍 기초는 see:COMM-B11). | mac |
| RUST-B07 | 비트 연산과 const fn 레지스터 필드 헬퍼 | const fn field_mask(pos, width) -> u32와 fn set_field(reg, pos, width, val)를 구현하고 범위 밖 val을 debug_assert로 잡으며, newtype Baud(u32)/Prescaler(u8)로 인자 순서 실수를 컴파일 에러로 만든 뒤, 생성된 Thumb 어셈블리가 C 매크로 버전과 같은 BFI/UBFX 수준인지 cargo objdump로 비교한다 (C 관례는 see:BM-B01). | mac |
| RUST-B08 | 바이트 직렬화: repr(C)·엔디언·struct 캐스트 없이 파싱 | 16바이트 센서 패킷 헤더를 u16::from_le_bytes/from_be_bytes와 try_into로 필드별 파싱하는 버전과 #[repr(C, packed)] + zerocopy::FromBytes 버전을 각각 구현해 정렬·패딩·엔디언 테스트를 통과시키고, packed 필드에 &참조를 만들면 왜 에러(E0793)인지 정리한다. | mac |
| RUST-B09 | trait와 제네릭: ops 함수포인터 구조체 → trait | PLAT-B07의 struct uart_ops { init, write, read }를 trait Uart로 정의하고 Mock/Loopback 두 구현을 만든 뒤, fn log<U: Uart>(u: &mut U)(단형화)와 fn log(u: &mut dyn Uart)(vtable) 버전의 코드 크기·어셈블리를 비교한다 (C++ CRTP 대응은 see:CPP-M05). | mac |
| RUST-B10 | 라이프타임: DMA 수신 버퍼를 빌리는 zero-copy 뷰 | 수신 버퍼에서 페이로드를 복사 없이 가리키는 struct Frame<'a> { hdr: u8, payload: &'a [u8] }와 fn parse(buf: &'a [u8]) -> Result<Frame<'a>, _>를 구현하고, 버퍼를 다음 수신에 재사용(&mut)하는 동안 이전 Frame을 쓰는 코드가 컴파일 에러가 됨을 보인 뒤 C 버전의 같은 버그를 ASan으로 재현해 비교한다. | mac |
| RUST-M01 | MMIO 레지스터 접근: read_volatile과 RegisterBlock | #[repr(C)] struct UartRegs { dr, sr, cr1, brr }를 VolatileCell 스타일 래퍼(UnsafeCell + read_volatile/write_volatile)로 구현하고 호스트에서는 정적 배열 주소로 매핑해 read/write/modify를 테스트하며, MMIO에 &mut 참조를 직접 만들면 왜 위험한지(dereferenceable·spurious read)와 core::ptr::addr_of!/raw pointer 접근을 정리한다 (C 대응은 see:C-M01). | mac |
| RUST-M02 | svd2rust PAC: SVD → 레지스터 API와 modify 클로저 | STM32F4(또는 직접 작성한 작은 SVD) 파일로 svd2rust + form을 돌려 PAC를 생성하고 `rcc.ahb1enr().modify(|_, w| w.gpioaen().set_bit())`와 GPIO MODER 설정 코드를 작성, thumbv7em 빌드의 어셈블리가 C의 RMW(LDR/ORR/STR)와 같은지 비교하고 unsafe { w.bits(..) }가 필요한 경우를 정리한다 (CMSIS 헤더는 see:BM-B01). | mac |
| RUST-M03 | 싱글톤 주변장치: Peripherals::take()와 split() | static AtomicBool로 두 번째 take()가 None을 반환하는 Peripherals 싱글톤과 GPIO 포트를 개별 핀 소유 값으로 쪼개는 split()을 구현하고, 같은 핀을 두 드라이버가 동시에 쓰려는 코드가 컴파일 에러(move 이후 사용)가 됨을 테스트, C의 전역 핸들 방식과 비교한다. | mac |
| RUST-M04 | Typestate GPIO: 설정 순서와 모드를 타입으로 강제 | Pin<MODE>와 into_push_pull_output(self) -> Pin<Output<PushPull>>, into_floating_input(self) 변환을 구현해 Output에만 set_high()가 존재하게 하고, size_of::<Pin<_>>() == 0과 어셈블리 동일성을 확인한 뒤 오용 케이스를 trybuild compile-fail 테스트로 고정한다 (C++ 템플릿 버전은 see:CPP-A01). | mac |
| RUST-M05 | embedded-hal 1.0 드라이버: I2C 센서 + SPI 버스 공유 | I2c 트레이트에 제네릭한 온도 센서 드라이버(TMP102 류: WHO_AM_I 확인, 설정 쓰기, 12비트 변환)를 작성해 embedded-hal-mock의 트랜잭션 기대값으로 테스트하고, SpiBus vs SpiDevice 차이와 embedded-hal-bus의 RefCellDevice로 두 장치가 한 SPI 버스와 각자 CS를 공유하는 구성을 구현한다 (CS RAII는 see:CPP-B07). | mac |
| RUST-M06 | ISR 공유 상태: static mut 대신 Mutex<RefCell<..>> | ISR이 갱신하고 main이 읽는 카운터·상태를 (1) AtomicU32/AtomicBool, (2) critical_section::Mutex<RefCell<Option<Driver>>> 두 방식으로 구현하고, Rust 2024에서 static mut 참조가 거부되는 이유와 Cortex-M0(atomic CAS 없음)에서 portable-atomic이 필요한 이유를 정리하며, 호스트에서는 critical-section std 구현 + 스레드를 ISR로 삼아 경쟁 테스트한다 (C++ 대응은 see:CPP-A04). | mac |
| RUST-M07 | Lock-free SPSC 큐: ISR 생산자·main 소비자 split | AtomicUsize head/tail과 UnsafeCell 버퍼로 Queue<T, N>을 구현하고 split(&mut self) -> (Producer<'_>, Consumer<'_>)로 역할을 분리한 뒤, loom으로 메모리 오더링을 Relaxed로 약화하면 실패하는 인터리빙을 찾아내고 heapless::spsc와 비교한다 (C 링 버퍼 ISR 버전은 see:DS-A02). | mac |
| RUST-M08 | unsafe 경계 설계: SAFETY 주석·불변식·Miri | 정적 메모리 블록 풀(MaybeUninit<[T; N]> + 비트맵)을 구현해 alloc이 &'static mut T 대신 소유 핸들 Box-like PoolBox<T>를 반환하게 하고, 모든 unsafe에 SAFETY 불변식을 쓰며, 일부러 넣은 aliasing/미초기화 읽기 버그를 cargo miri test가 잡는지 확인한다. | mac |
| RUST-M09 | FFI 1: Rust에서 C 벤더 HAL 호출 (bindgen·cc) | C로 작성된 작은 SPI flash 드라이버(.h/.c)를 build.rs의 cc 크레이트로 컴파일하고 bindgen으로 바인딩을 생성한 뒤, 포인터+길이 인자를 &[u8]로, int 에러 코드를 Result로 바꾸는 safe 래퍼를 작성해 호스트 테스트로 검증한다. | mac |
| RUST-M10 | FFI 2: C 펌웨어에 Rust 모듈 넣기 (staticlib·cbindgen) | SEC-A07의 no_std TLV 파서를 crate-type = ["staticlib"] + #[no_mangle] extern "C"로 노출하고 cbindgen으로 헤더를 생성해 C main에서 호출하며, panic=abort와 catch 불가 경계, C가 넘긴 포인터의 NULL/길이 검증, 반환 구조체의 #[repr(C)] 레이아웃을 테스트로 확인한다 (파서 원본은 see:SEC-A07). | mac |
| RUST-M11 | bare-metal 부팅: cortex-m-rt·memory.x·#[entry] | cortex-m-rt + memory.x + panic-semihosting으로 hello 펌웨어를 만들어 QEMU lm3s6965evb에서 semihosting 출력하고, objdump로 벡터 테이블·Reset 핸들러·.data 복사 루프를 찾아 BM-B08의 C startup과 대응시키며 panic_handler의 역할을 정리한다 (C 버전은 see:BM-B08). | **qemu** |
| RUST-M12 | #[exception]/#[interrupt]: SysTick ISR와 HardFault | QEMU에서 SysTick을 1kHz로 설정해 #[exception] SysTick이 AtomicU32 tick을 올리게 하고 main에서 wrapping_sub로 랩어라운드 안전 delay를 구현하고(debug 빌드의 오버플로 panic vs release 랩 차이 확인), 잘못된 주소 접근으로 HardFault를 유발해 #[exception] HardFault(ef: &ExceptionFrame)에서 PC/LR을 출력한다 (C 버전 크래시 덤프는 see:BM-A09). | **qemu** |
| RUST-M13 | defmt 로깅: 지연 포맷팅과 바이너리 크기 | 같은 펌웨어를 core::fmt(write!) 로깅과 defmt 로깅으로 각각 빌드해 .text 크기와 로그 1줄당 전송 바이트를 비교하고, defmt-semihosting + defmt-print로 QEMU 출력 디코딩, ELF의 .defmt 섹션을 확인한다. | **qemu** |
| RUST-A01 | DMA 버퍼 안전성: 'static 소유권 이전 Transfer API | mock DMA 엔진 위에 fn start(buf: &'static mut [u8; N]) -> Transfer와 Transfer::wait(self) -> (&'static mut [u8; N], Channel)을 구현하고, &mut 대여 기반 API가 mem::forget으로 왜 unsound해지는지 예제로 보인 뒤, 시작/완료 시 compiler_fence(Release/Acquire)와 캐시 clean/invalidate 위치를 정당화한다 (캐시 일관성은 see:ARCH-M12, 디스크립터 링은 see:COMM-A08). | mac |
| RUST-A02 | RTIC v2: 우선순위 천장(SRP)과 컴파일타임 락 | RTIC v2로 UART RX 하드웨어 태스크(높은 우선순위)와 처리 소프트웨어 태스크(낮은 우선순위)가 공유 리소스를 lock으로 나누는 앱을 QEMU에서 실행하고, 매크로가 계산한 ceiling과 BASEPRI 조작 코드를 cargo expand/objdump로 확인해 RTOS 뮤텍스 + 우선순위 상속과 비교한다 (see:RTOS). | **qemu** |
| RUST-A03 | async 내부: Future·Waker·정적 executor 직접 구현 | 힙 없이 고정 개수 태스크를 담는 미니 executor(run-queue + RawWaker vtable)와 'ISR이 wake하는' mock UART read Future를 구현해 두 태스크가 협조적으로 진행됨을 테스트하고, async fn이 컴파일된 Future의 size_of로 상태 기계 크기를 측정한다 (C++ 코루틴 대응은 see:MCPP-A02). | mac |
| RUST-A04 | Embassy: async 태스크·Channel·타이머·embedded-hal-async | embassy-executor(arch-std) + embassy-time(std 드라이버) + embassy-sync로 센서 샘플링·필터·텔레메트리 3개 태스크를 Channel/Signal로 잇고 select로 타임아웃을 구현해 호스트에서 실행하며, embedded-hal-async I2c에 제네릭한 드라이버로 바꾼 뒤 같은 코드가 thumbv7em(embassy-stm32 또는 nrf)으로 빌드되는지 확인한다. | mac |
| RUST-A05 | 코드 크기·panic 제거: opt-level z·LTO·cargo-bloat | RUST-M05 드라이버 기반 펌웨어를 opt-level="z"/"s", lto="fat", codegen-units=1, panic="abort" 조합으로 빌드해 .text를 표로 비교하고, cargo-bloat로 상위 심볼(core::fmt, panic 경로, 제네릭 복제)을 찾아 인덱싱 → get(), unwrap → 명시 처리로 panic 경로를 제거한 뒤 panic_handler 심볼 참조 유무로 검증한다. | mac |
| RUST-A06 | 테스트 전략: 호스트 + proptest + 온타깃 테스트 CI | RUST-B04 링 버퍼와 RUST-B06 파서에 proptest(참조 모델 VecDeque 비교, 임의 바이트열 무패닉)를 붙이고, defmt-test 또는 embedded-test로 QEMU에서 도는 온타깃 테스트를 구성한 뒤, GitHub Actions 스타일 매트릭스(cargo test, clippy -D warnings, thumbv7em build, miri)를 설계한다 (테스트 일반론은 see:TEST-M03). | mac |
| RUST-A07 | 종합: C 드라이버를 Rust로 점진 이식 (strangler) | C로 된 인터럽트 구동 UART + 링 버퍼 + 명령 파서(BM-M04 계열)에서 파서→링 버퍼→드라이버 순으로 Rust로 교체하며 각 단계마다 FFI shim과 기존 C 테스트를 유지하고, 크기·성능·발견된 버그를 비교한 뒤 'unsafe가 남는 곳, 인증(Ferrocene/ISO 26262) 이슈, 팀 도입 비용'을 면접용 1페이지로 정리한다 (see:BM-M04). | mac |

## 3단계 · 플랫폼 · 하드웨어 인터페이스 (Platform & HW Interfaces)

### ELX — 임베디드 리눅스 (Embedded Linux)

임베디드 리눅스는 SoC 기반 제품(카메라, 로봇, 차량, 서버 BMC)의 표준 플랫폼으로, 부트로더·디바이스 트리·커널 드라이버·rootfs를 다룰 줄 아는지가 면접의 핵심 질문이다. 이 트랙은 부트 플로우에서 시작해 문자/플랫폼 드라이버, 인터럽트·DMA, 빌드 시스템, 커널 디버깅과 PREEMPT_RT까지 직접 코드를 짜며 익힌다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| ELX-B01 | 부트 플로우 지도: ROM→SPL→U-Boot→Kernel→init | 제공된(또는 QEMU에서 캡처한) UART 부트 로그를 단계별(BootROM/SPL/U-Boot/kernel/init)로 분할하고 각 단계 소요시간·로드 주소·bootargs를 추출하는 Python 파서를 작성해 타임라인 표로 출력한다. | mac |
| ELX-B02 | 크로스 컴파일 기초: aarch64 툴체인과 sysroot | 공유 라이브러리를 링크하는 프로그램을 aarch64-linux-gnu-gcc로 크로스 빌드하고 file/readelf -h -l -d로 ABI·PT_INTERP·NEEDED를 확인한 뒤 qemu-aarch64 user-mode와 -L sysroot로 실행해 검증한다. | **linux** |
| ELX-B03 | 커널 빌드와 Kconfig: defconfig, 모듈 vs built-in | arm64 defconfig에서 시작해 menuconfig로 옵션 3개(예: CONFIG_GPIO_SIM=m, DEBUG_INFO, IKCONFIG_PROC)를 바꾸고 빌드한 뒤 .config diff, /proc/config.gz, modinfo로 반영 여부를 검증한다. | **linux** |
| ELX-B04 | QEMU virt에서 커널+initramfs 부팅 | BusyBox 정적 빌드로 cpio initramfs를 만들고 qemu-system-aarch64 -M virt로 부팅해, /init 스크립트에서 proc/sys/devtmpfs를 마운트하고 셸을 띄우는 것까지 확인한다. | **qemu** |
| ELX-B05 | Device Tree 읽기·쓰기: DTS, DTB, dtc | 가상 보드 DTS(cpus, memory, uart, i2c 센서 노드, interrupt-parent, #address-cells)를 작성해 dtc로 컴파일·역컴파일하고, FDT 헤더와 structure block을 순회해 노드 트리를 출력하는 C 파서를 작성해 dtc -I dtb -O dts 결과와 대조한다. | mac |
| ELX-B06 | 커널 모듈 Hello: init/exit, module_param, Kbuild | module_param(count, int)을 받아 dmesg에 출력하는 모듈과 EXPORT_SYMBOL_GPL 함수를 쓰는 두 번째 모듈을 Kbuild로 빌드하고 insmod/modprobe 의존성, /sys/module/<name>/parameters 노출을 검증한다. | **linux** |
| ELX-B07 | 문자 디바이스 드라이버: file_operations read/write | alloc_chrdev_region+cdev_add로 /dev/kecho를 만들고 4KB 커널 버퍼에 write한 내용을 read로 돌려주는 드라이버를 작성, 부분 read/쓰기 초과/동시 open을 사용자 테스트 프로그램으로 검증한다. | **linux** |
| ELX-B08 | ioctl 설계: _IO/_IOR/_IOW 번호와 구조체 ABI | /dev/kecho에 GET_STATS/SET_MODE/RESET ioctl을 추가하고(_IOR/_IOW 매크로, 고정폭 필드·패딩 명시), 잘못된 cmd/크기/사용자 포인터에 -ENOTTY/-EFAULT를 반환하는지 테스트 프로그램으로 검증한다. | **linux** |
| ELX-B09 | sysfs 속성과 kobject: DEVICE_ATTR | 드라이버에 threshold(rw), count(ro) 속성을 attribute_group으로 노출하고 범위 검증·kstrtoint 에러 처리를 구현, cat/echo 쉘 스크립트로 정상/오류 입력을 검증한다. | **linux** |
| ELX-B10 | 사용자공간 GPIO: libgpiod와 gpio-sim | gpio-sim(configfs)으로 가상 gpiochip을 만들고 libgpiod로 출력 토글·입력 엣지 이벤트 대기 프로그램을 작성, sim 쪽 pull 값을 바꿔 이벤트 타임스탬프가 들어오는지 확인한다. | **linux** |
| ELX-B11 | 사용자공간 I2C/SPI: i2c-dev와 spidev | i2c-stub 모듈로 가상 칩(주소 0x48)을 만들고 i2c-dev의 I2C_RDWR ioctl로 레지스터 write-then-read 유틸을 작성해 i2cget/i2cdump 결과와 대조한다; spidev ioctl(SPI_IOC_MESSAGE) 구조체 사용법도 코드로 정리한다. | **linux** |
| ELX-B12 | rootfs 구성: FHS, BusyBox, 동적 라이브러리 | BusyBox 동적 빌드 + ldd로 필요한 glibc 라이브러리를 복사해 최소 rootfs를 손으로 구성하고 ext4 이미지로 만들어 QEMU에서 root=/dev/vda로 부팅, /etc/inittab과 fstab 동작을 확인한다. | **qemu** |
| ELX-B13 | systemd 유닛 작성: 의존성, 재시작, watchdog | 가짜 센서 데몬을 Type=notify 서비스로 등록(After/Wants, Restart=on-failure, WatchdogSec=5)하고 데몬이 hang되면 systemd가 재시작하는지 journalctl과 systemctl show로 검증한다. | **linux** |
| ELX-M01 | Platform driver와 DT 바인딩: probe, of_match | QEMU virt DTB에 'don,fake-sensor' 노드(reg, interrupts, 커스텀 프로퍼티)를 추가하고 of_match_table 기반 platform_driver를 작성해 devm_ioremap_resource, platform_get_irq, of_property_read_u32로 리소스를 얻는지 dmesg로 확인한다. | **qemu** |
| ELX-M02 | Blocking I/O: wait queue와 poll/select 지원 | kecho 드라이버에 wait_event_interruptible 기반 blocking read, O_NONBLOCK 시 -EAGAIN, .poll 콜백을 추가하고 사용자 프로그램에서 poll()로 여러 fd를 기다리는 시나리오를 검증한다. | **linux** |
| ELX-M03 | mmap 드라이버: 커널 버퍼를 사용자공간에 매핑 | vmalloc/alloc_pages 버퍼를 .mmap으로 노출(remap_pfn_range 버전과 vm_ops->fault 버전 둘 다)하고 사용자공간 쓰기가 커널 쪽 sysfs 덤프로 보이는지 확인, 크기 초과·오프셋 오류 처리도 테스트한다. | **linux** |
| ELX-M04 | 드라이버 동기화: spinlock vs mutex vs atomic | 의도적으로 락 없는 카운터/리스트를 가진 드라이버에 다중 스레드 스트레스 테스트로 race를 재현한 뒤 mutex/spin_lock_irqsave/atomic_t로 각각 수정하고 CONFIG_PROVE_LOCKING(lockdep) 커널에서 경고가 없는지 확인한다. | **linux** |
| ELX-M05 | 인터럽트 처리: hard IRQ와 threaded IRQ | gpio-sim 라인을 gpiod_to_irq로 IRQ 소스로 사용해 request_threaded_irq 핸들러(hard: 타임스탬프 기록, thread: 처리)를 작성하고 /proc/interrupts 카운트와 지연 통계를 sysfs로 노출해 검증한다. | **linux** |
| ELX-M06 | Bottom half 비교: tasklet vs workqueue vs threaded IRQ | 같은 작업을 tasklet, workqueue(시스템/전용 WQ_HIGHPRI), threaded IRQ로 구현하고 ftrace(irq/workqueue 이벤트)로 IRQ→처리 지연 분포를 측정해 표로 비교한다. | **linux** |
| ELX-M07 | 커널 타이머와 hrtimer: 주기 샘플링 드라이버 | 1kHz hrtimer 콜백에서 가짜 센서를 샘플링해 kfifo에 저장하는 드라이버를 작성하고, 콜백 간격 지터 히스토그램을 sysfs로 노출해 timer_list(HZ 기반) 버전과 비교한다. | **linux** |
| ELX-M08 | I2C 클라이언트 드라이버: i2c_driver + regmap + hwmon | i2c-stub 위 가상 온도센서(TMP102 레지스터 맵 흉내)용 i2c_driver를 regmap(캐시, volatile 레지스터 지정)으로 작성하고 hwmon에 등록해 sensors/cat temp1_input으로 값이 읽히는지 검증한다. | **linux** |
| ELX-M09 | IIO 드라이버: 가상 ADC 채널, buffer, trigger | 가짜 4채널 ADC IIO 드라이버(iio_chan_spec, read_raw, scale)를 작성하고 iio-trig-hrtimer 소프트 트리거로 버퍼 캡처를 켜서 /dev/iio:deviceX에서 샘플 스트림을 읽는 사용자 프로그램으로 검증한다. | **linux** |
| ELX-M10 | DMA API: coherent vs streaming mapping | QEMU 'edu' PCI 장치의 DMA 엔진을 대상으로 coherent 버퍼와 streaming 매핑(dma_map_single + dma_sync_*) 두 방식으로 RAM↔장치 전송 드라이버를 작성하고 데이터 무결성과 매핑 해제 누락(CONFIG_DMA_API_DEBUG) 경고를 검증한다. | **qemu** |
| ELX-M11 | uevent와 udev 규칙: 핫플러그 이벤트 처리 | 드라이버에서 kobject_uevent_env로 커스텀 이벤트(SENSOR_ALARM=1)를 보내고, udev 규칙으로 /dev/sensor0 심볼릭 링크·권한·스크립트 실행을 설정해 udevadm monitor/test로 검증한다. | **linux** |
| ELX-M12 | U-Boot 다루기: env, bootcmd, FIT 이미지 | qemu_arm64_defconfig로 U-Boot를 빌드해 QEMU virt에서 실행하고, mkimage로 kernel+dtb+ramdisk FIT(.its)를 만들어 virtio/tftp에서 로드→bootm으로 부팅, bootargs·bootcmd를 saveenv로 바꿔 검증한다. | **qemu** |
| ELX-M13 | Buildroot: defconfig 분석과 커스텀 패키지 | qemu_aarch64_virt_defconfig를 읽어 툴체인/커널/rootfs 설정을 표로 정리하고, 센서 데몬용 패키지(Config.in + sensord.mk, generic-package)와 rootfs overlay·post-build 스크립트를 작성한다; 단일 패키지 빌드(make sensord)까지만 실행한다. | **linux** |
| ELX-M14 | Yocto 개념: layer, recipe, bbappend, bitbake | meta-don 레이어를 만들고 센서 데몬 recipe(SRC_URI, do_install, systemd 클래스)와 기존 레시피용 bbappend를 작성한 뒤 bitbake -e 출력에서 변수 최종값이 어떻게 결정되는지 해석한다. | **linux** |
| ELX-M15 | 커널 디버깅 1: printk, dynamic_debug, oops 디코드 | NULL 역참조·잘못된 list_del을 일으키는 모듈로 oops를 발생시키고 scripts/decode_stacktrace.sh, addr2line, gdb 'list *(func+0x..)'로 원인 라인을 찾는다; pr_debug를 dynamic_debug로 런타임에 켜고 끄는 것도 확인한다. | **linux** |
| ELX-A01 | ftrace·trace-cmd와 커스텀 tracepoint | 드라이버에 TRACE_EVENT(sensor_sample, sensor_irq)를 정의하고 trace-cmd record로 IRQ→threaded handler→사용자 read까지 지연을 측정, function_graph로 핫 경로를 찾아 개선 전후를 비교한다. | **linux** |
| ELX-A02 | KGDB/QEMU gdbstub로 커널 소스 레벨 디버깅 | qemu -s -S로 커널을 띄우고 vmlinux + scripts/gdb(lx-symbols, lx-dmesg)로 모듈 심볼을 로드, platform driver probe에 breakpoint를 걸어 리소스 구조체를 조사한다. | **qemu** |
| ELX-A03 | KASAN·kmemleak으로 커널 메모리 버그 잡기 | use-after-free, slab OOB, 누수를 의도적으로 넣은 모듈을 KASAN/kmemleak 커널에서 실행해 리포트(할당/해제 스택)를 해석하고 수정 후 리포트가 사라지는지 확인한다. | **linux** |
| ELX-A04 | PREEMPT_RT와 실시간 지연 측정: cyclictest | 표준 커널과 PREEMPT_RT 커널에서 stress-ng 부하 하에 cyclictest(-m -p90 -h) 히스토그램을 수집해 최대 지연을 비교하고, IRQ 스레드 우선순위(chrt)·CPU 격리(isolcpus)로 추가 개선한다. | **linux** |
| ELX-A05 | Device Tree overlay와 바인딩 스키마 검증 | base DTB에 센서/LED overlay(.dtso)를 fdtoverlay로 병합해 결과를 검증하고, 커스텀 compatible용 YAML 바인딩 스키마를 작성해 dt-validate로 올바른/잘못된 노드를 판별한다. | mac |
| ELX-A06 | UIO/VFIO 사용자공간 드라이버 | QEMU edu 장치를 uio_pci_generic(또는 vfio-pci)에 바인딩하고 사용자공간에서 BAR mmap, 레지스터 read/write, IRQ 대기(read on /dev/uioX)를 수행하는 드라이버를 작성한다. | **qemu** |
| ELX-A07 | 드라이버 전원 관리: runtime PM과 system suspend | platform driver에 runtime_suspend/resume(가짜 클럭 gate)과 autosuspend(500ms)를 구현하고, /sys/devices/.../power/runtime_status 변화와 echo freeze > /sys/power/state 시 콜백 순서를 검증한다. | **linux** |
| ELX-A08 | 부트 시간 최적화: initcall_debug, systemd-analyze | QEMU 이미지에서 initcall_debug·printk.time·systemd-analyze blame/critical-chain으로 부트 프로파일을 만들고, 커널 옵션 축소·모듈화·서비스 병렬화 등 3가지 개선을 적용해 전후 시간을 비교한다. | **qemu** |
| ELX-A09 | 커널 패치 워크플로: checkpatch, sparse, format-patch | 직접 만든 드라이버를 in-tree(drivers/misc) 패치 시리즈 3개로 나눠 git format-patch로 만들고 checkpatch.pl --strict, make C=1(sparse), W=1 경고를 모두 해결한다. | **linux** |
| ELX-A10 | 종합: 가상 센서 보드 BSP 엔드투엔드 | QEMU virt에 커스텀 DT 노드 → platform/I2C 드라이버 → IIO/sysfs 인터페이스 → systemd 데몬 → Buildroot 패키지/overlay까지 연결한 BSP를 구성하고, 부팅 후 센서 값이 데몬 로그에 나오는 것을 스모크 테스트로 검증한다. | **qemu** |

### PLAT — 플랫폼 소프트웨어 (Platform Software)

플랫폼 소프트웨어는 SoC가 켜지는 순간부터 OS가 안정적으로 돌기까지의 '바닥층'(부트 체인, TF-A/PSCI, UEFI/ACPI, 전원·클럭, 코어 간 IPC, 업데이트·텔레메트리)을 책임진다. 실리콘·AI 가속기·로봇 SoC 회사의 플랫폼/펌웨어 팀 면접에서 자주 묻는 설계 감각을 직접 구현하며 익힌다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| PLAT-B01 | SoC 부트 체인 지도: BootROM→BL1/BL2→BL31→BL33 | 가상 SoC 부트 체인 시뮬레이터(C)를 작성: 각 스테이지가 다음 이미지 헤더(magic, load_addr, size, entry, CRC32)를 검사해 '로드 후 점프'하고 hand-off 구조체(메모리 맵, 부트 소스)를 전달하며, 손상 이미지 주입 시 백업 슬롯으로 fallback하는지 테스트한다. | mac |
| PLAT-B02 | 부트 이미지 레이아웃: 파티션·헤더·정렬 | bootloader+kernel+dtb를 정렬된 오프셋으로 패킹하고 목차 헤더(TOC: 이름, 오프셋, 크기, 해시)를 생성하는 Python 이미지 패커와 C 언패커를 작성해 왕복(round-trip) 검증 및 오정렬/겹침 검출 테스트를 한다. | mac |
| PLAT-B03 | ARMv8 Exception Level·보안 상태와 SMC/ERET | EL/보안 상태 전이를 C 상태머신으로 모델링하고 SMC 호출 시 EL3 디스패처가 NS 컨텍스트(x0–x7, ELR, SPSR)를 저장→서비스→복원하는 흐름을 구현, 잘못된 전이(EL0에서 SMC 등)가 거부되는지 테스트한다. | mac |
| PLAT-B04 | PSCI 인터페이스: CPU_ON/OFF, SYSTEM_RESET | SMCCC function ID 인코딩(fast/yielding, 32/64, owner)을 디코드하고 PSCI 디스패처를 구현: 코어별 상태(ON/OFF/ON_PENDING), CPU_ON 중복 호출 시 ALREADY_ON, 잘못된 MPIDR 시 INVALID_PARAMS 반환을 단위 테스트로 검증한다. | mac |
| PLAT-B05 | 클럭 트리 모델링: PLL, divider, mux, gate | clk 노드(PLL/divider/mux/gate)로 클럭 트리를 C로 구성하고 recalc_rate 전파, set_rate 시 최적 divider/parent 선택, enable/disable 참조 카운트(부모 자동 enable)를 구현해 clk_summary 형식으로 출력·검증한다. | mac |
| PLAT-B06 | 리셋·핀먹스 컨트롤러 추상화 | pin→function 테이블 기반 pinmux 요청/해제 API(충돌 검사, 소유자 기록)와 reset line assert/deassert API(지연, 의존 순서)를 mock 레지스터 위에 구현하고, UART+SPI 공유 핀 충돌 시나리오로 테스트한다. | mac |
| PLAT-B07 | HAL 설계: ops 구조체와 벤더 포팅 레이어 | UART HAL(init/tx/rx/irq 콜백) 인터페이스를 정의하고 레지스터 맵이 다른 가상 벤더 A/B 백엔드 두 개를 구현, 동일한 테스트 스위트가 두 백엔드 모두 통과하도록 하고 추상화 비용(간접 호출)을 논의한다. | mac |
| PLAT-B08 | 펌웨어 버전·빌드 메타데이터 임베드 | 빌드 시 semver+git hash+dirty 플래그+build ID를 전용 섹션(.fw_info)에 넣는 스크립트/링커 설정을 만들고, 바이너리에서 이를 추출하는 호스트 툴과 버전 비교 함수(pre-release 포함) 단위 테스트를 작성한다. | mac |
| PLAT-B09 | 보드 bring-up 체크리스트: 전원→클럭→메모리→콘솔 | 가상 보드용 bring-up 계획서(전원 레일·리셋·클럭·DDR·UART first-light·JTAG 접속 체크)와 각 단계별 smoke-test 펌웨어 코드 경로(mock HW)를 작성하고, 단계 실패 시 다음 디버깅 액션을 표로 정리한다. | mac |
| PLAT-B10 | 바이너리 로깅: 포맷 ID와 지연 포매팅 | 로그 호출을 (포맷 문자열 ID + 인자) 바이너리 레코드로 링버퍼에 기록하는 인코더와, ELF의 문자열 테이블로 사람이 읽을 로그를 복원하는 호스트 디코더를 작성해 printf 대비 코드 크기·실행 시간을 측정한다. | mac |
| PLAT-B11 | ACPI vs Device Tree: 하드웨어 기술 방식 비교 | 샘플 DSDT를 iasl로 디컴파일해 _HID/_CRS/_STA 의미를 정리하고 같은 장치(UART, I2C 센서)를 DTS로 다시 기술, 열거·전원 제어 방식 차이를 비교표로 작성한다. | mac |
| PLAT-M01 | 전원 상태 머신: Run/Idle/Sleep/Deep-sleep | 상태별 전력·진입/복귀 지연·wakeup source를 가진 전력 관리 시뮬레이터를 작성하고 idle 구간 트레이스에 대해 break-even time 기반 상태 선택 정책을 구현해 에너지·지연을 비교한다. | mac |
| PLAT-M02 | DVFS 거버너: OPP 테이블과 전압/주파수 전환 순서 | OPP 테이블과 부하 트레이스를 입력으로 ondemand/schedutil 유사 거버너를 구현하고 전환 순서 위반을 검출하는 regulator/clk mock을 둬서, 정책별 에너지(∝CV²f)와 마감 미스를 비교한다. | mac |
| PLAT-M03 | Suspend/Resume 경로: 디바이스 순서와 롤백 | 부모-자식 디바이스 트리에 대해 suspend(자식→부모)/resume(부모→자식) 프레임워크를 구현하고, 중간 디바이스 suspend 실패 시 이미 내려간 디바이스를 역순 resume하는 롤백과 wakeup 소스 처리를 테스트한다. | mac |
| PLAT-M04 | Thermal 관리: 센서→trip point→cooling device | 간단한 열 모델(RC) 위에서 trip point(passive/critical), step-wise와 PID 쓰로틀링 거버너를 구현하고 히스테리시스 유무에 따른 진동, 최대 온도, 성능 손실을 비교한다. | mac |
| PLAT-M05 | 멀티코어 메일박스 IPC: 공유 메모리 + doorbell | 두 스레드를 두 코어로 보고 공유 메모리 슬롯 + doorbell(인터럽트 대용 조건변수) 메일박스를 구현, 헤더(type, seq, len), ACK/타임아웃/재전송, 메모리 배리어 위치를 포함한 프로토콜을 스트레스 테스트한다. | mac |
| PLAT-M06 | remoteproc/rpmsg 개념과 virtio vring 구현 | virtio split vring(desc/avail/used, 인덱스 wrap)을 C로 구현해 두 프로세스(공유 메모리) 간 rpmsg 유사 엔드포인트 메시징을 만들고, remoteproc 리소스 테이블(carveout, vdev, trace) 파서도 작성한다. | mac |
| PLAT-M07 | 펌웨어 업데이트: A/B 슬롯과 부트 카운터 | A/B 슬롯 메타데이터(active, trial, bootcount, good)와 부트로더 선택 로직, 앱의 mark-good을 구현하고, 각 단계 임의 지점 전원 차단 주입 테스트로 항상 부팅 가능한 슬롯이 남는지 검증한다. | mac |
| PLAT-M08 | 설정 저장소 운영: 스키마 버전·마이그레이션·공장 초기화 | 버전 태그가 붙은 설정 레코드에 대해 구버전→신버전 마이그레이션 체인, 기본값 채우기, 공장 초기화·손상 시 폴백을 구현하고, 다운그레이드·마이그레이션 중 전원 차단 시나리오를 테스트한다(플래시 KV 저장 엔진은 see:DS-A10). | mac |
| PLAT-M09 | 디바이스 텔레메트리: 카운터, 히스토그램, heartbeat | 펌웨어 메트릭 레지스트리(카운터/게이지/로그 스케일 히스토그램)와 주기적 heartbeat 스냅샷을 TLV로 직렬화하는 코드, 이를 파싱해 CSV로 내보내는 호스트 수집기를 작성한다. | mac |
| PLAT-M10 | 멀티코어 SoC 크래시 수집: 코어별 덤프와 업로드 큐 | 앱 코어·실시간 코어·DSP 각각의 크래시 레코드를 공유 예약 메모리에 기록하는 프로토콜(시퀀스 번호·CRC·소유 플래그)과 재부팅 후 수집·영구 저장·업로드 큐를 구현해 동시 크래시·연쇄 리셋 시나리오를 테스트한다(단일 MCU 캡처는 see:BM-A09, 디코더는 see:TOOL-A05). | mac |
| PLAT-M11 | UEFI 기초: Boot/Runtime Services와 Hello UEFI 앱 | gnu-efi(또는 EDK2 최소 앱)로 Hello UEFI 앱을 빌드해 QEMU+OVMF/AAVMF에서 실행하고 GetMemoryMap 결과와 UEFI 변수(BootOrder)를 출력, ExitBootServices 이후 사용 가능한 서비스 차이를 정리한다. | **qemu** |
| PLAT-M12 | TF-A on QEMU: BL31 런타임 서비스와 커스텀 SiP SMC | TF-A를 qemu 플랫폼으로 빌드해 BL1/BL2/BL31 로그를 확인하고, BL31에 커스텀 SiP 서비스(SMC ID 하나)를 추가한 뒤 Linux 모듈에서 arm_smccc_smc로 호출해 반환값을 검증한다. | **qemu** |
| PLAT-A01 | 측정 부트(Measured Boot)와 이벤트 로그 재생 | SHA-256 기반 PCR extend(PCR = H(PCR || digest))와 TCG 스타일 이벤트 로그를 각 부트 스테이지 모델에 넣고, 이벤트 로그를 재생해 최종 PCR 값과 일치하는지/변조 시 불일치하는지 검증하는 verifier를 작성한다. | mac |
| PLAT-A02 | SCMI: AP↔SCP 전원·클럭 제어 프로토콜 | SCMI 헤더(protocol_id, message_id, token, type)와 shared memory 채널을 구현해 agent(AP)와 platform(SCP) 스레드 간 BASE/CLOCK/PERF 프로토콜 일부(rate get/set, 지원 목록)를 주고받게 하고 비동기 지연 응답도 처리한다. | mac |
| PLAT-A03 | 멀티코어 부팅: spin-table vs PSCI, holding pen | pthread로 코어를 흉내 내 holding pen(spin-table: release_addr 폴링) 방식과 PSCI CPU_ON 방식을 구현하고, release 값 쓰기 전후 배리어(DSB/SEV 대응) 누락 시 발생할 수 있는 문제를 주석과 테스트로 설명한다. | mac |
| PLAT-A04 | 제조·프로비저닝 흐름: 시리얼, 캘리브레이션, 키 주입 | 호스트 프로비저닝 툴(Python) ↔ 디바이스 mock(pty UART) 프로토콜을 만들어 시리얼·MAC·캘리브레이션 값·디바이스 키를 OTP 모델(1회 기록, 비트 0→1만 가능)에 쓰고, 검증 후 공장 모드 잠금까지 수행하며 중복/재시도 시나리오를 테스트한다. | mac |
| PLAT-A05 | 업데이트 전송 프로토콜: 청크, 재개, 무결성 | Python 업데이터 ↔ C 디바이스 mock(소켓/pty) 간 청크 전송 프로토콜(청크 CRC, 오프셋 기반 재개, 윈도우 ACK, 최종 SHA-256)을 구현하고 패킷 손실·중복·연결 끊김 주입 테스트 후 A/B 슬롯 스왑까지 연결한다. | mac |
| PLAT-A06 | 리셋 원인 분석과 부트 루프 복구(safe mode) | reset reason 레지스터(POR, WDT, SW, brownout, lockup) 디코더와 부트 이력 링버퍼를 구현하고, N분 내 M회 비정상 리셋 시 safe mode(최소 기능+업데이트 경로만) 진입·해제 정책을 시나리오 테스트한다. | mac |
| PLAT-A07 | PMIC와 전원 시퀀싱 드라이버 | I2C regmap mock 위에 가상 PMIC 드라이버를 작성해 데이터시트식 시퀀스 테이블(레일, 전압, 지연, 의존)로 power-up/down을 수행하고 PGOOD 타임아웃·과전류 폴트 주입 시 안전한 역순 shutdown을 검증한다. | mac |
| PLAT-A08 | Linux 클럭 provider 드라이버(CCF) | fixed-rate + divider + gate로 구성된 가짜 클럭 컨트롤러 platform driver를 작성(clk_hw_register_*, of_clk_add_hw_provider)하고 소비자 드라이버가 clk_get/set_rate하는 모습을 /sys/kernel/debug/clk/clk_summary로 검증한다. | **qemu** |
| PLAT-A09 | 종합: 가상 SoC 플랫폼 펌웨어 아키텍처 리뷰 | 가상 AI 가속기 SoC(AP+관리 MCU+NPU)의 플랫폼 펌웨어 설계 문서를 작성(부트 체인, PM 상태, 메일박스 프로토콜, A/B 업데이트, 크래시/텔레메트리 경로)하고 핵심 코드 경로 3개를 앞선 항목 코드로 연결, trade-off를 면접식으로 방어한다. | mac |

### PCIE — PCIe · NVMe (PCIe & NVMe)

PCIe는 SSD·GPU·NPU·NIC를 SoC/호스트에 연결하는 표준 인터커넥트이고, NVMe는 그 위에서 가장 많이 쓰이는 스토리지 프로토콜이다. 실리콘 검증·SSD·가속기 펌웨어 면접에서는 config space/BAR/LTSSM/AER 같은 세부와 SQ/CQ·PRP 같은 NVMe 메커니즘을 '직접 디코드해봤는지'를 묻는다(예: Rivian HM 스크린의 lspci/LTSSM/L0 질문).

| ID | 제목 | 과제 | env |
|---|---|---|---|
| PCIE-B01 | PCIe 계층 구조: Transaction / Data Link / Physical | Memory Read 요청 하나가 TLP(헤더+데이터+ECRC) → DLL(Sequence#+LCRC) → PHY(STP/END 프레이밍, 128b/130b)로 감싸지는 과정을 바이트 단위로 출력하는 Python 인코더를 작성하고, 각 필드가 어떤 계층의 책임인지 주석으로 표기한다. | mac |
| PCIE-B02 | 링크 속도·폭과 실효 대역폭 계산: Gen1–Gen6 | Gen1–Gen6 × x1–x16에 대해 raw/인코딩 후 대역폭과 MPS(128/256/512B)·TLP 헤더·DLLP 오버헤드를 반영한 실효 쓰기 처리량을 계산하는 도구를 작성하고 'Gen4 x4 NVMe의 이론 최대 순차 읽기는?' 같은 질문에 답한다. | mac |
| PCIE-B03 | Configuration Space 해부: Type 0 헤더 | 256B/4KB config space hex 덤프(lspci -xxxx 형식)를 입력받아 Type 0 헤더 필드를 디코드하는 C 파서를 작성하고, Command 레지스터의 Memory Space/Bus Master 비트가 꺼져 있을 때의 의미를 출력에 포함한다. | mac |
| PCIE-B04 | Capability 리스트 순회와 PCIe Capability 디코드 | 파서를 확장해 0x34 legacy cap 리스트(PM, MSI, MSI-X, PCIe)와 0x100 extended cap 리스트(AER, LTR, L1SS 등)를 순회하고, PCIe Capability의 LnkCap/LnkCtl/LnkSta에서 최대·현재 속도/폭을 디코드하며 무한 루프 방지(방문 검사)를 구현한다. | mac |
| PCIE-B05 | BAR sizing과 디코드: all-1s 쓰기 알고리즘 | 크기·타입이 다른 BAR 6개를 가진 mock 디바이스(쓰기 마스크 동작 구현)에 대해 save→0xFFFFFFFF 쓰기→read→복원 sizing 루틴을 작성하고 64비트 BAR 쌍 처리, 디코드 중 Memory Space 비활성화 이유까지 테스트로 검증한다. | mac |
| PCIE-B06 | lspci·setpci 실전 해석: -tv, -vvv, -xxxx | Lima VM에서 lspci -tv/-vvv/-xxxx, setpci, /sys/bus/pci/devices/*/ 를 수집해 BDF, BAR, LnkCap/LnkSta, Kernel driver in use를 해석하고 자신이 만든 config 파서 출력과 대조한다; 실HW 샘플(Gen4 x4 다운트레이닝 사례)도 해석 문제로 답한다. | **linux** |
| PCIE-B07 | Type 1 헤더와 브리지: bus number, memory window | Type 1 헤더(Primary/Secondary/Subordinate Bus, Memory Base/Limit, Prefetchable Base/Limit 64비트)를 디코드하고, 주어진 주소/버스 번호가 어느 다운스트림 포트로 라우팅되는지 판정하는 함수를 작성해 테스트한다. | mac |
| PCIE-B08 | ECAM 주소 계산과 config 접근 방식 | ECAM 오프셋(bus<<20 | dev<<15 | fn<<12 | reg)으로 mock ECAM 메모리 영역을 접근하는 cfg_read/write(8/16/32) API와 CF8/CFC 방식 에뮬레이션을 구현하고, 존재하지 않는 함수 읽기 시 0xFFFF 반환 처리를 테스트한다. | mac |
| PCIE-B09 | 열거(Enumeration): DFS 버스 번호 할당 | root port–switch(USP/DSP)–endpoint로 구성된 가상 토폴로지를 만들고 DFS 열거기를 구현해 secondary/subordinate 번호를 할당, multi-function 검사(Header Type bit7)와 결과 트리(lspci -tv 형식) 출력을 검증한다. | mac |
| PCIE-B10 | TLP 헤더 인코딩/디코딩: MRd, MWr, CplD | 3DW/4DW MRd·MWr·CplD 헤더 인코더/디코더를 C로 작성하고, 임의 주소/길이 요청에 대한 First/Last DW BE 계산, 4KB 경계 분할, Completion의 Byte Count/Lower Address 계산을 테스트 벡터로 검증한다. | mac |
| PCIE-B11 | Posted/Non-posted/Completion과 Ordering 규칙 | ordering 규칙 표(Relaxed Ordering, ID-based Ordering 포함)를 코드화해 트랜잭션 시퀀스의 재정렬 허용 여부를 판단하는 검사기를 만들고, MMIO write 후 read-back이 필요한 드라이버 예시를 설명한다. | mac |
| PCIE-B12 | NVMe 컨트롤러 레지스터: CAP/VS/CC/CSTS/AQA/ASQ/ACQ | NVMe BAR0 레지스터 맵을 C 구조체로 정의(static_assert로 오프셋 검증)하고 CAP/VS/CC/CSTS 디코더를 작성해 실제 장치 덤프 샘플을 해석한다. | mac |
| PCIE-B13 | NVMe 컨트롤러 초기화 시퀀스: CC.EN과 CSTS.RDY | mock NVMe 컨트롤러(별도 스레드, CAP.TO 지연 반영)를 상대로 disable→RDY=0 대기→AQA/ASQ/ACQ 설정→CC(IOSQES/IOCQES, MPS) 설정→EN=1→RDY=1 대기(timeout) 시퀀스를 구현하고 CFS(치명적 상태) 주입 처리를 테스트한다. | mac |
| PCIE-M01 | SQ/CQ 링: head/tail, doorbell, Phase Tag | 호스트 스레드와 컨트롤러 스레드로 SQ/CQ 링을 구현(doorbell stride 반영, SQ Head를 CQE로 전달, Phase Tag 토글)하고 큐 가득 참/wrap/동시 완료 상황을 수백만 명령 스트레스 테스트로 검증한다. | mac |
| PCIE-M02 | Admin 명령: Identify Controller/Namespace 파싱 | Identify Controller(MDTS, SQES/CQES, NN, VWC, OACS)와 Identify Namespace(NSZE, NCAP, FLBAS, LBAF) 4KB 구조를 파싱하는 C 코드를 작성해 nvme id-ctrl/id-ns 바이너리 덤프(-b)를 해석하고 MDTS로 최대 전송 크기를 계산한다. | mac |
| PCIE-M03 | I/O 큐 생성과 Read/Write 명령 경로 | mock 컨트롤러(RAM-disk 백엔드)에 Create I/O CQ/SQ admin 명령 처리를 추가하고 호스트에서 SLBA/NLB 기반 Read/Write를 제출·완료 처리해 랜덤 패턴 데이터 무결성과 상태 코드(Invalid NS, LBA Out of Range)를 검증한다. | mac |
| PCIE-M04 | PRP 리스트 구성: 페이지 경계와 PRP2 해석 | 임의 버퍼(시작 오프셋, 길이, 비연속 물리 페이지)에 대해 PRP1/PRP2/PRP list(체인 포함)를 생성하는 호스트 함수와 컨트롤러 측 walker를 작성하고, 1페이지·2페이지·페이지 경계 걸침·리스트 체인 등 경계 케이스를 전수 테스트한다. | mac |
| PCIE-M05 | SGL 디스크립터와 PRP 비교 | SGL 디스크립터 생성/파싱기를 구현해 같은 버퍼를 PRP와 SGL로 기술했을 때의 디스크립터 수·메모리 사용을 비교하고, 바이트 단위 정렬이 필요한 경우(NVMe-oF, 작은 메타데이터)에 SGL이 왜 유리한지 정리한다. | mac |
| PCIE-M06 | Namespace와 LBA Format 관리 | mock 컨트롤러에 Namespace Management(create/delete)·Attach·Format NVM을 구현하고 512B/4KB LBA 포맷 간 주소 변환과 NSID 유효성(0, 0xFFFFFFFF broadcast) 처리를 테스트한다. | mac |
| PCIE-M07 | LTSSM 상태머신: Detect→Polling→Configuration→L0 | LTSSM 시뮬레이터(Detect.Quiet/Active, Polling.Active/Config, Configuration 하위 상태, L0, Recovery, 타임아웃)를 작성해 TS1/TS2 교환을 모델링하고, 레인 고장 시 x4→x2 폭 협상·Gen 속도 변경 시 Recovery.Speed 경로를 로그로 재현한다. | mac |
| PCIE-M08 | 링크 장애 진단: 속도·폭 저하와 Recovery 루프 | LnkCap vs LnkSta 비교, LTSSM 상태 레지스터·AER 카운터를 읽어 다운트레이닝/recovery 루프를 판정하는 진단 스크립트를 작성하고, 원인 후보(신호 무결성, equalization, refclk, PERST# 타이밍)별 디버그 플레이북과 2분 답변 스크립트를 만든다. | mac |
| PCIE-M09 | ASPM과 L-states: L0s/L1/L1.1/L1.2, D-states | 링크/디바이스 전력 상태 전이 모델을 만들고 경로상 모든 링크의 L1 exit latency 합과 엔드포인트 허용 latency(DevCap)를 비교해 ASPM 활성화 여부를 결정하는 함수를 구현, LTR·L1SS 설정 예제로 검증한다. | mac |
| PCIE-M10 | 인터럽트: INTx vs MSI vs MSI-X | mock BAR에 MSI-X 테이블(주소/데이터/마스크)과 PBA를 구현하고, 마스크된 벡터의 pending 처리·unmask 시 전달, NVMe CQ→벡터 매핑(CPU 친화)까지 시뮬레이션해 INTx 공유 라인 대비 장점을 측정한다. | mac |
| PCIE-M11 | Data Link Layer: 크레딧 흐름 제어와 ACK/NAK Replay | 송신/수신 DLL 시뮬레이터를 작성해 크레딧 유형(PH/PD/NPH/NPD/CplH/CplD) 소모·UpdateFC 반환, Sequence#·LCRC, NAK 시 replay와 REPLAY_NUM rollover→Recovery 진입을 비트 오류 주입으로 검증한다. | mac |
| PCIE-M12 | AER 에러 처리: Correctable, Non-fatal, Fatal | AER capability(Uncorrectable/Correctable Status, Mask, Severity, Header Log, Root Error Status) 디코더를 작성해 샘플 에러 로그(Completion Timeout, Malformed TLP, Bad DLLP)를 해석하고 등급별 처리 정책(카운트, 링크 리셋, 드라이버 recovery 콜백)을 구현한다. | mac |
| PCIE-M13 | Linux PCI 드라이버: probe, BAR iomap, MSI, DMA | QEMU edu 장치용 pci_driver를 작성(pci_enable_device, pci_request_regions, pci_iomap, dma_set_mask, pci_alloc_irq_vectors MSI)하여 factorial 계산 레지스터와 DMA 전송·완료 인터럽트를 검증하고, remove에서 역순 해제를 확인한다. | **qemu** |
| PCIE-M14 | Linux NVMe 툴링: nvme-cli와 sysfs | QEMU 에뮬 NVMe(-device nvme, 다중 namespace)로 nvme list/id-ctrl/id-ns/smart-log/get-feature/admin-passthru를 실행하고 /sys/class/nvme, 큐 개수(/sys/block/*/mq)를 확인해 앞서 만든 파서 결과와 대조한다. | **qemu** |
| PCIE-M15 | DMA와 IOMMU: IOVA 변환과 장치 격리 | IOVA→PA 다단계 IOMMU 페이지 테이블과 IOTLB를 모델링하고, 장치별 도메인 격리·권한 위반 시 fault 레코드 생성·IOTLB invalidation 누락 버그를 시뮬레이션한다; swiotlb bounce buffer와 비교 정리한다. | mac |
| PCIE-A01 | 사용자공간 NVMe 드라이버: VFIO로 BAR 매핑 (SPDK 스타일) | QEMU NVMe를 vfio-pci에 바인딩하고 사용자공간에서 BAR0 mmap, DMA 메모리 매핑(VFIO_IOMMU_MAP_DMA), admin 큐 생성, Identify, I/O 큐 생성, Read 1블록까지 수행하는 최소 드라이버를 작성한다. | **qemu** |
| PCIE-A02 | NVMe 성능 모델: QD, IOPS, Little's Law | 서비스 시간 분포를 가진 다중 큐 SSD 시뮬레이터로 QD 1–256 IOPS/평균·p99 지연 곡선을 그리고 Little's Law로 검증, 인터럽트 coalescing(시간/개수 임계)과 polling 모드의 CPU 비용 trade-off를 분석한다. | mac |
| PCIE-A03 | NVMe 에러·복구: 타임아웃, Abort, Controller Reset | mock 컨트롤러에 명령 무응답·CSTS.CFS 주입을 추가하고 호스트에 타임아웃 감시→Abort 명령→실패 시 controller reset→큐 재생성→in-flight 명령 재제출/실패 보고 경로를 구현해 데이터 손상 없이 복구되는지 검증한다. | mac |
| PCIE-A04 | End-to-End Protection(PI)과 데이터 무결성 명령 | PI Type 1(Guard CRC-16 T10-DIF, App Tag, Ref Tag=LBA 하위 32비트) 생성/검증을 구현해 mock 컨트롤러의 쓰기·읽기 경로에 넣고 비트플립·잘못된 LBA 주입 시 검출되는지 테스트하며, FUA/Flush/DSM Deallocate 처리도 추가한다. | mac |
| PCIE-A05 | SSD 펌웨어 관점: Firmware Download/Commit, SMART 로그 | mock 컨트롤러에 Firmware Image Download(오프셋 청크)·Firmware Commit(CA 0–3, 슬롯)과 SMART/Health 로그(Critical Warning, 온도, Percentage Used, Media Errors)를 구현하고 nvme-cli 형식 출력과 비교한다. | mac |
| PCIE-A06 | SR-IOV: PF/VF, VF RID 계산, ARI | SR-IOV extended capability(TotalVFs, NumVFs, First VF Offset, VF Stride, VF BAR) 디코더와 VF Routing ID 계산기(ARI 유무 비교)를 작성하고, NumVFs 변경 시 VF BAR 영역 배치를 시뮬레이션한다. | mac |
| PCIE-A07 | Hot-plug, Surprise Removal, DPC | 열거 시뮬레이터에 슬롯 hot-add/remove 이벤트를 추가해 버스/리소스 재할당을 처리하고, 드라이버 측 surprise removal(MMIO 읽기 0xFFFFFFFF 감지→I/O 실패 처리)과 DPC(Downstream Port Containment) 트리거 시 흐름을 시나리오 테스트한다. | mac |
| PCIE-A08 | 리소스 할당기: 브리지 윈도우 정렬과 P2P/ACS | 토폴로지 bottom-up으로 각 브리지 윈도우 크기(1MB 정렬, prefetchable 분리)를 합산하고 top-down으로 주소를 할당하는 리소스 할당기를 구현, 실패 케이스(공간 부족)와 P2P DMA 경로에서 ACS 설정이 주는 영향을 정리한다. | mac |
| PCIE-A09 | NVMe-oF 기초: NVMe/TCP PDU와 mini target | NVMe/TCP PDU(ICReq/ICResp, CapsuleCmd/CapsuleResp, H2CData/C2HData) 인코더/디코더를 작성하고 소켓 기반 mini target(RAM 백엔드)과 host로 Connect→Identify→Read/Write를 수행, RDMA 전송과의 차이를 정리한다. | mac |
| PCIE-A10 | CXL 입문: CXL.io/.cache/.mem과 HDM 디코더 | CXL DVSEC 레지스터 디코더와 Type 3 메모리 확장기의 HDM decoder(base, size, interleave ways/granularity) 기반 HPA→DPA 주소 변환 계산기를 작성하고, 2/4-way 인터리빙 예제로 검증한다. | mac |

### MEM — 메모리 시스템 (Memory Systems)

메모리 시스템은 SoC·AI 가속기·SSD 성능과 신뢰성을 좌우하는 병목이다. DRAM 타이밍·컨트롤러 스케줄링·ECC·캐시 일관성·NAND/FTL을 직접 시뮬레이터와 측정 코드로 구현하면서, 실리콘 bring-up/검증과 스토리지 펌웨어 면접에서 나오는 '왜 느린가/왜 깨지는가' 질문에 숫자로 답할 수 있게 한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| MEM-B01 | DRAM 조직: channel/rank/bank/row/column | 설정 가능한 주소 매핑(비트 필드 지정 + XOR 뱅크 해싱)으로 물리 주소를 (channel, rank, BG, bank, row, column)으로 분해·재조합하는 C 디코더를 작성하고 왕복 테스트와 연속 주소의 뱅크 분포를 출력한다. | mac |
| MEM-B02 | DRAM 명령과 타이밍: ACT/RD/PRE, tRCD·tRP·tRAS | 단일 뱅크 상태머신(IDLE/ACTIVE)과 타이밍 체커를 구현해 사이클 단위 명령 트레이스를 입력하면 tRCD/CL/tRP/tRAS/tRC/tWR 위반을 검출·보고하고, DDR4-3200 데이터시트 값으로 ns↔cycle 변환을 검증한다. | mac |
| MEM-B03 | Row hit/miss/conflict와 페이지 정책 | 접근 트레이스(순차, stride, 랜덤)에 대해 row hit/empty/conflict를 분류하고 open-page vs close-page 정책별 평균 지연을 계산하는 시뮬레이터를 작성해 어느 워크로드에 어느 정책이 유리한지 정리한다. | mac |
| MEM-B04 | DDR4 vs DDR5 vs LPDDR4/5 구조 비교와 대역폭 계산 | DDR4/DDR5(2×32b 서브채널, BL16)/LPDDR5(x16 채널, WCK) 비교표를 만들고 데이터율×버스폭×채널 수로 피크 대역폭을, 캐시라인 64B 전송에 필요한 burst 수를 계산하는 도구를 작성한다. | mac |
| MEM-B05 | 리프레시: tREFI, tRFC와 성능 손실 | 밀도별 tRFC와 tREFI(정상/고온 2x)를 넣어 리프레시로 잃는 시간 비율을 계산하고, all-bank vs per-bank(same-bank) refresh 스케줄을 시뮬레이터에 넣어 요청 지연 분포 변화를 측정한다. | mac |
| MEM-B06 | 패리티와 Hamming(7,4): 단일 비트 정정 | 짝수 패리티와 Hamming(7,4) 인코더/디코더를 비트 연산으로 구현하고, 모든 데이터×모든 단일 비트 오류 위치를 주입해 100% 정정되는지, 2비트 오류에서는 오정정되는지 전수 테스트한다. | mac |
| MEM-B07 | SECDED Hamming(72,64) 구현 | 64비트 데이터에 8비트 체크(7 Hamming + 전체 패리티)를 붙이는 SECDED 인코더/디코더와 신드롬→비트 위치 테이블을 구현하고 단일 비트 전수 정정, 2비트 무작위 100만 회 검출(오정정 0)을 테스트한다. | mac |
| MEM-B08 | 메모리 테스트 패턴: walking 1/0, address-in-address | 결함 주입 가능한 메모리 모델(stuck-at-0/1, 데이터 라인 단락, 주소 라인 alias)을 만들고 data bus/address bus/device 테스트(Barr 스타일)를 구현해 결함 종류별 탐지 여부 매트릭스를 출력한다. | mac |
| MEM-B09 | March 테스트: MATS+, March C- | March 표기(⇑(w0); ⇑(r0,w1); ⇓(r1,w0)...)를 해석하는 작은 인터프리터를 작성해 MATS+, March C-, March B를 실행하고 SAF/TF/CFin/AF 결함 모델별 탐지율 표를 만든다. | mac |
| MEM-B10 | 포인터 체이싱으로 메모리 지연 측정 | 랜덤 순열 linked list(캐시라인 단위)로 의존 load 체인을 만들어 4KB–1GB working set별 ns/load를 측정하고, M2의 L1/L2/SLC/DRAM 경계와 TLB 영향(페이지 단위 stride)을 그래프로 식별한다. | mac |
| MEM-B11 | 메모리 대역폭 측정: STREAM Copy/Scale/Add/Triad | STREAM 4커널을 직접 구현(배열 크기 ≥ 4×LLC, 컴파일러 최적화 확인)해 스레드 수 1–8별 GB/s를 측정하고, M2 LPDDR5 이론 대역폭 대비 효율과 쓰기 할당(write-allocate) 영향을 분석한다. | mac |
| MEM-B12 | NAND 플래시 기초: 페이지/블록, program/erase 제약 | NAND 칩 모델(planes/blocks/pages, 페이지 OOB, 블록 내 순차 program 강제, erase 단위, P/E 카운트, SLC/TLC 파라미터)을 C로 작성하고 제약 위반 시 에러를 반환하는지 테스트한다. | mac |
| MEM-M01 | 메모리 컨트롤러 스케줄링: FCFS vs FR-FCFS | 다중 뱅크 요청 큐 기반 MC 시뮬레이터를 작성해 FCFS, FR-FCFS, FR-FCFS+age cap 정책을 구현하고 혼합 트레이스에서 처리량·평균/최대 지연을 비교, 읽기/쓰기 큐 분리와 write drain 워터마크도 추가한다. | mac |
| MEM-M02 | 주소 매핑 정책과 뱅크 병렬성 | RoBaRaCoCh, RoRaBaChCo, XOR 해싱 매핑을 MC 시뮬레이터에 연결해 stride(64B–64KB) 접근별 bank conflict율·처리량을 측정하고 power-of-two stride 병목을 XOR 해싱이 해소함을 보인다. | mac |
| MEM-M03 | 다단계 캐시 시뮬레이터: inclusive vs exclusive | set-associative L1/L2 시뮬레이터(LRU/PLRU, write-back/write-through, write-allocate 여부, inclusive/exclusive)를 작성해 트레이스별 3C miss 분류와 DRAM 트래픽(bytes)을 비교한다. | mac |
| MEM-M04 | 일관성 트래픽 실측: 공유 캐시라인 경합과 코어 수 | 공유 카운터 atomic 증가, 코어별 카운터, 읽기 전용 공유 3가지 패턴을 1~8스레드(P/E 코어)로 실측해 확장성 곡선을 그리고, ARCH-A04 MESI 시뮬레이터의 상태 전이 횟수 예측과 비교한다. | mac |
| MEM-M05 | MOESI/MESIF와 디렉터리 기반 일관성 | MESI 시뮬레이터를 MOESI로 확장해 dirty 공유 시 쓰기 되돌림 감소를 측정하고, full-bit-vector 디렉터리 프로토콜(홈 노드, invalidation, ack 수집)을 구현해 코어 수 증가에 따른 메시지 수를 스누핑과 비교한다. | mac |
| MEM-M06 | 쓰기 대역폭과 RFO: write-allocate 비용 측정 | 순차 쓰기·memset·스트라이드 쓰기·읽기-수정-쓰기의 대역폭을 working set별로 M2에서 측정해 RFO로 생기는 실제 DRAM 트래픽(읽기+쓰기)을 추정하고, DC ZVA/STNP 사용 시 차이를 비교한다(false sharing은 see:ARCH-M08). | mac |
| MEM-M07 | 메모리 일관성 모델 시뮬레이터: SC·TSO·ARM 허용 결과 열거 | 작은 litmus 프로그램(MP, SB, LB, IRIW)을 입력받아 SC, TSO(store buffer), ARMv8(완화) 모델별 허용 결과 집합을 전수 열거하는 시뮬레이터를 작성하고, ARCH-A05의 M2 실측 결과와 대조한다. | mac |
| MEM-M08 | DRAM 저전력 상태: power-down·self-refresh 정책 | M01 컨트롤러 시뮬레이터에 idle 타이머 기반 power-down/self-refresh 진입·탈출(tXP, tXS) 모델을 추가해 버스트성 트레이스에서 타이머 값별 평균 지연 증가와 에너지 절감을 계산한다(TLB·page walk는 see:ARCH-A01~A03). | mac |
| MEM-M09 | NUMA: 지역성, first-touch, 원격 접근 비용 | 2/4-노드 NUMA 지연·대역폭 모델 시뮬레이터에 first-touch/interleave/bind 정책을 구현해 병렬 워크로드 성능을 비교하고, 리눅스 numactl --hardware/--membind 실측 절차를 정리한다. | mac |
| MEM-M10 | FTL 매핑 테이블: page-level L2P | 앞서 만든 NAND 모델 위에 page-mapping FTL(L2P 테이블, P2L(OOB 기록), valid bitmap, 활성 블록 할당)을 구현하고 랜덤 읽기/쓰기 수십만 회 후 참조 모델(배열 디스크)과 데이터 일치를 검증한다. | mac |
| MEM-M11 | Garbage Collection: victim 선택과 WAF | FTL에 GC(greedy, cost-benefit, FIFO victim 선택)를 추가하고 uniform/hot-cold(80/20) 워크로드와 OP 7%/28%에 대해 WAF·GC 지연 스파이크를 측정, hot/cold 분리 스트림의 효과를 비교한다. | mac |
| MEM-M12 | Wear leveling: dynamic vs static | FTL에 dynamic WL(낮은 P/E 프리 블록 선택)과 static WL(cold 데이터 블록 이동 임계값)을 구현해 장기 워크로드에서 P/E 분포 표준편차·첫 블록 수명 도달 시점·추가 쓰기량을 비교한다. | mac |
| MEM-M13 | Bad block 관리와 NAND 오류 처리 | NAND 모델에 factory bad block 마킹, P/E 증가에 따른 RBER 증가, program/erase 실패를 주입하고 FTL에 bad block 테이블·실패 시 데이터 재배치·read retry 단계를 구현, RBER→UBER(ECC 정정 능력 t) 계산도 포함한다. | mac |
| MEM-M14 | FTL 전원 차단 복구: 체크포인트와 OOB 스캔 | 주기적 L2P 체크포인트 + 이후 쓰기의 OOB(LPA, seq) 스캔으로 매핑을 재구성하는 복구 루틴을 구현하고, 임의 시점 power-loss를 수천 회 주입해 ACK된 쓰기가 모두 보존되는지 검증한다. | mac |
| MEM-A01 | Cycle-accurate DRAM 모델: bank group·tFAW | MC 시뮬레이터를 cycle 단위로 확장해 bank group별 tCCD_S/L, tRRD_S/L, tFAW, tWTR, 읽기↔쓰기 turnaround를 반영하고, 같은 BG 연속 접근 vs BG 교차 접근의 대역폭 차이를 트레이스로 재현해 Ramulator 결과와 비교한다. | mac |
| MEM-A02 | Row Hammer와 완화: TRR, PARA, ECC의 한계 | 행 활성화 횟수 기반 이웃 행 비트플립 확률 모델을 메모리 시뮬레이터에 넣고 double-sided hammer 패턴을 재현한 뒤 TRR(카운터), PARA(확률적 이웃 refresh), ECC 적용 시 남는 오류율을 비교한다. | mac |
| MEM-A03 | HBM 구조와 대역폭: stack, pseudo-channel, AI 가속기 | HBM2e/HBM3/HBM3E 스택당 대역폭(채널 수×폭×데이터율) 계산기를 만들고 가상 NPU(연산 TOPS, HBM 스택 수)의 roofline에 GEMM/attention 연산 강도를 찍어 compute-bound vs memory-bound를 판정한다. | mac |
| MEM-A04 | 메모리 트레이닝: write leveling과 DQ eye 센터링 | 노이즈·지터가 있는 DQ/DQS 타이밍 모델에서 지연 탭을 스윕해 pass/fail 윈도우를 찾고 eye 중앙 선택, 비트별 deskew, read gate 훈련을 구현한 뒤, 온도 드리프트 시 periodic retraining 필요성을 시뮬레이션한다. | mac |
| MEM-A05 | Chipkill·Reed-Solomon 심볼 ECC 기초 | GF(2^8) 산술(log/antilog 테이블)과 RS 인코더·신드롬 계산·단일 심볼 오류 정정을 구현하고, x4 DRAM 칩 하나가 통째로 고장 난 상황을 SECDED와 비교해 chipkill의 필요성을 보인다. | mac |
| MEM-A06 | ECC RAS 펌웨어: patrol scrub, CE/UE, page retire | SECDED 메모리 모델 위에 patrol scrub 스케줄러(대역폭 예산), CE 누적 카운트·누설 버킷 임계치 기반 page offlining, UE 시 처리 경로(poison, 리셋)를 구현하고 EDAC 스타일 로그를 출력한다. | mac |
| MEM-A07 | 하드웨어 prefetcher 모델: stride·stream | 캐시 시뮬레이터에 PC 기반 stride prefetcher와 stream prefetcher(degree/distance 파라미터)를 추가해 순차/stride/포인터 체이싱 트레이스에서 accuracy·coverage·지연 이득과 대역폭 오염을 비교한다. | mac |
| MEM-A08 | Roofline으로 메모리 병목 분석: 타일링 전후 | naive/블록 타일링/전치 버전 행렬곱을 M2에서 측정해 GFLOP/s와 추정 DRAM 트래픽(캐시 시뮬레이터 또는 이론식)으로 연산 강도를 계산하고, STREAM 대역폭·피크 FLOPS로 만든 roofline 위에 표시해 개선 원인을 설명한다. | mac |
| MEM-A09 | 고급 FTL: DFTL(매핑 캐시)과 SLC 캐시 | DFTL(CMT LRU 캐시, translation page, GTD)을 구현해 매핑 캐시 크기 대비 추가 NAND 읽기/쓰기를 측정하고, SLC 캐시(동적 영역, 버스트 쓰기 흡수·폴딩) 정책을 추가해 버스트 쓰기 지연 변화를 분석한다. | mac |
| MEM-A10 | 종합: SoC 메모리 서브시스템 bring-up 계획 | 가상 SoC의 DDR bring-up 문서(PHY init→training→MC 타이밍 설정→memtest 단계(데이터/주소 버스→March)→ECC/scrub 정책→대역폭/지연 검증)를 작성하고 앞선 항목 코드로 각 단계 핵심 코드 경로를 연결, 실패 시 디버그 트리를 정리한다. | mac |

## 4단계 · 품질 · 보안 · 인프라 (Quality, Security & Infra)

### TEST — 테스트 · 안전 · 신뢰성 (Testing, Safety & Reliability)

임베디드 소프트웨어는 버그 비용이 크고(리콜, 사고) 하드웨어가 늘 곁에 있지 않기 때문에, 호스트 단위 테스트·모킹·결함 주입·기능안전 분석 능력이 시니어 펌웨어 엔지니어의 핵심 역량으로 평가된다. 이 트랙은 Unity/TDD로 시작해 퍼징·커버리지·MISRA, ISO 26262/IEC 61508/DO-178C의 분석 기법(FMEA, FTA, ASIL)과 신뢰성 수학까지 직접 도구를 만들며 익힌다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| TEST-B01 | 미니 단위 테스트 프레임워크 직접 만들기 | TEST_ASSERT_EQUAL_INT/HEX/MEMORY 매크로(파일·라인 출력), 테스트 등록·러너, setjmp 기반 실패 시 다음 테스트로 계속하기를 갖춘 200줄 이하 프레임워크를 작성해 CRC-8 함수 테스트에 적용한다. | mac |
| TEST-B02 | Unity 실전: 경계값 분석과 테이블 기반 테스트 | Unity로 명령 파서 모듈(길이 필드, 체크섬, 미지원 명령)의 테스트 스위트를 작성하고 등가 분할·경계값 표를 먼저 만든 뒤 테이블 기반 테스트로 구현, 의도적으로 넣은 off-by-one 버그를 잡는지 확인한다. | mac |
| TEST-B03 | TDD로 드라이버 작성: Red→Green→Refactor | Grenning식 LED/GPIO 드라이버를 가상 레지스터 포인터를 주입받는 형태로 TDD 진행(테스트 목록 → 실패 테스트 → 최소 구현 → 리팩터)하고, 각 커밋을 사이클 단위로 남겨 리뷰한다. | mac |
| TEST-B04 | 하드웨어 의존성 분리: link seam, 함수 포인터, 전처리기 seam | I2C 센서 드라이버 하나를 link-time 대체(테스트용 i2c_fake.c), 함수 포인터 ops 주입, 전처리기 매크로 치환 세 방식으로 테스트하고 코드 침투성·런타임 비용·가독성을 비교표로 정리한다. | mac |
| TEST-B05 | CMock 스타일 mock: expect/return 큐 생성기 | 헤더의 함수 선언을 파싱해 Expect/Return/Ignore API와 호출 기록 큐를 가진 mock C 파일을 생성하는 Python 스크립트를 작성하고, 예상치 못한 호출·인자 불일치·호출 누락을 검출하는지 테스트한다. | mac |
| TEST-B06 | 코드 커버리지: line·branch·MC/DC | llvm-cov(gcov 호환)로 파서 테스트의 line/branch 커버리지를 측정해 누락 분기를 테스트로 채우고, 3조건 결정문 (A && B) || C에 대해 MC/DC를 만족하는 최소 테스트 세트를 손으로 설계·검증한다. | mac |
| TEST-B07 | 정적 분석 트리아지와 CI 게이트 | 샘플 펌웨어의 clang-tidy·cppcheck 결과를 SARIF로 모아 진짜 결함/오탐/스타일로 분류하고, baseline 파일·인라인 억제 규칙·신규 경고만 실패시키는 CI 게이트 스크립트를 작성한다(도구 사용법은 see:TOOL-M12). | mac |
| TEST-B08 | Sanitizer 기반 호스트 테스트: ASan/UBSan | signed overflow, 과도한 shift, 미정렬 접근, OOB, use-after-scope를 포함한 펌웨어 모듈을 -fsanitize=address,undefined로 단위 테스트해 리포트를 해석·수정하고, 테스트 빌드 기본 옵션으로 만든다. | mac |
| TEST-B09 | Watchdog 설계: 태스크 체크인 감시 | 태스크별 heartbeat 비트마스크와 감시 태스크가 모든 비트가 모였을 때만 하드웨어 WDT를 kick하는 구조를 구현하고(mock WDT), 한 태스크 hang·감시 태스크 hang·windowed WDT 조기 kick 시나리오를 테스트한다. | mac |
| TEST-B10 | 신뢰성 수학: MTBF, FIT, 직렬/병렬 시스템 | 지수 분포 가정의 R(t)=e^(-λt), FIT↔MTBF 변환, 직렬/병렬/k-out-of-n 블록 신뢰도를 계산하는 Python 도구를 만들고, 부품 FIT 표로 보드 MTBF와 이중화 전원의 개선 효과를 계산한다. | mac |
| TEST-M01 | MISRA C:2012 핵심 규칙과 deviation 관리 | 레거시 펌웨어 모듈에 cppcheck MISRA addon을 돌려 위반 상위 10개 규칙(암시적 변환, essential type, 복합 표현식 부작용, 단일 exit 등)을 수정하고, 의도적으로 유지할 위반 1건에 대한 deviation record를 작성한다. | mac |
| TEST-M02 | Fuzzing: libFuzzer로 프로토콜 파서 깨기 | TLV/명령 파서용 libFuzzer harness(ASan+UBSan)를 작성해 seed corpus·dictionary로 크래시를 찾고, 최소화(-minimize_crash)→수정→크래시 입력을 Unity 회귀 테스트와 corpus에 추가한다. | mac |
| TEST-M03 | Property-based·모델 기반 테스트 | 링버퍼/KV 저장소 같은 상태 모듈을 단순 참조 모델(Python dict/list)과 같은 랜덤 연산 시퀀스로 구동하는 하네스(C 모듈을 ctypes로 호출 + hypothesis)를 작성하고 실패 시 시퀀스 축소(shrinking) 결과를 분석한다. | mac |
| TEST-M04 | 소프트웨어 결함 주입: 에러 경로 테스트 | HAL 계층에 주입 지점(I2C NACK, 타임아웃, 버스 bit flip, malloc 실패, 플래시 쓰기 실패)을 넣고 시나리오 테이블로 구동해 각 에러가 올바른 재시도/보고/안전 상태로 이어지는지와 에러 경로 branch 커버리지를 측정한다. | mac |
| TEST-M05 | SIL→HIL 테스트 하네스 설계 | DUT(호스트 빌드 펌웨어 프로세스)와 센서/액추에이터 시뮬레이터를 pty/소켓으로 연결하는 pytest 하네스를 만들고, 전송 계층만 바꾸면 실제 보드(UART)로 전환 가능한 구조로 설계해 폐루프 시나리오 테스트를 작성한다. | mac |
| TEST-M06 | 회귀 테스트 전략: 테스트 피라미드와 영향 기반 선택 | unit/integration/HIL/soak 계층별 실행 정책을 문서화하고, 변경 파일→관련 테스트 매핑(의존성 그래프 또는 커버리지 데이터)으로 실행할 테스트를 선택하는 스크립트를 작성해 전수 실행 대비 시간/검출률을 비교한다. | mac |
| TEST-M07 | ISO 26262 기초: HARA와 ASIL 결정 | 전자식 파킹 브레이크·차선 유지·조명 3개 기능에 대해 HARA 표(위험 사건, S/E/C 근거)를 작성하고 ASIL 결정표를 코드로 구현해 결과를 검증, safety goal과 FTTI를 정의한다. | mac |
| TEST-M08 | FMEA: 고장 모드·영향 분석 | IMU 센서 보드(전원, MCU, IMU, CAN 트랜시버)의 FMEA 표를 작성하고 RPN(S×O×D)과 AIAG-VDA Action Priority를 계산하는 스크립트로 상위 위험 5개와 완화책(진단 추가, 설계 변경)을 정리한다. | mac |
| TEST-M09 | FTA: 결함 트리와 최소 컷셋 | AND/OR 게이트 결함 트리를 입력받아 최소 컷셋(MOCUS 알고리즘)과 rare-event 근사 최상위 사건 확률을 계산하는 Python 프로그램을 작성하고, '모터 의도치 않은 구동' 트리로 단일 고장점(1차 컷셋)을 찾는다. | mac |
| TEST-M10 | 런타임 오류 검출: 이중 저장, 제어 흐름 감시 | 안전 변수(값+비트 반전 사본) 래퍼, 범위/타당성 검사, 프로그램 흐름 서명(checkpoint 누적 해시) 모니터를 구현하고, 메모리 비트플립·함수 스킵 주입으로 탐지율을 측정한다. | mac |
| TEST-M11 | 런타임 자가진단: 분할 RAM March 테스트와 Flash CRC | RAM을 작은 블록 단위로 백업→March C- 실행→복원하는 비파괴 런타임 RAM 테스트(인터럽트 임계구역 포함)와 플래시 이미지 증분 CRC 검사를 구현하고 시간 예산 내 전체 순회 주기를 계산한다. | mac |
| TEST-M12 | IEC 61508·DO-178C 개념과 요구사항 추적성 | 소스/테스트 주석의 @req 태그를 스캔해 요구사항→구현→테스트 추적 매트릭스(HTML/CSV)와 미추적 요구사항·고아 코드 리포트를 생성하는 도구를 만들고, DAL A–D/SIL 1–4별 커버리지 목표 표와 연결한다. | mac |
| TEST-A01 | 이중화 아키텍처: 1oo2, 2oo3 voting, lockstep | 2oo3 TMR voter(정수/부동소수 허용오차, 불일치 기록)와 1oo2 비교기를 구현하고, 채널별 독립·공통 원인 고장 주입 시뮬레이션으로 안전 실패율과 가용도를 비교, lockstep 코어 개념과 연결해 설명한다. | mac |
| TEST-A02 | FMEDA와 하드웨어 메트릭: SPFM, LFM, PMHF | 부품별 FIT·고장 모드 분포·진단 커버리지를 담은 FMEDA 표를 입력받아 SPFM/LFM/PMHF를 계산하는 스크립트를 작성하고, ASIL B/D 목표 미달 시 어떤 안전 메커니즘을 추가해야 하는지 민감도 분석을 한다. | mac |
| TEST-A03 | Mutation testing: 테스트 스위트의 품질 측정 | C 소스에 연산자 치환(< ↔ <=, + ↔ -, 상수 ±1, 조건 부정) mutant를 생성해 테스트를 자동 실행하는 Python 도구를 작성하고 mutation score를 측정, 살아남은 mutant를 죽이는 테스트를 추가한다. | mac |
| TEST-A04 | 측정 기반 타이밍 분석과 WCET | 고해상도 타이머로 제어 루프 함수의 실행시간 분포를 수만 회 측정하고, 최악 경로를 유발하는 입력을 구성(분기·루프 상한 분석)해 관측 최대값과 비교, 캐시/인터럽트가 측정 기반 WCET에 주는 불확실성을 정리한다. | mac |
| TEST-A05 | 결정론적 시뮬레이션 테스트: 가상 시간과 재현 가능한 seed | 펌웨어 로직의 시간·타이머·인터럽트·통신을 가상 시계/이벤트 큐로 추상화한 시뮬레이션 하네스를 만들고, 랜덤 seed 기반 이벤트 순서 탐색으로 경쟁 조건 버그를 찾아 동일 seed로 재현·수정한다. | mac |
| TEST-A06 | Soak·Chaos 테스트: 장시간 스트레스와 누수 탐지 | 호스트 빌드 펌웨어를 가상 시간 가속으로 수십 일치 실행하며 메모리·핸들·큐 깊이 지표를 주기적으로 모으고, 랜덤 장애(버스 오류·지연·리셋) 주입 중 추세 기반 누수와 49.7일 랩 버그를 자동 판정하는 하네스를 작성한다(SEU 주입은 see:AERO-A03). | mac |
| TEST-A07 | 가속 수명 시험과 Weibull 분석 | 고장 시간 데이터(중도 절단 포함)에 Weibull(β, η)을 최대우도로 적합하는 스크립트를 작성하고, Arrhenius 가속계수로 사용 조건 수명을 환산, β<1/=1/>1이 의미하는 고장 메커니즘과 번인 필요성을 판단한다. | mac |
| TEST-A08 | 종합: 인증 가능한 안전 펌웨어 모듈 패키지 | 모터 과전류 보호 모듈에 대해 안전 요구사항 → 설계(안전 메커니즘) → MISRA 준수 코드 → MC/DC 단위 테스트 → FMEA/FTA → 추적 매트릭스 → 결함 주입 결과를 한 패키지로 만들고 면접식 safety case 발표를 준비한다. | mac |

### SEC — 임베디드 보안 (Embedded Security)

연결된 임베디드 디바이스(차량, 드론, 로봇, 방산 장비)는 물리적 접근이 가능한 공격자를 전제로 설계해야 하므로, 보안 부팅·키 관리·안전한 업데이트·사이드채널/글리치 방어가 펌웨어 엔지니어의 기본기가 되었다. 이 트랙은 C 메모리 안전 버그와 기본 암호 구현에서 출발해 chain of trust, TEE/MPU 하드닝, 공격 시뮬레이션까지 직접 만들어 보며 익힌다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| SEC-B01 | 위협 모델링: 자산, 공격 표면, STRIDE | Wi-Fi 연결 센서 디바이스(부트로더, 앱, OTA, 디버그 포트, 클라우드 API)의 DFD와 신뢰 경계를 그리고 STRIDE 위협 표(위협, 영향, 가능성, 완화책)를 작성해 상위 5개 위협과 대응 트랙 항목을 매핑한다. | mac |
| SEC-B02 | C 메모리 안전 버그 1: 스택 버퍼 오버플로 | 고정 크기 버퍼에 길이 검사 없이 복사하는 명령 핸들러를 작성해 -fno-stack-protector 빌드에서 lldb로 인접 변수·저장된 레지스터가 덮이는 모습을 관찰하고, ASan 탐지 → bounded copy와 길이 검증으로 수정한 뒤 회귀 테스트를 추가한다. | mac |
| SEC-B03 | C 메모리 안전 버그 2: 정수 오버플로, 부호 혼동, off-by-one | 패킷 길이 필드 처리에서 흔한 취약 패턴 5종(len+hdr 오버플로, signed/unsigned 비교, size_t 언더플로, off-by-one, 곱셈 오버플로)을 재현하고 __builtin_add_overflow 등 checked arithmetic으로 수정해 경계 테스트로 검증한다. | mac |
| SEC-B04 | C 메모리 안전 버그 3: UAF, double free, format string | use-after-free, double free, 사용자 입력을 포맷 문자열로 쓰는 로그 함수를 재현해 ASan/-Wformat-security로 탐지하고, 소유권 규칙·free 후 NULL·고정 포맷 사용으로 수정하며 펌웨어에서 정적 할당을 선호하는 이유를 정리한다. | mac |
| SEC-B05 | 해시 함수 구현: SHA-256 | SHA-256을 직접 구현(init/update/final 스트리밍 API, 빅엔디언 처리, 패딩)하고 NIST 테스트 벡터(빈 문자열, 'abc', 백만 'a')와 청크 분할 입력 일치를 검증한다. | mac |
| SEC-B06 | 메시지 인증: HMAC-SHA256과 길이 확장 공격 | 자신의 SHA-256으로 HMAC을 구현해 RFC 4231 벡터로 검증하고, H(secret||msg) 방식에 대한 length extension 공격을 실제로 구현해 위조 메시지가 검증을 통과함을 보인 뒤 HMAC에서는 실패함을 확인한다. | mac |
| SEC-B07 | 타이밍 안전 비교: constant-time memcmp | memcmp 기반 MAC 비교의 일치 바이트 수에 따른 실행시간 차이를 통계(수만 샘플, 중앙값)로 측정하고 XOR-OR 누적 constant-time 비교를 구현, -O2 디스어셈블리에서 분기가 없는지 확인한다. | mac |
| SEC-B08 | AES와 블록 암호 모드: ECB, CBC, CTR | Mbed TLS AES로 비트맵 이미지를 ECB/CBC/CTR 암호화해 ECB 패턴 노출을 확인하고, CTR nonce 재사용 시 두 평문 XOR이 드러나는 공격과 CBC 비트 플립 공격을 재현한 뒤 올바른 IV/nonce 관리 규칙을 정리한다. | mac |
| SEC-B09 | AEAD: AES-GCM / ChaCha20-Poly1305로 패킷 보호 | 디바이스↔게이트웨이 패킷 포맷(헤더=AAD, 카운터 기반 nonce, 태그)을 설계하고 Mbed TLS/libsodium으로 구현, 태그 변조·리플레이·nonce 중복을 거부하는지 테스트한다. | mac |
| SEC-B10 | 난수와 엔트로피: TRNG, DRBG, health test | 시간 시드 rand()로 생성한 키를 시드 범위 탐색으로 복원하는 데모를 만들고, CTR_DRBG(Mbed TLS) 기반 난수 API와 TRNG 원시 출력용 Repetition Count·Adaptive Proportion health test(SP 800-90B)를 구현한다. | mac |
| SEC-M01 | 디지털 서명: ECDSA P-256과 Ed25519 | Mbed TLS(ECDSA P-256)와 libsodium(Ed25519)으로 펌웨어 이미지 서명/검증 도구를 만들고 DER vs raw(r||s) 서명 포맷 변환, 공개키 해시 고정, ECDSA nonce 재사용 시 개인키가 복원되는 원리를 수식과 코드로 확인한다. | mac |
| SEC-M02 | Secure boot 검증 경로 구현 | 이미지 헤더(magic, 버전, 크기, 해시, 서명, 헤더 자체 범위)를 정의하고 부트로더 검증 함수(헤더 경계 검사→해시→서명→점프)를 구현, 변조 이미지·잘린 이미지·헤더 크기 조작·TOCTOU 시나리오 테스트 세트로 검증한다. | mac |
| SEC-M03 | Chain of trust와 키 계층: ROTPK, 인증서, 폐기 | OTP에 ROTPK 해시를 두고 root→content-signing 키 인증서 체인을 검증하는 코드를 구현, 키 폐기 비트(OTP fuse 모델)로 유출된 서명 키를 무효화하는 시나리오를 테스트한다. | mac |
| SEC-M04 | Anti-rollback: 보안 버전과 단조 카운터 | eFuse/OTP 단조 카운터 모델과 이미지의 security version을 비교하는 검사를 구현하고, A/B 업데이트에서 카운터를 '언제' 올려야 하는지(새 이미지 mark-good 이후) 잘못된 순서가 벽돌/우회를 만드는 시나리오로 검증한다. | mac |
| SEC-M05 | Secure firmware update: 서명된 매니페스트와 암호화 페이로드 | 서명된 매니페스트(버전, 대상 하드웨어, 페이로드 해시, 암호화 키 래핑 정보)와 AES-GCM 암호화 페이로드로 업데이트 패키지를 만들고 디바이스 검증/설치 로직을 구현, TUF/SUIT 개념과 비교한다. | mac |
| SEC-M06 | 키 저장과 키 래핑: KEK, AES-KW, 보안 엘리먼트 | 디바이스 고유 키(하드웨어 유도 KEK 모델)로 데이터 키를 AES-KW(RFC 3394)로 wrap/unwrap하는 코드를 구현해 RFC 벡터로 검증하고, OTP·보안 엘리먼트·TEE 키 저장 옵션별 위협 대응을 비교한다. | mac |
| SEC-M07 | TrustZone-A와 TEE: OP-TEE Trusted Application | OP-TEE QEMUv8 환경에서 hello_world TA와 클라이언트 앱을 빌드·실행하고(또는 가이드 따라 구조 분석), '키를 TA 밖으로 내보내지 않는 서명 서비스' TA 인터페이스(UUID, 명령 ID, 파라미터 타입)를 설계한다. | **qemu** |
| SEC-M08 | MPU 권한 분리: 비특권 태스크 샌드박스와 SVC 게이트 | QEMU Cortex-M에서 비특권 thread 모드 태스크에 자기 스택·데이터 영역만 허용하는 MPU 영역을 컨텍스트 전환마다 바꿔 끼우고, 특권 서비스는 SVC 게이트로만 호출하게 만든 뒤 다른 태스크 메모리·주변장치 접근이 MemManage fault로 차단되는지 검증한다(기본 MPU 설정은 see:BM-A08). | **qemu** |
| SEC-M09 | 컴파일러 하드닝: stack protector, FORTIFY, CFI, PIE | -fstack-protector-strong, _FORTIFY_SOURCE=2, -fsanitize=cfi, PIE/RELRO 각각의 효과를 디스어셈블리·readelf로 비교하고, bare-metal용 __stack_chk_guard(부팅 시 난수 초기화)와 __stack_chk_fail을 직접 구현한다. | mac |
| SEC-M10 | 보안 통신: 임베디드 TLS 1.3과 mTLS | Mbed TLS로 디바이스 인증서 기반 mTLS 클라이언트/서버를 구성하고 인증서 핀닝·호스트명 검증을 구현, 핸드셰이크 시간·피크 RAM·코드 크기를 측정해 PSK 모드와 비교한다. | mac |
| SEC-M11 | 디버그 포트 잠금: JTAG/SWD와 readout protection | RDP 레벨(0/1/2) 상태머신 모델(레벨 하향 시 전체 erase)과 challenge-response 인증 디버그 언락(디바이스 nonce + 서명된 응답, 디바이스 ID 바인딩)을 구현하고 리플레이·다른 디바이스 토큰 재사용을 거부하는지 테스트한다. | mac |
| SEC-M12 | 펌웨어 바이너리 분석: 비밀 노출 찾기 | API 키·비밀번호·개인키가 박힌 펌웨어 바이너리(직접 빌드)에서 strings, binwalk, 엔트로피 그래프, Ghidra 함수 참조로 비밀을 찾아내고, 빌드 파이프라인에서 비밀 스캔과 디바이스별 provisioning으로 대체하는 방안을 정리한다. | mac |
| SEC-A01 | 타이밍 사이드채널 공격 실습과 방어 | 로컬 소켓 서버의 비-constant-time 토큰 검증에 대해 바이트별 후보 응답시간을 수천 샘플 수집·통계 검정해 토큰을 복원하는 공격 스크립트를 작성하고, constant-time 비교·rate limit 적용 후 공격이 실패함을 확인한다. | mac |
| SEC-A02 | 전력 분석 시뮬레이션: CPA로 AES 키 복원 | AES 첫 라운드 S-box 출력의 Hamming weight + 가우시안 노이즈로 합성 전력 트레이스를 만들고, 키 바이트 후보별 상관계수로 키를 복원하는 CPA를 구현해 노이즈 대비 필요 트레이스 수를 측정, 마스킹 적용 시 효과를 비교한다. | mac |
| SEC-A03 | 결함 주입(글리칭) 방어 코딩 | 명령 스킵·분기 반전·레지스터 비트플립을 흉내 내는 결함 시뮬레이터(검증 함수 실행 단계에 주입)를 만들고, 이중 검사·상보 상수 플래그(0x3CA5/0xC35A)·무작위 지연·루프 완료 카운터 검증으로 secure boot 분기를 하드닝해 우회율을 비교한다. | mac |
| SEC-A04 | 제어 흐름 탈취와 완화: NX, ASLR, PAC/BTI | 격리된 실습 프로그램에서 스택 오버플로로 리턴 주소를 다른 함수로 바꾸는 제어 흐름 탈취를 재현(보호 해제 빌드)하고, M2(arm64e)에서 -mbranch-protection=standard 빌드 시 PACIASP/AUTIASP 삽입과 탈취 실패를 디스어셈블리·실행으로 확인한다. | mac |
| SEC-A05 | 원격 증명(Remote Attestation) | 측정값(부트 스테이지 해시)과 서버 nonce를 포함한 증명 토큰(CBOR/EAT 유사)을 디바이스 키로 서명하고, 서버 검증기가 서명·nonce 신선도·허용 측정값 목록을 확인하도록 구현해 리플레이와 변조 펌웨어를 거부하는지 테스트한다. | mac |
| SEC-A06 | 상태 기반 프로토콜 퍼징 | 인증→세션→명령 단계를 가진 디바이스 명령 프로토콜에 libFuzzer custom mutator(구조 인식, 메시지 시퀀스 생성)를 적용해 상태 커버리지를 높이고, 일반 퍼징 대비 도달 상태 수·발견 버그를 비교한다. | mac |
| SEC-A07 | 메모리 안전 언어 비교: Rust no_std 파서 | 앞서 취약했던 TLV 패킷 파서를 Rust no_std(슬라이스·Result 기반)로 재작성하고 C 버전과 같은 퍼징 하네스(cargo-fuzz)를 돌려 버그 수·코드 크기·성능을 비교, C와 FFI로 공존시키는 경계 설계를 정리한다. | mac |
| SEC-A08 | 종합: 디바이스 보안 아키텍처 리뷰 | B01 위협 모델을 갱신해 보안 부팅·키 계층·anti-rollback·secure update·디버그 잠금·증명·로깅을 엮은 보안 아키텍처 문서를 작성하고, 자체 레드팀 체크리스트(각 위협에 대한 테스트 증거 링크)로 평가해 잔여 위험을 정리한다. | mac |

### INFRA — 임베디드 인프라 · DevOps (Embedded Infra & DevOps)

펌웨어 팀의 생산성과 제품 품질은 빌드·테스트·배포 인프라에 달려 있으며, 스타트업(Anduril, Neros 등)과 대기업 모두 CI·HIL 팜·OTA·크래시 수집을 직접 구축할 수 있는 임베디드 엔지니어를 원한다. 이 트랙은 재현 가능한 빌드와 CI에서 시작해 보드 팜 제어, OTA·텔레메트리·크래시 파이프라인, 릴리스 관리까지 Python 자동화로 직접 만든다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| INFRA-B01 | 멀티 타깃 펌웨어 빌드: CMake toolchain file과 presets | host/arm-none-eabi 두 타깃을 CMake toolchain file + CMakePresets.json으로 구성하고, 호스트 빌드는 단위 테스트, 타깃 빌드는 .elf/.bin/.map 산출물과 크기 리포트(size)를 내도록 out-of-tree 빌드 스크립트를 작성한다. | mac |
| INFRA-B02 | Docker로 툴체인 고정 | arm-none-eabi-gcc·CMake·Python 버전을 고정한 Dockerfile(베이스 이미지 digest pin, 체크섬 검증 다운로드)을 작성하고 소스를 마운트해 빌드하는 래퍼 스크립트를 만들어, 호스트 빌드와 산출물 해시를 비교한다. | mac |
| INFRA-B03 | GitHub Actions 기초: 빌드·테스트 워크플로 | push/PR 트리거, matrix(host/arm, Debug/Release), 컨테이너 잡, 캐시(ccache·pip), 산출물 업로드를 갖춘 워크플로를 작성하고 act로 로컬 실행해 확인, 실패 시 로그를 읽고 고치는 과정을 기록한다. | mac |
| INFRA-B04 | Python 시리얼 콘솔 자동화: pty 가짜 디바이스와 expect | pty로 펌웨어 CLI 시뮬레이터(프롬프트, 명령, 지연 응답)를 띄우고 pyserial/pexpect 스타일 콘솔 라이브러리(expect 정규식, 타임아웃, 로그 저장)를 작성해 부트 대기→명령 실행→결과 파싱 시나리오를 자동화한다. | mac |
| INFRA-B05 | 버전·아티팩트 관리: git describe, 매니페스트, SBOM | git describe 기반 version.h 생성, 아티팩트 명명 규칙(제품-보드-버전-해시), SHA-256 체크섬 매니페스트(JSON), CycloneDX/SPDX SBOM 생성 스크립트를 작성하고 CI에서 자동으로 첨부되도록 한다. | mac |
| INFRA-B06 | 로그 파싱과 실패 시그니처 분류 | UART/테스트 로그를 정규식 규칙으로 파싱해 JSON 이벤트로 바꾸고, 알려진 실패 시그니처(HardFault, watchdog reset, assert, timeout) 분류기와 신규 시그니처 클러스터링(정규화 후 해시)을 구현한다. | mac |
| INFRA-B07 | pytest로 디바이스 테스트 작성: fixture, 파라미터화, 마커 | 펌웨어 시뮬레이터 연결을 session fixture로, 보드 종류를 파라미터로, 느린/HIL 테스트를 마커로 구분한 pytest 스위트를 작성하고 JUnit XML·실패 시 콘솔 로그 첨부 리포트를 생성한다. | mac |
| INFRA-B08 | Git 릴리스 워크플로: 브랜치, 태그, 백포트, changelog | 연습 저장소에서 main/release-x.y 브랜치, 서명된 태그, cherry-pick 백포트, Conventional Commits 기반 changelog 자동 생성 스크립트를 실습하고 릴리스 체크리스트를 작성한다. | mac |
| INFRA-B09 | pre-commit 훅과 코드 품질 게이트 | pre-commit으로 clang-format, clang-tidy(변경 파일만), 커밋 메시지 린트, 대용량/비밀 파일 차단 훅을 구성하고 CI에서 같은 설정을 실행하도록 해 로컬/CI 결과 일치를 확인한다. | mac |
| INFRA-M01 | 재현 가능한 빌드: 바이트 단위 동일성 | 타임스탬프·절대 경로·링크 순서·빌드 ID 등 비결정성 요인을 일부러 남긴 펌웨어를 두 환경(호스트, Docker)에서 빌드해 diffoscope로 차이를 찾고, SOURCE_DATE_EPOCH·-ffile-prefix-map·정렬된 입력으로 해시를 일치시킨다. | mac |
| INFRA-M02 | CI 파이프라인 설계: 스테이지, 게이트, 캐시 | build→unit→static analysis→SIL→HIL(조건부) 스테이지를 needs/조건으로 연결한 워크플로를 작성하고, ccache/의존성 캐시로 시간을 줄이며 실패 시 아티팩트·로그 보존과 PR 코멘트 요약을 구현한다. | mac |
| INFRA-M03 | HIL 테스트 팜 아키텍처: 예약·스케줄링 | 보드 인벤토리(타입, 능력 태그, 상태), 락/임대(lease, 타임아웃 회수), 작업 큐 스케줄러를 갖춘 Python 서비스 프로토타입(가짜 보드 프로세스 대상)을 만들고, 보드 고장 시 격리·재시도 정책을 구현한다. | mac |
| INFRA-M04 | 보드 제어 자동화: 전원·리셋·부트 모드·플래싱 | PowerSwitch/ResetLine/BootMode/Flasher 인터페이스와 mock 백엔드(USB 릴레이, PDU, GPIO, OpenOCD 명령 기록)를 구현하고 'power cycle→bootloader 진입→flash→verify→부팅 확인' 복구 시퀀스를 상태머신으로 작성한다. | mac |
| INFRA-M05 | 하드웨어 연결 CI: self-hosted runner와 보안 | self-hosted runner 라벨 체계, 테스트 팜 락과 연동하는 잡 래퍼, 포크 PR에서 HIL 잡 차단·승인 게이트, 러너 격리(컨테이너/임시 작업 디렉터리) 설계를 워크플로와 문서로 작성한다. | mac |
| INFRA-M06 | OTA 서버·클라이언트 기초 | Python 미니 OTA 서버(디바이스 체크인, 업데이트 매니페스트 응답, Range 다운로드)와 클라이언트(버전 비교, 재개 가능한 다운로드, 해시/서명 검증, 설치 결과 보고)를 구현하고 네트워크 끊김 주입 테스트를 한다. | mac |
| INFRA-M07 | 단계적 롤아웃과 자동 중단 | 코호트(내부→1%→10%→100%) 단계와 진행 조건(설치 성공률, 크래시율 임계)을 가진 롤아웃 컨트롤러를 시뮬레이터(가상 디바이스 수천 대, 불량 버전 주입)로 구현해 자동 중단·롤백 결정을 검증한다. | mac |
| INFRA-M08 | 텔레메트리 파이프라인: 수집→저장→알람 | 디바이스 시뮬레이터가 보내는 바이너리 TLV 메트릭을 받는 수집 서버, SQLite 시계열 스키마 저장, 버전별 집계 쿼리와 임계치 알람을 구현하고 수집 실패·중복 전송(idempotency) 처리를 테스트한다. | mac |
| INFRA-M09 | 크래시 리포트 수집과 심볼리케이션 | 디바이스 크래시 덤프(빌드 ID, PC/LR, 스택 주소들) 업로드 API, 빌드 ID→ELF 아카이브 매칭, addr2line/llvm-symbolizer 심볼리케이션, 상위 프레임 기반 중복 버킷팅과 버전별 발생 빈도 리포트를 구현한다. | mac |
| INFRA-M10 | 테스트 결과 분석 대시보드와 flaky 탐지 | 여러 실행의 JUnit XML을 수집해 테스트별 통과율·소요시간 추이를 계산하고, 같은 커밋에서 통과/실패가 섞이는 비율로 flaky 점수를 매겨 격리 후보를 리포트하는 스크립트와 정적 HTML 리포트를 만든다. | mac |
| INFRA-A01 | Yocto/Buildroot CI: sstate·download 캐시와 증분 빌드 | 공유 DL_DIR/SSTATE_DIR(또는 Buildroot BR2_DL_DIR·ccache) 미러 구조, 캐시 키 설계, 캐시 히트율 측정 방법, 야간 풀빌드 vs PR 증분 빌드 정책을 담은 CI 설계서와 워크플로를 작성한다(풀빌드 실행은 가이드만). | **linux** |
| INFRA-A02 | 코드 서명 인프라: 서명 서비스와 키 분리 | HSM을 흉내 낸 mock 서명 서버(키는 서버 내부에만, 정책: 릴리스 브랜치·승인된 잡만, 감사 로그)와 CI에서 해시만 보내 서명을 받아 이미지에 붙이는 클라이언트를 구현하고, 개발/양산 키 분리 정책을 문서화한다. | mac |
| INFRA-A03 | Fleet 관리: 디바이스 레지스트리와 desired/reported 상태 | 디바이스 레지스트리(ID, 그룹, 하드웨어 리비전, 펌웨어 버전)와 device shadow(desired/reported, 버전 번호 기반 충돌 해결) 동기화를 구현하고, 오프라인 디바이스 재접속 시 수렴을 시뮬레이션으로 검증한다. | mac |
| INFRA-A04 | 델타 업데이트: bsdiff·zstd 패치 | 두 펌웨어 버전 사이 bsdiff 패치와 zstd --patch-from 패치를 생성·적용하는 도구를 만들고 크기 절감률, 디바이스 측 필요 RAM/플래시, 패치 적용 중 전원 차단 시 복구 전략(in-place vs A/B)을 비교한다. | mac |
| INFRA-A05 | 생산 라인 테스트 스테이션 소프트웨어 | YAML로 정의된 테스트 시퀀스(측정 항목, 상·하한, 재시도)를 실행하는 스테이션 러너, 시리얼 번호 바인딩, 결과 DB 기록, 수율/Cpk 리포트를 구현하고 mock 디바이스로 불량 주입 시나리오를 돌린다. | mac |
| INFRA-A06 | 관측성: 버전별 회귀 탐지 | 가상 fleet 로그/메트릭에서 펌웨어 버전별 리셋율·에러율을 집계하고 이전 버전 대비 유의한 증가를 포아송/비율 검정으로 탐지해 알람을 내는 분석기를 구현, 표본 크기와 탐지 지연의 trade-off를 분석한다. | mac |
| INFRA-A07 | 종합: 펌웨어 릴리스 파이프라인 엔드투엔드 | 앞선 항목을 엮어 커밋→CI(빌드·테스트·SBOM)→서명→아티팩트 저장→SIL/HIL→staged OTA→텔레메트리 회귀 탐지→자동 롤백까지 동작하는 미니 플랫폼을 구성하고, 장애 대응 런북과 아키텍처 다이어그램을 작성한다. | mac |

### LPERF — 리눅스 성능 · 관측 (Linux Performance & Observability)

리눅스 성능 분석은 '느리다'는 증상을 CPU·메모리·I/O·스케줄러·락 중 어디서 시간이 새는지 증거로 귀속하는 기술로, 임베디드 리눅스 제품과 데이터센터 서버 면접 모두에서 perf·eBPF·flame graph 활용 능력을 직접 묻는다. 이 트랙은 Brendan Gregg의 Systems Performance / BPF Performance Tools 방법론(USE, on/off-CPU, 드릴다운)을 따라 지표의 출처부터 PMU top-down, bpftrace/libbpf 도구 작성, 회귀 감지와 상시 프로파일링까지 직접 구현하며 익힌다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| LPERF-B01 | 60초 성능 점검: uptime·vmstat·mpstat·iostat·sar | stress-ng로 CPU 포화·메모리 압박·디스크 I/O·과다 컨텍스트 스위치 4가지 부하를 하나씩 숨겨 실행하고, uptime→dmesg→vmstat 1→mpstat -P ALL→pidstat→iostat -xz→free -m→sar -n DEV 순서로 점검해 각 시나리오의 원인을 맞히는 판단 노트(어느 열을 보고 무엇을 결론냈는지)를 작성한다. | **linux** |
| LPERF-B02 | USE 방법론 도구: /proc 파싱과 PSI | /proc/stat(usr/sys/iowait/irq/softirq/steal 델타), /proc/loadavg, /proc/pressure/{cpu,memory,io}, /proc/meminfo, /proc/diskstats, /proc/net/dev를 1초 간격으로 읽어 CPU·메모리·디스크·NIC의 U/S/E 표를 출력하는 C 또는 Python 도구를 작성하고, load average가 D 상태 태스크를 포함해 CPU 사용률과 어긋나는 사례를 재현한다. | **linux** |
| LPERF-B03 | perf 시작하기: perf stat과 소프트웨어 이벤트 | perf_event_paranoid·kptr_restrict 설정이 사용자/커널 측정 범위에 주는 영향을 확인한 뒤, 정렬·해시·sleep 위주 세 프로그램을 perf stat -r 10 -e task-clock,context-switches,cpu-migrations,page-faults로 측정해 'CPUs utilized', 변동 계수(±%)의 의미를 설명하고 결과를 /usr/bin/time -v 및 getrusage 값과 대조한다. | **linux** |
| LPERF-B04 | perf_event_open으로 미니 perf stat 구현 | perf_event_open(2)으로 자식 프로세스를 fork/exec해 측정하는 C 도구를 작성하고(PERF_FORMAT_GROUP, inherit, enable_on_exec, exclude_kernel), 이벤트를 하드웨어 카운터 수보다 많이 열어 멀티플렉싱 시 time_enabled/time_running 스케일링을 구현한 뒤 perf stat 출력과 ±1% 이내로 일치하는지 검증한다. | **linux** |
| LPERF-B05 | 샘플링 프로파일링: perf record·report·annotate | 핫스팟이 의도적으로 박힌 C 프로그램을 -O2로 빌드해 perf record -F 99 -g로 프로파일하고, -fomit-frame-pointer 빌드에서 깨진 스택을 --call-graph dwarf로 복구하는 과정과 각 방식의 perf.data 크기·오버헤드를 비교한 뒤, perf annotate로 핫 루프의 명령어 수준 샘플 분포를 해석한다(계측형 프로파일러와의 비교는 see:TOOL-A09). | **linux** |
| LPERF-B06 | Flame Graph 직접 만들기: stack collapse → SVG | perf script 텍스트 출력(B05에서 캡처한 파일 또는 제공 샘플)을 파싱해 'a;b;c count' folded 포맷으로 접는 stackcollapse를 Python으로 구현하고, 알파벳 정렬·너비=샘플 비율 규칙으로 SVG를 생성해 brendangregg/FlameGraph의 flamegraph.pl 결과와 프레임별 샘플 수가 일치하는지 검증한다(icicle/역방향 그래프 옵션 포함). | mac |
| LPERF-B07 | 시스템 콜 관측: strace -c·-T vs perf trace | 작은 write를 반복하는 로거와 버퍼링 로거를 strace -c/-T/-f, perf trace -s, perf stat -e 'syscalls:sys_enter_*'로 각각 분석해 syscall 횟수·시간 분포를 비교하고, ptrace 기반 strace가 대상 프로그램을 몇 배 느리게 만드는지 측정해 운영 환경에서 쓰면 안 되는 이유를 수치로 정리한다(syscall 비용 기초는 see:OS-B03). | **linux** |
| LPERF-B08 | 메모리 사용량 지표: VSZ·RSS·PSS·USS와 page cache | 공유 라이브러리와 MAP_SHARED 매핑을 가진 프로세스 여러 개를 띄워 /proc/<pid>/smaps(_rollup)를 파싱해 RSS/PSS/USS와 Anonymous/File/Shmem/Swap을 계산하는 도구를 작성하고, 합계가 free의 used와 맞지 않는 이유(page cache·slab·커널 메모리)와 'available' 계산 근거를 /proc/meminfo로 설명한다. | **linux** |
| LPERF-B09 | Page fault 프로파일링: minor vs major | 큰 파일을 mmap해 순회하는 프로그램을 drop_caches 전후로 실행해 perf stat -e minor-faults,major-faults 차이를 측정하고, perf record -e page-faults -g로 폴트를 유발하는 코드 경로를 찾은 뒤 MAP_POPULATE·madvise(MADV_WILLNEED/SEQUENTIAL)·prefault 적용 전후를 비교한다(지연 스파이크 관점은 see:LLAT-A05, COW 관찰은 see:OS-M03). | **linux** |
| LPERF-B10 | 스레드 시간 분해: on-CPU·런큐 대기·sleep | CPU 바운드·락 대기·I/O 대기 스레드를 섞은 프로그램을 CPU 수보다 많이 실행하고, /proc/<pid>/task/<tid>/schedstat(실행 ns, 런큐 대기 ns, 타임슬라이스 수)와 status의 voluntary/nonvoluntary_ctxt_switches, stat의 state를 주기적으로 샘플링해 스레드별 시간 분해표를 출력하는 도구를 작성하고 wall time 합과 일치하는지 검증한다. | **linux** |
| LPERF-M01 | PMU 하드웨어 이벤트와 IPC 해석 | 포인터 체이싱(메모리 지연 바운드), 랜덤 분기 루프, 순차 스트리밍, 연산 바운드 4개 마이크로벤치를 perf stat -e cycles,instructions,L1-dcache-load-misses,branch-misses 및 ARMv8 raw 이벤트(r0003 L1D_CACHE_REFILL, r0010 BR_MIS_PRED, r0024 STALL_BACKEND)로 측정해 IPC·MPKI를 계산하고, 예측한 지표 순위와 비교한다(벤치 자체는 see:MEM-B10, ARCH-M03). | **linux** |
| LPERF-M02 | Top-down 분석(TMA) 계산기 | Intel TMA 레벨1(slots=4×cycles, IDQ_UOPS_NOT_DELIVERED, UOPS_ISSUED/RETIRED, recovery cycles)과 Arm Neoverse topdown(STALL_SLOT_FRONTEND/BACKEND, OP_SPEC, OP_RETIRED, BR_MIS_PRED) 공식을 Python으로 구현하고, 제공된 카운터 덤프 6세트(perf stat 출력 텍스트)를 파싱해 레벨1·레벨2(Memory vs Core bound) 분해와 다음 조사 액션을 출력한다; 각 분류가 합 100%가 되는지 검사한다. | mac |
| LPERF-M03 | 심볼 해석 문제 해결: stripped·JIT·컨테이너 | stripped 바이너리(별도 .debug 파일 + build-id), mmap(PROT_EXEC)으로 코드를 생성하는 toy JIT, 다른 mount namespace의 바이너리 세 경우를 perf record로 프로파일하고, perf buildid-cache·--symfs·/tmp/perf-<pid>.map 출력을 각각 적용해 함수명이 복원되는지 확인한다(DWARF/심볼화 기초는 see:TOOL-M14). | **linux** |
| LPERF-M04 | bpftrace 입문: 프로브 종류와 one-liner 10선 | syscall별 프로세스 카운트, read 크기 hist(), vfs_read 지연 histogram(kprobe+kretprobe, @start[tid]), 디스크 I/O 크기, 99Hz 커널 스택 profile, libc malloc 크기 uprobe 등 one-liner 10개를 작성해 출력 해석을 기록하고, 각 프로브 타입의 안정성(tracepoint ABI vs kprobe 내부 함수)과 오버헤드 차이를 정리한다(eBPF 검증기 개념은 see:VM-A07). | **linux** |
| LPERF-M05 | 스케줄러 지연 측정: runqlat 직접 구현 | sched_wakeup/sched_wakeup_new에서 타임스탬프를 저장하고 sched_switch의 next_pid에서 델타를 구하는 bpftrace runqlat 스크립트와 CPU별·cgroup별 버전을 작성해, stress-ng 오버서브스크립션 하에서 nice·SCHED_FIFO(chrt)·CPU 피닝 적용 전후의 분포를 perf sched latency 결과와 대조한다(격리 튜닝은 see:LLAT-M07, RT 커널은 see:ELX-A04). | **linux** |
| LPERF-M06 | Off-CPU 분석: 블로킹 시간 flame graph와 wakeup 체인 | 뮤텍스 경합·fsync·네트워크 대기·sleep이 섞인 서비스를 sched_switch(prev 상태·스택)로 off-CPU 시간을 스택별 합산하는 bpftrace/perf 스크립트로 분석해 off-CPU flame graph를 만들고, sched_wakeup의 waker 스택을 결합한 'offwake' 체인으로 누가 깨웠는지까지 추적해 on-CPU flame graph만으로는 놓친 병목을 보고서로 정리한다. | **linux** |
| LPERF-M07 | 락 경합 프로파일링: futex 대기와 holder 찾기 | 공유 맵을 전역 mutex로 보호하는 멀티스레드 서버를 만들고 sys_enter/exit_futex(FUTEX_WAIT) 지연을 uaddr·ustack별 hist로 집계하는 bpftrace 스크립트와 perf lock contention(-b) 결과를 비교해 핫 락을 특정한 뒤, 락 분할(striping)·RCU 스타일 읽기 경로로 개선해 전후 p99와 대기 시간을 비교한다(futex 구현은 see:OS-A01). | **linux** |
| LPERF-M08 | libbpf CO-RE 도구 작성: ring buffer와 skeleton | libbpf-bootstrap 구조로 vmlinux.h + BPF_CORE_READ를 쓰는 execsnoop/opensnoop 계열 도구(프로세스 생성·파일 open 이벤트, 지연 필터, PID 필터 인자)를 작성해 BPF ring buffer로 사용자 공간에 이벤트를 전달하고, perf buffer 대비 이벤트 유실·CPU 오버헤드를 부하 하에서 비교한다(XDP/tc 네트워크 eBPF는 see:NET). | **linux** |
| LPERF-M09 | 블록 I/O 지연: iostat 해석과 biolatency | fio로 QD1/QD8/QD32 랜덤 읽기·순차 쓰기 작업을 돌리며 iostat -x의 r_await·aqu-sz·%util을 기록해 Little 법칙(aqu-sz ≈ IOPS × await)을 검증하고, block_rq_issue→block_rq_complete 지연 bpftrace histogram(biolatency)과 비교해 %util 100%가 병렬 장치(NVMe)에서 포화를 뜻하지 않는 이유를 설명한다. | **linux** |
| LPERF-M10 | 메모리 할당 프로파일링: 핫스팟과 누수 스택 | malloc/free/calloc/realloc uprobe로 미해제 할당을 주소→(크기, 스택)으로 추적하는 bpftrace(또는 BCC memleak 스타일) 도구를 작성해 점진적 누수가 있는 데몬에서 누수 스택 Top-N을 찾고, 할당 핫스팟은 할당 횟수 flame graph로 시각화해 heaptrack/valgrind massif 결과와 탐지 범위·오버헤드를 비교한다(맥 쪽 누수 도구는 see:TOOL-M10). | **linux** |
| LPERF-M11 | 메모리 압박 관측: reclaim·PSI·OOM in cgroup v2 | cgroup v2에 memory.high/memory.max를 걸고 작업 집합이 한도를 넘는 워크로드를 실행해 memory.pressure(PSI some/full), memory.stat(pgscan, pgsteal, workingset_refault), vmstat의 kswapd 활동, 지연 분포를 함께 기록하고, OOM kill 로그(oom_score, memory.events)를 해석해 high와 max 설정 차이를 정리한다(컨테이너 격리 자체는 see:VIRT). | **linux** |
| LPERF-M12 | CPU 스로틀링과 꼬리 지연: cpu.max·nr_throttled | cgroup v2 cpu.max(예: 50ms/100ms)를 건 멀티스레드 요청 처리기에 버스트 부하를 넣어 cpu.stat의 nr_throttled·throttled_usec와 요청 지연 p50/p99를 함께 측정하고, 스레드 수·period 길이·cpu.max.burst 변화가 스로틀링과 꼬리 지연에 주는 영향을 표로 정리한다. | **linux** |
| LPERF-A01 | OS 노이즈 측정: osnoise·timerlat tracer와 IRQ 시간 | ftrace osnoise/timerlat tracer와 rtla osnoise top·rtla timerlat hist로 CPU별 노이즈 총량과 최대 지연을 측정하고, /proc/softirqs와 irq:softirq_entry/exit bpftrace 집계로 노이즈 원인을 분해한 뒤 IRQ affinity·nohz_full 적용 전후를 비교한다(ftrace 기초는 see:ELX-A01, 격리 설정은 see:LLAT-M07). | **linux** |
| LPERF-A02 | USDT·uprobe로 애플리케이션 정적 트레이싱 | C 서비스에 <sys/sdt.h> DTRACE_PROBE로 요청 시작/종료 USDT 프로브와 semaphore를 넣고 bpftrace usdt:로 요청 지연 hist·느린 요청 덤프를 구현한 뒤, 프로브 비활성/USDT 활성/함수 uprobe 활성 각각의 호출당 오버헤드(ns)를 측정해 상시 트레이싱 가능 여부를 판단한다. | **linux** |
| LPERF-A03 | perf c2c로 false sharing 찾기 | false sharing이 숨은 멀티스레드 통계 카운터 프로그램을 perf c2c record/report로 분석해 공유 캐시 라인, 오프셋별 접근 코드, HITM 비율을 읽어내고, 패딩/스레드 로컬 집계로 수정한 뒤 처리량과 c2c 결과 변화를 비교한다(false sharing 원리 실측은 see:ARCH-M08). | **linux** |
| LPERF-A04 | 임베디드 타깃 프로파일링: 크로스 perf와 오프라인 심볼화 | QEMU aarch64 virt 타깃(/var/lab)에 정적 빌드 perf와 stripped 애플리케이션을 올려 cpu-clock 샘플링과 trace-cmd(작은 버퍼, 이벤트 필터)로 기록하고, perf.data·trace.dat를 호스트로 가져와 --symfs·buildid-cache·debug 심볼 트리로 해석하며 타깃 측 CPU/메모리 오버헤드 예산을 표로 정리한다(QEMU 랩 구성은 see:ELX-B04). | **linux** |
| LPERF-A05 | CI 성능 회귀 감지: instructions 카운트와 차등 flame graph | 벤치를 wall time과 perf stat -e instructions:u(또는 cachegrind Ir)로 동시에 측정하는 CI 스크립트를 만들어 지표별 변동 계수를 비교하고, 0.5%/2% 회귀를 주입한 커밋에서 기준 빌드와의 folded 스택 차이를 빨강/파랑 차등 flame graph(difffolded)로 생성해 회귀 함수를 지목한다(통계 검정은 see:AIPERF-A06, bisect는 see:TOOL-M08, fleet 회귀는 see:INFRA-A06). | **linux** |
| LPERF-A06 | Continuous profiling 설계: fleet 상시 프로파일러 | 저빈도(19Hz) 샘플링 에이전트(perf 또는 libbpf profile)가 주기적으로 build-id 포함 folded 스택을 업로드하고, 서버가 오프라인 심볼화·버전/서비스별 집계·기간 비교 쿼리를 제공하는 구조(GWP·Parca·Pyroscope 참고)를 설계하고, 에이전트와 집계 서버의 최소 구현으로 오버헤드·저장량(샘플/일)을 계산해 검증한다. | **linux** |
| LPERF-A07 | 종합: 꼬리 지연 미스터리 사후 분석 | 요청 처리 서버에 락 경합, 주기적 fsync, cgroup CPU 스로틀링, 메모리 압박에 의한 reclaim 중 3가지를 무작위로 숨기는 주입 스크립트를 만들고, USE 점검 → on/off-CPU flame graph → runqlat·biolatency·PSI 순으로 원인을 찾아 p99 개선 전후 수치와 함께 1페이지 포스트모템(타임라인·증거·조치)을 작성한다. | **linux** |

## 5단계 · 응용 기술 (Applied Engineering)

### SIV — 실리콘 검증 (Silicon Validation)

실리콘이 처음 돌아온 날(first silicon)부터 양산까지, 칩이 스펙대로 동작하는지 C 펌웨어와 스크립트로 증명하는 기술이다. Rivian Silicon & Sensing, NVIDIA/Apple/Qualcomm의 post-silicon validation 직무는 레지스터 테스트 프레임워크, 링크 검증 플로우, shmoo/PVT, triage 능력을 직접 묻는다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| SIV-B01 | 레지스터 기술서 파싱 → C 헤더 자동 생성 | IP 블록 하나(10~20개 레지스터, 필드·access·reset 포함)를 JSON으로 기술하고 Python으로 C 헤더(#define OFFSET/MASK/SHIFT/RESET)를 생성; 필드 겹침·폭 초과를 검사하는 lint도 추가해 잘못된 기술서를 거부하는지 테스트한다. | mac |
| SIV-B02 | Mock MMIO 레지스터 파일과 접근 계층 | BM-B02 mock 주변장치 프레임워크 위에 access type(RW/RO/WO/W1C/W1S/RC) 시맨틱을 더한 레지스터 파일을 구현하고, 같은 테스트 코드가 함수 포인터 bus ops만 바꿔 실제 MMIO에도 붙도록 설계; 각 access type별 단위테스트로 동작을 검증한다. | mac |
| SIV-B03 | 리셋값 테스트 (Reset Value Test) | B01 기술서를 읽어 모든 레지스터의 리셋값을 읽고 (mask 적용, undefined/volatile 비트 제외) 기대값과 비교하는 C 테스트를 생성; mock에 리셋값 오류 2개를 주입해 레지스터·비트 단위 불일치 리포트가 나오는지 확인한다. | mac |
| SIV-B04 | Read/Write 테스트와 Access Type별 기대값 | 각 필드의 access type을 반영해 write 후 기대 read값을 계산하는 함수를 작성하고, 0x0/0xFFFFFFFF/0x5555/0xAAAA 패턴으로 R/W 테스트를 수행; RO 비트에 write가 먹는 결함과 W1C가 안 지워지는 결함을 mock에 주입해 검출한다. | mac |
| SIV-B05 | Walking 1s / Walking 0s 비트 테스트 | 32비트 RW 레지스터와 데이터 버스에 walking-1/walking-0 테스트를 구현하고, stuck-at-0, stuck-at-1, 인접 비트 short(OR/AND 결합) 결함 모델을 주입해 각 패턴이 어떤 결함을 잡는지 표로 정리한다. | mac |
| SIV-B06 | 주소 디코드·Aliasing 테스트 | 모든 RW 레지스터에 고유 패턴(예: 주소 해시)을 쓴 뒤 전체를 다시 읽어 덮어쓰기를 검사하는 테스트와 walking-address(2^n 오프셋) 테스트를 작성; 주소 비트 하나가 무시되는 mock 결함을 넣어 검출·원인 비트 추정까지 출력한다. | mac |
| SIV-B07 | SRAM March 테스트 (March C-) | March C- (⇑w0; ⇑r0w1; ⇑r1w0; ⇓r0w1; ⇓r1w0; ⇑r0)를 C로 구현하고 stuck-at, transition, coupling fault를 주입한 SRAM 모델에서 검출률을 측정; MATS+ 대비 시간 복잡도와 커버리지를 비교한다. | mac |
| SIV-B08 | 검증 테스트 프레임워크 골격 | TEST() 매크로 기반 자동 등록(linker section 또는 constructor), CHECK_EQ/CHECK_MASK 매크로, 이름 필터 실행, JSON 결과 로그(test, status, duration, failing reg/expected/actual)를 출력하는 프레임워크를 작성하고 B03~B07 테스트를 이식한다. | mac |
| SIV-B09 | Poll-with-Timeout 헬퍼와 에러 처리 | wait_for_field(reg, mask, value, timeout_us)를 구현해 타임아웃 시 마지막 읽은 값·경과시간·호출 위치를 기록; mock에서 N회 뒤 ready가 되는 경우/영원히 안 되는 경우/중간에 에러 비트가 서는 경우를 테스트한다. | mac |
| SIV-B10 | IP 블록 Bring-up 시퀀스 | 가상의 UART IP에 대해 clock gate/reset controller/IP 레지스터를 mock으로 만들고, 단계별 체크포인트와 실패 시 원인 메시지(클럭 미인가, 리셋 미해제, ID 불일치)를 내는 bring-up 함수를 작성; 각 단계 결함 주입으로 진단이 정확한지 확인한다. | mac |
| SIV-B11 | 인터럽트 검증 기초: Trigger-Check-Clear | mock 인터럽트 컨트롤러+IP에서 각 인터럽트 소스를 강제 트리거(force/test 레지스터)하고 raw status, mask 적용 후 status, IRQ 라인, clear 후 해제를 확인하는 테스트 매트릭스를 생성; mask가 무시되는 결함을 주입해 검출한다. | mac |
| SIV-B12 | 테스트 플랜 작성: Feature → Test Case → Pass Criteria | I2C 컨트롤러 IP 스펙(1페이지 요약)을 보고 feature list, 테스트 케이스(정상/경계/에러/스트레스), 필요한 장비·펌웨어, pass/fail 기준, 우선순위(P0/P1)를 담은 테스트 플랜 표를 작성하고, 그중 3개를 mock 위에서 실제 코드로 구현한다. | mac |
| SIV-M01 | ATE vs SLT vs 시스템 레벨 검증의 역할 | 20개 결함 시나리오(stuck-at gate, 타이밍 마진, PCIe 링크 간헐 실패, 열 의존 SRAM 오류, 펌웨어 errata 등)를 단계별로 분류하는 표를 작성하고, 테스트 시간·비용·커버리지 trade-off를 근거와 함께 설명하는 1페이지 설계 메모를 쓴다. | mac |
| SIV-M02 | DMA 검증: 디스크립터 체인과 데이터 무결성 | scatter-gather 디스크립터 체인을 처리하는 DMA 모델을 만들고 정렬/비정렬, 4KB 경계 교차, 길이 0/1/max, 체인 중간 에러 디스크립터를 랜덤+경계로 생성해 CRC32로 src/dst 비교; completion 인터럽트와 status 레지스터 일관성까지 확인한다. | mac |
| SIV-M03 | 인터럽트 지연·유실 검출 | 타이머 소스로 주기적 인터럽트를 발생시키는 이벤트 시뮬레이션에서 트리거 시각과 ISR 진입 시각을 기록해 latency 히스토그램을 만들고, 시퀀스 번호로 유실·중복·순서 뒤바뀜을 검출; 핸들러가 늦게 clear해 인터럽트가 합쳐지는 경우를 재현한다. | mac |
| SIV-M04 | PCIe 링크 검증 플로우 | PCIe config space 덤프(lspci -xxx 형식)를 파싱해 capability 체인을 따라 Link Capabilities/Status를 해석하고 기대 Gen/xN과 비교하는 checker를 작성; mock LTSSM 레지스터로 'Detect→Polling→Config→L0' 진행 감시, retrain 반복 N회 스트레스, 다운그레이드 검출을 구현한다. | mac |
| SIV-M05 | DDR 검증: Training 결과와 메모리 스트레스 | training 결과 레지스터(per-byte-lane read/write DQS delay window)를 모델링해 eye 폭이 임계 미만인 lane을 경고하고, LFSR 의사난수 주소/데이터·row hammer 유사 접근·checkerboard 패턴 스트레스를 대용량 버퍼에 수행해 비트 단위 오류 맵(lane/bit/address)을 출력한다. | mac |
| SIV-M06 | SerDes PRBS 생성·검사와 BER 신뢰구간 | PRBS7/15/31 generator와 self-synchronizing checker를 C로 구현하고 랜덤 비트 오류를 주입해 BER을 측정; 'BER<1e-12를 95% 신뢰도로 주장하려면 오류 0개에서 약 3e12 비트 필요'를 유도하고 링크 속도별 필요 측정 시간을 계산하는 도구를 만든다. | mac |
| SIV-M07 | Shmoo Plot 생성과 마진 분석 | 온도·공정 파라미터를 가진 pass/fail 모델(지연 = f(V,T))에 대해 V(0.65~1.0V)×F 2차원 sweep을 자동화하고 ASCII/CSV shmoo 출력, 최소 동작 전압(Vmin) 탐색은 이진 탐색으로 최적화; 간헐 실패를 반복 측정으로 구분하는 로직도 넣는다. | mac |
| SIV-M08 | PVT 코너 매트릭스와 테스트 시간 예산 | SS/TT/FF 칩 × 3전압 × 3온도 × 테스트 스위트(각 소요시간) 매트릭스를 입력받아 챔버 온도 변경 비용을 최소화하는 실행 순서와 총 소요시간을 계산하는 Python 스케줄러를 작성하고, 예산 초과 시 우선순위 기반으로 축소된 계획을 제안한다. | mac |
| SIV-M09 | 동시성 스트레스 테스트 설계 | pthread로 CPU 메모리 체커, DMA 모델, 인터럽트 소스를 동시에 돌리는 host 스트레스 하네스를 만들고 각 트래픽에 독립적인 checker(시그니처/CRC)를 붙여 장시간 실행; 공유 자원 경합 버그(일부러 넣은 race)를 몇 분 안에 잡아내는지 확인한다. | mac |
| SIV-M10 | 실패 로그 Triage와 시그니처 클러스터링 | 야간 회귀 결과(JSON 로그 수천 건, 합성 생성)를 파싱해 실패 시그니처(테스트명+레지스터+비트 패턴+에러코드 정규화)로 클러스터링하고, 칩/보드/온도/펌웨어 버전별 상관을 표로 출력해 '특정 보드에만'과 '특정 온도 이상에서만'을 자동으로 구분한다. | mac |
| SIV-M11 | Bisect 자동화: 펌웨어 커밋과 파라미터 | git bisect run으로 회귀 커밋을 찾는 스크립트(테스트 flaky 대비 3회 재시도 다수결 포함)와, 실패 경계(전압·주파수·버퍼 크기)를 bisect하는 일반화 함수를 작성; flaky 테스트에서 단순 bisect가 오답을 내는 사례를 재현하고 개선책을 비교한다. | mac |
| SIV-M12 | Python 테스트 시퀀서 | YAML 테스트 리스트(테스트명, 파라미터, 타임아웃, 재시도, 사전조건)를 읽어 C 테스트 바이너리(또는 ctypes로 로드한 .dylib)를 실행하고, 전원/온도 장비는 mock 클래스로 추상화해 setup/teardown 시퀀스·결과 DB(SQLite) 저장·요약 리포트를 생성한다. | mac |
| SIV-M13 | 검증 커버리지 모델 | 레지스터 필드값·DMA 길이 구간·인터럽트 조합을 coverpoint/bin/cross로 정의하는 Python 커버리지 모델을 만들고 시퀀서 실행 로그로 샘플링해 hole 리포트를 출력; 랜덤 테스트 시드를 늘려가며 커버리지 수렴 곡선을 그린다. | mac |
| SIV-A01 | Constrained-Random 테스트와 Scoreboard | 레지스터 쓰기/DMA 요청/인터럽트 이벤트를 제약 조건 하에 랜덤 생성하는 generator, 같은 입력을 받는 골든 모델, DUT 모델 출력을 비교하는 scoreboard를 구현; DUT 모델에 미묘한 버그(특정 순서에서만 status 갱신 누락)를 넣고 시드 재현성으로 디버그한다. | mac |
| SIV-A02 | 바운더리 스캔(EXTEST)으로 보드 연결 테스트 | BSDL 일부(핀↔셀 매핑)와 넷리스트를 입력받아 EXTEST 패턴(walking-1, 카운팅 시퀀스)을 생성하고, open/short/stuck 결함을 심은 가상 보드의 응답을 비교해 결함 넷을 진단하는 도구를 작성한다(TAP 상태기계는 see:TOOL-A01). | mac |
| SIV-A03 | 전력 특성화 자동화 | INA226 유사 전력 모니터를 mock I2C로 읽는 드라이버(shunt/bus/power 레지스터 스케일링)를 만들고, 워크로드×온도 sweep 데이터에서 dynamic/leakage 전력을 분리(P = Pleak(T) + C·V²·f 회귀)하는 분석 스크립트를 작성한다. | mac |
| SIV-A04 | 열 특성화와 Throttling 검증 | SoC 열 RC 모델(see:SIM)에 워크로드를 넣어 온도 상승을 시뮬레이션하고 thermal trip/throttle 임계·hysteresis·DVFS 단계 전환을 검증하는 테스트를 작성; 센서 오프셋 오류가 있을 때 throttle 지연이 얼마나 위험한지 정량화한다. | mac |
| SIV-A05 | Errata 관리와 칩 리비전별 워크어라운드 | chip ID/revision 레지스터를 읽어 errata 테이블(ID, 영향 리비전, 워크어라운드 함수, 검증 테스트)을 적용하는 프레임워크를 C로 구현하고, A0/B0 mock에서 각각 올바른 워크어라운드만 적용되는지·ECO 후 제거 가능한지 테스트한다. | mac |
| SIV-A06 | 보드 레벨 검증: 전원 시퀀싱과 클럭 | 레일별 ADC 샘플 로그(타임스탬프 포함)를 입력받아 전원 시퀀싱 순서·ramp time·허용오차(±5%)·glitch를 검사하는 checker와, 주파수 카운터 측정값으로 PLL/클럭 트리 출력 ppm 오차를 판정하는 테스트를 작성한다. | mac |
| SIV-A07 | 버스 인터페이스 실패 디버그 (LA 캡처 분석) | 로직 애널라이저 CSV(SCL/SDA 샘플)를 디코딩하는 I2C 프로토콜 파서를 작성해 NACK·clock stretching 타임아웃·setup/hold 위반·arbitration lost를 자동 표시하고, 합성 캡처 3종(풀업 약함, 타이밍 위반, 슬레이브 hang)의 원인을 리포트한다. | mac |
| SIV-A08 | 생산 연계: 수율·Binning 분석 | 웨이퍼별 다이 테스트 결과(합성: Vmin, Fmax, 누설, pass/fail 코드)로 수율·bin 분포·wafer map을 계산하고, 테스트 항목 순서를 'fail 확률/테스트 시간' 기준으로 재정렬해 평균 테스트 시간을 줄이는 최적화를 구현한다. | mac |
| SIV-A09 | 검증 캠페인 설계: 신규 센싱 SoC (면접형) | 카메라(MIPI CSI-2)·레이더(SerDes)·PCIe·DDR를 가진 가상 센싱 SoC에 대해 검증 단계(pre-si 준비, Day-0 bring-up, feature, PVT, stress, 양산 이관), 인프라(시퀀서, 랩 자동화, 대시보드), triage 플로우, 일정·인력·지표를 담은 설계 문서를 쓰고 Day-0 bring-up 스크립트 골격을 구현한다. | mac |

### HWLAB — 보드 브링업 · 계측 (Board Bring-up & Measurement)

보드 브링업과 계측은 실리콘·보드 검증 면접(Rivian, Apple, NVIDIA 등)에서 '스코프로 무엇을 어떻게 봤고 그 수치가 무엇을 의미하는가'를 묻는 영역이다. 회로도·데이터시트 읽기에서 출발해 스코프/LA 측정 원리, 전원·신호 무결성, eye diagram·지터 분해·BER bathtub까지를 합성 또는 실측 캡처(CSV/VCD/sigrok)를 직접 분석하는 코드로 다루고, 실제 장비가 필요한 항목은 최소한의 저가 장비로 한정한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| HWLAB-B01 | 회로도 넷리스트 감사: 레일·풀업·테스트 포인트 (Netlist Audit) | KiCad 넷리스트(.net S-expression, 예제 보드 export 또는 합성)를 파싱해 전원 레일별 연결 부품·테스트포인트 유무, I2C/RESET/strap 넷의 풀업·풀다운 존재와 값, 단일 핀(미연결) 넷을 리포트하는 Python 감사 도구를 작성하고, 풀업 누락·TP 없는 레일을 심은 넷리스트로 검출되는지 확인한다. | mac |
| HWLAB-B02 | 데이터시트 로직 레벨: 노이즈 마진과 전압 도메인 호환성 | 부품 JSON(VDDIO, VIH/VIL, VOH/VOL@IOH/IOL, 절대최대정격, 5V tolerant, ESD 다이오드 유무)으로 드라이버→리시버 쌍의 High/Low DC 노이즈 마진을 계산하고 1.8V↔3.3V 직결·open-drain 레벨시프트·전원 꺼진 칩으로의 back-powering 위험을 판정하는 검사기를 작성해 6개 인터페이스 조합 표로 검증한다. | mac |
| HWLAB-B03 | I2C 풀업 저항 계산: 버스 커패시턴스·Rise Time | Sm/Fm/Fm+ 스펙(tr max, VOL=0.4V@3mA/20mA, Cb max)으로 Rp(min)=(VDD−VOL)/IOL, Rp(max)=tr/(0.8473·Cb)를 계산하는 함수와 RC 에지 합성 파형에서 30–70% rise time을 측정하는 코드를 작성해, Cb 50~400pF에서 허용 Rp 창이 닫히는 지점을 찾는다(디코드 측 증상은 see:SIV-A07, 프로토콜은 see:COMM-B06). | mac |
| HWLAB-B04 | 파형 기본 측정 알고리즘: 상태 레벨·Rise/Fall·Overshoot (IEEE 181) | 스코프 CSV(time,volt)에서 히스토그램 mode로 top/base 상태 레벨을 구하고 10–90% rise/fall time(선형 보간 교차), overshoot/undershoot %, 주파수·duty·펄스폭을 계산하는 측정 라이브러리를 작성해, 링잉·잡음을 넣은 합성 펄스에서 min/max 기반 순진한 방법과 오차를 비교한다. | mac |
| HWLAB-B05 | 스코프 대역폭·프로브 부하·그라운드 리드 링잉 | tr_meas≈√(tr_sig²+tr_scope²+tr_probe²)와 BW≈0.35/tr로 1ns 에지를 3% 오차 이내로 보기 위한 대역폭을 계산하고, 10x 프로브(10MΩ‖~12pF) + 15cm 그라운드 리드(~150nH) RLC 모델을 시뮬레이션해 링잉 주파수 1/(2π√LC)와 overshoot가 스프링 팁(~10nH)에서 얼마나 줄어드는지 B04 도구로 측정한다. | mac |
| HWLAB-B06 | 트리거 로직 구현: Edge·Pulse-width·Runt·Timeout | 히스테리시스 edge 트리거, pulse-width(<,>), runt(두 임계 사이에서 되돌아오는 펄스), timeout 트리거를 샘플 스트림 상태기계로 구현하고, 10M 샘플 합성 캡처에 심어 둔 드문 runt/글리치 3개를 찾아 pre/post-trigger 윈도우로 잘라 저장한다. | mac |
| HWLAB-B07 | LA 캡처 포맷과 샘플링률: sigrok .sr·CSV·VCD | sigrok .sr(zip: metadata + logic-1-N 청크)·Saleae식 CSV·VCD를 공통 transition 리스트로 읽는 로더를 작성하고(VCD 파서는 see:EMU-B07 재사용), 10MHz SPI를 20/50/100MS/s로 재샘플링했을 때 에지 위치 오차(±1 샘플)와 디코드 실패가 생기는 경계를 실험한다; sigrok-cli 데모 .sr과 왕복 비교한다. | mac |
| HWLAB-B08 | 전원 레일 측정: DC 정확도·리플·20MHz BW Limit | 벅 출력 합성 캡처(DC + 스위칭 리플 + 고주파 스위칭 스파이크 + 잡음)에서 Vavg·pk-pk·RMS 리플을 계산하고, 20MHz BW limit을 Butterworth 필터로 흉내내 스파이크가 리플 수치를 얼마나 부풀리는지 비교한 뒤 레일 스펙(±3%, 30mVpp) pass/fail 리포트를 출력한다(시퀀싱 검사는 see:SIV-A06). | mac |
| HWLAB-B09 | 첫 전원 인가: 단락 검사·Golden 보드 비교·Inrush | 레일별 GND 저항 측정표(DUT 1장 + golden 5장)에서 golden 분포 대비 비율/z-score로 단락·오픈 의심 레일을 표시하고, 전류 제한 벤치 전원 투입 합성 캡처에서 inrush 피크·지속시간·정상 전류를 측정해 전류 제한 설정값을 추천하는 smoke-test 스크립트를 작성한다(펌웨어 bring-up 순서는 see:PLAT-B09). | mac |
| HWLAB-B10 | 열 측정 기초: 열전대 냉접점 보상과 Tj 추정 | NIST ITS-90 K형 다항식으로 mV↔°C 변환과 냉접점 보상(CJC)을 구현해 CJC 생략 시 오차를 보이고, 케이스 온도·전력으로 Tj=Tc+P·ΨJT와 Tj=Ta+P·θJA 추정치를 비교해 실제 보드에서 θJA를 쓰면 왜 틀리는지 수치로 설명한다(throttling 검증은 see:SIV-A04). | mac |
| HWLAB-M01 | 전송선로 반사: Bounce Diagram·TDR 해석·종단 | 반사계수 ρ=(ZL−Z0)/(ZL+Z0) 기반 lattice(bounce) 시뮬레이터로 스텝 응답을 만들고, 합성 TDR 파형에서 불연속 위치(거리=v·t/2, v=c/√εeff)와 임피던스를 역산하는 해석기를 작성한 뒤, 15Ω 드라이버+50Ω 트레이스에서 직렬 종단 R을 sweep해 overshoot 최소점을 찾고 병렬/Thevenin/AC 종단의 전력·지연 trade-off를 표로 정리한다. | mac |
| HWLAB-M02 | Load Transient 응답: Droop·Recovery·위상 마진 추정 | 2차 시스템으로 모델링한 레귤레이터에 부하 스텝(0.5→3A, 1A/µs)을 넣은 합성 캡처에서 droop 크기, 허용 밴드 복귀 시간, 링잉 주기를 측정하고, 로그 감쇠율→감쇠비 ζ→위상 마진 근사(PM≈100·ζ)를 추정해 보상 변경 전후 두 캡처 중 어느 쪽이 안정한지 판정한다. | mac |
| HWLAB-M03 | PDN 타겟 임피던스와 디커플링 커패시터 선정 | Ztarget=V·(허용 리플%)/ΔI를 계산하고, 커패시터(C, ESR, ESL) 병렬 네트워크의 |Z(f)|를 10kHz–1GHz에서 계산해 반공진 피크를 찾는 Python 도구를 작성한 뒤, 1/10/100µF 조합과 개수를 탐색해 목표 대역에서 Ztarget 이하가 되는 최소 BOM을 제시한다. | mac |
| HWLAB-M04 | 저전력 전류 프로파일 측정: 다이내믹 레인지·Burden 전압 | sleep 2µA ↔ active 15mA 버스트가 섞인 MCU 전류 파형을 합성해 샘플링률(1k/10k/100kS/s)·ADC 분해능·자동 레인지 전환 지연·션트 burden 전압(공급 전압 강하로 인한 brown-out)이 평균 전류·배터리 수명 추정에 주는 오차를 계산하고, 필요한 측정 장비 사양(PPK2/Joulescope급)을 도출한다(배터리 수명 모델은 see:RF-A04). | mac |
| HWLAB-M05 | LA 캡처 타이밍 컴플라이언스: SPI·UART vs 데이터시트 | COMM-A10 디코더 위에 SPI tCSS/tCSH/tSU/tH·SCLK 주파수·duty와 UART 실제 baud(비트 폭 히스토그램)·inter-byte gap 측정을 추가해 데이터시트 min/max 표(JSON)와 비교한 위반 리포트를 내고, 샘플링 주기에 의한 측정 불확도(±1/fs)를 pass/fail 판정에 반영한다(see:COMM-A10). | mac |
| HWLAB-M06 | 커스텀 sigrok 프로토콜 디코더 작성 (libsigrokdecode) | WS2812(또는 가상의 1-wire 벤더 프로토콜) 디코더를 libsigrokdecode Python API(Decoder 클래스, wait() 조건, put annotation)로 작성해 합성 .sr에 sigrok-cli -P로 실행하고 B07 로더 기반 독립 구현과 출력이 일치하는지 검증하며, I2C 디코더 위에 쌓는 센서 레지스터 스택 디코더도 하나 추가한다. | mac |
| HWLAB-M07 | 클럭 지터 측정: Period·Cycle-to-Cycle·TIE | 합성 클럭 캡처(RJ + 주기성 지터 + SSC)에서 보간 임계 교차로 에지 타임스탬프를 뽑아 period jitter, cycle-to-cycle jitter, TIE(최소제곱 주파수 추정 이상 클럭 대비)를 계산하고, c2c=period의 차분·period=TIE의 차분 관계와 SSC가 TIE를 지배하는 현상을 확인한다(PLL 추적은 see:DSP-A06, ppm 판정은 see:SIV-A06). | mac |
| HWLAB-M08 | 위상 잡음 → RMS 지터 적분과 레퍼런스 클럭 판정 | 위상잡음 표 L(f)[dBc/Hz]를 로그 보간해 12kHz–20MHz 구간 적분으로 RMS 위상 지터 σ=√(2∫10^(L/10)df)/(2πf0)를 계산하고, PCIe refclk식 필터(CDR/PLL 전달함수)를 적용했을 때 수치 변화를 보여 두 후보 오실레이터 중 스펙을 만족하는 쪽을 고른다. | mac |
| HWLAB-M09 | Eye Diagram 생성: CDR 기준 폴딩·Eye Height/Width·마스크 | PRBS7 NRZ 신호에 채널 ISI(FIR)·잡음·지터를 넣은 긴 캡처를 만들고, golden PLL(1차, BW=bitrate/1667) 클럭 복원으로 UI 단위 폴딩해 2D 히스토그램 eye를 그린 뒤 eye height/width·crossing %·폴리곤 마스크 hit 수를 계산하고, 이상 클럭 vs golden PLL 폴딩 결과 차이를 비교한다(DDR DQ eye 트레이닝은 see:MEM-A04). | mac |
| HWLAB-M10 | 지터 분해: RJ/DJ(DDJ·PJ)와 Dual-Dirac TJ@BER | M09 캡처의 TIE에서 반복 패턴 평균으로 DDJ, TIE 스펙트럼 FFT 피크로 PJ, 잔차 RMS로 RJ를 분리하고, 히스토그램 꼬리 가우시안 피팅으로 RJ·DJ(δδ)를 추정해 TJ(1e-12)=DJ(δδ)+2·7.034·RJ를 계산한 뒤 주입한 정답 성분과 비교해 분해 오차를 보고한다. | mac |
| HWLAB-M11 | BER Bathtub과 Q-scale 외삽 | 샘플링 위상을 UI 전체에 sweep하며 BER을 계측(오류 카운트는 see:SIV-M06 PRBS checker 재사용)해 bathtub을 만들고, 1e-6까지만 측정한 데이터를 Q-scale 선형 피팅으로 1e-12 eye opening까지 외삽해 M10 dual-Dirac 예측·장시간 직접 측정과 비교하며 측정 시간·신뢰도 trade-off를 표로 정리한다. | mac |
| HWLAB-M12 | 채널 손실·ISI와 등화: TX FFE·CTLE·DFE | Nyquist에서 −15dB급 손실을 갖는 채널을 FIR로 모델링해 pulse response의 pre/post-cursor를 측정하고, 3-tap TX FFE(de-emphasis), 1-zero/2-pole CTLE, 1~3-tap DFE를 구현해 단계별 eye height/width 개선(M09 도구)과 DFE 오류 전파를 측정한다(PAM4·FEC는 see:NETHW). | mac |
| HWLAB-A01 | CDR 구현과 Jitter Tolerance(JTOL)·Jitter Transfer | 오버샘플 파형에 대한 bang-bang(Alexander) 위상검출기 + 2차 디지털 루프 CDR을 구현하고, 사인 지터 주파수·진폭을 sweep해 BER 임계에서의 JTOL 곡선을 그려 스펙 마스크(저주파 수 UI ~ 고주파 0.1UI 플로어)와 비교하며, 루프 대역폭 변경이 JTOL과 jitter transfer 피킹에 주는 영향을 설명한다. | mac |
| HWLAB-A02 | In-system Lane Margining: 2D Eye 등고선과 마진 판정 | PCIe Gen4+ Lane Margining at Receiver처럼 timing offset × voltage offset 단계별 오류 카운트 로그(합성: 레인 16개 × 온도 3개)를 파싱해 레인별 2D eye 등고선과 마진(L/R 타이밍 UI, U/D mV)을 계산하고, 스펙 최소 마진 미달·레인 간 이상치·온도에 따른 마진 감소를 자동 플래그하는 리포트를 작성한다(see:PCIE, see:SIV-M05). | mac |
| HWLAB-A03 | 아날로그+디지털 캡처 상관: 레일 Droop ↔ 버스 오류 Root Cause | 샘플률·시간축이 다른 스코프 CSV(VDD_IO, 모터 PWM 전류)와 LA 캡처(SPI)를 공통 마커 에지 교차상관으로 정렬하고, SPI CRC 오류 시점과 레일 droop·PWM 에지의 조건부 발생률(오류 vs 정상)을 계산해 원인 가설을 뒷받침하며, aggressor 에지 동기 평균으로 크로스토크/그라운드 바운스 커플링 파형을 분리해 함께 검증한다. | mac |
| HWLAB-A04 | 열 RC 네트워크 피팅: 가열 곡선 → Foster 모델 → 펄스 부하 Tj | 전력 스텝 인가 시 온도 상승 곡선(합성 또는 열전대 로그)을 3단 Foster 지수합으로 피팅해 Rth_i·τ_i를 추출하고 Zth(t)를 만든 뒤, 듀티사이클 펄스 부하에서 피크 Tj를 중첩 원리로 예측해 시간영역 시뮬레이션과 대조한다(throttling 정책은 see:SIV-A04). | mac |
| HWLAB-A05 | 간헐 부팅 실패 Fault Isolation (면접형) | '저온에서 50번 중 1번 부팅 실패' 합성 데이터셋(보드·온도별 레일 ramp, PGOOD/RESET_N/PLL_LOCK 타이밍, 부트 로그)으로 가설 트리(시퀀싱 레이스, PLL 락 전 리셋 해제, 레일 미정착 중 strap 샘플링)를 세우고, 각 타이밍 마진 분포를 추정해 분포 꼬리 겹침으로 계산한 실패 확률을 관측 1/50과 대조해 원인을 지목하는 분석 스크립트와 리포트를 작성한다(시퀀싱 checker는 see:SIV-A06). | mac |
| HWLAB-A06 | 저가 LA 실캡처: fx2 LA + sigrok로 실보드 버스 분석 | fx2lafw 호환 8ch LA(24MS/s)로 MCU 보드의 I2C·SPI·UART를 sigrok-cli로 캡처해 B07/M05/M06 파이프라인으로 디코드·타이밍 판정하고, SPI 클럭을 올려 가며 24MS/s 한계로 측정이 실패하는 지점이 B07 예측과 맞는지 확인한다. | **lab** |
| HWLAB-A07 | 보급형 DSO 자동화: SCPI 파형 획득과 실측 검증 | pyvisa(pyvisa-py)로 DSO 파형 preamble(x/y increment·origin·reference)을 읽어 바이트 파형을 초·볼트로 변환하는 획득 루프를 만들고, 벅 컨버터 eval 보드에서 그라운드 리드 vs 스프링 팁 링잉(B05), BW limit on/off 리플(B08), 부하 스텝 droop(M02)을 실측해 예측과 비교한다(RF 장비 SCPI는 see:RF-A05). | **lab** |
| HWLAB-A08 | 종합: 신규 보드 브링업·계측 계획서 (면접형) | SoC + PMIC + LPDDR + PCIe + MIPI 카메라를 가진 가상 보드에 대해 DFT 요구(테스트포인트, 전류 측정 shunt/0Ω, 디버그 헤더), 단계별 측정 항목·장비·프로빙 방법·pass 기준(레일·시퀀스·클럭 지터·DDR/PCIe 마진), 실패 시 격리 트리, 일정을 담은 계획서를 쓰고 앞선 항목 도구를 CSV 입력 → MD/HTML 리포트를 내는 하나의 bring-up 리포트 파이프라인으로 묶는다(see:PLAT-B09, see:SIV-A09). | mac |

### EMU — 에뮬레이션 · 프로토타이핑 (Emulation & Prototyping)

실리콘이 나오기 전에 펌웨어를 RTL 시뮬레이터·에뮬레이터·FPGA 프로토타입·가상 플랫폼 위에서 먼저 돌려 bring-up 기간을 앞당기는 기술이다. 칩 회사(NVIDIA, Apple, Tesla Dojo, AI 가속기 스타트업)의 pre-silicon 펌웨어 직무에서 플랫폼 선택, transactor, QEMU 디바이스 모델 작성 능력을 묻는다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| EMU-B01 | Pre-silicon 플랫폼 지도와 속도 계산 | 각 플랫폼의 대표 속도(RTL sim ~10Hz–1kHz, emulator ~1MHz, FPGA ~10–50MHz, VP ~수백 MIPS)로 '리눅스 부팅 1e9~1e10 명령'과 '부트 ROM 1e5 명령' 소요시간 표를 계산하는 스크립트를 쓰고, 어떤 작업을 어디서 할지 근거를 정리한다. | mac |
| EMU-B02 | 플랫폼 선택 케이스 스터디 | 6개 시나리오(부트 ROM 디버그, 드라이버 개발, 성능 측정, 하드웨어 버그 재현, OS 포팅, 전력 추정)에 대해 플랫폼·이유(속도, 가시성, 정확도, 준비 시점, 비용)·위험을 표로 작성하는 설계 연습을 하고, 면접식 2분 답변을 준비한다. | mac |
| EMU-B03 | C로 쓰는 행위 수준(Behavioral) 디바이스 모델 | 타이머(LOAD/VALUE/CTRL/INTSTS)와 UART(TX FIFO/STATUS) 행위 모델을 C로 작성하고 시간은 tick() 호출로 진행; 같은 드라이버 코드가 모델에 붙어 동작하는지 단위테스트로 확인한다. | mac |
| EMU-B04 | HAL 경계로 모델↔실제 HW 전환 | bus_ops(read32/write32/wait_irq) 인터페이스를 정의하고 컴파일 타임(#ifdef)·링크 타임(약한 심볼)·런타임(함수 포인터) 세 방식으로 모델/실HW 백엔드를 교체하는 예제를 만들어 오버헤드와 유지보수성을 비교한다. | mac |
| EMU-B05 | Valid/Ready 핸드셰이크: 신호 수준 vs 트랜잭션 수준 | valid/ready 스트림 인터페이스를 (1) 매 사이클 신호를 평가하는 모델 (2) send(packet) 함수 호출 모델로 각각 구현해 1e6 패킷 전송 시간을 측정하고, backpressure 타이밍 정보가 어디서 사라지는지 정리한다. | mac |
| EMU-B06 | Verilator 입문: 카운터·레지스터 블록 시뮬레이션 | 8비트 카운터와 APB-lite 레지스터 블록 Verilog를 Verilator로 빌드하고 C++ 테스트벤치에서 clock 토글·리셋·레지스터 read/write를 수행, VCD 파형을 덤프해 GTKWave로 확인한다. | **fpga** |
| EMU-B07 | VCD 파형 파서와 타이밍 체크 | VCD 파일 파서를 Python으로 작성해 신호별 transition 리스트를 만들고, valid가 ready 없이 내려가는 프로토콜 위반·특정 신호 펄스 폭 위반을 자동 검출; B06에서 만든 VCD와 일부러 손상시킨 VCD로 검증한다. | mac |
| EMU-B08 | QEMU에서 베어메탈 펌웨어 실행과 디버그 | qemu-system-arm(mps2-an385) 또는 riscv virt에 UART 출력 베어메탈 프로그램을 올리고 -s -S로 gdb attach, -d in_asm,int 및 -trace 이벤트로 실행 흐름을 기록해 예외 진입을 추적한다. | **qemu** |
| EMU-B09 | 레지스터 기술서 → 디바이스 모델 자동 생성 | SIV-B01 형식의 JSON 기술서에서 C 모델 골격(레지스터 저장소, access type 처리, 리셋, 콜백 훅)을 생성하는 Python 코드젠을 만들고, 생성된 모델에 SIV 리셋값/RW 테스트를 그대로 돌려 통과시킨다. | mac |
| EMU-M01 | Verilator C++ 테스트벤치로 AXI-lite 레지스터 구동 | AXI4-lite 슬레이브 레지스터 블록 RTL에 대해 C++ 측 axi_write/axi_read 함수(채널별 handshake를 클럭 단위로 수행)를 작성하고 리셋값/RW 테스트를 RTL 위에서 실행, 의도적 RTL 버그(wstrb 무시)를 검출한다. | **fpga** |
| EMU-M02 | Transactor 구현: 트랜잭션 ↔ 신호 변환 | host 측 read/write/burst 요청 큐를 받아 RTL valid/ready 신호로 바꾸는 C++ transactor를 Verilator 위에 구현하고, 요청당 호출(chatty) 방식과 배치(burst/streaming) 방식의 시뮬레이션 처리량을 비교해 SCE-MI 스타일 설계가 왜 필요한지 설명한다. | **fpga** |
| EMU-M03 | DPI-C로 C 참조모델 호출 | SystemVerilog DPI-C import로 C CRC32 참조 함수를 호출하는 테스트벤치를 Verilator로 빌드하고, RTL CRC 엔진 출력과 매 패킷 비교하는 checker를 만든다. | **fpga** |
| EMU-M04 | RTL CPU 위에서 펌웨어 실행 (PicoRV32) | PicoRV32를 Verilator로 빌드하고 메모리 모델에 riscv 툴체인으로 컴파일한 C 펌웨어 hex를 로드; magic address write로 printf와 테스트 종료 코드를 전달하는 환경을 만들어 펌웨어 단위테스트를 RTL 위에서 실행하고 시뮬레이션 속도(Hz)를 측정한다. | **fpga** |
| EMU-M05 | QEMU 디바이스 모델 작성 1: MMIO 레지스터 | QEMU 소스에 TypeInfo/MemoryRegionOps를 가진 간단한 디바이스(ID, SCRATCH, COUNTER 레지스터)를 추가해 virt 머신에 매핑하고, 베어메탈 게스트에서 읽기/쓰기 테스트와 trace-event 출력으로 동작을 확인한다. | **qemu** |
| EMU-M06 | QEMU 디바이스 모델 작성 2: IRQ와 DMA | M05 디바이스에 qemu_irq 출력과 dma_memory_read/write 기반 메모리 복사 엔진, QEMUTimer로 완료 지연을 추가하고 게스트 드라이버에서 인터럽트 기반 DMA 완료 처리를 검증한다. | **qemu** |
| EMU-M07 | 프로세스 간 Co-simulation 브리지 | 'CPU측' 프로세스(드라이버 실행)와 '디바이스측' 프로세스(B03 모델)를 Unix socket 기반 read/write/irq 메시지 프로토콜로 연결하는 브리지를 구현하고, 메시지 왕복 지연이 성능을 지배함을 측정해 배칭·공유메모리 방식과 비교한다. | mac |
| EMU-M08 | Co-simulation 시간 동기: Lockstep vs Quantum | M07 브리지에 시뮬레이션 시간을 도입해 매 사이클 lockstep과 quantum(N 사이클 독립 실행 후 동기) 방식을 구현하고, quantum 크기별 실행 속도와 인터럽트 도착 시각 오차를 측정해 그래프로 trade-off를 보인다. | mac |
| EMU-M09 | 디버그 가시성: 트랜잭션 트레이스와 트리거 | 디바이스 모델·버스에 고정 크기 링 트레이스(타임스탬프, 주소, 데이터, 마스터 ID)와 조건 트리거(특정 주소 write 후 N개)를 넣고, 버그 발생 직전 히스토리를 덤프하는 기능을 구현; 전 구간 파형 저장 대비 메모리/속도 비용을 비교한다. | mac |
| EMU-M10 | 모델·QEMU·RTL 간 회귀 일관성 검사 | 공통 테스트 바이너리/스크립트를 B03 C 모델, M06 QEMU 모델, M01 RTL 환경에서 실행하는 러너를 만들고 결과 로그를 정규화해 diff; 일부러 모델 하나에 다른 리셋값을 넣어 플랫폼 불일치 리포트를 생성한다. | mac |
| EMU-A01 | Speed Bridge 모델과 버퍼 사이징 | 1Gbps 이더넷 트래픽을 1MHz 에뮬레이터 측 링크로 연결하는 speed bridge를 이산사건 모델로 구현하고, 버퍼 크기·flow control(PAUSE)·패킷 드롭률·지연 관계를 측정해 '왜 프로토콜 타임아웃 조정이 필요한가'를 설명한다. | mac |
| EMU-A02 | 멀티 FPGA 파티셔닝과 핀 멀티플렉싱 | 블록 그래프(게이트 수, 블록 간 신호 수)를 FPGA 용량 제약 하에 분할하는 그리디/KL 휴리스틱을 구현하고, 경계 신호 수와 가용 핀으로 TDM 비율과 유효 시스템 클럭을 계산해 파티션별 결과를 비교한다. | mac |
| EMU-A03 | 에뮬레이션 용량·일정 계획 | SoC 게이트 수, 팀별 테스트 수·평균 사이클, 컴파일 시간, 에뮬레이터 보드 수를 입력받아 주간 사용률·대기 시간·필요 자원을 계산하는 계획 도구를 작성하고, 회귀 우선순위 정책 2가지를 비교하는 설계 메모를 쓴다. | mac |
| EMU-A04 | Hybrid Emulation: 빠른 CPU 모델 + 느린 가속기 RTL | M07 브리지를 이용해 빠른 CPU 측(호스트 네이티브 드라이버)과 느린 가속기 측(사이클 모델, 의도적 지연)을 연결하고 커맨드 큐 배칭·도어벨 최소화로 전체 시뮬레이션 시간을 줄이는 최적화를 구현·측정한다. | mac |
| EMU-A05 | 체크포인트·복원과 결정적 재현 | 디바이스 모델·메모리·시뮬레이션 시간 상태를 직렬화하는 snapshot/restore를 구현하고, 입력 이벤트 로그 replay로 버그를 결정적으로 재현; 비결정 요소(호스트 시간, 스레드 순서)를 제거하는 방법을 정리한다. | mac |
| EMU-A06 | Toggle 활동 기반 동적 전력 추정 | VCD(B07 파서 재사용)에서 신호·블록별 toggle 수를 집계하고 가정한 커패시턴스와 P = α·C·V²·f로 블록별 동적 전력을 추정, 워크로드 두 개의 전력 프로파일을 시간 창별로 비교한다. | mac |
| EMU-A07 | Day-0 실리콘 준비: Pre-silicon Bring-up 플로우 설계 | 부트 ROM→첫 UART 출력→DDR init→OS 부트 단계마다 사용할 플랫폼, 재사용할 테스트, 준비물(JTAG 스크립트, 레지스터 덤프 도구), 성공 기준을 정의한 설계 문서를 쓰고, 같은 bring-up 스크립트가 모델·QEMU·실보드 백엔드에서 실행되는 골격을 구현한다. | mac |

### SIM — 시뮬레이션 · 모델링 (Simulation & Modeling)

하드웨어가 없거나 비싸거나 위험할 때, 시스템을 소프트웨어 모델로 돌려 성능·동작·한계를 미리 아는 기술이다. 칩 아키텍처 성능 모델, HIL용 플랜트 모델, Monte Carlo 공차 분석은 AI 가속기·로보틱스·자동차 회사에서 펌웨어 엔지니어에게도 자주 요구된다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| SIM-B01 | 이산사건 시뮬레이션 엔진 만들기 | min-heap 이벤트 큐(시간 동률은 삽입 순서로 tie-break), schedule/cancel API, run_until(t)를 C로 구현하고, 정해진 이벤트 시나리오의 실행 순서·취소 동작을 단위테스트로 검증한다. | mac |
| SIM-B02 | 난수 생성과 분포 샘플링 | 시드 고정 PRNG(xorshift/PCG)에서 균등→지수(역CDF), 정규(Box-Muller), 이산 분포(alias 또는 누적합)를 구현하고 표본 평균·분산·히스토그램을 이론값과 비교, 카이제곱 검정으로 확인한다. | mac |
| SIM-B03 | M/M/1 큐 시뮬레이션과 Little's Law | B01 엔진으로 M/M/1 큐를 시뮬레이션해 ρ=0.5/0.8/0.95에서 평균 대기시간·큐 길이를 이론값(W=1/(μ−λ))과 비교하고, L=λW가 성립함을 확인; warm-up 제거와 배치 평균으로 신뢰구간을 계산한다. | mac |
| SIM-B04 | 운영 법칙으로 파이프라인 병목 찾기 | 센서→DMA→전처리→압축→전송 5단계 파이프라인의 단계별 서비스 시간·방문 횟수로 병목, 최대 처리량, 목표 지연을 위한 큐 깊이를 계산하는 스크립트를 쓰고, DES 결과와 비교한다. | mac |
| SIM-B05 | Euler 적분과 스텝 크기 오차 | RC 회로 방전과 자유낙하를 forward Euler로 적분해 해석해와의 오차를 스텝 h별로 측정, 로그-로그 기울기로 1차 정확도를 확인하고 h > 2RC에서 발산하는 것을 보인다. | mac |
| SIM-B06 | RK4 구현과 정확도 비교 | 질량-스프링-댐퍼 시스템을 상태벡터 형태로 C에 구현하고 Euler/semi-implicit Euler/RK4로 적분해 오차 차수(1,1,4)와 에너지 보존(감쇠 0일 때)을 비교한다. | mac |
| SIM-B07 | 적응 스텝 적분기 (RK45) | Dormand-Prince 또는 RK-Fehlberg 임베디드 쌍으로 국소 오차를 추정해 스텝을 키우고 줄이는 적분기를 구현, 급변 구간이 있는 입력(스텝 입력)에서 고정 스텝 RK4 대비 함수 호출 수와 오차를 비교한다. | mac |
| SIM-B08 | DC 모터 플랜트 모델 | L di/dt = V − Ri − Ke·ω, J dω/dt = Kt·i − bω − τ_load 모델을 RK4로 시뮬레이션해 스텝 전압 응답(전류 피크, 정상 속도, 시정수)을 구하고 해석적 정상상태 값과 비교한다. | mac |
| SIM-B09 | Monte Carlo 기초: 공차 분석 | ±1% 저항 2개 분압기와 ±5% ADC 기준전압 체인의 출력 분포를 1e6 샘플 MC로 계산해 스펙 이탈 확률(수율)을 구하고, worst-case 분석·RSS 근사와 비교한다. | mac |
| SIM-M01 | Cycle-Approximate CPU 파이프라인 모델 | 5단 in-order 파이프라인을 RAW 해저드(포워딩 유무), 분기 예측(정적/2-bit), 캐시 미스 지연 파라미터로 모델링하고 합성 명령 트레이스로 CPI 분해(stall 원인별)를 출력한다. | mac |
| SIM-M02 | SSD/NAND 성능 DES 모델 | 호스트 큐 깊이 QD, 채널 수, 채널당 다이 수, tR/tPROG/전송 시간 파라미터를 가진 NAND SSD 읽기/쓰기 DES 모델을 만들어 QD별 IOPS·p99 지연 곡선을 그리고 다이 병렬성 포화 지점을 설명한다. | mac |
| SIM-M03 | SystemC 커널 개념: Evaluate/Update와 Delta Cycle | signal(현재값/다음값), process 민감도 리스트, evaluate→update→delta notification 루프를 가진 미니 커널을 C++로 구현해 swap 레지스터 예제에서 delta cycle 없이 생기는 레이스가 사라짐을 보이고, 실제 SystemC 동작과 대응시킨다. | mac |
| SIM-M04 | TLM-2.0 스타일 Loosely-Timed 메모리 모델 | b_transport(payload, delay&) 인터페이스를 가진 initiator/interconnect/memory 타깃을 C++로 구현하고, 시간 주석·quantum keeper로 로컬 시간을 누적; 같은 워크로드에서 approximately-timed(단계별 phase) 대비 속도·정확도 차이를 비교한다. | mac |
| SIM-M05 | 버스 중재·인터커넥트 경합 모델 | CPU·DMA·NPU 3개 마스터가 하나의 메모리 포트를 공유하는 DES 모델에 고정 우선순위/라운드로빈/가중 라운드로빈 중재기를 구현하고, 마스터별 평균·p99 지연과 starvation을 비교한다. | mac |
| SIM-M06 | 큐잉 네트워크: 해석 vs 시뮬레이션 | 카메라 프레임→ISP→NPU→후처리 3단 큐 네트워크를 해석적으로(각 단 M/M/1 가정) 풀고 DES와 비교; 서비스 시간이 결정적(D)이거나 bursty 도착일 때 해석이 얼마나 틀리는지 보인다. | mac |
| SIM-M07 | 센서 출력 합성 모델 (HIL 입력 생성) | 실제 궤적(진값)에서 IMU 출력을 생성하는 모델(바이어스, 백색잡음, bias random walk, 스케일 팩터, 비정렬, 양자화, 포화, 지연)을 구현하고 각 오차 항을 켜고 끄며 출력 통계를 검증한다(분석은 see:SENS). | mac |
| SIM-M08 | SoC 열 RC 네트워크 모델 | 다이→패키지→방열판→공기 3노드 열 RC 네트워크를 구성하고 워크로드 전력 프로파일 입력으로 온도 응답을 시뮬레이션, 온도 임계 기반 DVFS 정책을 붙여 성능 손실을 계산한다(see:SIV-A04). | mac |
| SIM-M09 | 실시간 HIL 루프 실행과 지터 측정 | B08 모터 모델을 1kHz 고정 스텝으로 실행하는 프로세스와 제어기 프로세스를 UDP(또는 공유메모리)로 연결하고, 주기 지터·deadline miss·통신 지연을 측정해 히스토그램으로 보고한다; macOS와 Linux PREEMPT_RT의 차이를 정리한다. | mac |
| SIM-M10 | 희귀 사건 확률: Importance Sampling | 가우시안 잡음 채널의 비트 오류(BER 1e-9 근처)를 naive MC와 평균 이동 importance sampling으로 추정해 같은 정확도에 필요한 샘플 수를 비교하고, 이론 Q함수 값과 대조한다. | mac |
| SIM-A01 | Stiff 시스템과 암시적 적분 | 빠른 전기 시정수와 느린 열 시정수를 함께 가진 stiff 모델을 RK4와 backward Euler/trapezoidal(뉴턴 반복 포함)로 적분해 안정 가능한 최대 스텝과 계산량을 비교한다. | mac |
| SIM-A02 | Co-simulation 마스터 (FMI 개념) | FMI 스타일 API(instantiate/setInputs/doStep/getOutputs)를 가진 C 모델 2개(모터 플랜트, PI 제어기)를 만들고 Jacobi/Gauss-Seidel 마스터 알고리즘과 통신 스텝 크기에 따른 오차·안정성을 비교한다. | mac |
| SIM-A03 | 디지털 트윈: 파라미터 식별 | B08 모터 모델에서 잡음 섞인 '측정 로그'를 생성하고, 선형 최소제곱(이산 모델 회귀)과 비선형 최적화(시뮬레이션 오차 최소화)로 R, Ke, J, b를 추정해 참값 대비 오차와 검증 데이터 예측 정확도를 평가한다. | mac |
| SIM-A04 | 성능 모델 상관(Correlation)과 보정 | M02 SSD 모델 또는 M04 TLM 모델을 기준으로 '실측'(숨긴 파라미터로 생성한 벤치마크 결과)과 비교해 오차가 큰 워크로드를 찾고, 파라미터 보정으로 오차를 ±5% 안에 넣는 과정을 문서화한다. | mac |
| SIM-A05 | 병렬 DES와 결정성 | 두 개의 논리 프로세스(LP)로 나눈 DES를 스레드로 병렬 실행하고 null message + lookahead 방식으로 인과성을 보장; 결과가 순차 실행과 비트 단위로 동일한지 검증하고 lookahead 크기별 속도를 측정한다. | mac |
| SIM-A06 | 아키텍처 설계 공간 탐색 (DSE) | 코어 수·SRAM 크기·DRAM 대역폭·NPU MAC 수를 변수로 하는 roofline+큐잉 하이브리드 성능 모델과 면적/전력 근사 모델을 만들어 수백 개 구성을 평가하고 성능-전력-면적 Pareto front와 추천 구성을 제시한다. | mac |
| SIM-A07 | 모델 기반 결함 주입 시뮬레이션 | M09 HIL 루프에 결함 주입기(센서 stuck/드리프트/스파이크, 메시지 유실, 메모리 비트 플립)를 넣고 제어기 측 진단 로직의 검출률·검출 지연·오경보율을 수천 회 MC로 측정해 표로 보고한다. | mac |

### DSP — 신호처리 · 고정소수점 (DSP & Fixed-Point)

FPU가 없거나 전력이 아까운 MCU에서 필터·FFT·제어를 정수 연산으로 정확하게 돌리는 기술이다. 오디오·모터 제어·센서·RF 펌웨어 면접에서 Q15 곱셈의 포화, biquad 구현, FFT 직접 구현, 고정소수점 오버플로 분석은 단골 문제다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| DSP-B01 | Q-format 기초: Qm.n 변환·범위·해상도 | float↔Q15/Q31/Q7.8 변환 함수(round-to-nearest, truncate, 포화 포함)를 C로 구현하고 범위 끝값(-1.0, 1−2^-15, +1.0 입력)과 랜덤 값에 대해 최대 오차가 0.5 LSB 이내인지 테스트한다. | mac |
| DSP-B02 | Q15 곱셈: 반올림·포화·-1×-1 코너 | q15_mul(a,b)를 32비트 곱 → 반올림 상수 0x4000 더하기 → >>15 → 포화 순서로 구현하고, 2^32개 전수 조합(또는 대표 샘플) 테스트로 float 기준 대비 오차와 -32768×-32768 오버플로 처리를 검증한다; ARM SMULBB/QDMULH 의미와 비교한다. | mac |
| DSP-B03 | 포화 덧셈/뺄셈과 오버플로 검출 | 분기 없는 포화 add/sub(int16/int32)를 구현하고 __builtin_add_overflow 버전과 결과를 전수/랜덤 비교; wrap되는 FIR 누산과 포화 누산의 출력 차이를 사인파 입력으로 보인다. | mac |
| DSP-B04 | 서로 다른 Q 형식의 혼합 연산 | Q1.15×Q3.13 → Q4.28 → Q1.15 변환 체인, N개 Q15 곱 누산에 필요한 guard bit(ceil(log2 N)) 계산기를 작성하고, 누산기 폭이 부족할 때 오버플로가 나는 입력을 만들어 확인한다. | mac |
| DSP-B05 | 샘플링·Aliasing·양자화 잡음(SQNR) | 사인파를 다양한 fs로 샘플링해 aliasing 주파수를 예측·확인하고, N비트(8~16) 양자화 후 SQNR을 측정해 6.02N+1.76dB와 비교; 입력 레벨이 낮을 때 SQNR이 떨어지는 것과 dither 효과를 보인다. | mac |
| DSP-B06 | 이동평균 필터: O(1) 구현과 주파수 응답 | running sum + 원형 버퍼 이동평균을 정수로 구현(N=2^k일 때 shift 나눗셈)하고, 장시간 실행에서도 누산 오차가 없음을 확인; 계단·사인 sweep 입력으로 sinc 형태 주파수 응답과 null 위치를 측정한다. | mac |
| DSP-B07 | 1차 IIR(EMA) 저역통과 필터 | y += (x − y) >> k 형태의 EMA를 구현하고 k별 시정수·-3dB 주파수를 계산·측정; 작은 입력 변화가 반영되지 않는 정체 현상을 재현하고 소수부 누산(상위 비트 확장)으로 해결한다. | mac |
| DSP-B08 | FIR 필터 직접 구현 (Q15 + 64비트 누산) | 임의 탭 수 FIR을 float와 Q15(int64 누산, 최종 반올림·포화)로 구현하고 numpy.convolve 결과와 비교해 오차를 측정; 원형 버퍼 인덱싱 vs 지연선 복제(double-size buffer) 방식의 속도를 비교한다. | mac |
| DSP-B09 | DFT 직접 구현과 bin 해석 | O(N²) DFT를 C로 구현해 numpy.fft와 비교하고, bin 주파수 k·fs/N, 실수 신호의 켤레 대칭, 진폭 스케일(2/N), DC/Nyquist bin 특성을 테스트 신호로 확인한다. | mac |
| DSP-B10 | 윈도잉과 스펙트럼 누설 | Rectangular/Hann/Hamming/Blackman/Flat-top 윈도를 구현하고 bin 사이 주파수 사인파로 누설(sidelobe), scalloping loss, ENBW를 측정해 표로 비교; 가까운 두 톤 분리 실험을 한다. | mac |
| DSP-B11 | LUT 사인/코사인과 위상 누산기(NCO) | 256/1024 엔트리 사인 LUT(1/4 파 대칭 활용)를 Q15로 만들고 nearest vs 선형보간 오차를 측정; 32비트 위상 누산기 NCO로 주파수 해상도 fs/2^32를 확인하고 SFDR을 FFT로 측정한다. | mac |
| DSP-B12 | 정수 sqrt와 Newton-Raphson 역수 | 비트 단위 정수 sqrt(uint32→uint16)와 Q-format Newton-Raphson 역수/역제곱근(초기값 LUT + 2회 반복)을 구현해 전수 또는 대량 랜덤 테스트로 최대 오차를 측정하고 반복 횟수별 정확도를 비교한다. | mac |
| DSP-M01 | Radix-2 DIT FFT 구현 (float) | in-place iterative radix-2 DIT FFT(비트 반전 재배열 + twiddle 테이블)를 C로 구현해 numpy.fft와 상대오차 1e-6 이내로 맞추고, N=64~65536에서 DFT 대비 실행시간이 N log N으로 증가함을 측정한다. | mac |
| DSP-M02 | 고정소수점 FFT와 Block Floating Point | M01을 Q15로 포팅해 (1) 매 단계 >>1 고정 스케일 (2) 오버플로 감지 시에만 스케일하는 block floating point를 구현하고, 입력 레벨별 출력 SNR을 float FFT 대비 측정해 비교한다. | mac |
| DSP-M03 | Real FFT 트릭과 Goertzel | N점 실수 FFT를 N/2점 복소 FFT + 후처리로 계산하는 split 알고리즘을 구현해 속도를 비교하고, Goertzel 알고리즘으로 DTMF 8개 톤을 검출하는 디코더를 만들어 합성 DTMF 신호(잡음 포함)로 검증한다. | mac |
| DSP-M04 | Biquad IIR: DF1 vs DF2T와 SOS 캐스케이드 | RBJ cookbook 공식으로 LPF/HPF/peaking 계수를 계산해 Direct Form I, Transposed Direct Form II biquad를 float로 구현하고 scipy.signal.sosfreqz 응답과 비교; 고차 필터를 SOS 캐스케이드로 쪼갤 때 순서·이득 분배가 중요한 이유를 실험한다. | mac |
| DSP-M05 | 고정소수점 Biquad: 계수 양자화·Limit Cycle | Q2.30 계수·Q31 상태 DF1 biquad를 구현해 저주파 LPF(fc=fs/1000)에서 계수 양자화로 극점이 이동하는 것을 계산하고, 무입력 limit cycle 재현과 error feedback(noise shaping)으로 개선을 측정한다. | mac |
| DSP-M06 | FIR 필터 설계와 계수 양자화 | windowed-sinc(Kaiser) 설계를 Python으로 직접 구현해 통과대역 리플·저지대역 감쇠 스펙을 만족하는 최소 탭 수를 찾고, Q15 양자화 후 응답 열화를 측정; 대칭 계수를 이용해 곱셈을 절반으로 줄인 C 구현을 작성한다. | mac |
| DSP-M07 | Decimation/Interpolation과 Polyphase | M배 decimator와 L배 interpolator를 (1) 필터 후 다운샘플 (2) polyphase 구조로 각각 구현해 출력이 동일하고 연산량이 1/M임을 보이고, 안티에일리어싱 필터 유무에 따른 aliasing 성분을 FFT로 측정한다. | mac |
| DSP-M08 | CIC 필터와 비트 성장 | N단 CIC decimator(R, M 파라미터)를 정수로 구현해 필요한 레지스터 폭 Bin + N·log2(RM)을 검증(모듈러 산술로 wrap되어도 결과가 맞음을 확인)하고, PDM 마이크 비트스트림 모델 → CIC → 보상 FIR로 PCM을 복원해 SNR을 측정한다. | mac |
| DSP-M09 | CORDIC: sin/cos/atan2/크기 | rotation 모드(sin/cos)와 vectoring 모드(atan2, magnitude)를 Q 형식 정수로 구현하고 게인 K 보정, 사분면 처리, 반복 횟수별 오차(비트당 약 1비트)를 측정해 libm과 비교한다. | mac |
| DSP-M10 | 다항식 근사: atan2·exp·log 빠른 계산 | atan(x) on [0,1]을 Taylor vs minimax(Remez 또는 Chebyshev) 다항식으로 근사해 최대 오차를 비교하고, 사분면 확장으로 fast_atan2를 구현; exp2/log2는 지수부 분리 + 다항식으로 구현해 libm 대비 오차·속도를 측정한다. | mac |
| DSP-M11 | 오버플로·동적범위 분석 | FIR의 L1 노름(Σ|h|) 기반 최악 이득과 IIR의 임펄스 응답 L1/L2 노름을 계산하는 도구를 만들고, 그 경계를 실제로 달성하는 적대적 입력(부호 정렬 입력)을 생성해 오버플로를 재현한 뒤 스케일링으로 해결한다. | mac |
| DSP-M12 | 고정소수점 PID (가볍게) | Q16.16 또는 Q15 기반 이산 PID(적분 포화 제한, 출력 포화)를 구현하고 float 버전과 1차 플랜트 시뮬레이션 결과를 비교해 적분 누산 정밀도 부족으로 생기는 정상상태 오차를 보이고 개선한다(제어 이론은 see:RCTL). | mac |
| DSP-A01 | SIMD로 FIR/biquad 가속 (NEON on M2) | Apple M2의 NEON intrinsics(vmlal_s16 등)로 Q15 FIR을 벡터화하고 스칼라 대비 속도를 측정; Cortex-M4 SMLAD(16비트 2개 MAC)와 어떻게 대응되는지, 데이터 정렬·tail 처리·biquad가 벡터화하기 어려운 이유를 정리한다. | mac |
| DSP-A02 | CMSIS-DSP 함수로 Cortex-M 파이프라인 구성 | QEMU Cortex-M 타깃에서 CMSIS-DSP의 arm_fir_q15, arm_biquad_cascade_df1_q31, arm_rfft_q15를 사용해 블록 처리 파이프라인을 만들고, 출력이 직접 구현(B08/M05/M02)과 비트 단위로 일치하는지 비교한다. | **qemu** |
| DSP-A03 | 오디오 처리 파이프라인 (I2S DMA 블록 처리) | I2S DMA half/full 완료 콜백을 흉내내는 host 시뮬레이터에서 WAV 입력을 블록 단위로 받아 EQ(biquad 3단)+게인+리미터를 Q31로 처리해 WAV로 출력하고, 블록 크기별 지연·연산 시간 예산 초과 시 underrun 검출을 구현한다. | mac |
| DSP-A04 | 분수비 샘플률 변환 (44.1k→48k) | 147/160 비율 다상 리샘플러 또는 Farrow 3차 보간 리샘플러를 구현해 1kHz 톤의 THD+N과 통과대역 평탄도를 측정, 구조별 연산량·메모리를 비교한다. | mac |
| DSP-A05 | 적응 필터 LMS/NLMS | 알려진 에코 경로(FIR)를 가진 echo cancellation 시나리오에서 LMS/NLMS를 float와 Q15로 구현해 스텝 크기별 수렴 속도·잔여 오차(ERLE)를 측정하고 고정소수점 stalling 문제를 재현·완화한다. | mac |
| DSP-A06 | 디지털 PLL: 위상·주파수 추적 | 잡음 섞인 사인파의 위상·주파수를 추적하는 2차 디지털 PLL을 구현해 루프 대역폭·감쇠비에 따른 lock 시간·지터를 측정하고, 주파수 스텝 입력에서 cycle slip 조건을 찾는다(통신 응용은 see:RF). | mac |
| DSP-A07 | ADC 스펙트럼 분석: SNR/SINAD/THD/ENOB | 비선형성·잡음·지터를 넣은 ADC 모델 캡처(코히어런트 샘플링 조건 포함)에 대해 FFT 기반 SNR, THD(고조파 5개), SINAD, SFDR, ENOB=(SINAD−1.76)/6.02를 계산하는 도구를 만들고 Welch PSD로 잡음 바닥을 추정한다(see:SENS). | mac |
| DSP-A08 | Bit-exact 검증 프레임워크 | numpy 정수 연산으로 C와 같은 반올림·포화를 재현한 골든 모델을 작성하고, 테스트 벡터 생성(경계·랜덤·적대적) → C 실행 → 비트 단위 diff → 첫 불일치 샘플 리포트 자동화를 FIR/biquad/FFT에 적용한다. | mac |
| DSP-A09 | MCU DSP 사이클 예산 설계 | 100MHz Cortex-M7에서 48kHz 스테레오, 8단 biquad + 256탭 FIR + 리미터를 돌릴 때 샘플당 사이클 예산, 블록 크기별 오버헤드/지연, TCM vs 캐시 배치를 계산한 설계 메모를 쓰고, host 측정값으로 연산량 추정을 검증한다. | mac |

### SENS — 센서 · 측정 (Sensors & Measurement)

센서 값은 '읽으면 끝'이 아니라 스케일링·보정·필터링·타임스탬프·고장 감지를 거쳐야 믿을 수 있는 측정값이 된다. 로보틱스·드론·자동차·의료기기 펌웨어 면접은 IMU 드라이버, 2점 보정, 상보필터, 타임스탬프 동기, 전류 센싱을 실무 수준으로 묻는다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| SENS-B01 | ADC 코드 → 공학 단위 변환 | 12비트 ADC + 분압기 + 앰프 체인에서 코드→mV 변환을 float와 고정소수점(곱셈 후 시프트, 반올림)으로 구현하고 전 코드 범위 오차를 비교; LSB 크기, 단극/양극(two's complement) 입력, 기준전압 오차의 영향을 표로 정리한다. | mac |
| SENS-B02 | 오버샘플링·평균화와 디더 | 잡음 없는/있는 ADC 모델에서 4^n 샘플 평균 후 decimation으로 유효 비트가 늘어나는지 측정하고, 잡음이 LSB보다 작으면 효과가 없음(디더 필요)과 상관 잡음에서는 효과가 줄어드는 것을 보인다. | mac |
| SENS-B03 | I2C 온도·압력 센서 드라이버 | TMP117 유사 온도 센서와 BMP280 유사 압력 센서(보정 계수 읽기 + 데이터시트 보상 공식)를 mock I2C 버스 위에서 구현하고, ID 확인·설정·변환 대기·NACK 재시도·two's complement 변환을 테스트한다(I2C 프로토콜 자체는 see:COMM). | mac |
| SENS-B04 | SPI IMU 드라이버: 설정과 버스트 읽기 | ICM-42688 유사 레지스터 맵을 mock SPI 디바이스로 만들고 WHO_AM_I 확인, 소프트 리셋, ODR/FSR 설정, 14바이트 버스트 읽기(엔디안·부호 처리), LSB/g·LSB/dps 변환을 구현해 기대 값과 비교한다. | mac |
| SENS-B05 | 2점 보정: 오프셋·게인 | 두 기준점 측정값으로 gain/offset을 계산해 고정소수점으로 적용하는 함수와, 보정값을 버전·CRC가 있는 구조체로 플래시 에뮬레이션 영역에 저장/로드하는 코드를 작성하고 CRC 손상 시 기본값 폴백을 테스트한다. | mac |
| SENS-B06 | 다항식 보정과 서미스터 선형화 | NTC 서미스터 분압 회로의 ADC→온도 변환을 Steinhart-Hart, 3차 다항식 최소제곱 근사, 33포인트 LUT+선형보간 세 방식으로 구현해 -40~125°C 오차·코드 크기·연산 시간을 비교한다. | mac |
| SENS-B07 | 스파이크 제거: 메디안 필터와 이상치 처리 | 3/5/7-점 메디안 필터(정렬 네트워크로 분기 최소화)를 구현하고 스파이크가 섞인 신호에서 이동평균과 비교; 변화율 제한(slew limit)과 결합한 이상치 제거기를 만든다(선형 필터는 see:DSP). | mac |
| SENS-B08 | 측정 불확도 계산과 오차 예산 | 션트 저항 전류 측정 체인(저항 공차·온도계수, 앰프 오프셋·게인 오차, ADC 양자화·잡음)의 불확도 예산 표를 작성하고, GUM 전파 법칙(RSS) 계산과 Monte Carlo 결과를 비교해 확장 불확도(k=2)를 보고한다. | mac |
| SENS-B09 | 쿼드러처 엔코더 디코딩과 속도 추정 | 상태 전이 테이블 기반 x4 쿼드러처 디코더(잘못된 전이 에러 카운트 포함)를 구현하고, 카운트 기반(M법)·주기 기반(T법)·혼합(M/T법) 속도 추정을 저속/고속 시뮬레이션 신호로 비교한다. | mac |
| SENS-B10 | 홀 센서 3상 디코딩 | 3비트 홀 상태 → 6개 섹터 매핑과 방향 판정, 에지 간격 기반 전기 속도 계산, 무효 상태(000/111)·순서 오류 검출을 구현해 시뮬레이션 홀 신호(잡음/글리치 포함)로 검증한다. | mac |
| SENS-M01 | 전류 센싱: 션트·앰프·PWM 동기 샘플링 | low-side/인라인 션트 + INA240 유사 앰프 모델에서 zero-current 오프셋 보정, PWM 중앙 정렬 시점 ADC 트리거(스위칭 링잉 회피)를 시뮬레이션하고, 잘못된 샘플링 시점에서 생기는 오차를 정량화한다. | mac |
| SENS-M02 | 가속도계 6면 보정과 자이로 바이어스 | 합성 데이터(바이어스·스케일·축 비정렬 포함)에서 6방향 정지 측정으로 가속도계 바이어스/스케일을 최소제곱으로 추정하고, 정지 구간 검출 후 자이로 바이어스를 추정; 보정 전후 |a|=1g 오차를 비교한다. | mac |
| SENS-M03 | 자력계 Hard/Soft Iron 보정 | hard iron 오프셋과 soft iron 행렬로 왜곡된 합성 자력계 데이터(구면 샘플 + 잡음)에 대해 타원체 최소제곱 피팅을 구현해 보정 행렬을 추정하고, 보정 후 반지름 분산과 heading 오차를 비교한다. | mac |
| SENS-M04 | 상보필터로 Pitch/Roll 추정 | 합성 궤적(진값) 기반 IMU 데이터에서 가속도계 각도·자이로 적분·상보필터(α 튜닝)를 비교해 RMS 오차와 동적 가속 시 오차를 측정하고, 시정수 τ와 α=τ/(τ+dt) 관계를 설명한다(Kalman은 see:RCTL). | mac |
| SENS-M05 | Allan Variance로 IMU 잡음 특성화 | 백색잡음+bias random walk+flicker 성분을 가진 장시간 합성 자이로 데이터에 대해 overlapping Allan deviation을 구현하고 기울기 -1/2 구간에서 ARW, 최소점에서 bias instability를 읽어 생성 파라미터와 비교한다. | mac |
| SENS-M06 | 센서 FIFO와 워터마크 인터럽트 | 패킹된 프레임(헤더+accel+gyro+temp+timestamp)을 가진 mock IMU FIFO와 워터마크 인터럽트를 구현하고, 드라이버의 FIFO 카운트 읽기·버스트 파싱·불완전 프레임·오버플로 복구를 테스트한다. | mac |
| SENS-M07 | 타임스탬핑과 클럭 드리프트 보정 | 센서 ODR 클럭(±1% 오차)과 MCU 타이머가 다른 시뮬레이션에서 DRDY 인터럽트 시각 기록, FIFO 배치에서 샘플별 시각 역산, 선형 회귀로 드리프트를 추정해 인터럽트 지연 지터를 제거하는 타임스탬프 필터를 구현한다. | mac |
| SENS-M08 | 다중 센서 시간 정렬 | 카메라(30Hz)·IMU(1kHz)·GPS(10Hz) 합성 스트림에 미지의 고정 오프셋을 넣고 상호상관으로 오프셋을 추정, 공통 시간축으로 보간(선형/SLERP)해 정렬 오차를 측정; 하드웨어 트리거 동기와의 차이를 정리한다. | mac |
| SENS-M09 | 센서 데이터 로깅 포맷 | 헤더(매직, 버전, 센서 설정)·레코드(타입, 길이, 타임스탬프, 페이로드, CRC)를 가진 로그 포맷과 C 로거(링 버퍼 → 플래시 블록), Python 디코더를 만들고 전원 손실(마지막 블록 절단)·스키마 버전 변경에서도 복구되는지 테스트한다. | mac |
| SENS-M10 | 센서 고장 감지와 Plausibility 검사 | 범위·변화율·stuck(분산 0)·잡음 증가·중복 센서 간 불일치(2oo3 투표) 검사를 구현하고, 결함 주입 시뮬레이션으로 검출 지연과 오경보율을 임계값별로 측정한다. | mac |
| SENS-A01 | Mahony/Madgwick AHRS (쿼터니언) | 쿼터니언 자세 적분과 Mahony(PI 보정) 또는 Madgwick(경사하강) 필터를 C로 구현해 합성 9축 데이터에서 roll/pitch/yaw RMS 오차를 측정하고, 자기 교란 구간에서 mag 게인 조정 효과를 비교한다. | mac |
| SENS-A02 | 고분해능 ΣΔ ADC 측정 체인 | ADS1262 유사 ΣΔ ADC 모델(sinc 필터 차수, 데이터율)을 만들어 채널 전환 시 settling 지연, 50/60Hz 노치 효과, 데이터율별 잡음을 시뮬레이션하고 다채널 스캔 주기를 설계한다. | mac |
| SENS-A03 | AC 전력 측정: RMS·유효전력·역률 | 위상차·고조파를 가진 합성 전압/전류 파형에서 Vrms/Irms/유효전력/피상전력/역률을 계산하고, 두 채널 샘플링 시차를 보간으로 보정했을 때 역률 오차가 줄어드는 것을 측정한다. | mac |
| SENS-A04 | 고속 ADC DMA 파이프라인 무손실 검증 | 타이머 트리거 ADC → DMA half/full 콜백 → 처리 큐 흐름을 host 이벤트 시뮬레이션으로 구현하고, 샘플 시퀀스 카운터로 누락·중복을 검증; 처리 시간이 버퍼 시간을 넘을 때 overrun 검출과 버퍼 크기 결정을 보인다. | mac |
| SENS-A05 | 절대 엔코더(SSI/BiSS-C)와 sin/cos 보간 | BiSS-C 유사 프레임(위치, 에러/경고 비트, CRC6) 파서와 sin/cos 아날로그 엔코더의 atan2 보간(오프셋·진폭 불일치 보정 포함)을 구현해 합성 신호에서 위치 오차를 측정한다. | mac |
| SENS-A06 | 센서 특성화 자동화: 온도 sweep 보정 | 여러 개체의 온도 sweep 합성 데이터(오프셋·게인의 온도 의존성)에 대해 개체별 다항 피팅, 잔차 분석, 이상 개체 검출, 펌웨어용 보정 테이블(계수 Q 형식) 생성까지 자동화하는 Python 파이프라인을 만든다. | mac |
| SENS-A07 | 센서 HAL/프레임워크 설계 | Zephyr sensor API를 참고해 sample_fetch/channel_get, 트리거, 속성 설정, 단위 변환을 가진 C 센서 프레임워크를 설계·구현하고 B03/B04 드라이버를 이식; 동기 vs 비동기(RTIO 유사) 읽기의 trade-off를 정리한다. | mac |
| SENS-A08 | 로드셀 측정 시스템 end-to-end 설계 | 휘트스톤 브리지 로드셀(mV/V 감도) + 계측 앰프 + 24비트 ADC 체인에서 nV/√Hz 잡음 밀도부터 유효 분해능까지 잡음 예산을 계산하고, 영점/스팬 보정·크리프 보상·필터 설계를 포함한 설계 문서와 핵심 코드 경로를 작성한다. | mac |

### RF — RF · 무선 펌웨어 (RF & Wireless Firmware)

RF 통합 경험을 펌웨어 관점으로 끌어올리는 트랙이다. 링크 버짓·변조·EVM 같은 RF 기초를 숫자로 다루고, BLE/LoRa/Wi-Fi/셀룰러 모뎀을 다루는 라디오 드라이버·프레이밍·FEC·호핑을 직접 구현해 무선 펌웨어 면접(Apple, Qualcomm, Anduril, 드론·IoT 회사)에 대비한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| RF-B01 | dB·dBm 연산 라이브러리 | dBm↔mW, dB 이득 합산, 선형 영역 전력 합(두 -90dBm 신호 합 = -87dBm), 전압비 vs 전력비 dB를 구현하는 C/Python 유틸과 단위테스트를 작성하고 흔한 실수(dBm끼리 더하기) 사례를 정리한다. | mac |
| RF-B02 | 경로 손실 모델: FSPL과 Log-distance | FSPL(d,f) 공식과 log-distance + log-normal shadowing 모델을 구현해 2.4GHz/868MHz에서 거리별 수신 전력 분포를 계산하고, 경로 손실 지수 n 추정을 측정 데이터(합성)로 회귀한다. | mac |
| RF-B03 | 잡음지수와 Friis 캐스케이드 | LNA·필터·믹서·IF 앰프 체인의 이득/NF로 Friis 공식 캐스케이드 NF 계산기를 만들고, LNA 앞 손실(케이블·필터)이 NF를 직접 더하는 것을 예제로 보이며 감도 = -174 + 10log(BW) + NF + SNRreq를 계산한다. | mac |
| RF-B04 | 링크 버짓 계산기 | Tx 전력, 안테나 이득, 케이블 손실, 경로 손실, 페이딩 마진, 수신 감도로 링크 마진과 최대 거리를 계산하는 도구를 만들고 BLE 1M/Coded PHY, LoRa SF7/SF12 시나리오를 비교한다. | mac |
| RF-B05 | I/Q 기저대역 신호 기초 | 복소 지수로 톤을 만들고 NCO 곱으로 주파수 이동, 음/양 주파수 구분, IQ 불균형(진폭/위상)이 만드는 이미지 톤을 FFT로 관찰하며 이미지 제거비(IRR)를 계산한다. | mac |
| RF-B06 | 디지털 변조: ASK/FSK/BPSK/QPSK 와 BER | 기저대역 OOK, 2-FSK(비동기 검출), BPSK, QPSK(Gray) 모뎀을 numpy로 구현해 AWGN 채널 Eb/N0별 BER을 측정하고 이론 곡선(Q함수)과 겹쳐 그려 변조별 전력 효율 차이를 설명한다. | mac |
| RF-B07 | QAM 성상도와 EVM 측정 | 16/64-QAM Gray 매핑/디매핑을 구현하고 AWGN·IQ 불균형·위상잡음·PA 압축(AM/AM)을 각각 넣어 RMS EVM(%, dB)을 측정; EVM과 SNR 관계(EVM≈1/√SNR)를 확인한다. | mac |
| RF-B08 | 패킷 프레이머: 프리앰블·싱크워드·CRC | 프리앰블+싱크워드+길이+페이로드+CRC16 프레이머/디프레이머를 C로 구현하고, 랜덤 비트 스트림 속 싱크워드 상관 검색(허용 비트 오류 k개), 길이 필드 손상 처리, CRC 실패 카운트를 테스트한다. | mac |
| RF-B09 | BLE CRC-24와 데이터 화이트닝 | BLE CRC-24(다항식 0x00065B, 광고 init 0x555555)와 채널 인덱스로 초기화되는 x^7+x^4+1 화이트닝 LFSR을 비트 순서(LSB first)까지 정확히 구현하고 스펙/공개 캡처의 테스트 벡터로 검증한다. | mac |
| RF-B10 | RSSI 처리와 거리 추정의 한계 | 레지스터 RSSI(오프셋·스케일)→dBm 변환, 선형 영역 평균 vs dB 평균 차이, 지수 필터를 구현하고 B02 shadowing 모델로 RSSI 기반 거리 추정 오차 분포를 계산한다. | mac |
| RF-B11 | 라디오 드라이버 상태기계 | SX127x/CC1101 유사 mock 트랜시버(SPI 레지스터, 상태 전이 지연, TX_DONE/RX_DONE/CRC_ERR IRQ)에 대해 SLEEP/STANDBY/TX/RX/CAL 상태기계 드라이버를 작성하고, IRQ 유실·타임아웃·잘못된 상태에서의 명령을 테스트한다. | mac |
| RF-M01 | 채널 코딩 이득: Hamming(7,4)의 BER/BLER | BSC·AWGN-BPSK 채널 시뮬레이터에서 Hamming(7,4) 부호화 전후 비트·블록 오류율 곡선을 Eb/N0 기준(부호율 보정 포함)으로 그리고, 경판정 디코딩 이득을 이론값과 비교한다(SECDED 메모리 ECC 구현은 see:MEM-B07). | mac |
| RF-M02 | 컨볼루션 코드와 Viterbi 디코더 | K=3(그다음 K=7, 171/133) r=1/2 컨볼루션 인코더와 hard-decision Viterbi(traceback)를 구현해 BPSK/AWGN에서 코딩 이득을 측정하고, soft-decision(3비트 양자화)으로 추가 이득(~2dB)을 확인한다. | mac |
| RF-M03 | 인터리빙과 버스트 오류 | Gilbert-Elliott 버스트 채널 모델에서 M01/M02 FEC 성능을 측정하고, 블록 인터리버를 넣었을 때 성능 회복과 지연 증가를 정량화한다. | mac |
| RF-M04 | BLE 광고 패킷 생성·파싱 | Access Address 0x8E89BED6, PDU 헤더, AdvA, AD 구조(Flags, Name, Manufacturer Data)를 조립하고 B09 CRC/화이트닝을 적용한 on-air 비트열을 생성; 역방향 파서로 공개 캡처 바이트를 디코드해 필드를 출력한다. | mac |
| RF-M05 | BLE 링크 레이어: 연결 이벤트와 채널 선택 | 광고/스캔/개시/연결 상태기계와 connection interval·supervision timeout 처리, Channel Selection Algorithm #1(hop increment)과 #2(PRN 기반) 및 used channel map 리매핑을 구현해 스펙 예제 값과 비교한다. | mac |
| RF-M06 | ATT/GATT 서버 최소 구현 | 핸들·UUID·권한을 가진 속성 테이블과 ATT Read/Write/Find Information/Read By Group Type 요청 처리기, Notification 생성, MTU 처리, 에러 응답을 C로 구현해 바이트 PDU 테스트 벡터로 검증한다. | mac |
| RF-M07 | LoRa CSS 변조와 Time-on-Air | numpy로 up-chirp 기반 심볼 변조와 dechirp + FFT 복조를 구현해 SNR -15dB 근처 SF별 SER을 측정하고, Semtech 공식으로 SF/BW/CR/payload별 time-on-air 계산기를 만들어 듀티사이클 제한과 연결한다. | mac |
| RF-M08 | 셀룰러 모뎀 AT 명령 통합 | 응답(OK/ERROR/+CME ERROR)과 비동기 URC(+CREG, +CEREG, RING)를 구분하는 라인 파서와 명령 큐·타임아웃·재시도, 등록→PDP 활성화→소켓 연결 상태기계를 구현하고 시뮬레이션 모뎀(pty 또는 mock 스크립트)으로 망 이탈·재등록을 테스트한다. | mac |
| RF-M09 | Wi-Fi 펌웨어 기초: 프레임 파싱과 재연결 | Beacon/Probe Response 바이트에서 SSID·채널·RSN IE·capability를 파싱하는 C 파서를 작성하고, 스캔→인증→연결→4-way handshake 완료→연결 해제 시 지수 백오프 재연결 상태기계를 mock 이벤트로 테스트한다. | mac |
| RF-M10 | 수신기 동기화: CFO 추정과 심볼 타이밍 | 프리앰블 기반 반송파 주파수 오프셋(CFO) 추정·보정과 Gardner 심볼 타이밍 복원 루프를 numpy로 구현해 CFO·클럭 오차(ppm)를 넣은 QPSK 신호를 복조하고, 보정 전후 EVM을 비교한다. | mac |
| RF-A01 | RF 캘리브레이션: Tx 전력·IQ·DC 오프셋 | IQ 불균형(진폭/위상)·DC 오프셋을 수신 신호 통계로 추정해 보정하는 알고리즘과, 온도·채널별 Tx 전력 보정 테이블(측정 → 보간 → 펌웨어 테이블) 생성을 구현하고 보정 전후 IRR·전력 오차를 비교한다. | mac |
| RF-A02 | 공존(Coexistence): Wi-Fi/BLE/LTE 중재 | Wi-Fi·BLE·LTE 트래픽 요구(주기, 우선순위, 지연 허용)를 가진 이벤트 시뮬레이션에 PTA 스타일 중재기(우선순위, 요청/승인, 강제 보호 구간)를 구현하고 정책별 처리량 손실·BLE 연결 이벤트 누락률을 비교한다. | mac |
| RF-A03 | OFDM-lite 송수신기 (numpy SDR) | IFFT/CP 삽입 송신기와 CP 제거·FFT·파일럿 기반 채널 추정·1-tap 등화 수신기를 구현해 다중경로 채널에서 CP 길이에 따른 ISI와 BER을 측정한다. | mac |
| RF-A04 | 저전력 라디오 듀티사이클과 배터리 수명 | sleep/wake/RX window/TX 전류 프로파일로 평균 전류·배터리 수명을 계산하고, 32kHz 클럭 ±50ppm 드리프트를 보상하는 window widening 스케줄러를 구현해 연결 유지율과 전력의 trade-off를 측정한다. | mac |
| RF-A05 | RF 생산 테스트 자동화 (SCPI) | SCPI over TCP mock 신호발생기/분석기를 만들고 Python으로 Tx 전력·주파수 오차 측정, PER 30.8% 기준 감도 이진 탐색, 한계 판정과 리포트를 자동화한다. | mac |
| RF-A06 | 실제 SDR로 신호 수신·디코드 | RTL-SDR/HackRF로 FM 방송 또는 433MHz OOK 리모컨 신호를 IQ 캡처해 numpy로 다운컨버전·필터·복조·비트 디코드하고, 캡처 RSSI·SNR을 측정한다. | **lab** |
| RF-A07 | 규제 준수 로직: 듀티사이클·LBT·호핑 | EU 868MHz 서브밴드 듀티사이클(1%) 계정, LBT(listen-before-talk) 절차, FCC 호핑 요구(채널 수, 체류시간)를 검사하는 송신 스케줄러/검사기를 구현하고 송신 로그로 위반을 검출한다. | mac |
| RF-A08 | RF Desense 디버그 케이스 스터디 | 보드의 클럭 목록(MCU, DDR, SPI, 디스플레이 등)으로 RF 수신 대역에 떨어지는 고조파·혼변조 성분을 계산하는 도구를 만들고, 감도 열화 측정(합성)과 대응책(클럭 주파수 변경, 스펙트럼 확산, 차폐, 시간 분할) 평가를 담은 디버그 리포트를 작성한다. | mac |

### WCONN — 리눅스 무선 연결성 (Linux Wireless Connectivity)

카메라·IoT·로봇 기기의 연결성 엔지니어는 리눅스 위에서 Wi-Fi·Bluetooth·셀룰러를 붙이고, 수십만 대 fleet에서 '연결이 끊긴다'는 문제를 로그만으로 풀어야 한다. 이 트랙은 cfg80211/mac80211/nl80211, wpa_supplicant·hostapd, WPA2/WPA3 핸드셰이크, BlueZ/HCI/GATT, 펌웨어 로딩·크래시 복구, 연결 관리 데몬과 프로비저닝을 직접 구현·재현하며 익힌다. PHY·링크 레이어 펌웨어와 BLE 패킷 포맷은 RF 트랙을 기반으로 한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| WCONN-B01 | 802.11 MAC 헤더와 Radiotap 파싱 | monitor 모드 pcap(LINKTYPE 127) 바이트를 읽어 radiotap present 비트맵(확장 비트·정렬 규칙 포함)에서 채널·신호 세기·레이트를 뽑고, 802.11 헤더의 type/subtype·주소 역할(RA/TA/DA/SA/BSSID)·재전송 비트·seq/frag를 출력하는 C 파서를 작성해 공개 캡처와 Wireshark 결과로 검증한다(IE 파싱 기초는 see:RF-M09). | mac |
| WCONN-B02 | 리눅스 무선 스택 지도: SoftMAC vs FullMAC | mac80211_hwsim 위에서 iw list/iw phy 출력(지원 대역·암호·인터페이스 조합·feature flag)을 주석으로 해설하고, iw dev wlan0 scan 한 번이 거치는 커널 함수 경로를 ftrace function_graph로 기록해 SoftMAC(mac80211)와 FullMAC(brcmfmac 등, cfg80211_ops 직접 구현)의 경로 차이를 다이어그램으로 정리한다. | **linux** |
| WCONN-B03 | Netlink TLV 파서: nlmsghdr·nlattr·중첩 속성 | libnl 없이 nlmsghdr·genlmsghdr·nlattr를 순회하는 C 파서(NLA_ALIGN, 길이 검증, 중첩 재귀, 잘린 버퍼 거부)를 작성하고, 미리 저장한 nl80211 GET_INTERFACE/GET_STATION 응답 hex 덤프를 속성 이름까지 디코드해 iw 출력과 일치함을 확인한다. 잘못된 길이 필드를 넣은 퍼즈 입력으로 경계 검사를 테스트한다. | mac |
| WCONN-B04 | Generic Netlink로 nl80211 직접 호출 | AF_NETLINK/NETLINK_GENERIC 소켓으로 CTRL_CMD_GETFAMILY("nl80211")를 보내 family id와 멀티캐스트 그룹(scan, mlme)을 얻고, NL80211_CMD_GET_INTERFACE dump와 TRIGGER_SCAN → NEW_SCAN_RESULTS 이벤트 대기 → GET_SCAN dump를 B03 파서로 처리하는 mini-iw를 작성해 hwsim 환경에서 iw 결과와 비교한다(nlmon으로 메시지 캡처). | **linux** |
| WCONN-B05 | mac80211_hwsim 랩: hostapd AP + wpa_supplicant STA | hwsim 라디오 3개를 만들어 하나는 netns 안 hostapd(WPA2-PSK, 2.4GHz ch6) AP, 하나는 wpa_supplicant STA, 하나는 모니터로 쓰는 셋업 스크립트를 작성하고, 연결 후 DHCP 없이 정적 IP로 ping이 되는지, hwsim0 캡처에 Beacon→Auth→Assoc→EAPOL 1–4가 순서대로 보이는지 자동 검사한다. | **linux** |
| WCONN-B06 | 채널·주파수 변환과 규제 도메인(regdb) | ieee80211_channel_to_frequency와 같은 변환(2.4GHz 2407+5ch·ch14=2484, 5GHz 5000+5ch, 6GHz 5950+5ch)을 구현하고, wireless-regdb의 db.txt를 파싱해 국가 코드와 채널 폭(20/40/80MHz)별 허용 채널·EIRP·DFS 여부를 출력하는 도구를 작성해 iw reg get 결과(US, KR, DE)와 비교한다. 두 도메인 교집합(AP 국가 IE vs 기기 설정) 계산도 테스트한다. | mac |
| WCONN-B07 | WPA2-PSK 키 유도: PBKDF2-HMAC-SHA1 → PMK | SHA-1과 HMAC-SHA1(HMAC 구조는 see:SEC-B06)을 이용해 PBKDF2(passphrase, SSID, 4096, 32)를 C로 구현하고 IEEE 802.11 부록 테스트 벡터(passphrase "password", SSID "IEEE")와 wpa_passphrase 출력으로 검증한 뒤, 4096회 반복이 MCU에서 얼마나 걸리는지 측정해 PMK 캐싱이 필요한 이유를 정리한다. | mac |
| WCONN-B08 | PTK 유도와 EAPOL-Key MIC 검증 | "Pairwise key expansion" PRF-384(min/max(AA,SPA) || min/max(ANonce,SNonce))로 PTK를 유도하고 KCK/KEK/TK를 분리한 뒤, 공개 WPA2 핸드셰이크 캡처의 메시지 2–4에서 MIC 필드를 0으로 지운 프레임에 HMAC-SHA1-128(descriptor v2)을 계산해 일치를 확인한다. 잘못된 passphrase로는 MIC가 틀리는 것을 보이고, 이것이 오프라인 사전 공격의 근거임을 설명한다. | mac |
| WCONN-B09 | HCI 패킷 기초: btsnoop 파서와 opcode/event 디코드 | btsnoop 파일(헤더 "btsnoop\0", datalink 1002 H4 / 2001 Linux monitor)을 읽어 HCI Command(opcode=OGF<<10|OCF)·Command Complete/Status·LE Meta Event·ACL(handle, PB/BC 플래그)을 디코드하는 파서를 작성하고, btmon 캡처 예제로 Reset → Read Local Version → LE Set Scan Parameters 흐름을 재구성한다(LL/PDU 포맷은 see:RF-M04). | mac |
| WCONN-B10 | BlueZ 랩: 가상 컨트롤러와 btmon | hci_vhci/btvirt로 가상 LE 컨트롤러 2개를 만들고 bluetoothd를 띄운 뒤 한쪽은 광고, 다른 쪽은 스캔·연결하도록 bluetoothctl 스크립트를 작성, btmon 출력에서 HCI ↔ mgmt ↔ D-Bus 계층이 각각 어떤 메시지를 주고받는지 표로 정리한다. | **linux** |
| WCONN-B11 | 커널 펌웨어 로딩: request_firmware 경로 | 가상 플랫폼 드라이버 모듈에서 request_firmware()/firmware_request_nowarn()/request_firmware_nowait()로 /lib/firmware의 blob을 로드하고 헤더(magic, 버전, CRC)를 검증하는 코드를 작성한 뒤, 파일 누락·손상·initramfs 미포함 상황을 재현해 dmesg와 동작 차이를 기록하고 firmware_class.path 대체 경로를 테스트한다. | **linux** |
| WCONN-M01 | 4-way handshake 상태기계와 GTK 설치 | B08의 키 유도를 재사용해 메시지 1–4 교환(ANonce/SNonce, RSN IE 일치 확인, KEK로 GTK 언랩 AES-KW, Install 비트)을 수행하는 AP/STA 시뮬레이터를 작성하고, 메시지 손실·중복·replay counter 역행·MIC 실패 주입 테스트를 통과시킨다. 메시지 3 재전송 시 동일 키 재설치(KRACK)를 막는 가드를 넣고 그 근거를 설명한다. | mac |
| WCONN-M02 | WPA3-SAE(Dragonfly) 개념 구현 | P-256 위에서 password element 유도(hunting-and-pecking과 hash-to-element 두 방식), commit(scalar, element)·confirm 교환, PMK/PMKID 유도를 Python으로 구현해 양측이 같은 PMK를 얻는지 검증하고, hostapd 테스트의 SAE 벡터와 비교한다. hunting-and-pecking 반복 횟수가 비밀번호에 따라 달라지는 타이밍 누설(Dragonblood)과 H2E가 이를 막는 이유를 측정·정리한다. | mac |
| WCONN-M03 | PMF(802.11w)와 deauth 공격 방어 | Deauthentication/Disassociation 프레임에 MMIE(IE 76, KeyID, IPN, MIC)를 붙이는 BIP-CMAC-128 생성/검증과 IPN replay 검사를 구현하고, 위조 deauth 수신 시 SA Query 요청/응답으로 진위를 확인하는 STA 측 로직을 시뮬레이션한다. 이후 hwsim 랩(B05)에서 ieee80211w=2 설정으로 실제 hostapd/wpa_supplicant 동작을 확인한다. | mac |
| WCONN-M04 | wpa_supplicant 제어 인터페이스 클라이언트 | Unix datagram 소켓으로 /run/wpa_supplicant/wlan0에 붙어 ATTACH, SCAN, SCAN_RESULTS 파싱, ADD_NETWORK/SET_NETWORK/ENABLE_NETWORK, STATUS를 수행하고, <N>CTRL-EVENT-CONNECTED/DISCONNECTED(reason)/SSID-TEMP-DISABLED 이벤트를 요청 응답과 분리해 처리하는 C 클라이언트를 작성한다. hwsim 랩에서 잘못된 PSK·AP 다운 시나리오의 이벤트 순서를 기록한다. | **linux** |
| WCONN-M05 | 스캔 결과 평가와 BSS 선택 정책 | 스캔 결과 덤프(BSSID, 주파수, RSSI, RSN 능력, BSS Load IE의 채널 사용률·접속 수, 지원 폭)를 입력으로 대역 선호(6>5>2.4GHz 보정), 보안 수준, 혼잡도, 히스테리시스를 반영한 BSS 선택기를 구현하고, 채널 목록·dwell 시간별 전체 스캔 시간을 계산해 부분 스캔(이전 채널 우선) 정책과 비교한다. | mac |
| WCONN-M06 | 로밍 결정 엔진: 트리거·히스테리시스·FT | log-distance 경로 손실(see:RF-B02)로 이동하는 STA의 RSSI 궤적을 만들고, RSSI 임계값+히스테리시스+최소 체류시간 기반 로밍 트리거와 백그라운드 스캔을 구현해 ping-pong 횟수·연결 단절 시간을 측정한다. full auth(4-way 포함) vs 802.11r FT(over-the-air) 재연결 지연 모델을 넣어 비교하고, 11k neighbor report·11v BSS transition 요청을 반영한 버전도 만든다. | mac |
| WCONN-M07 | Raw HCI 소켓으로 LE 스캔 구현 | AF_BLUETOOTH/BTPROTO_HCI 소켓(HCI_CHANNEL_USER 또는 raw + 필터)으로 LE Set Scan Parameters/Enable(또는 Extended Scan) 명령을 보내고 Command Complete 확인 후 LE Advertising Report를 B09 파서로 디코드해 주소·RSSI·AD 구조를 출력하는 C 프로그램을 작성한다. 중복 필터·스캔 윈도/인터벌 조합별 리포트 수를 btvirt 광고자 대상으로 측정한다. | **linux** |
| WCONN-M08 | BlueZ D-Bus GATT 서버: 프로비저닝 서비스 | SSID 목록 읽기·자격증명 쓰기·상태 notify 특성 3개를 갖는 커스텀 GATT 서비스를 D-Bus 객체(GattService1/GattCharacteristic1)로 구현하고 광고를 등록한 뒤, 다른 가상 컨트롤러의 bluetoothctl/gatttool 클라이언트로 읽기·쓰기·notify를 검증한다. MTU를 넘는 자격증명의 분할 쓰기(Prepare/Execute Write)도 처리한다(ATT 서버 원리는 see:RF-M06). | **linux** |
| WCONN-M09 | DHCP 클라이언트 상태기계와 연결 시간 측정 | RFC 2131의 INIT→SELECTING→REQUESTING→BOUND→RENEWING→REBINDING 상태기계(지수 백오프 재전송, xid 검사, T1=0.5·T2=0.875 lease)와 옵션 TLV 인코더/파서를 구현하고 mock 서버로 offer 유실·NAK·lease 만료를 테스트한다. 연결 이벤트 타임라인(assoc→4-way→DHCP→첫 패킷)에서 각 구간 지연을 측정하는 계측을 넣는다(UDP/IP 구현 자체는 see:NET). | mac |
| WCONN-M10 | 연결 관리 데몬: 다중 링크 정책과 failover | 링크별 상태(carrier, IP, 연결성 검사 HTTP 204 결과, 손실률)를 이벤트로 받아 route metric과 기본 경로를 바꾸는 정책 엔진을 구현하고, 히스테리시스·쿨다운으로 flapping을 막으며 LTE 데이터 비용을 고려한 '필요할 때만 LTE' 정책을 넣는다. netns + veth로 세 링크를 흉내 내고 링크 장애 주입 시 전환 시간과 끊김을 측정한다(라우팅/netns 상세는 see:NET). | **linux** |
| WCONN-M11 | 리눅스 셀룰러: QMI/MBIM 메시지와 ModemManager 모델 | QMUX 헤더(I/F type, length, service, client id) + QMI SDU(transaction id, message id) + TLV, 그리고 MBIM 메시지 헤더·Command(service UUID, CID) 인코더/파서를 구현해 libqmi/libmbim 문서의 예제 바이트를 디코드하고, ModemManager 상태(disabled→enabling→registered→connecting→connected)를 따라가는 mock 모뎀으로 신호 소실·재등록 시 bearer 재연결을 테스트한다(AT/URC 파서 기초는 see:RF-M08). | mac |
| WCONN-A01 | 엔터프라이즈 Wi-Fi: 802.1X/EAP-TLS | hwsim 랩에서 hostapd 내장 EAP 서버로 WPA2/WPA3-Enterprise EAP-TLS를 구성(자체 CA·서버·디바이스 인증서)하고, 캡처한 EAP-Request/Response·TLS 단편화(L/M 비트)를 파서로 재조립해 흐름을 재구성한 뒤 MSK→PMK 유도와 만료 인증서·잘못된 CA·서버 이름 불일치 실패를 재현해 로그 시그니처를 정리한다(TLS 기초는 see:SEC-M10). | **linux** |
| WCONN-A02 | hwsim + wmediumd 로밍·장애 주입 통합 테스트 | AP 2대(동일 SSID, 다른 채널, 802.11r 활성)와 STA 1대를 hwsim에 띄우고 wmediumd로 링크별 SNR/손실을 시간에 따라 바꿔 '이동'을 흉내 낸 뒤, pytest 하네스가 로밍 발생·단절 시간(마지막 성공 ping → 첫 성공 ping)·재인증 종류(FT vs full)를 로그에서 추출해 SLA를 판정하게 만든다. M06 엔진의 예측과 실측을 비교한다. | **linux** |
| WCONN-A03 | Wi-Fi 절전: PS-Poll·DTIM·U-APSD·TWT 모델링 | 비콘 주기·DTIM·listen interval·keepalive(TCP/MQTT ping) 주기·버퍼링된 다운링크 도착을 입력으로 legacy PS-Poll, U-APSD, 802.11ax TWT 각각의 평균 전류와 다운링크 지연 분포를 이벤트 시뮬레이션으로 계산하고, AP의 NAT/세션 타임아웃 때문에 keepalive를 줄일 수 없는 상황에서 최적 설정을 찾는다(듀티사이클·배터리 모델은 see:RF-A04). | mac |
| WCONN-A04 | 무선 펌웨어 크래시 감지·덤프·복구 | 가상 '무선 칩' 커널 모듈에 펌웨어 heartbeat 타임아웃·명령 응답 타임아웃을 감지하는 watchdog을 넣고 dev_coredumpv()로 덤프를 /sys/class/devcoredump에 노출한 뒤, 사용자공간 수집기가 덤프를 저장하고 칩 리셋 → 펌웨어 재로드(B11) → cfg80211 재등록 → 재연결을 수행하게 만든다. 복구 루프 폭주를 막는 재시도 예산과 iwlwifi/ath11k의 실제 recovery 흐름을 비교한다. | **linux** |
| WCONN-A05 | 디바이스 프로비저닝 종합: BLE 온보딩 → Wi-Fi 연결 | M08 GATT 서비스 위에 ECDH(X25519) + proof-of-possession(기기 라벨 PIN) 기반 세션 키 수립과 AES-GCM 자격증명 암호화를 얹고, 수신한 SSID/PSK를 M04 ctrl_iface로 적용해 연결 결과(잘못된 PSK, AP 없음, DHCP 실패)를 notify로 보고하는 온보딩 흐름을 구현한다. SoftAP·Wi-Fi Easy Connect(DPP)·QR 방식과 보안/UX를 비교하고 이후 클라우드 인증서 등록과의 연결을 설명한다(인증서 수명 관리는 see:SYSD-M08). | **linux** |
| WCONN-A06 | Matter·Thread 개요: SPAKE2+ 커미셔닝 | RFC 9383 SPAKE2+(P-256) 프로토콜을 Python으로 구현해 RFC 테스트 벡터로 검증하고, 이를 이용한 Matter PASE 세션 수립 → 운영 인증서(NOC) 설치 → CASE 세션 흐름을 메시지 시퀀스 다이어그램과 시뮬레이터로 재현한다. Thread 파티션·border router가 IPv6로 Wi-Fi/Ethernet과 연결되는 방식을 정리한다. | mac |
| WCONN-A07 | Fleet 연결성 신뢰성 분석 | 합성 fleet 이벤트 로그(disconnect reason/status code, 4-way 타임아웃, DHCP 지연, RSSI, AP OUI·대역, 펌웨어 버전)를 생성하고, reason code 분류표·연결 성공률·time-to-reconnect SLO 대시보드 데이터를 계산하는 분석기를 작성한다. 특정 AP 벤더×펌웨어 버전 조합의 이상을 찾아내고(버전 회귀 통계 검정은 see:INFRA-A06), 조치(펌웨어 롤백 vs 설정 회피)를 제안하는 리포트를 만든다. | mac |
| WCONN-A08 | Wi-Fi 연결 장애 트리아지 케이스 스터디 | hwsim 랩에서 잘못된 PSK(메시지 2 MIC 실패), AP 접속 수 초과(status 17), 비콘 손실, PMF 불일치, 규제 도메인 불일치로 인한 채널 미검출, DHCP 서버 무응답 6가지 장애를 재현해 로그·캡처를 수집하고, 이를 입력받아 원인과 근거 라인을 출력하는 규칙 기반 트리아지 도구를 작성해 면접식 '연결이 안 돼요' 질문에 답하는 디버그 순서도를 정리한다. | **linux** |

### RCTL — 로봇 제어 알고리즘 (Robot Control Algorithms)

로봇·모터 펌웨어의 핵심인 제어 알고리즘(PID, 상태공간, LQR, Kalman, FOC, 기구학, 경로계획)을 시뮬레이션 플랜트 위에서 직접 구현하는 트랙이다. Nimble Robotics 리크루터 단계 탈락의 교훈처럼 로보틱스 펌웨어 면접은 FOC·CAN 기반 모터 제어와 PID 실무 디테일(anti-windup, 이산화, 미분 필터)을 깊게 묻는다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| RCTL-B01 | 플랜트 모델과 개루프 응답 | 1차 모터 속도 모델 τ·dω/dt + ω = K·u를 이산 시뮬레이션(C)으로 만들고 스텝 응답 로그에서 K와 τ를 63.2% 규칙과 최소제곱으로 식별해 참값과 비교한다(모델링 심화는 see:SIM). | mac |
| RCTL-B02 | 이산 PID 구현과 기본 튜닝 | 1kHz 고정 주기 위치형 PID를 C로 구현해 B01 플랜트를 폐루프로 시뮬레이션하고, Kp/Ki/Kd 변화에 따른 상승시간·오버슈트·정상상태 오차 표를 만든 뒤 Ziegler-Nichols 계단 응답법으로 초기 이득을 정한다. | mac |
| RCTL-B03 | Anti-windup: Clamping vs Back-calculation | 출력 포화가 있는 플랜트에서 큰 스텝 명령 시 windup 현상을 재현하고, 조건부 적분(clamping)과 back-calculation(Kt 이득) anti-windup을 구현해 오버슈트·정착시간을 비교한다. | mac |
| RCTL-B04 | 미분항: 필터와 Derivative-on-Measurement | 측정 잡음이 있는 시뮬레이션에서 순수 미분, 1차 LPF 미분(N 계수), derivative-on-measurement를 비교해 출력 잡음 RMS와 setpoint kick 크기를 측정한다. | mac |
| RCTL-B05 | 이산화 방법과 증분형 PID | 연속 PI/PID를 forward Euler, backward Euler, Tustin으로 이산화한 계수를 유도·구현해 샘플 주기를 늘려가며 응답 차이를 보이고, 증분형 PID로 bumpless 수동→자동 전환을 구현한다. | mac |
| RCTL-B06 | Cascade 제어: 위치 → 속도 → 전류 | 2차 모터 모델(전류·속도·위치)에 내부 전류 PI(10kHz)·속도 PI(1kHz)·위치 P(100Hz) cascade를 구현하고, 내부 루프 대역폭이 충분히 높지 않을 때 외부 루프가 불안정해지는 것을 보인다. | mac |
| RCTL-B07 | 사다리꼴 속도 프로파일 | 최대 속도·가속도 제한으로 사다리꼴 프로파일(최고속도 도달 못 하는 삼각 프로파일 포함)을 생성하는 함수를 작성해 고정 주기 샘플링 시 최종 위치 오차·속도 연속성을 검증한다. | mac |
| RCTL-B08 | S-curve(저크 제한) 프로파일 | 최대 저크·가속도·속도 제한의 7구간 S-curve를 생성(제한에 따라 구간이 사라지는 경우 포함)하고, B07 사다리꼴과 동일 이동에서 소요 시간·가속도 연속성을 비교한다. | mac |
| RCTL-B09 | 피드포워드: 속도·가속도 FF | B06 서보에 속도·가속도(관성) 피드포워드를 추가하고 B07/B08 궤적 추종 시 추종 오차 RMS·최대값을 FF 유무별로 비교, 모델 오차(관성 ±20%)에 대한 민감도를 측정한다. | mac |
| RCTL-B10 | 2-link 팔 정기구학(FK)과 작업공간 | 평면 2-link(l1,l2) FK 함수를 구현하고 관절 제한을 고려한 작업공간 경계를 샘플링으로 그리며, 동차 변환 행렬로 일반화한 FK와 결과가 일치하는지 테스트한다. | mac |
| RCTL-B11 | 2-link 역기구학(IK): 해석해와 특이점 | 코사인 법칙 기반 해석적 IK(atan2 사용)를 구현하고 FK∘IK 왕복 오차, 도달 불가 입력, 완전히 펴진 특이 자세 근처 수치 거동을 테스트한다. | mac |
| RCTL-M01 | 상태공간 모델과 이산화 | DC 모터(전류·속도·위치) 상태공간 A,B,C를 세우고 행렬 지수(Padé 또는 급수)로 ZOH 이산화를 구현해 Euler 이산화와 비교; 가제어성·가관측성 행렬 랭크를 확인하는 작은 행렬 라이브러리를 C로 작성한다. | mac |
| RCTL-M02 | 극배치 상태 피드백과 Luenberger 관측기 | M01 모델에 Ackermann 공식으로 극배치 이득 K를 계산하고, 속도만 측정할 때 Luenberger 관측기로 전체 상태를 추정해 관측기 극 위치별 추정 수렴과 폐루프 응답을 비교한다. | mac |
| RCTL-M03 | LQR: 이산 Riccati와 도립진자 | 선형화된 카트-폴 도립진자 모델에 이산 대수 Riccati 방정식을 반복으로 풀어 LQR 이득을 계산하고, Q/R 가중치 변화에 따른 응답·입력 크기를 비교; 비선형 모델 시뮬레이션에서 수렴 영역을 확인한다. | mac |
| RCTL-M04 | 1D Kalman 필터: 위치·속도 추정 | 등속 모델 + 잡음 위치 측정 시뮬레이션에서 2상태 Kalman 필터를 C로 구현하고, Q/R 변화에 따른 추정 지연·잡음을 비교하며 NIS/NEES 일관성 검사로 튜닝이 맞는지 판정한다. | mac |
| RCTL-M05 | EKF: 비선형 2D 로봇 위치 추정 | unicycle 모션 모델 + 랜드마크 range-bearing 측정으로 EKF를 구현(자코비안 해석적 유도, 각도 wrap 처리)하고 오도메트리만 쓴 추정 대비 오차와 공분산 타원 일관성을 비교한다. | mac |
| RCTL-M06 | 자코비안과 속도 IK (Damped Least Squares) | 평면 3-link 팔의 자코비안을 구하고 의사역행렬 vs damped least squares 반복 IK로 목표 위치를 추종시켜 특이점 근처 관절 속도 폭주와 감쇠 계수 효과를 비교한다. | mac |
| RCTL-M07 | Clarke/Park 변환 (float·고정소수점) | Clarke(abc→αβ), Park(αβ→dq)와 역변환을 float와 Q15(DSP-B11 LUT sin/cos 재사용)로 구현하고, 평형 3상 입력에서 dq가 상수가 되는지·왕복 오차·전력 불변/진폭 불변 스케일 차이를 테스트한다. | mac |
| RCTL-M08 | SVPWM 구현 | αβ 전압 명령에서 섹터 판정·인접 벡터 작용 시간·3상 듀티를 계산하는 SVPWM과 min-max 주입 방식 등가 구현을 작성해 둘이 일치함을 확인하고, 사인 PWM 대비 DC 버스 이용률 15.5% 향상을 보인다. | mac |
| RCTL-M09 | FOC 전류 루프: PMSM 모델 위에서 | dq 좌표 PMSM 모델(Rs, Ld, Lq, λ, 극쌍)을 시뮬레이션하고 Clarke/Park→d/q PI(id=0 제어)→역Park→SVPWM 파이프라인을 20kHz로 구현해 토크 스텝 응답, 교차결합 디커플링 효과, 전압 포화를 확인한다. | mac |
| RCTL-M10 | FOC 속도 루프와 엔코더 각도 정렬 | 엔코더 기계각→전기각 변환과 초기 정렬 절차(d축 전류 주입)를 구현하고, 정렬 오차가 있을 때 토크 상수 저하를 측정한 뒤 M09 위에 속도 PI cascade를 붙여 부하 외란 응답을 평가한다. | mac |
| RCTL-M11 | 모터 파라미터 식별: R·L·Kt·관성 최소제곱 추정 | 시뮬레이션한 DC/PMSM 모터에 전압 스텝·정현 입력을 주고 전류·속도 로그로 R, L, Kt, J, 마찰을 최소제곱으로 추정해 참값 대비 오차와 노이즈 민감도를 평가한다(경로 계획은 see:AUTON). | mac |
| RCTL-M12 | 다축 보간: 선형·원호 보간과 G-code 실행 | G0/G1/G2/G3 파서와 B07 사다리꼴 프로파일을 결합한 다축 선형·원호 보간기를 구현하고, 경로 오차와 코너 속도 제한(junction deviation)을 측정한다. | mac |
| RCTL-A01 | MPC 입문: 제약 있는 이중적분기 | 이중적분기에 입력·상태 제약이 있는 선형 MPC(예측 구간 N)를 QP로 구성하고 projected gradient 또는 간단한 active-set으로 풀어 LQR(제약 무시+포화) 대비 제약 준수·성능을 비교하며 계산 시간을 측정한다. | mac |
| RCTL-A02 | 센서리스 FOC: 관측기와 PLL | M09 PMSM 모델에서 back-EMF 관측기(또는 sliding-mode observer) + PLL로 각도·속도를 추정해 FOC에 사용하고, 저속 영역 추정 실패와 오픈루프 기동→폐루프 전환 로직을 구현한다. | mac |
| RCTL-A03 | 주파수 응답 식별과 안정 여유 | 플랜트에 chirp/sine-sweep을 넣어 입출력으로 주파수 응답을 추정하고 개루프 Bode 선도에서 이득·위상 여유를 계산; 릴레이 피드백 auto-tune으로 임계 이득·주기를 구해 PID를 자동 설정한다. | mac |
| RCTL-A04 | 제어 루프 실시간 구현: 지연·지터의 영향 | B06 서보 시뮬레이션에 계산 지연(0~1 샘플)과 랜덤 지터를 넣어 위상 여유 손실(ω·Td)을 이론·시뮬레이션으로 비교하고, 지연 보상(상태 예측)과 '측정 즉시 출력' 스케줄링 패턴을 구현한다(see:RTOS). | mac |
| RCTL-A05 | 2-link 동역학과 Computed Torque 제어 | 라그랑지안으로 2-link 팔의 M(q), C(q,q̇), G(q)를 유도·구현해 시뮬레이션하고 독립 관절 PD와 computed torque 제어의 궤적 추종 오차를 비교, 질량 파라미터 오차에 대한 강건성을 평가한다. | mac |
| RCTL-A06 | 이동 로봇 궤적 추종: Pure Pursuit·Stanley | unicycle/bicycle 모델에서 pure pursuit(lookahead 거리)와 Stanley 제어기를 구현해 곡선 경로 추종 시 횡오차·조향 입력을 속도별로 비교한다. | mac |
| RCTL-A07 | CAN 기반 모터 드라이브 인터페이스 (CiA 402) | CiA 402 드라이브 상태기계(Switch On Disabled→Ready→Switched On→Operation Enabled, Fault)와 controlword/statusword, RPDO/TPDO 매핑, SYNC 기반 주기 위치 명령을 C로 구현하고 가상 CAN 버스(host 소켓 mock)로 다축 동기 이동·하트비트 손실 시 안전 정지를 테스트한다. | mac |
| RCTL-A08 | 임피던스 제어 1-DOF | 스프링-댐퍼 벽 접촉 모델을 가진 1-DOF 액추에이터 시뮬레이션에서 순수 위치 제어와 임피던스 제어(가상 M, B, K)를 비교해 접촉력 피크·안정성을 측정한다. | mac |
| RCTL-A09 | 모터 제어 안전·진단과 CI 테스트 | 과전류·과전압·엔코더 단선·추종 오차 초과·통신 타임아웃 결함 검출과 반응(토크 차단, 제어 정지, 브레이크)을 구현하고, 플랜트 시뮬레이션 기반 폐루프 테스트를 CI에서 돌리는 하네스(시나리오 + 합격 기준)를 만든다. | mac |

### DIST — 분산 시스템 (Distributed Systems)

여러 ECU·드론·로봇·클라우드가 함께 동작하는 시스템에서는 시간, 순서, 실패, 합의가 핵심 문제다. 분산 시스템 개념(Lamport/vector clock, Raft, 재시도·멱등성, PTP 시간 동기, 직렬화)을 임베디드 맥락의 구현 문제로 풀어 fleet·자율 시스템·자동차 회사 면접에 대비한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| DIST-B01 | 결함 주입 가능한 메시지 시뮬레이터 | 노드·메시지·가상 시간을 가진 Python 이산사건 시뮬레이터에 지연 분포, 유실, 중복, 재정렬, 네트워크 분할, 노드 crash/restart 주입을 구현하고 시드로 실행이 재현되는지 테스트한다. | mac |
| DIST-B02 | Lamport 논리 시계 | B01 시뮬레이터 노드에 Lamport clock을 붙여 송수신 규칙을 구현하고, 3노드 로그를 (timestamp, node_id)로 전순서 정렬했을 때 인과 관계가 위반되지 않음을 자동 검사한다. | mac |
| DIST-B03 | Vector Clock과 동시성 판정 | vector clock 증가·병합·비교(<, >, concurrent)를 구현하고 3개 ECU 로그 병합 시 동시 이벤트를 표시; Lamport clock으로는 동시성을 판별할 수 없는 반례를 테스트로 보인다. | mac |
| DIST-B04 | 하트비트 장애 검출기 튜닝 | 고정 타임아웃 하트비트 검출기와 phi accrual 검출기를 구현하고, 지연 지터·유실이 있는 시뮬레이션에서 임계값별 평균 검출 시간과 오탐(살아있는데 죽었다 판단) 비율 곡선을 그린다. | mac |
| DIST-B05 | 멱등성·재시도·중복 제거 | 유실 링크 위 요청/응답에 시퀀스 번호·재시도(지수 백오프 + jitter)·수신 측 dedup 윈도를 구현하고, 비멱등 명령(카운터 증가)이 중복 적용되지 않음을 결함 주입 테스트로 검증한다. | mac |
| DIST-B06 | 직렬화 기초: Varint·ZigZag·TLV | varint, zigzag, tag(field<<3|wire_type) 인코딩으로 간단한 메시지 인코더/디코더를 C로 작성해 protoc/python protobuf 출력과 바이트 단위로 비교하고, 필드 추가/삭제 시 하위 호환이 유지되는 규칙을 테스트한다. | mac |
| DIST-B07 | Zero-copy 직렬화 (FlatBuffers 개념) | offset 테이블(vtable 유사) 기반 zero-copy 메시지 리더를 C로 구현하고 B06 방식 대비 디코딩 시간·메시지 크기·정렬 요구·검증(verifier) 필요성을 비교하는 표를 만든다. | mac |
| DIST-B08 | Pub/Sub 브로커와 QoS | 와일드카드 토픽 매칭, QoS 0(최대 1회)/QoS 1(최소 1회, ack+재전송), retained message, 느린 구독자 backpressure(큐 제한, drop 정책)를 가진 in-process 브로커를 구현해 MQTT 의미론과 비교한다. | mac |
| DIST-B09 | NTP 오프셋·지연 계산과 클럭 규율 | t1~t4로 offset·round-trip delay를 계산하고 비대칭 지연이 만드는 오차를 보이며, 최소 지연 샘플 필터와 PI 루프로 로컬 시계 주파수를 조정(slew)하는 클럭 규율을 시뮬레이션해 수렴을 측정한다. | mac |
| DIST-B10 | 리더 선출: Bully 알고리즘 | B01 시뮬레이터에서 bully 알고리즘을 구현해 리더 crash·복귀 시나리오를 테스트하고, 네트워크 분할 시 두 리더가 생기는(split brain) 반례를 재현해 이후 Raft가 필요한 이유를 정리한다. | mac |
| DIST-M01 | PTP(IEEE 1588) 시간 동기 | two-step Sync/Follow_Up/Delay_Req/Delay_Resp 교환으로 offset·mean path delay를 계산하는 시뮬레이션을 만들고, 소프트웨어 vs 하드웨어 타임스탬프 지터, 비대칭 링크 오차, transparent clock의 correctionField 보정 효과를 정량화한다. | mac |
| DIST-M02 | BMCA와 gPTP 그랜드마스터 선택 | priority1/clockClass/accuracy/variance/priority2/clockIdentity 비교 기반 BMCA를 구현하고 그랜드마스터 장애·복귀 시 재선택과 시간 점프를 시뮬레이션해 802.1AS(gPTP) 자동차 네트워크에서의 의미를 정리한다. | mac |
| DIST-M03 | Raft 1: 리더 선출과 Term | B01 시뮬레이터에서 Raft follower/candidate/leader 상태, RequestVote, 랜덤 election timeout을 구현해 분할·지연 상황에서도 term당 리더가 최대 1명임을 수천 개 시드로 검사한다. | mac |
| DIST-M04 | Raft 2: 로그 복제와 Commit | AppendEntries(prevLogIndex/Term 일치 검사, 충돌 로그 삭제), commitIndex 갱신(현재 term 항목 과반 규칙)을 구현하고 분할·crash 결함 주입에서 커밋된 항목이 사라지지 않음을 자동 검증한다. | mac |
| DIST-M05 | 복제 모델: Primary-Backup vs Quorum | primary-backup 복제와 N/R/W quorum 복제 KV 저장소를 시뮬레이터에 구현하고, R+W>N 조건 위반 시 오래된 값 읽기를 재현하며 노드 장애별 가용성을 표로 비교한다. | mac |
| DIST-M06 | CAP/PACELC 설계 연습: 차량 fleet 설정 저장소 | 수만 대 차량이 오프라인을 자주 겪는 환경에서 기능 플래그/설정 배포 저장소를 설계(강한 일관성 vs 최종 일관성, 버전 벡터, 충돌 해결)하는 설계 문서를 쓰고, 디바이스 측 설정 동기 코드 경로를 구현한다. | mac |
| DIST-M07 | CRDT로 오프라인 우선 상태 동기 | G-Counter, PN-Counter, LWW-Register, OR-Set을 구현하고 임의 순서·중복 병합에서도 수렴(교환·결합·멱등 법칙)함을 property-based 테스트로 검증한다. | mac |
| DIST-M08 | Gossip 멤버십 (SWIM) | SWIM 스타일 ping/ping-req 간접 확인과 감염식(piggyback) 전파를 구현해 노드 수 N별 장애 전파 시간과 메시지 부하를 측정하고, 전체 하트비트 방식(O(N²))과 비교한다. | mac |
| DIST-M09 | Chandy-Lamport 분산 스냅샷 | 토큰을 주고받는 노드들에 marker 기반 Chandy-Lamport 스냅샷을 구현하고, 스냅샷 시점의 전체 토큰 수(노드 상태+채널 상태)가 보존됨을 검증한다. | mac |
| DIST-M10 | Fleet Telemetry 디바이스 측 파이프라인 | 디바이스 측 배칭·압축·store-and-forward(전원 손실 안전 큐)·백오프 업로드와 서버 측 (device_id, seq) 기반 중복 제거·순서 복원을 구현하고, 장기 오프라인·재부팅·중복 전송 시나리오에서 데이터 손실 0을 검증한다. | mac |
| DIST-A01 | Time-Triggered 멀티 ECU 스케줄 | 메시지 집합(주기, 길이, 송신 ECU, 마감)을 받아 TDMA/time-triggered 슬롯 스케줄을 생성하는 알고리즘(하이퍼피리어드, 그리디 배치)을 구현하고 충돌·마감 위반이 없음을 검증, FlexRay/TTEthernet 개념과 대응시킨다. | mac |
| DIST-A02 | 중복 비행 컴퓨터: TMR 투표와 Byzantine | 3중 중복 센서/컴퓨터 출력의 다수결·mid-value select를 구현해 단일 결함 마스킹을 확인하고, 4노드 OM(1) oral messages 알고리즘으로 배신 노드 1개를 견디며 3노드로는 불가능한 반례를 재현한다. | mac |
| DIST-A03 | 드론 군집 분산 합의(Average Consensus) | 통신 그래프 위에서 x_i += ε·Σ(x_j − x_i) 평균 합의를 구현해 그래프 연결성(대수적 연결도)·ε·링크 유실에 따른 수렴 속도를 측정하고, 대형 비행 위치 합의 시나리오에 적용한다. | mac |
| DIST-A04 | Paxos(단일 결정) 구현과 Raft 비교 | single-decree Paxos의 proposer/acceptor/learner를 시뮬레이터에 구현해 경쟁 proposer·분할에서 안전성(하나의 값만 선택)을 검증하고, livelock 재현과 Raft와의 구조적 차이를 정리한다. | mac |
| DIST-A05 | 선형성(Linearizability) 검사기 | 동시 클라이언트 연산 히스토리(invoke/return 시각)를 받아 Wing-Gong 백트래킹으로 선형성 여부를 판정하는 검사기를 구현하고, M05 quorum 저장소의 결함 주입 실행에서 위반 히스토리를 찾아낸다. | mac |
| DIST-A06 | 로봇 텔레메트리용 Reliable UDP | 시퀀스 번호·선택적 재전송(NACK/SACK 비트맵)·메시지 우선순위·최신값 우선(오래된 제어 메시지 폐기)을 가진 UDP 기반 프로토콜을 C로 구현하고, 유실 1~20% 링크에서 지연·처리량을 TCP 유사 방식과 비교한다(DDS/ROS 2 QoS 개념은 see:ROS). | mac |
| DIST-A07 | 다중 노드 로그 상관과 클럭 스큐 추정 | 서로 다른 오프셋·드리프트를 가진 3개 노드 로그에서 송수신 쌍(인과 제약)을 이용해 노드별 클럭 스큐를 추정(선형 계획/회귀)하고 보정된 통합 타임라인을 생성해 장애 전파 순서를 재구성한다. | mac |

## 6단계 · 비주얼 컴퓨팅 (Visual Computing)

### CAM — 카메라 · 이미지 신호처리 (Camera & ISP)

카메라 파이프라인은 이미지 센서 → MIPI CSI-2 수신 → ISP → 3A 제어 → 버퍼/DMA → 드라이버(V4L2)로 이어지는, 펌웨어·하드웨어·신호처리가 한데 얽힌 대표 시스템이다. 자율주행·드론·로봇(Anduril, Tesla, Rivian, Apple 카메라 팀)에서는 센서 bring-up, 프레임 타이밍, ISP 파라미터 검증이 단골 면접 주제다. 이 트랙은 RAW 버퍼 위에서 ISP 단계를 직접 구현하며 각 단계의 의미와 하드웨어 구현 제약을 체득한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| CAM-B01 | 이미지 센서 기초: 픽셀·Full Well·노이즈 모델 | QE, full well, conversion gain, read noise, dark current 파라미터로 픽셀 응답을 시뮬레이션하는 C/Python 모델을 만들고, 노출 시간을 바꿔 가며 SNR 곡선을 그려 shot-noise 영역에서 SNR ∝ sqrt(signal)임을 검증한다. | mac |
| CAM-B02 | Rolling Shutter vs Global Shutter 시뮬레이션 | 움직이는 막대 장면을 line time 기반으로 행마다 다른 시각에 샘플링하는 시뮬레이터를 작성해 rolling shutter skew 이미지를 생성하고, 측정된 기울기가 (속도 × line_time × 행 수) 예측값과 일치하는지 검증한다. | mac |
| CAM-B03 | Bayer CFA 패턴과 RAW10/RAW12 패킹 포맷 | RGB 이미지를 4가지 Bayer 순서로 모자이크하는 함수와, MIPI RAW10(4픽셀/5바이트)·RAW12(2픽셀/3바이트) pack/unpack 함수를 C로 구현하고 왕복(roundtrip) 비트 일치를 단위 테스트로 검증한다. | mac |
| CAM-B04 | 센서 레지스터 맵과 I2C 초기화 시퀀스 | 가상 센서(16비트 주소 I2C 레지스터 맵)를 mock으로 만들고 {reg, val, delay} 테이블 기반 초기화 드라이버를 작성해 readback 검증을 수행한 뒤, pixel clock·line_length_pck·frame_length_lines로 fps와 최대 노출 시간을 계산하는 함수를 테스트한다. | mac |
| CAM-B05 | MIPI CSI-2 패킷 구조: Short/Long Packet, ECC, CRC | CSI-2 바이트 스트림에서 FS/FE/LS/LE short packet과 RAW10 long packet을 파싱하는 디코더를 작성하고, 6비트 헤더 ECC(단일 비트 정정)와 CRC-16 페이로드 체크를 구현해 비트 오류 주입 테스트로 정정/검출 동작을 검증한다. | mac |
| CAM-B06 | Black Level 보정과 Defect Pixel 보정 | Optical black 행의 평균으로 채널별 black level을 추정해 빼는 함수와, 같은 색 이웃 8개의 median과 비교해 결함 픽셀을 찾아 대체하는 DPC를 구현하고, 결함을 주입한 합성 RAW에서 검출률·오검출률을 측정한다. | mac |
| CAM-B07 | Demosaic 기초: Bilinear 보간 | RGGB RAW에 대해 bilinear demosaic을 경계 처리 포함으로 구현하고, 원본 RGB → 모자이크 → 복원 결과의 채널별 PSNR을 계산하며 에지 부근 false color 아티팩트를 확대 이미지로 확인한다. | mac |
| CAM-B08 | 화이트 밸런스: Gray World와 White Patch | Gray World와 White Patch(상위 백분위) AWB 게인 추정을 구현하고, 인위적으로 색 캐스트를 준 이미지에서 회색 패치의 R/G·B/G 비율이 1에 얼마나 가까워지는지 정량 비교한다. | mac |
| CAM-B09 | CCM 색 보정 행렬과 감마 LUT (고정소수점) | 3x3 CCM(행 합 = 1 제약)을 Q4.10 고정소수점으로 적용하는 함수와 sRGB 감마를 1024-entry LUT + 선형보간으로 구현하고, float 레퍼런스 대비 최대 오차(LSB)와 클리핑 처리를 테스트한다. | mac |
| CAM-B10 | 프레임 버퍼 레이아웃: Stride, Plane, NV12/YUYV | width/height/format/정렬 요구(예: 64바이트)를 받아 plane별 offset·stride·전체 크기를 계산하는 함수와 NV12·YUYV·I420 간 변환기를 작성하고, 홀수 해상도와 패딩 영역을 포함한 테스트로 버퍼 오버런이 없음을 ASan으로 검증한다. | mac |
| CAM-M01 | 고품질 Demosaic: Malvar-He-Cutler | Malvar-He-Cutler 5x5 커널 demosaic을 정수 연산으로 구현하고, 같은 테스트 이미지에서 bilinear 대비 PSNR 향상과 zipper/false color 감소를 수치·이미지로 비교한다. | mac |
| CAM-M02 | 노이즈 제거: Bilateral 공간 NR과 Temporal NR | LUT 기반 range weight를 쓰는 bilateral 필터와, 프레임 차이로 블렌딩 계수를 조절하는 motion-adaptive temporal NR을 구현하고, 합성 노이즈 영상 시퀀스에서 평탄 영역 노이즈 감소량과 움직이는 물체의 ghosting을 측정한다. | mac |
| CAM-M03 | 샤프닝: Unsharp Mask와 Overshoot 제어 | Y 채널에 unsharp mask를 적용하되 노이즈 임계값(coring)과 로컬 min/max 기반 overshoot clamp를 넣어 구현하고, slanted edge에서 에지 기울기 증가와 halo 크기를 측정해 파라미터 영향을 표로 정리한다. | mac |
| CAM-M04 | 렌즈 셰이딩 보정(LSC)과 게인 그리드 보간 | 균일광(flat-field) RAW에서 채널별 17x13 게인 그리드를 추출하는 캘리브레이션 코드와, 그리드를 bilinear 보간해 픽셀별 게인을 적용하는 LSC 블록을 구현하고 보정 후 중심 대비 코너 밝기 편차를 측정한다. | mac |
| CAM-M05 | 렌즈 왜곡 보정: Brown-Conrady 모델과 Remap LUT | Brown-Conrady(k1,k2,p1,p2) 모델로 왜곡 이미지를 만들고, 출력 픽셀마다 입력 좌표를 계산해 bilinear 샘플링하는 remap과 16x16 블록 단위로 압축한 좌표 LUT 버전을 구현해 직선 체커보드의 직선성 오차를 비교한다. | mac |
| CAM-M06 | AE 자동 노출: 센서 지연을 고려한 제어 루프 | 장면 밝기가 계단 변화하는 시뮬레이터에서 가중 평균 휘도 → 목표 대비 오차 → 노출·아날로그 게인 분배를 하는 AE를 작성하고, 레지스터 적용 지연(2프레임)을 넣었을 때 진동 없이 수렴하도록 damping을 설계해 수렴 프레임 수와 overshoot를 측정한다. | mac |
| CAM-M07 | AWB 고급: Gray Zone 통계와 CCT 기반 CCM 보간 | 이미지를 32x24 블록 통계(R,G,B 합)로 축약하고 플랑크 궤적 근처 gray zone 블록만 골라 게인을 추정한 뒤, 추정 CCT로 두 개의 캘리브레이션 CCM을 보간하는 AWB를 구현해 혼합 조명 장면에서 Gray World 대비 오차를 비교한다. | mac |
| CAM-M08 | AF 자동 초점: Contrast 측정과 Hill-Climb 탐색 | 렌즈 위치에 따라 가우시안 블러가 변하는 시뮬레이터에서 Tenengrad/Laplacian 분산 포커스 메트릭을 구현하고, coarse-to-fine hill-climb으로 피크를 찾는 AF를 작성해 이동 횟수와 최종 위치 오차를 측정하며 PDAF가 탐색 횟수를 어떻게 줄이는지 설명을 덧붙인다. | mac |
| CAM-M09 | 라인 버퍼 스트리밍 ISP 파이프라인 통합 | BLC→DPC→Demosaic→AWB→CCM→Gamma→CSC를 각 단계가 필요한 최소 라인 버퍼만 갖는 스트리밍 파이프라인(한 줄 입력 → 지연된 한 줄 출력)으로 재구현하고, 앞서 만든 프레임 기반 레퍼런스와 bit-exact 일치를 검증하며 총 라인 버퍼 SRAM 크기를 계산한다. | mac |
| CAM-M10 | CSI-2 수신기 모델: 레인 병합·Virtual Channel·에러 처리 | 4-lane으로 바이트 분산된 CSI-2 스트림을 병합해 VC별로 프레임/라인을 재조립하는 수신기 상태 머신을 작성하고, ECC 오류·CRC 오류·누락된 FE·WC 불일치를 주입해 카운터 증가와 프레임 drop/복구 동작을 테스트한다. | mac |
| CAM-M11 | 캡처 버퍼 큐와 DMA: qbuf/dqbuf 소유권 모델 | V4L2와 유사한 버퍼 상태(DEQUEUED/QUEUED/ACTIVE/DONE) 머신과 DMA 완료 인터럽트를 흉내내는 생산자 스레드를 C로 구현하고, 소비자가 느릴 때 마지막 버퍼 재사용/프레임 drop 정책, sequence 번호 공백 검출을 테스트한다. | mac |
| CAM-M12 | 프레임 타임스탬프와 센서-IMU 시간 동기 | SOF 인터럽트 시각, 노출 시간, line time, readout 시간으로 프레임의 노출 중심 시각과 행별 타임스탬프를 계산하는 함수를 작성하고, 드리프트가 있는 IMU 클럭과 선형 회귀로 오프셋·스큐를 추정해 정렬 오차를 측정한다. | mac |
| CAM-A01 | V4L2 mmap 스트리밍 캡처 앱 | vivid 가상 카메라 드라이버에 대해 VIDIOC_QUERYCAP/S_FMT/REQBUFS/QBUF/DQBUF/STREAMON을 사용하는 mmap 캡처 프로그램을 작성하고, poll 기반 루프에서 fps, 버퍼 timestamp 간격 지터, sequence 누락을 로그로 분석한다. | **linux** |
| CAM-A02 | Media Controller와 V4L2 Subdev 파이프라인 구성 | vimc 가상 드라이버에서 media-ctl로 토폴로지를 출력하고 링크·패드 포맷을 설정해 파이프라인을 구성한 뒤, 포맷 불일치 시 STREAMON 실패(EPIPE)를 재현하고 원인을 설명하는 스크립트와 노트를 작성한다. | **linux** |
| CAM-A03 | HDR: 다중 노출 합성과 톤 매핑 | 선형 RAW 3장(노출비 1:4:16)을 가중치 기반으로 radiance 맵에 합성하고 움직임 영역 ghost 검출을 넣은 뒤, 글로벌 Reinhard와 로컬 톤 매핑을 구현해 결과를 비교하고 staggered HDR에서 노출 간 시간차가 만드는 아티팩트를 설명한다. | mac |
| CAM-A04 | ISP 파라미터 Per-frame 제어와 Shadow 레지스터 래칭 | ISP 레지스터를 shadow/active 이중 뱅크로 모델링하고 vsync에서만 래치되는 mock 하드웨어를 만든 뒤, 3A 펌웨어가 프레임 N 통계로 계산한 파라미터를 N+k에 원자적으로 적용하는 큐를 구현해 frame-ID 기반 테스트로 레이스(반쯤 적용된 파라미터)를 검출한다. | mac |
| CAM-A05 | 멀티 카메라 하드웨어 동기화 설계 | 외부 트리거와 프리런 마스터-슬레이브 두 방식을 각 센서 클럭 오차(±50ppm)를 넣어 시뮬레이션하고, 프레임 간 시작 시각 차이 분포를 측정한 뒤 frame_length 미세 조정으로 드리프트를 보정하는 제어기를 구현해 설계 트레이드오프를 정리한다. | mac |
| CAM-A06 | ISP 화질 검증: Golden Model과 MTF/SNR 측정 | float golden 파이프라인과 고정소수점 파이프라인 출력을 단계별 diff하는 검증 하네스를 만들고, slanted-edge 방법(ESF→LSF→FFT)으로 MTF50을 계산하는 코드와 평탄 패치 SNR 측정을 구현해 NR/샤프닝 파라미터 변화가 지표에 미치는 영향을 보고한다. | mac |
| CAM-A07 | 카메라 bring-up 디버깅 시나리오 | 지금까지 만든 센서·CSI-2·ISP 시뮬레이터에 Bayer 순서 오류, 레인 수 불일치, RAW10 언패킹 오프셋, black level 미적용, stride 오설정 등 결함을 무작위 주입하는 하네스를 만들고, 각 증상에서 원인을 찾는 진단 트리와 자동 체크 스크립트를 작성한다. | mac |

### DISP — 디스플레이 (Display)

디스플레이는 픽셀 클럭과 블랭킹 타이밍, 프레임버퍼 메모리, 색공간 변환, 합성(compositing), 패널 인터페이스(MIPI DSI/eDP/HDMI)가 실시간 제약 아래 맞물려 돌아가는 시스템이다. 차량 클러스터·HMI, 웨어러블, 스마트폰(Apple, Tesla, Rivian, Qualcomm 디스플레이 팀)에서는 패널 bring-up, 티어링, 대역폭/언더런 분석이 자주 출제된다. 이 트랙은 타이밍 생성기부터 DRM/KMS까지 직접 구현·측정하며 디스플레이 파이프라인을 끝까지 따라간다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| DISP-B01 | 디스플레이 타이밍: Porch·Sync·Blanking과 Pixel Clock | h/v active, front porch, sync width, back porch, refresh rate를 받아 htotal/vtotal과 pixel clock을 계산하는 함수를 작성하고, 1920x1080@60(148.5MHz), 1280x720@60 등 CTA-861 표준 모드 값과 일치하는지 테스트하며 xrandr modeline 문자열을 생성·파싱한다. | mac |
| DISP-B02 | 타이밍 생성기 사이클 시뮬레이션과 VCD 출력 | 픽셀 클럭 단위로 h/v 카운터를 증가시켜 HSYNC, VSYNC, DE, 극성을 생성하는 C 모델을 작성하고 VCD 파일로 덤프해 GTKWave로 확인하며, DE 펄스 수와 sync 폭이 설정값과 일치하는지 assert로 검증한다. | mac |
| DISP-B03 | 프레임버퍼 기초: 픽셀 포맷·Stride·Blit | 포맷별 put_pixel, fill_rect, blit(포맷 변환 포함), 클리핑을 갖춘 소프트웨어 프레임버퍼 라이브러리를 작성해 PPM으로 출력하고, stride ≠ width*bpp 인 버퍼와 화면 밖 좌표에서 메모리 침범이 없음을 ASan으로 확인한다. | mac |
| DISP-B04 | 비트맵 폰트 렌더링과 임베디드 UI 기초 | 8x16 비트맵 폰트 테이블로 문자열을 프레임버퍼에 그리는 함수와, 변경된 영역만 추적해 부분 전송하는 dirty-rectangle 관리자를 작성하고, SPI LCD 전송 바이트 수를 전체 갱신 대비 비교한다. | mac |
| DISP-B05 | RGB↔YUV 변환: BT.601/709, Full/Limited Range | BT.601/BT.709 × full/limited 4가지 조합의 RGB→YUV, YUV→RGB 변환을 Q8 정수 연산으로 구현하고, float 레퍼런스 대비 최대 오차와 모든 24비트 RGB 값에 대한 왕복 오차 분포를 측정한다. | mac |
| DISP-B06 | 크로마 서브샘플링: 4:2:0 업샘플과 NV12→RGB | NV12(4:2:0) 버퍼를 nearest와 bilinear(co-sited/center siting 구분) 크로마 업샘플로 RGB 변환하는 함수를 작성하고, 빨간 글자 테스트 패턴에서 색 번짐(color bleeding) 차이를 비교한다. | mac |
| DISP-B07 | 감마와 디더링: Ordered vs Floyd-Steinberg | 8비트 그라디언트를 6비트/5-6-5로 줄일 때 단순 절단, 4x4 Bayer ordered dithering, Floyd-Steinberg 오차 확산을 구현하고, 결과 이미지와 저역 필터 후 평균 오차·밴딩 단계 수를 비교한다. | mac |
| DISP-B08 | 알파 블렌딩과 Porter-Duff: Premultiplied Alpha | Porter-Duff SRC_OVER/SRC_IN/DST_OUT을 straight와 premultiplied 두 방식으로 구현하고, x*a/255를 (t=x*a+128; (t+(t>>8))>>8)로 근사하는 정수 트릭이 65536 조합 전체에서 정확히 반올림됨을 전수 검증한다. | mac |
| DISP-B09 | 백라이트 PWM과 인지 밝기 곡선 | CIE 1931 lightness 공식으로 0–100% 밝기 단계를 12비트 PWM 듀티 LUT로 변환하는 함수와 일정 시간에 걸친 부드러운 fade 램프 생성기를 구현하고, 선형 매핑 대비 저휘도 구간 단계 크기와 PWM 주파수(예: >1kHz) 선택 근거를 정리한다. | mac |
| DISP-M01 | 더블/트리플 버퍼링과 페이지 플립 상태 머신 | 렌더 스레드와 vblank마다 스캔아웃 주소를 래치하는 가상 디스플레이 컨트롤러를 C로 만들고, 단일 버퍼(티어링 발생 탐지), 더블 버퍼(플립 대기), 트리플 버퍼 상태 머신을 구현해 프레임 ID 체크로 티어링·중복 프레임을 검출한다. | mac |
| DISP-M02 | 티어링·Vsync·지연 분석: FIFO vs Mailbox | 렌더 시간 분포가 다른 워크로드에서 vsync off, FIFO(vsync), mailbox(트리플) 프레젠테이션을 이벤트 시뮬레이션하고, 입력-표시 지연 분포, 티어링 발생 비율, 프레임 페이싱 지터를 표로 비교한다. | mac |
| DISP-M03 | 하드웨어 오버레이 Plane과 스캔라인 합성기 | plane마다 포맷·위치·크기·z-order·전역/픽셀 알파를 갖는 합성기를 스캔라인 단위로 구현하고, 하드웨어 plane 수 제약 아래 레이어들을 plane에 배정하거나 GPU 합성으로 폴백하는 할당 알고리즘을 작성해 테스트 장면으로 검증한다. | mac |
| DISP-M04 | 디스플레이 스케일러: Polyphase 필터 | 8-phase × 4-tap 계수 테이블(Lanczos 기반, 계수 합 정규화)을 생성하고 고정소수점 polyphase 수평/수직 스케일러를 구현해, bilinear 대비 해상도 차트에서 aliasing과 ringing을 비교한다. | mac |
| DISP-M05 | 패널 초기화 시퀀스: Reset 타이밍과 DCS 명령 | 전원 레일/reset/지연 단계를 포함한 테이블 기반 패널 init 엔진과 mock DSI 호스트를 작성해 sleep out(0x11) → 120ms → display on(0x29) 등 시퀀스를 실행하고, 순서 위반·지연 부족을 검출하는 검사기를 단위 테스트로 검증한다. | mac |
| DISP-M06 | MIPI DSI 패킷과 Video/Command 모드 | DSI short/long packet(DI, WC, ECC, checksum) 인코더를 작성하고, 해상도·fps·bpp·레인 수로 필요한 레인 비트레이트를 계산하는 함수와 command 모드에서 TE 신호 이후 쓰기 시작 시각에 따른 티어링 조건을 모델링한다. | mac |
| DISP-M07 | EDID 파싱과 모드 선택 | 128바이트 base EDID와 CTA-861 확장 블록을 파싱해 제조사 ID, 체크섬, Detailed Timing Descriptor, 표준 타이밍, SVD 목록을 출력하는 C 파서를 작성하고, 실제 모니터 EDID 덤프 샘플과 손상된 체크섬 입력으로 테스트한다. | mac |
| DISP-M08 | HDMI TMDS 인코딩과 DP 링크 대역폭 | TMDS 인코더(XOR/XNOR 선택 + running disparity 기반 반전)와 디코더를 구현해 무작위 데이터 왕복과 disparity 유계를 검증하고, DP HBR2/HBR3 레인 수별 유효 대역폭으로 4K@60 10bpc 지원 여부를 계산한다. | mac |
| DISP-M09 | 디스플레이 컨트롤러 드라이버 (Mock 레지스터) | mock LCD 컨트롤러 레지스터 맵(타이밍, FB 주소, 제어, IRQ 상태)을 정의하고 set_mode, flip(다음 vblank 래치), vblank 콜백, FIFO underrun IRQ 복구를 구현한 드라이버를 작성해, vblank 경계 근처 flip 요청이 정확히 한 프레임 뒤에 반영되는지 테스트한다. | mac |
| DISP-A01 | DRM/KMS Dumb Buffer 모드셋과 페이지 플립 | vkms 가상 드라이버에서 libdrm으로 connector와 모드를 열거하고 dumb buffer 두 개를 만들어 drmModeSetCrtc 후 drmModePageFlip + 이벤트 루프로 애니메이션을 출력하며, flip 이벤트 타임스탬프 간격을 기록해 refresh 주기와 비교한다. | **linux** |
| DISP-A02 | Atomic Modesetting과 Plane 속성 | vkms에서 primary + overlay/cursor plane 속성(FB_ID, CRTC_X/Y/W/H, SRC_*)을 atomic request로 묶어 커밋하고, DRM_MODE_ATOMIC_TEST_ONLY로 지원되지 않는 구성을 사전 거절하는 흐름과 legacy API 대비 장점을 정리한다. | **linux** |
| DISP-A03 | 디스플레이 메모리 대역폭과 FIFO 언더런 분석 | plane 구성(해상도, bpp, 스케일)별 스캔아웃 대역폭을 계산하고, 메모리 지연이 확률 분포로 변하는 DRAM 모델 위에서 FIFO 깊이별 언더런 확률을 시뮬레이션해 목표(예: 1e-9 미만)를 만족하는 최소 FIFO와 QoS 우선순위 필요성을 정리한다. | mac |
| DISP-A04 | HDR 디스플레이: PQ/HLG 전달 함수와 톤 매핑 | PQ EOTF/inverse EOTF와 HLG OETF를 구현해 표준 기준값(예: 10000nit ↔ 1.0, 100nit ↔ 약 0.508)과 비교 검증하고, 1000nit 패널용 BT.2390 스타일 roll-off 톤 매핑 LUT(10비트)를 생성한다. | mac |
| DISP-A05 | 가변 주사율(VRR)과 프레임 페이싱 | 패널 VRR 범위(예: 48–120Hz)를 가진 디스플레이 모델에서 가변 렌더 시간 워크로드를 고정 60Hz vsync, VRR, VRR+Low Framerate Compensation으로 시뮬레이션해 표시 지연과 프레임 간격 지터를 비교한다. | mac |
| DISP-A06 | Panel Self Refresh와 저전력 디스플레이 설계 | 정지 화면 감지 → PSR 진입 → 부분 갱신(PSR2 selective update) → 탈출의 상태 머신을 구현하고, 사용 시나리오(타이핑, 영상, 정지)별 링크 활성 시간과 추정 전력을 계산해 진입 지연 파라미터의 트레이드오프를 정리한다. | mac |
| DISP-A07 | 타일 기반 프레임버퍼 압축 모델 | 16x16 타일 단위로 단색 타일 플래그, 기준값 + 델타 비트폭 인코딩을 하는 무손실 압축기/해제기를 구현해 왕복 일치를 검증하고, UI·게임·사진 이미지에서 압축률과 스캔아웃 대역폭 절감량, 타일 헤더 메타데이터 오버헤드를 측정한다. | mac |

### MEDIA — 비디오 · 오디오 미디어 (Video & Audio Media)

카메라·드론·스마트 스피커·차량 영상 시스템의 펌웨어는 결국 비디오/오디오를 압축·포장·전송·재생하는 미디어 파이프라인으로 이어진다. 미디어 면접(Apple, NVIDIA, Qualcomm, Meta Reality Labs, Tesla 등)에서는 H.264 NAL/SPS 파싱, 움직임 추정, 레이트 컨트롤, 컨테이너·RTP, 지터 버퍼, I2S/PDM과 AEC 같은 주제를 '직접 비트 단위로 구현해봤는지'를 묻는다. ffmpeg를 참조 오라클 삼아 모든 파서·코덱 결과를 맥에서 검증한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| MEDIA-B01 | 디지털 비디오 기초: Raw 비트레이트와 압축비 계산 | 1080p30/4K60, 8/10비트, 4:2:0/4:2:2/4:4:4 조합의 raw 비트레이트와 목표 비트레이트(예: 8 Mbps) 대비 압축비·bits-per-pixel을 계산하는 도구를 작성하고, ffmpeg로 만든 rawvideo/Y4M 파일 크기와 계산값이 일치함을 검증한다 (YUV 변환·서브샘플링 자체는 see:DISP-B05/B06, 버퍼 레이아웃은 see:CAM-B10). | mac |
| MEDIA-B02 | 화질 지표: Y4M 리더와 PSNR·SSIM 구현 | Y4M 헤더(W/H/F/C 태그)와 FRAME 단위를 읽는 C 리더와 plane별 PSNR, 8x8 윈도 SSIM을 구현하고, ffmpeg -lavfi psnr/ssim 출력과 소수점 둘째 자리까지 일치하는지 비교한다. | mac |
| MEDIA-B03 | 8x8 DCT-II/IDCT와 에너지 집중(Energy Compaction) | 분리형 2D DCT-II/IDCT(직접 O(N^4)과 행·열 분리 O(N^3))를 구현해 왕복 오차가 1e-9 이하임을 확인하고, 자연 영상 블록에서 상위 k개 계수만 남겼을 때의 PSNR 곡선을 그려 에너지 집중을 정량화한다. | mac |
| MEDIA-B04 | H.264 4x4 정수 변환과 양자화: QP와 Qstep | H.264 4x4 core transform(1,2 계수)과 post-scaling을 합친 양자화(MF 테이블, qbits=15+QP/6)·역양자화를 정수로 구현해 QP 0–51에서 재구성 PSNR과 0이 아닌 계수 수 곡선을 측정하고, 변환 행렬이 정확한 DCT가 아니어도 인코더-디코더 drift가 없는 이유를 설명한다. | mac |
| MEDIA-B05 | JPEG Baseline 인코더: 지그재그·RLE·표준 허프만 | 그레이스케일(가능하면 4:2:0 컬러) Baseline JPEG 인코더를 표준 양자화/허프만 테이블로 작성해 SOI/DQT/SOF0/DHT/SOS/EOI 마커와 0xFF 바이트 스터핑을 직접 쓰고, ffmpeg/미리보기로 디코드되는지와 품질 인자별 파일 크기·PSNR을 검증한다 (허프만 기초는 see:ALGO-A06). | mac |
| MEDIA-B06 | 비트 리더/라이터와 Exp-Golomb ue(v)/se(v) | MSB-first 비트 리더/라이터(u(n), read_bits 경계 처리, more_rbsp_data, rbsp_trailing_bits)와 ue(v)/se(v) 인코더·디코더를 C로 작성하고, 0–2^31 범위 랜덤 왕복 테스트와 잘린 입력에서의 경계 검사를 ASan으로 검증한다. | mac |
| MEDIA-B07 | Annex B 바이트스트림과 NAL 헤더: Start Code·Emulation Prevention | ffmpeg(libx264/libx265)로 만든 .h264/.hevc 파일에서 3/4바이트 start code로 NAL을 분리하고 0x000003 emulation prevention 바이트를 제거하는 파서를 작성해 nal_unit_type·nal_ref_idc(HEVC는 2바이트 헤더·layer/tid)를 출력, ffmpeg -bsf:v trace_headers 결과와 NAL 순서·타입이 일치함을 확인한다. | mac |
| MEDIA-B08 | SPS/PPS 파서: Profile·Level·해상도·Cropping·VUI | H.264 SPS(profile_idc, level_idc, chroma_format_idc, log2_max_frame_num, pic_order_cnt_type, pic_width_in_mbs, frame_cropping, VUI timing_info)와 PPS(entropy_coding_mode_flag 등)를 파싱해 1080p의 1088→1080 cropping과 fps를 계산하고, ffprobe -show_streams 값과 여러 프로파일/해상도 샘플로 비교 검증한다. | mac |
| MEDIA-B09 | PCM 오디오 기초: WAV(RIFF) 파서/라이터와 샘플 포맷 | RIFF/WAVE 청크(fmt, data, WAVE_FORMAT_EXTENSIBLE, 홀수 크기 패딩)를 읽고 쓰는 C 라이브러리로 S16/S24-in-S32/F32 변환(디더 없는 truncation vs TPDF 디더)과 인터리브/디인터리브를 구현하고, ffprobe 메타데이터와 1kHz −6 dBFS 톤의 RMS/피크 측정값으로 검증한다. | mac |
| MEDIA-B10 | 디지털 오디오 인터페이스: I2S·Left-Justified·TDM·PDM 프레이밍 | I2S(1비트 지연)·Left-Justified·DSP/TDM8(슬롯 32비트)·스테레오 PDM(한 데이터선, 상승/하강 에지 L/R) 직렬화기/역직렬화기를 비트 단위로 시뮬레이션해 VCD로 출력하고, 48 kHz에서 BCLK·MCLK(256fs) 비율 계산과 1비트 정렬 오류·슬롯 매핑 오류가 만드는 증상(채널 스왑, 6 dB 손실, 노이즈)을 재현한다. | mac |
| MEDIA-M01 | 움직임 추정: Full Search SAD vs 고속 탐색 | 16x16 블록에 대해 ±16 Full Search SAD와 Three-Step/Diamond 탐색, 그리고 6-tap 필터 half-pel 정제를 구현해 ffmpeg testsrc/패닝 시퀀스에서 잔차 에너지·SAD 연산 횟수·MV 필드를 비교하고, 탐색 범위가 메모리 대역폭에 미치는 영향을 계산한다. | mac |
| MEDIA-M02 | Intra 예측과 SATD 모드 결정 | 4x4(V/H/DC/대각 일부)와 16x16(V/H/DC/Plane) intra 예측기를 구현하고 SAD vs Hadamard SATD 비용으로 모드를 고를 때 변환 후 잔차 비트 추정이 어떻게 달라지는지 측정하며, 이웃 블록 불가용(프레임 경계/슬라이스 경계) 처리를 테스트한다. | mac |
| MEDIA-M03 | 미니 하이브리드 코덱: I/P 프레임 Closed-loop 인코더+디코더 | MEDIA-M01·M02·B04·B06을 묶어 I/P 프레임 인코더와 디코더(자체 비트스트림, Exp-Golomb)를 구현하고, 원본 프레임을 참조하는 open-loop 버전과 비교해 GOP 길이에 따른 drift PSNR 저하를 그래프로 보이며, 디코더 출력이 인코더 재구성과 bit-exact임을 MD5로 검증한다. | mac |
| MEDIA-M04 | 엔트로피 코딩: CAVLC 개념과 적응형 이진 산술 부호화(CABAC 원리) | 잔차 계수에 대해 Exp-Golomb, CAVLC 스타일(coeff_token/TotalZeros 개념 단순화), 문맥 적응 이진 산술 부호화기(이진화 + 확률 상태 갱신 + range 정규화, CABAC M-coder 개념 모델)를 구현해 QP별 비트수를 비교하고, 산술 부호 디코더 왕복 bit-exact와 캐리 전파 코너를 테스트한다. | mac |
| MEDIA-M05 | Slice Header·POC와 B-프레임 재정렬: 디코드 vs 표시 순서 | H.264 slice header(first_mb, slice_type, frame_num, pic_order_cnt_lsb, IDR)를 파싱해 POC type 0을 계산하고 DPB 출력(bumping) 모델로 표시 순서를 재구성하며, -bf 3으로 만든 x264 스트림의 ffprobe -show_frames PTS/DTS와 비교하고 필요한 최소 DTS 오프셋·DPB 크기를 도출한다. | mac |
| MEDIA-M06 | 레이트 컨트롤: VBV/HRD 버퍼 모델과 QP 적응 | 프레임 복잡도 트레이스(ffmpeg -pass 1 통계 또는 합성)에 대해 R-Q 모델 기반 프레임 레벨 QP 결정과 VBV leaky bucket(초기 지연, 버퍼 크기, maxrate) 시뮬레이터를 구현해 underflow/overflow가 없음을 검증하고, CBR/VBR/CRF(x264) 결과의 비트레이트 변동과 PSNR을 비교한다. | mac |
| MEDIA-M07 | MP4(ISOBMFF) 박스 파서와 샘플 테이블 | ftyp/moov/trak/mdia/minf/stbl(stsd·avcC, stts, ctts, stsc, stsz, stco/co64, stss) 재귀 파서를 작성해 샘플 i의 파일 오프셋·크기·DTS/PTS·키프레임 여부를 계산하고, avcC의 length-prefixed NAL을 Annex B로 변환해 추출한 스트림이 ffmpeg -c copy -bsf h264_mp4toannexb 결과와 바이트 일치함을 검증한다 (fMP4 moof/traf는 확장). | mac |
| MEDIA-M08 | MPEG-TS 디멀티플렉서: PAT/PMT·PES·PCR | 188바이트 TS 패킷(sync 0x47, PID, PUSI, adaptation field, continuity counter)을 파싱해 PAT→PMT→ES PID를 찾고 PES를 재조립해 PTS/DTS를 추출하며, PCR(27 MHz) 간격과 PTS 관계를 출력하고 CC 불연속을 주입해 검출되는지 ffprobe 결과와 함께 검증한다. | mac |
| MEDIA-M09 | RTP 패킷화: H.264 RFC 6184 (Single NAL·FU-A·STAP-A) | Annex B 스트림을 RTP(90 kHz 타임스탬프, marker 비트, 시퀀스 16비트 wrap)로 패킷화하면서 MTU 초과 NAL은 FU-A로 분할, SPS/PPS는 STAP-A로 묶는 packetizer와 depacketizer를 작성하고, 재조립 스트림이 원본과 바이트 일치·유실 FU 조각 시 NAL 폐기 동작을 테스트한다 (일반 UDP/소켓은 see:NET). | mac |
| MEDIA-M10 | 적응형 지터 버퍼: RFC 3550 지터 추정과 Playout Delay | RFC 3550 interarrival jitter 추정기와 재정렬·중복 제거·늦은 패킷 폐기를 하는 지터 버퍼를 구현하고, 지연/지터/버스트 손실 트레이스로 고정 vs 적응형(지터 배수, 백분위 기반) playout delay의 평균 지연·late-loss율을 비교한다 (재전송 프로토콜은 see:DIST-A06). | mac |
| MEDIA-M11 | PDM 마이크 데시메이션 체인 설계: CIC + Half-band + 보상 FIR | 2차 시그마-델타 변조기로 PDM 비트스트림을 생성하고 CIC(R=16) → half-band ×2 → 보상 FIR ×2 체인을 통과대역 리플·저지대역 감쇠 사양으로 설계해 SNR·연산량(MACs/s)·메모리를 측정하며, 단일 단계 FIR 대비 연산량 절감을 표로 정리한다 (CIC 기초와 비트 성장은 see:DSP-M08, polyphase는 see:DSP-M07). | mac |
| MEDIA-M12 | ALSA PCM 재생/캡처: hw_params·Period·XRUN 복구 | snd-aloop 루프백 장치에서 alsa-lib로 hw_params(rate, format, period_size, buffer_size)를 협상하는 재생+캡처 프로그램을 작성해 왕복 지연을 측정하고, 처리 지연을 주입해 XRUN(-EPIPE)을 유발한 뒤 snd_pcm_recover로 복구하며 period 크기별 지연·XRUN 빈도를 기록한다. | **linux** |
| MEDIA-A01 | V4L2 Stateful M2M 코덱 API: vicodec 인코더/디코더 | vicodec 가상 드라이버에 대해 V4L2 stateful encoder/decoder 인터페이스(OUTPUT=입력, CAPTURE=출력 큐, S_FMT 순서, STREAMON, V4L2_ENC_CMD_STOP drain, V4L2_EVENT_SOURCE_CHANGE 해상도 변경)를 구현한 C 프로그램을 작성해 YUV→FWHT 인코드→디코드 왕복 PSNR과 drain 시 마지막 버퍼(V4L2_BUF_FLAG_LAST) 처리를 검증한다 (캡처 기초는 see:CAM-A01). | **linux** |
| MEDIA-A02 | 미니 RTSP 서버: SDP·RTP/RTCP와 Sender Report | RTSP OPTIONS/DESCRIBE/SETUP/PLAY/TEARDOWN을 처리하고 SDP(sprop-parameter-sets, profile-level-id)를 생성하는 서버에서 MEDIA-M09 packetizer로 H.264 파일을 실시간 페이싱해 RTP/UDP로 보내고 RTCP SR(NTP↔RTP 타임스탬프 매핑)을 주기 전송하며, ffmpeg -rtsp_transport udp -i rtsp://... -c copy out.mp4로 수신된 스트림이 재생 가능한지 검증한다. | mac |
| MEDIA-A03 | A/V 동기화와 클럭 드리프트: 오디오 마스터 클럭과 비동기 SRC | 오디오 클럭(I2S 수정자 +80 ppm)과 비디오/시스템 클럭이 다른 시뮬레이터에서 오디오 마스터 기준 비디오 프레임 drop/repeat 정책과, 버퍼 수위를 PI 제어로 추적하는 비동기 샘플률 변환(가변 비율 Farrow)을 구현해 1시간 재생 동안 lip-sync 오차를 ±40 ms 이내로 유지함을 보인다 (고정 비율 SRC는 see:DSP-A04, RTCP 매핑은 MEDIA-A02). | mac |
| MEDIA-A04 | 실시간 비디오의 손실 복구와 혼잡 제어: NACK·FEC·GCC | 트레이스 기반 링크 시뮬레이터 위에서 NACK 재전송, XOR FEC(ULPFEC 개념), PLI 키프레임 요청, 손실 slice의 오류 은닉(이전 프레임 복사/MV 보간)과 GCC 지연 기반 대역폭 추정(trendline 필터 + 과사용 검출 + AIMD)을 구현해 대역폭 단계 변화·2% 손실에서 목표 비트레이트 추종과 PSNR/지연을 측정한다. | mac |
| MEDIA-A05 | 음향 에코 제거(AEC): 주파수 영역 블록 NLMS와 Double-talk | 참조-마이크 간 벌크 지연을 GCC-PHAT 상관으로 추정하고, partitioned block frequency-domain NLMS(overlap-save)와 Geigel/상관 기반 double-talk 검출, 잔여 에코 억제(NLP)를 구현해 시변 에코 경로·double-talk 구간에서 ERLE와 근단 음성 왜곡을 측정한다 (시간 영역 LMS 기초는 see:DSP-A05). | mac |
| MEDIA-A06 | 마이크 어레이: Delay-and-Sum 빔포밍과 GCC-PHAT 방향 추정 | 4채널 선형/원형 어레이의 합성 신호(분수 지연, 잔향 단순 모델, 잡음)를 생성해 분수 지연 FIR 기반 delay-and-sum 빔포머와 GCC-PHAT TDOA→DOA 추정기를 구현하고, 빔 패턴(주파수·각도별 이득)과 SNR 개선량, 마이크 간격에 따른 공간 aliasing을 측정한다. | mac |
| MEDIA-A07 | RDO와 HEVC CTU 쿼드트리 분할 결정 | 64x64 CTU를 32/16/8로 재귀 분할하며 각 노드에서 예측+변환+양자화 후 왜곡(SSE)과 추정 비트(MEDIA-M04 엔트로피 모델)로 J를 계산해 분할을 고르는 RDO 탐색과 조기 종료 휴리스틱을 구현하고, λ–QP 관계에 따른 BD-rate 경향과 연산량 대비 이득을 측정한다 (HEVC/AV1/VVC 블록 구조 비교 표 포함). | mac |
| MEDIA-A08 | 하드웨어 비디오 인코더 설계·검증: 처리량·대역폭·Bit-exact | 4K60 4:2:0 인코딩을 목표로 MB/CTU 처리율, 클럭당 사이클 예산, 참조 프레임 DDR 대역폭(탐색 윈도 캐싱 유무), 라인 버퍼 SRAM 크기를 계산하는 모델을 작성하고, C golden model 대비 bit-exact 회귀 하네스(ffmpeg -f framemd5, 적합성 스트림, 랜덤 파라미터 시드)와 장애 분류 흐름을 설계한다 (RTL 검증 방법론은 see:RTLDV, 카메라→인코더 시스템은 see:SYSD-M05). | mac |

### GFX — 그래픽스 · 3D 자료구조 (Graphics & 3D Data Structures)

그래픽스 기초 수학(벡터·행렬·쿼터니언·투영)과 래스터화, 그리고 BVH·k-d tree·octree·half-edge 같은 3D 자료구조는 렌더링뿐 아니라 로봇 인지, LiDAR 포인트 클라우드, 충돌 검사, 시뮬레이션의 공통 기반이다. GPU/게임/로보틱스 면접(NVIDIA, Apple, Tesla, Anduril)에서는 삼각형 래스터화, ray-triangle 교차, BVH 구축, k-NN 질의를 직접 짜게 하는 문제가 자주 나온다. 본격 렌더링 기법은 REND 트랙에 맡기고, 여기서는 기초 수학과 자료구조를 손으로 구현하고 검증한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| GFX-B01 | 벡터 연산: Dot·Cross·Normalize와 기하 의미 | vec2/vec3 라이브러리(dot, cross, length, normalize, project, reflect)를 C로 작성하고, 점이 직선 왼쪽/오른쪽인지 판정, 삼각형 면적·법선 계산, 0벡터 normalize 처리 등을 epsilon 비교 단위 테스트로 검증한다. | mac |
| GFX-B02 | 4x4 행렬 변환: Row/Column-Major와 합성 순서 | mat4 곱, translate/rotate(x,y,z)/scale 생성 함수를 column-major 저장으로 구현하고, T*R*S와 S*R*T가 점에 다른 결과를 주는 테스트와 row-major로 해석했을 때 전치 관계가 됨을 보이는 테스트를 작성한다. | mac |
| GFX-B03 | 역행렬과 법선 변환(Inverse-Transpose) | cofactor 방식 일반 4x4 역행렬과 R|t 구조를 이용한 affine 빠른 역행렬을 구현해 M*M^-1 ≈ I를 검증하고, 비균등 스케일에서 법선을 M으로 변환하면 수직성이 깨지고 (M^-1)^T로는 유지됨을 테스트로 보인다. | mac |
| GFX-B04 | 쿼터니언 회전과 Slerp | axis-angle → quat, quat 곱, 벡터 회전(q v q*), quat ↔ mat3 변환, slerp(가까운 경로 선택, 작은 각도 fallback)를 구현하고, 무작위 회전 1만 개에 대해 행렬 결과와의 일치와 정규화 드리프트를 측정한다. | mac |
| GFX-B05 | 카메라와 투영: LookAt·Perspective·NDC·Viewport | lookAt 뷰 행렬, perspective(fovy, aspect, near, far)와 orthographic 투영, perspective divide, viewport 변환을 구현하고, 알려진 3D 점들이 기대 픽셀 좌표와 깊이 값으로 매핑되는지 테스트하며 OpenGL(-1..1)과 Metal/D3D(0..1) 깊이 범위 차이를 반영한다. | mac |
| GFX-B06 | 클리핑: Cohen-Sutherland와 Sutherland-Hodgman | 2D 사각형에 대한 Cohen-Sutherland 선분 클리핑과, 동차 좌표에서 near 평면 포함 6개 평면에 대한 Sutherland-Hodgman 다각형 클리핑을 구현하고, 카메라 뒤로 걸친 삼각형이 올바르게 잘리는지 테스트한다. | mac |
| GFX-B07 | Bresenham 직선·원 래스터화 (정수 전용) | 8개 옥탄트를 모두 처리하는 Bresenham 직선과 midpoint 원 알고리즘을 정수 연산만으로 구현해 PPM으로 출력하고, float DDA 결과와 픽셀 단위로 비교하며 시작/끝점 대칭성(A→B와 B→A 동일 픽셀)을 검증한다. | mac |
| GFX-B08 | 삼각형 래스터화: Edge Function과 Top-Left Rule | 바운딩 박스 + edge function 방식 삼각형 래스터라이저를 작성하고 top-left fill rule을 적용해, 사각형을 두 삼각형으로 나눠 그렸을 때 모든 픽셀이 정확히 한 번만 칠해지는지(overdraw 카운트 버퍼) 검증한다. | mac |
| GFX-B09 | 무게중심 보간과 Perspective-Correct 보간 | 래스터라이저에 barycentric 기반 색/UV 보간을 추가하고, 체커보드 텍스처를 입힌 기울어진 평면을 affine 보간과 1/w 기반 perspective-correct 보간으로 각각 렌더링해 왜곡 차이를 이미지로 비교한다. | mac |
| GFX-M01 | Z-Buffer와 깊이 정밀도: Reversed-Z | z-buffer를 래스터라이저에 넣어 교차하는 삼각형을 올바르게 그리고, near/far 비율을 바꿔 가며 24비트 정수 깊이, float32 표준 깊이, float32 reversed-Z에서 구분 가능한 최소 거리 차이를 측정·그래프로 비교한다. | mac |
| GFX-M02 | 컬링: Backface·Frustum (AABB/Sphere vs Plane) | 뷰-투영 행렬에서 6개 frustum 평면을 추출하는 함수, sphere/AABB vs frustum 판정(p-vertex/n-vertex 최적화), 화면 공간 winding 기반 backface 컬링을 구현하고, 무작위 객체 1만 개 장면에서 컬링 비율과 오판정(false negative 0) 여부를 검증한다. | mac |
| GFX-M03 | 메시 포맷: OBJ/PLY 로더와 Indexed Mesh | OBJ(v/vt/vn, 다각형 fan 삼각분할)와 binary PLY 로더를 작성하고, 해시로 정점을 중복 제거해 indexed triangle mesh를 만든 뒤 면적 가중 정점 법선을 계산하며, 정점/인덱스 수와 메모리 크기를 원본과 비교한다. | mac |
| GFX-M04 | Half-Edge 메시 자료구조 | indexed mesh에서 twin 매칭으로 half-edge 구조를 구축하고 정점 one-ring 순회, 면 인접 면, 경계 에지 검출, 비다양체 에지 거부를 구현해, 닫힌 메시에서 오일러 특성 V−E+F=2를 검증한다. | mac |
| GFX-M05 | Ray-Triangle(Möller-Trumbore)과 Ray-AABB(Slab) 교차 | Möller-Trumbore ray-triangle 교차(t, u, v 반환)와 slab 방식 ray-AABB 교차(1/dir 사전계산, NaN/무한대 처리)를 구현하고, 무작위 레이와 해석적 정답 비교 테스트 및 평행 레이 같은 경계 케이스를 검증한다. | mac |
| GFX-M06 | BVH 구축과 순회: Median Split vs SAH | 삼각형 메시에 대해 median split과 binned SAH 두 방식으로 BVH를 구축해 평탄화된 배열 노드(32바이트 정렬)로 저장하고, 스택 기반 순회(가까운 자식 우선)로 레이 100만 개를 쏴서 브루트포스 대비 정답 일치와 노드 방문 수·속도를 비교한다. | mac |
| GFX-M07 | Octree: 포인트 클라우드 삽입·반경 질의·LOD | 버킷 크기와 최대 깊이를 갖는 포인트 octree를 구현해 삽입, 반경 질의, 레벨별 대표점(voxel 중심) LOD 생성을 지원하고, 100만 점 데이터에서 브루트포스 대비 질의 결과 일치와 속도, 메모리 사용량을 측정한다. | mac |
| GFX-M08 | k-d Tree: 구축과 k-최근접 이웃(k-NN) 질의 | median 분할(nth_element)로 3D k-d tree를 구축하고 최대 힙을 쓰는 k-NN 질의(분할 평면 거리로 가지치기)를 구현해, 무작위 점·질의에 대해 브루트포스와 결과가 완전히 일치하는지와 k, 점 개수에 따른 속도를 측정한다. | mac |
| GFX-M09 | Spatial Hashing과 Uniform Grid: Broad-Phase 충돌 | 셀 크기 기반 정수 좌표 해시(큰 소수 XOR) 공간 해시를 구현해 움직이는 구 1만 개의 충돌 후보 쌍을 매 프레임 계산하고, O(n²) 결과와 쌍 집합 일치 및 셀 크기에 따른 성능 변화를 측정한다. | mac |
| GFX-M10 | 정점 캐시 최적화와 인덱스 버퍼 재정렬 | FIFO/LRU 정점 캐시 시뮬레이터로 ACMR(삼각형당 캐시 미스)을 측정하는 도구와 Forsyth 선형 속도 정점 캐시 최적화 알고리즘을 구현하고, 무작위 순서·원본·최적화 후 ACMR과 정점 버퍼 재정렬 후 fetch 지역성을 비교한다. | mac |
| GFX-A01 | 고정소수점 래스터라이저와 Sub-pixel 정밀도 | 정점 좌표를 28.4 고정소수점으로 스냅하고 edge function을 증분 덧셈으로 평가하는 래스터라이저를 구현해, 오버플로 없는 최대 화면 크기를 계산·검증하고 float 래스터라이저와 픽셀 커버리지 차이 및 watertight(빈틈 없음) 특성을 테스트한다. | mac |
| GFX-A02 | 타일 기반 렌더링(TBR) 비닝 모델 | 화면을 32x32 타일로 나눠 삼각형을 타일 리스트에 비닝하는 단계와 타일별 on-chip 색/깊이 버퍼로 래스터화하는 단계를 구현하고, 즉시 모드 렌더링 대비 프레임버퍼 DRAM 읽기/쓰기 바이트와 비닝 메모리 오버헤드를 장면별로 비교한다. | mac |
| GFX-A03 | Hierarchical Z 버퍼와 Occlusion Culling | z-buffer에서 max-reduction mip 피라미드(HZB)를 생성하고, 객체 AABB의 화면 사각형 크기에 맞는 mip 레벨 하나로 가려짐을 판정하는 컬링을 구현해, 도시 장면에서 컬링 비율과 보수성(가시 객체를 절대 버리지 않음)을 검증한다. | mac |
| GFX-A04 | LBVH: Morton 코드 기반 병렬 BVH 구축과 Refit | 프리미티브 중심을 30비트 Morton 코드로 변환·radix 정렬하고 Karras 방식으로 내부 노드를 구성하는 LBVH를 구현해 SAH BVH 대비 구축 시간과 순회 비용을 비교하며, 정점이 움직일 때 상향식 AABB refit과 품질 저하를 측정한다. | mac |
| GFX-A05 | 메시 단순화: Quadric Error Metrics | 정점별 quadric 누적, 최적 붕괴 위치 계산, 우선순위 큐 기반 edge collapse(면 뒤집힘·위상 검사 포함)를 half-edge 메시에 구현하고, 삼각형 수 50%/10%/1%에서 Hausdorff 거리 근사 오차를 측정한다. | mac |
| GFX-A06 | 기하 견고성: Robust Predicate와 Watertight 교차 | naive orient2d가 거의 공선인 점에서 일관성 없는 결과를 내는 입력을 찾아 재현하고, 오차 한계 필터 + 확장 정밀도(또는 정수 좌표) 기반 robust orient2d와 watertight ray-triangle 교차를 구현해 공유 에지를 지나는 레이가 새지 않음을 검증한다. | mac |
| GFX-A07 | 메시 압축: 정점 양자화와 Octahedral 법선 인코딩 | AABB 기준 16비트 위치 양자화, octahedral 매핑 기반 2x8비트/2x16비트 법선 인코딩, half-float UV를 구현하고 원본 대비 위치 오차(최대·RMS), 법선 각도 오차, 메모리 절감률을 측정한다. | mac |

### GPGPU — GPU 연산 (GPU Compute)

GPU 연산은 AI 가속기 시대의 공용어다. SIMT 실행 모델, 워프/occupancy, 메모리 계층(global/shared/register), coalescing과 bank conflict를 이해해야 GEMM·reduction·softmax 같은 핵심 커널의 성능을 설명할 수 있고, NVIDIA·Apple·AI 칩 회사 면접에서 이 커널들을 직접 최적화하는 문제가 단골로 나온다. 이 트랙은 M2 맥의 Metal compute와 CPU 기반 SIMT 시뮬레이터로 대부분을 실습하고, CUDA 전용 내용은 gpu 환경 항목으로 분리한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| GPGPU-B01 | SIMT 실행 모델 CPU 시뮬레이터: Grid·Block·Warp | gridDim/blockDim/threadIdx를 제공하고 워프 단위(32 lane)로 커널 함수를 lockstep 실행하는 C 기반 SIMT 시뮬레이터를 작성해, vector add와 2D 인덱싱 커널을 돌려 CPU 레퍼런스와 결과를 비교하고 워프·블록 수를 출력한다. | mac |
| GPGPU-B02 | Metal 첫 커널: Vector Add와 호스트 셋업 | metal-cpp(또는 Objective-C/Swift)로 MTLDevice, MTLBuffer(shared storage), compute pipeline, dispatchThreads를 구성해 vector add 커널을 실행하고, CPU 결과와 비교하며 원소 수별 실행 시간(커맨드 버퍼 시작~완료)을 측정해 런치 오버헤드 구간을 확인한다. | mac |
| GPGPU-B03 | 2D 그리드 인덱싱과 경계 처리: 이미지 커널 | RGBA 이미지 그레이스케일 변환과 naive 행렬 전치 커널을 Metal로 구현하고, 너비·높이가 threadgroup 크기의 배수가 아닌 경우 경계 체크 유무에 따른 결과와 threadgroup 크기(8x8, 16x16, 32x8) 별 성능을 비교한다. | mac |
| GPGPU-B04 | 워프 다이버전스와 재수렴 스택 | B01 시뮬레이터에 active mask와 재수렴 스택(IPDOM)을 추가해 if/else·루프 분기를 처리하게 하고, 분기 패턴(lane 짝홀, 데이터 의존)별 SIMD 효율(활성 lane 비율)을 측정한 뒤 같은 패턴을 Metal에서 실행 시간으로 확인한다. | mac |
| GPGPU-B05 | 지연 은닉과 Little's Law: 필요한 병렬도 계산 | 메모리 지연(사이클), 목표 대역폭, 워프당 요청 바이트로 필요한 in-flight 워프 수를 계산하는 모델을 만들고, 시뮬레이터에서 동시 워프 수를 늘려 가며 달성 대역폭이 포화되는 지점이 모델 예측과 맞는지 확인한다. | mac |
| GPGPU-B06 | 메모리 대역폭 마이크로벤치마크 (M2 GPU) | Metal로 copy/scale/triad 커널을 작성해 버퍼 크기(캐시 내/외)별 달성 GB/s를 측정하고, M2 이론 대역폭(약 100GB/s) 대비 효율과 스레드당 처리 원소 수(1, 4, float4 벡터화)에 따른 차이를 그래프로 정리한다. | mac |
| GPGPU-B07 | 메모리 Coalescing: AoS vs SoA와 Stride 접근 | 시뮬레이터에 워프 단위 주소를 128바이트 세그먼트로 묶어 트랜잭션 수를 세는 기능을 추가해 stride 1~32, AoS vs SoA 접근의 트랜잭션 수를 계산하고, 같은 패턴을 Metal에서 실측해 대역폭 저하가 예측과 맞는지 비교한다. | mac |
| GPGPU-B08 | Occupancy 계산기 구현 | SM 사양(최대 워프, 레지스터 파일, shared memory, 최대 블록 수)과 커널 자원 사용량을 입력받아 제한 요인별 occupancy를 계산하는 Python 도구를 작성해 CUDA Occupancy Calculator 공개 예시와 결과를 맞추고, Metal의 maxTotalThreadsPerThreadgroup·threadExecutionWidth 조회 결과와 연결해 해석한다. | mac |
| GPGPU-B09 | Atomic과 Barrier: 레이스와 동기화 | 전역 카운터를 일반 증가/atomic 증가로 구현해 결과 차이를 재현하고, threadgroup memory에 쓰고 읽는 커널에서 barrier를 빼면 생기는 오류를 재현한 뒤, atomic 경합 정도(주소 수 1~1024)에 따른 처리량을 측정한다. | mac |
| GPGPU-M01 | 병렬 Reduction: 단계별 최적화 | Metal로 sum reduction을 interleaved addressing → sequential addressing → 로드 시 첫 덧셈 → simd_sum(워프 셔플) → 다중 패스 순으로 최적화하며 각 버전의 GB/s를 측정하고, 피크 대역폭 대비 효율과 float 합산 오차를 기록한다. | mac |
| GPGPU-M02 | Prefix Sum(Scan): Hillis-Steele vs Blelloch | threadgroup 내 Hillis-Steele과 Blelloch(up-sweep/down-sweep) exclusive scan을 구현하고, 블록 합 스캔 후 더하는 3단계 방식으로 1억 원소 배열 스캔을 완성해 CPU 결과와 일치·처리량을 측정하며 decoupled look-back 단일 패스 방식의 원리를 설명한다. | mac |
| GPGPU-M03 | Histogram: Privatization과 Atomic 경합 | 256-bin 히스토그램을 전역 atomic 버전과 threadgroup 지역 히스토그램 + 최종 병합 버전으로 구현하고, 균일 분포와 한 bin에 몰린 편향 분포 입력에서 처리량을 비교한다. | mac |
| GPGPU-M04 | Shared Memory Bank Conflict와 Padding 전치 | 시뮬레이터에 32-bank shared memory 모델을 넣어 접근 패턴별 conflict degree를 계산하고, Metal로 타일 전치 커널을 naive / threadgroup 타일 / 타일+1 padding 세 버전으로 구현해 conflict 예측과 실측 대역폭을 비교한다. | mac |
| GPGPU-M05 | Tiled GEMM: Shared Memory 타일링과 Register Blocking | Metal로 SGEMM을 naive → threadgroup 타일링 → 스레드당 4x4 register blocking → float4 벡터 로드 순으로 구현해 단계별 GFLOPS와 산술 강도를 기록하고, Accelerate(cblas_sgemm)와 MPS 행렬곱 성능 대비 비율을 보고한다. | mac |
| GPGPU-M06 | 2D Convolution: Halo 타일과 Separable 필터 | KxK 2D 컨볼루션을 naive 전역 메모리 버전과 halo 포함 threadgroup 타일 버전으로 구현하고, 가우시안처럼 분리 가능한 필터는 행/열 두 패스로 나눠 K=3,7,15에서 성능과 결과 일치(허용 오차)를 비교한다. | mac |
| GPGPU-M07 | Softmax: 수치 안정성과 Online Softmax | 행 단위 softmax를 3-pass(max, sum, normalize)와 online 2-pass(실행 중 max 갱신 시 합 재스케일) 방식으로 Metal에 구현하고, 큰 로짓(1e4) 입력에서 naive 버전의 overflow를 재현하며 행 길이별 메모리 트래픽과 성능을 비교한다. | mac |
| GPGPU-M08 | Stream Compaction과 Radix Sort | M02의 scan으로 predicate 기반 stream compaction을 구현하고, 4비트 digit 단위 LSD radix sort(블록 히스토그램 → 전역 scan → scatter)를 완성해 1천만 개 uint32 정렬 결과를 std::sort와 비교하고 안정성(stable)을 검증한다. | mac |
| GPGPU-M09 | 비동기 실행과 파이프라이닝: Command Buffer와 Stream | Metal에서 단일 command buffer 직렬 실행과 3개 in-flight command buffer + 세마포어로 CPU 준비·GPU 실행을 겹치는 버전을 구현해 처리량을 비교하고, M2 unified memory에서 shared vs private storage 차이와 CUDA stream·cudaMemcpyAsync 겹치기의 대응 관계를 정리한다. | mac |
| GPGPU-M10 | 커널 프로파일링과 병목 분류 | 지금까지 만든 커널들에 대해 FLOP 수와 바이트 수를 계산하고 실측 시간으로 달성 GFLOPS·GB/s를 구해 M2 GPU roofline 위에 표시하며, Xcode Metal GPU capture의 카운터로 한 커널의 병목을 확인해 보고서를 작성한다. | mac |
| GPGPU-A01 | CUDA 포팅과 Nsight Compute 분석 | reduction·tiled GEMM·softmax를 CUDA로 포팅해 __shfl_down_sync, shared memory, cudaEvent 타이밍을 적용하고, ncu로 achieved occupancy, DRAM throughput, bank conflict, warp stall 원인을 수집해 Metal 결과와 비교한 분석 노트를 작성한다. | **gpu** |
| GPGPU-A02 | 행렬 유닛 GEMM: simdgroup_matrix / Tensor Core | Metal simdgroup_matrix(8x8) 로드/곱누적/저장으로 FP16 입력·FP32 누적 GEMM을 구현해 M05 스칼라 버전 대비 성능과 오차를 비교하고, 같은 구조가 CUDA WMMA/mma.sync의 fragment 개념과 어떻게 대응하는지 정리한다. | mac |
| GPGPU-A03 | Fused Attention 커널 (FlashAttention 스타일) | Q 블록당 K/V 블록을 순회하며 online softmax로 출력을 누적하는 fused attention 커널을 Metal로 구현하고, S=N×N 행렬을 materialize하는 naive 3-커널 버전과 결과(허용 오차)·메모리 사용량·시퀀스 길이별 성능을 비교한다. | mac |
| GPGPU-A04 | 양자화 GEMM: INT8/FP16 Dequant-on-the-fly | 가중치를 per-channel int8(또는 4비트 그룹 양자화)로 저장하고 커널 안에서 FP16으로 dequantize해 곱하는 GEMV/GEMM을 Metal로 구현해, FP16 GEMM 대비 메모리 트래픽·속도(특히 batch=1 GEMV)와 출력 오차를 측정한다. | mac |
| GPGPU-A05 | 불규칙 워크로드: SpMV(CSR)와 부하 분산 | CSR SpMV를 thread-per-row, simdgroup-per-row, nnz 기반 균등 분할(merge-path 유사) 세 가지로 구현하고, 행 길이 분포가 균일한 행렬과 power-law 행렬에서 성능과 SIMD 효율을 비교한다. | mac |
| GPGPU-A06 | 커널 퓨전과 런치 오버헤드 | 빈 커널 반복 디스패치로 런치·커맨드 제출 오버헤드를 측정하고, bias add → GELU → residual add 체인을 3개 커널과 1개 fused 커널로 구현해 텐서 크기별 시간과 메모리 트래픽 감소량을 비교한다. | mac |
| GPGPU-A07 | GPU 수치 결정성과 결과 검증 하네스 | atomic float 합산 reduction이 실행마다 다른 결과를 내는 것을 재현하고, 고정 트리 순서 reduction과 Kahan/pairwise 합산으로 결정적·정확한 버전을 만든 뒤, ULP·상대오차·NaN/Inf 검사를 포함한 GPU vs CPU 비교 하네스를 작성해 모든 커널에 적용한다. | mac |

### GPUSYS — GPU 시스템 (GPU Systems)

GPU를 하나의 시스템으로 보면 드라이버 스택, 커맨드 버퍼와 링, 도어벨, GPU MMU, 컨텍스트 스위치·선점, GPU 내부 펌웨어 마이크로컨트롤러(GSP 등), 전력 관리, PCIe/NVLink 인터커넥트가 맞물린 거대한 펌웨어·드라이버 문제다. NVIDIA·AMD·Apple GPU 팀과 AI 가속기 회사의 시스템 소프트웨어/펌웨어 직무는 바로 이 층을 다루며, Don의 SSD 펌웨어(NVMe 큐·도어벨·DMA) 경험이 가장 직접적으로 전이되는 영역이다. 대부분 항목은 호스트 C로 GPU와 드라이버를 모델링해 검증한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| GPUSYS-B01 | GPU 소프트웨어 스택 지도: API → UMD → KMD → FW | UMD(커맨드 생성·버퍼 패치), KMD(ioctl 검증·메모리 상주화·제출), FW(큐 소비)를 함수 계층으로 나눈 C mock을 작성해 cudaLaunchKernel/vkQueueSubmit에 해당하는 호출 하나의 전 경로를 로그로 추적하고, 각 계층의 책임과 실패 지점을 표로 정리한다. | mac |
| GPUSYS-B02 | BAR와 MMIO: GPU 레지스터 접근 모델 | BAR0(MMIO 레지스터)와 BAR1(VRAM aperture)을 가진 mock GPU 디바이스를 정의하고 volatile 접근 래퍼, 레지스터 읽기 부작용(read-to-clear), 쓰기 순서 보장(메모리 배리어 자리)을 구현해 테스트하며, aperture 크기 제한 시 VRAM 창을 이동하는 로직을 작성한다. | mac |
| GPUSYS-B03 | 커맨드 버퍼 패킷 인코딩과 검증기 | PM4 유사 패킷 포맷(type, opcode, dword count)으로 SET_REG, DISPATCH, COPY, WRITE_FENCE, NOP 패킷 인코더와 디코더를 C로 구현하고, 범위 밖 레지스터 쓰기·잘린 패킷을 거부하는 KMD 검증기를 퍼즈 테스트한다. | mac |
| GPUSYS-B04 | 링 버퍼와 도어벨: 호스트-GPU 제출 큐 | 공유 메모리 링(크기 2^n dword), 드라이버가 갱신하는 wptr, GPU 스레드가 갱신하는 rptr, 도어벨 MMIO 쓰기를 pthread로 모델링하고, 링 끝에서 패킷이 쪼개지지 않게 NOP 패딩하는 규칙과 가득 참 대기를 구현해 스트레스 테스트한다. | mac |
| GPUSYS-B05 | 펜스와 타임라인 세마포어 | GPU 모델이 패킷 처리 후 fence 주소에 seqno를 쓰도록 하고, 드라이버 측 fence_wait(timeout), signaled 판정(32비트 wraparound 안전 비교), 큐 간 의존성을 위한 타임라인 세마포어 wait/signal을 구현해 순서 보장 테스트를 작성한다. | mac |
| GPUSYS-B06 | GPU 메모리 관리: Buffer Object와 서브할당 | VRAM 힙에서 정렬(4KB/64KB/2MB) 요구를 만족하는 BO 할당자와 작은 할당용 서브할당자(slab)를 구현하고, 메모리 부족 시 LRU BO를 시스템 메모리로 evict하는 정책을 넣어 단편화율과 eviction 횟수를 워크로드별로 측정한다. | mac |
| GPUSYS-B07 | GPU MMU: 다단계 페이지 테이블 | 49비트 GPU VA를 4단계 테이블로 변환하는 페이지 테이블 모델(PTE 유효/쓰기/캐시 속성 비트, 64KB·2MB 큰 페이지 지원)을 C로 구현해 map/unmap/translate를 제공하고, 무작위 매핑에 대한 참조 해시맵과 변환 결과 일치 및 테이블 메모리 오버헤드를 측정한다. | mac |
| GPUSYS-B08 | Unified Memory vs Discrete: Zero-copy 실측 | Metal에서 shared/private/managed storage 버퍼로 같은 커널을 실행해 CPU 쓰기→GPU 읽기 지연과 처리량을 측정하고, PCIe Gen4 x16 discrete GPU의 복사 시간 모델(지연 + 크기/대역폭)과 비교해 어느 크기부터 복사 비용이 커널 시간을 넘는지 계산한다. | mac |
| GPUSYS-M01 | GPU 인터럽트와 IH 링 처리 | GPU 모델이 이벤트(fence 완료, page fault, 에러)를 IH 링에 엔트리로 쓰고 MSI를 발생시키면, 드라이버 top half가 rptr만 읽어 워크큐로 넘기고 bottom half가 소스별 핸들러로 디스패치하는 구조를 구현해, 링 overflow 감지와 인터럽트 coalescing 효과를 측정한다. | mac |
| GPUSYS-M02 | GPU 작업 스케줄러: 우선순위와 타임슬라이스 | 여러 프로세스의 작업 큐를 받아 엔진에 배정하는 스케줄러를 이벤트 시뮬레이션으로 구현해 FIFO, 우선순위, 라운드로빈 타임슬라이스, 우선순위+aging을 비교하고, 고우선순위(UI/실시간) 작업의 지연 p99와 처리량을 표로 정리한다. | mac |
| GPUSYS-M03 | 컨텍스트 스위치와 선점 단위 | 컨텍스트 상태 크기(레지스터 파일, shared memory, 큐 상태)와 선점 단위별 대기 시간을 파라미터로 한 선점 모델을 만들고, 긴 컴퓨트 커널 실행 중 실시간 작업 도착 시 각 방식의 선점 지연과 저장/복원 오버헤드를 시뮬레이션해 비교한다. | mac |
| GPUSYS-M04 | GPU 페이지 폴트와 Unified Memory 마이그레이션 | B07 MMU에 미매핑 접근 시 fault 버퍼 기록 → 드라이버가 호스트 페이지를 VRAM으로 이동·매핑 → 재실행하는 흐름을 구현하고, VRAM 초과 워킹셋에서 thrashing을 재현한 뒤 2MB 단위 이동·prefetch·access counter 기반 정책별 이동 바이트와 시간을 비교한다. | mac |
| GPUSYS-M05 | GPU 펌웨어 마이크로컨트롤러 RPC (GSP 스타일) | 공유 메모리 기반 command/status 큐 쌍(헤더: 함수 ID, seq, 길이, 체크섬)과 펌웨어 측 이벤트 루프를 pthread로 구현해 동기 RPC(alloc_channel, set_clock)와 비동기 이벤트 알림을 처리하고, seq 불일치·타임아웃·펌웨어 무응답 시 드라이버의 복구 경로를 테스트한다. | mac |
| GPUSYS-M06 | GPU 펌웨어 로딩과 보안 부팅 | 헤더(매직, 버전, 섹션 테이블, 서명) + IMEM/DMEM 섹션 구조의 펌웨어 이미지 포맷을 정의하고, 해시 검증 → 섹션 DMA 적재 → 부팅 핸드셰이크(mailbox 레지스터) → 버전 다운그레이드 차단을 수행하는 로더를 구현해 변조 이미지 거부를 테스트한다. | mac |
| GPUSYS-M07 | GPU 행 감지와 복구 (TDR) | fence 진행이 일정 시간 멈추면 행으로 판정하는 watchdog, 해당 엔진 소프트 리셋, 원인 컨텍스트 guilty 표시·차단, 무고한 작업 재제출을 구현하고, 무한 루프 커널 주입 시 다른 프로세스 작업이 계속 완료되는지와 복구 시간을 측정한다. | mac |
| GPUSYS-M08 | DMA Copy Engine과 연산 겹치기 | 1D/2D(pitch) 복사 디스크립터를 처리하는 copy engine 스레드와 compute engine 스레드를 모델링하고, 세마포어로 의존성을 건 업로드-연산-다운로드 3단계 파이프라인을 구성해 직렬 대비 처리량 향상을 측정한다. | mac |
| GPUSYS-M09 | GPU 전력 관리: DVFS 거버너와 Power/Clock Gating | P = C·V²·f + 누설 전력 모델과 V-f 테이블을 가진 GPU 모델에 활용률 기반 DVFS 거버너, 전력 상한(TDP) 클램프, 온도 기반 스로틀, 유휴 시 power gating(진입/복귀 지연 포함)을 구현해 워크로드별 에너지와 성능 손실을 비교한다. | mac |
| GPUSYS-M10 | 텔레메트리와 성능 카운터 수집 | 32비트 누적 카운터(busy cycles, energy, ECC count)를 노출하는 mock 레지스터를 주기적으로 읽어 wraparound 안전 델타로 활용률·평균 전력을 계산하는 NVML 유사 API와 링 버퍼 로거를 작성하고, 샘플링 주기에 따른 측정 오차를 분석한다. | mac |
| GPUSYS-A01 | PCIe·NVLink 인터커넥트 대역폭 모델 | PCIe Gen3–Gen6(인코딩, TLP 헤더, MPS, DLLP/ACK 오버헤드)와 NVLink 세대별 링크 수로 실효 단방향 대역폭을 계산하는 모델을 만들고, 전송 크기별 시간 곡선과 P2P vs 호스트 경유 복사 시간을 비교하며 공개 측정치와 대조한다. | mac |
| GPUSYS-A02 | 멀티 GPU Collective: Ring All-Reduce | N개 스레드를 GPU로 보고 reduce-scatter + all-gather 단계의 ring all-reduce를 구현해 결과 정확성을 검증하고, 각 GPU 전송량 2(N−1)/N·S 공식을 측정치와 맞춘 뒤 tree all-reduce와 지연·대역폭 항(α-β 모델)으로 비교한다. | mac |
| GPUSYS-A03 | GPU 메모리 RAS: ECC, 페이지 리타이어, Row Remap | 단일 비트(정정 가능)·다중 비트(정정 불가) 오류를 무작위 주입하는 메모리 모델에서 오류 카운터, 임계 초과 페이지 리타이어 목록(영구 저장), 예비 row 리매핑, 비정정 오류 시 해당 컨텍스트 종료 정책을 구현하고 시나리오별 가용 용량 변화를 보고한다. | mac |
| GPUSYS-A04 | 리눅스 GEM·dma-buf 버퍼 공유와 sync_file | vgem 드라이버로 GEM 버퍼를 만들어 PRIME으로 dma-buf fd를 export/import하고 두 프로세스에서 mmap 공유를 확인한 뒤, vgem fence와 sync_file을 사용해 생산자-소비자 동기화를 구현하고 drm 디버그 로그로 흐름을 추적한다. | **linux** |
| GPUSYS-A05 | GPU 가상화와 분할: SR-IOV·MIG 설계 | SM·메모리 슬라이스·복사 엔진을 정해진 프로파일로 나누는 MIG 유사 파티셔너를 구현해 유효 조합 검증과 단편화를 다루고, 시분할 vGPU 대비 테넌트 간 간섭(지연 변동)과 격리 수준을 시뮬레이션해 설계 문서로 정리한다. | mac |
| GPUSYS-A06 | nvidia-smi·NVML 실측과 모델 대조 | NVML(pynvml)로 클럭, 전력, 온도, 활용률, PCIe 링크 세대/폭, throttle reason을 100ms 주기로 기록하는 로거를 작성해 GEMM 부하 전후를 측정하고, M09 DVFS 모델과 A01 대역폭 모델의 예측을 실측과 대조한다. | **gpu** |
| GPUSYS-A07 | GPU 드라이버·펌웨어 검증 계획 설계 | 지금까지 만든 GPU 시스템 모델(링, 펜스, MMU, 인터럽트, 펌웨어 RPC, TDR)을 대상으로 기능·스트레스·오류 주입·전력 상태 전환 테스트를 포함한 검증 계획을 작성하고, 그중 링 wraparound 스트레스, 펜스 순서, 페이지 폴트 폭주, 펌웨어 무응답 4개 테스트를 자동화해 실행한다. | mac |
| GPUSYS-A08 | 종합: 미니 GPU 시스템 시뮬레이터 | B03~M08 구성요소를 통합해 다중 프로세스가 커맨드 버퍼를 제출하면 스케줄러·MMU·copy/compute 엔진·IH 링·펜스를 거쳐 완료되는 시뮬레이터를 만들고, 처리량·지연 통계와 이벤트 타임라인(Chrome trace JSON)을 출력해 병목을 분석한다. | mac |

## 7단계 · 엣지 AI (Edge AI)

### EML — 디바이스 ML 추론 (On-Device ML Inference)

디바이스 ML 추론은 학습된 모델을 MCU·SoC·NPU 위에서 제한된 메모리와 전력으로 정확하고 빠르게 돌리는 기술이다. 텐서 레이아웃, conv/GEMM 커널, int8 양자화, 메모리 플래닝, 연산자 퓨전을 직접 구현해 봐야 TFLite Micro·ONNX Runtime·llama.cpp 같은 런타임과 NPU 컴파일러가 왜 그렇게 설계됐는지 설명할 수 있다. AI 가속기·엣지 AI 펌웨어 면접에서는 'int8 conv를 C로 구현하고 레퍼런스와 bit-exact로 맞춰라' 같은 문제가 실제로 나온다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| EML-B01 | 텐서 표현: Shape·Stride·Dtype와 NCHW/NHWC | C로 tensor 구조체(dims, strides, dtype, data)와 다차원 인덱스→오프셋 계산, NCHW↔NHWC transpose, view(reshape/slice)를 구현하고, numpy의 transpose/reshape 결과를 파일로 저장해 원소 단위로 일치하는지 검증한다. | mac |
| EML-B02 | CPU GEMM: Naive → Loop 순서 → Cache Blocking | SGEMM을 ijk naive, ikj 루프 순서, 캐시 블로킹(MC/KC/NC), 내부 4x4 레지스터 마이크로커널 순으로 구현해 행렬 크기별 GFLOPS를 측정하고 Accelerate cblas_sgemm 대비 비율을 기록한다. | mac |
| EML-B03 | Fully Connected 층과 활성화 함수 구현 | FC(y=Wx+b)와 활성화 함수(GELU tanh 근사, 빠른 exp 근사 포함)를 C로 구현하고, PyTorch로 생성한 입력·가중치·출력 파일을 로드해 최대 절대/상대 오차가 허용치 이내인지 검증하는 테스트 하네스를 만든다. | mac |
| EML-B04 | Conv2D 직접 구현: Padding·Stride·Dilation·Group | NHWC 입력·OHWI 가중치로 padding(same/valid), stride, dilation, groups를 지원하는 direct conv2d를 C로 구현하고, 무작위 파라미터 조합 200개에 대해 PyTorch conv2d 결과와 일치를 검증하며 출력 크기 공식을 테스트한다. | mac |
| EML-B05 | im2col + GEMM 컨볼루션 | im2col 변환 후 B02의 GEMM으로 conv를 계산하는 버전을 구현해 B04와 결과 일치를 확인하고, 레이어 크기별 im2col 버퍼 추가 메모리와 direct conv 대비 속도를 비교해 MCU에서 im2col이 부담되는 조건을 정리한다. | mac |
| EML-B06 | Depthwise Separable Conv와 Pooling | depthwise 3x3 conv, pointwise 1x1 conv, max/avg pool, global average pool을 구현해 PyTorch와 일치를 검증하고, 같은 입출력 채널에서 표준 conv 대비 MAC 수·파라미터 수 절감 비율을 공식과 실측으로 비교한다. | mac |
| EML-B07 | Softmax·LayerNorm과 BatchNorm 폴딩 | 수치 안정 softmax, LayerNorm, BatchNorm(추론 모드)을 구현하고, conv+BN을 W'=W·γ/σ, b'=(b−μ)·γ/σ+β로 폴딩하는 함수를 작성해 폴딩 전후 출력이 허용 오차 내에서 같은지 검증한다. | mac |
| EML-B08 | int8 Affine 양자화 기초: Scale·Zero-point | min/max로부터 scale·zero-point를 계산하는 함수(0.0이 정확히 표현되도록 nudging), quantize(round-half-away/RNE 선택, saturate)/dequantize를 구현하고, 가우시안·ReLU 출력 분포에서 대칭/비대칭 양자화의 MSE를 비교한다. | mac |
| EML-B09 | 정수 전용 GEMM과 Requantization (Fixed-point Multiplier) | M = S_a·S_w/S_out을 (int32 multiplier, shift)로 분해하는 함수와 SaturatingRoundingDoublingHighMul + RoundingDivideByPOT 기반 requantize를 구현하고, zero-point 보정항을 포함한 int8 GEMM 결과가 TFLite 레퍼런스 공식과 bit-exact로 일치하는지 검증한다. | mac |
| EML-B10 | 모델 파일 들여다보기: ONNX·TFLite 그래프 구조 | 작은 CNN을 PyTorch로 만들어 ONNX와 int8 TFLite로 내보낸 뒤, Python(onnx 패키지, tflite 플랫버퍼 파서)으로 노드 목록·입출력 shape·가중치 크기·텐서별 scale/zero-point를 출력하는 스크립트를 작성하고 Netron으로 본 구조와 대조한다. | mac |
| EML-M01 | Per-channel 양자화와 캘리브레이션 (MinMax·Percentile·KL) | 소형 CNN(MNIST/CIFAR-10)에 대해 가중치 per-tensor vs per-channel, 활성화 MinMax/99.99 percentile/KL-divergence(엔트로피) 캘리브레이션을 구현해 조합별 int8 시뮬레이션 정확도를 FP32 대비 표로 비교한다. | mac |
| EML-M02 | 양자화 연산자: Add·Concat 재스케일과 Fused Activation | 서로 다른 scale의 int8 텐서를 공통 scale로 재스케일해 더하는 quantized add, concat 재양자화, ReLU6을 출력 clamp 범위로 흡수하는 fused activation, int8 avgpool(반올림 나눗셈)을 구현하고 TFLite 결과와 bit-exact 일치를 검증한다. | mac |
| EML-M03 | 미니 그래프 런타임: Op Registry와 인터프리터 | JSON/바이너리 그래프 기술(노드, 입출력 텐서 ID, 파라미터)을 로드해 op registry에서 커널을 찾고 위상 정렬 순서로 prepare(shape 추론, 스크래치 요청) → invoke를 수행하는 C 런타임을 작성해, 앞서 만든 커널들로 소형 CNN 추론이 PyTorch와 일치함을 검증한다. | mac |
| EML-M04 | 메모리 플래닝: 텐서 수명과 Arena 오프셋 할당 | M03 그래프에서 텐서별 [첫 생성, 마지막 사용] 구간을 구하고 greedy-by-size, greedy-by-breadth 오프셋 할당과 in-place 연산 허용을 구현해, MobileNet류 그래프에서 naive 합계 대비 피크 아레나 크기를 비교하고 겹침 없음(수명 겹치는 텐서의 주소 비중첩)을 검증한다. | mac |
| EML-M05 | 연산자 퓨전 패스: Conv+BN+ReLU, Elementwise 체인 | M03 IR 위에서 conv→BN→ReLU 패턴 매칭·폴딩 패스와 연속 elementwise 연산 퓨전 패스를 구현하고, 패스 전후 출력 일치, 노드 수, 중간 텐서 바이트, 실행 시간을 비교한다. | mac |
| EML-M06 | TFLite Micro 해부: 호스트 빌드와 커스텀 연산자 | tflite-micro를 맥 호스트 타깃으로 빌드해 hello_world/person_detection 예제를 실행하고, MicroMutableOpResolver에 필요한 연산자만 등록해 바이너리 크기 변화를 측정하며, 아레나 크기를 이진 탐색으로 최소화하고 커스텀 연산자 하나를 등록·실행한다. | mac |
| EML-M07 | ONNX Runtime: 그래프 최적화와 Execution Provider | ONNX Runtime C/Python API로 모델을 실행하며 graph optimization level(basic/extended/all)별 최적화된 모델을 저장해 노드 변화를 비교하고, CPU EP와 CoreML EP에서 지연을 측정하며 CoreML EP로 넘어가지 못한 노드(파티션)를 로그로 확인한다. | mac |
| EML-M08 | End-to-End 키워드 스포팅: MFCC + DS-CNN int8 in C | Speech Commands로 DS-CNN을 학습·int8 양자화한 뒤 가중치를 C 헤더로 내보내고, C로 구현한 MFCC 프론트엔드(프레이밍, FFT, mel 필터, DCT)와 정수 추론 엔진으로 테스트셋 정확도를 Python 결과와 비교하며 지연·RAM·Flash 사용량을 보고한다. | mac |
| EML-M09 | 순수 C int8 이미지 분류기: TFLite와 Bit-exact 일치 | int8 TFLite 소형 CNN(CIFAR-10)의 가중치·양자화 파라미터를 추출해 자체 C 엔진으로 실행하고, 모든 레이어 출력이 TFLite 인터프리터(intermediate tensor 덤프)와 bit-exact 일치하는지 레이어별로 검증하며 첫 불일치 레이어를 찾는 디버깅 도구를 만든다. | mac |
| EML-M10 | 모델 변환 파이프라인: PyTorch → ONNX → TFLite int8 | MobileNetV2를 PyTorch → ONNX(opset 고정) → TFLite(대표 데이터셋 기반 full-int8 PTQ)로 변환하는 스크립트를 작성해 각 단계 출력 차이와 top-1 정확도 하락을 측정하고, 변환 실패/불일치를 일으키는 연산자 사례와 해결책을 기록한다. | mac |
| EML-M11 | SIMD 커널 최적화: NEON int8 Dot Product | M2의 NEON intrinsics(vdotq_s32, 가능하면 vmmlaq_s32)로 int8 GEMM 마이크로커널과 가중치 사전 패킹 레이아웃을 구현해 스칼라 버전 대비 GOPS와 bit-exact 결과를 확인하고, CMSIS-NN이 Cortex-M에서 같은 문제를 SMLAD로 푸는 방식과 비교 정리한다. | mac |
| EML-M12 | 추론 벤치마크 하네스 | 모델/런타임을 플러그인으로 받아 워밍업, N회 반복, p50/p90/p99 지연, 처리량, 피크 RSS, 콜드 스타트 시간을 측정하고 CSV/JSON으로 내보내는 C/Python 하네스를 작성해 자체 엔진·TFLite·ONNX Runtime을 같은 모델로 비교한다. | mac |
| EML-A01 | GGUF 포맷 파서 (llama.cpp) | GGUF 헤더(magic, version, KV 메타데이터, 텐서 정보 테이블, 정렬)를 파싱하는 C 프로그램을 작성해 작은 모델 파일의 아키텍처 하이퍼파라미터, 텐서 이름·shape·양자화 타입·오프셋을 출력하고, mmap으로 특정 텐서를 읽어 llama.cpp gguf 도구 출력과 대조한다. | mac |
| EML-A02 | 블록 양자화: Q8_0·Q4_0와 양자화 Dot Product | 32원소 블록당 fp16 scale을 갖는 Q8_0, Q4_0(4비트 니블 패킹) quantize/dequantize와 Q4_0×Q8_0 dot product를 C(가능하면 NEON)로 구현해 llama.cpp 구현과 결과 일치를 확인하고, 실제 가중치에서 RMSE와 메모리 크기를 FP16과 비교한다. | mac |
| EML-A03 | C로 트랜스포머 추론: Attention과 KV Cache | llama2.c의 stories15M 체크포인트를 사용하는 추론 엔진을 직접 작성(또는 해부 후 재작성)해 KV cache 유무에 따른 토큰당 연산량과 속도를 비교하고, 레퍼런스 출력 토큰 시퀀스와 일치(greedy 디코딩)함을 검증한다. | mac |
| EML-A04 | MCU 배포: Cortex-M에서 TFLM 실행 | Cortex-M 타깃(QEMU mps2-an386 등)으로 TFLM + CMSIS-NN을 크로스 빌드해 KWS 모델을 실행하고, 링커 맵으로 가중치(.rodata in Flash)·아레나(SRAM) 배치를 확인하며 DWT 사이클 카운터(보드 시)나 명령 수 기반으로 레이어별 비용을 측정한다. | **qemu** |
| EML-A05 | 빠른 컨볼루션: Winograd F(2x2,3x3) | Winograd F(2x2,3x3) 입력/필터/출력 변환 행렬을 사용한 conv를 구현해 direct conv와 결과를 비교하고, 곱셈 수 감소(36→16), FP32/FP16에서의 오차 증가, int8에서 변환 후 값 범위가 커지는 문제를 정량화한다. | mac |
| EML-A06 | 가지치기와 희소 추론: 언제 빨라지는가 | 소형 MLP/CNN에 magnitude pruning(희소도 50~95%)을 적용해 정확도 변화를 측정하고, CSR 희소 GEMV와 밀집 GEMV를 C로 구현해 희소도별 속도 교차점(break-even)을 찾으며 구조적 희소성(블록/2:4)이 왜 하드웨어에 유리한지 정리한다. | mac |
| EML-A07 | 혼합 정밀도와 층별 양자화 민감도 분석 | MobileNetV2에서 층 하나씩만 int8로 양자화(나머지 FP32)해 정확도 하락을 측정하는 민감도 분석 스크립트를 작성하고, 상위 민감 층을 FP16/int16으로 남긴 혼합 정밀도 구성과 QAT(fake quant 몇 epoch) 결과를 PTQ와 비교한다. | mac |
| EML-A08 | 온디바이스 추론 런타임 시스템 설계 | 로봇 카메라 파이프라인(검출 30fps + 분류 + KWS 상시)을 CPU/GPU/NPU가 있는 SoC에서 돌리는 런타임을 설계해 모델 파티셔닝·NPU 미지원 연산자 CPU fallback·공유 아레나·우선순위 스케줄링·데드라인 미스 처리를 문서화하고, 스케줄러와 fallback 분배 로직을 C 시뮬레이션으로 구현한다. | mac |

### AIPERF — AI 워크로드 · 성능 지표 (AI Workloads & Perf Metrics)

AI 가속기 회사의 엔지니어는 'TOPS가 높으면 빠른가?'라는 질문에 숫자로 답할 수 있어야 한다. 이 트랙은 TOPS·TOPS/W·지연·처리량·TTFT·tokens/s 같은 지표의 정확한 정의부터 roofline·산술 강도·KV cache 크기·트랜스포머 FLOP 계산, MLPerf 방법론, 전력 측정까지를 직접 계산하고 맥에서 실측하며 익힌다. NPU 아키텍처·성능 엔지니어 면접에서는 '이 레이어는 이 칩에서 memory-bound인가?', '7B 모델 decode tokens/s 상한은?' 같은 봉투 뒷면 계산이 거의 항상 나온다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| AIPERF-B01 | 지표 정의: FLOPs vs FLOPS, OPS, TOPS | MAC 유닛 수·클럭·정밀도별 처리율·희소성 배수를 입력받아 피크 TOPS/TFLOPS를 계산하는 Python 도구를 작성하고, 공개 칩 3종(예: 모바일 NPU, Jetson, 데이터센터 GPU)의 발표 수치를 재현하며 'dense int8' 기준으로 정규화한 비교표를 만든다. | mac |
| AIPERF-B02 | 지연 vs 처리량과 백분위 지연 | C로 작성한 행렬곱 추론 루프를 N회 실행해 지연 히스토그램, p50/p90/p99/max를 계산하고, 요청 도착률을 높여 가는 큐 시뮬레이션으로 처리량-지연 곡선(포화 시 지연 급증)을 그려 Little's law(L=λW)를 검증한다. | mac |
| AIPERF-B03 | 연산자 FLOP·바이트 계산기 | 연산자 종류·shape·dtype을 받아 FLOP, 가중치/입력/출력 바이트, 산술 강도를 계산하는 Python 모듈을 작성하고, ResNet-50(약 4.1 GMAC)과 MobileNetV2(약 300 MMAC) 전체 합계를 공개 수치와 비교 검증한다. | mac |
| AIPERF-B04 | Roofline 대역폭 천장 정하기: STREAM 결과 해석 | MEM-B11의 STREAM 측정(스레드 수·P/E 코어별)에서 roofline에 쓸 지속 대역폭 천장을 정하고, 이 값과 모델 크기·양자화 비트로 배치 1 디코딩의 tokens/s 상한을 계산해 llama.cpp 실측과 비교한다. | mac |
| AIPERF-B05 | 피크 연산 처리량 마이크로벤치마크 | 독립 누산기 개수를 1~16으로 바꾸는 NEON FMA 루프로 단일 코어 GFLOPS를 측정해 FMA 지연×처리율로 필요한 누산기 수를 설명하고, 전체 코어(P/E 코어 구분) 피크와 이론치를 비교한다. | mac |
| AIPERF-B06 | Roofline 모델 구축과 커널 배치 | B04·B05 측정치로 M2 CPU roofline을 그리고, 벡터 덧셈·GEMV·블록 GEMM·conv 커널의 산술 강도와 실측 GFLOPS를 점으로 찍어 각 커널이 어느 천장에 막혔는지와 ridge point 의미를 설명하는 리포트를 생성한다. | mac |
| AIPERF-B07 | 에너지 지표: TOPS/W와 데이터 이동 비용 | Horowitz 수치(int8 MAC, SRAM 읽기, DRAM 읽기의 pJ)를 사용해 한 conv 레이어의 MAC 에너지 vs SRAM/DRAM 접근 에너지 분해를 계산하는 모델을 만들고, 데이터 재사용(타일링) 정도에 따라 TOPS/W가 어떻게 달라지는지 그래프로 보인다. | mac |
| AIPERF-B08 | 모델 메모리 풋프린트 계산 | 모델 그래프로부터 파라미터 바이트(fp32/fp16/int8/int4), 피크 활성화 메모리(수명 기반), 런타임 오버헤드를 계산하는 도구를 작성해 MobileNetV2, ResNet-50, 1B/7B LLM이 512KB SRAM MCU, 8GB 모바일, 24GB 맥에 들어가는지 표로 판정한다. | mac |
| AIPERF-B09 | 활용률 지표: MFU와 MBU | 모델 FLOP·바이트 계산기와 실측 지연을 결합해 MFU/MBU를 산출하는 함수를 작성하고, 맥에서 측정한 CNN 추론과 LLM decode에 적용해 어떤 워크로드가 MFU가 낮고 MBU가 높은지 해석한다. | mac |
| AIPERF-M01 | 레이어별 Roofline 분석: 가상 NPU에서 CNN | 가상 엣지 NPU(예: 4 TOPS int8, 8 GB/s DRAM, 1MB SRAM)를 정의하고 MobileNetV2·ResNet-50 각 레이어의 산술 강도로 예상 시간(max(연산시간, 메모리시간))을 계산해 memory-bound 레이어 비율과 총 지연, 실효 TOPS를 보고한다. | mac |
| AIPERF-M02 | 배치 크기 효과: 가중치 재사용과 지연 비용 | FC/GEMM 레이어에 대해 배치 1~256의 산술 강도·예상 시간을 roofline 모델로 계산하고, 맥에서 Accelerate GEMM으로 실측해 배치별 처리량·지연 곡선을 모델과 비교하며 지연 SLA(예: 10ms) 하 최적 배치를 구한다. | mac |
| AIPERF-M03 | 트랜스포머 FLOP 계산: Prefill vs Decode | Llama 구조(hidden, layers, heads, kv_heads, FFN, vocab)를 입력받아 층별 GEMM·attention FLOP을 세는 계산기를 작성해 Llama-2-7B 파라미터 수와 토큰당 FLOP(≈2N)을 검증하고, 시퀀스 길이에 따라 attention FLOP 비중이 커지는 지점과 prefill/decode의 산술 강도를 비교한다. | mac |
| AIPERF-M04 | KV Cache 크기 계산과 동시 사용자 한도 | KV bytes = 2 × layers × kv_heads × head_dim × bytes × seq_len × batch 계산기를 작성해 Llama-2-7B(MHA)·Llama-3-8B(GQA)에서 토큰당 KV 크기를 구하고, 24GB/80GB 메모리에서 모델 가중치를 뺀 공간으로 4K/32K 컨텍스트 동시 시퀀스 수를 계산하며 KV int8 양자화 효과를 보인다. | mac |
| AIPERF-M05 | LLM 지표 실측: TTFT·TPOT·Tokens/s | llama.cpp(llama-bench, llama-server)로 소형 모델을 실행해 프롬프트 길이별 TTFT, TPOT(토큰 간 지연), 출력 tokens/s, 전체 지연을 측정하는 스크립트를 작성하고, prefill tokens/s와 decode tokens/s가 왜 수십 배 차이 나는지 M03 계산과 연결해 설명한다. | mac |
| AIPERF-M06 | Decode는 Memory-bound: Tokens/s 상한 예측 | 모델 가중치 크기(양자화 포함)와 KV 읽기량으로 M2 GPU에서의 decode tokens/s 상한을 예측하고 llama.cpp 실측값과 비교해 달성 MBU를 구하며, 예측과 실측 차이의 원인(커널 효율, dequant 비용, 오버헤드)을 정리한다. | mac |
| AIPERF-M07 | 계층적 Roofline: L1/L2/DRAM | 작업 셋 크기를 바꾸는 대역폭 측정으로 L1/L2(SLC)/DRAM 대역폭 천장을 구해 계층적 roofline을 그리고, EML-B02 GEMM의 블로킹 크기별 성능 점을 찍어 어떤 계층 천장에 막혀 있는지 분석한다. | mac |
| AIPERF-M08 | 추론 프로파일링: 연산자별 타임라인 | EML-M03 런타임에 연산자별 시작/종료 타임스탬프 계측을 넣어 Chrome trace(JSON)로 내보내고, ONNX Runtime 프로파일러 출력과 Instruments(Time Profiler/xctrace)로 같은 모델을 프로파일링해 상위 시간 연산자와 roofline 위치를 연결한 보고서를 작성한다. | mac |
| AIPERF-M09 | 전력 측정: powermetrics로 Energy per Inference | sudo powermetrics로 CPU/GPU/ANE 전력을 100ms 주기로 기록하면서 같은 모델을 CPU·GPU(Metal)·ANE(Core ML)에서 반복 실행하고, 유휴 전력을 빼 추론당 에너지(mJ)와 실효 TOPS/W를 계산해 백엔드별로 비교한다. | mac |
| AIPERF-M10 | MLPerf Inference 방법론과 미니 LoadGen | MLPerf 시나리오 규칙(Server: 포아송 도착 + p99 지연 제약, Offline: 최대 처리량, SingleStream: p90 지연)을 따르는 미니 LoadGen을 Python으로 구현해 자체 추론 엔진에 적용하고, Server 시나리오에서 지연 제약을 만족하는 최대 QPS를 이진 탐색으로 찾는다. | mac |
| AIPERF-M11 | MLPerf Tiny 방법론: KWS 벤치마크 재현 | MLPerf Tiny의 KWS(DS-CNN) 참조 모델을 받아 호스트에서 정확도 목표 충족을 확인하고, 반복 추론의 중앙값 지연 측정 절차를 규칙대로 재현하는 스크립트를 작성하며 실제 보드 제출 시 에너지 측정 구성(EnergyRunner, 전원 분리)을 정리한다. | mac |
| AIPERF-M12 | End-to-End 파이프라인 분석: 전처리·추론·후처리 | 카메라 프레임 → 리사이즈·정규화 → 검출 모델 → NMS 파이프라인을 구성해 구간별 지연과 Amdahl 법칙에 따른 최대 가속비를 계산하고, 추론만 3배 빨라질 때와 전처리를 GPU로 옮길 때의 전체 fps 향상을 예측·실측 비교한다. | mac |
| AIPERF-A01 | 가속기 스펙 시트 비판적 분석 | 공개 엣지 AI 칩 2–3종의 스펙(피크 TOPS, DRAM 대역폭, on-chip SRAM, 지원 정밀도)으로 ResNet-50 batch 1 지연과 7B LLM decode tokens/s를 roofline 기반으로 추정하고, 공개 벤치마크 결과와 비교해 utilization 차이와 원인을 1페이지 분석 보고서로 작성한다. | mac |
| AIPERF-A02 | Attention IO 복잡도: Naive vs FlashAttention | naive attention(Θ(Nd + N²) HBM 접근)과 tiled FlashAttention(Θ(N²d²/M) 접근)의 HBM 바이트를 시퀀스 길이 N, head_dim d, SRAM 크기 M에 대해 계산하는 모델을 만들고, GPGPU-A03 커널 실측 시간과 비교해 모델 정확도를 평가한다. | mac |
| AIPERF-A03 | LLM 서빙 스케줄링: Continuous Batching 시뮬레이션 | 요청 도착(포아송), 입력/출력 길이 분포, prefill·decode 단계 시간 모델(M03/M06 기반)을 가진 서빙 시뮬레이터를 작성해 static batching, continuous batching, chunked prefill을 비교하고 TTFT p99·TPOT·goodput 곡선을 그린다. | mac |
| AIPERF-A04 | 양자화의 성능-정확도 트레이드오프 실측 | 같은 모델을 F16, Q8_0, Q5_K, Q4_K, Q3_K로 양자화해 llama.cpp에서 tokens/s(prefill/decode)와 wikitext perplexity를 측정하고, 모델 바이트 대비 tokens/s가 M06 대역폭 모델을 따르는지와 품질 하락 곡선을 함께 그려 파레토 지점을 찾는다. | mac |
| AIPERF-A05 | 분산 추론 통신 비용: Tensor/Pipeline Parallel | tensor parallel(층당 all-reduce 2회)과 pipeline parallel(스테이지 간 활성화 전송, 버블)을 링크 대역폭·지연 파라미터로 모델링해 7B/70B 모델의 decode 토큰 지연과 처리량을 칩 수 1~8에서 계산하고, 칩 간 링크가 느린 엣지 환경에서의 최적 분할을 제시한다. | mac |
| AIPERF-A06 | 성능 회귀 감지: 노이즈와 통계 검정 | 같은 바이너리 반복 측정으로 노이즈 분포(열, 백그라운드, 코어 배치)를 수집하고, 부트스트랩 신뢰구간과 Mann-Whitney U 검정으로 두 빌드 간 회귀를 판정하는 스크립트를 작성해 인위적 2%/5% 지연 주입을 검출력과 오탐률로 평가한다. | mac |
| AIPERF-A07 | 열 스로틀링과 지속 성능 | GPU/ANE 추론을 20분 이상 연속 실행하며 처리량·전력·(가능하면) 온도를 시계열로 기록해 스로틀링 시작 시점과 지속 성능 비율을 구하고, 벤치마크 보고 시 워밍업·쿨다운·환경 온도 조건을 명시하는 규칙을 정리한다. | mac |
| AIPERF-A08 | DVFS와 에너지 최적 동작점 | 동적 전력(CV²f), 누설 전력, 유휴 전력, V-f 곡선, memory-bound 비율을 가진 모델로 주파수별 추론 지연·에너지를 계산해 compute-bound와 memory-bound 워크로드의 에너지 최적 주파수가 다름을 보이고, 데드라인 제약 하 최소 에너지 동작점을 찾는다. | mac |
| AIPERF-A09 | 설계 면접: 엣지 AI 칩 비교 벤치마크 스위트 | 로봇/차량용 엣지 AI 칩 2종을 평가하는 벤치마크 스위트를 설계해 워크로드 선정(CNN, 트랜스포머, LLM decode), 정확도 게이트, 시나리오, 전력·열 조건, 통계 처리, 보고 지표를 문서화하고, 지금까지 만든 도구들로 맥 CPU/GPU/ANE를 '두 칩'으로 가정해 샘플 보고서를 생성한다. | mac |

### NPU — AI 가속기 (NPU) (AI Accelerators (NPU))

NPU(AI 가속기)는 MAC 어레이·on-chip SRAM·DMA·명령 프로세서·펌웨어·컴파일러가 하나로 설계된 칩이며, 성능의 대부분은 '데이터를 얼마나 적게, 제때 옮기느냐'에서 결정된다. 이 트랙은 systolic array 사이클 시뮬레이터를 C로 직접 만들고, 그 위에 dataflow·타일링·DMA 더블 버퍼링·toy ISA·커맨드 큐 펌웨어·호스트 드라이버·그래프 컴파일러·golden model 검증까지 쌓아 올려 하나의 '나만의 NPU' 스택을 완성한다. Don의 SSD 펌웨어(큐·DMA·커맨드 처리)와 실리콘 검증 경험을 AI 가속기 펌웨어/아키텍처/검증 직무로 연결하는 핵심 트랙이다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| NPU-B01 | MAC 유닛 모델: int8×int8→int32 누산과 비트폭 분석 | 부호 있는/없는 int8 곱과 N비트 누산기를 갖는 MAC을 C로 모델링해 최악 입력(-128×-128)에서 누산 가능한 최대 K를 공식과 실험으로 구하고, int32 누산기 포화 vs wraparound 동작과 int16 누산기가 왜 부족한지 테스트로 보인다. | mac |
| NPU-B02 | Systolic Array 개념: 데이터 스큐와 타이밍 표 | 4x4 어레이에서 A 행렬 행을 사이클마다 한 칸씩 스큐해 왼쪽에서, B 열을 위에서 넣을 때 각 PE가 어느 사이클에 어떤 a·b 쌍을 곱하는지 표를 생성하는 프로그램을 작성하고, 첫 출력과 마지막 출력 사이클을 (2N+K−2 형태) 공식과 대조한다. | mac |
| NPU-B03 | Weight-Stationary Systolic Array 사이클 시뮬레이터 (C) | PE마다 weight 레지스터·입력 전달 레지스터·부분합 전달 레지스터를 갖는 R×C WS 어레이를 2-phase(조합 계산 후 일괄 레지스터 갱신) 사이클 시뮬레이터로 C에 구현해, 무작위 int8 GEMM 결과를 레퍼런스와 비교하고 weight 로딩 포함 총 사이클이 해석 공식과 일치함을 검증한다. | mac |
| NPU-B04 | Output-Stationary 시뮬레이터와 Dataflow 비교 | B03 프레임워크에 OS 어레이(PE가 출력 누산기를 보유, A·B가 흐름)를 추가해 정답 일치를 검증하고, M×K×N 모양(정사각, K가 큰 경우, N=1 GEMV)별 사이클·SRAM 읽기 수·PE 간 이동 횟수를 WS와 표로 비교한다. | mac |
| NPU-B05 | Dataflow 분류: WS·OS·IS·Row-Stationary 접근 횟수 분석 | conv 레이어 하나를 7중 루프로 쓰고 루프 순서·어떤 텐서를 PE 레지스터에 고정하는지에 따라 레지스터/글로벌 버퍼/DRAM 접근 횟수를 세는 Python 분석기를 작성해 WS/OS/IS/RS의 접근 수와 Horowitz 에너지 가중치 합을 비교한다. | mac |
| NPU-B06 | 어레이 활용률: 레이어 Shape와 어레이 크기 매핑 | R×C 어레이에 GEMM(M,K,N)을 매핑할 때 fold 수, 패딩 낭비, 파이프라인 fill/drain을 포함한 실효 활용률을 계산하는 함수를 작성하고, 32×32와 128×128 어레이에서 MobileNetV2 각 레이어(특히 depthwise, 작은 채널)의 활용률을 표로 비교한다. | mac |
| NPU-B07 | 루프 네스트 타일링과 DRAM 트래픽 | GEMM과 conv를 타일 루프 + 내부 루프로 분해하고 타일 크기·루프 순서·버퍼 용량을 입력받아 텐서별 DRAM 읽기/쓰기 바이트를 계산하는 모델을 작성해, 몇 가지 타일링을 비교하고 최소 트래픽 하한(통신 하한)과의 차이를 확인한다. | mac |
| NPU-B08 | 수 포맷 구현: int4·fp16·bf16·fp8(E4M3/E5M2) | fp32 ↔ fp16/bf16/E4M3/E5M2 변환을 round-to-nearest-even, 포화 모드, 서브노멀, NaN/Inf 규칙대로 C로 구현하고, 모든 fp8/fp16 비트 패턴과 무작위 fp32 값에 대해 Python ml_dtypes 결과와 전수 비교한다. | mac |
| NPU-B09 | NPU 블록 다이어그램과 Toy NPU 스펙 작성 | TPUv1, NVDLA, Ethos-U55, Gemmini의 구성요소(MAC 어레이, unified buffer, accumulator, activation unit, DMA, command processor)를 비교표로 정리하고, 16×16 int8 WS 어레이·256KB SRAM·DMA 2채널·벡터 유닛·명령 큐를 갖는 toy NPU의 블록도·메모리 맵·레지스터 맵 스펙 문서를 작성한다. | mac |
| NPU-M01 | SRAM 타일 크기 탐색과 버퍼 분할 | toy NPU의 256KB SRAM에서 conv 레이어의 타일 크기(출력 H/W 타일, 입력/출력 채널 타일)와 버퍼 분할을 전수/휴리스틱 탐색해 DRAM 트래픽 최소 해를 찾는 도구를 작성하고, 선택된 타일링으로 B03 시뮬레이터를 돌려 정답 일치와 예측 트래픽을 검증한다. | mac |
| NPU-M02 | 다차원 Strided DMA 엔진과 디스크립터 체인 | src/dst 주소, 3차원 크기·stride, 버스트 크기, next 포인터, 완료 인터럽트 플래그를 갖는 디스크립터 포맷과 DMA 엔진 모델을 구현해 DRAM의 NHWC 타일을 SRAM의 채널 블록 레이아웃으로 이동하고, 경계 타일·정렬 위반·버스트 효율(바이트/트랜잭션)을 테스트한다. | mac |
| NPU-M03 | 더블 버퍼링: DMA와 연산 오버랩 스케줄 | DMA 대역폭·지연과 어레이 연산 시간을 가진 이벤트 시뮬레이터에서 단일 버퍼, 더블 버퍼, 트리플 버퍼 스케줄을 구현해 레이어별 총 시간·DMA stall·compute stall을 측정하고, 타일 크기 변화에 따라 병목이 DMA ↔ 연산으로 바뀌는 지점을 찾는다. | mac |
| NPU-M04 | Toy NPU ISA, 어셈블러와 기능 시뮬레이터 | B09 스펙의 64비트 명령 인코딩(opcode, SRAM 주소, 크기, 플래그, 의존성 토큰)을 정의하고 텍스트 어셈블러와 명령 단위 기능 시뮬레이터를 작성해, 손으로 짠 tiled GEMM 프로그램이 레퍼런스와 일치하고 잘못된 의존성 토큰 시 데이터 해저드가 검출되는지 테스트한다. | mac |
| NPU-M05 | 커맨드 큐와 NPU 펌웨어 디스패처 | 호스트 제출 링(작업 디스크립터 + 도어벨)과 완료 큐를 두고, 펌웨어 메인 루프(pthread로 내부 MCU 모델)가 작업을 꺼내 DMA·MAC·벡터 엔진 큐로 명령을 분배하고 완료 시 인터럽트·상태를 기록하게 구현하며, 잘못된 디스크립터·엔진 타임아웃·큐 overflow 처리 경로를 테스트한다. | mac |
| NPU-M06 | 호스트 드라이버 인터페이스: 모델 로드부터 Wait까지 | npu_open/load_model/alloc_buffer/submit/wait/close 사용자 API와 이를 처리하는 커널 드라이버 모델(ioctl 디스패치, 버퍼 핀·IOMMU 매핑 테이블, 펌웨어 메일박스 RPC, 완료 IRQ → waitqueue)을 C로 구현해, 다중 프로세스 제출과 프로세스 종료 시 자원 정리를 테스트한다. | mac |
| NPU-M07 | Requantization과 후처리 유닛 (Bit-exact) | int32 누산 → bias add → per-channel (multiplier, shift) requant → zero-point → clamp → 활성화 LUT(sigmoid/GELU 256-entry 또는 piecewise linear)를 하는 후처리 유닛 모델을 작성해, EML-B09 레퍼런스 및 TFLite 결과와 bit-exact 일치를 전수/무작위 테스트로 검증한다. | mac |
| NPU-M08 | 그래프 Lowering과 CPU Fallback 파티셔닝 | conv/relu/add/pool/softmax로 된 소형 그래프를 입력받아 NPU 지원 여부로 서브그래프를 분할하고, NPU 서브그래프는 M01 타일링 + M04 ISA 명령 시퀀스로 lowering, 나머지는 CPU 커널 호출로 바꾸는 컴파일러를 작성해 전체 실행 결과를 PyTorch와 비교한다. | mac |
| NPU-M09 | NPU 컴파일러 스케줄링·SRAM 할당·레이어 퓨전 | M08 출력 명령 스트림에 대해 SRAM 버퍼를 구간 그래프 색칠(interval allocation)로 할당하고, 연속 conv 두 레이어를 타일 단위로 퓨전해 중간 활성화를 DRAM에 쓰지 않게 하는 패스를 구현해, 퓨전 전후 DRAM 트래픽·총 사이클·SRAM 피크를 비교한다. | mac |
| NPU-M10 | Golden Model 비교와 불일치 트리아지 | numpy 정수 golden model과 NPU 시뮬레이터 출력을 레이어·타일 단위로 비교하는 하네스를 작성하고, 반올림 모드 차이·누산기 포화·채널 순서 오류·타일 경계 오프셋 버그를 일부러 주입해 첫 불일치 타일·원소 좌표·오차 패턴으로 원인을 자동 분류하는 리포트를 출력한다. | mac |
| NPU-M11 | Systolic Array에 안 맞는 연산: Depthwise·Elementwise·Softmax | depthwise conv, elementwise add, softmax, layernorm을 (a) 어레이에 억지로 매핑할 때와 (b) SIMD 벡터 유닛(예: 16-lane)으로 처리할 때의 사이클을 모델링하고, MobileNetV2·소형 트랜스포머에서 이 연산들이 전체 시간에 차지하는 비율(Amdahl)과 벡터 유닛 폭 선택을 정리한다. | mac |
| NPU-M12 | bf16/fp8 MAC 데이터패스와 누산 정밀도 | B08 포맷으로 bf16×bf16, E4M3×E4M3 곱을 정확히 계산해 fp32(또는 축소 정밀도) 누산기에 더하는 MAC 모델을 구현하고, GEMM K 크기별 fp32 레퍼런스 대비 상대 오차와, 누산기를 fp16으로 줄이거나 누산 순서를 바꿨을 때의 오차 증가를 측정한다. | mac |
| NPU-A01 | 2:4 구조적 희소성: 압축 포맷과 희소 MAC | 가중치를 2:4 패턴으로 가지치기·압축(값 배열 + 메타데이터)하는 함수와, 메타데이터로 활성화를 선택(mux)해 곱하는 희소 MAC/systolic 모델을 구현해 밀집 결과와 일치를 검증하고, 사이클 2배 이득과 메타데이터 대역폭 오버헤드를 정량화한다. | mac |
| NPU-A02 | Microscaling(MX) 포맷: MXFP8·MXFP4·MXINT8 | OCP MX 규칙(블록 32, E8M0 공유 스케일, 원소 E4M3/E2M1/int8)으로 quantize/dequantize와 블록 단위 dot product(블록별 스케일 곱 후 fp32 누산)를 구현하고, 실제 LLM 가중치·활성화에서 per-tensor fp8, int8 대비 오차와 비트당 품질을 비교한다. | mac |
| NPU-A03 | 해석적 성능 모델 (Timeloop/SCALE-Sim lite)과 검증 | 어레이 크기·dataflow·SRAM 크기·DRAM 대역폭을 입력받아 레이어별 사이클과 DRAM 접근을 계산하는 Python 해석 모델을 작성해 B03/M03 시뮬레이터 및 SCALE-Sim 결과와 오차를 비교하고, 어레이 크기 × SRAM 크기 설계 공간의 성능/면적 대용 지표 파레토를 그린다. | mac |
| NPU-A04 | 트랜스포머를 NPU에 매핑: Attention·KV Cache | 소형 트랜스포머 블록(QKV/출력 projection, attention, FFN, RMSNorm, softmax)을 toy NPU 자원에 매핑하는 스케줄을 작성하고, prefill과 decode(배치 1, KV cache DRAM 상주)의 레이어별 사이클·DRAM 바이트를 계산해 decode가 GEMV·KV 읽기로 memory-bound가 되는 것을 보이며 개선안(배치, KV 양자화, 가중치 스트리밍)을 비교한다. | mac |
| NPU-A05 | 멀티코어 NPU와 NoC: 레이어 분할 전략 | 4코어 toy NPU와 공유 L2 SRAM, 메시 NoC(링크 대역폭·홉 지연) 모델을 만들고, conv/GEMM을 출력 채널 분할, 공간(H) 분할(halo 교환), 레이어 파이프라인 분할로 나눠 실행 시간·NoC 트래픽·동기화 대기를 시뮬레이션해 레이어 유형별 최적 전략을 정리한다. | mac |
| NPU-A06 | 성능 카운터(PMU)와 펌웨어 트레이싱 | MAC 활용 사이클, DMA 바이트, stall 원인별(DMA 대기, 의존성, SRAM 뱅크 충돌) 카운터를 시뮬레이터에 추가하고, 펌웨어가 작업 단위 이벤트를 링 버퍼에 기록·호스트가 수집해 Chrome trace 타임라인으로 변환하는 도구를 만들어 한 모델의 병목을 진단한 리포트를 작성한다. | mac |
| NPU-A07 | NPU 전력 관리: 클록 게이팅·파워 도메인·DVFS | MAC 어레이·SRAM 뱅크·DMA를 별도 파워 도메인으로 갖는 toy NPU 전력 모델(동적/누설, 게이팅 진입·복귀 지연, SRAM retention)을 만들고, 유휴 감지 기반 게이팅과 레이어 특성(memory-bound면 저주파) 기반 DVFS 정책을 구현해 추론당 에너지와 지연 증가를 비교한다. | mac |
| NPU-A08 | NPU 검증 계획: 다층 Golden 비교와 커버리지 | 연산자·레이어·네트워크·시스템(펌웨어, 드라이버, 전력 전환) 수준 검증 계획을 작성하고, 제약 무작위 연산자 파라미터 생성기 + golden 비교 + 기능 커버리지(shape 경계, 포화, 0 스케일 등 빈) 수집기를 구현해 시뮬레이터에서 커버리지 100%까지 회귀를 돌리고 발견된 버그를 기록한다. | mac |
| NPU-A09 | Systolic Array RTL과 Verilator 공동 시뮬레이션 | 4×4 int8 weight-stationary systolic array를 Verilog/SystemVerilog로 작성하고 Verilator C++ 테스트벤치에서 B03 C 모델과 같은 무작위 GEMM을 구동해 출력·사이클 수를 비교하며, 파형(VCD)으로 스큐·유효 신호 타이밍을 확인한다. | **fpga** |
| NPU-A10 | 실제 NPU 툴체인: Arm Ethos-U Vela 컴파일러 | int8 TFLite 모델(MobileNetV2, KWS)을 Vela로 Ethos-U55/U65 구성별로 컴파일해 NPU/CPU 연산자 분할, SRAM/Flash 사용량, 예상 사이클 리포트를 수집하고, 자체 M08/M09 컴파일러와 A03 해석 모델의 예측을 비교해 차이 원인을 분석한다. | mac |
| NPU-A11 | Apple Neural Engine 실측: Core ML Compute Units | coremltools로 CNN과 소형 트랜스포머를 Core ML로 변환해 computeUnits를 CPU_ONLY/CPU_AND_GPU/CPU_AND_NE/ALL로 바꿔 지연·전력(powermetrics ANE)을 측정하고, Xcode Core ML 성능 리포트로 ANE에서 fallback된 연산자를 찾아 모델 수정(레이아웃, 연산자 대체)으로 ANE 실행 비율을 높인다. | mac |
| NPU-A12 | 종합: 나만의 NPU 스택 End-to-End | EML-M09의 int8 CNN을 M08/M09 컴파일러로 toy ISA로 컴파일하고, M06 드라이버 → M05 펌웨어 → B03/M03 사이클 시뮬레이터에서 실행해 golden과 bit-exact 일치, 총 사이클·DRAM 바이트·추정 에너지·PMU 타임라인을 출력하는 데모와 아키텍처 문서(면접 포트폴리오용)를 완성한다. | mac |

## 8단계 · 산업 도메인 (Industry Domains)

### DRONE — 드론 비행 제어 (Drone Flight Control)

드론 비행 제어기(FC)는 수 kHz 자이로 루프, 자세 추정, 캐스케이드 PID, 모터 믹싱, ESC/RC 프로토콜, 페일세이프가 한 MCU 위에서 결정론적으로 돌아가는 대표적인 하드 리얼타임 펌웨어다. PX4·ArduPilot·Betaflight의 구조를 직접 작은 코드로 재구현해 보면 Anduril·Skydio·Shield AI 같은 드론/방산 회사 면접에서 묻는 '루프 레이트, 지연, 프로토콜 비트 레벨, 실패 처리'를 한 번에 다질 수 있다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| DRONE-B01 | FC 아키텍처: 태스크와 루프 레이트 골격 | 8 kHz gyro, 4 kHz PID, 1 kHz attitude, 250 Hz estimator, 50 Hz RX 태스크를 가진 시뮬레이션 스케줄러를 C로 작성하고, 가짜 tick으로 각 태스크 실제 실행 주기·지터·오버런 횟수를 측정해 출력한다. | mac |
| DRONE-B02 | 좌표계와 쿼터니언 기초 (NED/FRD, Euler) | 쿼터니언 곱·정규화·벡터 회전·Euler(ZYX)↔quat↔DCM 변환 라이브러리를 C로 구현하고, 무작위 자세 왕복 변환 오차와 pitch=±90° gimbal lock 케이스를 단위 테스트로 검증한다. | mac |
| DRONE-B03 | IMU 파이프라인: 스케일·바이어스·보드 회전 | ±2000 dps / ±16 g 설정의 raw gyro·accel 샘플을 rad/s, m/s²로 변환하고, 정지 상태 N초 평균으로 gyro 바이어스를 추정하며 board alignment(예: yaw 90°) 회전을 적용하는 코드를 작성해 합성 데이터로 검증한다. | mac |
| DRONE-B04 | 자이로 필터링: PT1 저역통과 + Biquad 노치 | PT1 LPF와 biquad notch를 C로 구현해 8 kHz 샘플 합성 gyro(실제 운동 + 200 Hz 모터 노이즈)에 적용하고, 노이즈 감쇠량과 50 Hz에서의 위상 지연(ms)을 계산·출력해 필터 컷오프 선택 트레이드오프를 설명한다. | mac |
| DRONE-B05 | 상보 필터(Complementary) 자세 추정 | roll/pitch 상보 필터를 구현하고, 시뮬레이션된 회전 + gyro 바이어스 + 가속 외란 데이터로 실제값 대비 RMS 오차를 alpha 값별로 비교한다. | mac |
| DRONE-B06 | 단일축 Rate PID (Betaflight 스타일) | deg/s setpoint를 받는 단일축 rate PID를 C로 구현하고(D-on-measurement, I-term anti-windup, throttle 기반 TPA 포함), 1축 관성+모터 지연 플랜트 시뮬레이션에서 스텝 응답 overshoot·settling time을 측정한다. | mac |
| DRONE-B07 | 모터 믹서: Quad-X 믹싱 행렬과 포화 처리 | Quad-X 믹싱 행렬로 4개 모터 출력을 계산하고, 출력이 [0,1]을 벗어날 때 throttle을 이동시켜 자세 권한을 보존하는 airmode식 desaturation을 구현해 극단 입력 테스트 케이스로 검증한다. | mac |
| DRONE-B08 | ESC 신호: PWM / OneShot125 타이머 설정 | 가상 타이머(클럭·prescaler·ARR·CCR 레지스터 mock)에 대해 표준 PWM(1000–2000 µs, 50/400 Hz)과 OneShot125(125–250 µs) 출력용 레지스터 값을 계산하는 드라이버를 작성하고, throttle→CCR 변환 분해능을 표로 비교한다. | mac |
| DRONE-B09 | RC 입력 1: SBUS 프레임 디코딩 | 100000 baud 8E2 반전 UART로 들어오는 SBUS 바이트 스트림 파서를 작성해 0x0F 헤더 동기화, 16채널 11-bit 언패킹, frame-lost/failsafe 플래그 처리를 구현하고, 직접 만든 인코더로 생성한 테스트 벡터로 검증한다. | mac |
| DRONE-M01 | DShot 프레임 인코딩과 CRC | DShot 패킷 인코더(CRC = (v ^ v>>4 ^ v>>8) & 0xF, 특수 명령 0–47)와 DShot600용 타이머 DMA duty 버퍼 생성기를 C로 작성하고, 디코더로 왕복 검증 + 비트 타이밍 표(T0H/T1H)를 출력한다. | mac |
| DRONE-M02 | Bidirectional DShot: eRPM 텔레메트리 디코딩 | bidirectional DShot 응답의 21비트 GCR 스트림을 디코딩(GCR→nibble, 반전 CRC 검사, 지수/가수 형식의 period→eRPM→RPM 변환)하는 함수를 작성하고, 인코더로 만든 정상·비트오류 벡터로 검증한다. | mac |
| DRONE-M03 | RC 입력 2: CRSF/ELRS 프로토콜 파서 | CRSF 스트림 파서를 상태 머신으로 구현해 RC_CHANNELS_PACKED(16×11bit)와 LINK_STATISTICS를 디코딩하고, CRC8(poly 0xD5) 오류·잘린 프레임·잡음 바이트 주입 테스트를 통과시킨다. | mac |
| DRONE-M04 | 추력 선형화와 배터리 전압 보상 | 추력∝RPM² 모델과 배터리 전압 처짐을 가진 모터 시뮬레이터에서 thrust linearization과 vbat 보상을 구현해 배터리 100%→30% 구간에서 rate 루프 응답(오버슈트·상승시간) 변화가 줄어드는지 확인한다(Mahony AHRS는 see:SENS-A01). | mac |
| DRONE-M05 | Angle→Rate 캐스케이드 자세 제어 | 쿼터니언 자세 오차 → P 제어로 body rate setpoint → B06 rate PID로 이어지는 3축 캐스케이드를 구현하고, 3축 강체 + 모터 1차 지연 시뮬레이션에서 큰 각도 명령(45°) 응답과 내부/외부 루프 대역폭 비율 영향을 실험한다. | mac |
| DRONE-M06 | MAVLink v2 파서와 메시지 송신 | MAVLink v2 바이트 스트림 파서(STX 0xFD, 페이로드 끝 0 트렁케이션, CRC_EXTRA 포함 체크섬)를 작성해 HEARTBEAT·ATTITUDE를 송수신하고, pymavlink로 생성한 프레임과 상호 검증하며 seq 기반 패킷 손실률을 계산한다. | mac |
| DRONE-M07 | GPS 파싱: NMEA GGA/RMC + UBX NAV-PVT | NMEA 체크섬(XOR) 검증 + GGA/RMC 필드 파서와 UBX 프레임 파서(0xB5 0x62, class/id/len, Fletcher-8 CK_A/CK_B)로 NAV-PVT를 디코딩하는 코드를 작성하고, 섞인 스트림·깨진 체크섬·fix 상실 로그로 테스트한다. | mac |
| DRONE-M08 | 비행 모드 상태 머신과 Arming 체크 | 비행 모드 상태 머신을 테이블 기반으로 구현하고(모드별 진입 조건: GPS fix, 센서 healthy, throttle low 등), 허용/거부 전이를 모두 커버하는 단위 테스트와 전이 다이어그램을 작성한다. | mac |
| DRONE-M09 | 다층 페일세이프: RC 손실·배터리·GPS·지오펜스 | RC 링크 손실 타임아웃, 배터리 저전압 2단계, GPS 손실, 지오펜스 위반을 감지해 우선순위에 따라 비행 모드를 강제 전환하는 failsafe 매니저를 구현하고, 동시 다발 실패 시나리오 테스트로 정책을 검증한다. | mac |
| DRONE-M10 | 블랙박스 로깅: 고속 링버퍼 + 예측/가변길이 인코딩 | Betaflight blackbox처럼 이전값 예측 + zigzag varint로 gyro/PID/motor 프레임을 인코딩하고 링버퍼→(mock) 플래시 페이지 쓰기 경로를 구현해, 2 kHz 로깅 시 압축률·필요 쓰기 대역폭·버퍼 오버플로 여부를 측정한다. | mac |
| DRONE-A01 | 위치 제어: Position→Velocity→Thrust 벡터→자세 | 위치 P → 속도 PID → 가속도 setpoint → (중력 보상) 추력 벡터 → yaw를 고려한 자세 쿼터니언 + 집합 추력 변환을 구현하고, 기울기 제한·추력 포화 처리를 넣어 6-DOF 시뮬레이터에서 웨이포인트 추종을 검증한다. | mac |
| DRONE-A02 | 고도 추정: 기압계 + 가속도 칼만 융합 | 상태 [고도, 수직속도, accel 바이어스] 칼만 필터를 구현해 합성 기압(노이즈·프롭워시 오프셋) + 수직가속도 데이터로 고도 추정 성능을 상보 필터와 비교하고, Q/R 튜닝 효과를 그래프로 보여준다. | mac |
| DRONE-A03 | Error-State EKF 자세 추정 (PX4 EKF2 개념) | 명목 쿼터니언 + 6차원 오차상태(자세오차, gyro 바이어스) ESKF를 구현해 accel/mag 측정 갱신, 오차 주입·리셋을 수행하고, Mahony 대비 바이어스 추정과 공분산 일관성(NEES)을 검증한다. | mac |
| DRONE-A04 | RPM 기반 동적 노치 필터 | M02에서 얻은 모터별 RPM으로 1·2·3차 고조파 노치 중심주파수를 매 루프 갱신하는 biquad 뱅크를 구현하고, 스로틀 스윕 시뮬레이션에서 고정 노치 대비 노이즈 감쇠·위상 지연·CPU 사이클을 비교한다. | mac |
| DRONE-A05 | SITL 구축: 6-DOF 쿼드 동역학 + 펌웨어 루프 | 모터 추력/토크 모델·강체 6-DOF·센서 노이즈 모델을 가진 쿼드 시뮬레이터를 작성하고, 앞에서 만든 추정기·제어기·믹서를 HAL 인터페이스 뒤에 두어 lockstep으로 실행, 이륙→호버→웨이포인트→착륙 시나리오를 자동 테스트로 만든다. | mac |
| DRONE-A06 | Rate 루프 시스템 식별과 자동 튜닝 | SITL에서 rate 축에 chirp 입력을 주어 주파수 응답(이득·위상)을 추정하고, 1차+지연 모델 피팅 → 위상여유 목표 기반 PID 게인 산출 → 재검증까지 자동으로 수행하는 스크립트를 작성한다. | mac |
| DRONE-A07 | 루프 지연·지터가 안정성에 미치는 영향 분석 | SITL에 센서→모터 지연(0.5–5 ms)과 랜덤 지터를 주입하는 옵션을 추가하고, 지연별 스텝 응답·진동 발생 임계값을 측정해 '지연 1 ms = 몇 도 위상 손실' 표와 예산 설계를 정리한다. | mac |
| DRONE-A08 | 시스템 설계: FC + 컴패니언 컴퓨터 자율 드론 | FC(MCU, 제어·페일세이프)와 컴패니언(Linux, 인지·계획) 사이 MAVLink 오프보드 인터페이스, 하트비트 감시, 링크 손실 시 FC 단독 안전 동작, 시간 동기를 포함한 설계 문서와 오프보드 setpoint 감시 코드 경로를 작성하고 트레이드오프를 정리한다. | mac |

### AUTO — 차량용 임베디드 (Automotive Embedded)

차량용 임베디드는 수십~수백 개 ECU가 CAN/CAN-FD/LIN/FlexRay/Automotive Ethernet으로 묶이고, AUTOSAR 계층·UDS 진단·ISO 26262 기능안전이 사실상 표준인 도메인이다. Tesla·Rivian·GM·현대 등 면접에서는 DBC 신호 패킹, ISO-TP/UDS, 부트로더 플래싱, E2E 보호, ASIL 분해를 실제 코드와 설계로 설명할 수 있는지를 본다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| AUTO-B01 | AUTOSAR Classic 계층: BSW·RTE·SWC 최소 구현 | Rte_Read/Rte_Write 스타일 포트 API를 생성하는 작은 RTE 스텁과 두 SWC(센서 → 제어 runnable)를 C로 작성하고, 주기 runnable 스케줄러 위에서 신호 흐름을 단위 테스트로 검증한다. | mac |
| AUTO-B02 | CAN 수신 필터/마스크 뱅크 계산기 | ECU가 받아야 할 ID 목록을 입력받아 제한된 개수의 ID/mask 필터 뱅크로 덮는(불필요 ID 최소화) 계산기를 작성하고, 표준/확장 ID 혼합과 bxCAN 32/16비트 필터 모드 차이를 테스트한다(CAN 프레임·중재·비트 타이밍은 see:COMM-M04~M06). | mac |
| AUTO-B03 | K-Matrix 설계: 신호 → 메시지 패킹과 ID 우선순위 | 신호 목록(크기·주기·마감·송신 ECU)을 입력받아 메시지로 패킹하고 deadline-monotonic으로 CAN ID를 할당하는 도구를 작성해 버스 부하율을 계산하고, A08 응답 시간 분석의 입력으로 내보낸다. | mac |
| AUTO-B04 | DBC 신호 packing/unpacking (Intel/Motorola) | Intel(little)/Motorola(big) 신호의 pack/unpack 함수를 C로 작성(부호 확장, factor/offset, 범위 클램프 포함)하고, cantools로 생성한 기대값과 무작위 신호 수천 개로 왕복 비교 테스트를 통과시킨다. | mac |
| AUTO-B05 | DBC 파서 + C 코드 생성기 | Python으로 DBC 서브셋 파서를 작성해 메시지/신호 정의를 읽고 C 구조체 + pack/unpack 함수를 생성하며, 생성 코드가 B04 레퍼런스 구현 및 cantools와 일치하는지 자동 테스트한다. | mac |
| AUTO-B06 | LIN: PID 패리티·체크섬·스케줄 테이블 | LIN 2.x 보호 ID(P0/P1 패리티), classic/enhanced 체크섬, break/sync 필드를 포함한 프레임 인코더/디코더와 마스터 스케줄 테이블 실행기를 작성하고, 가상 슬레이브(창문 모터 등)와 응답 테스트를 수행한다. | mac |
| AUTO-B07 | CAN 로그 분석: candump/ASC 파싱과 주기 이상 탐지 | candump/Vector ASC 로그 파서를 작성해 B05 DBC 디코더로 신호 시계열을 만들고, 메시지별 주기 지터·누락·카운터 점프를 탐지하는 리포트를 생성한다(CAN-FD 프레임 구조는 see:COMM-M08). | mac |
| AUTO-B08 | 주기 메시지 송신 스케줄러와 버스 부하 | 메시지 테이블(ID, 주기, DLC, 송신 타입)을 받아 송신 스케줄러를 구현하고, 스터핑 최악값 포함 버스 부하율을 계산하는 도구를 작성해 부하 70% 초과 시 경고를 낸다. 오프셋 배치로 버스트를 줄이는 효과도 측정한다. | mac |
| AUTO-B09 | ECU 전원 모드와 네트워크 관리 (KL15/KL30, NM) | KL30/KL15 입력, 웨이크업 소스, NM 메시지 수신을 이벤트로 받는 ECU 전원 상태 머신과 NM(Repeat Message/Normal/Ready Sleep/Prepare Bus-Sleep) 타이머 로직을 구현하고, 시나리오 테스트로 버스 동기 슬립을 검증한다. | mac |
| AUTO-M01 | DoIP (ISO 13400): 이더넷 진단과 라우팅 활성화 | UDP 차량 식별 응답, TCP 라우팅 활성화, 진단 메시지 ACK/NACK을 처리하는 DoIP 엔티티를 C로 구현하고, M02 UDS 서버를 DoIP 위에서 돌려 python 테스터와 상호 운용을 확인한다(ISO-TP는 see:COMM-A07). | mac |
| AUTO-M02 | UDS 서버 코어: 세션·디스패치·NRC·타이밍 | SID 테이블 기반 UDS 서버를 ISO-TP 위에 구현해 0x10/0x11/0x3E와 응답 대기 NRC 0x78(responsePending), 세션별 서비스 허용, S3 세션 타임아웃을 처리하고 테스터 스크립트로 정상/오류 경로를 검증한다. | mac |
| AUTO-M03 | UDS 보안 접근(0x27)과 DID 읽기/쓰기(0x22/0x2E) | SecurityAccess 0x27 seed/key(실습용 키 알고리즘 + 실패 카운터·잠금 지연)와 ReadDataByIdentifier/WriteDataByIdentifier를 구현하고, 보안 레벨이 없으면 NRC 0x33을 돌려주는 등 접근 제어 테스트를 작성한다. | mac |
| AUTO-M04 | DTC 관리: 상태 바이트·디바운스·Freeze Frame | 고장 이벤트에 카운터 기반 디바운스를 적용해 DTC 상태 바이트 8비트(testFailed, confirmedDTC 등)를 갱신하고, freeze frame 저장, ReadDTCInformation(0x19 서브펑션 0x01/0x02/0x04) 및 ClearDTC(0x14)를 구현해 시나리오로 검증한다. | mac |
| AUTO-M05 | E2E 보호: CRC + Alive Counter + Data ID | Profile 1 스타일(CRC8 SAE-J1850, 4비트 카운터, Data ID)의 E2E_Protect/E2E_Check와 수신 상태(OK/REPEATED/WRONG_SEQUENCE/ERROR) 평가를 구현하고, 반복·누락·지연·비트플립·마스커레이드 결함을 주입해 모두 검출되는지 확인한다. | mac |
| AUTO-M06 | CAN 부트로더: UDS 기반 플래시 프로그래밍 | mock 플래시(섹터 erase/program)를 가진 부트로더 UDS 서버와 테스터 측 플래싱 툴(Python)을 작성하고, 블록 시퀀스 카운터, CRC 검증, 중간 전원 차단 후 재시도, 애플리케이션 유효성 플래그 처리를 테스트한다. | mac |
| AUTO-M07 | 워치독과 프로그램 흐름 감시 (WdgM) | 체크포인트 기반 감시 모듈(alive 카운트, deadline 측정, 허용 전이 그래프 logical supervision)과 윈도우 워치독 mock을 구현하고, 무한 루프·순서 이탈·지연 결함 주입 시 안전 상태(출력 차단)로 가는지 검증한다. | mac |
| AUTO-M08 | SOME/IP 직렬화와 Service Discovery | SOME/IP 16바이트 헤더(Message ID=Service/Method, Length, Request ID, Protocol/Interface Version, Message Type, Return Code) 직렬화와 UDP 위 request/response, 간단한 SD OfferService/FindService를 C++로 구현해 맥 로컬 소켓으로 두 프로세스 간 통신을 검증한다. | mac |
| AUTO-M09 | Automotive Ethernet TSN: gPTP와 Time-Aware Shaper | gPTP의 Sync/Follow_Up/Pdelay 타임스탬프로 link delay와 offset을 계산하는 시뮬레이터, 그리고 트래픽 클래스별 게이트 제어 목록(GCL)을 받아 프레임 최악 지연을 계산하는 TAS 시뮬레이터를 작성한다. | mac |
| AUTO-M10 | FlexRay 정적/동적 세그먼트 스케줄 설계 | 주기·크기가 다른 메시지 집합을 FlexRay 정적 슬롯(base cycle + repetition)에 배치하는 스케줄러를 작성하고, 슬롯 사용률과 메시지별 최대 지연을 계산해 정적/동적 세그먼트 배분 트레이드오프를 정리한다. | mac |
| AUTO-A01 | ISO 26262 HARA와 ASIL 분해 | 전동 조향(EPS) 기능에 대해 HARA 표를 작성하고 S/E/C → ASIL 결정 함수를 코드로 구현한 뒤, ASIL D 안전 목표를 ASIL B(D)+B(D) 두 채널로 분해하는 아키텍처와 독립성(공통원인 고장) 논증을 문서로 작성한다. | mac |
| AUTO-A02 | 안전 메커니즘 구현: 1oo2D 비교·RAM/ROM 테스트 | 다양화된 두 계산 경로 비교(1oo2D), March C- RAM 테스트, ROM CRC, 설정 레지스터 readback 검사를 구현하고, 결함 주입 캠페인으로 각 메커니즘의 진단 커버리지(%)를 측정해 표로 정리한다. | mac |
| AUTO-A03 | Cause-Effect Chain 지연 분석과 LET | 센서(10 ms) → 퓨전(20 ms) → 제어(5 ms) → 액추에이터 runnable 체인을 시뮬레이션해 최악 data age/reaction time을 측정하고, 같은 체인을 Logical Execution Time 방식으로 바꿨을 때 지연·지터 결정성을 비교한다. | mac |
| AUTO-A04 | AUTOSAR Adaptive: ara::com 스타일 proxy/skeleton | M08 SOME/IP 위에 ara::com을 흉내 낸 C++ proxy/skeleton(FindService, 이벤트 구독, 메서드 future 반환)을 구현하고, 서비스 재시작·버전 불일치 시 동작을 테스트하며 Classic과의 설계 차이를 정리한다. | mac |
| AUTO-A05 | 차량 OTA: A/B 업데이트와 Uptane 메타데이터 검증 | Uptane 단순화 모델(root/targets/snapshot/timestamp 서명 메타데이터, 버전 롤백 방지)을 검증하는 Python 클라이언트와 ECU A/B 슬롯 스위치(시험 부팅 → 확정/자동 롤백) 상태 머신을 구현하고, 변조·재생·freeze 공격 시나리오를 테스트한다. | mac |
| AUTO-A06 | CAN 보안: SecOC (MAC + Freshness Value) | AES-CMAC 기반 truncated MAC과 freshness counter(전송 시 하위 비트만 송신, 수신 측 재구성)를 붙인 SecOC 송수신 모듈을 구현하고, 재생·위조·카운터 동기 상실 시나리오와 CAN-FD 페이로드 오버헤드를 평가한다. | mac |
| AUTO-A07 | ADAS 센서 퓨전: 레이더+카메라 객체 레벨 융합 | 합성 레이더(거리·속도 정확) + 카메라(횡위치·분류 정확) 객체 리스트를 받아 게이팅·연관·칼만 갱신·트랙 생성/삭제를 수행하는 융합기를 작성하고, 단일 센서 대비 위치 RMSE와 false track 비율을 비교한다. | mac |
| AUTO-A08 | CAN 최악 응답 시간 분석과 게이트웨이 설계 | 메시지 집합의 우선순위·주기·지터로 CAN WCRT를 반복 계산하는 도구를 구현하고 B08 시뮬레이터 최악 관측값과 비교한 뒤, 두 버스 사이 게이트웨이(필터·라우팅 테이블·큐) 추가 시 end-to-end 마감 충족 여부를 분석한다. | mac |

### AERO — 항공우주 · 방산 (Aerospace & Defense)

항공우주·방산 펌웨어는 DO-178C/DO-254 인증, ARINC 429/MIL-STD-1553 같은 전용 버스, ARINC 653 파티셔닝, 삼중화·락스텝 이중화, 방사선(SEU) 대응, FDIR가 기본 전제다. SpaceX·Anduril·Northrop·Lockheed·Blue Origin 면접에서는 '결정론, 추적성, 고장 허용'을 코드 수준에서 설명할 수 있는지를 보기 때문에, 각 개념을 작은 구현과 결함 주입으로 직접 확인한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| AERO-B01 | DO-178C 추적성: 요구사항↔코드↔테스트 매트릭스 | 코드 주석과 테스트에 박힌 요구사항 태그(@req HLR-xx/LLR-xx)를 수집해 양방향 추적 매트릭스를 만들고 미추적 요구사항·고아 코드/테스트를 보고하는 Python 도구를 작성해 샘플 프로젝트에 적용한다. | mac |
| AERO-B02 | 구조적 커버리지: Statement·Decision·MC/DC | N개 조건의 불리언 결정식에 대해 각 조건의 독립 영향을 보이는 MC/DC 최소 테스트 셋을 생성하는 도구를 작성하고, clang 소스 커버리지(-fprofile-instr-generate)와 비교해 statement/branch만으로는 놓치는 경우를 예시로 보인다. | mac |
| AERO-B03 | ARINC 429 워드 인코딩/디코딩 | label 비트 반전, SDI, BNR(2의 보수·resolution)과 BCD 데이터, SSM, 홀수 패리티를 처리하는 encode/decode 라이브러리를 C로 작성하고, 고도(label 203)·대기속도 등 예제 워드와 패리티 오류 케이스로 검증한다. | mac |
| AERO-B04 | MIL-STD-1553B 워드와 BC-RT 트랜잭션 | 1553 20비트 워드(3비트 sync + 16 data + parity)의 Manchester II 인코더/디코더와 Command/Status 워드 필드 파서를 작성하고, BC→RT, RT→BC, RT→RT 트랜잭션과 응답 타임아웃을 시뮬레이션한다. | mac |
| AERO-B05 | 순환 실행기(Cyclic Executive) 설계 | 주기·WCET가 주어진 태스크 집합에 대해 minor frame 크기 후보를 계산(조건 검사)하고 프레임 테이블을 생성하는 도구와, 그 테이블을 실행하며 프레임 오버런을 검출하는 C 실행기를 작성한다. | mac |
| AERO-B06 | CCSDS Space Packet 인코딩/디코딩 | 버전·타입·secondary header flag·APID·sequence flags·sequence count·data length 필드를 패킹하는 인코더/디코더와 CRC-16-CCITT 오류 검출 트레일러를 구현하고, 시퀀스 카운트 랩어라운드·갭 검출을 테스트한다. | mac |
| AERO-B07 | TMR 다수결 투표기와 중간값 선택 | 정수의 비트 단위 다수결 ((a&b)|(b&c)|(a&c)), 부동소수 센서의 중간값 선택 + 허용오차 비교로 고장 채널을 식별하는 voter를 작성하고, 단일·이중 결함 주입 시 출력 정확성과 검출 여부를 표로 정리한다. | mac |
| AERO-B08 | EDAC 메모리 래퍼: (72,64) 코덱 적용과 CE/UE 처리 | MEM-B07의 SECDED 코덱으로 mock RAM 래퍼(쓰기 시 인코딩, 읽기 시 디코딩·정정 후 write-back)를 만들고 CE/UE 카운터·주소 로그·UE 시 안전 모드 진입 콜백을 구현해 비트 플립 주입으로 검증한다. | mac |
| AERO-B09 | 메모리 스크러빙 태스크 설계 | B08 EDAC가 적용된 mock 메모리에 포아송 분포 SEU를 주입하는 시뮬레이터와 백그라운드 스크러버를 작성하고, 스크럽 주기에 따른 정정/정정불가 오류 발생률을 측정해 요구 신뢰도를 만족하는 주기를 도출한다. | mac |
| AERO-M01 | ARINC 653 시간 분할 파티션 스케줄러 | 파티션별 윈도우(offset, duration) 구성 파일을 읽어 major frame을 반복 실행하는 시뮬레이터를 작성하고, 파티션 내 우선순위 프로세스, 윈도우 초과 시 강제 선점, 한 파티션의 폭주가 다른 파티션에 영향을 주지 않음을 테스트로 보인다. | mac |
| AERO-M02 | ARINC 653 포트 (Sampling/Queuing)와 Health Monitor | Sampling port(최신값 + validity/age)와 Queuing port(FIFO, 오버플로 정책)를 구현하고, 오류 유형별 HM 테이블(무시/로그/파티션 재시작/모듈 정지)을 적용하는 health monitor를 작성해 결함 시나리오로 검증한다. | mac |
| AERO-M03 | FDIR: 고장 검출·격리·복구 계층 설계 | 자세 센서 3종(자이로, 태양센서, 스타트래커)의 고장 검출 규칙(범위·변화율·교차 일관성, N-of-M persistence)과 격리·재구성·safe mode 진입을 테이블 기반으로 구현하고, 고장 주입 시나리오별 반응 시간을 측정한다. | mac |
| AERO-M04 | 3중 IMU 이중화 관리: 감시·선택·페일오버 | 3개 IMU 신호에 대해 쌍별 비교 기반 고장 격리, 중간값 선택, 2채널로 축소 시 비교 모니터 모드, 전환 시 출력 과도(transient) 억제를 구현하고 드리프트·고착·노이즈 증가 결함을 주입해 검증한다. | mac |
| AERO-M05 | 락스텝(Lockstep) 비교 시뮬레이션 | 작은 명령어 집합 VM 두 개를 지연 1~2사이클 차이로 동시 실행하며 레지스터/버스 출력을 비교하는 락스텝 시뮬레이터를 작성하고, 레지스터 비트플립 주입 시 검출 지연과 공통원인 결함(같은 입력 오류)은 못 잡는다는 점을 실험으로 보인다. | mac |
| AERO-M06 | CCSDS TM Transfer Frame과 패킷 다중화 | B06 패킷들을 고정 길이 TM 프레임(VCID, master/VC frame count, First Header Pointer, idle 채움, OCF)으로 다중화하는 송신기와, 프레임 손실 상황에서도 패킷을 재조립하는 수신기를 작성해 검증한다. | mac |
| AERO-M07 | 텔레커맨드 처리: PUS 서비스 1/3/17 | CCSDS TC 패킷을 받아 체크섬·권한·파라미터 검증 후 PUS Service 1 acceptance/start/completion 보고를 내고, Service 3 주기 하우스키핑 리포트 정의·생성, Service 17 ping을 처리하는 온보드 명령 처리기를 구현한다. | mac |
| AERO-M08 | GNC 기초: Strapdown INS + GPS 보정 | 2D(또는 3D) strapdown INS(자세 적분, 가속도 회전·적분)를 구현해 IMU 바이어스로 인한 위치 오차 성장을 측정하고, 1 Hz GPS 위치로 loosely-coupled 칼만 필터 보정을 추가해 오차를 비교한다. | mac |
| AERO-M09 | 보안 데이터링크: AES-GCM + 재전송 방지 | AES-256-GCM(OpenSSL/mbedTLS)으로 페이로드를 암호화하고 헤더를 AAD로 인증하며, 64비트 시퀀스와 슬라이딩 윈도우로 재전송을 거부하는 링크 계층을 작성하고 nonce 재사용 금지·키 회전 절차를 테스트한다. | mac |
| AERO-M10 | 골든 이미지 부트 선택과 부트 카운터 | 골든(ROM)·프라이머리·백업 이미지 슬롯, CRC/서명 검사, 연속 부팅 실패 카운터, 워치독 리셋 원인 기록을 바탕으로 부팅 이미지를 고르는 부트 셀렉터를 구현하고 모든 손상 조합에서 안전 이미지로 귀결되는지 테스트한다. | mac |
| AERO-A01 | DO-178C 미니 프로젝트: 고도 경보 기능 | 고도 경보(altitude alerting) 기능의 HLR/LLR을 작성하고, C 구현 + MISRA 위반 점검(cppcheck misra addon) + 요구사항 기반 테스트 + MC/DC 달성 + B01 추적 매트릭스까지 완성해 DAL B 목표 대비 갭을 정리한다. | mac |
| AERO-A02 | WCET 정적 분석: CFG 경로와 루프 바운드(IPET) | 작은 함수의 CFG(기본 블록 사이클 비용·루프 바운드 주석)를 입력받아 최장 경로를 구하는 IPET 근사(DAG 최장 경로 + 루프 전개)를 구현하고, 측정 최댓값(see:TEST-A04)과 비교해 비관도를 분석한다. | mac |
| AERO-A03 | SEU 결함 주입 캠페인과 소프트웨어 내방사선 기법 | 대상 제어 루프 코드에 메모리·변수 비트플립을 무작위 주입하는 하네스를 만들고, 기본/CFC 서명/변수 TMR 적용 버전의 SDC(silent data corruption)·검출·크래시 비율을 수천 회 캠페인으로 비교한다. | mac |
| AERO-A04 | 비잔틴 고장과 4중 이중화 비행 컴퓨터 동기화 | 4채널 FCC 프레임 동기 시뮬레이터에서 교차 채널 데이터 교환과 OM(1) 합의를 구현하고, 채널 하나가 수신자마다 다른 값을 보내는 비잔틴 결함 하에서도 정상 채널이 같은 출력을 내는지 검증한다. | mac |
| AERO-A05 | 위성 자세 제어(ADCS): 쿼터니언 PD + 반작용 휠 | 강체 회전 동역학 + 반작용 휠 모델을 시뮬레이션하고 쿼터니언 오차 PD 자세 제어로 slew 기동을 수행한 뒤, 외란 토크로 휠 속도가 포화될 때 자기토커(magnetorquer) 기반 모멘텀 덤핑 로직을 추가해 검증한다. | mac |
| AERO-A06 | 키 제로화와 변조 감지 (암호 모듈 경계) | 키 로드·사용·제로화 상태 머신과 변조 이벤트(케이스 개방, 전압/온도 이상)에 대한 즉시 제로화 경로(컴파일러 최적화에 지워지지 않는 secure memzero 포함)를 구현하고, 제로화 후 키 잔존 여부를 메모리 덤프로 검증한다. | mac |
| AERO-A07 | MIL-STD-1553 BC 스케줄러 + RT 구현 | B04 시뮬레이터 위에 major/minor frame 메시지 스케줄을 실행하는 BC와 서브어드레스 버퍼를 가진 RT를 구현하고, 무응답·Message Error 비트·Bus A 고장 시 Bus B 재시도 정책과 버스 사용률을 테스트한다. | mac |
| AERO-A08 | 방산 자율 체계 펌웨어 설계 (Anduril 스타일) | 자율 요격/정찰 드론을 가정해 임무 상태 머신(Launch/Transit/Search/Track/Engage-hold/RTB)과 lost-link 자율 동작, 사람 승인(HITL) 게이트, 지오펜스·무장 안전 인터록을 포함한 설계 문서와 핵심 상태 머신 코드를 작성하고 시나리오 테스트로 검증한다. | mac |

### AUTON — 자율 시스템 (Autonomous Systems)

자율 시스템은 인지(perception) → 위치추정(localization) → 계획(planning) → 제어로 이어지는 스택과, 그 위를 감시하는 안전 모니터·시뮬레이션 검증으로 이루어진다. 저수준 제어(RCTL)와 미들웨어(ROS)는 다른 트랙에서 다루고, 여기서는 Scout AI·Anduril·Shield AI·Zoox 면접에서 자주 나오는 로컬라이제이션, 맵핑, 경로 계획, 행동 결정, 충돌 예측, 안전 아키텍처를 직접 구현한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| AUTON-B01 | 자율 스택 구조와 지연 예산 | 각 단계가 타임스탬프를 붙여 메시지를 넘기는 4단계 파이프라인 골격을 C++로 작성하고(스레드 + bounded 큐), 센서 획득부터 액추에이터 명령까지 end-to-end 지연 분포를 측정해 단계별 지연 예산 표와 비교한다. | mac |
| AUTON-B02 | 좌표 변환: SE(2)/SE(3)와 센서 외부 파라미터 | SE(2)/SE(3) 변환 클래스(합성, 역변환, 점 변환, 보간)를 구현하고, 외부 파라미터로 라이다 포인트를 맵 프레임으로 옮기는 테스트와 변환 순서를 뒤집었을 때 생기는 오류를 단위 테스트로 보인다. | mac |
| AUTON-B03 | 휠 오도메트리와 오차 누적 | 엔코더 틱으로 차동 구동 로봇 자세를 적분하는 오도메트리를 작성하고, 바퀴 반경·트레드 오차와 슬립 노이즈를 넣은 몬테카를로 시뮬레이션으로 자세 오차 타원이 커지는 모습을 시각화한다. | mac |
| AUTON-B04 | 라이다 포인트 클라우드 전처리 | 합성(또는 공개 KITTI 일부) 라이다 스캔에 voxel grid 다운샘플링, RANSAC 평면 지면 제거, KD-tree 기반 유클리드 클러스터링을 C++로 구현하고 클러스터별 3D 바운딩 박스와 처리 시간을 출력한다. | mac |
| AUTON-B05 | Occupancy Grid 매핑: log-odds + 레이 캐스팅 | 알려진 자세에서 얻은 2D 라이다 스캔으로 log-odds occupancy grid를 갱신(빈 셀/점유 셀, 클램핑)하는 매퍼를 구현하고, 시뮬레이션 환경 지도를 재구성해 정답 지도와 셀 정확도를 비교한다. | mac |
| AUTON-B06 | 코스트맵 계층: 정적·인플레이션·장애물 레이어 | B05 occupancy grid 위에 정적 지도·로봇 반경 인플레이션(지수 감쇠 비용)·실시간 장애물 레이어를 합성하는 코스트맵을 구현하고, ALGO-A01 A*가 레이어 변경에 따라 경로를 바꾸는지 테스트한다(A* 자체는 see:ALGO-A01). | mac |
| AUTON-B07 | Behavior Tree 엔진 구현 | C++로 작은 BT 엔진(Sequence, Fallback, Inverter, Retry, Timeout, Action/Condition 리프, blackboard)을 작성하고, '배터리 낮으면 귀환, 아니면 순찰' 트리를 tick 로그와 단위 테스트로 검증한다. | mac |
| AUTON-B08 | 계층적 상태 머신(HSM) 미션 관리 | Idle/Mission(Navigate, Inspect)/Recovery/Emergency를 가진 HSM(진입/퇴장 액션, 상위 상태 이벤트 위임)을 구현해 B07 BT와 같은 미션을 재작성하고, 기능 추가 시 변경량·가독성 트레이드오프를 정리한다. | mac |
| AUTON-B09 | End-to-end 지연 추적: 센서→결정 타임스탬프 체인 | 센서 메시지에 원본 타임스탬프를 붙여 단계별(전처리→추적→계획) 처리 시각을 기록하는 추적 라이브러리를 만들고, 합성 파이프라인에서 단계별 지연 분포와 예산 초과율을 리포트한다(센서 시간 정렬은 see:SENS-M08). | mac |
| AUTON-M01 | EKF 로컬라이제이션: 오도메트리 + 랜드마크 | 차동 구동 운동 모델과 알려진 랜드마크 range-bearing 측정 모델로 EKF를 구현하고, 야코비안 수치 검증, 각도 정규화 처리, NEES 일관성 검사로 필터가 과신하지 않는지 확인한다. | mac |
| AUTON-M02 | 파티클 필터 로컬라이제이션 (MCL) | occupancy grid 위에서 likelihood field 측정 모델과 low-variance 리샘플링을 쓰는 MCL을 구현하고, 전역 위치추정(kidnapped robot 포함)에서 파티클 수에 따른 수렴 성공률과 계산 시간을 측정한다. | mac |
| AUTON-M03 | 스캔 매칭: 2D ICP | KD-tree 최근접 대응, 이상치 거부, SVD 기반 rigid 정합을 반복하는 point-to-point ICP를 구현하고, 초기 자세 오차 크기별 수렴 영역과 point-to-line 변형의 수렴 속도 차이를 실험한다. | mac |
| AUTON-M04 | 다중 객체 추적: 데이터 연관 + 트랙 관리 | B04 클러스터 중심을 입력으로 등속 칼만 트랙, Mahalanobis 게이팅, 헝가리안 알고리즘 연관, M-of-N 트랙 확정/삭제를 구현하고, 교차 이동 시나리오에서 ID switch 수와 MOTA를 측정한다. | mac |
| AUTON-M05 | 라이다-카메라 융합: 투영과 연관 | 핀홀 + 왜곡 모델로 라이다 점을 카메라 영상에 투영하는 함수를 구현하고, 2D 바운딩 박스 안에 들어온 3D 점으로 객체 거리를 추정하는 late fusion을 작성해 외부 파라미터 오차가 연관 정확도에 주는 영향을 측정한다. | mac |
| AUTON-M06 | Hybrid A*: 차량 운동학 제약 계획 | (x, y, θ) 이산 셀에 연속 상태를 저장하는 Hybrid A*와 최소 회전반경 기반 전·후진 동작, 목표 근처 Dubins 해석 확장을 구현하고, 주차 시나리오에서 그리드 A* 대비 실행 가능 경로를 보여준다. | mac |
| AUTON-M07 | RRT / RRT* 샘플링 기반 계획 | 2D 다각형 장애물 환경에서 RRT와 RRT*(near 반경, rewire)를 구현하고, 반복 수에 따른 경로 비용 수렴과 실행 시간을 비교하며 goal bias·충돌 체크 해상도 영향을 실험한다. | mac |
| AUTON-M08 | 로컬 플래너: Dynamic Window Approach | (v, ω) 동적 윈도우 샘플링, 궤적 전방 시뮬레이션, 목표 방향·장애물 거리·속도 비용 가중합으로 명령을 고르는 DWA를 구현하고, 전역 A* 경로를 따라가며 갑자기 나타난 장애물을 회피하는 시나리오로 검증한다. | mac |
| AUTON-M09 | 동적 장애물 예측과 충돌 시간(TTC) | 등속·등각속도(원운동) 예측 모델을 구현하고, 두 원형 물체의 상대 운동에서 최초 충돌 시각을 해석적(2차 방정식)·수치적(근 찾기)으로 구하는 함수를 작성해 엣지 케이스(평행, 접선, 이미 겹침) 테스트를 통과시킨다. | mac |
| AUTON-M10 | 안전 모니터: Doer/Checker 아키텍처 | 계획기 출력을 받아 속도/가속 한계, 지오펜스, 최소 장애물 거리(TTC 기반), 입력 하트비트·데이터 신선도를 검사해 통과/제한/비상정지 명령으로 바꾸는 safety monitor를 구현하고, 버그 있는 플래너를 주입해 차단되는지 검증한다. | mac |
| AUTON-A01 | 포즈 그래프 SLAM: Gauss-Newton 최적화 | 2D 포즈 그래프(노드=자세, 엣지=상대 자세 + 정보 행렬) 최적화기를 Gauss-Newton + 희소 선형 풀이(Eigen)로 구현하고, 루프 클로저 추가 전후 궤적 오차와 잘못된 루프 클로저에 대한 robust kernel(Huber) 효과를 보인다. | mac |
| AUTON-A02 | OOSM 처리: 지연 측정 버퍼-리플레이 융합 | 상태·공분산 히스토리 버퍼를 둔 EKF에서 지연된 측정이 오면 해당 시점으로 되돌아가 갱신 후 재전파하는 buffer-and-replay를 구현하고, 단순 무시/즉시 적용 방식과 추정 오차·계산량을 비교한다. | mac |
| AUTON-A03 | Frenet 좌표계 궤적 계획 | 기준선의 Frenet 변환을 구현하고 횡방향 5차·종방향 4차 다항식 궤적 집합을 생성해 저크·편차·속도 비용과 곡률·가속 제약, M09 충돌 검사로 최적 궤적을 고르는 계획기를 작성한다. | mac |
| AUTON-A04 | 런타임 검증: 시간 논리 모니터 | bounded-response, always/eventually-within 연산자를 지원하는 스트리밍 모니터를 작성해 시뮬레이션 로그 위에서 위반 시점과 robustness 값을 출력하고, M10 safety monitor 규칙과 런타임 검증 규칙의 역할 차이를 정리한다. | mac |
| AUTON-A05 | 시뮬레이션 시나리오 테스트 하네스 | 헤드리스 2D 시뮬레이터(로봇 + 동적 장애물)에 플래너·안전 모니터를 연결하고, 시나리오 파라미터(속도, 진입 각, 센서 노이즈)를 스윕/무작위 생성해 충돌·최소거리·목표 도달 지표를 집계하고 실패 시나리오를 재현 가능한 시드로 저장하는 하네스를 작성한다. | mac |
| AUTON-A06 | 성능 저하 모드와 최소 위험 상태(MRC) | 센서별 health(신선도·일관성)와 ODD 조건을 입력으로 능력 레벨(Full/Reduced speed/Stop-in-lane/Pull-over)을 결정하는 degradation manager를 구현하고, 고장 조합 테이블 기반 테스트로 항상 안전한 방향으로만 전이하는지 검증한다. | mac |
| AUTON-A07 | 엣지 퍼셉션 파이프라인 실시간성 설계 | 카메라 프레임 → 전처리 → (가짜) 추론 → 추적 파이프라인을 bounded 큐 + drop-oldest 정책으로 구현하고, 추론 시간이 변동할 때 지연 백분위(p50/p99)와 처리량을 측정해 큐 깊이·드롭 정책·스레드 우선순위 선택 근거를 정리한다. | mac |
| AUTON-A08 | 다중 로봇 협조: 작업 할당과 충돌 회피 | N대 로봇과 M개 목표에 대해 sequential single-item auction 작업 할당을 구현하고, 시공간 예약 테이블을 쓰는 우선순위 기반 다중 경로 계획(cooperative A*)으로 충돌 없이 실행됨을 시뮬레이션으로 검증하며, 통신 두절 시 처리 방식을 논의한다. | mac |

### BMS — 배터리 관리 시스템 (Battery Management Systems)

BMS 펌웨어는 수백 개 셀의 전압·전류·온도를 정밀하게 측정하고, SoC/SoH/SoP를 추정하며, 보호·밸런싱·컨택터·충전 제어를 기능안전 수준으로 수행한다. Tesla·Rivian·Lucid·Joby·에너지 저장 회사 면접에서는 셀 모니터 IC 통신(isoSPI/PEC), 쿨롱 카운팅과 EKF의 한계, 프리차지 시퀀스, 보호 디바운스를 구체적인 코드와 숫자로 설명할 수 있어야 하므로, 셀 모델 시뮬레이터 위에서 전부 직접 만들어 본다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| BMS-B01 | 셀 기초: OCV-SoC 곡선과 화학별 차이 | NMC·LFP OCV-SoC 테이블을 선형 보간하는 OCV(SoC)·SoC(OCV) 함수를 C로 구현하고(단조성 검사, 경계 클램프), LFP 평탄 구간에서 10 mV 전압 오차가 몇 % SoC 오차가 되는지 계산해 비교한다. | mac |
| BMS-B02 | 등가회로 셀 모델 (Rint / 1-RC Thevenin) | OCV(SoC) + R0 + R1‖C1 1-RC 모델을 이산화한 셀 시뮬레이터를 작성하고, 펄스 방전(HPPC 유사) 프로파일에서 순간 강하와 완화(relaxation) 곡선을 출력해 R0/R1/τ가 파형의 어디에 나타나는지 확인한다. 이후 모든 항목의 테스트 플랜트로 재사용한다. | mac |
| BMS-B03 | 셀 전압 측정 체인과 보정 | 16비트 ADC 코드 → 셀 전압 변환, 2점 게인/오프셋 보정, 이동평균/IIR 필터를 구현하고, 오차 예산(ADC LSB, 기준전압 온도계수, 필터 지연)을 계산해 ±2 mV 목표를 만족하는지 시뮬레이션으로 검증한다. | mac |
| BMS-B04 | 전류 측정: 션트·홀 센서와 오프셋 보정 | 션트 저항 + 증폭기 체인의 코드→암페어 변환, 컨택터 개방(전류 0) 시점의 자동 영점 보정, 두 레인지(저전류 정밀/고전류) 센서 선택 로직을 구현하고 10 mA 오프셋이 24시간 동안 만드는 Ah 오차를 계산한다. | mac |
| BMS-B05 | 온도 측정: NTC 변환과 센서 고장 검출 | 분압 회로 ADC 값을 NTC 저항 → 온도로 바꾸는 Beta 식과 Steinhart-Hart 식(룩업 테이블 버전 포함)을 구현하고, 범위를 벗어나는 개방/단락 판정과 이웃 센서와의 타당성 검사를 추가해 테스트한다. | mac |
| BMS-B06 | 보호 기능: OV/UV/OC/OT 임계값·히스테리시스·디바운스 | 셀 OV/UV, 팩 충·방전 OC(I²t 또는 다단 시간-전류 곡선), OT/UT를 경고·제한·차단 3단계로 판정하는 보호 모듈을 구현하고(히스테리시스·디바운스 포함), 노이즈 스파이크와 실제 고장 프로파일로 응답 시간을 검증한다. | mac |
| BMS-B07 | 쿨롱 카운팅 SoC 추정 | 쿨롱 효율·공칭 용량·샘플 주기를 반영한 쿨롱 카운팅 SoC를 구현하고, B02 셀 모델 주행 사이클에 전류 센서 오프셋·게인 오차를 넣어 SoC 드리프트를 측정한 뒤, 충분한 휴지 후 OCV로 재보정하는 로직을 추가한다. | mac |
| BMS-B08 | 셀 모니터 IC 드라이버: 명령·PEC15 | PEC15 CRC(다항식 0x4599, 초기값 16)를 구현하고, mock SPI 위의 셀 모니터 IC 모델(ADCV 변환 시작, RDCVA..D 그룹 읽기, 설정 레지스터 쓰기·읽어 검증)과 드라이버를 작성해 PEC 오류·변환 미완료 상황을 테스트한다. | mac |
| BMS-B09 | 수동 셀 밸런싱 알고리즘 | 최저 셀 대비 ΔV 임계값·최소 전압 조건으로 방전 셀을 선택하고, 측정 시점에는 밸런싱을 멈추는 듀티 스케줄과 IC 온도 기반 동시 밸런싱 수 제한을 구현해, 불균형 팩 시뮬레이션에서 수렴 시간을 측정한다. | mac |
| BMS-M01 | isoSPI 데이지체인 통신 | N개 IC 체인 모델(각 IC가 데이터를 shift, 마지막 IC 데이터가 먼저 도착)을 만들고 드라이버에서 IC별 데이터 재정렬, PEC 실패 시 재시도/카운터, 체인 중간 단선 시 위치 진단, 양방향(ring) 체인 폴백을 구현한다. | mac |
| BMS-M02 | 프리차지·컨택터 제어 상태 머신 | DC 링크 커패시터 RC 모델과 함께 Open→Precharge→Close Main→Open Precharge→Closed 상태 머신(전압 95% 도달 조건, 타임아웃, 재시도 제한)을 구현하고, 컨택터 용착(welding)·프리차지 저항 과열·부하 단락 시나리오를 검증한다. | mac |
| BMS-M03 | 절연 저항 모니터링 (IMD) | HV+/HV− 각각에 기준 저항을 번갈아 연결해 측정한 전압 두 쌍으로 절연 저항을 계산하는 식을 유도·구현하고, 노이즈·Y-커패시턴스 정착 시간을 고려한 측정 타이밍과 500 Ω/V 기준 경고 판정을 테스트한다. | mac |
| BMS-M04 | EKF 기반 SoC 추정 (1-RC 모델) | 상태 [SoC, V_RC] EKF(OCV 기울기 dOCV/dSoC 야코비안)를 구현하고, 초기 SoC 30% 오차·전류 오프셋 조건에서 쿨롱 카운팅 대비 수렴과 RMSE를 비교하며 LFP 평탄 구간에서 관측성이 떨어지는 현상을 보인다. | mac |
| BMS-M05 | SoH 추정: 용량 페이드와 저항 증가 | 두 휴지 OCV 사이 적산 Ah / ΔSoC로 용량을 추정하고, 전류 스텝의 ΔV/ΔI로 R0를 추정하는 알고리즘을 구현해 합성 노화 데이터(용량 −20%, R0 +50%)에서 추정 정확도와 신뢰 조건(ΔSoC 최소폭 등)을 평가한다. | mac |
| BMS-M06 | SoP: 순간 가용 전력과 디레이팅 | 셀 전압 한계(Vmin/Vmax), R0·RC 상태를 이용해 10초·30초 최대 방전/충전 전류를 예측하는 SoP 함수와 온도·SoC 디레이팅 테이블을 구현하고, 셀 모델 시뮬레이션에서 해당 전류를 실제 인가했을 때 한계를 넘지 않는지 검증한다. | mac |
| BMS-M07 | CC/CV 충전 알고리즘과 JEITA 온도 제어 | 셀 모델을 플랜트로 CC/CV 충전 제어 루프(CV 단계 전압 PI 제어, C/20 종료, 타임아웃)와 JEITA 스타일 온도 구간별 전류/전압 제한, 가장 높은 셀 기준 제어를 구현해 불균형 팩 충전 시 개별 셀 과전압이 없는지 확인한다. | mac |
| BMS-M08 | BMS CAN 보고: 멀티플렉스 신호 설계 | 팩 상태·한계 전력·고장 비트·셀 전압/온도(멀티플렉스 인덱스)를 담는 DBC를 설계하고 pack/unpack 코드와 송신 스케줄을 구현해 버스 부하를 계산하며, 충전기/VCU 쪽 수신 측 타임아웃 처리도 구현한다. | mac |
| BMS-M09 | 팩 열 관리: 집중 열 모델과 냉각 제어 | 셀 그룹별 집중 열용량 모델(발열, 냉각판 전도, 이웃 셀 전달)을 시뮬레이션하고 냉각수 펌프/칠러 PI 제어, 저온 충전 전 히터 예열 로직을 구현해 고부하 주행·급속충전 시 최대 온도와 셀 간 온도 편차를 측정한다. | mac |
| BMS-M10 | 능동 셀 밸런싱 시뮬레이션 | 셀 간 전달 효율·전류 한계를 가진 능동 밸런싱 모델에 '최고 셀→최저 셀' 그리디와 SoC 기반 목표 밸런싱 알고리즘을 구현하고, B09 수동 밸런싱 대비 수렴 시간·손실 에너지·가용 용량 증가를 비교한다. | mac |
| BMS-A01 | BMS 기능안전: 안전 목표와 독립 2차 보호 | 과충전 위해에 대한 안전 목표·FTTI를 정의하고, 주 MCU 보호와 독립된 2차 OV 경로(별도 비교기/AFE 하드와이어 출력 모델)와 교차 감시를 구현한 뒤, 한 채널 고장 주입 시 FTTI 안에 안전 상태(컨택터 개방)로 가는지 검증한다. | mac |
| BMS-A02 | 열폭주 조기 감지: 다중 지표 융합 | 셀 전압 급락(dV/dt), 온도 상승률(dT/dt), 가스·압력 센서 신호를 persistence 조건과 투표 규칙으로 결합하는 감지기를 구현하고, 정상 급속충전·센서 고장·실제 전조 합성 프로파일에서 검출 시간과 오탐률을 측정한다. | mac |
| BMS-A03 | 내부 단락·자기방전 이상 셀 검출 | 휴지 구간 셀별 OCV 감소율과 누적 밸런싱 방전량을 추적해 팩 통계 대비 이상치(z-score, 이동 윈도우)를 찾는 알고리즘을 구현하고, 미세 누설 셀을 주입한 장기 시뮬레이션에서 검출까지 걸린 시간을 측정한다. | mac |
| BMS-A04 | 분산 BMS 아키텍처와 V/I 동기 샘플링 | 셀 모니터 IC 변환 시점과 전류 샘플 시점이 수 ms 어긋난 경우를 시뮬레이션해 R0/SoC/SoP 추정 오차를 정량화하고, 동기 트리거·타임스탬프 보간으로 보정하는 방식을 구현하며 중앙/분산/무선 BMS 구조 트레이드오프를 정리한다. | mac |
| BMS-A05 | Dual EKF: SoC와 모델 파라미터 동시 추정 | 상태 필터(SoC, V_RC)와 파라미터 필터(R0, Q)를 번갈아 갱신하는 dual EKF를 구현하고, 노화 셀 데이터에서 고정 파라미터 EKF 대비 SoC RMSE와 파라미터 수렴, 과도한 파라미터 노이즈의 부작용을 실험한다. | mac |
| BMS-A06 | NVM 데이터 관리: SoC/이력 저장과 전원 차단 내성 | mock 플래시/EEPROM 위에 이중 복사 + 시퀀스 번호 + CRC 레코드 저장, 셧다운 시 SoC 저장 경로, 섹터 순환 마모 분산을 구현하고 쓰기 중 무작위 전원 차단을 수천 번 주입해 항상 최신 유효 레코드를 복구하는지 검증한다. | mac |
| BMS-A07 | BMS HIL 스타일 테스트: 셀 에뮬레이터 + 결함 주입 | 앞에서 만든 셀/열/AFE/전류 센서 모델을 묶은 팩 에뮬레이터와 BMS 펌웨어를 HAL 경계로 연결하고, 주행·급속충전·센서 단선·AFE 통신 끊김·컨택터 용착 시나리오를 YAML로 정의해 자동 실행·판정하는 하네스를 작성한다. | mac |
| BMS-A08 | 시스템 설계: 800 V EV 팩 BMS | 800 V·200셀 직렬 팩을 가정해 AFE 배치·통신 토폴로지·MCU 이중화·컨택터/프리차지/퓨즈·IMD·열관리·CAN 인터페이스·OTA·안전 목표를 포함한 설계 문서를 작성하고, 측정 주기·보호 응답 시간·SoC 정확도 예산을 숫자로 정리한다. | mac |

### BMC — 서버 관리 컨트롤러 (Server Management Controller (BMC))

BMC(Baseboard Management Controller)는 ASPEED AST2600 같은 SoC 위에서 OpenBMC(Linux + D-Bus 데몬)를 돌리며 서버의 전원·센서·팬·로그·펌웨어 업데이트·원격 콘솔을 호스트 CPU와 독립적으로 관리한다. Google·Meta·Microsoft·NVIDIA·AMD의 플랫폼/데이터센터 하드웨어 팀 면접에서는 IPMI/Redfish/PLDM/MCTP 메시지, 팬 PID, 전원 시퀀싱, 서명 이미지·RoT를 코드와 설계로 설명할 수 있는지를 보며, Don의 SSD·PCIe 경험과도 직접 연결된다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| BMC-B01 | BMC의 역할과 OpenBMC를 QEMU에서 부팅하기 | QEMU의 ast2600-evb(또는 romulus-bmc) 머신으로 공개 OpenBMC 이미지를 부팅해 systemctl·busctl tree·journalctl로 주요 서비스(phosphor-*, bmcweb, ipmid)를 조사하고, 호스트 CPU와 독립 동작하는 BMC 블록 다이어그램과 서비스 지도를 작성한다. | **qemu** |
| BMC-B02 | PMBus/SMBus 센서 읽기와 LINEAR11/16 변환 | PMBus LINEAR11·LINEAR16(VOUT_MODE 지수) 인코딩/디코딩과 SMBus PEC(CRC-8, poly 0x07)를 구현하고, mock VR 장치(READ_VIN/READ_IOUT/READ_TEMPERATURE_1, STATUS_WORD)를 상대로 read word 트랜잭션·PEC 오류·NACK 처리를 테스트한다. | mac |
| BMC-B03 | IPMI 메시지 구조와 IPMB 체크섬 | IPMB 요청/응답 프레임(rsSA, NetFn/rsLUN, 체크섬1, rqSA, rqSeq/rqLUN, Cmd, data, 체크섬2)의 인코더/디코더와 Get Device ID·Get Self Test Results 응답 핸들러를 작성하고, 체크섬 오류·알 수 없는 명령(C1h) 완료 코드를 테스트한다. | mac |
| BMC-B04 | SDR 파싱과 센서 값 변환 (M, B, R-exp, B-exp) | Type 01h Full Sensor Record 바이너리를 파싱(10비트 부호 M/B, 4비트 부호 지수, 센서 단위, 임계값 필드)하고 raw↔실제 값 변환 함수를 작성해, ipmitool sdr 출력 형식과 같은 표를 만들고 경계값 왕복 오차를 검증한다. | mac |
| BMC-B05 | 센서 임계값 엔진: 어서트/디어서트와 히스테리시스 | 센서 값 스트림을 받아 상·하한 각 3단계 임계 상태를 추적하고 양/음 히스테리시스를 적용해 어서트·디어서트 이벤트를 생성하는 엔진을 구현하고, 경계 부근 진동 입력에서 이벤트 폭주가 없음을 테스트한다. | mac |
| BMC-B06 | SEL(System Event Log) 레코드와 로그 관리 | B05 이벤트를 16바이트 SEL 레코드(Record ID, 타입 02h, timestamp, Generator ID, EvM Rev, sensor type/number, event dir/type, event data 1–3)로 저장하는 로그 모듈과 Get SEL Entry·Reserve SEL·Clear SEL 처리를 구현해 가득참/순환 정책을 테스트한다. | mac |
| BMC-B07 | 팬 기초: PWM 듀티·타코 RPM·스텝 팬 커브 | 타코 펄스 카운트(팬당 2펄스/회전)→RPM 변환, mock PWM 레지스터 듀티 설정, 히스테리시스를 둔 온도→듀티 스텝 커브와 팬 고장(RPM 0, 응답 없음) 감지 시 나머지 팬 100% 정책을 구현해 테스트한다. | mac |
| BMC-B08 | 섀시 전원 제어 상태 머신 | Off/PoweringOn/On/GracefulOff/PoweringOff 상태와 PGOOD 입력, 전원 버튼 GPIO 펄스, 강제 종료(4초 홀드) 타이머, IPMI Chassis Control 명령을 처리하는 상태 머신을 구현하고 동시 이벤트(버튼 + 원격 명령) 테스트를 작성한다. | mac |
| BMC-B09 | KCS 호스트 인터페이스 상태 머신 | mock KCS 레지스터 모델 위에 호스트 측 드라이버와 BMC 측 상태 머신(IDLE/READ/WRITE/ERROR, IBF/OBF/C/D 비트, WRITE_START/WRITE_END/READ 제어 코드)을 구현하고, 정상 트랜잭션과 중단(abort)·오류 복구 시나리오를 테스트한다. | mac |
| BMC-M01 | Redfish 기초: 미니 Redfish 서비스 구현 | 앞의 센서·전원 모델을 노출하는 Python Redfish 서버(@odata.id 링크, GET 컬렉션, PATCH 팬 모드, ComputerSystem.Reset 액션, ETag, 세션 인증)를 작성하고 curl과 DMTF Redfish-Service-Validator로 스키마 적합성을 확인한다. | mac |
| BMC-M02 | 전원 시퀀싱: 레일 순서·PGOOD·역순 셧다운 | 의존 관계(예: P12V→P5V→P3V3→VCCIN→DDR)를 가진 레일 테이블을 읽어 mock GPIO로 enable하고 PGOOD 대기·타임아웃·지연을 처리하는 시퀀서를 구현하며, 중간 레일 실패 시 역순 셧다운과 실패 레일 SEL 기록을 테스트한다. | mac |
| BMC-M03 | PID 팬 제어 (phosphor-pid-control 스타일) | 서버 열 모델(CPU/DIMM/인렛 발열, 팬 RPM에 따른 냉각) 시뮬레이터 위에 존별 thermal PID(마진 기반, 최대값 선택)와 팬 RPM PID, 센서 누락 시 failsafe 듀티를 구현해 부하 스텝 시 온도 오버슈트·팬 소음(RPM 변동)을 측정한다. | mac |
| BMC-M04 | OpenBMC D-Bus 아키텍처: 센서 객체와 ObjectMapper | Linux VM에서 xyz.openbmc_project.Sensor.Value 인터페이스(Value/Unit 프로퍼티, PropertiesChanged 신호)를 내보내는 Python(dbus-next) 서비스와, 그것을 찾아 구독하는 클라이언트를 작성하고 busctl introspect/monitor로 확인하며 ObjectMapper 역할을 미니 구현한다. | **linux** |
| BMC-M05 | MCTP 패킷: 분할·재조립과 EID | MCTP 패킷 인코더와 메시지 분할(기본 64바이트 MTU)·재조립기(시퀀스 오류, 중복, 태그별 동시 메시지)를 구현하고, MCTP-over-SMBus 바인딩 헤더(목적지 주소, 명령 0x0F, PEC)를 씌워 왕복 테스트하며 Control 메시지(Get Endpoint ID 등)를 처리한다. | mac |
| BMC-M06 | PLDM Type 2: 플랫폼 모니터링 (PDR, GetSensorReading) | PLDM 헤더(Rq/D/Instance ID, Type, Command, Completion Code)와 Type 2 GetPDR·GetSensorReading 요청자/응답자를 M05 MCTP 위에서 구현하고, 가상 NIC/GPU 장치의 Numeric Sensor PDR을 읽어 센서 값을 BMC 센서 모델로 가져온다. | mac |
| BMC-M07 | IPMI over LAN (RMCP+)과 인증 세션 | OpenIPMI ipmi_sim을 띄우고 ipmitool -I lanplus 트래픽을 캡처해 RMCP/ASF Presence Ping, Open Session, RAKP 1–4 메시지 필드를 해석하는 파서를 Python으로 작성하며, cipher suite 0의 위험성과 RAKP 해시 노출 문제를 정리한다. | **linux** |
| BMC-M08 | 호스트 콘솔과 Serial-over-LAN | PTY를 호스트 UART로 가정해 콘솔 서버(obmc-console 유사)를 작성하고 Unix 소켓 다중 클라이언트 팬아웃, 쓰기 권한 1명 제한, 순환 콘솔 로그 버퍼, 접속 시 최근 로그 재생을 구현해 동시 접속 테스트를 수행한다. | mac |
| BMC-M09 | BMC 워치독과 호스트 복구 | Set/Get/Reset Watchdog Timer 명령(timer use, pre-timeout 인터럽트, timeout action: reset/power down/cycle)을 구현하고, BIOS POST 정지·OS 무응답 시나리오에서 재시도 횟수·대체 BIOS 이미지 부팅으로 에스컬레이션하는 복구 정책을 테스트한다. | mac |
| BMC-M10 | 서버 부품 펌웨어 업데이트 관리자 | 업로드 이미지 헤더 파싱·대상 판별·검증→스테이징→적용→활성화 상태 머신을 구현하고, BMC A/B 슬롯 전환, 호스트 off 상태에서 SPI 먹스 전환 후 BIOS 플래시 쓰기(mock)를 처리하며, Redfish UpdateService(multipart push)로 진행 상태를 노출한다. | mac |
| BMC-A01 | 서명 이미지 검증과 BMC Secure Boot 체인 | ROM(OTP에 공개키 해시) → 부트로더 → 커널 이미지로 이어지는 ECDSA/RSA 서명 검증 체인을 OpenSSL로 시뮬레이션하고, 보안 버전 카운터(OTP 퓨즈 모델) 기반 롤백 방지와 키 폐기 시나리오를 테스트한다. | mac |
| BMC-A02 | Root of Trust와 SPDM 측정·인증 | SPDM 핵심 흐름(GET_VERSION → GET_CAPABILITIES → NEGOTIATE_ALGORITHMS → GET_CERTIFICATE → CHALLENGE → GET_MEASUREMENTS)을 단순화한 요청자/응답자를 구현해 인증서 체인 검증과 서명된 측정값 비교를 수행하고, 측정 불일치 장치를 격리하는 정책을 설계한다. | mac |
| BMC-A03 | PLDM Type 5 펌웨어 업데이트 흐름 | PLDM FW Update 패키지 헤더 파싱과 RequestUpdate → PassComponentTable → UpdateComponent → RequestFirmwareData(FD 주도 pull) → TransferComplete → VerifyComplete → ApplyComplete → ActivateFirmware 흐름을 UA·FD 양쪽 상태 머신으로 구현하고 전송 중단·검증 실패·취소를 테스트한다. | mac |
| BMC-A04 | eSPI/LPC 호스트 연동: POST 코드와 부팅 진행 추적 | POST 코드 스누프 FIFO 모델과 링버퍼 히스토리, 코드→부팅 단계(SEC/PEI/DXE/BDS) 디코더, 일정 시간 같은 코드에 정체 시 hang 판정을 구현하고, M09 워치독 복구와 연동해 부팅 실패 원인 리포트를 생성한다. | mac |
| BMC-A05 | Redfish EventService와 텔레메트리 | M01 서버에 EventService 구독(POST /Subscriptions, 이벤트 필터)과 SSE 스트림, TelemetryService MetricReportDefinition(주기 집계 min/max/avg)을 추가하고, 구독자 다운·재시도·백프레셔 상황에서 메모리 상한을 지키는지 테스트한다. | mac |
| BMC-A06 | OpenBMC 스타일 센서 데몬 개발 (sdbusplus) | Linux VM에서 sdbusplus + boost::asio로 hwmon(또는 mock 파일) 값을 주기적으로 읽어 xyz.openbmc_project.Sensor.Value·Threshold 인터페이스로 게시하는 데몬을 작성하고 busctl로 검증한 뒤, bmcweb이 이를 Redfish Sensors로 노출하는 매핑을 코드 리딩으로 정리한다. | **linux** |
| BMC-A07 | BMC 신뢰성: 자기 복구와 호스트 무중단 | 골든 이미지 폴백 부트 셀렉터, systemd 스타일 데몬 재시작 정책(백오프, 크래시 루프 감지), BMC 재부팅 중에도 팬을 안전 듀티로 유지하는 하드웨어 기본값 설계, 상태 영속화를 구현/정리하고 결함 주입 테스트로 검증한다. | mac |
| BMC-A08 | 시스템 설계: 데이터센터 플릿 BMC 관리 | 10만 대 서버 대상 Redfish/이벤트 기반 텔레메트리 수집 파이프라인, 단계적(canary→wave) 펌웨어 롤아웃과 자동 롤백, 자격증명·인증서 회전, 대역외 네트워크 분리를 포함한 설계 문서를 작성하고 롤아웃 컨트롤러 핵심 로직을 코드로 구현한다. | mac |

## 9단계 · C++ 실전 도메인 (Applied C++ Domains)

### MCPP — 모던 C++ 심화 (Advanced Modern C++)

임베디드 C++(CPP 트랙)이 '펌웨어에서 안전하게 쓰는 C++'라면, 이 트랙은 언어 자체의 깊은 메커니즘(값 범주, 템플릿, 타입 소거, 코루틴, 메모리 모델, ABI)을 직접 구현하며 익힌다. NVIDIA·Apple·Tesla·HFT 등 C++ 비중이 큰 면접에서 'unique_ptr/std::function을 구현해 보라', 'move가 왜 안 일어나나' 같은 질문에 코드로 답할 수 있게 하는 것이 목표다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| MCPP-B01 | 값 범주: lvalue / prvalue / xvalue 판별 | decltype((expr)) 기반 value_category<T> 트레이트를 작성하고, 20개 식(변수, std::move, 함수 반환, 멤버 접근, 삼항 연산 등)의 범주를 static_assert로 검증한다. | mac |
| MCPP-B02 | 특수 멤버 함수 생성 규칙 & Rule of 0/3/5 | 호출을 기록하는 Tracer 클래스로 6가지 선언 조합(소멸자만, 복사만, =default 이동 등)에서 copy/move가 어떻게 선택되는지 표로 만들고 테스트로 고정한다. | mac |
| MCPP-B03 | Move semantics 구현: 소유 버퍼 클래스 | heap 배열을 소유하는 Buffer 클래스의 copy/move 생성·대입을 구현하고, move에 noexcept 유무에 따라 std::vector 재할당 시 copy vs move가 선택되는지 카운터로 검증한다. | mac |
| MCPP-B04 | Copy elision: RVO / NRVO / 보장된 생략 | Tracer로 prvalue 반환, 분기 반환, return std::move(local), 매개변수 반환 케이스를 측정하고 -fno-elide-constructors 빌드와 비교해 결과 표를 작성한다. | mac |
| MCPP-B05 | 참조 축약 & forwarding reference | type_name<T>() 헬퍼로 lvalue/rvalue/const 인자별 T 추론 결과를 출력하고, 참조 축약 규칙과 auto&& 동작을 static_assert 세트로 증명한다. | mac |
| MCPP-B06 | Perfect forwarding: make_unique / emplace 구현 | my_make_unique<T>(Args&&...)와 단순 vector의 emplace_back을 구현하고, forward를 빼먹었을 때 추가 copy가 생기는 것을 Tracer 테스트로 보인다. | mac |
| MCPP-B07 | 템플릿 인자 추론 & 오버로드 해석 | 오버로드 해석 퀴즈 15개를 코드로 작성해 예상 출력과 실제 출력을 비교하고, 틀린 문항마다 표준 규칙 근거를 주석으로 단다. | mac |
| MCPP-B08 | 클래스 템플릿 & 조건부 trivial: optional<T> 구현 | union 저장소 기반 my::optional<T>를 구현하고 T가 trivially copyable이면 optional도 그렇게 되도록 requires 절로 특수 멤버를 조건부 =default 처리, optional<T&> 부분 특수화·CTAD guide를 static_assert와 수명 테스트로 검증한다. | mac |
| MCPP-B09 | Variadic templates & fold expression | 타입 안전 print(fmt, args...)와 sum/all_of/any_of fold를 구현하고, 포맷 자리수와 인자 개수가 다르면 컴파일 에러가 나도록 만든다. | mac |
| MCPP-B10 | constexpr / consteval 기초: 컴파일 타임 문자열 해시 | consteval FNV-1a 해시로 문자열 switch(명령어 디스패처)를 구현하고, 런타임 호출 시 컴파일 에러, constinit 전역 초기화 순서 문제 해결을 테스트로 보인다. | mac |
| MCPP-B11 | 예외 안전성 보장 & copy-and-swap | N번째 할당에서 throw하는 테스트 allocator로 컨테이너 대입 연산 중 예외를 주입하고, copy-and-swap 적용 전후로 객체 상태가 보존되는지 검증한다. | mac |
| MCPP-B12 | 람다 내부: 클로저 타입과 캡처 수명 | 람다 5개를 손으로 쓴 functor로 바꿔 동작을 비교하고, 참조 캡처/this 캡처로 dangling을 만든 뒤 ASan으로 탐지, init-capture로 수정한다. | mac |
| MCPP-B13 | string_view / span 수명 함정 | 임시 string에서 만든 string_view, 재할당된 vector의 span 등 6개 dangling 사례를 재현해 ASan으로 잡고, -Wdangling 경고와 API 설계 규칙을 정리한다. | mac |
| MCPP-M01 | unique_ptr 구현: custom deleter & EBO | my::unique_ptr<T, D>를 [[no_unique_address]]로 구현해 기본 deleter일 때 sizeof==sizeof(T*)를 static_assert로 확인하고, 배열 특수화·release/reset·FILE* deleter를 테스트한다. | mac |
| MCPP-M02 | shared_ptr / weak_ptr 구현: control block | 원자적 strong/weak 카운트를 갖는 my::shared_ptr·weak_ptr·make_shared를 구현하고, 순환 참조 누수를 weak_ptr로 해결, 멀티스레드 복사 테스트를 TSan으로 돌린다. | mac |
| MCPP-M03 | shared_ptr 고급: aliasing·shared_from_this | 구현한 shared_ptr에 aliasing 생성자와 enable_shared_from_this를 추가하고, 비동기 콜백에서 this 수명 연장 패턴을 구현해 수명 테스트로 검증한다. | mac |
| MCPP-M04 | type traits 직접 구현 & detection idiom | is_same, remove_cvref, conditional, is_convertible, is_detected(void_t)를 직접 구현해 std 버전과 static_assert로 비교하고, '멤버 함수 reset()이 있는가' 감지기를 만든다. | mac |
| MCPP-M05 | SFINAE → Concepts 마이그레이션 | enable_if로 작성된 직렬화 오버로드 세트(정수/부동소수/컨테이너/사용자 타입)를 concepts로 재작성하고, 에러 메시지 길이와 오버로드 선택(subsumption)을 비교 기록한다. | mac |
| MCPP-M06 | 자체 컨테이너 iterator & ranges 호환성 | 링 형태 컨테이너에 random_access_iterator를 구현하고 std::random_access_iterator / std::ranges::range concept을 static_assert로 통과시킨 뒤 ranges::sort·views와 함께 동작시킨다. | mac |
| MCPP-M07 | Type erasure 1: std::function 구현 (SBO) | my::function<R(Args...)>를 수동 vtable + SBO로 구현하고, operator new 카운터로 작은 람다는 할당 0회임을 검증, move_only_function 변형도 만든다. | mac |
| MCPP-M08 | Type erasure 2: 외부 다형성 (Sean Parent 스타일) | Drawable 개념을 만족하는 서로 무관한 타입들을 담는 값 타입 컬렉션을 구현하고, 가상 상속 버전과 메모리 사용량·순회 성능을 벤치마크로 비교한다. | mac |
| MCPP-M09 | std::variant & visit로 상태 머신 | overloaded{} 패턴으로 통신 프로토콜 파서 상태 머신(Idle/Header/Payload/Crc)을 구현하고, valueless_by_exception 상황을 재현해 처리 방법을 테스트한다. | mac |
| MCPP-M10 | 예외의 내부 비용 vs std::expected monadic 체인 | 같은 설정 파일 파서를 예외 버전과 C++23 std::expected(and_then/transform/or_else) 버전으로 작성해 정상/에러 경로 지연, 바이너리 크기(__eh_frame/__unwind_info)를 측정하고, lldb로 __cxa_throw→unwind 경로를 추적해 정리한다. | mac |
| MCPP-M11 | Allocator & PMR: 할당 전략 교체 | 로깅 memory_resource와 고정 arena resource를 직접 작성해 pmr::vector/pmr::string에 연결하고, monotonic/pool resource 대비 할당 횟수·속도를 측정, hot loop 할당 0회를 검증한다. | mac |
| MCPP-M12 | Ranges & views: 커스텀 view adaptor | stride/chunk 유사 커스텀 view와 파이프(|) adaptor를 구현하고, 원소 접근 횟수를 세어 lazy 평가를 증명, dangling iterator(borrowed_range) 규칙을 테스트한다. | mac |
| MCPP-M13 | 메모리 모델 규칙: fence·release sequence | fence 기반 vs 연산 기반 동기화, RMW가 이어 주는 release sequence, atomic_ref 사용 예를 작성하고, 작은 SC 인터리빙 전수 탐색기를 만들어 data race가 있는 버전을 찾아낸 뒤 TSan 결과와 대조한다(실측 litmus는 OS 트랙). | mac |
| MCPP-A01 | Coroutines 1: generator<T> 직접 구현 | generator<T>를 처음부터 구현하고, operator new 오버로드로 코루틴 프레임 할당을 관찰하며, 예외 전파와 조기 파괴 시 소멸 순서를 테스트한다. | mac |
| MCPP-A02 | Coroutines 2: Task<T> & 단일 스레드 이벤트 루프 | Task<T>, sleep_for 타이머 awaitable, 단일 스레드 이벤트 루프를 구현해 센서 폴링 시나리오를 작성하고, 10만 단계 co_await 체인에서 스택 오버플로가 없음을 symmetric transfer로 보인다. | mac |
| MCPP-A03 | atomic wait/notify로 동기화 프리미티브 구현 | counting_semaphore, latch, 단순 mutex를 std::atomic::wait/notify로 구현하고 스트레스 테스트(TSan)와 std 구현 대비 지연 시간을 비교한다. | mac |
| MCPP-A04 | Undefined Behavior 사냥 | signed overflow, strict aliasing, 객체 수명 밖 접근, 미초기화, OOB, data race 등 10개 UB를 재현해 -O0/-O2 결과 차이를 기록하고 UBSan/ASan/TSan으로 탐지, std::bit_cast 등 올바른 대안으로 고친다. | mac |
| MCPP-A05 | ODR 위반·이름 맹글링·객체 레이아웃 | 두 TU에 서로 다른 inline 함수 정의로 ODR 위반을 만들어 링크 결과가 최적화 수준에 따라 바뀌는 것을 관찰하고, nm/c++filt로 맹글링 해독, -Xclang -fdump-record-layouts로 vtable·패딩을 확인한다. | mac |
| MCPP-A06 | ABI 안정 라이브러리: pImpl & C 인터페이스 | dylib v1→v2에서 멤버 추가로 ABI가 깨지는 데모를 만들고, pImpl·opaque handle C API·inline namespace 버저닝으로 재설계해 구 클라이언트 바이너리가 계속 동작함을 검증한다. | mac |
| MCPP-A07 | 템플릿 메타프로그래밍: typelist & 컴파일 타임 검증 | typelist(find/unique/transform/index_of)를 구현하고, consteval 포맷 문자열 검사기(인자 타입·개수 불일치를 컴파일 에러로)를 만들어 std::format 방식과 비교한다. | mac |
| MCPP-A08 | Expression templates: 임시 객체 없는 벡터 연산 | Vec 클래스에 a + b * c 형태의 expression template을 구현해 임시 객체 0개·단일 루프로 평가됨을 보이고, naive 구현 대비 성능과 어셈블리(-S)를 비교한다. | mac |
| MCPP-A09 | 빌드 시간 최적화: -ftime-trace & 인스턴스화 비용 | 중형 헤더 위주 프로젝트를 -ftime-trace(Perfetto/chrome://tracing)로 분석해 상위 병목 헤더·인스턴스화를 찾고, extern template·전방 선언·PCH·concepts 기반 제약 단순화로 전체/증분 빌드 시간을 줄여 전후 수치를 보고한다(코드 크기 bloat는 CPP 트랙). | mac |
| MCPP-A10 | C++20 Modules 실습 | 작은 라이브러리를 export module로 변환해 clang + CMake(3.28+)로 빌드하고, 헤더 버전 대비 재빌드 시간과 매크로/ODR 격리 효과를 비교한다. | mac |

### COMP — 컴파일러 (Compilers)

작은 C 계열 언어(TinyC)를 대상으로 렉서→파서→타입 검사→SSA IR→최적화→레지스터 할당→AArch64 코드 생성까지 직접 구현해 M2에서 실행한다. 컴파일러를 알면 펌웨어 성능 튜닝·디스어셈블리 읽기가 쉬워지고, NPU/AI 가속기 회사의 ML 컴파일러(TVM·MLIR) 팀과 대화할 수 있는 기반이 된다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| COMP-B01 | 컴파일 파이프라인 관찰: 전처리→IR→asm→obj | 작은 C 파일을 clang -E / -S / -emit-llvm / -c 로 단계별 출력해 각 산출물을 주석 달아 설명하고, -O0 vs -O2 IR 차이에서 어떤 최적화가 일어났는지 3가지 찾아낸다. | mac |
| COMP-B02 | 렉서: 토큰화 & 소스 위치 | TinyC(정수/식별자/키워드/연산자/주석/문자열) 렉서를 구현하고 line:col 위치를 포함한 토큰 덤프를 golden file 테스트로 검증, 잘못된 문자에 대한 에러 위치도 테스트한다. | mac |
| COMP-B03 | 정규식 → NFA → DFA: 렉서 생성기 원리 | 토큰 정규식(식별자, 숫자, 연산자)을 Thompson NFA로 만들고 subset construction으로 DFA를 생성해, 손 렉서와 동일한 토큰열을 내는지 퍼즈 입력으로 비교한다. | mac |
| COMP-B04 | 식 파서: 재귀 하강 & Pratt 우선순위 | Pratt 파서로 이항/단항/호출/인덱스 식을 파싱해 S-expression으로 출력하고, 우선순위·좌/우 결합 케이스 40개 테스트를 통과시킨다. | mac |
| COMP-B05 | 문장·함수 파서 & AST 설계 | if/while/return/블록/함수 선언을 파싱해 AST를 만들고, pretty printer 출력→재파싱 round-trip이 동일 AST를 내는지 테스트한다. 노드 표현을 std::variant로 한 이유를 기록한다. | mac |
| COMP-B06 | 에러 복구 & 진단 메시지 | panic-mode 동기화로 구문 에러 후 파싱을 재개하고, clang 스타일 캐럿(^) 진단을 출력해 에러가 여러 개인 입력에서 모든 에러가 정확한 위치로 보고되는지 테스트한다. | mac |
| COMP-B07 | 트리 워킹 인터프리터 | 환경(스코프 체인)과 함수 호출을 지원하는 인터프리터를 구현해 fib·소수 체·재귀 프로그램을 실행하고, 이후 컴파일러 출력 검증용 reference 실행기로 사용한다. | mac |
| COMP-B08 | 의미 분석: 스코프 & 심볼 테이블 | 블록 스코프·섀도잉·전방 선언을 처리하는 resolver 패스를 작성해 미정의 변수, 중복 선언, 초기화 전 사용 에러를 테스트로 검출한다. | mac |
| COMP-B09 | 타입 검사기 | int/bool/float/포인터/함수 타입과 C 유사 산술 변환 규칙을 갖는 타입 검사기를 구현하고, 타입 오류 케이스 25개와 정상 케이스를 테스트한다. | mac |
| COMP-M01 | 3-주소 코드 IR, 기본 블록 & CFG | TinyC AST를 3-주소 코드로 lowering하고 기본 블록·CFG를 구성해 Graphviz dot으로 출력, IR 인터프리터 결과가 트리 인터프리터와 일치하는지 테스트한다. | mac |
| COMP-M02 | 지배자 트리 & SSA 변환 | Cooper-Harvey-Kennedy 알고리즘으로 지배자 트리와 dominance frontier를 계산하고 phi 삽입·renaming으로 SSA를 만든 뒤, '모든 변수 정의는 1회, 사용은 정의에 지배됨' 검증기를 통과시킨다. | mac |
| COMP-M03 | 상수 폴딩 & SCCP | lattice 기반 SCCP를 SSA IR에 구현하고, 상수 조건 분기 제거·연쇄 상수 전파 테스트 프로그램에서 최적화 전후 IR 명령어 수와 실행 결과 동일성을 확인한다. | mac |
| COMP-M04 | DCE & CFG 단순화 | mark-sweep 방식의 aggressive DCE와 도달 불가 블록 제거·빈 블록/직선 블록 병합을 구현하고, 각 패스 후 SSA 검증기와 결과 동일성 테스트를 돌린다. | mac |
| COMP-M05 | CSE / GVN (전역 값 번호 매기기) | 기본 블록 내 local value numbering을 구현한 뒤 지배자 트리 순회 기반 GVN으로 확장해, 교환법칙 정규화 포함 중복 식이 제거되는 것을 테스트로 보인다. | mac |
| COMP-M06 | 루프 분석 & LICM / 강도 감소 | back edge로 자연 루프를 식별하고 preheader를 삽입해 LICM과 귀납 변수 강도 감소(i*4 → 덧셈)를 구현, 루프 실행 명령어 수 감소를 IR 인터프리터 카운터로 측정한다. | mac |
| COMP-M07 | 데이터 흐름 분석: liveness & live interval | 일반화된 worklist 데이터 흐름 솔버를 만들고 liveness를 계산해 선형화된 명령어 순서 기준 live interval을 출력, 수작업 계산 예제와 비교한다. | mac |
| COMP-M08 | 레지스터 할당: Linear Scan | Poletto-Sarkar linear scan을 구현해 가용 레지스터 수를 4/8/16으로 바꿔가며 spill 횟수를 측정하고, 할당 결과가 interval 겹침 규칙을 어기지 않는지 검증기로 확인한다. | mac |
| COMP-M09 | 명령어 선택: 트리 타일링 → AArch64 | maximal munch 트리 타일링으로 IR을 AArch64 명령어로 선택하고, 주소 모드(base+imm), madd, 즉시값 인코딩 제약(12-bit, 시프트)을 처리하는 패턴 테스트를 작성한다. | mac |
| COMP-M10 | AArch64 코드 생성 & AAPCS64 호출 규약 | 프롤로그/에필로그·스택 프레임·callee-saved 레지스터를 처리하는 코드 생성기를 만들어 .s를 clang으로 어셈블·링크하고 M2에서 직접 실행, 인터프리터와 출력이 같은지 테스트 스위트로 확인한다. | mac |
| COMP-A01 | 그래프 컬러링 레지스터 할당 (Chaitin-Briggs) | 간섭 그래프 구성·simplify·spill·optimistic coloring·conservative coalescing을 구현하고, 동일 벤치마크에서 linear scan 대비 spill 수·실행 시간을 비교 보고한다. | mac |
| COMP-A02 | 오브젝트 파일 & 링킹 기초 | 자체 단순 오브젝트 포맷(섹션·심볼·재배치)을 정의하고 여러 오브젝트를 합치는 미니 링커를 작성하며, 실제 Mach-O를 otool/nm으로 분석해 ADRP/ADD 재배치와 대응시킨다. | mac |
| COMP-A03 | 명령어 스케줄링 & 핍홀 최적화 | 리스트 스케줄링(의존성 DAG, 지연 가중치)과 핍홀 규칙(중복 mov 제거, 비교+분기 결합, ldr/str 쌍→ldp/stp)을 구현해 벤치마크 실행 시간과 명령어 수 변화를 측정한다. | mac |
| COMP-A04 | LLVM 패스 작성 (New Pass Manager) | 명령어 통계 분석 패스와 간단한 변환 패스(곱셈→시프트)를 플러그인으로 작성해 opt -load-pass-plugin으로 실행하고, FileCheck 스타일 테스트로 결과를 검증한다. | mac |
| COMP-A05 | TinyC → LLVM IR 프런트엔드 | TinyC AST에서 텍스트 LLVM IR(alloca + mem2reg 방식)을 생성해 clang으로 컴파일하고, 직접 만든 백엔드와 -O2 LLVM 결과의 성능·코드 크기를 비교한다. | mac |
| COMP-A06 | ML 그래프 IR: shape 추론 & 패턴 재작성 엔진 | 텐서 타입(shape·dtype·layout)을 갖는 그래프 IR과 shape 추론 패스, DAG 패턴 매처 기반 rewrite 엔진(MLIR RewritePattern 유사, 고정점 적용)을 구현하고, 퓨전·폴딩 규칙 몇 개를 패턴으로 등록해 참조 실행기와 수치 동일성을 확인한다(퓨전 자체의 성능 분석은 EML/NPU 트랙). | mac |
| COMP-A07 | 스케줄 프리미티브: 루프 IR 변환 (TVM/Halide 스타일) | 루프 네스트 IR에 split/reorder/tile/unroll/vectorize 변환을 구현하고 각 변환의 합법성(의존성) 검사를 넣어, matmul·conv 스케줄을 조합해 C 코드로 내보낸 뒤 M2에서 결과 동일성과 GFLOPS 변화를 측정한다. | mac |
| COMP-A08 | MLIR 입문: 다이얼렉트 lowering | linalg.matmul 예제를 mlir-opt로 linalg→affine→scf→llvm 다이얼렉트까지 단계별로 lowering하고 각 단계 IR을 해설, mlir-cpu-runner(또는 mlir-runner)로 실행해 결과를 검증한다. | mac |
| COMP-A09 | 루프 자동 벡터화 → AArch64 NEON | TinyC 백엔드에 단순 루프(배열 덧셈·내적·saxpy)의 의존성/정렬 검사, 벡터 본체 + 스칼라 꼬리 루프 생성, NEON(ld1/fadd/fmla) 코드 생성을 추가해 스칼라 버전 대비 속도와 결과 동일성을 측정하고 clang -Rpass=loop-vectorize 결과와 비교한다. | mac |

### VM — 언어 런타임 · VM (Language Runtimes & VMs)

바이트코드 VM, 가비지 컬렉터, JIT는 언어 런타임의 핵심이며 MicroPython·Lua·WASM처럼 임베디드 기기에도 직접 들어간다. 이 트랙은 스택/레지스터 VM부터 GC, 인라인 캐시, Apple Silicon JIT까지 C++로 구현해 메모리·성능 트레이드오프를 몸으로 익힌다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| VM-B01 | 스택 기반 바이트코드 VM | push/pop/산술/비교/jmp/jz/print 명령을 갖는 스택 VM과 텍스트 어셈블러를 구현하고, 루프로 1..N 합과 fib(25)를 실행해 기대값과 비교한다. | mac |
| VM-B02 | 바이트코드 컴파일러: AST → 스택 코드 | COMP 트랙의 TinyC 파서를 재사용해 지역 변수 슬롯, if/while 점프 백패칭, 함수 호출을 바이트코드로 생성하고 트리 인터프리터와 출력이 같은지 테스트한다. | mac |
| VM-B03 | 값 표현: tagged union vs NaN-boxing vs 포인터 태깅 | Value 타입을 tagged union과 NaN-boxing 두 방식으로 구현해 sizeof, 산술 벤치마크, 디버깅 난이도를 비교하고, 64-bit ARM 포인터 상위 비트(TBI) 활용 가능성을 정리한다. | mac |
| VM-B04 | 콜 프레임 & 네이티브 함수 (FFI) | CallFrame 스택, 인자/반환값 전달, 재귀 깊이 제한(stack overflow 에러), C++ 네이티브 함수 등록(clock, sqrt)을 구현하고 깊은 재귀·잘못된 인자 수 테스트를 작성한다. | mac |
| VM-B05 | 디스패치: switch vs computed goto vs threading | 같은 VM을 switch, computed goto(&&label), direct threading으로 구현해 M2에서 벤치마크하고, 명령어당 사이클과 분기 미스 경향을 비교한 표를 작성한다. | mac |
| VM-B06 | 레지스터 기반 VM (Lua 스타일) | 3-주소 형식(ADD A B C) 레지스터 VM과 컴파일러를 구현해 동일 프로그램에서 스택 VM 대비 실행 명령어 수, 바이트코드 크기, 실행 시간을 비교한다. | mac |
| VM-B07 | 힙 객체: 문자열 인터닝 & 해시 테이블 | 힙 문자열 객체와 인터닝 테이블, open addressing 해시 테이블(전역 변수/객체 필드 저장)을 구현하고 문자열 동등성 비교가 포인터 비교로 바뀌는 효과를 측정한다. | mac |
| VM-B08 | 바이트코드 디스어셈블러 & 검증기 | 디스어셈블러와 함께 점프 대상 유효성, 스택 깊이 균형, 상수 인덱스 범위를 검사하는 정적 검증기를 구현하고, 변조된 바이트코드 퍼즈 입력에서 VM이 크래시 없이 거부하는지 확인한다. | mac |
| VM-M01 | 클로저 & upvalue | 중첩 함수의 변수 캡처를 open upvalue 리스트와 close 동작으로 구현하고, 카운터 생성기·루프 변수 캡처 테스트로 의미론이 맞는지 검증한다. | mac |
| VM-M02 | Mark-Sweep GC | VM 스택·전역·upvalue를 루트로 하는 mark-sweep GC를 구현하고, 매 할당마다 GC를 돌리는 stress 모드와 ASan으로 누락된 루트 버그가 없는지 검증한다. | mac |
| VM-M03 | 참조 카운팅 & 순환 수집 | 참조 카운팅 GC를 구현한 뒤 trial deletion(Bacon-Rajan) 순환 수집기를 추가해 순환 구조가 회수됨을 테스트하고, mark-sweep 대비 처리량·지연을 비교한다. | mac |
| VM-M04 | Copying GC (Cheney) | Cheney BFS 복사 GC를 구현해 할당을 bump pointer로 바꾸고, 생존율에 따른 GC 시간과 단편화 제거 효과를 mark-sweep과 비교 측정한다. | mac |
| VM-M05 | 세대별 GC & write barrier | nursery(copying) + old(mark-sweep) 2세대 GC와 card table write barrier를 구현하고, old→young 포인터 누락 시 버그를 테스트로 재현·수정하며 minor GC 일시정지 시간을 측정한다. | mac |
| VM-M06 | 증분 삼색 마킹 & 일시정지 시간 | tri-color 증분 마킹(작업 단위 예산)을 구현하고 Dijkstra/Yuasa barrier로 불변식을 유지, GC 일시정지 히스토그램(p50/p99/max)을 stop-the-world 버전과 비교한다. | mac |
| VM-M07 | 객체 모델: hidden class & inline cache | 속성 추가 순서에 따른 shape 전이 트리와 monomorphic/polymorphic inline cache를 구현해, 속성 접근 벤치마크에서 해시 조회 대비 속도 향상과 megamorphic 전환을 측정한다. | mac |
| VM-M08 | VM 수준 코루틴 & 예외 처리 | Lua 스타일 코루틴(각자 값 스택 보유, resume/yield)과 try/throw 핸들러 스택을 구현하고, 생산자-소비자·중첩 예외 테스트를 통과시킨다. | mac |
| VM-M09 | WASM 바이너리 파서 & 인터프리터 | .wasm 바이너리(type/function/code/export 섹션, LEB128)를 파싱하고 i32 산술·local·block/loop/br_if·call을 실행하는 인터프리터를 구현해, wat2wasm으로 만든 테스트 모듈 결과를 검증한다. | mac |
| VM-A01 | JIT 기초: Apple Silicon에서 기계어 실행 | MAP_JIT 매핑과 pthread_jit_write_protect_np, sys_icache_invalidate를 사용해 AArch64 기계어(add/ret)를 직접 인코딩·실행하는 최소 JIT를 만들고, 각 단계 누락 시 발생하는 크래시를 재현한다. | mac |
| VM-A02 | 템플릿 JIT: 바이트코드 → AArch64 | 레지스터 VM의 산술·분기 명령을 AArch64 템플릿으로 변환하는 baseline JIT를 구현해 인터프리터와 결과 동일성 및 벤치마크 속도 향상을 측정한다. | mac |
| VM-A03 | 프로파일 기반 티어링 & 핫 루프 감지 | 함수 호출/back-edge 카운터로 핫 코드를 감지해 JIT로 승격하는 티어링을 구현하고, 임계값에 따른 워밍업 시간·최종 처리량 트레이드오프를 그래프로 보고한다. | mac |
| VM-A04 | WASM 런타임: 선형 메모리 샌드박싱 | WASM 인터프리터에 선형 메모리를 명시적 바운드 체크 vs guard page(mmap PROT_NONE + 시그널) 두 방식으로 구현해 성능을 비교하고, 호스트 함수 import(로그, 센서 읽기)를 추가한다. | mac |
| VM-A05 | MicroPython 내부 구조 분석 & C 모듈 | MicroPython unix 포트를 Mac에서 빌드해 C로 user module(가상 센서 드라이버)을 추가하고, gc.mem_free/heap 덤프로 객체 할당 패턴과 GC 동작을 관찰해 정리한다. | mac |
| VM-A06 | Lua 임베딩 & C API 샌드박스 | Lua 5.4를 C++ 호스트에 임베딩해 센서 API를 노출하고, 커스텀 lua_Alloc으로 메모리 상한, instruction count hook으로 실행 시간 상한을 걸어 악성 스크립트가 호스트를 멈추지 못함을 테스트한다. | mac |
| VM-A07 | eBPF 스타일 검증 VM | 레지스터 기반 소형 ISA와 검증기(유한 루프, 레지스터 타입·범위 추적, 맵 접근 경계)를 구현해 통과한 프로그램만 실행하고, 무한 루프·OOB 접근 프로그램이 로드 시 거부되는지 테스트한다. | mac |
| VM-A08 | MCU용 고정 메모리 스크립팅 VM 설계 | 32KB 고정 힙에서 동작하는 VM(compacting GC, 정적 스택, 오류 시 안전 정지)을 설계·구현하고, 메모리 고갈·장시간 실행 테스트와 함께 MicroPython/Lua 대비 트레이드오프 문서를 작성한다. | mac |

### ROS — 로봇 소프트웨어 스택 (Robot Software Stack)

ROS 2는 로봇·드론·자율주행 스타트업의 사실상 표준 미들웨어이며, 펌웨어 엔지니어는 micro-ROS·하드웨어 인터페이스·실시간 성능 경계에서 자주 만난다. 이 트랙은 ROS 2를 '사용'하는 것과 동시에, 직렬화·QoS·executor·tf를 갖춘 미니 pub/sub 미들웨어를 C++로 직접 구현해 내부 동작을 이해한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| ROS-B01 | 미니 pub/sub 1: 인프로세스 타입 안전 토픽 | 템플릿 기반 Topic<T>/Publisher<T>/Subscriber<T>를 구현해 다중 구독자 전달, 구독 해제 중 publish, 타입 불일치 컴파일 에러를 단위 테스트로 검증한다. | mac |
| ROS-B02 | 메시지 직렬화: CDR 인코딩 구현 | sensor_msgs/Imu 형태의 구조체를 XCDR1 규칙(정렬, 시퀀스 길이, 엔디언 헤더)으로 직렬화/역직렬화하는 코드를 작성하고, 알려진 바이트 덤프와 비교하는 round-trip 테스트를 만든다. | mac |
| ROS-B03 | 미니 pub/sub 2: UDP 멀티캐스트 & discovery | UDP 멀티캐스트로 participant announce(토픽명·타입 해시)를 주고받는 discovery와 unicast 데이터 전송을 구현해, 두 프로세스가 자동으로 연결되고 타입 불일치 시 거부되는지 확인한다. | mac |
| ROS-B04 | ROS 2 시작: rclcpp 노드·토픽 | colcon 워크스페이스에 rclcpp talker/listener 패키지를 작성하고 ros2 topic echo/hz/info와 rqt_graph로 그래프를 확인, 발행 주기 지터를 측정한다. | **linux** |
| ROS-B05 | Services & Actions | '목표 각도로 회전' action server(주기적 feedback, cancel, 선점)와 상태 조회 service를 구현하고, 클라이언트에서 취소·타임아웃 시나리오를 테스트한다. | **linux** |
| ROS-B06 | Parameters & launch | YAML 파라미터 파일·파라미터 변경 콜백(범위 검증 포함)·Python launch 파일(인자, remapping, namespace)로 2개 노드 시스템을 기동하고, 잘못된 값 설정이 거부되는지 확인한다. | **linux** |
| ROS-B07 | 커스텀 메시지 & rosidl 인터페이스 | 배터리 상태 커스텀 메시지·서비스를 정의해 빌드하고, 생성된 C++ 헤더와 type support 코드를 읽어 직렬화 경로를 B02의 CDR 구현과 대조한다. | **linux** |
| ROS-B08 | ROS 2 계층 구조: rclcpp → rcl → rmw → DDS | RMW_IMPLEMENTATION을 Fast DDS와 Cyclone DDS로 바꿔 동일 노드를 실행하고, gdb 백트레이스로 publish 호출 경로를 계층별로 문서화, 두 구현의 지연·CPU 사용을 비교한다. | **linux** |
| ROS-M01 | QoS 정책을 미니 미들웨어에 구현 | 미니 미들웨어에 KEEP_LAST(depth), TRANSIENT_LOCAL(늦은 구독자에게 마지막 N개 재전송), RELIABLE(ACK/NACK 재전송), deadline 위반 이벤트를 구현하고 패킷 손실 주입 테스트로 검증한다. | mac |
| ROS-M02 | ROS 2 QoS 불일치 & 손실 링크 실험 | RELIABLE 구독자-BEST_EFFORT 발행자 등 QoS 비호환 조합에서 연결 실패 이벤트를 확인하고, tc netem으로 손실/지연을 주입해 센서 데이터와 명령 토픽에 맞는 QoS 프로파일을 결정·정당화한다. | **linux** |
| ROS-M03 | Executor & callback group | SingleThreaded vs MultiThreaded executor, MutuallyExclusive vs Reentrant callback group 조합에서 타이머 콜백 안 동기 서비스 호출 데드락을 재현하고 올바른 구성으로 해결한다. | **linux** |
| ROS-M04 | 미니 미들웨어 3: executor & wait-set 구현 | wait-set(kqueue/poll 기반)과 타이머 휠로 단일·멀티스레드 executor를 구현하고, 콜백 우선순위와 starvation 여부를 테스트한다. | mac |
| ROS-M05 | tf2 변환 트리 구현 | 프레임 간 변환을 타임스탬프 버퍼로 저장하고 두 프레임 사이 lookupTransform(공통 조상 탐색, 이동 선형보간 + 회전 slerp)을 구현해, 알려진 기구학 예제와 extrapolation 에러 케이스를 테스트한다. | mac |
| ROS-M06 | Intra-process & zero-copy 통신 | unique_ptr publish 기반 intra-process 통신과 component container를 구성해 1MB 이미지 메시지의 복사 횟수·지연을 inter-process와 비교 측정한다. | **linux** |
| ROS-M07 | MCAP 기반 bag 기록/재생기 | MCAP 형식(chunk, index, schema/channel 레코드)으로 미니 미들웨어 토픽을 기록하고 원본 타이밍 그대로(배속 지원) 재생하는 도구를 구현, Foxglove/ros2 bag info로 열려 검증한다. | mac |
| ROS-M08 | Lifecycle(managed) 노드 | 센서 드라이버를 lifecycle 노드로 구현(configure/activate/deactivate/cleanup, 에러 처리 전이)하고, 매니저 노드로 의존 순서에 맞춰 기동·장애 시 복구하는 시나리오를 테스트한다. | **linux** |
| ROS-M09 | ros2_control 하드웨어 인터페이스 | 시뮬레이션 모터(위치·속도 상태, 지연 포함)를 SystemInterface 플러그인으로 구현하고 joint_trajectory_controller로 구동, read/write 주기 지터를 측정한다. | **linux** |
| ROS-A01 | 실시간 ROS 2: 지터 측정 & 할당 제거 | PREEMPT_RT 커널(또는 일반 커널 비교)에서 1kHz 제어 루프 노드의 지터 히스토그램을 측정하고, mlockall·메모리 사전할당·실시간 스레드 우선순위·전용 executor 적용 전후를 비교한다. | **linux** |
| ROS-A02 | ros2_tracing으로 end-to-end 지연 분석 | LTTng + ros2_tracing으로 센서→필터→제어 3단 파이프라인을 트레이스하고, publish→callback 지연과 executor 대기 시간을 분해해 병목을 찾아 개선한다. | **linux** |
| ROS-A03 | micro-ROS: MCU ↔ agent | micro-ROS 클라이언트(Zephyr native_sim 또는 실제 보드)를 빌드해 agent와 serial/UDP로 연결하고 IMU 토픽 발행·파라미터 서비스를 구현, 정적 메모리 설정(rmw_microxrcedds 히스토리 크기)을 조정한다. | **linux** |
| ROS-A04 | XRCE 스타일 경량 클라이언트 프로토콜 구현 | 모의 UART 위에서 세션 생성·스트림·reliable 재전송을 갖는 경량 클라이언트(정적 버퍼, malloc 없음)와 미니 미들웨어 브리지 agent를 구현하고 바이트 오류 주입 테스트를 통과시킨다. | mac |
| ROS-A05 | 미니 미들웨어 4: shared-memory 전송 | shm_open 기반 공유 메모리 청크 풀과 lock-free 인덱스 큐로 publisher→subscriber zero-copy 전송을 구현해 UDP 전송 대비 1KB/1MB 메시지 지연 p50/p99를 측정한다. | mac |
| ROS-A06 | 모바일 로봇 소프트웨어 아키텍처 설계 | LiDAR·IMU·휠 오도메트리·카메라를 가진 모바일 로봇의 노드 그래프, 토픽별 QoS, 프로세스/컴포넌트 배치, 워치독·안전 정지 경로를 설계 문서로 작성하고 핵심 노드 간 인터페이스를 launch로 구성한다. | mac |

### CV — 컴퓨터 비전 (Computer Vision)

고전 컴퓨터 비전 알고리즘(필터, 엣지, 코너, 호모그래피, 캘리브레이션, 스테레오, 광류)을 OpenCV 없이 C++20으로 직접 구현하고 OpenCV 결과와 비교 검증한다. 드론·로봇·카메라 펌웨어 면접에서는 '왜 이 알고리즘이 이렇게 동작하는가'와 '임베디드에서 어떻게 빠르게 돌리는가'를 함께 묻기 때문에, NEON·타일링·고정소수점 최적화까지 다룬다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| CV-B01 | Image<T> 컨테이너 & ROI 뷰 | stride·채널 수·픽셀 타입(u8/s16/f32)을 갖는 Image<T>와 ROI 뷰(복사 없음), 타입 변환·PGM/PPM 입출력을 구현하고, 패딩된 stride 이미지에서 ROI 연산 결과를 OpenCV Mat과 비교한다(NV12 등 센서 포맷은 CAM 트랙). | mac |
| CV-B02 | 픽셀 연산·LUT·히스토그램 평활화 | 밝기/대비, 감마 LUT, 히스토그램 계산과 평활화를 구현해 cv::equalizeHist 결과와 픽셀 단위 차이를 측정한다. | mac |
| CV-B03 | 2D 컨볼루션 & 경계 처리 | 임의 커널 2D 컨볼루션과 4가지 경계 모드를 구현하고 cv::filter2D와 최대 오차를 비교, 커널 크기에 따른 연산량을 측정한다. | mac |
| CV-B04 | 분리 가능 필터·Gaussian·적분 영상 | separable Gaussian blur와 적분 영상 기반 O(1) box filter를 구현해 naive 2D 컨볼루션 대비 속도와 결과 동일성을 확인한다. | mac |
| CV-B05 | 순위 필터: Median & Min/Max | naive 정렬 median, 히스토그램 슬라이딩(Huang) median, 3x3 정렬 네트워크 median을 구현해 salt-and-pepper 노이즈 영상에서 PSNR과 커널 크기별 실행 시간을 cv::medianBlur와 비교한다(bilateral NR은 CAM 트랙). | mac |
| CV-B06 | Sobel 그래디언트: 크기와 방향 | Sobel/Scharr로 Gx, Gy, 크기, 방향을 계산하고 합성 테스트 영상(사각형, 원, 경사)에서 기대 방향과 일치하는지 수치로 확인한다. | mac |
| CV-B07 | Canny 엣지 검출 전체 구현 | Gaussian→Sobel→방향 양자화 NMS→이중 임계값 히스테리시스(BFS 연결)를 구현해 cv::Canny와 엣지 픽셀 일치율을 측정한다. | mac |
| CV-B08 | 임계값: Global / Otsu / Adaptive | Otsu(클래스 간 분산 최대화)와 적분 영상 기반 adaptive mean threshold를 구현해 그림자 있는 문서 영상에서 결과를 비교하고 OpenCV와 임계값을 대조한다. | mac |
| CV-B09 | 형태학 연산: Erode·Dilate·Open·Close | 임의 구조 요소 erode/dilate와 open/close/gradient를 구현하고, 큰 직사각형 커널에 van Herk/Gil-Werman 알고리즘을 적용해 커널 크기와 무관한 실행 시간을 보인다. | mac |
| CV-M01 | 연결 요소 라벨링 & blob 통계 | 4/8-연결 2-pass 라벨링(union-find)을 구현하고 각 blob의 면적·중심·bounding box·모멘트를 계산해 cv::connectedComponentsWithStats와 비교한다. | mac |
| CV-M02 | 기하 변환 & 보간 | affine/perspective warp를 역매핑으로 구현하고 nearest/bilinear/bicubic 보간을 비교, 회전-역회전 왕복 PSNR로 보간 품질을 정량화한다. | mac |
| CV-M03 | 이미지 피라미드: Gaussian / Laplacian & 블렌딩 | Gaussian/Laplacian 피라미드 생성·복원(오차 0 확인)과 Laplacian 블렌딩으로 두 영상을 seam 없이 합성한다. | mac |
| CV-M04 | Harris / Shi-Tomasi 코너 | 구조 텐서·Harris 응답·Shi-Tomasi 최소 고유값과 NMS를 구현하고, 회전·밝기 변화 영상에서 반복성(repeatability)을 측정한다. | mac |
| CV-M05 | FAST 코너 & ORB 디스크립터 | FAST-9 검출과 방향(intensity centroid)·rotated BRIEF 256-bit 디스크립터를 구현해 회전 영상 간 매칭 정확도를 OpenCV ORB와 비교한다. | mac |
| CV-M06 | 특징 매칭: 해밍 거리·ratio test·cross-check | popcount 기반 brute-force 해밍 매처, Lowe ratio test, 상호 일치 검사를 구현해 정답 호모그래피가 있는 영상 쌍에서 precision/recall을 측정한다. | mac |
| CV-M07 | 호모그래피: 정규화 DLT | Hartley 정규화를 포함한 4점 이상 DLT(SVD) 호모그래피 추정을 구현하고, 합성 대응점에 노이즈를 넣어 정규화 유무에 따른 재투영 오차를 비교한다. | mac |
| CV-M08 | RANSAC & 파노라마 스티칭 | 적응형 반복 횟수 RANSAC으로 호모그래피를 추정해 두 장의 사진을 스티칭하고, 이상치 비율별 성공률·반복 횟수를 측정한다. | mac |
| CV-M09 | Hough 변환: 직선과 원 | (ρ,θ) 누적 배열 직선 Hough와 그래디언트 방향을 이용한 원 Hough를 구현해 차선/원형 표적 합성 영상에서 검출 정확도를 확인한다. | mac |
| CV-M10 | 핀홀 카메라 모델: 투영·역투영·좌표계 | 내부(K)·외부(R,t) 파라미터로 투영, 픽셀→정규화 좌표→3D 광선 역투영, FOV↔초점거리 변환, OpenCV/ROS 카메라 좌표 규약 변환을 구현하고 합성 장면 왕복 테스트로 검증한다(렌즈 왜곡 보정 구현은 CAM 트랙의 모델을 재사용). | mac |
| CV-A01 | 카메라 캘리브레이션 (Zhang 방법) | 합성 체커보드 뷰(알려진 정답)에서 호모그래피→폐형식 내부 파라미터→Levenberg-Marquardt 재투영 오차 최소화를 구현해 정답 및 cv::calibrateCamera와 비교한다. | mac |
| CV-A02 | 에피폴라 기하: 기본 행렬 & 정류 | 정규화 8-point 알고리즘(rank-2 강제)과 RANSAC으로 기본 행렬을 추정하고 에피폴라 선을 시각화, 본질 행렬에서 R,t 4해 중 정답을 cheirality로 고른다. | mac |
| CV-A03 | 스테레오 매칭 & 깊이 추정 | SAD/census 블록 매칭과 좌우 일관성 검사로 disparity map을 계산하고 Middlebury 데이터셋 정답 대비 bad-pixel 비율과 깊이 변환(Z=fB/d)을 검증한다. | mac |
| CV-A04 | 광류: 피라미드 Lucas-Kanade | 피라미드 Lucas-Kanade(반복 갱신, 고유값 신뢰도 검사)를 구현해 알려진 이동/회전 합성 시퀀스에서 추적 오차를 측정하고 cv::calcOpticalFlowPyrLK와 비교한다. | mac |
| CV-A05 | PnP 자세 추정 & 마커 기반 측위 | DLT 초기해 + Gauss-Newton 재투영 최소화 PnP를 구현해 정사각 마커 4코너로 6-DoF 자세를 추정하고, 노이즈에 따른 위치/각도 오차를 cv::solvePnP와 비교한다. | mac |
| CV-A06 | 임베디드 최적화: NEON SIMD & 고정소수점 | Gaussian 3x3과 Sobel을 arm_neon.h intrinsic + u8/s16 고정소수점으로 재작성해 스칼라 대비 Mpix/s 향상과 결과 동일성(비트 정확도)을 M2에서 측정한다. | mac |
| CV-A07 | 비전 연산 퓨전 & 타일 스케줄 (Halide 스타일) | Gaussian 피라미드→Harris 응답→NMS 3단을 단계별 전체 프레임 버전, 타일 퓨전(경계 halo 재계산) 버전, 타일 병렬 버전으로 구현해 중간 버퍼 메모리·처리 속도·재계산 비율을 비교한다(ISP식 라인 버퍼 스트리밍은 CAM 트랙). | mac |
| CV-A08 | 실시간 비전 파이프라인 설계 & 지연 예산 | 녹화 영상을 30fps로 공급하는 캡처 스레드, 더블 버퍼, 특징 검출+LK 추적 단계로 파이프라인을 만들고 단계별 지연·프레임 드롭을 계측해 33ms 예산 배분과 트레이드오프를 문서화한다. | mac |

### REND — 렌더링 (Rendering)

GFX 트랙의 기하·래스터화 부품을 바탕으로 레이 트레이서와 프로그래머블 소프트웨어 렌더러를 만들어 GPU 파이프라인이 내부에서 무엇을 하는지(셰이딩, 블렌딩, 텍스처 필터링, 그림자, AA, 타일 기반 처리)를 이해한다. Apple·NVIDIA·Qualcomm GPU/디스플레이 펌웨어·드라이버 직무에서 요구하는 GPU API(Metal/Vulkan) 개념과 렌더 그래프·동기화까지 연결한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| REND-B01 | 렌더 파이프라인 골격: GFX 부품 조립 | vertex shader / fragment shader를 함수 객체로 받는 프로그래머블 소프트웨어 파이프라인(입력 조립→정점 셰이딩→클립→원근 나눗셈→뷰포트→래스터→프래그먼트→깊이 테스트)을 구성해 OBJ 모델을 렌더링하고, 단계별 처리 개수 통계를 출력한다. | mac |
| REND-B02 | 프레임버퍼·이미지 출력·sRGB 감마 | float RGB 프레임버퍼와 PPM/PNG(stb_image_write) 출력을 구현하고, 선형 공간 블렌딩 vs sRGB 공간 블렌딩 그라디언트를 비교해 감마 처리의 필요성을 보인다. | mac |
| REND-B03 | 레이 트레이서 1: 카메라 레이 & 장면 교차 | 핀홀 카메라 레이 생성(픽셀 중심, 화각)과 구·평면 교차, GFX의 삼각형 교차를 묶은 Hittable 장면 인터페이스(최근접 t, 법선 방향 front-face 처리)를 구현해 법선 시각화 이미지를 렌더링하고 self-intersection(epsilon) 문제를 재현·해결한다. | mac |
| REND-B04 | 레이 트레이서 2: 재질·반사·굴절 | diffuse/metal/dielectric 재질과 재귀 반사·Snell 굴절·전반사·Schlick Fresnel을 구현해 유리구 장면을 렌더링하고, 레이 깊이에 따른 결과를 비교한다. | mac |
| REND-B05 | 블렌딩 & 투명도: alpha·premultiplied·정렬 | 소프트웨어 파이프라인에 블렌드 상태(src/dst factor)와 straight vs premultiplied alpha를 구현하고, 투명 삼각형 정렬 유무·깊이 쓰기 on/off에 따른 결과 오류를 재현, weighted blended OIT 근사와 비교한다. | mac |
| REND-B06 | 셰이딩 모델: Lambert·Phong·Blinn-Phong | 래스터라이저에 점/방향 광원과 Lambert·Phong·Blinn-Phong 조명을 구현하고 flat/Gouraud/per-pixel 셰이딩 결과와 연산량을 비교한다. | mac |
| REND-B07 | 법선 매핑 & 탄젠트 공간 | 메시 UV에서 정점별 탄젠트/바이탄젠트(MikkTSpace 개념)를 계산하고 normal map 샘플을 TBN 행렬로 변환해 셰이딩, 탄젠트 부호/정규화 오류가 만드는 아티팩트를 재현·수정한다. | mac |
| REND-M01 | 텍스처 매핑 & mipmap 필터링 | nearest/bilinear 샘플링, mip 체인 생성, 화면 공간 미분 기반 LOD 선택과 trilinear 필터링을 구현해 기울어진 체커 평면의 aliasing(무아레)이 줄어드는 것을 보인다. | mac |
| REND-M02 | 그림자: 레이 트레이싱 vs shadow map | 레이 트레이서에 shadow ray 기반 hard/soft shadow를, 래스터라이저에 shadow map을 구현하고 shadow acne·peter-panning을 bias와 PCF로 해결하는 과정을 기록한다. | mac |
| REND-M03 | 안티에일리어싱: SSAA·MSAA·후처리 AA | 래스터라이저에 4x SSAA, 4x MSAA(커버리지 샘플, 셰이딩 1회), 간단한 FXAA 유사 후처리를 구현해 엣지 품질과 픽셀당 셰이딩 횟수를 비교한다. | mac |
| REND-M04 | PBR: Cook-Torrance GGX | GGX NDF·Smith 기하 항·Schlick Fresnel의 Cook-Torrance BRDF를 구현해 metallic/roughness 그리드를 렌더링하고, white furnace 테스트로 에너지 보존을 검증한다. | mac |
| REND-M05 | 몬테카를로 경로 추적 & 중요도 샘플링 | cosine-weighted·BRDF 중요도 샘플링과 next event estimation을 포함한 경로 추적기를 만들어 Cornell box에서 spp별 노이즈(RMSE)를 균등 샘플링과 비교한다. | mac |
| REND-M06 | BVH 가속 & 멀티스레드 타일 렌더 | GFX 트랙의 BVH(SAH)를 연결해 10만 삼각형 메시를 렌더링하고, 타일 단위 작업 큐로 M2 코어 수에 따른 확장성(speedup)을 측정한다. | mac |
| REND-M07 | Forward vs Deferred 셰이딩 | 소프트웨어 래스터라이저에 G-buffer(위치/법선/알베도/깊이)를 쓰는 deferred 경로를 추가해 광원 1~256개에서 forward 대비 프레임 시간과 메모리 대역폭을 측정한다. | mac |
| REND-M08 | TBDR: 지연 HSR & 타일 메모리 load/store | GFX 트랙의 타일 binning 위에 타일 단위 hidden surface removal(가시 프래그먼트만 셰이딩)과 렌더 패스 load/store action(clear/load/dontCare, store/dontCare, memoryless 깊이)을 구현해 overdraw 장면에서 셰이딩 횟수와 DRAM 트래픽을 즉시 모드 대비 계수한다. | mac |
| REND-A01 | Metal 기초: 삼각형부터 텍스처까지 | metal-cpp로 device·command queue·command buffer·render pipeline state·vertex/fragment shader를 구성해 텍스처가 입혀진 회전 큐브를 렌더링하고, 각 객체의 역할을 소프트웨어 래스터라이저 단계와 대응시킨다. | mac |
| REND-A02 | Metal 컴퓨트 & CPU-GPU 동기화 | 톤매핑·블러 컴퓨트 커널을 추가하고 triple buffering + dispatch semaphore로 CPU-GPU 겹침을 구현, 동기화를 빼면 생기는 데이터 경합 아티팩트를 재현한다. | mac |
| REND-A03 | Vulkan 개념 매핑: 배리어·디스크립터·렌더 패스 | MoltenVK 위에서 Vulkan 삼각형 예제를 작성하고 descriptor set, pipeline barrier(레이아웃 전환), render pass/subpass를 Metal 대응 개념과 표로 정리, validation layer 경고를 모두 해결한다. | mac |
| REND-A04 | 렌더 그래프 설계 & 구현 | 패스가 읽기/쓰기 리소스를 선언하면 위상 정렬·사용 안 되는 패스 제거·배리어 삽입·transient 리소스 메모리 aliasing을 수행하는 렌더 그래프를 구현하고, deferred 파이프라인을 그래프로 표현해 메모리 절감을 측정한다. | mac |
| REND-A05 | 타일드/클러스터드 라이트 컬링 | 화면 타일 × 깊이 슬라이스 클러스터에 광원을 할당하는 컬링을 구현해 1000개 광원 장면에서 픽셀당 평가 광원 수와 프레임 시간을 deferred 기본형과 비교한다. | mac |
| REND-A06 | TAA: 지터·재투영·히스토리 클램핑 | 서브픽셀 지터(Halton), 모션 벡터 재투영, 이웃 색 클램핑을 갖춘 TAA를 구현해 움직이는 장면에서 고스팅과 엣지 품질을 MSAA와 비교한다. | mac |
| REND-A07 | 프레임 페이싱 & GPU 타임스탬프 프로파일링 | Metal counter sample buffer(또는 GPU start/end time)로 패스별 GPU 시간을, CPU 측 스코프 타이머로 인코딩 시간을 측정해 16.6ms 예산 대비 병목(CPU-bound/GPU-bound)을 판별하는 리포트를 만든다. | mac |

### PHYS — 물리 엔진 (Physics Engines)

적분기, 강체 동역학, 충돌 검출(SAP·GJK·SAT), 충격량 기반 접촉 해석, 구속조건까지 2D/3D 물리 엔진을 단계별로 구현한다. 로봇·드론 시뮬레이터, 게임, 자율주행 SITL 모두 같은 원리를 쓰므로, 수치 안정성·고정 타임스텝·결정성 같은 실무 이슈를 측정 가능한 실험으로 다룬다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| PHYS-B01 | 적분기 1: Explicit vs Semi-implicit Euler | 스프링-질량계를 explicit/semi-implicit(symplectic) Euler로 적분해 dt별 총에너지 drift를 CSV로 기록·플롯하고, 해석해 대비 오차와 발산 조건을 정리한다. | mac |
| PHYS-B02 | 적분기 2: Position Verlet & Velocity Verlet | position/velocity Verlet를 구현해 행성 궤도(중력 2체) 10만 스텝 시뮬레이션에서 궤도 반경·에너지 보존을 Euler와 비교한다. | mac |
| PHYS-B03 | 적분기 선택: 게임/로봇 물리에서 RK4가 드문 이유 | SIM의 RK4와 semi-implicit Euler·velocity Verlet로 튕기는 공·마찰 슬라이드처럼 힘이 불연속인 장면을 돌려 스텝당 비용, 접촉 시 오차, 에너지 거동을 비교하고 엔진들이 1차 symplectic 적분을 쓰는 근거를 정리한다. | mac |
| PHYS-B04 | 고정 타임스텝 물리 & 렌더 보간 | accumulator 기반 고정 dt 물리 루프와 이전/현재 상태 보간 출력을 구현해, 가변 프레임 시간(20~120fps 무작위)에서도 동일한 궤적을 내는지 테스트하고 spiral of death 방지책을 넣는다. | mac |
| PHYS-B05 | 파티클 시스템 & 힘 누적 | 중력·선형/이차 항력·스프링·댐퍼 힘 생성기를 갖춘 파티클 시스템을 구현해 종단 속도·감쇠 진동 주기를 해석해와 비교 검증한다. | mac |
| PHYS-B06 | 2D 강체: 질량·관성 모멘트·회전 | 다각형 질량 중심·관성 모멘트 계산과 힘/토크 적용, 각속도 적분을 구현하고 편심 충격 시 선/각 운동량 보존을 수치로 확인한다. | mac |
| PHYS-B07 | 기본 충돌 판정: 원·AABB·OBB | 원-원, AABB-AABB, 원-AABB, 점-OBB 최근접점과 교차 판정을 구현하고 경계 접촉·포함 관계 케이스를 포함한 단위 테스트를 작성한다. | mac |
| PHYS-B08 | 3D 강체 회전: 쿼터니언 & 관성 텐서 | 쿼터니언 각속도 적분(정규화 포함)과 I_world = R I_body Rᵀ 변환을 구현해 중간축 정리(Dzhanibekov 효과)를 재현하고 각운동량 보존 오차를 측정한다. | mac |
| PHYS-M01 | Broad phase 1: Dynamic AABB Tree | fat AABB(여유 마진), 증분 삽입(SAH 비용 기반 형제 선택)·삭제·트리 회전을 갖는 Box2D식 dynamic AABB tree를 구현해 1만 개 이동 물체에서 후보 쌍 수·갱신 비용을 GFX 트랙의 균일 그리드와 비교한다. | mac |
| PHYS-M02 | Broad phase 2: Sweep and Prune | 축 정렬 끝점 리스트를 삽입 정렬로 증분 갱신하는 SAP를 구현해 프레임 간 이동이 작은 장면에서 그리드 방식 대비 성능을 비교한다. | mac |
| PHYS-M03 | Narrow phase: SAT & 접촉 정보 | 2D 볼록 다각형과 3D 박스(15축) SAT를 구현해 접촉 법선·관통 깊이를 계산하고, 회전된 박스 쌍 랜덤 테스트로 brute-force 결과와 대조한다. | mac |
| PHYS-M04 | GJK: 볼록 형상 거리 & 교차 | 구·박스·캡슐·볼록 다면체의 support function과 GJK(simplex 갱신)로 거리/교차를 구현하고, SAT 결과와 비교·반복 횟수 통계·퇴화 케이스 처리를 테스트한다. | mac |
| PHYS-M05 | EPA & 접촉 매니폴드 | GJK 종료 simplex에서 EPA로 관통 법선·깊이를 구하고, 면 클리핑으로 박스-박스 최대 4점 접촉 매니폴드를 생성해 상자가 평면 위에 안정적으로 놓이는지 확인한다. | mac |
| PHYS-M06 | 충격량 기반 충돌 응답: 반발 & 마찰 | 회전을 포함한 법선 충격량(반발계수)과 마찰 원뿔 클램핑 충격량을 구현해 경사면 미끄러짐 각도가 tanθ=μ 이론과 일치하는지 검증한다. | mac |
| PHYS-M07 | Sequential Impulses 솔버 & warm starting | 누적 충격량 클램핑 sequential impulse 솔버에 warm starting을 추가해 박스 20단 쌓기의 안정성(지터, 붕괴)을 반복 횟수별로 비교한다. | mac |
| PHYS-M08 | 위치 보정: Baumgarte vs split impulse vs NGS | Baumgarte 안정화, split impulse(pseudo-velocity), non-linear Gauss-Seidel 위치 보정을 각각 구현해 쌓기·튀어오름 장면에서 관통 깊이와 에너지 증가를 비교한다. | mac |
| PHYS-M09 | 연속 충돌 검출 (CCD) | 빠른 탄환이 얇은 벽을 통과하는 터널링을 재현하고 conservative advancement(GJK 거리 기반) TOI 계산으로 해결, 서브스텝 방식과 비용·정확도를 비교한다. | mac |
| PHYS-A01 | 구속조건 & 관절: Jacobian 공식화 | 구속 Jacobian 형태로 거리·볼·힌지(각도 제한, 모터) 관절을 구현해 진자 체인·래그돌을 시뮬레이션하고, 구속 위반량(drift)을 시간에 따라 측정한다. | mac |
| PHYS-A02 | PBD/XPBD: 천 & 로프 | XPBD 거리·굽힘 구속으로 천과 로프를 구현해 반복 횟수/서브스텝에 따른 강성 의존성을 PBD와 비교하고, compliance로 물리적 강성을 설정할 수 있음을 보인다. | mac |
| PHYS-A03 | 솔버 안정성: 큰 질량비 & 강한 관절 | 1:1000 질량비 체인과 무거운 물체를 든 래그돌에서 sequential impulse 수렴 실패를 재현하고, 서브스테핑(Box2D v3 soft step), soft constraint(주파수·감쇠비 파라미터), 질량 스케일링 대책을 반복 횟수 대비 구속 오차로 비교한다(일반 stiff ODE 이론은 SIM 트랙). | mac |
| PHYS-A04 | 결정성: 부동소수점 & 재현 가능한 시뮬레이션 | 매 스텝 상태 해시를 기록하는 하네스로 -O0/-O2, -ffp-contract(FMA), 쌍 처리 순서, 멀티스레드 변경 시 발산 지점을 찾아내고, 결정성을 보장하는 규칙(정렬된 순회, FMA 고정 등)을 적용해 해시 일치를 확인한다. | mac |
| PHYS-A05 | Island 분할·sleeping·병렬 솔버 | union-find로 접촉 island를 분할하고 정지 island sleeping과 island 단위 병렬 풀이를 구현해, 1000개 물체 장면에서 CPU 시간과 결정성 유지 여부를 측정한다. | mac |
| PHYS-A06 | 다물체 동역학: Featherstone ABA | Articulated Body Algorithm을 구현해 2-링크 팔의 결과를 라그랑지안 해석식과 비교하고, n-링크에서 O(n) 확장성과 최대 좌표(maximal coordinate) 관절 방식과의 drift 차이를 보인다. | mac |
| PHYS-A07 | 미니 물리 엔진 통합 & 오픈소스 비교 | broad/narrow phase·솔버·관절·sleeping을 통합한 미니 엔진으로 피라미드·도미노 벤치마크를 돌려 스텝 시간 분해를 측정하고, Box2D v3/Jolt의 동일 단계 구현과 설계 차이를 문서화한다. | mac |

### GAME — 게임 엔진 구조 (Game Engine Architecture)

게임 엔진은 고정 프레임 예산 안에서 수만 개 객체를 처리하는 '소프트 실시간 시스템'으로, 메모리 아레나·잡 시스템·데이터 지향 설계·결정적 네트워킹 같은 기법이 임베디드·로봇 소프트웨어와 그대로 겹친다. 이 트랙은 게임 루프부터 ECS, 잡 시스템, 에셋 파이프라인, 롤백 넷코드까지 C++로 구현해 미니 엔진을 완성한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| GAME-B01 | 게임 루프: 가변 vs 고정 타임스텝 & 프레임 페이싱 | 가변 dt, 고정 dt, 고정 dt+보간 3가지 게임 루프를 구현해 인위적 프레임 지연 주입 시 시뮬레이션 결과·프레임 시간 분포를 비교하고, sleep vs busy-wait 페이싱의 정확도를 측정한다. | mac |
| GAME-B02 | 입력 처리 & 커맨드 패턴 | 키/패드 입력을 프레임 번호가 붙은 Command로 변환하는 입력 시스템을 구현하고, 커맨드 로그를 재생하면 동일한 게임 상태 해시가 나오는지 테스트한다. | mac |
| GAME-B03 | 이벤트 시스템: 옵저버 vs 지연 이벤트 큐 | 타입별 이벤트 버스를 즉시 호출 방식과 프레임 끝 일괄 처리 큐 방식으로 구현하고, 핸들러 안에서 구독/해제·이벤트 발행 시 발생하는 재진입 버그를 테스트로 재현·해결한다. | mac |
| GAME-B04 | 씬 그래프 & 트랜스폼 계층 | 로컬/월드 행렬을 갖는 씬 그래프와 dirty flag 지연 갱신을 구현해, 깊은 계층에서 루트 이동 시 재계산 횟수를 세고 재부모화(reparent) 시 월드 위치 보존을 테스트한다. | mac |
| GAME-B05 | 메모리 아레나: 선형·스택·프레임 할당자 | linear(bump), stack(marker 롤백), double-buffered frame 할당자를 구현하고 정렬 보장·overflow 감지 테스트, malloc 대비 할당 비용과 단편화를 측정한다. | mac |
| GAME-B06 | 오브젝트 풀 & generational handle | free-list 오브젝트 풀과 (index, generation) 핸들을 갖는 slot map을 구현해 삭제 후 재사용된 슬롯을 옛 핸들로 접근하면 안전하게 실패함을 테스트한다. | mac |
| GAME-B07 | 리소스 매니저: 참조 카운트 에셋 캐시 | 경로 해시 키 기반 리소스 캐시(핸들 + 참조 카운트, 마지막 해제 시 언로드)를 구현하고 중복 요청 시 1회 로딩, 순환 의존 에셋 처리를 테스트한다. | mac |
| GAME-B08 | 게임 오브젝트 행동: 계층적 상태 머신 | enter/exit/update 훅과 상위 상태 위임을 갖는 계층적 상태 머신으로 적 AI(순찰/추적/공격/피격)를 구현하고, 전이 테이블 테스트로 상태 전이 순서를 검증한다. | mac |
| GAME-M01 | ECS 1: sparse set 기반 (EnTT 스타일) | 컴포넌트 타입별 sparse set 저장소, 엔티티 생성/삭제(generation), 다중 컴포넌트 view 순회를 구현해 순회 중 삭제 안전성을 테스트하고 OOP(가상 함수) 구현과 10만 엔티티 갱신 시간을 비교한다. | mac |
| GAME-M02 | ECS 2: 아키타입 기반 & 쿼리 | 컴포넌트 조합별 청크(SoA) 저장, 컴포넌트 추가/제거 시 아키타입 이동, 쿼리 캐시를 구현해 sparse set ECS와 순회 속도·구조 변경 비용을 벤치마크로 비교한다. | mac |
| GAME-M03 | 잡 시스템: 잡 그래프 & 카운터 의존성 | OS 트랙의 work-stealing 스레드풀 위에 잡 카운터 의존성, parallel_for(청크 크기 자동), 연속 잡, 메인 스레드 전용 잡을 갖는 잡 API를 구현해 애니메이션→물리→렌더 준비 그래프를 실행하고 코어 수별 확장성과 청크 크기 영향을 측정한다. | mac |
| GAME-M04 | ECS 시스템 스케줄러: 접근 선언 기반 자동 병렬화 | 각 시스템이 Read<Position>/Write<Velocity> 형태로 접근을 선언하면 충돌 그래프를 만들어 병렬 실행 단계를 계산하는 스케줄러를 잡 시스템 위에 구현하고, 순차 실행과 결과 해시 일치·프레임 시간 단축을 확인한다(AoS/SoA 레이아웃 측정은 DS 트랙). | mac |
| GAME-M05 | Fiber 기반 잡 시스템 (Naughty Dog 방식) | AArch64 어셈블리로 callee-saved 레지스터·스택 포인터를 저장/복원하는 컨텍스트 스위치를 작성해 fiber 풀을 만들고, 잡 안에서 다른 잡 완료를 기다릴 때 스레드가 놀지 않음을 측정한다. | mac |
| GAME-M06 | 에셋 파이프라인: 오프라인 쿠킹 & 재배치 가능 바이너리 | OBJ/PNG를 엔진 전용 바이너리(헤더 버전, 오프셋 기반 포인터 fixup, 정렬)로 쿠킹하는 도구와 mmap 로더를 구현해 텍스트 파싱 대비 로딩 시간을 비교하고 버전 불일치를 감지한다. | mac |
| GAME-M07 | 스크립팅 통합: 컴포넌트 바인딩 & 핫 리로드 | Lua(또는 VM 트랙의 자체 VM)에 엔티티·컴포넌트 접근 API를 바인딩하고, 파일 변경 감지 시 스크립트를 핫 리로드하면서 상태를 보존하는 흐름을 구현·테스트한다. | mac |
| GAME-M08 | C++ 게임 로직 핫 리로드 (dylib) | 게임 로직을 dylib로 분리하고 dlopen/dlsym로 재로딩하되 상태는 호스트 메모리에 유지하는 구조를 구현해, 재빌드 후 실행 중인 게임에 변경이 반영되고 함수 포인터 dangling이 없는지 확인한다. | mac |
| GAME-M09 | 프로파일러: 계층 스코프 타이머 & Chrome trace | 스레드별 lock-free 이벤트 버퍼에 스코프 시작/끝을 기록하는 RAII 프로파일러를 만들어 Chrome trace JSON으로 출력, Perfetto에서 16.6ms 프레임의 병목을 찾아 개선한다. | mac |
| GAME-A01 | 네트워킹 1: UDP 위 신뢰성 계층 | 시퀀스 번호·ack 비트필드·RTT 추정·선택적 재전송을 갖는 UDP 프로토콜을 구현하고 손실/지연/재정렬 주입 프록시로 신뢰 메시지 전달과 지연 통계를 검증한다. | mac |
| GAME-A02 | 네트워킹 2: 결정적 락스텝 | 입력 지연 버퍼를 둔 P2P 락스텝을 구현하고 매 틱 상태 체크섬을 교환해 desync를 감지, 의도적 비결정성(정렬 안 된 순회)을 주입해 감지되는지 확인한다. | mac |
| GAME-A03 | 네트워킹 3: 롤백 넷코드 (GGPO 스타일) | 상태 스냅샷 링 버퍼, 원격 입력 예측, 늦은 입력 도착 시 롤백·재시뮬레이션을 구현해 50~150ms 지연/지터 환경에서 롤백 프레임 수와 재시뮬레이션 비용을 측정한다. | mac |
| GAME-A04 | 클라이언트 예측 & 서버 보정 & 엔티티 보간 | 권위 서버, 클라이언트 측 예측 + 서버 상태 수신 시 재조정(reconciliation), 원격 엔티티 보간을 구현해 지연별 위치 오차와 보정 튐(snap)을 측정한다. | mac |
| GAME-A05 | 비동기 스트리밍 로딩 & 메모리 예산 | 우선순위 I/O 스레드, 거리 기반 로드/언로드, 메모리 예산 초과 시 축출 정책을 갖는 스트리밍 시스템을 구현해 이동 시 프레임 스파이크와 메모리 사용량을 추적한다. | mac |
| GAME-A06 | 메모리 추적: 태그별 리포트 & 단편화 분석 | 할당자에 태그(렌더/물리/오디오)와 콜스택 샘플링을 추가해 태그별 사용량·피크·누수 리포트를 출력하고, 장시간 실행 단편화를 측정해 풀/아레나 재배치로 개선한다. | mac |
| GAME-A07 | 미니 엔진 통합 & 프레임 예산 분석 | ECS + 잡 시스템 + PHYS 물리 + 소프트웨어/Metal 렌더러를 통합해 1만 엔티티 장면을 60fps로 돌리고, 프로파일러로 서브시스템별 시간을 분해해 예산 초과 항목의 최적화 결과를 보고한다. | mac |

### LLAT — 저지연 시스템 (Low-Latency Systems)

저지연 시스템은 '평균'이 아니라 p99·p99.9 꼬리 지연을 다루는 엔지니어링으로, HFT·로봇 제어·네트워크 장비·펌웨어 모두 같은 원리(캐시, 메모리 오더링, lock-free, 할당 제거, 코어 격리)를 쓴다. 이 트랙은 측정법부터 lock-free 큐, 오더북, 커널 바이패스까지 C++로 구현·계측해 quant SWE와 실시간 펌웨어 면접을 동시에 대비한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| LLAT-B01 | 시간 측정: 클럭 소스와 오버헤드 | steady_clock, mach_absolute_time, clock_gettime_nsec_np, AArch64 cntvct_el0 직접 읽기(inline asm, isb 포함)의 해상도·호출 오버헤드·단조성을 측정하고, x86 rdtsc/rdtscp와의 차이(불변 TSC, 직렬화)를 정리한다. | mac |
| LLAT-B02 | 지연 분포: 히스토그램 & 백분위수 | HdrHistogram 방식(로그 버킷 + 선형 서브버킷) 히스토그램을 구현해 p50/p99/p99.9/max를 출력하고, coordinated omission이 있는 측정 루프와 보정 루프의 결과 차이를 보인다. | mac |
| LLAT-B03 | 마이크로벤치마크 방법론 | Google Benchmark로 같은 함수를 측정하며 DoNotOptimize/ClobberMemory 누락 시 결과가 0이 되는 사례, 워밍업·반복 횟수·P/E 코어 이동에 의한 분산을 재현하고 측정 체크리스트를 만든다. | mac |
| LLAT-B04 | Cold path 제거: 프리폴트·mlock·캐시 워밍 | hot path 버퍼를 사전 터치·mlock하고 캐시·분기 예측기를 워밍하는 초기화 루틴을 만들어 첫 1000개 요청의 지연 분포를 전후 비교하고, 남은 지연 원인을 정리한다(포인터 체이싱 지연 측정은 see:MEM-B10). | mac |
| LLAT-B05 | hot path 데이터 레이아웃: hot/cold 분리 & 패딩 | 주문 구조체를 hot 필드(가격·수량·id)와 cold 필드로 분리하고 offsetof/static_assert로 캐시 라인 배치를 고정, 처리 루프의 캐시 미스와 처리량 변화를 측정한다. | mac |
| LLAT-B06 | atomic 연산 비용: relaxed·acq_rel·seq_cst·CAS 경합 | fetch_add(각 memory_order)·CAS 루프·LSE 원자 명령(LDADD)의 단일 스레드 지연과 1~8스레드 경합 시 처리량을 M2에서 측정해 표로 만들고, hot path에서 원자 연산을 줄이는 기법을 정리한다(false sharing은 see:ARCH-M08). | mac |
| LLAT-B07 | 분기 예측 & branchless 코드 | 정렬/비정렬 데이터 필터 벤치마크로 미예측 비용을 측정하고, csel 유도 branchless 버전·[[likely]]·룩업 테이블로 개선해 생성 어셈블리(-S)와 함께 비교한다. | mac |
| LLAT-B08 | 할당 없는 hot path | operator new/malloc 후킹 카운터를 만들어 메시지 처리 루프의 할당을 찾아내고, 사전할당·객체 풀·고정 용량 컨테이너·string 대신 고정 버퍼로 교체해 할당 0회와 p99 개선을 보인다. | mac |
| LLAT-M01 | SPSC 큐 지연 튜닝: 캐시된 인덱스·패딩·배치 | DS 트랙의 C11 SPSC 큐를 C++로 옮긴 기준선에서 시작해 인덱스 간 128B 패딩, 상대 인덱스 캐싱(rigtorp 방식), 배치 push/pop, 슬롯 내 시퀀스 플래그 방식을 하나씩 적용하며 처리량과 왕복 지연 p99 변화를 표로 기록한다. | mac |
| LLAT-M02 | Seqlock: 단일 writer 스냅샷 공유 | 시장 데이터 스냅샷용 seqlock을 C++ 메모리 모델에 맞게(atomic 시퀀스, fence, 데이터 복사 방식) 구현하고 torn read 검출 스트레스 테스트와 rwlock 대비 reader 지연을 측정한다. | mac |
| LLAT-M03 | lock-free 메모리 회수: Hazard Pointer | hazard pointer 기반 회수를 구현해 lock-free 연결 리스트(또는 Michael-Scott 큐)에 적용하고 ASan으로 use-after-free가 없음을 확인, OS 트랙의 epoch 기반 회수와 읽기 경로 지연·메모리 상한을 비교한다(MPMC 큐 구현 자체는 DS 트랙). | mac |
| LLAT-M04 | LMAX Disruptor 패턴 구현 | 링 버퍼 + sequencer + 다단계 consumer(journal→replicate→business logic) 의존 barrier를 구현하고 배치 소비가 처리량·지연에 미치는 효과를 측정한다. | mac |
| LLAT-M05 | 스레드 간 핸드오프 지연 (ping-pong) | 두 스레드가 캐시 라인 하나로 ping-pong하는 벤치마크로 왕복 지연을 측정하고, macOS QoS 클래스(P/E 코어 유도)별 차이를 기록, Linux에서의 pthread_setaffinity_np 실험 계획과 비교한다. | mac |
| LLAT-M06 | Busy polling vs blocking 대기 | busy spin(yield/wfe 포함), condition_variable, atomic::wait, 하이브리드(spin-then-park) 대기를 구현해 이벤트→처리 시작 지연 히스토그램과 CPU 사용률을 비교하고 인터럽트 vs 폴링 펌웨어 설계와 연결한다. | mac |
| LLAT-M07 | CPU 격리 & 피닝: isolcpus·nohz_full·IRQ affinity | Linux VM에서 isolcpus/nohz_full/rcu_nocbs 부팅 옵션과 IRQ affinity를 설정하고, 격리 전후 busy loop의 인터럽션 지연(hiccup) 히스토그램을 측정한다. | **linux** |
| LLAT-M08 | 오더북 1: 가격 레벨 & FIFO 매칭 | add/cancel/modify/execute를 지원하는 가격-시간 우선 오더북을 구현(가격 레벨 맵 + intrusive 리스트 + 주문 id 해시로 O(1) 취소)하고, 랜덤 주문 시퀀스로 참조 구현과 체결 결과를 대조한다. | mac |
| LLAT-M09 | 오더북 2: 캐시 친화 최적화 & ITCH 리플레이 | ITCH 5.0 형식 메시지 생성기(또는 샘플 파일)로 피드를 리플레이하며, 가격 틱 인덱스 배열·풀 할당·hot/cold 분리를 적용해 메시지당 처리 지연 p50/p99를 단계별로 개선·기록한다. | mac |
| LLAT-A01 | 이진 프로토콜 제로카피 디코딩 (SBE 스타일) | 고정 레이아웃 이진 메시지(엔디언, 정렬 안 된 필드)를 std::bit_cast/memcpy 기반 flyweight 뷰로 디코딩하는 코드 생성기 수준 API를 만들고, 구조체 복사·파싱 방식 대비 ns/메시지를 측정한다. | mac |
| LLAT-A02 | 커널 바이패스: DPDK 폴링 모드 | Linux VM에서 DPDK를 빌드해 net_tap/af_packet 가상 디바이스로 testpmd와 간단한 rx→처리→tx 폴링 루프를 실행하고, 일반 소켓 recv 루프와 패킷당 지연·CPU 구조를 비교 정리한다. | **linux** |
| LLAT-A03 | io_uring 기반 저지연 I/O | liburing으로 UDP echo 서버를 epoll 버전과 io_uring(일반/SQPOLL/registered buffer) 버전으로 구현해 요청당 지연 히스토그램과 syscall 수를 비교한다. | **linux** |
| LLAT-A04 | 타임스탬핑: HW vs SW, SO_TIMESTAMPING, PTP 개념 | SO_TIMESTAMPING으로 소프트웨어 rx/tx 타임스탬프를 수집해 애플리케이션 시각과의 차이를 측정하고, NIC 하드웨어 타임스탬프·PTP(linuxptp) 동기 구조를 정리해 거래소/센서 퓨전 적용 사례와 연결한다. | **linux** |
| LLAT-A05 | 꼬리 지연 원인 분석: page fault·TLB·인터럽트 | hot loop에 의도적으로 첫 접근 page fault, 대용량 작업 집합(TLB 미스), 타이머 인터럽트를 유발해 지연 스파이크를 관찰하고 mlockall·prefault·huge page·perf/ftrace로 원인 귀속 후 개선한다. | **linux** |
| LLAT-A06 | 저지연 비동기 로거 (NanoLog 스타일) | 포맷 문자열 id + 원시 인자만 SPSC 버퍼에 기록하고 백그라운드 스레드가 포맷팅하는 바이너리 로거를 구현해 printf/spdlog 대비 호출당 지연과 버퍼 가득 참 정책(drop/block)을 측정한다. | mac |
| LLAT-A07 | Tick-to-trade 파이프라인 설계 & 지연 예산 | 피드 디코더→오더북→단순 전략→주문 인코더를 단일 스레드/2스레드(SPSC) 구성으로 구현하고 단계별 타임스탬프로 지연 예산을 분해, 구성별 p99 tick-to-trade를 비교해 설계 문서로 정리한다. | mac |

### STOR — 스토리지 엔진 · DB (Storage Engines & Databases)

스토리지 엔진은 페이지·버퍼 풀·B+tree·LSM·WAL·MVCC로 '전원이 꺼져도 데이터가 맞는' 시스템을 만드는 분야로, SSD/FTL 펌웨어 경력과 가장 직접적으로 이어진다. 이 트랙은 C++로 KV 스토어를 처음부터 쌓아 올리고, 플래시 인지 설계·LittleFS·SQLite 내부까지 분석해 스토리지/DB/펌웨어 면접을 함께 대비한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| STOR-B01 | 내구성 계층: fsync·F_FULLFSYNC·디바이스 쓰기 캐시 | 4KB 레코드 append를 write만, fsync, fdatasync 상당, fcntl(F_FULLFSYNC), F_BARRIERFSYNC로 각각 수행해 지연 분포와 초당 커밋 수를 측정하고, NVMe volatile write cache·FUA·flush 명령과 대응시켜 '어디까지 살아남는가' 표를 작성한다(원자 교체 패턴은 OS 트랙). | mac |
| STOR-B02 | 슬롯 페이지 레이아웃 | 4KB 슬롯 페이지(헤더, 슬롯 배열, 뒤에서 자라는 레코드 영역)에 insert/delete/update/compact를 구현하고, 무작위 연산 퍼즈 테스트로 참조 map과 내용 일치를 검증한다. | mac |
| STOR-B03 | 레코드 체크섬 & torn write 감지 | 페이지/레코드에 CRC32C(+ 버전, LSN)를 붙이는 포맷을 구현하고, 임의 위치 512B 단위 torn write·비트 플립 주입 시 100% 감지됨을 테스트한다. | mac |
| STOR-B04 | 버퍼 풀 매니저: pin/dirty & LRU/Clock | 페이지 테이블·pin count·dirty flag·LRU와 Clock 교체를 갖는 버퍼 풀을 구현하고, 스캔/핫셋 워크로드에서 hit ratio를 비교, pin된 페이지가 축출되지 않음을 테스트한다. | mac |
| STOR-B05 | Append-only 로그 KV (Bitcask) | append-only 데이터 파일 + 메모리 keydir + 병합(compaction)·hint 파일을 갖는 Bitcask KV를 구현하고, 재시작 시 keydir 복구와 컴팩션 후 공간 회수를 테스트한다. | mac |
| STOR-B06 | 디스크 해시 인덱스: Extendible Hashing | 디렉터리 배가·버킷 분할을 갖는 extendible hashing을 버퍼 풀 위에 구현하고, 편향 키 분포에서 버킷 이용률과 디렉터리 크기를 측정한다. | mac |
| STOR-B07 | LSM용 필터 선택: blocked Bloom vs Cuckoo filter | DS 트랙의 기본 블룸 필터를 출발점으로 캐시 라인 blocked Bloom과 cuckoo filter를 구현해 bits/key별 실측 FPR·조회 ns·삭제 지원 여부를 비교하고, SSTable 필터 블록에 어떤 것을 쓸지 결정 근거를 작성한다. | mac |
| STOR-M01 | B+tree 1: 버퍼 풀 위 디스크 B+tree & 삭제 | DS 트랙의 페이지 단위 B+tree 노드를 슬롯 페이지로 직렬화해 버퍼 풀(pin/unpin) 위에 올리고 삭제 시 재분배/병합을 구현, 프로세스 재시작 후 내용 보존과 불변식 검사기(정렬·채움률·높이 균일) 통과를 테스트한다. | mac |
| STOR-M02 | B+tree 2: 범위 스캔·prefix 압축·bulk load | 리프 체인 범위 반복자, 키 prefix 압축, 정렬 입력 bulk loading을 구현해 트리 높이·페이지 수·로딩 시간을 일반 삽입과 비교한다. | mac |
| STOR-M03 | WAL: 로그 레코드·LSN·group commit | LSN이 붙은 redo/undo 로그 레코드와 'page LSN ≤ flushed LSN' WAL 규칙을 구현하고, 여러 스레드 커밋을 묶는 group commit으로 fsync 횟수 대비 처리량 향상을 측정한다. | mac |
| STOR-M04 | 크래시 복구: ARIES & 크래시 주입 테스트 | fuzzy checkpoint와 ARIES 3단계 복구(CLR 포함)를 구현하고, 임의 시점 강제 종료·쓰기 일부 유실을 시뮬레이션하는 블록 디바이스 래퍼로 수천 회 크래시 테스트를 돌려 커밋된 트랜잭션만 남는지 확인한다. | mac |
| STOR-M05 | LSM 1: memtable(skiplist) & WAL | skiplist memtable + WAL 쓰기 경로와 크기 임계값에 따른 immutable memtable 전환을 구현하고, 크래시 후 WAL 재생으로 memtable이 복구되는지 테스트한다. | mac |
| STOR-M06 | LSM 2: SSTable 포맷 & 읽기 경로 | 데이터 블록(prefix 압축, restart point)·인덱스 블록·블룸 필터 블록·footer로 SSTable을 기록하고, memtable→L0→Ln 조회와 병합 반복자(k-way merge, tombstone 처리)를 구현해 참조 map과 대조한다. | mac |
| STOR-M07 | LSM 3: 컴팩션 & 증폭 측정 | leveled와 size-tiered 컴팩션을 구현해 동일 워크로드(쓰기 위주/읽기 위주/업데이트 위주)에서 write/read/space amplification을 측정해 표로 비교하고 선택 기준을 정리한다. | mac |
| STOR-M08 | 동시성 제어: 2PL & 데드락 감지 | 공유/배타 lock manager와 strict 2PL, wait-for 그래프 데드락 감지·희생자 선택을 구현하고, 은행 이체 동시 트랜잭션 테스트로 총액 불변식을 검증한다. | mac |
| STOR-M09 | MVCC & 스냅샷 격리 | 버전 체인·트랜잭션 타임스탬프 기반 스냅샷 격리 MVCC(first-committer-wins)와 버전 GC를 구현하고, write skew 이상 현상을 재현해 SSI 또는 명시적 잠금으로 막는다. | mac |
| STOR-M10 | B+tree 동시성: latch crabbing & optimistic | pessimistic latch crabbing과 optimistic(리프만 쓰기 래치, 실패 시 재시도) 방식을 구현해 멀티스레드 삽입/조회 처리량을 비교하고 TSan으로 경합을 검증한다. | mac |
| STOR-A01 | Flash 인지 스토리지: 앱 WA × FTL WA | 페이지 매핑·greedy GC·오버프로비저닝을 갖는 FTL 시뮬레이터 위에 B+tree(in-place 갱신)와 LSM(순차 쓰기) 엔진의 I/O 트레이스를 재생해 애플리케이션 WA와 FTL WA의 곱(end-to-end WA)을 비교하고 TRIM 효과를 측정한다. | mac |
| STOR-A02 | ZNS 친화 LSM 배치 | zone(append-only, reset 단위) 디바이스 모델을 구현하고 SSTable 수명(레벨) 기준 zone 할당 전략을 비교해 zone reset 시 유효 데이터 이동량을 측정, ZenFS 설계와 대조한다. | mac |
| STOR-A03 | LittleFS 내부: 메타데이터 쌍·CoW·웨어 레벨링 | littlefs를 RAM 블록 디바이스 에뮬레이터로 Mac에서 빌드해 임의 시점 전원 차단(쓰기 중단) 주입 테스트를 수천 회 돌리고, 메타데이터 쌍·CTZ skip-list·블록 마모 분포를 관찰해 설계 문서와 대조한다. | mac |
| STOR-A04 | SQLite 내부: 파일 포맷 직접 파싱 | sqlite 라이브러리 없이 .db 파일 헤더·B-tree 페이지(테이블/인덱스, 내부/리프)·varint·record 포맷·overflow 페이지를 파싱해 테이블 전체를 덤프하는 도구를 작성하고 sqlite3 CLI 결과와 비교, WAL 모드 파일 구조도 확인한다. | mac |
| STOR-A05 | 펌웨어 저장 계층 선택: FatFs vs LittleFS vs raw KV | 같은 NOR/NAND 에뮬레이터 위에서 FatFs, LittleFS, DS 트랙의 로그 구조 KV에 동일한 설정 저장·로그 append 워크로드를 돌려 전원 차단 주입 후 손상률, 블록별 마모 분포, RAM/코드 크기, 쓰기 증폭을 비교하고 용도별 선택 가이드를 작성한다. | mac |
| STOR-A06 | I/O 경로: 직접 I/O·정렬·큐 깊이 | macOS F_NOCACHE(Linux O_DIRECT 대응) 정렬 I/O와 스레드 기반 비동기 I/O로 큐 깊이 1~32에서 4KB 랜덤 읽기 IOPS/지연을 측정하고 NVMe 큐 구조와 연결해 해석한다. | mac |
| STOR-A07 | 미니 RocksDB 통합 & db_bench | WAL·memtable·SSTable·컴팩션·블룸 필터·복구를 통합한 KV 엔진에 db_bench 유사 도구(fillseq/fillrandom/readrandom/seekrandom)를 붙여 성능을 측정하고 병목 프로파일 결과와 개선 내역을 보고한다. | mac |
| STOR-A08 | 질의 실행: Volcano vs 벡터화 | scan·filter·projection·hash join·aggregate 연산자를 Volcano(tuple-at-a-time)와 벡터화(batch) 모델로 구현해 동일 질의에서 처리 시간과 함수 호출·분기 수를 비교한다. | mac |

## 10단계 · 칩 설계 · 검증 (Chip Design & Verification)

### RISCV — RISC-V ISA · 특권 구조 (RISC-V ISA & Privileged Arch)

RISC-V는 Tenstorrent·SiFive 기반 NPU·Rivos 등 AI 가속기의 제어/관리 코어와 마이크로컨트롤러에 빠르게 퍼지고 있는 개방형 ISA다. 이 트랙은 ARM 트랙과 겹치는 일반 ISA 기초 대신 RISC-V 고유의 인코딩·확장 구조, M/S/U 특권 모드·CSR·트랩·PLIC/AIA·PMP·Sv39·SBI, RVV 벡터와 커스텀 명령어를 '직접 ISS를 만들고 QEMU virt에서 돌려보며' 익힌다. 칩 회사 면접에서 자주 나오는 '트랩 때 하드웨어가 정확히 무엇을 하나', 'PMP와 MMU의 관계', 'LR/SC 규칙' 같은 질문에 비트 수준으로 답하는 것이 목표다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| RISCV-B01 | RV32I 명령어 포맷: R/I/S/B/U/J 인코딩과 즉시값 디코드 | 32비트 워드를 받아 포맷·opcode·rd/rs1/rs2·funct3/funct7·부호확장 즉시값을 출력하는 C 디코더를 작성하고, 스펙 표에서 옮긴 골든 인코딩 40개(분기 음수 오프셋, JAL ±1MiB 경계 포함)와 대조하는 테스트를 만든다 (ARM의 조건 필드·시프트 피연산자 인코딩과의 차이를 표로 정리, 토이 ISA 설계는 see:ARCH-B08). | mac |
| RISCV-B02 | 미니 어셈블러: 레이블·의사명령(li, la, call, ret) 확장 | RV32I 텍스트 어셈블리를 기계어 hex로 바꾸는 2-pass Python 어셈블러(레이블, .word/.data, li/la/mv/j/call/ret/nop 확장)를 작성하고, RISCV-B01 디코더로 round-trip 디스어셈블해 원문과 일치함을 검증한다. | mac |
| RISCV-B03 | 상수·주소 만들기: LUI/AUIPC와 %hi/%lo의 +0x800 보정 | 임의 32비트 상수를 LUI+ADDI로 만드는 li 확장 함수를 +0x800 보정 없이/있이 두 버전으로 작성해 0x00000800, 0x7FFFF800, 0xFFFFFFFF 등에서 버그를 재현하고, AUIPC+ADDI/JALR 쌍으로 ±2GiB PC 상대 주소를 만드는 코드를 RISCV-B02에 추가한다 (ARM MOVZ/MOVK·ADRP 방식과 비교, see:ARM-B03/A09). | mac |
| RISCV-B04 | RV32I 명령어 집합 시뮬레이터(ISS) 1: fetch-decode-execute | RV32I 기본 37개 명령(FENCE/ECALL/EBREAK 포함)을 실행하는 C ISS(평면 메모리, ecall a7=93으로 종료, 명령 카운트)를 구현하고, RISCV-B02로 어셈블한 fib·버블 정렬·memcpy 프로그램 결과를 C 기준 구현과 대조한다 (토이 CPU는 see:ARCH-B09). | mac |
| RISCV-B05 | 플래그 없는 ISA: SLT/SLTU로 캐리·오버플로·비교 구현 | RV32에서 64비트 덧셈/뺄셈(SLTU로 캐리), 부호 있는 덧셈 오버플로 검출, branchless min/max/abs를 어셈블리로 작성해 ISS에서 경계값 테스트하고, ARM ADDS/ADC/CSEL 버전(see:ARM-B05/B07)과 명령 수를 비교한다. | mac |
| RISCV-B06 | 로드/스토어: 부호 확장, 정렬, misaligned 접근 정책 | ISS의 로드/스토어에 misaligned 정책 3가지(허용/트랩/트랩 후 바이트 단위 에뮬레이션)를 옵션으로 넣고, 패킷 헤더 파서(비정렬 uint32 필드)를 각 정책으로 실행해 결과·소요 명령 수를 비교한다. | mac |
| RISCV-B07 | RISC-V psABI 호출 규약: ra/sp/a0–a7/s0–s11와 스택 프레임 | 재귀 factorial, 인자 10개 함수(스택 전달), 구조체 반환 함수를 어셈블리로 작성하고, ISS에 s 레지스터·sp를 호출 전후 비교하는 ABI 검사 하네스를 넣어 위반을 잡는다 (AAPCS64와 레지스터 역할 대응표 작성, see:ARM-M01). | mac |
| RISCV-B08 | RV64I: W 명령(ADDW/SLLIW)과 32비트 값 부호 확장 규칙 | ISS를 XLEN=64로 확장(LD/SD/LWU, *W 명령)하고, uint32_t 덧셈·시프트·비교를 *W 명령 유무로 컴파일한 시퀀스를 손으로 작성해 0x80000000 근처 값에서 결과가 갈리는 경우를 테스트로 보인다. | mac |
| RISCV-B09 | M 확장: MULH 계열과 0 나누기가 트랩하지 않는 DIV/REM | ISS에 M 확장을 추가해 __int128 기준 랜덤 테스트로 MULH 3종을 검증하고, ÷0·오버플로 결과를 x86(SIGFPE)·ARM SDIV(0 반환)와 비교한 표를 만든 뒤 펌웨어에서 ÷0 검사를 어디에 둬야 하는지 정리한다 (see:ARM-B07). | mac |
| RISCV-B10 | C 확장(RVC): 16비트 압축 명령 디코드와 혼합 길이 fetch | C.LW/C.SW/C.ADDI/C.J/C.BEQZ/C.LWSP 등 RVC 명령을 32비트 등가로 확장하는 expander를 ISS 앞단에 붙이고, 16/32비트가 섞인 명령 스트림과 4바이트 경계를 걸치는 32비트 명령 fetch를 테스트한다 (Thumb-2와 비교, see:ARM-B12). | mac |
| RISCV-B11 | 컴파일러 출력 읽기: RISC-V gcc/clang -O2 관용구 | 루프·switch·구조체 접근·64비트 곱셈 등 C 함수 6개를 riscv64 -O0/-O2(-march=rv64gc vs rv64gc_zba_zbb)로 컴파일해 objdump 출력에 명령별 주석을 달고, 같은 함수의 AArch64 출력(see:ARM-B02)과 명령 수·코드 크기를 표로 비교한다. | **qemu** |
| RISCV-B12 | QEMU virt 베어메탈 첫 부팅: 링커 스크립트·start.S·UART | -bios none으로 0x80000000에 올라가는 start.S(sp 설정, hart 0 외 WFI 파킹, .bss 제로화)와 링커 스크립트, 0x10000000 UART putc를 작성해 Hello를 출력하고, gdb로 reset 직후 a0/a1 값과 mhartid를 확인한다 (QEMU 디버그 일반은 see:EMU-B08, startup 일반은 see:BM-B07). | **qemu** |
| RISCV-M01 | ISS 적합성 검증: riscv-tests ELF 로더와 tohost | ISS에 ELF32/64 로더와 tohost 심볼 감시를 추가하고 riscv-tests의 rv32ui/rv32um/rv64ui -p 테스트를 빌드해 전부 실행, 실패 테스트는 TEST_CASE 번호(gp)로 원인 명령을 찾아 고친 통과표를 만든다. | **qemu** |
| RISCV-M02 | Zba/Zbb/Zbs 비트 조작 확장 | ISS에 Zba/Zbb/Zbs를 구현해 C 기준 함수와 랜덤 대조하고, 배열 인덱싱·레지스터 필드 조작·popcount 루틴을 rv32i 버전과 확장 버전으로 작성해 동적 명령 수를 비교한다 (ARM UBFX/BFI/RBIT/CLZ 대응, see:ARM-B06). | mac |
| RISCV-M03 | F/D 확장: fcsr 반올림 모드·예외 플래그와 NaN-boxing | ISS에 FADD.S/FMUL.S/FMADD.S/FCVT.W.S/FCVT.S.W/FMV/FCLASS 부분집합을 호스트 float + fenv로 구현해 5개 반올림 모드와 NX/OF/NV 플래그를 테스트 벡터로 검증하고, NaN-boxing 위반 입력이 canonical NaN으로 읽히는지 확인한다 (see:ARM-B11). | mac |
| RISCV-M04 | A 확장: LR/SC 예약과 AMO로 락·CAS 구현 | ISS를 2-hart 라운드로빈 인터리빙으로 확장하고 LR/SC 예약(다른 hart의 스토어 시 무효화)과 AMO를 구현한 뒤, AMOSWAP 스핀락·LR/SC CAS·ticket lock 어셈블리를 랜덤 스케줄 스트레스로 검증한다 (ARM LDXR/STXR·LSE와 비교, see:ARM-M04). | mac |
| RISCV-M05 | Zicsr와 M-mode CSR 파일: WARL 필드와 원자적 RMW | ISS에 mstatus/misa/mtvec/mepc/mcause/mtval/mie/mip/mscratch/mhartid/mcycle CSR 파일을 WARL 마스크와 함께 구현하고, 읽기 전용 CSR 쓰기·권한 부족 접근이 illegal instruction이 되는지와 csrr의 부작용 없음을 테스트 프로그램으로 검증한다. | mac |
| RISCV-M06 | 트랩 흐름 구현: 예외 진입, mstatus 스택, mret | ISS에 ecall/ebreak/illegal/misaligned/access fault 예외와 mret을 구현하고, 핸들러가 ecall이면 mepc+=4로 복귀하는 테스트와 vectored 모드 인터럽트 오프셋(4×cause) 테스트를 작성한다 (Cortex-M 자동 스태킹·AArch64 ELR/SPSR 방식과 비교, see:ARM-M09/M13). | mac |
| RISCV-M07 | 트랩 핸들러 어셈블리: mscratch 스왑과 컨텍스트 저장 | QEMU virt M-mode에서 31개 GPR을 트랩 프레임에 저장/복원하는 진입부와 mcause를 디코딩하는 C 핸들러를 작성하고, ecall 시스템콜 디스패치와 미구현 명령(예: MUL on rv32i 가정)을 소프트웨어 에뮬레이션해 복귀하는 경로를 gdb로 검증한다. | **qemu** |
| RISCV-M08 | CLINT 타이머 인터럽트: mtime/mtimecmp와 경쟁 없는 갱신 | QEMU virt CLINT(0x2000000)로 10ms 주기 틱과 틱 카운터를 구현하고, RV32 빌드에서 hi에 -1을 먼저 쓰는 순서와 순진한 순서의 차이를 설명하는 테스트(인위적 지연 삽입)를 만든 뒤 msip으로 hart 간 IPI를 보낸다 (Sstc stimecmp 차이도 정리). | **qemu** |
| RISCV-M09 | PLIC 외부 인터럽트: priority·threshold·claim/complete | PLIC 동작을 C 모델(소스 32개, 컨텍스트 2개)로 구현해 claim 순서·threshold 마스킹을 테스트하고, QEMU virt에서 UART RX(IRQ 10)를 PLIC로 받아 링버퍼에 넣는 인터럽트 구동 에코를 작성한다 (Cortex-M NVIC·GIC와 비교, see:ARCH-M09, see:BM-M01). | **qemu** |
| RISCV-M10 | 특권 모드 전환과 위임: M→S→U, medeleg/mideleg | M-mode 모니터가 PMP 전체 허용 항목을 설정하고 mret으로 S-mode 커널에 진입, 커널이 sret으로 U-mode 태스크를 실행하게 만든 뒤 U ecall·페이지 폴트는 S로 위임하고 S ecall은 M이 받도록 medeleg/mideleg를 설정해 각 트랩의 도착 모드를 로그로 검증한다. | **qemu** |
| RISCV-M11 | PMP: TOR/NA4/NAPOT 인코딩과 접근 검사기 | (base, size)를 NAPOT pmpaddr로 바꾸는 인코더와 (주소, 크기, 접근유형, 모드)를 받아 허용/폴트를 반환하는 PMP 검사기를 C로 작성해 걸침 접근·L 비트·매칭 없음(U/S는 거부, M은 허용) 테스트를 통과시키고, Cortex-M MPU 영역 규칙(see:ARCH-M15, see:BM-A08)과 차이를 정리한다. | mac |
| RISCV-M12 | Sv39/Sv48 페이지 테이블 워커: PTE 비트·슈퍼페이지·A/D | 레벨 수를 파라미터로 받는 Sv39/Sv48 워커를 C로 작성해 4KiB/2MiB/1GiB 매핑, misaligned superpage, W=1 R=0 예약 조합, U 페이지에 S-mode 접근(SUM)과 A/D 소프트웨어 관리 시나리오별 page fault 원인(scause 12/13/15)을 출력하는 테스트를 만든다 (일반 워커는 see:ARCH-A01, xv6 COW는 see:OS-A10). | mac |
| RISCV-M13 | SBI와 OpenSBI: S-mode 커널에서 펌웨어 서비스 호출 | QEMU 기본 OpenSBI 위에서 0x80200000에 올라가는 S-mode 커널을 작성해 sbi_probe_extension으로 지원 확장을 출력하고, DBCN 콘솔 출력, sbi_set_timer 틱, HSM hart_start로 보조 hart 3개를 깨워 각 hart가 a0/a1으로 받은 hartid·opaque를 출력하게 한다. | **qemu** |
| RISCV-A01 | S-mode에서 MMU 켜기: satp, 상위 절반 커널, sfence.vma | RISCV-M13 커널에 Sv39 identity + 0xffffffc0_00000000 상위 매핑 테이블을 만들고 satp 기록 → sfence.vma → 상위 주소 점프 → identity 제거를 수행한 뒤, 가드 페이지 접근으로 page fault를 유발해 stval·scause를 출력하는 핸들러로 검증한다 (AArch64 TTBR/TCR 방식과 비교, see:ARM-A03). | **qemu** |
| RISCV-A02 | 원격 펜스: IPI·TLB shootdown과 FENCE.I | -smp 4 S-mode 커널에서 공유 매핑의 PTE를 바꾸고 SBI RFENCE(remote_sfence_vma)와 IPI 기반 자체 shootdown 두 방식을 구현해 완료 대기·지연을 측정하고, 런타임에 코드 버퍼를 쓰는 로더에서 fence.i 누락 시 문제가 되는 이유를 설계 문서로 정리한다 (ARM DC/IC 유지보수는 see:ARM-A04). | **qemu** |
| RISCV-A03 | AIA: IMSIC/APLIC와 MSI 기반 인터럽트 | IMSIC 인터럽트 파일과 APLIC MSI 전달 모드를 C 모델로 구현해 PCIe 장치 MSI 쓰기가 특정 hart의 eip 비트로 가는 경로를 시뮬레이션하고, QEMU virt,aia=aplic-imsic에서 Linux /proc/interrupts로 MSI 할당을 관찰한 결과를 PLIC 모델(RISCV-M09)과 비교한다 (GICv3 ITS는 see:SOCX, MSI-X는 see:PCIE). | mac |
| RISCV-A04 | RVWMO 메모리 모델: FENCE 종류·.aq/.rl과 C11 매핑 | C11 atomic load/store/RMW를 memory_order별로 riscv64 컴파일한 매핑 표를 만들고, MEM-M07 리트머스 열거기에 RVWMO 규칙 부분집합(같은 주소 순서, fence, aq/rl)을 추가해 MP·SB·LB 허용 결과를 SC·TSO(Ztso)·ARMv8과 비교한다 (see:MEM-M07, see:ARCH-A05). | mac |
| RISCV-A05 | RVV 1.0 기초: vsetvli, SEW/LMUL, strip-mining | vsetvli strip-mining 루프로 saxpy·memcpy·int8 합을 RVV intrinsic과 어셈블리로 작성해 QEMU -cpu rv64,v=true,vlen=128/256/512에서 같은 바이너리가 모두 정답을 내는지 검증하고, 고정 폭 NEON 버전(see:ARM-M06)과 꼬리 처리 코드를 비교한다. | **qemu** |
| RISCV-A06 | RVV int8 내적·GEMM 커널: vwmacc, 리덕션, segment load | int8×int8→int32 내적과 4×VL 타일 GEMM 마이크로커널을 RVV로 작성해 스칼라 기준과 비트 단위 대조하고, QEMU insn 카운트 플러그인으로 LMUL=1/2/4별 동적 명령 수를 비교하며 ARM SDOT/SMMLA 커널(see:ARM-A05)과 설계 차이를 정리한다 (NPU 관점은 see:NPU). | **qemu** |
| RISCV-A07 | 커스텀 명령어: custom-0/1 opcode와 .insn, ISS 구현 | custom-0 공간에 int8 dot4(rs1·rs2의 4바이트 곱 합 + rd 누산) R형 명령을 정의해 ISS에 구현하고, .insn r 매크로를 쓰는 C 인라인 함수로 int8 내적 커널을 재작성해 동적 명령 수·코드 크기 감소를 측정한다. | mac |
| RISCV-A08 | 가속기 연결 설계: CFU vs RoCC vs MMIO 코프로세서 | RISCV-A07 dot4를 (1) 1사이클 CFU, (2) valid/ready 다중 사이클 CFU, (3) MMIO 레지스터+폴링 가속기로 각각 트랜잭션 C 모델과 펌웨어 드라이버를 작성해 벡터 길이별 사이클 비용 곡선을 그리고 어느 크기에서 MMIO+DMA가 이기는지 설계 문서로 정리한다 (RTL 구현은 see:RTLDV, NPU 명령 프로세서는 see:NPU-M05). | mac |
| RISCV-A09 | 디버그 스펙: debug mode, dcsr/dpc, 트리거와 GDB 스텁 | ISS에 debug mode(dcsr.ebreakm, step, dpc)와 mcontrol 실행/로드 주소 트리거를 구현하고 GDB remote serial protocol 최소 스텁(g/G/m/M/Z0/Z1/s/c)을 붙여 Python RSP 클라이언트 스크립트로 브레이크포인트·단일 스텝·하드웨어 워치포인트를 검증한다 (JTAG TAP·OpenOCD 흐름은 see:TOOL-A01/A03). | mac |
| RISCV-A10 | Lockstep 공동 시뮬레이션: ISS 커밋 로그 vs Spike 비교 | 자작 ISS에 Spike --log-commits 형식(pc, 명령, rd 쓰기값, 메모리 쓰기) 로그 출력을 추가하고 같은 ELF를 Spike와 실행해 첫 불일치를 찾는 diff 도구를 작성한 뒤, 일부러 심은 버그 3개(SRA 부호, JALR LSB 클리어 누락, CSR 부작용)를 자동으로 찾아내는지 검증한다 (회귀 일관성 검사는 see:EMU-M10). | **qemu** |
| RISCV-A11 | 종합: 자작 ISS로 xv6-riscv 부팅 | RV64IMA+Zicsr ISS에 특권 모드·Sv39·CLINT·PLIC·NS16550·virtio-blk(mmio)를 더해 xv6-riscv 커널과 fs.img로 셸 프롬프트까지 부팅하고 usertests 일부를 통과시키며, 부팅 중 막힌 지점마다 원인·수정을 디버그 로그로 남긴다 (xv6 자체 해킹은 see:OS-A09/A10). | **qemu** |

### RTLDV — RTL 설계 · 검증 (RTL Design & Verification)

RTL 설계·검증은 AI 가속기·SoC·실리콘 검증 팀에서 펌웨어/검증 엔지니어에게도 요구되는 공통 언어다. NVIDIA·Tenstorrent·Rivian 같은 회사의 면접은 FIFO·CDC·skid buffer·아비터를 화이트보드로 그리게 하고, assertion·formal·UVM·커버리지로 '어떻게 검증했는지'를 묻는다. 이 트랙은 맥에서 오픈소스 툴(iverilog·Verilator·yosys·SymbiYosys·cocotb)만으로 그 전 과정을 직접 해본다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| RTLDV-B01 | RTL 툴체인 셋업: iverilog·Verilator·GTKWave와 자기검사 TB | 8비트 가산기(carry-out 포함)와 4:1 MUX를 Verilog로 작성하고, 전수 입력을 돌며 기대값과 비교해 PASS/FAIL을 출력하는 자기검사(self-checking) Verilog 테스트벤치를 iverilog로 실행·$dumpvars VCD를 GTKWave로 확인한 뒤 같은 RTL을 verilator --lint-only -Wall로 경고 0개로 만든다(C++ TB 방식은 see:EMU-B06). | **fpga** |
| RTLDV-B02 | Blocking vs Non-blocking과 래치 추론 함정 | 3단 시프트 레지스터를 '=' 와 '<=' 로 각각 작성해 시뮬레이션 결과 차이를 파형으로 보이고, if에 else가 빠진 always @(*) 블록과 불완전 sensitivity list 예제를 만들어 yosys 'proc; opt; stat'에서 $dlatch 셀이 추론되는 것과 RTL/게이트 시뮬레이션 불일치를 확인한 뒤 default 할당으로 수정한다. | **fpga** |
| RTLDV-B03 | 조합 논리 RTL: 디코더·우선순위 인코더·One-hot MUX | N비트 파라미터 우선순위 인코더(valid 출력 포함), 3-to-8 디코더, one-hot 선택 AND-OR MUX를 SystemVerilog로 작성하고 N=8/16에서 전수 테스트하며, unique case를 깨는 입력(두 비트 동시 on)에서 Verilator --assert 경고가 나는지 확인한다(C 진리표 모델은 see:ARCH-B05). | **fpga** |
| RTLDV-B04 | 순차 로직 RTL: 카운터·LFSR·에지 검출·÷3 클럭 분주 | 업/다운 카운터, 16비트 최대주기 LFSR(TB에서 주기 65535 확인), 상승/하강 에지 검출기, 50% duty ÷3 분주기(posedge+negedge 조합, 면접 단골)를 작성하고, 분주 클럭으로 다른 플롭을 구동하는 버전과 clock-enable 버전을 비교해 왜 생성 클럭 대신 enable을 써야 하는지 주석으로 정리한다(C 모델은 see:ARCH-B06). | **fpga** |
| RTLDV-B05 | FSM 코딩 스타일: Moore/Mealy·인코딩 선택 | '1011'(겹침 허용) 시퀀스 검출기를 Moore/Mealy로, 그리고 UART TX(IDLE→START→DATA×8→STOP) FSM을 3-process 스타일로 작성해 TB로 검증하고, yosys fsm 패스로 상태 인코딩을 binary/one-hot으로 바꿔 셀 수를 비교하며 정의되지 않은 상태에서 IDLE 복귀(default)를 확인한다(C 버전은 see:ARCH-B07). | **fpga** |
| RTLDV-B06 | 리셋 전략: Sync/Async 리셋과 리셋 동기화기 | 동기 리셋/비동기 리셋 카운터와 2-FF 리셋 동기화기(async assert, sync deassert)를 작성하고, iverilog에서 리셋 해제를 클럭 에지 ±수 ps 위치로 스윕하는 TB로 '동기화기 없을 때 플롭마다 해제 사이클이 갈리는' 상황을 모델링해 보이며, 데이터패스 플롭에서 리셋을 뺐을 때의 면적 차이를 yosys stat로 확인한다(리셋 원인 디코딩은 see:ARCH-B14). | **fpga** |
| RTLDV-B07 | SystemVerilog 구조화: parameter·generate·struct·interface | packed struct로 패킷 헤더를 정의하고, generate for로 N입력 가산기 트리(파이프라인 레지스터 옵션 parameter)를, interface+modport로 valid/ready 버스를 정의해 하나의 top에서 연결한 뒤 N=4/8/16을 Verilator로 빌드·검증하고, 같은 코드를 iverilog -g2012로 돌려 지원되지 않는 구문 목록을 정리한다. | **fpga** |
| RTLDV-B08 | 동기 FIFO RTL: Full/Empty·Count·FWFT | DEPTH·WIDTH 파라미터 동기 FIFO를 (1) 포인터 MSB 확장 방식 (2) count 레지스터 방식으로 각각 작성하고 almost_full/almost_empty와 first-word-fall-through 옵션을 넣은 뒤, 큐 모델과 매 사이클 비교하는 Verilog TB로 동시 push/pop at full·empty, overflow/underflow 시도를 검증한다(링 버퍼 SW 버전은 see:DS). | **fpga** |
| RTLDV-B09 | cocotb 입문: Python 테스트벤치로 FIFO 구동 | B08 동기 FIFO에 대해 cocotb 테스트 3개(기본 순서 보존, 랜덤 push/pop 1e4 사이클 + Python deque 모델 비교, 리셋 중 동작)를 작성하고 Makefile에서 SIM=icarus와 SIM=verilator로 각각 실행해 결과·속도를 비교하며, 실패 시 seed(RANDOM_SEED)로 재현하는 방법을 확인한다. | **fpga** |
| RTLDV-B10 | Valid/Ready 핸드셰이크 RTL 규칙과 레지스터 슬라이스 | valid/ready 1단 레지스터 슬라이스(forward-registered)를 작성하고 cocotb TB에서 소스 valid·싱크 ready를 랜덤으로 토글하며 데이터 손실/중복이 없음을 시퀀스 번호로 검증한 뒤, ready가 꺾이는 사이클에 data가 바뀌는 버그 버전을 만들어 TB 체커가 잡는지 확인한다(신호 vs 트랜잭션 모델은 see:EMU-B05, AXI 채널 C 모델은 see:ARCH-M14). | **fpga** |
| RTLDV-B11 | 합성 기초: yosys로 셀·면적·메모리 추론 읽기 | 앞 아이템의 FIFO·FSM·가산기 트리를 yosys synth / synth_ice40으로 합성해 stat(LUT·FF·carry 수)을 표로 비교하고, FIFO 저장소가 SB_RAM40_4K로 추론되는 코딩 패턴과 비동기 읽기 때문에 FF로 풀리는 패턴을 비교하며 read-during-write 동작을 TB로 확인한다. | **fpga** |
| RTLDV-M01 | Skid Buffer: 등록된 Ready로 풀 스루풋 유지 | B10 레지스터 슬라이스를 ready까지 등록하는 skid buffer로 확장하고 cocotb TB에서 (1) 상시 ready 시 1 beat/cycle (2) 랜덤 backpressure에서 손실·중복 0 (3) 단순 ready 등록 버전이 50% 처리량으로 떨어지는 것을 측정해 비교한다(Tenstorrent/NVIDIA RTL 면접 단골). | **fpga** |
| RTLDV-M02 | 파이프라인 데이터패스: Stall·Flush·Bubble Collapse | 3단 곱셈-누산 파이프라인을 (1) 전역 stall 방식 (2) 단별 valid/ready(bubble collapse) 방식으로 작성하고 cocotb로 동일 랜덤 입력·backpressure에서 처리량과 지연(cycle)을 측정하며, flush 입력이 들어오면 진행 중 결과를 폐기하는 동작을 Python 모델과 비교해 검증한다. | **fpga** |
| RTLDV-M03 | 아비터 RTL: Fixed·Round-Robin·Weighted RR | N요청 fixed-priority, 마스크(또는 double-width 트릭) 라운드로빈, 가중 RR 아비터를 RTL로 작성하고 cocotb에서 grant one-hot·요청 없는 grant 금지·N사이클 내 무조건 서비스(starvation-free)를 체크하며, 각 방식의 yosys 셀 수와 논리 깊이를 비교한다(지연·대역폭 C 시뮬레이션은 see:ARCH-A10). | **fpga** |
| RTLDV-M04 | 고정소수점 데이터패스 RTL: Signed·확장·포화 | Q1.15 × Q1.15 → Q2.30 곱셈과 누산기(포화·round-to-nearest 옵션)를 SystemVerilog로 작성하고 cocotb에서 numpy 정수 모델과 경계값(-1×-1, 최대 누산 overflow) 포함 1e5 랜덤 벡터를 비교하며, $signed 누락·부호 확장 누락 버그를 일부러 넣어 어떤 입력에서 드러나는지 기록한다(Q-format 이론은 see:DSP). | **fpga** |
| RTLDV-M05 | CDC 1: 2-FF 동기화기·펄스 동기화·Req/Ack 핸드셰이크 | 2-FF 레벨 동기화기, 토글 기반 펄스 동기화기, 4-phase req/ack 멀티비트 버스 동기화기를 RTL로 작성하고 서로소 주기 두 클럭+랜덤 위상 cocotb TB에서 펄스 유실 0·데이터 일관성을 검증하며, 동기화기 출력에 랜덤 1사이클 지연을 주입하는 메타안정성 모델로 설계가 견디는지 확인하고 MTBF 식으로 2-FF vs 3-FF를 계산한다. | **fpga** |
| RTLDV-M06 | CDC 2: 비동기 FIFO RTL (Gray 포인터) | Gray 코드 읽기/쓰기 포인터와 2-FF 동기화로 full/empty를 생성하는 비동기 FIFO를 작성하고 클럭비(1:1, 3:7, 10:1)와 랜덤 지터를 바꿔가며 cocotb로 순서 보존·overflow/underflow 0을 검증하며, 이진 포인터를 그대로 넘기는 버그 버전이 실패하는 시드를 찾는다(C 이벤트 모델은 see:ARCH-A07). | **fpga** |
| RTLDV-M07 | Lint와 정적 CDC 검사: Verilator -Wall + 넷리스트 스크립트 | WIDTH·UNOPTFLAT(조합 루프)·LATCH·MULTIDRIVEN·CASEINCOMPLETE 경고를 유발하는 RTL 모음을 만들어 verilator --lint-only -Wall로 잡고 수정한 뒤, yosys write_json 넷리스트를 Python으로 읽어 '다른 클럭 도메인 플롭이 동기화기 없이 직접 D로 들어가는 경로'와 '동기화 전 조합 로직(glitch)'을 찾아내는 CDC 체커를 작성해 M05/M06 RTL과 일부러 망가뜨린 버전에 적용한다. | **fpga** |
| RTLDV-M08 | SVA 기초: Immediate·Concurrent Assertion으로 프로토콜 체크 | B10/M01 valid/ready 인터페이스에 'valid && !ready 이면 다음 사이클에도 valid·data 유지', B08 FIFO에 'full일 때 push 금지', 'pop 후 count 감소'를 SVA로 작성해 bind 파일로 붙이고 Verilator --assert(또는 --timing)로 실행해 의도적 버그 3종이 각각 어느 assertion에서 몇 사이클에 걸리는지 리포트한다. | **fpga** |
| RTLDV-M09 | Formal 입문: SymbiYosys BMC·k-Induction | B08 동기 FIFO에 formal 속성(count ≤ DEPTH, empty⇔count==0, 넣은 데이터가 순서대로 나옴 — 2-심볼 트릭)을 작성해 sby로 bmc depth 20을 통과시키고, prove(k-induction)가 실패하는 경우 반례 VCD를 분석해 포인터-카운트 불변식을 추가로 assert해 증명을 완료하며 cover로 full 도달 트레이스를 생성한다. | **fpga** |
| RTLDV-M10 | cocotb 검증 구조화: Driver·Monitor·Scoreboard | valid/ready 스트림 DUT(M02 파이프라인)에 대해 Transaction 클래스, 입력 Driver(랜덤 idle 삽입), 입출력 Monitor, 예상값 큐를 가진 Scoreboard를 cocotb로 분리 구현하고, 테스트 종료 시 drain·미매칭 트랜잭션 보고·타임아웃 watchdog를 넣은 뒤 같은 컴포넌트를 M01 skid buffer에 재사용한다(Python 모델 기반 scoreboard 개념은 see:SIV-A01). | **fpga** |
| RTLDV-M11 | Constrained Random 자극: 제약·분포·시드 재현 | 패킷 트랜잭션(길이 1–256, 64B 정렬 주소, 4KB 경계 미횡단, 길이 분포 dist 10/80/10)을 pyvsc 제약으로 랜덤화해 M10 Driver에 연결하고, 제약 충돌 시 처리·solve-before에 해당하는 순서 제어를 실험하며, 실패 시 seed+트랜잭션 로그로 단일 실패 재현 테스트를 자동 생성한다(SV rand/constraint 문법과 1:1 대응표 포함). | **fpga** |
| RTLDV-M12 | 기능·코드 커버리지: Covergroup·Cross·Hole 분석 | B08 FIFO에 cocotb-coverage로 fill level bin, full/empty 전이, 'full에서 동시 push/pop'·'almost_full 경계' cross를 정의하고, Verilator --coverage로 line/toggle 커버리지를 함께 수집해 verilator_coverage로 annotate한 뒤, 랜덤만으로 못 채운 hole을 directed 테스트로 메워 커버리지 수렴 곡선을 그린다(Python 모델 커버리지는 see:SIV-M13). | **fpga** |
| RTLDV-M13 | 레지스터 블록 RTL 생성: SystemRDL → APB CSR | 타이머 IP의 CTRL/LOAD/VALUE/INTSTS(W1C)/INTEN 레지스터를 SystemRDL로 기술해 PeakRDL-regblock으로 APB4 CSR RTL과 C 헤더를 생성하고, 하드웨어 측 hwset으로 인터럽트 비트를 세우는 카운터를 붙인 뒤 cocotb APB 드라이버로 리셋값·RW·W1C·sw-hw 동시 접근 우선순위를 검증한다(JSON 기술서→C 헤더 SW 흐름은 see:SIV-B01, see:EMU-B09). | **fpga** |
| RTLDV-A01 | UVM 아키텍처 실습: pyuvm Agent·Env·Sequence | M10 cocotb 컴포넌트를 pyuvm으로 옮겨 uvm_agent(sequencer/driver/monitor), uvm_env, uvm_scoreboard(analysis port 연결), base/random/backpressure sequence를 만들고, factory override로 에러 주입 driver로 교체하는 테스트를 작성한 뒤 각 클래스·phase를 SystemVerilog UVM 등가 코드 스니펫과 나란히 정리해 면접에서 UVM TB 구조를 화이트보드로 설명할 수 있게 한다. | **fpga** |
| RTLDV-A02 | UVM RAL: 레지스터 모델·Predictor·Mirror 검사 | M13에서 생성한 APB CSR 블록에 대해 pyuvm RAL(또는 직접 만든 미니 uvm_reg 계층)로 레지스터 모델·APB adapter·explicit predictor를 구성하고, hw_reset/bit_bash에 해당하는 내장 시퀀스와 mirror(check) 비교를 실행해 W1C 필드 access 정의를 틀리게 만든 모델 버그와 RTL 버그를 각각 구분해 잡는다(실리콘 측 리셋값·walking 테스트는 see:SIV-B03). | **fpga** |
| RTLDV-A03 | 회귀 인프라: 시드·테스트 리스트·커버리지 병합·Triage | 테스트×시드 매트릭스를 병렬 실행하는 Python 러너(결과 JSON, 타임아웃, 재실행 시 파형 덤프 on)를 작성하고 verilator_coverage로 회차별 커버리지를 병합, 실패 로그를 assertion명·메시지 정규화 시그니처로 묶어 'first failing seed + 재현 커맨드' 리포트를 생성하며, 일부러 넣은 저확률 버그가 몇 시드에서 처음 잡히는지 측정한다(실리콘 로그 triage는 see:SIV-M10, CI 파이프라인은 see:INFRA). | **fpga** |
| RTLDV-A04 | AXI4 버스트 슬레이브 RTL과 프로토콜 체커 | AXI4 메모리 슬레이브(outstanding 2, WRAP 주소 계산, WSTRB, SLVERR 응답)를 SystemVerilog로 작성하고, AW/W/B/AR/R 각 채널 규칙(VALID 유지, WLAST 위치, 4KB 비횡단, 같은 ID 순서)을 SVA 프로토콜 체커로 bind해 cocotb 랜덤 마스터로 검증한다(채널 C 모델은 see:ARCH-M14, AXI-lite Verilator TB는 see:EMU-M01, 인터커넥트 수준 ordering은 see:SOCX). | **fpga** |
| RTLDV-A05 | Formal 심화: AXI4-Lite 슬레이브 인터페이스 증명 | EMU-M01과 같은 AXI4-Lite 레지스터 슬레이브에 ZipCPU faxil_slave 스타일의 마스터 assume/슬레이브 assert 속성(outstanding 카운터, 응답 대기 상한)을 작성해 sby prove를 시도하고, 의도적으로 넣은 '동시 AW·W 도착 시 BVALID 누락' 버그의 반례를 확인·수정한 뒤 induction 실패를 내부 카운터 불변식으로 강화해 증명을 완료한다(see:EMU-M01). | **fpga** |
| RTLDV-A06 | Gate-level 시뮬레이션과 X-전파 문제 | 리셋되지 않은 데이터패스 플롭과 if(x)/case(x)의 X-optimism을 가진 RTL을 yosys로 합성해 write_verilog 넷리스트를 iverilog로 gate-level 시뮬레이션하고, RTL 시뮬레이션과 출력이 갈리는 지점을 파형으로 추적하며, Verilator --x-assign/--x-initial unique 랜덤 초기화로 같은 문제를 RTL 단계에서 잡는 방법을 비교한다. | **fpga** |
| RTLDV-A07 | 타이밍 클로저: 파이프라이닝·리타이밍과 등가성 검사 | 32비트 MAC과 우선순위 인코더 체인을 yosys+nextpnr-ice40으로 P&R해 타이밍 리포트에서 크리티컬 패스·Fmax를 읽고, 파이프라인 단 추가·리타이밍·one-hot 재인코딩으로 Fmax를 개선한 뒤 원본 대비 등가성(조합 변경은 yosys equiv_*, 지연 추가는 latency 보정 miter + sby)을 증명하며, multicycle/false path·SDC 제약 개념을 결과와 연결해 정리한다(setup/hold 산수는 see:ARCH-B13). | **fpga** |
| RTLDV-A08 | 저전력 RTL: Clock Gating·Operand Isolation·Toggle 측정 | M04 MAC 데이터패스에 ICG 셀 모델(래치+AND) 기반 clock gating과 입력 operand isolation을 넣고 동일 워크로드 VCD의 toggle 수를 비교해 동적 전력 감소를 추정하며, 전원 차단 도메인 출력에 isolation clamp·retention 레지스터 모델을 넣고 'shutdown 중 출력은 clamp 값', 'restore 후 상태 복원'을 SVA로 검증한다(toggle→전력 추정은 see:EMU-A06, NPU 전력 관리는 see:NPU-A07). | **fpga** |
| RTLDV-A09 | 저지연 스트리밍 패킷 파서 RTL (64-bit AXI-Stream) | 64비트 AXI-Stream(tkeep 포함)으로 들어오는 Ethernet/IPv4/UDP 프레임에서 dst MAC·IP·UDP 포트·페이로드 첫 8B를 추출하는 파서를 작성해 VLAN 유무로 바뀌는 오프셋을 처리하고, cocotb에서 scapy로 만든 패킷과 비교하며 첫 바이트 입력→필드 valid까지의 사이클 지연과 1 beat/cycle 유지를 측정한다(HFT FPGA 맥락은 see:QFIN, see:LLAT). | **fpga** |
| RTLDV-A10 | 버그 헌팅: 파형·Assertion·Formal Cover로 RTL 결함 국소화 | 앞서 만든 async FIFO·skid buffer·arbiter에 각각 은닉 버그(Gray 변환 비트 오류, skid 2번째 엔트리 덮어쓰기, RR 마스크 갱신 누락)를 넣은 버전을 서로 섞어 놓고, 실패 시드 → 파형 → assertion 추가 → sby cover/bmc로 최소 반례 트레이스 생성까지의 과정을 각 버그별 RCA 리포트(증상·근본원인·수정·재발 방지 assertion)로 작성한다(면접 '이 파형에서 버그를 찾아라' 대비). | **fpga** |
| RTLDV-A11 | 캡스톤: CSR·스트림·인터럽트를 갖춘 가속기 블록 설계·검증 | APB CSR(M13 생성)·AXI-Stream 입출력·완료 인터럽트를 가진 CRC32/dot-product 가속기를 스펙 문서(레지스터 맵, 타이밍, 에러 처리)부터 작성해 RTL로 구현하고, pyuvm TB(A01/A02 재사용)·인터페이스 SVA·sby 인터페이스 증명·기능 커버리지 클로저·yosys 면적/Fmax 리포트를 갖춘 검증 사인오프 체크리스트로 마무리한다(systolic array RTL은 see:NPU-A09, DPI-C 참조모델은 see:EMU-M03). | **fpga** |

### SOCX — SoC 인터커넥트 · 칩렛 (SoC Interconnect & Chiplets)

현대 SoC와 AI 가속기의 성능·전력·정확성은 코어보다 그 사이를 잇는 인터커넥트(AXI/CHI, 크로스바, NoC), SoC 수준 일관성·SMMU·인터럽트 패브릭, 그리고 칩렛 간 D2D 링크에서 결정되는 경우가 많다. NVIDIA·AMD·Tenstorrent·Apple·Rivian 같은 회사의 실리콘/펌웨어 면접은 'outstanding이 몇 개 필요한가', '이 라우팅은 데드락이 없나', 'nested walk 비용은?' 같은 질문을 수치로 답하길 요구하므로, 이 트랙은 모든 개념을 Mac에서 돌아가는 C/Python 모델로 직접 계산·검증한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| SOCX-B01 | SoC 인터커넥트 지도: Shared Bus vs Crossbar vs NoC | N=4–64 마스터/슬레이브에 대해 shared bus·full crossbar·ring·2D mesh의 crosspoint/링크 수, bisection 대역폭, 평균 홉 수를 계산하는 Python 도구를 작성하고, 평균 홉은 그래프 BFS 결과와 대조해 '몇 개 마스터부터 NoC가 이득인가'를 표로 정리한다(버스 기초는 see:ARCH-M13). | mac |
| SOCX-B02 | AXI4 버스트 주소 계산: AxLEN/AxSIZE/AxBURST·WSTRB | AW/AR 속성(addr, len, size, burst)과 데이터 버스 폭을 받아 beat별 주소·활성 바이트 레인·WSTRB를 출력하는 C 함수와 4KB 경계·WRAP 길이(2/4/8/16) 위반 검사기를 작성하고, 스펙 예제 표와 무작위 속성 1e5개 참조 모델 비교로 검증한다(채널 핸드셰이크는 see:ARCH-M14). | mac |
| SOCX-B03 | AXI 응답과 배타적 접근: EXOKAY와 Global Exclusive Monitor | AxLOCK 배타 읽기/쓰기를 처리하는 슬레이브 측 global exclusive monitor(마스터 ID별 예약 주소·크기)를 C로 구현하고, 두 마스터가 같은 락 변수를 경쟁하는 인터리빙 시나리오를 전수 열거해 EXOKAY/OKAY 결과와 락 상호배제가 지켜짐을 검증한다(코어 로컬 모니터는 see:ARM-M11). | mac |
| SOCX-B04 | 메모리 속성과 Early Write Response: AxCACHE·AxPROT | 쓰기 버퍼를 가진 인터커넥트 모델(Bufferable이면 조기 BRESP, Device-nGnRE면 최종 슬레이브 응답)을 C로 만들어 '인터럽트 클리어 레지스터에 쓴 직후 IRQ unmask → 중복 인터럽트' 버그를 재현하고, read-back·DSB 위치에 따른 수정 효과를 사이클 로그로 비교한다. | mac |
| SOCX-B05 | Outstanding 트랜잭션과 Little's Law 대역폭 | 대역폭 = outstanding × 바이트/트랜잭션 ÷ 왕복지연 모델로 'DDR 지연 150ns, 64B 버스트, 25.6GB/s 목표 → 필요 outstanding' 같은 문제를 푸는 계산기를 작성하고, outstanding 상한을 가진 마스터·고정지연 슬레이브 DES로 달성 대역폭 포화 곡선이 모델과 맞는지 확인한다(see:GPGPU-B05). | mac |
| SOCX-B06 | 버스 폭·프로토콜 변환기: AXI4 128b → AXI4-Lite/APB 32b | 128비트 AXI4 INCR 버스트를 32비트 AXI4-Lite 단일 전송 열로 분할하고 응답(최악 RESP 병합)을 재조립하는 width/protocol converter를 C로 구현해, 임의 size/len/주소 정렬 1e5개 요청에 대해 메모리 결과와 응답이 이상적 모델과 일치함을 검증하고 변환 후 처리량 손실을 계산한다. | mac |
| SOCX-B07 | 크로스바 주소 디코드와 TrustZone 보안 필터 | 마스터별 가시 주소 맵(부트 alias 리맵 포함)과 TZASC/TZC-400 스타일 영역(base/size/secure·non-secure R/W 권한) 필터를 가진 크로스바 디코더를 C로 작성하고, NS 마스터의 secure 영역 접근 → DECERR/인터럽트, 영역 겹침 시 우선순위 규칙을 테스트로 검증한다(단일 디코더는 see:ARCH-B11). | mac |
| SOCX-B08 | NoC 기초 지표: Flit·Phit와 Zero-load 지연 | k×k mesh/torus, ring, fat-tree에 대해 직경·평균 홉·bisection 링크 수와 T0 = H·t_router + L/b(직렬화) 무부하 지연, uniform 트래픽 이론 포화 주입률을 계산하는 Python 도구를 만들고, 64B 캐시라인 패킷의 플릿 폭(64/128/256b)별 지연을 비교한다. | mac |
| SOCX-B09 | 2D 메시 XY 라우팅 시뮬레이터 | 4×4/8×8 메시에서 XY 라우팅, 라우터 파이프라인 지연, 링크당 1플릿/사이클 제약을 가진 사이클 단위 C 시뮬레이터를 작성해 uniform·transpose·hotspot 트래픽의 주입률별 평균 지연 곡선을 그리고, 포화점이 B08의 bisection 상한과 어떻게 비교되는지 설명한다(멀티코어 NPU 매핑은 see:NPU-A05). | mac |
| SOCX-B10 | GICv3 기초: INTID 분류와 Affinity 라우팅 | GICD_ISENABLER/IPRIORITYR/IROUTER, GICR, ICC_IAR1/EOIR1 레지스터를 가진 GICv3 C 모델을 만들어 SPI의 affinity(Aff3.Aff2.Aff1.Aff0) 라우팅과 1-of-N 라우팅, running priority 기반 선점을 시뮬레이션하고 핸들러 순서를 기대 로그와 비교한다(NVIC형 모델은 see:ARCH-M09, QEMU GIC 설정은 see:ARM-A02). | mac |
| SOCX-B11 | 저전력 인터페이스: AMBA Q-channel·P-channel 핸드셰이크 | Q-channel 컨트롤러(클럭 컨트롤러 측)와 디바이스 측 FSM을 C로 구현해 outstanding 트랜잭션이 있을 때 QDENY, 유휴 시 QACCEPT 후 게이팅되는 흐름을 이벤트 시뮬레이션하고, 금지된 신호 전이를 잡는 프로토콜 체커로 결함 주입 테스트를 한다(도메인 시퀀싱은 see:ARCH-A09). | mac |
| SOCX-B12 | 칩렛 경제학: 수율 모델과 Good-die 비용 | Poisson/Murphy/negative-binomial 수율 모델, 웨이퍼당 다이 수, D2D PHY 면적 오버헤드, 패키징·KGD 테스트 비용을 넣은 Python 비용 모델을 작성해 800mm² 모놀리식 vs 4×200mm² 칩렛의 good-die당 비용을 비교하고, 결함 밀도별 손익분기 면적을 구한다. | mac |
| SOCX-M01 | 크로스바 HOL 블로킹과 VOQ | N×N 크로스바 사이클 시뮬레이터에서 단일 입력 FIFO와 VOQ(virtual output queue) + iSLIP 매칭을 구현해 uniform 트래픽 처리량을 측정하고, N이 커질 때 FIFO 포화율이 2−√2에 수렴함을 확인한다(중재기 자체는 see:ARCH-A10, see:SIM-M05). | mac |
| SOCX-M02 | ID 기반 순서와 Reorder Buffer 성능 | 지연이 다른 두 슬레이브와 ID 수·outstanding 상한을 조절할 수 있는 인터커넥트 모델을 C로 만들어, 단일 ID·ID per slave·ID per transaction 정책별 처리량과 마스터 측 reorder buffer 크기 요구를 측정하고, 인터커넥트가 ID 재매핑으로 순서를 보존하는 방법을 구현한다. | mac |
| SOCX-M03 | 인터커넥트 QoS: AxQOS와 대역폭/Outstanding 레귤레이터 | 디스플레이(주기적, 언더런 임계)·CPU(지연 민감)·DMA/NPU(대량) 마스터가 DDR 포트를 공유하는 DES에 AxQOS 우선순위 중재, 토큰 버킷 대역폭 레귤레이터, outstanding 제한을 각각 추가해 디스플레이 FIFO 언더런 횟수·CPU p99 지연·총 처리량을 비교한다(see:DISP-A03). | mac |
| SOCX-M04 | Wormhole 라우터와 Credit 기반 흐름 제어 | RC→VA→SA→ST 파이프라인, VC당 플릿 버퍼, 크레딧 반환 지연을 가진 wormhole 라우터를 B09 시뮬레이터에 넣고 버퍼 깊이 < 크레딧 RTT일 때 링크 활용률 저하를 측정해 '필요 버퍼 = RTT × 링크 대역폭' 식을 검증하며, VC 수 1/2/4별 포화 처리량을 비교한다(PCIe 크레딧은 see:PCIE-M11). | mac |
| SOCX-M05 | NoC 라우팅 데드락: 채널 의존 그래프 | 라우팅 함수(XY, 무제한 최소 경로, torus 기본 DOR)를 입력받아 CDG를 생성하고 사이클을 DFS로 찾는 Python 도구를 작성해 메시 XY는 비순환, torus 랩어라운드는 순환임을 보이고, dateline VC 2개 분리로 사이클이 사라짐을 확인한 뒤 시뮬레이터에서 실제 데드락을 재현·해소한다. | mac |
| SOCX-M06 | 적응 라우팅: Turn Model과 Odd-Even | west-first·north-last·odd-even 라우팅을 시뮬레이터에 구현하고 M05 CDG 검사기로 데드락 자유성을 먼저 확인한 다음, transpose·bit-complement·hotspot 트래픽에서 XY 대비 포화 처리량과 평균 지연을 비교해 적응성의 이득과 순서 역전(out-of-order 도착) 비용을 정리한다. | mac |
| SOCX-M07 | 프로토콜 데드락과 메시지 클래스 분리 | 요청을 받으면 응답을 보내야 소비가 끝나는 홈 노드 모델을 단일 VC 메시에 올려 버퍼 포화 시 프로토콜 데드락을 재현하고, REQ/RSP/SNP/DAT virtual network 분리(CHI 채널 구조와 대응)와 응답 예약 버퍼로 해소됨을 검증한다. | mac |
| SOCX-M08 | NIU: AXI 트랜잭션 → NoC 패킷화 | AXI AR/AW+W를 헤더 플릿(목적지, 트랜잭션 태그, 속성)+데이터 플릿으로 패킷화하고 응답 패킷을 AXI ID 순서 규칙에 맞게 재정렬하는 NIU 모델을 C로 작성해, 버스트 길이·플릿 폭별 헤더 오버헤드와 실효 대역폭을 계산하고 무작위 트래픽에서 순서 규칙 체커(see:ARCH-M14)를 통과시킨다. | mac |
| SOCX-M09 | ACE·ACE-Lite: 하드웨어 IO 일관성 | CPU 캐시 2개(MESI, see:ARCH-A04) + CCI 스타일 인터커넥트 + ACE-Lite DMA 마스터 모델을 만들어 DMA ReadOnce/WriteUnique가 CPU 캐시를 스누프하는 흐름을 구현하고, 비일관 DMA + clean/invalidate(see:ARCH-M12) 방식과 버퍼 크기별 지연·스누프 트래픽을 비교한다. | mac |
| SOCX-M10 | SoC 스케일 Snoop Filter: 크기와 Back-invalidation | 코어별 private 캐시 크기 합 대비 snoop filter 엔트리 수(0.5×–2×)와 연관도를 바꾸며 SF 축출 시 back-invalidation을 수행하는 모델을 트레이스 구동으로 작성해, 브로드캐스트 스누프 대비 스누프 메시지 감소와 back-invalidation으로 인한 추가 캐시 미스를 측정한다(디렉터리 기본은 see:MEM-M05). | mac |
| SOCX-M11 | AMBA CHI 트랜잭션 흐름: RN-F·HN-F·SN-F | RN-F 3개·HN-F(SF 포함)·SN-F로 구성된 CHI 메시지 시퀀스 모델을 Python으로 작성해 주요 트랜잭션의 메시지 순서와 최종 캐시 상태(UC/UD/SC/SD/I)를 검증하고, DCT/DMT 사용 여부에 따른 홉 수·지연과 HN 버퍼 부족 시 RetryAck→PCrdGrant 재시도 흐름을 재현한다. | mac |
| SOCX-M12 | SMMUv3: StreamID → STE → CD와 2단계 변환 | Stream Table(2-level)·Context Descriptor·stage1/stage2 4-level 페이지 테이블을 가진 SMMUv3 C 모델을 작성해 nested walk 메모리 접근 수가 최악 (4+1)×(4+1)−1=24임을 카운트로 확인하고, TLB/walk cache 유무별 평균 변환 지연과 fault 시 Event Queue 레코드 생성을 검증한다(IOMMU 기초는 see:PCIE-M15, 패스스루는 see:VIRT). | mac |
| SOCX-M13 | GICv3 ITS: MSI → LPI 변환 | GITS_CBASER 명령 큐(MAPD/MAPC/MAPTI/INV/SYNC/MOVI)와 Device Table→ITT→Collection→Redistributor 변환, LPI pending/config 테이블을 가진 ITS C 모델을 작성해 GITS_TRANSLATER에 (DeviceID, EventID) 쓰기 시 올바른 CPU에 LPI가 뜨는지, MOVI 후 인터럽트 이동과 INV 누락 버그를 테스트한다(PCIe MSI-X는 see:PCIE-M10). | mac |
| SOCX-A01 | UCIe Die-to-Die 링크 계산: 대역폭 밀도·에너지·지연 | UCIe 모듈(x16/x64 레인, 4–32 GT/s, 범프 피치) 파라미터로 GB/s/mm shoreline·GB/s/mm² 밀도, 링크 전력(pJ/bit × BW), FDI/RDI 계층 지연 합을 계산하는 Python 모델을 작성해 '다이 가장자리 10mm로 2TB/s 양방향이 가능한가?' 같은 설계 질문에 답하고, PCIe/CXL PHY 기반 연결과 비교한다(see:PCIE-B02). | mac |
| SOCX-A02 | D2D 링크 계층: Flit CRC와 Replay 시뮬레이션 | 256B flit + CRC 생성/검사, 시퀀스 번호, go-back-N replay 버퍼를 가진 D2D 링크 계층을 C로 구현해 BER 1e-15–1e-6 비트 오류 주입 하에서 실효 대역폭과 p99.99 지연을 측정하고, replay 버퍼 크기 < RTT×BW일 때의 처리량 손실을 확인한다(PCIe ACK/NAK은 see:PCIE-M11). | mac |
| SOCX-A03 | 멀티 다이 SoC 파티셔닝과 NUMA형 지연 모델 | 컴퓨트 다이 4개 + IO 다이(메모리 컨트롤러) 구성의 다이 내 NoC·D2D 링크·DDR/HBM 경로 모델을 C로 만들어 주소 인터리빙(다이 로컬 vs 전역 interleave) 정책별 평균/꼬리 지연과 D2D 링크 포화 지점을 측정하고, 같은 워크로드의 모놀리식 대비 성능 손실을 보고한다(OS 측 NUMA는 see:MEM-M09). | mac |
| SOCX-A04 | 다이 간 일관성: 홈 노드 배치와 원격 스누프 비용 | M11 CHI 모델을 두 다이로 확장(CHI C2C 개념)해 주소 해시 기반 홈 노드 분산, 다이별 로컬 홈 + 원격 SF 캐싱 두 방식에서 공유 데이터 생산자-소비자 트레이스의 원격 메시지 수·평균 미스 지연을 측정하고, 프로그래머에게 보이는 '다이 친화 배치' 지침을 도출한다. | mac |
| SOCX-A05 | HBM 채널·Pseudo-channel 인터리빙과 AI 텐서 접근 | HBM3 스택 N개 × 16채널 × 2 pseudo-channel 주소 매핑(선형/XOR 해시)을 가진 컨트롤러 측 모델에 GEMM 타일·KV 캐시 접근 트레이스를 넣어 채널별 요청 분포·뱅크 충돌·달성 대역폭을 측정하고, 해시 매핑으로 불균형을 해소함을 보인다(대역폭 계산은 see:MEM-A03, 매핑 기초는 see:MEM-M02). | mac |
| SOCX-A06 | CXL 패브릭: 스위치·메모리 풀링과 티어링 | 호스트 4개·CXL 스위치·Type 3 MLD 메모리 장치의 용량 할당(논리 장치 바인딩) 모델과, 로컬 DDR(~100ns) vs CXL(~250ns) 2티어 메모리에서 접근 빈도 기반 hot-page promotion/demotion 정책을 Python으로 시뮬레이션해 티어링 정책별 평균 메모리 지연과 마이그레이션 트래픽을 비교한다(CXL 기초·HDM은 see:PCIE-A10). | mac |
| SOCX-A07 | SoC 유스케이스 대역폭 예산 모델 | 4K60 녹화+프리뷰+NPU 검출 같은 동시 유스케이스의 마스터별 대역폭·지연 요구를 스프레드시트형 Python 예산 모델로 산출하고, 같은 트래픽을 M03 QoS DES에 넣어 평균 대역폭은 맞지만 버스트 겹침으로 실시간 마스터가 실패하는 경우를 찾아 버퍼 크기·QoS 설정을 권고한다(see:SIM-A06, see:CAM). | mac |
| SOCX-A08 | 인터커넥트 전력: 비트당 에너지와 NoC DVFS | 링크·라우터·SRAM 버퍼의 pJ/bit/hop 파라미터와 NoC 전압-주파수 테이블을 가진 전력 모델을 B09 시뮬레이터에 붙여, 트래픽 부하에 따른 NoC DVFS·유휴 링크 클럭 게이팅(B11 Q-channel) 정책별 에너지와 지연 증가를 비교하고, 데이터 이동 에너지가 연산 에너지를 넘는 지점을 계산한다(see:NPU-A07). | mac |
| SOCX-A09 | 인터커넥트 행(hang) 디버그와 성능 모니터 | 타임아웃 감지기·트랜잭션 트래커·지연 히스토그램/대역폭 PMU를 가진 인터커넥트 모델에 결함(슬레이브 무응답, 파워 게이팅된 도메인 접근, 크레딧 누수)을 주입하고, 행 시점의 outstanding 테이블 덤프만 보고 원인 블록을 지목하는 분석 스크립트를 작성한다(트리아지 일반론은 see:SIV-M10). | mac |
| SOCX-A10 | 종합 설계: AI 가속기 칩렛 SoC 인터커넥트 아키텍처 | 컴퓨트 칩렛 4개 + IO 다이 + HBM3 스택 4개 구성의 AI SoC에 대해 NoC 토폴로지, 일관성 도메인 경계, UCIe 링크 수, SMMU/ITS 위치, QoS 클래스를 결정하는 설계 문서를 작성하고, 앞선 B08·A01·A03·A05·A07 모델로 각 결정의 대역폭·지연 근거를 표로 제시한다(엣지 SoC 버전은 see:ARCH-A12). | mac |

## 11단계 · 데이터센터 · 네트워킹 (Datacenter & Networking)

### NET — 리눅스 네트워크 스택 (Linux Networking Stack)

리눅스 네트워크 스택은 데이터센터·AI 패브릭·SmartNIC·로봇 텔레메트리 어디서나 바닥에 깔린 공통 기반이다. 이 트랙은 맥에서 ARP·IP·ICMP·UDP·TCP를 유저스페이스(가상 와이어/utun)로 직접 구현해 원리를 몸으로 확인한 뒤, Lima VM에서 sk_buff·NAPI·오프로드·netfilter·tc·XDP/eBPF 같은 커널 경로를 계측한다. Meta/Google 프로덕션 엔지니어링, NVIDIA/Arista/Broadcom 네트워킹, 임베디드 리눅스 면접의 'TCP 동작 설명'과 '패킷 드롭 디버깅' 질문을 대비하며 DCN·NETHW 트랙의 선수 과정이다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| NET-B01 | 가상 와이어 시뮬레이터와 pcap 기록 (Link Harness) | 프레임 단위 send/recv API를 가진 인메모리 가상 와이어(점대점 + N포트 러닝 스위치)에 손실·지연·재정렬·복제·비트오류 주입 옵션을 넣고, 오가는 모든 프레임을 pcap(LINKTYPE_ETHERNET)으로 기록해 Wireshark로 열리는지 확인한다(프레임 파싱 자체는 see:COMM-A04). | mac |
| NET-B02 | 인터넷 체크섬 심화: 고속 계산과 증분 갱신 (RFC 1071/1624) | 32/64비트 누산 + 캐리 접기 고속 체크섬과 TTL 감소·NAT 주소/포트 변경 시의 증분 갱신(RFC 1624 식 HC' = ~(~HC + ~m + m'))을 구현해 무작위 패킷 1e6개에서 전체 재계산과 일치함을 검증하고, 0xFFFF/-0 코너 케이스와 UDP 체크섬 0의 의미를 테스트한다(기본 파서는 see:COMM-A04). | mac |
| NET-B03 | ARP: 캐시 상태, 대기 큐, Gratuitous ARP | B01 와이어 위 3호스트에서 ARP request/reply, INCOMPLETE→REACHABLE→STALE 캐시 상태와 만료, 해석 대기 중 패킷 큐(최대 N개, 초과 시 드롭), 재시도 3회 후 실패 처리를 구현하고, gratuitous ARP로 주소 중복 감지·캐시 갱신을 테스트한 뒤 IPv6 NDP(NS/NA)와의 차이를 정리한다. | mac |
| NET-B04 | IPv4 송수신·포워딩과 라우팅 테이블 LPM | IPv4 입력 검증(버전/IHL/총길이/체크섬)과 출력 경로를 구현하고, 선형 탐색 vs 이진 트라이 LPM 라우팅 테이블(기본 경로·호스트 경로 포함)로 두 서브넷을 잇는 라우터 노드를 만들어 포워딩·TTL 감소·증분 체크섬 갱신을 검증하며 10만 경로에서 조회 속도를 비교한다(하드웨어 LPM/TCAM은 see:NETHW). | mac |
| NET-B05 | ICMP: Echo 응답기, Time Exceeded, traceroute | 미니 스택에 echo reply와 라우터 노드의 Time Exceeded·Destination Unreachable 생성(원본 IP 헤더+8바이트 인용, 에러에 대한 에러 금지 규칙)을 구현하고, 맥에서 비특권 SOCK_DGRAM/IPPROTO_ICMP 소켓으로 ping과 UDP 기반 traceroute를 직접 작성해 실제 인터넷 경로로 검증한다. | mac |
| NET-B06 | IP 단편화·재조립과 Path MTU Discovery | MTU에 맞춰 8바이트 단위 오프셋으로 쪼개는 fragmenter와 RFC 815 hole-list 재조립기(순서 무작위·중복·겹침 조각 거부, 30초 타임아웃, 메모리 상한)를 구현해 무작위 순서 테스트로 원본 복원을 검증하고, DF 비트 + ICMP Fragmentation Needed로 PMTU를 낮추는 PMTUD와 ICMP 차단 시 블랙홀 현상을 재현한다. | mac |
| NET-B07 | UDP 계층과 포트 디멀티플렉싱 | (로컬 IP, 로컬 포트, 원격 IP, 원격 포트) 정확 일치 → 와일드카드 순 소켓 조회 해시 테이블, 에페메럴 포트 할당(충돌 회피, 범위 랩), 매칭 소켓이 없을 때 ICMP Port Unreachable 생성을 구현하고 연결형(connect된) UDP 소켓이 ICMP 에러를 받는 동작까지 테스트한다(신뢰성 UDP 프로토콜은 see:DIST-A06). | mac |
| NET-B08 | utun으로 맥 커널과 미니 스택 연결 (TUN) | PF_SYSTEM/SYSPROTO_CONTROL 소켓으로 utun 장치를 열고(4바이트 AF 헤더 처리) ifconfig로 점대점 주소를 부여한 뒤, B04~B07 미니 스택을 붙여 맥의 ping·nc -u가 미니 스택과 통신하는지 tcpdump -i utunN으로 확인하고, Linux TUN/TAP(L2 가능)와의 차이를 정리한다. | mac |
| NET-B09 | 소켓 API 수명주기: listen backlog, TIME_WAIT, half-close | socket/bind/listen/accept/connect 서버·클라이언트를 작성해 SO_REUSEADDR 없을 때 TIME_WAIT로 인한 bind 실패, backlog 초과 시 연결 지연, shutdown(SHUT_WR) half-close, 읽지 않은 데이터가 남은 채 close 시 RST 전송, SO_LINGER{1,0} 강제 RST를 각각 재현하고 tcpdump·netstat 상태로 근거를 남긴다. | mac |
| NET-B10 | TCP 바이트 스트림과 메시지 프레이밍 | 길이 접두(4바이트 BE) 프레이밍 송수신 라이브러리를 non-blocking 소켓용으로 작성(short write 이어쓰기, writev, 부분 헤더 수신 상태 유지)하고, 바이트를 1~7바이트 무작위 조각으로 쪼개 전달하는 결함 주입 프록시를 통과시켜 10만 메시지 무결성을 검증한다. | mac |
| NET-B11 | TCP 헤더·옵션과 3-way handshake/종료 관찰 | TCP 헤더와 옵션(MSS, Window Scale, SACK-Permitted, Timestamps, EOL/NOP 패딩)을 파싱하는 C 디코더를 B01 pcap 툴에 추가하고, lo0에서 tcpdump로 잡은 연결 설정·데이터·FIN/ACK 종료·RST 시나리오를 상대 seq/ack로 주석 단 래더 다이어그램으로 재구성한다. | mac |
| NET-B12 | Classic BPF: tcpdump 필터 컴파일과 인터프리터 | tcpdump -d로 'tcp port 80 and host 10.0.0.1' 등 필터의 cBPF 바이트코드를 덤프하고, A/X 레지스터·스크래치 메모리·ld/jeq/ret 명령을 갖춘 cBPF 인터프리터를 C로 구현해 B01 pcap에 적용한 결과를 libpcap(pcap_offline_filter) 결과와 일치시킨다(eBPF 검증기 개념은 see:VM-A07). | mac |
| NET-M01 | TCP 상태 머신 (RFC 9293) 테이블 구현 | (상태, 이벤트: 수신 세그먼트 플래그/앱 호출/타이머) → (다음 상태, 동작) 테이블 기반 FSM을 작성하고 동시 open, 동시 close(CLOSING), TIME_WAIT 2MSL, SYN_RCVD에서 RST, 구 세그먼트 수용성 검사(acceptability test)를 packetdrill 스타일 스크립트 테스트로 검증한다. | mac |
| NET-M02 | 미니 TCP 1: 신뢰 전송과 순서 재조립 | M01 FSM 위에 송신 버퍼(SND.UNA/NXT/WND), 수신 버퍼와 out-of-order 세그먼트 큐, 누적 ACK, 32비트 seq 랩어라운드 비교(SEQ_LT)를 구현하고 B01 와이어(손실 5%, 재정렬, 복제)에서 10MB 전송 후 SHA-256 일치를 검증한다(GBN/SR 기초는 see:COMM-A02). | mac |
| NET-M03 | RTO 추정과 재전송 타이머 (RFC 6298) + Fast Retransmit | SRTT/RTTVAR 갱신, Karn 알고리즘(재전송 세그먼트 표본 제외), 지수 백오프, 최소 RTO, Timestamps 옵션 기반 RTTM을 미니 TCP에 구현하고, 3 dup-ACK fast retransmit 유무에 따른 손실 복구 시간을 지연 변동 링크에서 측정해 spurious retransmission을 관찰한다. | mac |
| NET-M04 | 흐름 제어: 수신 윈도, Window Scaling, Zero-Window Probe | 광고 윈도·window scale 옵션, 수신 앱이 멈췄을 때 zero window → persist 타이머 프로브 → window update 재개, 송수신 양측 SWS 회피(Clark/Nagle 기준)를 미니 TCP에 넣고, 느린 소비자 시나리오에서 데드락 없이 재개됨과 프로브 간격 백오프를 검증한다. | mac |
| NET-M05 | Nagle × Delayed ACK 상호작용과 TCP_NODELAY | write-write-read 요청 패턴의 RPC 클라이언트/서버로 Nagle과 delayed ACK가 겹쳐 생기는 지연(Linux 40ms / macOS delayed_ack 설정)을 재현해 요청당 지연 히스토그램을 그리고, TCP_NODELAY·writev 합치기·TCP_NOPUSH(맥)/TCP_CORK(리눅스)로 각각 해결한 결과를 비교한다. | mac |
| NET-M06 | 혼잡 제어 1: Reno/NewReno (Slow Start, AIMD, Fast Recovery) | 미니 TCP에 cwnd/ssthresh, slow start, congestion avoidance, fast recovery(Reno vs NewReno partial ACK 처리)와 RTO 시 cwnd=1을 구현하고, 병목 대역폭·버퍼 크기를 가진 B01 링크에서 cwnd-시간 그래프와 처리량을 기록해 다중 손실 시 Reno의 성능 붕괴를 보인다(RFC 5681/6582). | mac |
| NET-M07 | SACK 스코어보드와 손실 복구 (RFC 6675, RACK 개념) | SACK 블록 생성(수신측)과 스코어보드·pipe 추정·NextSeg 선택(송신측)을 구현해 버스트 손실(한 윈도 3~10개)에서 NewReno 대비 복구 시간과 처리량을 비교하고, 시간 기반 손실 감지 RACK-TLP가 tail loss를 어떻게 줄이는지 시뮬레이션으로 확인한다. | mac |
| NET-M08 | 혼잡 제어 2: CUBIC vs BBR 이산사건 시뮬레이션 | 병목 링크(대역폭, 전파 RTT, 버퍼 0.25~4 BDP) 이산사건 시뮬레이터에 Reno·CUBIC(W(t)=C(t-K)^3+Wmax)·BBRv1(BtlBw/RTprop 추정, pacing, ProbeBW 게인 주기) 모델을 구현해 처리량, 큐 지연(bufferbloat), RTT가 다른 흐름 간 Jain 공정성, 랜덤 손실 내성을 표와 그래프로 비교한다(데이터센터 CC DCQCN/HPCC는 see:DCN). | mac |
| NET-M09 | 이벤트 루프 서버 심화: 타이머 휠, 백프레셔, C10K | OS-M12/M13 에코 서버(see:OS)를 확장해 kqueue/epoll 추상 리액터에 계층형 타이머 휠(유휴 연결 타임아웃), 연결별 쓰기 버퍼 high/low watermark 백프레셔(EVFILT_WRITE/EPOLLOUT 토글), HTTP/1.1 keep-alive 파서를 넣고 wrk로 1만 동시 연결 처리량·p99 지연·RSS를 측정한다. | mac |
| NET-M10 | netns·veth·bridge·VLAN 토폴로지 구성 | ip netns로 호스트 4개 + 라우터 1개를 만들고 veth 쌍, Linux bridge(vlan_filtering=1, 802.1Q VLAN 10/20 분리), 라우터 네임스페이스의 ip_forward와 VLAN 서브인터페이스로 inter-VLAN 라우팅을 구성하는 재실행 가능한 셸 스크립트를 작성해 ping 매트릭스와 bridge fdb/tcpdump로 검증한다(컨테이너 namespace 전반은 see:VIRT). | **linux** |
| NET-M11 | netfilter/nftables: 훅, conntrack, NAT | M10 토폴로지에서 nftables로 stateful 방화벽(ct state established,related 허용), SNAT masquerade, DNAT 포트 포워딩을 구성하고 nft monitor trace로 prerouting→forward→postrouting 경로를 확인하며, conntrack -L 상태와 C로 만든 5-튜플 미니 conntrack(타임아웃, NAT 역변환, B02 증분 체크섬) 결과를 대조한다. | **linux** |
| NET-M12 | tc qdisc: netem·tbf·fq_codel과 bufferbloat | veth에 tbf로 10Mbit 병목, netem으로 지연/손실/재정렬을 걸고 부하 중 ping RTT를 pfifo vs fq_codel로 비교해 bufferbloat를 수치로 보이며, 토큰 버킷과 CoDel(target 5ms, interval 100ms, sqrt 제어 법칙)을 C로 구현해 같은 트레이스에서 tc 결과와 경향을 대조한다. | **linux** |
| NET-M13 | 소켓 버퍼 튜닝: BDP, autotuning, keepalive, user timeout | netem으로 RTT 10/50/200ms 경로를 만들고 SO_SNDBUF/SO_RCVBUF 고정값 vs tcp_rmem autotuning에서 처리량을 측정해 BDP 공식과 대조하며, ss -tin의 cwnd/rtt/rcv_space를 해석하고 TCP_KEEPIDLE/INTVL/CNT·TCP_USER_TIMEOUT으로 죽은 피어 감지 시간을 설계값대로 맞춘다. | **linux** |
| NET-A01 | sk_buff 모델: headroom, push/pull, clone vs copy | head/data/tail/end 포인터와 skb_reserve/put/push/pull, 참조 카운트 데이터 공유 clone vs 깊은 copy, frags 배열(scatter-gather)을 가진 sk_buff 유사 구조를 C로 구현해 미니 스택의 계층 간 헤더 추가/제거를 무복사로 바꾸고, Lima VM에서 pahole로 실제 struct sk_buff 레이아웃·크기를 대조한다. | mac |
| NET-A02 | 수신 경로 추적: IRQ → NAPI poll → GRO → 프로토콜 | virtio-net에서 tracepoint(napi:napi_poll, net:netif_receive_skb, irq)와 bpftrace로 패킷당 경로·배치 크기·softirq CPU를 수집하고 /proc/net/softnet_stat(dropped, time_squeeze)을 해석하며, NAPI budget/weight와 인터럽트 재활성화 로직을 C 이산사건 모델로 만들어 부하별 인터럽트 수와 지연 트레이드오프를 그린다. | **linux** |
| NET-A03 | GSO/TSO/GRO 오프로드 효과 측정 | veth/virtio에서 ethtool -K로 tso/gso/gro를 켜고 끄며 iperf3 처리량·CPU 사용률·softirq 비율을 측정하고, tcpdump에 보이는 64KB 슈퍼패킷과 실제 와이어 세그먼트의 차이를 설명하며 GSO를 소프트웨어로 분할하는 C 함수(헤더 복제, seq/IP ID/체크섬 갱신)를 작성해 검증한다(NIC 하드웨어 오프로드는 see:NETHW). | **linux** |
| NET-A04 | RSS Toeplitz 해시와 RPS/RFS/XPS | 40바이트 키 Toeplitz 해시를 C로 구현해 Microsoft RSS 검증 벡터와 일치시키고, 인디렉션 테이블 큐 분산 균일도, 대칭(symmetric) 키로 양방향 흐름이 같은 큐에 가는지 검증한 뒤, Lima VM에서 rps_cpus/rps_flow_cnt/xps_cpus 설정 전후 softirq CPU 분포를 비교한다. | mac |
| NET-A05 | XDP 패킷 필터와 BPF 맵 (eBPF) | libbpf + clang -target bpf로 veth에 붙는 XDP 프로그램(특정 UDP 포트 XDP_DROP, 소스 IP별 per-CPU 카운터 맵, ICMP echo XDP_TX 반사)을 작성해 bpftool로 맵을 읽고, 같은 드롭을 nftables로 했을 때와 pps·CPU를 비교하며 검증기가 거부하는 경계 검사 누락 코드를 재현한다(cBPF는 B12, 검증기 개념은 see:VM-A07). | **linux** |
| NET-A06 | tc-BPF L4 로드밸런서 (Katran 스타일) | VIP:port → 백엔드 맵과 5-튜플 해시(일관 해시 Maglev 테이블)로 백엔드를 고르고 목적지 주소를 재작성(bpf_l3_csum_replace/l4_csum_replace)하는 tc ingress 프로그램과 역방향 SNAT 경로를 M10 netns 토폴로지에 붙이고, 백엔드 하나 제거 시 기존 흐름이 유지되는 비율을 측정한다. | **linux** |
| NET-A07 | accept 확장성: SO_REUSEPORT, EPOLLEXCLUSIVE, thundering herd | N워커가 하나의 리슨 소켓을 공유하는 epoll 서버에서 thundering herd(불필요한 wakeup 수)를 측정하고, EPOLLEXCLUSIVE와 SO_REUSEPORT(커널 해시 분산, 워커 재시작 시 연결 유실)를 비교하며, SO_ATTACH_REUSEPORT_CBPF로 CPU 지역성 스티어링을 적용해 워커별 연결 분포와 p99를 기록한다. | **linux** |
| NET-A08 | 제로카피 송수신: sendfile, splice, MSG_ZEROCOPY | 1GB 파일 전송 서버를 read/write, sendfile, splice(pipe 경유), MSG_ZEROCOPY(SO_EE_ORIGIN_ZEROCOPY 완료 통지 처리)로 구현해 처리량·CPU·syscall 수를 비교하고, 작은 메시지에서 zerocopy가 오히려 느린 이유와 버퍼 재사용 시점 규칙을 정리한다(io_uring은 see:LLAT-A03, 커널 바이패스는 see:LLAT-A02). | **linux** |
| NET-A09 | 가상 netdev 드라이버: ndo_start_xmit과 NAPI | veth처럼 쌍으로 연결된 가상 netdevice 커널 모듈을 작성해 ndo_open/stop/start_xmit, 링 버퍼 + NAPI poll 수신, netif_stop_queue/wake_queue 흐름 제어, rtnl_link_stats64·ethtool get_drvinfo를 구현하고 netns 두 개에 넣어 ping/iperf3과 ip -s link 카운터로 검증한다(모듈 기초는 see:ELX, HW NIC 디스크립터는 see:NETHW). | **linux** |
| NET-A10 | 패킷 드롭 디버깅 방법론 (프로덕션 면접형) | netns 토폴로지에 rp_filter 비대칭 경로, MTU 불일치 PMTU 블랙홀, conntrack 테이블 포화, rmem 초과 UDP 드롭, nft 규칙 드롭, ARP 실패를 숨겨 넣는 장애 주입 스크립트를 만들고, ip -s link·nstat·ss -m·/proc/net/snmp·kfree_skb 드롭 사유(bpftrace/dropwatch)만으로 각 원인을 찾아내는 런북을 작성한다. | **linux** |
| NET-A11 | 종합: 유저스페이스 TCP/IP 스택 상호운용 | B08 utun 위에 ARP를 제외한 IP/ICMP/UDP/TCP(M01~M07, A01 skb 모델) 미니 스택을 통합해 맥의 curl이 미니 스택 HTTP 서버에서 100MB를 내려받고 iperf 유사 전송이 손실 주입 하에서도 완료되게 하며, packetdrill 스타일 회귀 테스트 스위트와 tcptrace식 시퀀스 그래프로 동작을 문서화한다. | mac |

### DCN — 데이터센터 네트워크 · AI 패브릭 (Datacenter Networks & AI Fabrics)

AI 학습 클러스터에서는 GPU 간 통신이 스텝 시간의 큰 몫을 차지해, 토폴로지·로드밸런싱·무손실 이더넷·혼잡 제어·collective 알고리즘을 이해하는 엔지니어 수요가 NVIDIA·Broadcom·Arista·Meta·Google 인프라 전반에서 크다. 이 트랙은 Clos/fat-tree 수학과 ECMP부터 RDMA verbs(Soft-RoCE), PFC/DCQCN/HPCC, all-reduce α-β 모델, NCCL·SHARP·UEC까지 시뮬레이터와 실제 코드로 구현해 수치로 설명할 수 있게 만든다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| DCN-B01 | DC 패브릭 지표: 지연 분해와 Bisection Bandwidth | 패킷 하나의 종단 지연을 직렬화(크기/링크속도)·전파(m당 5ns)·스위치 처리·큐잉으로 분해하는 계산기를 Python으로 만들어 25/100/400/800G 링크와 홉 수별 표를 출력하고, 임의 그래프의 bisection bandwidth를 소규모 그래프에서 brute-force 최소 절단으로 계산해 검증한다. | mac |
| DCN-B02 | Leaf-Spine(2단 Clos) 설계 계산기 | 스위치 radix k, 포트 속도, 서버 NIC 속도, 목표 oversubscription을 입력받아 leaf/spine 개수·최대 서버 수·uplink 수·케이블 수를 출력하는 계산기를 구현하고, 64포트 400G 스위치로 3:1과 1:1 설계를 비교해 손으로 푼 값과 일치함을 확인한다. | mac |
| DCN-B03 | k-ary Fat-tree 생성기와 경로 수 검증 | k-ary fat-tree(pod/edge/aggregation/core)를 그래프(networkx 또는 인접 리스트)로 생성해 호스트·스위치·링크 수 공식을 assert로 검증하고, 같은 edge/같은 pod/다른 pod 호스트 쌍의 최단 경로 수(1, k/2, (k/2)²)를 BFS로 세어 확인한다. | mac |
| DCN-B04 | Oversubscription과 트래픽 패턴: Max-flow 실측 | B03 생성기로 만든 fat-tree와 3:1 oversubscribed leaf-spine에 random permutation·all-to-all·rack-local 트래픽 행렬을 걸고 multi-commodity를 단순화한 max-flow(또는 ECMP 균등분할 부하 계산)로 링크 최대 부하와 호스트당 달성 처리량을 구해, oversubscription 비율과 패턴별 병목을 표로 정리한다. | mac |
| DCN-B05 | ECMP 해시: 5-tuple → Next-hop 선택 | 5-tuple을 CRC32/CRC16/XOR-fold 해시 후 modulo N으로 next-hop을 고르는 ECMP 선택기를 C로 구현해 랜덤·순차 포트 플로우에서 버킷 분포의 카이제곱 값을 비교하고, elephant 플로우 M개가 N개 경로에서 충돌할 확률(생일 문제)을 해석식과 몬테카를로로 맞춘다. | mac |
| DCN-B06 | ECMP 극성화(Polarization)와 Resilient Hashing | leaf→spine→leaf 2단에서 두 단이 같은 해시 함수·시드를 쓰면 일부 경로만 쓰이는 polarization을 시뮬레이션으로 재현하고 스위치별 seed/필드 선택으로 해소하며, next-hop 하나가 죽을 때 modulo 해시와 resilient(consistent) 해시 테이블에서 재배치되는 플로우 비율을 측정한다. | mac |
| DCN-B07 | DC의 BGP: RFC 7938 eBGP 패브릭 시뮬레이션 | leaf-spine에 RFC 7938 방식 ASN(spine 공용 ASN, leaf별 ASN)을 배정한 path-vector 라우팅 시뮬레이터를 Python으로 구현해 prefix 광고·AS_PATH loop 방지·multipath 선택을 보이고, 링크 장애 시 withdraw 전파 라운드 수와 BFD 유무에 따른 수렴 시간을 비교한다. | mac |
| DCN-B08 | Incast: 다대일 동기 전송과 Goodput 붕괴 | N개 서버가 동시에 S바이트 블록을 한 클라이언트로 보내는 이산 이벤트 시뮬레이터(공유 출력 큐 B바이트, 단순화된 TCP 윈도우·RTO_min)를 만들어 N에 따른 goodput 곡선을 그리고, RTO_min 200ms→1ms·버퍼 증가·응답 크기 분할이 붕괴 지점을 어떻게 옮기는지 보인다 (TCP 기본 동작은 see:NET). | mac |
| DCN-B09 | RDMA 개념과 Verbs 객체: PD·MR·QP·CQ | Lima VM에 rdma_rxe 디바이스를 만들고 ibv_devinfo -v로 속성(max_qp, max_mr_size 등)을 읽은 뒤, ibv_alloc_pd → ibv_reg_mr(접근 플래그별) → ibv_create_cq → ibv_create_qp를 호출하는 C 프로그램을 작성해 각 객체의 핸들·lkey/rkey를 출력하고 잘못된 접근 플래그로 원격 쓰기가 거부되는 경우를 정리한다. | **linux** |
| DCN-B10 | RC QP 상태 전이와 SEND/RECV Ping-pong | 두 프로세스가 TCP 소켓으로 QPN·GID·PSN을 교환한 뒤 ibv_modify_qp로 RESET→INIT→RTR→RTS 전이를 수행하고 SEND/RECV ping-pong을 구현해 왕복 지연 히스토그램을 출력하며, RECV를 미리 post하지 않았을 때의 RNR NAK·재시도 동작을 관찰한다. | **linux** |
| DCN-B11 | One-sided RDMA WRITE/READ vs SEND | B10 코드에 원격 주소·rkey 교환을 추가해 RDMA WRITE, WRITE_WITH_IMM, READ를 구현하고 메시지 크기 8B~1MB에서 SEND와 지연·처리량을 측정한 뒤 perftest(ib_write_lat/ib_read_bw)와 대조하고, READ가 WRITE보다 느린 이유(왕복, outstanding READ 제한)를 설명한다. | **linux** |
| DCN-M01 | RDMA 전송 유형: RC·UC·UD와 연결 확장성 | UD QP로 Address Handle(GRH 포함)을 써서 데이터그램 SEND를 구현해 MTU 제한·순서 미보장을 확인하고, N노드×P프로세스 full-mesh RC 연결의 QP 수와 RNIC 컨텍스트 메모리를 계산해 UD·XRC·DCT(개념) 대안이 얼마나 줄이는지 표로 비교한다. | **linux** |
| DCN-M02 | RDMA 성능 패턴: Unsignaled·Inline·Doorbell Batching | 작은 메시지 WRITE 스트림에서 매 WR signaled vs N개마다 signaled, IBV_SEND_INLINE, WR 체인으로 doorbell 한 번에 여러 WR post하기를 단계별로 적용해 메시지율 변화를 측정하고, 실 RNIC에서 PCIe MMIO/DMA 읽기 횟수 관점의 비용 모델(see:PCIE)로 개선 이유를 설명한다. | **linux** |
| DCN-M03 | RoCEv2 패킷 해부: BTH·RETH·AETH와 ICRC | RoCEv2 패킷 빌더/파서를 C로 구현해 BTH(opcode, P_Key, dest QP, PSN, AckReq), RETH(VA, rkey, len), AETH(syndrome, MSN)를 인코딩·디코딩하고 ICRC(불변 필드 마스킹 후 CRC32)를 계산해 테스트 벡터와 대조하며, Lima rxe 인터페이스에서 tcpdump로 잡은 pcap을 같은 파서로 디코드해 WRITE 첫/중간/끝 패킷의 opcode를 확인한다 (UDP/IP 파싱 기초는 see:COMM-A04). | mac |
| DCN-M04 | PFC(802.1Qbb) 무손실 이더넷과 헤드룸 계산 | 케이블 길이·링크 속도·MTU·PAUSE 응답 시간으로 PFC 헤드룸을 계산하는 공식을 구현해 100G/400G·100m 링크 값을 구하고, 2단 스위치 이산 이벤트 시뮬레이터에서 우선순위별 XOFF/XON 임계값을 적용해 incast 시 무손실을 확인한 뒤 피해자 플로우의 HOL blocking·congestion spreading을 측정한다. | mac |
| DCN-M05 | PFC 데드락: 순환 버퍼 의존성 탐지 | 링크 장애 후 재라우팅으로 leaf-spine-leaf-spine 경로가 생기는 시나리오를 시뮬레이션해 PFC 데드락을 재현하고, (스위치,입력포트,우선순위) 노드의 buffer dependency graph에서 사이클을 DFS로 탐지하는 검사기를 구현해 up-down 라우팅·홉별 우선순위 상승·PFC watchdog 대책의 효과를 비교한다. | mac |
| DCN-M06 | ECN 마킹과 DCTCP | 단일 병목 큐 시뮬레이터에 임계값 K 즉시 마킹과 DCTCP 송신자(α ← (1−g)α + gF, cwnd ← cwnd(1−α/2))를 구현해 Reno 대비 큐 길이 시계열과 처리량을 비교하고, RFC 8257의 K 하한(K > C·RTT/7) 근거를 실험으로 확인한다 (Reno/CUBIC 구현은 see:NET, 스위치 WRED/ECN 마킹 HW는 see:NETHW). | mac |
| DCN-M07 | DCQCN: RoCE 혼잡 제어 시뮬레이터 | RED식 확률 ECN 마킹 스위치, 50µs당 최대 1개 CNP를 내는 NP, rate decrease(Rc ← Rc(1−α/2))·fast recovery·additive/hyper increase를 갖는 RP를 이산 이벤트로 구현해 8→1 incast에서 큐 길이·송신률 수렴·공정성을 그리고, Kmin/Kmax/Pmax와 타이머 파라미터 민감도와 PFC와의 상호작용을 정리한다. | mac |
| DCN-M08 | 지연 기반 혼잡 제어: TIMELY와 Swift | M07 시뮬레이터에 TIMELY(RTT gradient 기반 증감, Tlow/Thigh)와 Swift(fabric/host 지연 분리, target delay를 홉 수·플로우 수로 스케일링하는 AIMD)를 추가해 incast와 장거리 혼합 시나리오에서 큐 지연 p99·처리량·공정성을 DCQCN과 비교하고, 하드웨어 타임스탬프 정확도가 왜 전제인지 설명한다. | mac |
| DCN-M09 | HPCC: INT 기반 정밀 혼잡 제어 | 홉마다 (qlen, txBytes, timestamp, B)를 패킷에 추가하는 스위치 모델과 ACK로 받은 INT로 링크 이용률 U = qlen/(B·T) + txRate/B를 계산해 목표 η=0.95로 윈도우를 조정하는 HPCC 송신자를 구현하고, DCQCN 대비 큐 길이와 수렴 속도를 같은 시나리오에서 비교한다. | mac |
| DCN-M10 | ECMP 너머의 부하 분산: Flowlet·Packet Spraying | leaf-spine 시뮬레이터에 플로우 ECMP, flowlet(유휴 간격 δ > 경로 지연 차) 재선택, 패킷 단위 스프레이+수신측 재정렬 버퍼를 구현해 웹서치/데이터마이닝 플로우 크기 분포에서 FCT p50/p99와 재정렬 깊이를 비교하고, 비대칭(링크 장애) 상황에서 CONGA식 혼잡 인지 선택의 이점을 보인다. | mac |
| DCN-M11 | Dragonfly 토폴로지와 UGAL 적응 라우팅 | (a, p, h) 파라미터로 balanced dragonfly(a=2p=2h)를 생성해 그룹·라우터·노드 수와 직경을 검증하고, uniform·adversarial(그룹 i→i+1) 트래픽에서 minimal, Valiant, UGAL 라우팅의 처리량과 지연을 사이클 시뮬레이터로 비교해 fat-tree 대비 케이블·스위치 비용을 정리한다. | mac |
| DCN-M12 | Collective α-β 비용 모델: Ring·Tree·Halving-Doubling | all-reduce·reduce-scatter·all-gather·broadcast에 대해 ring, binary/double binary tree, recursive doubling, recursive halving-doubling(Rabenseifner)의 비용식을 유도해 계산기로 만들고, p=8~1024와 메시지 8B~1GB에서 최적 알고리즘 지도와 교차점을 그린다 (ring all-reduce 구현 기초는 see:GPUSYS-A02). | mac |
| DCN-M13 | Recursive Halving-Doubling All-Reduce 구현 | p개 스레드(또는 프로세스+소켓)로 recursive halving reduce-scatter + recursive doubling all-gather를 구현해 float 배열 합을 참조 결과와 비교 검증하고, p가 2의 거듭제곱이 아닐 때의 사전 축약 단계를 추가한 뒤 단계별 전송 바이트를 계측해 M12 비용식과 일치시킨다. | mac |
| DCN-A01 | 계층적(2D) All-Reduce와 Rail-optimized 토폴로지 | 노드 내 reduce-scatter(NVLink) → 레일별 노드 간 all-reduce(NIC) → 노드 내 all-gather 3단계 알고리즘을 구현·시뮬레이션해 flat ring 대비 시간을 비교하고, GPU i가 레일 i 스위치에 붙는 rail-optimized 배치와 일반 ToR 배치에서 노드 간 트래픽 홉 수·스위치 tier를 계산한다 (NVLink/PCIe 대역폭 값은 see:GPUSYS-A01). | mac |
| DCN-A02 | MoE용 All-to-All: 알고리즘과 패브릭 부하 | 토큰 라우팅 행렬(top-k, 불균형 계수 조절)에서 GPU 쌍별 바이트를 계산해 직접 pairwise 교환, 노드 내 집약 후 노드 간 전송하는 계층적 all-to-all의 시간을 α-β 모델로 비교하고, fat-tree 시뮬레이터에 트래픽을 실어 incast성 핫스팟과 링크 부하 분포를 측정한다 (병렬화별 통신량 산정은 see:AISYS). | mac |
| DCN-A03 | NCCL 내부 구조: 토폴로지 탐지·채널·프로토콜 | NCCL_DEBUG=INFO 로그와 NCCL_TOPO_DUMP_FILE 형식의 토폴로지 XML(공개 샘플 또는 직접 작성)을 파싱해 GPU-NIC-PCIe 스위치 트리, 채널별 ring 순서, 선택된 algo/proto를 표로 재구성하는 Python 도구를 만들고, nccl-tests의 algbw→busbw 변환식(all-reduce 2(n−1)/n 등)을 구현해 공개 결과표를 해석한다. | mac |
| DCN-A04 | nccl-tests 실측과 α-β 모델 대조 | 클라우드 4~8 GPU 인스턴스에서 all_reduce_perf/all_gather_perf/alltoall_perf를 8B~4GB로 스윕하고 NCCL_ALGO(Ring/Tree)·NCCL_PROTO를 바꿔 busbw 곡선을 얻은 뒤, M12 α-β 모델에 측정 α·β를 피팅해 소형 메시지 지연 구간과 대역폭 포화 구간의 오차 원인을 정리한다. | **gpu** |
| DCN-A05 | Scale-up vs Scale-out: NVLink·NVSwitch·UALink vs 이더넷/IB | NVL72급 NVSwitch 도메인, UALink 스케일업, 400/800G RoCE·InfiniBand 스케일아웃을 GPU당 대역폭·도메인 크기·홉 지연·스위치 수·전력 파라미터로 표현한 모델을 만들고, tensor parallel 그룹이 스케일업 도메인 안/밖에 있을 때 층당 all-reduce 시간 차를 계산해 도메인 크기 선택 근거를 제시한다 (링크 대역폭 계산은 see:GPUSYS-A01). | mac |
| DCN-A06 | InfiniBand: 크레딧 흐름 제어와 Subnet Manager 라우팅 | VL별 크레딧 기반 링크 흐름 제어를 시뮬레이션해 PFC(pause 기반)와 버퍼 요구량을 비교하고, fat-tree 그래프에서 SM처럼 LID를 할당하고 min-hop과 fat-tree(목적지 기반 분산) 라우팅으로 스위치별 LFT를 계산하는 mini SM을 구현해 모든 쌍 경로의 도달성·데드락 자유(채널 의존 그래프 무사이클)·링크 부하 균형을 검증한다. | mac |
| DCN-A07 | 손실 복구: Go-Back-N(RoCE) vs Selective Repeat(UEC) | 손실률·RTT·BDP를 파라미터로 go-back-N, selective repeat+SACK, 패킷 트리밍(헤더만 전달해 빠른 NACK) 송수신 상태기계를 시뮬레이터로 구현해 손실률 10⁻⁶~10⁻²에서 goodput을 비교하고, 무손실(PFC) 의존을 버리는 UEC 설계 동기(out-of-order 배치, 일시적 연결)를 정리한다. | mac |
| DCN-A08 | SHARP: 스위치 내 리덕션(In-network Aggregation) | fat-tree 위 집계 트리(leaf→spine 스위치에서 부분합)를 시뮬레이션하는 all-reduce를 구현해 호스트당 송신량이 2(n−1)/n·S에서 S로 줄어드는 것을 확인하고, ring/tree 대비 시간·스위치 메모리 한계로 인한 청크 분할·부동소수 합산 순서(결정성)를 분석한다. | mac |
| DCN-A09 | 패브릭 텔레메트리: INT·카운터로 혼잡/PFC Storm 진단 | 시뮬레이터가 내보내는 홉별 INT 메타데이터(스위치 ID, 큐 길이, 지연)와 포트 카운터(ECN 마킹, pause 송수신, drop)를 수집해 핫스팟 링크·혼잡 근원(root) 포트·PFC storm(고장 NIC의 지속 pause)을 자동 판정하는 분석기를 구현하고, 의도적으로 주입한 3가지 장애를 정확히 지목하는지 검증한다. | mac |
| DCN-A10 | 광 링크 기초: 모듈·도달거리·전력 예산 | 송신 출력·수신 감도·광섬유 손실(dB/km)·커넥터 손실·마진으로 링크 전력 예산 계산기를 만들어 400G-DR4/800G-DR8/FR4 사례를 검증하고, 32K GPU 클러스터의 트랜시버 수·모듈 전력(DSP vs LPO vs CPO)·DAC/AEC 대체 가능 구간을 산정한다 (PAM4/RS-FEC BER는 see:NETHW, see:SIV). | mac |
| DCN-A11 | 설계 면접: 수만 GPU 학습 클러스터 패브릭 | 16K~32K GPU 학습 클러스터에 대해 스케일업 도메인, rail-optimized 3단 Clos(또는 2단+멀티플레인) 스위치·광모듈 수, RoCE(DCQCN/PFC) vs IB vs UEC 선택, 장애 도메인과 링크 장애율로 인한 job 중단 빈도를 수치로 산출한 설계 문서와 계산 스크립트를 작성하고, 한 스텝의 통신 시간(DP all-reduce + TP + EP all-to-all) 예산을 A01·A02 모델로 검증한다. | mac |

### NETHW — SmartNIC · DPU · 스위치 ASIC (SmartNIC, DPU & Switch ASIC)

SmartNIC·DPU·스위치 ASIC은 데이터센터와 AI 패브릭의 패킷을 라인레이트로 처리하는 하드웨어다. NVIDIA(Mellanox/BlueField), Broadcom, Arista, Intel, AMD(Pensando), Marvell 면접에서는 pps 예산 계산, 디스크립터/큐/doorbell 같은 NIC 데이터패스, TCAM·LPM·match-action 파이프라인, 버퍼·스케줄러, 오프로드와 FEC를 '직접 모델링해 봤는지'를 묻는다. 펌웨어·SSD 경험을 NIC/DPU 펌웨어로 확장하는 트랙이다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| NETHW-B01 | Line-rate와 pps 계산: 64B 프레임, preamble·IFG | 10/25/100/400/800GbE에서 프레임 크기(64B~9KB)별 최대 pps와 패킷당 ns·코어 사이클 예산(preamble 8B + IFG 12B 포함)을 계산하는 도구를 작성하고, '100G 64B 라인레이트 = 148.8Mpps, 패킷당 6.72ns'를 유도해 3GHz 코어 몇 개가 필요한지 답한다. | mac |
| NETHW-B02 | NIC 데이터패스 해부: 와이어에서 소켓까지 | MAC FCS 검사→필터→큐 선택→RX 디스크립터 fetch→DMA write→writeback→MSI-X→NAPI poll 순서를 이벤트 로그로 출력하는 Python mock NIC 모델을 작성하고, RX/TX 1패킷당 MMIO·DMA read/write 횟수를 세어 배치(batch) 크기에 따라 어떻게 줄어드는지 표로 만든다. | mac |
| NETHW-B03 | Ethernet MAC: FCS, 패딩, runt/giant와 MIB 카운터 | TX(60B 패딩 + CRC-32 FCS 부착)와 RX(FCS 오류, runt <64B, giant >MTU/jumbo, VLAN 태그 길이 보정) 검사를 하는 C MAC 모델을 만들고, 오류 프레임 주입 테스트로 rx_crc_errors/rx_length_errors 등 MIB 카운터가 정확히 증가하는지 검증한다 (프레임 파싱 기초는 see:COMM-A04). | mac |
| NETHW-B04 | NIC 레지스터 맵 mock: CTRL/STATUS, ICR/IMS/IMC | e1000 스타일 mock BAR(CTRL, STATUS, ICR read-to-clear, IMS write-1-to-set, IMC write-1-to-clear, RCTL/TCTL)를 C로 구현하고, 소프트 리셋→링크 대기→인터럽트 마스크 설정→RX/TX enable 드라이버 시퀀스와 'ICR을 두 번 읽으면 이벤트를 잃는' 버그를 재현하는 테스트를 작성한다. | mac |
| NETHW-B05 | 멀티큐 TX/RX 링과 Tail Doorbell 배치 | COMM-A08의 단일 디스크립터 링을 N개 TX/RX 큐로 확장해 큐별 head/tail, tail doorbell MMIO, 'xmit_more'식 doorbell 지연 배치를 구현하고, 배치 크기별 doorbell 횟수와 링 가득 참(netif_stop_queue/wake) 처리를 테스트한다 (기초는 see:COMM-A08). | mac |
| NETHW-B06 | Completion Queue와 Split Queue 모델 | (1) 같은 링에 DD 비트 writeback하는 방식과 (2) 버퍼 posting 큐와 completion 큐를 분리하고 CQE에 generation bit를 둔 split queue(mlx5/gve DQO 스타일)를 모두 구현해, 순서 밖 완료·CQ wrap·CQE 압축을 처리하는 드라이버 poll 루프를 테스트한다 (NVMe phase tag는 see:PCIE-M01). | mac |
| NETHW-B07 | 체크섬 오프로드: CHECKSUM_PARTIAL/COMPLETE와 증분 갱신 | TX csum_start/csum_offset 방식(CHECKSUM_PARTIAL)으로 L4 체크섬을 채우는 NIC 엔진과 RX 전체 1의 보수 합을 돌려주는 CHECKSUM_COMPLETE 검증 경로를 C로 구현하고, NAT로 IP/포트를 바꿀 때 RFC 1624 증분 갱신 결과가 전체 재계산과 일치하는지 랜덤 테스트한다 (헤더 체크섬 기초는 see:COMM-A04). | mac |
| NETHW-B08 | 인터럽트 모더레이션: ITR, rx-usecs/rx-frames, adaptive | Poisson/버스트 트래픽 생성기와 NIC 인터럽트 coalescing 모델(타이머·프레임 임계·absolute vs packet timer)을 만들고 설정별 인터럽트/초·평균·p99 지연을 측정한 뒤, 부하에 따라 ITR을 바꾸는 adaptive 알고리즘(DIM 스타일)을 구현해 고정값 대비 개선을 보인다. | mac |
| NETHW-B09 | Linux에서 NIC 관찰: ethtool -l/-g/-k/-c/-S | Linux VM의 virtio-net/veth에서 ethtool -l(채널), -g(링 크기), -k(오프로드 플래그), -c(coalescing), -S(통계)와 /proc/interrupts, /sys/class/net/*/queues를 수집하는 스크립트를 작성하고, 오프로드를 끄고 켤 때(tso/gro off) iperf3 처리량·CPU 사용률 변화를 기록해 각 항목을 B02 모델 단계에 매핑한다. | **linux** |
| NETHW-B10 | 스위치 ASIC 기본: L2 MAC learning, aging, flooding, VLAN | 포트별 VLAN 멤버십(access/trunk, tagged/untagged)을 가진 L2 스위치 모델을 C로 구현해 source MAC 학습, aging 타이머, unknown unicast/broadcast flooding, MAC move 감지를 처리하고, 패킷 시나리오 스크립트로 기대 egress 포트 집합을 검증한다. | mac |
| NETHW-B11 | 패킷 파서: Parse Graph와 PHV | Ethernet→VLAN(QinQ)→IPv4/IPv6→TCP/UDP→VXLAN→inner Ethernet까지의 parse graph를 (상태, lookahead 필드 값)→다음 상태 테이블로 정의하는 table-driven C 파서를 작성하고, 추출 결과를 고정 슬롯 PHV에 담아 pcap 샘플로 검증하며 최대 파싱 깊이·예외(trap) 경로를 처리한다. | mac |
| NETHW-B12 | Ethernet PCS: 64b/66b 인코딩, 스크램블러, block lock | 64b/66b 인코더(sync header 01/10, control block type), self-synchronizing 스크램블러(x^58+x^39+1)/디스크램블러, 그리고 sync header로 block lock을 획득·상실하는 상태기계를 C로 구현하고 비트 슬립·오류 주입 테스트로 lock 획득에 필요한 블록 수와 hi_ber 판정을 검증한다. | mac |
| NETHW-M01 | TSO 세그멘테이션과 LRO 병합 엔진 | MSS·L2/L3/L4 헤더 길이(VXLAN 내부 오프셋 포함)를 담은 TX 컨텍스트 디스크립터를 받아 큰 버퍼를 세그먼트로 자르는 TSO 엔진(헤더 복제, IP ID 증가, TCP seq 진행, FIN/PSH는 마지막만, 체크섬 재계산)과 RX LRO 병합기(같은 flow·연속 seq·옵션 동일 조건)를 구현하고, LRO가 포워딩에서 문제를 일으키는 이유를 테스트로 보인다 (커널 GSO/GRO는 see:NET). | mac |
| NETHW-M02 | RSS 간접 테이블(RETA)과 Flow Director/ntuple 필터 | Toeplitz 해시(see:NET) 결과의 하위 비트로 128/512엔트리 RETA를 인덱싱하는 큐 선택기와 RSS보다 우선하는 5-tuple perfect-match(ntuple) 필터 테이블을 구현하고, 큐 불균형 측정→RETA 재가중 알고리즘과 aRFS(앱 코어로 flow 고정) 시나리오를 시뮬레이션한다. | mac |
| NETHW-M03 | TCAM 시뮬레이터: value/mask, 우선순위, range expansion | value/mask/priority 엔트리와 priority encoder를 가진 TCAM 모델을 C로 구현하고, 포트 범위 [lo,hi]를 최소 prefix 집합으로 확장하는 알고리즘(최악 2W-2개)을 작성해 5-tuple ACL 규칙 세트의 TCAM 엔트리 수와 선형 참조 매처 대비 결과 일치를 검증한다. | mac |
| NETHW-M04 | 하드웨어 LPM: DIR-24-8, multibit trie vs TCAM | DS-M13의 이진/Patricia 트라이를 출발점으로 DIR-24-8(DPDK rte_lpm 스타일, 최대 2회 접근)과 stride 가변 multibit trie를 구현하고, 동일 IPv4 라우팅 테이블에서 조회 메모리 접근 수·메모리 사용량·업데이트 비용을 TCAM(prefix 길이 순 정렬) 방식과 표로 비교한다 (기초는 see:DS-M13). | mac |
| NETHW-M05 | ASIC exact-match 테이블: bucketed cuckoo + stash | DS-A07의 cuckoo 해싱을 4-way bucket × 2~4 해시 뱅크 + 소형 stash(CAM)로 확장한 exact-match 테이블을 구현하고, 달성 가능한 최대 부하율·삽입 시 축출 경로 길이 분포·stash 넘침 확률을 측정해 d-left 해싱과 비교한다 (기초는 see:DS-A07). | mac |
| NETHW-M06 | RMT match-action 파이프라인과 stage 할당 | PHV를 입력으로 받는 N-stage match-action 시뮬레이터(stage별 테이블·action·PHV 수정)를 구현하고, 테이블 간 match/action/successor 의존성 그래프에서 stage 자원(SRAM/TCAM 블록, PHV 컨테이너) 제약을 만족하도록 테이블을 배치하는 greedy 컴파일러를 작성해 L3 라우터+ACL 프로그램의 필요 stage 수를 구한다. | mac |
| NETHW-M07 | P4_16 L3 라우터: bmv2 simple_switch와 P4Runtime | parser·ipv4_lpm 테이블·TTL 감소·체크섬 갱신·ECMP 해시 액션을 가진 P4_16 프로그램을 p4c로 컴파일해 veth/netns 토폴로지의 bmv2 simple_switch_grpc에서 실행하고, Python P4Runtime 클라이언트로 엔트리를 설치한 뒤 scapy 패킷으로 포워딩·TTL 만료·미스 시 drop을 검증한다. | **linux** |
| NETHW-M08 | 공유 패킷 버퍼: Dynamic Threshold와 incast 흡수 | 포트/큐별 정적 분할 vs Choudhury-Hahne dynamic threshold(T = α·(B − 사용량)) 공유 버퍼 시뮬레이터를 작성하고, N:1 incast 버스트와 장기 혼잡 큐가 섞인 시나리오에서 α값별 drop률·버퍼 점유·다른 큐 기아(starvation)를 측정한다 (incast·PFC 패브릭 측면은 see:DCN). | mac |
| NETHW-M09 | AQM: WRED와 ECN 마킹 | EWMA 평균 큐 길이, min/max threshold, max_p, 카운트 보정을 포함한 RED/WRED(트래픽 클래스별 프로파일)와 ECN-capable 패킷은 drop 대신 CE 마킹하는 경로를 구현하고, DCTCP식 즉시 step 마킹(K threshold)과 큐 길이 진동·지연을 비교한다 (DCQCN 혼잡제어는 see:DCN). | mac |
| NETHW-M10 | Egress 스케줄러: Strict Priority + DWRR + Token Bucket | SP 위에 DWRR(quantum·deficit)을 얹은 계층형 스케줄러와 큐별/포트별 token bucket shaper(CIR/CBS), 그리고 policer(single-rate three-color)를 C로 구현하고, 가변 패킷 크기 트래픽에서 설정 가중치 대비 실제 대역폭 비율·최대 지연을 측정해 WRR 대비 공정성을 보인다. | mac |
| NETHW-M11 | virtio-net 데이터패스: net header, packed ring, 알림 억제 | PLAT-M06의 split vring을 기반으로 virtio_net_hdr(flags, gso_type, csum_start/offset, num_buffers)와 mergeable RX 버퍼, packed virtqueue(avail/used wrap counter), VIRTIO_F_EVENT_IDX 알림 억제를 구현하고, 두 프로세스 간 공유 메모리로 패킷당 kick 횟수가 얼마나 줄어드는지 측정한다 (vring 기초는 see:PLAT-M06, 장치 에뮬레이션은 see:VIRT). | mac |
| NETHW-M12 | PAM4와 FEC 수학: pre-FEC BER → post-FEC 프레임 손실률 | NRZ vs PAM4 Gray 매핑·1/(1+D) precoding의 오류 전파 모델과, RS(528,514) KR4·RS(544,514) KP4에 대해 pre-FEC BER→심볼 오류율→코드워드 정정 실패 확률(이항 꼬리)→frame loss ratio를 계산하는 도구를 작성하고 Monte Carlo로 검증, 'KP4는 pre-FEC BER 2.4e-4를 견딘다'를 유도한다 (PRBS/BER 측정은 see:SIV-M06). | mac |
| NETHW-A01 | vhost-user와 vDPA: virtio 데이터패스 오프로드 | vhost-user 메시지(SET_MEM_TABLE, SET_VRING_ADDR/KICK/CALL)를 받아 GPA→HVA 메모리 테이블 변환으로 게스트 vring을 직접 처리하는 백엔드 프로세스를 작성하고(kick/call은 pipe로 대체), 같은 ring 레이아웃을 NIC가 DMA로 처리하는 vDPA와 vhost-net/SR-IOV를 라이브 마이그레이션·성능 관점에서 비교 정리한다. | mac |
| NETHW-A02 | OVS 아키텍처: megaflow 캐시와 flow offload | OpenFlow 다중 테이블 slow path, wildcard megaflow 캐시(tuple space search), exact-match microflow 캐시(EMC)로 구성된 미니 OVS를 구현해 트래픽 분포별 캐시 적중률·조회 비용을 측정하고, megaflow 엔트리를 tc flower 스타일 HW 규칙으로 내리는 오프로드 정책(무엇을, 언제 내리고 회수할지)을 설계한다. | mac |
| NETHW-A03 | SR-IOV eswitch: VF representor와 PF↔VF mailbox | PCIE-A06의 VF RID 모델 위에 VF별 MAC/VLAN anti-spoof 필터, VF 간·업링크 포워딩, representor 포트로의 미스 패킷 trap(switchdev 모드)을 가진 eswitch 모델과, VF가 MAC 설정·멀티캐스트 요청을 보내는 PF↔VF mailbox(메시지·ACK·타임아웃·악성 VF 거부)를 구현한다 (SR-IOV 기초는 see:PCIE-A06). | mac |
| NETHW-A04 | RDMA NIC 엔진 내부: QP 컨텍스트, MTT/MPT, PSN | WQE fetch→MPT(lkey/rkey 권한)·MTT(가상→물리 페이지) 변환→패킷 생성, 수신 측 PSN 검사·ACK/NAK·go-back-N 재전송을 하는 RNIC 모델을 C로 구현하고, QP 컨텍스트 캐시 미스가 많은 QP 수에서 성능을 떨어뜨리는 현상을 캐시 시뮬레이션으로 보인다 (verbs·RoCE 혼잡제어는 see:DCN). | mac |
| NETHW-A05 | 인라인 IPsec ESP 오프로드: SA 조회와 anti-replay | SPI→SA 해시 조회, ESP 헤더/트레일러(패딩·next header), AES-GCM(IV=seq 기반, AAD=SPI|seq) 암·복호화, 64/1024비트 sliding window anti-replay를 가진 인라인 엔진을 구현하고, crypto-only vs full packet offload에서 드라이버·NIC가 각각 맡는 일과 seq 오버플로·rekey 처리를 테스트한다 (AEAD 기초는 see:SEC-B09). | mac |
| NETHW-A06 | kTLS TX 오프로드와 재전송 resync | TLS 1.3 레코드 프레이밍(record seq→nonce)을 TCP 스트림 위에 올리고, NIC가 순서대로 들어오는 세그먼트만 인라인 암호화하는 모델에서 재전송·순서 밖 세그먼트가 오면 드라이버가 레코드 시작 위치를 찾아 resync 컨텍스트를 내려주는 경로를 구현해 모든 경우 출력 암호문이 소프트웨어 kTLS와 일치함을 검증한다. | mac |
| NETHW-A07 | RS(544,514) KP4 FEC 디코더 구현 | MEM-A05의 GF(2^8) RS를 GF(2^10)·t=15로 확장해 인코더, 신드롬, Berlekamp-Massey, Chien search, Forney를 C로 구현하고, 무작위·버스트 심볼 오류 주입으로 15개까지 정정·16개 이상 검출(uncorrectable 플래그)을 확인한 뒤 400GbE 코드워드 시간과 디코더 지연 예산을 계산한다 (GF 기초는 see:MEM-A05). | mac |
| NETHW-A08 | 패킷 프로세서 마이크로아키텍처 예산: 버스 폭·클럭·65B 문제 | 12.8T/25.6T/51.2T 스위치와 400G NIC에 대해 클럭·버스 폭(64B/128B/256B)·파이프라인 수 조합별 처리 가능 pps를 계산하는 도구를 작성하고, 65B 패킷이 버스 2비트를 쓰는 worst-case 효율 저하와 패킷 패킹/세그먼트 버스 해법, 테이블 조회 메모리 대역폭 요구를 정리해 설계안을 정당화한다. | mac |
| NETHW-A09 | 스위치 SDK 테이블 프로그래밍: make-before-break와 warm boot | SAI 스타일 객체(route, next-hop, ECMP group) 위에 shadow 테이블과 HW 테이블 모델을 두고, ECMP 멤버 교체·경로 변경을 make-before-break 순서(참조 카운트·사용 중 객체 삭제 금지)로 적용하는 드라이버와, 프로세스 재시작 후 HW 테이블을 읽어 원하는 상태와 diff·재조정하는 warm boot 로직을 구현해 업데이트 중 블랙홀 패킷 0을 검증한다. | mac |
| NETHW-A10 | NIC 펌웨어 인터페이스: Admin Queue, health, 리셋 복구 | mock NIC에 펌웨어 admin queue(명령 디스크립터·opcode·완료 상태·버전 협상)와 heartbeat 레지스터를 두고, 명령 타임아웃·펌웨어 크래시 주입 시 드라이버가 health 이상 감지→큐 정지→덤프 수집→FLR/펌웨어 리셋→재초기화→큐 재개를 수행하는 복구 상태기계(devlink health 스타일)를 구현해 진행 중 TX/RX 버퍼 누수가 없음을 검증한다. | mac |
| NETHW-A11 | DPU 스토리지 오프로드: NVMe 에뮬레이션 → NVMe-oF | PCIE-M01 SQ/CQ 모델을 'DPU가 노출하는 에뮬레이티드 NVMe 컨트롤러'로 재사용해, 호스트 SQ 엔트리를 DPU 측 서비스가 꺼내 PCIE-A09의 NVMe/TCP mini target으로 전달하고 완료를 호스트 CQ에 돌려주는 브리지를 구현하며, 큐 매핑·PRP DMA·타임아웃 전파와 추가 지연을 측정한다 (기초는 see:PCIE-A09). | mac |
| NETHW-A12 | DPU/IPU 아키텍처 설계 면접: 무엇을 어디에 둘 것인가 | '베어메탈 클라우드 호스트를 위한 200G DPU'를 주제로 OVS/eswitch 오프로드, 보안(호스트 격리·secure boot), 스토리지(NVMe 에뮬레이션), 암호화·압축 가속기를 fast path(HW)/exception path(Arm 코어)에 배치하는 설계 문서를 작성하고, exception path 비율별 Arm 코어 수·PCIe 대역폭을 계산하는 예산 스크립트로 trade-off를 정당화한다. | mac |

### VIRT — 가상화 · 컨테이너 (Virtualization & Containers)

가상화는 클라우드 서버, 차량·로봇 SoC의 혼합 임계 통합, Android pKVM, 컨피덴셜 컴퓨팅까지 현대 시스템 소프트웨어의 기반이다. 이 트랙은 trap-and-emulate 원리와 ARM EL2·stage-2 변환에서 출발해 M2 맥의 Hypervisor.framework로 실제 AArch64 게스트를 돌리는 미니 VMM을 키워 virtio 디바이스·인터럽트 가상화·Linux 부팅·마이그레이션까지 구현하고, KVM API와 namespaces/cgroups/seccomp 기반 컨테이너도 직접 만든다. NVIDIA·AWS(Nitro/Firecracker)·Google·Apple·Rivian 등의 하이퍼바이저/플랫폼 면접에서 묻는 'exit가 왜 비싼가, stage-2 fault를 어떻게 처리하나'에 코드로 답하는 것이 목표다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| VIRT-B01 | Popek–Goldberg 조건과 Trap-and-Emulate | 특권/민감 명령이 있는 toy ISA 해석기 위에서 게스트 커널을 비특권 모드로 실행하고 VMM이 트랩된 명령을 가상 CPU 상태로 에뮬레이트하게 만든 뒤, '비특권인데 민감한' 명령(x86 POPF 유사: 사용자 모드에서 IF 비트 변경이 조용히 무시됨)을 넣어 게스트가 오동작하는 테스트로 조건 위반을 재현한다. | mac |
| VIRT-B02 | 에뮬레이션 vs 반가상화(Hypercall) vs HW 지원 | B01 toy VMM에 (a) 민감 명령마다 트랩, (b) 게스트 소스를 고친 hypercall 배치(paravirt), (c) 블록 단위 바이너리 변환(민감 명령만 콜아웃으로 치환) 세 모드를 구현해 같은 게스트 워크로드의 exit 수와 '1 exit = 1000 사이클' 가정의 오버헤드를 표로 비교한다. | mac |
| VIRT-B03 | ARM EL2 레지스터 디코더: HCR_EL2·ESR_EL2 | HCR_EL2(VM, SWIO, FMO/IMO, TWI/TWE, TSC, TVM, TGE, E2H, RW) 값을 사람이 읽는 설명으로 바꾸고, ESR_EL2의 EC(0x01 WFx, 0x16 HVC, 0x17 SMC, 0x18 MSR/MRS, 0x20/0x24 lower-EL abort)와 ISS(data abort의 ISV/SAS/SRT/WnR/DFSC, sysreg의 Op0..CRm)를 디코딩하는 C 도구를 작성해 실제 덤프 값 10개로 검증한다(EL 기초는 see:ARM-M13, see:PLAT-B03). | mac |
| VIRT-B04 | VHE vs nVHE: 호스트 커널은 어디서 도는가 | nVHE(호스트 EL1 + EL2 stub)와 VHE(호스트 EL2) 두 구성에서 게스트 진입·탈출 시 저장/복원해야 하는 시스템 레지스터 목록과 EL 전환 횟수를 C 모델로 계산하고, E2H=1일 때 *_EL1 접근이 *_EL2로 리디렉션되는 규칙과 *_EL12 별칭을 표로 정리해 'KVM이 왜 VHE를 선호하나' 질문에 답한다. | mac |
| VIRT-B05 | Stage-2 변환: GVA→IPA→PA 중첩 페이지 워크 | 게스트 stage-1(4-level, 4KB)과 하이퍼바이저 stage-2(VTTBR/VTCR, IPA 40비트) 테이블을 메모리 배열에 만들고 GVA→IPA→PA 워크를 구현해 각 단계 PTE 읽기를 세며, 4×4 레벨에서 최대 24회 접근((n+1)(m+1)−1)과 2MB/1GB 블록 매핑 시 감소량을 테스트로 확인한다(stage-1 MMU 기초는 see:ARM-A03, see:OS-B10). | mac |
| VIRT-B06 | x86 VT-x/EPT 개념: VMCS와 Exit Reason | VMCS 주요 필드(guest/host state, execution controls, exit reason, exit qualification)와 EPT 엔트리(RWX, memory type, A/D 비트)를 디코딩하는 C 도구를 작성해 CPUID·HLT·I/O·EPT violation·EPT misconfig exit 샘플을 해석하고, 각 항목에 대응하는 ARM 개념(ESR EC, stage-2 fault, HCR 트랩 비트) 비교표를 만든다. | mac |
| VIRT-B07 | Hypervisor.framework 첫 VM: HVC 한 번 받기 | hv_vm_create → 16KB 정렬 게스트 RAM을 hv_vm_map으로 IPA 0x10000에 매핑 → `mov x0,#42; hvc #0` 기계어 복사 → hv_vcpu_create, PC/CPSR(0x3c5, EL1h) 설정 → hv_vcpu_run 루프를 작성하고, exit->exception.syndrome의 EC=0x16과 x0=42를 출력해 확인한다(HVC는 복귀 PC가 이미 다음 명령이라 PC를 올리지 않는다는 점 포함). | mac |
| VIRT-B08 | MMIO 트랩 에뮬레이션: 가상 PL011 UART | B07 VMM에서 게스트가 0x0900_0000(매핑 안 됨)의 PL011 UARTDR/UARTFR에 str/ldr 하면 생기는 data abort exit의 ISV·SAS·SRT·WnR로 레지스터 값을 읽고/써 주는 UART 모델을 구현하고(PC+4 직접 증가), 게스트 어셈블리 루프가 "Hello from EL1 guest"를 호스트 터미널에 출력하는지 확인한다. | mac |
| VIRT-B09 | 리눅스 네임스페이스: clone/unshare로 격리 관찰 | clone(CLONE_NEWPID|CLONE_NEWUTS|CLONE_NEWNS|CLONE_NEWNET|CLONE_NEWUSER) 자식 프로세스에서 getpid()==1, 독립 hostname, 빈 네트워크(lo만), uid_map 매핑을 출력하는 C 프로그램을 작성하고 /proc/<pid>/ns/* inode와 setns()로 다른 프로세스 네임스페이스에 들어가는 동작을 검증한다. | **linux** |
| VIRT-B10 | cgroup v2: CPU·메모리·PID 제한 걸기 | /sys/fs/cgroup 아래 하위 그룹을 만들고 cpu.max(20ms/100ms), memory.max, pids.max를 설정한 뒤 CPU 스핀·메모리 누수·fork 폭탄 프로그램을 넣어 cpu.stat throttled_usec, memory.events oom_kill, pids.events max가 증가하는지 C/셸로 검증한다. | **linux** |
| VIRT-B11 | 컨테이너 이미지 레이어: overlayfs와 whiteout | busybox rootfs를 base 레이어로, 파일 추가·삭제를 담은 레이어 2개를 만들어 overlayfs(lowerdir 다중, upperdir, workdir)로 합치고, 하위 레이어 파일 수정 시 copy-up, 삭제 시 whiteout(char 0/0) 생성을 확인한 뒤 OCI 레이어 tar의 .wh. 파일을 overlay whiteout으로 변환하는 스크립트를 작성한다. | **linux** |
| VIRT-M01 | HVF 게스트 MMU 켜기와 Stage-2 권한 | 게스트 코드가 IPA 공간에 stage-1 테이블을 만들고 MAIR/TCR/TTBR0_EL1/SCTLR_EL1을 설정해 MMU를 켠 뒤 고-VA에서 실행되게 하고, VMM이 hv_vm_protect로 한 페이지를 읽기 전용으로 바꿨을 때 게스트 쓰기가 stage-2 permission fault exit(DFSC)로 오는 것과 exit->exception.virtual_address/physical_address(GVA/IPA)를 확인한다. | mac |
| VIRT-M02 | VM Exit 비용 실측: HVC·MMIO·Sysreg 트랩 | HVF 게스트에서 HVC, MMIO load, 트랩되는 시스템 레지스터 접근을 각각 100만 회 수행하며 CNTVCT_EL0로 게스트 측 사이클을 재고 호스트 측 mach_absolute_time과 대조해 exit당 ns를 구한 뒤, 호스트 syscall 비용(see:OS-B03)과 비교하고 virtio 알림 억제·배치가 왜 중요한지 수치로 설명한다. | mac |
| VIRT-M03 | 가상 타이머·IRQ 주입·WFI 처리 | 게스트가 CNTV_CVAL_EL0/CNTV_CTL_EL0로 10ms 주기 타이머를 걸면 HV_EXIT_REASON_VTIMER_ACTIVATED exit에서 hv_vcpu_set_pending_interrupt(IRQ)로 주입하고 vtimer mask를 해제하며, 게스트의 WFI(EC=0x01) exit 때는 다음 데드라인까지 호스트 스레드를 재우는 VMM을 작성해 게스트 IRQ 핸들러가 100틱을 세고 호스트 CPU 사용률이 낮게 유지됨을 확인한다. | mac |
| VIRT-M04 | PSCI 에뮬레이션과 멀티 vCPU | HVC로 들어오는 PSCI_VERSION, CPU_ON(entry, context_id), CPU_OFF, SYSTEM_OFF/RESET을 SMCCC 규약대로 처리하고, vCPU마다 생성 스레드에서만 hv_vcpu_run을 돌리는 구조와 hv_vcpus_exit로 다른 vCPU를 깨우는 경로를 구현해 4개 vCPU가 공유 메모리 카운터를 LDXR/STXR로 올리는 게스트 테스트를 통과시킨다. | mac |
| VIRT-M05 | vGIC 모델: GICv3 List Register와 가상 인터럽트 | 가상 distributor(enable/pending/active/priority/라우팅)와 vCPU당 LR 4개 모델을 C로 작성해 pending 인터럽트를 우선순위 순으로 LR에 채우고, 게스트 ack/EOI에 따른 LR 상태 전이, LR 부족 시 underflow maintenance 인터럽트로 재충전하는 흐름, SGI(IPI)·PPI(타이머)·SPI를 테스트로 검증한다(물리 GIC 기초는 see:ARM-A02, GICv3 ITS는 see:SOCX). | mac |
| VIRT-M06 | virtio-mmio 트랜스포트: 디바이스 협상과 콘솔 | HVF VMM에 virtio-mmio v2 레지스터(MagicValue 0x74726976, DeviceID, Device/DriverFeatures(Sel), QueueSel/NumMax/Num/Desc/Driver/Device/Ready, QueueNotify, InterruptStatus/ACK, Status)와 virtio-console 백엔드를 구현하고, 베어메탈 게스트 드라이버로 ACKNOWLEDGE→DRIVER→FEATURES_OK→DRIVER_OK 협상 후 TX 큐로 문자열을 보내 출력한다(vring 자료구조는 see:PLAT-M06 재사용). | mac |
| VIRT-M07 | virtio-blk 백엔드와 알림 억제(EVENT_IDX) | M06 트랜스포트에 virtio-blk(헤더 type/sector → 데이터 → status 3-디스크립터 체인, 호스트 파일 pread/pwrite 백엔드)를 구현하고 VIRTIO_RING_F_EVENT_IDX의 used_event/avail_event 규칙과 vring_need_event()를 넣어, 4KB 랜덤 I/O 1만 건에서 알림 exit·인터럽트 횟수를 EVENT_IDX 유무로 비교한다. | mac |
| VIRT-M08 | virtio-pci 트랜스포트: Capability와 MSI-X | 가상 PCI config space에 virtio vendor capability 5종(COMMON_CFG, NOTIFY_CFG+notify_off_multiplier, ISR_CFG, DEVICE_CFG, PCI_CFG)과 MSI-X capability를 배치하는 에뮬레이터를 작성하고, 리눅스 virtio_pci_modern 프로브 순서를 흉내 낸 드라이버 측 파서로 BAR 오프셋 해석·큐별 notify 주소 계산·MSI-X 벡터 할당을 검증한다(config space/capability 기초는 see:PCIE). | mac |
| VIRT-M09 | KVM API 미니 VMM (arm64) | KVM_CREATE_VM → KVM_SET_USER_MEMORY_REGION → KVM_CREATE_VCPU → KVM_ARM_PREFERRED_TARGET/KVM_ARM_VCPU_INIT → KVM_SET_ONE_REG(PC) → mmap(kvm_run) → KVM_RUN 루프에서 KVM_EXIT_MMIO로 UART 출력, KVM_EXIT_SYSTEM_EVENT로 종료를 처리하는 C VMM을 작성해 B08 게스트 바이너리를 그대로 돌리고, HVF와 API 대응표(누가 PC를 올리는지, vCPU 스레드 규칙)를 만든다. | **linux** |
| VIRT-M10 | Dirty Page Tracking: Stage-2 쓰기 보호 로그 | HVF VMM에서 게스트 RAM 전체를 hv_vm_protect(READ|EXEC)로 쓰기 금지하고 permission fault exit마다 해당 페이지를 dirty 비트맵에 기록·쓰기 허용으로 되돌린 뒤 재실행하게 만들고, 게스트 쓰기 워크로드에서 주기별 dirty 페이지 수를 KVM_GET_DIRTY_LOG/dirty-ring 방식과 비교해 정리한다. | mac |
| VIRT-M11 | 디바이스 패스스루: VFIO 할당과 GPA=IOVA 매핑 | VFIO container/group/device 흐름(see:PCIE-A01)을 VM 관점으로 확장해 '게스트 RAM 전체를 IOVA=GPA로 VFIO_IOMMU_MAP_DMA'하는 C 모델과 IOMMU 변환기(see:PCIE-M15)를 연결하고, 게스트 드라이버가 GPA로 DMA할 때 성공·다른 VM 메모리 접근 시 fault·인터럽트 remapping(eventfd→irqfd) 경로를 테스트하며, 게스트 IOMMU가 필요한 경우의 nested(stage-1+2) 변환을 표로 정리한다(SR-IOV는 see:PCIE-A06, SMMU StreamID는 see:SOCX). | mac |
| VIRT-M12 | 미니 컨테이너 런타임: namespaces + pivot_root + cgroup | B09–B11을 합쳐 user/pid/mount/uts/net 네임스페이스 생성 → overlay rootfs 마운트 → pivot_root + /proc 마운트 → cgroup v2 그룹 등록 → capability 드롭(capset, PR_SET_NO_NEW_PRIVS) → execve(/bin/sh) 하는 `minirun`을 작성하고, 컨테이너 안에서 ps가 PID 1만 보이고 호스트 파일·메모리 한도 밖으로 나갈 수 없음을 테스트한다. | **linux** |
| VIRT-M13 | seccomp-BPF 필터 직접 작성 | seccomp_data의 arch 검사(AUDIT_ARCH_AARCH64) → nr 허용 목록 → SECCOMP_RET_ALLOW/ERRNO/KILL_PROCESS/TRAP을 반환하는 sock_filter 배열을 매크로로 작성해 M12 minirun에 적용하고, 금지 syscall 호출 시 EPERM·SIGSYS(si_syscall 출력)를 확인하며 아키텍처 검사를 빼먹었을 때의 우회 위험과 SECCOMP_RET_USER_NOTIF 용도를 정리한다. | **linux** |
| VIRT-A01 | HVF 미니 VMM으로 Linux 게스트 부팅 | M01–M07 VMM에 Image 헤더 파싱·로드(text_offset), DTB 생성(memory, cpus+psci, timer, GICv3, pl011, virtio-mmio 노드), x0=DTB 주소 부팅 규약, hv_gic_create(macOS 15+ GIC) 연결, initramfs 로드를 추가해 earlycon 로그부터 virtio-blk 루트 마운트·BusyBox 셸까지 부팅하고 부트 시간을 측정한다. | mac |
| VIRT-A02 | 라이브 마이그레이션: Pre-copy 수렴과 다운타임 | HVF VMM의 vCPU 레지스터·시스템 레지스터·디바이스 상태·RAM을 직렬화해 소켓으로 다른 VMM 프로세스에 보내 이어서 실행하는 save/restore를 구현하고, M10 dirty 로그로 pre-copy 라운드를 돌려 dirty rate vs 대역폭에 따른 라운드 수·최종 다운타임을 측정, 수렴 안 하는 워크로드에서 auto-converge(vCPU 쓰로틀)와 post-copy 대안을 비교한다. | mac |
| VIRT-A03 | KVM 성능 분석: Exit 프로파일과 Halt-Polling | KVM 게스트에서 네트워크·디스크·유휴 워크로드를 돌리며 kvm:kvm_exit/kvm_entry tracepoint(perf kvm stat 또는 trace-cmd)로 exit reason별 횟수·지연을 집계하고, halt_poll_ns 값과 vCPU 핀닝을 바꿔 웨이크업 지연·호스트 CPU 사용률 트레이드오프를 표로 정리한다. | **linux** |
| VIRT-A04 | ioeventfd·irqfd: VMM을 거치지 않는 알림 경로 | M09 KVM VMM에 KVM_IOEVENTFD(QueueNotify 주소)와 KVM_IRQFD(GSI)를 등록해 백엔드 워커 스레드가 eventfd로 알림을 받고 인터럽트를 주입하게 바꾸고, KVM_EXIT_MMIO 기반 동기 처리 대비 요청당 지연·VMM 스레드 CPU 사용량을 측정해 vhost/vDPA로 이어지는 동기를 정리한다(vhost-net/vDPA 데이터패스는 see:NETHW). | **linux** |
| VIRT-A05 | Firecracker 스타일 microVM과 Jailer | Firecracker 구조(virtio-net/blk/vsock + serial만, 레거시 BIOS 없음, vCPU 스레드별 seccomp, jailer의 chroot+netns+cgroup+uid 드롭)를 분석해 M09 KVM VMM을 M12/M13 minirun 안에서 띄우는 jailer를 작성하고, 디바이스 수·커널 config 축소에 따른 부팅 시간(VMM 시작→init 실행)과 메모리 오버헤드를 측정해 '왜 컨테이너가 아니라 microVM인가'를 설계 문서로 정리한다. | **linux** |
| VIRT-A06 | pKVM: 호스트를 믿지 않는 하이퍼바이저 | 페이지별 소유권 상태(host-owned, guest-owned, shared-owned/borrowed, hyp-owned)와 host_share_hyp/donate/reclaim hypercall 전이 규칙을 가진 C 모델에 호스트·게스트 stage-2 테이블을 연동해, 호스트가 보호 VM 페이지 접근 시 fault, 공유 해제 후 재접근 거부, VM 종료 시 페이지 scrub 후 반환을 테스트로 검증한다. | mac |
| VIRT-A07 | Xen 분할 드라이버: Grant Table과 Event Channel | grant table(domid, frame, readonly 플래그, map/unmap 참조 카운트)과 event channel(bind, notify, pending/mask 비트맵)을 C로 모델링하고 그 위에 blkfront/blkback 공유 링(req/rsp producer·consumer 인덱스)을 구현해 블록 요청을 주고받으며, grant 해제 중 사용(use-after-revoke) 버그 주입과 dom0/driver domain 구조의 장단점을 정리한다. | mac |
| VIRT-A08 | 혼합 임계 파티셔닝: 정적 파티션 하이퍼바이저와 캐시 컬러링 | Jailhouse/Bao/Xen dom0less 방식의 정적 파티션 설정(코어 핀닝, 메모리 영역, 디바이스·IRQ 할당) 검증기를 작성하고, LLC set 인덱스 비트 기반 캐시 컬러링 페이지 할당기와 공유 캐시 시뮬레이터로 Linux 파티션의 메모리 폭주가 RTOS 파티션 WCET에 주는 간섭을 컬러링·MemGuard식 대역폭 제한 유무별로 비교한다(ARINC 653 시간 분할은 see:AERO-M01, 안전 분석은 see:AUTO). | mac |
| VIRT-A09 | Confidential Computing: Arm CCA Realm과 GPT | Granule Protection Table(물리 granule별 Non-secure/Secure/Realm/Root PAS)과 RMI GRANULE_DELEGATE/UNDELEGATE·DATA_CREATE 흐름, Realm 초기 측정(RIM) 해시 확장을 C로 모델링해 NS 하이퍼바이저의 Realm 페이지 접근이 GPF로 막히는지 테스트하고, AMD SEV-SNP(RMP)·Intel TDX와 비교표를 만든다(원격 증명 토큰은 see:SEC-A05, TrustZone은 see:SEC-M07). | mac |
| VIRT-A10 | 종합: 엣지 AI·차량 SoC 가상화 플랫폼 설계 리뷰 | 8코어 Arm SoC + NPU + GPU에서 ASIL 제어 RTOS, Linux 인지 스택, 인포테인먼트 Android를 돌리는 플랫폼에 대해 하이퍼바이저 선택(pKVM/Xen/정적 파티션), 디바이스 할당(패스스루 vs virtio vs SR-IOV, GPU 분할은 see:GPUSYS-A05), 인터럽트 지연 예산, 메모리·캐시 격리, 보안 부팅·증명을 담은 설계서를 쓰고 핵심 경로(vIRQ 주입 지연 예산 계산기)를 코드로 검증한다. | mac |

### AISYS — AI 학습 · 서빙 시스템 (AI Training & Serving Systems)

NVIDIA·Meta·Google·Tenstorrent 같은 AI 인프라 조직의 시스템/펌웨어 엔지니어는 '70B 모델 학습에 GPU 메모리가 얼마나 필요한가?', '이 서빙 SLO에 GPU가 몇 대 필요한가?'에 봉투 뒷면 계산으로 답할 수 있어야 한다. 이 트랙은 학습 메모리·컴퓨트 수학, 데이터/텐서/파이프라인/시퀀스/전문가 병렬화와 ZeRO/FSDP, 혼합 정밀도, 체크포인트·고장 대응, 그리고 PagedAttention·prefix 캐시·speculative decoding·disaggregated 서빙까지를 맥에서 도는 Python/C 시뮬레이터와 미니 구현으로 직접 만들어 본다. 가속기 하드웨어(NPU/GPGPU)와 지표(AIPERF), 네트워크 collective(DCN) 위에 올라가는 '시스템 소프트웨어 층'이다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| AISYS-B01 | 학습 한 스텝 해부: 수동 역전파와 6N FLOP 규칙 | numpy로 2층 MLP와 단일 트랜스포머 블록의 forward/backward를 수동 구현해 finite-difference gradient check로 검증하고, GEMM마다 FLOP을 세어 backward가 forward의 약 2배(입력 grad + 가중치 grad)라서 토큰당 학습 FLOP≈6N이 됨을 보인다(추론 2N 계산은 see:AIPERF-M03). | mac |
| AISYS-B02 | 학습 컴퓨트 예산: 6ND, MFU와 GPU-시간 | N, D, GPU 수, GPU 피크 bf16 FLOPS, MFU를 받아 학습 일수·GPU-시간·전력량을 계산하는 Python 도구를 작성해 공개된 Llama-3-8B/70B·GPT-3 GPU-시간을 재현하고, Chinchilla(D≈20N) 최적 대비 과학습(over-training) 모델이 추론 비용 때문에 선택되는 이유를 수치로 설명한다(MFU 정의는 see:AIPERF-B09). | mac |
| AISYS-B03 | 학습 메모리 1: 파라미터·그래디언트·Adam 상태 | dtype별 weight/grad/fp32 master/Adam m·v 바이트를 합산하는 계산기를 작성해 7B·70B 모델의 학습 상태 메모리(≈16Ψ)를 구하고, PyTorch로 작은 GPT를 만들어 optimizer.step() 전후 텐서 바이트 합계(또는 torch.mps.current_allocated_memory)가 계산과 일치하는지 검증한다. | mac |
| AISYS-B04 | 학습 메모리 2: 트랜스포머 활성화 메모리 공식 | Korthikanti et al.의 층당 활성화 공식 s·b·h·(34 + 5·a·s/h) 바이트를 구현하고, PyTorch saved_tensors_hooks로 작은 GPT의 forward 중 저장된 텐서 바이트를 실제로 합산해 공식과 비교하며 attention 점수 행렬(a·s²) 항이 긴 시퀀스에서 지배적이 되는 지점을 찾는다. | mac |
| AISYS-B05 | Gradient Accumulation과 유효 배치 크기 | 작은 모델에서 배치 B 한 번과 마이크로배치 B/k를 k번 누적한 그래디언트를 비교해 허용 오차 내 일치를 검증하고, 가변 길이 시퀀스에서 마이크로배치별 평균 loss를 다시 평균하면 토큰 가중치가 틀어지는 버그를 재현·수정(전체 토큰 수로 정규화)하며 BatchNorm이 있으면 왜 동치가 깨지는지 설명한다. | mac |
| AISYS-B06 | 혼합 정밀도 1: fp16 동적 Loss Scaling과 fp32 Master Weight | numpy float16으로 작은 회귀 모델을 학습시켜 작은 그래디언트가 0으로 flush되는 현상을 재현한 뒤 동적 loss scaler(scale 증가 간격, inf/NaN 감지 시 step skip·backoff)를 구현하고, 가중치를 bf16에만 두면 w + lr·g 업데이트가 반올림으로 사라지는 stagnation을 fp32 master weight로 해결함을 보인다(포맷 비트 구현은 see:NPU-B08). | mac |
| AISYS-B07 | 데이터 병렬 1: 그래디언트 평균과 통신량 | N개 프로세스가 데이터 샤드를 나눠 그래디언트를 계산하고 all-reduce 평균 후 같은 업데이트를 적용하는 DDP를 직접 구현해 단일 프로세스 학습과 파라미터가 일치함을 검증하고, ring all-reduce 기준 랭크당 송신량 2(N−1)/N·Ψ·bytes와 링크 대역폭으로 스텝당 통신 시간을 추정한다(collective 알고리즘 자체는 see:DCN, see:GPUSYS-A02). | mac |
| AISYS-B08 | 텐서 병렬 1: Megatron Column/Row 분할 MLP | numpy로 MLP의 첫 GEMM은 column-parallel, 두 번째는 row-parallel로 t개 샤드에 나눠 계산하고 마지막에 all-reduce(합)를 하는 구현을 작성해 비분할 결과와 일치를 검증하며, attention head 분할까지 확장해 트랜스포머 층당 forward 2회·backward 2회 all-reduce가 필요함을 셈한다(추론 측 TP 비용은 see:AIPERF-A05). | mac |
| AISYS-B09 | 생성 루프: KV Cache 재사용과 샘플링 (Temperature·Top-k·Top-p) | PyTorch로 작은 GPT(nanoGPT 또는 GPT-2 124M 가중치)를 불러 prefill 한 번 후 KV cache를 이어 붙이며 토큰을 생성하는 루프와 greedy/temperature/top-k/top-p 샘플러를 구현하고, KV cache 없는 재계산 버전과 greedy 출력 일치·토큰당 지연 차이를 비교한다(C 추론 엔진은 see:EML-A03). | mac |
| AISYS-B10 | 서빙 용량 계획: QPS·SLO에서 레플리카 수 산정 | 요청 트레이스(도착 시각, 입력/출력 길이, 실제 TTFT·TPOT)에서 p50/p99 TTFT·TPOT, SLO 달성률, goodput을 계산하는 도구를 작성하고, 단일 레플리카의 처리량-지연 곡선(see:AIPERF-A03 시뮬레이터 결과)과 Little's law로 목표 QPS·SLO를 만족하는 최소 레플리카 수와 GPU-시간당 비용을 산정한다. | mac |
| AISYS-B11 | KV Cache 단편화: 연속 예약 vs 블록 할당 낭비 측정 | 출력 길이 분포가 있는 요청 스트림에 대해 (a) max_seq_len 연속 예약, (b) 실제 길이 연속 할당(외부 단편화 발생), (c) 고정 크기 블록 할당의 세 KV 할당기를 시뮬레이션해 예약 낭비·내부·외부 단편화 비율과 동시 수용 시퀀스 수를 비교한다(KV 크기 계산은 see:AIPERF-M04). | mac |
| AISYS-M01 | 데이터 병렬 2: 그래디언트 버킷팅과 Backward-통신 오버랩 | 층별 backward 시간과 그래디언트 크기, α-β 링크 모델을 입력으로 받는 이벤트 시뮬레이터를 작성해 버킷 크기(1MB~100MB)별 노출 통신 시간과 스텝 시간 타임라인을 그리고, gloo로 실제 hook 기반 버킷 all-reduce를 구현해 결과가 B07과 일치함을 확인한다. | mac |
| AISYS-M02 | ZeRO 1/2/3: 샤딩 단계별 메모리·통신 계산기 | Ψ, N_d, 정밀도를 입력받아 ZeRO-0/1/2/3의 디바이스당 모델 상태 메모리((2+2+K)Ψ → … → (2+2+K)Ψ/N_d)와 스텝당 통신량(2Ψ vs 3Ψ)을 계산하는 도구를 작성해 ZeRO 논문의 7.5B·64 GPU 표를 재현하고, 주어진 GPU 메모리에 들어가는 최대 모델 크기를 단계별로 구한다. | mac |
| AISYS-M03 | FSDP 미니 구현: All-gather 파라미터, Reduce-scatter 그래디언트 | gloo 프로세스 N개로 각 층 파라미터를 평탄화·샤딩해 보관하고, forward 직전 all-gather → 사용 후 해제, backward에서 다시 all-gather → grad reduce-scatter → 샤드 optimizer step을 수행하는 미니 FSDP를 구현해 단일 프로세스 학습과 파라미터 일치 및 랭크당 피크 메모리 감소를 검증한다. | mac |
| AISYS-M04 | Activation Checkpointing: 재계산 vs 메모리와 최적 분할 | PyTorch 작은 GPT에 층 단위 torch.utils.checkpoint를 적용해 피크 메모리·스텝 시간을 측정하고, n개 층에서 체크포인트 위치를 고르는 DP(메모리 예산 하 재계산 최소화)를 구현해 Chen et al.의 O(√n) 결과와 비교하며 attention 점수만 재계산하는 selective recompute(see B04 공식)의 절감률을 계산한다. | mac |
| AISYS-M05 | 텐서 병렬 2: Sequence Parallel로 분해한 통신과 활성화 | numpy 시뮬레이션으로 TP 영역은 hidden 축, LayerNorm/Dropout 영역은 sequence 축 샤딩으로 전환하는 g/ḡ 연산(all-gather/reduce-scatter)을 구현해 비분할 결과와 일치를 검증하고, 통신량은 같으면서 디바이스당 활성화가 1/t로 줄어드는 것을 B04 공식으로 계산한다. | mac |
| AISYS-M06 | 파이프라인 병렬 1: GPipe 스케줄과 버블 비율 | p 스테이지·m 마이크로배치·스테이지별 forward/backward 시간을 입력받는 이산 사건 시뮬레이터로 GPipe(all-forward 후 all-backward) 스케줄 타임라인을 생성해 버블 비율 (p−1)/(m+p−1) 공식을 검증하고, 스테이지 시간 불균형이 있을 때 실제 버블이 공식보다 커지는 것을 보인다. | mac |
| AISYS-M07 | 파이프라인 병렬 2: 1F1B와 Interleaved 스케줄 | M06 시뮬레이터에 1F1B와 interleaved 1F1B(v개 가상 스테이지) 스케줄을 추가해 스테이지별 in-flight 마이크로배치 수(피크 활성화 메모리)와 버블 비율 (p−1)/(v·m)을 GPipe와 비교하고, 스케줄의 의존성 위반(데드락)을 검출하는 검증기를 작성한다. | mac |
| AISYS-M08 | 혼합 정밀도 2: fp8 학습 스케일링 (Delayed·Current·Block) | ml_dtypes E4M3/E5M2로 forward(E4M3)·backward grad(E5M2) GEMM을 시뮬레이션하며 amax history 기반 delayed scaling, current scaling, 128×128 블록 스케일링을 구현하고, 이상치가 섞인 활성화·그래디언트에서 포화율·0 flush율·fp32 대비 상대 오차를 측정한다(포맷·MX 구현은 see:NPU-B08, see:NPU-A02). | mac |
| AISYS-M09 | FlashAttention 타일링: 블록 크기 선택과 Backward 재계산 | numpy로 tiled attention forward(online softmax 재스케일, 행별 logsumexp 저장)와 backward(dQ/dK/dV를 블록 재계산으로 산출)를 구현해 naive attention 및 autograd 결과와 일치를 검증하고, SRAM 크기 M과 head_dim d로 Br·Bc를 정했을 때 HBM 읽기/쓰기 바이트를 세어 backward 재계산의 FLOP 증가 대비 메모리 절감을 계산한다(IO 복잡도 모델은 see:AIPERF-A02, 커널은 see:GPGPU-A03). | mac |
| AISYS-M10 | PagedAttention 1: 블록 테이블과 KV 블록 관리자 | 고정 크기 물리 KV 블록 풀, free list, 시퀀스별 블록 테이블, 참조 카운트를 가진 블록 관리자와 블록 테이블을 따라 K/V를 gather하는 attention을 구현해 연속 KV attention과 결과 일치를 검증하고, B11 트레이스에서 블록 크기(8/16/32)별 내부 단편화와 테이블 오버헤드를 측정한다. | mac |
| AISYS-M11 | Prefix Caching: Radix Tree 공유와 Copy-on-Write | M10 블록 관리자 위에 블록 해시 체인(또는 SGLang식 radix tree) 기반 prefix 캐시, 분기 시 copy-on-write, refcount 0 블록의 LRU 퇴출을 구현하고, 공통 시스템 프롬프트·멀티턴·few-shot 합성 워크로드에서 캐시 적중률과 절약된 prefill 토큰(TTFT 감소)을 측정한다. | mac |
| AISYS-M12 | 미니 서빙 엔진: Iteration-level 스케줄러 + Paged KV | 작은 GPT(B09)를 실행하는 엔진에 waiting/running 큐, 매 iteration마다 prefill·decode 요청을 한 배치로 묶는 스케줄러, M10 블록 관리자를 결합하고, 포아송 도착 부하에서 TTFT/TPOT/처리량을 측정해 static batching 대비 이득을 AIPERF-A03 시뮬레이션 예측과 대조한다. | mac |
| AISYS-M13 | MoE 라우팅: Top-k Gating, Capacity Factor, Load-balancing Loss | PyTorch로 top-1/top-2 gating, expert별 capacity = cf·tokens·k/E 제한과 overflow 토큰 드롭, Switch Transformer 보조 load-balancing loss를 가진 MoE 층을 구현해 작은 모델 학습 중 expert 부하 분포·드롭률을 capacity factor 1.0/1.25/2.0과 aux loss 유무별로 비교한다(dispatch all-to-all 비용은 see:DCN). | mac |
| AISYS-A01 | Speculative Decoding: 수용률 수식과 분포 보존 검증 | rejection sampling 기반 speculative decoding(수용 확률 min(1, p/q), 거절 시 norm(max(0, p−q))에서 재샘플)을 구현해 작은 어휘 장난감 분포에서 출력 분포가 target과 같음을 χ² 검정으로 확인하고, distilgpt2→gpt2 쌍에서 수용률 α를 측정해 기대 토큰 (1−α^(k+1))/(1−α)와 비용비로 최적 추측 길이 k를 구한다. | mac |
| AISYS-A02 | 메모리 압박 선점과 SLO 인식 스케줄링 | M12 엔진(또는 AIPERF-A03 시뮬레이터)에 KV 부족 시 swap-out(호스트 메모리 대역폭 모델)·recompute 선점, 입장 제어(admission control), FCFS vs 예측 출력 길이 기반 SJF vs SLO 데드라인 우선 정책을 추가해 부하 증가에 따른 p99 TTFT·TPOT·SLO 달성률·goodput을 비교한다. | mac |
| AISYS-A03 | Disaggregated Prefill/Decode: KV 전송 비용과 P:D 비율 | prefill 풀과 decode 풀, 그 사이 KV 전송(토큰당 KV 바이트 × 입력 길이 ÷ 링크 대역폭, see:GPUSYS-A01·see:DCN)을 모델링하는 시뮬레이터를 작성해 colocated+chunked prefill 대비 SLO 하 GPU당 goodput을 비교하고, 입력/출력 길이 분포별 최적 P:D GPU 비율과 KV 전송이 병목이 되는 링크 대역폭 임계값을 구한다. | mac |
| AISYS-A04 | Expert Parallel: 부하 불균형과 Expert 배치·복제 | M13에서 측정한(또는 Zipf로 생성한) expert 부하 분포로 EP 랭크별 토큰 수·all-to-all 바이트·GEMM 시간을 계산해 가장 느린 랭크가 step time을 결정함을 보이고, 핫 expert 복제와 그리디 재배치(EPLB 방식) 알고리즘을 구현해 최대/평균 부하비와 step time 개선을 측정한다. | mac |
| AISYS-A05 | Context Parallel: Ring Attention 시뮬레이션 | numpy로 N개 가상 디바이스가 Q 조각을 고정하고 K/V 조각을 링으로 전달하며 online softmax(logsumexp 병합)로 부분 결과를 합치는 ring attention을 구현해 전체 attention과 일치를 검증하고, causal mask에서의 부하 불균형(zig-zag 분할로 해결)과 블록 연산 시간 ≥ KV 전송 시간이 되는 최소 시퀀스 길이를 계산한다. | mac |
| AISYS-A06 | 3D 병렬 구성 탐색기: DP×TP×PP×CP 스텝 시간 모델 | B03·B04·M02·M05·M07·M01 모델을 결합해 (dp, tp, pp, cp, 마이크로배치, ZeRO 단계, 재계산 여부) 조합마다 디바이스당 메모리, 계산 시간(MFU 가정), TP는 노드 내/DP·PP는 노드 간 링크 α-β 통신 시간, 파이프라인 버블을 합산하는 탐색기를 작성하고, 70B 모델·1024 GPU에서 최적 구성을 Megatron/Llama 3 공개 구성과 비교한다. | mac |
| AISYS-A07 | 학습 클러스터 신뢰성: 실패율과 최적 체크포인트 간격 | GPU당 고장률과 GPU 수로 클러스터 MTBF를 계산해(신뢰성 기초는 see:TEST-B10) Llama 3 보고(16K GPU, 54일 419회 중단)와 대조하고, 체크포인트 저장 시간 δ·재시작 시간 R로 Young/Daly 최적 간격 √(2δ·MTBF)를 구한 뒤 몬테카를로 시뮬레이터로 간격별 유효 학습 시간 비율(goodput)을 측정해 공식과 비교한다. | mac |
| AISYS-A08 | 분산 체크포인트: 비동기 저장과 Resharding 복원 | M03 미니 FSDP에 랭크별 샤드 + 메타데이터(전역 shape, 오프셋) 저장, GPU→CPU 스냅샷 후 백그라운드 스레드로 디스크에 쓰는 비동기 저장, N랭크 체크포인트를 M랭크로 resharding하는 로더를 구현하고, 임의 시점 kill 후 재시작이 RNG 상태·데이터로더 위치까지 포함해 무중단 학습과 비트 단위로 같은 loss 곡선을 내는지 검증한다. | mac |
| AISYS-A09 | Straggler와 Silent Data Corruption 탐지 | 랭크별 스텝 구간 시간(계산/통신 대기) 텔레메트리를 생성하는 시뮬레이터에 느린 노드·열 스로틀·링크 열화와 비트플립 SDC를 주입하고, collective 대기 시간 분포로 straggler 랭크를 특정하는 탐지기와 loss spike·grad norm 이상·DP 복제 간 파라미터 체크섬 불일치로 SDC를 잡는 탐지기를 구현해 탐지율·오탐률을 측정한다. | mac |
| AISYS-A10 | 설계 면접: LLM 서빙 시스템 (용량·비용·SLO) | '일 1천만 요청, 평균 입력 2K/출력 300 토큰, p99 TTFT 1s·TPOT 50ms' 요구사항으로 모델 정밀도·TP 크기·배치·KV 예산·prefix 캐시·disaggregation 여부·라우팅/오토스케일링을 결정하는 설계 문서를 작성하고, B10·M11·A02·A03 도구로 GPU 수와 1M 토큰당 비용을 계산해 근거로 제시한다. | mac |
| AISYS-A11 | 설계 면접: 대규모 학습 런 계획 (70B, 15T 토큰) | 70B 모델을 15T 토큰으로 학습하는 계획서를 작성해 B02로 GPU 수·기간, A06으로 병렬 구성, A07로 체크포인트 간격·예상 중단 횟수, A08·A09로 복구·탐지 전략, 혼합 정밀도(bf16/fp8) 선택을 정하고, 각 결정의 수치 근거와 '예상보다 MFU가 10%p 낮으면?' 같은 민감도 분석을 포함한다. | mac |

## 12단계 · 수리 · 퀀트 (Math & Quant)

### QPROB — 확률 · 퀀트 브레인티저 (Probability & Quant Puzzles)

Jane Street·Citadel·HRT·Optiver·SIG 등 퀀트 트레이더/퀀트 개발자 면접은 조건부 기댓값, 선형성 트릭, 마르코프 체인, 마틴게일, 최적 정지, 베팅·마켓 메이킹 게임 같은 확률 브레인티저를 빠르고 정확하게 푸는 능력을 본다. 이 트랙은 각 아이템을 '대표 문제 + 변형'으로 묶어 먼저 해석적으로 풀고, C 또는 Python Monte Carlo/정확 DP 프로그램으로 답을 검증하는 방식으로 직관과 엄밀함을 함께 기른다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| QPROB-B01 | Monte Carlo 검증 하네스와 표준오차 (de Méré) | 시드 고정 PRNG 위에 실행 평균·표준오차·95% CI를 내는 MC 하네스와 분수(fractions) 정확값 비교기를 만들고, de Méré 문제(4번 중 6이 한 번 이상 vs 24번 중 6-6 한 번 이상)를 해석적으로 풀어 N=1e3..1e7에서 오차가 1/√N로 줄어드는지 확인한다(PRNG·분포 샘플링 기초는 see:SIM-B02). | mac |
| QPROB-B02 | 조합론 카운팅: 포함-배제와 교란순열 | 모자 돌려받기 문제(n명 중 아무도 자기 모자를 못 받을 확률 → 1/e)와 포커 족보(풀하우스·투페어·플러시) 확률, 주사위 3개 합이 10이 되는 경우의 수를 포함-배제/생성함수로 풀고, C 또는 Python 전수 열거 + MC로 세 값이 일치함을 검증한다. | mac |
| QPROB-B03 | 조건부 확률과 베이즈: Monty Hall 변형들 | Monty Hall(호스트가 아는 경우 vs 모르고 우연히 염소를 연 경우), 두 아이 문제(최소 한 명이 아들 / 화요일에 태어난 아들), 유병률 0.1% 검사의 양성 예측도를 베이즈 정리로 풀고, 각 정보 생성 과정을 그대로 흉내 낸 MC로 답이 갈리는 이유를 보인다. | mac |
| QPROB-B04 | 기댓값의 선형성과 지시변수 트릭 | 무작위 순열의 고정점 수(=1), 동전 n번에서 run 개수(1+(n-1)/2), 순열의 사이클 수(H_n), 52장 카드에서 첫 에이스 전까지 카드 수(48/5)를 지시변수로 풀고 MC(1e6회)로 검증한다. | mac |
| QPROB-B05 | 생일 문제와 해시 충돌 근사 | 365일에서 충돌 확률 ≥50%가 되는 최소 인원(23), 1-e^{-n²/2m} 근사의 오차, 첫 충돌까지 기대 인원(≈√(πm/2)), 특정 생일과 겹칠 확률과의 차이를 계산하고 32/64비트 해시 충돌 실험(MC)으로 근사를 확인한다(확률적 자료구조는 see:DS-M14). | mac |
| QPROB-B06 | 대기시간 1: 기하·음이항과 HH vs HT | 공정한 동전에서 HH 대기(6) vs HT(4), 주사위로 6이 k번 나올 때까지 기대 횟수(음이항, 6k), p-동전의 첫 앞면 위치 분산을 조건부 기댓값 방정식으로 풀고, 시뮬레이션 분포 히스토그램과 이론 PMF를 비교한다. | mac |
| QPROB-B07 | 쿠폰 수집가 문제와 불균등 확률 | 주사위 6면을 모두 보기까지 기대 횟수(14.7)와 분산을 기하분포 합으로 유도하고, 불균등 확률 쿠폰의 기대값을 포함-배제(∫(1-Π(1-e^{-p_i t}))dt) 및 비트마스크 상태 DP로 정확 계산해 MC와 대조한다(비트마스크 DP는 see:ALGO-A03). | mac |
| QPROB-B08 | 연속·기하 확률: 막대 자르기와 만남 문제 | 막대를 두 점에서 잘라 삼각형이 될 확률(1/4), 두 사람이 1시간 중 15분 대기로 만날 확률(7/16), Bertrand 현 역설(현을 뽑는 3가지 방법 → 1/2, 1/3, 1/4)을 적분/넓이로 풀고 MC로 각 해석을 재현한다. | mac |
| QPROB-B09 | 순서통계량과 꼬리합 공식 (합이 1을 넘을 때까지 → e) | U(0,1) n개의 E[max]=n/(n+1)·E[k번째]=k/(n+1), 두 점 사이 기대 거리(1/3), 균등 난수를 합이 1을 넘을 때까지 더할 때 기대 개수(e)와 '증가하는 동안 계속 뽑기'의 기대 길이(e-1)를 꼬리합으로 풀고 MC로 검증한다. | mac |
| QPROB-B10 | 분산·공분산·상관계수 경계 | 고정점 수의 분산(=1)을 공분산 합으로 유도하고, corr(X,Y)=corr(Y,Z)=0.9일 때 corr(X,Z)의 최솟값(PSD 조건 → 0.62)과 n개 변수가 모두 같은 상관 ρ일 때 ρ≥-1/(n-1)을 증명한 뒤, 상관 표본 생성(Cholesky) MC와 고유값 검사로 확인한다. | mac |
| QPROB-B11 | CLT·정규근사와 멘탈 매스 드릴 | 공정 동전 1000번에서 앞면 ≥550의 확률을 연속성 보정 정규근사로 암산하고 정확 이항합과 비교하며, ln2·e·√ 근사·1.01^100·72법칙 등 암산 문제를 무작위로 내고 시간·오차를 기록하는 드릴 프로그램을 만든다. | mac |
| QPROB-M01 | 마르코프 체인 흡수: first-step과 기본행렬 | 6-6이 연속으로 나올 때까지 기대 횟수(42), 주사위 합 누적이 정확히 n에 닿을 확률(→2/7), 체스 나이트 무작위 이동의 귀환 시간을 N=(I-Q)^{-1} 기본행렬(분수 정확해)로 풀고 MC로 검증하는 범용 흡수 체인 솔버를 작성한다. | mac |
| QPROB-M02 | 패턴 대기시간과 Penney's game | KMP 실패함수 오토마톤으로 동전/주사위 패턴 대기 체인을 만들어 HTH vs HHT 선착 확률과 기대 대기시간을 풀고, Penney's game에서 상대 패턴을 이기는 최적 응수 표(Conway 수 검증 포함)를 출력한 뒤 MC로 확인한다(KMP는 see:ALGO-A04). | mac |
| QPROB-M03 | 도박꾼의 파산 (Gambler's Ruin) | 자본 k, 목표 N, 승률 p에서 파산 확률((q/p)^k 형태)과 기대 게임 길이(공정: k(N-k))를 차분방정식으로 풀고, 정확 DP와 MC로 검증하며 '카지노에서 p=18/38일 때 두 배로 불릴 확률'을 베팅 단위별로 비교한다. | mac |
| QPROB-M04 | 단순 랜덤워크: 반사원리와 투표 문제 | 투표 문제(득표 p>q일 때 A가 항상 앞설 확률 (p-q)/(p+q)), 2n걸음 후 원점 재방문 확률, 최대값 분포 P(M_n≥a)를 반사원리로 유도하고 경로 전수 열거(작은 n)·MC(큰 n)로 확인하며 1D/2D/3D 재귀성(Pólya)을 시뮬레이션한다. | mac |
| QPROB-M05 | 마틴게일과 Optional Stopping 정리 | S_n과 S_n²-n 마틴게일로 도박꾼 파산의 확률·기대시간을 재유도하고, '도박꾼 팀' 마틴게일로 ABRACADABRA 대기시간(26^11+26^4+26)을 구한 뒤 Wald 항등식과 더블링 전략(유한 자본) 실패를 MC로 보인다; OST 조건 위반 반례도 정리한다. | mac |
| QPROB-M06 | 기댓값 게임과 후진 귀납 (최적 정지 DP) | 주사위를 최대 n번 굴리며 멈출 수 있을 때 기대값(3.5→4.25→4.67…), '빨강 +1/검정 -1 카드를 원할 때 멈추는' 52장 게임의 가치, 굴린 값만큼 받되 재굴림에 $1을 내는 게임을 DP로 풀어 최적 임계 전략을 표로 출력하고 MC로 전략 가치를 검증한다. | mac |
| QPROB-M07 | 비서 문제와 임계 전략 | n명 중 최고를 뽑는 비서 문제의 최적 관찰 비율(1/e)과 성공 확률을 유도하고, 최고 순위 기대값 최소화·2명 선택·후보 값이 U(0,1)로 공개되는 full-information 변형을 DP로 풀어 MC로 검증한다. | mac |
| QPROB-M08 | Poisson 과정과 지수분포 경주 | 버스 대기 inspection paradox(평균 간격 10분인데 평균 대기 10분), 두 Poisson 흐름에서 A 1건이 B 3건보다 먼저 올 확률, 지수 수명 전구 3개가 모두 꺼질 기대시간, 은행 창구 3명 중 마지막으로 나갈 확률을 유도하고 이벤트 시뮬레이션으로 검증한다(큐잉 모델은 see:SIM-B03, 신뢰성은 see:TEST-B10). | mac |
| QPROB-M09 | 베이즈 갱신: Beta-Binomial과 Pólya 항아리 | 공정 999개 + 양면 앞면 1개 중 하나로 10연속 앞면을 본 뒤 다음이 앞면일 확률, Laplace 계승 규칙, Beta(1,1) 사전에서 k/n 관측 후 사후분포, Pólya 항아리 빨간 공 수의 균등분포를 유도하고 MC(사전에서 샘플→관측 조건부 거르기)로 확인한다. | mac |
| QPROB-M10 | 추정과 신뢰구간: MLE·불편성·커버리지 | 독일 전차 문제(U{1..N}의 MLE max vs 불편추정 max(1+1/n)-1)의 편향·MSE, 이항 비율의 Wald vs Wilson 구간 실제 커버리지(p=0.01, n=50), 다중검정으로 인한 거짓 발견률을 MC로 측정해 표로 비교한다(부트스트랩은 see:AIPERF-A06, 무고장 BER 구간은 see:SIV-M06). | mac |
| QPROB-M11 | 선형회귀 함정: 퀀트 면접 단골 질문 | OLS(정규방정식/QR)를 구현하고 ① Y~X와 X~Y 기울기 곱 = ρ² ② 평균으로의 회귀 ③ X 측정오차로 인한 attenuation bias ④ 누락변수 편향 ⑤ 데이터를 복제했을 때 t-통계량 변화 ⑥ 상관된 두 특성의 계수 불안정성을 합성 데이터로 재현해 이론식과 비교한다(수치 안정성은 see:NUM). | mac |
| QPROB-M12 | Kelly 기준과 로그 성장률 | 승률 p·배당 b 베팅의 f*=p-q/b와 성장률 g(f)를 유도하고, full/half/2× Kelly의 부의 경로 분포(중앙값·파산 근접 확률)를 MC로 비교하며, 연속 근사 f*=μ/σ²와 변동성 드래그(μ-σ²/2), 동시 독립 베팅 2개의 최적 비율을 수치 최적화로 구한다. | mac |
| QPROB-A01 | Brownian motion 기초: 랜덤워크 극한과 반사원리 | 스케일된 랜덤워크로 BM 경로를 만들어 이차변분 [B]_T=T를 확인하고, P(max_{t≤T} B_t ≥ a)=2P(B_T≥a)와 첫 도달시간 분포를 유도한 뒤 이산화 MC의 편향(장벽 건너뜀)을 Brownian bridge 보정으로 줄이는 것을 보인다(옵션 가격결정 응용은 see:QFIN). | mac |
| QPROB-A02 | Drift BM·지수 마틴게일과 출구 시간 | B_t, B_t²-t, exp(θB_t-θ²t/2) 마틴게일에 optional stopping을 적용해 [-a,b] 출구 확률·기대 출구시간(ab), drift μ BM이 -a에 닿을 확률 e^{-2μa/σ²}를 유도하고 Euler 경로 MC와 Itô 보조정리 수치 검증(E[B_t^4]=3t² 등)으로 확인한다. | mac |
| QPROB-A03 | 마켓 메이킹 게임: 가격 제시와 역선택 | 주사위 3개 합/숨겨진 카드 합 같은 대상에 호가를 내고 상대 체결 시 베이즈로 가치를 갱신하는 Glosten-Milgrom 스타일 시뮬레이터(정보거래자 비율 α, 노이즈 거래자)를 구현해 스프레드-손익 관계와 손익분기 스프레드를 해석해와 비교한다(재고 모델 Avellaneda-Stoikov는 see:QFIN). | mac |
| QPROB-A04 | 베팅 게임 전략: Bold play와 목표 도달 확률 | 불리한 게임(p<1/2)에서 목표 도달 확률을 최대화하는 bold play vs timid play를 가치반복 DP로 비교하고, 유리한 게임·유한 라운드에서 '최종 부의 기대 로그'와 '목표 도달 확률' 목적함수에 따른 최적 베팅이 어떻게 다른지 표로 정리한 뒤 MC로 검증한다(see:QPROB-M12). | mac |
| QPROB-A05 | 그래프 위 랜덤워크: 전기회로 유비 | 정육면체 위 개미가 반대 꼭짓점까지 가는 기대시간(10), 정12면체 반대편 도달, 원형 n개 점에서 마지막으로 방문되는 점의 분포(균등)를 선형계·commute time=2m·R_eff로 풀고, 그래프 입력 범용 솔버와 MC로 검증한다. | mac |
| QPROB-A06 | 분기 과정과 확률생성함수 | 자손 수 분포 p_k인 Galton-Watson 과정의 소멸 확률(G(s)=s 최소근)과 세대별 평균·분산을 PGF로 유도하고, '아메바가 1/4 확률로 죽고/1개/2개/3개로 분열' 문제(√2-1)를 풀어 세대 시뮬레이션으로 검증한다. | mac |
| QPROB-A07 | 무작위 순열의 사이클: 100명의 죄수 문제 | 100명의 죄수 문제의 사이클-따라가기 전략 성공 확률(1-Σ_{k=51}^{100}1/k≈0.312)을 유도하고, 최장 사이클 길이 분포·특정 원소가 속한 사이클 길이가 균등임을 증명한 뒤 MC와 정확 DP로 확인한다. | mac |
| QPROB-A08 | 고급 기하 확률: 반원·삼각형·Buffon | 원 위 n점이 한 반원 안에 있을 확률(n/2^{n-1}), 원 위 무작위 삼각형이 중심을 포함할 확률(1/4)과 구 위 사면체(1/8), 원판 내 무작위 삼각형의 예각 확률, Buffon 바늘로 π 추정의 분산을 대칭성/적분으로 풀고 MC로 검증한다. | mac |
| QPROB-A09 | 분산 감소 기법으로 퍼즐 답 정밀화 | 앞선 퍼즐(예: e 추정, 막대 삼각형, 도박꾼 파산)에 antithetic 변수, control variate, 층화 표본, 조건부 MC(Rao-Blackwell)를 적용해 분산 감소비와 '유효 샘플 수'를 표로 비교한다(희귀사건 importance sampling은 see:SIM-M10). | mac |
| QPROB-A10 | 모의 퀀트 면접 세트: 시간제한 혼합 문제 | 앞선 아이템에서 문제 20개를 무작위 출제하는 타이머 드릴(답 입력→정확값 자동 채점, 풀이 시간 기록)을 만들고, 각 문제마다 '30초 직관 답 → 정확 풀이 → MC 검증' 3단계 기록과 약점 태그 통계를 출력한다. | mac |

### QFIN — 시장 미시구조 · 트레이딩 시스템 (Market Microstructure & Trading Systems)

시장 미시구조·트레이딩 시스템 트랙은 HRT·Jane Street·Citadel Securities·Optiver·IMC·Jump 같은 회사의 quant developer / trading-systems SWE 면접에서 요구하는 도메인 지식을 직접 구현하며 익힌다. 주문 유형과 매칭 규칙, 시장 데이터 피드 복구, FIX 세션, 사전 리스크와 OMS, 백테스터, 옵션 가격결정, 마켓메이킹·실행 알고리즘까지 C++(실행 경로)와 Python(분석)으로 만든다. 오더북 자료구조·ITCH·SBE·tick-to-trade 지연 최적화는 LLAT 트랙을 토대로 쌓는다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| QFIN-B01 | 가격·수량 표현: 고정소수점 틱과 반올림 | "101.2350" 같은 10진 문자열을 int64 가격(1e-4 단위)·틱 인덱스로 변환하는 파서와 틱 크기 테이블(가격대별 틱, 예: $1 미만 0.0001)·반올림 규칙(매수 내림/매도 올림)을 C++로 구현하고, double 누적 오차로 같은 가격이 다른 레벨에 들어가는 버그를 재현·수정하는 단위 테스트로 검증한다. | mac |
| QFIN-B02 | 주문 유형·TIF 의미론과 주문 상태 머신 | 주문 유형×TIF 조합의 유효성 검사기와 주문 상태 머신(PendingNew→New→PartiallyFilled→Filled/Canceled/Rejected/Expired, 허용되지 않는 전이 거부)을 C++로 구현하고, 표 기반 테스트(각 유형·TIF별 기대 상태 전이)로 검증한다. | mac |
| QFIN-B03 | 매칭 규칙 확장: IOC/FOK/Post-only/STP | LLAT-M08 오더북 엔진(기초는 see:LLAT-M08)에 IOC 잔량 취소, FOK 사전 체결가능 수량 검사, Post-only 교차 시 거부/재가격(slide), 자기체결 방지(STP: cancel-newest/cancel-oldest/decrement)를 추가하고, 시나리오별 체결 리포트를 골든 파일과 대조한다. | mac |
| QFIN-B04 | L1/L2/L3: MBO에서 MBP 집계 뷰 만들기 | 주문 단위(MBO) 이벤트 스트림(add/cancel/modify/execute)을 받아 상위 N레벨 MBP 뷰(가격·총수량·주문 수)를 유지하고 변경된 레벨만 증분 업데이트로 내보내는 C++ 빌더를 구현해, 매 이벤트 후 전체 재계산 결과와 일치함을 랜덤 테스트로 검증한다(오더북 자료구조는 see:LLAT-M09). | mac |
| QFIN-B05 | 호가 지표: Spread·Mid·Microprice·Imbalance | B04의 MBP 스냅샷 시퀀스에서 spread, mid, size-weighted mid, microprice, top-N order book imbalance를 Python으로 계산하고, 합성 데이터에서 imbalance가 다음 mid 변화 방향을 얼마나 예측하는지(조건부 평균·hit rate)를 측정한다. | mac |
| QFIN-B06 | 체결 데이터 분석: OHLCV·VWAP·수익률·실현 변동성 | 합성 체결 틱(또는 공개 암호화폐 체결 데이터)을 pandas로 시간/거래량/달러 바(bar)로 집계해 OHLCV·VWAP을 만들고, 단순 vs 로그 수익률, 실현 변동성의 연율화, 샘플링 주기에 따른 microstructure noise(bid-ask bounce) 효과를 그래프로 확인한다. | mac |
| QFIN-B07 | 포지션·PnL 회계: 실현/미실현·수수료·리베이트 | 다종목 체결(부분 체결, 방향 전환 포함)을 받아 평균단가/FIFO 방식의 실현·미실현 PnL, maker 리베이트·taker 수수료, mark-to-market(mid/last 기준)을 계산하는 C++ 포지션 키퍼를 구현하고, 손계산한 시나리오 표와 두 방식의 총 PnL이 같음(배분만 다름)을 테스트로 검증한다. | mac |
| QFIN-B08 | FIX 1: tag=value 파서·BodyLength·CheckSum | SOH 구분 tag=value 메시지를 할당 없이 파싱하는 C++ 파서(string_view 기반, 반복 그룹 NoMDEntries 포함)와 인코더를 구현하고, BodyLength(9)·CheckSum(10, 바이트 합 mod 256) 계산/검증, 잘린 메시지·잘못된 체크섬 퍼징 테스트로 견고성을 검증한다. | mac |
| QFIN-B09 | 옵션 기초: 페이오프·Put-Call Parity·무차익 경계 | 콜/풋/스프레드/스트래들 페이오프를 그리고, 합성 옵션 체인(호가 포함)에서 put-call parity 위반, 행사가에 대한 단조성·볼록성(버터플라이) 위반, 상·하한 경계 위반을 찾아 차익 거래 포지션과 확정 이익을 출력하는 Python 검출기를 구현한다. | mac |
| QFIN-B10 | 이항 트리 가격결정: CRR·유럽형/미국형 | CRR 이항 트리로 유럽형·미국형 콜/풋을 C++로 가격결정하고, 단계 수 N에 따른 Black-Scholes 수렴(진동 포함)과 무배당 미국형 콜 = 유럽형 콜, 미국형 풋의 조기 행사 경계를 수치로 확인한다. | mac |
| QFIN-B11 | Black-Scholes 공식과 Greeks | erfc 기반 정규 CDF로 BS 가격·해석적 Greeks를 C++로 구현해 중앙 차분 수치 미분 및 B10 이항 트리 결과와 대조하고, 만기·moneyness에 따른 gamma/vega 형태를 Python으로 그려 면접식 직관 질문(ATM 근만기 gamma 등)에 답을 정리한다(확률 기초는 see:QPROB). | mac |
| QFIN-M01 | 개장/폐장 경매: 단일가 Uncrossing | 누적 매수/매도 곡선에서 최대 체결량 → 최소 불균형 → 시장 압력 → 기준가 근접 순 tie-break로 단일 체결가를 결정하고 가격-시간 우선으로 배분하는 경매 모듈을 C++로 구현해, 손계산 예제와 indicative price/imbalance 공시 메시지 생성을 테스트로 검증한다. | mac |
| QFIN-M02 | Pro-Rata 매칭과 하이브리드 배분 | 레벨 내 수량 비례 배분(최소 배분 수량, 내림 후 잔여 분배, top-order 우선권 포함)과 FIFO/pro-rata 혼합 비율 매칭을 B03 엔진의 전략 플러그인으로 구현하고, 총 체결량 보존·결정성을 속성 기반 테스트로 검증하며 큐 위치 가치가 FIFO와 어떻게 다른지 정리한다. | mac |
| QFIN-M03 | 피드 아비트리지: A/B 라인 병합 | MoldUDP64 스타일(세션, 시퀀스, 메시지 수) 패킷을 A/B 두 UDP 멀티캐스트 라인으로 송신하고 드롭·재정렬·중복을 주입하는 시뮬레이터를 만든 뒤, 먼저 도착한 쪽을 채택·중복 제거·갭 감지하는 C++ 아비트레이터를 구현해 출력 시퀀스의 완전성과 추가 지연 분포를 측정한다(UDP 헤더 파싱은 see:COMM-A04). | mac |
| QFIN-M04 | 갭 복구와 스냅샷 동기화 | 갭 발생 시 재전송 요청(MoldUDP64 rewind 개념) 또는 스냅샷 채널 구독으로 복구하되, 스냅샷 수신 중 증분 메시지를 버퍼링했다가 스냅샷 시퀀스 N 이후분만 재적용하는 상태 머신을 구현하고, 무작위 손실 주입 후 복구된 오더북이 무손실 기준 북과 일치함을 검증한다. | mac |
| QFIN-M05 | FIX 2: 세션 계층 — 시퀀스·하트비트·재전송 | B08 파서 위에 Logon/Logout, Heartbeat/TestRequest 타임아웃, MsgSeqNum 검증, ResendRequest 처리(PossDupFlag 재전송, 관리 메시지는 SequenceReset-GapFill), 시퀀스 번호 영속화를 갖춘 이니시에이터/억셉터를 구현하고, 메시지 유실·재접속·시퀀스 역전 주입 테스트로 검증한다(QuickFIX와 상호 운용은 선택). | mac |
| QFIN-M06 | 주문 게이트웨이와 사전 리스크 체크 | 가격 밴드(fat-finger), 최대 수량/명목금액, 종목·계좌별 포지션 한도(미체결 포함 worst-case), 주문 속도 제한, 미체결 주문 수, kill switch를 갖춘 리스크 게이트를 할당 없이 구현하고(see:LLAT-B08, 속도 제한은 see:ALGO-A11) 각 체크의 위반 테스트와 체크당 지연 p99를 측정한다(SEC 15c3-5 요구사항과 대응표 작성). | mac |
| QFIN-M07 | OMS: ExecutionReport 처리와 Cancel/Replace 경쟁 | ClOrdID/OrigClOrdID 체인으로 cancel/replace를 관리하는 OMS를 구현해, 취소 요청 중 체결(too-late-to-cancel), replace 중 부분 체결, 순서가 뒤바뀐 ExecutionReport, 거래소 측 unsolicited cancel 시나리오에서 잔량·포지션이 정확한지 시나리오 테스트로 검증한다. | mac |
| QFIN-M08 | 이벤트 기반 백테스터 1: 이벤트 루프·체결 시뮬레이터 | 시간순 이벤트 큐(시장 데이터·타이머·주문 응답), 전략 콜백 인터페이스, 지연(latency) 파라미터를 가진 체결 시뮬레이터(시장가는 반대 호가 소진, 지정가는 교차 시 체결)를 C++ 또는 Python으로 구현하고, 같은 시드로 두 번 돌려 결과가 비트 단위로 같은지와 B07 PnL 회계와의 일치를 검증한다. | mac |
| QFIN-M09 | 백테스터 2: 편향·비용 모델·성과 지표 | 의도적으로 look-ahead(종가로 같은 봉에 체결), survivorship(상장폐지 종목 제외), 수수료·슬리피지 누락, 지정가 큐 위치 무시 버그를 넣은 버전과 수정 버전을 비교해 Sharpe·최대 낙폭·턴오버 변화를 표로 만들고, 다중 검정에 의한 과최적화(파라미터 그리드 탐색 후 out-of-sample 붕괴)를 보인다. | mac |
| QFIN-M10 | 내재 변동성: Newton-Raphson + 안전한 대안 | vega 기반 Newton-Raphson(초기값 Brenner-Subrahmanyam 근사)에 bracketing bisection/Brent 폴백을 결합한 IV 솔버를 C++로 구현하고, 깊은 ITM/OTM·만기 직전·무차익 경계 밖 가격에서의 수렴 실패를 테스트로 재현·처리한다(일반 근 찾기 이론은 see:NUM). | mac |
| QFIN-M11 | Monte Carlo 옵션 가격결정과 분산 감소 | GBM 경로 생성으로 유럽형(BS와 대조)·아시안·배리어 옵션을 C++로 가격결정하고, 표준오차 기반 신뢰구간, antithetic/control variate(기하평균 아시안)의 분산 감소율, 배리어 이산 모니터링 편향을 측정한다(Brownian motion 기초는 see:QPROB). | mac |
| QFIN-M12 | 통계적 차익: 페어 트레이딩과 공적분 | 합성 공적분 쌍과 비공적분 쌍(상관만 높은 랜덤워크)을 생성해 Engle-Granger(OLS 헤지 비율 + ADF 검정), OU 반감기 추정, z-score 진입/청산 규칙을 Python(statsmodels)으로 구현하고, M08 백테스터로 거래 비용 포함 성과와 spurious regression 함정을 비교한다. | mac |
| QFIN-M13 | 실행 알고리즘: TWAP·VWAP·POV | 과거 거래량 곡선(U자형) 기반 VWAP 스케줄러, 무작위화된 TWAP, 참여율(POV) 알고리즘을 M08 백테스터 위의 child order 생성기로 구현하고, 선형+제곱근 충격 모형 하에서 도착가 대비 implementation shortfall과 VWAP 슬리피지를 비교한다. | mac |
| QFIN-A01 | 마켓메이킹 1: 재고 위험과 Avellaneda-Stoikov | 체결 강도 λ(δ)=A·e^(−kδ)인 포아송 도착 시뮬레이터에서 Avellaneda-Stoikov 호가와 대칭 고정 스프레드 호가를 비교해 PnL 분포·재고 분포·Sharpe를 측정하고, 위험 회피 γ와 k가 스프레드·재고 skew에 주는 영향을 그래프로 정리한다(베팅/마켓메이킹 게임 직관은 see:QPROB). | mac |
| QFIN-A02 | 마켓메이킹 2: 역선택·큐 포지션·Markout | 정보 있는 흐름(미래 가격을 아는 taker) 비율을 조절하는 LOB 시뮬레이터에서 큐 위치 추정 기반 지정가 체결 모형을 만들고, 체결 후 1/10/100 이벤트 markout으로 역선택 비용을 측정해 B05 imbalance 기반 호가 skew·철회 로직이 markout을 얼마나 개선하는지 보인다. | mac |
| QFIN-A03 | 변동성 스마일·표면 피팅과 무차익 검사 | M10 IV 솔버로 합성(또는 공개) 옵션 체인의 smile을 만들고 만기별 SVI 파라미터화를 최소제곱으로 피팅한 뒤, 버터플라이(밀도 ≥ 0)·캘린더(총분산 단조) 차익 조건을 검사하고 sticky-strike vs sticky-delta에 따른 Greeks 차이를 정리한다. | mac |
| QFIN-A04 | 실시간 포트폴리오 리스크: Greeks 집계·시나리오 | 체결·가격 틱마다 기초자산별 delta/gamma/vega/theta를 증분 갱신하는 C++ 리스크 엔진과, 기초가격×변동성 충격 시나리오 그리드 PnL, historical VaR을 계산해 전체 재계산 결과와 일치·갱신 지연을 측정하고 델타 헤지 트리거 규칙을 설계한다. | mac |
| QFIN-A05 | Almgren-Chriss 최적 실행 | 영구·일시 선형 충격 하 Almgren-Chriss 해(sinh 궤적)를 구현해 위험 회피 λ별 기대 비용-분산 효율적 경계를 그리고, M13 TWAP/VWAP과 Monte Carlo로 비교해 이론 경계 위에 놓이는지 확인한다. | mac |
| QFIN-A06 | 스마트 오더 라우터(SOR) | 거래소별 호가·수수료/리베이트·지연·숨은 유동성 체결 확률을 가진 다중 거래소 시뮬레이터에서 가격 우선(Reg NMS trade-through 금지) 스윕, 수수료 반영 순가격 라우팅, 지연 차를 고려한 동시 도착(sweep timing) 라우터를 구현해 체결률·평균 비용을 비교한다. | mac |
| QFIN-A07 | 거래소 설계: 시퀀서·저널·결정적 리플레이 | TCP 주문 게이트웨이(M05 FIX 세션) → 단일 시퀀서 → 매칭 엔진 → 저널 → 멀티캐스트 시장 데이터 발행(M03 형식)으로 이어지는 미니 거래소를 구현하고, 저널 리플레이로 매칭 상태가 비트 단위로 재현됨과 primary 장애 시 backup이 저널로 이어받는 failover를 시연한다(파이프라인 지연 분해는 see:LLAT-A07, 복제 모델은 see:DIST-M05). | mac |
| QFIN-A08 | FPGA 트레이딩 가속: 사이클 모델 피드 파서 | 64비트/클럭 데이터 경로로 ITCH 메시지를 cut-through 파싱하고 top-of-book을 갱신해 트리거 주문을 내는 파이프라인을 C++ 사이클 모델로 구현해 첫 바이트 도착→주문 첫 바이트 송신 사이클 수를 계산하고, 소프트웨어 경로(LLAT-A07)와의 지연 예산을 비교한다(HDL 구현·검증은 see:RTLDV, 선택적으로 Verilator). | mac |
| QFIN-A09 | 거래소 연결성: 코로케이션·전송 매체·시간 동기 | 광섬유(n≈1.47) vs 마이크로웨이브 vs 직선 거리로 시카고–뉴저지 편도 지연을 계산하고, 코로 내부 크로스커넥트·스위치 홉 지연 예산, MiFID II RTS 25 시계 정확도(UTC 대비 100µs) 준수를 위한 PTP 오프셋 모니터링 스크립트를 Python으로 작성해 설계 문서로 정리한다(PTP는 see:DIST-M01, 타임스탬핑은 see:LLAT-A04). | mac |

### NUM — 수치해석 · 선형대수 (Numerical Methods & Linear Algebra)

로봇의 EKF·LQR·최소제곱, ML의 학습·추론, 퀀트의 가격결정·리스크는 모두 같은 수치해석·선형대수 위에 서 있고, 버그의 상당수는 알고리즘이 아니라 반올림 오차·조건수·불안정한 분해에서 나온다. 이 트랙은 부동소수점 오차 해석부터 LU/QR/SVD, 반복 해법, 최적화, 자동 미분까지 C/C++로 직접 구현하고 numpy/scipy를 기준 오라클로 대조해 '왜 이 방법이 안정한가'를 손으로 확인한다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| NUM-B01 | 부동소수점 오차 모델: Machine Epsilon과 ULP | float/double의 machine epsilon을 루프로 구하고 nextafter 기반 ULP 거리 함수(부호·±0·비정규수 처리)를 구현해, libm sinf/expf의 최대 ULP 오차를 long double(또는 Python mpmath) 오라클로 측정한다 (비트 분해는 see:ARCH-B03). | mac |
| NUM-B02 | Catastrophic Cancellation과 수식 재구성 | 근의 공식(b²≫4ac), 1−cos x, log(1+x)/exp(x)−1, 분산(naive one-pass vs two-pass vs Welford)을 naive/안정 버전으로 각각 구현하고, 입력 범위별 상대오차를 mpmath 오라클 기준으로 그려 잃어버린 유효 자릿수를 설명한다. | mac |
| NUM-B03 | 합산 알고리즘: Naive·Pairwise·Kahan·Neumaier | 1e7개 랜덤·악조건(큰 값과 작은 값 혼합) 수열의 합을 naive, pairwise, Kahan, Neumaier로 구현해 math.fsum 대비 오차와 속도를 비교하고, -ffast-math(재결합 허용)에서 Kahan 보정 항이 최적화로 사라지는 것을 어셈블리로 확인한다 (FMA·결정성 이슈는 see:PHYS-A04). | mac |
| NUM-B04 | 근 찾기: Bisection·Newton·Secant | 세 방법과 bracket을 유지하는 safeguarded Newton(Newton 스텝이 구간을 벗어나면 bisection 폴백)을 구현해 반복별 오차로 수렴 차수(1, 2, 1.618)를 실측하고, 중근·순환·도함수 0 사례와 종료 조건(|f|, |Δx|, 최대 반복)을 테스트한다 (고정소수점 Newton 역수는 see:DSP-B12). | mac |
| NUM-B05 | 다항식 평가와 보간: Horner·Newton 차분·Runge 현상 | Horner 평가, Newton divided difference 보간, barycentric Lagrange 보간을 구현하고 등간격 vs Chebyshev 노드로 1/(1+25x²)를 보간해 차수별 최대 오차(Runge 현상)를 numpy.polynomial 결과와 비교한다 (minimax 근사는 see:DSP-M10). | mac |
| NUM-B06 | 3차 스플라인과 삼중대각 풀이 (Thomas) | Thomas 알고리즘으로 삼중대각 시스템을 O(n)에 풀어 natural/clamped cubic spline을 구현하고, 센서 보정 테이블·웨이포인트 보간에 적용해 scipy.interpolate.CubicSpline과 대조하며 Thomas가 피벗 없이 안정한 조건(대각 우세)을 확인한다 (선형 보간 LUT는 see:ALGO-B03). | mac |
| NUM-B07 | 수치 미분: 스텝 크기 h의 절단·반올림 트레이드오프 | forward/central difference와 complex-step 미분을 구현해 h=1e-1..1e-16에서 오차를 log-log로 그리고, 최적 h ≈ √ε(forward), ∛ε(central) 이론치와 비교하며 complex-step이 뺄셈이 없어 기계 정밀도에 도달함을 보인다. | mac |
| NUM-B08 | 수치 적분: Trapezoid·Simpson·Romberg·Gauss-Legendre | 네 방법과 adaptive Simpson을 구현해 매끄러운/끝점 특이점이 있는 피적분 함수에서 함수 평가 횟수 대비 오차와 수렴 차수를 측정하고 scipy.integrate.quad와 비교한다 (ODE 적분기는 see:SIM-B06, PHYS-B01). | mac |
| NUM-B09 | 행렬 라이브러리 기초: 레이아웃·BLAS 레벨·노름 | row/column-major와 leading dimension(lda)을 지원하는 C 행렬 타입에 BLAS 레벨 1/2/3 연산(axpy, dot, gemv, naive gemm)과 1/2/∞/Frobenius 노름을 구현해 numpy와 랜덤 대조 테스트하고, 레벨별 산술 강도(FLOP/byte)를 계산한다 (GEMM 블로킹 최적화는 see:EML-B02). | mac |
| NUM-B10 | 가우스 소거와 부분 피벗 (Partial Pivoting) | 부분 피벗 가우스 소거로 Ax=b를 풀고, [[ε,1],[1,1]] 같은 행렬에서 피벗 없이 해가 무너지는 것을 재현하며, 성장 인자(growth factor)·잔차 ‖b−Ax‖·전진 오차를 numpy.linalg.solve 결과와 비교한다. | mac |
| NUM-B11 | LU 분해: PA=LU와 다중 우변 재사용 | in-place PA=LU(피벗 벡터 저장), 전진/후진 대입, 행렬식을 구현하고 우변 k개를 LU 1회로 푸는 비용을 측정하며, 역행렬을 구해 곱하는 방식보다 solve가 정확하고 빠름을 scipy.linalg.lu_factor/lu_solve와 대조해 보인다. | mac |
| NUM-B12 | Cholesky 분해와 상관된 난수 생성 | LLᵀ와 LDLᵀ 분해를 구현해 SPD 판정(음/0 피벗 검출)과 해 풀이를 numpy.linalg.cholesky와 대조하고, 공분산 행렬로 상관 정규 난수를 생성해 표본 공분산이 목표값에 수렴함을 확인한다 (Monte Carlo 가격결정 응용은 see:QFIN). | mac |
| NUM-M01 | 조건수와 오차 해석: Forward vs Backward Error | LU 기반 1-노름 조건수 추정기(Hager/Higham)를 구현해 numpy.linalg.cond와 비교하고, Hilbert·Vandermonde 행렬에서 b 섭동에 따른 해의 상대오차가 κ(A)·ε 한계를 따름과 '잔차가 작아도 해는 틀릴 수 있음'을 보인다. | mac |
| NUM-M02 | QR 1: Classical vs Modified Gram-Schmidt | CGS, MGS, 재직교화 CGS2를 구현해 조건수 1e2..1e12 행렬에서 ‖QᵀQ−I‖를 측정하고, 직교성 손실이 κ², κ, ε 수준으로 각각 다르게 커지는 것을 로그 그래프로 확인한다. | mac |
| NUM-M03 | QR 2: Householder 반사와 Givens 회전 | Householder 벡터를 하삼각에 저장하는 Householder QR과 Givens 회전 QR을 구현해 numpy.linalg.qr과 대조하고(부호 정규화), 새 측정 행이 들어올 때 Givens로 R을 O(n²)에 갱신하는 스트리밍 QR 업데이트를 구현한다. | mac |
| NUM-M04 | 선형 최소제곱: 정규방정식 vs QR vs SVD | 다항식 피팅·센서 보정 문제를 정규방정식(Cholesky), Householder QR, SVD로 풀어 κ(AᵀA)=κ(A)²로 인한 정밀도 손실을 고차 다항식에서 재현하고, 가중 최소제곱(WLS)과 numpy.linalg.lstsq 결과를 비교한다 (응용은 see:RCTL-M11, SENS-M03). | mac |
| NUM-M05 | 재귀 최소제곱(RLS)과 망각 인자 | 망각 인자 λ를 갖는 RLS를 C로 구현해 시변 파라미터(예: 배터리 내부저항 드리프트)를 추적하고 배치 LS와 결과를 대조하며, float32 장시간 실행에서 P 행렬이 비대칭·비양정치로 무너지는 현상과 대칭화·공분산 리셋 대책을 시험한다 (Kalman과의 관계는 see:RCTL-M04). | mac |
| NUM-M06 | 고유값 1: Power·Inverse·Rayleigh Quotient Iteration | power iteration, shift-inverse iteration(LU 재사용), Rayleigh quotient iteration을 구현해 |λ2/λ1| 비율에 따른 수렴 속도와 RQI의 3차 수렴을 측정하고, PageRank(마르코프 전이행렬) 예제로 numpy.linalg.eig와 대조한다. | mac |
| NUM-M07 | 고유값 2: Jacobi 회전과 대칭 QR 알고리즘 | 대칭 행렬에 대해 cyclic Jacobi 고유값 법과, Householder 삼중대각화 + Wilkinson shift 암시적 QR 알고리즘을 구현해 numpy.linalg.eigh와 고유값·고유벡터(부호 정규화)를 대조하고 크기별 반복 횟수·시간을 비교한다. | mac |
| NUM-M08 | SVD와 PCA, 저랭크 근사 | one-sided Jacobi SVD를 구현해 numpy.linalg.svd와 대조하고, AᵀA 고유분해로 SVD를 구하면 작은 특이값이 사라지는 함정을 보이며, 센서 로그 PCA와 Eckart-Young 저랭크 근사·truncated pseudo-inverse를 적용한다 (SVD 응용은 see:CV-M07, AUTON-M03). | mac |
| NUM-M09 | 반복 해법 1: Jacobi·Gauss-Seidel·SOR | 2D 포아송(정상 상태 열전도 판) 5점 스텐실 시스템을 Jacobi, Gauss-Seidel, SOR(ω 스윕)로 풀어 반복 횟수를 반복 행렬 스펙트럼 반경 이론치·최적 ω와 비교하고, 대각 우세가 깨진 행렬에서 발산을 재현한다. | mac |
| NUM-M10 | 반복 해법 2: Conjugate Gradient와 전처리 | CSR 기반 CG와 Jacobi/불완전 Cholesky(IC0) 전처리 PCG를 구현해 M09 포아송 문제에서 격자 크기별 반복 횟수(√κ 스케일링)를 측정하고 scipy.sparse.linalg.cg와 대조한다 (CSR/SpMV 기초는 see:DS-M12). | mac |
| NUM-M11 | 희소 직접법: Fill-in과 재정렬 (RCM) | 대역(banded)/skyline 저장 Cholesky를 구현해 격자·그래프 행렬의 fill-in을 세고, 직접 구현한 Reverse Cuthill-McKee 재정렬 전후의 대역폭·fill-in·시간을 scipy.sparse.csgraph.reverse_cuthill_mckee 결과와 비교한다 (포즈 그래프 희소 풀이 응용은 see:AUTON-A01). | mac |
| NUM-M12 | 연립 비선형 방정식: 다변수 Newton과 라인 서치 | 해석/유한차분 Jacobian을 쓰는 다변수 Newton과 backtracking(Armijo) 감쇠를 구현해 UWB/GPS 거리 측정으로 위치를 구하는 삼변측량(trilateration)과 다이오드 회로 방정식에 적용하고, 초기값별 수렴 영역과 scipy.optimize.root 결과를 비교한다. | mac |
| NUM-M13 | 1차 최적화: GD·Momentum·Nesterov·Adam | 네 가지 최적화기를 C로 구현해 조건수가 다른 2차 함수와 Rosenbrock에서 수렴 궤적·반복 수를 비교하고, 학습률 상한 2/L과 GD 수렴률 (κ−1)/(κ+1), momentum의 (√κ−1)/(√κ+1) 개선을 실측으로 확인한다. | mac |
| NUM-A01 | 비선형 최소제곱: Gauss-Newton과 Levenberg-Marquardt | GN과 LM(λ 적응 갱신, 스텝 승인/거절)을 구현해 배터리 RC 모델 지수 곡선 피팅과 원·구 기하 거리 피팅(SENS-M03 대수적 해를 초기값으로)에 적용하고, scipy.optimize.least_squares와 해·반복 수·초기값 민감도를 비교한다 (응용은 see:CV-A01, AUTON-A01). | mac |
| NUM-A02 | 준뉴턴: BFGS와 L-BFGS | 강한 Wolfe 조건 라인 서치와 BFGS, two-loop recursion L-BFGS(m=3..20)를 구현해 고차원 Rosenbrock·로지스틱 회귀에서 반복 수·메모리를 GD/Newton과 비교하고 scipy.optimize.minimize(L-BFGS-B)와 대조한다. | mac |
| NUM-A03 | 자동 미분 1: Forward Mode (Dual Number) | C++ 템플릿 dual number 타입(사칙연산 + sin/exp/sqrt/atan2 규칙)을 구현해 EKF 운동·관측 모델의 Jacobian을 자동으로 구하고, 해석 미분·유한차분·complex-step(B07) 결과와 정확도·비용을 비교한다 (EKF는 see:RCTL-M05). | mac |
| NUM-A04 | 자동 미분 2: Reverse Mode 테이프 | 연산 그래프를 테이프에 기록하고 위상 역순으로 adjoint를 전파하는 reverse-mode AD(micrograd 스타일)를 C++로 구현해 작은 MLP/로지스틱 회귀를 M13 Adam으로 학습시키고, 유한차분 gradient check와 입력·출력 차원에 따른 forward vs reverse 비용을 측정한다. | mac |
| NUM-A05 | 회전의 수치: 정규화 드리프트와 SO(3) Exp/Log | 자이로 적분 시 쿼터니언/회전행렬의 정규성 드리프트를 측정해 재정규화(normalize vs SVD 직교 투영)를 비교하고, SO(3) exp/log map을 θ→0·π 근처 Taylor 분기로 안정하게 구현하며 쿼터니언 평균(Markley 고유벡터 법)을 scipy.spatial.transform과 대조한다 (기초 연산은 see:GFX-B04, PHYS-B08). | mac |
| NUM-A06 | 칼만 필터 수치 안정성: Joseph Form과 Square-Root | 표준 공분산 갱신, Joseph form, UD 분해(Bierman/Thornton) 또는 Cholesky square-root 갱신을 같은 필터에 구현해 float32 장시간 실행·매우 정확한 센서 조건에서 P의 대칭성·양정치성 붕괴를 재현하고 double 기준 대비 오차를 비교한다 (KF/EKF 기초는 see:RCTL-M04, AUTON-M01). | mac |
| NUM-A07 | 블록 LU와 LAPACK 호출 (Level-3 BLAS) | GEMM 업데이트를 쓰는 right-looking blocked LU를 구현해 B11 비블록 LU 대비 GFLOPS를 블록 크기별로 측정하고, Accelerate의 LAPACK dgetrf/dgesv·dpotrf를 column-major·lda·ipiv(1-based)·info 처리와 함께 호출해 결과와 성능을 대조한다 (GEMM 커널은 see:EML-B02, 루프라인은 see:AIPERF). | mac |
| NUM-A08 | 혼합 정밀도와 반복 정제 (Iterative Refinement) | float32(및 bf16 반올림 에뮬레이션)로 LU를 계산하고 double 잔차로 반복 정제해 double 수준 정확도에 도달하는 조건(κ·ε_low < 1)과 발산 경계를 실험으로 확인하고, GPU 텐서코어 HPL-MxP가 이 원리를 쓰는 이유를 FLOP·대역폭 관점에서 정리한다 (see:GPGPU). | mac |
| NUM-A09 | 고정소수점 vs 부동소수점: 임베디드 선형대수 선택 | 3×3 행렬 곱·역행렬과 2상태 칼만 갱신을 Q31(64비트 누산, 블록 스케일링)과 float32로 각각 구현해 동적 범위·오버플로·누적 오차를 double 오라클과 비교하고, 명령 수 기반 Cortex-M0(FPU 없음) vs M4F 사이클 추정과 함께 선택 기준을 설계 메모로 정리한다 (Q-format 기초는 see:DSP-B01). | mac |

## 13단계 · 종합 (Capstone)

### SYSD — 시스템 설계 (System Design)

임베디드/펌웨어 직무의 시스템 설계 면접은 웹 스케일 설계와 달리 전력·메모리·연결성·안전·현장 업데이트 같은 제약을 함께 다룬다. 이 트랙의 각 항목은 '요구사항 정리 → 수치 추정 → 구조도 → 핵심 코드 경로 구현 → 트레이드오프 문서화'를 45~60분 안에 수행하는 실전 연습이며, 핵심 경로는 C/C++로 호스트에서 검증한다. 2026-11-07 확장: 코어·인터럽트·격리·메모리 계층 같은 칩/플랫폼 아키텍처 결정과 오디오·USB·스토리지·콤보칩·베이스밴드·가속기 등 도메인별 시스템 설계를 추가했다. 다른 트랙에서 이미 구현하는 내용은 see: 로 가리키고 이 트랙에서는 설계 결정만 다룬다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| SYSD-B01 | 설계 면접 프레임워크: 45분 진행 템플릿 | 단계별 시간 배분·질문 목록·화이트보드 레이아웃을 담은 1페이지 템플릿을 만들고, '스마트 온도조절기' 문제에 45분 타이머로 적용해 녹음·자기평가한다. | mac |
| SYSD-B02 | 요구사항 도출: 기능·비기능·제약 | '웨어러블 걸음 수 측정기'와 '산업용 진동 센서' 프롬프트에 대해 기능/비기능(지연·전력·메모리·보안·안전·규제) 요구사항과 면접관에게 던질 확인 질문 15개를 작성한다. | mac |
| SYSD-B03 | Back-of-envelope 추정: 대역폭·저장·전력·플래시 수명 | 텔레메트리 대역폭, 배터리 수명(슬립/액티브 듀티), 플래시 P/E 수명, 플릿 클라우드 저장량 등 추정 문제 12개를 풀고 Python 계산기로 답을 검증, 자주 쓰는 수치 치트시트를 만든다. | mac |
| SYSD-B04 | 트레이드오프 글쓰기: ADR 3편 | 폴링 vs 인터럽트, SPI vs I2C 센서 버스, MQTT vs 자체 UDP 프로토콜 세 가지 결정을 ADR 형식(맥락/결정/대안/결과)으로 작성하고, 각 결정을 뒤집을 조건을 명시한다. | mac |
| SYSD-B05 | 인터페이스 설계: HAL API & 메시지 스키마 진화 | 센서 HAL C API(초기화·비동기 읽기·에러 코드)를 설계하고 nanopb/protobuf 스키마를 v1→v2로 진화시키며 구/신 펌웨어·서버 간 하위/상위 호환을 테스트로 검증한다. | mac |
| SYSD-B06 | 장애 모드 분석(FMEA) & 관측가능성 설계 | 배터리 구동 원격 센서 노드의 FMEA 표(고장 모드·영향·감지·완화)를 작성하고, 리셋 사유·워치독·heartbeat·크래시 덤프 업로드를 포함한 관측 지표 목록을 설계한다. | mac |
| SYSD-B07 | Rate limiter 설계: MCU와 분산 서버 | ALGO-A11의 토큰 버킷을 바탕으로 MCU용(정수 연산·고정 메모리·틱 기반)과 분산 서버용(공유 저장소·원자적 갱신·클럭 오차) 설계안을 쓰고, 핵심 경로를 C++로 구현해 버스트 트래픽 테스트로 비교한다(알고리즘 자체는 see:ALGO-A11). | mac |
| SYSD-B08 | 링 버퍼 기반 로깅 시스템 설계 | 리셋 후 보존되는 RAM 영역(.noinit) 링 버퍼, 지연 포맷(id + 인자), 레벨 필터, 플래시 덤프·업로드 경로를 설계하고 핵심 링 버퍼·덤프 코드를 구현해 '리셋 직전 로그 보존' 테스트를 통과시킨다. | mac |
| SYSD-B09 | 설계 문서의 뼈대: 요구사항에서 위험까지 | 배경·목표/비목표·요구사항(B02)·수치 추정(B03)·구조도·인터페이스(B05)·대안과 결정(B04 ADR)·위험과 완화(B06 FMEA)·검증 계획·미해결 질문 순서의 템플릿을 작성하고, '스마트 도어락'을 예제로 2쪽 이내로 채운 뒤 각 절이 면접 45분(B01) 중 어느 구간에 해당하는지 대응표를 만든다. | mac |
| SYSD-B10 | 코어 선택: Cortex-M · Cortex-R · Cortex-A | 모터 컨트롤러·SSD 컨트롤러·브레이크 ECU·Linux 게이트웨이·스마트 카메라 5개 제품에 대해 인터럽트 지연, MPU vs MMU, TCM·캐시, lockstep/ECC, 클럭·전력, 생태계(RTOS/Linux), 비용을 축으로 한 결정 매트릭스를 작성하고 이종 구성(A+M, R+A)의 역할 분담과 IPC를 설계한다 (코어 내부는 see:ARM). | mac |
| SYSD-B11 | 인터럽트 구조와 설계: NVIC vs GIC | NVIC(벡터 직접 진입, 우선순위 그룹, tail-chaining)와 GICv3(Distributor/Redistributor/CPU IF, SPI/PPI/SGI/LPI, affinity 라우팅)를 비교표로 정리하고, 제품 하나의 IRQ 목록·우선순위·코어 배치·지연 예산·인터럽트 스톰 대책을 설계 문서로 작성한다 (레지스터 실습은 see:BM-M01·SOCX-B10, 구현 없이 설계만). | mac |
| SYSD-B12 | 격리 설계: MPU 파티션 · MMU 프로세스 · 하이퍼바이저 | 같은 기능(제어 루프 + 통신 스택 + 서드파티 앱)을 MPU 파티션(RTOS), MMU 프로세스(Linux), 정적 파티션 하이퍼바이저 세 방식으로 나눠 보호 단위, 결함 격리, 시간 격리, 컨텍스트 스위치 비용, 인증(ASIL/DO-178C) 영향을 비교하고 선택 근거를 ADR로 남긴다 (구현은 see:SEC-M08·BM-A08·VIRT-A08). | mac |
| SYSD-B13 | 센싱 시스템 설계: 산업용 진동 모니터링 노드 | B02에서 요구사항을 뽑은 진동 센서를 이어받아 샘플링률·안티앨리어싱, ADC 분해능/ENOB 예산, 다채널 동기, DMA 버퍼 크기, 온보드 FFT/특징 추출 vs 원시 전송, 보정값 저장, 전력 예산을 결정하고 데이터 흐름도와 오차 예산표를 작성한다 (센서 구현은 see:SENS, 단일 센서 end-to-end는 SENS-A08). | mac |
| SYSD-B14 | 오디오 시스템 설계: 마이크 어레이에서 스피커까지 | 4-마이크 스마트 스피커를 예로 PDM/I2S/TDM 인터페이스 선택, 클럭 도메인과 ASRC, DMA 블록 크기 vs 지연, AEC·빔포밍·웨이크워드 처리 위치(DSP 코어 vs AP), 재생 경로와 BT/USB 출력, 언더런/오버런 정책을 설계하고 end-to-end 지연 예산을 계산한다 (I2S·DMA 구현은 see:DSP-A03, 프레이밍은 MEDIA-B10). | mac |
| SYSD-B15 | USB 장치 설계: 클래스 · 엔드포인트 · 전원 | CDC-ACM + MSC + DFU 복합 장치와 UAC2 + HID 오디오 장치 두 가지에 대해 인터페이스/엔드포인트 배정(하드웨어 EP 수 제약), FS/HS 전송 타입별 대역폭(isochronous 예약), 버스 전원(100/500 mA·suspend 2.5 mA)·BC1.2·USB-PD 협상, 펌웨어 업데이트 경로를 설계하고 디스크립터 트리를 문서화한다 (enumeration·컨트롤 전송 구현은 see:COMM-M15·COMM-A01). | mac |
| SYSD-B16 | 저전력 배터리 IoT 노드 설계: 10년 전력 예산 | CR2032/AA 리튬 셀로 10년을 목표로 하는 BLE·LoRa 환경 센서 노드의 상태별 전류(슬립·센싱·송신·피크), 듀티 사이클, 자가 방전·온도 감쇠, 피크 전류와 전압 강하(브라운아웃)를 표로 계산하고 RTC 웨이크업·센서 전원 게이팅·전송 배치 전략을 설계한다 (라디오 듀티 사이클은 see:RF-A04, 슬립 모드는 BM-M11). | mac |
| SYSD-M01 | A/B 부트로더 & 롤백 설계 | A/B 슬롯·이미지 헤더(버전, 서명, 해시)·trial boot·워치독 기반 confirm·rollback 카운터 상태 머신을 설계하고, 플래시 에뮬레이터 위에서 모든 단계 전원 차단 주입 테스트를 통과시킨다. | mac |
| SYSD-M02 | 펌웨어 OTA 시스템 (디바이스 + 서버) | 매니페스트 서명 검증, 청크 단위 재개 가능 다운로드, 델타 업데이트 선택 기준, 단계적 롤아웃/중단 정책을 설계하고, 끊김 주입 하에서 재개 다운로드 + 해시 검증 핵심 경로를 구현한다. | mac |
| SYSD-M03 | 센서 데이터 로거 (전원 차단 안전) | 1kHz 6축 IMU 로거의 DMA 더블 버퍼→압축→로그 구조 플래시 기록 파이프라인과 전원 차단 시 최대 손실량 보장을 설계하고, 기록·복구 경로를 플래시 에뮬레이터에서 구현·검증한다. | mac |
| SYSD-M04 | 드론 텔레메트리 파이프라인 | 기체→GCS(MAVLink)→클라우드 경로의 링크 예산, 메시지 우선순위/주기 조절, 링크 손실 시 버퍼링·다운샘플링, 지상 저장 스키마를 설계하고 우선순위 큐 기반 송신 스케줄러를 구현한다. | mac |
| SYSD-M05 | 카메라 시스템 설계: 센서→ISP→인코더→스트리밍 | 센서 캡처·ISP·H.264 인코더·네트워크 스트리밍 단계의 버퍼 풀(제로카피, 참조 카운트), 백프레셔, 프레임 드롭 정책, glass-to-glass 지연 예산을 설계하고, 3A(AE/AWB/AF) 통계→per-frame 파라미터 적용 경로와 2~4대 카메라 동기·대역폭(CSI-2 레인, DDR) 예산까지 설계 문서에 담은 뒤 버퍼 풀 핵심 코드를 구현한다 (ISP 알고리즘은 see:CAM, 멀티 카메라 동기는 CAM-A05). | mac |
| SYSD-M06 | BMC 플릿 매니저 | Redfish 폴링 vs 이벤트 구독, 센서 메트릭 수집 주기와 저장량, BMC 펌웨어 롤링 업데이트, 장애 BMC 격리를 설계하고 동시성 제한 폴러(백오프·타임아웃)를 구현한다. | mac |
| SYSD-M07 | 로봇 플릿 백엔드 | 로봇-클라우드 연결(MQTT/gRPC), 명령 전달 보장(멱등성, ack, 만료), 오프라인 로봇 처리, 맵/모델 배포를 설계하고 멱등 명령 큐 핵심 경로를 구현·테스트한다. | mac |
| SYSD-M08 | 디바이스 프로비저닝 & 인증서 수명 관리 | 제조 라인 키 주입(secure element), 디바이스 인증서 발급, mTLS 연결, 인증서 갱신/폐기, 기기 RMA 흐름을 설계하고 인증서 만료 전 갱신 상태 머신을 구현한다. | mac |
| SYSD-M09 | 디바이스 메트릭·로그 수집 시스템 | 디바이스 측 배치·압축·샘플링, 게이트웨이 집계, 수집 서버 백프레셔, 시계열 DB 다운샘플링/보존 정책을 설계하고 디바이스 측 배치 업로더(재시도·용량 상한)를 구현한다. | mac |
| SYSD-M10 | 드론 시스템 설계: 추력·전력·컴퓨트·링크 예산 | 임무(예: 25분 비행, 1 kg 페이로드, 5 km 영상 링크) 요구사항에서 추력 대 중량비, 모터/프롭/ESC, 배터리 용량과 비행시간, FC·컴패니언 컴퓨터 전력, 센서 구성(IMU 이중화, GPS, 거리 센서), RC/텔레메트리/영상 링크 버짓을 계산하는 스프레드시트형 모델을 작성하고 블록도를 그린다 (FC↔컴패니언 소프트웨어 분할은 see:DRONE-A08, 텔레메트리 파이프라인은 SYSD-M04). | mac |
| SYSD-M11 | 로봇 팔 설계: 관절 모듈과 실시간 버스 | 6축 협동 로봇 팔을 예로 관절 모듈(FOC 드라이브·듀얼 엔코더·브레이크·온도)의 펌웨어 구성, EtherCAT vs CAN-FD vs RS-485 비교(1 kHz 주기, Distributed Clock, 대역폭 계산), 중앙 제어기와의 분담, STO·충돌 감지·브레이크 해제 안전 체인을 설계하고 사이클 타이밍 다이어그램을 작성한다 (FOC는 see:RCTL-M09, CiA 402는 RCTL-A07, ros2_control은 ROS-M09). | mac |
| SYSD-M12 | 메모리 시스템 설계: SoC 메모리 계층 정하기 | 모바일 AP와 엣지 AI 카메라 SoC 두 경우에 대해 유스케이스별 대역폭/용량 요구를 집계하고 L1/L2 크기, 시스템 레벨 캐시 유무, TCM, DRAM 종류(LPDDR5X vs DDR5 vs HBM)·채널 수·인터리빙, QoS를 결정한 근거를 AMAT·대역폭 계산과 함께 설계 문서로 작성한다 (개별 계산 도구는 see:ARCH·MEM·SOCX-A07). | mac |
| SYSD-M13 | NAND 저장장치 설계: SSD · UFS 컨트롤러 | NVMe SSD와 UFS 4.0 장치에 대해 호스트 인터페이스(큐·커맨드 처리), 코어 수와 역할 분담(FE/FTL/BE), DRAM 유무(HMB), 매핑 테이블 크기 계산, 채널·웨이 병렬도와 성능 목표, 전원 차단 보호(PLP 커패시터 vs 저널링), 펌웨어 업데이트·SMART 설계를 비교한 설계 문서를 작성한다 (FTL 알고리즘은 see:MEM-M10~A09, 성능 모델은 SIM-M02, NVMe FW 커맨드는 PCIE-A05). | mac |
| SYSD-M14 | Wi-Fi / BT 콤보칩 펌웨어 설계 | Wi-Fi 6 + BT 5 콤보칩을 예로 FullMAC vs SoftMAC 분할, 호스트 인터페이스(SDIO/PCIe/UART HCI) 선택, 내부 코어 배치(MAC·BT 컨트롤러·공존 관리자), PTA 공존 정책, 절전(TWT·sniff), 펌웨어 다운로드·캘리브레이션 데이터, 크래시 덤프·리셋 복구 흐름을 설계하고 시퀀스 다이어그램을 작성한다 (공존 구현은 see:RF-A02, 리눅스 측은 WCONN-B02·A04). | mac |
| SYSD-M15 | 제조(MFG) 펌웨어 설계 | MFG 모드 진입/종료 조건, 테스트 명령 프로토콜(UART/USB, 버전·에러 코드), 캘리브레이션 데이터 저장 형식과 CRC, 시리얼/키 주입과 OTP 잠금, 테스트 스테이션과의 계약(타임아웃, 재시도), 출하 후 MFG 기능 비활성화와 RMA 재진입 경로를 설계하고 명령 프로토콜 파서를 구현한다 (스테이션 SW는 see:INFRA-A05, 프로비저닝 흐름은 PLAT-A04, RF 생산 테스트는 RF-A05). | mac |
| SYSD-M16 | 경량 테스트 펌웨어(Light-Weight Test Firmware) 설계 | OS 없는 테스트 이미지의 부트 최소 구성, 테스트 레지스트리(이름·태그·타임아웃), 호스트 명령 채널(UART 셸 vs 메일박스 vs JTAG 메모리), 결과·로그 포맷, 에뮬레이터/FPGA/실리콘/ATE 별 플랫폼 추상화, 이미지 크기 예산을 설계하고 테스트 디스패처 핵심 코드를 구현한다 (테스트 프레임워크·시퀀서는 see:SIV-B08·SIV-M12, Day-0 준비는 EMU-A07). | mac |
| SYSD-M17 | 이더넷 스위치 설계: 데이터 평면과 제어 평면 | 48×25G + 8×100G ToR 스위치를 예로 데이터 평면(파서·MAC/L3 테이블·ACL·버퍼·스케줄러) 자원 크기, 제어 평면(CPU·SDK/SAI·라우팅 데몬·LLDP/STP), 관리 평면을 나누고, 테이블 업데이트 make-before-break·warm boot·카운터 수집 경로를 설계하며 line-rate/버퍼 계산을 근거로 제시한다 (L2 구현은 see:NETHW-B10, P4는 NETHW-M07, SDK 프로그래밍은 NETHW-A09). | mac |
| SYSD-M18 | 시간 동기 아키텍처: 다중 센서 · 다중 컴퓨트 | 카메라·라이다·IMU·GNSS와 MCU·SoC 2대가 있는 로봇/차량에서 그랜드마스터 선택, PTP/gPTP 경로, 하드웨어 트리거·PPS, 타임스탬프 지점(노출 중앙 vs 프레임 시작), 허용 오차 예산(µs), 동기 상실 시 감지·성능 저하 정책을 설계하고 타이밍 다이어그램을 작성한다 (PTP 구현은 see:DIST-M01·M02, 센서 정렬은 SENS-M07·M08, CAM-M12). | mac |
| SYSD-M19 | 웨어러블 설계: 센서 허브 · Always-on · BLE | 스마트워치를 예로 센서 허브 MCU vs 메인 AP 역할 분담(걸음 수·심박·제스처를 어디서 처리할지), 센서 FIFO 배치 읽기, 디스플레이·BLE 동기화 주기, 하루 전력 예산(mAh), 충전·열 제약, OTA와 데이터 동기화 정책을 설계하고 상태별 전류 표와 웨이크업 흐름도를 작성한다. | mac |
| SYSD-M20 | 디버그 · 트레이스 · 필드 진단 인프라 설계 | SWD/JTAG 접근 정책(양산 후 잠금·인증 해제), 온칩 트레이스(ITM/ETM/ETB, RTT) 활용, 크래시 덤프 포맷과 보존 영역, 필드 로그 레벨·용량·업로드, RMA 시 수집 절차, 보안과의 충돌을 다루는 설계 문서를 작성한다 (트레이스 개념은 see:TOOL-A04, 덤프 포맷은 TOOL-A05·BM-A09, 수집 서버는 INFRA-M09). | mac |
| SYSD-A01 | 차량 OTA (Uptane) & 다중 ECU 오케스트레이션 | Director/Image 저장소 이중 서명(Uptane), primary/secondary ECU 역할, 다중 ECU 업데이트의 일관성(전부 또는 롤백), 주행 중 금지 조건을 설계하고 메타데이터 검증 핵심 로직을 구현한다. | mac |
| SYSD-A02 | HIL 테스트 팜 설계 | 보드 풀 예약·전원/리셋 제어·시리얼 로그 수집·플래싱·결과 저장·flaky 테스트 격리를 설계하고, 보드 예약 스케줄러와 테스트 실행 상태 머신을 구현한다. | mac |
| SYSD-A03 | AMP 혼합 시스템: 실시간 MCU + Linux | Cortex-M(모터 제어) + Cortex-A(Linux, 네트워크·UI) 구성에서 rpmsg 메시지 설계, 공유 메모리 소유권, Linux 크래시 시 안전 정지, 부팅 순서를 설계하고 공유 메모리 메시지 큐를 호스트에서 구현한다. | mac |
| SYSD-A04 | 엣지 AI 디바이스 플릿: 모델 배포·A/B·모니터링 | 모델 패키징·호환성(NPU 버전) 검사, 단계적 배포와 A/B 비교, 디바이스 측 추론 지표·드리프트 감지, 롤백을 설계하고 모델 매니페스트 호환성 검사기를 구현한다. | mac |
| SYSD-A05 | 실리콘 검증 데이터 파이프라인 | 테스트 스테이션→결과 수집 서버→회귀 비교·실패 분류(signature)→대시보드 흐름, 칩 리비전·보드·펌웨어 버전 메타데이터 스키마를 설계하고 실패 시그니처 클러스터링 핵심 로직을 구현한다. | mac |
| SYSD-A06 | 저대역 링크 원격 측정·명령 시스템 (store-and-forward) | 통신 윈도우 기반 store-and-forward, 명령 시퀀싱·인증·재전송, 우선순위 다운링크 큐, 시간 동기를 설계하고 윈도우 시뮬레이터 위에서 명령 전달 보장 로직을 구현한다. | mac |
| SYSD-A07 | 셀룰러 베이스밴드 펌웨어 설계 | LTE/5G NR 모뎀을 예로 L1(DSP/가속기, 슬롯 0.5 ms 타이밍), L2(MAC·RLC·PDCP, HARQ 프로세스), L3(RRC/NAS) 코어 배치, 하드 실시간 vs 소프트 실시간 경계, AP와의 인터페이스(공유 메모리·QMI/MBIM), DRX 절전, 크래시·로그(QXDM류) 경로를 설계하고 HARQ 타이밍 예산과 버퍼 크기를 계산한다 (호스트 측 AT/QMI는 see:RF-M08·WCONN-M11, OFDM 기초는 RF-A03). | mac |
| SYSD-A08 | CXL 시스템 설계: 메모리 확장과 풀링 | Type-3 메모리 확장 vs 스위치 기반 풀링 구성에서 용량·대역폭·추가 지연(~100+ ns) 계산, NUMA 노드 노출과 OS 티어링 정책(핫/콜드 페이지), 다중 호스트 할당·회수, 장애 도메인과 poison 처리, 관리(Fabric Manager)를 설계하고 워크로드별 이득/손해를 표로 정리한다 (프로토콜은 see:PCIE-A10, 패브릭 메커니즘은 SOCX-A06). | mac |
| SYSD-A09 | 데이터센터 AI 가속기 비교: GPU · TPU · LPU 등 | GPU(SIMT+HBM), TPU(대형 systolic+ICI), Groq LPU(SRAM·정적 스케줄), Cerebras(웨이퍼 스케일), Trainium/MTIA 등을 연산 구조·메모리 계층(SRAM/HBM 용량·대역폭)·스케일업/아웃 인터커넥트·프로그래밍 모델·학습 vs 추론 적합성으로 비교표를 만들고, 동일 LLM 추론 워크로드에 대한 roofline 추정으로 선택을 정당화한다 (스펙 분석은 see:AIPERF-A01, 인터커넥트는 DCN-A05). | mac |
| SYSD-A10 | SRAM 중심 LLM 추론칩 설계 | 70B 모델을 다이당 수백 MB SRAM 칩 여러 개로 서빙할 때 필요한 칩 수, 레이어/텐서 분할, 칩 간 링크 대역폭, 정적 컴파일 스케줄, KV 캐시 위치, 배치 1 지연 vs 처리량을 계산하고 HBM 기반 설계와 TCO·전력·유연성을 비교하는 설계 문서를 작성한다 (SRAM 타일링은 see:NPU-M01, KV 계산은 AIPERF-M04). | mac |
| SYSD-A11 | 자동차 충돌 시뮬레이션 시스템 설계 | 수백만 요소 차체 모델의 explicit 시간 적분 비용(CFL 시간 간격·스텝 수) 추정, 도메인 분할과 MPI 통신, CPU vs GPU 노드 선택, 체크포인트·재시작, 파라미터 스윕 작업 스케줄링, 결과 데이터(수 TB) 저장·후처리, 실차 시험 대비 검증 흐름을 설계하고 1회 해석 시간·클러스터 규모를 계산한다 (수치 기법은 see:NUM, 강체 충돌 기초는 PHYS, 시뮬레이션 일반은 SIM). | mac |
| SYSD-A12 | 차량 Zonal E/E 아키텍처 설계 | 중앙 HPC·4개 존 컨트롤러 구성에서 기능 배치, 이더넷 백본(TSN)과 CAN/LIN 잔존 구간, SOME/IP 서비스 경계, 전원 분배·전자 퓨즈, ASIL 분해에 따른 이중화, OTA 오케스트레이션, 하네스 길이·무게 이득을 설계하고 신호 경로 지연 예산을 계산한다 (프로토콜은 see:AUTO-M08·M09, OTA는 SYSD-A01). | mac |
| SYSD-A13 | FMCW 레이더 시스템 설계 | 77 GHz 차량 레이더를 예로 대역폭→거리 분해능, 처프 주기·개수→속도 분해능/최대 속도, 안테나 수(MIMO 가상 배열)→각 분해능을 계산하고, ADC→Range FFT→Doppler FFT→CFAR→DoA→트래킹 처리 체인의 연산량·메모리(레이더 큐브 크기)·지연 예산과 하드웨어 가속기 배치를 설계한다 (FFT는 see:DSP-M01, 융합은 AUTO-A07). | mac |
| SYSD-A14 | EV 트랙션 인버터 제어기 설계 | 800 V·200 kW 인버터를 예로 제어 MCU(lockstep) 선택, 전류 루프 주기(10~20 kHz)와 ADC·PWM 동기, 레졸버 인터페이스, 게이트 드라이버 고장 보고, 안전 상태(ASC vs 프리휠), 감시 MCU·워치독 체인, CAN/이더넷 토크 명령 경로를 설계하고 ASIL D 안전 메커니즘 배치표를 작성한다 (FOC는 see:RCTL-M09, 이중화는 TEST-A01, HARA는 AUTO-A01). | mac |
| SYSD-A15 | 엣지 AI SoC 아키텍처 정의: 제품 요구 → IP 구성 · PPA 예산 | 스마트 카메라/로봇용 엣지 AI SoC를 예로 목표 모델·프레임률에서 NPU TOPS·SRAM, CPU 클러스터, ISP·코덱, 메모리 인터페이스, 보안 블록, 저전력 도메인을 정하고 공정 노드별 면적·전력 추정과 유스케이스 대역폭 예산으로 스펙 시트와 블록도를 작성한다 (인터커넥트는 see:SOCX-A10, NPU 스펙은 NPU-B09, 메모리 맵은 ARCH-A12). | mac |
| SYSD-A16 | 모의 설계 면접 5회 & 루브릭 자기평가 | 앞 항목에서 다루지 않은 5개 프롬프트와 B09~A15 신규 주제 중 3개(예: 스마트 도어락, 배터리 팩 모니터, 산업 게이트웨이)를 45분씩 풀어 녹화하고, 요구사항·추정·구조·심화·트레이드오프·소통 6개 루브릭으로 채점해 개선점을 기록한다. | mac |

### CAP — 종합 프로젝트 & 면접 대비 (Capstone Projects & Interview Prep)

앞선 트랙들을 엮어 포트폴리오가 되는 종합 프로젝트를 완성하고, 코딩·디버깅·설계·행동 면접 라운드를 실전 형식으로 반복 연습한다. SSD/FTL·RF 통합 경력을 AI 가속기·로보틱스·실리콘 검증 직무 서사로 연결하는 것이 이 트랙의 최종 목표다.

| ID | 제목 | 과제 | env |
|---|---|---|---|
| CAP-B01 | C 코딩 드릴 1: 비트·문자열·포인터 | 비트 뒤집기/카운트, 문자열 파싱, 포인터 산술 문제 4개 세트를 문제당 20분 타이머로 풀고, 직접 작성한 테스트 하네스(경계값 포함)로 채점해 시간·실수 유형을 기록한다. | mac |
| CAP-B02 | C 코딩 드릴 2: 링 버퍼·리스트·고정 블록 할당자 | 링 버퍼, 단일/이중 연결 리스트 조작, 고정 블록 메모리 할당자를 종이(또는 컴파일러 없는 에디터)에 먼저 작성한 뒤 컴파일·ASan·테스트로 검증하고, 손코딩 단계에서 놓친 버그를 분류한다. | mac |
| CAP-B03 | C 코딩 드릴 3: 드라이버형 문제 | mock 레지스터 맵 드라이버(폴링/인터럽트), UART 프레임 파서 상태 머신, 디바운스·이벤트 큐 문제 4개를 각 30분 안에 구현하고 제공된 테스트로 채점한다. | mac |
| CAP-B04 | Think-aloud 라이브 코딩 연습 | 중간 난이도 문제 5개를 화면+음성 녹화하며 '명확화 질문→접근 설명→구현→테스트' 순서로 풀고, 녹화를 다시 보며 침묵 구간·설명 누락·테스트 누락을 체크리스트로 평가한다. | mac |
| CAP-B05 | 디버깅 라운드 1: 버그 심어진 코드 찾기 | UB·레이스·off-by-one·volatile 누락·정수 승격·엔디언·ISR 공유 변수 버그가 심어진 코드 10개를 각 10분 안에 찾아 설명하고, sanitizer/테스트로 재현·수정을 검증한다. | mac |
| CAP-B06 | 행동 면접 STAR 스토리 뱅크 | 갈등, 실패, 리더십, 모호함, 촉박한 일정, 고객 이슈, 기술적 의사결정, 학습 8개 주제에 대해 SSD·RF 통합 경력 기반 STAR 스토리(각 2분, 수치 포함)를 작성하고 질문-스토리 매핑표를 만든다. | mac |
| CAP-B07 | 이력서 프로젝트 딥다이브 대비 | SSD FTL/NVMe 펌웨어와 RF 통합 프로젝트 각각에 대해 '무엇을→어떻게→왜→대안→다시 한다면'의 5단계 예상 질문과 답, 아키텍처 화이트보드 그림을 준비하고 모의 질의응답을 녹음한다. | mac |
| CAP-B08 | 자기소개 & 지원 동기 피치 | 'Tell me about yourself' 1분/3분 버전과 AI 가속기·로보틱스·실리콘 검증 직무별 지원 동기를 작성하고, 녹음 후 시간·핵심 메시지·전환 논리 일관성을 점검한다. | mac |
| CAP-M01 | 디버깅 라운드 2: 크래시 덤프·로그 분석 | Cortex-M HardFault 레지스터 덤프(CFSR/HFSR, 스택 프레임), Linux core dump(lldb/gdb), 타이밍 로그 3종 시나리오를 분석해 근본 원인·재현 방법·수정·재발 방지책을 15분 내 설명하는 연습을 한다. | mac |
| CAP-M02 | C++ 코딩 라운드 드릴 | unique_ptr·shared_ptr 축약 구현, LRU 캐시, 스레드 안전 큐, 타입 소거 콜백 등 MCPP·LLAT 연계 문제 6개를 각 30분 타이머로 풀고 TSan/ASan 포함 테스트로 채점한다. | mac |
| CAP-M03 | 하드웨어/아키텍처 퀵파이어 Q&A | 캐시·DMA·인터럽트·버스·메모리 맵·전력·PCIe·통신 프로토콜 100문항 플래시카드를 만들어 간격 반복으로 연습하고, 틀린 문항을 해당 트랙 항목과 연결해 복습 목록을 만든다. | mac |
| CAP-M04 | 임베디드 시스템 설계 모의 면접 | SYSD에서 다룬 문제 3개를 다른 사람(또는 AI 면접관 역할극)과 45분씩 진행해 녹화하고, 루브릭 점수·면접관 질문에 대한 대응을 회고 문서로 정리한다. | mac |
| CAP-M05 | 종합 프로젝트 1: 미니 FTL | NAND 에뮬레이터(페이지 쓰기/블록 소거, 비트 오류) 위에 페이지 매핑 FTL·greedy/cost-benefit GC·웨어 레벨링·전원 차단 복구(OOB 메타데이터 스캔)를 구현하고, WAF·마모 분포·크래시 테스트 결과를 README에 정리한다. | mac |
| CAP-M06 | 종합 프로젝트 2: RTOS 센서 노드 (호스트 시뮬레이션) | FreeRTOS POSIX 포트 위에 모의 I2C IMU 드라이버, 센서 태스크→필터 태스크→CAN 프레임 송신 태스크, 링 버퍼 로거, 워치독 감시를 구현하고 태스크 지연·스택 사용량·큐 오버플로를 계측한다. | mac |
| CAP-M07 | 종합 프로젝트 3: 센서 노드에 A/B OTA 추가 | 프로젝트 2에 서명된 이미지(Ed25519)·A/B 슬롯·trial boot·롤백을 갖는 OTA를 CAN/UART 전송 경로로 추가하고, 전송 중단·서명 불일치·부팅 실패 시나리오 테스트를 자동화한다. | mac |
| CAP-M08 | 포트폴리오 정리 & 면접 서사 연결 | 각 종합 프로젝트의 README(문제·아키텍처 다이어그램·핵심 결정·측정 결과·한계), 2분 데모 영상, 이력서 bullet을 작성하고 STAR 스토리와 연결되는 '프로젝트 발표 5분' 원고를 만든다. | mac |
| CAP-A01 | 종합 프로젝트 4: 센서 노드 실보드 이식 | 프로젝트 2·3을 STM32/nRF 보드 + 실제 IMU + CAN 트랜시버로 이식하고, 로직 애널라이저로 I2C/CAN 타이밍을 측정해 시뮬레이션 가정과의 차이를 기록한다. | **board** |
| CAP-A02 | 종합 프로젝트 5: 실제 모델을 NPU 스택으로 End-to-End 실행 | NPU 트랙의 systolic 시뮬레이터+드라이버 스택(NPU-A12)에 EML 트랙의 int8 이미지 분류기를 올려, 그래프 파티셔닝(NPU/CPU fallback)→타일 스케줄→커맨드 큐 제출→결과 bit-exact 비교까지 자동화하고, 레이어별 이용률·DRAM 트래픽·병목 분석과 개선 1건을 포함한 기술 리포트와 데모를 만든다. | mac |
| CAP-A03 | 종합 프로젝트 6: 비행 제어기 SITL | 쿼드콥터 동역학 + IMU/기압계 노이즈 모델, EKF(또는 상보 필터) 자세 추정, 캐스케이드 PID(자세/각속도), MAVLink 텔레메트리를 통합한 SITL을 구현해 호버링·스텝 응답·센서 고장 주입 시나리오를 검증한다. | mac |
| CAP-A04 | 종합 프로젝트 7: 실리콘 검증 프레임워크 | mock 칩(레지스터 맵, PCIe LTSSM 상태, 인터럽트, 에러 카운터)에 대한 레지스터 R/W·리셋값 테스트, 링크 상태 모니터, 스트레스/에러 주입 테스트, 회귀 결과 리포트(HTML)를 갖춘 C/Python 프레임워크를 구현한다. | mac |
| CAP-A05 | 종합 프로젝트 8: 엣지 카메라 → 검출 → ROS 2 퍼블리시 | 카메라(또는 녹화 영상) 캡처→전처리(CV)→양자화 모델 추론(EML)→검출 결과 ROS 2 토픽 발행 파이프라인을 구성하고, 단계별 지연·프레임 드롭·CPU 사용률을 측정해 병목을 개선한다. | **linux** |
| CAP-A06 | 풀 루프 모의 온사이트 & 회고 | 코딩 2 + 디버깅 1 + 시스템 설계 1 + 행동 1 라운드를 하루에 연속 진행(각 45~60분)하고, 라운드별 점수·피로도·실수 패턴을 회고해 다음 2주 보완 계획을 세운다. | mac |
| CAP-A07 | 회사별 맞춤 준비 패킷 | JD→요구 역량 매핑→예상 기술/행동 질문 20개→보유 스토리·프로젝트 매칭→갭 보완 플랜으로 이어지는 템플릿을 만들고 실제 지원 중인 2개 회사에 적용한다. | mac |
