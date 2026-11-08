# 외부환경 세팅 가이드 (Mac · Linux · QEMU · HDL · 보드 · GPU · 계측)

커리큘럼의 각 아이템에는 `env` 값이 붙어 있다. 이 문서는 **env별로 이 맥(Apple Silicon, macOS 27.2)에서 무엇을 어떻게 설치·확인하는지**를 한 곳에 모은 것이다. 원본 기록은 `curriculum/envs.json`(문제 사이트의 "실습환경" 탭)이고, 여기서는 순서·이유·검증 명령을 붙여 실제로 따라 할 수 있게 풀었다.

> **쓰는 법**
> - 아이템의 env를 보고 해당 절만 세팅한다. 전부 할 필요 없다.
> - 권장 순서는 **1. mac → 3. QEMU → 4. HDL → 2. Linux VM**이다. 아이템 수가 많고 비용이 싼 순서다.
> - 각 절 끝의 **확인 명령**이 통과하면 그 env는 준비된 것이다. 마지막 절의 스모크 테스트로 한 번에 점검할 수 있다.
> - 설치 명령은 직접 터미널에서 실행한다 (`! brew install ...` 형태로 이 세션에서도 가능).

## 한눈에: env별 현황

현재 상태는 2026-11-08 이 맥에서 직접 확인한 값이다.

| env | 아이템 수 | 필요한 것 | 현재 상태 | 우선순위 |
|---|---|---|---|---|
| `mac` | 1940 | clang · python3 (+ 트랙별 pip/brew) | ✅ clang 있음, 단 **SDKROOT 지정 필요**. Python 3.9.6 · numpy 1.19라 오래됨 | 지금 |
| `linux` | 133 | Lima VM `elinux` | ⚠️ VM 있음, **Stopped**. 아이템별 패키지는 VM 안에서 필요할 때 설치 | 필요 시 |
| `qemu` | 69 | qemu · arm-none-eabi-gcc · riscv 툴체인 | ❌ 맥에 미설치 (VM 안 QEMU 랩만 있음) | 높음 |
| `fpga` | 41 | verilator · iverilog · yosys · cocotb | ❌ 미설치. **보드는 필요 없음** | 높음 |
| `board` | 2 | Nucleo 보드 + ST-Link | ⛔ 보드 없음 (선택) | 나중 |
| `gpu` | 3 | NVIDIA GPU | ⛔ 클라우드 대여 (선택) | 나중 |
| `lab` | 3 | RTL-SDR · 로직 애널라이저 · 오실로스코프 | ⛔ 장비 없음 (선택) | 나중 |

트랙별 분포 (env가 `mac`이 아닌 것):

| env | 트랙 (아이템 수) |
|---|---|
| `linux` | ELX 26 · LPERF 27 · WCONN 14 · ROS 13 · NET 12 · VIRT 9 · OS 6 · DCN 5 · LLAT 5 · BMC 3 · DISP 2 · CAM 2 · MEDIA 2 · INFRA/COMM/PCIE/GPUSYS/CAP/NETHW 각 1~2 |
| `qemu` | RISCV 14 · ARM 11 · BM 11 · ELX 9 · RUST 4 · PCIE 3 · PLAT 3 · EMU 3 · RTOS 2 · OS 2 · TOOL 2 · SEC 2 · DSP/EML/BMC 각 1 |
| `fpga` | RTLDV 35 · EMU 5 · NPU 1 |
| `gpu` | GPGPU-A01 · GPUSYS-A06 · DCN-A04 |
| `lab` | RF-A06 · HWLAB-A06 · HWLAB-A07 |
| `board` | BM-A10 · CAP-A01 |

## 1. `mac` — 맥 로컬에서 코드 돌리기

아이템의 95%가 여기서 끝난다. 호스트 맥에서 mock 레지스터, 소프트웨어 모델, 시뮬레이션으로 푼다.

### 1-1. 기본 C/C++ 빌드

macOS 27.2의 SDK 링크가 깨져 있어 기본 상태로는 링크가 실패한다. **SDKROOT를 26.4로 고정**한다. `~/.zshrc`에 한 번만 넣는다.

```sh
echo 'export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk' >> ~/.zshrc
source ~/.zshrc
```

빌드·실행·새니타이저:

```sh
clang -std=c11 -Wall -Wextra -O2 -g -fsanitize=address,undefined x.c -o x && ./x
clang++ -std=c++20 -Wall -Wextra -O2 -g x.cpp -o x && ./x
lldb ./x          # 디버거
```

**확인:** `echo 'int main(){return 0;}' | clang -x c - -o /tmp/t && /tmp/t && echo OK`

### 1-2. Python 환경

현재 `python3`가 3.9.6, numpy가 1.19라 최신 라이브러리와 맞지 않는다. 트랙별 패키지는 **venv에 깔아서** 시스템 Python을 건드리지 않는다.

```sh
brew install python@3.12
python3.12 -m venv ~/.venvs/study && source ~/.venvs/study/bin/activate
python -m pip install -U pip numpy scipy mpmath matplotlib
```

필요한 트랙에서만 추가한다.

| 트랙 | 설치 |
|---|---|
| NUM · QPROB · QFIN | `pip install numpy scipy mpmath` |
| AISYS (torch) | `pip install torch` — CPU 또는 MPS, 분산은 gloo 백엔드 |
| SIV · RTLDV 레지스터 생성 | `pip install peakrdl` (HDL 절 참고) |

> 이후 모든 Python 아이템은 `source ~/.venvs/study/bin/activate` 후 실행한다고 가정하면 된다.

### 1-3. 트랙별 추가 도구 (필요할 때만)

| 트랙 | 명령 | 용도 |
|---|---|---|
| RUST | `curl https://sh.rustup.rs -sSf \| sh` 후 `rustup target add thumbv7em-none-eabihf thumbv7m-none-eabi` · `cargo install cargo-binutils` | no_std 크로스 빌드 |
| MEDIA | `brew install ffmpeg` | 비트스트림 파서의 정답 오라클 |
| HWLAB | `brew install sigrok-cli` | LA 캡처(.sr) 변환·디코드 |
| HPC (OpenMP/MPI) | `brew install libomp open-mpi` | 아래 빌드 예시 |

OpenMP / MPI 빌드:

```sh
clang -Xpreprocessor -fopenmp -I$(brew --prefix libomp)/include \
      -L$(brew --prefix libomp)/lib -lomp x.c -o x
mpicc x.c -o x && mpirun -np 4 ./x
```

### 1-4. 맥 로컬의 알려진 제약

- **valgrind 없음** → ASan/UBSan, `leaks`, `MallocStackLogging`으로 대체. 정말 필요하면 Lima VM.
- **Hypervisor.framework** (VIRT 미니 VMM): `com.apple.security.hypervisor` entitlement가 필요하다.
  ```sh
  codesign --sign - --entitlements hv.plist --force ./vmm
  ```
  서명을 안 하면 `hv_vm_create`가 `HV_DENIED`를 반환한다.
- **utun / raw 소켓** (NET 일부): `sudo` 필요. 맥에는 TAP 장치가 없어 L2/ARP는 시뮬레이션 링크로 한다.
- **정확한 타이밍·PMU·실제 하드웨어 레지스터**는 맥에서 의미가 없다 → mock 또는 해당 env 사용.

## 2. `linux` — Lima VM `elinux`

커널 모듈, `/sys`, epoll, perf, 네트워크 스택, 디바이스 트리 등 **Linux 전용 API**가 필요한 133개 아이템용. EmbeddedLinux 스터디 때 만든 VM이 이미 있다.

- 사양: vz, aarch64, 6 CPU / 12GiB / 100GiB, Ubuntu
- 현재 `Stopped`. 안에 커널 이미지(Image, -dbg, -kasan, -rt 등), QEMU 랩 `/var/lab`, RTOS 랩 `/var/rtoslab`이 있다.

```sh
limactl start elinux          # 시작
limactl shell elinux          # 접속
limactl stop elinux           # 끝나면 정지 (맥 RAM 12GiB 반환)
```

**확인:** `limactl shell elinux -- uname -a`

### 2-1. 기본 패키지 (VM 안)

```sh
sudo apt update
sudo apt install -y build-essential gdb strace ltrace linux-headers-$(uname -r) \
    can-utils v4l-utils libdrm-dev ipmitool
```

### 2-2. 트랙별 추가 (해당 트랙을 할 때만)

| 트랙 | VM 안에서 |
|---|---|
| NET · LPERF | `sudo apt install -y iproute2 nftables ethtool tcpdump bpftrace bpfcc-tools libbpf-dev clang llvm linux-tools-$(uname -r)` |
| WCONN | `sudo apt install -y iw wpasupplicant hostapd bluez linux-modules-extra-$(uname -r)` 후 `sudo modprobe mac80211_hwsim radios=2` (BlueZ btvirt · wmediumd는 소스 빌드) |
| DCN RDMA | `sudo apt install -y rdma-core ibverbs-utils perftest` 후 `sudo rdma link add rxe0 type rxe netdev eth0` (Soft-RoCE) |
| ROS | ROS 2 공식 저장소 추가 후 `ros-jazzy-desktop` (Ubuntu 24.04) |
| NETHW P4 | p4c + bmv2(`simple_switch`) — 패키지 또는 소스 |
| MEDIA | `sudo modprobe snd-aloop vicodec` |
| 가상 장치 모듈 | `sudo modprobe vcan vivid vimc vkms vgem i2c-stub gpio-sim` |

### 2-3. VM의 한계 (아이템 풀 때 먼저 확인)

- **`/dev/kvm` 없음** → VIRT의 KVM 항목(M09, A03~A05)은 VM 안 QEMU TCG (`-M virt,virtualization=on`)로 대체하거나 arm64 베어메탈/클라우드 호스트에서.
- **하드웨어 PMU 없음** → LPERF의 PMU 항목은 software event로 대체. 정확한 카운터가 목적이면 실머신.
- **지연·지터 측정**은 하이퍼바이저 노이즈가 섞인다 → 절대값 말고 **경향 비교**로 기록.
- `rdma_rxe` 성능 수치는 상대 비교용.
- 모듈이 없으면 (예: `CONFIG_GPIO_SIM`) 커널 config를 확인하고 모듈만 빌드.
- Yocto / Buildroot 풀 이미지 빌드는 **guide-only** (2026-10-17 결정).

## 3. `qemu` — QEMU + 크로스 툴체인 (맥 호스트)

Cortex-M 베어메탈, RISC-V, 부트 코드, 펌웨어처럼 **실제 CPU 코어 위에서 돌려야 하는** 69개 아이템용. 지금 맥에는 아무것도 없다.

```sh
brew install qemu
brew install --cask gcc-arm-embedded        # arm-none-eabi-gcc / gdb
brew tap riscv-software-src/riscv && brew install riscv-tools   # riscv64-unknown-elf-gcc, spike
```

> 대안: `arm-none-eabi-gcc` 대신 `clang --target=thumbv7m-none-eabi` + `ld.lld`도 된다.

**확인:** `qemu-system-arm --version && arm-none-eabi-gcc --version | head -1`

### 3-1. 머신 선택

| 용도 | 머신 | 실행 |
|---|---|---|
| Cortex-M3 | `mps2-an385` | `qemu-system-arm -M mps2-an385 -nographic -semihosting -kernel app.elf` |
| Cortex-M4 (FPU) | `mps2-an386` | 위와 같고 머신만 교체 |
| Rust 튜토리얼 표준 | `lm3s6965evb` | `qemu-system-arm -M lm3s6965evb -nographic -semihosting-config enable=on,target=native -kernel target/thumbv7m-none-eabi/debug/app` |
| RISC-V | `virt` | `qemu-system-riscv64 -M virt -bios none -nographic -kernel x.elf` |
| Cortex-A (ELX/PCIE/PLAT) | `virt` | **VM 안 `/var/lab` 랩을 재사용** |

### 3-2. gdb로 붙이기

```sh
qemu-system-arm -M mps2-an386 -nographic -kernel app.elf -s -S    # -s: :1234 gdbstub, -S: 멈춘 채 시작
arm-none-eabi-gdb app.elf -ex 'target remote :1234'
```

### 3-3. 트랙별 메모

- **RTOS-A07 (Zephyr):** `pip install west` + Zephyr SDK → `west build -b qemu_cortex_m3 -t run`
- **RISCV / OS-A09·A10 (xv6) / EMU-M04:** 위 riscv-tools 필요
- **EMU-M05·M06 (QEMU 소스 빌드):** 맥에서 가능, 수십 분
- **OP-TEE / TF-A / OVMF:** 무거운 빌드 → Lima VM 안에서, 오래 걸리면 guide-only

### 3-4. 한계

QEMU는 **기능 에뮬레이션**이다. 사이클 수, 정확한 타이밍, 폴트 레지스터 세부값은 실칩과 다르다. 정확한 사이클이 필요한 항목(BM-A10)이 `board`로 분리된 이유다.

## 4. `fpga` — HDL 시뮬레이터 (보드 불필요)

이름은 `fpga`지만 **FPGA 보드는 필요 없다.** RTLDV(35) · EMU(5) · NPU(1) 모두 맥의 시뮬레이터로 진행한다.

```sh
brew install verilator icarus-verilog yosys
brew install gtkwave                      # 파형 보기. 안 되면 surfer
source ~/.venvs/study/bin/activate
pip install cocotb cocotb-coverage pyuvm pyvsc peakrdl
```

형식검증 · nextpnr · gate-level 시뮬 (RTLDV M09, A04~A07)은 **YosysHQ oss-cad-suite** 릴리스 tarball을 받아 PATH에 추가한다 (sby, smtbmc, eqy 포함).

**확인:** `verilator --version && iverilog -V | head -1 && yosys -V`

최소 동작 예:

```sh
verilator --cc --exe --build -Wall top.sv tb.cpp && ./obj_dir/Vtop
```

메모:
- 오픈소스 도구는 **SystemVerilog UVM / covergroup을 실행하지 못한다.** RTLDV는 `pyuvm` · `cocotb-coverage`로 같은 구조를 익히는 방식이다.
- EMU-M04는 `riscv64-unknown-elf` 툴체인도 필요하다 (3절과 공유).

## 5. `board` · `gpu` · `lab` — 선택 (나중에)

해당 아이템은 합쳐 8개뿐이고, **없어도 나머지 진도에는 지장이 없다.** 마지막에 몰아서 한다.

### board (2개)

| 아이템 | 준비물 |
|---|---|
| BM-A10 (사이클 측정) | STM32 Nucleo-F446RE 또는 -H743ZI (ST-Link 내장) |
| CAP-A01 | Nucleo 또는 nRF52 DK + IMU 브레이크아웃 + CAN 트랜시버(SN65HVD230) |

```sh
brew install openocd stlink
openocd -f board/st_nucleo_f4.cfg
st-info --probe        # 확인
```

QEMU로 못 하는 것: 정확한 사이클(DWT CYCCNT), 실제 버스 신호, 전력.

### gpu (3개: GPGPU-A01 · GPUSYS-A06 · DCN-A04)

Apple GPU는 CUDA를 못 쓴다. 클라우드 GPU(L4/T4/A10G 등)를 **시간 단위로 대여**한다. 세 항목을 **한 번에 몰아서** 한다.

```sh
nvidia-smi && nvcc --version     # 인스턴스에서 확인
ncu ...                          # Nsight Compute 프로파일
```

DCN-A04는 4~8 GPU 인스턴스에서 `nccl-tests`(`all_reduce_perf`)로 토폴로지별 busbw를 비교한다. **끝나면 인스턴스를 반드시 중지/삭제** (과금).

### lab (3개)

| 아이템 | 준비물 |
|---|---|
| RF-A06 | RTL-SDR v4 (또는 HackRF One) + 안테나 |
| HWLAB-A06 | fx2 기반 저가 LA(sigrok 호환) + Raspberry Pi Pico 신호원 |
| HWLAB-A07 | 엔트리급 DSO (Rigol DHO804 / Siglent SDS1104X-E) + 벅 컨버터 평가보드 + PyVISA |

```sh
brew install librtlsdr && rtl_sdr -f 433.92e6 -s 2.048e6 cap.bin     # 확인: rtl_test -t
```

**캡처만 장비가 필요하고 분석은 맥 Python**이다. 공개 IQ 샘플 파일로 먼저 연습할 수 있다. HWLAB 나머지 28개는 합성 캡처(CSV/VCD/.sr)로 맥에서 이미 분석 가능하다.

## 6. 한 번에 점검하는 스모크 테스트

```sh
echo "== mac ==";  echo 'int main(){return 0;}' | clang -x c - -o /tmp/t && /tmp/t && echo OK
echo "== py ==";   python3 -c "import numpy, sys; print(sys.version.split()[0], numpy.__version__)"
echo "== qemu =="; qemu-system-arm --version | head -1; arm-none-eabi-gcc --version | head -1
echo "== hdl ==";  verilator --version; iverilog -V 2>&1 | head -1; yosys -V
echo "== vm ==";   limactl list
```

`command not found`가 뜨는 줄이 아직 안 깐 env다.

## 7. 권장 세팅 순서

1. **SDKROOT 고정 + Python venv** (1절) — 10분. 이것만으로 1940개 아이템이 풀린다.
2. **QEMU + 크로스 툴체인** (3절) — `brew` 3줄. BM · ARM · RISCV · RUST 40개가 열린다.
3. **HDL 시뮬레이터** (4절) — `brew` + `pip`. RTLDV 35개 등이 열린다.
4. **Lima VM** (2절) — 시작만 하면 된다. 필요한 트랙의 패키지만 그때그때 설치.
5. board · gpu · lab (5절)은 위가 끝난 뒤 필요할 때.

## 8. 자주 막히는 곳

| 증상 | 원인 / 해결 |
|---|---|
| `clang`이 `library not found for -lSystem` | `SDKROOT`가 안 잡힘 → 1-1절 |
| `pip install`이 `externally-managed-environment` | venv 밖에서 설치 시도 → venv 활성화 |
| `hv_vm_create` = `HV_DENIED` | entitlement 서명 누락 → 1-4절 `codesign` |
| `limactl shell`이 멈춤 | VM이 Stopped → `limactl start elinux` |
| VM 안 `modprobe` 실패 | 해당 커널 모듈 미빌드 → 커널 config 확인 후 모듈 빌드 |
| QEMU 실행 후 출력이 없음 | `-nographic`와 `-semihosting` 누락, 또는 링커 스크립트의 메모리 주소가 머신과 불일치 |
| `verilator` 경고로 빌드 중단 | `-Wall`은 경고도 에러로 취급하는 경우가 있음 → 우선 `-Wno-fatal`로 돌려보고 경고를 하나씩 정리 |

## 참고

- 원본 기록: `curriculum/envs.json` (문제 사이트 "실습환경" 탭에 렌더링)
- 환경별 아이템 목록: `2026-11-07_curriculum_items_overview.md`
- 같은 맥에 SDK 27.2 링크가 깨져 있는 문제는 Apple 쪽 이슈라, 새 SDK가 고쳐질 때까지 26.4 고정을 유지한다.
