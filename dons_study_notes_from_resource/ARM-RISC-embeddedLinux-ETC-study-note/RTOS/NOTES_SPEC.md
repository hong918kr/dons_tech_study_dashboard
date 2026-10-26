# RTOS 스터디 노트 — 작성 스펙

## 목적
Don이 **RTOS를 원천부터(레지스터·스택·스케줄링 이론까지) 이해하고, 드론·로봇·차량 펌웨어 인터뷰를 통과하고, 입사 후 바로 태스크 설계·디버깅을 해내기** 위해 공부하는 노트 세트다.
전체 목차(나침반)는 `contents.md`. 각 노트는 그 목차의 항목 하나(예: `A3`)를 **교과서처럼 자세하고 쉽게** 푼다.
Don은 노트를 **처음부터 끝까지 읽으면서 공부**한다. 요약본이 아니다. Don의 요청: "embedded linux 연구 수준 정도로, 알기 쉽게, 차트·그림·코드 예시를 충분히".

- 출력: `<모듈폴더>/YYYY-MM-DD_<ID>_<topic>.md` (예: `A_rt_concepts/2026-10-25_A3_rtos_components.md`)
- HTML: `python3 build_site.py` → `site/index.html` (노트 작성자는 HTML을 만들지 않는다)

## 독자 — Don
- 임베디드 펌웨어 약 8년. SK hynix/Solidigm 엔터프라이즈 SSD 양산 펌웨어(bare-metal C, 멀티코어 ARM Cortex-R/M, NVMe/PCIe, NAND, DMA, 인터럽트, 에러 처리, 성능 튜닝), 현재 Apple에서 RF 칩셋 통합(새 실리콘 bring-up, PCIe/I2C/SPI/SPMI/RFFE 버스 장애 root cause, scope·로직 분석기·JTAG 디버그). UC Berkeley 응용수학.
- 잘 아는 것: C, 포인터·메모리 레이아웃, 비트 조작, 레지스터·인터럽트·DMA·캐시, 링버퍼, 측정 기반 디버깅. SSD 펌웨어의 실행부(executive) 구조.
- 약한 것: **범용 RTOS 자체** — FreeRTOS/Zephyr API와 내부 구현, 스케줄링 이론(RM·RTA·EDF), Zephyr 생태계(west·Kconfig·DT), 안전 인증. Embedded Linux는 같은 형식의 노트(153항목)로 막 공부했다.
- 그래서 **Don이 아는 bare-metal·SSD 펌웨어 개념과 연결해서** 설명한다. 예: "태스크 = 자기 스택을 가진 main 루프 하나", "블로킹 = 폴링 대신 스케줄러에게 '깨워 달라'고 맡기는 것", "PendSV = 가장 낮은 우선순위로 미뤄 둔 소프트웨어 인터럽트", "세마포어 = ISR이 세우고 태스크가 내리는 깃발".

## 노트의 방향 — "개념 → RTOS 커널 안 → 실무"
각 노트에 반드시 **"RTOS 커널 안에서는 무슨 일이 일어나나"** 절을 둔다: 개념이 FreeRTOS·Zephyr 소스의 어떤 구조체·함수(`TCB_t`, `pxReadyTasksLists`, `xTaskCreate`, `struct k_thread`, `z_swap` 등)로 구현되는지 실제 소스 위치(파일·함수 이름)와 함께 짧게 보여 주고, 깊은 설명은 해당 모듈(B, C, D …)로 미루고 ID로 참조만 한다.

## Embedded Linux 노트와 중복 금지
- `contents.md` §3 경계표에 있는 주제는 Linux 노트에서 이미 설명했다. **다시 풀어 설명하지 말고** "자세한 내용은 EL N1"처럼 참조하고 **RTOS·MCU에서 달라지는 점만** 쓴다.
- Linux 노트 위치: `../EmbeddedLinux/<모듈폴더>/*.md`. 관련 노트를 **먼저 읽고** 겹치지 않게 쓴다 (각 작성 지시에 읽을 노트를 적어 준다).
- 같은 모듈 안 다른 노트와도 겹치지 않게 지시서의 범위를 지킨다. 범위 밖은 ID로 참조만.

## Bare-Metal 노트와 겹침 — 반드시 표시 (Don 지시 2026-10-25)
- 형제 사이트 `../Bare-Metal/contents.md`(176항목)에 같은 주제가 있다. `contents.md`의 **BM 겹침** 칸이 대응표다 (예: `B1` ↔ `BM C1`).
- **RTOS 노트가 먼저 쓰이므로 주인**이다: 겹치는 개념은 이 노트에서 **빠짐없이 깊게** 쓴다 (Bare-Metal에 미루지 않는다).
- 겹침이 있는 노트는 **반드시** 아래 3곳에 표시한다:
  1. 머리 인용 블록에 한 줄: `> 🔁 **Bare-Metal 겹침**: BM C1(프로그래머 모델) — 이 노트가 주인. Bare-Metal 노트는 이 노트를 링크하고 <bare-metal 고유 관점 한 구절>만 더한다`
  2. 겹치는 내용이 시작되는 절 첫머리에 한 줄: `> 🔁 이 절은 Bare-Metal BM C4와 겹친다 (주인: 이 노트).`
  3. 용어 사전 아래가 아니라 **참고 자료 바로 앞**에 `## Bare-Metal 노트와의 관계` 절: 겹치는 BM 항목 표(BM ID · BM 주제 · 이 노트의 해당 절 · Bare-Metal 쪽에 남길 내용) — 2~6행.
- Bare-Metal 나침반의 해당 항목 행(핵심 개념 칸)을 읽고, 거기 적힌 개념이 이 노트에 다 들어갔는지 확인한다.
- 겹침이 없는 노트(`BM 겹침` 칸이 `—`)는 표시하지 않는다.

## 실무·면접 맥락 (예시 소재)
대표 시나리오: **드론 비행 제어기**(STM32H7급 MCU, IMU 1~8 kHz, 자세 제어 rate loop, ESC PWM/DShot, 텔레메트리 UART, GNSS) · **로봇 관절 모터 컨트롤러**(FOC 전류 루프 20 kHz, CAN) · **센서 허브 MCU**(Linux SoC 옆에서 IMU·타임스탬프 담당) · Don의 **SSD 컨트롤러 펌웨어**. Neros·Anduril(드론), Scout AI(국방 로보틱스), Rivian(차량 SoC) 같은 회사의 펌웨어 엔지니어 업무.
예시는 "예를 들어 드론 비행 제어기라면" 식으로 쓴다.

## 톤
- **한국어 기본**, 기술 용어는 영어 그대로 (task, context switch, preemption, deadline, jitter…). 설명체(~다). 존댓말 금지.
- **쉽고 친절하게**. 새 용어는 처음 나올 때 한 문장으로 풀어 준다.
- 한 절의 흐름: **직관(그림·비유) → 정의 → 작은 예제 → 코드로 확인(실제 실행 출력) → RTOS 커널 안과 연결 → Don 경험과 연결 → 흔한 함정**.
- 추상 설명만 길게 쓰지 않는다. 직접 돌려 보는 코드와 출력, 측정 숫자가 많아야 한다.

## 마크다운 규칙 (빌더가 지원하는 것만)
1. `#`~`###` 제목, 문단, `-`/`1.` 목록(**중첩 금지**), 표(한 행 한 줄, 셀 안 줄바꿈은 `<br>`), `> ` 인용, 코드펜스, `---`, `- [ ]` 체크박스, 인라인 `` `코드` `` `**굵게**` `[링크](url)`.
2. **raw HTML 금지** (단 표 셀 안 `<br>`, 그리고 `svg` 펜스는 허용).
3. **표 셀 안에 `|` 문자 금지** (빌더가 이스케이프 못 함). 셀 안에서는 "또는", "/", `→`로 표현. C의 `||`·`|=`도 셀에 넣지 말 것.
4. 본문에 별표 `*`를 그대로 쓰지 말 것(이탤릭으로 해석됨). 포인터·곱셈은 인라인 코드 안에 쓴다 (`uint32_t *sp`, `C * n`). 수식의 곱셈은 `×`, 시그마는 `Σ`.
5. 코드 블록 언어: ```` ```c ````, ```` ```sh ````(명령), ```` ```text ````(출력·설정), ```` ```python ````, ```` ```asm ````(어셈블리), ```` ```make ````, ```` ```cmake ````, ```` ```kconfig ```` 대신 ```` ```text ````. 다이어그램은 언어 없는 ```` ``` ````.

## 그림 · 차트 (필수 — 노트당 SVG 5개 이상, 그중 측정 차트 1개 이상)
- **한글이 들어가는 다이어그램은 반드시 SVG로 그린다.** 고정폭 글꼴에서 한글 폭이 영문 2칸과 맞지 않아 ASCII 박스가 어긋난다 (Don이 이미 지적한 문제).
- ASCII 다이어그램(언어 없는 펜스)은 **영문·숫자·기호만** 들어갈 때만 허용 (메모리 맵 주소, 스택 프레임 레이아웃 등).
- **SVG 펜스**: ```` ```svg ```` 안에 `<svg …>…</svg>`를 그대로 쓴다.
  - `viewBox` 사용, `width`/`height` 속성 생략. 가로 560~680.
  - 다크 모드 대응: 텍스트는 fill 지정 금지(자동으로 글자색). 선·축은 `stroke="currentColor"`. 데이터 색은 중간 채도만: 파랑 `#4a7bd0`, 주황 `#e08a3c`, 초록 `#3f9a6b`, 빨강 `#d0564a`, 회색 `#888`. 상자는 `fill="<색>" fill-opacity="0.12" stroke="<색>"`. 전체 배경 사각형 금지.
  - `<text>` 크기 12~14, `font-family` 지정 금지. `<script>`, 외부 이미지, `<foreignObject>` 금지. 화살표는 `<defs><marker …>` (마커 id는 노트 안에서 SVG마다 다르게: `ah1`, `ah2` …).
  - **글자가 상자를 넘지 않게 계산한다**: 13px 기준 한글 한 글자 ≈ 13px, 영문·숫자 ≈ 7.5px. 상자 폭 ≥ 글자 폭 합 + 20. 모든 요소가 viewBox 안에 있어야 한다 (오른쪽·아래 끝 넘침 금지 — 지난 모듈에서 viewBox 높이를 넘은 SVG가 여러 번 나왔다).
  - SVG 바로 다음 줄에 캡션 문단: `그림 N — 설명`.
- RTOS 노트에서 특히 좋은 그림: **태스크 타임라인(간트 차트: 시간축 위 어느 태스크가 CPU를 쥐었나, ISR 구간 표시)**, 상태 전이도, 메모리 맵(FLASH/RAM, 스택 배치), 지연 분해 막대, 비교 표 그림, 결정 흐름도.
- **측정 차트**: 노트 안에서 직접 측정한 숫자(응답 시간·지터 히스토그램, footprint 막대, 실행 모델별 지연)를 막대/선/히스토그램 SVG로 그린다. 좌표는 Python으로 계산해서 넣는다.

## 코드 예제 (핵심 — 반드시 실행해서 검증)
- 실행 환경: Mac의 **Lima Ubuntu VM** `elinux` (aarch64, Ubuntu 26.04, gcc 15, arm-none-eabi-gcc 14.2, QEMU 10.2). macOS에서 직접 컴파일·실행 금지.
- **RTOS 랩 `/var/rtoslab/` (공용, 읽기 전용)** — 먼저 `cat /var/rtoslab/README.txt`. 요약:
  - QEMU `mps2-an385` = **Cortex-M3 @ 25 MHz** 가상 보드. UART printf, `board_now_us()`(us 시계), `board_exit(code)`(QEMU 종료), HardFault 레지스터 출력이 템플릿에 들어 있다.
  - `templates/baremetal-mps2` (RTOS 없음) · `templates/freertos-mps2` (FreeRTOS V11.3.2, ARM_CM3 포트) · `templates/freertos-posix` (FreeRTOS를 Linux 프로세스로) · Zephyr v4.4.2 (`/var/rtoslab/bin/rtos-zbuild mps2/an385 …` — FreeRTOS와 **같은 가상 보드**, 또는 `native_sim/native/64`).
  - 빌드: `make -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS="/Users/donh/.rtos_scratch/<ID>/ex1.c" OUT=/var/tmp/rtos/<ID>/ex1` → 실행: `/var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/<ID>/ex1/app.elf`
  - **다른 CPU 타깃** (모듈 B부터): QEMU `mps2-an386`(M4F, FPU) · `mps2-an500`(M7) · `mps2-an505`(M33 + TrustZone) · `mps2-an521`(듀얼 M33) · `mps3-an536`(Cortex-R52) · `mps3-an547`(M55) · `microbit`(M0) · `qemu-system-riscv32/64 -machine virt` · `qemu-system-aarch64`(Cortex-A53 SMP). 템플릿은 M3 전용이므로 다른 코어는 `templates/common`을 **자기 폴더로 복사해서** 고친다(`-mcpu`, 메모리 맵, 벡터). Zephyr는 `rtos-zbuild <board> …` 후 `rtos-zrun <build_dir> [timeout]`로 보드에 맞는 QEMU 옵션으로 실행 (보드 예: `mps2/an521/cpu0`, `qemu_riscv32`, `qemu_cortex_a53/qemu_cortex_a53/smp`). Zephyr SDK 1.0.1 툴체인(`/var/rtoslab/zephyr-sdk-1.0.1/gnu/{arm-zephyr-eabi,riscv64-zephyr-elf,aarch64-zephyr-elf}`)을 bare-metal 빌드에 직접 써도 된다(RISC-V rv32는 이게 가장 쉽다).
  - **QEMU가 모델링하지 않는 것**: 캐시(M7/M85 D-cache 효과 없음), 플래시 wait state, 예외 스태킹 사이클, DWT CYCCNT. 사이클 시계가 필요하면 MPS2 FPGAIO COUNTER(0x40028018, 25 MHz). 모델링 안 되는 효과는 **실행해서 보여 줄 수 없다고 정직하게 쓰고**, 코드는 "빌드만 함"으로 표시하거나 VM 호스트(arm64)에서 대응 실험을 한다.
  - **프로세스 정리**: `limactl shell ... bash -lc '... pkill -f <패턴>'`은 자기 셸까지 죽인다(종료코드 255). 남은 QEMU는 `pgrep -a qemu-system`으로 PID를 찾아 `kill <PID>`.
  - **타이밍 주의**: `ICOUNT=1`을 붙이면 명령 1개 = 32 ns 가상 시간 → 실행할 때마다 같은 숫자(결정적, 약 31 MIPS로 25 MHz MCU와 비슷한 규모). 붙이지 않으면 가상 시계 = 호스트 실제 시간이고 명령은 호스트 속도로 돈다. **타이밍 수치를 보여 줄 때는 ICOUNT=1을 기본으로** 쓰고, 노트에 "QEMU icount 기준, 실제 칩과 절대값은 다르다(캐시·플래시 wait state·버스 미모델링)"를 한 번 밝힌다. QEMU에는 DWT CYCCNT가 없다.
  - Linux 쪽 비교 측정(예: Linux 스레드 주기 지터)은 VM 호스트에서 직접 C로 잰다 (`clock_nanosleep`, `sudo chrt -f 80`, `stress-ng` 가능). VM이므로 "VM 기준"이라고 밝힌다.
- 작업 폴더: 소스는 Mac 쪽 `~/.rtos_scratch/<ID>/`에 (VM에서 `/Users/donh/.rtos_scratch/<ID>/`로 읽힘, 홈은 VM에서 읽기 전용). 빌드 산출물은 VM의 `/var/tmp/rtos/<ID>/`. **다른 작성자와 폴더를 공유하지 말 것** (지난 모듈에서 공유 scratch 충돌이 있었다).
  ```sh
  limactl shell elinux -- bash -lc 'make -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS=/Users/donh/.rtos_scratch/A4/ex1.c OUT=/var/tmp/rtos/A4/ex1 && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/A4/ex1/app.elf'
  ```
- 노트당 **실행한 예제 8개 이상** (A6처럼 조사 중심 노트는 지시서에 따로 정한 수). 주력은 **C**(FreeRTOS·Zephyr·bare-metal). 분석 예제(`arm-none-eabi-nm --size-sort`, `size`, map 파일, `objdump -d`로 생성 코드 보기)와 Python 계산·차트 생성을 섞는다.
- **경고 0개** (`-Wall -Wextra`), 출력은 ```` ```text ```` 블록에 **실제 출력 그대로**. 지어낸 출력 금지. 출력이 길면 핵심 줄만 남기고 `...`로 생략했다고 밝힌다.
- 각 예제 위에 "무엇을 확인하는 코드인지" 한 문장, 아래에 "출력에서 볼 것" 한두 문장.
- 한 예제는 70줄 이내. 실제 존재하는 API·매크로만 (FreeRTOS V11.3.2, Zephyr v4.4.2 소스에서 확인).
- 노트 폴더에는 코드 파일을 남기지 않는다 (노트 본문에만).
- 공용 상태 금지: `/var/rtoslab` 수정 금지, apt 설치 금지(필요하면 보고), 전역 sysctl 변경 금지, AppArmor 건드리지 말 것. 자기 `/var/tmp/rtos/<ID>/` 안에서 git clone·pip venv는 허용.
- VM `/tmp`는 tmpfs다. 큰 산출물은 `/var/tmp/rtos/<ID>/`에.

## 노트 구조 (목표 900~1500줄 — Don은 "최대한 자세히"를 원한다. 넘쳐도 된다)
```markdown
# A3. <제목> — <한 줄 요약>

> **이 노트를 다 읽으면**: <할 수 있게 되는 것 3~4개를 · 로 이어서 한 줄>
> **왜 필요한가**: <인터뷰·실무에서 이 주제가 나오는 장면 한 줄>
> **Don 기준 난이도**: <이미 아는 부분 / 새로 배울 부분 한 줄>
> **선행 노트**: <예: A1, A2, EL N1 — 없으면 "없음">

---

## 0. 큰 그림 — 이게 왜 필요한가
<RTOS 펌웨어 엔지니어의 일에서 이 주제가 어디 쓰이는지. 그림 1개 이상. bare-metal/SSD 비유.>

### 실습 환경 한 줄
<이 노트의 예제가 도는 환경: QEMU mps2-an385 + FreeRTOS V11.3.2 / Zephyr v4.4.2 / VM 호스트, ICOUNT 여부>

## 1. ~ N. <주제별 절>
<각 절: 직관 → 정의 → 예제(실행 출력 포함) → 함정. ### 소제목 적극 사용. 비교는 표로.>

## N+1. RTOS 커널 안에서는 무슨 일이 일어나나
<개념 → FreeRTOS/Zephyr 소스의 구조체·함수 경로(파일 이름 포함). 그림 1개. 나중 모듈 ID로 연결.>

## N+2. 실무에서 다시 보기
<드론 FC·모터 컨트롤러·센서 허브·SSD FW에서 이 주제가 드러나는 장면 2~4개. 가능하면 코드.>

## N+3. 흔한 실수와 증상
| 실수 | 증상 | 원인 | 고치는 법 |   (6~10행)

## N+4. 면접에서 이렇게 말한다
<질문 6~8개: "**Q.** 질문(영어 원문)" 문단 → "**A.** 30초 답변(한국어)" 문단 → 그대로 쓸 수 있는 영어 답변(인용 `>`). 꼬리 질문이 있으면 "**꼬리 질문**:" 한 줄.>

## N+5. 직접 해보기
<연습문제 5~7개. 개념 문제 + 코드 과제 + 측정 과제. 각 문제 뒤에 "정답/힌트:" 한 줄.>

## N+6. 용어 사전
| 용어 | 뜻 | 한 줄 설명 |

## N+7. 요약 & 체크리스트
<한 문단 요약 + `- [ ]` 8~12개>

## 참고 자료
<실제로 존재하는 자료만: FreeRTOS 문서·소스 파일, Zephyr 문서 페이지, Arm 문서, 교재(장 번호는 확실할 때만), Memfault Interrupt 블로그 특정 글(확실할 때만). URL은 확실한 것만.>
```

## 정확성
- API·수치·연도는 정확해야 한다. 확실하지 않으면 쓰지 않는다. FreeRTOS는 V11.3.2 소스(`/var/rtoslab/src/FreeRTOS-Kernel`), Zephyr는 v4.4.2 소스(`/var/rtoslab/zephyrproject/zephyr`)에서 직접 grep해서 확인한다.
- 버전·날짜가 들어가는 사실(릴리스, 인증 현황, 점유율)은 웹에서 확인하고 출처를 참고 자료에 적는다. 확인 못 한 것은 "확인 못 함"으로 쓴다.
- 모든 코드 출력은 실제 실행한 결과다.
- 한 노트 = 한 파일. 지정된 파일만 만든다. `contents.md`, `build_site.py`, `NOTES_SPEC.md`, 다른 노트, `site/`는 **수정 금지**.
- 다 쓴 뒤 스스로 점검: SVG가 XML로 파싱되는지(`python3 -c "import xml.dom.minidom,sys; ..."`), 글자가 상자·viewBox 밖으로 나가지 않는지(좌표 계산), 표 셀에 `|`가 없는지, 출력 블록이 전부 실제 실행 결과인지.
