# A10. 셸·커맨드라인 생존 키트 — Embedded Linux 엔지니어가 하루 종일 쓰는 명령

> **이 노트를 다 읽으면**: 보드 앞에서 "무엇이 궁금한가"에 따라 맞는 명령을 바로 고를 수 있다 · 드라이버가 load/probe 됐는지, 누가 장치를 잡고 있는지, 왜 느린지를 셸만으로 좁힐 수 있다 · `set -euo pipefail`·파이프·awk로 안전한 진단 스크립트를 짤 수 있다 · i2c-tools·libgpiod·can-utils·devmem의 사용법과 위험을 설명할 수 있다
> **왜 필요한가**: 면접의 "probe가 안 불려요, 어떻게 디버그하나요?"와 입사 첫 주의 "보드 붙여 놨으니 CAN 좀 살려 봐"는 결국 같은 질문이다 — 셸에서 몇 개의 명령으로 상태를 읽어 내는 능력.
> **Don 기준 난이도**: scope·로직 분석기로 "측정해서 좁히는" 사고방식은 이미 강점 / 그 측정 도구가 Linux에서는 `/proc`·`/sys`·`dmesg`·`strace`라는 점, 그리고 셸 문법의 함정은 새로 익힌다
> **선행 노트**: A1(everything is a file), A2(프로세스), A3(파일 I/O·ioctl) — 없어도 읽을 수 있다

---

## 0. 큰 그림 — 보드 앞에서 던지는 여섯 가지 질문

bare-metal에서 Don이 보드를 디버그할 때는 JTAG으로 레지스터를 읽고, UART 로그를 보고, scope로 핀을 찍었다. Embedded Linux 보드에서도 하는 일은 같다. 다만 **커널이 이미 그 정보를 파일로 내놓고 있어서**, 대부분의 "측정"은 셸에서 파일을 읽는 것으로 끝난다. `/proc/interrupts`는 IRQ 카운터 레지스터 덤프, `/sys/bus/i2c/devices/`는 버스 스캔 결과, `dmesg`는 UART 로그 버퍼라고 생각하면 된다.

이 노트는 명령을 알파벳 순으로 외우지 않는다. 대신 **보드 앞에서 실제로 던지는 질문** 여섯 개를 기준으로 묶는다.

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="170" y="8" width="340" height="36" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="31" font-size="14" text-anchor="middle" font-weight="bold">보드가 이상하다 — 지금 무엇을 묻는가?</text>
  <line x1="340" y1="44" x2="340" y2="348" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
  <rect x="10" y="58" width="315" height="84" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="22" y="82" font-size="13" font-weight="bold">① 어떤 HW·드라이버가 있나?</text>
  <text x="22" y="106" font-size="12">lspci -k · lsusb -t · lsblk · gpiodetect</text>
  <text x="22" y="126" font-size="12">/proc/iomem · /sys/bus/*/devices</text>
  <rect x="355" y="58" width="315" height="84" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="367" y="82" font-size="13" font-weight="bold">② 드라이버가 load·probe 됐나?</text>
  <text x="367" y="106" font-size="12">lsmod · modinfo · modprobe -v · dmesg</text>
  <text x="367" y="126" font-size="12">/sys/bus/*/drivers · devices_deferred</text>
  <rect x="10" y="152" width="315" height="84" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="22" y="176" font-size="13" font-weight="bold">③ 누가 이 장치를 쓰나?</text>
  <text x="22" y="200" font-size="12">fuser -v · lsof /dev/X · /proc/PID/fd</text>
  <text x="22" y="220" font-size="12">strace -p PID · ps -o wchan</text>
  <rect x="355" y="152" width="315" height="84" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="367" y="176" font-size="13" font-weight="bold">④ 왜 느린가?</text>
  <text x="367" y="200" font-size="12">top · pidstat · vmstat 1 · iostat -x</text>
  <text x="367" y="220" font-size="12">free · /proc/interrupts · irqrate.sh</text>
  <rect x="10" y="246" width="315" height="84" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="22" y="270" font-size="13" font-weight="bold">⑤ 커널이 뭐라고 했나?</text>
  <text x="22" y="294" font-size="12">dmesg -T -w --level · journalctl -k -f</text>
  <text x="22" y="314" font-size="12">dynamic_debug · udevadm monitor</text>
  <rect x="355" y="246" width="315" height="84" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="367" y="270" font-size="13" font-weight="bold">⑥ 네트워크·CAN이 살아 있나?</text>
  <text x="367" y="294" font-size="12">ip -br a · ip -d -s link · ss -tulpn</text>
  <text x="367" y="314" font-size="12">ping · tcpdump · candump · cansend</text>
  <line x1="340" y1="100" x2="327" y2="100" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="100" x2="353" y2="100" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="194" x2="327" y2="194" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="194" x2="353" y2="194" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="288" x2="327" y2="288" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="340" y1="288" x2="353" y2="288" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="170" y="350" width="340" height="36" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="373" font-size="13" text-anchor="middle">모두 한 번에 모으기 → debug_bundle.sh (§13)</text>
</svg>
```
그림 1 — 보드 triage 흐름. 증상이 아니라 "지금 묻는 질문"으로 도구를 고른다. 이 노트의 §4~§10이 각 상자에 대응한다.

실습 환경: Mac 위 Lima VM `elinux` (aarch64, Ubuntu 26.04.1, `uname -r` = `7.0.0-34-generic`, bash 5.3.9). 이 VM에는 실제 I2C·GPIO·CAN 하드웨어가 없다. 그래서 커널에 들어 있는 **가짜 하드웨어 드라이버**를 써서 진짜 명령을 진짜로 돌린다.

| 실제 보드의 것 | 이 노트에서 대신 쓴 것 | 커널 모듈 |
|---|---|---|
| SoC의 I2C 컨트롤러 + IMU·기압 센서 | SMBus 가상 어댑터, 0x68·0x76에 가상 칩 | `i2c-stub` |
| SoC GPIO 컨트롤러 | configfs로 만드는 가상 GPIO 칩 | `gpio-sim` |
| CAN 컨트롤러 (FlexCAN, MCP2518FD 등) | bit timing까지 받는 가상 CAN / 루프백 CAN | `dummy_can`, `vcan` |
| 보드 UART 콘솔 | `socat`으로 만든 pty 한 쌍 | 없음 (유저 공간) |

`i2c-stub` 위에 커널의 **진짜 bmp280·mpu6050 드라이버**를 붙여서 probe 성공·실패까지 재현한다. 같은 VM에서 다른 노트의 실습 프로그램(`udp_rx`, `ex5`, `vcan0` 등)이 동시에 돌고 있어서 출력에 그런 이름이 섞여 있다. 출력은 모두 실제 실행 결과이고, 길면 줄을 잘랐다고 적었다. PID·시각·카운터 값은 실행마다 다르다.

---

## 1. 셸 기본기 — 파이프, 리다이렉션, exit code

### 1.1 세 개의 fd: stdin(0), stdout(1), stderr(2)

모든 프로세스는 태어날 때 fd 0·1·2를 물려받는다 (A1·A3에서 본 fd 테이블의 0~2번 칸). `>`는 fd 1을, `2>`는 fd 2를 파일로 바꿔 끼운다. `2>&1`은 "fd 2를 **지금 fd 1이 가리키는 곳**으로 복사하라"(내부적으로 `dup2(1, 2)`)는 뜻이다. 그래서 **순서가 중요하다** — 셸은 리다이렉션을 왼쪽부터 차례로 적용한다.

무엇을 확인하나: `> file 2>&1`과 `2>&1 > file`이 다르게 동작하는 것.

```sh
cd /tmp/A10
ls /dev/i2c-0 /dev/i2c-9 > out.txt 2>&1; echo "rc=$?"; cat out.txt
ls /dev/i2c-0 /dev/i2c-9 2>&1 > out2.txt; echo "--- out2.txt:"; cat out2.txt
```

```text
rc=2
ls: cannot access '/dev/i2c-9': No such file or directory
/dev/i2c-0
ls: cannot access '/dev/i2c-9': No such file or directory
--- out2.txt:
/dev/i2c-0
```

출력에서 볼 것: 첫 줄은 에러까지 파일에 들어갔다(`cat`으로 둘 다 나옴). 두 번째는 `2>&1`이 먼저 적용될 때 fd 1이 아직 터미널이라 에러는 화면으로, 정상 출력만 파일로 갔다. 빌드 로그를 통째로 남길 때는 항상 `make > build.log 2>&1` 순서다. bash 전용 축약형 `&> build.log`도 같은 뜻이다.

### 1.2 exit code — 명령의 "리턴값"

C의 `main()` 리턴값(0~255)이 셸의 `$?`다. **0이 성공**, 0이 아니면 실패다(C 함수 관례 `0 = OK`와 같다). `grep -q`처럼 출력 없이 exit code만으로 답하는 명령이 스크립트의 조건문 재료다.

```sh
grep -q vcan /proc/modules; echo "grep rc=$?"; true; echo $?; false; echo $?
cat /dev/i2c-9 | wc -l; echo "rc=$? PIPESTATUS=${PIPESTATUS[*]}"
set -o pipefail; cat /dev/i2c-9 | wc -l; echo "rc=$?"; set +o pipefail
```

```text
grep rc=0
0
1
cat: /dev/i2c-9: No such file or directory
0
rc=0 PIPESTATUS=1 0
cat: /dev/i2c-9: No such file or directory
0
rc=1
```

출력에서 볼 것: 파이프라인의 exit code는 기본적으로 **마지막 명령**(`wc`)의 것이라 `cat` 실패가 묻힌다(`rc=0`). `PIPESTATUS` 배열에는 각 단계의 값(`1 0`)이 남는다. `pipefail`을 켜면 실패한 단계가 하나라도 있으면 파이프라인 전체가 실패(`rc=1`)가 된다.

### 1.3 `set -euo pipefail` — 스크립트의 assert

Don이 펌웨어에서 `ASSERT()`를 켜 두고 개발하듯, 셸 스크립트에는 strict mode를 켠다.

| 옵션 | 뜻 | 펌웨어 비유 |
|---|---|---|
| `-e` (errexit) | 명령이 실패하면 즉시 스크립트 종료 | 에러 리턴을 무시하지 않는 `CHECK(rc)` |
| `-u` (nounset) | 정의 안 된 변수를 쓰면 에러 | 초기화 안 된 변수 사용 경고 |
| `-o pipefail` | 파이프 중간 실패도 실패로 본다 | 체인의 중간 단계 에러 전파 |
| `trap '...' ERR` | 실패 지점에서 훅 실행 | HardFault 핸들러에서 PC 출력 |

무엇을 확인하나: IMU가 없는데도 "캘리브레이션 굽기"까지 진행해 버리는 스크립트를 strict mode가 막는 것. 스크립트(`~/.elinux_scratch/A10/strict_demo.sh`):

```sh
#!/usr/bin/env bash
# strict mode가 없을 때와 있을 때: 실패를 무시하고 계속 가는가?
mode=${1:-loose}
if [ "$mode" = strict ]; then
  set -euo pipefail
  trap 'echo "ERR: line $LINENO: $BASH_COMMAND (rc=$?)" >&2' ERR
fi

echo "[$mode] step1: read IMU WHO_AM_I"
who=$(cat /sys/bus/i2c/devices/0-0068/name 2>/dev/null | tr -d ' ')
echo "[$mode] step2: who='$who' -> flashing calibration..."
echo "[$mode] step3: typo var = ${CALIB_FILE_TYPO}"
echo "[$mode] done"
```

```sh
./strict_demo.sh loose; echo "exit=$?"; ./strict_demo.sh strict; echo "exit=$?"
bash -uc 'echo calib=${CALIB_FILE_TYPO}'; echo rc=$?
```

```text
[loose] step1: read IMU WHO_AM_I
[loose] step2: who='' -> flashing calibration...
[loose] step3: typo var = 
[loose] done
exit=0
[strict] step1: read IMU WHO_AM_I
ERR: line 10: who=$(cat /sys/bus/i2c/devices/0-0068/name 2> /dev/null | tr -d ' ') (rc=1)
exit=1
bash: line 1: CALIB_FILE_TYPO: unbound variable
rc=127
```

출력에서 볼 것: loose 모드는 IMU가 없는데(`who=''`) 캘리브레이션 단계로 넘어가고, 오타 변수도 빈 문자열로 조용히 넘어가며 `exit=0`으로 "성공"한다. strict 모드는 10번째 줄에서 멈추고 어느 명령이 왜 실패했는지 찍는다. `-u` 단독 위반은 이 VM의 bash에서 `rc=127`로 끝났다.

함정: `-e`는 `if`·`while`의 조건, `&&`·`||`의 왼쪽, `!` 뒤의 명령에서는 동작하지 않는다. "실패해도 되는" 명령은 `cmd || true`로 의도를 드러낸다. 반대로 진단 스크립트처럼 **하나가 실패해도 끝까지 모아야 하는** 경우에는 `-e`를 일부러 뺀다 (§13의 debug_bundle.sh).

### 1.4 따옴표 — 단어 분리와 확장

셸은 따옴표 없는 `$var`를 공백에서 쪼개고(word splitting), glob 문자가 있으면 파일 이름으로 펼친다. 큰따옴표 `"..."` 안에서는 변수·`$(...)`만 확장되고 쪼개지지 않는다. 작은따옴표 `'...'` 안에서는 아무것도 확장되지 않는다.

```sh
f="imu log.txt"; touch "$f"; ls -l $f 2>&1; ls -l "$f"
n=can0; echo "dev=$n"; echo 'dev=$n'
```

```text
ls: cannot access 'imu': No such file or directory
ls: cannot access 'log.txt': No such file or directory
-rw-rw-r-- 1 donh donh 0 Oct  5 22:43 imu log.txt
dev=can0
dev=$n
```

출력에서 볼 것: 따옴표 없는 `$f`는 `imu`와 `log.txt` 두 인자로 쪼개졌다. 규칙은 하나다 — **변수는 항상 `"$var"`로 쓴다.** `limactl shell ... bash -lc '...'`나 `ssh board '...'`처럼 명령을 문자열로 넘길 때는 "어느 셸이 어느 따옴표를 벗기는가"를 한 번 더 생각해야 한다(바깥 셸이 먼저 한 겹 벗긴다).

### 1.5 반복문 — `/sys`를 훑는 기본 패턴

`/sys/class/<클래스>/` 아래 장치마다 같은 속성 파일이 있으므로, `for` 한 줄이면 "장치 목록 + 상태" 표가 나온다.

```sh
for n in /sys/class/net/*; do printf "%-6s %-5s mtu=%s\n" "$(basename "$n")" "$(cat "$n/operstate")" "$(cat "$n/mtu")"; done
while read -r name _ ; do echo "chip: $name"; done < <(sudo gpiodetect)
```

```text
eth0   up    mtu=1500
lo     unknown mtu=65536
chip: gpiochip0
chip: gpiochip1
chip: gpiochip2
```

출력에서 볼 것: `while read -r a b`는 한 줄을 공백 기준으로 나눠 변수에 넣는다(`_`는 나머지를 버리는 관례). `< <(cmd)`는 process substitution으로, `cmd`를 파이프로 `while`에 넘기는 형태와 달리 루프가 서브셸이 아니어서 루프 안에서 바꾼 변수가 루프 밖에서도 살아 있다.

### 1.6 here-doc — 설정 파일·udev 규칙을 스크립트 안에서 만들기

`<<EOF ... EOF`는 여러 줄 문자열을 stdin으로 넣는다. `EOF`에 따옴표를 붙이면(`<<'EOF'`) 안의 `$`가 확장되지 않는다 — udev 규칙이나 다른 스크립트를 생성할 때 필수다.

```sh
cat > sensors.conf <<EOF
# generated on $(hostname) kernel $(uname -r)
imu_bus=/dev/i2c-0
imu_addr=0x68
EOF
cat sensors.conf
cat <<'EOF'
echo $HOME is not expanded here
EOF
```

```text
# generated on lima-elinux kernel 7.0.0-34-generic
imu_bus=/dev/i2c-0
imu_addr=0x68
echo $HOME is not expanded here
```

### 1.7 `sudo echo > /sys/...`가 안 되는 이유와 `tee`

`sudo echo X > file`에서 `>`는 **sudo가 실행되기 전에 지금의(일반 사용자) 셸**이 연다. 그래서 root만 쓸 수 있는 sysfs 파일은 `Permission denied`가 난다. 해결은 `sudo tee`(파일을 여는 프로세스 자체가 root) 또는 `sudo sh -c "..."`.

```sh
D=$(ls -d /sys/devices/platform/gpio-sim.1/gpiochip*)
sudo echo pull-down > $D/sim_gpio3/pull 2>&1; echo "rc=$?"
echo pull-down | sudo tee $D/sim_gpio3/pull; echo "rc=$?"
sudo sh -c "echo pull-up > $D/sim_gpio3/pull"; echo "rc=$?"; sudo gpioget imu_int
```

```text
bash: line 3: /sys/devices/platform/gpio-sim.1/gpiochip2/sim_gpio3/pull: Permission denied
rc=1
pull-down
rc=0
rc=0
"imu_int"=active
```

출력에서 볼 것: `tee`는 받은 내용을 stdout에도 다시 찍는다(`pull-down`). 출력이 싫으면 `> /dev/null`을 붙인다. `tee -a`는 이어 쓰기라서 "화면에도 보고 로그에도 남기기"에 쓴다.

```sh
sudo i2cget -y 0 0x68 0x75 | tee -a /tmp/A10/probe.log; cat /tmp/A10/probe.log
```

```text
0x71
0x71
```

---

## 2. 텍스트 도구 — find, grep, xargs, sed, awk, cut·sort·uniq

Linux는 상태를 **텍스트**로 내놓는다. 그래서 텍스트를 거르고 세는 작은 도구를 파이프로 잇는 것이 곧 "로직 분석기의 트리거·필터·카운터"다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="40" width="140" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="80" y="67" font-size="13" text-anchor="middle">sudo dmesg</text>
  <rect x="180" y="40" width="140" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="250" y="67" font-size="12" text-anchor="middle">sed -E: 시각 제거</text>
  <rect x="350" y="40" width="140" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="420" y="67" font-size="13" text-anchor="middle">cut -d: -f1</text>
  <rect x="520" y="40" width="140" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="590" y="67" font-size="13" text-anchor="middle">sort</text>
  <line x1="150" y1="62" x2="178" y2="62" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="320" y1="62" x2="348" y2="62" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="490" y1="62" x2="518" y2="62" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="18" y="104" font-size="12">[ 0.06] audit: ...</text>
  <text x="188" y="104" font-size="12">audit: type=1400</text>
  <text x="358" y="104" font-size="12">audit</text>
  <text x="528" y="104" font-size="12">audit audit ...</text>
  <line x1="590" y1="84" x2="590" y2="168" stroke="currentColor" marker-end="url(#ah2)"/>
  <rect x="520" y="170" width="140" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="590" y="197" font-size="13" text-anchor="middle">uniq -c</text>
  <rect x="350" y="170" width="140" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="420" y="197" font-size="13" text-anchor="middle">sort -rn</text>
  <rect x="180" y="170" width="140" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="250" y="197" font-size="13" text-anchor="middle">head -6</text>
  <rect x="10" y="170" width="140" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="80" y="197" font-size="13" text-anchor="middle">터미널 (tty)</text>
  <line x1="520" y1="192" x2="492" y2="192" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="350" y1="192" x2="322" y2="192" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="180" y1="192" x2="152" y2="192" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="528" y="234" font-size="12">58 audit</text>
  <text x="358" y="234" font-size="12">147 systemd[1]</text>
  <text x="188" y="234" font-size="12">상위 6줄만</text>
  <line x1="40" y1="84" x2="40" y2="168" stroke="#d0564a" stroke-dasharray="5 4" marker-end="url(#ah2)"/>
  <text x="48" y="140" font-size="12">stderr(2) → 바로 터미널</text>
  <text x="10" y="22" font-size="12">각 화살표 = 커널 pipe: 앞 프로세스 fd 1 → 뒤 프로세스 fd 0</text>
  <text x="10" y="270" font-size="12">7개 프로세스가 동시에 실행된다. pipe 버퍼(기본 64 KiB)가 차면 앞 단계가 write()에서 block 된다.</text>
</svg>
```
그림 2 — `sudo dmesg`에서 `head`까지 이어지는 7단 파이프라인의 데이터 흐름. 각 단계는 별개 프로세스이고, stdout만 다음 단계로 흐른다. stderr는 `2>&1`을 붙이지 않는 한 파이프를 타지 않는다.

### 2.1 find — 파일 시스템 전체를 조건으로 훑기

| 형태 | 뜻 |
|---|---|
| `find DIR -name '*can*.ko*'` | 이름 패턴 (glob은 따옴표로 감싼다) |
| `-type f` / `-type d` / `-type l` | 일반 파일 / 디렉터리 / 심볼릭 링크 |
| `-maxdepth N` | 깊이 제한 (`/sys`는 링크 순환이 많아서 필수에 가깝다) |
| `-mmin -10` / `-newer FILE` | 최근 10분 내 수정 / FILE보다 새로움 |
| `-exec CMD {} \;` / `-print0` | 찾은 파일마다 실행 / NUL 구분 출력 (xargs -0과 짝) |

```sh
find /lib/modules/$(uname -r) -name "*can*.ko*" | head -6
find /sys/devices -name "uevent" -path "*i2c*" 2>/dev/null | head -4
find /sys/bus/i2c/devices/ -maxdepth 1 -name "0-*" -exec cat {}/name \;
touch -d "-1 min" /tmp/A10/stamp; find /var/log -maxdepth 1 -newer /tmp/A10/stamp -type f
```

```text
/lib/modules/7.0.0-34-generic/kernel/drivers/net/can/flexcan/flexcan.ko.zst
/lib/modules/7.0.0-34-generic/kernel/drivers/net/can/c_can/c_can_pci.ko.zst
/lib/modules/7.0.0-34-generic/kernel/drivers/net/can/c_can/c_can_platform.ko.zst
/lib/modules/7.0.0-34-generic/kernel/drivers/net/can/c_can/c_can.ko.zst
/lib/modules/7.0.0-34-generic/kernel/drivers/net/can/grcan.ko.zst
/lib/modules/7.0.0-34-generic/kernel/drivers/net/can/dummy_can.ko.zst
/sys/devices/i2c-0/uevent
/sys/devices/i2c-0/i2c-dev/i2c-0/uevent
mpu6050
bmp280
/var/log/kern.log
/var/log/syslog
/var/log/auth.log
```

출력에서 볼 것: 모듈 파일은 `.ko.zst`(zstd 압축)다. i.MX의 CAN 드라이버인 `flexcan.ko`가 보인다 — "이 커널에 내 컨트롤러 드라이버가 빌드돼 있나?"를 이렇게 확인한다. `-newer stamp`는 "방금 재현한 동안 어떤 로그 파일이 바뀌었나"를 찾는 데 좋다. (`0-*` 장치 두 개는 §5에서 만든 것이다.)

### 2.2 grep — 걸러내기

| 옵션 | 뜻 |
|---|---|
| `-i` / `-v` / `-c` / `-q` | 대소문자 무시 / 반전 / 개수 / 조용히 exit code만 |
| `-E` | 확장 정규식: OR(파이프 기호), `+`, `?`, `{n}` — 표 밖 예제 참고 |
| `-r` / `-l` / `-n` / `-H` | 재귀 / 파일 이름만 / 줄 번호 / 파일 이름 붙이기 |
| `-A N` / `-B N` / `-C N` | 매치 뒤·앞·양쪽 N줄 |
| `-o` / `-w` / `-F` | 매치 부분만 / 단어 단위 / 정규식 아님(고정 문자열) |

임베디드에서 가장 자주 하는 grep 세 가지: 커널 config 확인, 로그에서 에러 찾기, sysfs 속성 한꺼번에 덤프.

```sh
grep -E "^CONFIG_(CAN_VCAN|I2C_STUB|GPIO_SIM|DEBUG_FS|IKCONFIG|DYNAMIC_DEBUG)=" /boot/config-$(uname -r)
ls /proc/config.gz 2>&1
grep -r "" /sys/class/net/eth0/{operstate,speed,address,carrier} 2>/dev/null
sudo dmesg | grep -iE "fail|error|timeout|-110|defer|reject" | tail -5
```

```text
CONFIG_CAN_VCAN=m
CONFIG_I2C_STUB=m
CONFIG_GPIO_SIM=m
CONFIG_DYNAMIC_DEBUG=y
CONFIG_DEBUG_FS=y
ls: cannot access '/proc/config.gz': No such file or directory
/sys/class/net/eth0/operstate:up
/sys/class/net/eth0/speed:-1
/sys/class/net/eth0/address:52:55:55:a5:ed:31
/sys/class/net/eth0/carrier:1
[    0.061960] GPT: Use GNU Parted to correct GPT errors.
[  504.525402] A10: imu probe failed -110
[  585.988617] inv-mpu6050-i2c 0-0068: probe with driver inv-mpu6050-i2c rejects match -19
[  586.106586] inv-mpu6050-i2c 0-0068: probe with driver inv-mpu6050-i2c rejects match -19
[  605.493612] a6sleep: module verification failed: signature and/or required key missing - tainting kernel
```

출력에서 볼 것: `IKCONFIG`가 출력에 없다 = 설정되지 않음. 그래서 `/proc/config.gz`도 없다. 데스크톱 배포판은 `/boot/config-*`를 주지만, 임베디드 보드는 보통 `/proc/config.gz`(CONFIG_IKCONFIG_PROC)에 의존하므로 BSP 빌드 때 켜 두는 게 좋다. `grep -r "" 파일들`은 "파일 이름:내용"을 한 번에 찍는 sysfs 덤프 요령이다. 에러 grep의 `-110`은 `-ETIMEDOUT`, `-19`는 `-ENODEV`다(이 줄들은 §5·§6 실험이 남긴 것이고, `a6sleep`은 다른 노트의 실습 모듈이다). 드라이버 로그에는 숫자 errno가 그대로 찍히므로 자주 보는 값(`-5` EIO, `-6` ENXIO, `-16` EBUSY, `-22` EINVAL, `-110` ETIMEDOUT, `-517` EPROBE_DEFER)은 외워 둔다.

### 2.3 xargs — 표준입력을 인자로 바꾸기

파이프는 데이터를 **stdin**으로 넘기는데, `modinfo`·`grep FILE`처럼 **인자**를 받는 명령이 많다. `xargs`가 그 다리다. 파일 이름에 공백이 있을 수 있으면 `find -print0`과 `xargs -0`을 짝지어 쓴다.

```sh
find /sys/class/net/*/ -maxdepth 1 -name mtu -print0 | xargs -0 grep -H ""
echo "vcan can_raw i2c_stub" | xargs -n1 modinfo -F description
ls /sys/class/net | xargs -I{} sh -c "echo {}: \$(cat /sys/class/net/{}/statistics/rx_packets) rx pkts"
```

```text
/sys/class/net/eth0/mtu:1500
/sys/class/net/lo/mtu:65536
virtual CAN interface
PF_CAN raw protocol
I2C stub driver
eth0: 90510 rx pkts
lo: 160172 rx pkts
```

출력에서 볼 것: `-n1`은 인자 하나씩 명령을 여러 번 실행, `-I{}`는 자리표시자 치환이다.

### 2.4 sed — 스트림 편집

| 형태 | 뜻 |
|---|---|
| `sed 's/A/B/'` / `s/A/B/g` | 줄마다 첫 A를 B로 / 전부 |
| `sed -n '/pat/p'` | 맞는 줄만 출력 (`-n`이 기본 출력을 끔) |
| `sed -n '/start/,+1p'` / `sed -n '10,20p'` | 범위 출력 |
| `sed -i.bak 's/.../.../' f` | 파일을 제자리 수정, 원본은 `.bak`로 |
| `sed -E` | 확장 정규식 |

```sh
sed "s/^imu_addr=.*/imu_addr=0x69/" sensors.conf
sudo dmesg | sed -n "/i2c_dev/,+1p"
```

```text
# generated on lima-elinux kernel 7.0.0-34-generic
imu_bus=/dev/i2c-0
imu_addr=0x69
[    0.065190] i2c_dev: i2c /dev entries driver
[    0.065270] device-mapper: core: CONFIG_IMA_DISABLE_HTABLE is disabled. Duplicate IMA measurements will not be recorded in the IMA log.
```

`sed -n '/i2c_dev/,+1p'`는 매치한 줄과 그 다음 한 줄을 찍는다. 이제 파일을 제자리에서 고치고 diff로 확인한다.

```sh
sed -i.bak "s/0x68/0x69/" sensors.conf; diff sensors.conf.bak sensors.conf
```

```text
3c3
< imu_addr=0x68
---
> imu_addr=0x69
```

출력에서 볼 것: 보드의 설정 파일을 손으로 고칠 때 `sed -i.bak`로 고치고 `diff`로 확인하는 습관이 사고를 막는다. BusyBox `sed`도 `-i`는 지원한다(§12).

### 2.5 awk — 열(column) 단위 계산기

awk는 줄을 공백으로 쪼개 `$1`, `$2`, …, 마지막 열 `$NF`로 준다. `조건 { 동작 }` 형태이고, `BEGIN`/`END` 블록이 있다. `/proc` 파일들은 열 정렬 텍스트라 awk와 궁합이 가장 좋다.

무엇을 확인하나: `/proc/interrupts`에서 CPU 열을 합산해 "가장 많이 뛴 IRQ"를 뽑고, `/proc/meminfo`에서 비율을 계산하는 것.

```sh
awk '/virtio0-input/ { s=0; for(i=2;i<=NF;i++) if ($i ~ /^[0-9]+$/) s+=$i; print $NF, "total=" s }' /proc/interrupts
awk 'NR>1 { s=0; for(i=2;i<=5;i++) s+=$i; printf "%10d %-5s %s\n", s, $1, $NF }' /proc/interrupts | sort -rn | head -6
awk '/MemTotal/{t=$2} /MemAvailable/{a=$2} END{printf "avail %.1f%% of %d MiB\n", 100*a/t, t/1024}' /proc/meminfo
```

```text
virtio0-input.0 total=17412
    782542 IPI1: interrupts
    693769 10:   arch_timer
     47422 29:   virtio2-req.0
     35006 IPI0: interrupts
     17411 33:   virtio0-input.0
      7909 34:   virtio0-output.0
avail 87.6% of 3895 MiB
```

출력에서 볼 것: `i=2..5`는 이 VM의 CPU가 4개라서다(보드마다 CPU 수가 다르니 숫자 열만 세는 편이 안전하다 — §9의 irqrate.sh가 그렇게 한다). `IPI1`(rescheduling)·`arch_timer`가 상위인 것은 정상이다. 실제 보드라면 여기서 "내 센서 IRQ 번호가 목록에 있는가, 카운트가 오르는가"를 본다.

### 2.6 cut·sort·uniq — 세기

`sort`, `uniq -c`, `sort -rn`을 차례로 파이프로 잇는 것은 "무엇이 몇 번?"을 뽑는 관용구다. `uniq`는 **인접한** 같은 줄만 합치므로 반드시 앞에 `sort`가 있어야 한다.

```sh
ps -eo stat= | cut -c1 | sort | uniq -c | sort -rn
sudo dmesg | sed -E "s/^\[[^]]*\] //" | cut -d: -f1 | sort | uniq -c | sort -rn | head -6
cut -d: -f1,7 /etc/passwd | head -3
```

```text
     86 I
     84 S
      4 R
      1 Z
    147 systemd[1]
     58 audit
     47 pci 0000
     26 ACPI
     21 CPU features
     13   node   0
root:/bin/bash
daemon:/usr/sbin/nologin
bin:/usr/sbin/nologin
```

출력에서 볼 것: 프로세스 상태 첫 글자 통계(`I` idle 커널 스레드, `S` sleep, `R` running, `Z` zombie 1개). 두 번째는 그림 2의 파이프라인 그대로이고 "부팅 로그에서 누가 제일 시끄러운가"다. 보드에서 같은 줄 반복으로 로그가 넘치는(예: I2C NACK 폭주) 드라이버를 찾을 때 그대로 쓴다.

### 2.7 watch, less — 대화형 도구

`watch -n 0.5 -d 'grep imu /proc/interrupts'`는 0.5초마다 다시 실행하고 바뀐 부분을 강조한다(`-d`). 터미널 화면을 다시 그리는 대화형 도구라서 출력 블록 대신 동작을 설명한다(이 형태는 대화형이라 출력 생략). 스크립트에서 쓸 만한 옵션은 `-g`(출력이 바뀌면 종료)다 — "핀이 바뀔 때까지 기다리기"가 된다. 아래는 tty가 필요해서 `script`로 감싸 실제로 돌린 결과다.

```sh
D=$(ls -d /sys/devices/platform/gpio-sim.1/gpiochip*); echo pull-down | sudo tee $D/sim_gpio3/pull >/dev/null
( sleep 1.5; echo pull-up | sudo tee $D/sim_gpio3/pull >/dev/null ) &
start=$(date +%s.%N)
TERM=xterm script -qc "watch -g -n 0.2 cat $D/sim_gpio3/value" /dev/null >/dev/null; rc=$?
echo "watch -g exited rc=$rc after $(echo "$(date +%s.%N)-$start" | bc | cut -c1-4)s"; cat $D/sim_gpio3/value
```

```text
watch -g exited rc=0 after 1.76s
1
```

`less`도 대화형이다(출력 생략). 로그 볼 때 쓰는 키만 외운다.

| 키 | 동작 |
|---|---|
| `/pat` · `n` · `N` | 앞으로 검색 · 다음 · 이전 |
| `?pat` | 뒤로 검색 |
| `G` · `g` | 끝 · 처음 |
| `F` | 파일 끝을 계속 따라감 (`tail -f`처럼), `Ctrl-C`로 멈춤 |
| `&pat` | 맞는 줄만 보이기 (필터) |
| `-S` (옵션) | 긴 줄 접지 않기 — `dmesg` 볼 때 편함 |

`journalctl`은 기본 pager가 `less`라서 위 키가 그대로 통한다.

---

## 3. 정보는 어디에 있나 — `/proc`, `/sys`, `/dev`, debugfs, 로그

명령은 수백 개지만 **정보의 출처는 다섯 군데**다. `lsmod`는 `/proc/modules`를, `lspci`는 `/sys/bus/pci`를, `dmesg`는 `/dev/kmsg`를 예쁘게 찍는 프로그램일 뿐이다. BusyBox만 있는 보드에서 도구가 없으면 이 파일들을 직접 `cat`하면 된다.

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="10" width="130" height="420" rx="8" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="75" y="190" font-size="14" text-anchor="middle" font-weight="bold">커널</text>
  <text x="75" y="214" font-size="12" text-anchor="middle">drivers</text>
  <text x="75" y="232" font-size="12" text-anchor="middle">bus · class</text>
  <text x="75" y="250" font-size="12" text-anchor="middle">printk ring</text>
  <rect x="175" y="10" width="495" height="74" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="187" y="32" font-size="13" font-weight="bold">/proc — procfs: 프로세스·커널 상태 (텍스트)</text>
  <text x="187" y="54" font-size="12">/proc/interrupts  /proc/iomem  /proc/devices  /proc/modules</text>
  <text x="187" y="72" font-size="12">/proc/PID/fd, status, stack, io   /proc/meminfo  /proc/vmstat</text>
  <rect x="175" y="96" width="495" height="74" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="187" y="118" font-size="13" font-weight="bold">/sys — sysfs: 장치 모델 트리, 1값 1파일</text>
  <text x="187" y="140" font-size="12">/sys/bus/i2c/devices/0-0076   /sys/bus/i2c/drivers/bmp280/</text>
  <text x="187" y="158" font-size="12">/sys/class/net/eth0/mtu   /sys/dev/char/89:0   (udevadm info)</text>
  <rect x="175" y="182" width="495" height="74" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="187" y="204" font-size="13" font-weight="bold">/dev — devtmpfs: 장치 노드 (open·read·ioctl)</text>
  <text x="187" y="226" font-size="12">/dev/i2c-0 (c 89,0)   /dev/gpiochip2 (c 254,2)   /dev/ttyS0</text>
  <text x="187" y="244" font-size="12">/dev/kmsg  /dev/mem    (CAN·Ethernet은 /dev 없음 → ip)</text>
  <rect x="175" y="268" width="495" height="74" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="187" y="290" font-size="13" font-weight="bold">/sys/kernel/debug — debugfs: 드라이버 내부 (ABI 아님)</text>
  <text x="187" y="312" font-size="12">gpio  devices_deferred  dynamic_debug/control  i2c/  clk/</text>
  <text x="187" y="330" font-size="12">root 전용. 없으면: mount -t debugfs none /sys/kernel/debug</text>
  <rect x="175" y="354" width="495" height="74" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="187" y="376" font-size="13" font-weight="bold">로그 — printk 링버퍼 · journald · syslog</text>
  <text x="187" y="398" font-size="12">dmesg ← /dev/kmsg      journalctl -k / -u / -t ← journald</text>
  <text x="187" y="416" font-size="12">/var/log/kern.log, /var/log/syslog ← rsyslog (있을 때)</text>
  <line x1="140" y1="47" x2="173" y2="47" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="140" y1="133" x2="173" y2="133" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="140" y1="219" x2="173" y2="219" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="140" y1="305" x2="173" y2="305" stroke="currentColor" marker-end="url(#ah3)"/>
  <line x1="140" y1="391" x2="173" y2="391" stroke="currentColor" marker-end="url(#ah3)"/>
</svg>
```
그림 3 — 정보가 사는 다섯 곳. procfs는 "상태 보고서", sysfs는 "장치 모델의 트리", /dev는 "말을 거는 문", debugfs는 "드라이버 개발자의 뒷문", 로그는 "시간 순 기록"이다.

| 위치 | 성격 | ABI 안정성 | 대표 용도 |
|---|---|---|---|
| `/proc` | 프로세스 정보 + 옛날부터 있던 커널 상태 | 대부분 안정 | IRQ 카운트, 메모리 맵, 프로세스별 fd |
| `/sys` | 장치 모델(`kobject`) 트리, 속성 하나당 파일 하나 | 안정(문서화된 것) | 장치·드라이버 바인딩, 속성 읽기·쓰기 |
| `/dev` | char/block 장치 노드 (major:minor) | 안정 | 실제 I/O: `open`/`read`/`ioctl` |
| `/sys/kernel/debug` | 드라이버가 마음대로 내놓는 내부 상태 | **보장 없음** | 개발·디버그 전용 (제품에서 의존 금지) |
| 로그 | printk 링버퍼, journald 저장소 | 텍스트 형식은 바뀔 수 있음 | 무슨 일이 언제 있었나 |

`/dev`와 `/sys`를 잇는 끈은 **major:minor 번호**다.

```sh
grep -E "i2c|gpiochip|ttyS|ptm" /proc/devices
ls -l /dev/i2c-0; readlink -f /sys/dev/char/89:0
ls -l /dev/gpiochip2; readlink -f /sys/dev/char/$(stat -c "%Hr:%Lr" /dev/gpiochip2)
```

```text
  4 ttyS
  5 /dev/ptmx
 89 i2c
128 ptm
254 gpiochip
crw-rw---- 1 root i2c 89, 0 Oct  5 22:47 /dev/i2c-0
/sys/devices/i2c-0/i2c-dev/i2c-0
crw------- 1 root root 254, 2 Oct  5 22:41 /dev/gpiochip2
/sys/devices/platform/gpio-sim.1/gpiochip2
```

출력에서 볼 것: `/proc/devices`의 major 89가 `i2c`(i2c-dev 드라이버)다. `/sys/dev/char/89:0`을 따라가면 "이 /dev 노드 뒤에 어떤 장치가 있나"가 나온다. `gpiochip`의 254처럼 동적으로 받은 major는 보드마다 다를 수 있다.

---

## 4. 질문 ① "어떤 하드웨어·드라이버가 여기 있나?"

### 4.1 lspci / lsusb / lsblk — 열거 가능한 버스

PCIe·USB는 스스로 ID를 알려 주는 **열거(enumerable) 버스**라서 스캔하면 다 나온다. `-k`는 "어떤 커널 드라이버가 붙었나"를 함께 보여 준다. 없으면 `sudo apt install pciutils usbutils`로 설치한다(보드에서는 Yocto/Buildroot 이미지에 넣는다).

```sh
lspci; lspci -k | head -12
lsusb; lsusb -t
lsblk
```

```text
00:00.0 Host bridge: Apple Inc. Device 1a05
00:01.0 Ethernet controller: Red Hat, Inc. Virtio 1.0 network device (rev 01)
00:05.0 Communication controller: Red Hat, Inc. Virtio 1.0 console (rev 01)
00:06.0 Mass storage controller: Red Hat, Inc. Virtio 1.0 block device (rev 01)
...
00:0c.0 USB controller: Apple Inc. Device 1a06
00:00.0 Host bridge: Apple Inc. Device 1a05
00:01.0 Ethernet controller: Red Hat, Inc. Virtio 1.0 network device (rev 01)
	Subsystem: Red Hat, Inc. Device 0041
	Kernel driver in use: virtio-pci
	Kernel modules: virtio_pci
...
Bus 001 Device 001: ID 1d6b:0002 Linux Foundation 2.0 root hub
Bus 001 Device 002: ID 05ac:8105 Apple, Inc. Virtual USB Keyboard
Bus 001 Device 003: ID 05ac:8106 Apple, Inc. Virtual USB Digitizer
Bus 002 Device 001: ID 1d6b:0003 Linux Foundation 3.0 root hub
/:  Bus 001.Port 001: Dev 001, Class=root_hub, Driver=xhci_hcd/8p, 480M
    |__ Port 001: Dev 002, If 0, Class=Human Interface Device, Driver=usbhid, 12M
    |__ Port 002: Dev 003, If 0, Class=Human Interface Device, Driver=usbhid, 12M
/:  Bus 002.Port 001: Dev 001, Class=root_hub, Driver=xhci_hcd/8p, 10000M
NAME    MAJ:MIN RM   SIZE RO TYPE MOUNTPOINTS
vda     253:0    0   100G  0 disk 
├─vda1  253:1    0    99G  0 part /
├─vda13 253:13   0   923M  0 part /boot
└─vda15 253:15   0    99M  0 part /boot/efi
vdb     253:16   0 273.5M  1 disk /mnt/lima-cidata
```

출력에서 볼 것 (`...`은 줄 생략): `Kernel driver in use`가 없으면 "장치는 보이는데 드라이버가 안 붙었다"는 뜻이다. `lsusb -t`의 `Driver=`와 링크 속도(`12M`, `480M`)는 USB 카메라가 USB2로 잘못 붙었는지 볼 때 쓴다. Don의 SSD 경험으로 보면 `lspci -vv`의 `LnkSta`(링크 폭·속도)가 NVMe 링크 트레이닝 문제를 볼 때 첫 단서다.

### 4.2 `/proc/iomem`, `/proc/ioports` — 물리 주소 맵

`/proc/iomem`은 "어떤 물리 주소 구간을 누가 `request_mem_region()`으로 잡았나"의 표다. Don에게 익숙한 SoC 메모리 맵 그 자체다. **root로 읽어야 주소가 보인다** (일반 사용자는 0으로 가려진다).

```sh
sudo cat /proc/iomem | head -20
head -3 /proc/iomem
sudo cat /proc/ioports | head -5
```

```text
10000000-1000ffff : GICD
10010000-1008ffff : GICR
20060000-20060fff : ARMH0061:00
  20060000-20060fff : ARMH0061:00 ARMH0061:00
40000000-4fffffff : PCI ECAM
50000000-6ffdffff : PCI Bus 0000:00
  50000000-50000fff : 0000:00:0c.0
    50000000-50000fff : xhci-hcd
  50001000-500013ff : 0000:00:0c.0
  50001400-5000147f : 0000:00:05.0
...
70000000-ffffdfff : System RAM
  bee60000-c1d4ffff : Kernel code
  c1d50000-c2c1ffff : reserved
00000000-00000000 : GICD
00000000-00000000 : GICR
00000000-00000000 : ARMH0061:00
00000000-0000ffff : PCI Bus 0000:00
```

출력에서 볼 것: GIC(인터럽트 컨트롤러) 분배기·재분배기, `ARMH0061`(ARM PL061 GPIO 컨트롤러) MMIO 블록이 0x20060000에 있다. 이 주소는 §7.3에서 `devmem`으로 직접 읽어 본다. ARM에는 x86식 I/O 포트가 없어서 `/proc/ioports`는 PCI 구간 하나뿐이다. 드라이버가 `devm_ioremap_resource()`에 실패해 `-EBUSY`가 나면 여기서 "누가 먼저 잡았나"를 본다.

### 4.3 `/sys/bus`, `/sys/class` — 장치 모델 트리

`/sys/bus/<버스>/devices/`에는 그 버스에 등록된 **장치**, `/sys/bus/<버스>/drivers/`에는 등록된 **드라이버**가 있다. 장치 디렉터리 안의 `driver` 심볼릭 링크가 "누가 이 장치를 잡았나"다. `/sys/class/<기능>/`은 같은 장치를 "무슨 기능인가"(net, gpio, input, hwmon…)로 다시 묶은 뷰다.

```sh
ls /sys/bus | tr "\n" " "; echo
ls -l /sys/bus/pci/devices/0000:00:01.0/driver
ls -l /sys/class/net
```

```text
acpi amba auxiliary cdx clockevents clocksource container cpu dax edac event_source faux fsl-mc genpd genpd_provider gpio hid host1x-context i2c iscsi_flashnode mdio_bus memory memory_tiering mipi-dsi mmc nd node nvmem nvmem-layout pci pci-epf pci_express platform pnp pwrseq rapidio regulator scmi_protocol scsi sdio serial serial-base serio soc spi usb virtio vme workqueue xen xen-backend 
lrwxrwxrwx 1 root root 0 Oct  5 22:38 /sys/bus/pci/devices/0000:00:01.0/driver -> ../../../bus/pci/drivers/virtio-pci
total 0
lrwxrwxrwx 1 root root 0 Oct  5 22:38 eth0 -> ../../devices/pci0000:00/0000:00:01.0/virtio0/net/eth0
lrwxrwxrwx 1 root root 0 Oct  5 22:38 lo -> ../../devices/virtual/net/lo
```

출력에서 볼 것: `eth0` 링크를 따라가면 `PCI 장치 00:01.0 → virtio0 → net/eth0`라는 **부모 체인**이 그대로 경로에 드러난다. 이 체인이 G 모듈(장치 모델)의 `struct device` 부모 관계다.

### 4.4 udevadm info — 장치 속성과 부모 체인 한 번에

`udevadm info`는 sysfs 경로 하나에 대해 udev가 아는 속성(`E:` 환경 변수)을 보여 주고, `-a`는 **부모 장치를 타고 올라가며** 모든 sysfs 속성을 udev 규칙 문법(`ATTRS{...}==`)으로 찍는다. udev 규칙(예: "IMU를 `/dev/imu0`로 고정 이름 붙이기")을 쓸 때 이 출력을 복사해서 쓴다.

```sh
udevadm info /sys/class/net/eth0 | head -20
udevadm info -a /dev/i2c-0 | head -30
```

```text
P: /devices/pci0000:00/0000:00:01.0/virtio0/net/eth0
M: eth0
...
E: SUBSYSTEM=net
...
E: ID_NET_DRIVER=virtio_net
E: ID_BUS=pci
E: ID_VENDOR_ID=0x1af4
E: ID_MODEL_ID=0x1041
...
E: ID_MODEL_FROM_DATABASE=Virtio 1.0 network device
...
  looking at device '/devices/i2c-0/i2c-dev/i2c-0':
    KERNEL=="i2c-0"
    SUBSYSTEM=="i2c-dev"
    DRIVER==""
    ATTR{name}=="SMBus stub driver"
    ATTR{power/async}=="disabled"
...
  looking at parent device '/devices/i2c-0':
    KERNELS=="i2c-0"
    SUBSYSTEMS=="i2c"
    DRIVERS==""
    ATTRS{delete_device}=="(not readable)"
    ATTRS{name}=="SMBus stub driver"
    ATTRS{new_device}=="(not readable)"
```

출력에서 볼 것 (`...`은 줄 생략): 자기 자신은 `KERNEL`/`ATTR`, 부모는 `KERNELS`/`ATTRS`(복수형)로 매칭한다. 규칙 하나에서는 **자신 + 부모 하나**의 속성만 섞어 쓸 수 있다는 것이 `-a` 출력 머리말에도 적혀 있다. 부모의 `new_device`/`delete_device`는 §5.3에서 쓴다.

---

## 5. 질문 ② "내 드라이버가 load 됐나? probe 됐나?"

"load"와 "probe"는 다른 사건이다. Don의 펌웨어 비유로 말하면 **load = 드라이버 코드가 메모리에 올라옴**(모듈 링크), **probe = 그 드라이버가 특정 장치 인스턴스를 맡아 초기화 성공**(장치별 `init()` 호출이 OK를 리턴)이다. 모듈이 load 됐는데 probe가 안 되는 경우가 실무에서 가장 흔하다.

### 5.1 lsmod, modinfo — 무엇이 올라와 있고, 무엇을 받는가

```sh
lsmod | head -8; echo "count: $(lsmod | tail -n +2 | wc -l)"
modinfo vcan | head -11
modinfo i2c-stub | grep -E "^(parm|description)"
```

```text
Module                  Size  Used by
nft_compat             24576  12
udp_diag               12288  0
tcp_diag               16384  0
inet_diag              24576  2 tcp_diag,udp_diag
nft_chain_nat          12288  3
overlay               221184  0
xt_tcpudp              20480  0
count: 80
filename:       /lib/modules/7.0.0-34-generic/kernel/drivers/net/can/vcan.ko.zst
alias:          rtnl-link-vcan
author:         Urs Thuermann <urs.thuermann@volkswagen.de>
license:        Dual BSD/GPL
description:    virtual CAN interface
srcversion:     7B7F53D698F4A4D6E43727D
depends:        can-dev
intree:         Y
name:           vcan
vermagic:       7.0.0-34-generic SMP preempt mod_unload modversions aarch64
sig_id:         PKCS#7
description:    I2C stub driver
parm:           chip_addr:Chip addresses (up to 10, between 0x03 and 0x77) (array of ushort)
parm:           functionality:Override functionality bitfield (ulong)
parm:           bank_reg:Bank register (array of byte)
parm:           bank_mask:Bank value mask (array of byte)
parm:           bank_start:First banked register (array of byte)
parm:           bank_end:Last banked register (array of byte)
```

출력에서 볼 것: `Used by` 숫자는 **참조 카운트**다(0이 아니면 `rmmod`가 거부된다 — §8.4). `vermagic`이 실행 중인 커널과 다르면 모듈이 로드되지 않는다(out-of-tree 드라이버를 다른 커널 헤더로 빌드했을 때의 단골 에러). `alias: rtnl-link-vcan`은 `ip link add ... type vcan`이 이 모듈을 **자동 로드**하게 해 주는 별명이고, `parm:`은 `modprobe 모듈 이름=값`으로 넘길 수 있는 파라미터다.

### 5.2 modprobe, rmmod — 의존성까지 함께

`insmod`는 파일 하나를 그대로 넣고, `modprobe`는 `modules.dep`을 보고 의존 모듈(`can-dev`)까지 순서대로 넣는다. 실무에서는 거의 항상 `modprobe`다.

```sh
sudo modprobe vcan; echo rc=$?; lsmod | grep -E "^(vcan|can)"
sudo rmmod vcan; echo rc=$?; lsmod | grep -c "^vcan"
modprobe -n -v bmp280-spi
modprobe -c | grep -c "^alias"
```

```text
rc=0
vcan                   12288  0
can_dev                65536  1 vcan
rc=0
0
insmod /lib/modules/7.0.0-34-generic/kernel/drivers/iio/pressure/bmp280-spi.ko.zst 
66743
```

출력에서 볼 것: `can_dev`의 `Used by`에 `vcan`이 찍혔다 — 의존성이 함께 올라온 것. `-n -v`(dry run)는 "실제로 무엇을 insmod할지"만 보여 준다. 보드에서 모듈이 자동 로드되지 않을 때 이 alias 테이블(`modules.alias`)에 장치의 modalias가 있는지가 첫 확인 포인트다.

### 5.3 진짜 드라이버 probe 시켜 보기 — i2c-stub + bmp280

i2c 장치는 열거가 안 되는 버스라서, 보통은 **Device Tree**가 "이 버스의 0x76에 bmp280이 있다"고 알려 준다(D 모듈). DT 없이 시험할 때는 sysfs의 `new_device`에 `이름 주소`를 써서 장치를 즉석에서 만든다. 먼저 가상 I2C 버스에 0x68, 0x76 두 칩을 만들고(§7.1에서 자세히), 0x76의 chip ID 레지스터(0xD0)에 BMP280의 ID 0x58을 미리 써 둔다.

```sh
sudo modprobe i2c-stub chip_addr=0x68,0x76      # 이때 dmesg: "i2c-stub: Virtual chip at 0x68", "... at 0x76"
sudo i2cset -y 0 0x76 0xd0 0x58
echo bmp280 0x76 | sudo tee /sys/bus/i2c/devices/i2c-0/new_device; sleep 1
sudo dmesg | tail -4
ls -l /sys/bus/i2c/devices/
ls -l /sys/bus/i2c/devices/0-0076/driver
cat /sys/bus/i2c/devices/0-0076/name /sys/bus/i2c/devices/0-0076/modalias
lsmod | grep -E "bmp280|mpu6050"
```

```text
bmp280 0x76
[  558.737371] bmp280 0-0076: supply vddd not found, using dummy regulator
[  558.737421] bmp280 0-0076: supply vdda not found, using dummy regulator
[  558.740302] bmp280 0-0076: 0x58 is a known chip id for bmp280
[  559.011698] virtio_net virtio0 eth0: entered promiscuous mode
total 0
lrwxrwxrwx 1 root root 0 Oct  5 22:47 0-0076 -> ../../../devices/i2c-0/0-0076
lrwxrwxrwx 1 root root 0 Oct  5 22:47 i2c-0 -> ../../../devices/i2c-0
lrwxrwxrwx 1 root root 0 Oct  5 22:47 /sys/bus/i2c/devices/0-0076/driver -> ../../../bus/i2c/drivers/bmp280
bmp280
i2c:bmp280
bmp280_i2c             12288  0
bmp280                 57344  1 bmp280_i2c
industrialio_triggered_buffer    12288  1 bmp280
industrialio          147456  3 industrialio_triggered_buffer,kfifo_buf,bmp280
```

출력에서 볼 것: 장치 `0-0076`(버스 0, 주소 0x76)이 생기자 커널이 `modalias` `i2c:bmp280`으로 모듈을 **자동 로드**했고, 드라이버가 chip ID를 읽어 0x58을 확인한 뒤 probe에 성공했다. 증거는 세 가지: dmesg의 드라이버 메시지, `driver` 심볼릭 링크, 그리고 서브시스템(IIO) 장치의 등장이다. `supply ... not found, using dummy regulator`는 DT에 전원 레귤레이터가 연결 안 됐다는 경고다 — 실제 보드에서 이 줄이 보이면 DT의 `vdd-supply` 누락을 의심한다. (`virtio_net ... promiscuous` 줄은 다른 실습의 tcpdump 때문이다.)

```sh
ls /sys/bus/iio/devices/; cat /sys/bus/iio/devices/iio:device0/name
ls /sys/bus/iio/devices/iio:device0/ | grep -E "^in_"
cat /sys/bus/iio/devices/iio:device0/in_temp_input
```

```text
iio:device0
bmp280
in_pressure_input
in_pressure_oversampling_ratio
in_pressure_raw
in_pressure_scale
in_temp_input
in_temp_oversampling_ratio
in_temp_raw
in_temp_scale
0.000000000
```

출력에서 볼 것: probe가 성공한 드라이버는 서브시스템(I 모듈의 IIO)에 장치를 등록하고, 유저는 `in_temp_input`을 `cat`하는 것만으로 센서를 읽는다. 값이 0인 것은 stub 칩의 보정 레지스터가 전부 0이기 때문이다.

### 5.4 probe 실패를 재현하고 읽기 — mpu6050

이번에는 0x68에 MPU6050을 붙인다. 이 드라이버는 WHO_AM_I 레지스터(0x75)가 0x68이길 기대하는데, stub 칩의 0x75는 0x00이다. 동시에 `udevadm monitor`로 커널이 내보내는 uevent도 본다.

```sh
sudo i2cget -y 0 0x68 0x75
(sudo timeout 3 udevadm monitor --kernel --subsystem-match=i2c > /tmp/A10/um.txt 2>&1 &); sleep 0.5
echo mpu6050 0x68 | sudo tee /sys/bus/i2c/devices/i2c-0/new_device; sleep 2
sudo dmesg | tail -5
ls -l /sys/bus/i2c/devices/0-0068/driver; echo rc=$?
grep -v "^$" /tmp/A10/um.txt | tail -3
```

```text
0x00
mpu6050 0x68
[  567.159209] i2c i2c-0: new_device: Instantiated device mpu6050 at 0x68
[  567.172302] inv-mpu6050-i2c 0-0068: mounting matrix not found: using identity...
[  567.172328] inv-mpu6050-i2c 0-0068: supply vdd not found, using dummy regulator
[  567.172353] inv-mpu6050-i2c 0-0068: supply vddio not found, using dummy regulator
[  567.290450] inv-mpu6050-i2c 0-0068: invalid whoami 0x00 expected 0x68 (MPU6050)
ls: cannot access '/sys/bus/i2c/devices/0-0068/driver': No such file or directory
rc=2
KERNEL[567.277239] add      /devices/i2c-0/0-0068 (i2c)
KERNEL[567.290275] change   /devices/i2c-0/0-0068 (i2c)
KERNEL[567.408964] change   /devices/i2c-0/0-0068 (i2c)
```

출력에서 볼 것: **장치는 생겼지만(`add` uevent, `0-0068` 디렉터리) 드라이버는 붙지 않았다**(`driver` 링크 없음). 드라이버가 남긴 마지막 말이 `invalid whoami 0x00 expected 0x68`이다. 실제 보드에서 같은 줄이 나오면 원인은 배선·주소(AD0 핀)·전원 시퀀스·다른 칩 변종 중 하나다. 그리고 **driver core 자체는 "probe failed"라고 말해 주지 않았다** — probe가 `-ENODEV`/`-ENXIO`를 리턴하면 "이 장치는 내 것이 아니다"로 해석해 조용히 넘어가기 때문이다. 그래서 다음 도구가 필요하다.

### 5.5 bind / unbind — 다시 probe 시키기

`/sys/bus/<버스>/drivers/<드라이버>/bind`에 장치 이름을 쓰면 그 드라이버로 probe를 다시 시도하고, `unbind`는 `remove()`를 부른다. 재부팅 없이 "고치고 다시 probe"를 반복하는 루프다 (Don의 펌웨어 개발에서 "리셋 없이 init 재실행"에 해당).

```sh
sudo i2cset -y 0 0x68 0x75 0x68                   # WHO_AM_I를 맞는 값으로 "고친다"
echo 0-0068 | sudo tee /sys/bus/i2c/drivers/inv-mpu6050-i2c/bind; sleep 1
sudo dmesg | tail -3
ls -l /sys/bus/i2c/devices/0-0068/driver
echo 0-0068 | sudo tee /sys/bus/i2c/drivers/inv-mpu6050-i2c/unbind; ls /sys/bus/i2c/devices/0-0068/driver 2>&1
```

```text
0-0068
[  574.800410] inv-mpu6050-i2c 0-0068: supply vdd not found, using dummy regulator
[  574.800448] inv-mpu6050-i2c 0-0068: supply vddio not found, using dummy regulator
[  575.219296] i2c i2c-0: Added multiplexed i2c bus 1
lrwxrwxrwx 1 root root 0 Oct  5 22:48 /sys/bus/i2c/devices/0-0068/driver -> ../../../bus/i2c/drivers/inv-mpu6050-i2c
0-0068
ls: cannot access '/sys/bus/i2c/devices/0-0068/driver': No such file or directory
```

출력에서 볼 것: 이번에는 probe가 성공해 `driver` 링크가 생겼고, MPU6050의 보조 I2C 버스(자력계용)가 `i2c-1`로 추가됐다. `unbind` 후 링크가 사라졌다.

### 5.6 dynamic debug — driver core의 속마음 보기

`pr_debug()`/`dev_dbg()`는 평소 꺼져 있다가 `CONFIG_DYNAMIC_DEBUG=y`이면 런타임에 켤 수 있다. driver core의 `drivers/base/dd.c`를 켜면 "probe를 시도했나, 무엇을 리턴했나"가 보인다. WHO_AM_I를 다시 0으로 되돌리고 bind해 본다.

```sh
echo "file drivers/base/dd.c +p" | sudo tee /sys/kernel/debug/dynamic_debug/control
sudo grep "drivers/base/dd.c" /sys/kernel/debug/dynamic_debug/control | head -3
sudo i2cset -y 0 0x68 0x75 0x00
echo 0-0068 | sudo tee /sys/bus/i2c/drivers/inv-mpu6050-i2c/bind; echo "rc=$?"
sudo dmesg | tail -6
echo "file drivers/base/dd.c -p" | sudo tee /sys/kernel/debug/dynamic_debug/control >/dev/null
```

```text
file drivers/base/dd.c +p
drivers/base/dd.c:333 [dd]deferred_probe_extend_timeout =p "Extended deferred probe timeout by %d secs\n"
drivers/base/dd.c:1123 [dd]__device_attach =p "scheduling asynchronous probe\n"
drivers/base/dd.c:1242 [dd]__driver_attach_async_helper =p "driver %s async attach completed: %d\n"
0-0068
/sys/bus/i2c/drivers/inv-mpu6050-i2c/bind: No such device (os error 19)
rc=1
[  585.988650] i2c 0-0068: bus: 'i2c': really_probe: probing driver inv-mpu6050-i2c with device
[  585.988680] inv-mpu6050-i2c 0-0068: mounting matrix not found: using identity...
[  585.988697] inv-mpu6050-i2c 0-0068: supply vdd not found, using dummy regulator
[  585.988753] inv-mpu6050-i2c 0-0068: supply vddio not found, using dummy regulator
[  586.106267] inv-mpu6050-i2c 0-0068: invalid whoami 0x00 expected 0x68 (MPU6050)
[  586.106586] inv-mpu6050-i2c 0-0068: probe with driver inv-mpu6050-i2c rejects match -19
```

출력에서 볼 것: `really_probe: probing driver ...`(probe 진입)와 `rejects match -19`(probe가 `-ENODEV` 리턴)가 디버그 출력으로 드러났다. `bind` 쓰기 자체도 errno 19로 실패했다 — 쓰기의 리턴값이 probe 결과를 그대로 돌려준다. (`(os error 19)` 형식은 Ubuntu 26.04의 `tee`가 uutils(Rust) 구현이라서 나오는 문구다.) 모듈 전체를 켜려면 `module 모듈이름 +p`, 부팅 초기에는 커널 커맨드라인 `dyndbg="file drivers/base/dd.c +p"`를 쓴다.

### 5.7 deferred probe — `-EPROBE_DEFER`로 미뤄진 장치

드라이버가 필요한 자원(클럭, 레귤레이터, GPIO, 다른 장치)이 아직 준비 안 됐으면 probe는 `-EPROBE_DEFER`(-517)를 리턴하고 나중에 다시 시도된다. 부팅이 끝났는데도 남아 있는 목록이 debugfs의 `devices_deferred`다.

```sh
mount | grep debugfs
sudo cat /sys/kernel/debug/devices_deferred; echo "(end)"
```

```text
debugfs on /sys/kernel/debug type debugfs (rw,nosuid,nodev,noexec,relatime)
(end)
```

출력에서 볼 것: 이 VM에서는 비어 있다(미뤄진 장치 없음). 실제 보드 bring-up에서는 여기 장치 이름과 **이유**(예: 기다리는 공급자)가 남아 있는 경우가 많고, 이것이 "왜 내 카메라가 안 뜨지?"의 단골 답이다(G 모듈). debugfs가 마운트 안 된 보드라면 `sudo mount -t debugfs none /sys/kernel/debug`.

### 5.8 udevadm monitor — 핫플러그 이벤트 실시간 보기

`udevadm monitor`는 커널의 uevent(`KERNEL`)와 udev가 규칙 처리 후 내보내는 이벤트(`UDEV`)를 둘 다 보여 준다. USB 장치 꽂기, 모듈 로드, 네트워크 인터페이스 생성 등이 다 보인다.

```sh
(sudo timeout 3 udevadm monitor --kernel --udev --subsystem-match=net > /tmp/mon.txt 2>&1 &); sleep 0.5
sudo ip link add dev vcan0 type vcan; sleep 1; sudo ip link del vcan0; sleep 2
cat /tmp/mon.txt
```

```text
monitor will print the received events for:
UDEV - the event which udev sends out after rule processing
KERNEL - the kernel uevent

KERNEL[226.904760] add      /devices/virtual/net/vcan0 (net)
UDEV  [226.921807] add      /devices/virtual/net/vcan0 (net)
KERNEL[227.927083] remove   /devices/virtual/net/vcan0 (net)
UDEV  [227.931158] remove   /devices/virtual/net/vcan0 (net)
```

출력에서 볼 것: 커널 이벤트 후 약 17 ms 뒤 udev 처리 완료 이벤트가 나왔다. `KERNEL`은 있는데 `UDEV`가 없거나 늦으면 udev 규칙이 문제다. `--property`를 붙이면 각 이벤트의 `MODALIAS` 등 환경 변수까지 찍힌다.

---

## 6. 질문 ⑤ "커널이 뭐라고 했나?" — dmesg와 journalctl

### 6.1 로그가 흐르는 길

드라이버의 `dev_err()`나 `pr_info()`는 모두 `printk()`로 모이고, `printk`는 메시지를 커널 안의 **링버퍼**에 넣는다. Don의 펌웨어에서 RAM에 두던 원형 로그 버퍼와 똑같은 구조다. 이 링버퍼를 읽는 소비자가 여럿이다.

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="10" y="20" width="210" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="47" font-size="13" text-anchor="middle">드라이버: dev_err() 등</text>
  <rect x="10" y="100" width="210" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="115" y="127" font-size="13" text-anchor="middle">printk() + 레벨 0~7</text>
  <rect x="10" y="180" width="210" height="44" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="115" y="207" font-size="13" text-anchor="middle">printk 링버퍼 (RAM)</text>
  <line x1="115" y1="64" x2="115" y2="98" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="115" y1="144" x2="115" y2="178" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="10" y="280" width="210" height="58" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="115" y="303" font-size="13" text-anchor="middle">유저 프로세스</text>
  <text x="115" y="323" font-size="12" text-anchor="middle">stdout · logger · syslog()</text>
  <line x1="60" y1="280" x2="60" y2="226" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
  <text x="66" y="258" font-size="12">/dev/kmsg에 쓰기</text>
  <rect x="270" y="20" width="400" height="44" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="282" y="47" font-size="13">콘솔(UART·hvc): 레벨 &lt; console_loglevel 만</text>
  <rect x="270" y="100" width="400" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="282" y="127" font-size="13">/dev/kmsg → dmesg (-T -w -W --level)</text>
  <rect x="270" y="180" width="400" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="282" y="207" font-size="13">/dev/kmsg → systemd-journald → journal 파일</text>
  <rect x="270" y="260" width="400" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="282" y="287" font-size="13">journalctl -k / -b / -u / -t / -p / -f</text>
  <rect x="270" y="324" width="400" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888" stroke-dasharray="5 4"/>
  <text x="282" y="351" font-size="13">rsyslog (있으면) → /var/log/kern.log, syslog</text>
  <line x1="220" y1="202" x2="268" y2="42" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="220" y1="202" x2="268" y2="122" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="220" y1="202" x2="268" y2="202" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="220" y1="214" x2="268" y2="346" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#ah4)"/>
  <line x1="470" y1="224" x2="470" y2="258" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="220" y1="309" x2="268" y2="216" stroke="currentColor" marker-end="url(#ah4)"/>
</svg>
```
그림 4 — printk에서 도구까지의 로그 경로. 링버퍼는 크기가 고정이라 오래된 메시지는 덮어쓰인다. 재부팅 후에도 남는 것은 journald가 디스크에 저장했을 때(persistent journal)와 rsyslog 파일뿐이다.

### 6.2 dmesg — 링버퍼 직접 읽기

| 옵션 | 뜻 |
|---|---|
| `-T` | 부팅 후 초 대신 사람이 읽는 시각 (suspend가 있으면 어긋날 수 있음) |
| `-w` / `-W` | 전체 출력 후 계속 따라가기 / **새 메시지만** 따라가기 |
| `--level=err,warn` | 레벨 필터 (`emerg alert crit err warn notice info debug`) |
| `-x` / `-k` | 레벨·facility 표시 / 커널 메시지만 |
| `-c` | 읽고 나서 버퍼 비우기 (재현 전에 깨끗하게) |

```sh
sudo dmesg | tail -5
sudo dmesg -T | tail -3
sudo dmesg --level=err,warn | tail -6
cat /proc/sys/kernel/dmesg_restrict; dmesg 2>&1 | head -1
```

```text
[  192.950703] i2c-stub: Virtual chip at 0x68
[  205.344396] lo: entered promiscuous mode
[  208.345123] lo: left promiscuous mode
[  226.006558] sh (32477): drop_caches: 3
[  226.786124] vcan: Virtual CAN interface driver
[Mon Oct  5 22:41:55 2026] lo: left promiscuous mode
[Mon Oct  5 22:42:13 2026] sh (32477): drop_caches: 3
[Mon Oct  5 22:42:14 2026] vcan: Virtual CAN interface driver
[    0.061960] GPT: Use GNU Parted to correct GPT errors.
[    5.443704] kauditd_printk_skb: 175 callbacks suppressed
[    6.410741] coredump: 1932(sysctl): Unsafe core_pattern used with fs.suid_dumpable=2: pipe handler or fully qualified core dump path required. Set kernel.core_pattern before fs.suid_dumpable.
[   23.076269] overlayfs: fs on '/tmp/tmp.vzb7Oy7QFp/l' does not support file handles, falling back to xino=off.
[   29.648337] overlayfs: fs on '/home/donh.guest/.local/share/containerd-stargz-grpc/snapshotter/multiple-lowerdir-check3216485036/lower2' does not support file handles, falling back to xino=off.
[   48.836954] kauditd_printk_skb: 170 callbacks suppressed
1
dmesg: read kernel buffer failed: Operation not permitted
```

출력에서 볼 것: `dmesg_restrict=1`이라 일반 사용자는 읽을 수 없다(보안 기본값) — 그래서 `sudo`가 붙는다. 기본 출력의 `[  226.786124]`는 부팅 후 초(monotonic)이고, 드라이버 타이밍(예: probe에서 첫 IRQ까지)을 잴 때는 `-T`보다 이쪽이 정확하다.

`-W`로 새 메시지만 따라가면서 i2c-stub를 다시 로드해 본다.

```sh
(sudo timeout 2 dmesg -W > /tmp/A10/dw.txt 2>&1 &); sleep 0.5
sudo modprobe i2c-stub chip_addr=0x68,0x76; sleep 1.7; cat /tmp/A10/dw.txt
```

```text
[  513.734436] i2c-stub: Virtual chip at 0x68
[  513.734492] i2c-stub: Virtual chip at 0x76
```

### 6.3 printk 레벨과 콘솔

```sh
cat /proc/sys/kernel/printk
```

```text
4	4	1	7
```

출력에서 볼 것: 네 값은 순서대로 `console_loglevel`, 기본 메시지 레벨, 최소 콘솔 레벨, 부팅 시 기본 콘솔 레벨이다. 첫 값 4는 "레벨 숫자가 **4보다 작은**(0 emerg ~ 3 err) 메시지만 콘솔(UART)에 찍는다"는 뜻이다. 그래서 `dev_info()`는 dmesg에는 있어도 시리얼 콘솔에는 안 보인다. bring-up 중 콘솔에 다 보고 싶으면 `echo 8 | sudo tee /proc/sys/kernel/printk` 또는 커널 커맨드라인 `loglevel=8`. 반대로 UART 출력은 느려서(115200bps에서 한 줄 수 ms) 콘솔 로그가 많으면 타이밍이 바뀐다 — 펌웨어에서 printf 디버깅이 타이밍 버그를 숨기던 것과 같은 함정이다.

### 6.4 journalctl — 저장된 로그를 질의하기

systemd 시스템에서는 journald가 `/dev/kmsg`와 서비스 출력을 모두 받아 색인된 저장소에 둔다. `journalctl`은 그 질의 도구다.

| 옵션 | 뜻 |
|---|---|
| `-k` | 커널 메시지만 (dmesg와 같은 내용) |
| `-b` / `-b -1` / `--list-boots` | 이번 부팅 / 이전 부팅 / 부팅 목록 |
| `-u UNIT` / `-t TAG` | systemd 유닛별 / syslog 식별자별 |
| `-p err` | 우선순위 err 이상만 |
| `-f` / `-n N` | 따라가기 / 마지막 N줄 |
| `--since "2 min ago"` | 시간 범위 |
| `-o short-monotonic` | dmesg처럼 부팅 후 초로 표시 |

```sh
sudo journalctl -k -n 4 --no-pager
sudo journalctl --list-boots --no-pager | tail -3
sudo journalctl -u systemd-udevd -n 4 --no-pager
sudo journalctl -b -p err -n 4 --no-pager
sudo journalctl -k -o short-monotonic -n 2 --no-pager
```

```text
Oct 05 22:42:13 lima-elinux kernel: sh (32477): drop_caches: 3
Oct 05 22:42:14 lima-elinux kernel: vcan: Virtual CAN interface driver
Oct 05 22:42:21 lima-elinux kernel: sh (32777): drop_caches: 3
Oct 05 22:42:21 lima-elinux kernel: sh (32785): drop_caches: 3
IDX BOOT ID                          FIRST ENTRY                 LAST ENTRY
  0 75f62b08bce740c8b7c8856ae4b6bb86 Mon 2026-10-05 22:38:27 PDT Mon 2026-10-05 22:42:24 PDT
Oct 05 22:38:29 ubuntu systemd[1]: Starting systemd-udevd.service - Rule-based Manager for Device Events and Files...
Oct 05 22:38:29 ubuntu systemd-udevd[678]: Using default interface naming scheme 'v259'.
Oct 05 22:38:29 ubuntu systemd[1]: Started systemd-udevd.service - Rule-based Manager for Device Events and Files.
Oct 05 22:38:31 lima-elinux (udev-worker)[1038]: eth0: Could not set WakeOnLan to off, ignoring: Operation not supported
Oct 05 22:38:27 localhost kernel: PCI: OF: of_root node is NULL, cannot create PCI host bridge node
[  233.946283] lima-elinux kernel: sh (32777): drop_caches: 3
[  233.986017] lima-elinux kernel: sh (32785): drop_caches: 3
```

출력에서 볼 것: 부팅 목록에 0번(이번 부팅) 하나뿐이다. 이 VM은 이번 실습 직전에 재부팅됐다. 이전 부팅의 로그가 필요하면(예: "어젯밤 현장에서 드론이 리셋됐다") **persistent journal**(`/var/log/journal/` 디렉터리 존재, 또는 `journald.conf`의 `Storage=persistent`)이어야 `journalctl -b -1`이 동작한다. eMMC 수명 때문에 임베디드 제품은 일부러 휘발성(`/run/log/journal`)으로 두는 경우가 많으니 정책을 확인한다.

### 6.5 유저 공간에서 로그 남기기 — logger, /dev/kmsg, -f

```sh
logger -t sensord -p user.warning "imu: FIFO overflow, dropped 12 samples"; sleep 0.5; sudo journalctl -t sensord -n 1 --no-pager
echo "<4>A10: hello from userspace via /dev/kmsg" | sudo tee /dev/kmsg >/dev/null; sudo dmesg | tail -1; sudo journalctl -k -n 1 --no-pager
(sudo timeout 2 journalctl -k -f -n 0 > /tmp/A10/jf.txt 2>&1 &); sleep 0.7
echo "<3>A10: imu probe failed -110" | sudo tee /dev/kmsg >/dev/null; sleep 1.5; cat /tmp/A10/jf.txt
```

```text
Oct 05 22:42:24 lima-elinux sensord[32894]: imu: FIFO overflow, dropped 12 samples
[  237.319516] A10: hello from userspace via /dev/kmsg
Oct 05 22:42:24 lima-elinux A10: hello from userspace via /dev/kmsg
Oct 05 22:46:52 lima-elinux A10: imu probe failed -110
```

출력에서 볼 것: `/dev/kmsg`에 쓰면 커널 링버퍼에 들어가 dmesg에 보인다. `<4>`는 레벨(warning)이다. 테스트 스크립트가 "지금부터 재현 시작"이라는 마커를 커널 로그 사이에 끼워 넣을 때 아주 유용하다. journald는 이것이 유저 공간에서 쓴 것임을 알기 때문에 `kernel:` 대신 `A10:` 태그로 저장한다. `journalctl -f`는 `tail -f`처럼 새 줄을 기다린다.

---

## 7. 버스·핀·레지스터를 셸에서 직접 만지기

드라이버가 없거나 의심스러울 때, Don이 펌웨어에서 디버거로 레지스터를 찔러 보던 일을 셸에서 한다. **세 도구 모두 드라이버를 우회하므로 위험하다** — 제품 이미지에서는 빼는 것이 보통이다.

### 7.1 i2c-tools — i2cdetect, i2cget, i2cset, i2cdump

`i2c-tools`는 `/dev/i2c-N`(i2c-dev 드라이버)을 열고 `ioctl`로 SMBus 트랜잭션을 날린다. 이 VM에는 I2C 컨트롤러가 없어서 `i2c-stub`로 가상 어댑터를 만든다.

```sh
i2cdetect -l; echo rc=$?                          # 로드 전: 버스 없음
sudo modprobe i2c-stub chip_addr=0x68 && echo loaded
sudo i2cdetect -l
sudo i2cdetect -y 0
```

```text
rc=0
loaded
i2c-0	smbus     	SMBus stub driver               	SMBus adapter
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- -- 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- 68 -- -- -- -- -- -- -- 
70: -- -- -- -- -- -- -- --                         
```

출력에서 볼 것: 응답한 주소(0x68)는 숫자로, 응답 없는 주소는 `--`로 표시된다. `-y`는 "정말 할 거냐" 확인을 건너뛴다.

| 명령 | 형식 | 하는 일 |
|---|---|---|
| `i2cdetect -l` | | 시스템의 I2C 어댑터(버스) 목록 |
| `i2cdetect -y BUS` | `[-r] [-q] [first last]` | 0x03~0x77 주소마다 짧은 트랜잭션을 보내 ACK 여부 표시 |
| `i2cdetect -F BUS` | | 어댑터가 지원하는 트랜잭션 종류 |
| `i2cget -y BUS ADDR REG [b/w]` | | 레지스터 하나 읽기 (byte/word) |
| `i2cset -y BUS ADDR REG VAL [b/w]` | | 레지스터 하나 쓰기 |
| `i2cdump -y [-r a-b] BUS ADDR [b/w/i]` | | 레지스터 범위 덤프 |
| `i2ctransfer -y BUS w1@0x68 0x75 r1` | | 반복 시작(repeated start) 묶음 전송 (SMBus 아닌 raw I2C) |

```sh
sudo i2cset -y 0 0x68 0x75 0x71; echo rc=$?
sudo i2cget -y 0 0x68 0x75
sudo i2cdump -y -r 0x70-0x7f 0 0x68 b
sudo i2cget -y 0 0x50 0x00; echo rc=$?
sudo i2cdetect -F 0 | head -9
ls -l /dev/i2c-*
```

```text
rc=0
0x71
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f    0123456789abcdef
70: 00 00 00 00 00 71 00 00 00 00 00 00 00 00 00 00    .....q..........
Error: Read failed
rc=2
Functionalities implemented by /dev/i2c-0:
I2C                              no
SMBus Quick Command              yes
SMBus Send Byte                  yes
SMBus Receive Byte               yes
SMBus Write Byte                 yes
SMBus Read Byte                  yes
SMBus Write Word                 yes
SMBus Read Word                  yes
crw-rw---- 1 root i2c 89, 0 Oct  5 22:41 /dev/i2c-0
```

출력에서 볼 것: 0x75(MPU-9250 계열의 WHO_AM_I 위치)에 0x71을 쓰고 다시 읽었다. 장치가 없는 0x50 읽기는 실패(`rc=2`)다. `I2C no`는 이 stub 어댑터가 raw I2C 전송을 못 한다는 뜻이라 `i2ctransfer`는 여기서 쓸 수 없다. `/dev/i2c-0`은 `i2c` 그룹 소유라서 사용자를 그 그룹에 넣으면 sudo 없이 쓸 수 있다.

드라이버가 이미 잡은 주소는 `UU`로 나온다. bmp280을 0x76에 바인딩한 상태(§5.3)에서:

```sh
sudo i2cdetect -y 0
sudo strace -e trace=ioctl i2cdetect -y -r 0 0x50 0x50 2>&1 >/dev/null | grep -v TCGETS
```

```text
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:                         -- -- -- -- -- -- -- -- 
10: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
20: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
30: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
40: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
50: -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- 
60: -- -- -- -- -- -- -- -- 68 -- -- -- -- -- -- -- 
70: -- -- -- -- -- -- UU --                         
ioctl(3, _IOC(_IOC_NONE, 0x7, 0x5, 0), 0xffffe0bebe68) = 0
ioctl(3, _IOC(_IOC_NONE, 0x7, 0x3, 0), 0x50) = 0
ioctl(3, _IOC(_IOC_NONE, 0x7, 0x20, 0), 0xffffe0bebdb8) = -1 ENODEV (No such device)
+++ exited with 0 +++
```

출력에서 볼 것: `ioctl` 번호 0x0705 = `I2C_FUNCS`, 0x0703 = `I2C_SLAVE`(대상 주소 설정), 0x0720 = `I2C_SMBUS`(실제 전송)다. 응답 없는 주소에서 stub 어댑터는 `ENODEV`를 돌려줬다. 실제 하드웨어 컨트롤러는 주소 NACK를 보통 `ENXIO`로 돌려준다(커널 문서 `i2c/fault-codes`). 0x76에 `I2C_SLAVE`를 하면 `EBUSY`가 나는데, 드라이버가 잡은 주소라서이고 i2cdetect는 그걸 `UU`로 표시한다.

**위험 — 실제 보드에서 반드시 기억할 것**

- `i2cdetect`는 "읽기만" 하는 도구가 아니다. 기본 모드는 대부분의 주소에 **SMBus quick write**를, 0x30~0x37·0x50~0x5F에는 read byte를 보낸다. man 페이지 경고대로 quick write에 반응해 상태가 바뀌는 칩(일부 EEPROM 쓰기 보호 해제 등)이 있고, 어떤 칩은 예상 밖의 트랜잭션에 버스를 잡고(SDA low) 멈춘다. 모르는 버스는 `-r`(read만)로 범위를 좁혀서 스캔한다.
- `i2cset`은 드라이버 몰래 레지스터를 바꾼다. **PMIC 버스**(전압 레귤레이터)에 잘못 쓰면 코어 전압이 바뀌어 보드가 즉시 죽거나 손상될 수 있다.
- `i2cdump`/`i2cget`도 부작용이 있다: **read-to-clear** 상태 레지스터, 읽으면 하나씩 빠지는 FIFO 레지스터는 읽는 순간 드라이버가 볼 데이터를 빼앗는다.
- 드라이버가 바인딩된 주소(`UU`)에 `-f`(force)로 접근하면 드라이버와 경쟁한다. 디버그가 끝나면 드라이버를 unbind하고 쓰는 편이 안전하다.

### 7.2 libgpiod — gpiodetect, gpioinfo, gpioget, gpioset

GPIO 유저 공간 인터페이스는 `/dev/gpiochipN` 문자 장치(+`ioctl`)다. 옛 `/sys/class/gpio/export` 방식은 deprecated다. 도구는 `gpiod` 패키지(libgpiod)이고, **v1과 v2의 명령 문법이 다르다**는 것이 가장 큰 함정이다. 이 VM은 v2다.

`gpio-sim`으로 8핀짜리 가상 칩을 만들고 3번에 `imu_int`, 5번에 `cam_reset`이라는 이름을 붙였다 (configfs: `/sys/kernel/config/gpio-sim/sensorkit/bank0/...`에 `num_lines`, `line3/name` 등을 쓰고 `live`에 1).

```sh
gpioinfo --version | head -1
sudo gpiodetect
sudo gpioinfo imu_int cam_reset
sudo gpioget imu_int
```

```text
gpioinfo (libgpiod) v2.2.1
gpiochip0 [ARMH0061:00] (8 lines)
gpiochip1 [gpio-sim.0:node0] (8 lines)
gpiochip2 [gpio-sim.1:node0] (8 lines)
gpiochip2 3	"imu_int"       	input
gpiochip2 5	"cam_reset"     	input
"imu_int"=inactive
```

출력에서 볼 것: `gpiochip0`은 VM의 실제(가상화된) PL061 컨트롤러, `gpiochip1`은 다른 실습이 만든 sim 칩, `gpiochip2`가 이번 칩이다. **칩 번호는 probe 순서에 따라 바뀐다** — 그래서 스크립트에서는 칩 번호 대신 DT의 `gpio-line-names`로 붙인 **줄 이름**으로 찾는 것이 안전하다.

sim 칩은 sysfs로 외부 풀업을 흉내 낼 수 있다. 핀을 "외부에서" high로 당기고 다시 읽는다.

```sh
D=/sys/devices/platform/gpio-sim.1/gpiochip2
echo pull-up | sudo tee $D/sim_gpio3/pull >/dev/null
sudo gpioget imu_int; sudo gpioget --numeric imu_int; sudo gpioget -c gpiochip2 3
sudo gpioget gpiochip2 3; echo rc=$?
```

```text
"imu_int"=active
1
"3"=active
gpioget: cannot find line 'gpiochip2'
gpioget: cannot find line '3'
rc=1
```

출력에서 볼 것: v2에서는 칩을 `-c`로 지정한다. 인터넷 예제에 흔한 v1 문법 `gpioget gpiochip2 3`은 v2에서 "gpiochip2라는 이름의 줄"을 찾다가 실패한다.

```sh
sudo gpioset -t0 cam_reset=1; echo rc=$?; cat $D/sim_gpio5/value
(sudo timeout 2 gpioset cam_reset=1 &) ; sleep 0.5; sudo gpioinfo cam_reset; cat $D/sim_gpio5/value
```

```text
rc=0
0
gpiochip2 5	"cam_reset"     	output consumer="gpioset"
1
```

출력에서 볼 것: v2 `gpioset`은 기본적으로 **줄을 잡고 계속 실행**된다(값 유지). `-t0`으로 바로 끝내면 줄이 해제되면서 값이 유지된다는 보장이 없고, 여기서는 0으로 돌아갔다. 줄을 잡고 있는 동안에는 `consumer="gpioset"`으로 보인다. 즉 "리셋 핀을 high로 유지"하려면 `gpioset`을 백그라운드로 살려 두거나(`-z`/`--daemonize`), 결국은 드라이버나 DT(`gpio-hog`)로 해야 한다. 비교 표:

| 하고 싶은 것 | libgpiod v1 | libgpiod v2 |
|---|---|---|
| 칩·오프셋으로 읽기 | `gpioget gpiochip2 3` | `gpioget -c gpiochip2 3` |
| 이름으로 읽기 | `gpiofind imu_int` 결과를 넘김 | `gpioget imu_int` |
| 쓰고 바로 종료 | `gpioset gpiochip2 5=1` (기본이 exit) | `gpioset -t0 cam_reset=1` |
| 쓰고 유지 | `gpioset --mode=wait ...` | `gpioset cam_reset=1` (기본이 유지) |
| 엣지 기다리기 | `gpiomon gpiochip2 3` | `gpiomon -c gpiochip2 3` 또는 `gpiomon imu_int` |

debugfs는 칩별 상태를 한꺼번에 보여 준다(드라이버가 잡은 줄의 consumer 이름 포함).

```sh
sudo cat /sys/kernel/debug/gpio | head -14
```

```text
gpiochip0: 8 GPIOs, parent: amba/ARMH0061:00, ARMH0061:00:
 gpio-6   (                    |ACPI:Event          ) in  lo IRQ 

gpiochip1: 8 GPIOs, parent: platform/gpio-sim.0, gpio-sim.0:node0, can sleep:
 gpio-0   (<unused>) input,pull-down
 gpio-1   (<unused>) input,pull-down
 gpio-2   (<unused>) input,pull-down
 gpio-3   (sysfs) input,pull-down
 gpio-4   (<unused>) input,pull-down
 gpio-5   (<unused>) input,pull-down
 gpio-6   (<unused>) input,pull-down
 gpio-7   (<unused>) input,pull-down

gpiochip2: 8 GPIOs, parent: platform/gpio-sim.1, gpio-sim.1:node0, can sleep:
```

### 7.3 devmem / devmem2 — 물리 주소 직접 읽기·쓰기

`/dev/mem`을 `mmap`해서 물리 주소의 레지스터를 읽고 쓰는 도구다. JTAG 메모리 창을 셸에서 여는 것과 같다. `busybox devmem ADDRESS [WIDTH [VALUE]]`와 별도 패키지 `devmem2 ADDRESS [b/h/w] [VALUE]` 두 가지가 흔하다. §4.2의 `/proc/iomem`에서 본 PL061 GPIO 블록(0x20060000)의 PrimeCell ID 레지스터(오프셋 0xFE0~0xFEC)를 읽어 본다.

```sh
sudo devmem2 0x20060FE0 w
for o in FE0 FE4 FE8 FEC; do sudo busybox devmem 0x20060$o 32; done
sudo busybox devmem 0x80000000 32; echo rc=$?
grep -E "^CONFIG_(STRICT_DEVMEM|IO_STRICT_DEVMEM|DEVMEM)" /boot/config-$(uname -r)
```

```text
/dev/mem opened.
Memory mapped at address 0xe8e38ab98000.
Value at address 0x20060FE0 (0xe8e38ab98fe0): 0x61
0x00000061
0x00000010
0x00000004
0x00000000
devmem: mmap: Operation not permitted
rc=1
CONFIG_DEVMEM=y
CONFIG_STRICT_DEVMEM=y
```

출력에서 볼 것: PeriphID0~3 = 0x61, 0x10, 0x04, 0x00 → 부품 번호 0x061(PL061), 설계사 코드 0x41(ARM). 레지스터 맵에 있는 ID를 읽어 "주소 맵이 맞다"를 확인하는 것은 bring-up의 첫 MMIO 확인 방법이다. 반면 System RAM(0x80000000)은 `STRICT_DEVMEM` 때문에 거부됐다. `IO_STRICT_DEVMEM`까지 켜진 커널에서는 **드라이버가 잡은 MMIO 구간도** 막힌다(이 VM은 꺼져 있어서 PL061 읽기가 됐다).

위험: 쓰기(`devmem ADDR 32 VALUE`)는 드라이버 몰래 하드웨어 상태를 바꾼다. 클럭이 꺼진(clock-gated) 블록이나 전원이 내려간 도메인을 읽으면 **버스 에러로 커널이 즉시 멈출 수 있다**(SError/외부 abort). 드라이버가 runtime PM으로 블록을 꺼 두었을 수 있으니, 읽기 전에 그 장치가 켜져 있는지(`/sys/.../power/runtime_status`) 본다.

---

## 8. 질문 ③ "누가 이 장치를 쓰고 있나?"

### 8.1 실험 대상: 가짜 센서 데몬

`/dev/i2c-0`을 fd 3으로 열어 두고 0.5초마다 IMU 레지스터를 읽는 스크립트(`~/.elinux_scratch/A10/sensord.sh`)를 백그라운드로 띄운다.

```sh
#!/usr/bin/env bash
# 가짜 센서 데몬: /dev/i2c-0을 열어 둔 채 0.5초마다 IMU 레지스터를 읽는다
exec 3<>/dev/i2c-0
while true; do
  i2cget -y 0 0x68 0x75 >/dev/null
  sleep 0.5
done
```

```sh
sudo setsid /Users/donh/.elinux_scratch/A10/sensord.sh >/dev/null 2>&1 < /dev/null &
sleep 1
P=$(pgrep -f "^bash /Users/donh/.elinux_scratch/A10/sensord.sh"); echo "pid=$P"
pgrep -af sensord.sh | grep -v pgrep
```

```text
pid=99840
99782 sudo setsid /Users/donh/.elinux_scratch/A10/sensord.sh
99840 bash /Users/donh/.elinux_scratch/A10/sensord.sh
```

함정: 처음에 `pgrep -x sensord.sh`로 찾았더니 아무것도 안 나왔다. `#!/usr/bin/env bash` 스크립트는 프로세스 이름(comm)이 `bash`이기 때문이다. `pgrep -f`는 전체 커맨드라인을 본다. 단, `-f`는 그 문자열을 포함한 **자기 자신의 셸**도 잡을 수 있으니 패턴을 `^`로 고정한다.

### 8.2 fuser, lsof — 파일·장치 기준으로 프로세스 찾기

```sh
sudo fuser -v /dev/i2c-0
sudo lsof /dev/i2c-0
sudo lsof -nP -i :22 -sTCP:LISTEN
```

```text
                     USER        PID ACCESS COMMAND
/dev/i2c-0:          root      99840 F.... bash
                     root      113003 F.... sleep
COMMAND    PID USER FD   TYPE DEVICE SIZE/OFF NODE NAME
bash     99840 root 3u   CHR   89,0      0t0  751 /dev/i2c-0
sleep   113003 root 3u   CHR   89,0      0t0  751 /dev/i2c-0
COMMAND  PID USER  FD   TYPE DEVICE SIZE/OFF NODE NAME
systemd    1 root 136u  IPv4  17897      0t0  TCP *:22 (LISTEN)
systemd    1 root 137u  IPv6  17045      0t0  TCP *:22 (LISTEN)
sshd    1255 root   3u  IPv4  17897      0t0  TCP *:22 (LISTEN)
sshd    1255 root   4u  IPv6  17045      0t0  TCP *:22 (LISTEN)
```

출력에서 볼 것: `sleep`도 `/dev/i2c-0`을 들고 있다 — fork/exec된 자식이 fd 3을 **상속**했기 때문이다(A2·A3의 fd 상속, `O_CLOEXEC`를 안 쓰면 생기는 일). `lsof`의 `3u`는 fd 3, 읽기쓰기(`u`), `CHR 89,0`은 문자 장치 major 89 minor 0이다. `fuser`의 `F`는 "열린 파일"이라는 뜻이다. 포트 22를 systemd와 sshd가 함께 가진 것은 socket activation 때문이다.

### 8.3 `/proc/<pid>/` — 프로세스의 모든 것

```sh
sudo ls -l /proc/$P/fd
grep -E "^(Name|State|PPid|Threads|VmRSS|voluntary_ctxt_switches)" /proc/$P/status
tr "\0" " " < /proc/$P/cmdline; echo
cat /proc/$P/wchan; echo
sudo cat /proc/$P/stack | head -6
ps -o pid,ppid,stat,ni,pri,psr,rss,wchan:20,cmd -p $P
```

```text
total 0
lr-x------  1 root root 64 Oct  5 22:44 0 -> /dev/null
l-wx------  1 root root 64 Oct  5 22:44 1 -> /dev/null
l-wx------  1 root root 64 Oct  5 22:44 2 -> /dev/null
lr-x------+ 1 root root 64 Oct  5 22:44 255 -> /Users/donh/.elinux_scratch/A10/sensord.sh
lrwx------  1 root root 64 Oct  5 22:44 3 -> /dev/i2c-0
Name:	bash
State:	S (sleeping)
PPid:	99782
VmRSS:	    5276 kB
Threads:	1
voluntary_ctxt_switches:	21
bash /Users/donh/.elinux_scratch/A10/sensord.sh 
0
[<0>] do_wait+0x6c/0x200
[<0>] kernel_wait4+0xa8/0x1b8
[<0>] __do_sys_wait4+0x120/0x148
[<0>] __arm64_sys_wait4+0x2c/0x58
[<0>] invoke_syscall.constprop.0+0x60/0xf0
[<0>] el0_svc_common.constprop.0+0x114/0x140
    PID    PPID STAT  NI PRI PSR   RSS WCHAN                CMD
  99840   99782 Ss     0  19   2  5276 -                    bash /Users/donh/.elinux_scratch/A10/sensord.sh
```

출력에서 볼 것: `/proc/PID/stack`은 그 태스크의 **커널 스택 백트레이스**다. bash가 자식(`i2cget` 또는 `sleep`)을 `wait4()`로 기다리며 `do_wait`에서 잠들어 있다. `el0_svc_common`은 Don에게 익숙한 그림 그대로 — EL0에서 `SVC` 예외로 커널에 들어온 경로다. "프로세스가 멈췄다(hang)"는 신고에 `/proc/PID/stack`(root)은 JTAG으로 멈춘 코어의 콜스택을 보는 것과 같다. 이 커널에서 `wchan`은 0으로 나왔고 `ps`의 WCHAN도 `-`라서, 이런 경우에는 `stack`이 더 믿을 만하다.

| `/proc/PID/` 항목 | 내용 |
|---|---|
| `fd/`, `fdinfo/` | 열린 fd → 대상 파일, 각 fd의 오프셋·플래그 |
| `status` | 상태, 메모리(VmRSS), 스레드 수, 문맥 교환 횟수 |
| `stack` (root) | 커널 스택 — 어디서 잠들었나 |
| `maps`, `smaps_rollup` | 메모리 맵 (A5) |
| `io` | 읽고 쓴 바이트·syscall 수 |
| `limits` | 열 수 있는 파일 수, 잠글 수 있는 메모리 |
| `task/TID/` | 스레드별 같은 정보 |
| `cmdline`, `environ`, `cwd`, `exe` | 실행 정보 |

### 8.4 strace -p — 살아 있는 프로세스의 syscall 엿보기

```sh
sudo timeout 1.2 strace -f -p $P -e trace=openat,ioctl 2>&1 | grep -E "i2c|ioctl|attached" | head -10
```

```text
strace: Process 99840 attached
ioctl(2, TIOCGWINSZ, 0xffffe6e0cef0)    = -1 ENOTTY (Inappropriate ioctl for device)
strace: Process 119757 attached
[pid 119757] openat(AT_FDCWD, "/usr/lib/aarch64-linux-gnu/libi2c.so.0", O_RDONLY|O_CLOEXEC) = 4
[pid 119757] openat(AT_FDCWD, "/dev/i2c/0", O_RDWR) = -1 ENOENT (No such file or directory)
[pid 119757] openat(AT_FDCWD, "/dev/i2c-0", O_RDWR) = 4
[pid 119757] ioctl(4, _IOC(_IOC_NONE, 0x7, 0x5, 0), 0xffffe770d5c0) = 0
[pid 119757] ioctl(4, _IOC(_IOC_NONE, 0x7, 0x3, 0), 0x68) = 0
[pid 119757] ioctl(4, _IOC(_IOC_NONE, 0x7, 0x20, 0), 0xffffe770d508) = 0
[pid 119757] ioctl(1, TCGETS2, 0xffffe770d158) = -1 ENOTTY (Inappropriate ioctl for device)
```

출력에서 볼 것: `-f`로 자식(`i2cget`, pid 119757)까지 따라갔다. i2cget은 `/dev/i2c/0`(옛 devfs 경로)을 먼저 시도하고 `/dev/i2c-0`을 연 뒤 `I2C_FUNCS → I2C_SLAVE(0x68) → I2C_SMBUS` 순서로 ioctl을 날린다. 드라이버 없이 유저 공간에서 센서를 읽는 코드를 짤 때 그대로 따라 하면 되는 순서다. `strace -c`는 syscall 통계를 낸다.

```sh
sudo strace -c i2cdetect -y 0 2>&1 >/dev/null | head -8
```

```text
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 63.43    0.001275           5       225       112 ioctl
 36.12    0.000726           5       129           write
  0.45    0.000009           2         4           close
  0.00    0.000000           0         1         1 faccessat
  0.00    0.000000           0         5         1 openat
  0.00    0.000000           0         2           read
```

출력에서 볼 것: 버스 스캔 한 번이 ioctl 225번이고 그중 112번이 실패(응답 없는 주소)였다. `2>&1 >/dev/null` 순서 덕분에 strace의 통계(stderr)만 파이프로 가고 i2cdetect의 표(stdout)는 버려졌다 — §1.1의 순서 규칙의 실전 활용이다.

### 8.5 "rmmod가 안 돼요" — 참조 카운트 추적

데몬이 `/dev/i2c-0`을 들고 있는 상태에서 `i2c_stub`를 내려 본다.

```sh
lsmod | grep -E "^(i2c_stub|i2c_dev)"
sudo rmmod i2c_stub; echo rc=$?
sudo fuser -v /dev/i2c-0 2>&1
sudo fuser -k -TERM /dev/i2c-0; sleep 1; sudo fuser /dev/i2c-0; echo "fuser rc=$?"
sudo rmmod i2c_stub; echo rc=$?
```

```text
i2c_stub               24576  1
rmmod: ERROR: Module i2c_stub is in use
rc=1
                     USER        PID ACCESS COMMAND
/dev/i2c-0:          root      99840 F.... bash
                     root      190237 F.... sleep
 99840 190237/dev/i2c-0:         
fuser rc=1
rc=0
```

출력에서 볼 것: `i2c_stub`의 `Used by 1`은 `/dev/i2c-0`을 연 프로세스 때문이다 — i2c-dev가 열릴 때 어댑터를 잡으면서 그 어댑터를 제공한 모듈의 참조 카운트를 올린다. `fuser -k`로 잡고 있는 프로세스를 정리하자 `rmmod`가 성공했다. (`fuser -k` 출력이 깨져 보이는 것은 PID 목록은 stdout, 파일 이름은 stderr로 나가며 섞였기 때문이다.)

---

## 9. 질문 ④ "왜 느린가?" — CPU, 메모리, I/O, 인터럽트

"느리다"는 증상은 네 군데 중 하나다: **CPU가 바쁘다**, **메모리가 모자라 회수(reclaim)한다**, **I/O를 기다린다**, **인터럽트·softirq가 CPU를 먹는다**. 순서대로 좁힌다. 펌웨어에서 "CPU load 측정 → 큐 깊이 확인 → ISR 시간 측정"을 하던 것과 같은 순서다.

### 9.1 ps — 원하는 열만 골라 보기

| 옵션 | 뜻 |
|---|---|
| `ps -ef` / `ps aux` | 전체 프로세스 (System V / BSD 스타일) |
| `ps -eLf` | **스레드(LWP)까지** 한 줄씩 |
| `ps -eo pid,stat,psr,ni,rss,wchan:20,cmd` | 원하는 열만 (`psr` = 지금 도는 CPU) |
| `--sort=-%cpu` | 정렬 (앞의 `-`는 내림차순) |
| `-p PID` / `-L` | 특정 PID / 스레드 펼치기 |

```sh
ps -eo pid,nlwp,comm --sort=-nlwp | head -5
C=$(pgrep -x containerd)
ps -eLf | awk -v p=$C 'NR==1 || $2==p' | head -6
ps -L -o pid,lwp,psr,stat,wchan:16,comm -p $C | head -5
```

```text
    PID NLWP COMMAND
   9382   12 exe
   2088   11 lima-guestagent
   9417   10 containerd
   9546   10 buildkitd
UID          PID    PPID     LWP  C NLWP STIME TTY          TIME CMD
donh        9417    9382    9417  0   10 22:38 ?        00:00:00 containerd
donh        9417    9382    9439  0   10 22:38 ?        00:00:00 containerd
donh        9417    9382    9440  0   10 22:38 ?        00:00:00 containerd
donh        9417    9382    9441  0   10 22:38 ?        00:00:00 containerd
donh        9417    9382    9442  0   10 22:38 ?        00:00:00 containerd
    PID     LWP PSR STAT WCHAN            COMMAND
   9417    9417   3 Sl   futex_do_wait    containerd
   9417    9439   2 Sl   futex_do_wait    containerd
   9417    9440   2 Sl   futex_do_wait    containerd
   9417    9441   1 Sl   futex_do_wait    containerd
```

출력에서 볼 것: 같은 PID 아래 LWP(스레드 ID)가 여럿이다 — Linux에서 스레드는 PID를 공유하는 태스크다(A2). `STAT`의 `l`은 멀티스레드, `WCHAN`의 `futex_do_wait`는 뮤텍스·조건변수에서 잠든 것(A8)이다. 센서 데몬의 한 스레드만 CPU를 먹는다면 `top -H -p PID`나 `pidstat -t -p PID`로 스레드 단위로 본다.

### 9.2 top, htop, pidstat — 누가 CPU를 먹나

`top`과 `htop`은 대화형이다. 스크립트·로그에는 배치 모드(`top -b -n 1`)를 쓴다. `htop`은 배치 모드가 없어서(대화형 화면이라 출력 생략) 보드에서 사람이 볼 때만 쓴다(`F5` 트리, `F6` 정렬, `H` 스레드 표시 토글, `F2`에서 CPU별 막대).

```sh
yes > /dev/null & Y=$!
top -b -n 1 -o %CPU | head -12
ps -eo pid,user,%cpu,%mem,stat,comm --sort=-%cpu | head -5
pidstat -p $Y 1 2
kill $Y
```

```text
top - 22:44:14 up 5 min,  1 user,  load average: 1.36, 0.49, 0.18
Tasks: 151 total,   4 running, 146 sleeping,   0 stopped,   1 zombie
%Cpu(s): 30.2 us, 67.4 sy,  0.0 ni,  2.3 id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st 
MiB Mem :   3895.3 total,   3042.0 free,    531.9 used,    478.8 buff/cache     
MiB Swap:      0.0 total,      0.0 free,      0.0 used.   3363.4 avail Mem 

    PID USER      PR  NI    VIRT    RES    SHR S  %CPU  %MEM     TIME+ COMMAND
 115947 donh      20   0    2220   1096   1016 R  90.9   0.0   0:07.41 udp_rx
 138175 donh      20   0   10608   1476   1332 R  90.9   0.0   0:05.78 ex5
 183263 donh      20   0   17464   7084   4892 R  81.8   0.2   0:00.19 yes
      1 root      20   0   26116  16540  10552 S   0.0   0.4   0:01.21 systemd
      2 root      20   0       0      0      0 S   0.0   0.0   0:00.00 kthreadd
    PID USER     %CPU %MEM STAT COMMAND
 138175 donh     99.8  0.0 Rl   ex5
 115947 donh     96.1  0.0 R    udp_rx
 183263 donh     95.2  0.1 R    yes
      1 root      0.3  0.4 Ss   systemd
Linux 7.0.0-34-generic (lima-elinux) 	10/05/26 	_aarch64_	(4 CPU)

22:44:14      UID       PID    %usr %system  %guest   %wait    %CPU   CPU  Command
22:44:15      501    183263    4.00   97.00    0.00    0.00  101.00     2  yes
22:44:16      501    183263    2.00   97.00    0.00    0.00   99.00     2  yes
Average:      501    183263    3.00   97.00    0.00    0.00  100.00     -  yes
```

출력에서 볼 것: `udp_rx`, `ex5`는 같은 VM의 다른 실습 프로그램이다. `top` 헤더의 `us`(유저)/`sy`(커널)/`wa`(I/O 대기)/`hi`·`si`(하드·소프트 IRQ) 비율이 첫 단서다. `yes > /dev/null`이 **97% system**인 것이 흥미롭다 — 하는 일이 거의 `write()` syscall뿐이라 시간 대부분이 커널 안에 있다. 센서 데몬이 작은 `read()`를 초당 수만 번 부르면 같은 모양이 나온다(→ 버퍼를 키우거나 `poll`·`mmap`으로 바꾼다, A4·A5). `pidstat`의 `%wait`는 runqueue에서 CPU를 기다린 비율로, 실시간 스레드의 지연을 볼 때 중요하다.

### 9.3 free, vmstat — 메모리와 시스템 전체 흐름

```sh
free -h
vmstat 1 3
```

```text
               total        used        free      shared  buff/cache   available
Mem:           3.8Gi       543Mi       3.0Gi        10Mi       479Mi       3.3Gi
Swap:             0B          0B          0B
procs -----------memory---------- ---swap-- -----io---- -system-- -------cpu-------
 r  b   swpd   free   buff  cache   si   so    bi    bo   in   cs us sy id wa st gu
 1  0      0 3103120  28400 462184    0    0  3778  5363 6984   21  8  5 87  0  0  0
 1  0      0 3096248  28400 462352    0    0     0     0 21887  675 27  1 72  0  0  0
 1  0      0 3101664  28400 462352    0    0     0     0 21493  373 25  1 74  0  0  0
```

출력에서 볼 것: 메모리 여유는 `free`가 아니라 **`available`**을 본다(page cache는 필요하면 회수되므로). `vmstat`의 **첫 줄은 부팅 후 평균**이라 버리고 둘째 줄부터 읽는다. `r` = 실행 대기 태스크 수(CPU 수보다 계속 크면 CPU 부족), `b` = uninterruptible sleep(대개 I/O 대기) 수, `in` = 초당 인터럽트, `cs` = 초당 context switch.

디스크에 직접 쓰기(`oflag=direct`)를 걸고 다시 본다. `/tmp`는 tmpfs(RAM)라서 디스크 부하가 안 생기므로 `/var/tmp`에 쓴다.

```sh
df -h /tmp /var/tmp
(timeout 6 dd if=/dev/zero of=/var/tmp/A10big bs=1M count=20000 oflag=direct status=none &) ; sleep 0.5
iostat -dxy vda 1 2 | awk '/^Device|^vda/ {printf "%-7s %8s %10s %8s %7s %6s\n", $1,$8,$9,$12,$22,$23}'
vmstat 1 3
```

```text
Filesystem      Size  Used Avail Use% Mounted on
tmpfs           2.0G  9.5M  1.9G   1% /tmp
/dev/vda1        96G  3.5G   93G   4% /
Device       w/s      wkB/s  w_await  aqu-sz  %util
vda       293.00 1200128.00   155.38   45.90  78.70
Device       w/s      wkB/s  w_await  aqu-sz  %util
vda       291.00 1187840.00   157.26   94.66  85.60
procs -----------memory---------- ---swap-- -----io---- -system-- -------cpu-------
 r  b   swpd   free   buff  cache   si   so    bi    bo   in   cs us sy id wa st gu
 4  1      0 449924  12896 3164312    0    0  3508 30971 8019   19  9  5 86  1  0  0
 1  2      0 397092   9792 3233768    0    0 14308 2504137 22699 3387 26  8 25 41  0  0
 1  2      0 316136   8212 3319520    0    0 42220 2189787 23115 3913 25  7 17 50  0  0
```

출력에서 볼 것: `iostat -x`의 `w_await`(요청 하나가 끝나기까지 평균 ms)가 155 ms, `aqu-sz`(평균 큐 깊이)가 46~95다 — Don이 SSD 펌웨어에서 보던 바로 그 큐 깊이·지연이다. `vmstat`의 `b`가 1~2, `wa`가 41~50%로 올랐다. 즉 "CPU는 놀고 있는데 다들 디스크를 기다린다". 임베디드 보드에서 eMMC·SD 카드에 로그를 많이 쓰는 서비스가 있으면 정확히 이 모양이 나온다. (`iostat`은 `sysstat` 패키지.)

### 9.4 `/proc/interrupts`와 IRQ 비율 스크립트

`/proc/interrupts`는 IRQ마다 CPU별 누적 카운트다. **두 번 읽어 차이를 내면 초당 인터럽트 수**가 된다. 실제 보드에서 "센서 IRQ가 들어오나? 얼마나 자주? 어느 CPU로?"를 이걸로 본다. 첫 열이 IRQ 번호, 그 다음이 CPU별 카운트, 그 다음이 인터럽트 컨트롤러·hwirq·트리거 타입(Level/Edge)·이름이다.

```sh
head -1 /proc/interrupts; grep -E "virtio0|virtio1|arch_timer|xhci" /proc/interrupts | head -8
```

```text
           CPU0       CPU1       CPU2       CPU3       
 10:      39351      37603      39176      38224    GICv3  27 Level     arch_timer
 25:          0          0          0          0 GICv2m-PCI-MSIX-0000:00:05.0   0 Edge      virtio1-config
 26:          0          0          0          0 GICv2m-PCI-MSIX-0000:00:05.0   1 Edge      virtio1-input
 27:         17          0          0          0 GICv2m-PCI-MSIX-0000:00:05.0   2 Edge      virtio1-output
 32:          0          0          0          0 GICv2m-PCI-MSIX-0000:00:01.0   0 Edge      virtio0-config
 33:      17404          0          0          0 GICv2m-PCI-MSIX-0000:00:01.0   1 Edge      virtio0-input.0
 34:       7901          0          0          0 GICv2m-PCI-MSIX-0000:00:01.0   2 Edge      virtio0-output.0
 36:         50          0          0          0 GICv2m-PCI-MSIX-0000:00:0c.0   0 Edge      xhci_hcd
```

출력에서 볼 것: 타이머(`arch_timer`, GIC PPI 27)는 CPU마다 고르게 뛰고, 네트워크 수신(`virtio0-input.0`, MSI-X)은 CPU0에만 몰려 있다. 실제 보드라면 `GICv3 123 Level imu-int` 같은 줄이 보이고, 그 카운트가 안 오르면 핀·DT의 `interrupts` 속성·트리거 타입부터 의심한다. 특정 IRQ가 어느 CPU로 가는지는 `/proc/irq/N/smp_affinity_list`로 바꾼다(N 모듈).

스크립트 1 — IRQ 비율 측정기 (`~/.elinux_scratch/A10/irqrate.sh`):

```sh
#!/usr/bin/env bash
# irqrate.sh <irq-name-pattern> [interval_s] [samples]
# /proc/interrupts에서 이름이 맞는 줄의 전 CPU 합계를 주기적으로 읽어 초당 증가율을 출력한다.
set -euo pipefail
pat=${1:?usage: irqrate.sh <name-pattern> [interval] [samples]}
iv=${2:-1}; n=${3:-5}

irq_sum() {   # 패턴에 맞는 첫 줄의 CPU 열 합계 (숫자 열만 더한다)
  awk -v p="$pat" '$0 ~ p { s=0; for (i=2;i<=NF;i++) if ($i ~ /^[0-9]+$/) s+=$i; else break; print s; exit }' /proc/interrupts
}

prev=$(irq_sum)
[ -n "$prev" ] || { echo "no IRQ matching '$pat'" >&2; exit 1; }
printf "%-8s %12s %10s\n" "time" "total" "irq/s"
for ((k=0; k<n; k++)); do
  sleep "$iv"
  cur=$(irq_sum)
  printf "%-8s %12d %10.1f\n" "$(date +%T)" "$cur" "$(echo "($cur-$prev)/$iv" | bc -l)"
  prev=$cur
done
```

awk 루프는 2번째 열부터 숫자인 동안만 더하고 처음 숫자가 아닌 열(컨트롤러 이름)에서 멈춘다. 그래서 CPU 수와 무관하게 동작한다. 측정 중간(2.2초 뒤)에 게이트웨이로 ping flood 2만 개를 쏜다.

```sh
./irqrate.sh nosuchirq 1 1; echo "rc=$?"
GW=$(ip route | awk '/default/{print $3}')
( sleep 2.2; sudo ping -f -c 20000 -q $GW > /tmp/A10/ping.txt ) &
./irqrate.sh "virtio0-input" 1 6
wait; cat /tmp/A10/ping.txt
```

```text
no IRQ matching 'nosuchirq'
rc=1
time            total      irq/s
22:47:16        17450        0.0
22:47:17        17451        1.0
22:47:18        32980    15529.0
22:47:19        37452     4472.0
22:47:20        37453        1.0
22:47:21        37454        1.0
PING 192.168.5.2 (192.168.5.2) 56(84) bytes of data.

--- 192.168.5.2 ping statistics ---
20000 packets transmitted, 20000 received, 0% packet loss, time 1085ms
rtt min/avg/max/mdev = 0.021/0.040/0.299/0.007 ms, ipg/ewma 0.054/0.041 ms
```

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
  <text x="80" y="24" font-size="12">irq/s (virtio0-input, 4 CPU 합) — irqrate.sh 실측</text>
  <line x1="80" y1="40" x2="80" y2="260" stroke="currentColor"/>
  <line x1="80" y1="260" x2="650" y2="260" stroke="currentColor"/>
  <line x1="80" y1="205" x2="650" y2="205" stroke="#888" stroke-dasharray="3 4"/>
  <line x1="80" y1="150" x2="650" y2="150" stroke="#888" stroke-dasharray="3 4"/>
  <line x1="80" y1="95" x2="650" y2="95" stroke="#888" stroke-dasharray="3 4"/>
  <line x1="80" y1="40" x2="650" y2="40" stroke="#888" stroke-dasharray="3 4"/>
  <text x="72" y="264" font-size="12" text-anchor="end">0</text>
  <text x="72" y="209" font-size="12" text-anchor="end">4000</text>
  <text x="72" y="154" font-size="12" text-anchor="end">8000</text>
  <text x="72" y="99" font-size="12" text-anchor="end">12000</text>
  <text x="72" y="44" font-size="12" text-anchor="end">16000</text>
  <rect x="100" y="259" width="55" height="1" fill="#4a7bd0"/>
  <rect x="195" y="259" width="55" height="1" fill="#4a7bd0"/>
  <rect x="290" y="46.5" width="55" height="213.5" fill="#e08a3c" fill-opacity="0.85"/>
  <rect x="385" y="198.5" width="55" height="61.5" fill="#e08a3c" fill-opacity="0.85"/>
  <rect x="480" y="259" width="55" height="1" fill="#4a7bd0"/>
  <rect x="575" y="259" width="55" height="1" fill="#4a7bd0"/>
  <text x="127.5" y="252" font-size="12" text-anchor="middle">0</text>
  <text x="222.5" y="252" font-size="12" text-anchor="middle">1</text>
  <text x="317.5" y="58" font-size="12" text-anchor="middle">15529</text>
  <text x="412.5" y="192" font-size="12" text-anchor="middle">4472</text>
  <text x="507.5" y="252" font-size="12" text-anchor="middle">1</text>
  <text x="602.5" y="252" font-size="12" text-anchor="middle">1</text>
  <text x="127.5" y="278" font-size="12" text-anchor="middle">22:47:16</text>
  <text x="222.5" y="278" font-size="12" text-anchor="middle">22:47:17</text>
  <text x="317.5" y="278" font-size="12" text-anchor="middle">22:47:18</text>
  <text x="412.5" y="278" font-size="12" text-anchor="middle">22:47:19</text>
  <text x="507.5" y="278" font-size="12" text-anchor="middle">22:47:20</text>
  <text x="602.5" y="278" font-size="12" text-anchor="middle">22:47:21</text>
  <text x="80" y="305" font-size="12">1초 간격 샘플. 주황 = ping -f 20000개가 지나간 구간 (15529 + 4472 = 20001 IRQ)</text>
</svg>
```
그림 5 — irqrate.sh로 잰 네트워크 수신 IRQ 비율. 평소 초당 1회 남짓이던 카운터가 ping flood 동안 초당 1만 5천 회로 뛰었다.

출력에서 볼 것: echo reply 20,000개에 IRQ가 20,001번 — 응답 하나에 IRQ 하나다. ping -f는 응답을 받아야 다음을 보내므로 수신 큐에 패킷이 하나씩만 쌓이고, NAPI(인터럽트 후 폴링으로 전환해 여러 패킷을 한 번에 처리하는 커널 메커니즘)가 묶을 것이 없었다. 반대로 앞 절 `xargs` 예제에서 eth0는 패킷 9만 개를 받았지만 이 IRQ 카운터는 1만 7천대였다 — 대량 다운로드에서는 NAPI가 IRQ 한 번에 여러 패킷을 처리한다. **"IRQ 수 ≠ 이벤트 수"**는 센서 드라이버에서도 같다(FIFO watermark 인터럽트 한 번에 샘플 여러 개).

---

## 10. 질문 ⑥ "네트워크·CAN이 살아 있나?"

### 10.1 ip — 링크, 주소, 라우팅

`ifconfig`/`route`(net-tools)는 오래된 도구고, 지금은 `ip`(iproute2)가 표준이다. 둘 다 결국 커널에 **netlink 소켓**으로 묻는다.

```sh
ip -br addr
ip addr show eth0
ip -s link show eth0
ip route
ip route get 1.1.1.1
```

```text
lo               UNKNOWN        127.0.0.1/8 ::1/128 
eth0             UP             192.168.5.15/24 metric 200 fe80::5055:55ff:fea5:ed31/64 
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP group default qlen 1000
    link/ether 52:55:55:a5:ed:31 brd ff:ff:ff:ff:ff:ff
    altname enx525555a5ed31
    inet 192.168.5.15/24 metric 200 brd 192.168.5.255 scope global dynamic eth0
       valid_lft 3208sec preferred_lft 3208sec
    inet6 fe80::5055:55ff:fea5:ed31/64 scope link proto kernel_ll 
       valid_lft forever preferred_lft forever
2: eth0: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc pfifo_fast state UP mode DEFAULT group default qlen 1000
    link/ether 52:55:55:a5:ed:31 brd ff:ff:ff:ff:ff:ff
    RX:  bytes packets errors dropped  missed   mcast           
     126852027   90517      0       0       0       0 
    TX:  bytes packets errors dropped carrier collsns           
        673784    9440      0       0       0       0 
    altname enx525555a5ed31
default via 192.168.5.2 dev eth0 proto dhcp src 192.168.5.15 metric 200 
192.168.5.0/24 dev eth0 proto kernel scope link src 192.168.5.15 metric 200 
192.168.5.2 dev eth0 proto dhcp scope link src 192.168.5.15 metric 200 
1.1.1.1 via 192.168.5.2 dev eth0 src 192.168.5.15 uid 501 
    cache 
```

출력에서 볼 것: 플래그의 `UP`은 "관리자가 켬", `LOWER_UP`은 "물리 링크(캐리어) 있음"이다. **`UP`인데 `LOWER_UP`이 없으면 케이블·PHY 문제**다(`NO-CARRIER`로 표시됨). `-s`의 `errors`/`dropped`/`missed`가 오르면 드라이버·DMA 링 문제를 의심한다. `ip route get`은 "이 목적지로 가는 패킷이 실제로 어느 인터페이스·소스 주소를 쓰는가"를 커널에 직접 묻는다 — LiDAR가 연결된 두 번째 NIC로 패킷이 안 갈 때 쓴다.

### 10.2 ss — 소켓과 포트

```sh
sudo ss -tulpn
```

```text
Netid State  Recv-Q Send-Q     Local Address:Port  Peer Address:PortProcess                                                 
udp   UNCONN 0      0                0.0.0.0:5353       0.0.0.0:*    users:(("systemd-resolve",pid=1777,fd=13))             
udp   UNCONN 0      0             127.0.0.54:53         0.0.0.0:*    users:(("systemd-resolve",pid=1777,fd=21))             
udp   UNCONN 0      0          127.0.0.53%lo:53         0.0.0.0:*    users:(("systemd-resolve",pid=1777,fd=19))             
udp   UNCONN 0      0      192.168.5.15%eth0:68         0.0.0.0:*    users:(("systemd-network",pid=1031,fd=32))             
udp   UNCONN 0      0              127.0.0.1:323        0.0.0.0:*    users:(("chronyd",pid=1292,fd=4))                      
...
tcp   LISTEN 0      4096           127.0.0.1:43783      0.0.0.0:*    users:(("containerd",pid=9417,fd=16))                  
tcp   LISTEN 0      4096             0.0.0.0:22         0.0.0.0:*    users:(("sshd",pid=1255,fd=3),("systemd",pid=1,fd=136))
...
```

출력에서 볼 것 (`...`은 줄 생략): `-t` TCP, `-u` UDP, `-l` listen, `-p` 프로세스, `-n` 숫자 그대로. "LiDAR 수신 데몬이 2368 포트를 정말 열었나, 어느 주소(0.0.0.0 vs 특정 NIC)에 bind 했나"를 이걸로 확인한다. UDP 수신 버퍼가 넘치는지는 `Recv-Q`가 계속 차 있는지로 본다. `ss`는 커널의 `sock_diag` netlink를 쓰는데, §5.1 `lsmod`에 있던 `tcp_diag`·`udp_diag` 모듈이 바로 그것이다.

### 10.3 ping, tcpdump — 패킷이 오가나

```sh
GW=$(ip route | awk '/default/{print $3}'); ping -c 3 -W 1 $GW
```

```text
PING 192.168.5.2 (192.168.5.2) 56(84) bytes of data.
64 bytes from 192.168.5.2: icmp_seq=1 ttl=64 time=0.219 ms
64 bytes from 192.168.5.2: icmp_seq=2 ttl=64 time=0.145 ms
64 bytes from 192.168.5.2: icmp_seq=3 ttl=64 time=0.118 ms

--- 192.168.5.2 ping statistics ---
3 packets transmitted, 3 received, 0% packet loss, time 2080ms
rtt min/avg/max/mdev = 0.118/0.160/0.219/0.042 ms
```

`tcpdump`는 네트워크용 로직 분석기다. 가짜 LiDAR 패킷(UDP)을 루프백으로 보내고 캡처한다. `-nn`은 이름·포트 해석 안 함, `-c N`은 N개 후 종료, `-X`는 hex+ASCII 덤프, `-w`는 pcap 파일로 저장(나중에 Wireshark로 연다).

```sh
(sudo timeout 4 tcpdump -i lo -nn -c 3 -X udp port 12368 > /tmp/A10/td.txt 2>&1 &); sleep 1.5
for i in 1 2 3; do echo -n "LIDAR-PKT-$i" | socat - UDP-SENDTO:127.0.0.1:12368; sleep 0.3; done
sleep 1; cat /tmp/A10/td.txt
```

```text
tcpdump: verbose output suppressed, use -v[v]... for full protocol decode
listening on lo, link-type EN10MB (Ethernet), snapshot length 262144 bytes
22:45:27.305046 IP 127.0.0.1.42363 > 127.0.0.1.12368: UDP, length 11
	0x0000:  4500 0027 0e32 4000 4011 2e92 7f00 0001  E..'.2@.@.......
	0x0010:  7f00 0001 a57b 3050 0013 fe26 4c49 4441  .....{0P...&LIDA
	0x0020:  522d 504b 542d 31                        R-PKT-1
22:45:28.120296 IP 127.0.0.1.35227 > 127.0.0.1.12368: UDP, length 11
...
22:45:28.924386 IP 127.0.0.1.45019 > 127.0.0.1.12368: UDP, length 11
...

3 packets captured
6 packets received by filter
0 packets dropped by kernel
```

```sh
(sudo timeout 3 tcpdump -i lo -nn -w /tmp/A10/lidar.pcap udp port 12368 > /tmp/A10/tdw.txt 2>&1 &); sleep 1.5
for i in 1 2; do echo -n "X$i" | socat - UDP-SENDTO:127.0.0.1:12368; sleep 0.2; done; sleep 2
cat /tmp/A10/tdw.txt; sudo tcpdump -nn -r /tmp/A10/lidar.pcap
```

```text
tcpdump: listening on lo, link-type EN10MB (Ethernet), snapshot length 262144 bytes
2 packets captured
4 packets received by filter
0 packets dropped by kernel
reading from file /tmp/A10/lidar.pcap, link-type EN10MB (Ethernet), snapshot length 262144
22:45:32.248963 IP 127.0.0.1.38234 > 127.0.0.1.12368: UDP, length 2
22:45:32.961150 IP 127.0.0.1.43270 > 127.0.0.1.12368: UDP, length 2
```

출력에서 볼 것: hex 덤프에서 IP 헤더(`4500`…), UDP 헤더(포트 0x3050 = 12368), 페이로드 `LIDAR-PKT-1`이 그대로 보인다. `received by filter`가 캡처 수보다 큰 것은 필터 단계에 도달한 패킷이 더 있었다는 뜻이다(받는 소켓이 없어 생긴 ICMP port unreachable 등 다른 패킷). `dropped by kernel`이 0이 아니면 캡처가 못 따라간 것이니 필터를 좁히거나 `-w`로 파일에 쓴다. 보드에서 tcpdump는 `-w`로 저장하고 PC로 가져와 보는 것이 정석이다. 처음에 LiDAR 표준 포트 2368을 썼다가 같은 VM의 다른 실습과 겹쳐 캡처가 섞였다 — 공용 장비에서는 포트 충돌부터 확인한다.

### 10.4 CAN — SocketCAN 인터페이스 올리기

Linux에서 CAN 컨트롤러는 `/dev` 장치가 아니라 **네트워크 인터페이스**(`can0`)다(A9). 그래서 `ip`로 설정하고, can-utils(`candump`, `cansend`, `cangen`)로 프레임을 주고받는다. 이 VM에는 bit timing 설정을 실제로 받는 가상 컨트롤러 `dummy_can`이 있어서, 실제 보드와 **같은 문법**을 그대로 돌려 볼 수 있다.

```sh
sudo modprobe dummy_can; ip -br link | grep -i can
ip -details link show can0
sudo ip link set can0 up; echo rc=$?
```

```text
vcan0            UNKNOWN        <NOARP,UP,LOWER_UP> 
can0             DOWN           <NOARP,ECHO> 
9: can0: <NOARP,ECHO> mtu 16 qdisc noop state DOWN mode DEFAULT group default qlen 10
    link/can  promiscuity 0 allmulti 0 minmtu 16 maxmtu 16 
    can state STOPPED restart-ms 0 
	  dummy_can CC: tseg1 2..256 tseg2 2..128 sjw 1..128 brp 1..512 brp_inc 1
	  dummy_can FD: dtseg1 2..256 dtseg2 2..128 dsjw 1..128 dbrp 1..512 dbrp_inc 1
	  tdco 0..127 tdcf 0..127
	  dummy_can XL: xtseg1 2..256 xtseg2 2..128 xsjw 1..128 xbrp 1..512 xbrp_inc 1
	  xtdco 0..127 xtdcf 0..127
	  pwms 1..8 pwml 2..24 pwmo 0..16
	  clock 160000000 numtxqueues 1 numrxqueues 1 gso_max_size 65536 gso_max_segs 65535 tso_max_size 65536 tso_max_segs 65535 gro_max_size 65536 gso_ipv4_max_size 65536 gro_ipv4_max_size 65536 
RTNETLINK answers: Invalid argument
rc=2
```

출력에서 볼 것: `ip -details`는 컨트롤러가 허용하는 bit timing 범위(`tseg1`, `brp`…)와 CAN 클럭(160 MHz)을 보여 준다. **bitrate를 정하지 않고 `up` 하면 `Invalid argument`** — 실제 보드 첫날 가장 흔한 에러다(dmesg에는 `can0: bit-timing not yet defined`). 순서는 항상 `down → type can bitrate ... → up`이다.

```sh
sudo ip link set can0 type can bitrate 500000 sample-point 0.875; echo rc=$?
sudo ip link set can0 up; echo "up rc=$?"
ip -details link show can0 | sed -n 1,5p
(timeout 1.5 candump can0 > /tmp/A10/cd4.txt 2>&1 &); sleep 0.4; cansend can0 123#DEADBEEF; sleep 1.3; cat /tmp/A10/cd4.txt
ip -s link show can0 | sed -n 3,6p
```

```text
rc=0
up rc=0
10: can0: <NOARP,UP,LOWER_UP,ECHO> mtu 16 qdisc pfifo_fast state UP mode DEFAULT group default qlen 10
    link/can  promiscuity 0 allmulti 0 minmtu 16 maxmtu 16 
    can state STOPPED restart-ms 0 
	  bitrate 500000 sample-point 0.875
	  tq 12 prop-seg 69 phase-seg1 70 phase-seg2 20 sjw 10 brp 2
  can0  123   [4]  DE AD BE EF
    RX:  bytes packets errors dropped  missed   mcast           
             0       0      0       0       0       0 
    TX:  bytes packets errors dropped carrier collsns           
             4       1      0       0       0       0 
```

출력에서 볼 것: 커널이 500 kbit/s, sample point 87.5%를 만족하는 `brp=2`(tq 12.5 ns, 화면에는 12로 표시), `prop-seg 69 + phase-seg1 70 + phase-seg2 20`(+ sync 1 = 160 tq)을 **자동 계산**했다. 160 MHz / 2 / 160 tq = 500 kHz. 실제 컨트롤러에서는 이 값이 트랜시버·버스 길이에 맞는지 scope로 비트 폭(2 µs)을 재서 확인한다. 보낸 프레임이 TX 카운터에 1개로 잡혔다(dummy_can은 수신 측이 없어 RX는 0).

인터페이스가 up인 상태에서 bitrate를 바꾸려 하면(이 노트 작성 중 앞선 실행에서 본 출력):

```sh
sudo ip link set can0 type can bitrate 250000; echo rc=$?
```

```text
RTNETLINK answers: Device or resource busy
rc=2
```

**up 상태에서는 bit timing을 못 바꾼다**(`EBUSY`). 반드시 `ip link set can0 down`부터 한다.

버스 오프 자동 복구(`restart-ms`)는 실제 컨트롤러 드라이버(FlexCAN, MCP251xFD 등)는 지원하지만 dummy_can은 지원하지 않는다.

```sh
sudo ip link set can0 down; sudo ip link set can0 type can restart-ms 100; echo rc=$?
```

```text
Error: Device doesn't support restart from Bus Off.
rc=2
```

프레임을 주고받는다. `candump`를 백그라운드로 띄우고 `cansend`로 표준 ID(11비트), 확장 ID(29비트, J1939 형태), OBD-II 요청을 보낸다. 다른 실습이 `vcan0`을 쓰고 있어서 이 실험은 별도 vcan 인터페이스 `can_a10`에서 했다(vcan은 bitrate 개념이 없는 순수 소프트웨어 루프백이다).

```sh
I=can_a10; sudo ip link add dev $I type vcan; sudo ip link set $I type can bitrate 500000; echo rc=$?
sudo ip link set $I up
(timeout 2 candump -td -x $I > /tmp/A10/cd.txt 2>&1 &); sleep 0.5
cansend $I 123#DEADBEEF; sleep 0.1; cansend $I 18FF50E5#0102030405060708; sleep 0.1; cansend $I 7DF#02010D
sleep 2; cat /tmp/A10/cd.txt
(timeout 1.5 candump $I,123:7FF > /tmp/A10/cd2.txt 2>&1 &); sleep 0.4; cansend $I 123#11; cansend $I 456#22; sleep 1.5; cat /tmp/A10/cd2.txt
(timeout 1.5 candump -L $I > /tmp/A10/cd3.txt 2>&1 &); sleep 0.4; cansend $I 321#CAFE; sleep 1.5; cat /tmp/A10/cd3.txt
ip -s link show $I | sed -n 3,6p
```

```text
RTNETLINK answers: Operation not supported
rc=2
 (000.000000)  can_a10  TX - -  123   [4]  DE AD BE EF
 (000.112219)  can_a10  TX - -  18FF50E5   [8]  01 02 03 04 05 06 07 08
 (000.102906)  can_a10  TX - -       7DF   [3]  02 01 0D
  can_a10  123   [1]  11
(1791265560.934053) can_a10 321#CAFE
    RX:  bytes packets errors dropped  missed   mcast           
            19       6      0       0       0       0 
    TX:  bytes packets errors dropped carrier collsns           
            19       6      0       0       0       0 
```

출력에서 볼 것: vcan에 bitrate를 주면 `Operation not supported`다(타이밍이 없는 가상 버스). `candump -td`는 프레임 간 시간차, `-x`는 방향(TX/RX) 같은 추가 정보다. `ID#DATA` 형식에서 ID가 3자리면 표준, 8자리면 확장 프레임이다. `candump can_a10,123:7FF`는 **ID 필터**(`ID:MASK`)로 0x456은 걸러졌다 — 이 필터는 커널의 `CAN_RAW_FILTER` 소켓 옵션으로 걸린다(A9). `-L`은 `canplayer`로 다시 재생할 수 있는 로그 형식이다. 실제 버스에서 프레임이 안 보이면 `ip -details -statistics link show can0`의 `can state`(ERROR-ACTIVE → ERROR-WARNING → ERROR-PASSIVE → BUS-OFF)와 `bus-errors` 카운터를 먼저 본다. 이 dummy_can 출력에서는 이렇게 보였다.

```sh
ip -s -d link show can0 | tail -8
```

```text
	  pwms 1..8 pwml 2..24 pwmo 0..16
	  clock 160000000 
	  re-started bus-errors arbit-lost error-warn error-pass bus-off
	  0          0          0          0          0          0         numtxqueues 1 numrxqueues 1 gso_max_size 65536 gso_max_segs 65535 tso_max_size 65536 tso_max_segs 65535 gro_max_size 65536 gso_ipv4_max_size 65536 gro_ipv4_max_size 65536 
    RX:  bytes packets errors dropped  missed   mcast           
             0       0      0       0       0       0 
    TX:  bytes packets errors dropped carrier collsns           
             0       0      0       0       0       0 
```

실제 보드 전용 (VM에 하드웨어 없음 — 실제 보드 출력 예시는 생략): 트랜시버가 붙은 진짜 버스에서는 상대 노드가 ACK를 안 주면 `can state`가 ERROR-PASSIVE로 가고 `bus-errors`가 오른다. 노드가 하나뿐인 버스에서 "보냈는데 아무 일도 없다"는 대부분 ACK 부재 또는 종단 저항(120 Ω) 누락이다.

---

## 11. 보드 접속과 파일 전송

### 11.1 ssh, scp, rsync

네트워크가 살아 있는 보드라면 일은 대부분 ssh로 한다. 이 VM 안에서 자기 자신(localhost)에 임시 키로 접속해 시연했다(실험 후 키를 지웠다).

```sh
cd /tmp/A10; ssh-keygen -q -t ed25519 -N "" -f k -C a10-temp; cp ~/.ssh/authorized_keys ak.bak; cat k.pub >> ~/.ssh/authorized_keys
export SSHOPT="-i /tmp/A10/k -o BatchMode=yes"
mkdir -p fw && head -c 300000 /dev/urandom > fw/app.bin && echo v1 > fw/VERSION
ssh $SSHOPT localhost "uname -r; cat /proc/loadavg"
ssh $SSHOPT localhost "sudo dmesg" | grep -c i2c
scp $SSHOPT -q fw/app.bin localhost:/tmp/A10/app_copy.bin; echo rc=$?; ls -l /tmp/A10/app_copy.bin
rsync -az --stats -e "ssh $SSHOPT" fw/ localhost:/tmp/A10/board_fw/ | grep -E "Number of (regular )?files transferred|Total transferred file size"
echo v2 > fw/VERSION
rsync -azv -e "ssh $SSHOPT" fw/ localhost:/tmp/A10/board_fw/
touch fw/app.bin; rsync -azn --itemize-changes -e "ssh $SSHOPT" fw/ localhost:/tmp/A10/board_fw/
cp ak.bak ~/.ssh/authorized_keys          # 임시 키 제거
```

```text
7.0.0-34-generic
0.44 0.51 0.28 4/226 191449
28
rc=0
-rw-rw-r-- 1 donh donh 300000 Oct  5 22:48 /tmp/A10/app_copy.bin
Number of regular files transferred: 2
Total transferred file size: 300,003 bytes
sending incremental file list
VERSION

sent 155 bytes  received 41 bytes  392.00 bytes/sec
total size is 300,003  speedup is 1,530.63
<f..t...... app.bin
```

출력에서 볼 것: `ssh board "cmd"`의 출력은 **로컬** 파이프로 이어진다(원격 `dmesg`를 로컬 `grep`이 셌다). 따옴표 안의 파이프는 원격에서, 밖의 파이프는 로컬에서 실행된다. `rsync`는 두 번째 실행에서 바뀐 `VERSION`만 보냈다 — 빌드 산출물을 보드에 반복 배포할 때 `scp`보다 훨씬 빠르다. `-n --itemize-changes`(dry run)는 "무엇을 보낼지"만 보여 준다(`t`는 시각만 다름). 소스 경로 끝의 `/`는 "디렉터리 **안의 내용**을 보내라"는 뜻이고, 빼면 디렉터리 자체가 하나 더 생긴다.

### 11.2 시리얼 콘솔 — picocom, screen, minicom

네트워크가 없거나 부팅 초기(U-Boot, 커널 패닉)에는 UART 콘솔이 유일한 창이다. 이 VM에는 보드 UART가 없어서 `socat`으로 가상 직렬선(pty 한 쌍)을 만들고, 한쪽에 "보드"가 부트 배너를 쓰면 다른 쪽에서 `picocom`이 받는 것을 시연했다.

```sh
(socat pty,raw,echo=0,link=/tmp/A10/ttyBOARD pty,raw,echo=0,link=/tmp/A10/ttyHOST &) ; sleep 0.5
ls -l /tmp/A10/ttyBOARD /tmp/A10/ttyHOST
stty -F /tmp/A10/ttyHOST 115200 raw -echo; stty -F /tmp/A10/ttyHOST -a | head -2
( sleep 0.7; printf "U-Boot 2024.04 (fake banner)\r\nHit any key to stop autoboot:  3\r\n" > /tmp/A10/ttyBOARD ) &
picocom -b 115200 -q -x 2000 /tmp/A10/ttyHOST
```

```text
lrwxrwxrwx 1 donh donh 10 Oct  5 22:48 /tmp/A10/ttyBOARD -> /dev/pts/0
lrwxrwxrwx 1 donh donh 10 Oct  5 22:48 /tmp/A10/ttyHOST -> /dev/pts/1
speed 115200 baud; rows 0; columns 0; line = 0;
intr = ^C; quit = ^\; erase = ^?; kill = ^U; eof = ^D; eol = <undef>;
U-Boot 2024.04 (fake banner)
Hit any key to stop autoboot:  3
```

출력에서 볼 것: `picocom -x 2000`은 2초 뒤 자동 종료(스크립트용)다. 대화형으로 쓸 때 종료는 `Ctrl-A Ctrl-X`다. `stty -a`로 포트의 baud·raw 모드를 확인한다. 권한 문제가 나면 장치 그룹(`dialout`)을 본다.

```sh
ls -l /dev/ttyAMA0 /dev/ttyS0 2>&1
```

```text
ls: cannot access '/dev/ttyAMA0': No such file or directory
crw-rw---- 1 root dialout 4, 64 Oct  5 22:38 /dev/ttyS0
```

실제 보드 전용 (VM에 하드웨어 없음 — 실제 보드 출력 예시는 생략). 아래는 PC에 USB-UART 어댑터를 꽂았을 때 쓰는 형태다.

```sh
picocom -b 115200 /dev/ttyUSB0          # 종료: Ctrl-A Ctrl-X
screen /dev/ttyUSB0 115200              # 종료: Ctrl-A k  (y로 확인)
minicom -D /dev/ttyUSB0 -b 115200       # 종료: Ctrl-A x, 설정: Ctrl-A o
picocom -b 115200 --logfile boot.log /dev/ttyUSB0
```

USB-UART 이름은 칩에 따라 `/dev/ttyUSB0`(FTDI·CP210x) 또는 `/dev/ttyACM0`(CDC-ACM)이다. 꽂는 순간 `dmesg -W`나 `udevadm monitor`로 어느 이름이 생겼는지 본다(§5.8). 보드 쪽 콘솔 이름은 SoC UART 드라이버에 따라 `ttyS0`(8250 계열, TI AM62), `ttyAMA0`(PL011), `ttymxc0`(i.MX), `ttyTCU0`(Jetson) 등으로 다르고, 커널 커맨드라인 `console=ttymxc0,115200`이 그 이름을 정한다.

### 11.3 TFTP, NFS — 개발 중 부팅 루프 줄이기

실제 보드 전용 (VM에 하드웨어 없음 — 실제 보드 출력 예시는 생략). 커널·DT를 고칠 때마다 SD 카드를 다시 굽지 않도록, 개발 단계에서는 U-Boot가 **TFTP**로 커널 이미지를 PC에서 받아 오고, rootfs는 **NFS**로 PC 디렉터리를 그대로 마운트하는 구성을 흔히 쓴다. U-Boot 프롬프트에서의 대략적인 흐름(명령 이름은 실제 U-Boot 명령이고 주소·파일 이름은 보드마다 다르다):

```sh
setenv serverip 192.168.1.10; setenv ipaddr 192.168.1.50
tftpboot ${kernel_addr_r} Image; tftpboot ${fdt_addr_r} board.dtb
setenv bootargs console=ttymxc0,115200 root=/dev/nfs nfsroot=192.168.1.10:/srv/nfs/rootfs,v3 ip=dhcp
booti ${kernel_addr_r} - ${fdt_addr_r}
```

이렇게 하면 PC에서 빌드한 모듈을 NFS rootfs에 복사하는 즉시 보드에서 `modprobe`할 수 있다. 자세한 것은 C 모듈(부트 흐름)과 K 모듈(rootfs)에서 다룬다.

---

## 12. BusyBox 보드에서 — 같은 이름, 다른 옵션

작은 보드의 rootfs는 coreutils·util-linux·procps 대신 **BusyBox** 하나로 수백 개 명령을 제공하는 경우가 많다(Buildroot 기본값). 하나의 바이너리가 `argv[0]`(호출된 이름)을 보고 어떤 applet으로 동작할지 정한다. 이름은 같아도 옵션이 적다.

```sh
busybox | head -2; busybox --list | wc -l
busybox --list | grep -E "^(devmem|i2cget|i2cdetect|lsof|ss|ip|top|watch|ps)$" | tr "\n" " "; echo
busybox ps | head -3
busybox ps -eLf 2>&1 | head -3
echo "irq 42" | busybox grep -P "\d+" 2>&1 | head -3
echo a > /tmp/A10/bb.txt; busybox sed -i s/a/b/ /tmp/A10/bb.txt; cat /tmp/A10/bb.txt
busybox ip -br addr 2>&1 | head -2
sudo busybox dmesg | tail -1; sudo busybox dmesg -T 2>&1 | head -2
```

```text
BusyBox v1.37.0 (Ubuntu 1:1.37.0-7ubuntu1) multi-call binary.
BusyBox is copyrighted by many authors between 1998-2015.
274
devmem i2cdetect i2cget ip ps top watch 
PID   USER     COMMAND
    1 root     /usr/lib/systemd/systemd --switched-root --system --deserialize=48
    2 root     [kthreadd]
ps: invalid option -- 'L'
BusyBox v1.37.0 (Ubuntu 1:1.37.0-7ubuntu1) multi-call binary.

grep: invalid option -- 'P'
BusyBox v1.37.0 (Ubuntu 1:1.37.0-7ubuntu1) multi-call binary.

b
BusyBox v1.37.0 (Ubuntu 1:1.37.0-7ubuntu1) multi-call binary.

[  465.261267] can0: bit-timing not yet defined
dmesg: invalid option -- 'T'
BusyBox v1.37.0 (Ubuntu 1:1.37.0-7ubuntu1) multi-call binary.
```

출력에서 볼 것: 이 빌드에는 applet이 274개이고 `lsof`·`ss`는 없다. `ps -eLf`, `grep -P`, `dmesg -T`, `ip -br`가 모두 거부됐다(`ip`는 사용법만 찍고 끝남). 어떤 applet이 들어갈지는 **빌드 설정**(BusyBox `menuconfig`)으로 정해지므로 보드마다 다르다 — 항상 `busybox --list`로 먼저 확인한다.

| 하고 싶은 것 | GNU·util-linux·procps | BusyBox에서 대안 |
|---|---|---|
| 스레드 보기 | `ps -eLf` | `ls /proc/PID/task`, `top`에서 스레드 표시 옵션(빌드 설정에 따라) |
| 사람이 읽는 dmesg 시각 | `dmesg -T` | `cat /proc/uptime`과 비교해 계산, 또는 `logread`(syslogd 사용 시) |
| 누가 장치를 여나 | `lsof /dev/X`, `fuser -v` | `ls -l /proc/[0-9]*/fd/ 2>/dev/null` 결과를 grep (`fuser` applet이 있으면 그것) |
| 포트 확인 | `ss -tulpn` | `netstat -tulpn` (applet이 있으면), `cat /proc/net/tcp` |
| Perl 정규식 | `grep -P` | `grep -E`로 다시 쓰기 (`\d` 대신 `[0-9]`) |
| 짧은 ip 출력 | `ip -br a` | `ip addr` 전체 출력 |
| 배열·`[[ ]]`·`PIPESTATUS` | bash | BusyBox `ash`는 bash 확장 일부만 — 스크립트는 POSIX `sh`로 쓰고 `#!/bin/sh` |
| 로그 보기 | `journalctl` | systemd가 없으면 `logread`(BusyBox syslogd 링버퍼) 또는 `/var/log/messages` |

마지막 두 줄이 특히 중요하다. Don이 PC에서 bash로 짠 진단 스크립트를 보드로 가져가면 `#!/bin/bash`가 없어서 실행조차 안 되거나, `ash`에서 `pipefail`·배열이 안 될 수 있다. 보드에 들어갈 스크립트는 처음부터 `#!/bin/sh` + POSIX 문법으로 쓰고, 보드에서 한 번 돌려 본다.

---

## 13. 디버그 번들 스크립트 — "증상 났을 때 이거 한 번 돌려 주세요"

현장 장비나 동료 보드에서 문제가 났을 때, 명령을 하나씩 부탁하는 대신 **한 번에 상태를 수집하는 스크립트**를 준다. 스크립트 2 (`~/.elinux_scratch/A10/debug_bundle.sh`):

```sh
#!/usr/bin/env bash
# debug_bundle.sh [outdir] — 보드 상태를 한 번에 모아 tar.gz로 만든다 (root로 실행)
set -uo pipefail                       # -e는 일부러 뺀다: 명령 하나 실패해도 계속 모은다
out=${1:-/tmp}/bundle-$(hostname)-$(date +%Y%m%d-%H%M%S)
mkdir -p "$out"

run() {                                # run <파일이름> <명령...>
  local name=$1; shift
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "skip: $1 not installed" > "$out/$name.txt"; return
  fi
  { echo "\$ $*"; "$@"; echo "[rc=$?]"; } > "$out/$name.txt" 2>&1
}
grab() { [ -r "$1" ] && cp "$1" "$out/$2" 2>/dev/null || echo "missing $1" >> "$out/missing.txt"; }

run uname      uname -a
run dmesg      dmesg -T
run journal_k  journalctl -k -b --no-pager
run lsmod      lsmod
run lspci      lspci -k
run lsusb      lsusb -t
run i2c        i2cdetect -l
run gpio       gpiodetect
run iplink     ip -details -statistics link
run ss         ss -tulpn
run ps         ps -eo pid,ppid,stat,ni,psr,%cpu,rss,wchan:20,cmd
run free       free -m
run vmstat     vmstat 1 3
grab /proc/cmdline       cmdline.txt
grab /proc/interrupts    interrupts.txt
grab /proc/iomem         iomem.txt
grab /sys/kernel/debug/devices_deferred  devices_deferred.txt
grab /sys/kernel/debug/gpio              debugfs_gpio.txt

tar -C "$(dirname "$out")" -czf "$out.tar.gz" "$(basename "$out")" && rm -rf "$out"
echo "bundle: $out.tar.gz ($(du -h "$out.tar.gz" | cut -f1))"
```

설계 포인트: `-e`를 빼고 각 명령의 `rc`를 파일에 남긴다(진단 도구는 "끝까지 모으기"가 우선). 도구가 없으면 `skip`을 남긴다(BusyBox 보드 대응). 파일 이름과 `$ 명령`을 기록해 받는 사람이 무엇을 실행한 결과인지 알게 한다. `cp`로 `/proc` 파일을 복사하는 것은 동작하지만, 크기가 0으로 보이는 procfs 파일을 `cp`하지 못하는 오래된 도구도 있으니 의심되면 `cat 파일 > 대상`으로 바꾼다.

```sh
sudo /Users/donh/.elinux_scratch/A10/debug_bundle.sh /tmp/A10
f=$(ls -t /tmp/A10/bundle-*.tar.gz | head -1); tar -tzf $f | sort
tar -xzOf $f --wildcards "*/i2c.txt"
```

```text
bundle: /tmp/A10/bundle-lima-elinux-20261005-224734.tar.gz (36K)
bundle-lima-elinux-20261005-224734/
bundle-lima-elinux-20261005-224734/cmdline.txt
bundle-lima-elinux-20261005-224734/debugfs_gpio.txt
bundle-lima-elinux-20261005-224734/devices_deferred.txt
bundle-lima-elinux-20261005-224734/dmesg.txt
bundle-lima-elinux-20261005-224734/free.txt
bundle-lima-elinux-20261005-224734/gpio.txt
bundle-lima-elinux-20261005-224734/i2c.txt
bundle-lima-elinux-20261005-224734/interrupts.txt
bundle-lima-elinux-20261005-224734/iomem.txt
bundle-lima-elinux-20261005-224734/iplink.txt
bundle-lima-elinux-20261005-224734/journal_k.txt
bundle-lima-elinux-20261005-224734/lsmod.txt
bundle-lima-elinux-20261005-224734/lspci.txt
bundle-lima-elinux-20261005-224734/lsusb.txt
bundle-lima-elinux-20261005-224734/ps.txt
bundle-lima-elinux-20261005-224734/ss.txt
bundle-lima-elinux-20261005-224734/uname.txt
bundle-lima-elinux-20261005-224734/vmstat.txt
$ i2cdetect -l
i2c-0	smbus     	SMBus stub driver               	SMBus adapter
[rc=0]
```

출력에서 볼 것: 36 KB짜리 묶음 하나에 그림 1의 질문 대부분에 대한 답이 들어 있다. `missing.txt`가 없다 = 모든 파일을 읽었다. 실무에서는 여기에 `/proc/device-tree`(또는 `dtc -I fs -O dts /proc/device-tree` 결과), `/sys/kernel/debug/clk/clk_summary`, `/sys/kernel/debug/pinctrl/` 등을 보드에 맞게 더한다.

---

## 14. 커널 안에서는 무슨 일이 일어나나

이 노트의 명령들은 모두 "커널이 내놓은 파일·소켓을 읽고 쓰는 유저 프로그램"이다. 각 명령이 커널의 어디로 들어가는지 알면, 드라이버를 쓸 때 **내 코드가 어떤 명령에 어떻게 보일지**를 설계할 수 있다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="18" font-size="12" font-weight="bold">i2cget: /dev 노드 + ioctl 경로</text>
  <text x="360" y="18" font-size="12" font-weight="bold">cat sysfs 속성 경로</text>
  <rect x="10" y="28" width="320" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="22" y="53" font-size="13">i2cget -y 0 0x68 0x75</text>
  <rect x="10" y="80" width="320" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="22" y="101" font-size="12">open("/dev/i2c-0")</text>
  <text x="22" y="121" font-size="12">ioctl(I2C_SLAVE) → ioctl(I2C_SMBUS)</text>
  <rect x="360" y="28" width="310" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="372" y="53" font-size="13">cat .../0-0076/name</text>
  <rect x="360" y="80" width="310" height="52" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="372" y="111" font-size="12">open() → read()</text>
  <line x1="10" y1="148" x2="670" y2="148" stroke="currentColor" stroke-dasharray="6 4"/>
  <text x="670" y="142" font-size="12" text-anchor="end">유저 (EL0) 위 · 커널 (EL1) 아래 — syscall = SVC</text>
  <rect x="10" y="162" width="320" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="22" y="187" font-size="12">VFS: chrdev 89 → i2cdev_fops</text>
  <rect x="10" y="222" width="320" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="22" y="247" font-size="12">i2cdev_ioctl() → i2c_smbus_xfer()</text>
  <rect x="10" y="282" width="320" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="22" y="307" font-size="12">adapter-&gt;algo: .smbus_xfer / .xfer</text>
  <rect x="10" y="342" width="320" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="22" y="367" font-size="12">컨트롤러 레지스터 → SCL/SDA 파형</text>
  <rect x="360" y="162" width="310" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="372" y="187" font-size="12">kernfs_fop_read_iter()</text>
  <rect x="360" y="222" width="310" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="372" y="247" font-size="12">sysfs_kf_seq_show() → dev_attr_show()</text>
  <rect x="360" y="282" width="310" height="40" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="372" y="307" font-size="12">드라이버·버스의 show() 콜백</text>
  <rect x="360" y="342" width="310" height="40" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="372" y="367" font-size="12">sysfs_emit() → 유저 버퍼로 복사</text>
  <line x1="170" y1="68" x2="170" y2="78" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="170" y1="132" x2="170" y2="160" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="170" y1="202" x2="170" y2="220" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="170" y1="262" x2="170" y2="280" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="170" y1="322" x2="170" y2="340" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="515" y1="68" x2="515" y2="78" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="515" y1="132" x2="515" y2="160" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="515" y1="202" x2="515" y2="220" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="515" y1="262" x2="515" y2="280" stroke="currentColor" marker-end="url(#ah6)"/>
  <line x1="515" y1="322" x2="515" y2="340" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="10" y="408" font-size="12">초록·주황 칸 = 드라이버가 채우는 자리: file_operations · device_attribute · i2c_algorithm</text>
</svg>
```
그림 6 — 두 가지 대표 경로. 왼쪽: `/dev` 노드 + `ioctl`로 버스 트랜잭션을 직접 내리는 경로. 오른쪽: sysfs 속성 하나를 읽는 경로. 둘 다 SVC로 커널에 들어와 VFS를 거쳐 드라이버 콜백에 도달한다.

명령별로 정리하면:

| 명령 | 읽는 곳 / 쓰는 syscall | 커널 쪽 대응 | 드라이버를 쓸 때 다시 만나는 곳 |
|---|---|---|---|
| `cat /proc/interrupts` | procfs | `show_interrupts()` (kernel/irq/proc.c), `struct irq_desc`의 카운터 | `request_irq()`의 이름이 마지막 열에 찍힌다 (F 모듈) |
| `lsmod` | `/proc/modules` | `struct module` 리스트, refcount | `THIS_MODULE`, `.owner`, `try_module_get()` (E 모듈) |
| `modprobe` / `rmmod` | `finit_module()` / `delete_module()` | 모듈 로더, `module_init`/`module_exit` | 모듈 진입·종료 함수 (E 모듈) |
| `ls /sys/bus/i2c/devices` | sysfs (kernfs) | `struct device`, `kobject` 트리 | `device_register()`, `dev_set_drvdata()` (G 모듈) |
| `echo ... > bind` / `new_device` | sysfs 쓰기 | `driver_attach()` → `really_probe()` → `.probe()` | 내 `probe()`가 리턴하는 errno의 의미 (G 모듈) |
| `udevadm monitor` | netlink `NETLINK_KOBJECT_UEVENT` | `kobject_uevent()` | `MODULE_DEVICE_TABLE`이 만든 `MODALIAS`로 자동 로드 (G 모듈) |
| `dmesg` | `/dev/kmsg` 읽기 | printk 링버퍼 | `dev_err()`/`dev_dbg()`/`dev_err_probe()` 사용 습관 (E·M 모듈) |
| `i2cget` | `/dev/i2c-N`에 `ioctl` | i2c-dev → `i2c_smbus_xfer()` → 어댑터 `.smbus_xfer`/`.xfer` | 클라이언트 드라이버의 `i2c_smbus_read_byte_data()`와 같은 경로 (H 모듈) |
| `gpioget` | `/dev/gpiochipN`에 `GPIO_V2_GET_LINE_IOCTL` | gpiolib character device | 드라이버의 `devm_gpiod_get()`, DT의 `gpio-line-names` (H 모듈) |
| `devmem` | `/dev/mem`을 `mmap` | `devmem_is_allowed()` 검사 | 드라이버는 `devm_ioremap_resource()` + `readl()`/`writel()` (H 모듈) |
| `ip link set can0 ...` | rtnetlink (`RTM_NEWLINK`) | CAN 공통 계층의 changelink, `can_calc_bittiming()` | CAN 드라이버의 `struct can_bittiming_const` (I·Q 모듈) |
| `ss` | netlink `sock_diag` | `inet_diag`, `tcp_diag`·`udp_diag` 모듈 | 소켓 계층 (A9) |
| `strace` | `ptrace()` | 각 syscall 진입·종료 훅 | 내 `file_operations`가 어떤 순서로 불리는지 확인 (A3) |

Don의 언어로 정리하면: `/proc`·`/sys`의 파일 하나는 **커널이 노출한 디버그 레지스터 하나**이고, 그 "레지스터 읽기"는 실제로는 드라이버의 `show()` 함수 호출이다. 그래서 sysfs 속성 읽기가 느리거나(속성 `show()` 안에서 I2C 트랜잭션을 하면) 부작용이 있을 수 있다. `/dev` 노드의 `ioctl`은 **드라이버가 정의한 명령 레지스터**다. 드라이버를 설계할 때 "운영 중 사람이 셸로 볼 상태는 sysfs 속성으로, 고속 데이터는 `/dev` + `read`/`mmap`으로, 개발 중 내부 상태는 debugfs로"라는 원칙이 여기서 나온다.

---

## 15. Embedded 실무에서 다시 보기

### 15.1 장면 1 — "IMU 데이터가 안 와요" (센서 키트, I2C IMU + 인터럽트 핀)

1. `dmesg | grep -i imu` / `journalctl -k -b | grep -i mpu`: probe 메시지가 있나? `invalid whoami`(§5.4)면 배선·주소·칩 변종, `-EPROBE_DEFER` 흔적이면 `devices_deferred`(§5.7).
2. `ls -l /sys/bus/i2c/devices/*/driver`: 바인딩 됐나?
3. `i2cdetect -y -r BUS` (주소 범위 좁혀서): `UU`면 드라이버가 잡은 것, `--`면 버스에서 응답이 없다 → scope로 SCL/SDA, 풀업 저항.
4. `grep imu /proc/interrupts`를 두 번(또는 `irqrate.sh imu`): 카운트가 안 오르면 GPIO 인터럽트 설정(DT의 `interrupts`, 트리거 타입), `gpioinfo`로 그 핀을 누가 잡았는지.
5. 오르는데 데이터가 없다면: 유저 데몬 쪽. `fuser -v /dev/iio:device0`, `strace -p`로 `read`/`poll`이 어디서 막혔는지.

### 15.2 장면 2 — "CAN 모터 컨트롤러가 응답이 없어요"

1. `ip -details -statistics link show can0`: `state UP`? bitrate가 상대(모터 컨트롤러)와 같은가? `can state`와 `bus-errors`, `error-pass`, `bus-off` 카운터.
2. `candump -td -x can0`: 아무것도 안 보이면 수신 자체가 없음 → 트랜시버 STBY 핀(GPIO), 종단 저항.
3. 보내는 것만 있고 상대 응답이 없으면 `cansend`로 수동 프레임을 쏴 보고, `bus-errors`가 오르면 ACK 부재 = 물리 계층.
4. BUS-OFF 이후 자동 복구가 필요하면 `restart-ms 100`(드라이버가 지원할 때, §10.4).

### 15.3 장면 3 — "가끔 카메라 프레임이 끊겨요"

1. `top -H`/`pidstat -t -p PID 1`: 캡처 스레드가 CPU를 다 쓰나, `%wait`(runqueue 대기)가 큰가?
2. `vmstat 1`: `b`와 `wa`가 오르면 저장 I/O(녹화 eMMC)가 원인 → `iostat -x`로 `w_await`, `aqu-sz`(§9.3).
3. `/proc/interrupts`에서 CSI·ISP IRQ가 한 CPU에 몰렸는지 → affinity 조정(N 모듈).
4. `journalctl -k -f`를 띄워 두고 재현: 드라이버의 `buffer overrun` 같은 메시지 확인. `/dev/kmsg`에 "재현 시작" 마커를 써 두면 로그 읽기가 쉬워진다(§6.5).

### 15.4 장면 4 — 현장 장비 원격 지원

고객사 장비에 들어갈 수 있는 시간은 짧다. `debug_bundle.sh`(§13)를 rootfs에 미리 넣어 두고 "증상이 나면 이거 실행해서 tar.gz 보내 주세요"라고 하는 것이 실무에서 가장 효과가 큰 셸 스크립트다. 보드가 BusyBox라면 `#!/bin/sh`로 바꾸고 `command -v`로 없는 도구를 건너뛰게 한 지금 구조가 그대로 통한다.

---

## 16. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `sudo echo 1 > /sys/...` | `Permission denied` | 리다이렉션은 sudo 전에 일반 사용자 셸이 연다 | `echo 1 ` 다음에 파이프로 `sudo tee 파일`, 또는 `sudo sh -c "..."` |
| `cmd 2>&1 > log` | 에러가 로그에 없다 | 리다이렉션은 왼쪽부터 적용 | `cmd > log 2>&1` |
| 파이프 중간 실패를 놓침 | 스크립트가 "성공"으로 끝남 | 파이프라인 rc = 마지막 명령 rc | `set -o pipefail`, `PIPESTATUS` |
| 따옴표 없는 `$var` | 공백 있는 경로에서 이상한 에러 | word splitting, glob | 항상 `"$var"` |
| bitrate 없이 `ip link set can0 up` | `RTNETLINK answers: Invalid argument` | bit timing 미설정 | `down → type can bitrate N → up` |
| up 상태에서 bitrate 변경 | `Device or resource busy` | 동작 중 타이밍 변경 금지 | 먼저 `down` |
| v1 문법으로 `gpioget gpiochip2 3` | `cannot find line 'gpiochip2'` | libgpiod v2는 `-c`로 칩 지정 | `gpioget -c gpiochip2 3` 또는 줄 이름 |
| `gpioset -t0` 후 값이 유지된다고 가정 | 리셋 핀이 다시 떨어짐 | 줄을 놓으면 값 보장 없음 | `gpioset`을 살려 두거나 드라이버·`gpio-hog` |
| 모르는 버스에 `i2cdetect -y` 남발 | 칩 상태 변경, 버스 hang | quick write·read에 반응하는 칩 | `-r`, 주소 범위 지정, 회로도 먼저 |
| `dmesg`만 보고 probe 성공 판단 | probe 실패인데 에러 로그 없음 | `-ENODEV`/`-ENXIO`는 driver core가 조용히 넘김 | `driver` 링크 확인, dynamic debug로 `dd.c` 켜기 |
| `rmmod`가 `in use` | 모듈을 못 내림 | 누가 장치를 열어 refcount 보유 | `fuser -v /dev/X`, `lsof`로 찾아 종료 |
| PC용 bash 스크립트를 보드에서 실행 | `not found`, 문법 에러 | BusyBox `ash`, `/bin/bash` 없음 | `#!/bin/sh` + POSIX 문법, `busybox --list` 확인 |

---

## 17. 면접에서 이렇게 말한다

**Q.** "Your driver module loads but the device doesn't work. How do you debug it?"

**A.** load와 probe를 구분한다. `lsmod`로 load를 확인하고, `/sys/bus/<bus>/devices/<dev>/driver` 링크로 바인딩을 확인한다. 없으면 `dmesg`에서 드라이버의 마지막 메시지와 errno를 보고, `-EPROBE_DEFER`면 `devices_deferred`, 메시지가 없으면 dynamic debug로 `drivers/base/dd.c`를 켜고 `bind`에 다시 써서 probe를 재시도한다. 바인딩은 됐는데 동작이 없으면 `/proc/interrupts` 카운터와 버스 도구(`i2cdetect`, scope)로 하드웨어 쪽을 본다.

> I separate "loaded" from "probed". lsmod tells me the module is in; the driver symlink under /sys/bus/.../devices tells me it actually bound. If it didn't bind, I read the last driver message and errno in dmesg; for -EPROBE_DEFER I check devices_deferred in debugfs, and if probe fails silently with -ENODEV I turn on dynamic debug for drivers/base/dd.c and re-trigger with the bind file. If it's bound but dead, I watch /proc/interrupts and go to the bus with i2c-tools or a scope.

**꼬리 질문**: "Why might probe fail without any error message?" → `-ENODEV`/`-ENXIO`는 "내 장치가 아님"으로 해석되어 driver core가 디버그 레벨로만 남긴다.

**Q.** "rmmod says the module is in use. What do you do?"

**A.** `lsmod`의 `Used by` 카운트를 보고, 그 모듈이 만든 장치 노드를 누가 열었는지 `fuser -v`나 `lsof`로 찾는다. 다른 모듈 의존이면 그 모듈부터 내린다. 열린 fd가 자식에게 상속돼 있을 수 있으니 자식 프로세스까지 본다.

> lsmod's "Used by" is the refcount. Either another module depends on it, or a process holds a file it backs. I use fuser -v or lsof on the device node to find the holder — remembering that forked children inherit the fd unless O_CLOEXEC was used — stop it, then rmmod.

**Q.** "What's the difference between dmesg and journalctl?"

**A.** `dmesg`는 커널 printk 링버퍼를 `/dev/kmsg`로 직접 읽는다 — RAM에 있고 크기가 정해져 있어 오래된 것은 덮어쓰인다. `journalctl`은 journald가 커널 로그와 서비스 로그를 함께 모아 색인한 저장소를 질의한다. 부팅별(`-b`), 유닛별(`-u`), 우선순위별(`-p`) 필터가 되고, persistent로 설정하면 이전 부팅 로그도 남는다.

> dmesg reads the kernel's printk ring buffer through /dev/kmsg — volatile and size-limited. journalctl queries journald's indexed store, which contains kernel messages plus service output, filterable by boot, unit and priority, and persistent across reboots if storage is configured that way.

**Q.** "The system feels slow. What are your first commands?"

**A.** `uptime`/`top`으로 load와 CPU 분해(us·sy·wa·si)를 보고, `vmstat 1`로 `r`(CPU 대기), `b`·`wa`(I/O 대기), `in`·`cs`를 본다. I/O면 `iostat -x`의 await·큐 깊이, CPU면 `pidstat -t`로 스레드 단위, 인터럽트가 의심되면 `/proc/interrupts`를 두 번 읽어 비율을 낸다. 그다음에 perf·ftrace로 내려간다.

> I start broad: top for the us/sy/wa/si split, then vmstat 1 — r tells me CPU contention, b and wa tell me I/O waiting, in and cs show interrupt and switch rates. Then I narrow: iostat -x for await and queue depth, pidstat -t per thread, and /proc/interrupts sampled twice for IRQ rates. Only then do I reach for perf or ftrace.

**Q.** "How do you bring up a CAN interface on Linux?"

**A.** CAN은 SocketCAN 네트워크 인터페이스다. `ip link set can0 type can bitrate 500000`(필요하면 `sample-point`, CAN FD면 `dbitrate`·`fd on`)으로 타이밍을 정하고 `up` 한다. 동작 중에는 타이밍을 못 바꾸므로 먼저 down. 확인은 `ip -details -statistics link`의 state·에러 카운터와 `candump`/`cansend`.

> CAN is a SocketCAN netdev, so it's configured with ip: set the bitrate — and sample point or data bitrate for CAN FD — while the link is down, then bring it up. I verify with ip -details -statistics for the bit timing, controller state and error counters, and exercise it with candump and cansend.

**Q.** "Explain `2>&1` and why order matters. What does `set -euo pipefail` do?"

**A.** `2>&1`은 fd 2를 지금 fd 1이 가리키는 곳으로 복제(`dup2`)한다. 셸은 리다이렉션을 왼쪽부터 처리하니 `> file 2>&1`이어야 둘 다 파일로 간다. strict mode는 실패 시 종료(`-e`), 미정의 변수 에러(`-u`), 파이프 중간 실패 전파(`pipefail`)다. `-e`가 조건문 안에서는 안 먹는다는 함정도 안다.

> 2>&1 duplicates fd 2 onto whatever fd 1 points to at that moment; redirections apply left to right, so it has to come after "> file". set -euo pipefail makes the script exit on errors, treat unset variables as errors, and fail a pipeline if any stage fails — with the caveat that -e is suppressed in if conditions and && / || chains.

**Q.** "How do you check what's on an I2C bus, and what are the risks?"

**A.** `i2cdetect -l`로 버스, `i2cdetect -y N`으로 스캔한다. `UU`는 드라이버가 잡은 주소다. 하지만 스캔은 quick write나 read를 실제로 보내므로 일부 칩의 상태를 바꾸거나 버스를 멈출 수 있다. 회로도로 주소를 먼저 알고, 필요하면 `-r`로 범위를 좁힌다. `i2cset`은 드라이버를 우회하므로 PMIC 같은 버스에서는 특히 위험하다.

> i2cdetect -l lists adapters and i2cdetect -y N scans; UU means a kernel driver owns that address. Scanning isn't passive — it issues quick writes or reads that can change some chips' state or hang the bus — so I check the schematic first and limit the range. i2cset and i2cdump bypass the driver entirely; on a PMIC bus a wrong write can take the board down.

**Q.** "What's the difference between /proc, /sys and debugfs?"

**A.** `/proc`는 프로세스 정보와 오래된 커널 상태 파일, `/sys`는 장치 모델(kobject) 트리로 장치·드라이버·속성을 하나에 하나씩 보여 주는 안정 ABI다. debugfs는 드라이버 개발자가 자유롭게 내놓는 디버그 정보로 ABI 보장이 없어서 제품 코드가 의존하면 안 된다.

> /proc holds per-process data and legacy kernel status files. /sys mirrors the device model — buses, devices, drivers, one value per attribute — and is a stable ABI. debugfs is a free-form debugging interface with no ABI guarantees, so production code shouldn't depend on it.

---

## 18. 직접 해보기

1. (개념) `cmd > out.txt 2>&1`과 `cmd 2>&1 > out.txt`가 각각 fd 1·2를 어디로 보내는지 `dup2` 호출 순서로 설명하라.
정답/힌트: 앞은 `open→dup2(fd,1)` 후 `dup2(1,2)`, 뒤는 `dup2(1,2)`(터미널) 후 `open→dup2(fd,1)`.

2. (셸) `/sys/class/net/*`를 돌며 인터페이스별 `rx_packets`, `rx_dropped`, `carrier`를 표로 찍는 한 줄짜리 루프를 써라. carrier 파일 읽기가 실패하는 인터페이스(down 상태)는 어떻게 처리할까?
정답/힌트: down이면 `cat carrier`가 `Invalid argument`로 실패한다 → `2>/dev/null || echo -`.

3. (관찰) `irqrate.sh`를 `arch_timer`로 돌려 초당 타이머 인터럽트 수를 재고, CPU 수로 나눠 CPU당 값을 구하라. idle일 때와 `yes > /dev/null`을 돌릴 때 비교하라.
정답/힌트: NO_HZ(tickless) 커널은 idle CPU의 tick을 멈추므로 부하에 따라 값이 바뀐다. `CONFIG_HZ`와 비교.

4. (드라이버 관찰) i2c-stub에 칩을 만들고 bmp280을 `new_device`로 붙인 뒤, `udevadm monitor --kernel --property`를 띄워 놓고 `unbind`/`bind`를 해 보라. `bind`·`unbind` 이벤트의 `DRIVER=` 값을 확인하라.
정답/힌트: 장치 추가는 `add`, 드라이버 바인딩은 별도의 `bind` uevent로 온다.

5. (strace) `strace -e trace=openat,ioctl gpioget -c gpiochip0 0`을 돌려 `/dev/gpiochip0`에 어떤 ioctl이 가는지 보라.
정답/힌트: 칩 정보 조회 후 `GPIO_V2_GET_LINE_IOCTL`로 줄 요청 fd를 받고 그 fd에 값 읽기 ioctl.

6. (BusyBox) `debug_bundle.sh`를 `#!/bin/sh`로 바꾸고 `busybox sh`로 실행해 보라. 무엇이 깨지는가?
정답/힌트: 이 VM의 BusyBox 1.37 `ash`에서는 `set -o pipefail`과 `local`이 모두 동작해서 이 스크립트는 그대로 돌아갔다(직접 확인, 40K 번들 생성). 깨지는 것은 bash 전용 문법이다: 배열, `[[ ]]`, `PIPESTATUS`, `<(...)`. 오래된 BusyBox는 `pipefail`이 없을 수 있으니 보드의 버전에서 확인한다.

7. (CAN) `dummy_can`의 `can0`에 `bitrate 1000000 sample-point 0.8`을 주고 `ip -details`의 `tq`, `prop-seg`, `phase-seg1/2`, `brp`를 받아 적어 bit time이 1 µs가 되는지 검산하라.
정답/힌트: bit time = (1 + prop + ps1 + ps2) × tq.

---

## 19. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| exit code | 종료 상태 | 프로세스가 끝날 때 남기는 0~255 값, 0이 성공 |
| pipeline | 파이프라인 | 앞 명령의 stdout을 뒤 명령의 stdin으로 잇는 프로세스 사슬 |
| pipefail | 파이프 실패 전파 | 파이프 중 하나라도 실패하면 전체를 실패로 본다 |
| here-doc | 인라인 입력 | `<<EOF`로 여러 줄을 stdin으로 넣는 문법 |
| procfs | `/proc` | 프로세스·커널 상태를 텍스트 파일로 보여 주는 가상 FS |
| sysfs | `/sys` | 장치 모델(kobject) 트리를 파일로 보여 주는 가상 FS |
| debugfs | `/sys/kernel/debug` | 드라이버 디버그용 가상 FS, ABI 보장 없음 |
| uevent | 커널 이벤트 | 장치 추가·제거·바인딩을 유저 공간(udev)에 알리는 netlink 메시지 |
| modalias | 장치 별명 | `i2c:bmp280` 같은 문자열, 모듈 자동 로드의 키 |
| probe | 장치 초기화 | 드라이버가 특정 장치를 맡아 초기화하는 콜백 |
| bind / unbind | 바인딩 | 장치와 드라이버를 연결·해제하는 sysfs 조작 |
| EPROBE_DEFER | probe 연기 | 필요한 자원이 아직 없어 나중에 다시 probe (-517) |
| dynamic debug | 동적 디버그 | `pr_debug`·`dev_dbg`를 런타임에 켜고 끄는 기능 |
| printk ring buffer | 커널 로그 버퍼 | 커널 메시지가 쌓이는 고정 크기 원형 버퍼 |
| console_loglevel | 콘솔 레벨 | 이 값보다 작은 레벨의 메시지만 콘솔(UART)에 출력 |
| journald | 로그 데몬 | 커널·서비스 로그를 모아 색인 저장하는 systemd 구성 요소 |
| SMBus | 시스템 관리 버스 | I2C 위의 정형화된 트랜잭션 규격 (read byte 등) |
| libgpiod | GPIO 유저 라이브러리 | `/dev/gpiochipN` 문자 장치를 쓰는 라이브러리·도구 |
| SocketCAN | 리눅스 CAN 스택 | CAN 컨트롤러를 네트워크 인터페이스·소켓으로 다루는 구조 |
| NAPI | 인터럽트 완화 | 수신 IRQ 후 폴링으로 전환해 여러 패킷을 한 번에 처리 |
| BusyBox | 멀티콜 바이너리 | 하나의 실행 파일로 수백 개의 유닉스 명령을 제공 |
| STRICT_DEVMEM | /dev/mem 제한 | RAM 등 허용되지 않은 물리 주소의 `/dev/mem` 접근 차단 |

---

## 20. 요약 & 체크리스트

보드 앞에서 셸은 측정 장비다. 정보는 `/proc`(상태), `/sys`(장치 모델), `/dev`(I/O), debugfs(드라이버 내부), 로그(시간 순 기록) 다섯 곳에 있고, `lsmod`·`lspci`·`dmesg`·`ss`는 그 파일·소켓을 예쁘게 찍는 프로그램일 뿐이다. 문제는 "어떤 HW가 있나 → 드라이버가 load·probe 됐나 → 누가 장치를 쓰나 → 왜 느린가 → 커널이 뭐라 했나 → 네트워크·CAN이 사나"의 질문으로 쪼개서 좁힌다. load와 probe를 구분하고(`driver` 링크, dynamic debug), `i2c-tools`·`gpioset`·`devmem`은 드라이버를 우회하는 위험한 도구임을 기억한다. 스크립트는 `set -euo pipefail`·따옴표·`2>&1` 순서를 지키고, 보드용은 BusyBox `sh`로 돌아가게 쓴다.

- [ ] `> f 2>&1`과 `2>&1 > f`의 차이를 설명할 수 있다
- [ ] `set -euo pipefail`의 각 옵션과 `-e`의 예외를 설명할 수 있다
- [ ] `sort`, `uniq -c`, `awk`로 로그·`/proc` 파일에서 "무엇이 몇 번"을 뽑을 수 있다
- [ ] `/proc`, `/sys`, `/dev`, debugfs의 역할과 ABI 안정성 차이를 설명할 수 있다
- [ ] 드라이버의 load와 probe를 구분해 확인할 수 있다 (`lsmod`, `driver` 링크, `bind`)
- [ ] probe 실패를 dmesg·dynamic debug·`devices_deferred`로 추적할 수 있다
- [ ] `fuser`·`lsof`·`/proc/PID/stack`·`strace -p`로 장치를 잡은 프로세스와 그 상태를 찾을 수 있다
- [ ] `vmstat`·`iostat -x`·`pidstat`·`/proc/interrupts`로 CPU·I/O·IRQ 병목을 구분할 수 있다
- [ ] `ip link set can0 type can bitrate ...` 순서와 흔한 에러 두 가지를 말할 수 있다
- [ ] `i2cdetect`·`i2cset`·`devmem`의 위험을 구체적으로 설명할 수 있다
- [ ] libgpiod v1/v2 문법 차이를 알고 줄 이름으로 GPIO를 다룰 수 있다
- [ ] BusyBox 보드에서 빠진 옵션을 대체하는 방법과 `#!/bin/sh` 원칙을 설명할 수 있다

---

## 21. 한 장 치트 시트 — 질문별

파이프가 필요한 조합은 표 아래 코드 블록에 따로 적었다.

| 질문 | 먼저 칠 명령 | 더 깊이 |
|---|---|---|
| 어떤 HW가 있나 | `lspci -k`, `lsusb -t`, `lsblk`, `i2cdetect -l`, `gpiodetect` | `sudo cat /proc/iomem`, `ls /sys/bus/*/devices`, `udevadm info -a /dev/X` |
| 드라이버가 load 됐나 | `lsmod`, `modinfo NAME` | `modprobe -n -v NAME`, `modprobe -c` (alias), `/sys/module/NAME/parameters/` |
| probe 됐나 | `ls -l /sys/bus/BUS/devices/DEV/driver` | `cat /sys/kernel/debug/devices_deferred`, `echo "file drivers/base/dd.c +p"` → dynamic_debug/control |
| probe 다시 | `echo DEV` → `/sys/bus/BUS/drivers/DRV/unbind`, `bind` | `new_device`, `delete_device` (i2c) |
| 커널이 뭐라 했나 | `sudo dmesg -T`, `sudo dmesg -W` | `journalctl -k -b`, `journalctl -k -f`, `--level=err,warn`, `/proc/sys/kernel/printk` |
| 서비스 로그 | `journalctl -u UNIT -b` | `journalctl -t TAG`, `-p err`, `--since`, `logger -t TAG` |
| 핫플러그 | `udevadm monitor --kernel --udev` | `--property`, `--subsystem-match=` |
| 누가 장치를 쓰나 | `sudo fuser -v /dev/X`, `sudo lsof /dev/X` | `ls -l /proc/PID/fd`, `cat /proc/PID/stack`, `strace -f -p PID` |
| 왜 rmmod 안 되나 | `lsmod` (Used by) | `fuser -v` 후 프로세스 종료 |
| CPU | `top -b -n1`, `pidstat 1` | `ps -eLf`, `pidstat -t -p PID 1`, `top -H` |
| 메모리 | `free -h` (available) | `vmstat 1`, `/proc/meminfo`, `/proc/PID/status` |
| I/O | `vmstat 1` (b, wa) | `iostat -dx 1` (w_await, aqu-sz, %util) |
| 인터럽트 | `cat /proc/interrupts` 두 번 | `irqrate.sh NAME`, `/proc/irq/N/smp_affinity_list` |
| 네트워크 | `ip -br a`, `ip -s link`, `ip route get IP` | `ss -tulpn`, `ping -c`, `tcpdump -i IF -nn -w f.pcap` |
| CAN | `ip -details -statistics link show can0` | `ip link set can0 type can bitrate N` 후 `up`, `candump -td can0`, `cansend can0 123#DEADBEEF` |
| I2C 직접 | `i2cdetect -y -r BUS` | `i2cget`, `i2cset`, `i2cdump -r` (위험 주의) |
| GPIO 직접 | `gpioinfo`, `gpioget NAME` | `gpioset NAME=1` (v2는 유지형), `cat /sys/kernel/debug/gpio` |
| 레지스터 직접 | `busybox devmem ADDR 32` | `devmem2 ADDR w`, `STRICT_DEVMEM` 확인 |
| 보드 접속 | `ssh board`, `picocom -b 115200 /dev/ttyUSB0` | `rsync -az dir/ board:/path/`, `scp` |
| BusyBox 확인 | `busybox --list` | `ls /proc/PID/task`, `cat /proc/net/tcp` |
| 한 번에 모으기 | `sudo debug_bundle.sh /tmp` | §13 스크립트를 보드에 맞게 확장 |

파이프가 들어가는 자주 쓰는 조합:

```sh
sudo dmesg | grep -iE "fail|error|timeout|defer"            # 에러 훑기
sudo dmesg | sed -E "s/^\[[^]]*\] //" | cut -d: -f1 | sort | uniq -c | sort -rn | head   # 누가 시끄러운가
ps -eo stat= | cut -c1 | sort | uniq -c                      # 프로세스 상태 분포
awk 'NR>1 {s=0; for(i=2;i<=5;i++) s+=$i; print s, $NF}' /proc/interrupts | sort -rn | head   # IRQ 상위 (CPU 4개 기준)
ssh board "sudo dmesg" | grep -i imu                         # 원격 실행, 로컬 필터
sudo strace -c CMD 2>&1 >/dev/null | head                    # syscall 통계만
echo VALUE | sudo tee /sys/path/attr                          # root로 sysfs 쓰기
```

---

## 참고 자료

- man7.org: [bash(1)](https://man7.org/linux/man-pages/man1/bash.1.html), [dmesg(1)](https://man7.org/linux/man-pages/man1/dmesg.1.html), [journalctl(1)](https://man7.org/linux/man-pages/man1/journalctl.1.html), [proc(5)](https://man7.org/linux/man-pages/man5/proc.5.html), [sysfs(5)](https://man7.org/linux/man-pages/man5/sysfs.5.html), [ip-link(8)](https://man7.org/linux/man-pages/man8/ip-link.8.html), [ss(8)](https://man7.org/linux/man-pages/man8/ss.8.html), [strace(1)](https://man7.org/linux/man-pages/man1/strace.1.html), [vmstat(8)](https://man7.org/linux/man-pages/man8/vmstat.8.html), [udevadm(8)](https://man7.org/linux/man-pages/man8/udevadm.8.html)
- docs.kernel.org: [Implementing I2C device drivers in userspace (i2c-dev)](https://docs.kernel.org/i2c/dev-interface.html), [I2C/SMBus fault codes](https://docs.kernel.org/i2c/fault-codes.html), [Dynamic debug](https://docs.kernel.org/admin-guide/dynamic-debug-howto.html), [SocketCAN](https://docs.kernel.org/networking/can.html), [GPIO character device userspace API](https://docs.kernel.org/userspace-api/gpio/chardev.html), [gpio-sim](https://docs.kernel.org/admin-guide/gpio/gpio-sim.html), [Driver model: binding](https://docs.kernel.org/driver-api/driver-model/binding.html)
- i2c-tools man 페이지 (i2cdetect(8)의 경고 절): 패키지 `i2c-tools`, `man i2cdetect`
- libgpiod: [git.kernel.org libgpiod](https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/)
- can-utils: [github.com/linux-can/can-utils](https://github.com/linux-can/can-utils)
- Michael Kerrisk, "The Linux Programming Interface" — 5장(파일 I/O 심화: `dup2`), 44장(파이프), 12장(시스템·프로세스 정보, `/proc`)
- Bootlin, "Embedded Linux system development" 교육 슬라이드 (bootlin.com/training/embedded-linux)
- 커널 소스(elixir.bootlin.com): `drivers/i2c/i2c-dev.c`, `drivers/i2c/i2c-stub.c`, `drivers/base/dd.c`(`really_probe`), `kernel/irq/proc.c`(`show_interrupts`), `drivers/net/can/dev/netlink.c`
