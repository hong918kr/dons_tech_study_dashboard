
// 16. Adler-32 체크섬
// 설명: zlib 호환 Adler-32 체크섬 계산.
// 요구사항:
//   - 모듈러 연산 최적화
//   - 부분(누적) 업데이트 지원
#ifndef ADLER32_H
#define ADLER32_H
#include <stddef.h>
#include <stdint.h>
uint32_t adler32(const uint8_t* data, size_t len, uint32_t adler_init);
#endif
