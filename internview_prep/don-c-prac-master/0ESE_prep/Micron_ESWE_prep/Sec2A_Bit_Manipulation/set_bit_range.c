/*

샘플 문제 1 (초급/중급): 
"32비트 레지스터 값에서 특정 비트 범위(start_bit부터 end_bit까지)를 새로운 값으로 설정하는 C 함수
 uint32_t set_bit_range(uint32_t reg_val, uint8_t start_bit, uint8_t end_bit, uint32_t new_val)를 작성하시오. 
 예를 들어, set_bit_range(0xAABBCCDD, 8, 15, 0xEF)는 원래 값의 8번부터 15번 비트를 수정해야 합니다.".
*/
#include <stdint.h>

uint32_t set_bit_range(uint32_t reg_val, uint8_t start_bit, uint8_t end_bit, uint32_t new_val)
{
    if (start_bit > 31 || end_bit > 31 || start_bit > end_bit) return reg_val;
    uint32_t mask = ( (1U << (end_bit - start_bit +1)) - 1 ) << start_bit;
    
    uint32_t res = (reg_val & ~(mask)) | ((new_val) << start_bit) & mask;

    return res;
}

// uint32_t set_bit_range(uint32_t reg_val, uint8_t start_bit, uint8_t end_bit, uint32_t new_val)
// {
//     // 비트 범위 유효성 검사
//     if (start_bit > 31 || end_bit > 31 || start_bit > end_bit) {
//         return reg_val; // 유효하지 않은 입력 시 원래 값 반환
//     }

//     // 비트 마스크 생성
//     uint32_t mask = ((1U << (end_bit - start_bit + 1)) - 1) << start_bit;

//     // 새로운 값의 해당 비트 범위 추출 및 위치 조정
//     uint32_t new_val_shifted = (new_val << start_bit) & mask;

//     // 원래 값에서 해당 비트 범위 클리어 후 새로운 값 설정
//     reg_val = (reg_val & ~mask) | new_val_shifted;

//     return reg_val;


// }
#include <stdio.h>
int main(void)
{
    // 예제 사용
    uint32_t original_value = 0xAABBCCDD;
    uint8_t start_bit = 8;
    uint8_t end_bit = 15;
    uint32_t new_value = 0xEF;

    uint32_t modified_value = set_bit_range(original_value, start_bit, end_bit, new_value);

    // 결과 출력 (디버깅용)
    printf("Modified Value: 0x%X\n", modified_value); // 주석 처리된 출력문

    return 0;
}