/* ARM-A09 demo_ropi.c — Cortex-M4 에서 기본(절대 주소) vs -fropi -frwpi (위치 독립) 비교. demo_ropi.sh 참고 */
int counter;                                 /* RW 데이터 → RWPI 면 r9(static base) 기준 */
const int table[4] = {1, 2, 3, 4};           /* RO 데이터 → ROPI 면 PC 기준 */
int bump(int i) { return ++counter + table[i & 3]; }
