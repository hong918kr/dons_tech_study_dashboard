# 10. memcpy와 memmove — 겹침, 방향, word 최적화

> **주제**: 포인터 산술 · 겹침 판정 · `restrict` 계약 · 정렬 위상 · **난이도**: 기초 · **목표 시간**: 25분
> **레벨**: L1 메모리·포인터 · **해설은 보지 말 것**: 먼저 `starters/10_memcpy_move.c`를 채워 `make run N=10`으로 통과시킨다.

## 면접관의 문장

"Every firmware codebase I've worked on has a hand-rolled `memcpy` somewhere, usually because the vendor's one pulled in too much code or didn't handle a peripheral's alignment rule. So let's write them.

Give me `memcpy` and `memmove`. Then tell me the difference — and don't say 'memmove handles overlap', tell me what the implementation actually does differently, and why that's enough.

I also want a version that copies a word at a time instead of a byte at a time, because on a Cortex-M4 the byte loop is four times the bus traffic. There's a condition on when you're allowed to do that. What is it?

Last thing: the signature of `memcpy` in the standard has `restrict` on both pointers. What is that promising, and who is making the promise?"

## 요구사항

1. `regions_overlap(a, b, n)`은 길이가 같은 두 구간 `[a, a+n)`과 `[b, b+n)`이 한 바이트라도 겹치면 `1`, 아니면 `0`을 돌려준다. 부수효과가 없다.
2. `regions_overlap`은 `n == 0`이면 두 주소가 같아도 `0`이다. 길이 0 구간은 아무 바이트도 덮지 않는다. `a`나 `b`가 NULL이면 `0`이다.
3. `regions_overlap`은 두 구간이 딱 맞닿은 경우(`b == a + n`)를 겹치지 않는 것으로 판정한다. 1바이트만 겹치는 경우(`b == a + n - 1`)는 겹치는 것으로 판정한다.
4. `regions_overlap`의 내부 계산에 `a + n` 같은 주소 덧셈을 쓰지 않는다. 주소를 `uintptr_t`로 바꿔 두 시작 주소의 차이만 본다.
5. `my_copy_forward(dst, src, n)`는 항상 낮은 인덱스에서 높은 인덱스 방향으로만 옮긴다. 겹침 처리를 하지 않는다. `restrict`를 붙이지 않으므로 겹치는 인자로 불러도 undefined behaviour가 아니다.
6. `my_memcpy(dst, src, n)`는 `n`바이트를 옮긴다. `n == 0`이면 `dst`를 한 바이트도 건드리지 않는다. `dst` 바깥으로 한 바이트도 쓰지 않는다.
7. `my_memmove(dst, src, n)`는 두 구간이 어떻게 겹쳐 있어도 표준 `memmove`와 똑같은 결과를 낸다. `n == 0`이거나 `dst == src`면 아무것도 하지 않는다.
8. `my_memmove`는 겹침 여부를 검사하지 않는다. `dst`와 `src`의 주소 대소만 보고 복사 방향을 정한다. 왜 그것으로 충분한지 설명할 수 있어야 한다.
9. `my_memcpy_words(dst, src, n)`는 조건이 맞으면 4바이트씩 옮기고, 아니면 바이트 단위로 떨어진다. 어느 경로를 타든 결과는 `my_memcpy`와 완전히 같다.
10. `my_memcpy_words`가 word 경로를 타는 조건은 두 가지다. `n`이 `2 * sizeof(uint32_t)` 이상이고, `dst`와 `src`의 정렬 위상이 같다(`(uintptr_t)dst % 4 == (uintptr_t)src % 4`).
11. `my_memcpy_words`의 word 경로는 head(정렬 맞추기) → body(4바이트씩) → tail(남은 1~3바이트) 세 구간으로 나뉜다. head는 최대 3바이트다.
12. 네 복사 함수 모두 `dst`를 그대로 돌려준다. 표준 `memcpy`/`memmove`와 같은 계약이다.

## 인터페이스

```c
#include <stddef.h>
#include <stdint.h>

/* 길이가 같은 두 구간이 겹치는가 */
int   regions_overlap(const void *a, const void *b, size_t n);

/* 항상 앞에서부터. 겹침에서 무엇이 깨지는지 보이기 위한 함수 */
void *my_copy_forward(void *dst, const void *src, size_t n);

/* 겹치지 않는다는 계약을 받는다 */
void *my_memcpy(void *restrict dst, const void *restrict src, size_t n);

/* 겹쳐도 된다 */
void *my_memmove(void *dst, const void *src, size_t n);

/* 정렬 위상이 같으면 word 단위로 */
void *my_memcpy_words(void *restrict dst, const void *restrict src, size_t n);
```

## 제약

- `memcpy`, `memmove`, `memset`, `memcmp`를 구현 안에서 쓰지 않는다. 테스트 코드에서는 기준값 계산용으로 쓴다.
- 동적 할당 금지. 임시 버퍼를 잡아 복사하고 되돌리는 풀이는 인정하지 않는다(메모리도 시간도 두 배다).
- `my_memcpy`와 `my_memcpy_words`를 겹치는 인자로 부르지 않는다. `restrict`를 붙였다는 것은 그 검사를 하지 않겠다는 선언이다.
- 서로 다른 객체의 주소를 `<`로 직접 비교하지 않는다. `uintptr_t`로 바꿔 비교한다.
- 하드웨어 헤더 금지. C11 표준 라이브러리만.
- `cc -std=c11 -Wall -Wextra -O2 -g`에서 경고 0개.

## 예시 동작

16바이트 버퍼 `b`에 `0x10 0x11 ... 0x1F`가 들어 있다.

```
초기:            10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F

my_memmove(b+0, b+4, 8)   dst < src, 왼쪽으로 당긴다 → 앞에서부터
결과:            14 15 16 17 18 19 1A 1B 18 19 1A 1B 1C 1D 1E 1F
                 ^^^^^^^^^^^^^^^^^^^^^^^ 옮겨온 8바이트

my_memmove(b+4, b+0, 8)   dst > src, 오른쪽으로 민다 → 뒤에서부터
결과:            10 11 12 13 10 11 12 13 14 15 16 17 18 19 1A 1B
                             ^^^^^^^^^^^^^^^^^^^^^^^ 옮겨온 8바이트

my_copy_forward(b+4, b+0, 8)   같은 인자, 앞에서부터
결과:            10 11 12 13 10 11 12 13 10 11 12 13 18 19 1A 1B
                             ^^^^^^^^^^^ ^^^^^^^^^^^ 4칸 주기로 되풀이된다
                 원본 [4..7]을 읽기 전에 [0..3]으로 덮어 버렸다
```

겹침 판정의 경계는 이렇다.

```
regions_overlap(b, b,      0)  -> 0    길이 0은 아무것도 덮지 않는다
regions_overlap(b, b,      1)  -> 1    같은 주소 1바이트
regions_overlap(b, b + 8,  8)  -> 0    딱 맞닿음: [0..7] 과 [8..15]
regions_overlap(b, b + 8,  9)  -> 1    1바이트 겹침: [0..8] 과 [8..16]
regions_overlap(b + 8, b,  9)  -> 1    순서를 바꿔도 같다
```

word 경로 판단은 정렬 위상만 본다.

```
dst = 0x2000'0000, src = 0x2000'0100, n = 64   위상 0 == 0  -> word 경로
dst = 0x2000'0001, src = 0x2000'0101, n = 64   위상 1 == 1  -> head 3B 후 word
dst = 0x2000'0000, src = 0x2000'0101, n = 64   위상 0 != 1  -> 전부 바이트
dst = 0x2000'0000, src = 0x2000'0100, n = 7    n < 8        -> 전부 바이트
```

## 스스로 점검할 질문

1. `my_memmove`가 겹침 여부를 검사하지 않고 주소 대소만 보는 것으로 충분한 이유는 무엇인가? 겹치지 않는데 "뒤에서부터" 복사하면 무엇이 틀리는가?
2. `restrict`는 컴파일러에게 무엇을 허락하는가? 그 약속을 깨고 겹치는 인자로 `my_memcpy`를 부르면 어떤 일이 벌어질 수 있는가? 왜 "느려지는" 것이 아니라 "틀려지는" 것인가?
3. `dst`와 `src`의 정렬 위상이 다르면 어떤 head 길이로도 둘을 동시에 정렬할 수 없다. 왜 그런가? 종이에 주소를 적어 보이라.
4. 위상이 다를 때도 word 단위로 옮기는 방법이 있다. 어떻게 하는가? 그 방법이 왜 대부분의 Cortex-M에서는 이득이 아닌가?
5. `my_copy_forward(b+4, b+0, 8)`의 결과가 4칸 주기로 되풀이되는 이유를 바이트 단위 타임라인으로 설명하라.
6. `regions_overlap`에서 `a + n`을 계산하면 어떤 상황에서 문제가 되는가? 실제 임베디드 타깃에서 그게 일어날 수 있는가?
7. DMA로 복사하면 이 함수들과 무엇이 달라지는가? DMA 엔진이 겹침을 어떻게 처리하는가?
8. `my_memcpy`를 `-O2`로 컴파일하면 컴파일러가 libc의 `memcpy` 호출로 바꿔 버릴 수도 있다. 왜 그럴 수 있고, 펌웨어에서 그걸 막고 싶으면 어떻게 하는가?

## follow-up (면접관이 이어서 물을 것)

1. "This buffer is a DMA destination. What changes about your copy — alignment, cache, or both?"
2. "Now do it without a `while` loop in the byte path. Duff's device, or something better?"
3. "Your `memcpy` is called with `n` coming off the wire. What do you add?"
4. "Why does `memcpy` return `dst` at all? Nobody uses it."
5. "Show me a case where `memmove` is the right answer but everybody writes `memcpy`."

---

**해설**: [notes/L1_memory_pointers.md](../drills/L1_memory_pointers.html) §3, §4, §5 · **답안**: `solutions/10_memcpy_move.c`
