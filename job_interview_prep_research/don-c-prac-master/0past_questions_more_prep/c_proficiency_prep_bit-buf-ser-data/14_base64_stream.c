
// 14. 베이스64 인코더/디코더(스트리밍)
// 설명: MIME/Base64 인코딩 및 디코딩, 부분 입력 지원.
// 요구사항:
//   - 라인 래핑 옵션(76자 등)
//   - 패딩 처리("=")
//   - 스트리밍(부분 입력/출력) 지원
#ifndef BASE64_H
#define BASE64_H
#include <stddef.h>
typedef struct base64_enc_ctx base64_enc_ctx_t;
typedef struct base64_dec_ctx base64_dec_ctx_t;
void base64_enc_init(base64_enc_ctx_t* ctx, int line_wrap);
size_t base64_encode_update(base64_enc_ctx_t* ctx, const uint8_t* in, size_t in_len, char* out, size_t out_len);
size_t base64_encode_final(base64_enc_ctx_t* ctx, char* out, size_t out_len);
void base64_dec_init(base64_dec_ctx_t* ctx);
size_t base64_decode_update(base64_dec_ctx_t* ctx, const char* in, size_t in_len, uint8_t* out, size_t out_len);
size_t base64_decode_final(base64_dec_ctx_t* ctx, uint8_t* out, size_t out_len);
#endif