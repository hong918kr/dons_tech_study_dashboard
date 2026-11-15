/* adc.h — ADC 드라이버 공개 API */
#ifndef ADC_H
#define ADC_H
int adc_read_mv(void);           /* 밀리볼트 (3300 mV / 4096 counts) */
int adc_read_raw(void);          /* 원시 카운트 0..4095 — 이제 공개 API */
#endif
