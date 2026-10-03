# 🧵 N06 · Python은 싱글 스레드인가? — GIL, C 스레드와 엄밀히 무엇이 같고 다른가

> **엄밀한 답: CPython은 싱글 스레드가 아니다.** `threading.Thread`는 진짜 OS 스레드(pthread)를 만든다. 다만 **GIL(Global Interpreter Lock)** 때문에 한 순간에 **Python 바이트코드를 실행하는 스레드는 하나뿐**이다. 그래서 "동시성(concurrency)은 있고, Python 코드의 병렬성(parallelism)은 없다". 그런데도 스레드 전환이 바이트코드 사이 아무 데서나 일어나기 때문에 **race condition은 C처럼 생긴다** — 이 Mac에서 실제로 재 봤다. 결론적으로 Python 스레드는 "멀티코어 C 스레드"보다 **"싱글 코어 MCU에서 ISR이 main loop를 아무 때나 끊는 상황"**에 더 가깝다.

## 0. 한눈에

| 질문 | 답 |
|---|---|
| Python 스레드는 진짜 OS 스레드인가? | **예.** 스레드마다 native thread id가 다르다 (실측 1) |
| 여러 코어에서 Python 코드가 동시에 도나? | **아니오** (기본 CPython). GIL을 잡은 스레드 하나만 바이트코드 실행 |
| 그럼 lock이 필요 없나? | **필요하다.** `x += 1`은 바이트코드 여러 개라 중간에 전환되면 업데이트가 사라진다 (실측 2) |
| 스레드로 빨라지는 경우는? | **I/O 대기**(serial, socket, sleep)와 GIL을 놓는 C 확장. CPU 계산은 안 빨라진다 (실측 3·4) |
| C와 같은 점 | 인터리빙이 생기면 race가 난다 → 공유 상태는 lock · queue · atomic으로 |
| C와 다른 점 | 진짜 병렬 아님 · 데이터 레이스가 **UB가 아니라 "틀린 값"** · 메모리 재배치(reordering)를 신경 쓸 일이 거의 없음 |

## 1. 정확한 사실 관계

- CPython의 `threading.Thread` = OS 스레드 하나 (macOS/Linux에서 pthread). 스케줄링도 OS가 한다
- **GIL** = 인터프리터 전체를 보호하는 mutex 하나. 바이트코드를 실행하려면 이걸 잡아야 한다
- GIL을 잡은 스레드는 **switch interval**(기본 5ms, `sys.getswitchinterval()` = 0.005)마다 놓으라는 요청을 받는다 → 다른 스레드가 잡을 수 있다. 전환 지점은 **바이트코드 사이**
- GIL을 **놓는** 때: blocking I/O(`read`, `recv`, `serial.read`), `time.sleep`, 그리고 GIL을 명시적으로 놓는 C 확장(zlib, hashlib, numpy의 큰 연산 등)
- 그래서: **I/O-bound는 스레드로 동시에 기다릴 수 있고, CPU-bound Python 코드는 스레드를 늘려도 한 코어 분량**
- `asyncio`는 별개다 — **스레드 하나**에서 이벤트 루프가 `await` 지점에서만 협력적으로 전환 (이건 진짜 싱글 스레드)
- `multiprocessing`은 프로세스마다 인터프리터와 GIL이 따로 → CPU 병렬 가능, 대신 데이터는 복사/직렬화
- **free-threaded CPython**: 3.13에서 GIL 없는 빌드가 실험적으로 추가됐고(PEP 703, `python3.13t`), 3.14에서 공식 지원 단계가 됐다(PEP 779). 기본 배포판은 여전히 GIL 빌드다. GIL이 없어지면 Python 스레드도 진짜 병렬 → race는 C와 더 비슷해진다

## 2. 실측 — 이 Mac (Python 3.9.6 · 8코어 · switch interval 5ms)

| # | 실험 | 결과 | 의미 |
|---|---|---|---|
| 1 | 스레드 4개의 `threading.get_native_id()` | 서로 다른 id **4개** | 진짜 OS 스레드 |
| 2 | 스레드 4개 × `c.n += 1` 20만 번 (lock 없음) | 80만 중 **약 7만~36만 손실** (5회 반복, 매번 다름) | GIL이 있어도 race는 난다 |
| 2' | 같은 것을 `with lock:` 안에서 | 손실 **0** | lock이 필요하다 |
| 3 | CPU 계산 × 4: 순차 / 스레드 4 / 프로세스 4 | **0.51s / 0.53s / 0.25s** (다른 실행: 0.65 / 0.68 / 0.37) | 스레드는 CPU 병렬 안 됨, 프로세스는 됨 (생성 비용 포함) |
| 4 | 스레드 4개 × `sleep(0.5)` | **0.50s** (순차면 2.0s) | 대기 중엔 GIL을 놓는다 → I/O 동시성 |
| C | pthread 4개 × `volatile long ++` 200만 번 | 800만 중 **약 320만~370만 손실** | 멀티코어 진짜 병렬 + data race |
| C' | 같은 것을 `atomic_fetch_add` | 손실 **0** | C는 atomic 또는 mutex 필요 |

- 재현: [py_threads_gil.py](../experiments/gil/py_threads_gil.py) (`python3 experiments/gil/py_threads_gil.py`) · [c_threads_race.c](../experiments/gil/c_threads_race.c)
- 덤으로 배운 함정: macOS의 `multiprocessing` 기본 시작 방식은 **spawn**이라, 메인 코드를 `if __name__ == "__main__":` 아래에 두지 않으면 자식 프로세스가 파일 전체를 다시 실행하다 죽는다 (처음 실험에서 실제로 났다)

## 3. 왜 GIL이 있는데 race가 나나 — 바이트코드 단위로 보기

`c.n += 1` 한 줄은 바이트코드 여러 개다 (`dis.dis`로 본 Python 3.9):

```text
LOAD_FAST    c
DUP_TOP
LOAD_ATTR    n        ← 값 읽기   (스레드 A: 41 읽음)
LOAD_CONST   1                    ← 여기서 B로 전환, B가 41 읽고 42 저장
INPLACE_ADD
ROT_TWO
STORE_ATTR   n        ← 값 쓰기   (A가 42 저장 → B의 증가분 사라짐)
```

- GIL은 "바이트코드 **하나**가 실행되는 동안" 끊기지 않게 할 뿐, **바이트코드 여러 개로 된 read-modify-write**는 보호하지 않는다
- C에서 `counter++`가 `LDR → ADD → STR` 세 명령이라 ISR이 그 사이에 끼면 값이 사라지는 것과 **같은 구조**다
- 그래서 핵심 관점: **race는 "병렬"이 아니라 "인터리빙"에서 생긴다.** 싱글 코어 STM32에서도 ISR 때문에 race가 나듯, GIL 아래 Python에서도 스레드 전환 때문에 race가 난다

## 4. C 스레드와 엄밀 비교

| 항목 | C (pthread, 멀티코어) | C (싱글 코어 MCU, ISR + main) | CPython (GIL) |
|---|---|---|---|
| 동시에 실행되는 것 | 여러 코어에서 진짜 병렬 | 한 번에 하나, ISR이 아무 명령 사이에 끼어듦 | 한 번에 하나, 바이트코드 사이에서 전환 |
| 끊기는 단위 | 명령어(사실상 아무 데나) | 명령어 사이 | 바이트코드 사이 (5ms 요청 또는 blocking 호출) |
| 보호 없는 공유 쓰기의 결과 | **data race = UB** (컴파일러가 가정을 깨도 됨) | 값 손실, torn read | **틀린 값** (인터프리터 자체는 안 깨짐, UB 아님) |
| 메모리 재배치 | 컴파일러 + CPU 둘 다 → acquire/release, barrier 필요 | 컴파일러 재배치 → `volatile`/atomic/barrier | Python 코드 수준에선 사실상 안 보임 (GIL 넘김이 동기화 지점) |
| `volatile` | 동기화 아님 | ISR 공유 플래그에 필요(가시성), 원자성은 아님 | 해당 개념 없음 |
| 원자적인 것 | `_Atomic` 연산, 정렬된 워드 load/store(플랫폼별) | 정렬된 워드 load/store, `LDREX/STREX`, 인터럽트 잠깐 끄기 | 바이트코드 하나로 끝나는 내장 연산 — 예: `list.append`, `deque.append/popleft`, dict 대입 (**CPython 구현 세부**) |
| 도구 | mutex, condvar, semaphore, atomics | 인터럽트 disable 구간, SPSC 단일 writer, atomics | `Lock`, `RLock`, `Condition`, `Event`, `queue.Queue` |
| CPU 병렬이 필요하면 | 스레드 그대로 | (코어가 하나) | `multiprocessing`, GIL 놓는 C 확장, free-threaded 빌드 |

> 정리: **Python 스레드 = "선점형 싱글 코어"에 가깝다.** 그래서 C의 멀티코어용 도구(acquire/release, 캐시 일관성, false sharing)는 Python에선 거의 의미가 없고, 대신 **"어떤 연산이 한 번에 끝나나(원자성)"와 "기다림(Condition/Queue)"**이 핵심이다.

## 5. ring buffer에 적용하면

| | C (onsitePrep) | Python (문제 05 · 드릴 P4-05) |
|---|---|---|
| SPSC | 단일 writer + `head`는 producer만, `tail`은 consumer만 + **release/acquire**로 데이터 → 인덱스 순서 보장. lock 없음 | `count += 1`을 두 스레드가 건드리면 깨진다 → **Lock 하나 + Condition 두 개** |
| 기다림 | ISR은 못 기다림 → RTOS 세마포어 / task notification | `Condition.wait(timeout)`을 `while` 안에서 |
| 실무 답 | lock-free SPSC 링 | **`queue.Queue`** (내부가 정확히 Lock + Condition), 또는 `deque` (append/popleft가 CPython에서 thread-safe) |
| 흔한 오해 | "`volatile`이면 된다" → 원자성 · 순서 보장 없음 | "GIL이 있으니 된다" → read-modify-write는 보호 안 됨 |

- 그래서 Python 링에서 C처럼 "인덱스 두 개로 lock-free"를 흉내 내는 건 의미가 없다. Python에서 lock은 싸고, 진짜 병렬이 아니라서 lock-free의 이점(코어 간 경합 제거)도 없다
- 관련: [N03 ring buffer Python · C](2026-10-02_N03_ring_buffer_python_c.md) 4절 "Is your ring buffer thread-safe?" · [문제 05 blocking ring](../python/problems/05_blocking_ring.md) · [onsitePrep ring_buffer_spsc.c](../../onsitePrep/site/code/ring_buffer_spsc.c.html)

## 6. 테스트 자동화에서의 의미 — 그래서 무엇을 쓰나

| 작업 | 성격 | 선택 |
|---|---|---|
| 시리얼 · 소켓에서 계속 읽기, 여러 DUT 동시 제어 | I/O 대기 | **threading** (reader 스레드 + `queue.Queue`) 또는 asyncio |
| 이벤트가 아주 많은 네트워크 · 프로토콜 | I/O 대기, 연결 수 많음 | **asyncio** (스레드 하나, `await` 지점에서만 전환 → race 걱정이 적음) |
| 큰 로그 · 캡처 파일 파싱, 통계 | CPU 계산 | **multiprocessing** / `ProcessPoolExecutor`, 또는 numpy |
| 리그 여러 대에서 테스트 병렬 실행 | 프로세스 단위 | **pytest-xdist** (워커 = 프로세스, 리그 하나씩) |
| µs 타이밍 측정 | Python 지연이 ms 단위로 흔들림 | Python에서 재지 말고 **외부 장비(LA · 타이머 MCU)** — [S00 3절](../design/2026-10-03_S00_overview.md) "독립 관측" |

- HIL 하네스는 대부분 **I/O-bound** → GIL은 실무에서 거의 병목이 아니다. 대신 **타이밍 정밀도**는 Python에 기대지 않는다 (GC · 스케줄링 · switch interval 때문에 ms 단위 지터)

## 7. 면접 영어 답 (30초)

**Q. Isn't Python single-threaded?**

> "Not quite. CPython threads are real OS threads, but the GIL lets only one of them execute Python bytecode at a time. So you get concurrency, not parallelism, for Python code. Threads still help a lot for I/O — serial ports, sockets — because blocking calls release the GIL. For CPU-heavy work I'd use processes, or a C extension that releases the GIL."

**Q. Does the GIL make my code thread-safe?**

> "No. The GIL protects the interpreter, not my invariants. A thread switch can happen between bytecodes, so something like `count += 1` can lose updates — I measured that on my laptop: four threads lost over a hundred thousand increments out of eight hundred thousand. It's the same shape of bug as an ISR interrupting a read-modify-write on a single-core MCU. So shared state still needs a lock, or better, a `queue.Queue`."

**Q. How is that different from C threads?**

> "In C on a multicore part, threads really run in parallel, and an unsynchronized shared write is a data race — undefined behavior — plus I have to think about compiler and CPU reordering, so I use atomics with acquire/release or a mutex. In CPython the race just gives wrong values, and reordering isn't visible at the Python level. Python threads behave more like a preemptive single core."

**Q. Threads, multiprocessing or asyncio for a test harness?**

> "Mostly threads or asyncio, because a harness spends its time waiting on serial ports and instruments. Multiprocessing — or pytest-xdist — when I'm crunching large logs or running rigs in parallel. And I never use Python itself to measure microsecond timing; that's what a logic analyzer is for."

## 체크

- [ ] `python3 experiments/gil/py_threads_gil.py`를 직접 돌려서 2번(손실)과 3번(CPU)을 눈으로 확인
- [ ] 3절 바이트코드 그림을 보고 "race는 병렬이 아니라 인터리빙에서 생긴다"를 한 문장으로
- [ ] 4절 표에서 C(멀티코어) · MCU(ISR) · CPython 세 칸을 보지 않고 말하기
- [ ] 7절 Q1 · Q2를 소리 내어 한 번씩
- [ ] playground에서 lock 없는 카운터 → 손실 확인 → `with lock:`으로 고치기를 빈 파일에서
