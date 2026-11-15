---
id: ELX-B10
time: 70분
updated: 2026-11-15
---
# ELX-B10 사용자공간 GPIO: libgpiod와 gpio-sim

## 학습 목표

- configfs 로 gpio-sim 가상 칩(chip)을 만들고 라인 이름을 붙인 뒤, `/dev/gpiochipN` 문자 디바이스(character device) **uAPI v2** ioctl 만으로 라인을 찾고, 요청하고, 쓰고, 읽는다 (libgpiod 가 감싸는 바로 그 층).
- 출력 토글을 sim 쪽 `value` 로, 입력 엣지 이벤트를 sim 쪽 `pull` 쓰기로 만들어 **개수 · 순서 · RISING/FALLING · seqno · 단조 증가 타임스탬프**를 프로그램으로 검증한다.
- 커널이 돌려주는 에러(`EBUSY`, `EINVAL`, `EPERM`)와 이벤트 유실 지점 두 곳(FIFO 넘침 = 가장 오래된 것부터 버림, 빠른 엣지 합쳐짐)을 실제 출력과 소스(`drivers/gpio/gpiolib-cdev.c`)로 설명한다.
- 소프트웨어 디바운스(debounce), active-low, 여러 라인을 한 번에 쓰는 원자적 SET_VALUES, "리셋 펄스 → DRDY 대기" 같은 브링업 시퀀스를 사용자공간에서 짠다.
- `/sys/class/gpio` 가 왜 폐기(deprecated)됐는지 실제 커널 설정과 구조 차이로 말한다.

## 1. 문제

보드 브링업 첫 주에 가장 먼저 하는 일이 "LED 깜빡이기, 버튼 읽기, 센서 리셋 핀 흔들기" 이고, 면접에서는 "sysfs GPIO 는 왜 안 쓰나요?", "이벤트 타임스탬프는 어디서 찍히나요?", "엣지를 놓치는 곳은 어디인가요?" 로 나온다. STM32 에서 `GPIOx->BSRR` 에 쓰고 EXTI 플래그를 보던 일(BM-B11)을 Linux 사용자공간에서는 **라인 요청 fd 하나**로 한다.

이 레슨의 실습은 전부 실제 커널에서 돈다: Linux 6.18.55 (`/var/lab/out/Image`, `CONFIG_GPIO_SIM=y`, `CONFIG_GPIO_CDEV=y`), QEMU TCG `-machine virt -cpu cortex-a57 -smp 2 -m 512M`. 하드웨어 대신 **gpio-sim** 이 진짜 `gpio_chip` + 진짜 IRQ(`irq_sim`)를 제공한다. VM 에는 libgpiod 런타임(2.2.1)만 있고 헤더가 없어서, 프로그램은 `<linux/gpio.h>` 의 raw ioctl 로 쓴다. 끝에 같은 코드를 libgpiod v2 로 쓰면 어떻게 되는지 참고로 붙였다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">누가 무엇을 쥐고 있나 — 프로그램 쪽 vs 보드(sim) 쪽</text>
  <rect x="10" y="36" width="230" height="190" class="f-acc" stroke="currentColor"/>
  <text x="125" y="56" fill="currentColor" text-anchor="middle" font-weight="bold">b10_test (사용자공간)</text>
  <text x="20" y="80" fill="currentColor" font-size="12" font-family="monospace">open /dev/gpiochip1 → chip fd</text>
  <text x="20" y="100" fill="currentColor" font-size="12" font-family="monospace">LINEINFO: 이름 → offset</text>
  <text x="20" y="120" fill="currentColor" font-size="12" font-family="monospace">GET_LINE → line fd (소유권)</text>
  <text x="20" y="140" fill="currentColor" font-size="12" font-family="monospace">SET/GET_VALUES (mask,bits)</text>
  <text x="20" y="160" fill="currentColor" font-size="12" font-family="monospace">poll + read → 48B 이벤트</text>
  <text x="20" y="180" fill="currentColor" font-size="12" font-family="monospace">close(fd) → 라인 해제</text>
  <text x="20" y="210" fill="currentColor" font-size="12">TODO 1~4 = 둘째~다섯째 줄</text>
  <line x1="240" y1="120" x2="262" y2="120" stroke="currentColor" marker-end="url(#b1)"/>
  <rect x="264" y="36" width="210" height="56" class="f-mut" stroke="currentColor"/>
  <text x="369" y="58" fill="currentColor" text-anchor="middle">gpiolib-cdev.c</text>
  <text x="369" y="78" fill="currentColor" text-anchor="middle" font-size="12">linereq · kfifo · edge IRQ</text>
  <rect x="264" y="104" width="210" height="56" class="f-mut" stroke="currentColor"/>
  <text x="369" y="126" fill="currentColor" text-anchor="middle">gpiolib core</text>
  <text x="369" y="146" fill="currentColor" text-anchor="middle" font-size="12">gpio_desc · ACTIVE_LOW · 소유자</text>
  <rect x="264" y="172" width="210" height="56" class="f-warn" stroke="currentColor"/>
  <text x="369" y="194" fill="currentColor" text-anchor="middle">gpio-sim.c (can sleep)</text>
  <text x="369" y="214" fill="currentColor" text-anchor="middle" font-size="12">value_map · pull_map · irq_sim</text>
  <line x1="369" y1="92" x2="369" y2="102" stroke="currentColor"/>
  <line x1="369" y1="160" x2="369" y2="170" stroke="currentColor"/>
  <line x1="474" y1="200" x2="516" y2="200" stroke="currentColor" marker-end="url(#b1)"/>
  <rect x="518" y="36" width="212" height="190" class="f-ok" stroke="currentColor"/>
  <text x="624" y="56" fill="currentColor" text-anchor="middle" font-weight="bold">"보드" = sysfs</text>
  <text x="528" y="82" fill="currentColor" font-size="12" font-family="monospace">sim_gpioK/value (읽기 전용)</text>
  <text x="528" y="100" fill="currentColor" font-size="12">  우리가 구동 중인 레벨</text>
  <text x="528" y="126" fill="currentColor" font-size="12" font-family="monospace">sim_gpioK/pull  (쓰기)</text>
  <text x="528" y="144" fill="currentColor" font-size="12">  외부 회로가 끌어올림/내림</text>
  <text x="528" y="162" fill="currentColor" font-size="12">  → 입력값 변경 + 엣지 IRQ</text>
  <text x="528" y="190" fill="currentColor" font-size="12" font-family="monospace">configfs gpio-sim/b10</text>
  <text x="528" y="208" fill="currentColor" font-size="12">  칩·라인 이름을 만든다</text>
  <text x="10" y="256" fill="currentColor" font-size="12">라인: 0 LED0, 1 BTN0, 2 IMU_RST_N(pull-up), 3 IMU_DRDY, 4..7 BUS0..3 (8 lines, b10-sim)</text>
  <text x="10" y="280" fill="currentColor">검증은 항상 "반대편" 에서: 출력은 sim value 로, 입력은 sim pull 을 써서 만든 이벤트로.</text>
  <defs><marker id="b1" markerWidth="8" markerHeight="6" refX="7" refY="3" orient="auto"><path d="M0,0 L8,3 L0,6 z" fill="currentColor"/></marker></defs>
</svg>
```

### Part A — 라인 찾기, 출력 토글, 입력 엣지 이벤트 (핵심 메커니즘)

- **A1** `gpio_find_line(chip_fd, "BTN0")` — 전역 GPIO 번호 대신 **칩 + 라인 이름**으로 offset 을 찾는다 (`GPIO_GET_CHIPINFO_IOCTL` + `GPIO_V2_GET_LINEINFO_IOCTL`). 없는 이름은 `-ENOENT`.
- **A2** `gpio_request()` 로 LED0 를 출력(초기값 0)으로 잡고 `gpio_set()` 으로 10번 토글 → 매번 sim `value` 가 따라오는지 확인. line info 에 `USED|OUTPUT`, consumer `"b10-led"` 가 보여야 한다.
- **A3** BTN0 를 `INPUT | EDGE_RISING | EDGE_FALLING` 으로 잡고, sim `pull` 을 up/down 12번 쓰되 **매번 이벤트 1개를 받은 뒤 다음을 쓴다(closed loop)**. 12개, RISING 6 · FALLING 6 교대, `line_seqno` 1..12 연속, 타임스탬프 단조 증가, 각 타임스탬프가 "쓰기 직전 ~ 받은 직후" `CLOCK_MONOTONIC` 구간 안.

### Part B — 에지 케이스와 실패 (커널이 무엇을 거절하나, 어디서 잃나)

- **B1** 이미 잡힌 라인을 또 요청 → `-EBUSY`. fd 를 닫으면 `USED` 가 사라지고 sim value 가 pull 값으로 돌아간다.
- **B2** 입력 라인에 SET_VALUES → `-EPERM`, INPUT 없는 엣지 플래그 / INPUT+OUTPUT / offset 99 / `num_lines` 0·65 → `-EINVAL`, padding 이 0 이 아닌 LINEINFO → `-EINVAL`.
- **B3** rising 만 요청하고 6펄스 → 이벤트 6개, 전부 RISING.
- **B4** `event_buffer_size=5` 로 요청 후 읽지 않고 24엣지 → 몇 개가 남나, 누가 버려졌나를 `line_seqno` 로 증명.
- **B5** 디바운스 300 ms 로 요청하고 9번 튀기기(up/down/…/up) → 이벤트 1개(RISING).

### Part C — 브링업 시나리오

- **C1** BUS0..3 네 라인을 **한 요청**으로 잡고 `set(mask=0xF, bits=0xA)` 한 번에 패턴 쓰기, `mask=0x1` 로 한 비트만 바꾸기.
- **C2** IMU 리셋 시퀀스: `IMU_RST_N` 을 `ACTIVE_LOW` 출력으로 잡아 논리 1(=물리 0, 리셋 걸림) → 100 ms → 논리 0(해제). 자식 프로세스가 "센서" 역할로 해제를 보고 20 ms 뒤 `IMU_DRDY` 를 올린다. 부모는 DRDY RISING 이벤트를 기다려 **해제 → DRDY 지연을 이벤트 타임스탬프로** 잰다(≥ 20 ms).

## 2. Problem (English)

Using only the Linux GPIO character-device uAPI v2 (`<linux/gpio.h>`, no libgpiod), implement:

```c
int  gpio_find_line(int chip_fd, const char *name);              /* offset or -ENOENT/-errno */
int  gpio_request(int chip_fd, const struct line_spec *s);       /* line-request fd or -errno */
int  gpio_set(int lfd, uint64_t mask, uint64_t bits);            /* 0 or -errno */
int  gpio_get(int lfd, uint64_t mask, uint64_t *bits);           /* 0 or -errno */
int  gpio_wait_events(int lfd, struct gpio_v2_line_event *ev, int max, int timeout_ms);
void ev_audit(const struct gpio_v2_line_event *ev, int n, uint32_t first_seq, struct ev_audit *a);
```

- `line_spec` = offsets[n], flags, initial output bits, debounce (µs), event buffer size, consumer label. Initial output values and debounce must be passed as line-config attributes **in the same ioctl** as the request.
- `gpio_wait_events` returns as soon as `max` events are collected or nothing arrives for `timeout_ms`; a single `read()` may return several 48-byte events.
- `ev_audit` counts rising/falling, `line_seqno` gaps (including before the first event), non-increasing timestamps, and same-direction neighbours.
- Constraints: all errors are returned as `-errno`; never assume global GPIO numbers; the test drives the "board" through gpio-sim sysfs (`sim_gpioK/pull`, `sim_gpioK/value`).
- Example (real run): 12 closed-loop pull toggles on BTN0 → `got=12 rising=6 falling=6 missing=0 backwards=0 same_dir=0`; a request with `event_buffer_size=5` and 24 unread edges → `read=8 first line_seq=17 last=24 dropped=16`.

## 3. 시작 코드

| 파일 | 역할 |
|---|---|
| `starter.c` / `solution.c` / `practice.c` | 게스트 테스트 프로그램 (TODO 5개 / 정답 / 정답 + 추가 연습 P1–P4). 정적 빌드 |
| `guest.sh` | 게스트에서 configfs 로 gpio-sim 칩 `b10` 을 만들고 프로그램 실행 (`check` / `demo`) |
| `check.sh` | VM 에서 빌드 → 개인 initramfs → QEMU 부팅 → 판정 |
| `demo_burst.c` | 채점 안 하는 관찰용: debugfs 보기, open-loop 버스트, HTE 플래그 |
| `run_solution.txt`, `run_demo.txt` | 이 레슨의 숫자를 뽑은 실제 실행 로그 |

맥에서 한 줄 (VM 안에서 `/tmp/ELX-B10_work/<variant>.<pid>/` 에 복사해 빌드하고 부팅한다):

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B10/check.sh starter
```

빌드는 `gcc -static -std=c11 -Wall -Wextra -Werror -O2`. 시작 코드는 컴파일되고, 순수 함수 테스트 일부와 "sim 쪽 기본값" 테스트만 통과한다: `10/61 passed` → `RESULT: FAIL (starter: ...)`, exit 1, 약 7–11초 (C2 의 "센서" 자식이 3초 기다리다 포기하는 시간 포함).

<!-- code: starter.c -->

게스트 스크립트 (configfs 칩 만들기가 여기 있다):

<!-- code: guest.sh -->

## 4. 힌트

<details><summary>힌트 1 — 이름으로 찾기: 구조체를 0 으로</summary>

`struct gpiochip_info.lines` 가 라인 수. 각 offset 마다 `struct gpio_v2_line_info li; memset(&li,0,sizeof li); li.offset=i;` 후 LINEINFO. 커널 `lineinfo_get()` 은 `padding` 이 0 이 아니면 `-EINVAL` 이다 (B2 에서 확인).

</details>

<details><summary>힌트 2 — 요청 하나에 설정 전부</summary>

`req.config.flags` 는 모든 라인 기본값. 초기 출력값은 `attrs[k].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES`, `attr.values = bits`, `attrs[k].mask = 적용할 라인 비트`. 디바운스도 같은 방식(`..._ATTR_ID_DEBOUNCE`, `debounce_period_us`). `num_attrs` 를 잊지 말 것. 성공하면 `req.fd` 가 라인 fd.

</details>

<details><summary>힌트 3 — mask 의 비트 i 는 "i 번째로 요청한 라인"</summary>

offset 이 아니다. BUS0..3 이 offset 4..7 이어도 mask 는 0xF. `gpio_v2_line_values{bits, mask}` 를 SET_VALUES/GET_VALUES 에 그대로 넘긴다.

</details>

<details><summary>힌트 4 — 이벤트 읽기</summary>

`poll(POLLIN, timeout)` → 0 이면 끝. `read(fd, ev+n, (max-n)*sizeof *ev)` 는 FIFO 에 있는 만큼(요청한 크기 안에서) 한꺼번에 준다. 바이트 수를 48 로 나눈다. `EINTR` 은 다시.

</details>

<details><summary>힌트 5 — 감사(audit)</summary>

`want = first_seq` 로 시작해 `line_seqno > want` 면 그 차이만큼 잃은 것, 그다음 `want = line_seqno + 1`. 타임스탬프는 이전보다 커야 하고, BOTH 엣지면 id 가 번갈아야 한다.

</details>

## 5. 풀이

### Part A — uAPI v2 의 다섯 동작

**A1 이름 → offset.** 라인 이름은 configfs `line0/name` 에 쓴 문자열이 그대로 `gpio_v2_line_info.name` 으로 나온다 (DT 보드라면 `gpio-line-names` 속성). 이 레슨의 칩은 부팅마다 `gpiochip1` 이었지만(앞에 PL061 이 `gpiochip0`), 칩 번호도 프로브 순서에 따라 바뀔 수 있으니 실무에서는 "라벨/이름으로 찾기" 가 정석이다 (추가 연습 P4).

**A2 요청 구조체.** 이 VM 의 `/usr/include/linux/gpio.h` 는 랩 커널 소스 `include/uapi/linux/gpio.h` 와 include guard 만 다르다 (`diff` 로 확인). 실제 크기(VM 에서 `sizeof` 로 측정):

| 구조체 | 크기 | 메모 |
|---|---|---|
| `gpio_v2_line_request` | 592 B | `offsets[64]`, `consumer[32]`, `config`, `num_lines`, `event_buffer_size`, `padding[5]`, `fd` |
| `gpio_v2_line_config` | 272 B | `flags` + `num_attrs` + `attrs[10]` (각 24 B) |
| `gpio_v2_line_info` | 256 B | name, consumer, offset, num_attrs, flags, attrs[10], padding[4] |
| `gpio_v2_line_values` | 16 B | `bits`, `mask` (둘 다 `__u64`) |
| `gpio_v2_line_event` | 48 B | ts@0, id@8, offset@12, seqno@16, line_seqno@20, padding[6] |

모든 필드가 고정 폭(`__u32/__u64`)이고 명시적 padding 이 있어 32/64비트 ABI 가 같다 — ELX-B08 의 ioctl 구조체 설계 규칙이 그대로 적용된 예다. ioctl 번호도 실제 값: `GPIO_V2_GET_LINE_IOCTL = 0xc250b407` (dir=RW, size=0x250=592, type=0xB4, nr=7).

**출력 요청은 초기값과 함께 원자적으로.** OUTPUT_VALUES 속성을 같은 ioctl 에 넣으면 커널은 방향을 출력으로 바꾸는 순간 그 값을 쓴다(`gpio_sim_direction_output()` 이 direction 과 value 를 한 락 안에서 바꾼다). sysfs 시절의 `direction=out` → `value=1` 두 단계 사이에는 0 이 잠깐 나가는 **글리치 창**이 있었다. 리셋·모터 enable 핀에서는 이게 실제 사고다.

**A3 엣지 이벤트가 오는 길.** 아래는 `drivers/gpio/gpio-sim.c`, `kernel/irq/irq_sim.c`, `drivers/gpio/gpiolib-cdev.c` 를 따라간 경로다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">pull 쓰기 한 번 → 48바이트 이벤트 하나 (합쳐질 수 있는 곳 2군데, 버려지는 곳 1군데)</text>
  <rect x="10" y="36" width="160" height="54" class="f-ok" stroke="currentColor"/>
  <text x="90" y="58" fill="currentColor" text-anchor="middle">write "pull-up"</text>
  <text x="90" y="77" fill="currentColor" text-anchor="middle" font-size="12">gpio_sim_apply_pull</text>
  <line x1="170" y1="63" x2="196" y2="63" stroke="currentColor" marker-end="url(#b2)"/>
  <rect x="198" y="36" width="170" height="54" class="f-warn" stroke="currentColor"/>
  <text x="283" y="58" fill="currentColor" text-anchor="middle">irq_sim pending 비트</text>
  <text x="283" y="77" fill="currentColor" text-anchor="middle" font-size="12">라인당 1비트 + irq_work</text>
  <line x1="368" y1="63" x2="394" y2="63" stroke="currentColor" marker-end="url(#b2)"/>
  <rect x="396" y="36" width="160" height="54" class="f-mut" stroke="currentColor"/>
  <text x="476" y="58" fill="currentColor" text-anchor="middle">edge_irq_handler</text>
  <text x="476" y="77" fill="currentColor" text-anchor="middle" font-size="12">hard IRQ: ts 기록</text>
  <line x1="556" y1="63" x2="582" y2="63" stroke="currentColor" marker-end="url(#b2)"/>
  <rect x="584" y="36" width="146" height="54" class="f-warn" stroke="currentColor"/>
  <text x="657" y="58" fill="currentColor" text-anchor="middle">IRQ_WAKE_THREAD</text>
  <text x="657" y="77" fill="currentColor" text-anchor="middle" font-size="12">RUNTHREAD 1비트</text>
  <line x1="657" y1="90" x2="657" y2="118" stroke="currentColor" marker-end="url(#b2)"/>
  <rect x="470" y="120" width="260" height="70" class="f-mut" stroke="currentColor"/>
  <text x="600" y="140" fill="currentColor" text-anchor="middle">edge_irq_thread (irq/22-b10-btn)</text>
  <text x="600" y="158" fill="currentColor" text-anchor="middle" font-size="12">BOTH 이면 id = 지금 레벨을 읽어 결정</text>
  <text x="600" y="176" fill="currentColor" text-anchor="middle" font-size="12">line_seqno++ (여기서 번호가 붙는다)</text>
  <line x1="470" y1="155" x2="404" y2="155" stroke="currentColor" marker-end="url(#b2)"/>
  <rect x="190" y="120" width="212" height="70" class="f-acc" stroke="currentColor"/>
  <text x="296" y="140" fill="currentColor" text-anchor="middle">linereq kfifo</text>
  <text x="296" y="158" fill="currentColor" text-anchor="middle" font-size="12">크기 = roundup_pow_of_two(요청)</text>
  <text x="296" y="176" fill="currentColor" text-anchor="middle" font-size="12">꽉 차면 가장 오래된 것 버림</text>
  <line x1="190" y1="155" x2="132" y2="155" stroke="currentColor" marker-end="url(#b2)"/>
  <rect x="10" y="120" width="120" height="70" class="f-ok" stroke="currentColor"/>
  <text x="70" y="145" fill="currentColor" text-anchor="middle">poll + read</text>
  <text x="70" y="165" fill="currentColor" text-anchor="middle" font-size="12">48 B × n</text>
  <text x="10" y="222" fill="currentColor">합쳐짐 ①: 처리되기 전에 같은 라인 pull 을 또 쓰면 pending 비트는 이미 1 → 엣지 하나로 줄어든다.</text>
  <text x="10" y="242" fill="currentColor">합쳐짐 ②: 스레드가 도는 동안 hard IRQ 가 두 번 오면 재실행 예약은 한 번 (ELX-M05 Part B).</text>
  <text x="10" y="262" fill="currentColor">→ 합쳐진 엣지는 line_seqno 를 받지 못한다(번호 공백 없음). FIFO 에서 버려진 것만 공백이 생긴다.</text>
  <text x="10" y="290" fill="currentColor">실측 (closed loop, 12엣지): seq 1..12 연속, dt 193~5373 us (TCG, 실제 HW 아님)</text>
  <text x="10" y="312" fill="currentColor">이벤트 ts 는 hard IRQ 에서 찍힌다 → 사용자가 늦게 읽어도 엣지 간격은 정확.</text>
  <defs><marker id="b2" markerWidth="8" markerHeight="6" refX="7" refY="3" orient="auto"><path d="M0,0 L8,3 L0,6 z" fill="currentColor"/></marker></defs>
</svg>
```

실제 출력 (`run_solution.txt`, Linux 6.18.55, QEMU TCG cortex-a57 `-smp 2`):

```text
--- Part A3: input BTN0, closed-loop edge events ---
  ev0  RISING  ts=2.212781008 seq=1 line_seq=1 off=1 dt=0us
  ev1  FALLING ts=2.215561600 seq=2 line_seq=2 off=1 dt=2780us
  ev2  RISING  ts=2.215888304 seq=3 line_seq=3 off=1 dt=326us
  ev3  FALLING ts=2.221261920 seq=4 line_seq=4 off=1 dt=5373us
  …
  ev11 FALLING ts=2.223283392 seq=12 line_seq=12 off=1 dt=222us
got=12 rising=6 falling=6 missing=0 backwards=0 same_dir=0 causal=12
```

읽는 법:

- `ts` 는 기본 `CLOCK_MONOTONIC` (`line_event_timestamp()` → `ktime_get_ns()`), 부팅 후 2.2초. `EVENT_CLOCK_REALTIME` 플래그면 벽시계(P2), `EVENT_CLOCK_HTE` 면 하드웨어 타임스탬프 엔진.
- 라인 하나만 요청하면 `seqno == line_seqno` (`le.seqno = (lr->num_lines == 1) ? le.line_seqno : ...`). 여러 라인이면 `seqno` 는 요청 전체 순서, `line_seqno` 는 라인별 순서.
- `causal=12`: 매 이벤트의 ts 가 "pull 쓰기 직전 ~ 이벤트를 받은 직후" 사이 — 우리 프로그램의 `clock_gettime(CLOCK_MONOTONIC)` 과 커널 이벤트 ts 가 **같은 시계**라 직접 비교된다. dmesg 시각(`local_clock`)과는 비교하면 안 된다.
- **왜 closed loop 인가.** 다음 엣지를 쓰기 전에 이전 이벤트를 받았으니, 위 그림의 합쳐짐 ①② 가 일어날 틈이 없다. 그래서 "12개, 교대, 연속" 을 **타이밍 없이** 단정할 수 있다. 반대로 쉬지 않고 쓰면(open loop) 어떻게 되는지는 Part B 끝에서 본다.

### Part B — 커널이 거절하는 것, 잃는 것

**에러 표** (모두 실제 출력, 소스 위치는 랩 트리):

| 상황 | 결과 | 어디서 |
|---|---|---|
| 잡힌 LED0 을 또 요청 | `-16 (Device or resource busy)` | `gpiolib.c` `gpiod_request_commit`: `test_and_set_bit(GPIOD_FLAG_REQUESTED)` |
| 입력 라인에 SET_VALUES | `-1 (Operation not permitted)` | `gpiolib.c`: `!test_bit(GPIOD_FLAG_IS_OUT)` → `-EPERM` |
| EDGE_RISING 인데 INPUT 없음 | `-22 (Invalid argument)` | `gpio_v2_line_flags_validate`: "Edge detection requires explicit input." |
| INPUT \| OUTPUT | `-EINVAL` | 같은 함수: 모순되는 방향 |
| offset 99 (8라인 칩) | `-22 (Invalid argument)` | `gpio_device_get_desc`: `hwnum >= gdev->ngpio` |
| `num_lines` 0 또는 65 | `-EINVAL` | `linereq_create` 첫 검사 (65 는 우리 코드가 배열 넘침 전에 먼저 거절) |
| LINEINFO padding ≠ 0 | `-22 (Invalid argument)` | `lineinfo_get`: `!mem_is_zero(lineinfo.padding)` |
| SET_VALUES mask = 0 | `-EINVAL` | `linereq_set_values`: `num_set == 0` |

uAPI 가 padding 0 을 강제하는 이유: 나중에 그 자리에 새 필드를 넣어도 옛 프로그램(0 을 넣던)과 구분할 수 있게. 그래서 **memset 은 선택이 아니다**.

**B4 FIFO 넘침 — 가장 오래된 것부터.** `linereq_create()` 에서 `event_buffer_size` 가 0 이면 `num_lines × 16`, 최대 `64 × 16 = 1024`. 그 값이 `kfifo_alloc()` 으로 가고 `__kfifo_alloc()` 은 `roundup_pow_of_two(size)` 를 한다 → 5 를 요청하면 8. 꽉 찬 상태에서 새 이벤트가 오면 `linereq_put_event()` 가 `kfifo_skip()` 으로 **맨 앞(가장 오래된)** 을 버리고 넣는다.

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">event_buffer_size=5 → kfifo 8칸, 읽지 않고 24엣지 (실측)</text>
  <rect x="10" y="40" width="400" height="40" class="f-warn" stroke="currentColor"/>
  <text x="210" y="65" fill="currentColor" text-anchor="middle">line_seqno 1 .. 16 — kfifo_skip 으로 버려짐 (16개)</text>
  <rect x="410" y="40" width="320" height="40" class="f-ok" stroke="currentColor"/>
  <text x="570" y="65" fill="currentColor" text-anchor="middle">17 .. 24 — read() 가 받은 8개</text>
  <text x="10" y="110" fill="currentColor">read=8 first line_seq=17 last=24 gaps_inside=0 dropped=16</text>
  <text x="10" y="134" fill="currentColor">· 받은 8개 사이엔 공백 없음 (가장 오래된 쪽만 잘린다)</text>
  <text x="10" y="156" fill="currentColor">· 잃은 수 = 첫 line_seqno − 1 = 16 → 사용자공간이 번호만으로 유실을 안다</text>
  <text x="10" y="178" fill="currentColor">· 넘침은 dmesg 에 pr_debug_ratelimited 로만 남는다 (기본 꺼짐) — 번호가 유일한 증거</text>
</svg>
```

이 테스트의 엣지 사이 `usleep(5000)` 은 엣지를 합쳐지지 않게 하는 속도 조절일 뿐이고, 판정은 seqno 관계(8개, 내부 공백 0, 마지막−처음 = 7, dropped = 처음−1 > 0)로만 한다. 그래서 몇 엣지가 합쳐져도 결론은 같다.

**B5 소프트웨어 디바운스.** gpio-sim 의 `set_config` 는 bias(pull-up/down)만 받고 나머지는 `-ENOTSUPP`. `debounce_setup()` 은 하드웨어 디바운스를 먼저 시도하고 `-ENOTSUPP` 이면 **소프트웨어 디바운스**로 바꾼다: 엣지마다 `debounce_irq_handler` 가 `mod_delayed_work(…, debounce_period)` 로 작업을 미루고, 조용해진 뒤 `debounce_work_func()` 가 레벨을 읽어 바뀌었으면 이벤트 하나를 만든다. 실측:

```text
9 bouncy writes -> 1 event(s), first RISING line_seq=1, 309 ms after the last write
```

- 9번 쓰기(마지막 up) → 이벤트 1개, RISING. LINEINFO 의 attrs 에도 `debounce_period_us=300000` 이 보인다(테스트가 확인).
- 이 경로에서는 **타임스탬프가 작업(work)이 돌 때** 찍힌다 (`le.timestamp_ns = line_event_timestamp(line)` 이 work 함수 안). 그래서 "마지막 엣지 + 300 ms" 근처(309 ms, TCG). 디바운스를 쓰면 엣지 시각 정밀도를 포기한다는 뜻이다.
- 이 309 ms 는 정보로만 출력하고 판정은 "1개, RISING, 마지막 쓰기 이후" 로 한다.

**open loop 에서 엣지가 사라지는 모습** (`check.sh demo`, 채점 안 함; 네 번 돌린 실행에서 고른 실제 줄, `run_demo.txt` 는 마지막 실행):

```text
burst K=20    generated(last line_seqno)=3     delivered=3     merged=17   same_dir=2
burst K=20    generated(last line_seqno)=20    delivered=20    merged=0    same_dir=0
burst K=200   generated(last line_seqno)=8     delivered=8     merged=192  same_dir=4
burst K=1000  generated(last line_seqno)=23    delivered=23    merged=977  same_dir=18
```

`pwrite` 를 쉬지 않고 K번 하면 이벤트가 K보다 훨씬 적게 **만들어진다**(FIFO 는 1024 라 넘치지 않았다: delivered = generated). 같은 K=20 인데 3 과 20 이 나올 만큼 실행마다 다르다. 원인은 그림의 합쳐짐 ①: `irq_sim_set_irqchip_state()` 가 `assign_bit(hwirq, pending)` 으로 라인당 비트 하나만 세우므로, `irq_work` 가 돌기 전의 여러 쓰기는 하나가 된다. TCG 에서 self-IPI/irq_work 가 늦게 도는 것이 크게 보탠다고 추정한다(측정으로 분리하지는 않았다). `same_dir` > 0 은 BOTH 모드에서 id 를 "스레드가 돌 때의 레벨" 로 정하기 때문에 생긴다 — 엣지를 잃으면 RISING 이 두 번 연속 나올 수 있다. 실제 SoC GPIO 도 엣지 래치는 보통 1비트라 같은 종류의 합쳐짐이 있다. **"엣지 수 = 이벤트 수" 는 엣지 간격이 처리 시간보다 길 때만 성립한다.**

**sysfs GPIO 는 왜 폐기됐나.** 이 커널에서 실제로 본 것:

```text
=== legacy sysfs GPIO? ===
ls: /sys/class/gpio: No such file or directory
CONFIG_GPIO_CDEV=y
CONFIG_GPIO_SIM=y
CONFIG_HTE=y
lines mentioning GPIO_SYSFS in /proc/config.gz: 0
```

6.18 `drivers/gpio/Kconfig` 의 `GPIO_SYSFS` 는 `bool "..." if EXPERT` 이고 도움말이 "This ABI is deprecated" 다 (`Documentation/ABI/obsolete/sysfs-gpio`). 구조적 이유:

| sysfs (`/sys/class/gpio`) | chardev uAPI v2 |
|---|---|
| 전역 번호(`echo 523 > export`) — 프로브 순서에 따라 바뀜 | 칩 + offset, 이름으로 찾기 |
| export 한 프로세스가 죽어도 라인은 export 상태 | fd 가 소유권, 닫히면(죽으면) 자동 해제 (B1 에서 `USED` 사라짐) |
| 파일 하나 = 라인 하나, 여러 라인 동시 변경 불가 | 최대 64라인 한 요청, mask 로 원자적 SET (C1) |
| `direction` → `value` 사이 글리치 | 초기값을 요청과 함께 |
| `poll(POLLPRI)` = "바뀌었다" 뿐 | 큐 + ts + seqno + 방향 |
| bias / drive / debounce 설정 불가 | 플래그와 속성으로 요청 |

### Part C — 브링업에서 쓰는 모양

**C1 4비트 버스.** `set(mask=0xF, bits=0xA)` 후 sim 패턴 `0xA`, 다시 `set(mask=0x1, bits=0x1)` → 나머지 셋은 그대로라 `get(0xF) = 0xB`. 커널은 `linereq_set_values()` 에서 mask 에 든 라인만 모아 `gpiod_set_array_value_complex()` 한 번으로 넘긴다; 칩이 `set_multiple` 을 가지면(gpio-sim 은 `gpio_sim_set_multiple()`, 락 하나 안에서 `bitmap_replace`) 진짜로 한 번에 바뀐다. 실제 SoC 에서 같은 GPIO 뱅크 레지스터에 있는 핀들일 때만 "같은 클록에" 바뀐다는 점은 기억해 둘 것 (뱅크가 다르면 순차).

**C2 리셋 → DRDY.**

```svg
<svg viewBox="0 0 740 210" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">IMU 리셋 시퀀스 (물리 레벨, 실측 22 ms)</text>
  <text x="10" y="62" fill="currentColor" font-family="monospace">RST_N</text>
  <polyline points="80,50 160,50 160,80 460,80 460,50 720,50" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="300" y="100" fill="currentColor" text-anchor="middle" font-size="12">논리 1 = 물리 0 (ACTIVE_LOW) — 100 ms</text>
  <text x="10" y="142" fill="currentColor" font-family="monospace">DRDY</text>
  <polyline points="80,160 590,160 590,130 720,130" fill="none" stroke="currentColor" stroke-width="2"/>
  <line x1="460" y1="40" x2="460" y2="175" stroke="currentColor" stroke-dasharray="4 3"/>
  <line x1="590" y1="120" x2="590" y2="175" stroke="currentColor" stroke-dasharray="4 3"/>
  <rect x="462" y="168" width="126" height="22" class="f-acc" stroke="currentColor"/>
  <text x="525" y="184" fill="currentColor" text-anchor="middle" font-size="12">22 ms (≥ 20 ms)</text>
  <text x="160" y="42" fill="currentColor" text-anchor="middle" font-size="12">요청(초기값 1)</text>
  <text x="460" y="34" fill="currentColor" text-anchor="middle" font-size="12">set 0 (trel)</text>
  <text x="640" y="122" fill="currentColor" text-anchor="middle" font-size="12">RISING 이벤트 ts</text>
</svg>
```

```text
DRDY events=1 id=RISING, 22 ms after reset release, sensor exit=0
```

- `IMU_RST_N` 은 guest.sh 가 sim `pull-up` 으로 만들어 둔 "보드 풀업" 이라 요청 전 sim value 는 1(리셋 해제 상태). `ACTIVE_LOW` + 초기 논리 1 로 요청하자 sim value 0 — 드라이버/앱은 "assert" 만 생각하고 극성은 플래그가 맡는다. `gpio_get()` 도 논리값 1 을 준다.
- 해제 시각 `trel` 과 DRDY 이벤트 ts 가 같은 `CLOCK_MONOTONIC` 이라 빼기만 하면 된다. 센서 쪽은 해제를 본 **뒤에** 20 ms 자므로 `≥ 20 ms` 는 물리적으로 보장되는 하한이다 (그래서 이 부등식은 판정에 써도 타이밍 의존이 아니다).
- 실제 보드에서는 DRDY 를 놓치지 않으려고 **DRDY 를 먼저 요청하고 나서** 리셋을 푼다(코드도 그 순서). 반대로 하면 해제 직후 올라온 엣지가 요청 전이라 영영 안 온다.
- fd 를 닫으면 RST_N 은 다시 풀업(1)으로 — 프로세스가 죽어도 센서가 리셋에 묶이지 않는다. 반대로 "앱이 죽으면 반드시 리셋" 이 필요하면 하드웨어 풀다운을 달거나 커널 드라이버/hog 가 소유하게 해야 한다.

### 참고: 같은 일을 libgpiod v2 로 (이 VM 엔 헤더가 없어 컴파일 안 함)

함수 이름은 VM 의 `libgpiod.so.3` 에서 `nm -D` 로 존재를 확인했다. 시그니처와 enum 이름은 libgpiod 2.x 문서 기준 기억이므로 실제로 빌드할 때 `gpiod.h` 와 대조할 것.

```c
#include <gpiod.h>	/* 참고 코드 — 컴파일하지 않았다 */

struct gpiod_chip *chip = gpiod_chip_open("/dev/gpiochip1");
int off = gpiod_chip_get_line_offset_from_name(chip, "BTN0");          /* TODO 1 */
struct gpiod_line_settings *ls = gpiod_line_settings_new();
gpiod_line_settings_set_direction(ls, GPIOD_LINE_DIRECTION_INPUT);
gpiod_line_settings_set_edge_detection(ls, GPIOD_LINE_EDGE_BOTH);
struct gpiod_line_config *lc = gpiod_line_config_new();
unsigned int offs[1] = { (unsigned int)off };
gpiod_line_config_add_line_settings(lc, offs, 1, ls);
struct gpiod_request_config *rc = gpiod_request_config_new();
gpiod_request_config_set_consumer(rc, "b10-btn");
struct gpiod_line_request *req = gpiod_chip_request_lines(chip, rc, lc); /* TODO 2 */
struct gpiod_edge_event_buffer *buf = gpiod_edge_event_buffer_new(16);
if (gpiod_line_request_wait_edge_events(req, 2000000000LL) > 0) {        /* TODO 4: poll */
	int n = gpiod_line_request_read_edge_events(req, buf, 16);         /*         read */
	for (int i = 0; i < n; i++) {
		struct gpiod_edge_event *e = gpiod_edge_event_buffer_get_event(buf, i);
		printf("%s ts=%llu line_seq=%lu\n",
		       gpiod_edge_event_get_event_type(e) == GPIOD_EDGE_EVENT_RISING_EDGE ?
		       "RISING" : "FALLING",
		       (unsigned long long)gpiod_edge_event_get_timestamp_ns(e),
		       gpiod_edge_event_get_line_seqno(e));
	}
}
/* 출력: gpiod_line_request_set_value(req, off, GPIOD_LINE_VALUE_ACTIVE);  (TODO 3a) */
gpiod_edge_event_buffer_free(buf);
gpiod_line_request_release(req);
gpiod_request_config_free(rc);
gpiod_line_config_free(lc);
gpiod_line_settings_free(ls);
gpiod_chip_close(chip);
```

| 우리 함수 | libgpiod v2 | 도구 |
|---|---|---|
| `gpio_find_line` | `gpiod_chip_get_line_offset_from_name` | `gpioinfo`, `gpiofind`(v1) |
| `gpio_request` | `gpiod_chip_request_lines` (settings → line_config → request_config) | `gpioget/gpioset/gpiomon` 내부 |
| `gpio_set/get` | `gpiod_line_request_set_values_subset / get_values_subset` | `gpioset`, `gpioget` |
| `gpio_wait_events` | `wait_edge_events` + `read_edge_events` | `gpiomon` |
| (P3) watch | `gpiod_chip_watch_line_info` | `gpionotify` |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B10/check.sh solution
```

실제 결과: `61/61 passed`, `PASS kernel log clean`, `RESULT: PASS (solution)`, exit 0. 연속 3회 모두 PASS, 각 7–9초 (게스트 부팅 약 1.5초 포함, TCG).

판정 스크립트:

<!-- code: check.sh -->

## 7. 자주 하는 실수

1. **LINEINFO/요청 구조체를 memset 하지 않음** → 스택 쓰레기가 `padding` 에 들어가 `-EINVAL` (B2 에서 실측). 가끔만 실패해서 더 고약하다. 고침: `memset(&li, 0, sizeof li)` 후 필드 채우기.
2. **mask 를 offset 으로 계산** (`mask = 1 << offset`) → BUS0(offset 4) 에 `1<<4` 를 넣으면 5번째 요청 라인이 없어 아무것도 안 바뀌거나 `-EINVAL`. 고침: 비트 i = `offsets[i]`.
3. **출력을 INPUT 으로 요청한 뒤 SET_CONFIG/두 번째 단계에서 출력으로 바꾸고 값 쓰기** → 그 사이 이전 레벨이 나간다. 고침: OUTPUT + OUTPUT_VALUES 속성을 한 요청에.
4. **`read()` 한 번 = 이벤트 하나라고 가정** → 버퍼를 48 B 로 주면 괜찮지만 큰 버퍼를 주면 여러 개가 온다; 반대로 48 B 보다 작으면 `-EINVAL` (`linereq_read`: `count < sizeof(le)`). 고침: `got / sizeof(*ev)`.
5. **"엣지 N 번 = 이벤트 N 개" 로 테스트를 짬** → open loop 에서는 합쳐진다(실측 K=200 → 7~86). 고침: closed loop 로 쓰거나, 판정을 `line_seqno` 관계로.
6. **`gpioset` / 요청 fd 를 닫고 값이 유지될 거라 기대** → 해제되면 sim value 는 pull 로 돌아간다 (B1 실측). 값을 유지하려면 프로세스가 fd 를 쥐고 있어야 한다.
7. **이벤트 ts 를 dmesg 시각이나 `CLOCK_REALTIME` 과 비교** → 기본은 `CLOCK_MONOTONIC`. 벽시계가 필요하면 `EVENT_CLOCK_REALTIME` 을 요청(P2). NTP 점프가 있는 시계로 간격을 재면 안 된다.
8. **디바운스 이벤트의 ts 를 엣지 시각으로 씀** → 소프트웨어 디바운스는 작업 실행 시각(실측 마지막 엣지 + 309 ms).

## 8. 면접 Q&A

<details><summary>Q1. /sys/class/gpio 는 왜 폐기됐고 대신 무엇을 쓰나요?</summary>

전역 번호 의존, 소유권이 프로세스에 묶이지 않음(죽어도 export 유지), 라인 하나씩만, 방향→값 사이 글리치, 이벤트가 "바뀜" 뿐(큐·ts·순서 없음), bias/debounce 설정 불가. 대신 `/dev/gpiochipN` chardev uAPI v2 (5.10~) 와 그 래퍼 libgpiod. 6.18 에서 `GPIO_SYSFS` 는 `if EXPERT` 뒤에 숨어 기본으로는 빌드조차 안 된다(랩 커널 `/proc/config.gz` 에 줄이 0개).

**꼬리질문**: 그럼 sysfs 가 아직 쓰이는 곳은? → 오래된 스크립트·BSP 호환용. 6.18 Kconfig 에는 `GPIO_SYSFS_LEGACY` ("global GPIO numberspace-based functionalities", `GPIO_SYSFS` 를 켜면 기본 y)가 따로 있어, 전역 번호 부분을 떼어 낼 준비가 돼 있다. 새 형태의 세부 레이아웃은 이 레슨에서 켜 보지 않았다.
</details>

<details><summary>Q2. 이벤트 타임스탬프는 어디서, 어떤 시계로 찍히나요?</summary>

일반 경로는 hard IRQ 핸들러(`edge_irq_handler`)에서 `ktime_get_ns()` = `CLOCK_MONOTONIC`. 그래서 사용자공간이 늦게 읽어도 엣지 간격은 정확하다. `EVENT_CLOCK_REALTIME` 이면 `ktime_get_real_ns()`, `EVENT_CLOCK_HTE` 면 하드웨어 타임스탬프 엔진(Tegra 등). 소프트웨어 디바운스 경로는 work 실행 시각이다.

**꼬리질문**: HTE 플래그를 HTE 장치 없는 보드에서 쓰면? → 이 커널(CONFIG_HTE=y)에서 실측 `-19 (No such device)` + dmesg `hte_ts_get() no hte dev found`.
</details>

<details><summary>Q3. seqno 와 line_seqno 의 차이, 그리고 유실을 어떻게 알아내나요?</summary>

`line_seqno` 는 라인별, `seqno` 는 한 요청(여러 라인) 전체의 순서. 라인 하나면 같다. 번호는 스레드에서 이벤트를 **만들 때** 붙으므로 FIFO 에서 버려진 이벤트는 번호 공백으로 드러난다(실측: 17..24 수신 → 16개 유실). 반면 IRQ 단계에서 합쳐진 엣지는 번호를 받지 못해 공백이 없다 — 이건 "토글 수 − 마지막 번호" 처럼 바깥 정보가 있어야 안다.

**꼬리질문**: FIFO 가 넘치면 새 것과 옛 것 중 무엇을 버리나? → 옛 것(`kfifo_skip`). 최신 상태가 더 중요하다는 설계.
</details>

<details><summary>Q4. event_buffer_size 를 5 로 주면 실제 몇 개가 들어가나요?</summary>

8. `kfifo_alloc` → `roundup_pow_of_two`. 0 이면 `num_lines × 16`, 상한 `64 × 16 = 1024`. 실측 read=8.

**꼬리질문**: 버퍼를 키우면 유실이 없어지나? → 평균 처리 속도가 엣지 속도보다 느리면 버퍼는 지연만 늘릴 뿐 결국 넘친다. 근본 해결은 빨리 읽기(전용 스레드, RT 우선순위) 또는 커널 드라이버로 내리기.
</details>

<details><summary>Q5. 출력 라인의 초기값을 왜 요청과 같이 줘야 하나요?</summary>

두 단계면 방향 전환 순간 이전/기본 레벨이 나간다(글리치). 리셋·전원 enable·모터 브레이크 핀에서 실제 오동작. uAPI v2 는 `OUTPUT_VALUES` 속성으로 요청과 함께, 커널 쪽은 `gpiod_direction_output(desc, value)` 가 같은 이유로 값을 인자로 받는다.

**꼬리질문**: 부팅 중(앱이 뜨기 전) 핀 상태는 누가 정하나? → 패드 기본값·외부 풀업/다운·pinctrl 기본 state, 필요하면 DT `gpio-hog`. 사용자공간은 너무 늦다.
</details>

<details><summary>Q6. active-low 는 어디서 처리되나요?</summary>

gpiolib 이 `GPIOD_FLAG_ACTIVE_LOW` 로 논리↔물리를 바꾼다. 사용자공간 요청 플래그 `GPIO_V2_LINE_FLAG_ACTIVE_LOW` 도 같은 비트를 세운다. C2 에서 논리 1 → sim value 0. 엣지도 논리 기준이라 active-low 라인의 "RISING" 은 물리 하강이다 (`edge_detector_setup` 이 IRQF_TRIGGER 를 뒤집는다).

**꼬리질문**: DT 에서는? → `reset-gpios = <&gpio1 5 GPIO_ACTIVE_LOW>;` 처럼 소비자 노드에 적고, 드라이버는 `gpiod_set_value(reset, 1)` = "리셋 걸기" 로 쓴다.
</details>

<details><summary>Q7. 빠른 엣지를 놓치는 곳은 어디어디인가요?</summary>

(1) 하드웨어/irqchip 의 엣지 래치(보통 1비트) — gpio-sim 에서는 `irq_sim` pending 비트. (2) threaded IRQ 의 깨움 비트 `IRQTF_RUNTHREAD` (ELX-M05 Part B). (3) cdev 의 kfifo 넘침. (1)(2) 는 합쳐짐, (3) 은 버림. 실측 open loop K=1000 → 23~508 개만 생성.

**꼬리질문**: 엔코더 같은 kHz 신호를 세려면? → 사용자공간 GPIO 이벤트가 아니라 하드웨어 카운터(타이머 캡처, eQEP, `counter` 서브시스템)나 커널 드라이버.
</details>

<details><summary>Q8. gpio-sim 은 어떻게 쓰고 실제 하드웨어와 무엇이 다른가요?</summary>

configfs 에 칩/뱅크/라인 디렉터리를 만들고 `live=1` → platform 장치 `gpio-sim.0` + `/dev/gpiochipN`. `sim_gpioK/pull` 쓰기가 외부 입력, `value` 는 읽기 전용(쓰면 `write error: Operation not permitted`, 실측). 다른 점: `can_sleep = true`(mutex 보호, I2C 확장기 같은 성격), 하드웨어 디바운스 없음, IRQ 는 irq_work 로 소프트웨어 발생이라 타이밍이 실제와 다르다. CI 에서 libgpiod·드라이버 테스트용으로 쓴다.

**꼬리질문**: 커널 드라이버를 gpio-sim 라인에 붙이려면? → `gpiod_lookup` 테이블이나 DT `compatible = "gpio-simulator"` (ELX-M05 가 lookup 방식).
</details>

## 9. 추가 연습

<details><summary>P1. 잡고 있는 라인을 놓지 않고 출력 → 입력+엣지로 바꾸기 (SET_CONFIG)</summary>

`gpio_reconfigure(lfd, flags)`: `struct gpio_v2_line_config c; memset; c.flags = flags; ioctl(lfd, GPIO_V2_LINE_SET_CONFIG_IOCTL, &c)`. 바꾼 뒤 LINEINFO 에 INPUT·EDGE_RISING, consumer 는 그대로, sim pull-up → 같은 fd 로 RISING 이벤트. 엣지 플래그만 주고 INPUT 을 빼면 요청 때와 같은 검증(`gpio_v2_line_config_validate`)으로 `-EINVAL`. 해제/재요청 사이에 다른 프로세스가 라인을 가로챌 틈이 없다는 것이 핵심.
</details>

<details><summary>P2. EVENT_CLOCK_REALTIME</summary>

플래그를 더해 요청 → LINEINFO flags 에 `EVENT_CLOCK_REALTIME`, 이벤트 ts 가 `clock_gettime(CLOCK_REALTIME)` 앞뒤 구간 안. 실측: `realtime ts=1794784350063909840` (벽시계 ns) vs `monotonic now ~6525333840`. 로그를 다른 장비와 맞출 때는 좋지만 NTP 보정에 흔들리므로 간격 측정은 MONOTONIC.
</details>

<details><summary>P3. 라인 감시: 누가 내 핀을 가져갔나</summary>

칩 fd 에 `GPIO_V2_GET_LINEINFO_WATCH_IOCTL` (같은 라인 두 번 → `-EBUSY`, 실측). 그 뒤 요청 → 재설정(ACTIVE_LOW) → 해제를 하면 칩 fd 로 `gpio_v2_line_info_changed` 3개: 실측 `type=1 consumer="b10-watched" flags=0x5` (REQUESTED, USED|INPUT), `type=3 flags=0x7` (CONFIG, +ACTIVE_LOW), `type=2 consumer="" flags=0x4` (RELEASED). 끝나면 `GPIO_GET_LINEINFO_UNWATCH_IOCTL`. `gpionotify` 가 이것.
</details>

<details><summary>P4. 모든 칩에서 이름으로 찾기 (gpiofind)</summary>

`/dev` 에서 `gpiochip*` 를 열고 TODO 1 을 칩마다 호출, 찾으면 경로를 돌려준다. 실측 `IMU_DRDY -> /dev/gpiochip1 offset 3`, 없는 이름은 `-ENOENT`. 이름은 시스템 전체에서 유일하다는 보장이 없으므로(칩마다 따로) 실무에서는 첫 번째 일치를 쓰고 중복을 경고하는 편이 낫다.
</details>

<details><summary>P5 (생각만). 버튼 길게 누르기 감지</summary>

디바운스 20 ms 로 BOTH 를 받고, RISING ts 와 다음 FALLING ts 차이가 1 s 이상이면 long-press. ts 가 hard IRQ(또는 디바운스 work) 기준이라 앱이 바빠도 측정이 정확하다 — 단, 디바운스 경로의 ts 는 둘 다 같은 만큼 밀리므로 차이는 맞다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B10/check.sh practice
```

실제 결과: `75/75 passed`, `RESULT: PASS (practice)`, exit 0 (본문 61 + P1–P4 14).

관찰용 데모 (채점 안 함, 위 burst·sysfs·debugfs·HTE 출력의 출처):

```bash
limactl shell elinux -- bash /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/ELX/ELX-B10/check.sh demo
```

<!-- code: demo_burst.c -->

라인을 쥐고 있는 동안의 debugfs (`run_demo.txt` 에서):

```text
gpiochip1: 8 GPIOs, parent: platform/gpio-sim.0, b10-sim, can sleep:
 gpio-0   (b10-led) output-high,pull-down
 gpio-1   (b10-btn) input,pull-down
 gpio-2   (<unused>) input,pull-up
 …
/proc/interrupts lines with b10 in the name:
 22:          0          0  irq_sim   1 Edge      b10-btn
```

엣지 요청을 하면 consumer 이름으로 진짜 IRQ(22, `irq_sim` 칩, hwirq 1)가 생기고, 스레드 이름은 `irq/22-b10-btn` 이 된다(`kernel/irq/manage.c` 의 `"irq/%d-%s"`, 이름 = consumer 라벨 — 소스로 확인, ps 로는 안 봄) — ELX-M05 에서 커널 드라이버가 같은 IRQ 를 직접 요청하던 것과 같은 자리다.

## 10. 핵심 정리

- 사용자공간 GPIO = `/dev/gpiochipN` + uAPI v2: CHIPINFO/LINEINFO(찾기) → GET_LINE(요청, fd = 소유권) → SET/GET_VALUES(mask = 요청 순서 비트) → poll+read(48 B 이벤트). libgpiod 는 이 래퍼.
- 구조체는 반드시 memset — padding ≠ 0 이면 `-EINVAL`. 초기 출력값·디바운스는 속성으로 **요청과 함께**.
- 이벤트 ts 는 hard IRQ 에서 `CLOCK_MONOTONIC`; 번호는 스레드에서. FIFO 넘침은 가장 오래된 것을 버리고 `line_seqno` 공백으로 드러난다(5 → 8칸, 17..24 수신).
- 합쳐짐(irq_sim pending 비트, RUNTHREAD 비트, 실제 HW 엣지 래치)은 번호도 못 받는다 → 테스트는 closed loop 로, 판정은 번호 관계로.
- 하드웨어 디바운스가 없으면(gpio-sim) cdev 가 소프트웨어 디바운스로 대체, ts 는 work 시각.
- sysfs GPIO 는 전역 번호·소유권 없음·글리치·빈약한 이벤트로 폐기; 6.18 에선 EXPERT 뒤.
- 브링업: DRDY 를 먼저 요청하고 리셋을 풀어라; active-low 는 플래그에 맡겨라; fd 를 닫으면 핀은 pull 로 돌아간다.

## 참고자료

- 랩 커널 소스 (Linux 6.18.55, `/var/lab/src/linux-6.18.55`): `drivers/gpio/gpiolib-cdev.c` (`linereq_create`, `edge_irq_handler/thread`, `debounce_setup`, `linereq_put_event`, `lineinfo_get`), `drivers/gpio/gpio-sim.c` (`gpio_sim_apply_pull`, `gpio_sim_free`, `set_config`), `kernel/irq/irq_sim.c`, `lib/kfifo.c`, `include/uapi/linux/gpio.h`, `drivers/gpio/Kconfig` (`GPIO_SYSFS`)
- Documentation: `Documentation/userspace-api/gpio/chardev.rst`, `Documentation/admin-guide/gpio/gpio-sim.rst`, `Documentation/ABI/obsolete/sysfs-gpio` — https://docs.kernel.org/userspace-api/gpio/chardev.html , https://docs.kernel.org/admin-guide/gpio/gpio-sim.html
- libgpiod (v2 API, 도구 gpiodetect/gpioinfo/gpioget/gpioset/gpiomon/gpionotify): https://git.kernel.org/pub/scm/libs/libgpiod/libgpiod.git/
- 배경 노트: EmbeddedLinux `H_hw_access_buses/2026-10-10_H4_pinctrl_gpio.md` (§7 uAPI, §8 유실 측정)
- 관련 레슨: ELX-M05 (gpio-sim 라인을 커널 IRQ 로, RUNTHREAD 합쳐짐), ELX-B07 (문자 디바이스 read/poll 의미), ELX-B08 (ioctl 번호·고정 폭 구조체), ELX-B09 (sysfs 속성), ELX-B11 (사용자공간 I2C/SPI), BM-B11 (레지스터 수준 GPIO 드라이버)
