/* ENV: qemu */
/* ARM-A02 practice.S — 추가 연습 답. 본문(solution.S)을 그대로 포함하고 EL1 물리 타이머(CNTP) 도우미만 더한다. */
#include "solution.S"

// 연습 3: EL1 물리 타이머 — EL2 가 없으면 EL1 에서 그대로 쓸 수 있다 (CNTHCTL_EL2 트랩 없음)
    FN write_cntp_tval
    msr     cntp_tval_el0, x0
    isb
    ret
    FN write_cntp_cval
    msr     cntp_cval_el0, x0
    isb
    ret
    FN read_cntp_cval
    mrs     x0, cntp_cval_el0
    ret
    FN write_cntp_ctl
    msr     cntp_ctl_el0, x0        // bit0 ENABLE, bit1 IMASK, bit2 ISTATUS(RO) — CNTV 와 같은 배치
    isb
    ret
    FN read_cntp_ctl
    mrs     x0, cntp_ctl_el0
    ret
