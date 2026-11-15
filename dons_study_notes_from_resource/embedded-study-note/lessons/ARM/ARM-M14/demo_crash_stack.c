/* ARM-M14 demo 2 — 스택 오버런이 저장된 FP/LR 을 덮는다 → ret 이 쓰레기 주소로 점프
 *   cc -std=c11 -O1 -g -fno-stack-protector demo_crash_stack.c -o /tmp/ARM-M14_stack
 *   lldb -b -s demo_crash_stack.lldb /tmp/ARM-M14_stack > crash_stack.txt
 * UART 로 받은 "이름" 을 16바이트 지역 버퍼에 길이 검사 없이 복사한다 (memcpy 대신 손 루프:
 * _FORTIFY_SOURCE 의 __memcpy_chk 가 먼저 abort 하지 않도록).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 입력: 16 B 이름 + 8 B 'BBBBBBBB' + 8 B "CCCCCCCC" ... 패턴이 저장된 x29/x30 을 덮는다 */
static const char g_rx[] = "TMP117-SENSOR-01"      /* 16 B — 버퍼 크기 딱 맞음 */
                           "AAAAAAAABBBBBBBB"      /* 그 위 16 B */
                           "CCCCCCCCDDDDDDDD";     /* 그 위 16 B */
/* 추가 연습 2 용: 8바이트마다 다른 순환 패턴 (Metasploit pattern_create 형식, 48 B) */
static const char g_pat[] = "Aa0Aa1Aa2Aa3Aa4Aa5Aa6Aa7Aa8Aa9Ab0Ab1Ab2Ab3Ab4Ab5";

__attribute__((noinline))
void copy_name(char *dst, const char *src, size_t n) {
    for (size_t i = 0; i < n; i++)                 /* 길이 검사 없음 */
        ((volatile char *)dst)[i] = src[i];
}

__attribute__((noinline))
int set_label(const char *rx, size_t n) {
    char label[16];
    copy_name(label, rx, n);                       /* n > 16 이면 label 위를 덮는다 */
    printf("label[0]=%c\n", label[0]);
    return label[0];
}                                                  /* ← ret 이 덮인 LR 로 간다 */

int main(int argc, char **argv) {
    const char *rx = (argc > 1 && strcmp(argv[1], "pattern") == 0) ? g_pat : g_rx;
    size_t n = argc > 1 ? sizeof g_rx - 1 : 16;    /* 인자가 있으면 48 B 복사 = 오버런 */
    int r = set_label(rx, n);
    printf("ok %d\n", r);
    return 0;
}
