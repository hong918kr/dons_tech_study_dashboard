# A3. 빌드와 도구 — 컴파일러 플래그부터 TSan까지

> **이 노트를 읽고 나면**
> - `cc -std=c11 -O2 -Wall -Wextra -pthread -o build/a.out main.c xxx.c -lm` 을 플래그 하나씩 읽고, "undefined symbol" 이 링커의 무슨 불만인지 말할 수 있다
> - `./main.sh` 가 무엇을 빌드하고 `main.c` 가 어떻게 PASS/FAIL 를 판정하는지 알고, 실패 한 줄에서 고칠 위치를 찾아간다
> - TSan / ASan / UBSan 출력을 한 줄씩 해석하고, 언제 어느 것을 켤지 고른다
>
> **선행**: 없음 (도구 쪽 출발점이다). 사용자 공간 기초는 [A4](A4_linux_userspace_basics.md)
> **이 개념을 쓰는 문제**: 10문제 전부. `./main.sh tsan` 은 [01_gps_fix_cache](../01_gps_fix_cache/question_note) · [04_temp_single_flight](../04_temp_single_flight/question_note), `./main.sh asan` 은 [06_config_publish_rollback](../06_config_publish_rollback/question_note).

---

## 1. 왜 이게 필요한가

동시성 버그는 **보이지 않는 게 기본**이다. 이게 이 노트의 존재 이유다.

두 스레드가 락 없이 같은 `long` 을 각각 10만 번 올리는 프로그램을 이 맥에서 `-O2` 로 돌렸더니
`counter = 200000 (기대 200000)` — **맞았다.** 락도 atomic도 없는데 맞았다. 같은 소스를
ThreadSanitizer 로 다시 빌드하면 `WARNING: ThreadSanitizer: data race` 가 나온다.

즉 **테스트 통과는 동시성의 증거가 아니다.** 증거를 만들어 주는 게 도구다. 그래서 이 연습 폴더는
`./main.sh tsan` / `./main.sh asan` 을 처음부터 갖고 있다.

하나 더. 컴파일러는 우리가 쓴 걸 그대로 옮기지 않는다. `-O2` 는 **기다리는 코드를 통째로 지울 수
있다** (§5). "내 코드가 맞나"를 따지기 전에 "내가 빌드한 게 내 코드인가"부터 알아야 한다.

## 2. 그림으로 먼저

`.c` 파일이 실행 파일이 되기까지 **네 단계**다. 단계마다 에러 메시지의 주인이 다르다 — 이게 에러를
읽는 첫 번째 열쇠다.

```svg
<svg viewBox="0 0 740 200" role="img" aria-label="C 빌드 4단계: 전처리, 컴파일, 어셈블, 링크">
  <rect class="box" x="10" y="20" width="130" height="56" rx="8"/><text x="75" y="44" text-anchor="middle">전처리</text>
  <text class="lbl" x="75" y="64" text-anchor="middle">cc -E</text><text class="lbl" x="75" y="98" text-anchor="middle">main.c</text>
  <rect class="box" x="175" y="20" width="130" height="56" rx="8"/><text x="240" y="44" text-anchor="middle">컴파일</text>
  <text class="lbl" x="240" y="64" text-anchor="middle">cc -S</text><text class="lbl" x="240" y="98" text-anchor="middle">main.i (569줄)</text>
  <rect class="box" x="340" y="20" width="130" height="56" rx="8"/><text x="405" y="44" text-anchor="middle">어셈블</text>
  <text class="lbl" x="405" y="64" text-anchor="middle">cc -c</text><text class="lbl" x="405" y="98" text-anchor="middle">main.s</text>
  <rect class="fill-soft" x="505" y="20" width="130" height="56" rx="8"/><text x="570" y="44" text-anchor="middle">링크</text>
  <text class="lbl" x="570" y="64" text-anchor="middle">cc 가 ld 를 부른다</text><text class="lbl" x="570" y="98" text-anchor="middle">.o 들 + libm</text>
  <line class="accent" x1="140" y1="48" x2="167" y2="48"/><polygon class="accent" points="167,44 175,48 167,52"/>
  <line class="accent" x1="305" y1="48" x2="332" y2="48"/><polygon class="accent" points="332,44 340,48 332,52"/>
  <line class="accent" x1="470" y1="48" x2="497" y2="48"/><polygon class="accent" points="497,44 505,48 497,52"/>
  <line class="accent" x1="635" y1="48" x2="672" y2="48"/><polygon class="accent" points="672,44 680,48 672,52"/><text class="lbl" x="686" y="44">a.out</text>
  <rect class="box" x="10" y="120" width="295" height="66" rx="8"/><text x="157" y="142" text-anchor="middle">한 파일씩 따로 본다</text>
  <text class="lbl" x="24" y="170">#include 펼치기 · 치환 · 타입 검사 · 최적화</text>
  <rect class="fill-soft" x="340" y="120" width="295" height="66" rx="8"/><text x="487" y="142" text-anchor="middle">전부 모아서 본다</text>
  <text class="lbl" x="354" y="170">U ↔ T 연결 + 라이브러리(-lm) 붙이기</text>
  <line class="dash" x1="322" y1="115" x2="322" y2="191"/>
</svg>
```

하네스가 **어떻게 정답을 알고 있는지**의 구조.

```
            main.c (주어짐, 수정 금지)
  ┌──────────────────────────────────────────────────────┐
  │ 가짜 벤더: usleep 지연 + xorshift32 고정 시드           │
  │ fix 를 만들 때마다 같은 값을 두 곳에 넣는다              │
  │   ① 내 코드가 받는 gps_read_fix() 반환값  ② truth[]     │
  └───────┬──────────────────────────────────┬───────────┘
          │ ①                                │ ② + 누적 거리
   ┌──────▼─────────┐              ┌─────────▼──────────┐
   │ 내 코드         │  결과 비교    │ ok() check_fix()   │
   │ gps_cache.c    │ ───────────▶ │ check_dist()       │
   └────────────────┘              └─────────┬──────────┘
                                    PASS / FAIL 한 줄씩
```

핵심: **기대값이 소스에 하드코딩돼 있지 않다.** 실행마다 값이 달라지지만 하네스는 그 실행에서 실제로
만들어진 값과 비교한다. 그래서 같은 코드가 맥에서도 리눅스에서도 통과한다.

## 3. 개념 (용어를 하나씩)

### 번역 단위 (translation unit)

전처리가 끝난 `.c` 파일 하나. **컴파일러가 한 번에 보는 세계의 전부**다. `gps_cache.c` 를 컴파일할 때
컴파일러는 `main.c` 를 전혀 보지 못한다. 헤더에 선언만 있으면 "이런 함수가 어딘가 있겠지" 하고 넘어간다.

### 심볼 (symbol)

함수·전역 변수의 이름이 오브젝트 파일에 남은 것. `T` 는 내가 **제공**하는 것, `U` 는 내가
**필요한** 것. `nm hello.o` 의 실제 출력:

```
0000000000000000 T _main
                 U _printf
```

`_main` 은 내가 만들었고 `_printf` 는 남이 줘야 한다 (앞의 `_` 는 macOS 관례).
### undefined symbol

링커가 `U` 의 짝을 못 찾았다는 에러. **컴파일 에러가 아니라 링크 에러다.**

```
Undefined symbols for architecture arm64:
  "_helper", referenced from:
      _main in u-76a9ab.o
ld: symbol(s) not found for architecture arm64
```

읽는 법: "`_helper` 가 필요한데 `u-...o` 의 `_main` 이 부른다. 근데 아무도 안 준다." 원인은 거의 네
가지다 — 구현을 안 썼다 / 구현한 `.c` 를 **빌드 명령에 안 넣었다** / 오타거나 `static` 을 붙여 파일
밖에서 안 보이게 했다 / 라이브러리 누락(`sqrt` + `-lm`).

### 명령 한 줄 해부

```sh
cc -std=c11 -O2 -Wall -Wextra -pthread -o build/a.out main.c gps_cache.c -lm
```

| 조각 | 뜻 | 빼면 어떻게 되나 |
|---|---|---|
| `cc` | 컴파일러 드라이버. 네 단계를 알아서 다 돌린다 | — |
| `-std=c11` | C11 로 해석. `stdatomic.h` 가 이것 때문에 쓸 수 있다 | `atomic_int` 를 못 알아본다 |
| `-O2` | 최적화 2단계. 제품 빌드가 쓰는 수준 | 느려지고, 지워지는 함정이 덜 보인다 |
| `-Wall` | 흔한 경고를 켠다 (이름이 all 이지만 전부는 아니다) | 초기화 안 한 변수를 조용히 넘어간다 |
| `-Wextra` | `-Wall` 이 안 켜는 것 (미사용 파라미터, 부호 비교) | `if (u < -1)` 같은 버그를 놓친다 |
| `-pthread` | 스레드를 쓴다고 알림. 매크로 정의 + 라이브러리 링크를 같이 한다 | 리눅스에서 `pthread_create` 가 undefined |
| `main.c gps_cache.c` | 컴파일할 번역 단위. 순서가 링크 순서 | 빠진 파일의 함수가 undefined |
| `-lm` | 수학 라이브러리. `sqrt`, `cos`, `isnan` | 리눅스에서 `_sqrt` undefined |

`-pthread` 와 `-lpthread` 는 다르다. `-pthread` 는 **컴파일 단계에도** 영향을 준다
(`_REENTRANT` 계열 매크로). 링크 플래그만 주면 안 된다.

### `-O2` 가 만드는 함정 (맛보기)

`-O2` 는 "같은 결과를 더 빠르게" 만들 권한을 컴파일러에 준다. 문제는 **"같은 결과"의 기준이 단일
스레드**라는 것. 다른 스레드가 변수를 바꿀 가능성은 (atomic/volatile 로 알려주지 않으면) 고려 대상이
아니다. 그래서 기다리는 루프가 사라진다. 메모리 순서는 [B6. atomic 과 메모리 순서](B6_atomics_and_memory_order.md).

## 4. 코드로 보기

### 단계를 손으로 돌려 보기

```c
#include <stdio.h>
#define GREET "hi"
int main(void) { printf("%s\n", GREET); return 0; }
```

```sh
cc -std=c11 -E hello.c | wc -l      # 569
cc -std=c11 -E hello.c | tail -1    # int main(void) { printf("%s\n", "hi"); return 0; }
```

3줄이 569줄이 됐다. `stdio.h` 가 펼쳐졌고 `GREET` 가 치환됐다. **이걸 안 보면?** — `./main.sh window` 가
넘기는 `-DGPS_WINDOW_US=1500000ull` 이 뭐로 치환됐는지 확인할 길이 없다.

```sh
cc -std=c11 -O2 -S hello.c -o hello.s && grep -n puts hello.s    # 17:	bl	_puts
```

`printf` 를 썼는데 어셈블리에는 `puts` 가 있다. `-O2` 가 **내 함수 호출을 다른 함수로 바꿨다.**
컴파일러가 이 정도까지 손댄다는 감각이 중요하다.

### `-Wall -Wextra` 가 잡아 주는 것

```c
int main(int argc, char **argv) {
    int n; unsigned u = 3;
    if (u < -1) printf("?\n");
    printf("%d\n", n);
    return 0;
}
```

```
w.c:5:11: warning: comparison of integers of different signs: 'unsigned int' and 'int' [-Wsign-compare]
w.c:6:20: warning: variable 'n' is uninitialized when used here [-Wuninitialized]
4 warnings generated.   (나머지 둘은 unused parameter argc / argv)
```

`if (u < -1)` 은 **참이다.** `-1` 이 `unsigned` 로 변환돼 4294967295 가 된다. 링버퍼 인덱스를 `unsigned`
로 두고 `if (idx < start - 1)` 을 쓰면 이 버그가 그대로 나온다. 대괄호 안의 이름이 **그 경고를 켠
플래그**다. 경고 0개가 이 문제 세트의 기본선이다.

## 5. 단계별로 만들어 보기 — `-O2` 가 기다림을 지운다

"다른 스레드가 준비되면 빠져나오는 루프"를 세 버전으로 이 맥에서 돌렸다. `setter` 는 200 ms 뒤에
`ready = 1` 을 쓴다. **정답은 약 200 ms** 다.

### v0 — 그냥 `int` (틀렸다)

```c
static int ready;
static void *setter(void *a) { (void)a; usleep(200000); ready = 1; return NULL; }

int main(void) {
    pthread_t t; double t0 = now_ms();
    pthread_create(&t, NULL, setter, NULL);
    while (!ready) { }                       /* 기다린다 */
    printf("%.1f ms 만에 탈출 (ready=%d)\n", now_ms() - t0, ready);
    pthread_join(t, NULL); return 0;
}
```

`-O2` 빌드는 `루프를 빠져나오는 데 0.0 ms 걸렸다 (ready=0)`, `-O0` 빌드는 `202.2 ms 걸렸다 (ready=1)`.
같은 소스인데 결과가 다르다. 어셈블리를 보면 `-O2` `_main` 에 루프가 **아예 없다**.

```
	bl	_pthread_create
	bl	_puts            ← ready 를 한 번도 읽지 않는다 (_pthread_join 이 바로 다음)
```

컴파일러의 논리: "이 스레드 안에 `ready` 를 바꾸는 코드가 없다 → 루프 도는 동안 안 변한다 → 한 번 읽어
0이면 영원히 0 → 무한루프는 관측 가능한 효과가 없으니 지워도 된다." 단일 스레드 관점에서는 **정당한
최적화**다.

### v1 — `volatile` (타이밍만 고쳐진다)

`static volatile int ready;` 로 바꾸면 `v1: 202.9 ms 만에 탈출`.

`volatile` 은 "매번 메모리에서 다시 읽어라"다. 루프가 살아남았다. 하지만 **최적화만 막는다.** 원자성도
스레드 간 순서 보장도 없다. 증거 — `volatile int stop` 을 쓴 예제는 TSan 에 여전히 잡힌다.

```
WARNING: ThreadSanitizer: data race (pid=8152)
  Write of size 4 at 0x0001026a8010 by main thread:
  Previous read of size 4 at 0x0001026a8010 by thread T1:
  Location is global 'stop' at 0x0001026a8010
```

`volatile` 은 동시성 도구가 아니다. 임베디드에서 MMIO 레지스터에 쓰는 것이고 스레드 사이에는 안 쓴다.

### v2 — `atomic` (맞다)

```c
static atomic_int ready;
/* 쓰기 */ atomic_store_explicit(&ready, 1, memory_order_release);
/* 읽기 */ while (!atomic_load_explicit(&ready, memory_order_acquire)) { }
```

`v2: 207.6 ms 만에 탈출`. 타이밍도 맞고 TSan 도 조용하다. `atomic` 은 컴파일러와 CPU 양쪽에 동시에 말을 건다 (무슨 약속인지는
[B6](B6_atomics_and_memory_order.md)). 가져갈 것은 **v0 과 v2 의 차이가 눈으로는 안 보이고, 빌드 플래그를 바꾸면 결과가 뒤집힌다**는 것.

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
|---|---|---|---|
| `-O0` 으로만 테스트 | 손으로는 통과, `./main.sh` 로는 FAIL | 최적화가 루프·읽기를 지우거나 재배치한다 | 문제 폴더의 `main.sh` 를 그대로 쓴다. 플래그를 낮추지 않는다 |
| 스레드 간 플래그를 `volatile` 로 | TSan 은 계속 경고, 드물게 값이 이상함 | `volatile` 은 최적화만 막는다 | `atomic_int` + `acquire`/`release` |
| `-lm` 누락 | 리눅스에서 `undefined symbol: sqrt` | `libm` 은 자동으로 안 붙는다 (맥은 libSystem 에 있어 안 나기도 함) | 명령 **끝**에 `-lm` |
| 구현한 `.c` 를 빌드 명령에 안 넣음 | `undefined symbol: _gps_get_last_fix` | 링커가 `U` 짝을 못 찾았다 | `main.sh` 의 `SRC` 를 확인. 새 파일이면 명령에 추가 |
| `printf` 로 경쟁 상태 디버깅 | 넣으면 사라지고 빼면 다시 남 | `printf` 는 내부 락 + syscall 이라 스케줄을 바꾼다 (Heisenbug) | TSan. 꼭 찍어야 하면 배열에 기록하고 끝에 한 번 출력 |
| TSan 빌드로 성능 측정, 또는 ASan 과 TSan 동시 사용 | "느려서 FAIL", 또는 빌드/실행 실패 | TSan 은 5~15배 느리다. 두 sanitizer 는 같은 런타임 자리를 다툰다 | 정확성은 TSan, 지연은 일반 `-O2`. `tsan` 다음 `asan`, 따로 |

## 7. 손으로 확인하기

### 하네스 읽는 법

```sh
cd 01_gps_fix_cache
./main.sh            # main.c + gps_cache.c          (내 코드)
./main.sh sol        # main.c + gps_cache_solution.c (모범답안)
./main.sh window     # -DGPS_WINDOW_US=1500000ull -DWINDOW_TEST 추가 (기본 sol)
./main.sh tsan       # -fsanitize=thread -g 추가 (기본 sol)
```

`main.sh` 가 하는 일은 세 줄이다 — `mkdir -p build`, 위의 `cc` 한 줄, `./build/a.out`. `set -e` 가 있어
컴파일이 실패하면 실행까지 가지 않는다. **먼저 `./main.sh sol` 로 "전부 PASS" 가 어떻게 생겼는지 봐
두는 게 순서다.** 실제 출력 끝부분:

```
== Thread safety: 4 readers for 2 s while the sampler writes ==
  no torn struct ever observed         PASS
  3 getter calls in a row: worst 73 us, 0 batches over 5000 us (vendor block 100000 us)
  getters never block on the module    PASS
ALL CHECKS PASSED (0 failed)
```

### 실패 메시지에서 고칠 위치 찾기

검사 함수는 세 개뿐이고 셋 다 `main.c` 에 있다. 출력 모양으로 어느 것인지 알 수 있다.

| 출력 모양 | 검사 함수 | 무엇을 봐야 하나 |
|---|---|---|
| `라벨   FAIL  <- 설명` | `ok()` | 불리언 조건 하나. 라벨 문자열로 `main.c` 를 grep 하면 조건이 나온다 |
| `라벨   got 194.6179  want 194.6179  FAIL` | `check_dist()` | 거리 계산. 허용 오차가 `1e-6 * (1 + fabs(want))` 이니 값이 비슷하면 누적 순서가 아니라 구간 선택이 틀린 것 |
| `FAIL  <- TORN / incoherent struct` | `check_fix()` | **동기화 문제.** 서로 다른 fix 의 필드가 섞였다 |
| `FAIL  <- wrong fix  got ts=... want ts=...` | `check_fix()` | 값은 온전한데 **어느 fix 를 골랐는지** 가 틀렸다 → 이진 탐색 경계 |
| `FAIL: only 3 fixes -- the sampler thread is not running` | `main()` 직접 | 스레드를 아예 안 띄웠거나 바로 죽었다 |

`TORN` 을 어떻게 아는지가 이 하네스의 영리한 부분이다. 가짜 벤더는 모든 fix 가
`lon == -(lat + 85.0)` 과 `hdop == 1.0 + 100.0*(lat - 37.0)` 을 만족하게 값을 만든다. 서로 다른 두
fix 의 필드가 섞이면 등식이 깨진다. **산수만으로 찢어진 읽기를 100% 잡는다.** 실패한 라벨로 위치를
찾는 습관을 들이자 — `grep -n "latest fix after" 01_gps_fix_cache/main.c`.

### 디버깅 도구 (이 맥에서 확인한 사정)

`printf` 디버깅의 요점은 하나다 — **`printf` 는 관찰이 아니라 개입이다.** 내부 락을 잡고 syscall 을
부르니 스레드 스케줄이 바뀌고 경쟁 상태가 숨는다.

- `gdb` 는 **설치돼 있지 않다.** macOS 의 표준 디버거는 `lldb` (`/usr/bin/lldb`) 다. 최소 절차 —
  `lldb ./build/a.out` → `run` → 죽으면 `bt` → `frame variable` → `print f->lat`. 스레드 전체는
  `thread backtrace all`, 중단점은 `breakpoint set -f gps_cache.c -l 42`. gdb 의 `break`/`print`/`bt`
  가 거의 그대로 대응된다.
- 코어덤프는 이 맥에서 **꺼져 있다** (`ulimit -c` 가 `0`). `ulimit -c unlimited` 로 켜면
  `/cores/core.PID` 에 떨어진다. 안 켜도 크래시 리포트는 `~/Library/Logs/DiagnosticReports/` 에 남고,
  리눅스 보드에서는 `/proc/sys/kernel/core_pattern` 을 본다.
- 디버거 없이도 종료 코드로 원인을 좁힌다. 널 포인터를 읽는 프로그램은 `exit=139` = `128 + 11`,
  SIGSEGV. `134` 면 SIGABRT (assert, double free), `136` 이면 SIGFPE.

### ThreadSanitizer 출력 한 줄씩

TSan 이 잡는 것은 **data race** 하나다. 정의: 두 스레드가 같은 주소를 접근하고 최소 하나가 쓰기이며
둘 사이에 동기화(락/atomic/스레드 생성·종료)가 없는 경우. §1 카운터 예제의 실제 출력:

```
WARNING: ThreadSanitizer: data race (pid=7714)
  Write of size 8 at 0x000104b30000 by thread T2:
    #0 bump race.c:8 (race_tsan:arm64+0x100000728)
  Previous write of size 8 at 0x000104b30000 by thread T1:
    #0 bump race.c:8 (race_tsan:arm64+0x100000728)
  Location is global 'counter' at 0x000104b30000 (race_tsan+0x100008000)
  Thread T2 (tid=213305, running) created by main thread at:
    #1 main race.c:15 (race_tsan:arm64+0x100000694)
SUMMARY: ThreadSanitizer: data race race.c:8 in bump
counter = 200000 (기대 200000)
ThreadSanitizer: reported 1 warnings
```

| 줄 | 뜻 | 여기서 얻을 것 |
|---|---|---|
| `WARNING: ... data race` | 경쟁을 하나 찾았다. 여러 개면 블록이 여러 번 | 개수보다 **첫 블록**부터 고친다 |
| `Write of size 8 ... by thread T2` | **지금** 잡힌 접근 | `size` 가 필드 크기와 맞는지 (8 = `long`/`double`/포인터) |
| `Previous write/read ... by T1` | **짝이 되는** 다른 스레드의 접근 | 두 줄을 같은 락으로 묶어야 한다 |
| `Location is global 'counter'` | 문제가 된 주소의 정체 | `global` / `heap block` / `stack of thread`. heap 이면 할당 위치도 나온다 |
| `Thread T2 ... created by main thread at` | 그 스레드를 누가 만들었나 | 어느 워커인지 식별 |
| `Mutex M12 ... created at` (이 예에는 없음) | 그 접근이 **잡고 있던** 락 | 두 접근의 Mutex 번호가 다르면 서로 다른 락 = 보호가 아니다 |
| `counter = 200000` | **프로그램 자체는 통과했다** | 결과가 맞아도 경쟁은 경쟁 |

함정: `Previous read` 쪽도 **똑같이 범인이다.** 읽는 쪽도 락을 잡아야 짝이 맞는다.

TSan 이 **못 잡는 것** — 실행되지 않은 코드 경로(정적 분석이 아니라 런타임 관찰이다) · 락은 다 걸었지만
두 구간으로 쪼개 중간에 값이 바뀌는 논리적 경쟁(check-then-act, TOCTOU) · condvar 를 빼먹어 영원히
기다리는 멈춤(락 순서 역전은 일부 감지한다). 느려지는 이유는 **모든 메모리 접근을 계측**하기 때문이다.
접근마다 그 주소를 최근에 누가 어떤 벡터 클록으로 만졌는지 기록하는 shadow memory 를 갱신한다.

이 맥에서 겪은 것 하나 — **TSan 아래에서 바쁜 대기(`while (!flag) {}`)를 돌리면 프로그램이 끝나지
않을 수 있다.** 위 v1/v2 예제가 그랬다. 문제 폴더 코드는 condvar 로 기다리니 괜찮다.

### AddressSanitizer / UBSan

ASan 이 잡는 것은 **메모리 오용**이다 — use-after-free, 버퍼 오버런, double free, 누수.
[06_config_publish_rollback](../06_config_publish_rollback/question_note) 이 이걸 노린 문제다. 포인터를
바꿔치기하며 옛 config 를 해제하는데 아직 그걸 읽는 reader 가 있다. refcount 를 빼먹으면 이게 나온다.

```sh
cd 06_config_publish_rollback
./main.sh asan          # -fsanitize=address -fno-omit-frame-pointer -g (기본 sol)
./main.sh asan mine     # 내 코드로
```

`free` 한 `struct Config` 를 읽는 8줄 프로그램의 실제 출력:

```
==8392==ERROR: AddressSanitizer: heap-use-after-free on address 0x6020000000f4 at pc 0x0001027508b8
READ of size 4 at 0x6020000000f4 thread T0
    #0 0x0001027508b4 in main uaf.c:10
0x6020000000f4 is located 4 bytes inside of 8-byte region [0x6020000000f0,0x6020000000f8)
freed by thread T0 here:
    #1 0x000102750854 in main uaf.c:9
previously allocated by thread T0 here:
    #1 0x000102750804 in main uaf.c:7
SUMMARY: AddressSanitizer: heap-use-after-free uaf.c:10 in main
```

세 개의 스택이 핵심이다 — **지금 읽은 곳**(`:10`), **해제한 곳**(`:9`), **할당한 곳**(`:7`). 06번에서 이게
나오면 "해제한 곳" 은 `release()` 나 publish 경로이고 "읽은 곳" 은 reader 다. refcount 가 0이 되기 전에
free 했다는 뜻이다. `4 bytes inside of 8-byte region` 은 오프셋이니 구조체 안 어느 필드인지 알려 준다.
아래 `Shadow bytes` 표는 넘어가도 된다 (`fd`=해제됨, `fa`=레드존, `00`=정상 인 내부 지도).

UBSan 은 **정의되지 않은 동작**을 잡는다 (`-fsanitize=undefined`) — 배열 범위 초과, 부호 있는 정수
오버플로, 과한 시프트, 정렬 위반, 널 역참조. 출력은 짧다.

```
ub.c:6:20: runtime error: index 8 out of bounds for type 'int[8]'
ub.c:7:22: runtime error: shift exponent 33 is too large for 32-bit type 'int'
```

ASan 과 UBSan 은 **같이 켤 수 있다** (`-fsanitize=address,undefined`). 링버퍼 인덱스와 버킷 계산이 많은
이 문제 세트에서 UBSan 은 값싸고 효과가 좋다. 반대로 TSan 과 ASan 은 같이 못 켠다.

### 언제 무엇을 쓰나

| 증상 | 먼저 쓸 것 | 이유 |
|---|---|---|
| 값이 가끔 틀리다 / `TORN` FAIL | **TSan** | 동기화 누락이 1순위 용의자 |
| 세그폴트, `free(): invalid pointer` | **ASan** | 메모리 오용을 세 스택으로 정확히 보여 준다 |
| 인덱스·시간 계산이 의심스럽다 | **UBSan** | 범위 초과·오버플로를 즉시 집어 준다. 거의 안 느리다 |
| 그냥 멈췄다 (출력 없음) | **디버거** — `lldb -p PID` → `thread backtrace all` | 데드락은 스택 몇 개로 바로 보인다. TSan 은 도움이 안 된다 |
| 지연/성능 FAIL | **일반 `-O2` 빌드** | sanitizer 빌드의 숫자는 의미가 없다 |

실전 순서는 **UBSan+ASan → TSan → 일반 빌드로 성능**. 메모리 버그가 있으면 TSan 출력이 쓰레기가 된다.

## 8. 자가 점검

```check
Q: `undefined symbol: _gps_get_last_fix` 가 나왔다. 컴파일 에러인가 링크 에러인가? 어디를 볼까?
A: 링크 에러다. 네 단계 중 마지막에서 나왔으니 컴파일은 전부 성공했다. 선언(헤더)은 있는데 몸통을
   아무 `.o` 도 제공하지 않는다. `gps_cache.c` 에 정의가 있는지, 이름/시그니처 오타가 없는지,
   `main.sh` 의 `cc` 명령에 그 파일이 들어 있는지를 본다.
```

```check
Q: `-O0` 에서 통과하고 `-O2` 에서 실패한다. 무엇을 먼저 의심하나?
A: 스레드 간 통신을 컴파일러에게 알려주지 않은 곳. 평범한 `int` 플래그를 다른 스레드가 바꾸길
   기다리는 코드가 대표적이다. `-O2` 는 그 변수가 안 변한다고 가정할 권한이 있어 읽기를 루프 밖으로
   올리거나 루프를 지운다. 실제로 이 맥에서 기다림이 사라져 0.0 ms 에 빠져나왔다. `atomic` 으로
   바꾸고 TSan 을 돌린다. 절대 `-O2` 를 내려서 "고치지" 않는다.
```

```check
Q: TSan 출력에 `Previous read of size 4 ... by thread T1` 이 있다. 읽기만 하는 T1 도 고쳐야 하나?
A: 그렇다. data race 는 두 접근의 짝으로 성립한다. 한쪽만 락을 잡으면 다른 쪽과 배타가 되지
   않으므로 보호가 아니다. 읽는 쪽도 같은 mutex 를 잡거나 양쪽 다 atomic 으로 접근해야 한다.
   블록에 Mutex 줄이 양쪽에 있는데 번호가 다르면 서로 다른 락을 잡은 것이므로 역시 경쟁이다.
```

```check
Q: `./main.sh` 가 `TORN / incoherent struct` 로 FAIL 했다. 자료구조 코드를 고쳐야 하나?
A: 아니다. 동기화 문제다. 가짜 벤더는 모든 fix 가 `lon == -(lat+85.0)` 과
   `hdop == 1.0+100.0*(lat-37.0)` 을 만족하게 값을 만든다. 등식이 깨졌다는 것은 서로 다른 두 fix 의
   필드가 섞였다는 뜻이다. 구조체 전체를 한 단위로 publish/읽기 하도록 mutex 나 seqlock 을 고친다.
```

```check
Q: 왜 이 하네스는 기대값을 하드코딩하지 않나? 대신 무엇을 하나?
A: 값이 타이밍에 의존하기 때문이다. 하드코딩하면 같은 코드가 플랫폼과 부하에 따라 깨진다(원본
   문제가 macOS 에서 깨진 이유다). 대신 가짜 벤더가 fix 를 내보낼 때마다 `truth[]` 에 같은 값과 누적
   거리를 기록하고 거기에 대고 비교한다. 또 `fake_gps_quiesce()` 로 벤더를 멈춰 정답을 얼려 두고
   필드를 하나씩 맞춰 본다.
```

## 9. 요약 카드

| 기억할 것 | 한 줄 |
|---|---|
| 4단계 | 전처리 `-E` → 컴파일 `-S` → 어셈블 `-c` → 링크. 에러가 어느 단계 것인지부터 본다 |
| undefined symbol | 링커가 `U` 짝을 못 찾음. 파일/구현/`-lm` 누락, 오타, 불필요한 `static` |
| `-pthread` | 링크 전용이 아니라 컴파일+링크 플래그. `-lpthread` 로 대체하지 않는다 |
| `-O2` | 다른 스레드를 모른다. 평범한 변수를 기다리는 루프는 지워질 수 있다 |
| `volatile` ≠ atomic | 최적화만 막는다. 스레드 사이에는 `atomic` |
| 하네스 | 기대값 하드코딩 없음, `truth[]` 와 비교. `TORN` = 동기화, `wrong fix` = 경계 조건 |
| TSan | data race 만. 5~15배 느림. `Write` + `Previous read` **양쪽** 이 범인 |
| ASan | use-after-free / 오버런. 세 스택(읽음·해제·할당). TSan 과 동시 사용 불가. 순서는 UBSan+ASan → TSan → 성능 |
| macOS 차이 | `gdb` 없음 → `lldb`. 코어덤프 기본 꺼짐 (`ulimit -c` = 0) |
