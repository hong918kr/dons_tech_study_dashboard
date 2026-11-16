---
id: BM-A04
time: 110분
updated: 2026-11-15
---
# BM-A04 펌웨어 이미지 헤더 + CRC/SHA-256 검증

## 학습 목표

- 부트로더가 믿을 수 있는 64 B 이미지 헤더(magic, 헤더 버전, 이미지 버전, 크기, load 주소, 플래그, CRC-32, SHA-256, 헤더 CRC)를 설계하고, **필드마다 왜 있는지**를 말할 수 있다.
- SHA-256 을 FIPS 180-4 대로 직접 구현하고(압축 함수 + 패딩), FIPS 벡터와 python `hashlib` 교차검증(길이 0..300 전부)으로 맞았음을 보인다.
- `img_verify()` 를 "싼 검사 → 비싼 검사, 필드는 CRC 확인 뒤에만 믿는다, `64 + size` 같은 덧셈은 넘친다" 원칙으로 짜고, **모든 단일 비트 플립 / 모든 길이 잘림 / 롤백 / 거대한 size** 가 정해진 코드로 거부됨을 테스트로 보인다.
- CRC(우연한 오류) · SHA-256(의도적 변경 탐지, 단 저장된 해시는 누구나 다시 계산 가능) · 서명(진위) 의 차이를 **실제 CRC 위조 공격**으로 설명한다.
- 두 슬롯 중 무엇으로 부팅할지 정하고, 현장 엔지니어가 읽을 부트 로그를 남긴다.

## 1. 문제

펌웨어 업데이트 면접의 첫 질문은 거의 항상 **"부트로더는 이 이미지가 멀쩡하다는 걸 어떻게 아나?"** 다. 꼬리로 "CRC 면 충분한가?", "해시랑 서명은 뭐가 다른가?", "헤더가 깨졌는데 size 필드를 믿으면?", "옛 버전으로 되돌리는 공격은?" 이 따라온다. Don 이 SSD 펌웨어에서 FTL 메타데이터를 "헤더 CRC → 필드 사용" 순서로 읽고, 다운로드 커맨드로 받은 FW 이미지를 활성화 전에 검증하던 것과 같은 문제다 — 다만 이번엔 **부팅 자체**가 그 판정에 걸려 있다.

이 레슨은 맥에서 돈다(C + python). 헤더 포맷은 **이 레슨이 소유**하고, BM-A05(YMODEM)·BM-A06(전원 차단 내성 업데이트)·BM-A11(종합)이 축약판을 그대로 쓴다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">img_hdr_t — 64 B, little-endian, 슬롯 맨 앞 (payload 가 바로 뒤)</text>
  <rect x="10" y="34" width="70" height="44" class="f-acc" stroke="currentColor"/>
  <text x="15" y="52" fill="currentColor">0 magic</text>
  <text x="15" y="68" fill="currentColor">"BIMG"</text>
  <rect x="80" y="34" width="70" height="44" class="f-mut" stroke="currentColor"/>
  <text x="85" y="52" fill="currentColor">4 hdr_ver</text>
  <text x="85" y="68" fill="currentColor">u16 = 1</text>
  <rect x="150" y="34" width="72" height="44" class="f-mut" stroke="currentColor"/>
  <text x="155" y="52" fill="currentColor">6 hdr_size</text>
  <text x="155" y="68" fill="currentColor">u16 = 64</text>
  <rect x="222" y="34" width="100" height="44" class="f-ok" stroke="currentColor"/>
  <text x="227" y="52" fill="currentColor">8 img_version</text>
  <text x="227" y="68" fill="currentColor">M&lt;&lt;24|m&lt;&lt;16|p</text>
  <rect x="322" y="34" width="108" height="44" class="f-ok" stroke="currentColor"/>
  <text x="327" y="52" fill="currentColor">12 payload_size</text>
  <text x="327" y="68" fill="currentColor">bytes</text>
  <rect x="430" y="34" width="92" height="44" class="f-ok" stroke="currentColor"/>
  <text x="435" y="52" fill="currentColor">16 load_addr</text>
  <text x="435" y="68" fill="currentColor">0x08020000</text>
  <rect x="522" y="34" width="70" height="44" class="f-ok" stroke="currentColor"/>
  <text x="527" y="52" fill="currentColor">20 flags</text>
  <text x="527" y="68" fill="currentColor">bit0 SHA</text>
  <rect x="592" y="34" width="100" height="44" class="f-ok" stroke="currentColor"/>
  <text x="597" y="52" fill="currentColor">24 payload</text>
  <text x="597" y="68" fill="currentColor">crc32 (zlib)</text>
  <rect x="10" y="90" width="560" height="40" class="f-ok" stroke="currentColor"/>
  <text x="18" y="114" fill="currentColor">28 sha256[32] — SHA-256(payload), FIPS 180-4 (big-endian 출력 바이트 그대로)</text>
  <rect x="570" y="90" width="90" height="40" class="f-warn" stroke="currentColor"/>
  <text x="576" y="108" fill="currentColor">60 hdr_crc32</text>
  <text x="576" y="123" fill="currentColor">CRC([0,60))</text>
  <path d="M10 146 L570 146" stroke="currentColor"/>
  <path d="M10 140 L10 152 M570 140 L570 152" stroke="currentColor"/>
  <text x="200" y="166" fill="currentColor">hdr_crc32 가 덮는 범위 [0, 60)</text>
  <rect x="10" y="184" width="200" height="40" class="f-mut" stroke="currentColor"/>
  <text x="18" y="208" fill="currentColor">헤더 64 B</text>
  <rect x="210" y="184" width="380" height="40" class="f-acc" stroke="currentColor"/>
  <text x="218" y="202" fill="currentColor">payload = 앱 바이너리 (word0 = 초기 SP,</text>
  <text x="218" y="217" fill="currentColor">word1 = 리셋 벡터 | 1) — payload_crc32 / sha256 대상</text>
  <rect x="590" y="184" width="140" height="40" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="598" y="208" fill="currentColor">0xFF (지워진 슬롯)</text>
  <text x="10" y="250" fill="currentColor">초록 = 헤더 CRC 를 확인한 뒤에만 믿는 필드 / 노랑 = 헤더 자신의 CRC</text>
  <text x="10" y="270" fill="currentColor">magic·hdr_ver·hdr_size 는 CRC 보다 먼저 본다: 포맷을 알아야 CRC 위치(60)를 안다</text>
  <text x="10" y="290" fill="currentColor">_Static_assert(sizeof(img_hdr_t) == 64) — 패딩 없음 (자연 정렬로 딱 맞게 배치)</text>
</svg>
```

설계 결정 (이 레슨의 정의):

- **load_addr 는 "payload 가 놓여야 할 실행 주소"** 이고, 이 보드에선 내부 플래시 0x08020000(F4 섹터 5–7, 384 KB — RM0090 기준 대표값, 확인 권장) 하나뿐이다. 슬롯(외부 SPI NOR, COMM-A06)에 있는 이미지는 검증 후 그 주소로 **복사**된다. 64 B 헤더 바로 뒤의 payload 는 VTOR 정렬(벡터 테이블 크기를 2 의 거듭제곱으로 올린 값, 최소 128 B 이상 — ARMv7-M ARM 확인 권장)을 만족할 수 없으므로 슬롯에서 직접 실행(XIP)하는 설계가 아니다. XIP 를 원하면 MCUboot 처럼 헤더를 0x200 등으로 패딩한다(Q&A 4).
- **img_version = major<<24 | minor<<16 | patch** — 정수 비교가 곧 사전식 비교가 되게(연습 1). build 번호 없음.
- **min_version(롤백 카운터)** 은 이미지 밖에 산다 — OTP / eFuse / 쓰기 보호된 플래시 영역(연습 5). 여기서는 `img_verify` 의 인자.

### Part A — SHA-256 직접 구현 (CRC-32 는 주어짐)

- `sha256_compress(h[8], blk[64])` (TODO A1) — 메시지 스케줄 W[0..63] + 64 라운드.
- `sha256_final(ctx, out[32])` (TODO A2) — 0x80, 0 채움, 비트 길이 big-endian 8 B, 블록이 하나 더 필요한 경우.
- 검증: "abc", "", 448 비트 메시지, 백만 개의 'a', python `hashlib` 이 만든 체인 상수(길이 0..300 각각의 다이제스트를 이어 붙여 다시 해시), 큰 버퍼 6 개, 무작위 조각 200 번 = 한 번에.

### Part B — `img_verify()` 와 결함 주입

- `img_verify(slot, slot_len, min_version, &out)` (TODO B1) → `OK / ERR_ARG / EMPTY / BAD_MAGIC / BAD_HDR_VERSION / BAD_HDR_CRC / BAD_FLAGS / BAD_SIZE / TOO_BIG / BAD_LOAD_ADDR / ROLLBACK / BAD_CRC / BAD_SHA`.
- 주입: 320 B 이미지의 **모든 단일 비트 플립 2560 개**, **모든 길이에서 잘림**(나머지 0xFF / 버퍼 자체가 짧음 — ASan 으로 범위 밖 읽기 감시), 롤백(같음/작음/1.10 vs 1.9), `payload_size` = 0xFFFFFFFF·0xFFFFFFC0(64 를 더하면 0)·0xFFFFFFC1(=1), 실행 영역 384 KB 경계, 모르는 flag·SHA 빼기, 미래 헤더 버전, 저장된 SHA 256 비트 각각, **CRC 를 보존한 악의적 변경**, 무작위 결함 3000 개, python 이 만든 이미지.

### Part C — 두 슬롯 부트 결정 + 로그

- `boot_select(slots[2], min_version, log, cap, &chosen)` (TODO C1) — OK 인 슬롯 중 높은 버전, 같으면 A, 없으면 −1(부트로더에 남아 DFU). 6 가지 슬롯 상태 × 6 = 36 조합 전수, 현장 로그 문자열, 로그 버퍼가 24 B 여도 결정은 같고 넘치지 않음.

## 2. Problem (English)

> **Design and verify a firmware image header for a Cortex-M bootloader.**
>
> The image is `[64-byte header][payload]`, little-endian: `magic=0x474D4942 ("BIMG")`, `hdr_version=1`, `hdr_size=64`, `img_version=major<<24|minor<<16|patch`, `payload_size`, `load_addr`, `flags` (bit0 = SHA-256 present; other bits must be 0), `payload_crc32` (CRC-32/ISO-HDLC), `sha256[32]`, `hdr_crc32 = CRC-32(header[0..60))`.
>
> 1. Implement SHA-256 (FIPS 180-4) without a library: `sha256_init/update/final`. Pass the FIPS vectors and match Python `hashlib` for every length 0–300.
> 2. `img_rc_t img_verify(const uint8_t *slot, uint32_t slot_len, uint32_t min_version, img_info_t *out)` returns the **first** failing check in this order: empty slot, magic, header version/size, header CRC, flags, size (`8 ≤ payload_size ≤ slot_len − 64`, overflow-safe), `payload_size ≤ EXEC_MAX` (384 KB), `load_addr == 0x08020000`, anti-rollback (`img_version ≥ min_version`), payload CRC, payload SHA-256 (constant-time compare). Never read outside `slot[0, slot_len)`.
> 3. `int boot_select(const boot_slot_t s[2], uint32_t min_version, char *log, size_t cap, img_info_t *chosen)`: boot the valid slot with the highest version (tie → slot A), or return −1. Write a bounded, human-readable log.
> 4. A Python packer `mkimg.py` (stdlib only) must produce byte-identical images to the C packer, and each side must verify the other's output.
>
> Example: a slot whose header says `payload_size = 0xFFFFFFC0` with a valid header CRC → `BAD_SIZE` (a naive `64 + size > slot_len` wraps to 0 and passes). Flipping any single payload bit → `BAD_CRC`. Changing the reset vector and fixing 4 other bytes so the CRC-32 is unchanged → `BAD_SHA`.
>
> Follow-ups: Why is CRC not enough? If an attacker can rewrite the header, does SHA-256 help? Where does `min_version` live? How long does SHA-256 of 384 KB take on a 168 MHz Cortex-M4?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/BM-A04_st && /tmp/BM-A04_st
# 처음엔 "11/60 passed", exit 1 — CRC·이미지 만들기 같은 주어진 부분만 통과. TODO 4 곳을 채우면 60/60
```

TODO 는 4 곳: **A1** `sha256_compress`, **A2** `sha256_final`, **B1** `img_verify`, **C1** `boot_select`. CRC-32(zlib 의미의 `crc32_update`), 헤더 직렬화(`img_hdr_load/store/seal`), `img_build`, 로그 도우미(`log_printf`, `log_slot`)는 주어진다. 스텁은 안전한 오답(다이제스트 0, `ERR_ARG`, −1)을 돌려주고 루프는 모두 유한하다.

함께 쓰는 파일 (레슨 폴더에서 실행):

```bash
python3 -I mkimg.py vectors                      # C 하네스에 박힌 hashlib/zlib 교차검증 상수 재생성
python3 -I mkimg.py fixture                      # C 하네스의 PY_FIXTURE (python 이 만든 264 B 이미지)
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-A04_so
/tmp/BM-A04_so emit /tmp/BM-A04_c && python3 -I mkimg.py verify /tmp/BM-A04_c.img        # C 가 만든 것 → python 검증
python3 -I mkimg.py pack --ver 1.3.0 /tmp/BM-A04_c.payload /tmp/BM-A04_py.img && cmp /tmp/BM-A04_c.img /tmp/BM-A04_py.img
/tmp/BM-A04_so verify /tmp/BM-A04_py.img 0x01020000                                    # python 이 만든 것 → C 검증
/tmp/BM-A04_so emitmut /tmp/BM-A04_mut.bin && python3 -I mkimg.py batch /tmp/BM-A04_mut.bin   # 변이 2000 개 판정 비교
cc -std=c11 -Wall -Wextra -Werror -O2 demo_cost.c -o /tmp/BM-A04_demo && /tmp/BM-A04_demo    # 비용·플립 지도·패딩
```

## 4. 힌트

<details>
<summary>힌트 1 — A1: 스케줄과 라운드는 식 그대로</summary>

W[0..15] 는 블록을 **big-endian** 워드로 읽은 것(SHA-2 는 빅엔디언 세계다 — 헤더는 리틀엔디언이니 섞지 말 것). σ0 = ROR7^ROR18^**SHR**3, σ1 = ROR17^ROR19^**SHR**10 — 마지막 항만 회전이 아니라 시프트다. 라운드 끝의 레지스터 밀기는 `h=g; g=f; f=e; e=d+T1; d=c; c=b; b=a; a=T1+T2` 순서. 모든 덧셈은 `uint32_t` 로 mod 2³².

</details>

<details>
<summary>힌트 2 — A2: 블록이 하나 더 필요한 경계</summary>

0x80 을 넣은 뒤 버퍼에 남은 자리가 8 바이트(길이 필드)보다 적으면 = `nbuf > 56` 이면, 0 으로 채워 압축하고 새 블록에서 다시 시작한다. 블록 수 = ⌈(n + 9) / 64⌉ — n = 55 → 1, 56 → 2, 119 → 2, 120 → 3 (`demo_cost` 4번 출력). 길이는 **비트** 수, big-endian.

</details>

<details>
<summary>힌트 3 — B1: 무엇을 언제 믿나</summary>

magic 과 hdr_version/hdr_size 는 "이게 내가 아는 포맷인가" 를 정한다 — 포맷을 알아야 hdr_crc32 가 어디 있는지 안다. 그다음 헤더 CRC. 그 **뒤에야** payload_size, load_addr, flags 를 쓸 수 있다. 크기 검사는 `h.payload_size > slot_len - 64` (slot_len ≥ 64 는 이미 확인) — 뺄셈 쪽으로 옮기면 넘치지 않는다.

</details>

<details>
<summary>힌트 4 — B1: 순서가 결과 코드를 정한다</summary>

테스트는 "첫 번째로 걸린 검사" 를 기대한다. 롤백은 헤더만 보면 되는 싼 검사라 payload CRC 보다 앞이다 — 그래서 "옛 버전 + 손상" 이미지는 ROLLBACK. SHA 비교는 32 바이트 **전부**, 처음 다른 바이트에서 멈추지 않게(`ct_equal`).

</details>

<details>
<summary>힌트 5 — C1: 결정과 로그를 분리</summary>

두 슬롯을 먼저 다 검증하고(둘 다 로그에 남기려고), 그다음 고른다. `pick = -1; if (A ok) pick = 0; if (B ok && (pick < 0 || verB > verA)) pick = 1;` — 같은 버전이면 A 가 남는다. 로그는 `log_printf` 가 잘림을 처리하므로 cap 이 작아도 신경 쓸 것은 없다.

</details>

## 5. 풀이

### Part A — SHA-256

FIPS 180-4 의 SHA-256 은 Merkle–Damgård 구조다: 상태 8 워드(H0 = 처음 8 소수의 제곱근 소수부)에 64 B 블록을 하나씩 섞고, 마지막 블록에 패딩과 길이를 넣는다.

| 단계 | 내용 | 흔한 실수 |
|---|---|---|
| 메시지 스케줄 | W[t] = σ1(W[t−2]) + W[t−7] + σ0(W[t−15]) + W[t−16] | σ 의 마지막 항을 ROR 로 (SHR 이어야) |
| 라운드 | T1 = h + Σ1(e) + Ch(e,f,g) + K[t] + W[t], T2 = Σ0(a) + Maj(a,b,c) | Ch 의 `~e & g` 를 `~(e & g)` 로 |
| 상태 갱신 | H[i] += a..h | 라운드 변수만 바꾸고 H 에 안 더함 |
| 패딩 | 0x80, 0…, 비트 길이 64 비트 big-endian | `nbuf >= 56` / 길이를 바이트 단위·LE 로 |

`sha256_update` 는 주어지지만 읽어 둘 것: 지난번 잔여 조각을 먼저 64 B 로 채우고, 그다음 통째 블록은 **복사 없이** 입력에서 바로 압축, 남은 꼬리만 버퍼에. "무작위로 쪼개 200 번 = 한 번에" 테스트가 이 경로를 전부 지난다.

검증 전략 — 기억에서 꺼낸 바이트열은 믿지 않는다. 표준 벡터(abc / 빈 문자열 / 448 비트 / 백만 'a')도 `mkimg.py vectors` 가 hashlib 으로 다시 찍은 것과 대조했다(실제 출력):

```text
small (len 0..300) sha256 chain = 68344c4f9d6a7272e5631831c8f24b4873f847f0a9d5f2627c81bf0254d8ab2d
small (len 0..300) crc32 chain  = 0x625B381A
big (6 sizes)      sha256 chain = e30c418ca28781076ba3c6ec882e98bc2de9ab827644848701844500e7c400b9
sha256(b'abc') = ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
sha256(b'') = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
sha256(b'abcdbcdecdef'..x56) = 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1
sha256(b'aaaaaaaaaaaa'..x1000000) = cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0
crc32(b'123456789') = 0xCBF43926
```

"체인" 은 길이 0..300 의 다이제스트 301 개를 이어 붙여 다시 해시한 값이다 — C 가 같은 xorshift32 로 같은 버퍼를 만들어 같은 체인을 내야 통과. 301 개 중 하나만 틀려도 체인이 바뀌므로, 32 바이트 상수 하나로 9 632 바이트어치 비교를 한다. 패딩 경계(55/56/63/64/119/120)는 전부 이 안에 있다.

### Part B — 검사 순서

```svg
<svg viewBox="0 0 740 420" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">img_verify — 싼 검사 먼저, 필드는 헤더 CRC 뒤에만 믿는다</text>
  <rect x="10" y="32" width="250" height="28" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="18" y="51" fill="currentColor">slot_len &lt; 64 → BAD_SIZE</text>
  <rect x="10" y="68" width="250" height="28" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="18" y="87" fill="currentColor">헤더 64 B 전부 0xFF → EMPTY</text>
  <rect x="10" y="104" width="250" height="28" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="18" y="123" fill="currentColor">magic ≠ "BIMG" → BAD_MAGIC</text>
  <rect x="10" y="140" width="250" height="28" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="18" y="159" fill="currentColor">버전≠1 / 크기≠64 → BAD_HDR_VERSION</text>
  <rect x="10" y="176" width="250" height="28" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="18" y="195" fill="currentColor">CRC([0,60)) 불일치 → BAD_HDR_CRC</text>
  <path d="M135 60 L135 68 M135 96 L135 104 M135 132 L135 140 M135 168 L135 176" stroke="currentColor"/>
  <path d="M260 190 L275 190 L275 46 L290 46" stroke="currentColor" fill="none"/>
  <text x="18" y="226" fill="currentColor">여기부터 필드를 믿음 (hdr_ok = true)</text>
  <rect x="290" y="32" width="270" height="28" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="298" y="51" fill="currentColor">모르는 flag / SHA 없음 → BAD_FLAGS</text>
  <rect x="290" y="68" width="270" height="28" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="298" y="87" fill="currentColor">size &lt; 8 or &gt; slot_len−64 → BAD_SIZE</text>
  <rect x="290" y="104" width="270" height="28" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="298" y="123" fill="currentColor">size &gt; 384 KB → TOO_BIG</text>
  <rect x="290" y="140" width="270" height="28" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="298" y="159" fill="currentColor">load_addr 불일치 → BAD_LOAD_ADDR</text>
  <rect x="290" y="176" width="270" height="28" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="298" y="195" fill="currentColor">version &lt; min → ROLLBACK</text>
  <path d="M425 60 L425 68 M425 96 L425 104 M425 132 L425 140 M425 168 L425 176" stroke="currentColor"/>
  <rect x="290" y="232" width="270" height="28" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="298" y="251" fill="currentColor">CRC-32 → BAD_CRC (7 insn/B)</text>
  <rect x="290" y="268" width="270" height="28" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="298" y="287" fill="currentColor">SHA-256 → BAD_SHA (42–58 insn/B)</text>
  <rect x="290" y="304" width="270" height="28" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="298" y="323" fill="currentColor">OK: init_sp, reset_vec 채움</text>
  <path d="M425 204 L425 232 M425 260 L425 268 M425 296 L425 304" stroke="currentColor"/>
  <rect x="575" y="32" width="155" height="300" rx="5" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="583" y="52" fill="currentColor">비용</text>
  <text x="583" y="72" fill="currentColor">위쪽 검사:</text>
  <text x="583" y="88" fill="currentColor">헤더 64 B 만 읽음</text>
  <text x="583" y="250" fill="currentColor">아래 둘: payload</text>
  <text x="583" y="266" fill="currentColor">전체를 읽음</text>
  <text x="583" y="286" fill="currentColor">384 KB @168 MHz</text>
  <text x="583" y="302" fill="currentColor">CRC ≥ 16 ms</text>
  <text x="583" y="318" fill="currentColor">SHA ≥ 98 ms</text>
  <text x="10" y="360" fill="currentColor">모든 경로가 [0, slot_len) 안에서만 읽는다 — 크기 검사 전엔 payload 를 안 읽는다.</text>
  <text x="10" y="380" fill="currentColor">ROLLBACK 이 CRC 보다 앞: 옛 버전이면 손상 여부와 무관하게 부팅 불가 → 해시 생략.</text>
  <text x="10" y="400" fill="currentColor">hdr_ok 이면 실패해도 버전·크기를 로그에 남긴다 ("BAD_CRC (hdr ok: v1.5.0)").</text>
</svg>
```

각 순서의 이유:

1. **EMPTY 를 따로**: 지워진 슬롯은 "고장" 이 아니라 "아직 없음". 현장 로그에서 BAD_MAGIC 과 구분돼야 원인 분석이 된다.
2. **magic → 포맷 → 헤더 CRC**: hdr_version 이 2 인 미래 포맷은 크기도 CRC 위치도 다를 수 있다. 버전을 먼저 봐야 "CRC 틀림" 이 아니라 "모르는 포맷" 이라고 정확히 말한다(변이 M3 가 이걸 잡는다).
3. **flags 는 must-understand**: 모르는 비트가 서 있으면 거부. 내일 "bit1 = 암호화됨" 을 추가했을 때, 옛 부트로더가 암호문을 평문처럼 부팅하는 사고를 막는다. SHA 비트가 없는 이미지도 거부 — 이 보드의 정책(아래 Q&A 5 에서 이 정책의 한계를 따진다).
4. **크기: 뺄셈으로**. `64 + payload_size > slot_len` 은 payload_size = 0xFFFFFFC0 에서 0 이 되어 통과한다. 그다음 CRC 루프가 4 GB 를 읽는다. 이 레슨에선 `TOO_BIG`(384 KB) 검사가 뒤에서 막아 주지만, 그건 우연한 방어다 — 변이 M2 는 BAD_SIZE 대신 TOO_BIG 을 돌려 테스트에 걸리고, M2 + TOO_BIG 제거(M27)는 실제로 범위 밖을 읽는다(7. 자주 하는 실수의 ASan 출력).
5. **BAD_SIZE vs TOO_BIG 을 나눈 이유**: 앞은 "슬롯 내용이 자기모순(잘림/손상)", 뒤는 "멀쩡한데 이 보드용이 아님(빌드 설정 오류)". 고치는 사람이 다르다.

모든 단일 비트 플립의 결과 지도 (`demo_cost` 실제 출력):

```text
2) 64 B 헤더 단일 비트 플립 지도 (solution.c 의 검사 순서)
   magic        [0,4)            32 비트 → BAD_MAGIC
   hdr_version  [4,6)            16 비트 → BAD_HDR_VERSION
   hdr_size     [6,8)            16 비트 → BAD_HDR_VERSION
   img_version..sha256 [8,60)   416 비트 → BAD_HDR_CRC
   hdr_crc32    [60,64)          32 비트 → BAD_HDR_CRC
   payload                      모든 비트 → BAD_CRC (CRC-32 는 32 비트 이하 버스트를 전부 검출)
```

하네스는 320 B 이미지의 2560 비트를 하나씩 뒤집어 이 표와 정확히 같은 코드가 나오는지 본다. payload 의 단일 비트가 "반드시" 걸리는 건 운이 아니다 — CRC-32 생성 다항식은 차수 32 라 길이 32 이하의 모든 버스트 오류를 검출한다(연습 4 에서 1·2·3 비트 전수로 확인).

**잘림** 두 가지: (1) 다운로드가 cut 바이트에서 끊겨 나머지가 0xFF — cut 0 → EMPTY, 1..3 → BAD_MAGIC, 4..7 → BAD_HDR_VERSION, 8..63 → BAD_HDR_CRC, 64.. → BAD_CRC, **단 잘린 뒷부분이 원래도 0xFF 였다면 OK** (내용이 같으니 정답이다 — 테스트 oracle 이 이걸 명시). (2) 슬롯 버퍼 자체가 짧음 — 길이 0..319 각각을 정확한 크기의 힙 버퍼로 줘서 한 바이트만 넘어 읽어도 ASan 이 터지게 했다. 결과는 전부 BAD_SIZE.

**악의적 변경**. CRC-32 는 메시지에 대해 아핀(affine)이다: crc(m ⊕ e) = crc(m) ⊕ L(e). 그래서 아무 4 연속 바이트를 미지수로 두면 32×32 GF(2) 연립방정식 하나로 CRC 를 원하는 값에 맞출 수 있다(`crc_forge4`, 하네스 도우미). 테스트: 리셋 벡터를 0x08031337 로 바꾸고 payload 의 다른 4 바이트로 CRC 를 원래 값에 맞춤 → 헤더는 그대로 → `BAD_CRC` 를 통과하고 **SHA 만 잡는다**(`BAD_SHA`). 그런데 공격자가 헤더까지 쓸 수 있으면 SHA 를 다시 계산해 넣으면 그만이다 → 하네스는 이 경우 `OK` 를 **기대**한다. 해시는 "누가 만들었나" 를 말하지 않는다 — 그건 서명(SEC 트랙).

```svg
<svg viewBox="0 0 740 262" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">무엇을 막나 — CRC vs SHA-256 vs 서명 (이 레슨의 테스트 결과)</text>
  <rect x="10" y="32" width="190" height="34" class="f-mut" stroke="currentColor"/>
  <text x="18" y="54" fill="currentColor">공격/사고</text>
  <rect x="200" y="32" width="170" height="34" class="f-mut" stroke="currentColor"/>
  <text x="208" y="54" fill="currentColor">CRC-32 만</text>
  <rect x="370" y="32" width="170" height="34" class="f-mut" stroke="currentColor"/>
  <text x="378" y="54" fill="currentColor">+ SHA-256 (헤더 안)</text>
  <rect x="540" y="32" width="190" height="34" class="f-mut" stroke="currentColor"/>
  <text x="548" y="54" fill="currentColor">+ 서명 (SEC, 다음 단계)</text>
  <text x="18" y="88" fill="currentColor">비트 플립·버스트·잘림</text>
  <rect x="200" y="72" width="170" height="26" class="f-ok" stroke="currentColor"/>
  <text x="208" y="90" fill="currentColor">검출 (2560/2560)</text>
  <rect x="370" y="72" width="170" height="26" class="f-ok" stroke="currentColor"/>
  <text x="378" y="90" fill="currentColor">검출</text>
  <rect x="540" y="72" width="190" height="26" class="f-ok" stroke="currentColor"/>
  <text x="548" y="90" fill="currentColor">검출</text>
  <text x="18" y="124" fill="currentColor">payload 변경 + CRC 맞춤</text>
  <rect x="200" y="108" width="170" height="26" class="f-warn" stroke="currentColor"/>
  <text x="208" y="126" fill="currentColor">통과 (위조 성공)</text>
  <rect x="370" y="108" width="170" height="26" class="f-ok" stroke="currentColor"/>
  <text x="378" y="126" fill="currentColor">BAD_SHA</text>
  <rect x="540" y="108" width="190" height="26" class="f-ok" stroke="currentColor"/>
  <text x="548" y="126" fill="currentColor">검출</text>
  <text x="18" y="160" fill="currentColor">헤더까지 새로 계산</text>
  <rect x="200" y="144" width="170" height="26" class="f-warn" stroke="currentColor"/>
  <text x="208" y="162" fill="currentColor">통과</text>
  <rect x="370" y="144" width="170" height="26" class="f-warn" stroke="currentColor"/>
  <text x="378" y="162" fill="currentColor">통과 (테스트가 OK 기대)</text>
  <rect x="540" y="144" width="190" height="26" class="f-ok" stroke="currentColor"/>
  <text x="548" y="162" fill="currentColor">개인키 없으면 불가</text>
  <text x="18" y="196" fill="currentColor">옛 정품 이미지 재설치</text>
  <rect x="200" y="180" width="340" height="26" class="f-acc" stroke="currentColor"/>
  <text x="208" y="198" fill="currentColor">ROLLBACK — 버전이 서명돼 있을 때만 의미</text>
  <rect x="540" y="180" width="190" height="26" class="f-ok" stroke="currentColor"/>
  <text x="548" y="198" fill="currentColor">서명된 버전 + 단조 카운터</text>
  <text x="10" y="230" fill="currentColor">무결성(integrity) = "바뀌지 않았다" / 진위(authenticity) = "그 사람이 만들었다".</text>
  <text x="10" y="250" fill="currentColor">해시는 앞의 것만 준다 — 그것도 해시 자체가 안전한 곳(서명 안)에 있을 때만.</text>
</svg>
```

**무작위 결함 3000 개** — 결함 종류 10 가지(1 바이트, ≤ 32 비트 버스트, CRC 재봉인한 변경, 헤더 바이트, 롤백, load 주소, 거대 size, 모르는 flag, magic, 무결함)를 고정 시드로 섞고, 각 케이스의 기대 코드를 생성 시점에 정한다. 9 가지 결과가 모두 100 번 이상 나왔는지도 확인한다.

**python 과 양방향 교차검증** (실제 출력):

```text
C emit /tmp/BM-A04_c.img: 320 B, v1.3.0, crc32=0x81D1E170
python verify /tmp/BM-A04_c.img: OK (v1.3.0 size=256 crc=0x81D1E170 sha=5d89ecdbce39d5f1..)
packed /tmp/BM-A04_py.img: 320 B (hdr 64 + payload 256), v1.3.0, crc32=0x81D1E170, sha256=5d89ecdbce39d5f107035a3b5255c429994b89579df7a0c28155734f47581971
C image == python image (byte-identical)
C verify /tmp/BM-A04_py.img: OK (v1.3.0 size=256 crc=0x81D1E170)
C emitmut /tmp/BM-A04_mut.bin: 2000 records
python batch: 2000/2000 agree with C
  OK=177 BAD_MAGIC=275 BAD_HDR_CRC=163 BAD_FLAGS=251 BAD_SIZE=280 BAD_LOAD_ADDR=166 ROLLBACK=172 BAD_CRC=344 BAD_SHA=172
```

python 쪽 `verify()` 는 `struct`/`zlib`/`hashlib` 로 따로 짰다(코드 공유 없음). 파이썬 정수는 넘치지 않으므로 `HDR_SIZE + size > len(slot)` 이 그대로 맞다 — 같은 규격을 C 는 뺄셈으로 지킨다. 손으로 만든 결함 파일도 두 도구가 같은 답을 한다(실제 출력):

```text
python verify /tmp/BM-A04_flip.img: BAD_CRC (v1.3.0 size=256 crc=0x81D1E170 sha=5d89ecdbce39d5f1..)
C verify /tmp/BM-A04_flip.img: BAD_CRC (v1.3.0 size=256 crc=0x81D1E170)
python verify /tmp/BM-A04_py.img: ROLLBACK (v1.3.0 size=256 crc=0x81D1E170 sha=5d89ecdbce39d5f1..)
C verify /tmp/BM-A04_py.img: ROLLBACK (v1.3.0 size=256 crc=0x81D1E170)
python verify /tmp/BM-A04_big.img: TOO_BIG (v2.0.0 size=393217 crc=0x6C2D8EAA sha=c34ece263310288a..)
C verify /tmp/BM-A04_big.img: TOO_BIG (v2.0.0 size=393217 crc=0x6C2D8EAA)
python verify /tmp/BM-A04_wrongload.img: BAD_LOAD_ADDR (v1.3.0 size=256 crc=0x81D1E170 sha=5d89ecdbce39d5f1..)
python verify /tmp/BM-A04_nosha.img: BAD_FLAGS (v1.3.0 size=256 crc=0x81D1E170 sha=0000000000000000..)
```

(flip = 바이트 200 의 비트 2, ROLLBACK = `--min 1.4.0`, big = 0x60001 B payload, wrongload = `--load 0x08000000`, nosha = `--no-sha`.) `mkimg.py pack --ver 1.300.0` 은 `ValueError: version field out of range (255.255.65535)` 로 거부 — 패커가 필드 폭을 넘는 버전을 조용히 잘라 넣으면(300 & 0xFF = 44) 롤백 검사가 엉뚱하게 동작한다.

### Part C — 부트 결정과 로그

`boot_select` 의 실제 출력 (현장 시나리오 2 개):

```text
[boot] anti-rollback min=v1.2.0
[boot] slot A @0x00000000 len=512: OK v1.3.0 size=256 crc=0x81D1E170 sha=5d89ecdb.. sp=0x20020000 pc=0x080201C1
[boot] slot B @0x00080000 len=512: BAD_CRC (hdr ok: v1.5.0 size=256)
[boot] select A v1.3.0 -> copy 256 B to 0x08020000, jump reset=0x080201C1
[boot] anti-rollback min=v1.2.0
[boot] slot A @0x00000000 len=512: ROLLBACK (hdr ok: v1.1.0 size=256)
[boot] slot B @0x00080000 len=512: EMPTY
[boot] no bootable image -> stay in bootloader (DFU)
```

첫 번째를 읽는 법: "v1.5.0 을 B 에 받았는데 payload 가 깨졌다(헤더는 멀쩡 = 헤더를 먼저 쓰고 payload 쓰는 중 끊겼거나 전송 오류), 그래서 v1.3.0 으로 부팅했다". `hdr ok` 정보가 없으면 "B 가 왜 안 됐지?" 에서 멈춘다. 두 번째는 현장에서 가장 무서운 로그 — 부팅할 게 없다. 이때 부트로더가 **DFU/복구 모드로 남는 것**이 설계의 일부여야 한다(BM-A05 YMODEM).

36 조합 전수 결과 규칙: OK 둘 → 높은 버전(같으면 A), 하나 → 그것, 없음 → −1. 로그 버퍼 24 B 에서도 결정은 같고 문자열은 NUL 로 끝난다 — **로깅 실패가 부팅 결정을 바꾸면 안 된다**.

선택 이후(이 레슨 밖): payload 를 0x08020000 으로 복사 → **복사본을 다시 검증**(복사 중 오류) → 벡터 테이블 sanity(SP 가 RAM 안, 리셋 벡터 홀수·영역 안) → 점프(BM-A03). 롤백 카운터를 올리는 건 새 이미지가 **스스로 정상임을 확인한 뒤**(MCUboot 의 test/confirm — BM-A06)다. 확인 전에 올리면 새 이미지가 부팅에 실패했을 때 옛 이미지로도 못 돌아간다.

### 비용 — 부팅 시간 예산

호스트(`demo_cost` 실제 출력, 부하에 따라 절대값은 변함):

```text
   CRC-32 table (1 KB 표)       484.6 MB/s   (CRC table 대비 1.00x 느림)
   CRC-32 bitwise (표 없음)    158.9 MB/s   (CRC table 대비 3.05x 느림)
   SHA-256 (블록만)           310.4 MB/s   (CRC table 대비 1.56x 느림)
```

호스트의 64 비트 슈퍼스칼라 코어에선 SHA 가 CRC 의 1.5 배 정도지만, Cortex-M 에선 다르다. 같은 C 를 `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2` 로 컴파일해 objdump 의 루프 몸체 명령어 수 × 반복 횟수를 셌다(실제 디스어셈블 기준):

| 빌드 | 구간 | 몸체 명령어 × 반복 | 합 |
|---|---|---|---|
| `-O2 -fno-unroll-loops` | W[0..15] 적재 (`ldr`+`rev`, M4 비정렬 LDR 허용) | 7 × 16 | 112 |
| | W[16..63] 확장 | 18 × 48 | 864 |
| | 64 라운드 | 42 × 64 | 2688 |
| | 직선 코드(프롤로그·H 갱신) | 52 | 52 |
| | **블록당** | | **3716 → 58.1 insn/B** |
| `-O2` (기본, 2 라운드씩 펼침) | 블록당 | 48×2 + 54×12 + 59×32 + 57 | **2689 → 42.0 insn/B** |
| CRC-32 표 방식 | 바이트 루프 | 7 / 바이트 | **7 insn/B** |

384 KB(6144 블록) @168 MHz, **CPI = 1 이라는 하한 가정**에서 SHA 98–136 ms, CRC 16.4 ms — 비 6–8 배. 실제 Cortex-M4 는 LDR 2 사이클, 분기 시 파이프라인 재충전, 플래시 대기 상태(ART 가속기 히트/미스 — RM0090 확인 권장)가 더해지므로 이보다 느리다(실측은 보드에서 DWT CYCCNT 로 — BM-A10). 코드 크기 실제값: `sha256_compress` 324 B(펼치면 604 B) + K 표 256 B, 스택 332 B(`push` 9 레지스터 36 B + `sub sp, #0x128` 296 B — W[64] 가 256 B), CRC 표 1 KB(`.bss`; 실제로는 `const` 로 플래시에). 부트로더 설계 판단: 매 부팅 SHA 가 100 ms 넘게 걸리면 → 설치 시 한 번 SHA(+서명), 부팅 때는 CRC 만 / 하드웨어 해시 가속기(STM32F4 중 F415/F417 등 HASH 주변장치 있는 모델 — 확인 권장) / 실행 영역은 쓰기 보호 후 검사 생략 같은 정책을 고른다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/BM-A04_so && /tmp/BM-A04_so      # 60/60 passed, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/BM-A04_as && /tmp/BM-A04_as
```

실행 결과 (요약, 실제):

```text
-- Part A: CRC-32 (주어짐) + SHA-256 (FIPS 180-4)
-- Part B: img_verify — 비트 플립 / 잘림 / 롤백 / 넘침 / 악의적 변조 / python 이미지
-- Part B: 무작위 결함 3000 개 (고정 시드 xorshift32)
-- Part C: 두 슬롯 부트 결정 + 로그
...(위 부트 로그 8 줄)...
60/60 passed
```

ASan/UBSan 빌드도 60/60, 경고·런타임 오류 없음.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `if (64 + h.payload_size > slot_len)` | 0xFFFFFFC0 + 64 = 0 → 통과 → CRC 가 4 GB 를 읽는다 | `h.payload_size > slot_len - 64` (slot_len ≥ 64 먼저 확인) |
| 헤더 CRC 확인 전에 payload_size 사용 | 깨진 헤더의 쓰레기 값으로 메모리를 읽는다 | magic → 포맷 → 헤더 CRC → 그다음 필드 |
| 헤더 CRC 를 버전보다 먼저 | 미래 포맷을 "CRC 오류" 로 오진, CRC 위치도 모름 | 버전/크기 → CRC |
| `memcmp(d, h->sha256, sizeof d)` 에서 d 가 포인터 | 8 바이트만 비교 → 나머지 24 바이트 변조 통과 | 길이 32 를 상수로, `ct_equal(d, h, 32)` |
| 패딩 경계 `nbuf >= 56` | n = 55 에서 블록 하나를 더 만들어 다른 해시 | `nbuf > 56` (0x80 을 넣은 뒤) |
| 길이를 LE 또는 바이트 단위로 | FIPS 는 **비트** 수, big-endian | `bits >> (56 − 8i)` |
| 구조체를 `memcpy` 로 직렬화 (호스트 도구) | 엔디언·패딩이 컴파일러/호스트마다 다름 | 필드별 LE 저장 (`st_le32`) + `_Static_assert` |
| 버전을 문자열로 비교 | "1.10.0" < "1.9.3" | 정수 패킹, 필드 폭 검사 |
| 모르는 flag 무시 | 미래 의미(예: 암호화) 를 옛 부트로더가 무시하고 부팅 | 모르는 비트 = 거부 |
| 롤백 카운터를 새 이미지 부팅 직후 올림 | 새 이미지가 망가졌으면 옛 이미지로도 못 돌아감 | 자기 확인(confirm) 후 올림 (BM-A06) |

하네스가 실제로 잡는지 — 비공개 복사본에 넣은 변이(레슨 폴더에는 없음):

| 변이 | 결과 |
|---|---|
| M1 패딩 `nbuf >= 56` | 59/60 — python 체인(길이 55 등)만 잡는다 |
| M2 `64 + size > slot_len` | 59/60 — 거대 size 가 BAD_SIZE 대신 TOO_BIG |
| M3 헤더 CRC 를 버전보다 먼저 | 58/60 |
| M4 롤백 `<=` | 57/60 |
| M5 버전 무시하고 A 우선 | 59/60 (36 조합) |
| M6 σ0 의 SHR3 → SHR2 | 52/60 |
| M7 TOO_BIG 검사 없음 | 59/60 (384 KB + 1) |
| M8 EMPTY 검사 없음 | 57/60 |
| M9 길이 필드 LE | 53/60 |
| M10 다이제스트 31 B 만 비교 | 59/60 (저장된 SHA 256 비트 각각 재봉인) |

M2 + M7 을 같이 넣으면(넘침 + 백스톱 없음) 일반 빌드는 segfault(exit 139), ASan 빌드는 (실제 출력):

```text
==39100==ERROR: AddressSanitizer: global-buffer-overflow on address 0x00010228dfc0 at pc 0x0001021dee74 bp 0x00016dc24600 sp 0x00016dc245f8
    #1 0x0001021e06ac in img_verify M27_naive_and_no_exec.c:258
SUMMARY: AddressSanitizer: global-buffer-overflow M27_naive_and_no_exec.c:98 in crc32_update
```

M10 은 처음엔 살아남았다 — 헤더 비트 플립은 전부 헤더 CRC 가 먼저 잡기 때문이다. "저장된 SHA 필드를 바꾸고 헤더를 재봉인" 테스트를 추가해서야 잡혔다. **검사가 겹쳐 있으면 뒤쪽 검사의 버그는 앞쪽 검사가 가린다** — 각 검사를 단독으로 겨냥하는 테스트가 필요하다.

## 8. 면접 Q&A

<details>
<summary>Q1. CRC-32 면 충분하지 않나요? SHA-256 은 왜 넣죠?</summary>

우연한 오류(전송 비트 오류, 플래시 비트 썩음, 잘린 다운로드)에는 CRC-32 가 충분히 강하다 — 32 비트 이하 버스트 전부, 이 레슨의 64 B payload 에선 1·2·3 비트 오류 전부(연습 4: 22 238 720 개 3 비트 패턴 미검출 0). 하지만 CRC 는 선형이라 **의도적 변경을 숨기기 쉽다**: 4 바이트만 고르면 CRC 를 아무 값에나 맞출 수 있다(Part B 에서 리셋 벡터를 바꾸고 CRC 를 보존 — 연습 3 에선 0xDEADBEEF 로). SHA-256 은 그런 "같은 해시를 갖는 다른 payload" 를 찾는 게 계산상 불가능하다. 그래서 툴체인 버그나 중간 단계 변조 같은 "해시는 그대로 두고 내용만 바뀐" 경우를 잡는다. 꼬리: "그럼 CRC 는 빼도 되나?" → CRC 는 7 insn/B 로 빠르고(SHA 의 1/6–1/8) 부분 검증·스트리밍 진단에 좋다. 둘 다 두되 역할을 나눈다.

</details>

<details>
<summary>Q2. 공격자가 이미지를 바꿀 수 있다면 헤더 안의 SHA-256 은 막아 주나요?</summary>

못 막는다. 헤더도 같이 쓸 수 있으면 SHA 를 다시 계산해 넣으면 된다 — 하네스의 "악의적 변조 2" 가 바로 그 경우를 OK 로 **기대**한다. 해시는 무결성이지 진위가 아니다. 진위가 필요하면 (1) 해시에 **서명**(ECDSA P-256 / Ed25519, 공개키는 부트로더나 OTP 에 — SEC 트랙), (2) 공유 비밀 기반 MAC(HMAC — 키가 기기마다 노출될 위험), (3) 신뢰 체인의 루트(ROM 부트로더가 1 단 부트로더를 검증)가 있어야 한다. 그리고 img_version 도 서명 범위 안에 있어야 롤백 방지가 의미가 있다.

</details>

<details>
<summary>Q3. 검사 순서를 설명해 보세요. 왜 그 순서죠?</summary>

슬롯 길이 → EMPTY → magic → 헤더 버전/크기 → 헤더 CRC → (여기부터 필드 신뢰) flags → 크기(슬롯) → 크기(실행 영역) → load 주소 → 롤백 → payload CRC → payload SHA. 원칙 셋: ① **확인하지 않은 값으로 메모리를 읽지 않는다** — size 를 쓰기 전에 헤더 CRC, payload 를 읽기 전에 크기. ② **포맷을 먼저 알아야 무결성 필드 위치를 안다** — 버전 → CRC. ③ **싼 것 먼저** — 헤더 64 B 로 판정되는 건 payload 해시(수십~백 ms) 전에. 결과 코드가 "첫 실패" 이므로 순서 자체가 인터페이스다 — 바꾸면 테스트(와 현장 로그 해석)가 바뀐다.

</details>

<details>
<summary>Q4. 왜 64 B 헤더 뒤의 payload 를 바로 실행(XIP)하지 않고 복사하나요? 실행하게 하려면 뭘 바꾸겠나요?</summary>

벡터 테이블은 VTOR 가 가리키는데, VTOR 정렬은 테이블 크기(벡터 수를 2 의 거듭제곱으로 올린 × 4 B, 최소 128 B — ARMv7-M ARM 확인 권장)에 맞아야 한다. F4 처럼 벡터가 ~100 개면 512 B 정렬 — 슬롯 + 64 는 정렬이 안 맞는다. 바꾸는 법: (1) MCUboot 처럼 헤더 영역을 0x200 등으로 패딩해 payload 가 정렬된 주소에서 시작하게 하고, 앱을 "slot base + 헤더 크기" 에 링크(load_addr 가 곧 슬롯 안 주소), (2) 그러면 A/B 두 슬롯 각각에 맞는 두 빌드가 필요하거나(위치 의존 코드), 업데이트 시 swap(BM-A06)으로 항상 primary 에서 실행. 이 레슨은 외부 NOR 슬롯 → 내부 플래시 복사 모델이라 64 B 로 충분했다. 헤더 크기를 필드(`hdr_size`)로 둔 이유도 이런 변경 여지 때문이다.

</details>

<details>
<summary>Q5. SHA 플래그가 없는 이미지를 거부하는 정책 — 이 설계가 틀리는 경우는?</summary>

공격자 관점에선 의미가 없다: SHA 를 빼는 대신 그냥 SHA 를 다시 계산하면 되니까(Q2). 이 정책이 막는 것은 **실수** — 빌드 파이프라인이 `--no-sha` 로 만든 이미지가 양산 기기에 들어가는 것. 틀리는 경우: (1) 아주 작은 부트로더(예: 8 KB 안)에서 SHA 코드 324 B + K 256 B + 스택 332 B 가 부담일 때, (2) 부팅 시간 제약(384 KB SHA ≥ 98 ms)이 빡빡할 때. 그럼 "설치 시 SHA(+서명) 한 번, 부팅 시 CRC" 로 정책을 나누고 플래그 의미를 "설치 검증 완료" 로 바꾸는 식으로 재설계한다. 정책은 하드코딩된 한 줄이 아니라 보안 요구와 부팅 예산에서 나온다는 게 답의 핵심.

</details>

<details>
<summary>Q6. 롤백 방지 카운터는 어디에 저장하나요? 언제 올리죠?</summary>

이미지 밖, 공격자가 되돌릴 수 없는 곳: OTP / eFuse(단조, 한 번 태우면 끝 — 비트 수가 곧 최대 업데이트 횟수), 보안 요소, 또는 쓰기 보호된 플래시 영역. 플래시면 지우기 없이 1→0 만 하는 **온도계 부호**가 자연스럽다(연습 5: 찢어진 쓰기에도 값이 [old, new] 밖으로 안 나가고 줄지 않음 — 9 704 개 시나리오). 올리는 시점: 새 이미지가 부팅해 자기 검사를 통과하고 confirm 한 뒤. 너무 일찍 올리면 새 이미지가 망가졌을 때 옛 이미지로 복귀 불가(벽돌). 또 모든 버전마다 올리지 않고 "보안 수정이 포함된 릴리스" 에서만 올리는 별도 security counter 를 두는 설계가 흔하다(MCUboot 의 security counter — 문서 확인 권장) — eFuse 비트를 아끼기 위해.

</details>

<details>
<summary>Q7. 다운로드 중 전원이 나가면 헤더 검증이 어떻게 동작하죠? 쓰는 순서도 바꾸겠습니까?</summary>

이 레슨 잘림 테스트대로라면: 헤더를 먼저 쓰고 payload 를 쓰다 끊기면 "헤더 OK + BAD_CRC" — 검출은 되지만 비싼 payload 해시까지 가서야 안다. 더 좋은 순서는 **payload 를 먼저, 헤더(특히 magic)를 마지막에** 쓰는 것: 끊긴 슬롯은 EMPTY 나 BAD_MAGIC 로 64 B 만 보고 바로 판정되고, "magic 이 있다 = 다 썼다" 는 커밋 마커가 된다. 플래시는 1→0 만 되므로 magic 워드를 맨 나중에 프로그램하면 원자적 커밋에 가깝다(단 찢어진 워드 프로그램은 BM-A02/A06 에서 다룬다). Don 의 SSD 경험의 "메타데이터 마지막 + 시퀀스 번호" 패턴과 같다.

</details>

<details>
<summary>Q8. 외부 SPI NOR 에 있는 슬롯은 메모리 맵이 안 됩니다. 384 KB 를 RAM 에 올릴 수도 없고요. 어떻게 검증하나요?</summary>

스트리밍: 헤더 64 B 를 읽어 헤더 단계를 끝내고, payload 는 작은 청크(256 B 등)로 **한 번만** 읽으면서 CRC 와 SHA 를 같이 갱신한다(연습 2: 읽기 횟수 = 1 + ⌈n/청크⌉ 을 assert, 메모리 검증과 3000 케이스 일치). 두 번 읽으면 읽는 사이에 내용이 바뀌는 TOCTOU 문제도 생긴다 — 같은 이유로 **검증한 바이트를 그대로 복사**하거나, 복사한 뒤 내부 플래시에서 다시 검증한다. 꼬리: "DMA 로 읽으면?" → 청크 이중 버퍼링으로 읽기와 해시를 겹친다(BM-M08). QSPI 메모리 맵 모드가 있는 칩이면 포인터로 읽을 수 있지만 캐시 일관성은 확인해야 한다(ARCH-M12).

</details>

<details>
<summary>Q9. 부팅 시간이 50 ms 로 제한되면 이 설계를 어떻게 바꾸겠나요?</summary>

숫자부터: 384 KB SHA 는 CPI 1 하한으로도 98 ms(펼친 빌드) — 이미 초과. CRC 표 방식 16 ms 는 통과. 선택지: (1) **설치 시 검증, 부팅 시 생략** — 업데이트 때 SHA(+서명)를 검사해 내부 플래시에 쓰고 쓰기 보호(WRP), 부팅 때는 헤더 + CRC 만. (2) 실제 이미지 크기만 해시(payload_size 가 384 KB 보다 훨씬 작으면 비례해 줄어듦). (3) 하드웨어 해시 가속기(STM32 HASH, 다른 MCU 의 crypto 엔진 — 칩마다 확인). (4) 해시를 분할 저장(섹터별 해시 트리)해서 필요한 부분만 검사 — 대신 헤더 포맷이 커진다. (5) 앱이 뜬 다음 백그라운드에서 전체 검사하고 실패 시 다음 부팅에 롤백. 어떤 걸 고르든 "보안 요구(서명 필수?)" 와 "고장 모드(검증 생략 중 비트 썩음)" 를 같이 말해야 한다.

</details>

## 9. 추가 연습

### 연습 1 — 버전 패킹과 비교

`ver_parse("M.m.p")` 를 짜라: 십진수 세 필드, 점 정확히 두 개, major ≤ 255, minor ≤ 255, patch ≤ 65535, 파싱 중 넘침 없이. 2 만 개 무작위 쌍에서 정수 비교 == 사전식 비교를 확인하라. "1.10.0" 과 "1.9.3" 은 문자열 비교로 어떻게 나오나?

<details>
<summary>답 보기</summary>

필드 누적 전에 `f > 6553` 이면 거부(×10 해도 65 539 이하라 넘치지 않음), 마지막에 범위 검사. 빈 필드("1..2"), 필드 4 개, 숫자 아닌 문자, "1.2.99999999999" 전부 −1. 패킹 `M<<24 | m<<16 | p` 는 각 필드가 폭 안에 있으면 사전식 순서를 그대로 보존한다(상위 필드가 다르면 하위가 아무리 커도 못 넘음) — 20 000 쌍 불일치 0. `strcmp("1.10.0", "1.9.3") < 0` — 문자 '1' < '9' 라 1.10 이 더 "작다" 고 답한다. 롤백 검사가 이걸 쓰면 최신 이미지를 거부한다.

</details>

### 연습 2 — 외부 NOR 슬롯 스트리밍 검증

`verify_stream(read_fn, ctx, slot_len, min_v, chunk, cap)` — 헤더를 한 번 읽고, payload 를 cap 바이트씩 한 번만 읽으며 CRC 와 SHA 를 함께 갱신하라. 메모리 버전과 결과가 같고, 읽기 호출 수가 1 + ⌈n/cap⌉ 임을 3000 케이스(cap 1..256, 결함 섞음, 슬롯 길이 가끔 짧게)로 확인하라.

<details>
<summary>답 보기</summary>

핵심은 `crc32_update` 가 zlib 의미(이전 결과를 이어받음)이고 SHA 가 `update` 를 지원한다는 것 — 그래서 둘을 한 루프에 넣을 수 있다. 헤더 단계 판정은 메모리 버전과 같은 `hdr_check` 를 쓰고(크기 검사가 끝나야 payload 읽기 시작), 읽기 콜백 실패도 BAD_SIZE 로. 3000/3000 일치, 읽기 횟수 위반 0. RAM 사용: 청크 + 헤더 64 + SHA 컨텍스트 ~108 B + compress 스택 332 B 정도 — 384 KB 를 올리는 것과 비교가 안 된다. cap = 1 도 정답이 같아야 한다(성능만 나쁨).

</details>

### 연습 3 — CRC 위조

100 B 메시지의 바이트 40..43 을 골라 CRC-32 를 0xDEADBEEF 로 만들어라. 아무 위치·아무 목표로 2000 번 해서 항상 풀리는지 보라. 왜 항상 풀리나?

<details>
<summary>답 보기</summary>

실제 출력: `forged bytes @40 = 69 14 9B 34 → crc32 = 0xDEADBEEF`, 2000/2000 성공. CRC 는 crc(m ⊕ e) = crc(m) ⊕ crc₀(e) 꼴의 아핀 함수다. 4 연속 바이트(32 비트) 창의 각 비트를 하나씩 켰을 때의 CRC 변화 32 개가 GF(2)³² 의 기저를 이루면(연속 32 비트 창에선 항상 그렇다 — 생성 다항식의 x³² 계수가 1 이고 창이 "x^k 를 곱한 32 비트 다항식" 이라 mod g 에서 단사) 아무 목표든 가우스 소거 한 번에 풀린다. 이게 "CRC 는 적대적 환경에서 무결성이 아니다" 의 수학적 내용이다. 같은 일을 SHA-256 에 하려면 2²⁵⁶ 근처의 탐색(제2 원상 공격)이 필요하다.

</details>

### 연습 4 — 1/2/3 비트 오류 전수 검출 vs 바이트 합

64 B payload(512 비트)의 모든 1·2·3 비트 오류가 CRC-32 로 검출되는지 전수 확인하라(신드롬 S[i] = 비트 i 를 뒤집었을 때 CRC 변화, 오류 패턴의 신드롬 = XOR). 같은 2 비트 오류를 "바이트를 32 비트로 더한 체크섬" 은 몇 개 놓치나?

<details>
<summary>답 보기</summary>

실제 출력: `CRC-32: 1-bit 512 개 미검출 0, 2-bit 130816 개 미검출 0, 3-bit 22238720 개 미검출 0`, `바이트 합(32 비트 덧셈) 체크섬: 2-bit 130816 개 중 미검출 8128`. 신드롬이 선형이라 패턴 하나당 XOR 몇 번이면 끝나서 2 200 만 경우도 0.1 초 안이다. 바이트 합은 "같은 비트 위치에서 한 바이트는 0→1, 다른 바이트는 1→0" 이면 +2^b 와 −2^b 가 상쇄된다 — 비트 위치마다 (1 인 바이트 수) × (0 인 바이트 수) 를 더한 값이 8128 이다. 이 길이에서 CRC-32 의 해밍 거리는 이보다 크다고 알려져 있다(Koopman 의 CRC 표 — 확인 권장); 여기서는 3 비트까지만 전수로 보였다.

</details>

### 연습 5 — 온도계 롤백 카운터

플래시 워드 4 개(128 비트)로 롤백 카운터를 만들어라: 값 = 0 이 된 비트 수, 증가 = 워드의 아래 비트부터 1→0 프로그램(AND), 감소 요청과 용량 초과는 거부. 프로그램이 찢어지면(0 이 될 비트 중 무작위 일부만 0) 값이 어디에 있을 수 있나? 모든 (old, new, 찢어질 쓰기 번호, 시드 8 개)로 확인하라.

<details>
<summary>답 보기</summary>

찢어진 프로그램은 1→0 전이 중 **일부만** 일어나므로 0 비트는 늘기만 한다 → 값 ≥ old. 그리고 new 에서 0 이어야 할 비트 밖은 건드리지 않으므로 값 ≤ new. 실제 출력: `torn-write scenarios with a cut: 9704, invariant violations: 0`, 재부팅 후 같은 bump 를 다시 하면 항상 new 에 도달. 주의: 찢어진 뒤 0 비트가 "아래부터 연속" 이 아닐 수 있다 — 그래서 값을 "가장 높은 0 비트 위치" 가 아니라 **0 비트 개수**로 읽는다. 지우기가 없으니 128 번 올리면 끝 — eFuse 와 같은 성질이고, 그래서 모든 릴리스가 아니라 보안 릴리스에서만 올린다(Q6).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/BM-A04_pr && /tmp/BM-A04_pr   # 21/21 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o /tmp/BM-A04_pa && /tmp/BM-A04_pa
```

## 10. 핵심 정리

- 헤더 64 B: magic, hdr_version/hdr_size(포맷), img_version(M<<24|m<<16|p), payload_size, load_addr, flags(must-understand), payload CRC-32, SHA-256, **hdr_crc32 over [0,60)**. 직렬화는 필드별 LE + `_Static_assert`.
- 검사 순서 = 인터페이스: EMPTY → magic → 포맷 → 헤더 CRC → (필드 신뢰) flags → 크기 → 실행 영역 → load 주소 → 롤백 → CRC → SHA. 확인 안 된 값으로 메모리를 읽지 않는다.
- 크기 검사는 뺄셈으로: `size > slot_len − 64`. `64 + size` 는 0xFFFFFFC0 에서 0 으로 넘친다. 겹친 검사는 서로의 버그를 가린다 — 검사마다 단독 테스트.
- SHA-256: W 스케줄(σ 의 마지막 항은 SHR), 64 라운드, 패딩 0x80 + 비트 길이 BE, 블록 수 ⌈(n+9)/64⌉. 표준 벡터 + hashlib 체인(길이 0..300)으로 검증.
- CRC 는 우연한 오류용(≤ 32 비트 버스트 전부), 아핀이라 4 바이트로 위조 가능. SHA 는 변경 탐지, 그러나 헤더를 다시 쓸 수 있으면 무력. 진위 = 서명(SEC). 롤백 방지는 서명된 버전 + 단조 카운터(OTP/eFuse/온도계 플래시), confirm 뒤에 올린다.
- 비용(Cortex-M4 명령어 수, CPI 1 하한): SHA 42–58 insn/B, CRC 7 insn/B → 384 KB @168 MHz 에서 98–136 ms vs 16 ms. 부팅 예산이 정책을 정한다.
- 부트 로그는 슬롯마다 결과 + (헤더가 믿을 만하면) 버전·크기, 선택과 이유, 부팅할 게 없으면 DFU 에 남는다고 명시.

## 참고자료

- NIST FIPS 180-4, *Secure Hash Standard* — SHA-256 정의(§4.1.2 함수, §4.2.2 상수, §5.1.1 패딩, §6.2 계산) — <https://csrc.nist.gov/pubs/fips/180-4/upd1/final>. 표준 벡터("abc", 448 비트, 백만 'a')는 이 맥의 python `hashlib` 으로 재확인(5. 풀이 출력)
- NIST CAVP SHA 테스트 벡터 (SHAVS) — <https://csrc.nist.gov/projects/cryptographic-algorithm-validation-program/secure-hashing>
- CRC-32/ISO-HDLC 파라미터(poly 0x04C11DB7 반사, init/xorout 0xFFFFFFFF, check 0xCBF43926) — Greg Cook, *Catalogue of parametrised CRC algorithms* <https://reveng.sourceforge.io/crc-catalogue/17plus.htm>; python `zlib.crc32` 으로 check 값 확인
- Philip Koopman, CRC 다항식 해밍 거리 표 — <https://users.ece.cmu.edu/~koopman/crc/> (CRC-32 의 길이별 HD 는 확인 권장)
- MCUboot 문서 — 이미지 헤더/TLV, 헤더 패딩, test/confirm, security counter: <https://docs.mcuboot.com/design.html> (세부 필드는 문서 확인 권장)
- ARMv7-M Architecture Reference Manual — VTOR 정렬 규칙 (B3.2.5, 확인 권장); Cortex-M4 TRM — 명령어 사이클(LDR 2 사이클 등, 확인 권장)
- ST RM0090 — 내부 플래시 섹터 배치(섹터 5–7 = 0x08020000–0x080FFFFF, 128 KB), HASH 주변장치(일부 모델) — 확인 권장
- python 표준 라이브러리 문서: `struct`, `zlib.crc32`, `hashlib.sha256` — <https://docs.python.org/3.9/library/>
- 이 레슨의 파일: `mkimg.py`(패커 + 독립 검증기 + 교차검증 상수/fixture 생성), `demo_cost.c`(비용·플립 지도·패딩 + Cortex-M4 타깃 빌드)
- 관련 레슨: COMM-B08/B12 · ALGO-M01/M02 (CRC 구현), COMM-A06 (SPI NOR program/erase — 슬롯이 사는 곳), BM-B08 (벡터 테이블과 리셋), BM-M13 (fault 정책), 다음 단계: BM-A02 (플래시 쓰기 규칙), BM-A03 (부트로더 → 앱 점프와 벡터 sanity), BM-A05 (YMODEM 수신 — 이 헤더 축약판 사용), BM-A06 (전원 차단 내성 swap, test/confirm), BM-A10 (DWT 로 실제 사이클 측정), BM-A11 (종합), SEC-* (서명 검증, secure boot), PLAT-M07 (A/B 슬롯 정책)
