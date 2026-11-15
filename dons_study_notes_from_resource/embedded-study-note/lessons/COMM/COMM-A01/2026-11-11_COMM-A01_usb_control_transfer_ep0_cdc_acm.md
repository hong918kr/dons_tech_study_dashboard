---
id: COMM-A01
time: 120분
updated: 2026-11-11
---
# COMM-A01 USB 컨트롤 전송 상태 머신 + CDC-ACM 클래스 요청

## 학습 목표

- 장치(device) 쪽 EP0 상태 머신(IDLE → DATA_IN / DATA_OUT → STATUS_OUT / STATUS_IN → IDLE, STALLED)을 malloc 없이 짜고, SETUP · DATA · STATUS 단계를 **트랜잭션(토큰 + 데이터 + 핸드셰이크) 단위**로 설명한다.
- 데이터 단계 길이 = min(실제, wLength), ZLP(zero-length packet) 규칙 "짧은 응답이고 mps 의 배수면 ZLP" 를 mps 8/16/32/64 × 실제 0..3·mps × wLength 전수(50,776 전송)로 증명하고, 그 규칙을 어긴 펌웨어가 왜 **수백 개 중 하나**에서만 터지는지 실측으로 보인다.
- 요청의 효과를 STATUS 단계가 끝난 뒤에 commit 하는 이유를 SET_ADDRESS(주소 0 으로 오는 status IN)에서 시작해 SET_LINE_CODING 까지 일반화하고, 새 SETUP 이 진행 중 전송을 끊을 때 무엇을 버려야 하는지 말한다.
- 결함을 하나씩 주입해 결과를 단언한다: wLength 0, 데이터 도중 새 SETUP, 지원 안 하는 요청 → STALL → 회복, wLength/버퍼보다 긴 OUT, 호스트 조기 종료, ACK 유실(데이터 토글로 중복 제거), SET_ADDRESS status ACK 유실.
- CDC-ACM 요청(SET/GET_LINE_CODING, SET_CONTROL_LINE_STATE, SEND/GET_ENCAPSULATED_*)을 구현하고, enumeration → `/dev/ttyACM0` open → `stty` → close 의 bring-up 로그를 읽는다.

## 1. 문제

USB 장치 펌웨어(STM32 USB FS/OTG, nRF52840 USBD, RP2040 + TinyUSB)를 bring-up 할 때 실제로 시간을 잡아먹는 곳은 bulk 데이터 경로가 아니라 **EP0 컨트롤 전송**이다 — "장치 설명자 요청 실패", 특정 문자열만 읽으면 멈춤, `stty` 하면 포트가 죽음은 거의 다 SET_ADDRESS 타이밍, ZLP, 조기 status, STALL 회복 중 하나다. 면접에서 "wLength 보다 짧게 답하면 호스트는 끝을 어떻게 아나?" 를 묻는 이유다.

COMM-M15 는 호스트 쪽(디스크립터 파서, SETUP 로그, `host_ctrl` 이 한 번에 처리하는 control 전송)이었다. 이 레슨은 반대편 — **SETUP 을 받은 장치가 DATA 와 STATUS 단계를 패킷 하나하나 책임지는 펌웨어**다. 모델은 세 층이다 (실제 스택의 경계와 같다: TinyUSB `dcd_*` ↔ `usbd.c`, STM32 HAL PCD ↔ USBD core).

| 층 | 이 레슨 | 하는 일 |
|---|---|---|
| SIE (하드웨어, 주어짐) | `sie_setup / sie_in / sie_out` | 주소 비교, EP0 IN/OUT 버퍼 무장(arm) 여부 → DATA / NAK / STALL, DATA0/1 토글, 중복 OUT 은 ACK 후 폐기 |
| EP0 펌웨어 (TODO) | `ep0_on_setup / ep0_on_in_done / ep0_on_out` + 요청 핸들러 | 상태 머신, 표준·CDC 요청, ZLP, commit |
| 호스트 시뮬레이터 (주어짐) | `host_xfer` | SETUP → DATA(IN/OUT 패킷들) → STATUS 를 트랜잭션 단위로 주입 + 결함 옵션 |

이 모델의 SIE 규칙(이 레슨의 정의 — 칩마다 다르니 RM 확인 권장): SETUP 은 항상 ACK 하고, 받는 순간 EP0 STALL 을 풀고 IN/OUT 둘 다 NAK(무장 해제), 토글은 DATA1 로 맞춘다(STM32 USB FS 주변장치가 SETUP 수신 시 STAT_TX/STAT_RX 를 NAK 로 바꾸는 것으로 기억 — 확인 권장). IN 은 호스트 ACK 를 받아야 펌웨어의 "IN 완료" 콜백이 불린다. 토글이 기대와 다른 OUT 은 재전송이므로 ACK 하고 버린다.

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">control 전송 세 가지 — 트랜잭션 = 토큰 + 데이터 + 핸드셰이크 (mps 64, 이 레슨 모델의 실제 흐름)</text>
  <text x="10" y="50" fill="currentColor" font-weight="bold">① control read: GET_DESCRIPTOR(STRING 2) wLength=255, 실제 64 B</text>
  <g stroke="currentColor">
    <rect x="10" y="60" width="150" height="40" class="f-acc"/>
    <rect x="170" y="60" width="170" height="40" class="f-ok"/>
    <rect x="350" y="60" width="150" height="40" class="f-warn"/>
    <rect x="530" y="60" width="200" height="40" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="18" y="77">SETUP + DATA0(8 B)</text><text x="18" y="93">장치 ACK (항상)</text>
    <text x="178" y="77">IN → DATA1 64 B</text><text x="178" y="93">호스트 ACK (꽉 찬 패킷)</text>
    <text x="358" y="77">IN → DATA0 ZLP</text><text x="358" y="93">"여기서 끝" 표시</text>
    <text x="538" y="77">OUT DATA1 ZLP → ACK</text><text x="538" y="93">STATUS (반대 방향)</text>
    <text x="10" y="118">SETUP 단계</text><text x="170" y="118">DATA 단계 (IN)</text><text x="530" y="118">STATUS 단계</text>
  </g>
  <text x="10" y="155" fill="currentColor" font-weight="bold">② control write: SET_LINE_CODING wLength=7</text>
  <g stroke="currentColor">
    <rect x="10" y="165" width="150" height="40" class="f-acc"/>
    <rect x="170" y="165" width="170" height="40" class="f-ok"/>
    <rect x="530" y="165" width="200" height="40" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="18" y="182">SETUP 21 20 .. 07 00</text><text x="18" y="198">장치 ACK</text>
    <text x="178" y="182">OUT DATA1 7 B → ACK</text><text x="178" y="198">7 &lt; 64 → 데이터 끝</text>
    <text x="538" y="182">IN → DATA1 ZLP → ACK</text><text x="538" y="198">여기서 라인 코딩 commit</text>
  </g>
  <text x="10" y="240" fill="currentColor" font-weight="bold">③ no-data: SET_ADDRESS(7) — 데이터 단계가 없으면 STATUS 는 항상 IN</text>
  <g stroke="currentColor">
    <rect x="10" y="250" width="150" height="40" class="f-acc"/>
    <rect x="530" y="250" width="200" height="40" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="18" y="267">@0 SETUP 00 05 07 00..</text><text x="18" y="283">장치 ACK</text>
    <text x="538" y="267">@0 IN → DATA1 ZLP → ACK</text><text x="538" y="283">그 다음에야 DADDR = 7</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="320">토글: SETUP 은 DATA0, 데이터 단계는 DATA1 부터 번갈아, STATUS 는 항상 DATA1.</text>
    <text x="10" y="340">데이터 단계의 끝 = wLength 를 다 채움, 또는 mps 보다 짧은 패킷(ZLP 포함) — 그 밖의 신호는 없다.</text>
  </g>
</svg>
```

**이 Mac 의 실제 장치.** CDC-ACM 장치는 꽂혀 있지 않았다 (`/dev/cu.*` 에는 `Bluetooth-Incoming-Port`, `debug-console` 뿐). 대신 Studio Display 가 같은 CDC 계열의 **NCM**(네트워크) 기능을 내보낸다 — ACM 과 똑같이 "통신 IF + 데이터 IF" 쌍이다 (실제 출력, 발췌):

```text
$ ioreg -p IOUSB -l -w0 -r -n "Studio Display@20140000" | grep -E 'bDeviceClass|bDeviceSubClass|bDeviceProtocol|bMaxPacketSize0|USBSpeed|bcdUSB'
      "bDeviceClass" = 239     "bDeviceSubClass" = 2     "bDeviceProtocol" = 1     (0xEF/02/01 = IAD 사용)
      "USBSpeed" = 3           "bcdUSB" = 528 (0x0210)   "bMaxPacketSize0" = 64
$ ioreg -l -w0 -r -c IOUSBHostInterface
+-o NCM Control@3   "bInterfaceClass" = 2   "bInterfaceSubClass" = 13 (0x0D NCM)   "bNumEndpoints" = 0
  +-o AppleUSBNCMControl
    +-o AppleUSBNCMData
      +-o anri0  <class AppleUSBHostNCMRestrictedEthernetInterface>
+-o NCM Data@4      "bInterfaceClass" = 10 (CDC-Data)  "bInterfaceProtocol" = 1   "bNumEndpoints" = 2
```

(줄을 모으고 괄호 주석을 붙였을 뿐, 값은 그대로.) NCM Control 은 EP 0개 — 알림 EP 없이 클래스 요청을 전부 EP0 로 주고받는다는 뜻이다. 이 레슨의 장치는 고전적인 CDC-ACM(device class 0x02, IAD 없음, config 67 B: 통신 IF0 + 기능 디스크립터 4개 + interrupt IN 0x82, 데이터 IF1 + bulk 0x01/0x81)이고, 문자열 2(제품명)를 일부러 31글자 = 64 B 로 만들어 **mps 8 과 64 둘 다에서 ZLP 가 필요한** 실제 디스크립터를 하나 둔다.

### Part A — EP0 상태 머신과 데이터 단계

1. `ep0_on_setup()`: SETUP 8바이트 디코드(LE16), 진행 중이던 전송의 상태·대기 효과를 버림(`aborts++`), 요청 종류(표준/클래스/벤더)로 분기, 거부면 `ep0_stall()`.
2. `ep0_send(p, avail)`: 길이 = min(avail, wLength), `tx_zlp = 길이 < wLength && 길이 % mps == 0`, wLength 0 이면 데이터 단계 없이 status IN. 호스트가 데이터를 덜 읽고 status 로 올 수 있게 **status OUT 을 미리 무장**. `ep0_arm_next_in()` 이 패킷 하나씩.
3. `ep0_on_in_done()` (호스트 ACK 후 SIE 가 부름): 남은 데이터/ZLP → 다음 패킷, 다 보냈으면 STATUS_OUT; STATUS_IN 완료면 `ep0_commit()` → IDLE.
4. `ep0_on_out(p, n)`: DATA_OUT 누적(초과 → STALL), 짧은 패킷이나 wLength 도달 → 내용 검증 → STATUS_IN. DATA_IN/STATUS_OUT 에서 0바이트 = status 완료(DATA_IN 이었다면 조기 status).
5. 테스트: device/config/string 디스크립터, **ZLP 전수 행렬** (벤더 루프백 요청 `0xC0/0x01`, wValue = 장치가 "가진" 바이트 수), 호스트 ACK 유실 4000건.

### Part B — 표준 요청과 결함 주입

`std_request()`(GET_DESCRIPTOR, SET_ADDRESS, SET/GET_CONFIGURATION, GET_STATUS)와 `ep0_commit()`. 주입하고 결과를 단언한다:

| 주입 | 기대 |
|---|---|
| GET_DESCRIPTOR wLength = 0 | 데이터 단계 없음, status IN, OK |
| Windows 식 첫 읽기 (64 요청, 첫 패킷 뒤 status) | OK, 장치는 `early_status`, IN 버퍼 회수 |
| 지원 안 하는 요청 / SET_FEATURE / DEVICE_QUALIFIER / 없는 문자열 / 방향 비트 틀림 | STALL (데이터 있으면 data 단계, 없으면 status 단계에서), 다음 SETUP 에서 회복 |
| SET_ADDRESS 단계별 | status IN 은 주소 0 으로만 응답, 그 뒤 DADDR = 새 주소, 주소 0 무시 |
| SET_ADDRESS 후 status 전에 새 SETUP | 주소 그대로 0, 끊긴 효과는 다음 요청의 status 에서도 commit 안 됨 |
| SET_ADDRESS status ACK 유실 | 장치는 주소 0 에 남음 → 새 주소 요청 타임아웃 → 버스 리셋 후 성공 |
| 상태별 거부 | Default 에서 SET_CONFIGURATION, 주소 128, 구성 2, Configured 에서 SET_ADDRESS, 구성 전 CDC 요청 → STALL |
| 데이터 도중 새 SETUP (IN / OUT) | 남은 패킷 폐기, 새 요청 정확히 처리, 받다 만 데이터는 commit 안 됨 |
| OUT 이 버퍼/wLength 보다 김 | wLength 65 > 버퍼 64 → SETUP 에서 STALL, 호스트가 wLength 초과 전송 → STALL |
| 잘못된 라인 코딩 | status 단계 STALL, 값 유지 |
| OUT 핸드셰이크 유실 (랜덤 3000) | 같은 토글 재전송 → ACK 후 폐기, 받은 내용 = 보낸 내용 |

### Part C — CDC-ACM 요청과 bring-up 로그

1. `cdc_request()`: SET_LINE_CODING(7 B OUT), GET_LINE_CODING, SET_CONTROL_LINE_STATE(wValue bit0 DTR, bit1 RTS), SEND_ENCAPSULATED_COMMAND / GET_ENCAPSULATED_RESPONSE(에코). 수신자 = 통신 IF 0, Configured 에서만.
2. `line_coding_ok()`: dwDTERate ≠ 0, bCharFormat 0..2, bParityType 0..4, bDataBits ∈ {5,6,7,8,16}.
3. bring-up: 꽂힘 → 첫 읽기 → 리셋 → SET_ADDRESS(7) → device → DEVICE_QUALIFIER(STALL) → config 9 → 67 → 문자열 0..3 → SET_CONFIGURATION(1) → open(DTR|RTS, 115200 8N1, GET) → `stty 9600 cs7 parenb -parodd cstopb` → close. mps 8 과 64 둘 다, 최종 상태와 카운터를 단언.

## 2. Problem (English)

> Implement the device-side EP0 control-transfer engine for a full-speed CDC-ACM device (no malloc, fixed buffers). The SIE calls you on three events; you arm/disarm the EP0 IN/OUT buffers and set STALL.
>
> ```c
> void ep0_on_setup(usbd *d);                                  /* 8-byte SETUP in d->hw.setup        */
> void ep0_on_in_done(usbd *d);                                /* host ACKed the IN packet we armed   */
> void ep0_on_out(usbd *d, const uint8_t *p, uint16_t n);      /* new OUT packet (duplicates filtered) */
> void ep0_send(usbd *d, const uint8_t *p, uint16_t avail);     /* start an IN data stage              */
> int  std_request(usbd *d);  int cdc_request(usbd *d);         /* 0 = stage started, -1 = STALL       */
> ```
>
> Requirements: send min(avail, wLength) bytes in packets of bMaxPacketSize0; if that is shorter than wLength and a multiple of the packet size (including 0), terminate with a zero-length packet, otherwise never send one. wLength = 0 means no data stage and an IN status stage. Accept a status OUT at any time during an IN data stage. A new SETUP aborts whatever was in progress. SET_ADDRESS must take effect only after its status stage completes; generalize this: every side effect is committed after the status stage. Support GET_DESCRIPTOR (device/config/string), SET_ADDRESS, SET/GET_CONFIGURATION, GET_STATUS, and the CDC-ACM requests SET/GET_LINE_CODING, SET_CONTROL_LINE_STATE, SEND/GET_ENCAPSULATED_*; STALL everything else.
>
> Example (mps 8): `80 06 02 03 09 04 FF 00` for a 64-byte string → `8×8 B + ZLP`, then the host's OUT ZLP. `21 20 00 00 00 00 07 00` + `80 25 00 00 02 02 07` → line coding 9600 7E2 after the IN ZLP.
>
> Follow-ups: (1) Why exactly is the ZLP needed only when the reply is short *and* a multiple of mps? (2) The host's ACK to your last data packet is lost — what does your state machine see next? (3) The status ACK of SET_ADDRESS is lost — who recovers, and how? (4) Where does an extra OUT byte beyond wLength surface, and why not earlier?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 9개: A1 `ep0_arm_next_in` + `ep0_send`, A2 `ep0_on_in_done`, A3 `ep0_on_out`, A4 `ep0_on_setup`, B1 `std_request`, B2 `ep0_commit`, C1 `cdc_request`, C2 `line_coding_ok`. SIE, 호스트 시뮬레이터(`host_xfer`), 디스크립터, 로그, `ep0_stall / ep0_status_only / ep0_recv / ep0_done` 같은 작은 헬퍼, `get_descriptor`, `vendor_request`, 하네스는 주어진다. 스텁은 "모든 SETUP 을 STALL" 이므로 호스트는 즉시 STALL 을 보고, NAK 루프도 `NAK_LIMIT`(16)으로 끊겨 모든 테스트가 끝난다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:522  xfer(&g_d, 0, 0x80, REQ_GET_DESCRIPTOR, DT_DEVICE << 8, 0, 64, NULL, NULL, &r) == X_OK
FAIL starter.c:523  r.actual == 18 && r.pkts == 1 && r.zlps == 0 && memcmp(g_in, g_d.dev_desc, 18) == 0 && g_in[7] == 64
…
A4 ZLP matrix mps  8: 700 transfers, 700 bad
A4 ZLP matrix mps 64: 37828 transfers, 37828 bad
…
FAIL starter.c:747  strstr(g_log, "DADDR 0 -> 7") != NULL && strstr(g_log, "DEVICE: addr 7, config 1, line 9600 7E2") != NULL
12/103 passed
```

(exit 1, 0.1 초 안에 끝남.) 통과하는 12개는 "STALL 이 정답" 인 거부 테스트들(DEVICE_QUALIFIER, 없는 문자열, Default 상태의 SET_CONFIGURATION …)이다 — 다 거부하는 장치도 거부 테스트는 통과한다. 그래서 거부 테스트 뒤에는 항상 "그다음 정상 요청이 성공" 을 같이 단언한다.

## 4. 힌트

<details><summary>힌트 1 — 펌웨어는 "다음에 올 토큰" 을 위해 버퍼를 미리 무장한다</summary>

SIE 는 펌웨어에게 묻지 않는다. IN 토큰이 오면 IN 버퍼가 무장돼 있으면 DATA, 아니면 NAK. 그래서 펌웨어의 모든 콜백은 "다음 IN/OUT 토큰이 왔을 때 무엇이 나가야 하나" 를 준비하고 끝난다. IN 데이터 단계에서는 IN(다음 패킷) **과** OUT(status ZLP) 둘 다 무장한다 — 호스트가 어느 쪽을 보낼지 모른다.
</details>

<details><summary>힌트 2 — ZLP 는 "호스트가 끝을 알 수 없는 경우" 에만</summary>

호스트는 (a) wLength 만큼 받았거나 (b) mps 보다 짧은 패킷을 받으면 끝낸다. 보낼 길이 n 이 wLength 와 같으면 (a)로 끝나니 ZLP 불필요. n &lt; wLength 인데 마지막 패킷이 짧으면 (b)로 끝난다. n &lt; wLength 이고 n 이 mps 의 배수(0 포함)일 때만 둘 다 아니다 → ZLP.
</details>

<details><summary>힌트 3 — "IN 완료" 는 ACK 를 받았을 때만 온다</summary>

호스트 ACK 가 유실되면 SIE 는 같은 패킷·같은 토글을 다시 내보내고, 호스트는 토글을 보고 중복을 버린다. 펌웨어는 아무것도 할 필요 없다. 단 하나 예외: **마지막** 데이터 패킷의 ACK 가 유실되면 호스트는 이미 status OUT 으로 넘어간다 — 펌웨어는 DATA_IN 상태에서 OUT ZLP 를 받는다. 조기 status 경로가 이 경우도 처리한다.
</details>

<details><summary>힌트 4 — 효과는 status 뒤에</summary>

SET_ADDRESS 의 status IN 은 옛 주소(0)로 온다. 그러니 주소는 status IN 이 ACK 된 뒤(`ep0_on_in_done` 의 STATUS_IN 분기)에만 바꾼다. 같은 자리(`ep0_commit`)에서 구성·라인 코딩·제어선도 적용하면, 중간에 끊긴 요청은 자동으로 효과가 없다. 새 SETUP 에서 `act = ACT_NONE` 으로 지우는 것을 잊으면 끊긴 SET_ADDRESS 가 **다음 요청의 status** 에서 적용된다.
</details>

<details><summary>힌트 5 — OUT 초과는 어디서 보이나</summary>

SIE 는 패킷을 ACK 한 **뒤에** 펌웨어를 부른다. 펌웨어가 "넘쳤다" 고 STALL 해도 그 패킷은 이미 ACK 됐으니, 호스트는 다음 트랜잭션(보통 status IN)에서 STALL 을 본다. 반대로 wLength 가 버퍼보다 크면 SETUP 에서 바로 거절할 수 있다 — 첫 OUT 부터 STALL.
</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "control 전송 = SETUP / DATA / STATUS. 데이터 단계는 wLength 를 채우거나 짧은 패킷으로 끝나고, status 는 데이터와 반대 방향의 ZLP, 데이터가 없으면 IN."
2. "장치 펌웨어는 SIE 이벤트 3개(SETUP, IN 완료, OUT 수신)에 반응하는 상태 머신. 각 콜백은 다음 토큰을 위한 버퍼를 무장하고 끝난다."
3. "ZLP 는 n &lt; wLength && n % mps == 0 일 때만. 효과는 status 뒤에 commit — SET_ADDRESS 가 그걸 강제한다."
4. "새 SETUP 은 언제든 온다: 전송 상태와 대기 효과를 버리고 처음부터. protocol STALL 은 그 SETUP 이 푼다."

### Part A — 상태 머신

```svg
<svg viewBox="0 0 740 420" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">EP0 상태 머신 (solution.c) — 화살표 = SIE 이벤트 / 펌웨어 동작</text>
  <g stroke="currentColor">
    <rect x="300" y="40" width="140" height="40" rx="8" class="f-acc"/>
    <rect x="60" y="160" width="150" height="40" rx="8" class="f-ok"/>
    <rect x="530" y="160" width="150" height="40" rx="8" class="f-ok"/>
    <rect x="60" y="300" width="150" height="40" rx="8" class="f-mut"/>
    <rect x="530" y="300" width="150" height="40" rx="8" class="f-mut"/>
    <rect x="300" y="300" width="140" height="40" rx="8" class="f-warn"/>
  </g>
  <g fill="currentColor" font-weight="bold">
    <text x="345" y="65">IDLE</text><text x="100" y="185">DATA_IN</text><text x="565" y="185">DATA_OUT</text>
    <text x="85" y="325">STATUS_OUT</text><text x="565" y="325">STATUS_IN</text><text x="330" y="325">STALLED</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M300,70 L210,160"/><path d="M440,70 L530,160"/>
    <path d="M135,200 L135,300"/><path d="M605,200 L605,300"/>
    <path d="M60,320 Q20,60 300,55"/><path d="M680,320 Q730,60 440,55"/>
    <path d="M370,80 L370,300"/>
    <path d="M440,48 Q560,30 560,160"/>
    <path d="M210,170 Q300,120 300,62"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="150" y="105">SETUP IN, wLength&gt;0</text><text x="150" y="121">ep0_send: arm IN + OUT</text>
    <text x="470" y="105">SETUP OUT, wLength&gt;0</text><text x="470" y="121">ep0_recv: arm OUT</text>
    <text x="142" y="240">IN 완료 &amp; 남은 것 없음</text><text x="142" y="256">(남았으면 다음 패킷/ZLP)</text>
    <text x="440" y="240">짧은 패킷 or wLength 도달</text><text x="440" y="256">→ 검증 OK, arm IN ZLP</text>
    <text x="10" y="395">OUT ZLP (status 완료) → ep0_done</text>
    <text x="520" y="395">IN ZLP ACK → ep0_commit → done</text>
    <text x="378" y="200">요청 거부 / 초과 /</text><text x="378" y="216">검증 실패 → STALL</text>
    <text x="545" y="40">wLength = 0: 바로 STATUS_IN</text>
    <text x="215" y="145">OUT ZLP (조기 status)</text>
  </g>
  <text x="10" y="370" fill="currentColor" font-size="12">어느 상태에서든: 새 SETUP → (aborts++) IDLE 에서 다시 분기. STALLED 는 SETUP 만 푼다.</text>
</svg>
```

전이표 (코드 그대로):

| 상태 | 이벤트 | 동작 | 다음 |
|---|---|---|---|
| 아무 상태 | SETUP | 디코드, tx/rx/act 지움, IDLE 아닌 데서 왔으면 aborts++, 핸들러 | 핸들러가 정함 / STALLED |
| DATA_IN | IN 완료 | `tx_left > 0 \|\| tx_zlp` → 다음 패킷 | DATA_IN |
| DATA_IN | IN 완료, 남은 것 없음 | (status OUT 은 이미 무장) | STATUS_OUT |
| DATA_IN | OUT 0 B | early_status++, IN 회수 | IDLE |
| STATUS_OUT | OUT 0 B / &gt;0 B | done / STALL | IDLE / STALLED |
| DATA_OUT | OUT n B | 초과 → STALL; 끝이면 검증 → arm IN ZLP + OUT(초과 감시) | STATUS_IN / DATA_OUT |
| STATUS_IN | IN 완료 | `ep0_commit` | IDLE |
| STATUS_IN | OUT (새 패킷) | 호스트가 wLength 넘게 보냄 → STALL | STALLED |

추적 — GET_DESCRIPTOR(STRING 2) wLength 255, mps 8, 64 B (bring-up 로그의 그 요청):

| # | 트랜잭션 | 토글 | 길이 | 펌웨어 후 tx_left / tx_zlp | 상태 |
|---|---|---|---|---|---|
| 0 | SETUP | DATA0 | 8 | n = min(64, 255) = 64, 64 &lt; 255 && 64 % 8 == 0 → zlp=yes, 첫 패킷 무장 → 56 / yes | DATA_IN |
| 1–7 | IN | 1,0,1,0,1,0,1 | 8 | 48 … 0 / yes (8번째 패킷 무장 시 tx_left 0) | DATA_IN |
| 8 | IN | DATA0 | 8 | tx_left 0, zlp yes → ZLP 무장, zlp_armed++ → 0 / no | DATA_IN |
| 9 | IN | DATA1 | 0 | 남은 것 없음 | STATUS_OUT |
| 10 | OUT | DATA1 | 0 | status 완료 | IDLE |

데이터 패킷 수 공식 (a = min(n, wLength)): wLength = 0 → 0, a &lt; wLength → ⌊a/mps⌋ + 1 (마지막이 짧은 패킷이거나 ZLP), a = wLength → ⌈a/mps⌉. 위 예: ⌊64/8⌋ + 1 = 9. 하네스의 `expect_pkts` 가 이 식이고, A4 가 전수로 대조한다.

**ZLP 전수 행렬 (실제 출력).** 벤더 루프백 요청으로 장치가 가진 길이 n = 0..3·mps 와 wLength = 0..3·mps+1, 255, 0xFFFF 의 모든 조합을 보내고, 각 전송에서 결과 OK · 데이터 일치 · 패킷 수 = 공식 · ZLP 수 = 규칙 · 조기 status 없음 · 끝난 뒤 IN/OUT 버퍼 비무장을 확인한다:

```text
A4 ZLP matrix mps  8: 700 transfers, 0 bad
A4 ZLP matrix mps 16: 2548 transfers, 0 bad
A4 ZLP matrix mps 32: 9700 transfers, 0 bad
A4 ZLP matrix mps 64: 37828 transfers, 0 bad
```

"ZLP 가 남는" 버그는 호스트 눈에는 안 보인다(호스트는 이미 status 로 넘어간다). 그래서 장치 쪽 `early_status` 카운터를 단언한다 — 필요 없는 ZLP 를 무장한 장치는 DATA_IN 에 머문 채 status OUT 을 받기 때문이다. 이 모델(그리고 SETUP 에서 양방향 NAK 로 돌리는 STM32 FS 류)에서는 남은 ZLP 가 다음 SETUP 에 지워져 무해해 보이지만, 버퍼를 자동 회수하지 않는 컨트롤러라면 다음 control read 의 첫 패킷이 0바이트로 나가 전송이 잘린다 (컨트롤러별 확인 권장).

**ACK 유실 (A5, 랜덤 4000건).** 트랜잭션마다 약 25% 확률로 호스트 ACK 를 잃게 하고 단언: 데이터 동일, 패킷 수 동일, 호스트가 버린 중복 수 = (마지막을 뺀) 유실 수, 마지막 데이터 패킷의 유실 = 장치의 `early_status` 1. 마지막 패킷의 ACK 를 잃으면 호스트는 데이터가 끝난 줄 알고(짧은 패킷 또는 wLength 도달) status OUT 을 보내고, 장치는 아직 그 패킷이 "안 나간" 상태다. 조기 status 경로가 없으면 여기서 멈춘다 — 그래서 status OUT 을 **데이터 단계 시작 때** 무장하는 것이 정답이다.

### Part B — SET_ADDRESS 와 "status 뒤 commit"

USB 2.0 §9.4.6 의 규칙: 장치는 SET_ADDRESS 의 status 단계가 끝난 뒤 주소를 바꾼다. status IN 토큰은 아직 주소 0 으로 오기 때문이다. B4 는 이걸 SIE 수준에서 한 단계씩 본다: SETUP 직후 `hw.addr == 0`, 주소 5 로 보낸 IN 은 무응답, 주소 0 으로 보낸 IN 이 DATA1 ZLP 를 받고, 그 ACK 와 함께 `DADDR 0 -> 5`. 이후 주소 0 SETUP 은 무시된다.

이 레슨은 그 규칙을 일반화했다 — **모든 효과를 `ep0_commit()` 한 곳에서, status ACK 뒤에**. 이점:

- 데이터 단계 도중이나 status 전에 새 SETUP 이 오면 효과가 자동으로 사라진다(B5, B10). 라인 코딩을 데이터 수신 즉시 적용하는 M6 변이는 B10 에서 잡힌다.
- 내용 검증은 데이터가 다 왔을 때 하고(실패하면 status 를 STALL), 적용은 성공한 뒤 — "반쯤 적용된 상태" 가 없다.

트레이드오프: status 의 ACK 가 유실되면 호스트는 성공으로 알고 장치는 적용하지 않는다(아래 로그 3). SET_ADDRESS 는 호스트의 다음 요청이 타임아웃 → 포트 리셋 → 재열거로 복구된다. SET_LINE_CODING 이라면 장치의 UART 설정이 호스트 생각과 달라진 채 남는다 — 실제 스택 다수(TinyUSB 는 CDC 라인 코딩 콜백을 ACK 단계에서 부르는 것으로 기억, ST 라이브러리는 데이터 단계 완료 시점 — 둘 다 확인 권장)가 이 위험을 감수한다. 확률은 비트 오류율 수준이라 실무에서는 GET_LINE_CODING 으로 재확인하는 호스트 쪽 습관이 더 현실적이다.

**결함 시나리오 로그** (`./solution faults` 실제 출력, 시나리오 1 의 SET_ADDRESS/SET_CONFIGURATION 부분 생략):

```text
==== (1) SETUP mid-data: host abandons a 40 B OUT after 2 packets, then GET_ENCAPSULATED_RESPONSE ====
@7 SETUP 21 00 00 00 00 00 28 00  SEND_ENCAPSULATED_COMMAND wValue=0x0000 wIndex=0x0000 wLength=40
      ep0  IDLE -> DATA_OUT (expect 40 B)
  OUT  DATA1  8 B -> ACK
  OUT  DATA0  8 B -> ACK
  (host abandons transfer)
  => ABANDONED at data stage
@7 SETUP A1 01 00 00 00 00 40 00  GET_ENCAPSULATED_RESPONSE wValue=0x0000 wIndex=0x0000 wLength=64
      ep0  DATA_OUT -> IDLE
      ep0  IDLE -> DATA_IN  (send 0 B, zlp yes)
  IN   DATA1 ZLP
      ep0  DATA_IN -> STATUS_OUT
  OUT  DATA1 ZLP -> ACK
      ep0  STATUS_OUT -> IDLE
  => OK  0 B IN in 1 pkt (incl. ZLP)
==== (2) host ACK lost on the LAST data packet (mps 8, 18 B) ====
@7 SETUP 80 06 00 01 00 00 12 00  GET_DESCRIPTOR(DEVICE) wValue=0x0100 wIndex=0x0000 wLength=18
      ep0  IDLE -> DATA_IN  (send 18 B, zlp no)
  IN   DATA1  8 B
  IN   DATA0  8 B
  IN   DATA1  2 B   (host ACK lost)
  OUT  DATA1 ZLP -> ACK
      ep0  DATA_IN -> IDLE
  => OK  18 B IN in 3 pkt
==== (3) SET_ADDRESS status ACK lost -> device stays at 0 -> bus reset ====
RESET  (Default state, address 0)
@0 SETUP 00 05 09 00 00 00 00 00  SET_ADDRESS wValue=0x0009 wIndex=0x0000 wLength=0
      ep0  IDLE -> STATUS_IN
  IN   DATA1 ZLP   (host ACK lost)
  => OK  (no data stage)
@9 SETUP 80 06 00 01 00 00 12 00  GET_DESCRIPTOR(DEVICE) wValue=0x0100 wIndex=0x0000 wLength=18
  SETUP no response
  => TIMEOUT at setup stage
RESET  (Default state, address 0)
@0 SETUP 00 05 09 00 00 00 00 00  SET_ADDRESS wValue=0x0009 wIndex=0x0000 wLength=0
      ep0  IDLE -> STATUS_IN
  IN   DATA1 ZLP
      hw   DADDR 0 -> 9
      ep0  STATUS_IN -> IDLE
  => OK  (no data stage)
```

읽는 법: (1) 새 SETUP 이 `DATA_OUT -> IDLE` 로 진행 중 전송을 끊고, 받다 만 16 B 는 commit 되지 않아 응답은 빈 것(0 B 데이터 = ZLP 하나). 응답이 없을 때 0바이트로 답하는 것은 이 모델의 정의다 (CDC 1.2 의 정확한 규정은 확인 권장). (2) 마지막 패킷의 ACK 유실 → 장치는 `DATA_IN -> IDLE` (STATUS_OUT 을 거치지 않음) — 조기 status 경로가 정상 종료로 처리. (3) 호스트는 OK 로 알지만 DADDR 은 바뀌지 않았다.

**STALL 은 어디서 보이나.** 데이터 단계가 있는 요청을 거부하면 첫 데이터 토큰에서(B3 `0x42` → `STALL at data stage`), 데이터 없는 요청이면 status IN 에서(`SET_FEATURE` → status 단계). wLength 를 넘은 OUT 은 SIE 가 이미 ACK 한 뒤 펌웨어가 판단하므로 **다음** 트랜잭션에서 보인다: SET_LINE_CODING + 1 B(8 B 한 패킷) → status 단계 STALL, encapsulated 40 + 8 B → 6번째 패킷은 ACK, status 에서 STALL. 둘 다 효과는 commit 되지 않는다.

**OUT 중복 (B13, 랜덤 3000건).** 장치 ACK 를 약 25% 확률로 잃게 하면 호스트는 같은 토글로 재전송하고 SIE 는 토글 불일치를 보고 ACK + 폐기한다. 단언: 저장된 명령 = 보낸 바이트(중복이 두 번 쌓이지 않음), `dup_out` = 유실 수, GET_ENCAPSULATED_RESPONSE 로 되읽은 값 = 보낸 값.

### 실험 — `demo_bugs.c` (실측)

같은 모델에 흔한 EP0 버그 4개를 스위치로 넣고, ZLP 행렬(wLength 1..3·mps+1, 255, 0xFFFF)을 깨끗한 링크와 5% ACK 유실 링크에서, 그리고 Windows 식 첫 읽기를 돌렸다. host-fail = 호스트가 OK 가 아니거나 데이터가 틀림, dev-anom = 호스트는 OK 인데 장치가 조기 status 로 끝남.

```text
firmware                         mps | clean: host-fail / dev-anom | 5% ACK loss: fail / anom   | win 1st read
correct                            8 |     0 /    0  of   675    |     0 /    0  of   675    | OK
correct                           64 |     0 /    0  of 37635    |     0 /    0  of 37635    | OK
no ZLP at all                      8 |    33 /    0  of   675    |    33 /    0  of   675    | OK
no ZLP at all                     64 |   201 /    0  of 37635    |   201 /    0  of 37635    | OK
ZLP also when n == wLength         8 |     0 /   27  of   675    |     0 /   23  of   675    | OK
ZLP also when n == wLength        64 |     0 /  195  of 37635    |     0 /  187  of 37635    | OK
0-byte reply: no data ZLP          8 |    27 /    0  of   675    |    27 /    0  of   675    | OK
0-byte reply: no data ZLP         64 |   195 /    0  of 37635    |   195 /    0  of 37635    | OK
status-OUT armed late              8 |     0 /    0  of   675    |    35 /    0  of   675    | TIMEOUT
status-OUT armed late             64 |     0 /    0  of 37635    |  1805 /    0  of 37635    | OK
```

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">호스트가 실패를 보는 비율 (demo_bugs.c 실측, 막대 1% = 50 px)</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="58">no ZLP, mps 8</text><text x="10" y="88">no ZLP, mps 64</text>
    <text x="10" y="118">ZLP when n==wL, 8</text><text x="10" y="148">ZLP when n==wL, 64</text>
    <text x="10" y="178">empty no ZLP, mps 8</text><text x="10" y="208">empty no ZLP, mps 64</text>
    <text x="10" y="238">late status-OUT, 8</text><text x="10" y="268">late status-OUT, 64</text>
  </g>
  <g stroke="currentColor">
    <rect x="160" y="46" width="244" height="10" class="f-warn"/><rect x="160" y="57" width="244" height="8" class="f-mut"/>
    <rect x="160" y="76" width="27" height="10" class="f-warn"/><rect x="160" y="87" width="27" height="8" class="f-mut"/>
    <rect x="160" y="196" width="26" height="10" class="f-warn"/><rect x="160" y="207" width="26" height="8" class="f-mut"/>
    <rect x="160" y="166" width="200" height="10" class="f-warn"/><rect x="160" y="177" width="200" height="8" class="f-mut"/>
    <rect x="160" y="237" width="259" height="8" class="f-mut"/>
    <rect x="160" y="267" width="240" height="8" class="f-mut"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="410" y="60">4.9% / 4.9%</text><text x="193" y="90">0.53% / 0.53%</text>
    <text x="166" y="120">host 0% (장치 쪽 조기 status 4.0%)</text><text x="166" y="150">host 0% (장치 쪽 0.52%)</text>
    <text x="366" y="180">4.0% / 4.0%</text><text x="192" y="210">0.52% / 0.52%</text>
    <text x="166" y="230">clean 0%</text><text x="425" y="245">5.2% + 첫 읽기 TIMEOUT</text>
    <text x="166" y="260">clean 0%</text><text x="406" y="275">4.8%</text>
  </g>
  <g stroke="currentColor"><rect x="160" y="296" width="20" height="10" class="f-warn"/><rect x="330" y="296" width="20" height="10" class="f-mut"/></g>
  <g fill="currentColor" font-size="12"><text x="186" y="305">깨끗한 링크</text><text x="356" y="305">5% ACK 유실</text>
    <text x="10" y="325">비율 = 실패 조합 / 행렬 조합 수 (675 또는 37635). 실제 펌웨어가 만나는 길이 분포와는 다르다.</text></g>
</svg>
```

해석:

- **ZLP 누락**은 mps 64 에서 조합의 0.53% 에서만 터진다 — n 이 64·k 이면서 wLength 보다 짧을 때. 디스크립터가 우연히 64 B 가 되는 날(문자열 31글자, config 64 B) 나타나는 "어떤 제품명으로 바꿨더니 열거가 안 돼요" 버그다. 손 계산과 일치: mps 8 에서 n ∈ {8,16,24}, wLength &gt; n 인 조합 19 + 11 + 3 = 33.
- **ZLP 를 너무 많이** 보내는 버그는 호스트 눈에 0% — 장치 카운터로만 보인다. 테스트가 장치 상태를 단언해야 하는 이유.
- **빈 응답에 아무것도 안 보내는** 버그(“보낼 게 없으니 바로 STATUS_OUT”)는 n = 0 행 전체(mps 8: 25 + 2 = 27)에서 NAK 타임아웃.
- **status OUT 늦게 무장**은 깨끗한 링크에서 0% 라 단위 테스트를 통과하고, 실제로는 (a) Windows 식 첫 GET_DESCRIPTOR(mps 8 장치는 바로 TIMEOUT) 와 (b) 마지막 데이터 ACK 유실(5% 링크에서 약 5%)에서 터진다. "어떤 PC 에서만, 가끔" 의 전형.

### Part C — CDC-ACM 요청

| 요청 | bmRequestType | bRequest | wValue | wIndex | wLength / 데이터 |
|---|---|---|---|---|---|
| SEND_ENCAPSULATED_COMMAND | 0x21 | 0x00 | 0 | 통신 IF | n B OUT (이 모델 1..64) |
| GET_ENCAPSULATED_RESPONSE | 0xA1 | 0x01 | 0 | 통신 IF | 응답 IN (에코) |
| SET_LINE_CODING | 0x21 | 0x20 | 0 | 통신 IF | 7 B OUT |
| GET_LINE_CODING | 0xA1 | 0x21 | 0 | 통신 IF | 7 B IN |
| SET_CONTROL_LINE_STATE | 0x21 | 0x22 | bit0 DTR, bit1 RTS | 통신 IF | 0 |
| SEND_BREAK | 0x21 | 0x23 | 지속 ms | 통신 IF | 0 — ACM caps bit2 를 광고 안 했으니 STALL |

(CDC 1.2 / PSTN 1.2 의 값으로 기억 — 스펙 확인 권장. 0x21 = OUT · class · interface.) 라인 코딩 7바이트: `dwDTERate`(LE32) `bCharFormat`(0 = 1 stop, 1 = 1.5, 2 = 2) `bParityType`(0 none, 1 odd, 2 even, 3 mark, 4 space) `bDataBits`(5, 6, 7, 8, 16). 9600 7E2 = `80 25 00 00 02 02 07`. 기능 디스크립터의 ACM bmCapabilities 0x02 가 "SET/GET_LINE_CODING, SET_CONTROL_LINE_STATE, SERIAL_STATE 지원" 이고, encapsulated 쌍은 PSTN 표에서 ACM 필수로 기억한다(확인 권장 — Linux cdc-acm 은 tty 경로에서 쓰지 않는다; RNDIS 가 이 쌍으로 메시지를 나른다).

Linux 쪽 동작은 `drivers/usb/class/cdc-acm.c` 를 직접 확인했다: `acm_tty_set_termios()` 가 `bCharFormat = CSTOPB ? 2 : 0`, `bParityType = PARENB ? (PARODD ? 1 : 2) + (CMSPAR ? 2 : 0) : 0`, `bDataBits = tty_get_char_size()` 로 만들고, B0 이면 rate 는 그대로 두고 DTR 만 내린다. open 시 `acm_port_dtr_rts()` 가 DTR|RTS 를 SET_CONTROL_LINE_STATE 로 보낸다. 이 레슨 bring-up 의 순서(DTR|RTS → SET_LINE_CODING → GET)는 그 흐름을 단순화한 모델이다.

bring-up 로그 (`./solution log 8` 실제 출력, 발췌):

```text
---- plug-in, EP0 mps 8 ----
@0 SETUP 80 06 00 01 00 00 40 00  GET_DESCRIPTOR(DEVICE) wValue=0x0100 wIndex=0x0000 wLength=64
      ep0  IDLE -> DATA_IN  (send 18 B, zlp no)
  IN   DATA1  8 B
  (host stops data stage early)
  OUT  DATA1 ZLP -> ACK
      ep0  DATA_IN -> IDLE
  => OK  8 B IN in 1 pkt
RESET  (Default state, address 0)
@0 SETUP 00 05 07 00 00 00 00 00  SET_ADDRESS wValue=0x0007 wIndex=0x0000 wLength=0
      ep0  IDLE -> STATUS_IN
  IN   DATA1 ZLP
      hw   DADDR 0 -> 7
      ep0  STATUS_IN -> IDLE
  => OK  (no data stage)
…
@7 SETUP 80 06 02 03 09 04 FF 00  GET_DESCRIPTOR(STRING) wValue=0x0302 wIndex=0x0409 wLength=255
      ep0  IDLE -> DATA_IN  (send 64 B, zlp yes)
  IN   DATA1  8 B
  …(8 B 패킷 7개 더)
  IN   DATA1 ZLP
      ep0  DATA_IN -> STATUS_OUT
  OUT  DATA1 ZLP -> ACK
      ep0  STATUS_OUT -> IDLE
  => OK  64 B IN in 9 pkt (incl. ZLP)
…
---- stty -F /dev/ttyACM0 9600 cs7 parenb -parodd cstopb ----
@7 SETUP 21 20 00 00 00 00 07 00  SET_LINE_CODING wValue=0x0000 wIndex=0x0000 wLength=7
      ep0  IDLE -> DATA_OUT (expect 7 B)
  OUT  DATA1  7 B -> ACK
      ep0  DATA_OUT -> STATUS_IN
  IN   DATA1 ZLP
      ep0  STATUS_IN -> IDLE
  => OK  7 B OUT in 1 pkt
     dev  line coding 115200 8N1 -> 9600 7E2
---- close ----
@7 SETUP 21 22 00 00 00 00 00 00  SET_CONTROL_LINE_STATE wValue=0x0000 wIndex=0x0000 wLength=0
      ep0  IDLE -> STATUS_IN
  IN   DATA1 ZLP
      ep0  STATUS_IN -> IDLE
  => OK  (no data stage)
     dev  DTR 1 RTS 1 -> DTR 0 RTS 0
DEVICE: addr 7, config 1, line 9600 7E2, DTR 0 RTS 0 | setups 16 ok 15 stalls 1 aborts 0 early-status 1 zlp 2
bring-up OK
```

마지막 줄이 bring-up 엔지니어가 보는 요약이다: SETUP 16, 성공 15, STALL 1(DEVICE_QUALIFIER — FS 전용 장치의 정답), 조기 status 1(첫 읽기), ZLP 2(16 B 문자열 1, 64 B 문자열 2). mps 64 로 돌리면 첫 읽기가 18 B 한 패킷(짧은 패킷이라 정상 종료)이므로 `early-status 0`, ZLP 는 64 B 문자열 하나뿐이라 `zlp 1` — 테스트가 두 경우를 각각 단언한다.

**실제 스택의 분할 (확인 권장).** TinyUSB: `dcd_*`(칩별)가 SETUP 수신·전송 완료를 이벤트로 올리고, `usbd_control.c` 가 데이터 단계를 패킷으로 쪼개며 ZLP 를 판단, 클래스 드라이버는 `control_xfer_cb(rhport, stage, request)` 를 SETUP / DATA / ACK 단계마다 받는다 — 이 레슨의 `ep0_on_setup / ep0_out_complete / ep0_commit` 과 같은 분할. STM32 USB Device Library: `USBD_LL_SetupStage → USBD_StdDevReq / 클래스 Setup`, `USBD_CtlSendData / USBD_CtlPrepareRx`, `EP0_TxSent / EP0_RxReady` 콜백. SET_ADDRESS 는 코어마다 달라 USB FS 주변장치는 status 완료 뒤 DADDR 를 쓰고, OTG 코어는 SETUP 직후 DCFG.DAD 를 써도 하드웨어가 status 를 옛 주소로 처리한다 (COMM-M15 Q3).

### 복잡도와 RAM

요청 하나의 펌웨어 비용 = 패킷당 O(1) (memcpy mps 바이트). RAM: EP0 버퍼 64 + rx 64 + encap 64 + txbuf 8 + 디스크립터 RAM 사본 18 + 상태 수십 바이트 ≈ 250 B. `tx` 는 디스크립터를 **가리키기만** 한다(복사 없음) — 그래서 디스크립터는 const 플래시에 두고, 동적으로 만드는 응답(GET_STATUS, LINE_CODING)만 작은 `txbuf` 에 둔다. 주의: `txbuf` 를 가리키는 데이터 단계 도중 다른 코드가 `txbuf` 를 덮으면 안 된다 — 새 SETUP 이 오기 전까지 그 버퍼는 SIE 소유다 (CAM-M11 의 버퍼 소유권과 같은 생각).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 103/103 passed, exit 0 (약 0.1 초)
./solution log 8      # bring-up 로그 (mps 8),  ./solution log 64,  ./solution faults
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -O2 demo_bugs.c -o demo_bugs && ./demo_bugs                    # §5 의 버그 표
```

실행 결과 (실제 출력):

```text
A4 ZLP matrix mps  8: 700 transfers, 0 bad
A4 ZLP matrix mps 16: 2548 transfers, 0 bad
A4 ZLP matrix mps 32: 9700 transfers, 0 bad
A4 ZLP matrix mps 64: 37828 transfers, 0 bad
103/103 passed
```

ASan/UBSan 빌드도 같은 출력이고 경고가 없다. 하네스 검출력 확인: 정답 코드에 그럴듯한 버그 8개를 하나씩 넣은 사본(이 레슨 폴더 밖)을 돌렸고 모두 FAIL 했다 — ZLP 조건 `<=`, ZLP 없음, SET_ADDRESS 즉시 적용, status OUT 늦게 무장, 새 SETUP 에서 대기 효과를 안 지움, 라인 코딩을 데이터 단계에서 적용, OUT 초과 검사를 버퍼 크기로, 조기 status 에서 IN 버퍼를 회수 안 함. "대기 효과를 안 지움" 은 처음에는 살아남았다 — 끊긴 SET_ADDRESS 뒤의 요청이 STATUS_OUT 으로 끝나 commit 경로를 안 탔기 때문. B5 에 "status IN 으로 끝나는 요청"(wLength 0 GET_DESCRIPTOR)을 추가해 잡았다.

## 7. 자주 하는 실수

1. **ZLP 를 "길이가 mps 의 배수면" 으로만 판단** — wLength 로 잘린 경우(n == wLength)에도 보내게 된다. 호스트는 이미 끝났다고 보고 status 로 가므로 남은 ZLP 가 다음 전송을 오염시킬 수 있다. → `n < wLength && n % mps == 0`.
2. **ZLP 를 모름** — 64 B 디스크립터가 생기는 날 열거 실패. 실측: mps 64 에서 조합의 0.53%. 테스트 행렬에 64·k 길이를 반드시 넣는다.
3. **status OUT 을 데이터 다 보낸 뒤에 무장** — 단위 테스트는 통과, Windows 첫 읽기와 마지막 ACK 유실에서 NAK 타임아웃. → 데이터 단계 시작 때 무장.
4. **SET_ADDRESS 를 SETUP 에서 바로 적용** — status IN 에 답하지 못한다. (USB FS 주변장치 기준. OTG 코어는 반대로 바로 써야 하는 경우가 있다 — 칩 문서.)
5. **새 SETUP 에서 상태를 안 지움** — 이전 요청의 남은 tx 포인터·rx 길이·대기 효과가 새 요청에 섞인다. 특히 대기 효과(`act`)는 다음 status IN 에서 엉뚱하게 commit 된다.
6. **wLength 를 무시하고 디스크립터 전체를 보냄** — 호스트 입장에서 babble. 장치는 절대 wLength 를 넘지 않는다.
7. **OUT 데이터 길이를 버퍼 크기로만 검사** — wLength 를 넘는 호스트 데이터를 받아들인다(M7 변이). 검사는 `rx_expect - rx_len`, 그리고 wLength &gt; 버퍼는 SETUP 에서 거절.
8. **STALL 을 "영구 오류" 로 다룸** — EP0 의 protocol STALL 은 다음 SETUP 이 푼다. 펌웨어가 STALL 플래그를 SETUP 에서 안 풀면(칩이 자동으로 안 해 주면) 장치가 다시는 열거되지 않는다.

```c
/* 실수 1·2·3 을 고친 모양 — ep0_send 의 핵심 세 줄 */
uint16_t n = avail < d->wlength ? avail : d->wlength;
d->tx_zlp = n < d->wlength && n % d->hw.mps == 0;
hw_arm_status_out(d);            /* 데이터 단계 시작과 동시에 */
```

## 8. 면접 Q&A

<details><summary>Q1. 장치가 wLength 보다 짧게 답하면 호스트는 데이터 단계의 끝을 어떻게 아나요?</summary>

짧은 패킷(mps 미만, 0 포함)을 받는 순간 끝이다. 그래서 응답이 짧은데 길이가 mps 의 배수면 마지막 패킷이 "꽉 찬" 패킷이라 호스트는 더 올 것으로 보고 IN 을 계속 보낸다 — 장치는 ZLP 를 하나 더 보내야 한다. 응답이 wLength 와 같으면 호스트가 이미 다 받았으니 ZLP 를 보내면 안 된다. 이 레슨 A4 가 mps 4종 × 50,776 조합으로 확인했다. 같은 규칙이 bulk 의 "전송" 경계에도 적용되고(CDC 데이터에서 64 B 배수 메시지가 안 끝나는 버그), 그때는 클래스·호스트 드라이버 규약을 따로 봐야 한다.
</details>

<details><summary>Q2. 데이터 토글은 왜 필요하고, ACK 가 유실되면 각 쪽은 무엇을 보나요?</summary>

ACK 는 확인 응답이 없는 마지막 패킷이라 유실될 수 있다. IN: 장치는 ACK 를 못 받았으니 같은 데이터·같은 토글을 다시 보내고, 호스트는 토글이 기대와 다르니 중복으로 보고 버린 뒤 ACK. OUT: 호스트는 ACK 를 못 봤으니 같은 토글로 재전송하고, 장치(SIE)는 토글 불일치를 보고 ACK 하고 버린다. 1비트 시퀀스 번호를 쓴 stop-and-wait 이다(COMM-M14). 꼬리: 마지막 IN 데이터 패킷의 ACK 가 유실되면 호스트는 status OUT 으로 넘어가고 장치는 DATA_IN 에서 그걸 받는다 — 조기 status 처리가 없는 펌웨어는 여기서 멈춘다(A5 가 확인, demo 의 "late status-OUT" 이 5% 링크에서 약 5% 실패).
</details>

<details><summary>Q3. SET_ADDRESS 의 status ACK 가 유실되면 누가 어떻게 회복하나요?</summary>

장치는 ACK 를 못 받았으니 주소를 안 바꾸고 status ZLP 를 계속 들고 있다. 호스트는 성공으로 알고 새 주소로 다음 요청 → 무응답 → 재시도 후 포트 리셋 → 열거를 처음부터. 장치 펌웨어가 할 일은 버스 리셋에서 모든 상태(주소, 구성, EP0)를 깨끗이 초기화하는 것뿐이다 — 이 레슨 B6 과 `./solution faults` (3). "장치가 타이머로 알아서 적용" 같은 꼼수는 다른 경우(호스트가 정말 status 를 안 보낸 경우)를 망가뜨린다. 일반화: status 의 ACK 는 2-장군 문제의 마지막 메시지라 완벽한 합의는 불가능하고, USB 는 리셋이라는 상위 회복 수단으로 닫는다.
</details>

<details><summary>Q4. 왜 효과를 status 단계 뒤에 적용하나요? 데이터를 받자마자 적용하면 안 되나요?</summary>

SET_ADDRESS 는 프로토콜이 강제한다(status IN 이 옛 주소로 온다). 나머지는 설계 선택이지만, 한 곳(commit)에 모으면 (1) 데이터 도중·status 전의 새 SETUP 이 효과를 자동 취소하고, (2) 내용 검증 실패를 status STALL 로 알리면서 아무 상태도 바뀌지 않으며, (3) 테스트가 단순해진다(B5, B10, M6 변이). 비용은 status ACK 유실 시 장치가 적용 안 하는 것 — 확률은 낮고, SET_LINE_CODING 이라면 호스트가 GET 으로 재확인할 수 있다. 실제 스택은 섞여 있다: 많은 스택이 SET_CONFIGURATION 의 엔드포인트 열기는 SETUP 단계에서 바로 한다.
</details>

<details><summary>Q5. 장치가 요청을 지원하지 않으면? STALL 은 언제 풀리나요?</summary>

EP0 에 STALL 을 건다(protocol STALL). 데이터 단계가 있으면 첫 데이터 토큰에서, 없으면 status 에서 호스트가 STALL 을 본다. 다음 SETUP 이 오면 풀린다 — SETUP 은 STALL 이든 NAK 든 항상 받아야 하는 토큰이다. bulk/interrupt 엔드포인트의 functional STALL(ENDPOINT_HALT)은 다르다: 호스트가 CLEAR_FEATURE(ENDPOINT_HALT) 를 보낼 때까지 유지되고, 해제 시 토글이 DATA0 으로 돌아간다(연습 1). 면접 꼬리: "FS 전용 장치에 DEVICE_QUALIFIER 를 요청하면?" → STALL 이 정답(Windows 류 호스트가 실제로 묻는다 — bring-up 로그의 stalls 1).
</details>

<details><summary>Q6. 호스트가 wLength 보다 많은 OUT 데이터를 보내거나, wLength 가 장치 버퍼보다 크면?</summary>

wLength &gt; 버퍼는 SETUP 을 보는 순간 알 수 있으니 바로 STALL 한다(첫 OUT 이 STALL — B11). wLength 안에서 버퍼에 맞게 설계했는데 호스트가 더 보내면, SIE 는 그 패킷을 이미 ACK 한 뒤 펌웨어가 초과를 알게 되므로 STALL 은 **다음** 트랜잭션(보통 status IN)에서 보인다. 그래서 STATUS_IN 상태에서도 OUT 을 무장해 두고 "새 OUT 이 오면 STALL" 로 처리한다. 중요한 것은 넘친 데이터를 버퍼에 쓰지 않는 것 — 검사는 `n > rx_expect - rx_len` 를 memcpy **전에**.
</details>

<details><summary>Q7. 진행 중인 control 전송 도중 새 SETUP 이 오면 장치는 무엇을 해야 하나요?</summary>

이전 전송은 끝난 것으로 보고 버린다: 남은 IN 데이터, 받다 만 OUT 데이터, 대기 중 효과(SET_ADDRESS 포함), STALL. 그리고 새 요청을 처음부터 처리한다. 호스트가 타임아웃 뒤 재시도하거나, 드라이버가 요청을 취소하면 실제로 일어난다. SIE 가 SETUP 에서 양방향 NAK 로 돌려 주는 칩이면 하드웨어 버퍼는 정리되지만, 펌웨어 변수(`tx_left`, `rx_len`, `act`)는 펌웨어가 지워야 한다 — 이 레슨 B5 · B8 · B9 · B10 과 M5 변이.
</details>

<details><summary>Q8. CDC-ACM 에서 호스트가 `stty 9600 cs7 parenb cstopb` 를 하면 버스에 무엇이 오가나요? DTR 은 왜 중요한가요?</summary>

Linux cdc-acm 이 termios 를 7바이트로 바꿔 SET_LINE_CODING(`21 20 00 00 00 00 07 00` + `80 25 00 00 02 02 07`)을 보낸다: OUT 7 B → status IN ZLP. 장치는 그 값으로 실제 UART(USB-UART 브리지라면)를 재설정한다. open 은 SET_CONTROL_LINE_STATE(DTR|RTS = 3), close·hang-up(B0)은 DTR 을 내린다. 많은 장치가 DTR 을 "호스트 터미널이 열렸다" 신호로 쓴다 — 데이터를 DTR 이 올라온 뒤에만 보내거나, Arduino 류는 1200 baud open/close 를 부트로더 진입 신호로 쓴다(관례, 보드별 확인). Don 의 RF 모듈 AT 콘솔 경험과 연결하면: 모듈 쪽 UART 설정을 바꾸는 것은 이 7바이트이고, 호스트 터미널이 모듈을 "깨우는" 것은 DTR 이다.
</details>

<details><summary>Q9. 하드웨어 없이 이 펌웨어를 어떻게 검증했고, 실제 보드에서는 무엇을 더 보나요?</summary>

SIE 를 모델링하고 호스트를 트랜잭션 단위로 흉내 내서, 길이 전수 행렬 + 결함 주입 + 랜덤 ACK 유실로 상태 머신을 확인하고, 변이 8개로 테스트 자체의 검출력을 확인했다. 실제 보드에서는: USB 프로토콜 분석기(Beagle/Ellisys) 또는 Linux 호스트의 usbmon/Wireshark 로 이 레슨 로그와 같은 SETUP·IN·OUT 열, 48 MHz 클럭 정확도(FS), EP0 버퍼 크기와 bMaxPacketSize0 일치, 인터럽트 지연(호스트는 SETUP 뒤 데이터 단계 패킷을 일정 시간 안에 기대 — USB 2.0 §9.2.6 의 응답 시간 한계, 수치는 스펙 확인), 그리고 여러 OS(Windows 첫 읽기, macOS, Linux)에서 열거. 녹화한 트랜잭션을 회귀 테스트로 재생하는 것은 COMM-A09 의 주제다.
</details>

## 9. 추가 연습

### 연습 1 — functional STALL: ENDPOINT_HALT

bulk/interrupt 엔드포인트 0x01, 0x81, 0x82 에 halt 비트와 토글을 두고 SET_FEATURE / CLEAR_FEATURE(ENDPOINT_HALT, wIndex = EP 주소), GET_STATUS(endpoint)를 구현하라. 본문의 `std_request` 를 감싸서(`std_request_base`) 확장한다.

<details><summary>답</summary>

SET_FEATURE 뒤 bulk IN 은 계속 STALL — EP0 에서 다른 요청이 오가도 남는다(protocol STALL 과의 차이). GET_STATUS(0x81) → `01 00`. CLEAR_FEATURE 뒤 `00 00` 이고 다음 bulk IN 의 토글은 DATA0 — halt 가 아니었던 0x01 에 CLEAR_FEATURE 를 보내도 토글은 DATA0 으로 돌아간다(USB 2.0 §9.4.5 로 기억, 확인 권장: 호스트가 bulk 파이프를 리셋하는 표준 방법이 이것이다). EP0 의 halt, 없는 EP(0x83), 다른 feature selector, 구성 전 요청 → STALL. 이 연습은 효과를 SETUP 에서 바로 적용했다(본문 commit 규칙으로 옮겨도 된다).
</details>

### 연습 2 — `stty` 인자 → SET_LINE_CODING

`stty_apply(&lc, "9600 cs7 parenb -parodd cstopb")` 처럼 숫자(baud), cs5..cs8, [-]parenb, [-]parodd, [-]cmspar, [-]cstopb 를 현재 라인 코딩 위에 적용하라. 패리티는 Linux cdc-acm 의 식 `PARENB ? (PARODD ? 1 : 2) + (CMSPAR ? 2 : 0) : 0`. baud 0 은 hang-up.

<details><summary>답</summary>

실제 출력: `"9600 cs7 parenb -parodd cstopb" -> 80 25 00 00 02 02 07`. 이어서 `parodd cmspar` → mark(3), `-parodd` → space(4), `-parenb -cstopb cs8 115200` → `115200 8N1`. `0` → 반환 1, rate 유지(Linux 는 B0 에서 DTR 만 내리고 rate 는 이전 값 유지 — cdc-acm.c 확인). `cs9`, `crtscts`(이 연습에서는 미지원 토큰), `96OO`, 11자리 숫자 → −1. 랜덤 stty 조합 2000개를 mps 8 장치에 SET_LINE_CODING → GET_LINE_CODING 왕복 — 전부 일치.
</details>

### 연습 3 — SERIAL_STATE 알림과 interrupt 엔드포인트 크기

CDC 알림 SERIAL_STATE(10 B: `A1 20 00 00` + wIndex(LE) + `02 00` + UART 상태 비트맵 LE16 — bit0 DCD, bit1 DSR, bit2 break, bit3 ring, bit4 framing, bit5 parity, bit6 overrun; PSTN 확인 권장)를 만들고, 디스크립터에서 EP 0x82 의 wMaxPacketSize 를 읽어 패킷으로 나눈 뒤 호스트 쪽에서 재조립하라.

<details><summary>답</summary>

실제 출력: `SERIAL_STATE 10 B over EP 0x82 (wMaxPacketSize 8) -> 2 packets (8 + 2)`. 비트맵 128가지 전부 왕복 일치, 첫 패킷만으로 해석하면 −1. 교훈: 이 레슨의 config(그리고 실제 장치 다수)처럼 알림 EP 를 8 B 로 잡으면 10 B 알림이 두 트랜잭션으로 쪼개진다 — 호스트 드라이버가 재조립을 안 하면 상태가 깨진다(Linux cdc-acm 은 알림을 버퍼에 모아 재조립하는 코드가 있는 것으로 기억, 버전별 확인). 장치 설계에서는 알림 EP 를 16 B 이상으로 잡는 것이 무난하다.
</details>

### 연습 4 — control 전송 한 번의 트랜잭션 수

`ctrl_transactions(avail, wLength, mps)` = SETUP 1 + 데이터 패킷 + STATUS 1 을 닫힌 식으로 쓰고 시뮬레이터와 대조한 뒤, 본문 bring-up 의 전체 트랜잭션 수를 mps 8 과 64 로 비교하라.

<details><summary>답</summary>

데이터 패킷은 §5 의 식(a = min(avail, wLength); 0 / ⌊a/mps⌋+1 / ⌈a/mps⌉). mps 8/16/32/64 × avail 0..2·mps × wLength 0..2·mps+1 전부 시뮬레이터와 일치. 실제 출력: `bring-up control transactions (excl. STALLed qualifier) mps 8 = 64, mps 64 = 43` — 손 계산: 첫 읽기 3 + 나머지 14 요청 × 2 = 28 + 데이터 패킷(mps 64: 12, mps 8: 33). FS 에서 control 은 프레임당 몇 개 안 되는 트랜잭션만 받으므로(대역폭 예약 10% — M15 표) mps 8 장치는 열거가 눈에 띄게 느리다. LS 장치는 8 이 강제(M15)라 이 비용을 피할 수 없다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 35/35 passed
```

```text
practice 2: "9600 cs7 parenb -parodd cstopb" -> 80 25 00 00 02 02 07
practice 3: SERIAL_STATE 10 B over EP 0x82 (wMaxPacketSize 8) -> 2 packets (8 + 2)
practice 4: bring-up control transactions (excl. STALLed qualifier) mps 8 = 64, mps 64 = 43
35/35 passed
```

## 10. 핵심 정리

- control 전송 = SETUP(DATA0, 항상 ACK) → DATA(DATA1 부터 번갈아) → STATUS(반대 방향 ZLP, DATA1; 데이터 없으면 IN).
- 장치 EP0 = SIE 이벤트 3개(SETUP, IN 완료, OUT 수신)에 반응하는 상태 머신. 콜백은 다음 토큰을 위해 버퍼를 무장하고 끝난다.
- 보낼 길이 = min(실제, wLength). ZLP ⇔ 길이 &lt; wLength && 길이 % mps == 0 (0 포함). 패킷 수 0 / ⌊a/mps⌋+1 / ⌈a/mps⌉.
- IN 데이터 단계 시작과 동시에 status OUT 무장 — 호스트 조기 종료와 마지막 ACK 유실을 같은 경로로 처리.
- 효과는 status 뒤 한 곳에서 commit (SET_ADDRESS 는 필수, 나머지는 설계). 새 SETUP 은 전송 상태와 대기 효과를 버린다.
- protocol STALL 은 다음 SETUP 이 풀고, functional STALL(ENDPOINT_HALT)은 CLEAR_FEATURE 가 풀며 토글을 DATA0 으로.
- OUT 초과는 ACK 뒤에 알게 되므로 다음 트랜잭션에서 STALL. wLength &gt; 버퍼는 SETUP 에서 거절.
- 실측: ZLP 누락은 mps 64 조합의 0.53%, status OUT 늦게 무장은 깨끗한 링크 0% / 5% ACK 유실 약 5% — 잠복 버그는 장치 상태 단언과 결함 주입으로만 잡힌다.

## 참고자료

- USB-IF, *Universal Serial Bus Specification Revision 2.0* — §5.5 control 전송, §8.5.3 control 전송 단계·짧은 패킷·status, §8.6 데이터 토글, §9.4 표준 요청(§9.4.5 CLEAR_FEATURE 와 토글, §9.4.6 SET_ADDRESS), §9.2.6 응답 시간: <https://www.usb.org/document-library/usb-20-specification> (절 번호와 세부는 기억에 의존 — 원문 확인 권장)
- USB-IF, *Class Definitions for Communications Devices 1.2* + *PSTN Subclass 1.2* (ACM 요청, 라인 코딩 Table 17, SERIAL_STATE, 기능 디스크립터): <https://www.usb.org/document-library/class-definitions-communication-devices-12>
- Linux `drivers/usb/class/cdc-acm.c` — `acm_tty_set_termios()`(패리티·정지 비트·B0 처리), `acm_port_dtr_rts()`, `acm_set_control()` (이 레슨 작성 중 직접 확인): <https://github.com/torvalds/linux/blob/master/drivers/usb/class/cdc-acm.c>
- TinyUSB — `src/device/usbd.c`, `usbd_control.c`, `src/class/cdc/cdc_device.c` (control 단계 콜백, ZLP 처리): <https://github.com/hathach/tinyusb>
- STMicroelectronics, STM32 USB Device Library / HAL PCD (`USBD_CtlSendData`, `USBD_CtlPrepareRx`, `HAL_PCD_SetAddress`), 각 칩 RM 의 USB FS / OTG 장 — SETUP 수신 시 STAT_TX/RX 동작, DADDR/DCFG 쓰는 시점 확인 권장.
- Craig Peacock, *USB in a NutShell* ch.4–6 (control 전송, 열거): <https://www.beyondlogic.org/usbnutshell/usb4.shtml>
- 이 Mac: `ioreg -p IOUSB -l -w0`, `ioreg -l -w0 -r -c IOUSBHostInterface` (Studio Display CDC-NCM 인터페이스 확인; CDC-ACM 장치는 미연결).
- 관련 레슨: COMM-M15 (디스크립터 파싱·호스트 쪽 열거·SET_ADDRESS 조기 적용 버그), COMM-M14 (stop-and-wait ARQ = 데이터 토글), COMM-M13 (비동기 전송 큐·콜백 정확히 한 번), COMM-M12 (드라이버 계층과 오류 코드), COMM-M11 (DMA 수신과 IDLE — CDC bulk OUT 을 UART 로 넘기는 다음 단계), COMM-B14 (AT 명령 줄 편집기 — CDC 위의 콘솔), COMM-A09 (트랜잭션 기록/재생 회귀 테스트), CAM-M11 (버퍼 소유권), BM-M08 (DMA 드라이버, 다음 단계)
