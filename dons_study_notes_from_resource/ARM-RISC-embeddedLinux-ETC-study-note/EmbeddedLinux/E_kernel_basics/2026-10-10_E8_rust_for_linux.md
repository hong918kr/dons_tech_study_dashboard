# E8. Rust for Linux — 현황, 바인딩, 직접 Rust 모듈 만들기

> **이 노트를 다 읽으면**: Rust가 커널에서 막아 주는 버그 종류(UAF·double free·data race·OOB)를 C 코드와 실제 컴파일 에러로 비교해 설명할 수 있다 · 6.18 트리에 Rust가 어디까지 들어왔는지(어떤 드라이버, 어떤 서브시스템 추상화)를 grep 숫자로 말할 수 있다 · bindgen → `bindings` → `kernel` crate → 드라이버로 이어지는 층과 `module!`·`Result`↔errno·pinning·`unsafe`/`SAFETY`를 코드로 짚을 수 있다 · Rust 커널을 직접 빌드해 QEMU에서 샘플과 내 모듈 `e8_hello`를 insmod하고, C 쌍둥이 모듈과 크기·빌드 시간을 비교할 수 있다
> **왜 필요한가**: 2025~2026년 Embedded Linux 면접에서 "Rust in the kernel, have you tried it? Would you use it?"는 단골 트렌드 질문이고, 답의 질은 "실제로 빌드해 봤는가"와 "우리 보드에서 지금 쓸 수 있는가를 판단할 수 있는가"로 갈린다
> **Don 기준 난이도**: 메모리 버그가 왜 무서운지(SSD 펌웨어의 DMA 버퍼 재사용, 테이블 OOB)는 몸으로 안다 / 새로 배울 부분은 Rust 문법 조금(소유권·빌림·`Result`·`?`), 커널 Rust 특유의 빌드 체계(LLVM, bindgen, rmeta), pinning
> **선행 노트**: E2(설정과 빌드), E3(커널 모듈), E5(커널 C 관용구), E6(유저 ↔ 커널 경계) — G3(platform driver), H(동시성) 노트와 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어에서 겪었던 최악의 버그를 떠올려 보자. 아마 이런 종류였을 것이다.

- DMA가 아직 버퍼에 쓰고 있는데 FTL 쪽 코드가 그 버퍼를 free 리스트에 돌려줬다 → 다른 커맨드가 그 버퍼를 받아 쓰고, 며칠 뒤 데이터가 깨진다 (**use-after-free**).
- 매핑 테이블 인덱스 계산이 경계 조건에서 하나 넘쳤다 → 옆 테이블을 덮어쓴다 (**out-of-bounds**).
- 인터럽트 핸들러와 메인 루프가 같은 카운터를 락 없이 고쳤다 → 아주 가끔 값이 틀린다 (**data race**).

셋 다 C 컴파일러는 경고 하나 없이 통과시키고, 테스트에서는 드물게만 터지고, 터지면 원인과 증상이 멀리 떨어져 있다. 리눅스 커널도 똑같은 C 코드라서 같은 종류의 버그가 계속 나온다. **Rust for Linux**는 이 중 상당수를 "런타임에 운 좋게 잡는 것"에서 "컴파일 때 거절하는 것"으로 옮기려는 프로젝트다.

다만 커널 전체를 Rust로 다시 쓰는 것이 아니다. **C 커널은 그대로 두고, 그 위에 Rust로 드라이버를 쓸 수 있는 층을 얹는다.** 그림 1이 이 노트 전체의 지도다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <rect x="20" y="20" width="430" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="235" y="44" text-anchor="middle" font-size="14" font-weight="bold">Rust 드라이버 / 모듈</text>
  <text x="235" y="64" text-anchor="middle" font-size="12">rnull, ax88796b_rust, qt2025, nova-core, binder, e8_hello</text>
  <text x="235" y="81" text-anchor="middle" font-size="12">목표: safe Rust만 쓴다 (unsafe 0개가 이상적)</text>
  <rect x="20" y="120" width="430" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="235" y="144" text-anchor="middle" font-size="14" font-weight="bold">kernel crate — rust/kernel/</text>
  <text x="235" y="164" text-anchor="middle" font-size="12">Mutex, KVec, Arc, Error, platform::Driver, MiscDevice ...</text>
  <text x="235" y="181" text-anchor="middle" font-size="12">unsafe + // SAFETY: 로 안전한 API를 감싸 만든다</text>
  <rect x="20" y="220" width="250" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="145" y="244" text-anchor="middle" font-size="14" font-weight="bold">bindings crate</text>
  <text x="145" y="264" text-anchor="middle" font-size="12">bindgen이 C 헤더에서 자동 생성</text>
  <text x="145" y="281" text-anchor="middle" font-size="12">bindings_generated.rs</text>
  <rect x="290" y="220" width="160" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="370" y="244" text-anchor="middle" font-size="14" font-weight="bold">helpers</text>
  <text x="370" y="264" text-anchor="middle" font-size="12">rust/helpers/*.c</text>
  <text x="370" y="281" text-anchor="middle" font-size="12">inline·매크로 감싸기</text>
  <rect x="20" y="320" width="430" height="70" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="235" y="344" text-anchor="middle" font-size="14" font-weight="bold">C 커널 (그대로)</text>
  <text x="235" y="364" text-anchor="middle" font-size="12">mutex_lock(), krealloc(), _printk(), struct device ...</text>
  <text x="235" y="381" text-anchor="middle" font-size="12">Rust를 위해 C 코드를 바꾸지 않는다</text>
  <line x1="235" y1="90" x2="235" y2="118" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="145" y1="190" x2="145" y2="218" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="370" y1="190" x2="370" y2="218" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="145" y1="290" x2="145" y2="318" stroke="currentColor" marker-end="url(#ah1)"/>
  <line x1="370" y1="290" x2="370" y2="318" stroke="currentColor" marker-end="url(#ah1)"/>
  <text x="565" y="30" text-anchor="middle" font-size="13" font-weight="bold">unsafe 블록 수 (6.18 grep)</text>
  <rect x="470" y="40" width="190" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="565" y="61" text-anchor="middle" font-size="12">phy 2개: 0 / 237줄</text>
  <text x="565" y="79" text-anchor="middle" font-size="12">nova-core: 12 / 5,805줄</text>
  <rect x="470" y="120" width="190" height="70" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="565" y="144" text-anchor="middle" font-size="12">rust/kernel/</text>
  <text x="565" y="162" text-anchor="middle" font-size="12">1,400 / 41,443줄</text>
  <text x="565" y="180" text-anchor="middle" font-size="12">SAFETY 주석 1,632개</text>
  <rect x="470" y="220" width="190" height="70" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="565" y="244" text-anchor="middle" font-size="12">여기서부터는 전부</text>
  <text x="565" y="262" text-anchor="middle" font-size="12">unsafe extern "C"</text>
  <text x="565" y="280" text-anchor="middle" font-size="12">(raw pointer 세계)</text>
  <text x="565" y="345" text-anchor="middle" font-size="12">위로 갈수록 unsafe가</text>
  <text x="565" y="363" text-anchor="middle" font-size="12">줄어드는 것이 설계 목표</text>
</svg>
```
그림 1 — Rust 드라이버는 C 함수를 직접 부르지 않고 `kernel` crate의 안전한 API만 쓴다. `kernel` crate는 bindgen이 C 헤더에서 자동으로 만든 `bindings`(전부 `unsafe`)를 `unsafe` 블록 + `// SAFETY:` 주석으로 감싸서 안전한 타입으로 바꾼다. 오른쪽 숫자는 이 노트 §2에서 6.18.55 소스를 직접 grep한 값이다.

펌웨어 비유로 말하면 이렇다. 벤더 HAL(C, 레지스터를 raw pointer로 만짐)이 맨 아래에 있고, 그 위에 팀이 "잘못 쓸 수 없는" 드라이버 라이브러리를 만들어 두고, 애플리케이션 코드는 그 라이브러리만 쓰게 하는 구조다. 차이는 그 "잘못 쓸 수 없음"을 **코드 리뷰가 아니라 컴파일러가 강제한다**는 점이다.

이 노트에서 실제로 돌려 보는 것은 다음과 같다.

| 절 | 내용 | 예제 |
|---|---|---|
| §1 | 왜 Rust인가 — C와 Rust로 같은 버그를 써 보기 | 예제 1–4 |
| §2 | 역사와 6.18 현황 — 트리를 grep으로 조사 | 예제 5–6 |
| §3 | 동작 원리 — `rust/`, bindgen, helpers, `kernel` crate, `module!`, errno, pinning, `unsafe` | 예제 7–9 |
| §4 | Rust 커널 빌드 (LLVM=1, CONFIG_RUST) | 예제 10–11 |
| §5 | 실습 1 — `samples/rust`(minimal, print, platform 드라이버)를 QEMU에서 insmod | 예제 12 |
| §6 | 실습 2 — 내 모듈 `e8_hello`, `e8_fail` | 예제 13–15 |
| §7 | 실습 3 — 같은 모듈 C vs Rust: `.ko` 크기와 빌드 시간 | 예제 16 |
| §8 | 실습 4 — 커널 모듈에서 컴파일 타임에 잡히는 버그 | 예제 17 |

> 실행 환경: 모든 출력은 Lima VM `elinux`(aarch64, 호스트 커널 `uname -r` = `7.0.0-38-generic`) 안에서 실제로 돌린 결과다. 커널은 랩 소스 `/var/lab/src/linux-6.18.55`를 `O=/var/tmp/E8/build`로 **clang/LLVM + CONFIG_RUST**로 따로 빌드했고(랩의 `out/Image`는 GCC 빌드라 Rust가 꺼져 있다 — §4), QEMU `virt`(cortex-a57, 512 MB, TCG)에서 랩 BusyBox initramfs에 `.ko`를 넣어 부팅했다. 툴체인: rustc 1.93.1, bindgen 0.72.1, clang 21.1.8. 타임스탬프·주소는 실행마다 다르다.

---

## 1. 왜 커널에 Rust인가

### 1.1 메모리 안전 버그 — 이름부터 정리

"메모리 안전(memory safety)"은 **프로그램이 자기가 가질 권리가 있는 메모리만, 살아 있는 동안만, 올바른 타입으로 읽고 쓴다**는 성질이다. 이것을 깨는 버그를 메모리 안전 버그라고 부른다. 커널에서 자주 보는 것은 다음과 같다.

| 버그 | 한 줄 정의 | 커널에서의 전형적 장면 | Don의 펌웨어 경험 |
|---|---|---|---|
| use-after-free (UAF) | 해제한 메모리를 포인터로 계속 쓴다 | `kfree()` 뒤에 timer·workqueue 콜백이 그 구조체를 만진다 | DMA 진행 중인 버퍼를 free 리스트에 반환 |
| double free | 같은 메모리를 두 번 해제한다 | 에러 경로와 정상 경로가 둘 다 `kfree()` | 에러 처리 루틴이 버퍼를 두 번 반환 |
| out-of-bounds (OOB) | 배열 끝을 넘어 읽고 쓴다 | 유저가 준 길이를 검사 없이 `memcpy` | 매핑 테이블 인덱스 off-by-one |
| data race | 두 실행 흐름이 락 없이 같은 메모리를 동시에 쓰고, 하나 이상이 쓰기 | 인터럽트 핸들러와 process context가 같은 필드를 수정 | ISR과 메인 루프가 공유 카운터 수정 |
| 초기화 안 된 메모리 | 쓰기 전에 읽는다 | 스택 구조체 일부를 `copy_to_user`로 유출 | 부팅 직후 SRAM 쓰레기 값 사용 |
| NULL dereference | NULL 포인터를 따라간다 | `probe()` 실패 후 남은 포인터 사용 | 초기화 전 핸들 사용 |

C 커널은 이 버그들을 **런타임 도구**로 잡는다: KASAN(주소 검사), KCSAN(data race 탐지), lockdep(락 순서), KMSAN(초기화 안 된 메모리), 퍼저 syzkaller. 전부 훌륭하지만 "그 경로가 테스트 중에 실제로 실행돼야" 잡는다. Rust는 상당수를 **컴파일 단계에서 아예 거절**한다. 이 차이가 핵심이다.

### 1.2 숫자로 보면 — 조심해서 인용하기

면접에서 숫자를 말할 때는 출처를 정확히 해야 한다. 이 노트에서 확인한 것만 적는다.

- Google Android 팀 보안 블로그(2024년 9월, "Eliminating Memory Safety Vulnerabilities at the Source"): Android 전체 취약점 중 메모리 안전 취약점 비율이 **2019년 76% → 2024년 24%**로 내려갔다. 새 코드를 메모리 안전 언어(Rust, Java/Kotlin)로 쓰기 시작한 효과라는 분석이다. **주의**: 이것은 Android 플랫폼 전체 수치이지 리눅스 커널만의 수치가 아니고, 2024년 값은 연말까지 외삽한 값이라고 블로그 스스로 밝힌다.
- 커널 쪽 사건: 2025년 12월 Maintainers Summit(도쿄)에서 "Rust 실험(experiment)"을 **성공으로 결론**짓고, 문서에서 "experimental" 표기를 떼는 패치("rust: conclude the Rust experiment")가 나왔다(LWN 보도). 즉 2026년 현재 Rust는 "언젠가 빠질 수도 있는 실험"이 아니라 커널의 정식 구성 요소다. 다만 Miguel Ojeda(Rust for Linux 리더) 본인이 "모든 설정·아키텍처·툴체인에서 다 된다는 뜻은 아니다"라고 단서를 달았다.

"커널 CVE의 몇 %가 메모리 안전 버그다" 같은 커널 전용 수치는 출처마다 집계 방식이 달라서 여기서는 쓰지 않는다. 면접에서는 "업계에서 흔히 인용되는 숫자는 C/C++ 대형 코드베이스에서 심각한 보안 버그의 대략 70% 안팎이 메모리 안전 문제라는 것이고, Android는 Rust 도입 후 비율이 크게 줄었다고 발표했다" 정도로 정성적으로 말하는 것이 안전하다.

### 1.3 예제 1 — C로 쓴 data race: 경고 0개, 결과는 틀린다

무엇을 확인하나: 두 스레드가 락 없이 같은 카운터를 올리는 코드가 `-Wall -Wextra`에서 경고 없이 컴파일되고, 실행하면 업데이트를 잃어버리는 것을 본다.

```c
/* e8: the same unlocked counter in C: compiles clean, loses updates */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static volatile unsigned long count;

static void *worker(void *arg)
{
	(void)arg;
	for (int i = 0; i < 50000000; i++)
		count++;              /* load, add, store: not atomic */
	return NULL;
}

int main(void)
{
	pthread_t t;
	if (pthread_create(&t, NULL, worker, NULL) != 0) {
		perror("pthread_create");
		exit(1);
	}
	for (int i = 0; i < 50000000; i++)
		count++;
	pthread_join(t, NULL);
	printf("count = %lu (expected 100000000)\n", count);
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'mkdir -p /tmp/E8 && cd /tmp/E8 && gcc -std=gnu11 -Wall -Wextra -O2 -pthread /Users/donh/.elinux_scratch/E8/e8_race/race.c -o race_c && echo "gcc: 0 warnings"; for i in 1 2 3; do ./race_c; done'
```

```text
gcc: 0 warnings
count = 66287413 (expected 100000000)
count = 50912903 (expected 100000000)
count = 67240908 (expected 100000000)
```

출력에서 볼 것: 컴파일러는 아무 말도 하지 않았고, 결과는 매번 다르게 틀린다(값은 실행마다 다르다). `volatile`은 "레지스터에 캐시하지 마라"일 뿐 원자성과 무관하다는 것도 확인된다 — Don이 펌웨어에서 이미 아는 함정이다. 처음에 반복 횟수를 100만 번으로 했을 때는 두 번 다 정확히 2000000이 나왔다(스레드가 겹칠 틈이 없었음). **"테스트에서 안 터졌다"가 "버그가 없다"가 아니라는 증거**다.

### 1.4 예제 2 — 같은 코드를 Rust로: 컴파일이 거절된다

무엇을 확인하나: 같은 패턴을 Rust로 쓰면 rustc가 빌림 규칙(borrow rules) 위반으로 컴파일을 거절하는 것을 본다. 커널이 아닌 유저 공간 Rust지만 규칙은 똑같다.

```rust
// e8: two threads bump the same counter without a lock (C would compile this)
use std::thread;

fn main() {
    let mut count: u64 = 0;
    let t = thread::spawn(|| {
        for _ in 0..1000 {
            count += 1; // writer 1 (other thread)
        }
    });
    for _ in 0..1000 {
        count += 1; // writer 2 (main thread)
    }
    t.join().unwrap();
    println!("count = {count}");
}
```

```sh
limactl shell elinux -- bash -lc 'cd /tmp/E8 && rustc --edition 2021 /Users/donh/.elinux_scratch/E8/e8_race/race_bad.rs -o race_bad'
```

```text
error[E0373]: closure may outlive the current function, but it borrows `count`, which is owned by the current function
  --> /Users/donh/.elinux_scratch/E8/e8_race/race_bad.rs:6:27
   |
 6 |     let t = thread::spawn(|| {
   |                           ^^ may outlive borrowed value `count`
 7 |         for _ in 0..1000 {
 8 |             count += 1; // writer 1 (other thread)
   |             ----- `count` is borrowed here
   |
...
error[E0503]: cannot use `count` because it was mutably borrowed
  --> /Users/donh/.elinux_scratch/E8/e8_race/race_bad.rs:12:9
   |
...
11 |       for _ in 0..1000 {
12 |           count += 1; // writer 2 (main thread)
   |           ^^^^^^^^^^ use of borrowed `count`
...
error[E0502]: cannot borrow `count` as immutable because it is also borrowed as mutable
...
error: aborting due to 3 previous errors

Some errors have detailed explanations: E0373, E0502, E0503.
For more information about an error, try `rustc --explain E0373`.
```

(출력이 길어서 `...`로 줄였다.) 출력에서 볼 것: 에러가 세 개다. E0373은 "다른 스레드가 이 함수보다 오래 살 수 있는데 지역 변수를 빌려 갔다"(스택 변수 UAF의 씨앗), E0503은 "다른 쪽이 `&mut`로 빌려 간 동안 내가 또 썼다"(data race 그 자체). 규칙은 하나다: **`&mut`(쓰기 가능한 빌림)는 한 번에 하나만 존재할 수 있다.** 두 스레드가 동시에 쓸 수 있는 구조 자체가 타입 오류다.

컴파일러가 이끄는 고친 코드는 "공유 소유(`Arc`) + 락(`Mutex`)"이다.

```rust
// e8: the fix the compiler pushes you toward: shared ownership + lock
use std::sync::{Arc, Mutex};
use std::thread;

fn main() {
    let count = Arc::new(Mutex::new(0u64));
    let c2 = Arc::clone(&count);
    let t = thread::spawn(move || {
        for _ in 0..100_000 {
            *c2.lock().unwrap() += 1;
        }
    });
    for _ in 0..100_000 {
        *count.lock().unwrap() += 1;
    }
    t.join().unwrap();
    println!("count = {}", *count.lock().unwrap());
}
```

```text
count = 200000
```

출력에서 볼 것: 결과가 정확하다. 핵심은 **`Mutex<u64>` 안의 값은 `lock()`이 돌려주는 guard를 통해서만 만질 수 있다**는 점이다. C에서는 "이 필드는 `foo_lock`으로 보호됨"이 주석일 뿐이지만, Rust에서는 타입이다. 커널 Rust의 `kernel::sync::Mutex<T>`도 같은 모양이다(§3.4).

### 1.5 예제 3 — use-after-free도 컴파일 에러

무엇을 확인하나: 힙 버퍼 안을 가리키는 참조를 들고 있는 채로 버퍼를 해제하면 rustc가 거절한다.

```rust
// e8: keep a pointer into a buffer, then free the buffer (classic UAF)
fn main() {
    let buf: Vec<u8> = vec![0xAA; 64];
    let first: &u8 = &buf[0]; // borrow into the heap buffer
    drop(buf);                // free it (kfree in C)
    println!("first = {first:#x}"); // use after free
}
```

```text
error[E0505]: cannot move out of `buf` because it is borrowed
 --> /Users/donh/.elinux_scratch/E8/e8_race/uaf_bad.rs:5:10
  |
3 |     let buf: Vec<u8> = vec![0xAA; 64];
  |         --- binding `buf` declared here
4 |     let first: &u8 = &buf[0]; // borrow into the heap buffer
  |                       --- borrow of `buf` occurs here
5 |     drop(buf);                // free it (kfree in C)
  |          ^^^ move out of `buf` occurs here
6 |     println!("first = {first:#x}"); // use after free
  |                        ----- borrow later used here
  |
help: consider cloning the value if the performance cost is acceptable
...
error: aborting due to 1 previous error
```

출력에서 볼 것: 에러 메시지가 UAF의 세 요소(빌림 시작 4행 → 해제 5행 → 이후 사용 6행)를 정확히 짚는다. 규칙: **빌림(reference)은 빌려 온 대상보다 오래 살 수 없다.** C의 `kfree(p); p->x = 1;`은 같은 정보를 가지고도 컴파일러가 막지 않는다.

### 1.6 예제 4 — OOB는 런타임 검사: "조용한 오염" 대신 "즉시 정지"

무엇을 확인하나: 인덱스 범위 초과는 컴파일 타임에 다 알 수 없으므로 Rust는 **실행 중에 검사**하고, 넘으면 panic한다.

```rust
// e8: out-of-bounds index is checked at runtime -> panic, not silent corruption
fn main() {
    let regs = [0u32; 4];
    let idx = std::env::args().count() + 3; // 4 when run with no args
    println!("reading regs[{idx}]");
    println!("{}", regs[idx]);
}
```

```text
reading regs[4]

thread 'main' (495397) panicked at /Users/donh/.elinux_scratch/E8/e8_race/oob.rs:6:20:
index out of bounds: the len is 4 but the index is 4
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace
exit=101
```

출력에서 볼 것: 옆 메모리를 읽는 대신 그 자리에서 멈췄다(스레드 ID는 실행마다 다르다). **커널 안에서는 Rust panic이 어떻게 되나?** `rust/kernel/lib.rs`의 panic handler가 메시지를 `pr_emerg!`로 찍고 `BUG()`를 부른다 — 즉 oops다. 조용한 메모리 오염보다는 낫지만 여전히 커널 크래시이므로, 커널 Rust 코드는 인덱스 대신 `get()`(→ `Option`)을 쓰거나 범위를 먼저 검사하는 것이 관례다.

```rust
#[panic_handler]
fn panic(info: &core::panic::PanicInfo<'_>) -> ! {
    pr_emerg!("{}\n", info);
    // SAFETY: FFI call.
    unsafe { bindings::BUG() };
}
```

### 1.7 규칙 하나가 버그 하나를 막는다

예제 2–4에서 본 규칙을 커널 버그 종류와 짝지으면 그림 2가 된다.

```svg
<svg viewBox="0 0 680 374" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="130" y="30" text-anchor="middle" font-size="13" font-weight="bold">Rust 규칙 (컴파일 타임)</text>
  <text x="390" y="30" text-anchor="middle" font-size="13" font-weight="bold">막아 주는 커널 버그</text>
  <text x="590" y="30" text-anchor="middle" font-size="13" font-weight="bold">C 커널의 대응</text>
  <rect x="10" y="50" width="240" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="130" y="70" text-anchor="middle" font-size="12">소유자는 하나, scope 끝에서 drop</text>
  <line x1="250" y1="65" x2="288" y2="65" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="290" y="50" width="200" height="30" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="390" y="70" text-anchor="middle" font-size="12">누수, 에러 경로 정리 누락</text>
  <rect x="505" y="50" width="170" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="590" y="70" text-anchor="middle" font-size="12">goto err 사다리</text>
  <rect x="10" y="88" width="240" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="130" y="108" text-anchor="middle" font-size="12">move 뒤 원래 변수는 못 쓴다</text>
  <line x1="250" y1="103" x2="288" y2="103" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="290" y="88" width="200" height="30" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="390" y="108" text-anchor="middle" font-size="12">double free</text>
  <rect x="505" y="88" width="170" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="590" y="108" text-anchor="middle" font-size="12">kfree 뒤 p = NULL 관례</text>
  <rect x="10" y="126" width="240" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="130" y="146" text-anchor="middle" font-size="12">빌림은 소유자보다 오래 못 산다</text>
  <line x1="250" y1="141" x2="288" y2="141" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="290" y="126" width="200" height="30" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="390" y="146" text-anchor="middle" font-size="12">use-after-free (UAF)</text>
  <rect x="505" y="126" width="170" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="590" y="146" text-anchor="middle" font-size="12">KASAN (런타임)</text>
  <rect x="10" y="164" width="240" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="130" y="184" text-anchor="middle" font-size="12">&amp;mut는 한 번에 하나만</text>
  <line x1="250" y1="179" x2="288" y2="179" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="290" y="164" width="200" height="30" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="390" y="184" text-anchor="middle" font-size="12">data race, 동시 수정</text>
  <rect x="505" y="164" width="170" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="590" y="184" text-anchor="middle" font-size="12">lockdep, KCSAN</text>
  <rect x="10" y="202" width="240" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="130" y="222" text-anchor="middle" font-size="12">Mutex&lt;T&gt;: 데이터는 락 안에만</text>
  <line x1="250" y1="217" x2="288" y2="217" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="290" y="202" width="200" height="30" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="390" y="222" text-anchor="middle" font-size="12">락 없이 공유 데이터 접근</text>
  <rect x="505" y="202" width="170" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="590" y="222" text-anchor="middle" font-size="12">주석 'protected by'</text>
  <rect x="10" y="240" width="240" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="130" y="260" text-anchor="middle" font-size="12">인덱스는 bounds check</text>
  <line x1="250" y1="255" x2="288" y2="255" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="290" y="240" width="200" height="30" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="390" y="260" text-anchor="middle" font-size="12">out-of-bounds 읽기/쓰기</text>
  <rect x="505" y="240" width="170" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="590" y="260" text-anchor="middle" font-size="12">KASAN (런타임)</text>
  <rect x="10" y="278" width="240" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="130" y="298" text-anchor="middle" font-size="12">NULL 대신 Option&lt;T&gt;</text>
  <line x1="250" y1="293" x2="288" y2="293" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="290" y="278" width="200" height="30" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="390" y="298" text-anchor="middle" font-size="12">NULL pointer dereference</text>
  <rect x="505" y="278" width="170" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="590" y="298" text-anchor="middle" font-size="12">리뷰, 정적 분석</text>
  <rect x="10" y="316" width="240" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="130" y="336" text-anchor="middle" font-size="12">Result&lt;T&gt; + #[must_use]</text>
  <line x1="250" y1="331" x2="288" y2="331" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="290" y="316" width="200" height="30" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="390" y="336" text-anchor="middle" font-size="12">에러 반환값 무시</text>
  <rect x="505" y="316" width="170" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="590" y="336" text-anchor="middle" font-size="12">__must_check 경고</text>
</svg>
```
그림 2 — 왼쪽 Rust 규칙(대부분 컴파일 타임, bounds check만 런타임)이 가운데 버그 종류를 막는다. 오른쪽은 C 커널이 같은 버그에 쓰는 대응으로, 대부분 "실행돼야 잡는" 런타임 도구나 사람의 규율이다.

**한계도 정확히 알아 두자.** Rust가 막지 못하는 것: 논리 버그(레지스터에 잘못된 값 쓰기), 데드락(락 순서 — lockdep은 Rust 락에도 그대로 동작), 하드웨어가 DMA로 메모리를 덮어쓰는 문제(컴파일러는 DMA 엔진을 모른다), 그리고 `unsafe` 블록 안에서 사람이 한 실수. 그래서 커널 Rust의 설계 목표는 "`unsafe`를 없앤다"가 아니라 **"`unsafe`를 소수의 추상화 계층에 가두고, 드라이버는 safe 코드만 쓰게 한다"**이다(그림 1의 숫자).

---

## 2. 역사와 6.18 현황

### 2.1 짧은 역사

| 시점 | 사건 | 의미 |
|---|---|---|
| 2020~2021 | Rust for Linux 프로젝트(Miguel Ojeda 주도) RFC, 메일링 리스트 논의 | "커널에 두 번째 언어" 논쟁 시작 |
| 6.1 (2022년 12월) | Rust 인프라 최초 merge | 빌드 시스템, `rust/` 최소 crate, `rust_minimal` 샘플 정도. 실제 드라이버 없음 |
| 6.8 (2024년 초) | 첫 Rust 드라이버: ASIX AX88796B 네트워크 PHY (`ax88796b_rust.rs`) | 기존 C 드라이버의 Rust 재작성 — "Rust로 진짜 하드웨어를 몬다"의 첫 사례 |
| 이후 6.x | 추상화가 계속 추가: platform/PCI/DRM/block/firmware/irq/DMA … | 드라이버를 쓰려면 먼저 그 서브시스템의 추상화가 merge돼야 한다는 구조가 굳어짐 |
| 6.18 (2025년 말 LTS) | Android binder의 Rust 버전 merge, Arm Mali GPU용 `tyr` 등 (§2.2에서 트리로 확인) | 수백만 대 기기에 실릴 수 있는 규모의 Rust 코드 |
| 2025년 12월 | Maintainers Summit에서 "실험 종료 = 성공" 결론 | Rust는 정식 구성 요소. 단, 모든 아키텍처·설정이 지원된다는 뜻은 아님 |

버전 번호는 확실한 것만 적었다. 각 드라이버가 정확히 몇 버전에 들어왔는지는 면접에서 틀리기 쉬우므로, "6.1에 인프라, 6.8 즈음 첫 드라이버, 6.18 LTS에 binder" 정도로 말하면 충분하다.

### 2.2 예제 5 — 6.18.55 트리에서 Rust 찾기

무엇을 확인하나: 기억이 아니라 소스 트리에서 직접 Rust 파일이 어디에 몇 개 있는지 센다.

```sh
#!/bin/bash
# e8_census.sh: where is Rust in the 6.18.55 tree?
cd /var/lab/src/linux-6.18.55 || exit 1
echo "== .rs files by top-level dir"
find . -name '*.rs' | cut -d/ -f2 | sort | uniq -c
echo "== .rs files under drivers/"
find drivers -name '*.rs' | cut -d/ -f2,3 | sort | uniq -c
echo "== Kconfig entries that depend on RUST (drivers/)"
grep -rln 'depends on RUST' --include=Kconfig drivers
printf "== lines: all .rs = %s, C files in drivers/ = %s\n" \
  "$(find . -name '*.rs' -print0 | xargs -0 cat | wc -l)" \
  "$(find drivers -name '*.c' | wc -l)"
```

```sh
limactl shell elinux -- bash /Users/donh/.elinux_scratch/E8/e8_census.sh
```

```text
== .rs files by top-level dir
     64 drivers
      1 lib
      1 mm
    159 rust
     15 samples
      3 scripts
== .rs files under drivers/
     18 android/binder
      2 block/rnull
      1 cpufreq/rcpufreq_dt.rs
     11 gpu/drm
     30 gpu/nova-core
      2 net/phy
== Kconfig entries that depend on RUST (drivers/)
drivers/base/firmware_loader/Kconfig
drivers/android/Kconfig
drivers/net/phy/Kconfig
drivers/gpu/drm/tyr/Kconfig
drivers/gpu/drm/nova/Kconfig
drivers/gpu/nova-core/Kconfig
drivers/block/rnull/Kconfig
== lines: all .rs = 68641, C files in drivers/ = 21695
```

출력에서 볼 것: Rust 파일 243개 중 절반 이상(159개)이 `rust/` — 즉 **드라이버가 아니라 기반(추상화) 코드**다. `drivers/` 아래 Rust 파일은 64개, C 파일은 21,695개다. 실제 드라이버를 하나씩 보면:

| 드라이버 | 경로 | 무엇 | 비고 |
|---|---|---|---|
| Android binder (Rust) | `drivers/android/binder/` | Android IPC 드라이버의 Rust 재작성 | `ANDROID_BINDER_IPC_RUST`, C 버전과 택일(`!ANDROID_BINDER_IPC`) |
| nova-core | `drivers/gpu/nova-core/` | NVIDIA GPU(GSP 펌웨어 기반) 코어 드라이버 | PCI 드라이버. 아직 초기 단계 |
| nova (DRM) | `drivers/gpu/drm/nova/` | nova-core 위의 DRM 드라이버 | |
| tyr | `drivers/gpu/drm/tyr/` | Arm Mali GPU DRM 드라이버 | platform 드라이버 — SoC GPU라 임베디드와 가깝다 |
| drm_panic QR | `drivers/gpu/drm/drm_panic_qr.rs` | 커널 패닉 화면에 QR 코드 그리기 | 작은 순수 로직 |
| rnull | `drivers/block/rnull/` | null_blk의 Rust 버전 | block-mq 추상화 시험대 |
| ax88796b_rust, qt2025 | `drivers/net/phy/` | 이더넷 PHY 드라이버 | 각각 수백 줄 미만 |
| rcpufreq_dt | `drivers/cpufreq/rcpufreq_dt.rs` | DT 기반 cpufreq의 Rust 버전 | clk·OPP·regulator 추상화 사용 |

흔히 듣는 이름 중 **트리에 없는 것**도 확인했다: `drivers/nvme` 아래 `.rs` 파일은 0개(Rust NVMe 드라이버 rnvme는 out-of-tree 실험), Apple Silicon GPU 드라이버(Asahi)도 6.18 트리에 없다. 면접에서 "Asahi GPU 드라이버가 mainline Rust 드라이버다"라고 말하면 틀린다.

### 2.3 예제 6 — `unsafe`는 어느 층에 몰려 있나

무엇을 확인하나: 그림 1의 설계 목표("`unsafe`는 아래층에 가두고 드라이버는 safe")가 실제로 지켜지는지 센다. `grep -c 'unsafe {'`는 한 줄에 쓴 블록만 세는 근사치다.

```sh
#!/bin/bash
# e8_unsafe_census.sh: how much `unsafe` lives in each layer?
cd /var/lab/src/linux-6.18.55 || exit 1
printf "%-24s %6s %7s %7s %7s\n" dir files lines unsafe SAFETY
for d in rust/kernel drivers/android/binder drivers/gpu/nova-core \
         drivers/block/rnull drivers/net/phy samples/rust; do
  f=$(find "$d" -name '*.rs')
  printf "%-24s %6d %7d %7d %7d\n" "$d" "$(echo "$f" | wc -l)" \
    "$(cat $f | wc -l)" "$(cat $f | grep -c 'unsafe {')" \
    "$(cat $f | grep -c '// SAFETY:')"
done
```

```text
dir                       files   lines  unsafe  SAFETY
rust/kernel                 130   41443    1400    1632
drivers/android/binder       18    9389     141     155
drivers/gpu/nova-core        30    5805      12      27
drivers/block/rnull           2     366       0       0
drivers/net/phy               2     237       0       0
samples/rust                 15    1558       2       4
```

출력에서 볼 것: `rust/kernel`은 30줄에 하나꼴로 `unsafe`가 있다(C를 감싸는 층이니 당연하다). nova-core는 480줄에 하나, PHY 드라이버와 rnull은 **0개**다 — 드라이버가 safe Rust만으로 쓰였다는 뜻이다. binder가 상대적으로 많은 이유는 유저 메모리 매핑·페이지 관리를 직접 다루는 저수준 드라이버이기 때문이다. `SAFETY` 주석 수가 `unsafe` 수보다 많은 것은 `unsafe impl`·`unsafe fn` 계약 설명에도 같은 주석을 달기 때문이다.

### 2.4 서브시스템 지도 — 우리 센서 키트에 필요한 것이 있나

드라이버를 Rust로 쓰려면 **그 버스·서브시스템의 Rust 추상화가 먼저 있어야 한다.** `rust/kernel/` 아래 파일 목록을 서브시스템별로 확인하면 그림 3이 나온다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55/rust/kernel && for s in i2c spi gpio iio input can v4l2 media regmap serial tty usb pwm hwmon thermal watchdog irq dma clk regulator of platform pci net drm block cpufreq firmware debugfs configfs miscdevice workqueue; do n=$(ls -d $s $s.rs 2>/dev/null | wc -l); echo -n "$s:$n "; done; echo'
```

```text
i2c:0 spi:0 gpio:0 iio:0 input:0 can:0 v4l2:0 media:0 regmap:0 serial:0 tty:0 usb:1 pwm:0 hwmon:0 thermal:0 watchdog:0 irq:2 dma:1 clk:1 regulator:1 of:1 platform:1 pci:2 net:2 drm:1 block:2 cpufreq:1 firmware:1 debugfs:2 configfs:1 miscdevice:1 workqueue:1
```

(숫자는 `foo.rs` 파일과 `foo/` 디렉터리를 합친 개수 — 0이면 그 이름의 추상화가 없다.)

```svg
<svg viewBox="0 0 680 506" xmlns="http://www.w3.org/2000/svg">
  <text x="10" y="34" font-size="13" font-weight="bold">추상화 + 실제 in-tree 드라이버</text>
  <rect x="10" y="44" width="155" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="88" y="61" text-anchor="middle" font-size="13" font-weight="bold">block</text>
  <text x="88" y="77" text-anchor="middle" font-size="12">rnull</text>
  <rect x="177" y="44" width="155" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="254" y="61" text-anchor="middle" font-size="13" font-weight="bold">net/phy</text>
  <text x="254" y="77" text-anchor="middle" font-size="12">ax88796b, qt2025</text>
  <rect x="344" y="44" width="155" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="422" y="61" text-anchor="middle" font-size="13" font-weight="bold">drm</text>
  <text x="422" y="77" text-anchor="middle" font-size="12">nova, tyr</text>
  <rect x="511" y="44" width="155" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="588" y="61" text-anchor="middle" font-size="13" font-weight="bold">pci</text>
  <text x="588" y="77" text-anchor="middle" font-size="12">nova-core</text>
  <rect x="10" y="92" width="155" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="88" y="109" text-anchor="middle" font-size="13" font-weight="bold">cpufreq</text>
  <text x="88" y="125" text-anchor="middle" font-size="12">rcpufreq_dt</text>
  <rect x="177" y="92" width="155" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="254" y="109" text-anchor="middle" font-size="13" font-weight="bold">binder</text>
  <text x="254" y="125" text-anchor="middle" font-size="12">android</text>
  <text x="10" y="164" font-size="13" font-weight="bold">추상화는 있음 (샘플·기반만)</text>
  <rect x="10" y="174" width="155" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="88" y="191" text-anchor="middle" font-size="13" font-weight="bold">platform</text>
  <text x="88" y="207" text-anchor="middle" font-size="12">sample</text>
  <rect x="177" y="174" width="155" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="254" y="191" text-anchor="middle" font-size="13" font-weight="bold">usb</text>
  <text x="254" y="207" text-anchor="middle" font-size="12">sample</text>
  <rect x="344" y="174" width="155" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="422" y="191" text-anchor="middle" font-size="13" font-weight="bold">auxiliary</text>
  <text x="422" y="207" text-anchor="middle" font-size="12">sample</text>
  <rect x="511" y="174" width="155" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="588" y="191" text-anchor="middle" font-size="13" font-weight="bold">miscdevice</text>
  <text x="588" y="207" text-anchor="middle" font-size="12">sample</text>
  <rect x="10" y="222" width="155" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="88" y="239" text-anchor="middle" font-size="13" font-weight="bold">irq</text>
  <text x="88" y="255" text-anchor="middle" font-size="12">request_irq</text>
  <rect x="177" y="222" width="155" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="254" y="239" text-anchor="middle" font-size="13" font-weight="bold">dma</text>
  <text x="254" y="255" text-anchor="middle" font-size="12">coherent</text>
  <rect x="344" y="222" width="155" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="422" y="239" text-anchor="middle" font-size="13" font-weight="bold">clk / regulator</text>
  <text x="422" y="255" text-anchor="middle" font-size="12">consumer</text>
  <rect x="511" y="222" width="155" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="588" y="239" text-anchor="middle" font-size="13" font-weight="bold">of / property</text>
  <text x="588" y="255" text-anchor="middle" font-size="12">DT 읽기</text>
  <rect x="10" y="270" width="155" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="88" y="287" text-anchor="middle" font-size="13" font-weight="bold">firmware</text>
  <text x="88" y="303" text-anchor="middle" font-size="12">fw loader</text>
  <rect x="177" y="270" width="155" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="254" y="287" text-anchor="middle" font-size="13" font-weight="bold">debugfs / configfs</text>
  <text x="254" y="303" text-anchor="middle" font-size="12">sample</text>
  <rect x="344" y="270" width="155" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="422" y="287" text-anchor="middle" font-size="13" font-weight="bold">workqueue / timer</text>
  <text x="422" y="303" text-anchor="middle" font-size="12">hrtimer, delay</text>
  <rect x="511" y="270" width="155" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="588" y="287" text-anchor="middle" font-size="13" font-weight="bold">io / mem</text>
  <text x="588" y="303" text-anchor="middle" font-size="12">MMIO</text>
  <text x="10" y="342" font-size="13" font-weight="bold">추상화 없음 (6.18 기준)</text>
  <rect x="10" y="352" width="155" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="88" y="369" text-anchor="middle" font-size="13" font-weight="bold">i2c</text>
  <rect x="177" y="352" width="155" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="254" y="369" text-anchor="middle" font-size="13" font-weight="bold">spi</text>
  <rect x="344" y="352" width="155" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="422" y="369" text-anchor="middle" font-size="13" font-weight="bold">gpio</text>
  <rect x="511" y="352" width="155" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="588" y="369" text-anchor="middle" font-size="13" font-weight="bold">iio</text>
  <rect x="10" y="400" width="155" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="88" y="417" text-anchor="middle" font-size="13" font-weight="bold">input</text>
  <rect x="177" y="400" width="155" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="254" y="417" text-anchor="middle" font-size="13" font-weight="bold">can</text>
  <rect x="344" y="400" width="155" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="422" y="417" text-anchor="middle" font-size="13" font-weight="bold">v4l2 / media</text>
  <rect x="511" y="400" width="155" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="588" y="417" text-anchor="middle" font-size="13" font-weight="bold">regmap</text>
  <rect x="10" y="448" width="155" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="88" y="465" text-anchor="middle" font-size="13" font-weight="bold">tty / serial</text>
  <rect x="177" y="448" width="155" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="254" y="465" text-anchor="middle" font-size="13" font-weight="bold">pwm</text>
  <rect x="344" y="448" width="155" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="422" y="465" text-anchor="middle" font-size="13" font-weight="bold">hwmon / thermal</text>
  <rect x="511" y="448" width="155" height="40" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="588" y="465" text-anchor="middle" font-size="13" font-weight="bold">watchdog</text>
</svg>
```
그림 3 — 6.18.55 기준 Rust 추상화 지도. 초록은 추상화와 실제 in-tree 드라이버가 둘 다 있는 곳, 파랑은 추상화(와 샘플)만 있는 곳, 빨강은 추상화가 없어서 Rust 드라이버를 쓸 수 없는 곳이다.

**임베디드 엔지니어에게 이 그림의 결론은 분명하다.** 센서 키트의 주력 버스인 I2C·SPI·GPIO, 센서 프레임워크 IIO, 카메라 V4L2, CAN, regmap이 **6.18에는 전부 빨강**이다. 즉 "IMU 드라이버를 Rust로 mainline 6.18에서 쓰자"는 지금은 불가능하고, 하려면 추상화부터 직접 써서 upstream에 보내야 한다. 반면 platform 드라이버 + MMIO(`io/mem`) + IRQ + clk/regulator + DT 읽기(`of`, `device/property`)는 있으므로, **SoC 내부의 단순한 MMIO 블록**(예: 사내 FPGA IP의 레지스터 블록)을 platform 드라이버로 Rust로 쓰는 것은 이론상 가능하다.

---

## 3. 동작 원리 — C 커널 위에 Rust를 얹는 방법

### 3.1 `rust/` 디렉터리와 Kconfig 조건

무엇을 확인하나: Rust 지원을 구성하는 디렉터리와, `CONFIG_RUST`를 켤 수 있는 조건을 소스에서 본다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55 && ls rust | tr "\n" " "; echo; grep -n "config RUST$" -A14 init/Kconfig'
```

```text
Makefile bindgen_parameters bindings build_error.rs compiler_builtins.rs exports.c ffi.rs helpers kernel macros pin-init uapi
2090:config RUST
2091-	bool "Rust support"
2092-	depends on HAVE_RUST
2093-	depends on RUST_IS_AVAILABLE
2094-	select EXTENDED_MODVERSIONS if MODVERSIONS
2095-	depends on !MODVERSIONS || GENDWARFKSYMS
2096-	depends on !GCC_PLUGIN_RANDSTRUCT
2097-	depends on !RANDSTRUCT
2098-	depends on !DEBUG_INFO_BTF || (PAHOLE_HAS_LANG_EXCLUDE && !LTO)
2099-	depends on !CFI || HAVE_CFI_ICALL_NORMALIZE_INTEGERS_RUSTC
2100-	select CFI_ICALL_NORMALIZE_INTEGERS if CFI
2101-	depends on !CALL_PADDING || RUSTC_VERSION >= 108100
2102-	depends on !KASAN || CC_IS_CLANG
2103-	depends on !KASAN_SW_TAGS
2104-	depends on !(MITIGATION_RETHUNK && KASAN) || RUSTC_VERSION >= 108300
```

출력에서 볼 것: `RANDSTRUCT`(구조체 필드 순서를 무작위화하는 보안 옵션)와 같이 못 켠다 — bindgen이 본 구조체 레이아웃과 실제 레이아웃이 달라지기 때문이다. `MODVERSIONS`도 `GENDWARFKSYMS`가 있어야 된다. 벤더 커널이 이런 옵션을 켜 두었다면 Rust를 켜는 순간 설정 충돌부터 만난다.

`rust/` 안의 각 조각은 다음 역할을 한다.

| 경로 | 역할 | 비유 |
|---|---|---|
| `rust/bindings/` | `bindings_helper.h`(포함할 C 헤더 목록) → bindgen → `bindings` crate | 벤더 SDK 헤더를 자동 변환한 레지스터 정의 |
| `rust/helpers/` | C의 `static inline` 함수·매크로를 실제 함수로 감싼 C 파일 55개 | 인라인이라 링크할 심볼이 없는 것을 위한 얇은 래퍼 |
| `rust/kernel/` | `kernel` crate — 안전한 추상화 (130개 파일, 41,443줄) | 팀이 만든 "잘못 쓸 수 없는" 드라이버 라이브러리 |
| `rust/macros/` | `module!`, `#[vtable]` 등 proc-macro | 보일러플레이트 생성기 |
| `rust/pin-init/` | `pin_init!`, `#[pin_data]` — 제자리 초기화 라이브러리 | §3.7 |
| `rust/uapi/` | 유저 API 헤더(`include/uapi`)의 바인딩 | ioctl 구조체 공유 |
| `rust/ffi.rs` | `c_int`, `c_char` 등 C 타입 별칭 | `stdint.h` |
| `rust/exports.c` | Rust 심볼을 `EXPORT_SYMBOL_GPL`로 내보내기 | 모듈이 링크할 수 있게 |

### 3.2 예제 7 — 툴체인 확인: `rustavailable`과 최소 버전

무엇을 확인하나: 커널 빌드 시스템이 지금 툴체인을 받아들이는지, 최소 요구 버전이 무엇인지 확인한다. Rust 커널 작업의 첫 명령이다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55 && make O=/var/tmp/E8/ra LLVM=1 rustavailable 2>&1 | tail -3; for t in rustc bindgen llvm; do echo -n "min $t: "; scripts/min-tool-version.sh $t; done; rustc --version; bindgen --version; clang --version | head -1'
```

```text
make[1]: Entering directory '/var/tmp/E8/ra'
Rust is available!
make[1]: Leaving directory '/var/tmp/E8/ra'
min rustc: 1.78.0
min bindgen: 0.65.1
min llvm: 15.0.0
rustc 1.93.1 (01f6ddf75 2026-02-11) (built from a source tarball)
bindgen 0.72.1
Ubuntu clang version 21.1.8 (6ubuntu1)
```

출력에서 볼 것: 6.18의 최소 rustc는 1.78이다. 예전(6.1~6.10 무렵)에는 커널이 **특정 rustc 버전 하나**만 받았지만, 지금은 최소 버전 이상이면 된다(Debian stable의 rustc로 빌드할 수 있게 유지한다는 것이 정책). 대신 커널은 Rust의 **unstable 기능**(`#![feature(...)]`)을 일부 쓰므로, 너무 새 rustc에서 기능 이름이 바뀌면 깨질 수 있다 — 그래서 커널 소스에 `RUSTC_HAS_...` 같은 버전별 분기가 있다.

### 3.3 예제 8 — bindgen이 만든 바인딩 들여다보기

**bindgen**은 C 헤더를 libclang으로 파싱해서 같은 레이아웃의 Rust 선언을 자동 생성하는 도구다. 커널은 `rust/bindings/bindings_helper.h` 한 파일에 필요한 헤더를 `#include`로 모아 두고, 빌드 때 이것을 bindgen에 넣는다.

무엇을 확인하나: 내 빌드 트리에서 생성된 파일 크기와, C의 `struct list_head`, `_printk()`, `krealloc`, `GFP_KERNEL`이 Rust로 어떻게 옮겨졌는지 본다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E8/build/rust && wc -l bindings/bindings_generated.rs bindings/bindings_helpers_generated.rs uapi/uapi_generated.rs; grep -n -A4 "^pub struct list_head" bindings/bindings_generated.rs; grep -n -B1 -A1 "pub fn _printk(" bindings/bindings_generated.rs; grep -n "GFP_KERNEL" bindings/bindings_generated.rs | head -2; grep -n "pub fn krealloc_node_align_noprof\|pub fn kfree(" -A6 bindings/bindings_generated.rs | head -9'
```

```text
 127975 bindings/bindings_generated.rs
   1475 bindings/bindings_helpers_generated.rs
  88756 uapi/uapi_generated.rs
 218206 total
23956:pub struct list_head {
23957-    pub next: *mut list_head,
23958-    pub prev: *mut list_head,
23959-}
23960-impl Default for list_head {
24951-extern "C" {
24952:    pub fn _printk(fmt: *const ffi::c_char, ...) -> ffi::c_int;
24953-}
127692:pub const GFP_KERNEL: gfp_t = 3264;
127693:pub const GFP_KERNEL_ACCOUNT: gfp_t = 4197568;
39002:    pub fn krealloc_node_align_noprof(
39003-        objp: *const ffi::c_void,
39004-        new_size: usize,
39005-        align: ffi::c_ulong,
39006-        flags: gfp_t,
39007-        nid: ffi::c_int,
39008-    ) -> *mut ffi::c_void;
--
39011:    pub fn kfree(objp: *const ffi::c_void);
```

출력에서 볼 것:

- 바인딩은 **12만 8천 줄**, 4.2 MB짜리 자동 생성 파일이다. 사람이 손으로 쓰는 것이 아니다. C 헤더가 바뀌면 다음 빌드에서 자동으로 따라간다 — 이것이 "C 쪽 API가 바뀌어도 Rust 바인딩이 낡지 않는" 이유다(대신 그 위의 `kernel` crate 추상화는 사람이 고쳐야 한다).
- `list_head`의 필드는 `*mut list_head` — **raw pointer**다. Rust에서 raw pointer를 따라가는 것은 `unsafe`다. `extern "C"` 함수를 부르는 것도 전부 `unsafe`다. 바인딩 층은 "C를 그대로 옮겼을 뿐, 안전성은 하나도 보장하지 않는" 층이다.
- `GFP_KERNEL`이 숫자 상수 `3264`(= `0xcc0`)로 들어왔다. C에서는 비트 OR 매크로 식이라 bindgen이 직접 못 옮기므로, `bindings_helper.h`가 `const gfp_t RUST_CONST_HELPER_GFP_KERNEL = GFP_KERNEL;`처럼 상수 변수로 한 번 받아 두고, 빌드 스크립트(`rust/Makefile`의 `sed`)가 접두어를 지운다.

bindgen 실제 명령줄(`.bindings_generated.rs.cmd`의 앞부분, 뒤는 생략):

```text
savedcmd_rust/bindings/bindings_generated.rs := bindgen /var/lab/src/linux-6.18.55/rust/bindings/bindings_helper.h --blocklist-type __kernel_s?size_t --blocklist-type __kernel_ptrdiff_t --opaque-type xregs_state --opaque-type desc_struct ... --opaque-type spinlock --no-doc-comments ... --rust-target 1.68 --use-core --with-derive-default --ctypes-prefix ffi --no-layout-tests --no-debug '.*' --enable-function-attribute-detection -o rust/bindings/bindings_generated.rs -- -Wp,-MMD,rust/bindings/.bindings_generated.rs.d -nostdinc ...
```

`--` 뒤는 **C 컴파일 플래그**다. bindgen이 커널과 똑같은 매크로·include 경로로 헤더를 해석해야 구조체 레이아웃이 일치한다. 이것이 Rust 커널을 **clang(LLVM=1)으로 빌드하는 것이 정석**인 이유 중 하나다: GCC 전용 플래그는 libclang이 이해하지 못한다(6.18 문서는 GCC로 빌드한 커널 + Rust를 실험적 조합으로 본다).

### 3.4 helpers와 `kernel` crate — `Mutex::lock()`이 C까지 내려가는 길

C 커널 API 중 상당수는 함수가 아니라 `static inline` 함수나 매크로다(예: lockdep이 켜지면 `mutex_lock()`은 매크로가 된다). 매크로는 bindgen이 Rust 함수로 옮길 수 없고, 인라인 함수는 링크할 심볼이 없다. 그래서 `rust/helpers/*.c`가 진짜 함수로 한 번 감싼다.

```c
// rust/helpers/mutex.c (발췌)
void rust_helper_mutex_lock(struct mutex *lock)
{
	mutex_lock(lock);
}
```

bindgen은 이 helper를 원래 이름으로 보이게 만든다(`bindings_helpers_generated.rs`):

```text
extern "C" {
    #[link_name="rust_helper_mutex_lock"]
    pub fn mutex_lock(lock: *mut mutex);
}
```

그 위에서 `kernel` crate가 안전한 API를 만든다. 아래는 6.18 소스 그대로다.

```rust
// rust/kernel/sync/lock/mutex.rs — Backend 구현 (발췌)
unsafe fn lock(ptr: *mut Self::State) -> Self::GuardState {
    // SAFETY: The safety requirements of this function ensure that `ptr` points to valid
    // memory, and that it has been initialised before.
    unsafe { bindings::mutex_lock(ptr) };
}

// rust/kernel/sync/lock.rs — 드라이버가 부르는 safe API
pub fn lock(&self) -> Guard<'_, T, B> {
    // SAFETY: The constructor of the type calls `init`, so the existence of the object proves
    // that `init` was called.
    let state = unsafe { B::lock(self.state.get()) };
    // SAFETY: The lock was just acquired.
    unsafe { Guard::new(self, state) }
}
```

경로를 정리하면 이렇다(영문만이라 ASCII로 그린다).

```
driver:        self.samples.lock()                       safe Rust
kernel crate:  Lock::lock(&self) -> Guard<'_, T>         unsafe inside, safe outside
               MutexBackend::lock(ptr)                   unsafe fn + SAFETY
bindings:      bindings::mutex_lock(ptr)                 extern "C", link_name=rust_helper_mutex_lock
helpers (C):   rust_helper_mutex_lock(lock)              rust/helpers/mutex.c
C kernel:      mutex_lock(lock) -> __mutex_lock(...)     kernel/locking/mutex.c
unlock:        Drop for Guard -> MutexBackend::unlock -> mutex_unlock()
```

중요한 점 두 가지.

- **`Guard`가 scope를 벗어나면 `Drop`이 `mutex_unlock()`을 부른다.** C에서 흔한 "에러 경로에서 unlock 빼먹기"가 구조적으로 불가능하다. Don이 아는 RAII(C++) 그대로다.
- `Guard<'_, T>`의 수명 `'_`은 `&self`(뮤텍스)에 묶여 있어서, guard가 뮤텍스보다 오래 살 수 없다. 보호 대상 `T`에는 guard를 통해서만 접근한다(`Deref`).

`kernel` crate에서 드라이버 작성자가 가장 자주 만나는 타입을 C 대응과 같이 정리한다.

| Rust (`kernel::`) | C 대응 | 무엇이 안전해지나 |
|---|---|---|
| `KBox<T>` | `kmalloc` + `kfree` | 소유자가 사라지면 자동 해제, double free 불가 |
| `KVec<T>` / `VVec<T>` / `KVVec<T>` | `kmalloc_array` / `vmalloc` / `kvmalloc` + 길이 관리 | 할당 실패가 `Result`로, 인덱스 bounds check |
| `Arc<T>` | `kref` + `kref_put` | refcount를 잊을 수 없음 |
| `ARef<T>` | `get_device`/`put_device` 같은 C 쪽 refcount | C 객체 refcount를 타입으로 |
| `Mutex<T>` / `SpinLock<T>` | `struct mutex` / `spinlock_t` + 보호할 필드 | 락 없이 데이터 접근 불가 |
| `Error`, `Result<T>` | `-EINVAL` 같은 음수 errno | 에러를 무시하면 경고(`#[must_use]`) |
| `CStr`, `c_str!` | `const char *` (NUL 종료) | 길이·NUL 보장 |
| `Devres<T>` / `Revocable<T>` | `devm_*` | 장치가 사라지면 접근 자체가 실패 |
| `UserPtr`, `UserSlice` | `copy_from_user` / `copy_to_user` | 유저 포인터를 직접 역참조 불가(E6) |

### 3.5 `module!` 매크로 — 6.18에서 쓸 수 있는 키

C 모듈의 `module_init()`/`module_exit()`/`MODULE_LICENSE()`를 Rust에서는 `module!` 매크로 하나가 대신한다. 어떤 키를 받는지는 매크로 소스에 그대로 있다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55 && grep -n -A9 "const EXPECTED_KEYS" rust/macros/module.rs; ls rust/kernel/module_param.rs'
```

```text
107:        const EXPECTED_KEYS: &[&str] = &[
108-            "type",
109-            "name",
110-            "authors",
111-            "description",
112-            "license",
113-            "alias",
114-            "firmware",
115-        ];
116:        const REQUIRED_KEYS: &[&str] = &["type", "name", "license"];
ls: cannot access 'rust/kernel/module_param.rs': No such file or directory
```

출력에서 볼 것: **6.18에는 `params` 키가 없다.** 즉 C의 `module_param()`에 해당하는 것을 Rust 모듈에서 아직 쓸 수 없다(모듈 파라미터 지원은 이후 버전에서 추가되는 중이다 — 쓰기 전에 대상 커널 소스에서 반드시 확인할 것). 그래서 이 노트의 `e8_hello`는 파라미터 대신 상수를 쓴다. 이런 "C에선 당연한 것이 Rust 쪽엔 아직 없다"가 2026년 Rust 커널 개발의 일상이다.

`module!`이 생성하는 코드(매크로 소스에서 발췌)를 보면 `Result`가 errno로 바뀌는 지점이 정확히 보인다.

```rust
// rust/macros/module.rs 가 생성하는 코드 (format 문자열 안의 내용, 발췌)
pub unsafe extern "C" fn init_module() -> ::kernel::ffi::c_int {
    // ...
    unsafe { __init() }
}

unsafe fn __init() -> ::kernel::ffi::c_int {
    let initer =
        <{type_} as ::kernel::InPlaceModule>::init(&super::super::THIS_MODULE);
    // SAFETY: No data race, since `__MOD` can only be accessed by this module ...
    match unsafe { initer.__pinned_init(__MOD.as_mut_ptr()) } {
        Ok(m) => 0,
        Err(e) => e.to_errno(),
    }
}
```

즉 C 커널의 `do_init_module()` 입장에서는 평범한 `init_module()` 심볼이 있고, 0 또는 음수 errno를 돌려준다. Rust 모듈이라고 커널 모듈 로더가 따로 있는 것이 아니다.

### 3.6 `Result`와 errno — `?` 연산자

C 커널의 에러 규약(E6): 함수는 0 또는 음수 errno를 돌려주고, 포인터는 `ERR_PTR()`로 에러를 싣는다. Rust 커널은 이것을 타입으로 바꿨다.

- `kernel::error::Error` — 음수 errno 하나를 감싼 타입. `EINVAL`, `ENOMEM`, `ENODEV` 같은 상수가 `declare_err!`로 정의돼 있다(`rust/kernel/error.rs`).
- `Result<T = (), E = Error>` — 성공이면 `Ok(T)`, 실패면 `Err(Error)`.
- `Error::from_errno(c_int)` / `e.to_errno()` — C 경계에서 변환. `to_result(ret)`는 "C 함수가 음수를 돌려주면 `Err`"를 한 줄로.
- `?` 연산자 — `Err`이면 **그 자리에서 함수를 빠져나가며 에러를 위로 전달**한다. C의 `if (ret) goto err;` 사다리와 같은 일을 하는데, 이미 만든 `KVec` 같은 지역 자원은 `Drop`이 알아서 해제한다.

```rust
// rust/kernel/error.rs (발췌)
pub fn to_result(err: crate::ffi::c_int) -> Result {
    if err < 0 {
        Err(Error::from_errno(err))
    } else {
        Ok(())
    }
}
```

할당 실패는 `AllocError`라는 별도 타입이지만 `Error`로 자동 변환(`From`)되므로 `KVec::push(x, GFP_KERNEL)?`가 그대로 `-ENOMEM`이 된다. **GFP 플래그를 호출마다 명시**하게 한 것도 커널 Rust의 특징이다: 유저 공간 Rust의 `Vec::push`는 할당 실패 시 프로세스를 죽이지만, 커널은 그럴 수 없으므로 모든 할당 함수가 `flags`를 받고 `Result`를 돌려준다. 인터럽트 컨텍스트라면 `GFP_ATOMIC`을 넘기면 된다.

### 3.7 Pinning — 왜 커널 Rust는 `pin_init!`을 쓰나

Rust의 기본 가정은 "값은 언제든 다른 주소로 옮겨도(move = memcpy) 된다"이다. 그런데 커널의 많은 C 구조체는 **자기 주소를 자기 안에 저장**한다. `struct mutex` 안의 `wait_list`(`struct list_head`)는 비어 있을 때 `next`와 `prev`가 자기 자신을 가리킨다. 이 구조체를 memcpy로 옮기면 복사본이 옛 주소를 가리키게 된다(그림 4). `wait_queue_head`, `hrtimer`, `work_struct`, 리스트에 등록된 모든 객체가 같은 문제를 가진다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker>
    <marker id="ah5r" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="#d0564a"/></marker>
  </defs>
  <text x="160" y="24" text-anchor="middle" font-size="13" font-weight="bold">원래 위치 0x1000 (초기화 직후)</text>
  <rect x="30" y="36" width="260" height="150" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="160" y="58" text-anchor="middle" font-size="13">struct mutex</text>
  <text x="50" y="84" font-size="12">owner      = 0</text>
  <text x="50" y="106" font-size="12">wait_lock  = unlocked</text>
  <rect x="45" y="118" width="185" height="54" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="55" y="140" font-size="12">wait_list.next = 0x1010</text>
  <text x="55" y="160" font-size="12">wait_list.prev = 0x1010</text>
  <path d="M230,140 C268,140 268,108 242,108 L234,118" fill="none" stroke="currentColor" marker-end="url(#ah5)"/>
  <text x="236" y="100" font-size="12">자기 자신</text>
  <text x="510" y="24" text-anchor="middle" font-size="13" font-weight="bold">memcpy로 0x2000에 이동한 뒤</text>
  <rect x="380" y="36" width="260" height="150" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="510" y="58" text-anchor="middle" font-size="13">struct mutex (복사본)</text>
  <text x="400" y="84" font-size="12">owner      = 0</text>
  <text x="400" y="106" font-size="12">wait_lock  = unlocked</text>
  <rect x="395" y="118" width="185" height="54" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="405" y="140" font-size="12">wait_list.next = 0x1010</text>
  <text x="405" y="160" font-size="12">wait_list.prev = 0x1010</text>
  <line x1="395" y1="150" x2="232" y2="150" stroke="#d0564a" stroke-dasharray="5,3" marker-end="url(#ah5r)"/>
  <text x="300" y="212" text-anchor="middle" font-size="12">복사본의 리스트가 옛 주소를 가리킨다 → 옛 자리가 재사용되면 메모리 오염</text>
  <rect x="30" y="236" width="610" height="104" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="335" y="260" text-anchor="middle" font-size="13" font-weight="bold">Rust의 답: 최종 자리에서 초기화하고(pin_init), 그 뒤로 못 움직이게(Pin)</text>
  <text x="50" y="286" font-size="12">#[pin] samples: Mutex&lt;KVec&lt;u16&gt;&gt;</text>
  <text x="300" y="286" font-size="12">→ 이 필드는 구조적으로 pinned</text>
  <text x="50" y="308" font-size="12">samples &lt;- new_mutex!(v)</text>
  <text x="300" y="308" font-size="12">→ 옮기지 않고 그 주소에서 __mutex_init()</text>
  <text x="50" y="330" font-size="12">Pin&lt;&amp;mut Self&gt;</text>
  <text x="300" y="330" font-size="12">→ safe 코드로는 꺼내서 이동 불가</text>
</svg>
```
그림 4 — `struct mutex`를 초기화한 뒤 옮기면 리스트 포인터가 옛 주소를 가리킨다. Rust는 "최종 주소에서 바로 초기화(`pin_init`)"와 "그 뒤로 이동 금지(`Pin`)"로 이 문제를 타입 수준에서 막는다.

해결 도구는 세 가지다.

- `Pin<P>` — "이 포인터가 가리키는 값은 다시는 옮기지 않는다"는 약속을 담은 래퍼. safe 코드로는 `Pin<&mut T>`에서 `&mut T`를 꺼내 `mem::swap` 같은 것을 할 수 없다.
- `#[pin_data]` + `#[pin]` — 구조체의 어느 필드가 고정돼야 하는지 표시.
- `pin_init!` / `try_pin_init!` + `<-` 문법 — 값을 스택에서 만들어 옮기는 대신, **최종 메모리 위치를 받아 그 자리에서 초기화하는 함수(initializer)**를 만든다. `new_mutex!(v)`는 "그 자리에서 `__mutex_init()`을 부르는 initializer"를 돌려준다.

C 개발자 관점에서 보면 이것은 "구조체를 먼저 `kzalloc`하고, 그 포인터에 대고 `mutex_init(&p->lock)`을 부른다"는 C의 평범한 순서를 타입으로 강제한 것이다. C에서는 `struct foo tmp; mutex_init(&tmp.lock); *p = tmp;` 같은 실수를 해도 컴파일된다. 6.18 `module!`도 내부적으로 `InPlaceModule::init()`이 돌려준 initializer로 정적 변수 `__MOD` 자리에 바로 초기화한다(§3.5 코드의 `__pinned_init(__MOD.as_mut_ptr())`).

### 3.8 예제 9 — `unsafe`와 `// SAFETY:` 주석: clippy가 강제한다

`unsafe` 블록은 "여기서는 컴파일러가 검사하지 못하는 일(raw pointer 역참조, C 함수 호출, `static mut` 접근 등)을 한다, 올바름은 내가 보장한다"는 표시다. 커널 규칙은 **모든 `unsafe` 블록 바로 위에 왜 안전한지 `// SAFETY:` 주석으로 쓴다**는 것이고, 이것을 clippy lint `undocumented_unsafe_blocks`로 기계적으로 검사한다.

무엇을 확인하나: SAFETY 주석 없는 `unsafe` 블록에 커널 빌드의 `CLIPPY=1`이 경고를 내는 것을 본다.

```rust
// SPDX-License-Identifier: GPL-2.0
//! e8_unsafe: an unsafe block with and without a SAFETY comment.
use kernel::prelude::*;

module! {
    type: E8Unsafe,
    name: "e8_unsafe",
    license: "GPL",
}

struct E8Unsafe;

fn peek(p: *const u32) -> u32 {
    unsafe { p.read_volatile() } // no justification written
}

fn peek_ok(r: &u32) -> u32 {
    let p: *const u32 = r;
    // SAFETY: `p` comes from a live `&u32`, so it is non-null, aligned
    // and valid for reads for the duration of this call.
    unsafe { p.read_volatile() }
}

impl kernel::Module for E8Unsafe {
    fn init(_m: &'static ThisModule) -> Result<Self> {
        let reg: u32 = 0x5a5a;
        pr_info!("e8_unsafe: {:#x} {:#x}\n", peek(&reg), peek_ok(&reg));
        Ok(E8Unsafe)
    }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E8/mod/e8_unsafe && make -C /var/tmp/E8/build LLVM=1 M=$PWD CLIPPY=1 e8_unsafe.o'
```

```text
  CLIPPY [M] e8_unsafe.o
warning: unsafe block missing a safety comment
  --> e8_unsafe.rs:14:5
   |
14 |     unsafe { p.read_volatile() } // no justification written
   |     ^^^^^^^^^^^^^^^^^^^^^^^^^^^^
   |
   = help: consider adding a safety comment on the preceding line
   = help: for further information visit https://rust-lang.github.io/rust-clippy/rust-1.93.0/index.html#undocumented_unsafe_blocks
   = note: requested on the command line with `-W clippy::undocumented-unsafe-blocks`

warning: 1 warning emitted
```

출력에서 볼 것: 14행만 경고, SAFETY 주석이 있는 `peek_ok`는 통과했다. 그런데 **`peek`에는 더 깊은 문제가 있다**: 아무 raw pointer나 받는 함수를 safe `fn`으로 선언했으므로, 호출자가 쓰레기 주소를 넘겨도 컴파일된다. 이런 함수는 `unsafe fn`이어야 하고, 호출자가 `unsafe` 블록 + SAFETY로 책임을 진다. 이것을 Rust 용어로 **unsound**(safe 인터페이스로 UB를 만들 수 있음)라고 하며, 커널 리뷰에서 가장 엄하게 보는 부분이다. Don의 펌웨어 감각으로 번역하면: "`unsafe`는 MPU 권한을 잠깐 올리는 SVC 진입이고, SAFETY 주석은 그 진입 조건을 문서화한 것, unsound한 safe 함수는 권한 검사 없이 SVC를 노출한 API"다.

---

## 4. Rust 커널 빌드 — LLVM=1, CONFIG_RUST

### 4.1 예제 10 — 랩 커널에는 왜 Rust 모듈을 못 넣나

랩의 `out/Image`는 GCC로 빌드됐고 Rust 툴체인 없이 설정됐다. `.config`에 그 흔적이 그대로 있다.

```sh
limactl shell elinux -- bash -lc 'grep -E "^CONFIG_(CC_IS_GCC|GCC_VERSION|RUSTC_VERSION|RUST)=" /var/lab/out/linux-build/.config'
```

```text
CONFIG_CC_IS_GCC=y
CONFIG_GCC_VERSION=150200
CONFIG_RUSTC_VERSION=0
```

`CONFIG_RUST` 줄 자체가 없다. 이 트리에 대고 Rust 모듈을 빌드하면 어떻게 될까. 실제로 해 봤다.

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/E8/labtry && cp /var/tmp/E8/mod/e8_hello/{e8_hello.rs,Kbuild} /var/tmp/E8/labtry/ && cd /var/tmp/E8/labtry && make -C /var/lab/out/linux-build M=$PWD modules'
```

```text
  RUSTC [M] e8_hello.o
warning: target feature `neon` must be enabled to ensure that the ABI of the current target can be implemented correctly
  |
  = note: this was previously accepted by the compiler but is being phased out; it will become a hard error in a future release!
  = note: for more information, see issue #116344 <https://github.com/rust-lang/rust/issues/116344>

error[E0463]: can't find crate for `core`
  |
  = note: the `aarch64-unknown-none` target may not be installed
  = help: consider downloading the target with `rustup target add aarch64-unknown-none`
  = help: consider building the standard library from source with `cargo build -Zbuild-std`

error: aborting due to 1 previous error; 1 warning emitted
```

출력에서 볼 것: rustc의 도움말("rustup target add")은 **틀린 처방**이다. 커널은 rustup의 표준 라이브러리를 쓰지 않고 `core`를 커널 빌드 안에서 직접 컴파일해 `rust/libcore.rmeta`로 만든다. 그 파일이 없는 이유는 **커널이 `CONFIG_RUST=y`로 빌드되지 않았기 때문**이다. 교훈: Rust 모듈은 "Rust를 켜고 빌드한 그 커널 트리"에 대해서만 빌드된다. 벤더가 준 커널이 Rust 꺼진 바이너리라면, Rust 모듈 하나를 넣으려고 커널부터 다시 빌드해야 한다(§10).

### 4.2 예제 11 — 설정하고 빌드하기

커널 문서(`Documentation/rust/quick-start.rst`)는 이렇게 말한다: "Building a kernel with a complete LLVM toolchain is the best supported setup at the moment. ... Using GCC also works for some configurations, but it is very experimental at the moment." 그래서 `LLVM=1`(clang + ld.lld + llvm-ar …)로 빌드한다.

무엇을 확인하나: arm64 defconfig에서 시작해 Rust와 샘플을 켜고, `olddefconfig`가 의존성을 받아들였는지 본다.

```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/E8/build && cd /var/lab/src/linux-6.18.55 && make O=/var/tmp/E8/build LLVM=1 defconfig >/dev/null && cd /var/tmp/E8/build && /var/lab/src/linux-6.18.55/scripts/config --file .config -e RUST -e SAMPLES -e SAMPLES_RUST -m SAMPLE_RUST_MINIMAL -m SAMPLE_RUST_PRINT -e IKCONFIG -e IKCONFIG_PROC && make -C /var/lab/src/linux-6.18.55 O=/var/tmp/E8/build LLVM=1 olddefconfig >/dev/null && grep -E "^CONFIG_(RUST|SAMPLES_RUST|SAMPLE_RUST|CC_IS_CLANG|RUSTC_VERSION|CLANG_VERSION)" .config'
```

```text
CONFIG_CC_IS_CLANG=y
CONFIG_CLANG_VERSION=210108
CONFIG_RUSTC_VERSION=109301
CONFIG_RUST_IS_AVAILABLE=y
CONFIG_RUSTC_LLVM_VERSION=210108
CONFIG_RUSTC_HAS_COERCE_POINTEE=y
CONFIG_RUSTC_HAS_SPAN_FILE=y
CONFIG_RUSTC_HAS_UNNECESSARY_TRANSMUTES=y
CONFIG_RUSTC_HAS_FILE_WITH_NUL=y
CONFIG_RUSTC_HAS_FILE_AS_C_STR=y
CONFIG_RUST=y
CONFIG_RUSTC_VERSION_TEXT="rustc 1.93.1 (01f6ddf75 2026-02-11) (built from a source tarball)"
CONFIG_RUSTC_SUPPORTS_ARM64=y
CONFIG_SAMPLES_RUST=y
CONFIG_SAMPLE_RUST_MINIMAL=m
CONFIG_SAMPLE_RUST_PRINT=m
CONFIG_RUST_OVERFLOW_CHECKS=y
```

출력에서 볼 것:

- `CONFIG_RUSTC_VERSION=109301`은 1.93.1, `CONFIG_CLANG_VERSION=210108`과 `CONFIG_RUSTC_LLVM_VERSION=210108`은 **둘 다 LLVM 21.1.8**이다. rustc도 내부에 LLVM 백엔드를 갖고 있으므로, clang과 rustc가 같은 LLVM이면 C와 Rust 오브젝트가 같은 코드 생성기·같은 ABI 가정으로 만들어진다(cross-language LTO 같은 기능은 이 일치를 요구한다).
- `CONFIG_RUSTC_HAS_...` 줄들은 Kconfig가 **rustc 버전을 보고 기능 유무를 자동 판별**한 결과다. 같은 커널 소스라도 rustc 버전이 바뀌면 `.config`가 달라진다 — 재현 가능한 빌드를 원한다면 rustc 버전을 고정해야 하는 이유다.
- `CONFIG_RUST_OVERFLOW_CHECKS=y` — 정수 오버플로를 런타임에 검사해 panic(= `BUG()`)한다. C의 조용한 wraparound와 다르다.

그리고 빌드. 이 VM은 다른 작업과 CPU를 나눠 쓰고 있어서(load average 15 안팎) 빌드 시간을 줄이려고 defconfig에서 **모든 `=m` 드라이버와 ARCH_ 플랫폼(VEXPRESS 제외)을 끄고**, KVM·DRM·사운드·USB 등을 꺼서 QEMU `virt`에 필요한 built-in만 남겼다(`virt`는 PL011, virtio-mmio, GIC만 있으면 된다). Rust 샘플만 `=m`으로 다시 켰다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E8/build && sed -i -E "s/^(CONFIG_[A-Z0-9_]+)=m$/# \1 is not set/" .config && for a in $(grep -oE "^CONFIG_ARCH_[A-Z0-9]+=y" .config | cut -d= -f1); do [ $a = CONFIG_ARCH_VEXPRESS ] || sed -i "s/^$a=y$/# $a is not set/" .config; done; S=/var/lab/src/linux-6.18.55/scripts/config; $S --file .config -d KVM -d DRM -d SOUND -d WLAN -d BT -d USB_SUPPORT -d MEDIA_SUPPORT -d MTD -d CAN -m SAMPLE_RUST_MINIMAL -m SAMPLE_RUST_PRINT -m SAMPLE_RUST_MISC_DEVICE -m SAMPLE_RUST_DRIVER_PLATFORM; make -s -C /var/lab/src/linux-6.18.55 O=$PWD LLVM=1 olddefconfig; grep "=m$" .config'
```

```text
CONFIG_SAMPLE_RUST_MINIMAL=m
CONFIG_SAMPLE_RUST_MISC_DEVICE=m
CONFIG_SAMPLE_RUST_PRINT=m
CONFIG_SAMPLE_RUST_DRIVER_PLATFORM=m
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E8 && make -C /var/lab/src/linux-6.18.55 O=/var/tmp/E8/build LLVM=1 -j4 Image modules > e8_build.log 2>&1; grep -E "RUSTC|BINDGEN|EXPORTS" e8_build.log | head -20'
```

```text
  RUSTC L rust/core.o
  BINDGEN rust/bindings/bindings_generated.rs
  BINDGEN rust/bindings/bindings_helpers_generated.rs
  RUSTC P rust/libpin_init_internal.so
  RUSTC P rust/libmacros.so
  BINDGEN rust/uapi/uapi_generated.rs
  EXPORTS rust/exports_helpers_generated.h
  EXPORTS rust/exports_core_generated.h
  RUSTC L rust/compiler_builtins.o
  RUSTC L rust/ffi.o
  RUSTC L rust/pin_init.o
  RUSTC L rust/build_error.o
  RUSTC L rust/bindings.o
  RUSTC L rust/uapi.o
  EXPORTS rust/exports_bindings_generated.h
  RUSTC L rust/kernel.o
  EXPORTS rust/exports_kernel_generated.h
```

출력에서 볼 것: C 파일 컴파일(`CC`)이 시작되기 전에 Rust 기반 crate가 순서대로 만들어진다 — `core`(Rust 표준 라이브러리의 핵심부를 커널용 타깃으로 직접 컴파일), bindgen 두 번(일반 바인딩 + helper 바인딩), proc-macro(`libmacros.so`, 호스트에서 도는 컴파일러 플러그인이라 `RUSTC P`), 그리고 `kernel`. `EXPORTS` 줄은 Rust 심볼을 `EXPORT_SYMBOL_GPL`로 내보내는 헤더 생성이다.

(위 출력은 defconfig 그대로 시작한 첫 빌드의 로그에서 뽑은 것이다. 그 빌드는 너무 느려서 중간에 멈추고 설정을 줄여 같은 출력 디렉터리에서 다시 빌드했다.) 줄인 설정으로 다시 빌드한 결과:

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E8; tail -1 e8_build.log; echo $(( $(cat e8_build_end) - $(cat e8_build_start) ))s; grep -c "  CC " e8_build.log; grep "RUSTC \[M\]" e8_build.log; ls -l build/arch/arm64/boot/Image build/Module.symvers'
```

```text
EXIT=0
1296s
2648
  RUSTC [M] samples/rust/rust_minimal.o
  RUSTC [M] samples/rust/rust_misc_device.o
  RUSTC [M] samples/rust/rust_print_main.o
  RUSTC [M] samples/rust/rust_driver_platform.o
-rw-rw-r-- 1 donh donh   762778 Oct 10 14:04 build/Module.symvers
-rwxrwxr-x 1 donh donh 21248512 Oct 10 14:04 build/arch/arm64/boot/Image
```

출력에서 볼 것: `-j4`로 약 22분(다른 작업과 CPU 경쟁 중, load average 15 안팎), C 오브젝트 2,648개. Rust 쪽은 샘플 4개를 포함해도 rustc 호출이 몇십 번 수준이다 — 커널 빌드 시간의 대부분은 여전히 C다. 다만 `rust/core.o`와 `rust/kernel.o`는 각각 **한 번의 큰 rustc 호출**이라(crate 단위 컴파일) 병렬화가 안 되고, 이 단계가 빌드 초반의 직렬 구간이 된다.

빌드 파이프라인 전체를 그림으로 보면 그림 5와 같다.

```svg
<svg viewBox="0 0 680 470" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="22" font-size="13" font-weight="bold">① 커널 빌드 때 한 번 (make LLVM=1 Image)</text>
  <rect x="10" y="34" width="170" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="95" y="58" text-anchor="middle" font-size="12">bindings_helper.h</text>
  <rect x="240" y="34" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="345" y="58" text-anchor="middle" font-size="12">bindings_generated.rs</text>
  <rect x="10" y="88" width="170" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="95" y="112" text-anchor="middle" font-size="12">rust/helpers/*.c</text>
  <rect x="240" y="88" width="210" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="345" y="112" text-anchor="middle" font-size="12">helpers.o (vmlinux 안)</text>
  <rect x="10" y="142" width="170" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="95" y="166" text-anchor="middle" font-size="12">rust/kernel/*.rs, core</text>
  <rect x="240" y="142" width="210" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="345" y="166" text-anchor="middle" font-size="12">kernel.o + libkernel.rmeta</text>
  <line x1="180" y1="54" x2="238" y2="54" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="210" y="48" text-anchor="middle" font-size="12">bindgen</text>
  <line x1="180" y1="108" x2="238" y2="108" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="210" y="102" text-anchor="middle" font-size="12">clang</text>
  <line x1="180" y1="162" x2="238" y2="162" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="210" y="156" text-anchor="middle" font-size="12">rustc</text>
  <line x1="345" y1="74" x2="345" y2="86" stroke="currentColor" stroke-dasharray="3,3"/>
  <rect x="470" y="34" width="200" height="148" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="570" y="58" text-anchor="middle" font-size="13" font-weight="bold">vmlinux / Image</text>
  <text x="570" y="82" text-anchor="middle" font-size="12">C 커널 + kernel crate</text>
  <text x="570" y="104" text-anchor="middle" font-size="12">Rust 심볼도 export</text>
  <text x="570" y="126" text-anchor="middle" font-size="12">(v0 mangling: _RNv...)</text>
  <text x="570" y="148" text-anchor="middle" font-size="12">→ Module.symvers</text>
  <line x1="450" y1="108" x2="468" y2="108" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="450" y1="162" x2="468" y2="162" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="10" y="222" font-size="13" font-weight="bold">② 모듈 빌드 (make LLVM=1 M=$PWD)</text>
  <rect x="10" y="236" width="120" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="70" y="260" text-anchor="middle" font-size="12">e8_hello.rs</text>
  <rect x="180" y="236" width="140" height="40" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="250" y="260" text-anchor="middle" font-size="12">rustc (LLVM 21)</text>
  <rect x="370" y="236" width="120" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="430" y="260" text-anchor="middle" font-size="12">e8_hello.o</text>
  <rect x="540" y="236" width="130" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="605" y="260" text-anchor="middle" font-size="12">modpost</text>
  <line x1="130" y1="256" x2="178" y2="256" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="320" y1="256" x2="368" y2="256" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="490" y1="256" x2="538" y2="256" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="570" y1="182" x2="600" y2="234" stroke="currentColor" stroke-dasharray="4,3" marker-end="url(#ah2)"/>
  <line x1="345" y1="182" x2="270" y2="234" stroke="currentColor" stroke-dasharray="4,3" marker-end="url(#ah2)"/>
  <text x="250" y="206" text-anchor="middle" font-size="12">--extern kernel</text>
  <rect x="370" y="306" width="120" height="40" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="430" y="330" text-anchor="middle" font-size="12">e8_hello.mod.o</text>
  <rect x="540" y="306" width="130" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="605" y="330" text-anchor="middle" font-size="12">e8_hello.ko</text>
  <line x1="605" y1="276" x2="440" y2="304" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="560" y="298" text-anchor="middle" font-size="12">.mod.c → clang</text>
  <line x1="490" y1="326" x2="538" y2="326" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="514" y="320" text-anchor="middle" font-size="12">ld.lld</text>
  <rect x="10" y="300" width="330" height="160" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="175" y="322" text-anchor="middle" font-size="13" font-weight="bold">rustc에 넘어가는 핵심 플래그</text>
  <text x="20" y="346" font-size="12">--target=aarch64-unknown-none-softfloat</text>
  <text x="20" y="368" font-size="12">-Cpanic=abort (unwind 없음)</text>
  <text x="20" y="390" font-size="12">--sysroot=/dev/null (std 없음)</text>
  <text x="20" y="412" font-size="12">-Coverflow-checks=y</text>
  <text x="20" y="434" font-size="12">-Zbranch-protection=pac-ret</text>
  <text x="20" y="454" font-size="12">clang과 rustc의 LLVM 버전이 같다: 21.1.8</text>
</svg>
```
그림 5 — ① 커널 빌드 때 bindgen·clang·rustc가 기반 crate를 만들어 vmlinux에 넣고 심볼을 export한다. ② 모듈 빌드 때 rustc는 `--extern kernel`로 `libkernel.rmeta`(타입 정보)를 읽어 컴파일하고, 이후 modpost → `.mod.c` → ld.lld 단계는 C 모듈과 똑같다.

모듈 하나를 빌드할 때 rustc에 실제로 넘어간 명령줄(`.e8_hello.o.cmd`, lint 플래그 일부 생략):

```text
rustc --edition=2021 -Zbinary_dep_depinfo=y ... -Dunsafe_op_in_unsafe_fn ... -Wclippy::undocumented_unsafe_blocks ...
  -Cpanic=abort -Cembed-bitcode=n -Clto=n -Cforce-unwind-tables=n -Ccodegen-units=1 -Csymbol-mangling-version=v0
  -Crelocation-model=static -Zfunction-sections=n -Wclippy::float_arithmetic --target=aarch64-unknown-none-softfloat
  -Zbranch-protection=pac-ret -Copt-level=2 -Cdebug-assertions=n -Coverflow-checks=y -Cforce-frame-pointers=y ...
  --cfg MODULE @/var/tmp/E8/build/include/generated/rustc_cfg ...
  -Zcrate-attr=no_std ... --extern pin_init --extern kernel --crate-type rlib -L /var/tmp/E8/build/rust/
  --crate-name e8_hello --sysroot=/dev/null --out-dir ./ --emit=dep-info=./.e8_hello.o.d --emit=obj=e8_hello.o e8_hello.rs
```

| 플래그 | 의미 | 커널 C 쪽 대응 |
|---|---|---|
| `--target=aarch64-unknown-none-softfloat` | OS 없는 aarch64, FPU 레지스터 안 씀 | `-mgeneral-regs-only` (커널에서 FPU 금지, E4) |
| `-Cpanic=abort` | panic 시 스택 unwind 없이 바로 handler | 커널에 C++ 예외 같은 unwind 없음 |
| `--sysroot=/dev/null`, `-Zcrate-attr=no_std` | `std` 사용 금지, `core`·`kernel`만 | libc 없음 (E4) |
| `@.../rustc_cfg` | `.config`의 `CONFIG_*`를 `--cfg`로 전달 | `#include <generated/autoconf.h>` |
| `-Csymbol-mangling-version=v0` | Rust v0 심볼 이름 규칙 (`_RNv...`) | C는 이름 그대로 |
| `-Coverflow-checks=y` | 정수 오버플로 검사 | (없음 — C는 wraparound) |
| `-Zbranch-protection=pac-ret` | ARMv8.3 PAC 반환 주소 보호 | C의 `-mbranch-protection` 과 맞춤 |
| `-Dunsafe_op_in_unsafe_fn` | `unsafe fn` 안에서도 `unsafe {}` 블록 강제 | (없음) |

`-Z` 플래그가 많다는 점에 주목하자. `-Z`는 **unstable(nightly 전용) 옵션**인데 stable rustc에서 쓰기 위해 커널 빌드가 `RUSTC_BOOTSTRAP=1`을 설정한다. 커널이 아직 Rust의 unstable 기능에 기대고 있다는 뜻이고, Rust 프로젝트와 커널이 이 기능들을 안정화하는 작업을 함께 하고 있다.

---

## 5. 실습 1 — `samples/rust`를 QEMU에서 insmod

### 5.1 샘플 소스 — `rust_minimal`

먼저 소스를 읽는다. 6.18.55의 `samples/rust/rust_minimal.rs` 전체다.

```rust
// SPDX-License-Identifier: GPL-2.0

//! Rust minimal sample.

use kernel::prelude::*;

module! {
    type: RustMinimal,
    name: "rust_minimal",
    authors: ["Rust for Linux Contributors"],
    description: "Rust minimal sample",
    license: "GPL",
}

struct RustMinimal {
    numbers: KVec<i32>,
}

impl kernel::Module for RustMinimal {
    fn init(_module: &'static ThisModule) -> Result<Self> {
        pr_info!("Rust minimal sample (init)\n");
        pr_info!("Am I built-in? {}\n", !cfg!(MODULE));

        let mut numbers = KVec::new();
        numbers.push(72, GFP_KERNEL)?;
        numbers.push(108, GFP_KERNEL)?;
        numbers.push(200, GFP_KERNEL)?;

        Ok(RustMinimal { numbers })
    }
}

impl Drop for RustMinimal {
    fn drop(&mut self) {
        pr_info!("My numbers are {:?}\n", self.numbers);
        pr_info!("Rust minimal sample (exit)\n");
    }
}
```

C 모듈(E3)과 비교하면:

| C 모듈 | Rust 모듈 |
|---|---|
| `module_init(foo_init)` + `MODULE_LICENSE("GPL")` | `module! { type: ..., license: "GPL" }` |
| 전역 변수에 상태 저장 (`static u16 *buf;`) | `init()`이 돌려주는 **구조체 값이 곧 모듈 상태** |
| `module_exit()`에서 손으로 `kfree` | 모듈 구조체의 `Drop`이 자동으로 불림, 필드(`KVec`)도 자동 해제 |
| `return -ENOMEM;` + goto 정리 | `push(..)?` — 실패하면 이미 만든 것은 Drop되고 `-ENOMEM` 반환 |
| `printk(KERN_INFO "...%d", x)` | `pr_info!("...{}", x)` — 포맷 문자열을 컴파일 타임에 타입 검사 |

`pr_info!`가 포맷 인자의 타입을 컴파일 때 검사한다는 점이 사소해 보여도 중요하다. C의 `printk("%s", int_value)`는 `-Wformat`이 잡아 주지만 커스텀 `%p` 확장 등에서는 놓치는 경우가 있고, 잘못되면 커널에서 잘못된 주소를 읽는다.

### 5.2 예제 12 — 샘플 세 개를 QEMU에서 로드

무엇을 확인하나: 내가 빌드한 Rust 커널 `Image`로 QEMU를 띄우고, 빌드된 샘플 `.ko`를 initramfs에 넣어 `insmod`/`rmmod`해서 dmesg를 본다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E8 && rm -rf e8_rootfs && mkdir e8_rootfs && cp -a /var/lab/build/rootfs/. e8_rootfs/ && cp build/samples/rust/*.ko e8_rootfs/ && (cd e8_rootfs && find . | cpio -o -H newc --quiet | gzip) > e8_initrd.gz && ls -l build/samples/rust/*.ko'
```

```text
-rw-rw-r-- 1 donh donh 212504 Oct 10 14:04 build/samples/rust/rust_driver_platform.ko
-rw-rw-r-- 1 donh donh 154840 Oct 10 14:04 build/samples/rust/rust_minimal.ko
-rw-rw-r-- 1 donh donh 201312 Oct 10 14:04 build/samples/rust/rust_misc_device.ko
-rw-rw-r-- 1 donh donh 168640 Oct 10 14:04 build/samples/rust/rust_print.ko
```

게스트에서 돌릴 스크립트(initramfs 루트에 함께 넣는다). `rust_driver_platform`도 같이 넣고, D 모듈 방식대로 랩 `virt.dts`를 `/var/tmp/E8/e8_virt.dts`로 복사해 루트 노드 끝(마지막 `};` 바로 앞)에 이 샘플의 compatible을 가진 노드를 추가한 뒤 `-dtb`로 넘겼다. 샘플 소스가 읽는 속성 이름(`test,bool-prop`, `test,u32-prop`, `test,i16-array`)을 grep해서 그대로 넣었다.

```sh
#!/bin/sh
# e8_run_samples.sh: runs inside the QEMU guest (BusyBox)
echo "== guest $(uname -r): $(zcat /proc/config.gz | grep '^CONFIG_RUST=')"
dmesg -n 1                       # keep console quiet; read the log with dmesg
insmod /rust_minimal.ko && rmmod rust_minimal
insmod /rust_print.ko && rmmod rust_print
insmod /rust_driver_platform.ko
echo "== bound devices:"; ls /sys/bus/platform/drivers/rust_driver_platform/ | cat
echo "== dmesg"
dmesg | grep -E "rust_minimal|rust_print|rust_driver_platform"
```

```dts
	e8-rust-dev@9300000 {
		compatible = "test,rust-device";
		reg = <0x0 0x09300000 0x0 0x1000>;
		test,bool-prop;
		test,u32-prop = <0xdeadbeef>;
		test,i16-array = /bits/ 16 <1 2 (-3) 4>;
	};
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E8 && dtc -I dts -O dtb -o e8_virt.dtb e8_virt.dts && cp /Users/donh/.elinux_scratch/E8/e8_run_samples.sh e8_rootfs/ && (cd e8_rootfs && find . | cpio -o -H newc --quiet | gzip) > e8_initrd.gz && timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -kernel build/arch/arm64/boot/Image -initrd e8_initrd.gz -dtb e8_virt.dtb -append "console=ttyAMA0 quiet LAB_CMD=sh~/e8_run_samples.sh"'
```

```text
lab initramfs: 6.18.55 up 0.56s
== guest 6.18.55: CONFIG_RUST=y
[    0.770203] rust_print: Emergency message (level 0) without args
[    0.770642] rust_print: Emergency message (level 0) with args
== bound devices:
9300000.e8-rust-dev
bind
module
uevent
unbind
== dmesg
[    0.750884] rust_minimal: Rust minimal sample (init)
[    0.751154] rust_minimal: Am I built-in? false
[    0.757604] rust_minimal: My numbers are [72, 108, 200]
[    0.757925] rust_minimal: Rust minimal sample (exit)
[    0.770179] rust_print: Rust printing macros sample (init)
[    0.770203] rust_print: Emergency message (level 0) without args
[    0.770212] rust_print: Alert message (level 1) without args
...
[    0.770253] rust_print: A line that is continued without args
...
[    0.770749] rust_print: 1
[    0.770776] rust_print: "hello, world"
[    0.770951] rust_print: [/var/lab/src/linux-6.18.55/samples/rust/rust_print_main.rs:35:5] c = "hello, world"
[    0.771063] rust_print: Arc<dyn Display> says 42
[    0.771082] rust_print: Arc<dyn Display> says hello, world
[    0.771137] rust_print: "hello, world"
[    0.777387] rust_print: Rust printing macros sample (exit)
[    0.790474] rust_driver_platform 9300000.e8-rust-dev: Probed with info: '42'.
[    0.790766] rust_driver_platform 9300000.e8-rust-dev: matched compatible string idx = 0
[    0.791082] rust_driver_platform 9300000.e8-rust-dev: 'compatible'='"test,rust-device"'
[    0.791254] rust_driver_platform 9300000.e8-rust-dev: 'test,bool-prop'='true'
[    0.791315] rust_driver_platform 9300000.e8-rust-dev: 'test,u32-prop' is present
[    0.791363] rust_driver_platform 9300000.e8-rust-dev: 'test,u32-optional-prop'='0x12' (default = 0x12)
[    0.791472] rust_driver_platform 9300000.e8-rust-dev: /e8-rust-dev@9300000: property 'test,u32-required-prop' is missing
[    0.791677] rust_driver_platform 9300000.e8-rust-dev: 'test,u32-prop'='0xdeadbeef'
[    0.791860] rust_driver_platform 9300000.e8-rust-dev: 'test,i16-array'='[1, 2, -3, 4]'
[    0.792029] rust_driver_platform 9300000.e8-rust-dev: 'test,i16-array' length is 4
[    0.792094] rust_driver_platform 9300000.e8-rust-dev: 'test,i16-array'='[1, 2, -3, 4]' (KVec)
[    0.842476] reboot: Power down
```

(`rust_print`의 레벨별 메시지 일부를 `...`로 줄였다. 타임스탬프는 실행마다 다르다.)

출력에서 볼 것:

- `rust_minimal`: init에서 `KVec`에 넣은 숫자가 exit(`Drop`)에서 그대로 출력됐다. `rmmod` 때 `Drop`이 불리고, 그 뒤 `KVec`의 메모리도 자동 해제된다(`kfree` 호출 코드가 소스에 없다).
- `rust_print`: `pr_emerg!`(레벨 0)만 콘솔에 바로 찍혔다(`dmesg -n 1`로 콘솔 레벨을 낮춰 둔 결과). `dbg!` 매크로는 파일·줄 번호를 자동으로 붙인다. `Arc<dyn Display>`처럼 트레이트 객체(=C의 함수 포인터 테이블)도 커널에서 쓸 수 있다.
- `rust_driver_platform`: **D3 노트의 매칭 경로가 Rust 드라이버에도 그대로** 적용됐다 — DT 노드 `e8-rust-dev@9300000`이 platform_device가 되고, `of_device_table!`의 `"test,rust-device"`와 매칭되어 `probe()`가 불렸다(`Probed with info: '42'`는 매칭 표 항목에 붙은 데이터). 이어서 Rust `fwnode` API로 DT 속성을 읽었다: 없는 선택 속성은 기본값 `0x12`, 없는 필수 속성(`test,u32-required-prop`)은 `required_by(dev)`가 에러 메시지를 찍는다. sysfs에도 C 드라이버와 똑같이 `bind`/`unbind`가 있다.

---

## 6. 실습 2 — 내 모듈 `e8_hello` (KVec + GFP, `?`, pinned Mutex)

### 6.1 소스

목표: 센서 키트의 "채널 8개를 읽어 버퍼에 담아 두는" 모듈을 흉내 낸다. 이 작은 모듈에 지금까지 본 요소를 다 넣었다.

- `module!` — 6.18에 `params`가 없어서 채널 수는 상수.
- `read_sample()` — 잘못된 채널이면 `Err(EINVAL)`.
- `fill()` — `KVec::with_capacity(n, GFP_KERNEL)?`로 할당(실패 시 `-ENOMEM`), 루프 안에서 `read_sample(ch)?`로 에러 전파.
- 모듈 상태는 `Mutex<KVec<u16>>` — 뮤텍스가 들어 있으므로 `#[pin_data]` + `InPlaceModule` + `new_mutex!`로 제자리 초기화.
- `PinnedDrop` — 언로드 때 락을 잡고 내용 출력(락은 guard가 drop될 때 자동 해제).

```rust
// SPDX-License-Identifier: GPL-2.0
//! e8_hello: KVec + GFP flags, `?` error propagation, a pinned Mutex.
use kernel::prelude::*;
use kernel::{new_mutex, sync::Mutex};

module! {
    type: E8Hello,
    name: "e8_hello",
    authors: ["Don"],
    description: "E8 Rust-for-Linux hello",
    license: "GPL",
}

const NR_SAMPLES: usize = 8; // 6.18 module! has no `params:` key

// Pretend sensor read: a bad channel is -EINVAL.
fn read_sample(ch: usize) -> Result<u16> {
    if ch >= NR_SAMPLES {
        return Err(EINVAL);
    }
    Ok((ch as u16) * 100 + 7)
}

fn fill(n: usize) -> Result<KVec<u16>> {
    let mut v = KVec::with_capacity(n, GFP_KERNEL)?; // AllocError -> ENOMEM
    for ch in 0..n {
        v.push(read_sample(ch)?, GFP_KERNEL)?; // any Err returns early
    }
    Ok(v)
}

#[pin_data(PinnedDrop)]
struct E8Hello {
    #[pin]
    samples: Mutex<KVec<u16>>, // struct mutex inside: must not move
}

impl kernel::InPlaceModule for E8Hello {
    fn init(_m: &'static ThisModule) -> impl PinInit<Self, Error> {
        pr_info!("e8_hello: init, NR_SAMPLES={}\n", NR_SAMPLES);
        if let Err(e) = read_sample(99) {
            let n = e.to_errno();
            pr_info!("e8_hello: read_sample(99) -> {:?} = errno {}\n", e, n);
        }
        let v = fill(NR_SAMPLES);
        try_pin_init!(Self { samples <- new_mutex!(v?) })
    }
}

#[pinned_drop]
impl PinnedDrop for E8Hello {
    fn drop(self: Pin<&mut Self>) {
        let s = self.samples.lock();
        let (len, cap) = (s.len(), s.capacity());
        pr_info!("e8_hello: exit len={len} cap={cap} {:?}\n", &s[..]);
    }
}
```

```make
# Kbuild — C 모듈과 똑같다. kbuild가 e8_hello.rs를 찾아 rustc로 컴파일한다.
obj-m += e8_hello.o
```

읽는 요령:

- `try_pin_init!(Self { samples <- new_mutex!(v?) })` — `<-`는 "이 필드는 initializer로 제자리 초기화"라는 뜻이다. `v?`가 `Err`이면 initializer 전체가 에러가 되어 `init_module()`이 음수 errno를 돌려준다.
- `#[pin_data(PinnedDrop)]` + `#[pinned_drop]` — pinned 구조체는 일반 `Drop`을 쓸 수 없고(`&mut Self`를 받으면 이동이 가능해지므로) `Pin<&mut Self>`를 받는 `PinnedDrop`을 쓴다.
- `rustfmt --check`와 `CLIPPY=1` 둘 다 경고 0개로 통과시켰다(커널 Rust 코드는 `make rustfmtcheck`로 포맷을 강제한다).

### 6.2 예제 13 — 빌드: `make LLVM=1 M=...`

무엇을 확인하나: C 모듈과 같은 kbuild 명령으로 Rust 모듈이 빌드되고, `.ko`에 어떤 메타데이터가 들어가는지 본다. 소스는 Mac 쪽 scratch에 있고 VM 홈은 읽기 전용이라 `/var/tmp/E8/mod/`로 복사해 빌드한다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E8/mod/e8_hello && make -C /var/tmp/E8/build LLVM=1 M=$PWD modules && modinfo e8_hello.ko'
```

```text
  RUSTC [M] e8_hello.o
  MODPOST Module.symvers
  CC [M]  e8_hello.mod.o
  CC [M]  .module-common.o
  LD [M]  e8_hello.ko
filename:       /var/tmp/E8/mod/e8_hello/e8_hello.ko
author:         Don
description:    E8 Rust-for-Linux hello
license:        GPL
name:           e8_hello
depends:        
vermagic:       6.18.55 SMP preempt mod_unload aarch64
```

출력에서 볼 것: `RUSTC [M]` 한 줄만 Rust이고, 그다음 `MODPOST` → `.mod.o`(clang) → `LD [M]`은 C 모듈과 똑같다. `modinfo`의 author/description/license는 `module!`의 키에서 왔다. `vermagic`도 C 모듈과 같은 형식이다 — 커널 입장에서 이것은 그냥 `.ko`다.

참고로 커널 빌드(`Module.symvers`)가 끝나기 전에 같은 명령을 돌렸을 때는 `RUSTC` 단계는 통과하고 modpost에서 이렇게 실패했다(빌드 순서를 잘못 잡았을 때 보게 되는 에러다):

```text
WARNING: /var/tmp/E8/build/Module.symvers is missing.
         Modules may not have dependencies or modversions.
...
ERROR: modpost: "_RNvNtNtCsj9bqJNU6tGA_6kernel5print14format_strings4INFO" [e8_hello.ko] undefined!
ERROR: modpost: "_RNvNtCsj9bqJNU6tGA_6kernel5print11call_printk" [e8_hello.ko] undefined!
ERROR: modpost: "rust_helper_krealloc_node_align" [e8_hello.ko] undefined!
...
```

### 6.3 예제 14 — 오브젝트가 커널에서 무엇을 가져다 쓰나 (`llvm-nm`)

무엇을 확인하나: Rust 모듈이 링크할 미해결 심볼을 보고, Rust v0 mangling 이름과 demangle된 이름을 비교한다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E8/mod/e8_hello && llvm-nm -u e8_hello.o | head -5; echo ===; llvm-nm -u e8_hello.o | llvm-cxxfilt'
```

```text
                 U _RNvMs5_NtNtCscIHVMyPnRZn_4core3fmt8buildersNtB5_9DebugList5entry
                 U _RNvMs5_NtNtCscIHVMyPnRZn_4core3fmt8buildersNtB5_9DebugList6finish
                 U _RNvMsa_NtCscIHVMyPnRZn_4core3fmtNtB5_9Formatter10debug_list
                 U _RNvNtCsj9bqJNU6tGA_6kernel5print11call_printk
                 U _RNvNtNtCscIHVMyPnRZn_4core9panicking11panic_const24panic_const_sub_overflow
===
                 U <core::fmt::builders::DebugList>::entry
                 U <core::fmt::builders::DebugList>::finish
                 U <core::fmt::Formatter>::debug_list
                 U kernel::print::call_printk
                 U core::panicking::panic_const::panic_const_sub_overflow
                 U kernel::print::format_strings::INFO
                 U <kernel::sync::lock::mutex::MutexBackend as kernel::sync::lock::Backend>::assert_is_held
                 U <kernel::sync::lock::mutex::MutexBackend as kernel::sync::lock::Backend>::init
                 U <kernel::sync::lock::mutex::MutexBackend as kernel::sync::lock::Backend>::lock
                 U <kernel::sync::lock::mutex::MutexBackend as kernel::sync::lock::Backend>::unlock
                 U <u16 as core::fmt::Display>::fmt
                 U <i32 as core::fmt::Display>::fmt
                 U <kernel::error::Error as core::fmt::Debug>::fmt
                 U <usize as core::fmt::Display>::fmt
                 U <u16 as core::fmt::LowerHex>::fmt
                 U <u16 as core::fmt::UpperHex>::fmt
                 U rust_helper_krealloc_node_align
```

출력에서 볼 것:

- 모듈은 C 함수 `mutex_lock`을 직접 부르지 않고 `kernel` crate의 `MutexBackend::lock`을 부른다(그림 1의 층 구조 그대로). 반면 `KVec` 할당은 인라인되어 helper `rust_helper_krealloc_node_align`을 직접 부른다.
- `panic_const_sub_overflow`가 보인다 — `-Coverflow-checks=y` 때문에 뺄셈마다 오버플로 검사 + panic 경로가 들어간다.
- 이 심볼들은 vmlinux가 export해야 링크된다. **Rust 심볼 이름에는 crate 해시(`Csj9bqJNU6tGA_6kernel`)가 들어 있다.** 커널을 다른 rustc나 다른 설정으로 다시 빌드하면 해시가 바뀌어서, 예전에 빌드한 Rust `.ko`는 심볼을 못 찾는다. Rust 모듈은 C 모듈보다 훨씬 강하게 "그 커널 빌드 하나"에 묶여 있다는 뜻이다.

### 6.4 예제 15 — QEMU에서 로드하고, 실패 경로(`e8_fail`)도 보기

`init()`이 `Err`을 돌려주면 유저 공간의 `insmod`가 어떤 에러를 보는지 확인하려고 아주 작은 모듈을 하나 더 만들었다.

```rust
// SPDX-License-Identifier: GPL-2.0
//! e8_fail: init returns Err -> insmod sees a negative errno.
use kernel::prelude::*;

module! {
    type: E8Fail,
    name: "e8_fail",
    description: "E8 init error path",
    license: "GPL",
}

struct E8Fail;

fn probe_sensor(present: bool) -> Result {
    if !present {
        pr_err!("e8_fail: sensor not found\n");
        return Err(ENODEV);
    }
    Ok(())
}

impl kernel::Module for E8Fail {
    fn init(_m: &'static ThisModule) -> Result<Self> {
        probe_sensor(false)?; // early return with ENODEV
        pr_info!("e8_fail: never printed\n");
        Ok(E8Fail)
    }
}
```

무엇을 확인하나: `e8_hello`의 init/exit 메시지와 에러 값 출력, 그리고 `e8_fail`이 `-ENODEV`로 로드 실패하는 것을 게스트에서 본다.

게스트 스크립트:

```sh
#!/bin/sh
# e8_run_mine.sh: runs inside the QEMU guest (BusyBox)
dmesg -n 1
insmod /e8_hello.ko; echo "insmod e8_hello -> $?"
grep e8_hello /proc/modules
rmmod e8_hello
insmod /e8_fail.ko; echo "insmod e8_fail -> $?"
insmod /e8_chello.ko && rmmod e8_chello
echo "== dmesg"
dmesg | grep -E "e8_"
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E8 && rm -rf e8_rootfs2 && mkdir e8_rootfs2 && cp -a /var/lab/build/rootfs/. e8_rootfs2/ && cp mod/e8_hello/e8_hello.ko mod/e8_fail/e8_fail.ko mod/e8_chello/e8_chello.ko /Users/donh/.elinux_scratch/E8/e8_run_mine.sh e8_rootfs2/ && (cd e8_rootfs2 && find . | cpio -o -H newc --quiet | gzip) > e8_initrd2.gz && timeout 120 qemu-system-aarch64 -machine virt -cpu cortex-a57 -m 512M -nographic -kernel build/arch/arm64/boot/Image -initrd e8_initrd2.gz -append "console=ttyAMA0 quiet LAB_CMD=sh~/e8_run_mine.sh"'
```

```text
lab initramfs: 6.18.55 up 0.63s
insmod e8_hello -> 0
e8_hello 12288 0 - Live 0xffffcb568f60e000 (O)
insmod: can't insert '/e8_fail.ko': No such device
insmod e8_fail -> 1
== dmesg
[    0.000000] Kernel command line: console=ttyAMA0 quiet LAB_CMD=sh~/e8_run_mine.sh
[    0.000000] Unknown kernel command line parameters "LAB_CMD=sh~/e8_run_mine.sh", will be passed to user space.
[    0.581821]     LAB_CMD=sh~/e8_run_mine.sh
[    0.751702] e8_hello: loading out-of-tree module taints kernel.
[    0.756383] e8_hello: e8_hello: init, NR_SAMPLES=8
[    0.756691] e8_hello: e8_hello: read_sample(99) -> EINVAL = errno -22
[    0.776738] e8_hello: e8_hello: exit len=8 cap=8 [7, 107, 207, 307, 407, 507, 607, 707]
[    0.793135] e8_fail: e8_fail: sensor not found
[    0.813710] e8_chello: init, NR_SAMPLES=8
[    0.813746] e8_chello: read_sample(99) -> -22
[    0.820034] e8_chello: exit, len=8 first=7 last=707
[    0.858901] reboot: Power down
```

(모듈 주소·타임스탬프는 실행마다 다르다. `e8_fail.rs`의 `description:` 키는 처음에 빼먹었다가 modpost가 `WARNING: modpost: missing MODULE_DESCRIPTION() in e8_fail.o`를 내서 넣었다 — C의 `MODULE_DESCRIPTION()` 경고 규칙이 Rust 모듈에도 똑같이 적용된다.)

출력에서 볼 것:

- `read_sample(99) -> EINVAL = errno -22` — `Error`의 `Debug` 출력은 이름(`EINVAL`), `to_errno()`는 C와 같은 `-22`다. C 쌍둥이도 `-22`를 찍었다.
- exit 때 `len=8 cap=8`과 값 8개 — `fill()`의 `?` 경로를 하나도 타지 않고 다 채워졌고, `PinnedDrop`에서 락을 잡고 읽었다. 락 해제와 `KVec` 해제(`kfree`)는 코드에 없지만 일어났다.
- `e8_fail`: `probe_sensor(false)?`가 `Err(ENODEV)`를 위로 올렸고, `module!`의 `init_module()`이 `-19`를 돌려줘서 BusyBox `insmod`가 **"No such device"**를 출력했다. `never printed` 줄은 정말 안 나왔다. 실패한 init이므로 `Drop`도 불리지 않는다(만들어진 값이 없다).
- **메시지 앞에 모듈 이름이 두 번** 붙었다(`e8_hello: e8_hello: ...`). Rust의 `pr_info!`는 C의 `pr_fmt`처럼 **모듈 이름 접두어를 자동으로** 붙이기 때문이다. 그러니 Rust 모듈에서는 메시지에 이름을 직접 쓰지 않는다 — 첫 Rust 모듈에서 흔히 하는 실수다(C 쌍둥이는 `pr_fmt`를 정의하지 않아서 접두어가 한 번만 나왔다).
- `taints kernel`과 `(O)` 표시는 out-of-tree 모듈이라서이고 Rust와는 무관하다(E3).

---

## 7. 실습 3 — 같은 모듈을 C와 Rust로: 크기와 빌드 시간

### 7.1 C 쌍둥이 `e8_chello`

`e8_hello`와 같은 일을 하는 C 모듈이다(같은 Rust 커널 트리, 같은 `LLVM=1`로 빌드해서 비교 조건을 맞춘다).

```c
// SPDX-License-Identifier: GPL-2.0
/* e8_chello: the C twin of e8_hello (kmalloc_array, -EINVAL, mutex) */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#define NR_SAMPLES 8

static DEFINE_MUTEX(e8_lock);
static u16 *e8_samples;
static size_t e8_len;

static int read_sample(size_t ch, u16 *out)
{
	if (ch >= NR_SAMPLES)
		return -EINVAL;
	*out = ch * 100 + 7;
	return 0;
}

static int __init e8_chello_init(void)
{
	u16 dummy;
	size_t ch;
	int ret;

	pr_info("e8_chello: init, NR_SAMPLES=%d\n", NR_SAMPLES);
	pr_info("e8_chello: read_sample(99) -> %d\n", read_sample(99, &dummy));

	e8_samples = kmalloc_array(NR_SAMPLES, sizeof(*e8_samples), GFP_KERNEL);
	if (!e8_samples)
		return -ENOMEM;
	for (ch = 0; ch < NR_SAMPLES; ch++) {
		ret = read_sample(ch, &e8_samples[ch]);
		if (ret)
			goto err_free;
	}
	e8_len = NR_SAMPLES;
	return 0;

err_free:
	kfree(e8_samples);
	return ret;
}

static void __exit e8_chello_exit(void)
{
	mutex_lock(&e8_lock);
	pr_info("e8_chello: exit, len=%zu first=%u last=%u\n",
		e8_len, e8_samples[0], e8_samples[e8_len - 1]);
	kfree(e8_samples);
	mutex_unlock(&e8_lock);
}

module_init(e8_chello_init);
module_exit(e8_chello_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("E8 C twin of e8_hello");
```

C 버전에서 눈여겨볼 곳: `goto err_free` 정리 경로, `e8_samples`를 보호한다는 사실이 코드 어디에도 강제되지 않는 `e8_lock`, `e8_len - 1`에서 `e8_len`이 0이면 생기는 wraparound. 셋 다 Rust 버전에서는 각각 `Drop`, `Mutex<KVec>`, overflow check/bounds check로 처리된다.

### 7.2 예제 16 — 측정

무엇을 확인하나: 두 모듈의 `.ko` 파일 크기, 섹션 크기(`llvm-size`), 그리고 `make clean` 후 다시 빌드하는 시간을 5번씩 잰다. VM이 다른 작업과 CPU를 나눠 쓰고 있어서 시간은 흔들린다 — 그래서 중앙값을 쓴다.

```sh
#!/bin/bash
# e8_measure.sh: .ko size + clean build time, C vs Rust (median of 5)
K=/var/tmp/E8/build
for m in e8_chello e8_hello; do
  cd /var/tmp/E8/mod/$m || exit 1
  ts=()
  for i in 1 2 3 4 5; do
    make -s -C $K LLVM=1 M=$PWD clean
    s=$(date +%s.%N)
    make -s -C $K LLVM=1 M=$PWD modules >/dev/null 2>&1 || { echo build failed; exit 1; }
    e=$(date +%s.%N)
    ts+=("$(echo "$e - $s" | bc)")
  done
  med=$(printf "%s\n" "${ts[@]}" | sort -n | sed -n 3p)
  printf "%-10s ko=%6d bytes  build_median=%.2fs  all=%s\n" $m \
    "$(stat -c %s $m.ko)" "$med" "$(printf "%.2f " "${ts[@]}")"
  llvm-size $m.o $m.ko | sed 1d
done
```

```sh
limactl shell elinux -- bash -lc 'bash /Users/donh/.elinux_scratch/E8/e8_measure.sh'
```

```text
e8_chello  ko=137808 bytes  build_median=1.64s  all=1.76 1.93 1.64 1.45 1.63 
    453	     48	      9	    510	    1fe	e8_chello.o
    613	   1136	      9	   1758	    6de	e8_chello.ko
e8_hello   ko=164928 bytes  build_median=1.25s  all=1.19 1.47 1.36 1.21 1.25 
   1667	      0	     56	   1723	    6bb	e8_hello.o
   1826	   1088	     56	   2970	    b9a	e8_hello.ko
```

(`llvm-size` 열은 `text data bss dec hex filename` 순서다.) `.ko` 전체 크기는 디버그 정보가 대부분이라 디버그 정보를 뺀 크기도 쟀다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E8/mod && for m in e8_chello e8_hello; do llvm-strip --strip-debug -o /tmp/E8/$m.stripped.ko $m/$m.ko; printf "%-10s full=%6d  strip-debug=%6d\n" $m $(stat -c %s $m/$m.ko) $(stat -c %s /tmp/E8/$m.stripped.ko); done'
```

```text
e8_chello  full=137808  strip-debug=  6432
e8_hello   full=164928  strip-debug= 11816
```

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <text x="10" y="24" font-size="13" font-weight="bold">strip-debug .ko 크기 (bytes)</text>
  <text x="140" y="54" text-anchor="end" font-size="12">C e8_chello</text>
  <rect x="150" y="36" width="228.6" height="26" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="386.6" y="54" font-size="12">6,432</text>
  <text x="140" y="86" text-anchor="end" font-size="12">Rust e8_hello</text>
  <rect x="150" y="68" width="420.0" height="26" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="578.0" y="86" font-size="12">11,816</text>
  <line x1="150" y1="30" x2="150" y2="100" stroke="currentColor"/>
  <text x="10" y="144" font-size="13" font-weight="bold">.ko .text 크기 (bytes)</text>
  <text x="140" y="174" text-anchor="end" font-size="12">C e8_chello</text>
  <rect x="150" y="156" width="141.0" height="26" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="299.0" y="174" font-size="12">613</text>
  <text x="140" y="206" text-anchor="end" font-size="12">Rust e8_hello</text>
  <rect x="150" y="188" width="420.0" height="26" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="578.0" y="206" font-size="12">1,826</text>
  <line x1="150" y1="150" x2="150" y2="220" stroke="currentColor"/>
  <text x="10" y="264" font-size="13" font-weight="bold">clean 빌드 시간 중앙값 (s)</text>
  <text x="140" y="294" text-anchor="end" font-size="12">C e8_chello</text>
  <rect x="150" y="276" width="420.0" height="26" fill="#4a7bd0" fill-opacity="0.75" stroke="#4a7bd0"/>
  <text x="578.0" y="294" font-size="12">1.64</text>
  <text x="140" y="326" text-anchor="end" font-size="12">Rust e8_hello</text>
  <rect x="150" y="308" width="320.1" height="26" fill="#e08a3c" fill-opacity="0.75" stroke="#e08a3c"/>
  <text x="478.1" y="326" font-size="12">1.25</text>
  <line x1="150" y1="270" x2="150" y2="340" stroke="currentColor"/>
</svg>
```
그림 6 — 같은 기능의 C 모듈과 Rust 모듈. 위 두 막대는 크기(Rust가 `.text` 약 3배, 디버그 정보 뺀 `.ko` 약 1.8배), 아래는 `make clean` 후 모듈 빌드 시간의 5회 중앙값(VM이 다른 작업과 CPU를 나눠 쓰는 중이라 ±0.3초 정도 흔들린다).

출력에서 볼 것:

- **코드 크기**: Rust 쪽 `.text`가 1,826 B로 C(613 B)의 약 3배다. 원인은 앞에서 본 것들이다 — 포맷 출력(`{:?}`로 `KVec` 전체를 찍는 `core::fmt` 경로), overflow check와 bounds check의 panic 경로, pin-init의 에러 처리 코드. 기능을 더 넣으면 비율은 줄어든다(공통 코드는 vmlinux의 `kernel`/`core`에 한 번만 있다). 임베디드 관점에서 수 KB 차이는 보통 문제가 아니지만, **vmlinux 쪽에 `core.o`(약 1.1 MB 오브젝트)와 `kernel.o`가 추가**되는 것은 플래시가 작은 보드라면 따져 봐야 한다.
- **빌드 시간**: 오히려 Rust 모듈이 조금 빨랐다(1.25 s vs 1.64 s). C 파일 하나는 `linux/module.h`가 끌고 오는 수백 개 헤더를 매번 전처리·파싱하지만, rustc는 미리 만들어 둔 `libkernel.rmeta`(타입 정보)만 읽기 때문으로 보인다. 다만 이것은 모듈 하나의 이야기이고, `kernel` crate 자체를 다시 컴파일해야 하는 커널 빌드에서는 반대로 Rust 쪽이 직렬 병목이 된다(§4.2).

---

## 8. 실습 4 — 커널 모듈에서 컴파일 타임에 잡히는 버그

§1에서는 유저 공간 Rust로 보았다. 이번에는 **커널 모듈 빌드**(`make LLVM=1 M=...`)에서 같은 종류의 버그가 거절되는 것을 본다. C 드라이버에서 실제로 흔한 두 패턴을 넣었다: 인터럽트 핸들러와 process context가 공유하는 전역 카운터(`static mut`), 그리고 버퍼를 해제한 뒤 그 안을 가리키는 포인터를 쓰는 UAF.

### 8.1 예제 17 — `e8_bad`

```rust
// SPDX-License-Identifier: GPL-2.0
//! e8_bad: patterns that are legal C but rejected by rustc.
use kernel::prelude::*;

module! {
    type: E8Bad,
    name: "e8_bad",
    license: "GPL",
}

static mut IRQ_COUNT: u32 = 0; // global shared by IRQ and process context

struct E8Bad;

impl kernel::Module for E8Bad {
    fn init(_m: &'static ThisModule) -> Result<Self> {
        // (1) data race on a global: plain `static mut` access
        IRQ_COUNT += 1;

        // (2) use-after-free: keep a reference, then free the buffer
        let mut buf = KVec::new();
        buf.push(0xAAu8, GFP_KERNEL)?;
        let first = &buf[0];
        drop(buf);
        pr_info!("first={}\n", first);
        Ok(E8Bad)
    }
}
```

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/E8/mod/e8_bad && make -C /var/tmp/E8/build LLVM=1 M=$PWD modules'
```

```text
  RUSTC [M] e8_bad.o
error[E0133]: use of mutable static is unsafe and requires unsafe block
  --> e8_bad.rs:18:9
   |
18 |         IRQ_COUNT += 1;
   |         ^^^^^^^^^ use of mutable static
   |
   = note: mutable statics can be mutated by multiple threads: aliasing violations or data races will cause undefined behavior

error[E0505]: cannot move out of `buf` because it is borrowed
  --> e8_bad.rs:24:14
   |
21 |         let mut buf = KVec::new();
   |             ------- binding `buf` declared here
22 |         buf.push(0xAAu8, GFP_KERNEL)?;
23 |         let first = &buf[0];
   |                      --- borrow of `buf` occurs here
24 |         drop(buf);
   |              ^^^ move out of `buf` occurs here
25 |         pr_info!("first={}\n", first);
   |                                ----- borrow later used here

error: aborting due to 2 previous errors

Some errors have detailed explanations: E0133, E0505.
For more information about an error, try `rustc --explain E0133`.
make[3]: *** [/var/lab/src/linux-6.18.55/scripts/Makefile.build:354: e8_bad.o] Error 1
```

출력에서 볼 것:

- E0133: 전역 가변 변수 접근은 그 자체로 `unsafe`다. 컴파일러가 붙인 설명("mutable statics can be mutated by multiple threads: ... data races will cause undefined behavior")이 정확히 커널 드라이버의 전역 카운터 문제다. `unsafe {}`로 감싸면 컴파일은 되지만, 그러면 SAFETY 주석에 "왜 race가 없는지"를 써야 하고 리뷰어가 그것을 본다. 올바른 Rust 답은 `kernel::sync::atomic` 타입이나 `SpinLock<u32>`(IRQ와 공유하면 spinlock, H 모듈)이다.
- E0505: §1.5와 같은 UAF 거절이 `KVec`(= `kmalloc`된 버퍼)에서도 똑같이 일어난다.

같은 두 패턴을 C로 쓰면(`static u32 irq_count; irq_count++;` / `kfree(buf); pr_info("%u", *first);`) `-Wall -Wextra`로도 경고가 없다(UAF는 gcc의 `-Wuse-after-free`가 아주 단순한 경우만 잡는다). 결국 KASAN/KCSAN이 켜진 테스트 커널에서 그 경로가 실행돼야 잡힌다.

---

## 9. 커널 안에서는 무슨 일이 일어나나

`insmod e8_hello.ko`부터 `rmmod`까지를 C 모듈 로더(E3)와 Rust 생성 코드를 이어서 따라가면 이렇다(영문만이라 ASCII).

```
user:   insmod e8_hello.ko  ->  finit_module(fd, "", 0)
kernel: load_module()                       kernel/module/main.c
          -> layout/relocate, resolve symbols   (Rust symbols exported by vmlinux: kernel::*, core::*)
          -> do_init_module(mod)
               -> do_one_initcall(mod->init)    mod->init == init_module   (generated by module!)
                    -> __init()
                         -> <E8Hello as InPlaceModule>::init(&THIS_MODULE)  -> impl PinInit
                         -> initer.__pinned_init(__MOD.as_mut_ptr())        build in place, in static __MOD
                              fill(): KVec::with_capacity -> krealloc_node_align (helper) -> kmalloc path
                              new_mutex!(): __mutex_init(&__MOD.samples.inner, name, key)
                         -> Ok  -> return 0          Err(e) -> return e.to_errno()  (e.g. -19 ENODEV)
user:   rmmod e8_hello  ->  delete_module()
kernel:   -> mod->exit == cleanup_module  -> __exit()
               -> drop_in_place(__MOD)  -> PinnedDrop::drop  -> Mutex guard: mutex_lock/unlock
               -> field drops: KVec -> kfree
```

드라이버 작성 관점에서 다시 만나는 지점:

- **probe/remove**(G3): Rust platform 드라이버(`kernel::platform::Driver`)의 `probe()`는 `Result<Pin<KBox<Self>>>`를 돌려주고, remove 때 그 상자가 drop된다. "probe에서 만든 것은 remove에서 해제"라는 C 규칙이 `Drop`으로 바뀐다. `samples/rust/rust_driver_platform.rs`가 예제다.
- **devm**(G4): `Devres<T>`는 장치가 unbind되면 안의 자원(MMIO 매핑 등)을 회수하고, 그 뒤 접근은 실패(`Revocable`)한다 — C의 "remove 뒤에 남은 workqueue가 iounmap된 레지스터를 만지는" 버그를 막는다.
- **file_operations**(F): `kernel::miscdevice::MiscDevice` 트레이트를 구현하면 `open`/`ioctl`/`read_iter` 등이 C `file_operations`로 연결된다(`#[vtable]` 매크로가 구현한 함수만 테이블에 채운다). `samples/rust/rust_misc_device.rs`.
- **동시성**(H): `SpinLock<T>`, `Mutex<T>`, `Arc<T>`, `CondVar`, RCU, atomic. lockdep 클래스 키는 `new_mutex!`/`new_spinlock!` 매크로가 호출 위치마다 정적 키를 만들어 준다(`static_lock_class!()`).

---

## 10. Embedded 실무에서 다시 보기

### 10.1 장면 1 — "다음 센서 드라이버는 Rust로 쓰자"는 제안을 받았다

예를 들어 센서 키트(i.MX8 + IMU(SPI) + 기압계(I2C) + CAN 모터 컨트롤러)에서 팀 리드가 이렇게 물었다고 하자. Don이 확인할 순서는 다음과 같다.

| 확인 항목 | 확인 방법 | 이 노트의 결과(6.18) |
|---|---|---|
| 대상 커널에 그 버스의 Rust 추상화가 있나 | `ls rust/kernel/` (예제 6 방식) | I2C·SPI·GPIO·IIO·CAN 없음 → **불가** |
| 벤더 BSP 커널 버전 | `uname -r`, BSP 릴리스 노트 | 벤더 커널은 보통 5.15/6.1/6.6 계열 — 추상화가 6.18보다 더 적다 |
| BSP 커널이 clang으로 빌드되나, Rust와 충돌하는 옵션(`RANDSTRUCT` 등)이 있나 | `.config`, 예제 11 방식 | 대부분 GCC 빌드 → 빌드 체계부터 바꿔야 함 |
| Yocto/Buildroot가 커널용 rustc·bindgen을 제공하나 | 레이어/패키지 확인 | 최근 Yocto는 Rust 툴체인 레시피가 있지만 커널 Rust 통합은 프로젝트별 작업 |
| 인증·안전 규격(차량·국방)이 요구하는 툴체인 자격 | 품질 부서 | 컴파일러 자격 인증 범위에 rustc가 들어 있나 |

결론적으로 2026년 현재 일반적인 센서 드라이버는 **C로 쓰는 것이 현실적인 답**이다. 대신 "이 드라이버의 유저 공간 쪽(데몬, 프로토콜 파서)은 Rust로 쓰자"는 훨씬 쉽고 이득도 크다 — 유저 공간 Rust는 커널 추상화가 필요 없다.

### 10.2 장면 2 — Rust가 이미 맞는 곳

- **GPU/가속기 드라이버**: nova·tyr처럼 펌웨어 인터페이스가 복잡하고 공격 표면이 넓은 드라이버. DRM 메인테이너가 "새 DRM 드라이버는 Rust를 요구하는 쪽으로 가고 있다"고 언급했다는 보도(LWN, 2025 Maintainers Summit)도 있다. AI 가속기 칩 쪽으로 가려는 Don에게는 이쪽이 가장 관련 깊다.
- **Android 기기**: binder가 Rust로 들어왔고 Android 커널(GKI)은 Rust 모듈을 실제로 출하한다. Android 기반 임베디드 제품이라면 Rust 커널 코드를 읽을 일이 생긴다.
- **SoC 내부의 단순 MMIO 블록**: platform + `io::mem` + IRQ + clk 추상화가 있으므로, 사내 IP 블록의 새 드라이버를 Rust로 시도해 볼 수 있다. 단 upstream하지 않는 out-of-tree라면 커널 업그레이드 때마다 `kernel` crate API 변화(아직 자주 바뀐다)를 따라가야 한다.

### 10.3 장면 3 — 툴체인 고정

예제 11에서 본 것처럼 `.config` 자체가 rustc 버전(`CONFIG_RUSTC_VERSION`, `CONFIG_RUSTC_HAS_*`)에 따라 달라지고, 예제 14처럼 Rust 심볼에 crate 해시가 들어간다. 따라서 제품 빌드에서는:

```sh
# 빌드 서버에서 기록해 둘 것 (재현 가능한 Rust 커널 빌드)
rustc --version; bindgen --version; clang --version | head -1
grep -E "^CONFIG_(RUSTC_VERSION|RUSTC_LLVM_VERSION|CLANG_VERSION)=" .config
```

를 빌드 산출물과 함께 남기고, 컨테이너 이미지로 rustc·bindgen·clang 버전을 고정한다. 그리고 **Rust 모듈은 커널과 같은 빌드에서 함께 빌드**한다(따로 빌드한 Rust `.ko`를 다른 커널 빌드에 넣는 일은 C보다 훨씬 쉽게 깨진다).

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| Rust 꺼진 커널 트리에 Rust 모듈 빌드 | `error[E0463]: can't find crate for core` (rustup 안내가 나옴) | 커널 빌드에 `rust/libcore.rmeta` 등이 없음 | `CONFIG_RUST=y`로 커널을 다시 빌드, 그 트리로 `M=` 빌드 |
| `LLVM=1` 없이 빌드 | `RUST_IS_AVAILABLE`은 되는데 설정·빌드가 이상하거나 GCC 전용 플래그 문제 | GCC + Rust는 문서상 "very experimental" | 커널 전체를 `make LLVM=1`로 |
| `RANDSTRUCT`·`MODVERSIONS`가 켜진 벤더 config | menuconfig에 `Rust support`가 안 보임 | Kconfig `depends on !RANDSTRUCT` 등 | 충돌 옵션 확인 후 끄거나(`GENDWARFKSYMS`) 포기 |
| `module!`에 `params:` 사용 (6.18) | 매크로 에러(알 수 없는 키) | 6.18 `EXPECTED_KEYS`에 없음 | 상수/sysfs/configfs로 대체, 또는 지원 버전 확인 |
| 뮤텍스를 가진 구조체를 일반 `struct` + `Module`로 | `Mutex::new`가 `impl PinInit`이라 대입이 안 됨 | 뮤텍스는 이동 불가(pinning) | `#[pin_data]` + `InPlaceModule` + `new_mutex!` |
| `#[pin_data]`인데 `impl Drop` | 컴파일 에러 | pinned 타입은 `PinnedDrop`만 | `#[pin_data(PinnedDrop)]` + `#[pinned_drop]` |
| `unsafe` 위에 SAFETY 주석 없음 | `CLIPPY=1`에서 `undocumented_unsafe_blocks` 경고, 리뷰 거절 | 커널 규칙 | 불변 조건을 `// SAFETY:`로 문서화 |
| raw pointer를 받는 safe `fn` | 컴파일은 됨, 리뷰에서 unsound 지적 | safe 인터페이스로 UB 가능 | `unsafe fn` + `# Safety` 문서 |
| 커널 Rust에서 인덱스로 배열 접근 남발 | 범위 밖이면 panic → `BUG()` → oops | bounds check는 런타임 | `get()`으로 `Option` 처리, 길이 먼저 검사 |
| 커널만 다시 빌드하고 Rust `.ko`는 예전 것 사용 | insmod 시 `Unknown symbol _RNv...` | Rust 심볼 이름에 crate 해시 | 커널과 모듈을 같은 빌드에서 |

---

## 12. 면접에서 이렇게 말한다

**Q.** Why is the Linux kernel adopting Rust? What problem does it solve?

**A.** 커널 보안 버그의 큰 부분이 UAF, OOB, data race 같은 메모리 안전 버그인데, C에서는 KASAN·KCSAN 같은 런타임 도구로 "그 경로가 실행돼야" 잡힌다. Rust는 소유권·빌림 규칙으로 이 중 상당수를 컴파일 때 거절한다. 커널 전체를 다시 쓰는 게 아니라 C 커널 위에 안전한 추상화 층을 두고, 새 드라이버를 그 위에서 safe Rust로 쓰게 하는 것이 목표다. 2025년 말 Maintainers Summit에서 실험 단계를 끝내고 정식 구성 요소가 됐다.

> Most serious kernel bugs are memory-safety bugs — use-after-free, out-of-bounds, data races — and in C we only catch them at runtime with KASAN or KCSAN if a test happens to hit the path. Rust's ownership and borrowing rules reject many of those at compile time. The approach isn't rewriting the kernel; it's a layer of safe abstractions over the existing C APIs so new drivers can be written in safe Rust, with the unsafe code concentrated in a small, reviewed layer. As of the end of 2025 the maintainers declared the experiment over — Rust is a permanent part of the kernel.

**꼬리 질문**: "What does Rust not protect you from?" → 논리 버그, 데드락, DMA가 메모리를 덮어쓰는 문제, `unsafe` 안의 실수.

**Q.** How does Rust code call into the C kernel? Walk me through the layers.

**A.** bindgen이 `bindings_helper.h`에 모아 둔 C 헤더를 파싱해 `bindings` crate를 자동 생성한다 — 6.18 빌드에서 12만 8천 줄이다. 매크로·인라인 함수는 `rust/helpers/*.c`로 한 번 감싼다. 그 위의 `kernel` crate가 raw pointer와 `extern "C"` 호출을 `unsafe` + SAFETY 주석으로 감싸 `Mutex<T>`, `KVec`, `Arc` 같은 안전한 타입을 만든다. 드라이버는 `kernel` crate만 쓴다. 예를 들어 `Mutex::lock()`은 `MutexBackend::lock` → `bindings::mutex_lock` → `rust_helper_mutex_lock` → C `mutex_lock`으로 내려가고, guard가 drop될 때 unlock된다.

> There are three layers. bindgen generates a `bindings` crate from the C headers listed in `bindings_helper.h` — about 128k lines in my 6.18 build — and inline functions or macros get thin C wrappers in `rust/helpers`. On top of that, the `kernel` crate wraps those raw, unsafe FFI calls into safe types like `Mutex<T>`, `KVec`, `Arc`, with every unsafe block carrying a SAFETY comment. Drivers only use the `kernel` crate. For example `Mutex::lock()` goes down through the backend to `rust_helper_mutex_lock` and the C `mutex_lock`, and the returned guard unlocks on drop, so you can't forget the unlock on an error path.

**Q.** What is pinning and why does kernel Rust need it?

**A.** Rust는 기본적으로 값을 memcpy로 옮겨도 된다고 가정하는데, `struct mutex`의 `wait_list`처럼 자기 주소를 담는 C 구조체가 많다. 옮기면 포인터가 옛 주소를 가리킨다. 그래서 `Pin`으로 "이 값은 더 이상 이동하지 않는다"를 타입으로 표현하고, `pin_init!`/`new_mutex!`로 최종 위치에서 바로 초기화한다. C의 "구조체를 먼저 할당하고 그 자리에서 `mutex_init`"을 컴파일러가 강제하는 것이다.

> Many kernel C structures are self-referential — an empty `list_head` in a mutex's wait list points to itself — so moving one with a memcpy corrupts it. Rust normally assumes any value can be moved. `Pin` expresses "this will never move again", and the `pin_init` macros initialize the object directly in its final location, like calling `mutex_init` on already-allocated memory in C, except the compiler enforces it.

**Q.** How do errors work in a Rust kernel module compared to C?

**A.** C는 0 또는 음수 errno를 돌려준다. Rust에서는 `kernel::error::Error`가 errno를 감싸고 함수는 `Result<T>`를 돌려준다. `?` 연산자가 `goto err` 사다리를 대신하고, 이미 만든 지역 자원은 `Drop`이 해제한다. 경계에서는 `to_result()`로 C 반환값을 `Result`로, `module!`이 생성한 `init_module()`은 `e.to_errno()`로 다시 음수 errno로 바꾼다. 실제로 `Err(ENODEV)`를 돌려준 모듈을 insmod하면 "No such device"가 나온다.

> In C you return zero or a negative errno. In Rust the `Error` type wraps the errno and functions return `Result<T>`. The question-mark operator replaces the goto-cleanup ladder, and anything already allocated is freed by `Drop`. At the boundary `to_result()` converts C return codes, and the `init_module` generated by `module!` converts an `Err` back with `to_errno()`. I tested it: a module whose init returns `ENODEV` makes insmod fail with "No such device".

**Q.** What's actually in mainline in Rust today?

**A.** 6.18 트리를 grep해 보면 `.rs` 파일 243개 중 159개가 `rust/` 추상화 층이고, `drivers/` 아래는 64개다. 실제 드라이버는 Android binder의 Rust 버전, NVIDIA용 nova-core와 nova DRM, Arm Mali용 tyr, rnull, PHY 드라이버 두 개, cpufreq-dt의 Rust 버전, DRM panic QR 코드 정도다. 추상화는 platform, PCI, USB, DRM, block, net PHY, IRQ, DMA, clk, regulator, firmware 등이 있지만 I2C·SPI·GPIO·IIO·V4L2·CAN은 6.18에 없다.

> I counted in a 6.18 tree: 243 Rust files, 159 of them in the `rust/` abstraction layer and 64 under `drivers/`. Real drivers include the Rust Android binder, nova-core and the nova DRM driver for NVIDIA GPUs, tyr for Arm Mali, the rnull block driver, two PHY drivers and a Rust cpufreq-dt. There are abstractions for platform, PCI, USB, DRM, block, PHY, IRQ, DMA, clocks, regulators and firmware loading, but not yet for I2C, SPI, GPIO, IIO, V4L2 or CAN in 6.18.

**Q.** Would you write our next sensor driver in Rust?

**A.** 지금은 아마 아니다. 이유는 세 가지: 센서가 붙는 I2C·SPI·IIO 추상화가 6.18에도 없고, 벤더 BSP 커널은 그보다 오래됐고 보통 GCC로 빌드되며, Rust 모듈은 rustc 버전과 커널 빌드에 강하게 묶여서 툴체인 고정 비용이 든다. 대신 유저 공간 데몬이나 프로토콜 파서는 Rust로 쓰는 것이 바로 이득이고, SoC 내부 MMIO 블록처럼 추상화가 있는 영역이나 GPU·가속기 드라이버라면 Rust를 진지하게 검토하겠다.

> Probably not today. The I2C, SPI and IIO abstractions a sensor driver needs aren't in 6.18, vendor BSP kernels are older and usually GCC-built, and Rust modules are tightly bound to the exact rustc and kernel build, so we'd pay a toolchain-pinning cost. I'd use Rust right away for the user-space daemons and protocol parsers, and I'd seriously consider it in-kernel for a platform MMIO block or a GPU or accelerator driver, where the abstractions exist and the attack surface is large.

**꼬리 질문**: "Have you built one?" → "6.18.55를 LLVM=1 + CONFIG_RUST로 빌드해 QEMU에서 samples와 내 모듈(KVec, `?`, pinned Mutex)을 insmod해 봤고, 같은 기능의 C 모듈과 크기·빌드 시간을 비교했다."

**Q.** What does `unsafe` mean in kernel Rust, and how is it controlled?

**A.** `unsafe` 블록은 raw pointer 역참조, C 함수 호출, `static mut` 접근처럼 컴파일러가 검증 못 하는 일을 하는 곳이다. 커널 규칙은 모든 `unsafe` 블록 위에 왜 안전한지 `// SAFETY:` 주석을 쓰는 것이고, clippy의 `undocumented_unsafe_blocks`로 검사한다. 목표는 `unsafe`를 `kernel` crate 같은 추상화 층에 모으고 드라이버는 safe만 쓰는 것이다. 실제로 6.18에서 rust/kernel은 `unsafe` 블록이 약 1,400개인데 PHY 드라이버와 rnull은 0개다.

> An unsafe block is where you do things the compiler can't verify — dereferencing raw pointers, calling C, touching a mutable static. The kernel rule is that every unsafe block has a SAFETY comment explaining why the invariants hold, and clippy's `undocumented_unsafe_blocks` lint enforces it. The goal is to keep unsafe inside the abstraction layer: in 6.18 `rust/kernel` has around 1,400 unsafe blocks while the PHY drivers and rnull have zero.

---

## 13. 직접 해보기

1. **개념**: `struct hrtimer`나 `struct work_struct`를 Rust에서 쓸 때 왜 pinning이 필요한지, 그 구조체 안의 어느 필드 때문인지 C 헤더에서 찾아 설명하라. — 정답/힌트: 둘 다 커널 내부 리스트·트리(`timerqueue_node`의 rbtree 노드, `work_struct.entry`의 `list_head`)에 자기 주소로 연결된다. `include/linux/hrtimer_types.h`, `include/linux/workqueue_types.h`.

2. **관찰**: 빌드 트리의 `rust/bindings/bindings_generated.rs`에서 `struct platform_device`와 `ioremap` 관련 선언을 찾고, 그 위의 `rust/kernel/io/mem.rs`가 이것을 어떤 안전한 타입으로 감싸는지 읽어라. — 정답/힌트: `grep -n "pub struct platform_device" -A10`, `grep -n "pub struct IoMem\|pub struct ExclusiveIoMem" rust/kernel/io/mem.rs`.

3. **코드**: `e8_hello`에 `SpinLock<u32>` 카운터를 추가하고, init에서 증가시킨 뒤 exit에서 출력하라. `new_spinlock!`과 `#[pin]`이 필요하다. — 정답/힌트: `use kernel::{new_spinlock, sync::SpinLock};`, 필드 `#[pin] count: SpinLock<u32>`, 초기화 `count <- new_spinlock!(0)`, 사용 `*self.count.lock() += 1`.

4. **코드**: `e8_fail`의 `ENODEV`를 `EPROBE_DEFER`로 바꿔 insmod하면 유저 공간에 어떤 에러가 보이는가? 예측하고 확인하라. — 정답/힌트: `EPROBE_DEFER`(517)는 커널 내부 전용 값이라 유저 공간 strerror에 이름이 없다. `insmod`가 보이는 메시지와 `rust/kernel/error.rs`의 `declare_err!(EPROBE_DEFER, ...)`를 비교해 보라. module init에서 `-EPROBE_DEFER`를 돌려주는 것은 의미가 없다(defer는 probe에서만 의미가 있다, D3 §10).

5. **관찰**: §5.2의 `e8-rust-dev@9300000` 노드에 `test,u32-required-prop`을 추가하고, 자식 노드 하나와 그 안에 `test,ref-arg = <&phandle 1 2>`를 넣어 다시 부팅하라. dmesg의 "is missing" 줄이 사라지고, 샘플 마지막 `for child in fwnode.children()` 루프가 무엇을 찍는가? 그리고 C platform 드라이버(G3)의 `of_match_table`·`probe`·`remove`가 Rust 샘플에서 각각 무엇인지 표로 정리하라. — 정답/힌트: 참조 대상 노드에 `#test,ref-cells`가 아니라 고정 개수 `NArgs::N(2)`로 읽으므로 phandle 뒤 인자 2개가 `FwNodeReferenceArgs`로 출력된다. 대응: `kernel::of_device_table!` ↔ `of_match_table`, `platform::Driver::probe` ↔ `probe`, `Drop`(그리고 선택적 `unbind`) ↔ `remove`.

6. **측정**: §7의 측정을 `CONFIG_RUST_OVERFLOW_CHECKS=n`으로 커널을 다시 빌드해서 반복하고, `e8_hello.ko`의 `.text` 크기가 얼마나 줄어드는지 보라. — 정답/힌트: `llvm-size e8_hello.o`. overflow 검사와 panic 경로가 빠진다. `llvm-nm -u`에서 `panic_const_sub_overflow`가 사라지는지도 확인.

7. **면접 대비**: "Rust in the kernel"에 대한 30초 답변을 이 노트의 숫자 세 개(243개 파일/159개 `rust/`, binder in 6.18, I2C·SPI·IIO 추상화 없음)를 넣어 영어로 말해 보라. — 정답/힌트: §12의 다섯 번째와 여섯 번째 답을 합친다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| Rust for Linux (RfL) | 커널에 Rust 지원을 넣는 프로젝트 | Miguel Ojeda 주도, 6.1에 첫 merge |
| ownership | 소유권 | 값마다 소유자는 하나, 소유자가 사라지면 해제 |
| borrow (`&T`, `&mut T`) | 빌림 | 소유권 없이 참조. `&mut`는 동시에 하나만 |
| borrow checker | 빌림 검사기 | 빌림 규칙 위반을 컴파일 때 거절하는 rustc 부분 |
| `Drop` | 소멸자 트레이트 | scope를 벗어날 때 자동 호출 (RAII) |
| `unsafe` | 검증 밖 영역 표시 | raw pointer·FFI·`static mut` 등. 책임은 작성자 |
| `// SAFETY:` | 안전성 근거 주석 | 모든 `unsafe` 블록 위에 필수, clippy가 검사 |
| unsound | 건전하지 않음 | safe 인터페이스로 UB를 만들 수 있는 상태 |
| bindgen | C → Rust 바인딩 생성기 | libclang으로 헤더를 파싱해 `extern "C"`·구조체 생성 |
| `bindings` crate | 자동 생성된 FFI 층 | 전부 unsafe, 사람이 직접 쓰지 않음 |
| `rust/helpers` | C 래퍼 함수 모음 | 인라인 함수·매크로를 심볼로 노출 |
| `kernel` crate | 안전한 추상화 라이브러리 | `rust/kernel/`, 드라이버가 쓰는 API |
| `module!` | 모듈 선언 매크로 | `init_module`/`cleanup_module`/modinfo 생성 |
| `Result<T>` / `Error` | 에러 타입 | errno를 감싼 타입, `?`로 전파 |
| `?` 연산자 | 에러 조기 반환 | `Err`이면 즉시 return — goto 정리 대체 |
| `KVec` / `KBox` | 커널 할당 컨테이너 | `kmalloc` 기반, GFP 플래그 명시, 실패는 `Result` |
| pinning (`Pin`) | 이동 금지 보장 | 자기 참조 구조체(리스트·락)를 안전하게 |
| `pin_init!` / `<-` | 제자리 초기화 | 최종 주소에서 초기화하는 initializer 생성 |
| `#[vtable]` | ops 테이블 매크로 | 구현한 함수만 C ops 구조체에 채움 |
| rmeta | Rust 메타데이터 파일 | crate의 타입 정보. 같은 rustc에서만 읽힘 |
| v0 mangling | Rust 심볼 이름 규칙 | `_RNv...` 형식, crate 해시 포함 |
| `LLVM=1` | 커널 LLVM 빌드 | clang·ld.lld·`llvm-*` 사용, Rust의 표준 조합 |
| clippy | Rust 린터 | `make CLIPPY=1`로 커널 규칙 lint 실행 |

---

## 15. 요약 & 체크리스트

Rust for Linux는 C 커널을 그대로 두고 그 위에 **bindgen이 만든 `bindings` → `unsafe`를 감싼 `kernel` crate → safe Rust 드라이버**라는 층을 쌓아, UAF·double free·data race 같은 메모리 안전 버그를 컴파일 단계에서 거절하려는 프로젝트다. 6.1에 인프라가 들어왔고, 6.18 LTS에는 `.rs` 파일 243개(그중 159개가 `rust/` 추상화), binder·nova·tyr·rnull·PHY 같은 실제 드라이버가 있으며, 2025년 말 실험 단계를 공식적으로 끝냈다. 커널 Rust는 `module!`로 모듈을 선언하고, `Result`/`Error`로 errno를 다루고(`?`가 goto 정리를 대체), 자기 참조 C 구조체 때문에 `Pin`과 `pin_init!`을 쓰며, 모든 `unsafe`에 `// SAFETY:` 근거를 단다. 직접 빌드해 보면 LLVM=1과 `CONFIG_RUST`가 필요하고, Rust 모듈은 rustc 버전·커널 빌드에 강하게 묶인다. 임베디드 관점의 결론은 "I2C·SPI·IIO 추상화가 없는 6.18에서 센서 드라이버는 아직 C, 그러나 GPU·가속기·Android·단순 MMIO 블록과 유저 공간에서는 Rust가 이미 현실적"이다.

- [ ] UAF, OOB, double free, data race를 C 예시와 함께 설명하고, 각각 Rust의 어떤 규칙이 막는지 말할 수 있다
- [ ] Android의 메모리 안전 취약점 비율 수치(76% → 24%)를 출처와 한계(Android 전체, 2024는 외삽)까지 정확히 인용할 수 있다
- [ ] 6.18 트리에 있는 Rust 드라이버 5개 이상과, 추상화가 없는 임베디드 서브시스템(I2C·SPI·GPIO·IIO·V4L2·CAN)을 말할 수 있다
- [ ] bindgen → `bindings` → helpers → `kernel` crate → 드라이버 층을 `Mutex::lock()` 예로 따라갈 수 있다
- [ ] `module!`의 키와 6.18에 `params`가 없다는 사실을 소스로 확인하는 방법을 안다
- [ ] `Result`/`Error`/`?`/`to_errno()`로 에러가 insmod까지 전달되는 경로를 설명할 수 있다
- [ ] pinning이 왜 필요한지 `struct mutex`의 `wait_list`로 그림을 그려 설명할 수 있다
- [ ] `unsafe` 블록과 SAFETY 주석, unsound한 safe 함수의 차이를 설명할 수 있다
- [ ] `make LLVM=1` + `CONFIG_RUST`로 커널을 빌드하고, `make LLVM=1 M=...`로 Rust 모듈을 빌드해 QEMU에서 insmod할 수 있다
- [ ] Rust 모듈이 왜 커널 빌드·rustc 버전에 강하게 묶이는지(rmeta, v0 심볼의 crate 해시) 설명할 수 있다
- [ ] "우리 다음 드라이버를 Rust로?"라는 질문에 확인 항목(추상화, BSP 버전, 툴체인, 인증)으로 판단을 내릴 수 있다

## 참고 자료

- docs.kernel.org Rust 문서: [Rust — The Linux Kernel documentation](https://docs.kernel.org/rust/index.html) (Quick Start, General Information, Coding Guidelines, Arch Support)
- 커널 소스(6.18.55): `rust/kernel/`, `rust/macros/module.rs`, `rust/kernel/error.rs`, `rust/kernel/sync/lock.rs`, `rust/helpers/mutex.c`, `samples/rust/`, `Documentation/rust/quick-start.rst` — 온라인으로는 [elixir.bootlin.com/linux/v6.18/source/rust](https://elixir.bootlin.com/linux/v6.18/source/rust)
- Rust for Linux 프로젝트: [rust-for-linux.com](https://rust-for-linux.com)
- Google Security Blog (2024-09-25): [Eliminating Memory Safety Vulnerabilities at the Source](https://security.googleblog.com/2024/09/eliminating-memory-safety-vulnerabilities-Android.html)
- LWN.net, 2025 Maintainers Summit의 Rust 세션 보도: [lwn.net/Articles/1050174](https://lwn.net/Articles/1050174/)
- The Rust Programming Language (공식 책) 4장 Ownership, 16장 Fearless Concurrency: [doc.rust-lang.org/book](https://doc.rust-lang.org/book/)
- 관련 노트: E2(설정과 빌드), E3(커널 모듈), E4(커널 프로그래밍 제약), E5(커널 C 관용구), E6(유저 ↔ 커널 경계), D3(드라이버 매칭), G3(platform driver), H(동시성)
