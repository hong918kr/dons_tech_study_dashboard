/* expect: preprocess — the include file does not exist */
#include "board_pins.h"
int main(void) { return LED_PIN; }
