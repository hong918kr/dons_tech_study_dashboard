/* board.h — board constants (one per hardware revision). */
#ifndef BOARD_H
#define BOARD_H

#define BOARD_NAME      "nucleo-sim"
#define UART_BAUD       115200u
#define SENSOR_RATE_HZ  100u
#define FILTER_TAPS     4u      /* moving-average length: changes struct mavg layout! */

#endif
