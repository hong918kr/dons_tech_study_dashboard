/* expect: preprocess — #if without #endif */
#define USE_DMA 1
#if USE_DMA
static int dma_ready(void) { return 1; }
int main(void) { return !dma_ready(); }
