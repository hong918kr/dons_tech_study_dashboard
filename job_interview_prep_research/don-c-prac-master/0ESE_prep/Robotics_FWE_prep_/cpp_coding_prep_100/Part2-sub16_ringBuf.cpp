#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>

// 16. 링 버퍼(Ring Buffer) / 원형 큐(Circular Queue)
class RingBuffer {
public:
    static constexpr size_t SIZE = 8;
    RingBuffer() : head(0), tail(0), count(0) { std::memset(buf, 0, sizeof(buf)); }
    bool push(uint8_t val) {
        if (is_full()) return false;
        buf[head] = val;
        head = (head + 1) % SIZE;
        ++count;
        return true;
    }
    bool pop(uint8_t& val) {
        if (is_empty()) return false;
        val = buf[tail];
        tail = (tail + 1) % SIZE;
        --count;
        return true;
    }
    bool is_full() const { return count == SIZE; }
    bool is_empty() const { return count == 0; }
    size_t size() const { return count; }
    void print() const {
        std::cout << "[RingBuffer] ";
        size_t idx = tail;
        for (size_t i = 0; i < count; ++i) {
            std::cout << (int)buf[idx] << " ";
            idx = (idx + 1) % SIZE;
        }
        std::cout << "\n";
    }
private:
    uint8_t buf[SIZE];
    size_t head, tail, count;
};

// --- 테스트 코드 ---
int main() {
    RingBuffer rb;

    std::cout << "=== RingBuffer Test ===\n";

    // 1. 빈 버퍼에서 pop 시도
    uint8_t val = 0;
    std::cout << "Pop from empty: " << (rb.pop(val) ? "Success" : "Fail") << "\n";

    // 2. 버퍼에 데이터 채우기
    for (uint8_t i = 1; i <= 8; ++i) {
        bool ok = rb.push(i * 10);
        std::cout << "Push " << (int)(i * 10) << ": " << (ok ? "Success" : "Fail") << "\n";
    }
    rb.print();

    // 3. 가득 찬 버퍼에 push 시도
    bool ok = rb.push(99);
    std::cout << "Push to full: " << (ok ? "Success" : "Fail") << "\n";

    // 4. pop 3번
    for (int i = 0; i < 3; ++i) {
        if (rb.pop(val)) std::cout << "Pop: " << (int)val << "\n";
    }
    rb.print();

    // 5. 다시 push 2번
    rb.push(77);
    rb.push(88);
    rb.print();

    // 6. 모두 pop
    while (rb.pop(val)) {
        std::cout << "Pop: " << (int)val << "\n";
    }
    rb.print();

    std::cout << "=== End of Test ===\n";
    return 0;
}

/*
-------------------------------
[스터디/분석/설명]

1. **Ring Buffer란?**
   - 고정 크기의 배열을 사용하여 FIFO(선입선출) 큐를 구현하는 자료구조.
   - head(쓰기 위치), tail(읽기 위치), count(현재 데이터 개수)로 관리.
   - head/tail이 끝에 도달하면 0으로 돌아가 "원형"처럼 동작.

2. **장점**
   - 메모리 할당/해제 없이 빠르고 예측 가능한 동작.
   - 오버플로우/언더플로우를 쉽게 감지 가능.
   - 임베디드, 통신 버퍼, 실시간 시스템에서 널리 사용.

3. **주요 동작**
   - push: 데이터 추가, 가득 차면 실패.
   - pop: 데이터 제거, 비어 있으면 실패.
   - is_full/is_empty: 상태 확인.

4. **주의점**
   - head/tail/counter 관리 실수 시 데이터 손실/중복 가능.
   - 멀티스레드 환경에서는 동기화 필요.

5. **활용 예시**
   - UART 수신 버퍼, 오디오 샘플 버퍼, 실시간 데이터 스트림 등.

6. **C++로 구현할 때 팁**
   - std::array, std::mutex 등 C++ 표준 라이브러리 활용 가능.
   - 템플릿으로 타입/크기 일반화 가능.

7. **확장 아이디어**
   - 멀티스레드 안전 버전 (lock, atomic 등)
   - 오버라이트(Overwrite) 모드 지원
   - 임의 위치

   */