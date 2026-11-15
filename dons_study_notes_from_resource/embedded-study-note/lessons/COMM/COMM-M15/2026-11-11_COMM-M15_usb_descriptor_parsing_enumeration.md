---
id: COMM-M15
time: 90분
updated: 2026-11-11
---
# COMM-M15 USB 기초: 디스크립터 파싱과 enumeration 흐름

## 학습 목표

- device(18 B) · configuration(9 B + wTotalLength) · interface(9) · endpoint(7) · IAD(8) · string 디스크립터의 필드를 오프셋 단위로 읽고, 모르는 클래스 디스크립터(HID 0x21, CS_INTERFACE 0x24, CS_ENDPOINT 0x25)는 **bLength 로 건너뛰는** 파서를 malloc 없이 짠다.
- 엔드포인트 한 개를 보고 방향·전송 타입·iso sync/usage·`wMaxPacketSize`(bits 10:0 + 추가 트랜잭션 bits 12:11)·`bInterval` 의 실제 주기(속도별 공식이 다름)를 말한다.
- 악의적이거나 깨진 디스크립터(bLength 0, 남은 바이트보다 긴 bLength, 거짓 wTotalLength, 개수 불일치, 홀수 길이 문자열)를 **각각 다른 오류**로 거르고, 300만 건 변이 퍼징으로 "순진한 파서"가 얼마나 자주 멈추거나 범위 밖을 읽는지 숫자로 보인다.
- enumeration 을 SETUP 8바이트 단위로 재현한다: reset → GET_DESCRIPTOR(device, 64) → reset → SET_ADDRESS → device 18 → config 9 → config 전체 → 문자열 → SET_CONFIGURATION. wLength 잘림, EP0 패킷 분할과 ZLP, "SET_ADDRESS 를 너무 일찍 적용하는" 펌웨어 버그까지.
- control / bulk / interrupt / isochronous 를 용도·보장·재시도·최대 패킷 크기로 비교한다.

## 1. 문제

USB 장치 펌웨어(STM32 USB FS / OTG, nRF52840 USBD, TinyUSB)를 처음 올리면 가장 먼저 부딪히는 곳이 **enumeration** 이고, 면접관이 "장치가 '알 수 없는 장치'로 뜹니다. 어디부터 보나요?" 를 묻는 이유도 같다 — 원인의 대부분이 디스크립터 바이트(특히 wTotalLength, bNumInterfaces, bLength)와 EP0 처리(SET_ADDRESS 타이밍, ZLP)에 있다.

USB 는 지금까지의 UART/SPI/I2C(COMM-B01~B07)와 달리 **호스트가 모든 것을 묻는** 버스다. 장치는 꽂히자마자 자기소개서(디스크립터)를 호스트가 요청하는 대로 조각조각 보내고, 호스트는 그걸 읽어 주소를 주고, 설정(configuration)을 고르고, 드라이버를 붙인다. 이 레슨은 그 자기소개서를 읽는 쪽(호스트 파서)과 보내는 쪽(mock 장치)을 둘 다 짠다.

**진짜 장치 하나 — 이 Mac 에서 직접 확인.** `system_profiler SPUSBDataType` 은 이 Mac(macOS 27.2)에서 아무것도 출력하지 않았고, `system_profiler SPUSBHostDataType` 과 `ioreg` 가 동작했다. Studio Display 의 USB 허브 아래 Logitech 마우스 (실제 출력, 발췌):

```text
$ ioreg -p IOUSB -l -w0 -r -n "USB Optical Mouse@20114000"
      "USBSpeed" = 2                       "UsbLinkSpeed" = 1500000     (Low-Speed 1.5 Mb/s)
      "idVendor" = 1133      (0x046D)      "idProduct" = 49271  (0xC077)
      "bcdUSB" = 512         (0x0200)      "bcdDevice" = 29184  (0x7200)
      "bDeviceClass" = 0     "bDeviceSubClass" = 0     "bDeviceProtocol" = 0
      "bMaxPacketSize0" = 8  "iManufacturer" = 1  "iProduct" = 2  "iSerialNumber" = 0
      "bNumConfigurations" = 1   "kUSBCurrentConfiguration" = 1   "USB Address" = 8
      "UsbDeviceSignature" = <6d0477c0000000030102>
$ ioreg -l -w0 -r -c IOUSBHostInterface   (같은 마우스의 interface 0)
      "bInterfaceNumber" = 0  "bAlternateSetting" = 0  "bNumEndpoints" = 1
      "bInterfaceClass" = 3   "bInterfaceSubClass" = 1  "bInterfaceProtocol" = 2   "bConfigurationValue" = 1
  자식 AppleUserUSBHostHIDDevice:
      "ReportDescriptor" = <05010902a1010901a1000509...81 06 c0 c0>  (46 바이트)
      "MaxInputReportSize" = 4    "ReportInterval" = 8000
```

ioreg 는 디스크립터를 **필드 값으로 풀어서** 보여 줄 뿐 원본 바이트는 주지 않는다(리눅스 `lsusb -v` 나 `/sys/bus/usb/devices/*/descriptors` 는 원본을 준다). 그래도 device 디스크립터의 14개 필드가 전부 있으므로 18바이트를 정확히 재구성할 수 있다 — `MOUSE_DEV[]` 가 그것이다. `UsbDeviceSignature` 는 VID(LE) PID(LE) + 장치 class/sub/proto(00 00 00) + 인터페이스 class/sub/proto(03 01 02)로 읽힌다(필드 이름에서 한 추정 — Apple 문서로 확인 안 함). `ReportInterval = 8000` µs 는 bInterval 이 8 ms 로 반영된 흔적으로 보이는데, LS 마우스 bInterval 이 10 이어도 xHCI 가 주기를 2의 거듭제곱으로 내림하면 8 ms 가 된다 (추정, xHCI 스펙 확인 권장). bInterval 원본은 ioreg 에 없으므로 이 레슨의 마우스 엔드포인트(4 B, 10 ms)는 **constructed** 다.

이 레슨의 예시 덤프 3개 (전부 이 레슨이 만든 spec-consistent 배열, 마우스는 위 실측 필드 반영):

| 예시 | 속도 | 구성 | 볼 것 |
|---|---|---|---|
| `CDC_*` CDC-ACM 시리얼 | FS | IAD + 통신 IF(CS 디스크립터 4개 + interrupt IN) + 데이터 IF(bulk OUT/IN) = 75 B | IAD, 클래스 디스크립터 건너뛰기 |
| `MOUSE_*` HID 마우스 | LS | IF(HID/boot/mouse) + HID 디스크립터 + interrupt IN = 34 B | 실측 device 18 B, 0x21 |
| `UAC_*` UAC2 비슷한 오디오 | HS | IAD + AC IF + AS IF alt0(EP 0개)/alt1(iso OUT async + iso IN feedback) = 97 B | alternate setting, iso, 0x25 |

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">CDC_CFG (75 B) — GET_DESCRIPTOR(CONFIGURATION) 한 번에 오는 "평평한" 바이트열과 그 안의 트리</text>
  <g stroke="currentColor">
    <rect x="10" y="34" width="54" height="26" class="f-acc"/>
    <rect x="64" y="34" width="48" height="26" class="f-warn"/>
    <rect x="112" y="34" width="54" height="26" class="f-ok"/>
    <rect x="166" y="34" width="30" height="26" class="f-mut"/>
    <rect x="196" y="34" width="30" height="26" class="f-mut"/>
    <rect x="226" y="34" width="24" height="26" class="f-mut"/>
    <rect x="250" y="34" width="30" height="26" class="f-mut"/>
    <rect x="280" y="34" width="42" height="26" class="f-ok"/>
    <rect x="322" y="34" width="54" height="26" class="f-ok"/>
    <rect x="376" y="34" width="42" height="26" class="f-ok"/>
    <rect x="418" y="34" width="42" height="26" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="16" y="51">CFG 9</text><text x="68" y="51">IAD 8</text><text x="116" y="51">IF0 9</text>
    <text x="169" y="51">CS</text><text x="199" y="51">CS</text><text x="229" y="51">CS</text><text x="253" y="51">CS</text>
    <text x="284" y="51">EP82</text><text x="326" y="51">IF1 9</text><text x="380" y="51">EP01</text><text x="422" y="51">EP81</text>
    <text x="10" y="76">off 0</text><text x="64" y="76">9</text><text x="112" y="76">17</text><text x="166" y="76">26</text>
    <text x="280" y="76">45</text><text x="322" y="76">52</text><text x="376" y="76">61</text><text x="418" y="76">68..74</text>
    <text x="470" y="51">← 다음 위치 = 현재 + bLength</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="112">09 02 4B 00 02 01 00 80 32</text>
    <text x="250" y="112">CONFIG wTotalLength=75 bNumInterfaces=2 value=1</text>
    <text x="40" y="138">08 0B 00 02 02 02 01 00</text>
    <text x="250" y="138">IAD first=0 count=2 class CDC/ACM/AT</text>
    <text x="40" y="164">09 04 00 00 01 02 02 01 00</text>
    <text x="250" y="164">INTERFACE 0 alt 0, 1 EP, CDC comm / ACM</text>
    <text x="60" y="190">05 24 00 10 01 …(×4)</text>
    <text x="250" y="190">CS_INTERFACE Header/CallMgmt/ACM/Union — 건너뜀</text>
    <text x="60" y="216">07 05 82 03 08 00 10</text>
    <text x="250" y="216">ENDPOINT 0x82 IN interrupt 8 B, 16 ms</text>
    <text x="40" y="242">09 04 01 00 02 0A 00 00 00</text>
    <text x="250" y="242">INTERFACE 1 alt 0, 2 EP, CDC-Data</text>
    <text x="60" y="268">07 05 01 02 40 00 00</text>
    <text x="250" y="268">ENDPOINT 0x01 OUT bulk 64 B</text>
    <text x="60" y="294">07 05 81 02 40 00 00</text>
    <text x="250" y="294">ENDPOINT 0x81 IN  bulk 64 B</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M28,118 V134 H38"/><path d="M28,134 V160 H38"/><path d="M48,170 V186 H58"/><path d="M48,186 V212 H58"/>
    <path d="M28,160 V238 H38"/><path d="M48,248 V264 H58"/><path d="M48,264 V290 H58"/>
  </g>
  <text x="10" y="330" fill="currentColor">트리는 포인터가 아니라 순서로 표현된다: endpoint 는 바로 앞 interface 소속,</text>
  <text x="10" y="350" fill="currentColor">IAD 는 [first, first+count) 를 한 기능으로 묶음. bNumInterfaces = alt 를 뺀 번호 개수.</text>
  <text x="10" y="376" fill="currentColor" font-size="12">(바이트는 solution.c 의 CDC_CFG 그대로, ./solution dump 의 해석과 일치)</text>
</svg>
```

### Part A — 디스크립터 파서와 필드 해석

1. `usb_parse_device(d, n, &dev)`: 18 B, LE16 필드, `bMaxPacketSize0 ∈ {8,16,32,64}` (bcdUSB ≥ 3.0 이면 9 = 2⁹ = 512 도 허용).
2. `ep_interval_us(type, speed, bInterval)`: interrupt LS/FS = bInterval ms, HS = 2^(b−1)×125 µs; iso FS = 2^(b−1) ms, HS = 2^(b−1)×125 µs; 범위 밖 / LS 의 iso·bulk = −1; control/bulk = 0. 필드 헬퍼 `ep_type/ep_sync/ep_usage/ep_mps/ep_mult` 는 주어진다.
3. `usb_parse_config(buf, n, &cfg)`: wTotalLength 까지 걷기. INTERFACE / ENDPOINT / IAD 는 표에 기록, 그 밖의 타입은 `ext[]` 에 (타입, 길이, 서브타입, 소속 interface, 오프셋)만 남기고 건너뜀. 고정 크기 표(IF 16, EP 32, IAD 8, 기타 32).
4. `usb_string_to_utf8(d, n, out, cap)`: UTF-16LE → UTF-8 (서로게이트 쌍 결합). 예시 3개 + "한글 🔌" 문자열.
5. 주어진 `usb_dump_*` 가 위 결과로 `lsusb -v` 비슷한 덤프를 호출자 버퍼에 쓴다 (`./solution dump`).

### Part B — 깨진 디스크립터를 각각 다른 오류로

`desc_next()`(경계 검사 반복자)와 `usb_parse_config` 의 검사들. 주입하고 **정확한 코드**를 단언한다:

| 주입 | 기대 |
|---|---|
| 중간 디스크립터 bLength 0 / 1 | `BLEN_ZERO` (0 이면 순진한 루프는 영원히 제자리) |
| 마지막 EP bLength 0x20 (> 남은 7) | `BLEN_OVERRUN` |
| 버퍼 40 B 만 / wTotalLength 76 | `TOTAL_GT_BUF` · wTotalLength 5 → `TOTAL_SMALL` · n < 9 → `SHORT` |
| 첫 타입이 DEVICE / 중간에 CONFIG 타입 | `TYPE` |
| bNumInterfaces 3 (실제 2) | `NUM_IF` |
| bNumEndpoints 2 vs 1, 3 vs 2, 0 vs 1 | `NUM_EP` (다음 interface 에서 또는 끝에서 검출) |
| interface 없이 endpoint / EP 번호 0 / 예약 비트 | `EP_ORPHAN` / `EP_ADDR` |
| IAD 가 IF 0..2 를 가리킴 / count 0 | `IAD_RANGE` |
| interface bLength 5, config bLength 7 | `BLEN_SMALL` |
| EP 33개 (표 32) | `CAP` |
| 문자열 bLength 7 / 짝 없는 서로게이트 / 출력 용량 부족 | `STR_ODD` / `STR_UTF16` / `CAP` |
| bMaxPacketSize0 = 12 | `MPS0` |

허용해야 하는 것도 테스트한다: UAC1 처럼 **9바이트 endpoint**(bRefresh, bSynchAddress 추가) → 앞 7바이트만 해석, wTotalLength 뒤 꼬리 바이트 → 무시. 마지막으로 고정 시드 변이 퍼징 18,000건을 **정확한 크기의 힙 사본**에서 돌려 ASan 이 1바이트 초과 읽기도 잡게 한다. 결과는 정의된 코드 중 하나여야 하고, OK 면 불변식(EP 합계, 개수, ext 범위)이 성립해야 한다.

### Part C — SETUP 디코더, mock 장치, enumeration 로그

1. `setup_decode(s[8], out, cap)`: bmRequestType(방향 bit7, 타입 6:5, 수신자 4:0) / bRequest / wValue / wIndex / wLength(LE16) → 한 줄.
2. `data_stage_packets(avail, wLength, mps0, sizes, cap)`: 실제 길이 = min(avail, wLength) 를 mps0 로 자르고, 실제 < wLength 인데 마지막이 꽉 찼으면 **ZLP**.
3. `mock_setup()`: 디스크립터 배열로 GET_DESCRIPTOR(device/config/string) 응답(wLength 로 잘라서), SET_ADDRESS 는 **status 단계 뒤** 적용, SET_CONFIGURATION.
4. `host_enumerate()`: 위 순서로 SETUP 을 보내고 로그를 남긴다. 결함 주입: 없는 문자열 index → STALL 이어도 계속, wTotalLength 를 부풀린 장치 → `TOTAL_GT_BUF` 로 실패, SET_ADDRESS 를 즉시 적용하는 장치 → status 단계 무응답, LANGID 없는 장치 → 문자열 없이 Configured.

## 2. Problem (English)

> You are writing the host-side descriptor parser and enumeration sequencer for a USB stack test bench (no malloc; fixed tables).
>
> ```c
> int  usb_parse_device(const uint8_t *d, size_t n, usb_dev *out);           /* 18-byte device descriptor */
> int  usb_parse_config(const uint8_t *buf, size_t n, usb_cfg *c);            /* full configuration bundle */
> long ep_interval_us(int type, usb_speed sp, uint8_t bInterval);             /* service period, 0 = none, -1 = invalid */
> int  usb_string_to_utf8(const uint8_t *d, size_t n, char *out, size_t cap); /* UTF-16LE -> UTF-8, returns bytes */
> int  setup_decode(const uint8_t s[8], char *out, size_t cap);
> int  data_stage_packets(size_t avail, uint16_t wLength, uint8_t mps0, uint16_t *sizes, int cap);
> int  host_enumerate(host_ctx *h, uint8_t new_addr);
> ```
>
> Requirements: walk descriptors strictly by `bLength`, never read past `n` or `wTotalLength`, skip unknown/class-specific descriptors, and report a distinct negative error for: bLength < 2, bLength past the end, bLength below the standard size, wTotalLength < 9 or > received bytes, wrong descriptor type, bNumInterfaces / bNumEndpoints mismatch (alternate settings share an interface number), endpoint before any interface, invalid endpoint address, IAD pointing at missing interfaces, odd-length or ill-formed UTF-16 strings, and table/output overflow.
>
> Example: `80 06 00 01 00 00 40 00` decodes to `IN STD DEV | GET_DESCRIPTOR(DEVICE, 0) wIndex=0x0000 wLength=64`. A device with bMaxPacketSize0 = 8 answering GET_DESCRIPTOR(CONFIGURATION) with wLength = 34 sends `8+8+8+8+2`; a 16-byte string requested with wLength 255 is sent as `8+8+ZLP`.
>
> Follow-ups: (1) Why is the first device-descriptor read only 8/64 bytes and followed by a reset? (2) What breaks if the device applies SET_ADDRESS immediately? (3) Count the bandwidth an isochronous endpoint with wMaxPacketSize 0x1400 reserves at high speed. (4) How would you fuzz this parser?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

TODO 는 9개: A1 `usb_parse_device`, A2 `ep_interval_us`, B1 `desc_next`, A3+B2 `usb_parse_config`, A4 `usb_string_to_utf8`, C1 `setup_decode`, C2 `data_stage_packets`, C3 `mock_setup`, C4 `host_enumerate`. 덤프 출력기, `host_ctrl`(control 전송 한 번: SETUP → DATA → STATUS 와 로그), `mock_status`, 예시 배열, 하네스는 주어진다. 스텁은 "SHORT / 0 / STALL / NORESP" 를 돌려주므로 모든 루프가 끝난다. 처음 실행 (실제 출력, 중간 생략):

```text
FAIL starter.c:527  usb_parse_device(MOUSE_DEV, 18, &d) == USB_OK
FAIL starter.c:530  usb_parse_device(CDC_DEV, 18, &d) == USB_OK && d.cls == 0xEF && d.sub == 0x02 && d.proto == 0x01 && d.mps0 == 64
…
B fuzz: 18000 mutants  OK=0  SHORT=18000 BLEN_ZERO=0 OVERRUN=0 SMALL=0 TYPE=0 TOTAL_SMALL=0 GT_BUF=0 NUM_IF=0 NUM_EP=0 ORPHAN=0 EP_ADDR=0 IAD=0  (1 error kinds)
…
FAIL starter.c:816  host_enumerate(&g_h, 5) == USB_OK && g_h.langid == 0 && g_h.str_missing == 2 && g_m.cfg_value == 1
12/109 passed
```

(exit 1, 0.1 초 안에 끝남.) 통과하는 12개는 스텁의 "거부" 반환값이 우연히 맞는 경우(8바이트 버퍼 → `SHORT`, 작은 출력 버퍼 → `CAP` 등)와 "아무것도 받아들이지 않으면 저절로 성립하는" 안전성 검사(퍼징 불변식, 덤프 오버플로)다.

## 4. 힌트

<details><summary>힌트 1 — 파서의 유일한 진실은 bLength</summary>

`off += buf[off]` 하나로 다음 디스크립터에 간다. 그러니 그 덧셈 전에 세 가지만 확인하면 범위 밖으로 못 나간다: 남은 바이트 ≥ 2, bLength ≥ 2, bLength ≤ 남은 바이트. "남은" 의 기준은 n 이 아니라 **min(n, wTotalLength)** 이고, wTotalLength > n 은 그 자체로 오류다(장치가 거짓말했거나 덜 받았다).
</details>

<details><summary>힌트 2 — bNumEndpoints 는 언제 검사하나</summary>

interface 디스크립터에서 "앞으로 EP 가 k개 온다"고 선언한다. 확인은 **다음 interface 가 나올 때**와 **끝에 도달했을 때** 한다. 그 사이에 클래스 디스크립터(CS_INTERFACE, CS_ENDPOINT)가 섞여 있어도 세지 않는다.
</details>

<details><summary>힌트 3 — bNumInterfaces 는 interface 디스크립터 수가 아니다</summary>

UAC 예시는 interface 디스크립터가 3개(IF0, IF1 alt0, IF1 alt1)지만 bNumInterfaces = 2. 서로 다른 bInterfaceNumber 를 세라 — 256비트 비트맵(`uint32_t present[8]`)이면 malloc 없이 끝난다. IAD 범위 검사도 같은 비트맵으로.
</details>

<details><summary>힌트 4 — bInterval 은 속도마다 단위가 다르다</summary>

FS/LS interrupt 만 "그냥 ms". HS 와 iso 는 지수: 2^(bInterval−1) × (125 µs 또는 1 ms). `125L << (b − 1)` 로 계산하고 b 가 1..16 밖이면 −1.
</details>

<details><summary>힌트 5 — enumeration 은 "모르는 것을 하나씩 알아 가는" 순서</summary>

호스트는 처음에 EP0 최대 패킷 크기를 모른다 → 첫 패킷 하나만 읽어 offset 7 을 얻는다. 주소가 없다 → 주소 0 으로 SET_ADDRESS. config 길이를 모른다 → 9바이트만 읽어 wTotalLength 를 얻고 다시 전체를 읽는다. 문자열 언어를 모른다 → index 0 에서 LANGID 를 읽는다.
</details>

## 5. 풀이

### 면접에서 말하는 순서

1. "디스크립터는 TLV 다 — bLength, bDescriptorType, 내용. 파서는 bLength 로만 이동하고, 모르는 타입은 건너뛴다(그래서 HID·CDC·UAC 클래스가 표준 파서를 깨지 않고 확장된다)."
2. "configuration 은 평평한 바이트열이고 트리는 순서로 표현된다. endpoint 는 직전 interface 소속, IAD 는 연속 interface 묶음."
3. "장치가 주는 바이트는 신뢰하지 않는다: 세 가지 경계 검사 + 개수 교차 검증 + 고정 크기 표 + 퍼징."
4. "enumeration 은 모르는 값을 하나씩 알아내는 순서: mps0 → 주소 → 디스크립터 → config 길이 → 전체 → 문자열 → SET_CONFIGURATION."

### Part A — 필드 표

| 디스크립터 | 크기 | 오프셋: 필드 |
|---|---|---|
| DEVICE (1) | 18 | 0 bLength, 1 type, 2 bcdUSB(2), 4 class, 5 sub, 6 proto, 7 **bMaxPacketSize0**, 8 idVendor(2), 10 idProduct(2), 12 bcdDevice(2), 14 iManufacturer, 15 iProduct, 16 iSerialNumber, 17 bNumConfigurations |
| CONFIGURATION (2) | 9 | 2 **wTotalLength**(2), 4 bNumInterfaces, 5 bConfigurationValue, 6 iConfiguration, 7 bmAttributes(bit7=1, bit6 self-powered, bit5 remote wakeup), 8 bMaxPower(2 mA 단위, USB 2.0) |
| INTERFACE (4) | 9 | 2 bInterfaceNumber, 3 bAlternateSetting, 4 bNumEndpoints(EP0 제외), 5 class, 6 sub, 7 proto, 8 iInterface |
| ENDPOINT (5) | 7 | 2 bEndpointAddress, 3 bmAttributes, 4 wMaxPacketSize(2), 6 bInterval |
| IAD (0x0B) | 8 | 2 bFirstInterface, 3 bInterfaceCount, 4 function class, 5 sub, 6 proto, 7 iFunction |
| STRING (3) | 2+2k | index 0: LANGID 배열(0x0409 = English US), 그 외: UTF-16LE 텍스트(NUL 없음) |

모든 다바이트 필드는 **little-endian** — SETUP 패킷도 같다. bcd 필드는 "0x0200 = 2.00", 그래서 마우스의 bcdDevice 0x7200 은 덤프에서 `72.00` 이다.

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">endpoint 디스크립터의 비트 필드 (예: UAC 의 EP 0x01 / 0x81, high-bandwidth 0x1400)</text>
  <text x="10" y="51" fill="currentColor">bEndpointAddress</text>
  <g stroke="currentColor">
    <rect x="150" y="34" width="60" height="26" class="f-acc"/><rect x="210" y="34" width="180" height="26" class="f-mut"/><rect x="390" y="34" width="240" height="26" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="156" y="51">7: 방향</text><text x="250" y="51">6:4 예약(0)</text><text x="430" y="51">3:0 엔드포인트 번호 1..15</text>
    <text x="20" y="78">1 = IN (장치→호스트), 0 = OUT · 0x81 = EP1 IN, 0x01 = EP1 OUT (같은 번호, 다른 엔드포인트)</text>
  </g>
  <text x="10" y="117" fill="currentColor">bmAttributes</text>
  <g stroke="currentColor">
    <rect x="150" y="100" width="120" height="26" class="f-mut"/><rect x="270" y="100" width="120" height="26" class="f-warn"/>
    <rect x="390" y="100" width="120" height="26" class="f-warn"/><rect x="510" y="100" width="120" height="26" class="f-acc"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="170" y="117">7:6 예약</text><text x="280" y="117">5:4 usage (iso)</text><text x="400" y="117">3:2 sync (iso)</text><text x="520" y="117">1:0 전송 타입</text>
    <text x="20" y="144">타입 00 control, 01 iso, 10 bulk, 11 interrupt · sync 00 none, 01 async, 10 adaptive, 11 sync</text>
    <text x="20" y="162">usage 00 data, 01 feedback, 10 implicit-fb data · 0x05 = iso+async+data, 0x11 = iso+feedback</text>
  </g>
  <text x="10" y="201" fill="currentColor">wMaxPacketSize</text>
  <g stroke="currentColor">
    <rect x="150" y="184" width="90" height="26" class="f-mut"/><rect x="240" y="184" width="130" height="26" class="f-warn"/><rect x="370" y="184" width="260" height="26" class="f-ok"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="160" y="201">15:13 예약</text><text x="246" y="201">12:11 추가 트랜잭션</text><text x="390" y="201">10:0 패킷 최대 바이트 (0..2047)</text>
    <text x="20" y="228">0x1400 → bits12:11 = 10 → 마이크로프레임당 3 트랜잭션, bits10:0 = 1024</text>
    <text x="20" y="246">→ HS iso/interrupt: 1024 × 3 = 3072 B / 125 µs (연습 2: 24,576,000 B/s). 11 은 예약값.</text>
  </g>
  <text x="10" y="290" fill="currentColor">bInterval</text>
  <g fill="currentColor" font-size="12">
    <text x="20" y="312">FS/LS interrupt: bInterval ms (1..255)</text>
    <text x="20" y="330">HS interrupt, FS/HS iso: 2^(bInterval−1) × (HS 125 µs | FS 1 ms), bInterval 1..16</text>
    <text x="20" y="348">bulk/control: 주기 없음 (HS bulk/control OUT 의 bInterval = 최대 NAK 비율)</text>
    <text x="20" y="366">LS 에는 bulk·iso 가 없다 → 이 레슨 모델은 −1</text>
  </g>
</svg>
```

### Part A — 덤프 (실제 `./solution dump` 출력)

```text
==== CDC-ACM + IAD (constructed) (FS 12M) ====
Device: bcdUSB 2.00  class 0xEF (Misc(IAD)) sub 0x02 proto 0x01  bMaxPacketSize0 64
        idVendor 0x1209 idProduct 0x0001 bcdDevice 1.00  iMfr 1 iProd 2 iSerial 3  bNumConfigurations 1
Configuration 1: wTotalLength 75  bNumInterfaces 2  bmAttributes 0x80 (bus-powered)  MaxPower 100 mA
  IAD: interfaces 0..1  function class 0x02 (CDC) sub 0x02 proto 0x01
  Interface 0 alt 0: 1 EP  class 0x02 (CDC) sub 0x02 proto 0x01  iInterface 0
    [CS_INTERFACE 0x24 len 5 subtype 0x00] skipped by bLength
    [CS_INTERFACE 0x24 len 5 subtype 0x01] skipped by bLength
    [CS_INTERFACE 0x24 len 4 subtype 0x02] skipped by bLength
    [CS_INTERFACE 0x24 len 5 subtype 0x06] skipped by bLength
    EP 0x82 IN  Interrupt   wMaxPacketSize 0x0008 = 8 B x1  bInterval 16 -> every 16000 us
  Interface 1 alt 0: 2 EP  class 0x0A (CDC-Data) sub 0x00 proto 0x00  iInterface 0
    EP 0x01 OUT Bulk        wMaxPacketSize 0x0040 = 64 B x1  bInterval 0 (no period)
    EP 0x81 IN  Bulk        wMaxPacketSize 0x0040 = 64 B x1  bInterval 0 (no period)
==== HID mouse (device desc from this Mac's ioreg) (LS 1.5M) ====
Device: bcdUSB 2.00  class 0x00 (per-interface) sub 0x00 proto 0x00  bMaxPacketSize0 8
        idVendor 0x046D idProduct 0xC077 bcdDevice 72.00  iMfr 1 iProd 2 iSerial 0  bNumConfigurations 1
Configuration 1: wTotalLength 34  bNumInterfaces 1  bmAttributes 0xA0 (bus-powered, remote-wakeup)  MaxPower 100 mA
  Interface 0 alt 0: 1 EP  class 0x03 (HID) sub 0x01 proto 0x02  iInterface 0
    [HID 0x21 len 9: bcdHID 1.11, class desc 0x22 len 46] skipped by bLength
    EP 0x81 IN  Interrupt   wMaxPacketSize 0x0004 = 4 B x1  bInterval 10 -> every 10000 us
==== UAC2-like iso (constructed) (HS 480M) ====
  … IAD: interfaces 0..1  function class 0x01 (Audio) sub 0x00 proto 0x20
  Interface 1 alt 0: 0 EP  class 0x01 (Audio) sub 0x02 proto 0x20  iInterface 0
  Interface 1 alt 1: 2 EP  class 0x01 (Audio) sub 0x02 proto 0x20  iInterface 0
    [CS_INTERFACE 0x24 len 16 subtype 0x01] skipped by bLength
    [CS_INTERFACE 0x24 len 6 subtype 0x02] skipped by bLength
    EP 0x01 OUT Isochronous wMaxPacketSize 0x0038 = 56 B x1 [async, data]  bInterval 1 -> every 125 us
    [CS_ENDPOINT 0x25 len 8 subtype 0x01] skipped by bLength
    EP 0x81 IN  Isochronous wMaxPacketSize 0x0004 = 4 B x1 [no-sync, feedback]  bInterval 4 -> every 1000 us
```

(UAC 부분은 앞 4줄 생략.) 장치 class 0xEF/0x02/0x01 은 "Miscellaneous / Common Class / IAD" — "인터페이스 묶음은 IAD 를 보라"는 표시다. CDC-ACM 은 인터페이스가 2개(통신 + 데이터)인 **한 기능**이라, 다른 기능과 같이 들어간 composite 장치에서 OS 가 둘을 한 드라이버에 묶으려면 IAD 가 필요하다. UAC 의 56 B = 48 kHz × 2 ch × 4 B ÷ 8000 마이크로프레임 = 48 B + 한 샘플 프레임 여유(8 B), 피드백 4 B 는 HS 의 16.16 형식 (UAC2 스펙 확인 권장).

### Part B — 경계 검사 반복자

```c
static int desc_next(const uint8_t *buf, size_t total, size_t off, uint8_t *blen, uint8_t *btype) {
    size_t rem = total - off;               /* 호출자가 off < total 보장, total ≤ n 은 이미 검사 */
    if (rem < 2) return USB_E_SHORT;        /* bDescriptorType 도 못 읽는다 */
    uint8_t b = buf[off];
    if (b < 2) return USB_E_BLEN_ZERO;      /* 0 이면 off += 0 → 무한 루프 */
    if (b > rem) return USB_E_BLEN_OVERRUN; /* 내용이 버퍼 밖 */
    *blen = b; *btype = buf[off + 1];
    return USB_OK;
}
```

이 세 줄이 퍼징 300만 건에서 "멈춤 0, 범위 밖 읽기 0"을 만든다. 그 위에 의미 검사가 얹힌다: 타입별 최소 길이(BLEN_SMALL — interface 를 5바이트로 선언해 놓고 d[8] 을 읽으면 다음 디스크립터를 읽게 된다), 개수 교차 검증(NUM_IF, NUM_EP), 위치 규칙(EP_ORPHAN), 값 규칙(EP_ADDR, IAD_RANGE). **더 긴** 표준 디스크립터는 허용한다 — UAC1 의 9바이트 endpoint 가 실제로 있고, 스펙도 확장 여지를 남겨 둔다(앞 7바이트만 해석).

검출 시점 추적 (CDC, IF0 의 bNumEndpoints 를 2 로 바꾼 경우):

| off | 타입 | 동작 | 상태 |
|---|---|---|---|
| 17 | INTERFACE 0 | cur=0, decl=2 | found 0 |
| 26..44 | CS ×4 | ext[] 에 기록, 세지 않음 | found 0 |
| 45 | ENDPOINT 0x82 | found 1 | |
| 52 | INTERFACE 1 | **이전 IF: found 1 ≠ decl 2 → NUM_EP** | 반환 |

### 실험 — `demo_fuzz.c` (실측, Apple Silicon, Apple clang 21.0.0, -O2)

예시 3개 × 100만 변이(1~3회: 비트 뒤집기 / 랜덤 바이트 / 0x00 / 0xFF / 잘라내기 / wTotalLength 바꾸기). "순진한 워커"는 흔한 코드 — wTotalLength 를 믿고 `off += buf[off]` 만 하는 루프 — 로, 실제로 넘겨 읽지는 않고 읽었을 위치만 센다.

```text
CDC-ACM+IAD (75 B)   OK  22.44% | naive: clean  39.19%  HANG  6.28%  OOB-read  54.53%
HID mouse (34 B)     OK  24.33% | naive: clean  37.78%  HANG  4.40%  OOB-read  57.82%
UAC2-like (97 B)     OK  27.77% | naive: clean  42.67%  HANG  5.06%  OOB-read  52.28%

robust parser result histogram over 3000000 mutants:
  OK              745343  (24.84%)       NUM_IF           53622  ( 1.79%)
  SHORT           147134  ( 4.90%)       NUM_EP          128780  ( 4.29%)
  BLEN_ZERO       110505  ( 3.68%)       EP_ORPHAN        28085  ( 0.94%)
  BLEN_OVERRUN    205747  ( 6.86%)       EP_ADDR          48564  ( 1.62%)
  BLEN_SMALL       15690  ( 0.52%)       IAD_RANGE        38041  ( 1.27%)
  TYPE             94149  ( 3.14%)
  TOTAL_SMALL      62580  ( 2.09%)
  TOTAL_GT_BUF   1321760  (44.06%)

cross table (rows = naive walker, cols = robust parser):
                  robust OK   robust ERR
  clean              745343       451024
  HANG                    0       157272
  OOB-read                0      1646361

3000000 parses (robust + naive) in 0.14 s CPU
```

(히스토그램은 읽기 쉽게 두 열로 옮겼고 숫자는 그대로.) 읽는 법:

- 순진한 워커는 변이의 **약 5%에서 멈추고(bLength 0), 절반 넘게 범위 밖을 읽는다** — 대부분은 wTotalLength 를 믿었기 때문(잘라내기·wTotalLength 변이가 6가지 중 2가지라 GT_BUF 가 44%로 가장 많다). 펌웨어 호스트(USB OTG 호스트 모드 MCU)나 커널에서 이건 워치독 리셋이거나 보안 버그다.
- 견고한 파서가 OK 라고 한 것은 순진한 워커도 모두 "clean" — 즉 정상 입력에서 둘은 같다. 반대로 순진한 워커가 "clean" 이라 한 것의 **451,024건(38%)은 사실 개수 불일치·타입 오류·고아 EP** 였다. 멈추지 않는다고 맞게 읽은 것은 아니다.
- 바이트당 비용: 300만 건 × (두 파서) 0.14 s → 파싱 한 번에 수십 ns. 검사를 넣어도 비용은 무시할 만하다.

### Part C — enumeration 순서와 로그

```svg
<svg viewBox="0 0 740 470" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">enumeration (이 레슨의 호스트 모델, Windows 스타일 첫 읽기 — 호스트마다 다름)</text>
  <line x1="60" y1="40" x2="60" y2="455" stroke="currentColor" stroke-width="2"/>
  <line x1="560" y1="40" x2="560" y2="455" stroke="currentColor" stroke-width="2"/>
  <text x="40" y="36" fill="currentColor" font-weight="bold">Host</text>
  <text x="535" y="36" fill="currentColor" font-weight="bold">Device</text>
  <g stroke="currentColor"><rect x="572" y="48" width="160" height="96" class="f-mut"/>
    <rect x="572" y="150" width="160" height="236" class="f-warn"/><rect x="572" y="392" width="160" height="56" class="f-ok"/></g>
  <g fill="currentColor" font-size="12">
    <text x="582" y="68">Default 상태</text><text x="582" y="86">주소 0, EP0 만</text>
    <text x="582" y="170">Address 상태 (5)</text><text x="582" y="188">STATUS 뒤에야 주소 변경</text>
    <text x="582" y="412">Configured</text><text x="582" y="430">EP1..n 사용 가능</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="70" y="58">① bus reset</text>
    <text x="70" y="80">② @0 GET_DESCRIPTOR(DEVICE) wLength=64</text>
    <text x="250" y="100">← 첫 패킷 8 B: bMaxPacketSize0 = 8</text>
    <text x="70" y="122">③ bus reset (다시)</text>
    <text x="70" y="142">④ @0 SET_ADDRESS(5) → STATUS(IN ZLP) 후 주소 5</text>
    <text x="70" y="170">⑤ @5 GET_DESCRIPTOR(DEVICE) wLength=18 → 8+8+2</text>
    <text x="70" y="196">⑥ @5 GET_DESCRIPTOR(CONFIG) wLength=9 → wTotalLength 34</text>
    <text x="70" y="222">⑦ @5 GET_DESCRIPTOR(CONFIG) wLength=34 → 8+8+8+8+2</text>
    <text x="70" y="248">⑧ @5 GET_DESCRIPTOR(STRING 0) wLength=255 → LANGID 0x0409</text>
    <text x="70" y="274">⑨ @5 STRING 1, 2 (wIndex=0x0409) → "Logitech", "USB Optical Mouse"</text>
    <text x="70" y="300">    (iSerialNumber = 0 → 요청 안 함)</text>
    <text x="70" y="326">⑩ 드라이버 선택 (class 3/1/2 → HID boot mouse)</text>
    <text x="70" y="350">⑪ @5 SET_CONFIGURATION(1) → STATUS</text>
    <text x="70" y="380">    (HID 드라이버: SET_IDLE, GET_DESCRIPTOR(REPORT) … 이 레슨 밖)</text>
    <text x="70" y="420">⑫ interrupt IN EP 0x81 폴링 시작</text>
  </g>
  <g stroke="currentColor"><line x1="60" y1="86" x2="552" y2="86"/><line x1="560" y1="104" x2="68" y2="104"/>
    <line x1="60" y1="148" x2="552" y2="148"/><line x1="60" y1="356" x2="552" y2="356"/><line x1="560" y1="426" x2="68" y2="426"/></g>
  <g class="f-acc" stroke="currentColor"><path d="M552,82 L560,86 L552,90 Z"/><path d="M68,100 L60,104 L68,108 Z"/>
    <path d="M552,144 L560,148 L552,152 Z"/><path d="M552,352 L560,356 L552,360 Z"/><path d="M68,422 L60,426 L68,430 Z"/></g>
</svg>
```

실제 로그 (`./solution enum`, 마우스, 새 주소 5 — 실제 출력):

```text
RESET (device -> Default state, address 0)
@0 SETUP 80 06 00 01 00 00 40 00 | IN  STD DEV | GET_DESCRIPTOR(DEVICE, 0) wIndex=0x0000 wLength=64
     DATA IN 8 B (first packet only: 8), host ends data stage
     STATUS OUT ZLP ok
RESET again (bMaxPacketSize0 = 8 learned)
@0 SETUP 00 05 05 00 00 00 00 00 | OUT STD DEV | SET_ADDRESS(5) wIndex=0x0000 wLength=0
     STATUS IN ZLP ok
@5 SETUP 80 06 00 01 00 00 12 00 | IN  STD DEV | GET_DESCRIPTOR(DEVICE, 0) wIndex=0x0000 wLength=18
     DATA IN 18 B in 3 pkt (8+8+2)
     STATUS OUT ZLP ok
@5 SETUP 80 06 00 02 00 00 09 00 | IN  STD DEV | GET_DESCRIPTOR(CONFIGURATION, 0) wIndex=0x0000 wLength=9
     DATA IN 9 B in 2 pkt (8+1)
     STATUS OUT ZLP ok
@5 SETUP 80 06 00 02 00 00 22 00 | IN  STD DEV | GET_DESCRIPTOR(CONFIGURATION, 0) wIndex=0x0000 wLength=34
     DATA IN 34 B in 5 pkt (8+8+8+8+2)
     STATUS OUT ZLP ok
@5 SETUP 80 06 00 03 00 00 FF 00 | IN  STD DEV | GET_DESCRIPTOR(STRING, 0) wIndex=0x0000 wLength=255
     DATA IN 4 B in 1 pkt (4)
     STATUS OUT ZLP ok
@5 SETUP 80 06 01 03 09 04 FF 00 | IN  STD DEV | GET_DESCRIPTOR(STRING, 1) wIndex=0x0409 wLength=255
     DATA IN 18 B in 3 pkt (8+8+2)
     STATUS OUT ZLP ok
@5 SETUP 80 06 02 03 09 04 FF 00 | IN  STD DEV | GET_DESCRIPTOR(STRING, 2) wIndex=0x0409 wLength=255
     DATA IN 36 B in 5 pkt (8+8+8+8+4)
     STATUS OUT ZLP ok
@5 SETUP 00 09 01 00 00 00 00 00 | OUT STD DEV | SET_CONFIGURATION(1) wIndex=0x0000 wLength=0
     STATUS IN ZLP ok
CONFIGURED: addr 5, config 1, 1 interface(s), 1 endpoint(s)
result OK  "Logitech" / "USB Optical Mouse"
```

SETUP 8바이트 읽기: `80 06 00 01 00 00 40 00` = bmRequestType 0x80(IN · 표준 · 장치), bRequest 6(GET_DESCRIPTOR), wValue 0x0100(**상위 바이트 = 타입 1 DEVICE, 하위 = index 0**), wIndex 0, wLength 0x0040. 문자열 요청에서는 wIndex 가 LANGID(0x0409)다.

왜 이런 순서인가:

| 단계 | 호스트가 모르는 것 | 왜 |
|---|---|---|
| 첫 GET_DESCRIPTOR(64) | EP0 최대 패킷 | offset 7 이 첫 8바이트 안에 있도록 디스크립터가 설계됨. 첫 패킷만 받고 끝낸다. Windows 는 이 뒤 다시 reset 한다는 것이 널리 알려진 동작이고, Linux 도 "new scheme" 에서 64바이트 요청을 쓴다 (둘 다 확인 권장) |
| SET_ADDRESS | 주소 | 주소 0 은 지금 enumeration 중인 장치 하나뿐 (허브가 포트를 하나씩 enable) |
| config 9 → 전체 | wTotalLength | 크기를 모르는 데이터를 위한 2단계 읽기 |
| STRING 0 | 지원 언어 | 문자열 요청의 wIndex 로 쓸 LANGID |
| SET_CONFIGURATION | — | 이 순간부터 EP0 외 엔드포인트가 살아난다 (Configured 상태) |

**ZLP 규칙** (`data_stage_packets`): 데이터 단계는 "wLength 만큼 다 보냈다" 또는 "mps0 보다 짧은 패킷이 왔다" 로 끝난다. 그래서 실제 길이가 mps0 의 배수이고 wLength 보다 짧으면 장치는 **0바이트 패킷(ZLP)** 을 하나 더 보내야 한다. 테스트: (16 B, wLength 255, mps 8) → `8+8+0`, (64, 255, 64) → `64+0`, (16, 16, 8) → `8+8` (wLength 에 도달했으니 ZLP 불필요). 장치 펌웨어가 ZLP 를 빼먹으면 호스트는 다음 IN 을 계속 보내고 장치는 NAK 만 하다가 전송이 타임아웃난다 — 길이가 우연히 64의 배수인 문자열/디스크립터에서만 터지는 버그.

**SET_ADDRESS 는 status 단계 뒤에** (USB 2.0 §9.4.6): status 단계(IN ZLP)는 아직 **주소 0** 으로 오기 때문이다. C7 테스트의 `bug_addr_early` 장치는 SETUP 을 받자마자 주소를 5로 바꿔서, 주소 0 으로 온 status IN 에 답하지 않는다 → 호스트는 `STATUS: no response` 로 enumeration 실패. 실제 칩에서: STM32 USB FS device 주변장치(F0/F1/L0/G4 등)는 status IN 완료 인터럽트에서 DADDR 을 써야 하고, OTG 코어(F4/F7 의 OTG_FS/HS)는 하드웨어가 처리하므로 SETUP 직후 DCFG.DAD 를 바로 써도 된다 — ST HAL 이 두 코어에서 다르게 처리하는 이유다 (RM·HAL 소스 확인 권장).

다른 결함 주입 결과 (테스트로 확인):

| 주입 | 결과 |
|---|---|
| iSerialNumber=3 인데 문자열 3 없음 | 그 요청만 STALL, `str_missing=1`, 그래도 Configured (문자열은 선택 사항) |
| wTotalLength 를 80 으로 부풀리고 75 B 만 보냄 | 2번째 config 읽기 75 B → `TOTAL_GT_BUF`, SET_CONFIGURATION 안 함 |
| SET_ADDRESS 즉시 적용 | status 단계 무응답 → `NORESP` |
| string 0 STALL (LANGID 없음) | 문자열 2개 건너뜀(`str_missing=2`), Configured |

### 엔드포인트 타입 정리 (USB 2.0 — 수치는 스펙 확인 권장)

| | control | bulk | interrupt | isochronous |
|---|---|---|---|---|
| 용도 | enumeration, 설정·명령 (EP0 필수) | 대용량, 시간 무관: 저장장치, CDC 데이터, 프린터 | 작고 주기적: 키보드·마우스, CDC 알림, 센서 | 실시간 스트림: 오디오, 비디오 |
| 대역폭 | 예약 몫 있음 (FS 10%, HS 20%) | 보장 없음 — 남는 시간 전부 | 주기·크기 **보장** (periodic) | 주기·크기 **보장** (periodic) |
| 지연 | 최선 노력 | 버스가 바쁘면 무한정 | 최대 bInterval | 고정 주기 |
| 오류 시 | 핸드셰이크 + 재시도 | 재시도 (데이터 무결) | 재시도 (다음 주기) | **재시도 없음**, CRC 오류는 버림 |
| 최대 패킷 LS / FS / HS | 8 / 8·16·32·64 / 64 | — / 8·16·32·64 / 512 | 8 / 64 / 1024 (×3) | — / 1023 / 1024 (×3) |

periodic(interrupt + iso) 예약 한도는 FS 프레임의 90%, HS 마이크로프레임의 80% 로 알려져 있다. 오디오가 bulk 를 안 쓰는 이유: bulk 는 빠를 수는 있어도 **언제** 올지 보장이 없고, 오디오는 늦게 온 정확한 샘플보다 제때 온 약간 틀린 샘플이 낫다(그래서 iso 는 재시도도 없다).

### 복잡도와 RAM

파서: O(wTotalLength), 반복 ≤ wTotalLength/2 (매번 2바이트 이상 전진). RAM: `usb_cfg` 표가 IF 16×9 + EP 32×7 + IAD 8×6 + 기타 32×6 ≈ 600 B 정도 — MCU 호스트에서도 정적 할당 가능. 디스크립터 원본은 복사하지 않고 `ext[].off` 로 가리킨다 (그래서 `host_ctx` 는 config 버퍼와 문자열 버퍼를 따로 둔다 — 문자열을 읽으며 config 를 덮어쓰지 않게).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution            # 109/109 passed, exit 0 (약 0.1 초)
./solution dump     # 예시 3개 덤프 (§5)     ./solution enum   # 마우스 enumeration 로그 (§5)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 오류 없음
cc -std=c11 -Wall -Wextra -O2 demo_fuzz.c -o demo_fuzz && ./demo_fuzz                   # 300만 변이, 약 0.3 초
```

실행 결과 (실제 출력):

```text
B fuzz: 18000 mutants  OK=4413  SHORT=905 BLEN_ZERO=662 OVERRUN=1193 SMALL=100 TYPE=584 TOTAL_SMALL=398 GT_BUF=7971 NUM_IF=306 NUM_EP=787 ORPHAN=169 EP_ADDR=289 IAD=223  (12 error kinds)
109/109 passed
```

ASan/UBSan 빌드도 같은 출력이고 경고가 없다 — 퍼징 버퍼를 `malloc(n)` 정확한 크기로 잡았으므로, 파서가 n 을 1바이트라도 넘었다면 ASan 이 heap-buffer-overflow 로 멈췄을 것이다.

## 7. 자주 하는 실수

1. **`while (off < total) off += buf[off];` 에 검사 없음** — bLength 0 이면 무한 루프, 크면 범위 밖. 실측: 변이 입력의 약 5% 멈춤, 절반 이상 범위 밖 읽기. → `desc_next` 의 세 검사.
2. **wTotalLength 를 믿는다** — 장치가 75 B 만 보내고 80 이라고 할 수 있다. → `total > n` 이면 `TOTAL_GT_BUF`, 걷기는 `min` 이 아니라 아예 거부.
3. **"config 다음은 interface" 라고 가정** — IAD, 클래스 디스크립터, 벤더 디스크립터가 끼어든다. 타입으로 분기하고 모르는 건 건너뛴다. 반대로 HID 디스크립터를 "interface 뒤 정확히 +9" 로 찾는 코드도 깨진다.
4. **bNumInterfaces = interface 디스크립터 개수로 검사** — alternate setting 이 있는 장치(UAC, UVC, 많은 CDC-NCM)를 전부 거부한다. 서로 다른 번호를 센다.
5. **wMaxPacketSize 를 그대로 바이트로 씀** — 0x1400 을 5120 바이트로 읽으면 버퍼를 5배로 잡거나 대역폭을 잘못 계산한다. `w & 0x7FF` 와 `((w >> 11) & 3) + 1`.
6. **HS bInterval 을 ms 로 해석** — HS interrupt bInterval 4 는 4 ms 가 아니라 2³ × 125 µs = 1 ms. HS 로 옮긴 장치가 갑자기 8배 자주(혹은 드물게) 폴링되는 버그.
7. **문자열을 "한 바이트 건너 하나씩" ASCII 로 읽기** — 한글·기호·서로게이트 쌍이 깨진다. 홀수 bLength 는 거부.
8. **장치 펌웨어: SET_ADDRESS 즉시 적용, ZLP 누락, wLength 무시하고 전체 전송** — 셋 다 이 레슨 mock 이 재현한다. wLength 보다 많이 보내면 호스트는 babble/overflow 로 본다.

```c
/* 실수 8 의 고친 모양 (장치 쪽 GET_DESCRIPTOR) */
size_t k = desc_len < wLength ? desc_len : wLength;      /* 잘라서 */
send_ep0(desc, k, /*zlp=*/ k < wLength && k % mps0 == 0);  /* 배수면 ZLP */
```

## 8. 면접 Q&A

<details><summary>Q1. USB 장치를 꽂으면 무슨 일이 일어나는지 처음부터 말해 보세요.</summary>

허브가 D+/D− 풀업으로 연결과 속도(LS = D− 풀업, FS = D+ 풀업, HS 는 reset 중 chirp 로 협상)를 감지 → 호스트가 포트 reset → 장치는 Default 상태, 주소 0. 호스트가 GET_DESCRIPTOR(DEVICE) 로 첫 8/64바이트를 읽어 bMaxPacketSize0 을 알고, (Windows 스타일이면) 다시 reset, SET_ADDRESS(n) — 장치는 status 단계 뒤 주소 n. device 18 B → config 9 B(wTotalLength) → config 전체 → LANGID → 문자열 → 드라이버 매칭(VID/PID 또는 class/sub/proto, composite 면 IAD 단위) → SET_CONFIGURATION → 클래스 드라이버가 자기 요청(HID SET_IDLE, CDC SET_LINE_CODING …)을 보낸다. 이 레슨의 `./solution enum` 로그가 정확히 이 순서다.
</details>

<details><summary>Q2. 왜 첫 GET_DESCRIPTOR 는 64바이트를 요청하고 첫 패킷만 받나요? 18바이트를 바로 요청하면 안 되나요?</summary>

호스트는 EP0 최대 패킷 크기를 아직 모른다. 8로 가정하고 18을 요청해도 동작은 하지만, 장치의 mps0 가 64라면 장치는 18바이트를 한 패킷에 보낸다 — 호스트가 8바이트 패킷을 기대했다면 babble 이 된다. 그래서 "충분히 큰 64로 요청 → 첫 패킷만 받기" 가 안전하다: 어떤 mps0 든 첫 패킷은 ≥ 8 이고 offset 7(bMaxPacketSize0)을 담는다. 그 뒤 reset 하는 것은 Windows 의 오래된 호환성 동작으로 알려져 있다 (확인 권장). 펌웨어 쪽 교훈: 장치는 데이터 단계를 호스트가 **도중에 끝내도**(조기 status) 꼬이지 않아야 한다.
</details>

<details><summary>Q3. SET_ADDRESS 를 받자마자 주소 레지스터를 바꾸면 어떻게 되나요?</summary>

SET_ADDRESS 의 status 단계(호스트 IN, 장치 ZLP)는 **아직 주소 0** 으로 온다. 주소를 먼저 바꾼 장치는 그 IN 토큰을 자기 것으로 보지 않아 답하지 않고, 호스트는 타임아웃 → 재시도 → "장치 설명자 요청 실패". 이 레슨 C7 테스트가 재현(`STATUS: no response`, `NORESP`). 스펙: USB 2.0 §9.4.6 "status 단계가 끝난 뒤 주소 변경", 그리고 호스트는 SET_ADDRESS 후 2 ms 의 복구 시간을 준다(§9.2.6.3, 확인 권장). STM32 USB FS 주변장치는 status 완료 콜백에서 DADDR 을 쓰고, OTG 코어는 하드웨어가 처리해 즉시 써도 된다 — 코어마다 다르니 RM 확인.
</details>

<details><summary>Q4. 네 가지 전송 타입을 언제 쓰나요? 오디오는 왜 iso 이고 재시도가 없죠?</summary>

control = 설정·명령(EP0), bulk = 대용량·시간 무관(UAS/BOT 저장장치, CDC 데이터 — Don 의 SSD 경험으로 치면 USB 인클로저의 데이터 경로), interrupt = 작고 주기적인 것(HID, CDC 알림), iso = 실시간 스트림. periodic 타입만 대역폭이 예약된다. 오디오는 1 ms(FS) / 125 µs(HS) 마다 정해진 양이 **제때** 가야 한다 — 늦은 재전송은 이미 쓸모가 없으니 iso 는 핸드셰이크·재시도가 없고, CRC 오류 패킷은 버려 한 샘플 블록이 튄다. async 출력 장치는 장치 쪽 클럭 기준으로 feedback EP(이 레슨 0x81, 4 B, 1 ms)로 호스트에게 "샘플을 조금 더/덜 보내라" 를 알린다.
</details>

<details><summary>Q5. IAD 는 왜 필요하고, device class 0xEF/0x02/0x01 은 무슨 뜻인가요?</summary>

USB 의 드라이버 바인딩 단위는 기본적으로 interface 하나다. 그런데 CDC-ACM(통신 + 데이터), UVC(제어 + 스트리밍), UAC(제어 + 스트림들)처럼 **여러 interface 가 한 기능**인 클래스가 있다. 장치가 그것 하나뿐이면 device class 로 표현되지만, CDC 두 개나 CDC + MSC 를 한 장치에 넣으면(composite) OS 가 어떤 interface 를 묶어야 할지 모른다. IAD(타입 0x0B)가 [bFirstInterface, +bInterfaceCount) 를 한 기능으로 묶고, device class EF/02/01 은 "IAD 를 쓰는 장치" 라는 표시다 (IAD ECN). LTE/Wi-Fi 모듈이 USB 로 AT 포트·진단 포트·네트워크를 여러 개 내보낼 때가 전형적인 composite 다.
</details>

<details><summary>Q6. ZLP 는 언제 보내야 하고, 빼먹으면 어떻게 되나요?</summary>

control/bulk 전송의 끝은 "요청 길이를 다 채움" 또는 "짧은 패킷" 이다. 보낼 데이터가 wLength 보다 짧고 길이가 mps 의 배수이면 마지막 패킷이 "꽉 찬" 패킷이라 호스트는 끝을 모른다 → 장치가 0바이트 패킷을 하나 더 보내야 한다. 빼먹으면 호스트가 IN 을 계속 보내고 장치는 NAK → 타임아웃. 디스크립터·문자열 길이가 우연히 64(또는 8)의 배수일 때만 나타나서 늦게 발견된다. 이 레슨 `data_stage_packets(64, 255, 64)` → `64+0` 테스트. bulk 에서도 같은 규칙이 CDC 등에서 문제를 일으킨다 (클래스별 확인).
</details>

<details><summary>Q7. 장치가 준 디스크립터를 왜 이렇게 의심하나요? 어떻게 테스트하나요?</summary>

호스트 입장에서 디스크립터는 **외부 입력**이다. 악의적인 USB 장치(BadUSB 류, 혹은 FaceDancer 같은 장치 에뮬레이터)가 bLength 0, 거짓 wTotalLength, 개수 불일치를 보낼 수 있고, 커널 USB 코어와 클래스 드라이버의 디스크립터 파싱은 실제로 퍼징(syzkaller 의 USB 퍼징 등)으로 많은 버그가 발견된 곳이다 (확인 권장). 방법: 경계 검사 반복자 하나로 모든 이동을 강제, 고정 크기 표 + CAP 오류, 변이 퍼징을 정확한 크기 버퍼 + ASan 으로. 실측: 순진한 워커는 변이의 5% 에서 멈추고 55% 에서 범위 밖을 읽었고, 견고한 파서는 0/0 이었다. 리눅스는 `usb_parse_configuration()`(drivers/usb/core/config.c)이 비슷한 검사를 하며, 이상한 것은 경고하고 고쳐서 계속 가는 쪽이 많다(관대함 vs 엄격함 — 호스트는 관대, 테스트 장비는 엄격이 보통).
</details>

<details><summary>Q8. 개발 중인 보드가 "알 수 없는 USB 장치(장치 설명자 요청 실패)"로 뜹니다. 무엇부터 보나요?</summary>

(1) 전기: D+ 풀업이 켜지는지, VBUS 감지 설정, FS 는 48 MHz 클럭 정확도(HSE/CRS — 내부 RC 그대로면 실패), 케이블. (2) EP0: 첫 SETUP 을 받는지(인터럽트가 오는지), bMaxPacketSize0 와 EP0 버퍼 설정 일치, 첫 GET_DESCRIPTOR 에 데이터가 나가는지. (3) SET_ADDRESS 타이밍(Q3). (4) 디스크립터 바이트: wTotalLength = 실제 합(가장 흔한 실수 — 연습 4 의 빌더가 자동 계산하는 이유), bNumInterfaces, bNumEndpoints, bLength. (5) ZLP. 도구: USB 프로토콜 분석기(Beagle, Ellisys)나 리눅스 호스트의 usbmon/Wireshark 로 SETUP 을 이 레슨 로그처럼 본다. macOS 에서는 `ioreg -p IOUSB -l` 에 장치가 어디까지 올라왔는지가 보인다.
</details>

## 9. 추가 연습

### 연습 1 — HID report 디스크립터 아이템 워커 (실측 데이터)

이 Mac 마우스의 `ReportDescriptor`(ioreg, 46 B)를 short item(prefix = bTag 7:4, bType 3:2, bSize 1:0 — 3 은 4바이트)과 long item(0xFE)으로 걸어서 아이템 수, collection 깊이, Input 아이템의 총 비트(Report Size × Report Count 합)를 구하라. 잘림·짝 안 맞는 End Collection 은 오류.

<details><summary>답</summary>

실제 출력: `items=24 collections=2 depth=2 input_items=2 input_bits=32 (4 bytes)` — ioreg 의 `MaxInputReportSize = 4` 와 일치. 구조: Usage Page(Generic Desktop) / Usage(Mouse) / Collection(Application) / Usage(Pointer) / Collection(Physical) / 1비트 × 8개 Input(Data,Var,Abs) — usage 는 버튼 1..3 만 정의되어 있고 나머지 5비트는 HID 규칙상 마지막 usage 를 이어받아 사실상 쓰이지 않는다 (HID 1.11 확인 권장) / X, Y, Wheel 을 8비트 × 3개 (−127..127, Input Data,Var,**Rel**) / End ×2. 즉 리포트 = [버튼][dX][dY][wheel] 4바이트. HID 디스크립터의 wDescriptorLength(46)가 이 길이와 같은지도 검사. 45바이트로 자르면 UNBALANCED, 43이면 TRUNC.
</details>

### 연습 2 — 주기 전송 대역폭과 alt 0 규칙

interrupt/iso 엔드포인트가 예약하는 최대 바이트/초 = mps × (추가 트랜잭션 + 1) × 10⁶ / 주기(µs) 를 구하고, "기본 설정(alt 0)에는 크기 > 0 인 iso 엔드포인트가 없어야 한다" 를 검사하라.

<details><summary>답</summary>

실제 출력: `UAC data 448000 B/s, feedback 4000 B/s, mouse 400 B/s, CDC notify 500 B/s, HS hi-bw iso 24576000 B/s`. 56 B × 8000/s = 448 kB/s (48 kHz 스테레오 32비트 슬롯 384 kB/s + 여유), 3072 B × 8000/s ≈ 24.6 MB/s (HS 한 엔드포인트의 최대 — 비디오가 쓰는 크기). alt 0 규칙은 USB 2.0 §5.6.3 으로 기억한다(확인 권장): 장치가 Configured 되자마자 대역폭을 잡지 않도록, 스트리밍 interface 는 alt 0 = 0 대역폭, 재생 시작 때 SET_INTERFACE(alt 1)로 대역폭을 요청한다. 그래서 UAC 예시가 alt 0 에 EP 0개를 둔다.
</details>

### 연습 3 — 드라이버 바인딩: 엔드포인트 찾기

`find_ep(cfg, if_class, alt, type, dir_in)` 와 `iad_for_interface(cfg, ifnum)` 을 짜서 CDC 의 bulk IN/OUT, 알림 EP, UAC alt 1 의 iso OUT/feedback 을 찾아라.

<details><summary>답</summary>

CDC: (0x0A, alt 0, bulk, IN) → 0x81, OUT → 0x01, (0x02, interrupt IN) → 0x82, interface 1 → IAD 0. UAC: (0x01, alt 1, iso OUT) → 0x01, alt 0 → −1, iso IN → 0x81(usage = feedback). 실제 드라이버는 하드코딩된 EP 번호가 아니라 이렇게 디스크립터에서 찾는다 — 장치 펌웨어가 EP 번호를 바꿔도 동작하게. CDC 는 원래 Union 기능 디스크립터(CS_INTERFACE subtype 0x06)가 제어/데이터 interface 를 이어 주는데, 이 레슨 파서는 그것을 ext[] 로만 남기므로 (`ext[3].subtype == 0x06`, `owner == 0`) 거기서 bSubordinateInterface0 을 읽도록 확장할 수 있다.
</details>

### 연습 4 — 장치 쪽 configuration 빌더

장치 펌웨어 입장: `cb_begin / cb_iad / cb_iface / cb_put(클래스 디스크립터) / cb_ep / cb_finish` 로 configuration 을 조립하고, **wTotalLength 와 bNumInterfaces 를 마지막에 자동으로** 채워라. 용량 초과는 끈적한(sticky) 오류.

<details><summary>답</summary>

실제 출력: `builder -> 75 bytes, identical to CDC_CFG; cap 74 -> CAP`. 손으로 쓴 75바이트 배열과 바이트 단위로 같고, 본문 파서가 OK 를 준다. TinyUSB 의 `TUD_CONFIG_DESCRIPTOR(... TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN ...)` 매크로나 ST 라이브러리의 `USB_CDC_CONFIG_DESC_SIZ` 상수가 하는 일을 런타임에 한 것이다 — 손으로 센 합계가 틀리는 것이 enumeration 실패 1순위라서.
</details>

### 연습 5 — UTF-8 → string 디스크립터 (장치 쪽)

UTF-8 문자열을 UTF-16LE string 디스크립터로 만들어라. U+10000 이상은 서로게이트 쌍, bLength ≤ 255 이므로 코드 유닛은 최대 126개, 잘못된 UTF-8(overlong, 끊김, 서로게이트를 UTF-8 로 인코딩, > U+10FFFF)은 거부.

<details><summary>답</summary>

"한글 🔌" → 12 B 로 본문의 `STR_KO` 와 동일. 'A' × 126 → 254 B OK, 127 → TOO_LONG. 🔌 × 63 = 126 유닛 OK, 64 → TOO_LONG (글자 수가 아니라 UTF-16 유닛 수가 한계). `C0 80`, `ED A0 80`, `F4 90 80 80`, `E2 82`(끊김) 모두 거부. 랜덤 왕복 3000/3000: 임의 코드 포인트로 만든 디스크립터 → 본문 디코더로 UTF-8 → 이 인코더 → 원래 바이트와 동일.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 40/40 passed
```

```text
practice 1: items=24 collections=2 depth=2 input_items=2 input_bits=32 (4 bytes)
practice 2: UAC data 448000 B/s, feedback 4000 B/s, mouse 400 B/s, CDC notify 500 B/s, HS hi-bw iso 24576000 B/s
practice 4: builder -> 75 bytes, identical to CDC_CFG; cap 74 -> CAP
practice 5: random round trips 3000/3000
40/40 passed
```

## 10. 핵심 정리

- 디스크립터 = [bLength][bDescriptorType][내용], 다바이트 필드는 LE. 파서는 bLength 로만 이동, 모르는 타입(HID 0x21, CS 0x24/0x25)은 건너뛴다.
- configuration 은 평평한 바이트열: endpoint 는 직전 interface 소속, IAD 는 interface 묶음, bNumInterfaces 는 alt 를 뺀 번호 개수.
- 이동 전 세 검사(남은 ≥ 2, bLength ≥ 2, bLength ≤ 남은) + wTotalLength ≤ 받은 바이트 + 개수 교차 검증. 실측: 순진한 워커는 변이의 ~5% 멈춤, ~55% 범위 밖, 견고한 파서는 0.
- endpoint: bit7 방향, bits1:0 타입, iso sync 3:2 / usage 5:4, wMaxPacketSize 10:0 + 12:11(×1~3), bInterval 은 FS/LS interrupt 만 ms, 나머지는 2^(b−1) × 125 µs/1 ms.
- enumeration: 64 요청·첫 패킷(mps0) → reset → SET_ADDRESS(status 뒤 적용) → device 18 → config 9 → 전체 → LANGID → 문자열 → SET_CONFIGURATION.
- EP0 데이터: min(길이, wLength), mps0 로 분할, 배수이고 짧으면 ZLP.
- periodic(interrupt/iso)만 대역폭 보장, iso 는 재시도 없음, bulk 는 남는 시간 전부. alt 0 = 0 대역폭.

## 참고자료

- USB-IF, *Universal Serial Bus Specification Revision 2.0* — ch.5(전송 타입, 대역폭), ch.8(패킷·ZLP), ch.9(§9.3 SETUP, §9.4 표준 요청, §9.6 디스크립터 표 9-8 ~ 9-13): <https://www.usb.org/document-library/usb-20-specification> (이 레슨의 오프셋·비트 정의는 기억에 의존, 원문 확인 권장)
- USB-IF, *Interface Association Descriptors* ECN (2003) / *USB Interface Association Descriptor Device Class Code and Use Model* (class 0xEF/0x02/0x01).
- USB-IF Class specs: CDC 1.2 + PSTN 1.2 (ACM, 기능 디스크립터), HID 1.11 (HID 디스크립터, report 아이템 형식, boot protocol), Audio Device Class 2.0 (UAC2) — <https://www.usb.org/documents>
- Craig Peacock, *USB in a NutShell* — 디스크립터·enumeration 입문: <https://www.beyondlogic.org/usbnutshell/usb1.shtml>
- Linux: `drivers/usb/core/config.c` (`usb_parse_configuration`), `drivers/usb/core/hub.c` (`hub_port_init`, old/new enumeration scheme), `lsusb -v`, usbmon (`Documentation/usb/usbmon.rst`).
- TinyUSB (`tud_descriptor_*_cb`, `TUD_CONFIG_DESCRIPTOR` 매크로), STMicroelectronics STM32 USB Device Library / HAL PCD (`HAL_PCD_SetAddress` — 코어별 동작 확인 권장).
- pid.codes — 오픈소스 프로젝트용 VID 0x1209 공유 (예시 VID 출처, PID 할당 규칙은 사이트 확인): <https://pid.codes>
- 이 Mac: `ioreg -p IOUSB -l -w0`, `ioreg -l -w0 -r -c IOUSBHostInterface`, `system_profiler SPUSBHostDataType` (macOS 27.2 에서 `SPUSBDataType` 은 빈 출력).
- 관련 레슨: COMM-B10 (길이 prefix 파서·재동기화), COMM-B12 (COBS), COMM-M10 (견고한 스트림 파서), COMM-M12 (드라이버 계층화·오류 코드 변환), COMM-M13 (비동기 전송 큐), COMM-M14 (재시도·타임아웃), C-B13 (엔디언), C-B12 (패딩 — 디스크립터를 struct 로 캐스팅하지 않는 이유), TEST-M02 (파서 퍼징, 다음 단계), BM-M08 (DMA 드라이버, 다음 단계)
