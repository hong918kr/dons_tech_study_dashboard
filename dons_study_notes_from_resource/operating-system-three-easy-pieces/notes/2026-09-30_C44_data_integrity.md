# Ch.44 데이터 무결성과 보호 — 체크섬으로 조용한 손상 잡기

> 📖 원문: [44. Data Integrity and Protection](../book-md/C44_data_integrity_and_protection.md) · [PDF p.543](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=543) · ⏱️ 읽기 약 40분 (C 코드 + checksum.py 포함 시 +30분) · 🔗 선행: [Ch.38 RAID](2026-09-30_C38_raid.md), [Ch.42 저널링](2026-09-30_C42_crash_consistency_journaling.md)

## 0. 한눈에 보기

> **CRUX — "How should systems ensure that the data written to storage is protected? What techniques are required? How can such techniques be made efficient, with both low space and time overheads?"**
> (저장 장치에 쓴 데이터가 보호되도록 시스템은 무엇을 해야 하나? 어떤 기법이 필요하고, 공간·시간 오버헤드를 낮게 유지하며 어떻게 효율적으로 만들까?)

- RAID의 **fail-stop** 모델(디스크가 통째로 살거나 죽거나)은 현실의 절반이다. 현대 디스크는 **fail-partial**: 대체로 잘 동작하면서 **일부 블록만** 문제를 일으킨다.
- 두 종류: **LSE(latent sector error)** — 읽으면 **에러를 돌려준다**(탐지 쉬움, 중복으로 복구). **corruption** — 틀린 데이터를 **조용히** 돌려준다(탐지가 핵심 문제).
- 탐지 도구 = **체크섬**: XOR, 덧셈, **Fletcher**, **CRC**. 강도와 속도의 트레이드오프, 충돌은 피할 수 없다.
- 체크섬만으로 못 잡는 것: **misdirected write**(엉뚱한 주소에 씀) → 체크섬에 **물리 ID(디스크, 블록 번호)** 추가. **lost write**(완료 보고했는데 안 씀) → **부모(inode/간접 블록)에 자식 체크섬** 저장(ZFS) 또는 read-after-write.
- 아무도 안 읽는 데이터는 썩어도 모른다 → **scrubbing**(주기적 전체 검사). 오버헤드는 공간(~0.19%)보다 **CPU 시간**이 크다 → 복사와 체크섬 계산 합치기.

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| fail-stop | 디스크 전체가 동작 or 완전 고장, 탐지 쉬움 | 초기 RAID의 가정 |
| fail-partial | 대체로 동작하나 일부 블록이 접근 불가/틀린 값 | Prabhakaran IRON FS |
| LSE (latent sector error) | 섹터가 손상되어 읽기 시 에러 반환 | head crash, cosmic ray, ECC로 못 고침 |
| block corruption | 틀린 내용을 에러 없이 반환 | 버그 FW, 불량 버스 |
| silent fault | 장치가 아무 표시 없이 틀린 데이터 반환 | 가장 위험 |
| in-disk ECC | 디스크가 섹터마다 저장하는 오류 정정 코드 | 고칠 수 있으면 고치고 아니면 에러 |
| checksum | 큰 데이터의 작은 요약(4~8B) | 저장 시 계산, 읽을 때 비교 |
| collision | 다른 데이터, 같은 체크섬 | 4KB → 4B 이니 필연 |
| XOR checksum | 워드 단위 XOR | 같은 열의 비트 2개 변경 못 잡음 |
| Fletcher checksum | s1 += d mod 255, s2 += s1 mod 255 | 순서 변화 탐지, CRC에 가까운 강도 |
| CRC | 데이터를 큰 이진수로 보고 다항식으로 나눈 나머지 | 네트워크·스토리지 표준 |
| 520B sector | 512B 데이터 + 8B 체크섬/메타 | 엔터프라이즈 디스크, T10 PI |
| misdirected write | 데이터는 맞게, 주소는 틀리게 씀 | 물리 ID로 탐지 |
| lost write | 완료를 보고했지만 실제로 안 씀 | 부모 체크섬(ZFS)으로 탐지 |
| scrubbing | 주기적으로 모든 블록을 읽어 검사 | 야간/주간 스캔 |

## 2. 디스크 고장 모드 (44.1)

Bairavasundaram 등의 연구(3년, 디스크 150만 대 이상)에서 **최소 한 번** 문제를 보인 드라이브 비율(원문 그림 44.1):

| | Cheap (주로 SATA) | Costly (SCSI/FC) |
|---|---|---|
| LSE | 9.40% | 1.40% |
| Corruption | 0.50% | 0.05% |

비싼 드라이브가 약 **10배** 덜 겪지만, 0이 아니므로 시스템은 반드시 대비해야 한다.

### 2.1 LSE

- 원인: 헤드가 표면에 닿는 **head crash**(정상 동작에선 일어나면 안 되는), 우주선(cosmic ray)의 비트 뒤집힘 등.
- 디스크는 **in-disk ECC** 로 비트가 괜찮은지 확인하고, 고칠 수 있으면 고친다. 정보가 부족해 못 고치면 **읽기 요청에 에러를 반환**한다.
- 추가 발견: 비싼 드라이브도 LSE가 하나 생기면 싼 드라이브만큼 추가 에러가 잘 생긴다 / 대부분 2년차에 연간 에러율 증가 / 디스크가 클수록 LSE 증가 / LSE가 있는 디스크 대부분은 50개 미만 / LSE는 더 많은 LSE를 부른다 / **공간·시간 지역성**이 강하다 / **scrubbing이 유용**(대부분의 LSE가 scrub으로 발견됨).

### 2.2 Corruption

- 디스크 자신도 모르는 손상. 예: **버그 있는 FW가 블록을 엉뚱한 위치에** 씀 → 디스크 ECC는 그 섹터 내용이 "멀쩡하다"고 하지만, 클라이언트 입장에선 틀린 블록이 돌아온다. 또는 호스트→디스크 **불량 버스**에서 손상된 데이터를 디스크가 그대로 저장.
- 장치가 아무 표시도 안 하므로 **silent fault**.
- 발견: 같은 등급 안에서도 모델별 차이가 크다 / 노화 효과가 모델마다 다르다 / 워크로드·디스크 크기 영향은 작다 / 손상 디스크 대부분은 소수만 / **한 디스크 안에서도, RAID의 디스크들 사이에서도 독립이 아니다** / 공간 지역성, 약간의 시간 지역성 / LSE와 약한 상관.

이 새로운 관점이 **fail-partial** 모델이다: 통째로 죽을 수도 있고(fail-stop), 멀쩡해 보이면서 **일부 블록이 접근 불가(LSE, non-silent)** 거나 **틀린 내용(corruption, silent)** 일 수도 있다.

```svg
<svg viewBox="0 0 700 290" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C44-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <g stroke="currentColor" fill="none">
    <rect x="250" y="10" width="200" height="34" rx="6"/>
    <rect x="40" y="80" width="180" height="40" rx="6"/>
    <rect x="260" y="80" width="180" height="40" rx="6" style="fill:var(--accent-soft)"/>
    <rect x="480" y="80" width="200" height="40" rx="6" style="stroke:var(--accent);fill:var(--accent-soft)" stroke-width="2"/>
    <rect x="420" y="160" width="120" height="34" rx="6"/>
    <rect x="560" y="160" width="120" height="34" rx="6"/>
    <rect x="40" y="230" width="180" height="44" rx="6" stroke-dasharray="4 3"/>
    <rect x="260" y="230" width="180" height="44" rx="6" stroke-dasharray="4 3"/>
    <rect x="460" y="230" width="220" height="44" rx="6" stroke-dasharray="4 3"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="350" y="32" font-weight="bold">디스크 고장 모드</text>
    <text x="130" y="98">fail-stop</text><text x="130" y="113" font-size="11">디스크 전체 고장</text>
    <text x="350" y="98">LSE (non-silent)</text><text x="350" y="113" font-size="11">읽으면 에러 반환</text>
    <text x="580" y="98" font-weight="bold">corruption (silent)</text><text x="580" y="113" font-size="11">틀린 데이터를 조용히 반환</text>
    <text x="480" y="182" font-size="12">misdirected write</text>
    <text x="620" y="182" font-size="12">lost write</text>
    <text x="130" y="249" font-size="12">탐지: 자명</text><text x="130" y="265" font-size="11">복구: RAID 재구성</text>
    <text x="350" y="249" font-size="12">탐지: 디스크가 알려줌</text><text x="350" y="265" font-size="11">복구: 미러/패리티 (RAID-DP)</text>
    <text x="570" y="249" font-size="12">탐지: 체크섬 + 물리ID + 부모 체크섬</text><text x="570" y="265" font-size="11">복구: 중복 사본이 있어야</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.3" marker-end="url(#C44-arrow)">
    <path d="M300,46 L140,78"/><path d="M350,46 L350,78"/><path d="M400,46 L570,78"/>
    <path d="M560,122 L490,158"/><path d="M600,122 L620,158"/>
    <path d="M130,122 L130,228"/><path d="M350,122 L350,228"/>
  </g>
  <text x="440" y="215" fill="currentColor" font-size="11">fail-partial = LSE + corruption</text>
</svg>
```

## 3. LSE 다루기 (44.2)

> **CRUX — "How should a storage system handle latent sector errors? How much extra machinery is needed to handle this form of partial failure?"**

LSE는 **정의상 탐지가 쉽다**(디스크가 에러를 준다). 그러니 가진 **중복성**을 쓰면 된다: 미러 RAID면 다른 사본을 읽고, RAID-4/5면 패리티 그룹의 다른 블록들로 재구성.

까다로운 경우: RAID-4/5에서 **디스크 하나가 통째로 죽어 재구성하는 도중**, 다른 디스크에서 LSE를 만나면 그 스트라이프는 **재구성 불가**. 그래서 **NetApp RAID-DP** 처럼 패리티를 2개(이중 패리티) 둔다. 비용은 스트라이프마다 패리티 2개 유지(WAFL의 로그 구조가 쓰기 비용을 많이 완화) + 디스크 한 대 분량의 공간. 디스크가 커질수록 재구성 시간이 길어지고 "재구성 중 LSE" 확률이 커져서 RAID-6이 사실상 표준이 됐다.

## 4. 손상 탐지: 체크섬 (44.3)

> **CRUX — "Given the silent nature of such failures, what can a storage system do to detect when corruption arises? What techniques are needed? How can one implement them efficiently?"**

손상이 **탐지만** 되면 복구는 LSE와 같다(다른 사본 사용). 그래서 핵심은 탐지. 주 무기가 **체크섬**: 데이터 덩어리(예: 4KB)를 받아 작은 요약(4~8B)을 만드는 함수. 데이터와 함께 저장하고, 나중에 접근할 때 다시 계산해 비교.

> **TIP — 공짜 점심은 없다 (TNSTAAFL).** 강한 체크섬일수록 계산이 비싸다. 보호 수준과 비용 사이의 고전적 트레이드오프.

### 4.1 체크섬 함수들

**XOR**: 블록을 워드 단위로 XOR. 원문 예제 — 16바이트를 4바이트씩 한 줄에 놓고 **열마다 XOR**:

```text
365e c4cd  →  0011 0110 0101 1110  1100 0100 1100 1101
ba14 8a92  →  1011 1010 0001 0100  1000 1010 1001 0010
ecef 2c3a  →  1110 1100 1110 1111  0010 1100 0011 1010
40be f666  →  0100 0000 1011 1110  1111 0110 0110 0110
XOR        →  0010 0000 0001 1011  1001 0100 0000 0011  = 0x201b9403
```

한계: **같은 열(비트 위치)의 비트 두 개가 바뀌면** XOR 결과가 그대로 → 못 잡는다.

**덧셈(addition)**: 워드 단위 2의 보수 덧셈, overflow 무시. 빠르다. 많은 변화를 잡지만 **데이터가 shift(순서 변경)** 되면 못 잡는다(덧셈은 교환법칙).

**Fletcher**: 바이트 d1…dn 에 대해 `s1 = (s1 + di) mod 255`, `s2 = (s2 + s1) mod 255`. s2가 **위치 가중합**이라 순서 변화를 잡는다. 모든 1비트·2비트 오류와 많은 burst 오류를 잡아 **CRC에 거의 맞먹는다**.

**CRC (cyclic redundancy check)**: 블록 D를 하나의 큰 이진수로 보고 약속된 값(생성 다항식) k로 나눈 **나머지**. 이 이진 나눗셈은 시프트·XOR(또는 테이블)로 효율적으로 구현할 수 있어 네트워크에서도 표준. 생성 다항식 차수 이하의 **burst 오류는 전부** 잡는다.

어떤 함수든 **충돌**은 피할 수 없다: 4KB를 4B로 요약하면 같은 요약을 갖는 다른 블록이 반드시 있다. 좋은 체크섬 = 충돌 가능성을 최소화하면서 계산이 쉬운 것.

### 4.2 직접 계산해 보기 (checksum.py 숫자로)

데이터 바이트 `1, 2, 3, 4`:

```text
Add  = 1 + 2 + 3 + 4                = 10   (mod 256)
Xor  = 1 ^ 2 ^ 3 ^ 4                = 4    (0001^0010=0011, ^0011=0000, ^0100=0100)
Fletcher: s1 = 1, 3, 6, 10 → a = 10
          s2 = 1, 4, 10, 20 → b = 20
```

순서를 바꾼 `2, 1, 3, 4`: Add = 10, Xor = 4로 **그대로**. Fletcher는 s1 = 2, 3, 6, 10 / s2 = 2, 5, 11, 21 → **(10, 21)** 로 달라진다 → 순서 변화를 Fletcher만 잡는다.

**새 예제 — Fletcher의 약점:** mod 255 에서 **0x00 과 0xFF 는 같은 값(0 ≡ 255)** 이다. `255, 0, 0, 0` 과 `0, 0, 0, 0` 은 Fletcher가 둘 다 (0, 0). 아래 C 실험에서 "0x00 → 0xFF" 손상을 Fletcher16이 **2만 번 전부** 놓친다.

### 4.3 체크섬 배치 (layout)

- 가장 단순: 블록(섹터)마다 체크섬 하나. 디스크가 512B 단위로만 쓸 수 있으니, 제조사는 **520B 섹터**로 포맷해 여분 8B에 체크섬을 둔다.
- 그런 기능이 없는 디스크: FS가 **체크섬 n개를 한 섹터에 모으고** 그 뒤에 데이터 블록 n개를 둔다. 모든 디스크에서 동작하지만, D1 하나 덮어쓰기 = 체크섬 섹터 **읽기 1 + 쓰기 2**(체크섬 섹터, D1). 520B 방식은 쓰기 1번.

```svg
<svg viewBox="0 0 700 260" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <text x="20" y="22" fill="currentColor" font-weight="bold">(a) 520B 섹터: 블록마다 체크섬(+ 물리 ID)</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="32" width="36" height="40" style="fill:var(--accent-soft)"/><rect x="56" y="32" width="110" height="40"/>
    <rect x="176" y="32" width="36" height="40" style="fill:var(--accent-soft)"/><rect x="212" y="32" width="110" height="40"/>
    <rect x="332" y="32" width="36" height="40" style="fill:var(--accent-soft)"/><rect x="368" y="32" width="110" height="40"/>
    <rect x="488" y="32" width="36" height="40" style="fill:var(--accent-soft)"/><rect x="524" y="32" width="110" height="40"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="11">
    <text x="38" y="50">C</text><text x="38" y="64" font-size="9">d0 b0</text><text x="111" y="56">D0</text>
    <text x="194" y="50">C</text><text x="194" y="64" font-size="9">d0 b1</text><text x="267" y="56">D1</text>
    <text x="350" y="50">C</text><text x="350" y="64" font-size="9">d0 b2</text><text x="423" y="56">D2</text>
    <text x="506" y="50">C</text><text x="506" y="64" font-size="9">d0 b3</text><text x="579" y="56">D3</text>
  </g>
  <text x="20" y="92" fill="currentColor" font-size="11">D1 덮어쓰기 = 쓰기 1번 (체크섬이 같은 섹터 안). C = checksum, d = disk 번호, b = block 번호</text>
  <text x="20" y="130" fill="currentColor" font-weight="bold">(b) 512B 디스크: 체크섬 섹터 + 데이터 n개</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="140" width="60" height="70" style="stroke:var(--accent);fill:var(--accent-soft)" stroke-width="2"/>
    <rect x="80" y="140" width="110" height="70"/><rect x="190" y="140" width="110" height="70" style="stroke:var(--accent)" stroke-width="2"/>
    <rect x="300" y="140" width="110" height="70"/><rect x="410" y="140" width="110" height="70"/><rect x="520" y="140" width="110" height="70"/>
  </g>
  <g fill="currentColor" text-anchor="middle" font-size="11">
    <text x="50" y="158">C[D0]</text><text x="50" y="172">C[D1]</text><text x="50" y="186">C[D2]</text><text x="50" y="200">C[D3..]</text>
    <text x="135" y="180">D0</text><text x="245" y="180">D1</text><text x="355" y="180">D2</text><text x="465" y="180">D3</text><text x="575" y="180">D4</text>
  </g>
  <g fill="currentColor" font-size="11">
    <text x="20" y="232">D1 덮어쓰기 = 체크섬 섹터 읽기 + C[D1] 갱신 + 체크섬 섹터 쓰기 + D1 쓰기 = 1 read + 2 writes</text>
    <text x="20" y="250">공간 오버헤드: 4KB 블록당 8B = 0.19% · 체크섬과 데이터가 같은 쓰기에 원자적으로 묶이지 않는 점도 주의</text>
  </g>
</svg>
```

## 5. 체크섬 사용하기 (44.4)

읽기 시: 블록 D와 **저장된 체크섬 Cs(D)** 를 함께 읽고, 받은 D로 **계산한 체크섬 Cc(D)** 를 만든다.

- Cs(D) == Cc(D): 아마 손상 없음 → 사용자에게 반환.
- Cs(D) != Cc(D): 저장 이후 데이터가 바뀌었다 → **손상 탐지**.

탐지 후: 중복 사본이 있으면 그것을 쓰고, 없으면 **에러 반환**. 탐지는 만능이 아니다 — 멀쩡한 사본이 없으면 운이 없는 것.

## 6. 새 문제: misdirected write (44.5)

> **CRUX — "How should a storage system or disk controller detect misdirected writes? What additional features are required from the checksum?"**

디스크/RAID 컨트롤러가 데이터는 올바르게, **위치는 틀리게** 쓰는 고장. 단일 디스크: Dx를 주소 x가 아니라 y에 씀(Dy를 "손상"시킴). 다중 디스크: 디스크 i의 블록을 디스크 j에 씀.

해법은 간단: 체크섬 옆에 **물리 식별자(physical ID)** — 디스크 번호와 섹터(블록) 번호 — 를 같이 저장. 디스크 10의 블록 4를 읽었는데 저장된 ID가 (10, 4)가 아니면 misdirected write다. 디스크에 중복 정보가 꽤 늘지만, **중복이 곧 오류 탐지(와 복구)의 열쇠**다.

## 7. 마지막 문제: lost write (44.6)

> **CRUX — "How should a storage system or disk controller detect lost writes? What additional features are required from the checksum?"**

장치가 "쓰기 완료"라고 알렸지만 **실제로는 영속화되지 않아** 옛 내용이 남아 있는 고장. 지금까지의 방법은 못 잡는다: 옛 블록의 체크섬은 옛 내용과 **맞고**, 물리 ID도 **맞다**.

- **write verify / read-after-write**: 쓰고 바로 다시 읽어 확인. 확실하지만 쓰기 I/O가 **2배**.
- **다른 곳에 체크섬**: ZFS는 각 **inode와 간접 블록에 자식 블록들의 체크섬**을 담는다. 데이터 블록 쓰기가 사라져도 inode의 체크섬이 옛 데이터와 안 맞는다. inode 쓰기와 데이터 쓰기가 **동시에** 사라질 때만 실패(드물지만 가능).

## 8. scrubbing (44.7)

체크섬은 언제 검사되나? 응용이 읽을 때. 하지만 대부분의 데이터는 거의 안 읽힌다 → 검사 안 된 채 **bit rot** 이 쌓여 결국 **모든 사본**이 망가질 수 있다. 그래서 **disk scrubbing**: 주기적으로(야간·주간) 모든 블록을 읽어 체크섬을 검사하고, 깨진 사본은 멀쩡한 사본으로 고친다. 2.1절의 "대부분의 LSE가 scrub으로 발견됨"이 그 효과.

## 9. 체크섬의 오버헤드 (44.8)

- **공간**: (1) 디스크 — 4KB당 8B = **0.19%**. (2) 메모리 — 검사 후 버리면 잠깐뿐. 메모리 손상 대비로 체크섬을 메모리에 계속 들고 있을 때만 눈에 띈다.
- **시간**: 더 눈에 띈다. 저장할 때 한 번, 읽을 때 한 번 CPU가 블록 전체를 훑는다. 줄이는 법: **복사와 체크섬 계산을 한 번의 순회로 합치기**(어차피 page cache → 사용자 버퍼 복사가 필요하니까). 네트워크 스택도 같은 기법.
- **추가 I/O**: 체크섬을 데이터와 따로 저장하면 추가 접근(설계로 줄임), scrubbing의 I/O(스케줄로 조절 — 모두가 잠든 한밤중에).

## 10. 요약 (44.9)

체크섬 구현과 사용이 데이터 보호의 중심이다. 체크섬 종류마다 막는 고장이 다르고, 저장 장치가 진화하면 새 고장 모드가 생길 것이다. 연구와 산업이 기본 접근을 다시 보게 될지 — "Time will tell. Or it won't."

## 11. 직접 해보기

### 11.1 체크섬 4종 구현 · 손상 주입 · 속도 · misdirected/lost write (C)

```c
/*
 * C44_checksums.c — XOR / ADD / Fletcher / CRC32 를 직접 구현하고 비교
 *   1) 원문의 16바이트 XOR 예제 재현 (답: 0x201b9403)
 *   2) 4KB 블록에 여러 종류의 손상을 주입 → 각 체크섬이 "못 잡은" 횟수
 *   3) 속도 (MB/s, -O0 빌드라 절대값보다 상대 비교만)
 *   4) misdirected write / lost write: 블록 체크섬만으로는 왜 부족한가
 *
 * build: cc -Wall -Wextra -O0 code/C44_checksums.c -o .work/bin/C44_checksums
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------------- 체크섬 함수들 ---------------- */
static uint32_t ck_xor(const uint8_t *p, size_t n) { /* 4바이트 단위 XOR (빅엔디언으로 묶음) */
    uint32_t c = 0;
    for (size_t i = 0; i + 4 <= n; i += 4)
        c ^= (uint32_t)p[i] << 24 | (uint32_t)p[i + 1] << 16 | (uint32_t)p[i + 2] << 8 | p[i + 3];
    return c;
}
static uint32_t ck_add(const uint8_t *p, size_t n) { /* 4바이트 단위 2의 보수 덧셈, overflow 무시 */
    uint32_t c = 0;
    for (size_t i = 0; i + 4 <= n; i += 4)
        c += (uint32_t)p[i] << 24 | (uint32_t)p[i + 1] << 16 | (uint32_t)p[i + 2] << 8 | p[i + 3];
    return c;
}
static uint32_t ck_fletcher16(const uint8_t *p, size_t n) { /* 원문 정의: s1+=d mod 255, s2+=s1 mod 255 */
    uint32_t s1 = 0, s2 = 0;
    for (size_t i = 0; i < n; i++) { s1 = (s1 + p[i]) % 255; s2 = (s2 + s1) % 255; }
    return s2 << 8 | s1;
}
static uint32_t crc_table[256];
static void crc_init(void) { /* CRC-32 (IEEE 802.3, reflected poly 0xEDB88320) */
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
}
static uint32_t ck_crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

typedef uint32_t (*ckfn)(const uint8_t *, size_t);
static const char *names[] = { "XOR32", "ADD32", "Fletcher16", "CRC32" };
static ckfn fns[] = { ck_xor, ck_add, ck_fletcher16, ck_crc32 };

/* ---------------- 손상 주입 ---------------- */
#define BLK 4096
static uint32_t seed = 42;
static uint32_t rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }

static void fault(uint8_t *b, int kind) {
    switch (kind) {
    case 0: { /* 1비트 뒤집기 */
        uint32_t bit = rnd() % (BLK * 8);
        b[bit / 8] ^= (uint8_t)(1u << (bit % 8));
        break;
    }
    case 1: { /* 서로 다른 두 4B 워드의 "같은 위치" 비트 2개 뒤집기 */
        uint32_t w1 = rnd() % (BLK / 4), w2;
        do w2 = rnd() % (BLK / 4); while (w2 == w1);
        uint32_t bit = rnd() % 32;
        b[w1 * 4 + bit / 8] ^= (uint8_t)(1u << (bit % 8));
        b[w2 * 4 + bit / 8] ^= (uint8_t)(1u << (bit % 8));
        break;
    }
    case 2: { /* 서로 다른 두 4B 워드 자리 바꾸기 (순서 뒤바뀜) */
        uint32_t w1 = rnd() % (BLK / 4), w2, t;
        do { w2 = rnd() % (BLK / 4); } while (w2 == w1 || memcmp(b + w1 * 4, b + w2 * 4, 4) == 0);
        memcpy(&t, b + w1 * 4, 4); memcpy(b + w1 * 4, b + w2 * 4, 4); memcpy(b + w2 * 4, &t, 4);
        break;
    }
    case 3: { /* 0x00 바이트를 0xFF 로 (먼저 0x00 인 바이트를 하나 만든다) */
        uint32_t i = rnd() % BLK;
        b[i] = 0x00;
        break; /* 원본 쪽에서 0 으로 만든 뒤, 손상본에서 0xFF 로 — main 에서 처리 */
    }
    case 4: { /* 32비트 이하 연속 burst */
        uint32_t start = rnd() % (BLK * 8 - 32), len = 2 + rnd() % 31;
        for (uint32_t k = 0; k < len; k++)
            if (k == 0 || k == len - 1 || (rnd() & 1)) b[(start + k) / 8] ^= (uint8_t)(1u << ((start + k) % 8));
        break;
    }
    case 5: /* 블록 전체가 쓰레기 */
        for (int i = 0; i < BLK; i++) b[i] = (uint8_t)rnd();
        break;
    }
}
static const char *fault_names[] = { "1-bit flip", "2 bits, same col", "swap 2 words", "0x00 -> 0xFF",
                                     "burst <= 32 bits", "random garbage" };

/* ---------------- misdirected / lost write ---------------- */
typedef struct { uint32_t csum; int disk, blockno; uint8_t data[64]; } dblk_t;
static dblk_t disk0[8];

static void write_blk(int target, int actual, char c, int lost) { /* target 에 쓰려 했지만 actual 에 써짐 */
    dblk_t b;
    memset(b.data, c, sizeof b.data);
    b.csum = ck_crc32(b.data, sizeof b.data);
    b.disk = 0; b.blockno = target; /* 의도한 물리 ID 를 함께 기록 */
    if (!lost) disk0[actual] = b;
}
static void read_blk(int addr, uint32_t parent_csum) {
    dblk_t *b = &disk0[addr];
    int ok_ck = ck_crc32(b->data, sizeof b->data) == b->csum;
    int ok_id = b->blockno == addr;
    int ok_parent = ck_crc32(b->data, sizeof b->data) == parent_csum;
    printf("  read blk %d: data='%c'  csum %s  physID %s  parent-csum %s\n", addr, b->data[0],
           ok_ck ? "ok " : "BAD", ok_id ? "ok " : "BAD", ok_parent ? "ok " : "BAD");
}

int main(void) {
    crc_init();
    printf("== 1. book XOR example ==\n");
    uint8_t ex[16] = { 0x36, 0x5e, 0xc4, 0xcd, 0xba, 0x14, 0x8a, 0x92,
                       0xec, 0xef, 0x2c, 0x3a, 0x40, 0xbe, 0xf6, 0x66 };
    printf("XOR32 = 0x%08x  ADD32 = 0x%08x  Fletcher16 = 0x%04x  CRC32 = 0x%08x\n", ck_xor(ex, 16),
           ck_add(ex, 16), ck_fletcher16(ex, 16), ck_crc32(ex, 16));
    uint8_t h1[4] = { 1, 2, 3, 4 }, h2[4] = { 2, 1, 3, 4 };
    printf("checksum.py -D 1,2,3,4 style (bytes): add=%u xor=%u fletcher(a,b)=(%u,%u)\n",
           (1 + 2 + 3 + 4) & 0xff, 1 ^ 2 ^ 3 ^ 4, ck_fletcher16(h1, 4) & 0xff, ck_fletcher16(h1, 4) >> 8);
    printf("reordered 2,1,3,4                : fletcher(a,b)=(%u,%u)  <- only Fletcher notices order\n",
           ck_fletcher16(h2, 4) & 0xff, ck_fletcher16(h2, 4) >> 8);

    printf("\n== 2. undetected corruptions out of 20000 trials per fault (4KB block) ==\n");
    printf("%-18s", "fault");
    for (int f = 0; f < 4; f++) printf("%12s", names[f]);
    printf("\n");
    static uint8_t orig[BLK], bad[BLK];
    const int TRIALS = 20000;
    for (int kind = 0; kind < 6; kind++) {
        int miss[4] = { 0 };
        for (int t = 0; t < TRIALS; t++) {
            for (int i = 0; i < BLK; i++) orig[i] = (uint8_t)rnd();
            if (kind == 3) { /* 원본에 0x00 바이트 하나, 손상본은 그 자리가 0xFF */
                uint32_t i = rnd() % BLK;
                orig[i] = 0x00;
                memcpy(bad, orig, BLK);
                bad[i] = 0xFF;
            } else {
                memcpy(bad, orig, BLK);
                fault(bad, kind);
            }
            if (memcmp(orig, bad, BLK) == 0) continue;
            for (int f = 0; f < 4; f++)
                if (fns[f](orig, BLK) == fns[f](bad, BLK)) miss[f]++;
        }
        printf("%-18s", fault_names[kind]);
        for (int f = 0; f < 4; f++) printf("%12d", miss[f]);
        printf("\n");
    }

    printf("\n== 3. speed over 64 MB (-O0, relative only) ==\n");
    size_t N = 64u << 20;
    uint8_t *big = malloc(N);
    if (!big) return 1;
    for (size_t i = 0; i < N; i++) big[i] = (uint8_t)(i * 2654435761u >> 13);
    for (int f = 0; f < 4; f++) {
        clock_t c0 = clock();
        volatile uint32_t r = 0;
        for (size_t off = 0; off < N; off += BLK) r ^= fns[f](big + off, BLK);
        double s = (double)(clock() - c0) / CLOCKS_PER_SEC;
        printf("%-11s %8.1f MB/s\n", names[f], 64.0 / s);
    }
    free(big);

    printf("\n== 4. misdirected write & lost write ==\n");
    for (int i = 0; i < 8; i++) write_blk(i, i, (char)('A' + i), 0);
    uint32_t parent[8]; /* ZFS 처럼 "부모"(inode/간접블록)가 자식 체크섬을 들고 있음 */
    for (int i = 0; i < 8; i++) parent[i] = disk0[i].csum;
    printf("misdirected: write 'X' meant for blk 3 lands on blk 5\n");
    write_blk(3, 5, 'X', 0);
    { dblk_t t; memset(t.data, 'X', 64); parent[3] = ck_crc32(t.data, 64); }
    read_blk(5, parent[5]);
    read_blk(3, parent[3]);
    printf("lost write: write 'Y' to blk 6 is acked but never persisted\n");
    write_blk(6, 6, 'Y', 1);
    { dblk_t t; memset(t.data, 'Y', 64); parent[6] = ck_crc32(t.data, 64); }
    read_blk(6, parent[6]);
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 -pthread code/C44_checksums.c -o .work/bin/C44_checksums && .work/bin/C44_checksums
== 1. book XOR example ==
XOR32 = 0x201b9403  ADD32 = 0x1e2171ff  Fletcher16 = 0x04b2  CRC32 = 0x314ca7a8
checksum.py -D 1,2,3,4 style (bytes): add=10 xor=4 fletcher(a,b)=(10,20)
reordered 2,1,3,4                : fletcher(a,b)=(10,21)  <- only Fletcher notices order

== 2. undetected corruptions out of 20000 trials per fault (4KB block) ==
fault                    XOR32       ADD32  Fletcher16       CRC32
1-bit flip                   0           0           0           0
2 bits, same col         20000       10395          30           0
swap 2 words             20000       20000         464           0
0x00 -> 0xFF                 0           0       20000           0
burst <= 32 bits             0           0           0           0
random garbage               0           0           0           0

== 3. speed over 64 MB (-O0, relative only) ==
XOR32         1893.2 MB/s
ADD32         2248.1 MB/s
Fletcher16     267.5 MB/s
CRC32          393.6 MB/s

== 4. misdirected write & lost write ==
misdirected: write 'X' meant for blk 3 lands on blk 5
  read blk 5: data='X'  csum ok   physID BAD  parent-csum BAD
  read blk 3: data='D'  csum ok   physID ok   parent-csum BAD
lost write: write 'Y' to blk 6 is acked but never persisted
  read blk 6: data='G'  csum ok   physID ok   parent-csum BAD
```

해석:

- **1번**: 원문 XOR 예제 **0x201b9403** 을 그대로 재현. checksum.py 의 `1,2,3,4` 결과(add 10, xor 4, fletcher 10,20)와 순서를 바꾼 `2,1,3,4` → Fletcher만 (10, 21)로 변함.
- **2번 표 (2만 번 중 못 잡은 횟수)**:
  - **1비트 뒤집기, 32비트 이하 burst, 완전 쓰레기**: 넷 다 잡는다(쓰레기의 경우 이론적 충돌 확률 2^-32, Fletcher16은 2^-16 수준 — 2만 번으로는 0).
  - **같은 열의 비트 2개**: XOR은 **100% 놓침**(원문 설명 그대로). ADD는 **약 절반**(10395) — 두 비트가 반대 방향(0→1, 1→0)으로 바뀌면 합이 그대로라서. Fletcher16은 30번(0.15%), CRC32는 0.
  - **두 워드 자리 바꿈**: XOR·ADD **100% 놓침**(교환법칙). Fletcher16도 464번(2.3%) 놓침 — 두 워드를 바꾸면 s1(단순 합)은 그대로이고 s2의 변화량은 (위치 차) × (두 워드의 바이트 합 차) mod 255 인데, 255 = 3·5·17 이라 이 곱이 255의 배수가 되는 경우가 생각보다 흔하기 때문. CRC32는 0.
  - **0x00 → 0xFF**: **Fletcher16만 100% 놓침** (mod 255에서 0 ≡ 255). XOR/ADD/CRC는 다 잡는다.
  - 결론: "Fletcher는 CRC에 거의 맞먹는다"는 원문 말은 **랜덤 비트 오류 기준**으로는 맞지만, 구조적 손상(바이트 0xFF↔0x00, 특정 재배열)에선 CRC가 확실히 강하다. 스토리지에서 CRC32C(iSCSI, ext4 메타데이터, Btrfs)가 표준이 된 이유.
- **3번 속도 (-O0)**: XOR/ADD는 워드 단위라 빠르고, Fletcher16은 **바이트마다 mod 연산 2번**이라 가장 느리며, 테이블 CRC32는 그 사이. 실제로는 Fletcher도 mod를 미루는 최적화를 하고, CRC32C는 **CPU 명령(x86 SSE4.2 `crc32`, ARMv8 `CRC32C*`)** 으로 수 GB/s 가 나온다 — 절대값보다 "강할수록 비싸다(TNSTAAFL)"의 감각만 볼 것.
- **4번**: blk 3에 갈 'X' 가 blk 5에 써졌다 → blk 5는 **자기 체크섬은 맞는데(csum ok) 물리 ID가 3이라 BAD** → misdirected 탐지. blk 3은 옛 'D' 그대로라 csum·physID 모두 ok — **부모 체크섬만이** "쓰기가 도착 안 했다"를 잡는다. lost write(blk 6)도 마찬가지로 **parent-csum만 BAD**. 6·7절의 주장을 정확히 재현한다.

### 11.2 checksum.py 시뮬레이터

```text
cd .tools/ostep-homework/file-integrity
python3 checksum.py -c
```

```text
OPTIONS seed 0
OPTIONS data_size 4
OPTIONS data 

Decimal:          216        194        107         66 
Hex:             0xd8       0xc2       0x6b       0x42 
Bin:       0b11011000 0b11000010 0b01101011 0b01000010 

Add:             71       (0b01000111)
Xor:             51       (0b00110011)
Fletcher(a,b):   73,196   (0b01001001,0b11000100)
```

손으로 검산:

```text
Add : 216 + 194 + 107 + 66 = 583,  583 mod 256 = 71                        ✓
Xor : 11011000 ^ 11000010 = 00011010
      00011010 ^ 01101011 = 01110001
      01110001 ^ 01000010 = 00110011 = 51                                  ✓
Fletcher (mod 255):
      s1: 216 → (216+194)=410 mod 255=155 → 155+107=262 mod 255=7 → 7+66 = 73
      s2: 216 → 216+155=371 mod 255=116 → 116+7=123 → 123+73 = 196         ✓
```

Fletcher의 0x00/0xFF 맹점과 순서 민감성:

```text
python3 checksum.py -D 255,0,0,0 -c   →  Add 255, Xor 255, Fletcher(a,b) 0, 0
python3 checksum.py -D 2,1,3,4 -c     →  Add 10,  Xor 4,   Fletcher(a,b) 10, 21
python3 checksum.py -D 1,2,3,4 -c     →  Add 10,  Xor 4,   Fletcher(a,b) 10, 20
```

(마지막 세 줄은 각 실행의 `Add/Xor/Fletcher` 줄만 한 줄로 모은 것.) `255,0,0,0` 의 Fletcher는 `0,0,0,0` 과 같은 (0, 0) — 4.2절의 약점이 시뮬레이터에서도 그대로 보인다.

## 12. 펌웨어 엔지니어의 눈으로

- **LSE = SSD의 UECC(uncorrectable ECC) 읽기.** NAND는 LSE가 "가끔"이 아니라 **일상**이다: 모든 페이지가 BCH/LDPC ECC로 보호되고, hard-decision 실패 → **read retry(Vref 이동)** → **soft-decision LDPC** → 그래도 실패하면 die 간 XOR 패리티(**RAIN/RAID-like**, 원문 3절의 "중복성으로 복구")로 재구성, 최후엔 호스트에 Unrecovered Read Error. Don이 다룬 ECC 파이프라인 전체가 원문 3절 한 문단의 하드웨어 버전이다. "재구성 중 또 LSE" 문제 때문에 다이 실패 + 페이지 UECC를 동시에 견디도록 패리티 그룹을 설계하는 것도 RAID-DP와 같은 사고.
- **520B 섹터 + 물리 ID = T10 DIF/PI, NVMe End-to-End Data Protection.** PI 8바이트 = **Guard(CRC-16 T10-DIF)** + App Tag + **Ref Tag(= LBA 하위 32비트)**. Ref Tag가 바로 원문의 "physical ID"라서 **misdirected write/read를 탐지**한다. NVMe에서는 컨트롤러가 호스트에서 받은 PI를 DMA 경로·DRAM 버퍼·NAND까지 들고 다니며 각 구간에서 검사한다(PRACT/PRCHK 비트). FW 버그로 맵이 꼬여 다른 LBA의 데이터를 읽어도 Ref Tag 불일치로 걸린다.
- **misdirected/lost write 대응 = OOB 메타데이터.** NAND 페이지의 spare 영역에 **LBA + 시퀀스 번호 + CRC** 를 같이 프로그램한다. 읽은 페이지의 OOB LBA ≠ 요청 LBA → 매핑 오류(misdirected). lost write는 SSD에서 "**Flush 완료 보고 후 전원 손실로 사라진 쓰기**"로 나타나며, PLP 또는 정확한 flush 처리 외엔 원천 해결책이 없다 — 그래서 상위(ZFS, DB의 page LSN/체크섬)가 부모 쪽 체크섬으로 방어한다.
- **scrubbing = background media scan / patrol read / read-disturb refresh.** NAND는 시간이 지나면 전하가 새고(data retention), 이웃 읽기로 교란(read disturb)된다. FW는 유휴 시간에 블록을 읽어 **ECC 정정 비트 수가 임계치를 넘으면 미리 다른 블록으로 옮긴다(refresh/relocation)** — "모든 사본이 썩기 전에 고친다"는 scrub 동기 그대로이며, 임계치 판단이 체크섬 일치/불일치보다 한 단계 정교하다(얼마나 나빠졌는지까지 앎).
- **"복사와 체크섬 합치기" = 데이터 경로 하드웨어 오프로드.** SSD 컨트롤러는 호스트 DMA 엔진·NAND 채널 컨트롤러 안에 **CRC/ECC 엔진을 인라인**으로 두어 데이터가 지나가는 김에 계산한다(CPU 사이클 0). AI 가속기·NIC도 같은 이유로 DMA 경로에 CRC를 둔다. 원문 9절의 소프트웨어 최적화의 하드웨어 극단.
- **Fletcher vs CRC 선택**: 11.1 결과처럼 Fletcher의 0x00/0xFF 맹점은 **erase 상태(0xFF)** 와 **0으로 채운 데이터**가 흔한 NAND에서는 치명적일 수 있다. 펌웨어 메타데이터 보호에 CRC(하드웨어 가속)를 쓰는 실무적 이유 중 하나.

## 13. 면접 질문

### Q1. fail-stop과 fail-partial의 차이는? LSE와 corruption은 각각 어떻게 다루나?
<details>
<summary>답 보기</summary>

**fail-stop**: 디스크 전체가 동작하거나 완전히 죽음, 탐지 자명 (초기 RAID의 가정). **fail-partial**: 대체로 동작하면서 일부 블록이 **에러 반환(LSE, non-silent)** 하거나 **틀린 데이터 반환(corruption, silent)**. LSE: 탐지는 공짜 → 미러/패리티로 **복구**, 재구성 중 LSE 대비 **이중 패리티(RAID-6/RAID-DP)**. corruption: **탐지가 핵심** → 체크섬(+물리 ID, 부모 체크섬), 탐지 후엔 다른 사본으로 복구.

</details>

### Q2. XOR, 덧셈, Fletcher, CRC의 장단점을 비교하라. 각각 못 잡는 오류 예는?
<details>
<summary>답 보기</summary>

**XOR**: 가장 빠름, **같은 비트 위치의 2비트 오류**·워드 재배열 못 잡음. **덧셈**: 빠름, **재배열**(교환법칙)·상쇄되는 변화 못 잡음. **Fletcher**: s2가 위치 가중합이라 재배열·모든 1/2비트 오류 탐지, CRC에 근접. 단 **mod 255라 0x00↔0xFF 구분 불가**, 특정 거리의 재배열 일부 놓침, 바이트당 연산이 많음. **CRC**: 다항식 나눗셈, 차수 이하 **burst 전부** 탐지, 구조적 손상에 강함, 테이블/하드웨어 명령으로 빠름. 모든 체크섬은 **충돌**이 있다(큰 → 작은 사상).

</details>

### Q3. 블록마다 체크섬을 저장해도 잡을 수 없는 고장 두 가지와 그 해법은?
<details>
<summary>답 보기</summary>

(1) **misdirected write**: 데이터와 체크섬이 함께 엉뚱한 주소에 써짐 → 그 블록 자체는 일관됨. 해법: 체크섬에 **물리 ID(디스크·블록 번호)** 포함 (T10 PI의 Ref Tag). (2) **lost write**: 쓰기가 아예 안 됨 → 옛 블록의 체크섬·ID 모두 맞음. 해법: **read-after-write**(I/O 2배) 또는 **부모 메타데이터에 자식 체크섬**(ZFS의 블록 포인터 체크섬, Merkle tree) — 부모와 자식 쓰기가 동시에 사라질 때만 실패.

</details>

### Q4. ZFS는 왜 체크섬을 블록 안이 아니라 부모 포인터에 두나? 그 구조의 다른 이점은?
<details>
<summary>답 보기</summary>

블록 안 체크섬은 블록 자신과 함께 쓰이므로 **lost write·misdirected write**를 못 잡는다. 부모(간접 블록/inode)의 포인터 옆에 자식 체크섬을 두면 "부모가 기대하는 내용"과 비교하게 된다. 이게 루트까지 이어지면 **Merkle tree** → 루트(uberblock) 하나로 전체 트리의 무결성 검증, COW와 결합해 원자적 루트 교체 시 일관 + 검증된 스냅샷. 대가: 블록이 바뀌면 체크섬이 루트까지 전파 — COW라 어차피 새 경로를 쓰므로 추가 비용이 작다.

</details>

### Q5. 체크섬 검사가 읽기 시에만 일어나면 무엇이 문제고, scrubbing은 어떻게 설계하나?
<details>
<summary>답 보기</summary>

거의 안 읽히는 데이터는 손상이 쌓여도 모른다 → 언젠가 **모든 사본**이 망가진 뒤에야 발견. scrubbing: 주기적으로 모든 블록을 읽어 검사·수리. 설계 포인트: **I/O 예산**(유휴 시간/야간, 속도 제한), **전체 주기**(예: 주 1회 — 손상률과 사본 수로 결정), LSE의 **공간 지역성**을 이용해 에러 근처를 우선 검사, 발견 시 **즉시 재작성**. SSD에선 ECC 정정 비트 수로 "곧 깨질" 블록을 미리 옮기는 patrol read/refresh가 같은 역할.

</details>

## 14. 자가 점검 & 숙제

### 퀴즈 1. 4KB 블록마다 8바이트 체크섬. 10TB 디스크에서 체크섬이 차지하는 공간은?
<details>
<summary>답 보기</summary>

8/4096 = 0.195% → 10TB × 0.00195 ≈ **19.5GB** (정확히 10×2^40 / 4096 × 8 = 20,480MiB = 20GiB, 이진 단위 기준).

</details>

### 퀴즈 2. 바이트 `10, 20, 30` 의 Fletcher (a, b)는? `30, 20, 10` 은?
<details>
<summary>답 보기</summary>

`10,20,30`: s1 = 10, 30, 60 → a = 60, s2 = 10, 40, 100 → b = **(60, 100)**. `30,20,10`: s1 = 30, 50, 60, s2 = 30, 80, 140 → **(60, 140)**. a(단순 합)는 같고 b가 순서를 구별한다.

</details>

### 퀴즈 3. 체크섬 섹터를 따로 두는 방식에서, 연속된 블록 D0~D7 (같은 체크섬 섹터 공유) 을 한꺼번에 덮어쓰면 I/O는 몇 번?
<details>
<summary>답 보기</summary>

체크섬 8개를 모두 새로 계산하므로 체크섬 섹터를 **읽을 필요조차 없을 수 있다**(같은 섹터의 다른 체크섬이 있으면 읽기 1). 쓰기는 데이터 8 + 체크섬 섹터 1. 큰 순차 쓰기에선 체크섬 섹터 비용이 amortize 된다 — 작은 랜덤 덮어쓰기에서만 "1 read + 2 writes"가 아프다.

</details>

### 퀴즈 4. misdirected write로 블록 x에 갈 데이터가 블록 y에 써졌다. (1) y를 읽을 때, (2) x를 읽을 때 각각 무엇으로 탐지하나?
<details>
<summary>답 보기</summary>

(1) y: 블록 자체 체크섬은 맞지만 저장된 **물리 ID = x ≠ y** → 탐지. (2) x: 옛 데이터가 남아 체크섬·ID 모두 맞음 → **lost write와 같은 상황** → 부모 체크섬(또는 버전/세대 번호)이 있어야 탐지 (11.1의 4번 출력).

</details>

### 원문 Homework 중 꼭 해볼 것

이 판(v0.91) 원문에는 44장 Homework 문항이 없지만, `checksum.py` README와 11.1 코드로:

- **`-s 1,2,3` 의 Add/Xor/Fletcher를 손으로 계산 후 `-c` 로 확인**: mod 256 덧셈, 비트 XOR, mod 255 Fletcher 계산 연습 (11.2 검산 방식).
- **Add·Xor는 같고 Fletcher만 다른 입력, 그리고 Fletcher까지 같은 서로 다른 입력 찾기**: 각 체크섬이 무엇에 둔감한지(재배열, 0x00/0xFF) 확인.
- **`code/C44_checksums.c` 확장**: CRC32C(Castagnoli 다항식 0x82F63B78)를 추가하고, 하드웨어 명령(`__builtin_arm_crc32cd`, Apple Silicon)과 속도 비교 — "강할수록 비싸다"가 하드웨어 지원으로 어떻게 뒤집히는지.

## 15. 다음으로

- Persistence 파트 정리 → 다음 파트 [Ch.47 분산 시스템](2026-09-30_C47_distributed_systems.md) (네트워크에서도 체크섬 + 재전송, 같은 문제의 다른 무대)
- NAND 관점의 ECC·scrub·패리티 → [부록 I Flash SSD](2026-09-30_C0I_flash_ssd.md)
- 복습: [Ch.38 RAID](2026-09-30_C38_raid.md) (fail-stop 가정, 패리티 재구성), [Ch.43 LFS](2026-09-30_C43_lfs.md) (WAFL·ZFS의 COW)
