@ ENV: qemu
@ ARM-M11 — Cortex-M3 (ARMv7-M) LDREX/STREX 원자 연산, 손으로 쓴 Thumb-2.
@ build: arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -c demo_m3_prims.S   (run_qemu.sh 참고)
@ gdb 가 "LDREX 와 STREX 사이" 에 멈출 수 있도록 inc_after_ldrex / inc_after_strex 를 전역 라벨로 둔다.

        .syntax unified
        @ .cpu 지시어는 일부러 두지 않는다: -mcpu 가 결정 (cortex-m0 으로 어셈블하면 에러 — run_qemu.sh m0)
        .thumb
        .text

@ uint32_t m_atomic_inc(volatile uint32_t *p, uint32_t *attempts)
@   *p += 1 을 원자적으로. 새 값을 반환, *attempts = LDREX/STREX 시도 횟수 (1 = 재시도 없음).
@   순서 보장은 없음 (relaxed 카운터) — 통계 카운터에는 이것으로 충분.
        .global m_atomic_inc
        .type   m_atomic_inc, %function
        .thumb_func
m_atomic_inc:
        movs    r3, #0
1:      adds    r3, r3, #1          @ attempts++
        ldrex   r2, [r0]            @ r2 = *p, 로컬 모니터 -> Exclusive(p)
        .global inc_after_ldrex
inc_after_ldrex:
        adds    r2, r2, #1
        strex   r12, r2, [r0]       @ 모니터가 아직 Exclusive(p) 면 쓰고 r12=0, 아니면 안 쓰고 r12=1
        .global inc_after_strex
inc_after_strex:
        cmp     r12, #0
        bne     1b                  @ 실패 -> 처음부터 다시 (값을 다시 읽는다)
        str     r3, [r1]
        mov     r0, r2
        bx      lr
        .size   m_atomic_inc, . - m_atomic_inc

@ uint32_t m_cas(volatile uint32_t *p, uint32_t expected, uint32_t desired)
@   strong CAS: *p == expected 면 desired 를 쓴다. 항상 "읽은 옛 값" 을 반환 (== expected 면 성공).
        .global m_cas
        .type   m_cas, %function
        .thumb_func
m_cas:
1:      ldrex   r3, [r0]
        cmp     r3, r1
        bne     2f                  @ 값이 다르면 쓰지 않는다
        strex   r12, r2, [r0]
        cmp     r12, #0
        bne     1b                  @ 값은 맞았는데 모니터를 잃음 -> 재시도 (strong)
        mov     r0, r3
        bx      lr
2:      clrex                       @ 실패 경로: 열어 둔 Exclusive 상태를 정리
        mov     r0, r3
        bx      lr
        .size   m_cas, . - m_cas

@ int m_mutex_try_lock(volatile uint32_t *lock)   1 = 잡음, 0 = 이미 잠김 (기다리지 않는다)
        .global m_mutex_try_lock
        .type   m_mutex_try_lock, %function
        .thumb_func
m_mutex_try_lock:
        movs    r2, #1
1:      ldrex   r1, [r0]
        cbnz    r1, 2f              @ 잠겨 있으면 포기
        strex   r3, r2, [r0]
        cmp     r3, #0
        bne     1b                  @ 풀려 있었는데 모니터를 잃음 -> 다시 확인
        dmb                         @ acquire: 임계 구역 접근이 잠금보다 앞서지 않게
        movs    r0, #1
        bx      lr
2:      clrex
        movs    r0, #0
        bx      lr
        .size   m_mutex_try_lock, . - m_mutex_try_lock

@ void m_mutex_unlock(volatile uint32_t *lock)
        .global m_mutex_unlock
        .type   m_mutex_unlock, %function
        .thumb_func
m_mutex_unlock:
        dmb                         @ release: 임계 구역 쓰기가 해제보다 먼저 보이게
        movs    r1, #0
        str     r1, [r0]
        bx      lr
        .size   m_mutex_unlock, . - m_mutex_unlock

@ uint32_t m_plain_inc(volatile uint32_t *p)  — 비교용 "틀린" 버전: ldr / adds / str
        .global m_plain_inc
        .type   m_plain_inc, %function
        .thumb_func
m_plain_inc:
        ldr     r1, [r0]
        adds    r1, r1, #1
        str     r1, [r0]
        mov     r0, r1
        bx      lr
        .size   m_plain_inc, . - m_plain_inc
