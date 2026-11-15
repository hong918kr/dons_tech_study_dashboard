---
id: COMM-M09
time: 90분
updated: 2026-11-11
---
# COMM-M09 SocketCAN 실습: vcan, candump, C 소켓 프로그램

## 학습 목표

- `socket(PF_CAN, SOCK_RAW, CAN_RAW)` → `bind(ifindex)` → `write(struct can_frame)` / `poll` + `recvmsg` 로 CAN 프레임을 주고받는 C 코드를, 아무것도 막히지 않게(타임아웃) 짠다.
- `CAN_RAW_FILTER` 의 `can_id / can_mask` 규칙을 비트 단위로 설명하고, **"0x123 만 받으려 했는데 EFF 0x00000123 과 RTR 도 들어온다"** 함정을 커널에서 직접 재현한 뒤 고친다. 사용자 공간 모델과 실제 커널 전달을 랜덤 2560 프레임으로 대조한다.
- 에러 프레임(`CAN_RAW_ERR_FILTER`), 루프백(`CAN_RAW_LOOPBACK`, `CAN_RAW_RECV_OWN_MSGS`), 수신 큐 넘침(`SO_RXQ_OVFL`)이 어디서 어떻게 보이는지 실측으로 말한다.
- CAN FD 소켓(`CAN_RAW_FD_FRAMES`, `struct canfd_frame` 72 B)이 **소켓 옵션 × 인터페이스 MTU × 쓰기 크기** 세 가지가 맞아야 동작함을 EINVAL 로 확인한다.
- candump/cansend/cangen 출력을 읽고, 내 C 포매터가 `candump -L` 과 글자 하나까지 같은 줄을 찍는지 검증한다. vcan 과 실제 `can0` 의 차이(비트레이트, ACK, bus-off)를 설명한다.

## 1. 문제

로봇·차량 펌웨어 면접에서 "리눅스 쪽 CAN 은 어떻게 다뤄 봤어요?" 다음에 오는 질문은 거의 정해져 있다 — **필터 마스크를 써 보라, 내 프레임이 나한테도 오나, FD 는 왜 EINVAL 이 나나, 프레임이 빠지면 어디서 빠지나.** 이 레슨은 그 네 가지를 진짜 리눅스 커널(Lima VM `elinux`, Ubuntu, 커널 7.0)에서 돌려 확인한다. 맥에서는 `linux/can.h` 가 없어서 빌드조차 안 된다.

COMM-M04~M08 은 CAN 을 비트 수준 시뮬레이터로 봤다(프레임·스터핑 M04, 중재 M05, 비트 타이밍 M06, 에러 카운터 M07, FD M08). 실제 제품에서는 그 일을 CAN 컨트롤러(STM32 bxCAN/FDCAN, NXP FlexCAN, MCP2518FD …)가 하고, 리눅스가 돌아가는 보드(i.MX, Jetson, Raspberry Pi + MCP2515 HAT)에서는 **SocketCAN** 이 그 컨트롤러를 네트워크 인터페이스(`can0`)로 보여 준다. 응용 프로그램은 이더넷처럼 소켓을 연다. 소프트웨어 전용 가상 인터페이스 `vcan0` 은 같은 API 를 하드웨어 없이 제공한다 — 단위 테스트, CI, 로그 재생(`canplayer`)에 쓰는 바로 그것이다.

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">SocketCAN 스택: 이 레슨의 프로세스 하나 = 소켓 여러 개</text>
  <rect x="20" y="30" width="700" height="78" class="f-mut" stroke="currentColor"/>
  <text x="30" y="47" fill="currentColor">사용자 공간 (solution.c 한 프로세스)</text>
  <rect x="35" y="56" width="150" height="42" class="f-acc" stroke="currentColor"/>
  <text x="45" y="73" fill="currentColor">tx: bind(vcan0)</text><text x="45" y="89" fill="currentColor">write(can_frame 16 B)</text>
  <rect x="200" y="56" width="160" height="42" class="f-ok" stroke="currentColor"/>
  <text x="210" y="73" fill="currentColor">rx: CAN_RAW_FILTER</text><text x="210" y="89" fill="currentColor">poll → recvmsg</text>
  <rect x="375" y="56" width="160" height="42" class="f-ok" stroke="currentColor"/>
  <text x="385" y="73" fill="currentColor">frx: CAN_RAW_FD_FRAMES</text><text x="385" y="89" fill="currentColor">read → 16 또는 72 B</text>
  <rect x="550" y="56" width="160" height="42" class="f-warn" stroke="currentColor"/>
  <text x="560" y="73" fill="currentColor">erx: ERR_FILTER 만</text><text x="560" y="89" fill="currentColor">에러 프레임 감시</text>
  <rect x="20" y="128" width="700" height="96" class="f-mut" stroke="currentColor"/>
  <text x="30" y="145" fill="currentColor">커널: af_can (PF_CAN) + can-raw</text>
  <text x="40" y="166" fill="currentColor">can_send(): 크기·len 검사 (16 B 클래식 / 72 B FD, MTU 확인) → 실패면 EINVAL</text>
  <text x="40" y="184" fill="currentColor">   loopback 켜짐 + 드라이버가 IFF_ECHO 없음 → 복제본을 바로 수신 경로로 (vcan 의 "버스" = 이것뿐)</text>
  <text x="40" y="202" fill="currentColor">can_rcv_filter(): 필터 리스트 대조 → raw_rcv() → 소켓 수신 큐 (sk_rcvbuf 넘치면 버림, sk_drops++)</text>
  <text x="40" y="218" fill="currentColor">RECV_OWN_MSGS 꺼짐이면 보낸 소켓은 자기 프레임을 건너뜀</text>
  <line x1="110" y1="98" x2="110" y2="128" stroke="currentColor" marker-end="url(#a1)"/>
  <line x1="280" y1="128" x2="280" y2="98" stroke="currentColor" marker-end="url(#a1)"/>
  <line x1="455" y1="128" x2="455" y2="98" stroke="currentColor" marker-end="url(#a1)"/>
  <line x1="630" y1="128" x2="630" y2="98" stroke="currentColor" marker-end="url(#a1)"/>
  <rect x="20" y="244" width="330" height="100" class="f-acc" stroke="currentColor"/>
  <text x="30" y="262" fill="currentColor" font-weight="bold">vcan0 (가상, mtu 72)</text>
  <text x="30" y="280" fill="currentColor">vcan_tx(): echo 모드 아님 → 통계만 세고 폐기</text>
  <text x="30" y="298" fill="currentColor">비트레이트·ACK·중재·에러 없음</text>
  <text x="30" y="316" fill="currentColor">새 vcan 기본 mtu = 2060 (CAN XL) — 실측</text>
  <text x="30" y="334" fill="currentColor">loopback 끄면 아무도 못 듣는다 (테스트로 확인)</text>
  <rect x="390" y="244" width="330" height="100" class="f-warn" stroke="currentColor"/>
  <text x="400" y="262" fill="currentColor" font-weight="bold">can0 (실제 컨트롤러 드라이버)</text>
  <text x="400" y="280" fill="currentColor">TX 메일박스 → 트랜시버 → CAN_H/CAN_L 버스</text>
  <text x="400" y="298" fill="currentColor">ACK 받은 뒤 echo skb 로 로컬 수신 (IFF_ECHO)</text>
  <text x="400" y="316" fill="currentColor">bitrate·sample-point (M06), bus-off (M07)</text>
  <text x="400" y="334" fill="currentColor">컨트롤러가 에러 프레임(CAN_ERR_*)을 만든다</text>
  <line x1="185" y1="224" x2="185" y2="244" stroke="currentColor" marker-end="url(#a1)"/>
  <line x1="555" y1="224" x2="555" y2="244" stroke="currentColor" marker-end="url(#a1)"/>
  <defs><marker id="a1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
</svg>
```

(커널 함수 이름 `can_send` / `can_rcv_filter` / `raw_rcv` / `vcan_tx` 는 net/can/af_can.c, raw.c, drivers/net/can/vcan.c 에 대한 기억이다 — 소스 확인 권장. 그림의 **동작**(루프백 끄면 수신 0, 자기 프레임 안 보임, mtu 2060 기본값)은 이 VM 에서 실측했다.)

### Part A — 소켓 열기 · 클래식 프레임 송수신 (A1–A3)

1. `can_open(ifname)`: `socket(PF_CAN, SOCK_RAW, CAN_RAW)` → `if_nametoindex` → `bind(struct sockaddr_can)`. 없는 인터페이스 → `-ENODEV`, 실패 시 fd 누수 없음. `ifname` 이 NULL/"" 이면 ifindex 0 = **모든 CAN 인터페이스**(연습 5).
2. `can_send(fd, id, data, len)`: `struct can_frame` 을 0 으로 채우고 `can_id`(EFF/RTR 플래그 포함)·`len`·`data` 를 넣어 **정확히 16 B** `write`. `len > 8` 은 커널이 `EINVAL` — 하지만 `data[8]` 로 복사할 때 넘치지 않게 하는 건 내 책임.
3. `can_recv(fd, cf, timeout_ms, &msg_flags)`: `poll` 로 기다리고 `recvmsg(MSG_DONTWAIT)` 로 읽는다. 반환 16(클래식) / 72(FD) / 0(타임아웃) / `-errno`. `msg_flags` 의 `MSG_DONTROUTE` = 이 호스트가 보낸 프레임, `MSG_CONFIRM` = **이 소켓이** 보낸 프레임.
4. 테스트: 0x123 3바이트, EFF 0x1ABCDEF0 8바이트, RTR, len 0..8 전부, len 9 → `-EINVAL`, 100 개 연속 순서 보존, 자기 프레임 안 보임 → `RECV_OWN_MSGS` 켜면 `MSG_CONFIRM` 과 함께 보임, `LOOPBACK` 끄면 vcan 에선 아무도 못 받음.

### Part B — 필터와 에러 프레임 (B1–B3, 면접관이 파는 곳)

1. `can_set_filters(fd, f, n)`: `CAN_RAW_FILTER`. 여러 개 = OR, **n = 0 = 아무것도 안 받음**, 새 소켓 기본 = `{0, 0}` = 전부. 최대 `CAN_RAW_FILTER_MAX` = 512 개(513 → `EINVAL`).
2. `can_set_err_mask(fd, m)`: `CAN_RAW_ERR_FILTER`. vcan 은 에러 프레임을 **스스로 만들지 않는다** → 테스트에서는 `CAN_ERR_FLAG | CAN_ERR_BUSOFF` 프레임을 직접 써서 주입(vcan 전용 실험).
3. `filter_match(rx_id, f)`: 커널 규칙의 사용자 공간 모델 — `(rx_id & mask) == (f_id & mask)`, `CAN_INV_FILTER` 면 반전, 그리고 "SFF 전용" 보정 하나.
4. 주입하고 단언할 것: `{0x123, 0x7FF}` 에 EFF 0x123·RTR 0x123 이 **통과**, `{0x123, 0x7FF|EFF|RTR}` 은 SFF 데이터 0x123 만, 범위 `{0x100, 0x700}`, 반전, 겹치는 필터 → **한 번만** 전달, 빈 필터, 513 개, 랜덤 필터 40 라운드 × 64 프레임에서 커널 == 모델, 에러 프레임은 에러 마스크가 있는 소켓에만(`{0,0}` 필터도 못 받음).

### Part C — CAN FD 와 candump 표기 (C1–C3)

1. `can_enable_fd(fd)`: `CAN_RAW_FD_FRAMES = 1`.
2. `can_send_fd(fd, id, data, len, flags)`: `struct canfd_frame` 72 B 쓰기, `flags` = `CANFD_BRS | CANFD_ESI`.
3. 실측으로 단언: 64 B + BRS → FD 소켓이 72 B 로 받음, FD 를 안 켠 소켓에는 **오지 않음**, FD 소켓이 클래식 프레임을 받으면 16 B (읽은 크기 = 프레임 종류), FD 안 켠 소켓의 72 B 쓰기 → `EINVAL`, MTU 16 인 vcan1 → `EINVAL`, FD 유효 길이 16 종 왕복, len 65 → `EINVAL`.
4. `can_fmt(cf, mtu, out, cap)`: `candump -L` / `cansend` 한 줄 표기 — `123#112233`, `1ABCDEF0#..`, `456#R2`, `321##5112233`, `20000040#00..`. 용량 부족 → `-ENOSPC`. demo.sh 가 같은 프레임을 진짜 `candump -L` 로 찍어 **글자 단위로 비교**한다.

## 2. Problem (English)

> **SocketCAN raw-socket helpers (Linux).** Using only the CAN_RAW socket API, implement:
>
> ```c
> int  can_open(const char *ifname);                               // fd or -errno; NULL/"" = all CAN ifaces
> int  can_send(int fd, canid_t id, const uint8_t *data, uint8_t len);   // classic frame, 0 or -errno
> int  can_recv(int fd, struct canfd_frame *cf, int timeout_ms, int *msg_flags);
>      // 16 = classic, 72 = CAN FD, 0 = timeout, -errno. Must never block longer than timeout_ms.
> int  can_set_filters(int fd, const struct can_filter *f, size_t n);   // n == 0 -> receive nothing
> int  can_set_err_mask(int fd, can_err_mask_t mask);
> bool filter_match(canid_t rx_id, const struct can_filter *f);    // user-space model of the kernel rule
> int  can_enable_fd(int fd);
> int  can_send_fd(int fd, canid_t id, const uint8_t *data, uint8_t len, uint8_t flags);
> int  can_fmt(const struct canfd_frame *cf, int mtu, char *out, size_t cap);   // candump -L style
> ```
>
> Constraints: no dynamic allocation, no blocking calls without a timeout, no fd leaks on error paths.
> The tests run in one process on `vcan0` (MTU 72) and `vcan1` (MTU 16).
>
> Examples:
> - Filter `{0x123, CAN_SFF_MASK}`; send SFF `0x123`, SFF `0x124`, EFF `0x00000123`, RTR `0x123` → the
>   receiver gets **three** frames (all but `0x124`). Explain why and give the mask that admits only the SFF data frame.
> - `can_send(fd, 0x123, d, 9)` → `-EINVAL`. `can_send_fd()` on a socket without `CAN_RAW_FD_FRAMES`, or on
>   an MTU-16 interface → `-EINVAL`.
> - `can_fmt({0x321, len 3, flags BRS|FDF, 11 22 33}, 72, ...)` → `"321##5112233"`.
>
> Follow-ups: how do you detect frames lost in the socket queue? What changes on a real `can0`?

## 3. 시작 코드

<!-- code: starter.c -->

검사 스크립트 (VM 안에서 vcan0/vcan1 준비 → 복사 → 엄격 빌드 → 실행 → ASan/UBSan):

<!-- code: check.sh -->

```bash
# 맥 터미널에서 (VM 이 맥 홈을 같은 경로에 읽기 전용으로 마운트하고 있다)
limactl shell elinux -- bash $PWD/check.sh
# VM 안에서 직접
gcc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter vcan0 vcan1
```

TODO 는 아홉 개: A1 `can_open`, A2 `can_send`, A3 `can_recv`, B1 `can_set_filters`, B2 `can_set_err_mask`, B3 `filter_match`, C1 `can_enable_fd`, C2 `can_send_fd`, C3 `can_fmt`. 스텁은 `-ENOSYS` / "타임아웃(0)" / `false` 를 돌려주므로 아무것도 막히지 않는다. 처음 실행 결과 (실제 출력, 중간 생략):

```text
FAIL starter.c:163  tx >= 0 && rx >= 0
FAIL starter.c:164  can_open("nosuch9") == -ENODEV
FAIL starter.c:170  can_send(tx, 0x123, d3, 3) == 0
…
B7 random filters: 40 rounds, 0 frames sent, kernel delivered 0, rounds where kernel == model: 0
…
MTU vcan0=-9 vcan1=-9
…
C3 received FD frame formatted: (none)
12/88 passed
```

(exit 1, 0.00 s.) 통과하는 12 개는 구조체 배치 검사 4 개와 "오면 안 되는 프레임이 안 왔다" 류 — 스텁이 늘 "타임아웃"이라 우연히 맞는다. 그래서 테스트마다 **먼저 "와야 하는 것"이 왔는지** 확인한 뒤에 "안 와야 하는 것"을 본다.

## 4. 힌트

<details><summary>힌트 1 — bind 할 주소는 sockaddr_can 하나</summary>

`struct sockaddr_can addr = {0}; addr.can_family = AF_CAN; addr.can_ifindex = if_nametoindex("vcan0");` — IP 주소 같은 건 없다. CAN 에는 "주소"가 없고 **인터페이스**만 고른다. `if_nametoindex` 가 0 을 돌려주면 errno 는 `ENODEV` (실측). 실패 경로에서 `close(fd)` 하기 전에 errno 를 저장하라 — `close` 가 errno 를 바꿀 수 있다.
</details>

<details><summary>힌트 2 — write 크기가 곧 프레임 종류</summary>

커널은 `write` 길이로 프레임 종류를 판단한다: 16 B(`CAN_MTU`) = 클래식, 72 B(`CANFD_MTU`) = FD. 15 B 를 쓰면 `EINVAL` (실측). 그래서 `can_send` 는 `len` 과 상관없이 늘 `sizeof(struct can_frame)` 을 쓴다. 읽을 때도 마찬가지 — 72 B 버퍼로 읽고 **반환값**으로 종류를 안다.
</details>

<details><summary>힌트 3 — 막히지 않는 수신</summary>

`poll(&p, 1, timeout_ms)` 이 0 이면 타임아웃, 양수면 `revents & POLLIN` 확인 후 `recvmsg(fd, &mh, MSG_DONTWAIT)`. `recvmsg` 를 써야 `mh.msg_flags` 로 `MSG_CONFIRM`(내가 보낸 것)을 알 수 있다. `read` 로는 그 정보가 사라진다.
</details>

<details><summary>힌트 4 — 필터: 마스크에 플래그 비트도 들어간다</summary>

`can_id` 의 비트 31 = EFF, 30 = RTR, 29 = ERR. 커널 비교는 32비트 전체에서 `(rx & mask) == (id & mask)` 다. 마스크가 `0x7FF` 면 비트 29~31 은 **안 본다** → EFF 0x00000123 도, RTR 0x123 도 하위 11비트가 0x123 이라 통과. 반전은 `id` 쪽의 `CAN_INV_FILTER`(= 비트 29 를 재사용) 로 표시한다.
</details>

<details><summary>힌트 5 — FD 가 EINVAL 일 때 볼 세 가지</summary>

① 이 소켓에 `CAN_RAW_FD_FRAMES` 를 켰나 ② 인터페이스 MTU 가 72(또는 CAN XL 2060)인가 — `ioctl(SIOCGIFMTU)` ③ 72 B 를 썼고 `len ≤ 64` 인가. 셋 중 하나라도 아니면 커널이 `EINVAL`. `cansend` 도 ② 를 먼저 확인하고 "CAN interface is not CAN FD capable - sorry." 를 찍는다.
</details>

<details><summary>힌트 6 — candump -L 표기 규칙</summary>

ID: SFF 는 `%03X`, EFF 는 `%08X`(앞자리 0 포함 — 그래서 `00000123` 은 EFF), 에러 프레임은 `CAN_ERR_FLAG` 를 포함해 8자리. 클래식은 `#`, FD 는 `##` + **flags 하위 니블 한 자리**. RTR 은 `R` 뒤에 len 이 0 이 아니면 한 자리. 데이터는 `%02X` 연속.
</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "SocketCAN 은 CAN 컨트롤러를 netdev 로 보여 주고, 응용은 `PF_CAN` 소켓을 연다. 여러 프로세스가 같은 버스를 동시에 듣고, 필터는 커널에서 돈다."
2. "프레임 종류는 쓰기·읽기 크기로 구분 — 16 B 클래식, 72 B FD. FD 는 소켓 옵션 + 인터페이스 MTU 둘 다 필요."
3. "필터는 `(rx & mask) == (id & mask)` 를 32비트 전체로. 플래그 비트를 마스크에 안 넣으면 EFF·RTR 이 섞여 들어온다."
4. "자기 프레임은 기본으로 안 보이고, 수신 큐가 넘치면 조용히 버려진다 — `SO_RXQ_OVFL` 로 센다."
5. "vcan 은 버스가 없다 — 실제 can0 에선 ACK 없으면 송신이 안 끝나고, bus-off 가 에러 프레임으로 온다."

### Part A — 구조체 배치와 송수신

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">can_id (32비트) 와 두 프레임 구조체 — 클래식은 FD 의 앞 16 B 와 같은 배치</text>
  <g stroke="currentColor">
    <rect x="20" y="30" width="40" height="30" class="f-warn"/><rect x="60" y="30" width="40" height="30" class="f-warn"/><rect x="100" y="30" width="40" height="30" class="f-warn"/>
    <rect x="140" y="30" width="300" height="30" class="f-mut"/><rect x="440" y="30" width="280" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor">
    <text x="28" y="50">EFF</text><text x="68" y="50">RTR</text><text x="108" y="50">ERR</text>
    <text x="200" y="50">비트 28..11: EFF 일 때만 (상위 18비트)</text><text x="490" y="50">비트 10..0: SFF ID (11비트)</text>
    <text x="30" y="76">31</text><text x="70" y="76">30</text><text x="110" y="76">29</text><text x="145" y="76">28</text><text x="425" y="76">11</text><text x="445" y="76">10</text><text x="705" y="76">0</text>
    <text x="20" y="96">CAN_EFF_FLAG 0x80000000 · CAN_RTR_FLAG 0x40000000 · CAN_ERR_FLAG 0x20000000 (= 필터의 CAN_INV_FILTER)</text>
  </g>
  <text x="20" y="130" fill="currentColor" font-weight="bold">struct can_frame — 16 B = CAN_MTU</text>
  <g stroke="currentColor">
    <rect x="20" y="140" width="160" height="34" class="f-acc"/><rect x="180" y="140" width="40" height="34" class="f-ok"/>
    <rect x="220" y="140" width="40" height="34" class="f-mut"/><rect x="260" y="140" width="40" height="34" class="f-mut"/><rect x="300" y="140" width="40" height="34" class="f-mut"/>
    <rect x="340" y="140" width="320" height="34" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="60" y="162">can_id [0..3]</text><text x="186" y="162">len</text><text x="224" y="162">pad</text><text x="262" y="162">res0</text><text x="302" y="162">dlc8</text>
    <text x="440" y="162">data[8]  [8..15]</text>
  </g>
  <text x="20" y="208" fill="currentColor" font-weight="bold">struct canfd_frame — 72 B = CANFD_MTU</text>
  <g stroke="currentColor">
    <rect x="20" y="218" width="160" height="34" class="f-acc"/><rect x="180" y="218" width="40" height="34" class="f-ok"/>
    <rect x="220" y="218" width="40" height="34" class="f-warn"/><rect x="260" y="218" width="40" height="34" class="f-mut"/><rect x="300" y="218" width="40" height="34" class="f-mut"/>
    <rect x="340" y="218" width="380" height="34" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="60" y="240">can_id [0..3]</text><text x="186" y="240">len</text><text x="222" y="240">flags</text><text x="262" y="240">res0</text><text x="302" y="240">res1</text>
    <text x="440" y="240">data[64]  [8..71]</text>
    <text x="20" y="276">flags: BRS 0x01 · ESI 0x02 · FDF 0x04 (이 VM 커널은 받은 FD 프레임에 FDF 를 붙임: 보낸 0x1 → 받은 0x5)</text>
    <text x="20" y="294">len = 실제 바이트 수 (DLC 아님). 클래식 DLC 9..15 는 len8_dlc 로 따로 (CC_LEN8_DLC 모드에서만)</text>
  </g>
</svg>
```

크기·오프셋은 `test_layout()` 이 컴파일된 헤더로 확인한다(`sizeof` 16/72, `data` 오프셋 8, `len` 오프셋 같음). 같은 배치이므로 **72 B 버퍼 하나로 클래식과 FD 를 모두 읽고**, `read` 반환값 16/72 로 구분하는 것이 표준 패턴이다.

송신 경로에서 내가 할 일과 커널이 할 일을 나눈다.

| 검사 | 누가 | 이 VM 실측 결과 |
|---|---|---|
| `write` 크기가 16 또는 72 인가 | 커널 | 15 B → `EINVAL` |
| 클래식 `len ≤ 8` | 커널 | len 9 → `EINVAL`, 수신 측 아무것도 안 옴 |
| FD `len ≤ 64` | 커널 | 65 → `EINVAL` |
| FD `len` 이 유효 DLC 길이(12, 16, 20 …)인가 | **검사 안 함** | len 9 FD 프레임이 그대로 9 로 배달됨 (vcan) |
| `data` 복사가 `data[8]` 을 넘지 않나 | **나** | `memcpy(cf.data, data, min(len, 8))` |

FD len 9 가 통과하는 건 vcan 에서 본 결과다. 실제 FD 컨트롤러 드라이버는 len 을 DLC 로 바꿀 때(`can_fd_len2dlc`) 올림하므로 와이어에는 DLC 9 = 12 바이트가 나가고 받는 쪽은 12 를 본다(커널 소스·드라이버 확인 권장) — 그래서 연습 2 의 파서는 유효 길이만 받는다(COMM-M08 의 DLC 표).

수신 쪽의 핵심은 **절대 무한 대기하지 않기**. `poll` 타임아웃을 두 종류로 썼다: 와야 하는 프레임은 `T_POS = 500 ms` 까지, 안 와야 하는 프레임은 `T_NEG = 30 ms` 동안 안 오면 "안 왔다". vcan 전달은 `write` 가 돌아오기 전에 거의 끝나므로(소프트IRQ 가 같은 CPU 에서 바로 돈다) 30 ms 는 넉넉하다. 실측 전체 실행 0.60 s.

루프백 세 가지를 표로 정리하면 (모두 테스트로 확인):

| 설정 (보내는 소켓) | 다른 소켓 rx | 보낸 소켓 자신 |
|---|---|---|
| 기본 (`LOOPBACK=1`, `RECV_OWN_MSGS=0`) | 받음, `msg_flags` 에 `MSG_DONTROUTE` | 안 받음 |
| `RECV_OWN_MSGS=1` | 받음 (`MSG_CONFIRM` 없음) | 받음, `MSG_CONFIRM` (실측 `msg_flags = 0x804`) |
| `LOOPBACK=0` | **vcan 에선 아무도 못 받음** | 안 받음 |

마지막 줄이 vcan 의 정체를 보여 준다: vcan 에는 버스가 없고, 같은 호스트의 다른 소켓이 프레임을 받는 유일한 길이 af_can 의 로컬 루프백이다. 실제 can0 에서 `LOOPBACK=0` 이면 프레임은 버스로 나가 **다른 노드**는 받지만 같은 호스트의 다른 프로그램은 못 본다.

### Part B — 필터 규칙

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">수신 프레임 하나가 소켓 큐에 들어가기까지 (can-raw 소켓 기준)</text>
  <rect x="20" y="32" width="170" height="40" class="f-acc" stroke="currentColor"/>
  <text x="30" y="50" fill="currentColor">프레임 도착 (rx_id)</text><text x="30" y="65" fill="currentColor">vcan 루프백 / 드라이버</text>
  <rect x="230" y="32" width="170" height="40" class="f-mut" stroke="currentColor"/>
  <text x="240" y="50" fill="currentColor">rx_id 에 ERR_FLAG ?</text><text x="240" y="65" fill="currentColor">(에러 프레임)</text>
  <line x1="190" y1="52" x2="230" y2="52" stroke="currentColor" marker-end="url(#a2)"/>
  <rect x="450" y="32" width="270" height="40" class="f-warn" stroke="currentColor"/>
  <text x="460" y="50" fill="currentColor">예: (rx_id &amp; err_mask) != 0 인 소켓만</text><text x="460" y="65" fill="currentColor">데이터 필터 {0,0} 이어도 못 받음</text>
  <line x1="400" y1="52" x2="450" y2="52" stroke="currentColor" marker-end="url(#a2)"/>
  <text x="410" y="46" fill="currentColor">예</text>
  <line x1="315" y1="72" x2="315" y2="104" stroke="currentColor" marker-end="url(#a2)"/>
  <text x="322" y="92" fill="currentColor">아니오 — 필터마다</text>
  <rect x="120" y="104" width="390" height="74" class="f-mut" stroke="currentColor"/>
  <text x="130" y="122" fill="currentColor">inv = f.can_id &amp; CAN_INV_FILTER</text>
  <text x="130" y="138" fill="currentColor">mask에 EFF, id에 없음 → mask &amp;= SFF_MASK|EFF|RTR</text>
  <text x="130" y="154" fill="currentColor">eq = (rx_id &amp; mask) == (f.can_id &amp; mask)</text>
  <text x="130" y="170" fill="currentColor">match = inv ? !eq : eq</text>
  <line x1="315" y1="178" x2="315" y2="206" stroke="currentColor" marker-end="url(#a2)"/>
  <rect x="120" y="206" width="390" height="40" class="f-ok" stroke="currentColor"/>
  <text x="130" y="224" fill="currentColor">기본: 하나라도 match → 1번 전달 (겹쳐도 1번)</text>
  <text x="130" y="240" fill="currentColor">JOIN_FILTERS: 모든 필터 match 일 때만 (연습 1)</text>
  <line x1="315" y1="246" x2="315" y2="272" stroke="currentColor" marker-end="url(#a2)"/>
  <rect x="120" y="272" width="390" height="44" class="f-warn" stroke="currentColor"/>
  <text x="130" y="290" fill="currentColor">소켓 큐 sk_rcvbuf 에 자리? 없으면 버림 + sk_drops++</text>
  <text x="130" y="306" fill="currentColor">→ 다음에 들어오는 프레임의 SO_RXQ_OVFL 값 (연습 4)</text>
  <text x="540" y="140" fill="currentColor">필터 0 개 (n = 0):</text>
  <text x="540" y="156" fill="currentColor">등록된 리스트 없음 → 0 개 수신</text>
  <text x="540" y="186" fill="currentColor">새 소켓 기본: {0, 0}</text>
  <text x="540" y="202" fill="currentColor">mask 0 → 모두 match</text>
  <defs><marker id="a2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
</svg>
```

`filter_match` 는 커널의 `can_rcv_list_find()` 가 필터 등록 시 하는 정리(반전 비트 떼기, "SFF 전용" 보정, `id &= mask`)와 수신 시의 비교를 그대로 옮긴 것이다(기억 기반 — 커널 소스 확인 권장). 기억이 틀렸으면 B7 랜덤 대조가 잡는다: 이 모델을 일부러 망가뜨려 보면 실제로 이렇게 됐다.

| 모델 변형 (일부러 틀림) | B7: 커널 == 모델 라운드 | 걸린 이름 붙은 테스트 |
|---|---|---|
| 정답 | **40/40** | — |
| "SFF 전용" 보정 삭제 | 39/40 | B2b |
| INV 무시 | 20/40 | — |
| 플래그 비트를 비교에서 뺌 (`& CAN_EFF_MASK`) | 32/40 | B2 |

예제 하나를 비트로 따라가 보자. 필터 `{0x123, 0x7FF}`, 수신 EFF `0x00000123` → `rx_id = 0x80000123`.

| 값 | hex | `& 0x7FF` |
|---|---|---|
| rx_id (EFF 0x123) | `0x80000123` | `0x123` |
| f.can_id | `0x00000123` | `0x123` |

같다 → 통과. 마스크를 `0x7FF | CAN_EFF_FLAG | CAN_RTR_FLAG = 0xC00007FF` 로 바꾸면 `rx_id & mask = 0x80000123 ≠ 0x00000123` → 거절. 이게 B1/B2 테스트이고, `candump vcan0,123:7FF` 와 `candump vcan0,123:C00007FF` 로도 똑같이 보인다(아래 실험 3).

"SFF 전용" 보정(B2b)은 구석 규칙이다: 마스크에 EFF 플래그가 있는데 id 에 없으면 "SFF 프레임만 원한다"는 뜻이므로 커널이 마스크의 비트 11..28 을 지운다. 그래서 `{0x1123, CAN_EFF_MASK | CAN_EFF_FLAG}` 가 SFF `0x123` 을 받는다(실측). 이걸 빼먹은 모델은 랜덤 40 라운드 중 1 라운드에서만 틀렸다 — **이름 붙은 구석 테스트가 필요한 이유**.

B7 실제 출력:

```text
B7 random filters: 40 rounds, 2560 frames sent, kernel delivered 1397, rounds where kernel == model: 40
```

라운드마다 필터 1~4 개(정확 SFF/범위/EFF/완전 랜덤, 1/4 확률 반전) + **센티널** 필터 `{0x7FF, SFF_MASK|EFF|RTR}` 를 걸고, 64 프레임 뒤에 SFF 0x7FF 를 보낸다. 센티널이 도착하면 그 앞의 프레임은 다 왔다는 뜻(같은 소켓 큐는 순서 보존) — 타임아웃을 기다리지 않아 빠르고 결정적이다. 랜덤 프레임에서 정확히 0x7FF SFF 데이터만 빼 둔다.

에러 프레임(B8): `can_send(tx, CAN_ERR_FLAG | CAN_ERR_BUSOFF, zeros, 8)` 이 vcan 에서는 **된다**(실측 write = 16). 결과: 에러 마스크 `CAN_ERR_MASK` + 데이터 필터 0 개인 소켓 = "에러만 보는 감시 태스크"가 받고, 마스크가 `CAN_ERR_CRTL` 뿐인 소켓과 `{0,0}` 소켓은 못 받는다. 실제 can0 에서는 컨트롤러 드라이버가 bus-off(`CAN_ERR_BUSOFF`), error-passive(`CAN_ERR_CRTL` + `data[1]`), 재시작(`CAN_ERR_RESTARTED`) 등을 만들어 올린다(linux/can/error.h; 세부 비트 의미는 헤더 확인 권장, 상태 전이는 COMM-M07). 실제 하드웨어 인터페이스에 에러 프레임을 **쓰는** 것은 하지 말 것 — 테스트용 주입은 vcan 에서만.

### Part C — CAN FD: 세 가지가 맞아야 한다

모든 칸을 테스트(+ demo.sh 의 cansend)로 확인했다.

| 소켓 `FD_FRAMES` | 인터페이스 MTU | 쓰기 | 결과 |
|---|---|---|---|
| 꺼짐 | 72 | 16 B 클래식 | 성공 |
| 꺼짐 | 72 | 72 B FD | `EINVAL` |
| 켜짐 | 72 | 72 B FD, len 64 | 성공 → FD 소켓이 72 B 로 받음, flags `0x5` |
| 켜짐 | 72 | 16 B 클래식 | 성공 → FD 소켓이 **16 B** 로 받음 |
| 켜짐 | 16 (vcan1) | 72 B FD | `EINVAL` (cansend: "not CAN FD capable") |
| 켜짐 | 16 | 16 B 클래식 | 성공 |
| 켜짐 | 72 | len 65 | `EINVAL` |

받는 쪽: `FD_FRAMES` 를 안 켠 소켓에는 FD 프레임이 **아예 오지 않는다**(버려진다, 에러도 없음). 그래서 FD 버스를 진단하는 툴은 반드시 FD 를 켜고, 읽은 크기로 분기한다.

인터페이스 MTU 는 링크가 **내려가 있을 때만** 바꿀 수 있다 — up 상태에서 `ip link set vcan1 mtu 72` → "RTNETLINK answers: Device or resource busy" (실측). 그래서 check.sh 는 MTU 가 다를 때만 down → mtu → up 한다. 새로 만든 vcan 의 기본 MTU 는 이 커널에서 **2060**(CAN XL MTU)이었다 — 브리프 대로 72 로 내려 "FD 까지만" 인 인터페이스를 만든다.

`can_fmt` 의 기대값은 손으로 쓴 것이고, 같은 프레임을 진짜 `candump -L` 이 찍은 줄과 demo.sh 가 비교한다(실험 2: 9 줄 전부 같음). FD 줄의 flags 자리는 **받은 프레임의 flags** 이므로 `cansend vcan0 321##1112233` 이 로그에는 `321##5112233` 으로 남는다(FDF 0x4 가 커널에서 붙음 — 이 VM 커널 7.0 실측. 옛 커널은 다를 수 있다).

### 실험 — demo.sh (실측, Lima VM `elinux`, 커널 7.0.0-38, can-utils 2023.03, gcc 15.2)

<!-- code: demo.sh -->

실행: `limactl shell elinux -- bash $PWD/demo.sh` (check.sh 와 동시에 돌리지 말 것). 도구 `demo_tool.c` 는 solution 함수의 축약 복사본이다.

**실험 2 — 우리 포매터 vs candump -L**: 같은 프레임 9 개를 우리 `demo_tool send` 가 받아서 찍은 줄과, 동시에 돌던 `candump -L vcan0` 줄(타임스탬프·인터페이스 제거)을 나란히:

```text
same 123#112233
same 1ABCDEF0#1223344556677889
same 456#R
same 456#R2
same 7FF#
same 00000123#16
same 321##5172839
same 321##718293A4B5C6D7E8FA0B1C2D3E4F5061728394A5B6C7D8E9FB0C1D2E3F405162738495A6B7C8D9EA
same 18DAF110##4192A3B4C5D6E7F90A1B2C3D4
=> IDENTICAL (9 lines)
```

(64 바이트 줄은 90 글자에서 잘라 보였다. 마지막 줄: flags 0 으로 보낸 EFF FD 프레임도 받으면 `##4` = FDF.)

**실험 3 — cansend → candump(사람용 형식) + 필터**: `cansend` 로 `123#01`, `00000123#02`, `124#03`, `123#R`, `1ABCDEF0#DEADBEEF`, `321##1AABBCC` 를 보내고 여섯 수신자로 동시에 봤다: `D_all` = `candump vcan0`, `D_123_7FF` = `candump vcan0,123:7FF`, `D_123_C00007FF` = `candump vcan0,123:C00007FF`, `D_inv` = `candump vcan0,123~7FF`(반전), `O_*` = 같은 필터를 건 우리 `demo_tool recv`.

```text
-- D_all
  vcan0  123   [1]  01
  vcan0  00000123   [1]  02
  vcan0       124   [1]  03
  vcan0       123   [0]  remote request
  vcan0  1ABCDEF0   [4]  DE AD BE EF
  vcan0       321  [03]  AA BB CC
-- D_123_7FF
  vcan0  123   [1]  01
  vcan0  00000123   [1]  02
  vcan0       123   [0]  remote request
-- O_123_7FF
our 123#01
our 00000123#02
our 123#R
-- D_123_C00007FF
  vcan0  123   [1]  01
-- O_123_C00007FF
our 123#01
-- D_inv
  vcan0  124   [1]  03
  vcan0  1ABCDEF0   [4]  DE AD BE EF
  vcan0       321  [03]  AA BB CC
```

읽는 법: candump 의 사람용 형식은 EFF 프레임을 한 번 본 뒤부터 SFF ID 를 8칸 오른쪽 정렬로 바꾼다(첫 줄만 왼쪽에 붙은 이유). FD 프레임은 길이가 `[03]` 두 자리. `123:7FF` 가 EFF·RTR 까지 받는 것 = Part B 의 함정이 candump 에서도 그대로다. 마지막의 FD 프레임은 candump 가 FD 를 켜고 열기 때문에 보인다. 반전 필터 `123~7FF` 에서 EFF 0x123 과 RTR 0x123 은 하위 11비트가 0x123 이라 빠졌다.

**실험 4 — 에러 프레임**: `demo_tool errinj` 로 BUSOFF 에러 프레임 1 개 + `cansend vcan0 010#`.

```text
-- candump -e vcan0,0:0,#FFFFFFFF
  vcan0  20000040   [8]  00 00 00 00 00 00 00 00   ERRORFRAME
	bus-off
  vcan0  010   [0]
-- candump vcan0 (no error mask)
  vcan0  010   [0]
```

**실험 5 — MTU 16 에 FD**: `cansend vcan1 123##1AA` → `CAN interface is not CAN FD capable - sorry.`, rc=1. vcan0 은 rc=0.

**실험 6 — 수신 큐 넘침**: 읽지 않는 소켓에 클래식 프레임을 쏟아붓고, 다 읽은 뒤 표지 프레임 하나를 더 보내 그 프레임의 `SO_RXQ_OVFL` 값을 본다.

```text
rcvbuf=   2304  sent=2000 write_err=0  read=    3  dropcount(marker)= 1997  read+dropped=2000
rcvbuf=  65536  sent=2000 write_err=0  read=   69  dropcount(marker)= 1931  read+dropped=2000
rcvbuf= 212992  sent=2000 write_err=0  read=  222  dropcount(marker)= 1778  read+dropped=2000
rcvbuf= 212992  sent=20000 write_err=0  read=  222  dropcount(marker)=19778  read+dropped=20000
```

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">실측: 아무도 안 읽을 때 소켓 큐에 남는 클래식 프레임 수 (2000 개 송신)</text>
  <g stroke="currentColor">
    <rect x="200" y="40" width="5" height="28" class="f-warn"/>
    <rect x="200" y="90" width="93" height="28" class="f-warn"/>
    <rect x="200" y="140" width="300" height="28" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="20" y="59">SO_RCVBUF 0 → 2304 B</text><text x="215" y="59">3 개 (1997 개 버림)</text>
    <text x="20" y="109">32768 요청 → 65536 B</text><text x="303" y="109">69 개 (1931 개 버림)</text>
    <text x="20" y="159">기본 212992 B</text><text x="510" y="159">222 개 (1778 개 버림)</text>
    <text x="20" y="192">막대 길이 = 받은 개수 (222 = 300 px). 212992 / 222 ≈ 959 B — 16 B 프레임 하나가 큐에서 차지하는 skb 크기 추정</text>
  </g>
</svg>
```

읽을 거리 세 가지: ① `SO_RCVBUF` 로 요청한 값은 커널이 **두 배**로 잡는다(32768 → 65536, 0 → 최소 2304 — 리눅스 일반 동작). ② 프레임 하나가 16 B 인데 큐에서는 skb 오버헤드 때문에 약 1 KB 를 먹는다 → 기본 큐 ≈ 222 프레임. 500 kbit/s 버스가 꽉 차면 SFF 8 B 프레임은 IFS 포함 111 비트(스터핑 없음)~135 비트(최악 스터핑, COMM-M04) → 초당 약 3700–4500 개 → **222 개는 약 49–60 ms. 그만큼만 안 읽어도 넘친다.** ③ 송신 쪽은 아무 에러도 없다(write_err 0). 잃은 것은 수신자만 안다 — 그래서 `SO_RXQ_OVFL`. 값은 "이 프레임이 큐에 들어갈 때까지 이 소켓이 버린 누적 수"라서 **넘치기 전에 들어와 있던 3 개에는 0** 이 찍히고, 넘친 뒤 들어온 표지 프레임에 1997 이 찍힌다(연습 4 가 이걸 단언). candump 가 "DROPCOUNT: dropped N CAN frames" 를 찍는 것도 이 값이다.

**실험 7 — cangen**: `cangen vcan0 -g 1 -n 500 -I 42A -L 8 -D i` → `candump -n 500` 이 500 줄(첫 줄 `00 00 …`, 끝 줄 `F3 01 00 …` = 499 리틀엔디언 증가 카운터), `/sys/class/net/vcan0/statistics/rx_packets` 증가량 = 500. vcan 은 루프백된 프레임을 rx 로 센다(그림 1의 "통계만 세고").

### 실제 can0 은 무엇이 다른가

| 항목 | vcan0 | 실제 can0 |
|---|---|---|
| 비트레이트 | 없음 | `ip link set can0 type can bitrate 500000 sample-point 0.875` (+ FD: `dbitrate 2000000 fd on`) — **링크 down 에서만** (COMM-M06) |
| ACK | 없음 — 혼자여도 "성공" | 다른 노드가 ACK 안 하면 컨트롤러가 계속 재전송 → TX 큐가 차서 `write` 가 `ENOBUFS`, TEC 증가 → error-passive (COMM-M07). 혼자 꽂은 보드에서 흔히 보는 증상(드라이버마다 다름, 확인 권장) |
| 자기 프레임 순서 | 쓰자마자 루프백 | 버스에서 중재에 이기고 ACK 받은 뒤 echo → 로컬 수신 순서 = 실제 버스 순서 |
| 에러 프레임 | 직접 주입해야 함 | 드라이버가 생성: bus-off, passive, RX FIFO overflow(`CAN_ERR_CRTL_RX_OVERFLOW`) … |
| bus-off 복구 | 해당 없음 | `ip link set can0 type can restart-ms 100` 자동 / `ip link set can0 type can restart` 수동 (COMM-M07) |
| 통계 | rx/tx packets | `ip -details -statistics link show can0`: state ERROR-ACTIVE/WARNING/PASSIVE/BUS-OFF, berr-counter tx/rx, restarts, bus-error |
| 프레임을 잃는 곳 | 소켓 큐만 | 컨트롤러 RX FIFO(드라이버가 NAPI 로 늦게 비우면), netdev backlog, 소켓 큐 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```text
== kernel 7.0.0-38-generic, gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0
== run starter (must FAIL)
12/88 passed
   -> starter exit=1 (want 1), 0.00 s
== run solution (must PASS)
88/88 passed
   -> solution exit=0 (want 0), 0.60 s
== run practice (must PASS)
34/34 passed
   -> practice exit=0 (want 0), 0.72 s
== ASan/UBSan solution
88/88 passed
   -> solution_san exit=0 (want 0), 0.60 s
== ASan/UBSan practice
34/34 passed
   -> practice_san exit=0 (want 0), 0.71 s
RESULT: PASS (starter fails as intended, solution/practice pass, sanitizers clean)
```

solution 의 관찰 줄(실제 출력):

```text
B7 random filters: 40 rounds, 2560 frames sent, kernel delivered 1397, rounds where kernel == model: 40
MTU vcan0=72 vcan1=16
FD rx flags = 0x5 (BRS=0x1 ESI=0x2 FDF=0x4)
C3 received FD frame formatted: 321##5ABCD
88/88 passed
```

설계 메모:
- 모든 함수가 `-errno` 를 돌려준다(전역 errno 를 호출자가 다시 읽지 않아도 된다). 실패 경로는 errno 를 먼저 저장하고 `close`.
- 테스트는 "와야 하는 것"을 먼저 확인 → "안 와야 하는 것"을 `T_NEG` 로 확인. 스텁이 늘 타임아웃이면 앞쪽이 FAIL 한다.
- 랜덤 대조는 고정 시드 xorshift32(0xC0FFEE) + 센티널 프레임으로 타임아웃 없이 끝낸다.

## 7. 자주 하는 실수

**1) 필터 마스크에 플래그 비트를 안 넣는다.** `{0x123, CAN_SFF_MASK}` 로 "0x123 만" 받는다고 생각하지만 EFF 0x00000123 과 RTR 0x123 도 온다(B1, 실험 3). J1939 처럼 EFF 를 쓰는 장비가 섞인 버스에서 엉뚱한 프레임이 파서에 들어간다.

```c
struct can_filter f = { 0x123, CAN_SFF_MASK | CAN_EFF_FLAG | CAN_RTR_FLAG };   /* SFF 데이터 0x123 만 */
```

**2) `read` 길이를 `sizeof(struct can_frame)` 으로 고정.** FD 를 켠 소켓에서 72 B 프레임을 16 B 버퍼로 읽으면 잘린다(`MSG_TRUNC`). 늘 72 B 버퍼로 읽고 반환값으로 분기.

**3) `recv` 를 타임아웃 없이 부른다.** 버스가 조용하면 영원히 멈춘다 — 워치독 리셋의 단골. `poll` + 남은 시간(연습 3 의 deadline)으로.

```c
long end = now_ms() + deadline_ms;
for (;;) { long left = end - now_ms(); if (left <= 0) return -ETIMEDOUT; r = can_recv(fd, cf, (int)left, NULL); ... }
```

**4) 타임아웃을 매번 처음부터 다시 잰다.** 잡음 프레임이 하나 올 때마다 `poll(…, 100)` 을 새로 부르면, 0x7E8 이 영영 안 와도 잡음이 50 ms 마다 오는 버스에서는 **절대 타임아웃이 안 난다.** 위처럼 마감시각을 고정하거나, 커널 필터로 잡음을 아예 막는다(연습 3 의 두 번째 경우: `skipped == 0`).

**5) "보냈으니 받았겠지".** 수신 큐가 넘치면 송신 측은 아무 에러도 못 본다(실험 6: write_err 0, 1778 개 소실). 로그 수집기라면 `SO_RXQ_OVFL` 을 켜고, 큰 `SO_RCVBUF`, 커널 필터, 빠른 읽기 스레드.

**6) up 상태에서 MTU/비트레이트를 바꾼다.** `EBUSY`. down → 설정 → up.

**7) FD 를 켰는데 `EINVAL`.** 소켓 옵션은 켰지만 인터페이스 MTU 가 16 인 경우(vcan1). `ioctl(SIOCGIFMTU)` 로 먼저 확인하고 사용자에게 "이 인터페이스는 FD 아님"을 알린다.

**8) `memcpy(cf.data, data, len)` 을 그대로.** `len` 이 9 이상이면 내 스택을 넘쳐 쓴 **뒤에** 커널이 EINVAL 을 준다. 복사는 `min(len, 8)`.

## 8. 면접 Q&A

<details><summary>Q1. 왜 CAN 을 문자 디바이스(/dev/can0) 가 아니라 소켓으로 만들었나요?</summary>

여러 프로세스가 같은 버스를 동시에 들어야 하고(진단 툴 + 제어 루프 + 로거), 각자 다른 ID 만 원한다. 소켓이면 프로세스마다 독립된 수신 큐와 **커널 안 필터**가 생긴다 — 문자 디바이스는 보통 한 명이 열고 모든 프레임을 사용자 공간에서 거른다. 또 netdev 이므로 `ip link` 로 설정, 통계, qdisc, 네임스페이스, `vcan`/`vxcan` 같은 가상 장치를 공짜로 얻는다. 꼬리질문 "단점은?" — 큐가 프로세스마다 있어 느린 리더는 혼자 프레임을 잃는다(실험 6), 실시간 지연은 소프트IRQ·스케줄러에 달림(RT 커널, 우선순위 높은 수신 스레드).
</details>

<details><summary>Q2. 필터 {0x123, 0x7FF} 를 걸었는데 다른 프레임이 들어옵니다. 왜죠?</summary>

비교가 `(rx_id & mask) == (id & mask)` 로 32비트 전체에서 일어나는데, 마스크 0x7FF 는 EFF(31)·RTR(30) 비트를 안 본다. 그래서 EFF 0x00000123 과 RTR 0x123 이 통과(B1, `candump vcan0,123:7FF` 도 동일). 마스크에 `CAN_EFF_FLAG | CAN_RTR_FLAG` 를 넣어 `0xC00007FF` 로. 꼬리: "범위로 받으려면?" — 0x100–0x1FF 는 `{0x100, 0x700}`(하위 8비트 무시). "이것만 빼고?" — id 에 `CAN_INV_FILTER`. "여러 개면?" — OR, 겹쳐도 한 번만 전달, AND 는 `CAN_RAW_JOIN_FILTERS`.
</details>

<details><summary>Q3. 제가 보낸 프레임이 제 프로그램에 다시 들어오나요?</summary>

기본은 같은 호스트의 **다른** 소켓만 받는다(`CAN_RAW_LOOPBACK` = 1, `CAN_RAW_RECV_OWN_MSGS` = 0). 자기 것도 받으려면 RECV_OWN_MSGS 를 켜고, `recvmsg` 의 `MSG_CONFIRM` 으로 "내가 보낸 것"을 구분한다(실측 `msg_flags` 0x804). 실제 can0 에서 이 echo 는 프레임이 **버스에 실제로 나간 뒤**(ACK 후) 오므로 "송신 완료 확인"·버스 순서 기록에 쓴다. vcan 은 쓰자마자 돌아온다.
</details>

<details><summary>Q4. CAN FD 프레임을 write 했더니 EINVAL 입니다. 무엇부터 보나요?</summary>

세 가지: ① 그 소켓에 `CAN_RAW_FD_FRAMES` ② 인터페이스 MTU 72(이상) — `ip -details link show can0` 의 mtu, 컨트롤러가 FD 를 지원하고 `fd on` 으로 설정됐는지 ③ 72 B 를 쓰고 len ≤ 64. 받는 쪽도 FD 를 안 켜면 FD 프레임이 **조용히 안 온다** — "보냈는데 안 와요" 의 원인. 꼬리: "len 9 는?" — vcan 은 통과시켰다(실측). 실제 드라이버는 DLC 로 올림해 12 바이트로 나간다(COMM-M08) → 앱에서 유효 길이만 쓰는 게 맞다.
</details>

<details><summary>Q5. 로그에서 프레임이 빠집니다. 어디서 빠질 수 있고 어떻게 확인하나요?</summary>

아래에서 위로: ① 컨트롤러 RX FIFO/메일박스 overrun — 드라이버가 늦게 비움, `ip -s link` 의 rx over/missed, 에러 프레임 `CAN_ERR_CRTL_RX_OVERFLOW`(드라이버 지원 시) ② netdev backlog ③ **소켓 수신 큐** — 앱이 늦게 읽음, `SO_RXQ_OVFL`(candump 의 DROPCOUNT). 실측으로 기본 큐는 클래식 프레임 약 222 개 = 꽉 찬 500k 버스에서 수십 ms. 대책: 큰 `SO_RCVBUF`(root 면 `SO_RCVBUFFORCE`), 커널 필터로 필요한 ID 만, 수신 전용 고우선순위 스레드, 하드웨어 타임스탬프로 순서 확인.
</details>

<details><summary>Q6. bus-off 는 응용에서 어떻게 알고, 어떻게 돌아오나요?</summary>

에러 마스크에 `CAN_ERR_BUSOFF`(+ `CAN_ERR_CRTL`, `CAN_ERR_RESTARTED`)를 걸어 에러 프레임으로 받는다 — B8 처럼 데이터 필터 0 개 + 에러 마스크만 건 감시 소켓을 따로 두면 깔끔하다. 복구는 커널/드라이버 설정: `restart-ms` 를 주면 그 시간 후 자동 재시작, 0 이면 `ip link set can0 type can restart` 로 수동. 프로토콜상 bus-off 노드는 128 × 11 recessive 비트를 본 뒤에야 다시 참여(COMM-M07). 꼬리: "자동 재시작이 위험한 경우?" — 고장 난 노드가 계속 bus-off ↔ 복귀를 반복하며 버스를 방해(babbling idiot). 재시작 횟수 제한과 상위 진단 보고.
</details>

<details><summary>Q7. vcan 으로 다 통과한 코드가 실제 can0 에서 실패할 수 있는 이유는?</summary>

vcan 에는 ACK·중재·비트 타이밍이 없다. 실제로는 (1) 상대 노드가 없거나 비트레이트·종단저항이 틀리면 ACK 가 없어 프레임이 TX 에 걸리고 `ENOBUFS`/타임아웃 (2) 우선순위 낮은 ID 는 중재에서 밀려 지연(COMM-M05) (3) FD 는 컨트롤러·트랜시버가 데이터 비트레이트를 지원해야 함 (4) 에러 프레임·상태 전이가 실제로 발생 (5) echo 순서가 버스 순서. 그래서 테스트 계층은 vcan 단위 테스트 → 두 보드 + 실제 버스 + 로직 애널라이저/CAN 분석기(HWLAB-M05/M06) → 오류 주입(케이블 분리, 종단 제거).
</details>

<details><summary>Q8. 요청을 보내고 응답을 100 ms 안에 기다리는 함수를 짜 보세요. 버스에 다른 트래픽이 많습니다.</summary>

연습 3. 핵심은 ① 응답 ID 만 받게 **커널 필터**(`{0x7E8, 0x7F8|EFF|RTR}` = 0x7E8..0x7EF) ② 마감시각을 한 번 정하고 매 루프마다 남은 시간으로 `poll` ③ 그래도 들어온 다른 프레임은 세고 버림 ④ 반환 코드 0 / `-ETIMEDOUT` / `-errno` 구분. 꼬리: "여러 ECU 가 응답하면?" — 마스크로 받은 뒤 첫 응답 반환 또는 마감까지 모아 반환. "멀티프레임 응답(ISO-TP)은?" — `CAN_ISOTP` 소켓(linux/can/isotp.h)이 흐름 제어를 커널에서 해 준다(다음 단계).
</details>

## 9. 추가 연습

### 연습 1 — CAN_RAW_JOIN_FILTERS: OR 를 AND 로

`setsockopt(CAN_RAW_JOIN_FILTERS, 1)` 을 켜면 필터 **전부**에 맞아야 전달된다. `{0x100, 0x700}`(0x100–0x1FF) AND `{0x001, 0x00F}`(하위 니블 1) → 0x101, 0x111, 0x1F1 만. 사용자 공간 AND 모델을 만들고, 랜덤 12 라운드(필터 2~3 개, 각자 다른 니블을 봄, 가끔 반전·EFF 플래그) × 48 프레임에서 커널과 비교하라.

<details><summary>답</summary>

`filters_accept_join` = 모든 `filter_match` 가 참(n == 0 이면 거짓). AND 에는 "모든 필터를 통과하는 센티널"을 일반적으로 만들 수 없어 라운드 끝을 `T_NEG` 타임아웃으로 판단한다(12 × 30 ms). 실측: `delivered 194, kernel == AND-model in 12 rounds`. 모델을 OR 로 바꾸면 0/12 — 테스트에 이빨이 있다. 쓰임새: "ID 범위 AND 특정 비트 패턴"처럼 마스크 하나로 표현 안 되는 조건(커널 4.1 이후 옵션 — 버전 확인 권장).
</details>

### 연습 2 — cansend 문법 파서와 왕복

`can_parse("123#11.22.33", &cf)` → `CAN_MTU`, `"1ABCDEF0#"`(EFF), `"456#R"`/`"456#R3"`(RTR), `"321##1AABB"`(FD, flags 니블) → `CANFD_MTU`. 잘못된 입력 12 종(ID 자리수, 비 hex, SFF > 0x7FF, EFF > 0x1FFFFFFF, 홀수 자리, 9 바이트 클래식, `R9`, FD flags 없음, FD 9 바이트, `1.2`) → `-EINVAL`. 랜덤 3000 프레임 fmt → parse → fmt 왕복, 그리고 7 줄을 실제 버스로 보내 받은 프레임의 표기가 입력 줄과 같은지.

<details><summary>답</summary>

ID 자리수로 SFF/EFF 를 정하는 것이 cansend 규칙(3자리 = SFF, 8자리 = EFF). FD 길이는 이 레슨의 정의로 유효 길이만 허용(cansend 자체가 9 같은 값을 어떻게 다루는지는 확인 안 함). 버스 왕복에서 `"321##5112233"` 은 그대로 돌아오지만 `"321##1AA"` 는 `321##5AA` 로 돌아온다(실측 관찰 줄 `P2 observe: sent 321##1AA -> received 321##5AA`) — 로그 → 재생(`canplayer`) 도구를 짤 때 FDF 니블을 무시하고 비교해야 하는 이유.
</details>

### 연습 3 — 요청/응답 + 마감시각

`can_request(fd, 0x7DF, req, 3, 0x7E8, 0x7F8, &resp, deadline_ms, &skipped, hook, ctx)`: 요청 송신 → 마감까지 `(id & 0x7F8) == 0x7E8` 응답 대기, 다른 프레임은 `skipped` 로 셈. 테스트용 `hook` 이 송신 직후 "ECU" 를 한 번 돌려(잡음 20 개 + 0x7DF 를 읽고 `04 41 0C 1A F8` 응답) 단일 스레드로 결정적. 세 경우: 필터 없음(skipped 20), 커널 필터(0x7E9 응답, skipped 0), ECU 없음(`-ETIMEDOUT`).

<details><summary>답</summary>

hook 은 실제로는 다른 노드가 하는 일을 테스트 안에 끼워 넣는 장치다(스레드 없이 결정적). 데이터 바이트 모양은 OBD-II 모드 01 PID 0x0C(엔진 rpm) 요청/응답의 흔한 예를 흉내 낸 것(ISO 15765-4 / SAE J1979 세부는 확인 권장). 핵심은 `left = end - now_ms()` — 실수 4 참고. 커널 필터를 건 두 번째 경우 잡음 20 개가 소켓에 들어오지도 않아 `skipped == 0`.
</details>

### 연습 4 — 수신 큐 넘침 감지 (SO_RXQ_OVFL)

`SO_RCVBUF = 0`(커널 최소 2304 B) + `SO_RXQ_OVFL = 1` 인 소켓에 200 프레임을 보내고 읽어라. 그다음 표지 프레임 하나를 보내 읽는다. 단언: 읽은 개수 k 는 0 < k < 200, 그 k 개에 찍힌 drop 값은 0, 표지에 찍힌 값 + k = 200.

<details><summary>답</summary>

`recvmsg` 의 제어 메시지에서 `SOL_SOCKET / SO_RXQ_OVFL` 의 `uint32_t` 를 꺼낸다. 실측 `P4 rcvbuf=2304: sent 200, read 3 (last id 2), drops stamped on those=0, on marker=197`. 값이 "누적"이고 "큐에 들어갈 때 찍힌다"는 것이 요점 — 손실 직후가 아니라 **다음 프레임이 도착해야** 알 수 있다. 버스가 조용해지면 마지막 손실은 다음 프레임 전까지 안 보인다.
</details>

### 연습 5 — 게이트웨이: 모든 인터페이스 소켓 + sendto

ifindex 0 에 bind 한 소켓 하나(FD 켬)로 vcan0·vcan1 을 함께 듣는다. `recvfrom` 의 `sockaddr_can.can_ifindex` 로 출처를 보고, vcan0 에서 온 SFF 데이터 0x100–0x1FF 만 ID + 0x400 으로 `sendto(ifindex = vcan1)`. FD 프레임은 vcan1(MTU 16)로 못 가므로 `fd_dropped`, vcan1 에서 온 것은 루프 방지로 무시. vcan1 모니터가 정확히 `0x1AB`(원래 vcan1 트래픽), `0x523`, `0x5FF` 만 보는지.

<details><summary>답</summary>

bind 하지 않은(ifindex 0) 소켓은 `write` 대신 `sendto` 로 목적지 인터페이스를 줘야 한다. 게이트웨이 자신이 보낸 프레임은 RECV_OWN_MSGS 가 꺼져 있어 다시 들어오지 않지만, 두 게이트웨이가 마주 보면 무한 루프가 생기므로 "출처 인터페이스로는 되돌려 보내지 않기"를 명시한다. 실측 `P5 gateway: fwd=2 ignored=2 fd_dropped=1 from_other_if=1`. 실제 제품에서는 커널 게이트웨이 `cangw`(linux/can/gw.h, 사용자 공간 왕복 없음)를 먼저 검토.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash $PWD/check.sh      # practice: 34/34 passed (ASan/UBSan 빌드도 34/34)
# VM 안에서 직접: gcc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice vcan0 vcan1
```

```text
P1 join filters: 12 rounds x 48 frames, delivered 194, kernel == AND-model in 12 rounds
P2 observe: sent 321##1AA -> received 321##5AA
P4 rcvbuf=2304: sent 200, read 3 (last id 2), drops stamped on those=0, on marker=197
P5 gateway: fwd=2 ignored=2 fd_dropped=1 from_other_if=1
34/34 passed
```

## 10. 핵심 정리

- SocketCAN = CAN 컨트롤러를 netdev 로, 응용은 `socket(PF_CAN, SOCK_RAW, CAN_RAW)` + `bind(ifindex)`. vcan = 버스 없는 가상 장치, "버스" 는 af_can 의 로컬 루프백뿐.
- 프레임 종류는 크기: 16 B `can_frame` / 72 B `canfd_frame`(같은 앞 배치). 72 B 버퍼로 읽고 반환값으로 분기. 수신은 늘 `poll` + 타임아웃(마감시각 고정).
- 필터: `(rx & mask) == (id & mask)` 를 32비트로 — SFF 데이터만이면 마스크 `0xC00007FF`. 여러 개 = OR(한 번만 전달), n = 0 = 아무것도 안 받음, 반전 `CAN_INV_FILTER`, AND 는 `JOIN_FILTERS`.
- 자기 프레임은 기본으로 안 보임(RECV_OWN_MSGS → `MSG_CONFIRM`). 에러 프레임은 `CAN_RAW_ERR_FILTER` 소켓에만, vcan 은 안 만든다.
- FD = 소켓 `CAN_RAW_FD_FRAMES` + MTU 72 이상 + 72 B 쓰기, 하나라도 빠지면 EINVAL. FD 안 켠 소켓엔 FD 프레임이 조용히 안 옴. 이 커널은 FDF(0x4) 를 붙여 돌려줌.
- 수신 큐는 기본 ≈ 222 프레임(실측). 넘치면 송신 측은 모른다 — `SO_RXQ_OVFL`, 큰 rcvbuf, 커널 필터.
- 실제 can0: 비트레이트·FD 설정은 down 상태에서(M06), ACK 없으면 TX 막힘, bus-off 는 에러 프레임 + restart-ms(M07).

## 참고자료

- Linux kernel documentation, *SocketCAN — Controller Area Network*: <https://docs.kernel.org/networking/can.html> (RAW 소켓 옵션, 필터 규칙, FD, 에러 마스크)
- 헤더 (이 VM 의 /usr/include/linux/can.h, can/raw.h, can/error.h): `struct can_frame`, `struct canfd_frame`, `CAN_RAW_FILTER_MAX` 512, `CANFD_BRS/ESI/FDF`, `CAN_ERR_*` — 크기·상수는 코드로 확인.
- 커널 소스 net/can/af_can.c (`can_send`, `can_rcv_list_find`, `can_rcv_filter`), net/can/raw.c (`raw_rcv`, JOIN_FILTERS), drivers/net/can/vcan.c — 본문 설명은 기억 기반, 동작은 실측. 원문 확인 권장: <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/net/can>
- can-utils (candump, cansend, cangen, canplayer, cangw): <https://github.com/linux-can/can-utils> — 이 레슨은 Ubuntu 패키지 2023.03 으로 실행.
- Bosch, *CAN Specification 2.0* / ISO 11898-1:2015 (CAN FD) — 프레임 형식, FDF/BRS/ESI 의미.
- 관련 레슨: COMM-M04 (프레임 비트·스터핑), COMM-M05 (중재), COMM-M06 (비트 타이밍 → `ip link ... bitrate`), COMM-M07 (TEC/REC·bus-off·restart-ms), COMM-M08 (CAN FD DLC·처리량), OS-B14 (IPC 와 소켓), SIV-B09 (poll-with-timeout 헬퍼), HWLAB-M05/M06 (로직 애널라이저·디코더), 다음 단계: OS-M13 (epoll), ISO-TP 소켓(`CAN_ISOTP`), `cangw`.
