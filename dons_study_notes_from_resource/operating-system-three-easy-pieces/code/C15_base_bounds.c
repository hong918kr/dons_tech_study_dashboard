// C15_base_bounds.c — 장난감 MMU: base & bounds 동적 재배치를 소프트웨어로 흉내 낸다
// build: cc -Wall -Wextra -O0 code/C15_base_bounds.c -o .work/bin/C15_base_bounds
//  - 물리 메모리 64KB 를 바이트 배열로 두고, 모든 load/store 를 translate() 를 거치게 한다
//  - bounds 는 "주소 공간 크기" 방식 (VA < bounds 검사 후 base 를 더함)
//  - base/bounds 변경은 커널 모드에서만 허용 (특권 명령 흉내)
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define KB 1024u
#define PHYS_SIZE (64 * KB)

static uint8_t phys[PHYS_SIZE];          // "DRAM"

typedef enum { KERNEL, USER } mode_t_;
struct cpu { uint32_t base, bounds; mode_t_ mode; } cpu;   // MMU 레지스터 1쌍 (CPU당)
struct proc { const char *name; uint32_t base, bounds; int alive; };   // PCB 에 저장되는 값

static int faults;

// 하드웨어가 매 접근마다 하는 일 (Figure 15.5 의 "Translate virtual address")
static int translate(uint32_t va, uint32_t *pa) {
    if (va >= cpu.bounds) {                 // 음수는 uint32 라서 아주 큰 값이 되어 같이 걸린다
        faults++;
        return -1;                          // → out-of-bounds 예외, OS 핸들러로
    }
    *pa = cpu.base + va;
    return 0;
}

static int set_base_bounds(uint32_t base, uint32_t bounds) {
    if (cpu.mode != KERNEL) {
        printf("  !! privileged-op exception: user mode 에서 base 변경 시도\n");
        return -1;
    }
    cpu.base = base; cpu.bounds = bounds;
    return 0;
}

static void show(uint32_t va) {
    uint32_t pa;
    if (translate(va, &pa) == 0)
        printf("  VA %5u (%5.2f KB) -> PA %5u (%5.2f KB)\n", va, va / 1024.0, pa, pa / 1024.0);
    else
        printf("  VA %5u (%5.2f KB) -> FAULT (out of bounds, bounds=%u)\n", va, va / 1024.0, cpu.bounds);
}

static int load32(uint32_t va, uint32_t *val) {
    uint32_t pa;
    if (translate(va, &pa)) return -1;
    memcpy(val, &phys[pa], 4);
    return 0;
}
static int store32(uint32_t va, uint32_t val) {
    uint32_t pa;
    if (translate(va, &pa)) return -1;
    memcpy(&phys[pa], &val, 4);
    return 0;
}

static void context_switch(struct proc *from, struct proc *to) {
    cpu.mode = KERNEL;                       // 타이머 인터럽트 → 커널 모드
    if (from) { from->base = cpu.base; from->bounds = cpu.bounds; }   // save to PCB
    set_base_bounds(to->base, to->bounds);  // restore from PCB
    cpu.mode = USER;                         // return-from-trap
    printf("  [switch] -> %s (base=%u, bounds=%u)\n", to->name, cpu.base, cpu.bounds);
}

int main(void) {
    printf("[1] 원문 예제: 4KB 주소 공간을 물리 16KB 에 올림\n");
    cpu.mode = KERNEL; set_base_bounds(16 * KB, 4 * KB); cpu.mode = USER;
    uint32_t vas[] = {0, 1 * KB, 3000, 4400};
    for (unsigned i = 0; i < 4; i++) show(vas[i]);

    printf("\n[2] Figure 15.1/15.2: 16KB 프로세스를 물리 32KB 에 재배치, x=3000 at VA 15KB\n");
    struct proc A = {"A", 32 * KB, 16 * KB, 1}, B = {"B", 16 * KB, 16 * KB, 1};
    context_switch(NULL, &A);
    show(128);                               // 명령어 fetch: 128 + 32768
    store32(15 * KB, 3000);                  // 스택의 x 초기값
    uint32_t x; load32(15 * KB, &x); x += 3; store32(15 * KB, x);   // x = x + 3
    uint32_t pa; translate(15 * KB, &pa);
    printf("  x = x + 3 실행 후: VA 15KB 의 값 = %u, 실제 저장 위치 PA %u (= %u KB)\n", x, pa, pa / KB);

    printf("\n[3] 컨텍스트 스위치 + 같은 VA 다른 PA\n");
    context_switch(&A, &B);
    store32(15 * KB, 7777);
    translate(15 * KB, &pa);
    printf("  B 가 VA 15KB 에 7777 저장 -> PA %u\n", pa);
    context_switch(&B, &A);
    load32(15 * KB, &x);
    printf("  A 로 돌아와 VA 15KB 읽기 -> %u (B 의 쓰기에 영향 없음)\n", x);

    printf("\n[4] 유저 모드에서 base 를 바꾸려 하면?\n");
    set_base_bounds(0, 64 * KB);

    printf("\n[5] B 의 bad load (VA 20KB)\n");
    context_switch(&A, &B);
    if (load32(20 * KB, &x) < 0) {
        cpu.mode = KERNEL;
        printf("  OS: out-of-bounds trap -> B 종료, B 의 16KB 를 free list 로 반환\n");
        B.alive = 0;
    }

    printf("\n[6] A 를 물리 48KB 로 옮기기 (deschedule -> memcpy -> PCB base 수정)\n");
    memcpy(&phys[48 * KB], &phys[A.base], A.bounds);
    A.base = 48 * KB;
    context_switch(NULL, &A);
    load32(15 * KB, &x);
    translate(15 * KB, &pa);
    printf("  A 는 모른 채 VA 15KB 를 읽음 -> %u (이제 PA %u)\n", x, pa);

    printf("\n총 fault 횟수: %d\n", faults);
    return 0;
}
