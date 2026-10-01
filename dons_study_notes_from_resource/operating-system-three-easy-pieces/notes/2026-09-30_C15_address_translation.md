# Ch.15 주소 변환 — base & bounds 로 하는 동적 재배치

> 📖 원문: [15. Mechanism: Address Translation](../book-md/C15_mechanism_address_translation.md) · [PDF p.152](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=152) · ⏱️ 읽기 약 45분 · 🔗 선행: [Ch.06](2026-09-30_C06_limited_direct_execution.md), [Ch.13](2026-09-30_C13_address_spaces.md)

## 0. 한눈에 보기

- CPU 가상화에서 쓴 **제한적 직접 실행(LDE)** 전략을 메모리에도 적용한다: 평소엔 프로그램이 하드웨어에서 직접 돌고, 중요한 순간에만 OS 가 끼어든다.
- 핵심 메커니즘은 **하드웨어 기반 주소 변환(hardware-based address translation)**: 모든 명령어 fetch, load, store 마다 하드웨어가 가상 주소를 물리 주소로 바꾼다.
- 첫 번째 구현: **base & bounds (= 동적 재배치, dynamic relocation)**. `PA = VA + base`, 단 `VA < bounds` 가 아니면 예외.
- OS 의 일: 프로세스 생성 시 빈 슬롯 찾기(free list), 종료 시 회수, **컨텍스트 스위치 때 base/bounds 저장·복원**, 예외 핸들러 설치.
- 한계: 주소 공간 전체를 연속으로 올려야 해서 힙과 스택 사이의 빈 공간이 물리 메모리를 낭비한다 → **내부 단편화(internal fragmentation)** → 다음 장 세그먼트로.

> **THE CRUX: HOW TO EFFICIENTLY AND FLEXIBLY VIRTUALIZE MEMORY** — "How can we build an efficient virtualization of memory? How do we provide the flexibility needed by applications? How do we maintain control over which memory locations an application can access, and thus ensure that application memory accesses are properly restricted? How do we do all of this efficiently?"
>
> → 메모리를 어떻게 효율적으로 가상화할까? 응용이 원하는 유연성은 어떻게 주나? 응용이 접근할 수 있는 위치를 어떻게 통제할까? 이 모든 걸 어떻게 효율적으로 할까?

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 주소 변환(address translation) | 하드웨어가 매 메모리 접근의 VA 를 PA 로 바꿈 | VA 128 → PA 32896 |
| 개입(interposition) | 인터페이스 사이에 끼어들어 기능 추가 | MMU 가 모든 load/store 에 끼어듦 |
| 정적 재배치(static relocation) | 로더가 실행 파일의 주소를 미리 고쳐 씀 | `movl 1000` → `movl 4000` |
| 동적 재배치(dynamic relocation) | 실행 중 하드웨어가 base 를 더함 | base & bounds |
| base 레지스터 | 주소 공간이 물리 메모리에서 시작하는 곳 | 32KB |
| bounds(limit) 레지스터 | 주소 공간 크기(또는 끝 물리 주소) | 16KB |
| MMU(memory management unit) | CPU 안의 주소 변환 담당 회로 | base/bounds → 나중에 TLB, 페이지 테이블 워커 |
| 특권 모드(privileged/kernel mode) | base/bounds 를 바꿀 수 있는 유일한 모드 | 유저가 바꾸려 하면 예외 |
| out-of-bounds 예외 | VA ≥ bounds 일 때 하드웨어가 일으킴 | OS 가 프로세스 종료 |
| free list | 사용 중이 아닌 물리 메모리 범위 목록 | [16-32KB], [48-64KB] |
| PCB(process control block) | 프로세스별 저장 구조. base/bounds 도 여기 저장 | `struct proc` |
| 내부 단편화(internal fragmentation) | 할당 단위 안에서 안 쓰고 버려지는 공간 | 힙-스택 사이 빈칸 |

## 2. 가정 (15.1)

일단 우스울 정도로 단순하게 시작한다.

1. 주소 공간은 물리 메모리에 **연속(contiguous)** 으로 놓인다.
2. 주소 공간은 물리 메모리보다 **작다**.
3. 모든 주소 공간의 **크기가 같다**.

비현실적이지만, 다음 장들에서 하나씩 풀어 간다(세그먼트 → 1 완화, 페이징 → 1·3 완화, 스와핑 → 2 완화).

## 3. 예제: x = x + 3 (15.2)

```c
void func() {
    int x = 3000;
    x = x + 3;   // 관심 있는 줄
}
```

원문(x86, 32비트)은 이렇게 컴파일된다고 한다. `x` 의 주소가 `ebx` 에 있다고 가정:

```text
128: movl 0x0(%ebx), %eax   ; load 0+ebx into eax
132: addl $0x03, %eax       ; add 3 to eax register
135: movl %eax, 0x0(%ebx)   ; store eax back to mem
```

같은 일을 내 Mac(arm64)에서 `cc -O1 -c` 후 `otool -tv` 로 보면 (함수 인자로 `x` 의 주소를 받게 바꾼 버전):

```text
_func:
0000000000000000	ldr	w8, [x0]
0000000000000004	add	w8, w8, #0x3
0000000000000008	str	w8, [x0]
000000000000000c	ret
```

load → add → store 구조는 똑같다. 프로세스 입장에서 일어나는 **메모리 접근** 은:

1. 주소 128 에서 명령어 fetch
2. 실행: 주소 15KB 에서 load
3. 주소 132 에서 명령어 fetch
4. 실행: (메모리 접근 없음)
5. 주소 135 에서 명령어 fetch
6. 실행: 주소 15KB 에 store

**명령어 하나에 메모리 접근이 1~2번** — fetch 도 주소 변환 대상이라는 점을 놓치지 말 것.

프로그램은 자기 주소 공간이 0 ~ 16KB 라고 믿는다. 그런데 OS 는 이걸 물리 32KB 에 올려 두고 싶다(Fig 15.2). 프로그램 모르게(**투명하게**) 어떻게 재배치할까?

> **TIP — 개입은 강력하다(INTERPOSITION IS POWERFUL)**: 잘 정의된 인터페이스라면 그 사이에 끼어들어 기능을 추가할 수 있다. 하드웨어가 모든 메모리 접근에 끼어들어 주소를 바꾸는 것이 대표 사례이고, 장점은 클라이언트(프로그램)를 전혀 고치지 않아도 된다는 것.

## 4. 동적(하드웨어 기반) 재배치 (15.3)

1950년대 말 첫 시분할 기계에서 나온 아이디어. CPU 마다 레지스터 두 개:

- **base**: 이 프로세스가 물리 메모리 어디서 시작하나
- **bounds**(limit): 이 프로세스가 쓸 수 있는 크기

프로그램은 0번지에 로드된다고 가정하고 컴파일된다. 실행 시 OS 가 물리 위치를 정해 base 에 넣는다. 그 후 **모든** 메모리 참조는:

```text
if (VA >= bounds) raise OUT_OF_BOUNDS exception   (음수도 걸림)
PA = VA + base
```

```svg
<svg viewBox="0 0 700 260" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C15-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="350" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">MMU 안의 base &amp; bounds 데이터패스</text>
  <rect x="20" y="95" width="110" height="44" fill="none" stroke="currentColor" rx="6"/>
  <text x="75" y="115" text-anchor="middle" fill="currentColor">CPU 코어</text>
  <text x="75" y="131" text-anchor="middle" fill="currentColor" font-size="11">VA = 15KB</text>
  <line x1="130" y1="117" x2="198" y2="117" stroke="currentColor" marker-end="url(#C15-arrow)"/>
  <rect x="200" y="92" width="110" height="50" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="255" y="114" text-anchor="middle" fill="currentColor">비교기</text>
  <text x="255" y="131" text-anchor="middle" fill="currentColor" font-size="11">VA &lt; bounds ?</text>
  <rect x="200" y="35" width="110" height="34" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="255" y="57" text-anchor="middle" fill="currentColor">bounds = 16KB</text>
  <line x1="255" y1="69" x2="255" y2="90" stroke="currentColor" marker-end="url(#C15-arrow)"/>
  <line x1="310" y1="117" x2="398" y2="117" stroke="currentColor" marker-end="url(#C15-arrow)"/>
  <text x="354" y="110" text-anchor="middle" fill="currentColor" font-size="11">yes</text>
  <circle cx="420" cy="117" r="20" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="420" y="122" text-anchor="middle" fill="currentColor" font-size="18">+</text>
  <rect x="365" y="35" width="110" height="34" fill="none" style="stroke:var(--accent)" stroke-width="2"/>
  <text x="420" y="57" text-anchor="middle" fill="currentColor">base = 32KB</text>
  <line x1="420" y1="69" x2="420" y2="95" stroke="currentColor" marker-end="url(#C15-arrow)"/>
  <line x1="440" y1="117" x2="538" y2="117" stroke="currentColor" marker-end="url(#C15-arrow)"/>
  <rect x="540" y="92" width="140" height="50" fill="none" stroke="currentColor" rx="6"/>
  <text x="610" y="114" text-anchor="middle" fill="currentColor">물리 메모리</text>
  <text x="610" y="131" text-anchor="middle" fill="currentColor" font-size="11">PA = 47KB</text>
  <line x1="255" y1="142" x2="255" y2="188" stroke="#d9534f" marker-end="url(#C15-arrow)"/>
  <text x="265" y="170" fill="#d9534f" font-size="11">no</text>
  <rect x="180" y="190" width="150" height="44" fill="#d9534f" fill-opacity="0.15" stroke="#d9534f" rx="6"/>
  <text x="255" y="210" text-anchor="middle" fill="currentColor">out-of-bounds 예외</text>
  <text x="255" y="226" text-anchor="middle" fill="currentColor" font-size="11">→ 커널 모드, OS 핸들러</text>
  <text x="520" y="200" fill="currentColor" font-size="11">base/bounds 는 특권 명령으로만 변경.</text>
  <text x="520" y="216" fill="currentColor" font-size="11">CPU(코어)마다 한 쌍.</text>
  <text x="520" y="232" fill="currentColor" font-size="11">컨텍스트 스위치 때 PCB 에 저장/복원.</text>
</svg>
```

### 4.1 원문 트레이스

base = 32KB(32768), bounds = 16KB:

```text
명령어 fetch  VA 128     → 128 < 16384 OK → PA = 128 + 32768     = 32896
load         VA 15KB    → 15360 < 16384 OK → PA = 15360 + 32768 = 48128 (= 47KB)
명령어 fetch  VA 132     → PA 32900
store        VA 15KB    → PA 48128
```

변환이 실행 시점에 일어나고, 프로세스가 돌기 시작한 뒤에도 주소 공간을 옮길 수 있으니 **동적** 재배치라고 부른다.

> **ASIDE — 소프트웨어 기반(정적) 재배치**: 하드웨어 지원 전에는 **로더(loader)** 가 실행 파일 안의 주소를 미리 고쳐 썼다. 3000 에 로드한다면 `movl 1000, %eax` → `movl 4000, %eax`. 문제: (1) **보호가 없다** — 프로그램이 아무 주소나 만들어 낼 수 있다. (2) 한 번 놓으면 **다시 옮기기 어렵다**.

> **TIP — 하드웨어 기반 동적 재배치**: 약간의 하드웨어가 큰 일을 한다. base 로 변환하고 bounds 로 범위를 지킨다. 단순하고 효율적인 메모리 가상화.

### 4.2 bounds 의 두 가지 정의

- **(A) 크기** 를 담는다: 먼저 `VA < bounds` 검사 → base 를 더함. (이 책의 기본)
- **(B) 끝 물리 주소** 를 담는다: 먼저 base 를 더함 → `PA < bounds` 검사.

논리적으로 동등하다. (A) 는 덧셈과 비교를 **병렬로** 할 수 있어 하드웨어가 살짝 유리하다(비교가 base 와 무관하므로).

### 4.3 변환 예제 (원문)

4KB 주소 공간을 물리 16KB 에 올렸다 (base = 16384, bounds = 4096):

| VA | 계산 | PA |
|---|---|---|
| 0 | 0 < 4096, 0 + 16384 | 16KB (16384) |
| 1KB (1024) | 1024 < 4096, 1024 + 16384 | 17KB (17408) |
| 3000 | 3000 < 4096, 3000 + 16384 | 19384 |
| 4400 | 4400 ≥ 4096 | **Fault (out of bounds)** |

### 4.4 새 예제 (직접 계산)

base = 0x3082 (12418), bounds = 472 — 아래 시뮬레이터 seed 0 의 값이다.

| VA | 계산 | 결과 |
|---|---|---|
| 430 (0x1ae) | 430 < 472 → 430 + 12418 | **12848** (0x3230) |
| 265 (0x109) | 265 < 472 → 265 + 12418 | **12683** (0x318b) |
| 523 (0x20b) | 523 ≥ 472 | **violation** |
| 471 | 471 < 472 → 471 + 12418 | 12889 (마지막 합법 바이트) |
| 472 | 472 ≥ 472 | violation (첫 불법 바이트) |

경계 규칙: **합법 VA 는 0 ~ bounds-1**. "bounds 와 같으면" 이미 밖이다.

> **ASIDE — 자료구조: free list**: OS 는 어떤 물리 메모리 범위가 비어 있는지 추적해야 한다. 가장 단순한 건 빈 범위들의 리스트. Fig 15.2 라면 [16KB-32KB], [48KB-64KB] 두 항목.

## 5. 하드웨어 지원 요약 (15.4)

| 하드웨어 요구 사항 | 왜 필요한가 |
|---|---|
| 특권 모드(privileged mode) | 유저 프로세스가 특권 연산을 못 하게 |
| base/bounds 레지스터 | CPU 마다 한 쌍. 변환과 범위 검사 |
| VA 변환 + 범위 검사 회로 | 덧셈기와 비교기. 이 경우 매우 단순 |
| base/bounds 갱신용 특권 명령 | OS 가 유저 프로그램 실행 전에 설정 |
| 예외 핸들러 등록용 특권 명령 | 예외 시 실행할 OS 코드를 하드웨어에 알려 줌 |
| 예외 발생 능력 | 특권 명령 시도, 범위 밖 접근 시 |

base 레지스터를 유저가 바꿀 수 있다면? 아무 물리 주소나 읽고 쓸 수 있다 → 보호 붕괴. 그래서 **특권 명령** 이다.

## 6. OS 가 할 일 (15.5)

| OS 요구 사항 | 내용 |
|---|---|
| 메모리 관리 | 새 프로세스용 메모리 할당, 종료 프로세스 회수, free list 관리 |
| base/bounds 관리 | 컨텍스트 스위치 때 올바르게 설정 |
| 예외 처리 | 예외 시 실행할 코드. 보통 문제 프로세스 종료 |

1. **프로세스 생성**: 모든 주소 공간이 같은 크기라 물리 메모리를 **슬롯 배열** 로 보고 free list 에서 빈 슬롯을 찾아 사용 표시.
2. **프로세스 종료**: 메모리를 free list 로 돌려주고 관련 자료구조 정리.
3. **컨텍스트 스위치**: CPU 당 base/bounds 는 한 쌍뿐이므로, 나가는 프로세스의 값을 **PCB 에 저장**, 들어오는 프로세스의 값을 **복원**.
4. **예외 핸들러**: 부팅 때 특권 명령으로 설치. 범위 밖 접근 → 대개 프로세스 종료("Bye bye, misbehaving process").

덤: 프로세스가 **멈춰 있을 때는 쉽게 옮길 수 있다**. deschedule → 주소 공간 복사 → PCB 의 base 수정 → 재개. 프로세스는 이사한 줄도 모른다.

### 6.1 타임라인 (Figure 15.5 요약)

```svg
<svg viewBox="0 0 700 430" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="12">
  <defs>
    <marker id="C15-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="6" markerHeight="6" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="110" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">OS (커널 모드)</text>
  <text x="350" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">하드웨어</text>
  <text x="590" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">프로그램 (유저 모드)</text>
  <line x1="230" y1="28" x2="230" y2="420" stroke="currentColor" stroke-dasharray="3 3"/>
  <line x1="470" y1="28" x2="470" y2="420" stroke="currentColor" stroke-dasharray="3 3"/>
  <rect x="10" y="35" width="210" height="50" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="20" y="52" fill="currentColor">부팅: 트랩 테이블 설치(시스콜,</text>
  <text x="20" y="67" fill="currentColor">타이머, 불법 메모리 접근 핸들러),</text>
  <text x="20" y="81" fill="currentColor">타이머 시작, free list 초기화</text>
  <rect x="10" y="95" width="210" height="50" fill="none" stroke="currentColor"/>
  <text x="20" y="112" fill="currentColor">A 시작: 프로세스 테이블 항목,</text>
  <text x="20" y="127" fill="currentColor">메모리 할당, <tspan style="fill:var(--accent)" font-weight="bold">base/bounds 설정</tspan>,</text>
  <text x="20" y="141" fill="currentColor">return-from-trap</text>
  <line x1="220" y1="130" x2="245" y2="160" stroke="currentColor" marker-end="url(#C15-arrow2)"/>
  <text x="250" y="170" fill="currentColor">A 레지스터 복원, 유저 모드, A 의 PC 로</text>
  <line x1="460" y1="175" x2="485" y2="190" stroke="currentColor" marker-end="url(#C15-arrow2)"/>
  <text x="490" y="200" fill="currentColor">A 실행: fetch / load / store</text>
  <text x="250" y="222" fill="currentColor">매 접근: 범위 검사 + VA→PA 변환</text>
  <text x="250" y="237" fill="currentColor" font-size="11">(OS 개입 없음 = 직접 실행)</text>
  <text x="250" y="262" fill="currentColor" font-weight="bold">타이머 인터럽트 → 커널 모드</text>
  <line x1="245" y1="267" x2="220" y2="280" stroke="currentColor" marker-end="url(#C15-arrow2)"/>
  <rect x="10" y="275" width="210" height="50" fill="none" stroke="currentColor"/>
  <text x="20" y="292" fill="currentColor">switch(): A 레지스터와</text>
  <text x="20" y="307" fill="currentColor"><tspan style="fill:var(--accent)" font-weight="bold">base/bounds 를 PCB(A)에 저장</tspan>,</text>
  <text x="20" y="321" fill="currentColor">PCB(B)에서 복원, return-from-trap</text>
  <text x="490" y="345" fill="currentColor">B 실행: <tspan fill="#d9534f">bad load</tspan></text>
  <text x="250" y="365" fill="#d9534f" font-weight="bold">범위 밖! → 커널 모드, 트랩 핸들러</text>
  <line x1="245" y1="370" x2="220" y2="383" stroke="currentColor" marker-end="url(#C15-arrow2)"/>
  <rect x="10" y="378" width="210" height="40" fill="#d9534f" fill-opacity="0.12" stroke="#d9534f"/>
  <text x="20" y="395" fill="currentColor">B 종료: 메모리 반환(free list),</text>
  <text x="20" y="410" fill="currentColor">프로세스 테이블에서 제거</text>
</svg>
```

여전히 LDE 다: OS 는 하드웨어를 세팅하고 비켜서 있다가, **타이머** 나 **잘못된 접근** 때만 개입한다.

## 7. 요약 (15.6)

- 주소 변환으로 OS 는 프로세스의 **모든** 메모리 접근을 통제하고 주소 공간 안에 가둔다.
- 효율의 열쇠는 하드웨어: 덧셈 하나 + 비교 하나.
- base & bounds 는 **보호** 도 준다: 프로세스가 트랩 테이블을 덮어써 기계를 탈취하는 일을 막는다.
- 문제: Fig 15.2 처럼 32KB~48KB 슬롯 안에서 힙과 스택 사이가 텅 비어도 통째로 잡힌다 → **내부 단편화**. 주소 공간이 물리 메모리보다 크면 아예 못 올린다.

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <text x="320" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">Fig 15.2 물리 메모리 64KB: 재배치된 프로세스와 내부 단편화</text>
  <rect x="120" y="35" width="170" height="60" fill="none" stroke="currentColor"/>
  <text x="205" y="70" text-anchor="middle" fill="currentColor">Operating System</text>
  <rect x="120" y="95" width="170" height="60" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="205" y="130" text-anchor="middle" fill="currentColor">(not in use)</text>
  <rect x="120" y="155" width="170" height="15" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="205" y="167" text-anchor="middle" fill="currentColor" font-size="11">Code</text>
  <rect x="120" y="170" width="170" height="15" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="205" y="182" text-anchor="middle" fill="currentColor" font-size="11">Heap</text>
  <rect x="120" y="185" width="170" height="40" fill="#d9534f" fill-opacity="0.15" stroke="#d9534f"/>
  <text x="205" y="209" text-anchor="middle" fill="currentColor" font-size="11">allocated but not in use</text>
  <rect x="120" y="225" width="170" height="15" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="205" y="237" text-anchor="middle" fill="currentColor" font-size="11">Stack</text>
  <rect x="120" y="240" width="170" height="50" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="205" y="270" text-anchor="middle" fill="currentColor">(not in use)</text>
  <g font-size="11" fill="currentColor" text-anchor="end">
    <text x="114" y="40">0KB</text><text x="114" y="99">16KB</text><text x="114" y="159">32KB</text>
    <text x="114" y="244">48KB</text><text x="114" y="294">64KB</text>
  </g>
  <rect x="290" y="155" width="6" height="85" style="fill:var(--accent)"/>
  <text x="305" y="165" fill="currentColor" font-size="12">base = 32KB</text>
  <text x="305" y="182" fill="currentColor" font-size="12">bounds = 16KB</text>
  <text x="305" y="209" fill="#d9534f" font-size="12">← 힙-스택 사이: 물리 메모리를 잡아먹지만</text>
  <text x="305" y="225" fill="#d9534f" font-size="12">   아무도 안 씀 = 내부 단편화</text>
  <text x="305" y="120" fill="currentColor" font-size="12">free list: [16-32KB], [48-64KB]</text>
</svg>
```

## 8. 직접 해보기

### 8.1 장난감 MMU — `code/C15_base_bounds.c`

물리 메모리를 64KB 바이트 배열로 두고, 모든 load/store 가 `translate()` 를 거치게 했다. 원문 예제 + 컨텍스트 스위치 + 특권 명령 + 프로세스 이사까지.

```c
// C15_base_bounds.c — 장난감 MMU: base & bounds 동적 재배치를 소프트웨어로 흉내 낸다
// build: cc -Wall -Wextra -O0 code/C15_base_bounds.c -o .work/bin/C15_base_bounds
//  - 물리 메모리 64KB 를 바이트 배열로 두고, 모든 load/store 를 translate() 를 거치게 한다
//  - bounds 는 "주소 공간 크기" 방식 (VA < bounds 검사 후 base 를 더함)
//  - base/bounds 변경은 커널 모드에서만 허용 (특권 명령 흉내)
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define KB 1024u
#define PHYS_SIZE (64 * KB)

static uint8_t phys[PHYS_SIZE];          // "DRAM"

typedef enum { KERNEL, USER } mode_t_;
struct cpu { uint32_t base, bounds; mode_t_ mode; } cpu;   // MMU 레지스터 1쌍 (CPU당)
struct proc { const char *name; uint32_t base, bounds; int alive; };   // PCB 에 저장되는 값

static int faults;

// 하드웨어가 매 접근마다 하는 일 (Figure 15.5 의 "Translate virtual address")
static int translate(uint32_t va, uint32_t *pa) {
    if (va >= cpu.bounds) {                 // 음수는 uint32 라서 아주 큰 값이 되어 같이 걸린다
        faults++;
        return -1;                          // → out-of-bounds 예외, OS 핸들러로
    }
    *pa = cpu.base + va;
    return 0;
}

static int set_base_bounds(uint32_t base, uint32_t bounds) {
    if (cpu.mode != KERNEL) {
        printf("  !! privileged-op exception: user mode 에서 base 변경 시도\n");
        return -1;
    }
    cpu.base = base; cpu.bounds = bounds;
    return 0;
}

static void show(uint32_t va) {
    uint32_t pa;
    if (translate(va, &pa) == 0)
        printf("  VA %5u (%5.2f KB) -> PA %5u (%5.2f KB)\n", va, va / 1024.0, pa, pa / 1024.0);
    else
        printf("  VA %5u (%5.2f KB) -> FAULT (out of bounds, bounds=%u)\n", va, va / 1024.0, cpu.bounds);
}

static int load32(uint32_t va, uint32_t *val) {
    uint32_t pa;
    if (translate(va, &pa)) return -1;
    memcpy(val, &phys[pa], 4);
    return 0;
}
static int store32(uint32_t va, uint32_t val) {
    uint32_t pa;
    if (translate(va, &pa)) return -1;
    memcpy(&phys[pa], &val, 4);
    return 0;
}

static void context_switch(struct proc *from, struct proc *to) {
    cpu.mode = KERNEL;                       // 타이머 인터럽트 → 커널 모드
    if (from) { from->base = cpu.base; from->bounds = cpu.bounds; }   // save to PCB
    set_base_bounds(to->base, to->bounds);  // restore from PCB
    cpu.mode = USER;                         // return-from-trap
    printf("  [switch] -> %s (base=%u, bounds=%u)\n", to->name, cpu.base, cpu.bounds);
}

int main(void) {
    printf("[1] 원문 예제: 4KB 주소 공간을 물리 16KB 에 올림\n");
    cpu.mode = KERNEL; set_base_bounds(16 * KB, 4 * KB); cpu.mode = USER;
    uint32_t vas[] = {0, 1 * KB, 3000, 4400};
    for (unsigned i = 0; i < 4; i++) show(vas[i]);

    printf("\n[2] Figure 15.1/15.2: 16KB 프로세스를 물리 32KB 에 재배치, x=3000 at VA 15KB\n");
    struct proc A = {"A", 32 * KB, 16 * KB, 1}, B = {"B", 16 * KB, 16 * KB, 1};
    context_switch(NULL, &A);
    show(128);                               // 명령어 fetch: 128 + 32768
    store32(15 * KB, 3000);                  // 스택의 x 초기값
    uint32_t x; load32(15 * KB, &x); x += 3; store32(15 * KB, x);   // x = x + 3
    uint32_t pa; translate(15 * KB, &pa);
    printf("  x = x + 3 실행 후: VA 15KB 의 값 = %u, 실제 저장 위치 PA %u (= %u KB)\n", x, pa, pa / KB);

    printf("\n[3] 컨텍스트 스위치 + 같은 VA 다른 PA\n");
    context_switch(&A, &B);
    store32(15 * KB, 7777);
    translate(15 * KB, &pa);
    printf("  B 가 VA 15KB 에 7777 저장 -> PA %u\n", pa);
    context_switch(&B, &A);
    load32(15 * KB, &x);
    printf("  A 로 돌아와 VA 15KB 읽기 -> %u (B 의 쓰기에 영향 없음)\n", x);

    printf("\n[4] 유저 모드에서 base 를 바꾸려 하면?\n");
    set_base_bounds(0, 64 * KB);

    printf("\n[5] B 의 bad load (VA 20KB)\n");
    context_switch(&A, &B);
    if (load32(20 * KB, &x) < 0) {
        cpu.mode = KERNEL;
        printf("  OS: out-of-bounds trap -> B 종료, B 의 16KB 를 free list 로 반환\n");
        B.alive = 0;
    }

    printf("\n[6] A 를 물리 48KB 로 옮기기 (deschedule -> memcpy -> PCB base 수정)\n");
    memcpy(&phys[48 * KB], &phys[A.base], A.bounds);
    A.base = 48 * KB;
    context_switch(NULL, &A);
    load32(15 * KB, &x);
    translate(15 * KB, &pa);
    printf("  A 는 모른 채 VA 15KB 를 읽음 -> %u (이제 PA %u)\n", x, pa);

    printf("\n총 fault 횟수: %d\n", faults);
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 code/C15_base_bounds.c -o .work/bin/C15_base_bounds && .work/bin/C15_base_bounds
[1] 원문 예제: 4KB 주소 공간을 물리 16KB 에 올림
  VA     0 ( 0.00 KB) -> PA 16384 (16.00 KB)
  VA  1024 ( 1.00 KB) -> PA 17408 (17.00 KB)
  VA  3000 ( 2.93 KB) -> PA 19384 (18.93 KB)
  VA  4400 ( 4.30 KB) -> FAULT (out of bounds, bounds=4096)

[2] Figure 15.1/15.2: 16KB 프로세스를 물리 32KB 에 재배치, x=3000 at VA 15KB
  [switch] -> A (base=32768, bounds=16384)
  VA   128 ( 0.12 KB) -> PA 32896 (32.12 KB)
  x = x + 3 실행 후: VA 15KB 의 값 = 3003, 실제 저장 위치 PA 48128 (= 47 KB)

[3] 컨텍스트 스위치 + 같은 VA 다른 PA
  [switch] -> B (base=16384, bounds=16384)
  B 가 VA 15KB 에 7777 저장 -> PA 31744
  [switch] -> A (base=32768, bounds=16384)
  A 로 돌아와 VA 15KB 읽기 -> 3003 (B 의 쓰기에 영향 없음)

[4] 유저 모드에서 base 를 바꾸려 하면?
  !! privileged-op exception: user mode 에서 base 변경 시도

[5] B 의 bad load (VA 20KB)
  [switch] -> B (base=16384, bounds=16384)
  OS: out-of-bounds trap -> B 종료, B 의 16KB 를 free list 로 반환

[6] A 를 물리 48KB 로 옮기기 (deschedule -> memcpy -> PCB base 수정)
  [switch] -> A (base=49152, bounds=16384)
  A 는 모른 채 VA 15KB 를 읽음 -> 3003 (이제 PA 64512)

총 fault 횟수: 2
```

(경고 0개.) 원문 숫자 32896, 47KB, 16KB/17KB/19384/Fault 가 모두 일치한다. [3] 에서 A 와 B 는 **같은 VA 15KB** 를 쓰지만 PA 는 48128 과 31744 로 다르다. [6] 은 "멈춘 프로세스는 쉽게 옮길 수 있다" 의 실물 — 프로세스 코드는 한 줄도 안 바뀌었다.

### 8.2 OSTEP 시뮬레이터 — relocation.py

```text
cd .tools/ostep-homework/vm-mechanism
python3 relocation.py -s 1 -c
```

```text
ARG seed 1
ARG address space size 1k
ARG phys mem size 16k

Base-and-Bounds register information:

  Base   : 0x0000363c (decimal 13884)
  Limit  : 290

Virtual Address Trace
  VA  0: 0x0000030e (decimal:  782) --> SEGMENTATION VIOLATION
  VA  1: 0x00000105 (decimal:  261) --> VALID: 0x00003741 (decimal: 14145)
  VA  2: 0x000001fb (decimal:  507) --> SEGMENTATION VIOLATION
  VA  3: 0x000001cc (decimal:  460) --> SEGMENTATION VIOLATION
  VA  4: 0x0000029b (decimal:  667) --> SEGMENTATION VIOLATION
```

손으로 검산: limit 290 이므로 261 만 통과. 261 + 13884 = **14145** ✔. 나머지(782, 507, 460, 667)는 모두 ≥ 290 → violation.

**숙제 Q2**: `-s 0 -n 10` 에서 모든 VA 가 합법이 되려면 bounds 는? 먼저 `-c` 없이 돌려 VA 들을 보면 최대가 929 다. 따라서 **bounds = 930** (합법 범위가 0 ~ bounds-1 이므로 929 + 1).

```text
python3 relocation.py -s 0 -n 10 -l 930 -c
```

```text
Virtual Address Trace
  VA  0: 0x00000308 (decimal:  776) --> VALID: 0x00003913 (decimal: 14611)
  VA  1: 0x000001ae (decimal:  430) --> VALID: 0x000037b9 (decimal: 14265)
  VA  2: 0x00000109 (decimal:  265) --> VALID: 0x00003714 (decimal: 14100)
  VA  3: 0x0000020b (decimal:  523) --> VALID: 0x00003816 (decimal: 14358)
  VA  4: 0x0000019e (decimal:  414) --> VALID: 0x000037a9 (decimal: 14249)
  VA  5: 0x00000322 (decimal:  802) --> VALID: 0x0000392d (decimal: 14637)
  VA  6: 0x00000136 (decimal:  310) --> VALID: 0x00003741 (decimal: 14145)
  VA  7: 0x000001e8 (decimal:  488) --> VALID: 0x000037f3 (decimal: 14323)
  VA  8: 0x00000255 (decimal:  597) --> VALID: 0x00003860 (decimal: 14432)
  VA  9: 0x000003a1 (decimal:  929) --> VALID: 0x000039ac (decimal: 14764)
```

10개 전부 VALID. (주의: `-l` 을 주면 시뮬레이터가 난수를 소비하는 순서가 바뀌어 base 가 12418 이 아니라 14611 − 776 = 13835 로 바뀌고, VA 목록도 한 칸 밀렸다(776 이 새로 들어오고 516 이 빠짐). 최댓값은 여전히 929 라 결론은 같다.)

**숙제 Q3**: `-s 1 -n 10 -l 100` 일 때 주소 공간이 물리 메모리(16KB = 16384)에 다 들어가는 최대 base 는? `base + limit ≤ 16384` → **base = 16284**.

```text
python3 relocation.py -s 1 -n 10 -l 100 -b 16284 -c
...
  VA  8: 0x00000060 (decimal:   96) --> VALID: 0x00003ffc (decimal: 16380)
  VA  9: 0x0000001d (decimal:   29) --> VALID: 0x00003fb9 (decimal: 16313)

python3 relocation.py -s 1 -n 10 -l 100 -b 16285 -c
Error: address space does not fit into physical memory with those base/bounds values.
Base + Limit: 16385   Psize: 16384
```

16284 는 되고 16285 는 시뮬레이터가 거부한다 — 경계 확인 완료. (VA 0~7 은 모두 100 이상이라 violation, 출력 일부 생략.)

## 9. 펌웨어 엔지니어의 눈으로

- **Cortex-R/M 의 MPU 가 바로 base & bounds 다.** SSD 컨트롤러 FW 에서 MPU region 을 "base 주소 + 크기 + 권한" 으로 잡아 스택 오버플로나 NULL 근처 접근을 잡았던 것 — 다만 MPU 는 **변환은 안 하고 검사만** 한다(PA = VA). base 를 더하는 재배치 기능이 빠진 bounds-only 버전.
- **컨텍스트 스위치 비용 비교**: base/bounds 는 레지스터 2개 저장·복원이 끝이다. RTOS 에서 태스크마다 MPU region 을 다시 프로그래밍하는 것과 같은 수준. 페이지 테이블로 가면 TLB flush/ASID 문제가 생긴다(Ch.19).
- **PCIe BAR 와 주소 윈도**: 디바이스의 BAR 는 "호스트 물리 주소 공간에서 이 디바이스가 시작하는 base + 크기" 이고, 엔드포인트 내부의 inbound/outbound **ATU(address translation unit)** 는 말 그대로 base 를 빼고 더하는 재배치 창이다. RF 칩셋 PCIe 디버깅에서 "윈도 밖 접근 → completion abort / UR" 은 bounds fault 의 하드웨어 버전.
- **IOMMU/DART 의 가장 단순한 형태도 이것**: 디바이스가 낸 DMA 주소가 허용 창(base..base+size) 밖이면 fault 를 내고 차단. 펌웨어 버그로 DMA 가 엉뚱한 곳을 쓰는 걸 막는 마지막 방어선.
- **AI 가속기**: 초기 가속기나 단순 NPU 는 커맨드 버퍼마다 "디바이스 메모리 base + 크기" 를 레지스터로 주고 범위 검사만 하는 구조가 흔하다. 컨텍스트 격리가 이 수준이면 한 작업이 다른 작업의 텐서를 덮어쓰는 것은 막지만, 희소한 주소 공간은 못 쓴다 — 그래서 GPU 는 결국 페이지 테이블로 갔다.

## 10. 면접 질문

### Q1. base & bounds 변환 과정을 설명하고, 하드웨어/OS 가 각각 무엇을 하는지 나눠 말하라.
<details>
<summary>답 보기</summary>

**하드웨어(MMU)**: 매 fetch/load/store 마다 `VA < bounds` 검사 후 `PA = VA + base`. 위반이면 예외를 일으키고 커널 모드로 전환해 OS 핸들러로 점프. base/bounds 를 바꾸는 명령은 **특권 명령**.
**OS**: 프로세스 생성 시 free list 에서 공간을 찾아 base/bounds 결정, 종료 시 회수, **컨텍스트 스위치 때 base/bounds 를 PCB 에 저장/복원**, 부팅 때 예외 핸들러 설치, 위반 프로세스 종료.
핵심 문장: "OS 는 세팅만 하고 빠지며, 변환은 하드웨어가 매번 한다 — LDE 의 메모리 버전".

</details>

### Q2. bounds 레지스터에 "크기" 를 넣는 방식과 "끝 물리 주소" 를 넣는 방식의 차이는?
<details>
<summary>답 보기</summary>

크기 방식: **VA 를 bounds 와 비교** 하고 동시에 base 를 더할 수 있다(비교가 덧셈 결과에 의존하지 않음) → 임계 경로가 짧다.
끝 주소 방식: **base 를 먼저 더한 PA 를 비교** 해야 하므로 덧셈 후 비교가 직렬. 논리적으로는 동등하다.
x86 세그먼트 limit, ARM MPU region size 는 크기 방식에 가깝다.

</details>

### Q3. 정적 재배치(로더가 주소를 고쳐 쓰는 방식)의 문제는?
<details>
<summary>답 보기</summary>

1. **보호 없음**: 프로그램이 계산으로 아무 주소나 만들 수 있다(포인터 산술).
2. **재배치 불가**: 로드 후엔 절대 주소가 코드/데이터 곳곳에 박혀 있어 옮기기 어렵다.
3. 로드 시간이 오래 걸린다(재배치 테이블 처리).
현대에도 PIC/PIE + 동적 링커의 재배치(GOT 패치)로 일부 남아 있지만, 보호는 MMU 가 맡는다.

</details>

### Q4. base & bounds 의 단점 두 가지와 다음 단계의 해법은?
<details>
<summary>답 보기</summary>

1. **내부 단편화**: 힙과 스택 사이 빈 공간도 물리 메모리를 차지한다. 64비트 주소 공간이면 불가능에 가깝다.
2. **주소 공간 전체가 연속이고 물리 메모리에 다 들어가야** 한다. 코드 공유도 안 된다.
해법: 논리 세그먼트(코드/힙/스택)마다 base/bounds 를 두는 **세그먼트**(Ch.16) → 외부 단편화가 생기니 고정 크기 **페이징**(Ch.18).

</details>

### Q5. 유저 프로세스가 base 레지스터를 바꿀 수 있다면 무슨 일이 생기나? 그럼 어떻게 막나?
<details>
<summary>답 보기</summary>

base 를 0 으로, bounds 를 최대로 바꾸면 **물리 메모리 전체(커널, 트랩 테이블, 다른 프로세스)를 읽고 쓸 수 있다** → 트랩 테이블을 덮어 커널 권한 탈취.
그래서 base/bounds 갱신은 **특권 명령** 이고, 유저 모드에서 실행하면 CPU 가 "illegal instruction" 예외를 일으킨다. 같은 원리로 페이지 테이블 base 레지스터(x86 CR3, ARM TTBR)도 특권 레지스터다.

</details>

## 11. 자가 점검 & 숙제

### 퀴즈 1. base = 20KB, bounds = 8KB 일 때 VA 5000, 8192, 8191 의 결과는?
<details>
<summary>답 보기</summary>

20KB = 20480.
- 5000 < 8192 → 20480 + 5000 = **25480**
- 8192 ≥ 8192 → **fault**
- 8191 < 8192 → 20480 + 8191 = **28671** (마지막 합법 바이트)

</details>

### 퀴즈 2. 한 명령어 `movl 0x0(%ebx), %eax` 를 실행하는 동안 MMU 는 몇 번 변환하나?
<details>
<summary>답 보기</summary>

**2번**: 명령어 fetch 1번 + 데이터 load 1번. (명령어가 여러 바이트에 걸쳐 있어도 개념적으로 fetch 한 번.)

</details>

### 퀴즈 3. 컨텍스트 스위치 때 base/bounds 를 저장하는 곳은? 프로세스가 실행 중이 아닐 때 이사하는 절차는?
<details>
<summary>답 보기</summary>

**PCB(프로세스 구조체)**. 이사: deschedule → 주소 공간을 새 위치로 복사 → PCB 의 base 를 새 위치로 갱신 → 다음에 스케줄될 때 새 base 가 복원된다.

</details>

### 퀴즈 4. relocation.py 에서 `-a 1k -p 16k` 이고 limit = 1024 일 때 base 의 최댓값은?
<details>
<summary>답 보기</summary>

base + limit ≤ 16384 → **15360**.

</details>

### 원문 Homework 중 꼭 해볼 것

- **Q1 (seed 1, 2, 3 손 변환)**: 위의 seed 1 처럼 `-c` 없이 먼저 풀고 검산 → **범위 검사 → 덧셈 순서** 를 손에 익히는 문제.
- **Q2 / Q3**: 위에서 풀었다 → **bounds 는 "최대 VA + 1"**, **base 의 최댓값은 "물리 크기 − limit"** 이라는 두 경계 감각.
- **Q5 (bounds 값에 따른 합법 비율 그래프)**: VA 가 0 ~ asize-1 에서 균등하게 뽑히므로 합법 비율 ≈ `bounds / asize` 인 직선이 나와야 한다. 여러 seed 로 확인해 **이론과 시뮬레이션이 맞는지** 보는 문제.

## 12. 다음으로

- 다음 장: [Ch.16 세그먼트](2026-09-30_C16_segmentation.md) — base/bounds 를 코드·힙·스택마다 하나씩 두면?
- 빈 슬롯을 찾는 free list 의 본격적인 이야기: [Ch.17 빈 공간 관리](2026-09-30_C17_free_space_management.md)
- 이전 장: [Ch.14 메모리 API](2026-09-30_C14_memory_api.md)
