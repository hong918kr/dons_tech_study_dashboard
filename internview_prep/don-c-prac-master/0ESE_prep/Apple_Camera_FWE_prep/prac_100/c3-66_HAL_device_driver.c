
//66. // 타이머 인터럽트로 LED 토글
// 설명: 타이머 인터럽트로 1초마다 LED 토글 코드 작성 (main 루프는 비어 있음)
// 개념: 인터럽트, 하드웨어 제어
// 함수 시그니처:
void timer_isr(void); // 1초마다 호출
void led_init(void);
// 샘플 입력: timer_isr() 2회 호출
// 샘플 출력: LED ON → OFF → ON
