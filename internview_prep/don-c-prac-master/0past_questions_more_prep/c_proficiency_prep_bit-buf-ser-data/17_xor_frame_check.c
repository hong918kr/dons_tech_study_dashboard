
// 17. XOR 기반 간단 프레이밍 체크
// 설명: 헤더+페이로드+XOR 체크섬으로 프레임 무결성 검증.
// 요구사항:
//   - 경계 검사(길이, 최소/최대)
//   - 오류 검출 및 재동기화 루틴
#ifndef XOR_FRAME_H
#define XOR_FRAME_H
#include <stddef.h>
#include <stdint.h>
uint8_t xor_checksum(const uint8_t* data, size_t len);
int     xor_frame_verify(const uint8_t* frame, size_t len);
#endif
