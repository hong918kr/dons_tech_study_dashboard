/* OS-A14 demo_xpc_ports — XPC 객체가 만들 때마다 이 태스크의 Mach 포트 이름 공간이 어떻게 변하나 (관찰용)
 *
 * 익명 리스너 활성화 → 엔드포인트 → 연결 + 동기 호출 1 번, 단계마다 mach_port_names 로 권리 종류별 개수를 센다.
 * XPC 가 Mach 포트 위에 있다는 것을 "포트 수가 늘어난다" 로만 보여 준다 (내부 구조는 비공개).
 * build: cc -std=c11 -Wall -Wextra -Werror -O2 demo_xpc_ports.c -o demo_xpc_ports && ./demo_xpc_ports
 */
#define _DARWIN_C_SOURCE
#include <dispatch/dispatch.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <xpc/xpc.h>

static void census(const char *when) {
    mach_port_name_array_t n; mach_msg_type_number_t nc;
    mach_port_type_array_t t; mach_msg_type_number_t tc;
    if (mach_port_names(mach_task_self(), &n, &nc, &t, &tc) != KERN_SUCCESS) return;
    unsigned r = 0, s = 0, so = 0, d = 0, ps = 0;
    for (unsigned i = 0; i < nc; i++) {
        if (t[i] & MACH_PORT_TYPE_RECEIVE) r++;
        if (t[i] & MACH_PORT_TYPE_SEND) s++;
        if (t[i] & MACH_PORT_TYPE_SEND_ONCE) so++;
        if (t[i] & MACH_PORT_TYPE_DEAD_NAME) d++;
        if (t[i] & MACH_PORT_TYPE_PORT_SET) ps++;
    }
    printf("%-34s names=%2u  receive=%2u send=%2u send-once=%u dead=%u port-set=%u\n", when, nc, r, s, so, d, ps);
    vm_deallocate(mach_task_self(), (vm_address_t)n, nc * sizeof n[0]);
    vm_deallocate(mach_task_self(), (vm_address_t)t, tc * sizeof t[0]);
}

int main(void) {
    census("start");
    dispatch_queue_t q = dispatch_queue_create("os-a14.ports", DISPATCH_QUEUE_SERIAL);
    xpc_connection_t lis = xpc_connection_create(NULL, q);
    xpc_connection_set_event_handler(lis, ^(xpc_object_t peer) {
        if (xpc_get_type(peer) != XPC_TYPE_CONNECTION) return;
        xpc_connection_set_event_handler(peer, ^(xpc_object_t m) {
            xpc_object_t r = xpc_dictionary_create_reply(m);
            if (r) { xpc_connection_send_message(peer, r); xpc_release(r); }
        });
        xpc_connection_activate(peer);
    });
    census("listener created (inactive)");
    xpc_connection_activate(lis);
    census("listener activated");
    xpc_endpoint_t ep = xpc_endpoint_create(lis);
    census("endpoint created");
    xpc_connection_t c = xpc_connection_create_from_endpoint(ep);
    xpc_connection_set_event_handler(c, ^(xpc_object_t e) { (void)e; });
    xpc_connection_activate(c);
    census("client connection activated");
    xpc_object_t m = xpc_dictionary_create(NULL, NULL, 0);
    xpc_object_t r = xpc_connection_send_message_with_reply_sync(c, m);
    census("after 1 sync call");
    char *d = xpc_copy_description(c);
    printf("client: %s\n", d);
    free(d);
    xpc_release(r); xpc_release(m);
    xpc_connection_cancel(c); xpc_release(c);
    xpc_connection_cancel(lis); xpc_release(lis); xpc_release(ep);
    return 0;
}
