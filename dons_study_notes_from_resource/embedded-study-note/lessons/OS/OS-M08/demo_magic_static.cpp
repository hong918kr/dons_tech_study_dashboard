// OS-M08 demo_magic_static.cpp — C++11 "magic static": 함수 안 static 지역 변수 초기화는 스레드 안전 ([stmt.dcl]/4)
//   c++ -std=c++20 -O2 -S demo_magic_static.cpp -o -     (컴파일러가 만든 빠른 경로 + __cxa_guard_acquire 확인)
struct Table { unsigned tab[64]; Table(); };
Table::Table() { for (unsigned i = 0; i < 64; i++) tab[i] = i * 2654435761u; }
const Table &table() {
    static Table t;          // 첫 호출만 생성자 실행, 동시에 들어온 스레드는 끝날 때까지 기다린다
    return t;
}
