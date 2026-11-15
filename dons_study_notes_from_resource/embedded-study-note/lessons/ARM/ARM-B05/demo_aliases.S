/* ARM-B05 demo: 별칭(alias) 명령이 실제로 어떤 명령으로 어셈블되는지, B.cond 의 cond 비트 보기
 *   cc -c demo_aliases.S -o /tmp/ARM-B05_alias.o
 *   objdump -d /tmp/ARM-B05_alias.o                 (별칭으로 표시)
 *   objdump -d -M no-aliases /tmp/ARM-B05_alias.o   (진짜 명령으로 표시)
 */
    .text
    .p2align 2
aliases:
    cmp     x0, x1              // = subs xzr, x0, x1
    cmp     x0, #-5             // 음수 즉시값 → 어셈블러가 cmn x0, #5 로 바꾼다
    cmn     x0, #1              // = adds xzr, x0, #1  (x0 == -1 ?)
    tst     w9, #0x60           // = ands wzr, w9, #0x60
    cset    w0, pl              // = csinc w0, wzr, wzr, mi  (조건 반전!)
    csetm   x0, lt              // = csinv x0, xzr, xzr, ge
    cinc    x8, x8, lt          // = csinc x8, x8, x8, ge
    cinv    x0, x0, lt          // = csinv x0, x0, x0, ge
    cneg    x0, x0, lt          // = csneg x0, x0, x0, ge
    mov     x0, #-1             // = movn x0, #0
conds:
    b.eq    done
    b.ne    done
    b.hs    done
    b.lo    done
    b.mi    done
    b.pl    done
    b.vs    done
    b.vc    done
    b.hi    done
    b.ls    done
    b.ge    done
    b.lt    done
    b.gt    done
    b.le    done
    cbz     x0, done
    cbnz    w0, done
    tbz     x0, #63, done
    tbnz    w9, #5, done
done:
    ret
