---
id: ARM-A09
time: 120분
updated: 2026-11-15
---
# ARM-A09 PIC와 리로케이션: ADRP/ADD, GOT, @PAGE

## 학습 목표

- `ADRP + ADD/LDR` 두 명령이 전역 변수 주소를 만드는 과정을 **비트 단위로** 적용(apply)하고 되돌려(decode) 본다. 실제 `ld.bfd`·`ld.lld`·`ld64` 가 쓴 워드와 한 비트도 다르지 않게 맞춘다.
- 리로케이션 이름(`ARM64_RELOC_PAGE21` ↔ `R_AARCH64_ADR_PREL_PG_HI21` 등)을 보고 "어느 명령의 어느 칸을, 어떤 식(S+A−P)으로, 어떤 범위 검사와 함께" 고치는지 말한다. 범위는 ADRP ±4 GB, ADR ±1 MB, B/BL ±128 MB, B.cond ±1 MB, TBZ ±32 KB.
- GOT(Global Offset Table)와 stub/PLT가 언제 생기고 언제 링커가 없애는지(GOT→ADD, ADRP+ADD→NOP+ADR 완화)를, **지금 실행 중인 프로그램의 명령 워드를 직접 읽어서** 확인한다. 맥 dyld 의 chained fixup 과 리눅스 `R_AARCH64_RELATIVE/GLOB_DAT` 도 함께 본다.
- `relocation truncated to fit`, `recompile with -fPIC`, `does not have address`, 정렬이 안 맞는 `:lo12:` 같은 실제 링크 에러를 원인과 함께 설명한다. 특히 **새 ld64 는 정렬 오류를 조용히 넘긴다**는 실측 결과를 안다.
- 펌웨어 관점에서 Cortex-M 이 보통 non-PIC 인 이유, `-fropi/-frwpi`, 베니어(veneer), A/B 슬롯 부트로더 설계를 말할 수 있다.

## 1. 문제

크래시 덤프나 `objdump` 를 읽다 보면 `adrp x9, 0x20000` 다음 `ldr x9, [x9, #4032]` 같은 줄이 계속 나온다. "이 주소가 어디서 왔나요?", "GOT 는 왜 필요하죠?", "`relocation truncated to fit` 은 어떻게 고치죠?" 는 펌웨어·BSP·툴체인 면접의 단골 꼬리질문이다. 부트로더가 앱을 다른 주소에 올리거나 코드를 RAM 으로 복사하는 순간, 이 지식이 없으면 디버깅을 시작할 수가 없다.

AArch64 명령은 32비트 고정 길이다. 그래서 64비트 주소를 한 명령에 담을 수 없다. 해법은 두 단계다. **ADRP** 가 "PC 가 있는 4 KB 페이지 + 부호 있는 21비트 페이지 수" 로 목표 페이지(±4 GB)를 만든다. 그 다음 **ADD 또는 LDR/STR 의 12비트 칸**이 페이지 안 오프셋을 채운다. 둘 다 PC 기준이라 코드를 어디에 올려도 그대로 동작한다. 이것이 PIC(Position-Independent Code)다. 어셈블러는 주소를 모르므로 칸을 0 으로 두고 **리로케이션**(relocation, "여기 이 칸을 이 식으로 채워라" 라는 메모)을 남긴다. 링커나 로더가 나중에 그 칸을 채운다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">ADRP + ADD = 페이지 + 페이지 안 오프셋 (ld.bfd -pie 실측: use_all+0x8, g_val = 0x20010)</text>
  <rect x="10" y="34" width="220" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="56" fill="currentColor" font-family="monospace">P = 0x8b0</text>
  <text x="20" y="76" fill="currentColor" font-family="monospace">Page(P) = 0x0</text>
  <text x="20" y="96" fill="currentColor" font-size="12">PC 의 하위 12비트를 버린다</text>
  <rect x="260" y="34" width="230" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="270" y="56" fill="currentColor" font-family="monospace">90000109  adrp x9</text>
  <text x="270" y="76" fill="currentColor" font-family="monospace">imm21 = 0x20 (페이지 32개)</text>
  <text x="270" y="96" fill="currentColor" font-family="monospace">x9 = 0x0 + 0x20&lt;&lt;12 = 0x20000</text>
  <rect x="520" y="34" width="210" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="530" y="56" fill="currentColor" font-family="monospace">91004129  add x9,x9,#0x10</text>
  <text x="530" y="76" fill="currentColor" font-family="monospace">imm12 = 0x010</text>
  <text x="530" y="96" fill="currentColor" font-family="monospace">x9 = 0x20010 = &amp;g_val</text>
  <path d="M230 69 L258 69" stroke="currentColor" stroke-width="2"/>
  <path d="M250 63 L258 69 L250 75" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M490 69 L518 69" stroke="currentColor" stroke-width="2"/>
  <path d="M510 63 L518 69 L510 75" stroke="currentColor" fill="none" stroke-width="2"/>
  <rect x="10" y="124" width="720" height="160" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="146" fill="currentColor" font-weight="bold">.o 에선 칸이 0, 옆에 리로케이션 (readelf -rW demo_elf.o 실측, R_AARCH64_ 생략)</text>
  <text x="20" y="170" fill="currentColor" font-family="monospace" font-size="12">0x08 90000009 adrp x9, 0       ADR_PREL_PG_HI21   g_val    imm21=(Page(S+A)-Page(P))&gt;&gt;12</text>
  <text x="20" y="190" fill="currentColor" font-family="monospace" font-size="12">0x0c 91000129 add  x9, x9, #0  ADD_ABS_LO12_NC    g_val    imm12=(S+A)&amp;0xfff</text>
  <text x="20" y="210" fill="currentColor" font-family="monospace" font-size="12">0x1c f940012a ldr  x10, [x9]   LDST64_ABS_LO12_NC g_tab+8  imm12=((S+A)&amp;0xfff)/8</text>
  <text x="20" y="230" fill="currentColor" font-family="monospace" font-size="12">0x30 90000009 adrp x9, 0       ADR_GOT_PAGE       ext_val  S 대신 GOT 칸 주소 G</text>
  <text x="20" y="250" fill="currentColor" font-family="monospace" font-size="12">0x40 94000000 bl   0           CALL26             ext_func imm26=(S+A-P)&gt;&gt;2</text>
  <text x="20" y="272" fill="currentColor" font-size="12">Mach-O 이름: ARM64_RELOC_PAGE21 / PAGEOFF12 / GOT_LOAD_* / BRANCH26 (asm 표기 _g@PAGE)</text>
</svg>
```

이 레슨은 리로케이션을 **읽는 법**에서 끝나지 않는다(그건 TOOL-B04). 직접 **적용하는 코드**를 짠다. 그 코드를 맥과 VM 의 실제 링커 출력, 그리고 지금 돌고 있는 프로그램 자신과 대조한다.

### Part A — 적용기와 디코더 (C, 7개 함수)

- `apply_adrp(insn, P, S+A)` / `decode_adrp(insn, P)`: imm21 의 immlo `[30:29]` 와 immhi `[23:5]` 를 다룬다. 페이지 차이가 [−2²⁰, 2²⁰−1] 밖이면 `RELOC_RANGE`.
- `apply_add_lo12`, `apply_ldst_lo12(insn, S+A, size)`, `decode_lo12`: `size` 는 1/2/4/8/16 이다. LDR/STR 은 **오프셋을 size 로 나눈 값**을 넣는다. 나머지가 있으면 인코딩할 수 없으므로 `RELOC_ALIGN`.
- `apply_call26` / `decode_call26`: B/BL 의 imm26, 범위 [−2²⁷, 2²⁷−4], 4 B 정렬.
- 명령 종류가 리로케이션 타입과 안 맞으면 `RELOC_BADINSN` (예: ADRP 자리에 ADR, 8 B 타입인데 4 B LDR).
- 검증: 실제 링커가 만든 워드 19개(ld.bfd PIE/static, 경계 링크, ld64)를 "골든"으로 쓰고, 무작위 왕복 2만 번과 경계값을 더한다.

### Part B — 실행 중인 이미지 해부 + 함정 재현

- `shared_pic.S`(주어짐)에는 같은 이미지의 `pic_g`, `pic_tab` 을 `@PAGE/@PAGEOFF` 로 읽는 함수가 있다. 같은 `pic_g` 를 **일부러 GOT 로** 읽는 함수, libSystem 의 데이터 `__stdoutp` 를 GOT 로 읽는 함수, `b _abs` 꼬리 호출 함수도 있다. 각 명령에 전역 라벨 `site_*` 이 붙어 있다.
- 하네스는 실행 중에 `site_*` 의 **링크된 워드**를 읽는다. 디코더로 주소를 다시 계산해 C 의 `&pic_g` 와 비교한다. 같은 이미지 심볼을 요청한 GOT 로드는 링커가 ADD 로 바꿨는지 확인한다. 외부 심볼은 GOT 칸(`__DATA_CONST,__got`)의 내용이 정말 `&__stdoutp` 인지 본다. `b _abs` 는 stub(`__TEXT,__stubs`)으로 가는지, 그 stub 이 읽는 GOT 칸에 `abs` 가 들어 있는지 따라간다.
- 함정은 전부 실제로 재현한다(`demo_mac.sh`, `demo_elf.sh`). `@PAGE` 없는 adrp(어셈블러 에러), dylib 심볼을 `@PAGE` 로 참조(ld64 에러), non-PIC asm 을 `.so` 로 링크(`recompile with -fPIC`), ADRP 4 GB / BL 128 MB 경계 넘기(에러·베니어), 그리고 **4 B 정렬 심볼을 8 B LDR 의 `@PAGEOFF` 로 읽기**다. 마지막 것은 ld.bfd·ld.lld 는 에러인데 새 ld64 는 조용히 틀린 워드를 쓴다. 내 적용기는 이 경우 `RELOC_ALIGN` 을 내야 한다.

### Part C — 미니 링커와 링커 완화(relaxation)

- `relax_got_ldr_to_add`: GOT 를 읽는 `LDR Xt,[Xn,#..]` 를 `ADD Xt,Xn,#lo12(S)` 로 바꾼다. ld64·lld 가 같은 이미지 심볼에 하는 일이다.
- `relax_adrp_to_adr`: lld 의 `tryRelaxAdrpAdd` 규칙대로 `ADRP+ADD` 를 `NOP + ADR` 로 바꾼다(±1 MB 안일 때).
- `link_text(text, n, base, rela[], nr, sym[], relax)`: `demo_elf.o` 의 `.text` 워드(구멍 = 0)와 `readelf -r` 의 RELA 10개, 최종 심볼 주소를 받아 링크된 워드를 만든다. **ld.bfd -pie, ld.bfd -static, ld.lld --no-relax, ld.lld(완화)** 4가지 실제 결과와 19워드 전부 일치해야 한다.
- `check_reloc.sh`: 위 모든 것을 **실제 도구로 다시 빌드**한다. `objdump/nm/dyld_info`(맥)와 `readelf/objdump`(VM)에서 사실 122줄을 뽑아 내 디코더로 판정한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">명령 비트 배치 (Arm ARM C4.1 기준, 리로케이션이 고치는 칸 = 진한 칸)</text>
  <g stroke="currentColor">
    <rect x="120" y="36" width="20" height="24"/><rect x="140" y="36" width="40" height="24" class="f-acc"/><rect x="180" y="36" width="100" height="24"/><rect x="280" y="36" width="380" height="24" class="f-acc"/><rect x="660" y="36" width="70" height="24"/>
    <rect x="120" y="76" width="20" height="24"/><rect x="140" y="76" width="40" height="24" class="f-acc"/><rect x="180" y="76" width="100" height="24"/><rect x="280" y="76" width="380" height="24" class="f-acc"/><rect x="660" y="76" width="70" height="24"/>
    <rect x="120" y="116" width="230" height="24"/><rect x="350" y="116" width="240" height="24" class="f-acc"/><rect x="590" y="116" width="70" height="24"/><rect x="660" y="116" width="70" height="24"/>
    <rect x="120" y="156" width="230" height="24"/><rect x="350" y="156" width="240" height="24" class="f-acc"/><rect x="590" y="156" width="70" height="24"/><rect x="660" y="156" width="70" height="24"/>
    <rect x="120" y="196" width="120" height="24"/><rect x="240" y="196" width="490" height="24" class="f-acc"/>
    <rect x="120" y="236" width="160" height="24"/><rect x="280" y="236" width="380" height="24" class="f-acc"/><rect x="660" y="236" width="70" height="24"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="10" y="53">ADRP</text><text x="124" y="53">1</text><text x="146" y="53">immlo</text><text x="192" y="53">10000</text><text x="380" y="53">immhi [23:5]</text><text x="676" y="53">Rd</text>
    <text x="10" y="93">ADR</text><text x="124" y="93">0</text><text x="146" y="93">immlo</text><text x="192" y="93">10000</text><text x="380" y="93">immhi [23:5]</text><text x="676" y="93">Rd</text>
    <text x="10" y="133">ADD imm</text><text x="130" y="133">sf 00 100010 sh=0</text><text x="400" y="133">imm12 [21:10]</text><text x="606" y="133">Rn</text><text x="676" y="133">Rd</text>
    <text x="10" y="173">LDR/STR</text><text x="130" y="173">size 111 V 01 opc</text><text x="370" y="173">imm12 = off/size</text><text x="606" y="173">Rn</text><text x="676" y="173">Rt</text>
    <text x="10" y="213">B / BL</text><text x="130" y="213">x00101</text><text x="420" y="213">imm26 [25:0] = off/4</text>
    <text x="10" y="253">B.cond/CBZ</text><text x="130" y="253">01010100 / x011010o</text><text x="380" y="253">imm19 [23:5] = off/4</text><text x="672" y="253">cond/Rt</text>
  </g>
  <text x="120" y="290" fill="currentColor" font-size="12">비트 31 ← → 비트 0. ADRP = 페이지 수(±4 GB), ADR = 바이트 수(±1 MB)</text>
  <text x="120" y="310" fill="currentColor" font-size="12">imm21 = immhi:immlo, 하위 2비트(immlo)가 [30:29] 에 따로 있다. TBZ 는 imm14 [18:5]</text>
</svg>
```

## 2. Problem (English)

> **AArch64 relocation engine (ELF + Mach-O).**
>
> Implement, in C, the instruction patchers a linker uses for position-independent AArch64 code:
>
> ```c
> enum { RELOC_OK, RELOC_RANGE, RELOC_ALIGN, RELOC_BADINSN };
> int      apply_adrp(uint32_t *insn, uint64_t P, uint64_t SA);         // ADR_PREL_PG_HI21 / PAGE21 / GOT pages
> uint64_t decode_adrp(uint32_t insn, uint64_t P);
> int      apply_add_lo12(uint32_t *insn, uint64_t SA);                // ADD_ABS_LO12_NC / PAGEOFF12 on ADD
> int      apply_ldst_lo12(uint32_t *insn, uint64_t SA, unsigned size); // LDSTn_ABS_LO12_NC / PAGEOFF12 on LDR/STR
> uint64_t decode_lo12(uint32_t insn);                                  // byte offset encoded in ADD/LDR/STR
> int      apply_call26(uint32_t *insn, uint64_t P, uint64_t SA);       // CALL26 / JUMP26 / BRANCH26
> uint64_t decode_call26(uint32_t insn, uint64_t P);
> int relax_got_ldr_to_add(uint32_t ldr, uint64_t S, uint32_t *add_out);
> int relax_adrp_to_adr(uint32_t adrp, uint32_t add, uint64_t P_add, uint64_t S, uint32_t out[2]);
> int link_text(uint32_t *text, size_t n, uint64_t base, const rela *r, size_t nr, const symval *sym, int relax);
> ```
>
> Constraints: preserve every bit outside the patched field; ADRP range ±4 GB (page delta in [−2²⁰, 2²⁰−1]); CALL26 range [−2²⁷, 2²⁷−4] and 4-byte aligned; scaled LDR/STR offsets must be multiples of the access size; reject an instruction that does not match the relocation type.
>
> Example: `P = 0x8b0, S+A = 0x20010`: `adrp x9` `0x90000009 → 0x90000109`, `add x9,x9,#0` `0x91000129 → 0x91004129`. `P = 0x10008, S = 0x8010008` for a BL → `RELOC_RANGE` (ld.bfd inserts a veneer here).
>
> Then prove it on the running process: decode the linked `ADRP+ADD` of `pic_addr_local` back to `&pic_g`, follow `b abs` through the stub into the GOT slot, and reproduce the exact words that ld.bfd and ld.lld (with and without relaxation) produced for a 10-relocation object.
>
> Follow-ups: Why page-granular ADRP instead of a 21-bit byte offset? When must a reference go through the GOT? What does "relocation truncated to fit" mean and how do you fix it? Why is Cortex-M firmware usually not PIC?

## 3. 시작 코드

주어진 asm (TODO 아님). Mach-O/ELF 겸용이다. `_g@PAGE` ↔ `g`, `_g@GOTPAGEOFF` ↔ `:got_lo12:g` 를 매크로로 바꾼다. 매크로가 2단계인 이유: `_##s` 처럼 붙이기(`##`)를 쓰면 인자 `EXT_DATA` 가 먼저 펼쳐지지 않는다. 처음에 1단계로 짰을 때 실제로 `Undefined symbols: "_EXT_DATA"` 링크 에러가 났다.

<!-- code: shared_pic.S -->

여기를 채운다 (하네스 포함):

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c shared_pic.S -o /tmp/ARM-A09_starter && /tmp/ARM-A09_starter
```

실제 출력(발췌). 스텁은 적용기가 `-1`, 디코더가 `0` 을 돌려준다. 하네스는 디코딩한 주소가 GOT/stub 섹션 안에 있을 때만 역참조하므로 스텁 상태에서도 죽지 않는다.

```text
  golden[0] bfd adrp g_val     err=-1 word=90000009 (want err=0 word=90000109)
FAIL starter.c:176  ok
…
FAIL starter.c:179  decode_adrp(0xf07fffe0, 0x10000) == 0x10000F000
…
info: mini-linker vs ld.bfd -pie        err=-1  words differing=10/19
…
11/76 passed
```

통과하는 11개는 asm 자체 동작(`pic_addr_local() == &pic_g` 등)과 주어진 도우미 검사다. `words differing=10/19` 는 RELA 10개가 고칠 워드 10개가 그대로 0 이라는 뜻이다.

## 4. 힌트

<details>
<summary>힌트 1 — ADRP 는 "바이트 차이" 가 아니라 "페이지 차이"</summary>

`imm21 = (Page(S+A) − Page(P)) >> 12`, `Page(x) = x & ~0xFFF`. P 의 하위 12비트도 버린다는 점이 핵심이다. 그래서 `P = 0x10FFF` 와 `0x10000` 은 같은 결과를 낸다. 뺄셈 결과는 음수일 수 있으므로 `int64_t` 로 바꾼 뒤 **산술** 시프트한다. 그 다음 하위 2비트를 immlo `[30:29]` 에, 나머지 19비트를 immhi `[23:5]` 에 넣는다.

</details>

<details>
<summary>힌트 2 — 디코드에서 부호 확장을 잊지 말 것</summary>

`imm = immlo | immhi << 2` 는 21비트 2의 보수다. `0xF07FFFE0` 의 imm 은 `0xFFFFF`(양수, +4 GB−4 KB)다. 앞 페이지를 가리키는 ADRP 는 imm 최상위 비트가 1 이다. `sext(imm, 21) << 12` 를 Page(P) 에 더한다. 주어진 `sext()` 를 쓰면 된다.

</details>

<details>
<summary>힌트 3 — LDR/STR 의 12비트는 "스케일된" 값</summary>

`ldr x10, [x9, #4032]` 의 워드에는 4032 가 아니라 4032/8 = 504 가 들어 있다. 접근 크기는 `size` 비트 `[31:30]` 이 정한다(`1 << size`). 예외는 SIMD 128비트(`V=1, size=00, opc=1x`)로 16 이다. `(S+A) & 0xFFF` 가 크기의 배수가 아니면 어떤 imm12 로도 표현할 수 없다. 이게 `RELOC_ALIGN` 이다.

</details>

<details>
<summary>힌트 4 — 완화(relaxation)는 "같은 레지스터, 바로 옆 명령, addend 0, 선점 불가"</summary>

lld 가 `ADRP+LDR(GOT)` 를 바꾸려면 조건이 있다. 두 리로케이션이 연속(off, off+4)이고, 같은 심볼이고, addend 가 둘 다 0 이고, 심볼이 정의되어 있고 **선점(preempt) 불가**여야 한다. `adrp Xd` 의 Xd 가 `ldr Xd, [Xd]` 의 두 레지스터와 같아야 한다. 그러면 먼저 `ADRP+ADD` 로 바꾼다. 거리가 ±1 MB 안이면 다시 `NOP` + `ADR Xd, S` 로 바꾼다. ADR 은 **두 번째 칸**에 들어가므로 PC 는 ADD 자리의 주소다.

</details>

<details>
<summary>힌트 5 — link_text 의 S 는 타입마다 다르다</summary>

일반 타입은 `S + A`. `ADR_GOT_PAGE`·`LD64_GOT_LO12_NC` 는 심볼 주소가 아니라 **GOT 칸 주소** `G` 를 쓴다(`sym[].got`). P 는 `base + r_offset` 이다. 에러가 나면 몇 번째 엔트리인지 돌려줘야 디버깅이 된다: `(i+1)*16 + code`.

</details>

## 5. 풀이

### 5.1 Part A — 비트를 손으로 한 번 따라가기

`ld.bfd -pie` 결과 하나를 끝까지 계산해 본다(`demo_elf.txt`).

| 리로케이션 | P | S+A (nm) | 계산 | .o 워드 → 링크 워드 |
|---|---|---|---|---|
| `ADR_PREL_PG_HI21 g_val` | 0x8b0 | 0x20010 | (0x20000−0x0)>>12 = 0x20 → immlo 0, immhi 0x8 → `0x8<<5 = 0x100` | `90000009 → 90000109` |
| `ADD_ABS_LO12_NC g_val` | 0x8b4 | 0x20010 | imm12 = 0x010 → `0x10<<10 = 0x4000` | `91000129 → 91004129` |
| `LDST64 g_tab+8` | 0x8c4 | 0x20020 | 0x020/8 = 4 → `4<<10 = 0x1000` | `f940012a → f940112a` |
| `LDST16 g_half` | 0x8d0 | 0x20030 | 0x030/2 = 24 → `0x6000` | `7940012a → 7940612a` |
| `ADR_GOT_PAGE ext_val` | 0x8d8 | **G** = 0x1ffc0 | (0x1f000−0)>>12 = 31 = 0b11111 → immlo **3** (`0x60000000`), immhi 7 (`0xe0`) | `90000009 → f00000e9` |
| `LD64_GOT_LO12_NC ext_val` | 0x8dc | G = 0x1ffc0 | 0xfc0/8 = 504 → `0x7e000` | `f9400129 → f947e129` |
| `CALL26 ext_func` | 0x8e8 | 0x900 | (0x900−0x8e8)/4 = 6 | `94000000 → 94000006` |
| `JUMP26 ext_tail` | 0x8f0 | 0x908 | 6 | `14000000 → 14000006` |

다섯째 줄을 보자. **같은 ADRP 인데 첫 바이트가 `90` 에서 `f0` 으로 바뀌었다.** immlo 가 비트 29–30 에 있기 때문이다. objdump 의 `f00000e9 adrp x9, 1f000` 이 낯설어 보이는 이유가 이것이다. 크래시 덤프에서 `b0…`, `d0…`, `f0…` 로 시작하는 워드는 대개 ADRP 다(op=1 이라 최상위 비트 1, 그 아래 두 비트는 immlo).

범위 경계는 VM 의 링커 스크립트로 직접 만들었다(`demo_range.S`, `demo_elf.sh` 의 range 절 실측):

```text
-- far_data at 0x10000F000 (adrp at 0x10000), ld.bfd
   10000:	f07fffe0 	adrp	x0, 10000f000 <far_data>
-- far_data at 0x100010000 (adrp at 0x10000), ld.bfd
(.text.near+0x0): relocation truncated to fit: R_AARCH64_ADR_PREL_PG_HI21 against symbol `far_data' defined in .data.far section in range.o
-- far_func at 0x8000FFC (bl at 0x10008), ld.bfd
   10008:	95ffc3fd 	bl	8000ffc <far_func>
-- far_func at 0x8010008 (bl at 0x10008), ld.bfd
   10008:	94000004 	bl	10018 <__far_func_veneer>
0000000000010018 <__far_func_veneer>:
   10018:	90040010 	adrp	x16, 8010000 <far_data+0x7fe0000>
   1001c:	91002210 	add	x16, x16, #0x8
   10020:	d61f0200 	br	x16
-- far_func at 0x8010008 (bl at 0x10008), ld.lld
   10008:	94000002 	bl	10010 <__AArch64AbsLongThunk_far_func>
   10010:	15fffffe 	b	8010008 <far_func>
```

| 대상 | 최대 (+) | 실측 경계 | 넘으면 |
|---|---|---|---|
| ADRP | +4 GB − 4 KB (imm21 = 0xFFFFF) | `0x10000 → 0x10000F000` OK, `0x100010000` 에러 | `relocation truncated to fit` (자동 해결 없음) |
| BL/B | +128 MB − 4 | 거리 `0x7FF0FF4` OK, 정확히 `0x8000000` 밖 | ld.bfd: `__far_func_veneer` (ADRP+ADD+BR x16) 삽입. lld: `__AArch64AbsLongThunk_*` |

lld 썽크(thunk)가 `b` 한 줄인 것도 실측이다. 썽크가 `bl` 보다 8 B 뒤(목표 쪽)에 놓여서 거리가 `0x7FFFFF8` 로 줄었고, 그래서 B 한 번으로 닿았다. 이름은 "AbsLong" 이지만 이번 배치에서는 짧은 형태가 나왔다. 베니어가 x16/x17(IP0/IP1)을 쓰는 것은 AAPCS64 가 "링커가 끼워 넣는 코드를 위한 scratch" 로 이 두 레지스터를 정해 두었기 때문이다(문서 확인 권장). 그래서 함수 진입 직후 x16/x17 값은 믿으면 안 된다.

Mach-O 이름과 ELF 이름은 1:1 이다. 단 Mach-O 는 하위 12비트를 `PAGEOFF12` 하나로 부르고 **명령을 보고** add 인지 몇 바이트 ldr 인지 판단한다(`apply_named()` 의 `PAGEOFF12` 분기). ELF 는 타입 이름에 크기를 담는다.

| 하는 일 | Mach-O (`otool -rv` 축약) | ELF |
|---|---|---|
| ADRP 페이지 | `PAGE21` (`PAGE21`) | `ADR_PREL_PG_HI21` |
| ADD 하위 12 | `PAGEOFF12` (`PAGOF12`) | `ADD_ABS_LO12_NC` |
| LDR/STR 하위 12 | `PAGEOFF12` | `LDST{8,16,32,64,128}_ABS_LO12_NC` |
| GOT 칸 페이지 / 칸 로드 | `GOT_LOAD_PAGE21` / `GOT_LOAD_PAGEOFF12` (`GOTLDP`/`GOTLDPOF`) | `ADR_GOT_PAGE` / `LD64_GOT_LO12_NC` |
| B/BL | `BRANCH26` (`BR26`) | `JUMP26` / `CALL26` |
| 상수 덧셈 | 별도 엔트리 `ARM64_RELOC_ADDEND` | RELA 의 `r_addend` 칸 |
| 데이터 안 포인터 | `UNSIGNED` → 링크 후 chained fixup | `ABS64` → PIE 면 `R_AARCH64_RELATIVE` |

`ADDEND` 는 실제로 나왔다(`demo_mac.txt` 1절). `ldr x0, [x8, _pic_tab@PAGEOFF + 16]` 한 줄이 엔트리 두 개가 됐다:

```text
00000010 False long   False  ADDEND  False     addend = 0x000010
00000010 False long   True   PAGOF12 False     _pic_tab
```

### 5.2 Part B — 실행 중인 나 자신을 디코딩하기

맥에서 `solution.c + shared_pic.S` 를 링크한 결과(`demo_mac.txt` 2절):

```text
0000000100002a4c <_pic_addr_local>:
100002a4c: d0000020    	adrp	x0, 0x100008000 <_g_rng>
100002a50: 91002000    	add	x0, x0, #0x8
0000000100002a64 <_pic_addr_local_got>:
100002a64: d0000020    	adrp	x0, 0x100008000 <_g_rng>
100002a68: 91002000    	add	x0, x0, #0x8          ← .o 에선 ldr x0, [x0] (GOT_LOAD_PAGEOFF12)
0000000100002a70 <_pic_addr_ext_got>:
100002a70: d0000000    	adrp	x0, 0x100004000 <_strcspn+0x100004000>
100002a74: f9400c00    	ldr	x0, [x0, #0x18]       ← __got + 0x18 = bind libSystem/___stdoutp
0000000100002a7c <_pic_call_abs>:
100002a7c: 14000004    	b	0x100002a8c           ← __stubs 의 두 번째 stub
100002a8c: d0000010    	adrp	x16, 0x100004000
100002a90: f9401210    	ldr	x16, [x16, #0x20]     ← __got + 0x20 = bind libSystem/_abs
100002a94: d61f0200    	br	x16
```

(`<_g_rng>` 는 objdump 가 가장 가까운 심볼을 붙인 것뿐이다. `_pic_g` 는 `0x100008008` 이다.) 여기서 세 가지를 읽을 수 있다.

1. **같은 이미지 심볼은 GOT 가 사라진다.** `pic_addr_local_got` 은 GOT 를 요청했지만 ld64 가 `ldr` 을 `add` 로 바꿨다. GOT 칸이 아예 없다(`dyld_info -fixups` 에 `_pic_g` bind 가 없음). `pic_g` 가 실행 파일 안에 있으니 실행 시점에 바뀔 일이 없기 때문이다. 이번에는 `.loh`(Linker Optimization Hint) 없이도 일어났다. 반대로 `demo_loh.S` 에 `.loh AdrpAdd` 를 달아도 ld64 는 `adrp+add` 를 ADR 로 바꾸지 **않았다**(7절 실측, 거리 약 15 KB). 어떤 조건에서 LOH 를 적용하는지는 확인하지 못했다.
2. **다른 이미지 데이터는 GOT 를 꼭 거친다.** `__stdoutp` 의 주소는 dyld 가 libSystem 을 어디에 올렸는지(ASLR) 알아야 정해진다. 코드(`__TEXT`)는 읽기 전용이고 여러 프로세스가 공유하므로 고칠 수 없다. 그래서 고칠 수 있는 데이터 칸 하나(`__DATA_CONST,__got`)에 주소를 두고 코드는 그 칸을 PC 상대로 읽는다.
3. **다른 이미지 함수는 stub → GOT.** `b _abs` 의 BRANCH26 은 ±128 MB 라서 dylib 까지 닿는다는 보장이 없다. 그래서 링커가 같은 이미지 안에 3명령짜리 stub(`adrp x16 / ldr x16 / br x16`)을 만들고 그쪽으로 분기시킨다.

하네스는 이 셋을 실행 중에 검사한다. 출력(실측):

```text
info: local symbol via GOT -> linker relaxed LDR to ADD (no GOT slot used) (word[1]=91002000)
info: b abs -> stub +16 B from site, stub reads GOT slot +5524 B from stub
```

같은 `solution.c` 를 VM(gcc 15.2 + ld.bfd 2.46, PIE)에서 돌리면 결과가 다르다(실측):

```text
info: local symbol via GOT -> linker kept the GOT load (word[1]=f947f400)
info: b abs -> stub -9160 B from site, stub reads GOT slot +127816 B from stub
76/76 passed
```

ld.bfd 는 같은 이미지 심볼의 GOT 로드를 **그대로 둔다**. 이것은 `demo_elf.txt` 의 `pie_bfd` 에서도 보인다(`f947e129 ldr x9, [x9, #4032]` + `.rela.dyn` 의 `0x1ffc0 RELATIVE 20040`). 리눅스의 stub 은 PLT(`abs@plt`)다. 하네스의 stub 워커가 맥 stub(`ldr x16`)과 PLT(`ldr x17`) 둘 다 "ADRP + 64비트 LDR" 로 시작한다는 것만 확인하는 이유가 이것이다.

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">dylib 심볼 접근 = 코드는 그대로, 데이터 칸(GOT)만 로더가 채움 (demo_mac.txt 3절 실측)</text>
  <rect x="10" y="34" width="300" height="130" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor" font-weight="bold">__TEXT,__text  _scaled</text>
  <text x="20" y="76" fill="currentColor" font-family="monospace" font-size="12">5bc: 9400000d bl  0x1000005f0</text>
  <text x="20" y="98" fill="currentColor" font-family="monospace" font-size="12">5c0: 90000028 adrp x8, 0x100004000</text>
  <text x="20" y="120" fill="currentColor" font-family="monospace" font-size="12">5c4: f9400108 ldr  x8, [x8]</text>
  <text x="20" y="142" fill="currentColor" font-family="monospace" font-size="12">5c8: f9400108 ldr  x8, [x8] (값)</text>
  <rect x="10" y="184" width="300" height="100" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="204" fill="currentColor" font-weight="bold">__TEXT,__stubs</text>
  <text x="20" y="226" fill="currentColor" font-family="monospace" font-size="12">5f0: 90000030 adrp x16, 0x100004000</text>
  <text x="20" y="248" fill="currentColor" font-family="monospace" font-size="12">5f4: f9400610 ldr  x16, [x16, #0x8]</text>
  <text x="20" y="270" fill="currentColor" font-family="monospace" font-size="12">5f8: d61f0200 br   x16</text>
  <rect x="380" y="34" width="350" height="130" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="390" y="54" fill="currentColor" font-weight="bold">__DATA_CONST,__got (파일 안 원본 → 로드 후)</text>
  <text x="390" y="78" fill="currentColor" font-family="monospace" font-size="12">0x100004000: 8010000000000000</text>
  <text x="390" y="96" fill="currentColor" font-family="monospace" font-size="12">  bind ord 0 next 2 → &amp;_sensor_gain</text>
  <text x="390" y="120" fill="currentColor" font-family="monospace" font-size="12">0x100004008: 8010000000000001</text>
  <text x="390" y="138" fill="currentColor" font-family="monospace" font-size="12">  bind ord 1 next 2 → &amp;_sensor_read</text>
  <text x="390" y="158" fill="currentColor" font-family="monospace" font-size="12">0x100004010: 8000000000000002 (_printf, 끝)</text>
  <rect x="380" y="200" width="350" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="390" y="222" fill="currentColor" font-weight="bold">libsensor.dylib (ASLR 로 어디든)</text>
  <text x="390" y="246" fill="currentColor" font-family="monospace" font-size="12">_sensor_read / _sensor_gain</text>
  <path d="M310 76 L330 76 L330 226 L312 226" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M320 220 L312 226 L320 232" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M310 248 L360 248 L360 128 L378 128" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M370 122 L378 128 L370 134" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M310 109 L378 86" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M555 164 L555 198" stroke="currentColor" stroke-width="2" stroke-dasharray="5 4"/>
  <path d="M549 190 L555 198 L561 190" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="565" y="186" fill="currentColor" font-size="12">dyld 가 로드 때 덮어씀</text>
  <text x="10" y="306" fill="currentColor" font-size="12">bl → stub → GOT 칸 → dylib. 데이터는 adrp+ldr 로 칸에서 주소를 읽고, 한 번 더 ldr 로 값을 읽는다.</text>
</svg>
```

**chained fixups.** 맥의 새 포맷에서 `__got` 의 파일 내용은 주소가 아니라 "할 일 목록" 이다. 같은 바이트를 두 도구로 읽으면(`demo_mac.txt` 3절):

```text
0000000100004000	00000000 80100000 00000001 80100000          (otool -s, 리틀엔디안 32비트 묶음)
  0x00004000:  raw: 0x8010000000000000         bind: (next: 002, bindOrdinal: 0x000000)
  0x00004008:  raw: 0x8010000000000001         bind: (next: 002, bindOrdinal: 0x000001)
  0x00004010:  raw: 0x8000000000000002         bind: (next: 000, bindOrdinal: 0x000002)
  0x00008008:  raw: 0x0010000000008000       rebase: (next: 002, target: 0x00000008000)
  0x00008010:  raw: 0x8000000000000000         bind: (next: 000, bindOrdinal: 0x000000)
```

비트 63 = bind 여부, `[62:51]` = 다음 픽스업까지 거리(4 B 단위). bind 면 `[23:0]` = import 번호, rebase 면 `[35:0]` = 이미지 기준 오프셋이다. dyld 는 페이지마다 이 연결 리스트를 따라가며 진짜 포인터로 덮어쓴다. ELF 의 `.rela.dyn` 표(`R_AARCH64_RELATIVE`, `GLOB_DAT`, `JUMP_SLOT`)가 하는 일을 데이터 자체에 접어 넣은 셈이다. 리눅스는 기본이 지연 바인딩(lazy binding)일 수 있다. 그래서 PLT 의 GOT 칸은 첫 호출 전에 resolver 를 가리킬 수 있다(배포판의 `-z now` 기본값에 따라 다름, 확인 권장). 하네스가 `pic_call_abs(-5)` 를 먼저 한 번 부르는 이유다. 연습 5에서 이 체인을 직접 걷는다.

**함정 재현 — 전부 실제 출력.**

| 실수 | 도구 | 실제 결과 |
|---|---|---|
| `adrp x0, _local_bias` (`@PAGE` 없음) | 맥 clang 어셈블러 | `error: ADR/ADRP relocations must be GOT relative` + `unknown AArch64 fixup kind!` |
| dylib 심볼을 `adrp/ldr ..@PAGE/@PAGEOFF` 로 | ld64 | `ld: fixup error (kind=arm64_adrp_lo12) at '_f3' from bad3.o, target '_sensor_gain' does not have address` |
| non-PIC asm(`adrp x9, g_val`)을 `-shared` 로 | ld.bfd | `relocation R_AARCH64_ADR_PREL_PG_HI21 against symbol `g_val' which may bind externally can not be used when making a shared object; recompile with -fPIC` |
| 〃 | ld.lld | `relocation R_AARCH64_ADR_PREL_PG_HI21 cannot be used against symbol 'g_val'; recompile with -fPIC` |
| 4 B 정렬 `odd` 를 8 B `ldr` 의 `:lo12:` 로 | ld.bfd | `relocation truncated to fit: R_AARCH64_LDST64_ABS_LO12_NC` + 정렬 원인 경고 |
| 〃 | ld.lld | `improper alignment for relocation R_AARCH64_LDST64_ABS_LO12_NC: 0x22016C is not aligned to 8 bytes` |
| 〃 (`@PAGEOFF`) | **ld64 기본 (ld-1266.8)** | **경고 없이 링크.** `ldr x0, [x8]` 을 써서 `f2()=0x504030201 (expected 0x5)` |
| 〃 | ld64 `-ld_classic` | `ld: 64-bit LDR/STR not 8-byte aligned: from _f2 (0x100003F74) to _odd (0x100008004)` |

마지막 두 줄이 이 레슨에서 가장 무서운 관찰이다. `_odd = 0x100008004` 일 때 `0x004/8` 은 정수가 아니다. 새 링커는 몫 0 을 넣고 넘어갔다. 그 결과 `0x100008000` 부터 8 B(`01 02 03 04 05 00 00 00`)를 읽었다. 공유 라이브러리의 `-fPIC` 에러는 "이 심볼은 다른 `.so` 가 같은 이름으로 대체(interpose)할 수 있으니 PC 상대로 고정하면 안 된다" 는 뜻이다. 고치는 방법은 둘이다. GOT 를 쓰거나(`:got:`), 심볼을 `.hidden` 으로 만들어 선점 불가로 한다.

### 5.3 Part C — 미니 링커와 완화

`link_text` 는 Part A 함수들의 디스패처다. relax=1 일 때만 연속 쌍을 먼저 본다. 조건은 lld 21.1.8 소스(`lld/ELF/Arch/AArch64.cpp` 의 `AArch64Relaxer::tryRelaxAdrpAdd` / `tryRelaxAdrpLdr`, WebFetch 로 확인, 줄 번호는 미확인)의 검사를 그대로 옮겼다.

| lld 검사 (소스 인용 요약) | 내 구현 |
|---|---|
| `adrpRel.offset + 4 != addRel.offset` → 포기 | `r[i+1].off == a->off + 4` |
| `adrpRel.sym != ...sym`, `addend != 0` → 포기 | 같은 `sym`, 둘 다 `addend == 0` |
| GOT 쪽: `!isDefined() \|\| isPreemptible \|\| isGnuIFunc()` → 포기 | `!sym[].preemptible` (ifunc 는 모델에 없음) |
| `(ldrInstr & 0x3b000000) != 0x39000000`, `sf` 비트 | `relax_got_ldr_to_add` 의 `is_ldr_x_uimm` |
| `adrpDestReg != addDestReg \|\| != addSrcReg` | `relax_adrp_to_adr` 의 Rd/Rn 비교 |
| `val < -1024*1024 \|\| val >= 1024*1024` → ADR 포기 | `[-1 MB, 1 MB)` |
| GOT→ADD 는 `val != SignExtend64(val, 33)` (±4 GB) 이면 포기 | `apply_adrp` 의 범위 검사로 대신 |

lld 실측 전후(`demo_elf.txt`):

```text
pie_lld_norelax:  1097c: 90000109 adrp x9, 30000     pie_lld:  1097c: d503201f nop
                  10980: 91334129 add  x9, x9, #0xcd0          10980: 10101a89 adr x9, 30cd0 <g_val>
                  109a4: 90000089 adrp x9, 20000               109a4: d503201f nop
                  109a8: f9463d29 ldr  x9, [x9, #3192]         109a8: 10101ac9 adr x9, 30d00 <ext_val>
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">lld 완화 2단계 (ext_val: 같은 실행 파일 안, 선점 불가)</text>
  <rect x="10" y="36" width="220" height="90" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="58" fill="currentColor" font-weight="bold">원래 (GOT)</text>
  <text x="20" y="82" fill="currentColor" font-family="monospace" font-size="12">adrp x9, :got:ext_val</text>
  <text x="20" y="104" fill="currentColor" font-family="monospace" font-size="12">ldr  x9, [x9, :got_lo12:]</text>
  <rect x="260" y="36" width="220" height="90" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="270" y="58" fill="currentColor" font-weight="bold">1단계: GOT → ADD</text>
  <text x="270" y="82" fill="currentColor" font-family="monospace" font-size="12">adrp x9, ext_val</text>
  <text x="270" y="104" fill="currentColor" font-family="monospace" font-size="12">add  x9, x9, :lo12:ext_val</text>
  <rect x="510" y="36" width="220" height="90" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="520" y="58" fill="currentColor" font-weight="bold">2단계: ±1 MB → ADR</text>
  <text x="520" y="82" fill="currentColor" font-family="monospace" font-size="12">nop</text>
  <text x="520" y="104" fill="currentColor" font-family="monospace" font-size="12">adr  x9, ext_val</text>
  <path d="M230 81 L258 81" stroke="currentColor" stroke-width="2"/><path d="M250 75 L258 81 L250 87" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M480 81 L508 81" stroke="currentColor" stroke-width="2"/><path d="M500 75 L508 81 L500 87" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="20" y="152" fill="currentColor" font-size="12">메모리 로드 1번 제거 (GOT 칸 읽기)</text>
  <text x="270" y="152" fill="currentColor" font-size="12">명령 수는 그대로 2개</text>
  <text x="520" y="152" fill="currentColor" font-size="12">의존 사슬 1단, 크기 그대로</text>
  <text x="20" y="184" fill="currentColor" font-size="12">못 하는 경우: 다른 이미지 / 선점 가능 심볼 / addend ≠ 0 / 쌍이 떨어짐 / 레지스터 다름 / 범위 밖</text>
  <text x="20" y="206" fill="currentColor" font-size="12">GOT 칸은 완화 후에도 남는다: pie_lld 의 .rela.dyn 에 0x20c78 RELATIVE 30d00 이 그대로 있음 (실측)</text>
  <text x="20" y="228" fill="currentColor" font-size="12">ld.bfd 2.46 은 이 경우 아무 완화도 하지 않았다 (adrp+ldr GOT 그대로, 실측)</text>
</svg>
```

정답 코드의 실행 결과(맥, 실측):

```text
info: local symbol via GOT -> linker relaxed LDR to ADD (no GOT slot used) (word[1]=91002000)
info: b abs -> stub +16 B from site, stub reads GOT slot +5524 B from stub
info: mini-linker vs ld.bfd -pie        err=0  words differing=0/19
info: mini-linker vs ld.bfd -static     err=0  words differing=0/19
info: mini-linker vs ld.lld --no-relax  err=0  words differing=0/19
info: mini-linker vs ld.lld (relax)     err=0  words differing=0/19
76/76 passed
```

ASan/UBSan(`cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c shared_pic.S`)도 `76/76 passed` 로 깨끗했다. VM 의 gcc 15.2 / clang 21 빌드도 전부 통과했다(GOT/stub 줄만 위처럼 다름).

**뮤턴트(mutant) 확인.** 정답의 사본(스크래치 폴더)에 버그 6개를 하나씩 넣고 돌렸다. 전부 잡혔다:

| 주입한 버그 | 결과 |
|---|---|
| ADRP 범위 `> 2^20` (하나 큼) | 75/76 — 골든 `adrp +4GB` |
| decode_adrp 부호 확장 생략 | 74/76 — 무작위 왕복 |
| LDR 정렬 검사 삭제 (= ld64 의 실제 동작) | 74/76 — 골든 `macho misaligned` |
| ADR 완화에서 ADRP 의 P 사용 | 74/76 — lld 비교 |
| GOT 타입에 S 를 그대로 사용 | 72/76 — bfd/lld 비교 4건 |
| CALL26 정렬 검사 삭제 | 75/76 — `0x1002` 대상 |

**check_reloc.sh** (실측, 약 2 초):

```text
cross-tool PASS tool: _pic_g GOT load relaxed to ADD, symbol defined in image
cross-tool PASS tool: GOT slot 0x100004018 is dyld_info bind -> ___stdoutp
cross-tool PASS tool: stub 0x100002a8c -> GOT 0x100004020 bind -> _abs
cross-tool PASS app: stub 0x1000005f0 -> GOT 0x100004008 bind -> _sensor_read
cross-tool PASS app: GOT slot 0x100004000 is dyld_info bind -> _sensor_gain
cross-tool PASS pie_bfd: GOT slot 0x1ffc0 holds/relocates to ext_val=0x20040
cross-tool PASS static_bfd: GOT slot 0x49fec0 holds/relocates to ext_val=0x4a0070
cross-tool PASS pie_lld_norelax: GOT slot 0x20c78 holds/relocates to ext_val=0x30d00
122/122 passed
check_reloc: OK
```

122줄은 디코드 76줄(objdump 가 찍은 대상 = 내 디코더 값)과 적용 44줄(.o 워드 + 리로케이션 + nm/dyld_info/readelf 의 주소 → 링크된 워드), lld 완화 쌍 2줄이다. 사실 추출은 파이썬이 하지만 숫자를 옮기기만 한다. 비트 계산과 판정은 `solution` 의 `tool` 모드가 한다. `bash check_reloc.sh starter` 는 `10/122` 로 실패한다(`exit 1`).

### 5.4 펌웨어 쪽 연결

- **Cortex-M 은 보통 non-PIC.** 링커 스크립트가 플래시/RAM 주소를 고정하고, 동적 로더가 없으니 GOT 가 필요 없다. 같은 C 를 `thumbv7em` 로 컴파일하면 절대 주소 `movw/movt` + `R_ARM_THM_MOVW_ABS_NC/MOVT_ABS` 가 나온다(`demo_ropi.txt`).
- **`-fropi -frwpi`** (위치 독립 RO / RW). 같은 함수가 이렇게 바뀌었다(실측). RO(`table`)는 `R_ARM_THM_MOVW_PREL_NC` + `add r3, pc` 로 PC 상대가 된다. RW(`counter`)는 `R_ARM_THM_MOVW_BREL_NC` + `ldr.w r2, [r9, r1]` 로 **r9 = static base** 상대가 된다. 앱을 어느 슬롯에 올리든 r9 만 RAM 영역에 맞추면 된다. 대신 r9 를 일반 레지스터로 못 쓰고, 모든 RW 접근에 덧셈이 붙는다. 툴체인 지원은 Arm Compiler/clang 위주이고 GCC 는 제한적이다(확인 권장).
- **베니어**: 큰 SoC 펌웨어(코드가 ITCM 과 DDR 에 나뉨, AArch64 R-profile/A-profile 부트 코드)에서 BL 이 128 MB 를 넘으면 링커가 x16 을 쓰는 베니어를 끼운다. 함수 진입 직후 x16/x17 값이 깨져 보이는 이유다. Cortex-M Thumb BL 은 ±16 MB 이고 링커가 `__*_veneer` 를 만든다(문서 확인 권장).
- **코드를 RAM 으로 복사**(XIP 플래시 → SRAM)할 때: PC 상대 코드는 그냥 돌지만 절대 주소(`movw/movt`, 리터럴 풀, `.data` 의 함수 포인터)는 링크 주소를 가리킨다. 부트로더가 RELATIVE 류 테이블을 적용하거나(우리의 `link_text` 가 하는 일의 축소판), 처음부터 실행 주소(VMA)로 링크하고 로드 주소(LMA)만 플래시에 둔다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c shared_pic.S -o /tmp/ARM-A09_solution && /tmp/ARM-A09_solution
bash check_reloc.sh                     # 맥 + VM(elinux) 실제 도구 대조, OK 면 exit 0
ARM_A09_NO_VM=1 bash check_reloc.sh     # VM 없이 맥 부분만
```

check_reloc.sh 전체:

<!-- code: check_reloc.sh -->

관찰 스크립트: `bash demo_mac.sh > demo_mac.txt` (맥), `limactl shell elinux -- bash -lc 'bash <레슨 경로>/demo_elf.sh'` (VM, `demo_elf.S`/`demo_elf_ext.c`/`demo_elf_main.c`/`demo_range.S`/`demo_misalign.S` 사용), `bash demo_ropi.sh` (Cortex-M ROPI/RWPI). 캡처는 `demo_mac.txt`, `demo_elf.txt`, `demo_practice.txt`, `demo_ropi.txt`.

## 7. 자주 하는 실수

**1. decode 에서 Page(P) 대신 P 를 더한다.** `decode_adrp(0x90000109, 0x8b0)` 가 0x208b0 이 된다. ADRP 결과의 하위 12비트는 항상 0 이다.
```c
return page_of(P) + ((uint64_t)sext(imm, 21) << 12);     /* P + ... 가 아님 */
```

**2. imm21 부호 확장을 빼먹는다.** 뒤쪽 페이지(imm 최상위 비트 1)가 +4 GB 근처로 해석된다. 무작위 왕복 테스트가 바로 잡는다(뮤턴트 2).

**3. LDR 의 `:lo12:` 에 바이트 오프셋을 그대로 넣는다.** `ldr x10, [x9, #32]` 의 워드에는 32 가 아니라 4 가 들어간다(`f940112a`). 반대로 정렬이 안 맞는 심볼은 거부해야 한다. ld64 처럼 몫만 넣으면 **조용히 다른 주소**를 읽는다.
```c
if (lo % size) return RELOC_ALIGN;         /* 새 ld64 가 하지 않은 검사 */
*insn = (*insn & ~(0xFFFu << 10)) | ((uint32_t)(lo / size) << 10);
```
asm 에서 막는 법: 데이터에 `.p2align 3`(C 면 `_Alignas(8)`)을 주고, 맥에서는 의심되면 `-Wl,-ld_classic` 으로 한 번 링크해 본다(deprecated 경고가 나지만 검사한다, 실측).

**4. GOT 리로케이션에 심볼 주소를 쓴다.** `ADR_GOT_PAGE`/`LD64_GOT_LO12_NC` 의 S 는 **GOT 칸 주소**다. 심볼 주소를 넣으면 LDR 이 변수의 **값**을 주소로 알고 한 번 더 따라간다. 뮤턴트 5가 bfd/lld 비교 4건에서 잡혔다.

**5. 공유 라이브러리용 asm 을 `adrp sym` + `:lo12:` 로 짠다.** 실행 파일에서는 되지만 `.so` 에서는 `recompile with -fPIC` 로 실패한다. 외부에 보이는 심볼이면 `:got:` 를 쓰고, 내부용이면 `.hidden sym` 을 붙인다.

**6. Mach-O 에서 `@PAGE` 를 빼먹거나 dylib 심볼에 `@PAGE` 를 쓴다.** 앞의 것은 어셈블러 에러이고, 뒤의 것은 ld64 의 `does not have address` 다. dylib 심볼은 반드시 `@GOTPAGE/@GOTPAGEOFF` 를 쓴다.

**7. ADR 완화에서 PC 를 ADRP 자리로 잡는다.** lld 는 NOP 을 첫 칸에, ADR 을 **둘째 칸**에 넣는다. 오프셋도 둘째 칸 기준이다(뮤턴트 4).

## 8. 면접 Q&A

<details>
<summary>Q1. ADRP 는 왜 바이트가 아니라 4 KB 페이지 단위인가요? 범위는?</summary>

32비트 명령에 즉시값이 21비트밖에 없다. 바이트 단위면 ±1 MB(그게 ADR)다. 페이지 단위로 세면 같은 21비트가 ±4 GB 가 된다. 버린 하위 12비트는 바로 다음 ADD/LDR/STR 의 imm12 가 채운다. 정확한 범위는 페이지 차이 [−2²⁰, 2²⁰−1], 즉 −4 GB ~ +4 GB−4 KB 다. 실측으로 `0x10000 → 0x10000F000` 은 링크됐고 `0x100010000` 은 `relocation truncated to fit` 이었다. 대가는 명령 2개다. 그래서 가까운 대상이면 lld 가 `NOP + ADR` 로 바꾼다.
</details>

<details>
<summary>Q2. GOT 는 왜 필요한가요? 어떤 참조가 GOT 를 거치나요?</summary>

컴파일·링크 시점에 대상 주소를 모르는데, 그 주소가 로드 시점에 정해지는 경우다(다른 이미지의 심볼, `.so` 안에서 선점 가능한 default-visibility 심볼). 코드는 읽기 전용이고 공유되므로 로더가 고칠 수 없다. 그래서 주소를 담을 데이터 칸(GOT)을 하나 두고 코드는 그 칸을 PC 상대로 읽는다. 로더는 심볼당 8 B 하나만 고치면 된다. 같은 이미지 안이고 선점 불가인 심볼은 링커가 GOT 로드를 ADD 로 바꾼다. ld64 와 lld 는 그렇게 했고, ld.bfd 2.46 은 그대로 뒀다(실측).
</details>

<details>
<summary>Q3. 꼬리질문: 함수 호출은 왜 GOT 가 아니라 stub/PLT 를 거치나요? 지연 바인딩은?</summary>

`bl` 은 PC 상대 26비트(±128 MB)라 주소를 레지스터에서 읽을 수 없다. 그래서 링커가 같은 이미지 안에 `adrp x16; ldr x16,[x16,#off]; br x16` stub(맥)이나 PLT 엔트리(리눅스, `ldr x17` + `br x17`)를 만들어 그쪽으로 `bl` 한다. stub 이 GOT 칸을 읽어 점프한다. 리눅스는 PLT 칸을 처음에 resolver 로 두고 첫 호출 때 채우는 지연 바인딩이 가능하다(`-z now` 면 로드 때 전부 채운다). 맥의 chained fixups 은 로드 때 dyld 가 bind 를 전부 처리한다고 알고 있다(확인 권장). x16/x17 은 이런 링커 생성 코드용 scratch(IP0/IP1)이다.
</details>

<details>
<summary>Q4. "relocation truncated to fit" 을 봤습니다. 어떻게 접근하나요?</summary>

(1) 어느 타입인지 본다. `CALL26/JUMP26` 이면 128 MB 초과다. 보통 링커가 베니어로 해결하지만 섹션 배치나 링커 스크립트가 막았을 수 있다. `CONDBR19/TSTBR14` 는 ld.bfd 가 베니어를 넣지 않았다(실측: 그냥 에러, lld 는 미확인). 함수가 너무 크거나 섹션이 갈라진 것이다. 컴파일러 옵션이나 코드 배치를 고친다. `ADR_PREL_PG_HI21` 이면 데이터가 코드에서 4 GB 넘게 떨어졌다. 메모리 맵을 바꾸거나 `-mcmodel=large`(MOVZ/MOVK 4개, 연습 3)를 쓴다. `LDSTn_ABS_LO12_NC` 면 범위가 아니라 **정렬** 문제다. ld.bfd 가 친절하게 경고까지 붙였다. (2) `-Wl,-Map` 으로 두 심볼의 최종 주소를 확인한다. (3) asm 이면 손으로 쓴 리로케이션 연산자를 의심한다.
</details>

<details>
<summary>Q5. 왜 Cortex-M 펌웨어는 보통 PIC 가 아닌가요? 위치 독립이 꼭 필요하면?</summary>

로더가 없고 실행 주소가 링커 스크립트로 고정되어 있다. 이 상태에서 PIC 는 비용만 든다(GOT 로드, 레지스터 하나 소모). 그래서 `movw/movt` 절대 주소나 리터럴 풀을 쓴다. 꼭 필요하면 `-fropi`(RO 를 PC 상대로) / `-frwpi`(RW 를 r9 static base 상대로)를 쓴다. 실측 출력에서 `R_ARM_THM_MOVW_PREL_NC` + `add r3, pc`, 그리고 `ldr.w r2, [r9, r1]` 이 나왔다. 대안: 슬롯마다 따로 링크한 이미지 2개를 배포, MPU/플래시 리맵·별칭(alias) 주소로 항상 같은 주소에서 실행, 부트로더가 재배치 테이블을 적용.
</details>

<details>
<summary>Q6. ADRP+ADD, 리터럴 풀(LDR literal), MOVZ/MOVK 4개 — 주소 만드는 법을 비교해 주세요.</summary>

| 방식 | 크기 | 범위 | PIC? | 로드 |
|---|---|---|---|---|
| ADRP+ADD | 8 B | ±4 GB | 예 | 없음 |
| ADR | 4 B | ±1 MB | 예 | 없음 |
| LDR literal + 8 B 상수 | 4+8 B | 상수까지 ±1 MB, 값은 아무 데나 | 상수 자체는 절대 주소(로드 때 rebase 필요) | 메모리 1회 |
| MOVZ+MOVK×3 | 16 B | 64비트 전체 | 아니오(`MOVW_UABS_G*`) | 없음 |
| ADRP+LDR(GOT) | 8 B | GOT 칸까지 ±4 GB | 예 | 메모리 1회 |

AArch64 기본 code model(small)이 ADRP 를 고른 이유: 로드가 없고 PIC 이고 8 B 다.
</details>

<details>
<summary>Q7. (스태프) 새 ld64 가 정렬 안 맞는 PAGEOFF12 를 조용히 링크했습니다. 이 설계가 틀리는 경우와, 팀 빌드에서 어떻게 막겠습니까?</summary>

틀리는 경우: 손으로 쓴 asm 이나 `__attribute__((packed))` 구조체 멤버, 링커 스크립트로 정렬을 깎은 섹션처럼 "선언한 정렬 < 접근 크기" 인 곳이다. 컴파일러가 만든 코드는 정렬을 알고 맞추므로 거의 안 생긴다. 그래서 asm/부트 코드에서만 터진다. 증상은 크래시도 아니고 **옆 값을 읽는 것**이다. 막는 법: (1) CI 에 리로케이션 검사기를 둔다. 우리의 `apply_ldst_lo12` 처럼 `.o` 의 PAGEOFF12 + 최종 심볼 주소로 정렬을 재계산한다(`check_reloc.sh` 의 구조). (2) 같은 `.o` 를 ELF 툴체인(lld)으로도 링크해 에러를 받는 교차 검증. (3) asm 데이터에 `.p2align`, C 에 `_Alignas`, 그리고 `_Static_assert(_Alignof(...))`. 단 "모든 ld64 버전에서 조용하다" 는 확인하지 않았다. 이번 버전(ld-1266.8) 한 번의 관찰이고, `-ld_classic` 은 에러를 냈다.
</details>

<details>
<summary>Q8. (스태프) A/B 두 슬롯에 같은 앱 바이너리를 올려야 하는 부트로더를 설계한다면, 리로케이션 관점에서 무엇을 고르겠습니까?</summary>

선택지와 대가:
1. 슬롯마다 링크한 이미지 2개를 만든다. 가장 단순하고 런타임 비용이 0 이다. 대신 OTA 패키지가 2배이거나 서버가 슬롯을 알아야 한다. 서명도 2개다.
2. 하드웨어 리맵을 쓴다. 플래시 뱅크 스왑이나 별칭(alias) 주소로 항상 같은 VMA 에서 실행한다. 칩이 지원하면 최선이다(예: 일부 STM32 의 뱅크 스왑, 확인 권장).
3. PIC + 부트 때 자기 재배치를 한다. `-fPIE` 로 만들고 `.rela.dyn` 의 RELATIVE 만 남게 한 뒤, 부트로더(또는 앱의 startup)가 `*(base+off) = base + addend` 를 적용한다. 데이터 쪽 포인터만 고치면 되고 코드는 이미 PC 상대다. 이 레슨의 `link_text` 와 같은 원리다. 비용은 RAM 에 복사된 `.data`/GOT 의 rebase 시간과 GOT 로드 오버헤드다.
4. Cortex-M 이면 ROPI/RWPI 를 쓴다. r9 = 슬롯별 RW 베이스이고, 툴체인 지원을 확인해야 한다.
서명 검증 관점: 3번은 서명한 바이트가 실행 중 바이트와 달라진다(재배치 후). 그래서 "로드 전 검증 → 재배치" 순서와, 재배치 대상이 RAM 뿐임을 보장해야 한다. 나는 칩이 리맵을 지원하면 2, 아니면 Cortex-A/R 은 3, Cortex-M 은 1 을 기본으로 고르겠다.
</details>

<details>
<summary>Q9. lld 가 adrp+add 를 nop+adr 로 바꾸면 크기가 그대로인데 무엇을 얻나요? 왜 nop 을 지우지 않죠?</summary>

GOT→ADD 단계는 **메모리 로드 하나**를 없앤다(캐시 미스 가능성 제거). ADD→ADR 단계는 의존 사슬을 2단에서 1단으로 줄인다. 코어에 따라 ADRP+ADD 를 이미 매크로 융합(fusion)하면 이득이 거의 없을 수 있다(코어 의존, 확인 권장). NOP 을 지우면 뒤 코드가 모두 4 B 씩 당겨진다. 그러면 이미 계산한 모든 PC 상대 오프셋, 점프 테이블, 디버그 정보가 틀어진다. 링커는 섹션 크기를 바꾸지 않는 완화만 한다(RISC-V 의 크기 변경 완화와 대조).
</details>

## 9. 추가 연습

모든 정답 워드는 `demo_practice.sh`(VM, ld.bfd) / `demo_elf.sh` / `demo_mac.sh` 의 실제 출력에서 왔다.

**연습 1 — ADR (`R_AARCH64_ADR_PREL_LO21`).** `apply_adr/decode_adr` 를 짜라. ADRP 와 같은 immlo/immhi 배치지만 단위가 **바이트**, 범위 ±1 MB, Page() 없음. ld.bfd `adr x4, 18004` @0x10010 = `0x1003ffa4`, lld 완화 결과 `0x10101a89` 와 맞춘다.

<details>
<summary>답</summary>

`pcrel_field(S−P, 21 bits, scale 1)` 로 범위를 검사한다(+1 MB 정확히는 밖, −1 MB 정확히는 안). `insn = (insn & 0x9F00001F) | (f&3)<<29 | (f>>2)<<5`. 디코드는 `P + sext(immhi:immlo, 21)`.
</details>

**연습 2 — 조건 분기 (`CONDBR19`, `TSTBR14`).** `b.ne`·`cbz`(imm19, ±1 MB)와 `tbz/tbnz`(imm14, ±32 KB) 적용기를 짜라. 실측 경계: `b.ne` +0xffffc OK / +0x100000 에러, `tbz` +0x7ffc OK / +0x8000 에러, `tbnz` −0x8000 OK / −0x8004 에러. ld.bfd 는 셋 다 `relocation truncated to fit` 이었고 베니어를 넣지 않았다.

<details>
<summary>답</summary>

필드 위치만 다르다. imm19 = `[23:5]`, imm14 = `[18:5]`, 단위 4 B. 명령 판별: B.cond `(w & 0xFF000010) == 0x54000000`, CBZ/CBNZ `(w & 0x7E000000) == 0x34000000`, TBZ/TBNZ `(w & 0x7E000000) == 0x36000000`. objdump 가 `tbz x1, #3` 를 `tbz w1, #3` 으로 찍는 것도 실측이다. 비트 번호 < 32 면 b5=0 이라 w 표기가 된다.
</details>

**연습 3 — 절대 주소 MOVZ/MOVK ×4 (`MOVW_UABS_G3`, `G2_NC`, `G1_NC`, `G0_NC`).** `far_data = 0x123456789ab0` 일 때 ld.bfd 결과 `d2e00002 f2c24682 f2aacf02 f2935602` 를 재현하라. 명령 안의 `hw` 필드 순서(3,2,1,0)가 맞지 않으면 거부한다.

<details>
<summary>답</summary>

i 번째 명령의 imm16 `[20:5]` = `(S >> 16*hw) & 0xFFFF`. non-PIC(로드 주소가 바뀌면 틀림)이지만 64비트 전 범위를 쓸 수 있다. `-mcmodel=large` 나 아주 먼 MMIO 주소에 쓴다. ADRP 와 달리 로더가 고칠 칸이 코드 안에 있다는 점에 주의한다(text relocation).
</details>

**연습 4 — 베니어 생성기.** `bl` @0x10008 → `far_func` 0x8010008(정확히 128 MB, 밖)에서 ld.bfd 가 만든 `bl 10018` + `adrp x16 / add x16,#0x8 / br x16` (`94000004`, `90040010 91002210 d61f0200`)을 재현하라.

<details>
<summary>답</summary>

베니어 주소 V 에서 ADRP x16 → target 페이지, ADD x16 → lo12, `br x16`. BL 은 V 로 다시 적용한다. ADRP 자체도 ±4 GB 한계가 있다. 그보다 멀면 `ldr x16, literal; br x16` 형(절대 주소 상수)이 필요하다. lld 의 `AbsLongThunk` 이름이 그 형태에서 왔다(이번 실측은 짧은 B 형이었음).
</details>

**연습 5 — chained fixup 해독.** `DYLD_CHAINED_PTR_64_OFFSET` 포인터를 해독하고 페이지 안 체인을 걸어라. `__got` 원본 `8010000000000000, 8010000000000001, 8000000000000002` 와 `__data` 의 `start[0] = 0x0008` 부터 `0010000000008000 (rebase → 0x8000), 8000000000000000 (bind 0)` 이 `dyld_info -fixup_chain_details` 와 같아야 한다.

<details>
<summary>답</summary>

bind: bit63=1, ordinal `[23:0]`, addend `[31:24]`, next `[62:51]`. rebase: target `[35:0]`, high8 `[43:36]`(포인터 최상위 바이트로 복원), next 같음. 다음 픽스업 = 현재 + next×4, next=0 이면 끝. rebase target 은 mach header 기준 오프셋이라 `0x100000000 + 0x8000 = 0x100008000` = `_local_bias` (dyld_info `rebase 0x100008000` 과 일치).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c shared_pic.S -o /tmp/ARM-A09_practice && /tmp/ARM-A09_practice
# 실제 출력: 29/29 passed  (ASan/UBSan 빌드도 29/29)
limactl shell elinux -- bash -lc 'bash <레슨 경로>/demo_practice.sh'   # 정답 워드 재생성 (VM)
```

## 10. 핵심 정리

- 주소 = `ADRP`(Page(S+A) − Page(P), 21비트 페이지, ±4 GB) + `ADD`/`LDR` 의 imm12. LDR/STR 의 imm12 는 **크기로 나눈 값**이다.
- 리로케이션 = (어느 칸, 무슨 식, 무슨 범위). ELF `ADR_PREL_PG_HI21 / ADD_ABS_LO12_NC / LDSTn_ABS_LO12_NC / CALL26` ↔ Mach-O `PAGE21 / PAGEOFF12 / BRANCH26` + 별도 `ADDEND`.
- GOT = 로드 때 정해지는 주소를 담는 데이터 칸. 코드는 읽기 전용으로 남는다. 함수는 stub/PLT 가 GOT 를 읽어 `br x16/x17`.
- 같은 이미지 + 선점 불가면 링커가 GOT 를 없앤다(ld64·lld: LDR→ADD, lld: →NOP+ADR). ld.bfd 2.46 은 그대로 둔다.
- 범위: B/BL ±128 MB(베니어 자동), B.cond/CBZ ±1 MB, TBZ ±32 KB(자동 해결 없음), ADR ±1 MB, ADRP ±4 GB.
- `recompile with -fPIC` = 선점 가능한 심볼을 PC 상대로 고정했다는 뜻이다. `.hidden` 이나 `:got:` 로 고친다.
- 새 ld64(ld-1266.8)는 정렬 안 맞는 `@PAGEOFF` LDR 을 **조용히** 틀리게 링크했다. 직접 검사하거나 교차 링크한다.
- Cortex-M = 보통 non-PIC(`movw/movt`). 슬롯 독립이 필요하면 ROPI(PC 상대)/RWPI(r9 base), 슬롯별 링크, 하드웨어 리맵 중에서 고른다.

## 참고자료

- Arm, *ELF for the Arm 64-bit Architecture (AArch64)* (AAELF64) — 리로케이션 표, `S+A−P` 식, `_NC` 의미: https://github.com/ARM-software/abi-aa/blob/main/aaelf64/aaelf64.rst
- Arm, *Arm Architecture Reference Manual for A-profile* (DDI 0487), C6 ADRP/ADR/ADD/LDR/B 인코딩 (문서 확인 권장)
- Arm, *Procedure Call Standard for the Arm 64-bit Architecture* (AAPCS64) — IP0/IP1 (x16/x17): https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst
- LLVM lld 21.1.8 `lld/ELF/Arch/AArch64.cpp` — `AArch64Relaxer::tryRelaxAdrpAdd/tryRelaxAdrpLdr`: https://github.com/llvm/llvm-project/blob/llvmorg-21.1.8/lld/ELF/Arch/AArch64.cpp
- Apple SDK 헤더 `MacOSX26.4.sdk/usr/include/mach-o/fixup-chains.h` — `dyld_chained_ptr_64_rebase` (162행: target 36 / high8 8 / reserved 7 / next 12 / bind 1), `dyld_chained_ptr_64_bind` (219행: ordinal 24 / addend 8 / reserved 19 / next 12 / bind 1), `DYLD_CHAINED_PTR_64_OFFSET = 6` (102행). `mach-o/arm64/reloc.h` (ARM64_RELOC_*)
- Apple, *Writing ARM64 code for Apple platforms*: https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms
- Arm Compiler 문서 — `-fropi` / `-frwpi` (RWPI 의 r9 static base, 문서 확인 권장)
- 관련 레슨: TOOL-B04 (nm/objdump/otool 로 리로케이션 읽기), TOOL-B05 (링크 에러), ARM-B03 (MOVZ/MOVK), ARM-M14 (크래시 덤프), BM-A01 (VTOR/코드 재배치)
