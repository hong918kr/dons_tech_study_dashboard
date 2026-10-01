# 21. 객체 풀 (`acquire` / `release` + placement new)

> **주제**: 타입이 있는 고정 풀 · 생성자·소멸자 보장 · 04와의 차이 · **난이도**: 심화 · **목표 시간**: 30분
> **해설은 보지 말 것**: 먼저 `starters/21_object_pool.cpp`를 채워 `make run N=21`으로 통과시킨다.

## 면접관의 문장

"You already wrote me a fixed-block memory pool in C. Now the things we hand out aren't byte buffers — they're objects. An audio session that grabs a mixer channel in its constructor and gives it back in its destructor. Four of them, maximum, and no heap.

Write `object_pool<T, N>`. `acquire` takes constructor arguments and hands back a fully constructed `T*`, or `nullptr` when the pool is empty. `release` runs the destructor and takes the slot back.

Then answer the question I care about: what happens when somebody forgets to release? With `malloc` you leak memory. Here the memory is static, so what exactly leaks? And tell me how this differs from the C pool you wrote earlier — I want to hear the words constructor and destructor."

## 요구사항

1. 슬롯은 `union { Slot *next; alignas(T) unsigned char storage[sizeof(T)]; }`다. 자유 상태에서는 링크, 사용 중에는 `T`의 저장소다. 사용 중인 슬롯의 메타데이터 오버헤드는 0바이트다.
2. 생성자는 슬롯 `N`개를 `slots_[0] -> slots_[1] -> ... -> nullptr` 순서로 엮고 `free_head_`를 첫 슬롯에 맞춘다. 이 시점에 `T`의 생성자는 호출되지 않는다.
3. `acquire(args...)`는 자유 슬롯 하나를 떼어 내고 그 저장소 위에 placement new로 `T`를 생성해서 돌려준다. 인자는 완벽 전달한다. 비었으면 `nullptr`이고 **생성자는 호출되지 않는다**. 반복문 없이 O(1)이어야 한다.
4. `acquire`에서 순서를 지킨다. `storage`를 덮어쓰기 전에 `next` 링크를 먼저 읽어 둬야 한다. union이므로 둘은 같은 메모리다.
5. `release(p)`의 반환값은 정확히 셋이다. `Release::Ok`(정상 반납 또는 `p == nullptr`인 no-op), `Release::Foreign`(이 풀의 슬롯 시작 주소가 아님), `Release::DoubleFree`(이미 자유 상태인 슬롯).
6. `Release::Ok`일 때만 소멸자를 호출한다. 거부된 호출은 `used`, `high_water`, free list, `live` 표 중 **무엇도** 바꾸지 않는다. 특히 double release가 free list에 같은 슬롯을 두 번 넣으면 안 된다.
7. `Foreign`으로 거부해야 하는 것: 풀 범위 밖 주소(스택 객체 등), 슬롯 중간을 가리키는 주소, one-past-end 주소.
8. 풀의 소멸자는 **아직 살아 있는 객체 전부의 소멸자를 호출**한다. 반납을 잊은 코드가 있어도 `T`의 소멸자가 획득한 자원은 해제된다. 04의 raw 블록 풀에는 없는 책임이다.
9. `destroy_all()`은 인덱스 역순으로 파괴하고 슬롯을 free list에 되돌린다. 호출 뒤 `used() == 0`이다.
10. `capacity()`, `used()`, `available()`, `high_water()`를 제공한다. `high_water`는 release를 해도 내려가지 않는다.
11. `owns(p)`는 부수효과 없이 소유 여부만 판정한다. `live_at(i)`는 살아 있는 객체 포인터 또는 `nullptr`을 돌려준다(진단용).
12. 복사 생성·복사 대입·이동 생성·이동 대입을 모두 `= delete`한다. 풀이 이동하면 이미 배급된 포인터가 엉뚱한 주소를 가리킨다.
13. release는 LIFO다. 방금 반납한 슬롯이 다음 `acquire`에서 그대로 나온다.
14. 전부 acquire하고 전부 release하는 사이클을 100번 돌려도 용량이 줄지 않고(단편화 0), 생성 횟수와 소멸 횟수가 정확히 같아야 한다.

## 인터페이스

```cpp
enum class Release : int { Ok = 0, Foreign = -1, DoubleFree = -2 };

template <typename T, std::size_t N>
class object_pool {
public:
    using value_type = T;
    object_pool() noexcept;
    ~object_pool();
    object_pool(const object_pool &) = delete;
    object_pool &operator=(const object_pool &) = delete;
    object_pool(object_pool &&) = delete;
    object_pool &operator=(object_pool &&) = delete;

    template <typename... Args> T *acquire(Args &&...args);
    Release release(T *p);
    void destroy_all() noexcept;

    static constexpr std::size_t capacity() noexcept;
    std::size_t used() const noexcept;
    std::size_t available() const noexcept;
    std::size_t high_water() const noexcept;
    bool owns(const T *p) const noexcept;
    T *live_at(std::size_t i) noexcept;
};
```

## 제약

- C++17. `new`(placement new 제외), `delete`, `malloc`, `free`, STL 컨테이너 전부 금지.
- 허용 헤더: `<cstdint> <cstddef> <new> <type_traits> <cassert> <cstdio> <utility>`.
- `acquire`와 `release`는 반복문 없이 상수 시간. 생성자와 `destroy_all`만 `N`에 비례하는 반복문을 갖는다.
- 예외가 없다. 고갈은 `nullptr`, 오류는 열거형 반환값으로만 알린다.
- 반환 주소는 항상 `alignof(T)`의 배수여야 한다.
- `c++ -std=c++17 -Wall -Wextra -O2 -g -fno-exceptions -fno-rtti`에서 경고 0개.

## 예시 동작

용량 4, `Session`은 생성자에서 채널을 열고 소멸자에서 닫는다.

```
object_pool<Session,4> pool;        ctor=0 dtor=0 open=0  used=0 hw=0
a = pool.acquire(11, 44100)         ctor=1        open=1  used=1 hw=1  -> &slot[0]
b = pool.acquire(12, 48000)         ctor=2        open=2  used=2 hw=2  -> &slot[1]
c = pool.acquire(13)                ctor=3        open=3  used=3 hw=3
d = pool.acquire(14)                ctor=4        open=4  used=4 hw=4
e = pool.acquire(15)                ctor=4        open=4  used=4 hw=4  -> nullptr (생성자 미호출)
pool.release(c)          -> Ok      dtor=1        open=3  used=3 hw=4
f = pool.acquire(99)                ctor=5        open=4  used=4 hw=4  -> &slot[2] (LIFO)
pool.release(&stack_obj) -> Foreign             상태 불변, 소멸자 미호출
pool.release((T*)((char*)a + 1)) -> Foreign      슬롯 중간 주소
pool.release(a)          -> Ok
pool.release(a)          -> DoubleFree          상태 불변
pool.release(nullptr)    -> Ok                  no-op
pool.release(e2)         -> Foreign             one-past-end 주소
```

주의: `c`를 반납한 뒤 그 슬롯이 `f`에게 다시 나갔다. 이때 낡은 포인터 `c`로 `release`를
부르면 주소가 유효하고 live 상태이므로 `Ok`가 되고, 실제로는 `f`가 파괴된다. 풀이 잡아
줄 수 없는 종류의 버그다 — 반납한 포인터는 즉시 버려야 한다.

스코프를 벗어날 때:

```
{
    object_pool<Session,4> pool;
    pool.acquire(10); pool.acquire(20); pool.acquire(30);   // 반납을 잊었다
}   // 풀 소멸자가 30, 20, 10 순서로 소멸자를 호출한다. open == 0
```

## 스스로 점검할 질문

1. 04의 `pool_alloc`이 돌려준 블록과 이 `acquire`가 돌려준 포인터는 무엇이 다른가? 호출자가 해야 하는 일이 어떻게 줄었는가?
2. `release`를 잊으면 정확히 무엇이 누수되는가? 메모리가 아니라면 무엇인가?
3. `acquire`에서 `next`를 읽기 전에 placement new를 하면 어떤 일이 일어나는가? 그 버그의 증상은?
4. `live_` 표를 비트맵으로 바꾸면 무엇을 얻고 무엇을 잃는가?
5. 풀 소멸자가 남은 객체를 파괴하는 것이 항상 옳은가? 그 객체를 가리키는 포인터를 누가 아직 들고 있다면?
6. `destroy_all()`을 인덱스 역순으로 도는 것과 생성 역순으로 도는 것은 같은가? 다르다면 왜 인덱스 역순으로 했는가?
7. 같은 저장소에 `T`를 파괴하고 다시 생성했을 때, 이전 포인터를 계속 써도 되는가? `std::launder`는 여기서 무엇을 해 주는가?
8. ISR에서 `acquire`를 호출하려면 무엇을 추가해야 하는가? `T`의 생성자가 오래 걸리면?

## follow-up (면접관이 이어서 물을 것)

1. "Make `acquire` return something that releases itself when it goes out of scope."
2. "Two different types, one pool. Possible? Desirable?"
3. "How do you find the code path that forgot to release, in the field?"
4. "What's the alignment story if `T` contains a `double` and you're on a Cortex-M0+?"
5. "Compare this with an RTOS memory pool API. What does the RTOS give you that this doesn't?"

---

**해설**: [notes/L3_embedded_cpp.md](../drills/L3_embedded_cpp.html) §5, §7 · **답안**: `solutions/21_object_pool.cpp`
