	.build_version macos, 26, 4	sdk_version 26, 4
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_sum_loop                       ; -- Begin function sum_loop
	.p2align	2
_sum_loop:                              ; @sum_loop
; %bb.0:
	sub	sp, sp, #32
	str	x0, [sp, #24]
	str	x1, [sp, #16]
	str	xzr, [sp, #8]
	str	xzr, [sp]
	b	LBB0_1
LBB0_1:                                 ; =>This Inner Loop Header: Depth=1
	ldr	x8, [sp]
	ldr	x9, [sp, #16]
	subs	x8, x8, x9
	b.ge	LBB0_4
	b	LBB0_2
LBB0_2:                                 ;   in Loop: Header=BB0_1 Depth=1
	ldr	x8, [sp, #24]
	ldr	x9, [sp]
	ldr	x9, [x8, x9, lsl #3]
	ldr	x8, [sp, #8]
	add	x8, x8, x9
	str	x8, [sp, #8]
	b	LBB0_3
LBB0_3:                                 ;   in Loop: Header=BB0_1 Depth=1
	ldr	x8, [sp]
	add	x8, x8, #1
	str	x8, [sp]
	b	LBB0_1
LBB0_4:
	ldr	x0, [sp, #8]
	add	sp, sp, #32
	ret
                                        ; -- End function
	.globl	_dispatch                       ; -- Begin function dispatch
	.p2align	2
_dispatch:                              ; @dispatch
; %bb.0:
	sub	sp, sp, #48
	stp	x29, x30, [sp, #32]             ; 16-byte Folded Spill
	add	x29, sp, #32
	stur	w0, [x29, #-8]
	stur	w1, [x29, #-12]
	ldur	w8, [x29, #-8]
	add	w8, w8, #0
	mov	w8, w8
                                        ; kill: def $x8 killed $w8
	str	x8, [sp, #8]                    ; 8-byte Folded Spill
	subs	x8, x8, #12
	b.hi	LBB1_12
; %bb.1:
	ldr	x11, [sp, #8]                   ; 8-byte Folded Reload
	adrp	x10, lJTI1_0@PAGE
	add	x10, x10, lJTI1_0@PAGEOFF
Ltmp1:
	adr	x8, Ltmp1
	ldrsw	x9, [x10, x11, lsl #2]
	add	x8, x8, x9
	br	x8
LBB1_2:
	ldur	w0, [x29, #-12]
	bl	_op_delete_sq
	stur	w0, [x29, #-4]
	b	LBB1_13
LBB1_3:
	ldur	w0, [x29, #-12]
	bl	_op_create_sq
	stur	w0, [x29, #-4]
	b	LBB1_13
LBB1_4:
	ldur	w0, [x29, #-12]
	bl	_op_get_log
	stur	w0, [x29, #-4]
	b	LBB1_13
LBB1_5:
	ldur	w0, [x29, #-12]
	bl	_op_delete_cq
	stur	w0, [x29, #-4]
	b	LBB1_13
LBB1_6:
	ldur	w0, [x29, #-12]
	bl	_op_create_cq
	stur	w0, [x29, #-4]
	b	LBB1_13
LBB1_7:
	ldur	w0, [x29, #-12]
	bl	_op_identify
	stur	w0, [x29, #-4]
	b	LBB1_13
LBB1_8:
	ldur	w0, [x29, #-12]
	bl	_op_abort
	stur	w0, [x29, #-4]
	b	LBB1_13
LBB1_9:
	ldur	w0, [x29, #-12]
	bl	_op_set_feat
	stur	w0, [x29, #-4]
	b	LBB1_13
LBB1_10:
	ldur	w0, [x29, #-12]
	bl	_op_get_feat
	stur	w0, [x29, #-4]
	b	LBB1_13
LBB1_11:
	ldur	w0, [x29, #-12]
	bl	_op_async_ev
	stur	w0, [x29, #-4]
	b	LBB1_13
LBB1_12:
	mov	w8, #-22                        ; =0xffffffea
	stur	w8, [x29, #-4]
	b	LBB1_13
LBB1_13:
	ldur	w0, [x29, #-4]
	ldp	x29, x30, [sp, #32]             ; 16-byte Folded Reload
	add	sp, sp, #48
	ret
	.p2align	2
lJTI1_0:
	.long	LBB1_2-Ltmp1
	.long	LBB1_3-Ltmp1
	.long	LBB1_4-Ltmp1
	.long	LBB1_12-Ltmp1
	.long	LBB1_5-Ltmp1
	.long	LBB1_6-Ltmp1
	.long	LBB1_7-Ltmp1
	.long	LBB1_12-Ltmp1
	.long	LBB1_8-Ltmp1
	.long	LBB1_9-Ltmp1
	.long	LBB1_10-Ltmp1
	.long	LBB1_12-Ltmp1
	.long	LBB1_11-Ltmp1
                                        ; -- End function
	.globl	_pkt_score                      ; -- Begin function pkt_score
	.p2align	2
_pkt_score:                             ; @pkt_score
; %bb.0:
	sub	sp, sp, #16
	str	x0, [sp, #8]
	ldr	x8, [sp, #8]
	ldr	w8, [x8, #4]
	ldr	x9, [sp, #8]
	ldrh	w9, [x9, #2]
	add	w8, w8, w9
	ldr	x9, [sp, #8]
	ldrb	w9, [x9, #19]
	add	w0, w8, w9
	add	sp, sp, #16
	ret
                                        ; -- End function
	.globl	_call_twice                     ; -- Begin function call_twice
	.p2align	2
_call_twice:                            ; @call_twice
; %bb.0:
	sub	sp, sp, #48
	stp	x29, x30, [sp, #32]             ; 16-byte Folded Spill
	add	x29, sp, #32
	stur	x0, [x29, #-8]
	ldur	x0, [x29, #-8]
	bl	_helper
	str	x0, [sp, #16]
	ldur	x8, [x29, #-8]
	add	x0, x8, #1
	bl	_helper
	str	x0, [sp, #8]
	ldr	x8, [sp, #16]
	ldr	x9, [sp, #8]
	mul	x8, x8, x9
	ldur	x9, [x29, #-8]
	add	x0, x8, x9
	ldp	x29, x30, [sp, #32]             ; 16-byte Folded Reload
	add	sp, sp, #48
	ret
                                        ; -- End function
	.globl	_get_checked                    ; -- Begin function get_checked
	.p2align	2
_get_checked:                           ; @get_checked
; %bb.0:
	sub	sp, sp, #48
	stp	x29, x30, [sp, #32]             ; 16-byte Folded Spill
	add	x29, sp, #32
	str	x0, [sp, #16]
	str	x1, [sp, #8]
	str	x2, [sp]
	ldr	x8, [sp]
	ldr	x9, [sp, #8]
	subs	x8, x8, x9
	b.lo	LBB4_2
	b	LBB4_1
LBB4_1:
	ldr	x0, [sp]
	bl	_report_oob
	stur	w0, [x29, #-4]
	b	LBB4_3
LBB4_2:
	ldr	x8, [sp, #16]
	ldr	x9, [sp]
	ldr	w8, [x8, x9, lsl #2]
	stur	w8, [x29, #-4]
	b	LBB4_3
LBB4_3:
	ldur	w0, [x29, #-4]
	ldp	x29, x30, [sp, #32]             ; 16-byte Folded Reload
	add	sp, sp, #48
	ret
                                        ; -- End function
	.globl	_const_math                     ; -- Begin function const_math
	.p2align	2
_const_math:                            ; @const_math
; %bb.0:
	sub	sp, sp, #16
	str	w0, [sp, #12]
	str	w1, [sp, #8]
	ldr	w8, [sp, #12]
	mov	w9, #9                          ; =0x9
	mul	w8, w8, w9
	ldr	w9, [sp, #12]
	mov	w10, #10                        ; =0xa
	udiv	w9, w9, w10
	add	w8, w8, w9
	ldr	w9, [sp, #8]
	mov	w10, #4                         ; =0x4
	sdiv	w9, w9, w10
	add	w0, w8, w9
	add	sp, sp, #16
	ret
                                        ; -- End function
.subsections_via_symbols
