# L3. 임베디드 C++ — 공짜인 것과 값을 치르는 것

> **이 레벨의 목표**: 힙도 예외도 RTTI도 없는 환경에서 C++의 어느 기능이 "C와 같은 기계어"가 되고 어느 기능이 flash를 먹는지 구분한다. 그리고 그 판단을 면접에서 한 문장으로 말한다.
> **수록 문제**: 18 raii_guard · 19 static_vector · 20 register_wrapper · 21 object_pool
> **언제 다시 보나**: C++로 된 드라이버·BSP 코드를 리뷰할 때, "여기서 C++ 써도 되나?"를 판단해야 할 때, 코드 크기가 갑자기 늘었을 때.
> **개념 배경**: 이 노트는 [C02 제약 환경의 C/C++ §8](../concepts/C02_c_cpp_constrained.html)을 전제로 한다. 그쪽이 "무엇을 끄는가"의 표와 빌드 옵션이고, 이 노트는 "그 위에서 무엇을 어떻게 쓰는가"다. 중복되는 설명은 반복하지 않고 링크로 둔다.

---

## 0. 왜 이게 면접에 나오나

Hark 같은 오디오 기기 펌웨어는 대개 C와 C++이 섞여 있다. 부트·벤더 SDK·레거시 드라이버는 C, 그 위의 상태기계·파이프라인·서비스 계층은 C++인 구성이 흔하다. 그래서 면접관이 확인하려는 것은 "C++을 아는가"가 아니라 이 세 가지다.

1. **비용을 아는가.** C++ 기능을 쓰면서 그것이 flash, RAM, 인터럽트 지연에 얼마를 더하는지 말할 수 있는가. `-fno-exceptions`가 왜 붙어 있는지 설명할 수 있는가.
2. **힙 없이 객체를 다룰 수 있는가.** `new`가 금지된 곳에서 생성자와 소멸자를 어떻게 제어하는가. placement new와 명시적 소멸자 호출을 손으로 쓸 수 있는가.
3. **하드웨어 상태를 타입으로 지킬 수 있는가.** 인터럽트 잠금, 레지스터 필드, 객체 수명 같은 것을 "잊으면 컴파일이 안 되는" 구조로 만들 수 있는가.

이 레벨의 네 문제는 정확히 그 세 가지다. 18은 스코프로 하드웨어 상태를 지키고, 19와 21은 힙 없이 객체 수명을 관리하고, 20은 비트 실수를 컴파일 에러로 바꾼다.

---

## 1. 비용 지도 — 공짜인 것과 값을 치르는 것

### 1.1 거의 공짜 (`-O2`에서 C와 같은 기계어)

| 기능 | 왜 공짜인가 | 확인 방법 |
|---|---|---|
| 클래스와 멤버 함수 | `this` 포인터를 넘기는 자유 함수와 같다. 인라인되면 호출조차 사라진다 | `objdump -d`로 C 버전과 비교 |
| RAII 가드 | 생성자·소멸자가 인라인되어 잠금/복원 명령 두 개만 남는다 | 문제 18에서 `sizeof(IrqLock) == 4` |
| `constexpr` 함수와 변수 | 컴파일 타임에 계산이 끝난다. 런타임 코드가 0 | map에서 `.text`가 아니라 `.rodata`에 보인다 |
| 참조, `nullptr`, `enum class` | 순수한 타입 수준 장치 | 생성 코드 없음 |
| 템플릿 (인스턴스가 하나일 때) | 그 타입 전용 코드 한 벌 = 손으로 쓴 것과 같다 | `nm --size-sort`로 크기 확인 |
| `std::move`, `std::forward` | 캐스팅이다. 런타임 동작이 없다 | 생성 코드 없음 |
| `static_assert`, 타입 트레이트 | 컴파일 타임 전용 | 생성 코드 없음 |

### 1.2 값을 치르는 것

| 기능 | 무엇을 치르나 | 임베디드 관행 |
|---|---|---|
| 예외 | 언와인드 테이블(`.ARM.exidx`, `.ARM.extab`)과 언와인더 런타임. 툴체인에 따라 수 KB에서 수십 KB. 던지는 시간이 비결정적 | `-fno-exceptions`. 실패는 반환값 |
| RTTI | 클래스마다 `type_info`와 이름 문자열 | `-fno-rtti` |
| 가상 함수 | 객체마다 vptr 4바이트, 클래스마다 vtable(flash), 호출마다 간접 분기(파이프라인·분기 예측 손해), 인라인 불가 | 인터페이스가 정말 여러 구현을 가질 때만 |
| 템플릿 (인스턴스 여러 개) | 인스턴스마다 코드 복제. `Ring<uint8_t,64>`와 `Ring<uint8_t,128>`은 두 벌 | thin template 패턴(1.3) |
| 함수 지역 `static` 객체 | 스레드 안전 초기화 가드(`__cxa_guard_acquire`)와 `.bss` 플래그 | `-fno-threadsafe-statics` 또는 전역으로 |
| `std::function` | 캡처가 내부 버퍼를 넘으면 힙 할당 | 함수 포인터 + `void* ctx`, 또는 고정 크기 delegate |
| `dynamic_cast`, `typeid` | RTTI 필요 | 금지. 태그 필드나 방문자 패턴 |
| iostream, `std::string`, `std::vector` | 힙과 수십~수백 KB | 금지. 문제 19의 `static_vector`, 고정 버퍼 |

### 1.3 템플릿 코드 복제를 줄이는 thin template

```cpp
/* 공통 로직은 크기를 인자로 받는 비템플릿 베이스에 한 번만 */
class RingBase {
protected:
    RingBase(unsigned char *buf, std::size_t cap) : buf_(buf), cap_(cap) {}
    bool push_impl(unsigned char v);   /* 코드가 여기 한 벌만 생긴다 */
    unsigned char *buf_;
    std::size_t cap_, head_ = 0, tail_ = 0;
};

/* 템플릿은 저장소와 타입만 담당한다 */
template <std::size_t N>
class Ring : private RingBase {
public:
    Ring() : RingBase(storage_, N) {}
    bool push(unsigned char v) { return push_impl(v); }   /* 인라인 래퍼 */
private:
    unsigned char storage_[N];
};
```

`Ring<64>`와 `Ring<256>`이 공유하는 `push_impl`은 한 벌이다. 크기만 다른 인스턴스를 여러 개 쓰는 코드에서 이 패턴은 수 KB를 줄인다.

---

## 2. 예외와 RTTI를 끄면 무엇이 금지되나

### 2.1 왜 끄나

- **flash와 RAM**: 언와인드 테이블은 "던지지 않아도" 링크된다. 예외를 한 번도 안 쓰는 펌웨어에서도 섹션이 남는다.
- **결정성**: `throw`부터 `catch`까지 걸리는 시간은 호출 깊이와 소멸자 개수에 달려 있다. 인터럽트 지연을 계산해야 하는 코드에 이런 것을 둘 수 없다.
- **스택**: 언와인딩은 추가 스택을 쓴다. 이미 8 KB를 쪼개 쓰는 상황에서 감당하기 어렵다.
- **RTTI**: `dynamic_cast`가 없으면 잘못된 다운캐스팅이 원천적으로 불가능하고, `type_info` 문자열도 사라진다.

중요한 오해 하나. `-fno-rtti`는 **가상 함수를 끄지 않는다**. vtable은 그대로 동작한다. 사라지는 것은 `dynamic_cast`와 `typeid`뿐이다.

### 2.2 금지되는 것과 대체

| 예외가 없으면 못 하는 것 | 대체 |
|---|---|
| 생성자에서 실패를 알리는 것 | 생성자는 실패하지 않게 설계한다. 하드웨어 초기화는 별도 `init()`이 `bool`을 돌려준다 |
| `operator new`의 `std::bad_alloc` | 힙 자체를 안 쓴다. 고갈은 `nullptr` 반환(문제 21) |
| `at()`의 범위 검사 예외 | `assert` + 디버그 빌드, 또는 `bool`/포인터 반환(문제 19) |
| 소멸자에서의 오류 보고 | 소멸자는 반드시 성공하는 연산만 한다. PRIMASK 복원은 실패할 수 없다 |
| `std::optional`의 `value()` throw | `has_value()` 확인 후 접근. 또는 포인터 반환 |

그래서 이 레벨의 API는 전부 "실패를 반환값으로 말한다". `emplace_back`은 `nullptr`, `acquire`는 `nullptr`, `release`는 열거형 코드다. 이것은 스타일 선택이 아니라 `-fno-exceptions`의 직접적 결과다.

---

## 3. RAII — 스코프가 곧 하드웨어 상태 (문제 18)

### 3.1 C 버전의 구조적 결함

```c
uint32_t s = irq_lock();
if (credits < n) {
    return false;          /* <-- 여기서 unlock을 빠뜨렸다 */
}
credits -= n;
irq_unlock(s);
return true;
```

리뷰에서 이 버그를 놓치는 이유는, 잠금과 해제가 **다른 줄**에 있고 그 사이에 분기가 들어갈 수 있기 때문이다. 시간이 지나면서 누군가 `return`을 하나 추가하면 끝이다. 증상은 "몇 시간 뒤 버튼이 안 먹는다" 같은 형태로 나타나고, 원인 지점과 증상 지점이 멀어 디버깅이 오래 걸린다.

RAII는 이 문제를 문법적으로 제거한다. 스코프를 벗어나는 모든 경로 — `return`, `break`, `goto`, 중첩 블록 탈출 — 에서 소멸자가 호출되는 것은 컴파일러의 책임이다.

### 3.2 인터럽트 가드의 규칙 네 가지

1. **이전 상태를 저장하고 복원한다.** `set_primask(0)`을 하드코딩하면 중첩이 깨진다. 안쪽 가드가 바깥 구간의 잠금을 풀어 버리고, 그 뒤 코드는 자기가 보호받고 있다고 착각한다. 이것이 중첩 테스트가 필요한 이유다.
2. **복사와 이동을 모두 삭제한다.** 복사되면 소멸자가 두 번 돌아 잠금이 일찍 풀린다. 이동을 허용하려면 "이미 넘겨줬음" 플래그가 필요해져서 가드가 4바이트에서 8바이트로 커지고 소멸자에 분기가 생긴다. 스코프에 붙어 있는 것이 목적이므로 이동할 이유가 없다.
3. **이름 있는 지역 변수로 만든다.** `IrqLock();`은 임시 객체라 그 자리에서 바로 파괴된다. 아무 구간도 보호하지 못하면서 컴파일은 된다. 실제로 자주 나오는 실수다.
4. **구간을 짧게 유지한다.** 가드가 잠그는 사이클 수가 곧 시스템 최악 인터럽트 지연에 더해진다. 잠금 안에서 `printf`나 flash 쓰기를 하면 안 된다.

### 3.3 ISR 안에서의 RAII

ISR 안에서 RAII 자체는 완전히 안전하다. 문제가 되는 것은 "무엇을 하는 가드인가"다.

| ISR에서 | 판단 |
|---|---|
| 인터럽트 잠금 가드 | 가능. Cortex-M에서 같은 우선순위 ISR은 이미 재진입하지 않지만, 더 높은 우선순위를 막아야 한다면 의미가 있다. 다만 구간이 극히 짧아야 한다 |
| 뮤텍스/세마포어 가드 | 금지. ISR에서 블로킹은 데드락이다. RTOS의 `FromISR` API를 쓴다 |
| 함수 지역 `static` 객체의 첫 생성 | 위험. 초기화 가드가 처음 도는 지점이 ISR이면 재진입 문제가 생긴다 |
| 힙을 만지는 가드 | 금지. 애초에 힙이 없다 |
| 소멸자가 오래 걸리는 객체 | 금지. 소멸자도 ISR 시간 예산 안에 들어가야 한다 |

가드가 ISR 진입 시점의 PRIMASK를 저장한다는 점도 기억할 것. ISR은 보통 PRIMASK가 0인 상태로 진입하므로(우선순위로 가려진 것이지 마스킹된 것이 아니다) 가드는 정상 동작한다.

---

## 4. 객체 수명을 손으로 관리하기 — placement new (문제 19, 21)

### 4.1 무엇을 하고 무엇을 하지 않나

```cpp
alignas(T) unsigned char buf[sizeof(T)];

T *p = ::new (static_cast<void *>(buf)) T(args...);   /* 생성자만 실행. 할당은 없다 */
p->~T();                                             /* 소멸자만 실행. 해제는 없다 */
```

- placement new는 **메모리를 할당하지 않는다.** 주어진 주소에서 생성자를 실행할 뿐이다. `<new>` 헤더만 있으면 되고 힙 런타임을 끌어오지 않는다.
- 짝이 되는 것은 `delete`가 아니라 **명시적 소멸자 호출**이다. placement new로 만든 객체에 `delete`를 쓰면 힙 해제를 시도해서 즉시 깨진다.
- 저장소를 재사용하기 전에 반드시 소멸자를 불러야 한다. 이것을 빠뜨리는 것이 "정적 메모리에서의 누수"다. 메모리는 그대로 있지만 소멸자가 반납하기로 한 자원(채널, 핸들, 참조 카운트)이 영구히 잠긴다.

### 4.2 정렬

저장소는 `alignas(T)`로 정렬을 올려야 한다. `unsigned char buf[sizeof(T)]`의 정렬은 1이다. `T`가 8바이트 정렬을 요구하면(예: `double`이나 `uint64_t` 멤버) 정렬이 어긋난 주소에서 생성자가 돌고, Cortex-M0+나 M3에서는 unaligned 접근이 HardFault다.

x86 host 테스트에서는 이 버그가 **재현되지 않는다.** x86은 unaligned 접근을 허용하기 때문이다. 그래서 정렬은 테스트가 아니라 `static_assert`와 `alignas`로 지켜야 한다.

### 4.3 `std::launder`와 저장소 재사용

같은 저장소에 객체를 파괴하고 다시 생성하면, 이전에 얻은 포인터를 계속 쓰는 것은 엄격하게는 미정의다. 컴파일러가 "그 주소에 있는 객체는 여전히 옛 객체"라고 가정해 최적화할 수 있다. `std::launder`(C++17, `<new>`)는 "이 주소에 새 객체가 놓였다"고 알려 주는 표식이고, 런타임 비용은 0이다.

문제 19와 21이 저장소에서 원소 포인터를 꺼낼 때마다 `std::launder`를 통과시키는 이유가 이것이다. 실무에서 이것 없이도 대개 동작하지만, 면접에서 "왜 launder를 쓰나"를 물으면 "저장소 재사용 뒤 포인터의 유효성 문제"라고 답할 수 있어야 한다.

### 4.4 파괴 순서

파괴는 생성의 역순이다. 스택 변수, 멤버 변수, 배열 원소 모두 그렇다. `static_vector::clear()`도 뒤에서 앞으로 돈다.

```
생성: [0] [1] [2] [3] [4]        ->  파괴: [4] [3] [2] [1] [0]
```

왜 중요한가. 앞 원소가 뒤 원소보다 먼저 만들어졌다면, 뒤 원소가 앞 원소를 참조하고 있을 수 있다. 역순으로 파괴하면 "참조하는 쪽이 먼저 죽는다"가 보장된다. 순서를 뒤집으면 이미 파괴된 객체를 소멸자가 건드리는 경로가 열린다.

---

## 5. 고정 용량 컨테이너와 객체 풀

### 5.1 `static_vector<T, N>`의 불변식 (문제 19)

```
buf_:  [ T(0) ][ T(1) ][ T(2) ][ ---- ][ ---- ]      size_ = 3, N = 5
        ^^^^^^^^^^^^^^^^^^^^^^  살아 있는 객체
                                ^^^^^^^^^^^^^^  아직 객체가 아닌 raw 바이트
```

- `[0, size_)`에는 살아 있는 객체가, `[size_, N)`에는 아무 객체도 없다. 이 불변식이 코드 전체를 지배한다.
- 생성이 성공한 **뒤에** `size_`를 올린다. `size_`를 내린 **뒤에** 소멸자를 부른다. 순서를 지키면 어느 시점에 중단되어도 불변식이 유지된다.
- 이동은 `O(N)`이다. `std::vector`처럼 포인터만 훔칠 수 없다. 저장소가 객체 안에 박혀 있기 때문이다. 이것이 "고정 용량 컨테이너의 이동은 공짜가 아니다"의 의미다.
- 이동 대입은 대상의 기존 원소를 **먼저 파괴**해야 한다. 빠뜨리면 소멸자가 실행되지 않은 객체가 덮여 사라진다.

### 5.2 문제 21의 `object_pool` vs 문제 04의 memory pool

같은 free list, 다른 책임이다. 이 표가 면접 답변의 골자다.

| | 04 memory pool (C) | 21 object_pool (C++) |
|---|---|---|
| 빌려주는 것 | `void*` raw 블록 | 생성이 끝난 `T*` |
| 크기 | 런타임에 `POOL_BLOCK_SIZE`로 고정 | `sizeof(T)`로 컴파일 타임 결정 |
| 정렬 | `max_align_t`로 최대 정렬까지 끌어올림(어떤 타입이 올지 모름) | `alignof(T)`만큼. 낭비가 없다 |
| alloc 시 | 바이트를 건네준다. 내용은 쓰레기 | 생성자를 호출한다. 불변식이 성립한 객체가 나온다 |
| free 시 | 링크만 되돌린다 | 소멸자를 호출한 뒤 링크를 되돌린다 |
| 호출자의 의무 | 초기화와 정리를 직접 | 없음. `acquire`/`release`가 다 한다 |
| 풀 파괴 시 | 할 일이 없다 | 살아 있는 객체의 소멸자를 대신 호출해야 한다 |
| 살아 있는 표시 | double free 검출용 bitmap | 소멸자 호출 대상을 알기 위해 필수 |
| 타입 안전 | 없음. `void*`를 아무 타입으로나 캐스팅 | 있음. `T` 말고 나올 수 없다 |
| 반납 누락의 결과 | 블록 하나가 영구히 나감 | 블록 + `T`의 소멸자가 반납할 자원이 영구히 잠김 |

한 문장으로: **04는 메모리를 관리하고 21은 객체 수명을 관리한다.** free list는 같지만, 21은 "어느 슬롯이 살아 있는가"를 알아야 하고 풀 소멸자가 남은 객체를 정리할 책임을 진다.

### 5.3 acquire의 순서 함정

```cpp
Slot *s = free_head_;
free_head_ = s->next;                    /* (1) 링크를 먼저 읽는다 */
T *obj = ::new (s->storage) T(args...);  /* (2) 그 다음에 저장소를 덮는다 */
```

`next`와 `storage`는 union이므로 **같은 메모리**다. (2)를 먼저 하면 생성자가 `next` 링크를 덮어쓰고, 그 뒤 `s->next`를 읽으면 객체의 첫 바이트를 포인터로 해석한다. free list가 엉뚱한 주소로 이어지고, 다음 `acquire`가 풀 밖의 메모리를 돌려준다. 증상은 "가끔 이상한 주소가 나온다"이고 재현이 어렵다.

---

## 6. `constexpr` — 런타임 계산을 0으로 (문제 20)

### 6.1 마스크와 분주비

```cpp
constexpr std::uint32_t ones(std::uint32_t width)
{
    return (width >= 32u) ? 0xFFFFFFFFu : ((std::uint32_t{1} << (width & 31u)) - 1u);
}
```

`width`를 인자로 받는 이유가 있다. `1u << 32`는 미정의 동작이고, 상수 32를 직접 시프트하면 컴파일러가 경고까지 낸다. 인자로 받아 `width & 31`로 마스킹하면 그 경로가 아예 생기지 않는다. 경계값을 타입/문법 수준에서 지우는 전형적인 방법이다.

```cpp
constexpr std::uint32_t kBaudDiv = (kSysClkHz + kBaud / 2u) / kBaud;
static_assert(F_BaudDiv::fits(kBaudDiv), "분주비가 필드 폭을 넘는다");
```

클럭을 바꿔 분주비가 필드 폭을 넘어가면 **빌드가 멈춘다.** 보드를 켜서 UART가 깨진 것을 보고 알아내는 것과, 빌드 로그에서 아는 것의 차이다.

### 6.2 컴파일 타임 테이블

CRC 테이블, 감마 보정 테이블, 필터 계수처럼 순수 계산으로 나오는 표는 `constexpr` 함수로 만들어 `.rodata`에 놓는다. 얻는 것이 세 가지다. 런타임 초기화 코드가 없고, 테이블용 RAM이 없고, `static_assert`로 알려진 체크값을 검증할 수 있다. 구체적인 코드는 [C02 §8.3](../concepts/C02_c_cpp_constrained.html)에 있다.

### 6.3 레지스터 초기값을 한 번에 쓰기

필드를 하나씩 `set`하면 필드 개수만큼 read-modify-write가 일어난다. 문제 20의 테스트가 이것을 숫자로 보여준다. 필드 7개를 따로 쓰면 MMIO 읽기 7회 + 쓰기 7회, `constexpr`로 조립한 상수를 한 번 쓰면 읽기 0회 + 쓰기 1회다.

접근 횟수만 문제가 아니다. 중간 상태가 하드웨어에 노출된다. "보 레이트는 새 값인데 패리티는 아직 옛 값"인 상태로 한 바이트가 전송될 수 있고, enable 비트를 나중에 켜기로 했다면 설정 순서가 곧 버그가 된다. 그래서 초기화는 상수 하나로, 실행 중 변경은 필요한 필드만 한 번의 RMW로 한다.

---

## 7. 템플릿 vs 가상 함수 — 어느 쪽이 작은가

같은 "여러 구현을 하나의 인터페이스로" 요구를 푸는 두 방법이다. 판단 기준은 **구현이 몇 개이고, 런타임에 바뀌는가**다.

| | 템플릿 (컴파일 타임 다형성) | 가상 함수 (런타임 다형성) |
|---|---|---|
| 결정 시점 | 컴파일 타임 | 런타임 |
| 호출 | 직접 호출, 인라인 가능 | 간접 분기. 인라인 불가 |
| 객체 크기 | 추가 0 | vptr 4바이트 |
| flash | 인스턴스마다 코드 한 벌 | 코드 한 벌 + 클래스마다 vtable |
| 인스턴스 2개 | 코드 2벌. 가상 함수보다 커질 수 있다 | 코드 1벌 |
| 인스턴스 10개 | 코드 10벌. 거의 항상 더 크다 | 코드 1벌 |
| 최적화 | 상수 전파, 루프 언롤이 타입별로 들어간다 | 호출 지점에서 최적화가 끊긴다 |
| 실행 중 교체 | 불가 | 가능 |
| 디버깅 | 에러 메시지가 길다 | 스택에서 어느 구현인지 보기 쉽다 |

실무의 결론은 대개 이렇다.

- **컴파일 타임에 결정되는 것은 템플릿.** 버퍼 크기, 레지스터 필드, 타이머 개수 같은 것. 문제 19, 20, 21이 전부 이 경우다.
- **런타임에 바뀌는 것은 가상 함수.** 코덱 플러그인, 전송 계층 선택, 테스트용 목(mock) 교체. 인스턴스가 늘어날수록 이쪽이 작다.
- **둘 다 아닌 세 번째 선택**: 함수 포인터 테이블 + `void* ctx`(문제 13). vptr도 vtable도 없고 C와 상호 운용된다. BSP 드라이버 인터페이스의 표준 관행이다.

크기를 실제로 확인하는 명령은 이것이다. 추측으로 말하지 말고 숫자를 본다.

```
arm-none-eabi-nm -C --size-sort --print-size build/fw.elf | tail -30
arm-none-eabi-size -A build/fw.elf
```

---

## 8. 정적 초기화 순서와 시작 시점

전역 객체의 생성자는 `main()`보다 먼저 돈다. 문제는 **서로 다른 번역 단위의 전역 객체 생성 순서가 표준에 정해져 있지 않다**는 것이다. 링크 순서에 따라 달라진다.

```
uart.cpp:   Uart g_uart(115200);     // 생성자가 클럭 객체를 참조한다
clock.cpp:  ClockTree g_clock;       // 생성자가 PLL을 설정한다
```

`g_uart`가 먼저 생성되면 아직 PLL이 설정되지 않은 상태로 보 레이트를 계산한다. 이것이 static initialization order fiasco다. 펌웨어에서 특히 나쁜 이유는, 이 시점이 클럭·전원·워치독 설정 전일 수 있고 디버거를 붙이기도 어려운 구간이라는 점이다.

회피 원칙 세 가지.

1. **전역 객체의 생성자는 하드웨어를 건드리지 않는다.** 멤버 초기화만 한다. 실제 초기화는 `main()`에서 명시적 순서로 `init()`을 부른다. 순서가 코드에 적혀 있으면 리뷰할 수 있다.
2. **생성 시점을 직접 고른다.** 저장소를 `static`으로 두고, 클럭 설정이 끝난 뒤 placement new로 만든다(4.1). 문제 21의 풀이 그 구조다.
3. **`constexpr` 생성자로 만들어 컴파일 타임에 초기화를 끝낸다.** 그러면 순서 문제가 원천적으로 없고 `.data`나 `.rodata`에 값이 그대로 놓인다.

관련 빌드 옵션은 [C02 §8.1](../concepts/C02_c_cpp_constrained.html)에 정리되어 있다. `-fno-threadsafe-statics`(함수 지역 static의 가드 제거)와 `-fno-use-cxa-atexit`(종료 시 소멸자 등록 제거)가 자주 같이 붙는다. 펌웨어는 `main`에서 돌아오지 않으므로 전역 소멸자가 필요 없다.

---

## 흔한 함정

| 함정 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 가드를 임시 객체로 만든다 (`IrqLock();`) | 보호가 전혀 안 되는데 컴파일은 된다. 간헐적 데이터 손상 | 임시 객체는 그 문장 끝에서 파괴된다 | 이름 있는 지역 변수로 만든다 |
| 소멸자에서 `set_primask(0)` 하드코딩 | 중첩 구간의 바깥 잠금이 일찍 풀린다 | 이전 상태를 저장하지 않았다 | 생성자에서 저장, 소멸자에서 복원 |
| 가드를 복사·이동 가능하게 둔다 | 소멸자가 두 번 돌아 잠금이 조기 해제 | 기본 생성된 복사 생성자 | 네 가지 모두 `= delete` |
| placement new 뒤 `delete` 사용 | 힙 해제 시도. 즉시 깨지거나 조용히 망가진다 | 짝이 되는 것은 명시적 소멸자 호출 | `p->~T()` |
| 저장소 재사용 전 소멸자 누락 | 메모리는 멀쩡한데 채널·핸들이 고갈된다 | 정적 메모리에서의 "누수"는 자원 누수다 | `release`/`clear`가 반드시 소멸자를 부르게 |
| `alignas(T)` 누락 | host 테스트는 통과, 타깃에서 HardFault | `unsigned char[]`의 정렬은 1 | 저장소에 `alignas(T)`와 `static_assert` |
| 파괴를 앞에서 뒤로 | 이미 파괴된 객체를 참조하는 소멸자 | 생성 역순이 아니다 | 뒤에서 앞으로 |
| 이동 대입에서 기존 원소 파괴 누락 | 소멸자가 실행되지 않은 객체가 사라진다 | `clear()`를 먼저 부르지 않았다 | 대입 전에 파괴, 자기 대입은 조기 반환 |
| union 슬롯에서 생성 후 `next` 읽기 | free list가 풀 밖을 가리킨다 | `next`와 `storage`가 같은 메모리 | 링크를 먼저 읽고 나서 생성 |
| 필드 폭을 넘는 상수를 매크로로 쓴다 | 이웃 필드가 조용히 바뀐다 | 매크로는 범위 검사를 하지 않는다 | `static_assert(F::fits(V))` |
| 필드를 하나씩 `set` | MMIO 접근이 필드 수만큼. 중간 상태가 노출된다 | RMW가 매번 일어난다 | `constexpr` 상수 하나 또는 한 번의 `modify` |
| `N`만 다른 템플릿 인스턴스 남발 | flash가 조용히 늘어난다 | 인스턴스마다 코드 복제 | thin template (1.3) |
| 전역 객체 생성자에서 하드웨어 초기화 | 부팅이 링크 순서에 따라 실패한다 | 번역 단위 간 초기화 순서 미정 | `main()`에서 명시적 `init()` |

---

## 면접 질문 10개

**1. 펌웨어에서 예외를 끄는 이유는?**
30초 답: 세 가지다. 첫째 코드 크기 — 언와인드 테이블과 언와인더가 던지지 않아도 링크된다. 둘째 결정성 — `throw`부터 `catch`까지의 시간이 호출 깊이에 따라 달라져서 인터럽트 지연을 계산할 수 없다. 셋째 스택 — 언와인딩이 추가 스택을 쓴다. 대신 실패는 반환값으로 알린다. 생성자는 실패하지 않게 설계하고, 하드웨어 초기화는 별도 `init()`이 `bool`을 돌려주게 만든다.
English: "We disable exceptions for code size, for determinism, and for stack budget, so failure has to come back as a return value instead."

**2. RAII가 C의 lock/unlock보다 나은 점을 한 문장으로 말해 보라.**
30초 답: 해제가 스코프에 붙기 때문에 나중에 누가 `return`을 추가해도 해제가 빠지지 않는다. C 버전은 잠금과 해제가 다른 줄에 있어서 그 사이에 분기가 생기면 리뷰로 잡아야 하는데, RAII는 컴파일러가 보장한다. `-O2`에서 생성 코드는 C 버전과 같다.
English: "RAII ties the release to the scope, so a `return` added later can't skip it, and at `-O2` it compiles to the same instructions as the C version."

**3. 인터럽트 가드의 소멸자가 인터럽트를 그냥 켜면 안 되는 이유는?**
30초 답: 중첩이 깨진다. 이미 잠긴 구간 안에서 잡은 안쪽 가드가 바깥 구간의 잠금을 풀어 버리고, 그 뒤 코드는 보호받고 있다고 착각한 채로 공유 상태를 만진다. 그래서 생성자에서 이전 PRIMASK를 저장하고 소멸자는 그 값을 복원한다. 중첩 테스트로 확인한다.
English: "The destructor must restore the saved PRIMASK, not blindly enable interrupts, otherwise a nested guard unlocks the outer critical section."

**4. placement new는 무엇을 하고 무엇을 하지 않나?**
30초 답: 메모리를 할당하지 않고, 주어진 주소에서 생성자만 실행한다. `<new>` 헤더만 필요하고 힙 런타임을 끌어오지 않는다. 짝은 `delete`가 아니라 명시적 소멸자 호출이다. 저장소는 `alignas(T)`로 정렬을 올려야 하고, 저장소를 재사용하기 전에 반드시 소멸자를 불러야 한다.
English: "Placement new allocates nothing — it just runs the constructor at an address you already own, and its counterpart is an explicit destructor call, not `delete`."

**5. 정적 메모리를 쓰는데도 "누수"가 있을 수 있나?**
30초 답: 있다. 메모리는 정적이라 사라지지 않지만, 소멸자가 반납하기로 한 자원 — 믹서 채널, DMA 스트림, 참조 카운트 — 이 영구히 잠긴다. 그래서 객체 풀의 소멸자는 아직 살아 있는 객체의 소멸자를 대신 호출하고, 그러기 위해 어느 슬롯이 살아 있는지 추적한다. raw 블록 풀에는 없는 책임이다.
English: "The memory can't leak because it's static, but the resources the destructor was supposed to release do — which is why the pool destroys whatever is still live."

**6. 템플릿과 가상 함수 중 어느 쪽을 쓰나?**
30초 답: 결정 시점으로 나눈다. 컴파일 타임에 정해지는 것 — 버퍼 크기, 레지스터 필드 — 은 템플릿, 런타임에 바뀌는 것 — 코덱 교체, 목 객체 — 은 가상 함수다. 크기 관점에서 인스턴스가 하나면 템플릿이 작고, 여러 개면 인스턴스마다 코드가 복제되어 가상 함수가 작아진다. 크기만 다른 인스턴스가 많으면 공통 로직을 비템플릿 베이스로 빼는 thin template을 쓴다. 추측하지 않고 `nm --size-sort`로 확인한다.
English: "Templates when the choice is known at compile time, virtual functions when it changes at runtime — and I check the actual instance sizes with `nm --size-sort` instead of guessing."

**7. `constexpr`가 임베디드에서 특히 중요한 이유는?**
30초 답: 계산이 컴파일 타임에 끝나서 런타임 코드도 RAM도 쓰지 않고, 결과가 `.rodata`로 flash에 놓인다. CRC 테이블을 손으로 붙여 넣는 대신 컴파일러가 만들고 `static_assert`로 알려진 체크값을 검증할 수 있다. 더 중요한 것은 클럭 분주비 같은 설정값의 범위를 `static_assert`로 막으면 잘못된 보드 설정이 런타임 버그가 아니라 컴파일 에러가 된다는 점이다.
English: "`constexpr` moves the work to build time — no code, no RAM — and `static_assert` turns a wrong clock setting into a build error instead of a field bug."

**8. static initialization order fiasco는 무엇이고 어떻게 피하나?**
30초 답: 서로 다른 번역 단위의 전역 객체 생성 순서가 표준에 정해져 있지 않아서, UART 객체가 클럭 객체보다 먼저 생성되면 아직 설정되지 않은 PLL로 보 레이트를 계산한다. 링크 순서에 따라 달라지므로 재현도 어렵다. 피하는 방법은 전역 생성자에서 하드웨어를 건드리지 않고 `main()`에서 명시적 순서로 `init()`을 부르거나, 저장소를 정적으로 두고 원하는 시점에 placement new로 만드는 것이다.
English: "Global objects in different translation units have no defined construction order, so I keep hardware out of global constructors and initialize explicitly from `main()`."

**9. ISR 안에서 쓰면 안 되는 C++ 기능은?**
30초 답: 블로킹하는 가드 — 뮤텍스나 세마포어 — 는 데드락이므로 금지하고 RTOS의 `FromISR` API를 쓴다. 함수 지역 `static` 객체의 첫 생성도 위험하다. 초기화 가드가 ISR에서 처음 돌면 재진입 문제가 생긴다. 힙을 만지는 것, 소멸자가 오래 걸리는 객체, 가상 함수 체인이 긴 호출도 시간 예산 때문에 피한다. 반면 인터럽트 잠금 RAII 가드나 인라인되는 멤버 함수는 ISR에서 완전히 안전하다.
English: "No blocking guards, no first-time function-local statics, no heap — but a plain RAII interrupt guard inside an ISR is fine."

**10. `-fno-rtti`가 금지하는 것은 정확히 무엇인가?**
30초 답: `dynamic_cast`와 `typeid`, 그리고 클래스별 `type_info` 데이터다. 흔한 오해와 달리 **가상 함수는 그대로 동작한다.** vtable은 RTTI와 별개다. 런타임 타입 분기가 필요하면 기반 클래스에 열거형 태그를 두거나, 방문자 패턴처럼 가상 함수로 분기를 표현한다.
English: "`-fno-rtti` removes `dynamic_cast` and `typeid`, but virtual functions and vtables still work — people often confuse the two."

---

## 이 레벨 문제들

| N | 문제 | 무엇을 연습하나 | 난이도 | 목표 시간 | 이 노트의 어디 |
|---|---|---|---|---|---|
| 18 | [인터럽트 잠금 RAII 가드](../problems/18_raii_guard.html) | 생성자·소멸자로 하드웨어 상태 보호, 복사·이동 삭제, 중첩 | 기초 | 20분 | §2, §3 |
| 19 | [static_vector](../problems/19_static_vector.html) | placement new, 파괴 순서, 완벽한 전달, 용량 초과 | 중급 | 30분 | §4, §5.1 |
| 20 | [타입 안전 레지스터 필드](../problems/20_register_wrapper.html) | 템플릿 + `constexpr` 마스크, `static_assert`로 컴파일 에러화 | 중급 | 30분 | §6, §7 |
| 21 | [객체 풀](../problems/21_object_pool.html) | 타입이 있는 고정 풀, 소멸자 보장, 04와의 차이 | 심화 | 30분 | §4, §5.2 |

푸는 순서는 18 → 19 → 21 → 20을 권한다. 18로 RAII를 손에 익히고, 19에서 placement new를 처음 쓰고, 21에서 그것을 free list와 합치고, 20은 성격이 다른 템플릿 메타 연습이라 마지막에 둔다.

```
make run N=18     # 내 구현 채점
make sol N=18     # 모범답안 실행
```

---

## 체크리스트

- [ ] 예외를 끄는 이유 세 가지(코드 크기, 결정성, 스택)를 막힘 없이 말할 수 있다
- [ ] `-fno-rtti`가 가상 함수를 끄지 않는다는 것을 안다
- [ ] "거의 공짜"인 기능과 "값을 치르는" 기능을 각각 다섯 개씩 댈 수 있다
- [ ] 인터럽트 가드가 이전 PRIMASK를 저장해야 하는 이유를 중첩 시나리오로 설명할 수 있다
- [ ] 가드의 복사와 이동을 모두 삭제하는 이유를 각각 다르게 설명할 수 있다
- [ ] 임시 객체로 만든 가드가 왜 아무것도 보호하지 않는지 안다
- [ ] placement new의 짝이 `delete`가 아니라 `p->~T()`임을 안다
- [ ] `alignas(T)`를 빼먹은 버그가 x86 host 테스트에서 재현되지 않는 이유를 안다
- [ ] 파괴가 생성의 역순이어야 하는 이유를 예로 들 수 있다
- [ ] 정적 메모리에서도 "누수"가 생긴다는 것을 자원 관점으로 설명할 수 있다
- [ ] 04 memory pool과 21 object_pool의 차이를 생성자·소멸자로 설명할 수 있다
- [ ] union 슬롯에서 링크를 먼저 읽어야 하는 이유를 안다
- [ ] `1u << 32`를 피하는 `ones(width)` 구현을 그 자리에서 쓸 수 있다
- [ ] 필드를 하나씩 쓰는 것과 상수 하나를 쓰는 것의 MMIO 접근 횟수 차이를 안다
- [ ] 템플릿과 가상 함수의 코드 크기 교환을 인스턴스 개수로 설명할 수 있다
- [ ] thin template 패턴을 코드로 쓸 수 있다
- [ ] static initialization order fiasco와 회피 세 가지를 안다
- [ ] 네 문제 모두 `-Wall -Wextra` 경고 0으로 통과시켜 봤다
