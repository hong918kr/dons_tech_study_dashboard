/* Task 2 보너스 (재구성): ring buffer 에 쌓인 바이트에서 패킷 하나를 꺼낸다.
 * 프레임 형식 [추정]:  SYNC(0xAA) | LEN(1B) | PAYLOAD(LEN B) | CRC8(LEN + PAYLOAD)
 */
#ifndef PACKET_H
#define PACKET_H
#include "rb.h"

// --- api ---
#define PKT_SYNC       0xAAu
#define PKT_NEED_MORE  (-1)   /* 아직 완전한 패킷이 없다 (앞쪽 쓰레기만 버렸을 수 있음) */
#define PKT_TOO_BIG    (-2)   /* 패킷은 맞지만 out 버퍼가 작다 → 그 패킷은 버림 */

/* 성공: payload 길이(0..255)를 돌려주고 그 프레임을 rb 에서 제거, payload 를 out 에 복사 */
int extract_packet(rb_t *rb, uint8_t *out, size_t out_cap);
// --- end ---
#endif
