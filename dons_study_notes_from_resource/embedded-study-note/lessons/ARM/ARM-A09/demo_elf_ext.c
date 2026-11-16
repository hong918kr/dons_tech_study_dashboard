/* ARM-A09 demo_elf_ext.c — demo_elf.S 가 부르는 "다른 파일" (ext_val / ext_func / ext_tail) */
long ext_val = 42;
long ext_func(long x) { return x + 1; }
long ext_tail(long x) { return x + 2; }
