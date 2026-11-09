// CPP-M10 demo_target — Cortex-M4 로 컴파일해 손으로 쓴 C 배열과 비교한다 (asm_target.sh 참고, 링크 없음)
// -DNDEBUG 로 빌드한다 (assert 는 demo_link.cpp 에서 따로 측정)
#include "demo_static_vector.hpp"

// ---- 비교 1: 손으로 쓴 C 스타일 ----
struct CBuf { int a[8]; unsigned n; };
extern "C" int  cbuf_push(CBuf* b, int x)       { if (b->n >= 8) return 0; b->a[b->n++] = x; return 1; }
extern "C" void cbuf_erase(CBuf* b, unsigned i) { for (unsigned k = i; k + 1 < b->n; ++k) b->a[k] = b->a[k + 1]; --b->n; }
extern "C" int  cbuf_sum(const CBuf* b)         { int s = 0; for (unsigned i = 0; i < b->n; ++i) s += b->a[i]; return s; }

// ---- 비교 1: static_vector<int, 8> ----
using V = static_vector<int, 8>;
extern "C" bool sv_push(V& v, int x)        { return v.push_back(x); }
extern "C" void sv_erase(V& v, unsigned i)  { v.erase(v.cbegin() + i); }
extern "C" int  sv_sum(const V& v)          { int s = 0; for (int x : v) s += x; return s; }

// ---- 비교 2: 조건부 trivial 소멸자 ----
struct NT { int v; ~NT(); };
void sink(void*);
extern "C" void scope_int() { static_vector<int, 4> v; sink(&v); }
extern "C" void scope_nt()  { static_vector<NT, 4> v; sink(&v); }

// ---- 비교 3: 전역 객체의 초기화 ----
static_vector<int, 8> g_int;
static_vector<NT, 4>  g_nt;
extern "C" void* use_globals(int w) { return w ? static_cast<void*>(&g_int) : static_cast<void*>(&g_nt); }
