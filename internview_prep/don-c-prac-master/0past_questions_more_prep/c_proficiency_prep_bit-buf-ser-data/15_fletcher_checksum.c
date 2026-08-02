
// 15. Fletcher-16/32 체크섬
// 설명: 바이트/워드 스트림에 대해 Fletcher-16 또는 Fletcher-32 체크섬 계산.
// 요구사항:
//   - 스트리밍(누적) 처리
//   - 표준 레퍼런스 벡터와 일치
//   - 성능/코드크기 트레이드오프(루프/언롤 등)
#ifndef FLETCHER_H
#define FLETCHER_H
#include <stddef.h>
#include <stdint.h>
uint16_t fletcher16(const uint8_t* data, size_t len, uint16_t sum1_init, uint16_t sum2_init);
uint32_t fletcher32(const uint16_t* data, size_t len, uint32_t sum1_init, uint32_t sum2_init);
#endif
