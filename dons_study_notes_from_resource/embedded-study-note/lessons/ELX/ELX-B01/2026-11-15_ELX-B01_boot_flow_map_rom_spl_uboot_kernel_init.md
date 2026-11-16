---
id: ELX-B01
time: 70분
updated: 2026-11-15
---
# ELX-B01 부트 플로우 지도: ROM→SPL→U-Boot→Kernel→init

## 학습 목표

- 전원 ON부터 PID 1까지 **각 단계가 어디서 실행되고, 다음 단계를 어디에 올리고, 무엇을 넘기는지** 표 하나로 말할 수 있다 (arm64: `x0 = DTB 물리 주소`, bootargs는 `/chosen`).
- UART 로그 한 덩어리를 **단계별로 잘라** 시작·끝 시각, 줄 수, 로드 주소, bootargs, 커널 버전을 뽑는 파서를 Python으로 쓴다.
- 실제 로그의 함정 — CR 덮어쓰기, ESC 제어열, 깨진 바이트, 콘솔이 켜질 때 **몰아서 나오는 커널 줄**, printk 시각 리셋 — 을 테스트로 다룬다.
- 부트 루프 로그를 부팅별로 나누고 "어느 단계에서 왜 죽었나"를 기계적으로 뽑는다 (bring-up 첫날 하는 일).

## 1. 문제

보드 bring-up 첫날 손에 쥐는 것은 **UART 로그 하나**다. "어디까지 갔고 어디서 멈췄나, 시간은 어디서 새나"를 그 로그로 답하는 능력이 임베디드 리눅스 면접의 단골 첫 질문("전원 켜면 무슨 일이 일어나나요?")과 실무 디버깅을 동시에 덮는다.

> 환경: 로그 파서(starter/solution/practice)는 **맥의 Python 3.9 표준 라이브러리**만 쓴다. 입력 로그 중 2개는 이 랩에서 **실제로 캡처**했다 — Lima VM `elinux`(aarch64, TCG 에뮬레이션) 위 `qemu-system-aarch64 -machine virt,secure=on -cpu cortex-a57 -smp 1 -m 1057`, TF-A v2.15.0 → U-Boot 2026.10 → Linux 6.18.55 → BusyBox `/init`. 나머지 2개는 i.MX8M Plus 스타일로 손으로 쓴 **합성(synthetic)** 로그다 (QEMU에는 SPL·DDR training이 없어서). TCG 시각은 실제 하드웨어 시간이 아니다 — **순서와 비율**만 본다.

부팅은 릴레이 경주다. 각 주자는 자기가 아는 최소한만 초기화하고, 다음 주자를 **더 큰 메모리**에 올려 놓고 바통(인자)을 넘긴다. 아래 그림의 주소는 이번 레슨에서 캡처한 QEMU 로그와 TF-A/U-Boot 소스에서 확인한 값이다.

```svg
<svg viewBox="0 0 760 420" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="b01-a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="14">누가 누구를 어디에 올리고 무엇을 넘기나 (QEMU virt secure=on 실측)</text>
  <g fill="currentColor" font-weight="bold"><text x="120" y="42">실행 위치 (이 랩)</text><text x="430" y="42">다음 단계에 넘기는 것</text></g>
  <g stroke="currentColor">
    <rect x="10" y="50" width="100" height="44" rx="6" class="f-mut"/>
    <rect x="10" y="104" width="100" height="44" rx="6" class="f-acc"/>
    <rect x="10" y="158" width="100" height="44" rx="6" class="f-acc"/>
    <rect x="10" y="212" width="100" height="44" rx="6" class="f-acc"/>
    <rect x="10" y="266" width="100" height="44" rx="6" class="f-ok"/>
    <rect x="10" y="320" width="100" height="44" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-weight="bold">
    <text x="60" y="76">BootROM</text><text x="60" y="124">SPL</text><text x="60" y="140" font-weight="normal">TF-A BL1+BL2</text>
    <text x="60" y="178">BL31</text><text x="60" y="194" font-weight="normal">EL3 상주</text>
    <text x="60" y="238">U-Boot</text><text x="60" y="292">Linux</text><text x="60" y="346">init</text>
  </g>
  <g stroke="currentColor" marker-end="url(#b01-a)">
    <line x1="60" y1="94" x2="60" y2="102"/><line x1="60" y1="148" x2="60" y2="156"/><line x1="60" y1="202" x2="60" y2="210"/>
    <line x1="60" y1="256" x2="60" y2="264"/><line x1="60" y1="310" x2="60" y2="318"/>
  </g>
  <g fill="currentColor">
    <text x="120" y="68">없음: CPU가 flash 0x0 의 BL1 에서 리셋</text>
    <text x="120" y="86" opacity=".75">(실제 칩: 마스크 ROM, 부트 핀으로 매체 선택)</text>
    <text x="120" y="122" font-family="monospace">BL1 @ flash 0x0 (ROM 역할)</text>
    <text x="120" y="140" font-family="monospace">BL2 → secure SRAM 0x0e00_0000 대</text>
    <text x="120" y="176">BL31 = secure SRAM 에 남아 PSCI 서비스</text>
    <text x="120" y="194" opacity=".75">(종료 때 "ERROR: QEMU System Power off")</text>
    <text x="120" y="230" font-family="monospace">0x6000_0000 → 재배치 0x8158_f000</text>
    <text x="120" y="248" font-family="monospace">kernel_addr_r=0x4040_0000</text>
    <text x="120" y="284" font-family="monospace">x0 = DTB 0x7f3f_e000 (EL1)</text>
    <text x="120" y="302" font-family="monospace">initrd 0x7f40_4000..0x7f52_b7e2</text>
    <text x="120" y="338" font-family="monospace">/init (initramfs) PID 1, EL0</text>
    <text x="120" y="356" opacity=".75">BusyBox sh 스크립트</text>
    <text x="430" y="68">실제 칩: SPL 을 SRAM 으로 읽어 점프</text>
    <text x="430" y="86" opacity=".75">(DRAM 은 아직 죽어 있다)</text>
    <text x="430" y="122">BL31 → SRAM, U-Boot(BL33) → DRAM</text>
    <text x="430" y="140" opacity=".75">실제 보드 SPL: 여기서 DDR training</text>
    <text x="430" y="176">ERET → U-Boot 를 Non-secure EL1 로</text>
    <text x="430" y="194" opacity=".75">"BL1: Booting BL31" 이 경계 줄</text>
    <text x="430" y="230">Image·initrd·DTB 배치, /chosen 에</text>
    <text x="430" y="248" font-family="monospace">bootargs, linux,initrd-start/end</text>
    <text x="430" y="284">rootfs(initramfs) 풀고 execve("/init")</text>
    <text x="430" y="302" opacity=".75">"Run /init as init process" 가 경계</text>
    <text x="430" y="338">모르는 cmdline 인자 → argv/envp</text>
    <text x="430" y="356" font-family="monospace">b01_from=uboot LAB_CMD=...</text>
  </g>
  <text x="10" y="390" fill="currentColor" opacity=".8">합성 i.MX8MP 로그: SPL 이 DDR training 0.65 s,</text>
  <text x="10" y="408" fill="currentColor" opacity=".8">booti 가 Image 를 0x4048_0000 → 0x4060_0000 로 옮김, DTB 0x4300_0000.</text>
</svg>
```

- **BootROM**: 칩에 구워진 코드. 부트 핀(boot mode)을 읽어 eMMC/SD/SPI NOR/USB 중 하나에서 **작은** 다음 단계를 칩 내부 SRAM(OCRAM)에 읽는다. 보통 UART에 아무것도 안 찍는다 → 로그에서는 "전원 ON부터 첫 줄까지의 침묵"으로만 보인다.
- **SPL(Secondary Program Loader)**: SRAM(수백 KB)에 들어갈 만큼 작은 U-Boot. 핵심 일은 **DDR 초기화(training)** 와 다음 이미지 로드. arm64 SoC에서는 그다음에 **TF-A BL31**(EL3 보안 모니터)과 U-Boot 본체를 올린다. QEMU virt에는 SPL이 없고, TF-A의 BL1+BL2가 그 역할을 한다 (TF-A 문서 용어로 BL1 = "AP Trusted ROM").
- **U-Boot**: 장치 초기화, 환경 변수, 저장 매체에서 Image/DTB/initrd 로드, `booti`로 점프. U-Boot 환경·FIT은 ELX-M12에서 다룬다.
- **kernel**: `head.S`가 x0의 DTB 주소를 보관하고 MMU를 켠 뒤 `start_kernel`. DTB의 `/chosen/bootargs`가 커널 command line이 된다. 마지막에 `/init`(initramfs) 또는 `/sbin/init`(디스크 rootfs)을 실행한다.
- **init**: 첫 사용자 공간 프로세스(PID 1). BusyBox init이든 systemd든 여기서부터 사용자 공간이다 (ELX-B04 initramfs, ELX-B13 systemd).

### Part A — 한 줄 정리 + 단계 상태 기계 + 타임라인

`capture_boot.py`(이 레슨 폴더)는 QEMU 시리얼의 각 줄 앞에 **grabserial 스타일 호스트 시각** `[  abs_s  delta_s] `를 붙인다 (abs = QEMU를 띄운 순간 = "전원 ON"부터 경과 초, delta = 직전 줄과의 차이). U-Boot·TF-A 줄에는 자체 시각이 없기 때문이다. 커널 줄은 그 뒤에 다시 printk 시각 `[    0.000000]`을 갖는다.

1. `clean_text(s)` — 터미널에 실제로 보이는 글자만 남긴다: ESC 제어열 제거, 줄 끝 CR/LF 제거, **줄 중간 CR = 0열로 돌아가 덮어쓰기**, 깨진 바이트 제거.
2. `parse_line(raw)` → `Line(no, host, ktime, text)` — 호스트 시각과 printk 시각을 떼어 낸다.
3. `step(state, line)` — 단계 상태 기계: 표지(marker) 줄로 단계를 **앞으로만** 옮기고, 핸드오프 줄("Starting kernel ...", "Run /init as init process")을 경계로 쓴다.
4. `parse_boot_log(text)` → 단계 6개의 시작/끝/길이/줄 수. 출력은 `format_timeline()`이 표로 그린다.

### Part B — 정보 추출과 가장자리 경우

- `extract_info(text)` — TF-A/U-Boot/커널 버전, `Kernel command line:`(= 커널이 실제로 받은 bootargs), init 경로, U-Boot 환경의 주소(`kernel_addr_r=0x40400000`, `fdtcontroladdr=8054cce0` — **0x가 없어도 16진수**), `booti`가 찍는 주소(`Loading Ramdisk to`, `Loading Device Tree to`, `Moving Image from`), `=> booti ${kernel_addr_r} ...`의 `${var}` 풀기, 커널 printk 시계 0의 호스트 시각 추정.
- 가장자리: 펌웨어 단계가 **없는** 로그(QEMU `-kernel` 직접 부팅), 호스트 시각이 없는 로그, 캡처가 커널 중간부터 시작된 로그, CRLF/LF, 전원 ON 순간의 쓰레기 바이트, **printk 시각 리셋**.

### Part C — 부트 루프 로그 진단

합성 로그 `imx8mp_bootloop_synthetic.log`에는 부팅이 3번 들어 있다: ① `rootwait`가 빠진 bootargs로 커널 패닉 → 워치독 리셋 ② SPL이 eMMC를 못 읽고 멈춤 → 전원 재투입 ③ 정상 부팅. `split_boots()`로 나누고 `diagnose()`로 부팅마다 "마지막 단계, init 도달 여부, 첫 치명 줄, bootargs"를 뽑는다.

## 2. Problem (English)

> **Boot-log timeline parser.**
>
> You are given a UART capture of an embedded Linux board. Each line may carry a host timestamp prefix `[abs delta]` added by the capture tool, and kernel lines additionally carry a printk timestamp `[    s.uuuuuu]`. Raw bytes may include CRLF, mid-line carriage returns (progress counters), ANSI escape sequences and garbage bytes.
>
> 1. Implement `clean_text`, `parse_line` and a stage state machine `step` that assigns every non-empty line to one of `BootROM, SPL, BL31, U-Boot, kernel, init`. Stages only move forward within a boot; a firmware banner seen again, a stage marker earlier than the current stage, or a printk timestamp that jumps backwards starts a **new boot**. Handoff lines (`BL1: Booting BL31`, `Starting kernel ...`, `Run <path> as init process`) belong to the outgoing stage, and their timestamp is the start of the next stage.
> 2. `parse_boot_log(text, boot=0)` returns one row per stage: `present, start, end, dur, lines, first_no, first_text, note`. BootROM prints nothing: infer it as `[0, first stage start)` for the first boot when host timestamps exist. If there is no `Starting kernel ...` handoff, the kernel starts at `min(first arrival, min(host - printk))`.
> 3. `extract_info` returns versions, `bootargs` (from `Kernel command line:`), init path, U-Boot env addresses and `booti` load addresses as ints.
> 4. `split_boots` / `diagnose` handle a capture that contains a boot loop: for each boot report the last stage reached, whether init was reached, the first fatal line, and the bootargs.
>
> Example: for the captured QEMU log the timeline is `BootROM 0.000–0.176 (inferred), SPL 0.176–0.196, BL31 0.196–0.212, U-Boot 0.212–0.562, kernel 0.562–1.714, init 1.714–1.858`, and `bootargs == "console=ttyAMA0 b01_from=uboot LAB_CMD=cat~/proc/cmdline"`.

## 3. 시작 코드

<!-- code: starter.py -->

```bash
cd /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B01 && python3 -I starter.py
# 처음엔 "1/37 passed", exit 1 — TODO 7개(clean_text, parse_line, step, parse_boot_log, extract_info, split_boots, diagnose)를 채워 37/37
```

입력 로그 4개 (레슨 폴더에 같이 있다):

| 파일 | 출처 | 줄 수 | 무엇을 시험하나 |
|---|---|---|---|
| `qemu_tfa_uboot_boot.log` | **실측** TF-A → U-Boot → `booti` → `/init` | 284 | 골든 표, CRLF·CR 덮어쓰기·ESC, TF-A 런타임 메시지 |
| `qemu_direct_kernel_boot.log` | **실측** `-kernel` 직접 부팅 (펌웨어 없음) | 252 | 빠진 단계, printk 리셋(두 번 이어 붙여서) |
| `imx8mp_synthetic_boot.log` | **합성** i.MX8MP 스타일 | 74 | SPL DDR training, `Moving Image`, systemd ESC 색 |
| `imx8mp_bootloop_synthetic.log` | **합성** 부트 루프 3회 | 141 | Part C |

실측 로그는 맥에서 아래 한 줄로 다시 뜰 수 있다 (VM에서 QEMU를 돌리고, 결과는 맥 쪽 파일로 리다이렉트). 시각은 실행마다 조금씩 다르므로 테스트는 레슨에 들어 있는 파일을 쓴다.

```bash
limactl shell elinux -- python3 -I /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B01/capture_boot.py tfa-uboot > /tmp/ELX-B01_tfa.log
```

```text
capture: mode=tfa-uboot qemu exit=0 total 1.860 s
```

`capture_boot.py`가 하는 일 (expect 대신 파이썬으로 같은 일을 한다 — 시각 도장까지 한 번에 붙이려고):

- `-device loader,file=/var/lab/out/Image,addr=0x40400000`, `-device loader,file=initramfs.cpio.gz,addr=0x44000000` — 리셋 전에 RAM에 바이트만 복사한다(PC는 안 바꾼다). 실제 보드의 `fatload mmc ...`를 대신한다.
- `Hit any key`가 보이면 스페이스 → `=> `마다 `printenv kernel_addr_r ramdisk_addr_r fdtcontroladdr`, `setenv bootargs "console=ttyAMA0 b01_from=uboot LAB_CMD=cat~/proc/cmdline"`, `booti ${kernel_addr_r} ${ramdisk_addr_r}:0x1277e2 ${fdtcontroladdr}` (0x1277e2 = initramfs 1210338바이트).
- `LAB_CMD=`는 랩 initramfs의 `/init`이 실행하고 `poweroff -f` 하는 명령이다 → 여기서는 `cat /proc/cmdline`으로 bootargs가 사용자 공간까지 왔는지 찍고 끝난다. 그래서 **ROM 자리부터 init까지 한 번에 끝까지** 간다.
- 줄 내용은 손대지 않는다: CR, ESC가 그대로 남는다 (`file` 결과: `ASCII text, with CRLF, CR line terminators, with escape sequences`).

## 4. 힌트

<details>
<summary>힌트 1 — 호스트 접두어는 정리하기 *전에* 뗀다</summary>

U-Boot 카운트다운 줄의 실물은 `[  0.394029  0.052090] ESC[2K CR Hit any key to stop autoboot: 2 ESC[2K CR Hit any key to stop autoboot: 0 CR`이다. CR 덮어쓰기를 먼저 하면 줄 맨 앞의 `[  0.394029 ...]`가 지워진다. 접두어는 캡처 도구가 붙인 ASCII이므로 원문에서 먼저 떼고, 나머지만 `clean_text`.

</details>

<details>
<summary>힌트 2 — CR 은 "0열로 돌아가 덮어쓰기"</summary>

`screen = ""; for seg in s.split("\r"): screen = seg + screen[len(seg):]` — 뒤 조각이 짧으면 앞 조각의 꼬리가 남는다(`"abcdef\rXY"` → `"XYcdef"`). 줄 끝의 `\r\n`은 먼저 떼야 빈 조각이 화면을 지우지 않는다.

</details>

<details>
<summary>힌트 3 — 표지 없는 printk 줄의 단계</summary>

`[    1.163490] reboot: Power down`처럼 init 이후에도 커널 줄이 나온다. "printk 시각이 있으면 kernel"은 **현재 단계가 kernel보다 앞일 때만** 적용한다. 단계는 앞으로만 간다.

</details>

<details>
<summary>힌트 4 — 새 부팅을 알아보는 세 신호</summary>

① 지금 단계보다 앞 단계의 표지(커널 다음에 `U-Boot SPL ...`) ② 이 부팅에서 이미 본 배너가 또 나옴(SPL에서 멈췄다가 다시 `U-Boot SPL ...` — 단계 번호는 같아서 ①로는 못 잡는다) ③ printk 시각이 0.1 s 넘게 뒤로 감(펌웨어 출력 없이 커널만 다시 시작: kexec, 잘린 캡처).

</details>

<details>
<summary>힌트 5 — 커널 시작 시각은 첫 커널 줄의 도착 시각이 아니다</summary>

`Starting kernel ...`은 0.562 s인데 `[0.000000] Booting Linux` 줄은 0.918 s에 도착한다. 콘솔 드라이버가 등록되기 전의 printk는 버퍼에 쌓였다가 **몰아서** 나오기 때문이다. 핸드오프 줄이 있으면 그 시각을 쓰고, 없으면 `min(host − printk)`로 printk 0의 호스트 시각을 추정한다.

</details>

## 5. 풀이

### Part A — 한 줄 정리: 실물 로그에 있던 것들

실측 로그(`cat -v`로 본 원문)에 실제로 있던 것과 처리 방법:

| 원문 (실측) | 문제 | `clean_text` 결과 |
|---|---|---|
| `NOTICE:  BL1: Booting BL2^M` | 모든 줄이 CRLF | `NOTICE:  BL1: Booting BL2` |
| `^[[?25h^M` | 커서 보이기 ESC만 있는 줄 | `""` → 건너뜀 |
| `^[[2K^MHit any key ...: 2^[[2K^MHit any key ...: 0^M` | 줄 지우기 + CR 덮어쓰기 카운트다운 | `Hit any key to stop autoboot: 0` |
| `Loading Environment from Flash...^M*** Warning - bad CRC, using default environment^M` | 중간 CR | `*** Warning - bad CRC, using default environment` (터미널에 보이는 것과 같다) |
| `[^[[0;32m  OK  ^[[0m] Created slice ...` (합성, systemd) | 색 ESC, 그리고 `[`로 시작 | `[  OK  ] Created slice ...` — 숫자가 아니라서 printk 아님 |
| `\x00\xff\xff\x00` (테스트) | 전원 ON 순간 UART 잡음 | `""` |

### Part A — 단계 상태 기계

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <defs><marker id="b01-b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="14">step(): 표지·핸드오프·새 부팅 규칙</text>
  <g stroke="currentColor">
    <rect x="10" y="40" width="100" height="40" rx="6" class="f-mut"/><rect x="135" y="40" width="100" height="40" rx="6" class="f-acc"/>
    <rect x="260" y="40" width="100" height="40" rx="6" class="f-acc"/><rect x="385" y="40" width="100" height="40" rx="6" class="f-acc"/>
    <rect x="510" y="40" width="100" height="40" rx="6" class="f-ok"/><rect x="635" y="40" width="115" height="40" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-weight="bold">
    <text x="60" y="65">BootROM</text><text x="185" y="65">SPL</text><text x="310" y="65">BL31</text>
    <text x="435" y="65">U-Boot</text><text x="560" y="65">kernel</text><text x="692" y="65">init</text>
  </g>
  <g stroke="currentColor" marker-end="url(#b01-b)"><line x1="110" y1="60" x2="133" y2="60"/><line x1="235" y1="60" x2="258" y2="60"/>
    <line x1="360" y1="60" x2="383" y2="60"/><line x1="485" y1="60" x2="508" y2="60"/><line x1="610" y1="60" x2="633" y2="60"/></g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="135" y="100">U-Boot SPL …</text><text x="135" y="116">NOTICE: BL1:/BL2:</text>
    <text x="260" y="100">NOTICE: BL31:</text><text x="385" y="100">U-Boot 2026.10</text>
    <text x="510" y="100">Booting Linux</text><text x="510" y="116">또는 printk 줄</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="100">출력 없음</text><text x="10" y="116">(첫 줄까지 추정)</text>
    <text x="10" y="150" font-weight="bold">핸드오프 (이 줄의 시각 = 다음 단계 시작, 줄 자체는 앞 단계 소속)</text>
    <text x="10" y="168" font-family="monospace">SPL→BL31 "BL1: Booting BL31" · U-Boot→kernel "Starting kernel ..."</text>
    <text x="10" y="184" font-family="monospace">kernel→init "Run /init as init process"</text>
    <text x="10" y="200" font-weight="bold">새 부팅</text>
    <text x="10" y="220">① 앞 단계 표지가 뒤에서 (kernel 다음 SPL)  ② 이미 본 배너 키 (TPL·SPL·TF-A·U-Boot·Linux)</text>
    <text x="10" y="240">③ printk 시각 역행 &gt; 0.1 s.   TF-A "ERROR: ..." 같은 표지 없는 줄은 현재 단계에 남는다</text>
  </g>
</svg>
```

표지는 **줄 맨 앞에 고정(`^`)** 한다. `U-Boot \d{4}\.\d{2}`는 `U-Boot SPL 2024.04`와 겹치지 않고, 줄 중간에 "U-Boot"가 들어간 사용자 공간 출력에도 걸리지 않는다.

`NOTICE:  BL1: Booting BL31`은 재미있는 줄이다. BL2(S-EL1)는 BL31을 직접 부를 수 없어서 SMC로 BL1(EL3)에 돌아가고, **BL1이 다시 찍은 뒤** BL31로 간다 (`bl1/bl1_main.c`의 `NOTICE("BL1: Booting BL31\n")`). 그래서 BL2 줄 다음에 BL1 줄이 또 나온다 — SPL 안에서 일어나는 일이므로 SPL에 남기고, 그 시각(0.196010)을 BL31 시작으로 쓴다.

### Part A — 시간 규칙과 실측 타임라인

```bash
cd /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B01 && python3 -I solution.py qemu_tfa_uboot_boot.log
```

```text
stage      start_s     end_s    dur_ms  lines  first line / note
BootROM      0.000     0.176     175.7      0  (inferred) t=0 (power on) -> first UART line
SPL          0.176     0.196      20.3      7  NOTICE:  Booting Trusted Firmware
BL31         0.196     0.212      16.5      2  NOTICE:  BL31: v2.15.0(release):
U-Boot       0.212     0.562     349.3     25  U-Boot 2026.10 (Oct 10 2026 - 09:59:26 -0700
kernel       0.562     1.714    1151.9    238  Booting Linux on physical CPU 0x0000000000 [
init         1.714     1.858     144.1      5  lab initramfs: 6.18.55 up 1.07s
tfa_version     v2.15.0
uboot_version   2026.10
kernel_version  6.18.55
machine         linux,dummy-virt
bootargs        console=ttyAMA0 b01_from=uboot LAB_CMD=cat~/proc/cmdline
init            /init
kernel_t0_host  0.693177
printk_init     1.016896
env             kernel_addr_r=0x40400000 ramdisk_addr_r=0x44000000 fdtcontroladdr=0x8054cce0
addr            fdt_blob=0x8054cce0 ramdisk=0x7f404000 ramdisk_end=0x7f52b7e2 fdt=0x7f3fe000 fdt_end=0x7f403087 kernel=0x40400000
boot 1: last=init init=True fatal=None
```

```svg
<svg viewBox="0 0 760 290" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="14">실측 타임라인 (호스트 시각, x = 80 + 340·t)</text>
  <g fill="currentColor" font-weight="bold"><text x="10" y="52">BootROM</text><text x="10" y="78">SPL</text><text x="10" y="104">BL31</text>
    <text x="10" y="130">U-Boot</text><text x="10" y="156">kernel</text><text x="10" y="182">init</text></g>
  <rect x="80" y="40" width="59.75" height="16" class="f-mut" stroke="currentColor"/>
  <rect x="139.75" y="66" width="6.89" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="146.64" y="92" width="5.61" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="152.25" y="118" width="118.78" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="271.03" y="144" width="391.63" height="16" class="f-ok" stroke="currentColor"/>
  <rect x="662.66" y="170" width="49.01" height="16" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="146" y="52">175.7 ms — QEMU 기동 (실제 칩이면 ROM 시간)</text>
    <text x="154" y="78">20.3 ms  TF-A BL1+BL2</text><text x="158" y="104">16.5 ms  BL31</text>
    <text x="277" y="130">349.3 ms (자동 입력 대기 포함)</text>
    <text x="440" y="156" text-anchor="middle">1151.9 ms</text>
    <text x="655" y="182" text-anchor="end">144.1 ms</text>
  </g>
  <line x1="80" y1="205" x2="720" y2="205" stroke="currentColor"/>
  <g stroke="currentColor"><line x1="80" y1="205" x2="80" y2="211"/><line x1="250" y1="205" x2="250" y2="211"/>
    <line x1="420" y1="205" x2="420" y2="211"/><line x1="590" y1="205" x2="590" y2="211"/></g>
  <g fill="currentColor" text-anchor="middle"><text x="80" y="224">0</text><text x="250" y="224">0.5 s</text><text x="420" y="224">1.0 s</text><text x="590" y="224">1.5 s</text></g>
  <g stroke="currentColor" stroke-dasharray="4 3"><line x1="271.03" y1="140" x2="271.03" y2="245"/>
    <line x1="315.68" y1="140" x2="315.68" y2="262"/><line x1="391.99" y1="140" x2="391.99" y2="279"/></g>
  <g fill="currentColor" font-size="12">
    <text x="265" y="242" text-anchor="end">Starting kernel ... 0.562</text>
    <text x="321" y="259">printk 0 ≈ 0.693 (추정: min(host − printk))</text>
    <text x="397" y="276">첫 커널 줄 도착 0.918 — 콘솔 등록 때 몰아서 나온 줄</text>
  </g>
</svg>
```

경계 규칙을 줄 번호로 따라가면:

| 단계 | start 근거 (줄) | end 근거 | 비고 |
|---|---|---|---|
| BootROM | 0.0 (QEMU spawn) | SPL start | 줄 0개, `note=inferred`. QEMU엔 ROM이 없으니 이 175.7 ms는 QEMU 프로세스·머신 생성 시간 |
| SPL | 1행 `Booting Trusted Firmware` 0.175733 | 7행 `BL1: Booting BL31` 0.196010 | 7줄 |
| BL31 | 핸드오프 0.196010 | 12행 `U-Boot 2026.10` 0.212499 | 빈 줄 10·11행은 세지 않는다 |
| U-Boot | 0.212499 | 40행 `Starting kernel ...` 0.561847 | `=>` 명령 대기는 스크립트가 50 ms씩 쉬어서 생긴 것 |
| kernel | 핸드오프 0.561847 | 279행 `Run /init as init process` 1.713714 | 42행(첫 커널 줄)부터 279행까지 238줄 |
| init | 핸드오프 1.713714 | 마지막 줄 284행 1.857856 | `reboot: Power down`(printk)과 BL31의 `ERROR: QEMU System Power off`까지 5줄 |

**커널 t0 추정이 왜 min인가.** 커널 줄 하나의 `host − printk`는 "printk 시계 0의 호스트 시각 + 그 줄이 늦게 도착한 만큼"이다. 콘솔 등록 전에 쌓였다가 몰아서 나온 줄은 늦게 도착한 만큼 값이 크고, 실시간으로 나온 줄은 작다. 그래서 최솟값이 가장 좋은 추정이다 (그래도 줄 전송 시간만큼은 늦다). 실측: 0.693177 (157행 `ACPI: Interpreter disabled.` 0.977495 − 0.284318). 검산: 0.693177 + 1.016896(`Run /init`의 printk) = 1.710073 ≈ 실제 도착 1.713714. 합성 로그는 참값을 알고 있다 — 생성기 기준 printk 0 = 2.540215, 추정 2.545337 → **5.1 ms 늦게** 추정된다(115200 baud에서 줄 하나 보내는 시간 정도).

"Starting kernel ..."(0.562)과 printk 0(0.693) 사이 131 ms는 커널이 **printk 시계가 돌기 전에** 한 일이다 — `head.S`(부트 인자 보관, 페이지 테이블, MMU on), `setup_arch`의 앞부분. 이 구간의 printk는 모두 `[    0.000000]`으로 찍힌다(아래 로그의 sched_clock 줄부터 시각이 움직인다). TCG 에뮬레이션 시간이라 실제 칩에서는 훨씬 짧다.

```text
[  0.949722  0.002755] [    0.000000] clocksource: arch_sys_counter: mask: 0x1ffffffffffffff max_cycles: 0x1cd42e208c, max_idle_ns: 881590405314 ns
[  0.949737  0.000015] [    0.000049] sched_clock: 57 bits at 63MHz, resolution 16ns, wraps every 4398046511096ns
…
[  0.958875  0.000027] [    0.220643] 9000000.pl011: ttyAMA0 at MMIO 0x9000000 (irq = 13, base_baud = 0) is a PL011 rev1
[  0.958898  0.000023] [    0.222258] printk: console [ttyAMA0] enabled
[  0.970103  0.011205] [    0.275192] HugeTLB: registered 1.00 GiB page size, pre-allocated 0 pages
```

(실측 로그 발췌, 107·148행 부근. 42~148행의 107줄 — printk 0.000000~0.222258 — 이 호스트 0.918~0.959 s, 약 41 ms 동안 한꺼번에 나왔다. 콘솔 등록 뒤의 줄부터 host − printk ≈ 0.69로 일정해진다.)

### Part B — 무엇을 어디서 뽑나

| 정보 | 줄 (실측 QEMU) | 출처 코드 |
|---|---|---|
| 커널 bootargs | `Kernel command line: console=ttyAMA0 b01_from=uboot LAB_CMD=...` | `init/main.c:905` `pr_notice("Kernel command line: %s\n", ...)` |
| 커널이 모르는 인자 | `Unknown kernel command line parameters "b01_from=uboot LAB_CMD=...", will be passed to user space.` | `init/main.c:851` |
| init 경로 | `Run /init as init process` | `init/main.c:1357`, 기본값 `ramdisk_execute_command = "/init"`(165행) |
| U-Boot 주소 약속 | `kernel_addr_r=0x40400000`, `fdtcontroladdr=8054cce0` | U-Boot 환경 (`printenv`) |
| initrd 최종 위치 | `Loading Ramdisk to 7f404000, end 7f52b7e2 ... OK` | U-Boot `boot/image-board.c:591` |
| DTB 최종 위치 = x0 | `Loading Device Tree to 000000007f3fe000, end 000000007f403087 ... OK` | U-Boot `boot/image-fdt.c:313` |
| Image 이동 (합성 i.MX) | `Moving Image from 0x40480000 to 0x40600000, end=0x42a00000` | U-Boot `cmd/booti.c:83` |

U-Boot `booti`는 initrd와 DTB를 **RAM 위쪽**(U-Boot 자신 0x8158_f000 바로 아래)으로 옮기고, DTB의 `/chosen`에 `bootargs`(`boot/fdt_support.c` `fdt_chosen`)와 `linux,initrd-start/end`(`fdt_initrd`)를 써 넣은 뒤, arm64에서는 `arch/arm/lib/bootm.c:280`의 `armv8_switch_to_el2((u64)images->ft_addr, 0, 0, 0, ...)`로 점프한다 → **x0 = DTB 주소, x1~x3 = 0**. 커널 쪽에서는 `arch/arm64/kernel/head.S`의 `preserve_boot_args`가 `mov x21, x0  // x21=FDT`로 받아 `__fdt_pointer`에 저장하고, `setup.c:372`는 x1~x3이 0이 아니면 `WARNING: x1-x3 nonzero in violation of boot protocol`을 찍는다. `drivers/of/fdt.c:1116`의 `early_init_dt_scan_chosen`이 `/chosen/bootargs`를 읽어 command line이 된다.

`booti`가 Image를 옮기는 규칙은 U-Boot `arch/arm/lib/image.c`의 `booti_setup()`에 있다: Image 헤더 flags의 bit 3("2 MB 정렬만 맞으면 물리 메모리 어디든")이 켜져 있으면 `ALIGN(load − text_offset, 2 MiB) + text_offset`, 꺼져 있으면 DRAM 시작. 랩 Image 헤더는 `text_offset=0, image_size=0x2980000, flags=0xa`(bit 3 켜짐)라서 0x4040_0000은 그대로, 0x4048_0000은 0x4060_0000으로 옮겨진다 (추가 연습 2).

**가장자리 경우 — 실제로 돌린 결과**:

| 경우 | 입력 | 결과 |
|---|---|---|
| 펌웨어 없음 | `qemu_direct_kernel_boot.log` (`-kernel`) | BootROM/SPL/BL31/U-Boot `(missing)`, kernel start = 0.157495 (`note: start = min(host - printk)`), 첫 줄 도착은 0.381489 |
| 호스트 시각 없음 | 실측 로그에서 `[abs delta]` 제거 | 줄 수 `[0, 7, 2, 25, 238, 5]` 그대로, 시각 `None`, BootROM 추정 안 함. printk로 `Run /init` = 1.016896은 여전히 나온다 |
| 캡처가 커널 중간부터 | direct 로그 앞 60줄 버림 | kernel+init만 present |
| CRLF → LF | 실측 로그 `\r\n`→`\n` | 표 동일 |
| printk 리셋 | direct 로그(시각 제거) + 앞 3줄 잘린 두 번째 사본 | `Booting Linux`가 없어도 printk 1.197712 → 0.000000 역행으로 2번째 부팅 |
| 전원 ON 잡음 | `b"\x00\xff\xff\x00\n"` + 합성 로그 | 표 동일 (단계 없는 줄은 무시) |

`-kernel` 직접 부팅 로그의 커널 줄도 비교해 볼 만하다: 펌웨어 경로에서는 `psci: SMC Calling Convention v1.5`(TF-A BL31이 응답), `-kernel` 경로에서는 `psci: SMC Calling Convention v1.0`과 `psci: Trusted OS migration not required` — PSCI를 누가 구현했는지가 로그에 드러난다 (실측: 펌웨어 경로 61~65행, `-kernel` 경로 19~23행).

### Part C — 부트 루프 진단

```bash
cd /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B01 && python3 -I solution.py imx8mp_bootloop_synthetic.log
```

```text
stage      start_s     end_s    dur_ms  lines  first line / note
BootROM      0.000     0.217     216.8      0  (inferred) t=0 (power on) -> first UART line
SPL          0.217     0.959     742.7      8  U-Boot SPL 2024.04-imx (Mar 12 2026 - 10:21:
BL31         0.959     1.015      56.0      2  NOTICE:  BL31: v2.10.0  (release):lf-6.6.36-
U-Boot       1.015     2.478    1462.8     26  U-Boot 2024.04-imx (Mar 12 2026 - 10:21:44 +
kernel       2.478     3.504    1025.7     19  Booting Linux on physical CPU 0x0000000000 [
init             -         -         -      -  (missing)
…
bootargs        console=ttymxc1,115200 root=/dev/mmcblk2p2 rw
…
boot 1: last=kernel init=False fatal=Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0)
boot 2: last=SPL init=False fatal=SPL: failed to boot from all boot devices
boot 3: last=init init=True fatal=None
```

(합성 로그 결과. 표는 첫 번째 부팅만 보여 준다.)

읽는 법:

1. **부팅 1**: init이 `(missing)` — 커널에서 끝났다. 첫 치명 줄이 `VFS: Unable to mount root fs on unknown-block(0,0)`이고 bootargs에 `rootwait`이 없다. eMMC(`mmc2`)가 아직 probe되기 전에 루트 마운트를 시도한 것이다. 부팅 3의 bootargs에는 `rootwait`이 있다 — **두 부팅의 bootargs 비교**가 범인을 가리킨다.
2. **부팅 2**: 다음 `U-Boot SPL` 배너가 커널 뒤에 나왔으니 새 부팅(규칙 ①). SPL에서 `SPL: failed to boot from all boot devices` → `### ERROR ### Please RESET the board ###`(U-Boot `lib/hang.c`). U-Boot 배너가 없으니 SPL이 다음 이미지를 못 읽었다 — eMMC boot 파티션 손상, 부트 매체 선택, 전원 시퀀스를 본다.
3. **부팅 3**: SPL 배너가 **SPL 단계에서 다시** 나왔다. 단계 번호가 같아 규칙 ①로는 못 잡고, 배너 키 `SPL`을 이미 봤다는 규칙 ②가 잡는다. 이 부팅은 BootROM을 추정하지 않는다 — 리셋 순간을 모르기 때문이다(앞 부팅의 마지막 줄 뒤 언제 리셋됐는지 로그에 없다).

실제 보드라면 U-Boot의 `Reset cause:` 줄(WDOG / POR)과 부트 카운터(`bootcount`/`bootlimit`, ELX-M12)를 같이 본다.

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
cd /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B01 && python3 -I solution.py && python3 -I -W error -X dev solution.py > /dev/null && echo dev-mode-clean
# 37/37 passed, exit 0 (맥에서 약 0.1 s)
```

`capture_boot.py`(VM에서 돌리는 캡처 도구)와 `gen_synthetic_logs.py`(합성 로그 생성기 — 115200 baud 전송 시간과 콘솔 등록 전 버퍼링을 흉내 낸다)도 같은 폴더에 있다.

<!-- code: capture_boot.py -->

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 정리 후에 `[abs delta]` 접두어를 뗀다 | 카운트다운 줄의 CR 덮어쓰기가 접두어를 지워 그 줄의 시각을 잃는다 | 원문에서 `HOST_RE.match` 먼저, 나머지만 `clean_text` |
| CR을 그냥 지운다 (`s.replace("\r", "")`) | `Hit any key ...: 2Hit any key ...: 0`처럼 화면에 없던 글자열이 생긴다 | `screen = seg + screen[len(seg):]` |
| printk 줄이면 무조건 kernel | init 이후의 `reboot: Power down` 때문에 단계가 거꾸로 간다 | `cur < KERNEL`일 때만 kernel로 |
| `"U-Boot" in text`로 U-Boot 판정 | `U-Boot SPL 2024.04`가 U-Boot 본체로 잡힌다 | `^U-Boot \d{4}\.\d{2}` |
| 커널 시작 = 첫 커널 줄 도착 | 실측에서 0.918 vs 0.562 — **356 ms** 틀린다 | 핸드오프 줄 시각, 없으면 `min(host − printk)` |
| printk `[0.000000]` = 커널 진입 | sched_clock 등록 전 줄은 전부 0이다 (실측 131 ms 구간) | "printk 0 ≠ 커널 진입"을 표에 적는다 |
| TF-A `ERROR:` 줄 = 실패/리셋 | 실측 마지막 줄은 BL31이 PSCI `SYSTEM_OFF`를 처리하며 찍은 정상 메시지 | 치명 패턴은 구체적으로(`FATAL_RE`) |
| `fdtcontroladdr=8054cce0`을 10진수로 | U-Boot 숫자는 기본 16진수 | `int(v, 16)` |
| 같은 단계 배너 반복을 무시 | SPL 리셋 루프가 "부팅 1번"으로 보인다 | 배너 키 집합 `seen` |

## 8. 면접 Q&A

<details>
<summary>Q1. arm64 보드에서 전원을 켜면 셸이 뜰 때까지 무슨 일이 일어나나요? 왜 SPL이 따로 있나요?</summary>

BootROM이 부트 핀을 보고 매체에서 SPL을 **칩 내부 SRAM**에 읽어 실행 → SPL이 클럭·PMIC·**DDR training**을 하고 BL31(TF-A, EL3)과 U-Boot를 DRAM에 로드 → BL31이 보안 설정 후 U-Boot를 Non-secure EL2/EL1로 → U-Boot가 Image·DTB·initrd를 로드하고 `booti` → 커널이 드라이버를 올리고 rootfs를 마운트한 뒤 `/init` 또는 `/sbin/init` 실행. SPL이 따로 있는 이유: 리셋 직후엔 **DRAM이 안 살아 있고** SRAM은 수백 KB라 U-Boot 본체(MB 단위)가 안 들어간다. 꼬리질문 "QEMU virt에서는?" → ROM·DDR training이 없다. CPU가 secure flash 0번지의 TF-A BL1에서 리셋되고, BL2가 BL31을 secure SRAM(0x0e00_0000대)에, U-Boot를 DRAM 0x6000_0000에 올린다(이 레슨 실측 로그와 `plat/qemu/qemu/include/platform_def.h`).

</details>

<details>
<summary>Q2. 부트로더가 arm64 커널에 넘기는 것은 정확히 무엇인가요?</summary>

`Documentation/arch/arm64/booting.rst`의 약속: **x0 = DTB 물리 주소, x1 = x2 = x3 = 0**, MMU off, D-cache off(이미지 영역은 clean), 인터럽트 마스크, EL2(권장) 또는 non-secure EL1, Image는 2 MB 정렬 + `text_offset`. U-Boot는 `armv8_switch_to_el2(ft_addr, 0, 0, 0, ...)`, 커널은 `head.S`에서 `mov x21, x0`로 받는다. 꼬리질문 "x1이 0이 아니면?" → `setup.c`가 `WARNING: x1-x3 nonzero in violation of boot protocol`을 찍는다(부팅은 계속). "32비트 ARM은?" → `r0=0, r1=machine type, r2=ATAGS 또는 DTB 주소`. bootargs·initrd 위치는 레지스터가 아니라 DTB의 `/chosen`(`bootargs`, `linux,initrd-start/end`)으로 간다.

</details>

<details>
<summary>Q3. U-Boot의 bootargs는 어떤 경로로 커널과 사용자 공간까지 가나요?</summary>

U-Boot 환경 `bootargs` → `fdt_chosen()`이 DTB `/chosen/bootargs`에 기록 → 커널 `early_init_dt_scan_chosen()`이 읽어 `boot_command_line` → `Kernel command line:` 출력 → 커널 파라미터로 처리되고, 커널이 모르는 `a=b` 꼴은 init의 **환경 변수**(`envp_init`)로, `=` 없는 것은 argv(`argv_init`)로 넘어간다 (`init/main.c` `unknown_bootoption`)(`Unknown kernel command line parameters "...", will be passed to user space.`). 실측: `b01_from=uboot LAB_CMD=...`가 그렇게 넘어가 `/proc/cmdline`에도 그대로 보였다. 꼬리질문 "setenv bootargs를 했는데 반영이 안 된다" → `bootcmd`가 `run mmcargs` 같은 스크립트로 bootargs를 **덮어쓰는** 경우가 흔하다. 커널 쪽 `CONFIG_CMDLINE_FORCE`도 DT 값을 무시한다. 로그의 `Kernel command line:`이 진실이다 (추가 연습 5).

</details>

<details>
<summary>Q4. "Starting kernel ..." 다음에 아무것도 안 나옵니다. 어떻게 좁히나요?</summary>

커널 콘솔이 켜지기 전에 죽었을 가능성이 크다(이 레슨 실측에서도 그 구간이 356 ms 침묵이었다). 순서: ① `earlycon`(예: `earlycon=pl011,0x9000000`, i.MX `earlycon=ec_imx6q,0x30890000`)을 bootargs에 넣어 초기 printk를 바로 보이게 ② `console=` 이름 오타(ttyAMA0/ttymxc1/ttyS0) ③ DTB가 이 보드 것인지, UART 노드·`/memory`가 맞는지 ④ 로드 영역 겹침 — Image의 `image_size`가 커져 DTB/initrd를 덮는지(추가 연습 3) ⑤ 커널 config에 그 UART 드라이버가 built-in인지 ⑥ JTAG로 PC가 어디서 도는지, 또는 리셋 후 U-Boot에서 `__log_buf`(System.map 주소)를 `md`로 덤프. 꼬리질문 "printk는 버퍼에 있는데 왜 안 보이나" → 콘솔 드라이버 등록 전 printk는 링 버퍼에만 쌓인다.

</details>

<details>
<summary>Q5. 부팅 시간을 재려고 dmesg 시각을 그대로 쓰면 무엇이 빠지나요?</summary>

① 커널 이전 전부(ROM, SPL DDR training, U-Boot, 이미지 로드) ② 커널 진입부터 sched_clock 등록까지(모두 `0.000000`) ③ UART 출력 비용. 그래서 **외부 기준 시계**(grabserial 같은 캡처 도구, 또는 GPIO 토글 + 로직 분석기)로 재고, 커널 내부는 printk/`initcall_debug`로 쪼갠다(ELX-A08). 꼬리질문 "115200 baud가 부팅을 늦추나?" → 초당 약 11.5 KB라 80자 줄 하나에 약 7 ms. 콘솔 출력이 동기식이면 그대로 부팅 시간이 된다 → 양산 이미지는 `quiet`/`loglevel`을 쓴다. 이 레슨 합성 로그도 그 모델로 만들었다.

</details>

<details>
<summary>Q6. TF-A의 BL1/BL2/BL31/BL32/BL33은 각각 무엇이고, BL31은 왜 부팅 후에도 남아 있나요?</summary>

BL1 = AP Trusted ROM(EL3, 리셋 벡터), BL2 = Trusted Boot Firmware(다음 이미지 로드·검증, 보통 S-EL1 또는 EL3), BL31 = EL3 Runtime Firmware(보안 모니터), BL32 = Secure-EL1 Payload(OP-TEE 같은 TEE, 선택), BL33 = Non-trusted Firmware(U-Boot/UEFI). 실제 SoC는 BL1 대신 자체 ROM을, BL2 대신 U-Boot SPL을 쓰는 경우가 많다(i.MX8M: ROM → SPL → BL31 → U-Boot). BL31이 남는 이유: **PSCI**(CPU on/off, suspend, SYSTEM_OFF/RESET)를 SMC로 서비스해야 한다. 이 레슨 실측 로그의 마지막 줄 `ERROR:   QEMU System Power off: with GPIO.`가 그 증거 — 리눅스가 `poweroff`로 PSCI SYSTEM_OFF를 부르자 BL31이 찍었다.

</details>

<details>
<summary>Q7. initramfs의 /init과 디스크 rootfs의 /sbin/init은 어떻게 고르나요? "No working init found"는 언제 나오나요?</summary>

initramfs가 있으면 커널은 `rdinit=`(기본 `/init`)을 실행한다(`init/main.c`의 `ramdisk_execute_command`). 없거나 실패하면 `root=`의 rootfs를 마운트하고 `init=` 또는 `/sbin/init`, `/etc/init`, `/bin/init`, `/bin/sh` 순으로 시도, 모두 실패하면 `panic("No working init found. ...")`. 꼬리질문 "rootwait은 왜?" → eMMC/USB처럼 늦게 probe되는 장치는 루트 마운트 시점에 아직 없을 수 있다. `rootwait`이 없으면 Part C 부팅 1처럼 `VFS: Unable to mount root fs`로 패닉한다. init이 종료하면 `Attempted to kill init!` 패닉(ELX-B04에서 실제로 본다).

</details>

<details>
<summary>Q8. 고객사에서 "가끔 재부팅 루프"라며 2 MB짜리 UART 로그를 보냈습니다. 어떻게 분석하나요?</summary>

① 배너(TPL/SPL/TF-A/U-Boot/Linux)와 printk 리셋으로 **부팅 단위로 자른다** ② 부팅마다 마지막 도달 단계와 첫 치명 줄(panic, abort, `failed to boot`, `Please RESET`)을 표로 ③ 성공 부팅과 실패 부팅의 차이(bootargs, `Reset cause:`, 온도·전압 줄, DDR training 결과, 커널 버전/슬롯)를 비교 ④ 리셋 원인 분류: 워치독(실패 뒤 일정 간격으로 재시작), 브라운아웃(로그 중간이 끊기고 바로 ROM 배너), 소프트웨어 reboot(`reboot:` 줄). 이 레슨의 `diagnose()`가 ①②를 자동화한다. 꼬리질문 "A/B 슬롯이면?" → U-Boot `bootcount`/`bootlimit`과 `altbootcmd`로 다른 슬롯으로 넘어간다(ELX-M12).

</details>

## 9. 추가 연습

### 연습 1 — 부팅 시간 예산 초과

단계별 길이(ms)와 예산(ms) 사전을 받아 초과한 단계와 초과량을 큰 순서로 돌려준다. 예산 `SPL 500, BL31 50, U-Boot 1000, kernel 1500, init 3000`이면 실측 QEMU는 통과, 합성 i.MX8MP는?

<details>
<summary>답 보기</summary>

`[("init", 1281.3), ("U-Boot", 462.8), ("SPL", 242.7), ("BL31", 6.0)]`. U-Boot 1462.8 ms 중 약 1 s는 `bootdelay=1` 자동 부팅 대기다 — 양산에서는 `bootdelay=0`(또는 -2)이 가장 싼 최적화다. SPL 742.7 ms는 대부분 DDR training(합성값). 실제 보드는 training 결과를 저장해 두고 재사용하는 방법을 SoC 벤더가 제공하기도 한다(ELX-A08).

</details>

### 연습 2 — arm64 Image 헤더와 booti 이동 주소

랩 커널 `/var/lab/out/Image`의 앞 64바이트(VM에서 `xxd -l 64`로 읽은 실물)를 해석하고, U-Boot `booti_setup()`처럼 실제 실행 주소를 계산하라.

```text
00000000: 4d5a 40fa 3ba8 7f14 0000 0000 0000 0000  MZ@.;...........
00000010: 0000 9802 0000 0000 0a00 0000 0000 0000  ................
00000020: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000030: 0000 0000 0000 0000 4152 4d64 4000 0000  ........ARMd@...
```

<details>
<summary>답 보기</summary>

리틀 엔디언: 0x08 `text_offset = 0`, 0x10 `image_size = 0x2980000`(43.5 MB, 파일 42,740,224바이트보다 크다 — BSS 등 실행 중 필요한 전체 크기), 0x18 `flags = 0xa` → bit0 = 0(LE), bit1-2 = 01(4 KB 페이지), bit3 = 1(어디든), 0x38 매직 `ARMd`(0x644d5241). 첫 4바이트 `MZ`는 EFI stub용 PE 헤더 흔적이다. 이동: `ALIGN(0x40400000, 2M) = 0x40400000`(이동 없음 — 실측 로그에 `Moving Image` 줄이 없는 이유), `ALIGN(0x40480000, 2M) = 0x40600000`(합성 i.MX 로그의 `Moving Image from 0x40480000 to 0x40600000`). bit3이 꺼진 옛 커널이면 DRAM 시작으로 옮긴다.

</details>

### 연습 3 — 로드 영역 겹침

`{이름: (시작, 크기)}`에서 겹치는 쌍을 찾아라. QEMU: kernel `0x40400000 + image_size`, initrd `0x44000000 + 0x1277e2`, QEMU DTB `0x40000000 + 1 MiB`. `ramdisk_addr_r=0x42000000`으로 바꾸면? 합성 i.MX에서 Image가 36 MiB → 43 MiB로 커지면?

<details>
<summary>답 보기</summary>

반열린 구간 `[s, s+z)` 두 개는 `sa < sb+zb and sb < sa+za`일 때 겹친다. QEMU 기본값은 겹침 없음(커널 끝 0x42D8_0000 < 0x4400_0000). `ramdisk_addr_r=0x42000000`이면 커널과 겹친다 — U-Boot가 initrd를 먼저 읽고 `booti`가 커널을 옮기면 조용히 깨진다. i.MX는 커널 0x4060_0000 + 0x2b00000 = 0x4310_0000 > DTB 0x4300_0000 → **커널이 DTB를 밟는다**. 커널 config에 드라이버를 잔뜩 built-in으로 넣은 날 "Starting kernel ..." 뒤에 멈추는 전형적인 원인이다. (i.MX8MP 주소 0x40480000/0x43000000은 NXP EVK 기본 환경의 기억값이며 이 랩에서 확인하지 않았다.)

</details>

### 연습 4 — 가장 긴 침묵

연속한 두 줄의 호스트 시각 차이 상위 3개를 찾아라. 그 시간은 "어느 줄이 느린가"가 아니라 "그 줄 **앞에서** 무엇이 조용히 일했나"다.

<details>
<summary>답 보기</summary>

실측: ① 0.355760 s, 42행 `Booting Linux on physical CPU` 앞 — 커널 진입부터 콘솔 등록까지(출력이 버퍼에만 쌓임) ② 0.115909 s, 202행 `Key type asymmetric registered` 앞 — 어떤 initcall이 오래 걸렸는지는 `initcall_debug`로 봐야 안다(ELX-A08) ③ 0.091315 s, 280행 `lab initramfs:` 앞 — `/init` 셸 스크립트가 proc/sys/dev를 마운트하고 `uname`·`cut`을 실행한 시간. (TCG 시간이라 절대값은 의미 없다.)

</details>

### 연습 5 — bootargs 추적

U-Boot `=> setenv bootargs "..."`, 커널 `Kernel command line:`, `Unknown kernel command line parameters "..."`를 뽑아, U-Boot가 넣은 것과 커널이 받은 것이 같은지, 사용자 공간으로 넘어간 인자가 무엇인지 돌려준다.

<details>
<summary>답 보기</summary>

실측: 같음(`same=True`), 사용자 공간으로 `["b01_from=uboot", "LAB_CMD=cat~/proc/cmdline"]`. 직접 부팅 로그는 U-Boot `setenv` 줄이 없어서 `same=False`. 테스트는 커널 쪽을 `console=ttyS0`으로 바꾼 로그로 불일치를 잡는다 — 실제로는 `bootcmd` 스크립트가 bootargs를 덮어쓰거나, 커널이 `CONFIG_CMDLINE_FORCE`일 때 생긴다.

</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
cd /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B01 && python3 -I practice.py && python3 -I -W error -X dev practice.py > /dev/null && echo dev-mode-clean
# 18/18 passed
```

## 10. 핵심 정리

- **ROM → SPL(SRAM, DDR training) → BL31(EL3 상주, PSCI) → U-Boot(DRAM) → Linux → /init(PID 1)**. 각 단계는 다음 단계를 더 큰 메모리에 올리고 바통을 넘긴다.
- arm64 핸드오프: **x0 = DTB 물리 주소, x1~x3 = 0**, MMU off, Image 2 MB 정렬. bootargs와 initrd 위치는 DTB `/chosen`으로 간다.
- 실측 QEMU: `kernel_addr_r=0x40400000`, initrd → 0x7f40_4000, DTB → 0x7f3f_e000, U-Boot 재배치 0x8158_f000, BL31 secure SRAM.
- 단계 경계는 **핸드오프 줄**(`BL1: Booting BL31`, `Starting kernel ...`, `Run /init as init process`)로 자른다. 첫 커널 줄의 도착 시각은 356 ms 늦었다(콘솔 등록 때 몰아서 출력).
- printk 0 ≠ 커널 진입. 호스트 기준 시계 + `min(host − printk)`로 정렬한다.
- 로그 정리: 접두어 먼저 떼고, CR은 덮어쓰기, ESC·깨진 바이트 제거, 표지는 `^`로 고정, 단계는 앞으로만.
- 부트 루프: 배너 반복·앞 단계 표지·printk 역행으로 자르고, 부팅마다 마지막 단계·첫 치명 줄·bootargs를 비교한다.

## 참고자료

- Linux `Documentation/arch/arm64/booting.rst` — arm64 부트 프로토콜, Image 헤더 (랩 소스 `/var/lab/src/linux-6.18.55`)
- Linux `init/main.c` (Kernel command line, Unknown parameters, `Run %s as init process`, No working init found), `arch/arm64/kernel/head.S`(`preserve_boot_args`), `arch/arm64/kernel/setup.c`, `drivers/of/fdt.c`(`early_init_dt_scan_chosen`)
- U-Boot `cmd/booti.c`, `arch/arm/lib/image.c`(`booti_setup`), `arch/arm/lib/bootm.c`, `boot/fdt_support.c`(`fdt_chosen`, `fdt_initrd`), `boot/image-fdt.c`, `boot/image-board.c` (랩 소스 `/var/lab/src/u-boot`)
- TF-A `bl1/bl1_main.c`, `plat/qemu/qemu/include/platform_def.h`; [TF-A firmware design](https://trustedfirmware-a.readthedocs.io/en/latest/design/firmware-design.html)
- [U-Boot SPL / Falcon mode 문서](https://docs.u-boot.org/en/latest/usage/index.html), [grabserial (Tim Bird)](https://github.com/tbird20d/grabserial)
- Don의 노트 `EmbeddedLinux/C_boot/2026-10-10_C1_boot_map.md`(같은 랩에서의 부트 지도·시간 측정), C3 U-Boot, C5 TF-A, C6 커널 시작, C7 init
- 관련 레슨: BM-B08(Cortex-M 리셋 → main), ELX-B04(initramfs와 PID 1), ELX-B05(Device Tree), ELX-M12(U-Boot env·FIT), ELX-A08(부트 시간 최적화)
