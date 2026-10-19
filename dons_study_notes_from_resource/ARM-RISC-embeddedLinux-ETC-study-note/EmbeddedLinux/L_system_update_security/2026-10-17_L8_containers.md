# L8. 컨테이너 on embedded — namespace · cgroup v2 · overlayfs · Podman을 장비 위에서 직접 뜯어 보기

> **이 노트를 다 읽으면**: `unshare`와 50줄짜리 C 프로그램(namespace + overlayfs + pivot_root + cgroup)으로 "컨테이너"를 직접 만들어 컨테이너가 VM이 아니라 **꼬리표 붙은 프로세스**라는 것을 보여 줄 수 있다 · cgroup v2의 `memory.max`(OOM)·`memory.high`·`cpu.max`(throttling)·`pids.max`를 손으로 걸고 `memory.events`·`cpu.stat`·`pids.events`로 결과를 읽을 수 있다 · Podman(crun·conmon·netavark)으로 arm64 이미지를 오프라인 import → 실행 → `--memory/--cpus/--pids-limit`이 어느 cgroup 파일이 되는지, rootless가 user namespace로 어떻게 돌아가는지, Quadlet으로 부팅 때 자동 시작·재시작하는 법을 직접 확인할 수 있다 · "로봇 소프트웨어를 컨테이너로 배포할까?", "컨테이너 안 RT 스레드가 deadline을 놓친다", "Jetson에서 GPU가 컨테이너로 어떻게 들어가나" 같은 설계 질문에 측정 근거로 답할 수 있다
> **왜 필요한가**: AI 가속기·로보틱스 회사(Jetson Orin 기반 제품이 대표적)는 인퍼런스·인식 스택을 컨테이너로 배포하는 경우가 많다. 면접에서는 "컨테이너를 쓰면 무엇을 얻고 무엇을 잃나", "OTA는 A/B로 할까 컨테이너로 할까", "cgroup으로 서비스 자원을 어떻게 나누나"가 나오고, 입사 후에는 장비 위에서 `podman`/`docker`·systemd unit·`/dev` 노드 권한을 바로 만지게 된다
> **Don 기준 난이도**: 자원 예산(메모리 풀 한도, CPU 시간 할당), MPU로 태스크 격리, 펌웨어 이미지 슬롯 교체는 이미 안다 / Linux가 그 격리를 "namespace(무엇이 보이나) + cgroup(얼마나 쓰나) + 계층형 파일시스템(어떤 파일이 보이나) + capability·seccomp(무엇을 할 수 있나)"로 쪼개 두었다는 점, 그리고 그 위의 도구층(OCI, runc/crun, Podman, containerd, Docker)이 새로 배울 부분
> **선행 노트**: A2(프로세스·fork/clone), A7(IPC), F1(스케줄러 — cpu.weight·RT), F11 §10(memcg OOM), J3 §7(overlayfs), K6(Ubuntu Base·L4T), L1(systemd) — L4(OTA)·L6(capabilities·seccomp·LSM)와 서로 참조한다

---

## 0. 큰 그림 — 이게 왜 필요한가

SSD 펌웨어에서 여러 태스크를 한 칩에 올릴 때 두 가지를 했다. 첫째, **격리**: MPU 영역을 태스크마다 다르게 잡아서 한 태스크가 남의 메모리를 못 보게 했다. 둘째, **예산**: 버퍼 풀을 태스크별로 나눠 쓰게 하고, 백그라운드 GC가 호스트 I/O의 시간을 다 먹지 않도록 시간 할당을 걸었다.

Linux 컨테이너는 바로 이 두 가지를 **프로세스 묶음 단위로** 해 주는 커널 기능의 조합이다. 핵심 문장 하나를 먼저 외워 두자.

> **컨테이너는 VM이 아니다. 컨테이너는 "커널이 꼬리표를 붙여 둔 평범한 프로세스"다.** 게스트 커널도, 하이퍼바이저도 없다. 호스트 커널 하나를 모두가 같이 쓴다.

꼬리표는 네 종류다.

- **namespace** — 무엇이 보이나. PID 번호표, 마운트 트리, 네트워크 인터페이스, hostname, IPC 객체, UID 매핑, cgroup 루트, 시계 오프셋을 프로세스 묶음마다 따로 보이게 한다.
- **cgroup v2** — 얼마나 쓰나. CPU 시간(`cpu.max`, `cpu.weight`), 메모리(`memory.max`, `memory.high`), 프로세스 수(`pids.max`), 블록 I/O(`io.max`), 쓸 수 있는 CPU 코어(`cpuset.cpus`)를 묶음 단위로 제한·회계한다.
- **rootfs** — 어떤 파일이 보이나. OCI 이미지의 읽기전용 층(layer)들을 overlayfs로 겹치고, `pivot_root`로 그 디렉터리를 `/`로 만든다.
- **권한 축소** — 무엇을 할 수 있나. capability 일부 제거, seccomp 필터, LSM(AppArmor/SELinux) 프로파일, user namespace로 "컨테이너 안 root ≠ 호스트 root".

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="14" width="320" height="62" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="38" text-anchor="middle" font-size="13" font-weight="bold">namespaces — 무엇이 보이나</text>
  <text x="170" y="60" text-anchor="middle" font-size="12">pid mnt net uts ipc user cgroup time</text>
  <rect x="350" y="14" width="320" height="62" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="38" text-anchor="middle" font-size="13" font-weight="bold">cgroup v2 — 얼마나 쓰나</text>
  <text x="510" y="60" text-anchor="middle" font-size="12">cpu.max · memory.max · pids.max · io.max</text>
  <rect x="200" y="122" width="280" height="64" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="148" text-anchor="middle" font-size="14" font-weight="bold">컨테이너 = 평범한 프로세스</text>
  <text x="340" y="170" text-anchor="middle" font-size="12">task_struct + 꼬리표 4종</text>
  <rect x="10" y="232" width="320" height="62" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="256" text-anchor="middle" font-size="13" font-weight="bold">rootfs — 어떤 파일이 보이나</text>
  <text x="170" y="278" text-anchor="middle" font-size="12">image layers → overlayfs + pivot_root</text>
  <rect x="350" y="232" width="320" height="62" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="256" text-anchor="middle" font-size="13" font-weight="bold">권한 축소 — 무엇을 할 수 있나</text>
  <text x="510" y="278" text-anchor="middle" font-size="12">capabilities · seccomp · LSM · userns</text>
  <line x1="170" y1="76" x2="250" y2="120" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="76" x2="430" y2="120" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="170" y1="232" x2="250" y2="188" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="510" y1="232" x2="430" y2="188" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="340" y="322" text-anchor="middle" font-size="13">게스트 커널 없음 — 호스트 Linux 커널 하나를 모두가 공유한다</text>
</svg>
```
그림 1 — 컨테이너를 이루는 네 가지 커널 기능. 가운데 프로세스는 호스트에서 `ps`로 그대로 보이는 보통 프로세스다

### 0.1 Embedded 장비에서 컨테이너가 왜 등장했나

예를 들어 Jetson Orin 기반 자율 로봇이라면 한 보드에 이런 것들이 같이 돈다.

- 카메라·LiDAR 인식(CUDA·TensorRT·cuDNN, 수 GB의 라이브러리)
- ROS 2 미들웨어와 플래너
- 모터 제어 브리지(CAN), 안전 감시 데몬
- 로그 업로더, 원격 관리 에이전트, 웹 UI

여기서 문제가 세 가지 생긴다. (1) **의존성 지옥**: 인식팀은 TensorRT 10 + Python 3.12, 플래너팀은 다른 ROS 배포판을 원한다. (2) **배포 속도**: 인식 모델과 앱은 매주 바뀌는데 OS 전체 이미지를 매주 A/B로 굽기는 무겁다. (3) **자원 싸움**: 업로더가 메모리를 새면 OOM killer가 인식 프로세스를 쏠 수 있다.

컨테이너는 (1) 앱마다 자기 userspace를 들고 다니게 하고, (2) 앱 층만 따로 바꿔 끼우게 하며, (3) cgroup으로 앱마다 예산을 건다. 대가도 분명하다 — flash 용량(이미지 층), 시작 시간, RT 지연 위험, 장치 접근의 복잡도, 보안 설정 실수. 이 노트는 그 득실을 **QEMU 게스트에서 직접 재서** 판단할 수 있게 하는 것이 목표다.

### 0.2 이 노트의 실험 환경

- 실행 환경: Mac 위 Lima VM `elinux`(Ubuntu 26.04, 커널 `7.0.0-38-generic`, aarch64)에서 QEMU TCG로 게스트를 띄운다.
- 게스트: 랩 커널 `Image-sec`(6.18.55-sec — 모든 namespace, cgroup v2 전 컨트롤러, overlayfs, veth/bridge, nftables) + `rootfs-sysd.ext4`(Ubuntu Base 24.04.5 arm64 + systemd 255 + podman·crun·conmon·netavark). `-smp 4 -m 2G`.
- 실험 스크립트와 정적 링크 C 바이너리는 별도 ext4 작업 디스크(`/dev/vdb` → `/mnt/w`)로 넣었다. 출력은 모두 게스트에서 실제로 실행한 결과다.
- **TCG 주의**: 게스트 CPU는 에뮬레이션이라 실제 Cortex-A78AE(Orin)보다 수십 배 느리다. 시간·지연 숫자는 **상대 비교용**이지 실제 장비 수치가 아니다.

---

## 1. 컨테이너 · VM · chroot — 무엇이 다른가

### 1.1 직관

- **chroot**: "이 디렉터리를 `/`라고 생각해라"만 바꾼다. PID·네트워크·자원은 그대로 공유한다. 빌드 도구(K6 §3)로는 충분하지만 격리는 아니다.
- **컨테이너**: chroot보다 강한 `pivot_root` + namespace 8종 + cgroup + 권한 축소. 커널은 공유한다.
- **VM**: 하이퍼바이저(KVM 등)가 가상 하드웨어를 주고, 그 위에서 **게스트 커널**이 따로 돈다. 격리는 가장 강하지만 메모리·부팅·I/O 비용이 크다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
  <text x="160" y="22" text-anchor="middle" font-size="14" font-weight="bold">컨테이너</text>
  <text x="500" y="22" text-anchor="middle" font-size="14" font-weight="bold">VM</text>
  <rect x="20" y="36" width="90" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="65" y="58" text-anchor="middle" font-size="12">perception</text>
  <rect x="115" y="36" width="90" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="160" y="58" text-anchor="middle" font-size="12">planner</text>
  <rect x="210" y="36" width="90" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="255" y="58" text-anchor="middle" font-size="12">uploader</text>
  <rect x="20" y="76" width="90" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="65" y="98" text-anchor="middle" font-size="12">libs (image)</text>
  <rect x="115" y="76" width="90" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="160" y="98" text-anchor="middle" font-size="12">libs (image)</text>
  <rect x="210" y="76" width="90" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="255" y="98" text-anchor="middle" font-size="12">libs (image)</text>
  <rect x="20" y="118" width="280" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="160" y="140" text-anchor="middle" font-size="12">runtime: podman / crun (프로세스 생성만)</text>
  <rect x="20" y="160" width="280" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="180" text-anchor="middle" font-size="13" font-weight="bold">호스트 커널 하나</text>
  <text x="160" y="197" text-anchor="middle" font-size="12">namespace · cgroup · overlayfs</text>
  <rect x="20" y="212" width="280" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="160" y="234" text-anchor="middle" font-size="12">SoC (CPU · GPU · NPU · ISP)</text>
  <rect x="360" y="36" width="135" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="427" y="58" text-anchor="middle" font-size="12">app A</text>
  <rect x="505" y="36" width="135" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="572" y="58" text-anchor="middle" font-size="12">app B</text>
  <rect x="360" y="76" width="135" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="427" y="98" text-anchor="middle" font-size="12">guest userspace</text>
  <rect x="505" y="76" width="135" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="572" y="98" text-anchor="middle" font-size="12">guest userspace</text>
  <rect x="360" y="118" width="135" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="427" y="140" text-anchor="middle" font-size="12">게스트 커널</text>
  <rect x="505" y="118" width="135" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="572" y="140" text-anchor="middle" font-size="12">게스트 커널</text>
  <rect x="360" y="160" width="280" height="44" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="500" y="180" text-anchor="middle" font-size="13" font-weight="bold">하이퍼바이저 (KVM · Xen)</text>
  <text x="500" y="197" text-anchor="middle" font-size="12">가상 CPU · 가상 장치 · stage-2 MMU</text>
  <rect x="360" y="212" width="280" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="500" y="234" text-anchor="middle" font-size="12">SoC</text>
  <text x="160" y="276" text-anchor="middle" font-size="12">시작 수십 ms~수 초 · 오버헤드 작음</text>
  <text x="500" y="276" text-anchor="middle" font-size="12">부팅 수 초 · 커널 메모리 따로</text>
</svg>
```
그림 2 — 컨테이너와 VM의 층 비교. 컨테이너에는 "게스트 커널" 층이 없다. 그래서 가볍지만, 커널 버그·커널 모듈·장치 드라이버는 모두 공유한다

| 항목 | chroot | 컨테이너 | VM |
|---|---|---|---|
| 커널 | 공유 | 공유 | 따로(게스트 커널) |
| 보이는 PID·네트워크 | 호스트와 같음 | namespace로 분리 | 완전히 분리 |
| 자원 제한 | 없음 | cgroup | 가상 HW 크기 + 호스트 cgroup |
| 시작 시간 | 수 ms | 수십 ms~1 s (런타임에 따라) | 수 초 |
| 메모리 오버헤드 | 0 | 작음(런타임 감시 프로세스 수 MB) | 게스트 커널 + page cache 중복 |
| 장치 접근 | 그대로 | `/dev` 노드를 골라 넣고 cgroup으로 허용 | 가상 장치 또는 passthrough(VFIO) |
| 격리 강도 | 약함(탈출 쉬움) | 중간(커널 공격면 공유) | 강함 |
| Embedded 쓰임 | 빌드·복구 셸 | 앱 배포·의존성 분리 | 혼합 중요도(안전 RTOS + Linux) 분리 |

### 1.2 bare-metal 비유로 정리

| Linux 컨테이너 | SSD 펌웨어·RTOS에서 비슷한 것 |
|---|---|
| mount namespace + pivot_root | 태스크마다 다른 MPU 영역 → 볼 수 있는 메모리 창이 다르다 |
| PID namespace | 태스크 ID를 로컬 번호로 다시 매김 |
| cgroup `memory.max` | 태스크 전용 버퍼 풀 크기 — 넘으면 할당 실패(여기서는 OOM kill) |
| cgroup `cpu.max` | 백그라운드 GC에 걸던 시간 할당(budget per period) |
| `cpuset.cpus` | 특정 코어에 특정 태스크 고정(코어 0 = 호스트 I/O, 코어 1 = 백그라운드) |
| image layer | 부트로더 + 공통 라이브러리 + 앱 영역으로 나뉜 펌웨어 이미지 |
| capability·seccomp | 특권 명령(SVC 번호)별 허용 목록 |

### 1.3 흔한 함정

- "컨테이너 = 보안 경계"라고 믿는 것. 커널을 공유하니 커널 취약점 하나로 전부 뚫린다. 신뢰할 수 없는 코드를 돌릴 때는 VM 또는 gVisor·Kata 같은 샌드박스가 필요하다.
- "컨테이너 안에서 커널 모듈을 넣으면 그 컨테이너 전용이다" — 아니다. 모듈은 커널 전체에 들어간다. 그래서 정상적인 컨테이너는 `CAP_SYS_MODULE`이 없다.
- "컨테이너 이미지에 커널이 들어 있다" — 없다. 이미지 안 `/lib/modules`는 아무 의미가 없다. 드라이버는 호스트 이미지(OS 층)에 있어야 한다.

---

## 2. Namespace — "무엇이 보이나"를 프로세스마다 다르게

### 2.1 직관

namespace는 커널 자원의 **이름표 공간**이다. 같은 커널 안에 "PID 번호표 묶음"이 여러 개 있고, 프로세스는 그중 하나를 들고 산다. 다른 묶음의 프로세스는 아예 보이지 않는다. SSD 펌웨어에서 호스트별 namespace(NVMe namespace)가 같은 NAND 위에 서로 다른 LBA 공간을 보여 주던 것과 닮았다 — 실제 저장소는 하나인데 보이는 주소 공간이 다르다.

### 2.2 정의 — 8가지

| namespace | clone 플래그 | 분리하는 것 | 도입 | 컨테이너에서의 효과 |
|---|---|---|---|---|
| mnt | `CLONE_NEWNS` | 마운트 트리 | 2.4.19 | 컨테이너만의 `/`(overlay), `/proc`, bind mount |
| uts | `CLONE_NEWUTS` | hostname, domainname | 2.6.19 | 컨테이너마다 다른 hostname |
| ipc | `CLONE_NEWIPC` | System V IPC, POSIX 메시지 큐 | 2.6.19 | 공유 메모리 키 충돌 방지 (A7) |
| pid | `CLONE_NEWPID` | PID 번호 | 2.6.24 | 컨테이너 안 첫 프로세스가 PID 1 |
| net | `CLONE_NEWNET` | 네트워크 장치, IP, 라우팅, 방화벽, 소켓 포트 | 2.6.29 | 컨테이너만의 `eth0`, 포트 80을 여러 컨테이너가 동시에 |
| user | `CLONE_NEWUSER` | UID/GID 매핑, capability의 유효 범위 | 3.8 | rootless: 안의 root = 밖의 일반 사용자 |
| cgroup | `CLONE_NEWCGROUP` | `/proc/self/cgroup`과 cgroupfs의 루트 | 4.6 | 컨테이너가 자기 cgroup을 `/`로 본다 |
| time | `CLONE_NEWTIME` | `CLOCK_MONOTONIC`·`CLOCK_BOOTTIME` 오프셋 | 5.6 | 체크포인트·복원(CRIU) 후 시계 연속성 |

API는 세 개뿐이다: `clone()`/`clone3()`(새 namespace에서 자식 만들기), `unshare()`(나 자신을 새 namespace로), `setns()`(이미 있는 namespace에 들어가기 — `podman exec`, `nsenter`). 각 namespace는 `/proc/<pid>/ns/<type>` 파일로 보이고, `readlink`하면 `type:[inode번호]`가 나온다. **번호가 같으면 같은 namespace**다.

### 2.3 예제 1 — `unshare`로 PID·UTS·NET·MNT 분리하기

무엇을 확인하나: 호스트의 namespace 번호, `unshare`로 만든 새 namespace 안에서 PID 1·hostname·빈 네트워크, 그리고 바깥에서 본 같은 프로세스의 모습. (스크립트는 `set -x`로 돌렸고 노트에는 명령과 출력을 나눠 실었다. 이하 모든 게스트 예제 동일.)

```sh
uname -r
lsns -t pid -t net -t uts -t mnt -o NS,TYPE,NPROCS,PID,COMMAND | head -8
readlink /proc/self/ns/pid /proc/self/ns/uts /proc/self/ns/net /proc/self/ns/mnt
unshare --pid --fork --mount-proc --uts --net --ipc bash -c '
  hostname l8box; echo "hostname=$(hostname) my_pid=$$"; ps -e -o pid,comm
  ip -br link; readlink /proc/self/ns/pid /proc/self/ns/uts /proc/self/ns/net /proc/self/ns/mnt'
```

```text
6.18.55-sec
        NS TYPE NPROCS   PID COMMAND
4026531832 mnt      98     1 /sbin/init
4026531833 net     104     1 /sbin/init
4026531836 pid     104     1 /sbin/init
4026531838 uts     101     1 /sbin/init
4026532253 mnt       1   155 ├─/usr/lib/systemd/systemd-udevd
4026532254 mnt       1   176 ├─/usr/lib/systemd/systemd-networkd
4026532255 uts       1   155 ├─/usr/lib/systemd/systemd-udevd
pid:[4026531836]
uts:[4026531838]
net:[4026531833]
mnt:[4026531832]
hostname=l8box my_pid=1
    PID COMMAND
      1 bash
      4 ps
lo               DOWN           00:00:00:00:00:00 <LOOPBACK> 
pid:[4026532272]
uts:[4026532270]
net:[4026532273]
mnt:[4026532269]
```

출력에서 볼 것:

- 게스트 커널은 `6.18.55-sec`다(VM 호스트는 `7.0.0-38-generic`).
- **systemd가 이미 namespace를 쓰고 있다.** `systemd-udevd`, `systemd-networkd`는 자기만의 mnt(·uts) namespace에 산다. unit 파일의 `ProtectSystem=`, `PrivateTmp=`, `ProtectHostname=` 같은 옵션(L1)이 바로 이것이다. "컨테이너 엔진 없이도 격리는 된다"의 증거다.
- `unshare` 안에서 bash는 **PID 1**이고, `ps`에는 프로세스 두 개뿐이다. `--mount-proc`가 새 mount namespace에 새 `/proc`을 마운트해서 새 PID namespace의 내용만 보여 준다.
- 네트워크는 `lo` 하나, 그것도 DOWN. 새 net namespace는 **텅 빈 네트워크 스택**으로 시작한다.
- 네 개의 inode 번호가 모두 바뀌었다(`…836` → `…272` 등). 값은 실행마다 다르다.

### 2.4 바깥에서 본 컨테이너 프로세스

무엇을 확인하나: 같은 프로세스를 호스트에서 보면 평범한 PID를 갖고, `/proc/<pid>/ns`가 어떤 namespace를 바꿨고 어떤 것을 공유하는지 보여 준다.

```sh
unshare --pid --fork --mount-proc --uts --net sleep 30 &
sleep 1; P=$(pgrep -n -x sleep); echo "outside view: sleep pid=$P"
lsns -p $P -o NS,TYPE,NPROCS,PID,COMMAND
ls -l /proc/$P/ns | awk '{print $9, $10, $11}'
```

```text
outside view: sleep pid=468
        NS TYPE   NPROCS   PID COMMAND
4026531834 time      102     1 /sbin/init
4026531835 cgroup    102     1 /sbin/init
4026531837 user      102     1 /sbin/init
4026531839 ipc       102     1 /sbin/init
4026532269 mnt         2   464 unshare --pid --fork --mount-proc --uts --net sleep 30
4026532270 uts         2   464 unshare --pid --fork --mount-proc --uts --net sleep 30
4026532271 pid         1   468 └─sleep 30
4026532272 net         2   464 unshare --pid --fork --mount-proc --uts --net sleep 30
  
cgroup -> cgroup:[4026531835]
ipc -> ipc:[4026531839]
mnt -> mnt:[4026532269]
net -> net:[4026532272]
pid -> pid:[4026532271]
pid_for_children -> pid:[4026532271]
time -> time:[4026531834]
time_for_children -> time:[4026531834]
user -> user:[4026531837]
uts -> uts:[4026532270]
```

출력에서 볼 것:

- 컨테이너 안에서 PID 1인 `sleep`이 호스트에서는 **PID 468**이다. 호스트의 `ps`, `top`, `kill`로 그대로 다룰 수 있다. 장비에서 "컨테이너 안 프로세스의 CPU 사용률"을 보려고 따로 도구를 깔 필요가 없다는 뜻이다.
- mnt·uts·net namespace는 `unshare`(464)와 `sleep`(468)이 **둘이 같이** 쓴다(NPROCS 2). 그런데 pid namespace에는 `sleep` 하나만 있다. `unshare(CLONE_NEWPID)`는 호출한 프로세스 자신이 아니라 **그다음에 만들 자식**의 PID namespace만 바꾸기 때문이다. 그래서 `--fork`가 필요하고, `/proc/<pid>/ns/pid_for_children`이라는 파일이 따로 있다.
- user·ipc·cgroup·time은 호스트(PID 1)와 같은 번호다 — 이번에는 분리하지 않았다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="660" height="306" rx="8" fill="#888" fill-opacity="0.06" stroke="#888"/>
  <text x="24" y="32" font-size="13" font-weight="bold">호스트 초기 namespace — pid:[…836] · net:[…833] · mnt:[…832]</text>
  <rect x="24" y="52" width="230" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="139" y="74" text-anchor="middle" font-size="12">PID 1 /sbin/init (systemd)</text>
  <rect x="24" y="96" width="230" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="139" y="118" text-anchor="middle" font-size="12">PID 155 udevd — 자기 mnt · uts</text>
  <rect x="24" y="152" width="230" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="139" y="174" text-anchor="middle" font-size="12">PID 464 unshare</text>
  <line x1="254" y1="169" x2="318" y2="169" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="286" y="160" text-anchor="middle" font-size="12">fork</text>
  <rect x="320" y="52" width="336" height="178" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="488" y="74" text-anchor="middle" font-size="13" font-weight="bold">새 namespace 묶음</text>
  <text x="488" y="96" text-anchor="middle" font-size="12">mnt:[…269] · uts:[…270] · net:[…272]</text>
  <text x="488" y="114" text-anchor="middle" font-size="12">unshare(464)와 sleep(468)이 공유</text>
  <rect x="340" y="128" width="296" height="54" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="488" y="150" text-anchor="middle" font-size="12">pid:[…271] — sleep만 들어 있다</text>
  <text x="488" y="170" text-anchor="middle" font-size="12">호스트 PID 468 = 안에서 PID 1</text>
  <text x="488" y="210" text-anchor="middle" font-size="12">hostname l8box · 네트워크는 lo(DOWN)만</text>
  <rect x="24" y="250" width="632" height="48" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="270" text-anchor="middle" font-size="12">그대로 공유: user:[…837] · ipc:[…839] · cgroup:[…835] · time:[…834]</text>
  <text x="340" y="289" text-anchor="middle" font-size="12">같은 inode 번호 = 같은 namespace (/proc/&lt;pid&gt;/ns/*)</text>
</svg>
```
그림 3 — 예제 1·2.4의 namespace 관계. PID namespace는 `unshare`의 자식부터 적용되고, 나머지 셋은 `unshare` 자신도 같이 들어간다

### 2.5 예제 2 — veth pair로 두 network namespace 잇기

무엇을 확인하나: 텅 빈 net namespace에 "가상 랜선" 한 쌍(veth)을 꽂아 호스트와 통신시키기. Podman의 bridge 네트워크(§7)는 이것 + 브리지 + NAT 규칙이다.

```sh
ip netns add l8a
ip link add l8v0 type veth peer name l8v1
ip link set l8v1 netns l8a
ip addr add 10.88.0.1/24 dev l8v0; ip link set l8v0 up
ip -n l8a addr add 10.88.0.2/24 dev l8v1; ip -n l8a link set l8v1 up; ip -n l8a link set lo up
ip -n l8a -br addr
ping -c 2 -W 2 10.88.0.2
ip netns exec l8a ping -c 1 -W 2 10.88.0.1
ip netns del l8a; ip -br link | grep l8 || echo "veth gone with netns"
```

```text
lo               UNKNOWN        127.0.0.1/8 
l8v1@if4         UP             10.88.0.2/24 
PING 10.88.0.2 (10.88.0.2) 56(84) bytes of data.
64 bytes from 10.88.0.2: icmp_seq=1 ttl=64 time=5.96 ms
64 bytes from 10.88.0.2: icmp_seq=2 ttl=64 time=1.49 ms

--- 10.88.0.2 ping statistics ---
2 packets transmitted, 2 received, 0% packet loss, time 1005ms
rtt min/avg/max/mdev = 1.485/3.723/5.962/2.238 ms
PING 10.88.0.1 (10.88.0.1) 56(84) bytes of data.
64 bytes from 10.88.0.1: icmp_seq=1 ttl=64 time=0.702 ms

--- 10.88.0.1 ping statistics ---
1 packets transmitted, 1 received, 0% packet loss, time 0ms
rtt min/avg/max/mdev = 0.702/0.702/0.702/0.000 ms
veth gone with netns
```

출력에서 볼 것: `l8v1@if4` — 짝(peer)이 다른 namespace의 ifindex 4라는 표시다. 양방향 ping이 된다. namespace를 지우면 그 안의 veth 한쪽이 사라지고, 짝인 `l8v0`도 같이 사라진다. 지연(수 ms)은 TCG 에뮬레이션 값이다 — 실제 하드웨어의 veth 왕복은 수십 µs 수준이다.

### 2.6 흔한 함정

- `unshare --pid`만 하고 `--fork`를 빼면 "fork: Cannot allocate memory" 같은 이상한 에러가 난다. 호출자 자신은 옛 PID namespace에 남아 있기 때문이다.
- `--mount-proc` 없이 PID namespace만 바꾸면 `ps`가 여전히 호스트 프로세스를 보여 준다. `ps`는 `/proc`을 읽을 뿐이고 그 `/proc`은 호스트 것이다.
- 컨테이너의 PID 1은 커널이 특별 취급한다: 핸들러를 등록하지 않은 시그널(`SIGTERM` 포함)은 무시된다. 그래서 셸 스크립트를 PID 1로 두면 `podman stop`이 10초 기다렸다가 `SIGKILL`한다. `--init`(tini 같은 작은 init)을 쓰거나 앱이 `SIGTERM`을 처리해야 한다.
- net namespace 안에서 `CAP_NET_ADMIN`이 있으면 그 namespace의 방화벽·라우팅을 바꿀 수 있다 — 호스트 것이 아니라면 안전하지만, `--network host`라면 호스트 것이다(§12).

---

## 3. cgroup v2 — "얼마나 쓰나"를 묶음마다 제한하기

### 3.1 직관과 정의

cgroup(control group)은 프로세스들을 **트리 모양 그룹**으로 묶고, 그룹마다 자원 한도와 회계를 붙이는 기능이다. v2는 `/sys/fs/cgroup` 아래 **단일 계층** 하나만 있다(v1은 컨트롤러마다 트리가 따로였다). 디렉터리 = 그룹, 파일 = 설정·통계다. 프로세스를 그룹에 넣는 방법은 그 그룹의 `cgroup.procs`에 PID를 쓰는 것뿐이다. fork된 자식은 부모의 그룹을 물려받는다.

규칙 두 가지만 기억하면 된다.

- **top-down 활성화**: 부모의 `cgroup.subtree_control`에 켜진 컨트롤러만 자식 디렉터리에 파일로 나타난다.
- **no internal processes**: 컨트롤러가 켜진 그룹은(루트 제외) 프로세스를 직접 갖지 않고 자식 그룹에만 둔다. systemd가 `.slice`(중간 노드) / `.service`·`.scope`(잎)로 나누는 이유다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <rect x="250" y="12" width="180" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="34" text-anchor="middle" font-size="13" font-weight="bold">/sys/fs/cgroup</text>
  <line x1="300" y1="46" x2="85" y2="84" stroke="currentColor"/>
  <line x1="320" y1="46" x2="255" y2="84" stroke="currentColor"/>
  <line x1="360" y1="46" x2="425" y2="84" stroke="currentColor"/>
  <line x1="380" y1="46" x2="595" y2="84" stroke="currentColor"/>
  <rect x="20" y="86" width="130" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="85" y="108" text-anchor="middle" font-size="12">init.scope</text>
  <rect x="180" y="86" width="150" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="255" y="108" text-anchor="middle" font-size="12">system.slice</text>
  <rect x="350" y="86" width="150" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="425" y="108" text-anchor="middle" font-size="12">machine.slice</text>
  <rect x="530" y="86" width="130" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="595" y="108" text-anchor="middle" font-size="12">l8demo (손으로)</text>
  <line x1="230" y1="120" x2="140" y2="154" stroke="currentColor"/>
  <line x1="280" y1="120" x2="300" y2="154" stroke="currentColor"/>
  <line x1="425" y1="120" x2="480" y2="154" stroke="currentColor"/>
  <rect x="20" y="156" width="200" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="120" y="178" text-anchor="middle" font-size="12">systemd-networkd.service</text>
  <rect x="230" y="156" width="160" height="34" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="310" y="178" text-anchor="middle" font-size="12">l8app.service</text>
  <rect x="400" y="156" width="260" height="34" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="530" y="178" text-anchor="middle" font-size="12">libpod-&lt;container id&gt;.scope</text>
  <rect x="400" y="200" width="260" height="96" rx="4" fill="#e08a3c" fill-opacity="0.06" stroke="#e08a3c" stroke-dasharray="4 3"/>
  <text x="410" y="222" font-size="12">memory.max = 67108864</text>
  <text x="410" y="242" font-size="12">cpu.max = 50000 100000</text>
  <text x="410" y="262" font-size="12">pids.max = 30</text>
  <text x="410" y="282" font-size="12">cgroup.procs = 컨테이너 PID들</text>
  <rect x="20" y="214" width="370" height="82" rx="4" fill="#888" fill-opacity="0.06" stroke="#888" stroke-dasharray="4 3"/>
  <text x="30" y="236" font-size="12">cgroup.subtree_control — 자식에게 켤 컨트롤러</text>
  <text x="30" y="256" font-size="12">memory.events · cpu.stat · pids.events — 통계</text>
  <text x="30" y="276" font-size="12">cgroup.kill — 1을 쓰면 그룹 전체 SIGKILL</text>
  <text x="340" y="320" text-anchor="middle" font-size="12">systemd가 트리를 관리한다: slice = 중간 노드, service · scope = 프로세스가 사는 잎</text>
</svg>
```
그림 4 — 이 게스트의 cgroup v2 트리(일부). rootful Podman 컨테이너는 `machine.slice` 아래 scope로, Quadlet 컨테이너는 자기 `.service` 아래로 들어간다(예제 10·13에서 확인)

### 3.2 주요 컨트롤러와 파일

| 컨트롤러 | 설정 파일 | 의미 | 넘으면 |
|---|---|---|---|
| memory | `memory.max` | 하드 한도 | 그룹 안 reclaim → 실패하면 그룹 안에서 OOM kill |
| memory | `memory.high` | 소프트 한도(throttle 지점) | reclaim + 벌칙 sleep. 죽이지 않는다 |
| memory | `memory.min` / `memory.low` | 보호량(이만큼은 남겨 둠) | 다른 그룹의 reclaim에서 보호 |
| memory | `memory.oom.group` | 1이면 OOM 때 그룹 통째로 kill | 반쯤 죽은 서비스 방지 (F11 §10) |
| cpu | `cpu.weight` (1~10000, 기본 100) | 경쟁할 때의 비율 | 쉬는 CPU는 누구든 쓴다 |
| cpu | `cpu.max` ("quota period", µs) | 주기당 최대 실행 시간 | 그 주기의 남은 시간 동안 **throttle** |
| cpuset | `cpuset.cpus`, `cpuset.mems` | 쓸 수 있는 CPU·메모리 노드 | 그 밖으로는 스케줄 안 됨 |
| pids | `pids.max` | 태스크(스레드 포함) 수 | `fork`/`clone`이 `EAGAIN` |
| io | `io.max` ("major:minor rbps= wbps= riops= wiops=") | 장치별 대역폭·IOPS 상한 | 요청 지연 |

`cpu.weight` vs `cpu.max`는 면접 단골이다. weight는 **경쟁할 때만** 효과가 있고 놀고 있는 CPU는 막지 않는다(work-conserving). max는 CPU가 놀고 있어도 **막는다** — 그래서 지연을 만든다(§4).

### 3.3 예제 3 — `memory.max`로 OOM kill, `memory.high`로 throttle

무엇을 확인하나: 32 MiB 한도 그룹에서 100 MiB를 만지는 프로그램이 어떻게 죽는지, 같은 32 MiB를 `memory.high`로 걸면 어떻게 되는지. `l8_eat`은 8 MiB씩 `malloc` + `memset`하는 정적 링크 C 프로그램이다.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv) {
    int total = argc > 1 ? atoi(argv[1]) : 100;          /* MiB to touch */
    for (int mb = 8; mb <= total; mb += 8) {
        char *p = malloc(8 << 20);
        if (!p) { perror("malloc"); return 1; }
        memset(p, 0xA5, 8 << 20);                          /* fault every page in */
        printf("touched %d MiB\n", mb);
        fflush(stdout);
        usleep(50000);
    }
    puts("done (never OOM-killed)");
    return 0;
}
```

```sh
cat /sys/fs/cgroup/cgroup.controllers
cat /sys/fs/cgroup/cgroup.subtree_control
mkdir /sys/fs/cgroup/l8demo; cd /sys/fs/cgroup/l8demo
echo 32M > memory.max; echo 0 > memory.swap.max
sh -c 'echo $$ > /sys/fs/cgroup/l8demo/cgroup.procs; exec /mnt/w/l8_eat 100'; echo "exit=$?"
cat memory.events; cat memory.peak
dmesg | grep -E "oom-kill:|Memory cgroup out of memory" | tail -2
echo max > memory.max; echo 32M > memory.high
timeout 15 sh -c 'echo $$ > /sys/fs/cgroup/l8demo/cgroup.procs; exec /mnt/w/l8_eat 48'; echo "exit=$?"
grep -E "^(high|max|oom_kill) " memory.events
```

```text
cpuset cpu io memory hugetlb pids
cpuset cpu io memory pids
touched 8 MiB
touched 16 MiB
touched 24 MiB
/mnt/w/ex2_cg.sh: line 9:   545 Killed                  sh -c 'echo $$ > /sys/fs/cgroup/l8demo/cgroup.procs; exec '$W'/l8_eat 100'
exit=137
low 0
high 0
max 50
oom 1
oom_kill 1
oom_group_kill 0
33554432
[   95.242673] oom-kill:constraint=CONSTRAINT_MEMCG,nodemask=(null),cpuset=l8demo,mems_allowed=0,oom_memcg=/l8demo,task_memcg=/l8demo,task=l8_eat,pid=545,uid=0
[   95.243765] Memory cgroup out of memory: Killed process 545 (l8_eat) total-vm:33792kB, anon-rss:32540kB, file-rss:76kB, shmem-rss:0kB, UID:0 pgtables:100kB oom_score_adj:0
touched 8 MiB
touched 16 MiB
touched 24 MiB
touched 32 MiB
exit=124
high 705
max 50
oom_kill 1
```

출력에서 볼 것:

- 루트의 `subtree_control`에 `cpuset cpu io memory pids`가 켜져 있다(systemd가 켠다). 그래서 `mkdir l8demo`만으로 그 파일들이 생긴다.
- `memory.max`: 24 MiB까지 찍고 다음 8 MiB를 만지다가 **SIGKILL(exit 137 = 128 + 9)**. `memory.events`의 `max 50`은 "한도에 50번 부딪혀 reclaim을 시도했다", `oom_kill 1`은 "한 번 죽였다". `memory.peak`는 정확히 33554432 = 32 MiB.
- dmesg의 `constraint=CONSTRAINT_MEMCG`, `oom_memcg=/l8demo` — 시스템 전체 OOM이 아니라 **그 그룹 안에서만** 일어난 OOM이다. 다른 서비스는 아무 일도 겪지 않았다. 이것이 장비에서 "업로더가 메모리를 새도 인식 프로세스는 산다"를 만드는 장치다(F11 §10에 OOM 경로 상세).
- `memory.high`: 32 MiB를 찍은 뒤 **멈춘 것처럼 느려지다가** 15초 timeout(exit 124)으로 끝났다. 죽지 않았다(`oom_kill` 그대로 1). `high 705`는 high를 넘은 횟수. swap이 없는 장비에서 익명 메모리는 reclaim할 데가 없어서 프로세스가 벌칙 sleep에 갇힌다. **`memory.high`만 걸고 `memory.max`를 안 걸면 "죽지도 않고 일도 안 하는" 좀비 서비스가 된다** — 보통은 high를 max보다 조금 낮게 두어 경고 구간으로 쓴다.

### 3.4 예제 4 — `cpu.max`로 throttling 재기

무엇을 확인하나: 20%(20 ms / 100 ms) 한도에서 busy loop를 3초 돌리면 실제로 몇 ms를 쓰고 몇 번 throttle되는지.

```sh
cat cpu.stat | head -6
echo "20000 100000" > cpu.max
timeout 3 sh -c 'echo $$ > /sys/fs/cgroup/l8demo/cgroup.procs; while :; do :; done'
cat cpu.stat
```

```text
usage_usec 926990
user_usec 134955
system_usec 792034
nice_usec 0
nr_periods 0
nr_throttled 0
usage_usec 1532213
user_usec 740178
system_usec 792034
nice_usec 0
nr_periods 31
nr_throttled 30
throttled_usec 2345455
nr_bursts 0
burst_usec 0
```

출력에서 볼 것: 3초 동안 `usage_usec`가 926990 → 1532213, 즉 **605 ms ≈ 20%** 만 썼다. 31개 주기 중 30개에서 throttle됐고(`nr_throttled 30`), 묶여 있던 시간 합이 2.35초다. 앞 예제들이 쓴 CPU 시간(usage 927 ms)은 그대로 누적돼 있다 — 통계는 그룹이 살아 있는 동안 계속 쌓인다.

### 3.5 예제 5 — `pids.max`로 fork bomb 멈추기

무엇을 확인하나: 프로세스 수 한도 20에서 `sleep`을 40개 띄우려 하면 어디서 멈추는지, 그리고 `cgroup.kill`로 그룹을 한 번에 정리하기.

```sh
mkdir /sys/fs/cgroup/l8demo; cd /sys/fs/cgroup/l8demo
echo 20 > pids.max
sh -c 'echo $$ > /sys/fs/cgroup/l8demo/cgroup.procs; for i in $(seq 40); do sleep 20 & done 2>/tmp/l8err; tail -2 /tmp/l8err'
echo "in cgroup now: $(cat pids.current)"
cat pids.events pids.peak
echo 1 > cgroup.kill; sleep 1; cat cgroup.procs | wc -l
cd /; rmdir /sys/fs/cgroup/l8demo && echo removed
```

```text
in cgroup now: 19
max 1
20
0
removed
```

출력에서 볼 것: `sleep`을 40개 띄우려 했지만 **pids.peak 20**에서 멈췄다. 셸(1) + sleep 19개 = 20개가 한도다. 21번째 `fork()`가 `EAGAIN`을 받자 dash가 루프를 중단했고(`pids.events`의 `max 1` = 한도에 1번 걸림), 셸이 끝난 뒤 그룹에는 sleep 19개가 남았다. `cgroup.kill`에 1을 쓰자 그룹의 모든 프로세스가 한 번에 SIGKILL되어 0개가 됐다 — `kill`로 PID를 하나씩 찾는 것보다 확실하다(fork 중인 프로세스도 놓치지 않는다). systemd가 서비스를 멈출 때도 같은 원리로 cgroup 단위로 정리한다.

### 3.6 흔한 함정

- **v1 문서로 v2를 만지는 것**: `memory.limit_in_bytes`, `cpu.cfs_quota_us`, `devices.allow`는 v1 파일이다. 이 랩 커널처럼 v1 memcg가 꺼진 커널(F11)과 최신 배포판에는 없다. Docker 옛 문서·Jetson 옛 포럼 글이 v1 기준인 경우가 많다.
- **systemd 밑에 손으로 디렉터리를 만드는 것**: 예제처럼 루트 아래 `l8demo`를 만들면 systemd가 모르는 그룹이 생긴다. 제품에서는 `systemd-run --scope -p MemoryMax=32M ...`이나 unit 파일의 `MemoryMax=`·`CPUQuota=`·`TasksMax=`(L1)를 쓴다. Podman도 기본으로 systemd에게 scope를 만들어 달라고 한다(cgroup manager = systemd).
- **`pids.max`가 스레드도 센다**: 스레드 300개짜리 ROS 2 노드에 `--pids-limit 100`을 주면 `pthread_create`가 `EAGAIN`으로 실패한다. 증상이 "가끔 노드가 뜨다가 죽는다"로 보인다.
- **`memory.max`에 page cache도 과금된다**: 큰 로그 파일을 쓰는 컨테이너는 익명 메모리가 작아도 cache 때문에 한도에 닿는다. cache는 reclaim 가능하니 보통 괜찮지만, dirty page가 많으면 writeback 대기로 지연이 생긴다.

---

## 4. 실시간·성능 — 컨테이너 안 제어 루프가 deadline을 놓치는 이유

### 4.1 직관

`cpu.max`는 "100 ms마다 20 ms까지만"처럼 **주기 단위 예산**이다. 예산을 다 쓰면 그 주기의 남은 80 ms 동안 그룹 전체가 run queue에서 빠진다. 평균 사용률이 예산보다 낮아도, 짧은 시간에 몰아 쓰면 묶인다. 10 ms 주기 제어 루프 입장에서는 **80 ms 동안 CPU가 사라지는 것**과 같다. 이게 "컨테이너에 `--cpus=0.2`를 줬더니 평균 CPU는 15%인데 제어 루프가 가끔 수십 ms 늦는다"의 정체다.

SSD 펌웨어로 치면, 백그라운드 GC 태스크에 "1 ms 슬롯마다 200 µs" 예산을 거는 건 괜찮지만, 같은 방식으로 **호스트 명령 처리 태스크**에 예산을 걸면 큐 지연이 폭발하는 것과 같다. 예산(throttling)은 배경 작업용이고, 지연에 민감한 작업에는 우선순위·전용 코어를 준다.

### 4.2 예제 6 — 주기 루프의 지연을 `cpu.max` 설정별로 재기

무엇을 확인하나: 10 ms마다 깨어나 3 ms 일하는(평균 30%) 루프를 cgroup 한도를 바꿔 가며 300주기 돌리고, 깨어남 지연(wake-up latency)과 deadline miss(일이 주기 안에 안 끝남)를 센다. 마지막은 같은 20% 한도에서 SCHED_FIFO로 돌린다.

```c
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static long long ns(struct timespec t) { return t.tv_sec * 1000000000LL + t.tv_nsec; }
static void spin_us(long us) {                       /* burn CPU for 'us' microseconds */
    struct timespec a, b;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &a);
    do clock_gettime(CLOCK_THREAD_CPUTIME_ID, &b); while (ns(b) - ns(a) < us * 1000LL);
}
int main(int argc, char **argv) {
    long period_us = argc > 1 ? atol(argv[1]) : 10000;   /* 10 ms control loop */
    long work_us   = argc > 2 ? atol(argv[2]) : 3000;    /* 3 ms of work per cycle */
    int  cycles    = argc > 3 ? atoi(argv[3]) : 300;
    struct timespec next, now;
    long long worst = 0, sum = 0; int miss = 0;
    clock_gettime(CLOCK_MONOTONIC, &next);
    for (int i = 0; i < cycles; i++) {
        next.tv_nsec += period_us * 1000;
        while (next.tv_nsec >= 1000000000) { next.tv_nsec -= 1000000000; next.tv_sec++; }
        if (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL)) { perror("sleep"); return 1; }
        clock_gettime(CLOCK_MONOTONIC, &now);
        long long late = ns(now) - ns(next);                 /* wake-up latency */
        spin_us(work_us);
        clock_gettime(CLOCK_MONOTONIC, &now);
        long long done = ns(now) - ns(next);                 /* finish time inside period */
        if (done > period_us * 1000LL) miss++;
        if (late > worst) worst = late;
        sum += late;
    }
    printf("cycles=%d avg_wake_late=%lldus worst_wake_late=%lldus deadline_miss=%d\n",
           cycles, sum / cycles / 1000, worst / 1000, miss);
    return 0;
}
```

```sh
mkdir /sys/fs/cgroup/l8lat; cd /sys/fs/cgroup/l8lat
st() { awk '/^(usage_usec|nr_throttled|throttled_usec)/{printf "%s=%s ", $1, $2}' cpu.stat; echo; }
run() { echo "## cpu.max=\"$(cat cpu.max)\" cmd: $*"; st; sh -c 'echo $$ > /sys/fs/cgroup/l8lat/cgroup.procs; exec "$@"' sh "$@"; st; }
echo max > cpu.max;            run /mnt/w/l8_lat 10000 3000 300
echo "50000 100000" > cpu.max; run /mnt/w/l8_lat 10000 3000 300
echo "20000 100000" > cpu.max; run /mnt/w/l8_lat 10000 3000 300
echo "2000 10000" > cpu.max;   run /mnt/w/l8_lat 10000 3000 300
echo "20000 100000" > cpu.max; run chrt -f 50 /mnt/w/l8_lat 10000 3000 300
zcat /proc/config.gz | grep -E "RT_GROUP_SCHED|CFS_BANDWIDTH"
```

```text
## cpu.max="max 100000" cmd: /mnt/w/l8_lat 10000 3000 300
usage_usec=0 nr_throttled=0 throttled_usec=0 
cycles=300 avg_wake_late=1224us worst_wake_late=28573us deadline_miss=11
usage_usec=1056742 nr_throttled=0 throttled_usec=0 
## cpu.max="50000 100000" cmd: /mnt/w/l8_lat 10000 3000 300
usage_usec=1056742 nr_throttled=0 throttled_usec=0 
cycles=300 avg_wake_late=478us worst_wake_late=2476us deadline_miss=0
usage_usec=2019489 nr_throttled=0 throttled_usec=0 
## cpu.max="20000 100000" cmd: /mnt/w/l8_lat 10000 3000 300
usage_usec=2019489 nr_throttled=0 throttled_usec=0 
cycles=300 avg_wake_late=802908us worst_wake_late=1666299us deadline_miss=291
usage_usec=2971222 nr_throttled=46 throttled_usec=3554806 
## cpu.max="2000 10000" cmd: /mnt/w/l8_lat 10000 3000 300
usage_usec=2971222 nr_throttled=46 throttled_usec=3554806 
cycles=300 avg_wake_late=1009133us worst_wake_late=2568567us deadline_miss=300
usage_usec=4085121 nr_throttled=596 throttled_usec=7690960 
## cpu.max="20000 100000" cmd: chrt -f 50 /mnt/w/l8_lat 10000 3000 300
usage_usec=4085121 nr_throttled=596 throttled_usec=7690960 
cycles=300 avg_wake_late=2244us worst_wake_late=37862us deadline_miss=32
usage_usec=5338871 nr_throttled=598 throttled_usec=7793501 
CONFIG_CFS_BANDWIDTH=y
# CONFIG_RT_GROUP_SCHED is not set
```

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="13" font-weight="bold">10 ms 주기 · 3 ms 일 · 300주기 중 deadline miss 수 (TCG)</text>
  <text x="182" y="67" text-anchor="end" font-size="12">한도 없음 (max)</text>
  <rect x="190" y="50" width="14" height="24" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
  <text x="210" y="67" font-size="12">11 · 평균 지연 1.2 ms</text>
  <text x="182" y="109" text-anchor="end" font-size="12">50% (50/100 ms)</text>
  <rect x="190" y="92" width="2" height="24" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
  <text x="198" y="109" font-size="12">0 · 평균 지연 0.48 ms</text>
  <text x="182" y="151" text-anchor="end" font-size="12">20% (20/100 ms)</text>
  <rect x="190" y="134" width="369" height="24" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
  <text x="553" y="151" text-anchor="end" font-size="12">291 · 평균 지연 803 ms</text>
  <text x="182" y="193" text-anchor="end" font-size="12">20% (2/10 ms)</text>
  <rect x="190" y="176" width="380" height="24" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
  <text x="564" y="193" text-anchor="end" font-size="12">300 · 평균 지연 1009 ms</text>
  <text x="182" y="235" text-anchor="end" font-size="12">20% + SCHED_FIFO</text>
  <rect x="190" y="218" width="41" height="24" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
  <text x="237" y="235" font-size="12">32 · 평균 지연 2.2 ms</text>
  <line x1="190" y1="44" x2="190" y2="260" stroke="currentColor"/>
  <line x1="190" y1="260" x2="570" y2="260" stroke="currentColor"/>
  <line x1="190" y1="260" x2="190" y2="265" stroke="currentColor"/>
  <text x="190" y="280" text-anchor="middle" font-size="12">0</text>
  <line x1="317" y1="260" x2="317" y2="265" stroke="currentColor"/>
  <text x="317" y="280" text-anchor="middle" font-size="12">100</text>
  <line x1="443" y1="260" x2="443" y2="265" stroke="currentColor"/>
  <text x="443" y="280" text-anchor="middle" font-size="12">200</text>
  <line x1="570" y1="260" x2="570" y2="265" stroke="currentColor"/>
  <text x="570" y="280" text-anchor="middle" font-size="12">300</text>
</svg>
```
그림 5 — 예제 6의 결과. 한도가 수요(30%)보다 넉넉하면 문제없지만, 수요보다 작은 순간 거의 모든 주기를 놓친다. SCHED_FIFO는 `cpu.max`에 걸리지 않는다

출력에서 볼 것:

- **한도 50%(수요 30%보다 큼)**: throttle 0회, miss 0. 한도 자체가 나쁜 게 아니라 **한도 < 순간 수요**가 문제다.
- **한도 20%**: 300주기 중 291개를 놓쳤고 평균 깨어남 지연이 0.8초다. 루프는 "따라잡으려고" 밀린 주기를 몰아서 돌고, 그게 다시 예산을 다 써서 또 묶인다. 46번 throttle에 묶인 시간 합 3.55초.
- **같은 20%라도 period를 10 ms로 줄이면**(2 ms / 10 ms) throttle 횟수는 550번으로 늘고 한 번 묶이는 시간은 짧아지지만, 수요(30%)가 한도보다 크니 결과는 똑같이 전멸이다. period를 줄이면 "묶이는 최대 시간"은 줄지만 관리 오버헤드(타이머)가 늘고, 수요 초과는 해결하지 못한다.
- **SCHED_FIFO**: usage가 1.25초 늘었는데(약 42% — 20% 한도를 훌쩍 넘음) throttle은 2회만 늘었다(`chrt`가 exec되기 전 fair 클래스에서 잠깐). CFS bandwidth는 **fair 클래스 전용**이라 RT 태스크는 `cpu.max`를 무시한다. 이 커널은 `RT_GROUP_SCHED`가 꺼져 있어 RT 태스크의 그룹 단위 제한 수단이 없다(F1 §5 — 대신 fair_server가 RT가 CPU를 독점하지 못하게 일반 태스크 몫을 예약한다).
- **TCG 주의**: 한도 없음에서도 miss 11, worst 28 ms가 나온 것은 에뮬레이터의 vCPU 스레드가 VM 호스트에서 다른 작업(다른 노트 작성자들의 QEMU)과 경쟁하기 때문이다. 실제 하드웨어라면 이 기준선은 µs 단위다. 절대값이 아니라 설정 간 차이를 봐야 한다.

### 4.3 예제 7 — 시끄러운 이웃과 cpuset

무엇을 확인하나: 같은 루프를 CPU3에 고정하고, busy loop 두 개(다른 cgroup = "이웃 컨테이너")를 같은 CPU에 두었을 때, 다른 CPU로 떼어 냈을 때, 같은 CPU지만 `cpu.weight`를 크게 줬을 때.

```sh
mkdir /sys/fs/cgroup/l8noisy /sys/fs/cgroup/l8ctl
hog() { sh -c "echo \$\$ > /sys/fs/cgroup/l8noisy/cgroup.procs; exec sh -c 'while :; do :; done'" & }
ctl() { sh -c 'echo $$ > /sys/fs/cgroup/l8ctl/cgroup.procs; exec "$@"' sh "$@"; }
echo 3 > /sys/fs/cgroup/l8ctl/cpuset.cpus; ctl /mnt/w/l8_lat 10000 3000 300            # A
echo 3 > /sys/fs/cgroup/l8noisy/cpuset.cpus; hog; hog; sleep 1; ctl /mnt/w/l8_lat 10000 3000 300   # B
echo 0-1 > /sys/fs/cgroup/l8noisy/cpuset.cpus; sleep 1; ctl /mnt/w/l8_lat 10000 3000 300           # C
echo 3 > /sys/fs/cgroup/l8noisy/cpuset.cpus; echo 10000 > /sys/fs/cgroup/l8ctl/cpu.weight; sleep 1
ctl /mnt/w/l8_lat 10000 3000 300                                                       # D
echo 1 > /sys/fs/cgroup/l8noisy/cgroup.kill
```

```text
0-3
## A: no neighbor, ctl on CPU3
cycles=300 avg_wake_late=1653us worst_wake_late=34533us deadline_miss=17
## B: 2 hogs share CPU3 with ctl (same cpuset)
cycles=300 avg_wake_late=7089us worst_wake_late=68217us deadline_miss=100
## C: hogs moved to CPU0-1 (cpuset partition), ctl alone on CPU3
cycles=300 avg_wake_late=3709us worst_wake_late=36953us deadline_miss=73
## D: hogs back on CPU3 but ctl cpu.weight=10000 vs hogs 100
cycles=300 avg_wake_late=1444us worst_wake_late=22334us deadline_miss=25
```

출력에서 볼 것: 이웃이 같은 코어에 있으면(B) miss가 17 → 100으로, 평균 지연이 4배로 뛴다. weight를 100배 주면(D) 기준선(A) 수준으로 돌아온다 — EEVDF/CFS가 깨어난 고-weight 태스크에 빨리 CPU를 준다. C(코어 분리)는 이론상 A와 같아야 하는데 73이 나왔다. **이것은 TCG 한계다**: 게스트의 "다른 코어"에서 도는 busy loop도 VM 호스트의 실제 코어를 먹어서, 에뮬레이션된 CPU3의 진행 자체가 느려진다. 실제 SoC에서는 코어 분리가 가장 확실한 방법이고, 그래서 다음 절의 규칙이 나온다.

### 4.4 장비에서의 규칙

- **제어 루프에는 `cpu.max`를 걸지 않는다.** 배경 작업(업로더, 로그 압축, 모델 다운로드)에만 건다. 지연에 민감한 컨테이너에는 `cpu.weight`(Podman `--cpu-shares` — cgroup v2에서는 weight로 환산된다)로 우선권을 준다.
- **코어를 나눈다.** `cpuset.cpus`(Podman `--cpuset-cpus`)로 컨테이너별 코어를 분리하고, 하드 RT 스레드용 코어는 부트 인자 `isolcpus=`·`nohz_full=`·`rcu_nocbs=`로 일반 스케줄링·tick·RCU 콜백에서 빼고, IRQ affinity를 옮긴다(N2). cgroup v2의 `cpuset.cpus.partition=isolated`는 런타임에 같은 효과를 내는 최신 방법이다(이 게스트에도 파일이 있다 — §3.3 목록).
- **RT 스레드를 컨테이너 안에서 쓰려면** `CAP_SYS_NICE`(`--cap-add SYS_NICE`)와 `--ulimit rtprio=99`, 그리고 메모리 lock(`--ulimit memlock=-1`, 앱의 `mlockall`)이 필요하다. 커널이 PREEMPT_RT인지, 컨테이너 런타임 오버헤드가 아니라 커널·드라이버 지연이 지배적인지 `cyclictest`로 확인한다(N3).
- **Docker의 `--cpu-rt-runtime`은** `RT_GROUP_SCHED`가 켜진 커널에서만 의미가 있다. 이 랩 커널과 대부분의 배포판·임베디드 커널은 꺼져 있다 — 옵션이 있다고 동작한다는 뜻이 아니다.
- **측정은 장비에서, 부하를 건 상태로.** 이웃 컨테이너가 모델을 내려받고 압축을 푸는 중일 때(메모리 대역폭, page cache writeback까지 경쟁) 제어 루프의 worst-case를 잰다.

---

## 5. rootfs — 이미지 층, overlayfs, 그리고 50줄짜리 컨테이너

### 5.1 직관 — 층을 겹친다

컨테이너 이미지는 tar 파일 몇 개(층)다. 런타임은 층들을 풀어서 디렉터리로 두고, overlayfs로 **아래(lower) 여러 층 + 위(upper) 쓰기층 하나**를 겹쳐 하나의 디렉터리(merged)로 만든다. 컨테이너가 파일을 고치면 그 파일만 upper로 복사(copy-up)된 뒤 고쳐진다. 원본 층은 절대 바뀌지 않으니, 같은 층을 여러 컨테이너·이미지가 공유할 수 있다. overlayfs 자체의 동작(copy-up, whiteout, opaque)은 J3 §7에서 직접 실험했다. 여기서는 컨테이너 쪽 쓰임새만 본다.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="160" y="14" width="360" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="39" text-anchor="middle" font-size="13" font-weight="bold">merged → pivot_root 후 컨테이너의 /</text>
  <line x1="340" y1="72" x2="340" y2="56" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="160" y="74" width="360" height="36" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="340" y="97" text-anchor="middle" font-size="12">upperdir — 컨테이너 쓰기층 (컨테이너마다 1개)</text>
  <rect x="160" y="118" width="360" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="141" text-anchor="middle" font-size="12">층 3: app.version (3 kB) — v1 · v2가 다름</text>
  <rect x="160" y="162" width="360" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="185" text-anchor="middle" font-size="12">층 2: model.bin (5.25 MB) — v1 · v2가 공유</text>
  <rect x="160" y="206" width="360" height="36" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="229" text-anchor="middle" font-size="12">층 1: Alpine 3.24.2 base (8.95 MB) — 모두가 공유</text>
  <text x="540" y="141" font-size="12">lowerdir</text>
  <text x="540" y="161" font-size="12">(읽기전용,</text>
  <text x="540" y="181" font-size="12">sha256 이름)</text>
  <rect x="20" y="74" width="120" height="36" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="80" y="97" text-anchor="middle" font-size="12">workdir</text>
  <line x1="140" y1="92" x2="158" y2="92" stroke="currentColor"/>
  <text x="340" y="272" text-anchor="middle" font-size="12">쓰기: 파일을 upper로 copy-up 후 수정 · 삭제: upper에 whiteout</text>
  <text x="340" y="294" text-anchor="middle" font-size="12">층 디렉터리: /var/lib/containers/storage/overlay/&lt;id&gt;/diff (예제 10 · 12)</text>
</svg>
```
그림 6 — 컨테이너 rootfs의 층 구조. 숫자는 예제 12에서 만든 `l8/app:v2` 이미지의 실제 층 크기다

### 5.2 예제 8 — namespace + overlayfs + pivot_root + cgroup으로 컨테이너 직접 만들기

무엇을 확인하나: 런타임 없이 C 60줄로 "컨테이너"를 만든다. 부모가 cgroup(`l8mini`: 64 MiB, pids 20, CPU 50%)을 만들고 `clone(CLONE_NEWPID|NEWNS|NEWUTS|NEWNET|NEWIPC)`로 자식을 만든 뒤 자식 PID를 `cgroup.procs`에 넣고 파이프로 "출발" 신호를 준다. 자식은 마운트 전파를 끊고(`MS_PRIVATE`), tmpfs 위에 upper/work를 만들어 Alpine rootfs를 lower로 overlay를 올리고, `pivot_root`로 그것을 `/`로 만든 뒤 옛 루트를 떼어 내고, `/proc`을 새로 마운트하고, hostname을 바꾸고 `execv`한다. runc/crun이 하는 일의 뼈대가 정확히 이것이다.

```c
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#define DIE(m) do { perror(m); exit(1); } while (0)
static int sync_pipe[2];
static char *lower, **cmd;
static char stack[1 << 20];

static void wr(const char *path, const char *val) {
    int fd = open(path, O_WRONLY);
    if (fd < 0 || write(fd, val, strlen(val)) < 0) DIE(path);
    close(fd);
}
static int child(void *arg) {
    char c, opts[512];
    (void)arg;
    if (read(sync_pipe[0], &c, 1) != 1) DIE("sync");      /* wait: parent put us in cgroup */
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)) DIE("make-rprivate");
    if (mount("tmpfs", "/run/l8c", "tmpfs", 0, "size=16m")) DIE("tmpfs");
    mkdir("/run/l8c/up", 0755); mkdir("/run/l8c/wk", 0755); mkdir("/run/l8c/m", 0755);
    snprintf(opts, sizeof opts, "lowerdir=%s,upperdir=/run/l8c/up,workdir=/run/l8c/wk", lower);
    if (mount("overlay", "/run/l8c/m", "overlay", 0, opts)) DIE("overlay");
    mkdir("/run/l8c/m/.old", 0700);
    if (syscall(SYS_pivot_root, "/run/l8c/m", "/run/l8c/m/.old")) DIE("pivot_root");
    if (chdir("/")) DIE("chdir");
    if (umount2("/.old", MNT_DETACH)) DIE("umount old");
    rmdir("/.old");
    if (mount("proc", "/proc", "proc", 0, NULL)) DIE("proc");
    if (sethostname("l8c", 3)) DIE("sethostname");
    execv(cmd[0], cmd); DIE("execv");
}
int main(int argc, char **argv) {
    char pid[16];
    if (argc < 3) { fprintf(stderr, "usage: %s LOWERDIR CMD...\n", argv[0]); return 2; }
    lower = argv[1]; cmd = argv + 2;
    mkdir("/run/l8c", 0755);
    if (mkdir("/sys/fs/cgroup/l8mini", 0755) && access("/sys/fs/cgroup/l8mini", F_OK)) DIE("cg");
    wr("/sys/fs/cgroup/l8mini/memory.max", "64M");
    wr("/sys/fs/cgroup/l8mini/pids.max", "20");
    wr("/sys/fs/cgroup/l8mini/cpu.max", "50000 100000");
    if (pipe(sync_pipe)) DIE("pipe");
    int flags = CLONE_NEWPID | CLONE_NEWNS | CLONE_NEWUTS | CLONE_NEWNET | CLONE_NEWIPC | SIGCHLD;
    pid_t p = clone(child, stack + sizeof stack, flags, NULL);
    if (p < 0) DIE("clone");
    snprintf(pid, sizeof pid, "%d", p);
    wr("/sys/fs/cgroup/l8mini/cgroup.procs", pid);
    if (write(sync_pipe[1], "x", 1) != 1) DIE("go");
    int st; waitpid(p, &st, 0);
    printf("[host] container pid %d exited, status %d\n", p, WEXITSTATUS(st));
    rmdir("/sys/fs/cgroup/l8mini");
    return 0;
}
```

빌드는 VM 호스트에서 정적 링크로 했다(게스트 glibc 버전과 무관하게 돌도록). 경고 0개.

```sh
# VM 호스트
gcc -std=gnu11 -Wall -Wextra -O2 -static l8_mini.c -o /var/tmp/L8/w/l8_mini
# 게스트
mkdir -p /var/l8/alpine && tar -xzf /mnt/w/alpine-minirootfs-3.24.2-aarch64.tar.gz -C /var/l8/alpine
du -sh /var/l8/alpine
/mnt/w/l8_mini /var/l8/alpine /bin/sh -c 'echo "hostname=$(hostname) pid=$$"; ps; cat /etc/alpine-release; ls /sys/class/net 2>&1; cat /proc/self/cgroup; cat /sys/fs/cgroup/memory.max 2>&1; touch /i_was_here; ls /'
ls /var/l8/alpine/i_was_here
```

```text
8.9M	/var/l8/alpine
hostname=l8c pid=1
PID   USER     TIME  COMMAND
    1 root      0:00 /bin/sh -c echo "hostname=$(hostname) pid=$$"; ps; cat /etc/alpine-release; ls /sys/class/net 2>&1; cat /proc/self/cgroup; cat /sys/fs/cgroup/memory.max 2>&1; touch /i_was_here; ls /
    3 root      0:00 ps
3.24.2
ls: /sys/class/net: No such file or directory
0::/l8mini
cat: can't open '/sys/fs/cgroup/memory.max': No such file or directory
bin
dev
etc
home
i_was_here
lib
media
mnt
opt
proc
root
run
sbin
srv
sys
tmp
usr
var
[host] container pid 609 exited, status 0
ls: cannot access '/var/l8/alpine/i_was_here': No such file or directory
```

출력에서 볼 것:

- Ubuntu 게스트 안에서 **Alpine 3.24.2 셸이 PID 1, hostname `l8c`** 로 돈다. 커널은 그대로 6.18.55-sec다 — "다른 배포판을 돌리는데 커널은 하나"가 컨테이너다.
- `i_was_here`는 컨테이너 안 `/`에는 생겼지만 lower(`/var/l8/alpine`)에는 없다. tmpfs upper에만 쓰였고, 컨테이너가 끝나면 tmpfs와 함께 사라진다. "컨테이너를 재시작하면 쓴 파일이 사라진다"의 원리이자, 그래서 데이터는 volume(bind mount)으로 빼야 하는 이유다.
- **빠진 것들이 보인다**: `/sys`를 마운트하지 않아 `/sys/class/net`이 없다(real 런타임은 `sysfs`를 읽기전용으로 마운트한다). cgroup namespace를 안 만들어서 `/proc/self/cgroup`이 호스트 경로 `/l8mini`를 그대로 보여 준다(Podman 컨테이너는 cgroup namespace를 따로 받는다 — 예제 10의 `lsns`). `/dev`도 Alpine 디렉터리의 빈 껍데기다. capability를 하나도 안 떨궜고, seccomp도 없고, user namespace도 없다 — **이 컨테이너 안 root는 호스트 root 그대로다.** 런타임이 해 주는 일의 절반은 이런 "빠진 것 채우기와 권한 깎기"다.

### 5.3 흔한 함정

- `MS_PRIVATE` 없이 `pivot_root`하면 `EINVAL`이 난다. systemd가 `/`를 shared로 마운트해 두기 때문이다(마운트 전파). 또 private로 안 바꾸면 컨테이너 안의 마운트가 호스트로 새어 나간다.
- `chroot`로 대신하면 안 되나? chroot는 프로세스의 루트 포인터만 바꾸고 옛 루트가 마운트 트리에 남아 있어, 권한이 있으면 `..`로 탈출할 수 있다. `pivot_root` + 옛 루트 `umount`가 정석이다.
- overlay의 upper·work는 **같은 파일시스템**에 있어야 하고, lower로 overlay를 또 쓸 수는 있지만(중첩) upper로는 못 쓴다. rootless에서 overlay를 못 쓰던 옛 커널에서는 `fuse-overlayfs`를 썼다(이 이미지에도 설치돼 있다).

---

## 6. OCI 표준과 런타임 스택 — 누가 무엇을 하나

§5의 50줄 C 프로그램이 "컨테이너 만들기"의 전부라면, Docker·Podman·containerd는 왜 이렇게 많을까? 실제 제품에는 이미지 받기·검증·층 관리·네트워크·로그·재시작·API가 필요하고, 그걸 층으로 나눠 놓은 것이 지금의 생태계다. 층을 나누는 기준이 **OCI**(Open Container Initiative) 표준이다.

### 6.1 OCI 표준 세 가지

| 표준 | 정하는 것 | 장비에서 만나는 모양 |
|---|---|---|
| image-spec | 이미지 = manifest(JSON) + config(JSON: CMD, ENV, arch) + layers(tar, gzip 또는 zstd 압축). 여러 아키텍처는 image index로 묶는다 | `podman image inspect`, `podman save`의 tar 내용, `linux/arm64/v8` 플랫폼 태그 |
| runtime-spec | "bundle" = `config.json` + `rootfs/` 디렉터리. `config.json`에 namespace 목록, cgroup 경로·한도, mount, capability, seccomp, device 허용 목록, hook | runc·crun이 읽는 파일. 예제 10에서 실제 파일을 본다 |
| distribution-spec | registry HTTP API (`/v2/<name>/manifests/<tag>`, `/v2/<name>/blobs/<digest>`) | 사설 registry, 미러, 오프라인 registry |

핵심은 **내용 주소(content addressing)** 다. 층과 config는 모두 `sha256:` 다이제스트로 이름 붙는다. 같은 층은 몇 개의 이미지가 써도 디스크에 한 번만 저장되고, 받는 쪽이 다이제스트를 다시 계산해 변조를 잡는다. SSD 펌웨어 이미지의 섹션별 해시와 같은 발상이다.

### 6.2 런타임 스택 그림

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="14" width="190" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="115" y="35" text-anchor="middle" font-size="13">podman CLI · Quadlet</text>
  <rect x="245" y="14" width="190" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="35" text-anchor="middle" font-size="13">docker CLI</text>
  <rect x="470" y="14" width="190" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="565" y="35" text-anchor="middle" font-size="13">k3s · kubelet</text>
  <rect x="20" y="60" width="190" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="115" y="81" text-anchor="middle" font-size="12">libpod (데몬 없음)</text>
  <rect x="245" y="60" width="190" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="81" text-anchor="middle" font-size="12">dockerd (상주 데몬)</text>
  <rect x="470" y="60" width="190" height="32" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="565" y="81" text-anchor="middle" font-size="12">CRI 플러그인</text>
  <rect x="20" y="106" width="190" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="115" y="127" text-anchor="middle" font-size="12">netavark · aardvark-dns</text>
  <rect x="245" y="106" width="415" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="452" y="127" text-anchor="middle" font-size="12">containerd (상주 데몬 — 이미지 · snapshot · 수명 관리)</text>
  <rect x="20" y="152" width="190" height="32" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="115" y="173" text-anchor="middle" font-size="12">conmon (컨테이너당 1개)</text>
  <rect x="245" y="152" width="415" height="32" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="452" y="173" text-anchor="middle" font-size="12">containerd-shim-runc-v2 (컨테이너당 1개)</text>
  <line x1="115" y1="184" x2="115" y2="204" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="452" y1="184" x2="452" y2="204" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="20" y="206" width="640" height="34" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="228" text-anchor="middle" font-size="13" font-weight="bold">OCI runtime — runc (Go) · crun (C, 작고 빠름) · youki (Rust)</text>
  <rect x="20" y="250" width="640" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="272" text-anchor="middle" font-size="12">bundle(config.json + rootfs) → clone3 · pivot_root · cgroup 쓰기 · seccomp → execve</text>
  <rect x="20" y="296" width="640" height="40" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="314" text-anchor="middle" font-size="13" font-weight="bold">Linux 커널</text>
  <text x="340" y="330" text-anchor="middle" font-size="12">namespaces · cgroup v2 · overlayfs · capabilities · seccomp · LSM</text>
</svg>
```
그림 7 — 세 갈래 도구가 결국 같은 OCI runtime(runc/crun)을 거쳐 같은 커널 기능을 쓴다. Podman은 상주 데몬이 없고, Docker·k3s는 containerd 데몬을 둔다

**OCI runtime(runc/crun)은 "컨테이너 하나 만들고 빠지는" 짧은 프로그램이다.** `config.json`을 읽고 §5에서 손으로 한 일(clone, mount, pivot_root, cgroup 쓰기, capability 떨구기, seccomp 적재)을 한 뒤 `execve`로 앱이 된다. 그 뒤 컨테이너를 지켜보는 것은 **conmon**(Podman) 또는 **shim**(containerd)이다 — stdout/stderr를 로그로 모으고, 종료 코드를 받아 두고, 런타임 데몬이 재시작돼도 컨테이너가 죽지 않게 해 준다.

### 6.3 런타임·엔진 비교

| 도구 | 상주 데몬 | 크기·의존성 | Embedded에서의 위치 |
|---|---|---|---|
| runc | 없음(1회 실행) | Go 정적 바이너리 약 10 MB | Docker·containerd 기본 런타임 |
| crun | 없음(1회 실행) | C, 이 게스트의 `/usr/bin/crun` 469 KB + libseccomp·libyajl | Podman 기본(Fedora·Ubuntu 24.04 포함). 시작이 빠르고 메모리 작음 |
| containerd | 있음 | Go, 수십 MB | Docker·k3s의 아랫단. 단독으로 `ctr`/`nerdctl`로도 쓴다 |
| Docker Engine (Moby) | dockerd + containerd | 가장 무겁다 | 개발 PC 표준. Jetson L4T 이미지에 기본 설치되는 경우가 많다 |
| balena-engine | 있음(Moby 포크) | Docker보다 작게 다듬음, delta pull, 저장 공간 절약 | balenaOS 기기 |
| Podman | **없음** | Go CLI(이 게스트 35 MB) + conmon(201 KB) + crun + netavark(5.3 MB) | systemd와 궁합(Quadlet), rootless. RHEL·Fedora IoT·Ubuntu에서 쓰기 쉽다 |
| systemd-nspawn | 없음(systemd-machined 선택) | systemd에 포함 | OS 트리를 통째로 띄우는 "가벼운 OS 컨테이너". 이미지 생태계는 없다 |
| LXC / Incus | 선택 | liblxc | "시스템 컨테이너"(안에서 init이 돈다). 산업용 게이트웨이에서 가끔 |
| k3s | 있음 | 약 70 MB 단일 바이너리 + containerd | 장비 군단(fleet)을 Kubernetes로 관리하고 싶을 때. 메모리 수백 MB 각오 |

크기 숫자 중 crun·conmon·podman·netavark는 이 게스트 이미지(Ubuntu 24.04 arm64 패키지)에서 잰 실제 파일 크기이고, 나머지는 기억 기반의 대략값이다. 예제 14·15에서 이 게스트의 실제 시작 시간과 메모리를 잰다.

### 6.4 흔한 함정

- "Docker를 지우고 Podman을 쓰면 이미지를 다시 만들어야 한다" — 아니다. 둘 다 OCI 이미지를 쓴다. 바뀌는 것은 데몬 유무, 소켓 API, 네트워크 도구, systemd 통합 방식이다.
- runc와 crun을 섞어 쓸 때 cgroup 드라이버(systemd vs cgroupfs)가 엔진마다 다르면, 같은 장비에서 두 엔진이 서로의 cgroup을 이상하게 본다. 한 장비에는 엔진 하나만 두는 것이 원칙이다.
- "containerd만 깔면 docker 명령이 된다" — 아니다. `ctr`은 디버그용 저수준 도구이고, 사람이 쓰는 CLI는 `nerdctl`이다.

---

## 7. Podman을 장비 위에서 — 오프라인 import부터 cgroup 매핑, rootless까지

### 7.1 왜 Podman인가 (장비 관점)

- **상주 데몬이 없다.** `podman run`은 컨테이너를 만들고 conmon에게 넘긴 뒤 끝난다. 장비에서 "dockerd가 죽어서 모든 컨테이너가 같이 죽었다", "dockerd가 RAM 100 MB를 먹는다" 같은 문제가 없다.
- **systemd와 직접 맞물린다.** Quadlet(§9)으로 컨테이너를 보통 unit처럼 다룬다. 재시작·의존성·로그·watchdog을 systemd가 맡는다(L1).
- **Docker CLI와 거의 같다.** 같은 OCI 이미지, 같은 Containerfile(Dockerfile) 문법.

Jetson L4T처럼 Docker가 기본으로 깔려 오는 BSP도 많다. 원리는 같으니, 여기서 본 cgroup·namespace·장치 매핑은 Docker에도 그대로 적용된다.

### 7.2 예제 9 — arm64 Alpine 이미지를 오프라인으로 만들고 실행하기

무엇을 확인하나: registry 없이 tarball 하나로 이미지를 만든다. 장비 공장 라인·폐쇄망에서 쓰는 경로다. tarball은 VM 호스트에서 내려받아 Alpine 서버의 sha256 값과 대조했다.

```sh
# VM 호스트 (/var/tmp/L8/dl)
B=https://dl-cdn.alpinelinux.org/alpine/v3.24/releases/aarch64
curl -sfLO $B/alpine-minirootfs-3.24.2-aarch64.tar.gz
curl -sfLO $B/alpine-minirootfs-3.24.2-aarch64.tar.gz.sha256
sha256sum -c alpine-minirootfs-3.24.2-aarch64.tar.gz.sha256
```

```text
alpine-minirootfs-3.24.2-aarch64.tar.gz: OK
```

`latest-releases.yaml`에 적힌 sha256(`9bf70a7f…773`)과도 같았다. 둘 다 같은 서버에서 온 값이라 "전송 중 손상"은 잡지만 "서버가 오염된 경우"는 못 잡는다 — 제품이라면 GPG 서명(`.asc`)이나 사내 미러의 서명으로 검증한다.

```sh
# 게스트
podman --version; crun --version | head -1; conmon --version | head -1; /usr/lib/podman/netavark --version
podman info --format '{{.Host.CgroupsVersion}} {{.Host.CgroupManager}} {{.Store.GraphDriverName}} {{.Host.OCIRuntime.Name}} {{.Host.NetworkBackend}}'
podman import --change 'CMD ["/bin/sh"]' /mnt/w/alpine-minirootfs-3.24.2-aarch64.tar.gz localhost/l8/alpine:3.24.2
podman images
podman run --rm --network none localhost/l8/alpine:3.24.2 sh -c 'cat /etc/alpine-release; id; hostname; ps'
```

```text
podman version 4.9.3
crun version 1.14.1
conmon version 2.1.10
netavark 1.4.0
v2 systemd overlay crun netavark
Getting image source signatures
Copying blob sha256:9bf70a7f18ea44094cbb5f70c58f9af129c8214745743db0e68e5502cc2ce773
Copying config sha256:58897d826f9094d2d11f6ee3fa011175cdeec613f3ff792f9d97cdf7e4f8b142
Writing manifest to image destination
sha256:58897d826f9094d2d11f6ee3fa011175cdeec613f3ff792f9d97cdf7e4f8b142
REPOSITORY           TAG         IMAGE ID      CREATED         SIZE
localhost/l8/alpine  3.24.2      58897d826f90  11 minutes ago  8.95 MB
3.24.2
uid=0(root) gid=0(root) groups=0(root),1(bin),2(daemon),3(sys),4(adm),6(disk),10(wheel),11(floppy),20(dialout),26(tape),27(video)
ebda535768bc
PID   USER     TIME  COMMAND
    1 root      0:00 ps
```

출력에서 볼 것:

- Ubuntu 24.04의 스택: Podman 4.9.3 + crun 1.14.1 + conmon 2.1.10 + netavark 1.4.0, cgroup v2 + systemd cgroup manager + overlay 저장 드라이버.
- `Copying blob sha256:9bf70a7f…` — **층의 다이제스트가 tarball의 sha256과 같다.** 압축된 tar를 그대로 층으로 쓴 것이다. 내용 주소 방식이 눈에 보인다.
- "CREATED 11 minutes ago"는 tarball 파일의 시각 기준이다(게스트 시계와 차이). hostname은 컨테이너 ID 앞 12자리다.
- `ps`의 PID 1이 `ps` 자신이다 — `sh -c`가 마지막 명령을 `exec`로 바꿔 실행했기 때문이다.

### 7.3 네트워크 — netavark와 "MASQUERADE가 없다"

```sh
podman run --rm localhost/l8/alpine:3.24.2 ip addr show eth0; echo "bridge exit=$?"
nft list tables
zcat /proc/config.gz | grep -E "XT_TARGET_MASQ|NFT_COMPAT|IP_NF_NAT"
```

```text
Error: netavark: unable to append rule '! -d 224.0.0.0/4 -j MASQUERADE' to table 'nat': code: 4, msg: Warning: Extension MASQUERADE revision 0 not supported, missing kernel module?
iptables v1.8.10 (nf_tables):  RULE_APPEND failed (No such file or directory): rule in chain NETAVARK-1D8721804F16F

bridge exit=127
table ip nat
table ip filter
CONFIG_NFT_COMPAT=y
# CONFIG_NETFILTER_XT_TARGET_MASQUERADE is not set
```

출력에서 볼 것: 기본(bridge) 네트워크가 **실패한다.** netavark 1.4는 방화벽 규칙을 `iptables` 명령으로 쓰고, Ubuntu의 `iptables`는 nf_tables 백엔드(`iptables-nft`)다. 이 백엔드는 `-j MASQUERADE` 같은 xtables 타깃을 `nft_compat`으로 커널의 xt 모듈에 넘기는데, 랩 커널에는 nftables 네이티브 masquerade(`NFT_MASQ`)만 있고 xt 타깃(`NETFILTER_XT_TARGET_MASQUERADE`)이 없다. 브리지(`podman0`)와 nat 테이블까지는 만들어졌고, 마지막 규칙에서 멈췄다.

이게 장비에서 실제로 자주 만나는 함정이다. **"컨테이너 엔진이 요구하는 커널 옵션"은 엔진·버전마다 다르다.** 해결책은 셋 중 하나다: (1) 커널에 xt 타깃(또는 레거시 `IP_NF_NAT`)을 켠다, (2) nftables를 직접 쓰는 방화벽 드라이버를 가진 netavark로 올린다(기억 기준 netavark 1.12부터 `firewall_driver = "nftables"` 지원 — 버전 확인 필요), (3) NAT가 필요 없는 네트워크를 쓴다. Docker·Podman 저장소에는 커널 설정 검사 스크립트(moby의 `check-config.sh`)가 있으니 BSP 커널을 만들 때 돌려 본다.

(3)을 확인해 보자. `--internal` 네트워크는 외부로 나가는 NAT 규칙을 만들지 않는다. 장비 안 컨테이너끼리의 통신(인식 → 플래너)에는 이것으로 충분한 경우가 많다.

```sh
podman network create --internal l8int
podman run -d --name l8srv --network l8int localhost/l8/alpine:3.24.2 sleep 120 >/dev/null
podman run --rm --network l8int localhost/l8/alpine:3.24.2 sh -c 'ip -o -4 addr show eth0; ping -c 2 -W 3 l8srv'
ip -br link | grep -E "podman|veth" | cut -c1-60
podman run --rm --network host localhost/l8/alpine:3.24.2 ip -o addr | head -3
```

```text
2: eth0    inet 10.89.0.3/24 brd 10.89.0.255 scope global eth0\       valid_lft forever preferred_lft forever
PING l8srv (10.89.0.2): 56 data bytes
64 bytes from 10.89.0.2: seq=0 ttl=42 time=10.210 ms
64 bytes from 10.89.0.2: seq=1 ttl=42 time=0.980 ms

--- l8srv ping statistics ---
2 packets transmitted, 2 packets received, 0% packet loss
round-trip min/avg/max = 0.980/5.595/10.210 ms
podman1          UP             d6:7d:65:49:83:a3 <BROADCAST
veth0@if2        UP             16:02:f4:77:a9:1b <BROADCAST
1: lo    inet 127.0.0.1/8 scope host lo\       valid_lft forever preferred_lft forever
1: lo    inet6 ::1/128 scope host \       valid_lft forever preferred_lft forever
2: enp0s1    inet 10.0.2.15/24 brd 10.0.2.255 scope global dynamic enp0s1\       valid_lft 86220sec preferred_lft 86220sec
```

출력에서 볼 것: 내부 네트워크에서는 브리지 `podman1` + 컨테이너마다 veth 한 쌍(예제 2와 같은 구조)이 생기고, aardvark-dns 덕분에 **컨테이너 이름(`l8srv`)으로** ping이 된다. `--network host`는 net namespace를 만들지 않아 호스트의 `enp0s1`이 그대로 보인다 — 가장 빠르고 단순하지만 격리가 없다. ROS 2/DDS처럼 멀티캐스트 탐색을 쓰는 로봇 스택은 실무에서 `--network host`를 많이 쓴다(그 대가는 §12).

### 7.4 예제 10 — `--memory/--cpus/--pids-limit`은 어느 파일이 되나

무엇을 확인하나: Podman 옵션이 OCI `config.json`을 거쳐 cgroup 파일의 어떤 값이 되는지, 컨테이너가 어떤 namespace·capability·seccomp·LSM을 받는지.

```sh
podman run -d --name l8lim --network none --memory 64m --cpus 0.5 --pids-limit 30 localhost/l8/alpine:3.24.2 sleep 600
P=$(podman inspect -f '{{.State.Pid}}' l8lim); cat /proc/$P/cgroup
CG=/sys/fs/cgroup$(cut -d: -f3 /proc/$P/cgroup)
for f in memory.max memory.swap.max cpu.max pids.max cpu.weight; do echo "$f = $(cat $CG/$f)"; done
lsns -p $P -o NS,TYPE,PID,COMMAND
grep -E "^(CapEff|CapBnd|Seccomp|NoNewPrivs)" /proc/$P/status; grep -E "^(CapEff|Seccomp)" /proc/self/status
cat /sys/kernel/security/lsm; echo; cat /proc/$P/attr/current; echo
capsh --decode=$(awk '/^CapEff/{print $2}' /proc/$P/status)
ps -o pid,ppid,rss,comm -p $(pgrep -d, conmon),$P
```

```text
0::/machine.slice/libpod-29e322ca149b0ce5e81a973aa3965d3e082de91283bb5552fb4b3636277b4359.scope/container
memory.max = 67108864
memory.swap.max = 67108864
cpu.max = 50000 100000
pids.max = 30
cpu.weight = 100
        NS TYPE     PID COMMAND
4026531834 time       1 /sbin/init
4026531837 user       1 /sbin/init
4026532269 mnt      703 sleep 600
4026532270 uts      703 sleep 600
4026532271 ipc      703 sleep 600
4026532272 pid      703 sleep 600
4026532273 net      703 sleep 600
4026532324 cgroup   703 sleep 600
CapEff:	00000000800405fb
CapBnd:	00000000800405fb
NoNewPrivs:	0
Seccomp:	2
Seccomp_filters:	1
CapEff:	000001ffffffffff
Seccomp:	0
Seccomp_filters:	0
capability,landlock,lockdown,yama,apparmor,bpf,ima,evm
containers-default-0.57.4-apparmor1 (enforce)

0x00000000800405fb=cap_chown,cap_dac_override,cap_fowner,cap_fsetid,cap_kill,cap_setgid,cap_setuid,cap_setpcap,cap_net_bind_service,cap_sys_chroot,cap_setfcap
    PID    PPID   RSS COMMAND
    701       1  2164 conmon
    703     701   880 sleep
```

같은 컨테이너를 다시 띄워(ID는 다르다) OCI bundle의 `config.json`을 직접 읽었다:

```sh
B=$(find /var/lib/containers /run/containers -name config.json -path "*$CID*" | head -1)
jq -c '{ociVersion, args: .process.args, caps: (.process.capabilities.effective|length), seccomp_default: .linux.seccomp.defaultAction, seccomp_rules: (.linux.seccomp.syscalls|length), namespaces: [.linux.namespaces[].type], resources: .linux.resources, cgroupsPath: .linux.cgroupsPath}' $B
jq -c '(.linux.maskedPaths|length), .linux.readonlyPaths[:3], .root' $B
cat /sys/fs/cgroup/machine.slice/libpod-$CID.scope/container/memory.current
```

```text
{"ociVersion":"1.1.0","args":["sleep","600"],"caps":11,"seccomp_default":"SCMP_ACT_ERRNO","seccomp_rules":21,"namespaces":["pid","network","ipc","uts","mount","cgroup"],"resources":{"devices":[{"allow":false,"access":"rwm"}],"memory":{"limit":67108864,"swap":134217728},"cpu":{"quota":50000,"period":100000},"pids":{"limit":30}},"cgroupsPath":"machine.slice:libpod:a664cccd3014baf2f90dbb01b11d477f1d42f454dec384f9f0d85c424ce48cc7"}
12
["/proc/asound","/proc/bus","/proc/fs"]
{"path":"/var/lib/containers/storage/overlay/ea6f4a85b174347b8911efdc4d32491a1c40eb5808b0a73d86fa315a15093bdb/merged"}
1040384
```

| Podman 옵션 | `config.json` | cgroup v2 파일 | 값 |
|---|---|---|---|
| `--memory 64m` | `memory.limit` = 67108864 | `memory.max` | 67108864 |
| (자동) | `memory.swap` = 134217728 (memory + swap 합) | `memory.swap.max` | 67108864 (= 합 − memory) |
| `--cpus 0.5` | `cpu.quota` 50000 / `cpu.period` 100000 | `cpu.max` | `50000 100000` |
| `--pids-limit 30` | `pids.limit` = 30 | `pids.max` | 30 |
| (기본) | `cpu.shares` 없음 | `cpu.weight` | 100 |
| (기본) | `devices: [{allow:false, access:rwm}]` + 기본 허용 목록 | BPF device 프로그램 | §11 |
| `cgroupsPath` `machine.slice:libpod:<id>` | systemd 드라이버 형식 "slice:prefix:name" | `machine.slice/libpod-<id>.scope/container` | |

출력에서 볼 것:

- `--cpus 0.5`는 **`cpu.max`(throttling)** 다. `cpu.weight`가 아니다. §4의 결론에 따르면 제어 루프 컨테이너에 `--cpus`를 거는 것은 위험하다.
- swap 값이 두 번 나온다. OCI의 `memory.swap`은 Docker v1 시절 의미(메모리 + swap 합계)를 따르고, crun이 v2의 `memory.swap.max`(swap만)로 바꿔 쓴다. swap이 없는 장비라 실제 효과는 없지만, v1·v2 의미 차이 때문에 생기는 혼란의 대표 예다.
- namespace는 6개(pid, net, ipc, uts, mnt, **cgroup**)를 새로 만들었고 **user와 time은 호스트와 공유**한다 — rootful 기본값. 컨테이너 안 root는 호스트 UID 0이다(§12).
- capability가 41개 전체(`000001ffffffffff`)에서 11개(`800405fb`)로 줄었다. `CAP_SYS_ADMIN`, `CAP_NET_ADMIN`, `CAP_SYS_MODULE`, `CAP_MKNOD`, `CAP_NET_RAW`는 없다.
- seccomp: 기본 동작 `SCMP_ACT_ERRNO`(목록에 없는 syscall은 에러) + 허용 규칙 21묶음. AppArmor `containers-default-0.57.4` 프로파일이 enforce로 붙었다. `/proc` 아래 12개 경로는 가려지고(masked) 일부는 읽기전용이다. 세부는 L6.
- rootfs는 `/var/lib/containers/storage/overlay/<id>/merged` — §5의 overlay merged 디렉터리가 이것이다. `config.json`은 영구 저장소(`userdata/`)와 crun의 상태 디렉터리(`/run/crun/<id>/`) 두 곳에 있었다.
- 자원: 컨테이너 cgroup의 `memory.current`는 약 1 MB, conmon이 RSS 약 2.1 MB. 예제 15에서 5개를 띄워 다시 잰다.

### 7.5 예제 11 — rootless: user namespace와 `uid_map`

무엇을 확인하나: 일반 사용자 `l8u`가 띄운 컨테이너 안의 root가 호스트에서 누구인지, rootful과 무엇이 다른지.

```sh
useradd -m -s /bin/bash l8u; grep l8u /etc/subuid /etc/subgid
loginctl enable-linger l8u
su - l8u -c "podman load -q -i /var/l8/app_v2.tar; podman unshare cat /proc/self/uid_map; podman run --rm --network none localhost/l8/app:v2 sh -c 'id; cat /proc/self/uid_map'"
# 별도 실행: l8u가 띄운 컨테이너 l8r(sleep 300)을 호스트 쪽에서 보기 — 아래 출력의 USER 줄부터
ps -o user,pid,comm -p $P; cat /proc/$P/uid_map; grep CapEff /proc/$P/status
podman run --rm --network none localhost/l8/alpine:3.24.2 cat /proc/self/uid_map      # rootful 비교
```

```text
/etc/subuid:l8u:100000:65536
/etc/subgid:l8u:100000:65536
Loaded image: localhost/l8/app:v2
         0       1000          1
         1     100000      65536
uid=0(root) gid=0(root) groups=0(root),1(bin),2(daemon),3(sys),4(adm),6(disk),10(wheel),11(floppy),20(dialout),26(tape),27(video)
         0       1000          1
         1     100000      65536
USER         PID COMMAND
l8u         1289 sleep
         0       1000          1
         1     100000      65536
CapEff:	00000000800405fb
         0          0 4294967295
```

(`su - l8u -c` 명령 앞에는 `export XDG_RUNTIME_DIR=/run/user/1000`이 붙어 있다 — 노트에서는 생략.)

출력에서 볼 것:

- `useradd`가 `/etc/subuid`에 `100000:65536`을 자동으로 잡았다. `uid_map`의 의미는 "안의 UID 0 → 밖의 1000(l8u) 1개", "안의 1~65536 → 밖의 100000~165535".
- 컨테이너 안에서는 `uid=0(root)`인데 호스트의 `ps`에서는 **`l8u`** 다. capability 비트도 rootful과 같은 11개지만, 그 capability는 **컨테이너의 user namespace 안에서만** 유효하다. 호스트 파일에 대해서는 l8u의 권한뿐이다.
- rootful은 `0 0 4294967295` — 항등 매핑, 즉 user namespace가 없다.
- 위 출력에서 경고 줄(`time=... level=warning`)은 생략했다. 실제로는 매 명령마다 이런 경고가 나왔다:

```text
time="2026-10-18T04:42:59Z" level=warning msg="The cgroupv2 manager is set to systemd but there is no systemd user session available"
time="2026-10-18T04:42:59Z" level=warning msg="Falling back to --cgroup-manager=cgroupfs"
```

그리고 그 결과가 중요하다:

```sh
su - l8u -c "podman info --format '{{.Host.CgroupManager}} {{.Store.GraphDriverName}} rootless={{.Host.Security.Rootless}} net={{.Host.NetworkBackend}}'; podman run -d --name l8r --network none --memory 32m localhost/l8/app:v2 sleep 300; podman exec l8r cat /sys/fs/cgroup/memory.max"
cat /proc/$P/cgroup
ls /usr/lib/systemd/user/ | grep -i dbus || echo "no user dbus unit (dbus-user-session not installed)"
```

```text
cgroupfs overlay rootless=true net=netavark
max
0::/user.slice/user-0.slice/session-2.scope
no user dbus unit (dbus-user-session not installed)
```

**`--memory 32m`을 줬는데 컨테이너의 `memory.max`는 `max`다 — 한도가 조용히 무시됐다.** rootless 컨테이너가 cgroup 한도를 걸려면 systemd 사용자 세션(사용자용 D-Bus)을 통해 **위임(delegation)된 cgroup**을 받아야 한다. 이 이미지에는 사용자 D-Bus(`dbus-user-session` 패키지)가 없어 Podman이 cgroupfs로 물러났고, 컨테이너는 root의 로그인 세션 scope(`session-2.scope`)에 그대로 남았다. 장비에서 rootless를 쓰려면 이미지에 `dbus-user-session`을 넣고, linger로 사용자 매니저를 띄우고, 서비스를 `systemctl --user`(또는 사용자 Quadlet)로 돌려야 한다. **자원 한도가 실제로 걸렸는지는 항상 cgroup 파일로 확인한다.**

---

## 8. 이미지 배포와 저장 비용 — 층, 공유, 오프라인 load

### 8.1 예제 12 — 층 공유와 save/load

무엇을 확인하나: 같은 베이스 위에 "모델 파일(5 MiB)" 층과 "버전 파일" 층을 얹은 이미지 v1·v2를 만들고, 디스크에 실제로 무엇이 몇 번 저장되는지, `save`/`load`로 registry 없이 옮길 수 있는지.

```sh
mkdir -p /var/l8/ctx && cd /var/l8/ctx
printf 'FROM localhost/l8/alpine:3.24.2\nRUN dd if=/dev/urandom of=/opt/model.bin bs=1M count=5 2>/dev/null\nRUN echo v1 > /opt/app.version\n' > Containerfile
podman build --network none -q -t localhost/l8/app:v1 .
sed -i 's/v1 >/v2 >/' Containerfile
podman build --network none -q -t localhost/l8/app:v2 .
podman history localhost/l8/app:v2 --format '{{.ID}} {{.Size}} {{.CreatedBy}}' | cut -c1-110
podman images --format '{{.Repository}}:{{.Tag}} {{.Size}}'
ls /var/lib/containers/storage/overlay | grep -v '^l$' | wc -l
du -sh /var/lib/containers/storage/overlay
podman save -o /var/l8/app_v2.tar localhost/l8/app:v2; ls -l /var/l8/app_v2.tar
podman rmi localhost/l8/app:v2; podman load -q -i /var/l8/app_v2.tar
podman image exists localhost/l8/app:v2 && echo "loaded offline"
```

```text
4bb49880cf856a40569e65a22ec08bae7181e6af0d23b14c07d1fd224a7390bb
6c1a4eb90bd4439fde21c5b84b57489f29d7fb5ed0de5c392a9bd7838b3a2dde
6c1a4eb90bd4 3.07kB /bin/sh -c echo v2 > /opt/app.version
998048b3934a 5.25MB /bin/sh -c dd if=/dev/urandom of=/opt/mode...
58897d826f90 8.95MB /bin/sh -c #(nop) ADD file:1886356f5ecb944...
localhost/l8/app:v2 14.2 MB
localhost/l8/app:v1 14.2 MB
localhost/l8/alpine:3.24.2 8.95 MB
4
15M	/var/lib/containers/storage/overlay
Copying blob sha256:1886356f5ecb94414310225351b1ea4917218dc323d689c2921f28737bd09a28
Copying blob sha256:6f1bdf88de114c732162be3fa9319b38f6c56dcbaea58c8f59425b114ba55929
Copying blob sha256:9b422cc8adfb12980fe24c265a87564247c97079a5e9082843794587f12cc78a
Copying config sha256:6c1a4eb90bd4439fde21c5b84b57489f29d7fb5ed0de5c392a9bd7838b3a2dde
Writing manifest to image destination
-rw-r--r-- 1 root root 14212096 Oct 18 04:42 /var/l8/app_v2.tar
Untagged: localhost/l8/app:v2
Deleted: 6c1a4eb90bd4439fde21c5b84b57489f29d7fb5ed0de5c392a9bd7838b3a2dde
Loaded image: localhost/l8/app:v2
loaded offline
```

출력에서 볼 것:

- 이미지 세 개의 "SIZE" 합은 8.95 + 14.2 + 14.2 = 37 MB인데, 디스크의 층 디렉터리는 **4개, 15 MB**다. base(8.95 MB)와 model(5.25 MB) 층은 세 이미지가 공유하고, v1·v2는 3 kB짜리 마지막 층만 다르다. v2의 `dd` 층은 v1 빌드의 캐시를 재사용했다 — 같은 명령, 같은 부모 층이라서다(내용이 랜덤이어도 캐시 키는 명령이다).
- `podman save`의 tar(14.2 MB)는 층 3개 + config + manifest다. 이걸 지우고 `load`하면 registry 없이 이미지가 돌아온다. 장비 쪽에 이미 base·model 층이 있다면 진짜로 보내야 하는 것은 3 kB 층 하나뿐이다 — 그래서 registry pull(층 단위 비교)이 tar 통째보다 효율적이고, tar로 보낼 때도 "바뀐 층만 담은 tar"를 만드는 도구(skopeo로 OCI layout을 다루는 방식 등)를 쓴다.
- `podman images`의 "SIZE"는 **압축 해제 후 크기**다. registry로 받을 때는 압축된 층(gzip·zstd) 크기만큼 네트워크를 쓰고, 디스크에는 풀린 크기만큼 쓴다.

예제 9·10에서 본 저장소 크기: 이미지 하나일 때 `/var/lib/containers/storage` 9.2 MB, 층 디렉터리 하나 8.9 MB — 엔진 메타데이터 오버헤드는 수백 kB 수준이었다. rootless 사용자 `l8u`의 저장소는 별도(`~/.local/share/containers/storage`, 15 MB)다. **rootful과 rootless는 층을 공유하지 않는다** — 같은 이미지를 두 번 저장하게 되니 장비에서는 한쪽으로 통일한다.
### 8.2 flash와 셀룰러 링크 위에서의 이미지 배포 — 실무 규칙

예제 12의 숫자를 장비 규모로 옮기면 이렇다. Jetson용 CUDA·TensorRT 베이스 이미지는 수 GB(압축 상태로도 1 GB 이상)인 경우가 흔하다. 이걸 LTE로 매주 받으면 데이터 요금과 시간이 감당이 안 된다. 그래서:

- **베이스 층은 고정하고 자주 바뀌는 것을 위층에 둔다.** Containerfile 순서를 "OS 패키지 → 큰 런타임(CUDA·TensorRT) → 파이썬 의존성 → 모델 → 앱 코드" 로 둔다. 앱 코드 한 줄을 고쳐도 마지막 층 몇 MB만 다시 받는다. 예제 12에서 v1 → v2가 공유 층을 다시 받지 않는 것이 그 원리다.
- **태그가 아니라 다이제스트로 고정한다.** `image@sha256:...`로 배포하면 "같은 태그인데 장비마다 내용이 다른" 사고가 없다. OTA 매니페스트에 다이제스트를 싣는다.
- **오프라인·현장 경로를 준비한다.** 공장 라인·군 시설·지하 현장처럼 registry에 못 닿는 곳에서는 `podman save`/`docker save`로 만든 tar를 USB나 OTA 번들(RAUC 번들의 추가 이미지 등, L4)로 옮겨 `load`한다. 예제 12에서 실제로 save → rmi → load가 오프라인으로 되는 것을 확인했다.
- **registry 미러·캐시**: 차량 기지·창고에 로컬 registry(pull-through cache)를 두면 장비 군단이 같은 층을 한 번만 WAN으로 받는다. Podman은 `/etc/containers/registries.conf`의 `[[registry.mirror]]`로 미러를 지정한다.
- **압축 방식**: OCI 층은 gzip이 기본이고 zstd도 표준이다. zstd는 풀기가 빨라 작은 CPU에 유리하다. 장비 쪽 엔진 버전이 zstd 층을 지원하는지 확인한다.
- **저장 공간 회계**: 이미지 층(압축 해제 상태) + 컨테이너 쓰기층(upper) + 로그(conmon이 쓰는 컨테이너 로그는 크기 제한을 걸어야 한다: `--log-opt max-size=`) + 롤백용 직전 이미지. data 파티션 크기를 이 합의 2배 이상으로 잡는 것이 안전하다.
- **flash 수명**: 층을 받을 때 압축 해제한 내용을 그대로 쓴다. 매일 수백 MB를 쓰면 eMMC의 TBW를 계산해 봐야 한다(J1). 업데이트 주기와 층 크기를 같이 설계한다.
- **서명 검증**: `/etc/containers/policy.json`으로 "이 registry의 이미지는 이 키로 서명된 것만 허용"을 강제할 수 있다(sigstore·GPG). 장비가 공격자 registry에서 이미지를 받지 않게 하는 마지막 방어선이다(L5·L6).

---

## 9. systemd 통합 — Quadlet으로 "부팅 시 시작 · 죽으면 재시작"

### 9.1 직관

장비의 서비스 관리자는 systemd다(L1). 컨테이너도 결국 서비스이니 systemd가 관리하게 하는 것이 자연스럽다. 예전에는 `podman generate systemd`로 unit 파일을 뽑았지만, Podman 4.4부터는 **Quadlet**이 표준이다: `/etc/containers/systemd/<이름>.container` 파일을 두면 부팅 때 systemd generator(`podman-system-generator`)가 그것을 `<이름>.service`로 변환한다.

### 9.2 예제 13 — Quadlet unit

무엇을 확인하나: 2초마다 tick을 찍다가 4번째에 exit 3으로 "죽는" 앱을 Quadlet으로 정의하고, 생성된 unit, 부팅 시 시작, 실패 시 재시작, healthcheck, cgroup 배치를 확인한다.

```text
[Unit]
Description=L8 demo sensor app (Quadlet)
After=local-fs.target

[Container]
Image=localhost/l8/alpine:3.24.2
ContainerName=l8app
Network=none
Exec=/bin/sh -c 'touch /tmp/healthy; i=0; while :; do i=$((i+1)); echo "tick $i"; [ $i -ge 4 ] && { echo "simulated crash"; exit 3; }; sleep 2; done'
HealthCmd=/bin/test -f /tmp/healthy
HealthInterval=5s
PodmanArgs=--memory 64m --pids-limit 32

[Service]
Restart=on-failure
RestartSec=2
TimeoutStartSec=120

[Install]
WantedBy=multi-user.target default.target
```

```sh
mkdir -p /etc/containers/systemd && cp /mnt/w/l8app.container /etc/containers/systemd/
/usr/libexec/podman/quadlet -dryrun 2>&1 | sed -n '1,40p'
systemctl daemon-reload
systemctl cat l8app.service | head -5
systemctl is-enabled l8app.service
```

```text
quadlet-generator[1780]: Error occurred resolving path "/usr/share/containers/systemd": lstat /usr/share/containers/systemd: no such file or directory
quadlet-generator[1780]: Loading source unit file /etc/containers/systemd/l8app.container
---l8app.service---
[Unit]
Description=L8 demo sensor app (Quadlet)
After=local-fs.target
SourcePath=/etc/containers/systemd/l8app.container
RequiresMountsFor=%t/containers

[X-Container]
Image=localhost/l8/alpine:3.24.2
ContainerName=l8app
Network=none
Exec=/bin/sh -c 'touch /tmp/healthy; i=0; while :; do i=$((i+1)); echo "tick $i"; [ $i -ge 4 ] && { echo "simulated crash"; exit 3; }; sleep 2; done'
HealthCmd=/bin/test -f /tmp/healthy
HealthInterval=5s
PodmanArgs=--memory 64m --pids-limit 32

[Service]
Restart=on-failure
RestartSec=2
TimeoutStartSec=120
Environment=PODMAN_SYSTEMD_UNIT=%n
KillMode=mixed
ExecStop=/usr/bin/podman rm -v -f -i --cidfile=%t/%N.cid
ExecStopPost=-/usr/bin/podman rm -v -f -i --cidfile=%t/%N.cid
Delegate=yes
Type=notify
NotifyAccess=all
SyslogIdentifier=%N
ExecStart=/usr/bin/podman run --name=l8app --cidfile=%t/%N.cid --replace --rm --cgroups=split --network=none --sdnotify=conmon -d --health-cmd "/bin/test -f /tmp/healthy" --health-interval 5s --memory 64m --pids-limit 32 localhost/l8/alpine:3.24.2 /bin/sh -c "touch /tmp/healthy; i=0; while :; do i=$((i+1)); echo \"tick $i\"; [ $i -ge 4 ] && { echo \"simulated crash\"; exit 3; }; sleep 2; done"

[Install]
WantedBy=multi-user.target default.target

# /run/systemd/generator/l8app.service
# Automatically generated by /usr/lib/systemd/system-generators/podman-system-generator
# 
[Unit]
Description=L8 demo sensor app (Quadlet)
generated
```

출력에서 볼 것:

- `.container` 파일이 `ExecStart=/usr/bin/podman run ...` 한 줄로 번역된다. 중요한 추가 옵션: `--cgroups=split`(컨테이너를 **이 서비스의 cgroup 아래**에 둔다), `--sdnotify=conmon` + `Type=notify`(컨테이너가 시작되면 conmon이 systemd에 READY를 알린다), `--replace --rm`(남은 컨테이너를 지우고 새로 만든다), `Delegate=yes`(서비스 cgroup의 하위 관리를 Podman에 위임).
- unit은 `/run/systemd/generator/`에 생기고 `is-enabled`가 `generated`다. `systemctl enable`이 필요 없다 — `[Install]`의 `WantedBy=`를 generator가 읽어 부팅 때 걸어 준다. `/usr/share/containers/systemd` 오류 줄은 "그 디렉터리가 없다"는 무해한 메시지다.

재부팅 후(같은 이미지로 다시 부팅):

```sh
systemctl status l8app.service --no-pager | head -12
journalctl -b -u l8app.service --no-pager -o short-monotonic | grep -vE "healthcheck|podman\[" | head -30
```

```text
● l8app.service - L8 demo sensor app (Quadlet)
     Loaded: loaded (/etc/containers/systemd/l8app.container; generated)
     Active: active (running) since Sun 2026-10-18 05:03:49 UTC; 2s ago
   Main PID: 431 (conmon)
      Tasks: 3 (limit: 2369)
     Memory: 10.9M (peak: 23.7M)
        CPU: 12.826s
     CGroup: /system.slice/l8app.service
             ├─libpod-payload-ce46cd4353de45487b2daa3ad738ef48cc0fece161c4b80f02b927bba06f466d
             │ ├─435 /bin/sh -c "touch /tmp/healthy; i=0; while :; do i=\$((i+1)); echo \"tick \$i\"; [ \$i -ge 4 ] && { echo \"simulated crash\"; exit 3; }; sleep 2; done"
             │ └─461 sleep 2
             └─runtime
[   34.206099] labsysd systemd[1]: Starting l8app.service - L8 demo sensor app (Quadlet)...
[   61.266676] labsysd systemd[1]: Started l8app.service - L8 demo sensor app (Quadlet).
[   61.881033] labsysd l8app[431]: tick 1
[   62.721490] labsysd l8app[298]: ce46cd4353de45487b2daa3ad738ef48cc0fece161c4b80f02b927bba06f466d
[   64.081094] labsysd l8app[431]: tick 2
```

50초 동안 지켜본 재시작(다른 부팅에서 `systemctl start` 후 `sleep 50`):

```sh
systemctl show l8app.service -p NRestarts -p ActiveState -p MemoryMax
journalctl -u l8app.service --no-pager -o short-monotonic | grep -vE 'healthcheck|podman\[' | grep -E "tick|crash|Scheduled restart|Started|exited|Failed" | head -16
systemctl list-timers --all --no-pager | grep -c podman
```

```text
NRestarts=3
MemoryMax=infinity
ActiveState=activating
[  159.161950] labsysd systemd[1]: Started l8app.service - L8 demo sensor app (Quadlet).
[  159.884783] labsysd l8app[435]: tick 1
[  162.029387] labsysd l8app[435]: tick 2
[  164.309677] labsysd l8app[435]: tick 3
[  166.714750] labsysd l8app[435]: tick 4
[  166.959407] labsysd l8app[435]: simulated crash
[  187.344230] labsysd systemd[1]: l8app.service: Main process exited, code=exited, status=3/NOTIMPLEMENTED
[  192.041383] labsysd systemd[1]: l8app.service: Failed with result 'exit-code'.
[  194.154591] labsysd systemd[1]: l8app.service: Scheduled restart job, restart counter is at 1.
[  206.873804] labsysd systemd[1]: Started l8app.service - L8 demo sensor app (Quadlet).
[  206.985287] labsysd l8app[578]: tick 1
[  209.105259] labsysd l8app[578]: tick 2
[  211.365923] labsysd l8app[578]: tick 3
[  213.566396] labsysd l8app[578]: tick 4
[  213.604993] labsysd l8app[578]: simulated crash
[  221.582461] labsysd systemd[1]: l8app.service: Main process exited, code=exited, status=3/NOTIMPLEMENTED
```

출력에서 볼 것:

- **부팅 때 자동 시작**: 부팅 34초에 Starting, 61초에 Started. 27초가 걸린 것은 TCG에서 podman이 이미지·네트워크·cgroup을 준비하는 시간이다(§10에서 따로 잰다). 장비에서 "전원 인가 후 N초 안에 서비스"가 요구사항이면 이 시간을 반드시 실측한다.
- **cgroup 배치**: 컨테이너 프로세스(435)가 `system.slice/l8app.service/libpod-payload-<id>` 아래에 있다(그림 4). conmon은 서비스의 Main PID(431)이고, 서비스의 `runtime` 하위 그룹도 있다. `--cgroups=split`의 결과다. unit 자체의 `MemoryMax=infinity` — `--memory 64m`은 unit이 아니라 payload cgroup에 걸린다.
- **앱 종료 코드 3이 서비스 종료 코드가 된다**(`status=3`). conmon이 컨테이너의 종료 코드를 그대로 전달하기 때문이다. `Restart=on-failure`가 2초 뒤(`RestartSec=2`, 로그 시각은 TCG라 늘어졌다) 다시 시작하고, 50초 동안 3번 재시작했다.
- healthcheck는 systemd 타이머로 돈다(`list-timers`에 podman 타이머 1개). Podman 4.9는 `HealthOnFailure=kill`처럼 health 실패 시 행동을 정할 수 있고, `Notify=healthy`(최신 버전)는 health가 통과해야 서비스를 시작 완료로 보고한다 — 버전마다 지원 키가 다르니 `podman-systemd.unit(5)`을 확인한다.

### 9.3 장비용 Quadlet 체크리스트

- `Restart=on-failure` + `StartLimitIntervalSec=`/`StartLimitBurst=`로 재시작 폭주를 막고, 한도를 넘으면 `OnFailure=`로 복구 unit(이전 이미지로 롤백 등, §13)을 부른다.
- 기본 `--sdnotify=conmon`에서는 READY를 conmon이 보내므로 앱의 실제 준비 상태와 무관하다. 앱이 직접 `sd_notify("READY=1")`(그리고 `WATCHDOG=1`)를 보내게 하려면 `Notify=true`(→ `--sdnotify=container`)로 NOTIFY 소켓을 컨테이너에 넘긴다. watchdog 메시지 전달은 Podman 버전별로 동작을 확인한다(이 노트에서는 실행 안 함).
- 이미지는 태그 대신 다이제스트(`Image=registry/app@sha256:...`)로 고정하고, `AutoUpdate=registry`(+ `podman-auto-update.timer`)는 장비 군단 정책에 맞을 때만 켠다.
- 네트워크가 필요한 컨테이너는 `After=network-online.target`, 장치가 필요한 컨테이너는 `After=dev-video0.device`(udev 규칙, L2)와 `AddDevice=/dev/video0`.
- 로그는 journald로 간다(`SyslogIdentifier=%N`). journald 크기 제한(`SystemMaxUse=`)이 data 파티션 예산과 맞는지 확인한다.

---

## 10. 측정 — 시작 시간과 메모리 오버헤드

### 10.1 예제 14 — `/bin/true` 하나를 띄우는 비용

무엇을 확인하나: 같은 Alpine rootfs의 `true`를 (1) 그냥, (2) chroot, (3) unshare + chroot, (4) 예제 8의 `l8_mini`, (5) `podman run --rm`, (6) `systemd-nspawn`으로 5번씩 실행해 벽시계 시간을 잰다.

```sh
t() { local s=$(date +%s%N); "$@" >/dev/null 2>&1; echo $(( ($(date +%s%N)-s)/1000000 )); }
echo "native: $(for i in 1 2 3 4 5; do t /var/l8/alpine/bin/busybox true; done | tr '\n' ' ')"
echo "chroot: $(for i in 1 2 3 4 5; do t chroot /var/l8/alpine /bin/true; done | tr '\n' ' ')"
echo "unshare: $(for i in 1 2 3 4 5; do t unshare -pfmun --mount-proc chroot /var/l8/alpine /bin/true; done | tr '\n' ' ')"
echo "l8_mini: $(for i in 1 2 3 4 5; do t /mnt/w/l8_mini /var/l8/alpine /bin/true; done | tr '\n' ' ')"
echo "podman: $(for i in 1 2 3 4 5; do t podman run --rm --network none localhost/l8/alpine:3.24.2 true; done | tr '\n' ' ')"
echo "nspawn: $(for i in 1 2 3 4 5; do t systemd-nspawn -q --register=no -D /var/l8/alpine /bin/true; done | tr '\n' ' ')"
systemd-nspawn -q --register=no -D /var/l8/alpine /bin/cat /etc/alpine-release; echo "nspawn exit=$?"
```

```text
native: 197 171 153 211 112 
chroot: 232 274 226 334 251 
unshare: 510 310 300 242 327 
l8_mini: 405 356 246 259 280 
podman: 7686 6617 7010 6880 6725 
nspawn: 2198 2752 2226 1436 1447 
3.24.2
nspawn exit=0
```

```svg
<svg viewBox="0 0 680 345" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" text-anchor="middle" font-size="13" font-weight="bold">/bin/true 한 번 실행까지 걸린 시간 (5회 평균, ms, TCG)</text>
  <text x="192" y="63" text-anchor="end" font-size="12">native (busybox true)</text>
  <rect x="200" y="46" width="10" height="24" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
  <text x="216" y="63" font-size="12">169 ms</text>
  <text x="192" y="103" text-anchor="end" font-size="12">chroot</text>
  <rect x="200" y="86" width="15" height="24" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
  <text x="221" y="103" font-size="12">263 ms</text>
  <text x="192" y="143" text-anchor="end" font-size="12">unshare + chroot</text>
  <rect x="200" y="126" width="19" height="24" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
  <text x="225" y="143" font-size="12">338 ms</text>
  <text x="192" y="183" text-anchor="end" font-size="12">l8_mini (예제 8)</text>
  <rect x="200" y="166" width="18" height="24" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
  <text x="224" y="183" font-size="12">309 ms</text>
  <text x="192" y="223" text-anchor="end" font-size="12">systemd-nspawn</text>
  <rect x="200" y="206" width="115" height="24" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
  <text x="321" y="223" font-size="12">2012 ms</text>
  <text x="192" y="263" text-anchor="end" font-size="12">podman run --rm</text>
  <rect x="200" y="246" width="399" height="24" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
  <text x="593" y="263" text-anchor="end" font-size="12">6984 ms</text>
  <line x1="200" y1="40" x2="200" y2="286" stroke="currentColor"/>
  <line x1="200" y1="286" x2="600" y2="286" stroke="currentColor"/>
  <line x1="200" y1="286" x2="200" y2="291" stroke="currentColor"/>
  <text x="200" y="306" text-anchor="middle" font-size="12">0</text>
  <line x1="314" y1="286" x2="314" y2="291" stroke="currentColor"/>
  <text x="314" y="306" text-anchor="middle" font-size="12">2000</text>
  <line x1="429" y1="286" x2="429" y2="291" stroke="currentColor"/>
  <text x="429" y="306" text-anchor="middle" font-size="12">4000</text>
  <line x1="543" y1="286" x2="543" y2="291" stroke="currentColor"/>
  <text x="543" y="306" text-anchor="middle" font-size="12">6000</text>
  <text x="340" y="330" text-anchor="middle" font-size="12">TCG 에뮬레이션 값 — 실제 Cortex-A78AE에서는 모두 수십 배 짧다. 비율만 볼 것</text>
</svg>
```
그림 8 — 예제 14의 평균값. namespace·overlay·cgroup 자체는 싸고(l8_mini ≈ chroot의 1.2배), 비용의 대부분은 엔진의 준비 작업이다

출력에서 볼 것:

- **TCG라 모든 값이 크다.** `true` 한 번이 170 ms인 것은 에뮬레이션 + `date` 포크 비용이다. 실제 Orin급 하드웨어에서는 native가 1 ms 안쪽이다. 비율로 읽자.
- **커널 기능 자체는 싸다**: unshare(5종 namespace) + chroot, 그리고 overlay·pivot_root·cgroup까지 하는 `l8_mini`가 chroot와 비슷하다. 컨테이너의 "격리 비용"은 커널이 아니다.
- **엔진이 비싸다**: `podman run`이 native의 약 40배, nspawn의 3.5배. Podman은 매번 이미지 메타데이터 확인, 저장소 잠금, 컨테이너 객체·overlay 마운트 생성, conmon 실행, crun 실행(OCI hook·seccomp 프로파일 컴파일 포함), 종료 후 정리(`--rm`)를 한다. 실제 하드웨어에서 `podman run --rm`은 보통 수백 ms 수준이다(기억 기반의 대략값 — 장비에서 직접 잴 것).
- **설계 결론**: 컨테이너는 "한 번 띄워 오래 도는 서비스"용이다. 이벤트마다 `podman run`을 하는 설계(요청마다 컨테이너 하나)는 장비에서 피한다. 부팅 시간 예산에서는 엔진 준비 시간을 서비스마다 더해야 한다(예제 13의 27초를 기억하자 — TCG지만 비율은 실제로도 크다).
- `systemd-nspawn`은 이미지 생태계 없이 디렉터리 트리를 바로 띄운다. Alpine 트리에서도 바로 돌았다(`3.24.2`).

### 10.2 예제 15 — 메모리 오버헤드

무엇을 확인하나: `sleep` 컨테이너 5개를 띄웠을 때 엔진이 상주하며 먹는 메모리.

```sh
for i in 1 2 3 4 5; do podman run -d --network none localhost/l8/alpine:3.24.2 sleep 600 >/dev/null; done
ps -o rss=,comm= -C conmon | awk '{s+=$1; n++} END {print n " conmon, total RSS " s " kB"}'
for c in $(podman ps -q); do cat /sys/fs/cgroup/machine.slice/libpod-$(podman inspect -f '{{.Id}}' $c).scope/container/memory.current; done | awk '{s+=$1} END {print "5 containers memory.current total " s/1024 " kB"}'
pgrep -x podman || echo "no podman daemon"
for i in 1 2 3 4 5; do sleep 600 & done; sleep 1; ps -o rss= -C sleep | awk '{s+=$1} END {print "5 native sleep RSS " s " kB"}'
```

```text
5 conmon, total RSS 10820 kB
5 containers memory.current total 2104 kB
no podman daemon
5 native sleep RSS 8424 kB
```

출력에서 볼 것:

- 컨테이너 하나당 **conmon 약 2.2 MB RSS** + 컨테이너 cgroup 안의 메모리(여기서는 busybox `sleep`이라 약 0.4 MB). 상주 데몬은 없다(`pgrep -x podman` 없음).
- 네이티브 Ubuntu `sleep` 5개의 RSS 합 8.4 MB는 glibc 공유 라이브러리를 프로세스마다 센 값이라 직접 비교용이 아니다(RSS는 공유 페이지를 중복으로 센다). 비교한다면 cgroup의 `memory.current`나 `/proc/<pid>/smaps_rollup`의 Pss가 맞다.
- **진짜 메모리 비용은 엔진보다 "앱마다 따로 가진 userspace"다.** 컨테이너 A가 glibc + Python 3.12, B가 musl + Python 3.11이면 두 벌의 라이브러리가 각각 page cache와 RSS를 차지한다. 같은 베이스 층을 쓰면 파일이 같은 inode(같은 overlay lower)라 page cache를 공유한다. CUDA·TensorRT처럼 수백 MB짜리 라이브러리는 **모든 컨테이너가 같은 베이스 층을 쓰도록** 맞추는 것이 RAM 절약의 핵심이다.
- Docker라면 여기에 dockerd + containerd(합쳐서 대략 수십~100 MB대, 버전·설정마다 다름 — 기억 기반)가 더해진다.

---

## 11. 장치와 GPU — `/dev` 노드는 어떻게 컨테이너에 들어가나

### 11.1 세 개의 문

컨테이너 안 프로세스가 `/dev/video0`을 `open()`하려면 문 세 개를 통과해야 한다.

1. **노드가 보여야 한다** — 컨테이너의 mount namespace에 그 장치 파일이 있어야 한다. 런타임은 컨테이너용 `/dev`를 tmpfs로 새로 만들고 기본 노드(`null`, `zero`, `full`, `random`, `urandom`, `tty`, `console`, `pts/`)만 넣는다.
2. **device cgroup이 허용해야 한다** — `config.json`의 `linux.resources.devices` 목록을 런타임이 BPF 프로그램으로 컴파일해 cgroup에 붙인다. 예제 10에서 본 `{"allow":false,"access":"rwm"}`가 "기본은 전부 거부"다.
3. **파일 권한이 맞아야 한다** — 노드의 소유자·그룹·모드, 그리고 user namespace의 UID/GID 매핑.

`--device /dev/video0`은 1과 2를 한 번에 해 준다. 노드만 bind mount(`-v`)하면 1만 되고 2에서 막힌다.

### 11.2 예제 16 — `--device`, bind mount, mknod, `--privileged`, CDI

무엇을 확인하나: vivid(가상 V4L2 카메라 드라이버, I5)로 `/dev/video0`을 만들고, 컨테이너에 넣는 여러 방법이 각각 어느 문에서 막히는지.

```sh
modprobe vivid n_devs=1 node_types=0x1 && sleep 1; ls -l /dev/video*
I=localhost/l8/alpine:3.24.2
podman run --rm --network none $I ls -l /dev/video0
podman run --rm --network none --device /dev/video0 $I sh -c 'ls -l /dev/video0; head -c 0 /dev/video0 && echo open-ok'
podman run --rm --network none -v /dev/video0:/dev/video0 $I sh -c 'ls -l /dev/video0; head -c 0 /dev/video0 && echo open-ok'
podman run --rm --network none $I sh -c 'mknod /tmp/v c 81 0'
podman run --rm --network none --cap-add MKNOD $I sh -c 'mknod /tmp/v c 81 0 && ls -l /tmp/v; head -c 0 /tmp/v && echo open-ok'
podman run --rm --network none --device /dev/video0:/dev/video0:r $I sh -c 'head -c 0 /dev/video0 && echo read-open-ok; echo -n > /dev/video0 && echo write-open-ok'
podman run --rm --network none --privileged $I sh -c 'ls /dev | wc -l; ls /dev | grep -cE "^(vda|mem|video0)$"; grep CapEff /proc/self/status'
```

```text
crw-rw---- 1 root video 81, 0 Oct 18 05:05 /dev/video0
ls: /dev/video0: No such file or directory
crw-rw----    1 root     44         81,   0 Oct 18 05:05 /dev/video0
open-ok
crw-rw----    1 root     44         81,   0 Oct 18 05:05 /dev/video0
head: /dev/video0: Operation not permitted
mknod: /tmp/v: Operation not permitted
crw-r--r--    1 root     root       81,   0 Oct 18 05:05 /tmp/v
head: /tmp/v: Operation not permitted
read-open-ok
sh: can't create /dev/video0: Operation not permitted
169
3
CapEff:	000001ffffffffff
```

출력에서 볼 것:

| 방법 | 문 1 (노드) | 문 2 (device cgroup) | 결과 |
|---|---|---|---|
| 아무것도 안 함 | 없음 | — | `No such file` |
| `--device /dev/video0` | 있음 | 허용(rwm) | open 성공 |
| `-v /dev/video0:/dev/video0` | 있음(bind) | **거부** | `Operation not permitted` |
| `mknod` (기본) | — | — | `CAP_MKNOD`이 없어 mknod 자체가 실패 |
| `--cap-add MKNOD` + mknod | 직접 만듦 | **거부** | 노드는 생겼지만 open이 `EPERM` |
| `--device ...:r` | 있음 | 읽기만 허용 | 읽기 open 성공, 쓰기 open `EPERM` |
| `--privileged` | 호스트 `/dev` 전체(169개, `vda`·`mem` 포함) | 전부 허용 | 모든 capability(`1ffffffffff`) |

- 컨테이너 안에서 그룹이 `video`가 아니라 `44`로 보인다. 호스트의 `video` GID가 44인데 Alpine의 `/etc/group`에는 GID 44의 이름이 없기 때문이다(Alpine의 video는 27). 장치 권한을 **그룹 이름**으로 맞추는 udev 규칙(L2)과 컨테이너 안 사용자의 그룹 ID가 어긋나는 흔한 사례다. `--group-add keep-groups`(rootless)나 `--group-add 44`로 맞춘다.
- `mknod`로 만든 노드도 열 수 없다는 것이 핵심이다. **device cgroup은 "어떤 경로냐"가 아니라 (타입, major, minor, 접근)으로 판단한다**(§15). 그래서 컨테이너가 스스로 장치 노드를 만들어 탈출하는 길이 막힌다.
- `--privileged`에서는 `/dev/vda`(루트 디스크)와 `/dev/mem`이 보인다. 이 컨테이너는 부트 파티션을 덮어쓸 수 있다(§12.3).

이어서 CDI(Container Device Interface)로 같은 장치를 넣어 보자. CDI는 "장치 하나를 컨테이너에 넣으려면 무엇을 편집해야 하는가"(노드, mount, 환경 변수, hook)를 YAML/JSON으로 적어 두는 표준이다. Podman 4.9는 `/etc/cdi/`의 명세를 읽고 `--device <vendor>/<class>=<name>`으로 쓴다.

```text
cdiVersion: "0.6.0"
kind: "l8.example/camera"
devices:
  - name: cam0
    containerEdits:
      deviceNodes:
        - path: /dev/video0
      env:
        - L8_CAMERA=cam0
containerEdits:
  mounts:
    - hostPath: /etc/l8-camera.conf
      containerPath: /etc/l8-camera.conf
      options: ["ro", "bind"]
```

```sh
mkdir -p /etc/cdi; cp /mnt/w/l8-camera.yaml /etc/cdi/; echo "gain=4" > /etc/l8-camera.conf
podman run --rm --network none --device l8.example/camera=cam0 localhost/l8/alpine:3.24.2 sh -c 'ls -l /dev/video0; echo L8_CAMERA=$L8_CAMERA; cat /etc/l8-camera.conf; head -c 0 /dev/video0 && echo open-ok'
```

```text
crw-rw-rw-    1 root     root       81,   0 Oct 18 05:06 /dev/video0
L8_CAMERA=cam0
gain=4
open-ok
```

출력에서 볼 것: 이름 하나(`l8.example/camera=cam0`)로 장치 노드 + 보정 파일 mount + 환경 변수가 한 번에 들어갔다. 장치가 노드 하나가 아니라 "노드 여러 개 + 라이브러리 + 설정"의 묶음일 때 CDI가 빛난다 — 그게 정확히 GPU다. 노드 모드가 `--device` 때(`rw-rw----`, GID 44)와 달리 `rw-rw-rw-` · root:root로 나왔다 — 명세에 `permissions`·`uid`·`gid`를 적지 않은 결과로 보인다. 제품 명세에서는 이 셋을 명시해 의도한 권한을 고정한다.

검증 경로: 첫 시도(장치 모듈이 없던 rootfs)에서는 같은 명령이 `failed to stat CDI host device "/dev/video0"`로 실패했다 — CDI는 명세의 호스트 노드가 실제로 있는지 검사한다. 장치 드라이버가 늦게 probe되는 장비(deferred probe, G3)에서는 컨테이너 시작 순서를 장치 준비 뒤로(`After=dev-video0.device`) 둬야 하는 이유다.
### 11.3 Jetson에서 GPU가 컨테이너로 들어가는 길 (가이드 — 이 랩에서 실행 안 함, 기억 기반)

Jetson(Orin 등)의 GPU는 PCIe 카드가 아니라 SoC 안의 iGPU다. 그래도 원리는 §11.1~11.2와 같다 — **장치 노드 + 드라이버 라이브러리 + device cgroup 허용**. 다만 넣을 것이 많다.

1. **커널 드라이버는 호스트에만 있다.** iGPU 커널 드라이버(L4T의 nvgpu 계열)와 펌웨어는 L4T(K6 §9)의 OS 층에 있다. 컨테이너는 이것을 바꿀 수 없다.
2. **장치 노드**: GPU 메모리 할당자(`/dev/nvmap`), GPU 채널·제어 노드(`/dev/nvhost-*` 또는 `/dev/nvgpu/igpu0/*` 계열), 경우에 따라 비디오 인코더·디코더·VIC·DLA 노드. 정확한 목록은 JetPack 버전마다 다르다.
3. **user-mode 드라이버 라이브러리**: `libcuda.so`와 Tegra 전용 라이브러리(`/usr/lib/aarch64-linux-gnu/tegra/` 아래)는 **호스트 커널 드라이버 버전과 맞아야 한다.** 그래서 이미지에 굽기보다 호스트에서 bind mount해 넣는다.
4. **NVIDIA Container Toolkit**이 이 목록을 관리한다. Jetson에서는 L4T가 제공하는 CSV 목록(장치 노드·라이브러리·심볼릭 링크 목록, `/etc/nvidia-container-runtime/host-files-for-container.d/` 아래)을 읽어 컨테이너에 넣는다. 최근 방식은 이 목록으로 **CDI 명세를 생성**하는 것이다:

```sh
# 가이드(실행 안 함) — Jetson 호스트에서, JetPack/Toolkit 버전에 따라 옵션이 다를 수 있다
sudo nvidia-ctk cdi generate --mode=csv --output=/etc/cdi/nvidia.yaml
nvidia-ctk cdi list                                   # nvidia.com/gpu=all 같은 이름이 보인다
podman run --rm --device nvidia.com/gpu=all nvcr.io/nvidia/l4t-jetpack:<L4T 태그> nvidia-smi
# Docker: --runtime nvidia 또는 CDI 지원 버전에서 --device nvidia.com/gpu=all
```

5. **베이스 이미지 태그가 L4T 버전에 묶인다.** NVIDIA의 `l4t-base`, `l4t-jetpack`, `l4t-tensorrt` 같은 이미지는 L4T 릴리스 태그를 쓴다. 기억 기준으로 JetPack 5까지는 CUDA 등 상당 부분을 호스트에서 mount해 넣었고, JetPack 6부터는 CUDA 같은 사용자 라이브러리를 컨테이너 안에 포함하는 쪽으로 바뀌었다 — 실제 제품에서는 NVIDIA 문서로 확인할 것.

설계상 결론: **JetPack(OS 층)을 올리면 GPU 컨테이너도 같이 검증·재빌드해야 한다.** 그래서 §13의 "OS ↔ 앱 호환표"가 Jetson에서는 특히 중요하고, OS 업데이트는 "새 OS + 맞는 앱 이미지 세트"를 한 단위로 다룬다. 예제 16의 `l8.example/camera` CDI 명세는 `nvidia.yaml`과 같은 형식(`deviceNodes`, `mounts`, `env`, 그리고 NVIDIA는 `hooks`로 `ldconfig` 갱신까지)이다 — 규모만 다르다.

---

## 12. 보안 모델 — rootless, user namespace, 그리고 `--privileged`라는 함정

깊은 내용(capability 집합, seccomp BPF, AppArmor/SELinux 프로파일 작성)은 L6에서 다룬다. 여기서는 **컨테이너 런타임이 기본으로 무엇을 깔아 주는지**와 장비에서 자주 하는 실수만 정리한다.

### 12.1 기본으로 깔리는 방어층

예제 10에서 실제 `config.json`과 `/proc/<pid>/status`로 본 것을 정리하면 이렇다.

| 층 | Podman 기본값(이 게스트) | 효과 |
|---|---|---|
| capability | 11개만 남김(`CapEff` 비트마스크로 확인) | `CAP_SYS_ADMIN`, `CAP_SYS_MODULE`, `CAP_SYS_TIME`, `CAP_NET_ADMIN`, `CAP_SYS_RAWIO` 없음 → mount·모듈·시계·네트워크 설정·`/dev/mem` 불가 |
| seccomp | 기본 프로파일(`Seccomp: 2` = filter 모드) | `kexec_load`, `init_module`, `bpf`, `perf_event_open`, `keyctl` 등 위험 syscall을 `EPERM`으로 |
| `/proc`·`/sys` | `/sys` 읽기전용, `/proc/kcore`·`/proc/sysrq-trigger` 등은 가리거나 읽기전용 | 커널 상태 조작 차단 |
| device cgroup | 기본 장치(`null`, `zero`, `random`, `tty` 등)만 허용 | §11의 `/dev/video0` 실험 |
| LSM | 호스트에 AppArmor가 켜져 있으면 `containers-default` 프로파일 | 경로 기반 추가 제한(L6) |
| no_new_privs | 기본은 꺼짐(`--security-opt no-new-privileges`로 켬) | setuid 바이너리로 권한 상승 차단 |

### 12.2 rootful · rootless · user namespace

- **rootful**: `root`가 `podman run`. 컨테이너 안 UID 0 = 호스트 UID 0(user namespace 없음). capability를 깎았을 뿐 "진짜 root"다. 탈출하면 호스트 root.
- **rootless**: 일반 사용자가 `podman run`. Podman이 user namespace를 만들고 `/etc/subuid`의 범위(예: `100000-165535`)를 매핑한다. 컨테이너 안 UID 0은 호스트에서 **그 사용자 자신**, 안 UID 1은 호스트 UID 100000이다(예제 11 출력). 탈출해도 일반 사용자 권한이다.
- **rootful + userns**: `podman run --userns=auto`처럼 root가 띄우되 매핑을 쓰는 방식. 장치 접근이 필요한 시스템 서비스에 좋은 절충이다.

rootless의 대가: 1024 미만 포트 바인딩 불가(설정으로 완화), 네트워크가 사용자 공간 스택(slirp4netns·pasta)을 거쳐 느리다, cgroup 위임이 systemd 사용자 세션에 의존한다, `/dev` 장치의 그룹 권한이 매핑 때문에 꼬인다. 장비에서는 "rootful 엔진 + 서비스마다 최소 권한 + userns" 조합이 흔하다.

### 12.3 `--privileged` — 장비에서 가장 흔한 안티패턴

"카메라가 안 열려요" → `--privileged` 한 줄로 해결하는 순간, 그 컨테이너는:

- capability 전부(`CAP_SYS_ADMIN` 포함)를 갖고,
- seccomp·AppArmor가 꺼지고,
- 호스트의 `/dev` **전체**가 보이고 device cgroup이 모두 허용된다(예제 16 마지막 줄).

즉 `mount /dev/mmcblk0p1 /mnt`로 부트 파티션을 덮어쓰거나 `/dev/mem`으로 레지스터를 만질 수 있다. 사실상 "호스트 root 셸"이다. 고치는 법은 언제나 같다 — **필요한 장치 노드만 `--device`(또는 CDI)로, 필요한 capability만 `--cap-add`로, 필요한 sysfs 경로만 읽기전용 bind로**.

### 12.4 흔한 함정

- `-v /var/run/docker.sock:/var/run/docker.sock` — 컨테이너에 엔진 소켓을 주면 그 컨테이너는 아무 컨테이너나 `--privileged`로 띄울 수 있다 = 호스트 root. 원격 관리 에이전트를 만들 때 특히 조심.
- `--network host` + `CAP_NET_ADMIN` — 호스트 방화벽·라우팅을 바꿀 수 있다. 진단용으로 잠깐 쓰고 제품 설정에 남기지 말 것.
- 이미지 안에 SSH 키·클라우드 자격 증명을 굽는 것 — 층은 지워도 이전 층에 남는다(`podman history`로 보인다). 비밀은 런타임에 주입한다(`podman secret`, TPM 봉인 키 — L6).

---

## 13. 업데이트 전략 — 컨테이너 업데이트 vs A/B OS 업데이트

L4에서 A/B 파티션·bootcount 롤백·RAUC/SWUpdate/Mender를 다룬다. 컨테이너가 들어오면 질문이 하나 생긴다. **"OS 전체를 A/B로 바꿀까, 앱 컨테이너만 바꿀까?"** 현장의 답은 거의 항상 **둘 다(하이브리드)** 다.

### 13.1 두 방식의 성격

| 항목 | A/B OS 이미지 업데이트 (L4) | 컨테이너 앱 업데이트 |
|---|---|---|
| 단위 | rootfs 파티션 통째(수백 MB~수 GB, 압축·delta 가능) | 바뀐 층만(수 MB~수백 MB) |
| 원자성 | 슬롯 전환 한 번 — 매우 강함 | 태그 전환 + 재시작. 여러 컨테이너를 같이 바꿀 때는 원자적이지 않다 |
| 롤백 | bootcount·부트로더가 자동 | 이전 이미지가 남아 있으면 태그만 되돌림(직접 구현) |
| 바꿀 수 있는 것 | 커널·드라이버·부트로더·systemd·엔진 자체 | 앱 userspace만. 커널·모듈·펌웨어·GPU 커널 드라이버는 못 바꾼다 |
| 재부팅 | 필요 | 보통 불필요(서비스 재시작) |
| 검증 | 이미지 서명 + dm-verity(L5·L6) | 이미지 서명(cosign·sigstore, `policy.json`) + 다이제스트 고정 |
| 위험 | 큰 다운로드, 재부팅 중 전원 차단은 A/B가 막아 줌 | 데이터 파티션이 차면 pull 실패, 층 GC 필요, 버전 조합 폭발 |

### 13.2 하이브리드 구조

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah13" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="16" width="300" height="34" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="38" text-anchor="middle" font-size="12">bootloader — A/B 선택 · bootcount</text>
  <rect x="20" y="58" width="300" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="80" text-anchor="middle" font-size="13" font-weight="bold">rootfs A — 읽기전용 · dm-verity</text>
  <text x="170" y="100" text-anchor="middle" font-size="12">커널 · 모듈 · podman · GPU 드라이버</text>
  <rect x="20" y="122" width="300" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="144" text-anchor="middle" font-size="13" font-weight="bold">rootfs B — 비활성 슬롯</text>
  <text x="170" y="164" text-anchor="middle" font-size="12">다음 OS 버전을 여기에 쓴다</text>
  <rect x="20" y="186" width="300" height="96" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="170" y="210" text-anchor="middle" font-size="13" font-weight="bold">data 파티션 (ext4 · F2FS)</text>
  <text x="170" y="232" text-anchor="middle" font-size="12">/var/lib/containers — 이미지 층</text>
  <text x="170" y="252" text-anchor="middle" font-size="12">Quadlet unit · 앱 설정 · 모델 파일</text>
  <text x="170" y="272" text-anchor="middle" font-size="12">로그 · 크래시 덤프 (L7)</text>
  <rect x="370" y="40" width="290" height="80" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="515" y="62" text-anchor="middle" font-size="13" font-weight="bold">OS 업데이트 — 드물게 · 원자적</text>
  <text x="515" y="84" text-anchor="middle" font-size="12">RAUC · SWUpdate · Mender → 비활성 슬롯</text>
  <text x="515" y="104" text-anchor="middle" font-size="12">부팅 실패면 bootcount 롤백 (L4)</text>
  <rect x="370" y="176" width="290" height="100" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="515" y="198" text-anchor="middle" font-size="13" font-weight="bold">앱 업데이트 — 자주 · 작게</text>
  <text x="515" y="220" text-anchor="middle" font-size="12">바뀐 층만 pull 또는 load</text>
  <text x="515" y="240" text-anchor="middle" font-size="12">unit의 이미지 태그 교체 → 재시작</text>
  <text x="515" y="260" text-anchor="middle" font-size="12">healthcheck 실패 → 이전 태그로</text>
  <line x1="370" y1="110" x2="324" y2="148" stroke="#e08a3c" marker-end="url(#ah13)"/>
  <line x1="370" y1="232" x2="324" y2="232" stroke="#3f9a6b" marker-end="url(#ah13)"/>
  <text x="340" y="314" text-anchor="middle" font-size="12">원칙: 커널 ABI에 묶인 것(드라이버 · 펌웨어 · GPU 드라이버)은 OS 층에,</text>
  <text x="340" y="334" text-anchor="middle" font-size="12">자주 바뀌는 것(인식 모델 · 앱 · 설정)은 컨테이너 층에 둔다</text>
</svg>
```
그림 9 — 불변 A/B OS + 데이터 파티션 위 컨테이너 앱. OS 슬롯은 OTA 도구가, 앱 층은 컨테이너 엔진이 바꾼다

설계할 때 지켜야 할 규칙 몇 가지:

- **엔진 저장소는 rootfs가 아니라 data 파티션에 둔다.** rootfs가 읽기전용(J3)이면 `/var/lib/containers`는 어차피 쓸 수 없다. OS 슬롯을 바꿔도 앱 이미지는 살아 있어야 한다.
- **OS ↔ 앱 호환표를 관리한다.** Jetson이라면 JetPack(L4T) 메이저 버전이 바뀌면 GPU user-mode 드라이버 ABI가 바뀌어 기존 CUDA 컨테이너가 깨질 수 있다(§11). OS 업데이트 패키지에 "이 OS와 맞는 앱 이미지 다이제스트 목록"을 같이 싣고, 새 OS로 처음 부팅했을 때 앱 healthcheck까지 통과해야 bootcount를 "성공"으로 확정하는 방식이 안전하다.
- **앱 롤백도 자동이어야 한다.** 새 태그로 재시작 → healthcheck N회 실패 → 이전 다이제스트로 되돌림. Podman의 `podman auto-update`는 레이블이 붙은 Quadlet 컨테이너를 새 이미지로 바꾸고, 새 컨테이너가 시작에 실패하면 이전 이미지로 롤백하는 기능을 갖고 있다(기능 범위는 버전마다 다르니 쓰는 버전 문서를 확인).
- **층 GC**: 오래된 이미지를 지우지 않으면 data 파티션이 찬다. `podman image prune`을 업데이트 성공 확정 뒤에 돌린다. 단, 롤백용 직전 버전 하나는 남긴다.

### 13.3 이미 이 구조로 만든 배포판들

| 제품 | OS 층 업데이트 | 앱 층 | 비고 |
|---|---|---|---|
| balenaOS | Yocto 기반 호스트 OS, 호스트 OS 업데이트(HUP)로 교체 | balena-engine 컨테이너, supervisor 컨테이너가 관리, delta 다운로드 | balenaCloud(상용) 또는 openBalena |
| Torizon OS (Toradex) | Yocto 기반 + OSTree(파일 단위 content-addressed 저장소) | Docker 컨테이너 | Uptane 계열 보안 OTA(aktualizr) |
| Ubuntu Core | 커널·base·gadget도 snap(squashfs), 실패하면 자동 롤백 | snap(OCI 아님, AppArmor + seccomp 격리) | Docker도 snap으로 깔 수 있다 |
| Fedora IoT · RHEL for Edge | rpm-ostree(OSTree) + greenboot 헬스체크 롤백 | Podman + Quadlet | 이 노트의 Podman 실습과 가장 가깝다 |
| 직접 조립 | Yocto/Buildroot + RAUC/SWUpdate(L4) | Podman 또는 Docker | 가장 흔한 사내 구성 |

표의 세부 기능은 각 제품 문서 기준의 일반적 설명이다(기억 기반 — 도입 전에 현재 문서로 확인할 것).

---

## 14. 언제 컨테이너가 나쁜 선택인가

컨테이너는 공짜가 아니다. 다음 조건 중 하나라도 강하게 걸리면 쓰지 않거나 일부만 쓴다.

- **작은 RAM·flash**: 256 MB RAM, 128 MB NAND인 센서 노드에 엔진(수십 MB) + 앱마다 libc 복사본은 사치다. Buildroot 단일 이미지 + systemd 격리 옵션(L1의 `MemoryMax=`, `ProtectSystem=`, `PrivateTmp=`)으로 cgroup·namespace의 이득 대부분을 엔진 없이 얻는다. systemd unit도 cgroup과 namespace를 쓴다는 점을 기억하자 — 컨테이너 엔진이 없어도 "격리"는 가능하다.
- **하드 실시간**: 100 µs급 모터 전류 루프는 Linux가 아니라 MCU(또는 Cortex-R 코어)에 둔다. 1 ms급 제어 루프를 Linux에 둔다면 PREEMPT_RT 커널 + `isolcpus` + SCHED_FIFO + 메모리 lock이 핵심이고, 컨테이너는 그 위에 "얹어도 되는가"를 따로 검증해야 한다(§4).
- **결정론적 부팅 시간**: "전원 인가 후 2초 안에 CAN 하트비트" 같은 요구는 엔진 시작·이미지 마운트·네트워크 설정이 끼면 지키기 어렵다. 그런 서비스는 호스트 프로세스로 먼저 띄운다.
- **장치 밀착 코드**: 커널 모듈·DT overlay·펌웨어 로딩·`/dev/mem` 접근이 필요한 코드는 어차피 OS 층에 묶인다. 컨테이너로 감싸면 `--privileged` 유혹만 생긴다.
- **팀·앱이 하나**: 앱 하나, 팀 하나, 릴리스 주기가 OS와 같다면 A/B 단일 이미지가 가장 단순하고 검증하기 쉽다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah14" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="14" width="330" height="48" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="185" y="35" text-anchor="middle" font-size="13">RAM ≥ 1 GB, data 파티션 여유</text>
  <text x="185" y="53" text-anchor="middle" font-size="13">수백 MB 이상인가?</text>
  <rect x="400" y="14" width="260" height="48" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="530" y="35" text-anchor="middle" font-size="12">컨테이너 없이 단일 이미지</text>
  <text x="530" y="53" text-anchor="middle" font-size="12">+ systemd 격리 옵션 (L1)</text>
  <line x1="350" y1="38" x2="398" y2="38" stroke="currentColor" marker-end="url(#ah14)"/>
  <text x="374" y="30" text-anchor="middle" font-size="12">아니오</text>
  <line x1="185" y1="62" x2="185" y2="108" stroke="currentColor" marker-end="url(#ah14)"/>
  <text x="200" y="90" font-size="12">예</text>
  <rect x="20" y="110" width="330" height="48" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="185" y="131" text-anchor="middle" font-size="13">같은 Linux에 1 ms 이하 지터의</text>
  <text x="185" y="149" text-anchor="middle" font-size="13">제어 루프가 있나?</text>
  <rect x="400" y="110" width="260" height="48" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="530" y="131" text-anchor="middle" font-size="12">RT 루프는 MCU 또는 호스트 프로세스</text>
  <text x="530" y="149" text-anchor="middle" font-size="12">+ isolcpus, 나머지만 컨테이너</text>
  <line x1="350" y1="134" x2="398" y2="134" stroke="currentColor" marker-end="url(#ah14)"/>
  <text x="374" y="126" text-anchor="middle" font-size="12">예</text>
  <line x1="185" y1="158" x2="185" y2="204" stroke="currentColor" marker-end="url(#ah14)"/>
  <text x="200" y="186" font-size="12">아니오</text>
  <rect x="20" y="206" width="330" height="48" rx="8" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="185" y="227" text-anchor="middle" font-size="13">앱이 OS보다 자주 바뀌거나</text>
  <text x="185" y="245" text-anchor="middle" font-size="13">팀 · 의존성 스택이 여럿인가?</text>
  <rect x="400" y="206" width="260" height="48" rx="4" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="530" y="227" text-anchor="middle" font-size="12">A/B 단일 이미지가 더 단순</text>
  <text x="530" y="245" text-anchor="middle" font-size="12">(L4만으로 충분)</text>
  <line x1="350" y1="230" x2="398" y2="230" stroke="currentColor" marker-end="url(#ah14)"/>
  <text x="374" y="222" text-anchor="middle" font-size="12">아니오</text>
  <line x1="185" y1="254" x2="185" y2="300" stroke="currentColor" marker-end="url(#ah14)"/>
  <text x="200" y="282" font-size="12">예</text>
  <rect x="20" y="302" width="640" height="56" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="325" text-anchor="middle" font-size="13" font-weight="bold">하이브리드: 불변 A/B OS + 컨테이너 앱 (그림 9)</text>
  <text x="340" y="345" text-anchor="middle" font-size="12">rootful 엔진 · 최소 권한 · --device/CDI · Quadlet · healthcheck 롤백</text>
</svg>
```
그림 10 — 장비에 컨테이너를 쓸지 정하는 순서. 위의 두 질문이 "아니오/예"로 걸리면 컨테이너보다 다른 수단이 먼저다

---

## 15. 커널 안에서는 무슨 일이 일어나나

컨테이너 런타임이 부르는 syscall은 몇 개 안 된다: `clone3`/`unshare`/`setns`, `mount`(overlay, proc, bind), `pivot_root`, cgroupfs 파일 쓰기, `capset`/`prctl`, `seccomp`, `bpf`(device 프로그램), `execve`. 각각이 커널의 어떤 구조체로 이어지는지 따라가 보자(함수 이름은 6.18 기준, 버전에 따라 조금씩 다르다).

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah15" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="250" y="14" width="180" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="39" text-anchor="middle" font-size="14" font-weight="bold">task_struct</text>
  <line x1="290" y1="54" x2="110" y2="94" stroke="currentColor" marker-end="url(#ah15)"/>
  <line x1="320" y1="54" x2="280" y2="94" stroke="currentColor" marker-end="url(#ah15)"/>
  <line x1="360" y1="54" x2="430" y2="94" stroke="currentColor" marker-end="url(#ah15)"/>
  <line x1="400" y1="54" x2="590" y2="94" stroke="currentColor" marker-end="url(#ah15)"/>
  <rect x="10" y="96" width="200" height="120" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="118" text-anchor="middle" font-size="13" font-weight="bold">nsproxy</text>
  <text x="110" y="140" text-anchor="middle" font-size="12">uts_ns · ipc_ns · mnt_ns</text>
  <text x="110" y="160" text-anchor="middle" font-size="12">pid_ns_for_children</text>
  <text x="110" y="180" text-anchor="middle" font-size="12">net_ns · cgroup_ns</text>
  <text x="110" y="200" text-anchor="middle" font-size="12">time_ns</text>
  <rect x="220" y="96" width="120" height="120" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="280" y="118" text-anchor="middle" font-size="13" font-weight="bold">cred</text>
  <text x="280" y="140" text-anchor="middle" font-size="12">uid · gid</text>
  <text x="280" y="160" text-anchor="middle" font-size="12">user_ns</text>
  <text x="280" y="180" text-anchor="middle" font-size="12">cap_effective</text>
  <text x="280" y="200" text-anchor="middle" font-size="12">cap_bset</text>
  <rect x="350" y="96" width="160" height="120" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="430" y="118" text-anchor="middle" font-size="13" font-weight="bold">cgroups → css_set</text>
  <text x="430" y="140" text-anchor="middle" font-size="12">memory css (memcg)</text>
  <text x="430" y="160" text-anchor="middle" font-size="12">cpu css (task_group)</text>
  <text x="430" y="180" text-anchor="middle" font-size="12">pids css</text>
  <text x="430" y="200" text-anchor="middle" font-size="12">cpuset · io css</text>
  <rect x="520" y="96" width="150" height="120" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="595" y="118" text-anchor="middle" font-size="13" font-weight="bold">seccomp · fs</text>
  <text x="595" y="140" text-anchor="middle" font-size="12">seccomp.filter</text>
  <text x="595" y="160" text-anchor="middle" font-size="12">fs → root · pwd</text>
  <text x="595" y="180" text-anchor="middle" font-size="12">thread_pid → upid[]</text>
  <text x="595" y="200" text-anchor="middle" font-size="12">no_new_privs</text>
  <rect x="10" y="240" width="660" height="104" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="20" y="262" font-size="12">clone3 → copy_namespaces() · unshare → unshare_nsproxy_namespaces()</text>
  <text x="20" y="284" font-size="12">페이지 폴트 → memcg 과금(try_charge_memcg) → max 초과 → reclaim → mem_cgroup_out_of_memory()</text>
  <text x="20" y="306" font-size="12">CFS 대역폭: runtime 소진 → throttle_cfs_rq() → period 타이머가 다음 주기에 unthrottle</text>
  <text x="20" y="328" font-size="12">copy_process() → pids_can_fork() → -EAGAIN · open(/dev/x) → devcgroup BPF 검사 → -EPERM</text>
</svg>
```
그림 11 — 컨테이너의 꼬리표가 커널 자료구조의 어디에 붙어 있나. namespace는 `nsproxy`와 `cred`, 자원 한도는 `css_set`, 권한은 `cred`와 `seccomp`에 산다

### 15.1 namespace — `nsproxy`와 `ns_common`

- 모든 태스크는 `task_struct->nsproxy`로 namespace 묶음을 가리킨다. 같은 컨테이너의 프로세스들은 같은 `nsproxy`를 공유한다(참조 카운트). user namespace만은 `cred->user_ns`에 있다 — 권한 판단(`ns_capable()`)이 자격 증명과 붙어 있어야 하기 때문이다.
- `clone3(CLONE_NEW*)`는 `copy_process()` 안에서 `copy_namespaces()`를 불러 새 namespace 객체를 만든다. `unshare()`는 `unshare_nsproxy_namespaces()`로 자기 자신의 것을 바꾼다. `setns(fd)`는 `/proc/<pid>/ns/*`의 파일(nsfs)이 가리키는 `ns_common`으로 들어간다 — `podman exec`가 바로 이 경로다.
- PID namespace에서 한 태스크는 층마다 다른 번호를 갖는다. `struct pid`의 `numbers[]` 배열에 `struct upid { nr, ns }`가 층 수만큼 있어서, 호스트에서는 1234, 컨테이너 안에서는 1로 보인다(예제 1). 컨테이너의 PID 1이 죽으면 커널이 그 namespace의 나머지를 모두 `SIGKILL`한다(`zap_pid_ns_processes()`).
- 네트워크 namespace: 모든 `net_device`는 정확히 하나의 `struct net`에 속한다(`dev_net()`). `ip link set l8v1 netns l8a`는 `dev_change_net_namespace()`로 장치를 옮긴다. NIC 드라이버(I6)는 대개 namespace를 의식하지 않지만, 장치를 다른 namespace로 옮길 수 없게 표시하는 드라이버도 있다(6.18에서는 `net_device->netns_immutable` 비트, 예전 커널의 `NETIF_F_NETNS_LOCAL`).

### 15.2 cgroup — `css_set`과 컨트롤러 콜백

- `task_struct->cgroups`는 `css_set`을 가리키고, `css_set`은 컨트롤러마다 `cgroup_subsys_state`(css) 포인터를 들고 있다. `cgroup.procs`에 PID를 쓰면 `cgroup_attach_task()`가 태스크를 새 `css_set`으로 옮기고 각 컨트롤러의 `attach` 콜백을 부른다(cpu 컨트롤러는 `sched_move_task()`로 태스크를 다른 `task_group`의 run queue로 옮긴다).
- **memory**: 페이지를 할당할 때마다 그 페이지를 memcg에 과금한다(`__mem_cgroup_charge()` → `try_charge_memcg()`). `memory.max`를 넘으면 그 cgroup 안에서만 reclaim하고, 실패하면 `mem_cgroup_out_of_memory()`가 cgroup 안에서 희생자를 고른다 — dmesg의 `constraint=CONSTRAINT_MEMCG`(F11 §10). `memory.high`를 넘으면 유저로 돌아가는 길에 `__mem_cgroup_handle_over_high()`가 reclaim + 벌칙 sleep을 건다 — 예제 3의 "느려지지만 죽지 않는다".
- **cpu**: `cpu.max`는 CFS bandwidth control이다. 그룹의 `cfs_rq`가 주기(period)마다 quota만큼의 runtime을 받고, 다 쓰면 `throttle_cfs_rq()`로 run queue에서 통째로 빠진다. 주기 hrtimer가 돌아오면 다시 들어간다. 이 메커니즘은 **fair 클래스 전용**이라 SCHED_FIFO 태스크는 `cpu.max`에 걸리지 않는다(예제 6).
- **pids**: `copy_process()`가 `cgroup_can_fork()` → `pids_can_fork()`로 카운터를 올려 보고 한도를 넘으면 `-EAGAIN`을 돌려준다. fork bomb이 "Resource temporarily unavailable"로 멈추는 이유다.
- **devices**: cgroup v2에는 `devices.allow` 파일이 없다. 런타임이 `BPF_PROG_TYPE_CGROUP_DEVICE` 프로그램을 cgroup에 붙이고, 커널은 장치 파일을 열거나(`devcgroup_check_permission()`, `inode_permission()` 경로) `mknod`할 때 그 프로그램에 (type, major, minor, access)를 물어본다.

### 15.3 드라이버 쪽에서 보면

- **장치 cgroup 검사는 드라이버 `.open`보다 먼저다.** 예제 16에서 `-v /dev/video0:/dev/video0`로 노드만 넣으면 `open()`이 `EPERM`으로 실패하는데, 이때 V4L2 드라이버(I5)의 `v4l2_open()`은 불리지도 않는다. 문자 디바이스 경로(G4: `chrdev_open()` → `fops->open`)에 닿기 전에 VFS의 권한 검사에서 막힌다. "드라이버 로그에 아무것도 없는데 컨테이너에서 장치가 안 열린다"는 증상의 원인이 여기다.
- **드라이버는 namespace를 거의 모른다.** sysfs(G8)·`/dev` 노드는 호스트 전체에 하나다. 컨테이너에 장치를 준다는 것은 "그 노드를 컨테이너 mount namespace에 보이게 + device cgroup이 허용 + 파일 권한/UID 매핑이 맞게" 세 가지를 맞추는 일이다. GPU처럼 노드가 여럿이고 user-mode 라이브러리가 커널 드라이버 버전과 맞아야 하는 장치는 그래서 CDI 같은 "편집 목록"이 필요해진다(§11, O4).
- **mount·overlayfs**: 이미지 층은 VFS의 stacking 파일시스템(F12, J3 §7)이다. overlayfs는 upper에 쓸 때 copy-up을 하고, 그 비용은 큰 파일(모델 가중치)을 수정할 때 크게 나타난다. 모델 파일은 이미지 층에 두고 읽기만 하거나, data 볼륨으로 bind mount한다.

---

## 16. Embedded 실무에서 다시 보기

### 16.1 Jetson 기반 자율 로봇 — 무엇을 컨테이너에 넣고 무엇을 빼나

예를 들어 Orin 위의 순찰 로봇이라면:

| 구성 요소 | 위치 | 이유 |
|---|---|---|
| 커널, 드라이버, DT overlay, 펌웨어, GPU 커널 드라이버 | OS 층(A/B, L4) | 커널 ABI에 묶임. 컨테이너가 못 바꾼다 |
| 모터 전류 루프, 비상 정지 | 외부 MCU(O3) | 하드 RT, Linux가 죽어도 동작해야 함 |
| CAN 브리지·안전 감시(1~10 ms) | 호스트 systemd 서비스, `isolcpus` 코어, SCHED_FIFO | `cpu.max` 위험(§4), 부팅 직후 가장 먼저 떠야 함 |
| 인식(TensorRT), 플래너(ROS 2) | 컨테이너, CDI로 GPU·카메라, `cpu.weight` 높게, `cpuset` 분리 | 의존성이 무겁고 자주 바뀜 |
| 로그 업로더, 원격 관리, 웹 UI | 컨테이너, `cpu.max`·`memory.max`·`pids.max` 엄격 | 배경 작업, 고장 나도 로봇은 계속 |

### 16.2 서비스별 자원 예산을 실제 숫자로

```text
# /etc/containers/systemd/uploader.container  (가이드 — 키 이름은 podman-systemd.unit(5) 기준)
[Container]
Image=registry.local/robot/uploader@sha256:<digest>
Network=host
PodmanArgs=--memory 128m --memory-reservation 96m --cpus 0.3 --pids-limit 64 --cpuset-cpus 0-1
Volume=/data/logs:/logs:ro

[Service]
Restart=on-failure
StartLimitBurst=5
```

`--memory-reservation`은 cgroup v2에서 `memory.low`(보호량)로 바뀌는 옵션이다. 인식 컨테이너는 반대로 `--memory`를 넉넉히, `--cpuset-cpus 2-5`로 큰 코어를 주고 `--cpus`는 주지 않는다. 숫자는 장비에서 부하를 걸고 `memory.peak`·`cpu.stat`·`memory.events`를 읽어 정한다(예제 3·4의 파일들).

### 16.3 필드에서 "컨테이너가 이상하다" 디버깅 순서

1. `systemctl status <svc>` / `journalctl -u <svc>` — 종료 코드(예제 13의 `status=3`), OOM 여부.
2. `cat /sys/fs/cgroup/<경로>/memory.events` — `oom_kill`이 올랐나, `high`가 계속 오르나(§3.3).
3. `cat .../cpu.stat` — `nr_throttled`가 오르면 `--cpus`가 원인(§4).
4. `cat .../pids.events` — `max`가 오르면 스레드 수 한도.
5. `podman inspect <c>` → `HostConfig`, 실제 cgroup 파일과 대조 — rootless에서 한도가 무시됐을 수 있다(예제 11).
6. 장치: `ls -l /dev/...` 안팎 비교, `--device`/CDI 여부, 그룹 ID(예제 16).
7. `nsenter -t <pid> -n ip addr`, `nsenter -t <pid> -m ls /`로 호스트 도구를 들고 컨테이너 namespace에 들어간다 — 이미지에 디버그 도구를 넣지 않아도 된다.

### 16.4 커널 설정 체크리스트 (BSP 커널을 만들 때)

컨테이너 엔진이 요구하는 커널 옵션은 BSP 기본 defconfig에 빠져 있는 경우가 많다. 이 노트에서 실제로 걸린 것은 `NETFILTER_XT_TARGET_MASQUERADE`였다(§7.3).

- namespace: `NAMESPACES`, `UTS_NS`, `IPC_NS`, `PID_NS`, `NET_NS`, `USER_NS`, `CGROUPS` + `CGROUP_*`
- cgroup 컨트롤러: `MEMCG`, `CGROUP_SCHED` + `CFS_BANDWIDTH`, `CGROUP_PIDS`, `CPUSETS`, `BLK_CGROUP`, `CGROUP_BPF`(device 제어)
- 파일시스템: `OVERLAY_FS`, `TMPFS`, (rootless 옛 방식) `FUSE_FS`
- 네트워크: `VETH`, `BRIDGE`, `BRIDGE_NETFILTER`, `NF_NAT`, nftables + 엔진이 쓰는 방식(xt 타깃 또는 nft masquerade)
- 보안: `SECCOMP`, `SECCOMP_FILTER`, `SECURITY_APPARMOR` 또는 SELinux, `KEYS`
- 확인 도구: moby 저장소의 `contrib/check-config.sh`를 BSP 커널 `.config`에 돌린다(가이드 — 이 노트에서 실행 안 함)

---

## 17. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 제어 루프 컨테이너에 `--cpus` | 평균 CPU는 낮은데 가끔 수십~수백 ms 지연, deadline miss | `cpu.max` throttling(예제 6) | `--cpus` 제거, `cpu.weight`·`cpuset`·isolcpus, RT 스레드는 SCHED_FIFO |
| 카메라 안 열려서 `--privileged` | 일단 동작 → 보안 감사에서 지적, 실수로 디스크 덮어쓰기 | 모든 장치·capability 허용 | `--device`/CDI로 노드만, 필요한 capability만 |
| 노드만 `-v`로 bind | `open: Operation not permitted`, 드라이버 로그 없음 | device cgroup 거부(예제 16) | `--device` 사용 |
| rootless + 사용자 D-Bus 없음 | `--memory`가 효과 없음, OOM이 시스템 전체로 | cgroupfs 폴백, 위임 없음(예제 11) | `dbus-user-session` + linger + `systemctl --user`, 또는 rootful |
| BSP 커널에 xt MASQUERADE 없음 | `netavark: unable to append rule ... MASQUERADE` | 엔진이 iptables-nft 경로 사용(§7.3) | 커널 옵션 추가, nftables 드라이버 netavark, 또는 `--internal`/host 네트워크 |
| `pids-limit`을 프로세스 수로 착각 | 스레드 많은 노드가 시작 중 `pthread_create` 실패 | pids는 스레드도 센다 | 스레드 수 측정 후 여유 있게 |
| 엔진 저장소를 rootfs에 | 읽기전용 rootfs에서 pull 실패, A/B 전환 후 이미지 사라짐 | `/var/lib/containers`가 슬롯마다 따로 | data 파티션으로 이동(§13) |
| 오래된 이미지 GC 안 함 | 몇 달 뒤 data 파티션 100%, 로그·업데이트 실패 | 층 누적 | 업데이트 확정 후 prune, 직전 버전 1개만 유지 |
| JetPack 올린 뒤 GPU 컨테이너 그대로 | `libcuda` 초기화 실패, CUDA 에러 | user-mode 드라이버와 커널 드라이버 버전 불일치 | OS ↔ 앱 호환표, L4T 태그에 맞춘 이미지 재빌드 |
| 셸 스크립트를 PID 1로 | `podman stop`이 10초 걸리고 SIGKILL, 좀비 누적 | PID 1은 기본 시그널 동작이 없음, 자식 수거 안 함 | `--init` 또는 앱이 SIGTERM 처리 |

---

## 18. 면접에서 이렇게 말한다

**Q.** "What actually is a container on Linux? How is it different from a VM?"

**A.** 컨테이너는 커널이 꼬리표를 붙인 평범한 프로세스다. namespace가 보이는 자원을, cgroup이 쓰는 양을, overlayfs와 pivot_root가 파일 트리를, capability·seccomp·LSM이 권한을 제한한다. 커널은 호스트와 공유하고 게스트 커널이 없다. VM은 하이퍼바이저 위에 별도 커널을 띄워 격리가 더 강하지만 무겁다. 50줄 C로 clone + overlay + pivot_root + cgroup 쓰기만 해도 컨테이너가 된다는 걸 직접 짜 봤다.

> A container is just a Linux process with extra kernel attributes: namespaces control what it can see, cgroups control how much it can use, an overlayfs root plus pivot_root controls which files it sees, and capabilities, seccomp and an LSM profile limit what it can do. There is no guest kernel — it shares the host kernel, which is why it starts fast and is cheap, but also why a kernel bug breaks the isolation. A VM runs its own kernel on a hypervisor: stronger isolation, more memory and boot time. I've built a minimal container in about fifty lines of C — clone with new namespaces, mount an overlay, pivot_root, write the PID into a cgroup — and that's essentially what runc or crun does.

**꼬리 질문**: "Which namespaces does Podman create by default?" → 이 게스트의 rootful 기본값은 pid, net, ipc, uts, mnt, cgroup. user와 time은 공유(예제 10). rootless면 user namespace가 추가된다.

**Q.** "Should we ship our robot software as containers?"

**A.** 전부 말고 일부만이다. 커널·드라이버·GPU 커널 드라이버·안전 감시 서비스는 불변 A/B OS 층에 두고 OTA 도구로 원자적으로 바꾼다. 자주 바뀌고 의존성이 무거운 인식·플래닝·클라우드 에이전트는 컨테이너로 data 파티션에 두고 층 단위로 업데이트하며 healthcheck 실패 시 이전 다이제스트로 롤백한다. 하드 RT 루프는 MCU나 isolcpus 코어의 호스트 프로세스에 둔다. RAM이 작거나 앱이 하나면 컨테이너 없이 systemd 격리 옵션으로 충분하다.

> Partly. I'd use a hybrid: an immutable A/B root filesystem for the kernel, drivers, GPU driver stack, the container engine and the safety-critical services, updated atomically with RAUC or Mender and rolled back by the bootloader. On top of that, the fast-moving, dependency-heavy pieces — perception, planning, telemetry — ship as containers on the data partition, pinned by digest, started by systemd via Quadlet, and rolled back automatically if the healthcheck fails. Hard real-time loops stay on an MCU or on isolated cores outside any CPU quota. And if the device is small or runs a single app, I'd skip the engine and use systemd's own sandboxing and resource controls.

**꼬리 질문**: "What does the container engine cost us?" → 실측: 컨테이너당 conmon 약 2 MB, 상주 데몬 없음(Podman). 시작 시간은 엔진 쪽이 지배적이라 부팅 예산에 넣어야 한다(예제 14·15). 진짜 비용은 앱마다 별도 userspace와 flash의 층 저장 공간.

**Q.** "A real-time thread inside a container misses its deadlines. Why, and how do you fix it?"

**A.** 첫 용의자는 `--cpus`, 즉 cgroup `cpu.max`다. 주기 예산을 다 쓰면 그룹 전체가 그 주기 끝까지 run queue에서 빠진다. 10 ms 주기·3 ms 일 루프를 20% 한도로 돌리니 300주기 중 291개를 놓쳤고, 50% 한도에서는 0개였다. 둘째는 같은 코어의 시끄러운 이웃 — cpuset으로 코어를 나누고 isolcpus·nohz_full·IRQ affinity로 RT 코어를 비운다. 셋째는 스케줄링 정책 — SCHED_FIFO를 쓰려면 CAP_SYS_NICE와 rtprio ulimit가 필요하다. CFS bandwidth는 fair 클래스 전용이라 FIFO 스레드는 cpu.max에 안 걸린다는 것도 측정했다. 마지막으로 mlockall, PREEMPT_RT 여부, cyclictest로 커널 지연을 따로 본다.

> The usual culprit is a CPU quota — `--cpus` maps to cgroup v2 `cpu.max`, which is CFS bandwidth control: once the group burns its quota for the period, all its threads are throttled until the next period, so a periodic loop sees tens of milliseconds of stall even when average usage is low. I measured it: a 10 ms loop doing 3 ms of work missed 291 of 300 deadlines under a 20 % quota and none under 50 %, and `cpu.stat` showed the throttle counts. The fixes are: no quota on latency-critical containers — use `cpu.weight` instead — pin them with cpusets and isolate those cores with isolcpus, nohz_full and IRQ affinity, run the thread as SCHED_FIFO with CAP_SYS_NICE and an rtprio limit, lock memory, and then verify kernel latency with cyclictest under load. Note that CFS bandwidth doesn't apply to SCHED_FIFO tasks at all, and `--cpu-rt-runtime` needs RT_GROUP_SCHED, which most kernels don't enable.

**꼬리 질문**: "Then why not just make everything SCHED_FIFO?" → RT 태스크가 폭주하면 같은 코어의 모든 일반 태스크가 굶는다(6.12+ 커널은 fair_server가 최소 몫을 보장하지만 그만큼 RT 지연이 생긴다 — F1 §5). 우선순위 설계와 코어 분리가 먼저다.

**Q.** "How does a GPU get into a container on Jetson?"

**A.** 원리는 일반 장치와 같다: 장치 노드가 컨테이너 /dev에 보여야 하고, device cgroup이 그 major/minor를 허용해야 하고, 권한이 맞아야 한다. GPU는 여기에 user-mode 드라이버 라이브러리가 호스트 커널 드라이버 버전과 맞아야 한다는 조건이 붙는다. NVIDIA Container Toolkit이 L4T의 목록으로 노드·라이브러리·hook을 모아 CDI 명세를 만들고, `--device nvidia.com/gpu=all`로 넣는다. 랩에서는 vivid 카메라로 같은 메커니즘을 확인했다 — 노드만 bind하면 open이 EPERM, `--device`나 CDI면 성공. 그래서 JetPack을 올리면 GPU 컨테이너도 같이 검증해야 한다.

> Same three gates as any device: the node has to exist in the container's /dev, the device cgroup — a BPF program on the container's cgroup in v2 — has to allow that major/minor, and the permissions or group IDs have to line up. For the Jetson iGPU there's a fourth: the user-space driver libraries must match the host's kernel driver, so they come from the host. The NVIDIA Container Toolkit reads the L4T lists of device nodes and libraries and generates a CDI spec, and the engine applies it with something like `--device nvidia.com/gpu=all`. I reproduced the mechanism with a vivid camera: bind-mounting the node alone gives EPERM on open because the device cgroup denies it, while `--device` or my own CDI spec works. The practical consequence is that a JetPack upgrade is also a container upgrade — we keep an OS-to-image compatibility matrix.

**Q.** "Explain cgroup v2 memory.max versus memory.high."

**A.** max는 하드 한도라 넘으면 그 그룹 안에서 reclaim하고 실패하면 그룹 안에서 OOM kill한다 — dmesg에 CONSTRAINT_MEMCG로 보이고 다른 서비스는 무사하다. high는 throttle 지점이라 넘으면 reclaim + 벌칙 sleep으로 느려질 뿐 죽지 않는다. swap 없는 장비에서 high만 걸면 서비스가 멈춘 것처럼 갇힌다는 걸 실측했다. 보통 high를 max보다 약간 낮게 두어 조기 경보 + 완만한 감속으로 쓰고, memory.events로 감시한다.

> `memory.max` is the hard limit: allocation beyond it triggers reclaim within the group and, if that fails, an OOM kill scoped to that cgroup — the kernel log says CONSTRAINT_MEMCG and nothing outside is touched. `memory.high` is a throttle point: the kernel reclaims and makes the offending tasks sleep, but never kills. On a swapless device, anonymous memory can't be reclaimed, so a group over `high` just stalls — I saw it freeze at 32 MiB until a timeout. I set `high` a bit below `max` for early back-pressure and watch `memory.events` for the high, max and oom_kill counters.

**Q.** "What's the difference between Docker and Podman, and which would you put on a device?"

**A.** 둘 다 OCI 이미지와 runc/crun을 쓴다. Docker는 dockerd + containerd 상주 데몬이 컨테이너를 소유하고, Podman은 데몬 없이 컨테이너마다 conmon 하나만 남는다. 장비에서는 데몬 단일 장애점이 없고 systemd(Quadlet)와 바로 맞물리는 Podman이 편하다. 다만 BSP나 고객 도구가 Docker를 전제하면(Jetson 기본 이미지처럼) 그대로 쓰되 같은 원칙 — 최소 권한, 다이제스트 고정, data 파티션 저장소 — 을 적용한다.

> Both use OCI images and an OCI runtime underneath. Docker runs a long-lived daemon, dockerd on top of containerd, which owns the containers; Podman is daemonless — each container just has a small conmon monitor, which I measured at about 2 MB. On a device I prefer Podman because there's no daemon to crash or to budget memory for, and Quadlet turns a container into a normal systemd service with restart policies, dependencies and journald logging. If the BSP or the customer's tooling assumes Docker, as many Jetson images do, I'd keep it and apply the same rules: least privilege, digests not tags, storage on the data partition.

**Q.** "How do you keep containers from filling the flash or wearing it out?"

**A.** 층 공유를 설계한다 — 공통 베이스, 자주 바뀌는 것은 맨 위 층. v1/v2 실험에서 이미지 합계 37 MB가 디스크에는 15 MB였고 v2는 3 kB 층만 달랐다. 업데이트 확정 후 prune하고 직전 버전 하나만 남긴다. 컨테이너 로그 크기 제한, 쓰기층에 큰 파일 쓰지 않기(데이터는 volume), 하루 쓰기량으로 eMMC 수명 계산. 오프라인 현장은 save/load나 OTA 번들로.

> Design the layers so that sharing does the work: one common base, heavy runtimes next, the app on top. In my test, three images totalling 37 MB took 15 MB on disk, and the v2 update differed by a single 3 kB layer. Then housekeeping: prune old images after an update is confirmed while keeping one rollback version, cap container log sizes, keep writes out of the overlay upper layer by using volumes for data, and budget daily write volume against the eMMC's endurance. For sites without registry access, ship `podman save` archives or embed images in the OTA bundle.

---

## 19. 직접 해보기

**문제 1 — 개념**: `unshare --pid sleep 100`(`--fork` 없이)을 실행하면 무슨 일이 일어나는가? `/proc/self/ns/pid`와 `pid_for_children`이 왜 따로 있는가?

정답/힌트: 호출자는 옛 PID namespace에 남고 자식부터 새 namespace에 들어간다. `--fork` 없이 셸을 쓰면 첫 자식이 PID 1이 되고 그게 끝나면 이후 fork가 실패한다(예제 1·2.6).

**문제 2 — cgroup 관찰**: 예제 3을 `memory.oom.group=1`로 바꾸고, 그룹 안에 `sleep` 두 개와 `l8_eat`를 같이 넣어 OOM이 났을 때 누가 죽는지 `memory.events`의 `oom_group_kill`로 확인하라.

정답/힌트: oom.group이 1이면 그룹 전체가 죽고 `oom_group_kill`이 1 오른다(F11 §10).

**문제 3 — 지연 실험**: 예제 6에서 `cpu.max`를 "30000 100000"(수요와 같음), "35000 100000"으로 바꿔 miss 수를 재고, `cpu.max.burst`를 써서 결과가 어떻게 바뀌는지 보라.

정답/힌트: 수요에 딱 맞는 한도는 측정 잡음·커널 오버헤드 때문에 여전히 놓친다. burst는 쓰지 않고 남긴 예산을 다음 주기에 빌려 쓰게 해 순간 몰림을 흡수한다.

**문제 4 — 코드 과제**: 예제 8의 `l8_mini`에 (a) `CLONE_NEWCGROUP`(그룹에 넣은 뒤 `unshare`로) (b) `/sys` 읽기전용 마운트 (c) `CAP_SYS_ADMIN`을 bounding set에서 빼기(`prctl(PR_CAPBSET_DROP, ...)`)를 추가하라. 컨테이너 안에서 `/proc/self/cgroup`이 `0::/`로, `/sys/class/net`이 `lo`만, `mount`가 `EPERM`이 되는지 확인하라.

정답/힌트: cgroup namespace의 루트는 `unshare` 시점의 cgroup이다. 그래서 부모가 PID를 그룹에 넣은 뒤 자식이 `unshare(CLONE_NEWCGROUP)`해야 한다.

**문제 5 — Podman 매핑**: `podman run --cpu-shares 512 --memory-reservation 32m --cpuset-cpus 1 ...`을 띄우고 `cpu.weight`, `memory.low`, `cpuset.cpus` 값을 읽어 표 예제 10에 행을 추가하라.

정답/힌트: cgroup v2에서 shares는 weight로 환산된다. 환산식이 직관과 다르니(crun은 shares 2~262144를 weight 1~10000 구간으로 선형 변환한다고 알고 있다 — 기억 기반) 실제 값을 직접 읽어서 확인할 것.

**문제 6 — 장치 관찰**: 예제 16의 `--device /dev/video0` 컨테이너를 띄운 상태에서 호스트의 `/sys/fs/cgroup/machine.slice/libpod-<id>.scope/container/`에 device 관련 파일이 있는지 찾아보라. 없다면 허용 목록은 어디에 있는가?

정답/힌트: v2에는 `devices.*` 파일이 없다. 허용 목록은 `config.json`의 `linux.resources.devices`에 있고, 커널에는 cgroup에 붙은 `BPF_PROG_TYPE_CGROUP_DEVICE` 프로그램으로 존재한다(bpftool이 있다면 `bpftool cgroup list <경로>`로 볼 수 있다).

**문제 7 — 설계**: RAM 2 GB, eMMC 16 GB, LTE 월 1 GB 데이터 한도인 순찰 로봇에서 인식 모델(300 MB)을 매주 바꾸려 한다. 층 구조, 다이제스트 고정, 롤백, GC 정책을 한 쪽으로 써 보라.

정답/힌트: 모델을 별도 층(또는 이미지 밖 data 볼륨 + 서명 검증)으로 분리, 베이스·런타임 층 고정, 주 1회 300 MB × 4 = 1.2 GB라 LTE 한도를 넘으니 모델 delta·압축(zstd)·Wi-Fi 기지에서만 받기 정책이 필요하다는 결론까지 가야 한다.

---

## 20. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| container | 컨테이너 | namespace·cgroup·별도 rootfs·권한 축소가 붙은 프로세스 묶음. 커널은 호스트와 공유 |
| namespace | 이름 공간 | PID·mount·net·uts·ipc·user·cgroup·time 자원을 프로세스 묶음마다 따로 보이게 하는 커널 기능 |
| cgroup v2 | 컨트롤 그룹 v2 | `/sys/fs/cgroup` 단일 계층에서 프로세스 묶음의 CPU·메모리·pids·I/O를 제한·회계 |
| controller | 컨트롤러 | cgroup의 자원 종류별 모듈(cpu, memory, pids, io, cpuset). `cgroup.subtree_control`로 자식에게 켠다 |
| CFS bandwidth | CFS 대역폭 제어 | `cpu.max`의 구현. 주기마다 quota만큼만 실행, 다 쓰면 throttle |
| memcg | memory cgroup | 메모리 과금·한도·OOM을 cgroup 단위로 |
| overlayfs | 오버레이 파일시스템 | 읽기전용 lower 층들 위에 쓰기 upper를 겹치는 stacking FS (J3 §7) |
| pivot_root | 루트 교체 syscall | 현재 mount namespace의 `/`를 새 디렉터리로 바꾸고 옛 루트를 떼어 낼 수 있게 한다 |
| OCI | Open Container Initiative | 이미지·런타임·배포 형식 표준 |
| bundle | OCI 번들 | `config.json` + `rootfs/`. OCI runtime의 입력 |
| runc · crun | OCI runtime | bundle을 받아 컨테이너 프로세스를 만들고 빠지는 저수준 도구(Go · C) |
| conmon | container monitor | Podman이 컨테이너마다 두는 작은 감시 프로세스(로그·종료 코드) |
| containerd · shim | 컨테이너 데몬 · 감시자 | Docker·k3s 아래의 이미지·수명 관리 데몬과 컨테이너별 감시 프로세스 |
| netavark | Podman 네트워크 도구 | bridge·veth·방화벽 규칙(NAT) 설정. aardvark-dns가 컨테이너 이름 DNS |
| rootless | 비특권 컨테이너 | 일반 사용자가 user namespace로 띄우는 컨테이너 |
| subuid · subgid | 보조 UID 범위 | rootless 컨테이너 안 UID 1~65535를 매핑할 호스트 UID 구간(`/etc/subuid`) |
| Quadlet | Podman systemd 생성기 | `.container` 파일을 부팅 때 `.service` unit으로 변환 |
| CDI | Container Device Interface | 장치를 컨테이너에 넣기 위한 "편집 목록"(노드·mount·env·hook) JSON/YAML 표준 |
| NVIDIA Container Toolkit | NVIDIA 컨테이너 도구 | GPU 장치 노드·드라이버 라이브러리를 컨테이너에 넣는 도구(`nvidia-ctk`, CDI 생성) |
| layer · digest | 이미지 층 · 다이제스트 | 파일 변경분 tar와 그 sha256 이름. 같은 다이제스트는 한 번만 저장 |
| `--privileged` | 특권 컨테이너 | 모든 capability·장치, seccomp/LSM 해제. 사실상 호스트 root |
| isolcpus · cpuset | CPU 격리 · CPU 집합 | 특정 코어를 일반 스케줄링에서 빼거나(부트 인자), cgroup이 쓸 코어를 고정 |

---

## 21. 요약 & 체크리스트

컨테이너는 VM이 아니라 **커널이 꼬리표를 붙인 평범한 프로세스**다. namespace가 "무엇이 보이나"를, cgroup v2가 "얼마나 쓰나"를, overlayfs + `pivot_root`가 "어떤 파일이 보이나"를, capability·seccomp·LSM·user namespace가 "무엇을 할 수 있나"를 정한다. OCI 표준 덕분에 Podman·Docker·k3s는 같은 이미지와 같은 runtime(runc/crun)을 쓰고, 장비에서는 데몬 없는 Podman + Quadlet이 systemd와 가장 자연스럽게 맞물린다. Embedded에서의 득은 의존성 분리·앱 단위 빠른 업데이트·서비스별 자원 예산이고, 실은 flash·RAM·시작 시간, `cpu.max` throttling이 만드는 지연 스파이크, 장치 접근의 복잡도, `--privileged` 같은 보안 실수다. 제품 구조는 대개 **불변 A/B OS(커널·드라이버·GPU 드라이버) + data 파티션 위 컨테이너 앱(healthcheck 롤백)** 의 하이브리드이고, 하드 RT 루프·작은 장비·결정론적 부팅이 걸리면 컨테이너 밖에 둔다.

- [ ] 컨테이너와 VM과 chroot의 차이를 커널 공유 여부와 격리 수단으로 설명할 수 있다
- [ ] 8가지 namespace가 각각 무엇을 분리하는지 말하고 `unshare`·`lsns`·`/proc/<pid>/ns`로 확인할 수 있다
- [ ] veth pair로 두 network namespace를 잇고 ping으로 확인할 수 있다
- [ ] cgroup v2에서 `memory.max`·`memory.high`·`cpu.max`·`pids.max`를 걸고 `memory.events`·`cpu.stat`·`pids.events`를 해석할 수 있다
- [ ] `cpu.max`가 주기적 제어 루프의 지연을 어떻게 망가뜨리는지, 왜 SCHED_FIFO에는 안 걸리는지 측정으로 설명할 수 있다
- [ ] namespace + overlayfs + `pivot_root` + cgroup으로 최소 컨테이너를 C로 짤 수 있다
- [ ] OCI image/runtime spec과 runc·crun·conmon·containerd·Podman·Docker의 역할을 그림으로 설명할 수 있다
- [ ] 이미지를 오프라인으로 import·save·load하고 층 공유로 저장 공간이 어떻게 절약되는지 보여 줄 수 있다
- [ ] `podman run --memory/--cpus/--pids-limit`이 어느 cgroup 파일의 어떤 값이 되는지 말할 수 있다
- [ ] rootless 컨테이너의 `uid_map`을 읽고 rootful과의 차이를 설명할 수 있다
- [ ] Quadlet `.container` 파일로 부팅 시 시작·실패 시 재시작·healthcheck를 구성할 수 있다
- [ ] `--device`·CDI·device cgroup으로 장치(카메라, Jetson GPU)가 컨테이너에 들어가는 경로를 설명하고 `--privileged`를 피하는 법을 말할 수 있다

---

## 참고 자료

- man7.org: [namespaces(7)](https://man7.org/linux/man-pages/man7/namespaces.7.html), [cgroups(7)](https://man7.org/linux/man-pages/man7/cgroups.7.html), [user_namespaces(7)](https://man7.org/linux/man-pages/man7/user_namespaces.7.html), [pid_namespaces(7)](https://man7.org/linux/man-pages/man7/pid_namespaces.7.html), [network_namespaces(7)](https://man7.org/linux/man-pages/man7/network_namespaces.7.html), [unshare(1)](https://man7.org/linux/man-pages/man1/unshare.1.html), [pivot_root(2)](https://man7.org/linux/man-pages/man2/pivot_root.2.html), [clone(2)](https://man7.org/linux/man-pages/man2/clone.2.html), [lsns(8)](https://man7.org/linux/man-pages/man8/lsns.8.html)
- 커널 문서: [Control Group v2](https://docs.kernel.org/admin-guide/cgroup-v2.html), [CFS Bandwidth Control](https://docs.kernel.org/scheduler/sched-bwc.html), [Overlay Filesystem](https://docs.kernel.org/filesystems/overlayfs.html), [Seccomp BPF](https://docs.kernel.org/userspace-api/seccomp_filter.html)
- 커널 소스(elixir.bootlin.com, v6.18): `kernel/nsproxy.c`, `kernel/pid_namespace.c`, `kernel/cgroup/cgroup.c`, `kernel/cgroup/pids.c`, `mm/memcontrol.c`, `kernel/sched/fair.c`(`throttle_cfs_rq`), `security/device_cgroup.c`, `fs/namespace.c`(`pivot_root`), `fs/overlayfs/`
- OCI: [opencontainers/image-spec](https://github.com/opencontainers/image-spec), [opencontainers/runtime-spec](https://github.com/opencontainers/runtime-spec), [opencontainers/distribution-spec](https://github.com/opencontainers/distribution-spec)
- Podman: [podman-run(1)](https://docs.podman.io/en/latest/markdown/podman-run.1.html), [podman-systemd.unit(5) — Quadlet](https://docs.podman.io/en/latest/markdown/podman-systemd.unit.5.html), [podman-auto-update(1)](https://docs.podman.io/en/latest/markdown/podman-auto-update.1.html)
- CDI: [cncf-tags/container-device-interface](https://github.com/cncf-tags/container-device-interface)
- NVIDIA: [NVIDIA Container Toolkit 문서](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/index.html) (Jetson 지원 범위는 JetPack 버전별 문서 확인)
- Michael Kerrisk, LWN "Namespaces in operation" 연재(2013, 1편부터) — namespace 개념의 고전 해설
- Kerrisk, **The Linux Programming Interface** — 28장(프로세스 생성 상세), 상위 개념 보충용
