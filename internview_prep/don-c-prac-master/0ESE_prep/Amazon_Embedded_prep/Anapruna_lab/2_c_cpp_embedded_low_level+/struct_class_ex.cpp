#include <iostream>
#include <cassert>
#include <cstring>
using namespace std;

// struct와 class 차이, 생성자/소멸자, 멤버 초기화, RAII 예제

// 1. struct와 class 차이
struct MyStruct {
    int a;
    void set(int v) { a = v; }
};

class MyClass {
    int a;
public:
    MyClass(int v = 0) : a(v) {} // 생성자, 멤버 초기화
    void set(int v) { a = v; }
    int get() const { return a; }
    ~MyClass() {} // 소멸자
};

// 2. RAII 예제: 파일 자동 닫기
class FileRAII {
    FILE* fp;
public:
    FileRAII(const char* fname, const char* mode) {
        fp = fopen(fname, mode);
    }
    ~FileRAII() {
        if (fp) fclose(fp);
    }
    bool valid() const { return fp != nullptr; }
    void write(const char* msg) {
        if (fp) fputs(msg, fp);
    }
};

// 3. 멤버 초기화 예제
class Buffer {
    char* buf;
    size_t sz;
public:
    Buffer(size_t n) : sz(n) {
        buf = new char[sz];
        memset(buf, 0, sz);
    }
    ~Buffer() { delete[] buf; }
    char& operator[](size_t i) { return buf[i]; }
    size_t size() const { return sz; }
};

// 테스트 코드
int main() {
    // struct와 class 차이
    MyStruct s;
    s.a = 10;
    s.set(20);
    assert(s.a == 20);

    MyClass c(5);
    assert(c.get() == 5);
    c.set(42);
    assert(c.get() == 42);

    // RAII 예제
    {
        FileRAII f("test.txt", "w");
        assert(f.valid());
        f.write("hello\n");
    } // 여기서 자동으로 파일 닫힘

    // 멤버 초기화/소멸자
    Buffer b(10);
    assert(b.size() == 10);
    b[0] = 'A';
    assert(b[0] == 'A');

    cout << "All tests PASS" << endl;
    return 0;
}

/*
문제 예시:
- struct와 class의 차이를 코드로 설명하라.
- 생성자/소멸자, 멤버 초기화, RAII 패턴을 활용하는 클래스를 구현하라.
- main에서 위 기능들을 테스트하는 코드를 작성하라.
*/