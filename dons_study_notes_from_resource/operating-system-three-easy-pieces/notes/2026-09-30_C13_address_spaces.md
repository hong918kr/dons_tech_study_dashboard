# Ch.13 주소 공간 — 메모리 가상화의 출발점

> 📖 원문: [13. The Abstraction: Address Spaces](../book-md/C13_the_abstraction_address_spaces.md) · [PDF p.132](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=132) · ⏱️ 읽기 약 30분 · 🔗 선행: [Ch.04](2026-09-30_C04_process.md), [Ch.06](2026-09-30_C06_limited_direct_execution.md)

## 0. 한눈에 보기

- 옛날 컴퓨터는 물리 메모리를 그대로 프로그램에 줬다. 여러 프로그램을 동시에 메모리에 올려 두고(멀티프로그래밍, 시분할) 번갈아 돌리기 시작하면서 **보호(protection)** 와 **쉬운 프로그래밍 모델** 이 필요해졌다.
- OS가 내놓은 답이 **주소 공간(address space)**: "프로세스마다 0번지부터 시작하는, 나만의 큰 메모리"라는 착각(illusion). 안에는 코드 · 힙 · 스택이 있다.
- 프로그램이 보는 모든 주소는 **가상 주소(virtual address)** 이고, OS + 하드웨어가 매 접근마다 실제 **물리 주소(physical address)** 로 바꿔 준다.
- 가상 메모리(VM)의 세 목표: **투명성(transparency) · 효율(efficiency) · 보호(protection)**.

> **THE CRUX: HOW TO VIRTUALIZE MEMORY** — "How can the OS build this abstraction of a private, potentially large address space for multiple running processes (all sharing memory) on top of a single, physical memory?"
>
> → 물리 메모리는 하나뿐인데, 여러 프로세스 각각에게 "나만의, 잠재적으로 매우 큰" 주소 공간이라는 추상화를 어떻게 만들어 줄 것인가?

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 물리 메모리(physical memory) | 실제 DRAM 칩의 바이트 배열 | 512KB 짜리 RAM, 주소 0 ~ 524287 |
| 멀티프로그래밍(multiprogramming) | 여러 프로세스를 메모리에 올려 두고 I/O 대기 때 다른 걸 돌림 | 비싼 CPU를 놀리지 않기 |
| 시분할(time sharing) | 짧게 번갈아 돌려 여러 사용자가 동시에 쓰는 느낌 | 터미널 수십 대가 한 메인프레임 공유 |
| 주소 공간(address space) | 실행 중인 프로그램이 보는 메모리 전체 | 0 ~ 16KB 의 가상 메모리 |
| 코드(code) | 명령어가 있는 정적 영역 | `main()` 의 기계어 |
| 힙(heap) | `malloc()` / `new` 로 동적으로 받는 영역, 위쪽 주소로 자람 | 연결 리스트 노드 |
| 스택(stack) | 지역 변수 · 인자 · 리턴 주소, 아래쪽 주소로 자람 | 함수 호출 프레임 |
| 가상 주소(virtual address, VA) | 프로그램이 만들어 내는 주소 | `printf("%p")` 로 찍히는 값 |
| 물리 주소(physical address, PA) | 메모리 버스에 실제로 나가는 주소 | 프로세스 A의 VA 0 → PA 320KB |
| 투명성(transparency) | 가상화가 프로그램 눈에 안 보임 | 재컴파일 없이 어디든 로드 |
| 효율(efficiency) | 시간 · 공간 오버헤드가 작음 | TLB 같은 하드웨어 지원 |
| 보호(protection) / 격리(isolation) | 남의 메모리와 OS 메모리를 못 건드림 | 버그난 프로세스가 커널을 못 덮어씀 |
| 마이크로커널(microkernel) | OS 내부도 서로 격리 | Mach, seL4 |

## 2. 초기 시스템 (13.1)

처음에는 추상화가 거의 없었다. 물리 메모리 0번지부터 OS가 있었고(사실상 라이브러리), 그 뒤 64KB부터 끝까지를 **한 개의 프로그램** 이 통째로 썼다. 사용자 기대치가 낮았으니 OS 개발자는 편했다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <text x="130" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">Fig 13.1 초기: 프로그램 1개</text>
  <rect x="60" y="35" width="140" height="60" fill="none" stroke="currentColor"/>
  <text x="130" y="70" text-anchor="middle" fill="currentColor">Operating System</text>
  <rect x="60" y="95" width="140" height="220" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="130" y="200" text-anchor="middle" fill="currentColor">Current Program</text>
  <text x="130" y="218" text-anchor="middle" fill="currentColor" font-size="11">(code, data, ...)</text>
  <text x="54" y="40" text-anchor="end" fill="currentColor" font-size="11">0KB</text>
  <text x="54" y="99" text-anchor="end" fill="currentColor" font-size="11">64KB</text>
  <text x="54" y="315" text-anchor="end" fill="currentColor" font-size="11">max</text>

  <text x="480" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">Fig 13.2 멀티프로그래밍: 3개 공존 (512KB)</text>
  <rect x="410" y="35" width="140" height="35" fill="none" stroke="currentColor"/>
  <text x="480" y="57" text-anchor="middle" fill="currentColor">OS</text>
  <rect x="410" y="70" width="140" height="35" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="480" y="92" text-anchor="middle" fill="currentColor">(free)</text>
  <rect x="410" y="105" width="140" height="35" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="480" y="127" text-anchor="middle" fill="currentColor">Process C</text>
  <rect x="410" y="140" width="140" height="35" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="480" y="162" text-anchor="middle" fill="currentColor">Process B</text>
  <rect x="410" y="175" width="140" height="35" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="480" y="197" text-anchor="middle" fill="currentColor">(free)</text>
  <rect x="410" y="210" width="140" height="35" style="fill:var(--accent-soft);stroke:var(--accent)" stroke-width="2"/>
  <text x="480" y="232" text-anchor="middle" fill="currentColor" font-weight="bold">Process A</text>
  <rect x="410" y="245" width="140" height="70" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="480" y="285" text-anchor="middle" fill="currentColor">(free)</text>
  <g font-size="11" fill="currentColor" text-anchor="end">
    <text x="404" y="40">0KB</text><text x="404" y="75">64KB</text><text x="404" y="110">128KB</text>
    <text x="404" y="145">192KB</text><text x="404" y="180">256KB</text><text x="404" y="215">320KB</text>
    <text x="404" y="250">384KB</text><text x="404" y="318">512KB</text>
  </g>
  <text x="560" y="232" fill="currentColor" font-size="11" style="fill:var(--accent)">← A의 VA 0 = PA 320KB</text>
</svg>
```

## 3. 멀티프로그래밍과 시분할 (13.2)

기계가 비쌌기 때문에 놀리는 CPU 시간이 아까웠다. 그래서:

1. **멀티프로그래밍**: 여러 프로세스를 준비시켜 두고, 하나가 I/O 하러 가면 다른 걸 돌린다 → CPU 활용률 상승.
2. **시분할**: 배치(batch) 처리로 "디버그 한 번에 하루"가 되던 프로그래머들이 반발 → 여러 사람이 동시에 대화형으로 쓰기. 응답성(interactivity)이 중요해졌다.

시분할을 가장 무식하게 구현하면? 프로세스 하나에게 **메모리 전체** 를 주고, 바꿀 때마다 메모리 전체를 디스크에 저장하고 다음 프로세스 것을 읽어 온다. 레지스터 저장은 빠르지만 메모리 통째 저장은 끔찍하게 느리다.

계산으로 감을 잡아 보자.

```text
[원문 시대 감각] 메모리 512KB, 드럼/디스크 전송 1MB/s 라고 치면
  저장 0.5s + 복원 0.5s = 스위치 1번에 약 1초  → 타임슬라이스(수십 ms)보다 수십 배 느림

[현대 버전, 새 예제] 프로세스가 4GB 를 쓰고 NVMe SSD 가 3GB/s 라면
  4GB / 3GB/s ≈ 1.33s (저장) + 1.33s (복원) ≈ 2.7s
  타임슬라이스 10ms 대비 약 270배 → 말이 안 된다
```

그래서 결론: **프로세스들을 메모리에 그대로 둔 채로** 번갈아 돌리자(Fig 13.2). 그런데 여러 프로그램이 한 메모리에 같이 살면 새 문제가 생긴다 — **보호**. A가 B의 메모리(또는 OS)를 읽거나 쓰면 안 된다.

## 4. 주소 공간 (13.3)

사용자(프로그래머)를 위해 OS는 물리 메모리를 감싼 쉬운 추상화를 만든다. 그게 **주소 공간**: 실행 중인 프로그램의 시점에서 본 메모리 전부.

주소 공간 안의 세 주인공:

- **코드**: 명령어. 실행 중 크기가 변하지 않으니(정적) 맨 위(주소 0 쪽)에 박아 둔다.
- **힙**: `malloc()` 으로 받는 동적 메모리. 실행 중 커진다.
- **스택**: 함수 호출 체인 추적, 지역 변수, 인자, 리턴 값. 실행 중 커졌다 줄었다 한다.
- (그 외 전역 변수 같은 정적 데이터도 있지만 지금은 셋만 생각한다.)

커지는 게 두 개(힙, 스택)라서 **서로 반대쪽 끝에 놓고 마주 보며 자라게** 한다. 그래야 둘 다 최대한 자랄 여유가 생긴다.

```svg
<svg viewBox="0 0 640 360" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs>
    <marker id="C13-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" style="fill:var(--accent)"/>
    </marker>
  </defs>
  <text x="200" y="20" text-anchor="middle" fill="currentColor" font-weight="bold">Fig 13.3 16KB 주소 공간 예</text>
  <rect x="130" y="35" width="140" height="40" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="200" y="60" text-anchor="middle" fill="currentColor">Program Code</text>
  <rect x="130" y="75" width="140" height="40" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="200" y="100" text-anchor="middle" fill="currentColor">Heap</text>
  <rect x="130" y="115" width="140" height="185" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="200" y="212" text-anchor="middle" fill="currentColor">(free)</text>
  <rect x="130" y="300" width="140" height="40" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="200" y="325" text-anchor="middle" fill="currentColor">Stack</text>
  <line x1="200" y1="120" x2="200" y2="165" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C13-arrow)"/>
  <line x1="200" y1="295" x2="200" y2="250" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C13-arrow)"/>
  <g font-size="11" fill="currentColor" text-anchor="end">
    <text x="124" y="40">0KB</text><text x="124" y="80">1KB</text><text x="124" y="120">2KB</text>
    <text x="124" y="305">15KB</text><text x="124" y="343">16KB</text>
  </g>
  <g font-size="12" fill="currentColor">
    <text x="285" y="58">명령어. 정적이라 크기 고정</text>
    <text x="285" y="98">malloc 데이터</text>
    <text x="285" y="150">힙은 "아래로"(큰 주소 쪽으로) 자람</text>
    <text x="285" y="270">스택은 "위로"(작은 주소 쪽으로) 자람</text>
    <text x="285" y="325">지역 변수, 인자, 리턴 값</text>
  </g>
  <text x="200" y="358" text-anchor="middle" fill="currentColor" font-size="11">그림에서 "위/아래"는 페이지 방향. 주소로는 힙 ↑(증가), 스택 ↓(감소)</text>
</svg>
```

> 원문 그림은 0번지를 **위** 에 그려서 "힙이 아래로, 스택이 위로" 자란다고 말한다. 주소 값으로 말하면 **힙은 주소가 커지는 쪽, 스택은 주소가 작아지는 쪽** 이다. 면접에서는 주소 기준으로 말하는 게 안전하다.

이 배치는 관례일 뿐이다. 스레드가 여러 개면 스택도 여러 개가 필요하므로 이렇게 깔끔하게 나눌 수 없다(Ch.26 이후).

### 핵심: 가상 주소 ≠ 물리 주소

프로그램은 자기가 0 ~ 16KB 에 있다고 믿지만, 실제로는 물리 메모리 아무 데나 올라가 있다. Fig 13.2 에서 A는 320KB, B는 192KB, C는 128KB 에 있다.

- 프로세스 A 가 **가상 주소 0** 에서 load 하면, OS + 하드웨어는 이것을 **물리 주소 320KB** 로 보내야 한다.
- (새 예제) 프로세스 C 가 가상 주소 1000 을 읽으면 → 128KB + 1000 = 131072 + 1000 = **물리 132072**.
- (새 예제) 프로세스 B 가 가상 주소 70KB 를 읽으려 하면? B 의 영역은 64KB(192~256KB) 뿐이므로 **이 접근은 막혀야 한다** — 그대로 더하면 262KB = 빈 공간(혹은 남의 영역)을 건드린다. "막는 것" 이 바로 다음 장의 bounds 검사다.

"어떻게 바꾸는가" 는 다음 장들(Ch.15 base & bounds → Ch.16 세그먼트 → Ch.18 페이징)의 주제다.

> **TIP — 격리의 원칙(THE PRINCIPLE OF ISOLATION)**: 두 개체가 제대로 격리되어 있으면 하나가 망가져도 다른 하나는 멀쩡하다. OS는 프로세스끼리, 그리고 프로세스와 OS 사이를 메모리 격리로 떼어 놓는다. 마이크로커널은 한 발 더 나가 OS 내부 구성 요소끼리도 격리한다.

## 5. 가상 메모리의 목표 (13.4)

| 목표 | 뜻 | 이걸 못 지키면? |
|---|---|---|
| 투명성(transparency) | 프로그램은 가상화를 눈치채지 못하고, 자기만의 물리 메모리가 있는 것처럼 동작 | 프로그램마다 "어디에 로드될지" 신경 써야 함 |
| 효율(efficiency) | 시간(느려지지 않게) · 공간(관리 자료구조가 메모리를 너무 먹지 않게) 모두 | 모든 load/store 가 2배 느려지면 아무도 안 씀 |
| 보호(protection) | 남의 프로세스와 OS 를 load/store/명령어 fetch 어떤 방식으로도 못 건드림 | 한 프로그램 버그가 시스템 전체를 죽임 |

여기서 "투명하다"는 "다 드러난다"가 아니라 정반대, **"보이지 않는다"** 라는 뜻이다(원문 각주의 농담 포인트). 시간 효율을 위해서는 결국 **하드웨어 지원**(TLB 등)이 필수다.

## 6. 모든 주소는 가상이다 (ASIDE)

C 프로그램에서 포인터를 찍어 본 적이 있다면, 그건 전부 가상 주소다. 원문 예제(64-bit Mac OS X, 책이 쓰일 당시 x86):

```text
location of code : 0x1095afe50
location of heap : 0x1096008c0
location of stack : 0x7fff691aea64
```

코드 → 힙 → (아주 멀리) 스택 순서. 물리 메모리 어디에 있는지는 OS와 하드웨어만 안다. 아래 "직접 해보기"에서 지금의 Apple Silicon Mac 으로 다시 찍어 보면 그림이 조금 다르다.

## 7. 요약 (13.5)

- VM 시스템은 프로그램에게 **크고, 듬성듬성하고(sparse), 나만의(private)** 주소 공간이라는 착각을 준다.
- 매 메모리 참조마다 OS + 하드웨어가 가상 → 물리 변환을 하며, 프로세스끼리 · OS 를 보호한다.
- 이를 위해 저수준 **메커니즘** (주소 변환 하드웨어)과 **정책** (빈 공간 관리, 무엇을 내쫓을지)이 모두 필요하다. 다음 장들은 메커니즘부터 아래에서 위로 쌓는다.

## 8. 직접 해보기

이 장에는 OSTEP 시뮬레이터가 없다. 대신 내 Mac 에서 주소 공간 지도를 직접 그려 보고, `fork()` 로 "같은 가상 주소, 다른 물리 메모리"를 확인한다.

`code/C13_address_space.c`:

```c
// C13_address_space.c — 내 프로세스의 주소 공간 지도 그리기 + "모든 주소는 가상" 확인
// build: cc -Wall -Wextra -O0 code/C13_address_space.c -o .work/bin/C13_address_space
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>

int g_init = 42;        // .data  (초기값 있는 전역)
int g_zero;             // .bss   (0으로 초기화되는 전역)
const char *g_str = "hi"; // 문자열 리터럴은 읽기 전용 영역(__TEXT,__cstring)

static void deeper(int depth) {
    int local;
    printf("  stack  depth %d : %p\n", depth, (void *)&local);
    if (depth < 2) deeper(depth + 1);   // 재귀할수록 주소가 작아진다 = 스택은 아래로 자람
}

int main(void) {
    void *h1 = malloc(16);
    void *h2 = malloc(16);
    void *big = malloc(1 << 20);            // 1MB: macOS malloc은 큰 요청을 별도 영역에서 준다
    void *m = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    int x = 3;

    printf("[1] 주소 공간 지도 (pid %d)\n", getpid());
    printf("  code   main()   : %p\n", (void *)main);
    printf("  rodata \"hi\"     : %p\n", (void *)g_str);
    printf("  data   g_init   : %p\n", (void *)&g_init);
    printf("  bss    g_zero   : %p\n", (void *)&g_zero);
    printf("  heap   malloc#1 : %p\n", h1);
    printf("  heap   malloc#2 : %p\n", h2);
    printf("  heap   1MB      : %p\n", big);
    printf("  mmap   4KB anon : %p\n", m);
    printf("  stack  x        : %p\n", (void *)&x);
    deeper(0);

    // [2] 같은 가상 주소, 다른 값: fork 후 부모·자식이 &g_init 을 각각 고친다
    printf("\n[2] fork 후 같은 가상 주소 %p 에 서로 다른 값\n", (void *)&g_init);
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        g_init = 1111;
        printf("  child : &g_init=%p value=%d\n", (void *)&g_init, g_init);
        exit(0);
    }
    waitpid(pid, NULL, 0);
    g_init = 2222;
    printf("  parent: &g_init=%p value=%d\n", (void *)&g_init, g_init);

    free(h1); free(h2); free(big);
    munmap(m, 4096);
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 code/C13_address_space.c -o .work/bin/C13_address_space && .work/bin/C13_address_space \
    && echo "--- run 2" && .work/bin/C13_address_space | head -4
[1] 주소 공간 지도 (pid 61811)
  code   main()   : 0x1025f4548
  rodata "hi"     : 0x1025f4890
  data   g_init   : 0x1025fc000
  bss    g_zero   : 0x1025fc010
  heap   malloc#1 : 0x102c41970
  heap   malloc#2 : 0x102c41980
  heap   1MB      : 0x7ce800000
  mmap   4KB anon : 0x102614000
  stack  x        : 0x16d80abc4
  stack  depth 0 : 0x16d80ab68
  stack  depth 1 : 0x16d80ab38
  stack  depth 2 : 0x16d80ab08

[2] fork 후 같은 가상 주소 0x1025fc000 에 서로 다른 값
  child : &g_init=0x1025fc000 value=1111
  parent: &g_init=0x1025fc000 value=2222
--- run 2
[1] 주소 공간 지도 (pid 61819)
  code   main()   : 0x104210548
  rodata "hi"     : 0x104210890
  data   g_init   : 0x104218000
```

(Apple clang 21, macOS 26.4, arm64. 경고 0개.)

읽는 법:

- **코드 → rodata → data → bss** 는 실행 파일 이미지 순서대로 붙어 있다 (`0x1025f4...` ~ `0x1025fc...`). `g_init` 과 `g_zero` 가 정확히 16바이트 차이.
- **스택은 재귀할수록 주소가 작아진다**: depth 0 → 1 → 2 에서 `0x...b68 → b38 → b08`, 프레임당 0x30 = 48바이트씩 내려감. 원문 그림의 "스택은 위로 자란다(=작은 주소 쪽)"를 확인.
- **원문 그림과 다른 점**: 이 Mac 에서 스택(`0x16d80...`, 약 6GB 근처)은 1MB malloc 영역(`0x7ce800000`, 약 31GB 근처)보다 **아래** 에 있다. 64비트 주소 공간은 너무 넓어서 "코드-힙-…-스택" 이 한 줄로 마주 보는 그림은 개념도일 뿐이고, 실제 OS는 영역을 여기저기 흩어 둔다(그래서 Ch.16 의 "sparse address space" 이야기가 중요해진다).
- **ASLR**: 두 번째 실행에선 `main()` 이 `0x1025f4548 → 0x104210548` 로 바뀌었다. 하위 비트(`...548`)는 그대로, 상위가 통째로 밀렸다 = 이미지 전체를 다른 base 에 올렸다는 뜻. 프로그램은 전혀 몰라도 잘 돈다 → **투명성** 의 실물.
- **fork**: 부모와 자식이 **같은 가상 주소** `0x1025fc000` 에 서로 다른 값(1111 / 2222)을 갖는다. 가상 주소가 같아도 물리 메모리는 따로라는 결정적 증거 (실제로는 copy-on-write 로 쓰는 순간 갈라진다, Ch.23).

## 9. 펌웨어 엔지니어의 눈으로

- **SSD 컨트롤러 FW 는 대개 "Fig 13.1 세계"** 였다: Cortex-R 계열에서 링커 스크립트로 ITCM/DTCM/SRAM/DRAM 주소를 직접 박고, 가상 주소 없이 물리 주소로 돈다. 대신 보호는 **MPU 영역(region base + size + 권한)** 으로 했다 — 이게 다음 장의 base & bounds 를 영역 여러 개로 늘린 것(= Ch.16 세그먼트)과 거의 같은 발상이다.
- **"모든 주소는 가상"은 디바이스에도 적용된다**: Apple SoC 의 **DART(IOMMU)** 는 PCIe/코프로세서가 내보내는 주소를 다시 변환한다. RF 칩셋 디버깅 때 DMA 주소와 CPU 쪽 주소가 다르게 보이는 이유가 이것 — 디바이스마다 자기 "주소 공간" 을 가진 셈이다.
- **AI 가속기 / GPU** 는 컨텍스트(프로세스)마다 GPU 가상 주소 공간을 갖고, 유니파이드 메모리(Apple Silicon, CUDA UVM)는 CPU 와 GPU 가 같은 가상 주소를 공유하게 만든다. "같은 VA, 다른 PA" 와 반대로 "다른 장치, 같은 VA" 를 맞추는 문제다.
- **투명성 vs 결정성**: 펌웨어에선 ASLR 같은 무작위성이 디버깅을 어렵게 해서 꺼 두기도 한다. 범용 OS 는 보안을 위해 투명성을 이용해 주소를 섞는다. 같은 메커니즘, 다른 정책.

## 10. 면접 질문

### Q1. 프로세스의 주소 공간이란 무엇이고, 무엇으로 구성되는가?
<details>
<summary>답 보기</summary>

**실행 중인 프로그램이 보는 메모리 전체** 에 대한 OS 의 추상화다. 프로그램은 0번지부터 시작하는 자기만의 연속 메모리가 있다고 믿는다.
구성: **코드(text)**, 초기화된 전역(data), 0 초기화 전역(bss), **힙**(malloc, 큰 주소 쪽으로 성장), mmap 영역(공유 라이브러리, 익명 매핑), **스택**(스레드마다 하나, 작은 주소 쪽으로 성장), 그리고 커널 영역(유저 모드에서 접근 불가).
핵심은 이 모든 주소가 **가상 주소** 이고, MMU 가 매 접근마다 물리 주소로 바꾼다는 점.

</details>

### Q2. `printf("%p", &x)` 로 찍힌 값은 물리 주소인가?
<details>
<summary>답 보기</summary>

아니다. 유저 레벨에서 볼 수 있는 주소는 **전부 가상 주소** 다. 물리 위치는 OS(페이지 테이블)와 MMU 만 안다.
증거: `fork()` 후 부모와 자식이 **같은 주소에 다른 값** 을 가진다. 또 ASLR 때문에 실행할 때마다 값이 바뀌어도 프로그램은 정상 동작한다.
Linux 에선 `/proc/self/pagemap`(root 필요)으로 VA→PFN 을 볼 수 있지만, 보안상 일반 사용자에게는 막혀 있다.

</details>

### Q3. 가상 메모리의 세 가지 목표는? 각각 어떤 대가를 치르나?
<details>
<summary>답 보기</summary>

- **투명성**: 프로그램이 가상화를 몰라도 됨. 대가 → 모든 메모리 접근에 변환이 끼어든다.
- **효율**: 시간(변환이 빨라야 함 → **TLB** 등 하드웨어 필수)과 공간(페이지 테이블 같은 메타데이터가 작아야 함).
- **보호/격리**: 다른 프로세스와 커널을 load/store/fetch 로 건드릴 수 없음. 대가 → 권한 검사 하드웨어 + 위반 시 trap 처리.
세 목표는 서로 당긴다: 보호를 세밀하게 할수록 메타데이터(공간)가 늘고, 변환이 복잡해질수록 시간이 든다.

</details>

### Q4. 왜 메모리 전체를 디스크에 저장하는 방식으로 시분할을 하지 않나?
<details>
<summary>답 보기</summary>

레지스터 저장은 수백 바이트지만 메모리는 MB~GB 다. 예: 4GB 를 3GB/s SSD 에 쓰고 읽으면 스위치당 약 **2.7초**, 타임슬라이스 10ms 의 수백 배.
그래서 **여러 프로세스를 메모리에 동시에 상주** 시키고 레지스터만 바꾼다. 대신 그때부터 **보호** 와 **재배치(relocation)** 문제가 생기고, 이를 하드웨어 주소 변환으로 해결한다.

</details>

### Q5. 힙과 스택을 주소 공간의 양 끝에 두는 이유는? 멀티스레드에서는?
<details>
<summary>답 보기</summary>

둘 다 실행 중에 크기가 변하므로 **반대 방향으로 마주 보며 자라게** 해 남는 공간을 최대한 공유하려는 것. 어느 쪽이 얼마나 클지 미리 몰라도 된다.
멀티스레드면 **스레드마다 스택이 필요** 해서 이 그림이 깨진다. 실제로는 각 스레드 스택을 mmap 으로 따로 잡고 사이에 **guard page** 를 두어 오버플로를 잡는다. 64비트에서는 공간이 넉넉해 영역을 멀리 흩어 둔다.

</details>

## 11. 자가 점검 & 숙제

### 퀴즈 1. Fig 13.2 에서 프로세스 B(192KB 에 로드)가 가상 주소 2000 을 읽으면 물리 주소는?
<details>
<summary>답 보기</summary>

192KB = 196608. 196608 + 2000 = **198608**.

</details>

### 퀴즈 2. "투명한(transparent) 가상 메모리" 의 뜻은?
<details>
<summary>답 보기</summary>

**프로그램 입장에서 보이지 않는다** 는 뜻. 프로그램은 자신이 가상화되어 있다는 걸 모르고, 자기만의 물리 메모리가 있는 것처럼 동작한다. "다 공개된다" 는 일상적 의미와 반대.

</details>

### 퀴즈 3. 위 실행 결과에서 스택 프레임 하나의 크기는 몇 바이트였나? 스택은 어느 방향으로 자랐나?
<details>
<summary>답 보기</summary>

`0x16d80ab68 - 0x16d80ab38 = 0x30` = **48바이트**. 깊어질수록 주소가 **작아졌으므로** 스택은 낮은 주소 쪽으로 자란다.

</details>

### 퀴즈 4. 보호(protection)가 없으면 "OS 자체" 가 위험한 이유는?
<details>
<summary>답 보기</summary>

유저 프로그램이 OS 메모리, 예를 들어 **트랩 테이블(trap table)** 을 덮어쓰면 다음 시스템 콜/인터럽트 때 자기 코드를 커널 모드로 실행시킬 수 있다. 즉 기계 전체의 제어권을 빼앗긴다.

</details>

### 숙제 (원문 v0.91 에는 이 장 숙제가 없어서, macOS 에 맞춘 대체 과제)

- 위 프로그램을 3번 실행해 `main`, `malloc`, 스택 주소의 **어느 비트가 바뀌는지** 비교하기 → ASLR 이 무엇을 무작위화하는지 확인.
- 프로그램 끝에 `sleep(60)` 을 넣고 다른 터미널에서 `vmmap <pid>` 로 영역 목록을 보며, 출력한 주소가 어느 영역(`__TEXT`, `MALLOC_TINY`, `Stack` …)에 속하는지 대응시키기 → 주소 공간 = 영역들의 집합이라는 감각 만들기.
- `vm_stat` 로 시스템 전체 물리 페이지 사용을 보고, 큰 `malloc` 을 받기만 하고 안 쓸 때와 실제로 쓸 때(memset) 차이를 관찰 → "가상으로 받는 것"과 "물리 메모리를 쓰는 것"은 다르다(Ch.21 demand paging 예고).

## 12. 다음으로

- 다음 장: [Ch.14 메모리 API](2026-09-30_C14_memory_api.md) — `malloc`/`free` 를 쓰는 쪽에서 본 주소 공간과 흔한 버그.
- 그 다음: [Ch.15 주소 변환](2026-09-30_C15_address_translation.md) — "VA 0 → PA 320KB" 를 하드웨어가 실제로 어떻게 하는가(base & bounds).
- 원문 대화 장: [12. A Dialogue on Memory Virtualization](../book-md/C12_a_dialogue_on_memory_virtualization.md)
