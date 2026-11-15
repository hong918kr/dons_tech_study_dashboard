	.build_version macos, 26, 4	sdk_version 26, 4
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_sum_loop                       ; -- Begin function sum_loop
	.p2align	2
_sum_loop:                              ; @sum_loop
; %bb.0:
	cmp	x1, #1
	b.lt	LBB0_3
; %bb.1:
	cmp	x1, #8
	b.hs	LBB0_4
; %bb.2:
	mov	x9, #0                          ; =0x0
	mov	x8, #0                          ; =0x0
	b	LBB0_7
LBB0_3:
	mov	x8, #0                          ; =0x0
	mov	x0, x8
	ret
LBB0_4:
	and	x9, x1, #0x7ffffffffffffff8
	add	x8, x0, #32
	movi.2d	v0, #0000000000000000
	mov	x10, x9
	movi.2d	v1, #0000000000000000
	movi.2d	v2, #0000000000000000
	movi.2d	v3, #0000000000000000
LBB0_5:                                 ; =>This Inner Loop Header: Depth=1
	ldp	q4, q5, [x8, #-32]
	ldp	q6, q7, [x8], #64
	add.2d	v0, v4, v0
	add.2d	v1, v5, v1
	add.2d	v2, v6, v2
	add.2d	v3, v7, v3
	subs	x10, x10, #8
	b.ne	LBB0_5
; %bb.6:
	add.2d	v0, v1, v0
	add.2d	v0, v2, v0
	add.2d	v0, v3, v0
	addp.2d	d0, v0
	fmov	x8, d0
	cmp	x1, x9
	b.eq	LBB0_9
LBB0_7:
	sub	x10, x1, x9
	add	x9, x0, x9, lsl #3
LBB0_8:                                 ; =>This Inner Loop Header: Depth=1
	ldr	x11, [x9], #8
	add	x8, x11, x8
	subs	x10, x10, #1
	b.ne	LBB0_8
LBB0_9:
	mov	x0, x8
	ret
                                        ; -- End function
	.globl	_dispatch                       ; -- Begin function dispatch
	.p2align	2
_dispatch:                              ; @dispatch
; %bb.0:
	cmp	w0, #12
	b.hi	LBB1_8
; %bb.1:
	mov	w8, w0
Lloh0:
	adrp	x9, lJTI1_0@PAGE
Lloh1:
	add	x9, x9, lJTI1_0@PAGEOFF
	adr	x10, LBB1_2
	ldrb	w11, [x9, x8]
	add	x10, x10, x11, lsl #2
	br	x10
LBB1_2:
	mov	x0, x1
	b	_op_delete_sq
LBB1_3:
	mov	x0, x1
	b	_op_get_log
LBB1_4:
	mov	x0, x1
	b	_op_identify
LBB1_5:
	mov	x0, x1
	b	_op_create_sq
LBB1_6:
	mov	x0, x1
	b	_op_abort
LBB1_7:
	mov	x0, x1
	b	_op_set_feat
LBB1_8:
	mov	w0, #-22                        ; =0xffffffea
	ret
LBB1_9:
	mov	x0, x1
	b	_op_delete_cq
LBB1_10:
	mov	x0, x1
	b	_op_create_cq
LBB1_11:
	mov	x0, x1
	b	_op_async_ev
LBB1_12:
	mov	x0, x1
	b	_op_get_feat
	.loh AdrpAdd	Lloh0, Lloh1
	.section	__TEXT,__const
lJTI1_0:
	.byte	(LBB1_2-LBB1_2)>>2
	.byte	(LBB1_5-LBB1_2)>>2
	.byte	(LBB1_3-LBB1_2)>>2
	.byte	(LBB1_8-LBB1_2)>>2
	.byte	(LBB1_9-LBB1_2)>>2
	.byte	(LBB1_10-LBB1_2)>>2
	.byte	(LBB1_4-LBB1_2)>>2
	.byte	(LBB1_8-LBB1_2)>>2
	.byte	(LBB1_6-LBB1_2)>>2
	.byte	(LBB1_7-LBB1_2)>>2
	.byte	(LBB1_12-LBB1_2)>>2
	.byte	(LBB1_8-LBB1_2)>>2
	.byte	(LBB1_11-LBB1_2)>>2
                                        ; -- End function
	.section	__TEXT,__text,regular,pure_instructions
	.globl	_pkt_score                      ; -- Begin function pkt_score
	.p2align	2
_pkt_score:                             ; @pkt_score
; %bb.0:
	ldr	w8, [x0, #4]
	ldrh	w9, [x0, #2]
	ldrb	w10, [x0, #19]
	add	w8, w8, w9
	add	w0, w8, w10
	ret
                                        ; -- End function
	.globl	_call_twice                     ; -- Begin function call_twice
	.p2align	2
_call_twice:                            ; @call_twice
; %bb.0:
	stp	x20, x19, [sp, #-32]!           ; 16-byte Folded Spill
	stp	x29, x30, [sp, #16]             ; 16-byte Folded Spill
	add	x29, sp, #16
	mov	x19, x0
	bl	_helper
	mov	x20, x0
	add	x0, x19, #1
	bl	_helper
	madd	x0, x0, x20, x19
	ldp	x29, x30, [sp, #16]             ; 16-byte Folded Reload
	ldp	x20, x19, [sp], #32             ; 16-byte Folded Reload
	ret
                                        ; -- End function
	.globl	_get_checked                    ; -- Begin function get_checked
	.p2align	2
_get_checked:                           ; @get_checked
; %bb.0:
	cmp	x2, x1
	b.hs	LBB4_2
; %bb.1:
	ldr	w0, [x0, x2, lsl #2]
	ret
LBB4_2:
	mov	x0, x2
	b	_report_oob
                                        ; -- End function
	.globl	_const_math                     ; -- Begin function const_math
	.p2align	2
_const_math:                            ; @const_math
; %bb.0:
	add	w8, w0, w0, lsl #3
	mov	w9, #52429                      ; =0xcccd
	movk	w9, #52428, lsl #16
	umull	x9, w0, w9
	lsr	x9, x9, #35
	add	w8, w8, w9
	add	w9, w1, #3
	cmp	w1, #0
	csel	w9, w9, w1, lt
	add	w0, w8, w9, asr #2
	ret
                                        ; -- End function
.subsections_via_symbols
