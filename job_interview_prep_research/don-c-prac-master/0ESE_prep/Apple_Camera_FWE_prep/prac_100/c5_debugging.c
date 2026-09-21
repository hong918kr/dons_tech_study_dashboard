
/*

카테고리 5: 코드 리뷰 및 디버깅 (90~100)
이 섹션은 다른 사람의 코드를 읽고 잠재적인 버그나 개선점을 찾는 능력을 평가합니다. 실제 코드 조각이 주어질 가능성이 높습니다.

90. (코드 리뷰) 아래 코드의 문제점을 찾고 수정하세요. 두 개의 다른 태스크에서 이 함수를 호출할 수 있습니다.
C
uint32_t shared_counter = 0;
void increment_counter() {
    shared_counter++;
}

91. (코드 리뷰) 아래 ISR 코드의 잠재적인 문제점은 무엇인가요?
C
void timer_isr_handler() {
    // 매우 복잡하고 오래 걸리는 연산 수행
    long_running_calculation(); 
    printf("Timer interrupt occurred!\n");
}

92. (코드 리뷰) 아래 코드에서 temperature 변수에 volatile 키워드가 필요한 이유를 설명하세요.
C
// 이 변수는 온도 센서 DMA에 의해 백그라운드에서 업데이트됨
uint16_t temperature; 

void check_overheat() {
    while (temperature < 100) {
        // 대기
    }
    shutdown_system();
}

93. (코드 리뷰) 아래 C++ 코드의 문제점은 무엇이며, 어떻게 수정해야 할까요?
C++
void process_data(char* data) {
    char buffer[1];
    strcpy(buffer, data);
    //... buffer 처리...
}

94. (디버깅) 간헐적으로 발생하는 시스템 오작동(intermittent fault)을 디버깅하기 위한 접근 전략을 설명하세요. 어떤 도구(오실로스코프, 로직 분석기 등)를 사용하시겠습니까?

95. (코드 리뷰) 아래의 두 태스크가 교착 상태에 빠질 수 있는 시나리오를 설명하세요.
C
// Task 1
lock(mutex_A);
lock(mutex_B);
//...
unlock(mutex_B);
unlock(mutex_A);

// Task 2
lock(mutex_B);
lock(mutex_A);
//...
unlock(mutex_A);
unlock(mutex_B);

96. (디버깅) 펌웨어 업데이트 후 일부 기기에서만 부팅이 되지 않는 문제가 발생했습니다. 원인을 찾기 위해 어떤 단계를 밟으시겠습니까?

97. (코드 리뷰) 아래 코드에서 발생할 수 있는 스택 오버플로우의 원인은 무엇인가요?
C
void recursive_function(int depth) {
    char large_array;
    //...
    if (depth > 0) {
        recursive_function(depth - 1);
    }
}

98. (디버깅) JTAG 디버거를 사용하여 하드 폴트(Hard Fault)가 발생했을 때, 원인을 분석하는 일반적인 과정을 설명하세요.

99. (코드 리뷰) 아래의 포인터 사용 코드에서 잠재적인 위험은 무엇인가요?
C
char* create_message() {
    char message = "Hello, World!";
    return message;
}

100. (디버깅) I2C 통신이 실패할 때, 문제의 원인이 하드웨어적인지 소프트웨어적인지 어떻게 판단하시겠습니까? 당신의 디버깅 과정을 단계별로 설명해주세요.

*/
