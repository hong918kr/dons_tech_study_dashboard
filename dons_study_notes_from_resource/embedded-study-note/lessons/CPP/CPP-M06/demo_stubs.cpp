// CPP-M06 측정용: virtual 소멸자 전역 객체를 맨몸 링크하려면 crt 가 주던 심볼을 직접 줘야 한다
//   -DSTUB_DELETE  operator delete 를 무한루프로 대체 (newlib free 를 끊는다)
//   -DSTUB_PURE    __cxa_pure_virtual 을 무한루프로 대체 (terminate 체인을 끊는다)
extern "C" { void* __dso_handle = nullptr; void _init() {} void _fini() {} }
#ifdef STUB_DELETE
void operator delete(void*) noexcept { for (;;) {} }
void operator delete(void*, unsigned int) noexcept { for (;;) {} }
#endif
#ifdef STUB_PURE
extern "C" void __cxa_pure_virtual() { for (;;) {} }
#endif
