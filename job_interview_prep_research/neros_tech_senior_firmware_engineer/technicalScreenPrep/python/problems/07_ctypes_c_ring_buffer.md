# 🔗 07 · C ring buffer를 pytest + ctypes로 테스트 — Python/C 경계

> 리크루터가 "Python/C 둘 다 가능"이라고 했다. 둘을 **한 문제에서** 보여 주는 방법: 온사이트용으로 만든 C 링버퍼([onsitePrep/code/ring_buffer.c](../../../onsitePrep/site/code/ring_buffer.c.html))를 그대로 공유 라이브러리로 빌드하고, pytest에서 `ctypes`로 불러 테스트한다. C 구조체의 `head`/`tail`을 Python에서 직접 `UINT32_MAX` 근처로 세팅해 32비트 wrap까지 검증한다.

## 왜 나오나

- "Python or C, your choice" → C로 짜고 Python으로 테스트하는 것이 **FW 테스트 팀의 실제 일**이다 (펌웨어 모듈을 host에서 단위 테스트)
- Full Stack 인터뷰어 [추정]에게는 "C 코드도 결국 테스트 가능한 라이브러리"라는 관점이 설득력 있다
- 면접에서 이걸 다 짜라고 하진 않는다 [추정]. 대신 "How would you test C firmware code from Python?"에 **구체적으로** 답할 재료다

## 문제

> "You wrote this ring buffer in C. Show me how you'd test it from pytest without hardware."

- shim: [c/rb_shim.c](../../c/rb_shim.c) — 원본이 전부 `static`이라 심볼이 없다. shim이 원본을 `#include`하고 `main`을 `rb_selftest_main`으로 바꾼 뒤 non-static 래퍼 `shim_*`를 export
- session fixture가 `cc -shared -fPIC`로 한 번 빌드 (`tmp_path_factory`), 컴파일러가 없으면 skip
- 할 일: `RB(ctypes.Structure)`의 `_fields_`를 `rb_t`와 똑같이, 그리고 TODO 테스트 채우기

## 엣지 케이스

- **구조체 레이아웃**: `ctypes.sizeof(RB) == shim_sizeof()` — 이게 틀리면 C가 Python 메모리를 덮어쓴다. 그래서 `rb` fixture가 먼저 검사하고 실패시킨다
- 16칸 전부 사용 → 17번째 push 실패 (drop-new)
- `head = tail = 0xFFFFFFFF - 3`에서 10개 push/pop → unsigned wrap 뒤에도 count 정확
- bulk `write`/`read`가 wrap 지점에서 `memcpy` 두 번으로 나뉘는 경우 (wlen 11, rlen 7 같은 서로소 크기)
- 원본 파일의 `main()` 셀프테스트도 그대로 호출 → 0 반환

## 힌트

1. `fn.argtypes`/`fn.restype`를 **반드시** 선언. 안 하면 ctypes가 반환형을 `int`로 가정해 `size_t`(64비트)가 잘린다
2. 출력 버퍼는 `ctypes.create_string_buffer(n)`, 결과는 `.raw[:got]`
3. 포인터 인자는 `ctypes.byref(obj)` (가볍다), 값을 받아 올 땐 `v = ctypes.c_uint8(); fn(..., ctypes.byref(v)); v.value`

## 풀이 해설

- **왜 ctypes**: 표준 라이브러리, 빌드 단계 없음. 함수가 많아지면 `cffi`(헤더를 그대로 파싱), C++이면 `pybind11`. 펌웨어 쪽에서 많이 쓰는 대안은 C 단위 테스트 프레임워크(Unity/CMock, GoogleTest)를 host에서 돌리는 것 — 둘 다 말할 수 있으면 된다
- **모델 비교 테스트**: Python `deque`를 정답 모델로 C 구현과 무작위 5000번 비교. "같은 테스트 아이디어를 언어 경계 너머로 재사용"이 핵심 메시지
- **원본을 건드리지 않는다**: `#define main ...` + `#include`로 감싸서 온사이트 코드와 테스트가 같은 소스를 공유한다
- **host 테스트의 한계**: 엔디언·정렬·`volatile`/ISR 타이밍·캐시는 host에서 안 보인다 → 타깃(HIL)에서 따로. host 테스트는 **로직**, HIL은 **타이밍과 하드웨어**

## 말하면서 풀기

- "I'd build the C module as a shared library in a session fixture and drive it with ctypes. First test: the struct layout matches, because if it doesn't, everything else is undefined behavior."
- "Because I can set head and tail directly from Python, I can start the indices just below 2^32 and prove the unsigned wrap math works — that's hard to reach otherwise."
- "Host tests cover logic. Timing, ISR interaction and cache effects still need the target, so this complements HIL rather than replacing it."

## 꼬리 질문

- "Sanitizers?" → shim을 `-fsanitize=address,undefined`로 빌드하고 `DYLD_INSERT_LIBRARIES`/`LD_PRELOAD`로 런타임을 올리거나, C 쪽 테스트는 `make test`의 ASan 빌드로 ([onsitePrep Makefile](../../../onsitePrep/site/code/Makefile.html))
- "How does this run in CI?" → 같은 컨테이너에서 `cc` + pytest. 타깃 크로스 컴파일(`arm-none-eabi-gcc`)은 별도 job, HIL은 리그 runner
- "Testing static functions?" → shim에서 `#include`하는 방식, 또는 `#ifdef UNIT_TEST`로 static을 풀기. 전자가 원본을 오염시키지 않는다

## 파일

- [starter](../starters/07_ctypes_c_ring_buffer.py) · [모범답안](../solutions/07_ctypes_c_ring_buffer.py) · [c/rb_shim.c](../../c/rb_shim.c)
- 채점: `python3 python/run.py 07` · `python3 python/run.py 07 --sol`
