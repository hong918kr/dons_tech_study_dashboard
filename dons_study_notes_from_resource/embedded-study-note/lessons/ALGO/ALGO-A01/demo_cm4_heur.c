/* ALGO-A01 demo_cm4_heur — 옥타일 / 맨해튼 휴리스틱의 Cortex-M4 코드 (자유 환경 파일: <stdint.h> 만)
 * build: clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_cm4_heur.c -o -
 * 정수 10/14 라서 FPU·sqrt 가 필요 없다는 것을 실제 명령으로 확인한다. */
#include <stdint.h>

int32_t h_octile(int r, int c, int gr, int gc) {
    int dx = r > gr ? r - gr : gr - r, dy = c > gc ? c - gc : gc - c;
    return dx > dy ? 10 * dx + 4 * dy : 10 * dy + 4 * dx;
}

int32_t h_manhattan(int r, int c, int gr, int gc) {
    int dx = r > gr ? r - gr : gr - r, dy = c > gc ? c - gc : gc - c;
    return 10 * (dx + dy);
}
