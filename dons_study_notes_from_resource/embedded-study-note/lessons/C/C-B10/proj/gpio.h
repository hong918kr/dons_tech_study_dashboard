/* gpio.h */
#ifndef GPIO_H
#define GPIO_H
void     gpio_init(void);
void     gpio_write(unsigned pin, int level);
unsigned gpio_state(void);
#endif
