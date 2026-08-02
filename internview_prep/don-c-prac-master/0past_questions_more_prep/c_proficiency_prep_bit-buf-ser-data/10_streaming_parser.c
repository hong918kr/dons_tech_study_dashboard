/*
10. 스트리밍 가변길이 프레임 파서 상태기계 - 실제 인터뷰 스타일 상세 설명

상황 설명:
당신은 산업용 IoT 게이트웨이의 시리얼 통신 펌웨어를 개발 중입니다.
이 게이트웨이는 여러 센서와 액추에이터로부터 실시간으로 데이터를 수집하고,  
네트워크를 통해 서버 또는 다른 장치로 데이터를 전송합니다.

시리얼, CAN, 무선 등 바이트 스트림 기반 프로토콜에서는  
패킷(프레임) 경계를 명확히 구분해야 하며,  
특정 바이트(예: 0x7E)를 "프레임 시작/종료" 구분자로 사용합니다.

문제는, 실제 데이터에도 0x7E(프레임 구분자)나 0x7D(이스케이프 바이트)가 포함될 수 있다는 점입니다.
이를 해결하기 위해 "바이트 스터핑(byte stuffing)" 규칙을 적용합니다:
- 프레임 시작: 0x7E
- 데이터 중 0x7E → [0x7D, 0x5E]로 변환
- 데이터 중 0x7D → [0x7D, 0x5D]로 변환
- 프레임 끝: 다음 0x7E가 나오면 프레임 종료(길이 제한, 오류 복구 등도 필요)

이런 파서는 실시간 스트림에서 바이트 단위로 입력을 받아  
프레임 단위로 콜백을 호출해야 하며,  
부분 입력, 경계 분할, 오류 복구, 상태 리셋 등도 지원해야 합니다.

면접관이 평가하고자 하는 포인트:
- 상태기계 설계 및 구현 능력
- 바이트 스터핑/이스케이프 처리의 정확성
- 경계/오버런/오류 복구 처리
- API 설계 및 테스트 케이스 작성 능력

문제:
아래 시그니처에 따라 스트리밍 프레임 파서를 구현하세요.
- frame_parser_create(frame_callback_t cb, void* user)
- frame_parser_destroy(frame_parser_t* parser)
- frame_parser_feed(frame_parser_t* parser, const uint8_t* data, size_t len)
- frame_parser_reset(frame_parser_t* parser)

아래는 구현 예시와 테스트 코드입니다.
*/

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#define FRAME_MAX_LEN 256

typedef void (*frame_callback_t)(const uint8_t* frame, size_t len, void* user);

typedef enum {
    FP_IDLE,
    FP_IN_FRAME,
    FP_ESC
} fp_state_t;

typedef struct frame_parser {
    frame_callback_t cb;
    void* user;
    uint8_t buf[FRAME_MAX_LEN];
    size_t pos;
    fp_state_t state;
} frame_parser_t;

frame_parser_t* frame_parser_create(frame_callback_t cb, void* user) {
    if (!cb) return NULL;
    frame_parser_t* p = (frame_parser_t*)malloc(sizeof(frame_parser_t));
    if (!p) return NULL;
    p->cb = cb;
    p->user = user;
    p->pos = 0;
    p->state = FP_IDLE;
    return p;
}

void frame_parser_destroy(frame_parser_t* parser) {
    if (parser) free(parser);
}

void frame_parser_reset(frame_parser_t* parser) {
    if (!parser) return;
    parser->pos = 0;
    parser->state = FP_IDLE;
}

void frame_parser_feed(frame_parser_t* parser, const uint8_t* data, size_t len) {
    if (!parser || !data) return;
    for (size_t i = 0; i < len; ++i) {
        uint8_t b = data[i];
        switch (parser->state) {
            case FP_IDLE:
                if (b == 0x7E) {
                    parser->pos = 0;
                    parser->state = FP_IN_FRAME;
                }
                break;
            case FP_IN_FRAME:
                if (b == 0x7E) {
                    // 프레임 종료
                    if (parser->pos > 0) {
                        parser->cb(parser->buf, parser->pos, parser->user);
                    }
                    parser->pos = 0;
                    // 다음 프레임 시작
                    parser->state = FP_IN_FRAME;
                } else if (b == 0x7D) {
                    parser->state = FP_ESC;
                } else {
                    if (parser->pos < FRAME_MAX_LEN) {
                        parser->buf[parser->pos++] = b;
                    } else {
                        // 오버런: 프레임 무시, 상태 리셋
                        parser->pos = 0;
                        parser->state = FP_IDLE;
                    }
                }
                break;
            case FP_ESC:
                if (b == 0x5E) b = 0x7E;
                else if (b == 0x5D) b = 0x7D;
                // else: 잘못된 이스케이프, 그대로 저장(혹은 오류 복구)
                if (parser->pos < FRAME_MAX_LEN) {
                    parser->buf[parser->pos++] = b;
                    parser->state = FP_IN_FRAME;
                } else {
                    parser->pos = 0;
                    parser->state = FP_IDLE;
                }
                break;
        }
    }
}

// 테스트 콜백
typedef struct {
    uint8_t frames[4][FRAME_MAX_LEN];
    size_t lens[4];
    int count;
} test_ctx_t;

void test_cb(const uint8_t* frame, size_t len, void* user) {
    test_ctx_t* ctx = (test_ctx_t*)user;
    assert(ctx->count < 4);
    memcpy(ctx->frames[ctx->count], frame, len);
    ctx->lens[ctx->count] = len;
    ctx->count++;
}

// 테스트 코드
int main() {
    frame_parser_t* parser;
    test_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    parser = frame_parser_create(test_cb, &ctx);
    assert(parser);

    // 1. 단순 프레임
    uint8_t test1[] = {0x7E, 0x01, 0x02, 0x03, 0x7E};
    frame_parser_feed(parser, test1, sizeof(test1));
    assert(ctx.count == 1 && ctx.lens[0] == 3);
    assert(ctx.frames[0][0] == 0x01 && ctx.frames[0][2] == 0x03);

    // 2. 이스케이프 포함 프레임
    uint8_t test2[] = {0x7E, 0x11, 0x7D, 0x5E, 0x22, 0x7D, 0x5D, 0x33, 0x7E};
    frame_parser_feed(parser, test2, sizeof(test2));
    assert(ctx.count == 2 && ctx.lens[1] == 5);
    assert(ctx.frames[1][0] == 0x11 && ctx.frames[1][1] == 0x7E && ctx.frames[1][3] == 0x7D);

    // 3. 연속 프레임, 경계 분할
    uint8_t test3a[] = {0x7E, 0xAA, 0xBB};
    uint8_t test3b[] = {0xCC, 0x7E, 0x7E, 0xDD, 0x7E};
    frame_parser_feed(parser, test3a, sizeof(test3a));
    frame_parser_feed(parser, test3b, sizeof(test3b));
    assert(ctx.count == 4);
    assert(ctx.lens[2] == 3 && ctx.frames[2][0] == 0xAA && ctx.frames[2][2] == 0xCC);
    assert(ctx.lens[3] == 1 && ctx.frames[3][0] == 0xDD);

    // 4. 오버런/에러 복구
    frame_parser_reset(parser);
    ctx.count = 0;
    uint8_t big[FRAME_MAX_LEN+10];
    big[0] = 0x7E;
    memset(big+1, 0x55, FRAME_MAX_LEN+8);
    big[FRAME_MAX_LEN+9] = 0x7E;
    frame_parser_feed(parser, big, FRAME_MAX_LEN+10);
    // 오버런 발생: 프레임 무시, 콜백 호출 안 됨
    assert(ctx.count == 0);

    frame_parser_destroy(parser);
    printf("All streaming frame parser tests passed!\n");
    return 0;
}