
/*

카테고리 3: 하드웨어 추상화 및 디바이스 드라이버 (55~7)

55. 하드웨어 추상화 계층(Hardware Abstraction Layer, HAL)이란 무엇이며, 왜 중요한가요?
56. (설계) 여러 마이크로컨트롤러 플랫폼에서 호환되는 GPIO 드라이버를 위한 HAL API를 설계하세요. (예: gpio_init, gpio_set_direction, gpio_write, gpio_read)
57. 메모리 맵 I/O(Memory-Mapped I/O)란 무엇인가요?
58. (코딩) I2C 프로토콜을 사용하여 특정 슬레이브 주소(device address)의 특정 레지스터(register address)에 데이터를 쓰는 함수 i2c_write_register(device_addr, reg_addr, data)를 (의사 코드 또는 C로) 구현하세요.
59. I2C와 SPI 통신 프로토콜의 주요 차이점은 무엇인가요?
60. DMA(Direct Memory Access)란 무엇이며, 카메라 시스템에서 센서로부터 이미지 데이터를 수신할 때 DMA를 사용하면 어떤 이점이 있나요?
61. (설계) UART 수신 드라이버를 인터럽트와 순환 버퍼를 사용하여 설계하세요. 데이터 수신은 ISR에서 처리하고, 애플리케이션은 버퍼에서 데이터를 읽어갑니다.
62. (코딩) 주어진 16비트 값의 상위 바이트와 하위 바이트를 교환하는 매크로 SWAP_BYTES(x)를 작성하세요.
63. 카메라의 자동 초점(AF)에 사용되는 VCM(Voice Coil Motor)은 일반적으로 어떻게 제어되나요? (예: PWM, DAC, I2C 드라이버 IC)
64. (설계) I2C를 통해 제어되는 VCM 드라이버 IC를 위한 간단한 드라이버 API를 설계하세요. (예: vcm_init(), vcm_set_position(uint16_t pos))
65. 인터럽트 핸들러(ISR) 작성 시 주의해야 할 점은 무엇인가요? (예: 짧게 유지, 비재진입 함수 호출 금지 등)
66. (코딩) 특정 GPIO 핀에 연결된 LED를 1초에 한 번씩 토글하는 코드를 타이머 인터럽트를 사용하여 작성하세요. main 루프는 비어 있어야 합니다.
67. 디바운싱(Debouncing)이란 무엇이며, 기계식 버튼 입력을 처리할 때 왜 필요한가요? 소프트웨어로 디바운싱을 구현하는 방법을 설명하세요.
68. (설계) MIPI CSI-2 인터페이스를 통해 들어오는 비디오 스트림을 처리하기 위한 버퍼 관리 전략을 설명하세요. (예: 핑퐁 버퍼, 링 버퍼)
69. 플래시 메모리와 RAM의 차이점은 무엇이며, 펌웨어 코드는 일반적으로 어디에 저장되나요?
70. 부트로더(Bootloader)의 역할은 무엇인가요?
71. (코딩) SPI 마스터 디바이스로서 슬레이브 디바이스와 한 바이트를 주고받는 함수 spi_transfer(uint8_t tx_data)를 구현하세요.
72. 전력 관리 기법(예: 클럭 게이팅, 슬립 모드)에 대해 설명하고, 배터리로 동작하는 카메라 시스템에서 이를 어떻게 활용할 수 있을지 설명하세요.
73. (설계) ADC(Analog-to-Digital Converter)를 사용하여 배터리 전압을 주기적으로 측정하는 모듈을 설계하세요. 측정된 값이 특정 임계값 아래로 떨어지면 시스템에 알려야 합니다.
74. JTAG, SWD와 같은 하드웨어 디버깅 인터페이스는 어떤 용도로 사용되나요.

*/

/*

카테고리 3: 하드웨어 추상화 및 디바이스 드라이버 (55~74)
각 문제는 함수 시그니처, 설명, 개념, 샘플 입력/출력, 배경 개념을 포함합니다.

55. // 하드웨어 추상화 계층(HAL)이란?
// 설명: HAL의 개념과 중요성을 설명하는 함수
// 개념: 플랫폼 독립성, 코드 재사용
// 함수 시그니처:
void explain_hal(void);
// 샘플 입력/출력: (출력) "HAL은 하드웨어 의존성을 추상화하여 코드 이식성 향상"

56. // GPIO HAL API 설계
// 설명: 여러 MCU에서 호환 가능한 GPIO HAL API 설계
// 개념: 추상화, 인터페이스 설계
// 함수 시그니처:
typedef enum { GPIO_INPUT, GPIO_OUTPUT } gpio_dir_t;
void gpio_init(int pin);
void gpio_set_direction(int pin, gpio_dir_t dir);
void gpio_write(int pin, int value);
int  gpio_read(int pin);
// 샘플 입력: gpio_init(5); gpio_set_direction(5, GPIO_OUTPUT); gpio_write(5, 1);
// 샘플 출력: 핀 5가 출력으로 설정되고 HIGH 출력

57. // 메모리 맵 I/O란?
// 설명: 메모리 맵 I/O의 개념과 장단점 설명
// 개념: 주소 공간, 하드웨어 레지스터 접근
// 함수 시그니처:
void explain_memory_mapped_io(void);
// 샘플 입력/출력: (출력) "특정 주소에 쓰면 하드웨어 동작"

58. // I2C 레지스터 쓰기 함수 구현
// 설명: I2C 슬레이브의 특정 레지스터에 데이터를 쓰는 함수 구현
// 개념: 시리얼 통신, 프로토콜
// 함수 시그니처:
int i2c_write_register(uint8_t dev_addr, uint8_t reg_addr, uint8_t data);
// 샘플 입력: i2c_write_register(0x50, 0x10, 0xAB);
// 샘플 출력: 0 (성공), -1 (실패)

59. // I2C와 SPI의 차이점
// 설명: I2C와 SPI의 주요 차이점 설명
// 개념: 동기식 통신, 버스 구조, 속도
// 함수 시그니처:
void explain_i2c_vs_spi(void);
// 샘플 입력/출력: (출력) "I2C는 2선식, SPI는 4선식, 속도/구조 차이"

60. // DMA란? 장점은?
// 설명: DMA의 개념과 이미지 데이터 수신 시 장점 설명
// 개념: CPU 오프로드, 고속 데이터 전송
// 함수 시그니처:
void explain_dma(void);
// 샘플 입력/출력: (출력) "DMA는 CPU 개입 없이 데이터 이동"

61. // UART 수신 드라이버 설계
// 설명: 인터럽트와 순환 버퍼를 이용한 UART 수신 드라이버 설계
// 개념: 인터럽트, 버퍼, 동기화
// 함수 시그니처:
void uart_rx_isr(void); // ISR에서 호출
int uart_read_byte(uint8_t* byte); // 애플리케이션에서 호출
// 샘플 입력: ISR에서 0x41 수신, uart_read_byte(&b) 호출
// 샘플 출력: b==0x41

62. // 16비트 바이트 스왑 매크로
// 설명: 16비트 값의 상위/하위 바이트를 교환하는 매크로 작성
// 개념: 비트 연산, 엔디안 변환
// 매크로:
#define SWAP_BYTES(x) ( ((x) >> 8) | ((x) << 8) )
// 샘플 입력: SWAP_BYTES(0x1234)
// 샘플 출력: 0x3412

63. // VCM(Voice Coil Motor) 제어 방식
// 설명: 카메라 AF용 VCM의 일반적 제어 방식 설명
// 개념: PWM, DAC, I2C
// 함수 시그니처:
void explain_vcm_control(void);
// 샘플 입력/출력: (출력) "VCM은 I2C, PWM, DAC로 제어"

64. // VCM 드라이버 API 설계
// 설명: I2C 제어 VCM 드라이버 API 설계
// 개념: 추상화, 하드웨어 제어
// 함수 시그니처:
void vcm_init(void);
int  vcm_set_position(uint16_t pos);
// 샘플 입력: vcm_init(); vcm_set_position(100);
// 샘플 출력: 0 (성공)

65. // ISR 작성 시 주의점
// 설명: ISR 작성 시 주의점(짧게, 비재진입 함수 금지 등) 설명
// 개념: 인터럽트, 실시간성
// 함수 시그니처:
void explain_isr_cautions(void);
// 샘플 입력/출력: (출력) "ISR은 짧고 간결하게, 블로킹 금지"

66. // 타이머 인터럽트로 LED 토글
// 설명: 타이머 인터럽트로 1초마다 LED 토글 코드 작성 (main 루프는 비어 있음)
// 개념: 인터럽트, 하드웨어 제어
// 함수 시그니처:
void timer_isr(void); // 1초마다 호출
void led_init(void);
// 샘플 입력: timer_isr() 2회 호출
// 샘플 출력: LED ON → OFF → ON

67. // 디바운싱이란? 소프트웨어 구현법
// 설명: 디바운싱 개념과 소프트웨어 구현 방법 설명
// 개념: 노이즈 제거, 타이머
// 함수 시그니처:
void explain_debouncing(void);
// 샘플 입력/출력: (출력) "연속 입력 무시, 타이머로 안정화"

68. // MIPI CSI-2 버퍼 관리 전략
// 설명: MIPI CSI-2 비디오 스트림 버퍼 관리 전략 설명
// 개념: 핑퐁 버퍼, 링 버퍼
// 함수 시그니처:
void explain_mipi_buffering(void);
// 샘플 입력/출력: (출력) "핑퐁/링 버퍼로 프레임 손실 방지"

69. // 플래시 메모리 vs RAM
// 설명: 플래시와 RAM의 차이, 펌웨어 저장 위치 설명
// 개념: 비휘발성, 속도
// 함수 시그니처:
void explain_flash_vs_ram(void);
// 샘플 입력/출력: (출력) "플래시는 비휘발성, 펌웨어 저장"

70. // 부트로더 역할
// 설명: 부트로더의 역할 설명
// 개념: 초기화, 펌웨어 로딩
// 함수 시그니처:
void explain_bootloader(void);
// 샘플 입력/출력: (출력) "부트로더가 펌웨어 로딩/업데이트"

71. // SPI 마스터 바이트 전송 함수
// 설명: SPI 마스터로 슬레이브와 한 바이트 송수신 함수 구현
// 개념: 동기식 통신, 시프트 레지스터
// 함수 시그니처:
uint8_t spi_transfer(uint8_t tx_data);
// 샘플 입력: spi_transfer(0xA5)
// 샘플 출력: (예시) 0x5A (슬레이브 응답)

72. // 전력 관리 기법과 활용
// 설명: 클럭 게이팅, 슬립 모드 등 전력 관리 기법과 카메라 시스템 적용법 설명
// 개념: 저전력, 배터리 수명
// 함수 시그니처:
void explain_power_management(void);
// 샘플 입력/출력: (출력) "슬립 모드로 대기 전력 절감"

73. // ADC로 배터리 전압 측정 모듈 설계
// 설명: ADC로 배터리 전압 측정, 임계값 이하 알림 설계
// 개념: 아날로그-디지털 변환, 임계값 비교
// 함수 시그니처:
void adc_init(void);
uint16_t adc_read_voltage(void);
void check_battery_voltage(void);
// 샘플 입력: adc_read_voltage() → 3200mV
// 샘플 출력: 임계값 이하 시 경고 출력

74. // JTAG, SWD 디버깅 인터페이스 용도
// 설명: JTAG, SWD의 용도와 장점 설명
// 개념: 하드웨어 디버깅, 브레이크포인트
// 함수 시그니처:
void explain_jtag_swd(void);
// 샘플 입력/출력: (출력) "JTAG/SWD로 실시간 디버깅, 메모리 접근"

*/