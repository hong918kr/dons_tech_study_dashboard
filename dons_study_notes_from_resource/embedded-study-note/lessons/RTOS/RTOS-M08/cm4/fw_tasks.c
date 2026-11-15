/* RTOS-M08 — Cortex-M4F 전용: fw_graph.c 의 함수를 진짜 FreeRTOS 태스크 루프로 감싼다 (스택 분석용, 링크 안 함).
 * 태스크 진입 함수의 프레임과 "커널 API 호출도 태스크 스택을 쓴다" 를 .su 로 보기 위한 파일.
 * 빌드: bash cm4/kernel_su.sh (FreeRTOS V11.3.2 헤더 + cm4/FreeRTOSConfig.h)
 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "fw_graph.h"

QueueHandle_t g_telem_q;                 /* ctrl → comms: 자세 스냅샷 */
TaskHandle_t  g_rx_task;                 /* isr_uart_rx_rtos 가 알림을 준다 */
static ctl_state_t s_ctl;

void ctrl_task(void *arg) {              /* 1 kHz 자세 제어 (FPU 사용) */
    (void)arg;
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        control_step(&s_ctl);
        (void)xQueueSend(g_telem_q, &s_ctl.att, 0);            /* 꽉 차면 버린다 (0 tick) */
        (void)xTaskDelayUntil(&last, pdMS_TO_TICKS(1));
    }
}

void comms_task(void *arg) {             /* 텔레메트리 송신 (float 곱셈 하나 때문에 FPU 문맥이 생긴다) */
    (void)arg;
    att_t a;
    uint8_t seq = 0;
    for (;;) {
        if (xQueueReceive(g_telem_q, &a, portMAX_DELAY) == pdPASS) {
            s_ctl.att = a;
            comms_send_telemetry(&s_ctl, seq++);
        }
    }
}

void rx_task(void *arg) {                /* 명령 수신: ISR 알림 → 프레임 파싱 (FPU 안 씀) */
    (void)arg;
    for (;;) {
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        while (rx_parse_once() != -1) { }
    }
}

void USART2_IRQHandler(void) {           /* 진짜 ISR 모양: 바이트 저장 + 태스크 알림 + yield 요청 */
    BaseType_t woken = pdFALSE;
    isr_uart_rx();
    vTaskNotifyGiveFromISR(g_rx_task, &woken);
    portYIELD_FROM_ISR(woken);
}
