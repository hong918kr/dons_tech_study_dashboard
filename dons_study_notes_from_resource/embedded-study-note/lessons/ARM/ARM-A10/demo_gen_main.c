/* ARM-A10 demo_gen_main.c — demo_gen.c 의 함수들을 링크해 실행 파일로 만들기 위한 짝 (sink + main).
 * sink 를 다른 번역 단위에 두어 인라인되지 않게 한다 → 꼬리 호출 b _sink 의 목적지가 함수 밖. */
int leaf_add(int, int); int nonleaf(int); int early_exit(int); int tail_call(int); int call_ptr(int (*)(int), int);
int sink(int x) { return x * 3; }
int main(void) {
    int r = leaf_add(1, 2) + nonleaf(1) + early_exit(-5) + early_exit(2) + tail_call(1) + call_ptr(sink, 1);
    return r == 3 + 4 + -1 + 12 + 12 + 9 ? 0 : 1;
}
