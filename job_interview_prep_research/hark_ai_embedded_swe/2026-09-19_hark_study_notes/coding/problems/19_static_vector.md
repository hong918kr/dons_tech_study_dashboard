# 19. 힙 없는 고정 용량 컨테이너 `static_vector<T, N>`

> **주제**: placement new · 소멸자 명시 호출 · 파괴 순서 · 완벽한 전달 · **난이도**: 중급 · **목표 시간**: 30분
> **해설은 보지 말 것**: 먼저 `starters/19_static_vector.cpp`를 채워 `make run N=19`로 통과시킨다.

## 면접관의 문장

"We like C++ but we can't have the heap. `std::vector` is out. What we actually need is a list of up to sixteen active BLE connections, or up to eight audio streams — the maximum is known at compile time, and the objects have real constructors and destructors that register and unregister things.

Write me `static_vector<T, N>`. Storage is a byte array inside the object. Construct elements with placement new, destroy them with an explicit destructor call. `emplace_back` should forward its arguments so I don't get an extra copy.

Two things I will check. First: when the container dies, are all the live elements destroyed, and in what order? Second: what happens on the seventeenth `push_back`? We have no exceptions, so don't tell me it throws."

## 요구사항

1. 저장소는 `alignas(T) unsigned char buf_[N * sizeof(T)]` 하나다. 힙을 쓰지 않는다. 빈 컨테이너를 만들 때 `T`의 생성자는 한 번도 호출되지 않는다.
2. `capacity()`는 `N`을 돌려주는 `static constexpr` 함수다. `size()`, `empty()`, `full()`을 제공한다.
3. `emplace_back(args...)`는 인자를 `T`의 생성자에 완벽 전달(`std::forward`)해서 저장소 안에서 직접 생성하고, 새 원소의 주소를 돌려준다. 복사 생성자도 이동 생성자도 호출되면 안 된다.
4. 용량이 꽉 찬 상태의 `emplace_back`은 `nullptr`을 돌려주고 **`T`의 생성자를 호출하지 않는다**. `size()`도, 마지막 원소도 변하지 않는다.
5. `push_back(const T&)`는 복사 생성자를, `push_back(T&&)`는 이동 생성자를 호출한다. 성공/실패를 `bool`로 돌려준다.
6. `pop_back()`은 마지막 원소의 소멸자를 호출하고 `size()`를 줄인다. 빈 컨테이너에서는 `false`를 돌려주고 아무 소멸자도 호출하지 않는다.
7. `clear()`는 **뒤에서 앞으로**, 즉 생성의 역순으로 파괴한다. 원소 5개를 0,1,2,3,4 순서로 넣었으면 파괴 순서는 4,3,2,1,0이다.
8. 소멸자는 남아 있는 원소를 전부 파괴한다. 반납을 잊은 코드가 있어도 누수가 0이어야 한다.
9. 복사 생성과 복사 대입은 `= delete`한다. 이동 생성과 이동 대입은 구현한다. 원소를 하나씩 이동 생성하고 원본을 비운다(`O(N)`이며 공짜가 아니다).
10. 이동 대입은 **대상의 기존 원소를 먼저 전부 파괴**한 뒤 옮겨 온다. 자기 자신에게 대입하면 아무 원소도 파괴하지 않는다.
11. `operator[]`, `back()`, `begin()`, `end()`, `data()`를 제공한다. 원소는 연속으로 놓여 있어야 하므로 `&v[3] == &v[0] + 3`이 성립한다.
12. `T`가 `std::uint32_t`처럼 trivial한 타입이어도 동작해야 한다.

## 인터페이스

```cpp
template <typename T, std::size_t N>
class static_vector {
public:
    using value_type = T;
    using size_type = std::size_t;

    static_vector() noexcept = default;
    ~static_vector();
    static_vector(const static_vector &) = delete;
    static_vector &operator=(const static_vector &) = delete;
    static_vector(static_vector &&other) noexcept;
    static_vector &operator=(static_vector &&other) noexcept;

    static constexpr size_type capacity() noexcept;
    size_type size() const noexcept;
    bool empty() const noexcept;
    bool full() const noexcept;

    template <typename... Args> T *emplace_back(Args &&...args);
    bool push_back(const T &v);
    bool push_back(T &&v);
    bool pop_back() noexcept;
    void clear() noexcept;

    T &operator[](size_type i) noexcept;
    T &back() noexcept;
    T *begin() noexcept;
    T *end() noexcept;
    T *data() noexcept;
};
```

## 제약

- C++17. `new`(placement new 제외), `delete`, `malloc`, `std::vector`, `std::string` 전부 금지.
- 허용 헤더: `<cstdint> <cstddef> <new> <type_traits> <cassert> <cstdio> <utility>`.
- 예외가 없다. 실패는 반환값으로만 알린다.
- `size_` 갱신과 생성/파괴의 순서를 지킨다. 생성이 끝난 뒤에 `size_`를 올리고, `size_`를 내린 뒤에 소멸자를 호출한다.
- 저장소 정렬은 `alignas(T)`로 올린다. `alignof(T)`가 8인 타입에서 정렬이 어긋나면 Cortex-M0+에서 HardFault다.
- `c++ -std=c++17 -Wall -Wextra -O2 -g -fno-exceptions -fno-rtti`에서 경고 0개.

## 예시 동작

용량 3, 원소는 생성/파괴를 세는 `Tracked`다.

```
static_vector<Tracked,3> v;     ctor=0 dtor=0   size=0   저장소만 있고 객체는 없음
v.emplace_back(1)               ctor=1 dtor=0   size=1   -> &buf_[0]
v.emplace_back(2)               ctor=2 dtor=0   size=2
v.emplace_back(3)               ctor=3 dtor=0   size=3   full
v.emplace_back(4)               ctor=3 dtor=0   size=3   -> nullptr (생성자 미호출)
v.pop_back()                    ctor=3 dtor=1   size=2   3번 원소만 파괴
v.clear()                       ctor=3 dtor=3   size=0   파괴 순서: 2, 1
```

이동:

```
a = [1, 2]                      move_ctor=0
static_vector<Tracked,3> b(std::move(a));
                                move_ctor=2   b=[1,2], a.size()==0
                                a의 moved-from 원소 2개는 파괴됨 (dtor=2)
```

## 스스로 점검할 질문

1. `emplace_back`에서 생성이 끝나기 전에 `size_`를 먼저 올리면 어떤 버그가 생기는가? 예외가 없으면 괜찮은가?
2. `clear()`를 앞에서 뒤로 파괴하면 어떤 타입에서 실제로 문제가 되는가?
3. `alignas(T)`를 빼면 x86 host 테스트에서 재현되는가? 어느 타깃에서 언제 터지는가?
4. `std::launder`는 왜 필요한가? 없으면 컴파일러가 무엇을 가정할 수 있는가?
5. `static_vector<T,N>`의 이동이 `std::vector`의 이동보다 비싼 이유는 무엇인가?
6. `N`만 다른 인스턴스를 네 개 만들면 코드 크기는 어떻게 되는가? 줄이려면 어떻게 나누는가?
7. `T`가 trivially destructible이면 `clear()`를 어떻게 최적화할 수 있는가? 그 최적화를 컴파일러가 알아서 하는가?
8. 이 컨테이너를 ISR과 task가 함께 쓰려면 무엇이 더 필요한가?

## follow-up (면접관이 이어서 물을 것)

1. "Add `erase(iterator)`. What does it do to the order of the remaining elements?"
2. "Make `size()` fit in one byte for N up to 255. Does that help anything?"
3. "How would you give this a `constexpr` constructor so it can live in flash?"
4. "What does the linker map look like for `static_vector<Frame, 64>` where `Frame` is 512 bytes?"
5. "Why not just use a plain array plus a count? Convince me the class pays for itself."

---

**해설**: [notes/L3_embedded_cpp.md](../drills/L3_embedded_cpp.html) §4, §5 · **답안**: `solutions/19_static_vector.cpp`
