---
id: ELX-B13
time: 70분
updated: 2026-11-15
---
# ELX-B13 systemd 유닛 작성: 의존성, 재시작, watchdog

## 학습 목표

- `NOTIFY_SOCKET` 프로토콜(AF_UNIX 데이터그램 한 개)을 libsystemd 없이 C로 직접 구현하고, 경로 소켓과 추상(abstract, `@`) 소켓의 주소 길이 차이를 설명한다.
- `WATCHDOG_USEC` / `WATCHDOG_PID` 를 읽어 **"일이 진행됐을 때만" 절반 주기로** `WATCHDOG=1` 을 보내는 데몬을 만든다.
- `Wants=` + `After=`, `Type=notify`, `Restart=on-failure`, `WatchdogSec=5`, `StartLimitBurst=` 가 들어간 유닛을 쓰고, 각 설정이 실제로 무엇을 하는지 `systemctl show` 와 `journalctl` 의 **실제 줄**로 확인한다.
- 데몬이 멈추면(hang) systemd가 SIGABRT로 죽이고 재시작하는 흐름, 계속 죽으면 시작 제한(start limit)에서 멈추는 흐름을 시간 순으로 읽는다.
- 면접에서 "systemd watchdog vs HW watchdog", "After vs Wants vs Requires", "Type=simple 이면 왜 순서가 깨지나"를 근거를 들어 답한다.

## 1. 문제

장비(로봇 컴패니언 컴퓨터, 카메라, Jetson 같은 리눅스 보드)에서 센서 데몬이 **멈췄을 때 누가 알아채고 살리느냐**는 bring-up과 필드 신뢰성의 핵심이고, 임베디드 리눅스 면접 단골 질문이다. 펌웨어에서 메인 루프가 IWDG(독립 watchdog)를 kick하던 것과 같은 일을, 리눅스 사용자공간에서는 PID 1(systemd)이 서비스 단위로 해 준다.

> 환경: **Lima VM `elinux` 의 진짜 systemd** — Ubuntu 26.04.1 LTS, aarch64, Linux 7.0.0-38-generic, `systemd 259 (259.5-0ubuntu3.4)`. QEMU 게스트가 아니다. 유닛 파일은 **`/run/systemd/system/esn-b13-*.service`(런타임 전용, 재부팅하면 사라짐)** 에만 쓰고, `check.sh` 가 끝날 때 trap으로 stop + 삭제 + `daemon-reload` 한다. 다른 유닛은 건드리지 않는다. 시간 수치는 VM 안의 측정값이다(실제 보드가 아님).
> libsystemd: 런타임 라이브러리(`libsystemd.so.0`)는 있지만 **헤더(`/usr/include/systemd/`)와 pkg-config 파일이 없다** → `sd_notify(3)` 를 쓸 수 없어서 프로토콜을 직접 구현한다. 사실 그게 이 레슨의 목적이기도 하다.

등장인물은 셋이다.

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">PID 1 ↔ esn_sensord: 환경 변수로 주고, 데이터그램으로 받고, 시그널로 끝낸다</text>
<rect x="10" y="40" width="250" height="230" class="f-acc" stroke="currentColor"/>
<text x="20" y="62" fill="currentColor" font-weight="bold">PID 1 (systemd 259)</text>
<text x="20" y="85" fill="currentColor" font-size="12">유닛 그래프: Wants= / After=</text>
<text x="20" y="105" fill="currentColor" font-size="12">TimeoutStartSec=4 (READY 기다림)</text>
<text x="20" y="125" fill="currentColor" font-size="12">WatchdogSec=5 (ping 기다림)</text>
<text x="20" y="145" fill="currentColor" font-size="12">Restart=on-failure, RestartSec=1</text>
<text x="20" y="165" fill="currentColor" font-size="12">StartLimitBurst=4 / 60 s</text>
<text x="20" y="190" fill="currentColor" font-size="12" font-family="monospace">/run/systemd/notify</text>
<text x="20" y="208" fill="currentColor" font-size="12">(SOCK_DGRAM, SO_PASSCRED)</text>
<text x="20" y="235" fill="currentColor" font-size="12">NotifyAccess=main:</text>
<text x="20" y="253" fill="currentColor" font-size="12">MainPID가 보낸 것만 받음</text>
<rect x="480" y="40" width="250" height="230" class="f-ok" stroke="currentColor"/>
<text x="490" y="62" fill="currentColor" font-weight="bold">esn_sensord (MainPID)</text>
<text x="490" y="85" fill="currentColor" font-size="12">센서 열기 → 보정(300 ms)</text>
<text x="490" y="105" fill="currentColor" font-size="12">메인 루프 100 ms: 샘플 1개</text>
<text x="490" y="125" fill="currentColor" font-size="12">진행했으면 2.5 s마다 ping</text>
<text x="490" y="145" fill="currentColor" font-size="12">hang 플래그 → 루프 멈춤</text>
<text x="490" y="165" fill="currentColor" font-size="12">SIGTERM → STOPPING=1, exit 0</text>
<text x="490" y="190" fill="currentColor" font-size="12">센서 없음 → ERRNO=19, exit 3</text>
<text x="490" y="235" fill="currentColor" font-size="12">stdout = journald 스트림 소켓</text>
<text x="490" y="253" fill="currentColor" font-size="12">(printf 한 줄 = journal 한 줄)</text>
<line x1="260" y1="80" x2="480" y2="80" stroke="currentColor"/>
<polygon points="480,80 470,75 470,85" fill="currentColor"/>
<text x="270" y="74" fill="currentColor" font-size="12">exec 시 env: NOTIFY_SOCKET,</text>
<text x="270" y="96" fill="currentColor" font-size="12">WATCHDOG_USEC=5000000, WATCHDOG_PID</text>
<line x1="480" y1="140" x2="260" y2="140" stroke="currentColor"/>
<polygon points="260,140 270,135 270,145" fill="currentColor"/>
<text x="275" y="125" fill="currentColor" font-size="12" font-family="monospace">READY=1 / STATUS=...</text>
<text x="275" y="160" fill="currentColor" font-size="12" font-family="monospace">WATCHDOG=1 / STOPPING=1</text>
<text x="275" y="178" fill="currentColor" font-size="12" font-family="monospace">ERRNO=19</text>
<line x1="260" y1="215" x2="480" y2="215" stroke="currentColor"/>
<polygon points="480,215 470,210 470,220" fill="currentColor"/>
<text x="270" y="207" fill="currentColor" font-size="12">stop → SIGTERM</text>
<text x="270" y="235" fill="currentColor" font-size="12">ping 끊김 5 s → SIGABRT</text>
<rect x="10" y="290" width="250" height="40" class="f-mut" stroke="currentColor"/>
<text x="20" y="315" fill="currentColor" font-size="12">esn-b13-dep (oneshot, RemainAfterExit)</text>
<text x="280" y="315" fill="currentColor" font-size="12">Wants= 로 끌려오고, After= 로 먼저 끝난다 ("센서 전원 레일 ON" 역할)</text>
</svg>
```

### Part A — sd_notify를 손으로 (`notify_addr`, `notify_send`)

- A1. `notify_addr(env, &sa, &len)`: `NOTIFY_SOCKET` 값을 `struct sockaddr_un` 으로 바꾼다. `/abs/path` → 파일시스템 소켓(길이에 NUL 포함), `@name` → 추상 소켓(`sun_path[0] = '\0'`, 길이에 NUL 없음). `NULL`, `""`, `"@"`, 상대 경로는 `-EINVAL`, `sun_path`(108바이트)에 안 들어가면 `-ENAMETOOLONG`.
- A2. `notify_send(msg)`: `NOTIFY_SOCKET` 이 없으면 0(systemd 밖에서 돌아도 그냥 동작), 있으면 `socket(AF_UNIX, SOCK_DGRAM)` + `sendto(MSG_NOSIGNAL)` 로 **데이터그램 한 개**를 보내고 1, 실패하면 `-errno`.

### Part B — watchdog 주기와 "진행했을 때만" ping

- B1. `watchdog_period_usec()`: `WATCHDOG_USEC` 를 엄격하게 파싱(숫자 아닌 글자, 0, 음수 → 0 = 끔), `WATCHDOG_PID` 가 있고 내 pid가 아니면 0, 아니면 **절반**을 돌려준다.
- B2. `pet_due(now, last_pet, period, samples, samples_at_pet)`: 주기가 지났고 **그 사이 샘플이 하나라도 늘었을 때만** 1.

### Part C — 데몬과 유닛, 그리고 진짜 systemd로 검증

- C1–C4. `run_daemon()` 안의 네 군데: 센서 없음 → `STATUS=sensor missing\nERRNO=19` 후 exit 3, 보정 끝 → `READY=1`, 루프 → `WATCHDOG=1` + 상태, SIGTERM → `STOPPING=1` 후 exit 0.
- 유닛 파일(`esn-b13-sensord.service`, `esn-b13-dep.service`)은 이미 주어진다. `check.sh` 가 다음을 **실제 systemd** 에서 확인한다.

| 단계 | 하는 일 | PASS 조건 (전부 횟수·정확한 값·journal 줄) |
|---|---|---|
| selftest | 내가 PID 1 역할: 추상 소켓 + `SO_PASSCRED` 로 받기 | 44/44 (주소, 송신, 자격증명 pid, 주기, READY→ping×3→STOPPING 순서, hang, ERRNO) |
| start | `systemctl start` (READY=1까지 막힘) | rc 0, active/running, dep도 active(Wants), dep가 먼저(After), env 3개 |
| healthy | 6.5 s 대기 (> WatchdogSec) | 같은 MainPID, NRestarts=0, `pets=2` |
| hang | `touch hang` | `Watchdog timeout (limit 5s)!` → SIGABRT → `restart counter is at 1`, 새 MainPID |
| stop | `systemctl stop` | 데몬이 SIGTERM 처리 후 exit 0, `Deactivated successfully.`, 재시작 없음 |
| start limit | 센서 파일 치움 | exit 3 반복 → NRestarts=4, `Start request repeated too quickly.`, 수동 start도 거부, `reset-failed` 후 복구 |

## 2. Problem (English)

> Write a `Type=notify` sensor daemon for systemd **without libsystemd**. Implement:
>
> ```c
> int notify_addr(const char *env, struct sockaddr_un *sa, socklen_t *len); /* "/path" or "@abstract" */
> int notify_send(const char *msg);       /* 1 sent, 0 no $NOTIFY_SOCKET, <0 -errno; one datagram */
> uint64_t watchdog_period_usec(void);    /* WATCHDOG_USEC/2, 0 if unset/garbage/other WATCHDOG_PID */
> int pet_due(uint64_t now, uint64_t last, uint64_t period, uint64_t samples, uint64_t samples_at_pet);
> ```
>
> The daemon sends `READY=1` after calibration, `WATCHDOG=1` every `WatchdogSec/2` **only if the main loop made progress**, `STOPPING=1` on SIGTERM, and `STATUS=`/`ERRNO=19` before exiting 3 when the sensor is missing.
>
> Constraints: abstract addresses must use the exact length (no trailing NUL); never die from SIGPIPE; a forked child must not pet (honour `WATCHDOG_PID`).
>
> Unit: `Wants=`+`After=` a oneshot dependency, `Restart=on-failure`, `RestartSec=1`, `WatchdogSec=5`, `TimeoutStartSec=4`, `StartLimitIntervalSec=60`, `StartLimitBurst=4`. Prove with `systemctl show -p NRestarts,Result,ActiveState,MainPID` and `journalctl -u` that a hung daemon is killed with SIGABRT (`Watchdog timeout`) and restarted, and that a crash loop stops at the start limit.
>
> Example (`check.sh solution`): `after 6.5 s: MainPID=632917 NRestarts=0 StatusText=temp=23500 mC samples=50 pets=2` … `30/30 passed`.

## 3. 시작 코드

파일: 데몬 `starter.c`(→ `solution.c`, `practice.c`), 유닛 템플릿 2개(`@W@`=작업 디렉터리, `@TAG@`=`<variant>-<pid>` 를 `check.sh` 가 치환), 검사 스크립트 `check.sh`. 모든 명령은 **Mac에서 한 줄**로 친다.

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B13/check.sh starter
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B13/check.sh solution
limactl shell elinux -- bash -lc 'SHOWJ=1 bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B13/check.sh solution'
```

세 번째 줄은 끝에 이번 실행의 journal(`-o short-monotonic`)까지 찍는다. 데몬만 따로 시험하려면(VM 안, systemd 없이): `gcc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/ELX-B13_d && /tmp/ELX-B13_d --selftest`.

<!-- code: esn-b13-sensord.service -->

<!-- code: esn-b13-dep.service -->

<!-- code: starter.c -->

<!-- code: check.sh -->

starter를 돌리면(실측, `check_starter.txt`) selftest가 19/44에서 멈추고, 진짜 systemd에서는 **READY=1이 안 와서 4초 뒤 start가 타임아웃**난다.

```text
FAIL: selftest (fake PID 1): 19/44 passed
Job for esn-b13-sensord-starter-610076.service failed because a timeout was exceeded.
systemctl start rc=1 took 4.20 s
FAIL: systemctl start returns 0 (READY=1 arrived before TimeoutStartSec=4)
---- journal (why the start failed) ----
Starting esn-b13-sensord-starter-610076.service - ELX-B13 fake sensor daemon (starter-610076)...
esn_sensord: pid=614448 NOTIFY_SOCKET=/run/systemd/notify ping_period_us=0
esn_sensord: start-up done (temp=23500 mC)
esn-b13-sensord-starter-610076.service: start operation timed out. Terminating.
esn_sensord: SIGTERM -> exit 0 (samples=39 pets=0)
esn-b13-sensord-starter-610076.service: Failed with result 'timeout'.
---- systemctl show ----
ActiveState=activating SubState=auto-restart Result=timeout NRestarts=0
2/5 passed
RESULT: FAIL (starter, 3 failed)
```

데몬은 "start-up done"이라고 믿지만 systemd는 그걸 모른다 — `Type=notify` 에서 **시작 완료의 유일한 증거는 `READY=1` 데이터그램**이다. 데몬이 SIGTERM에 exit 0으로 끝났는데도 결과는 `timeout` 이고, `Restart=on-failure` 라서 곧바로 `auto-restart` 로 들어간다(시작 타임아웃도 "실패"다).

## 4. 힌트

<details><summary>힌트 1 — 추상 소켓 길이는 왜 "정확히"여야 하나</summary>

경로 소켓은 커널이 이름을 **NUL까지 strlen** 해서 다시 계산하므로 길이를 넉넉히 줘도 된다. 추상 소켓은 이름이 "`sun_path[0]='\0'` 다음부터 **addrlen이 가리키는 끝까지의 바이트 전부**"다. 뒤에 NUL 하나라도 더 붙이면 다른 이름이 된다. 그래서 `offsetof(struct sockaddr_un, sun_path) + strlen(env)` (`@` 자리가 `\0` 으로 바뀌어 한 바이트를 차지하므로 +1이 필요 없다).
</details>

<details><summary>힌트 2 — sendto 하나면 끝이다</summary>

`connect` 할 필요 없다. 매번 `socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0)` → `sendto(fd, msg, strlen(msg), MSG_NOSIGNAL, ...)` → `close`. 메시지는 `"READY=1\nSTATUS=..."` 처럼 줄바꿈으로 여러 개를 한 데이터그램에 담는다. 받는 쪽이 없으면 `ECONNREFUSED` 또는 `ENOENT` 가 오는데, 그걸 `-errno` 로 돌려주기만 하면 된다.
</details>

<details><summary>힌트 3 — WATCHDOG_PID 는 왜 보나</summary>

`WatchdogSec=` 를 켜면 systemd는 `WATCHDOG_USEC` 와 함께 `WATCHDOG_PID=<MainPID>` 를 넣어 준다. 데몬이 `fork()` 한 자식은 환경 변수를 그대로 물려받는다. 자식이 "나도 ping해야지" 하고 보내면 엉뚱한 프로세스가 watchdog을 먹이는 셈이다. 그래서 `WATCHDOG_PID != getpid()` 면 0(끔)을 돌려준다. 파싱은 `strtoull` + `errno` + `*end == '\0'` 세 가지를 다 본다.
</details>

<details><summary>힌트 4 — ping은 어디서 보내나</summary>

메인 루프 안, **실제 일(센서 읽기)이 끝난 뒤**다. 별도 스레드나 타이머에서 무조건 보내면 메인 루프가 멈춰도 ping은 계속 나가서 watchdog이 아무것도 못 잡는다(펌웨어에서 "타이머 ISR에서 WDT kick 금지"와 같은 규칙). 그래서 `pet_due` 가 `samples != samples_at_pet` 까지 본다.
</details>

## 5. 풀이

### 5.1 Part A — 주소 만들기와 데이터그램 한 개

`struct sockaddr_un` 은 `sun_family`(2바이트) + `sun_path[108]` 이다. aarch64 Linux에서 `offsetof(sun_path) = 2`.

```svg
<svg viewBox="0 0 740 220" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">NOTIFY_SOCKET 값 → sockaddr_un 바이트 (addrlen이 이름의 끝을 정한다)</text>
<text x="10" y="50" fill="currentColor" font-size="12" font-family="monospace">"/run/systemd/notify"</text>
<rect x="200" y="36" width="50" height="22" class="f-mut" stroke="currentColor"/>
<text x="206" y="52" fill="currentColor" font-size="12">AF_UNIX</text>
<rect x="250" y="36" width="310" height="22" class="f-ok" stroke="currentColor"/>
<text x="258" y="52" fill="currentColor" font-size="12" font-family="monospace">/run/systemd/notify</text>
<rect x="560" y="36" width="30" height="22" class="f-ok" stroke="currentColor"/>
<text x="566" y="52" fill="currentColor" font-size="12" font-family="monospace">\0</text>
<text x="600" y="52" fill="currentColor" font-size="12">len = 2+19+1 = 22</text>
<text x="10" y="100" fill="currentColor" font-size="12" font-family="monospace">"@esn/abc"</text>
<rect x="200" y="86" width="50" height="22" class="f-mut" stroke="currentColor"/>
<text x="206" y="102" fill="currentColor" font-size="12">AF_UNIX</text>
<rect x="250" y="86" width="30" height="22" class="f-warn" stroke="currentColor"/>
<text x="256" y="102" fill="currentColor" font-size="12" font-family="monospace">\0</text>
<rect x="280" y="86" width="150" height="22" class="f-ok" stroke="currentColor"/>
<text x="288" y="102" fill="currentColor" font-size="12" font-family="monospace">esn/abc</text>
<text x="450" y="102" fill="currentColor" font-size="12">len = 2+8 = 10 (뒤에 NUL 없음)</text>
<text x="10" y="140" fill="currentColor" font-size="12">경로: 커널 unix_mkname_bsd()가 strlen으로 길이를 다시 잰다 → 넉넉한 len도 통한다.</text>
<text x="10" y="160" fill="currentColor" font-size="12">추상: 이름 = sun_path[0] 이후 len까지의 바이트 전부.</text>
<text x="10" y="178" fill="currentColor" font-size="12">len+1 이면 "esn/abc\0" 라는 다른 이름 → ECONNREFUSED.</text>
<text x="10" y="205" fill="currentColor" font-size="12">selftest가 두 길이(22, 10)를 그대로 검사: len == off + 19 + 1, len == off + 8</text>
</svg>
```

커널 소스(`/var/lab/src/linux-6.18.55/net/unix/af_unix.c`, VM 커널은 7.0이지만 이 부분 로직은 같다고 보고 6.18 소스를 읽었다)에서 확인한 것:

| 함수 | 하는 일 | 이 레슨과의 관계 |
|---|---|---|
| `unix_validate_addr()` | `addr_len <= offsetof(sun_path)` 이거나 `> sizeof` 면 `-EINVAL` | 빈 이름은 커널도 거부 |
| `unix_mkname_bsd()` | `return strlen(addr->__data) + offset + 1;` — 경로는 길이를 다시 잰다 | 경로 소켓의 `+1` 은 관례, 있어도 없어도 된다 |
| `unix_find_other()` | `if (sunaddr->sun_path[0])` → BSD(경로), 아니면 `unix_find_abstract(..., addr_len, ...)` | 추상은 **addr_len 그대로** 비교 |
| `unix_maybe_add_creds()` | 받는 쪽이 `SO_PASSCRED` 면 `pid = task_tgid(current)` 를 붙인다 | systemd가 보낸 사람을 아는 방법. **tgid**라서 같은 프로세스의 스레드는 MainPID로 보인다 |

정말 그런지 `demo_abstract_len.c` 로 확인했다(VM, 실측):

<!-- code: demo_abstract_len.c -->

```text
exact (offsetof + 1 + strlen)        len= 15 -> delivered
exact + 1 (a trailing NUL)           len= 16 -> Connection refused
sizeof(struct sockaddr_un)           len=110 -> Connection refused
receiver got 7 bytes
```

`sizeof(sa)` 를 길이로 넘기는 코드는 경로 소켓(`/run/systemd/notify`)에서는 멀쩡히 동작하다가, 컨테이너처럼 `NOTIFY_SOCKET=@...` 을 주는 환경에서만 조용히 실패한다. 테스트에 추상 소켓을 꼭 넣는 이유다.

`notify_send` 의 나머지 결정:

- **`NOTIFY_SOCKET` 없음 → 0**: 같은 바이너리를 BusyBox init(ELX-B04, ELX-B12)이나 터미널에서 돌려도 문제없다.
- **`MSG_NOSIGNAL`**: 데이터그램 소켓에서 SIGPIPE가 날 일은 드물지만, 보내는 쪽 코드를 스트림 소켓과 공유할 때 안전하다. 실제 실패는 `ECONNREFUSED`/`ENOENT` 로 온다(selftest: 받는 소켓을 지운 뒤 `notify_send(...) < 0`).
- **매번 열고 닫기**: 2.5초에 한 번이라 비용이 없다. 대신 fd 누수·재시작 후 stale fd 문제가 없다.
- **데이터그램 = 메시지 경계 보존**: `READY=1\nSTATUS=...` 가 한 번에 원자적으로 도착한다. 스트림이면 경계를 따로 만들어야 한다(OS-B14 참고).

### 5.2 Part B — 절반 주기, 그리고 "진행했을 때만"

| 입력 | `watchdog_period_usec()` | 이유 |
|---|---|---|
| `WATCHDOG_USEC=5000000`, `WATCHDOG_PID` 없음 | 2500000 | 절반 주기 |
| 위 + `WATCHDOG_PID=<내 pid>` | 2500000 | systemd가 실제로 주는 형태 (아래 실측) |
| `WATCHDOG_PID=1` | 0 | 물려받은 환경 — 내가 MainPID가 아님 |
| `abc`, `5000000x`, `-5`, `0`, 없음 | 0 | 잘못된 값이면 끈다 (추측하지 않는다) |

**왜 절반인가**: ping 하나가 스케줄링 지연·로드로 늦어져도 다음 ping이 한도 안에 들어오게 하는 여유다. 주기를 한도와 같게 잡으면 조금만 늦어도 SIGABRT를 맞는다. `sd_watchdog_enabled(3)` 문서도 절반을 권한다(문서 기준, 이 VM엔 헤더가 없어 함수는 안 썼다).

`check.sh` 가 데몬의 환경을 `/proc/<MainPID>/environ` 에서 직접 읽은 결과(실측):

```text
  env: NOTIFY_SOCKET=/run/systemd/notify
  env: WATCHDOG_PID=632917
  env: WATCHDOG_USEC=5000000
after 6.5 s: MainPID=632917 NRestarts=0 StatusText=temp=23500 mC samples=50 pets=2
```

6.5초 동안 `pets=2` — READY 뒤 2.5 s, 5.0 s에 한 번씩. `samples=50` 은 100 ms 루프 × 약 5초(마지막 ping 시점의 값이 STATUS에 실렸다).

**진행 조건**은 B2의 핵심이다. 아래 그림은 hang을 주입한 실제 실행의 journal 타임스탬프(`-o short-monotonic`)를 READY 시점 기준으로 다시 그린 것이다. ping 두 개(2.50, 5.00)는 로그를 남기지 않으므로 **코드에서 계산한 시각**이다.

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="20" fill="currentColor" font-size="13" font-weight="bold">hang → Watchdog timeout → SIGABRT → 재시작 (실측, 0 = Started 349.242 s)</text>
<line x1="40" y1="150" x2="720" y2="150" stroke="currentColor"/>
<text x="40" y="170" fill="currentColor">0</text>
<text x="165" y="170" fill="currentColor">2.5</text>
<text x="290" y="170" fill="currentColor">5.0</text>
<text x="415" y="170" fill="currentColor">7.5</text>
<text x="540" y="170" fill="currentColor">10.0</text>
<text x="690" y="170" fill="currentColor">13 s</text>
<circle cx="40" cy="150" r="6" class="f-ok" stroke="currentColor"/>
<text x="20" y="60" fill="currentColor">READY=1</text>
<text x="20" y="76" fill="currentColor">Started</text>
<line x1="40" y1="82" x2="40" y2="144" stroke="currentColor"/>
<circle cx="165" cy="150" r="5" class="f-ok" stroke="currentColor"/>
<circle cx="290" cy="150" r="5" class="f-ok" stroke="currentColor"/>
<text x="140" y="130" fill="currentColor">ping (계산)</text>
<text x="265" y="130" fill="currentColor">ping (계산)</text>
<circle cx="376" cy="150" r="6" class="f-warn" stroke="currentColor"/>
<text x="330" y="60" fill="currentColor">+6.71 hang 주입</text>
<text x="330" y="76" fill="currentColor">루프 멈춤, ping 없음</text>
<line x1="376" y1="82" x2="376" y2="144" stroke="currentColor"/>
<rect x="290" y="185" width="251" height="18" class="f-warn" stroke="currentColor"/>
<text x="350" y="198" fill="currentColor">WatchdogSec=5 (마지막 ping부터)</text>
<circle cx="541" cy="150" r="6" class="f-warn" stroke="currentColor"/>
<text x="450" y="40" fill="currentColor">+10.01 Watchdog timeout (limit 5s)!</text>
<text x="450" y="56" fill="currentColor">→ Killing ... with signal SIGABRT</text>
<text x="450" y="72" fill="currentColor">+10.10 status=6/ABRT, Result=watchdog</text>
<line x1="541" y1="78" x2="541" y2="144" stroke="currentColor"/>
<circle cx="605" cy="150" r="5" class="f-acc" stroke="currentColor"/>
<circle cx="620" cy="150" r="6" class="f-ok" stroke="currentColor"/>
<text x="440" y="222" fill="currentColor">+11.30 Scheduled restart (counter 1)</text>
<text x="440" y="238" fill="currentColor">+11.60 Started, 새 pid 653713</text>
<line x1="612" y1="156" x2="612" y2="208" stroke="currentColor"/>
<text x="40" y="262" fill="currentColor">RestartSec=1: 실패(+10.10) 뒤 약 1.2 s 후 재시작 job</text>
<text x="40" y="280" fill="currentColor">hang 주입부터 kill까지 3.3 s — 기준은 hang이 아니라 "마지막 ping + 5 s"</text>
</svg>
```

### 5.3 Part C — 유닛 파일 한 줄씩, 그리고 실제 결과

| 지시어 | 섹션 | 뜻 | 이 레슨에서 확인한 증거 |
|---|---|---|---|
| `Wants=dep` | Unit | dep도 같이 시작(약한 의존: dep가 실패해도 나는 시작) | 우리는 sensord만 start했는데 dep `ActiveState=active` |
| `After=dep` | Unit | **순서만**: dep의 start job이 끝난 뒤 나를 실행 | dep active `294348930713` us ≤ 데몬 exec `294348932270` us |
| `StartLimitIntervalSec=60`, `StartLimitBurst=4` | Unit | 60 s 안에 start 4번까지, 그 다음은 거부 | NRestarts=4, `Start request repeated too quickly.` |
| `Type=notify` | Service | `READY=1` 이 와야 active | starter: `start operation timed out` |
| `Restart=on-failure` | Service | exit≠0, 시그널, watchdog, 타임아웃이면 재시작. 깨끗한 stop은 아님 | watchdog 뒤 재시작, `systemctl stop` 뒤엔 inactive 유지 |
| `RestartSec=1` | Service | 재시작 전 대기(기본 100 ms — 이 VM `DefaultRestartUSec=100ms`) | 실패 → 재시작 job 간격 약 1.07–1.2 s |
| `WatchdogSec=5` | Service | 5 s 안에 `WATCHDOG=1` 없으면 `WatchdogSignal`(기본 SIGABRT) | `Watchdog timeout (limit 5s)!` |
| `TimeoutStartSec=4` | Service | READY까지 기다리는 시간 | starter 4.20 s 만에 실패 |

**Wants vs Requires vs After**: `Wants`/`Requires` 는 "같이 켜라"(끌어오기), `After`/`Before` 는 "누가 먼저"(순서). 둘은 독립 축이다. `Wants=` 만 쓰면 dep와 나는 **동시에** 시작된다. `Requires=` 는 dep가 실패하면 나도 시작 안 하고 dep가 stop되면 나도 stop. 센서 전원처럼 "없으면 의미 없음"이면 `Requires=`+`After=`, "있으면 좋음"이면 `Wants=`+`After=` 가 기본형이다.

**`After=` 는 상대의 `Type=` 이 정한 "시작 완료"를 기다린다.** 이 레슨의 dep는 `oneshot`(프로세스가 끝나야 완료) + `RemainAfterExit=yes`(끝나도 active 유지)다. 만약 sensord 뒤에 `After=esn-b13-sensord` 로 붙는 소비자 서비스가 있다면, sensord가 `Type=simple` 일 때 그 소비자는 fork 직후 출발해서 아직 보정 중인 센서를 연다. `Type=notify` 라서 READY까지 기다린다 — 그게 READY를 "보정 끝난 뒤"에 보내는 이유다.

hang 단계에서 `check.sh` 가 0.2초마다 본 상태 변화(실측):

```text
  15:17:53.487 ActiveState=active SubState=running MainPID=632917 Result=success NRestarts=0
  15:17:57.099 ActiveState=activating SubState=auto-restart MainPID=0 Result=watchdog NRestarts=0
  15:17:58.361 ActiveState=activating SubState=start MainPID=653713 Result=success NRestarts=1
  15:17:58.574 ActiveState=active SubState=running MainPID=653713 Result=success NRestarts=1
```

`Result=watchdog` 는 **auto-restart 대기 1초 동안만** 보인다. 재시작이 성공하면 `success` 로 덮인다. 그래서 "죽었었다"의 영구 기록은 `NRestarts` 와 journal이다. 같은 순간의 journal(`SHOWJ=1`, `check_solution.txt`):

```text
[294349.242406] lima-elinux systemd[1]: Started esn-b13-sensord-solution-632206.service - ELX-B13 fake sensor daemon (solution-632206).
[294355.949979] lima-elinux esn_sensord[632917]: esn_sensord: hang injected: main loop stuck, no more WATCHDOG=1
[294359.252161] lima-elinux systemd[1]: esn-b13-sensord-solution-632206.service: Watchdog timeout (limit 5s)!
[294359.252178] lima-elinux systemd[1]: esn-b13-sensord-solution-632206.service: Killing process 632917 (esn_sensord) with signal SIGABRT.
[294359.341150] lima-elinux systemd[1]: esn-b13-sensord-solution-632206.service: Main process exited, code=dumped, status=6/ABRT
[294359.341580] lima-elinux systemd[1]: esn-b13-sensord-solution-632206.service: Failed with result 'watchdog'.
[294360.537511] lima-elinux systemd[1]: esn-b13-sensord-solution-632206.service: Scheduled restart job, restart counter is at 1.
[294360.845453] lima-elinux systemd[1]: Started esn-b13-sensord-solution-632206.service - ELX-B13 fake sensor daemon (solution-632206).
```

- **왜 SIGABRT인가**: 기본 `WatchdogSignal=SIGABRT`. 잡히지 않은 SIGABRT는 코어 덤프를 만든다 → "멈춘 순간의 스택"을 얻을 수 있다. `code=dumped` 는 그 표시다. 이 VM의 `core_pattern` 은 apport 파이프이고, apport는 `/var/log/apport.log` 에 `executable does not belong to a package, ignoring` 을 남기고 버렸다(`/var/crash` 에 새 파일 없음 — 확인함). `fs/coredump.c` 의 `coredump_pipe()` 주석대로 파이프 core_pattern에서는 RLIMIT_CORE가 (1이라는 특수값 말고는) 의미가 없어서 helper가 항상 불린다.
- **hang 중 SIGTERM은 안 먹는다**: 멈춘 루프(`pause()`)는 핸들러가 플래그만 세우고 다시 멈춘다. 그래서 진짜 멈춘 데몬에 `systemctl stop` 을 하면 `TimeoutStopSec=5` 뒤 SIGKILL까지 간다. selftest C-2가 "SIGTERM 뒤에도 STOPPING=1이 안 온다"를 확인한다.

**stop**: `systemctl stop` → SIGTERM → 데몬이 `STOPPING=1` 을 보내고 exit 0 → `Deactivated successfully.` 그 뒤 1.5초(> RestartSec) 동안 inactive 그대로다. **stop은 실패가 아니다** — 어떤 `Restart=` 값도 수동 stop 뒤에 살리지 않는다. 참고: stop이 끝나면 아무도 참조하지 않는 이 유닛은 메모리에서 내려가서(GC) `systemctl show` 가 기본값(`Result=success`, `ExecMainCode=0`)을 보인다. 그래서 stop의 증거는 journal로 잡았다.

**시작 제한**: 센서 파일을 치우고 start하면(실측):

```text
PASS: start fails while the sensor is missing (rc=1)
PASS: gives up: ActiveState=failed within 15 s
  NRestarts/ExecMainStatus/StatusErrno/StatusText = 4/3/19/sensor missing
PASS: NRestarts=4 ExecMainStatus=3 StatusErrno=19 StatusText='sensor missing'
PASS: journal: Start request repeated too quickly.
Job for esn-b13-sensord-solution-632206.service failed because the control process exited with error code.
  manual start: rc=1 Result=exit-code daemon runs 4 -> 4, limiter lines 1 -> 2
PASS: manual start refused by the limiter: rc!=0, daemon NOT executed, one more 'repeated too quickly'
PASS: reset-failed clears the limiter: start works again
```

| journal (monotonic s) | 사건 |
|---|---|
| 362.611 | 수동 start #1: `missing -> exit 3`, `status=3/NOTIMPLEMENTED` |
| 363.677 / 364.757 / 365.800 | 자동 재시작 #2, #3, #4 — 각각 exit 3 |
| 366.871 | `restart counter is at 4` → `Start request repeated too quickly.` → failed |
| 367.245 | 수동 start: 데몬 실행 없이 다시 `repeated too quickly` |

읽을 거리 네 개:

1. **Burst=4 = start 4번**(수동 1 + 자동 3). 다섯 번째 시도(카운터 4)가 거부된다. 그래서 `NRestarts=4` 인데 실제 실행은 4번이다.
2. `ERRNO=19` 는 `StatusErrno=19` 로, `STATUS=sensor missing` 은 `StatusText` 로 남는다 — 현장에서 `systemctl status` 한 줄로 원인이 보인다. exit 3이 `NOTIMPLEMENTED` 로 찍히는 건 systemd가 LSB init 스크립트의 종료 코드 이름표를 붙이기 때문이다(3 = "unimplemented feature"). 이름은 무시하고 숫자를 보면 된다.
3. **제한에 걸려도 `Result` 는 `exit-code` 로 남았다**(systemd 259 실측). 그래서 수동 start가 거부될 때 `systemctl` 이 "control process exited with error code" 라는 **오해하기 쉬운 메시지**를 낸다. 진짜 원인은 journal의 `Start request repeated too quickly.` 다. (처음에 `Result=start-limit-hit` 을 기대한 검사를 썼다가 실측에서 틀려서 고쳤다. 이미 failed 상태가 아닌 유닛이 제한에 걸리면 `start-limit-hit` 이 나올 수 있다 — 이 경로는 이번에 재현하지 않았다.)
4. `systemctl reset-failed` 가 제한 카운터를 지운다. 현장에서 "고쳤는데 안 켜져요"의 단골 원인이다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

검사(VM, 실측 3회 연속):

```text
$ limactl shell elinux -- bash .../ELX-B13/check.sh solution     # × 3
30/30 passed
RESULT: PASS (solution)            # 세 번 모두 exit 0, 한 번에 약 21–22 s
```

추가로 VM에서 `gcc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c` 로 selftest를 돌려 `44/44 passed`, 경고 0(practice도 `49/49`). clang `-Wall -Wextra -Werror` 빌드도 통과.

## 7. 자주 하는 실수

1. **추상 소켓에 `sizeof(struct sockaddr_un)` 을 길이로 넘김** → 경로 소켓에서는 되고 `@` 주소에서만 `Connection refused`(5.1 실측). 고침: `offsetof(sun_path) + strlen(env)`.
2. **ping을 별도 스레드/타이머에서 무조건 보냄** → 메인 루프가 데드락이어도 ping이 나가서 watchdog이 무력화. 고침: 메인 루프가 실제 일을 끝낸 자리에서, 진행이 있을 때만(`pet_due`).
   ```c
   /* 나쁨 */ for (;;) { sleep(2); notify_send("WATCHDOG=1"); }      /* 다른 스레드 */
   /* 좋음 */ if (pet_due(now, last_pet, period, samples, samples_at_pet)) notify_send("WATCHDOG=1\n...");
   ```
3. **READY=1을 너무 일찍 보냄**(main 시작하자마자) → `After=` 로 기다리던 소비자가 보정 전 센서를 연다. `Type=simple` 로 둔 것과 똑같아진다. 고침: 장치 열기·보정이 끝난 뒤에.
4. **`Wants=` 만 쓰고 `After=` 를 빠뜨림** → 같이 시작되긴 하지만 **동시에** 시작된다. 순서가 필요하면 둘 다.
5. **stdout 버퍼링** → stdout이 journald **소켓**이라 기본이 완전 버퍼링(full buffering)이다. 로그가 몇 KB 쌓일 때까지 안 보이거나, SIGABRT로 죽으면 **영영 사라진다**. 고침: `setvbuf(stdout, NULL, _IOLBF, 0)` (이 레슨의 `main` 첫 줄) 또는 stderr 사용.
6. **`Restart=always` 로 "확실하게"** → 관리자가 의도적으로 끝낸(exit 0) 경우도 되살려 디버깅을 방해하고, 설정 오류처럼 재시작해도 소용없는 실패도 반복한다. 고침: `on-failure` + `RestartPreventExitStatus=`(추가 연습 P1) + 시작 제한.
7. **시작 제한 계산 실수** → 기본값은 10 s / 5번(이 VM `DefaultStartLimitIntervalUSec=10s`, `DefaultStartLimitBurst=5`)인데 `RestartSec=3` 이면 10초 안에 5번이 안 돼서 **영원히 재시작**한다. `RestartSec × Burst < IntervalSec` 이 되게 같이 정한다(여기선 1 × 4 < 60).
8. **유닛 파일을 고치고 `daemon-reload` 를 잊음** → systemd는 옛 내용을 그대로 쓴다(`systemctl status` 에 "changed on disk" 경고). `check.sh` 는 설치·삭제 때마다 reload한다.

## 8. 면접 Q&A

<details><summary>Q1. After=, Wants=, Requires= 의 차이는?</summary>

끌어오기(Wants/Requires)와 순서(After/Before)는 다른 축이다. `Wants=B`: A를 시작하면 B도 시작 job을 넣지만 B가 실패해도 A는 간다. `Requires=B`: B 시작이 실패하면 A도 실패하고, B가 stop되면 A도 stop. 둘 다 순서는 정하지 않는다 → `After=B` 를 같이 써야 "B가 다 뜬 뒤 A". 이 레슨에서 dep active 시각 ≤ 데몬 exec 시각을 monotonic 타임스탬프로 확인했다.

꼬리: "그럼 Requires=만 쓰면?" → 같이 시작되지만 동시에 출발하므로 경주(race)가 남는다. B가 실패하면 A의 start job은 취소되지만, B보다 A가 먼저 일을 시작해 버릴 수 있다. 꼬리2: "BindsTo/PartOf는?" → BindsTo는 B가 어떤 이유로든 inactive가 되면 A도 멈춤(장치 unit과 짝), PartOf는 stop/restart만 전파.
</details>

<details><summary>Q2. Type=simple / exec / forking / notify 중 센서 데몬엔 뭘 쓰나?</summary>

`notify`. simple은 fork 직후, exec는 execve 성공 직후를 "시작 완료"로 본다 — 장치 열기·보정이 끝났는지 systemd가 알 길이 없다. forking은 옛날식 이중 fork 데몬용. notify는 데몬이 직접 `READY=1` 을 보낼 때 active가 되므로 `After=` 로 기다리는 소비자가 안전하다. 대가: READY를 안 보내면 `TimeoutStartSec` 뒤 실패(starter 실측: `start operation timed out. Terminating.`, `Result=timeout`).

꼬리: "보정이 TimeoutStartSec보다 오래 걸리면?" → `TimeoutStartSec` 를 늘리거나, 보정 중에 `EXTEND_TIMEOUT_USEC=` 를 주기적으로 보낸다(추가 연습 P2: 6 s 보정이 4 s 타임아웃 아래에서 성공).
</details>

<details><summary>Q3. sd_notify는 내부적으로 뭘 하나? 왜 아무 프로세스나 보낼 수 없나?</summary>

`$NOTIFY_SOCKET`(여기선 `/run/systemd/notify`)에 `AF_UNIX` `SOCK_DGRAM` 으로 `KEY=VALUE\n...` 텍스트 한 개를 보낼 뿐이다. 받는 쪽(PID 1)은 `SO_PASSCRED` 를 켜 두었으므로 커널이 메시지마다 **보낸 프로세스의 pid(tgid)·uid·gid** 를 붙인다(`unix_maybe_add_creds()`). 보내는 쪽이 위조할 수 없다. 기본 `NotifyAccess=main` 이면 MainPID가 보낸 것만 받는다. 스레드는 tgid가 같으니 괜찮고, fork한 자식은 거부된다 — 추가 연습 P5에서 자식의 `STATUS=` 가 무시되고(`StatusText=ready temp=23500 mC` 그대로), `NotifyAccess=all` 로 바꾸면 `StatusText=helper pid 965776 says hi` 로 바뀌는 것을 실측했다.

꼬리: "거부되면 journal에 남나?" → 이 VM(systemd 259 기본 로그 레벨)에서는 아무 줄도 안 남았다. 디버그 레벨에서는 찍는 것으로 알고 있다(미확인). 그래서 "STATUS가 안 바뀐다"는 증상으로 의심해야 한다. 꼬리2: "NotifyAccess=all 이면 다 받나?" → 보낸 프로세스가 PID 1이 읽기 전에 죽으면 버려질 수 있다(P5 함정, 실측 경주).
</details>

<details><summary>Q4. WatchdogSec=5 일 때 systemd는 정확히 언제, 무엇을 하나?</summary>

마지막 `WATCHDOG=1` 로부터 5 s 안에 다음 ping이 없으면 `Watchdog timeout (limit 5s)!` 을 찍고 `WatchdogSignal=`(기본 SIGABRT)을 보낸다. 결과는 `Result=watchdog`, `Restart=on-failure`(또는 on-watchdog/on-abnormal)면 `RestartSec` 뒤 재시작. 실측: 마지막 ping(계산상 +5.00) → timeout +10.01, hang 주입(+6.71)이 기준이 아니다. 데몬은 절반(2.5 s)마다 ping한다.

꼬리: "왜 SIGKILL이 아니라 SIGABRT?" → 코어 덤프로 멈춘 지점의 스택을 남기려고. 코어는 `systemd-coredump` 나(이 VM은 apport) 다른 core_pattern이 받는다. 꼬리2: "데몬이 스스로 고장을 알면?" → `WATCHDOG=trigger` 로 즉시 같은 동작(P3 실측: `Watchdog request (last status: sensor fault: self-triggered watchdog)!`).
</details>

<details><summary>Q5. systemd 서비스 watchdog과 하드웨어 watchdog은 어떤 관계인가?</summary>

사슬이다. 서비스 → PID 1: `WATCHDOG=1`(서비스 watchdog, 서비스 하나를 재시작). PID 1 → 하드웨어: `RuntimeWatchdogSec=`(system.conf)이면 PID 1이 `/dev/watchdog0` 를 주기적으로 먹인다 — PID 1 자신이 멈추면 SoC watchdog이 보드를 리셋. `RebootWatchdogSec=` 는 재부팅 도중 멈춤 대비. 이 VM은 `RuntimeWatchdogUSec=0`(꺼짐), `/dev/watchdog*` 없음, `RebootWatchdogUSec=10min`(기본) — 실측. 장비라면 둘 다 켜서 "서비스 멈춤 → 재시작", "PID 1/커널 멈춤 → 보드 리셋" 두 층을 만든다.

꼬리: "재시작해도 계속 죽으면?" → 시작 제한에서 멈추고 `StartLimitAction=reboot` 이나 `FailureAction=`, `OnFailure=` 훅(로그 저장, fault LED)으로 넘긴다. 재부팅 루프는 bootcount(ELX-M12의 U-Boot 쪽)로 막는다.
</details>

<details><summary>Q6. 크래시 루프를 어떻게 막나? 이 레슨의 숫자로 설명해 보라.</summary>

`StartLimitIntervalSec=60`, `StartLimitBurst=4`: 60 s 안에 start 4번까지. 실측: 수동 1 + 자동 3번 실행, 네 번째 자동 재시작 시도(카운터 4)에서 `Start request repeated too quickly.` → failed, `NRestarts=4`. 그 뒤 수동 start도 데몬을 실행하지 않고 거부. `reset-failed` 가 카운터를 지운다. 재시작해도 소용없는 실패(설정 오류, 장치 없음)는 `RestartPreventExitStatus=3` 으로 아예 재시작하지 않는다(P1 실측: NRestarts=0, 즉시 failed).

꼬리: "그때 systemctl은 뭐라고 하나?" → 259에서는 `Result` 가 이전 실패(`exit-code`)로 남아서 "control process exited with error code" 라고 한다. 오해하기 쉽다 — journal을 봐야 한다.
</details>

<details><summary>Q7. 유닛 파일은 어디에 두나? 왜 이 레슨은 /run 에 뒀나?</summary>

우선순위: `/etc/systemd/system`(관리자) > `/run/systemd/system`(런타임, tmpfs, 재부팅 시 사라짐) > `/usr/lib/systemd/system`(패키지). 같은 이름이면 앞의 것이 이긴다. 부분 수정은 `<unit>.d/*.conf` drop-in(사전순 적용 — P5b에서 `zz-p5b.conf` 로 이름을 지은 이유: `p5b.conf` 는 `practice.conf` 보다 **앞**에 정렬돼서 `Environment=` 가 덮이지 않았다). 공유 VM에서 실험할 땐 `/run` 이 안전하다: 깜빡 잊어도 재부팅하면 사라지고, `/etc` 를 더럽히지 않는다.

꼬리: "고친 뒤 꼭 할 일은?" → `systemctl daemon-reload`. `systemd-analyze verify <file>` 로 문법·실행 파일 존재를 미리 확인(이 레슨 check.sh 첫 단계).
</details>

<details><summary>Q8. 현장에서 "센서 서비스가 부팅 때 안 떠요"를 받으면 무엇부터 보나?</summary>

① `systemctl status <unit>`: ActiveState/Result, `Status:`(우리 `STATUS=`), 최근 로그. ② `systemctl show -p Result,NRestarts,ExecMainStatus,StatusErrno,MainPID`: 숫자로. ③ `journalctl -b -u <unit> -o short-monotonic`: 시간 순 사건(이 레슨의 모든 증거). ④ 의존 체인: `systemctl list-dependencies`, `systemd-analyze critical-chain <unit>`(부팅 시간 쪽은 ELX-A08). ⑤ 시작 제한에 걸렸으면 원인 고친 뒤 `reset-failed`.

꼬리: "StatusErrno는 누가 채우나?" → 데몬이 `ERRNO=19` 를 보내야 채워진다. 그래서 실패 경로에서도 notify를 보내게 만든다(C1).
</details>

## 9. 추가 연습

`practice.c` 는 solution에 아래 P2–P5를 더한 것이고, P1과 P2의 설정은 drop-in `practice.conf` 로 들어간다. `check.sh practice` 가 전부 한 번에 검증한다.

<details><summary>P1. 센서가 없으면(exit 3) 재시작하지 말고 바로 failed로</summary>

재시작해도 센서가 생기지 않으므로 시간·로그만 낭비한다. `[Service] RestartPreventExitStatus=3`. 실측: `ActiveState/NRestarts/ExecMainStatus/StatusErrno = failed/0/3/19`, journal에 `Scheduled restart` 줄 없음. 펌웨어로 치면 "HW 결함이면 리셋 루프 대신 fault 상태로".
</details>

<details><summary>P2. 보정이 6초 걸린다. TimeoutStartSec=4 는 그대로 두고 시작에 성공하라</summary>

보정 중 1 s마다 `EXTEND_TIMEOUT_USEC=3000000` 을 보낸다(지금부터 3 s 안에 READY 또는 다음 연장이 오면 된다). 실측: `systemctl start rc=0 took 6.03 s` (3회 6.03–6.07 s), journal에 `calibration done after 6000 ms`, `start operation timed out` 없음. selftest는 2500 ms 보정에서 EXTEND가 정확히 3개 온 뒤 READY가 오는지 센다.
</details>

<details><summary>P3. 데몬이 스스로 고장을 감지하면 기다리지 말고 watchdog 동작을 일으켜라</summary>

`<hang>.fault` 플래그 → `WATCHDOG=trigger\nSTATUS=sensor fault: self-triggered watchdog`. 실측 journal:
`Watchdog request (last status: sensor fault: self-triggered watchdog)!` → `Killing process ... with signal SIGABRT.` → `Failed with result 'watchdog'.` → 재시작. 5 s 기다리지 않는다(실측: trigger 로그 301572.242028 → `Killing` 301572.242084, 0.1 ms 미만).
</details>

<details><summary>P4. 시그널 없이 데몬이 스스로 끝내기로 했다(설정에서 disabled). systemd에 알리고 깨끗이 끝내라</summary>

`<hang>.quit` 플래그 → `STOPPING=1` 을 먼저 보내고 1.5 s 정리 후 exit 0. 실측: 정리하는 동안 `ActiveState=deactivating`(pid 살아 있음), 끝나면 `Deactivated successfully.`, exit 0이라 `Restart=on-failure` 는 재시작하지 않음. journal에서 `self stop`(301579.582) → `Deactivated`(301581.083) = 1.50 s — usleep이 끊기지 않았으니 systemd가 이 상태에서 SIGTERM을 보내지 않은 것으로 보인다(관찰에 근거한 추론, systemd 소스는 이 VM에 없어 확인 못 함).
</details>

<details><summary>P5. fork한 도우미 프로세스가 STATUS=를 보내면? NotifyAccess로 비교하라</summary>

READY 뒤 자식이 `STATUS=helper pid N says hi` 를 보낸다. 기본(main): `StatusText=ready temp=23500 mC` 그대로(거부). `NotifyAccess=all`(drop-in `zz-p5b.conf`): `StatusText=helper pid 965776 says hi  MainPID=965562` — pid가 다르다. selftest는 가짜 PID 1에서 자격증명 pid가 MainPID와 다르고 메시지 안의 pid와 같은지 확인한다. 자식은 `WATCHDOG_PID` 가 달라서 `watchdog_period_usec()` 도 0이다(Part B).

**함정(실측)**: 처음 버전은 자식이 보내자마자 `_exit` 하고 부모가 `waitpid` 했다. 그러면 `NotifyAccess=all` 인데도 **3번 연속 StatusText가 안 바뀌었다**(그 전 두 번은 바뀜 — 경주). PID 1이 데이터그램을 읽을 때 보낸 pid가 이미 죽어 있으면 그 pid를 이 유닛(cgroup)에 연결하지 못해서 버리는 것으로 보인다(관찰에 근거한 추론, systemd 소스 미확인). 그래서 자식이 보낸 뒤 1 s 살아 있게 하고, 부모는 `SIGCHLD=SIG_IGN` 으로 기다리지 않게 고쳤다 → 3/3 PASS. "보내고 바로 죽는 도우미 스크립트의 notify가 가끔 사라진다"는 현장 증상의 정체다. 그래서 notify는 가능하면 MainPID가 직접 보내는 것이 정석이다.
</details>

### 한 번에 검증

<!-- code: practice.conf -->

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B13/check.sh practice
```

```text
PASS: selftest (fake PID 1): 49/49 passed
systemctl start rc=0 took 6.03 s
PASS: P2: journal shows 6000 ms calibration, no start timeout
PASS: P5: NotifyAccess=main ignores STATUS= from the helper pid (StatusText=ready temp=23500 mC)
PASS: P3: restarted after WATCHDOG=trigger (NRestarts=1, new MainPID)
PASS: P3: journal 'Watchdog request (last status: sensor fault: self-triggered watchdog)!'
PASS: P4: STOPPING=1 -> ActiveState=deactivating while pid 962363 still runs
PASS: P4: clean exit 0 -> inactive, not restarted (Restart=on-failure)
PASS: P1: failed at once, NRestarts=0, ExecMainStatus=3, StatusErrno=19
PASS: P5b: NotifyAccess=all -> StatusText from the helper pid shows up
  StatusText=helper pid 965776 says hi  MainPID=965562
19/19 passed
RESULT: PASS (practice)
```

(일부 줄 생략 — 전체는 `check_practice.txt`. 연속 3회 PASS, 각 약 26 s.)

## 10. 핵심 정리

- `sd_notify` = `$NOTIFY_SOCKET` 으로 보내는 **AF_UNIX 데이터그램 한 개**(`KEY=VALUE\n`). `@` 는 추상 소켓: `sun_path[0]='\0'`, 길이에 NUL 없음 — `sizeof` 를 넘기면 `Connection refused`.
- `Type=notify`: `READY=1` 이 시작 완료의 유일한 증거. 안 오면 `TimeoutStartSec` 뒤 `Result=timeout`.
- `WatchdogSec=N` → env `WATCHDOG_USEC`, `WATCHDOG_PID`. **N/2 마다, 진짜 일이 진행됐을 때만** `WATCHDOG=1`. 끊기면 `Watchdog timeout` → SIGABRT(코어) → `Restart=` 정책.
- 끌어오기(`Wants`/`Requires`)와 순서(`After`)는 따로. 순서는 상대 `Type=` 의 "완료" 시점을 기다린다.
- `Restart=on-failure` + `RestartSec` + `StartLimitBurst/IntervalSec`(`[Unit]`) 를 같이 설계: RestartSec × Burst < Interval. 거부되면 `reset-failed`.
- 증거는 `systemctl show -p NRestarts,Result,ActiveState,MainPID,StatusErrno` 와 `journalctl -u -o short-monotonic`. `Result=watchdog` 은 재시작 대기 동안만 보인다.
- 서비스 watchdog(PID 1이 서비스 감시) 위에 HW watchdog(`RuntimeWatchdogSec`, 하드웨어가 PID 1 감시)을 얹어 두 층으로.

## 참고자료

- `sd_notify(3)`, `sd_watchdog_enabled(3)`, `systemd.service(5)`(Type=, Restart=, WatchdogSec=, NotifyAccess=, RestartPreventExitStatus=), `systemd.unit(5)`(Wants=, Requires=, After=, StartLimitIntervalSec=), `systemd-system.conf(5)`(RuntimeWatchdogSec=) — https://www.freedesktop.org/software/systemd/man/latest/
- Lennart Poettering, "systemd for Administrators, Part XV: Watchdogs" — http://0pointer.de/blog/projects/watchdog.html
- Linux 소스 `/var/lab/src/linux-6.18.55/net/unix/af_unix.c` (`unix_validate_addr`, `unix_mkname_bsd`, `unix_find_other`, `unix_maybe_add_creds`), `fs/coredump.c` (`coredump_pipe`)
- 배경 노트: `EmbeddedLinux/L_system_update_security/2026-10-17_L1_systemd.md` §5–6 (QEMU 이미지의 systemd 255로 같은 프로토콜 실험, OnFailure= 훅)
- 이 사이트의 관련 레슨: OS-B14(Unix 소켓 IPC 비교), OS-M18(graceful shutdown, signalfd), ELX-B04(직접 쓴 PID 1), ELX-B12(BusyBox init/inittab), ELX-M11(udev), ELX-M12(U-Boot bootcount), ELX-A08(systemd-analyze로 부팅 시간)
- 실측 로그: `check_solution.txt`, `check_starter.txt`, `check_practice.txt` (이 디렉터리)
