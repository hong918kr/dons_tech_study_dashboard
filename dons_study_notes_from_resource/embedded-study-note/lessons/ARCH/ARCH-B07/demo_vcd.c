/* ARCH-B07 demo — Moore/Mealy 1011 검출기 파형을 VCD(Value Change Dump, IEEE 1364) 파일로 쓴다.
 * GTKWave / Surfer 같은 파형 뷰어로 열면 RTL 시뮬레이션과 같은 모양으로 볼 수 있다.
 *
 * 단순화(zero-delay): 클럭 주기 10 ns, 상승 에지 t = 10k. 입력·상태·출력은 모두 에지 시각에 바뀐다고 본다
 * (실제 회로는 clk→q, 조합 지연만큼 늦게 바뀐다 — ARCH-B13).
 *
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_vcd.c -o /tmp/esn_ARCH-B07/vcd && /tmp/esn_ARCH-B07/vcd /tmp/esn_ARCH-B07/b07.vcd
 */
#include <stdint.h>
#include <stdio.h>

static const uint8_t MOORE_T[5][2] = { {0, 1}, {2, 1}, {0, 3}, {2, 4}, {2, 1} };
static const uint8_t MEALY_T[4][2] = { {0, 1}, {2, 1}, {0, 3}, {2, 1} };

static void bin3(FILE *f, unsigned v, char id) { fprintf(f, "b%u%u%u %c\n", (v >> 2) & 1u, (v >> 1) & 1u, v & 1u, id); }

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "b07.vcd";
    const uint8_t in[] = {1, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 0, 0};
    const int n = (int)(sizeof in / sizeof in[0]);
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return 1; }

    fprintf(f, "$timescale 1ns $end\n$scope module b07 $end\n");
    fprintf(f, "$var wire 1 ! clk $end\n$var wire 1 \" din $end\n");
    fprintf(f, "$var reg 3 # moore_state $end\n$var wire 1 $ moore_out $end\n");
    fprintf(f, "$var reg 3 %% mealy_state $end\n$var wire 1 & mealy_out $end\n$var reg 1 ' mealy_q $end\n");
    fprintf(f, "$upscope $end\n$enddefinitions $end\n");

    uint8_t ms = 0, ys = 0, q = 0;
    int changes = 0;
    for (int k = 0; k < n; k++) {
        uint8_t mo = ms == 4, yo = ys == 3 && in[k];
        fprintf(f, "#%d\n1!\n%u\"\n", 10 * k, in[k]);
        bin3(f, ms, '#'); fprintf(f, "%u$\n", mo);
        bin3(f, ys, '%'); fprintf(f, "%u&\n%u'\n", yo, q);
        fprintf(f, "#%d\n0!\n", 10 * k + 5);
        changes += 7;
        ms = MOORE_T[ms][in[k]]; ys = MEALY_T[ys][in[k]]; q = yo;   /* 에지에서 한꺼번에 커밋 */
    }
    fprintf(f, "#%d\n", 10 * n);
    fclose(f);
    printf("wrote %s: %d cycles, %d value records (뷰어: gtkwave %s)\n", path, n, changes, path);
    return 0;
}
