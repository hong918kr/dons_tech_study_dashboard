
// 20. 정적 배열 기반 벡터
// 설명: capacity 고정, size 가변의 정수 컨테이너(vector).
// 요구사항:
//   - 바운드 체크
//   - push/pop/insert/erase 연산
#ifndef STATIC_VEC_H
#define STATIC_VEC_H
#include <stddef.h>
#define STATIC_VEC_CAPACITY 64
typedef struct {
    int data[STATIC_VEC_CAPACITY];
    size_t size;
} static_vec_t;
void static_vec_init(static_vec_t* v);
int  static_vec_push(static_vec_t* v, int val);
int  static_vec_pop(static_vec_t* v, int* val);
int  static_vec_insert(static_vec_t* v, size_t idx, int val);
int  static_vec_erase(static_vec_t* v, size_t idx);
#endif