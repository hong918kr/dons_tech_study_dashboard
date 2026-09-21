/*
Implement a simple UART packet parser FSM.
States: IDLE, SYNC, LENGTH, DATA, CHECKSUM. Serial communication protocol implementation.
*/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef enum {
    UART_IDLE,
    UART_SYNC,
    UART_LENGTH,
    UART_DATA,
    UART_CHECKSUM
} uart_state_t;

typedef struct {
    uart_state_t state;
    uint8_t length;
    uint8_t data[16];
    uint8_t data_idx;
    uint8_t checksum;
    bool packet_ready;
} uart_fsm_t;

// Initialize FSM
void uart_fsm_init(uart_fsm_t *fsm) {
    fsm->state = UART_IDLE;
    fsm->length = 0;
    fsm->data_idx = 0;
    fsm->checksum = 0;
    fsm->packet_ready = false;
}

// Call this function for each received byte
void uart_fsm_update(uart_fsm_t *fsm, uint8_t byte) {
    switch (fsm->state) {
        case UART_IDLE:
            if (byte == 0xAA) { // SYNC byte
                fsm->state = UART_SYNC;
            }
            break;
        case UART_SYNC:
            fsm->length = byte;
            fsm->data_idx = 0;
            fsm->checksum = 0;
            if (fsm->length > 0 && fsm->length <= sizeof(fsm->data)) {
                fsm->state = UART_LENGTH;
            } else {
                fsm->state = UART_IDLE;
            }
            break;
        case UART_LENGTH:
            fsm->data[fsm->data_idx++] = byte;
            fsm->checksum ^= byte;
            if (fsm->data_idx >= fsm->length) {
                fsm->state = UART_DATA;
            }
            break;
        case UART_DATA:
            if (byte == fsm->checksum) {
                fsm->packet_ready = true;
            }
            fsm->state = UART_IDLE;
            break;
        case UART_CHECKSUM:
            // Not used in this simple example
            fsm->state = UART_IDLE;
            break;
    }
}

// --- Test code ---
int main(void) {
    uart_fsm_t fsm;
    uart_fsm_init(&fsm);

    // Example packet: SYNC(0xAA), LENGTH(3), DATA(0x11, 0x22, 0x33), CHECKSUM(0x11^0x22^0x33=0x00)
    uint8_t packet[] = {0xAA, 3, 0x11, 0x22, 0x33, 0x00};
    int len = sizeof(packet) / sizeof(packet[0]);

    printf("Byte | State      | Packet Ready | Data\n");
    printf("----------------------------------------\n");
    for (int i = 0; i < len; ++i) {
        uart_fsm_update(&fsm, packet[i]);
        const char *state_str[] = {"IDLE", "SYNC", "LENGTH", "DATA", "CHECKSUM"};
        printf("0x%02X | %-10s | %d           | ", packet[i], state_str[fsm.state], fsm.packet_ready);
        if (fsm.packet_ready) {
            for (int j = 0; j < fsm.length; ++j)
                printf("%02X ", fsm.data[j]);
            printf("\n");
            fsm.packet_ready = false; // Reset for next packet
        } else {
            printf("\n");
        }
    }
    return 0;
}

/*
------------------ Example Input/Output ------------------

Byte | State      | Packet Ready | Data
----------------------------------------
0xAA | SYNC       | 0           | 
0x03 | LENGTH     | 0           | 
0x11 | LENGTH     | 0           | 
0x22 | LENGTH     | 0           | 
0x33 | DATA       | 0           | 
0x00 | IDLE       | 1           | 11 22 33 

----------------------------------------------------------
*/