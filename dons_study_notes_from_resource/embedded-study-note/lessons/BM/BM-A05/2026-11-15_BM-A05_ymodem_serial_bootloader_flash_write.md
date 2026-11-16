---
id: BM-A05
time: 120분
updated: 2026-11-15
---
# BM-A05 시리얼 부트로더: YMODEM 수신과 플래시 기록

## 학습 목표

- YMODEM 패킷(SOH 128 / STX 1024, 블록 번호 + 1의 보수, CRC-16/XMODEM)과 세션('C' → block 0 → 데이터 → EOT·NAK·EOT·ACK → null block 0)을 바이트 단위로 그리고, 수신 상태 머신을 **타임아웃·재시도 상한이 있는** 코드로 짤 수 있다.
- 중복 블록(ACK 유실)은 ACK 만 하고 **다시 쓰지 않으며**, 그 밖의 번호는 동기 상실로 CAN 하는 이유를 설명하고, 파일 크기로 0x1A 패딩을 잘라 내는 규칙을 지킨다.
- 블록마다 lazy erase → blank check → program → read-back verify 하는 플래시 경로를 만들고, **헤더(유효 표시)를 마지막에 쓰는 commit** 으로 어느 플래시 연산에서 전원이 나가도 다음 부팅이 그 이미지를 거부함을 전수 테스트로 보인다.
- 지우기 정책(lazy / upfront)과 송신기 타임아웃, 블록 크기와 비트 오류율의 관계를 시뮬레이터 숫자로 방어한다.

## 1. 문제

"현장에서 벽돌이 된 보드를 UART 하나로 살린다" — 시리얼 부트로더는 펌웨어 면접에서 **"업데이트 중에 전원이 나가면?"**, **"ACK 가 사라지면 같은 블록이 두 번 오는데 어떻게 하죠?"**, **"플래시 지우는 동안 UART 바이트는?"**, **"받은 이미지가 진짜 완전한지 어떻게 알죠?"** 를 한 번에 묻는 단골 주제다. STM32 의 IAP 예제(AN3965 계열 "in-application programming using the USART", 문서 번호 확인 권장)와 수많은 양산 부트로더가 YMODEM 을 쓰는 이유는 단순하다: 터미널 프로그램(Tera Term, minicom + lrzsz `sz --ymodem`)만 있으면 되고, 프로토콜이 작아서 부트로더 몇 KB 에 들어간다. SSD 펌웨어에서 FTL 메타데이터를 "데이터 먼저, 커밋 레코드는 마지막" 으로 쓰던 원칙이 여기서 그대로 **"payload 먼저, 헤더는 마지막"** 이 된다.

이 레슨은 맥에서 돈다. UART·송신기·채널(비트 플립/유실)·플래시는 결정적 시뮬레이터이고, 시간 단위는 **1 tick = 115200 8N1 의 1 바이트 시간 = 86.8 µs (11520 tick = 1 s)** 다. 수신기 코드는 진짜 부트로더처럼 `uart_getc(timeout)` 로 블로킹 읽기를 하고, 그 호출이 시뮬레이터 시간을 진행시킨다 (송신기 상태 머신은 그 사이에 돈다). 플래시 erase/program 은 `plat_busy()` 로 시간을 먹고, 그동안 들어온 바이트는 64 B RX FIFO 에 쌓이거나 넘친다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">YMODEM 패킷 (CRC 모드) — 이 레슨의 송·수신기가 쓰는 형식</text>
  <rect x="10" y="34" width="60" height="40" class="f-acc" stroke="currentColor"/>
  <text x="18" y="52" fill="currentColor">SOH 01</text><text x="18" y="67" fill="currentColor">STX 02</text>
  <rect x="70" y="34" width="60" height="40" class="f-mut" stroke="currentColor"/>
  <text x="80" y="52" fill="currentColor">blk</text><text x="80" y="67" fill="currentColor">mod 256</text>
  <rect x="130" y="34" width="60" height="40" class="f-mut" stroke="currentColor"/>
  <text x="140" y="52" fill="currentColor">255−blk</text><text x="140" y="67" fill="currentColor">보수</text>
  <rect x="190" y="34" width="400" height="40" class="f-ok" stroke="currentColor"/>
  <text x="200" y="52" fill="currentColor">data: SOH → 128 B, STX → 1024 B</text>
  <text x="200" y="67" fill="currentColor">마지막 블록은 0x1A(CPMEOF) 로 패딩 — 크기로 잘라 낸다</text>
  <rect x="590" y="34" width="70" height="40" class="f-warn" stroke="currentColor"/>
  <text x="598" y="52" fill="currentColor">CRC hi</text>
  <rect x="660" y="34" width="70" height="40" class="f-warn" stroke="currentColor"/>
  <text x="668" y="52" fill="currentColor">CRC lo</text>
  <text x="540" y="90" fill="currentColor">CRC-16/XMODEM(data)</text>
  <text x="540" y="105" fill="currentColor">0x1021, init 0, 빅엔디언 전송</text>
  <text x="10" y="100" fill="currentColor">총 길이: 133 B (SOH) / 1029 B (STX). 보수 검사는 헤더만, CRC 는 data 만 덮는다.</text>
  <text x="10" y="138" fill="currentColor" font-weight="bold">block 0 (blk = 0, SOH 128 B, 남는 바이트 = 0x00)</text>
  <rect x="10" y="150" width="150" height="40" class="f-acc" stroke="currentColor"/>
  <text x="18" y="174" fill="currentColor">"app_v1.2.3.bin"</text>
  <rect x="160" y="150" width="40" height="40" class="f-mut" stroke="currentColor"/>
  <text x="168" y="174" fill="currentColor">NUL</text>
  <rect x="200" y="150" width="90" height="40" class="f-ok" stroke="currentColor"/>
  <text x="208" y="174" fill="currentColor">"40064"</text>
  <rect x="290" y="150" width="250" height="40" class="f-mut" stroke="currentColor"/>
  <text x="298" y="174" fill="currentColor">" 15071360000 100644" (mtime, mode 8진)</text>
  <rect x="540" y="150" width="190" height="40" class="f-mut" stroke="currentColor"/>
  <text x="548" y="174" fill="currentColor">00 00 … 00</text>
  <text x="200" y="208" fill="currentColor">↑ 10진 파일 크기: 부트로더는 필수로 요구 (없으면 BLOCK0 오류)</text>
  <text x="10" y="236" fill="currentColor">null block 0 = 이름 길이 0 (첫 바이트 NUL) → "배치 끝"</text>
  <text x="10" y="262" fill="currentColor">제어 문자: EOT 04 · ACK 06 · NAK 15 · CAN 18 (2 개 연속 = 중단) · 'C' 43 (CRC 모드 요청)</text>
  <text x="10" y="288" fill="currentColor">Forsberg: 패킷 시작 대기 10 s, 글자 사이 1 s, 오류 10 회 재시도 — 이 레슨 모델 값 (문서 확인 권장)</text>
</svg>
```

전송되는 파일은 그냥 바이너리가 아니라 **BM-A04 이미지**(64 B 헤더 + payload)다. 이 레슨에는 그 헤더의 축약판(CRC-32 만, SHA 생략)이 들어 있다: magic `0x474D4942`, hdr_version 1, hdr_size 64, img_version, payload_size, load_addr(= 슬롯 + 64), payload_crc32, hdr_crc32. 플래시는 BM-A02 모델 축약판: 0x08000000, 16 KB 섹터 × 8, prog_unit 8 B, program 은 AND(1→0 만), `cut_at` 번째 연산에서 전원 차단(찢긴 erase = 섞인 비트, 찢긴 program = 1→0 전이의 무작위 일부). 섹터 0–1 은 부트로더, **슬롯 = 섹터 2–7 = 0x08008000, 96 KB**.

### Part A — YMODEM 수신 핵심

- `crc16_xmodem(p, n)` — "123456789" → 0x31C3.
- `ym_read_packet(buf, &len, &blk, first_to)` — 첫 바이트는 `first_to`, 나머지는 글자마다 1 s. 반환: OK / EOT / CAN(2 개 연속) / TIMEOUT / SHORT / GARBAGE / BAD_BLK / BAD_CRC.
- `ym_parse_block0(d, len, name, cap, &size)` — 0 OK, 1 null(배치 끝), −1 형식 오류(NUL 없음, 크기 없음, 32 비트 넘침, 숫자 뒤 이상한 문자).
- `ym_receive(y)` — 'C' 로 시작해 null block 0 까지. 깨끗한 전송의 응답 바이트열을 정확히 맞춘다: `C ACK C ACK ACK NAK ACK C ACK` (164 B 파일, 블록 2 개).

### Part B — 고장 주입 (전부 결과를 단언)

비트 플립(데이터 / 블록 번호 / 시작 바이트), 바이트 유실, ACK 유실(→ 중복 블록), 송신기의 이중 전송, block 0 ACK 유실, 늦게 켜진 송신기, EOT 응답 유실 두 종, 블록 6 에서 송신기 사망(→ NAK 10 회 + CAN), 중간 CAN CAN, 틀린 블록 번호, 잘린 전송(EOT 가 일찍), block 0 크기 거짓말, 슬롯보다 큰 파일, 나쁜 이미지 3 종(magic / 크기 불일치 / payload CRC), 0 으로 안 써지는 셀(verify 실패), 그리고 고정 시드 300 회 랜덤 soak. 불변식: **"성공이면 슬롯 == 파일, 실패면 부트로더가 슬롯을 거부"**.

### Part C — 플래시 기록과 전원 차단

- `sink_write(s, off, d, n)` — 헤더 64 B 는 RAM 에 모아 즉시 검증(fail fast), payload 는 lazy erase → 0xFF 꼬리 패딩 → blank check → program → verify.
- `sink_commit(s)` — 두 번째 EOT 에서: 플래시에서 다시 읽은 payload CRC 확인 → 헤더 program (마지막 쓰기) → verify → `img_check`.
- 전원 차단: 27 개 플래시 연산 × 시드 3 = 81 시나리오, 재부팅 후 판정 + 재전송 복구. 부트 로그 시나리오(빈 보드 복구, 전송 중 정전 → 재전송).

## 2. Problem (English)

> **Serial bootloader: YMODEM receive into a flash slot.**
>
> A Cortex-M bootloader owns flash sectors 0–1; the application slot is sectors 2–7 (0x08008000, 96 KB, 16 KB sectors, 8-byte program unit, NOR semantics). The image has a 64-byte header (magic, versions, payload size, load address, payload CRC-32, header CRC-32). Implement:
>
> 1. `uint16_t crc16_xmodem(const uint8_t *p, uint32_t n)` (poly 0x1021, init 0; "123456789" → 0x31C3).
> 2. `pkt_rc_t ym_read_packet(buf, &len, &blk, first_timeout)` using a blocking `int uart_getc(uint32_t timeout_ticks)`.
> 3. `int ym_parse_block0(...)`: pathname NUL, decimal length; a null pathname ends the batch.
> 4. `ym_rc_t ym_receive(ym_t *y)`: request CRC mode with 'C'; ACK good blocks; ACK duplicates (block = expected − 1) **without rewriting flash**; cancel (CAN CAN) on any other block number; NAK the first EOT and commit on the second; abort after 10 consecutive errors. Trim the last block to the file size from block 0.
> 5. `sink_write` / `sink_commit`: erase each sector on first touch, blank-check, program, verify; hold the header in RAM and program it **last**, only after the payload CRC read back from flash matches.
>
> Constraints: no malloc, every wait bounded, a power cut can hit any flash operation. Invariant: after any failure (including power loss) the next boot must reject the slot; after success the slot must equal the file byte for byte.
>
> Example: a 164-byte image → receiver sends `43 06 43 06 06 15 06 43 06` ('C', ACK, 'C', ACK, ACK, NAK, ACK, 'C', ACK). A 20 064-byte image takes 2 erases + 24 block programs + 1 header program = 27 flash operations.
>
> Follow-ups: why NAK the first EOT? Why is the header written last? Erase the whole slot up front or lazily? 128- or 1024-byte blocks on a noisy line?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "13/77 passed", exit 1 (부트로더가 빈 슬롯을 거부하는 쪽 테스트만 통과) — TODO 6곳을 채워 77/77
./solution -v   # 정답 빌드에서 soak / 전원 차단 요약과 부트 로그 출력
```

TODO: A1 `crc16_xmodem`, A2 `ym_read_packet`, A3 `ym_parse_block0`, A4 `ym_receive`, C1 `sink_write`, C2 `sink_commit`. 주어진 것: 플래시 모델(BM-A02 축약판), 헤더·`img_check`(BM-A04 축약판), `sink_begin`(크기 검사 + upfront 정책), `fl_erase/fl_program`(연산 + BSY 시간), 송신기 모델, 채널 고장 주입, 하네스. 스텁 상태에서도 모든 루프는 유한하다 (시뮬레이터에 5천만 이벤트 상한도 있다).

관찰용 파일:

```bash
python3 -I send_ymodem.py                                                      # 독립 python 송신기 → ymodem_fixture.bin
cc -std=c11 -Wall -Wextra -Werror -O2 demo_ymodem.c -o demo && ./demo          # 블록 크기 × BER, 지우기 정책 × erase 시간
```

(demo/practice 는 같은 폴더의 `solution.c` 를 `#include` 한다 — 레슨 폴더에서 빌드.)

## 4. 힌트

<details>
<summary>힌트 1 — A1/A2: CRC 와 "끝까지 읽고 판정"</summary>

MSB-first: `crc ^= byte << 8` 후 8 번 `crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1`. 패킷은 블록 번호 보수가 틀려도 **끝까지 읽은 뒤** 판정한다 — 중간에 그만두면 남은 바이트가 다음 "패킷 시작" 으로 읽힌다. 그래도 남는 찌꺼기는 호출자가 purge 한다.

</details>

<details>
<summary>힌트 2 — A4: 블록 번호는 세 경우뿐</summary>

`expect` 를 `uint8_t` 로 두면 255 → 0 넘어감이 공짜다. `blk == expect` → 새 블록, `blk == expect − 1` → 송신기가 내 ACK 를 못 봤다 → ACK 만 다시, 그 밖 → 둘 사이의 블록 하나가 통째로 사라졌거나 송신기 버그 → 이어 붙일 방법이 없으니 CAN. block 0 중복(expect == 1)이면 송신기는 ACK 다음 'C' 를 기다리므로 'C' 도 다시 보낸다.

</details>

<details>
<summary>힌트 3 — A4: 타임아웃에 무엇을 보내나</summary>

송신기가 무엇을 기다리는지 생각하라. 데이터 단계에서 송신기는 ACK/NAK 를 기다린다 → NAK. block 0 전과 commit 뒤(null block 0 대기)에는 'C' 를 기다린다 → 'C'. 패킷이 깨졌으면(CRC/보수/SHORT/쓰레기) purge 후 NAK. 연속 오류가 10 을 넘으면 CAN CAN — 단, commit 뒤라면 이미지는 이미 완성이므로 OK 로 끝낸다.

</details>

<details>
<summary>힌트 4 — C1: 헤더는 왜 RAM 에?</summary>

헤더가 플래시에 먼저 써지면, 그 뒤 어디서 끊겨도 "헤더는 그럴듯한데 payload 가 반쯤" 인 슬롯이 남는다. payload CRC 가 걸러 주긴 하지만 그건 확률적 방어(2⁻³²)이고, 부트로더가 매 부팅마다 슬롯 전체 CRC 를 계산해야만 안전하다. 헤더를 마지막에 쓰면 **헤더가 있다 = 그 전 쓰기가 다 끝났다** 가 된다. 그리고 헤더가 다 모이자마자 magic·CRC·크기를 검사하면 나쁜 파일을 erase 한 번 없이 거절할 수 있다.

</details>

<details>
<summary>힌트 5 — C1: 패딩과 blank check</summary>

prog_unit 이 8 이라 파일 끝 꼬리는 8 의 배수로 늘려야 한다. 늘린 부분은 0xFF — 지워진 셀에 1 을 쓰면 아무것도 안 바뀐다. blank check 는 `(old & new) == new`: 쓰려는 1 비트가 이미 0 이면 erase 없이는 불가능하다. 같은 데이터를 두 번 쓰는 건 이 검사를 통과한다는 점(연산 횟수로만 보인다)도 기억하라.

</details>

## 5. 풀이

### 5.1 세션 전체 — 누가 무엇을 기다리나

```svg
<svg viewBox="0 0 740 470" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">YMODEM 세션 (오류 1 번 포함) — 이 레슨의 응답 순서</text>
  <text x="90" y="40" fill="currentColor" font-weight="bold">송신기 (PC: sz --ymodem)</text>
  <text x="500" y="40" fill="currentColor" font-weight="bold">수신기 (부트로더)</text>
  <path d="M150 50 L150 460 M560 50 L560 460" stroke="currentColor" stroke-dasharray="4 3"/>
  <path d="M560 66 L150 66" stroke="currentColor"/><text x="300" y="62" fill="currentColor">'C' (3 s 마다, 최대 20 회)</text>
  <path d="M150 92 L560 92" stroke="currentColor"/><text x="200" y="88" fill="currentColor">SOH 00 FF "app.bin\0 40064 …" CRC</text>
  <rect x="570" y="80" width="160" height="22" class="f-mut" stroke="currentColor"/><text x="576" y="95" fill="currentColor">파싱, sink_begin</text>
  <path d="M560 116 L150 116" stroke="currentColor"/><text x="300" y="112" fill="currentColor">ACK, 'C'</text>
  <path d="M150 142 L560 142" stroke="currentColor"/><text x="200" y="138" fill="currentColor">STX 01 FE data[1024] CRC</text>
  <rect x="570" y="130" width="160" height="22" class="f-acc" stroke="currentColor"/><text x="576" y="145" fill="currentColor">erase 섹터 0 + program</text>
  <path d="M560 166 L150 166" stroke="currentColor"/><text x="320" y="162" fill="currentColor">ACK</text>
  <path d="M150 192 L560 192" stroke="currentColor"/><text x="200" y="188" fill="currentColor">STX 02 FD … (비트 플립!)</text>
  <rect x="570" y="180" width="160" height="22" class="f-warn" stroke="currentColor"/><text x="576" y="195" fill="currentColor">BAD_CRC → purge 1 s</text>
  <path d="M560 216 L150 216" stroke="currentColor"/><text x="320" y="212" fill="currentColor">NAK</text>
  <path d="M150 242 L560 242" stroke="currentColor"/><text x="200" y="238" fill="currentColor">STX 02 FD … (재전송)</text>
  <path d="M560 266 L150 266" stroke="currentColor"/><text x="320" y="262" fill="currentColor">ACK</text>
  <text x="300" y="288" fill="currentColor">⋮ 블록 3 … 마지막 (SOH 128, 0x1A 패딩)</text>
  <path d="M150 310 L560 310" stroke="currentColor"/><text x="320" y="306" fill="currentColor">EOT</text>
  <path d="M560 334 L150 334" stroke="currentColor"/><text x="250" y="330" fill="currentColor">NAK (첫 EOT 는 "정말?" 확인)</text>
  <path d="M150 358 L560 358" stroke="currentColor"/><text x="320" y="354" fill="currentColor">EOT</text>
  <rect x="570" y="346" width="160" height="22" class="f-ok" stroke="currentColor"/><text x="576" y="361" fill="currentColor">commit: CRC → 헤더 기록</text>
  <path d="M560 382 L150 382" stroke="currentColor"/><text x="300" y="378" fill="currentColor">ACK, 'C'</text>
  <path d="M150 408 L560 408" stroke="currentColor"/><text x="200" y="404" fill="currentColor">SOH 00 FF 00…00 CRC (null block 0)</text>
  <path d="M560 432 L150 432" stroke="currentColor"/><text x="320" y="428" fill="currentColor">ACK → 리셋, 앱으로</text>
  <text x="10" y="456" fill="currentColor">송신기 ACK 타임아웃 10 s (모델) · 수신기 패킷 대기 10 s / 글자 1 s · 둘 다 재시도 10 회</text>
</svg>
```

핵심은 **"모든 대기에 상한, 모든 오류에 정해진 응답"** 이다. 수신기 쪽 규칙 표:

| 상태 | 받은 것 | 응답 | 다음 |
|---|---|---|---|
| W_B0 (block 0 대기) | 타임아웃, 아직 아무것도 못 받음 | 'C' (20 회 후 NO_SENDER) | W_B0 |
| W_B0 | block 0 OK | 파싱·`sink_begin` → ACK, 'C' | W_DATA |
| W_B0 | blk ≠ 0 | CAN CAN | SEQ |
| W_DATA | blk == expect | 잘라서 `sink_write` → ACK | expect++ |
| W_DATA | blk == expect−1 | ACK (+ expect==1 이면 'C'), 쓰지 않음 | — |
| W_DATA | 그 밖의 blk | CAN CAN | SEQ |
| W_DATA | 첫 EOT | NAK | eot1 = 1 |
| W_DATA | 둘째 EOT | written==size? `sink_commit` → ACK, 'C' | W_FINAL |
| W_DATA | 타임아웃 / 깨진 패킷 | (purge) NAK, 10 회 넘으면 CAN CAN | — |
| W_FINAL | EOT (내 ACK 유실) | ACK, 'C' 다시 | W_FINAL |
| W_FINAL | null block 0 | ACK | **OK** |
| 아무 상태 | CAN CAN | — | CANCELLED |
| 아무 상태 | 플래시 POWER_CUT | 아무것도 안 보냄 | POWER |

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">ym_receive 상태 머신 (오류 경로는 아래 상자)</text>
  <rect x="20" y="60" width="130" height="50" rx="8" class="f-mut" stroke="currentColor"/>
  <text x="30" y="82" fill="currentColor">W_B0</text><text x="30" y="98" fill="currentColor">'C' 반복</text>
  <rect x="300" y="60" width="150" height="50" rx="8" class="f-acc" stroke="currentColor"/>
  <text x="310" y="82" fill="currentColor">W_DATA</text><text x="310" y="98" fill="currentColor">expect, eot1</text>
  <rect x="590" y="60" width="130" height="50" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="600" y="82" fill="currentColor">W_FINAL</text><text x="600" y="98" fill="currentColor">헤더 commit 완료</text>
  <path d="M150 85 L300 85" stroke="currentColor"/><text x="160" y="78" fill="currentColor">block 0 OK / ACK C</text>
  <path d="M450 85 L590 85" stroke="currentColor"/><text x="458" y="78" fill="currentColor">EOT#2 + commit OK</text>
  <path d="M340 60 C 340 30, 410 30, 410 60" fill="none" stroke="currentColor"/>
  <text x="300" y="36" fill="currentColor">blk==expect: write, ACK / ==expect−1: ACK / EOT#1: NAK</text>
  <path d="M655 110 L655 150" stroke="currentColor"/>
  <rect x="590" y="150" width="130" height="34" rx="8" class="f-ok" stroke="currentColor"/>
  <text x="600" y="172" fill="currentColor">YM_OK (null b0)</text>
  <rect x="20" y="210" width="700" height="58" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="30" y="230" fill="currentColor">CAN 보내고 끝: SEQ(번호 어긋남) · TIMEOUT(연속 오류 &gt; 10) · TOO_BIG · BLOCK0 · SHORT(EOT 때 바이트 부족)</text>
  <text x="30" y="248" fill="currentColor">IMAGE(헤더/CRC) · FLASH(blank/verify) | 그냥 끝: CANCELLED · POWER(정전) · NO_SENDER</text>
  <text x="30" y="264" fill="currentColor">어느 실패든 헤더는 0xFF 로 남아 있다 → 다음 부팅 img_check = NO_IMAGE → 다시 복구 모드</text>
  <path d="M375 110 L375 210" stroke="currentColor" stroke-dasharray="4 3"/>
</svg>
```

### 5.2 Part A — 왜 이렇게 짜나

**첫 EOT 를 NAK 하는 이유.** Forsberg 문서의 YMODEM 배치 그림이 EOT → NAK → EOT → ACK 순서다 (문서 확인 권장; 본문엔 이유가 없다). 실무적 해석: EOT 는 CRC 가 없는 1 바이트라 노이즈 한 방에 "가짜 EOT" 가 될 수 있다. 한 번 되묻고 두 번째 EOT 에서만 commit 하면, 가짜 EOT 뒤에는 송신기가 NAK 를 "블록 재전송 요청" 으로 받아 진짜 데이터를 다시 보내고(이 레슨 모델은 데이터 블록을 받으면 eot1 을 지운다) 파일이 잘리지 않는다. 그리고 commit 직전 `written == size` 를 한 번 더 확인한다 — 송신기가 일찍 EOT 를 보내면(Part B "잘린 전송") SHORT.

**중복 vs 동기 상실.** 송신기는 ACK 를 받아야만 다음 블록으로 간다. 그러니 수신기가 볼 수 있는 블록 번호는 "내가 기다리는 것(expect)" 또는 "방금 ACK 한 것(expect−1, ACK 가 사라짐)" 뿐이다. 그 밖이면 프로토콜 가정이 깨진 것 — Forsberg 표현으로 "fatal loss of synchronization" → CAN. 중복을 **다시 쓰면 안 되는** 이유는 Part C 에서 숫자로 보인다: 같은 데이터를 같은 자리에 다시 쓰는 건 AND 의미론상 무해해 보이지만, 흔한 버그는 "다음 오프셋에 쓰기" 다 (mutant M1: 다음 블록이 그 자리에 오면 blank check 실패 → 전송 실패).

**크기로 자르기.** 마지막 블록은 0x1A 로 패딩된다. "끝의 0x1A 를 지운다" 는 진짜 데이터가 0x1A 로 끝나는 바이너리를 망가뜨린다 (테스트: 파일 끝 5 바이트가 0x1A, mutant M2 가 실패). 그래서 이 부트로더는 block 0 의 크기를 **필수**로 요구하고, 첫 블록의 이미지 헤더 `payload_size + 64` 와 교차 확인한다 — 둘이 다르면 플래시 연산 0 번으로 거절 (`lie_size` 테스트).

**purge.** 깨진 패킷 뒤에는 송신기가 아직 나머지 바이트를 보내고 있을 수 있다. 바로 NAK 하면 그 찌꺼기를 다음 패킷 시작으로 읽는다. 선이 1 s 조용해질 때까지 버리고(상한 4096 B) NAK 한다. 비용이 크다: demo (1) 에서 오류 하나당 시간은 패킷 길이보다 이 1 s purge 가 지배한다 — BER 2.5e-6 에서 1K/128 모두 재전송 2.2 회, 시간 10.00 s vs 10.03 s 로 거의 같다.

### 5.3 Part C — 플래시 경로와 "헤더는 마지막"

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">슬롯 배치와 20 064 B 이미지의 플래시 연산 순서 (op 1–27)</text>
  <rect x="10" y="34" width="120" height="40" class="f-mut" stroke="currentColor"/>
  <text x="18" y="52" fill="currentColor">섹터 0–1</text><text x="18" y="67" fill="currentColor">부트로더 32 KB</text>
  <rect x="130" y="34" width="100" height="40" class="f-acc" stroke="currentColor"/>
  <text x="136" y="52" fill="currentColor">섹터 2</text><text x="136" y="67" fill="currentColor">0x08008000</text>
  <rect x="230" y="34" width="100" height="40" class="f-acc" stroke="currentColor"/>
  <text x="236" y="52" fill="currentColor">섹터 3</text><text x="236" y="67" fill="currentColor">0x0800C000</text>
  <rect x="330" y="34" width="400" height="40" class="f-mut" stroke="currentColor"/>
  <text x="338" y="58" fill="currentColor">섹터 4–7: lazy 면 이번 업데이트에선 안 지움</text>
  <rect x="130" y="84" width="16" height="20" class="f-warn" stroke="currentColor"/>
  <text x="152" y="99" fill="currentColor">헤더 64 B: RAM 에 보관 → op 27 에서 기록</text>
  <text x="10" y="134" fill="currentColor">op:</text>
  <rect x="40" y="120" width="22" height="22" class="f-acc" stroke="currentColor"/><text x="45" y="136" fill="currentColor">E</text>
  <rect x="62" y="120" width="352" height="22" class="f-ok" stroke="currentColor"/>
  <text x="70" y="136" fill="currentColor">op 2–17: 블록 1–16 program (섹터 2)</text>
  <rect x="414" y="120" width="22" height="22" class="f-acc" stroke="currentColor"/><text x="419" y="136" fill="currentColor">E</text>
  <rect x="436" y="120" width="220" height="22" class="f-ok" stroke="currentColor"/>
  <text x="442" y="136" fill="currentColor">op 19–26: 블록 17–24</text>
  <rect x="656" y="120" width="34" height="22" class="f-warn" stroke="currentColor"/><text x="660" y="136" fill="currentColor">H</text>
  <text x="10" y="170" fill="currentColor">전원 차단 81 시나리오 (op 1..27 × 찢김 시드 3), 처음 슬롯엔 유효한 옛 이미지 v1:</text>
  <text x="20" y="192" fill="currentColor">• op 1 (섹터 2 erase 가 찢김): 헤더 자리에 섞인 비트 → 거부</text>
  <text x="20" y="212" fill="currentColor">• op 2–26: 헤더 = 0xFF 그대로 → NO_IMAGE (75/75), 부트로더가 바로 복구 모드</text>
  <text x="20" y="232" fill="currentColor">• op 27 (헤더 program 이 찢김): 3 번 모두 거부 — 받아들였다면 내용이 정확해야 한다 (검사됨)</text>
  <text x="20" y="252" fill="currentColor">• 81/81 재전송으로 복구, 부트로더 영역 0 바이트 변경</text>
  <text x="10" y="282" fill="currentColor">대가: 단일 슬롯이라 op 1 부터 옛 v1 도 죽는다 → A/B 슬롯·스왑은 PLAT-M07 / BM-A06</text>
</svg>
```

`sink_write` 한 번이 하는 일 (블록 1, off = 0, n = 1024):

| 단계 | 내용 | 플래시 연산 |
|---|---|---|
| 1 | [0,64) → `s->hdr`. 다 모였으니 `img_hdr_check` + `payload_size == size − 64` | 없음 (불량이면 여기서 IMAGE, erase 0 번) |
| 2 | payload [64,1024) 가 닿는 슬롯 섹터 0 이 아직 안 지워짐 → `fl_erase` | erase 1 (4608 tick = 400 ms 모델) |
| 3 | 960 B → 8 의 배수라 패딩 없음 | — |
| 4 | blank check: 읽어서 `(old & new) == new` | — |
| 5 | `fl_program(0x08008040, 960 B)` | program 1 (45 tick) |
| 6 | read-back → memcmp, `committed = 1024` | — |

설계 결정과 근거:

- **lazy erase** — 필요한 섹터만, 처음 닿을 때. 대안 "upfront" 은 block 0 직후 몰아서 지우는 것. demo (2) 실측(40 KB 이미지, 송신기 타임아웃 10 s 모델):

```text
  policy          erase(ms)  erases  time(s)  max ACK delay(s)  snd timeouts  retx  overruns  rc
  lazy                  400       3     4.88              0.40             0     0         0  OK
  upfront-needed        400       3     4.88              1.20             0     0         0  OK
  upfront-slot          400       6     6.08              2.40             0     0         0  OK
  lazy                 2000       3     9.68              2.00             0     0         0  OK
  upfront-needed       2000       3     9.68              6.00             0     0         0  OK
  upfront-slot         2000       6    16.77             12.00             1     2        69  OK
  lazy                 4000       3    15.68              4.00             0     0         0  OK
  upfront-needed       4000       3    16.77             12.00             1     2        69  OK
  upfront-slot         4000       6    28.77             24.00             2     3       202  OK
```

  lazy 의 최대 ACK 지연은 "섹터 1 개 erase" 로 묶인다. upfront 는 지연이 섹터 수에 비례해 송신기 타임아웃을 넘고(연습 4: 정확히 `SND_TO ≤ 6·t_erase` 에서), 송신기가 block 0 을 재전송하는 동안 수신기는 erase 중이라 64 B FIFO 가 넘친다(133 − 64 = 69 바이트 유실). 그래도 프로토콜이 복구하지만(rc OK) 시간·마모 모두 손해다. upfront 의 장점은 "전송 중엔 플래시 지연이 없다" 는 것뿐 — erase 가 매우 빠른 칩이나, 수신 중 CPU 가 플래시에서 멈추면 안 되는 경우(같은 뱅크 실행)에 고려. 2000/4000 ms 줄은 F4 의 128 KB 섹터급 erase 시간을 흉내 낸 가상 스윕이다 (실제 값은 데이터시트 확인 권장).
- **0xFF 패딩** — 지워진 셀에 1 을 쓰는 건 "변화 없음" 이라 꼬리 바이트가 슬롯 밖 의미를 갖지 않는다 (테스트: 파일 끝 다음 바이트 == 0xFF).
- **blank check 를 따로** — 플래시 컨트롤러가 0→1 프로그램을 거부하는 칩도 있고(STM32 PGSERR 류 — 확인 권장), 조용히 AND 하는 칩도 있다. 드라이버가 먼저 보면 칩과 무관하게 "지우지 않은 곳에 쓰기" 버그를 잡는다 (Part C 단위 테스트: 1 비트 다른 데이터 재기록 → FLASH, `last_frc = NOT_ERASED`).
- **verify** — 0 으로 안 써지는 셀(stuck-1)을 주입하면 블록 3 에서 VERIFY → CAN, 슬롯 무효 (mutant M5 는 이걸 놓쳐서 commit 의 CRC 에서야 IMAGE 로 실패 → 오류 코드가 달라 테스트가 잡는다).
- **commit = 플래시 기준 CRC** — RAM 으로 계산한 CRC 가 아니라 다시 읽은 플래시로 계산한다. RAM 버퍼는 맞았는데 플래시가 틀린 경우가 바로 우리가 걱정하는 경우다.

전원 차단 결과 (solution `-v` 실제 출력):

```text
  soak: 300 runs, ok 300, clean fail 0, bad 0, packet errors recovered 2035
  power-cut: N=27 ops, 81 scenarios, refused 78/78, header blank 75, header-op cut: accepted(correct) 0 / refused 3, recovered by resend 81
```

SSD 와의 대응: FTL 이 맵 업데이트를 "데이터 페이지 → 저널 → 체크포인트 커밋 레코드" 순으로 쓰고, 부팅 때 커밋 레코드가 없는 트랜잭션을 버리는 것과 같은 구조다. 여기서 커밋 레코드 = 이미지 헤더, 원자성 단위 = 8 B program 하나가 아니라 "헤더 64 B 전체 + hdr_crc32" (찢긴 헤더는 hdr CRC 가 거른다).

### 5.4 부트 로그 (Part C 시나리오 실제 출력)

```text
[boot] slot@0x08008000 check: NO_IMAGE
[boot] no valid app -> YMODEM recovery on UART ('C' every 3 s)
[ym] rc=OK file='app_v1.2.3.bin' size=40064 blocks=40 dups=1 naks=8 crc_err=7 short=0 timeouts=0
[ym] flash: erases=3 programs=41 committed=40064 B, elapsed 22588 ms
[boot] reset -> slot check: OK
[boot] slot@0x08008000 check: OK
[boot] app v1.2.3 payload 40000 B crc32=C1530064 -> jump 0x08008040 (BM-A03)
[boot] slot@0x08008000 check: BAD_MAGIC
[boot] no valid app -> YMODEM recovery on UART ('C' every 3 s)
[ym] rc=POWER file='app_v1.3.0.bin' size=50064 blocks=17 dups=0 naks=0 crc_err=0 short=0 timeouts=0
[ym] flash: erases=2 programs=18 committed=17408 B, elapsed 2492 ms
[sim] *** power lost ***
[boot] slot@0x08008000 check: NO_IMAGE
[boot] no valid app -> YMODEM recovery on UART ('C' every 3 s)
[ym] rc=OK file='app_v1.3.0.bin' size=50064 blocks=56 dups=0 naks=1 crc_err=0 short=0 timeouts=0
[ym] flash: erases=4 programs=57 committed=50064 B, elapsed 6202 ms
[boot] reset -> slot check: OK
```

첫 전송은 바이트당 150 ppm 비트 플립 + ACK 하나 유실 선이다: CRC 오류 7 → NAK 7, 여기에 첫 EOT 에 대한 NAK 1 = naks 8. 유실된 ACK 는 송신기가 10 s 타임아웃 뒤 같은 블록을 다시 보내게 했고(중복 1) 그 10 s 와 purge 들이 22.6 s 의 대부분이다. 정전 시나리오는 op 20(블록 18 의 program 중)에서 끊기고 — 블록 17 까지 17 408 B 가 committed — 재부팅하면 헤더가 0xFF 라 NO_IMAGE → 재전송 → OK. 현장 엔지니어가 이 로그로 알 수 있는 것: 선 품질(crc_err), 송신기 쪽 문제(timeouts), 마지막 good 오프셋(committed), 부트로더가 앱으로 점프한 주소. 점프 자체(VTOR, MSP, SysTick 정리)는 BM-A03.

### 5.5 숫자 감각

연습 3 의 식은 시뮬레이터와 tick 단위로 정확히 일치한다 (오류 없는 lazy 전송):

T = 133 (block 0) + Σ_블록 [ (L+5) + 처음 닿는 섹터면 t_erase + t_prog(⌈payload/8⌉·8) ] + 1 + 1 + t_prog(64) + 133

```text
ex3:  65536 B 1K  : 87565 ticks = 7601 ms, 74.8% of 11520 B/s line rate
ex3:  98304 B 1K  : 131213 ticks = 11390 ms, 74.9% of 11520 B/s line rate
ex3:  65536 B 128 : 89868 ticks = 7801 ms, 72.9% of 11520 B/s line rate
ex3:  98304 B 128 : 134668 ticks = 11689 ms, 73.0% of 11520 B/s line rate
```

선 속도의 25 % 를 erase 가 먹는다 (16 KB 마다 400 ms 모델). 송신기 응답 지연(USB-UART 어댑터 지연 등)을 10 ms 넣으면 블록 수가 8 배인 128 B 모드가 급격히 느려진다 — demo (1):

```text
  blk   BER(1/bit)  turn(ms)  time(s)  retx/xfer  eff%   ok
  1K      0.0e+00       0.0     7.60        0.0   74.8  5/5
  128     0.0e+00       0.0     7.80        0.0   72.9  5/5
  1K      1.3e-05       0.0    17.19        8.8   33.1  5/5
  128     1.3e-05       0.0    16.70        8.8   34.1  5/5
  1K      1.3e-04       0.0   101.29       87.6    5.6  3/5
  128     1.3e-04       0.0    83.87       75.0    6.8  5/5
  1K      0.0e+00      10.0     8.28        0.0   68.7  5/5
  128     0.0e+00      10.0    12.95        0.0   43.9  5/5
```

읽는 법: 깨끗한 선 + 지연 있는 링크 → 1K 가 압도적. 아주 나쁜 선(BER 1.3e-4) → 1K 패킷이 거의 매번 깨져 연속 10 회 오류로 2/5 가 중단, 128 은 5/5 성공. 송신기들이 오류가 잦으면 128 로 내려가는 이유다 (lrzsz 의 실제 규칙은 소스 확인 권장).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 77/77 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan   # 77/77, 경고 0
```

버그 검출 정직성 — 정답의 사본에 그럴듯한 버그 7 개를 넣고 하네스가 잡는지 확인했다 (레슨 폴더엔 남기지 않음):

| mutant | 바꾼 것 | 결과 |
|---|---|---|
| M1 | 중복 블록(expect−1)도 새 블록으로 기록 | 71/77 — ACK 유실·이중 전송 테스트 실패 |
| M2 | 크기 대신 끝의 0x1A 를 지워서 자르기 | 74/77 — 0x1A 로 끝나는 파일 |
| M3 | 첫 EOT 에서 바로 commit | 76/77 — 응답 바이트열 불일치 |
| M4 | 섹터를 쓸 때마다 다시 erase | 47/77 |
| M5 | read-back verify 생략 | 76/77 — stuck 셀이 FLASH 대신 IMAGE |
| M6 | 헤더를 payload 와 함께 먼저 기록 | 63/77 — 전원 차단 후 헤더가 0xFF 가 아님 등 |
| M7 | 깨진 패킷 뒤 purge 생략 | 76/77 — 시작 바이트 플립 테스트의 purge 카운터 |

M7 은 결과(파일)가 여전히 맞아서 카운터 단언으로만 잡힌다 — purge 의 효과는 정확성보다 "불필요한 연쇄 오류 방지" 이기 때문이다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| 중복 블록을 무시(무응답) | 송신기는 ACK 를 못 받았다 → 계속 재전송 → 10 회 후 포기 | `else if (blk == (uint8_t)(expect-1)) { ACK; }` |
| 중복 블록을 다시 기록 | 오프셋이 밀려 다음 블록이 이미 쓴 자리 → blank check 실패 / 파일 깨짐 | 쓰지 말고 ACK 만 |
| `int expect` 로 비교 | 블록 256 에서 `blk(0) != 256` → 동기 상실로 오판 | `uint8_t expect`, `(uint8_t)(expect - 1)` |
| 끝의 0x1A 제거로 크기 결정 | 바이너리 끝의 진짜 0x1A 손실 | block 0 크기 필수 + 헤더 크기 교차 확인 |
| 헤더(또는 "valid" 플래그)를 먼저 기록 | 정전 시 그럴듯한 헤더 + 반쪽 payload | 헤더는 RAM, payload CRC 확인 후 마지막에 |
| 커밋 CRC 를 RAM 버퍼로 계산 | 플래시가 틀린 경우를 못 잡음 | 플래시를 다시 읽어 CRC |
| 타임아웃 없는 `while(!RXNE)` | 송신기가 죽으면 부트로더가 영원히 멈춤 (워치독이 리셋 → 다시 멈춤) | 모든 대기에 tick 상한, 오류 10 회 → CAN |
| 깨진 패킷 직후 바로 NAK | 남은 바이트를 다음 패킷 시작으로 읽어 연쇄 오류 | 1 s 조용해질 때까지 purge 후 NAK |
| CAN 하나에 중단 | 노이즈 바이트 0x18 하나로 업데이트 실패 | CAN 2 개 연속만 중단 |
| 슬롯 전체를 먼저 지우기 | 큰 섹터면 첫 ACK 가 송신기 타임아웃 초과 + 불필요한 마모 | lazy erase (또는 erase 중 송신기 대기 시간 확인) |
| 크기 검사 `off + n > size` | off + n 이 32 비트에서 넘칠 수 있다 | `off > size \|\| n > size - off` |
| 실패 뒤 "옛 앱으로 점프" | 단일 슬롯에선 옛 앱이 이미 지워졌다 | 헤더 검사 결과로만 결정 (img_check) |

## 8. 면접 Q&A

<details>
<summary>Q1. YMODEM 이 XMODEM 과 뭐가 다르죠? 부트로더에서 왜 YMODEM 을 쓰나요?</summary>

XMODEM 은 128 B 블록 + (원래) 1 바이트 체크섬, 파일 이름·크기가 없다. XMODEM-CRC 가 CRC-16 을, XMODEM-1K 가 1024 B 블록을 더했고, YMODEM 은 거기에 **block 0 (이름, 10 진 크기, mtime, mode)** 과 배치(null block 0 으로 끝)를 더한 것이다 (Forsberg, 문서 확인 권장). 부트로더 입장에서 결정적인 건 크기: 마지막 블록의 0x1A 패딩을 정확히 잘라 낼 수 있고, 슬롯보다 큰 파일을 첫 패킷에서 거절할 수 있다. 그리고 표준 터미널 도구로 보낼 수 있어 현장 지원이 쉽다. 단점: 반이중 stop-and-wait 라 링크 지연에 약하고, 무결성은 블록 CRC-16 뿐 — 이미지 전체 무결성은 이미지 헤더의 CRC-32/SHA-256(BM-A04), 진위는 서명(SEC)이 맡는다.

</details>

<details>
<summary>Q2. ACK 가 사라지면 무슨 일이 생기고, 수신기는 어떻게 해야 하나요?</summary>

송신기는 ACK 없이는 다음으로 가지 않으므로 타임아웃(또는 수신기의 타임아웃 NAK) 뒤 같은 블록을 다시 보낸다. 수신기 입장에선 blk == expect − 1. 해야 할 일: **ACK 를 다시 보내고 아무것도 쓰지 않는다.** 무시하면 송신기가 재시도를 소진하고, 다시 쓰면 오프셋이 밀린다. block 0 의 ACK 가 사라진 경우엔 송신기가 ACK 다음 'C' 를 기다리므로 'C' 도 다시 보내야 한다 (테스트 `drop_r2s_at = 1`). 꼬리: "두 블록 차이 나면?" → 프로토콜상 불가능 = 동기 상실 → CAN.

</details>

<details>
<summary>Q3. 업데이트 도중 전원이 나가면 어떻게 되나요? 어떻게 증명했죠?</summary>

설계: payload 는 블록마다 기록·검증하고, 슬롯의 "유효" 표시인 헤더는 RAM 에 들고 있다가 두 번째 EOT 에서 플래시에서 다시 읽은 payload CRC 가 맞을 때만 마지막으로 쓴다. 그래서 헤더 기록 전 어느 연산에서 끊겨도 헤더 자리는 0xFF(NO_IMAGE) 거나, 첫 erase 가 찢긴 경우엔 섞인 비트(magic/CRC 불량)다. 증명: 20 064 B 이미지의 27 개 플래시 연산 각각에서 `cut_at` 으로 끊고, 찢김 시드 3 개 → 81 시나리오. 재부팅 판정: 78/78 거부, 그중 75 개는 헤더 0xFF, 헤더 기록 자체가 찢긴 3 개도 거부(받아들였다면 내용이 정확한지까지 검사), 81/81 재전송으로 복구. 정직한 한계: 단일 슬롯이라 첫 erase 순간 옛 앱도 사라진다 — 업데이트 중 정전이면 "부트로더에 머무르며 재전송을 기다리는" 상태가 최선이다. 항상 실행 가능한 앱을 원하면 A/B 슬롯이나 스왑(BM-A06, PLAT-M07).

</details>

<details>
<summary>Q4. 슬롯을 처음에 다 지우나요, 블록마다 지우나요?</summary>

처음 닿는 섹터만 그때 지운다(lazy). 이유: ACK 지연이 "섹터 하나 erase" 로 묶인다. upfront 는 block 0 의 ACK 가 섹터 수 × erase 시간만큼 늦어지고, 그게 송신기 타임아웃을 넘으면 송신기가 재전송하는 동안 수신기는 erase 중이라 RX FIFO 가 넘친다 — demo: 섹터 erase 2 s × 6 = 12 s > 10 s → 송신기 타임아웃 1, 오버런 69 B (프로토콜이 결국 복구하지만 느리다). 연습 4 가 경계를 정확히 보였다: `SND_TO ≤ 6·t_erase` 이면 재전송. 그리고 lazy 는 이미지가 덮는 섹터만 마모시킨다(40 KB → 3 섹터 vs 6). upfront 가 나은 경우: 플래시 erase 중 CPU 가 멈추는 구조에서 "전송 중엔 erase 하지 않기" 가 필요할 때 — 단 송신기 타임아웃 예산을 계산해야 한다.

</details>

<details>
<summary>Q5. 플래시를 지우는 동안 UART 로 들어오는 바이트는 어떻게 되나요? (YMODEM-G 는?)</summary>

STM32F4 USART 는 수신 버퍼가 DR 1 바이트뿐이다. 같은 뱅크의 플래시를 erase 하면 그 뱅크에서 코드를 가져오는 CPU 는 멈추고(ISR 이 플래시에 있으면 ISR 도), 다음 바이트부터 ORE. 대책: ① 프로토콜이 stop-and-wait 이므로 정상 흐름에선 erase 중 아무도 안 보낸다 — 문제는 송신기 재전송과 쓰레기 뿐, ② RX 는 DMA 순환 버퍼(COMM-M11)나 RAM 에서 도는 ISR, ③ erase 함수를 RAM 에서 실행 / 듀얼 뱅크면 다른 뱅크에서 실행. YMODEM-G 는 ACK 없이 스트리밍하고 오류 시 전송 전체를 중단하는 변형이라 수신기가 흐름을 늦출 방법이 없다: 이 모델 수치로 erase 400 ms 동안 4608 바이트가 들어오는데 FIFO 는 64 B → 확정 실패. 부트로더엔 쓰지 않는다.

</details>

<details>
<summary>Q6. CRC-16 이 충분한가요? 이미지 전체엔 왜 CRC-32 를 또 쓰죠?</summary>

블록 CRC-16 은 **선 위의 오류**를 위한 것이다: g = x¹⁶+x¹²+x⁵+1 은 g(0)=1 이라 길이 ≤ 16 버스트를 전부 잡고(연습 1: 2억 6천만 개 위치·패턴 전수, 놓친 것 0), 길이 17 버스트는 2¹⁵ 패턴 중 1 개만 놓치며, x 의 위수가 32767 이라 32767 비트 안의 2 비트 오류도 전부 잡는다 — 1029 B 패킷(8208 비트)엔 충분. 하지만 ① 무작위 오류에선 블록당 2⁻¹⁶ 확률로 통과하고, ② 수신기 버그(오프셋 실수), 플래시 쓰기 오류, 송신 파일 자체의 손상은 못 잡는다. 그래서 끝에서 끝(end-to-end)으로 이미지 CRC-32 를 플래시에서 다시 읽어 확인한다. 진위(누가 만들었나)는 CRC 로는 불가 — 서명 검증(SEC).

</details>

<details>
<summary>Q7. 수신기의 모든 대기에 타임아웃을 두는 이유와 값은? 워치독은?</summary>

부트로더가 멈추면 장비는 벽돌이다. 값: 패킷 시작 대기 10 s, 글자 사이 1 s, 'C' 3 s 간격, 연속 오류 10 회 (Forsberg 의 수치를 이 모델의 값으로 채택, 문서 확인 권장). 송신기가 죽으면 이 레슨에서 정확히 NAK 10 회 + 타임아웃 11 회 뒤 CAN CAN 으로 끝난다. 워치독(BM-M10): IWDG 는 끌 수 없으니 부트로더도 kick 해야 하는데, kick 위치는 "대기 루프" 가 아니라 "진행이 있을 때"(블록 수신, 'C' 재전송) 여야 한다. 그리고 최장 erase 시간 < IWDG 최소 타임아웃이 되게 설정하거나 erase 를 쪼갠다.

</details>

<details>
<summary>Q8. (staff) 이 부트로더를 양산 제품용으로 바꾼다면 무엇을 바꾸겠습니까?</summary>

① **A/B 슬롯** — 수신은 비활성 슬롯에만, 헤더 commit 후 "다음 부팅 시험(test)" 표시, 앱이 자기 확인(confirm)을 못 하면 되돌리기 (MCUboot test/confirm 개념, BM-A06, PLAT-M07). 단일 슬롯의 "정전 = 앱 없음" 이 사라진다. ② **서명 검증**(ECDSA/Ed25519) — CRC 는 진위를 보장하지 않는다 (SEC). ③ **anti-rollback** — img_version 을 OTP/eFuse/보호된 카운터와 비교 (BM-A04). ④ 프로토콜 — 공장/현장 UART 라면 YMODEM 유지, 하지만 크기·해시를 먼저 보내는 자체 프레이밍(COBS + CRC, 재전송 창)으로 바꾸면 응답 지연에 강해진다 (COMM-A02 의 슬라이딩 윈도). ⑤ 진입 조건 — 부트로더 진입 핀/매직 워드(.noinit), 디버그 포트 잠금(RDP). ⑥ 로그 — 이 레슨의 `[ym]` 줄을 백업 레지스터나 로그 섹터에 남겨 현장 회수(BM-A09).

</details>

<details>
<summary>Q9. (staff) 이 설계가 틀리는 경우는 어디인가요?</summary>

① **헤더 commit 이 8 B program 여러 개** — 이 모델은 헤더 64 B 를 한 번의 program 호출로 보지만 실제론 8 개 단위가 순서대로 쓰인다. 중간에 끊기면 일부만 쓰인 헤더가 남는데, hdr_crc32 가 맨 끝(60–63) 에 있어 마지막 단위가 쓰여야만 통과한다 — 그래서 안전하다. 순서를 바꾸면(CRC 를 앞에) 이 논리가 깨진다. ② **약하게 지워진 셀** — 찢긴 erase 가 0xFF 로 "읽히지만" 전하가 불안정한 상태(NOR 의 partially erased cell, 확인 권장)면 blank check 를 통과하고 나중에 비트가 바뀔 수 있다. 대책: 정전 뒤엔 항상 다시 erase (이 레슨은 세션마다 erase 하므로 OK), 부팅 때마다 전체 CRC 검사(이 레슨은 함). ③ **같은 데이터 재기록** 은 blank check 로 못 잡는다 — 테스트는 연산 횟수로 잡는다. ④ 송신기가 블록 크기를 바꿀 때 오프셋 정렬: 128 과 1024 는 모두 8 의 배수라 OK 지만 prog_unit 이 256 B 인 칩(일부 신형 MCU, 확인 권장)이면 RAM 에 모아 단위로 써야 한다. ⑤ 시뮬레이터는 바이트당 독립 오류 — 실제 선 잡음은 버스트이고 USB-UART 는 패킷 단위로 몰아서 전달한다.

</details>

## 9. 추가 연습

### 연습 1 — CRC-16 표 방식과 오류 검출 증명

256 항목 표로 `crc16_table()` 을 만들어 3000 개 무작위 버퍼에서 비트 방식과 같은지 확인하라. 그리고 STX 패킷(검사 범위 1026 B = 8208 비트)에서 길이 ≤ 16 인 모든 버스트가 검출됨을, 길이 17 버스트가 몇 개 빠지는지를, 2 비트 오류가 언제 빠지는지를 계산으로 보여라.

<details>
<summary>답 보기</summary>

CRC 는 init 0 이고 xorout 0 이라 **선형**이다: 오류 패턴 e 가 안 잡힘 ⇔ e(x) ≡ 0 (mod g). 길이 ≤ 16 버스트 = x^s·b(x), deg b ≤ 15, b(0) = 1. b 의 나머지는 b 자신(≠ 0)이고, g(0) = 1 이라 x 는 mod g 에서 가역 → 모든 s 에서 ≠ 0. 코드는 b 32768 개 × 위치 8193 개를 LFSR 곱셈(`mulx`)으로 직접 확인한다. 길이 17: b = 1 + … + x¹⁶, b mod g = b ⊕ g → 0 이 되는 건 b == g 하나. 2 비트: x^i + x^j ≡ 0 ⇔ x^(j−i) ≡ 1 → x 의 위수. 실제 출력:

```text
ex1: bursts<=16 checked 268468224, missed 0; 17-bit bursts missed 1/32768; order of x mod g = 32767 (> 8208 bits)
```

위수 32767 = 2¹⁵ − 1 (g = (x+1)·p(x), p 원시 다항식이라고 알려진 성질과 일치 — 문헌 확인 권장). 표 방식은 `tab[i] = crc16_xmodem(&i, 1)`, `c = (c << 8) ^ tab[(c >> 8) ^ byte]` — 부트로더에선 512 B 표 vs 비트 루프 속도의 트레이드오프.

</details>

### 연습 2 — 독립 송신기(python)의 스트림을 재생

`send_ymodem.py` 는 C 와 따로 짠 구현(`binascii.crc_hqx`, `zlib.crc32`, `struct`)으로 이미지 + 송신 바이트열 + 기대 응답을 `ymodem_fixture.bin` 에 쓴다. 그 스트림을 응답과 무관하게 "선" 에 흘려(raw 모드, FIFO = 스트림 전체) C 수신기가 같은 파일과 같은 응답을 내는지 확인하라.

<details>
<summary>답 보기</summary>

```text
$ python3 -I send_ymodem.py
crc16('123456789') = 0x31C3
image 2564 B (payload 2500 B, crc32 0x64F54C48), 7 data blocks, stream 2991 B, responses 43 06 43 06 06 06 06 06 06 06 15 06 43 06
block0 crc16 = 0xA70F, wrote ymodem_fixture.bin (5585 B)

ex2: python fixture image 2564 B, stream 2991 B, 14 responses matched, rc=OK
```

데이터 블록 7 개 = STX 2 개(2048 B) + SOH 4 개(512 B) + 마지막 SOH 1 개(4 B + 0x1A 패딩 124). payload 의 마지막 3 바이트가 진짜 0x1A 인데 슬롯엔 남고, 그다음 바이트는 0xFF — 크기로 자른 증거. python block 0 은 mtime 을 다른 값(`1700000000` 의 8 진)으로 쓰므로 파서가 "크기 뒤 공백 + 아무 필드" 를 견디는지도 같이 본다. 두 구현이 같은 오해를 할 확률을 줄이는 것이 교차 검증의 목적이다 (sz/lsz 는 맥과 VM 모두 설치돼 있지 않아 실제 lrzsz 와의 교차는 하지 못했다).

</details>

### 연습 3 — 전송 시간을 tick 단위로 정확히 예측

오류 없는 lazy 전송이 끝나는 시각(null block 0 에 ACK 한 tick)을 식으로 쓰고 시뮬레이터와 18 개 케이스(9 크기 × 1K/128)에서 정확히 같은지 확인하라.

<details>
<summary>답 보기</summary>

5.5 의 식. 바이트 1 개 = 1 tick 이고 송신기는 응답 다음 tick 부터 보내므로, 블록 하나 = 패킷 길이(L+5) + (처음 닿는 섹터면 erase) + program(헤더를 뺀 payload 를 8 의 배수로 올린 길이 × 47/1024 올림). 끝에는 EOT 2 개, 헤더 64 B program(3 tick), null block 133. 18/18 일치, 예: 64 KB 1K 블록 = 87 565 tick = 7.60 s (선 속도의 74.8 %). 여기서 얻는 감각: 오류가 없을 때 시간의 약 25 % 가 erase 다 — 이미지가 슬롯보다 작으면 lazy 가 그만큼 아낀다.

</details>

### 연습 4 — upfront erase 와 송신기 타임아웃의 경계, lazy 의 최대 지연

upfront-slot 정책에서 block 0 의 ACK 는 6·t_erase 늦어진다. 송신기 ACK 타임아웃 SND_TO(10 s = 115 200 tick)와 비교해 재전송이 생기는 정확한 조건을 구하고, t_erase = 경계−1 과 경계에서 확인하라. lazy 정책의 최대 ACK 지연도 식으로 구하라.

<details>
<summary>답 보기</summary>

송신기 마감 = block 0 마지막 바이트 시각 + SND_TO, 수신기 ACK = 같은 시각 + 6·t_erase. 시뮬레이터는 BSY 대기 중 마감이 ≤ 목표 시각인 사건을 먼저 처리하므로 재전송 ⇔ SND_TO ≤ 6·t_erase ⇔ t_erase ≥ 19 200 tick (1.667 s). 19 199 → 송신기 타임아웃 0, 최대 지연 = 6 × 19 199. 19 200 →

```text
ex4: erase 19200 ticks x6 >= SND_TO: sender timeouts 1, dup 1, overruns 0, rc=OK
```

(이 경계에선 재전송이 erase 가 끝나는 순간 시작해 오버런 0, 중복 1.) lazy 의 최대 지연 = t_erase + t_prog(1024) = 4608 + 47 tick (블록이 새 섹터의 첫 블록일 때). 일반화: 송신기 타임아웃 > (한 번에 지우는 섹터 수) × (최악 erase 시간, 데이터시트의 max — typ 아님) 이어야 한다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
python3 -I send_ymodem.py     # (fixture 가 이미 있으면 생략 가능 — 같은 내용으로 다시 만든다)
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice   # 14/14 passed  (-v: 수치 출력)
```

## 10. 핵심 정리

- 패킷 = SOH/STX + blk + 255−blk + 128/1024 B + CRC-16/XMODEM(0x1021, init 0, "123456789" → 0x31C3). block 0 = 이름 NUL 10 진 크기; null 이름 = 배치 끝. 'C' 로 CRC 모드 시작.
- 블록 번호: expect → 기록 + ACK, expect−1 → ACK 만(다시 쓰지 않음, block 0 이면 'C' 도), 그 밖 → CAN CAN. `uint8_t` 로 256 넘어감 처리.
- EOT → NAK → EOT → (written == size 확인, commit) → ACK 'C' → null block 0 → ACK. 크기로 0x1A 패딩을 자른다.
- 모든 대기에 상한: 패킷 10 s, 글자 1 s, 'C' 3 s × 20, 연속 오류 10 → CAN. 깨진 패킷 뒤엔 1 s purge 후 NAK. CAN 은 2 개 연속만.
- 플래시: 처음 닿는 섹터만 erase(lazy, ACK 지연 = 섹터 1 개) → 0xFF 패딩 → blank check `(old & new) == new` → program → read-back verify.
- 헤더(유효 표시)는 RAM 에 두고 **마지막에** 기록, 그 전에 플래시에서 다시 읽은 payload CRC-32 확인. 27 연산 × 3 시드 전원 차단 → 78/78 거부, 81/81 재전송 복구.
- 단일 슬롯의 한계: 첫 erase 부터 옛 앱도 없다 → A/B·스왑(BM-A06, PLAT-M07), 진위는 서명(SEC).

## 참고자료

- Chuck Forsberg, *XMODEM/YMODEM PROTOCOL REFERENCE* (1988) — §3.2 1K 블록(STX), §4 YMODEM 배치(block 0 의 pathname / 10 진 length / 8 진 mod date·mode, null pathname 으로 종료, Figure 3 의 EOT·NAK·EOT·ACK), §6.3 타임아웃(10 s, 글자 1 s)·재시도 10 회·"fatal loss of synchronization", CAN 2 개 연속(YAM). 확인한 사본: <https://techheap.packetizer.com/communications/modems/xmodem-ymodem_reference.html>, 다른 사본 <https://android.googlesource.com/platform/external/syslinux/+/76d05dc695b06c4e987bb8078f78032441e1430c/com32/libupload/ymodem.txt>, 개요 <https://en.wikipedia.org/wiki/YMODEM>
- lrzsz (`sz --ymodem`, `rz`) — 실제 송신기 동작(블록 크기 선택, CAN 개수, 타임아웃)은 소스 확인 권장. 이 맥과 Lima VM 모두 sz/lsz 미설치라 교차 실험은 python 독립 구현으로 대신함.
- ST AN3965 *STM32F40x/STM32F41x in-application programming using the USART* (YMODEM IAP 예제, 문서 번호·내용 확인 권장), RM0090 3장 Flash (섹터 배치 16/16/16/16/64/128 KB, PSIZE 프로그램 단위, PGSERR 등 — 확인 권장), STM32F405/407 데이터시트 Flash 특성표 (섹터 erase / word program 시간 typ·max — 이 레슨의 400 ms / 1 KB 4.1 ms 는 모델 가정, 확인 권장)
- Python `binascii.crc_hqx` (CRC-CCITT, init 인자 0 → XMODEM), `zlib.crc32` — <https://docs.python.org/3/library/binascii.html>
- 관련 레슨: BM-A02 (플래시 드라이버·모델 원본), BM-A04 (이미지 헤더·CRC/SHA-256 원본), BM-A03 (부트로더 → 앱 점프), BM-A06 (전원 차단 내성 스왑 업데이트), BM-A09 (현장 로그/덤프), BM-M10 (워치독), BM-B12 / COMM-B12 (UART, CRC), COMM-M11 (UART DMA 순환 수신), COMM-M14 (stop-and-wait ARQ), COMM-A02 (슬라이딩 윈도), COMM-A06 (SPI NOR program/erase), ALGO-M01/M02 (CRC 비트/표 방식). 다음 단계: PLAT-M07 (A/B 슬롯 결정), SEC-* (서명 검증, 시큐어 부트)
