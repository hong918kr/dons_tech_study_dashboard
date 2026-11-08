/* C-A08 데모: 마스크가 서로 겹치지 않는가 — OR == XOR 는 마스크 2개일 때만 맞다.
 *   clang -std=c11 -fsyntax-only demo_mask_overlap.c
 * 같은 비트가 세 마스크에 있으면 XOR 에서 살아남아 "겹침 없음" 으로 통과한다. OR == 합 은 잡는다.
 */
_Static_assert((0x0Fu | 0x0Fu | 0x0Fu) == (0x0Fu ^ 0x0Fu ^ 0x0Fu), "XOR: three copies of 0x0F overlap");
_Static_assert((0x0Fu | 0x0Fu | 0x0Fu) == (0x0Fu + 0x0Fu + 0x0Fu), "sum: three copies of 0x0F overlap");
