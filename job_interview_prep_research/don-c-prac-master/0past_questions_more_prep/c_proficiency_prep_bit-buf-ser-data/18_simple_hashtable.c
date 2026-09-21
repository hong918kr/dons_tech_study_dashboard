
// 18. 간단 해시 테이블(오픈 어드레싱)
// 설명: 고정 용량, 정수 키 기반의 해시 테이블(선형/이차 탐사).
// 요구사항:
//   - 삭제 마커(tombstone)
//   - 로드팩터 계산 및 문서화
#ifndef SIMPLE_HT_H
#define SIMPLE_HT_H
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t key; uint32_t value; int used; int tombstone; } ht_entry_t;
typedef struct { ht_entry_t* table; size_t capacity; size_t size; } simple_ht_t;
void simple_ht_init(simple_ht_t* ht, size_t capacity);
void simple_ht_free(simple_ht_t* ht);
int  simple_ht_put(simple_ht_t* ht, uint32_t key, uint32_t value);
int  simple_ht_get(const simple_ht_t* ht, uint32_t key, uint32_t* value);
int  simple_ht_del(simple_ht_t* ht, uint32_t key);
float simple_ht_load_factor(const simple_ht_t* ht);
#endif
