/* TOOL-B14 main.c — 링크 실험용 (target.c 와 같이 링크). 결과: checksum({1,2,3,4}) = 2 → exit 2 */
#include <stdint.h>
uint32_t checksum(const uint8_t *p, uint32_t n);
static const uint8_t msg[4] = {1, 2, 3, 4};
int main(void) { return (int)(checksum(msg, 4) & 0x7f); }
