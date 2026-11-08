/* C-M15 demo — 같은 4단계 init 을 세 가지 스타일로: Cortex-M4 에서 코드가 어떻게 다른가
 * 타깃 전용 (freestanding 헤더만):
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_codegen.c -o -
 *   clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -c demo_codegen.c -o /tmp/cg.o
 *   xcrun llvm-nm --size-sort -S /tmp/cg.o
 */
int  clk_enable(void);   void clk_disable(void);
int  gpio_request(void); void gpio_free(void);
int  dma_alloc(void);    void dma_free(void);
int  irq_request(void);  void irq_free(void);

int init_goto(void) {
    int ret;
    ret = clk_enable();   if (ret < 0) goto err_out;
    ret = gpio_request(); if (ret < 0) goto err_clk;
    ret = dma_alloc();    if (ret < 0) goto err_gpio;
    ret = irq_request();  if (ret < 0) goto err_dma;
    return 0;
err_dma:  dma_free();
err_gpio: gpio_free();
err_clk:  clk_disable();
err_out:  return ret;
}

int init_nested(void) {
    int ret = clk_enable();
    if (ret >= 0) {
        ret = gpio_request();
        if (ret >= 0) {
            ret = dma_alloc();
            if (ret >= 0) {
                ret = irq_request();
                if (ret < 0) dma_free();
            }
            if (ret < 0) gpio_free();
        }
        if (ret < 0) clk_disable();
    }
    return ret < 0 ? ret : 0;
}

int init_staged(void) {
    int done = 0, ret = 0;
    if (ret >= 0) { ret = clk_enable();   if (ret >= 0) done = 1; }
    if (ret >= 0) { ret = gpio_request(); if (ret >= 0) done = 2; }
    if (ret >= 0) { ret = dma_alloc();    if (ret >= 0) done = 3; }
    if (ret >= 0) { ret = irq_request();  if (ret >= 0) done = 4; }
    if (ret < 0) {
        switch (done) {
        case 3: dma_free();    __attribute__((fallthrough));
        case 2: gpio_free();   __attribute__((fallthrough));
        case 1: clk_disable(); break;
        default: break;
        }
        return ret;
    }
    return 0;
}
