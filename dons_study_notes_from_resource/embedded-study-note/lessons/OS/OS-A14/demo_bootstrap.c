/* OS-A14 demo_bootstrap — launchd 가 "서비스 이름 → 포트" 를 잇는 모습을 직접 본다 (macOS 전용, 관찰용)
 *
 *   1) bootstrap_look_up("com.apple.system.notification_center") — notifyd 의 launchd.plist MachServices 항목
 *   2) 없는 이름 → BOOTSTRAP_UNKNOWN_SERVICE(1102)
 *   3) bootstrap_check_in(임의 이름) — man 페이지는 "launchd.plist 에 없으면 UNKNOWN_SERVICE" 라고 적는다. 실제 결과를 출력.
 *   4) bootstrap_register(폐기 API, 10.5 deprecated) 로 이름을 걸고 fork 한 자식이 look_up → send → 부모가 받는다.
 *      이름은 com.example.os-a14.<pid> 이고, receive right 를 부수면/프로세스가 끝나면 사라지는지 확인한다.
 *
 * 체크 파일이 아니다: 3)·4) 는 이 맥(macOS 27.2)의 관찰이며 다른 버전에서 다를 수 있다.
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_bootstrap.c -o demo_bootstrap && ./demo_bootstrap
 */
#define _DARWIN_C_SOURCE
#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"   /* bootstrap_register 는 일부러 쓴다 */

typedef struct { mach_msg_header_t h; int32_t pid; } hello_t;
typedef union { mach_msg_header_t h; hello_t m; uint8_t raw[sizeof(hello_t) + sizeof(mach_msg_max_trailer_t)]; } hello_buf_t;

static int child_lookup_and_send(const char *name) {   /* 반환: 자식 종료 코드 (0 = look_up + send 성공) */
    fflush(stdout);
    pid_t c = fork();
    if (c == 0) {
        alarm(5);
        mach_port_t sp = MACH_PORT_NULL;
        kern_return_t kr = bootstrap_look_up(bootstrap_port, name, &sp);
        printf("    [child %d] bootstrap_look_up -> %d (%s), send right name 0x%x\n", (int)getpid(), kr, bootstrap_strerror(kr), sp);
        fflush(stdout);
        if (kr != KERN_SUCCESS) _exit(1);
        hello_t m; memset(&m, 0, sizeof m);
        m.h.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
        m.h.msgh_size = sizeof m; m.h.msgh_remote_port = sp; m.pid = getpid();
        _exit(mach_msg(&m.h, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof m, 0, MACH_PORT_NULL, 500, MACH_PORT_NULL) == KERN_SUCCESS ? 0 : 2);
    }
    int st = 0;
    waitpid(c, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

int main(void) {
    mach_port_t me = mach_task_self(), sp = MACH_PORT_NULL;
    printf("pid %d, bootstrap_port = 0x%x (this task's send right to its launchd bootstrap namespace)\n", (int)getpid(), bootstrap_port);

    kern_return_t kr = bootstrap_look_up(bootstrap_port, "com.apple.system.notification_center", &sp);
    mach_port_type_t t = 0; mach_port_type(me, sp, &t);
    printf("1) look_up com.apple.system.notification_center -> %d, name 0x%x, type 0x%x (SEND=0x%x)\n", kr, sp, t, MACH_PORT_TYPE_SEND);
    if (kr == KERN_SUCCESS) mach_port_deallocate(me, sp);

    kr = bootstrap_look_up(bootstrap_port, "com.example.os-a14.does-not-exist", &sp);
    printf("2) look_up unknown name -> %d (%s)\n", kr, bootstrap_strerror(kr));

    char name[128];
    snprintf(name, sizeof name, "com.example.os-a14.checkin.%d", (int)getpid());
    sp = MACH_PORT_NULL;
    kr = bootstrap_check_in(bootstrap_port, name, &sp);
    t = 0; if (kr == KERN_SUCCESS) mach_port_type(me, sp, &t);
    printf("3) check_in %s -> %d (%s), name 0x%x type 0x%x (RECEIVE=0x%x)\n", name, kr, bootstrap_strerror(kr), sp, t, MACH_PORT_TYPE_RECEIVE);
    if (kr == KERN_SUCCESS) mach_port_mod_refs(me, sp, MACH_PORT_RIGHT_RECEIVE, -1);

    snprintf(name, sizeof name, "com.example.os-a14.%d", (int)getpid());
    mach_port_t p = MACH_PORT_NULL;
    mach_port_allocate(me, MACH_PORT_RIGHT_RECEIVE, &p);
    mach_port_insert_right(me, p, p, MACH_MSG_TYPE_MAKE_SEND);
    kr = bootstrap_register(bootstrap_port, name, p);
    printf("4) bootstrap_register %s -> %d (%s)\n", name, kr, bootstrap_strerror(kr));
    int code = child_lookup_and_send(name);
    hello_buf_t b; memset(&b, 0, sizeof b);
    kr = mach_msg(&b.h, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0, sizeof b, p, 1000, MACH_PORT_NULL);
    printf("   child exit %d; parent received kr=0x%x hello from pid %d\n", code, kr, kr == KERN_SUCCESS ? b.m.pid : -1);
    mach_port_mod_refs(me, p, MACH_PORT_RIGHT_SEND, -1);
    mach_port_mod_refs(me, p, MACH_PORT_RIGHT_RECEIVE, -1);          /* 서비스 포트를 부순다 */
    usleep(50000);
    printf("   after destroying the receive right:\n");
    code = child_lookup_and_send(name);
    printf("   child exit %d\n", code);
    return 0;
}
