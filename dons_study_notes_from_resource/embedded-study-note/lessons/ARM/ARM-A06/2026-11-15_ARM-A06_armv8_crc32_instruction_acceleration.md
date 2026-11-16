---
id: ARM-A06
time: 110분
updated: 2026-11-15
---
# ARM-A06 ARMv8 CRC32 명령으로 CRC 가속

## 학습 목표

- `CRC32B/H/W/X`(CRC-32, IEEE)와 `CRC32CB/CH/CW/CX`(CRC-32C, Castagnoli)가 계산하는 값을 정확히 말한다. 명령은 조건화(`~`)를 하지 않으므로, zlib 모양 API 로 감싸고 check 값 `0xCBF43926` / `0xE3069283` 을 코드로 확인한다.
- intrinsic(`arm_acle.h`) 판과 손 asm(`crc32cx`) 판을 쓴다. 정렬 머리(1·2·4 B) → 8 B 본문 → 꼬리(4·2·1 B) 구조로 만들고, 길이 × 정렬 전수와 페이지 끝 경계로 검증한다.
- `crc32cx` 의 **지연 3 사이클 / 처리량 1 개/사이클**을 직접 재고(Little 의 법칙), 버퍼를 독립 스트림 3개로 나눠 지연을 숨긴다.
- 세 결과를 GF(2) 시프트로 합친다. `PMULL`(`vmull_p64`)로 `c·x^(8n−33)` 을 곱하고 `crc32cx(0, 곱)` 으로 mod P 를 얻는다. 여기서 **왜 33인지**를 유도한다. combine·scatter-gather 에도 같은 연산을 쓴다.
- 1 KiB~1 MiB 처리량을 비트 단위·테이블·slicing-8(ALGO 트랙)·1-way·3-way·zlib 과 비교한다. 이론 한계(8 B/사이클)와 실측을 맞대어 보고, 펌웨어 쪽(STM32 CRC 유닛, Cortex-M 에는 명령이 없음)과 연결한다.

## 1. 문제

CRC-32C 는 NVMe/TCP·iSCSI 다이제스트, ext4·Btrfs 메타데이터 체크섬, SCTP 에 쓰인다. CRC-32 는 이더넷 FCS, zlib/gzip/PNG 에 쓰인다. 둘 다 SSD 컨트롤러(Cortex-R/A)와 SoC 펌웨어에서 **바이트당 비용**이 곧 처리량 예산이다. 면접은 "CRC 명령이 있는데 왜 3-way 로 나눠?", "세 결과를 어떻게 합쳐?", "x^33 은 어디서 나와?", "Cortex-M 에서는?" 순서로 들어온다. 이 레슨은 ALGO-M01(비트 단위)과 ALGO-M02(테이블·slicing·combine) 위에, **하드웨어 명령과 그 지연을 숨기는 법**을 얹는다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">crc32cx: 지연 3 사이클, 처리량 1 개/사이클 — 사슬 1개 vs 독립 사슬 3개 (모델)</text> <text x="130" y="46" fill="currentColor" font-size="12" text-anchor="middle">0</text> <line x1="130" y1="52" x2="130" y2="232" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/> <text x="170" y="46" fill="currentColor" font-size="12" text-anchor="middle">1</text> <line x1="170" y1="52" x2="170" y2="232" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/> <text x="210" y="46" fill="currentColor" font-size="12" text-anchor="middle">2</text> <line x1="210" y1="52" x2="210" y2="232" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/>
<text x="250" y="46" fill="currentColor" font-size="12" text-anchor="middle">3</text> <line x1="250" y1="52" x2="250" y2="232" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/> <text x="290" y="46" fill="currentColor" font-size="12" text-anchor="middle">4</text> <line x1="290" y1="52" x2="290" y2="232" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/> <text x="330" y="46" fill="currentColor" font-size="12" text-anchor="middle">5</text> <line x1="330" y1="52" x2="330" y2="232" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/> <text x="370" y="46" fill="currentColor" font-size="12" text-anchor="middle">6</text>
<line x1="370" y1="52" x2="370" y2="232" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/> <text x="410" y="46" fill="currentColor" font-size="12" text-anchor="middle">7</text> <line x1="410" y1="52" x2="410" y2="232" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/> <text x="450" y="46" fill="currentColor" font-size="12" text-anchor="middle">8</text> <line x1="450" y1="52" x2="450" y2="232" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/> <text x="490" y="46" fill="currentColor" font-size="12" text-anchor="middle">9</text> <line x1="490" y1="52" x2="490" y2="232" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/>
<text x="530" y="46" fill="currentColor" font-size="12" text-anchor="middle">10</text> <line x1="530" y1="52" x2="530" y2="232" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/> <text x="570" y="46" fill="currentColor" font-size="12" text-anchor="middle">11</text> <line x1="570" y1="52" x2="570" y2="232" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/> <text x="610" y="46" fill="currentColor" font-size="12" text-anchor="middle">12</text> <line x1="610" y1="52" x2="610" y2="232" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/> <text x="650" y="46" fill="currentColor" font-size="12" text-anchor="middle">13</text>
<line x1="650" y1="52" x2="650" y2="232" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/> <text x="690" y="46" fill="currentColor" font-size="12" text-anchor="middle">14</text> <line x1="690" y1="52" x2="690" y2="232" stroke="currentColor" stroke-dasharray="2 4" stroke-width="0.5"/> <text x="10" y="46" fill="currentColor" font-size="12">사이클 →</text> <text x="10" y="77" fill="currentColor">사슬 1개</text> <rect x="131" y="60" width="118" height="24" class="f-mut" stroke="currentColor"/><text x="190.0" y="77" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">c←crc(c,d0)</text>
<rect x="251" y="60" width="118" height="24" class="f-mut" stroke="currentColor"/><text x="310.0" y="77" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">c←crc(c,d1)</text> <rect x="371" y="60" width="118" height="24" class="f-mut" stroke="currentColor"/><text x="430.0" y="77" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">c←crc(c,d2)</text> <rect x="491" y="60" width="118" height="24" class="f-mut" stroke="currentColor"/><text x="550.0" y="77" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">c←crc(c,d3)</text>
<text x="10" y="132" fill="currentColor">사슬 A</text><text x="10" y="167" fill="currentColor">사슬 B</text><text x="10" y="202" fill="currentColor">사슬 C</text> <rect x="131" y="115" width="118" height="24" class="f-acc" stroke="currentColor"/><text x="190.0" y="132" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">a0</text> <rect x="171" y="150" width="118" height="24" class="f-ok" stroke="currentColor"/><text x="230.0" y="167" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">b0</text>
<rect x="211" y="185" width="118" height="24" class="f-warn" stroke="currentColor"/><text x="270.0" y="202" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">c0</text> <rect x="251" y="115" width="118" height="24" class="f-acc" stroke="currentColor"/><text x="310.0" y="132" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">a1</text> <rect x="291" y="150" width="118" height="24" class="f-ok" stroke="currentColor"/><text x="350.0" y="167" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">b1</text>
<rect x="331" y="185" width="118" height="24" class="f-warn" stroke="currentColor"/><text x="390.0" y="202" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">c1</text> <rect x="371" y="115" width="118" height="24" class="f-acc" stroke="currentColor"/><text x="430.0" y="132" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">a2</text> <rect x="411" y="150" width="118" height="24" class="f-ok" stroke="currentColor"/><text x="470.0" y="167" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">b2</text>
<rect x="451" y="185" width="118" height="24" class="f-warn" stroke="currentColor"/><text x="510.0" y="202" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">c2</text> <rect x="491" y="115" width="118" height="24" class="f-acc" stroke="currentColor"/><text x="550.0" y="132" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">a3</text> <rect x="531" y="150" width="118" height="24" class="f-ok" stroke="currentColor"/><text x="590.0" y="167" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">b3</text>
<rect x="571" y="185" width="118" height="24" class="f-warn" stroke="currentColor"/><text x="630.0" y="202" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">c3</text> <text x="10" y="258" fill="currentColor">사슬 1개: 12 사이클에 명령 4개 → 8 B × 4 / 12 = 2.67 B/사이클 (다음 명령이 앞 결과를 기다린다)</text> <text x="10" y="280" fill="currentColor">사슬 3개: 매 사이클 새 명령 1개 → 8 B/사이클. 4개 이상은 이득 없음 (CRC 유닛 1개 가정)</text> <text x="10" y="302" fill="currentColor">실측 demo_lat (min 기반): 지연 2.89–2.94, 사슬 2개 1.31–1.32, 3개 0.87–0.88, 8개 0.91–0.95 사이클/명령</text> <text x="10" y="322" fill="currentColor" font-size="12">사이클 = add 의존 사슬 1개를 1 사이클로 본 추정. Apple 이 공개한 수치가 아니다.</text>
</svg>
```

| | CRC-32 (ISO-HDLC) | CRC-32C (iSCSI/Castagnoli) |
|---|---|---|
| 다항식 normal / 반사 | 0x04C11DB7 / **0xEDB88320** | 0x1EDC6F41 / **0x82F63B78** |
| AArch64 명령 | `crc32b/h/w/x` | `crc32cb/ch/cw/cx` |
| ACLE intrinsic | `__crc32b/h/w/d` | `__crc32cb/ch/cw/cd` |
| check("123456789") | 0xCBF43926 (코드로 확인) | 0xE3069283 (코드로 확인) |
| residue (조건화 후) | 0x2144DF1C (ALGO-M02) | 0x48674BC7 (연습 4 가 확인) |
| 쓰는 곳 | 이더넷, zlib, PNG, gzip | iSCSI, NVMe/TCP, ext4, Btrfs, SCTP |
| x86 | 명령 없음 (PCLMULQDQ 로) | SSE4.2 `crc32` |

### Part A — 1-way: intrinsic 과 asm

- `uint32_t crc32_raw_hw(uint32_t c, const uint8_t *p, size_t n)` / `crc32c_raw_hw(...)` — 조건화 없는 레지스터 갱신. p 를 8 B 정렬로 맞추는 머리(1·2·4 B 명령), 8 B 본문, 꼬리(4·2·1 B). **p+n 밖은 읽지 않는다.**
- `crc32_arm(crc, p, n)` / `crc32c_arm(crc, p, n)` — zlib 모양(처음 crc = 0, 이어 부르면 스트리밍). `~raw(~crc)`.
- `crc32c_asm(crc, p, n)` (`solution.S`) — 같은 구조를 손으로 쓴다. `tbz` 로 주소·길이 비트를 검사하고, `ldr x3, [x1], #8` 다음에 `crc32cx` 를 둔다.
- 검증: check 값, zlib `crc32()`, 비트 단위·테이블·slicing-8 기준(`shared_ref.c`, ALGO-M01/M02 축약판), 길이 0..64 × 정렬 0..15 전수, 랜덤 2000개, 스트리밍 조각, 페이지 끝.

### Part B — 3-way 인터리브와 GF(2) 시프트

- `crc_mulk(c, k, castagnoli)` = `crc32(c)x(0, pmull(c, k))` = c·k·x^33 mod P.
- `crc_shift(c, n, castagnoli)` = c·x^(8n) mod P ("0 바이트 n 개 먹이기"). n < 5 이면 8n−33 < 0 이라서 명령으로 0 을 직접 먹인다.
- `crc32c_combine_hw(crc1, crc2, len2)`, `crc32_combine_hw(...)` — zlib `crc32_combine()` 과 대조한다.
- `crc32c_3way(crc, p, n)` — 3·B 덩어리(B = 1024, 그다음 128)를 세 체인으로 동시에 처리하고, `c = c0·x^(16B) ⊕ c1·x^(8B) ⊕ c2` 로 합친다. 길이 0..4096 **전수**와 256 KiB 까지 랜덤으로 1-way 와 대조한다.
- 버그 6종(`shared_bugs.c`)을 강한 테스트 묶음이 모두 잡는지 확인한다. 약한 묶음(≤ 300 B)이 3-way 상수 버그를 **놓친다는 사실**도 CHECK 로 고정해 둔다.

### Part C — 펌웨어: scatter-gather 와 STM32

- `crc32c_sgl(sg, cnt)` — 4 KiB 블록이 DMA 세그먼트 여러 개로 흩어져 있다(PRP/SGL). 세그먼트마다 독립 CRC(엔진·코어별) → combine. 길이 0 세그먼트와 비정렬 시작을 포함한다.
- `stm32_crc_hw(w, nw)` — STM32 CRC 유닛 기본 설정(poly 0x04C11DB7, init 0xFFFFFFFF, 32비트 워드 MSB 먼저, 반사·xorout 없음)과 같은 값을 **호스트(이미지 생성 툴)**에서 낸다. 반사형 명령에 `rbit` 을 써서 비반사 CRC 를 만든다. 기준은 비트 단위 CRC-32/MPEG-2(check 0x0376E6E7)다.

## 2. Problem (English)

> **Accelerate CRC-32 and CRC-32C with the ARMv8 CRC32 instructions.**
>
> 1. Implement `uint32_t crc32c_arm(uint32_t crc, const void *p, size_t n)` (and the CRC-32 twin) with ACLE intrinsics, zlib-style: start with `crc = 0`, chaining calls must equal one call. Handle any alignment and length; never read outside `[p, p+n)`. Also write `crc32c_asm` in AArch64 assembly.
> 2. `crc32cx` has ~3-cycle latency but 1/cycle throughput. Split the buffer into three independent streams and merge them: implement `crc_shift(c, n)` = c·x^(8n) mod P using one `PMULL` (64×64 carry-less multiply) plus one `crc32cx`, and use it for `crc32c_3way` and `crc32c_combine(crc1, crc2, len2)`.
> 3. Firmware: CRC-32C of a block given as a scatter-gather list (compute per segment, then combine); reproduce the STM32 CRC peripheral (MPEG-2 style, word-fed, MSB-first) on a host using the reflected instruction.
>
> Constraints: plain `cc -O2` build (enable features per function with `target("+crc,+aes")`), little-endian AArch64, `n` up to 2^61.
>
> Examples: `crc32_arm(0,"123456789",9) = 0xCBF43926`, `crc32c_arm(0,"123456789",9) = 0xE3069283`, `crc_shift(c,4) == crc32cw(c,0)`, `crc_shift(c,8) == crc32cx(c,0)`, STM32 CRC of the single word `0x12345678` = `0xDF8A8A2B` (our host model).
>
> Follow-ups: Why exactly 3 streams? Where does x^33 come from? What changes for buffers < 384 B? How does a library beat 8 bytes/cycle? What do you do on a Cortex-M?

## 3. 시작 코드

`starter.c` = 하네스 + TODO(1-way 두 개, 래퍼 두 개, `crc_mulk`, `crc_shift`, 3-way 내부 루프, SGL, STM32). `starter.S` = `crc32c_asm` 스텁으로, crc 를 그대로 돌려준다. `shared_ref.c` = 소프트웨어 기준(ALGO-M01/M02 축약판), `shared_bugs.c` = 테스트가 잡아야 하는 버그 6종이고, 둘 다 TODO 가 아니다.

<!-- code: starter.c -->

<!-- code: starter.S -->

<!-- code: shared_ref.c -->

<!-- code: shared_bugs.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c starter.S shared_ref.c shared_bugs.c -lz -o /tmp/ARM-A06_starter && /tmp/ARM-A06_starter
# (체커는 소스에 zlib.h 가 보이면 -lz 를 자동으로 붙인다)
```

처음 실행 결과 (실제 출력, 앞부분과 끝):

```text
check("123456789"): bitwise IEEE=0xCBF43926  C=0xE3069283  | crc32_arm=0x00000000 crc32c_arm=0x00000000 crc32c_asm=0x00000000
FAIL starter.c:208  crc32_arm(0, chk, 9) == 0xCBF43926u
FAIL starter.c:209  crc32c_arm(0, chk, 9) == 0xE3069283u
FAIL starter.c:210  crc32c_asm(0, chk, 9) == 0xE3069283u
len 0..64 x align 0..15: crc32_arm vs zlib bad=1024, crc32c_arm vs bitwise bad=1024, crc32c_asm bad=1024
…
3-way: len 0..4096 (all) + 200 random up to 256 KiB: bad=200
…
stm32_crc_hw({0x12345678}) = 0x00000000
21/42 passed
```

통과하는 21개는 두 종류다. 하나는 하네스 자체 검사다: 기준 구현의 check 값, 상수표 = `xpow_mod`, `vmull_p64` = 소프트웨어 clmul, 버그 탐지(버그 함수는 완성품이라 처음부터 잡힌다), MPEG-2 check. 다른 하나는 스텁이 우연히 맞는 경우다(n = 0, 빈 SGL, 양쪽이 모두 스텁인 combine 비교). `bad=1024` 는 65 × 16 = 1040 개 중 n = 0 인 16개만 맞았다는 뜻이다.

관찰용 파일 (테스트와 별개, 모두 맥에서 실행):

```bash
bash demo_isa.sh                                     # 어떤 타깃에 CRC32/PMULL 이 있나 (어셈블러에 묻기) → isa_m6.txt
cc -std=c11 -O2 demo_trace.c -o /tmp/ARM-A06_trace && /tmp/ARM-A06_trace          # 단계별 상태 → trace_m6.txt
cc -std=c11 -O2 demo_lat.c demo_lat.S -o /tmp/ARM-A06_lat && /tmp/ARM-A06_lat      # 지연/처리량 → lat_m6.txt
cc -std=c11 -O2 demo_bench.c solution.S shared_ref.c shared_bugs.c -lz -o /tmp/ARM-A06_bench && /tmp/ARM-A06_bench   # → bench_m6.txt
bash demo_zlib_disasm.sh                             # 시스템 zlib 의 crc32 를 lldb 로 디스어셈블 → zlib_disasm_m6.txt
```

## 4. 힌트

<details>
<summary>힌트 1 — 명령 하나가 하는 일</summary>

`crc32cx w0, w0, x1` 은 테이블 CRC 의 "8 바이트 먹이기"와 같다. 상태 w0 에 x1 의 바이트를 **낮은 바이트부터**(리틀 엔디언 메모리 순서) 넣는다. 조건화는 하지 않는다. 그래서 zlib 과 같은 값을 얻으려면 `~` 를 앞뒤에 직접 붙인다: `crc = ~raw(~crc, p, n)`.

</details>

<details>
<summary>힌트 2 — 머리와 꼬리</summary>

주소 비트 0 → 1 B, 비트 1 → 2 B, 비트 2 → 4 B 를 차례로 먹이면 p 가 8 의 배수가 된다. 단계마다 n 이 충분한지 먼저 본다. 본문이 끝나면 n < 8 이므로, n 의 비트 2·1·0 을 차례로 보며 4·2·1 B 를 먹인다. 이 순서가 곧 메모리 순서다.

</details>

<details>
<summary>힌트 3 — 지연을 숨기는 법</summary>

명령 하나가 결과를 내기까지 3 사이클이 걸리고, 유닛은 매 사이클 새 명령을 받는다면, 동시에 3개가 진행 중이어야 유닛이 쉬지 않는다(지연 × 처리량 = 3). 서로 의존하지 않는 체인 3개를 만든다: 구간 A 는 이어받은 상태에서, B 와 C 는 0 에서 시작한다.

</details>

<details>
<summary>힌트 4 — 합치기 = 0 바이트 먹이기</summary>

`raw(A‖B, s) = raw(A, s)·x^(8|B|) ⊕ raw(B, 0)` (선형성). 그러니 필요한 것은 "상태에 x^(8n) 곱하기"다. `crc32cx(0, d)` 는 d·x^32 mod P 다. 64비트 다항식 곱 `PMULL` 결과를 여기 넣으면 mod P 가 공짜로 나온다. 곱셈 상수에서 빼야 할 x 의 지수를 세어 보라(32 + 1).

</details>

<details>
<summary>힌트 5 — STM32 를 반사형 명령으로</summary>

비반사 CRC 를 MSB 먼저 처리하는 것은, 모든 비트를 뒤집은 세계에서 LSB 먼저 처리하는 것과 같다. 상태도 `rbit`, 입력 워드도 `rbit`, 결과도 `rbit` 한다. 다항식은 같은 0x04C11DB7(반사 0xEDB88320) 이므로 `__crc32w` 를 그대로 쓴다.

</details>

## 5. 풀이

### 면접에서 말하는 순서

| 단계 | 이 문제에서 말할 것 |
|---|---|
| 1. 범위 확인 | 어느 CRC 인가(32 vs 32C), 조건화·바이트 순서, 버퍼 크기 분포(짧은 패킷 vs 큰 블록), 타깃 코어(FEAT_CRC32? PMULL?), 정렬 보장 |
| 2. 단순 해법 | 1-way: 머리 → `crc32cx` 본문 → 꼬리. 바이트당 3/8 사이클, slicing-8 의 약 4배 |
| 3. 병목 | 본문 루프는 **지연**에 묶인다(3 사이클/8 B). 유닛 처리량은 1/사이클이라서 3배의 여유가 있다 |
| 4. 개선 | 독립 스트림 3개 + combine. 시프트는 PMULL + crc32cx, 상수 `x^(8B−33)` 은 미리 계산한다 |
| 5. 테스트 | check 값, n = 0, 길이 0..64 × 정렬 0..15, 길이 0..4096 전수(3-way 경계 384/3072 포함), 페이지 끝, 조각 스트리밍, zlib 대조, 버그 주입 |
| 6. 한계 | 384 B 미만은 1-way, CRC 명령만으로는 8 B/사이클이 천장. 더 가려면 PMULL folding(zlib 이 그렇게 한다, 아래 실측) |

### 0. 기능 확인 — 이 칩·이 컴파일러에서 쓸 수 있나

```text
$ sysctl hw.optional.arm.FEAT_CRC32 hw.optional.arm.FEAT_PMULL machdep.cpu.brand_string
hw.optional.arm.FEAT_CRC32: 1
hw.optional.arm.FEAT_PMULL: 1
machdep.cpu.brand_string: Apple M6
$ echo | cc -dM -E - | grep -E '__ARM_FEATURE_(CRC32|AES|CRYPTO)'
#define __ARM_FEATURE_AES 1
#define __ARM_FEATURE_CRC32 1
#define __ARM_FEATURE_CRYPTO 1
```

`demo_isa.sh` 로 어셈블러(LLVM)에 직접 물어본 결과(`isa_m6.txt`, 발췌):

```text
--- AArch32 / M-profile: crc32b r0, r0, r1
--target=armv7a-none-eabi -marm                          ERR  instruction requires: crc
--target=armv8a-none-eabi -marm                          OK
--target=armv8r-none-eabi -mcpu=cortex-r52               OK
--target=thumbv7em-none-eabi -mcpu=cortex-m7             ERR  instruction requires: crc
--target=thumbv8m.main-none-eabi -mcpu=cortex-m33        ERR  instruction requires: crc
--target=thumbv8.1m.main-none-eabi -mcpu=cortex-m55      ERR  instruction requires: crc
--target=thumbv8.1m.main-none-eabi -mcpu=cortex-m85      ERR  instruction requires: crc
--- AArch64: crc32cx w0, w0, x1
--target=aarch64-none-elf -march=armv8-a                 ERR  instruction requires: crc
--target=aarch64-none-elf -march=armv8-a+crc             OK
--target=aarch64-none-elf -march=armv8.1-a               OK
--target=aarch64-none-elf -mcpu=cortex-a53               OK
--- AArch64: pmull v0.1q, v0.1d, v1.1d (64x64 다항식 곱 = FEAT_PMULL, AES 확장에 묶임)
--target=aarch64-none-elf -march=armv8.1-a               ERR  instruction requires: aes
--target=aarch64-none-elf -march=armv8-a+aes             OK
```

- CRC32 명령은 Armv8.0 에서 선택이고 **Armv8.1 부터 필수**다(Arm ARM 기준, 확인 권장). 어셈블러도 `armv8-a` 에서는 거부하고 `armv8.1-a` 에서는 받는다.
- **M-profile 코어(M7/M33/M55/M85)에는 LLVM 모델상 CRC32 명령이 없다.** Arm 문서에서도 M-profile ISA 에 CRC32 가 없다고 알고 있다(확인 권장). Cortex-R52(Armv8-R AArch32)는 받는다. 그래서 MCU 는 CRC 유닛(STM32 CRC, nRF 의 무선 CRC 등)이나 테이블을 쓴다. Part C 가 그 이야기다.
- `PMULL` 64×64 는 AES(암호) 확장에 묶여 있다. 수출 규제 등으로 crypto 확장을 빼고 나오는 SoC 가 있다(예: 일부 Cortex-A53 SoC, 확인 권장). 그런 코어에서는 3-way 의 시프트를 테이블로 대신해야 한다(Q7).
- 그래서 코드에는 `__attribute__((target("+crc,+aes")))`(C), `.arch_extension crc`(.S) 를 붙였다. 맥 기본 타깃에는 이미 켜져 있다. 이렇게 하면 리눅스 generic `armv8-a` 의 gcc/clang 에서도 플래그 없이 빌드된다. VM 실측: gcc 15.2·clang 21.1.8 모두 `42/42 passed`.

### Part A — 명령의 정확한 의미

Arm ARM 의 의사코드를 요약하면(기억 기준, 확인 권장) `CRC32X(acc, val)` = `BitReverse( Poly32Mod2( BitReverse(acc):Zeros(64) ⊕ BitReverse(val):Zeros(32), P ) )` 다. 반사 표현(32비트 값의 비트 k = x^(31−k) 계수)으로 쓰면 이렇다.

> `crc32x(acc, d) = acc·x^64 + d·x^32  (mod P)`,  d 는 64비트 값(비트 k = x^(63−k)), 낮은 주소 바이트가 먼저 들어간다.

이 식이 바이트 테이블 8 번과 같다는 것은 테스트로 확인했다(`crc32c_arm == ref_bitwise` 1040/1040, 랜덤 2000/2000). 테이블 CRC 의 상태 `c` 와 이 명령의 `acc` 는 **같은 레지스터**이고 조건화만 빠져 있다. `demo_trace.c` 실측(`trace_m6.txt`):

```text
1) "123456789" at an 8-aligned address
  start  p%8=0 n=9  state=0xFFFFFFFF
  body crc32cx  8 B (x=0x3837363534333231) -> 0x9F787F65
  tail crc32cb  1 B -> 0x1CF96D7C
  final ~state = 0xE3069283
```

`x = 0x3837363534333231` 은 "12345678" 을 리틀 엔디언 `ldr` 로 읽은 값이다. 최하위 바이트 '1'(0x31)이 가장 먼저 들어간다. 바이트 순서를 뒤집어 넣으면(`rev`) 값이 틀린다(버그 3).

### Part A — 정렬 머리 / 꼬리

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">29 바이트, 시작 주소 % 8 = 5 — 머리 1+2 B → 본문 8 B × 3 → 꼬리 2 B (demo_trace 실측)</text> <rect x="60" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="71.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">5</text> <rect x="82" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="93.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">6</text> <rect x="104" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/>
<text x="115.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">7</text> <line x1="126" y1="40" x2="126" y2="112" stroke="currentColor" stroke-width="2"/> <text x="126" y="38" fill="currentColor" font-size="12" text-anchor="middle">8·1</text> <rect x="126" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="137.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">0</text> <rect x="148" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/>
<text x="159.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">1</text> <rect x="170" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="181.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">2</text> <rect x="192" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="203.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">3</text> <rect x="214" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/>
<text x="225.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">4</text> <rect x="236" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="247.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">5</text> <rect x="258" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="269.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">6</text> <rect x="280" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/>
<text x="291.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">7</text> <line x1="302" y1="40" x2="302" y2="112" stroke="currentColor" stroke-width="2"/> <text x="302" y="38" fill="currentColor" font-size="12" text-anchor="middle">8·2</text> <rect x="302" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="313.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">0</text> <rect x="324" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/>
<text x="335.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">1</text> <rect x="346" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="357.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">2</text> <rect x="368" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="379.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">3</text> <rect x="390" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/>
<text x="401.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">4</text> <rect x="412" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="423.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">5</text> <rect x="434" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="445.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">6</text> <rect x="456" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/>
<text x="467.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">7</text> <line x1="478" y1="40" x2="478" y2="112" stroke="currentColor" stroke-width="2"/> <text x="478" y="38" fill="currentColor" font-size="12" text-anchor="middle">8·3</text> <rect x="478" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="489.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">0</text> <rect x="500" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/>
<text x="511.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">1</text> <rect x="522" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="533.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">2</text> <rect x="544" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="555.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">3</text> <rect x="566" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/>
<text x="577.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">4</text> <rect x="588" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="599.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">5</text> <rect x="610" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="621.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">6</text> <rect x="632" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/>
<text x="643.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">7</text> <line x1="654" y1="40" x2="654" y2="112" stroke="currentColor" stroke-width="2"/> <text x="654" y="38" fill="currentColor" font-size="12" text-anchor="middle">8·4</text> <rect x="654" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/> <text x="665.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">0</text> <rect x="676" y="46" width="22" height="22" fill="none" stroke="currentColor" stroke-width="0.6"/>
<text x="687.0" y="62" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">1</text> <rect x="61" y="74" width="20" height="24" class="f-warn" stroke="currentColor"/> <text x="71.0" y="91" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">cb</text> <line x1="71.0" y1="98" x2="71.0" y2="116" stroke="currentColor" stroke-width="0.6"/> <text x="71.0" y="128" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">0x3B3DE162</text> <rect x="83" y="74" width="42" height="24" class="f-warn" stroke="currentColor"/>
<text x="104.0" y="91" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">ch</text> <line x1="104.0" y1="98" x2="104.0" y2="136" stroke="currentColor" stroke-width="0.6"/> <text x="104.0" y="148" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">0xFF3D640C</text> <rect x="127" y="74" width="174" height="24" class="f-acc" stroke="currentColor"/> <text x="214.0" y="91" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">cx</text> <line x1="214.0" y1="98" x2="214.0" y2="116" stroke="currentColor" stroke-width="0.6"/>
<text x="214.0" y="128" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">0x35F221BB</text> <rect x="303" y="74" width="174" height="24" class="f-acc" stroke="currentColor"/> <text x="390.0" y="91" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">cx</text> <line x1="390.0" y1="98" x2="390.0" y2="136" stroke="currentColor" stroke-width="0.6"/> <text x="390.0" y="148" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">0xAC81A30B</text> <rect x="479" y="74" width="174" height="24" class="f-acc" stroke="currentColor"/>
<text x="566.0" y="91" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">cx</text> <line x1="566.0" y1="98" x2="566.0" y2="116" stroke="currentColor" stroke-width="0.6"/> <text x="566.0" y="128" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">0xD07F63B9</text> <rect x="655" y="74" width="42" height="24" class="f-ok" stroke="currentColor"/> <text x="676.0" y="91" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">ch</text> <line x1="676.0" y1="98" x2="676.0" y2="136" stroke="currentColor" stroke-width="0.6"/>
<text x="676.0" y="148" fill="currentColor" font-size="12" text-anchor="middle" font-family="monospace">0xC50C0197</text> <text x="10" y="62" fill="currentColor" font-size="12">주소%8</text> <text x="10" y="91" fill="currentColor" font-size="12">명령</text> <text x="10" y="138" fill="currentColor" font-size="12">상태</text> <text x="10" y="186" fill="currentColor">정렬된 주소에 두면 cx·cx·cx → cw·cb. 마지막 상태는 둘 다 0xC50C0197 (~ → 0x3AF3FE68).</text> <text x="10" y="208" fill="currentColor">규칙: 머리는 주소의 비트 0·1·2 를 지우고(남은 n 이 모자라면 바로 꼬리), 꼬리는 n 의 비트 2·1·0 만큼.</text> <text x="10" y="230" fill="currentColor">어느 단계도 p+n 을 넘지 않는다 → 페이지 끝 테스트(다음 페이지 PROT_NONE)에서 0 불일치·0 크래시.</text>
</svg>
```

```text
2) 29-byte message at address % 8 = 5
  start  p%8=5 n=29  state=0xFFFFFFFF
  head crc32cb  1 B -> 0x3B3DE162
  head crc32ch  2 B -> 0xFF3D640C
  body crc32cx  8 B (x=0x62206B6369757120) -> 0x35F221BB
  body crc32cx  8 B (x=0x786F66206E776F72) -> 0xAC81A30B
  body crc32cx  8 B (x=0x6F2073706D756A20) -> 0xD07F63B9
  tail crc32ch  2 B -> 0xC50C0197
  final ~state = 0x3AF3FE68
   same bytes at an 8-aligned address (no head):
  …
  tail crc32cb  1 B -> 0xC50C0197
  final ~state = 0x3AF3FE68
   equal: yes
```

명령을 어떻게 나눠 먹이든 마지막 상태가 같다(`0xC50C0197`). CRC 는 바이트 열에만 의존하고 "몇 바이트씩 먹였나"와는 무관하다. 머리를 두는 이유는 두 가지다.

1. **정렬된 8 B 로드**: AArch64 의 Normal 메모리는 비정렬 `ldr` 을 허용한다. 그래도 캐시 라인·페이지를 걸치는 로드는 느려질 수 있다. 실측으로는 큰 버퍼에서 차이가 노이즈 수준이었다(`p+1 unaligned` 행, 아래 표). 맥 M6 에서는 머리를 생략해도 됐을 것이다.
2. **MMU-off / Device 메모리**: 부트 초기(ARM-M13)나 `-mstrict-align` 환경에서는 비정렬 접근이 fault 를 낸다. 펌웨어 라이브러리는 머리를 둔다.

꼬리는 **p+n 너머를 읽지 않는 것**이 핵심이다. 하네스는 2 페이지를 mmap 하고 둘째 페이지를 `PROT_NONE` 으로 막는다. 그다음 n = 0..64 버퍼를 첫 페이지 끝에 붙여 부른다. 결과는 `crc32c_arm bad=0, crc32c_asm bad=0` 이고 크래시도 없다. 꼬리를 "8 B 로드 후 마스크"로 바꾸면 여기서 SIGBUS 가 난다.

clang -O2 가 intrinsic 판을 내린 결과는 손 asm 과 거의 같다(`cc -O2 -S solution.c`, 본문과 꼬리만):

```text
LBB1_10:                                ; =>This Inner Loop Header: Depth=1
	ldr	x8, [x1], #8
	crc32cx	w0, w0, x8
	sub	x2, x2, #8
	cmp	x2, #7
	b.hi	LBB1_10
LBB1_11:
	cmp	x2, #4
	b.hs	LBB1_16
	tbnz	w2, #1, LBB1_17
```

그래서 1-way 에서는 intrinsic 과 .S 의 처리량이 같다(표: 10.06 vs 10.31 GB/s). asm 을 손으로 쓰는 이유는 속도가 아니다. 면접 화이트보드, 부트 ROM·RTOS 포트 같은 C 런타임이 없는 곳, 레지스터 할당을 통제해야 하는 곳에서 쓴다.

### Part B — 왜 3-way 인가: 지연 × 처리량

`demo_lat.c` + `demo_lat.S` 는 같은 명령을 사슬 1/2/3/4/8 개로 돌린다. 사이클 수는 "add 의존 사슬 1개 = 1 사이클"로 환산한 추정치다. 부하가 큰 공유 맥이라 표본 31 개의 최솟값 기반 비율을 주로 본다(`lat_m6.txt` run 1, load avg 25.9):

```text
load avg 25.9 22.8 18.8 | add chain: min 0.2541 ns/insn (~3.93 GHz if add = 1 cycle), median 0.2701
crc32cx  1 chain (latency)   2.94 cycles/insn (min-based)   2.96 (median-based)
pmull    1 chain (latency)   2.64 cycles/insn (min-based)   2.89 (median-based)
crc32cx  2 chains            1.32 cycles/insn (min-based)   1.72 (median-based)
crc32cx  3 chains            0.88 cycles/insn (min-based)   1.00 (median-based)
crc32cx  4 chains            0.93 cycles/insn (min-based)   0.99 (median-based)
crc32cx  8 chains            0.95 cycles/insn (min-based)   1.00 (median-based)
pmull    4 chains            0.66 cycles/insn (min-based)   0.74 (median-based)
```

- `crc32cx` 지연 ≈ 3 사이클이고, 사슬 3개 이상에서 ≈ 1 사이클/명령으로 포화한다. 4개·8개로 늘려도 더 빨라지지 않는다. **CRC 유닛이 사이클당 1개**인 모양이다. Little 의 법칙대로 동시에 진행 중이어야 하는 명령 수 = 지연 × 처리량 = 3 × 1 = 3 이다.
- 사슬 3개의 0.88 이 1 보다 작은 것은 측정 오차다. add 사슬과 측정 시점의 클럭이 다를 수 있다. 다른 실행에서는 0.87–1.12 가 나왔다. "≈ 1"로 읽는다.
- `pmull` 도 지연 ≈ 3 이다. 사슬 4개는 대부분의 실행에서 0.66–0.75 였다(한 번은 1.11). 4 사슬 × 지연 3 이면 0.75 가 한계이므로 이 값은 지연에 묶인 것이다. 즉 PMULL 은 **사이클당 1개 넘게** 받는 것으로 보인다. zlib 이 이 점을 이용한다(아래 성능 절).
- Apple 은 이 수치를 공개하지 않았다. 위 값은 이 맥에서 잰 추정이다.

### Part B — 시프트 수학: 왜 x^(8n−33) 인가

합치기에 필요한 연산은 하나다: 조건화 없는 상태 c 를 "0 바이트 n 개 먹인 상태" c·x^(8n) mod P 로 바꾸는 것. 재료는 두 가지다.

1. **`PMULL v.1q, a.1d, b.1d`** — 정수 a, b 의 무캐리 곱이다. a 의 비트 i 와 b 의 비트 j 가 결과 비트 i+j 로 간다. 반사 표현에서 a 의 비트 i = x^(31−i), b 의 비트 j = x^(31−j) 이므로 결과 비트 k 는 x^(62−k) 다. 32×32 → 최대 63비트라서 결과의 비트 63 은 항상 0 이다.
2. **`crc32cx(0, q)`** = q·x^32 mod P 다. 단, 64비트 값 q 는 비트 k = x^(63−k) 로 읽힌다.

곱의 비트 k 는 실제로 x^(62−k) 인데, 명령은 x^(63−k) 로 읽는다. 그래서 x 하나가 더 붙는다.

> `crc32cx(0, pmull(c, k)) = x · (c·k) · x^32 = c · k · x^33  (mod P)`

따라서 k = x^(8n−33) mod P 를 넣으면 결과는 c·x^(8n) 이다. 이 등식은 하네스가 확인한다. n = 0..300 과 n ≤ 2^40 랜덤에서, 두 다항식 모두 소프트웨어 `gf2_mulmod(xpow_mod(8n), c)` 와 대조해 불일치 0 이었다(`bsh == 0`). 짧은 확인도 있다: `crc_shift(c, 4) == crc32cw(c, 0)`, `crc_shift(c, 8) == crc32cx(c, 0)`. 4 바이트 0 을 먹이는 것은 x^32 곱셈이다.

- **n < 5 함정**: 8n − 33 < 0 이다. `uint64_t` 로 계산하면 2^64 근처로 감싸서 엉뚱한 지수가 된다. x 는 mod P 에서 역원이 있으니 음수 지수도 정의할 수는 있다. 하지만 4 바이트 이하는 명령으로 0 을 직접 먹이는 것이 가장 단순하다. 임계값을 `n < 4` 로 바꾸는 변이를 넣어 보면 n = 4 에서 틀린다(변이 M2 → `38/42`).
- **combine**: `crc(A‖B) = shift(crc(A), |B|) ⊕ crc(B)` 다. init = xorout = ~0 이 상쇄되므로 조건화된 값끼리 그대로 성립한다(증명: ALGO-M02 Part B). zlib `crc32_combine()` 과 2000 번 대조해 불일치 0 이었다. 임의 길이는 `xpow_mod` 가 O(log n) 번의 32×32 GF(2) 곱이다. 3-way 의 덩어리 크기는 고정이라 상수를 미리 계산한다.

| B (스트림 길이) | k1 = x^(8B−33) mod P_c | k2 = x^(16B−33) mod P_c | 덩어리 |
|---|---|---|---|
| 1024 | 0x170076FA | 0xA51B6135 | 3 KiB |
| 128 | 0x0D3B6092 | 0xB9E02B86 | 384 B |

(값은 `xpow_mod` 로 계산했고 테스트가 다시 확인한다. k2(B) = k1(2B) 인 것도 보인다: 0xB9E02B86 = k1(256).)

### Part B — 3-way 의 구조

```svg
<svg viewBox="0 0 740 400" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">crc32c_3way: 3·B 바이트 덩어리 하나 (B = 1024 또는 128)</text> <rect x="60" y="36" width="210" height="30" class="f-acc" stroke="currentColor"/> <text x="165" y="56" fill="currentColor" text-anchor="middle">구간 A : B 바이트</text> <rect x="280" y="36" width="210" height="30" class="f-ok" stroke="currentColor"/> <text x="385" y="56" fill="currentColor" text-anchor="middle">구간 B : B 바이트</text> <rect x="500" y="36" width="210" height="30" class="f-warn" stroke="currentColor"/> <text x="605" y="56" fill="currentColor" text-anchor="middle">구간 C : B 바이트</text>
<text x="60" y="84" fill="currentColor" font-size="12" font-family="monospace">p</text><text x="280" y="84" fill="currentColor" font-size="12" font-family="monospace">p+B</text><text x="500" y="84" fill="currentColor" font-size="12" font-family="monospace">p+2B</text><text x="710" y="84" fill="currentColor" font-size="12" font-family="monospace" text-anchor="end">p+3B</text> <line x1="165" y1="90" x2="165" y2="112" stroke="currentColor"/><path d="M160,106 L165,114 L170,106" fill="none" stroke="currentColor"/> <rect x="70" y="116" width="190" height="46" fill="none" stroke="currentColor"/> <text x="165" y="134" fill="currentColor" text-anchor="middle" font-size="12">c0 ← c (이어받은 상태)</text>
<text x="165" y="152" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">crc32cx × B/8 번</text> <line x1="385" y1="90" x2="385" y2="112" stroke="currentColor"/><path d="M380,106 L385,114 L390,106" fill="none" stroke="currentColor"/> <rect x="290" y="116" width="190" height="46" fill="none" stroke="currentColor"/> <text x="385" y="134" fill="currentColor" text-anchor="middle" font-size="12">c1 ← 0</text> <text x="385" y="152" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">crc32cx × B/8 번</text> <line x1="605" y1="90" x2="605" y2="112" stroke="currentColor"/><path d="M600,106 L605,114 L610,106" fill="none" stroke="currentColor"/>
<rect x="510" y="116" width="190" height="46" fill="none" stroke="currentColor"/> <text x="605" y="134" fill="currentColor" text-anchor="middle" font-size="12">c2 ← 0</text> <text x="605" y="152" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">crc32cx × B/8 번</text> <rect x="70" y="190" width="190" height="46" class="f-mut" stroke="currentColor"/><text x="165" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">pmull c0, k2</text><text x="165" y="226" fill="currentColor" text-anchor="middle" font-size="12">k2 = x^(16B−33) mod P</text>
<rect x="290" y="190" width="190" height="46" class="f-mut" stroke="currentColor"/><text x="385" y="208" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">pmull c1, k1</text><text x="385" y="226" fill="currentColor" text-anchor="middle" font-size="12">k1 = x^(8B−33) mod P</text> <rect x="70" y="252" width="190" height="30" fill="none" stroke="currentColor"/><text x="165" y="272" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">crc32cx wzr, 곱</text>
<rect x="290" y="252" width="190" height="30" fill="none" stroke="currentColor"/><text x="385" y="272" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">crc32cx wzr, 곱</text> <line x1="165" y1="162" x2="165" y2="188" stroke="currentColor"/><line x1="165" y1="236" x2="165" y2="250" stroke="currentColor"/><line x1="165" y1="282" x2="165" y2="300" stroke="currentColor"/> <line x1="385" y1="162" x2="385" y2="188" stroke="currentColor"/><line x1="385" y1="236" x2="385" y2="250" stroke="currentColor"/><line x1="385" y1="282" x2="385" y2="300" stroke="currentColor"/> <line x1="605" y1="162" x2="605" y2="300" stroke="currentColor"/>
<line x1="165" y1="300" x2="605" y2="300" stroke="currentColor"/> <circle cx="385" cy="318" r="12" fill="none" stroke="currentColor"/><text x="385" y="323" fill="currentColor" text-anchor="middle">⊕</text><line x1="385" y1="300" x2="385" y2="306" stroke="currentColor"/> <line x1="385" y1="330" x2="385" y2="348" stroke="currentColor"/><rect x="290" y="348" width="190" height="26" class="f-acc" stroke="currentColor"/><text x="385" y="366" fill="currentColor" text-anchor="middle" font-size="12">새 c → 다음 덩어리의 c0</text> <text x="612" y="240" fill="currentColor" font-size="12">c2: 시프트 없음</text>
<text x="10" y="394" fill="currentColor" font-size="12">c = c0·x^(16B) ⊕ c1·x^(8B) ⊕ c2 (조건화 없는 상태, mod P). 곱 1번 = fmov·pmull·fmov·crc32cx (clang -O2).</text>
</svg>
```

```c
uint32_t c0 = c, c1 = 0, c2 = 0;
for (size_t i = 0; i < blk; i += 8) {          /* 세 체인이 서로 기다리지 않는다 */
    c0 = __crc32cd(c0, ld64(p + i));
    c1 = __crc32cd(c1, ld64(p + blk + i));
    c2 = __crc32cd(c2, ld64(p + 2 * blk + i));
}
c = crc_mulk(c0, k2, 1) ^ crc_mulk(c1, k1, 1) ^ c2;
```

- **왜 c1, c2 는 0 에서 시작하나**: 선형성 때문이다. `raw(A‖B‖C, s) = raw(A,s)·x^(16B) ⊕ raw(B,0)·x^(8B) ⊕ raw(C,0)`.
- **clang 이 만든 것**: 큰 덩어리(B = 1024)는 위 모양 그대로 `crc32cx` 3개가 번갈아 나오는 루프가 됐다. 작은 덩어리(B = 128)는 완전히 펼쳐져서(unroll) 체인별로 16개씩 이어 놓은 모양이 됐다. 체인끼리 독립이라 비순차(OoO) 코어가 창 안에서 섞어 실행한다. 창이 작은 in-order 코어(Cortex-A53/A55)에서는 이런 배치가 손해일 수 있다. 그런 코어에는 명령을 직접 번갈아 놓는 asm 을 쓴다(확인 권장).
- **임계 경로**: 다음 덩어리의 c0 는 이번 combine 결과를 기다린다(fmov→pmull→fmov→crc32cx, 추정 10 사이클 이상). 덩어리당 이 지연이 그대로 더해진다. B = 128 이면 체인 16개 × 3 = 48 사이클에 +10 이라 무시하기 어렵다. **fold-ahead**(연습 2)는 세 체인 모두 0 에서 시작하고 이어받은 상태를 x^(24B) 로 미는 방식이다. 이렇게 하면 combine 이 체인과 겹친다. 실측에서 1 KiB 는 15.9–16.9 → 17.0–18.2 GB/s 로 약간 빨라졌고, 64 KiB 이상은 노이즈 안에서 같았다.
- **384 B 미만**은 1-way 로 처리한다. 3·B 의 B 를 더 줄이면 combine 비용(곱 2번)이 본문 대비 커진다.

### Part B — 버그 6종과 테스트의 힘

하네스의 `suite_c(f, small, maxlen)` 는 길이 0..small × 정렬 16가지와 길이 0..maxlen 전수(오프셋·시작값 랜덤)로 구성된 묶음이다. 실제 불일치 수(강한 묶음 64/1200 / 약한 묶음 16/300):

```text
bugs (mismatches, strong / weak suite): no_invert=2224/556 ieee_body=2044/378 bswap_body=2046/376 tail_no1=1117/287 3way_no33=813/0 3way_swapk=812/0
```

| 버그 | 무엇이 틀렸나 | 강/약 | 교훈 |
|---|---|---|---|
| `bug_no_invert` | `~` 없음 | 2224 / 556 | n = 0 만 우연히 맞는다 |
| `bug_ieee_body` | 본문만 `crc32x` | 2044 / 378 | 머리·꼬리만 쓰는 n < 8 은 맞는다 |
| `bug_bswap_body` | 8 B 를 BE 로 | 2046 / 376 | 같은 이유 |
| `bug_tail_no1` | 꼬리 1 B 누락 | 1117 / 287 | 정렬·길이에 따라 반만 틀린다 |
| `bug_3way_no33` | 상수 x^(8B) | 813 / **0** | **384 B 미만 테스트로는 영원히 안 보인다** |
| `bug_3way_swapk` | k1 ↔ k2 | 812 / **0** | 같은 이유 |

3-way 버그를 잡으려면 테스트 길이가 경로 전환 지점(384 B, 3072 B)을 넘어야 한다. 그래서 하네스는 길이 0..4096 을 **전수**로 돈다. 별도로 정답 코드에 변이 7개를 넣어(레슨 폴더 밖 사본) 하네스가 모두 잡는 것도 확인했다. asm 루프 `b.hs`→`b.hi`(38/42), 시프트 임계 `n<4`(38/42), 3-way `c0 = 0`(39/42), STM32 마지막 `rbit` 누락(41/42), SGL combine 인자 순서(41/42), 꼬리 `p += 2` 누락(35/42), asm 머리 `crc32ch`→`crc32cb`(38/42).

### Part C — scatter-gather (NVMe/iSCSI)

SSD 컨트롤러에서 4 KiB 논리 블록은 호스트 메모리의 여러 PRP/SGL 조각으로 온다. 데이터 경로 엔진이 조각마다 CRC 를 따로 내면, 펌웨어(또는 다음 엔진)가 그 결과를 합쳐야 한다.

```c
uint32_t acc = 0;                                   /* 빈 메시지의 CRC = 0 */
for (size_t i = 0; i < cnt; i++) {
    uint32_t ci = crc32c_3way(0, sg[i].p, sg[i].len);   /* 이 줄은 병렬로 돌 수 있다 */
    acc = crc32c_combine_hw(acc, ci, sg[i].len);
}
```

테스트에서는 세그먼트 0~16개를 썼고, 길이 0 세그먼트가 약 1/4, 시작 주소는 임의, 길이는 0..1499 B 다. 500 회 모두 "이어 붙인 사본의 CRC"와 같았고, 빈 목록은 0 이었다. 실제 시스템에서는 combine 비용이 세그먼트 수 × O(log len) 이다. 4 KiB 블록처럼 길이 집합이 작으면 상수를 표로 만든다(zlib 은 x^(2^k) 표를 쓴다).

### Part C — STM32 CRC 유닛을 호스트에서

STM32F1/F4 계열 CRC 유닛의 기본 동작은 비반사, MSB 먼저, 32비트 워드 단위, init 0xFFFFFFFF, xorout 없음이다. 이것은 워드를 빅 엔디언 바이트로 늘어놓은 CRC-32/MPEG-2 와 같다(ALGO-M02 연습 3, 칩별 RM 확인 권장). 부트로더가 검사할 이미지 CRC 는 **호스트 툴**이 미리 계산해 붙인다. 이 툴이 수 MB 이미지를 빠르게 처리하려면 반사형 명령을 써야 하는데, 다음 성질을 이용한다.

> 비반사 CRC(상태 r, MSB 먼저) ≡ 모든 것을 비트 반전한 반사 CRC(상태 rbit(r), LSB 먼저)

```c
uint32_t s = 0xFFFFFFFFu;                                  /* rbit(0xFFFFFFFF) */
for (size_t i = 0; i < nw; i++) s = __crc32w(s, __rbit(w[i]));
return __rbit(s);
```

`ref_mpeg2_bitwise(0xFFFFFFFF, "123456789")` = **0x0376E6E7**(RevEng check)이다. 랜덤 워드 배열 1000개를 BE 직렬화한 MPEG-2 와 대조해 불일치 0 이었다. 출력 `stm32_crc_hw({0x12345678}) = 0xDF8A8A2B` 는 이 모델이 계산한 값이다. ST 포럼 등에서 같은 값을 본 기억이 있지만, 실칩 대조는 하지 않았다(확인 권장). 칩에서는 반대로 CPU 가 계산하지 않는다. CRC 유닛 + DMA 로 플래시를 읽으며 검사하는 것이 정석이다.

### 성능 실측 — 1 KiB / 64 KiB / 1 MiB

`demo_bench.c` 는 측정 전에 모든 행이 기준과 같은 값을 내는지 확인한다. 한 라운드는 모든 행 × 크기를 번갈아 돌며 각각 best-of-5 를 잰다. 이렇게 7 라운드를 돌려 중앙값과 [최소..최대] 를 낸다. 표본 1개 ≈ 8 MiB 다. 이 맥은 다른 작성자 10명과 공유 중이었다(load avg ≈ 25, `uptime` 함께 기록). 절대값보다 비율을 봐야 한다. `bench_m6.txt` run 1:

```text
zlib 1.2.12 | load avg at start 25.9 22.8 18.8 | GB/s = median over 7 rounds of (best of 5 samples) [min..max]
implementation                               1 KiB                64 KiB                 1 MiB
bitwise (C, CRC-32C)           0.15 [ 0.13.. 0.16]   0.15 [ 0.15.. 0.16]   0.16 [ 0.14.. 0.17]
byte table (C)                 0.49 [ 0.38.. 0.53]   0.48 [ 0.41.. 0.51]   0.45 [ 0.41.. 0.50]
slicing-by-8 (C)               2.66 [ 2.17.. 2.79]   2.64 [ 2.59.. 3.02]   2.60 [ 2.55.. 2.70]
crc32c_arm 1-way (intrin)     11.46 [11.20..12.99]  10.06 [ 7.03..11.07]  10.23 [ 9.14..10.33]
crc32c_asm 1-way (.S)         11.04 [ 6.54..11.56]  10.31 [ 9.95..11.15]  10.68 [10.09..11.91]
crc32c_3way (intrin+PMULL)    16.87 [13.81..19.84]  29.01 [20.04..33.73]  28.59 [21.43..32.89]
crc32c_3way, p+1 unaligned    16.17 [13.27..18.37]  28.11 [17.53..32.17]  29.19 [27.55..33.23]
3-way fold-ahead (ex. 2)      18.19 [11.28..21.07]  29.48 [17.13..35.22]  28.76 [15.74..35.36]
crc32_arm 1-way (IEEE)        11.42 [11.18..13.48]  10.24 [10.07..10.60]  10.23 [ 7.03..10.55]
zlib crc32() (IEEE)           25.25 [24.54..29.04]  39.98 [27.66..44.76]  40.80 [38.73..46.90]
```

```svg
<svg viewBox="0 0 740 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">64 KiB 처리량 (GB/s, 로그 눈금) — bench_m6.txt run 1 의 중앙값</text> <line x1="190.0" y1="30" x2="190.0" y2="268" stroke="currentColor" stroke-dasharray="2 3" stroke-width="0.6"/><text x="190.0" y="284" fill="currentColor" font-size="12" text-anchor="middle">0.1</text> <line x1="340.0" y1="30" x2="340.0" y2="268" stroke="currentColor" stroke-dasharray="2 3" stroke-width="0.6"/><text x="340.0" y="284" fill="currentColor" font-size="12" text-anchor="middle">1</text>
<line x1="490.0" y1="30" x2="490.0" y2="268" stroke="currentColor" stroke-dasharray="2 3" stroke-width="0.6"/><text x="490.0" y="284" fill="currentColor" font-size="12" text-anchor="middle">10</text> <line x1="640.0" y1="30" x2="640.0" y2="268" stroke="currentColor" stroke-dasharray="2 3" stroke-width="0.6"/><text x="640.0" y="284" fill="currentColor" font-size="12" text-anchor="middle">100</text> <text x="10" y="50" fill="currentColor" font-size="12">bitwise (C)</text> <rect x="190" y="36" width="26.4" height="18" class="f-mut" stroke="currentColor"/> <text x="222.4" y="50" fill="currentColor" font-size="12">0.15</text>
<text x="10" y="76" fill="currentColor" font-size="12">byte table (C)</text> <rect x="190" y="62" width="102.2" height="18" class="f-mut" stroke="currentColor"/> <text x="298.2" y="76" fill="currentColor" font-size="12">0.48</text> <text x="10" y="102" fill="currentColor" font-size="12">slicing-by-8 (C)</text> <rect x="190" y="88" width="213.2" height="18" class="f-mut" stroke="currentColor"/> <text x="409.2" y="102" fill="currentColor" font-size="12">2.64</text> <text x="10" y="128" fill="currentColor" font-size="12">crc32c 1-way intrin</text> <rect x="190" y="114" width="300.4" height="18" class="f-ok" stroke="currentColor"/>
<text x="496.4" y="128" fill="currentColor" font-size="12">10.06</text> <text x="10" y="154" fill="currentColor" font-size="12">crc32c 1-way .S</text> <rect x="190" y="140" width="302.0" height="18" class="f-ok" stroke="currentColor"/> <text x="498.0" y="154" fill="currentColor" font-size="12">10.31</text> <text x="10" y="180" fill="currentColor" font-size="12">crc32c 3-way + PMULL</text> <rect x="190" y="166" width="369.4" height="18" class="f-acc" stroke="currentColor"/> <text x="565.4" y="180" fill="currentColor" font-size="12">29.01</text> <text x="10" y="206" fill="currentColor" font-size="12">3-way fold-ahead</text>
<rect x="190" y="192" width="370.4" height="18" class="f-acc" stroke="currentColor"/> <text x="566.4" y="206" fill="currentColor" font-size="12">29.48</text> <text x="10" y="232" fill="currentColor" font-size="12">crc32 1-way (IEEE)</text> <rect x="190" y="218" width="301.5" height="18" class="f-ok" stroke="currentColor"/> <text x="497.5" y="232" fill="currentColor" font-size="12">10.24</text> <text x="10" y="258" fill="currentColor" font-size="12">zlib crc32() (IEEE)</text> <rect x="190" y="244" width="390.3" height="18" class="f-warn" stroke="currentColor"/> <text x="586.3" y="258" fill="currentColor" font-size="12">39.98</text>
<line x1="496.2" y1="30" x2="496.2" y2="288" stroke="currentColor" stroke-width="1.5"/> <text x="496.2" y="300" fill="currentColor" font-size="12" text-anchor="middle">1-way 한계 ≈ 8 B / 3 사이클</text> <line x1="567.8" y1="30" x2="567.8" y2="306" stroke="currentColor" stroke-width="1.5"/> <text x="567.8" y="318" fill="currentColor" font-size="12" text-anchor="middle">CRC 유닛 한계 ≈ 8 B/사이클</text> <text x="10" y="336" fill="currentColor" font-size="12">세로 실선 = 3.9–4.5 GHz 추정 클럭에서의 이론값 가운데(11, 33 GB/s). 막대 길이 = 150·log10(GB/s ÷ 0.1) px.</text>
</svg>
```

**예측과 비교.** add 사슬에서 추정한 클럭은 실행마다 3.9–4.5 GHz 였다. 이 값으로 계산하면:

| 경로 | 모델 | 예측 (3.9–4.5 GHz) | 실측 64 KiB / 1 MiB | 비고 |
|---|---|---|---|---|
| 1-way | 8 B / 3 사이클 (지연) | 10.4–12.0 GB/s | 10.1–10.7 | 모델과 일치 |
| 3-way | 8 B / 1 사이클 (처리량) | 31–36 GB/s | 28.6–29.0 (라운드 최대 33.7) | 예측의 80–95%. combine·루프 오버헤드·부하 |
| 비율 3-way / 1-way | 3 | 3.0 | **2.8–2.9** | |
| 1-way / slicing-8 | — | — | 약 3.8–3.9배 | 테이블 8 KiB 도 필요 없다 |
| 3-way / byte table | — | — | 약 60배 | |

- **1 KiB 에서 3-way 가 17 GB/s 에 그치는 이유**: 384 B 덩어리 2개(768 B)만 3-way 로 돌고, 256 B 는 1-way 로 돈다. combine 도 2 번 들어간다. 짧은 버퍼 비중이 큰 시스템(네트워크 패킷)이면 B 를 더 작게 단계화하거나 다른 방법을 쓴다.
- **zlib 40 GB/s > CRC 유닛 한계 33 GB/s**. CRC 명령만으로는 불가능한 속도라서, 시스템 libz 를 디스어셈블해 보았다(`demo_zlib_disasm.sh`, macOS 27.2 의 `/usr/lib/libz.1.dylib`).

```text
== crc32_z: 108 instructions, first bl -> 0x2d11c9c60
== helper (0x2d11c9c60, first 64 instructions): instruction counts
  20 eor
   6 ld1
   9 pmull
   8 pmull2
== helper loop excerpt
    0x2d11c9c8c <+44>:  ld1.4s { v1, v2, v3 }, [x1], #48
    0x2d11c9c98 <+56>:  ld1.4s { v4, v5 }, [x1], #32
    0x2d11c9ca0 <+64>:  pmull.1q v4, v0, v16
    0x2d11c9ca4 <+68>:  eor.16b v4, v4, v6
    0x2d11c9cb4 <+84>:  pmull2.1q v0, v0, v16
    0x2d11c9cb8 <+88>:  eor.16b v0, v0, v4
```

`crc32_z` 는 16 B 정렬까지 테이블로 처리한 다음 도우미 함수를 부른다. 도우미에는 `crc32x` 가 하나도 없고, 128비트 누산기 4개(v0–v3)를 `pmull`/`pmull2` + `eor` 로 접는다(folding). Intel 의 PCLMULQDQ 방식과 같은 계열로 보인다. PMULL 처리량이 사이클당 1개를 넘는 것으로 보이므로(위 `pmull 4 chains` 대부분 0.66–0.75) CRC 유닛의 8 B/사이클 천장을 넘는다. 이것은 디스어셈블에서 읽은 해석이고, Apple 이 문서화한 내용은 확인하지 못했다(확인 권장). CRC-32C 도 같은 folding 이 가능하고 상수만 다르다. 이 레슨의 "다음 단계"다.

### 복잡도

| 함수 | 시간 | 추가 메모리 | 비고 |
|---|---|---|---|
| `crc32c_raw_hw` / `crc32c_asm` | n/8 명령 + ≤ 6 | 0 | 지연 3 사이클/8 B |
| `crc_mulk` | 상수: fmov·pmull·fmov·crc32cx | 0 | ≈ 10 사이클 지연 (추정) |
| `crc_shift` (임의 n) | O(log n) × gf2_mulmod(32 반복) + 1 곱 | 0 | 상수표를 두면 O(log n) 곱만 |
| `crc32c_3way` | n/8 명령, 처리량 1/사이클 + 덩어리당 곱 2 | 상수 4개 | 384 B 미만은 1-way |
| `crc32c_sgl` | O(n) + cnt × O(log len) | 0 | 세그먼트 CRC 는 병렬 가능 |
| `stm32_crc_hw` | 워드당 rbit + crc32w | 0 | 연습 3: 2 워드씩 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

<!-- code: solution.S diff=starter.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c solution.S shared_ref.c shared_bugs.c -lz -o /tmp/ARM-A06_solution && /tmp/ARM-A06_solution
# ... 42/42 passed   (0.17 s)
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c solution.S shared_ref.c shared_bugs.c -lz -o /tmp/ARM-A06_asan && /tmp/ARM-A06_asan
# ... 42/42 passed   (0.33 s, 경고 없음)
clang --target=aarch64-none-elf -c solution.S     # ELF 문법으로도 어셈블된다 (SYM/FTYPE 매크로)
```

정답 실행의 정보 줄(실제 출력):

```text
check("123456789"): bitwise IEEE=0xCBF43926  C=0xE3069283  | crc32_arm=0xCBF43926 crc32c_arm=0xE3069283 crc32c_asm=0xE3069283
len 0..64 x align 0..15: crc32_arm vs zlib bad=0, crc32c_arm vs bitwise bad=0, crc32c_asm bad=0
page-end (len 0..64 ending at a PROT_NONE page): crc32c_arm bad=0, crc32c_asm bad=0
combine: crc32c vs one-shot bad=0, crc32 vs zlib crc32_combine bad=0
3-way: len 0..4096 (all) + 200 random up to 256 KiB: bad=0
bugs (mismatches, strong / weak suite): no_invert=2224/556 ieee_body=2044/378 bswap_body=2046/376 tail_no1=1117/287 3way_no33=813/0 3way_swapk=812/0
mpeg2 check("123456789") = 0x0376E6E7
stm32_crc_hw({0x12345678}) = 0xDF8A8A2B
42/42 passed
```

## 7. 자주 하는 실수

1. **명령이 zlib 과 같은 값을 낸다고 믿는다.** `__crc32cd` 는 조건화를 하지 않는다. `~` 를 빼면 길이가 1 이상인 모든 입력이 틀린다(`bug_no_invert` 2224/2224 중 n = 0 만 맞음). 고친 코드: `return ~crc32c_raw_hw(~crc, p, n);`
2. **`crc32x` 와 `crc32cx` 혼동.** 이름이 한 글자 차이다. 머리·꼬리만 쓰는 짧은 테스트(n < 8)로는 안 잡힌다. check 값 하나("123456789", 9 B → 본문 1번)가 가장 싼 방어선이다.
3. **8 바이트를 "네트워크 순서"로 읽는다.** CRC 는 **메모리 순서**로 먹어야 하고, 리틀 엔디언 `ldr` 이 정확히 그 순서다. `bswap` 을 넣으면 틀린다(`bug_bswap_body`). 빅 엔디언 AArch64(BE8) 빌드라면 반대로 `rev` 가 필요할 것이다(확인 권장).
4. **꼬리를 "8 B 읽고 마스크"로 처리한다.** 값은 맞아도 p+n 너머를 읽는다. 페이지 끝에서 SIGBUS/SIGSEGV 가 나고 ASan 도 잡는다. 4·2·1 B 로 나눠 먹인다.
5. **시프트 상수에서 x^33 을 빠뜨리거나 k1/k2 를 바꾼다.** 384 B 미만 테스트는 3-way 경로를 안 타서 **0 개**를 잡는다. 길이 전수 테스트가 경로 전환 지점을 넘어야 한다.
6. **n < 5 를 일반식에 넣는다.** `8*n - 33` 이 `uint64_t` 로 감싼다. 4 바이트 이하는 `crc32cb(c, 0)` 반복으로 처리한다.
7. **gcc 에서 intrinsic 을 target 없는 함수에서 부른다.** 맥 clang 은 기본 타깃에 CRC/AES 가 있어 통과하지만, VM 의 gcc 15.2(generic armv8-a)는 실패한다:
   ```text
   arm_acle.h:266:1: error: inlining failed in call to 'always_inline' '__crc32cd': target specific option mismatch
   ```
   테스트 코드의 `main` 에서 직접 부른 `__crc32cw`, `vmull_p64` 가 원인이었다. 고친 방법: `HW`(`target("+crc,+aes")`) 를 붙인 작은 도우미 함수로 옮긴다(`pmull64`, `crc32c_zero4/8`). 런타임에는 `getauxval(AT_HWCAP) & HWCAP_CRC32` / `HWCAP_PMULL`(리눅스) 또는 `hw.optional.arm.FEAT_CRC32`(맥)로 확인하고 분기한다.
8. **벤치마크 타이머 해상도.** 처음 만든 벤치는 `CLOCK_MONOTONIC` 으로 2 MiB 표본을 쟀다. 그러자 여러 행이 똑같은 `10.92` GB/s 를 냈다(마이크로초 단위로 양자화된 것으로 보인다). `clock_gettime_nsec_np(CLOCK_UPTIME_RAW)`(24 MHz 틱 = 41.7 ns)로 바꾸고 표본을 8 MiB 로 키워 해결했다. 수치가 이상하게 똑같으면 타이머부터 의심한다.

## 8. 면접 Q&A

<details>
<summary>Q1. CRC32X 와 CRC32CX 는 무엇이 다르고, 어떤 프로토콜이 어느 쪽을 쓰나? x86 은?</summary>

다항식만 다르다. CRC32X 는 0x04C11DB7(이더넷·zlib·PNG 의 CRC-32), CRC32CX 는 0x1EDC6F41(Castagnoli: iSCSI, NVMe/TCP 다이제스트, ext4/Btrfs 메타데이터, SCTP)이다. 둘 다 반사형이고 조건화는 하지 않는다. x86 SSE4.2 의 `crc32` 명령은 32C 전용이라서 zlib CRC-32 는 PCLMULQDQ folding 으로 가속한다. 새 저장장치·네트워크 프로토콜이 32C 를 많이 고른 데는 하드웨어 지원과, 같은 길이에서의 오류 검출 거리(Castagnoli 논문)가 이유로 꼽힌다(확인 권장).

</details>

<details>
<summary>Q2. 명령이 있는데 왜 3-way 로 나누나? 왜 하필 3인가?</summary>

1-way 루프는 매 명령이 앞 결과를 기다리므로 **지연**(≈ 3 사이클)에 묶인다. 유닛은 매 사이클 새 명령을 받을 수 있다(처리량 1). 동시에 진행 중이어야 하는 명령 수는 지연 × 처리량 = 3 이다(Little 의 법칙). 실측도 사슬 1/2/3/4/8 개에서 2.94/1.32/0.88/0.93/0.95 사이클이 나와, 3에서 포화했다. 처리량은 2.8–2.9배(10 → 29 GB/s)가 됐다. 다른 코어에서는 지연·처리량을 다시 재서 N 을 정한다. 예를 들어 CRC 유닛이 2개면 6-way 가 필요하다.

</details>

<details>
<summary>Q3. 세 결과를 어떻게 합치나? x^33 은 어디서 나오나?</summary>

`c = c0·x^(16B) ⊕ c1·x^(8B) ⊕ c2` 로 합친다. 곱셈은 `PMULL` 로 c·k(63비트)를 구하고 `crc32cx(0, ·)` 로 mod P 를 얻는다. 명령은 64비트 입력에 x^32 을 곱한다. 63비트 곱을 64비트 슬롯에 넣으면 반사 표현에서 x 가 하나 더 붙는다. 그래서 합이 x^33 이고, 상수는 k = x^(8n−33) mod P 로 미리 줄여 둔다. 하드웨어 명령 하나가 Barrett 축소를 대신해 주는 셈이다. 꼬리질문 "n < 5 는?" → 지수가 음수가 되니 0 바이트를 명령으로 직접 먹인다.

</details>

<details>
<summary>Q4. 조건화(init ~0, xorout ~0)는 왜 있고, combine 에서는 왜 문제가 안 되나?</summary>

init ~0 이 없으면 메시지 앞의 0 바이트가 CRC 를 바꾸지 않는다(상태 0 에 0 을 넣으면 0). 그래서 앞쪽 0 의 개수 오류를 못 잡는다. xorout ≠ 0 이면 잔여 레지스터가 0 이 아니게 된다. 그래서 "올바른 프레임 + CRC" 뒤에 0 바이트가 더 붙으면 잔여값 검사가 실패한다. xorout = 0 이면 그대로 통과해 버린다(ALGO-M01 연습 2 실측). combine 식 `shift(crc(A), |B|) ⊕ crc(B)` 를 전개하면 init 과 xorout 이 만드는 상수항 `shift(~0)` 이 두 번 나와 상쇄된다. 그래서 조건화된 값끼리 바로 합칠 수 있다. 조건화 없는 내부 상태로 계산하는 3-way 에서는 c1, c2 를 0 에서 시작하면 된다.

</details>

<details>
<summary>Q5. AArch64 는 비정렬 로드가 되는데 정렬 머리가 왜 필요한가?</summary>

맥 M6 의 큰 버퍼에서는 p+1 비정렬 시작과 정렬 시작의 차이가 노이즈 수준이었다(28.1 vs 29.0 GB/s, 1 MiB 에서는 오히려 29.2). 그래도 머리를 두는 이유는 셋이다. (1) MMU 를 켜기 전이나 Device 메모리, `SCTLR.A=1`, `-mstrict-align` 환경에서는 비정렬 접근이 fault 를 낸다(ARM-M13). 부트로더의 이미지 검사가 바로 이 환경이다. (2) 일부 코어는 캐시 라인을 걸치는 로드가 느리다. (3) 3-way 는 세 스트림이 모두 같은 정렬을 갖게 된다. 꼬리는 성능이 아니라 **안전** 문제다. p+n 을 넘어 읽으면 안 된다(페이지 끝 테스트).

</details>

<details>
<summary>Q6. 이 3-way 설계가 틀리거나 손해인 경우는? (staff)</summary>

(1) **짧은 버퍼**: 384 B 미만은 이득이 0 이다. 1 KiB 도 17 GB/s 로 zlib(25)보다 느렸다. 패킷 처리라면 B 를 32/64 로 더 쪼개거나, 여러 패킷을 동시에 처리하는 "패킷 간 인터리브"가 낫다. (2) **PMULL 이 없는 코어**(crypto 확장 없는 SoC): 시프트를 테이블로 대신한다. x^(8B) 곱은 32비트 상태의 바이트 4개마다 256 엔트리 표 4개(4 KiB)로 선형 변환할 수 있다. (3) **in-order 코어**: clang 이 체인을 펼쳐 이어 놓으면 섞이지 않을 수 있다. 명령을 번갈아 놓은 asm 이 필요하다. (4) **임계 경로**: 다음 덩어리의 c0 가 combine 을 기다린다. fold-ahead(연습 2)로 겹친다. (5) **메모리 대역**: 아주 큰 버퍼가 DRAM 에서 오면 30 GB/s 는 코어 하나의 대역 한계와 겹칠 수 있다(이 측정은 1 MiB 라 캐시 안).

</details>

<details>
<summary>Q7. zlib 이 40 GB/s 를 낸다. 8 B/사이클 천장을 어떻게 넘었고, 더 빠르게 하려면 어떻게 바꾸겠나? (staff)</summary>

시스템 libz 를 디스어셈블해 보니 큰 버퍼 경로에 `crc32x` 가 없었다. 128비트 누산기 4개를 `pmull`/`pmull2` + `eor` 로 접고 있었다(PCLMULQDQ 백서와 같은 folding 계열로 해석, 확인 권장). 한 번에 64 B 를 접고 마지막에 축소한다. PMULL 은 사이클당 1개 넘게 받는 것으로 보이므로(4 사슬 실측 대부분 0.66–0.75 사이클/명령, 지연에 묶인 값) CRC 유닛 천장을 넘는다. 바꾼다면 (a) CRC-32C 도 상수만 바꿔 PMULL folding 을 쓰거나, (b) CRC 명령 체인과 PMULL folding 을 **동시에** 돌려 두 유닛을 함께 쓰는 하이브리드를 만들 수 있다. 두 실행 유닛이 독립이면 합이 될 수 있다(이 맥에서는 안 재 봄). (c) SVE2/SME 의 더 넓은 다항식 곱도 후보다. 어느 쪽이든 결과는 이 레슨의 테스트 묶음으로 1-way 와 대조한다.

</details>

<details>
<summary>Q8. Cortex-M 펌웨어에서는 어떻게 하나? 호스트 툴과 값이 안 맞으면?</summary>

M-profile 에는 CRC32 명령이 없다(어셈블러가 M7/M33/M55/M85 모두 `requires: crc` 로 거부, Arm 문서로도 확인 권장). 선택지는 세 가지다. (1) 칩의 CRC 유닛 + DMA: CPU 부하가 거의 없고 플래시 검사에 적합하다. (2) 바이트 테이블 1 KiB / 니블 64 B(ALGO-M02). (3) 여러 칩이 같은 값을 내야 하면 소프트웨어 표준 CRC 를 쓴다. STM32F1/F4 CRC 유닛의 기본값은 zlib CRC-32 가 **아니라** MPEG-2 계열이다. 그래서 이미지 생성 툴(호스트, AArch64 라면)은 `rbit` + `crc32w` 로 같은 값을 낸다(Part C). 값이 안 맞으면 다섯 파라미터(poly/init/refin/refout/xorout)와 워드 공급 순서를 먼저 의심한다. Cortex-R52 는 CRC32 명령이 있으므로(어셈블러 OK) R 계열 SSD 컨트롤러라면 이 레슨 코드를 AArch32 로 옮길 수 있다.

</details>

<details>
<summary>Q9. 3-way 코드가 맞는지 어떻게 테스트하겠나? 버그가 숨는 곳은?</summary>

경로가 바뀌는 지점을 모두 지나야 한다. 머리(주소 % 8 = 0..7), 본문, 꼬리(n % 8), 3-way 소(384 B)·대(3072 B) 덩어리, 덩어리 경계 ±1 이 그 지점이다. 그래서 길이 0..4096 **전수** × 랜덤 오프셋·시작 crc, 큰 랜덤 길이, 스트리밍 조각, 페이지 끝을 돈다. 기준은 독립적인 구현 두 개(비트 단위, slicing-8)와 zlib 이다. 같은 묶음이 버그 6종을 다 잡는 것, 그리고 약한 묶음(≤ 300 B)이 상수 버그를 **못 잡는다**는 것도 CHECK 로 고정했다. 그래야 누가 테스트를 줄여도 드러난다. 상수표는 `xpow_mod` 로 재계산해 비교한다.

</details>

## 9. 추가 연습

### 연습 1 — CRC-32(IEEE) 3-way, zlib 과 대조

`crc32_3way(crc, p, n)` 을 만든다. 구조는 CRC-32C 판과 같고 `__crc32d` 와 IEEE 상수만 다르다. 상수는 직접 계산하고, 테스트가 `xpow_mod` 로 확인하게 한다.

<details>
<summary>답</summary>

B = 1024: k1 = 0xBBF2F6D6, k2 = 0x7B4AA8B7. B = 128: k1 = 0x910EEEC1, k2 = 0xE95C1271 (x^(8B−33), x^(16B−33) mod P_ieee). `crc_mulk` 의 축소도 `__crc32d` 로 바꿔야 한다. 축소 다항식이 곱셈 다항식과 같아야 하기 때문이다. 길이 0..4096 전수 + 200 KiB 까지 랜덤 100개, zlib `crc32()` 과 불일치 0, check 0xCBF43926.

</details>

### 연습 2 — fold-ahead 3-way

덩어리마다 세 체인을 **모두 0** 에서 시작하고, 이어받은 상태는 `c·x^(24B)` 로 한 번에 민다: `c = c·x^(24B) ⊕ c0·x^(16B) ⊕ c1·x^(8B) ⊕ c2`. 무엇이 좋아지나?

<details>
<summary>답</summary>

k3 = x^(24B−33): B = 1024 → 0x359674F7, B = 128 → 0xD270F1A2. 다음 덩어리의 체인이 이번 combine 을 기다리지 않는다. 임계 경로에는 `c ← mulk(c) ⊕ …` 하나만 남고, 덩어리마다 곱이 1번 늘어난다. 실측(`bench_m6.txt`)에서 1 KiB 는 16.9 → 18.2(run 1), 15.9 → 17.0(run 2) GB/s 로 약간 빨라졌다. 64 KiB·1 MiB 는 노이즈 안에서 같았다(29.0 vs 29.5). 덩어리가 클수록 combine 지연의 비중이 작기 때문이다.

</details>

### 연습 3 — STM32 CRC 를 워드 2개씩

`crc32x` 하나로 STM32 워드 2개를 처리하라. 어떤 64비트 값을 넣어야 하나?

<details>
<summary>답</summary>

`__rbitll(((uint64_t)w[i] << 32) | w[i+1])` 를 넣는다. `crc32x` 는 비트 0 부터 먹는데, rbit 후 비트 0 = 원래 비트 63 = w[i] 의 비트 31 이다. 그래서 w[i] 의 MSB 가 가장 먼저 들어가고, 이어서 w[i+1] 의 MSB 가 들어간다. 홀수 개면 마지막은 `crc32w(s, rbit(w))` 로 처리한다. 워드 0..33개 랜덤 1000 개에서 워드 단위판·비트 단위 MPEG-2 와 불일치 0 이었다.

</details>

### 연습 4 — CRC-32C 잔여값(residue)으로 수신 검사

메시지 뒤에 CRC-32C 를 리틀 엔디언으로 붙인 전체의 CRC 는 메시지와 무관한 상수다. 그 상수를 구하고, 1비트 오류가 항상 잡히는지 확인하라.

<details>
<summary>답</summary>

실측 `0x48674BC7` 이다. 이것은 RevEng CRC-32/ISCSI 의 residue 0xB798B438 의 `~`(xorout 반영)다. 랜덤 메시지 500개(0..1999 B)가 모두 이 값이었다. CRC 필드를 포함해 아무 1비트를 뒤집으면 500/500 모두 값이 바뀌었다. 수신 측은 "CRC 를 다시 계산해 필드와 비교" 대신 "전체 CRC == 상수" 하나로 끝난다. 3-way 나 DMA 엔진이 필드까지 한 번에 흘려 계산할 때 편하다.

</details>

### 연습 5 — 명령만으로 시프트 (O(n)) vs PMULL (O(log n))

`crc32cx(c, 0)` 은 x^64 곱이다. 이것만 반복해서 `shift_linear(c, n)` 을 만들고, PMULL 판과 n = 0..2000 에서 같은지 확인하라. 언제 이쪽이 낫나?

<details>
<summary>답</summary>

`for (; n >= 8; n -= 8) c = crc32cx(c, 0); while (n--) c = crc32cb(c, 0);` 이다. 두 다항식 모두 n = 0..2000 에서 PMULL 판과 같았다. 비용은 n/8 × 3 사이클(지연)이다. n 이 수십 바이트 이하이거나 PMULL 이 없는 코어라면 이쪽이 단순하고 충분하다. 반대로 SGL 처럼 len 이 수 KiB 이상이면 O(log n) 판(또는 고정 상수)을 쓴다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c shared_ref.c shared_bugs.c -lz -o /tmp/ARM-A06_practice && /tmp/ARM-A06_practice
# residue of msg||crc_le: 0x48674BC7 (want 0x48674BC7), single-bit errors missed: 0/500
# 13/13 passed   (ASan/UBSan 빌드도 13/13, VM 의 gcc 15.2·clang 21.1.8 빌드도 13/13)
```

## 10. 핵심 정리

- `crc32(c)b/h/w/x` = 반사형 CRC 레지스터 갱신이고 조건화는 없다. zlib 모양은 `~raw(~crc)`. check: CRC-32 0xCBF43926, CRC-32C 0xE3069283.
- 1-way 구조: 머리(주소 비트 0·1·2 → 1·2·4 B) → `ldr x; crc32cx` 본문 → 꼬리(n 비트 2·1·0). p+n 너머는 절대 읽지 않는다(페이지 끝 테스트).
- `crc32cx` 지연 ≈ 3, 처리량 ≈ 1/사이클(M6 실측). 1-way 는 지연에 묶여 ~10 GB/s, 3-way 는 처리량에 묶여 ~29 GB/s(2.8–2.9배)다.
- 합치기 = 0 바이트 먹이기 = x^(8n) 곱. `crc32cx(0, pmull(c, k))` = c·k·x^33 이므로 k = x^(8n−33). n < 5 는 명령으로 0 을 먹인다.
- combine: `crc(A‖B) = shift(crc(A),|B|) ⊕ crc(B)` 로 3-way, SGL 세그먼트, 병렬 DMA 에 같은 식을 쓴다.
- 테스트는 경로 전환 지점(8, 384, 3072 B)을 모두 넘는 길이 전수로 한다. 약한 테스트는 상수 버그를 0 개 잡는다.
- CRC 명령의 천장은 8 B/사이클이다. 시스템 zlib 은 PMULL folding 으로 40 GB/s 를 낸다(디스어셈블 확인). M-profile 에는 명령이 없으므로 CRC 유닛, 테이블, 호스트 툴의 `rbit` 기법을 쓴다.

## 참고자료

- Arm Architecture Reference Manual for A-profile (DDI 0487) — CRC32B/H/W/X, CRC32CB/CH/CW/CX (FEAT_CRC32), PMULL/PMULL2 (FEAT_PMULL): <https://developer.arm.com/documentation/ddi0487/latest>
- Arm C Language Extensions (ACLE) — `__crc32*`, `__crc32c*`, `__rbit`, `__ARM_FEATURE_CRC32`: <https://github.com/ARM-software/acle>
- Arm Intrinsics (`vmull_p64`): <https://developer.arm.com/architectures/instruction-sets/intrinsics/>
- V. Gopal et al., Intel, *Fast CRC Computation for Generic Polynomials Using PCLMULQDQ Instruction* (2009) — folding, 상수 x^(k)·mod P 유도.
- V. Gopal et al., Intel, *Fast CRC Computation for iSCSI Polynomial Using CRC32 Instruction* (2011) — 3-way 스트림 + combine (제목 기억 기준, 확인 권장).
- G. Castagnoli, S. Bräuer, M. Herrmann, *Optimization of Cyclic Redundancy-Check Codes with 24 and 32 Parity Bits*, IEEE Trans. Commun. 1993.
- RevEng CRC catalogue (CRC-32/ISO-HDLC, CRC-32/ISCSI, CRC-32/MPEG-2 의 check·residue): <https://reveng.sourceforge.io/crc-catalogue/17plus.htm>
- zlib `crc32.c` (`crc32_combine`, x^(2^k) 표): <https://github.com/madler/zlib/blob/master/crc32.c>
- RFC 3720 (iSCSI, CRC32C 다이제스트): <https://www.rfc-editor.org/rfc/rfc3720>
- Linux kernel, ARM64 ELF hwcaps (`HWCAP_CRC32`, `HWCAP_PMULL`): <https://docs.kernel.org/arch/arm64/elf_hwcaps.html>
- Apple, Determining instruction set characteristics (`hw.optional.arm.FEAT_CRC32`): <https://developer.apple.com/documentation/kernel/1387446-sysctlbyname/determining_instruction_set_characteristics>
- ST, AN4187 *Using the CRC peripheral in the STM32 family* (문서 번호 기억 기준, 확인 권장).
- 관련 레슨: ALGO-M01 (비트 단위 CRC, 파라미터 모델), ALGO-M02 (테이블·slicing-8·combine 증명, STM32 MPEG-2), ARM-M03 (지연/처리량 측정법), ARM-M06 (NEON·SDOT 처리량), ARM-M13 (MMU-off = Device 메모리, 비정렬 fault), ARM-A05 (int8 GEMM, 같은 "지연 vs 처리량" 사고), ARM-A11 (컴파일러 출력 읽기)
