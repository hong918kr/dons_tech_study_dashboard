# Embedded Linux 스터디 노트 — 작성 스펙

## 목적
Don이 **Embedded Linux / 커널 드라이버 인터뷰를 통과하고, 입사 후 실무를 바로 해내기** 위해 공부하는 노트 세트다.
전체 목차(나침반)는 `contents.md`. 각 노트는 그 목차의 항목 하나(예: `A3`)를 **교과서처럼 자세하고 쉽게** 푼다.
Don은 노트를 **처음부터 끝까지 읽으면서 공부**한다. 요약본이 아니다. "최대한 자세하고 쉽게, 예제 코드와 그림을 다 포함해서"가 Don의 요청이다.

- 출력: `<모듈폴더>/YYYY-MM-DD_<ID>_<topic>.md` (예: `A_userspace/2026-10-05_A3_file_io_device_access.md`)
- HTML: `python3 build_site.py` → `site/index.html` (노트 작성자는 HTML을 만들지 않는다)

## 독자 — Don
- 임베디드 펌웨어 약 8년. SK hynix/Solidigm 엔터프라이즈 SSD 양산 펌웨어(bare-metal C, ARM Cortex-R/M, NVMe/PCIe, NAND, DMA, 에러 처리, 성능 튜닝), 현재 Apple에서 RF 칩셋 통합(새 실리콘 bring-up, PCIe/I2C/SPI/SPMI/RFFE 버스 장애 root cause, scope·로직 분석기·JTAG 디버그).
- 잘 아는 것: C, 포인터·메모리 레이아웃, 비트 조작, 레지스터·인터럽트·DMA·캐시, 링버퍼, RTOS 수준의 동시성, 측정 기반 디버깅.
- 약한 것: **Linux 자체** — 유저스페이스 시스템 프로그래밍, 커널 내부, 드라이버 프레임워크, Device Tree, 빌드 시스템. 리눅스 셸은 쓰지만 깊이가 없다.
- 그래서 **Don이 아는 bare-metal 개념과 연결해서** 설명한다. 예: "프로세스 = MPU로 격리된 태스크 + 자기만의 주소 공간", "syscall = SVC 예외로 특권 모드 진입", "fd = 커널이 들고 있는 핸들 테이블의 인덱스", "page fault = MMU abort를 커널이 정상 경로로 처리하는 것", "epoll = 여러 인터럽트 소스를 한 번에 기다리는 이벤트 그룹(RTOS event flags)".

## 노트의 방향 — "유저 → 커널 → 드라이버"로 연결
모듈 A~B는 유저스페이스지만, Don의 최종 목표는 **드라이버**다. 그래서 각 노트에 반드시
**"커널 안에서는 무슨 일이 일어나나"** 절을 둔다: 유저가 호출한 API가 커널의 어떤 구조체·함수(`task_struct`, `struct file`, `file_operations`, `wait_queue`, `vm_area_struct` 등)로 이어지고, **드라이버를 쓸 때 이 지식이 어디서 다시 나오는지**(예: `read()` → 드라이버의 `.read`, `poll()` → 드라이버의 `.poll` + `poll_wait`, `mmap()` → `.mmap` + `remap_pfn_range`). 깊은 설명은 해당 모듈 노트(F, G, H …)로 미루고 ID로 참조만 한다.

## 실무·면접 맥락 (예시 소재)
대표 시나리오: 로봇/드론/차량용 센서 키트를 Linux SoC(예: NXP i.MX8, TI AM62, NVIDIA Jetson Orin)로 돌리는 장비 — IMU(I2C/SPI), 카메라(V4L2), GNSS(UART), LiDAR(UDP), CAN 모터 컨트롤러. Scout AI(국방 로보틱스)·Verkada(카메라) 같은 회사의 Embedded Linux 엔지니어 업무.
예시는 "예를 들어 센서 키트라면" 식으로 쓴다.

## 톤
- **한국어 기본**, 기술 용어는 영어 그대로 (file descriptor, syscall, page fault, context switch…). 설명체(~다). 존댓말 금지.
- **쉽고 친절하게**. 새 용어는 처음 나올 때 한 문장으로 풀어 준다.
- 한 절의 흐름: **직관(그림·비유) → 정의 → 작은 예제 → 코드로 확인(실제 실행 출력) → 커널/드라이버와 연결 → Don 경험과 연결 → 흔한 함정**.
- 추상 설명만 길게 쓰지 않는다. 직접 돌려 보는 명령과 출력이 많아야 한다.

## 마크다운 규칙 (빌더가 지원하는 것만)
1. `#`~`###` 제목, 문단, `-`/`1.` 목록(**중첩 금지**), 표(한 행 한 줄, 셀 안 줄바꿈은 `<br>`), `> ` 인용, 코드펜스, `---`, `- [ ]` 체크박스, 인라인 `` `코드` `` `**굵게**` `[링크](url)`.
2. **raw HTML 금지** (단 표 셀 안 `<br>`, 그리고 `svg` 펜스는 허용).
3. **표 셀 안에 `|` 문자 금지** (빌더가 이스케이프 못 함). 파이프 명령은 표 밖 코드 블록에 쓴다. 셀 안에서는 "파이프" 또는 `→`로 표현.
4. 본문에 별표 `*`를 그대로 쓰지 말 것(이탤릭으로 해석됨). 포인터·곱셈·glob은 인라인 코드 안에 쓴다 (`char *p`, `*.c`).
5. 코드 블록 언어: ```` ```c ````, ```` ```sh ````(명령), ```` ```text ````(출력), ```` ```python ````. 다이어그램은 언어 없는 ```` ``` ````.

## 그림 · 차트 (필수 — 노트당 SVG 4개 이상)
- **한글이 들어가는 다이어그램은 반드시 SVG로 그린다.** 고정폭 글꼴에서 한글 폭이 영문 2칸과 정확히 맞지 않아 ASCII 박스·화살표가 어긋난다 (Don이 이미 지적한 문제).
- ASCII 다이어그램(언어 없는 펜스)은 **영문·숫자·기호만** 들어갈 때만 허용 (메모리 맵 주소, 구조체 레이아웃 등).
- **SVG 펜스**: ```` ```svg ```` 안에 `<svg …>…</svg>`를 그대로 쓴다.
  - `viewBox` 사용, `width`/`height` 속성 생략 (반응형). 가로 560~680.
  - 다크 모드 대응: 텍스트는 fill 지정 금지(자동으로 글자색). 선·축은 `stroke="currentColor"`. 데이터 색은 중간 채도만: 파랑 `#4a7bd0`, 주황 `#e08a3c`, 초록 `#3f9a6b`, 빨강 `#d0564a`, 회색 `#888`. 상자는 `fill="<색>" fill-opacity="0.12" stroke="<색>"`. 전체 배경 사각형 금지.
  - `<text>` 크기 12~14, `font-family` 지정 금지. `<script>`, 외부 이미지, `<foreignObject>` 금지. 화살표는 `<defs><marker …>` 사용 (마커 id는 노트 안에서 SVG마다 다르게: `ah1`, `ah2` …).
  - **글자가 상자를 넘지 않게 계산한다**: 13px 기준 한글 한 글자 ≈ 13px, 영문·숫자 ≈ 7.5px. 상자 폭 ≥ 글자 폭 합 + 20. 모든 요소가 viewBox 안에 있어야 한다 (오른쪽·아래 끝 넘침 금지).
  - SVG 바로 다음 줄에 캡션 문단: `그림 N — 설명`.
- 좋은 그림 예: 유저/커널 경계와 syscall 경로, 프로세스 주소 공간 레이아웃, fd 테이블 → open file table → inode 3단 구조, fork 전후 비교, 상태 전이도, 타임라인(시퀀스) 다이어그램, epoll 내부 구조, 지연·처리량 막대그래프(직접 측정한 값으로).
- **측정 차트**: 가능하면 노트 안에서 직접 측정한 숫자(예: syscall 비용, pipe vs shm 처리량, poll vs epoll 확장성)를 막대/선 그래프 SVG로 그린다. 좌표는 Python으로 계산해서 넣는다.

## 코드 예제 (핵심 — 반드시 Linux에서 실행해서 검증)
- 실행 환경: Mac의 **Lima Ubuntu VM** `elinux` (aarch64, Ubuntu 26.04, kernel 7.0, gcc 15 · strace · ltrace · gdb · lsof · socat · sysstat 설치됨, sudo 가능). macOS에서 직접 컴파일·실행 금지 (epoll 등 Linux 전용 API가 없고 동작이 다르다).
- 작업 폴더: 소스는 Mac 쪽 `~/.elinux_scratch/<ID>/`에 쓴다 (VM에서 같은 경로로 읽힘, 홈 디렉터리는 VM에서 읽기 전용). 바이너리·실행 산출물은 VM의 `/tmp/<ID>/`에 만든다:
  ```sh
  limactl shell elinux -- bash -lc 'mkdir -p /tmp/A3 && gcc -std=gnu11 -Wall -Wextra -O2 ~/.elinux_scratch/A3/ex1.c -o /tmp/A3/ex1 && cd /tmp/A3 && ./ex1'
  ```
  (VM 안에서 `~`는 VM 사용자 홈이다. Mac 경로는 절대 경로 `/Users/donh/.elinux_scratch/...`로 쓴다.)
- 노트당 **실행 가능한 예제 8개 이상**. 주력은 **C** (POSIX/Linux API). 셸 명령(`strace`, `ls -l /proc/<pid>/fd`, `cat /proc/<pid>/maps` 등)으로 내부를 들여다보는 예제를 섞는다.
- **경고 0개** (`-Wall -Wextra`), 출력은 ```` ```text ```` 블록에 **실제 출력 그대로** 붙인다. 지어낸 출력 금지. PID·주소처럼 매번 바뀌는 값은 그대로 두고 "값은 실행마다 다르다"고 적는다.
- `strace` 출력은 길면 핵심 줄만 남기고 `...`로 생략했다고 밝힌다.
- 각 예제 위에 "무엇을 확인하는 코드인지" 한 문장, 아래에 "출력에서 볼 것" 한두 문장.
- 한 예제는 60줄 이내. 에러 처리는 생략하지 말고 짧게(`perror` + `exit`). 실제 존재하는 API·플래그만.
- 노트 폴더에는 코드 파일을 남기지 않는다 (노트 본문에만).

## 모듈 C 이후 — QEMU 부팅 랩 (공용, 읽기 전용)
- VM 안 `/var/lab/` 에 공용 랩이 있다. 먼저 `cat /var/lab/README.txt`. 구성: TF-A v2.15 BL1+FIP(`out/flash.bin`) → U-Boot v2026.10(`out/u-boot.bin`) → Linux 6.18.55 LTS(`out/Image`, 빌드 트리 `out/linux-build`) → BusyBox 1.37 static initramfs(`out/initramfs.cpio.gz`), QEMU `virt` 머신 DT(`out/virt.dtb`, `out/virt.dts`).
- **랩은 읽기 전용**이다. 수정이 필요하면 `/var/tmp/<ID>/`로 복사해서 쓴다 (소스 트리 `src/`도 마찬가지 — 커널·U-Boot 설정을 바꿔 다시 빌드하려면 `O=/var/tmp/<ID>/build`로 별도 출력 디렉터리를 쓴다).
- **큰 산출물은 `/var/tmp/<ID>/`** (디스크). VM의 `/tmp`는 2 GB tmpfs라 커널·이미지 빌드가 넘친다.
- QEMU는 KVM 없이 TCG로 돈다(호스트 M2) — 부팅에 5~30초. 자동 실행은 `timeout`을 걸고, initramfs의 `LAB_CMD=명령~인자` 커널 인자(공백은 `~`)를 쓰면 명령 하나 실행 후 자동 poweroff 된다. 대화형이 필요하면 `expect`나 `-serial` 파이프를 쓴다.
- **커널 모듈 예제는 실제로 빌드하고 QEMU 게스트 안에서 insmod해서 확인한다**: `make -C /var/lab/out/linux-build M=$PWD` (CROSS_COMPILE 불필요 — VM이 arm64), 만든 `.ko`를 복사한 initramfs(rootfs 디렉터리 `/var/lab/build/rootfs`를 `/var/tmp/<ID>/rootfs`로 복사 → `.ko` 추가 → `cpio`로 다시 묶기)에 넣어 부팅한다. VM 호스트 커널(7.0)에 랩 모듈을 넣지 말 것.
- 이름 충돌 방지: 모듈·DT 노드·네트워크 인터페이스·gpio-sim 칩 이름에 노트 ID 접두어(`c3_`, `d6_` 등). 다른 노트 작성자가 같은 VM에서 동시에 일한다. 공용 상태(apt 설치, `/var/lab` 수정, 전역 sysctl)는 바꾸지 않는다. 꼭 필요한 패키지가 없으면 설치하지 말고 보고한다 (예외: 자기 디렉터리 안 Python venv에 pip 설치는 허용).
- 코드 펜스 언어 추가 허용: ```` ```dts ````(Device Tree 소스), ```` ```make ````(Makefile·Kbuild), ```` ```text ````(Kconfig·U-Boot 환경 등 그 밖의 설정).

## 노트 구조 (목표 700~1100줄)
```markdown
# A3. <제목> — <한 줄 요약>

> **이 노트를 다 읽으면**: <할 수 있게 되는 것 3~4개를 · 로 이어서 한 줄>
> **왜 필요한가**: <인터뷰·실무에서 이 주제가 나오는 장면 한 줄>
> **Don 기준 난이도**: <이미 아는 부분 / 새로 배울 부분 한 줄>
> **선행 노트**: <예: A1, A2 — 없으면 "없음">

---

## 0. 큰 그림 — 이게 왜 필요한가
<Embedded Linux 엔지니어의 일에서 이 주제가 어디 쓰이는지. 그림 1개 이상. bare-metal 비유.>

## 1. ~ N. <주제별 절>
<각 절: 직관 → 정의 → 예제(실행 출력 포함) → 함정. ### 소제목 적극 사용. 비교는 표로.>

## N+1. 커널 안에서는 무슨 일이 일어나나
<API → 커널 구조체·함수 경로. 그림 1개. 드라이버 작성 때 다시 만나는 지점을 모듈 ID로 연결.>

## N+2. Embedded 실무에서 다시 보기
<센서 키트 같은 실제 장비에서 이 주제가 드러나는 장면 2~4개. 가능하면 코드.>

## N+3. 흔한 실수와 증상
| 실수 | 증상 | 원인 | 고치는 법 |   (6~10행)

## N+4. 면접에서 이렇게 말한다
<질문 6~8개: "**Q.** 질문(영어 원문)" 문단 → "**A.** 30초 답변(한국어)" 문단 → 그대로 쓸 수 있는 영어 답변(인용 `>`). 꼬리 질문이 있으면 "**꼬리 질문**:" 한 줄.>

## N+5. 직접 해보기
<연습문제 5~7개. 개념 문제 + 코드 과제 + "/proc·strace로 관찰하기" 과제. 각 문제 뒤에 "정답/힌트:" 한 줄.>

## N+6. 용어 사전
| 용어 | 뜻 | 한 줄 설명 |

## N+7. 요약 & 체크리스트
<한 문단 요약 + `- [ ]` 8~12개 ("~를 설명할 수 있다", "~를 코드로 짤 수 있다" 식)>

## 참고 자료
<실제로 존재하는 자료만: man7.org man 페이지, Kerrisk "The Linux Programming Interface", docs.kernel.org, LWN.net 특정 기사(확실할 때만), Bootlin 슬라이드, OSTEP 해당 장, elixir.bootlin.com 소스 위치. URL은 확실한 것만.>
```

## 정확성
- 커널·API·수치는 정확해야 한다. 확실하지 않으면 쓰지 않는다. 커널 내부 함수 이름은 현재 커널(6.x LTS ~ VM의 7.0) 기준으로 쓰고, 버전에 따라 다르면 그렇다고 밝힌다.
- 모든 코드 출력은 VM에서 실제 실행한 결과다. VM 커널 버전은 `uname -r`로 확인해 노트에 한 번 적는다.
- 한 노트 = 한 파일. 지정된 파일만 만든다. `contents.md`, `build_site.py`, `NOTES_SPEC.md`, 다른 노트, `site/`는 **수정 금지**.
- 다 쓴 뒤 스스로 점검: SVG가 XML로 파싱되는지, 글자가 상자·viewBox 밖으로 나가지 않는지, 표 셀에 `|`가 없는지, 출력 블록이 전부 실제 실행 결과인지.
